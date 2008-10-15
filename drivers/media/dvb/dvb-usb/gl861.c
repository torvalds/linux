/* DVB USB compliant linux driver for GL861 USB2.0 devices.
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#include "gl861.h"

#include "zl10353.h"
#include "qt1010.h"

/* debug */
static int dvb_usb_gl861_debug;
module_param_named(debug, dvb_usb_gl861_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=rc (or-able))."
	DVB_USB_DEBUG_STATUS);
DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static int gl861_i2c_msg(struct dvb_usb_device *d, u8 addr,
			 u8 *wbuf, u16 wlen, u8 *rbuf, u16 rlen)
{
	u16 index;
	u16 value = addr << (8 + 1);
	int wo = (rbuf == NULL || rlen == 0); /* write-only */
	u8 req, type;

	if (wo) {
		req = GL861_REQ_I2C_WRITE;
		type = GL861_WRITE;
	} else { /* rw */
		req = GL861_REQ_I2C_READ;
		type = GL861_READ;
	}

	switch (wlen) {
	case 1:
		index = wbuf[0];
		break;
	case 2:
		index = wbuf[0];
		value = value + wbuf[1];
		break;
	default:
		warn("wlen = %x, aborting.", wlen);
		return -EINVAL;
	}

	msleep(1); /* avoid I2C errors */

	return usb_control_msg(d->udev, usb_rcvctrlpipe(d->udev, 0), req, type,
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
		if (i+1 < num && (msg[i+1].flags & I2C_M_RD)) {
			if (gl861_i2c_msg(d, msg[i].addr, msg[i].buf,
				msg[i].len, msg[i+1].buf, msg[i+1].len) < 0)
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

static struct i2c_algorithm gl861_i2c_algo = {
	.master_xfer   = gl861_i2c_xfer,
	.functionality = gl861_i2c_func,
};

/* Callbacks for DVB USB */
static struct zl10353_config gl861_zl10353_config = {
	.demod_address = 0x0f,
	.no_tuner = 1,
	.parallel_ts = 1,
};

static int gl861_frontend_attach(struct dvb_usb_adapter *adap)
{

	adap->fe = dvb_attach(zl10353_attach, &gl861_zl10353_config,
		&adap->dev->i2c_adap);
	if (adap->fe == NULL)
		return -EIO;

	return 0;
}

static struct qt1010_config gl861_qt1010_config = {
	.i2c_address = 0x62
};

static int gl861_tuner_attach(struct dvb_usb_adapter *adap)
{
	return dvb_attach(qt1010_attach,
			  adap->fe, &adap->dev->i2c_adap,
			  &gl861_qt1010_config) == NULL ? -ENODEV : 0;
}

/* DVB USB Driver stuff */
static struct dvb_usb_device_properties gl861_properties;

static int gl861_probe(struct usb_interface *intf,
		       const struct usb_device_id *id)
{
	struct dvb_usb_device *d;
	struct usb_host_interface *alt;
	int ret;

	if (intf->num_altsetting < 2)
		return -ENODEV;

	ret = dvb_usb_device_init(intf, &gl861_properties, THIS_MODULE, &d,
				  adapter_nr);
	if (ret == 0) {
		alt = usb_altnum_to_altsetting(intf, 0);

		if (alt == NULL) {
			deb_rc("not alt found!\n");
			return -ENODEV;
		}

		ret = usb_set_interface(d->udev, alt->desc.bInterfaceNumber,
					alt->desc.bAlternateSetting);
	}

	return ret;
}

static struct usb_device_id gl861_table [] = {
		{ USB_DEVICE(USB_VID_MSI, USB_PID_MSI_MEGASKY580_55801) },
		{ USB_DEVICE(USB_VID_ALINK, USB_VID_ALINK_DTU) },
		{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, gl861_table);

static struct dvb_usb_device_properties gl861_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,
	.usb_ctrl = DEVICE_SPECIFIC,

	.size_of_priv     = 0,

	.num_adapters = 1,
	.adapter = {{

		.frontend_attach  = gl861_frontend_attach,
		.tuner_attach     = gl861_tuner_attach,

		.stream = {
			.type = USB_BULK,
			.count = 7,
			.endpoint = 0x81,
			.u = {
				.bulk = {
					.buffersize = 512,
				}
			}
		},
	} },
	.i2c_algo         = &gl861_i2c_algo,

	.num_device_descs = 2,
	.devices = {
		{
			.name = "MSI Mega Sky 55801 DVB-T USB2.0",
			.cold_ids = { NULL },
			.warm_ids = { &gl861_table[0], NULL },
		},
		{
			.name = "A-LINK DTU DVB-T USB2.0",
			.cold_ids = { NULL },
			.warm_ids = { &gl861_table[1], NULL },
		},
	}
};

static struct usb_driver gl861_driver = {
	.name		= "dvb_usb_gl861",
	.probe		= gl861_probe,
	.disconnect	= dvb_usb_device_exit,
	.id_table	= gl861_table,
};

/* module stuff */
static int __init gl861_module_init(void)
{
	int ret;

	ret = usb_register(&gl861_driver);
	if (ret)
		err("usb_register failed. Error number %d", ret);

	return ret;
}

static void __exit gl861_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&gl861_driver);
}

module_init(gl861_module_init);
module_exit(gl861_module_exit);

MODULE_AUTHOR("Carl Lundqvist <comabug@gmail.com>");
MODULE_DESCRIPTION("Driver MSI Mega Sky 580 DVB-T USB2.0 / GL861");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");
