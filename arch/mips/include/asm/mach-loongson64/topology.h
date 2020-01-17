/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_MACH_TOPOLOGY_H
#define _ASM_MACH_TOPOLOGY_H

#ifdef CONFIG_NUMA

#define cpu_to_yesde(cpu)	(cpu_logical_map(cpu) >> 2)

extern cpumask_t __yesde_cpumask[];
#define cpumask_of_yesde(yesde)	(&__yesde_cpumask[yesde])

struct pci_bus;
extern int pcibus_to_yesde(struct pci_bus *);

#define cpumask_of_pcibus(bus)	(cpu_online_mask)

extern unsigned char __yesde_distances[MAX_NUMNODES][MAX_NUMNODES];

#define yesde_distance(from, to)	(__yesde_distances[(from)][(to)])

#endif

#include <asm-generic/topology.h>

#endif /* _ASM_MACH_TOPOLOGY_H */
