/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Fixmap support for Hexagon - enough to support highmem features
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_FIXMAP_H
#define _ASM_FIXMAP_H

/*
 * A lot of the fixmap info is already in mem-layout.h
 */
#include <asm/mem-layout.h>

#include <asm-generic/fixmap.h>

#define kmap_get_fixmap_pte(vaddr) \
	pte_offset_kernel(pmd_offset(pud_offset(p4d_offset(pgd_offset_k(vaddr), \
				(vaddr)), (vaddr)), (vaddr)), (vaddr))

#endif
