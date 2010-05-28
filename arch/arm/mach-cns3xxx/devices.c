/*
 * CNS3xxx common devices
 *
 * Copyright 2008 Cavium Networks
 *		  Scott Shu
 * Copyright 2010 MontaVista Software, LLC.
 *		  Anton Vorontsov <avorontsov@mvista.com>
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/platform_device.h>
#include <mach/cns3xxx.h>
#include <mach/irqs.h>
#include "core.h"
#include "devices.h"

/*
 * SDHCI
 */
static struct resource cns3xxx_sdhci_resources[] = {
	[0] = {
		.start = CNS3XXX_SDIO_BASE,
		.end   = CNS3XXX_SDIO_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_CNS3XXX_SDIO,
		.end   = IRQ_CNS3XXX_SDIO,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device cns3xxx_sdhci_pdev = {
	.name		= "sdhci-cns3xxx",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(cns3xxx_sdhci_resources),
	.resource	= cns3xxx_sdhci_resources,
};

void __init cns3xxx_sdhci_init(void)
{
	u32 __iomem *gpioa = __io(CNS3XXX_MISC_BASE_VIRT + 0x0014);
	u32 gpioa_pins = __raw_readl(gpioa);

	/* MMC/SD pins share with GPIOA */
	gpioa_pins |= 0x1fff0004;
	__raw_writel(gpioa_pins, gpioa);

	cns3xxx_pwr_clk_en(CNS3XXX_PWR_CLK_EN(SDIO));
	cns3xxx_pwr_soft_rst(CNS3XXX_PWR_SOFTWARE_RST(SDIO));

	platform_device_register(&cns3xxx_sdhci_pdev);
}
