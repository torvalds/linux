// SPDX-License-Identifier: GPL-2.0-only
/*
 * kallsyms.c: in-kernel printing of symbolic oopses and stack traces.
 *
 * Rewritten and vastly simplified by Rusty Russell for in-kernel
 * module loader:
 *   Copyright 2002 Rusty Russell <rusty@rustcorp.com.au> IBM Corporation
 *
 * ChangeLog:
 *
 * (25/Aug/2004) Paulo Marques <pmarques@grupopie.com>
 *      Changed the compression method from stem compression to "table lookup"
 *      compression (see scripts/kallsyms.c for a more complete description)
 *
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * This driver is based on Google Debug Kinfo Driver
 */

#define pr_fmt(fmt) "DebugSymbol: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/kallsyms.h>
#include "debug_symbol.h"
#include "../../android/debug_kinfo.h"

struct debug_symbol_data {
	const unsigned long *addresses;
	const int *offsets;
	const u8 *names;
	unsigned int num_syms;
	unsigned long relative_base;
	const char *token_table;
	const u16 *token_index;
	const unsigned int *markers;
};

static struct debug_symbol_data debug_symbol;
static void *debug_symbol_vaddr;

static void fill_ds_entries(void);

struct ds_entry {
	char *name;
	unsigned long addr;
};

#define DS_ENTRY(symbol)	\
	{ .name = #symbol, .addr = 0 }

static struct ds_entry ds_entries[] = {
	DS_ENTRY(_sdata),
	DS_ENTRY(__bss_stop),
	DS_ENTRY(__per_cpu_start),
	DS_ENTRY(__per_cpu_end),
	DS_ENTRY(__start_ro_after_init),
	DS_ENTRY(__end_ro_after_init),
	DS_ENTRY(prb),
	DS_ENTRY(linux_banner),
	DS_ENTRY(irq_stack_ptr),
	DS_ENTRY(tick_cpu_device),
	DS_ENTRY(modules),
};

bool debug_symbol_available(void)
{
	struct kernel_all_info *kainfo;
	struct kernel_info *kinfo;

	if (!debug_symbol_vaddr)
		return false;

	kainfo = (struct kernel_all_info *)debug_symbol_vaddr;
	kinfo = &(kainfo->info);

	if (kainfo->magic_number != DEBUG_KINFO_MAGIC) {
		pr_debug("debug_symbol is not available now\n");
		return false;
	}

	if (!debug_symbol.addresses) {
		debug_symbol.addresses = (unsigned long *)
						__phys_to_kimg(kinfo->_addresses_pa);
		debug_symbol.offsets = (int *)
						__phys_to_kimg(kinfo->_offsets_pa);
		debug_symbol.names = (u8 *)
						__phys_to_kimg(kinfo->_names_pa);
		debug_symbol.num_syms = kinfo->num_syms;
		debug_symbol.relative_base =
						__phys_to_kimg(kinfo->_relative_pa);
		debug_symbol.token_table = (char *)
						__phys_to_kimg(kinfo->_token_table_pa);
		debug_symbol.token_index = (u16 *)
						__phys_to_kimg(kinfo->_token_index_pa);
		debug_symbol.markers = (unsigned int *)
						__phys_to_kimg(kinfo->_markers_pa);
		fill_ds_entries();
	}

	return true;
}
EXPORT_SYMBOL(debug_symbol_available);

/* In line with kallsyms_expand_symbol from kernel/kallsyms.c */
static unsigned int debug_symbol_expand_symbol(unsigned int off,
					   char *result, size_t maxlen)
{
	int len, skipped_first = 0;
	const char *tptr;
	const u8 *data;

	data = &debug_symbol.names[off];
	len = *data;
	data++;
	off++;

	if ((len & 0x80) != 0) {
		len = (len & 0x7F) | (*data << 7);
		data++;
		off++;
	}

	off += len;

	while (len) {
		tptr = &debug_symbol.token_table[debug_symbol.token_index[*data]];
		data++;
		len--;

		while (*tptr) {
			if (skipped_first) {
				if (maxlen <= 1)
					goto tail;
				*result = *tptr;
				result++;
				maxlen--;
			} else
				skipped_first = 1;
			tptr++;
		}
	}

tail:
	if (maxlen)
		*result = '\0';

	return off;
}

/* In line with kallsyms_sym_address from kernel/kallsyms.c */
static unsigned long debug_symbol_sym_address(int idx)
{
	if (!IS_ENABLED(CONFIG_KALLSYMS_BASE_RELATIVE))
		return debug_symbol.addresses[idx];

	if (!IS_ENABLED(CONFIG_KALLSYMS_ABSOLUTE_PERCPU))
		return debug_symbol.relative_base + (u32)debug_symbol.offsets[idx];

	if (debug_symbol.offsets[idx] >= 0)
		return debug_symbol.offsets[idx];

	return debug_symbol.relative_base - 1 - debug_symbol.offsets[idx];
}

/* In line with cleanup_symbol_name from kernel/kallsyms.c */
static bool cleanup_symbol_name(char *s)
{
	char *res;

	if (!IS_ENABLED(CONFIG_LTO_CLANG))
		return false;

	res = strchr(s, '.');
	if (res) {
		*res = '\0';
		return true;
	}

	return false;
}

static unsigned long find_in_ds_entries(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ds_entries); i++)
		if (strcmp(name, ds_entries[i].name) == 0)
			return ds_entries[i].addr;

	return 0;
}

/* In line with kallsyms_lookup_name from kernel/kallsyms.c */
unsigned long debug_symbol_lookup_name(const char *name)
{
	char namebuf[KSYM_NAME_LEN];
	unsigned long i, ret;
	unsigned int off;

	if (!*name)
		return 0;

	ret = find_in_ds_entries(name);
	if (ret)
		return ret;

	for (i = 0, off = 0; i < debug_symbol.num_syms; i++) {
		off = debug_symbol_expand_symbol(off, namebuf, ARRAY_SIZE(namebuf));

		if (strcmp(namebuf, name) == 0 ||
				(cleanup_symbol_name(namebuf) &&
				strcmp(namebuf, name) == 0)) {
			return debug_symbol_sym_address(i);
		}
	}

	return 0;
}
EXPORT_SYMBOL(debug_symbol_lookup_name);

static void fill_ds_entries(void)
{
	char namebuf[KSYM_NAME_LEN];
	unsigned long i;
	unsigned int off;
	int index;

	for (i = 0, off = 0; i < debug_symbol.num_syms; i++) {
		off = debug_symbol_expand_symbol(off, namebuf, ARRAY_SIZE(namebuf));

		for (index = 0; index < ARRAY_SIZE(ds_entries); index++) {
			if (ds_entries[index].addr != 0)
				continue;

			if (strcmp(namebuf, ds_entries[index].name) == 0 ||
					(cleanup_symbol_name(namebuf) &&
					strcmp(namebuf, ds_entries[index].name) == 0)) {
				ds_entries[index].addr = debug_symbol_sym_address(i);
				continue;
			}
		}
	}
}

static int __init debug_symbol_init(void)
{
	struct device_node *rmem_node;
	struct reserved_mem *rmem;

	rmem_node = of_find_compatible_node(NULL, NULL, "google,debug-kinfo");
	if (!rmem_node) {
		pr_err("cannot get compatible node\n");
		goto out;
	}

	rmem_node = of_parse_phandle(rmem_node, "memory-region", 0);
	if (!rmem_node) {
		pr_err("cannot get memory region\n");
		goto out;
	}

	rmem = of_reserved_mem_lookup(rmem_node);
	if (!rmem) {
		pr_err("cannot get reserved memory\n");
		goto out;
	}

	debug_symbol_vaddr = memremap(rmem->base, rmem->size, MEMREMAP_WB);
	if (!debug_symbol_vaddr) {
		pr_err("failed to map reserved memory\n");
		goto out;
	}

	memset(debug_symbol_vaddr, 0, sizeof(struct kernel_all_info));
	rmem->priv = debug_symbol_vaddr;

out:
	return 0;
}
arch_initcall(debug_symbol_init);

MODULE_DESCRIPTION("QCOM Debug Symbol driver");
MODULE_LICENSE("GPL");
