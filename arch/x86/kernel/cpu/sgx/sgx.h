/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _X86_SGX_H
#define _X86_SGX_H

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/rwsem.h>
#include <linux/types.h>
#include <asm/asm.h>
#include "arch.h"

#undef pr_fmt
#define pr_fmt(fmt) "sgx: " fmt

#define SGX_MAX_EPC_SECTIONS		8
#define SGX_EEXTEND_BLOCK_SIZE		256
#define SGX_NR_TO_SCAN			16
#define SGX_NR_LOW_PAGES		32
#define SGX_NR_HIGH_PAGES		64

/* Pages, which are being tracked by the page reclaimer. */
#define SGX_EPC_PAGE_RECLAIMER_TRACKED	BIT(0)

struct sgx_epc_page {
	unsigned int section;
	unsigned int flags;
	struct sgx_encl_page *owner;
	struct list_head list;
};

/*
 * The firmware can define multiple chunks of EPC to the different areas of the
 * physical memory e.g. for memory areas of the each node. This structure is
 * used to store EPC pages for one EPC section and virtual memory area where
 * the pages have been mapped.
 *
 * 'lock' must be held before accessing 'page_list' or 'free_cnt'.
 */
struct sgx_epc_section {
	unsigned long phys_addr;
	void *virt_addr;
	struct sgx_epc_page *pages;

	spinlock_t lock;
	struct list_head page_list;
	unsigned long free_cnt;

	/*
	 * Pages which need EREMOVE run on them before they can be
	 * used.  Only safe to be accessed in ksgxd and init code.
	 * Not protected by locks.
	 */
	struct list_head init_laundry_list;
};

extern struct sgx_epc_section sgx_epc_sections[SGX_MAX_EPC_SECTIONS];

static inline unsigned long sgx_get_epc_phys_addr(struct sgx_epc_page *page)
{
	struct sgx_epc_section *section = &sgx_epc_sections[page->section];
	unsigned long index;

	index = ((unsigned long)page - (unsigned long)section->pages) / sizeof(*page);

	return section->phys_addr + index * PAGE_SIZE;
}

static inline void *sgx_get_epc_virt_addr(struct sgx_epc_page *page)
{
	struct sgx_epc_section *section = &sgx_epc_sections[page->section];
	unsigned long index;

	index = ((unsigned long)page - (unsigned long)section->pages) / sizeof(*page);

	return section->virt_addr + index * PAGE_SIZE;
}

struct sgx_epc_page *__sgx_alloc_epc_page(void);
void sgx_free_epc_page(struct sgx_epc_page *page);

void sgx_mark_page_reclaimable(struct sgx_epc_page *page);
int sgx_unmark_page_reclaimable(struct sgx_epc_page *page);
struct sgx_epc_page *sgx_alloc_epc_page(void *owner, bool reclaim);

#endif /* _X86_SGX_H */
