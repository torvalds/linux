// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 */
#include <linux/init.h>
#include <linux/libfdt.h>
#include <linux/pgtable.h>
#include <asm/soc.h>

/*
 * This is called extremly early, before parse_dtb(), to allow initializing
 * SoC hardware before memory or any device driver initialization.
 */
void __init soc_early_init(void)
{
	void (*early_fn)(const void *fdt);
	const struct of_device_id *s;
	const void *fdt = dtb_early_va;

	for (s = (void *)&__soc_early_init_table_start;
	     (void *)s < (void *)&__soc_early_init_table_end; s++) {
		if (!fdt_node_check_compatible(fdt, 0, s->compatible)) {
			early_fn = s->data;
			early_fn(fdt);
			return;
		}
	}
}

static bool soc_builtin_dtb_match(unsigned long vendor_id,
				unsigned long arch_id, unsigned long imp_id,
				const struct soc_builtin_dtb *entry)
{
	return entry->vendor_id == vendor_id &&
	       entry->arch_id == arch_id &&
	       entry->imp_id == imp_id;
}

void * __init soc_lookup_builtin_dtb(void)
{
	unsigned long vendor_id, arch_id, imp_id;
	const struct soc_builtin_dtb *s;

	__asm__ ("csrr %0, mvendorid" : "=r"(vendor_id));
	__asm__ ("csrr %0, marchid" : "=r"(arch_id));
	__asm__ ("csrr %0, mimpid" : "=r"(imp_id));

	for (s = (void *)&__soc_builtin_dtb_table_start;
	     (void *)s < (void *)&__soc_builtin_dtb_table_end; s++) {
		if (soc_builtin_dtb_match(vendor_id, arch_id, imp_id, s))
			return s->dtb_func();
	}

	return NULL;
}
