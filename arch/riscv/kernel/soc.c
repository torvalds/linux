// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 */
#include <linux/init.h>
#include <linux/libfdt.h>
#include <asm/pgtable.h>
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
