// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2022 Google LLC
// Author: Ard Biesheuvel <ardb@google.com>

// NOTE: code in this file runs *very* early, and is not permitted to use
// global variables or anything that relies on absolute addressing.

#include <linux/libfdt.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/sizes.h>
#include <linux/string.h>

#include <asm/archrandom.h>
#include <asm/memory.h>
#include <asm/pgtable.h>

#include "pi.h"

static u64 __init get_kaslr_seed(void *fdt, int node)
{
	static char const seed_str[] __initconst = "kaslr-seed";
	fdt64_t *prop;
	u64 ret;
	int len;

	if (node < 0)
		return 0;

	prop = fdt_getprop_w(fdt, node, seed_str, &len);
	if (!prop || len != sizeof(u64))
		return 0;

	ret = fdt64_to_cpu(*prop);
	*prop = 0;
	return ret;
}

u64 __init kaslr_early_init(void *fdt, int chosen)
{
	u64 seed, range;

	if (kaslr_disabled_cmdline())
		return 0;

	seed = get_kaslr_seed(fdt, chosen);
	if (!seed) {
		if (!__early_cpu_has_rndr() ||
		    !__arm64_rndr((unsigned long *)&seed))
			return 0;
	}

	/*
	 * OK, so we are proceeding with KASLR enabled. Calculate a suitable
	 * kernel image offset from the seed. Let's place the kernel in the
	 * 'middle' half of the VMALLOC area, and stay clear of the lower and
	 * upper quarters to avoid colliding with other allocations.
	 */
	range = (VMALLOC_END - KIMAGE_VADDR) / 2;
	return range / 2 + (((__uint128_t)range * seed) >> 64);
}
