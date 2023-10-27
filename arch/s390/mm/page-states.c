// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2008
 *
 * Guest page hinting for unused pages.
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/mm.h>
#include <asm/page-states.h>
#include <asm/sections.h>
#include <asm/page.h>

int __bootdata_preserved(cmma_flag);

void arch_free_page(struct page *page, int order)
{
	if (!cmma_flag)
		return;
	__set_page_unused(page_to_virt(page), 1UL << order);
}

void arch_alloc_page(struct page *page, int order)
{
	if (!cmma_flag)
		return;
	if (cmma_flag < 2)
		__set_page_stable_dat(page_to_virt(page), 1UL << order);
	else
		__set_page_stable_nodat(page_to_virt(page), 1UL << order);
}
