/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_S390_KFENCE_H
#define _ASM_S390_KFENCE_H

#include <linux/mm.h>
#include <linux/kfence.h>
#include <asm/set_memory.h>
#include <asm/page.h>

void __kernel_map_pages(struct page *page, int numpages, int enable);

static __always_inline bool arch_kfence_init_pool(void)
{
	return true;
}

#define arch_kfence_test_address(addr) ((addr) & PAGE_MASK)

/*
 * Do not split kfence pool to 4k mapping with arch_kfence_init_pool(),
 * but earlier where page table allocations still happen with memblock.
 * Reason is that arch_kfence_init_pool() gets called when the system
 * is still in a limbo state - disabling and enabling bottom halves is
 * not yet allowed, but that is what our page_table_alloc() would do.
 */
static __always_inline void kfence_split_mapping(void)
{
#ifdef CONFIG_KFENCE
	unsigned long pool_pages = KFENCE_POOL_SIZE >> PAGE_SHIFT;

	set_memory_4k((unsigned long)__kfence_pool, pool_pages);
#endif
}

static inline bool kfence_protect_page(unsigned long addr, bool protect)
{
	__kernel_map_pages(virt_to_page(addr), 1, !protect);
	return true;
}

#endif /* _ASM_S390_KFENCE_H */
