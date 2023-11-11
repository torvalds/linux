// SPDX-License-Identifier: GPL-2.0-only
/*
 * CNS3xxx common devices
 *
 * Copyright 2008 Cavium Networks
 *		  Scott Shu
 * Copyright 2010 MontaVista Software, LLC.
 *		  Anton Vorontsov <avorontsov@mvista.com>
 */

#include <linux/io.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include "cns3xxx.h"
#include "pm.h"
#include "core.h"
#include "devices.h"

/*
 * AHCI
 */
static struct resource cns3xxx_ahci_resource[] = {
	[0] = {
		.start	= CNS3XXX_SATA2_BASE,
		.end	= CNS3XXX_SATA2_BASE + CNS3XXX_SATA2_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_CNS3XXX_SATA,
		.end	= IRQ_CNS3XXX_SATA,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 cns3xxx_ahci_dmamask = DMA_BIT_MASK(32);

static struct platform_device cns3xxx_ahci_pdev = {
	.name		= "ahci",
	.id		= 0,
	.resource	= cns3xxx_ahci_resource,
	.num_resources	= ARRAY_SIZE(cns3xxx_ahci_resource),
	.dev		= {
		.dma_mask		= &cns3xxx_ahci_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

void __init cns3xxx_ahci_init(void)
{
	u32 tmp;

	tmp = __raw_readl(MISC_SATA_POWER_MODE);
	tmp |= 0x1 << 16; /* Disable SATA PHY 0 from SLUMBER Mode */
	tmp |= 0x1 << 17; /* Disable SATA PHY 1 from SLUMBER Mode */
	__raw_writel(tmp, MISC_SATA_POWER_MODE);

	/* Enable SATA PHY */
	cns3xxx_pwr_power_up(0x1 << PM_PLL_HM_PD_CTRL_REG_OFFSET_SATA_PHY0);
	cns3xxx_pwr_power_up(0x1 << PM_PLL_HM_PD_CTRL_REG_OFFSET_SATA_PHY1);

	/* Enable SATA Clock */
	cns3xxx_pwr_clk_en(0x1 << PM_CLK_GATE_REG_OFFSET_SATA);

	/* De-Asscer SATA Reset */
	cns3xxx_pwr_soft_rst(CNS3XXX_PWR_SOFTWARE_RST(SATA));

	platform_device_register(&cns3xxx_ahci_pdev);
}

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
	u32 __iomem *gpioa = IOMEM(CNS3XXX_MISC_BASE_VIRT + 0x0014);
	u32 gpioa_pins = __raw_readl(gpioa);

	/* MMC/SD pins share with GPIOA */
	gpioa_pins |= 0x1fff0004;
	__raw_writel(gpioa_pins, gpioa);

	cns3xxx_pwr_clk_en(CNS3XXX_PWR_CLK_EN(SDIO));
	cns3xxx_pwr_soft_rst(CNS3XXX_PWR_SOFTWARE_RST(SDIO));

	platform_device_register(&cns3xxx_sdhci_pdev);
}
