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
#include <mach/gpio.h>
#include <plat/devs.h>
#include <plat/ehci.h>
#include <plat/usb-phy.h>

#ifdef CONFIG_USB_EHCI_S5P
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
	if (!npd->phy_suspend)
		npd->phy_suspend = s5p_usb_phy_suspend;
	if (!npd->phy_resume)
		npd->phy_resume = s5p_usb_phy_resume;
}
#endif

#ifdef CONFIG_USB_OHCI_S5P
/* USB Host Controlle OHCI registrations */
static struct resource s5p_ohci_resource[] = {
	[0] = {
		.start = S5P_PA_OHCI,
		.end   = S5P_PA_OHCI  + SZ_256 - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_USB_HOST,
		.end   = IRQ_USB_HOST,
		.flags = IORESOURCE_IRQ,
	}
};

static u64 s5p_device_ohci_dmamask = 0xffffffffUL;

struct platform_device s5p_device_ohci = {
	.name		= "s5p-ohci",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_ohci_resource),
	.resource	= s5p_ohci_resource,
	.dev		= {
		.dma_mask = &s5p_device_ohci_dmamask,
		.coherent_dma_mask = 0xffffffffUL
	}
};

void __init s5p_ohci_set_platdata(struct s5p_ohci_platdata *pd)
{
	struct s5p_ohci_platdata *npd;

	npd = s3c_set_platdata(pd, sizeof(struct s5p_ohci_platdata),
			&s5p_device_ohci);

	if (!npd->phy_init)
		npd->phy_init = s5p_usb_phy_init;
	if (!npd->phy_exit)
		npd->phy_exit = s5p_usb_phy_exit;
	if (!npd->phy_suspend)
		npd->phy_suspend = s5p_usb_phy_suspend;
	if (!npd->phy_resume)
		npd->phy_resume = s5p_usb_phy_resume;
}
#endif

#ifdef CONFIG_S5P_DEV_USB_SWITCH
/* USB Switch */
static struct resource s5p_usbswitch_resource[] = {
	[0] = {
		.start = IRQ_EINT(29),
		.end   = IRQ_EINT(29),
		.flags = IORESOURCE_IRQ,
	},
	[1] = {
		.start = IRQ_EINT(28),
		.end   = IRQ_EINT(28),
		.flags = IORESOURCE_IRQ,
	}
};

struct platform_device s5p_device_usbswitch = {
	.name		= "exynos-usb-switch",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_usbswitch_resource),
	.resource	= s5p_usbswitch_resource,
};

void __init s5p_usbswitch_set_platdata(struct s5p_usbswitch_platdata *pd)
{
	struct s5p_usbswitch_platdata *npd;

	npd = s3c_set_platdata(pd, sizeof(struct s5p_usbswitch_platdata),
			&s5p_device_usbswitch);

	s5p_usbswitch_resource[0].start = gpio_to_irq(npd->gpio_host_detect);
	s5p_usbswitch_resource[0].end = gpio_to_irq(npd->gpio_host_detect);

	s5p_usbswitch_resource[1].start = gpio_to_irq(npd->gpio_device_detect);
	s5p_usbswitch_resource[1].end = gpio_to_irq(npd->gpio_device_detect);
}
#endif /* CONFIG_USB_SWITCH */
