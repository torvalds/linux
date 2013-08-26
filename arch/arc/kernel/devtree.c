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
#include <asm/clk.h>
#include <asm/mach_desc.h>

/**
 * setup_machine_fdt - Machine setup when an dtb was passed to the kernel
 * @dt:		virtual address pointer to dt blob
 *
 * If a dtb was passed to the kernel, then use it to choose the correct
 * machine_desc and to setup the system.
 */
struct machine_desc * __init setup_machine_fdt(void *dt)
{
	struct machine_desc *mdesc = NULL, *mdesc_best = NULL;
	unsigned int score, mdesc_score = ~1;
	unsigned long dt_root;
	const char *model, *compat;
	void *clk;
	char manufacturer[16];
	unsigned long len;

	if (!early_init_dt_scan(dt))
		return NULL;

	dt_root = of_get_flat_dt_root();

	/*
	 * The kernel could be multi-platform enabled, thus could have many
	 * "baked-in" machine descriptors. Search thru all for the best
	 * "compatible" string match.
	 */
	for_each_machine_desc(mdesc) {
		score = of_flat_dt_match(dt_root, mdesc->dt_compat);
		if (score > 0 && score < mdesc_score) {
			mdesc_best = mdesc;
			mdesc_score = score;
		}
	}
	if (!mdesc_best) {
		const char *prop;
		long size;

		pr_err("\n unrecognized device tree list:\n[ ");

		prop = of_get_flat_dt_prop(dt_root, "compatible", &size);
		if (prop) {
			while (size > 0) {
				printk("'%s' ", prop);
				size -= strlen(prop) + 1;
				prop += strlen(prop) + 1;
			}
		}
		printk("]\n\n");

		machine_halt();
	}

	/* compat = "<manufacturer>,<model>" */
	compat =  mdesc_best->dt_compat[0];

	model = strchr(compat, ',');
	if (model)
		model++;

	strlcpy(manufacturer, compat, model ? model - compat : strlen(compat));

	pr_info("Board \"%s\" from %s (Manufacturer)\n", model, manufacturer);

	clk = of_get_flat_dt_prop(dt_root, "clock-frequency", &len);
	if (clk)
		arc_set_core_freq(of_read_ulong(clk, len/4));

	return mdesc_best;
}
