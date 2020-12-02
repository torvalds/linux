// SPDX-License-Identifier: GPL-2.0
/********************************************************************************
*
*  Copyright (C) 2017 	NEXTCHIP Inc. All rights reserved.
*  Module		: The decoder's audio module
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
#include <linux/string.h>
#include <linux/delay.h>

#include "nvp6158_common.h"
#include "nvp6158_audio.h"

/*******************************************************************************
 * extern variable
 *******************************************************************************/
extern int	nvp6158_chip_id[4];	/* Chip ID */
extern int	nvp6158_rev_id[4];	/* Reversion ID */
static int	g_ai_type = NC_AD_AI;
static int	g_aud_sample = NC_AD_SAMPLE_RATE_8000;
extern unsigned int	nvp6158_cnt;	/* Chip count */
extern unsigned int	nvp6158_iic_addr[4];	/* Slave address of Chip */

/*******************************************************************************
*	Description		: initialize audio
*	Argurments		: recmaster(0[slave],1[master];), pbmaster(), ch_num(audio channel number)
*					  samplerate(sample rate), bits(bits)
*	Return value	: void
*	Modify			:
*	warning			:
*
* 	 param:
*		- xxmaster:0[slave],1[master];
*		- ch_num: audio channel number
*		- samplerate: 0[8k], 1[16k]
*		- bits: 0[16bits], 1[8bits]
*
*******************************************************************************/
void nvp6158_audio_init(unsigned char recmaster, unsigned char pbmaster,
			unsigned char ch_num, unsigned char samplerate, unsigned char bits)
{
	int i;
	unsigned char val_1x39;
	for(i=0; i<nvp6158_cnt; i++) {
		gpio_i2c_write(nvp6158_iic_addr[i], 0xFF, 0x01);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x94, 0x00);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x00, 0x02);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x08, 0x03);  //I2s outputs 16ch audio
		if(ch_num == 16) { // 4chips' audio cascade
			if(0 == i)
				gpio_i2c_write(nvp6158_iic_addr[i], 0x06, 0x3A);  //first stage
			else if(1 == i)
				gpio_i2c_write(nvp6158_iic_addr[i], 0x06, 0x38);  //middle stage
			else if(2 == i)
				gpio_i2c_write(nvp6158_iic_addr[i], 0x06, 0x38);  //middle stage	
			else if(3 == i)
				gpio_i2c_write(nvp6158_iic_addr[i], 0x06, 0x39);  //last stage	
		} else if(ch_num == 8) {
			if(0 == i) {
				gpio_i2c_write(nvp6158_iic_addr[i], 0x08, 0x02);  //I2s outputs 8ch audio
				gpio_i2c_write(nvp6158_iic_addr[i], 0x06, 0x3A);  //first stage
			} else if(1 == i) {
				gpio_i2c_write(nvp6158_iic_addr[i], 0x06, 0x39);  //last stage
				gpio_i2c_write(nvp6158_iic_addr[i], 0x0f, 0x54);  //set I2S right sequence
				gpio_i2c_write(nvp6158_iic_addr[i], 0x10, 0x76);
			}
		} else {
			gpio_i2c_write(nvp6158_iic_addr[i], 0x08, 0x01);  //I2s outputs 4ch audio, left & right each 2 channels
			gpio_i2c_write(nvp6158_iic_addr[i], 0x06, 0x3B);  //first stage
			gpio_i2c_write(nvp6158_iic_addr[i], 0x0f, 0x32);  //set I2S right sequence
		}
		gpio_i2c_write(nvp6158_iic_addr[i], 0x07, 0x00 | (recmaster << 7) | (samplerate << 3) | (bits <<2 ));
		if(recmaster == 0) {
			val_1x39 = gpio_i2c_read(nvp6158_iic_addr[i], 0x39);
			val_1x39 |= 0x80;
			gpio_i2c_write(nvp6158_iic_addr[i], 0x39, val_1x39);
		}
		gpio_i2c_write(nvp6158_iic_addr[i], 0x13, 0x00 | (pbmaster << 7) | (samplerate << 3) | (bits << 2));
		gpio_i2c_write(nvp6158_iic_addr[i], 0x01, 0x09);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x02, 0x09);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x03, 0x09);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x04, 0x09);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x05, 0x09);

		gpio_i2c_write(nvp6158_iic_addr[i], 0x22, 0x08);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x23, 0x10);  //playback
		gpio_i2c_write(nvp6158_iic_addr[i], 0x31, 0x09);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x47, 0x07);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x49, 0x00);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x44, 0x00);

		gpio_i2c_write(nvp6158_iic_addr[i], 0x32, 0xF0);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x94, 0x40);

		gpio_i2c_write(nvp6158_iic_addr[i], 0x38, 0x18);
		msleep(30);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x38, 0x08);
	}
}

void nvp6168_audio_init(unsigned char recmaster, unsigned char pbmaster,
			unsigned char ch_num, unsigned char samplerate, unsigned char bits)
{
	int i;
	for(i=0; i<nvp6158_cnt; i++) {
		gpio_i2c_write(nvp6158_iic_addr[i], 0xFF, 0x01);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x94, 0x00);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x08, 0x03);  //I2s outputs 16ch audio
		if(ch_num == 16) { // 4chips' audio cascade
			if(0 == i)
				gpio_i2c_write(nvp6158_iic_addr[i], 0x06, 0x1A);  //first stage
			else if(1 == i)
				gpio_i2c_write(nvp6158_iic_addr[i], 0x06, 0x18);  //middle stage
			else if(2 == i)
				gpio_i2c_write(nvp6158_iic_addr[i], 0x06, 0x18);  //middle stage	
			else if(3 == i)
				gpio_i2c_write(nvp6158_iic_addr[i], 0x06, 0x19);  //last stage
		} else if(ch_num == 8) {
			if(0 == i) {
				gpio_i2c_write(nvp6158_iic_addr[i], 0x08, 0x02);  //I2s outputs 8ch audio
				gpio_i2c_write(nvp6158_iic_addr[i], 0x06, 0x1A);  //first stage
			} else if(1 == i) {
				gpio_i2c_write(nvp6158_iic_addr[i], 0x06, 0x19);  //last stage
				gpio_i2c_write(nvp6158_iic_addr[i], 0x0f, 0x54);  //set I2S right sequence
				gpio_i2c_write(nvp6158_iic_addr[i], 0x10, 0x76);
			}
		} else {
			gpio_i2c_write(nvp6158_iic_addr[i], 0x08, 0x01);  //I2s outputs 4ch audio, left & right each 2 channels
			gpio_i2c_write(nvp6158_iic_addr[i], 0x06, 0x1B);  //first stage
			gpio_i2c_write(nvp6158_iic_addr[i], 0x0f, 0x32);  //set I2S right sequence
		}
		gpio_i2c_write(nvp6158_iic_addr[i], 0x07, 0x00 | (recmaster << 7) | (samplerate << 3) | (bits << 2));
		gpio_i2c_write(nvp6158_iic_addr[i], 0x13, 0x00 | (pbmaster << 7) | (samplerate << 3) | (bits << 2));
		gpio_i2c_write(nvp6158_iic_addr[i], 0x01, 0x09);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x02, 0x09);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x03, 0x09);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x04, 0x09);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x05, 0x09);

		gpio_i2c_write(nvp6158_iic_addr[i], 0x22, 0x08);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x23, 0x10);  //playback
		gpio_i2c_write(nvp6158_iic_addr[i], 0x31, 0x0A);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x47, 0x01);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x49, 0x88);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x44, 0x00);

		gpio_i2c_write(nvp6158_iic_addr[i], 0x32, 0x00);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x00, 0x02);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x06, 0x1B);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x46, 0x10);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x48, 0xD0);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x94, 0x40);

		gpio_i2c_write(nvp6158_iic_addr[i], 0x38, 0x18);
		msleep(30);
		gpio_i2c_write(nvp6158_iic_addr[i], 0x38, 0x08);

		nvp6158_audio_re_initialize(i);
	}
}


void nvp6158_audio_powerdown(unsigned char chip)
{
	unsigned char val_1x00;
	gpio_i2c_write(nvp6158_iic_addr[chip], 0xFF, 0x01);
	val_1x00 = gpio_i2c_read(nvp6158_iic_addr[chip], 0x00);
	gpio_i2c_write(nvp6158_iic_addr[chip], 0x00, (val_1x00 | 0xC0)); //bit7 AFE, bit6 DAC
}

void nvp6158_audio_in_type_set(int type)
{
	if(type < NC_AD_MAX) {
		printk("[%s] Change audio input type %d > %d\r\n", __func__, g_ai_type, type);
		g_ai_type = type;
	} else {
		printk("[%s] Invalid argument %d\r\n", __func__, type);
	}
}

int nvp6158_audio_in_type_get(void)
{
	return g_ai_type;
}

void nvp6158_audio_sample_rate_set(int sample)
{
	if(sample < NC_AD_SAMPLE_RATE_MAX) {
		printk("[%s] Change audio sampling rate %d > %d\r\n", __func__, g_aud_sample, sample);
		g_aud_sample = sample;
	} else {
		printk("[%s] Invalid argument %d\r\n", __func__, sample);
	}
}

int nvp6158_audio_sample_rate_get(void)
{
	return g_aud_sample;
}

void nvp6158_audio_re_initialize(int devnum)
{
	gpio_i2c_write(nvp6158_iic_addr[devnum], 0xFF, 0x01);

	// Set audio sampling rate
//		if(g_aud_sample == NC_AD_SAMPLE_RATE_8000)
//		{
//			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x07, 0x80);
//		}
//		else if(g_aud_sample == NC_AD_SAMPLE_RATE_16000)
//		{
//			gpio_i2c_write(nvp6158_iic_addr[devnum], 0x07, 0x88);
//		}
//		else
//		{
//			printk("[%s] Not supported yet [%d] \r\n", __func__, g_aud_sample);
//		}

	// Set audio input type
	if(g_ai_type == NC_AD_AOC) {
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x00, 0x00);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x31, 0x08);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x46, 0x00);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x58, 0x00);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x62, 0xFF);
	} else if(g_ai_type == NC_AD_AI) {
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x00, 0x02);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x31, 0x0A);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x46, 0x10);
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x58, 0x0f);  //05.29 org=0x02
		gpio_i2c_write(nvp6158_iic_addr[devnum], 0x62, 0x00);
	}
}

static void nvp6158_set_aoc_720_25p_ex_b(decoder_dev_ch_info_s *decoder_info)
{
	int ch = decoder_info->ch;

	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF, 0x12);

	if(g_aud_sample == NC_AD_SAMPLE_RATE_8000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0x11);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x0F);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0x50);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x17);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x0E);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	} else if(g_aud_sample == NC_AD_SAMPLE_RATE_16000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x10);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0x11);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x1E);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0xA0);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x0B);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x18);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	}
}

static void nvp6158_set_aoc_720_30p_ex_b(decoder_dev_ch_info_s *decoder_info)
{
	int ch = decoder_info->ch;

	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF, 0x12);

	if(g_aud_sample == NC_AD_SAMPLE_RATE_8000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0x12);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x0F);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0x50);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x17);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x0E);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	} else if(g_aud_sample == NC_AD_SAMPLE_RATE_16000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x10);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0x12);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x1E);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0xA0);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x0B);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x18);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	}
}

static void nvp6158_set_aoc_720_50p(decoder_dev_ch_info_s *decoder_info)
{
	int ch = decoder_info->ch;

	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF, 0x12);

	if(g_aud_sample == NC_AD_SAMPLE_RATE_8000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0x10);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x0A);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0x28);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x17);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x08);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	} else if(g_aud_sample == NC_AD_SAMPLE_RATE_16000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x10);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0x11);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x14);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0x50);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x0B);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x12);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	}
}

static void nvp6158_set_aoc_720_60p(decoder_dev_ch_info_s *decoder_info)
{
	int ch = decoder_info->ch;

	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF, 0x12);

	if(g_aud_sample == NC_AD_SAMPLE_RATE_8000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x03);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0x10);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x0A);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0x28);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x17);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x08);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	} else if(g_aud_sample == NC_AD_SAMPLE_RATE_16000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x03);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x10);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0x11);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x14);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0x50);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x0B);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x12);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	}
}

static void nvp6158_set_aoc_1080_25p(decoder_dev_ch_info_s *decoder_info)
{
	int ch = decoder_info->ch;

	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF, 0x12);

	if(g_aud_sample == NC_AD_SAMPLE_RATE_8000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0x12);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x0A);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0x50);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x13);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x08);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	} else if(g_aud_sample == NC_AD_SAMPLE_RATE_16000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x10);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0x12);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x14);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0xA0);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x09);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x12);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	}
}

static void nvp6158_set_aoc_1080_30p(decoder_dev_ch_info_s *decoder_info)
{
	int ch = decoder_info->ch;

	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF, 0x12);

	if(g_aud_sample == NC_AD_SAMPLE_RATE_8000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x03);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0x12);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x0A);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0x50);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x13);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x08);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	} else if(g_aud_sample == NC_AD_SAMPLE_RATE_16000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x03);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x10);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0x12);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x14);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0xA0);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x09);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x12);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	}
}

static void nvp6158_set_aoc_3m_25p(decoder_dev_ch_info_s *decoder_info)
{
	int ch = decoder_info->ch;

	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF, 0x12);

	if(g_aud_sample == NC_AD_SAMPLE_RATE_8000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x10);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x06);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0x6A);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x0A);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0x50);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x13);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x08);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	} else if(g_aud_sample == NC_AD_SAMPLE_RATE_16000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x10);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x10);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x06);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0x6A);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x14);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0xA0);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x09);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x12);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	}
}

static void nvp6158_set_aoc_3m_30p(decoder_dev_ch_info_s *decoder_info)
{
	int ch = decoder_info->ch;

	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF, 0x12);

	if(g_aud_sample == NC_AD_SAMPLE_RATE_8000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x03);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x10);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x06);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0x6A);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x0A);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0x50);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x13);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x08);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	} else if(g_aud_sample == NC_AD_SAMPLE_RATE_16000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x03);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x10);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x10);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x06);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0x6A);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x14);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0xA0);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x09);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x12);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	}
}

static void nvp6158_set_aoc_4m_25p(decoder_dev_ch_info_s *decoder_info)
{
	int ch = decoder_info->ch;

	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF, 0x12);

	if(g_aud_sample == NC_AD_SAMPLE_RATE_8000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x10);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x05);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0xD4);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x0A);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0x50);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x17);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x08);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	} else if(g_aud_sample == NC_AD_SAMPLE_RATE_16000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x10);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x10);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x05);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0xD4);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x14);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0xA0);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x0B);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x12);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	}
}

static void nvp6158_set_aoc_4m_30p(decoder_dev_ch_info_s *decoder_info)
{
	int ch = decoder_info->ch;

	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF, 0x12);

	if(g_aud_sample == NC_AD_SAMPLE_RATE_8000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x03);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x10);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x05);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0xD4);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x0A);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0x50);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x17);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x08);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	} else if(g_aud_sample == NC_AD_SAMPLE_RATE_16000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x03);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x10);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x10);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x05);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0xD4);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x14);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0xA0);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x0B);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x12);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	}
}

static void nvp6158_set_aoc_4m_15p(decoder_dev_ch_info_s *decoder_info)
{
	int ch = decoder_info->ch;

	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF, 0x12);

	if(g_aud_sample == NC_AD_SAMPLE_RATE_8000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x03);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x05);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0xD4);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x0A);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0xA0);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x0B);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x08);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	} else if(g_aud_sample == NC_AD_SAMPLE_RATE_16000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x03);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x11);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x05);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0xD4);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x14);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0x40);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x05);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x12);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	}
}

static void nvp6158_set_aoc_5m_12_5p(decoder_dev_ch_info_s *decoder_info)
{
	int ch = decoder_info->ch;

	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF, 0x12);

	if(g_aud_sample == NC_AD_SAMPLE_RATE_8000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x07);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0xB6);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x0A);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0xA0);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x0B);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x08);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	} else if(g_aud_sample == NC_AD_SAMPLE_RATE_16000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x10);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x07);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0xB6);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x14);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0x40);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x05);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x12);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	}
}

static void nvp6158_set_aoc_5m_20p(decoder_dev_ch_info_s *decoder_info)
{
	int ch = decoder_info->ch;

	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF, 0x12);

	if(g_aud_sample == NC_AD_SAMPLE_RATE_8000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x10);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x07);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0xB6);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x0A);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0x64);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x13);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x08);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	} else if(g_aud_sample == NC_AD_SAMPLE_RATE_16000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x10);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x10);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x07);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0xB6);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x14);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0xC8);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x09);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x12);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	}
}

static void nvp6158_set_aoc_5_3m_20p(decoder_dev_ch_info_s *decoder_info)
{
	int ch = decoder_info->ch;

	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF, 0x12);

	if(g_aud_sample == NC_AD_SAMPLE_RATE_8000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x10);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x07);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0x4D);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x0A);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0xA0);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x13);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x08);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	} else if(g_aud_sample == NC_AD_SAMPLE_RATE_16000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x10);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x10);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x07);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0x4D);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x14);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0xC8);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x0B);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x12);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	}
}

static void nvp6158_set_aoc_8m_12_5p(decoder_dev_ch_info_s *decoder_info)
{
	int ch = decoder_info->ch;

	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF, 0x12);

	if(g_aud_sample == NC_AD_SAMPLE_RATE_8000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x10);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x08);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0xC4);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x0A);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0xA0);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x13);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x08);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	} else if(g_aud_sample == NC_AD_SAMPLE_RATE_16000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x10);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x10);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x08);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0xC4);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x14);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0x40);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x09);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x12);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	}
}

static void nvp6158_set_aoc_8m_15p(decoder_dev_ch_info_s *decoder_info)
{
	int ch = decoder_info->ch;

	gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0xFF, 0x12);

	if(g_aud_sample == NC_AD_SAMPLE_RATE_8000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x03);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x10);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x08);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0xC4);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x0A);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0xA0);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x13);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x08);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	} else if(g_aud_sample == NC_AD_SAMPLE_RATE_16000) {
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x00 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x01 + (ch*0x40), 0x03);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x02 + (ch*0x40), 0x10);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x18 + (ch*0x40), 0x11);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x04 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x05 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x06 + (ch*0x40), 0x00);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x07 + (ch*0x40), 0x08);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x08 + (ch*0x40), 0xC4);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x09 + (ch*0x40), 0x03);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0A + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0B + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0C + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0D + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0E + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x0F + (ch*0x40), 0x40);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x10 + (ch*0x40), 0x04);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x11 + (ch*0x40), 0x14);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x12 + (ch*0x40), 0x01);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x13 + (ch*0x40), 0x40);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x14 + (ch*0x40), 0x09);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x15 + (ch*0x40), 0x12);

		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x17 + (ch*0x40), 0x00);
		gpio_i2c_write(nvp6158_iic_addr[decoder_info->devnum], 0x19 + (ch*0x40), 0xA0);
	}
}

void nvp6158_audio_set_aoc_format(decoder_dev_ch_info_s *decoder_info)
{
	switch(decoder_info->fmt_def) {
		// 1M
		case AHD20_720P_25P_EX_Btype :
			nvp6158_set_aoc_720_25p_ex_b(decoder_info);
			break;
		case AHD20_720P_30P_EX_Btype :
			nvp6158_set_aoc_720_30p_ex_b(decoder_info);
			break;
		case AHD20_720P_50P :
			nvp6158_set_aoc_720_50p(decoder_info);
		case AHD20_720P_60P :
			nvp6158_set_aoc_720_60p(decoder_info);
			printk("[%s] Not supported yet. [0x%X] \r\n", __func__, decoder_info->fmt_def);
			break;
			// 2M
		case AHD20_1080P_25P :
			nvp6158_set_aoc_1080_25p(decoder_info);
			break;
		case AHD20_1080P_30P :
			nvp6158_set_aoc_1080_30p(decoder_info);
			break;
			// 3M
		case AHD30_3M_25P :
			nvp6158_set_aoc_3m_25p(decoder_info);
		case AHD30_3M_30P :
			nvp6158_set_aoc_3m_30p(decoder_info);
			printk("[%s] Not supported yet. [0x%X] \r\n", __func__, decoder_info->fmt_def);
			break;
			// 4M
		case AHD30_4M_25P :
			nvp6158_set_aoc_4m_25p(decoder_info);
		case AHD30_4M_30P :
			nvp6158_set_aoc_4m_30p(decoder_info);
		case AHD30_4M_15P :
			nvp6158_set_aoc_4m_15p(decoder_info);
			printk("[%s] Not supported yet. [0x%X] \r\n", __func__, decoder_info->fmt_def);
			break;
			// 5M
		case AHD30_5M_12_5P :
			nvp6158_set_aoc_5m_12_5p(decoder_info);
		case AHD30_5M_20P :
			nvp6158_set_aoc_5m_20p(decoder_info);
			printk("[%s] Not supported yet. [0x%X] \r\n", __func__, decoder_info->fmt_def);
			break;
			// 5.3M
		case AHD30_5_3M_20P :
			nvp6158_set_aoc_5_3m_20p(decoder_info);
			printk("[%s] Not supported yet. [0x%X] \r\n", __func__, decoder_info->fmt_def);
			break;
			// 8M
		case AHD30_8M_12_5P :
			nvp6158_set_aoc_8m_12_5p(decoder_info);
		case AHD30_8M_15P :
			nvp6158_set_aoc_8m_15p(decoder_info);
			printk("[%s] Not supported yet. [0x%X] \r\n", __func__, decoder_info->fmt_def);
			break;
		default :
			printk("[%s] Not supported format [0x%X] \r\n", __func__, decoder_info->fmt_def);
			break;
	}
}

