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

/*
 * Hack, we "know" we will only have one of these at any one time, so only
 * create one static structure pointer.
 */
struct es1_ap_dev {
	struct usb_interface *usb_intf;

} *es1_ap_dev;


static int ap_probe(struct usb_interface *interface,
		    const struct usb_device_id *id)
{
	if (es1_ap_dev) {
		dev_err(&interface->dev, "Already have a es1_ap_dev???\n");
		return -ENODEV;
	}
	es1_ap_dev = kzalloc(sizeof(*es1_ap_dev), GFP_KERNEL);
	if (!es1_ap_dev)
		return -ENOMEM;

	es1_ap_dev->usb_intf = interface;
	usb_set_intfdata(interface, es1_ap_dev);
	return 0;
}

static void ap_disconnect(struct usb_interface *interface)
{
	es1_ap_dev = usb_get_intfdata(interface);

	/* Tear down everything! */

	kfree(es1_ap_dev);
	es1_ap_dev = NULL;

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
