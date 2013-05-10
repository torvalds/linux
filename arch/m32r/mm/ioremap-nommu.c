/*
 *  linux/arch/m32r/mm/ioremap-nommu.c
 *
 *  Copyright (c) 2001, 2002  Hiroyuki Kondo
 *
 *  Taken from mips version.
 *    (C) Copyright 1995 1996 Linus Torvalds
 *    (C) Copyright 2001 Ralf Baechle
 */

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#include <linux/module.h>
#include <asm/addrspace.h>
#include <asm/byteorder.h>

#include <linux/vmalloc.h>
#include <asm/io.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>


/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space. Needed when the kernel wants to access high addresses
 * directly.
 *
 * NOTE! We need to allow non-page-aligned mappings too: we will obviously
 * have to convert them into an offset in a page-aligned mapping, but the
 * caller shouldn't need to know that small detail.
 */

#define IS_LOW512(addr) (!((unsigned long)(addr) & ~0x1fffffffUL))

void __iomem *
__ioremap(unsigned long phys_addr, unsigned long size, unsigned long flags)
{
	return (void *)phys_addr;
}

#define IS_KSEG1(addr) (((unsigned long)(addr) & ~0x1fffffffUL) == KSEG1)

void iounmap(volatile void __iomem *addr)
{
}

