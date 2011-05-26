/*
 *  intelmid_vm_control.c - Intel Sound card driver for MID
 *
 *  Copyright (C) 2010 Intel Corp
 *  Authors:	Vinod Koul <vinod.koul@intel.com>
 *
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
 * This file contains the control operations of msic vendors
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/pci.h>
#include <linux/file.h>
#include <linux/delay.h>
#include <sound/control.h>
#include "intel_sst.h"
#include <linux/input.h>
#include "intelmid_snd_control.h"
#include "intelmid.h"

#define AUDIOMUX12  0x24c
#define AUDIOMUX34  0x24d

static int msic_init_card(void)
{
	struct sc_reg_access sc_access[] = {
		/* dmic configuration */
		{0x241, 0x85, 0},
		{0x242, 0x02, 0},
		/* audio paths config */
		{0x24C, 0x10, 0},
		{0x24D, 0x32, 0},
		/* PCM2 interface slots */
		/* preconfigured slots for 0-5 both tx, rx */
		{0x272, 0x10, 0},
		{0x273, 0x32, 0},
		{0x274, 0xFF, 0},
		{0x275, 0x10, 0},
		{0x276, 0x32, 0},
		{0x277, 0x54, 0},
		/*Sinc5 decimator*/
		{0x24E, 0x28, 0},
		/*TI vibra w/a settings*/
		{0x384, 0x80, 0},
		{0x385, 0x80, 0},
		{0x267, 0x00, 0},
		{0x261, 0x00, 0},
		/* pcm port setting */
		{0x278, 0x00, 0},
		{0x27B, 0x01, 0},
		{0x27C, 0x0a, 0},
		/* Set vol HSLRVOLCTRL, IHFVOL */
		{0x259, 0x08, 0},
		{0x25A, 0x08, 0},
		{0x25B, 0x08, 0},
		{0x25C, 0x08, 0},
		/* HSEPRXCTRL  Enable the headset left and right FIR filters  */
		{0x250, 0x30, 0},
		/* HSMIXER */
		{0x256, 0x11, 0},
		/* amic configuration */
		{0x249, 0x01, 0x0},
		{0x24A, 0x01, 0x0},
		/* unmask ocaudio/accdet interrupts */
		{0x1d, 0x00, 0x00},
		{0x1e, 0x00, 0x00},
	};
	snd_msic_ops.card_status = SND_CARD_INIT_DONE;
	sst_sc_reg_access(sc_access, PMIC_WRITE, 28);
	snd_msic_ops.pb_on = 0;
	snd_msic_ops.pbhs_on = 0;
	snd_msic_ops.cap_on = 0;
	snd_msic_ops.input_dev_id = DMIC; /*def dev*/
	snd_msic_ops.output_dev_id = STEREO_HEADPHONE;
	snd_msic_ops.jack_interrupt_status = false;
	pr_debug("msic init complete!!\n");
	return 0;
}
static int msic_line_out_restore(u8 value)
{
	struct sc_reg_access hs_drv_en[] = {
		{0x25d, 0x03, 0x03},
	};
	struct sc_reg_access ep_drv_en[] = {
		{0x25d, 0x40, 0x40},
	};
	struct sc_reg_access ihf_drv_en[] = {
		{0x25d, 0x0c, 0x0c},
	};
	struct sc_reg_access vib1_drv_en[] = {
		{0x25d, 0x10, 0x10},
	};
	struct sc_reg_access vib2_drv_en[] = {
		{0x25d, 0x20, 0x20},
	};
	struct sc_reg_access pmode_enable[] = {
		{0x381, 0x10, 0x10},
	};
	int retval = 0;

	pr_debug("msic_lineout_restore_lineout_dev:%d\n", value);

	switch (value) {
	case HEADSET:
		pr_debug("Selecting Lineout-HEADSET-restore\n");
		if (snd_msic_ops.output_dev_id == STEREO_HEADPHONE)
			retval = sst_sc_reg_access(hs_drv_en,
							PMIC_READ_MODIFY, 1);
		else
			retval = sst_sc_reg_access(ep_drv_en,
							PMIC_READ_MODIFY, 1);
		break;
	case IHF:
		pr_debug("Selecting Lineout-IHF-restore\n");
		retval = sst_sc_reg_access(ihf_drv_en, PMIC_READ_MODIFY, 1);
		if (retval)
			return retval;
		retval = sst_sc_reg_access(pmode_enable, PMIC_READ_MODIFY, 1);
		break;
	case VIBRA1:
		pr_debug("Selecting Lineout-Vibra1-restore\n");
		retval = sst_sc_reg_access(vib1_drv_en, PMIC_READ_MODIFY, 1);
		break;
	case VIBRA2:
		pr_debug("Selecting Lineout-VIBRA2-restore\n");
		retval = sst_sc_reg_access(vib2_drv_en, PMIC_READ_MODIFY, 1);
		break;
	case NONE:
		pr_debug("Selecting Lineout-NONE-restore\n");
		break;
	default:
		return -EINVAL;
	}
	return retval;
}
static int msic_get_lineout_prvstate(void)
{
	struct sc_reg_access hs_ihf_drv[2] = {
		{0x257, 0x0, 0x0},
		{0x25d, 0x0, 0x0},
	};
	struct sc_reg_access vib1drv[2] = {
		{0x264, 0x0, 0x0},
		{0x25D, 0x0, 0x0},
	};
	struct sc_reg_access vib2drv[2] = {
		{0x26A, 0x0, 0x0},
		{0x25D, 0x0, 0x0},
	};
	int retval = 0, drv_en, dac_en, dev_id, mask;
	for (dev_id = 0; dev_id < snd_msic_ops.line_out_names_cnt; dev_id++) {
		switch (dev_id) {
		case HEADSET:
			pr_debug("msic_get_lineout_prvs_state: HEADSET\n");
			sst_sc_reg_access(hs_ihf_drv, PMIC_READ, 2);

			mask = (MASK0|MASK1);
			dac_en = (hs_ihf_drv[0].value) & mask;

			mask = ((MASK0|MASK1)|MASK6);
			drv_en = (hs_ihf_drv[1].value) & mask;

			if (dac_en && (!drv_en)) {
				snd_msic_ops.prev_lineout_dev_id = HEADSET;
				return retval;
			}
			break;
		case IHF:
			pr_debug("msic_get_lineout_prvstate: IHF\n");
			sst_sc_reg_access(hs_ihf_drv, PMIC_READ, 2);

			mask = (MASK2 | MASK3);
			dac_en = (hs_ihf_drv[0].value) & mask;

			mask = (MASK2 | MASK3);
			drv_en = (hs_ihf_drv[1].value) & mask;

			if (dac_en && (!drv_en)) {
				snd_msic_ops.prev_lineout_dev_id = IHF;
				return retval;
			}
			break;
		case VIBRA1:
			pr_debug("msic_get_lineout_prvstate: vibra1\n");
			sst_sc_reg_access(vib1drv, PMIC_READ, 2);

			mask = MASK1;
			dac_en = (vib1drv[0].value) & mask;

			mask = MASK4;
			drv_en = (vib1drv[1].value) & mask;

			if (dac_en && (!drv_en)) {
				snd_msic_ops.prev_lineout_dev_id = VIBRA1;
				return retval;
			}
			break;
		case VIBRA2:
			pr_debug("msic_get_lineout_prvstate: vibra2\n");
			sst_sc_reg_access(vib2drv, PMIC_READ, 2);

			mask = MASK1;
			dac_en = (vib2drv[0].value) & mask;

			mask = MASK5;
			drv_en = ((vib2drv[1].value) & mask);

			if (dac_en && (!drv_en)) {
				snd_msic_ops.prev_lineout_dev_id = VIBRA2;
				return retval;
			}
			break;
		case NONE:
			pr_debug("msic_get_lineout_prvstate: NONE\n");
			snd_msic_ops.prev_lineout_dev_id = NONE;
			return retval;
		default:
			pr_debug("Invalid device id\n");
			snd_msic_ops.prev_lineout_dev_id = NONE;
			return -EINVAL;
		}
	}
	return retval;
}
static int msic_set_selected_lineout_dev(u8 value)
{
	struct sc_reg_access lout_hs[] = {
		{0x25e, 0x33, 0xFF},
		{0x25d, 0x0, 0x43},
	};
	struct sc_reg_access lout_ihf[] = {
		{0x25e, 0x55, 0xff},
		{0x25d, 0x0, 0x0c},
	};
	struct sc_reg_access lout_vibra1[] = {

		{0x25e, 0x61, 0xff},
		{0x25d, 0x0, 0x10},
	};
	struct sc_reg_access lout_vibra2[] = {

		{0x25e, 0x16, 0xff},
		{0x25d, 0x0, 0x20},
	};
	struct sc_reg_access lout_def[] = {
		{0x25e, 0x66, 0x0},
	};
	struct sc_reg_access pmode_disable[] = {
		{0x381, 0x00, 0x10},
	};
	struct sc_reg_access pmode_enable[] = {
		{0x381, 0x10, 0x10},
	};
	int retval = 0;

	pr_debug("msic_set_selected_lineout_dev:%d\n", value);
	msic_get_lineout_prvstate();
	msic_line_out_restore(snd_msic_ops.prev_lineout_dev_id);
	snd_msic_ops.lineout_dev_id = value;

	switch (value) {
	case HEADSET:
		pr_debug("Selecting Lineout-HEADSET\n");
		if (snd_msic_ops.pb_on)
			retval = sst_sc_reg_access(lout_hs,
					PMIC_READ_MODIFY, 2);
			if (retval)
				return retval;
			retval = sst_sc_reg_access(pmode_disable,
					PMIC_READ_MODIFY, 1);
		break;
	case IHF:
		pr_debug("Selecting Lineout-IHF\n");
		if (snd_msic_ops.pb_on)
			retval = sst_sc_reg_access(lout_ihf,
							PMIC_READ_MODIFY, 2);
			if (retval)
				return retval;
			retval = sst_sc_reg_access(pmode_enable,
					PMIC_READ_MODIFY, 1);
		break;
	case VIBRA1:
		pr_debug("Selecting Lineout-Vibra1\n");
		if (snd_msic_ops.pb_on)
			retval = sst_sc_reg_access(lout_vibra1,
							PMIC_READ_MODIFY, 2);
			if (retval)
				return retval;
			retval = sst_sc_reg_access(pmode_disable,
					PMIC_READ_MODIFY, 1);
		break;
	case VIBRA2:
		pr_debug("Selecting Lineout-VIBRA2\n");
		if (snd_msic_ops.pb_on)
			retval = sst_sc_reg_access(lout_vibra2,
							PMIC_READ_MODIFY, 2);
			if (retval)
				return retval;
			retval = sst_sc_reg_access(pmode_disable,
					PMIC_READ_MODIFY, 1);
		break;
	case NONE:
		pr_debug("Selecting Lineout-NONE\n");
			retval = sst_sc_reg_access(lout_def,
							PMIC_WRITE, 1);
			if (retval)
				return retval;
			retval = sst_sc_reg_access(pmode_disable,
					PMIC_READ_MODIFY, 1);
		break;
	default:
		return -EINVAL;
	}
	return retval;
}


static int msic_power_up_pb(unsigned int device)
{
	struct sc_reg_access vaud[] = {
		/* turn on the audio power supplies */
		{0x0DB, 0x07, 0},
	};
	struct sc_reg_access pll[] = {
		/* turn on PLL */
		{0x240, 0x20, 0},
	};
	struct sc_reg_access vhs[] = {
		/*  VHSP */
		{0x0DC, 0x3D, 0},
		/*  VHSN */
		{0x0DD, 0x3F, 0},
	};
	struct sc_reg_access hsdac[] = {
		{0x382, 0x40, 0x40},
		/*  disable driver */
		{0x25D, 0x0, 0x43},
		/* DAC CONFIG ; both HP, LP on */
		{0x257, 0x03, 0x03},
	};
	struct sc_reg_access hs_filter[] = {
		/* HSEPRXCTRL  Enable the headset left and right FIR filters  */
		{0x250, 0x30, 0},
		/* HSMIXER */
		{0x256, 0x11, 0},
	};
	struct sc_reg_access hs_enable[] = {
		/* enable driver */
		{0x25D, 0x3, 0x3},
		{0x26C, 0x0, 0x2},
		/* unmute the headset */
		{ 0x259, 0x80, 0x80},
		{ 0x25A, 0x80, 0x80},
	};
	struct sc_reg_access vihf[] = {
		/*  VIHF ON */
		{0x0C9, 0x27, 0x00},
	};
	struct sc_reg_access ihf_filter[] = {
		/*  disable driver */
		{0x25D, 0x00, 0x0C},
		/*Filer DAC enable*/
		{0x251, 0x03, 0x03},
		{0x257, 0x0C, 0x0C},
	};
	struct sc_reg_access ihf_en[] = {
		/*enable drv*/
		{0x25D, 0x0C, 0x0c},
	};
	struct sc_reg_access ihf_unmute[] = {
		/*unmute headset*/
		{0x25B, 0x80, 0x80},
		{0x25C, 0x80, 0x80},
	};
	struct sc_reg_access epdac[] = {
		/*  disable driver */
		{0x25D, 0x0, 0x43},
		/* DAC CONFIG ; both HP, LP on */
		{0x257, 0x03, 0x03},
	};
	struct sc_reg_access ep_enable[] = {
		/* enable driver */
		{0x25D, 0x40, 0x40},
		/* unmute the headset */
		{ 0x259, 0x80, 0x80},
		{ 0x25A, 0x80, 0x80},
	};
	struct sc_reg_access vib1_en[] = {
		/* enable driver, ADC */
		{0x25D, 0x10, 0x10},
		{0x264, 0x02, 0x82},
	};
	struct sc_reg_access vib2_en[] = {
		/* enable driver, ADC */
		{0x25D, 0x20, 0x20},
		{0x26A, 0x02, 0x82},
	};
	struct sc_reg_access pcm2_en[] = {
		/* enable pcm 2 */
		{0x27C, 0x1, 0x1},
	};
	int retval = 0;

	if (snd_msic_ops.card_status == SND_CARD_UN_INIT) {
		retval = msic_init_card();
		if (retval)
			return retval;
	}

	pr_debug("powering up pb.... Device %d\n", device);
	sst_sc_reg_access(vaud, PMIC_WRITE, 1);
	msleep(1);
	sst_sc_reg_access(pll, PMIC_WRITE, 1);
	msleep(1);
	switch (device) {
	case SND_SST_DEVICE_HEADSET:
		snd_msic_ops.pb_on = 1;
		snd_msic_ops.pbhs_on = 1;
		if (snd_msic_ops.output_dev_id == STEREO_HEADPHONE) {
			sst_sc_reg_access(vhs, PMIC_WRITE, 2);
			sst_sc_reg_access(hsdac, PMIC_READ_MODIFY, 3);
			sst_sc_reg_access(hs_filter, PMIC_WRITE, 2);
			sst_sc_reg_access(hs_enable, PMIC_READ_MODIFY, 4);
		} else {
			sst_sc_reg_access(epdac, PMIC_READ_MODIFY, 2);
			sst_sc_reg_access(hs_filter, PMIC_WRITE, 2);
			sst_sc_reg_access(ep_enable, PMIC_READ_MODIFY, 3);
		}
		if (snd_msic_ops.lineout_dev_id == HEADSET)
			msic_set_selected_lineout_dev(HEADSET);
		break;
	case SND_SST_DEVICE_IHF:
		snd_msic_ops.pb_on = 1;
		sst_sc_reg_access(vihf, PMIC_WRITE, 1);
		sst_sc_reg_access(ihf_filter, PMIC_READ_MODIFY, 3);
		sst_sc_reg_access(ihf_en, PMIC_READ_MODIFY, 1);
		sst_sc_reg_access(ihf_unmute, PMIC_READ_MODIFY, 2);
		if (snd_msic_ops.lineout_dev_id == IHF)
			msic_set_selected_lineout_dev(IHF);
		break;

	case SND_SST_DEVICE_VIBRA:
		snd_msic_ops.pb_on = 1;
		sst_sc_reg_access(vib1_en, PMIC_READ_MODIFY, 2);
		if (snd_msic_ops.lineout_dev_id == VIBRA1)
			msic_set_selected_lineout_dev(VIBRA1);
		break;

	case SND_SST_DEVICE_HAPTIC:
		snd_msic_ops.pb_on = 1;
		sst_sc_reg_access(vib2_en, PMIC_READ_MODIFY, 2);
		if (snd_msic_ops.lineout_dev_id == VIBRA2)
			msic_set_selected_lineout_dev(VIBRA2);
		break;

	default:
		pr_warn("Wrong Device %d, selected %d\n",
			       device, snd_msic_ops.output_dev_id);
	}
	return sst_sc_reg_access(pcm2_en, PMIC_READ_MODIFY, 1);
}

static int msic_power_up_cp(unsigned int device)
{
	struct sc_reg_access vaud[] = {
		/* turn on the audio power supplies */
		{0x0DB, 0x07, 0},
	};
	struct sc_reg_access pll[] = {
		/* turn on PLL */
		{0x240, 0x20, 0},
	};
	struct sc_reg_access dmic_bias[] = {
		/*  Turn on AMIC supply  */
		{0x247, 0xA0, 0xA0},
	};
	struct sc_reg_access dmic[] = {
		/* mic demux enable */
		{0x245, 0x3F, 0x3F},
		{0x246, 0x07, 0x07},

	};
	struct sc_reg_access amic_bias[] = {
		/*  Turn on AMIC supply  */
		{0x247, 0xFC, 0xFC},
	};
	struct sc_reg_access amic[] = {
		/*MIC EN*/
		{0x249, 0x01, 0x01},
		{0x24A, 0x01, 0x01},
		/*ADC EN*/
		{0x248, 0x05, 0x0F},

	};
	struct sc_reg_access pcm2[] = {
		/* enable pcm 2 */
		{0x27C, 0x1, 0x1},
	};
	struct sc_reg_access tx_on[] = {
		/*wait for mic to stabalize before turning on audio channels*/
		{0x24F, 0x3C, 0x0},
	};
	int retval = 0;

	if (snd_msic_ops.card_status == SND_CARD_UN_INIT) {
		retval = msic_init_card();
		if (retval)
			return retval;
	}

	pr_debug("powering up cp....%d\n", snd_msic_ops.input_dev_id);
	sst_sc_reg_access(vaud, PMIC_WRITE, 1);
	msleep(500);/*FIXME need optimzed value here*/
	sst_sc_reg_access(pll, PMIC_WRITE, 1);
	msleep(1);
	snd_msic_ops.cap_on = 1;
	if (snd_msic_ops.input_dev_id == AMIC) {
		sst_sc_reg_access(amic_bias, PMIC_READ_MODIFY, 1);
		msleep(1);
		sst_sc_reg_access(amic, PMIC_READ_MODIFY, 3);
	} else {
		sst_sc_reg_access(dmic_bias, PMIC_READ_MODIFY, 1);
		msleep(1);
		sst_sc_reg_access(dmic, PMIC_READ_MODIFY, 2);
	}
	msleep(1);
	sst_sc_reg_access(tx_on, PMIC_WRITE, 1);
	return sst_sc_reg_access(pcm2, PMIC_READ_MODIFY, 1);
}

static int msic_power_down(void)
{
	struct sc_reg_access power_dn[] = {
		/*  VHSP */
		{0x0DC, 0xC4, 0},
		/*  VHSN */
		{0x0DD, 0x04, 0},
		/*  VIHF */
		{0x0C9, 0x24, 0},
	};
	struct sc_reg_access pll[] = {
		/* turn off PLL*/
		{0x240, 0x00, 0x0},
	};
	struct sc_reg_access vaud[] = {
		/* turn off VAUD*/
		{0x0DB, 0x04, 0},
	};

	pr_debug("powering dn msic\n");
	snd_msic_ops.pbhs_on = 0;
	snd_msic_ops.pb_on = 0;
	snd_msic_ops.cap_on = 0;
	sst_sc_reg_access(power_dn, PMIC_WRITE, 3);
	msleep(1);
	sst_sc_reg_access(pll, PMIC_WRITE, 1);
	msleep(1);
	sst_sc_reg_access(vaud, PMIC_WRITE, 1);
	return 0;
}

static int msic_power_down_pb(unsigned int device)
{
	struct sc_reg_access drv_enable[] = {
		{0x25D, 0x00, 0x00},
	};
	struct sc_reg_access hs_mute[] = {
		{0x259, 0x80, 0x80},
		{0x25A, 0x80, 0x80},
		{0x26C, 0x02, 0x02},
	};
	struct sc_reg_access hs_off[] = {
		{0x257, 0x00, 0x03},
		{0x250, 0x00, 0x30},
		{0x382, 0x00, 0x40},
	};
	struct sc_reg_access ihf_mute[] = {
		{0x25B, 0x80, 0x80},
		{0x25C, 0x80, 0x80},
	};
	struct sc_reg_access ihf_off[] = {
		{0x257, 0x00, 0x0C},
		{0x251, 0x00, 0x03},
	};
	struct sc_reg_access vib1_off[] = {
		{0x264, 0x00, 0x82},
	};
	struct sc_reg_access vib2_off[] = {
		{0x26A, 0x00, 0x82},
	};
	struct sc_reg_access lout_off[] = {
		{0x25e, 0x66, 0x00},
	};
	struct sc_reg_access pmode_disable[] = {
		{0x381, 0x00, 0x10},
	};



	pr_debug("powering dn pb for device %d\n", device);
	switch (device) {
	case SND_SST_DEVICE_HEADSET:
		snd_msic_ops.pbhs_on = 0;
		sst_sc_reg_access(hs_mute, PMIC_READ_MODIFY, 3);
		drv_enable[0].mask = 0x43;
		sst_sc_reg_access(drv_enable, PMIC_READ_MODIFY, 1);
		sst_sc_reg_access(hs_off, PMIC_READ_MODIFY, 3);
		if (snd_msic_ops.lineout_dev_id == HEADSET)
			sst_sc_reg_access(lout_off, PMIC_WRITE, 1);
		break;

	case SND_SST_DEVICE_IHF:
		sst_sc_reg_access(ihf_mute, PMIC_READ_MODIFY, 2);
		drv_enable[0].mask = 0x0C;
		sst_sc_reg_access(drv_enable, PMIC_READ_MODIFY, 1);
		sst_sc_reg_access(ihf_off, PMIC_READ_MODIFY, 2);
		if (snd_msic_ops.lineout_dev_id == IHF) {
			sst_sc_reg_access(lout_off, PMIC_WRITE, 1);
			sst_sc_reg_access(pmode_disable, PMIC_READ_MODIFY, 1);
		}
		break;

	case SND_SST_DEVICE_VIBRA:
		sst_sc_reg_access(vib1_off, PMIC_READ_MODIFY, 1);
		drv_enable[0].mask = 0x10;
		sst_sc_reg_access(drv_enable, PMIC_READ_MODIFY, 1);
		if (snd_msic_ops.lineout_dev_id == VIBRA1)
			sst_sc_reg_access(lout_off, PMIC_WRITE, 1);
		break;

	case SND_SST_DEVICE_HAPTIC:
		sst_sc_reg_access(vib2_off, PMIC_READ_MODIFY, 1);
		drv_enable[0].mask = 0x20;
		sst_sc_reg_access(drv_enable, PMIC_READ_MODIFY, 1);
		if (snd_msic_ops.lineout_dev_id == VIBRA2)
			sst_sc_reg_access(lout_off, PMIC_WRITE, 1);
		break;
	}
	return 0;
}

static int msic_power_down_cp(unsigned int device)
{
	struct sc_reg_access dmic[] = {
		{0x247, 0x00, 0xA0},
		{0x245, 0x00, 0x38},
		{0x246, 0x00, 0x07},
	};
	struct sc_reg_access amic[] = {
		{0x248, 0x00, 0x05},
		{0x249, 0x00, 0x01},
		{0x24A, 0x00, 0x01},
		{0x247, 0x00, 0xA3},
	};
	struct sc_reg_access tx_off[] = {
		{0x24F, 0x00, 0x3C},
	};

	pr_debug("powering dn cp....\n");
	snd_msic_ops.cap_on = 0;
	sst_sc_reg_access(tx_off, PMIC_READ_MODIFY, 1);
	if (snd_msic_ops.input_dev_id == DMIC)
		sst_sc_reg_access(dmic, PMIC_READ_MODIFY, 3);
	else
		sst_sc_reg_access(amic, PMIC_READ_MODIFY, 4);
	return 0;
}

static int msic_set_selected_output_dev(u8 value)
{
	int retval = 0;

	pr_debug("msic set selected output:%d\n", value);
	snd_msic_ops.output_dev_id = value;
	if (snd_msic_ops.pbhs_on)
		msic_power_up_pb(SND_SST_DEVICE_HEADSET);
	return retval;
}

static int msic_set_selected_input_dev(u8 value)
{

	struct sc_reg_access sc_access_dmic[] = {
		{0x24C, 0x10, 0x0},
	};
	struct sc_reg_access sc_access_amic[] = {
		{0x24C, 0x76, 0x0},

	};
	int retval = 0;

	pr_debug("msic_set_selected_input_dev:%d\n", value);
	snd_msic_ops.input_dev_id = value;
	switch (value) {
	case AMIC:
		pr_debug("Selecting AMIC1\n");
		retval = sst_sc_reg_access(sc_access_amic, PMIC_WRITE, 1);
		break;
	case DMIC:
		pr_debug("Selecting DMIC1\n");
		retval = sst_sc_reg_access(sc_access_dmic, PMIC_WRITE, 1);
		break;
	default:
		return -EINVAL;

	}
	if (snd_msic_ops.cap_on)
		retval = msic_power_up_cp(SND_SST_DEVICE_CAPTURE);
	return retval;
}

static int msic_set_hw_dmic_route(u8 hw_ch_index)
{
	struct sc_reg_access sc_access_router;
	int    retval = -EINVAL;

	switch (hw_ch_index) {
	case HW_CH0:
		sc_access_router.reg_addr = AUDIOMUX12;
		sc_access_router.value    = snd_msic_ops.hw_dmic_map[0];
		sc_access_router.mask     = (MASK2 | MASK1 | MASK0);
		pr_debug("hw_ch0.  value = 0x%x\n",
				sc_access_router.value);
		retval = sst_sc_reg_access(&sc_access_router,
				PMIC_READ_MODIFY, 1);
		break;

	case HW_CH1:
		sc_access_router.reg_addr = AUDIOMUX12;
		sc_access_router.value    = (snd_msic_ops.hw_dmic_map[1]) << 4;
		sc_access_router.mask     = (MASK6 | MASK5 | MASK4);
		pr_debug("### hw_ch1.  value = 0x%x\n",
				sc_access_router.value);
		retval = sst_sc_reg_access(&sc_access_router,
				PMIC_READ_MODIFY, 1);
		break;

	case HW_CH2:
		sc_access_router.reg_addr = AUDIOMUX34;
		sc_access_router.value    = snd_msic_ops.hw_dmic_map[2];
		sc_access_router.mask     = (MASK2 | MASK1 | MASK0);
		pr_debug("hw_ch2.  value = 0x%x\n",
				sc_access_router.value);
		retval = sst_sc_reg_access(&sc_access_router,
				PMIC_READ_MODIFY, 1);
		break;

	case HW_CH3:
		sc_access_router.reg_addr = AUDIOMUX34;
		sc_access_router.value    = (snd_msic_ops.hw_dmic_map[3]) << 4;
		sc_access_router.mask     = (MASK6 | MASK5 | MASK4);
		pr_debug("hw_ch3.  value = 0x%x\n",
				sc_access_router.value);
		retval = sst_sc_reg_access(&sc_access_router,
				PMIC_READ_MODIFY, 1);
		break;
	}

	return retval;
}


static int msic_set_pcm_voice_params(void)
{
	return 0;
}

static int msic_set_pcm_audio_params(int sfreq, int word_size, int num_channel)
{
	return 0;
}

static int msic_set_audio_port(int status)
{
	return 0;
}

static int msic_set_voice_port(int status)
{
	return 0;
}

static int msic_set_mute(int dev_id, u8 value)
{
	return 0;
}

static int msic_set_vol(int dev_id, int value)
{
	return 0;
}

static int msic_get_mute(int dev_id, u8 *value)
{
	return 0;
}

static int msic_get_vol(int dev_id, int *value)
{
	return 0;
}

static int msic_set_headset_state(int state)
{
	struct sc_reg_access hs_enable[] = {
		{0x25D, 0x03, 0x03},
	};

	if (state)
		/*enable*/
		sst_sc_reg_access(hs_enable, PMIC_READ_MODIFY, 1);
	else {
		hs_enable[0].value = 0;
		sst_sc_reg_access(hs_enable, PMIC_READ_MODIFY, 1);
	}
	return 0;
}

static int msic_enable_mic_bias(void)
{
	struct sc_reg_access jack_interrupt_reg[] = {
		{0x0DB, 0x07, 0x00},

	};
	struct sc_reg_access jack_bias_reg[] = {
		{0x247, 0x0C, 0x0C},
	};

	sst_sc_reg_access(jack_interrupt_reg, PMIC_WRITE, 1);
	sst_sc_reg_access(jack_bias_reg, PMIC_READ_MODIFY, 1);
	return 0;
}

static int msic_disable_mic_bias(void)
{
	if (snd_msic_ops.jack_interrupt_status == true)
		return 0;
	if (!(snd_msic_ops.pb_on || snd_msic_ops.cap_on))
		msic_power_down();
	return 0;
}

static int msic_disable_jack_btn(void)
{
	struct sc_reg_access btn_disable[] = {
		{0x26C, 0x00, 0x01}
	};

	if (!(snd_msic_ops.pb_on || snd_msic_ops.cap_on))
		msic_power_down();
	snd_msic_ops.jack_interrupt_status = false;
	return sst_sc_reg_access(btn_disable, PMIC_READ_MODIFY, 1);
}

static int msic_enable_jack_btn(void)
{
	struct sc_reg_access btn_enable[] = {
			{0x26b, 0x77, 0x00},
			{0x26C, 0x01, 0x00},
	};
	return sst_sc_reg_access(btn_enable, PMIC_WRITE, 2);
}
static int msic_convert_adc_to_mvolt(unsigned int mic_bias)
{
	return (ADC_ONE_LSB_MULTIPLIER * mic_bias) / 1000;
}
int msic_get_headset_state(int mic_bias)
{
	struct sc_reg_access msic_hs_toggle[] = {
		{0x070, 0x00, 0x01},
	};
	if (mic_bias >= 0 && mic_bias < 400) {

		pr_debug("Detected Headphone!!!\n");
		sst_sc_reg_access(msic_hs_toggle, PMIC_READ_MODIFY, 1);

	} else if (mic_bias > 400 && mic_bias < 650) {

		pr_debug("Detected American headset\n");
		msic_hs_toggle[0].value = 0x01;
		sst_sc_reg_access(msic_hs_toggle, PMIC_READ_MODIFY, 1);

	} else if (mic_bias >= 650 && mic_bias < 2000) {

		pr_debug("Detected Headset!!!\n");
		sst_sc_reg_access(msic_hs_toggle, PMIC_READ_MODIFY, 1);
		/*power on jack and btn*/
		snd_msic_ops.jack_interrupt_status = true;
		msic_enable_jack_btn();
		msic_enable_mic_bias();
		return SND_JACK_HEADSET;

	} else
		pr_debug("Detected Open Cable!!!\n");

	return SND_JACK_HEADPHONE;
}

static int msic_get_mic_bias(void *arg)
{
	struct snd_intelmad *intelmad_drv = (struct snd_intelmad *)arg;
	u16 adc_adr = intelmad_drv->adc_address;
	u16 adc_val;
	int ret;
	struct sc_reg_access adc_ctrl3[2] = {
			{0x1C2, 0x05, 0x0},
	};

	struct sc_reg_access audio_adc_reg1 = {0,};
	struct sc_reg_access audio_adc_reg2 = {0,};

	msic_enable_mic_bias();
	/* Enable the msic for conversion before reading */
	ret = sst_sc_reg_access(adc_ctrl3, PMIC_WRITE, 1);
	if (ret)
		return ret;
	adc_ctrl3[0].value = 0x04;
	/* Re-toggle the RRDATARD bit */
	ret = sst_sc_reg_access(adc_ctrl3, PMIC_WRITE, 1);
	if (ret)
		return ret;

	audio_adc_reg1.reg_addr = adc_adr;
	/* Read the higher bits of data */
	msleep(1000);
	ret = sst_sc_reg_access(&audio_adc_reg1, PMIC_READ, 1);
	if (ret)
		return ret;
	pr_debug("adc read value %x", audio_adc_reg1.value);

	/* Shift bits to accomodate the lower two data bits */
	adc_val = (audio_adc_reg1.value << 2);
	adc_adr++;
	audio_adc_reg2. reg_addr = adc_adr;
	ret = sst_sc_reg_access(&audio_adc_reg2, PMIC_READ, 1);
	if (ret)
		return ret;
	pr_debug("adc read value %x", audio_adc_reg2.value);

	/* Adding lower two bits to the higher bits */
	audio_adc_reg2.value &= 03;
	adc_val += audio_adc_reg2.value;

	pr_debug("ADC value 0x%x", adc_val);
	msic_disable_mic_bias();
	return adc_val;
}

static void msic_pmic_irq_cb(void *cb_data, u8 intsts)
{
	struct mad_jack *mjack = NULL;
	unsigned int present = 0, jack_event_flag = 0, buttonpressflag = 0;
	struct snd_intelmad *intelmaddata = cb_data;
	int retval = 0;

	pr_debug("value returned = 0x%x\n", intsts);

	if (snd_msic_ops.card_status == SND_CARD_UN_INIT) {
		retval = msic_init_card();
		if (retval)
			return;
	  }

	mjack = &intelmaddata->jack[0];
	if (intsts & 0x1) {
		pr_debug("MAD short_push detected\n");
		present = SND_JACK_BTN_0;
		jack_event_flag = buttonpressflag = 1;
		mjack->jack.type = SND_JACK_BTN_0;
		mjack->jack.key[0] = BTN_0 ;
	}

	if (intsts & 0x2) {
		pr_debug(":MAD long_push detected\n");
		jack_event_flag = buttonpressflag = 1;
		mjack->jack.type = present = SND_JACK_BTN_1;
		mjack->jack.key[1] = BTN_1;
	}

	if (intsts & 0x4) {
		unsigned int mic_bias;
		jack_event_flag = 1;
		buttonpressflag = 0;
		mic_bias = msic_get_mic_bias(intelmaddata);
		pr_debug("mic_bias = %d\n", mic_bias);
		mic_bias = msic_convert_adc_to_mvolt(mic_bias);
		pr_debug("mic_bias after conversion = %d mV\n", mic_bias);
		mjack->jack_dev_state = msic_get_headset_state(mic_bias);
		mjack->jack.type = present = mjack->jack_dev_state;
	}

	if (intsts & 0x8) {
		mjack->jack.type = mjack->jack_dev_state;
		present = 0;
		jack_event_flag = 1;
		buttonpressflag = 0;
		msic_disable_jack_btn();
		msic_disable_mic_bias();
	}
	if (jack_event_flag)
		sst_mad_send_jack_report(&mjack->jack,
					buttonpressflag, present);
}



struct snd_pmic_ops snd_msic_ops = {
	.set_input_dev	=	msic_set_selected_input_dev,
	.set_output_dev =	msic_set_selected_output_dev,
	.set_lineout_dev =	msic_set_selected_lineout_dev,
	.set_hw_dmic_route =    msic_set_hw_dmic_route,
	.set_mute	=	msic_set_mute,
	.get_mute	=	msic_get_mute,
	.set_vol	=	msic_set_vol,
	.get_vol	=	msic_get_vol,
	.init_card	=	msic_init_card,
	.set_pcm_audio_params	= msic_set_pcm_audio_params,
	.set_pcm_voice_params	= msic_set_pcm_voice_params,
	.set_voice_port = msic_set_voice_port,
	.set_audio_port = msic_set_audio_port,
	.power_up_pmic_pb =	msic_power_up_pb,
	.power_up_pmic_cp =	msic_power_up_cp,
	.power_down_pmic_pb =	msic_power_down_pb,
	.power_down_pmic_cp =	msic_power_down_cp,
	.power_down_pmic	=	msic_power_down,
	.pmic_irq_cb	=	msic_pmic_irq_cb,
	.pmic_jack_enable = msic_enable_mic_bias,
	.pmic_get_mic_bias	= msic_get_mic_bias,
	.pmic_set_headset_state = msic_set_headset_state,
};
