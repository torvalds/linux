/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_64_SLICE_H
#define _ASM_POWERPC_BOOK3S_64_SLICE_H

#ifdef CONFIG_PPC_MM_SLICES

#define SLICE_LOW_SHIFT		28
#define SLICE_LOW_TOP		(0x100000000ul)
#define SLICE_NUM_LOW		(SLICE_LOW_TOP >> SLICE_LOW_SHIFT)
#define GET_LOW_SLICE_INDEX(addr)	((addr) >> SLICE_LOW_SHIFT)

#define SLICE_HIGH_SHIFT	40
#define SLICE_NUM_HIGH		(H_PGTABLE_RANGE >> SLICE_HIGH_SHIFT)
#define GET_HIGH_SLICE_INDEX(addr)	((addr) >> SLICE_HIGH_SHIFT)

#else /* CONFIG_PPC_MM_SLICES */

#define get_slice_psize(mm, addr)	((mm)->context.user_psize)
#define slice_set_user_psize(mm, psize)		\
do {						\
	(mm)->context.user_psize = (psize);	\
	(mm)->context.sllp = SLB_VSID_USER | mmu_psize_defs[(psize)].sllp; \
} while (0)

#endif /* CONFIG_PPC_MM_SLICES */

#endif /* _ASM_POWERPC_BOOK3S_64_SLICE_H */
