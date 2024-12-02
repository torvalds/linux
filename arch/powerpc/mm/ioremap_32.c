// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <mm/mmu_decl.h>

void __iomem *ioremap_wt(phys_addr_t addr, unsigned long size)
{
	pgprot_t prot = pgprot_cached_wthru(PAGE_KERNEL);

	return __ioremap_caller(addr, size, prot, __builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_wt);

void __iomem *
__ioremap_caller(phys_addr_t addr, unsigned long size, pgprot_t prot, void *caller)
{
	unsigned long v;
	phys_addr_t p, offset;
	int err;

	/*
	 * Choose an address to map it to.
	 * Once the vmalloc system is running, we use it.
	 * Before then, we use space going down from IOREMAP_TOP
	 * (ioremap_bot records where we're up to).
	 */
	p = addr & PAGE_MASK;
	offset = addr & ~PAGE_MASK;
	size = PAGE_ALIGN(addr + size) - p;

	/*
	 * If the address lies within the first 16 MB, assume it's in ISA
	 * memory space
	 */
	if (p < 16 * 1024 * 1024)
		p += _ISA_MEM_BASE;

#ifndef CONFIG_CRASH_DUMP
	/*
	 * Don't allow anybody to remap normal RAM that we're using.
	 * mem_init() sets high_memory so only do the check after that.
	 */
	if (slab_is_available() && p <= virt_to_phys(high_memory - 1) &&
	    page_is_ram(__phys_to_pfn(p))) {
		pr_warn("%s(): phys addr 0x%llx is RAM lr %ps\n", __func__,
			(unsigned long long)p, __builtin_return_address(0));
		return NULL;
	}
#endif

	if (size == 0)
		return NULL;

	/*
	 * Is it already mapped?  Perhaps overlapped by a previous
	 * mapping.
	 */
	v = p_block_mapped(p);
	if (v)
		return (void __iomem *)v + offset;

	if (slab_is_available())
		return do_ioremap(p, offset, size, prot, caller);

	/*
	 * Should check if it is a candidate for a BAT mapping
	 */
	pr_warn("ioremap() called early from %pS. Use early_ioremap() instead\n", caller);

	err = early_ioremap_range(ioremap_bot - size - PAGE_SIZE, p, size, prot);
	if (err)
		return NULL;
	ioremap_bot -= size + PAGE_SIZE;

	return (void __iomem *)ioremap_bot + offset;
}

void iounmap(volatile void __iomem *addr)
{
	/*
	 * If mapped by BATs then there is nothing to do.
	 * Calling vfree() generates a benign warning.
	 */
	if (v_block_mapped((unsigned long)addr))
		return;

	if (addr > high_memory && (unsigned long)addr < ioremap_bot)
		vunmap((void *)(PAGE_MASK & (unsigned long)addr));
}
EXPORT_SYMBOL(iounmap);
