// SPDX-License-Identifier: GPL-2.0
/*
 * From split of dump_linuxpagetables.c
 * Copyright 2016, Rashmica Gupta, IBM Corp.
 *
 */
#include <linux/kernel.h>
#include <asm/pgtable.h>

#include "dump_linuxpagetables.h"

static const struct flag_info flag_array[] = {
	{
		.mask	= _PAGE_PRIVILEGED,
		.val	= 0,
		.set	= "user",
		.clear	= "    ",
	}, {
		.mask	= _PAGE_READ,
		.val	= _PAGE_READ,
		.set	= "r",
		.clear	= " ",
	}, {
		.mask	= _PAGE_WRITE,
		.val	= _PAGE_WRITE,
		.set	= "w",
		.clear	= " ",
	}, {
		.mask	= _PAGE_EXEC,
		.val	= _PAGE_EXEC,
		.set	= " X ",
		.clear	= "   ",
	}, {
		.mask	= _PAGE_PTE,
		.val	= _PAGE_PTE,
		.set	= "pte",
		.clear	= "   ",
	}, {
		.mask	= _PAGE_PRESENT,
		.val	= _PAGE_PRESENT,
		.set	= "present",
		.clear	= "       ",
	}, {
		.mask	= H_PAGE_HASHPTE,
		.val	= H_PAGE_HASHPTE,
		.set	= "hpte",
		.clear	= "    ",
	}, {
		.mask	= _PAGE_DIRTY,
		.val	= _PAGE_DIRTY,
		.set	= "dirty",
		.clear	= "     ",
	}, {
		.mask	= _PAGE_ACCESSED,
		.val	= _PAGE_ACCESSED,
		.set	= "accessed",
		.clear	= "        ",
	}, {
		.mask	= _PAGE_NON_IDEMPOTENT,
		.val	= _PAGE_NON_IDEMPOTENT,
		.set	= "non-idempotent",
		.clear	= "              ",
	}, {
		.mask	= _PAGE_TOLERANT,
		.val	= _PAGE_TOLERANT,
		.set	= "tolerant",
		.clear	= "        ",
	}, {
		.mask	= H_PAGE_BUSY,
		.val	= H_PAGE_BUSY,
		.set	= "busy",
	}, {
#ifdef CONFIG_PPC_64K_PAGES
		.mask	= H_PAGE_COMBO,
		.val	= H_PAGE_COMBO,
		.set	= "combo",
	}, {
		.mask	= H_PAGE_4K_PFN,
		.val	= H_PAGE_4K_PFN,
		.set	= "4K_pfn",
	}, {
#else /* CONFIG_PPC_64K_PAGES */
		.mask	= H_PAGE_F_GIX,
		.val	= H_PAGE_F_GIX,
		.set	= "f_gix",
		.is_val	= true,
		.shift	= H_PAGE_F_GIX_SHIFT,
	}, {
		.mask	= H_PAGE_F_SECOND,
		.val	= H_PAGE_F_SECOND,
		.set	= "f_second",
	}, {
#endif /* CONFIG_PPC_64K_PAGES */
		.mask	= _PAGE_SPECIAL,
		.val	= _PAGE_SPECIAL,
		.set	= "special",
	}
};

struct pgtable_level pg_level[5] = {
	{
	}, { /* pgd */
		.flag	= flag_array,
		.num	= ARRAY_SIZE(flag_array),
	}, { /* pud */
		.flag	= flag_array,
		.num	= ARRAY_SIZE(flag_array),
	}, { /* pmd */
		.flag	= flag_array,
		.num	= ARRAY_SIZE(flag_array),
	}, { /* pte */
		.flag	= flag_array,
		.num	= ARRAY_SIZE(flag_array),
	},
};
