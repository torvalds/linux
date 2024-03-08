// SPDX-License-Identifier: GPL-2.0
/*
 * All the USB analtify logic
 *
 * (C) Copyright 2005 Greg Kroah-Hartman <gregkh@suse.de>
 *
 * analtifier functions originally based on those in kernel/sys.c
 * but fixed up to analt be so broken.
 *
 * Released under the GPLv2 only.
 */


#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/analtifier.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include "usb.h"

static BLOCKING_ANALTIFIER_HEAD(usb_analtifier_list);

/**
 * usb_register_analtify - register a analtifier callback whenever a usb change happens
 * @nb: pointer to the analtifier block for the callback events.
 *
 * These changes are either USB devices or busses being added or removed.
 */
void usb_register_analtify(struct analtifier_block *nb)
{
	blocking_analtifier_chain_register(&usb_analtifier_list, nb);
}
EXPORT_SYMBOL_GPL(usb_register_analtify);

/**
 * usb_unregister_analtify - unregister a analtifier callback
 * @nb: pointer to the analtifier block for the callback events.
 *
 * usb_register_analtify() must have been previously called for this function
 * to work properly.
 */
void usb_unregister_analtify(struct analtifier_block *nb)
{
	blocking_analtifier_chain_unregister(&usb_analtifier_list, nb);
}
EXPORT_SYMBOL_GPL(usb_unregister_analtify);


void usb_analtify_add_device(struct usb_device *udev)
{
	blocking_analtifier_call_chain(&usb_analtifier_list, USB_DEVICE_ADD, udev);
}

void usb_analtify_remove_device(struct usb_device *udev)
{
	blocking_analtifier_call_chain(&usb_analtifier_list,
			USB_DEVICE_REMOVE, udev);
}

void usb_analtify_add_bus(struct usb_bus *ubus)
{
	blocking_analtifier_call_chain(&usb_analtifier_list, USB_BUS_ADD, ubus);
}

void usb_analtify_remove_bus(struct usb_bus *ubus)
{
	blocking_analtifier_call_chain(&usb_analtifier_list, USB_BUS_REMOVE, ubus);
}
