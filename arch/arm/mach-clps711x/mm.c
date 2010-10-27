/*
 *  linux/arch/arm/mach-clps711x/mm.c
 *
 *  Generic MM setup for the CLPS711x-based machines.
 *
 *  Copyright (C) 2001 Deep Blue Solutions Ltd
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
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/sizes.h>
#include <mach/hardware.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/mach/map.h>
#include <asm/hardware/clps7111.h>

/*
 * This maps the generic CLPS711x registers
 */
static struct map_desc clps711x_io_desc[] __initdata = {
	{
		.virtual	= CLPS7111_VIRT_BASE,
		.pfn		= __phys_to_pfn(CLPS7111_PHYS_BASE),
		.length		= SZ_1M,
		.type		= MT_DEVICE
	}
};

void __init clps711x_map_io(void)
{
	iotable_init(clps711x_io_desc, ARRAY_SIZE(clps711x_io_desc));
}
