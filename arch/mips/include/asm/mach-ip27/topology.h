#ifndef _ASM_MACH_TOPOLOGY_H
#define _ASM_MACH_TOPOLOGY_H	1

#include <asm/sn/hub.h>
#include <asm/sn/types.h>
#include <asm/mmzone.h>

struct cpuinfo_ip27 {
//	cpuid_t		p_cpuid;	/* PROM assigned cpuid */
	cnodeid_t	p_nodeid;	/* my node ID in compact-id-space */
	nasid_t		p_nasid;	/* my node ID in numa-as-id-space */
	unsigned char	p_slice;	/* Physical position on node board */
#if 0
	unsigned long		loops_per_sec;
	unsigned long		ipi_count;
	unsigned long		irq_attempt[NR_IRQS];
	unsigned long		smp_local_irq_count;
	unsigned long		prof_multiplier;
	unsigned long		prof_counter;
#endif
};

extern struct cpuinfo_ip27 sn_cpu_info[NR_CPUS];

#define cpu_to_node(cpu)	(sn_cpu_info[(cpu)].p_nodeid)
#define cpumask_of_node(node)	((node) == -1 ?				\
				 cpu_all_mask :				\
				 &hub_data(node)->h_cpus)
struct pci_bus;
extern int pcibus_to_node(struct pci_bus *);

#define cpumask_of_pcibus(bus)	(cpu_online_mask)

extern unsigned char __node_distances[MAX_COMPACT_NODES][MAX_COMPACT_NODES];

#define node_distance(from, to) (__node_distances[(from)][(to)])

#include <asm-generic/topology.h>

#endif /* _ASM_MACH_TOPOLOGY_H */
