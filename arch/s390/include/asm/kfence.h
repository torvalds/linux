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
#ifdef CONFIG_KFENCE
	unsigned long pool_pages = KFENCE_POOL_SIZE >> PAGE_SHIFT;

	set_memory_4k((unsigned long)__kfence_pool, pool_pages);
#endif
	return true;
}

#define arch_kfence_test_address(addr) ((addr) & PAGE_MASK)

static inline bool kfence_protect_page(unsigned long addr, bool protect)
{
	__kernel_map_pages(virt_to_page((void *)addr), 1, !protect);
	return true;
}

#endif /* _ASM_S390_KFENCE_H */
