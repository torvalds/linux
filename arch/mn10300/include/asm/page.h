/* MN10300 Page table definitions
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_PAGE_H
#define _ASM_PAGE_H

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12

#ifndef __ASSEMBLY__
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE - 1))
#else
#define PAGE_SIZE	+(1 << PAGE_SHIFT)	/* unary plus marks an
						 * immediate val not an addr */
#define PAGE_MASK	+(~(PAGE_SIZE - 1))
#endif

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#define clear_page(page)	memset((void *)(page), 0, PAGE_SIZE)
#define copy_page(to, from)	memcpy((void *)(to), (void *)(from), PAGE_SIZE)

#define clear_user_page(addr, vaddr, page)	clear_page(addr)
#define copy_user_page(vto, vfrom, vaddr, to)	copy_page(vto, vfrom)

/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;
typedef struct page *pgtable_t;

#define PTE_MASK	PAGE_MASK
#define HPAGE_SHIFT	22

#ifdef CONFIG_HUGETLB_PAGE
#define HPAGE_SIZE		((1UL) << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)
#endif

#define pte_val(x)	((x).pte)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) })
#define __pgd(x)	((pgd_t) { (x) })
#define __pgprot(x)	((pgprot_t) { (x) })

#define __ARCH_USE_5LEVEL_HACK
#include <asm-generic/pgtable-nopmd.h>

#endif /* !__ASSEMBLY__ */

/*
 * This handles the memory map.. We could make this a config
 * option, but too many people screw it up, and too few need
 * it.
 *
 * A __PAGE_OFFSET of 0xC0000000 means that the kernel has
 * a virtual address space of one gigabyte, which limits the
 * amount of physical memory you can use to about 950MB.
 */

#ifndef __ASSEMBLY__

/* Pure 2^n version of get_order */
static inline int get_order(unsigned long size) __attribute__((const));
static inline int get_order(unsigned long size)
{
	int order;

	size = (size - 1) >> (PAGE_SHIFT - 1);
	order = -1;
	do {
		size >>= 1;
		order++;
	} while (size);
	return order;
}

#endif /* __ASSEMBLY__ */

#include <asm/page_offset.h>

#define __PAGE_OFFSET		(PAGE_OFFSET_RAW)
#define PAGE_OFFSET		((unsigned long) __PAGE_OFFSET)

/*
 * main RAM and kernel working space are coincident at 0x90000000, but to make
 * life more interesting, there's also an uncached virtual shadow at 0xb0000000
 * - these mappings are fixed in the MMU
 */
#define __pfn_disp		(CONFIG_KERNEL_RAM_BASE_ADDRESS >> PAGE_SHIFT)

#define __pa(x)			((unsigned long)(x))
#define __va(x)			((void *)(unsigned long)(x))
#define pfn_to_kaddr(pfn)	__va((pfn) << PAGE_SHIFT)
#define pfn_to_page(pfn)	(mem_map + ((pfn) - __pfn_disp))
#define page_to_pfn(page)	((unsigned long)((page) - mem_map) + __pfn_disp)
#define __pfn_to_phys(pfn)	PFN_PHYS(pfn)

#define pfn_valid(pfn)					\
({							\
	unsigned long __pfn = (pfn) - __pfn_disp;	\
	__pfn < max_mapnr;				\
})

#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#define virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)
#define page_to_phys(page)	(page_to_pfn(page) << PAGE_SHIFT)

#define VM_DATA_DEFAULT_FLAGS \
	(VM_READ | VM_WRITE | \
	((current->personality & READ_IMPLIES_EXEC) ? VM_EXEC : 0) | \
		 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#endif /* __KERNEL__ */

#endif /* _ASM_PAGE_H */
