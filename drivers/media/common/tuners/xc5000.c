/*
 *  Driver for Xceive XC5000 "QAM/8VSB single chip tuner"
 *
 *  Copyright (c) 2007 Xceive Corporation
 *  Copyright (c) 2007 Steven Toth <stoth@linuxtv.org>
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

#include "dvb_frontend.h"

#include "xc5000.h"
#include "tuner-i2c.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off debugging (default:off).");

static DEFINE_MUTEX(xc5000_list_mutex);
static LIST_HEAD(hybrid_tuner_instance_list);

#define dprintk(level, fmt, arg...) if (debug >= level) \
	printk(KERN_INFO "%s: " fmt, "xc5000", ## arg)

#define XC5000_DEFAULT_FIRMWARE "dvb-fe-xc5000-1.1.fw"
#define XC5000_DEFAULT_FIRMWARE_SIZE 12332

struct xc5000_priv {
	struct tuner_i2c_props i2c_props;
	struct list_head hybrid_tuner_instance_list;

	u32 if_khz;
	u32 freq_hz;
	u32 bandwidth;
	u8  video_standard;
	u8  rf_mode;
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
#define XC_PRODUCT_ID_FW_LOADED 	0x1388

/* Registers */
#define XREG_INIT         0x00
#define XREG_VIDEO_MODE   0x01
#define XREG_AUDIO_MODE   0x02
#define XREG_RF_FREQ      0x03
#define XREG_D_CODE       0x04
#define XREG_IF_OUT       0x05
#define XREG_SEEK_MODE    0x07
#define XREG_POWER_DOWN   0x0A
#define XREG_SIGNALSOURCE 0x0D /* 0=Air, 1=Cable */
#define XREG_SMOOTHEDCVBS 0x0E
#define XREG_XTALFREQ     0x0F
#define XREG_FINERFFREQ   0x10
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
#define DTV6			17
#define DTV8			18
#define DTV7_8			19
#define DTV7			20
#define FM_Radio_INPUT2 	21
#define FM_Radio_INPUT1 	22

static struct XC_TV_STANDARD XC5000_Standard[MAX_TV_STANDARD] = {
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
};

static int  xc5000_is_firmware_loaded(struct dvb_frontend *fe);
static int  xc5000_writeregs(struct xc5000_priv *priv, u8 *buf, u8 len);
static int  xc5000_readregs(struct xc5000_priv *priv, u8 *buf, u8 len);
static void xc5000_TunerReset(struct dvb_frontend *fe);

static int xc_send_i2c_data(struct xc5000_priv *priv, u8 *buf, int len)
{
	return xc5000_writeregs(priv, buf, len)
		? XC_RESULT_I2C_WRITE_FAILURE : XC_RESULT_SUCCESS;
}

static int xc_read_i2c_data(struct xc5000_priv *priv, u8 *buf, int len)
{
	return xc5000_readregs(priv, buf, len)
		? XC_RESULT_I2C_READ_FAILURE : XC_RESULT_SUCCESS;
}

static int xc_reset(struct dvb_frontend *fe)
{
	xc5000_TunerReset(fe);
	return XC_RESULT_SUCCESS;
}

static void xc_wait(int wait_ms)
{
	msleep(wait_ms);
}

static void xc5000_TunerReset(struct dvb_frontend *fe)
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
		if (ret)
			printk(KERN_ERR "xc5000: reset failed\n");
	} else
		printk(KERN_ERR "xc5000: no tuner reset callback function, fatal\n");
}

static int xc_write_reg(struct xc5000_priv *priv, u16 regAddr, u16 i2cData)
{
	u8 buf[4];
	int WatchDogTimer = 5;
	int result;

	buf[0] = (regAddr >> 8) & 0xFF;
	buf[1] = regAddr & 0xFF;
	buf[2] = (i2cData >> 8) & 0xFF;
	buf[3] = i2cData & 0xFF;
	result = xc_send_i2c_data(priv, buf, 4);
	if (result == XC_RESULT_SUCCESS) {
		/* wait for busy flag to clear */
		while ((WatchDogTimer > 0) && (result == XC_RESULT_SUCCESS)) {
			buf[0] = 0;
			buf[1] = XREG_BUSY;

			result = xc_send_i2c_data(priv, buf, 2);
			if (result == XC_RESULT_SUCCESS) {
				result = xc_read_i2c_data(priv, buf, 2);
				if (result == XC_RESULT_SUCCESS) {
					if ((buf[0] == 0) && (buf[1] == 0)) {
						/* busy flag cleared */
					break;
					} else {
						xc_wait(100); /* wait 5 ms */
						WatchDogTimer--;
					}
				}
			}
		}
	}
	if (WatchDogTimer < 0)
		result = XC_RESULT_I2C_WRITE_FAILURE;

	return result;
}

static int xc_read_reg(struct xc5000_priv *priv, u16 regAddr, u16 *i2cData)
{
	u8 buf[2];
	int result;

	buf[0] = (regAddr >> 8) & 0xFF;
	buf[1] = regAddr & 0xFF;
	result = xc_send_i2c_data(priv, buf, 2);
	if (result != XC_RESULT_SUCCESS)
		return result;

	result = xc_read_i2c_data(priv, buf, 2);
	if (result != XC_RESULT_SUCCESS)
		return result;

	*i2cData = buf[0] * 256 + buf[1];
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
			result = xc_reset(fe);
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

static int xc_initialize(struct xc5000_priv *priv)
{
	dprintk(1, "%s()\n", __func__);
	return xc_write_reg(priv, XREG_INIT, 0);
}

static int xc_SetTVStandard(struct xc5000_priv *priv,
	u16 VideoMode, u16 AudioMode)
{
	int ret;
	dprintk(1, "%s(0x%04x,0x%04x)\n", __func__, VideoMode, AudioMode);
	dprintk(1, "%s() Standard = %s\n",
		__func__,
		XC5000_Standard[priv->video_standard].Name);

	ret = xc_write_reg(priv, XREG_VIDEO_MODE, VideoMode);
	if (ret == XC_RESULT_SUCCESS)
		ret = xc_write_reg(priv, XREG_AUDIO_MODE, AudioMode);

	return ret;
}

static int xc_shutdown(struct xc5000_priv *priv)
{
	return XC_RESULT_SUCCESS;
	/* Fixme: cannot bring tuner back alive once shutdown
	 *        without reloading the driver modules.
	 *    return xc_write_reg(priv, XREG_POWER_DOWN, 0);
	 */
}

static int xc_SetSignalSource(struct xc5000_priv *priv, u16 rf_mode)
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

static int xc_set_RF_frequency(struct xc5000_priv *priv, u32 freq_hz)
{
	u16 freq_code;

	dprintk(1, "%s(%u)\n", __func__, freq_hz);

	if ((freq_hz > xc5000_tuner_ops.info.frequency_max) ||
		(freq_hz < xc5000_tuner_ops.info.frequency_min))
		return XC_RESULT_OUT_OF_RANGE;

	freq_code = (u16)(freq_hz / 15625);

	return xc_write_reg(priv, XREG_RF_FREQ, freq_code);
}


static int xc_set_IF_frequency(struct xc5000_priv *priv, u32 freq_khz)
{
	u32 freq_code = (freq_khz * 1024)/1000;
	dprintk(1, "%s(freq_khz = %d) freq_code = 0x%x\n",
		__func__, freq_khz, freq_code);

	return xc_write_reg(priv, XREG_IF_OUT, freq_code);
}


static int xc_get_ADC_Envelope(struct xc5000_priv *priv, u16 *adc_envelope)
{
	return xc_read_reg(priv, XREG_ADC_ENV, adc_envelope);
}

static int xc_get_frequency_error(struct xc5000_priv *priv, u32 *freq_error_hz)
{
	int result;
	u16 regData;
	u32 tmp;

	result = xc_read_reg(priv, XREG_FREQ_ERROR, &regData);
	if (result)
		return result;

	tmp = (u32)regData;
	(*freq_error_hz) = (tmp * 15625) / 1000;
	return result;
}

static int xc_get_lock_status(struct xc5000_priv *priv, u16 *lock_status)
{
	return xc_read_reg(priv, XREG_LOCK, lock_status);
}

static int xc_get_version(struct xc5000_priv *priv,
	u8 *hw_majorversion, u8 *hw_minorversion,
	u8 *fw_majorversion, u8 *fw_minorversion)
{
	u16 data;
	int result;

	result = xc_read_reg(priv, XREG_VERSION, &data);
	if (result)
		return result;

	(*hw_majorversion) = (data >> 12) & 0x0F;
	(*hw_minorversion) = (data >>  8) & 0x0F;
	(*fw_majorversion) = (data >>  4) & 0x0F;
	(*fw_minorversion) = data & 0x0F;

	return 0;
}

static int xc_get_hsync_freq(struct xc5000_priv *priv, u32 *hsync_freq_hz)
{
	u16 regData;
	int result;

	result = xc_read_reg(priv, XREG_HSYNC_FREQ, &regData);
	if (result)
		return result;

	(*hsync_freq_hz) = ((regData & 0x0fff) * 763)/100;
	return result;
}

static int xc_get_frame_lines(struct xc5000_priv *priv, u16 *frame_lines)
{
	return xc_read_reg(priv, XREG_FRAME_LINES, frame_lines);
}

static int xc_get_quality(struct xc5000_priv *priv, u16 *quality)
{
	return xc_read_reg(priv, XREG_QUALITY, quality);
}

static u16 WaitForLock(struct xc5000_priv *priv)
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

static int xc_tune_channel(struct xc5000_priv *priv, u32 freq_hz)
{
	int found = 0;

	dprintk(1, "%s(%u)\n", __func__, freq_hz);

	if (xc_set_RF_frequency(priv, freq_hz) != XC_RESULT_SUCCESS)
		return 0;

	if (WaitForLock(priv) == 1)
		found = 1;

	return found;
}

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

static int xc5000_writeregs(struct xc5000_priv *priv, u8 *buf, u8 len)
{
	struct i2c_msg msg = { .addr = priv->i2c_props.addr,
		.flags = 0, .buf = buf, .len = len };

	if (i2c_transfer(priv->i2c_props.adap, &msg, 1) != 1) {
		printk(KERN_ERR "xc5000: I2C write failed (len=%i)\n",
			(int)len);
		return -EREMOTEIO;
	}
	return 0;
}

static int xc5000_readregs(struct xc5000_priv *priv, u8 *buf, u8 len)
{
	struct i2c_msg msg = { .addr = priv->i2c_props.addr,
		.flags = I2C_M_RD, .buf = buf, .len = len };

	if (i2c_transfer(priv->i2c_props.adap, &msg, 1) != 1) {
		printk(KERN_ERR "xc5000 I2C read failed (len=%i)\n", (int)len);
		return -EREMOTEIO;
	}
	return 0;
}

static int xc5000_fwupload(struct dvb_frontend *fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	const struct firmware *fw;
	int ret;

	/* request the firmware, this will block and timeout */
	printk(KERN_INFO "xc5000: waiting for firmware upload (%s)...\n",
		XC5000_DEFAULT_FIRMWARE);

	ret = request_firmware(&fw, XC5000_DEFAULT_FIRMWARE,
		&priv->i2c_props.adap->dev);
	if (ret) {
		printk(KERN_ERR "xc5000: Upload failed. (file not found?)\n");
		ret = XC_RESULT_RESET_FAILURE;
		goto out;
	} else {
		printk(KERN_INFO "xc5000: firmware read %Zu bytes.\n",
		       fw->size);
		ret = XC_RESULT_SUCCESS;
	}

	if (fw->size != XC5000_DEFAULT_FIRMWARE_SIZE) {
		printk(KERN_ERR "xc5000: firmware incorrect size\n");
		ret = XC_RESULT_RESET_FAILURE;
	} else {
		printk(KERN_INFO "xc5000: firmware upload\n");
		ret = xc_load_i2c_sequence(fe,  fw->data);
	}

out:
	release_firmware(fw);
	return ret;
}

static void xc_debug_dump(struct xc5000_priv *priv)
{
	u16 adc_envelope;
	u32 freq_error_hz = 0;
	u16 lock_status;
	u32 hsync_freq_hz = 0;
	u16 frame_lines;
	u16 quality;
	u8 hw_majorversion = 0, hw_minorversion = 0;
	u8 fw_majorversion = 0, fw_minorversion = 0;

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

static int xc5000_set_params(struct dvb_frontend *fe,
	struct dvb_frontend_parameters *params)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	int ret;

	dprintk(1, "%s() frequency=%d (Hz)\n", __func__, params->frequency);

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

	dprintk(1, "%s() frequency=%d (compensated)\n",
		__func__, priv->freq_hz);

	ret = xc_SetSignalSource(priv, priv->rf_mode);
	if (ret != XC_RESULT_SUCCESS) {
		printk(KERN_ERR
			"xc5000: xc_SetSignalSource(%d) failed\n",
			priv->rf_mode);
		return -EREMOTEIO;
	}

	ret = xc_SetTVStandard(priv,
		XC5000_Standard[priv->video_standard].VideoMode,
		XC5000_Standard[priv->video_standard].AudioMode);
	if (ret != XC_RESULT_SUCCESS) {
		printk(KERN_ERR "xc5000: xc_SetTVStandard failed\n");
		return -EREMOTEIO;
	}

	ret = xc_set_IF_frequency(priv, priv->if_khz);
	if (ret != XC_RESULT_SUCCESS) {
		printk(KERN_ERR "xc5000: xc_Set_IF_frequency(%d) failed\n",
		       priv->if_khz);
		return -EIO;
	}

	xc_tune_channel(priv, priv->freq_hz);

	if (debug)
		xc_debug_dump(priv);

	return 0;
}

static int xc5000_is_firmware_loaded(struct dvb_frontend *fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	int ret;
	u16 id;

	ret = xc5000_readreg(priv, XREG_PRODUCT_ID, &id);
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

static int xc_load_fw_and_init_tuner(struct dvb_frontend *fe);

static int xc5000_set_analog_params(struct dvb_frontend *fe,
	struct analog_parameters *params)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	int ret;

	if (xc5000_is_firmware_loaded(fe) != XC_RESULT_SUCCESS)
		xc_load_fw_and_init_tuner(fe);

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
			"xc5000: xc_SetSignalSource(%d) failed\n",
			priv->rf_mode);
		return -EREMOTEIO;
	}

	ret = xc_SetTVStandard(priv,
		XC5000_Standard[priv->video_standard].VideoMode,
		XC5000_Standard[priv->video_standard].AudioMode);
	if (ret != XC_RESULT_SUCCESS) {
		printk(KERN_ERR "xc5000: xc_SetTVStandard failed\n");
		return -EREMOTEIO;
	}

	xc_tune_channel(priv, priv->freq_hz);

	if (debug)
		xc_debug_dump(priv);

	return 0;
}

static int xc5000_get_frequency(struct dvb_frontend *fe, u32 *freq)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	dprintk(1, "%s()\n", __func__);
	*freq = priv->freq_hz;
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

static int xc_load_fw_and_init_tuner(struct dvb_frontend *fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	int ret = 0;

	if (xc5000_is_firmware_loaded(fe) != XC_RESULT_SUCCESS) {
		ret = xc5000_fwupload(fe);
		if (ret != XC_RESULT_SUCCESS)
			return ret;
	}

	/* Start the tuner self-calibration process */
	ret |= xc_initialize(priv);

	/* Wait for calibration to complete.
	 * We could continue but XC5000 will clock stretch subsequent
	 * I2C transactions until calibration is complete.  This way we
	 * don't have to rely on clock stretching working.
	 */
	xc_wait(100);

	/* Default to "CABLE" mode */
	ret |= xc_write_reg(priv, XREG_SIGNALSOURCE, XC_RF_MODE_CABLE);

	return ret;
}

static int xc5000_sleep(struct dvb_frontend *fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	int ret;

	dprintk(1, "%s()\n", __func__);

	/* On Pinnacle PCTV HD 800i, the tuner cannot be reinitialized
	 * once shutdown without reloading the driver. Maybe I am not
	 * doing something right.
	 *
	 */

	ret = xc_shutdown(priv);
	if (ret != XC_RESULT_SUCCESS) {
		printk(KERN_ERR
			"xc5000: %s() unable to shutdown tuner\n",
			__func__);
		return -EREMOTEIO;
	} else
		return XC_RESULT_SUCCESS;
}

static int xc5000_init(struct dvb_frontend *fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	dprintk(1, "%s()\n", __func__);

	if (xc_load_fw_and_init_tuner(fe) != XC_RESULT_SUCCESS) {
		printk(KERN_ERR "xc5000: Unable to initialise tuner\n");
		return -EREMOTEIO;
	}

	if (debug)
		xc_debug_dump(priv);

	return 0;
}

static int xc5000_release(struct dvb_frontend *fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;

	dprintk(1, "%s()\n", __func__);

	mutex_lock(&xc5000_list_mutex);

	if (priv)
		hybrid_tuner_release_state(priv);

	mutex_unlock(&xc5000_list_mutex);

	fe->tuner_priv = NULL;

	return 0;
}

static const struct dvb_tuner_ops xc5000_tuner_ops = {
	.info = {
		.name           = "Xceive XC5000",
		.frequency_min  =    1000000,
		.frequency_max  = 1023000000,
		.frequency_step =      50000,
	},

	.release	   = xc5000_release,
	.init		   = xc5000_init,
	.sleep		   = xc5000_sleep,

	.set_params	   = xc5000_set_params,
	.set_analog_params = xc5000_set_analog_params,
	.get_frequency	   = xc5000_get_frequency,
	.get_bandwidth	   = xc5000_get_bandwidth,
	.get_status	   = xc5000_get_status
};

struct dvb_frontend *xc5000_attach(struct dvb_frontend *fe,
				   struct i2c_adapter *i2c,
				   struct xc5000_config *cfg)
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
		   call to xc5000_attach occurs before the digital side) */
		priv->if_khz = cfg->if_khz;
	}

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
EXPORT_SYMBOL(xc5000_attach);

MODULE_AUTHOR("Steven Toth");
MODULE_DESCRIPTION("Xceive xc5000 silicon tuner driver");
MODULE_LICENSE("GPL");
