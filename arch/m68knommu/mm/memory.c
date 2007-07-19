/*
 *  linux/arch/m68knommu/mm/memory.c
 *
 *  Copyright (C) 1998  Kenneth Albanowski <kjahds@kjahds.com>,
 *  Copyright (C) 1999-2002, Greg Ungerer (gerg@snapgear.com)
 *
 *  Based on:
 *
 *  linux/arch/m68k/mm/memory.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 */

#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/slab.h>

#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>

/*
 * Map some physical address range into the kernel address space.
 * The code is copied and adapted from map_chunk().
 */

unsigned long kernel_map(unsigned long paddr, unsigned long size,
			 int nocacheflag, unsigned long *memavailp )
{
	return paddr;
}


int is_in_rom(unsigned long addr)
{
	extern unsigned long _ramstart, _ramend;

	/*
	 *	What we are really trying to do is determine if addr is
	 *	in an allocated kernel memory region. If not then assume
	 *	we cannot free it or otherwise de-allocate it. Ideally
	 *	we could restrict this to really being in a ROM or flash,
	 *	but that would need to be done on a board by board basis,
	 *	not globally.
	 */
	if ((addr < _ramstart) || (addr >= _ramend))
		return(1);

	/* Default case, not in ROM */
	return(0);
}

