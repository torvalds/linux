/*
 * drivers/usb/generic.c - generic driver for USB devices (not interfaces)
 *
 * (C) Copyright 2005 Greg Kroah-Hartman <gregkh@suse.de>
 *
 * based on drivers/usb/usb.c which had the following copyrights:
 *	(C) Copyright Linus Torvalds 1999
 *	(C) Copyright Johannes Erdfelt 1999-2001
 *	(C) Copyright Andreas Gal 1999
 *	(C) Copyright Gregory P. Smith 1999
 *	(C) Copyright Deti Fliegl 1999 (new USB architecture)
 *	(C) Copyright Randy Dunlap 2000
 *	(C) Copyright David Brownell 2000-2004
 *	(C) Copyright Yggdrasil Computing, Inc. 2000
 *		(usb_device_id matching changes by Adam J. Richter)
 *	(C) Copyright Greg Kroah-Hartman 2002-2003
 *
 */

#include <linux/config.h>
#include <linux/usb.h>
#include "usb.h"

static int generic_probe(struct usb_device *udev)
{
	return 0;
}
static void generic_disconnect(struct usb_device *udev)
{
	/* if this is only an unbind, not a physical disconnect, then
	 * unconfigure the device */
	if (udev->state == USB_STATE_CONFIGURED)
		usb_set_configuration(udev, 0);

	/* in case the call failed or the device was suspended */
	if (udev->state >= USB_STATE_CONFIGURED)
		usb_disable_device(udev, 0);
}

struct usb_device_driver usb_generic_driver = {
	.name =	"usb",
	.probe = generic_probe,
	.disconnect = generic_disconnect,
};
