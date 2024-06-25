// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm Serial USB driver
 *
 *	Copyright (c) 2008 QUALCOMM Incorporated.
 *	Copyright (c) 2009 Greg Kroah-Hartman <gregkh@suse.de>
 *	Copyright (c) 2009 Novell Inc.
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

#define QUECTEL_EC20_PID	0x9215

/* standard device layouts supported by this driver */
enum qcserial_layouts {
	QCSERIAL_G2K = 0,	/* Gobi 2000 */
	QCSERIAL_G1K = 1,	/* Gobi 1000 */
	QCSERIAL_SWI = 2,	/* Sierra Wireless */
	QCSERIAL_HWI = 3,	/* Huawei */
};

#define DEVICE_G1K(v, p) \
	USB_DEVICE(v, p), .driver_info = QCSERIAL_G1K
#define DEVICE_SWI(v, p) \
	USB_DEVICE(v, p), .driver_info = QCSERIAL_SWI
#define DEVICE_HWI(v, p) \
	USB_DEVICE(v, p), .driver_info = QCSERIAL_HWI

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
	{DEVICE_G1K(0x1410, 0xa001)},	/* Novatel/Verizon USB-1000 */
	{DEVICE_G1K(0x1410, 0xa002)},	/* Novatel Gobi Modem device */
	{DEVICE_G1K(0x1410, 0xa003)},	/* Novatel Gobi Modem device */
	{DEVICE_G1K(0x1410, 0xa004)},	/* Novatel Gobi Modem device */
	{DEVICE_G1K(0x1410, 0xa005)},	/* Novatel Gobi Modem device */
	{DEVICE_G1K(0x1410, 0xa006)},	/* Novatel Gobi Modem device */
	{DEVICE_G1K(0x1410, 0xa007)},	/* Novatel Gobi Modem device */
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
	{DEVICE_G1K(0x1bc7, 0x900e)},	/* Telit Gobi QDL device */

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
	{USB_DEVICE(0x413c, 0x81a6)},	/* Dell DW5570 QDL (MC8805) */
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
	{USB_DEVICE(0x0AF0, 0x8120)},	/* Option GTM681W */

	/* non-Gobi Sierra Wireless devices */
	{DEVICE_SWI(0x03f0, 0x4e1d)},	/* HP lt4111 LTE/EV-DO/HSPA+ Gobi 4G Module */
	{DEVICE_SWI(0x0f3d, 0x68a2)},	/* Sierra Wireless MC7700 */
	{DEVICE_SWI(0x114f, 0x68a2)},	/* Sierra Wireless MC7750 */
	{DEVICE_SWI(0x1199, 0x68a2)},	/* Sierra Wireless MC7710 */
	{DEVICE_SWI(0x1199, 0x68c0)},	/* Sierra Wireless MC7304/MC7354 */
	{DEVICE_SWI(0x1199, 0x901c)},	/* Sierra Wireless EM7700 */
	{DEVICE_SWI(0x1199, 0x901e)},	/* Sierra Wireless EM7355 QDL */
	{DEVICE_SWI(0x1199, 0x901f)},	/* Sierra Wireless EM7355 */
	{DEVICE_SWI(0x1199, 0x9040)},	/* Sierra Wireless Modem */
	{DEVICE_SWI(0x1199, 0x9041)},	/* Sierra Wireless MC7305/MC7355 */
	{DEVICE_SWI(0x1199, 0x9051)},	/* Netgear AirCard 340U */
	{DEVICE_SWI(0x1199, 0x9053)},	/* Sierra Wireless Modem */
	{DEVICE_SWI(0x1199, 0x9054)},	/* Sierra Wireless Modem */
	{DEVICE_SWI(0x1199, 0x9055)},	/* Netgear AirCard 341U */
	{DEVICE_SWI(0x1199, 0x9056)},	/* Sierra Wireless Modem */
	{DEVICE_SWI(0x1199, 0x9060)},	/* Sierra Wireless Modem */
	{DEVICE_SWI(0x1199, 0x9061)},	/* Sierra Wireless Modem */
	{DEVICE_SWI(0x1199, 0x9062)},	/* Sierra Wireless EM7305 QDL */
	{DEVICE_SWI(0x1199, 0x9063)},	/* Sierra Wireless EM7305 */
	{DEVICE_SWI(0x1199, 0x9070)},	/* Sierra Wireless MC74xx */
	{DEVICE_SWI(0x1199, 0x9071)},	/* Sierra Wireless MC74xx */
	{DEVICE_SWI(0x1199, 0x9078)},	/* Sierra Wireless EM74xx */
	{DEVICE_SWI(0x1199, 0x9079)},	/* Sierra Wireless EM74xx */
	{DEVICE_SWI(0x1199, 0x907a)},	/* Sierra Wireless EM74xx QDL */
	{DEVICE_SWI(0x1199, 0x907b)},	/* Sierra Wireless EM74xx */
	{DEVICE_SWI(0x1199, 0x9090)},	/* Sierra Wireless EM7565 QDL */
	{DEVICE_SWI(0x1199, 0x9091)},	/* Sierra Wireless EM7565 */
	{DEVICE_SWI(0x1199, 0x90d2)},	/* Sierra Wireless EM9191 QDL */
	{DEVICE_SWI(0x1199, 0xc080)},	/* Sierra Wireless EM7590 QDL */
	{DEVICE_SWI(0x1199, 0xc081)},	/* Sierra Wireless EM7590 */
	{DEVICE_SWI(0x413c, 0x81a2)},	/* Dell Wireless 5806 Gobi(TM) 4G LTE Mobile Broadband Card */
	{DEVICE_SWI(0x413c, 0x81a3)},	/* Dell Wireless 5570 HSPA+ (42Mbps) Mobile Broadband Card */
	{DEVICE_SWI(0x413c, 0x81a4)},	/* Dell Wireless 5570e HSPA+ (42Mbps) Mobile Broadband Card */
	{DEVICE_SWI(0x413c, 0x81a8)},	/* Dell Wireless 5808 Gobi(TM) 4G LTE Mobile Broadband Card */
	{DEVICE_SWI(0x413c, 0x81a9)},	/* Dell Wireless 5808e Gobi(TM) 4G LTE Mobile Broadband Card */
	{DEVICE_SWI(0x413c, 0x81b1)},	/* Dell Wireless 5809e Gobi(TM) 4G LTE Mobile Broadband Card */
	{DEVICE_SWI(0x413c, 0x81b3)},	/* Dell Wireless 5809e Gobi(TM) 4G LTE Mobile Broadband Card (rev3) */
	{DEVICE_SWI(0x413c, 0x81b5)},	/* Dell Wireless 5811e QDL */
	{DEVICE_SWI(0x413c, 0x81b6)},	/* Dell Wireless 5811e QDL */
	{DEVICE_SWI(0x413c, 0x81c2)},	/* Dell Wireless 5811e */
	{DEVICE_SWI(0x413c, 0x81cb)},	/* Dell Wireless 5816e QDL */
	{DEVICE_SWI(0x413c, 0x81cc)},	/* Dell Wireless 5816e */
	{DEVICE_SWI(0x413c, 0x81cf)},   /* Dell Wireless 5819 */
	{DEVICE_SWI(0x413c, 0x81d0)},   /* Dell Wireless 5819 */
	{DEVICE_SWI(0x413c, 0x81d1)},   /* Dell Wireless 5818 */
	{DEVICE_SWI(0x413c, 0x81d2)},   /* Dell Wireless 5818 */
	{DEVICE_SWI(0x413c, 0x8217)},	/* Dell Wireless DW5826e */
	{DEVICE_SWI(0x413c, 0x8218)},	/* Dell Wireless DW5826e QDL */

	/* Huawei devices */
	{DEVICE_HWI(0x03f0, 0x581d)},	/* HP lt4112 LTE/HSPA+ Gobi 4G Modem (Huawei me906e) */

	{ }				/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);

static int handle_quectel_ec20(struct device *dev, int ifnum)
{
	int altsetting = 0;

	/*
	 * Quectel EC20 Mini PCIe LTE module layout:
	 * 0: DM/DIAG (use libqcdm from ModemManager for communication)
	 * 1: NMEA
	 * 2: AT-capable modem port
	 * 3: Modem interface
	 * 4: NDIS
	 */
	switch (ifnum) {
	case 0:
		dev_dbg(dev, "Quectel EC20 DM/DIAG interface found\n");
		break;
	case 1:
		dev_dbg(dev, "Quectel EC20 NMEA GPS interface found\n");
		break;
	case 2:
	case 3:
		dev_dbg(dev, "Quectel EC20 Modem port found\n");
		break;
	case 4:
		/* Don't claim the QMI/net interface */
		altsetting = -1;
		break;
	}

	return altsetting;
}

static int qcprobe(struct usb_serial *serial, const struct usb_device_id *id)
{
	struct usb_host_interface *intf = serial->interface->cur_altsetting;
	struct device *dev = &serial->dev->dev;
	int retval = -ENODEV;
	__u8 nintf;
	__u8 ifnum;
	int altsetting = -1;
	bool sendsetup = false;

	/* we only support vendor specific functions */
	if (intf->desc.bInterfaceClass != USB_CLASS_VENDOR_SPEC)
		goto done;

	nintf = serial->dev->actconfig->desc.bNumInterfaces;
	dev_dbg(dev, "Num Interfaces = %d\n", nintf);
	ifnum = intf->desc.bInterfaceNumber;
	dev_dbg(dev, "This Interface = %d\n", ifnum);

	if (nintf == 1) {
		/* QDL mode */
		/* Gobi 2000 has a single altsetting, older ones have two */
		if (serial->interface->num_altsetting == 2)
			intf = usb_altnum_to_altsetting(serial->interface, 1);
		else if (serial->interface->num_altsetting > 2)
			goto done;

		if (intf && intf->desc.bNumEndpoints == 2 &&
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

	/* default to enabling interface */
	altsetting = 0;

	/*
	 * Composite mode; don't bind to the QMI/net interface as that
	 * gets handled by other drivers.
	 */

	switch (id->driver_info) {
	case QCSERIAL_G1K:
		/*
		 * Gobi 1K USB layout:
		 * 0: DM/DIAG (use libqcdm from ModemManager for communication)
		 * 1: serial port (doesn't respond)
		 * 2: AT-capable modem port
		 * 3: QMI/net
		 */
		if (nintf < 3 || nintf > 4) {
			dev_err(dev, "unknown number of interfaces: %d\n", nintf);
			altsetting = -1;
			goto done;
		}

		if (ifnum == 0) {
			dev_dbg(dev, "Gobi 1K DM/DIAG interface found\n");
			altsetting = 1;
		} else if (ifnum == 2)
			dev_dbg(dev, "Modem port found\n");
		else
			altsetting = -1;
		break;
	case QCSERIAL_G2K:
		/* handle non-standard layouts */
		if (nintf == 5 && id->idProduct == QUECTEL_EC20_PID) {
			altsetting = handle_quectel_ec20(dev, ifnum);
			goto done;
		}

		/*
		 * Gobi 2K+ USB layout:
		 * 0: QMI/net
		 * 1: DM/DIAG (use libqcdm from ModemManager for communication)
		 * 2: AT-capable modem port
		 * 3: NMEA
		 */
		if (nintf < 3 || nintf > 4) {
			dev_err(dev, "unknown number of interfaces: %d\n", nintf);
			altsetting = -1;
			goto done;
		}

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
		break;
	case QCSERIAL_SWI:
		/*
		 * Sierra Wireless layout:
		 * 0: DM/DIAG (use libqcdm from ModemManager for communication)
		 * 2: NMEA
		 * 3: AT-capable modem port
		 * 8: QMI/net
		 */
		switch (ifnum) {
		case 0:
			dev_dbg(dev, "DM/DIAG interface found\n");
			break;
		case 2:
			dev_dbg(dev, "NMEA GPS interface found\n");
			sendsetup = true;
			break;
		case 3:
			dev_dbg(dev, "Modem port found\n");
			sendsetup = true;
			break;
		default:
			/* don't claim any unsupported interface */
			altsetting = -1;
			break;
		}
		break;
	case QCSERIAL_HWI:
		/*
		 * Huawei devices map functions by subclass + protocol
		 * instead of interface numbers. The protocol identify
		 * a specific function, while the subclass indicate a
		 * specific firmware source
		 *
		 * This is a list of functions known to be non-serial.  The rest
		 * are assumed to be serial and will be handled by this driver
		 */
		switch (intf->desc.bInterfaceProtocol) {
			/* QMI combined (qmi_wwan) */
		case 0x07:
		case 0x37:
		case 0x67:
			/* QMI data (qmi_wwan) */
		case 0x08:
		case 0x38:
		case 0x68:
			/* QMI control (qmi_wwan) */
		case 0x09:
		case 0x39:
		case 0x69:
			/* NCM like (huawei_cdc_ncm) */
		case 0x16:
		case 0x46:
		case 0x76:
			altsetting = -1;
			break;
		default:
			dev_dbg(dev, "Huawei type serial port found (%02x/%02x/%02x)\n",
				intf->desc.bInterfaceClass,
				intf->desc.bInterfaceSubClass,
				intf->desc.bInterfaceProtocol);
		}
		break;
	default:
		dev_err(dev, "unsupported device layout type: %lu\n",
			id->driver_info);
		break;
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

	if (!retval)
		usb_set_serial_data(serial, (void *)(unsigned long)sendsetup);

	return retval;
}

static int qc_attach(struct usb_serial *serial)
{
	struct usb_wwan_intf_private *data;
	bool sendsetup;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	sendsetup = !!(unsigned long)(usb_get_serial_data(serial));
	if (sendsetup)
		data->use_send_setup = 1;

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
	.dtr_rts	     = usb_wwan_dtr_rts,
	.write		     = usb_wwan_write,
	.write_room	     = usb_wwan_write_room,
	.chars_in_buffer     = usb_wwan_chars_in_buffer,
	.tiocmget            = usb_wwan_tiocmget,
	.tiocmset            = usb_wwan_tiocmset,
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
