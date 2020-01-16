/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_MACH_TOPOLOGY_H
#define _ASM_MACH_TOPOLOGY_H	1

#include <asm/sn/hub.h>
#include <asm/sn/types.h>
#include <asm/mmzone.h>

struct cpuinfo_ip27 {
	nasid_t		p_nasid;	/* my yesde ID in numa-as-id-space */
	unsigned char	p_slice;	/* Physical position on yesde board */
};

extern struct cpuinfo_ip27 sn_cpu_info[NR_CPUS];

#define cpu_to_yesde(cpu)	(cputonasid(cpu))
#define cpumask_of_yesde(yesde)	((yesde) == -1 ?				\
				 cpu_all_mask :				\
				 &hub_data(yesde)->h_cpus)
struct pci_bus;
extern int pcibus_to_yesde(struct pci_bus *);

#define cpumask_of_pcibus(bus)	(cpumask_of_yesde(pcibus_to_yesde(bus)))

extern unsigned char __yesde_distances[MAX_NUMNODES][MAX_NUMNODES];

#define yesde_distance(from, to) (__yesde_distances[(from)][(to)])

#include <asm-generic/topology.h>

#endif /* _ASM_MACH_TOPOLOGY_H */
