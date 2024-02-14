/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_MACH_TOPOLOGY_H
#define _ASM_MACH_TOPOLOGY_H	1

#include <asm/sn/types.h>
#include <asm/mmzone.h>

struct cpuinfo_ip27 {
	nasid_t		p_nasid;	/* my node ID in numa-as-id-space */
	unsigned short	p_speed;	/* cpu speed in MHz */
	unsigned char	p_slice;	/* Physical position on node board */
};

extern struct cpuinfo_ip27 sn_cpu_info[NR_CPUS];

#define cpu_to_node(cpu)	(cputonasid(cpu))
#define cpumask_of_node(node)	((node) == -1 ?				\
				 cpu_all_mask :				\
				 &hub_data(node)->h_cpus)
struct pci_bus;
extern int pcibus_to_node(struct pci_bus *);

#define cpumask_of_pcibus(bus)	(cpumask_of_node(pcibus_to_node(bus)))

extern unsigned char __node_distances[MAX_NUMNODES][MAX_NUMNODES];

#define node_distance(from, to) (__node_distances[(from)][(to)])

#include <asm-generic/topology.h>

#endif /* _ASM_MACH_TOPOLOGY_H */
