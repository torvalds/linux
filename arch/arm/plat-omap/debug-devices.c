/*
 * linux/arch/arm/plat-omap/debug-devices.c
 *
 * Copyright (C) 2005 Nokia Corporation
 * Modified from mach-omap2/board-h4.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <asm/hardware.h>
#include <asm/io.h>

#include <asm/arch/board.h>
#include <asm/arch/gpio.h>


/* Many OMAP development platforms reuse the same "debug board"; these
 * platforms include H2, H3, H4, and Perseus2.
 */

static struct resource smc91x_resources[] = {
	[0] = {
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.flags  = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWEDGE,
	},
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

static struct resource led_resources[] = {
	[0] = {
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device led_device = {
	.name		= "omap_dbg_led",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(led_resources),
	.resource	= led_resources,
};

static struct platform_device *debug_devices[] __initdata = {
	&smc91x_device,
	&led_device,
	/* ps2 kbd + mouse ports */
	/* 4 extra uarts */
	/* 6 input dip switches */
	/* 8 output pins */
};

int __init debug_card_init(u32 addr, unsigned gpio)
{
	int	status;

	smc91x_resources[0].start = addr + 0x300;
	smc91x_resources[0].end   = addr + 0x30f;

	smc91x_resources[1].start = OMAP_GPIO_IRQ(gpio);
	smc91x_resources[1].end   = OMAP_GPIO_IRQ(gpio);

	status = omap_request_gpio(gpio);
	if (status < 0) {
		printk(KERN_ERR "GPIO%d unavailable for smc91x IRQ\n", gpio);
		return status;
	}
	omap_set_gpio_direction(gpio, 1);

	led_resources[0].start = addr;
	led_resources[0].end   = addr + SZ_4K - 1;

	return platform_add_devices(debug_devices, ARRAY_SIZE(debug_devices));
}
