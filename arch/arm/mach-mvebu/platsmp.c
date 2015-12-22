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

#define ARMADA_XP_MAX_CPUS 4

#define AXP_BOOTROM_BASE 0xfff00000
#define AXP_BOOTROM_SIZE 0x100000

static struct clk *get_cpu_clk(int cpu)
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

static void set_secondary_cpu_clock(unsigned int cpu)
{
	int thiscpu;
	unsigned long rate;
	struct clk *cpu_clk;

	thiscpu = get_cpu();

	cpu_clk = get_cpu_clk(thiscpu);
	if (!cpu_clk)
		goto out;
	clk_prepare_enable(cpu_clk);
	rate = clk_get_rate(cpu_clk);

	cpu_clk = get_cpu_clk(cpu);
	if (!cpu_clk)
		goto out;
	clk_set_rate(cpu_clk, rate);
	clk_prepare_enable(cpu_clk);

out:
	put_cpu();
}

static int armada_xp_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	int ret, hw_cpu;

	pr_info("Booting CPU %d\n", cpu);

	hw_cpu = cpu_logical_map(cpu);
	set_secondary_cpu_clock(hw_cpu);
	mvebu_pmsu_set_cpu_boot_addr(hw_cpu, armada_xp_secondary_startup);

	/*
	 * This is needed to wake up CPUs in the offline state after
	 * using CPU hotplug.
	 */
	arch_send_wakeup_ipi_mask(cpumask_of(cpu));

	/*
	 * This is needed to take secondary CPUs out of reset on the
	 * initial boot.
	 */
	ret = mvebu_cpu_reset_deassert(hw_cpu);
	if (ret) {
		pr_warn("unable to boot CPU: %d\n", ret);
		return ret;
	}

	return 0;
}

/*
 * When a CPU is brought back online, either through CPU hotplug, or
 * because of the boot of a kexec'ed kernel, the PMSU configuration
 * for this CPU might be in the deep idle state, preventing this CPU
 * from receiving interrupts. Here, we therefore take out the current
 * CPU from this state, which was entered by armada_xp_cpu_die()
 * below.
 */
static void armada_xp_secondary_init(unsigned int cpu)
{
	mvebu_v7_pmsu_idle_exit();
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

#ifdef CONFIG_HOTPLUG_CPU
static void armada_xp_cpu_die(unsigned int cpu)
{
	/*
	 * CPU hotplug is implemented by putting offline CPUs into the
	 * deep idle sleep state.
	 */
	armada_370_xp_pmsu_idle_enter(true);
}

/*
 * We need a dummy function, so that platform_can_cpu_hotplug() knows
 * we support CPU hotplug. However, the function does not need to do
 * anything, because CPUs going offline can enter the deep idle state
 * by themselves, without any help from a still alive CPU.
 */
static int armada_xp_cpu_kill(unsigned int cpu)
{
	return 1;
}
#endif

const struct smp_operations armada_xp_smp_ops __initconst = {
	.smp_init_cpus		= armada_xp_smp_init_cpus,
	.smp_prepare_cpus	= armada_xp_smp_prepare_cpus,
	.smp_boot_secondary	= armada_xp_boot_secondary,
	.smp_secondary_init     = armada_xp_secondary_init,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= armada_xp_cpu_die,
	.cpu_kill               = armada_xp_cpu_kill,
#endif
};

CPU_METHOD_OF_DECLARE(armada_xp_smp, "marvell,armada-xp-smp",
		      &armada_xp_smp_ops);
