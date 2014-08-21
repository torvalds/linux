/* DVB USB compliant linux driver for Conexant USB reference design.
 *
 * The Conexant reference design I saw on their website was only for analogue
 * capturing (using the cx25842). The box I took to write this driver (reverse
 * engineered) is the one labeled Medion MD95700. In addition to the cx25842
 * for analogue capturing it also has a cx22702 DVB-T demodulator on the main
 * board. Besides it has a atiremote (X10) and a USB2.0 hub onboard.
 *
 * Maybe it is a little bit premature to call this driver cxusb, but I assume
 * the USB protocol is identical or at least inherited from the reference
 * design, so it can be reused for the "analogue-only" device (if it will
 * appear at all).
 *
 * TODO: Use the cx25840-driver for the analogue part
 *
 * Copyright (C) 2005 Patrick Boettcher (patrick.boettcher@desy.de)
 * Copyright (C) 2006 Michael Krufky (mkrufky@linuxtv.org)
 * Copyright (C) 2006, 2007 Chris Pascoe (c.pascoe@itee.uq.edu.au)
 *
 *   This program is free software; you can redistribute it and/or modify it
 *   under the terms of the GNU General Public License as published by the Free
 *   Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#include <media/tuner.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

#include "cxusb.h"

#include "cx22702.h"
#include "lgdt330x.h"
#include "mt352.h"
#include "mt352_priv.h"
#include "zl10353.h"
#include "tuner-xc2028.h"
#include "tuner-simple.h"
#include "mxl5005s.h"
#include "max2165.h"
#include "dib7000p.h"
#include "dib0070.h"
#include "lgs8gxx.h"
#include "atbm8830.h"
#include "si2168.h"
#include "si2157.h"
#include "sp2.h"

/* Max transfer size done by I2C transfer functions */
#define MAX_XFER_SIZE  80

/* debug */
static int dvb_usb_cxusb_debug;
module_param_named(debug, dvb_usb_cxusb_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=rc (or-able))." DVB_USB_DEBUG_STATUS);

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

#define deb_info(args...)   dprintk(dvb_usb_cxusb_debug, 0x03, args)
#define deb_i2c(args...)    dprintk(dvb_usb_cxusb_debug, 0x02, args)

static int cxusb_ctrl_msg(struct dvb_usb_device *d,
			  u8 cmd, u8 *wbuf, int wlen, u8 *rbuf, int rlen)
{
	int wo = (rbuf == NULL || rlen == 0); /* write-only */
	u8 sndbuf[MAX_XFER_SIZE];

	if (1 + wlen > sizeof(sndbuf)) {
		warn("i2c wr: len=%d is too big!\n",
		     wlen);
		return -EOPNOTSUPP;
	}

	memset(sndbuf, 0, 1+wlen);

	sndbuf[0] = cmd;
	memcpy(&sndbuf[1], wbuf, wlen);
	if (wo)
		return dvb_usb_generic_write(d, sndbuf, 1+wlen);
	else
		return dvb_usb_generic_rw(d, sndbuf, 1+wlen, rbuf, rlen, 0);
}

/* GPIO */
static void cxusb_gpio_tuner(struct dvb_usb_device *d, int onoff)
{
	struct cxusb_state *st = d->priv;
	u8 o[2], i;

	if (st->gpio_write_state[GPIO_TUNER] == onoff)
		return;

	o[0] = GPIO_TUNER;
	o[1] = onoff;
	cxusb_ctrl_msg(d, CMD_GPIO_WRITE, o, 2, &i, 1);

	if (i != 0x01)
		deb_info("gpio_write failed.\n");

	st->gpio_write_state[GPIO_TUNER] = onoff;
}

static int cxusb_bluebird_gpio_rw(struct dvb_usb_device *d, u8 changemask,
				 u8 newval)
{
	u8 o[2], gpio_state;
	int rc;

	o[0] = 0xff & ~changemask;	/* mask of bits to keep */
	o[1] = newval & changemask;	/* new values for bits  */

	rc = cxusb_ctrl_msg(d, CMD_BLUEBIRD_GPIO_RW, o, 2, &gpio_state, 1);
	if (rc < 0 || (gpio_state & changemask) != (newval & changemask))
		deb_info("bluebird_gpio_write failed.\n");

	return rc < 0 ? rc : gpio_state;
}

static void cxusb_bluebird_gpio_pulse(struct dvb_usb_device *d, u8 pin, int low)
{
	cxusb_bluebird_gpio_rw(d, pin, low ? 0 : pin);
	msleep(5);
	cxusb_bluebird_gpio_rw(d, pin, low ? pin : 0);
}

static void cxusb_nano2_led(struct dvb_usb_device *d, int onoff)
{
	cxusb_bluebird_gpio_rw(d, 0x40, onoff ? 0 : 0x40);
}

static int cxusb_d680_dmb_gpio_tuner(struct dvb_usb_device *d,
		u8 addr, int onoff)
{
	u8  o[2] = {addr, onoff};
	u8  i;
	int rc;

	rc = cxusb_ctrl_msg(d, CMD_GPIO_WRITE, o, 2, &i, 1);

	if (rc < 0)
		return rc;
	if (i == 0x01)
		return 0;
	else {
		deb_info("gpio_write failed.\n");
		return -EIO;
	}
}

static int cxusb_tt_ct2_4400_gpio_tuner(struct dvb_usb_device *d, int onoff)
{
	u8 o[2], i;
	int rc;

	o[0] = 0x83;
	o[1] = onoff;
	rc = cxusb_ctrl_msg(d, CMD_GPIO_WRITE, o, 2, &i, 1);

	if (rc) {
		deb_info("gpio_write failed.\n");
		return -EIO;
	}
	return 0;
}

/* I2C */
static int cxusb_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msg[],
			  int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int ret;
	int i;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	for (i = 0; i < num; i++) {

		if (le16_to_cpu(d->udev->descriptor.idVendor) == USB_VID_MEDION)
			switch (msg[i].addr) {
			case 0x63:
				cxusb_gpio_tuner(d, 0);
				break;
			default:
				cxusb_gpio_tuner(d, 1);
				break;
			}

		if (msg[i].flags & I2C_M_RD) {
			/* read only */
			u8 obuf[3], ibuf[MAX_XFER_SIZE];

			if (1 + msg[i].len > sizeof(ibuf)) {
				warn("i2c rd: len=%d is too big!\n",
				     msg[i].len);
				ret = -EOPNOTSUPP;
				goto unlock;
			}
			obuf[0] = 0;
			obuf[1] = msg[i].len;
			obuf[2] = msg[i].addr;
			if (cxusb_ctrl_msg(d, CMD_I2C_READ,
					   obuf, 3,
					   ibuf, 1+msg[i].len) < 0) {
				warn("i2c read failed");
				break;
			}
			memcpy(msg[i].buf, &ibuf[1], msg[i].len);
		} else if (i+1 < num && (msg[i+1].flags & I2C_M_RD) &&
			   msg[i].addr == msg[i+1].addr) {
			/* write to then read from same address */
			u8 obuf[MAX_XFER_SIZE], ibuf[MAX_XFER_SIZE];

			if (3 + msg[i].len > sizeof(obuf)) {
				warn("i2c wr: len=%d is too big!\n",
				     msg[i].len);
				ret = -EOPNOTSUPP;
				goto unlock;
			}
			if (1 + msg[i + 1].len > sizeof(ibuf)) {
				warn("i2c rd: len=%d is too big!\n",
				     msg[i + 1].len);
				ret = -EOPNOTSUPP;
				goto unlock;
			}
			obuf[0] = msg[i].len;
			obuf[1] = msg[i+1].len;
			obuf[2] = msg[i].addr;
			memcpy(&obuf[3], msg[i].buf, msg[i].len);

			if (cxusb_ctrl_msg(d, CMD_I2C_READ,
					   obuf, 3+msg[i].len,
					   ibuf, 1+msg[i+1].len) < 0)
				break;

			if (ibuf[0] != 0x08)
				deb_i2c("i2c read may have failed\n");

			memcpy(msg[i+1].buf, &ibuf[1], msg[i+1].len);

			i++;
		} else {
			/* write only */
			u8 obuf[MAX_XFER_SIZE], ibuf;

			if (2 + msg[i].len > sizeof(obuf)) {
				warn("i2c wr: len=%d is too big!\n",
				     msg[i].len);
				ret = -EOPNOTSUPP;
				goto unlock;
			}
			obuf[0] = msg[i].addr;
			obuf[1] = msg[i].len;
			memcpy(&obuf[2], msg[i].buf, msg[i].len);

			if (cxusb_ctrl_msg(d, CMD_I2C_WRITE, obuf,
					   2+msg[i].len, &ibuf,1) < 0)
				break;
			if (ibuf != 0x08)
				deb_i2c("i2c write may have failed\n");
		}
	}

	if (i == num)
		ret = num;
	else
		ret = -EREMOTEIO;

unlock:
	mutex_unlock(&d->i2c_mutex);
	return ret;
}

static u32 cxusb_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm cxusb_i2c_algo = {
	.master_xfer   = cxusb_i2c_xfer,
	.functionality = cxusb_i2c_func,
};

static int cxusb_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	u8 b = 0;
	if (onoff)
		return cxusb_ctrl_msg(d, CMD_POWER_ON, &b, 1, NULL, 0);
	else
		return cxusb_ctrl_msg(d, CMD_POWER_OFF, &b, 1, NULL, 0);
}

static int cxusb_aver_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	int ret;
	if (!onoff)
		return cxusb_ctrl_msg(d, CMD_POWER_OFF, NULL, 0, NULL, 0);
	if (d->state == DVB_USB_STATE_INIT &&
	    usb_set_interface(d->udev, 0, 0) < 0)
		err("set interface failed");
	do {} while (!(ret = cxusb_ctrl_msg(d, CMD_POWER_ON, NULL, 0, NULL, 0)) &&
		   !(ret = cxusb_ctrl_msg(d, 0x15, NULL, 0, NULL, 0)) &&
		   !(ret = cxusb_ctrl_msg(d, 0x17, NULL, 0, NULL, 0)) && 0);
	if (!ret) {
		/* FIXME: We don't know why, but we need to configure the
		 * lgdt3303 with the register settings below on resume */
		int i;
		u8 buf, bufs[] = {
			0x0e, 0x2, 0x00, 0x7f,
			0x0e, 0x2, 0x02, 0xfe,
			0x0e, 0x2, 0x02, 0x01,
			0x0e, 0x2, 0x00, 0x03,
			0x0e, 0x2, 0x0d, 0x40,
			0x0e, 0x2, 0x0e, 0x87,
			0x0e, 0x2, 0x0f, 0x8e,
			0x0e, 0x2, 0x10, 0x01,
			0x0e, 0x2, 0x14, 0xd7,
			0x0e, 0x2, 0x47, 0x88,
		};
		msleep(20);
		for (i = 0; i < sizeof(bufs)/sizeof(u8); i += 4/sizeof(u8)) {
			ret = cxusb_ctrl_msg(d, CMD_I2C_WRITE,
					     bufs+i, 4, &buf, 1);
			if (ret)
				break;
			if (buf != 0x8)
				return -EREMOTEIO;
		}
	}
	return ret;
}

static int cxusb_bluebird_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	u8 b = 0;
	if (onoff)
		return cxusb_ctrl_msg(d, CMD_POWER_ON, &b, 1, NULL, 0);
	else
		return 0;
}

static int cxusb_nano2_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	int rc = 0;

	rc = cxusb_power_ctrl(d, onoff);
	if (!onoff)
		cxusb_nano2_led(d, 0);

	return rc;
}

static int cxusb_d680_dmb_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	int ret;
	u8  b;
	ret = cxusb_power_ctrl(d, onoff);
	if (!onoff)
		return ret;

	msleep(128);
	cxusb_ctrl_msg(d, CMD_DIGITAL, NULL, 0, &b, 1);
	msleep(100);
	return ret;
}

static int cxusb_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	u8 buf[2] = { 0x03, 0x00 };
	if (onoff)
		cxusb_ctrl_msg(adap->dev, CMD_STREAMING_ON, buf, 2, NULL, 0);
	else
		cxusb_ctrl_msg(adap->dev, CMD_STREAMING_OFF, NULL, 0, NULL, 0);

	return 0;
}

static int cxusb_aver_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	if (onoff)
		cxusb_ctrl_msg(adap->dev, CMD_AVER_STREAM_ON, NULL, 0, NULL, 0);
	else
		cxusb_ctrl_msg(adap->dev, CMD_AVER_STREAM_OFF,
			       NULL, 0, NULL, 0);
	return 0;
}

static void cxusb_d680_dmb_drain_message(struct dvb_usb_device *d)
{
	int       ep = d->props.generic_bulk_ctrl_endpoint;
	const int timeout = 100;
	const int junk_len = 32;
	u8        *junk;
	int       rd_count;

	/* Discard remaining data in video pipe */
	junk = kmalloc(junk_len, GFP_KERNEL);
	if (!junk)
		return;
	while (1) {
		if (usb_bulk_msg(d->udev,
			usb_rcvbulkpipe(d->udev, ep),
			junk, junk_len, &rd_count, timeout) < 0)
			break;
		if (!rd_count)
			break;
	}
	kfree(junk);
}

static void cxusb_d680_dmb_drain_video(struct dvb_usb_device *d)
{
	struct usb_data_stream_properties *p = &d->props.adapter[0].fe[0].stream;
	const int timeout = 100;
	const int junk_len = p->u.bulk.buffersize;
	u8        *junk;
	int       rd_count;

	/* Discard remaining data in video pipe */
	junk = kmalloc(junk_len, GFP_KERNEL);
	if (!junk)
		return;
	while (1) {
		if (usb_bulk_msg(d->udev,
			usb_rcvbulkpipe(d->udev, p->endpoint),
			junk, junk_len, &rd_count, timeout) < 0)
			break;
		if (!rd_count)
			break;
	}
	kfree(junk);
}

static int cxusb_d680_dmb_streaming_ctrl(
		struct dvb_usb_adapter *adap, int onoff)
{
	if (onoff) {
		u8 buf[2] = { 0x03, 0x00 };
		cxusb_d680_dmb_drain_video(adap->dev);
		return cxusb_ctrl_msg(adap->dev, CMD_STREAMING_ON,
			buf, sizeof(buf), NULL, 0);
	} else {
		int ret = cxusb_ctrl_msg(adap->dev,
			CMD_STREAMING_OFF, NULL, 0, NULL, 0);
		return ret;
	}
}

static int cxusb_rc_query(struct dvb_usb_device *d, u32 *event, int *state)
{
	struct rc_map_table *keymap = d->props.rc.legacy.rc_map_table;
	u8 ircode[4];
	int i;

	cxusb_ctrl_msg(d, CMD_GET_IR_CODE, NULL, 0, ircode, 4);

	*event = 0;
	*state = REMOTE_NO_KEY_PRESSED;

	for (i = 0; i < d->props.rc.legacy.rc_map_size; i++) {
		if (rc5_custom(&keymap[i]) == ircode[2] &&
		    rc5_data(&keymap[i]) == ircode[3]) {
			*event = keymap[i].keycode;
			*state = REMOTE_KEY_PRESSED;

			return 0;
		}
	}

	return 0;
}

static int cxusb_bluebird2_rc_query(struct dvb_usb_device *d, u32 *event,
				    int *state)
{
	struct rc_map_table *keymap = d->props.rc.legacy.rc_map_table;
	u8 ircode[4];
	int i;
	struct i2c_msg msg = { .addr = 0x6b, .flags = I2C_M_RD,
			       .buf = ircode, .len = 4 };

	*event = 0;
	*state = REMOTE_NO_KEY_PRESSED;

	if (cxusb_i2c_xfer(&d->i2c_adap, &msg, 1) != 1)
		return 0;

	for (i = 0; i < d->props.rc.legacy.rc_map_size; i++) {
		if (rc5_custom(&keymap[i]) == ircode[1] &&
		    rc5_data(&keymap[i]) == ircode[2]) {
			*event = keymap[i].keycode;
			*state = REMOTE_KEY_PRESSED;

			return 0;
		}
	}

	return 0;
}

static int cxusb_d680_dmb_rc_query(struct dvb_usb_device *d, u32 *event,
		int *state)
{
	struct rc_map_table *keymap = d->props.rc.legacy.rc_map_table;
	u8 ircode[2];
	int i;

	*event = 0;
	*state = REMOTE_NO_KEY_PRESSED;

	if (cxusb_ctrl_msg(d, 0x10, NULL, 0, ircode, 2) < 0)
		return 0;

	for (i = 0; i < d->props.rc.legacy.rc_map_size; i++) {
		if (rc5_custom(&keymap[i]) == ircode[0] &&
		    rc5_data(&keymap[i]) == ircode[1]) {
			*event = keymap[i].keycode;
			*state = REMOTE_KEY_PRESSED;

			return 0;
		}
	}

	return 0;
}

static int cxusb_tt_ct2_4400_rc_query(struct dvb_usb_device *d)
{
	u8 i[2];
	int ret;
	u32 cmd, keycode;
	u8 rc5_cmd, rc5_addr, rc5_toggle;

	ret = cxusb_ctrl_msg(d, 0x10, NULL, 0, i, 2);
	if (ret)
		return ret;

	cmd = (i[0] << 8) | i[1];

	if (cmd != 0xffff) {
		rc5_cmd = cmd & 0x3F; /* bits 1-6 for command */
		rc5_addr = (cmd & 0x07C0) >> 6; /* bits 7-11 for address */
		rc5_toggle = (cmd & 0x0800) >> 11; /* bit 12 for toggle */
		keycode = (rc5_addr << 8) | rc5_cmd;
		rc_keydown(d->rc_dev, RC_BIT_RC5, keycode, rc5_toggle);
	}

	return 0;
}

static struct rc_map_table rc_map_dvico_mce_table[] = {
	{ 0xfe02, KEY_TV },
	{ 0xfe0e, KEY_MP3 },
	{ 0xfe1a, KEY_DVD },
	{ 0xfe1e, KEY_FAVORITES },
	{ 0xfe16, KEY_SETUP },
	{ 0xfe46, KEY_POWER2 },
	{ 0xfe0a, KEY_EPG },
	{ 0xfe49, KEY_BACK },
	{ 0xfe4d, KEY_MENU },
	{ 0xfe51, KEY_UP },
	{ 0xfe5b, KEY_LEFT },
	{ 0xfe5f, KEY_RIGHT },
	{ 0xfe53, KEY_DOWN },
	{ 0xfe5e, KEY_OK },
	{ 0xfe59, KEY_INFO },
	{ 0xfe55, KEY_TAB },
	{ 0xfe0f, KEY_PREVIOUSSONG },/* Replay */
	{ 0xfe12, KEY_NEXTSONG },	/* Skip */
	{ 0xfe42, KEY_ENTER	 },	/* Windows/Start */
	{ 0xfe15, KEY_VOLUMEUP },
	{ 0xfe05, KEY_VOLUMEDOWN },
	{ 0xfe11, KEY_CHANNELUP },
	{ 0xfe09, KEY_CHANNELDOWN },
	{ 0xfe52, KEY_CAMERA },
	{ 0xfe5a, KEY_TUNER },	/* Live */
	{ 0xfe19, KEY_OPEN },
	{ 0xfe0b, KEY_1 },
	{ 0xfe17, KEY_2 },
	{ 0xfe1b, KEY_3 },
	{ 0xfe07, KEY_4 },
	{ 0xfe50, KEY_5 },
	{ 0xfe54, KEY_6 },
	{ 0xfe48, KEY_7 },
	{ 0xfe4c, KEY_8 },
	{ 0xfe58, KEY_9 },
	{ 0xfe13, KEY_ANGLE },	/* Aspect */
	{ 0xfe03, KEY_0 },
	{ 0xfe1f, KEY_ZOOM },
	{ 0xfe43, KEY_REWIND },
	{ 0xfe47, KEY_PLAYPAUSE },
	{ 0xfe4f, KEY_FASTFORWARD },
	{ 0xfe57, KEY_MUTE },
	{ 0xfe0d, KEY_STOP },
	{ 0xfe01, KEY_RECORD },
	{ 0xfe4e, KEY_POWER },
};

static struct rc_map_table rc_map_dvico_portable_table[] = {
	{ 0xfc02, KEY_SETUP },       /* Profile */
	{ 0xfc43, KEY_POWER2 },
	{ 0xfc06, KEY_EPG },
	{ 0xfc5a, KEY_BACK },
	{ 0xfc05, KEY_MENU },
	{ 0xfc47, KEY_INFO },
	{ 0xfc01, KEY_TAB },
	{ 0xfc42, KEY_PREVIOUSSONG },/* Replay */
	{ 0xfc49, KEY_VOLUMEUP },
	{ 0xfc09, KEY_VOLUMEDOWN },
	{ 0xfc54, KEY_CHANNELUP },
	{ 0xfc0b, KEY_CHANNELDOWN },
	{ 0xfc16, KEY_CAMERA },
	{ 0xfc40, KEY_TUNER },	/* ATV/DTV */
	{ 0xfc45, KEY_OPEN },
	{ 0xfc19, KEY_1 },
	{ 0xfc18, KEY_2 },
	{ 0xfc1b, KEY_3 },
	{ 0xfc1a, KEY_4 },
	{ 0xfc58, KEY_5 },
	{ 0xfc59, KEY_6 },
	{ 0xfc15, KEY_7 },
	{ 0xfc14, KEY_8 },
	{ 0xfc17, KEY_9 },
	{ 0xfc44, KEY_ANGLE },	/* Aspect */
	{ 0xfc55, KEY_0 },
	{ 0xfc07, KEY_ZOOM },
	{ 0xfc0a, KEY_REWIND },
	{ 0xfc08, KEY_PLAYPAUSE },
	{ 0xfc4b, KEY_FASTFORWARD },
	{ 0xfc5b, KEY_MUTE },
	{ 0xfc04, KEY_STOP },
	{ 0xfc56, KEY_RECORD },
	{ 0xfc57, KEY_POWER },
	{ 0xfc41, KEY_UNKNOWN },    /* INPUT */
	{ 0xfc00, KEY_UNKNOWN },    /* HD */
};

static struct rc_map_table rc_map_d680_dmb_table[] = {
	{ 0x0038, KEY_UNKNOWN },	/* TV/AV */
	{ 0x080c, KEY_ZOOM },
	{ 0x0800, KEY_0 },
	{ 0x0001, KEY_1 },
	{ 0x0802, KEY_2 },
	{ 0x0003, KEY_3 },
	{ 0x0804, KEY_4 },
	{ 0x0005, KEY_5 },
	{ 0x0806, KEY_6 },
	{ 0x0007, KEY_7 },
	{ 0x0808, KEY_8 },
	{ 0x0009, KEY_9 },
	{ 0x000a, KEY_MUTE },
	{ 0x0829, KEY_BACK },
	{ 0x0012, KEY_CHANNELUP },
	{ 0x0813, KEY_CHANNELDOWN },
	{ 0x002b, KEY_VOLUMEUP },
	{ 0x082c, KEY_VOLUMEDOWN },
	{ 0x0020, KEY_UP },
	{ 0x0821, KEY_DOWN },
	{ 0x0011, KEY_LEFT },
	{ 0x0810, KEY_RIGHT },
	{ 0x000d, KEY_OK },
	{ 0x081f, KEY_RECORD },
	{ 0x0017, KEY_PLAYPAUSE },
	{ 0x0816, KEY_PLAYPAUSE },
	{ 0x000b, KEY_STOP },
	{ 0x0827, KEY_FASTFORWARD },
	{ 0x0026, KEY_REWIND },
	{ 0x081e, KEY_UNKNOWN },    /* Time Shift */
	{ 0x000e, KEY_UNKNOWN },    /* Snapshot */
	{ 0x082d, KEY_UNKNOWN },    /* Mouse Cursor */
	{ 0x000f, KEY_UNKNOWN },    /* Minimize/Maximize */
	{ 0x0814, KEY_UNKNOWN },    /* Shuffle */
	{ 0x0025, KEY_POWER },
};

static int cxusb_tt_ct2_4400_read_mac_address(struct dvb_usb_device *d, u8 mac[6])
{
	u8 wbuf[2];
	u8 rbuf[6];
	int ret;
	struct i2c_msg msg[] = {
		{
			.addr = 0x51,
			.flags = 0,
			.buf = wbuf,
			.len = 2,
		}, {
			.addr = 0x51,
			.flags = I2C_M_RD,
			.buf = rbuf,
			.len = 6,
		}
	};

	wbuf[0] = 0x1e;
	wbuf[1] = 0x00;
	ret = cxusb_i2c_xfer(&d->i2c_adap, msg, 2);

	if (ret == 2) {
		memcpy(mac, rbuf, 6);
		return 0;
	} else {
		if (ret < 0)
			return ret;
		return -EIO;
	}
}

static int cxusb_tt_ct2_4650_ci_ctrl(void *priv, u8 read, int addr,
					u8 data, int *mem)
{
	struct dvb_usb_device *d = priv;
	u8 wbuf[3];
	u8 rbuf[2];
	int ret;

	wbuf[0] = (addr >> 8) & 0xff;
	wbuf[1] = addr & 0xff;

	if (read) {
		ret = cxusb_ctrl_msg(d, CMD_SP2_CI_READ, wbuf, 2, rbuf, 2);
	} else {
		wbuf[2] = data;
		ret = cxusb_ctrl_msg(d, CMD_SP2_CI_WRITE, wbuf, 3, rbuf, 1);
	}

	if (ret)
		goto err;

	if (read)
		*mem = rbuf[1];

	return 0;
err:
	deb_info("%s: ci usb write returned %d\n", __func__, ret);
	return ret;

}

static int cxusb_dee1601_demod_init(struct dvb_frontend* fe)
{
	static u8 clock_config []  = { CLOCK_CTL,  0x38, 0x28 };
	static u8 reset []         = { RESET,      0x80 };
	static u8 adc_ctl_1_cfg [] = { ADC_CTL_1,  0x40 };
	static u8 agc_cfg []       = { AGC_TARGET, 0x28, 0x20 };
	static u8 gpp_ctl_cfg []   = { GPP_CTL,    0x33 };
	static u8 capt_range_cfg[] = { CAPT_RANGE, 0x32 };

	mt352_write(fe, clock_config,   sizeof(clock_config));
	udelay(200);
	mt352_write(fe, reset,          sizeof(reset));
	mt352_write(fe, adc_ctl_1_cfg,  sizeof(adc_ctl_1_cfg));

	mt352_write(fe, agc_cfg,        sizeof(agc_cfg));
	mt352_write(fe, gpp_ctl_cfg,    sizeof(gpp_ctl_cfg));
	mt352_write(fe, capt_range_cfg, sizeof(capt_range_cfg));

	return 0;
}

static int cxusb_mt352_demod_init(struct dvb_frontend* fe)
{	/* used in both lgz201 and th7579 */
	static u8 clock_config []  = { CLOCK_CTL,  0x38, 0x29 };
	static u8 reset []         = { RESET,      0x80 };
	static u8 adc_ctl_1_cfg [] = { ADC_CTL_1,  0x40 };
	static u8 agc_cfg []       = { AGC_TARGET, 0x24, 0x20 };
	static u8 gpp_ctl_cfg []   = { GPP_CTL,    0x33 };
	static u8 capt_range_cfg[] = { CAPT_RANGE, 0x32 };

	mt352_write(fe, clock_config,   sizeof(clock_config));
	udelay(200);
	mt352_write(fe, reset,          sizeof(reset));
	mt352_write(fe, adc_ctl_1_cfg,  sizeof(adc_ctl_1_cfg));

	mt352_write(fe, agc_cfg,        sizeof(agc_cfg));
	mt352_write(fe, gpp_ctl_cfg,    sizeof(gpp_ctl_cfg));
	mt352_write(fe, capt_range_cfg, sizeof(capt_range_cfg));
	return 0;
}

static struct cx22702_config cxusb_cx22702_config = {
	.demod_address = 0x63,
	.output_mode = CX22702_PARALLEL_OUTPUT,
};

static struct lgdt330x_config cxusb_lgdt3303_config = {
	.demod_address = 0x0e,
	.demod_chip    = LGDT3303,
};

static struct lgdt330x_config cxusb_aver_lgdt3303_config = {
	.demod_address       = 0x0e,
	.demod_chip          = LGDT3303,
	.clock_polarity_flip = 2,
};

static struct mt352_config cxusb_dee1601_config = {
	.demod_address = 0x0f,
	.demod_init    = cxusb_dee1601_demod_init,
};

static struct zl10353_config cxusb_zl10353_dee1601_config = {
	.demod_address = 0x0f,
	.parallel_ts = 1,
};

static struct mt352_config cxusb_mt352_config = {
	/* used in both lgz201 and th7579 */
	.demod_address = 0x0f,
	.demod_init    = cxusb_mt352_demod_init,
};

static struct zl10353_config cxusb_zl10353_xc3028_config = {
	.demod_address = 0x0f,
	.if2 = 45600,
	.no_tuner = 1,
	.parallel_ts = 1,
};

static struct zl10353_config cxusb_zl10353_xc3028_config_no_i2c_gate = {
	.demod_address = 0x0f,
	.if2 = 45600,
	.no_tuner = 1,
	.parallel_ts = 1,
	.disable_i2c_gate_ctrl = 1,
};

static struct mt352_config cxusb_mt352_xc3028_config = {
	.demod_address = 0x0f,
	.if2 = 4560,
	.no_tuner = 1,
	.demod_init = cxusb_mt352_demod_init,
};

/* FIXME: needs tweaking */
static struct mxl5005s_config aver_a868r_tuner = {
	.i2c_address     = 0x63,
	.if_freq         = 6000000UL,
	.xtal_freq       = CRYSTAL_FREQ_16000000HZ,
	.agc_mode        = MXL_SINGLE_AGC,
	.tracking_filter = MXL_TF_C,
	.rssi_enable     = MXL_RSSI_ENABLE,
	.cap_select      = MXL_CAP_SEL_ENABLE,
	.div_out         = MXL_DIV_OUT_4,
	.clock_out       = MXL_CLOCK_OUT_DISABLE,
	.output_load     = MXL5005S_IF_OUTPUT_LOAD_200_OHM,
	.top		 = MXL5005S_TOP_25P2,
	.mod_mode        = MXL_DIGITAL_MODE,
	.if_mode         = MXL_ZERO_IF,
	.AgcMasterByte   = 0x00,
};

/* FIXME: needs tweaking */
static struct mxl5005s_config d680_dmb_tuner = {
	.i2c_address     = 0x63,
	.if_freq         = 36125000UL,
	.xtal_freq       = CRYSTAL_FREQ_16000000HZ,
	.agc_mode        = MXL_SINGLE_AGC,
	.tracking_filter = MXL_TF_C,
	.rssi_enable     = MXL_RSSI_ENABLE,
	.cap_select      = MXL_CAP_SEL_ENABLE,
	.div_out         = MXL_DIV_OUT_4,
	.clock_out       = MXL_CLOCK_OUT_DISABLE,
	.output_load     = MXL5005S_IF_OUTPUT_LOAD_200_OHM,
	.top		 = MXL5005S_TOP_25P2,
	.mod_mode        = MXL_DIGITAL_MODE,
	.if_mode         = MXL_ZERO_IF,
	.AgcMasterByte   = 0x00,
};

static struct max2165_config mygica_d689_max2165_cfg = {
	.i2c_address = 0x60,
	.osc_clk = 20
};

/* Callbacks for DVB USB */
static int cxusb_fmd1216me_tuner_attach(struct dvb_usb_adapter *adap)
{
	dvb_attach(simple_tuner_attach, adap->fe_adap[0].fe,
		   &adap->dev->i2c_adap, 0x61,
		   TUNER_PHILIPS_FMD1216ME_MK3);
	return 0;
}

static int cxusb_dee1601_tuner_attach(struct dvb_usb_adapter *adap)
{
	dvb_attach(dvb_pll_attach, adap->fe_adap[0].fe, 0x61,
		   NULL, DVB_PLL_THOMSON_DTT7579);
	return 0;
}

static int cxusb_lgz201_tuner_attach(struct dvb_usb_adapter *adap)
{
	dvb_attach(dvb_pll_attach, adap->fe_adap[0].fe, 0x61, NULL, DVB_PLL_LG_Z201);
	return 0;
}

static int cxusb_dtt7579_tuner_attach(struct dvb_usb_adapter *adap)
{
	dvb_attach(dvb_pll_attach, adap->fe_adap[0].fe, 0x60,
		   NULL, DVB_PLL_THOMSON_DTT7579);
	return 0;
}

static int cxusb_lgh064f_tuner_attach(struct dvb_usb_adapter *adap)
{
	dvb_attach(simple_tuner_attach, adap->fe_adap[0].fe,
		   &adap->dev->i2c_adap, 0x61, TUNER_LG_TDVS_H06XF);
	return 0;
}

static int dvico_bluebird_xc2028_callback(void *ptr, int component,
					  int command, int arg)
{
	struct dvb_usb_adapter *adap = ptr;
	struct dvb_usb_device *d = adap->dev;

	switch (command) {
	case XC2028_TUNER_RESET:
		deb_info("%s: XC2028_TUNER_RESET %d\n", __func__, arg);
		cxusb_bluebird_gpio_pulse(d, 0x01, 1);
		break;
	case XC2028_RESET_CLK:
		deb_info("%s: XC2028_RESET_CLK %d\n", __func__, arg);
		break;
	default:
		deb_info("%s: unknown command %d, arg %d\n", __func__,
			 command, arg);
		return -EINVAL;
	}

	return 0;
}

static int cxusb_dvico_xc3028_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct dvb_frontend	 *fe;
	struct xc2028_config	  cfg = {
		.i2c_adap  = &adap->dev->i2c_adap,
		.i2c_addr  = 0x61,
	};
	static struct xc2028_ctrl ctl = {
		.fname       = XC2028_DEFAULT_FIRMWARE,
		.max_len     = 64,
		.demod       = XC3028_FE_ZARLINK456,
	};

	/* FIXME: generalize & move to common area */
	adap->fe_adap[0].fe->callback = dvico_bluebird_xc2028_callback;

	fe = dvb_attach(xc2028_attach, adap->fe_adap[0].fe, &cfg);
	if (fe == NULL || fe->ops.tuner_ops.set_config == NULL)
		return -EIO;

	fe->ops.tuner_ops.set_config(fe, &ctl);

	return 0;
}

static int cxusb_mxl5003s_tuner_attach(struct dvb_usb_adapter *adap)
{
	dvb_attach(mxl5005s_attach, adap->fe_adap[0].fe,
		   &adap->dev->i2c_adap, &aver_a868r_tuner);
	return 0;
}

static int cxusb_d680_dmb_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct dvb_frontend *fe;
	fe = dvb_attach(mxl5005s_attach, adap->fe_adap[0].fe,
			&adap->dev->i2c_adap, &d680_dmb_tuner);
	return (fe == NULL) ? -EIO : 0;
}

static int cxusb_mygica_d689_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct dvb_frontend *fe;
	fe = dvb_attach(max2165_attach, adap->fe_adap[0].fe,
			&adap->dev->i2c_adap, &mygica_d689_max2165_cfg);
	return (fe == NULL) ? -EIO : 0;
}

static int cxusb_cx22702_frontend_attach(struct dvb_usb_adapter *adap)
{
	u8 b;
	if (usb_set_interface(adap->dev->udev, 0, 6) < 0)
		err("set interface failed");

	cxusb_ctrl_msg(adap->dev, CMD_DIGITAL, NULL, 0, &b, 1);

	adap->fe_adap[0].fe = dvb_attach(cx22702_attach, &cxusb_cx22702_config,
					 &adap->dev->i2c_adap);
	if ((adap->fe_adap[0].fe) != NULL)
		return 0;

	return -EIO;
}

static int cxusb_lgdt3303_frontend_attach(struct dvb_usb_adapter *adap)
{
	if (usb_set_interface(adap->dev->udev, 0, 7) < 0)
		err("set interface failed");

	cxusb_ctrl_msg(adap->dev, CMD_DIGITAL, NULL, 0, NULL, 0);

	adap->fe_adap[0].fe = dvb_attach(lgdt330x_attach,
					 &cxusb_lgdt3303_config,
					 &adap->dev->i2c_adap);
	if ((adap->fe_adap[0].fe) != NULL)
		return 0;

	return -EIO;
}

static int cxusb_aver_lgdt3303_frontend_attach(struct dvb_usb_adapter *adap)
{
	adap->fe_adap[0].fe = dvb_attach(lgdt330x_attach, &cxusb_aver_lgdt3303_config,
			      &adap->dev->i2c_adap);
	if (adap->fe_adap[0].fe != NULL)
		return 0;

	return -EIO;
}

static int cxusb_mt352_frontend_attach(struct dvb_usb_adapter *adap)
{
	/* used in both lgz201 and th7579 */
	if (usb_set_interface(adap->dev->udev, 0, 0) < 0)
		err("set interface failed");

	cxusb_ctrl_msg(adap->dev, CMD_DIGITAL, NULL, 0, NULL, 0);

	adap->fe_adap[0].fe = dvb_attach(mt352_attach, &cxusb_mt352_config,
					 &adap->dev->i2c_adap);
	if ((adap->fe_adap[0].fe) != NULL)
		return 0;

	return -EIO;
}

static int cxusb_dee1601_frontend_attach(struct dvb_usb_adapter *adap)
{
	if (usb_set_interface(adap->dev->udev, 0, 0) < 0)
		err("set interface failed");

	cxusb_ctrl_msg(adap->dev, CMD_DIGITAL, NULL, 0, NULL, 0);

	adap->fe_adap[0].fe = dvb_attach(mt352_attach, &cxusb_dee1601_config,
					 &adap->dev->i2c_adap);
	if ((adap->fe_adap[0].fe) != NULL)
		return 0;

	adap->fe_adap[0].fe = dvb_attach(zl10353_attach,
					 &cxusb_zl10353_dee1601_config,
					 &adap->dev->i2c_adap);
	if ((adap->fe_adap[0].fe) != NULL)
		return 0;

	return -EIO;
}

static int cxusb_dualdig4_frontend_attach(struct dvb_usb_adapter *adap)
{
	u8 ircode[4];
	int i;
	struct i2c_msg msg = { .addr = 0x6b, .flags = I2C_M_RD,
			       .buf = ircode, .len = 4 };

	if (usb_set_interface(adap->dev->udev, 0, 1) < 0)
		err("set interface failed");

	cxusb_ctrl_msg(adap->dev, CMD_DIGITAL, NULL, 0, NULL, 0);

	/* reset the tuner and demodulator */
	cxusb_bluebird_gpio_rw(adap->dev, 0x04, 0);
	cxusb_bluebird_gpio_pulse(adap->dev, 0x01, 1);
	cxusb_bluebird_gpio_pulse(adap->dev, 0x02, 1);

	adap->fe_adap[0].fe =
		dvb_attach(zl10353_attach,
			   &cxusb_zl10353_xc3028_config_no_i2c_gate,
			   &adap->dev->i2c_adap);
	if ((adap->fe_adap[0].fe) == NULL)
		return -EIO;

	/* try to determine if there is no IR decoder on the I2C bus */
	for (i = 0; adap->dev->props.rc.legacy.rc_map_table != NULL && i < 5; i++) {
		msleep(20);
		if (cxusb_i2c_xfer(&adap->dev->i2c_adap, &msg, 1) != 1)
			goto no_IR;
		if (ircode[0] == 0 && ircode[1] == 0)
			continue;
		if (ircode[2] + ircode[3] != 0xff) {
no_IR:
			adap->dev->props.rc.legacy.rc_map_table = NULL;
			info("No IR receiver detected on this device.");
			break;
		}
	}

	return 0;
}

static struct dibx000_agc_config dib7070_agc_config = {
	.band_caps = BAND_UHF | BAND_VHF | BAND_LBAND | BAND_SBAND,

	/*
	 * P_agc_use_sd_mod1=0, P_agc_use_sd_mod2=0, P_agc_freq_pwm_div=5,
	 * P_agc_inv_pwm1=0, P_agc_inv_pwm2=0, P_agc_inh_dc_rv_est=0,
	 * P_agc_time_est=3, P_agc_freeze=0, P_agc_nb_est=5, P_agc_write=0
	 */
	.setup = (0 << 15) | (0 << 14) | (5 << 11) | (0 << 10) | (0 << 9) |
		 (0 << 8) | (3 << 5) | (0 << 4) | (5 << 1) | (0 << 0),
	.inv_gain = 600,
	.time_stabiliz = 10,
	.alpha_level = 0,
	.thlock = 118,
	.wbd_inv = 0,
	.wbd_ref = 3530,
	.wbd_sel = 1,
	.wbd_alpha = 5,
	.agc1_max = 65535,
	.agc1_min = 0,
	.agc2_max = 65535,
	.agc2_min = 0,
	.agc1_pt1 = 0,
	.agc1_pt2 = 40,
	.agc1_pt3 = 183,
	.agc1_slope1 = 206,
	.agc1_slope2 = 255,
	.agc2_pt1 = 72,
	.agc2_pt2 = 152,
	.agc2_slope1 = 88,
	.agc2_slope2 = 90,
	.alpha_mant = 17,
	.alpha_exp = 27,
	.beta_mant = 23,
	.beta_exp = 51,
	.perform_agc_softsplit = 0,
};

static struct dibx000_bandwidth_config dib7070_bw_config_12_mhz = {
	.internal = 60000,
	.sampling = 15000,
	.pll_prediv = 1,
	.pll_ratio = 20,
	.pll_range = 3,
	.pll_reset = 1,
	.pll_bypass = 0,
	.enable_refdiv = 0,
	.bypclk_div = 0,
	.IO_CLK_en_core = 1,
	.ADClkSrc = 1,
	.modulo = 2,
	/* refsel, sel, freq_15k */
	.sad_cfg = (3 << 14) | (1 << 12) | (524 << 0),
	.ifreq = (0 << 25) | 0,
	.timf = 20452225,
	.xtal_hz = 12000000,
};

static struct dib7000p_config cxusb_dualdig4_rev2_config = {
	.output_mode = OUTMODE_MPEG2_PAR_GATED_CLK,
	.output_mpeg2_in_188_bytes = 1,

	.agc_config_count = 1,
	.agc = &dib7070_agc_config,
	.bw  = &dib7070_bw_config_12_mhz,
	.tuner_is_baseband = 1,
	.spur_protect = 1,

	.gpio_dir = 0xfcef,
	.gpio_val = 0x0110,

	.gpio_pwm_pos = DIB7000P_GPIO_DEFAULT_PWM_POS,

	.hostbus_diversity = 1,
};

struct dib0700_adapter_state {
	int (*set_param_save)(struct dvb_frontend *);
	struct dib7000p_ops dib7000p_ops;
};

static int cxusb_dualdig4_rev2_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct dib0700_adapter_state *state = adap->priv;

	if (usb_set_interface(adap->dev->udev, 0, 1) < 0)
		err("set interface failed");

	cxusb_ctrl_msg(adap->dev, CMD_DIGITAL, NULL, 0, NULL, 0);

	cxusb_bluebird_gpio_pulse(adap->dev, 0x02, 1);

	if (!dvb_attach(dib7000p_attach, &state->dib7000p_ops))
		return -ENODEV;

	if (state->dib7000p_ops.i2c_enumeration(&adap->dev->i2c_adap, 1, 18,
				       &cxusb_dualdig4_rev2_config) < 0) {
		printk(KERN_WARNING "Unable to enumerate dib7000p\n");
		return -ENODEV;
	}

	adap->fe_adap[0].fe = state->dib7000p_ops.init(&adap->dev->i2c_adap, 0x80,
					      &cxusb_dualdig4_rev2_config);
	if (adap->fe_adap[0].fe == NULL)
		return -EIO;

	return 0;
}

static int dib7070_tuner_reset(struct dvb_frontend *fe, int onoff)
{
	struct dvb_usb_adapter *adap = fe->dvb->priv;
	struct dib0700_adapter_state *state = adap->priv;

	return state->dib7000p_ops.set_gpio(fe, 8, 0, !onoff);
}

static int dib7070_tuner_sleep(struct dvb_frontend *fe, int onoff)
{
	return 0;
}

static struct dib0070_config dib7070p_dib0070_config = {
	.i2c_address = DEFAULT_DIB0070_I2C_ADDRESS,
	.reset = dib7070_tuner_reset,
	.sleep = dib7070_tuner_sleep,
	.clock_khz = 12000,
};

static int dib7070_set_param_override(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct dvb_usb_adapter *adap = fe->dvb->priv;
	struct dib0700_adapter_state *state = adap->priv;

	u16 offset;
	u8 band = BAND_OF_FREQUENCY(p->frequency/1000);
	switch (band) {
	case BAND_VHF: offset = 950; break;
	default:
	case BAND_UHF: offset = 550; break;
	}

	state->dib7000p_ops.set_wbd_ref(fe, offset + dib0070_wbd_offset(fe));

	return state->set_param_save(fe);
}

static int cxusb_dualdig4_rev2_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct dib0700_adapter_state *st = adap->priv;
	struct i2c_adapter *tun_i2c;

	/*
	 * No need to call dvb7000p_attach here, as it was called
	 * already, as frontend_attach method is called first, and
	 * tuner_attach is only called on sucess.
	 */
	tun_i2c = st->dib7000p_ops.get_i2c_master(adap->fe_adap[0].fe,
					DIBX000_I2C_INTERFACE_TUNER, 1);

	if (dvb_attach(dib0070_attach, adap->fe_adap[0].fe, tun_i2c,
	    &dib7070p_dib0070_config) == NULL)
		return -ENODEV;

	st->set_param_save = adap->fe_adap[0].fe->ops.tuner_ops.set_params;
	adap->fe_adap[0].fe->ops.tuner_ops.set_params = dib7070_set_param_override;
	return 0;
}

static int cxusb_nano2_frontend_attach(struct dvb_usb_adapter *adap)
{
	if (usb_set_interface(adap->dev->udev, 0, 1) < 0)
		err("set interface failed");

	cxusb_ctrl_msg(adap->dev, CMD_DIGITAL, NULL, 0, NULL, 0);

	/* reset the tuner and demodulator */
	cxusb_bluebird_gpio_rw(adap->dev, 0x04, 0);
	cxusb_bluebird_gpio_pulse(adap->dev, 0x01, 1);
	cxusb_bluebird_gpio_pulse(adap->dev, 0x02, 1);

	adap->fe_adap[0].fe = dvb_attach(zl10353_attach,
					 &cxusb_zl10353_xc3028_config,
					 &adap->dev->i2c_adap);
	if ((adap->fe_adap[0].fe) != NULL)
		return 0;

	adap->fe_adap[0].fe = dvb_attach(mt352_attach,
					 &cxusb_mt352_xc3028_config,
					 &adap->dev->i2c_adap);
	if ((adap->fe_adap[0].fe) != NULL)
		return 0;

	return -EIO;
}

static struct lgs8gxx_config d680_lgs8gl5_cfg = {
	.prod = LGS8GXX_PROD_LGS8GL5,
	.demod_address = 0x19,
	.serial_ts = 0,
	.ts_clk_pol = 0,
	.ts_clk_gated = 1,
	.if_clk_freq = 30400, /* 30.4 MHz */
	.if_freq = 5725, /* 5.725 MHz */
	.if_neg_center = 0,
	.ext_adc = 0,
	.adc_signed = 0,
	.if_neg_edge = 0,
};

static int cxusb_d680_dmb_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct dvb_usb_device *d = adap->dev;
	int n;

	/* Select required USB configuration */
	if (usb_set_interface(d->udev, 0, 0) < 0)
		err("set interface failed");

	/* Unblock all USB pipes */
	usb_clear_halt(d->udev,
		usb_sndbulkpipe(d->udev, d->props.generic_bulk_ctrl_endpoint));
	usb_clear_halt(d->udev,
		usb_rcvbulkpipe(d->udev, d->props.generic_bulk_ctrl_endpoint));
	usb_clear_halt(d->udev,
		usb_rcvbulkpipe(d->udev, d->props.adapter[0].fe[0].stream.endpoint));

	/* Drain USB pipes to avoid hang after reboot */
	for (n = 0;  n < 5;  n++) {
		cxusb_d680_dmb_drain_message(d);
		cxusb_d680_dmb_drain_video(d);
		msleep(200);
	}

	/* Reset the tuner */
	if (cxusb_d680_dmb_gpio_tuner(d, 0x07, 0) < 0) {
		err("clear tuner gpio failed");
		return -EIO;
	}
	msleep(100);
	if (cxusb_d680_dmb_gpio_tuner(d, 0x07, 1) < 0) {
		err("set tuner gpio failed");
		return -EIO;
	}
	msleep(100);

	/* Attach frontend */
	adap->fe_adap[0].fe = dvb_attach(lgs8gxx_attach, &d680_lgs8gl5_cfg, &d->i2c_adap);
	if (adap->fe_adap[0].fe == NULL)
		return -EIO;

	return 0;
}

static struct atbm8830_config mygica_d689_atbm8830_cfg = {
	.prod = ATBM8830_PROD_8830,
	.demod_address = 0x40,
	.serial_ts = 0,
	.ts_sampling_edge = 1,
	.ts_clk_gated = 0,
	.osc_clk_freq = 30400, /* in kHz */
	.if_freq = 0, /* zero IF */
	.zif_swap_iq = 1,
	.agc_min = 0x2E,
	.agc_max = 0x90,
	.agc_hold_loop = 0,
};

static int cxusb_mygica_d689_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct dvb_usb_device *d = adap->dev;

	/* Select required USB configuration */
	if (usb_set_interface(d->udev, 0, 0) < 0)
		err("set interface failed");

	/* Unblock all USB pipes */
	usb_clear_halt(d->udev,
		usb_sndbulkpipe(d->udev, d->props.generic_bulk_ctrl_endpoint));
	usb_clear_halt(d->udev,
		usb_rcvbulkpipe(d->udev, d->props.generic_bulk_ctrl_endpoint));
	usb_clear_halt(d->udev,
		usb_rcvbulkpipe(d->udev, d->props.adapter[0].fe[0].stream.endpoint));


	/* Reset the tuner */
	if (cxusb_d680_dmb_gpio_tuner(d, 0x07, 0) < 0) {
		err("clear tuner gpio failed");
		return -EIO;
	}
	msleep(100);
	if (cxusb_d680_dmb_gpio_tuner(d, 0x07, 1) < 0) {
		err("set tuner gpio failed");
		return -EIO;
	}
	msleep(100);

	/* Attach frontend */
	adap->fe_adap[0].fe = dvb_attach(atbm8830_attach, &mygica_d689_atbm8830_cfg,
		&d->i2c_adap);
	if (adap->fe_adap[0].fe == NULL)
		return -EIO;

	return 0;
}

static int cxusb_tt_ct2_4400_attach(struct dvb_usb_adapter *adap)
{
	struct dvb_usb_device *d = adap->dev;
	struct cxusb_state *st = d->priv;
	struct i2c_adapter *adapter;
	struct i2c_client *client_demod;
	struct i2c_client *client_tuner;
	struct i2c_client *client_ci;
	struct i2c_board_info info;
	struct si2168_config si2168_config;
	struct si2157_config si2157_config;
	struct sp2_config sp2_config;
	u8 o[2], i;

	/* reset the tuner */
	if (cxusb_tt_ct2_4400_gpio_tuner(d, 0) < 0) {
		err("clear tuner gpio failed");
		return -EIO;
	}
	msleep(100);
	if (cxusb_tt_ct2_4400_gpio_tuner(d, 1) < 0) {
		err("set tuner gpio failed");
		return -EIO;
	}
	msleep(100);

	/* attach frontend */
	si2168_config.i2c_adapter = &adapter;
	si2168_config.fe = &adap->fe_adap[0].fe;
	si2168_config.ts_mode = SI2168_TS_PARALLEL;
	memset(&info, 0, sizeof(struct i2c_board_info));
	strlcpy(info.type, "si2168", I2C_NAME_SIZE);
	info.addr = 0x64;
	info.platform_data = &si2168_config;
	request_module(info.type);
	client_demod = i2c_new_device(&d->i2c_adap, &info);
	if (client_demod == NULL || client_demod->dev.driver == NULL)
		return -ENODEV;

	if (!try_module_get(client_demod->dev.driver->owner)) {
		i2c_unregister_device(client_demod);
		return -ENODEV;
	}

	st->i2c_client_demod = client_demod;

	/* attach tuner */
	memset(&si2157_config, 0, sizeof(si2157_config));
	si2157_config.fe = adap->fe_adap[0].fe;
	memset(&info, 0, sizeof(struct i2c_board_info));
	strlcpy(info.type, "si2157", I2C_NAME_SIZE);
	info.addr = 0x60;
	info.platform_data = &si2157_config;
	request_module(info.type);
	client_tuner = i2c_new_device(adapter, &info);
	if (client_tuner == NULL || client_tuner->dev.driver == NULL) {
		module_put(client_demod->dev.driver->owner);
		i2c_unregister_device(client_demod);
		return -ENODEV;
	}
	if (!try_module_get(client_tuner->dev.driver->owner)) {
		i2c_unregister_device(client_tuner);
		module_put(client_demod->dev.driver->owner);
		i2c_unregister_device(client_demod);
		return -ENODEV;
	}

	st->i2c_client_tuner = client_tuner;

	/* initialize CI */
	if (d->udev->descriptor.idProduct ==
		USB_PID_TECHNOTREND_CONNECT_CT2_4650_CI) {

		memcpy(o, "\xc0\x01", 2);
		cxusb_ctrl_msg(d, CMD_GPIO_WRITE, o, 2, &i, 1);
		msleep(100);

		memcpy(o, "\xc0\x00", 2);
		cxusb_ctrl_msg(d, CMD_GPIO_WRITE, o, 2, &i, 1);
		msleep(100);

		memset(&sp2_config, 0, sizeof(sp2_config));
		sp2_config.dvb_adap = &adap->dvb_adap;
		sp2_config.priv = d;
		sp2_config.ci_control = cxusb_tt_ct2_4650_ci_ctrl;
		memset(&info, 0, sizeof(struct i2c_board_info));
		strlcpy(info.type, "sp2", I2C_NAME_SIZE);
		info.addr = 0x40;
		info.platform_data = &sp2_config;
		request_module(info.type);
		client_ci = i2c_new_device(&d->i2c_adap, &info);
		if (client_ci == NULL || client_ci->dev.driver == NULL) {
			module_put(client_tuner->dev.driver->owner);
			i2c_unregister_device(client_tuner);
			module_put(client_demod->dev.driver->owner);
			i2c_unregister_device(client_demod);
			return -ENODEV;
		}
		if (!try_module_get(client_ci->dev.driver->owner)) {
			i2c_unregister_device(client_ci);
			module_put(client_tuner->dev.driver->owner);
			i2c_unregister_device(client_tuner);
			module_put(client_demod->dev.driver->owner);
			i2c_unregister_device(client_demod);
			return -ENODEV;
		}

		st->i2c_client_ci = client_ci;

	}

	return 0;
}

/*
 * DViCO has shipped two devices with the same USB ID, but only one of them
 * needs a firmware download.  Check the device class details to see if they
 * have non-default values to decide whether the device is actually cold or
 * not, and forget a match if it turns out we selected the wrong device.
 */
static int bluebird_fx2_identify_state(struct usb_device *udev,
				       struct dvb_usb_device_properties *props,
				       struct dvb_usb_device_description **desc,
				       int *cold)
{
	int wascold = *cold;

	*cold = udev->descriptor.bDeviceClass == 0xff &&
		udev->descriptor.bDeviceSubClass == 0xff &&
		udev->descriptor.bDeviceProtocol == 0xff;

	if (*cold && !wascold)
		*desc = NULL;

	return 0;
}

/*
 * DViCO bluebird firmware needs the "warm" product ID to be patched into the
 * firmware file before download.
 */

static const int dvico_firmware_id_offsets[] = { 6638, 3204 };
static int bluebird_patch_dvico_firmware_download(struct usb_device *udev,
						  const struct firmware *fw)
{
	int pos;

	for (pos = 0; pos < ARRAY_SIZE(dvico_firmware_id_offsets); pos++) {
		int idoff = dvico_firmware_id_offsets[pos];

		if (fw->size < idoff + 4)
			continue;

		if (fw->data[idoff] == (USB_VID_DVICO & 0xff) &&
		    fw->data[idoff + 1] == USB_VID_DVICO >> 8) {
			struct firmware new_fw;
			u8 *new_fw_data = vmalloc(fw->size);
			int ret;

			if (!new_fw_data)
				return -ENOMEM;

			memcpy(new_fw_data, fw->data, fw->size);
			new_fw.size = fw->size;
			new_fw.data = new_fw_data;

			new_fw_data[idoff + 2] =
				le16_to_cpu(udev->descriptor.idProduct) + 1;
			new_fw_data[idoff + 3] =
				le16_to_cpu(udev->descriptor.idProduct) >> 8;

			ret = usb_cypress_load_firmware(udev, &new_fw,
							CYPRESS_FX2);
			vfree(new_fw_data);
			return ret;
		}
	}

	return -EINVAL;
}

/* DVB USB Driver stuff */
static struct dvb_usb_device_properties cxusb_medion_properties;
static struct dvb_usb_device_properties cxusb_bluebird_lgh064f_properties;
static struct dvb_usb_device_properties cxusb_bluebird_dee1601_properties;
static struct dvb_usb_device_properties cxusb_bluebird_lgz201_properties;
static struct dvb_usb_device_properties cxusb_bluebird_dtt7579_properties;
static struct dvb_usb_device_properties cxusb_bluebird_dualdig4_properties;
static struct dvb_usb_device_properties cxusb_bluebird_dualdig4_rev2_properties;
static struct dvb_usb_device_properties cxusb_bluebird_nano2_properties;
static struct dvb_usb_device_properties cxusb_bluebird_nano2_needsfirmware_properties;
static struct dvb_usb_device_properties cxusb_aver_a868r_properties;
static struct dvb_usb_device_properties cxusb_d680_dmb_properties;
static struct dvb_usb_device_properties cxusb_mygica_d689_properties;
static struct dvb_usb_device_properties cxusb_tt_ct2_4400_properties;

static int cxusb_probe(struct usb_interface *intf,
		       const struct usb_device_id *id)
{
	if (0 == dvb_usb_device_init(intf, &cxusb_medion_properties,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf, &cxusb_bluebird_lgh064f_properties,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf, &cxusb_bluebird_dee1601_properties,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf, &cxusb_bluebird_lgz201_properties,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf, &cxusb_bluebird_dtt7579_properties,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf, &cxusb_bluebird_dualdig4_properties,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf, &cxusb_bluebird_nano2_properties,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf,
				&cxusb_bluebird_nano2_needsfirmware_properties,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf, &cxusb_aver_a868r_properties,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf,
				     &cxusb_bluebird_dualdig4_rev2_properties,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf, &cxusb_d680_dmb_properties,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf, &cxusb_mygica_d689_properties,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf, &cxusb_tt_ct2_4400_properties,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0)
		return 0;

	return -EINVAL;
}

static void cxusb_disconnect(struct usb_interface *intf)
{
	struct dvb_usb_device *d = usb_get_intfdata(intf);
	struct cxusb_state *st = d->priv;
	struct i2c_client *client;

	/* remove I2C client for CI */
	client = st->i2c_client_ci;
	if (client) {
		module_put(client->dev.driver->owner);
		i2c_unregister_device(client);
	}

	/* remove I2C client for tuner */
	client = st->i2c_client_tuner;
	if (client) {
		module_put(client->dev.driver->owner);
		i2c_unregister_device(client);
	}

	/* remove I2C client for demodulator */
	client = st->i2c_client_demod;
	if (client) {
		module_put(client->dev.driver->owner);
		i2c_unregister_device(client);
	}

	dvb_usb_device_exit(intf);
}

static struct usb_device_id cxusb_table [] = {
	{ USB_DEVICE(USB_VID_MEDION, USB_PID_MEDION_MD95700) },
	{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_LG064F_COLD) },
	{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_LG064F_WARM) },
	{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_DUAL_1_COLD) },
	{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_DUAL_1_WARM) },
	{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_LGZ201_COLD) },
	{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_LGZ201_WARM) },
	{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_TH7579_COLD) },
	{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_TH7579_WARM) },
	{ USB_DEVICE(USB_VID_DVICO, USB_PID_DIGITALNOW_BLUEBIRD_DUAL_1_COLD) },
	{ USB_DEVICE(USB_VID_DVICO, USB_PID_DIGITALNOW_BLUEBIRD_DUAL_1_WARM) },
	{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_DUAL_2_COLD) },
	{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_DUAL_2_WARM) },
	{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_DUAL_4) },
	{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_DVB_T_NANO_2) },
	{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_DVB_T_NANO_2_NFW_WARM) },
	{ USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_VOLAR_A868R) },
	{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_DUAL_4_REV_2) },
	{ USB_DEVICE(USB_VID_CONEXANT, USB_PID_CONEXANT_D680_DMB) },
	{ USB_DEVICE(USB_VID_CONEXANT, USB_PID_MYGICA_D689) },
	{ USB_DEVICE(USB_VID_TECHNOTREND, USB_PID_TECHNOTREND_TVSTICK_CT2_4400) },
	{ USB_DEVICE(USB_VID_TECHNOTREND, USB_PID_TECHNOTREND_CONNECT_CT2_4650_CI) },
	{}		/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, cxusb_table);

static struct dvb_usb_device_properties cxusb_medion_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = CYPRESS_FX2,

	.size_of_priv     = sizeof(struct cxusb_state),

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.streaming_ctrl   = cxusb_streaming_ctrl,
			.frontend_attach  = cxusb_cx22702_frontend_attach,
			.tuner_attach     = cxusb_fmd1216me_tuner_attach,
			/* parameter for the MPEG2-data transfer */
					.stream = {
						.type = USB_BULK,
				.count = 5,
				.endpoint = 0x02,
				.u = {
					.bulk = {
						.buffersize = 8192,
					}
				}
			},
		}},
		},
	},
	.power_ctrl       = cxusb_power_ctrl,

	.i2c_algo         = &cxusb_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 1,
	.devices = {
		{   "Medion MD95700 (MDUSBTV-HYBRID)",
			{ NULL },
			{ &cxusb_table[0], NULL },
		},
	}
};

static struct dvb_usb_device_properties cxusb_bluebird_lgh064f_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl          = DEVICE_SPECIFIC,
	.firmware          = "dvb-usb-bluebird-01.fw",
	.download_firmware = bluebird_patch_dvico_firmware_download,
	/* use usb alt setting 0 for EP4 transfer (dvb-t),
	   use usb alt setting 7 for EP2 transfer (atsc) */

	.size_of_priv     = sizeof(struct cxusb_state),

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.streaming_ctrl   = cxusb_streaming_ctrl,
			.frontend_attach  = cxusb_lgdt3303_frontend_attach,
			.tuner_attach     = cxusb_lgh064f_tuner_attach,

			/* parameter for the MPEG2-data transfer */
					.stream = {
						.type = USB_BULK,
				.count = 5,
				.endpoint = 0x02,
				.u = {
					.bulk = {
						.buffersize = 8192,
					}
				}
			},
		}},
		},
	},

	.power_ctrl       = cxusb_bluebird_power_ctrl,

	.i2c_algo         = &cxusb_i2c_algo,

	.rc.legacy = {
		.rc_interval      = 100,
		.rc_map_table     = rc_map_dvico_portable_table,
		.rc_map_size      = ARRAY_SIZE(rc_map_dvico_portable_table),
		.rc_query         = cxusb_rc_query,
	},

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 1,
	.devices = {
		{   "DViCO FusionHDTV5 USB Gold",
			{ &cxusb_table[1], NULL },
			{ &cxusb_table[2], NULL },
		},
	}
};

static struct dvb_usb_device_properties cxusb_bluebird_dee1601_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl          = DEVICE_SPECIFIC,
	.firmware          = "dvb-usb-bluebird-01.fw",
	.download_firmware = bluebird_patch_dvico_firmware_download,
	/* use usb alt setting 0 for EP4 transfer (dvb-t),
	   use usb alt setting 7 for EP2 transfer (atsc) */

	.size_of_priv     = sizeof(struct cxusb_state),

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.streaming_ctrl   = cxusb_streaming_ctrl,
			.frontend_attach  = cxusb_dee1601_frontend_attach,
			.tuner_attach     = cxusb_dee1601_tuner_attach,
			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 5,
				.endpoint = 0x04,
				.u = {
					.bulk = {
						.buffersize = 8192,
					}
				}
			},
		}},
		},
	},

	.power_ctrl       = cxusb_bluebird_power_ctrl,

	.i2c_algo         = &cxusb_i2c_algo,

	.rc.legacy = {
		.rc_interval      = 150,
		.rc_map_table     = rc_map_dvico_mce_table,
		.rc_map_size      = ARRAY_SIZE(rc_map_dvico_mce_table),
		.rc_query         = cxusb_rc_query,
	},

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 3,
	.devices = {
		{   "DViCO FusionHDTV DVB-T Dual USB",
			{ &cxusb_table[3], NULL },
			{ &cxusb_table[4], NULL },
		},
		{   "DigitalNow DVB-T Dual USB",
			{ &cxusb_table[9],  NULL },
			{ &cxusb_table[10], NULL },
		},
		{   "DViCO FusionHDTV DVB-T Dual Digital 2",
			{ &cxusb_table[11], NULL },
			{ &cxusb_table[12], NULL },
		},
	}
};

static struct dvb_usb_device_properties cxusb_bluebird_lgz201_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl          = DEVICE_SPECIFIC,
	.firmware          = "dvb-usb-bluebird-01.fw",
	.download_firmware = bluebird_patch_dvico_firmware_download,
	/* use usb alt setting 0 for EP4 transfer (dvb-t),
	   use usb alt setting 7 for EP2 transfer (atsc) */

	.size_of_priv     = sizeof(struct cxusb_state),

	.num_adapters = 2,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.streaming_ctrl   = cxusb_streaming_ctrl,
			.frontend_attach  = cxusb_mt352_frontend_attach,
			.tuner_attach     = cxusb_lgz201_tuner_attach,

			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 5,
				.endpoint = 0x04,
				.u = {
					.bulk = {
						.buffersize = 8192,
					}
				}
			},
		}},
		},
	},
	.power_ctrl       = cxusb_bluebird_power_ctrl,

	.i2c_algo         = &cxusb_i2c_algo,

	.rc.legacy = {
		.rc_interval      = 100,
		.rc_map_table     = rc_map_dvico_portable_table,
		.rc_map_size      = ARRAY_SIZE(rc_map_dvico_portable_table),
		.rc_query         = cxusb_rc_query,
	},

	.generic_bulk_ctrl_endpoint = 0x01,
	.num_device_descs = 1,
	.devices = {
		{   "DViCO FusionHDTV DVB-T USB (LGZ201)",
			{ &cxusb_table[5], NULL },
			{ &cxusb_table[6], NULL },
		},
	}
};

static struct dvb_usb_device_properties cxusb_bluebird_dtt7579_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl          = DEVICE_SPECIFIC,
	.firmware          = "dvb-usb-bluebird-01.fw",
	.download_firmware = bluebird_patch_dvico_firmware_download,
	/* use usb alt setting 0 for EP4 transfer (dvb-t),
	   use usb alt setting 7 for EP2 transfer (atsc) */

	.size_of_priv     = sizeof(struct cxusb_state),

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.streaming_ctrl   = cxusb_streaming_ctrl,
			.frontend_attach  = cxusb_mt352_frontend_attach,
			.tuner_attach     = cxusb_dtt7579_tuner_attach,

			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 5,
				.endpoint = 0x04,
				.u = {
					.bulk = {
						.buffersize = 8192,
					}
				}
			},
		}},
		},
	},
	.power_ctrl       = cxusb_bluebird_power_ctrl,

	.i2c_algo         = &cxusb_i2c_algo,

	.rc.legacy = {
		.rc_interval      = 100,
		.rc_map_table     = rc_map_dvico_portable_table,
		.rc_map_size      = ARRAY_SIZE(rc_map_dvico_portable_table),
		.rc_query         = cxusb_rc_query,
	},

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 1,
	.devices = {
		{   "DViCO FusionHDTV DVB-T USB (TH7579)",
			{ &cxusb_table[7], NULL },
			{ &cxusb_table[8], NULL },
		},
	}
};

static struct dvb_usb_device_properties cxusb_bluebird_dualdig4_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl         = CYPRESS_FX2,

	.size_of_priv     = sizeof(struct cxusb_state),

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.streaming_ctrl   = cxusb_streaming_ctrl,
			.frontend_attach  = cxusb_dualdig4_frontend_attach,
			.tuner_attach     = cxusb_dvico_xc3028_tuner_attach,
			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 5,
				.endpoint = 0x02,
				.u = {
					.bulk = {
						.buffersize = 8192,
					}
				}
			},
		}},
		},
	},

	.power_ctrl       = cxusb_power_ctrl,

	.i2c_algo         = &cxusb_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,

	.rc.legacy = {
		.rc_interval      = 100,
		.rc_map_table     = rc_map_dvico_mce_table,
		.rc_map_size      = ARRAY_SIZE(rc_map_dvico_mce_table),
		.rc_query         = cxusb_bluebird2_rc_query,
	},

	.num_device_descs = 1,
	.devices = {
		{   "DViCO FusionHDTV DVB-T Dual Digital 4",
			{ NULL },
			{ &cxusb_table[13], NULL },
		},
	}
};

static struct dvb_usb_device_properties cxusb_bluebird_nano2_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl         = CYPRESS_FX2,
	.identify_state   = bluebird_fx2_identify_state,

	.size_of_priv     = sizeof(struct cxusb_state),

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.streaming_ctrl   = cxusb_streaming_ctrl,
			.frontend_attach  = cxusb_nano2_frontend_attach,
			.tuner_attach     = cxusb_dvico_xc3028_tuner_attach,
			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 5,
				.endpoint = 0x02,
				.u = {
					.bulk = {
						.buffersize = 8192,
					}
				}
			},
		}},
		},
	},

	.power_ctrl       = cxusb_nano2_power_ctrl,

	.i2c_algo         = &cxusb_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,

	.rc.legacy = {
		.rc_interval      = 100,
		.rc_map_table     = rc_map_dvico_portable_table,
		.rc_map_size      = ARRAY_SIZE(rc_map_dvico_portable_table),
		.rc_query         = cxusb_bluebird2_rc_query,
	},

	.num_device_descs = 1,
	.devices = {
		{   "DViCO FusionHDTV DVB-T NANO2",
			{ NULL },
			{ &cxusb_table[14], NULL },
		},
	}
};

static struct dvb_usb_device_properties cxusb_bluebird_nano2_needsfirmware_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl          = DEVICE_SPECIFIC,
	.firmware          = "dvb-usb-bluebird-02.fw",
	.download_firmware = bluebird_patch_dvico_firmware_download,
	.identify_state    = bluebird_fx2_identify_state,

	.size_of_priv      = sizeof(struct cxusb_state),

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.streaming_ctrl   = cxusb_streaming_ctrl,
			.frontend_attach  = cxusb_nano2_frontend_attach,
			.tuner_attach     = cxusb_dvico_xc3028_tuner_attach,
			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 5,
				.endpoint = 0x02,
				.u = {
					.bulk = {
						.buffersize = 8192,
					}
				}
			},
		}},
		},
	},

	.power_ctrl       = cxusb_nano2_power_ctrl,

	.i2c_algo         = &cxusb_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,

	.rc.legacy = {
		.rc_interval      = 100,
		.rc_map_table     = rc_map_dvico_portable_table,
		.rc_map_size      = ARRAY_SIZE(rc_map_dvico_portable_table),
		.rc_query         = cxusb_rc_query,
	},

	.num_device_descs = 1,
	.devices = {
		{   "DViCO FusionHDTV DVB-T NANO2 w/o firmware",
			{ &cxusb_table[14], NULL },
			{ &cxusb_table[15], NULL },
		},
	}
};

static struct dvb_usb_device_properties cxusb_aver_a868r_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl         = CYPRESS_FX2,

	.size_of_priv     = sizeof(struct cxusb_state),

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.streaming_ctrl   = cxusb_aver_streaming_ctrl,
			.frontend_attach  = cxusb_aver_lgdt3303_frontend_attach,
			.tuner_attach     = cxusb_mxl5003s_tuner_attach,
			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 5,
				.endpoint = 0x04,
				.u = {
					.bulk = {
						.buffersize = 8192,
					}
				}
			},
		}},
		},
	},
	.power_ctrl       = cxusb_aver_power_ctrl,

	.i2c_algo         = &cxusb_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 1,
	.devices = {
		{   "AVerMedia AVerTVHD Volar (A868R)",
			{ NULL },
			{ &cxusb_table[16], NULL },
		},
	}
};

static
struct dvb_usb_device_properties cxusb_bluebird_dualdig4_rev2_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl         = CYPRESS_FX2,

	.size_of_priv     = sizeof(struct cxusb_state),

	.num_adapters = 1,
	.adapter = {
		{
		.size_of_priv    = sizeof(struct dib0700_adapter_state),
		.num_frontends = 1,
		.fe = {{
			.streaming_ctrl  = cxusb_streaming_ctrl,
			.frontend_attach = cxusb_dualdig4_rev2_frontend_attach,
			.tuner_attach    = cxusb_dualdig4_rev2_tuner_attach,
			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 7,
				.endpoint = 0x02,
				.u = {
					.bulk = {
						.buffersize = 4096,
					}
				}
			},
		}},
		},
	},

	.power_ctrl       = cxusb_bluebird_power_ctrl,

	.i2c_algo         = &cxusb_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,

	.rc.legacy = {
		.rc_interval      = 100,
		.rc_map_table     = rc_map_dvico_mce_table,
		.rc_map_size      = ARRAY_SIZE(rc_map_dvico_mce_table),
		.rc_query         = cxusb_rc_query,
	},

	.num_device_descs = 1,
	.devices = {
		{   "DViCO FusionHDTV DVB-T Dual Digital 4 (rev 2)",
			{ NULL },
			{ &cxusb_table[17], NULL },
		},
	}
};

static struct dvb_usb_device_properties cxusb_d680_dmb_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl         = CYPRESS_FX2,

	.size_of_priv     = sizeof(struct cxusb_state),

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.streaming_ctrl   = cxusb_d680_dmb_streaming_ctrl,
			.frontend_attach  = cxusb_d680_dmb_frontend_attach,
			.tuner_attach     = cxusb_d680_dmb_tuner_attach,

			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 5,
				.endpoint = 0x02,
				.u = {
					.bulk = {
						.buffersize = 8192,
					}
				}
			},
		}},
		},
	},

	.power_ctrl       = cxusb_d680_dmb_power_ctrl,

	.i2c_algo         = &cxusb_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,

	.rc.legacy = {
		.rc_interval      = 100,
		.rc_map_table     = rc_map_d680_dmb_table,
		.rc_map_size      = ARRAY_SIZE(rc_map_d680_dmb_table),
		.rc_query         = cxusb_d680_dmb_rc_query,
	},

	.num_device_descs = 1,
	.devices = {
		{
			"Conexant DMB-TH Stick",
			{ NULL },
			{ &cxusb_table[18], NULL },
		},
	}
};

static struct dvb_usb_device_properties cxusb_mygica_d689_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl         = CYPRESS_FX2,

	.size_of_priv     = sizeof(struct cxusb_state),

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.streaming_ctrl   = cxusb_d680_dmb_streaming_ctrl,
			.frontend_attach  = cxusb_mygica_d689_frontend_attach,
			.tuner_attach     = cxusb_mygica_d689_tuner_attach,

			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 5,
				.endpoint = 0x02,
				.u = {
					.bulk = {
						.buffersize = 8192,
					}
				}
			},
		}},
		},
	},

	.power_ctrl       = cxusb_d680_dmb_power_ctrl,

	.i2c_algo         = &cxusb_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,

	.rc.legacy = {
		.rc_interval      = 100,
		.rc_map_table     = rc_map_d680_dmb_table,
		.rc_map_size      = ARRAY_SIZE(rc_map_d680_dmb_table),
		.rc_query         = cxusb_d680_dmb_rc_query,
	},

	.num_device_descs = 1,
	.devices = {
		{
			"Mygica D689 DMB-TH",
			{ NULL },
			{ &cxusb_table[19], NULL },
		},
	}
};

static struct dvb_usb_device_properties cxusb_tt_ct2_4400_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl         = CYPRESS_FX2,

	.size_of_priv     = sizeof(struct cxusb_state),

	.num_adapters = 1,
	.read_mac_address = cxusb_tt_ct2_4400_read_mac_address,

	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.streaming_ctrl   = cxusb_streaming_ctrl,
			/* both frontend and tuner attached in the
			   same function */
			.frontend_attach  = cxusb_tt_ct2_4400_attach,

			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 8,
				.endpoint = 0x82,
				.u = {
					.bulk = {
						.buffersize = 4096,
					}
				}
			},
		} },
		},
	},

	.i2c_algo = &cxusb_i2c_algo,
	.generic_bulk_ctrl_endpoint = 0x01,
	.generic_bulk_ctrl_endpoint_response = 0x81,

	.rc.core = {
		.rc_codes       = RC_MAP_TT_1500,
		.allowed_protos = RC_BIT_RC5,
		.rc_query       = cxusb_tt_ct2_4400_rc_query,
		.rc_interval    = 150,
	},

	.num_device_descs = 2,
	.devices = {
		{
			"TechnoTrend TVStick CT2-4400",
			{ NULL },
			{ &cxusb_table[20], NULL },
		},
		{
			"TechnoTrend TT-connect CT2-4650 CI",
			{ NULL },
			{ &cxusb_table[21], NULL },
		},
	}
};

static struct usb_driver cxusb_driver = {
	.name		= "dvb_usb_cxusb",
	.probe		= cxusb_probe,
	.disconnect     = cxusb_disconnect,
	.id_table	= cxusb_table,
};

module_usb_driver(cxusb_driver);

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@desy.de>");
MODULE_AUTHOR("Michael Krufky <mkrufky@linuxtv.org>");
MODULE_AUTHOR("Chris Pascoe <c.pascoe@itee.uq.edu.au>");
MODULE_DESCRIPTION("Driver for Conexant USB2.0 hybrid reference design");
MODULE_VERSION("1.0-alpha");
MODULE_LICENSE("GPL");
