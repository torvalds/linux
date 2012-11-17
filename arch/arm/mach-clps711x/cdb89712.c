/*
 *  linux/arch/arm/mach-clps711x/cdb89712.c
 *
 *  Copyright (C) 2000-2001 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <mach/hardware.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"

#define CDB89712_CS8900_BASE	(CS2_PHYS_BASE + 0x300)
#define CDB89712_CS8900_IRQ	(IRQ_EINT3)

static struct resource cdb89712_cs8900_resource[] __initdata = {
	DEFINE_RES_MEM(CDB89712_CS8900_BASE, SZ_1K),
	DEFINE_RES_IRQ(CDB89712_CS8900_IRQ),
};

static void __init cdb89712_init(void)
{
	platform_device_register_simple("cs89x0", 0, cdb89712_cs8900_resource,
					ARRAY_SIZE(cdb89712_cs8900_resource));
}

MACHINE_START(CDB89712, "Cirrus-CDB89712")
	/* Maintainer: Ray Lehtiniemi */
	.atag_offset	= 0x100,
	.map_io		= clps711x_map_io,
	.init_irq	= clps711x_init_irq,
	.timer		= &clps711x_timer,
	.init_machine	= cdb89712_init,
	.restart	= clps711x_restart,
MACHINE_END
