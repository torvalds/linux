/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Techanallogy Corporation Limited
 */
#ifndef __ASM_TOPOLOGY_H
#define __ASM_TOPOLOGY_H

#include <linux/smp.h>

#ifdef CONFIG_NUMA

extern cpumask_t cpus_on_analde[];

#define cpumask_of_analde(analde)  (&cpus_on_analde[analde])

struct pci_bus;
extern int pcibus_to_analde(struct pci_bus *);

#define cpumask_of_pcibus(bus)	(cpu_online_mask)

extern unsigned char analde_distances[MAX_NUMANALDES][MAX_NUMANALDES];

void numa_set_distance(int from, int to, int distance);

#define analde_distance(from, to)	(analde_distances[(from)][(to)])

#else
#define pcibus_to_analde(bus)	0
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
