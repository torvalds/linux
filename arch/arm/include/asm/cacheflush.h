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

#include <asm/glue-cache.h>
#include <asm/shmparam.h>
#include <asm/cachetype.h>
#include <asm/outercache.h>

#define CACHE_COLOUR(vaddr)	((vaddr & (SHMLBA - 1)) >> PAGE_SHIFT)

/*
 * This flag is used to indicate that the page pointed to by a pte is clean
 * and does not require cleaning before returning it to the user.
 */
#define PG_dcache_clean PG_arch_1

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
 *	flush_icache_all()
 *
 *		Unconditionally clean and invalidate the entire icache.
 *		Currently only needed for cache-v6.S and cache-v7.S, see
 *		__flush_icache_all for the generic implementation.
 *
 *	flush_kern_all()
 *
 *		Unconditionally clean and invalidate the entire cache.
 *
 *     flush_kern_louis()
 *
 *             Flush data cache levels up to the level of unification
 *             inner shareable and invalidate the I-cache.
 *             Only needed from v7 onwards, falls back to flush_cache_all()
 *             for all other processor versions.
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
	void (*flush_icache_all)(void);
	void (*flush_kern_all)(void);
	void (*flush_kern_louis)(void);
	void (*flush_user_all)(void);
	void (*flush_user_range)(unsigned long, unsigned long, unsigned int);

	void (*coherent_kern_range)(unsigned long, unsigned long);
	int  (*coherent_user_range)(unsigned long, unsigned long);
	void (*flush_kern_dcache_area)(void *, size_t);

	void (*dma_map_area)(const void *, size_t, int);
	void (*dma_unmap_area)(const void *, size_t, int);

	void (*dma_flush_range)(const void *, const void *);
};

/*
 * Select the calling method
 */
#ifdef MULTI_CACHE

extern struct cpu_cache_fns cpu_cache;

#define __cpuc_flush_icache_all		cpu_cache.flush_icache_all
#define __cpuc_flush_kern_all		cpu_cache.flush_kern_all
#define __cpuc_flush_kern_louis		cpu_cache.flush_kern_louis
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
#define dmac_unmap_area			cpu_cache.dma_unmap_area
#define dmac_flush_range		cpu_cache.dma_flush_range

#else

extern void __cpuc_flush_icache_all(void);
extern void __cpuc_flush_kern_all(void);
extern void __cpuc_flush_kern_louis(void);
extern void __cpuc_flush_user_all(void);
extern void __cpuc_flush_user_range(unsigned long, unsigned long, unsigned int);
extern void __cpuc_coherent_kern_range(unsigned long, unsigned long);
extern int  __cpuc_coherent_user_range(unsigned long, unsigned long);
extern void __cpuc_flush_dcache_area(void *, size_t);

/*
 * These are private to the dma-mapping API.  Do not use directly.
 * Their sole purpose is to ensure that data held in the cache
 * is visible to DMA, or data written by DMA to system memory is
 * visible to the CPU.
 */
extern void dmac_map_area(const void *, size_t, int);
extern void dmac_unmap_area(const void *, size_t, int);
extern void dmac_flush_range(const void *, const void *);

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

/* Invalidate I-cache */
#define __flush_icache_all_generic()					\
	asm("mcr	p15, 0, %0, c7, c5, 0"				\
	    : : "r" (0));

/* Invalidate I-cache inner shareable */
#define __flush_icache_all_v7_smp()					\
	asm("mcr	p15, 0, %0, c7, c1, 0"				\
	    : : "r" (0));

/*
 * Optimized __flush_icache_all for the common cases. Note that UP ARMv7
 * will fall through to use __flush_icache_all_generic.
 */
#if (defined(CONFIG_CPU_V7) && \
     (defined(CONFIG_CPU_V6) || defined(CONFIG_CPU_V6K))) || \
	defined(CONFIG_SMP_ON_UP)
#define __flush_icache_preferred	__cpuc_flush_icache_all
#elif __LINUX_ARM_ARCH__ >= 7 && defined(CONFIG_SMP)
#define __flush_icache_preferred	__flush_icache_all_v7_smp
#elif __LINUX_ARM_ARCH__ == 6 && defined(CONFIG_ARM_ERRATA_411920)
#define __flush_icache_preferred	__cpuc_flush_icache_all
#else
#define __flush_icache_preferred	__flush_icache_all_generic
#endif

static inline void __flush_icache_all(void)
{
	__flush_icache_preferred();
	dsb(ishst);
}

/*
 * Flush caches up to Level of Unification Inner Shareable
 */
#define flush_cache_louis()		__cpuc_flush_kern_louis()

#define flush_cache_all()		__cpuc_flush_kern_all()

static inline void vivt_flush_cache_mm(struct mm_struct *mm)
{
	if (cpumask_test_cpu(smp_processor_id(), mm_cpumask(mm)))
		__cpuc_flush_user_all();
}

static inline void
vivt_flush_cache_range(struct vm_area_struct *vma, unsigned long start, unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;

	if (!mm || cpumask_test_cpu(smp_processor_id(), mm_cpumask(mm)))
		__cpuc_flush_user_range(start & PAGE_MASK, PAGE_ALIGN(end),
					vma->vm_flags);
}

static inline void
vivt_flush_cache_page(struct vm_area_struct *vma, unsigned long user_addr, unsigned long pfn)
{
	struct mm_struct *mm = vma->vm_mm;

	if (!mm || cpumask_test_cpu(smp_processor_id(), mm_cpumask(mm))) {
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
#define flush_cache_user_range(s,e)	__cpuc_coherent_user_range(s,e)

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
extern void flush_kernel_dcache_page(struct page *);

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
		dsb(ishst);
}

static inline void flush_cache_vunmap(unsigned long start, unsigned long end)
{
	if (!cache_is_vipt_nonaliasing())
		flush_cache_all();
}

/*
 * Memory synchronization helpers for mixed cached vs non cached accesses.
 *
 * Some synchronization algorithms have to set states in memory with the
 * cache enabled or disabled depending on the code path.  It is crucial
 * to always ensure proper cache maintenance to update main memory right
 * away in that case.
 *
 * Any cached write must be followed by a cache clean operation.
 * Any cached read must be preceded by a cache invalidate operation.
 * Yet, in the read case, a cache flush i.e. atomic clean+invalidate
 * operation is needed to avoid discarding possible concurrent writes to the
 * accessed memory.
 *
 * Also, in order to prevent a cached writer from interfering with an
 * adjacent non-cached writer, each state variable must be located to
 * a separate cache line.
 */

/*
 * This needs to be >= the max cache writeback size of all
 * supported platforms included in the current kernel configuration.
 * This is used to align state variables to their own cache lines.
 */
#define __CACHE_WRITEBACK_ORDER 6  /* guessed from existing platforms */
#define __CACHE_WRITEBACK_GRANULE (1 << __CACHE_WRITEBACK_ORDER)

/*
 * There is no __cpuc_clean_dcache_area but we use it anyway for
 * code intent clarity, and alias it to __cpuc_flush_dcache_area.
 */
#define __cpuc_clean_dcache_area __cpuc_flush_dcache_area

/*
 * Ensure preceding writes to *p by this CPU are visible to
 * subsequent reads by other CPUs:
 */
static inline void __sync_cache_range_w(volatile void *p, size_t size)
{
	char *_p = (char *)p;

	__cpuc_clean_dcache_area(_p, size);
	outer_clean_range(__pa(_p), __pa(_p + size));
}

/*
 * Ensure preceding writes to *p by other CPUs are visible to
 * subsequent reads by this CPU.  We must be careful not to
 * discard data simultaneously written by another CPU, hence the
 * usage of flush rather than invalidate operations.
 */
static inline void __sync_cache_range_r(volatile void *p, size_t size)
{
	char *_p = (char *)p;

#ifdef CONFIG_OUTER_CACHE
	if (outer_cache.flush_range) {
		/*
		 * Ensure dirty data migrated from other CPUs into our cache
		 * are cleaned out safely before the outer cache is cleaned:
		 */
		__cpuc_clean_dcache_area(_p, size);

		/* Clean and invalidate stale data for *p from outer ... */
		outer_flush_range(__pa(_p), __pa(_p + size));
	}
#endif

	/* ... and inner cache: */
	__cpuc_flush_dcache_area(_p, size);
}

#define sync_cache_w(ptr) __sync_cache_range_w(ptr, sizeof *(ptr))
#define sync_cache_r(ptr) __sync_cache_range_r(ptr, sizeof *(ptr))

/*
 * Disabling cache access for one CPU in an ARMv7 SMP system is tricky.
 * To do so we must:
 *
 * - Clear the SCTLR.C bit to prevent further cache allocations
 * - Flush the desired level of cache
 * - Clear the ACTLR "SMP" bit to disable local coherency
 *
 * ... and so without any intervening memory access in between those steps,
 * not even to the stack.
 *
 * WARNING -- After this has been called:
 *
 * - No ldrex/strex (and similar) instructions must be used.
 * - The CPU is obviously no longer coherent with the other CPUs.
 * - This is unlikely to work as expected if Linux is running non-secure.
 *
 * Note:
 *
 * - This is known to apply to several ARMv7 processor implementations,
 *   however some exceptions may exist.  Caveat emptor.
 *
 * - The clobber list is dictated by the call to v7_flush_dcache_*.
 *   fp is preserved to the stack explicitly prior disabling the cache
 *   since adding it to the clobber list is incompatible with having
 *   CONFIG_FRAME_POINTER=y.  ip is saved as well if ever r12-clobbering
 *   trampoline are inserted by the linker and to keep sp 64-bit aligned.
 */
#define v7_exit_coherency_flush(level) \
	asm volatile( \
	".arch	armv7-a \n\t" \
	"stmfd	sp!, {fp, ip} \n\t" \
	"mrc	p15, 0, r0, c1, c0, 0	@ get SCTLR \n\t" \
	"bic	r0, r0, #"__stringify(CR_C)" \n\t" \
	"mcr	p15, 0, r0, c1, c0, 0	@ set SCTLR \n\t" \
	"isb	\n\t" \
	"bl	v7_flush_dcache_"__stringify(level)" \n\t" \
	"mrc	p15, 0, r0, c1, c0, 1	@ get ACTLR \n\t" \
	"bic	r0, r0, #(1 << 6)	@ disable local coherency \n\t" \
	"mcr	p15, 0, r0, c1, c0, 1	@ set ACTLR \n\t" \
	"isb	\n\t" \
	"dsb	\n\t" \
	"ldmfd	sp!, {fp, ip}" \
	: : : "r0","r1","r2","r3","r4","r5","r6","r7", \
	      "r9","r10","lr","memory" )

int set_memory_ro(unsigned long addr, int numpages);
int set_memory_rw(unsigned long addr, int numpages);
int set_memory_x(unsigned long addr, int numpages);
int set_memory_nx(unsigned long addr, int numpages);

void flush_uprobe_xol_access(struct page *page, unsigned long uaddr,
			     void *kaddr, unsigned long len);
#endif
