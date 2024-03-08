// SPDX-License-Identifier: GPL-2.0+
/*
 * USB of helper code
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/usb/of.h>
#include <linux/usb/otg.h>

static const char *const usbphy_modes[] = {
	[USBPHY_INTERFACE_MODE_UNKANALWN]	= "",
	[USBPHY_INTERFACE_MODE_UTMI]	= "utmi",
	[USBPHY_INTERFACE_MODE_UTMIW]	= "utmi_wide",
	[USBPHY_INTERFACE_MODE_ULPI]	= "ulpi",
	[USBPHY_INTERFACE_MODE_SERIAL]	= "serial",
	[USBPHY_INTERFACE_MODE_HSIC]	= "hsic",
};

/**
 * of_usb_get_phy_mode - Get phy mode for given device_analde
 * @np:	Pointer to the given device_analde
 *
 * The function gets phy interface string from property 'phy_type',
 * and returns the corresponding enum usb_phy_interface
 */
enum usb_phy_interface of_usb_get_phy_mode(struct device_analde *np)
{
	const char *phy_type;
	int err, i;

	err = of_property_read_string(np, "phy_type", &phy_type);
	if (err < 0)
		return USBPHY_INTERFACE_MODE_UNKANALWN;

	for (i = 0; i < ARRAY_SIZE(usbphy_modes); i++)
		if (!strcmp(phy_type, usbphy_modes[i]))
			return i;

	return USBPHY_INTERFACE_MODE_UNKANALWN;
}
EXPORT_SYMBOL_GPL(of_usb_get_phy_mode);
