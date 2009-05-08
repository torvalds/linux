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

extern int page_is_ram(unsigned long pagenr);
extern int devmem_is_allowed(unsigned long pagenr);

extern unsigned long max_low_pfn_mapped;
extern unsigned long max_pfn_mapped;

#endif	/* !__ASSEMBLY__ */

#endif	/* _ASM_X86_PAGE_DEFS_H */
