#ifndef __ASM_SH_CACHEFLUSH_H
#define __ASM_SH_CACHEFLUSH_H

#include <linux/mm.h>

#ifdef __KERNEL__

#ifdef CONFIG_CACHE_OFF
/*
 * Nothing to do when the cache is disabled, initial flush and explicit
 * disabling is handled at CPU init time.
 *
 * See arch/sh/kernel/cpu/init.c:cache_init().
 */
#define flush_cache_all()			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_dup_mm(mm)			do { } while (0)
#define flush_cache_range(vma, start, end)	do { } while (0)
#define flush_cache_page(vma, vmaddr, pfn)	do { } while (0)
#define flush_dcache_page(page)			do { } while (0)
#define flush_icache_range(start, end)		do { } while (0)
#define flush_icache_page(vma,pg)		do { } while (0)
#define flush_cache_sigtramp(vaddr)		do { } while (0)
#define flush_icache_user_range(vma,pg,adr,len)	do { } while (0)
#define __flush_wback_region(start, size)	do { (void)(start); } while (0)
#define __flush_purge_region(start, size)	do { (void)(start); } while (0)
#define __flush_invalidate_region(start, size)	do { (void)(start); } while (0)
#else
#include <cpu/cacheflush.h>

/*
 * Consistent DMA requires that the __flush_xxx() primitives must be set
 * for any of the enabled non-coherent caches (most of the UP CPUs),
 * regardless of PIPT or VIPT cache configurations.
 */

/* Flush (write-back only) a region (smaller than a page) */
extern void __flush_wback_region(void *start, int size);
/* Flush (write-back & invalidate) a region (smaller than a page) */
extern void __flush_purge_region(void *start, int size);
/* Flush (invalidate only) a region (smaller than a page) */
extern void __flush_invalidate_region(void *start, int size);
#endif

#define ARCH_HAS_FLUSH_ANON_PAGE
extern void __flush_anon_page(struct page *page, unsigned long);

static inline void flush_anon_page(struct vm_area_struct *vma,
				   struct page *page, unsigned long vmaddr)
{
	if (boot_cpu_data.dcache.n_aliases && PageAnon(page))
		__flush_anon_page(page, vmaddr);
}

#define ARCH_HAS_FLUSH_KERNEL_DCACHE_PAGE
static inline void flush_kernel_dcache_page(struct page *page)
{
	flush_dcache_page(page);
}

extern void copy_to_user_page(struct vm_area_struct *vma,
	struct page *page, unsigned long vaddr, void *dst, const void *src,
	unsigned long len);

extern void copy_from_user_page(struct vm_area_struct *vma,
	struct page *page, unsigned long vaddr, void *dst, const void *src,
	unsigned long len);

#define flush_cache_vmap(start, end)		flush_cache_all()
#define flush_cache_vunmap(start, end)		flush_cache_all()

#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

void kmap_coherent_init(void);
void *kmap_coherent(struct page *page, unsigned long addr);
void kunmap_coherent(void);

#define PG_dcache_dirty	PG_arch_1

void cpu_cache_init(void);

#endif /* __KERNEL__ */
#endif /* __ASM_SH_CACHEFLUSH_H */
