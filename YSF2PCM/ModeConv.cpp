/*
 *   Copyright (C) 2010,2014,2016 and 2018 by Jonathan Naylor G4KLX
 *   Copyright (C) 2016 Mathias Weyland, HB9FRV
 *   Copyright (C) 2018 by Andy Uribe CA6JAU
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "ModeConv.h"
#include "YSFConvolution.h"
#include "CRC.h"
#include "Utils.h"

#include "Log.h"

#include <cstdio>
#include <cassert>
#include <md380_vocoder.h>

const unsigned char BIT_MASK_TABLE[] = {0x80U, 0x40U, 0x20U, 0x10U, 0x08U, 0x04U, 0x02U, 0x01U};

#define WRITE_BIT(p,i,b) p[(i)>>3] = (b) ? (p[(i)>>3] | BIT_MASK_TABLE[(i)&7]) : (p[(i)>>3] & ~BIT_MASK_TABLE[(i)&7])
#define READ_BIT(p,i)    (p[(i)>>3] & BIT_MASK_TABLE[(i)&7])

const unsigned int INTERLEAVE_TABLE_26_4[] = {
	0U, 4U,  8U, 12U, 16U, 20U, 24U, 28U, 32U, 36U, 40U, 44U, 48U, 52U, 56U, 60U, 64U, 68U, 72U, 76U, 80U, 84U, 88U, 92U, 96U, 100U,
	1U, 5U,  9U, 13U, 17U, 21U, 25U, 29U, 33U, 37U, 41U, 45U, 49U, 53U, 57U, 61U, 65U, 69U, 73U, 77U, 81U, 85U, 89U, 93U, 97U, 101U,
	2U, 6U, 10U, 14U, 18U, 22U, 26U, 30U, 34U, 38U, 42U, 46U, 50U, 54U, 58U, 62U, 66U, 70U, 74U, 78U, 82U, 86U, 90U, 94U, 98U, 102U,
	3U, 7U, 11U, 15U, 19U, 23U, 27U, 31U, 35U, 39U, 43U, 47U, 51U, 55U, 59U, 63U, 67U, 71U, 75U, 79U, 83U, 87U, 91U, 95U, 99U, 103U};

const unsigned char WHITENING_DATA[] = {0x93U, 0xD7U, 0x51U, 0x21U, 0x9CU, 0x2FU, 0x6CU, 0xD0U, 0xEFU, 0x0FU,
										0xF8U, 0x3DU, 0xF1U, 0x73U, 0x20U, 0x94U, 0xEDU, 0x1EU, 0x7CU, 0xD8U};

const unsigned char AMBE_SILENCE[] = {0xF8U, 0x01U, 0xA9U, 0x9FU, 0x8CU, 0xE0U, 0x80U};
const unsigned char YSF_SILENCE[] = {0x7BU, 0xB2U, 0x8EU, 0x43U, 0x36U, 0xE4U, 0xA2U, 0x39U, 0x78U, 0x49U, 0x33U, 0x68U, 0x33U};

CModeConv::CModeConv() :
m_ysfN(0U),
m_pcmN(0U),
m_YSF(5000U, "PCM2YSF"),
m_PCM(50000U, "YSF2PCM")
{
}

CModeConv::~CModeConv()
{
}

void CModeConv::encodeYSF(int16_t *pcm, uint8_t *vch)
{
	static const uint8_t scramble_code[180] = {
	1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 0, 1, 1, 1,
	0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1,
	1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1,
	0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0,
	1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 1,
	1, 1, 1, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 0, 1, 1,
	0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0,
	1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 0, 1, 1, 1, 1, 0,
	0, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0
	};
	unsigned char ambe49[49];
	unsigned char ambe[7];
	uint8_t buf[104];
	uint8_t result[104];
	
	memset(vch, 0, 13);
	memset(ambe, 0, 7);
	
	md380_encode(ambe, pcm);
	
	for(int i = 0; i < 6; ++i){
		for(int j = 0; j < 8; j++){
			ambe49[j+(8*i)] = (1 & (ambe[i] >> (7 - j)));
		}
	}
	ambe49[48] = (1 & (ambe[6] >> 7));

	for (int i=0; i<27; i++) {
		buf[0+i*3] = ambe49[i];
		buf[1+i*3] = ambe49[i];
		buf[2+i*3] = ambe49[i];
	}
	memcpy(buf+81, ambe49+27, 22);
	buf[103] = 0;
	for (int i=0; i<104; i++) {
		buf[i] = buf[i] ^ scramble_code[i];
	}

	int x=4;
	int y=26;
	for (int i=0; i<x; i++) {
		for (int j=0; j<y; j++) {
			result[i+j*x] = buf[j+i*y];
		}
	}
	for(int i = 0; i < 13; ++i){
		for(int j = 0; j < 8; ++j){
			vch[i] |= (result[(i*8)+j] << (7-j));
		}
	}
}

void CModeConv::putPCMHeader(int16_t *pcm)
{
	uint8_t vch[13];
	encodeYSF(pcm, vch);
	m_YSF.addData(&TAG_HEADER, 1U);
	m_YSF.addData(vch, 13U);
	m_ysfN += 1U;
}

void CModeConv::putPCM(int16_t *pcm)
{
	uint8_t vch[13];
	encodeYSF(pcm, vch);
	m_YSF.addData(&TAG_DATA, 1U);
	m_YSF.addData(vch, 13U);
	m_ysfN += 1U;
}

void CModeConv::putPCMEOT(int16_t *pcm)
{
	uint8_t vch[13];
	encodeYSF(pcm, vch);
	m_YSF.addData(&TAG_EOT, 1U);
	m_YSF.addData(vch, 13U);
	m_ysfN += 1U;
}

bool CModeConv::getPCM(int16_t *pcm)
{
	//fprintf(stderr, "CModeConv::getPCM N == %d\n", m_pcmN);
	if(m_pcmN){
		--m_pcmN;
		return m_PCM.getData(pcm, 160U);
	}
	else{
		return false;
	}
} 

void CModeConv::putYSF(unsigned char* data)
{
	unsigned char v_tmp[7U];
	int16_t pcm[160];

	assert(data != NULL);

	::memset(v_tmp, 0, 7U);

	data += YSF_SYNC_LENGTH_BYTES + YSF_FICH_LENGTH_BYTES;

	unsigned int offset = 40U; // DCH(0)

	// We have a total of 5 VCH sections, iterate through each
	for (unsigned int j = 0U; j < 5U; j++, offset += 144U) {

		unsigned char vch[13U];
		unsigned int dat_a = 0U;
		unsigned int dat_b = 0U;
		unsigned int dat_c = 0U;

		// Deinterleave
		for (unsigned int i = 0U; i < 104U; i++) {
			unsigned int n = INTERLEAVE_TABLE_26_4[i];
			bool s = READ_BIT(data, offset + n);
			WRITE_BIT(vch, i, s);
		}

		// "Un-whiten" (descramble)
		for (unsigned int i = 0U; i < 13U; i++)
			vch[i] ^= WHITENING_DATA[i];
	
		for (unsigned int i = 0U; i < 12U; i++) {
			dat_a <<= 1U;
			if (READ_BIT(vch, 3U*i + 1U))
				dat_a |= 0x01U;;
		}
		
		for (unsigned int i = 0U; i < 12U; i++) {
			dat_b <<= 1U;
			if (READ_BIT(vch, 3U*(i + 12U) + 1U))
				dat_b |= 0x01U;;
		}
		
		for (unsigned int i = 0U; i < 3U; i++) {
			dat_c <<= 1U;
			if (READ_BIT(vch, 3U*(i + 24U) + 1U))
				dat_c |= 0x01U;;
		}

		for (unsigned int i = 0U; i < 22U; i++) {
			dat_c <<= 1U;
			if (READ_BIT(vch, i + 81U))
				dat_c |= 0x01U;;
		}

		for (unsigned int i = 0U; i < 12U; i++) {
			bool s1 = (dat_a << (i + 20U)) & 0x80000000;
			bool s2 = (dat_b << (i + 20U)) & 0x80000000;
			WRITE_BIT(v_tmp, i, s1);
			WRITE_BIT(v_tmp, i + 12U, s2);
		}

		for (unsigned int i = 0U; i < 25U; i++) {
			bool s = (dat_c << (i + 7U)) & 0x80000000;
			WRITE_BIT(v_tmp, i + 24U, s);
		}
		md380_decode(v_tmp, pcm);
		//m_PCM.addData(&TAG_DATA, 1U);
		m_PCM.addData(pcm, 160U);
		m_pcmN += 1;
	}
}

unsigned int CModeConv::getYSF(unsigned char* data)
{
	unsigned char tag[1U];

	tag[0U] = TAG_NODATA;

	data += YSF_SYNC_LENGTH_BYTES + YSF_FICH_LENGTH_BYTES;
	
	if (m_ysfN >= 1U) {
		m_YSF.peek(tag, 1U);

		if (tag[0U] != TAG_DATA) {
			m_YSF.getData(tag, 1U);
			m_YSF.getData(data, 13U);
			m_ysfN -= 1U;
			return tag[0U];
		}
	}

	if (m_ysfN >= 5U) {
		data += 5U;
		m_YSF.getData(tag, 1U);
		m_YSF.getData(data, 13U);
		m_ysfN -= 1U;

		data += 18U;
		m_YSF.getData(tag, 1U);
		m_YSF.getData(data, 13U);
		m_ysfN -= 1U;

		data += 18U;
		m_YSF.getData(tag, 1U);
		m_YSF.getData(data, 13U);
		m_ysfN -= 1U;

		data += 18U;
		m_YSF.getData(tag, 1U);
		m_YSF.getData(data, 13U);
		m_ysfN -= 1U;

		data += 18U;
		m_YSF.getData(tag, 1U);
		m_YSF.getData(data, 13U);
		m_ysfN -= 1U;

		return TAG_DATA;
	}
	else
		return TAG_NODATA;
}
