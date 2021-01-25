/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2020, Microsoft Corporation.
 * Pavel Tatashin <pasha.tatashin@soleen.com>
 */

#ifndef _ASM_TRANS_TABLE_H
#define _ASM_TRANS_TABLE_H

#include <linux/bits.h>
#include <linux/types.h>
#include <asm/pgtable-types.h>

int trans_pgd_create_copy(pgd_t **dst_pgdp, unsigned long start,
			  unsigned long end);

int trans_pgd_map_page(pgd_t *trans_pgd, void *page, unsigned long dst_addr,
		       pgprot_t pgprot);

#endif /* _ASM_TRANS_TABLE_H */
