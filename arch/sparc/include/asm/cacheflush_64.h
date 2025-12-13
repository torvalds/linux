/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SPARC64_CACHEFLUSH_H
#define _SPARC64_CACHEFLUSH_H

#include <asm/page.h>

#ifndef __ASSEMBLER__

#include <linux/mm.h>

/* Cache flush operations. */
#define flushw_all()	__asm__ __volatile__("flushw")

void __flushw_user(void);
#define flushw_user() __flushw_user()

#define flush_user_windows flushw_user
#define flush_register_windows flushw_all

/* These are the same regardless of whether this is an SMP kernel or not. */
#define flush_cache_mm(__mm) \
	do { if ((__mm) == current->mm) flushw_user(); } while(0)
#define flush_cache_dup_mm(mm) flush_cache_mm(mm)
#define flush_cache_range(vma, start, end) \
	flush_cache_mm((vma)->vm_mm)
#define flush_cache_page(vma, page, pfn) \
	flush_cache_mm((vma)->vm_mm)

/*
 * On spitfire, the icache doesn't snoop local stores and we don't
 * use block commit stores (which invalidate icache lines) during
 * module load, so we need this.
 */
void flush_icache_range(unsigned long start, unsigned long end);
void __flush_icache_page(unsigned long);

void __flush_dcache_page(void *addr, int flush_icache);
void flush_dcache_folio_impl(struct folio *folio);
#ifdef CONFIG_SMP
void smp_flush_dcache_folio_impl(struct folio *folio, int cpu);
void flush_dcache_folio_all(struct mm_struct *mm, struct folio *folio);
#else
#define smp_flush_dcache_folio_impl(folio, cpu) flush_dcache_folio_impl(folio)
#define flush_dcache_folio_all(mm, folio) flush_dcache_folio_impl(folio)
#endif

void __flush_dcache_range(unsigned long start, unsigned long end);
#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1
void flush_dcache_folio(struct folio *folio);
#define flush_dcache_folio flush_dcache_folio
static inline void flush_dcache_page(struct page *page)
{
	flush_dcache_folio(page_folio(page));
}

void flush_ptrace_access(struct vm_area_struct *, struct page *,
			 unsigned long uaddr, void *kaddr,
			 unsigned long len, int write);

#define copy_to_user_page(vma, page, vaddr, dst, src, len)		\
	do {								\
		flush_cache_page(vma, vaddr, page_to_pfn(page));	\
		memcpy(dst, src, len);					\
		flush_ptrace_access(vma, page, vaddr, src, len, 0);	\
	} while (0)

#define copy_from_user_page(vma, page, vaddr, dst, src, len) 		\
	do {								\
		flush_cache_page(vma, vaddr, page_to_pfn(page));	\
		memcpy(dst, src, len);					\
		flush_ptrace_access(vma, page, vaddr, dst, len, 1);	\
	} while (0)

#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

#define flush_cache_vmap(start, end)		do { } while (0)
#define flush_cache_vmap_early(start, end)	do { } while (0)
#define flush_cache_vunmap(start, end)		do { } while (0)

#endif /* !__ASSEMBLER__ */

#endif /* _SPARC64_CACHEFLUSH_H */
