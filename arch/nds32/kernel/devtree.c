// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/bug.h>
#include <linux/printk.h>
#include <linux/of_fdt.h>

void __init early_init_devtree(void *params)
{
	if (!params || !early_init_dt_scan(params)) {
		pr_crit("\n"
			"Error: invalid device tree blob at (virtual address 0x%p)\n"
			"\nPlease check your bootloader.", params);

		BUG_ON(1);
	}

	dump_stack_set_arch_desc("%s (DT)", of_flat_dt_get_machine_name());
}
