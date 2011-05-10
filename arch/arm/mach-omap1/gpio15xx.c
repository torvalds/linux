/*
 * OMAP15xx specific gpio init
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

#define OMAP1_MPUIO_VBASE		OMAP1_MPUIO_BASE
#define OMAP1510_GPIO_BASE		0xFFFCE000

/* gpio1 */
static struct __initdata resource omap15xx_mpu_gpio_resources[] = {
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

static struct __initdata omap_gpio_platform_data omap15xx_mpu_gpio_config = {
	.virtual_irq_start	= IH_MPUIO_BASE,
	.bank_type		= METHOD_MPUIO,
	.bank_width		= 16,
	.bank_stride		= 1,
};

static struct __initdata platform_device omap15xx_mpu_gpio = {
	.name           = "omap_gpio",
	.id             = 0,
	.dev            = {
		.platform_data = &omap15xx_mpu_gpio_config,
	},
	.num_resources = ARRAY_SIZE(omap15xx_mpu_gpio_resources),
	.resource = omap15xx_mpu_gpio_resources,
};

/* gpio2 */
static struct __initdata resource omap15xx_gpio_resources[] = {
	{
		.start	= OMAP1510_GPIO_BASE,
		.end	= OMAP1510_GPIO_BASE + SZ_2K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_GPIO_BANK1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct __initdata omap_gpio_platform_data omap15xx_gpio_config = {
	.virtual_irq_start	= IH_GPIO_BASE,
	.bank_type		= METHOD_GPIO_1510,
	.bank_width		= 16,
};

static struct __initdata platform_device omap15xx_gpio = {
	.name           = "omap_gpio",
	.id             = 1,
	.dev            = {
		.platform_data = &omap15xx_gpio_config,
	},
	.num_resources = ARRAY_SIZE(omap15xx_gpio_resources),
	.resource = omap15xx_gpio_resources,
};

/*
 * omap15xx_gpio_init needs to be done before
 * machine_init functions access gpio APIs.
 * Hence omap15xx_gpio_init is a postcore_initcall.
 */
static int __init omap15xx_gpio_init(void)
{
	if (!cpu_is_omap15xx())
		return -EINVAL;

	platform_device_register(&omap15xx_mpu_gpio);
	platform_device_register(&omap15xx_gpio);

	gpio_bank_count = 2;
	return 0;
}
postcore_initcall(omap15xx_gpio_init);
