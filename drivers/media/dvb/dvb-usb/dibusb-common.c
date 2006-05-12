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
int dibusb_streaming_ctrl(struct dvb_usb_device *d, int onoff)
{
	if (d->priv != NULL) {
		struct dibusb_state *st = d->priv;
		if (st->ops.fifo_ctrl != NULL)
			if (st->ops.fifo_ctrl(d->fe,onoff)) {
				err("error while controlling the fifo of the demod.");
				return -ENODEV;
			}
	}
	return 0;
}
EXPORT_SYMBOL(dibusb_streaming_ctrl);

int dibusb_pid_filter(struct dvb_usb_device *d, int index, u16 pid, int onoff)
{
	if (d->priv != NULL) {
		struct dibusb_state *st = d->priv;
		if (st->ops.pid_ctrl != NULL)
			st->ops.pid_ctrl(d->fe,index,pid,onoff);
	}
	return 0;
}
EXPORT_SYMBOL(dibusb_pid_filter);

int dibusb_pid_filter_ctrl(struct dvb_usb_device *d, int onoff)
{
	if (d->priv != NULL) {
		struct dibusb_state *st = d->priv;
		if (st->ops.pid_parse != NULL)
			if (st->ops.pid_parse(d->fe,onoff) < 0)
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

int dibusb2_0_streaming_ctrl(struct dvb_usb_device *d, int onoff)
{
	u8 b[3] = { 0 };
	int ret;

	if ((ret = dibusb_streaming_ctrl(d,onoff)) < 0)
		return ret;

	if (onoff) {
		b[0] = DIBUSB_REQ_SET_STREAMING_MODE;
		b[1] = 0x00;
		if ((ret = dvb_usb_generic_write(d,b,2)) < 0)
			return ret;
	}

	b[0] = DIBUSB_REQ_SET_IOCTL;
	b[1] = onoff ? DIBUSB_IOCTL_CMD_ENABLE_STREAM : DIBUSB_IOCTL_CMD_DISABLE_STREAM;
	return dvb_usb_generic_write(d,b,3);
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

	if (num > 2)
		warn("more than 2 i2c messages at a time is not handled yet. TODO.");

	for (i = 0; i < num; i++) {
		/* write/read request */
		if (i+1 < num && (msg[i+1].flags & I2C_M_RD)) {
			if (dibusb_i2c_msg(d, msg[i].addr, msg[i].buf,msg[i].len,
						msg[i+1].buf,msg[i+1].len) < 0)
				break;
			i++;
		} else
			if (dibusb_i2c_msg(d, msg[i].addr, msg[i].buf,msg[i].len,NULL,0) < 0)
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

int dibusb_dib3000mc_frontend_attach(struct dvb_usb_device *d)
{
	struct dib3000_config demod_cfg;
	struct dibusb_state *st = d->priv;

	for (demod_cfg.demod_address = 0x8; demod_cfg.demod_address < 0xd; demod_cfg.demod_address++)
		if ((d->fe = dib3000mc_attach(&demod_cfg,&d->i2c_adap,&st->ops)) != NULL) {
			d->fe->ops->tuner_ops.init = dvb_usb_tuner_init_i2c;
			d->fe->ops->tuner_ops.set_params = dvb_usb_tuner_set_params_i2c;
			d->tuner_pass_ctrl = st->ops.tuner_pass_ctrl;
			return 0;
		}

	return -ENODEV;
}
EXPORT_SYMBOL(dibusb_dib3000mc_frontend_attach);

int dibusb_dib3000mc_tuner_attach (struct dvb_usb_device *d)
{
	d->pll_addr = 0x60;
	d->pll_desc = &dvb_pll_env57h1xd5;
	return 0;
}
EXPORT_SYMBOL(dibusb_dib3000mc_tuner_attach);

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
