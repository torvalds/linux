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
 * Handle issues around the Tile "home cache" model of coherence.
 */

#ifndef _ASM_TILE_HOMECACHE_H
#define _ASM_TILE_HOMECACHE_H

#include <asm/page.h>
#include <linux/cpumask.h>

struct page;
struct task_struct;
struct vm_area_struct;
struct zone;

/*
 * Coherence point for the page is its memory controller.
 * It is not present in any cache (L1 or L2).
 */
#define PAGE_HOME_UNCACHED -1

/*
 * Is this page immutable (unwritable) and thus able to be cached more
 * widely than would otherwise be possible?  On tile64 this means we
 * mark the PTE to cache locally; on tilepro it means we have "nc" set.
 */
#define PAGE_HOME_IMMUTABLE -2

/*
 * Each cpu considers its own cache to be the home for the page,
 * which makes it incoherent.
 */
#define PAGE_HOME_INCOHERENT -3

#if CHIP_HAS_CBOX_HOME_MAP()
/* Home for the page is distributed via hash-for-home. */
#define PAGE_HOME_HASH -4
#endif

/* Homing is unknown or unspecified.  Not valid for page_home(). */
#define PAGE_HOME_UNKNOWN -5

/* Home on the current cpu.  Not valid for page_home(). */
#define PAGE_HOME_HERE -6

/* Support wrapper to use instead of explicit hv_flush_remote(). */
extern void flush_remote(unsigned long cache_pfn, unsigned long cache_length,
			 const struct cpumask *cache_cpumask,
			 HV_VirtAddr tlb_va, unsigned long tlb_length,
			 unsigned long tlb_pgsize,
			 const struct cpumask *tlb_cpumask,
			 HV_Remote_ASID *asids, int asidcount);

/* Set homing-related bits in a PTE (can also pass a pgprot_t). */
extern pte_t pte_set_home(pte_t pte, int home);

/* Do a cache eviction on the specified cpus. */
extern void homecache_evict(const struct cpumask *mask);

/*
 * Change a kernel page's homecache.  It must not be mapped in user space.
 * If !CONFIG_HOMECACHE, only usable on LOWMEM, and can only be called when
 * no other cpu can reference the page, and causes a full-chip cache/TLB flush.
 */
extern void homecache_change_page_home(struct page *, int order, int home);

/*
 * Flush a page out of whatever cache(s) it is in.
 * This is more than just finv, since it properly handles waiting
 * for the data to reach memory, but it can be quite
 * heavyweight, particularly on incoherent or immutable memory.
 */
extern void homecache_finv_page(struct page *);

/*
 * Flush a page out of the specified home cache.
 * Note that the specified home need not be the actual home of the page,
 * as for example might be the case when coordinating with I/O devices.
 */
extern void homecache_finv_map_page(struct page *, int home);

/*
 * Allocate a page with the given GFP flags, home, and optionally
 * node.  These routines are actually just wrappers around the normal
 * alloc_pages() / alloc_pages_node() functions, which set and clear
 * a per-cpu variable to communicate with homecache_new_kernel_page().
 * If !CONFIG_HOMECACHE, uses homecache_change_page_home().
 */
extern struct page *homecache_alloc_pages(gfp_t gfp_mask,
					  unsigned int order, int home);
extern struct page *homecache_alloc_pages_node(int nid, gfp_t gfp_mask,
					       unsigned int order, int home);
#define homecache_alloc_page(gfp_mask, home) \
  homecache_alloc_pages(gfp_mask, 0, home)

/*
 * These routines are just pass-throughs to free_pages() when
 * we support full homecaching.  If !CONFIG_HOMECACHE, then these
 * routines use homecache_change_page_home() to reset the home
 * back to the default before returning the page to the allocator.
 */
void __homecache_free_pages(struct page *, unsigned int order);
void homecache_free_pages(unsigned long addr, unsigned int order);
#define __homecache_free_page(page) __homecache_free_pages((page), 0)
#define homecache_free_page(page) homecache_free_pages((page), 0)


/*
 * Report the page home for LOWMEM pages by examining their kernel PTE,
 * or for highmem pages as the default home.
 */
extern int page_home(struct page *);

#define homecache_migrate_kthread() do {} while (0)

#define homecache_kpte_lock() 0
#define homecache_kpte_unlock(flags) do {} while (0)


#endif /* _ASM_TILE_HOMECACHE_H */
