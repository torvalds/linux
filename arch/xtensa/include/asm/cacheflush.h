/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * (C) 2001 - 2013 Tensilica Inc.
 */

#ifndef _XTENSA_CACHEFLUSH_H
#define _XTENSA_CACHEFLUSH_H

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
 * __invalidate_dcache_page_alias(vaddr,paddr)
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
static inline void __flush_dcache_page(unsigned long va)
{
}
static inline void __flush_dcache_range(unsigned long va, unsigned long sz)
{
}
# define __flush_invalidate_dcache_all()	__invalidate_dcache_all()
# define __flush_invalidate_dcache_page(p)	__invalidate_dcache_page(p)
# define __flush_invalidate_dcache_range(p,s)	__invalidate_dcache_range(p,s)
#endif

#if defined(CONFIG_MMU) && (DCACHE_WAY_SIZE > PAGE_SIZE)
extern void __flush_invalidate_dcache_page_alias(unsigned long, unsigned long);
extern void __invalidate_dcache_page_alias(unsigned long, unsigned long);
#else
static inline void __flush_invalidate_dcache_page_alias(unsigned long virt,
							unsigned long phys) { }
static inline void __invalidate_dcache_page_alias(unsigned long virt,
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
 * (see also Documentation/core-api/cachetlb.rst)
 */

#if defined(CONFIG_MMU) && \
	((DCACHE_WAY_SIZE > PAGE_SIZE) || defined(CONFIG_SMP))

#ifdef CONFIG_SMP
void flush_cache_all(void);
void flush_cache_range(struct vm_area_struct*, ulong, ulong);
void flush_icache_range(unsigned long start, unsigned long end);
void flush_cache_page(struct vm_area_struct*,
			     unsigned long, unsigned long);
#else
#define flush_cache_all local_flush_cache_all
#define flush_cache_range local_flush_cache_range
#define flush_icache_range local_flush_icache_range
#define flush_cache_page  local_flush_cache_page
#endif

#define local_flush_cache_all()						\
	do {								\
		__flush_invalidate_dcache_all();			\
		__invalidate_icache_all();				\
	} while (0)

#define flush_cache_mm(mm)		flush_cache_all()
#define flush_cache_dup_mm(mm)		flush_cache_mm(mm)

#define flush_cache_vmap(start,end)		flush_cache_all()
#define flush_cache_vmap_early(start,end)	do { } while (0)
#define flush_cache_vunmap(start,end)		flush_cache_all()

void flush_dcache_folio(struct folio *folio);
#define flush_dcache_folio flush_dcache_folio

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1
static inline void flush_dcache_page(struct page *page)
{
	flush_dcache_folio(page_folio(page));
}

void local_flush_cache_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end);
void local_flush_cache_page(struct vm_area_struct *vma,
		unsigned long address, unsigned long pfn);

#else

#define flush_cache_all()				do { } while (0)
#define flush_cache_mm(mm)				do { } while (0)
#define flush_cache_dup_mm(mm)				do { } while (0)

#define flush_cache_vmap(start,end)			do { } while (0)
#define flush_cache_vmap_early(start,end)		do { } while (0)
#define flush_cache_vunmap(start,end)			do { } while (0)

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 0
#define flush_dcache_page(page)				do { } while (0)

#define flush_icache_range local_flush_icache_range
#define flush_cache_page(vma, addr, pfn)		do { } while (0)
#define flush_cache_range(vma, start, end)		do { } while (0)

#endif

#define flush_icache_user_range flush_icache_range

/* Ensure consistency between data and instruction cache. */
#define local_flush_icache_range(start, end)				\
	do {								\
		__flush_dcache_range(start, (end) - (start));		\
		__invalidate_icache_range(start,(end) - (start));	\
	} while (0)

#define flush_dcache_mmap_lock(mapping)			do { } while (0)
#define flush_dcache_mmap_unlock(mapping)		do { } while (0)

#if defined(CONFIG_MMU) && (DCACHE_WAY_SIZE > PAGE_SIZE)

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

#endif /* _XTENSA_CACHEFLUSH_H */
