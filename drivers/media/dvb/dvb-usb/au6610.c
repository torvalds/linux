/* DVB USB compliant linux driver for Sigmatek DVB-110 DVB-T USB2.0 receiver
 *
 * Copyright (C) 2006 Antti Palosaari <crope@iki.fi>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */

#include "au6610.h"

#include "zl10353.h"
#include "qt1010.h"

/* debug */
static int dvb_usb_au6610_debug;
module_param_named(debug, dvb_usb_au6610_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=rc (or-able))." DVB_USB_DEBUG_STATUS);

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static int au6610_usb_msg(struct dvb_usb_device *d, u8 operation, u8 addr,
			  u8 *wbuf, u16 wlen, u8 *rbuf, u16 rlen)
{
	int ret;
	u16 index;
	u8 usb_buf[6]; /* enough for all known requests,
			  read returns 5 and write 6 bytes */
	switch (wlen) {
	case 1:
		index = wbuf[0] << 8;
		break;
	case 2:
		index = wbuf[0] << 8;
		index += wbuf[1];
		break;
	default:
		warn("wlen = %x, aborting.", wlen);
		return -EINVAL;
	}

	ret = usb_control_msg(d->udev, usb_rcvctrlpipe(d->udev, 0), operation,
			      USB_TYPE_VENDOR|USB_DIR_IN, addr << 1, index, usb_buf,
			      sizeof(usb_buf), AU6610_USB_TIMEOUT);

	if (ret < 0)
		return ret;

	switch (operation) {
	case AU6610_REQ_I2C_READ:
	case AU6610_REQ_USB_READ:
		/* requested value is always 5th byte in buffer */
		rbuf[0] = usb_buf[4];
	}

	return ret;
}

static int au6610_i2c_msg(struct dvb_usb_device *d, u8 addr,
			  u8 *wbuf, u16 wlen, u8 *rbuf, u16 rlen)
{
	u8 request;
	u8 wo = (rbuf == NULL || rlen == 0); /* write-only */

	if (wo) {
		request = AU6610_REQ_I2C_WRITE;
	} else { /* rw */
		request = AU6610_REQ_I2C_READ;
	}

	return au6610_usb_msg(d, request, addr, wbuf, wlen, rbuf, rlen);
}


/* I2C */
static int au6610_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msg[],
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
		if (i+1 < num && (msg[i+1].flags & I2C_M_RD)) {
			if (au6610_i2c_msg(d, msg[i].addr, msg[i].buf,
					   msg[i].len, msg[i+1].buf,
					   msg[i+1].len) < 0)
				break;
			i++;
		} else if (au6610_i2c_msg(d, msg[i].addr, msg[i].buf,
					       msg[i].len, NULL, 0) < 0)
				break;
	}

	mutex_unlock(&d->i2c_mutex);
	return i;
}


static u32 au6610_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm au6610_i2c_algo = {
	.master_xfer   = au6610_i2c_xfer,
	.functionality = au6610_i2c_func,
};

/* Callbacks for DVB USB */
static int au6610_identify_state(struct usb_device *udev,
				 struct dvb_usb_device_properties *props,
				 struct dvb_usb_device_description **desc,
				 int *cold)
{
	*cold = 0;
	return 0;
}

static struct zl10353_config au6610_zl10353_config = {
	.demod_address = 0x0f,
	.no_tuner = 1,
	.parallel_ts = 1,
};

static int au6610_zl10353_frontend_attach(struct dvb_usb_adapter *adap)
{
	if ((adap->fe = dvb_attach(zl10353_attach, &au6610_zl10353_config,
				   &adap->dev->i2c_adap)) != NULL) {
		return 0;
	}

	return -EIO;
}

static struct qt1010_config au6610_qt1010_config = {
	.i2c_address = 0x62
};

static int au6610_qt1010_tuner_attach(struct dvb_usb_adapter *adap)
{
	return dvb_attach(qt1010_attach,
			  adap->fe, &adap->dev->i2c_adap,
			  &au6610_qt1010_config) == NULL ? -ENODEV : 0;
}

/* DVB USB Driver stuff */
static struct dvb_usb_device_properties au6610_properties;

static int au6610_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	struct dvb_usb_device *d;
	struct usb_host_interface *alt;
	int ret;

	if (intf->num_altsetting < AU6610_ALTSETTING_COUNT)
		return -ENODEV;

	ret = dvb_usb_device_init(intf, &au6610_properties, THIS_MODULE, &d,
				  adapter_nr);
	if (ret == 0) {
		alt = usb_altnum_to_altsetting(intf, AU6610_ALTSETTING);

		if (alt == NULL) {
			deb_rc("no alt found!\n");
			return -ENODEV;
		}
		ret = usb_set_interface(d->udev, alt->desc.bInterfaceNumber,
					alt->desc.bAlternateSetting);
	}

	return ret;
}


static struct usb_device_id au6610_table [] = {
	{ USB_DEVICE(USB_VID_ALCOR_MICRO, USB_PID_SIGMATEK_DVB_110) },
	{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, au6610_table);

static struct dvb_usb_device_properties au6610_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,
	.usb_ctrl = DEVICE_SPECIFIC,
	.size_of_priv     = 0,
	.identify_state   = au6610_identify_state,
	.num_adapters = 1,
	.adapter = {
		{
			.frontend_attach  = au6610_zl10353_frontend_attach,
			.tuner_attach     = au6610_qt1010_tuner_attach,

			.stream = {
				.type = USB_ISOC,
				.count = 5,
				.endpoint = 0x82,
				.u = {
					.isoc = {
						.framesperurb = 40,
						.framesize = 942,   /* maximum packet size */
						.interval = 1.25,   /* 125 us */
					}
				}
			},
		}
	},
	.i2c_algo = &au6610_i2c_algo,
	.num_device_descs = 1,
	.devices = {
		{
			"Sigmatek DVB-110 DVB-T USB2.0",
			{ &au6610_table[0], NULL },
			{ NULL },
		},
	}
};

static struct usb_driver au6610_driver = {
	.name       = "dvb_usb_au6610",
	.probe      = au6610_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table   = au6610_table,
};

/* module stuff */
static int __init au6610_module_init(void)
{
	int ret;

	if ((ret = usb_register(&au6610_driver))) {
		err("usb_register failed. Error number %d", ret);
		return ret;
	}

	return 0;
}

static void __exit au6610_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&au6610_driver);
}

module_init (au6610_module_init);
module_exit (au6610_module_exit);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Driver Sigmatek DVB-110 DVB-T USB2.0 / AU6610");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");
