// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/arch/h8300/mm/memory.c
 *
 *  Copyright (C) 2002  Yoshinori Sato <ysato@users.sourceforge.jp>,
 *
 *  Based on:
 *
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

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/traps.h>
#include <asm/io.h>

void cache_clear(unsigned long paddr, int len)
{
}


void cache_push(unsigned long paddr, int len)
{
}

void cache_push_v(unsigned long vaddr, int len)
{
}

/*
 * Map some physical address range into the kernel address space.
 */

unsigned long kernel_map(unsigned long paddr, unsigned long size,
			 int nocacheflag, unsigned long *memavailp)
{
	return paddr;
}
