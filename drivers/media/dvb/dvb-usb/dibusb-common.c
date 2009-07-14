/* Common methods for dibusb-based-receivers.
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#include "dibusb.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info (|-able))." DVB_USB_DEBUG_STATUS);
MODULE_LICENSE("GPL");

#define deb_info(args...) dprintk(debug,0x01,args)

/* common stuff used by the different dibusb modules */
int dibusb_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	if (adap->priv != NULL) {
		struct dibusb_state *st = adap->priv;
		if (st->ops.fifo_ctrl != NULL)
			if (st->ops.fifo_ctrl(adap->fe,onoff)) {
				err("error while controlling the fifo of the demod.");
				return -ENODEV;
			}
	}
	return 0;
}
EXPORT_SYMBOL(dibusb_streaming_ctrl);

int dibusb_pid_filter(struct dvb_usb_adapter *adap, int index, u16 pid, int onoff)
{
	if (adap->priv != NULL) {
		struct dibusb_state *st = adap->priv;
		if (st->ops.pid_ctrl != NULL)
			st->ops.pid_ctrl(adap->fe,index,pid,onoff);
	}
	return 0;
}
EXPORT_SYMBOL(dibusb_pid_filter);

int dibusb_pid_filter_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	if (adap->priv != NULL) {
		struct dibusb_state *st = adap->priv;
		if (st->ops.pid_parse != NULL)
			if (st->ops.pid_parse(adap->fe,onoff) < 0)
				err("could not handle pid_parser");
	}
	return 0;
}
EXPORT_SYMBOL(dibusb_pid_filter_ctrl);

int dibusb_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	u8 b[3];
	int ret;
	b[0] = DIBUSB_REQ_SET_IOCTL;
	b[1] = DIBUSB_IOCTL_CMD_POWER_MODE;
	b[2] = onoff ? DIBUSB_IOCTL_POWER_WAKEUP : DIBUSB_IOCTL_POWER_SLEEP;
	ret = dvb_usb_generic_write(d,b,3);
	msleep(10);
	return ret;
}
EXPORT_SYMBOL(dibusb_power_ctrl);

int dibusb2_0_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	u8 b[3] = { 0 };
	int ret;

	if ((ret = dibusb_streaming_ctrl(adap,onoff)) < 0)
		return ret;

	if (onoff) {
		b[0] = DIBUSB_REQ_SET_STREAMING_MODE;
		b[1] = 0x00;
		if ((ret = dvb_usb_generic_write(adap->dev,b,2)) < 0)
			return ret;
	}

	b[0] = DIBUSB_REQ_SET_IOCTL;
	b[1] = onoff ? DIBUSB_IOCTL_CMD_ENABLE_STREAM : DIBUSB_IOCTL_CMD_DISABLE_STREAM;
	return dvb_usb_generic_write(adap->dev,b,3);
}
EXPORT_SYMBOL(dibusb2_0_streaming_ctrl);

int dibusb2_0_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	if (onoff) {
		u8 b[3] = { DIBUSB_REQ_SET_IOCTL, DIBUSB_IOCTL_CMD_POWER_MODE, DIBUSB_IOCTL_POWER_WAKEUP };
		return dvb_usb_generic_write(d,b,3);
	} else
		return 0;
}
EXPORT_SYMBOL(dibusb2_0_power_ctrl);

static int dibusb_i2c_msg(struct dvb_usb_device *d, u8 addr,
			  u8 *wbuf, u16 wlen, u8 *rbuf, u16 rlen)
{
	u8 sndbuf[wlen+4]; /* lead(1) devaddr,direction(1) addr(2) data(wlen) (len(2) (when reading)) */
	/* write only ? */
	int wo = (rbuf == NULL || rlen == 0),
		len = 2 + wlen + (wo ? 0 : 2);

	sndbuf[0] = wo ? DIBUSB_REQ_I2C_WRITE : DIBUSB_REQ_I2C_READ;
	sndbuf[1] = (addr << 1) | (wo ? 0 : 1);

	memcpy(&sndbuf[2],wbuf,wlen);

	if (!wo) {
		sndbuf[wlen+2] = (rlen >> 8) & 0xff;
		sndbuf[wlen+3] = rlen & 0xff;
	}

	return dvb_usb_generic_rw(d,sndbuf,len,rbuf,rlen,0);
}

/*
 * I2C master xfer function
 */
static int dibusb_i2c_xfer(struct i2c_adapter *adap,struct i2c_msg msg[],int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int i;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	for (i = 0; i < num; i++) {
		/* write/read request */
		if (i+1 < num && (msg[i].flags & I2C_M_RD) == 0
					  && (msg[i+1].flags & I2C_M_RD)) {
			if (dibusb_i2c_msg(d, msg[i].addr, msg[i].buf,msg[i].len,
						msg[i+1].buf,msg[i+1].len) < 0)
				break;
			i++;
		} else if ((msg[i].flags & I2C_M_RD) == 0) {
			if (dibusb_i2c_msg(d, msg[i].addr, msg[i].buf,msg[i].len,NULL,0) < 0)
				break;
		} else
			break;
	}

	mutex_unlock(&d->i2c_mutex);
	return i;
}

static u32 dibusb_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

struct i2c_algorithm dibusb_i2c_algo = {
	.master_xfer   = dibusb_i2c_xfer,
	.functionality = dibusb_i2c_func,
};
EXPORT_SYMBOL(dibusb_i2c_algo);

int dibusb_read_eeprom_byte(struct dvb_usb_device *d, u8 offs, u8 *val)
{
	u8 wbuf[1] = { offs };
	return dibusb_i2c_msg(d, 0x50, wbuf, 1, val, 1);
}
EXPORT_SYMBOL(dibusb_read_eeprom_byte);

/* 3000MC/P stuff */
// Config Adjacent channels  Perf -cal22
static struct dibx000_agc_config dib3000p_mt2060_agc_config = {
	.band_caps = BAND_VHF | BAND_UHF,
	.setup     = (1 << 8) | (5 << 5) | (1 << 4) | (1 << 3) | (0 << 2) | (2 << 0),

	.agc1_max = 48497,
	.agc1_min = 23593,
	.agc2_max = 46531,
	.agc2_min = 24904,

	.agc1_pt1 = 0x65,
	.agc1_pt2 = 0x69,

	.agc1_slope1 = 0x51,
	.agc1_slope2 = 0x27,

	.agc2_pt1 = 0,
	.agc2_pt2 = 0x33,

	.agc2_slope1 = 0x35,
	.agc2_slope2 = 0x37,
};

static struct dib3000mc_config stk3000p_dib3000p_config = {
	&dib3000p_mt2060_agc_config,

	.max_time     = 0x196,
	.ln_adc_level = 0x1cc7,

	.output_mpeg2_in_188_bytes = 1,

	.agc_command1 = 1,
	.agc_command2 = 1,
};

static struct dibx000_agc_config dib3000p_panasonic_agc_config = {
	.band_caps = BAND_VHF | BAND_UHF,
	.setup     = (1 << 8) | (5 << 5) | (1 << 4) | (1 << 3) | (0 << 2) | (2 << 0),

	.agc1_max = 56361,
	.agc1_min = 22282,
	.agc2_max = 47841,
	.agc2_min = 36045,

	.agc1_pt1 = 0x3b,
	.agc1_pt2 = 0x6b,

	.agc1_slope1 = 0x55,
	.agc1_slope2 = 0x1d,

	.agc2_pt1 = 0,
	.agc2_pt2 = 0x0a,

	.agc2_slope1 = 0x95,
	.agc2_slope2 = 0x1e,
};

#if defined(CONFIG_DVB_DIB3000MC) || 					\
	(defined(CONFIG_DVB_DIB3000MC_MODULE) && defined(MODULE))

static struct dib3000mc_config mod3000p_dib3000p_config = {
	&dib3000p_panasonic_agc_config,

	.max_time     = 0x51,
	.ln_adc_level = 0x1cc7,

	.output_mpeg2_in_188_bytes = 1,

	.agc_command1 = 1,
	.agc_command2 = 1,
};

int dibusb_dib3000mc_frontend_attach(struct dvb_usb_adapter *adap)
{
	if ((adap->fe = dvb_attach(dib3000mc_attach, &adap->dev->i2c_adap, DEFAULT_DIB3000P_I2C_ADDRESS,  &mod3000p_dib3000p_config)) != NULL ||
		(adap->fe = dvb_attach(dib3000mc_attach, &adap->dev->i2c_adap, DEFAULT_DIB3000MC_I2C_ADDRESS, &mod3000p_dib3000p_config)) != NULL) {
		if (adap->priv != NULL) {
			struct dibusb_state *st = adap->priv;
			st->ops.pid_parse = dib3000mc_pid_parse;
			st->ops.pid_ctrl  = dib3000mc_pid_control;
		}
		return 0;
	}
	return -ENODEV;
}
EXPORT_SYMBOL(dibusb_dib3000mc_frontend_attach);

static struct mt2060_config stk3000p_mt2060_config = {
	0x60
};

int dibusb_dib3000mc_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct dibusb_state *st = adap->priv;
	u8 a,b;
	u16 if1 = 1220;
	struct i2c_adapter *tun_i2c;

	// First IF calibration for Liteon Sticks
	if (adap->dev->udev->descriptor.idVendor  == USB_VID_LITEON &&
		adap->dev->udev->descriptor.idProduct == USB_PID_LITEON_DVB_T_WARM) {

		dibusb_read_eeprom_byte(adap->dev,0x7E,&a);
		dibusb_read_eeprom_byte(adap->dev,0x7F,&b);

		if (a == 0x00)
			if1 += b;
		else if (a == 0x80)
			if1 -= b;
		else
			warn("LITE-ON DVB-T: Strange IF1 calibration :%2X %2X\n", a, b);

	} else if (adap->dev->udev->descriptor.idVendor  == USB_VID_DIBCOM &&
		   adap->dev->udev->descriptor.idProduct == USB_PID_DIBCOM_MOD3001_WARM) {
		u8 desc;
		dibusb_read_eeprom_byte(adap->dev, 7, &desc);
		if (desc == 2) {
			a = 127;
			do {
				dibusb_read_eeprom_byte(adap->dev, a, &desc);
				a--;
			} while (a > 7 && (desc == 0xff || desc == 0x00));
			if (desc & 0x80)
				if1 -= (0xff - desc);
			else
				if1 += desc;
		}
	}

	tun_i2c = dib3000mc_get_tuner_i2c_master(adap->fe, 1);
	if (dvb_attach(mt2060_attach, adap->fe, tun_i2c, &stk3000p_mt2060_config, if1) == NULL) {
		/* not found - use panasonic pll parameters */
		if (dvb_attach(dvb_pll_attach, adap->fe, 0x60, tun_i2c, DVB_PLL_ENV57H1XD5) == NULL)
			return -ENOMEM;
	} else {
		st->mt2060_present = 1;
		/* set the correct parameters for the dib3000p */
		dib3000mc_set_config(adap->fe, &stk3000p_dib3000p_config);
	}
	return 0;
}
EXPORT_SYMBOL(dibusb_dib3000mc_tuner_attach);
#endif

/*
 * common remote control stuff
 */
struct dvb_usb_rc_key dibusb_rc_keys[] = {
	/* Key codes for the little Artec T1/Twinhan/HAMA/ remote. */
	{ 0x00, 0x16, KEY_POWER },
	{ 0x00, 0x10, KEY_MUTE },
	{ 0x00, 0x03, KEY_1 },
	{ 0x00, 0x01, KEY_2 },
	{ 0x00, 0x06, KEY_3 },
	{ 0x00, 0x09, KEY_4 },
	{ 0x00, 0x1d, KEY_5 },
	{ 0x00, 0x1f, KEY_6 },
	{ 0x00, 0x0d, KEY_7 },
	{ 0x00, 0x19, KEY_8 },
	{ 0x00, 0x1b, KEY_9 },
	{ 0x00, 0x15, KEY_0 },
	{ 0x00, 0x05, KEY_CHANNELUP },
	{ 0x00, 0x02, KEY_CHANNELDOWN },
	{ 0x00, 0x1e, KEY_VOLUMEUP },
	{ 0x00, 0x0a, KEY_VOLUMEDOWN },
	{ 0x00, 0x11, KEY_RECORD },
	{ 0x00, 0x17, KEY_FAVORITES }, /* Heart symbol - Channel list. */
	{ 0x00, 0x14, KEY_PLAY },
	{ 0x00, 0x1a, KEY_STOP },
	{ 0x00, 0x40, KEY_REWIND },
	{ 0x00, 0x12, KEY_FASTFORWARD },
	{ 0x00, 0x0e, KEY_PREVIOUS }, /* Recall - Previous channel. */
	{ 0x00, 0x4c, KEY_PAUSE },
	{ 0x00, 0x4d, KEY_SCREEN }, /* Full screen mode. */
	{ 0x00, 0x54, KEY_AUDIO }, /* MTS - Switch to secondary audio. */
	/* additional keys TwinHan VisionPlus, the Artec seemingly not have */
	{ 0x00, 0x0c, KEY_CANCEL }, /* Cancel */
	{ 0x00, 0x1c, KEY_EPG }, /* EPG */
	{ 0x00, 0x00, KEY_TAB }, /* Tab */
	{ 0x00, 0x48, KEY_INFO }, /* Preview */
	{ 0x00, 0x04, KEY_LIST }, /* RecordList */
	{ 0x00, 0x0f, KEY_TEXT }, /* Teletext */
	/* Key codes for the KWorld/ADSTech/JetWay remote. */
	{ 0x86, 0x12, KEY_POWER },
	{ 0x86, 0x0f, KEY_SELECT }, /* source */
	{ 0x86, 0x0c, KEY_UNKNOWN }, /* scan */
	{ 0x86, 0x0b, KEY_EPG },
	{ 0x86, 0x10, KEY_MUTE },
	{ 0x86, 0x01, KEY_1 },
	{ 0x86, 0x02, KEY_2 },
	{ 0x86, 0x03, KEY_3 },
	{ 0x86, 0x04, KEY_4 },
	{ 0x86, 0x05, KEY_5 },
	{ 0x86, 0x06, KEY_6 },
	{ 0x86, 0x07, KEY_7 },
	{ 0x86, 0x08, KEY_8 },
	{ 0x86, 0x09, KEY_9 },
	{ 0x86, 0x0a, KEY_0 },
	{ 0x86, 0x18, KEY_ZOOM },
	{ 0x86, 0x1c, KEY_UNKNOWN }, /* preview */
	{ 0x86, 0x13, KEY_UNKNOWN }, /* snap */
	{ 0x86, 0x00, KEY_UNDO },
	{ 0x86, 0x1d, KEY_RECORD },
	{ 0x86, 0x0d, KEY_STOP },
	{ 0x86, 0x0e, KEY_PAUSE },
	{ 0x86, 0x16, KEY_PLAY },
	{ 0x86, 0x11, KEY_BACK },
	{ 0x86, 0x19, KEY_FORWARD },
	{ 0x86, 0x14, KEY_UNKNOWN }, /* pip */
	{ 0x86, 0x15, KEY_ESC },
	{ 0x86, 0x1a, KEY_UP },
	{ 0x86, 0x1e, KEY_DOWN },
	{ 0x86, 0x1f, KEY_LEFT },
	{ 0x86, 0x1b, KEY_RIGHT },

	/* Key codes for the DiBcom MOD3000 remote. */
	{ 0x80, 0x00, KEY_MUTE },
	{ 0x80, 0x01, KEY_TEXT },
	{ 0x80, 0x02, KEY_HOME },
	{ 0x80, 0x03, KEY_POWER },

	{ 0x80, 0x04, KEY_RED },
	{ 0x80, 0x05, KEY_GREEN },
	{ 0x80, 0x06, KEY_YELLOW },
	{ 0x80, 0x07, KEY_BLUE },

	{ 0x80, 0x08, KEY_DVD },
	{ 0x80, 0x09, KEY_AUDIO },
	{ 0x80, 0x0a, KEY_MEDIA },      /* Pictures */
	{ 0x80, 0x0b, KEY_VIDEO },

	{ 0x80, 0x0c, KEY_BACK },
	{ 0x80, 0x0d, KEY_UP },
	{ 0x80, 0x0e, KEY_RADIO },
	{ 0x80, 0x0f, KEY_EPG },

	{ 0x80, 0x10, KEY_LEFT },
	{ 0x80, 0x11, KEY_OK },
	{ 0x80, 0x12, KEY_RIGHT },
	{ 0x80, 0x13, KEY_UNKNOWN },    /* SAP */

	{ 0x80, 0x14, KEY_TV },
	{ 0x80, 0x15, KEY_DOWN },
	{ 0x80, 0x16, KEY_MENU },       /* DVD Menu */
	{ 0x80, 0x17, KEY_LAST },

	{ 0x80, 0x18, KEY_RECORD },
	{ 0x80, 0x19, KEY_STOP },
	{ 0x80, 0x1a, KEY_PAUSE },
	{ 0x80, 0x1b, KEY_PLAY },

	{ 0x80, 0x1c, KEY_PREVIOUS },
	{ 0x80, 0x1d, KEY_REWIND },
	{ 0x80, 0x1e, KEY_FASTFORWARD },
	{ 0x80, 0x1f, KEY_NEXT},

	{ 0x80, 0x40, KEY_1 },
	{ 0x80, 0x41, KEY_2 },
	{ 0x80, 0x42, KEY_3 },
	{ 0x80, 0x43, KEY_CHANNELUP },

	{ 0x80, 0x44, KEY_4 },
	{ 0x80, 0x45, KEY_5 },
	{ 0x80, 0x46, KEY_6 },
	{ 0x80, 0x47, KEY_CHANNELDOWN },

	{ 0x80, 0x48, KEY_7 },
	{ 0x80, 0x49, KEY_8 },
	{ 0x80, 0x4a, KEY_9 },
	{ 0x80, 0x4b, KEY_VOLUMEUP },

	{ 0x80, 0x4c, KEY_CLEAR },
	{ 0x80, 0x4d, KEY_0 },
	{ 0x80, 0x4e, KEY_ENTER },
	{ 0x80, 0x4f, KEY_VOLUMEDOWN },
};
EXPORT_SYMBOL(dibusb_rc_keys);

int dibusb_rc_query(struct dvb_usb_device *d, u32 *event, int *state)
{
	u8 key[5],cmd = DIBUSB_REQ_POLL_REMOTE;
	dvb_usb_generic_rw(d,&cmd,1,key,5,0);
	dvb_usb_nec_rc_key_to_event(d,key,event,state);
	if (key[0] != 0)
		deb_info("key: %x %x %x %x %x\n",key[0],key[1],key[2],key[3],key[4]);
	return 0;
}
EXPORT_SYMBOL(dibusb_rc_query);
