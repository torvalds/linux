/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ABI_CSKY_CACHEFLUSH_H
#define __ABI_CSKY_CACHEFLUSH_H

/* Keep includes the same across arches.  */
#include <linux/mm.h>

/*
 * The cache doesn't need to be flushed when TLB entries change when
 * the cache is mapped to physical memory, not virtual memory
 */
#define flush_cache_all()			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_dup_mm(mm)			do { } while (0)

#define flush_cache_range(vma, start, end) \
	do { \
		if (vma->vm_flags & VM_EXEC) \
			icache_inv_all(); \
	} while (0)

#define flush_cache_page(vma, vmaddr, pfn)	do { } while (0)
#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 0
#define flush_dcache_page(page)			do { } while (0)
#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

#define flush_icache_range(start, end)		cache_wbinv_range(start, end)

void flush_icache_page(struct vm_area_struct *vma, struct page *page);
void flush_icache_user_range(struct vm_area_struct *vma, struct page *page,
			     unsigned long vaddr, int len);

#define flush_cache_vmap(start, end)		do { } while (0)
#define flush_cache_vunmap(start, end)		do { } while (0)

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
do { \
	memcpy(dst, src, len); \
	cache_wbinv_range((unsigned long)dst, (unsigned long)dst + len); \
} while (0)
#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)

#endif /* __ABI_CSKY_CACHEFLUSH_H */
