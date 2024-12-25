/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_PAGE_MMU_H
#define _ASM_LKL_PAGE_MMU_H

#include <linux/const.h>

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12
#define PAGE_SIZE	(_AC(1, UL) << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))
#define PFN_PTE_SHIFT PAGE_SHIFT

#ifndef __ASSEMBLY__

struct page;
#define clear_page(page)	memset((void *)(page), 0, PAGE_SIZE)
#define copy_page(to, from)	memcpy((void *)(to), (void *)(from), PAGE_SIZE)
#define clear_user_page(page, vaddr, pg)	clear_page(page)
#define copy_user_page(to, from, vaddr, pg)	copy_page(to, from)

typedef struct {unsigned long pte; } pte_t;
typedef struct {unsigned long pmd; } pmd_t;
typedef struct {unsigned long pgd; } pgd_t;
typedef struct {unsigned long pgprot; } pgprot_t;
typedef struct page *pgtable_t;

#define pte_val(p) ((p).pte)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define pmd_val(x)	((x).pmd)
#define __pmd(x) ((pmd_t) { (x) })

#define __pte(x)	((pte_t) { (x) })
#define __pgd(x)	((pgd_t) { (x) })
#define __pgprot(x)	((pgprot_t) { (x) })

#define pte_get_bits(p, bits) ((p).pte & (bits))
#define pte_set_bits(p, bits) ((p).pte |= (bits))
#define pte_clear_bits(p, bits) ((p).pte &= ~(bits))
#define pte_copy(to, from) ({ (to).pte = (from).pte; })
#define pte_is_zero(p) (!((p).pte & ~_PAGE_NEWPAGE))
#define pte_set_val(p, phys, prot) ({ (p).pte = (phys) | pgprot_val(prot); })

extern unsigned long memory_start;
extern unsigned long memory_end;

#define PAGE_OFFSET		(memory_start)
// Define ARCH_PFN_OFFSET so that kernel virtual addresses are the same as
// the corresponding physical addresses (i.e. 1-to-1 mapping).
#define ARCH_PFN_OFFSET (memory_start >> PAGE_SHIFT)

#define __pa(virt)  ((unsigned long) (virt))
#define __va(phys) ((void *)(phys))

#define virt_to_pfn(kaddr)	(__pa(kaddr) >> PAGE_SHIFT)

#define phys_to_pfn(p) PHYS_PFN(p)
#define pfn_to_phys(pfn) PFN_PHYS(pfn)

#define virt_addr_valid(v) pfn_valid(phys_to_pfn(__pa(v)))

#endif // __ASSEMBLY__

#include <asm-generic/memory_model.h>
#include <asm-generic/getorder.h>

#endif // _ASM_LKL_PAGE_MMU_H
