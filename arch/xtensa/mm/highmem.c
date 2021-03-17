/*
 * High memory support for Xtensa architecture
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2014 Cadence Design Systems Inc.
 */

#include <linux/export.h>
#include <linux/highmem.h>
#include <asm/tlbflush.h>

static pte_t *kmap_pte;

#if DCACHE_WAY_SIZE > PAGE_SIZE
unsigned int last_pkmap_nr_arr[DCACHE_N_COLORS];
wait_queue_head_t pkmap_map_wait_arr[DCACHE_N_COLORS];

static void __init kmap_waitqueues_init(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pkmap_map_wait_arr); ++i)
		init_waitqueue_head(pkmap_map_wait_arr + i);
}
#else
static inline void kmap_waitqueues_init(void)
{
}
#endif

static inline enum fixed_addresses kmap_idx(int type, unsigned long color)
{
	return (type + KM_TYPE_NR * smp_processor_id()) * DCACHE_N_COLORS +
		color;
}

void *kmap_atomic_high_prot(struct page *page, pgprot_t prot)
{
	enum fixed_addresses idx;
	unsigned long vaddr;

	idx = kmap_idx(kmap_atomic_idx_push(),
		       DCACHE_ALIAS(page_to_phys(page)));
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
#ifdef CONFIG_DEBUG_HIGHMEM
	BUG_ON(!pte_none(*(kmap_pte + idx)));
#endif
	set_pte(kmap_pte + idx, mk_pte(page, prot));

	return (void *)vaddr;
}
EXPORT_SYMBOL(kmap_atomic_high_prot);

void kunmap_atomic_high(void *kvaddr)
{
	if (kvaddr >= (void *)FIXADDR_START &&
	    kvaddr < (void *)FIXADDR_TOP) {
		int idx = kmap_idx(kmap_atomic_idx(),
				   DCACHE_ALIAS((unsigned long)kvaddr));

		/*
		 * Force other mappings to Oops if they'll try to access this
		 * pte without first remap it.  Keeping stale mappings around
		 * is a bad idea also, in case the page changes cacheability
		 * attributes or becomes a protected page in a hypervisor.
		 */
		pte_clear(&init_mm, kvaddr, kmap_pte + idx);
		local_flush_tlb_kernel_range((unsigned long)kvaddr,
					     (unsigned long)kvaddr + PAGE_SIZE);

		kmap_atomic_idx_pop();
	}
}
EXPORT_SYMBOL(kunmap_atomic_high);

void __init kmap_init(void)
{
	unsigned long kmap_vstart;

	/* Check if this memory layout is broken because PKMAP overlaps
	 * page table.
	 */
	BUILD_BUG_ON(PKMAP_BASE < TLBTEMP_BASE_1 + TLBTEMP_SIZE);
	/* cache the first kmap pte */
	kmap_vstart = __fix_to_virt(FIX_KMAP_BEGIN);
	kmap_pte = virt_to_kpte(kmap_vstart);
	kmap_waitqueues_init();
}
