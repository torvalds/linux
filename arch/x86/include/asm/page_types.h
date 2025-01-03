/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PAGE_DEFS_H
#define _ASM_X86_PAGE_DEFS_H

#include <linux/const.h>
#include <linux/types.h>
#include <linux/mem_encrypt.h>

#include <vdso/page.h>

#define __VIRTUAL_MASK		((1UL << __VIRTUAL_MASK_SHIFT) - 1)

/* Cast P*D_MASK to a signed type so that it is sign-extended if
   virtual addresses are 32-bits but physical addresses are larger
   (ie, 32-bit PAE). */
#define PHYSICAL_PAGE_MASK	(((signed long)PAGE_MASK) & __PHYSICAL_MASK)
#define PHYSICAL_PMD_PAGE_MASK	(((signed long)PMD_MASK) & __PHYSICAL_MASK)
#define PHYSICAL_PUD_PAGE_MASK	(((signed long)PUD_MASK) & __PHYSICAL_MASK)

#define HPAGE_SHIFT		PMD_SHIFT
#define HPAGE_SIZE		(_AC(1,UL) << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)

#define HUGE_MAX_HSTATE 2

#define PAGE_OFFSET		((unsigned long)__PAGE_OFFSET)

#define VM_DATA_DEFAULT_FLAGS	VM_DATA_FLAGS_TSK_EXEC

/* Physical address where kernel should be loaded. */
#define LOAD_PHYSICAL_ADDR ((CONFIG_PHYSICAL_START \
				+ (CONFIG_PHYSICAL_ALIGN - 1)) \
				& ~(CONFIG_PHYSICAL_ALIGN - 1))

#define __START_KERNEL		(__START_KERNEL_map + LOAD_PHYSICAL_ADDR)

#ifdef CONFIG_X86_64
#include <asm/page_64_types.h>
#define IOREMAP_MAX_ORDER       (PUD_SHIFT)
#else
#include <asm/page_32_types.h>
#define IOREMAP_MAX_ORDER       (PMD_SHIFT)
#endif	/* CONFIG_X86_64 */

#ifndef __ASSEMBLY__

#ifdef CONFIG_DYNAMIC_PHYSICAL_MASK
extern phys_addr_t physical_mask;
#define __PHYSICAL_MASK		physical_mask
#else
#define __PHYSICAL_MASK		((phys_addr_t)((1ULL << __PHYSICAL_MASK_SHIFT) - 1))
#endif

extern int devmem_is_allowed(unsigned long pagenr);

extern unsigned long max_low_pfn_mapped;
extern unsigned long max_pfn_mapped;

static inline phys_addr_t get_max_mapped(void)
{
	return (phys_addr_t)max_pfn_mapped << PAGE_SHIFT;
}

bool pfn_range_is_mapped(unsigned long start_pfn, unsigned long end_pfn);

extern void initmem_init(void);

#endif	/* !__ASSEMBLY__ */

#endif	/* _ASM_X86_PAGE_DEFS_H */
