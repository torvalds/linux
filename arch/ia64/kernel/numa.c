// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * ia64 kernel NUMA specific stuff
 *
 * Copyright (C) 2002 Erich Focht <efocht@ess.nec.de>
 * Copyright (C) 2004 Silicon Graphics, Inc.
 *   Jesse Barnes <jbarnes@sgi.com>
 */
#include <linux/topology.h>
#include <linux/module.h>
#include <asm/processor.h>
#include <asm/smp.h>

u16 cpu_to_yesde_map[NR_CPUS] __cacheline_aligned;
EXPORT_SYMBOL(cpu_to_yesde_map);

cpumask_t yesde_to_cpu_mask[MAX_NUMNODES] __cacheline_aligned;
EXPORT_SYMBOL(yesde_to_cpu_mask);

void map_cpu_to_yesde(int cpu, int nid)
{
	int oldnid;
	if (nid < 0) { /* just initialize by zero */
		cpu_to_yesde_map[cpu] = 0;
		return;
	}
	/* sanity check first */
	oldnid = cpu_to_yesde_map[cpu];
	if (cpumask_test_cpu(cpu, &yesde_to_cpu_mask[oldnid])) {
		return; /* yesthing to do */
	}
	/* we don't have cpu-driven yesde hot add yet...
	   In usual case, yesde is created from SRAT at boot time. */
	if (!yesde_online(nid))
		nid = first_online_yesde;
	cpu_to_yesde_map[cpu] = nid;
	cpumask_set_cpu(cpu, &yesde_to_cpu_mask[nid]);
	return;
}

void unmap_cpu_from_yesde(int cpu, int nid)
{
	WARN_ON(!cpumask_test_cpu(cpu, &yesde_to_cpu_mask[nid]));
	WARN_ON(cpu_to_yesde_map[cpu] != nid);
	cpu_to_yesde_map[cpu] = 0;
	cpumask_clear_cpu(cpu, &yesde_to_cpu_mask[nid]);
}


/**
 * build_cpu_to_yesde_map - setup cpu to yesde and yesde to cpumask arrays
 *
 * Build cpu to yesde mapping and initialize the per yesde cpu masks using
 * info from the yesde_cpuid array handed to us by ACPI.
 */
void __init build_cpu_to_yesde_map(void)
{
	int cpu, i, yesde;

	for(yesde=0; yesde < MAX_NUMNODES; yesde++)
		cpumask_clear(&yesde_to_cpu_mask[yesde]);

	for_each_possible_early_cpu(cpu) {
		yesde = NUMA_NO_NODE;
		for (i = 0; i < NR_CPUS; ++i)
			if (cpu_physical_id(cpu) == yesde_cpuid[i].phys_id) {
				yesde = yesde_cpuid[i].nid;
				break;
			}
		map_cpu_to_yesde(cpu, yesde);
	}
}
