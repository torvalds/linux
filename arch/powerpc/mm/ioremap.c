// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/mmzone.h>
#include <linux/vmalloc.h>

unsigned long ioremap_bot;
EXPORT_SYMBOL(ioremap_bot);

void __iomem *ioremap(phys_addr_t addr, unsigned long size)
{
	pgprot_t prot = pgprot_noncached(PAGE_KERNEL);
	void *caller = __builtin_return_address(0);

	return __ioremap_caller(addr, size, prot, caller);
}
EXPORT_SYMBOL(ioremap);

void __iomem *ioremap_wc(phys_addr_t addr, unsigned long size)
{
	pgprot_t prot = pgprot_noncached_wc(PAGE_KERNEL);
	void *caller = __builtin_return_address(0);

	return __ioremap_caller(addr, size, prot, caller);
}
EXPORT_SYMBOL(ioremap_wc);

void __iomem *ioremap_coherent(phys_addr_t addr, unsigned long size)
{
	pgprot_t prot = pgprot_cached(PAGE_KERNEL);
	void *caller = __builtin_return_address(0);

	return __ioremap_caller(addr, size, prot, caller);
}

void __iomem *ioremap_prot(phys_addr_t addr, size_t size, unsigned long flags)
{
	pte_t pte = __pte(flags);
	void *caller = __builtin_return_address(0);

	/* writeable implies dirty for kernel addresses */
	if (pte_write(pte))
		pte = pte_mkdirty(pte);

	return __ioremap_caller(addr, size, pte_pgprot(pte), caller);
}
EXPORT_SYMBOL(ioremap_prot);

int early_ioremap_range(unsigned long ea, phys_addr_t pa,
			unsigned long size, pgprot_t prot)
{
	unsigned long i;

	for (i = 0; i < size; i += PAGE_SIZE) {
		int err = map_kernel_page(ea + i, pa + i, pgprot_nx(prot));

		if (WARN_ON_ONCE(err))  /* Should clean up */
			return err;
	}

	return 0;
}
