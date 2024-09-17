// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * arch/arm/mach-ep93xx/gesbc9312.c
 * Glomation GESBC-9312-sx support.
 *
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>

#include "hardware.h"

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "soc.h"

static struct ep93xx_eth_data __initdata gesbc9312_eth_data = {
	.phy_id		= 1,
};

static void __init gesbc9312_init_machine(void)
{
	ep93xx_init_devices();
	ep93xx_register_flash(4, EP93XX_CS6_PHYS_BASE, SZ_8M);
	ep93xx_register_eth(&gesbc9312_eth_data, 0);
}

MACHINE_START(GESBC9312, "Glomation GESBC-9312-sx")
	/* Maintainer: Lennert Buytenhek <buytenh@wantstofly.org> */
	.atag_offset	= 0x100,
	.nr_irqs	= NR_EP93XX_IRQS,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.init_time	= ep93xx_timer_init,
	.init_machine	= gesbc9312_init_machine,
	.restart	= ep93xx_restart,
MACHINE_END
