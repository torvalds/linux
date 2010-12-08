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

#define OMAP7XX_GPIO1_BASE		0xfffbc000
#define OMAP7XX_GPIO2_BASE		0xfffbc800
#define OMAP7XX_GPIO3_BASE		0xfffbd000
#define OMAP7XX_GPIO4_BASE		0xfffbd800
#define OMAP7XX_GPIO5_BASE		0xfffbe000
#define OMAP7XX_GPIO6_BASE		0xfffbe800
#define OMAP1_MPUIO_VBASE		OMAP1_MPUIO_BASE

/* mpu gpio */
static struct __initdata resource omap7xx_mpu_gpio_resources[] = {
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

static struct __initdata omap_gpio_platform_data omap7xx_mpu_gpio_config = {
	.virtual_irq_start	= IH_MPUIO_BASE,
	.bank_type		= METHOD_MPUIO,
	.bank_width		= 32,
};

static struct __initdata platform_device omap7xx_mpu_gpio = {
	.name           = "omap_gpio",
	.id             = 0,
	.dev            = {
		.platform_data = &omap7xx_mpu_gpio_config,
	},
	.num_resources = ARRAY_SIZE(omap7xx_mpu_gpio_resources),
	.resource = omap7xx_mpu_gpio_resources,
};

/* gpio1 */
static struct __initdata resource omap7xx_gpio1_resources[] = {
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

static struct __initdata omap_gpio_platform_data omap7xx_gpio1_config = {
	.virtual_irq_start	= IH_GPIO_BASE,
	.bank_type		= METHOD_GPIO_7XX,
	.bank_width		= 32,
};

static struct __initdata platform_device omap7xx_gpio1 = {
	.name           = "omap_gpio",
	.id             = 1,
	.dev            = {
		.platform_data = &omap7xx_gpio1_config,
	},
	.num_resources = ARRAY_SIZE(omap7xx_gpio1_resources),
	.resource = omap7xx_gpio1_resources,
};

/* gpio2 */
static struct __initdata resource omap7xx_gpio2_resources[] = {
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

static struct __initdata omap_gpio_platform_data omap7xx_gpio2_config = {
	.virtual_irq_start	= IH_GPIO_BASE + 32,
	.bank_type		= METHOD_GPIO_7XX,
	.bank_width		= 32,
};

static struct __initdata platform_device omap7xx_gpio2 = {
	.name           = "omap_gpio",
	.id             = 2,
	.dev            = {
		.platform_data = &omap7xx_gpio2_config,
	},
	.num_resources = ARRAY_SIZE(omap7xx_gpio2_resources),
	.resource = omap7xx_gpio2_resources,
};

/* gpio3 */
static struct __initdata resource omap7xx_gpio3_resources[] = {
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

static struct __initdata omap_gpio_platform_data omap7xx_gpio3_config = {
	.virtual_irq_start	= IH_GPIO_BASE + 64,
	.bank_type		= METHOD_GPIO_7XX,
	.bank_width		= 32,
};

static struct __initdata platform_device omap7xx_gpio3 = {
	.name           = "omap_gpio",
	.id             = 3,
	.dev            = {
		.platform_data = &omap7xx_gpio3_config,
	},
	.num_resources = ARRAY_SIZE(omap7xx_gpio3_resources),
	.resource = omap7xx_gpio3_resources,
};

/* gpio4 */
static struct __initdata resource omap7xx_gpio4_resources[] = {
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

static struct __initdata omap_gpio_platform_data omap7xx_gpio4_config = {
	.virtual_irq_start	= IH_GPIO_BASE + 96,
	.bank_type		= METHOD_GPIO_7XX,
	.bank_width		= 32,
};

static struct __initdata platform_device omap7xx_gpio4 = {
	.name           = "omap_gpio",
	.id             = 4,
	.dev            = {
		.platform_data = &omap7xx_gpio4_config,
	},
	.num_resources = ARRAY_SIZE(omap7xx_gpio4_resources),
	.resource = omap7xx_gpio4_resources,
};

/* gpio5 */
static struct __initdata resource omap7xx_gpio5_resources[] = {
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

static struct __initdata omap_gpio_platform_data omap7xx_gpio5_config = {
	.virtual_irq_start	= IH_GPIO_BASE + 128,
	.bank_type		= METHOD_GPIO_7XX,
	.bank_width		= 32,
};

static struct __initdata platform_device omap7xx_gpio5 = {
	.name           = "omap_gpio",
	.id             = 5,
	.dev            = {
		.platform_data = &omap7xx_gpio5_config,
	},
	.num_resources = ARRAY_SIZE(omap7xx_gpio5_resources),
	.resource = omap7xx_gpio5_resources,
};

/* gpio6 */
static struct __initdata resource omap7xx_gpio6_resources[] = {
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

static struct __initdata omap_gpio_platform_data omap7xx_gpio6_config = {
	.virtual_irq_start	= IH_GPIO_BASE + 160,
	.bank_type		= METHOD_GPIO_7XX,
	.bank_width		= 32,
};

static struct __initdata platform_device omap7xx_gpio6 = {
	.name           = "omap_gpio",
	.id             = 6,
	.dev            = {
		.platform_data = &omap7xx_gpio6_config,
	},
	.num_resources = ARRAY_SIZE(omap7xx_gpio6_resources),
	.resource = omap7xx_gpio6_resources,
};

static struct __initdata platform_device * omap7xx_gpio_dev[] = {
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

	gpio_bank_count = ARRAY_SIZE(omap7xx_gpio_dev);

	return 0;
}
postcore_initcall(omap7xx_gpio_init);
