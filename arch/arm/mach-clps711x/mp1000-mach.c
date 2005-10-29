/*
 *  linux/arch/arm/mach-mp1000/mp1000.c
 *
 *  Copyright (C) 2005 Comdial Corporation
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
#include <linux/init.h>
#include <linux/types.h>
#include <linux/string.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/arch/mp1000-seprom.h>

#include "common.h"

extern void mp1000_map_io(void);

static void __init mp1000_init(void)
{
    seprom_init();
}

MACHINE_START(MP1000, "Comdial MP1000")
	/* Maintainer: Jon Ringle */
	.phys_ram	= 0xc0000000,
	.phys_io	= 0x80000000,
	.io_pg_offst	= ((0xff000000) >> 18) & 0xfffc,
	.boot_params	= 0xc0015100,
	.map_io		= mp1000_map_io,
	.init_irq	= clps711x_init_irq,
	.init_machine	= mp1000_init,
	.timer		= &clps711x_timer,
MACHINE_END

