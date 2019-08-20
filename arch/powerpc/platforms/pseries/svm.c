// SPDX-License-Identifier: GPL-2.0+
/*
 * Secure VM platform
 *
 * Copyright 2018 IBM Corporation
 * Author: Anshuman Khandual <khandual@linux.vnet.ibm.com>
 */

#include <linux/mm.h>
#include <asm/ultravisor.h>

/* There's one dispatch log per CPU. */
#define NR_DTL_PAGE (DISPATCH_LOG_BYTES * CONFIG_NR_CPUS / PAGE_SIZE)

static struct page *dtl_page_store[NR_DTL_PAGE];
static long dtl_nr_pages;

static bool is_dtl_page_shared(struct page *page)
{
	long i;

	for (i = 0; i < dtl_nr_pages; i++)
		if (dtl_page_store[i] == page)
			return true;

	return false;
}

void dtl_cache_ctor(void *addr)
{
	unsigned long pfn = PHYS_PFN(__pa(addr));
	struct page *page = pfn_to_page(pfn);

	if (!is_dtl_page_shared(page)) {
		dtl_page_store[dtl_nr_pages] = page;
		dtl_nr_pages++;
		WARN_ON(dtl_nr_pages >= NR_DTL_PAGE);
		uv_share_page(pfn, 1);
	}
}
