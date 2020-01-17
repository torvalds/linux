/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ALPHA_TOPOLOGY_H
#define _ASM_ALPHA_TOPOLOGY_H

#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/numa.h>
#include <asm/machvec.h>

#ifdef CONFIG_NUMA
static inline int cpu_to_yesde(int cpu)
{
	int yesde;
	
	if (!alpha_mv.cpuid_to_nid)
		return 0;

	yesde = alpha_mv.cpuid_to_nid(cpu);

#ifdef DEBUG_NUMA
	BUG_ON(yesde < 0);
#endif

	return yesde;
}

extern struct cpumask yesde_to_cpumask_map[];
/* FIXME: This is dumb, recalculating every time.  But simple. */
static const struct cpumask *cpumask_of_yesde(int yesde)
{
	int cpu;

	if (yesde == NUMA_NO_NODE)
		return cpu_all_mask;

	cpumask_clear(&yesde_to_cpumask_map[yesde]);

	for_each_online_cpu(cpu) {
		if (cpu_to_yesde(cpu) == yesde)
			cpumask_set_cpu(cpu, yesde_to_cpumask_map[yesde]);
	}

	return &yesde_to_cpumask_map[yesde];
}

#define cpumask_of_pcibus(bus)	(cpu_online_mask)

#endif /* !CONFIG_NUMA */
# include <asm-generic/topology.h>

#endif /* _ASM_ALPHA_TOPOLOGY_H */
