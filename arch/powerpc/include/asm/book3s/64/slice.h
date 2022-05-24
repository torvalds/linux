/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_64_SLICE_H
#define _ASM_POWERPC_BOOK3S_64_SLICE_H

#ifndef __ASSEMBLY__

#ifdef CONFIG_PPC_64S_HASH_MMU
#ifdef CONFIG_HUGETLB_PAGE
#define HAVE_ARCH_HUGETLB_UNMAPPED_AREA
#endif
#define HAVE_ARCH_UNMAPPED_AREA
#define HAVE_ARCH_UNMAPPED_AREA_TOPDOWN
#endif

#define SLICE_LOW_SHIFT		28
#define SLICE_LOW_TOP		(0x100000000ul)
#define SLICE_NUM_LOW		(SLICE_LOW_TOP >> SLICE_LOW_SHIFT)
#define GET_LOW_SLICE_INDEX(addr)	((addr) >> SLICE_LOW_SHIFT)

#define SLICE_HIGH_SHIFT	40
#define SLICE_NUM_HIGH		(H_PGTABLE_RANGE >> SLICE_HIGH_SHIFT)
#define GET_HIGH_SLICE_INDEX(addr)	((addr) >> SLICE_HIGH_SHIFT)

#define SLB_ADDR_LIMIT_DEFAULT	DEFAULT_MAP_WINDOW_USER64

struct mm_struct;

unsigned long slice_get_unmapped_area(unsigned long addr, unsigned long len,
				      unsigned long flags, unsigned int psize,
				      int topdown);

unsigned int get_slice_psize(struct mm_struct *mm, unsigned long addr);

void slice_set_range_psize(struct mm_struct *mm, unsigned long start,
			   unsigned long len, unsigned int psize);

void slice_init_new_context_exec(struct mm_struct *mm);
void slice_setup_new_exec(void);

#endif /* __ASSEMBLY__ */

#endif /* _ASM_POWERPC_BOOK3S_64_SLICE_H */
