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

struct sgx_epc_page {
	unsigned int section;
	struct list_head list;
};

/*
 * The firmware can define multiple chunks of EPC to the different areas of the
 * physical memory e.g. for memory areas of the each node. This structure is
 * used to store EPC pages for one EPC section and virtual memory area where
 * the pages have been mapped.
 */
struct sgx_epc_section {
	unsigned long phys_addr;
	void *virt_addr;
	struct list_head page_list;
	struct list_head laundry_list;
	struct sgx_epc_page *pages;
	spinlock_t lock;
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

#endif /* _X86_SGX_H */
