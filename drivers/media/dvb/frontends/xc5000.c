/*
 *  Driver for Xceive XC5000 "QAM/8VSB single chip tuner"
 *
 *  Copyright (c) 2007 Xceive Corporation
 *  Copyright (c) 2007 Steven Toth <stoth@hauppauge.com>
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
#include <linux/delay.h>
#include <linux/dvb/frontend.h>
#include <linux/i2c.h>

#include "dvb_frontend.h"

#include "xc5000.h"
#include "xc5000_priv.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off debugging (default:off).");

#define dprintk(level,fmt, arg...) if (debug >= level) \
	printk(KERN_INFO "%s: " fmt, "xc5000", ## arg)

#define XC5000_DEFAULT_FIRMWARE "dvb-fe-xc5000-1.1.fw"
#define XC5000_DEFAULT_FIRMWARE_SIZE 12400

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
typedef struct {
	char *Name;
	unsigned short AudioMode;
	unsigned short VideoMode;
} XC_TV_STANDARD;

/* Tuner standards */
#define DTV6	17

XC_TV_STANDARD XC5000_Standard[MAX_TV_STANDARD] = {
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
	{"D/K-SECAM-A2 L/DK3",0x0E00, 0x8009},
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

static int  xc5000_writeregs(struct xc5000_priv *priv, u8 *buf, u8 len);
static int  xc5000_readregs(struct xc5000_priv *priv, u8 *buf, u8 len);
static void xc5000_TunerReset(struct dvb_frontend *fe);

int xc_send_i2c_data(struct xc5000_priv *priv,
	unsigned char *bytes_to_send, int nb_bytes_to_send)
{
	return xc5000_writeregs(priv, bytes_to_send, nb_bytes_to_send)
		? XC_RESULT_I2C_WRITE_FAILURE : XC_RESULT_SUCCESS;
}

int xc_read_i2c_data(struct xc5000_priv *priv, unsigned char *bytes_received,
	int nb_bytes_to_receive)
{
	return xc5000_readregs(priv, bytes_received, nb_bytes_to_receive)
		? XC_RESULT_I2C_READ_FAILURE : XC_RESULT_SUCCESS;
}

int xc_reset(struct dvb_frontend *fe)
{
	xc5000_TunerReset(fe);
	return XC_RESULT_SUCCESS;
}

void xc_wait(int wait_ms)
{
	msleep( wait_ms );
}

static void xc5000_TunerReset(struct dvb_frontend *fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	int ret;

	dprintk(1, "%s()\n", __FUNCTION__);

	if(priv->cfg->tuner_reset) {
		ret = priv->cfg->tuner_reset(fe);
		if (ret)
			printk(KERN_ERR "xc5000: reset failed\n");
	} else
		printk(KERN_ERR "xc5000: no tuner reset function, fatal\n");
}

int xc_write_reg(struct xc5000_priv *priv, unsigned short int regAddr,
	unsigned short int i2cData)
{
	unsigned char buf[4];
	int WatchDogTimer = 5;
	int result;

	buf[0] = (regAddr >> 8) & 0xFF;
	buf[1] = regAddr & 0xFF;
	buf[2] = (i2cData >> 8) & 0xFF;
	buf[3] = i2cData & 0xFF;
	result = xc_send_i2c_data(priv, buf, 4);
	if ( result == XC_RESULT_SUCCESS) {
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

int xc_read_reg(struct xc5000_priv *priv, unsigned short int regAddr,
	unsigned short int *i2cData)
{
	unsigned char buf[2];
	int result;

	buf[0] = (regAddr >> 8) & 0xFF;
	buf[1] = regAddr & 0xFF;
	result = xc_send_i2c_data(priv, buf, 2);
	if (result!=XC_RESULT_SUCCESS)
		return result;

	result = xc_read_i2c_data(priv, buf, 2);
	if (result!=XC_RESULT_SUCCESS)
		return result;

	*i2cData = buf[0] * 256 + buf[1];
	return result;
}

int xc_load_i2c_sequence(struct dvb_frontend *fe, unsigned char i2c_sequence[])
{
	struct xc5000_priv *priv = fe->tuner_priv;

	int i, nbytes_to_send, result;
	unsigned int len, pos, index;
	unsigned char buf[XC_MAX_I2C_WRITE_LENGTH];

	index=0;
	while ((i2c_sequence[index]!=0xFF) || (i2c_sequence[index+1]!=0xFF)) {

		len = i2c_sequence[index]* 256 + i2c_sequence[index+1];
		if (len==0x0000) {
			/* RESET command */
			result = xc_reset(fe);
			index += 2;
			if (result!=XC_RESULT_SUCCESS)
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
				if ((len - pos) > XC_MAX_I2C_WRITE_LENGTH - 2) {
					nbytes_to_send = XC_MAX_I2C_WRITE_LENGTH;
				} else {
					nbytes_to_send = (len - pos + 2);
				}
				for (i=2; i<nbytes_to_send; i++) {
					buf[i] = i2c_sequence[index + pos + i - 2];
				}
				result = xc_send_i2c_data(priv, buf, nbytes_to_send);

				if (result!=XC_RESULT_SUCCESS)
					return result;

				pos += nbytes_to_send - 2;
			}
			index += len;
		}
	}
	return XC_RESULT_SUCCESS;
}

int xc_initialize(struct xc5000_priv *priv)
{
	dprintk(1, "%s()\n", __FUNCTION__);
	return xc_write_reg(priv, XREG_INIT, 0);
}

int xc_SetTVStandard(struct xc5000_priv *priv, unsigned short int VideoMode,
	unsigned short int AudioMode)
{
	int ret;
	dprintk(1, "%s(%d,%d)\n", __FUNCTION__, VideoMode, AudioMode);
	dprintk(1, "%s() Standard = %s\n",
		__FUNCTION__,
		XC5000_Standard[priv->video_standard].Name);

	ret = xc_write_reg(priv, XREG_VIDEO_MODE, VideoMode);
	if (ret == XC_RESULT_SUCCESS)
		ret = xc_write_reg(priv, XREG_AUDIO_MODE, AudioMode);

	return ret;
}

int xc_shutdown(struct xc5000_priv *priv)
{
	return xc_write_reg(priv, XREG_POWER_DOWN, 0);
}

int xc_SetSignalSource(struct xc5000_priv *priv, unsigned short int rf_mode)
{
	dprintk(1, "%s(%d) Source = %s\n", __FUNCTION__, rf_mode,
		rf_mode == XC_RF_MODE_AIR ? "ANTENNA" : "CABLE");

	if( (rf_mode != XC_RF_MODE_AIR) && (rf_mode != XC_RF_MODE_CABLE) )
	{
		rf_mode = XC_RF_MODE_CABLE;
		printk(KERN_ERR
			"%s(), Invalid mode, defaulting to CABLE",
			__FUNCTION__);
	}
	return xc_write_reg(priv, XREG_SIGNALSOURCE, rf_mode);
}

int xc_set_RF_frequency(struct xc5000_priv *priv, long frequency_in_hz)
{
	unsigned int frequency_code = (unsigned int)(frequency_in_hz / 15625);

	if ((frequency_in_hz>1023000000) || (frequency_in_hz<1000000))
		return XC_RESULT_OUT_OF_RANGE;

	return xc_write_reg(priv, XREG_RF_FREQ ,frequency_code);
}

int xc_FineTune_RF_frequency(struct xc5000_priv *priv, long frequency_in_hz)
{
	unsigned int frequency_code = (unsigned int)(frequency_in_hz / 15625);
	if ((frequency_in_hz>1023000000) || (frequency_in_hz<1000000))
		return XC_RESULT_OUT_OF_RANGE;

	return xc_write_reg(priv, XREG_FINERFFREQ ,frequency_code);
}

int xc_set_IF_frequency(struct xc5000_priv *priv, u32 freq_hz)
{
	u32 freq_code = (freq_hz * 1024)/1000000;
	dprintk(1, "%s(%d)\n", __FUNCTION__, freq_hz);

	printk(KERN_ERR "FIXME - Hardcoded IF, FIXME\n");
	freq_code = 0x1585;

	return xc_write_reg(priv, XREG_IF_OUT ,freq_code);
}

int xc_set_Xtal_frequency(struct xc5000_priv *priv, long xtalFreqInKHz)
{
	unsigned int xtalRatio = (32000 * 0x8000)/xtalFreqInKHz;
	return xc_write_reg(priv, XREG_XTALFREQ ,xtalRatio);
}

int xc_get_ADC_Envelope(struct xc5000_priv *priv,
	unsigned short int *adc_envelope)
{
	return xc_read_reg(priv, XREG_ADC_ENV, adc_envelope);
}

int xc_get_frequency_error(struct xc5000_priv *priv, u32 *frequency_error_hz)
{
	int result;
	unsigned short int regData;
	u32 tmp;

	result = xc_read_reg(priv, XREG_FREQ_ERROR, &regData);
	if (result)
		return result;

	tmp = (u32)regData;
	(*frequency_error_hz) = (tmp * 15625) / 1000;
	return result;
}

int xc_get_lock_status(struct xc5000_priv *priv,
	unsigned short int *lock_status)
{
	return xc_read_reg(priv, XREG_LOCK, lock_status);
}

int xc_get_version(struct xc5000_priv *priv,
	unsigned char* hw_majorversion,
	unsigned char* hw_minorversion,
	unsigned char* fw_majorversion,
	unsigned char* fw_minorversion)
{
	unsigned short int data;
	int result;

	result = xc_read_reg(priv, XREG_VERSION, &data);
	if (result)
		return result;

	(*hw_majorversion) = (data>>12) & 0x0F;
	(*hw_minorversion) = (data>>8) & 0x0F;
	(*fw_majorversion) = (data>>4) & 0x0F;
	(*fw_minorversion) = (data) & 0x0F;

	return 0;
}

int xc_get_product_id(struct xc5000_priv *priv, unsigned short int *product_id)
{
	return xc_read_reg(priv, XREG_PRODUCT_ID, product_id);
}

int xc_get_hsync_freq(struct xc5000_priv *priv, int *hsync_freq_hz)
{
	unsigned short int regData;
	int result;

	result = xc_read_reg(priv, XREG_HSYNC_FREQ, &regData);
	if (result)
		return result;

	(*hsync_freq_hz) = ((regData & 0x0fff) * 763)/100;
	return result;
}

int xc_get_frame_lines(struct xc5000_priv *priv,
	unsigned short int *frame_lines)
{
	return xc_read_reg(priv, XREG_FRAME_LINES, frame_lines);
}

int xc_get_quality(struct xc5000_priv *priv, unsigned short int *quality)
{
	return xc_read_reg(priv, XREG_QUALITY, quality);
}

unsigned short int WaitForLock(struct xc5000_priv *priv)
{
	unsigned short int lockState = 0;
	int watchDogCount = 40;
	while ((lockState == 0) && (watchDogCount > 0))
	{
		xc_get_lock_status(priv, &lockState);
		if (lockState != 1)
		{
			xc_wait(5);
			watchDogCount--;
		}
	}
	return lockState;
}

int xc_tune_channel(struct xc5000_priv *priv, u32 freq)
{
	int found = 0;

	dprintk(1, "%s(%d)\n", __FUNCTION__, freq);

	if (xc_set_RF_frequency(priv, freq) != XC_RESULT_SUCCESS)
		return 0;

	if (WaitForLock(priv)== 1)
		found = 1;

	return found;
}

static int xc5000_readreg(struct xc5000_priv *priv, u16 reg, u16 *val)
{
	u8 buf[2] = { reg >> 8, reg & 0xff };
	u8 bval[2] = { 0, 0 };
	struct i2c_msg msg[2] = {
		{ .addr = priv->cfg->i2c_address,
			.flags = 0, .buf = &buf[0], .len = 2 },
		{ .addr = priv->cfg->i2c_address,
			.flags = I2C_M_RD, .buf = &bval[0], .len = 2 },
	};

	if (i2c_transfer(priv->i2c, msg, 2) != 2) {
		printk(KERN_WARNING "xc5000 I2C read failed\n");
		return -EREMOTEIO;
	}

	*val = (bval[0] << 8) | bval[1];
	return 0;
}

static int xc5000_writeregs(struct xc5000_priv *priv, u8 *buf, u8 len)
{
	struct i2c_msg msg = { .addr = priv->cfg->i2c_address,
		.flags = 0, .buf = buf, .len = len };

	if (i2c_transfer(priv->i2c, &msg, 1) != 1) {
		printk(KERN_ERR "xc5000 I2C write failed (len=%i)\n",
			(int)len);
		return -EREMOTEIO;
	}
	return 0;
}

static int xc5000_readregs(struct xc5000_priv *priv, u8 *buf, u8 len)
{
	struct i2c_msg msg = { .addr = priv->cfg->i2c_address,
		.flags = I2C_M_RD, .buf = buf, .len = len };

	if (i2c_transfer(priv->i2c, &msg, 1) != 1) {
		printk(KERN_ERR "xc5000 I2C read failed (len=%i)\n",(int)len);
		return -EREMOTEIO;
	}
	return 0;
}

static int xc5000_fwupload(struct dvb_frontend* fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	const struct firmware *fw;
	int ret;

	/* request the firmware, this will block until someone uploads it */
	printk(KERN_INFO "xc5000: waiting for firmware upload (%s)...\n",
		XC5000_DEFAULT_FIRMWARE);

	if(!priv->cfg->request_firmware) {
		printk(KERN_ERR "xc5000: no firmware callback, fatal\n");
		return -EIO;
	}

	ret = priv->cfg->request_firmware(fe, &fw, XC5000_DEFAULT_FIRMWARE);
	if (ret) {
		printk(KERN_ERR "xc5000: Upload failed. (file not found?)\n");
		ret = XC_RESULT_RESET_FAILURE;
	} else {
		printk(KERN_INFO "xc5000: firmware read %d bytes.\n", fw->size);
		ret = XC_RESULT_SUCCESS;
	}

	if(fw->size != XC5000_DEFAULT_FIRMWARE_SIZE) {
		printk(KERN_ERR "xc5000: firmware incorrect size\n");
		ret = XC_RESULT_RESET_FAILURE;
	} else {
		printk(KERN_INFO "xc5000: firmware upload\n");
		ret = xc_load_i2c_sequence(fe,  fw->data );
	}

	release_firmware(fw);
	return ret;
}

void xc_debug_dump(struct xc5000_priv *priv)
{
	unsigned short	adc_envelope;
	u32		frequency_error_hz;
	unsigned short	lock_status;
	unsigned char	hw_majorversion, hw_minorversion = 0;
	unsigned char	fw_majorversion, fw_minorversion = 0;
	int          	hsync_freq_hz;
	unsigned short	frame_lines;
	unsigned short	quality;

	/* Wait for stats to stabilize.
	 * Frame Lines needs two frame times after initial lock
	 * before it is valid.
	 */
	xc_wait( 100 );

	xc_get_ADC_Envelope(priv,  &adc_envelope );
	dprintk(1, "*** ADC envelope (0-1023) = %u\n", adc_envelope);

	xc_get_frequency_error(priv,  &frequency_error_hz );
	dprintk(1, "*** Frequency error = %d Hz\n", frequency_error_hz);

	xc_get_lock_status(priv,  &lock_status );
	dprintk(1, "*** Lock status (0-Wait, 1-Locked, 2-No-signal) = %u\n",
		lock_status);

	xc_get_version(priv,  &hw_majorversion, &hw_minorversion,
		&fw_majorversion, &fw_minorversion );
	dprintk(1, "*** HW: V%02x.%02x, FW: V%02x.%02x\n",
		hw_majorversion, hw_minorversion,
		fw_majorversion, fw_minorversion);

	xc_get_hsync_freq(priv,  &hsync_freq_hz );
	dprintk(1, "*** Horizontal sync frequency = %u Hz\n", hsync_freq_hz);

	xc_get_frame_lines(priv,  &frame_lines );
	dprintk(1, "*** Frame lines = %u\n", frame_lines);

	xc_get_quality(priv,  &quality );
	dprintk(1, "*** Quality (0:<8dB, 7:>56dB) = %u\n", quality);
}

static int xc5000_set_params(struct dvb_frontend *fe,
	struct dvb_frontend_parameters *params)
{
	struct xc5000_priv *priv = fe->tuner_priv;

	dprintk(1, "%s() frequency=%d\n", __FUNCTION__, params->frequency);

	priv->frequency = params->frequency - 1750000;
	priv->bandwidth = 6;
	priv->video_standard = DTV6;

	switch(params->u.vsb.modulation) {
	case VSB_8:
	case VSB_16:
		dprintk(1, "%s() VSB modulation\n", __FUNCTION__);
		priv->rf_mode = XC_RF_MODE_AIR;
		break;
	case QAM_64:
	case QAM_256:
	case QAM_AUTO:
		dprintk(1, "%s() QAM modulation\n", __FUNCTION__);
		priv->rf_mode = XC_RF_MODE_CABLE;
		break;
	default:
		return -EINVAL;
	}

	dprintk(1, "%s() frequency=%d (compensated)\n",
		__FUNCTION__, priv->frequency);

	/* FIXME: check result codes */
	xc_SetSignalSource(priv, priv->rf_mode);

	xc_SetTVStandard(priv,
		XC5000_Standard[priv->video_standard].VideoMode,
		XC5000_Standard[priv->video_standard].AudioMode);

	xc_set_IF_frequency(priv, priv->cfg->if_frequency);
	xc_tune_channel(priv, priv->frequency);
	xc_debug_dump(priv);

	return 0;
}

static int xc5000_get_frequency(struct dvb_frontend *fe, u32 *freq)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	dprintk(1, "%s()\n", __FUNCTION__);
	*freq = priv->frequency;
	return 0;
}

static int xc5000_get_bandwidth(struct dvb_frontend *fe, u32 *bw)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	dprintk(1, "%s()\n", __FUNCTION__);
	*bw = priv->bandwidth;
	return 0;
}

static int xc5000_get_status(struct dvb_frontend *fe, u32 *status)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	unsigned short int lock_status = 0;

	xc_get_lock_status(priv, &lock_status);

	dprintk(1, "%s() lock_status = 0x%08x\n", __FUNCTION__, lock_status);

	*status = lock_status;

	return 0;
}

int xc_load_fw_and_init_tuner(struct dvb_frontend *fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	int ret;

	if(priv->fwloaded == 0) {
		ret = xc5000_fwupload(fe);
		if( ret != XC_RESULT_SUCCESS )
			return -EREMOTEIO;

		priv->fwloaded = 1;
	}

	/* Start the tuner self-calibration process */
	ret |= xc_initialize(priv);

	/* Wait for calibration to complete.
	 * We could continue but XC5000 will clock stretch subsequent
	 * I2C transactions until calibration is complete.  This way we
	 * don't have to rely on clock stretching working.
	 */
	xc_wait( 100 );

	/* Default to "CABLE" mode */
	ret |= xc_write_reg(priv, XREG_SIGNALSOURCE, XC_RF_MODE_CABLE);

	return ret;
}

static int xc5000_init(struct dvb_frontend *fe)
{
	struct xc5000_priv *priv = fe->tuner_priv;
	dprintk(1, "%s()\n", __FUNCTION__);

	xc_load_fw_and_init_tuner(fe);
	xc_debug_dump(priv);

	return 0;
}

static int xc5000_release(struct dvb_frontend *fe)
{
	dprintk(1, "%s()\n", __FUNCTION__);
	kfree(fe->tuner_priv);
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

	.release       = xc5000_release,
	.init          = xc5000_init,

	.set_params    = xc5000_set_params,
	.get_frequency = xc5000_get_frequency,
	.get_bandwidth = xc5000_get_bandwidth,
	.get_status    = xc5000_get_status
};

struct dvb_frontend * xc5000_attach(struct dvb_frontend *fe,
	struct i2c_adapter *i2c,
	struct xc5000_config *cfg)
{
	struct xc5000_priv *priv = NULL;
	u16 id = 0;

	dprintk(1, "%s()\n", __FUNCTION__);

	priv = kzalloc(sizeof(struct xc5000_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;

	priv->cfg = cfg;
	priv->bandwidth = 6000000; /* 6MHz */
	priv->i2c = i2c;
	priv->fwloaded = 0;

	if (xc5000_readreg(priv, XREG_PRODUCT_ID, &id) != 0) {
		kfree(priv);
		return NULL;
	}

	if ( (id != 0x2000) && (id != 0x1388) ) {
		printk(KERN_ERR
			"xc5000: Device not found at addr 0x%02x (0x%x)\n",
			cfg->i2c_address, id);
		kfree(priv);
		return NULL;
	}

	printk(KERN_INFO "xc5000: successfully identified at address 0x%02x\n",
		cfg->i2c_address);

	memcpy(&fe->ops.tuner_ops, &xc5000_tuner_ops,
		sizeof(struct dvb_tuner_ops));

	fe->tuner_priv = priv;

	return fe;
}
EXPORT_SYMBOL(xc5000_attach);

MODULE_AUTHOR("Steven Toth");
MODULE_DESCRIPTION("Xceive XC5000 silicon tuner driver");
MODULE_LICENSE("GPL");
