/*
  ==============================================================================

    BinauralRenderer.cpp
    Created: 5 Feb 2022 4:59:07pm
    Author:  sedur

  ==============================================================================
*/

#include "BinauralRenderer.h"

BinauralRenderer::BinauralRenderer()
	: m_order(0)
	, m_numAmbiChans(1)
	, m_numLsChans(0)
	, m_numHrirAdded(0)
	, m_blockSize(0)
	, m_sampleRate(0.0)
	, m_yaw(0.0f)
	, m_pitch(0.0f)
	, m_roll(0.0f)
	, m_enableRenderer(false)
	, m_enableDualBand(true)
	, m_enableRotation(true)
{
}

void BinauralRenderer::sendMsgToLogWindow(juce::String message)
{
	m_currentLogMessage += message + "\n";
	sendChangeMessage();  // broadcast change message to inform and update the editor
}

//void BinauralRenderer::processOscMessage(const OSCMessage& message)
//{
//	const float pi = MathConstants<float>::pi;
//
//	// HEAD TRACKING DATA - QUATERNIONS
//	if (message.size() == 4 && message.getAddressPattern() == "/rendering/quaternions")
//	{
//		// change message index order from 0,1,2,3 to match unity coordinates
//		float qW = message[0].getFloat32();
//		float qX = message[1].getFloat32();
//		float qY = message[3].getFloat32();
//		float qZ = message[2].getFloat32();
//
//		float roll, pitch, yaw;
//
//		// roll (x-axis rotation)
//		float sinp = 2.0f * (qW * qY - qZ * qX);
//
//		if (fabs(sinp) >= 1.0f)
//			roll = copysign(pi / 2, sinp) * (180.0f / pi); // use 90 degrees if out of range
//		else
//			roll = asin(sinp) * (180 / pi);
//
//		// pitch (y-axis rotation)
//		float sinr_cosp = 2.0f * (qW * qX + qY * qZ);
//		float cosr_cosp = 1.0f - 2.0f * (qX * qX + qY * qY);
//		pitch = atan2(sinr_cosp, cosr_cosp) * (180.0f / pi);
//
//		// yaw (z-axis rotation)
//		float siny_cosp = 2.0f * (qW * qZ + qX * qY);
//		float cosy_cosp = 1.0f - 2.0f * (qY * qY + qZ * qZ);
//		yaw = atan2(siny_cosp, cosy_cosp) * (180.0f / pi);
//
//		// sign change
//		roll = roll * -1.0f;
//		pitch = pitch * -1.0f;
//
//		setHeadTrackingData(roll, pitch, yaw);
//	}
//
//	if (message.size() == 1 && message.getAddressPattern() == "/rendering/setorder" && message[0].isInt32())
//	{
//		int order = message[0].getInt32();
//		setOrder(order);
//	}
//
//	if (message.size() == 3 && message.getAddressPattern() == "/rendering/htrpy")
//	{
//		float roll = message[0].getFloat32();
//		float pitch = message[1].getFloat32();
//		float yaw = message[2].getFloat32();
//
//		setHeadTrackingData(roll, pitch, yaw);
//	}
//}

void BinauralRenderer::setOrder(const int order)
{
	juce::ScopedLock lock(m_procLock);

	m_order = order;
	m_numAmbiChans = (order + 1) * (order + 1);

	sendMsgToLogWindow("switched to: " + juce::String(m_order) + " order");
}

int BinauralRenderer::getOrder() const
{
	return m_order;
}

void BinauralRenderer::clearVirtualLoudspeakers()
{
	juce::ScopedLock lock(m_procLock);

	m_azi.clear();
	m_ele.clear();
	m_numLsChans = 0;
}

void BinauralRenderer::setVirtualLoudspeakers(const std::vector<float>& azi, const std::vector<float>& ele, const int chans)
{
	juce::ScopedLock lock(m_procLock);

	m_azi = azi;
	m_ele = ele;
	m_numLsChans = chans;
}

void BinauralRenderer::getVirtualLoudspeakers(std::vector<float>& azi, std::vector<float>& ele, int& chans)
{
	azi = m_azi;
	ele = m_ele;
	chans = m_numLsChans;
}

void BinauralRenderer::setDecodingMatrix(std::vector<float>& decodeMatrix)
{
	juce::ScopedLock lock(m_procLock);

	m_basicDecodeMatrix = decodeMatrix;
}

void BinauralRenderer::updateMatrices()
{
	juce::ScopedLock lock(m_procLock);

	m_basicDecodeTransposeMatrix.resize(m_numAmbiChans * m_numLsChans);
	mat_trans(m_basicDecodeTransposeMatrix.data(), m_basicDecodeMatrix.data(), m_numLsChans, m_numAmbiChans);
}

void BinauralRenderer::setHeadTrackingData(float roll, float pitch, float yaw)
{
	// swap the rotation direction
	m_roll = -roll;
	m_pitch = -pitch;
	m_yaw = -yaw;

	//m_headTrackRotator.updateEulerRPY(m_roll, m_pitch, m_yaw);
	m_headTrackRotator.updateEulerYPR(m_yaw, -m_pitch, m_roll);
}

float BinauralRenderer::getRoll()
{
	return m_roll;
}

float BinauralRenderer::getPitch()
{
	return m_pitch;
}

float BinauralRenderer::getYaw()
{
	return m_yaw;
}

bool BinauralRenderer::isRendererEnabled()
{
	return m_enableRenderer;
}

void BinauralRenderer::enableRenderer(bool enable)
{
	m_enableRenderer = enable;
}

void BinauralRenderer::enableDualBand(bool enable)
{
	m_enableDualBand = enable;
	//uploadHRIRsToEngine();
}

void BinauralRenderer::enableRotation(bool enable)
{
	m_enableRotation = enable;
}

void BinauralRenderer::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
	if (sampleRate != m_sampleRate)
	{
		m_sampleRate = sampleRate;
	}

	if (samplesPerBlockExpected != m_blockSize)
	{
		m_blockSize = samplesPerBlockExpected;

		m_workingBuffer.setSize(64, m_blockSize);
		m_convBuffer.setSize(2, m_blockSize);
	}
}

void BinauralRenderer::processBlock(juce::AudioBuffer<float>& buffer)
{
	juce::ScopedLock lock(m_procLock);

	if (!m_enableRenderer || buffer.getNumChannels() <= 2)
		return;

	// TODO: Re-implement dualband filter
	// 
	//if (m_enableDualBand)
	//	m_dbFilter.process(buffer); // preprocess buffer with shelf filters

	if (m_enableRotation)
		m_headTrackRotator.process(buffer);

	m_workingBuffer.makeCopyOf(buffer);
	m_convBuffer.clear();

	buffer.clear();

	if ((m_convEngines.size() != m_numAmbiChans) || (m_convEngines.size() == 0))
	{
		// not enough convolution engines to perform this process
		return;
	}

	for (int i = 0; i < m_numAmbiChans; ++i)
	{
		for (int j = 0; j < 2; ++j)
		{
			m_convBuffer.copyFrom(j, 0, m_workingBuffer.getReadPointer(i), buffer.getNumSamples());
		}
		// Adds buffers to be processed
		m_convEngines[i]->Add(m_convBuffer.getArrayOfWritePointers(), m_convBuffer.getNumSamples(), 2);

		// Gets the number of available 
		int availSamples = juce::jmin((int)m_convEngines[i]->Avail(buffer.getNumSamples()), buffer.getNumSamples());

		if (availSamples > 0)
		{
			float* convo = nullptr;

			for (int k = 0; k < 2; ++k)
			{
				convo = m_convEngines[i]->Get()[k];
				buffer.addFrom(k, 0, convo, availSamples);
			}

			m_convEngines[i]->Advance(availSamples);
		}
	}
}

void BinauralRenderer::releaseResources()
{
}

void BinauralRenderer::clearHRIR()
{
	m_hrirBuffers.clear();
	m_hrirShdBuffers.clear();

	m_numHrirAdded = 0;
}

void BinauralRenderer::addHRIR(const juce::AudioBuffer<float>& buffer)
{
	m_hrirBuffers.push_back(buffer);
	m_numHrirAdded++;
}

bool BinauralRenderer::uploadHRIRsToEngine()
{
	juce::ScopedLock lock(m_procLock);

	// convert the discrete HRIRs into SHD HRIRs for improved computational efficiency
	if (!convertHRIRToSHDHRIR())
	{
		sendMsgToLogWindow("could not upload SHD HRIRs to engine as conversion to SHD HRIRs failed");
		return false;
	}

	m_convEngines.clear();

	for (int i = 0; i < m_numAmbiChans; ++i)
	{
		WDL_ImpulseBuffer impulseBuffer;
		impulseBuffer.samplerate = m_sampleRate;
		impulseBuffer.SetNumChannels(2);

		for (int m = 0; m < impulseBuffer.GetNumChannels(); ++m)
			impulseBuffer.impulses[m].Set(m_hrirShdBuffers[i].getReadPointer(m), m_hrirShdBuffers[i].getNumSamples());

		std::unique_ptr<WDL_ConvolutionEngine> convEngine = std::make_unique<WDL_ConvolutionEngine>();
		convEngine->SetImpulse(&impulseBuffer, -1, 0, 0, false);
		convEngine->Reset();

		m_convEngines.push_back(std::move(convEngine));
	}

	return true;
}

bool BinauralRenderer::convertHRIRToSHDHRIR()
{
	if (m_hrirBuffers.size() <= 0)
	{
		sendMsgToLogWindow("no HRIRs available to convert to SHD HRIRs");
		return false;
	}

	// clear the current SHD HRIR as this process will create new ones
	m_hrirShdBuffers.clear();

	juce::AudioBuffer<float> hrirShdBuffer(2, m_hrirBuffers[0].getNumSamples());
	juce::AudioBuffer<float> basicBuffer(2, m_hrirBuffers[0].getNumSamples());

	for (int i = 0; i < m_numAmbiChans; ++i)
	{
		hrirShdBuffer.clear();

		for (int j = 0; j < m_numLsChans; ++j)
		{
			basicBuffer.clear();

			for (int n = 0; n < m_hrirBuffers[j].getNumChannels(); ++n)
			{
				basicBuffer.copyFrom(n, 0, m_hrirBuffers[j].getReadPointer(n), m_hrirBuffers[j].getNumSamples());
			}

			for (int k = 0; k < 2; ++k)
			{
				const int idx = (i * m_numLsChans) + j;

				const float basicMatrixIdx = m_basicDecodeTransposeMatrix[idx];
				hrirShdBuffer.addFrom(k, 0, basicBuffer.getWritePointer(k), basicBuffer.getNumSamples(), basicMatrixIdx);
			}
		}

		// add the newly created buffer to the member array
		m_hrirShdBuffers.push_back(hrirShdBuffer);
	}

	return true;
}

void BinauralRenderer::loadAmbixFile(const juce::File& ambixFile)
{
	if (!ambixFile.existsAsFile())
		sendMsgToLogWindow("failed to load: " + ambixFile.getFileName());

	AmbixLoader loader(ambixFile);

	clearVirtualLoudspeakers();

	setOrder(loader.getAmbiOrder());

	//m_dbFilter.init(m_sampleRate, m_order); // initialize dual band filter

	std::vector<float> azi;
	std::vector<float> ele;
	loader.getSourcePositions(azi, ele);
	setVirtualLoudspeakers(azi, ele, static_cast<int>(azi.size()));

	std::vector<float> decodeMatrix;
	loader.getDecodeMatrix(decodeMatrix);
	setDecodingMatrix(decodeMatrix);

	updateMatrices();

	clearHRIR();

	juce::AudioBuffer<float> hrir;

	for (int i = 0; i < loader.getNumHrirs(); ++i)
	{
		loader.getHrir(i, hrir);
		addHRIR(hrir);
	}

	uploadHRIRsToEngine();

	sendMsgToLogWindow("successfully loaded: " + ambixFile.getFileName());

	listeners.call([&](BinauralRenderer::Listener& l) { l.ambixFileLoaded(ambixFile); });
}

void BinauralRenderer::addListener(Listener* newListener)
{
	listeners.add(newListener);
}

void BinauralRenderer::removeListener(Listener* listener)
{
	listeners.remove(listener);
}
