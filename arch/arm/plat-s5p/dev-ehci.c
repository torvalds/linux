/*
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/platform_device.h>
#include <mach/irqs.h>
#include <mach/map.h>
#include <plat/devs.h>
#include <plat/ehci.h>
#include <plat/usb-phy.h>

/* USB EHCI Host Controller registration */
static struct resource s5p_ehci_resource[] = {
	[0] = {
		.start	= S5P_PA_EHCI,
		.end	= S5P_PA_EHCI + SZ_256 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_USB_HOST,
		.end	= IRQ_USB_HOST,
		.flags	= IORESOURCE_IRQ,
	}
};

static u64 s5p_device_ehci_dmamask = 0xffffffffUL;

struct platform_device s5p_device_ehci = {
	.name		= "s5p-ehci",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_ehci_resource),
	.resource	= s5p_ehci_resource,
	.dev		= {
		.dma_mask = &s5p_device_ehci_dmamask,
		.coherent_dma_mask = 0xffffffffUL
	}
};

void __init s5p_ehci_set_platdata(struct s5p_ehci_platdata *pd)
{
	struct s5p_ehci_platdata *npd;

	npd = s3c_set_platdata(pd, sizeof(struct s5p_ehci_platdata),
			&s5p_device_ehci);

	if (!npd->phy_init)
		npd->phy_init = s5p_usb_phy_init;
	if (!npd->phy_exit)
		npd->phy_exit = s5p_usb_phy_exit;
}
