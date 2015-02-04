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
#include <asm/clk.h>
#include <asm/mach_desc.h>

#ifdef CONFIG_SERIAL_EARLYCON

static unsigned int __initdata arc_base_baud;

unsigned int __init arc_early_base_baud(void)
{
	return arc_base_baud/16;
}

static void __init arc_set_early_base_baud(unsigned long dt_root)
{
	unsigned int core_clk = arc_get_core_freq();

	if (of_flat_dt_is_compatible(dt_root, "abilis,arc-tb10x"))
		arc_base_baud = core_clk/3;
	else
		arc_base_baud = core_clk;
}
#else
#define arc_set_early_base_baud(dt_root)
#endif

static const void * __init arch_get_next_mach(const char *const **match)
{
	static const struct machine_desc *mdesc = __arch_info_begin;
	const struct machine_desc *m = mdesc;

	if (m >= __arch_info_end)
		return NULL;

	mdesc++;
	*match = m->dt_compat;
	return m;
}

/**
 * setup_machine_fdt - Machine setup when an dtb was passed to the kernel
 * @dt:		virtual address pointer to dt blob
 *
 * If a dtb was passed to the kernel, then use it to choose the correct
 * machine_desc and to setup the system.
 */
const struct machine_desc * __init setup_machine_fdt(void *dt)
{
	const struct machine_desc *mdesc;
	unsigned long dt_root;
	const void *clk;
	int len;

	if (!early_init_dt_scan(dt))
		return NULL;

	mdesc = of_flat_dt_match_machine(NULL, arch_get_next_mach);
	if (!mdesc)
		machine_halt();

	dt_root = of_get_flat_dt_root();
	clk = of_get_flat_dt_prop(dt_root, "clock-frequency", &len);
	if (clk)
		arc_set_core_freq(of_read_ulong(clk, len/4));

	arc_set_early_base_baud(dt_root);

	return mdesc;
}
