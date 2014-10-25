#ifndef _ASM_MACH_TOPOLOGY_H
#define _ASM_MACH_TOPOLOGY_H

#ifdef CONFIG_NUMA

#define cpu_to_node(cpu)	((cpu) >> 2)
#define parent_node(node)	(node)
#define cpumask_of_node(node)	(&__node_data[(node)]->cpumask)

struct pci_bus;
extern int pcibus_to_node(struct pci_bus *);

#define cpumask_of_pcibus(bus)	(cpu_online_mask)

extern unsigned char __node_distances[MAX_NUMNODES][MAX_NUMNODES];

#define node_distance(from, to)	(__node_distances[(from)][(to)])

#endif

#include <asm-generic/topology.h>

#endif /* _ASM_MACH_TOPOLOGY_H */
