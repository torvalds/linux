/* DVB USB compliant Linux driver for the Friio USB2.0 ISDB-T receiver.
 *
 * Copyright (C) 2009 Akihiro Tsukada <tskd2@yahoo.co.jp>
 *
 * This module is based off the the gl861 and vp702x modules.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#include "friio.h"

/* debug */
int dvb_usb_friio_debug;
module_param_named(debug, dvb_usb_friio_debug, int, 0644);
MODULE_PARM_DESC(debug,
		 "set debugging level (1=info,2=xfer,4=rc,8=fe (or-able))."
		 DVB_USB_DEBUG_STATUS);

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

/**
 * Indirect I2C access to the PLL via FE.
 * whole I2C protocol data to the PLL is sent via the FE's I2C register.
 * This is done by a control msg to the FE with the I2C data accompanied, and
 * a specific USB request number is assigned for that purpose.
 *
 * this func sends wbuf[1..] to the I2C register wbuf[0] at addr (= at FE).
 * TODO: refoctored, smarter i2c functions.
 */
static int gl861_i2c_ctrlmsg_data(struct dvb_usb_device *d, u8 addr,
				  u8 *wbuf, u16 wlen, u8 *rbuf, u16 rlen)
{
	u16 index = wbuf[0];	/* must be JDVBT90502_2ND_I2C_REG(=0xFE) */
	u16 value = addr << (8 + 1);
	int wo = (rbuf == NULL || rlen == 0);	/* write only */
	u8 req, type;

	deb_xfer("write to PLL:0x%02x via FE reg:0x%02x, len:%d\n",
		 wbuf[1], wbuf[0], wlen - 1);

	if (wo && wlen >= 2) {
		req = GL861_REQ_I2C_DATA_CTRL_WRITE;
		type = GL861_WRITE;
		udelay(20);
		return usb_control_msg(d->udev, usb_sndctrlpipe(d->udev, 0),
				       req, type, value, index,
				       &wbuf[1], wlen - 1, 2000);
	}

	deb_xfer("not supported ctrl-msg, aborting.");
	return -EINVAL;
}

/* normal I2C access (without extra data arguments).
 * write to the register wbuf[0] at I2C address addr with the value wbuf[1],
 *  or read from the register wbuf[0].
 * register address can be 16bit (wbuf[2]<<8 | wbuf[0]) if wlen==3
 */
static int gl861_i2c_msg(struct dvb_usb_device *d, u8 addr,
			 u8 *wbuf, u16 wlen, u8 *rbuf, u16 rlen)
{
	u16 index;
	u16 value = addr << (8 + 1);
	int wo = (rbuf == NULL || rlen == 0);	/* write-only */
	u8 req, type;
	unsigned int pipe;

	/* special case for the indirect I2C access to the PLL via FE, */
	if (addr == friio_fe_config.demod_address &&
	    wbuf[0] == JDVBT90502_2ND_I2C_REG)
		return gl861_i2c_ctrlmsg_data(d, addr, wbuf, wlen, rbuf, rlen);

	if (wo) {
		req = GL861_REQ_I2C_WRITE;
		type = GL861_WRITE;
		pipe = usb_sndctrlpipe(d->udev, 0);
	} else {		/* rw */
		req = GL861_REQ_I2C_READ;
		type = GL861_READ;
		pipe = usb_rcvctrlpipe(d->udev, 0);
	}

	switch (wlen) {
	case 1:
		index = wbuf[0];
		break;
	case 2:
		index = wbuf[0];
		value = value + wbuf[1];
		break;
	case 3:
		/* special case for 16bit register-address */
		index = (wbuf[2] << 8) | wbuf[0];
		value = value + wbuf[1];
		break;
	default:
		deb_xfer("wlen = %x, aborting.", wlen);
		return -EINVAL;
	}
	msleep(1);
	return usb_control_msg(d->udev, pipe, req, type,
			       value, index, rbuf, rlen, 2000);
}

/* I2C */
static int gl861_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msg[],
			  int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int i;


	if (num > 2)
		return -EINVAL;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	for (i = 0; i < num; i++) {
		/* write/read request */
		if (i + 1 < num && (msg[i + 1].flags & I2C_M_RD)) {
			if (gl861_i2c_msg(d, msg[i].addr,
					  msg[i].buf, msg[i].len,
					  msg[i + 1].buf, msg[i + 1].len) < 0)
				break;
			i++;
		} else
			if (gl861_i2c_msg(d, msg[i].addr, msg[i].buf,
					  msg[i].len, NULL, 0) < 0)
				break;
	}

	mutex_unlock(&d->i2c_mutex);
	return i;
}

static u32 gl861_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static int friio_ext_ctl(struct dvb_usb_adapter *adap,
			 u32 sat_color, int lnb_on)
{
	int i;
	int ret;
	struct i2c_msg msg;
	u8 *buf;
	u32 mask;
	u8 lnb = (lnb_on) ? FRIIO_CTL_LNB : 0;

	buf = kmalloc(2, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	msg.addr = 0x00;
	msg.flags = 0;
	msg.len = 2;
	msg.buf = buf;

	buf[0] = 0x00;

	/* send 2bit header (&B10) */
	buf[1] = lnb | FRIIO_CTL_LED | FRIIO_CTL_STROBE;
	ret = gl861_i2c_xfer(&adap->dev->i2c_adap, &msg, 1);
	buf[1] |= FRIIO_CTL_CLK;
	ret += gl861_i2c_xfer(&adap->dev->i2c_adap, &msg, 1);

	buf[1] = lnb | FRIIO_CTL_STROBE;
	ret += gl861_i2c_xfer(&adap->dev->i2c_adap, &msg, 1);
	buf[1] |= FRIIO_CTL_CLK;
	ret += gl861_i2c_xfer(&adap->dev->i2c_adap, &msg, 1);

	/* send 32bit(satur, R, G, B) data in serial */
	mask = 1 << 31;
	for (i = 0; i < 32; i++) {
		buf[1] = lnb | FRIIO_CTL_STROBE;
		if (sat_color & mask)
			buf[1] |= FRIIO_CTL_LED;
		ret += gl861_i2c_xfer(&adap->dev->i2c_adap, &msg, 1);
		buf[1] |= FRIIO_CTL_CLK;
		ret += gl861_i2c_xfer(&adap->dev->i2c_adap, &msg, 1);
		mask >>= 1;
	}

	/* set the strobe off */
	buf[1] = lnb;
	ret += gl861_i2c_xfer(&adap->dev->i2c_adap, &msg, 1);
	buf[1] |= FRIIO_CTL_CLK;
	ret += gl861_i2c_xfer(&adap->dev->i2c_adap, &msg, 1);

	kfree(buf);
	return (ret == 70);
}


static int friio_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff);

/* TODO: move these init cmds to the FE's init routine? */
static u8 streaming_init_cmds[][2] = {
	{0x33, 0x08},
	{0x37, 0x40},
	{0x3A, 0x1F},
	{0x3B, 0xFF},
	{0x3C, 0x1F},
	{0x3D, 0xFF},
	{0x38, 0x00},
	{0x35, 0x00},
	{0x39, 0x00},
	{0x36, 0x00},
};
static int cmdlen = sizeof(streaming_init_cmds) / 2;

/*
 * Command sequence in this init function is a replay
 *  of the captured USB commands from the Windows proprietary driver.
 */
static int friio_initialize(struct dvb_usb_device *d)
{
	int ret;
	int i;
	int retry = 0;
	u8 *rbuf, *wbuf;

	deb_info("%s called.\n", __func__);

	wbuf = kmalloc(3, GFP_KERNEL);
	if (!wbuf)
		return -ENOMEM;

	rbuf = kmalloc(2, GFP_KERNEL);
	if (!rbuf) {
		kfree(wbuf);
		return -ENOMEM;
	}

	/* use gl861_i2c_msg instead of gl861_i2c_xfer(), */
	/* because the i2c device is not set up yet. */
	wbuf[0] = 0x11;
	wbuf[1] = 0x02;
	ret = gl861_i2c_msg(d, 0x00, wbuf, 2, NULL, 0);
	if (ret < 0)
		goto error;
	msleep(2);

	wbuf[0] = 0x11;
	wbuf[1] = 0x00;
	ret = gl861_i2c_msg(d, 0x00, wbuf, 2, NULL, 0);
	if (ret < 0)
		goto error;
	msleep(1);

	/* following msgs should be in the FE's init code? */
	/* cmd sequence to identify the device type? (friio black/white) */
	wbuf[0] = 0x03;
	wbuf[1] = 0x80;
	/* can't use gl861_i2c_cmd, as the register-addr is 16bit(0x0100) */
	ret = usb_control_msg(d->udev, usb_sndctrlpipe(d->udev, 0),
			      GL861_REQ_I2C_DATA_CTRL_WRITE, GL861_WRITE,
			      0x1200, 0x0100, wbuf, 2, 2000);
	if (ret < 0)
		goto error;

	msleep(2);
	wbuf[0] = 0x00;
	wbuf[2] = 0x01;		/* reg.0x0100 */
	wbuf[1] = 0x00;
	ret = gl861_i2c_msg(d, 0x12 >> 1, wbuf, 3, rbuf, 2);
	/* my Friio White returns 0xffff. */
	if (ret < 0 || rbuf[0] != 0xff || rbuf[1] != 0xff)
		goto error;

	msleep(2);
	wbuf[0] = 0x03;
	wbuf[1] = 0x80;
	ret = usb_control_msg(d->udev, usb_sndctrlpipe(d->udev, 0),
			      GL861_REQ_I2C_DATA_CTRL_WRITE, GL861_WRITE,
			      0x9000, 0x0100, wbuf, 2, 2000);
	if (ret < 0)
		goto error;

	msleep(2);
	wbuf[0] = 0x00;
	wbuf[2] = 0x01;		/* reg.0x0100 */
	wbuf[1] = 0x00;
	ret = gl861_i2c_msg(d, 0x90 >> 1, wbuf, 3, rbuf, 2);
	/* my Friio White returns 0xffff again. */
	if (ret < 0 || rbuf[0] != 0xff || rbuf[1] != 0xff)
		goto error;

	msleep(1);

restart:
	/* ============ start DEMOD init cmds ================== */
	/* read PLL status to clear the POR bit */
	wbuf[0] = JDVBT90502_2ND_I2C_REG;
	wbuf[1] = (FRIIO_PLL_ADDR << 1) + 1;	/* +1 for reading */
	ret = gl861_i2c_msg(d, FRIIO_DEMOD_ADDR, wbuf, 2, NULL, 0);
	if (ret < 0)
		goto error;

	msleep(5);
	/* note: DEMODULATOR has 16bit register-address. */
	wbuf[0] = 0x00;
	wbuf[2] = 0x01;		/* reg addr: 0x0100 */
	wbuf[1] = 0x00;		/* val: not used */
	ret = gl861_i2c_msg(d, FRIIO_DEMOD_ADDR, wbuf, 3, rbuf, 1);
	if (ret < 0)
		goto error;
/*
	msleep(1);
	wbuf[0] = 0x80;
	wbuf[1] = 0x00;
	ret = gl861_i2c_msg(d, FRIIO_DEMOD_ADDR, wbuf, 2, rbuf, 1);
	if (ret < 0)
		goto error;
 */
	if (rbuf[0] & 0x80) {	/* still in PowerOnReset state? */
		if (++retry > 3) {
			deb_info("failed to get the correct"
				 " FE demod status:0x%02x\n", rbuf[0]);
			goto error;
		}
		msleep(100);
		goto restart;
	}

	/* TODO: check return value in rbuf */
	/* =========== end DEMOD init cmds ===================== */
	msleep(1);

	wbuf[0] = 0x30;
	wbuf[1] = 0x04;
	ret = gl861_i2c_msg(d, 0x00, wbuf, 2, NULL, 0);
	if (ret < 0)
		goto error;

	msleep(2);
	/* following 2 cmds unnecessary? */
	wbuf[0] = 0x00;
	wbuf[1] = 0x01;
	ret = gl861_i2c_msg(d, 0x00, wbuf, 2, NULL, 0);
	if (ret < 0)
		goto error;

	wbuf[0] = 0x06;
	wbuf[1] = 0x0F;
	ret = gl861_i2c_msg(d, 0x00, wbuf, 2, NULL, 0);
	if (ret < 0)
		goto error;

	/* some streaming ctl cmds (maybe) */
	msleep(10);
	for (i = 0; i < cmdlen; i++) {
		ret = gl861_i2c_msg(d, 0x00, streaming_init_cmds[i], 2,
				    NULL, 0);
		if (ret < 0)
			goto error;
		msleep(1);
	}
	msleep(20);

	/* change the LED color etc. */
	ret = friio_streaming_ctrl(&d->adapter[0], 0);
	if (ret < 0)
		goto error;

	return 0;

error:
	kfree(wbuf);
	kfree(rbuf);
	deb_info("%s:ret == %d\n", __func__, ret);
	return -EIO;
}

/* Callbacks for DVB USB */

static int friio_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	int ret;

	deb_info("%s called.(%d)\n", __func__, onoff);

	/* set the LED color and saturation (and LNB on) */
	if (onoff)
		ret = friio_ext_ctl(adap, 0x6400ff64, 1);
	else
		ret = friio_ext_ctl(adap, 0x96ff00ff, 1);

	if (ret != 1) {
		deb_info("%s failed to send cmdx. ret==%d\n", __func__, ret);
		return -EREMOTEIO;
	}
	return 0;
}

static int friio_frontend_attach(struct dvb_usb_adapter *adap)
{
	if (friio_initialize(adap->dev) < 0)
		return -EIO;

	adap->fe_adap[0].fe = jdvbt90502_attach(adap->dev);
	if (adap->fe_adap[0].fe == NULL)
		return -EIO;

	return 0;
}

/* DVB USB Driver stuff */
static struct dvb_usb_device_properties friio_properties;

static int friio_probe(struct usb_interface *intf,
		       const struct usb_device_id *id)
{
	struct dvb_usb_device *d;
	struct usb_host_interface *alt;
	int ret;

	if (intf->num_altsetting < GL861_ALTSETTING_COUNT)
		return -ENODEV;

	alt = usb_altnum_to_altsetting(intf, FRIIO_BULK_ALTSETTING);
	if (alt == NULL) {
		deb_rc("not alt found!\n");
		return -ENODEV;
	}
	ret = usb_set_interface(interface_to_usbdev(intf),
				alt->desc.bInterfaceNumber,
				alt->desc.bAlternateSetting);
	if (ret != 0) {
		deb_rc("failed to set alt-setting!\n");
		return ret;
	}

	ret = dvb_usb_device_init(intf, &friio_properties,
				  THIS_MODULE, &d, adapter_nr);
	if (ret == 0)
		friio_streaming_ctrl(&d->adapter[0], 1);

	return ret;
}


struct jdvbt90502_config friio_fe_config = {
	.demod_address = FRIIO_DEMOD_ADDR,
	.pll_address = FRIIO_PLL_ADDR,
};

static struct i2c_algorithm gl861_i2c_algo = {
	.master_xfer   = gl861_i2c_xfer,
	.functionality = gl861_i2c_func,
};

static struct usb_device_id friio_table[] = {
	{ USB_DEVICE(USB_VID_774, USB_PID_FRIIO_WHITE) },
	{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, friio_table);


static struct dvb_usb_device_properties friio_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,
	.usb_ctrl = DEVICE_SPECIFIC,

	.size_of_priv = 0,

	.num_adapters = 1,
	.adapter = {
		/* caps:0 =>  no pid filter, 188B TS packet */
		/* GL861 has a HW pid filter, but no info available. */
		{
		.num_frontends = 1,
		.fe = {{
			.caps  = 0,

			.frontend_attach  = friio_frontend_attach,
			.streaming_ctrl = friio_streaming_ctrl,

			.stream = {
				.type = USB_BULK,
				/* count <= MAX_NO_URBS_FOR_DATA_STREAM(10) */
				.count = 8,
				.endpoint = 0x01,
				.u = {
					/* GL861 has 6KB buf inside */
					.bulk = {
						.buffersize = 16384,
					}
				}
			},
		}},
		}
	},
	.i2c_algo = &gl861_i2c_algo,

	.num_device_descs = 1,
	.devices = {
		{
			.name = "774 Friio ISDB-T USB2.0",
			.cold_ids = { NULL },
			.warm_ids = { &friio_table[0], NULL },
		},
	}
};

static struct usb_driver friio_driver = {
	.name		= "dvb_usb_friio",
	.probe		= friio_probe,
	.disconnect	= dvb_usb_device_exit,
	.id_table	= friio_table,
};

module_usb_driver(friio_driver);

MODULE_AUTHOR("Akihiro Tsukada <tskd2@yahoo.co.jp>");
MODULE_DESCRIPTION("Driver for Friio ISDB-T USB2.0 Receiver");
MODULE_VERSION("0.2");
MODULE_LICENSE("GPL");
