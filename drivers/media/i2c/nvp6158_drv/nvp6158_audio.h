// SPDX-License-Identifier: GPL-2.0
/********************************************************************************
*
*  Copyright (C) 2017 	NEXTCHIP Inc. All rights reserved.
*  Module		: The decoder's audio header file
*  Description	: Audio i/o
*  Author		:
*  Date         :
*  Version		: Version 2.0
*
********************************************************************************
*  History      :
*
*
********************************************************************************/
#ifndef _AUDIO_H_
#define _AUDIO_H_

/********************************************************************
 *  define and enum
 ********************************************************************/
#define AIG_DEF   0x08
#define AOG_DEF   0x08

/********************************************************************
 *  structure
 ********************************************************************/

/********************************************************************
 *  external api
 ********************************************************************/
extern void nvp6158_audio_init(unsigned char recmaster, unsigned char pbmaster,
                              unsigned char ch_num, unsigned char samplerate, unsigned char bits);
extern void nvp6168_audio_init(unsigned char recmaster, unsigned char pbmaster,
                              unsigned char ch_num, unsigned char samplerate, unsigned char bits);
extern void nvp6158_audio_powerdown(unsigned char chip);
// Add for Raptor4
void nvp6158_audio_in_type_set(int type);
int nvp6158_audio_in_type_get(void);
void nvp6158_audio_sample_rate_set(int sample);
int nvp6158_audio_sample_rate_get(void);
void nvp6158_audio_re_initialize(int devnum);
void nvp6158_audio_set_aoc_format(decoder_dev_ch_info_s *decoder_info);

#endif	// End of _AUDIO_H_

/********************************************************************
 *  End of file
 ********************************************************************/

