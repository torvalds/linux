/*
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_POWERPC_CACHEFLUSH_H
#define _ASM_POWERPC_CACHEFLUSH_H

#ifdef __KERNEL__

#include <linux/mm.h>
#include <asm/cputable.h>

/*
 * No cache flushing is required when address mappings are changed,
 * because the caches on PowerPCs are physically addressed.
 */
#define flush_cache_all()			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_dup_mm(mm)			do { } while (0)
#define flush_cache_range(vma, start, end)	do { } while (0)
#define flush_cache_page(vma, vmaddr, pfn)	do { } while (0)
#define flush_icache_page(vma, page)		do { } while (0)
#define flush_cache_vmap(start, end)		do { } while (0)
#define flush_cache_vunmap(start, end)		do { } while (0)

extern void flush_dcache_page(struct page *page);
#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

extern void __flush_icache_range(unsigned long, unsigned long);
static inline void flush_icache_range(unsigned long start, unsigned long stop)
{
	if (!cpu_has_feature(CPU_FTR_COHERENT_ICACHE))
		__flush_icache_range(start, stop);
}

extern void flush_icache_user_range(struct vm_area_struct *vma,
				    struct page *page, unsigned long addr,
				    int len);
extern void __flush_dcache_icache(void *page_va);
extern void flush_dcache_icache_page(struct page *page);
#if defined(CONFIG_PPC32) && !defined(CONFIG_BOOKE)
extern void __flush_dcache_icache_phys(unsigned long physaddr);
#endif /* CONFIG_PPC32 && !CONFIG_BOOKE */

extern void flush_dcache_range(unsigned long start, unsigned long stop);
#ifdef CONFIG_PPC32
extern void clean_dcache_range(unsigned long start, unsigned long stop);
extern void invalidate_dcache_range(unsigned long start, unsigned long stop);
#endif /* CONFIG_PPC32 */
#ifdef CONFIG_PPC64
extern void flush_inval_dcache_range(unsigned long start, unsigned long stop);
extern void flush_dcache_phys_range(unsigned long start, unsigned long stop);
#endif

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
	do { \
		memcpy(dst, src, len); \
		flush_icache_user_range(vma, page, vaddr, len); \
	} while (0)
#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)



#ifdef CONFIG_DEBUG_PAGEALLOC
/* internal debugging function */
void kernel_map_pages(struct page *page, int numpages, int enable);
#endif

#endif /* __KERNEL__ */

#endif /* _ASM_POWERPC_CACHEFLUSH_H */
