// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/io.h>
#include <asm/io-workarounds.h>

unsigned long ioremap_bot;
EXPORT_SYMBOL(ioremap_bot);

void __iomem *ioremap(phys_addr_t addr, unsigned long size)
{
	pgprot_t prot = pgprot_noncached(PAGE_KERNEL);
	void *caller = __builtin_return_address(0);

	if (iowa_is_active())
		return iowa_ioremap(addr, size, prot, caller);
	return __ioremap_caller(addr, size, prot, caller);
}
EXPORT_SYMBOL(ioremap);

void __iomem *ioremap_wc(phys_addr_t addr, unsigned long size)
{
	pgprot_t prot = pgprot_noncached_wc(PAGE_KERNEL);
	void *caller = __builtin_return_address(0);

	if (iowa_is_active())
		return iowa_ioremap(addr, size, prot, caller);
	return __ioremap_caller(addr, size, prot, caller);
}
EXPORT_SYMBOL(ioremap_wc);

void __iomem *ioremap_coherent(phys_addr_t addr, unsigned long size)
{
	pgprot_t prot = pgprot_cached(PAGE_KERNEL);
	void *caller = __builtin_return_address(0);

	if (iowa_is_active())
		return iowa_ioremap(addr, size, prot, caller);
	return __ioremap_caller(addr, size, prot, caller);
}

void __iomem *ioremap_prot(phys_addr_t addr, unsigned long size, unsigned long flags)
{
	pte_t pte = __pte(flags);
	void *caller = __builtin_return_address(0);

	/* writeable implies dirty for kernel addresses */
	if (pte_write(pte))
		pte = pte_mkdirty(pte);

	/* we don't want to let _PAGE_USER and _PAGE_EXEC leak out */
	pte = pte_exprotect(pte);
	pte = pte_mkprivileged(pte);

	if (iowa_is_active())
		return iowa_ioremap(addr, size, pte_pgprot(pte), caller);
	return __ioremap_caller(addr, size, pte_pgprot(pte), caller);
}
EXPORT_SYMBOL(ioremap_prot);
