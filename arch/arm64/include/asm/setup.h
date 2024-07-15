// SPDX-License-Identifier: GPL-2.0

#ifndef __ARM64_ASM_SETUP_H
#define __ARM64_ASM_SETUP_H

#include <linux/string.h>

#include <uapi/asm/setup.h>

/*
 * These two variables are used in the head.S file.
 */
extern phys_addr_t __fdt_pointer __initdata;
extern u64 __cacheline_aligned boot_args[4];

static inline bool arch_parse_debug_rodata(char *arg)
{
	extern bool rodata_enabled;
	extern bool rodata_full;

	if (!arg)
		return false;

	if (!strcmp(arg, "full")) {
		rodata_enabled = rodata_full = true;
		return true;
	}

	if (!strcmp(arg, "off")) {
		rodata_enabled = rodata_full = false;
		return true;
	}

	if (!strcmp(arg, "on")) {
		rodata_enabled = true;
		rodata_full = false;
		return true;
	}

	return false;
}
#define arch_parse_debug_rodata arch_parse_debug_rodata

#endif
