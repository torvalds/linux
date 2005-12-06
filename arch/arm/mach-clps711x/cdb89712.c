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

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"

/*
 * Map the CS89712 Ethernet port.  That should be moved to the
 * ethernet driver, perhaps.
 */
static struct map_desc cdb89712_io_desc[] __initdata = {
	{
		.virtual	= ETHER_BASE,
		.pfn		=__phys_to_pfn(ETHER_START),
		.length		= ETHER_SIZE,
		.type		= MT_DEVICE
	}
};

static void __init cdb89712_map_io(void)
{
	clps711x_map_io();
	iotable_init(cdb89712_io_desc, ARRAY_SIZE(cdb89712_io_desc));
}

MACHINE_START(CDB89712, "Cirrus-CDB89712")
	/* Maintainer: Ray Lehtiniemi */
	.phys_ram	= 0xc0000000,
	.phys_io	= 0x80000000,
	.io_pg_offst	= ((0xff000000) >> 18) & 0xfffc,
	.boot_params	= 0xc0000100,
	.map_io		= cdb89712_map_io,
	.init_irq	= clps711x_init_irq,
	.timer		= &clps711x_timer,
MACHINE_END
