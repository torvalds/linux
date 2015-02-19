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
#include <linux/platform_data/gpio-omap.h>

#include <mach/irqs.h>

#define OMAP1_MPUIO_VBASE		OMAP1_MPUIO_BASE
#define OMAP1510_GPIO_BASE		0xFFFCE000

/* gpio1 */
static struct resource omap15xx_mpu_gpio_resources[] = {
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

static struct omap_gpio_reg_offs omap15xx_mpuio_regs = {
	.revision       = USHRT_MAX,
	.direction	= OMAP_MPUIO_IO_CNTL,
	.datain		= OMAP_MPUIO_INPUT_LATCH,
	.dataout	= OMAP_MPUIO_OUTPUT,
	.irqstatus	= OMAP_MPUIO_GPIO_INT,
	.irqenable	= OMAP_MPUIO_GPIO_MASKIT,
	.irqenable_inv	= true,
	.irqctrl	= OMAP_MPUIO_GPIO_INT_EDGE,
};

static struct omap_gpio_platform_data omap15xx_mpu_gpio_config = {
	.is_mpuio		= true,
	.bank_width		= 16,
	.bank_stride		= 1,
	.regs			= &omap15xx_mpuio_regs,
};

static struct platform_device omap15xx_mpu_gpio = {
	.name           = "omap_gpio",
	.id             = 0,
	.dev            = {
		.platform_data = &omap15xx_mpu_gpio_config,
	},
	.num_resources = ARRAY_SIZE(omap15xx_mpu_gpio_resources),
	.resource = omap15xx_mpu_gpio_resources,
};

/* gpio2 */
static struct resource omap15xx_gpio_resources[] = {
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

static struct omap_gpio_reg_offs omap15xx_gpio_regs = {
	.revision	= USHRT_MAX,
	.direction	= OMAP1510_GPIO_DIR_CONTROL,
	.datain		= OMAP1510_GPIO_DATA_INPUT,
	.dataout	= OMAP1510_GPIO_DATA_OUTPUT,
	.irqstatus	= OMAP1510_GPIO_INT_STATUS,
	.irqenable	= OMAP1510_GPIO_INT_MASK,
	.irqenable_inv	= true,
	.irqctrl	= OMAP1510_GPIO_INT_CONTROL,
	.pinctrl	= OMAP1510_GPIO_PIN_CONTROL,
};

static struct omap_gpio_platform_data omap15xx_gpio_config = {
	.bank_width		= 16,
	.regs                   = &omap15xx_gpio_regs,
};

static struct platform_device omap15xx_gpio = {
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

	return 0;
}
postcore_initcall(omap15xx_gpio_init);
