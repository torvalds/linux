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

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1
extern void flush_dcache_page(struct page*);
extern void flush_cache_range(struct vm_area_struct*, ulong, ulong);
extern void flush_cache_page(struct vm_area_struct*,
			     unsigned long, unsigned long);

#else

#define flush_cache_all()				do { } while (0)
#define flush_cache_mm(mm)				do { } while (0)
#define flush_cache_dup_mm(mm)				do { } while (0)

#define flush_cache_vmap(start,end)			do { } while (0)
#define flush_cache_vunmap(start,end)			do { } while (0)

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 0
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

#define XTENSA_CACHEBLK_LOG2	29
#define XTENSA_CACHEBLK_SIZE	(1 << XTENSA_CACHEBLK_LOG2)
#define XTENSA_CACHEBLK_MASK	(7 << XTENSA_CACHEBLK_LOG2)

#if XCHAL_HAVE_CACHEATTR
static inline u32 xtensa_get_cacheattr(void)
{
	u32 r;
	asm volatile("	rsr %0, cacheattr" : "=a"(r));
	return r;
}

static inline u32 xtensa_get_dtlb1(u32 addr)
{
	u32 r = addr & XTENSA_CACHEBLK_MASK;
	return r | ((xtensa_get_cacheattr() >> (r >> (XTENSA_CACHEBLK_LOG2-2)))
			& 0xF);
}
#else
static inline u32 xtensa_get_dtlb1(u32 addr)
{
	u32 r;
	asm volatile("	rdtlb1 %0, %1" : "=a"(r) : "a"(addr));
	asm volatile("	dsync");
	return r;
}

static inline u32 xtensa_get_cacheattr(void)
{
	u32 r = 0;
	u32 a = 0;
	do {
		a -= XTENSA_CACHEBLK_SIZE;
		r = (r << 4) | (xtensa_get_dtlb1(a) & 0xF);
	} while (a);
	return r;
}
#endif

static inline int xtensa_need_flush_dma_source(u32 addr)
{
	return (xtensa_get_dtlb1(addr) & ((1 << XCHAL_CA_BITS) - 1)) >= 4;
}

static inline int xtensa_need_invalidate_dma_destination(u32 addr)
{
	return (xtensa_get_dtlb1(addr) & ((1 << XCHAL_CA_BITS) - 1)) != 2;
}

static inline void flush_dcache_unaligned(u32 addr, u32 size)
{
	u32 cnt;
	if (size) {
		cnt = (size + ((XCHAL_DCACHE_LINESIZE - 1) & addr)
			+ XCHAL_DCACHE_LINESIZE - 1) / XCHAL_DCACHE_LINESIZE;
		while (cnt--) {
			asm volatile("	dhwb %0, 0" : : "a"(addr));
			addr += XCHAL_DCACHE_LINESIZE;
		}
		asm volatile("	dsync");
	}
}

static inline void invalidate_dcache_unaligned(u32 addr, u32 size)
{
	int cnt;
	if (size) {
		asm volatile("	dhwbi %0, 0 ;" : : "a"(addr));
		cnt = (size + ((XCHAL_DCACHE_LINESIZE - 1) & addr)
			- XCHAL_DCACHE_LINESIZE - 1) / XCHAL_DCACHE_LINESIZE;
		while (cnt-- > 0) {
			asm volatile("	dhi %0, %1" : : "a"(addr),
						"n"(XCHAL_DCACHE_LINESIZE));
			addr += XCHAL_DCACHE_LINESIZE;
		}
		asm volatile("	dhwbi %0, %1" : : "a"(addr),
						"n"(XCHAL_DCACHE_LINESIZE));
		asm volatile("	dsync");
	}
}

static inline void flush_invalidate_dcache_unaligned(u32 addr, u32 size)
{
	u32 cnt;
	if (size) {
		cnt = (size + ((XCHAL_DCACHE_LINESIZE - 1) & addr)
			+ XCHAL_DCACHE_LINESIZE - 1) / XCHAL_DCACHE_LINESIZE;
		while (cnt--) {
			asm volatile("	dhwbi %0, 0" : : "a"(addr));
			addr += XCHAL_DCACHE_LINESIZE;
		}
		asm volatile("	dsync");
	}
}

#endif /* __KERNEL__ */
#endif /* _XTENSA_CACHEFLUSH_H */
