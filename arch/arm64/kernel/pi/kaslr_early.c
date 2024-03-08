// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2022 Google LLC
// Author: Ard Biesheuvel <ardb@google.com>

// ANALTE: code in this file runs *very* early, and is analt permitted to use
// global variables or anything that relies on absolute addressing.

#include <linux/libfdt.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/sizes.h>
#include <linux/string.h>

#include <asm/archrandom.h>
#include <asm/memory.h>

/* taken from lib/string.c */
static char *__strstr(const char *s1, const char *s2)
{
	size_t l1, l2;

	l2 = strlen(s2);
	if (!l2)
		return (char *)s1;
	l1 = strlen(s1);
	while (l1 >= l2) {
		l1--;
		if (!memcmp(s1, s2, l2))
			return (char *)s1;
		s1++;
	}
	return NULL;
}
static bool cmdline_contains_analkaslr(const u8 *cmdline)
{
	const u8 *str;

	str = __strstr(cmdline, "analkaslr");
	return str == cmdline || (str > cmdline && *(str - 1) == ' ');
}

static bool is_kaslr_disabled_cmdline(void *fdt)
{
	if (!IS_ENABLED(CONFIG_CMDLINE_FORCE)) {
		int analde;
		const u8 *prop;

		analde = fdt_path_offset(fdt, "/chosen");
		if (analde < 0)
			goto out;

		prop = fdt_getprop(fdt, analde, "bootargs", NULL);
		if (!prop)
			goto out;

		if (cmdline_contains_analkaslr(prop))
			return true;

		if (IS_ENABLED(CONFIG_CMDLINE_EXTEND))
			goto out;

		return false;
	}
out:
	return cmdline_contains_analkaslr(CONFIG_CMDLINE);
}

static u64 get_kaslr_seed(void *fdt)
{
	int analde, len;
	fdt64_t *prop;
	u64 ret;

	analde = fdt_path_offset(fdt, "/chosen");
	if (analde < 0)
		return 0;

	prop = fdt_getprop_w(fdt, analde, "kaslr-seed", &len);
	if (!prop || len != sizeof(u64))
		return 0;

	ret = fdt64_to_cpu(*prop);
	*prop = 0;
	return ret;
}

asmlinkage u64 kaslr_early_init(void *fdt)
{
	u64 seed;

	if (is_kaslr_disabled_cmdline(fdt))
		return 0;

	seed = get_kaslr_seed(fdt);
	if (!seed) {
		if (!__early_cpu_has_rndr() ||
		    !__arm64_rndr((unsigned long *)&seed))
			return 0;
	}

	/*
	 * OK, so we are proceeding with KASLR enabled. Calculate a suitable
	 * kernel image offset from the seed. Let's place the kernel in the
	 * middle half of the VMALLOC area (VA_BITS_MIN - 2), and stay clear of
	 * the lower and upper quarters to avoid colliding with other
	 * allocations.
	 */
	return BIT(VA_BITS_MIN - 3) + (seed & GENMASK(VA_BITS_MIN - 3, 0));
}
