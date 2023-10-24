// SPDX-License-Identifier: GPL-2.0-only
/*
 * ioremap implementation.
 *
 * Copyright (C) 2015 Cadence Design Systems Inc.
 */

#include <linux/io.h>
#include <linux/pgtable.h>
#include <asm/cacheflush.h>
#include <asm/io.h>

void __iomem *ioremap_prot(phys_addr_t phys_addr, size_t size,
			   unsigned long prot)
{
	unsigned long pfn = __phys_to_pfn((phys_addr));
	WARN_ON(pfn_valid(pfn));

	return generic_ioremap_prot(phys_addr, size, __pgprot(prot));
}
EXPORT_SYMBOL(ioremap_prot);

void iounmap(volatile void __iomem *addr)
{
	unsigned long va = (unsigned long) addr;

	if ((va >= XCHAL_KIO_CACHED_VADDR &&
	      va - XCHAL_KIO_CACHED_VADDR < XCHAL_KIO_SIZE) ||
	    (va >= XCHAL_KIO_BYPASS_VADDR &&
	      va - XCHAL_KIO_BYPASS_VADDR < XCHAL_KIO_SIZE))
		return;

	generic_iounmap(addr);
}
EXPORT_SYMBOL(iounmap);
