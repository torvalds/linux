#ifndef __ASM_SH_CPU_SH5_CACHEFLUSH_H
#define __ASM_SH_CPU_SH5_CACHEFLUSH_H

#ifndef __ASSEMBLY__

extern void flush_cache_all(void);
extern void flush_cache_mm(struct mm_struct *mm);
extern void flush_cache_sigtramp(unsigned long vaddr);
extern void flush_cache_range(struct vm_area_struct *vma, unsigned long start,
			      unsigned long end);
extern void flush_cache_page(struct vm_area_struct *vma, unsigned long addr, unsigned long pfn);
extern void flush_dcache_page(struct page *pg);
extern void flush_icache_range(unsigned long start, unsigned long end);

/* XXX .. */
extern void (*__flush_wback_region)(void *start, int size);
extern void (*__flush_purge_region)(void *start, int size);
extern void (*__flush_invalidate_region)(void *start, int size);

#define flush_cache_dup_mm(mm)	flush_cache_mm(mm)
#define flush_icache_page(vma, page)	do { } while (0)

#endif /* __ASSEMBLY__ */

#endif /* __ASM_SH_CPU_SH5_CACHEFLUSH_H */
