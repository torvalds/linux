// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Driver for Xceive XC5000 "QAM/8VSB single chip tuner"
 *
 *  Copyright (c) 2007 Xceive Corporation
 *  Copyright (c) 2007 Steven Toth <stoth@linuxtv.org>
 *  Copyright (c) 2009 Devin Heitmueller <dheitmueller@kernellabs.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/videodev2.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/dvb/frontend.h>
#include <linux/i2c.h>

#include <media/dvb_frontend.h>

#include "xc5000.h"
#include "tuner-i2c.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off debugging (default:off).");

static int no_poweroff;
module_param(no_poweroff, int, 0644);
MODULE_PARM_DESC(no_poweroff, "0 (default) powers device off when not used.\n"
	"\t\t1 keep device energized and with tuner ready all the times.\n"
	"\t\tFaster, but consumes more power and keeps the device hotter");

static DEFINE_MUTEX(xc5000_list_mutex);
static LIST_HEAD(hybrid_tuner_instance_list);

#define dprintk(level, fmt, arg...) if (debug >= level) \
	printk(KERN_INFO "%s: " fmt, "xc5000", ## arg)

struct xc5000_priv {
	struct tuner_i2c_props i2c_props;
	struct list_head hybrid_tuner_instance_list;

	u32 if_khz;
	u16 xtal_khz;
	u32 freq_hz, freq_offset;
	u32 bandwidth;
	u8  video_standard;
	unsigned int mode;
	u8  rf_mode;
	u8  radio_input;
	u16  output_amp;

	int chip_id;
	u16 pll_register_no;
	u8 init_status_supported;
	u8 fw_checksum_supported;

	struct dvb_frontend *fe;
	struct delayed_work timer_sleep;

	bool inited;
};

/* Misc Defines */
#define MAX_TV_STANDARD			24
#define XC_MAX_I2C_WRITE_LENGTH		64

/* Time to suspend after the .sleep callback is called */
#define XC5000_SLEEP_TIME		5000 /* ms */

/* Signal Types */
#define XC_RF_MODE_AIR			0
#define XC_RF_MODE_CABLE		1

/* Product id */
#define XC_PRODUCT_ID_FW_NOT_LOADED	0x2000
#define XC_PRODUCT_ID_FW_LOADED	0x1388

/* Registers */
#define XREG_INIT         0x00
#define XREG_VIDEO_MODE   0x01
#define XREG_AUDIO_MODE   0x02
#define XREG_RF_FREQ      0x03
#define XREG_D_CODE       0x04
#define XREG_IF_OUT       0x05
#define XREG_SEEK_MODE    0x07
#define XREG_POWER_DOWN   0x0A /* Obsolete */
/* Set the output amplitude - SIF for analog, DTVP/DTVN for digital */
#define XREG_OUTPUT_AMP   0x0B
#define XREG_SIGNALSOURCE 0x0D /* 0=Air, 1=Cable */
#define XREG_SMOOTHEDCVBS 0x0E
#define XREG_XTALFREQ     0x0F
#define XREG_FINERFREQ    0x10
#define XREG_DDIMODE      0x11

#define XREG_ADC_ENV      0x00
#define XREG_QUALITY      0x01
#define XREG_FRAME_LINES  0x02
#define XREG_HSYNC_FREQ   0x03
#define XREG_LOCK         0x04
#define XREG_FREQ_ERROR   0x05
#define XREG_SNR          0x06
#define XREG_VERSION      0x07
#define XREG_PRODUCT_ID   0x08
#define XREG_BUSY         0x09
#define XREG_BUILD        0x0D
#define XREG_TOTALGAIN    0x0F
#define XREG_FW_CHECKSUM  0x12
#define XREG_INIT_STATUS  0x13

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
	char *name;
	u16 audio_mode;
	u16 video_mode;
};

/* Tuner standards */
#define MN_NTSC_PAL_BTSC	0
#define MN_NTSC_PAL_A2		1
#define MN_NTSC_PAL_EIAJ	2
#define MN_NTSC_PAL_MONO	3
#define BG_PAL_A2		4
#define BG_PAL_NICAM		5
#define BG_PAL_MONO		6
#define I_PAL_NICAM		7
#define I_PAL_NICAM_MONO	8
#define DK_PAL_A2		9
#define DK_PAL_NICAM		10
#define DK_PAL_MONO		11
#define DK_SECAM_A2DK1		12
#define DK_SECAM_A2LDK3		13
#define DK_SECAM_A2MONO		14
#define L_SECAM_NICAM		15
#define LC_SECAM_NICAM		16
#define DTV6			17
#define DTV8			18
#define DTV7_8			19
#define DTV7			20
#define FM_RADIO_INPUT2		21
#define FM_RADIO_INPUT1		22
#define FM_RADIO_INPUT1_MONO	23

static struct XC_TV_STANDARD xc5000_standard[MAX_TV_STANDARD] = {
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
	{"FM Radio-INPUT1",   0x0208, 0x9002},
	{"FM Radio-INPUT1_MONO", 0x0278, 0x9002}
};


struct xc5000_fw_cfg {
	char *name;
	u16 size;
	u16 pll_reg;
	u8 init_status_supported;
	u8 fw_checksum_supported;
};

#define XC5000A_FIRMWARE "dvb-fe-xc5000-1.6.114.fw"
static const struct xc5000_fw_cfg xc5000a_1_6_114 = {
	.name = XC5000A_FIRMWARE,
	.size = 12401,
	.pll_reg = 0x806c,
};

#define XC5000C_FIRMWARE "dvb-fe-xc5000c-4.1.30.7.fw"
static const struct xc5000_fw_cfg xc5000c_41_024_5 = {
	.name = XC5000C_FIRMWARE,
	.size = 16497,
	.pll_reg = 0x13,
	.init_status_supported = 1,
	.fw_checksum_supported = 1,
};

static inline const struct xc5000_fw_cfg *xc5000_assign_firmware(int chip_id)
{
	switch (chip_id) {
	default:
	case XC5000A:
		return &xc5000a_1_6_114;
	case XC5000C:
		return &xc5000c_41_024_5;
	}
}

static int xc_load_fw_and_init_tuner(struct dvb_frontend *fe, int force);
static int xc5000_is_firmware_loaded(struct dvb_frontend *fe);
static int xc5000_readreg(struct xc5000_priv *priv, u16 reg, u16 *val);
static int xc5000_tuner_reset(struct dvb_frontend *fe);

static int xc_send_i2c_data(struct xc5000_priv *priv, u8 *buf, int len)
{
	struct i2c_msg msg = { .addr = priv->i2c_props.addr,
			       .flags = 0, .buf = buf, .len = len };

	if (i2c_transfer(priv->i2c_props.adap, &msg, 1) != 1) {
		printk(KERN_ERR "xc5000: I2C write failed (len=%i)\n", len);
		return -EREMOTEIO;
	}
	return 0;
}

#if 0
/* This routine is never used because the only time we read data from the
   i2c bus is when we read registers, and we want that to be an atomic i2c
   transaction in case we are on a multi-master bus */
static int xc_read_i2c_data(struct xc5000_priv *priv, u8 *buf, int len)
{
	struct i2c_msg msg = { .addr = priv->i2c_props.addr,
		.flags = I2C_M_RD, .buf = buf, .len = len };

	if (i2c_transfer(priv->i2c_props.adap, &msg, 1) != 1) {
		printk(KERN_ERR "xc5000 I2C read failed (len=%i)\n", len);
		return -EREMOTEIO;
	}
	return 0;
}
#endif

static int xc5000_readreg(struct xc5000_priv *priv, u16 reg, u16 *val)
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
		printk(KERN_WARNING "xc5000: I2C read failed\n");
		return -EREMOTEIO;
	}

	*val = (bval[0] << 8) | bval[1];
	return 0;
}

static int xc5000_tuner_reset(struct dvb_frontend *fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	int ret;

	dprintk(1, "%s()\n", __func__);

	if (fe->callback) {
		ret = fe->callback(((fe->dvb) && (fe->dvb->priv)) ?
					   fe->dvb->priv :
					   priv->i2c_props.adap->algo_data,
					   DVB_FRONTEND_COMPONENT_TUNER,
					   XC5000_TUNER_RESET, 0);
		if (ret) {
			printk(KERN_ERR "xc5000: reset failed\n");
			return ret;
		}
	} else {
		printk(KERN_ERR "xc5000: no tuner reset callback function, fatal\n");
		return -EINVAL;
	}
	return 0;
}

static int xc_write_reg(struct xc5000_priv *priv, u16 reg_addr, u16 i2c_data)
{
	u8 buf[4];
	int watch_dog_timer = 100;
	int result;

	buf[0] = (reg_addr >> 8) & 0xFF;
	buf[1] = reg_addr & 0xFF;
	buf[2] = (i2c_data >> 8) & 0xFF;
	buf[3] = i2c_data & 0xFF;
	result = xc_send_i2c_data(priv, buf, 4);
	if (result == 0) {
		/* wait for busy flag to clear */
		while ((watch_dog_timer > 0) && (result == 0)) {
			result = xc5000_readreg(priv, XREG_BUSY, (u16 *)buf);
			if (result == 0) {
				if ((buf[0] == 0) && (buf[1] == 0)) {
					/* busy flag cleared */
					break;
				} else {
					msleep(5); /* wait 5 ms */
					watch_dog_timer--;
				}
			}
		}
	}
	if (watch_dog_timer <= 0)
		result = -EREMOTEIO;

	return result;
}

static int xc_load_i2c_sequence(struct dvb_frontend *fe, const u8 *i2c_sequence)
{
	struct xc5000_priv *priv = fe->tuner_priv;

	int i, nbytes_to_send, result;
	unsigned int len, pos, index;
	u8 buf[XC_MAX_I2C_WRITE_LENGTH];

	index = 0;
	while ((i2c_sequence[index] != 0xFF) ||
		(i2c_sequence[index + 1] != 0xFF)) {
		len = i2c_sequence[index] * 256 + i2c_sequence[index+1];
		if (len == 0x0000) {
			/* RESET command */
			result = xc5000_tuner_reset(fe);
			index += 2;
			if (result != 0)
				return result;
		} else if (len & 0x8000) {
			/* WAIT command */
			msleep(len & 0x7FFF);
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

				if (result != 0)
					return result;

				pos += nbytes_to_send - 2;
			}
			index += len;
		}
	}
	return 0;
}

static int xc_initialize(struct xc5000_priv *priv)
{
	dprintk(1, "%s()\n", __func__);
	return xc_write_reg(priv, XREG_INIT, 0);
}

static int xc_set_tv_standard(struct xc5000_priv *priv,
	u16 video_mode, u16 audio_mode, u8 radio_mode)
{
	int ret;
	dprintk(1, "%s(0x%04x,0x%04x)\n", __func__, video_mode, audio_mode);
	if (radio_mode) {
		dprintk(1, "%s() Standard = %s\n",
			__func__,
			xc5000_standard[radio_mode].name);
	} else {
		dprintk(1, "%s() Standard = %s\n",
			__func__,
			xc5000_standard[priv->video_standard].name);
	}

	ret = xc_write_reg(priv, XREG_VIDEO_MODE, video_mode);
	if (ret == 0)
		ret = xc_write_reg(priv, XREG_AUDIO_MODE, audio_mode);

	return ret;
}

static int xc_set_signal_source(struct xc5000_priv *priv, u16 rf_mode)
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

static const struct dvb_tuner_ops xc5000_tuner_ops;

static int xc_set_rf_frequency(struct xc5000_priv *priv, u32 freq_hz)
{
	u16 freq_code;

	dprintk(1, "%s(%u)\n", __func__, freq_hz);

	if ((freq_hz > xc5000_tuner_ops.info.frequency_max_hz) ||
		(freq_hz < xc5000_tuner_ops.info.frequency_min_hz))
		return -EINVAL;

	freq_code = (u16)(freq_hz / 15625);

	/* Starting in firmware version 1.1.44, Xceive recommends using the
	   FINERFREQ for all normal tuning (the doc indicates reg 0x03 should
	   only be used for fast scanning for channel lock) */
	return xc_write_reg(priv, XREG_FINERFREQ, freq_code);
}


static int xc_set_IF_frequency(struct xc5000_priv *priv, u32 freq_khz)
{
	u32 freq_code = (freq_khz * 1024)/1000;
	dprintk(1, "%s(freq_khz = %d) freq_code = 0x%x\n",
		__func__, freq_khz, freq_code);

	return xc_write_reg(priv, XREG_IF_OUT, freq_code);
}


static int xc_get_adc_envelope(struct xc5000_priv *priv, u16 *adc_envelope)
{
	return xc5000_readreg(priv, XREG_ADC_ENV, adc_envelope);
}

static int xc_get_frequency_error(struct xc5000_priv *priv, u32 *freq_error_hz)
{
	int result;
	u16 reg_data;
	u32 tmp;

	result = xc5000_readreg(priv, XREG_FREQ_ERROR, &reg_data);
	if (result != 0)
		return result;

	tmp = (u32)reg_data;
	(*freq_error_hz) = (tmp * 15625) / 1000;
	return result;
}

static int xc_get_lock_status(struct xc5000_priv *priv, u16 *lock_status)
{
	return xc5000_readreg(priv, XREG_LOCK, lock_status);
}

static int xc_get_version(struct xc5000_priv *priv,
	u8 *hw_majorversion, u8 *hw_minorversion,
	u8 *fw_majorversion, u8 *fw_minorversion)
{
	u16 data;
	int result;

	result = xc5000_readreg(priv, XREG_VERSION, &data);
	if (result != 0)
		return result;

	(*hw_majorversion) = (data >> 12) & 0x0F;
	(*hw_minorversion) = (data >>  8) & 0x0F;
	(*fw_majorversion) = (data >>  4) & 0x0F;
	(*fw_minorversion) = data & 0x0F;

	return 0;
}

static int xc_get_buildversion(struct xc5000_priv *priv, u16 *buildrev)
{
	return xc5000_readreg(priv, XREG_BUILD, buildrev);
}

static int xc_get_hsync_freq(struct xc5000_priv *priv, u32 *hsync_freq_hz)
{
	u16 reg_data;
	int result;

	result = xc5000_readreg(priv, XREG_HSYNC_FREQ, &reg_data);
	if (result != 0)
		return result;

	(*hsync_freq_hz) = ((reg_data & 0x0fff) * 763)/100;
	return result;
}

static int xc_get_frame_lines(struct xc5000_priv *priv, u16 *frame_lines)
{
	return xc5000_readreg(priv, XREG_FRAME_LINES, frame_lines);
}

static int xc_get_quality(struct xc5000_priv *priv, u16 *quality)
{
	return xc5000_readreg(priv, XREG_QUALITY, quality);
}

static int xc_get_analogsnr(struct xc5000_priv *priv, u16 *snr)
{
	return xc5000_readreg(priv, XREG_SNR, snr);
}

static int xc_get_totalgain(struct xc5000_priv *priv, u16 *totalgain)
{
	return xc5000_readreg(priv, XREG_TOTALGAIN, totalgain);
}

#define XC_TUNE_ANALOG  0
#define XC_TUNE_DIGITAL 1
static int xc_tune_channel(struct xc5000_priv *priv, u32 freq_hz, int mode)
{
	dprintk(1, "%s(%u)\n", __func__, freq_hz);

	if (xc_set_rf_frequency(priv, freq_hz) != 0)
		return -EREMOTEIO;

	return 0;
}

static int xc_set_xtal(struct dvb_frontend *fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	int ret = 0;

	switch (priv->chip_id) {
	default:
	case XC5000A:
		/* 32.000 MHz xtal is default */
		break;
	case XC5000C:
		switch (priv->xtal_khz) {
		default:
		case 32000:
			/* 32.000 MHz xtal is default */
			break;
		case 31875:
			/* 31.875 MHz xtal configuration */
			ret = xc_write_reg(priv, 0x000f, 0x8081);
			break;
		}
		break;
	}
	return ret;
}

static int xc5000_fwupload(struct dvb_frontend *fe,
			   const struct xc5000_fw_cfg *desired_fw,
			   const struct firmware *fw)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	int ret;

	/* request the firmware, this will block and timeout */
	dprintk(1, "waiting for firmware upload (%s)...\n",
		desired_fw->name);

	priv->pll_register_no = desired_fw->pll_reg;
	priv->init_status_supported = desired_fw->init_status_supported;
	priv->fw_checksum_supported = desired_fw->fw_checksum_supported;


	dprintk(1, "firmware uploading...\n");
	ret = xc_load_i2c_sequence(fe,  fw->data);
	if (!ret) {
		ret = xc_set_xtal(fe);
		dprintk(1, "Firmware upload complete...\n");
	} else
		printk(KERN_ERR "xc5000: firmware upload failed...\n");

	return ret;
}

static void xc_debug_dump(struct xc5000_priv *priv)
{
	u16 adc_envelope = 0;
	u32 freq_error_hz = 0;
	u16 lock_status = 0;
	u32 hsync_freq_hz = 0;
	u16 frame_lines = 0;
	u16 quality = 0;
	u16 snr = 0;
	u16 totalgain = 0;
	u8 hw_majorversion = 0, hw_minorversion = 0;
	u8 fw_majorversion = 0, fw_minorversion = 0;
	u16 fw_buildversion = 0;
	u16 regval;

	/* Wait for stats to stabilize.
	 * Frame Lines needs two frame times after initial lock
	 * before it is valid.
	 */
	msleep(100);

	xc_get_adc_envelope(priv,  &adc_envelope);
	dprintk(1, "*** ADC envelope (0-1023) = %d\n", adc_envelope);

	xc_get_frequency_error(priv, &freq_error_hz);
	dprintk(1, "*** Frequency error = %d Hz\n", freq_error_hz);

	xc_get_lock_status(priv,  &lock_status);
	dprintk(1, "*** Lock status (0-Wait, 1-Locked, 2-No-signal) = %d\n",
		lock_status);

	xc_get_version(priv,  &hw_majorversion, &hw_minorversion,
		&fw_majorversion, &fw_minorversion);
	xc_get_buildversion(priv,  &fw_buildversion);
	dprintk(1, "*** HW: V%d.%d, FW: V %d.%d.%d\n",
		hw_majorversion, hw_minorversion,
		fw_majorversion, fw_minorversion, fw_buildversion);

	xc_get_hsync_freq(priv,  &hsync_freq_hz);
	dprintk(1, "*** Horizontal sync frequency = %d Hz\n", hsync_freq_hz);

	xc_get_frame_lines(priv,  &frame_lines);
	dprintk(1, "*** Frame lines = %d\n", frame_lines);

	xc_get_quality(priv,  &quality);
	dprintk(1, "*** Quality (0:<8dB, 7:>56dB) = %d\n", quality & 0x07);

	xc_get_analogsnr(priv,  &snr);
	dprintk(1, "*** Unweighted analog SNR = %d dB\n", snr & 0x3f);

	xc_get_totalgain(priv,  &totalgain);
	dprintk(1, "*** Total gain = %d.%d dB\n", totalgain / 256,
		(totalgain % 256) * 100 / 256);

	if (priv->pll_register_no) {
		if (!xc5000_readreg(priv, priv->pll_register_no, &regval))
			dprintk(1, "*** PLL lock status = 0x%04x\n", regval);
	}
}

static int xc5000_tune_digital(struct dvb_frontend *fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	int ret;
	u32 bw = fe->dtv_property_cache.bandwidth_hz;

	ret = xc_set_signal_source(priv, priv->rf_mode);
	if (ret != 0) {
		printk(KERN_ERR
			"xc5000: xc_set_signal_source(%d) failed\n",
			priv->rf_mode);
		return -EREMOTEIO;
	}

	ret = xc_set_tv_standard(priv,
		xc5000_standard[priv->video_standard].video_mode,
		xc5000_standard[priv->video_standard].audio_mode, 0);
	if (ret != 0) {
		printk(KERN_ERR "xc5000: xc_set_tv_standard failed\n");
		return -EREMOTEIO;
	}

	ret = xc_set_IF_frequency(priv, priv->if_khz);
	if (ret != 0) {
		printk(KERN_ERR "xc5000: xc_Set_IF_frequency(%d) failed\n",
		       priv->if_khz);
		return -EIO;
	}

	dprintk(1, "%s() setting OUTPUT_AMP to 0x%x\n",
		__func__, priv->output_amp);
	xc_write_reg(priv, XREG_OUTPUT_AMP, priv->output_amp);

	xc_tune_channel(priv, priv->freq_hz, XC_TUNE_DIGITAL);

	if (debug)
		xc_debug_dump(priv);

	priv->bandwidth = bw;

	return 0;
}

static int xc5000_set_digital_params(struct dvb_frontend *fe)
{
	int b;
	struct xc5000_priv *priv = fe->tuner_priv;
	u32 bw = fe->dtv_property_cache.bandwidth_hz;
	u32 freq = fe->dtv_property_cache.frequency;
	u32 delsys  = fe->dtv_property_cache.delivery_system;

	if (xc_load_fw_and_init_tuner(fe, 0) != 0) {
		dprintk(1, "Unable to load firmware and init tuner\n");
		return -EINVAL;
	}

	dprintk(1, "%s() frequency=%d (Hz)\n", __func__, freq);

	switch (delsys) {
	case SYS_ATSC:
		dprintk(1, "%s() VSB modulation\n", __func__);
		priv->rf_mode = XC_RF_MODE_AIR;
		priv->freq_offset = 1750000;
		priv->video_standard = DTV6;
		break;
	case SYS_DVBC_ANNEX_B:
		dprintk(1, "%s() QAM modulation\n", __func__);
		priv->rf_mode = XC_RF_MODE_CABLE;
		priv->freq_offset = 1750000;
		priv->video_standard = DTV6;
		break;
	case SYS_ISDBT:
		/* All ISDB-T are currently for 6 MHz bw */
		if (!bw)
			bw = 6000000;
		/* fall to OFDM handling */
		fallthrough;
	case SYS_DMBTH:
	case SYS_DVBT:
	case SYS_DVBT2:
		dprintk(1, "%s() OFDM\n", __func__);
		switch (bw) {
		case 6000000:
			priv->video_standard = DTV6;
			priv->freq_offset = 1750000;
			break;
		case 7000000:
			priv->video_standard = DTV7;
			priv->freq_offset = 2250000;
			break;
		case 8000000:
			priv->video_standard = DTV8;
			priv->freq_offset = 2750000;
			break;
		default:
			printk(KERN_ERR "xc5000 bandwidth not set!\n");
			return -EINVAL;
		}
		priv->rf_mode = XC_RF_MODE_AIR;
		break;
	case SYS_DVBC_ANNEX_A:
	case SYS_DVBC_ANNEX_C:
		dprintk(1, "%s() QAM modulation\n", __func__);
		priv->rf_mode = XC_RF_MODE_CABLE;
		if (bw <= 6000000) {
			priv->video_standard = DTV6;
			priv->freq_offset = 1750000;
			b = 6;
		} else if (bw <= 7000000) {
			priv->video_standard = DTV7;
			priv->freq_offset = 2250000;
			b = 7;
		} else {
			priv->video_standard = DTV7_8;
			priv->freq_offset = 2750000;
			b = 8;
		}
		dprintk(1, "%s() Bandwidth %dMHz (%d)\n", __func__,
			b, bw);
		break;
	default:
		printk(KERN_ERR "xc5000: delivery system is not supported!\n");
		return -EINVAL;
	}

	priv->freq_hz = freq - priv->freq_offset;
	priv->mode = V4L2_TUNER_DIGITAL_TV;

	dprintk(1, "%s() frequency=%d (compensated to %d)\n",
		__func__, freq, priv->freq_hz);

	return xc5000_tune_digital(fe);
}

static int xc5000_is_firmware_loaded(struct dvb_frontend *fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	int ret;
	u16 id;

	ret = xc5000_readreg(priv, XREG_PRODUCT_ID, &id);
	if (!ret) {
		if (id == XC_PRODUCT_ID_FW_NOT_LOADED)
			ret = -ENOENT;
		else
			ret = 0;
		dprintk(1, "%s() returns id = 0x%x\n", __func__, id);
	} else {
		dprintk(1, "%s() returns error %d\n", __func__, ret);
	}

	return ret;
}

static void xc5000_config_tv(struct dvb_frontend *fe,
			     struct analog_parameters *params)
{
	struct xc5000_priv *priv = fe->tuner_priv;

	dprintk(1, "%s() frequency=%d (in units of 62.5khz)\n",
		__func__, params->frequency);

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
		return;
	}

	if (params->std & V4L2_STD_PAL_BG) {
		/* default to NICAM audio standard */
		priv->video_standard = BG_PAL_NICAM;
		return;
	}

	if (params->std & V4L2_STD_PAL_I) {
		/* default to NICAM audio standard */
		priv->video_standard = I_PAL_NICAM;
		return;
	}

	if (params->std & V4L2_STD_PAL_DK) {
		/* default to NICAM audio standard */
		priv->video_standard = DK_PAL_NICAM;
		return;
	}

	if (params->std & V4L2_STD_SECAM_DK) {
		/* default to A2 DK1 audio standard */
		priv->video_standard = DK_SECAM_A2DK1;
		return;
	}

	if (params->std & V4L2_STD_SECAM_L) {
		priv->video_standard = L_SECAM_NICAM;
		return;
	}

	if (params->std & V4L2_STD_SECAM_LC) {
		priv->video_standard = LC_SECAM_NICAM;
		return;
	}
}

static int xc5000_set_tv_freq(struct dvb_frontend *fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	u16 pll_lock_status;
	int ret;

tune_channel:
	ret = xc_set_signal_source(priv, priv->rf_mode);
	if (ret != 0) {
		printk(KERN_ERR
			"xc5000: xc_set_signal_source(%d) failed\n",
			priv->rf_mode);
		return -EREMOTEIO;
	}

	ret = xc_set_tv_standard(priv,
		xc5000_standard[priv->video_standard].video_mode,
		xc5000_standard[priv->video_standard].audio_mode, 0);
	if (ret != 0) {
		printk(KERN_ERR "xc5000: xc_set_tv_standard failed\n");
		return -EREMOTEIO;
	}

	xc_write_reg(priv, XREG_OUTPUT_AMP, 0x09);

	xc_tune_channel(priv, priv->freq_hz, XC_TUNE_ANALOG);

	if (debug)
		xc_debug_dump(priv);

	if (priv->pll_register_no != 0) {
		msleep(20);
		ret = xc5000_readreg(priv, priv->pll_register_no,
				     &pll_lock_status);
		if (ret)
			return ret;
		if (pll_lock_status > 63) {
			/* PLL is unlocked, force reload of the firmware */
			dprintk(1, "xc5000: PLL not locked (0x%x).  Reloading...\n",
				pll_lock_status);
			if (xc_load_fw_and_init_tuner(fe, 1) != 0) {
				printk(KERN_ERR "xc5000: Unable to reload fw\n");
				return -EREMOTEIO;
			}
			goto tune_channel;
		}
	}

	return 0;
}

static int xc5000_config_radio(struct dvb_frontend *fe,
			       struct analog_parameters *params)

{
	struct xc5000_priv *priv = fe->tuner_priv;

	dprintk(1, "%s() frequency=%d (in units of khz)\n",
		__func__, params->frequency);

	if (priv->radio_input == XC5000_RADIO_NOT_CONFIGURED) {
		dprintk(1, "%s() radio input not configured\n", __func__);
		return -EINVAL;
	}

	priv->freq_hz = params->frequency * 125 / 2;
	priv->rf_mode = XC_RF_MODE_AIR;

	return 0;
}

static int xc5000_set_radio_freq(struct dvb_frontend *fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	int ret;
	u8 radio_input;

	if (priv->radio_input == XC5000_RADIO_FM1)
		radio_input = FM_RADIO_INPUT1;
	else if  (priv->radio_input == XC5000_RADIO_FM2)
		radio_input = FM_RADIO_INPUT2;
	else if  (priv->radio_input == XC5000_RADIO_FM1_MONO)
		radio_input = FM_RADIO_INPUT1_MONO;
	else {
		dprintk(1, "%s() unknown radio input %d\n", __func__,
			priv->radio_input);
		return -EINVAL;
	}

	ret = xc_set_tv_standard(priv, xc5000_standard[radio_input].video_mode,
			       xc5000_standard[radio_input].audio_mode, radio_input);

	if (ret != 0) {
		printk(KERN_ERR "xc5000: xc_set_tv_standard failed\n");
		return -EREMOTEIO;
	}

	ret = xc_set_signal_source(priv, priv->rf_mode);
	if (ret != 0) {
		printk(KERN_ERR
			"xc5000: xc_set_signal_source(%d) failed\n",
			priv->rf_mode);
		return -EREMOTEIO;
	}

	if ((priv->radio_input == XC5000_RADIO_FM1) ||
				(priv->radio_input == XC5000_RADIO_FM2))
		xc_write_reg(priv, XREG_OUTPUT_AMP, 0x09);
	else if  (priv->radio_input == XC5000_RADIO_FM1_MONO)
		xc_write_reg(priv, XREG_OUTPUT_AMP, 0x06);

	xc_tune_channel(priv, priv->freq_hz, XC_TUNE_ANALOG);

	return 0;
}

static int xc5000_set_params(struct dvb_frontend *fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;

	if (xc_load_fw_and_init_tuner(fe, 0) != 0) {
		dprintk(1, "Unable to load firmware and init tuner\n");
		return -EINVAL;
	}

	switch (priv->mode) {
	case V4L2_TUNER_RADIO:
		return xc5000_set_radio_freq(fe);
	case V4L2_TUNER_ANALOG_TV:
		return xc5000_set_tv_freq(fe);
	case V4L2_TUNER_DIGITAL_TV:
		return xc5000_tune_digital(fe);
	}

	return 0;
}

static int xc5000_set_analog_params(struct dvb_frontend *fe,
			     struct analog_parameters *params)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	int ret;

	if (priv->i2c_props.adap == NULL)
		return -EINVAL;

	switch (params->mode) {
	case V4L2_TUNER_RADIO:
		ret = xc5000_config_radio(fe, params);
		if (ret)
			return ret;
		break;
	case V4L2_TUNER_ANALOG_TV:
		xc5000_config_tv(fe, params);
		break;
	default:
		break;
	}
	priv->mode = params->mode;

	return xc5000_set_params(fe);
}

static int xc5000_get_frequency(struct dvb_frontend *fe, u32 *freq)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	dprintk(1, "%s()\n", __func__);
	*freq = priv->freq_hz + priv->freq_offset;
	return 0;
}

static int xc5000_get_if_frequency(struct dvb_frontend *fe, u32 *freq)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	dprintk(1, "%s()\n", __func__);
	*freq = priv->if_khz * 1000;
	return 0;
}

static int xc5000_get_bandwidth(struct dvb_frontend *fe, u32 *bw)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	dprintk(1, "%s()\n", __func__);

	*bw = priv->bandwidth;
	return 0;
}

static int xc5000_get_status(struct dvb_frontend *fe, u32 *status)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	u16 lock_status = 0;

	xc_get_lock_status(priv, &lock_status);

	dprintk(1, "%s() lock_status = 0x%08x\n", __func__, lock_status);

	*status = lock_status;

	return 0;
}

static int xc_load_fw_and_init_tuner(struct dvb_frontend *fe, int force)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	const struct xc5000_fw_cfg *desired_fw = xc5000_assign_firmware(priv->chip_id);
	const struct firmware *fw;
	int ret, i;
	u16 pll_lock_status;
	u16 fw_ck;

	cancel_delayed_work(&priv->timer_sleep);

	if (!force && xc5000_is_firmware_loaded(fe) == 0)
		return 0;

	ret = request_firmware(&fw, desired_fw->name,
			       priv->i2c_props.adap->dev.parent);
	if (ret) {
		pr_err("xc5000: Upload failed. rc %d\n", ret);
		return ret;
	}
	dprintk(1, "firmware read %zu bytes.\n", fw->size);

	if (fw->size != desired_fw->size) {
		pr_err("xc5000: Firmware file with incorrect size\n");
		release_firmware(fw);
		return -EINVAL;
	}

	/* Try up to 5 times to load firmware */
	for (i = 0; i < 5; i++) {
		if (i)
			printk(KERN_CONT " - retrying to upload firmware.\n");

		ret = xc5000_fwupload(fe, desired_fw, fw);
		if (ret != 0)
			goto err;

		msleep(20);

		if (priv->fw_checksum_supported) {
			if (xc5000_readreg(priv, XREG_FW_CHECKSUM, &fw_ck)) {
				printk(KERN_ERR
				       "xc5000: FW checksum reading failed.");
				continue;
			}

			if (!fw_ck) {
				printk(KERN_ERR
				       "xc5000: FW checksum failed = 0x%04x.",
				       fw_ck);
				continue;
			}
		}

		/* Start the tuner self-calibration process */
		ret = xc_initialize(priv);
		if (ret) {
			printk(KERN_ERR "xc5000: Can't request self-calibration.");
			continue;
		}

		/* Wait for calibration to complete.
		 * We could continue but XC5000 will clock stretch subsequent
		 * I2C transactions until calibration is complete.  This way we
		 * don't have to rely on clock stretching working.
		 */
		msleep(100);

		if (priv->init_status_supported) {
			if (xc5000_readreg(priv, XREG_INIT_STATUS, &fw_ck)) {
				printk(KERN_ERR
				       "xc5000: FW failed reading init status.");
				continue;
			}

			if (!fw_ck) {
				printk(KERN_ERR
				       "xc5000: FW init status failed = 0x%04x.",
				       fw_ck);
				continue;
			}
		}

		if (priv->pll_register_no) {
			ret = xc5000_readreg(priv, priv->pll_register_no,
					     &pll_lock_status);
			if (ret)
				continue;
			if (pll_lock_status > 63) {
				/* PLL is unlocked, force reload of the firmware */
				printk(KERN_ERR
				       "xc5000: PLL not running after fwload.");
				continue;
			}
		}

		/* Default to "CABLE" mode */
		ret = xc_write_reg(priv, XREG_SIGNALSOURCE, XC_RF_MODE_CABLE);
		if (!ret)
			break;
		printk(KERN_ERR "xc5000: can't set to cable mode.");
	}

err:
	release_firmware(fw);
	if (!ret)
		printk(KERN_INFO "xc5000: Firmware %s loaded and running.\n",
		       desired_fw->name);
	else
		printk(KERN_CONT " - too many retries. Giving up\n");

	return ret;
}

static void xc5000_do_timer_sleep(struct work_struct *timer_sleep)
{
	struct xc5000_priv *priv =container_of(timer_sleep, struct xc5000_priv,
					       timer_sleep.work);
	struct dvb_frontend *fe = priv->fe;
	int ret;

	dprintk(1, "%s()\n", __func__);

	/* According to Xceive technical support, the "powerdown" register
	   was removed in newer versions of the firmware.  The "supported"
	   way to sleep the tuner is to pull the reset pin low for 10ms */
	ret = xc5000_tuner_reset(fe);
	if (ret != 0)
		printk(KERN_ERR
			"xc5000: %s() unable to shutdown tuner\n",
			__func__);
}

static int xc5000_sleep(struct dvb_frontend *fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;

	dprintk(1, "%s()\n", __func__);

	/* Avoid firmware reload on slow devices */
	if (no_poweroff)
		return 0;

	schedule_delayed_work(&priv->timer_sleep,
			      msecs_to_jiffies(XC5000_SLEEP_TIME));

	return 0;
}

static int xc5000_suspend(struct dvb_frontend *fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	int ret;

	dprintk(1, "%s()\n", __func__);

	cancel_delayed_work(&priv->timer_sleep);

	ret = xc5000_tuner_reset(fe);
	if (ret != 0)
		printk(KERN_ERR
			"xc5000: %s() unable to shutdown tuner\n",
			__func__);

	return 0;
}

static int xc5000_resume(struct dvb_frontend *fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;

	dprintk(1, "%s()\n", __func__);

	/* suspended before firmware is loaded.
	   Avoid firmware load in resume path. */
	if (!priv->inited)
		return 0;

	return xc5000_set_params(fe);
}

static int xc5000_init(struct dvb_frontend *fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	dprintk(1, "%s()\n", __func__);

	if (xc_load_fw_and_init_tuner(fe, 0) != 0) {
		printk(KERN_ERR "xc5000: Unable to initialise tuner\n");
		return -EREMOTEIO;
	}

	if (debug)
		xc_debug_dump(priv);

	priv->inited = true;

	return 0;
}

static void xc5000_release(struct dvb_frontend *fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;

	dprintk(1, "%s()\n", __func__);

	mutex_lock(&xc5000_list_mutex);

	if (priv) {
		cancel_delayed_work_sync(&priv->timer_sleep);
		hybrid_tuner_release_state(priv);
	}

	mutex_unlock(&xc5000_list_mutex);

	fe->tuner_priv = NULL;
}

static int xc5000_set_config(struct dvb_frontend *fe, void *priv_cfg)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	struct xc5000_config *p = priv_cfg;

	dprintk(1, "%s()\n", __func__);

	if (p->if_khz)
		priv->if_khz = p->if_khz;

	if (p->radio_input)
		priv->radio_input = p->radio_input;

	if (p->output_amp)
		priv->output_amp = p->output_amp;

	return 0;
}


static const struct dvb_tuner_ops xc5000_tuner_ops = {
	.info = {
		.name              = "Xceive XC5000",
		.frequency_min_hz  =    1 * MHz,
		.frequency_max_hz  = 1023 * MHz,
		.frequency_step_hz =   50 * kHz,
	},

	.release	   = xc5000_release,
	.init		   = xc5000_init,
	.sleep		   = xc5000_sleep,
	.suspend	   = xc5000_suspend,
	.resume		   = xc5000_resume,

	.set_config	   = xc5000_set_config,
	.set_params	   = xc5000_set_digital_params,
	.set_analog_params = xc5000_set_analog_params,
	.get_frequency	   = xc5000_get_frequency,
	.get_if_frequency  = xc5000_get_if_frequency,
	.get_bandwidth	   = xc5000_get_bandwidth,
	.get_status	   = xc5000_get_status
};

struct dvb_frontend *xc5000_attach(struct dvb_frontend *fe,
				   struct i2c_adapter *i2c,
				   const struct xc5000_config *cfg)
{
	struct xc5000_priv *priv = NULL;
	int instance;
	u16 id = 0;

	dprintk(1, "%s(%d-%04x)\n", __func__,
		i2c ? i2c_adapter_id(i2c) : -1,
		cfg ? cfg->i2c_address : -1);

	mutex_lock(&xc5000_list_mutex);

	instance = hybrid_tuner_request_state(struct xc5000_priv, priv,
					      hybrid_tuner_instance_list,
					      i2c, cfg->i2c_address, "xc5000");
	switch (instance) {
	case 0:
		goto fail;
	case 1:
		/* new tuner instance */
		priv->bandwidth = 6000000;
		fe->tuner_priv = priv;
		priv->fe = fe;
		INIT_DELAYED_WORK(&priv->timer_sleep, xc5000_do_timer_sleep);
		break;
	default:
		/* existing tuner instance */
		fe->tuner_priv = priv;
		break;
	}

	if (priv->if_khz == 0) {
		/* If the IF hasn't been set yet, use the value provided by
		   the caller (occurs in hybrid devices where the analog
		   call to xc5000_attach occurs before the digital side) */
		priv->if_khz = cfg->if_khz;
	}

	if (priv->xtal_khz == 0)
		priv->xtal_khz = cfg->xtal_khz;

	if (priv->radio_input == 0)
		priv->radio_input = cfg->radio_input;

	/* don't override chip id if it's already been set
	   unless explicitly specified */
	if ((priv->chip_id == 0) || (cfg->chip_id))
		/* use default chip id if none specified, set to 0 so
		   it can be overridden if this is a hybrid driver */
		priv->chip_id = (cfg->chip_id) ? cfg->chip_id : 0;

	/* don't override output_amp if it's already been set
	   unless explicitly specified */
	if ((priv->output_amp == 0) || (cfg->output_amp))
		/* use default output_amp value if none specified */
		priv->output_amp = (cfg->output_amp) ? cfg->output_amp : 0x8a;

	/* Check if firmware has been loaded. It is possible that another
	   instance of the driver has loaded the firmware.
	 */
	if (xc5000_readreg(priv, XREG_PRODUCT_ID, &id) != 0)
		goto fail;

	switch (id) {
	case XC_PRODUCT_ID_FW_LOADED:
		printk(KERN_INFO
			"xc5000: Successfully identified at address 0x%02x\n",
			cfg->i2c_address);
		printk(KERN_INFO
			"xc5000: Firmware has been loaded previously\n");
		break;
	case XC_PRODUCT_ID_FW_NOT_LOADED:
		printk(KERN_INFO
			"xc5000: Successfully identified at address 0x%02x\n",
			cfg->i2c_address);
		printk(KERN_INFO
			"xc5000: Firmware has not been loaded previously\n");
		break;
	default:
		printk(KERN_ERR
			"xc5000: Device not found at addr 0x%02x (0x%x)\n",
			cfg->i2c_address, id);
		goto fail;
	}

	mutex_unlock(&xc5000_list_mutex);

	memcpy(&fe->ops.tuner_ops, &xc5000_tuner_ops,
		sizeof(struct dvb_tuner_ops));

	return fe;
fail:
	mutex_unlock(&xc5000_list_mutex);

	xc5000_release(fe);
	return NULL;
}
EXPORT_SYMBOL_GPL(xc5000_attach);

MODULE_AUTHOR("Steven Toth");
MODULE_DESCRIPTION("Xceive xc5000 silicon tuner driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(XC5000A_FIRMWARE);
MODULE_FIRMWARE(XC5000C_FIRMWARE);
