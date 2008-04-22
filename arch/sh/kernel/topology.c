/*
 * arch/sh/kernel/topology.c
 *
 *  Copyright (C) 2007  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/node.h>
#include <linux/nodemask.h>

static DEFINE_PER_CPU(struct cpu, cpu_devices);

static int __init topology_init(void)
{
	int i, ret;

#ifdef CONFIG_NEED_MULTIPLE_NODES
	for_each_online_node(i)
		register_one_node(i);
#endif

	for_each_present_cpu(i) {
		ret = register_cpu(&per_cpu(cpu_devices, i), i);
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
