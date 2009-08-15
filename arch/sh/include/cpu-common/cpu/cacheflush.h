/*
 * Copyright (C) 2003 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_CPU_SH_CACHEFLUSH_H
#define __ASM_CPU_SH_CACHEFLUSH_H

/*
 * Cache flushing:
 *
 *  - flush_cache_all() flushes entire cache
 *  - flush_cache_mm(mm) flushes the specified mm context's cache lines
 *  - flush_cache_dup mm(mm) handles cache flushing when forking
 *  - flush_cache_page(mm, vmaddr, pfn) flushes a single page
 *  - flush_cache_range(vma, start, end) flushes a range of pages
 *
 *  - flush_dcache_page(pg) flushes(wback&invalidates) a page for dcache
 *  - flush_icache_range(start, end) flushes(invalidates) a range for icache
 *  - flush_icache_page(vma, pg) flushes(invalidates) a page for icache
 *  - flush_cache_sigtramp(vaddr) flushes the signal trampoline
 */
extern void (*flush_cache_all)(void);
extern void (*flush_cache_mm)(struct mm_struct *mm);
extern void (*flush_cache_dup_mm)(struct mm_struct *mm);
extern void (*flush_cache_page)(struct vm_area_struct *vma,
				unsigned long addr, unsigned long pfn);
extern void (*flush_cache_range)(struct vm_area_struct *vma,
				 unsigned long start, unsigned long end);
extern void (*flush_dcache_page)(struct page *page);
extern void (*flush_icache_range)(unsigned long start, unsigned long end);
extern void (*flush_icache_page)(struct vm_area_struct *vma,
				 struct page *page);
extern void (*flush_cache_sigtramp)(unsigned long address);

extern void (*__flush_wback_region)(void *start, int size);
extern void (*__flush_purge_region)(void *start, int size);
extern void (*__flush_invalidate_region)(void *start, int size);

#endif /* __ASM_CPU_SH_CACHEFLUSH_H */
