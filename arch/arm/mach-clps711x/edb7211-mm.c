/*
 *  linux/arch/arm/mach-clps711x/mm.c
 *
 *  Extra MM routines for the EDB7211 board
 *
 *  Copyright (C) 2000, 2001 Blue Mug, Inc.  All Rights Reserved.
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

#include <mach/hardware.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/sizes.h>
 
#include <asm/mach/map.h>

extern void clps711x_map_io(void);

/*
 * The on-chip registers are given a size of 1MB so that a section can
 * be used to map them; this saves a page table.  This is the place to
 * add mappings for ROM, expansion memory, PCMCIA, etc.  (if static
 * mappings are chosen for those areas).
 *
 * Here is a physical memory map (to be fleshed out later):
 *
 * Physical Address  Size  Description
 * ----------------- ----- ---------------------------------
 * c0000000-c001ffff 128KB reserved for video RAM [1]
 * c0020000-c0023fff  16KB parameters (see Documentation/arm/Setup)
 * c0024000-c0027fff  16KB swapper_pg_dir (task 0 page directory)
 * c0028000-...            kernel image (TEXTADDR)
 *
 * [1] Unused pages should be given back to the VM; they are not yet.
 *     The parameter block should also be released (not sure if this
 *     happens).
 */
static struct map_desc edb7211_io_desc[] __initdata = {
 	{	/* memory-mapped extra keyboard row */
	 	.virtual 	= EP7211_VIRT_EXTKBD,
		.pfn		= __phys_to_pfn(EP7211_PHYS_EXTKBD),
		.length		= SZ_1M,
		.type		= MT_DEVICE,
	}, {	/* and CS8900A Ethernet chip */
		.virtual	= EP7211_VIRT_CS8900A,
		.pfn		= __phys_to_pfn(EP7211_PHYS_CS8900A),
		.length		= SZ_1M,
		.type		= MT_DEVICE,
	}, { 	/* flash banks */
		.virtual	= EP7211_VIRT_FLASH1,
		.pfn		= __phys_to_pfn(EP7211_PHYS_FLASH1),
		.length		= SZ_8M,
		.type		= MT_DEVICE,
	}, {
		.virtual	= EP7211_VIRT_FLASH2,
		.pfn		= __phys_to_pfn(EP7211_PHYS_FLASH2),
		.length		= SZ_8M,
		.type		= MT_DEVICE,
	}
};

void __init edb7211_map_io(void)
{
        clps711x_map_io();
        iotable_init(edb7211_io_desc, ARRAY_SIZE(edb7211_io_desc));
}

