/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ALPHA_PAGE_H
#define _ALPHA_PAGE_H

#include <linux/const.h>
#include <asm/pal.h>
#include <vdso/page.h>

#ifdef CONFIG_HUGETLB_PAGE
/*
 * The default huge page is the largest granularity hint block, 4MB. All
 * three hint sizes are registered as hstates, hence HUGE_MAX_HSTATE, which
 * linux/hugetlb.h needs before it includes asm/hugetlb.h.
 */
#define HPAGE_SHIFT		22
#define HPAGE_SIZE		(_AC(1, UL) << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)
#define HUGE_MAX_HSTATE		3
#endif

#ifndef __ASSEMBLER__

#define STRICT_MM_TYPECHECKS

extern void clear_page(void *page);

#define vma_alloc_zeroed_movable_folio(vma, vaddr) \
	vma_alloc_folio(GFP_HIGHUSER_MOVABLE | __GFP_ZERO, 0, vma, vaddr)

extern void copy_page(void * _to, void * _from);
#define copy_user_page(to, from, vaddr, pg)	copy_page(to, from)

#ifdef STRICT_MM_TYPECHECKS
/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)	((x).pte)
#define pmd_val(x)	((x).pmd)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) } )
#define __pmd(x)	((pmd_t) { (x) } )
#define __pgd(x)	((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

#else
/*
 * .. while these make it easier on the compiler
 */
typedef unsigned long pte_t;
typedef unsigned long pmd_t;
typedef unsigned long pgd_t;
typedef unsigned long pgprot_t;

#define pte_val(x)	(x)
#define pmd_val(x)	(x)
#define pgd_val(x)	(x)
#define pgprot_val(x)	(x)

#define __pte(x)	(x)
#define __pgd(x)	(x)
#define __pgprot(x)	(x)

#endif /* STRICT_MM_TYPECHECKS */

typedef struct page *pgtable_t;

#ifdef USE_48_BIT_KSEG
#define PAGE_OFFSET		0xffff800000000000UL
#else
#define PAGE_OFFSET		0xfffffc0000000000UL
#endif

#else

#ifdef USE_48_BIT_KSEG
#define PAGE_OFFSET		0xffff800000000000
#else
#define PAGE_OFFSET		0xfffffc0000000000
#endif

#endif /* !__ASSEMBLER__ */

#define __pa(x)			((unsigned long) (x) - PAGE_OFFSET)
#define __va(x)			((void *)((unsigned long) (x) + PAGE_OFFSET))

#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#define virt_addr_valid(kaddr)	pfn_valid((__pa(kaddr) >> PAGE_SHIFT))

#include <asm-generic/memory_model.h>
#include <asm-generic/getorder.h>

#endif /* _ALPHA_PAGE_H */
