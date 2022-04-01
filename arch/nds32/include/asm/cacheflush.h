/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef __NDS32_CACHEFLUSH_H__
#define __NDS32_CACHEFLUSH_H__

#include <linux/mm.h>

#define PG_dcache_dirty PG_arch_1

void flush_icache_range(unsigned long start, unsigned long end);
#define flush_icache_range flush_icache_range

void flush_icache_page(struct vm_area_struct *vma, struct page *page);
#define flush_icache_page flush_icache_page

#ifdef CONFIG_CPU_CACHE_ALIASING
void flush_cache_mm(struct mm_struct *mm);
void flush_cache_dup_mm(struct mm_struct *mm);
void flush_cache_range(struct vm_area_struct *vma,
		       unsigned long start, unsigned long end);
void flush_cache_page(struct vm_area_struct *vma,
		      unsigned long addr, unsigned long pfn);
void flush_cache_kmaps(void);
void flush_cache_vmap(unsigned long start, unsigned long end);
void flush_cache_vunmap(unsigned long start, unsigned long end);

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1
void flush_dcache_page(struct page *page);
void copy_to_user_page(struct vm_area_struct *vma, struct page *page,
		       unsigned long vaddr, void *dst, void *src, int len);
void copy_from_user_page(struct vm_area_struct *vma, struct page *page,
			 unsigned long vaddr, void *dst, void *src, int len);

#define ARCH_HAS_FLUSH_ANON_PAGE
void flush_anon_page(struct vm_area_struct *vma,
		     struct page *page, unsigned long vaddr);

#define ARCH_IMPLEMENTS_FLUSH_KERNEL_VMAP_RANGE 1
void flush_kernel_vmap_range(void *addr, int size);
void invalidate_kernel_vmap_range(void *addr, int size);
#define flush_dcache_mmap_lock(mapping)   xa_lock_irq(&(mapping)->i_pages)
#define flush_dcache_mmap_unlock(mapping) xa_unlock_irq(&(mapping)->i_pages)

#else
void flush_icache_user_page(struct vm_area_struct *vma, struct page *page,
	                     unsigned long addr, int len);
#define flush_icache_user_page flush_icache_user_page

#include <asm-generic/cacheflush.h>
#endif

#endif /* __NDS32_CACHEFLUSH_H__ */
