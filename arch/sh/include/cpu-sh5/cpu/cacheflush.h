#ifndef __ASM_SH_CPU_SH5_CACHEFLUSH_H
#define __ASM_SH_CPU_SH5_CACHEFLUSH_H

#ifndef __ASSEMBLY__

struct vm_area_struct;
struct page;
struct mm_struct;

extern void flush_cache_all(void);
extern void flush_cache_mm(struct mm_struct *mm);
extern void flush_cache_sigtramp(unsigned long vaddr);
extern void flush_cache_range(struct vm_area_struct *vma, unsigned long start,
			      unsigned long end);
extern void flush_cache_page(struct vm_area_struct *vma, unsigned long addr, unsigned long pfn);
extern void flush_dcache_page(struct page *pg);
extern void flush_icache_range(unsigned long start, unsigned long end);
extern void flush_icache_user_range(struct vm_area_struct *vma,
				    struct page *page, unsigned long addr,
				    int len);

#define flush_cache_dup_mm(mm)	flush_cache_mm(mm)

#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

#define flush_icache_page(vma, page)	do { } while (0)
void p3_cache_init(void);

#endif /* __ASSEMBLY__ */

#endif /* __ASM_SH_CPU_SH5_CACHEFLUSH_H */

