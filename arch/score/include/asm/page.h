/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SCORE_PAGE_H
#define _ASM_SCORE_PAGE_H

#include <linux/pfn.h>
#include <linux/const.h>

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	(12)
#define PAGE_SIZE	(_AC(1,UL) << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

#define PAGE_UP(addr)	(((addr)+((PAGE_SIZE)-1))&(~((PAGE_SIZE)-1)))
#define PAGE_DOWN(addr)	((addr)&(~((PAGE_SIZE)-1)))

/* align addr on a size boundary - adjust address up/down if needed */
#define _ALIGN_UP(addr, size)	(((addr)+((size)-1))&(~((size)-1)))
#define _ALIGN_DOWN(addr, size)	((addr)&(~((size)-1)))

/* align addr on a size boundary - adjust address up if needed */
#define _ALIGN(addr, size)	_ALIGN_UP(addr, size)

/*
 * PAGE_OFFSET -- the first address of the first page of memory. When not
 * using MMU this corresponds to the first free page in physical memory (aligned
 * on a page boundary).
 */
#define PAGE_OFFSET		(0xA0000000UL)

#define clear_page(pgaddr)			memset((pgaddr), 0, PAGE_SIZE)
#define copy_page(to, from)			memcpy((to), (from), PAGE_SIZE)

#define clear_user_page(pgaddr, vaddr, page)	memset((pgaddr), 0, PAGE_SIZE)
#define copy_user_page(vto, vfrom, vaddr, topg) \
			memcpy((vto), (vfrom), PAGE_SIZE)

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

#define __pte(x)	((pte_t) { (x) })
#define __pgd(x)	((pgd_t) { (x) })
#define __pgprot(x)	((pgprot_t) { (x) })

extern unsigned long max_low_pfn;
extern unsigned long min_low_pfn;
extern unsigned long max_pfn;

#define __pa(x)		((unsigned long)(x) - PAGE_OFFSET)
#define __va(x)		((void *)((unsigned long) (x) + PAGE_OFFSET))

#define phys_to_pfn(phys)	(PFN_DOWN(phys))
#define pfn_to_phys(pfn)	(PFN_PHYS(pfn))

#define virt_to_pfn(vaddr)	(phys_to_pfn((__pa(vaddr))))
#define pfn_to_virt(pfn)	__va(pfn_to_phys((pfn)))

#define virt_to_page(vaddr)	(pfn_to_page(virt_to_pfn(vaddr)))
#define page_to_virt(page)	(pfn_to_virt(page_to_pfn(page)))

#define page_to_phys(page)	(pfn_to_phys(page_to_pfn(page)))
#define page_to_bus(page)	(page_to_phys(page))
#define phys_to_page(paddr)	(pfn_to_page(phys_to_pfn(paddr)))

#define pfn_valid(pfn)		(((pfn) >= min_low_pfn) && ((pfn) < max_low_pfn))

#define ARCH_PFN_OFFSET		(PAGE_OFFSET >> PAGE_SHIFT)

#endif /* __ASSEMBLY__ */

#define virt_addr_valid(vaddr)	(pfn_valid(virt_to_pfn(vaddr)))

#endif /* __KERNEL__ */

#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#include <asm-generic/memory_model.h>
#include <asm-generic/getorder.h>

#endif /* _ASM_SCORE_PAGE_H */
