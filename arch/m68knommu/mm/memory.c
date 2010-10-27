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

#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>

/*
 * Map some physical address range into the kernel address space.
 */

unsigned long kernel_map(unsigned long paddr, unsigned long size,
			 int nocacheflag, unsigned long *memavailp )
{
	return paddr;
}

