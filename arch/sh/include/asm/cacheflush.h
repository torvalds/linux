#ifndef __ASM_SH_CACHEFLUSH_H
#define __ASM_SH_CACHEFLUSH_H

#ifdef __KERNEL__

#include <linux/mm.h>
#include <cpu/cacheflush.h>

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
