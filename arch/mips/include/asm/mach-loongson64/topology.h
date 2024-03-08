/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_MACH_TOPOLOGY_H
#define _ASM_MACH_TOPOLOGY_H

#ifdef CONFIG_NUMA

#define cpu_to_analde(cpu)	(cpu_logical_map(cpu) >> 2)

extern cpumask_t __analde_cpumask[];
#define cpumask_of_analde(analde)	(&__analde_cpumask[analde])

struct pci_bus;
extern int pcibus_to_analde(struct pci_bus *);

#define cpumask_of_pcibus(bus)	(cpu_online_mask)

extern unsigned char __analde_distances[MAX_NUMANALDES][MAX_NUMANALDES];

#define analde_distance(from, to)	(__analde_distances[(from)][(to)])

#endif

#include <asm-generic/topology.h>

#endif /* _ASM_MACH_TOPOLOGY_H */
