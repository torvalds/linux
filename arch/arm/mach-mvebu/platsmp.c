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
#include <linux/mbus.h>
#include <asm/cacheflush.h>
#include <asm/smp_plat.h>
#include "common.h"
#include "armada-370-xp.h"
#include "pmsu.h"
#include "coherency.h"

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

void __init set_secondary_cpus_clock(void)
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
	unsigned int ncores = num_possible_cpus();

	if (ncores == 0 || ncores > ARMADA_XP_MAX_CPUS)
		panic("Invalid number of CPUs in DT\n");

	set_smp_cross_call(armada_mpic_send_doorbell);
}

void __init armada_xp_smp_prepare_cpus(unsigned int max_cpus)
{
	set_secondary_cpus_clock();
	flush_cache_all();
	set_cpu_coherent(cpu_logical_map(smp_processor_id()), 0);
	mvebu_mbus_add_window("bootrom", 0xfff00000, SZ_1M);
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
