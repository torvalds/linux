// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

/**
 * Low level function to establish the page tables for an IO mapping
 */
void __iomem *__ioremap_at(phys_addr_t pa, void *ea, unsigned long size, pgprot_t prot)
{
	int ret;
	unsigned long va = (unsigned long)ea;

	/* We don't support the 4K PFN hack with ioremap */
	if (pgprot_val(prot) & H_PAGE_4K_PFN)
		return NULL;

	if ((ea + size) >= (void *)IOREMAP_END) {
		pr_warn("Outside the supported range\n");
		return NULL;
	}

	WARN_ON(pa & ~PAGE_MASK);
	WARN_ON(((unsigned long)ea) & ~PAGE_MASK);
	WARN_ON(size & ~PAGE_MASK);

	if (slab_is_available()) {
		ret = ioremap_page_range(va, va + size, pa, prot);
		if (ret)
			unmap_kernel_range(va, size);
	} else {
		ret = early_ioremap_range(va, pa, size, prot);
	}

	if (ret)
		return NULL;

	return (void __iomem *)ea;
}
EXPORT_SYMBOL(__ioremap_at);

/**
 * Low level function to tear down the page tables for an IO mapping. This is
 * used for mappings that are manipulated manually, like partial unmapping of
 * PCI IOs or ISA space.
 */
void __iounmap_at(void *ea, unsigned long size)
{
	WARN_ON(((unsigned long)ea) & ~PAGE_MASK);
	WARN_ON(size & ~PAGE_MASK);

	unmap_kernel_range((unsigned long)ea, size);
}
EXPORT_SYMBOL(__iounmap_at);

void __iomem *__ioremap_caller(phys_addr_t addr, unsigned long size,
			       pgprot_t prot, void *caller)
{
	phys_addr_t paligned, offset;
	void __iomem *ret;
	int err;

	/* We don't support the 4K PFN hack with ioremap */
	if (pgprot_val(prot) & H_PAGE_4K_PFN)
		return NULL;

	/*
	 * Choose an address to map it to. Once the vmalloc system is running,
	 * we use it. Before that, we map using addresses going up from
	 * ioremap_bot.  vmalloc will use the addresses from IOREMAP_BASE
	 * through ioremap_bot.
	 */
	paligned = addr & PAGE_MASK;
	offset = addr & ~PAGE_MASK;
	size = PAGE_ALIGN(addr + size) - paligned;

	if (size == 0 || paligned == 0)
		return NULL;

	if (slab_is_available())
		return do_ioremap(paligned, offset, size, prot, caller);

	pr_warn("ioremap() called early from %pS. Use early_ioremap() instead\n", caller);

	err = early_ioremap_range(ioremap_bot, paligned, size, prot);
	if (err)
		return NULL;

	ret = (void __iomem *)ioremap_bot + offset;
	ioremap_bot += size;

	return ret;
}

/*
 * Unmap an IO region and remove it from vmalloc'd list.
 * Access to IO memory should be serialized by driver.
 */
void iounmap(volatile void __iomem *token)
{
	void *addr;

	if (!slab_is_available())
		return;

	addr = (void *)((unsigned long __force)PCI_FIX_ADDR(token) & PAGE_MASK);

	if ((unsigned long)addr < ioremap_bot) {
		pr_warn("Attempt to iounmap early bolted mapping at 0x%p\n", addr);
		return;
	}
	vunmap(addr);
}
EXPORT_SYMBOL(iounmap);
