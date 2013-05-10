/*
 * DVB USB Linux driver for AME DTV-5100 USB2.0 DVB-T
 *
 * Copyright (C) 2008  Antoine Jacquet <royale@zerezo.com>
 * http://royale.zerezo.com/dtv5100/
 *
 * Inspired by gl861.c and au6610.c drivers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "dtv5100.h"
#include "zl10353.h"
#include "qt1010.h"

/* debug */
static int dvb_usb_dtv5100_debug;
module_param_named(debug, dvb_usb_dtv5100_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level" DVB_USB_DEBUG_STATUS);
DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static int dtv5100_i2c_msg(struct dvb_usb_device *d, u8 addr,
			   u8 *wbuf, u16 wlen, u8 *rbuf, u16 rlen)
{
	u8 request;
	u8 type;
	u16 value;
	u16 index;

	switch (wlen) {
	case 1:
		/* write { reg }, read { value } */
		request = (addr == DTV5100_DEMOD_ADDR ? DTV5100_DEMOD_READ :
							DTV5100_TUNER_READ);
		type = USB_TYPE_VENDOR | USB_DIR_IN;
		value = 0;
		break;
	case 2:
		/* write { reg, value } */
		request = (addr == DTV5100_DEMOD_ADDR ? DTV5100_DEMOD_WRITE :
							DTV5100_TUNER_WRITE);
		type = USB_TYPE_VENDOR | USB_DIR_OUT;
		value = wbuf[1];
		break;
	default:
		warn("wlen = %x, aborting.", wlen);
		return -EINVAL;
	}
	index = (addr << 8) + wbuf[0];

	msleep(1); /* avoid I2C errors */
	return usb_control_msg(d->udev, usb_rcvctrlpipe(d->udev, 0), request,
			       type, value, index, rbuf, rlen,
			       DTV5100_USB_TIMEOUT);
}

/* I2C */
static int dtv5100_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msg[],
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
			if (dtv5100_i2c_msg(d, msg[i].addr, msg[i].buf,
					    msg[i].len, msg[i+1].buf,
					    msg[i+1].len) < 0)
				break;
			i++;
		} else if (dtv5100_i2c_msg(d, msg[i].addr, msg[i].buf,
					   msg[i].len, NULL, 0) < 0)
				break;
	}

	mutex_unlock(&d->i2c_mutex);
	return i;
}

static u32 dtv5100_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm dtv5100_i2c_algo = {
	.master_xfer   = dtv5100_i2c_xfer,
	.functionality = dtv5100_i2c_func,
};

/* Callbacks for DVB USB */
static struct zl10353_config dtv5100_zl10353_config = {
	.demod_address = DTV5100_DEMOD_ADDR,
	.no_tuner = 1,
	.parallel_ts = 1,
};

static int dtv5100_frontend_attach(struct dvb_usb_adapter *adap)
{
	adap->fe = dvb_attach(zl10353_attach, &dtv5100_zl10353_config,
			      &adap->dev->i2c_adap);
	if (adap->fe == NULL)
		return -EIO;

	/* disable i2c gate, or it won't work... is this safe? */
	adap->fe->ops.i2c_gate_ctrl = NULL;

	return 0;
}

static struct qt1010_config dtv5100_qt1010_config = {
	.i2c_address = DTV5100_TUNER_ADDR
};

static int dtv5100_tuner_attach(struct dvb_usb_adapter *adap)
{
	return dvb_attach(qt1010_attach,
			  adap->fe, &adap->dev->i2c_adap,
			  &dtv5100_qt1010_config) == NULL ? -ENODEV : 0;
}

/* DVB USB Driver stuff */
static struct dvb_usb_device_properties dtv5100_properties;

static int dtv5100_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	int i, ret;
	struct usb_device *udev = interface_to_usbdev(intf);

	/* initialize non qt1010/zl10353 part? */
	for (i = 0; dtv5100_init[i].request; i++) {
		ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				      dtv5100_init[i].request,
				      USB_TYPE_VENDOR | USB_DIR_OUT,
				      dtv5100_init[i].value,
				      dtv5100_init[i].index, NULL, 0,
				      DTV5100_USB_TIMEOUT);
		if (ret)
			return ret;
	}

	ret = dvb_usb_device_init(intf, &dtv5100_properties,
				  THIS_MODULE, NULL, adapter_nr);
	if (ret)
		return ret;

	return 0;
}

static struct usb_device_id dtv5100_table[] = {
	{ USB_DEVICE(0x06be, 0xa232) },
	{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, dtv5100_table);

static struct dvb_usb_device_properties dtv5100_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,
	.usb_ctrl = DEVICE_SPECIFIC,

	.size_of_priv = 0,

	.num_adapters = 1,
	.adapter = {{
		.frontend_attach = dtv5100_frontend_attach,
		.tuner_attach    = dtv5100_tuner_attach,

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

	.i2c_algo = &dtv5100_i2c_algo,

	.num_device_descs = 1,
	.devices = {
		{
			.name = "AME DTV-5100 USB2.0 DVB-T",
			.cold_ids = { NULL },
			.warm_ids = { &dtv5100_table[0], NULL },
		},
	}
};

static struct usb_driver dtv5100_driver = {
	.name		= "dvb_usb_dtv5100",
	.probe		= dtv5100_probe,
	.disconnect	= dvb_usb_device_exit,
	.id_table	= dtv5100_table,
};

/* module stuff */
static int __init dtv5100_module_init(void)
{
	int ret;

	ret = usb_register(&dtv5100_driver);
	if (ret)
		err("usb_register failed. Error number %d", ret);

	return ret;
}

static void __exit dtv5100_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&dtv5100_driver);
}

module_init(dtv5100_module_init);
module_exit(dtv5100_module_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
