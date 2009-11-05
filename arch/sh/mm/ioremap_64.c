/*
 * arch/sh/mm/ioremap_64.c
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003 - 2007  Paul Mundt
 *
 * Mostly derived from arch/sh/mm/ioremap.c which, in turn is mostly
 * derived from arch/i386/mm/ioremap.c .
 *
 *   (C) Copyright 1995 1996 Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/vmalloc.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/bootmem.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/addrspace.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/mmu.h>

void __iomem *__ioremap_caller(unsigned long offset, unsigned long size,
			       unsigned long flags, void *caller)
{
	pgprot_t prot;

	prot = __pgprot(_PAGE_PRESENT | _PAGE_READ     | _PAGE_WRITE  |
			_PAGE_DIRTY   | _PAGE_ACCESSED | _PAGE_SHARED | flags);

	return ioremap_fixed(offset, size, prot);
}
EXPORT_SYMBOL(__ioremap_caller);

void __iounmap(void __iomem *virtual)
{
	iounmap_fixed(virtual);
}
EXPORT_SYMBOL(__iounmap);
