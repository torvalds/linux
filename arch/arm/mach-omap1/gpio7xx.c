/*
 * OMAP7xx specific gpio init
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/
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

#define OMAP7XX_GPIO1_BASE		0xfffbc000
#define OMAP7XX_GPIO2_BASE		0xfffbc800
#define OMAP7XX_GPIO3_BASE		0xfffbd000
#define OMAP7XX_GPIO4_BASE		0xfffbd800
#define OMAP7XX_GPIO5_BASE		0xfffbe000
#define OMAP7XX_GPIO6_BASE		0xfffbe800
#define OMAP1_MPUIO_VBASE		OMAP1_MPUIO_BASE

/* mpu gpio */
static struct resource omap7xx_mpu_gpio_resources[] = {
	{
		.start	= OMAP1_MPUIO_VBASE,
		.end	= OMAP1_MPUIO_VBASE + SZ_2K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_7XX_MPUIO,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct omap_gpio_reg_offs omap7xx_mpuio_regs = {
	.revision	= USHRT_MAX,
	.direction	= OMAP_MPUIO_IO_CNTL / 2,
	.datain		= OMAP_MPUIO_INPUT_LATCH / 2,
	.dataout	= OMAP_MPUIO_OUTPUT / 2,
	.irqstatus	= OMAP_MPUIO_GPIO_INT / 2,
	.irqenable	= OMAP_MPUIO_GPIO_MASKIT / 2,
	.irqenable_inv	= true,
	.irqctrl	= OMAP_MPUIO_GPIO_INT_EDGE >> 1,
};

static struct omap_gpio_platform_data omap7xx_mpu_gpio_config = {
	.is_mpuio		= true,
	.bank_width		= 16,
	.bank_stride		= 2,
	.regs                   = &omap7xx_mpuio_regs,
};

static struct platform_device omap7xx_mpu_gpio = {
	.name           = "omap_gpio",
	.id             = 0,
	.dev            = {
		.platform_data = &omap7xx_mpu_gpio_config,
	},
	.num_resources = ARRAY_SIZE(omap7xx_mpu_gpio_resources),
	.resource = omap7xx_mpu_gpio_resources,
};

/* gpio1 */
static struct resource omap7xx_gpio1_resources[] = {
	{
		.start	= OMAP7XX_GPIO1_BASE,
		.end	= OMAP7XX_GPIO1_BASE + SZ_2K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_7XX_GPIO_BANK1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct omap_gpio_reg_offs omap7xx_gpio_regs = {
	.revision	= USHRT_MAX,
	.direction	= OMAP7XX_GPIO_DIR_CONTROL,
	.datain		= OMAP7XX_GPIO_DATA_INPUT,
	.dataout	= OMAP7XX_GPIO_DATA_OUTPUT,
	.irqstatus	= OMAP7XX_GPIO_INT_STATUS,
	.irqenable	= OMAP7XX_GPIO_INT_MASK,
	.irqenable_inv	= true,
	.irqctrl	= OMAP7XX_GPIO_INT_CONTROL,
};

static struct omap_gpio_platform_data omap7xx_gpio1_config = {
	.bank_width		= 32,
	.regs			= &omap7xx_gpio_regs,
};

static struct platform_device omap7xx_gpio1 = {
	.name           = "omap_gpio",
	.id             = 1,
	.dev            = {
		.platform_data = &omap7xx_gpio1_config,
	},
	.num_resources = ARRAY_SIZE(omap7xx_gpio1_resources),
	.resource = omap7xx_gpio1_resources,
};

/* gpio2 */
static struct resource omap7xx_gpio2_resources[] = {
	{
		.start	= OMAP7XX_GPIO2_BASE,
		.end	= OMAP7XX_GPIO2_BASE + SZ_2K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_7XX_GPIO_BANK2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct omap_gpio_platform_data omap7xx_gpio2_config = {
	.bank_width		= 32,
	.regs			= &omap7xx_gpio_regs,
};

static struct platform_device omap7xx_gpio2 = {
	.name           = "omap_gpio",
	.id             = 2,
	.dev            = {
		.platform_data = &omap7xx_gpio2_config,
	},
	.num_resources = ARRAY_SIZE(omap7xx_gpio2_resources),
	.resource = omap7xx_gpio2_resources,
};

/* gpio3 */
static struct resource omap7xx_gpio3_resources[] = {
	{
		.start	= OMAP7XX_GPIO3_BASE,
		.end	= OMAP7XX_GPIO3_BASE + SZ_2K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_7XX_GPIO_BANK3,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct omap_gpio_platform_data omap7xx_gpio3_config = {
	.bank_width		= 32,
	.regs			= &omap7xx_gpio_regs,
};

static struct platform_device omap7xx_gpio3 = {
	.name           = "omap_gpio",
	.id             = 3,
	.dev            = {
		.platform_data = &omap7xx_gpio3_config,
	},
	.num_resources = ARRAY_SIZE(omap7xx_gpio3_resources),
	.resource = omap7xx_gpio3_resources,
};

/* gpio4 */
static struct resource omap7xx_gpio4_resources[] = {
	{
		.start	= OMAP7XX_GPIO4_BASE,
		.end	= OMAP7XX_GPIO4_BASE + SZ_2K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_7XX_GPIO_BANK4,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct omap_gpio_platform_data omap7xx_gpio4_config = {
	.bank_width		= 32,
	.regs			= &omap7xx_gpio_regs,
};

static struct platform_device omap7xx_gpio4 = {
	.name           = "omap_gpio",
	.id             = 4,
	.dev            = {
		.platform_data = &omap7xx_gpio4_config,
	},
	.num_resources = ARRAY_SIZE(omap7xx_gpio4_resources),
	.resource = omap7xx_gpio4_resources,
};

/* gpio5 */
static struct resource omap7xx_gpio5_resources[] = {
	{
		.start	= OMAP7XX_GPIO5_BASE,
		.end	= OMAP7XX_GPIO5_BASE + SZ_2K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_7XX_GPIO_BANK5,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct omap_gpio_platform_data omap7xx_gpio5_config = {
	.bank_width		= 32,
	.regs			= &omap7xx_gpio_regs,
};

static struct platform_device omap7xx_gpio5 = {
	.name           = "omap_gpio",
	.id             = 5,
	.dev            = {
		.platform_data = &omap7xx_gpio5_config,
	},
	.num_resources = ARRAY_SIZE(omap7xx_gpio5_resources),
	.resource = omap7xx_gpio5_resources,
};

/* gpio6 */
static struct resource omap7xx_gpio6_resources[] = {
	{
		.start	= OMAP7XX_GPIO6_BASE,
		.end	= OMAP7XX_GPIO6_BASE + SZ_2K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_7XX_GPIO_BANK6,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct omap_gpio_platform_data omap7xx_gpio6_config = {
	.bank_width		= 32,
	.regs			= &omap7xx_gpio_regs,
};

static struct platform_device omap7xx_gpio6 = {
	.name           = "omap_gpio",
	.id             = 6,
	.dev            = {
		.platform_data = &omap7xx_gpio6_config,
	},
	.num_resources = ARRAY_SIZE(omap7xx_gpio6_resources),
	.resource = omap7xx_gpio6_resources,
};

static struct platform_device *omap7xx_gpio_dev[] __initdata = {
	&omap7xx_mpu_gpio,
	&omap7xx_gpio1,
	&omap7xx_gpio2,
	&omap7xx_gpio3,
	&omap7xx_gpio4,
	&omap7xx_gpio5,
	&omap7xx_gpio6,
};

/*
 * omap7xx_gpio_init needs to be done before
 * machine_init functions access gpio APIs.
 * Hence omap7xx_gpio_init is a postcore_initcall.
 */
static int __init omap7xx_gpio_init(void)
{
	int i;

	if (!cpu_is_omap7xx())
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(omap7xx_gpio_dev); i++)
		platform_device_register(omap7xx_gpio_dev[i]);

	return 0;
}
postcore_initcall(omap7xx_gpio_init);
