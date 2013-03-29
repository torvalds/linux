#ifndef _PARISC_CACHEFLUSH_H
#define _PARISC_CACHEFLUSH_H

#include <linux/mm.h>
#include <linux/uaccess.h>
#include <asm/tlbflush.h>

/* The usual comment is "Caches aren't brain-dead on the <architecture>".
 * Unfortunately, that doesn't apply to PA-RISC. */

/* Internal implementation */
void flush_data_cache_local(void *);  /* flushes local data-cache only */
void flush_instruction_cache_local(void *); /* flushes local code-cache only */
#ifdef CONFIG_SMP
void flush_data_cache(void); /* flushes data-cache only (all processors) */
void flush_instruction_cache(void); /* flushes i-cache only (all processors) */
#else
#define flush_data_cache() flush_data_cache_local(NULL)
#define flush_instruction_cache() flush_instruction_cache_local(NULL)
#endif

#define flush_cache_dup_mm(mm) flush_cache_mm(mm)

void flush_user_icache_range_asm(unsigned long, unsigned long);
void flush_kernel_icache_range_asm(unsigned long, unsigned long);
void flush_user_dcache_range_asm(unsigned long, unsigned long);
void flush_kernel_dcache_range_asm(unsigned long, unsigned long);
void flush_kernel_dcache_page_asm(void *);
void flush_kernel_icache_page(void *);
void flush_user_dcache_range(unsigned long, unsigned long);
void flush_user_icache_range(unsigned long, unsigned long);

/* Cache flush operations */

void flush_cache_all_local(void);
void flush_cache_all(void);
void flush_cache_mm(struct mm_struct *mm);

#define ARCH_HAS_FLUSH_KERNEL_DCACHE_PAGE
void flush_kernel_dcache_page_addr(void *addr);
static inline void flush_kernel_dcache_page(struct page *page)
{
	flush_kernel_dcache_page_addr(page_address(page));
}

#define flush_kernel_dcache_range(start,size) \
	flush_kernel_dcache_range_asm((start), (start)+(size));
/* vmap range flushes and invalidates.  Architecturally, we don't need
 * the invalidate, because the CPU should refuse to speculate once an
 * area has been flushed, so invalidate is left empty */
static inline void flush_kernel_vmap_range(void *vaddr, int size)
{
	unsigned long start = (unsigned long)vaddr;

	flush_kernel_dcache_range_asm(start, start + size);
}
static inline void invalidate_kernel_vmap_range(void *vaddr, int size)
{
	unsigned long start = (unsigned long)vaddr;
	void *cursor = vaddr;

	for ( ; cursor < vaddr + size; cursor += PAGE_SIZE) {
		struct page *page = vmalloc_to_page(cursor);

		if (test_and_clear_bit(PG_dcache_dirty, &page->flags))
			flush_kernel_dcache_page(page);
	}
	flush_kernel_dcache_range_asm(start, start + size);
}

#define flush_cache_vmap(start, end)		flush_cache_all()
#define flush_cache_vunmap(start, end)		flush_cache_all()

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1
extern void flush_dcache_page(struct page *page);

#define flush_dcache_mmap_lock(mapping) \
	spin_lock_irq(&(mapping)->tree_lock)
#define flush_dcache_mmap_unlock(mapping) \
	spin_unlock_irq(&(mapping)->tree_lock)

#define flush_icache_page(vma,page)	do { 		\
	flush_kernel_dcache_page(page);			\
	flush_kernel_icache_page(page_address(page)); 	\
} while (0)

#define flush_icache_range(s,e)		do { 		\
	flush_kernel_dcache_range_asm(s,e); 		\
	flush_kernel_icache_range_asm(s,e); 		\
} while (0)

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
do { \
	flush_cache_page(vma, vaddr, page_to_pfn(page)); \
	memcpy(dst, src, len); \
	flush_kernel_dcache_range_asm((unsigned long)dst, (unsigned long)dst + len); \
} while (0)

#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
do { \
	flush_cache_page(vma, vaddr, page_to_pfn(page)); \
	memcpy(dst, src, len); \
} while (0)

void flush_cache_page(struct vm_area_struct *vma, unsigned long vmaddr, unsigned long pfn);
void flush_cache_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end);

/* defined in pacache.S exported in cache.c used by flush_anon_page */
void flush_dcache_page_asm(unsigned long phys_addr, unsigned long vaddr);

#define ARCH_HAS_FLUSH_ANON_PAGE
static inline void
flush_anon_page(struct vm_area_struct *vma, struct page *page, unsigned long vmaddr)
{
	if (PageAnon(page)) {
		flush_tlb_page(vma, vmaddr);
		preempt_disable();
		flush_dcache_page_asm(page_to_phys(page), vmaddr);
		preempt_enable();
	}
}

#ifdef CONFIG_DEBUG_RODATA
void mark_rodata_ro(void);
#endif

#ifdef CONFIG_PA8X00
/* Only pa8800, pa8900 needs this */

#include <asm/kmap_types.h>

#define ARCH_HAS_KMAP

void kunmap_parisc(void *addr);

static inline void *kmap(struct page *page)
{
	might_sleep();
	return page_address(page);
}

#define kunmap(page)			kunmap_parisc(page_address(page))

static inline void *kmap_atomic(struct page *page)
{
	pagefault_disable();
	return page_address(page);
}

static inline void __kunmap_atomic(void *addr)
{
	kunmap_parisc(addr);
	pagefault_enable();
}

#define kmap_atomic_prot(page, prot)	kmap_atomic(page)
#define kmap_atomic_pfn(pfn)	kmap_atomic(pfn_to_page(pfn))
#define kmap_atomic_to_page(ptr)	virt_to_page(ptr)
#endif

#endif /* _PARISC_CACHEFLUSH_H */

