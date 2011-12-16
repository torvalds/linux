/* linux/arch/arm/mach-s5p64x0/dev-spi.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Copyright (C) 2010 Samsung Electronics Co. Ltd.
 *	Jaswinder Singh <jassi.brar@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>

#include <mach/dma.h>
#include <mach/map.h>
#include <mach/irqs.h>
#include <mach/regs-clock.h>
#include <mach/spi-clocks.h>

#include <plat/cpu.h>
#include <plat/s3c64xx-spi.h>
#include <plat/gpio-cfg.h>

static char *s5p64x0_spi_src_clks[] = {
	[S5P64X0_SPI_SRCCLK_PCLK] = "pclk",
	[S5P64X0_SPI_SRCCLK_SCLK] = "sclk_spi",
};

/* SPI Controller platform_devices */

/* Since we emulate multi-cs capability, we do not touch the CS.
 * The emulated CS is toggled by board specific mechanism, as it can
 * be either some immediate GPIO or some signal out of some other
 * chip in between ... or some yet another way.
 * We simply do not assume anything about CS.
 */
static int s5p6440_spi_cfg_gpio(struct platform_device *pdev)
{
	unsigned int base;

	switch (pdev->id) {
	case 0:
		base = S5P6440_GPC(0);
		break;

	case 1:
		base = S5P6440_GPC(4);
		break;

	default:
		dev_err(&pdev->dev, "Invalid SPI Controller number!");
		return -EINVAL;
	}

	s3c_gpio_cfgall_range(base, 3,
			      S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);

	return 0;
}

static int s5p6450_spi_cfg_gpio(struct platform_device *pdev)
{
	unsigned int base;

	switch (pdev->id) {
	case 0:
		base = S5P6450_GPC(0);
		break;

	case 1:
		base = S5P6450_GPC(4);
		break;

	default:
		dev_err(&pdev->dev, "Invalid SPI Controller number!");
		return -EINVAL;
	}

	s3c_gpio_cfgall_range(base, 3,
			      S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);

	return 0;
}

static struct resource s5p64x0_spi0_resource[] = {
	[0] = {
		.start	= S5P64X0_PA_SPI0,
		.end	= S5P64X0_PA_SPI0 + 0x100 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= DMACH_SPI0_TX,
		.end	= DMACH_SPI0_TX,
		.flags	= IORESOURCE_DMA,
	},
	[2] = {
		.start	= DMACH_SPI0_RX,
		.end	= DMACH_SPI0_RX,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.start	= IRQ_SPI0,
		.end	= IRQ_SPI0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct s3c64xx_spi_info s5p6440_spi0_pdata = {
	.cfg_gpio	= s5p6440_spi_cfg_gpio,
	.fifo_lvl_mask	= 0x1ff,
	.rx_lvl_offset	= 15,
	.tx_st_done	= 25,
};

static struct s3c64xx_spi_info s5p6450_spi0_pdata = {
	.cfg_gpio	= s5p6450_spi_cfg_gpio,
	.fifo_lvl_mask	= 0x1ff,
	.rx_lvl_offset	= 15,
	.tx_st_done	= 25,
};

static u64 spi_dmamask = DMA_BIT_MASK(32);

struct platform_device s5p64x0_device_spi0 = {
	.name		= "s3c64xx-spi",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(s5p64x0_spi0_resource),
	.resource	= s5p64x0_spi0_resource,
	.dev = {
		.dma_mask		= &spi_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

static struct resource s5p64x0_spi1_resource[] = {
	[0] = {
		.start	= S5P64X0_PA_SPI1,
		.end	= S5P64X0_PA_SPI1 + 0x100 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= DMACH_SPI1_TX,
		.end	= DMACH_SPI1_TX,
		.flags	= IORESOURCE_DMA,
	},
	[2] = {
		.start	= DMACH_SPI1_RX,
		.end	= DMACH_SPI1_RX,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.start	= IRQ_SPI1,
		.end	= IRQ_SPI1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct s3c64xx_spi_info s5p6440_spi1_pdata = {
	.cfg_gpio	= s5p6440_spi_cfg_gpio,
	.fifo_lvl_mask	= 0x7f,
	.rx_lvl_offset	= 15,
	.tx_st_done	= 25,
};

static struct s3c64xx_spi_info s5p6450_spi1_pdata = {
	.cfg_gpio	= s5p6450_spi_cfg_gpio,
	.fifo_lvl_mask	= 0x7f,
	.rx_lvl_offset	= 15,
	.tx_st_done	= 25,
};

struct platform_device s5p64x0_device_spi1 = {
	.name		= "s3c64xx-spi",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(s5p64x0_spi1_resource),
	.resource	= s5p64x0_spi1_resource,
	.dev = {
		.dma_mask		= &spi_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

void __init s5p64x0_spi_set_info(int cntrlr, int src_clk_nr, int num_cs)
{
	struct s3c64xx_spi_info *pd;

	/* Reject invalid configuration */
	if (!num_cs || src_clk_nr < 0
			|| src_clk_nr > S5P64X0_SPI_SRCCLK_SCLK) {
		printk(KERN_ERR "%s: Invalid SPI configuration\n", __func__);
		return;
	}

	switch (cntrlr) {
	case 0:
		if (soc_is_s5p6450())
			pd = &s5p6450_spi0_pdata;
		else
			pd = &s5p6440_spi0_pdata;

		s5p64x0_device_spi0.dev.platform_data = pd;
		break;
	case 1:
		if (soc_is_s5p6450())
			pd = &s5p6450_spi1_pdata;
		else
			pd = &s5p6440_spi1_pdata;

		s5p64x0_device_spi1.dev.platform_data = pd;
		break;
	default:
		printk(KERN_ERR "%s: Invalid SPI controller(%d)\n",
							__func__, cntrlr);
		return;
	}

	pd->num_cs = num_cs;
	pd->src_clk_nr = src_clk_nr;
	pd->src_clk_name = s5p64x0_spi_src_clks[src_clk_nr];
}
