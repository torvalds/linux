/*
 * OMAP16xx specific gpio init
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - https://www.ti.com/
 *
 * Author:
 *	Charulatha V <charu@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/gpio.h>
#include <linux/platform_data/gpio-omap.h>

#include <mach/irqs.h>

#include "soc.h"

#define OMAP1610_GPIO1_BASE		0xfffbe400
#define OMAP1610_GPIO2_BASE		0xfffbec00
#define OMAP1610_GPIO3_BASE		0xfffbb400
#define OMAP1610_GPIO4_BASE		0xfffbbc00
#define OMAP1_MPUIO_VBASE		OMAP1_MPUIO_BASE

/* smart idle, enable wakeup */
#define SYSCONFIG_WORD			0x14

/* mpu gpio */
static struct resource omap16xx_mpu_gpio_resources[] = {
	{
		.start	= OMAP1_MPUIO_VBASE,
		.end	= OMAP1_MPUIO_VBASE + SZ_2K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_MPUIO,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct omap_gpio_reg_offs omap16xx_mpuio_regs = {
	.revision       = USHRT_MAX,
	.direction	= OMAP_MPUIO_IO_CNTL,
	.datain		= OMAP_MPUIO_INPUT_LATCH,
	.dataout	= OMAP_MPUIO_OUTPUT,
	.irqstatus	= OMAP_MPUIO_GPIO_INT,
	.irqenable	= OMAP_MPUIO_GPIO_MASKIT,
	.irqenable_inv	= true,
	.irqctrl	= OMAP_MPUIO_GPIO_INT_EDGE,
};

static struct omap_gpio_platform_data omap16xx_mpu_gpio_config = {
	.is_mpuio		= true,
	.bank_width		= 16,
	.bank_stride		= 1,
	.regs                   = &omap16xx_mpuio_regs,
};

static struct platform_device omap16xx_mpu_gpio = {
	.name           = "omap_gpio",
	.id             = 0,
	.dev            = {
		.platform_data = &omap16xx_mpu_gpio_config,
	},
	.num_resources = ARRAY_SIZE(omap16xx_mpu_gpio_resources),
	.resource = omap16xx_mpu_gpio_resources,
};

/* gpio1 */
static struct resource omap16xx_gpio1_resources[] = {
	{
		.start	= OMAP1610_GPIO1_BASE,
		.end	= OMAP1610_GPIO1_BASE + SZ_2K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_GPIO_BANK1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct omap_gpio_reg_offs omap16xx_gpio_regs = {
	.revision       = OMAP1610_GPIO_REVISION,
	.direction	= OMAP1610_GPIO_DIRECTION,
	.set_dataout	= OMAP1610_GPIO_SET_DATAOUT,
	.clr_dataout	= OMAP1610_GPIO_CLEAR_DATAOUT,
	.datain		= OMAP1610_GPIO_DATAIN,
	.dataout	= OMAP1610_GPIO_DATAOUT,
	.irqstatus	= OMAP1610_GPIO_IRQSTATUS1,
	.irqenable	= OMAP1610_GPIO_IRQENABLE1,
	.set_irqenable	= OMAP1610_GPIO_SET_IRQENABLE1,
	.clr_irqenable	= OMAP1610_GPIO_CLEAR_IRQENABLE1,
	.wkup_en	= OMAP1610_GPIO_WAKEUPENABLE,
	.edgectrl1	= OMAP1610_GPIO_EDGE_CTRL1,
	.edgectrl2	= OMAP1610_GPIO_EDGE_CTRL2,
};

static struct omap_gpio_platform_data omap16xx_gpio1_config = {
	.bank_width		= 16,
	.regs                   = &omap16xx_gpio_regs,
};

static struct platform_device omap16xx_gpio1 = {
	.name           = "omap_gpio",
	.id             = 1,
	.dev            = {
		.platform_data = &omap16xx_gpio1_config,
	},
	.num_resources = ARRAY_SIZE(omap16xx_gpio1_resources),
	.resource = omap16xx_gpio1_resources,
};

/* gpio2 */
static struct resource omap16xx_gpio2_resources[] = {
	{
		.start	= OMAP1610_GPIO2_BASE,
		.end	= OMAP1610_GPIO2_BASE + SZ_2K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_1610_GPIO_BANK2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct omap_gpio_platform_data omap16xx_gpio2_config = {
	.bank_width		= 16,
	.regs                   = &omap16xx_gpio_regs,
};

static struct platform_device omap16xx_gpio2 = {
	.name           = "omap_gpio",
	.id             = 2,
	.dev            = {
		.platform_data = &omap16xx_gpio2_config,
	},
	.num_resources = ARRAY_SIZE(omap16xx_gpio2_resources),
	.resource = omap16xx_gpio2_resources,
};

/* gpio3 */
static struct resource omap16xx_gpio3_resources[] = {
	{
		.start	= OMAP1610_GPIO3_BASE,
		.end	= OMAP1610_GPIO3_BASE + SZ_2K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_1610_GPIO_BANK3,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct omap_gpio_platform_data omap16xx_gpio3_config = {
	.bank_width		= 16,
	.regs                   = &omap16xx_gpio_regs,
};

static struct platform_device omap16xx_gpio3 = {
	.name           = "omap_gpio",
	.id             = 3,
	.dev            = {
		.platform_data = &omap16xx_gpio3_config,
	},
	.num_resources = ARRAY_SIZE(omap16xx_gpio3_resources),
	.resource = omap16xx_gpio3_resources,
};

/* gpio4 */
static struct resource omap16xx_gpio4_resources[] = {
	{
		.start	= OMAP1610_GPIO4_BASE,
		.end	= OMAP1610_GPIO4_BASE + SZ_2K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_1610_GPIO_BANK4,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct omap_gpio_platform_data omap16xx_gpio4_config = {
	.bank_width		= 16,
	.regs                   = &omap16xx_gpio_regs,
};

static struct platform_device omap16xx_gpio4 = {
	.name           = "omap_gpio",
	.id             = 4,
	.dev            = {
		.platform_data = &omap16xx_gpio4_config,
	},
	.num_resources = ARRAY_SIZE(omap16xx_gpio4_resources),
	.resource = omap16xx_gpio4_resources,
};

static struct platform_device *omap16xx_gpio_dev[] __initdata = {
	&omap16xx_mpu_gpio,
	&omap16xx_gpio1,
	&omap16xx_gpio2,
	&omap16xx_gpio3,
	&omap16xx_gpio4,
};

/*
 * omap16xx_gpio_init needs to be done before
 * machine_init functions access gpio APIs.
 * Hence omap16xx_gpio_init is a postcore_initcall.
 */
static int __init omap16xx_gpio_init(void)
{
	int i;
	void __iomem *base;
	struct resource *res;
	struct platform_device *pdev;
	struct omap_gpio_platform_data *pdata;

	if (!cpu_is_omap16xx())
		return -EINVAL;

	/*
	 * Enable system clock for GPIO module.
	 * The CAM_CLK_CTRL *is* really the right place.
	 */
	omap_writel(omap_readl(ULPD_CAM_CLK_CTRL) | 0x04,
					ULPD_CAM_CLK_CTRL);

	for (i = 0; i < ARRAY_SIZE(omap16xx_gpio_dev); i++) {
		pdev = omap16xx_gpio_dev[i];
		pdata = pdev->dev.platform_data;

		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (unlikely(!res)) {
			dev_err(&pdev->dev, "Invalid mem resource.\n");
			return -ENODEV;
		}

		base = ioremap(res->start, resource_size(res));
		if (unlikely(!base)) {
			dev_err(&pdev->dev, "ioremap failed.\n");
			return -ENOMEM;
		}

		__raw_writel(SYSCONFIG_WORD, base + OMAP1610_GPIO_SYSCONFIG);
		iounmap(base);

		platform_device_register(omap16xx_gpio_dev[i]);
	}

	return 0;
}
postcore_initcall(omap16xx_gpio_init);
