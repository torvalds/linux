// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 ARM Ltd.
 */

#include <linux/bitops.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/thread_info.h>

#include <asm/cpufeature.h>
#include <asm/mte.h>
#include <asm/sysreg.h>

void mte_sync_tags(pte_t *ptep, pte_t pte)
{
	struct page *page = pte_page(pte);
	long i, nr_pages = compound_nr(page);

	/* if PG_mte_tagged is set, tags have already been initialised */
	for (i = 0; i < nr_pages; i++, page++) {
		if (!test_and_set_bit(PG_mte_tagged, &page->flags))
			mte_clear_page_tags(page_address(page));
	}
}

int memcmp_pages(struct page *page1, struct page *page2)
{
	char *addr1, *addr2;
	int ret;

	addr1 = page_address(page1);
	addr2 = page_address(page2);
	ret = memcmp(addr1, addr2, PAGE_SIZE);

	if (!system_supports_mte() || ret)
		return ret;

	/*
	 * If the page content is identical but at least one of the pages is
	 * tagged, return non-zero to avoid KSM merging. If only one of the
	 * pages is tagged, set_pte_at() may zero or change the tags of the
	 * other page via mte_sync_tags().
	 */
	if (test_bit(PG_mte_tagged, &page1->flags) ||
	    test_bit(PG_mte_tagged, &page2->flags))
		return addr1 != addr2;

	return ret;
}

void flush_mte_state(void)
{
	if (!system_supports_mte())
		return;

	/* clear any pending asynchronous tag fault */
	dsb(ish);
	write_sysreg_s(0, SYS_TFSRE0_EL1);
	clear_thread_flag(TIF_MTE_ASYNC_FAULT);
}
