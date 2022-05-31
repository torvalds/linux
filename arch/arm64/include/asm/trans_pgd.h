/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2021, Microsoft Corporation.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

#ifndef _ASM_TRANS_TABLE_H
#define _ASM_TRANS_TABLE_H

#include <linux/bits.h>
#include <linux/types.h>
#include <asm/pgtable-types.h>

/*
 * trans_alloc_page
 *	- Allocator that should return exactly one zeroed page, if this
 *	  allocator fails, trans_pgd_create_copy() and trans_pgd_idmap_page()
 *	  return -ENOMEM error.
 *
 * trans_alloc_arg
 *	- Passed to trans_alloc_page as an argument
 */

struct trans_pgd_info {
	void * (*trans_alloc_page)(void *arg);
	void *trans_alloc_arg;
};

int trans_pgd_create_copy(struct trans_pgd_info *info, pgd_t **trans_pgd,
			  unsigned long start, unsigned long end);

int trans_pgd_idmap_page(struct trans_pgd_info *info, phys_addr_t *trans_ttbr0,
			 unsigned long *t0sz, void *page);

int trans_pgd_copy_el2_vectors(struct trans_pgd_info *info,
			       phys_addr_t *el2_vectors);

extern char trans_pgd_stub_vectors[];

#endif /* _ASM_TRANS_TABLE_H */
