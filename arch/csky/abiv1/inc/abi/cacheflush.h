/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef __ABI_CSKY_CACHEFLUSH_H
#define __ABI_CSKY_CACHEFLUSH_H

#include <linux/mm.h>
#include <asm/string.h>
#include <asm/cache.h>

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1
extern void flush_dcache_page(struct page *);

#define flush_cache_mm(mm)			dcache_wbinv_all()
#define flush_cache_page(vma, page, pfn)	cache_wbinv_all()
#define flush_cache_dup_mm(mm)			cache_wbinv_all()

#define ARCH_HAS_FLUSH_KERNEL_DCACHE_PAGE
extern void flush_kernel_dcache_page(struct page *);

#define flush_dcache_mmap_lock(mapping)		xa_lock_irq(&mapping->i_pages)
#define flush_dcache_mmap_unlock(mapping)	xa_unlock_irq(&mapping->i_pages)

static inline void flush_kernel_vmap_range(void *addr, int size)
{
	dcache_wbinv_all();
}
static inline void invalidate_kernel_vmap_range(void *addr, int size)
{
	dcache_wbinv_all();
}

#define ARCH_HAS_FLUSH_ANON_PAGE
static inline void flush_anon_page(struct vm_area_struct *vma,
			 struct page *page, unsigned long vmaddr)
{
	if (PageAnon(page))
		cache_wbinv_all();
}

/*
 * if (current_mm != vma->mm) cache_wbinv_range(start, end) will be broken.
 * Use cache_wbinv_all() here and need to be improved in future.
 */
extern void flush_cache_range(struct vm_area_struct *vma, unsigned long start, unsigned long end);
#define flush_cache_vmap(start, end)		cache_wbinv_all()
#define flush_cache_vunmap(start, end)		cache_wbinv_all()

#define flush_icache_page(vma, page)		do {} while (0);
#define flush_icache_range(start, end)		cache_wbinv_range(start, end)

#define flush_icache_user_range(vma,page,addr,len) \
	flush_dcache_page(page)

#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
do { \
	memcpy(dst, src, len); \
} while (0)

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
do { \
	memcpy(dst, src, len); \
	cache_wbinv_all(); \
} while (0)

#endif /* __ABI_CSKY_CACHEFLUSH_H */
