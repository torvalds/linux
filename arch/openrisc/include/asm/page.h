/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * et al.
 */

#ifndef __ASM_OPENRISC_PAGE_H
#define __ASM_OPENRISC_PAGE_H


/* PAGE_SHIFT determines the page size */

#define PAGE_SHIFT      CONFIG_PAGE_SHIFT
#ifdef __ASSEMBLY__
#define PAGE_SIZE       (1 << PAGE_SHIFT)
#else
#define PAGE_SIZE       (1UL << PAGE_SHIFT)
#endif
#define PAGE_MASK       (~(PAGE_SIZE-1))

#define PAGE_OFFSET	0xc0000000
#define KERNELBASE	PAGE_OFFSET

/* This is not necessarily the right place for this, but it's needed by
 * drivers/of/fdt.c
 */
#include <asm/setup.h>

#ifndef __ASSEMBLY__

#define clear_page(page)	memset((page), 0, PAGE_SIZE)
#define copy_page(to, from)	memcpy((to), (from), PAGE_SIZE)

#define clear_user_page(page, vaddr, pg)        clear_page(page)
#define copy_user_page(to, from, vaddr, pg)     copy_page(to, from)

/*
 * These are used to make use of C type-checking..
 */
typedef struct {
	unsigned long pte;
} pte_t;
typedef struct {
	unsigned long pgd;
} pgd_t;
typedef struct {
	unsigned long pgprot;
} pgprot_t;
typedef struct page *pgtable_t;

#define pte_val(x)	((x).pte)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) })
#define __pgd(x)	((pgd_t) { (x) })
#define __pgprot(x)	((pgprot_t) { (x) })

#endif /* !__ASSEMBLY__ */


#ifndef __ASSEMBLY__

#define __va(x) ((void *)((unsigned long)(x) + PAGE_OFFSET))
#define __pa(x) ((unsigned long) (x) - PAGE_OFFSET)

static inline unsigned long virt_to_pfn(const void *kaddr)
{
	return __pa(kaddr) >> PAGE_SHIFT;
}

#define virt_to_page(addr) \
	(mem_map + (((unsigned long)(addr)-PAGE_OFFSET) >> PAGE_SHIFT))

#define page_to_phys(page)      ((dma_addr_t)page_to_pfn(page) << PAGE_SHIFT)

#define virt_addr_valid(kaddr)	(pfn_valid(virt_to_pfn(kaddr)))

#endif /* __ASSEMBLY__ */

#include <asm-generic/memory_model.h>
#include <asm-generic/getorder.h>

#endif /* __ASM_OPENRISC_PAGE_H */
