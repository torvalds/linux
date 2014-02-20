/*
 * Copyright (C) 2008 Sensoray Company Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/firmware.h>
#include <cypress_firmware.h>

struct fw_config {
	u16 vendor;
	u16 product;
	const char * const fw_name1;
	const char * const fw_name2;
};

struct fw_config fw_configs[] = {
	{ 0x1943, 0xa250, "go7007/s2250-1.fw", "go7007/s2250-2.fw" },
	{ 0x093b, 0xa002, "go7007/px-m402u.fw", NULL },
	{ 0x093b, 0xa004, "go7007/px-tv402u.fw", NULL },
	{ 0x0eb1, 0x6666, "go7007/lr192.fw", NULL },
	{ 0x0eb1, 0x6668, "go7007/wis-startrek.fw", NULL },
	{ 0, 0, NULL, NULL }
};
MODULE_FIRMWARE("go7007/s2250-1.fw");
MODULE_FIRMWARE("go7007/s2250-2.fw");
MODULE_FIRMWARE("go7007/px-m402u.fw");
MODULE_FIRMWARE("go7007/px-tv402u.fw");
MODULE_FIRMWARE("go7007/lr192.fw");
MODULE_FIRMWARE("go7007/wis-startrek.fw");

static int go7007_loader_probe(struct usb_interface *interface,
				const struct usb_device_id *id)
{
	struct usb_device *usbdev;
	const struct firmware *fw;
	u16 vendor, product;
	const char *fw1, *fw2;
	int ret;
	int i;

	usbdev = usb_get_dev(interface_to_usbdev(interface));
	if (!usbdev)
		goto failed2;

	if (usbdev->descriptor.bNumConfigurations != 1) {
		dev_err(&interface->dev, "can't handle multiple config\n");
		goto failed2;
	}

	vendor = le16_to_cpu(usbdev->descriptor.idVendor);
	product = le16_to_cpu(usbdev->descriptor.idProduct);

	for (i = 0; fw_configs[i].fw_name1; i++)
		if (fw_configs[i].vendor == vendor &&
		    fw_configs[i].product == product)
			break;

	/* Should never happen */
	if (fw_configs[i].fw_name1 == NULL)
		goto failed2;

	fw1 = fw_configs[i].fw_name1;
	fw2 = fw_configs[i].fw_name2;

	dev_info(&interface->dev, "loading firmware %s\n", fw1);

	if (request_firmware(&fw, fw1, &usbdev->dev)) {
		dev_err(&interface->dev,
			"unable to load firmware from file \"%s\"\n", fw1);
		goto failed2;
	}
	ret = cypress_load_firmware(usbdev, fw, CYPRESS_FX2);
	release_firmware(fw);
	if (0 != ret) {
		dev_err(&interface->dev, "loader download failed\n");
		goto failed2;
	}

	if (fw2 == NULL)
		return 0;

	if (request_firmware(&fw, fw2, &usbdev->dev)) {
		dev_err(&interface->dev,
			"unable to load firmware from file \"%s\"\n", fw2);
		goto failed2;
	}
	ret = cypress_load_firmware(usbdev, fw, CYPRESS_FX2);
	release_firmware(fw);
	if (0 != ret) {
		dev_err(&interface->dev, "firmware download failed\n");
		goto failed2;
	}
	return 0;

failed2:
	usb_put_dev(usbdev);
	dev_err(&interface->dev, "probe failed\n");
	return -ENODEV;
}

static void go7007_loader_disconnect(struct usb_interface *interface)
{
	dev_info(&interface->dev, "disconnect\n");
	usb_put_dev(interface_to_usbdev(interface));
	usb_set_intfdata(interface, NULL);
}

static const struct usb_device_id go7007_loader_ids[] = {
	{ USB_DEVICE(0x1943, 0xa250) },
	{ USB_DEVICE(0x093b, 0xa002) },
	{ USB_DEVICE(0x093b, 0xa004) },
	{ USB_DEVICE(0x0eb1, 0x6666) },
	{ USB_DEVICE(0x0eb1, 0x6668) },
	{}                          /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, go7007_loader_ids);

static struct usb_driver go7007_loader_driver = {
	.name		= "go7007-loader",
	.probe		= go7007_loader_probe,
	.disconnect	= go7007_loader_disconnect,
	.id_table	= go7007_loader_ids,
};

module_usb_driver(go7007_loader_driver);

MODULE_AUTHOR("");
MODULE_DESCRIPTION("firmware loader for go7007-usb");
MODULE_LICENSE("GPL v2");
