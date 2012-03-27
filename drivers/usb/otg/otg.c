/*
 * otg.c -- USB OTG utility code
 *
 * Copyright (C) 2004 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/device.h>

#include <linux/usb/otg.h>

static struct usb_phy *phy;

/**
 * usb_get_transceiver - find the (single) USB transceiver
 *
 * Returns the transceiver driver, after getting a refcount to it; or
 * null if there is no such transceiver.  The caller is responsible for
 * calling usb_put_transceiver() to release that count.
 *
 * For use by USB host and peripheral drivers.
 */
struct usb_phy *usb_get_transceiver(void)
{
	if (phy)
		get_device(phy->dev);
	return phy;
}
EXPORT_SYMBOL(usb_get_transceiver);

/**
 * usb_put_transceiver - release the (single) USB transceiver
 * @x: the transceiver returned by usb_get_transceiver()
 *
 * Releases a refcount the caller received from usb_get_transceiver().
 *
 * For use by USB host and peripheral drivers.
 */
void usb_put_transceiver(struct usb_phy *x)
{
	if (x)
		put_device(x->dev);
}
EXPORT_SYMBOL(usb_put_transceiver);

/**
 * usb_set_transceiver - declare the (single) USB transceiver
 * @x: the USB transceiver to be used; or NULL
 *
 * This call is exclusively for use by transceiver drivers, which
 * coordinate the activities of drivers for host and peripheral
 * controllers, and in some cases for VBUS current regulation.
 */
int usb_set_transceiver(struct usb_phy *x)
{
	if (phy && x)
		return -EBUSY;
	phy = x;
	return 0;
}
EXPORT_SYMBOL(usb_set_transceiver);

const char *otg_state_string(enum usb_otg_state state)
{
	switch (state) {
	case OTG_STATE_A_IDLE:
		return "a_idle";
	case OTG_STATE_A_WAIT_VRISE:
		return "a_wait_vrise";
	case OTG_STATE_A_WAIT_BCON:
		return "a_wait_bcon";
	case OTG_STATE_A_HOST:
		return "a_host";
	case OTG_STATE_A_SUSPEND:
		return "a_suspend";
	case OTG_STATE_A_PERIPHERAL:
		return "a_peripheral";
	case OTG_STATE_A_WAIT_VFALL:
		return "a_wait_vfall";
	case OTG_STATE_A_VBUS_ERR:
		return "a_vbus_err";
	case OTG_STATE_B_IDLE:
		return "b_idle";
	case OTG_STATE_B_SRP_INIT:
		return "b_srp_init";
	case OTG_STATE_B_PERIPHERAL:
		return "b_peripheral";
	case OTG_STATE_B_WAIT_ACON:
		return "b_wait_acon";
	case OTG_STATE_B_HOST:
		return "b_host";
	default:
		return "UNDEFINED";
	}
}
EXPORT_SYMBOL(otg_state_string);
