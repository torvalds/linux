/*
 * Qualcomm Serial USB driver
 *
 *	Copyright (c) 2008 QUALCOMM Incorporated.
 *	Copyright (c) 2009 Greg Kroah-Hartman <gregkh@suse.de>
 *	Copyright (c) 2009 Novell Inc.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 *
 */

#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/slab.h>
#include "usb-wwan.h"

#define DRIVER_AUTHOR "Qualcomm Inc"
#define DRIVER_DESC "Qualcomm USB Serial driver"

#define DEVICE_G1K(v, p) \
	USB_DEVICE(v, p), .driver_info = 1

static const struct usb_device_id id_table[] = {
	/* Gobi 1000 devices */
	{DEVICE_G1K(0x05c6, 0x9211)},	/* Acer Gobi QDL device */
	{DEVICE_G1K(0x05c6, 0x9212)},	/* Acer Gobi Modem Device */
	{DEVICE_G1K(0x03f0, 0x1f1d)},	/* HP un2400 Gobi Modem Device */
	{DEVICE_G1K(0x03f0, 0x201d)},	/* HP un2400 Gobi QDL Device */
	{DEVICE_G1K(0x04da, 0x250d)},	/* Panasonic Gobi Modem device */
	{DEVICE_G1K(0x04da, 0x250c)},	/* Panasonic Gobi QDL device */
	{DEVICE_G1K(0x413c, 0x8172)},	/* Dell Gobi Modem device */
	{DEVICE_G1K(0x413c, 0x8171)},	/* Dell Gobi QDL device */
	{DEVICE_G1K(0x1410, 0xa001)},	/* Novatel Gobi Modem device */
	{DEVICE_G1K(0x1410, 0xa008)},	/* Novatel Gobi QDL device */
	{DEVICE_G1K(0x0b05, 0x1776)},	/* Asus Gobi Modem device */
	{DEVICE_G1K(0x0b05, 0x1774)},	/* Asus Gobi QDL device */
	{DEVICE_G1K(0x19d2, 0xfff3)},	/* ONDA Gobi Modem device */
	{DEVICE_G1K(0x19d2, 0xfff2)},	/* ONDA Gobi QDL device */
	{DEVICE_G1K(0x1557, 0x0a80)},	/* OQO Gobi QDL device */
	{DEVICE_G1K(0x05c6, 0x9001)},   /* Generic Gobi Modem device */
	{DEVICE_G1K(0x05c6, 0x9002)},	/* Generic Gobi Modem device */
	{DEVICE_G1K(0x05c6, 0x9202)},	/* Generic Gobi Modem device */
	{DEVICE_G1K(0x05c6, 0x9203)},	/* Generic Gobi Modem device */
	{DEVICE_G1K(0x05c6, 0x9222)},	/* Generic Gobi Modem device */
	{DEVICE_G1K(0x05c6, 0x9008)},	/* Generic Gobi QDL device */
	{DEVICE_G1K(0x05c6, 0x9009)},	/* Generic Gobi Modem device */
	{DEVICE_G1K(0x05c6, 0x9201)},	/* Generic Gobi QDL device */
	{DEVICE_G1K(0x05c6, 0x9221)},	/* Generic Gobi QDL device */
	{DEVICE_G1K(0x05c6, 0x9231)},	/* Generic Gobi QDL device */
	{DEVICE_G1K(0x1f45, 0x0001)},	/* Unknown Gobi QDL device */

	/* Gobi 2000 devices */
	{USB_DEVICE(0x1410, 0xa010)},	/* Novatel Gobi 2000 QDL device */
	{USB_DEVICE(0x1410, 0xa011)},	/* Novatel Gobi 2000 QDL device */
	{USB_DEVICE(0x1410, 0xa012)},	/* Novatel Gobi 2000 QDL device */
	{USB_DEVICE(0x1410, 0xa013)},	/* Novatel Gobi 2000 QDL device */
	{USB_DEVICE(0x1410, 0xa014)},	/* Novatel Gobi 2000 QDL device */
	{USB_DEVICE(0x413c, 0x8185)},	/* Dell Gobi 2000 QDL device (N0218, VU936) */
	{USB_DEVICE(0x413c, 0x8186)},	/* Dell Gobi 2000 Modem device (N0218, VU936) */
	{USB_DEVICE(0x05c6, 0x9208)},	/* Generic Gobi 2000 QDL device */
	{USB_DEVICE(0x05c6, 0x920b)},	/* Generic Gobi 2000 Modem device */
	{USB_DEVICE(0x05c6, 0x9224)},	/* Sony Gobi 2000 QDL device (N0279, VU730) */
	{USB_DEVICE(0x05c6, 0x9225)},	/* Sony Gobi 2000 Modem device (N0279, VU730) */
	{USB_DEVICE(0x05c6, 0x9244)},	/* Samsung Gobi 2000 QDL device (VL176) */
	{USB_DEVICE(0x05c6, 0x9245)},	/* Samsung Gobi 2000 Modem device (VL176) */
	{USB_DEVICE(0x03f0, 0x241d)},	/* HP Gobi 2000 QDL device (VP412) */
	{USB_DEVICE(0x03f0, 0x251d)},	/* HP Gobi 2000 Modem device (VP412) */
	{USB_DEVICE(0x05c6, 0x9214)},	/* Acer Gobi 2000 QDL device (VP413) */
	{USB_DEVICE(0x05c6, 0x9215)},	/* Acer Gobi 2000 Modem device (VP413) */
	{USB_DEVICE(0x05c6, 0x9264)},	/* Asus Gobi 2000 QDL device (VR305) */
	{USB_DEVICE(0x05c6, 0x9265)},	/* Asus Gobi 2000 Modem device (VR305) */
	{USB_DEVICE(0x05c6, 0x9234)},	/* Top Global Gobi 2000 QDL device (VR306) */
	{USB_DEVICE(0x05c6, 0x9235)},	/* Top Global Gobi 2000 Modem device (VR306) */
	{USB_DEVICE(0x05c6, 0x9274)},	/* iRex Technologies Gobi 2000 QDL device (VR307) */
	{USB_DEVICE(0x05c6, 0x9275)},	/* iRex Technologies Gobi 2000 Modem device (VR307) */
	{USB_DEVICE(0x1199, 0x9000)},	/* Sierra Wireless Gobi 2000 QDL device (VT773) */
	{USB_DEVICE(0x1199, 0x9001)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{USB_DEVICE(0x1199, 0x9002)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{USB_DEVICE(0x1199, 0x9003)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{USB_DEVICE(0x1199, 0x9004)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{USB_DEVICE(0x1199, 0x9005)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{USB_DEVICE(0x1199, 0x9006)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{USB_DEVICE(0x1199, 0x9007)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{USB_DEVICE(0x1199, 0x9008)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{USB_DEVICE(0x1199, 0x9009)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{USB_DEVICE(0x1199, 0x900a)},	/* Sierra Wireless Gobi 2000 Modem device (VT773) */
	{USB_DEVICE(0x1199, 0x9011)},   /* Sierra Wireless Gobi 2000 Modem device (MC8305) */
	{USB_DEVICE(0x16d8, 0x8001)},	/* CMDTech Gobi 2000 QDL device (VU922) */
	{USB_DEVICE(0x16d8, 0x8002)},	/* CMDTech Gobi 2000 Modem device (VU922) */
	{USB_DEVICE(0x05c6, 0x9204)},	/* Gobi 2000 QDL device */
	{USB_DEVICE(0x05c6, 0x9205)},	/* Gobi 2000 Modem device */

	/* Gobi 3000 devices */
	{USB_DEVICE(0x03f0, 0x371d)},	/* HP un2430 Gobi 3000 QDL */
	{USB_DEVICE(0x05c6, 0x920c)},	/* Gobi 3000 QDL */
	{USB_DEVICE(0x05c6, 0x920d)},	/* Gobi 3000 Composite */
	{USB_DEVICE(0x1410, 0xa020)},   /* Novatel Gobi 3000 QDL */
	{USB_DEVICE(0x1410, 0xa021)},	/* Novatel Gobi 3000 Composite */
	{USB_DEVICE(0x413c, 0x8193)},	/* Dell Gobi 3000 QDL */
	{USB_DEVICE(0x413c, 0x8194)},	/* Dell Gobi 3000 Composite */
	{USB_DEVICE(0x1199, 0x68a4)},	/* Sierra Wireless QDL */
	{USB_DEVICE(0x1199, 0x68a5)},	/* Sierra Wireless Modem */
	{USB_DEVICE(0x1199, 0x68a8)},	/* Sierra Wireless QDL */
	{USB_DEVICE(0x1199, 0x68a9)},	/* Sierra Wireless Modem */
	{USB_DEVICE(0x1199, 0x9010)},	/* Sierra Wireless Gobi 3000 QDL */
	{USB_DEVICE(0x1199, 0x9012)},	/* Sierra Wireless Gobi 3000 QDL */
	{USB_DEVICE(0x1199, 0x9013)},	/* Sierra Wireless Gobi 3000 Modem device (MC8355) */
	{USB_DEVICE(0x1199, 0x9014)},	/* Sierra Wireless Gobi 3000 QDL */
	{USB_DEVICE(0x1199, 0x9015)},	/* Sierra Wireless Gobi 3000 Modem device */
	{USB_DEVICE(0x1199, 0x9018)},	/* Sierra Wireless Gobi 3000 QDL */
	{USB_DEVICE(0x1199, 0x9019)},	/* Sierra Wireless Gobi 3000 Modem device */
	{USB_DEVICE(0x1199, 0x901b)},	/* Sierra Wireless MC7770 */
	{USB_DEVICE(0x12D1, 0x14F0)},	/* Sony Gobi 3000 QDL */
	{USB_DEVICE(0x12D1, 0x14F1)},	/* Sony Gobi 3000 Composite */

	/* non Gobi Qualcomm serial devices */
	{USB_DEVICE_INTERFACE_NUMBER(0x0f3d, 0x68a2, 0)},	/* Sierra Wireless MC7700 Device Management */
	{USB_DEVICE_INTERFACE_NUMBER(0x0f3d, 0x68a2, 2)},	/* Sierra Wireless MC7700 NMEA */
	{USB_DEVICE_INTERFACE_NUMBER(0x0f3d, 0x68a2, 3)},	/* Sierra Wireless MC7700 Modem */
	{USB_DEVICE_INTERFACE_NUMBER(0x114f, 0x68a2, 0)},	/* Sierra Wireless MC7750 Device Management */
	{USB_DEVICE_INTERFACE_NUMBER(0x114f, 0x68a2, 2)},	/* Sierra Wireless MC7750 NMEA */
	{USB_DEVICE_INTERFACE_NUMBER(0x114f, 0x68a2, 3)},	/* Sierra Wireless MC7750 Modem */
	{USB_DEVICE_INTERFACE_NUMBER(0x1199, 0x68a2, 0)},	/* Sierra Wireless MC7710 Device Management */
	{USB_DEVICE_INTERFACE_NUMBER(0x1199, 0x68a2, 2)},	/* Sierra Wireless MC7710 NMEA */
	{USB_DEVICE_INTERFACE_NUMBER(0x1199, 0x68a2, 3)},	/* Sierra Wireless MC7710 Modem */
	{USB_DEVICE_INTERFACE_NUMBER(0x1199, 0x901c, 0)},	/* Sierra Wireless EM7700 Device Management */
	{USB_DEVICE_INTERFACE_NUMBER(0x1199, 0x901c, 2)},	/* Sierra Wireless EM7700 NMEA */
	{USB_DEVICE_INTERFACE_NUMBER(0x1199, 0x901c, 3)},	/* Sierra Wireless EM7700 Modem */

	{ }				/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);

static int qcprobe(struct usb_serial *serial, const struct usb_device_id *id)
{
	struct usb_host_interface *intf = serial->interface->cur_altsetting;
	struct device *dev = &serial->dev->dev;
	int retval = -ENODEV;
	__u8 nintf;
	__u8 ifnum;
	bool is_gobi1k = id->driver_info ? true : false;
	int altsetting = -1;

	dev_dbg(dev, "Is Gobi 1000 = %d\n", is_gobi1k);

	nintf = serial->dev->actconfig->desc.bNumInterfaces;
	dev_dbg(dev, "Num Interfaces = %d\n", nintf);
	ifnum = intf->desc.bInterfaceNumber;
	dev_dbg(dev, "This Interface = %d\n", ifnum);

	if (nintf == 1) {
		/* QDL mode */
		/* Gobi 2000 has a single altsetting, older ones have two */
		if (serial->interface->num_altsetting == 2)
			intf = &serial->interface->altsetting[1];
		else if (serial->interface->num_altsetting > 2)
			goto done;

		if (intf->desc.bNumEndpoints == 2 &&
		    usb_endpoint_is_bulk_in(&intf->endpoint[0].desc) &&
		    usb_endpoint_is_bulk_out(&intf->endpoint[1].desc)) {
			dev_dbg(dev, "QDL port found\n");

			if (serial->interface->num_altsetting == 1)
				retval = 0; /* Success */
			else
				altsetting = 1;
		}
		goto done;

	}

	/* allow any number of interfaces when doing direct interface match */
	if (id->match_flags & USB_DEVICE_ID_MATCH_INT_NUMBER) {
		dev_dbg(dev, "Generic Qualcomm serial interface found\n");
		altsetting = 0;
		goto done;
	}

	if (nintf < 3 || nintf > 4) {
		dev_err(dev, "unknown number of interfaces: %d\n", nintf);
		goto done;
	}

	/* default to enabling interface */
	altsetting = 0;

	/* Composite mode; don't bind to the QMI/net interface as that
	 * gets handled by other drivers.
	 */

	if (is_gobi1k) {
		/* Gobi 1K USB layout:
		 * 0: serial port (doesn't respond)
		 * 1: serial port (doesn't respond)
		 * 2: AT-capable modem port
		 * 3: QMI/net
		 */
		if (ifnum == 2)
			dev_dbg(dev, "Modem port found\n");
		else
			altsetting = -1;
	} else {
		/* Gobi 2K+ USB layout:
		 * 0: QMI/net
		 * 1: DM/DIAG (use libqcdm from ModemManager for communication)
		 * 2: AT-capable modem port
		 * 3: NMEA
		 */
		switch (ifnum) {
		case 0:
			/* Don't claim the QMI/net interface */
			altsetting = -1;
			break;
		case 1:
			dev_dbg(dev, "Gobi 2K+ DM/DIAG interface found\n");
			break;
		case 2:
			dev_dbg(dev, "Modem port found\n");
			break;
		case 3:
			/*
			 * NMEA (serial line 9600 8N1)
			 * # echo "\$GPS_START" > /dev/ttyUSBx
			 * # echo "\$GPS_STOP"  > /dev/ttyUSBx
			 */
			dev_dbg(dev, "Gobi 2K+ NMEA GPS interface found\n");
			break;
		}
	}

done:
	if (altsetting >= 0) {
		retval = usb_set_interface(serial->dev, ifnum, altsetting);
		if (retval < 0) {
			dev_err(dev,
				"Could not set interface, error %d\n",
				retval);
			retval = -ENODEV;
		}
	}

	return retval;
}

static int qc_attach(struct usb_serial *serial)
{
	struct usb_wwan_intf_private *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	spin_lock_init(&data->susp_lock);

	usb_set_serial_data(serial, data);

	return 0;
}

static void qc_release(struct usb_serial *serial)
{
	struct usb_wwan_intf_private *priv = usb_get_serial_data(serial);

	usb_set_serial_data(serial, NULL);
	kfree(priv);
}

static struct usb_serial_driver qcdevice = {
	.driver = {
		.owner     = THIS_MODULE,
		.name      = "qcserial",
	},
	.description         = "Qualcomm USB modem",
	.id_table            = id_table,
	.num_ports           = 1,
	.probe               = qcprobe,
	.open		     = usb_wwan_open,
	.close		     = usb_wwan_close,
	.write		     = usb_wwan_write,
	.write_room	     = usb_wwan_write_room,
	.chars_in_buffer     = usb_wwan_chars_in_buffer,
	.attach              = qc_attach,
	.release	     = qc_release,
	.port_probe          = usb_wwan_port_probe,
	.port_remove	     = usb_wwan_port_remove,
#ifdef CONFIG_PM
	.suspend	     = usb_wwan_suspend,
	.resume		     = usb_wwan_resume,
#endif
};

static struct usb_serial_driver * const serial_drivers[] = {
	&qcdevice, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL v2");
