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
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/usb.h>
#include <dvb-usb.h>

#define S2250_LOADER_FIRMWARE	"s2250_loader.fw"
#define S2250_FIRMWARE		"s2250.fw"

typedef struct device_extension_s {
    struct kref     kref;
    int minor;
    struct usb_device *usbdev;
} device_extension_t, *pdevice_extension_t;

#define USB_s2250loader_MAJOR 240
#define USB_s2250loader_MINOR_BASE 0
#define MAX_DEVICES 256

static pdevice_extension_t s2250_dev_table[MAX_DEVICES];
static DECLARE_MUTEX(s2250_dev_table_mutex);

#define to_s2250loader_dev_common(d) container_of(d, device_extension_t, kref)
static void s2250loader_delete(struct kref *kref)
{
	pdevice_extension_t s = to_s2250loader_dev_common(kref);
	s2250_dev_table[s->minor] = NULL;
	kfree(s);
}

static int s2250loader_probe(struct usb_interface *interface,
				const struct usb_device_id *id)
{
	struct usb_device *usbdev;
	int minor, ret;
	pdevice_extension_t s = NULL;
	const struct firmware *fw;

	usbdev = usb_get_dev(interface_to_usbdev(interface));
	if (!usbdev) {
		printk(KERN_ERR "Enter s2250loader_probe failed\n");
		return -1;
	}
	printk(KERN_INFO "Enter s2250loader_probe 2.6 kernel\n");
	printk(KERN_INFO "vendor id 0x%x, device id 0x%x devnum:%d\n",
	   usbdev->descriptor.idVendor, usbdev->descriptor.idProduct,
	   usbdev->devnum);

	if (usbdev->descriptor.bNumConfigurations != 1) {
		printk(KERN_ERR "can't handle multiple config\n");
		return -1;
	}
	down(&s2250_dev_table_mutex);

	for (minor = 0; minor < MAX_DEVICES; minor++) {
		if (s2250_dev_table[minor] == NULL)
			break;
	}

	if (minor < 0 || minor >= MAX_DEVICES) {
		printk(KERN_ERR "Invalid minor: %d\n", minor);
		goto failed;
	}

	/* Allocate dev data structure */
	s = kmalloc(sizeof(device_extension_t), GFP_KERNEL);
	if (s == NULL) {
		printk(KERN_ERR "Out of memory\n");
		goto failed;
	}
	s2250_dev_table[minor] = s;

	printk(KERN_INFO "s2250loader_probe: Device %d on Bus %d Minor %d\n",
		usbdev->devnum, usbdev->bus->busnum, minor);

	memset(s, 0, sizeof(device_extension_t));
	s->usbdev = usbdev;
	printk(KERN_INFO "loading 2250 loader\n");

	kref_init(&(s->kref));

	up(&s2250_dev_table_mutex);

	if (request_firmware(&fw, S2250_LOADER_FIRMWARE, &usbdev->dev)) {
		printk(KERN_ERR
			"s2250: unable to load firmware from file \"%s\"\n",
			S2250_LOADER_FIRMWARE);
		goto failed2;
	}
	ret = usb_cypress_load_firmware(usbdev, fw, CYPRESS_FX2);
	release_firmware(fw);
	if (0 != ret) {
		printk(KERN_ERR "loader download failed\n");
		goto failed2;
	}

	if (request_firmware(&fw, S2250_FIRMWARE, &usbdev->dev)) {
		printk(KERN_ERR
			"s2250: unable to load firmware from file \"%s\"\n",
			S2250_FIRMWARE);
		goto failed2;
	}
	ret = usb_cypress_load_firmware(usbdev, fw, CYPRESS_FX2);
	release_firmware(fw);
	if (0 != ret) {
		printk(KERN_ERR "firmware_s2250 download failed\n");
		goto failed2;
	}

	usb_set_intfdata(interface, s);
	return 0;

failed:
	up(&s2250_dev_table_mutex);
failed2:
	if (s)
		kref_put(&(s->kref), s2250loader_delete);

	printk(KERN_ERR "probe failed\n");
	return -1;
}

static void s2250loader_disconnect(struct usb_interface *interface)
{
	pdevice_extension_t s = usb_get_intfdata(interface);
	printk(KERN_INFO "s2250: disconnect\n");
	lock_kernel();
	s = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);
	kref_put(&(s->kref), s2250loader_delete);
	unlock_kernel();
}

static struct usb_device_id s2250loader_ids[] = {
	{USB_DEVICE(0x1943, 0xa250)},
	{}                          /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, s2250loader_ids);

static struct usb_driver s2250loader_driver = {
	.name		= "s2250-loader",
	.probe		= s2250loader_probe,
	.disconnect	= s2250loader_disconnect,
	.id_table	= s2250loader_ids,
};

int s2250loader_init(void)
{
	int r;
	unsigned i = 0;

	for (i = 0; i < MAX_DEVICES; i++)
		s2250_dev_table[i] = NULL;

	r = usb_register(&s2250loader_driver);
	if (r) {
		printk(KERN_ERR "usb_register failed. Error number %d\n", r);
		return -1;
	}

	printk(KERN_INFO "s2250loader_init: driver registered\n");
	return 0;
}
EXPORT_SYMBOL(s2250loader_init);

void s2250loader_cleanup(void)
{
	printk(KERN_INFO "s2250loader_cleanup\n");
	usb_deregister(&s2250loader_driver);
}
EXPORT_SYMBOL(s2250loader_cleanup);
