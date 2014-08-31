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

struct es1_ap_dev {
	struct usb_device *usb_dev;
	struct usb_interface *usb_intf;

	__u8 ap_in_endpoint;
	__u8 ap_out_endpoint;
	u8 *ap_buffer;

};

/*
 * Hack, we "know" we will only have one of these at any one time, so only
 * create one static structure pointer.
 */
static struct es1_ap_dev *es1_ap_dev;


static int ap_probe(struct usb_interface *interface,
		    const struct usb_device_id *id)
{
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	size_t buffer_size;
	int i;

	if (es1_ap_dev) {
		dev_err(&interface->dev, "Already have a es1_ap_dev???\n");
		return -ENODEV;
	}
	es1_ap_dev = kzalloc(sizeof(*es1_ap_dev), GFP_KERNEL);
	if (!es1_ap_dev)
		return -ENOMEM;

	// FIXME
	// figure out endpoint for talking to the AP.
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (usb_endpoint_is_bulk_in(endpoint)) {
			buffer_size = usb_endpoint_maxp(endpoint);
			// FIXME - Save buffer_size?
			es1_ap_dev->ap_in_endpoint = endpoint->bEndpointAddress;
		}
		if (usb_endpoint_is_bulk_out(endpoint)) {
			// FIXME - anything else about this we need?
			es1_ap_dev->ap_out_endpoint = endpoint->bEndpointAddress;
		}
		// FIXME - properly exit once found the AP endpoint
		// FIXME - set up cport endpoints
	}

	// FIXME - allocate buffer
	// FIXME = start up talking, then create the gb "devices" based on what the AP tells us.

	es1_ap_dev->usb_intf = interface;
	es1_ap_dev->usb_dev = usb_get_dev(interface_to_usbdev(interface));
	usb_set_intfdata(interface, es1_ap_dev);
	return 0;
}

static void ap_disconnect(struct usb_interface *interface)
{
	es1_ap_dev = usb_get_intfdata(interface);

	/* Tear down everything! */

	usb_put_dev(es1_ap_dev->usb_dev);
	kfree(es1_ap_dev->ap_buffer);
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
