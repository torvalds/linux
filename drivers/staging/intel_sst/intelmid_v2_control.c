/*
 *  intelmid_v2_control.c - Intel Sound card driver for MID
 *
 *  Copyright (C) 2008-10 Intel Corp
 *  Authors:	Vinod Koul <vinod.koul@intel.com>
 *		Harsha Priya <priya.harsha@intel.com>
 *		KP Jeeja <jeeja.kp@intel.com>
 *		Dharageswari R <dharageswari.r@intel.com>
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
 *  This file contains the control operations of vendor 3
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/gpio.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/file.h>
#include <sound/control.h>
#include "intel_sst.h"
#include "intelmid_snd_control.h"
#include "intelmid.h"

enum reg_v3 {
	VAUDIOCNT = 0x51,
	VOICEPORT1 = 0x100,
	VOICEPORT2 = 0x101,
	AUDIOPORT1 = 0x102,
	AUDIOPORT2 = 0x103,
	ADCSAMPLERATE = 0x104,
	DMICCTRL1 = 0x105,
	DMICCTRL2 = 0x106,
	MICCTRL = 0x107,
	MICSELVOL = 0x108,
	LILSEL = 0x109,
	LIRSEL = 0x10a,
	VOICEVOL = 0x10b,
	AUDIOLVOL = 0x10c,
	AUDIORVOL = 0x10d,
	LMUTE = 0x10e,
	RMUTE = 0x10f,
	POWERCTRL1 = 0x110,
	POWERCTRL2 = 0x111,
	DRVPOWERCTRL = 0x112,
	VREFPLL = 0x113,
	PCMBUFCTRL = 0x114,
	SOFTMUTE = 0x115,
	DTMFPATH = 0x116,
	DTMFVOL = 0x117,
	DTMFFREQ = 0x118,
	DTMFHFREQ = 0x119,
	DTMFLFREQ = 0x11a,
	DTMFCTRL = 0x11b,
	DTMFASON = 0x11c,
	DTMFASOFF = 0x11d,
	DTMFASINUM = 0x11e,
	CLASSDVOL = 0x11f,
	VOICEDACAVOL = 0x120,
	AUDDACAVOL = 0x121,
	LOMUTEVOL = 0x122,
	HPLVOL = 0x123,
	HPRVOL = 0x124,
	MONOVOL = 0x125,
	LINEOUTMIXVOL = 0x126,
	EPMIXVOL = 0x127,
	LINEOUTLSEL = 0x128,
	LINEOUTRSEL = 0x129,
	EPMIXOUTSEL = 0x12a,
	HPLMIXSEL = 0x12b,
	HPRMIXSEL = 0x12c,
	LOANTIPOP = 0x12d,
	AUXDBNC = 0x12f,
};

static void nc_set_amp_power(int power)
{
	if (snd_pmic_ops_nc.gpio_amp)
		gpio_set_value(snd_pmic_ops_nc.gpio_amp, power);
}

/****
 * nc_init_card - initialize the sound card
 *
 * This initializes the audio paths to know values in case of this sound card
 */
static int nc_init_card(void)
{
	struct sc_reg_access sc_access[] = {
		{VAUDIOCNT, 0x25, 0},
		{VOICEPORT1, 0x00, 0},
		{VOICEPORT2, 0x00, 0},
		{AUDIOPORT1, 0x98, 0},
		{AUDIOPORT2, 0x09, 0},
		{AUDIOLVOL, 0x00, 0},
		{AUDIORVOL, 0x00, 0},
		{LMUTE, 0x03, 0},
		{RMUTE, 0x03, 0},
		{POWERCTRL1, 0x00, 0},
		{POWERCTRL2, 0x00, 0},
		{DRVPOWERCTRL, 0x00, 0},
		{VREFPLL, 0x10, 0},
		{HPLMIXSEL, 0xee, 0},
		{HPRMIXSEL, 0xf6, 0},
		{PCMBUFCTRL, 0x0, 0},
		{VOICEVOL, 0x0e, 0},
		{HPLVOL, 0x06, 0},
		{HPRVOL, 0x06, 0},
		{MICCTRL, 0x51, 0x00},
		{ADCSAMPLERATE, 0x8B, 0x00},
		{MICSELVOL, 0x5B, 0x00},
		{LILSEL, 0x06, 0},
		{LIRSEL, 0x46, 0},
		{LOANTIPOP, 0x00, 0},
		{DMICCTRL1, 0x40, 0},
		{AUXDBNC, 0xff, 0},
	};
	snd_pmic_ops_nc.card_status = SND_CARD_INIT_DONE;
	snd_pmic_ops_nc.master_mute = UNMUTE;
	snd_pmic_ops_nc.mute_status = UNMUTE;
	sst_sc_reg_access(sc_access, PMIC_WRITE, 27);
	mutex_init(&snd_pmic_ops_nc.lock);
	pr_debug("init complete!!\n");
	return 0;
}

static int nc_enable_audiodac(int value)
{
	struct sc_reg_access sc_access[3];
	int mute_val = 0;

	if (snd_pmic_ops_nc.mute_status == MUTE)
		return 0;

	if (((snd_pmic_ops_nc.output_dev_id == MONO_EARPIECE) ||
		(snd_pmic_ops_nc.output_dev_id == INTERNAL_SPKR)) &&
		(value == UNMUTE))
		return 0;
	if (value == UNMUTE) {
			/* unmute the system, set the 7th bit to zero */
			mute_val = 0x00;
		} else {
			/* MUTE:Set the seventh bit */
			mute_val = 0x04;

		}
		sc_access[0].reg_addr = LMUTE;
		sc_access[1].reg_addr = RMUTE;
		sc_access[0].mask = sc_access[1].mask = MASK2;
		sc_access[0].value = sc_access[1].value = mute_val;

		if (snd_pmic_ops_nc.num_channel == 1)
			sc_access[1].value = 0x04;
		return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 2);

}

static int nc_power_up_pb(unsigned int port)
{
	struct sc_reg_access sc_access[7];
	int retval = 0;

	if (snd_pmic_ops_nc.card_status == SND_CARD_UN_INIT)
		retval = nc_init_card();
	if (retval)
		return retval;
	if (port == 0xFF)
		return 0;
	mutex_lock(&snd_pmic_ops_nc.lock);
	nc_enable_audiodac(MUTE);
	msleep(30);

	pr_debug("powering up pb....\n");

	sc_access[0].reg_addr = VAUDIOCNT;
	sc_access[0].value = 0x27;
	sc_access[0].mask = 0x27;
	sc_access[1].reg_addr = VREFPLL;
	if (port == 0) {
		sc_access[1].value = 0x3A;
		sc_access[1].mask = 0x3A;
	} else if (port == 1) {
		sc_access[1].value = 0x35;
		sc_access[1].mask = 0x35;
	}
	retval =  sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 2);



	sc_access[0].reg_addr = POWERCTRL1;
	if (port == 0) {
		sc_access[0].value = 0x40;
		sc_access[0].mask = 0x40;
	} else if (port == 1) {
		sc_access[0].value = 0x01;
		sc_access[0].mask = 0x01;
	}
	sc_access[1].reg_addr = POWERCTRL2;
	sc_access[1].value = 0x0C;
	sc_access[1].mask = 0x0C;

	sc_access[2].reg_addr = DRVPOWERCTRL;
	sc_access[2].value = 0x86;
	sc_access[2].mask = 0x86;

	sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 3);

	msleep(30);

	snd_pmic_ops_nc.pb_on = 1;

	/*
	 * There is a mismatch between Playback Sources and the enumerated
	 * values of output sources.  This mismatch causes ALSA upper to send
	 * Item 1 for Internal Speaker, but the expected enumeration is 2!  For
	 * now, treat MONO_EARPIECE and INTERNAL_SPKR identically and power up
	 * the needed resources
	 */
	if (snd_pmic_ops_nc.output_dev_id == MONO_EARPIECE ||
	    snd_pmic_ops_nc.output_dev_id == INTERNAL_SPKR)
		nc_set_amp_power(1);
	nc_enable_audiodac(UNMUTE);
	mutex_unlock(&snd_pmic_ops_nc.lock);
	return 0;
}

static int nc_power_up_cp(unsigned int port)
{
	struct sc_reg_access sc_access[5];
	int retval = 0;


	if (snd_pmic_ops_nc.card_status == SND_CARD_UN_INIT)
		retval = nc_init_card();
	if (retval)
		return retval;


	pr_debug("powering up cp....\n");

	if (port == 0xFF)
		return 0;
	sc_access[0].reg_addr = VAUDIOCNT;
	sc_access[0].value = 0x27;
	sc_access[0].mask = 0x27;
	sc_access[1].reg_addr = VREFPLL;
	if (port == 0) {
		sc_access[1].value = 0x3E;
		sc_access[1].mask = 0x3E;
	} else if (port == 1) {
		sc_access[1].value = 0x35;
		sc_access[1].mask = 0x35;
	}

	retval =  sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 2);


	sc_access[0].reg_addr = POWERCTRL1;
	if (port == 0) {
		sc_access[0].value = 0xB4;
		sc_access[0].mask = 0xB4;
	} else if (port == 1) {
		sc_access[0].value = 0xBF;
		sc_access[0].mask = 0xBF;
	}
	sc_access[1].reg_addr = POWERCTRL2;
	if (port == 0) {
		sc_access[1].value = 0x0C;
		sc_access[1].mask = 0x0C;
	} else if (port == 1) {
		sc_access[1].value = 0x02;
		sc_access[1].mask = 0x02;
	}

	return  sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 2);

}

static int nc_power_down(void)
{
	int retval = 0;
	struct sc_reg_access sc_access[5];

	if (snd_pmic_ops_nc.card_status == SND_CARD_UN_INIT)
		retval = nc_init_card();
	if (retval)
		return retval;
	nc_enable_audiodac(MUTE);


	pr_debug("powering dn nc_power_down ....\n");

	if (snd_pmic_ops_nc.output_dev_id == MONO_EARPIECE ||
	    snd_pmic_ops_nc.output_dev_id == INTERNAL_SPKR)
		nc_set_amp_power(0);

	msleep(30);

	sc_access[0].reg_addr = DRVPOWERCTRL;
	sc_access[0].value = 0x00;
	sc_access[0].mask = 0x00;

	sst_sc_reg_access(sc_access, PMIC_WRITE, 1);

	sc_access[0].reg_addr = POWERCTRL1;
	sc_access[0].value = 0x00;
	sc_access[0].mask = 0x00;

	sc_access[1].reg_addr = POWERCTRL2;
	sc_access[1].value = 0x00;
	sc_access[1].mask = 0x00;



	sst_sc_reg_access(sc_access, PMIC_WRITE, 2);

	msleep(30);
	sc_access[0].reg_addr = VREFPLL;
	sc_access[0].value = 0x10;
	sc_access[0].mask = 0x10;

	sc_access[1].reg_addr = VAUDIOCNT;
	sc_access[1].value = 0x25;
	sc_access[1].mask = 0x25;


	retval =  sst_sc_reg_access(sc_access, PMIC_WRITE, 2);

	msleep(30);
	return nc_enable_audiodac(UNMUTE);
}

static int nc_power_down_pb(unsigned int device)
{

	int retval = 0;
	struct sc_reg_access sc_access[5];

	if (snd_pmic_ops_nc.card_status == SND_CARD_UN_INIT)
		retval = nc_init_card();
	if (retval)
		return retval;

	pr_debug("powering dn pb....\n");
	mutex_lock(&snd_pmic_ops_nc.lock);
	nc_enable_audiodac(MUTE);


	msleep(30);


	sc_access[0].reg_addr = DRVPOWERCTRL;
	sc_access[0].value = 0x00;
	sc_access[0].mask = 0x00;

	sst_sc_reg_access(sc_access, PMIC_WRITE, 1);

	msleep(30);

	sc_access[0].reg_addr = POWERCTRL1;
	sc_access[0].value = 0x00;
	sc_access[0].mask = 0x41;

	sc_access[1].reg_addr = POWERCTRL2;
	sc_access[1].value = 0x00;
	sc_access[1].mask = 0x0C;

	sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 2);

	msleep(30);

	snd_pmic_ops_nc.pb_on = 0;

	nc_enable_audiodac(UNMUTE);
	mutex_unlock(&snd_pmic_ops_nc.lock);
	return 0;
}

static int nc_power_down_cp(unsigned int device)
{
	struct sc_reg_access sc_access[] = {
		{POWERCTRL1, 0x00, 0xBE},
		{POWERCTRL2, 0x00, 0x02},
	};
	int retval = 0;

	if (snd_pmic_ops_nc.card_status == SND_CARD_UN_INIT)
		retval = nc_init_card();
	if (retval)
		return retval;

	pr_debug("powering dn cp....\n");
	return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 1);
}

static int nc_set_pcm_voice_params(void)
{
	struct sc_reg_access sc_access[] = {
			{0x100, 0xD5, 0},
			{0x101, 0x08, 0},
			{0x104, 0x03, 0},
			{0x107, 0x10, 0},
			{0x10B, 0x0E, 0},
			{0x10E, 0x03, 0},
			{0x10F, 0x03, 0},
			{0x114, 0x13, 0},
			{0x115, 0x00, 0},
			{0x128, 0xFE, 0},
			{0x129, 0xFE, 0},
			{0x12A, 0xFE, 0},
			{0x12B, 0xDE, 0},
			{0x12C, 0xDE, 0},
	};
	int retval = 0;

	if (snd_pmic_ops_nc.card_status == SND_CARD_UN_INIT)
		retval = nc_init_card();
	if (retval)
		return retval;

	sst_sc_reg_access(sc_access, PMIC_WRITE, 14);
	pr_debug("Voice parameters set successfully!!\n");
	return 0;
}


static int nc_set_pcm_audio_params(int sfreq, int word_size, int num_channel)
{
	int config2 = 0;
	struct sc_reg_access sc_access;
	int retval = 0;

	if (snd_pmic_ops_nc.card_status == SND_CARD_UN_INIT)
		retval = nc_init_card();
	if (retval)
		return retval;

	switch (sfreq) {
	case 8000:
		config2 = 0x00;
		break;
	case 11025:
		config2 = 0x01;
		break;
	case 12000:
		config2 = 0x02;
		break;
	case 16000:
		config2 = 0x03;
		break;
	case 22050:
		config2 = 0x04;
		break;
	case 24000:
		config2 = 0x05;
		break;
	case 32000:
		config2 = 0x07;
		break;
	case 44100:
		config2 = 0x08;
		break;
	case 48000:
		config2 = 0x09;
		break;
	}

	snd_pmic_ops_nc.num_channel = num_channel;
	if (snd_pmic_ops_nc.num_channel == 1)	{

		sc_access.value = 0x07;
		sc_access.reg_addr = RMUTE;
		pr_debug("RIGHT_HP_MUTE value%d\n", sc_access.value);
		sc_access.mask = MASK2;
		sst_sc_reg_access(&sc_access, PMIC_READ_MODIFY, 1);
	} else {
		sc_access.value = 0x00;
		sc_access.reg_addr = RMUTE;
		pr_debug("RIGHT_HP_MUTE value %d\n", sc_access.value);
		sc_access.mask = MASK2;
		sst_sc_reg_access(&sc_access, PMIC_READ_MODIFY, 1);


	}

	pr_debug("word_size = %d\n", word_size);

	if (word_size == 24) {
		sc_access.reg_addr = AUDIOPORT2;
		sc_access.value = config2 | 0x10;
		sc_access.mask = 0x1F;
	} else {
		sc_access.value = config2;
		sc_access.mask = 0x1F;
		sc_access.reg_addr = AUDIOPORT2;
	}
	sst_sc_reg_access(&sc_access, PMIC_READ_MODIFY, 1);

	pr_debug("word_size = %d\n", word_size);
	sc_access.reg_addr = AUDIOPORT1;
	sc_access.mask = MASK5|MASK4|MASK1|MASK0;
	if (word_size == 16)
		sc_access.value = 0x98;
	else if (word_size == 24)
		sc_access.value = 0xAB;

	return sst_sc_reg_access(&sc_access, PMIC_READ_MODIFY, 1);



}

static int nc_set_selected_output_dev(u8 value)
{
	struct sc_reg_access sc_access_HP[] = {
		{LMUTE, 0x02, 0x06},
		{RMUTE, 0x02, 0x06},
		{DRVPOWERCTRL, 0x06, 0x06},
	};
	struct sc_reg_access sc_access_IS[] = {
		{LMUTE, 0x04, 0x06},
		{RMUTE, 0x04, 0x06},
		{DRVPOWERCTRL, 0x00, 0x06},
	};
	int retval = 0;

	snd_pmic_ops_nc.output_dev_id = value;
	if (snd_pmic_ops_nc.card_status == SND_CARD_UN_INIT)
		retval = nc_init_card();
	if (retval)
		return retval;
	pr_debug("nc set selected output:%d\n", value);
	mutex_lock(&snd_pmic_ops_nc.lock);
	switch (value) {
	case STEREO_HEADPHONE:
		if (snd_pmic_ops_nc.pb_on)
			sst_sc_reg_access(sc_access_HP+2, PMIC_WRITE, 1);
		retval = sst_sc_reg_access(sc_access_HP, PMIC_WRITE, 2);
		nc_set_amp_power(0);
		break;
	case MONO_EARPIECE:
	case INTERNAL_SPKR:
		retval = sst_sc_reg_access(sc_access_IS, PMIC_WRITE, 3);
		if (snd_pmic_ops_nc.pb_on)
			nc_set_amp_power(1);
		break;
	default:
		pr_err("rcvd illegal request: %d\n", value);
		mutex_unlock(&snd_pmic_ops_nc.lock);
		return -EINVAL;
	}
	mutex_unlock(&snd_pmic_ops_nc.lock);
	return retval;
}

static int nc_audio_init(void)
{
	struct sc_reg_access sc_acces, sc_access[] = {
			{0x100, 0x00, 0},
			{0x101, 0x00, 0},
			{0x104, 0x8B, 0},
			{0x107, 0x11, 0},
			{0x10B, 0x0E, 0},
			{0x114, 0x00, 0},
			{0x115, 0x00, 0},
			{0x128, 0x00, 0},
			{0x129, 0x00, 0},
			{0x12A, 0x00, 0},
			{0x12B, 0xee, 0},
			{0x12C, 0xf6, 0},
	};

	sst_sc_reg_access(sc_access, PMIC_WRITE, 12);
	pr_debug("Audio Init successfully!!\n");

	/*set output device */
	nc_set_selected_output_dev(snd_pmic_ops_nc.output_dev_id);

	if (snd_pmic_ops_nc.num_channel == 1) {
		sc_acces.value = 0x07;
		sc_acces.reg_addr = RMUTE;
		pr_debug("RIGHT_HP_MUTE value%d\n", sc_acces.value);
		sc_acces.mask = MASK2;
		sst_sc_reg_access(&sc_acces, PMIC_READ_MODIFY, 1);
	} else {
		sc_acces.value = 0x00;
		sc_acces.reg_addr = RMUTE;
		pr_debug("RIGHT_HP_MUTE value%d\n", sc_acces.value);
		sc_acces.mask = MASK2;
		sst_sc_reg_access(&sc_acces, PMIC_READ_MODIFY, 1);
	}

	return 0;
}

static int nc_set_audio_port(int status)
{
	struct sc_reg_access sc_access[2] = {{0,},};
	int retval = 0;

	if (snd_pmic_ops_nc.card_status == SND_CARD_UN_INIT)
		retval = nc_init_card();
	if (retval)
		return retval;

	if (status == DEACTIVATE) {
		/* Deactivate audio port-tristate and power */
		sc_access[0].value = 0x00;
		sc_access[0].mask = MASK4|MASK5;
		sc_access[0].reg_addr = AUDIOPORT1;
		return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 1);
	} else if (status == ACTIVATE) {
		/* activate audio port */
		nc_audio_init();
		sc_access[0].value = 0x10;
		sc_access[0].mask =  MASK4|MASK5 ;
		sc_access[0].reg_addr = AUDIOPORT1;
		return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 1);
	} else
		return -EINVAL;

}

static int nc_set_voice_port(int status)
{
	struct sc_reg_access sc_access[2] = {{0,},};
	int retval = 0;

	if (snd_pmic_ops_nc.card_status == SND_CARD_UN_INIT)
		retval = nc_init_card();
	if (retval)
		return retval;

	if (status == DEACTIVATE) {
		/* Activate Voice port */
		sc_access[0].value = 0x00;
		sc_access[0].mask = MASK4;
		sc_access[0].reg_addr = VOICEPORT1;
		return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 1);
	} else if (status == ACTIVATE) {
		/* Deactivate voice port */
		nc_set_pcm_voice_params();
		sc_access[0].value = 0x10;
		sc_access[0].mask = MASK4;
		sc_access[0].reg_addr = VOICEPORT1;
		return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 1);
	} else
		return -EINVAL;
}

static int nc_set_mute(int dev_id, u8 value)
{
	struct sc_reg_access sc_access[3];
	u8 mute_val, cap_mute;
	int retval = 0;

	if (snd_pmic_ops_nc.card_status == SND_CARD_UN_INIT)
		retval = nc_init_card();
	if (retval)
		return retval;

	pr_debug("set device id::%d, value %d\n", dev_id, value);

	switch (dev_id) {
	case PMIC_SND_MUTE_ALL:
		pr_debug("PMIC_SND_MUTE_ALL value %d\n", value);
		snd_pmic_ops_nc.mute_status = value;
		snd_pmic_ops_nc.master_mute = value;
		if (value == UNMUTE) {
			/* unmute the system, set the 7th bit to zero */
			mute_val = cap_mute = 0x00;
		} else {
			/* MUTE:Set the seventh bit */
			mute_val = 0x80;
			cap_mute = 0x40;
		}
		sc_access[0].reg_addr = AUDIOLVOL;
		sc_access[1].reg_addr = AUDIORVOL;
		sc_access[0].mask = sc_access[1].mask = MASK7;
		sc_access[0].value = sc_access[1].value = mute_val;
		if (snd_pmic_ops_nc.num_channel == 1)
				sc_access[1].value = 0x80;
		if (!sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 2)) {
			sc_access[0].reg_addr = 0x109;
			sc_access[1].reg_addr = 0x10a;
			sc_access[2].reg_addr = 0x105;
			sc_access[0].mask = sc_access[1].mask =
				sc_access[2].mask = MASK6;
			sc_access[0].value = sc_access[1].value =
				sc_access[2].value = cap_mute;

			if ((snd_pmic_ops_nc.input_dev_id == AMIC) ||
				(snd_pmic_ops_nc.input_dev_id == DMIC))
					sc_access[1].value = 0x40;
			if (snd_pmic_ops_nc.input_dev_id == HS_MIC)
					sc_access[0].value = 0x40;
			retval = sst_sc_reg_access(sc_access,
					PMIC_READ_MODIFY, 3);
		}
		break;
	case PMIC_SND_HP_MIC_MUTE:
		pr_debug("PMIC_SND_HPMIC_MUTE value %d\n", value);
		if (value == UNMUTE) {
			/* unmute the system, set the 6th bit to one */
			sc_access[0].value = 0x00;
		} else {
			/* mute the system, reset the 6th bit to zero */
			sc_access[0].value = 0x40;
		}
		sc_access[0].reg_addr = LIRSEL;
		sc_access[0].mask = MASK6;
		retval = sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 1);
		break;
	case PMIC_SND_AMIC_MUTE:
		pr_debug("PMIC_SND_AMIC_MUTE value %d\n", value);
		if (value == UNMUTE) {
			/* unmute the system, set the 6th bit to one */
			sc_access[0].value = 0x00;
		} else {
			/* mute the system, reset the 6th bit to zero */
			sc_access[0].value = 0x40;
		}
		sc_access[0].reg_addr = LILSEL;
		sc_access[0].mask = MASK6;
		retval = sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 1);
		break;

	case PMIC_SND_DMIC_MUTE:
		pr_debug("INPUT_MUTE_DMIC value%d\n", value);
		if (value == UNMUTE) {
			/* unmute the system, set the 6th bit to one */
			sc_access[1].value = 0x00;
			sc_access[0].value = 0x00;
		} else {
			/* mute the system, reset the 6th bit to zero */
			sc_access[1].value = 0x40;
			sc_access[0].value = 0x40;
		}
		sc_access[0].reg_addr = DMICCTRL1;
		sc_access[0].mask = MASK6;
		sc_access[1].reg_addr = LILSEL;
		sc_access[1].mask = MASK6;
		retval = sst_sc_reg_access(sc_access,
					PMIC_READ_MODIFY, 2);
		break;

	case PMIC_SND_LEFT_HP_MUTE:
	case PMIC_SND_RIGHT_HP_MUTE:
		snd_pmic_ops_nc.mute_status = value;
		if (value == UNMUTE)
			sc_access[0].value = 0x0;
		else
			sc_access[0].value = 0x04;

		if (dev_id == PMIC_SND_LEFT_HP_MUTE) {
			sc_access[0].reg_addr = LMUTE;
			pr_debug("LEFT_HP_MUTE value %d\n",
					sc_access[0].value);
		} else {
			if (snd_pmic_ops_nc.num_channel == 1)
				sc_access[0].value = 0x04;
			sc_access[0].reg_addr = RMUTE;
			pr_debug("RIGHT_HP_MUTE value %d\n",
					sc_access[0].value);
		}
		sc_access[0].mask = MASK2;
		retval = sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 1);
		break;
	case PMIC_SND_LEFT_SPEAKER_MUTE:
	case PMIC_SND_RIGHT_SPEAKER_MUTE:
		if (value == UNMUTE)
			sc_access[0].value = 0x00;
		else
			sc_access[0].value = 0x03;
		sc_access[0].reg_addr = LMUTE;
		pr_debug("SPEAKER_MUTE %d\n", sc_access[0].value);
		sc_access[0].mask = MASK1;
		retval = sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, 1);
		break;
	default:
		return -EINVAL;
	}
	return retval ;

}

static int nc_set_vol(int dev_id, int value)
{
	struct sc_reg_access sc_access[3];
	int retval = 0, entries = 0;

	if (snd_pmic_ops_nc.card_status == SND_CARD_UN_INIT)
		retval = nc_init_card();
	if (retval)
		return retval;

	pr_debug("set volume:%d\n", dev_id);
	switch (dev_id) {
	case PMIC_SND_CAPTURE_VOL:
		pr_debug("PMIC_SND_CAPTURE_VOL:value::%d\n", value);
		sc_access[0].value = sc_access[1].value =
					sc_access[2].value = -value;
		sc_access[0].mask = sc_access[1].mask = sc_access[2].mask =
					(MASK0|MASK1|MASK2|MASK3|MASK4|MASK5);
		sc_access[0].reg_addr = 0x10a;
		sc_access[1].reg_addr = 0x109;
		sc_access[2].reg_addr = 0x105;
		entries = 3;
		break;

	case PMIC_SND_LEFT_PB_VOL:
		pr_debug("PMIC_SND_LEFT_HP_VOL %d\n", value);
		sc_access[0].value = -value;
		sc_access[0].reg_addr  = HPLVOL;
		sc_access[0].mask = (MASK0|MASK1|MASK2|MASK3|MASK4);
		entries = 1;
		break;

	case PMIC_SND_RIGHT_PB_VOL:
		pr_debug("PMIC_SND_RIGHT_HP_VOL value %d\n", value);
		if (snd_pmic_ops_nc.num_channel == 1) {
			sc_access[0].value = 0x04;
			sc_access[0].reg_addr = RMUTE;
			sc_access[0].mask = MASK2;
		} else {
			sc_access[0].value = -value;
			sc_access[0].reg_addr  = HPRVOL;
			sc_access[0].mask = (MASK0|MASK1|MASK2|MASK3|MASK4);
		}
		entries = 1;
		break;

	case PMIC_SND_LEFT_MASTER_VOL:
		pr_debug("PMIC_SND_LEFT_MASTER_VOL value %d\n", value);
		sc_access[0].value = -value;
		sc_access[0].reg_addr = AUDIOLVOL;
		sc_access[0].mask =
			(MASK0|MASK1|MASK2|MASK3|MASK4|MASK5|MASK6);
		entries = 1;
		break;

	case PMIC_SND_RIGHT_MASTER_VOL:
		pr_debug("PMIC_SND_RIGHT_MASTER_VOL value %d\n", value);
		sc_access[0].value = -value;
		sc_access[0].reg_addr = AUDIORVOL;
		sc_access[0].mask =
				(MASK0|MASK1|MASK2|MASK3|MASK4|MASK5|MASK6);
		entries = 1;
		break;

	default:
		return -EINVAL;

	}
	return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, entries);
}

static int nc_set_selected_input_dev(u8 value)
{
	struct sc_reg_access sc_access[6];
	u8 num_val;
	int retval = 0;

	if (snd_pmic_ops_nc.card_status == SND_CARD_UN_INIT)
		retval = nc_init_card();
	if (retval)
		return retval;
	snd_pmic_ops_nc.input_dev_id = value;

	pr_debug("nc set selected input:%d\n", value);

	switch (value) {
	case AMIC:
		pr_debug("Selecting AMIC\n");
		sc_access[0].reg_addr = 0x107;
		sc_access[0].value = 0x40;
		sc_access[0].mask =  MASK6|MASK3|MASK1|MASK0;
		sc_access[1].reg_addr = 0x10a;
		sc_access[1].value = 0x40;
		sc_access[1].mask = MASK6;
		sc_access[2].reg_addr = 0x109;
		sc_access[2].value = 0x00;
		sc_access[2].mask = MASK6;
		sc_access[3].reg_addr = 0x105;
		sc_access[3].value = 0x40;
		sc_access[3].mask = MASK6;
		num_val = 4;
		break;

	case HS_MIC:
		pr_debug("Selecting HS_MIC\n");
		sc_access[0].reg_addr = MICCTRL;
		sc_access[0].mask =  MASK6|MASK3|MASK1|MASK0;
		sc_access[0].value = 0x00;
		sc_access[1].reg_addr = 0x109;
		sc_access[1].mask = MASK6;
		sc_access[1].value = 0x40;
		sc_access[2].reg_addr = 0x10a;
		sc_access[2].mask = MASK6;
		sc_access[2].value = 0x00;
		sc_access[3].reg_addr = 0x105;
		sc_access[3].value = 0x40;
		sc_access[3].mask = MASK6;
		sc_access[4].reg_addr = ADCSAMPLERATE;
		sc_access[4].mask = MASK7|MASK6|MASK5|MASK4|MASK3;
		sc_access[4].value = 0xc8;
		num_val = 5;
		break;

	case DMIC:
		pr_debug("DMIC\n");
		sc_access[0].reg_addr = MICCTRL;
		sc_access[0].mask = MASK6|MASK3|MASK1|MASK0;
		sc_access[0].value = 0x0B;
		sc_access[1].reg_addr = 0x105;
		sc_access[1].value = 0x80;
		sc_access[1].mask = MASK7|MASK6;
		sc_access[2].reg_addr = 0x10a;
		sc_access[2].value = 0x40;
		sc_access[2].mask = MASK6;
		sc_access[3].reg_addr = LILSEL;
		sc_access[3].mask = MASK6;
		sc_access[3].value = 0x00;
		sc_access[4].reg_addr = ADCSAMPLERATE;
		sc_access[4].mask =  MASK7|MASK6|MASK5|MASK4|MASK3;
		sc_access[4].value = 0x33;
		num_val = 5;
		break;
	default:
		return -EINVAL;
	}
	return sst_sc_reg_access(sc_access, PMIC_READ_MODIFY, num_val);
}

static int nc_get_mute(int dev_id, u8 *value)
{
	int retval = 0, mask = 0;
	struct sc_reg_access sc_access = {0,};

	if (snd_pmic_ops_nc.card_status == SND_CARD_UN_INIT)
		retval = nc_init_card();
	if (retval)
		return retval;

	pr_debug("get mute::%d\n", dev_id);

	switch (dev_id) {
	case PMIC_SND_AMIC_MUTE:
		pr_debug("PMIC_SND_INPUT_MUTE_MIC1\n");
		sc_access.reg_addr = LILSEL;
		mask = MASK6;
		break;
	case PMIC_SND_HP_MIC_MUTE:
		pr_debug("PMIC_SND_INPUT_MUTE_MIC2\n");
		sc_access.reg_addr = LIRSEL;
		mask = MASK6;
		break;
	case PMIC_SND_LEFT_HP_MUTE:
	case PMIC_SND_RIGHT_HP_MUTE:
		mask = MASK2;
		pr_debug("PMIC_SN_LEFT/RIGHT_HP_MUTE\n");
		if (dev_id == PMIC_SND_RIGHT_HP_MUTE)
			sc_access.reg_addr = RMUTE;
		else
			sc_access.reg_addr = LMUTE;
		break;

	case PMIC_SND_LEFT_SPEAKER_MUTE:
		pr_debug("PMIC_MONO_EARPIECE_MUTE\n");
		sc_access.reg_addr = RMUTE;
		mask = MASK1;
		break;
	case PMIC_SND_DMIC_MUTE:
		pr_debug("PMIC_SND_INPUT_MUTE_DMIC\n");
		sc_access.reg_addr = 0x105;
		mask = MASK6;
		break;
	default:
		return -EINVAL;

	}
	retval = sst_sc_reg_access(&sc_access, PMIC_READ, 1);
	pr_debug("reg value = %d\n", sc_access.value);
	if (retval)
		return retval;
	*value = (sc_access.value) & mask;
	pr_debug("masked value = %d\n", *value);
	if (*value)
		*value = 0;
	else
		*value = 1;
	pr_debug("value returned = 0x%x\n", *value);
	return retval;
}

static int nc_get_vol(int dev_id, int *value)
{
	int retval = 0, mask = 0;
	struct sc_reg_access sc_access = {0,};

	if (snd_pmic_ops_nc.card_status == SND_CARD_UN_INIT)
		retval = nc_init_card();
	if (retval)
		return retval;

	switch (dev_id) {
	case PMIC_SND_CAPTURE_VOL:
		pr_debug("PMIC_SND_INPUT_CAPTURE_VOL\n");
		sc_access.reg_addr =  LILSEL;
		mask = (MASK0|MASK1|MASK2|MASK3|MASK4|MASK5);
		break;

	case PMIC_SND_LEFT_MASTER_VOL:
		pr_debug("GET_VOLUME_PMIC_LEFT_MASTER_VOL\n");
		sc_access.reg_addr = AUDIOLVOL;
		mask = (MASK0|MASK1|MASK2|MASK3|MASK4|MASK5|MASK6);
		break;

	case PMIC_SND_RIGHT_MASTER_VOL:
		pr_debug("GET_VOLUME_PMIC_RIGHT_MASTER_VOL\n");
		sc_access.reg_addr = AUDIORVOL;
		mask = (MASK0|MASK1|MASK2|MASK3|MASK4|MASK5|MASK6);
		break;

	case PMIC_SND_RIGHT_PB_VOL:
		pr_debug("GET_VOLUME_PMIC_RIGHT_HP_VOL\n");
		sc_access.reg_addr = HPRVOL;
		mask = (MASK0|MASK1|MASK2|MASK3|MASK4);
		break;

	case PMIC_SND_LEFT_PB_VOL:
		pr_debug("GET_VOLUME_PMIC_LEFT_HP_VOL\n");
		sc_access.reg_addr = HPLVOL;
		mask = (MASK0|MASK1|MASK2|MASK3|MASK4);
		break;

	default:
		return -EINVAL;

	}
	retval = sst_sc_reg_access(&sc_access, PMIC_READ, 1);
	pr_debug("value read = 0x%x\n", sc_access.value);
	*value = -((sc_access.value) & mask);
	pr_debug("get vol value returned = %d\n", *value);
	return retval;
}

static void hp_automute(enum snd_jack_types type, int present)
{
	u8 in = DMIC;
	u8 out = INTERNAL_SPKR;
	if (present) {
		if (type == SND_JACK_HEADSET)
			in = HS_MIC;
		out = STEREO_HEADPHONE;
	}
	nc_set_selected_input_dev(in);
	nc_set_selected_output_dev(out);
}

static void nc_pmic_irq_cb(void *cb_data, u8 intsts)
{
	u8 value = 0;
	struct mad_jack *mjack = NULL;
	unsigned int present = 0, jack_event_flag = 0, buttonpressflag = 0;
	struct snd_intelmad *intelmaddata = cb_data;
	struct sc_reg_access sc_access_read = {0,};

	sc_access_read.reg_addr = 0x132;
	sst_sc_reg_access(&sc_access_read, PMIC_READ, 1);
	value = (sc_access_read.value);
	pr_debug("value returned = 0x%x\n", value);

	mjack = &intelmaddata->jack[0];
	if (intsts & 0x1) {
		pr_debug("SST DBG:MAD headset detected\n");
		/* send headset detect/undetect */
		present = (value == 0x1) ? 1 : 0;
		jack_event_flag = 1;
		mjack->jack.type = SND_JACK_HEADSET;
		hp_automute(SND_JACK_HEADSET, present);
	}

	if (intsts & 0x2) {
		pr_debug(":MAD headphone detected\n");
		/* send headphone detect/undetect */
		present = (value == 0x2) ? 1 : 0;
		jack_event_flag = 1;
		mjack->jack.type = SND_JACK_HEADPHONE;
		hp_automute(SND_JACK_HEADPHONE, present);
	}

	if (intsts & 0x4) {
		pr_debug("MAD short push detected\n");
		/* send short push */
		present = 1;
		jack_event_flag = 1;
		buttonpressflag = 1;
		mjack->jack.type = MID_JACK_HS_SHORT_PRESS;
	}

	if (intsts & 0x8) {
		pr_debug(":MAD long push detected\n");
		/* send long push */
		present = 1;
		jack_event_flag = 1;
		buttonpressflag = 1;
		mjack->jack.type = MID_JACK_HS_LONG_PRESS;
	}

	if (jack_event_flag)
		sst_mad_send_jack_report(&mjack->jack,
					buttonpressflag, present);
}
static int nc_jack_enable(void)
{
	return 0;
}

struct snd_pmic_ops snd_pmic_ops_nc = {
	.input_dev_id   =       DMIC,
	.output_dev_id  =       INTERNAL_SPKR,
	.set_input_dev	=	nc_set_selected_input_dev,
	.set_output_dev =	nc_set_selected_output_dev,
	.set_mute	=	nc_set_mute,
	.get_mute	=	nc_get_mute,
	.set_vol	=	nc_set_vol,
	.get_vol	=	nc_get_vol,
	.init_card	=	nc_init_card,
	.set_pcm_audio_params	= nc_set_pcm_audio_params,
	.set_pcm_voice_params	= nc_set_pcm_voice_params,
	.set_voice_port = nc_set_voice_port,
	.set_audio_port = nc_set_audio_port,
	.power_up_pmic_pb =	nc_power_up_pb,
	.power_up_pmic_cp =	nc_power_up_cp,
	.power_down_pmic_pb =	nc_power_down_pb,
	.power_down_pmic_cp =	nc_power_down_cp,
	.power_down_pmic	=	nc_power_down,
	.pmic_irq_cb	=	nc_pmic_irq_cb,
	.pmic_jack_enable =	nc_jack_enable,
};
