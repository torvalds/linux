/*
 * Copyright (C) 2012 Synopsys, Inc. (www.synopsys.com)
 *
 * Based on reduced version of METAG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <linux/init.h>
#include <linux/reboot.h>
#include <linux/memblock.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <asm/prom.h>

/* called from unflatten_device_tree() to bootstrap devicetree itself */
void * __init early_init_dt_alloc_memory_arch(u64 size, u64 align)
{
	return __va(memblock_alloc(size, align));
}

/**
 * setup_machine_fdt - Machine setup when an dtb was passed to the kernel
 * @dt:		virtual address pointer to dt blob
 *
 * If a dtb was passed to the kernel, then use it to choose the correct
 * machine_desc and to setup the system.
 */
int __init setup_machine_fdt(void *dt)
{
	struct boot_param_header *devtree = dt;
	unsigned long dt_root;
	char *model, *compat;
	char manufacturer[16];

	/* check device tree validity */
	if (be32_to_cpu(devtree->magic) != OF_DT_HEADER)
		return 1;

	/* Search the mdescs for the 'best' compatible value match */
	initial_boot_params = devtree;
	dt_root = of_get_flat_dt_root();

	/* compat = "<manufacturer>,<model>" */
	compat = of_get_flat_dt_prop(dt_root, "compatible", NULL);
	if (!compat)
		compat = "<unknown>";

	model = strchr(compat, ',');
	if (model)
		model++;

	strlcpy(manufacturer, compat, model ? model - compat : strlen(compat));

	pr_info("Board \"%s\" from %s (Manufacturer)\n", model, manufacturer);

	/* Retrieve various information from the /chosen node */
	of_scan_flat_dt(early_init_dt_scan_chosen, boot_command_line);

	return 0;
}
