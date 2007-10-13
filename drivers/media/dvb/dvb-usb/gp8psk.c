/* DVB USB compliant Linux driver for the
 *  - GENPIX 8pks/qpsk/DCII USB2.0 DVB-S module
 *
 * Copyright (C) 2006,2007 Alan Nisota (alannisota@gmail.com)
 * Copyright (C) 2006,2007 Genpix Electronics (genpix@genpix-electronics.com)
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
		warn("usb in %d operation failed.", req);
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

	ret = -EINVAL;

	if (gp8psk_usb_out_op(d, LOAD_BCM4500,1,0,NULL, 0))
		goto out_rel_fw;

	info("downloading bcm4500 firmware from file '%s'",bcm4500_firmware);

	ptr = fw->data;
	buf = kmalloc(64, GFP_KERNEL | GFP_DMA);

	while (ptr[0] != 0xff) {
		u16 buflen = ptr[0] + 4;
		if (ptr + buflen >= fw->data + fw->size) {
			err("failed to load bcm4500 firmware.");
			goto out_free;
		}
		memcpy(buf, ptr, buflen);
		if (dvb_usb_generic_write(d, buf, buflen)) {
			err("failed to load bcm4500 firmware.");
			goto out_free;
		}
		ptr += buflen;
	}

	ret = 0;

out_free:
	kfree(buf);
out_rel_fw:
	release_firmware(fw);

	return ret;
}

static int gp8psk_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	u8 status, buf;
	int gp_product_id = le16_to_cpu(d->udev->descriptor.idProduct);

	if (onoff) {
		gp8psk_usb_in_op(d, GET_8PSK_CONFIG,0,0,&status,1);
		if (! (status & bm8pskStarted)) {  /* started */
			if(gp_product_id == USB_PID_GENPIX_SKYWALKER_CW3K)
				gp8psk_usb_out_op(d, CW3K_INIT, 1, 0, NULL, 0);
			if (gp8psk_usb_in_op(d, BOOT_8PSK, 1, 0, &buf, 1))
				return -EINVAL;
		}

		if (gp_product_id == USB_PID_GENPIX_8PSK_REV_1_WARM)
			if (! (status & bm8pskFW_Loaded)) /* BCM4500 firmware loaded */
				if(gp8psk_load_bcm4500fw(d))
					return EINVAL;

		if (! (status & bmIntersilOn)) /* LNB Power */
			if (gp8psk_usb_in_op(d, START_INTERSIL, 1, 0,
					&buf, 1))
				return EINVAL;

		/* Set DVB mode to 1 */
		if (gp_product_id == USB_PID_GENPIX_8PSK_REV_1_WARM)
			if (gp8psk_usb_out_op(d, SET_DVB_MODE, 1, 0, NULL, 0))
				return EINVAL;
		/* Abort possible TS (if previous tune crashed) */
		if (gp8psk_usb_out_op(d, ARM_TRANSFER, 0, 0, NULL, 0))
			return EINVAL;
	} else {
		/* Turn off LNB power */
		if (gp8psk_usb_in_op(d, START_INTERSIL, 0, 0, &buf, 1))
			return EINVAL;
		/* Turn off 8psk power */
		if (gp8psk_usb_in_op(d, BOOT_8PSK, 0, 0, &buf, 1))
			return -EINVAL;
		if(gp_product_id == USB_PID_GENPIX_SKYWALKER_CW3K)
			gp8psk_usb_out_op(d, CW3K_INIT, 0, 0, NULL, 0);
	}
	return 0;
}

int gp8psk_bcm4500_reload(struct dvb_usb_device *d)
{
	u8 buf;
	int gp_product_id = le16_to_cpu(d->udev->descriptor.idProduct);
	/* Turn off 8psk power */
	if (gp8psk_usb_in_op(d, BOOT_8PSK, 0, 0, &buf, 1))
		return -EINVAL;
	/* Turn On 8psk power */
	if (gp8psk_usb_in_op(d, BOOT_8PSK, 1, 0, &buf, 1))
		return -EINVAL;
	/* load BCM4500 firmware */
	if (gp_product_id == USB_PID_GENPIX_8PSK_REV_1_WARM)
		if (gp8psk_load_bcm4500fw(d))
			return EINVAL;
	return 0;
}

static int gp8psk_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	return gp8psk_usb_out_op(adap->dev, ARM_TRANSFER, onoff, 0 , NULL, 0);
}

static int gp8psk_frontend_attach(struct dvb_usb_adapter *adap)
{
	adap->fe = gp8psk_fe_attach(adap->dev);
	return 0;
}

static struct dvb_usb_device_properties gp8psk_properties;

static int gp8psk_usb_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	int ret;
	struct usb_device *udev = interface_to_usbdev(intf);
	ret =  dvb_usb_device_init(intf,&gp8psk_properties,THIS_MODULE,NULL);
	if (ret == 0) {
		info("found Genpix USB device pID = %x (hex)",
			le16_to_cpu(udev->descriptor.idProduct));
	}
	return ret;
}

static struct usb_device_id gp8psk_usb_table [] = {
	    { USB_DEVICE(USB_VID_GENPIX, USB_PID_GENPIX_8PSK_REV_1_COLD) },
	    { USB_DEVICE(USB_VID_GENPIX, USB_PID_GENPIX_8PSK_REV_1_WARM) },
	    { USB_DEVICE(USB_VID_GENPIX, USB_PID_GENPIX_8PSK_REV_2) },
	    { USB_DEVICE(USB_VID_GENPIX, USB_PID_GENPIX_SKYWALKER_1) },
	    { USB_DEVICE(USB_VID_GENPIX, USB_PID_GENPIX_SKYWALKER_CW3K) },
	    { 0 },
};
MODULE_DEVICE_TABLE(usb, gp8psk_usb_table);

static struct dvb_usb_device_properties gp8psk_properties = {
	.usb_ctrl = CYPRESS_FX2,
	.firmware = "dvb-usb-gp8psk-01.fw",

	.num_adapters = 1,
	.adapter = {
		{
			.streaming_ctrl   = gp8psk_streaming_ctrl,
			.frontend_attach  = gp8psk_frontend_attach,
			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 7,
				.endpoint = 0x82,
				.u = {
					.bulk = {
						.buffersize = 8192,
					}
				}
			},
		}
	},
	.power_ctrl       = gp8psk_power_ctrl,

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 4,
	.devices = {
		{ .name = "Genpix 8PSK-to-USB2 Rev.1 DVB-S receiver",
		  .cold_ids = { &gp8psk_usb_table[0], NULL },
		  .warm_ids = { &gp8psk_usb_table[1], NULL },
		},
		{ .name = "Genpix 8PSK-to-USB2 Rev.2 DVB-S receiver",
		  .cold_ids = { NULL },
		  .warm_ids = { &gp8psk_usb_table[2], NULL },
		},
		{ .name = "Genpix SkyWalker-1 DVB-S receiver",
		  .cold_ids = { NULL },
		  .warm_ids = { &gp8psk_usb_table[3], NULL },
		},
		{ .name = "Genpix SkyWalker-CW3K DVB-S receiver",
		  .cold_ids = { NULL },
		  .warm_ids = { &gp8psk_usb_table[4], NULL },
		},
		{ NULL },
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
MODULE_DESCRIPTION("Driver for Genpix 8psk-to-USB2 DVB-S");
MODULE_VERSION("1.1");
MODULE_LICENSE("GPL");
