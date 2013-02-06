/* linux/arch/arm/mach-exynos/dev-spi.c
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
#include <linux/clk.h>

#include <mach/irqs.h>
#include <mach/dma.h>
#include <mach/map.h>
#include <mach/gpio.h>
#include <mach/spi-clocks.h>
#include <mach/regs-clock.h>

#include <plat/s3c64xx-spi.h>
#include <plat/gpio-cfg.h>
#include <plat/irqs.h>

static char *spi_src_clks[] = {
	[EXYNOS_SPI_SRCCLK_SCLK] = "sclk_spi",
};

/* SPI Controller platform_devices */

/* Since we emulate multi-cs capability, we do not touch the CS.
 * The emulated CS is toggled by board specific mechanism, as it can
 * be either some immediate GPIO or some signal out of some other
 * chip in between ... or some yet another way.
 * We simply do not assume anything about CS.
 */
#if defined(CONFIG_ARCH_EXYNOS5)
static int exynos_spi_cfg_gpio(struct platform_device *pdev)
{
	int gpio;

	switch (pdev->id) {
	case 0:
		s3c_gpio_cfgpin(EXYNOS5_GPA2(0), S3C_GPIO_SFN(2));
		s3c_gpio_cfgpin(EXYNOS5_GPA2(2), S3C_GPIO_SFN(2));
		s3c_gpio_cfgpin(EXYNOS5_GPA2(3), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(EXYNOS5_GPA2(0), S3C_GPIO_PULL_UP);
		s3c_gpio_setpull(EXYNOS5_GPA2(2), S3C_GPIO_PULL_UP);
		s3c_gpio_setpull(EXYNOS5_GPA2(3), S3C_GPIO_PULL_UP);

		for (gpio = EXYNOS5_GPA2(0); gpio < EXYNOS5_GPA2(4); gpio++)
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV3);

		break;

	case 1:
		s3c_gpio_cfgpin(EXYNOS5_GPA2(4), S3C_GPIO_SFN(2));
		s3c_gpio_cfgpin(EXYNOS5_GPA2(6), S3C_GPIO_SFN(2));
		s3c_gpio_cfgpin(EXYNOS5_GPA2(7), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(EXYNOS5_GPA2(4), S3C_GPIO_PULL_UP);
		s3c_gpio_setpull(EXYNOS5_GPA2(6), S3C_GPIO_PULL_UP);
		s3c_gpio_setpull(EXYNOS5_GPA2(7), S3C_GPIO_PULL_UP);

		for (gpio = EXYNOS5_GPA2(4); gpio < EXYNOS5_GPA2(8); gpio++)
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV3);

		break;

	case 2:
		s3c_gpio_cfgpin(EXYNOS5_GPB1(1), S3C_GPIO_SFN(5));
		s3c_gpio_cfgpin(EXYNOS5_GPB1(3), S3C_GPIO_SFN(5));
		s3c_gpio_cfgpin(EXYNOS5_GPB1(4), S3C_GPIO_SFN(5));
		s3c_gpio_setpull(EXYNOS5_GPB1(1), S3C_GPIO_PULL_UP);
		s3c_gpio_setpull(EXYNOS5_GPB1(3), S3C_GPIO_PULL_UP);
		s3c_gpio_setpull(EXYNOS5_GPB1(4), S3C_GPIO_PULL_UP);

		for (gpio = EXYNOS5_GPB1(1); gpio < EXYNOS5_GPB1(5); gpio++)
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV3);

		break;

	default:
		dev_err(&pdev->dev, "Invalid SPI Controller number!");
		return -EINVAL;
	}

	return 0;
}
#else
static int exynos_spi_cfg_gpio(struct platform_device *pdev)
{
	int gpio;

	switch (pdev->id) {
	case 0:
		s3c_gpio_cfgpin(EXYNOS4_GPB(0), S3C_GPIO_SFN(2));
		s3c_gpio_cfgpin(EXYNOS4_GPB(2), S3C_GPIO_SFN(2));
#ifdef CONFIG_SEC_MODEM_P8LTE
		s3c_gpio_cfgpin(EXYNOS4_GPB(1), S3C_GPIO_SFN(2));
#endif
		s3c_gpio_cfgpin(EXYNOS4_GPB(3), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(EXYNOS4_GPB(0), S3C_GPIO_PULL_UP);
		s3c_gpio_setpull(EXYNOS4_GPB(2), S3C_GPIO_PULL_UP);
		s3c_gpio_setpull(EXYNOS4_GPB(3), S3C_GPIO_PULL_UP);

		for (gpio = EXYNOS4_GPB(0); gpio < EXYNOS4_GPB(4); gpio++)
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV3);

		break;

	case 1:
		s3c_gpio_cfgpin(EXYNOS4_GPB(4), S3C_GPIO_SFN(2));
		s3c_gpio_cfgpin(EXYNOS4_GPB(6), S3C_GPIO_SFN(2));
		s3c_gpio_cfgpin(EXYNOS4_GPB(7), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(EXYNOS4_GPB(4), S3C_GPIO_PULL_UP);
		s3c_gpio_setpull(EXYNOS4_GPB(6), S3C_GPIO_PULL_UP);
		s3c_gpio_setpull(EXYNOS4_GPB(7), S3C_GPIO_PULL_UP);

		for (gpio = EXYNOS4_GPB(4); gpio < EXYNOS4_GPB(8); gpio++)
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV3);

		break;

	case 2:
		s3c_gpio_cfgpin(EXYNOS4_GPC1(1), S3C_GPIO_SFN(5));
		s3c_gpio_cfgpin(EXYNOS4_GPC1(3), S3C_GPIO_SFN(5));
		s3c_gpio_cfgpin(EXYNOS4_GPC1(4), S3C_GPIO_SFN(5));
		s3c_gpio_setpull(EXYNOS4_GPC1(1), S3C_GPIO_PULL_UP);
		s3c_gpio_setpull(EXYNOS4_GPC1(3), S3C_GPIO_PULL_UP);
		s3c_gpio_setpull(EXYNOS4_GPC1(4), S3C_GPIO_PULL_UP);

		for (gpio = EXYNOS4_GPC1(1); gpio < EXYNOS4_GPC1(5); gpio++)
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV3);

		break;

	default:
		dev_err(&pdev->dev, "Invalid SPI Controller number!");
		return -EINVAL;
	}

	return 0;
}
#endif

static struct resource exynos_spi0_resource[] = {
	[0] = {
		.start = EXYNOS_PA_SPI0,
		.end   = EXYNOS_PA_SPI0 + 0x100 - 1,
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

static struct s3c64xx_spi_info exynos_spi0_pdata = {
	.cfg_gpio = exynos_spi_cfg_gpio,
	.fifo_lvl_mask = 0x1ff,
	.rx_lvl_offset = 15,
	.high_speed = 1,
	.clk_from_cmu = true,
	.tx_st_done = 25,
};

static u64 spi_dmamask = DMA_BIT_MASK(32);

struct platform_device exynos_device_spi0 = {
	.name		  = "s3c64xx-spi",
	.id		  = 0,
	.num_resources	  = ARRAY_SIZE(exynos_spi0_resource),
	.resource	  = exynos_spi0_resource,
	.dev = {
		.dma_mask		= &spi_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data = &exynos_spi0_pdata,
	},
};

static struct resource exynos_spi1_resource[] = {
	[0] = {
		.start = EXYNOS_PA_SPI1,
		.end   = EXYNOS_PA_SPI1 + 0x100 - 1,
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

static struct s3c64xx_spi_info exynos_spi1_pdata = {
	.cfg_gpio = exynos_spi_cfg_gpio,
	.fifo_lvl_mask = 0x7f,
	.rx_lvl_offset = 15,
	.high_speed = 1,
	.clk_from_cmu = true,
	.tx_st_done = 25,
};

struct platform_device exynos_device_spi1 = {
	.name		  = "s3c64xx-spi",
	.id		  = 1,
	.num_resources	  = ARRAY_SIZE(exynos_spi1_resource),
	.resource	  = exynos_spi1_resource,
	.dev = {
		.dma_mask		= &spi_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data = &exynos_spi1_pdata,
	},
};

static struct resource exynos_spi2_resource[] = {
	[0] = {
		.start = EXYNOS_PA_SPI2,
		.end   = EXYNOS_PA_SPI2 + 0x100 - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = DMACH_SPI2_TX,
		.end   = DMACH_SPI2_TX,
		.flags = IORESOURCE_DMA,
	},
	[2] = {
		.start = DMACH_SPI2_RX,
		.end   = DMACH_SPI2_RX,
		.flags = IORESOURCE_DMA,
	},
	[3] = {
		.start = IRQ_SPI2,
		.end   = IRQ_SPI2,
		.flags = IORESOURCE_IRQ,
	},
};

static struct s3c64xx_spi_info exynos_spi2_pdata = {
	.cfg_gpio = exynos_spi_cfg_gpio,
	.fifo_lvl_mask = 0x7f,
	.rx_lvl_offset = 15,
	.high_speed = 1,
	.clk_from_cmu = true,
	.tx_st_done = 25,
};

struct platform_device exynos_device_spi2 = {
	.name		  = "s3c64xx-spi",
	.id		  = 2,
	.num_resources	  = ARRAY_SIZE(exynos_spi2_resource),
	.resource	  = exynos_spi2_resource,
	.dev = {
		.dma_mask		= &spi_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data = &exynos_spi2_pdata,
	},
};

void __init exynos_spi_set_info(int cntrlr, int src_clk_nr, int num_cs)
{
	struct s3c64xx_spi_info *pd;

	/* Reject invalid configuration */
	if (!num_cs || src_clk_nr < 0
			|| src_clk_nr > EXYNOS_SPI_SRCCLK_SCLK) {
		printk(KERN_ERR "%s: Invalid SPI configuration\n", __func__);
		return;
	}

	switch (cntrlr) {
	case 0:
		pd = &exynos_spi0_pdata;
		break;
	case 1:
		pd = &exynos_spi1_pdata;
		break;
	case 2:
		pd = &exynos_spi2_pdata;
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
