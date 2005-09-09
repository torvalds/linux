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
 * TODO: check if the cx25840-driver (from ivtv) can be used for the analogue
 * part
 *
 * Copyright (C) 2005 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#include "cxusb.h"

#include "cx22702.h"

/* debug */
int dvb_usb_cxusb_debug;
module_param_named(debug,dvb_usb_cxusb_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=rc (or-able))." DVB_USB_DEBUG_STATUS);

static int cxusb_ctrl_msg(struct dvb_usb_device *d,
		u8 cmd, u8 *wbuf, int wlen, u8 *rbuf, int rlen)
{
	int wo = (rbuf == NULL || rlen == 0); /* write-only */
	u8 sndbuf[1+wlen];
	memset(sndbuf,0,1+wlen);

	sndbuf[0] = cmd;
	memcpy(&sndbuf[1],wbuf,wlen);
	if (wo)
		dvb_usb_generic_write(d,sndbuf,1+wlen);
	else
		dvb_usb_generic_rw(d,sndbuf,1+wlen,rbuf,rlen,0);

	return 0;
}

/* GPIO */
static void cxusb_gpio_tuner(struct dvb_usb_device *d, int onoff)
{
	struct cxusb_state *st = d->priv;
	u8 o[2],i;

	if (st->gpio_write_state[GPIO_TUNER] == onoff)
		return;

	o[0] = GPIO_TUNER;
	o[1] = onoff;
	cxusb_ctrl_msg(d,CMD_GPIO_WRITE,o,2,&i,1);

	if (i != 0x01)
		deb_info("gpio_write failed.\n");

	st->gpio_write_state[GPIO_TUNER] = onoff;
}

/* I2C */
static int cxusb_i2c_xfer(struct i2c_adapter *adap,struct i2c_msg msg[],int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int i;

	if (down_interruptible(&d->i2c_sem) < 0)
		return -EAGAIN;

	if (num > 2)
		warn("more than 2 i2c messages at a time is not handled yet. TODO.");

	for (i = 0; i < num; i++) {

		switch (msg[i].addr) {
			case 0x63:
				cxusb_gpio_tuner(d,0);
				break;
			default:
				cxusb_gpio_tuner(d,1);
				break;
		}

		/* read request */
		if (i+1 < num && (msg[i+1].flags & I2C_M_RD)) {
			u8 obuf[3+msg[i].len], ibuf[1+msg[i+1].len];
			obuf[0] = msg[i].len;
			obuf[1] = msg[i+1].len;
			obuf[2] = msg[i].addr;
			memcpy(&obuf[3],msg[i].buf,msg[i].len);

			if (cxusb_ctrl_msg(d, CMD_I2C_READ,
						obuf, 3+msg[i].len,
						ibuf, 1+msg[i+1].len) < 0)
				break;

			if (ibuf[0] != 0x08)
				deb_info("i2c read could have been failed\n");

			memcpy(msg[i+1].buf,&ibuf[1],msg[i+1].len);

			i++;
		} else { /* write */
			u8 obuf[2+msg[i].len], ibuf;
			obuf[0] = msg[i].addr;
			obuf[1] = msg[i].len;
			memcpy(&obuf[2],msg[i].buf,msg[i].len);

			if (cxusb_ctrl_msg(d,CMD_I2C_WRITE, obuf, 2+msg[i].len, &ibuf,1) < 0)
				break;
			if (ibuf != 0x08)
				deb_info("i2c write could have been failed\n");
		}
	}

	up(&d->i2c_sem);
	return i;
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

static int cxusb_streaming_ctrl(struct dvb_usb_device *d, int onoff)
{
	u8 buf[2] = { 0x03, 0x00 };
	if (onoff)
		cxusb_ctrl_msg(d,CMD_STREAMING_ON, buf, 2, NULL, 0);
	else
		cxusb_ctrl_msg(d,CMD_STREAMING_OFF, NULL, 0, NULL, 0);

	return 0;
}

struct cx22702_config cxusb_cx22702_config = {
	.demod_address = 0x63,

	.output_mode = CX22702_PARALLEL_OUTPUT,

	.pll_init = dvb_usb_pll_init_i2c,
	.pll_set  = dvb_usb_pll_set_i2c,
};

/* Callbacks for DVB USB */
static int cxusb_tuner_attach(struct dvb_usb_device *d)
{
	u8 bpll[4] = { 0x0b, 0xdc, 0x9c, 0xa0 };
	d->pll_addr = 0x61;
	memcpy(d->pll_init,bpll,4);
	d->pll_desc = &dvb_pll_fmd1216me;
	return 0;
}

static int cxusb_frontend_attach(struct dvb_usb_device *d)
{
	u8 b;
	if (usb_set_interface(d->udev,0,6) < 0)
		err("set interface failed");

	cxusb_ctrl_msg(d,CMD_DIGITAL, NULL, 0, &b, 1);

	if ((d->fe = cx22702_attach(&cxusb_cx22702_config, &d->i2c_adap)) != NULL)
		return 0;

	return -EIO;
}

/* DVB USB Driver stuff */
static struct dvb_usb_properties cxusb_properties;

static int cxusb_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	return dvb_usb_device_init(intf,&cxusb_properties,THIS_MODULE,NULL);
}

static struct usb_device_id cxusb_table [] = {
		{ USB_DEVICE(USB_VID_MEDION, USB_PID_MEDION_MD95700) },
		{}		/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, cxusb_table);

static struct dvb_usb_properties cxusb_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = CYPRESS_FX2,

	.size_of_priv     = sizeof(struct cxusb_state),

	.streaming_ctrl   = cxusb_streaming_ctrl,
	.power_ctrl       = cxusb_power_ctrl,
	.frontend_attach  = cxusb_frontend_attach,
	.tuner_attach     = cxusb_tuner_attach,

	.i2c_algo         = &cxusb_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,
	/* parameter for the MPEG2-data transfer */
	.urb = {
		.type = DVB_USB_BULK,
		.count = 5,
		.endpoint = 0x02,
		.u = {
			.bulk = {
				.buffersize = 8192,
			}
		}
	},

	.num_device_descs = 1,
	.devices = {
		{   "Medion MD95700 (MDUSBTV-HYBRID)",
			{ NULL },
			{ &cxusb_table[0], NULL },
		},
	}
};

static struct usb_driver cxusb_driver = {
	.owner		= THIS_MODULE,
	.name		= "dvb_usb_cxusb",
	.probe		= cxusb_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table	= cxusb_table,
};

/* module stuff */
static int __init cxusb_module_init(void)
{
	int result;
	if ((result = usb_register(&cxusb_driver))) {
		err("usb_register failed. Error number %d",result);
		return result;
	}

	return 0;
}

static void __exit cxusb_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&cxusb_driver);
}

module_init (cxusb_module_init);
module_exit (cxusb_module_exit);

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@desy.de>");
MODULE_DESCRIPTION("Driver for Conexant USB2.0 hybrid reference design");
MODULE_VERSION("1.0-alpha");
MODULE_LICENSE("GPL");
