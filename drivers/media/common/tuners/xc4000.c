/*
 *  Driver for Xceive XC4000 "QAM/8VSB single chip tuner"
 *
 *  Copyright (c) 2007 Xceive Corporation
 *  Copyright (c) 2007 Steven Toth <stoth@linuxtv.org>
 *  Copyright (c) 2009 Devin Heitmueller <dheitmueller@kernellabs.com>
 *  Copyright (c) 2009 Davide Ferri <d.ferri@zero11.it>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/videodev2.h>
#include <linux/delay.h>
#include <linux/dvb/frontend.h>
#include <linux/i2c.h>
#include <asm/unaligned.h>

#include "dvb_frontend.h"

#include "xc4000.h"
#include "tuner-i2c.h"
#include "tuner-xc2028-types.h"

static int debug=1;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off debugging (default:off).");

static int no_poweroff;
module_param(no_poweroff, int, 0644);
MODULE_PARM_DESC(no_poweroff, "0 (default) powers device off when not used.\n"
	"\t\t1 keep device energized and with tuner ready all the times.\n"
	"\t\tFaster, but consumes more power and keeps the device hotter");

static DEFINE_MUTEX(xc4000_list_mutex);
static LIST_HEAD(hybrid_tuner_instance_list);

#define dprintk(level, fmt, arg...) if (debug >= level) \
	printk(KERN_INFO "%s: " fmt, "xc4000", ## arg)

#define XC4000_DEFAULT_FIRMWARE "xc4000-02.fw"
#define XC4000_DEFAULT_FIRMWARE_SIZE 18643


/* struct for storing firmware table */
struct firmware_description {
	unsigned int  type;
	v4l2_std_id   id;
	__u16         int_freq;
	unsigned char *ptr;
	unsigned int  size;
};

struct firmware_properties {
	unsigned int	type;
	v4l2_std_id	id;
	v4l2_std_id	std_req;
	__u16		int_freq;
	unsigned int	scode_table;
	int 		scode_nr;
};

struct xc4000_priv {
	struct tuner_i2c_props i2c_props;
	struct list_head hybrid_tuner_instance_list;
	struct firmware_description *firm;
	int			firm_size;
	__u16			firm_version;
	u32 if_khz;
	u32 freq_hz;
	u32 bandwidth;
	u8  video_standard;
	u8  rf_mode;
//	struct xc2028_ctrl	ctrl;
	struct firmware_properties cur_fw;
	__u16			hwmodel;
	__u16			hwvers;
};

/* Misc Defines */
#define MAX_TV_STANDARD			23
#define XC_MAX_I2C_WRITE_LENGTH		64

/* Signal Types */
#define XC_RF_MODE_AIR			0
#define XC_RF_MODE_CABLE		1

/* Result codes */
#define XC_RESULT_SUCCESS		0
#define XC_RESULT_RESET_FAILURE		1
#define XC_RESULT_I2C_WRITE_FAILURE	2
#define XC_RESULT_I2C_READ_FAILURE	3
#define XC_RESULT_OUT_OF_RANGE		5

/* Product id */
#define XC_PRODUCT_ID_FW_NOT_LOADED	0x2000
#define XC_PRODUCT_ID_FW_LOADED 	0x0FA0

/* Registers (Write-only) */
#define XREG_INIT         0x00
#define XREG_VIDEO_MODE   0x01
#define XREG_AUDIO_MODE   0x02
#define XREG_RF_FREQ      0x03
#define XREG_D_CODE       0x04
#define XREG_DIRECTSITTING_MODE 0x05
#define XREG_SEEK_MODE    0x06
#define XREG_POWER_DOWN   0x08
#define XREG_SIGNALSOURCE 0x0A
#define XREG_AMPLITUDE    0x10

/* Registers (Read-only) */
#define XREG_ADC_ENV      0x00
#define XREG_QUALITY      0x01
#define XREG_FRAME_LINES  0x02
#define XREG_HSYNC_FREQ   0x03
#define XREG_LOCK         0x04
#define XREG_FREQ_ERROR   0x05
#define XREG_SNR          0x06
#define XREG_VERSION      0x07
#define XREG_PRODUCT_ID   0x08

/*
   Basic firmware description. This will remain with
   the driver for documentation purposes.

   This represents an I2C firmware file encoded as a
   string of unsigned char. Format is as follows:

   char[0  ]=len0_MSB  -> len = len_MSB * 256 + len_LSB
   char[1  ]=len0_LSB  -> length of first write transaction
   char[2  ]=data0 -> first byte to be sent
   char[3  ]=data1
   char[4  ]=data2
   char[   ]=...
   char[M  ]=dataN  -> last byte to be sent
   char[M+1]=len1_MSB  -> len = len_MSB * 256 + len_LSB
   char[M+2]=len1_LSB  -> length of second write transaction
   char[M+3]=data0
   char[M+4]=data1
   ...
   etc.

   The [len] value should be interpreted as follows:

   len= len_MSB _ len_LSB
   len=1111_1111_1111_1111   : End of I2C_SEQUENCE
   len=0000_0000_0000_0000   : Reset command: Do hardware reset
   len=0NNN_NNNN_NNNN_NNNN   : Normal transaction: number of bytes = {1:32767)
   len=1WWW_WWWW_WWWW_WWWW   : Wait command: wait for {1:32767} ms

   For the RESET and WAIT commands, the two following bytes will contain
   immediately the length of the following transaction.

*/
struct XC_TV_STANDARD {
	char *Name;
	u16 AudioMode;
	u16 VideoMode;
};

/* Tuner standards */
#define MN_NTSC_PAL_BTSC	0
#define MN_NTSC_PAL_A2		1
#define MN_NTSC_PAL_EIAJ	2
#define MN_NTSC_PAL_Mono	3
#define BG_PAL_A2		4
#define BG_PAL_NICAM		5
#define BG_PAL_MONO		6
#define I_PAL_NICAM		7
#define I_PAL_NICAM_MONO	8
#define DK_PAL_A2		9
#define DK_PAL_NICAM		10
#define DK_PAL_MONO		11
#define DK_SECAM_A2DK1		12
#define DK_SECAM_A2LDK3 	13
#define DK_SECAM_A2MONO 	14
#define L_SECAM_NICAM		15
#define LC_SECAM_NICAM		16
#define FM_Radio_INPUT2 	21
#define FM_Radio_INPUT1 	22

/* WAS :
static struct XC_TV_STANDARD XC4000_Standard[MAX_TV_STANDARD] = {
	{"M/N-NTSC/PAL-BTSC", 0x0400, 0x8020},
	{"M/N-NTSC/PAL-A2",   0x0600, 0x8020},
	{"M/N-NTSC/PAL-EIAJ", 0x0440, 0x8020},
	{"M/N-NTSC/PAL-Mono", 0x0478, 0x8020},
	{"B/G-PAL-A2",        0x0A00, 0x8049},
	{"B/G-PAL-NICAM",     0x0C04, 0x8049},
	{"B/G-PAL-MONO",      0x0878, 0x8059},
	{"I-PAL-NICAM",       0x1080, 0x8009},
	{"I-PAL-NICAM-MONO",  0x0E78, 0x8009},
	{"D/K-PAL-A2",        0x1600, 0x8009},
	{"D/K-PAL-NICAM",     0x0E80, 0x8009},
	{"D/K-PAL-MONO",      0x1478, 0x8009},
	{"D/K-SECAM-A2 DK1",  0x1200, 0x8009},
	{"D/K-SECAM-A2 L/DK3", 0x0E00, 0x8009},
	{"D/K-SECAM-A2 MONO", 0x1478, 0x8009},
	{"L-SECAM-NICAM",     0x8E82, 0x0009},
	{"L'-SECAM-NICAM",    0x8E82, 0x4009},
	{"DTV6",              0x00C0, 0x8002},
	{"DTV8",              0x00C0, 0x800B},
	{"DTV7/8",            0x00C0, 0x801B},
	{"DTV7",              0x00C0, 0x8007},
	{"FM Radio-INPUT2",   0x9802, 0x9002},
	{"FM Radio-INPUT1",   0x0208, 0x9002}
};*/

static struct XC_TV_STANDARD XC4000_Standard[MAX_TV_STANDARD] = {
	{"M/N-NTSC/PAL-BTSC", 0x0000, 0x8020},
	{"M/N-NTSC/PAL-A2",   0x0000, 0x8020},
	{"M/N-NTSC/PAL-EIAJ", 0x0040, 0x8020},
	{"M/N-NTSC/PAL-Mono", 0x0078, 0x8020},
	{"B/G-PAL-A2",        0x0000, 0x8059},
	{"B/G-PAL-NICAM",     0x0004, 0x8059},
	{"B/G-PAL-MONO",      0x0078, 0x8059},
	{"I-PAL-NICAM",       0x0080, 0x8049},
	{"I-PAL-NICAM-MONO",  0x0078, 0x8049},
	{"D/K-PAL-A2",        0x0000, 0x8049},
	{"D/K-PAL-NICAM",     0x0080, 0x8049},
	{"D/K-PAL-MONO",      0x0078, 0x8049},
	{"D/K-SECAM-A2 DK1",  0x0000, 0x8049},
	{"D/K-SECAM-A2 L/DK3", 0x0000, 0x8049},
	{"D/K-SECAM-A2 MONO", 0x0078, 0x8049},
	{"L-SECAM-NICAM",     0x8080, 0x0009},
	{"L'-SECAM-NICAM",    0x8080, 0x4009},
	{"DTV6",              0x00C0, 0x8002},
	{"DTV8",              0x00C0, 0x800B},
	{"DTV7/8",            0x00C0, 0x801B},
	{"DTV7",              0x00C0, 0x8007},
	{"FM Radio-INPUT2",   0x0008, 0x9800},
	{"FM Radio-INPUT1",   0x0008, 0x9000}
};

static int xc4000_is_firmware_loaded(struct dvb_frontend *fe);
static int xc4000_readreg(struct xc4000_priv *priv, u16 reg, u16 *val);
static int xc4000_TunerReset(struct dvb_frontend *fe);

static int xc_send_i2c_data(struct xc4000_priv *priv, u8 *buf, int len)
{
	struct i2c_msg msg = { .addr = priv->i2c_props.addr,
			       .flags = 0, .buf = buf, .len = len };
	if (i2c_transfer(priv->i2c_props.adap, &msg, 1) != 1) {
		printk(KERN_ERR "xc4000: I2C write failed (len=%i)\n", len);
		return XC_RESULT_I2C_WRITE_FAILURE;
	}
	return XC_RESULT_SUCCESS;
}

/* This routine is never used because the only time we read data from the
   i2c bus is when we read registers, and we want that to be an atomic i2c
   transaction in case we are on a multi-master bus */

static void xc_wait(int wait_ms)
{
	msleep(wait_ms);
}

static int xc4000_TunerReset(struct dvb_frontend *fe)
{
	struct xc4000_priv *priv = fe->tuner_priv;
	int ret;

	dprintk(1, "%s()\n", __func__);

	if (fe->callback) {
		ret = fe->callback(((fe->dvb) && (fe->dvb->priv)) ?
					   fe->dvb->priv :
					   priv->i2c_props.adap->algo_data,
					   DVB_FRONTEND_COMPONENT_TUNER,
					   XC4000_TUNER_RESET, 0);
		if (ret) {
			printk(KERN_ERR "xc4000: reset failed\n");
			return XC_RESULT_RESET_FAILURE;
		}
	} else {
		printk(KERN_ERR "xc4000: no tuner reset callback function, fatal\n");
		return XC_RESULT_RESET_FAILURE;
	}
	return XC_RESULT_SUCCESS;
}

static int xc_write_reg(struct xc4000_priv *priv, u16 regAddr, u16 i2cData)
{
	u8 buf[4];
	int result;

	buf[0] = (regAddr >> 8) & 0xFF;
	buf[1] = regAddr & 0xFF;
	buf[2] = (i2cData >> 8) & 0xFF;
	buf[3] = i2cData & 0xFF;
	result = xc_send_i2c_data(priv, buf, 4);

	return result;
}

static int xc_load_i2c_sequence(struct dvb_frontend *fe, const u8 *i2c_sequence)
{
	struct xc4000_priv *priv = fe->tuner_priv;

	int i, nbytes_to_send, result;
	unsigned int len, pos, index;
	u8 buf[XC_MAX_I2C_WRITE_LENGTH];

	index = 0;
	while ((i2c_sequence[index] != 0xFF) ||
		(i2c_sequence[index + 1] != 0xFF)) {
		len = i2c_sequence[index] * 256 + i2c_sequence[index+1];
		if (len == 0x0000) {
			/* RESET command */
			result = xc4000_TunerReset(fe);
			index += 2;
			if (result != XC_RESULT_SUCCESS)
				return result;
		} else if (len & 0x8000) {
			/* WAIT command */
			xc_wait(len & 0x7FFF);
			index += 2;
		} else {
			/* Send i2c data whilst ensuring individual transactions
			 * do not exceed XC_MAX_I2C_WRITE_LENGTH bytes.
			 */
			index += 2;
			buf[0] = i2c_sequence[index];
			buf[1] = i2c_sequence[index + 1];
			pos = 2;
			while (pos < len) {
				if ((len - pos) > XC_MAX_I2C_WRITE_LENGTH - 2)
					nbytes_to_send =
						XC_MAX_I2C_WRITE_LENGTH;
				else
					nbytes_to_send = (len - pos + 2);
				for (i = 2; i < nbytes_to_send; i++) {
					buf[i] = i2c_sequence[index + pos +
						i - 2];
				}
				result = xc_send_i2c_data(priv, buf,
					nbytes_to_send);

				if (result != XC_RESULT_SUCCESS)
					return result;

				pos += nbytes_to_send - 2;
			}
			index += len;
		}
	}
	return XC_RESULT_SUCCESS;
}

static int xc_SetTVStandard(struct xc4000_priv *priv,
	u16 VideoMode, u16 AudioMode)
{
	int ret;
	dprintk(1, "%s(0x%04x,0x%04x)\n", __func__, VideoMode, AudioMode);
	dprintk(1, "%s() Standard = %s\n",
		__func__,
		XC4000_Standard[priv->video_standard].Name);

	ret = xc_write_reg(priv, XREG_VIDEO_MODE, VideoMode);
	if (ret == XC_RESULT_SUCCESS)
		ret = xc_write_reg(priv, XREG_AUDIO_MODE, AudioMode);

	return ret;
}

static int xc_SetSignalSource(struct xc4000_priv *priv, u16 rf_mode)
{
	dprintk(1, "%s(%d) Source = %s\n", __func__, rf_mode,
		rf_mode == XC_RF_MODE_AIR ? "ANTENNA" : "CABLE");

	if ((rf_mode != XC_RF_MODE_AIR) && (rf_mode != XC_RF_MODE_CABLE)) {
		rf_mode = XC_RF_MODE_CABLE;
		printk(KERN_ERR
			"%s(), Invalid mode, defaulting to CABLE",
			__func__);
	}
	return xc_write_reg(priv, XREG_SIGNALSOURCE, rf_mode);
}

static const struct dvb_tuner_ops xc4000_tuner_ops;

static int xc_set_RF_frequency(struct xc4000_priv *priv, u32 freq_hz)
{
	u16 freq_code;

	dprintk(1, "%s(%u)\n", __func__, freq_hz);

	if ((freq_hz > xc4000_tuner_ops.info.frequency_max) ||
		(freq_hz < xc4000_tuner_ops.info.frequency_min))
		return XC_RESULT_OUT_OF_RANGE;

	freq_code = (u16)(freq_hz / 15625);

	/* WAS: Starting in firmware version 1.1.44, Xceive recommends using the
	   FINERFREQ for all normal tuning (the doc indicates reg 0x03 should
	   only be used for fast scanning for channel lock) */
	return xc_write_reg(priv, XREG_RF_FREQ, freq_code); /* WAS: XREG_FINERFREQ */
}


static int xc_get_ADC_Envelope(struct xc4000_priv *priv, u16 *adc_envelope)
{
	return xc4000_readreg(priv, XREG_ADC_ENV, adc_envelope);
}

static int xc_get_frequency_error(struct xc4000_priv *priv, u32 *freq_error_hz)
{
	int result;
	u16 regData;
	u32 tmp;

	result = xc4000_readreg(priv, XREG_FREQ_ERROR, &regData);
	if (result != XC_RESULT_SUCCESS)
		return result;

	tmp = (u32)regData;
	(*freq_error_hz) = (tmp * 15625) / 1000;
	return result;
}

static int xc_get_lock_status(struct xc4000_priv *priv, u16 *lock_status)
{
	return xc4000_readreg(priv, XREG_LOCK, lock_status);
}

static int xc_get_version(struct xc4000_priv *priv,
	u8 *hw_majorversion, u8 *hw_minorversion,
	u8 *fw_majorversion, u8 *fw_minorversion)
{
	u16 data;
	int result;

	result = xc4000_readreg(priv, XREG_VERSION, &data);
	if (result != XC_RESULT_SUCCESS)
		return result;

	(*hw_majorversion) = (data >> 12) & 0x0F;
	(*hw_minorversion) = (data >>  8) & 0x0F;
	(*fw_majorversion) = (data >>  4) & 0x0F;
	(*fw_minorversion) = data & 0x0F;

	return 0;
}

/* WAS THERE
static int xc_get_buildversion(struct xc4000_priv *priv, u16 *buildrev)
{
	return xc4000_readreg(priv, XREG_BUILD, buildrev);
}*/

static int xc_get_hsync_freq(struct xc4000_priv *priv, u32 *hsync_freq_hz)
{
	u16 regData;
	int result;

	result = xc4000_readreg(priv, XREG_HSYNC_FREQ, &regData);
	if (result != XC_RESULT_SUCCESS)
		return result;

	(*hsync_freq_hz) = ((regData & 0x0fff) * 763)/100;
	return result;
}

static int xc_get_frame_lines(struct xc4000_priv *priv, u16 *frame_lines)
{
	return xc4000_readreg(priv, XREG_FRAME_LINES, frame_lines);
}

static int xc_get_quality(struct xc4000_priv *priv, u16 *quality)
{
	return xc4000_readreg(priv, XREG_QUALITY, quality);
}

static u16 WaitForLock(struct xc4000_priv *priv)
{
	u16 lockState = 0;
	int watchDogCount = 40;

	while ((lockState == 0) && (watchDogCount > 0)) {
		xc_get_lock_status(priv, &lockState);
		if (lockState != 1) {
			xc_wait(5);
			watchDogCount--;
		}
	}
	return lockState;
}

#define XC_TUNE_ANALOG  0
#define XC_TUNE_DIGITAL 1
static int xc_tune_channel(struct xc4000_priv *priv, u32 freq_hz, int mode)
{
	int found = 0;

	dprintk(1, "%s(%u)\n", __func__, freq_hz);

	if (xc_set_RF_frequency(priv, freq_hz) != XC_RESULT_SUCCESS)
		return 0;

	if (mode == XC_TUNE_ANALOG) {
		if (WaitForLock(priv) == 1)
			found = 1;
	}

	return found;
}

static int xc4000_readreg(struct xc4000_priv *priv, u16 reg, u16 *val)
{
	u8 buf[2] = { reg >> 8, reg & 0xff };
	u8 bval[2] = { 0, 0 };
	struct i2c_msg msg[2] = {
		{ .addr = priv->i2c_props.addr,
			.flags = 0, .buf = &buf[0], .len = 2 },
		{ .addr = priv->i2c_props.addr,
			.flags = I2C_M_RD, .buf = &bval[0], .len = 2 },
	};

	if (i2c_transfer(priv->i2c_props.adap, msg, 2) != 2) {
		printk(KERN_WARNING "xc4000: I2C read failed\n");
		return -EREMOTEIO;
	}

	*val = (bval[0] << 8) | bval[1];
	return XC_RESULT_SUCCESS;
}

#define dump_firm_type(t) 	dump_firm_type_and_int_freq(t, 0)
static void dump_firm_type_and_int_freq(unsigned int type, u16 int_freq)
{
	 if (type & BASE)
		printk("BASE ");
	 if (type & INIT1)
		printk("INIT1 ");
	 if (type & F8MHZ)
		printk("F8MHZ ");
	 if (type & MTS)
		printk("MTS ");
	 if (type & D2620)
		printk("D2620 ");
	 if (type & D2633)
		printk("D2633 ");
	 if (type & DTV6)
		printk("DTV6 ");
	 if (type & QAM)
		printk("QAM ");
	 if (type & DTV7)
		printk("DTV7 ");
	 if (type & DTV78)
		printk("DTV78 ");
	 if (type & DTV8)
		printk("DTV8 ");
	 if (type & FM)
		printk("FM ");
	 if (type & INPUT1)
		printk("INPUT1 ");
	 if (type & LCD)
		printk("LCD ");
	 if (type & NOGD)
		printk("NOGD ");
	 if (type & MONO)
		printk("MONO ");
	 if (type & ATSC)
		printk("ATSC ");
	 if (type & IF)
		printk("IF ");
	 if (type & LG60)
		printk("LG60 ");
	 if (type & ATI638)
		printk("ATI638 ");
	 if (type & OREN538)
		printk("OREN538 ");
	 if (type & OREN36)
		printk("OREN36 ");
	 if (type & TOYOTA388)
		printk("TOYOTA388 ");
	 if (type & TOYOTA794)
		printk("TOYOTA794 ");
	 if (type & DIBCOM52)
		printk("DIBCOM52 ");
	 if (type & ZARLINK456)
		printk("ZARLINK456 ");
	 if (type & CHINA)
		printk("CHINA ");
	 if (type & F6MHZ)
		printk("F6MHZ ");
	 if (type & INPUT2)
		printk("INPUT2 ");
	 if (type & SCODE)
		printk("SCODE ");
	 if (type & HAS_IF)
		printk("HAS_IF_%d ", int_freq);
}

static int seek_firmware(struct dvb_frontend *fe, unsigned int type,
			 v4l2_std_id *id)
{
	struct xc4000_priv *priv = fe->tuner_priv;
	int                 i, best_i = -1, best_nr_matches = 0;
	unsigned int        type_mask = 0;

	printk("%s called, want type=", __func__);
	if (debug) {
		dump_firm_type(type);
		printk("(%x), id %016llx.\n", type, (unsigned long long)*id);
	}

	if (!priv->firm) {
		printk("Error! firmware not loaded\n");
		return -EINVAL;
	}

	if (((type & ~SCODE) == 0) && (*id == 0))
		*id = V4L2_STD_PAL;

	if (type & BASE)
		type_mask = BASE_TYPES;
	else if (type & SCODE) {
		type &= SCODE_TYPES;
		type_mask = SCODE_TYPES & ~HAS_IF;
	} else if (type & DTV_TYPES)
		type_mask = DTV_TYPES;
	else if (type & STD_SPECIFIC_TYPES)
		type_mask = STD_SPECIFIC_TYPES;

	type &= type_mask;

	if (!(type & SCODE))
		type_mask = ~0;

	/* Seek for exact match */
	for (i = 0; i < priv->firm_size; i++) {
		if ((type == (priv->firm[i].type & type_mask)) &&
		    (*id == priv->firm[i].id))
			goto found;
	}

	/* Seek for generic video standard match */
	for (i = 0; i < priv->firm_size; i++) {
		v4l2_std_id match_mask;
		int nr_matches;

		if (type != (priv->firm[i].type & type_mask))
			continue;

		match_mask = *id & priv->firm[i].id;
		if (!match_mask)
			continue;

		if ((*id & match_mask) == *id)
			goto found; /* Supports all the requested standards */

		nr_matches = hweight64(match_mask);
		if (nr_matches > best_nr_matches) {
			best_nr_matches = nr_matches;
			best_i = i;
		}
	}

	if (best_nr_matches > 0) {
		printk("Selecting best matching firmware (%d bits) for "
			  "type=", best_nr_matches);
//		dump_firm_type(type);
		printk("(%x), id %016llx:\n", type, (unsigned long long)*id);
		i = best_i;
		goto found;
	}

	/*FIXME: Would make sense to seek for type "hint" match ? */

	i = -ENOENT;
	goto ret;

found:
	*id = priv->firm[i].id;

ret:
	printk("%s firmware for type=", (i < 0) ? "Can't find" : "Found");
	if (debug) {
		dump_firm_type(type);
		printk("(%x), id %016llx.\n", type, (unsigned long long)*id);
		if (i < 0)
			dump_stack();
	}
	return i;
}

static int load_firmware(struct dvb_frontend *fe, unsigned int type,
			 v4l2_std_id *id)
{
	struct xc4000_priv *priv = fe->tuner_priv;
	int                pos, rc;
	unsigned char      *p;

	printk("%s called\n", __func__);

	pos = seek_firmware(fe, type, id);
	if (pos < 0)
		return pos;

	printk("Loading firmware for type=");
//	dump_firm_type(priv->firm[pos].type);
	printk("(%x), id %016llx.\n", priv->firm[pos].type,
	       (unsigned long long)*id);

	p = priv->firm[pos].ptr;
	printk("firmware length = %d\n", priv->firm[pos].size);

	rc = xc_load_i2c_sequence(fe, p);

	return rc;
}

static int xc4000_fwupload(struct dvb_frontend *fe)
{
	struct xc4000_priv *priv = fe->tuner_priv;
	const struct firmware *fw   = NULL;
	const unsigned char   *p, *endp;
	int                   rc = 0;
	int		      n, n_array;
	char		      name[33];
	char		      *fname;

	printk("%s called\n", __func__);

	fname = XC4000_DEFAULT_FIRMWARE;

	printk("Reading firmware %s\n",  fname);
	rc = request_firmware(&fw, fname, priv->i2c_props.adap->dev.parent);
	if (rc < 0) {
		if (rc == -ENOENT)
			printk("Error: firmware %s not found.\n",
				   fname);
		else
			printk("Error %d while requesting firmware %s \n",
				   rc, fname);

		return rc;
	}
	p = fw->data;
	endp = p + fw->size;

	if (fw->size < sizeof(name) - 1 + 2 + 2) {
		printk("Error: firmware file %s has invalid size!\n",
			  fname);
		goto corrupt;
	}

	memcpy(name, p, sizeof(name) - 1);
	name[sizeof(name) - 1] = 0;
	p += sizeof(name) - 1;

	priv->firm_version = get_unaligned_le16(p);
	p += 2;

	n_array = get_unaligned_le16(p);
	p += 2;

	printk("Loading %d firmware images from %s, type: %s, ver %d.%d\n",
		   n_array, fname, name,
		   priv->firm_version >> 8, priv->firm_version & 0xff);

	priv->firm = kzalloc(sizeof(*priv->firm) * n_array, GFP_KERNEL);
	if (priv->firm == NULL) {
		printk("Not enough memory to load firmware file.\n");
		rc = -ENOMEM;
		goto err;
	}
	priv->firm_size = n_array;

	n = -1;
	while (p < endp) {
		__u32 type, size;
		v4l2_std_id id;
		__u16 int_freq = 0;

		n++;
		if (n >= n_array) {
			printk("More firmware images in file than "
				  "were expected!\n");
			goto corrupt;
		}

		/* Checks if there's enough bytes to read */
		if (endp - p < sizeof(type) + sizeof(id) + sizeof(size))
			goto header;

		type = get_unaligned_le32(p);
		p += sizeof(type);

		id = get_unaligned_le64(p);
		p += sizeof(id);

		if (type & HAS_IF) {
			int_freq = get_unaligned_le16(p);
			p += sizeof(int_freq);
			if (endp - p < sizeof(size))
				goto header;
		}

		size = get_unaligned_le32(p);
		p += sizeof(size);

		if (!size || size > endp - p) {
			printk("Firmware type ");
//			dump_firm_type(type);
			printk("(%x), id %llx is corrupted "
			       "(size=%d, expected %d)\n",
			       type, (unsigned long long)id,
			       (unsigned)(endp - p), size);
			goto corrupt;
		}

		priv->firm[n].ptr = kzalloc(size, GFP_KERNEL);
		if (priv->firm[n].ptr == NULL) {
			printk("Not enough memory to load firmware file.\n");
			rc = -ENOMEM;
			goto err;
		}

		if (debug) {
			printk("Reading firmware type ");
			dump_firm_type_and_int_freq(type, int_freq);
			printk("(%x), id %llx, size=%d.\n",
			       type, (unsigned long long)id, size);
		}

		memcpy(priv->firm[n].ptr, p, size);
		priv->firm[n].type = type;
		priv->firm[n].id   = id;
		priv->firm[n].size = size;
		priv->firm[n].int_freq = int_freq;

		p += size;
	}

	if (n + 1 != priv->firm_size) {
		printk("Firmware file is incomplete!\n");
		goto corrupt;
	}

	goto done;

header:
	printk("Firmware header is incomplete!\n");
corrupt:
	rc = -EINVAL;
	printk("Error: firmware file is corrupted!\n");

err:
	printk("Releasing partially loaded firmware file.\n");
//	free_firmware(priv);

done:
	release_firmware(fw);
	if (rc == 0)
		printk("Firmware files loaded.\n");

	return rc;
}

static int load_scode(struct dvb_frontend *fe, unsigned int type,
			 v4l2_std_id *id, __u16 int_freq, int scode)
{
	struct xc4000_priv *priv = fe->tuner_priv;
	int                pos, rc;
	unsigned char	   *p;
	u8 scode_buf[13];
	u8 indirect_mode[5];

	dprintk(1, "%s called int_freq=%d\n", __func__, int_freq);

	if (!int_freq) {
		pos = seek_firmware(fe, type, id);
		if (pos < 0)
			return pos;
	} else {
		for (pos = 0; pos < priv->firm_size; pos++) {
			if ((priv->firm[pos].int_freq == int_freq) &&
			    (priv->firm[pos].type & HAS_IF))
				break;
		}
		if (pos == priv->firm_size)
			return -ENOENT;
	}

	p = priv->firm[pos].ptr;

	if (priv->firm[pos].type & HAS_IF) {
		if (priv->firm[pos].size != 12 * 16 || scode >= 16)
			return -EINVAL;
		p += 12 * scode;
	} else {
		/* 16 SCODE entries per file; each SCODE entry is 12 bytes and
		 * has a 2-byte size header in the firmware format. */
		if (priv->firm[pos].size != 14 * 16 || scode >= 16 ||
		    le16_to_cpu(*(__u16 *)(p + 14 * scode)) != 12)
			return -EINVAL;
		p += 14 * scode + 2;
	}

	tuner_info("Loading SCODE for type=");
	dump_firm_type_and_int_freq(priv->firm[pos].type,
				    priv->firm[pos].int_freq);
	printk("(%x), id %016llx.\n", priv->firm[pos].type,
	       (unsigned long long)*id);

	scode_buf[0] = 0x00;
	memcpy(&scode_buf[1], p, 12);

	/* Enter direct-mode */
	rc = xc_write_reg(priv, XREG_DIRECTSITTING_MODE, 0);
	if (rc < 0) {
		printk("failed to put device into direct mode!\n");
		return -EIO;
	}

	rc = xc_send_i2c_data(priv, scode_buf, 13);
	if (rc != XC_RESULT_SUCCESS) {
		/* Even if the send failed, make sure we set back to indirect
		   mode */
		printk("Failed to set scode %d\n", rc);
	}

	/* Switch back to indirect-mode */
	memset(indirect_mode, 0, sizeof(indirect_mode));
	indirect_mode[4] = 0x88;
	xc_send_i2c_data(priv, indirect_mode, sizeof(indirect_mode));
	msleep(10);

	return 0;
}

static int check_firmware(struct dvb_frontend *fe, unsigned int type,
			  v4l2_std_id std, __u16 int_freq)
{
	struct xc4000_priv         *priv = fe->tuner_priv;
	struct firmware_properties new_fw;
	int			   rc = 0, is_retry = 0;
	u16			   version, hwmodel;
	v4l2_std_id		   std0;
	u8 		   	   hw_major, hw_minor, fw_major, fw_minor;

	dprintk(1, "%s called\n", __func__);

	if (!priv->firm) {
		rc = xc4000_fwupload(fe);
		if (rc < 0)
			return rc;
	}

#ifdef DJH_DEBUG
	if (priv->ctrl.mts && !(type & FM))
		type |= MTS;
#endif

retry:
	new_fw.type = type;
	new_fw.id = std;
	new_fw.std_req = std;
//	new_fw.scode_table = SCODE | priv->ctrl.scode_table;
	new_fw.scode_table = SCODE;
	new_fw.scode_nr = 0;
	new_fw.int_freq = int_freq;

	dprintk(1, "checking firmware, user requested type=");
	if (debug) {
		dump_firm_type(new_fw.type);
		printk("(%x), id %016llx, ", new_fw.type,
		       (unsigned long long)new_fw.std_req);
		if (!int_freq) {
			printk("scode_tbl ");
#ifdef DJH_DEBUG
			dump_firm_type(priv->ctrl.scode_table);
			printk("(%x), ", priv->ctrl.scode_table);
#endif
		} else
			printk("int_freq %d, ", new_fw.int_freq);
		printk("scode_nr %d\n", new_fw.scode_nr);
	}

	/* No need to reload base firmware if it matches */
	if (((BASE | new_fw.type) & BASE_TYPES) ==
	    (priv->cur_fw.type & BASE_TYPES)) {
		dprintk(1, "BASE firmware not changed.\n");
		goto skip_base;
	}

	/* Updating BASE - forget about all currently loaded firmware */
	memset(&priv->cur_fw, 0, sizeof(priv->cur_fw));

	/* Reset is needed before loading firmware */
	rc = xc4000_TunerReset(fe);
	if (rc < 0)
		goto fail;

	/* BASE firmwares are all std0 */
	std0 = 0;
	rc = load_firmware(fe, BASE | new_fw.type, &std0);
	if (rc < 0) {
		printk("Error %d while loading base firmware\n", rc);
		goto fail;
	}

	/* Load INIT1, if needed */
	dprintk(1, "Load init1 firmware, if exists\n");

	rc = load_firmware(fe, BASE | INIT1 | new_fw.type, &std0);
	if (rc == -ENOENT)
		rc = load_firmware(fe, (BASE | INIT1 | new_fw.type) & ~F8MHZ,
				   &std0);
	if (rc < 0 && rc != -ENOENT) {
		tuner_err("Error %d while loading init1 firmware\n",
			  rc);
		goto fail;
	}

	printk("Done with init1\n");

skip_base:
	/*
	 * No need to reload standard specific firmware if base firmware
	 * was not reloaded and requested video standards have not changed.
	 */
	if (priv->cur_fw.type == (BASE | new_fw.type) &&
	    priv->cur_fw.std_req == std) {
		dprintk(1, "Std-specific firmware already loaded.\n");
		goto skip_std_specific;
	}

	/* Reloading std-specific firmware forces a SCODE update */
	priv->cur_fw.scode_table = 0;

	/* Load the standard firmware */
	rc = load_firmware(fe, new_fw.type, &new_fw.id);

	if (rc < 0)
		goto fail;

skip_std_specific:
	if (priv->cur_fw.scode_table == new_fw.scode_table &&
	    priv->cur_fw.scode_nr == new_fw.scode_nr) {
		dprintk(1, "SCODE firmware already loaded.\n");
		goto check_device;
	}

	if (new_fw.type & FM)
		goto check_device;

	/* Load SCODE firmware, if exists */
	rc = load_scode(fe, new_fw.type | new_fw.scode_table, &new_fw.id,
			new_fw.int_freq, new_fw.scode_nr);
	if (rc != XC_RESULT_SUCCESS)
		dprintk(1, "load scode failed %d\n", rc);

check_device:
	rc = xc4000_readreg(priv, XREG_PRODUCT_ID, &hwmodel);

	if (xc_get_version(priv, &hw_major, &hw_minor, &fw_major, 
			   &fw_minor) != XC_RESULT_SUCCESS) {
		printk("Unable to read tuner registers.\n");
		goto fail;
	}

	dprintk(1, "Device is Xceive %d version %d.%d, "
		"firmware version %d.%d\n",
		hwmodel, hw_major, hw_minor, fw_major, fw_minor);

	/* Check firmware version against what we downloaded. */
#ifdef DJH_DEBUG
	if (priv->firm_version != ((version & 0xf0) << 4 | (version & 0x0f))) {
		printk("Incorrect readback of firmware version %x.\n",
		       (version & 0xff));
		goto fail;
	}
#endif

	/* Check that the tuner hardware model remains consistent over time. */
	if (priv->hwmodel == 0 && hwmodel == 4000) {
		priv->hwmodel = hwmodel;
		priv->hwvers  = version & 0xff00;
	} else if (priv->hwmodel == 0 || priv->hwmodel != hwmodel ||
		   priv->hwvers != (version & 0xff00)) {
		printk("Read invalid device hardware information - tuner "
			  "hung?\n");
		goto fail;
	}

	memcpy(&priv->cur_fw, &new_fw, sizeof(priv->cur_fw));

	/*
	 * By setting BASE in cur_fw.type only after successfully loading all
	 * firmwares, we can:
	 * 1. Identify that BASE firmware with type=0 has been loaded;
	 * 2. Tell whether BASE firmware was just changed the next time through.
	 */
	priv->cur_fw.type |= BASE;

	return 0;

fail:
	memset(&priv->cur_fw, 0, sizeof(priv->cur_fw));
	if (!is_retry) {
		msleep(50);
		is_retry = 1;
		dprintk(1, "Retrying firmware load\n");
		goto retry;
	}

	if (rc == -ENOENT)
		rc = -EINVAL;
	return rc;
}

static void xc_debug_dump(struct xc4000_priv *priv)
{
	u16 adc_envelope;
	u32 freq_error_hz = 0;
	u16 lock_status;
	u32 hsync_freq_hz = 0;
	u16 frame_lines;
	u16 quality;
	u8 hw_majorversion = 0, hw_minorversion = 0;
	u8 fw_majorversion = 0, fw_minorversion = 0;
//	u16 fw_buildversion = 0;

	/* Wait for stats to stabilize.
	 * Frame Lines needs two frame times after initial lock
	 * before it is valid.
	 */
	xc_wait(100);

	xc_get_ADC_Envelope(priv,  &adc_envelope);
	dprintk(1, "*** ADC envelope (0-1023) = %d\n", adc_envelope);

	xc_get_frequency_error(priv, &freq_error_hz);
	dprintk(1, "*** Frequency error = %d Hz\n", freq_error_hz);

	xc_get_lock_status(priv,  &lock_status);
	dprintk(1, "*** Lock status (0-Wait, 1-Locked, 2-No-signal) = %d\n",
		lock_status);

	xc_get_version(priv,  &hw_majorversion, &hw_minorversion,
		&fw_majorversion, &fw_minorversion);
// WAS:
//	xc_get_buildversion(priv,  &fw_buildversion);
//	dprintk(1, "*** HW: V%02x.%02x, FW: V%02x.%02x.%04x\n",
//		hw_majorversion, hw_minorversion,
//		fw_majorversion, fw_minorversion, fw_buildversion);
// NOW:
	dprintk(1, "*** HW: V%02x.%02x, FW: V%02x.%02x\n",
		hw_majorversion, hw_minorversion,
		fw_majorversion, fw_minorversion);

	xc_get_hsync_freq(priv,  &hsync_freq_hz);
	dprintk(1, "*** Horizontal sync frequency = %d Hz\n", hsync_freq_hz);

	xc_get_frame_lines(priv,  &frame_lines);
	dprintk(1, "*** Frame lines = %d\n", frame_lines);

	xc_get_quality(priv,  &quality);
	dprintk(1, "*** Quality (0:<8dB, 7:>56dB) = %d\n", quality);
}

static int xc4000_set_params(struct dvb_frontend *fe,
	struct dvb_frontend_parameters *params)
{
	struct xc4000_priv *priv = fe->tuner_priv;
	int ret;

	dprintk(1, "%s() frequency=%d (Hz)\n", __func__, params->frequency);

	/* FIXME: setup proper parameters */
	if (check_firmware(fe, DTV8, 0, priv->if_khz) != XC_RESULT_SUCCESS) {
		return -EREMOTEIO;
	}

	if (fe->ops.info.type == FE_ATSC) {
		dprintk(1, "%s() ATSC\n", __func__);
		switch (params->u.vsb.modulation) {
		case VSB_8:
		case VSB_16:
			dprintk(1, "%s() VSB modulation\n", __func__);
			priv->rf_mode = XC_RF_MODE_AIR;
			priv->freq_hz = params->frequency - 1750000;
			priv->bandwidth = BANDWIDTH_6_MHZ;
			priv->video_standard = DTV6;
			break;
		case QAM_64:
		case QAM_256:
		case QAM_AUTO:
			dprintk(1, "%s() QAM modulation\n", __func__);
			priv->rf_mode = XC_RF_MODE_CABLE;
			priv->freq_hz = params->frequency - 1750000;
			priv->bandwidth = BANDWIDTH_6_MHZ;
			priv->video_standard = DTV6;
			break;
		default:
			return -EINVAL;
		}
	} else if (fe->ops.info.type == FE_OFDM) {
		dprintk(1, "%s() OFDM\n", __func__);
		switch (params->u.ofdm.bandwidth) {
		case BANDWIDTH_6_MHZ:
			priv->bandwidth = BANDWIDTH_6_MHZ;
			priv->video_standard = DTV6;
			priv->freq_hz = params->frequency - 1750000;
			break;
		case BANDWIDTH_7_MHZ:
			printk(KERN_ERR "xc4000 bandwidth 7MHz not supported\n");
			return -EINVAL;
		case BANDWIDTH_8_MHZ:
			priv->bandwidth = BANDWIDTH_8_MHZ;
			priv->video_standard = DTV8;
			priv->freq_hz = params->frequency - 2750000;
			break;
		default:
			printk(KERN_ERR "xc4000 bandwidth not set!\n");
			return -EINVAL;
		}
		priv->rf_mode = XC_RF_MODE_AIR;
	} else {
		printk(KERN_ERR "xc4000 modulation type not supported!\n");
		return -EINVAL;
	}

	dprintk(1, "%s() frequency=%d (compensated)\n",
		__func__, priv->freq_hz);

	ret = xc_SetSignalSource(priv, priv->rf_mode);
	if (ret != XC_RESULT_SUCCESS) {
		printk(KERN_ERR
			"xc4000: xc_SetSignalSource(%d) failed\n",
			priv->rf_mode);
		return -EREMOTEIO;
	}

	ret = xc_SetTVStandard(priv,
		XC4000_Standard[priv->video_standard].VideoMode,
		XC4000_Standard[priv->video_standard].AudioMode);
	if (ret != XC_RESULT_SUCCESS) {
		printk(KERN_ERR "xc4000: xc_SetTVStandard failed\n");
		return -EREMOTEIO;
	}
#ifdef DJH_DEBUG
	ret = xc_set_IF_frequency(priv, priv->if_khz);
	if (ret != XC_RESULT_SUCCESS) {
		printk(KERN_ERR "xc4000: xc_Set_IF_frequency(%d) failed\n",
		       priv->if_khz);
		return -EIO;
	}
#endif
	xc_tune_channel(priv, priv->freq_hz, XC_TUNE_DIGITAL);

	if (debug)
		xc_debug_dump(priv);

	return 0;
}

static int xc4000_is_firmware_loaded(struct dvb_frontend *fe)
{
	struct xc4000_priv *priv = fe->tuner_priv;
	int ret;
	u16 id;

	ret = xc4000_readreg(priv, XREG_PRODUCT_ID, &id);
	if (ret == XC_RESULT_SUCCESS) {
		if (id == XC_PRODUCT_ID_FW_NOT_LOADED)
			ret = XC_RESULT_RESET_FAILURE;
		else
			ret = XC_RESULT_SUCCESS;
	}

	dprintk(1, "%s() returns %s id = 0x%x\n", __func__,
		ret == XC_RESULT_SUCCESS ? "True" : "False", id);
	return ret;
}

static int xc4000_set_analog_params(struct dvb_frontend *fe,
	struct analog_parameters *params)
{
	struct xc4000_priv *priv = fe->tuner_priv;
	int ret;

	dprintk(1, "%s() frequency=%d (in units of 62.5khz)\n",
		__func__, params->frequency);

	/* FIXME: setup proper parameters */
	if (check_firmware(fe, DTV8, 0, priv->if_khz) != XC_RESULT_SUCCESS) {
		return -EREMOTEIO;
	}

	/* Fix me: it could be air. */
	priv->rf_mode = params->mode;
	if (params->mode > XC_RF_MODE_CABLE)
		priv->rf_mode = XC_RF_MODE_CABLE;

	/* params->frequency is in units of 62.5khz */
	priv->freq_hz = params->frequency * 62500;

	/* FIX ME: Some video standards may have several possible audio
		   standards. We simply default to one of them here.
	 */
	if (params->std & V4L2_STD_MN) {
		/* default to BTSC audio standard */
		priv->video_standard = MN_NTSC_PAL_BTSC;
		goto tune_channel;
	}

	if (params->std & V4L2_STD_PAL_BG) {
		/* default to NICAM audio standard */
		priv->video_standard = BG_PAL_NICAM;
		goto tune_channel;
	}

	if (params->std & V4L2_STD_PAL_I) {
		/* default to NICAM audio standard */
		priv->video_standard = I_PAL_NICAM;
		goto tune_channel;
	}

	if (params->std & V4L2_STD_PAL_DK) {
		/* default to NICAM audio standard */
		priv->video_standard = DK_PAL_NICAM;
		goto tune_channel;
	}

	if (params->std & V4L2_STD_SECAM_DK) {
		/* default to A2 DK1 audio standard */
		priv->video_standard = DK_SECAM_A2DK1;
		goto tune_channel;
	}

	if (params->std & V4L2_STD_SECAM_L) {
		priv->video_standard = L_SECAM_NICAM;
		goto tune_channel;
	}

	if (params->std & V4L2_STD_SECAM_LC) {
		priv->video_standard = LC_SECAM_NICAM;
		goto tune_channel;
	}

tune_channel:
	ret = xc_SetSignalSource(priv, priv->rf_mode);
	if (ret != XC_RESULT_SUCCESS) {
		printk(KERN_ERR
			"xc4000: xc_SetSignalSource(%d) failed\n",
			priv->rf_mode);
		return -EREMOTEIO;
	}

	ret = xc_SetTVStandard(priv,
		XC4000_Standard[priv->video_standard].VideoMode,
		XC4000_Standard[priv->video_standard].AudioMode);
	if (ret != XC_RESULT_SUCCESS) {
		printk(KERN_ERR "xc4000: xc_SetTVStandard failed\n");
		return -EREMOTEIO;
	}

	xc_tune_channel(priv, priv->freq_hz, XC_TUNE_ANALOG);

	if (debug)
		xc_debug_dump(priv);

	return 0;
}

static int xc4000_get_frequency(struct dvb_frontend *fe, u32 *freq)
{
	struct xc4000_priv *priv = fe->tuner_priv;
	dprintk(1, "%s()\n", __func__);
	*freq = priv->freq_hz;
	return 0;
}

static int xc4000_get_bandwidth(struct dvb_frontend *fe, u32 *bw)
{
	struct xc4000_priv *priv = fe->tuner_priv;
	dprintk(1, "%s()\n", __func__);

	*bw = priv->bandwidth;
	return 0;
}

static int xc4000_get_status(struct dvb_frontend *fe, u32 *status)
{
	struct xc4000_priv *priv = fe->tuner_priv;
	u16 lock_status = 0;

	xc_get_lock_status(priv, &lock_status);

	dprintk(1, "%s() lock_status = 0x%08x\n", __func__, lock_status);

	*status = lock_status;

	return 0;
}

static int xc4000_sleep(struct dvb_frontend *fe)
{
	/* FIXME: djh disable this for now... */
	return XC_RESULT_SUCCESS;
}

static int xc4000_init(struct dvb_frontend *fe)
{
	struct xc4000_priv *priv = fe->tuner_priv;
	dprintk(1, "%s()\n", __func__);

	if (check_firmware(fe, DTV8, 0, priv->if_khz) != XC_RESULT_SUCCESS) {
		printk(KERN_ERR "xc4000: Unable to initialise tuner\n");
		return -EREMOTEIO;
	}

	if (debug)
		xc_debug_dump(priv);

	return 0;
}

static int xc4000_release(struct dvb_frontend *fe)
{
	struct xc4000_priv *priv = fe->tuner_priv;

	dprintk(1, "%s()\n", __func__);

	mutex_lock(&xc4000_list_mutex);

	if (priv)
		hybrid_tuner_release_state(priv);

	mutex_unlock(&xc4000_list_mutex);

	fe->tuner_priv = NULL;

	return 0;
}

static const struct dvb_tuner_ops xc4000_tuner_ops = {
	.info = {
		.name           = "Xceive XC4000",
		.frequency_min  =    1000000,
		.frequency_max  = 1023000000,
		.frequency_step =      50000,
	},

	.release	   = xc4000_release,
	.init		   = xc4000_init,
	.sleep		   = xc4000_sleep,

	.set_params	   = xc4000_set_params,
	.set_analog_params = xc4000_set_analog_params,
	.get_frequency	   = xc4000_get_frequency,
	.get_bandwidth	   = xc4000_get_bandwidth,
	.get_status	   = xc4000_get_status
};

struct dvb_frontend *xc4000_attach(struct dvb_frontend *fe,
				   struct i2c_adapter *i2c,
				   struct xc4000_config *cfg)
{
	struct xc4000_priv *priv = NULL;
	int instance;
	u16 id = 0;

	dprintk(1, "%s(%d-%04x)\n", __func__,
		i2c ? i2c_adapter_id(i2c) : -1,
		cfg ? cfg->i2c_address : -1);

	mutex_lock(&xc4000_list_mutex);

	instance = hybrid_tuner_request_state(struct xc4000_priv, priv,
					      hybrid_tuner_instance_list,
					      i2c, cfg->i2c_address, "xc4000");
	switch (instance) {
	case 0:
		goto fail;
		break;
	case 1:
		/* new tuner instance */
		priv->bandwidth = BANDWIDTH_6_MHZ;
		fe->tuner_priv = priv;
		break;
	default:
		/* existing tuner instance */
		fe->tuner_priv = priv;
		break;
	}

	if (priv->if_khz == 0) {
		/* If the IF hasn't been set yet, use the value provided by
		   the caller (occurs in hybrid devices where the analog
		   call to xc4000_attach occurs before the digital side) */
		priv->if_khz = cfg->if_khz;
	}

	/* Check if firmware has been loaded. It is possible that another
	   instance of the driver has loaded the firmware.
	 */

	if (xc4000_readreg(priv, XREG_PRODUCT_ID, &id) != XC_RESULT_SUCCESS)
			goto fail;

	switch (id) {
	case XC_PRODUCT_ID_FW_LOADED:
		printk(KERN_INFO
			"xc4000: Successfully identified at address 0x%02x\n",
			cfg->i2c_address);
		printk(KERN_INFO
			"xc4000: Firmware has been loaded previously\n");
		break;
	case XC_PRODUCT_ID_FW_NOT_LOADED:
		printk(KERN_INFO
			"xc4000: Successfully identified at address 0x%02x\n",
			cfg->i2c_address);
		printk(KERN_INFO
			"xc4000: Firmware has not been loaded previously\n");
		break;
	default:
		printk(KERN_ERR
			"xc4000: Device not found at addr 0x%02x (0x%x)\n",
			cfg->i2c_address, id);
		goto fail;
	}

	mutex_unlock(&xc4000_list_mutex);

	memcpy(&fe->ops.tuner_ops, &xc4000_tuner_ops,
		sizeof(struct dvb_tuner_ops));

	/* FIXME: For now, load the firmware at startup.  We will remove this
	   before the code goes to production... */
	check_firmware(fe, DTV8, 0, priv->if_khz);

	return fe;
fail:
	mutex_unlock(&xc4000_list_mutex);

	xc4000_release(fe);
	return NULL;
}
EXPORT_SYMBOL(xc4000_attach);

MODULE_AUTHOR("Steven Toth, Davide Ferri");
MODULE_DESCRIPTION("Xceive xc4000 silicon tuner driver");
MODULE_LICENSE("GPL");
