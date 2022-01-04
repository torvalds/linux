// SPDX-License-Identifier: GPL-2.0
/*
 * arch/sh/kernel/topology.c
 *
 *  Copyright (C) 2007  Paul Mundt
 */
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/topology.h>
#include <linux/node.h>
#include <linux/nodemask.h>
#include <linux/export.h>

static DEFINE_PER_CPU(struct cpu, cpu_devices);

cpumask_t cpu_core_map[NR_CPUS];
EXPORT_SYMBOL(cpu_core_map);

static cpumask_t cpu_coregroup_map(int cpu)
{
	/*
	 * Presently all SH-X3 SMP cores are multi-cores, so just keep it
	 * simple until we have a method for determining topology..
	 */
	return *cpu_possible_mask;
}

const struct cpumask *cpu_coregroup_mask(int cpu)
{
	return &cpu_core_map[cpu];
}

int arch_update_cpu_topology(void)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu)
		cpu_core_map[cpu] = cpu_coregroup_map(cpu);

	return 0;
}

static int __init topology_init(void)
{
	int i, ret;

#ifdef CONFIG_NUMA
	for_each_online_node(i)
		register_one_node(i);
#endif

	for_each_present_cpu(i) {
		struct cpu *c = &per_cpu(cpu_devices, i);

		c->hotpluggable = 1;

		ret = register_cpu(c, i);
		if (unlikely(ret))
			printk(KERN_WARNING "%s: register_cpu %d failed (%d)\n",
			       __func__, i, ret);
	}

#if defined(CONFIG_NUMA) && !defined(CONFIG_SMP)
	/*
	 * In the UP case, make sure the CPU association is still
	 * registered under each node. Without this, sysfs fails
	 * to make the connection between nodes other than node0
	 * and cpu0.
	 */
	for_each_online_node(i)
		if (i != numa_node_id())
			register_cpu_under_node(raw_smp_processor_id(), i);
#endif

	return 0;
}
subsys_initcall(topology_init);
