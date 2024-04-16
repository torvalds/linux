/*
 * arch/parisc/kernel/topology.c
 *
 * Copyright (C) 2017 Helge Deller <deller@gmx.de>
 *
 * based on arch/arm/kernel/topology.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/sched/topology.h>
#include <linux/cpu.h>

#include <asm/topology.h>
#include <asm/sections.h>

static DEFINE_PER_CPU(struct cpu, cpu_devices);

/*
 * store_cpu_topology is called at boot when only one cpu is running
 * and with the mutex cpu_hotplug.lock locked, when several cpus have booted,
 * which prevents simultaneous write access to cpu_topology array
 */
void store_cpu_topology(unsigned int cpuid)
{
	struct cpu_topology *cpuid_topo = &cpu_topology[cpuid];
	struct cpuinfo_parisc *p;
	int max_socket = -1;
	unsigned long cpu;

	/* If the cpu topology has been already set, just return */
	if (cpuid_topo->core_id != -1)
		return;

#ifdef CONFIG_HOTPLUG_CPU
	per_cpu(cpu_devices, cpuid).hotpluggable = 1;
#endif
	if (register_cpu(&per_cpu(cpu_devices, cpuid), cpuid))
		pr_warn("Failed to register CPU%d device", cpuid);

	/* create cpu topology mapping */
	cpuid_topo->thread_id = -1;
	cpuid_topo->core_id = 0;

	p = &per_cpu(cpu_data, cpuid);
	for_each_online_cpu(cpu) {
		const struct cpuinfo_parisc *cpuinfo = &per_cpu(cpu_data, cpu);

		if (cpu == cpuid) /* ignore current cpu */
			continue;

		if (cpuinfo->cpu_loc == p->cpu_loc) {
			cpuid_topo->core_id = cpu_topology[cpu].core_id;
			if (p->cpu_loc) {
				cpuid_topo->core_id++;
				cpuid_topo->package_id = cpu_topology[cpu].package_id;
				continue;
			}
		}

		if (cpuid_topo->package_id == -1)
			max_socket = max(max_socket, cpu_topology[cpu].package_id);
	}

	if (cpuid_topo->package_id == -1)
		cpuid_topo->package_id = max_socket + 1;

	update_siblings_masks(cpuid);

	pr_info("CPU%u: cpu core %d of socket %d\n",
		cpuid,
		cpu_topology[cpuid].core_id,
		cpu_topology[cpuid].package_id);
}

/*
 * init_cpu_topology is called at boot when only one cpu is running
 * which prevent simultaneous write access to cpu_topology array
 */
void __init init_cpu_topology(void)
{
	reset_cpu_topology();
}
