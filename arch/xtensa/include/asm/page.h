/*
 * include/asm-xtensa/page.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version2 as
 * published by the Free Software Foundation.
 *
 * Copyright (C) 2001 - 2007 Tensilica Inc.
 */

#ifndef _XTENSA_PAGE_H
#define _XTENSA_PAGE_H

#include <asm/processor.h>
#include <asm/types.h>
#include <asm/cache.h>
#include <platform/hardware.h>
#include <asm/kmem_layout.h>

/*
 * PAGE_SHIFT determines the page size
 */

#define PAGE_SHIFT	12
#define PAGE_SIZE	(__XTENSA_UL_CONST(1) << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#ifdef CONFIG_MMU
#define PAGE_OFFSET	XCHAL_KSEG_CACHED_VADDR
#define PHYS_OFFSET	XCHAL_KSEG_PADDR
#define MAX_LOW_PFN	(PHYS_PFN(XCHAL_KSEG_PADDR) + \
			 PHYS_PFN(XCHAL_KSEG_SIZE))
#else
#define PAGE_OFFSET	PLATFORM_DEFAULT_MEM_START
#define PHYS_OFFSET	PLATFORM_DEFAULT_MEM_START
#define MAX_LOW_PFN	PHYS_PFN(0xfffffffful)
#endif

/*
 * Cache aliasing:
 *
 * If the cache size for one way is greater than the page size, we have to
 * deal with cache aliasing. The cache index is wider than the page size:
 *
 * |    |cache| cache index
 * | pfn  |off|	virtual address
 * |xxxx:X|zzz|
 * |    : |   |
 * | \  / |   |
 * |trans.|   |
 * | /  \ |   |
 * |yyyy:Y|zzz|	physical address
 *
 * When the page number is translated to the physical page address, the lowest
 * bit(s) (X) that are part of the cache index are also translated (Y).
 * If this translation changes bit(s) (X), the cache index is also afected,
 * thus resulting in a different cache line than before.
 * The kernel does not provide a mechanism to ensure that the page color
 * (represented by this bit) remains the same when allocated or when pages
 * are remapped. When user pages are mapped into kernel space, the color of
 * the page might also change.
 *
 * We use the address space VMALLOC_END ... VMALLOC_END + DCACHE_WAY_SIZE * 2
 * to temporarily map a patch so we can match the color.
 */

#if DCACHE_WAY_SIZE > PAGE_SIZE
# define DCACHE_ALIAS_ORDER	(DCACHE_WAY_SHIFT - PAGE_SHIFT)
# define DCACHE_ALIAS_MASK	(PAGE_MASK & (DCACHE_WAY_SIZE - 1))
# define DCACHE_ALIAS(a)	(((a) & DCACHE_ALIAS_MASK) >> PAGE_SHIFT)
# define DCACHE_ALIAS_EQ(a,b)	((((a) ^ (b)) & DCACHE_ALIAS_MASK) == 0)
#else
# define DCACHE_ALIAS_ORDER	0
# define DCACHE_ALIAS(a)	((void)(a), 0)
#endif
#define DCACHE_N_COLORS		(1 << DCACHE_ALIAS_ORDER)

#if ICACHE_WAY_SIZE > PAGE_SIZE
# define ICACHE_ALIAS_ORDER	(ICACHE_WAY_SHIFT - PAGE_SHIFT)
# define ICACHE_ALIAS_MASK	(PAGE_MASK & (ICACHE_WAY_SIZE - 1))
# define ICACHE_ALIAS(a)	(((a) & ICACHE_ALIAS_MASK) >> PAGE_SHIFT)
# define ICACHE_ALIAS_EQ(a,b)	((((a) ^ (b)) & ICACHE_ALIAS_MASK) == 0)
#else
# define ICACHE_ALIAS_ORDER	0
#endif


#ifdef __ASSEMBLY__

#define __pgprot(x)	(x)

#else

/*
 * These are used to make use of C type-checking..
 */

typedef struct { unsigned long pte; } pte_t;		/* page table entry */
typedef struct { unsigned long pgd; } pgd_t;		/* PGD table entry */
typedef struct { unsigned long pgprot; } pgprot_t;
typedef struct page *pgtable_t;

#define pte_val(x)	((x).pte)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) } )
#define __pgd(x)	((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

/*
 * Pure 2^n version of get_order
 * Use 'nsau' instructions if supported by the processor or the generic version.
 */

#if XCHAL_HAVE_NSA

static inline __attribute_const__ int get_order(unsigned long size)
{
	int lz;
	asm ("nsau %0, %1" : "=r" (lz) : "r" ((size - 1) >> PAGE_SHIFT));
	return 32 - lz;
}

#else

# include <asm-generic/getorder.h>

#endif

struct page;
struct vm_area_struct;
extern void clear_page(void *page);
extern void copy_page(void *to, void *from);

/*
 * If we have cache aliasing and writeback caches, we might have to do
 * some extra work
 */

#if defined(CONFIG_MMU) && DCACHE_WAY_SIZE > PAGE_SIZE
extern void clear_page_alias(void *vaddr, unsigned long paddr);
extern void copy_page_alias(void *to, void *from,
			    unsigned long to_paddr, unsigned long from_paddr);

#define clear_user_highpage clear_user_highpage
void clear_user_highpage(struct page *page, unsigned long vaddr);
#define __HAVE_ARCH_COPY_USER_HIGHPAGE
void copy_user_highpage(struct page *to, struct page *from,
			unsigned long vaddr, struct vm_area_struct *vma);
#else
# define clear_user_page(page, vaddr, pg)	clear_page(page)
# define copy_user_page(to, from, vaddr, pg)	copy_page(to, from)
#endif

/*
 * This handles the memory map.  We handle pages at
 * XCHAL_KSEG_CACHED_VADDR for kernels with 32 bit address space.
 * These macros are for conversion of kernel address, not user
 * addresses.
 */

#define ARCH_PFN_OFFSET		(PHYS_OFFSET >> PAGE_SHIFT)

#ifdef CONFIG_MMU
static inline unsigned long ___pa(unsigned long va)
{
	unsigned long off = va - PAGE_OFFSET;

	if (off >= XCHAL_KSEG_SIZE)
		off -= XCHAL_KSEG_SIZE;

	return off + PHYS_OFFSET;
}
#define __pa(x)	___pa((unsigned long)(x))
#else
#define __pa(x)	\
	((unsigned long) (x) - PAGE_OFFSET + PHYS_OFFSET)
#endif
#define __va(x)	\
	((void *)((unsigned long) (x) - PHYS_OFFSET + PAGE_OFFSET))
#define pfn_valid(pfn) \
	((pfn) >= ARCH_PFN_OFFSET && ((pfn) - ARCH_PFN_OFFSET) < max_mapnr)

#ifdef CONFIG_DISCONTIGMEM
# error CONFIG_DISCONTIGMEM not supported
#endif

#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#define page_to_virt(page)	__va(page_to_pfn(page) << PAGE_SHIFT)
#define virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)
#define page_to_phys(page)	(page_to_pfn(page) << PAGE_SHIFT)

#endif /* __ASSEMBLY__ */

#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#include <asm-generic/memory_model.h>
#endif /* _XTENSA_PAGE_H */
