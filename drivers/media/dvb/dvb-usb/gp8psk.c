/* DVB USB compliant Linux driver for the
 *  - GENPIX 8pks/qpsk USB2.0 DVB-S module
 *
 * Copyright (C) 2006 Alan Nisota (alannisota@gmail.com)
 *
 * Thanks to GENPIX for the sample code used to implement this module.
 *
 * This module is based off the vp7045 and vp702x modules
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#include "gp8psk.h"

/* debug */
static char bcm4500_firmware[] = "dvb-usb-gp8psk-02.fw";
int dvb_usb_gp8psk_debug;
module_param_named(debug,dvb_usb_gp8psk_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,xfer=2,rc=4 (or-able))." DVB_USB_DEBUG_STATUS);

int gp8psk_usb_in_op(struct dvb_usb_device *d, u8 req, u16 value, u16 index, u8 *b, int blen)
{
	int ret = 0,try = 0;

	if ((ret = mutex_lock_interruptible(&d->usb_mutex)))
		return ret;

	while (ret >= 0 && ret != blen && try < 3) {
		ret = usb_control_msg(d->udev,
			usb_rcvctrlpipe(d->udev,0),
			req,
			USB_TYPE_VENDOR | USB_DIR_IN,
			value,index,b,blen,
			2000);
		deb_info("reading number %d (ret: %d)\n",try,ret);
		try++;
	}

	if (ret < 0 || ret != blen) {
		warn("usb in operation failed.");
		ret = -EIO;
	} else
		ret = 0;

	deb_xfer("in: req. %x, val: %x, ind: %x, buffer: ",req,value,index);
	debug_dump(b,blen,deb_xfer);

	mutex_unlock(&d->usb_mutex);

	return ret;
}

int gp8psk_usb_out_op(struct dvb_usb_device *d, u8 req, u16 value,
			     u16 index, u8 *b, int blen)
{
	int ret;

	deb_xfer("out: req. %x, val: %x, ind: %x, buffer: ",req,value,index);
	debug_dump(b,blen,deb_xfer);

	if ((ret = mutex_lock_interruptible(&d->usb_mutex)))
		return ret;

	if (usb_control_msg(d->udev,
			usb_sndctrlpipe(d->udev,0),
			req,
			USB_TYPE_VENDOR | USB_DIR_OUT,
			value,index,b,blen,
			2000) != blen) {
		warn("usb out operation failed.");
		ret = -EIO;
	} else
		ret = 0;
	mutex_unlock(&d->usb_mutex);

	return ret;
}

static int gp8psk_load_bcm4500fw(struct dvb_usb_device *d)
{
	int ret;
	const struct firmware *fw = NULL;
	u8 *ptr, *buf;
	if ((ret = request_firmware(&fw, bcm4500_firmware,
					&d->udev->dev)) != 0) {
		err("did not find the bcm4500 firmware file. (%s) "
			"Please see linux/Documentation/dvb/ for more details on firmware-problems. (%d)",
			bcm4500_firmware,ret);
		return ret;
	}

	if (gp8psk_usb_out_op(d, LOAD_BCM4500,1,0,NULL, 0)) {
		release_firmware(fw);
		return -EINVAL;
	}

	info("downloaidng bcm4500 firmware from file '%s'",bcm4500_firmware);

	ptr = fw->data;
	buf = (u8 *) kmalloc(512, GFP_KERNEL | GFP_DMA);

	while (ptr[0] != 0xff) {
		u16 buflen = ptr[0] + 4;
		if (ptr + buflen >= fw->data + fw->size) {
			err("failed to load bcm4500 firmware.");
			release_firmware(fw);
			kfree(buf);
			return -EINVAL;
		}
		memcpy(buf, ptr, buflen);
		if (dvb_usb_generic_write(d, buf, buflen)) {
			err("failed to load bcm4500 firmware.");
			release_firmware(fw);
			kfree(buf);
			return -EIO;
		}
		ptr += buflen;
	}
	release_firmware(fw);
	kfree(buf);
	return 0;
}

static int gp8psk_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	u8 status, buf;
	if (onoff) {
		gp8psk_usb_in_op(d, GET_8PSK_CONFIG,0,0,&status,1);
		if (! (status & 0x01))  /* started */
			if (gp8psk_usb_in_op(d, BOOT_8PSK, 1, 0, &buf, 1))
				return -EINVAL;

		if (! (status & 0x02)) /* BCM4500 firmware loaded */
			if(gp8psk_load_bcm4500fw(d))
				return EINVAL;

		if (! (status & 0x04)) /* LNB Power */
			if (gp8psk_usb_in_op(d, START_INTERSIL, 1, 0,
					&buf, 1))
				return EINVAL;

		/* Set DVB mode */
		if(gp8psk_usb_out_op(d, SET_DVB_MODE, 1, 0, NULL, 0))
			return -EINVAL;
		gp8psk_usb_in_op(d, GET_8PSK_CONFIG,0,0,&status,1);
	} else {
		/* Turn off LNB power */
		if (gp8psk_usb_in_op(d, START_INTERSIL, 0, 0, &buf, 1))
			return EINVAL;
		/* Turn off 8psk power */
		if (gp8psk_usb_in_op(d, BOOT_8PSK, 0, 0, &buf, 1))
			return -EINVAL;

	}
	return 0;
}


static int gp8psk_streaming_ctrl(struct dvb_usb_device *d, int onoff)
{
	return gp8psk_usb_out_op(d, ARM_TRANSFER, onoff, 0 , NULL, 0);
}

static int gp8psk_frontend_attach(struct dvb_usb_device *d)
{
	d->fe = gp8psk_fe_attach(d);

	return 0;
}

static struct dvb_usb_properties gp8psk_properties;

static int gp8psk_usb_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	return dvb_usb_device_init(intf,&gp8psk_properties,THIS_MODULE,NULL);
}

static struct usb_device_id gp8psk_usb_table [] = {
	    { USB_DEVICE(USB_VID_GENPIX, USB_PID_GENPIX_8PSK_COLD) },
	    { USB_DEVICE(USB_VID_GENPIX, USB_PID_GENPIX_8PSK_WARM) },
	    { 0 },
};
MODULE_DEVICE_TABLE(usb, gp8psk_usb_table);

static struct dvb_usb_properties gp8psk_properties = {
	.caps = 0,

	.usb_ctrl = CYPRESS_FX2,
	.firmware = "dvb-usb-gp8psk-01.fw",

	.streaming_ctrl   = gp8psk_streaming_ctrl,
	.power_ctrl       = gp8psk_power_ctrl,
	.frontend_attach  = gp8psk_frontend_attach,

	.generic_bulk_ctrl_endpoint = 0x01,
	/* parameter for the MPEG2-data transfer */
	.urb = {
		.type = DVB_USB_BULK,
		.count = 7,
		.endpoint = 0x82,
		.u = {
			.bulk = {
				.buffersize = 8192,
			}
		}
	},

	.num_device_descs = 1,
	.devices = {
		{ .name = "Genpix 8PSK-USB DVB-S USB2.0 receiver",
		  .cold_ids = { &gp8psk_usb_table[0], NULL },
		  .warm_ids = { &gp8psk_usb_table[1], NULL },
		},
		{ 0 },
	}
};

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver gp8psk_usb_driver = {
	.name		= "dvb_usb_gp8psk",
	.probe		= gp8psk_usb_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table	= gp8psk_usb_table,
};

/* module stuff */
static int __init gp8psk_usb_module_init(void)
{
	int result;
	if ((result = usb_register(&gp8psk_usb_driver))) {
		err("usb_register failed. (%d)",result);
		return result;
	}

	return 0;
}

static void __exit gp8psk_usb_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&gp8psk_usb_driver);
}

module_init(gp8psk_usb_module_init);
module_exit(gp8psk_usb_module_exit);

MODULE_AUTHOR("Alan Nisota <alannisota@gamil.com>");
MODULE_DESCRIPTION("Driver for Genpix 8psk-USB DVB-S USB2.0");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
