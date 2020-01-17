// SPDX-License-Identifier: GPL-2.0
/*
 * All the USB yestify logic
 *
 * (C) Copyright 2005 Greg Kroah-Hartman <gregkh@suse.de>
 *
 * yestifier functions originally based on those in kernel/sys.c
 * but fixed up to yest be so broken.
 *
 * Released under the GPLv2 only.
 */


#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/yestifier.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include "usb.h"

static BLOCKING_NOTIFIER_HEAD(usb_yestifier_list);

/**
 * usb_register_yestify - register a yestifier callback whenever a usb change happens
 * @nb: pointer to the yestifier block for the callback events.
 *
 * These changes are either USB devices or busses being added or removed.
 */
void usb_register_yestify(struct yestifier_block *nb)
{
	blocking_yestifier_chain_register(&usb_yestifier_list, nb);
}
EXPORT_SYMBOL_GPL(usb_register_yestify);

/**
 * usb_unregister_yestify - unregister a yestifier callback
 * @nb: pointer to the yestifier block for the callback events.
 *
 * usb_register_yestify() must have been previously called for this function
 * to work properly.
 */
void usb_unregister_yestify(struct yestifier_block *nb)
{
	blocking_yestifier_chain_unregister(&usb_yestifier_list, nb);
}
EXPORT_SYMBOL_GPL(usb_unregister_yestify);


void usb_yestify_add_device(struct usb_device *udev)
{
	blocking_yestifier_call_chain(&usb_yestifier_list, USB_DEVICE_ADD, udev);
}

void usb_yestify_remove_device(struct usb_device *udev)
{
	blocking_yestifier_call_chain(&usb_yestifier_list,
			USB_DEVICE_REMOVE, udev);
}

void usb_yestify_add_bus(struct usb_bus *ubus)
{
	blocking_yestifier_call_chain(&usb_yestifier_list, USB_BUS_ADD, ubus);
}

void usb_yestify_remove_bus(struct usb_bus *ubus)
{
	blocking_yestifier_call_chain(&usb_yestifier_list, USB_BUS_REMOVE, ubus);
}
