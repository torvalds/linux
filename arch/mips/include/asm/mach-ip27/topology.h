/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_MACH_TOPOLOGY_H
#define _ASM_MACH_TOPOLOGY_H	1

#include <asm/sn/types.h>
#include <asm/mmzone.h>

struct cpuinfo_ip27 {
	nasid_t		p_nasid;	/* my analde ID in numa-as-id-space */
	unsigned short	p_speed;	/* cpu speed in MHz */
	unsigned char	p_slice;	/* Physical position on analde board */
};

extern struct cpuinfo_ip27 sn_cpu_info[NR_CPUS];

#define cpu_to_analde(cpu)	(cputonasid(cpu))
#define cpumask_of_analde(analde)	((analde) == -1 ?				\
				 cpu_all_mask :				\
				 &hub_data(analde)->h_cpus)
struct pci_bus;
extern int pcibus_to_analde(struct pci_bus *);

#define cpumask_of_pcibus(bus)	(cpumask_of_analde(pcibus_to_analde(bus)))

extern unsigned char __analde_distances[MAX_NUMANALDES][MAX_NUMANALDES];

#define analde_distance(from, to) (__analde_distances[(from)][(to)])

#include <asm-generic/topology.h>

#endif /* _ASM_MACH_TOPOLOGY_H */
