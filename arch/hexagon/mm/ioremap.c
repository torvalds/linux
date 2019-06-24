// SPDX-License-Identifier: GPL-2.0-only
/*
 * I/O remap functions for Hexagon
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#include <linux/io.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>

void __iomem *ioremap_nocache(unsigned long phys_addr, unsigned long size)
{
	unsigned long last_addr, addr;
	unsigned long offset = phys_addr & ~PAGE_MASK;
	struct vm_struct *area;

	pgprot_t prot = __pgprot(_PAGE_PRESENT|_PAGE_READ|_PAGE_WRITE
					|(__HEXAGON_C_DEV << 6));

	last_addr = phys_addr + size - 1;

	/*  Wrapping not allowed  */
	if (!size || (last_addr < phys_addr))
		return NULL;

	/*  Rounds up to next page size, including whole-page offset */
	size = PAGE_ALIGN(offset + size);

	area = get_vm_area(size, VM_IOREMAP);
	addr = (unsigned long)area->addr;

	if (ioremap_page_range(addr, addr+size, phys_addr, prot)) {
		vunmap((void *)addr);
		return NULL;
	}

	return (void __iomem *) (offset + addr);
}

void __iounmap(const volatile void __iomem *addr)
{
	vunmap((void *) ((unsigned long) addr & PAGE_MASK));
}
