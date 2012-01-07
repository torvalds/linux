/*
 * linux/arch/arm/mach-h720x/h7202-eval.c
 *
 * Copyright (C) 2003 Thomas Gleixner <tglx@linutronix.de>
 *               2003 Robert Schwebel <r.schwebel@pengutronix.de>
 *		 2004 Sascha Hauer <s.hauer@pengutronix.de>
 *
 * Architecture specific stuff for Hynix HMS30C7202 development board
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/platform_device.h>

#include <asm/setup.h>
#include <asm/types.h>
#include <asm/mach-types.h>
#include <asm/page.h>
#include <asm/mach/arch.h>
#include <mach/irqs.h>
#include <mach/hardware.h>
#include "common.h"

static struct resource cirrus_resources[] = {
	[0] = {
		.start	= ETH0_PHYS + 0x300,
		.end	= ETH0_PHYS + 0x300 + 0x10,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_CHAINED_GPIOB(8),
		.end	= IRQ_CHAINED_GPIOB(8),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device cirrus_device = {
	.name		= "cirrus-cs89x0",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(cirrus_resources),
	.resource	= cirrus_resources,
};

static struct platform_device *devices[] __initdata = {
	&cirrus_device,
};

/*
 * Hardware init. This is called early in initcalls
 * Place pin inits here. So you avoid adding ugly
 * #ifdef stuff to common drivers.
 * Use this only, if your bootloader is not able
 * to initialize the pins proper.
 */
static void __init init_eval_h7202(void)
{
	init_hw_h7202();
	(void) platform_add_devices(devices, ARRAY_SIZE(devices));

	/* Enable interrupt on portb bit 8 (ethernet) */
	CPU_REG (GPIO_B_VIRT, GPIO_POL) &= ~(1 << 8);
	CPU_REG (GPIO_B_VIRT, GPIO_EN) |= (1 << 8);
}

MACHINE_START(H7202, "Hynix HMS30C7202")
	/* Maintainer: Robert Schwebel, Pengutronix */
	.atag_offset	= 0x100,
	.map_io		= h720x_map_io,
	.init_irq	= h7202_init_irq,
	.timer		= &h7202_timer,
	.init_machine	= init_eval_h7202,
	.dma_zone_size	= SZ_256M,
	.restart	= h720x_restart,
MACHINE_END
