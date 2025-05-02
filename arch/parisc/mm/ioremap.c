// SPDX-License-Identifier: GPL-2.0
/*
 * arch/parisc/mm/ioremap.c
 *
 * (C) Copyright 1995 1996 Linus Torvalds
 * (C) Copyright 2001-2019 Helge Deller <deller@gmx.de>
 * (C) Copyright 2005 Kyle McMartin <kyle@parisc-linux.org>
 */

#include <linux/vmalloc.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/mm.h>

void __iomem *ioremap_prot(phys_addr_t phys_addr, size_t size,
			   pgprot_t prot)
{
#ifdef CONFIG_EISA
	unsigned long end = phys_addr + size - 1;
	/* Support EISA addresses */
	if ((phys_addr >= 0x00080000 && end < 0x000fffff) ||
	    (phys_addr >= 0x00500000 && end < 0x03bfffff))
		phys_addr |= F_EXTEND(0xfc000000);
#endif

	/*
	 * Don't allow anybody to remap normal RAM that we're using..
	 */
	if (phys_addr < virt_to_phys(high_memory)) {
		char *t_addr, *t_end;
		struct page *page;

		t_addr = __va(phys_addr);
		t_end = t_addr + (size - 1);
	   
		for (page = virt_to_page(t_addr); 
		     page <= virt_to_page(t_end); page++) {
			if(!PageReserved(page))
				return NULL;
		}
	}

	return generic_ioremap_prot(phys_addr, size, prot);
}
EXPORT_SYMBOL(ioremap_prot);
