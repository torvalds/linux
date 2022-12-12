/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_PAGE_H
#define _ASM_PAGE_H

#include <linux/const.h>
#include <asm/addrspace.h>

/*
 * PAGE_SHIFT determines the page size
 */
#ifdef CONFIG_PAGE_SIZE_4KB
#define PAGE_SHIFT	12
#endif
#ifdef CONFIG_PAGE_SIZE_16KB
#define PAGE_SHIFT	14
#endif
#ifdef CONFIG_PAGE_SIZE_64KB
#define PAGE_SHIFT	16
#endif
#define PAGE_SIZE	(_AC(1, UL) << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE - 1))

#define HPAGE_SHIFT	(PAGE_SHIFT + PAGE_SHIFT - 3)
#define HPAGE_SIZE	(_AC(1, UL) << HPAGE_SHIFT)
#define HPAGE_MASK	(~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)

#ifndef __ASSEMBLY__

#include <linux/kernel.h>
#include <linux/pfn.h>

/*
 * It's normally defined only for FLATMEM config but it's
 * used in our early mem init code for all memory models.
 * So always define it.
 */
#define ARCH_PFN_OFFSET	PFN_UP(PHYS_OFFSET)

extern void clear_page(void *page);
extern void copy_page(void *to, void *from);

#define clear_user_page(page, vaddr, pg)	clear_page(page)
#define copy_user_page(to, from, vaddr, pg)	copy_page(to, from)

extern unsigned long shm_align_mask;

struct page;
struct vm_area_struct;
void copy_user_highpage(struct page *to, struct page *from,
	      unsigned long vaddr, struct vm_area_struct *vma);

#define __HAVE_ARCH_COPY_USER_HIGHPAGE

typedef struct { unsigned long pte; } pte_t;
#define pte_val(x)	((x).pte)
#define __pte(x)	((pte_t) { (x) })
typedef struct page *pgtable_t;

typedef struct { unsigned long pgd; } pgd_t;
#define pgd_val(x)	((x).pgd)
#define __pgd(x)	((pgd_t) { (x) })

/*
 * Manipulate page protection bits
 */
typedef struct { unsigned long pgprot; } pgprot_t;
#define pgprot_val(x)	((x).pgprot)
#define __pgprot(x)	((pgprot_t) { (x) })
#define pte_pgprot(x)	__pgprot(pte_val(x) & ~_PFN_MASK)

#define ptep_buddy(x)	((pte_t *)((unsigned long)(x) ^ sizeof(pte_t)))

/*
 * __pa()/__va() should be used only during mem init.
 */
#define __pa(x)		PHYSADDR(x)
#define __va(x)		((void *)((unsigned long)(x) + PAGE_OFFSET - PHYS_OFFSET))

#define pfn_to_kaddr(pfn)	__va((pfn) << PAGE_SHIFT)

#ifdef CONFIG_FLATMEM

static inline int pfn_valid(unsigned long pfn)
{
	/* avoid <linux/mm.h> include hell */
	extern unsigned long max_mapnr;
	unsigned long pfn_offset = ARCH_PFN_OFFSET;

	return pfn >= pfn_offset && pfn < max_mapnr;
}

#endif

#define virt_to_pfn(kaddr)	PFN_DOWN(PHYSADDR(kaddr))
#define virt_to_page(kaddr)	pfn_to_page(virt_to_pfn(kaddr))

extern int __virt_addr_valid(volatile void *kaddr);
#define virt_addr_valid(kaddr)	__virt_addr_valid((volatile void *)(kaddr))

#define VM_DATA_DEFAULT_FLAGS \
	(VM_READ | VM_WRITE | \
	 ((current->personality & READ_IMPLIES_EXEC) ? VM_EXEC : 0) | \
	 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#include <asm-generic/memory_model.h>
#include <asm-generic/getorder.h>

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_PAGE_H */
