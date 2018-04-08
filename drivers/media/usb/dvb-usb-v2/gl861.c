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
		dev_err(&d->udev->dev, "%s: wlen=%d, aborting\n",
				KBUILD_MODNAME, wlen);
		return -EINVAL;
	}

	usleep_range(1000, 2000); /* avoid I2C errors */

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

	adap->fe[0] = dvb_attach(zl10353_attach, &gl861_zl10353_config,
		&adap_to_d(adap)->i2c_adap);
	if (adap->fe[0] == NULL)
		return -EIO;

	return 0;
}

static struct qt1010_config gl861_qt1010_config = {
	.i2c_address = 0x62
};

static int gl861_tuner_attach(struct dvb_usb_adapter *adap)
{
	return dvb_attach(qt1010_attach,
			  adap->fe[0], &adap_to_d(adap)->i2c_adap,
			  &gl861_qt1010_config) == NULL ? -ENODEV : 0;
}

static int gl861_init(struct dvb_usb_device *d)
{
	/*
	 * There is 2 interfaces. Interface 0 is for TV and interface 1 is
	 * for HID remote controller. Interface 0 has 2 alternate settings.
	 * For some reason we need to set interface explicitly, defaulted
	 * as alternate setting 1?
	 */
	return usb_set_interface(d->udev, 0, 0);
}

/* DVB USB Driver stuff */
static struct dvb_usb_device_properties gl861_props = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,

	.i2c_algo = &gl861_i2c_algo,
	.frontend_attach = gl861_frontend_attach,
	.tuner_attach = gl861_tuner_attach,
	.init = gl861_init,

	.num_adapters = 1,
	.adapter = {
		{
			.stream = DVB_USB_STREAM_BULK(0x81, 7, 512),
		}
	}
};

static const struct usb_device_id gl861_id_table[] = {
	{ DVB_USB_DEVICE(USB_VID_MSI, USB_PID_MSI_MEGASKY580_55801,
		&gl861_props, "MSI Mega Sky 55801 DVB-T USB2.0", NULL) },
	{ DVB_USB_DEVICE(USB_VID_ALINK, USB_VID_ALINK_DTU,
		&gl861_props, "A-LINK DTU DVB-T USB2.0", NULL) },
	{ }
};
MODULE_DEVICE_TABLE(usb, gl861_id_table);

static struct usb_driver gl861_usb_driver = {
	.name = KBUILD_MODNAME,
	.id_table = gl861_id_table,
	.probe = dvb_usbv2_probe,
	.disconnect = dvb_usbv2_disconnect,
	.suspend = dvb_usbv2_suspend,
	.resume = dvb_usbv2_resume,
	.reset_resume = dvb_usbv2_reset_resume,
	.no_dynamic_id = 1,
	.soft_unbind = 1,
};

module_usb_driver(gl861_usb_driver);

MODULE_AUTHOR("Carl Lundqvist <comabug@gmail.com>");
MODULE_DESCRIPTION("Driver MSI Mega Sky 580 DVB-T USB2.0 / GL861");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");
