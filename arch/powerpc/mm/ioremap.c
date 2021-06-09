// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/mmzone.h>
#include <linux/vmalloc.h>
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

int early_ioremap_range(unsigned long ea, phys_addr_t pa,
			unsigned long size, pgprot_t prot)
{
	unsigned long i;

	for (i = 0; i < size; i += PAGE_SIZE) {
		int err = map_kernel_page(ea + i, pa + i, prot);

		if (WARN_ON_ONCE(err))  /* Should clean up */
			return err;
	}

	return 0;
}

void __iomem *do_ioremap(phys_addr_t pa, phys_addr_t offset, unsigned long size,
			 pgprot_t prot, void *caller)
{
	struct vm_struct *area;
	int ret;
	unsigned long va;

	area = __get_vm_area_caller(size, VM_IOREMAP, IOREMAP_START, IOREMAP_END, caller);
	if (area == NULL)
		return NULL;

	area->phys_addr = pa;
	va = (unsigned long)area->addr;

	ret = ioremap_page_range(va, va + size, pa, prot);
	if (!ret)
		return (void __iomem *)area->addr + offset;

	vunmap_range(va, va + size);
	free_vm_area(area);

	return NULL;
}

#ifdef CONFIG_ZONE_DEVICE
/*
 * Override the generic version in mm/memremap.c.
 *
 * With hash translation, the direct-map range is mapped with just one
 * page size selected by htab_init_page_sizes(). Consult
 * mmu_psize_defs[] to determine the minimum page size alignment.
*/
unsigned long memremap_compat_align(void)
{
	unsigned int shift = mmu_psize_defs[mmu_linear_psize].shift;

	if (radix_enabled())
		return SUBSECTION_SIZE;
	return max(SUBSECTION_SIZE, 1UL << shift);

}
EXPORT_SYMBOL_GPL(memremap_compat_align);
#endif
