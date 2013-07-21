/*
 * USB of helper code
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/usb/of.h>
#include <linux/usb/otg.h>

static const char *const usbphy_modes[] = {
	[USBPHY_INTERFACE_MODE_UNKNOWN]	= "",
	[USBPHY_INTERFACE_MODE_UTMI]	= "utmi",
	[USBPHY_INTERFACE_MODE_UTMIW]	= "utmi_wide",
	[USBPHY_INTERFACE_MODE_ULPI]	= "ulpi",
	[USBPHY_INTERFACE_MODE_SERIAL]	= "serial",
	[USBPHY_INTERFACE_MODE_HSIC]	= "hsic",
};

/**
 * of_usb_get_phy_mode - Get phy mode for given device_node
 * @np:	Pointer to the given device_node
 *
 * The function gets phy interface string from property 'phy_type',
 * and returns the correspondig enum usb_phy_interface
 */
enum usb_phy_interface of_usb_get_phy_mode(struct device_node *np)
{
	const char *phy_type;
	int err, i;

	err = of_property_read_string(np, "phy_type", &phy_type);
	if (err < 0)
		return USBPHY_INTERFACE_MODE_UNKNOWN;

	for (i = 0; i < ARRAY_SIZE(usbphy_modes); i++)
		if (!strcmp(phy_type, usbphy_modes[i]))
			return i;

	return USBPHY_INTERFACE_MODE_UNKNOWN;
}
EXPORT_SYMBOL_GPL(of_usb_get_phy_mode);
