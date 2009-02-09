#ifndef _ASM_X86_PAGE_DEFS_H
#define _ASM_X86_PAGE_DEFS_H

#include <linux/const.h>

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12
#define PAGE_SIZE	(_AC(1,UL) << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#define __PHYSICAL_MASK		((phys_addr_t)(1ULL << __PHYSICAL_MASK_SHIFT) - 1)
#define __VIRTUAL_MASK		((1UL << __VIRTUAL_MASK_SHIFT) - 1)

/* Cast PAGE_MASK to a signed type so that it is sign-extended if
   virtual addresses are 32-bits but physical addresses are larger
   (ie, 32-bit PAE). */
#define PHYSICAL_PAGE_MASK	(((signed long)PAGE_MASK) & __PHYSICAL_MASK)

/* PTE_PFN_MASK extracts the PFN from a (pte|pmd|pud|pgd)val_t */
#define PTE_PFN_MASK		((pteval_t)PHYSICAL_PAGE_MASK)

/* PTE_FLAGS_MASK extracts the flags from a (pte|pmd|pud|pgd)val_t */
#define PTE_FLAGS_MASK		(~PTE_PFN_MASK)

#define PMD_PAGE_SIZE		(_AC(1, UL) << PMD_SHIFT)
#define PMD_PAGE_MASK		(~(PMD_PAGE_SIZE-1))

#define HPAGE_SHIFT		PMD_SHIFT
#define HPAGE_SIZE		(_AC(1,UL) << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)

#define HUGE_MAX_HSTATE 2

#define PAGE_OFFSET		((unsigned long)__PAGE_OFFSET)

#define VM_DATA_DEFAULT_FLAGS \
	(((current->personality & READ_IMPLIES_EXEC) ? VM_EXEC : 0 ) | \
	 VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#ifdef CONFIG_X86_64
#include <asm/page_64_types.h>
#else
#include <asm/page_32_types.h>
#endif	/* CONFIG_X86_64 */

#ifndef __ASSEMBLY__

#include <linux/types.h>

typedef struct { pgdval_t pgd; } pgd_t;
typedef struct { pgprotval_t pgprot; } pgprot_t;

#if PAGETABLE_LEVELS > 3
typedef struct { pudval_t pud; } pud_t;
#else
#include <asm-generic/pgtable-nopud.h>
#endif

#if PAGETABLE_LEVELS > 2
typedef struct { pmdval_t pmd; } pmd_t;
#else
#include <asm-generic/pgtable-nopmd.h>
#endif

typedef struct page *pgtable_t;

extern int page_is_ram(unsigned long pagenr);
extern int pagerange_is_ram(unsigned long start, unsigned long end);
extern int devmem_is_allowed(unsigned long pagenr);
extern void map_devmem(unsigned long pfn, unsigned long size,
		       pgprot_t vma_prot);
extern void unmap_devmem(unsigned long pfn, unsigned long size,
			 pgprot_t vma_prot);

extern unsigned long max_low_pfn_mapped;
extern unsigned long max_pfn_mapped;

#endif	/* !__ASSEMBLY__ */

#endif	/* _ASM_X86_PAGE_DEFS_H */
