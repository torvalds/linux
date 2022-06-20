/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef __ASM_TOPOLOGY_H
#define __ASM_TOPOLOGY_H

#include <linux/smp.h>

#ifdef CONFIG_NUMA

extern cpumask_t cpus_on_node[];

#define cpumask_of_node(node)  (&cpus_on_node[node])

struct pci_bus;
extern int pcibus_to_node(struct pci_bus *);

#define cpumask_of_pcibus(bus)	(cpu_online_mask)

extern unsigned char node_distances[MAX_NUMNODES][MAX_NUMNODES];

void numa_set_distance(int from, int to, int distance);

#define node_distance(from, to)	(node_distances[(from)][(to)])

#else
#define pcibus_to_node(bus)	0
#endif

#ifdef CONFIG_SMP
#define topology_physical_package_id(cpu)	(cpu_data[cpu].package)
#define topology_core_id(cpu)			(cpu_data[cpu].core)
#define topology_core_cpumask(cpu)		(&cpu_core_map[cpu])
#define topology_sibling_cpumask(cpu)		(&cpu_sibling_map[cpu])
#endif

#include <asm-generic/topology.h>

static inline void arch_fix_phys_package_id(int num, u32 slot) { }
#endif /* __ASM_TOPOLOGY_H */
