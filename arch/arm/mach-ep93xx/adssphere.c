// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * arch/arm/mach-ep93xx/adssphere.c
 * ADS Sphere support.
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

static struct ep93xx_eth_data __initdata adssphere_eth_data = {
	.phy_id		= 1,
};

static void __init adssphere_init_machine(void)
{
	ep93xx_init_devices();
	ep93xx_register_flash(4, EP93XX_CS6_PHYS_BASE, SZ_32M);
	ep93xx_register_eth(&adssphere_eth_data, 1);
}

MACHINE_START(ADSSPHERE, "ADS Sphere board")
	/* Maintainer: Lennert Buytenhek <buytenh@wantstofly.org> */
	.atag_offset	= 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.init_time	= ep93xx_timer_init,
	.init_machine	= adssphere_init_machine,
	.init_late	= ep93xx_init_late,
	.restart	= ep93xx_restart,
MACHINE_END
