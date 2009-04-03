/*
 * include/asm-xtensa/cacheflush.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * (C) 2001 - 2007 Tensilica Inc.
 */

#ifndef _XTENSA_CACHEFLUSH_H
#define _XTENSA_CACHEFLUSH_H

#ifdef __KERNEL__

#include <linux/mm.h>
#include <asm/processor.h>
#include <asm/page.h>

/*
 * Lo-level routines for cache flushing.
 *
 * invalidate data or instruction cache:
 *
 * __invalidate_icache_all()
 * __invalidate_icache_page(adr)
 * __invalidate_dcache_page(adr)
 * __invalidate_icache_range(from,size)
 * __invalidate_dcache_range(from,size)
 *
 * flush data cache:
 *
 * __flush_dcache_page(adr)
 *
 * flush and invalidate data cache:
 *
 * __flush_invalidate_dcache_all()
 * __flush_invalidate_dcache_page(adr)
 * __flush_invalidate_dcache_range(from,size)
 *
 * specials for cache aliasing:
 *
 * __flush_invalidate_dcache_page_alias(vaddr,paddr)
 * __invalidate_icache_page_alias(vaddr,paddr)
 */

extern void __invalidate_dcache_all(void);
extern void __invalidate_icache_all(void);
extern void __invalidate_dcache_page(unsigned long);
extern void __invalidate_icache_page(unsigned long);
extern void __invalidate_icache_range(unsigned long, unsigned long);
extern void __invalidate_dcache_range(unsigned long, unsigned long);


#if XCHAL_DCACHE_IS_WRITEBACK
extern void __flush_invalidate_dcache_all(void);
extern void __flush_dcache_page(unsigned long);
extern void __flush_dcache_range(unsigned long, unsigned long);
extern void __flush_invalidate_dcache_page(unsigned long);
extern void __flush_invalidate_dcache_range(unsigned long, unsigned long);
#else
# define __flush_dcache_range(p,s)		do { } while(0)
# define __flush_dcache_page(p)			do { } while(0)
# define __flush_invalidate_dcache_page(p) 	__invalidate_dcache_page(p)
# define __flush_invalidate_dcache_range(p,s)	__invalidate_dcache_range(p,s)
#endif

#if defined(CONFIG_MMU) && (DCACHE_WAY_SIZE > PAGE_SIZE)
extern void __flush_invalidate_dcache_page_alias(unsigned long, unsigned long);
#else
static inline void __flush_invalidate_dcache_page_alias(unsigned long virt,
							unsigned long phys) { }
#endif
#if defined(CONFIG_MMU) && (ICACHE_WAY_SIZE > PAGE_SIZE)
extern void __invalidate_icache_page_alias(unsigned long, unsigned long);
#else
static inline void __invalidate_icache_page_alias(unsigned long virt,
						unsigned long phys) { }
#endif

/*
 * We have physically tagged caches - nothing to do here -
 * unless we have cache aliasing.
 *
 * Pages can get remapped. Because this might change the 'color' of that page,
 * we have to flush the cache before the PTE is changed.
 * (see also Documentation/cachetlb.txt)
 */

#if (DCACHE_WAY_SIZE > PAGE_SIZE)

#define flush_cache_all()						\
	do {								\
		__flush_invalidate_dcache_all();			\
		__invalidate_icache_all();				\
	} while (0)

#define flush_cache_mm(mm)		flush_cache_all()
#define flush_cache_dup_mm(mm)		flush_cache_mm(mm)

#define flush_cache_vmap(start,end)	flush_cache_all()
#define flush_cache_vunmap(start,end)	flush_cache_all()

extern void flush_dcache_page(struct page*);
extern void flush_cache_range(struct vm_area_struct*, ulong, ulong);
extern void flush_cache_page(struct vm_area_struct*, unsigned long, unsigned long);

#else

#define flush_cache_all()				do { } while (0)
#define flush_cache_mm(mm)				do { } while (0)
#define flush_cache_dup_mm(mm)				do { } while (0)

#define flush_cache_vmap(start,end)			do { } while (0)
#define flush_cache_vunmap(start,end)			do { } while (0)

#define flush_dcache_page(page)				do { } while (0)

#define flush_cache_page(vma,addr,pfn)			do { } while (0)
#define flush_cache_range(vma,start,end)		do { } while (0)

#endif

/* Ensure consistency between data and instruction cache. */
#define flush_icache_range(start,end) 					\
	do {								\
		__flush_dcache_range(start, (end) - (start));		\
		__invalidate_icache_range(start,(end) - (start));	\
	} while (0)

/* This is not required, see Documentation/cachetlb.txt */
#define	flush_icache_page(vma,page)			do { } while (0)

#define flush_dcache_mmap_lock(mapping)			do { } while (0)
#define flush_dcache_mmap_unlock(mapping)		do { } while (0)

#if (DCACHE_WAY_SIZE > PAGE_SIZE)

extern void copy_to_user_page(struct vm_area_struct*, struct page*,
		unsigned long, void*, const void*, unsigned long);
extern void copy_from_user_page(struct vm_area_struct*, struct page*,
		unsigned long, void*, const void*, unsigned long);

#else

#define copy_to_user_page(vma, page, vaddr, dst, src, len)		\
	do {								\
		memcpy(dst, src, len);					\
		__flush_dcache_range((unsigned long) dst, len);		\
		__invalidate_icache_range((unsigned long) dst, len);	\
	} while (0)

#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)

#endif

#endif /* __KERNEL__ */
#endif /* _XTENSA_CACHEFLUSH_H */
