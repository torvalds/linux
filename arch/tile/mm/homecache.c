/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * This code maintains the "home" for each page in the system.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/bootmem.h>
#include <linux/rmap.h>
#include <linux/pagemap.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/sysctl.h>
#include <linux/pagevec.h>
#include <linux/ptrace.h>
#include <linux/timex.h>
#include <linux/cache.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <linux/hugetlb.h>

#include <asm/page.h>
#include <asm/sections.h>
#include <asm/tlbflush.h>
#include <asm/pgalloc.h>
#include <asm/homecache.h>

#include <arch/sim.h>

#include "migrate.h"


#if CHIP_HAS_COHERENT_LOCAL_CACHE()

/*
 * The noallocl2 option suppresses all use of the L2 cache to cache
 * locally from a remote home.  There's no point in using it if we
 * don't have coherent local caching, though.
 */
static int __write_once noallocl2;
static int __init set_noallocl2(char *str)
{
	noallocl2 = 1;
	return 0;
}
early_param("noallocl2", set_noallocl2);

#else

#define noallocl2 0

#endif


/*
 * Update the irq_stat for cpus that we are going to interrupt
 * with TLB or cache flushes.  Also handle removing dataplane cpus
 * from the TLB flush set, and setting dataplane_tlb_state instead.
 */
static void hv_flush_update(const struct cpumask *cache_cpumask,
			    struct cpumask *tlb_cpumask,
			    unsigned long tlb_va, unsigned long tlb_length,
			    HV_Remote_ASID *asids, int asidcount)
{
	struct cpumask mask;
	int i, cpu;

	cpumask_clear(&mask);
	if (cache_cpumask)
		cpumask_or(&mask, &mask, cache_cpumask);
	if (tlb_cpumask && tlb_length) {
		cpumask_or(&mask, &mask, tlb_cpumask);
	}

	for (i = 0; i < asidcount; ++i)
		cpumask_set_cpu(asids[i].y * smp_width + asids[i].x, &mask);

	/*
	 * Don't bother to update atomically; losing a count
	 * here is not that critical.
	 */
	for_each_cpu(cpu, &mask)
		++per_cpu(irq_stat, cpu).irq_hv_flush_count;
}

/*
 * This wrapper function around hv_flush_remote() does several things:
 *
 *  - Provides a return value error-checking panic path, since
 *    there's never any good reason for hv_flush_remote() to fail.
 *  - Accepts a 32-bit PFN rather than a 64-bit PA, which generally
 *    is the type that Linux wants to pass around anyway.
 *  - Canonicalizes that lengths of zero make cpumasks NULL.
 *  - Handles deferring TLB flushes for dataplane tiles.
 *  - Tracks remote interrupts in the per-cpu irq_cpustat_t.
 *
 * Note that we have to wait until the cache flush completes before
 * updating the per-cpu last_cache_flush word, since otherwise another
 * concurrent flush can race, conclude the flush has already
 * completed, and start to use the page while it's still dirty
 * remotely (running concurrently with the actual evict, presumably).
 */
void flush_remote(unsigned long cache_pfn, unsigned long cache_control,
		  const struct cpumask *cache_cpumask_orig,
		  HV_VirtAddr tlb_va, unsigned long tlb_length,
		  unsigned long tlb_pgsize,
		  const struct cpumask *tlb_cpumask_orig,
		  HV_Remote_ASID *asids, int asidcount)
{
	int rc;
	struct cpumask cache_cpumask_copy, tlb_cpumask_copy;
	struct cpumask *cache_cpumask, *tlb_cpumask;
	HV_PhysAddr cache_pa;
	char cache_buf[NR_CPUS*5], tlb_buf[NR_CPUS*5];

	mb();   /* provided just to simplify "magic hypervisor" mode */

	/*
	 * Canonicalize and copy the cpumasks.
	 */
	if (cache_cpumask_orig && cache_control) {
		cpumask_copy(&cache_cpumask_copy, cache_cpumask_orig);
		cache_cpumask = &cache_cpumask_copy;
	} else {
		cpumask_clear(&cache_cpumask_copy);
		cache_cpumask = NULL;
	}
	if (cache_cpumask == NULL)
		cache_control = 0;
	if (tlb_cpumask_orig && tlb_length) {
		cpumask_copy(&tlb_cpumask_copy, tlb_cpumask_orig);
		tlb_cpumask = &tlb_cpumask_copy;
	} else {
		cpumask_clear(&tlb_cpumask_copy);
		tlb_cpumask = NULL;
	}

	hv_flush_update(cache_cpumask, tlb_cpumask, tlb_va, tlb_length,
			asids, asidcount);
	cache_pa = (HV_PhysAddr)cache_pfn << PAGE_SHIFT;
	rc = hv_flush_remote(cache_pa, cache_control,
			     cpumask_bits(cache_cpumask),
			     tlb_va, tlb_length, tlb_pgsize,
			     cpumask_bits(tlb_cpumask),
			     asids, asidcount);
	if (rc == 0)
		return;
	cpumask_scnprintf(cache_buf, sizeof(cache_buf), &cache_cpumask_copy);
	cpumask_scnprintf(tlb_buf, sizeof(tlb_buf), &tlb_cpumask_copy);

	pr_err("hv_flush_remote(%#llx, %#lx, %p [%s],"
	       " %#lx, %#lx, %#lx, %p [%s], %p, %d) = %d\n",
	       cache_pa, cache_control, cache_cpumask, cache_buf,
	       (unsigned long)tlb_va, tlb_length, tlb_pgsize,
	       tlb_cpumask, tlb_buf,
	       asids, asidcount, rc);
	panic("Unsafe to continue.");
}

static void homecache_finv_page_va(void* va, int home)
{
	if (home == smp_processor_id()) {
		finv_buffer_local(va, PAGE_SIZE);
	} else if (home == PAGE_HOME_HASH) {
		finv_buffer_remote(va, PAGE_SIZE, 1);
	} else {
		BUG_ON(home < 0 || home >= NR_CPUS);
		finv_buffer_remote(va, PAGE_SIZE, 0);
	}
}

void homecache_finv_map_page(struct page *page, int home)
{
	unsigned long flags;
	unsigned long va;
	pte_t *ptep;
	pte_t pte;

	if (home == PAGE_HOME_UNCACHED)
		return;
	local_irq_save(flags);
#ifdef CONFIG_HIGHMEM
	va = __fix_to_virt(FIX_KMAP_BEGIN + kmap_atomic_idx_push() +
			   (KM_TYPE_NR * smp_processor_id()));
#else
	va = __fix_to_virt(FIX_HOMECACHE_BEGIN + smp_processor_id());
#endif
	ptep = virt_to_pte(NULL, (unsigned long)va);
	pte = pfn_pte(page_to_pfn(page), PAGE_KERNEL);
	__set_pte(ptep, pte_set_home(pte, home));
	homecache_finv_page_va((void *)va, home);
	__pte_clear(ptep);
	hv_flush_page(va, PAGE_SIZE);
#ifdef CONFIG_HIGHMEM
	kmap_atomic_idx_pop();
#endif
	local_irq_restore(flags);
}

static void homecache_finv_page_home(struct page *page, int home)
{
	if (!PageHighMem(page) && home == page_home(page))
		homecache_finv_page_va(page_address(page), home);
	else
		homecache_finv_map_page(page, home);
}

static inline bool incoherent_home(int home)
{
	return home == PAGE_HOME_IMMUTABLE || home == PAGE_HOME_INCOHERENT;
}

static void homecache_finv_page_internal(struct page *page, int force_map)
{
	int home = page_home(page);
	if (home == PAGE_HOME_UNCACHED)
		return;
	if (incoherent_home(home)) {
		int cpu;
		for_each_cpu(cpu, &cpu_cacheable_map)
			homecache_finv_map_page(page, cpu);
	} else if (force_map) {
		/* Force if, e.g., the normal mapping is migrating. */
		homecache_finv_map_page(page, home);
	} else {
		homecache_finv_page_home(page, home);
	}
	sim_validate_lines_evicted(PFN_PHYS(page_to_pfn(page)), PAGE_SIZE);
}

void homecache_finv_page(struct page *page)
{
	homecache_finv_page_internal(page, 0);
}

void homecache_evict(const struct cpumask *mask)
{
	flush_remote(0, HV_FLUSH_EVICT_L2, mask, 0, 0, 0, NULL, NULL, 0);
}

/* Report the home corresponding to a given PTE. */
static int pte_to_home(pte_t pte)
{
	if (hv_pte_get_nc(pte))
		return PAGE_HOME_IMMUTABLE;
	switch (hv_pte_get_mode(pte)) {
	case HV_PTE_MODE_CACHE_TILE_L3:
		return get_remote_cache_cpu(pte);
	case HV_PTE_MODE_CACHE_NO_L3:
		return PAGE_HOME_INCOHERENT;
	case HV_PTE_MODE_UNCACHED:
		return PAGE_HOME_UNCACHED;
#if CHIP_HAS_CBOX_HOME_MAP()
	case HV_PTE_MODE_CACHE_HASH_L3:
		return PAGE_HOME_HASH;
#endif
	}
	panic("Bad PTE %#llx\n", pte.val);
}

/* Update the home of a PTE if necessary (can also be used for a pgprot_t). */
pte_t pte_set_home(pte_t pte, int home)
{
	/* Check for non-linear file mapping "PTEs" and pass them through. */
	if (pte_file(pte))
		return pte;

#if CHIP_HAS_MMIO()
	/* Check for MMIO mappings and pass them through. */
	if (hv_pte_get_mode(pte) == HV_PTE_MODE_MMIO)
		return pte;
#endif


	/*
	 * Only immutable pages get NC mappings.  If we have a
	 * non-coherent PTE, but the underlying page is not
	 * immutable, it's likely the result of a forced
	 * caching setting running up against ptrace setting
	 * the page to be writable underneath.  In this case,
	 * just keep the PTE coherent.
	 */
	if (hv_pte_get_nc(pte) && home != PAGE_HOME_IMMUTABLE) {
		pte = hv_pte_clear_nc(pte);
		pr_err("non-immutable page incoherently referenced: %#llx\n",
		       pte.val);
	}

	switch (home) {

	case PAGE_HOME_UNCACHED:
		pte = hv_pte_set_mode(pte, HV_PTE_MODE_UNCACHED);
		break;

	case PAGE_HOME_INCOHERENT:
		pte = hv_pte_set_mode(pte, HV_PTE_MODE_CACHE_NO_L3);
		break;

	case PAGE_HOME_IMMUTABLE:
		/*
		 * We could home this page anywhere, since it's immutable,
		 * but by default just home it to follow "hash_default".
		 */
		BUG_ON(hv_pte_get_writable(pte));
		if (pte_get_forcecache(pte)) {
			/* Upgrade "force any cpu" to "No L3" for immutable. */
			if (hv_pte_get_mode(pte) == HV_PTE_MODE_CACHE_TILE_L3
			    && pte_get_anyhome(pte)) {
				pte = hv_pte_set_mode(pte,
						      HV_PTE_MODE_CACHE_NO_L3);
			}
		} else
#if CHIP_HAS_CBOX_HOME_MAP()
		if (hash_default)
			pte = hv_pte_set_mode(pte, HV_PTE_MODE_CACHE_HASH_L3);
		else
#endif
			pte = hv_pte_set_mode(pte, HV_PTE_MODE_CACHE_NO_L3);
		pte = hv_pte_set_nc(pte);
		break;

#if CHIP_HAS_CBOX_HOME_MAP()
	case PAGE_HOME_HASH:
		pte = hv_pte_set_mode(pte, HV_PTE_MODE_CACHE_HASH_L3);
		break;
#endif

	default:
		BUG_ON(home < 0 || home >= NR_CPUS ||
		       !cpu_is_valid_lotar(home));
		pte = hv_pte_set_mode(pte, HV_PTE_MODE_CACHE_TILE_L3);
		pte = set_remote_cache_cpu(pte, home);
		break;
	}

#if CHIP_HAS_NC_AND_NOALLOC_BITS()
	if (noallocl2)
		pte = hv_pte_set_no_alloc_l2(pte);

	/* Simplify "no local and no l3" to "uncached" */
	if (hv_pte_get_no_alloc_l2(pte) && hv_pte_get_no_alloc_l1(pte) &&
	    hv_pte_get_mode(pte) == HV_PTE_MODE_CACHE_NO_L3) {
		pte = hv_pte_set_mode(pte, HV_PTE_MODE_UNCACHED);
	}
#endif

	/* Checking this case here gives a better panic than from the hv. */
	BUG_ON(hv_pte_get_mode(pte) == 0);

	return pte;
}
EXPORT_SYMBOL(pte_set_home);

/*
 * The routines in this section are the "static" versions of the normal
 * dynamic homecaching routines; they just set the home cache
 * of a kernel page once, and require a full-chip cache/TLB flush,
 * so they're not suitable for anything but infrequent use.
 */

#if CHIP_HAS_CBOX_HOME_MAP()
static inline int initial_page_home(void) { return PAGE_HOME_HASH; }
#else
static inline int initial_page_home(void) { return 0; }
#endif

int page_home(struct page *page)
{
	if (PageHighMem(page)) {
		return initial_page_home();
	} else {
		unsigned long kva = (unsigned long)page_address(page);
		return pte_to_home(*virt_to_pte(NULL, kva));
	}
}
EXPORT_SYMBOL(page_home);

void homecache_change_page_home(struct page *page, int order, int home)
{
	int i, pages = (1 << order);
	unsigned long kva;

	BUG_ON(PageHighMem(page));
	BUG_ON(page_count(page) > 1);
	BUG_ON(page_mapcount(page) != 0);
	kva = (unsigned long) page_address(page);
	flush_remote(0, HV_FLUSH_EVICT_L2, &cpu_cacheable_map,
		     kva, pages * PAGE_SIZE, PAGE_SIZE, cpu_online_mask,
		     NULL, 0);

	for (i = 0; i < pages; ++i, kva += PAGE_SIZE) {
		pte_t *ptep = virt_to_pte(NULL, kva);
		pte_t pteval = *ptep;
		BUG_ON(!pte_present(pteval) || pte_huge(pteval));
		__set_pte(ptep, pte_set_home(pteval, home));
	}
}

struct page *homecache_alloc_pages(gfp_t gfp_mask,
				   unsigned int order, int home)
{
	struct page *page;
	BUG_ON(gfp_mask & __GFP_HIGHMEM);   /* must be lowmem */
	page = alloc_pages(gfp_mask, order);
	if (page)
		homecache_change_page_home(page, order, home);
	return page;
}
EXPORT_SYMBOL(homecache_alloc_pages);

struct page *homecache_alloc_pages_node(int nid, gfp_t gfp_mask,
					unsigned int order, int home)
{
	struct page *page;
	BUG_ON(gfp_mask & __GFP_HIGHMEM);   /* must be lowmem */
	page = alloc_pages_node(nid, gfp_mask, order);
	if (page)
		homecache_change_page_home(page, order, home);
	return page;
}

void __homecache_free_pages(struct page *page, unsigned int order)
{
	if (put_page_testzero(page)) {
		homecache_change_page_home(page, order, initial_page_home());
		if (order == 0) {
			free_hot_cold_page(page, 0);
		} else {
			init_page_count(page);
			__free_pages(page, order);
		}
	}
}
EXPORT_SYMBOL(__homecache_free_pages);

void homecache_free_pages(unsigned long addr, unsigned int order)
{
	if (addr != 0) {
		VM_BUG_ON(!virt_addr_valid((void *)addr));
		__homecache_free_pages(virt_to_page((void *)addr), order);
	}
}
EXPORT_SYMBOL(homecache_free_pages);
