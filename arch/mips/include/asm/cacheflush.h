/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 95, 96, 97, 98, 99, 2000, 01, 02, 03 by Ralf Baechle
 * Copyright (C) 1999, 2000, 2001 Silicon Graphics, Inc.
 */
#ifndef _ASM_CACHEFLUSH_H
#define _ASM_CACHEFLUSH_H

/* Keep includes the same across arches.  */
#include <linux/mm.h>
#include <asm/cpu-features.h>

/* Cache flushing:
 *
 *  - flush_cache_all() flushes entire cache
 *  - flush_cache_mm(mm) flushes the specified mm context's cache lines
 *  - flush_cache_dup mm(mm) handles cache flushing when forking
 *  - flush_cache_page(mm, vmaddr, pfn) flushes a single page
 *  - flush_cache_range(vma, start, end) flushes a range of pages
 *  - flush_icache_range(start, end) flush a range of instructions
 *  - flush_dcache_page(pg) flushes(wback&invalidates) a page for dcache
 *
 * MIPS specific flush operations:
 *
 *  - flush_icache_all() flush the entire instruction cache
 *  - flush_data_cache_page() flushes a page from the data cache
 *  - __flush_icache_user_range(start, end) flushes range of user instructions
 */

 /*
 * This flag is used to indicate that the page pointed to by a pte
 * is dirty and requires cleaning before returning it to the user.
 */
#define PG_dcache_dirty			PG_arch_1

#define folio_test_dcache_dirty(folio)		\
	test_bit(PG_dcache_dirty, &(folio)->flags.f)
#define folio_set_dcache_dirty(folio)	\
	set_bit(PG_dcache_dirty, &(folio)->flags.f)
#define folio_clear_dcache_dirty(folio)	\
	clear_bit(PG_dcache_dirty, &(folio)->flags.f)

extern void (*flush_cache_all)(void);
extern void (*__flush_cache_all)(void);
extern void (*flush_cache_mm)(struct mm_struct *mm);
#define flush_cache_dup_mm(mm)	do { (void) (mm); } while (0)
extern void (*flush_cache_range)(struct vm_area_struct *vma,
	unsigned long start, unsigned long end);
extern void (*flush_cache_page)(struct vm_area_struct *vma, unsigned long page, unsigned long pfn);
void __flush_dcache_folio_pages(struct folio *folio, struct page *page, unsigned int nr);

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1
static inline void flush_dcache_folio(struct folio *folio)
{
	if (cpu_has_dc_aliases)
		__flush_dcache_folio_pages(folio, folio_page(folio, 0),
					   folio_nr_pages(folio));
	else if (!cpu_has_ic_fills_f_dc)
		folio_set_dcache_dirty(folio);
}
#define flush_dcache_folio flush_dcache_folio

static inline void flush_dcache_page(struct page *page)
{
	struct folio *folio = page_folio(page);

	if (cpu_has_dc_aliases)
		__flush_dcache_folio_pages(folio, page, 1);
	else if (!cpu_has_ic_fills_f_dc)
		folio_set_dcache_dirty(folio);
}

#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

#define ARCH_HAS_FLUSH_ANON_PAGE
extern void __flush_anon_page(struct page *, unsigned long);
static inline void flush_anon_page(struct vm_area_struct *vma,
	struct page *page, unsigned long vmaddr)
{
	if (cpu_has_dc_aliases && PageAnon(page))
		__flush_anon_page(page, vmaddr);
}

extern void (*flush_icache_range)(unsigned long start, unsigned long end);
extern void (*local_flush_icache_range)(unsigned long start, unsigned long end);
extern void (*__flush_icache_user_range)(unsigned long start,
					 unsigned long end);
extern void (*__local_flush_icache_user_range)(unsigned long start,
					       unsigned long end);

extern void (*__flush_cache_vmap)(void);

static inline void flush_cache_vmap(unsigned long start, unsigned long end)
{
	if (cpu_has_dc_aliases)
		__flush_cache_vmap();
}

#define flush_cache_vmap_early(start, end)     do { } while (0)

extern void (*__flush_cache_vunmap)(void);

static inline void flush_cache_vunmap(unsigned long start, unsigned long end)
{
	if (cpu_has_dc_aliases)
		__flush_cache_vunmap();
}

extern void copy_to_user_page(struct vm_area_struct *vma,
	struct page *page, unsigned long vaddr, void *dst, const void *src,
	unsigned long len);

extern void copy_from_user_page(struct vm_area_struct *vma,
	struct page *page, unsigned long vaddr, void *dst, const void *src,
	unsigned long len);

extern void (*flush_icache_all)(void);
extern void (*flush_data_cache_page)(unsigned long addr);

/* Run kernel code uncached, useful for cache probing functions. */
unsigned long run_uncached(void *func);

extern void *kmap_coherent(struct page *page, unsigned long addr);
extern void kunmap_coherent(void);
extern void *kmap_noncoherent(struct page *page, unsigned long addr);

static inline void kunmap_noncoherent(void)
{
	kunmap_coherent();
}

#define ARCH_IMPLEMENTS_FLUSH_KERNEL_VMAP_RANGE 1
/*
 * For now flush_kernel_vmap_range and invalidate_kernel_vmap_range both do a
 * cache writeback and invalidate operation.
 */
extern void (*__flush_kernel_vmap_range)(unsigned long vaddr, int size);

static inline void flush_kernel_vmap_range(void *vaddr, int size)
{
	if (cpu_has_dc_aliases)
		__flush_kernel_vmap_range((unsigned long) vaddr, size);
}

static inline void invalidate_kernel_vmap_range(void *vaddr, int size)
{
	if (cpu_has_dc_aliases)
		__flush_kernel_vmap_range((unsigned long) vaddr, size);
}

#endif /* _ASM_CACHEFLUSH_H */
