// SPDX-License-Identifier: GPL-2.0
/*
 * From split of dump_linuxpagetables.c
 * Copyright 2016, Rashmica Gupta, IBM Corp.
 *
 */
#include <linux/kernel.h>
#include <linux/pgtable.h>

#include "ptdump.h"

static const struct flag_info flag_array[] = {
	{
#ifdef CONFIG_PPC_16K_PAGES
		.mask	= _PAGE_HUGE,
		.val	= _PAGE_HUGE,
#else
		.mask	= _PAGE_SPS,
		.val	= _PAGE_SPS,
#endif
		.set	= "huge",
		.clear	= "    ",
	}, {
		.mask	= _PAGE_RO | _PAGE_NA,
		.val	= 0,
		.set	= "rw",
	}, {
		.mask	= _PAGE_RO | _PAGE_NA,
		.val	= _PAGE_RO,
		.set	= "r ",
	}, {
		.mask	= _PAGE_RO | _PAGE_NA,
		.val	= _PAGE_NA,
		.set	= "  ",
	}, {
		.mask	= _PAGE_EXEC,
		.val	= _PAGE_EXEC,
		.set	= " X ",
		.clear	= "   ",
	}, {
		.mask	= _PAGE_PRESENT,
		.val	= _PAGE_PRESENT,
		.set	= "present",
		.clear	= "       ",
	}, {
		.mask	= _PAGE_GUARDED,
		.val	= _PAGE_GUARDED,
		.set	= "guarded",
		.clear	= "       ",
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
		.mask	= _PAGE_NO_CACHE,
		.val	= _PAGE_NO_CACHE,
		.set	= "no cache",
		.clear	= "        ",
	}, {
		.mask	= _PAGE_SPECIAL,
		.val	= _PAGE_SPECIAL,
		.set	= "special",
	}
};

struct ptdump_pg_level pg_level[5] = {
	{ /* pgd */
		.flag	= flag_array,
		.num	= ARRAY_SIZE(flag_array),
	}, { /* p4d */
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
