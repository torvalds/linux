/*  intel_sst_v1_control.c - Intel SST Driver for audio engine
 *
 *  Copyright (C) 2008-10 Intel Corp
 *  Authors:	Vinod Koul <vinod.koul@intel.com>
 *	Harsha Priya <priya.harsha@intel.com>
 *	Dharageswari R <dharageswari.r@intel.com>
 *	KP Jeeja <jeeja.kp@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This file contains the control operations of vendor 2
 */

#include <linux/pci.h>
#include <linux/file.h>
#include <asm/mrst.h>
#include <sound/pcm.h>
#include "jack.h"
#include <sound/pcm_params.h>
#include <sound/control.h>
#include <sound/initval.h>
#include "intel_sst.h"
#include "intel_sst_ioctl.h"
#include "intelmid.h"
#include "intelmid_snd_control.h"

#include <linux/gpio.h>
#define KOSKI_VOICE_CODEC_ENABLE 46

enum _reg_v2 {

	MASTER_CLOCK_PRESCALAR  = 0x205,
	SET_MASTER_AND_LR_CLK1	= 0x20b,
	SET_MASTER_AND_LR_CLK2	= 0x20c,
	MASTER_MODE_AND_DATA_DELAY = 0x20d,
	DIGITAL_INTERFACE_TO_DAI2 = 0x20e,
	CLK_AND_FS1 = 0x208,
	CLK_AND_FS2 = 0x209,
	DAI2_TO_DAC_HP = 0x210,
	HP_OP_SINGLE_ENDED = 0x224,
	ENABLE_OPDEV_CTRL = 0x226,
	ENABLE_DEV_AND_USE_XTAL = 0x227,

	/* Max audio subsystem (PQ49) MAX 8921 */
	AS_IP_MODE_CTL = 0xF9,
	AS_LEFT_SPKR_VOL_CTL = 0xFA, /* Mono Earpiece volume control */
	AS_RIGHT_SPKR_VOL_CTL = 0xFB,
	AS_LEFT_HP_VOL_CTL = 0xFC,
	AS_RIGHT_HP_VOL_CTL = 0xFD,
	AS_OP_MIX_CTL = 0xFE,
	AS_CONFIG = 0xFF,

	/* Headphone volume control & mute registers */
	VOL_CTRL_LT = 0x21c,
	VOL_CTRL_RT = 0x21d,

};
/**
 * mx_init_card - initilize the sound card
 *
 * This initilizes the audio paths to know values in case of this sound card
 */
static int mx_init_card(void)
{
	if (is_aava())  {

		struct sc_reg_access sc_access[] = {
			{0x200, 0x00, 0x0},
			{0x201, 0xC0, 0x0},
			{0x202, 0x00, 0x0},
			{0x203, 0x00, 0x0},
			{0x204, 0x0e, 0x0},
			{0x205, 0x20, 0x0},
			{0x206, 0x00, 0x0},
			{0x207, 0x00, 0x0},
			{0x208, 0x00, 0x0},
			{0x209, 0x51, 0x0},
			{0x20a, 0x00, 0x0},
			{0x20b, 0x5a, 0x0},
			{0x20c, 0xbe, 0x0},
			{0x20d, 0x90, 0x0},
			{0x20e, 0x51, 0x0},
			{0x20f, 0x00, 0x0},
			{0x210, 0x21, 0x0},
			{0x211, 0x00, 0x0},
			{0x212, 0x00, 0x0},
			{0x213, 0x00, 0x0},
			{0x214, 0x41, 0x0},
			{0x215, 0x81, 0x0},
			{0x216, 0x00, 0x0},
			{0x217, 0x00, 0x0},
			{0x218, 0x00, 0x0},
			{0x219, 0x00, 0x0},
			{0x21a, 0x00, 0x0},
			{0x21b, 0x00, 0x0},
			{0x21c, 0x00, 0x0},
			{0x21d, 0x00, 0x0},
			{0x21e, 0x00, 0x0},
			{0x21f, 0x00, 0x0},
			{0x220, 0x00, 0x0},
			{0x221, 0x00, 0x0},
			{0x222, 0x51, 0x0},
			{0x223, 0x20, 0x0}, /* Jack detection: 00 -> 01 */
			{0x224, 0x40, 0x0},
			{0x225, 0x80, 0x0}, /* JAck detection: 00 -> 80 */
			{0x226, 0x00, 0x0},
			{0x227, 0x00, 0x0},
			{0xf9, 0x40, 0x0},
			{0xfa, 0x1F, 0x0},
			{0xfb, 0x1F, 0x0},
			{0xfc, 0x1F, 0x0},
			{0xfd, 0x1F, 0x0},
			{0xfe, 0x00, 0x0},
			{0xff, 0x00, 0x0}, /* Removed sel_output */
		};
		int retval;

		/*init clock sig to voice codec*/
		retval = gpio_request(KOSKI_VOICE_CODEC_ENABLE,
					"sound_voice_codec");
		if (retval) {
			pr_err("sst: Error enabling voice codec clock\n");
		} else {
			gpio_direction_output(KOSKI_VOICE_CODEC_ENABLE, 1);
			pr_debug("sst: Voice codec clock enabled\n");
		}

		snd_pmic_ops_mx.card_status = SND_CARD_INIT_DONE;
		snd_pmic_ops_mx.master_mute = UNMUTE;
		snd_pmic_ops_mx.mute_status = UNMUTE;
		snd_pmic_ops_mx.num_channel = 2;
		pr_debug("**************inside aava\n");
		return sst_sc_reg_access(sc_access, PMIC_WRITE, 47);
	} else {
		struct sc_reg_access sc_access[] = {
			{0x200, 0x80, 0x00},
			{0x201, 0xC0, 0x00},
			{0x202, 0x00, 0x00},
			{0x203, 0x00, 0x00},
			{0x204, 0x02, 0x00},
			{0x205, 0x10, 0x00},
			{0x206, 0x60, 0x00},
			{0x207, 0x00, 0x00},
			{0x208, 0x90, 0x00},
			{0x209, 0x51, 0x00},
			{0x20a, 0x00, 0x00},
			{0x20b, 0x10, 0x00},
			{0x20c, 0x00, 0x00},
			{0x20d, 0x00, 0x00},
			{0x20e, 0x21, 0x00},
			{0x20f, 0x00, 0x00},
			{0x210, 0x84, 0x00},
			{0x211, 0xB3, 0x00},
			{0x212, 0x00, 0x00},
			{0x213, 0x00, 0x00},
			{0x214, 0x41, 0x00},
			{0x215, 0x00, 0x00},
			{0x216, 0x00, 0x00},
			{0x217, 0x00, 0x00},
			{0x218, 0x03, 0x00},
			{0x219, 0x03, 0x00},
			{0x21a, 0x00, 0x00},
			{0x21b, 0x00, 0x00},
			{0x21c, 0x00, 0x00},
			{0x21d, 0x00, 0x00},
			{0x21e, 0x00, 0x00},
			{0x21f, 0x00, 0x00},
			{0x220, 0x20, 0x00},
			{0x221, 0x20, 0x00},
			{0x222, 0x51, 0x00},
			{0x223, 0x20, 0x00},
			{0x224, 0x04, 0x00},
			{0x225, 0x80, 0x00},
			{0x226, 0x0F, 0x00},
			{0x227, 0x08, 0x00},
			{0xf9,  0x40, 0x00},
			{0xfa,  0x1f, 0x00},
			{0xfb,  0x1f, 0x00},
			{0xfc,  0x1f, 0x00},
			{0xfd,  0x1f, 0x00},
			{0xfe,  0x00, 0x00},
			{0xff,  0x0c, 0x00},
		};
		snd_pmic_ops_mx.card_status = SND_CARD_INIT_DONE;
		snd_pmic_ops_mx.num_channel = 2;
		snd_pmic_ops_mx.master_mute = UNMUTE;
		snd_pmic_ops_mx.mute_status = UNMUTE;
		return sst_sc_reg_access(sc_access, PMIC_WRITE, 47);
	}
}

static int mx_init_capture_card(void)
{
	struct sc_reg_access sc_access[] = {
		{0x206, 0x5a, 0x0},
		{0x207, 0xbe, 0x0},
		{0x208, 0x90, 0x0},
		{0x209, 0x32, 0x0},
		{0x20e, 0x22, 0x0},
		{0x210, 0x84, 0x0},
		{0x223, 0x20, 0x0},
		{0x226, 0xC0, 0x0},
	};

	int retval = 0;

	retval = sst_sc_reg_access(sc_access, PMIC_WRITE, 8);
	if (0 != retval) {
		/* pmic communication fails */
		pr_debug("sst: pmic commn failed\n");
		return retval;
	}

	pr_debug("sst: Capture configuration complete!!\n");
	return 0;
}

static int mx_init_playback_card(void)
{
	struct sc_reg_access sc_access[] = {
		{0x206, 0x00, 0x0},
		{0x207, 0x00, 0x0},
		{0x208, 0x00, 0x0},
		{0x209, 0x51, 0x0},
		{0x20e, 0x51, 0x0},
		{0x210, 0x21, 0x0},
		{0x223, 0x01, 0x0},
	};
	int retval = 0;

	retval = sst_sc_reg_access(sc_access, PMIC_WRITE, 9);
	if (0 != retval) {
		/* pmic communication fails */
		pr_debug("sst: pmic commn failed\n");
		return retval;
	}

	pr_debug("sst: Playback configuration complete!!\n");
	return 0;
}

static int mx_enable_audiodac(int value)
{
	struct sc_reg_access sc_access[3];
	int mute_val = 0;
	int mute_val1 = 0;
	int retval = 0;

	sc_access[0].reg_addr = AS_LEFT_HP_VOL_CTL;
	sc_access[1].reg_addr = AS_RIGHT_HP_VOL_CTL;

	if (value == UNMUTE) {
		mute_val = 0x1F;
		mute_val1 = 0x00;
	} else {
		mute_val = 0x00;
		mute_val1 = 0x40;
	}
	sc_access[0].mask = sc_access[1].mask = MASK0|MASK1|MASK2|MASK3|MASK4;
	sc_access[0].value = sc_access[1].value = (u8)mute_val;
	retval = sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 2);
	if (retval)
		return retval;
	pr_debug("sst: mute status = %d", snd_pmic_ops_mx.mute_status);
	if (snd_pmic_ops_mx.mute_status == MUTE ||
				snd_pmic_ops_mx.master_mute == MUTE)
		return retval;

	sc_access[0].reg_addr = VOL_CTRL_LT;
	sc_access[1].reg_addr = VOL_CTRL_RT;
	sc_access[0].mask = sc_access[1].mask = MASK6;
	sc_access[0].value = sc_access[1].value = mute_val1;
	if (snd_pmic_ops_mx.num_channel == 1)
		sc_access[1].value = 0x40;
	return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 2);
}

static int mx_power_up_pb(unsigned int port)
{

	int retval = 0;
	struct sc_reg_access sc_access[3];

	if (snd_pmic_ops_mx.card_status == SND_CARD_UN_INIT) {
		retval = mx_init_card();
		if (retval)
			return retval;
	}
	if ((is_aava()) && port == 1)
		mx_init_playback_card();
	retval = mx_enable_audiodac(MUTE);
	if (retval)
		return retval;

	msleep(10);

	sc_access[0].reg_addr = AS_CONFIG;
	sc_access[0].mask  = MASK7;
	sc_access[0].value = 0x80;
	retval = sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 1);
	if (retval)
		return retval;

	sc_access[0].reg_addr = ENABLE_OPDEV_CTRL;
	sc_access[0].mask  = 0xff;
	sc_access[0].value = 0x3C;
	retval = sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 1);
	if (retval)
		return retval;

	sc_access[0].reg_addr = ENABLE_DEV_AND_USE_XTAL;
	sc_access[0].mask  = 0x80;
	sc_access[0].value = 0x80;
	retval = sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 1);
	if (retval)
		return retval;

	return mx_enable_audiodac(UNMUTE);
}

static int mx_power_down_pb(void)
{
	struct sc_reg_access sc_access[3];
	int retval = 0;

	if (snd_pmic_ops_mx.card_status == SND_CARD_UN_INIT) {
		retval = mx_init_card();
		if (retval)
			return retval;
	}

	retval = mx_enable_audiodac(MUTE);
	if (retval)
		return retval;

	sc_access[0].reg_addr = ENABLE_OPDEV_CTRL;
	sc_access[0].mask  = MASK3|MASK2;
	sc_access[0].value = 0x00;

	retval = sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 1);
	if (retval)
		return retval;

	return mx_enable_audiodac(UNMUTE);
}

static int mx_power_up_cp(unsigned int port)
{
	int retval = 0;
	struct sc_reg_access sc_access[] = {
		{ENABLE_DEV_AND_USE_XTAL, 0x80, MASK7},
		{ENABLE_OPDEV_CTRL, 0x3, 0x3},
	};

	if (snd_pmic_ops_mx.card_status == SND_CARD_UN_INIT) {
		retval = mx_init_card();
		if (retval)
			return retval;
	}

	if (is_aava()) {
		retval = mx_init_capture_card();
		if (retval)
			return retval;
		return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 1);
	} else
		return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 2);
}

static int mx_power_down_cp(void)
{
	struct sc_reg_access sc_access[] = {
		{ENABLE_OPDEV_CTRL, 0x00, MASK1|MASK0},
	};
	int retval = 0;

	if (snd_pmic_ops_mx.card_status == SND_CARD_UN_INIT) {
		retval = mx_init_card();
		if (retval)
			return retval;
	}

	return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 1);
}

static int mx_power_down(void)
{
	int retval = 0;
	struct sc_reg_access sc_access[3];

	if (snd_pmic_ops_mx.card_status == SND_CARD_UN_INIT) {
		retval = mx_init_card();
		if (retval)
			return retval;
	}

	retval = mx_enable_audiodac(MUTE);
	if (retval)
		return retval;

	sc_access[0].reg_addr = AS_CONFIG;
	sc_access[0].mask  = MASK7;
	sc_access[0].value = 0x00;
	retval = sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 1);
	if (retval)
		return retval;

	sc_access[0].reg_addr = ENABLE_DEV_AND_USE_XTAL;
	sc_access[0].mask  = MASK7;
	sc_access[0].value = 0x00;
	retval = sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 1);
	if (retval)
		return retval;

	sc_access[0].reg_addr = ENABLE_OPDEV_CTRL;
	sc_access[0].mask  = MASK3|MASK2;
	sc_access[0].value = 0x00;
	retval = sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 1);
	if (retval)
		return retval;

	return mx_enable_audiodac(UNMUTE);
}

static int mx_set_pcm_voice_params(void)
{
	int retval = 0;
	struct sc_reg_access sc_access[] = {
		{0x200, 0x80, 0x00},
		{0x201, 0xC0, 0x00},
		{0x202, 0x00, 0x00},
		{0x203, 0x00, 0x00},
		{0x204, 0x0e, 0x00},
		{0x205, 0x20, 0x00},
		{0x206, 0x8f, 0x00},
		{0x207, 0x21, 0x00},
		{0x208, 0x18, 0x00},
		{0x209, 0x32, 0x00},
		{0x20a, 0x00, 0x00},
		{0x20b, 0x5A, 0x00},
		{0x20c, 0xBE, 0x00},/* 0x00 -> 0xBE Koski */
		{0x20d, 0x00, 0x00}, /* DAI2 'off' */
		{0x20e, 0x40, 0x00},
		{0x20f, 0x00, 0x00},
		{0x210, 0x84, 0x00},
		{0x211, 0x33, 0x00}, /* Voice filter */
		{0x212, 0x00, 0x00},
		{0x213, 0x00, 0x00},
		{0x214, 0x41, 0x00},
		{0x215, 0x00, 0x00},
		{0x216, 0x00, 0x00},
		{0x217, 0x20, 0x00},
		{0x218, 0x00, 0x00},
		{0x219, 0x00, 0x00},
		{0x21a, 0x40, 0x00},
		{0x21b, 0x40, 0x00},
		{0x21c, 0x09, 0x00},
		{0x21d, 0x09, 0x00},
		{0x21e, 0x00, 0x00},
		{0x21f, 0x00, 0x00},
		{0x220, 0x00, 0x00}, /* Microphone configurations */
		{0x221, 0x00, 0x00}, /* Microphone configurations */
		{0x222, 0x50, 0x00}, /* Microphone configurations */
		{0x223, 0x21, 0x00}, /* Microphone configurations */
		{0x224, 0x00, 0x00},
		{0x225, 0x80, 0x00},
		{0xf9, 0x40, 0x00},
		{0xfa, 0x19, 0x00},
		{0xfb, 0x19, 0x00},
		{0xfc, 0x12, 0x00},
		{0xfd, 0x12, 0x00},
		{0xfe, 0x00, 0x00},
	};

	if (snd_pmic_ops_mx.card_status == SND_CARD_UN_INIT) {
		retval = mx_init_card();
		if (retval)
			return retval;
	}
	pr_debug("sst: SST DBG mx_set_pcm_voice_params called\n");
	return sst_sc_reg_access(sc_access, PMIC_WRITE, 44);
}

static int mx_set_pcm_audio_params(int sfreq, int word_size, int num_channel)
{
	int retval = 0;

	if (!is_aava()) {
		int config1 = 0, config2 = 0, filter = 0xB3;
		struct sc_reg_access sc_access[5];

		if (snd_pmic_ops_mx.card_status == SND_CARD_UN_INIT) {
			retval = mx_init_card();
			if (retval)
				return retval;
		}

		switch (sfreq) {
		case 8000:
			config1 = 0x10;
			config2 = 0x00;
			filter = 0x33;
			break;
		case 11025:
			config1 = 0x16;
			config2 = 0x0d;
			break;
		case 12000:
			config1 = 0x18;
			config2 = 0x00;
			break;
		case 16000:
			config1 = 0x20;
			config2 = 0x00;
			break;
		case 22050:
			config1 = 0x2c;
			config2 = 0x1a;
			break;
		case 24000:
			config1 = 0x30;
			config2 = 0x00;
			break;
		case 32000:
			config1 = 0x40;
			config2 = 0x00;
			break;
		case 44100:
			config1 = 0x58;
			config2 = 0x33;
			break;
		case 48000:
			config1 = 0x60;
			config2 = 0x00;
			break;
		}

		snd_pmic_ops_mx.num_channel = num_channel;
		/*mute the right channel if MONO*/
		if (snd_pmic_ops_mx.num_channel == 1)	{

			sc_access[0].reg_addr = VOL_CTRL_RT;
			sc_access[0].value = 0x40;
			sc_access[0].mask = MASK6;

			sc_access[1].reg_addr = 0x224;
			sc_access[1].value = 0x05;
			sc_access[1].mask = MASK0|MASK1|MASK2;

			retval = sst_sc_reg_access(sc_access,
					PMIC_READ_MODIFY, 2);
			if (retval)
				return retval;
		} else {
			sc_access[0].reg_addr = VOL_CTRL_RT;
			sc_access[0].value = 0x00;
			sc_access[0].mask = MASK6;

			sc_access[1].reg_addr = 0x224;
			sc_access[1].value = 0x04;
			sc_access[1].mask = MASK0|MASK1|MASK2;

			retval = sst_sc_reg_access(sc_access,
					PMIC_READ_MODIFY, 2);
			if (retval)
				return retval;
		}
		sc_access[0].reg_addr =	0x206;
		sc_access[0].value = config1;
		sc_access[1].reg_addr = 0x207;
		sc_access[1].value = config2;

		if (word_size == 16) {
			sc_access[2].value = 0x51;
			sc_access[3].value = 0x31;
		} else if (word_size == 24) {
			sc_access[2].value = 0x52;
			sc_access[3].value = 0x92;
		}

		sc_access[2].reg_addr = 0x209;
		sc_access[3].reg_addr = 0x20e;

		sc_access[4].reg_addr = 0x211;
		sc_access[4].value = filter;

		return sst_sc_reg_access(sc_access, PMIC_WRITE, 5);
	} else {
		int config1 = 0, config2 = 0, filter = 0x00;
		struct sc_reg_access sc_access[5];

		pr_debug("sst: mx_set_pcm_audio_params - inside AAVA\n");

		if (snd_pmic_ops_mx.card_status == SND_CARD_UN_INIT) {
			retval = mx_init_card();
			if (retval)
				return retval;
		}

		switch (sfreq) {
		case 8000:
			config1 = 0x20;
			config2 = 0x0f;
			filter = 0x33;
			break;
		case 11025:
			config1 = 0x14;
			config2 = 0xd8;
			break;
		case 12000:
			config1 = 0x16;
			config2 = 0xaf;
			break;
		case 16000:
			config1 = 0x1e;
			config2 = 0x3f;
			break;
		case 22050:
			config1 = 0x29;
			config2 = 0xaf;
			break;
		case 24000:
			config1 = 0x2d;
			config2 = 0x5f;
			break;
		case 32000:
			config1 = 0x3c;
			config2 = 0x7f;
			break;
		case 44100:
			config1 = 0x53;
			config2 = 0x5f;
			break;
		case 48000:
			config1 = 0x5a;
			config2 = 0xbe;
			break;
		}

		snd_pmic_ops_mx.num_channel = num_channel;
		/*mute the right channel if MONO*/
		sc_access[0].reg_addr =	0x20b;
		sc_access[0].value = config1;
		sc_access[1].reg_addr = 0x20c;
		sc_access[1].value = config2;
		if (word_size == 16) {
			sc_access[2].value = 0x51;
			sc_access[3].value = 0x51;
		} else if (word_size == 24) {
			sc_access[2].value = 0x52;
			sc_access[3].value = 0x92;

		}

		sc_access[2].reg_addr = 0x209;
		sc_access[3].reg_addr = 0x20e;
		sc_access[4].reg_addr = 0x211;
		sc_access[4].value = filter;

		return sst_sc_reg_access(sc_access, PMIC_WRITE, 5);
	}
	return 0;
}

static int mx_set_selected_output_dev(u8 dev_id)
{
	struct sc_reg_access sc_access[2];
	int num_reg = 0;
	int retval = 0;

	if (snd_pmic_ops_mx.card_status == SND_CARD_UN_INIT) {
		retval = mx_init_card();
		if (retval)
			return retval;
	}

	pr_debug("sst: mx_set_selected_output_dev dev_id:0x%x\n", dev_id);
	snd_pmic_ops_mx.output_dev_id = dev_id;
	switch (dev_id) {
	case STEREO_HEADPHONE:
		sc_access[0].reg_addr = 0xFF;
		sc_access[0].value = 0x8C;
		sc_access[0].mask =
			MASK2|MASK3|MASK5|MASK6|MASK4;

		num_reg = 1;
		break;
	case MONO_EARPIECE:
	case INTERNAL_SPKR:
		sc_access[0].reg_addr = 0xFF;
		sc_access[0].value = 0xb0;
		sc_access[0].mask =  MASK2|MASK3|MASK5|MASK6|MASK4;

		num_reg = 1;
		break;
	case RECEIVER:
		pr_debug("sst: RECEIVER Koski selected\n");

		/* configuration - AS enable, receiver enable */
		sc_access[0].reg_addr = 0xFF;
		sc_access[0].value = 0x81;
		sc_access[0].mask = 0xff;

		num_reg = 1;
		break;
	default:
		pr_err("sst: Not a valid output dev\n");
		return 0;
	}
	return sst_sc_reg_access(sc_access, PMIC_WRITE, num_reg);
}


static int mx_set_voice_port(int status)
{
	int retval = 0;

	if (snd_pmic_ops_mx.card_status == SND_CARD_UN_INIT) {
		retval = mx_init_card();
		if (retval)
			return retval;
	}
	if (status == ACTIVATE)
		retval = mx_set_pcm_voice_params();

	return retval;
}

static int mx_set_audio_port(int status)
{
	int retval = 0;
	if (is_aava()) {
		if (snd_pmic_ops_mx.card_status == SND_CARD_UN_INIT)
			retval = mx_init_card();
		if (retval)
			return retval;
		if (status == ACTIVATE) {
			mx_init_card();
			mx_set_selected_output_dev
					(snd_pmic_ops_mx.output_dev_id);
		}
	}
	return retval;

}

static int mx_set_selected_input_dev(u8 dev_id)
{
	struct sc_reg_access sc_access[2];
	int num_reg = 0;
	int retval = 0;

	if (snd_pmic_ops_mx.card_status == SND_CARD_UN_INIT) {
		retval = mx_init_card();
		if (retval)
			return retval;
	}
	snd_pmic_ops_mx.input_dev_id = dev_id;
	pr_debug("sst: mx_set_selected_input_dev dev_id:0x%x\n", dev_id);

	switch (dev_id) {
	case AMIC:
		sc_access[0].reg_addr = 0x223;
		sc_access[0].value = 0x00;
		sc_access[0].mask = MASK7|MASK6|MASK5|MASK4|MASK0;
		sc_access[1].reg_addr = 0x222;
		sc_access[1].value = 0x50;
		sc_access[1].mask = MASK7|MASK6|MASK5|MASK4;
		num_reg = 2;
		break;

	case HS_MIC:
		sc_access[0].reg_addr = 0x223;
		sc_access[0].value = 0x20;
		sc_access[0].mask = MASK7|MASK6|MASK5|MASK4|MASK0;
		sc_access[1].reg_addr = 0x222;
		sc_access[1].value = 0x51;
		sc_access[1].mask = MASK7|MASK6|MASK5|MASK4;
		num_reg = 2;
		break;
	case DMIC:
		sc_access[1].reg_addr = 0x222;
		sc_access[1].value = 0x00;
		sc_access[1].mask = MASK7|MASK6|MASK5|MASK4|MASK0;
		sc_access[0].reg_addr = 0x223;
		sc_access[0].value = 0x20;
		sc_access[0].mask = MASK7|MASK6|MASK5|MASK4|MASK0;
		num_reg = 2;
		break;
	}
	return sst_sc_reg_access(sc_access, PMIC_WRITE, num_reg);
}

static int mx_set_mute(int dev_id, u8 value)
{
	struct sc_reg_access sc_access[5];
	int num_reg = 0;
	int retval = 0;

	if (snd_pmic_ops_mx.card_status == SND_CARD_UN_INIT) {
		retval = mx_init_card();
		if (retval)
			return retval;
	}


	pr_debug("sst: set_mute dev_id:0x%x , value:%d\n", dev_id, value);

	switch (dev_id) {
	case PMIC_SND_DMIC_MUTE:
	case PMIC_SND_AMIC_MUTE:
	case PMIC_SND_HP_MIC_MUTE:
		sc_access[0].reg_addr =	0x220;
		sc_access[1].reg_addr =	0x221;
		sc_access[2].reg_addr =	0x223;
		if (value == MUTE) {
			sc_access[0].value = 0x00;
			sc_access[1].value = 0x00;
			if (snd_pmic_ops_mx.input_dev_id == DMIC)
				sc_access[2].value = 0x00;
			else
				sc_access[2].value = 0x20;
		} else {
			sc_access[0].value = 0x20;
			sc_access[1].value = 0x20;
			if (snd_pmic_ops_mx.input_dev_id == DMIC)
				sc_access[2].value = 0x20;
			else
				sc_access[2].value = 0x00;
		}
		sc_access[0].mask = MASK5|MASK6;
		sc_access[1].mask = MASK5|MASK6;
		sc_access[2].mask = MASK5|MASK6;
		num_reg = 3;
		break;
	case PMIC_SND_LEFT_SPEAKER_MUTE:
	case PMIC_SND_LEFT_HP_MUTE:
		sc_access[0].reg_addr =	VOL_CTRL_LT;
		if (value == MUTE)
			sc_access[0].value = 0x40;
		else
			sc_access[0].value = 0x00;
		sc_access[0].mask = MASK6;
		num_reg = 1;
		snd_pmic_ops_mx.mute_status = value;
		break;
	case PMIC_SND_RIGHT_SPEAKER_MUTE:
	case PMIC_SND_RIGHT_HP_MUTE:
		sc_access[0].reg_addr = VOL_CTRL_RT;
		if (snd_pmic_ops_mx.num_channel == 1)
			value = MUTE;
		if (value == MUTE)
			sc_access[0].value = 0x40;
		else
			sc_access[0].value = 0x00;
		sc_access[0].mask = MASK6;
		num_reg = 1;
		snd_pmic_ops_mx.mute_status = value;
		break;
	case PMIC_SND_MUTE_ALL:
		sc_access[0].reg_addr = VOL_CTRL_RT;
		sc_access[1].reg_addr = VOL_CTRL_LT;
		sc_access[2].reg_addr =	0x220;
		sc_access[3].reg_addr =	0x221;
		sc_access[4].reg_addr =	0x223;
		snd_pmic_ops_mx.master_mute = value;
		if (value == MUTE) {
			sc_access[0].value = sc_access[1].value = 0x40;
			sc_access[2].value = 0x00;
			sc_access[3].value = 0x00;
			if (snd_pmic_ops_mx.input_dev_id == DMIC)
				sc_access[4].value = 0x00;
			else
				sc_access[4].value = 0x20;

		} else {
			sc_access[0].value = sc_access[1].value = 0x00;
			sc_access[2].value = sc_access[3].value = 0x20;
				sc_access[4].value = 0x20;
			if (snd_pmic_ops_mx.input_dev_id == DMIC)
				sc_access[4].value = 0x20;
			else
				sc_access[4].value = 0x00;


		}
		if (snd_pmic_ops_mx.num_channel == 1)
			sc_access[0].value = 0x40;
		sc_access[0].mask = sc_access[1].mask = MASK6;
		sc_access[2].mask = MASK5|MASK6;
		sc_access[3].mask = MASK5|MASK6|MASK2|MASK4;
		sc_access[4].mask = MASK5|MASK6|MASK4;

		num_reg = 5;
		break;
	case PMIC_SND_RECEIVER_MUTE:
		sc_access[0].reg_addr =  VOL_CTRL_RT;
		if (value == MUTE)
			sc_access[0].value = 0x40;
		else
			sc_access[0].value = 0x00;
		sc_access[0].mask = MASK6;
		num_reg = 1;
		break;
	}

	return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, num_reg);
}

static int mx_set_vol(int dev_id, int value)
{
	struct sc_reg_access sc_access[2] = {{0},};
	int num_reg = 0;
	int retval = 0;

	if (snd_pmic_ops_mx.card_status == SND_CARD_UN_INIT) {
		retval = mx_init_card();
		if (retval)
			return retval;
	}
	pr_debug("sst: set_vol dev_id:0x%x ,value:%d\n", dev_id, value);
	switch (dev_id) {
	case PMIC_SND_RECEIVER_VOL:
		return 0;
		break;
	case PMIC_SND_CAPTURE_VOL:
		sc_access[0].reg_addr =	0x220;
		sc_access[1].reg_addr =	0x221;
		sc_access[0].value = sc_access[1].value = -value;
		sc_access[0].mask = sc_access[1].mask =
			(MASK0|MASK1|MASK2|MASK3|MASK4);
		num_reg = 2;
		break;
	case PMIC_SND_LEFT_PB_VOL:
		sc_access[0].value = -value;
		sc_access[0].reg_addr = VOL_CTRL_LT;
		sc_access[0].mask = (MASK0|MASK1|MASK2|MASK3|MASK4|MASK5);
		num_reg = 1;
		break;
	case PMIC_SND_RIGHT_PB_VOL:
		sc_access[0].value = -value;
		sc_access[0].reg_addr = VOL_CTRL_RT;
		sc_access[0].mask = (MASK0|MASK1|MASK2|MASK3|MASK4|MASK5);
		if (snd_pmic_ops_mx.num_channel == 1) {
			sc_access[0].value = 0x40;
			sc_access[0].mask = MASK6;
			sc_access[0].reg_addr = VOL_CTRL_RT;
		}
		num_reg = 1;
		break;
	}
	return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, num_reg);
}

static int mx_get_mute(int dev_id, u8 *value)
{
	struct sc_reg_access sc_access[4] = {{0},};
	int retval = 0, num_reg = 0, mask = 0;

	if (snd_pmic_ops_mx.card_status == SND_CARD_UN_INIT) {
		retval = mx_init_card();
		if (retval)
			return retval;
	}
	switch (dev_id) {
	case PMIC_SND_DMIC_MUTE:
	case PMIC_SND_AMIC_MUTE:
	case PMIC_SND_HP_MIC_MUTE:
		sc_access[0].reg_addr = 0x220;
		mask = MASK5|MASK6;
		num_reg = 1;
		retval = sst_sc_reg_access(sc_access, PMIC_READ, num_reg);
		if (retval)
			return retval;
		*value = sc_access[0].value & mask;
		if (*value)
			*value = UNMUTE;
		else
			*value = MUTE;
		return retval;
	case PMIC_SND_LEFT_HP_MUTE:
	case PMIC_SND_LEFT_SPEAKER_MUTE:
		sc_access[0].reg_addr = VOL_CTRL_LT;
		num_reg = 1;
		mask = MASK6;
		break;
	case PMIC_SND_RIGHT_HP_MUTE:
	case PMIC_SND_RIGHT_SPEAKER_MUTE:
		sc_access[0].reg_addr = VOL_CTRL_RT;
		num_reg = 1;
		mask = MASK6;
		break;
	}
	retval = sst_sc_reg_access(sc_access, PMIC_READ, num_reg);
	if (retval)
		return retval;
	*value = sc_access[0].value & mask;
	if (*value)
		*value = MUTE;
	else
		*value = UNMUTE;
	return retval;
}

static int mx_get_vol(int dev_id, int *value)
{
	struct sc_reg_access sc_access = {0,};
	int retval = 0, mask = 0, num_reg = 0;

	if (snd_pmic_ops_mx.card_status == SND_CARD_UN_INIT) {
		retval = mx_init_card();
		if (retval)
			return retval;
	}
	switch (dev_id) {
	case PMIC_SND_CAPTURE_VOL:
		sc_access.reg_addr = 0x220;
		mask = MASK0|MASK1|MASK2|MASK3|MASK4;
		num_reg = 1;
		break;
	case PMIC_SND_LEFT_PB_VOL:
		sc_access.reg_addr = VOL_CTRL_LT;
		mask = MASK0|MASK1|MASK2|MASK3|MASK4|MASK5;
		num_reg = 1;
		break;
	case PMIC_SND_RIGHT_PB_VOL:
		sc_access.reg_addr = VOL_CTRL_RT;
		mask = MASK0|MASK1|MASK2|MASK3|MASK4|MASK5;
		num_reg = 1;
		break;
	}
	retval = sst_sc_reg_access(&sc_access, PMIC_READ, num_reg);
	if (retval)
		return retval;
	*value = -(sc_access.value & mask);
	pr_debug("sst: get volume value extracted %d\n", *value);
	return retval;
}

struct snd_pmic_ops snd_pmic_ops_mx = {
	.set_input_dev = mx_set_selected_input_dev,
	.set_output_dev = mx_set_selected_output_dev,
	.set_mute = mx_set_mute,
	.get_mute = mx_get_mute,
	.set_vol = mx_set_vol,
	.get_vol = mx_get_vol,
	.init_card = mx_init_card,
	.set_pcm_audio_params = mx_set_pcm_audio_params,
	.set_pcm_voice_params = mx_set_pcm_voice_params,
	.set_voice_port = mx_set_voice_port,
	.set_audio_port = mx_set_audio_port,
	.power_up_pmic_pb = mx_power_up_pb,
	.power_up_pmic_cp = mx_power_up_cp,
	.power_down_pmic_pb = mx_power_down_pb,
	.power_down_pmic_cp = mx_power_down_cp,
	.power_down_pmic =  mx_power_down,
};

