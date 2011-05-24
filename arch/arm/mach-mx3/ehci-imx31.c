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

#include <mach/hardware.h>
#include <mach/mxc_ehci.h>

#define USBCTRL_OTGBASE_OFFSET	0x600

#define MX31_OTG_SIC_SHIFT	29
#define MX31_OTG_SIC_MASK	(0x3 << MX31_OTG_SIC_SHIFT)
#define MX31_OTG_PM_BIT		(1 << 24)

#define MX31_H2_SIC_SHIFT	21
#define MX31_H2_SIC_MASK	(0x3 << MX31_H2_SIC_SHIFT)
#define MX31_H2_PM_BIT		(1 << 16)
#define MX31_H2_DT_BIT		(1 << 5)

#define MX31_H1_SIC_SHIFT	13
#define MX31_H1_SIC_MASK	(0x3 << MX31_H1_SIC_SHIFT)
#define MX31_H1_PM_BIT		(1 << 8)
#define MX31_H1_DT_BIT		(1 << 4)

int mx31_initialize_usb_hw(int port, unsigned int flags)
{
	unsigned int v;

	v = readl(MX31_IO_ADDRESS(MX31_USB_BASE_ADDR + USBCTRL_OTGBASE_OFFSET));

	switch (port) {
	case 0:	/* OTG port */
		v &= ~(MX31_OTG_SIC_MASK | MX31_OTG_PM_BIT);
		v |= (flags & MXC_EHCI_INTERFACE_MASK) << MX31_OTG_SIC_SHIFT;

		if (!(flags & MXC_EHCI_POWER_PINS_ENABLED))
			v |= MX31_OTG_PM_BIT;

		break;
	case 1: /* H1 port */
		v &= ~(MX31_H1_SIC_MASK | MX31_H1_PM_BIT | MX31_H1_DT_BIT);
		v |= (flags & MXC_EHCI_INTERFACE_MASK) << MX31_H1_SIC_SHIFT;

		if (!(flags & MXC_EHCI_POWER_PINS_ENABLED))
			v |= MX31_H1_PM_BIT;

		if (!(flags & MXC_EHCI_TTL_ENABLED))
			v |= MX31_H1_DT_BIT;

		break;
	case 2:	/* H2 port */
		v &= ~(MX31_H2_SIC_MASK | MX31_H2_PM_BIT | MX31_H2_DT_BIT);
		v |= (flags & MXC_EHCI_INTERFACE_MASK) << MX31_H2_SIC_SHIFT;

		if (!(flags & MXC_EHCI_POWER_PINS_ENABLED))
			v |= MX31_H2_PM_BIT;

		if (!(flags & MXC_EHCI_TTL_ENABLED))
			v |= MX31_H2_DT_BIT;

		break;
	default:
		return -EINVAL;
	}

	writel(v, MX31_IO_ADDRESS(MX31_USB_BASE_ADDR + USBCTRL_OTGBASE_OFFSET));

	return 0;
}

