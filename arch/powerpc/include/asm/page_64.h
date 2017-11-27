#ifndef _ASM_POWERPC_PAGE_64_H
#define _ASM_POWERPC_PAGE_64_H

/*
 * Copyright (C) 2001 PPC64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 * We always define HW_PAGE_SHIFT to 12 as use of 64K pages remains Linux
 * specific, every notion of page number shared with the firmware, TCEs,
 * iommu, etc... still uses a page size of 4K.
 */
#define HW_PAGE_SHIFT		12
#define HW_PAGE_SIZE		(ASM_CONST(1) << HW_PAGE_SHIFT)
#define HW_PAGE_MASK		(~(HW_PAGE_SIZE-1))

/*
 * PAGE_FACTOR is the number of bits factor between PAGE_SHIFT and
 * HW_PAGE_SHIFT, that is 4K pages.
 */
#define PAGE_FACTOR		(PAGE_SHIFT - HW_PAGE_SHIFT)

/* Segment size; normal 256M segments */
#define SID_SHIFT		28
#define SID_MASK		ASM_CONST(0xfffffffff)
#define ESID_MASK		0xfffffffff0000000UL
#define GET_ESID(x)		(((x) >> SID_SHIFT) & SID_MASK)

/* 1T segments */
#define SID_SHIFT_1T		40
#define SID_MASK_1T		0xffffffUL
#define ESID_MASK_1T		0xffffff0000000000UL
#define GET_ESID_1T(x)		(((x) >> SID_SHIFT_1T) & SID_MASK_1T)

#ifndef __ASSEMBLY__
#include <asm/cache.h>

typedef unsigned long pte_basic_t;

static inline void clear_page(void *addr)
{
	unsigned long iterations;
	unsigned long onex, twox, fourx, eightx;

	iterations = ppc64_caches.l1d.blocks_per_page / 8;

	/*
	 * Some verisions of gcc use multiply instructions to
	 * calculate the offsets so lets give it a hand to
	 * do better.
	 */
	onex = ppc64_caches.l1d.block_size;
	twox = onex << 1;
	fourx = onex << 2;
	eightx = onex << 3;

	asm volatile(
	"mtctr	%1	# clear_page\n\
	.balign	16\n\
1:	dcbz	0,%0\n\
	dcbz	%3,%0\n\
	dcbz	%4,%0\n\
	dcbz	%5,%0\n\
	dcbz	%6,%0\n\
	dcbz	%7,%0\n\
	dcbz	%8,%0\n\
	dcbz	%9,%0\n\
	add	%0,%0,%10\n\
	bdnz+	1b"
	: "=&r" (addr)
	: "r" (iterations), "0" (addr), "b" (onex), "b" (twox),
		"b" (twox+onex), "b" (fourx), "b" (fourx+onex),
		"b" (twox+fourx), "b" (eightx-onex), "r" (eightx)
	: "ctr", "memory");
}

extern void copy_page(void *to, void *from);

/* Log 2 of page table size */
extern u64 ppc64_pft_size;

#endif /* __ASSEMBLY__ */

#ifdef CONFIG_PPC_MM_SLICES

#define SLICE_LOW_SHIFT		28
#define SLICE_HIGH_SHIFT	40

#define SLICE_LOW_TOP		(0x100000000ul)
#define SLICE_NUM_LOW		(SLICE_LOW_TOP >> SLICE_LOW_SHIFT)
#define SLICE_NUM_HIGH		(H_PGTABLE_RANGE >> SLICE_HIGH_SHIFT)

#define GET_LOW_SLICE_INDEX(addr)	((addr) >> SLICE_LOW_SHIFT)
#define GET_HIGH_SLICE_INDEX(addr)	((addr) >> SLICE_HIGH_SHIFT)

#ifndef __ASSEMBLY__
struct mm_struct;

extern unsigned long slice_get_unmapped_area(unsigned long addr,
					     unsigned long len,
					     unsigned long flags,
					     unsigned int psize,
					     int topdown);

extern unsigned int get_slice_psize(struct mm_struct *mm,
				    unsigned long addr);

extern void slice_set_user_psize(struct mm_struct *mm, unsigned int psize);
extern void slice_set_range_psize(struct mm_struct *mm, unsigned long start,
				  unsigned long len, unsigned int psize);

#endif /* __ASSEMBLY__ */
#else
#define slice_init()
#ifdef CONFIG_PPC_BOOK3S_64
#define get_slice_psize(mm, addr)	((mm)->context.user_psize)
#define slice_set_user_psize(mm, psize)		\
do {						\
	(mm)->context.user_psize = (psize);	\
	(mm)->context.sllp = SLB_VSID_USER | mmu_psize_defs[(psize)].sllp; \
} while (0)
#else /* !CONFIG_PPC_BOOK3S_64 */
#ifdef CONFIG_PPC_64K_PAGES
#define get_slice_psize(mm, addr)	MMU_PAGE_64K
#else /* CONFIG_PPC_64K_PAGES */
#define get_slice_psize(mm, addr)	MMU_PAGE_4K
#endif /* !CONFIG_PPC_64K_PAGES */
#define slice_set_user_psize(mm, psize)	do { BUG(); } while(0)
#endif /* CONFIG_PPC_BOOK3S_64 */

#define slice_set_range_psize(mm, start, len, psize)	\
	slice_set_user_psize((mm), (psize))
#endif /* CONFIG_PPC_MM_SLICES */

#ifdef CONFIG_HUGETLB_PAGE

#ifdef CONFIG_PPC_MM_SLICES
#define HAVE_ARCH_HUGETLB_UNMAPPED_AREA
#endif

#endif /* !CONFIG_HUGETLB_PAGE */

#define VM_DATA_DEFAULT_FLAGS \
	(is_32bit_task() ? \
	 VM_DATA_DEFAULT_FLAGS32 : VM_DATA_DEFAULT_FLAGS64)

/*
 * This is the default if a program doesn't have a PT_GNU_STACK
 * program header entry. The PPC64 ELF ABI has a non executable stack
 * stack by default, so in the absence of a PT_GNU_STACK program header
 * we turn execute permission off.
 */
#define VM_STACK_DEFAULT_FLAGS32	(VM_READ | VM_WRITE | VM_EXEC | \
					 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#define VM_STACK_DEFAULT_FLAGS64	(VM_READ | VM_WRITE | \
					 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#define VM_STACK_DEFAULT_FLAGS \
	(is_32bit_task() ? \
	 VM_STACK_DEFAULT_FLAGS32 : VM_STACK_DEFAULT_FLAGS64)

#include <asm-generic/getorder.h>

#endif /* _ASM_POWERPC_PAGE_64_H */
