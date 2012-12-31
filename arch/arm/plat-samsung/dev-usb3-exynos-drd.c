/* arch/arm/plat-samsung/dev-usb3-dwc-drd.c
 *
 * Copyright (c) 2011 Samsung Electronics Co. Ltd
 * Author: Anton Tikhomirov <av.tikhomirov@samsung.com>
 *
 * Device definition for EXYNOS SuperSpeed USB 3.0 DRD Controller
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include <linux/platform_data/exynos_usb3_drd.h>

#include <mach/irqs.h>
#include <mach/map.h>

#include <plat/devs.h>
#include <plat/usb-phy.h>

static struct resource exynos_ss_udc_resources[] = {
	[0] = {
		.start	= EXYNOS5_PA_SS_DRD,
		.end	= EXYNOS5_PA_SS_DRD + 0x100000 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_USB3_DRD,
		.end	= IRQ_USB3_DRD,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource exynos_xhci_resources[] = {
	[0] = {
		.start	= EXYNOS5_PA_SS_DRD,
		.end	= EXYNOS5_PA_SS_DRD + 0x100000 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_USB3_DRD,
		.end	= IRQ_USB3_DRD,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 exynos_ss_udc_dmamask = DMA_BIT_MASK(32);
static u64 exynos_xhci_dmamask = DMA_BIT_MASK(32);

struct platform_device exynos_device_ss_udc = {
	.name		= "exynos-ss-udc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(exynos_ss_udc_resources),
	.resource	= exynos_ss_udc_resources,
	.dev		= {
		.dma_mask		= &exynos_ss_udc_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

struct platform_device exynos_device_xhci = {
	.name		= "exynos-xhci",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(exynos_xhci_resources),
	.resource	= exynos_xhci_resources,
	.dev		= {
		.dma_mask		= &exynos_xhci_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

void __init exynos_ss_udc_set_platdata(struct exynos_usb3_drd_pdata *pd)
{
	struct exynos_usb3_drd_pdata *npd;

	npd = s3c_set_platdata(pd, sizeof(struct exynos_usb3_drd_pdata),
			&exynos_device_ss_udc);

	npd->phy_type = S5P_USB_PHY_DRD;
	if (!npd->phy_init)
		npd->phy_init = s5p_usb_phy_init;
	if (!npd->phy_exit)
		npd->phy_exit = s5p_usb_phy_exit;
}

void __init exynos_xhci_set_platdata(struct exynos_usb3_drd_pdata *pd)
{
	struct exynos_usb3_drd_pdata *npd;

	npd = s3c_set_platdata(pd, sizeof(struct exynos_usb3_drd_pdata),
			&exynos_device_xhci);

	npd->phy_type = S5P_USB_PHY_DRD;
	if (!npd->phy_init)
		npd->phy_init = s5p_usb_phy_init;
	if (!npd->phy_exit)
		npd->phy_exit = s5p_usb_phy_exit;
}
