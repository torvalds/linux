/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MMU_H
#define __MMU_H

#ifdef CONFIG_METAG_USER_TCM
#include <linux/list.h>
#endif

#ifdef CONFIG_HUGETLB_PAGE
#include <asm/page.h>
#endif

typedef struct {
	/* Software pgd base pointer used for Meta 1.x MMU. */
	unsigned long pgd_base;
#ifdef CONFIG_METAG_USER_TCM
	struct list_head tcm;
#endif
#ifdef CONFIG_HUGETLB_PAGE
#if HPAGE_SHIFT < HUGEPT_SHIFT
	/* last partially filled huge page table address */
	unsigned long part_huge;
#endif
#endif
} mm_context_t;

/* Given a virtual address, return the pte for the top level 4meg entry
 * that maps that address.
 * Returns 0 (an empty pte) if that range is not mapped.
 */
unsigned long mmu_read_first_level_page(unsigned long vaddr);

/* Given a linear (virtual) address, return the second level 4k pte
 * that maps that address.  Returns 0 if the address is not mapped.
 */
unsigned long mmu_read_second_level_page(unsigned long vaddr);

/* Get the virtual base address of the MMU */
unsigned long mmu_get_base(void);

/* Initialize the MMU. */
void mmu_init(unsigned long mem_end);

#ifdef CONFIG_METAG_META21_MMU
/*
 * For cpu "cpu" calculate and return the address of the
 * MMCU_TnLOCAL_TABLE_PHYS0 if running in local-space or
 * MMCU_TnGLOBAL_TABLE_PHYS0 if running in global-space.
 */
static inline unsigned long mmu_phys0_addr(unsigned int cpu)
{
	unsigned long phys0;

	phys0 = (MMCU_T0LOCAL_TABLE_PHYS0 +
		(MMCU_TnX_TABLE_PHYSX_STRIDE * cpu)) +
		(MMCU_TXG_TABLE_PHYSX_OFFSET * is_global_space(PAGE_OFFSET));

	return phys0;
}

/*
 * For cpu "cpu" calculate and return the address of the
 * MMCU_TnLOCAL_TABLE_PHYS1 if running in local-space or
 * MMCU_TnGLOBAL_TABLE_PHYS1 if running in global-space.
 */
static inline unsigned long mmu_phys1_addr(unsigned int cpu)
{
	unsigned long phys1;

	phys1 = (MMCU_T0LOCAL_TABLE_PHYS1 +
		(MMCU_TnX_TABLE_PHYSX_STRIDE * cpu)) +
		(MMCU_TXG_TABLE_PHYSX_OFFSET * is_global_space(PAGE_OFFSET));

	return phys1;
}
#endif /* CONFIG_METAG_META21_MMU */

#endif
