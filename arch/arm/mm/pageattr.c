// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 */
#include <linux/mm.h>
#include <linux/module.h>

#include <asm/tlbflush.h>
#include <asm/set_memory.h>

struct page_change_data {
	pgprot_t set_mask;
	pgprot_t clear_mask;
};

static int change_page_range(pte_t *ptep, unsigned long addr, void *data)
{
	struct page_change_data *cdata = data;
	pte_t pte = *ptep;

	pte = clear_pte_bit(pte, cdata->clear_mask);
	pte = set_pte_bit(pte, cdata->set_mask);

	set_pte_ext(ptep, pte, 0);
	return 0;
}

static bool in_range(unsigned long start, unsigned long size,
	unsigned long range_start, unsigned long range_end)
{
	return start >= range_start && start < range_end &&
		size <= range_end - start;
}

/*
 * This function assumes that the range is mapped with PAGE_SIZE pages.
 */
static int __change_memory_common(unsigned long start, unsigned long size,
				pgprot_t set_mask, pgprot_t clear_mask)
{
	struct page_change_data data;
	int ret;

	data.set_mask = set_mask;
	data.clear_mask = clear_mask;

	ret = apply_to_page_range(&init_mm, start, size, change_page_range,
				  &data);

	flush_tlb_kernel_range(start, start + size);
	return ret;
}

static int change_memory_common(unsigned long addr, int numpages,
				pgprot_t set_mask, pgprot_t clear_mask)
{
	unsigned long start = addr & PAGE_MASK;
	unsigned long end = PAGE_ALIGN(addr) + numpages * PAGE_SIZE;
	unsigned long size = end - start;

	WARN_ON_ONCE(start != addr);

	if (!size)
		return 0;

	if (!in_range(start, size, MODULES_VADDR, MODULES_END) &&
	    !in_range(start, size, VMALLOC_START, VMALLOC_END))
		return -EINVAL;

	return __change_memory_common(start, size, set_mask, clear_mask);
}

int set_memory_ro(unsigned long addr, int numpages)
{
	return change_memory_common(addr, numpages,
					__pgprot(L_PTE_RDONLY),
					__pgprot(0));
}

int set_memory_rw(unsigned long addr, int numpages)
{
	return change_memory_common(addr, numpages,
					__pgprot(0),
					__pgprot(L_PTE_RDONLY));
}

int set_memory_nx(unsigned long addr, int numpages)
{
	return change_memory_common(addr, numpages,
					__pgprot(L_PTE_XN),
					__pgprot(0));
}

int set_memory_x(unsigned long addr, int numpages)
{
	return change_memory_common(addr, numpages,
					__pgprot(0),
					__pgprot(L_PTE_XN));
}

int set_memory_valid(unsigned long addr, int numpages, int enable)
{
	if (enable)
		return __change_memory_common(addr, PAGE_SIZE * numpages,
					      __pgprot(L_PTE_VALID),
					      __pgprot(0));
	else
		return __change_memory_common(addr, PAGE_SIZE * numpages,
					      __pgprot(0),
					      __pgprot(L_PTE_VALID));
}
