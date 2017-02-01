/*
 * Copyright (C) 2016 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/irqchip.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>

#include <asm/fw/fw.h>
#include <asm/irq_cpu.h>
#include <asm/machine.h>
#include <asm/mips-cpc.h>
#include <asm/prom.h>
#include <asm/smp-ops.h>
#include <asm/time.h>

static __initdata const void *fdt;
static __initdata const struct mips_machine *mach;
static __initdata const void *mach_match_data;

void __init prom_init(void)
{
	plat_get_fdt();
	BUG_ON(!fdt);
}

void __init *plat_get_fdt(void)
{
	const struct mips_machine *check_mach;
	const struct of_device_id *match;

	if (fdt)
		/* Already set up */
		return (void *)fdt;

	if ((fw_arg0 == -2) && !fdt_check_header((void *)fw_arg1)) {
		/*
		 * We booted using the UHI boot protocol, so we have been
		 * provided with the appropriate device tree for the board.
		 * Make use of it & search for any machine struct based upon
		 * the root compatible string.
		 */
		fdt = (void *)fw_arg1;

		for_each_mips_machine(check_mach) {
			match = mips_machine_is_compatible(check_mach, fdt);
			if (match) {
				mach = check_mach;
				mach_match_data = match->data;
				break;
			}
		}
	} else if (IS_ENABLED(CONFIG_LEGACY_BOARDS)) {
		/*
		 * We weren't booted using the UHI boot protocol, but do
		 * support some number of boards with legacy boot protocols.
		 * Attempt to find the right one.
		 */
		for_each_mips_machine(check_mach) {
			if (!check_mach->detect)
				continue;

			if (!check_mach->detect())
				continue;

			mach = check_mach;
		}

		/*
		 * If we don't recognise the machine then we can't continue, so
		 * die here.
		 */
		BUG_ON(!mach);

		/* Retrieve the machine's FDT */
		fdt = mach->fdt;
	}
	return (void *)fdt;
}

void __init plat_mem_setup(void)
{
	if (mach && mach->fixup_fdt)
		fdt = mach->fixup_fdt(fdt, mach_match_data);

	strlcpy(arcs_cmdline, boot_command_line, COMMAND_LINE_SIZE);
	__dt_setup_arch((void *)fdt);
}

void __init device_tree_init(void)
{
	int err;

	unflatten_and_copy_device_tree();
	mips_cpc_probe();

	err = register_cps_smp_ops();
	if (err)
		err = register_up_smp_ops();
}

void __init plat_time_init(void)
{
	struct device_node *np;
	struct clk *clk;

	of_clk_init(NULL);

	if (!cpu_has_counter) {
		mips_hpt_frequency = 0;
	} else if (mach && mach->measure_hpt_freq) {
		mips_hpt_frequency = mach->measure_hpt_freq();
	} else {
		np = of_get_cpu_node(0, NULL);
		if (!np) {
			pr_err("Failed to get CPU node\n");
			return;
		}

		clk = of_clk_get(np, 0);
		if (IS_ERR(clk)) {
			pr_err("Failed to get CPU clock: %ld\n", PTR_ERR(clk));
			return;
		}

		mips_hpt_frequency = clk_get_rate(clk);
		clk_put(clk);

		switch (boot_cpu_type()) {
		case CPU_20KC:
		case CPU_25KF:
			/* The counter runs at the CPU clock rate */
			break;
		default:
			/* The counter runs at half the CPU clock rate */
			mips_hpt_frequency /= 2;
			break;
		}
	}

	clocksource_probe();
}

void __init arch_init_irq(void)
{
	struct device_node *intc_node;

	intc_node = of_find_compatible_node(NULL, NULL,
					    "mti,cpu-interrupt-controller");
	if (!cpu_has_veic && !intc_node)
		mips_cpu_irq_init();

	irqchip_init();
}

static int __init publish_devices(void)
{
	if (!of_have_populated_dt())
		panic("Device-tree not present");

	if (of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL))
		panic("Failed to populate DT");

	return 0;
}
arch_initcall(publish_devices);

void __init prom_free_prom_memory(void)
{
}
