/*
 * Copyright (C) 2003 Microtronix Datacom Ltd.
 * Copyright (C) 2000-2002 Greg Ungerer <gerg@snapgear.com>
 *
 * Ported from m68knommu.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_CACHEFLUSH_H
#define _ASM_NIOS2_CACHEFLUSH_H

#include <linux/mm_types.h>

/*
 * This flag is used to indicate that the page pointed to by a pte is clean
 * and does not require cleaning before returning it to the user.
 */
#define PG_dcache_clean PG_arch_1

struct mm_struct;

extern void flush_cache_all(void);
extern void flush_cache_mm(struct mm_struct *mm);
extern void flush_cache_dup_mm(struct mm_struct *mm);
extern void flush_cache_range(struct vm_area_struct *vma, unsigned long start,
	unsigned long end);
extern void flush_cache_page(struct vm_area_struct *vma, unsigned long vmaddr,
	unsigned long pfn);
#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1
extern void flush_dcache_page(struct page *page);

extern void flush_icache_range(unsigned long start, unsigned long end);
extern void flush_icache_page(struct vm_area_struct *vma, struct page *page);

#define flush_cache_vmap(start, end)		flush_dcache_range(start, end)
#define flush_cache_vunmap(start, end)		flush_dcache_range(start, end)

extern void copy_to_user_page(struct vm_area_struct *vma, struct page *page,
				unsigned long user_vaddr,
				void *dst, void *src, int len);
extern void copy_from_user_page(struct vm_area_struct *vma, struct page *page,
				unsigned long user_vaddr,
				void *dst, void *src, int len);

extern void flush_dcache_range(unsigned long start, unsigned long end);
extern void invalidate_dcache_range(unsigned long start, unsigned long end);

#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

#endif /* _ASM_NIOS2_CACHEFLUSH_H */
