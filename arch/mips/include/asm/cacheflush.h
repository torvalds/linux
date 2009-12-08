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
 *  - flush_cache_sigtramp() flush signal trampoline
 *  - flush_icache_all() flush the entire instruction cache
 *  - flush_data_cache_page() flushes a page from the data cache
 */
extern void (*flush_cache_all)(void);
extern void (*__flush_cache_all)(void);
extern void (*flush_cache_mm)(struct mm_struct *mm);
#define flush_cache_dup_mm(mm)	do { (void) (mm); } while (0)
extern void (*flush_cache_range)(struct vm_area_struct *vma,
	unsigned long start, unsigned long end);
extern void (*flush_cache_page)(struct vm_area_struct *vma, unsigned long page, unsigned long pfn);
extern void __flush_dcache_page(struct page *page);

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1
static inline void flush_dcache_page(struct page *page)
{
	if (cpu_has_dc_aliases || !cpu_has_ic_fills_f_dc)
		__flush_dcache_page(page);

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

static inline void flush_icache_page(struct vm_area_struct *vma,
	struct page *page)
{
}

extern void (*flush_icache_range)(unsigned long start, unsigned long end);
extern void (*local_flush_icache_range)(unsigned long start, unsigned long end);

extern void (*__flush_cache_vmap)(void);

static inline void flush_cache_vmap(unsigned long start, unsigned long end)
{
	if (cpu_has_dc_aliases)
		__flush_cache_vmap();
}

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

extern void (*flush_cache_sigtramp)(unsigned long addr);
extern void (*flush_icache_all)(void);
extern void (*local_flush_data_cache_page)(void * addr);
extern void (*flush_data_cache_page)(unsigned long addr);

/*
 * This flag is used to indicate that the page pointed to by a pte
 * is dirty and requires cleaning before returning it to the user.
 */
#define PG_dcache_dirty			PG_arch_1

#define Page_dcache_dirty(page)		\
	test_bit(PG_dcache_dirty, &(page)->flags)
#define SetPageDcacheDirty(page)	\
	set_bit(PG_dcache_dirty, &(page)->flags)
#define ClearPageDcacheDirty(page)	\
	clear_bit(PG_dcache_dirty, &(page)->flags)

/* Run kernel code uncached, useful for cache probing functions. */
unsigned long run_uncached(void *func);

extern void *kmap_coherent(struct page *page, unsigned long addr);
extern void kunmap_coherent(void);

#endif /* _ASM_CACHEFLUSH_H */
