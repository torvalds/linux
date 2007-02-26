/*
 * USB device quirk handling logic and table
 *
 * Copyright (c) 2007 Oliver Neukum
 * Copyright (c) 2007 Greg Kroah-Hartman <gregkh@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, version 2.
 *
 *
 */

#include <linux/usb.h>
#include <linux/usb/quirks.h>
#include "usb.h"

/* List of quirky USB devices.  Please keep this list ordered by:
 * 	1) Vendor ID
 * 	2) Product ID
 * 	3) Class ID
 *
 * as we want specific devices to be overridden first, and only after that, any
 * class specific quirks.
 *
 * Right now the logic aborts if it finds a valid device in the table, we might
 * want to change that in the future if it turns out that a whole class of
 * devices is broken...
 */
static const struct usb_device_id usb_quirk_list[] = {
	/* HP 5300/5370C scanner */
	{ USB_DEVICE(0x03f0, 0x0701), .driver_info = USB_QUIRK_STRING_FETCH_255 },

	/* Elsa MicroLink 56k (V.250) */
	{ USB_DEVICE(0x05cc, 0x2267), .driver_info = USB_QUIRK_NO_AUTOSUSPEND },

	{ }  /* terminating entry must be last */
};

static void usb_autosuspend_quirk(struct usb_device *udev)
{
#ifdef	CONFIG_USB_SUSPEND
	/* disable autosuspend, but allow the user to re-enable it via sysfs */
	udev->autosuspend_delay = 0;
#endif
}

static const struct usb_device_id *find_id(struct usb_device *udev)
{
	const struct usb_device_id *id = usb_quirk_list;

	for (; id->idVendor || id->bDeviceClass || id->bInterfaceClass ||
			id->driver_info; id++) {
		if (usb_match_device(udev, id))
			return id;
	}
	return NULL;
}

/*
 * Detect any quirks the device has, and do any housekeeping for it if needed.
 */
void usb_detect_quirks(struct usb_device *udev)
{
	const struct usb_device_id *id = usb_quirk_list;

	id = find_id(udev);
	if (id)
		udev->quirks = (u32)(id->driver_info);
	if (udev->quirks)
		dev_dbg(&udev->dev, "USB quirks for this device: %x\n",
				udev->quirks);

	/* do any special quirk handling here if needed */
	if (udev->quirks & USB_QUIRK_NO_AUTOSUSPEND)
		usb_autosuspend_quirk(udev);
}
