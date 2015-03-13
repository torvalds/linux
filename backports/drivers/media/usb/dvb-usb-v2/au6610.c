/*
 * DVB USB Linux driver for Alcor Micro AU6610 DVB-T USB2.0.
 *
 * Copyright (C) 2006 Antti Palosaari <crope@iki.fi>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "au6610.h"
#include "zl10353.h"
#include "qt1010.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static int au6610_usb_msg(struct dvb_usb_device *d, u8 operation, u8 addr,
			  u8 *wbuf, u16 wlen, u8 *rbuf, u16 rlen)
{
	int ret;
	u16 index;
	u8 *usb_buf;

	/*
	 * allocate enough for all known requests,
	 * read returns 5 and write 6 bytes
	 */
	usb_buf = kmalloc(6, GFP_KERNEL);
	if (!usb_buf)
		return -ENOMEM;

	switch (wlen) {
	case 1:
		index = wbuf[0] << 8;
		break;
	case 2:
		index = wbuf[0] << 8;
		index += wbuf[1];
		break;
	default:
		dev_err(&d->udev->dev, "%s: wlen=%d, aborting\n",
				KBUILD_MODNAME, wlen);
		ret = -EINVAL;
		goto error;
	}

	ret = usb_control_msg(d->udev, usb_rcvctrlpipe(d->udev, 0), operation,
			      USB_TYPE_VENDOR|USB_DIR_IN, addr << 1, index,
			      usb_buf, 6, AU6610_USB_TIMEOUT);

	dvb_usb_dbg_usb_control_msg(d->udev, operation,
			(USB_TYPE_VENDOR|USB_DIR_IN), addr << 1, index,
			usb_buf, 6);

	if (ret < 0)
		goto error;

	switch (operation) {
	case AU6610_REQ_I2C_READ:
	case AU6610_REQ_USB_READ:
		/* requested value is always 5th byte in buffer */
		rbuf[0] = usb_buf[4];
	}
error:
	kfree(usb_buf);
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
static struct zl10353_config au6610_zl10353_config = {
	.demod_address = 0x0f,
	.no_tuner = 1,
	.parallel_ts = 1,
};

static int au6610_zl10353_frontend_attach(struct dvb_usb_adapter *adap)
{
	adap->fe[0] = dvb_attach(zl10353_attach, &au6610_zl10353_config,
			&adap_to_d(adap)->i2c_adap);
	if (adap->fe[0] == NULL)
		return -ENODEV;

	return 0;
}

static struct qt1010_config au6610_qt1010_config = {
	.i2c_address = 0x62
};

static int au6610_qt1010_tuner_attach(struct dvb_usb_adapter *adap)
{
	return dvb_attach(qt1010_attach, adap->fe[0],
			&adap_to_d(adap)->i2c_adap,
			&au6610_qt1010_config) == NULL ? -ENODEV : 0;
}

static int au6610_init(struct dvb_usb_device *d)
{
	/* TODO: this functionality belongs likely to the streaming control */
	/* bInterfaceNumber 0, bAlternateSetting 5 */
	return usb_set_interface(d->udev, 0, 5);
}

static struct dvb_usb_device_properties au6610_props = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,

	.i2c_algo = &au6610_i2c_algo,
	.frontend_attach = au6610_zl10353_frontend_attach,
	.tuner_attach = au6610_qt1010_tuner_attach,
	.init = au6610_init,

	.num_adapters = 1,
	.adapter = {
		{
			.stream = DVB_USB_STREAM_ISOC(0x82, 5, 40, 942, 1),
		},
	},
};

static const struct usb_device_id au6610_id_table[] = {
	{ DVB_USB_DEVICE(USB_VID_ALCOR_MICRO, USB_PID_SIGMATEK_DVB_110,
		&au6610_props, "Sigmatek DVB-110", NULL) },
	{ }
};
MODULE_DEVICE_TABLE(usb, au6610_id_table);

static struct usb_driver au6610_driver = {
	.name = KBUILD_MODNAME,
	.id_table = au6610_id_table,
	.probe = dvb_usbv2_probe,
	.disconnect = dvb_usbv2_disconnect,
	.suspend = dvb_usbv2_suspend,
	.resume = dvb_usbv2_resume,
	.reset_resume = dvb_usbv2_reset_resume,
	.no_dynamic_id = 1,
	.soft_unbind = 1,
};

module_usb_driver(au6610_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Driver for Alcor Micro AU6610 DVB-T USB2.0");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");
