/*
 * Greybus "AP" USB driver
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/usb.h>


static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x0000, 0x0000) },		// FIXME
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);


static int ap_probe(struct usb_interface *interface,
		    const struct usb_device_id *id)
{

	return 0;
}

static void ap_disconnect(struct usb_interface *interface)
{


}

static struct usb_driver es1_ap_driver = {
	.name =		"es1_ap_driver",
	.probe =	ap_probe,
	.disconnect =	ap_disconnect,
	.id_table =	id_table,
};

module_usb_driver(es1_ap_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org>");
