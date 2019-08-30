/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
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
#define flush_cache_vunmap(start, end)		do { } while (0)

#ifdef CONFIG_PPC_BOOK3S_64
/*
 * Book3s has no ptesync after setting a pte, so without this ptesync it's
 * possible for a kernel virtual mapping access to return a spurious fault
 * if it's accessed right after the pte is set. The page fault handler does
 * not expect this type of fault. flush_cache_vmap is not exactly the right
 * place to put this, but it seems to work well enough.
 */
#define flush_cache_vmap(start, end)		do { asm volatile("ptesync" ::: "memory"); } while (0)
#else
#define flush_cache_vmap(start, end)		do { } while (0)
#endif

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1
extern void flush_dcache_page(struct page *page);
#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

extern void flush_icache_range(unsigned long, unsigned long);
extern void flush_icache_user_range(struct vm_area_struct *vma,
				    struct page *page, unsigned long addr,
				    int len);
extern void __flush_dcache_icache(void *page_va);
extern void flush_dcache_icache_page(struct page *page);
#if defined(CONFIG_PPC32) && !defined(CONFIG_BOOKE)
extern void __flush_dcache_icache_phys(unsigned long physaddr);
#else
static inline void __flush_dcache_icache_phys(unsigned long physaddr)
{
	BUG();
}
#endif

#ifdef CONFIG_PPC32
/*
 * Write any modified data cache blocks out to memory and invalidate them.
 * Does not invalidate the corresponding instruction cache blocks.
 */
static inline void flush_dcache_range(unsigned long start, unsigned long stop)
{
	void *addr = (void *)(start & ~(L1_CACHE_BYTES - 1));
	unsigned long size = stop - (unsigned long)addr + (L1_CACHE_BYTES - 1);
	unsigned long i;

	for (i = 0; i < size >> L1_CACHE_SHIFT; i++, addr += L1_CACHE_BYTES)
		dcbf(addr);
	mb();	/* sync */
}

/*
 * Write any modified data cache blocks out to memory.
 * Does not invalidate the corresponding cache lines (especially for
 * any corresponding instruction cache).
 */
static inline void clean_dcache_range(unsigned long start, unsigned long stop)
{
	void *addr = (void *)(start & ~(L1_CACHE_BYTES - 1));
	unsigned long size = stop - (unsigned long)addr + (L1_CACHE_BYTES - 1);
	unsigned long i;

	for (i = 0; i < size >> L1_CACHE_SHIFT; i++, addr += L1_CACHE_BYTES)
		dcbst(addr);
	mb();	/* sync */
}

/*
 * Like above, but invalidate the D-cache.  This is used by the 8xx
 * to invalidate the cache so the PPC core doesn't get stale data
 * from the CPM (no cache snooping here :-).
 */
static inline void invalidate_dcache_range(unsigned long start,
					   unsigned long stop)
{
	void *addr = (void *)(start & ~(L1_CACHE_BYTES - 1));
	unsigned long size = stop - (unsigned long)addr + (L1_CACHE_BYTES - 1);
	unsigned long i;

	for (i = 0; i < size >> L1_CACHE_SHIFT; i++, addr += L1_CACHE_BYTES)
		dcbi(addr);
	mb();	/* sync */
}

#endif /* CONFIG_PPC32 */
#ifdef CONFIG_PPC64
extern void flush_dcache_range(unsigned long start, unsigned long stop);
extern void flush_inval_dcache_range(unsigned long start, unsigned long stop);
#endif

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
	do { \
		memcpy(dst, src, len); \
		flush_icache_user_range(vma, page, vaddr, len); \
	} while (0)
#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)

#endif /* __KERNEL__ */

#endif /* _ASM_POWERPC_CACHEFLUSH_H */
