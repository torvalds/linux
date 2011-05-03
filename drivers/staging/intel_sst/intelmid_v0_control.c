/*
 *  intel_sst_v0_control.c - Intel SST Driver for audio engine
 *
 *  Copyright (C) 2008-10 Intel Corporation
 *  Authors:	Vinod Koul <vinod.koul@intel.com>
 *		Harsha Priya <priya.harsha@intel.com>
 *		Dharageswari R <dharageswari.r@intel.com>
 *		KP Jeeja <jeeja.kp@intel.com>
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
 *  This file contains the control operations of vendor 1
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/pci.h>
#include <linux/file.h>
#include <sound/control.h>
#include "intel_sst.h"
#include "intelmid_snd_control.h"
#include "intelmid.h"

enum _reg_v1 {
	VOICEPORT1 = 0x180,
	VOICEPORT2 = 0x181,
	AUDIOPORT1 = 0x182,
	AUDIOPORT2 = 0x183,
	MISCVOICECTRL = 0x184,
	MISCAUDCTRL = 0x185,
	DMICCTRL1 = 0x186,
	AUDIOBIAS = 0x187,
	MICCTRL = 0x188,
	MICLICTRL1 = 0x189,
	MICLICTRL2 = 0x18A,
	MICLICTRL3 = 0x18B,
	VOICEDACCTRL1 = 0x18C,
	STEREOADCCTRL = 0x18D,
	AUD15 = 0x18E,
	AUD16 = 0x18F,
	AUD17 = 0x190,
	AUD18 = 0x191,
	RMIXOUTSEL = 0x192,
	ANALOGLBR = 0x193,
	ANALOGLBL = 0x194,
	POWERCTRL1 = 0x195,
	POWERCTRL2 = 0x196,
	HEADSETDETECTINT = 0x197,
	HEADSETDETECTINTMASK = 0x198,
	TRIMENABLE = 0x199,
};

int rev_id = 0x20;
static bool jack_det_enabled;

/****
 * fs_init_card - initialize the sound card
 *
 * This initializes the audio paths to know values in case of this sound card
 */
static int fs_init_card(void)
{
	struct sc_reg_access sc_access[] = {
		{0x180, 0x00, 0x0},
		{0x181, 0x00, 0x0},
		{0x182, 0xF8, 0x0},
		{0x183, 0x08, 0x0},
		{0x184, 0x00, 0x0},
		{0x185, 0x40, 0x0},
		{0x186, 0x06, 0x0},
		{0x187, 0x80, 0x0},
		{0x188, 0x40, 0x0},
		{0x189, 0x39, 0x0},
		{0x18a, 0x39, 0x0},
		{0x18b, 0x1F, 0x0},
		{0x18c, 0x00, 0x0},
		{0x18d, 0x00, 0x0},
		{0x18e, 0x39, 0x0},
		{0x18f, 0x39, 0x0},
		{0x190, 0x39, 0x0},
		{0x191, 0x11, 0x0},
		{0x192, 0x0E, 0x0},
		{0x193, 0x00, 0x0},
		{0x194, 0x00, 0x0},
		{0x195, 0x00, 0x0},
		{0x196, 0x7C, 0x0},
		{0x197, 0x00, 0x0},
		{0x198, 0x0B, 0x0},
		{0x199, 0x00, 0x0},
		{0x037, 0x3F, 0x0},
	};

	snd_pmic_ops_fs.card_status = SND_CARD_INIT_DONE;
	snd_pmic_ops_fs.master_mute = UNMUTE;
	snd_pmic_ops_fs.mute_status = UNMUTE;
	snd_pmic_ops_fs.num_channel = 2;
	return sst_sc_reg_access(sc_access, PMIC_WRITE, 27);
}

static int fs_enable_audiodac(int value)
{
	struct sc_reg_access sc_access[3];
	sc_access[0].reg_addr = AUD16;
	sc_access[1].reg_addr = AUD17;
	sc_access[2].reg_addr = AUD15;
	sc_access[0].mask = sc_access[1].mask = sc_access[2].mask = MASK7;

	if (snd_pmic_ops_fs.mute_status == MUTE)
		return 0;
	if (value == MUTE) {
			sc_access[0].value = sc_access[1].value =
				sc_access[2].value = 0x80;

		} else {
			sc_access[0].value = sc_access[1].value =
			sc_access[2].value =  0x0;
		}
	if (snd_pmic_ops_fs.num_channel == 1)
		sc_access[1].value = sc_access[2].value = 0x80;
	return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 3);

}

static int fs_power_up_pb(unsigned int port)
{
	struct sc_reg_access sc_access[] = {
		{AUDIOBIAS, 0x00, MASK7},
		{POWERCTRL1, 0xC6, 0xC6},
		{POWERCTRL2, 0x30, 0x30},

	};
	int retval = 0;

	if (snd_pmic_ops_fs.card_status == SND_CARD_UN_INIT)
		retval = fs_init_card();
	if (retval)
		return retval;
	retval = fs_enable_audiodac(MUTE);
	retval = sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 3);

	if (retval)
		return retval;

	pr_debug("in fs power up pb\n");
	return fs_enable_audiodac(UNMUTE);
}

static int fs_power_down_pb(unsigned int device)
{
	struct sc_reg_access sc_access[] = {
		{POWERCTRL1, 0x00, 0xC6},
		{POWERCTRL2, 0x00, 0x30},
	};
	int retval = 0;

	if (snd_pmic_ops_fs.card_status == SND_CARD_UN_INIT)
		retval = fs_init_card();
	if (retval)
		return retval;
	retval = fs_enable_audiodac(MUTE);
	retval = sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 2);

	if (retval)
		return retval;

	pr_debug("in fsl power down pb\n");
	return fs_enable_audiodac(UNMUTE);
}

static int fs_power_up_cp(unsigned int port)
{
	struct sc_reg_access sc_access[] = {
		{POWERCTRL2, 0x32, 0x32}, /*NOTE power up A ADC only as*/
		{AUDIOBIAS, 0x00, MASK7},
					/*as turning on V ADC causes noise*/
	};
	int retval = 0;

	if (snd_pmic_ops_fs.card_status == SND_CARD_UN_INIT)
		retval = fs_init_card();
	if (retval)
		return retval;
	return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 2);
}

static int fs_power_down_cp(unsigned int device)
{
	struct sc_reg_access sc_access[] = {
		{POWERCTRL2, 0x00, 0x03},
	};
	int retval = 0;

	if (snd_pmic_ops_fs.card_status == SND_CARD_UN_INIT)
		retval = fs_init_card();
	if (retval)
		return retval;
	return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 1);
}

static int fs_power_down(void)
{
	int retval = 0;
	struct sc_reg_access sc_access[] = {
		{AUDIOBIAS, MASK7, MASK7},
	};

	if (snd_pmic_ops_fs.card_status == SND_CARD_UN_INIT)
		retval = fs_init_card();
	if (retval)
		return retval;
	return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 1);
}

static int fs_set_pcm_voice_params(void)
{
	struct sc_reg_access sc_access[] = {
		{0x180, 0xA0, 0},
		{0x181, 0x04, 0},
		{0x182, 0x0, 0},
		{0x183, 0x0, 0},
		{0x184, 0x18, 0},
		{0x185, 0x40, 0},
		{0x186, 0x06, 0},
		{0x187, 0x0, 0},
		{0x188, 0x10, 0},
		{0x189, 0x39, 0},
		{0x18a, 0x39, 0},
		{0x18b, 0x02, 0},
		{0x18c, 0x0, 0},
		{0x18d, 0x0, 0},
		{0x18e, 0x39, 0},
		{0x18f, 0x0, 0},
		{0x190, 0x0, 0},
		{0x191, 0x20, 0},
		{0x192, 0x20, 0},
		{0x193, 0x0, 0},
		{0x194, 0x0, 0},
		{0x195, 0x06, 0},
		{0x196, 0x25, 0},
		{0x197, 0x0, 0},
		{0x198, 0xF, 0},
		{0x199, 0x0, 0},
	};
	int retval = 0;

	if (snd_pmic_ops_fs.card_status == SND_CARD_UN_INIT)
		retval = fs_init_card();
	if (retval)
		return retval;
	return sst_sc_reg_access(sc_access, PMIC_WRITE, 26);
}

static int fs_set_audio_port(int status)
{
	struct sc_reg_access sc_access[2];
	int retval = 0;

	if (snd_pmic_ops_fs.card_status == SND_CARD_UN_INIT)
		retval = fs_init_card();
	if (retval)
		return retval;
	if (status == DEACTIVATE) {
		/* Deactivate audio port-tristate and power */
		sc_access[0].value = 0x00;
		sc_access[0].mask = MASK6|MASK7;
		sc_access[0].reg_addr = AUDIOPORT1;
		sc_access[1].value = 0x00;
		sc_access[1].mask = MASK4|MASK5;
		sc_access[1].reg_addr = POWERCTRL2;
		return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 2);
	} else if (status == ACTIVATE) {
		/* activate audio port */
		sc_access[0].value = 0xC0;
		sc_access[0].mask = MASK6|MASK7;
		sc_access[0].reg_addr = AUDIOPORT1;
		sc_access[1].value = 0x30;
		sc_access[1].mask = MASK4|MASK5;
		sc_access[1].reg_addr = POWERCTRL2;
		return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 2);
	} else
		return -EINVAL;
}

static int fs_set_voice_port(int status)
{
	struct sc_reg_access sc_access[2];
	int retval = 0;

	if (snd_pmic_ops_fs.card_status == SND_CARD_UN_INIT)
		retval = fs_init_card();
	if (retval)
		return retval;
	if (status == DEACTIVATE) {
		/* Deactivate audio port-tristate and power */
		sc_access[0].value = 0x00;
		sc_access[0].mask = MASK6|MASK7;
		sc_access[0].reg_addr = VOICEPORT1;
		sc_access[1].value = 0x00;
		sc_access[1].mask = MASK0|MASK1;
		sc_access[1].reg_addr = POWERCTRL2;
		return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 2);
	} else if (status == ACTIVATE) {
		/* activate audio port */
		sc_access[0].value = 0xC0;
		sc_access[0].mask = MASK6|MASK7;
		sc_access[0].reg_addr = VOICEPORT1;
		sc_access[1].value = 0x03;
		sc_access[1].mask = MASK0|MASK1;
		sc_access[1].reg_addr = POWERCTRL2;
		return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 2);
	} else
		return -EINVAL;
}

static int fs_set_pcm_audio_params(int sfreq, int word_size, int num_channel)
{
	u8 config1 = 0;
	struct sc_reg_access sc_access[4];
	int retval = 0, num_value = 0;

	if (snd_pmic_ops_fs.card_status == SND_CARD_UN_INIT)
		retval = fs_init_card();
	if (retval)
		return retval;
	switch (sfreq) {
	case 8000:
		config1 = 0x00;
		break;
	case 11025:
		config1 = 0x01;
		break;
	case 12000:
		config1 = 0x02;
		break;
	case 16000:
		config1 = 0x03;
		break;
	case 22050:
		config1 = 0x04;
		break;
	case 24000:
		config1 = 0x05;
		break;
	case 26000:
		config1 = 0x06;
		break;
	case 32000:
		config1 = 0x07;
		break;
	case 44100:
		config1 = 0x08;
		break;
	case 48000:
		config1 = 0x09;
		break;
	}
	snd_pmic_ops_fs.num_channel = num_channel;
	if (snd_pmic_ops_fs.num_channel == 1)	{
		sc_access[0].reg_addr = AUD17;
		sc_access[1].reg_addr = AUD15;
		sc_access[0].mask = sc_access[1].mask = MASK7;
		sc_access[0].value = sc_access[1].value = 0x80;
		sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 2);

	} else {
		sc_access[0].reg_addr = AUD17;
		sc_access[1].reg_addr = AUD15;
		sc_access[0].mask = sc_access[1].mask = MASK7;
		sc_access[0].value = sc_access[1].value = 0x00;
		sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 2);

	}
	pr_debug("sfreq:%d,Register value = %x\n", sfreq, config1);

	if (word_size == 24) {
		sc_access[0].reg_addr  = AUDIOPORT1;
		sc_access[0].mask = MASK0|MASK1|MASK2|MASK3;
		sc_access[0].value = 0xFB;


		sc_access[1].reg_addr  = AUDIOPORT2;
		sc_access[1].value = config1 | 0x10;
		sc_access[1].mask = MASK0 | MASK1 | MASK2 | MASK3
					| MASK4 | MASK5 | MASK6;

		sc_access[2].reg_addr  = MISCAUDCTRL;
		sc_access[2].value = 0x02;
		sc_access[2].mask = 0x02;

		num_value = 3 ;

	} else {

		sc_access[0].reg_addr  = AUDIOPORT2;
		sc_access[0].value = config1;
		sc_access[0].mask = MASK0|MASK1|MASK2|MASK3;

		sc_access[1].reg_addr  = MISCAUDCTRL;
		sc_access[1].value = 0x00;
		sc_access[1].mask = 0x02;
		num_value = 2;
	}
	return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, num_value);

}

static int fs_set_selected_input_dev(u8 value)
{
	struct sc_reg_access sc_access_dmic[] = {
		{MICCTRL, 0x81, 0xf7},
		{MICLICTRL3, 0x00, 0xE0},
	};
	struct sc_reg_access sc_access_mic[] = {
		{MICCTRL, 0x40, MASK2|MASK4|MASK5|MASK6|MASK7},
		{MICLICTRL3, 0x00, 0xE0},
	};
	struct sc_reg_access sc_access_hsmic[] = {
		{MICCTRL, 0x10, MASK2|MASK4|MASK5|MASK6|MASK7},
		{MICLICTRL3, 0x00, 0xE0},
	};

	int retval = 0;

	if (snd_pmic_ops_fs.card_status == SND_CARD_UN_INIT)
		retval = fs_init_card();
	if (retval)
		return retval;

	switch (value) {
	case AMIC:
		pr_debug("Selecting amic not supported in mono cfg\n");
		return sst_sc_reg_access(sc_access_mic, PMIC_READ_MODIFY, 2);
		break;

	case HS_MIC:
		pr_debug("Selecting hsmic\n");
		return sst_sc_reg_access(sc_access_hsmic,
				PMIC_READ_MODIFY, 2);
		break;

	case DMIC:
		pr_debug("Selecting dmic\n");
		return sst_sc_reg_access(sc_access_dmic, PMIC_READ_MODIFY, 2);
		break;

	default:
		return -EINVAL;

	}
}

static int fs_set_selected_output_dev(u8 value)
{
	struct sc_reg_access sc_access_hp[] = {
		{0x191, 0x11, 0x0},
		{0x192, 0x0E, 0x0},
	};
	struct sc_reg_access sc_access_is[] = {
		{0x191, 0x17, 0xFF},
		{0x192, 0x08, 0xFF},
	};
	int retval = 0;

	if (snd_pmic_ops_fs.card_status == SND_CARD_UN_INIT)
		retval = fs_init_card();
	if (retval)
		return retval;

	switch (value) {
	case STEREO_HEADPHONE:
		pr_debug("SST DBG:Selecting headphone\n");
		return sst_sc_reg_access(sc_access_hp, PMIC_WRITE, 2);
		break;
	case MONO_EARPIECE:
	case INTERNAL_SPKR:
		pr_debug("SST DBG:Selecting internal spkr\n");
		return sst_sc_reg_access(sc_access_is, PMIC_READ_MODIFY, 2);
		break;

	default:
		return -EINVAL;

	}
}

static int fs_set_mute(int dev_id, u8 value)
{
	struct sc_reg_access sc_access[6] = {{0,},};
	int reg_num = 0;
	int retval = 0;

	if (snd_pmic_ops_fs.card_status == SND_CARD_UN_INIT)
		retval = fs_init_card();
	if (retval)
		return retval;


	pr_debug("dev_id:0x%x value:0x%x\n", dev_id, value);
	switch (dev_id) {
	case PMIC_SND_DMIC_MUTE:
		sc_access[0].reg_addr = MICCTRL;
		sc_access[1].reg_addr = MICLICTRL1;
		sc_access[2].reg_addr = MICLICTRL2;
		sc_access[0].mask = MASK5;
		sc_access[1].mask = sc_access[2].mask = MASK6;
		if (value == MUTE) {
			sc_access[0].value = 0x20;
			sc_access[2].value = sc_access[1].value = 0x40;
		} else
			sc_access[0].value = sc_access[1].value
			= sc_access[2].value = 0x0;
		reg_num = 3;
		break;
	case PMIC_SND_HP_MIC_MUTE:
	case PMIC_SND_AMIC_MUTE:
		sc_access[0].reg_addr = MICLICTRL1;
		sc_access[1].reg_addr = MICLICTRL2;
		sc_access[0].mask = sc_access[1].mask = MASK6;
		if (value == MUTE)
			sc_access[0].value = sc_access[1].value = 0x40;
		else
			sc_access[0].value = sc_access[1].value = 0x0;
		reg_num = 2;
		break;
	case PMIC_SND_LEFT_SPEAKER_MUTE:
	case PMIC_SND_LEFT_HP_MUTE:
		sc_access[0].reg_addr = AUD16;
		sc_access[1].reg_addr = AUD15;

		sc_access[0].mask = sc_access[1].mask = MASK7;
		if (value == MUTE)
			sc_access[0].value = sc_access[1].value = 0x80;
		else
			sc_access[0].value = sc_access[1].value = 0x0;
		reg_num = 2;
		snd_pmic_ops_fs.mute_status = value;
		break;
	case PMIC_SND_RIGHT_HP_MUTE:
	case PMIC_SND_RIGHT_SPEAKER_MUTE:
		sc_access[0].reg_addr = AUD17;
		sc_access[1].reg_addr = AUD15;
		sc_access[0].mask = sc_access[1].mask = MASK7;
		if (value == MUTE)
			sc_access[0].value = sc_access[1].value = 0x80;
		else
			sc_access[0].value = sc_access[1].value = 0x0;
		snd_pmic_ops_fs.mute_status = value;
		if (snd_pmic_ops_fs.num_channel == 1)
			sc_access[0].value = sc_access[1].value = 0x80;
		reg_num = 2;
		break;
	case PMIC_SND_MUTE_ALL:
		sc_access[0].reg_addr = AUD16;
		sc_access[1].reg_addr = AUD17;
		sc_access[2].reg_addr = AUD15;
		sc_access[3].reg_addr = MICCTRL;
		sc_access[4].reg_addr = MICLICTRL1;
		sc_access[5].reg_addr = MICLICTRL2;
		sc_access[0].mask = sc_access[1].mask =
				sc_access[2].mask = MASK7;
		sc_access[3].mask = MASK5;
		sc_access[4].mask = sc_access[5].mask = MASK6;

		if (value == MUTE) {
			sc_access[0].value =
			sc_access[1].value = sc_access[2].value = 0x80;
			sc_access[3].value = 0x20;
			sc_access[4].value = sc_access[5].value = 0x40;

		} else {
			sc_access[0].value = sc_access[1].value =
			sc_access[2].value = sc_access[3].value =
			sc_access[4].value = sc_access[5].value = 0x0;
		}
		if (snd_pmic_ops_fs.num_channel == 1)
			sc_access[1].value = sc_access[2].value = 0x80;
		reg_num = 6;
		snd_pmic_ops_fs.mute_status = value;
		snd_pmic_ops_fs.master_mute = value;
		break;

	}
	return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, reg_num);
}

static int fs_set_vol(int dev_id, int value)
{
	struct sc_reg_access sc_acces, sc_access[4] = {{0},};
	int reg_num = 0;
	int retval = 0;

	if (snd_pmic_ops_fs.card_status == SND_CARD_UN_INIT)
		retval = fs_init_card();
	if (retval)
		return retval;

	switch (dev_id) {
	case PMIC_SND_LEFT_PB_VOL:
		pr_debug("PMIC_SND_LEFT_PB_VOL:%d\n", value);
		sc_access[0].value = sc_access[1].value = value;
		sc_access[0].reg_addr = AUD16;
		sc_access[1].reg_addr = AUD15;
		sc_access[0].mask = sc_access[1].mask =
			(MASK0|MASK1|MASK2|MASK3|MASK4|MASK5);
		reg_num = 2;
		break;

	case PMIC_SND_RIGHT_PB_VOL:
		pr_debug("PMIC_SND_RIGHT_PB_VOL:%d\n", value);
		sc_access[0].value = sc_access[1].value = value;
		sc_access[0].reg_addr = AUD17;
		sc_access[1].reg_addr = AUD15;
		sc_access[0].mask = sc_access[1].mask =
			(MASK0|MASK1|MASK2|MASK3|MASK4|MASK5);
		if (snd_pmic_ops_fs.num_channel == 1) {
			sc_access[0].value = sc_access[1].value = 0x80;
			sc_access[0].mask = sc_access[1].mask = MASK7;
		}
		reg_num = 2;
		break;
	case PMIC_SND_CAPTURE_VOL:
		pr_debug("PMIC_SND_CAPTURE_VOL:%d\n", value);
		sc_access[0].reg_addr = MICLICTRL1;
		sc_access[1].reg_addr = MICLICTRL2;
		sc_access[2].reg_addr = DMICCTRL1;
		sc_access[2].value = value;
		sc_access[0].value = sc_access[1].value = value;
		sc_acces.reg_addr = MICLICTRL3;
		sc_acces.value = value;
		sc_acces.mask = (MASK0|MASK1|MASK2|MASK3|MASK5|MASK6|MASK7);
		retval = sst_sc_reg_access(&sc_acces, PMIC_READ_MODIFY, 1);
		sc_access[0].mask = sc_access[1].mask =
		sc_access[2].mask = (MASK0|MASK1|MASK2|MASK3|MASK4|MASK5);
		reg_num = 3;
		break;

	default:
		return -EINVAL;
	}

	return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, reg_num);
}

static int fs_get_mute(int dev_id, u8 *value)
{
	struct sc_reg_access sc_access[6] = {{0,},};

	int retval = 0, temp_value = 0, mask = 0;

	if (snd_pmic_ops_fs.card_status == SND_CARD_UN_INIT)
		retval = fs_init_card();
	if (retval)
		return retval;

	switch (dev_id) {

	case PMIC_SND_AMIC_MUTE:
	case PMIC_SND_HP_MIC_MUTE:
		sc_access[0].reg_addr = MICLICTRL1;
		mask = MASK6;
		retval = sst_sc_reg_access(sc_access, PMIC_READ, 1);
		if (sc_access[0].value & mask)
			*value = MUTE;
		else
			*value = UNMUTE;
		break;
	case PMIC_SND_DMIC_MUTE:
		sc_access[0].reg_addr = MICCTRL;
		mask = MASK5;
		retval = sst_sc_reg_access(sc_access, PMIC_READ, 1);
		temp_value = (sc_access[0].value & mask);
		if (temp_value == 0)
			*value = UNMUTE;
		else
			*value = MUTE;
		break;

	case PMIC_SND_LEFT_HP_MUTE:
	case PMIC_SND_LEFT_SPEAKER_MUTE:
		sc_access[0].reg_addr = AUD16;
		mask = MASK7;
		retval = sst_sc_reg_access(sc_access, PMIC_READ, 1);
		temp_value = sc_access[0].value & mask;
		if (temp_value == 0)
			*value = UNMUTE;
		else
			*value = MUTE;
		break;
	case PMIC_SND_RIGHT_HP_MUTE:
	case PMIC_SND_RIGHT_SPEAKER_MUTE:
		sc_access[0].reg_addr = AUD17;
		mask = MASK7;
		retval = sst_sc_reg_access(sc_access, PMIC_READ, 1);
		temp_value = sc_access[0].value & mask;
		if (temp_value == 0)
			*value = UNMUTE;
		else
			*value = MUTE;
		break;
	default:
		return -EINVAL;
	}

	return retval;
}

static int fs_get_vol(int dev_id, int *value)
{
	struct sc_reg_access sc_access = {0,};
	int retval = 0, mask = 0;

	if (snd_pmic_ops_fs.card_status == SND_CARD_UN_INIT)
		retval = fs_init_card();
	if (retval)
		return retval;

	switch (dev_id) {
	case PMIC_SND_CAPTURE_VOL:
		pr_debug("PMIC_SND_CAPTURE_VOL\n");
		sc_access.reg_addr = MICLICTRL1;
		mask = (MASK5|MASK4|MASK3|MASK2|MASK1|MASK0);
		break;
	case PMIC_SND_LEFT_PB_VOL:
		pr_debug("PMIC_SND_LEFT_PB_VOL\n");
		sc_access.reg_addr = AUD16;
		mask = (MASK5|MASK4|MASK3|MASK2|MASK1|MASK0);
		break;
	case PMIC_SND_RIGHT_PB_VOL:
		pr_debug("PMIC_SND_RT_PB_VOL\n");
		sc_access.reg_addr = AUD17;
		mask = (MASK5|MASK4|MASK3|MASK2|MASK1|MASK0);
		break;
	default:
		return -EINVAL;
	}

	retval = sst_sc_reg_access(&sc_access, PMIC_READ, 1);
	pr_debug("value read = 0x%x\n", sc_access.value);
	*value = (int) (sc_access.value & mask);
	pr_debug("value returned = 0x%x\n", *value);
	return retval;
}

static void fs_pmic_irq_enable(void *data)
{
	struct snd_intelmad *intelmaddata = data;
	struct sc_reg_access sc_access[] = {
				{0x187, 0x00, MASK7},
				{0x188, 0x10, MASK4},
				{0x18b, 0x10, MASK4},
	};

	struct sc_reg_access sc_access_write[] = {
				{0x198, 0x00, 0x0},
	};
	pr_debug("Audio interrupt enable\n");
	sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 3);
	sst_sc_reg_access(sc_access_write, PMIC_WRITE, 1);

	intelmaddata->jack[0].jack_status = 0;
	/*intelmaddata->jack[1].jack_status = 0;*/

	jack_det_enabled = true;
	return;
}

static void fs_pmic_irq_cb(void *cb_data, u8 value)
{
	struct mad_jack *mjack = NULL;
	struct snd_intelmad *intelmaddata = cb_data;
	unsigned int present = 0, jack_event_flag = 0, buttonpressflag = 0;

	mjack = &intelmaddata->jack[0];

	if (value & 0x4) {
		if (!jack_det_enabled)
			fs_pmic_irq_enable(intelmaddata);

		/* send headphone detect */
		pr_debug(":MAD headphone %d\n", value & 0x4);
		present = !(mjack->jack_status);
		mjack->jack_status = present;
		jack_event_flag = 1;
		mjack->jack.type = SND_JACK_HEADPHONE;
	}

	if (value & 0x2) {
		/* send short push */
		pr_debug(":MAD short push %d\n", value & 0x2);
		present = 1;
		jack_event_flag = 1;
		buttonpressflag = 1;
		mjack->jack.type = MID_JACK_HS_SHORT_PRESS;
	}

	if (value & 0x1) {
		/* send long push */
		pr_debug(":MAD long push %d\n", value & 0x1);
		present = 1;
		jack_event_flag = 1;
		buttonpressflag = 1;
		mjack->jack.type = MID_JACK_HS_LONG_PRESS;
	}

	if (value & 0x8) {
		if (!jack_det_enabled)
			fs_pmic_irq_enable(intelmaddata);
		/* send headset detect */
		pr_debug(":MAD headset = %d\n", value & 0x8);
		present = !(mjack->jack_status);
		mjack->jack_status = present;
		jack_event_flag = 1;
		mjack->jack.type = SND_JACK_HEADSET;
	}


	if (jack_event_flag)
		sst_mad_send_jack_report(&mjack->jack,
						buttonpressflag, present);

	return;
}
static int fs_jack_enable(void)
{
	return 0;
}

struct snd_pmic_ops snd_pmic_ops_fs = {
	.set_input_dev = fs_set_selected_input_dev,
	.set_output_dev = fs_set_selected_output_dev,
	.set_mute = fs_set_mute,
	.get_mute = fs_get_mute,
	.set_vol = fs_set_vol,
	.get_vol = fs_get_vol,
	.init_card = fs_init_card,
	.set_pcm_audio_params = fs_set_pcm_audio_params,
	.set_pcm_voice_params = fs_set_pcm_voice_params,
	.set_voice_port = fs_set_voice_port,
	.set_audio_port = fs_set_audio_port,
	.power_up_pmic_pb =	fs_power_up_pb,
	.power_up_pmic_cp =	fs_power_up_cp,
	.power_down_pmic_pb =	fs_power_down_pb,
	.power_down_pmic_cp =	fs_power_down_cp,
	.power_down_pmic	=	fs_power_down,
	.pmic_irq_cb	=	fs_pmic_irq_cb,
	/*
	 * Jack detection enabling
	 * need be delayed till first IRQ happen.
	 */
	.pmic_irq_enable =	NULL,
	.pmic_jack_enable = fs_jack_enable,
};
