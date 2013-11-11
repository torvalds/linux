/*
 * Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/platform_data/usb-ehci-mxc.h>

#include "hardware.h"

#define USBCTRL_OTGBASE_OFFSET	0x600

#define MX25_OTG_SIC_SHIFT	29
#define MX25_OTG_SIC_MASK	(0x3 << MX25_OTG_SIC_SHIFT)
#define MX25_OTG_PM_BIT		(1 << 24)
#define MX25_OTG_PP_BIT		(1 << 11)
#define MX25_OTG_OCPOL_BIT	(1 << 3)

#define MX25_H1_SIC_SHIFT	21
#define MX25_H1_SIC_MASK	(0x3 << MX25_H1_SIC_SHIFT)
#define MX25_H1_PP_BIT		(1 << 18)
#define MX25_H1_PM_BIT		(1 << 16)
#define MX25_H1_IPPUE_UP_BIT	(1 << 7)
#define MX25_H1_IPPUE_DOWN_BIT	(1 << 6)
#define MX25_H1_TLL_BIT		(1 << 5)
#define MX25_H1_USBTE_BIT	(1 << 4)
#define MX25_H1_OCPOL_BIT	(1 << 2)

int mx25_initialize_usb_hw(int port, unsigned int flags)
{
	unsigned int v;

	v = readl(MX25_IO_ADDRESS(MX25_USB_BASE_ADDR + USBCTRL_OTGBASE_OFFSET));

	switch (port) {
	case 0:	/* OTG port */
		v &= ~(MX25_OTG_SIC_MASK | MX25_OTG_PM_BIT | MX25_OTG_PP_BIT |
			MX25_OTG_OCPOL_BIT);
		v |= (flags & MXC_EHCI_INTERFACE_MASK) << MX25_OTG_SIC_SHIFT;

		if (!(flags & MXC_EHCI_POWER_PINS_ENABLED))
			v |= MX25_OTG_PM_BIT;

		if (flags & MXC_EHCI_PWR_PIN_ACTIVE_HIGH)
			v |= MX25_OTG_PP_BIT;

		if (!(flags & MXC_EHCI_OC_PIN_ACTIVE_LOW))
			v |= MX25_OTG_OCPOL_BIT;

		break;
	case 1: /* H1 port */
		v &= ~(MX25_H1_SIC_MASK | MX25_H1_PM_BIT | MX25_H1_PP_BIT |
			MX25_H1_OCPOL_BIT | MX25_H1_TLL_BIT | MX25_H1_USBTE_BIT |
			MX25_H1_IPPUE_DOWN_BIT | MX25_H1_IPPUE_UP_BIT);
		v |= (flags & MXC_EHCI_INTERFACE_MASK) << MX25_H1_SIC_SHIFT;

		if (!(flags & MXC_EHCI_POWER_PINS_ENABLED))
			v |= MX25_H1_PM_BIT;

		if (flags & MXC_EHCI_PWR_PIN_ACTIVE_HIGH)
			v |= MX25_H1_PP_BIT;

		if (!(flags & MXC_EHCI_OC_PIN_ACTIVE_LOW))
			v |= MX25_H1_OCPOL_BIT;

		if (!(flags & MXC_EHCI_TTL_ENABLED))
			v |= MX25_H1_TLL_BIT;

		if (flags & MXC_EHCI_INTERNAL_PHY)
			v |= MX25_H1_USBTE_BIT;

		if (flags & MXC_EHCI_IPPUE_DOWN)
			v |= MX25_H1_IPPUE_DOWN_BIT;

		if (flags & MXC_EHCI_IPPUE_UP)
			v |= MX25_H1_IPPUE_UP_BIT;

		break;
	default:
		return -EINVAL;
	}

	writel(v, MX25_IO_ADDRESS(MX25_USB_BASE_ADDR + USBCTRL_OTGBASE_OFFSET));

	return 0;
}

