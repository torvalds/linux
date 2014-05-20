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

static struct clk *__init get_cpu_clk(int cpu)
{
	struct clk *cpu_clk;
	struct device_node *np = of_get_cpu_node(cpu, NULL);

	if (WARN(!np, "missing cpu node\n"))
		return NULL;
	cpu_clk = of_clk_get(np, 0);
	if (WARN_ON(IS_ERR(cpu_clk)))
		return NULL;
	return cpu_clk;
}

static void __init set_secondary_cpus_clock(void)
{
	int thiscpu, cpu;
	unsigned long rate;
	struct clk *cpu_clk;

	thiscpu = smp_processor_id();
	cpu_clk = get_cpu_clk(thiscpu);
	if (!cpu_clk)
		return;
	clk_prepare_enable(cpu_clk);
	rate = clk_get_rate(cpu_clk);

	/* set all the other CPU clk to the same rate than the boot CPU */
	for_each_possible_cpu(cpu) {
		if (cpu == thiscpu)
			continue;
		cpu_clk = get_cpu_clk(cpu);
		if (!cpu_clk)
			return;
		clk_set_rate(cpu_clk, rate);
	}
}

static int armada_xp_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	int ret, hw_cpu;

	pr_info("Booting CPU %d\n", cpu);

	hw_cpu = cpu_logical_map(cpu);
	mvebu_pmsu_set_cpu_boot_addr(hw_cpu, armada_xp_secondary_startup);
	ret = mvebu_cpu_reset_deassert(hw_cpu);
	if (ret) {
		pr_warn("unable to boot CPU: %d\n", ret);
		return ret;
	}

	return 0;
}

static void __init armada_xp_smp_init_cpus(void)
{
	unsigned int ncores = num_possible_cpus();

	if (ncores == 0 || ncores > ARMADA_XP_MAX_CPUS)
		panic("Invalid number of CPUs in DT\n");
}

static void __init armada_xp_smp_prepare_cpus(unsigned int max_cpus)
{
	struct device_node *node;
	struct resource res;
	int err;

	set_secondary_cpus_clock();
	flush_cache_all();
	set_cpu_coherent();

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
	.smp_boot_secondary	= armada_xp_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= armada_xp_cpu_die,
#endif
};

CPU_METHOD_OF_DECLARE(armada_xp_smp, "marvell,armada-xp-smp",
		      &armada_xp_smp_ops);
