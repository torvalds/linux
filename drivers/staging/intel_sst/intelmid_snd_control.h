#ifndef __INTELMID_SND_CTRL_H__
#define __INTELMID_SND_CTRL_H__
/*
 *  intelmid_snd_control.h - Intel Sound card driver for MID
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
 *  This file defines all snd control functions
 */

/*
Mask bits
*/
#define MASK0 0x01	/* 0000 0001 */
#define MASK1 0x02	/* 0000 0010 */
#define MASK2 0x04	/* 0000 0100 */
#define MASK3 0x08	/* 0000 1000 */
#define MASK4 0x10	/* 0001 0000 */
#define MASK5 0x20	/* 0010 0000 */
#define MASK6 0x40	/* 0100 0000 */
#define MASK7 0x80	/* 1000 0000 */
/*
value bits
*/
#define VALUE0	0x01	/* 0000 0001 */
#define VALUE1	0x02	/* 0000 0010 */
#define VALUE2	0x04	/* 0000 0100 */
#define VALUE3	0x08	/* 0000 1000 */
#define VALUE4	0x10	/* 0001 0000 */
#define VALUE5	0x20	/* 0010 0000 */
#define VALUE6	0x40	/* 0100 0000 */
#define VALUE7	0x80	/* 1000 0000 */

#define MUTE 0    /* ALSA Passes 0 for mute */
#define UNMUTE 1  /* ALSA Passes 1 for unmute */

#define MAX_VOL_PMIC_VENDOR0    0x3f /* max vol in dB for stereo & voice DAC */
#define MIN_VOL_PMIC_VENDOR0    0 /* min vol in dB for stereo & voice DAC */
/* Head phone volume control  */
#define MAX_HP_VOL_PMIC_VENDOR1    6 /* max volume in dB for HP */
#define MIN_HP_VOL_PMIC_VENDOR1    (-84) /* min volume in dB for HP */
#define MAX_HP_VOL_INDX_PMIC_VENDOR1 40 /* Number of HP volume control values */

/* Mono Earpiece Volume control */
#define MAX_EP_VOL_PMIC_VENDOR1    0 /* max volume in dB for EP */
#define MIN_EP_VOL_PMIC_VENDOR1    (-75) /* min volume in dB for EP */
#define MAX_EP_VOL_INDX_PMIC_VENDOR1 32 /* Number of EP volume control values */

int sst_sc_reg_access(struct sc_reg_access *sc_access,
					int type, int num_val);
extern struct snd_pmic_ops snd_pmic_ops_fs;
extern struct snd_pmic_ops snd_pmic_ops_mx;
extern struct snd_pmic_ops snd_pmic_ops_nc;
extern struct snd_pmic_ops snd_msic_ops;

/* device */
enum SND_INPUT_DEVICE {
	AMIC,
	DMIC,
	HS_MIC,
	IN_UNDEFINED
};

enum SND_OUTPUT_DEVICE {
	STEREO_HEADPHONE,
	MONO_EARPIECE,

	INTERNAL_SPKR,
	RECEIVER,
	OUT_UNDEFINED
};

enum pmic_controls {
	PMIC_SND_HP_MIC_MUTE =			0x0001,
	PMIC_SND_AMIC_MUTE =			0x0002,
	PMIC_SND_DMIC_MUTE =			0x0003,
	PMIC_SND_CAPTURE_VOL =			0x0004,
/* Output controls */
	PMIC_SND_LEFT_PB_VOL =			0x0010,
	PMIC_SND_RIGHT_PB_VOL =			0x0011,
	PMIC_SND_LEFT_HP_MUTE =			0x0012,
	PMIC_SND_RIGHT_HP_MUTE =		0x0013,
	PMIC_SND_LEFT_SPEAKER_MUTE =		0x0014,
	PMIC_SND_RIGHT_SPEAKER_MUTE =		0x0015,
	PMIC_SND_RECEIVER_VOL =			0x0016,
	PMIC_SND_RECEIVER_MUTE =		0x0017,
/* Other controls */
	PMIC_SND_MUTE_ALL =			0x0020,
	PMIC_MAX_CONTROLS =			0x0020,
};

#endif /* __INTELMID_SND_CTRL_H__ */


