/*
 *  arch/arm/include/asm/cacheflush.h
 *
 *  Copyright (C) 1999-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _ASMARM_CACHEFLUSH_H
#define _ASMARM_CACHEFLUSH_H

#include <linux/mm.h>

#include <asm/glue.h>
#include <asm/shmparam.h>
#include <asm/cachetype.h>

#define CACHE_COLOUR(vaddr)	((vaddr & (SHMLBA - 1)) >> PAGE_SHIFT)

/*
 *	Cache Model
 *	===========
 */
#undef _CACHE
#undef MULTI_CACHE

#if defined(CONFIG_CPU_CACHE_V3)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE v3
# endif
#endif

#if defined(CONFIG_CPU_CACHE_V4)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE v4
# endif
#endif

#if defined(CONFIG_CPU_ARM920T) || defined(CONFIG_CPU_ARM922T) || \
    defined(CONFIG_CPU_ARM925T) || defined(CONFIG_CPU_ARM1020) || \
    defined(CONFIG_CPU_ARM1026)
# define MULTI_CACHE 1
#endif

#if defined(CONFIG_CPU_FA526)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE fa
# endif
#endif

#if defined(CONFIG_CPU_ARM926T)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE arm926
# endif
#endif

#if defined(CONFIG_CPU_ARM940T)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE arm940
# endif
#endif

#if defined(CONFIG_CPU_ARM946E)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE arm946
# endif
#endif

#if defined(CONFIG_CPU_CACHE_V4WB)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE v4wb
# endif
#endif

#if defined(CONFIG_CPU_XSCALE)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE xscale
# endif
#endif

#if defined(CONFIG_CPU_XSC3)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE xsc3
# endif
#endif

#if defined(CONFIG_CPU_MOHAWK)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE mohawk
# endif
#endif

#if defined(CONFIG_CPU_FEROCEON)
# define MULTI_CACHE 1
#endif

#if defined(CONFIG_CPU_V6)
//# ifdef _CACHE
#  define MULTI_CACHE 1
//# else
//#  define _CACHE v6
//# endif
#endif

#if defined(CONFIG_CPU_V7)
//# ifdef _CACHE
#  define MULTI_CACHE 1
//# else
//#  define _CACHE v7
//# endif
#endif

#if !defined(_CACHE) && !defined(MULTI_CACHE)
#error Unknown cache maintainence model
#endif

/*
 * This flag is used to indicate that the page pointed to by a pte
 * is dirty and requires cleaning before returning it to the user.
 */
#define PG_dcache_dirty PG_arch_1

/*
 *	MM Cache Management
 *	===================
 *
 *	The arch/arm/mm/cache-*.S and arch/arm/mm/proc-*.S files
 *	implement these methods.
 *
 *	Start addresses are inclusive and end addresses are exclusive;
 *	start addresses should be rounded down, end addresses up.
 *
 *	See Documentation/cachetlb.txt for more information.
 *	Please note that the implementation of these, and the required
 *	effects are cache-type (VIVT/VIPT/PIPT) specific.
 *
 *	flush_kern_all()
 *
 *		Unconditionally clean and invalidate the entire cache.
 *
 *	flush_user_all()
 *
 *		Clean and invalidate all user space cache entries
 *		before a change of page tables.
 *
 *	flush_user_range(start, end, flags)
 *
 *		Clean and invalidate a range of cache entries in the
 *		specified address space before a change of page tables.
 *		- start - user start address (inclusive, page aligned)
 *		- end   - user end address   (exclusive, page aligned)
 *		- flags - vma->vm_flags field
 *
 *	coherent_kern_range(start, end)
 *
 *		Ensure coherency between the Icache and the Dcache in the
 *		region described by start, end.  If you have non-snooping
 *		Harvard caches, you need to implement this function.
 *		- start  - virtual start address
 *		- end    - virtual end address
 *
 *	coherent_user_range(start, end)
 *
 *		Ensure coherency between the Icache and the Dcache in the
 *		region described by start, end.  If you have non-snooping
 *		Harvard caches, you need to implement this function.
 *		- start  - virtual start address
 *		- end    - virtual end address
 *
 *	flush_kern_dcache_area(kaddr, size)
 *
 *		Ensure that the data held in page is written back.
 *		- kaddr  - page address
 *		- size   - region size
 *
 *	DMA Cache Coherency
 *	===================
 *
 *	dma_flush_range(start, end)
 *
 *		Clean and invalidate the specified virtual address range.
 *		- start  - virtual start address
 *		- end    - virtual end address
 */

struct cpu_cache_fns {
	void (*flush_kern_all)(void);
	void (*flush_user_all)(void);
	void (*flush_user_range)(unsigned long, unsigned long, unsigned int);

	void (*coherent_kern_range)(unsigned long, unsigned long);
	void (*coherent_user_range)(unsigned long, unsigned long);
	void (*flush_kern_dcache_area)(void *, size_t);

	void (*dma_map_area)(const void *, size_t, int);
	void (*dma_unmap_area)(const void *, size_t, int);

	void (*dma_flush_range)(const void *, const void *);
};

struct outer_cache_fns {
	void (*inv_range)(unsigned long, unsigned long);
	void (*clean_range)(unsigned long, unsigned long);
	void (*flush_range)(unsigned long, unsigned long);
};

/*
 * Select the calling method
 */
#ifdef MULTI_CACHE

extern struct cpu_cache_fns cpu_cache;

#define __cpuc_flush_kern_all		cpu_cache.flush_kern_all
#define __cpuc_flush_user_all		cpu_cache.flush_user_all
#define __cpuc_flush_user_range		cpu_cache.flush_user_range
#define __cpuc_coherent_kern_range	cpu_cache.coherent_kern_range
#define __cpuc_coherent_user_range	cpu_cache.coherent_user_range
#define __cpuc_flush_dcache_area	cpu_cache.flush_kern_dcache_area

/*
 * These are private to the dma-mapping API.  Do not use directly.
 * Their sole purpose is to ensure that data held in the cache
 * is visible to DMA, or data written by DMA to system memory is
 * visible to the CPU.
 */
#define dmac_map_area			cpu_cache.dma_map_area
#define dmac_unmap_area		cpu_cache.dma_unmap_area
#define dmac_flush_range		cpu_cache.dma_flush_range

#else

#define __cpuc_flush_kern_all		__glue(_CACHE,_flush_kern_cache_all)
#define __cpuc_flush_user_all		__glue(_CACHE,_flush_user_cache_all)
#define __cpuc_flush_user_range		__glue(_CACHE,_flush_user_cache_range)
#define __cpuc_coherent_kern_range	__glue(_CACHE,_coherent_kern_range)
#define __cpuc_coherent_user_range	__glue(_CACHE,_coherent_user_range)
#define __cpuc_flush_dcache_area	__glue(_CACHE,_flush_kern_dcache_area)

extern void __cpuc_flush_kern_all(void);
extern void __cpuc_flush_user_all(void);
extern void __cpuc_flush_user_range(unsigned long, unsigned long, unsigned int);
extern void __cpuc_coherent_kern_range(unsigned long, unsigned long);
extern void __cpuc_coherent_user_range(unsigned long, unsigned long);
extern void __cpuc_flush_dcache_area(void *, size_t);

/*
 * These are private to the dma-mapping API.  Do not use directly.
 * Their sole purpose is to ensure that data held in the cache
 * is visible to DMA, or data written by DMA to system memory is
 * visible to the CPU.
 */
#define dmac_map_area			__glue(_CACHE,_dma_map_area)
#define dmac_unmap_area		__glue(_CACHE,_dma_unmap_area)
#define dmac_flush_range		__glue(_CACHE,_dma_flush_range)

extern void dmac_map_area(const void *, size_t, int);
extern void dmac_unmap_area(const void *, size_t, int);
extern void dmac_flush_range(const void *, const void *);

#endif

#ifdef CONFIG_OUTER_CACHE

extern struct outer_cache_fns outer_cache;

static inline void outer_inv_range(unsigned long start, unsigned long end)
{
	if (outer_cache.inv_range)
		outer_cache.inv_range(start, end);
}
static inline void outer_clean_range(unsigned long start, unsigned long end)
{
	if (outer_cache.clean_range)
		outer_cache.clean_range(start, end);
}
static inline void outer_flush_range(unsigned long start, unsigned long end)
{
	if (outer_cache.flush_range)
		outer_cache.flush_range(start, end);
}

#else

static inline void outer_inv_range(unsigned long start, unsigned long end)
{ }
static inline void outer_clean_range(unsigned long start, unsigned long end)
{ }
static inline void outer_flush_range(unsigned long start, unsigned long end)
{ }

#endif

/*
 * Copy user data from/to a page which is mapped into a different
 * processes address space.  Really, we want to allow our "user
 * space" model to handle this.
 */
extern void copy_to_user_page(struct vm_area_struct *, struct page *,
	unsigned long, void *, const void *, unsigned long);
#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	do {							\
		memcpy(dst, src, len);				\
	} while (0)

/*
 * Convert calls to our calling convention.
 */
#define flush_cache_all()		__cpuc_flush_kern_all()

static inline void vivt_flush_cache_mm(struct mm_struct *mm)
{
	if (cpumask_test_cpu(smp_processor_id(), mm_cpumask(mm)))
		__cpuc_flush_user_all();
}

static inline void
vivt_flush_cache_range(struct vm_area_struct *vma, unsigned long start, unsigned long end)
{
	if (cpumask_test_cpu(smp_processor_id(), mm_cpumask(vma->vm_mm)))
		__cpuc_flush_user_range(start & PAGE_MASK, PAGE_ALIGN(end),
					vma->vm_flags);
}

static inline void
vivt_flush_cache_page(struct vm_area_struct *vma, unsigned long user_addr, unsigned long pfn)
{
	if (cpumask_test_cpu(smp_processor_id(), mm_cpumask(vma->vm_mm))) {
		unsigned long addr = user_addr & PAGE_MASK;
		__cpuc_flush_user_range(addr, addr + PAGE_SIZE, vma->vm_flags);
	}
}

#ifndef CONFIG_CPU_CACHE_VIPT
#define flush_cache_mm(mm) \
		vivt_flush_cache_mm(mm)
#define flush_cache_range(vma,start,end) \
		vivt_flush_cache_range(vma,start,end)
#define flush_cache_page(vma,addr,pfn) \
		vivt_flush_cache_page(vma,addr,pfn)
#else
extern void flush_cache_mm(struct mm_struct *mm);
extern void flush_cache_range(struct vm_area_struct *vma, unsigned long start, unsigned long end);
extern void flush_cache_page(struct vm_area_struct *vma, unsigned long user_addr, unsigned long pfn);
#endif

#define flush_cache_dup_mm(mm) flush_cache_mm(mm)

/*
 * flush_cache_user_range is used when we want to ensure that the
 * Harvard caches are synchronised for the user space address range.
 * This is used for the ARM private sys_cacheflush system call.
 */
#define flush_cache_user_range(vma,start,end) \
	__cpuc_coherent_user_range((start) & PAGE_MASK, PAGE_ALIGN(end))

/*
 * Perform necessary cache operations to ensure that data previously
 * stored within this range of addresses can be executed by the CPU.
 */
#define flush_icache_range(s,e)		__cpuc_coherent_kern_range(s,e)

/*
 * Perform necessary cache operations to ensure that the TLB will
 * see data written in the specified area.
 */
#define clean_dcache_area(start,size)	cpu_dcache_clean_area(start, size)

/*
 * flush_dcache_page is used when the kernel has written to the page
 * cache page at virtual address page->virtual.
 *
 * If this page isn't mapped (ie, page_mapping == NULL), or it might
 * have userspace mappings, then we _must_ always clean + invalidate
 * the dcache entries associated with the kernel mapping.
 *
 * Otherwise we can defer the operation, and clean the cache when we are
 * about to change to user space.  This is the same method as used on SPARC64.
 * See update_mmu_cache for the user space part.
 */
#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1
extern void flush_dcache_page(struct page *);

static inline void __flush_icache_all(void)
{
#ifdef CONFIG_ARM_ERRATA_411920
	extern void v6_icache_inval_all(void);
	v6_icache_inval_all();
#else
	asm("mcr	p15, 0, %0, c7, c5, 0	@ invalidate I-cache\n"
	    :
	    : "r" (0));
#endif
}
static inline void flush_kernel_vmap_range(void *addr, int size)
{
	if ((cache_is_vivt() || cache_is_vipt_aliasing()))
	  __cpuc_flush_dcache_area(addr, (size_t)size);
}
static inline void invalidate_kernel_vmap_range(void *addr, int size)
{
	if ((cache_is_vivt() || cache_is_vipt_aliasing()))
	  __cpuc_flush_dcache_area(addr, (size_t)size);
}

#define ARCH_HAS_FLUSH_ANON_PAGE
static inline void flush_anon_page(struct vm_area_struct *vma,
			 struct page *page, unsigned long vmaddr)
{
	extern void __flush_anon_page(struct vm_area_struct *vma,
				struct page *, unsigned long);
	if (PageAnon(page))
		__flush_anon_page(vma, page, vmaddr);
}

#define ARCH_HAS_FLUSH_KERNEL_DCACHE_PAGE
static inline void flush_kernel_dcache_page(struct page *page)
{
	/* highmem pages are always flushed upon kunmap already */
	if ((cache_is_vivt() || cache_is_vipt_aliasing()) && !PageHighMem(page))
		__cpuc_flush_dcache_area(page_address(page), PAGE_SIZE);
}

#define flush_dcache_mmap_lock(mapping) \
	spin_lock_irq(&(mapping)->tree_lock)
#define flush_dcache_mmap_unlock(mapping) \
	spin_unlock_irq(&(mapping)->tree_lock)

#define flush_icache_user_range(vma,page,addr,len) \
	flush_dcache_page(page)

/*
 * We don't appear to need to do anything here.  In fact, if we did, we'd
 * duplicate cache flushing elsewhere performed by flush_dcache_page().
 */
#define flush_icache_page(vma,page)	do { } while (0)

/*
 * flush_cache_vmap() is used when creating mappings (eg, via vmap,
 * vmalloc, ioremap etc) in kernel space for pages.  On non-VIPT
 * caches, since the direct-mappings of these pages may contain cached
 * data, we need to do a full cache flush to ensure that writebacks
 * don't corrupt data placed into these pages via the new mappings.
 */
static inline void flush_cache_vmap(unsigned long start, unsigned long end)
{
	if (!cache_is_vipt_nonaliasing())
		flush_cache_all();
	else
		/*
		 * set_pte_at() called from vmap_pte_range() does not
		 * have a DSB after cleaning the cache line.
		 */
		dsb();
}

static inline void flush_cache_vunmap(unsigned long start, unsigned long end)
{
	if (!cache_is_vipt_nonaliasing())
		flush_cache_all();
}

#endif
