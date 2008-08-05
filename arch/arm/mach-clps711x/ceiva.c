/*
 *  linux/arch/arm/mach-clps711x/arch-ceiva.c
 *
 *  Copyright (C) 2002, Rob Scott <rscott@mtrob.fdns.net>
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

#include <linux/kernel.h>

#include <mach/hardware.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/sizes.h>

#include <asm/mach/map.h>

#include "common.h"

static struct map_desc ceiva_io_desc[] __initdata = {
 	/* SED1355 controlled video RAM & registers */
 	{
		.virtual	= CEIVA_VIRT_SED1355,
		.pfn		= __phys_to_pfn(CEIVA_PHYS_SED1355),
		.length		= SZ_2M,
		.type		= MT_DEVICE
	}
};


static void __init ceiva_map_io(void)
{
        clps711x_map_io();
        iotable_init(ceiva_io_desc, ARRAY_SIZE(ceiva_io_desc));
}


MACHINE_START(CEIVA, "CEIVA/Polaroid Photo MAX Digital Picture Frame")
	/* Maintainer: Rob Scott */
	.phys_io	= 0x80000000,
	.io_pg_offst	= ((0xff000000) >> 18) & 0xfffc,
	.boot_params	= 0xc0000100,
	.map_io		= ceiva_map_io,
	.init_irq	= clps711x_init_irq,
	.timer		= &clps711x_timer,
MACHINE_END
