// SPDX-License-Identifier: GPL-2.0-only
#include <linux/highmem.h>
#include <linux/export.h>
#include <linux/swap.h> /* for totalram_pages */
#include <linux/memblock.h>

void *kmap_atomic_high_prot(struct page *page, pgprot_t prot)
{
	unsigned long vaddr;
	int idx, type;

	type = kmap_atomic_idx_push();
	idx = type + KM_TYPE_NR*smp_processor_id();
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
	BUG_ON(!pte_none(*(kmap_pte-idx)));
	set_pte(kmap_pte-idx, mk_pte(page, prot));
	arch_flush_lazy_mmu_mode();

	return (void *)vaddr;
}
EXPORT_SYMBOL(kmap_atomic_high_prot);

/*
 * This is the same as kmap_atomic() but can map memory that doesn't
 * have a struct page associated with it.
 */
void *kmap_atomic_pfn(unsigned long pfn)
{
	return kmap_atomic_prot_pfn(pfn, kmap_prot);
}
EXPORT_SYMBOL_GPL(kmap_atomic_pfn);

void kunmap_atomic_high(void *kvaddr)
{
	unsigned long vaddr = (unsigned long) kvaddr & PAGE_MASK;

	if (vaddr >= __fix_to_virt(FIX_KMAP_END) &&
	    vaddr <= __fix_to_virt(FIX_KMAP_BEGIN)) {
		int idx, type;

		type = kmap_atomic_idx();
		idx = type + KM_TYPE_NR * smp_processor_id();

#ifdef CONFIG_DEBUG_HIGHMEM
		WARN_ON_ONCE(vaddr != __fix_to_virt(FIX_KMAP_BEGIN + idx));
#endif
		/*
		 * Force other mappings to Oops if they'll try to access this
		 * pte without first remap it.  Keeping stale mappings around
		 * is a bad idea also, in case the page changes cacheability
		 * attributes or becomes a protected page in a hypervisor.
		 */
		kpte_clear_flush(kmap_pte-idx, vaddr);
		kmap_atomic_idx_pop();
		arch_flush_lazy_mmu_mode();
	}
#ifdef CONFIG_DEBUG_HIGHMEM
	else {
		BUG_ON(vaddr < PAGE_OFFSET);
		BUG_ON(vaddr >= (unsigned long)high_memory);
	}
#endif
}
EXPORT_SYMBOL(kunmap_atomic_high);

void __init set_highmem_pages_init(void)
{
	struct zone *zone;
	int nid;

	/*
	 * Explicitly reset zone->managed_pages because set_highmem_pages_init()
	 * is invoked before memblock_free_all()
	 */
	reset_all_zones_managed_pages();
	for_each_zone(zone) {
		unsigned long zone_start_pfn, zone_end_pfn;

		if (!is_highmem(zone))
			continue;

		zone_start_pfn = zone->zone_start_pfn;
		zone_end_pfn = zone_start_pfn + zone->spanned_pages;

		nid = zone_to_nid(zone);
		printk(KERN_INFO "Initializing %s for node %d (%08lx:%08lx)\n",
				zone->name, nid, zone_start_pfn, zone_end_pfn);

		add_highpages_with_active_regions(nid, zone_start_pfn,
				 zone_end_pfn);
	}
}
