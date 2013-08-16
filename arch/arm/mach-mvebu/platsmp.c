/*
 * Symmetric Multi Processing (SMP) support for Armada XP
 *
 * Copyright (C) 2012 Marvell
 *
 * Lior Amsalem <alior@marvell.com>
 * Yehuda Yitschak <yehuday@marvell.com>
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * The Armada XP SoC has 4 ARMv7 PJ4B CPUs running in full HW coherency
 * This file implements the routines for preparing the SMP infrastructure
 * and waking up the secondary CPUs
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/mbus.h>
#include <asm/cacheflush.h>
#include <asm/smp_plat.h>
#include "common.h"
#include "armada-370-xp.h"
#include "pmsu.h"
#include "coherency.h"

#define AXP_BOOTROM_BASE 0xfff00000
#define AXP_BOOTROM_SIZE 0x100000

void __init set_secondary_cpus_clock(void)
{
	int thiscpu;
	unsigned long rate;
	struct clk *cpu_clk = NULL;
	struct device_node *np = NULL;

	thiscpu = smp_processor_id();
	for_each_node_by_type(np, "cpu") {
		int err;
		int cpu;

		err = of_property_read_u32(np, "reg", &cpu);
		if (WARN_ON(err))
			return;

		if (cpu == thiscpu) {
			cpu_clk = of_clk_get(np, 0);
			break;
		}
	}
	if (WARN_ON(IS_ERR(cpu_clk)))
		return;
	clk_prepare_enable(cpu_clk);
	rate = clk_get_rate(cpu_clk);

	/* set all the other CPU clk to the same rate than the boot CPU */
	for_each_node_by_type(np, "cpu") {
		int err;
		int cpu;

		err = of_property_read_u32(np, "reg", &cpu);
		if (WARN_ON(err))
			return;

		if (cpu != thiscpu) {
			cpu_clk = of_clk_get(np, 0);
			clk_set_rate(cpu_clk, rate);
		}
	}
}

static void armada_xp_secondary_init(unsigned int cpu)
{
	armada_xp_mpic_smp_cpu_init();
}

static int armada_xp_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	pr_info("Booting CPU %d\n", cpu);

	armada_xp_boot_cpu(cpu, armada_xp_secondary_startup);

	return 0;
}

static void __init armada_xp_smp_init_cpus(void)
{
	struct device_node *np;
	unsigned int i, ncores;

	np = of_find_node_by_name(NULL, "cpus");
	if (!np)
		panic("No 'cpus' node found\n");

	ncores = of_get_child_count(np);
	if (ncores == 0 || ncores > ARMADA_XP_MAX_CPUS)
		panic("Invalid number of CPUs in DT\n");

	/* Limit possible CPUs to defconfig */
	if (ncores > nr_cpu_ids) {
		pr_warn("SMP: %d CPUs physically present. Only %d configured.",
			ncores, nr_cpu_ids);
		pr_warn("Clipping CPU count to %d\n", nr_cpu_ids);
		ncores = nr_cpu_ids;
	}

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);

	set_smp_cross_call(armada_mpic_send_doorbell);
}

void __init armada_xp_smp_prepare_cpus(unsigned int max_cpus)
{
	struct device_node *node;
	struct resource res;
	int err;

	set_secondary_cpus_clock();
	flush_cache_all();
	set_cpu_coherent(cpu_logical_map(smp_processor_id()), 0);

	/*
	 * In order to boot the secondary CPUs we need to ensure
	 * the bootROM is mapped at the correct address.
	 */
	node = of_find_compatible_node(NULL, NULL, "marvell,bootrom");
	if (!node)
		panic("Cannot find 'marvell,bootrom' compatible node");

	err = of_address_to_resource(node, 0, &res);
	if (err < 0)
		panic("Cannot get 'bootrom' node address");

	if (res.start != AXP_BOOTROM_BASE ||
	    resource_size(&res) != AXP_BOOTROM_SIZE)
		panic("The address for the BootROM is incorrect");
}

struct smp_operations armada_xp_smp_ops __initdata = {
	.smp_init_cpus		= armada_xp_smp_init_cpus,
	.smp_prepare_cpus	= armada_xp_smp_prepare_cpus,
	.smp_secondary_init	= armada_xp_secondary_init,
	.smp_boot_secondary	= armada_xp_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= armada_xp_cpu_die,
#endif
};
