/* linux/arch/arm/plat-s3c64xx/dev-spi.c
 *
 * Copyright (C) 2009 Samsung Electronics Ltd.
 *	Jaswinder Singh <jassi.brar@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include <mach/dma.h>
#include <mach/map.h>
#include <mach/gpio.h>
#include <mach/gpio-bank-c.h>
#include <mach/spi-clocks.h>

#include <plat/s3c64xx-spi.h>
#include <plat/gpio-cfg.h>
#include <plat/irqs.h>

static char *spi_src_clks[] = {
	[S3C64XX_SPI_SRCCLK_PCLK] = "pclk",
	[S3C64XX_SPI_SRCCLK_SPIBUS] = "spi-bus",
	[S3C64XX_SPI_SRCCLK_48M] = "spi_48m",
};

/* SPI Controller platform_devices */

/* Since we emulate multi-cs capability, we do not touch the GPC-3,7.
 * The emulated CS is toggled by board specific mechanism, as it can
 * be either some immediate GPIO or some signal out of some other
 * chip in between ... or some yet another way.
 * We simply do not assume anything about CS.
 */
static int s3c64xx_spi_cfg_gpio(struct platform_device *pdev)
{
	switch (pdev->id) {
	case 0:
		s3c_gpio_cfgpin(S3C64XX_GPC(0), S3C64XX_GPC0_SPI_MISO0);
		s3c_gpio_cfgpin(S3C64XX_GPC(1), S3C64XX_GPC1_SPI_CLKO);
		s3c_gpio_cfgpin(S3C64XX_GPC(2), S3C64XX_GPC2_SPI_MOSIO);
		s3c_gpio_setpull(S3C64XX_GPC(0), S3C_GPIO_PULL_UP);
		s3c_gpio_setpull(S3C64XX_GPC(1), S3C_GPIO_PULL_UP);
		s3c_gpio_setpull(S3C64XX_GPC(2), S3C_GPIO_PULL_UP);
		break;

	case 1:
		s3c_gpio_cfgpin(S3C64XX_GPC(4), S3C64XX_GPC4_SPI_MISO1);
		s3c_gpio_cfgpin(S3C64XX_GPC(5), S3C64XX_GPC5_SPI_CLK1);
		s3c_gpio_cfgpin(S3C64XX_GPC(6), S3C64XX_GPC6_SPI_MOSI1);
		s3c_gpio_setpull(S3C64XX_GPC(4), S3C_GPIO_PULL_UP);
		s3c_gpio_setpull(S3C64XX_GPC(5), S3C_GPIO_PULL_UP);
		s3c_gpio_setpull(S3C64XX_GPC(6), S3C_GPIO_PULL_UP);
		break;

	default:
		dev_err(&pdev->dev, "Invalid SPI Controller number!");
		return -EINVAL;
	}

	return 0;
}

static struct resource s3c64xx_spi0_resource[] = {
	[0] = {
		.start = S3C64XX_PA_SPI0,
		.end   = S3C64XX_PA_SPI0 + 0x100 - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = DMACH_SPI0_TX,
		.end   = DMACH_SPI0_TX,
		.flags = IORESOURCE_DMA,
	},
	[2] = {
		.start = DMACH_SPI0_RX,
		.end   = DMACH_SPI0_RX,
		.flags = IORESOURCE_DMA,
	},
	[3] = {
		.start = IRQ_SPI0,
		.end   = IRQ_SPI0,
		.flags = IORESOURCE_IRQ,
	},
};

static struct s3c64xx_spi_info s3c64xx_spi0_pdata = {
	.cfg_gpio = s3c64xx_spi_cfg_gpio,
	.fifo_lvl_mask = 0x7f,
	.rx_lvl_offset = 13,
};

static u64 spi_dmamask = DMA_BIT_MASK(32);

struct platform_device s3c64xx_device_spi0 = {
	.name		  = "s3c64xx-spi",
	.id		  = 0,
	.num_resources	  = ARRAY_SIZE(s3c64xx_spi0_resource),
	.resource	  = s3c64xx_spi0_resource,
	.dev = {
		.dma_mask		= &spi_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data = &s3c64xx_spi0_pdata,
	},
};
EXPORT_SYMBOL(s3c64xx_device_spi0);

static struct resource s3c64xx_spi1_resource[] = {
	[0] = {
		.start = S3C64XX_PA_SPI1,
		.end   = S3C64XX_PA_SPI1 + 0x100 - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = DMACH_SPI1_TX,
		.end   = DMACH_SPI1_TX,
		.flags = IORESOURCE_DMA,
	},
	[2] = {
		.start = DMACH_SPI1_RX,
		.end   = DMACH_SPI1_RX,
		.flags = IORESOURCE_DMA,
	},
	[3] = {
		.start = IRQ_SPI1,
		.end   = IRQ_SPI1,
		.flags = IORESOURCE_IRQ,
	},
};

static struct s3c64xx_spi_info s3c64xx_spi1_pdata = {
	.cfg_gpio = s3c64xx_spi_cfg_gpio,
	.fifo_lvl_mask = 0x7f,
	.rx_lvl_offset = 13,
};

struct platform_device s3c64xx_device_spi1 = {
	.name		  = "s3c64xx-spi",
	.id		  = 1,
	.num_resources	  = ARRAY_SIZE(s3c64xx_spi1_resource),
	.resource	  = s3c64xx_spi1_resource,
	.dev = {
		.dma_mask		= &spi_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data = &s3c64xx_spi1_pdata,
	},
};
EXPORT_SYMBOL(s3c64xx_device_spi1);

void __init s3c64xx_spi_set_info(int cntrlr, int src_clk_nr, int num_cs)
{
	struct s3c64xx_spi_info *pd;

	/* Reject invalid configuration */
	if (!num_cs || src_clk_nr < 0
			|| src_clk_nr > S3C64XX_SPI_SRCCLK_48M) {
		printk(KERN_ERR "%s: Invalid SPI configuration\n", __func__);
		return;
	}

	switch (cntrlr) {
	case 0:
		pd = &s3c64xx_spi0_pdata;
		break;
	case 1:
		pd = &s3c64xx_spi1_pdata;
		break;
	default:
		printk(KERN_ERR "%s: Invalid SPI controller(%d)\n",
							__func__, cntrlr);
		return;
	}

	pd->num_cs = num_cs;
	pd->src_clk_nr = src_clk_nr;
	pd->src_clk_name = spi_src_clks[src_clk_nr];
}
