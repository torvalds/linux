/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_TOPOLOGY_H
#define _ASM_POWERPC_TOPOLOGY_H
#ifdef __KERNEL__


struct device;
struct device_node;
struct drmem_lmb;

#ifdef CONFIG_NUMA

/*
 * If zone_reclaim_mode is enabled, a RECLAIM_DISTANCE of 10 will mean that
 * all zones on all nodes will be eligible for zone_reclaim().
 */
#define RECLAIM_DISTANCE 10

#include <asm/mmzone.h>

#define cpumask_of_node(node) ((node) == -1 ?				\
			       cpu_all_mask :				\
			       node_to_cpumask_map[node])

struct pci_bus;
#ifdef CONFIG_PCI
extern int pcibus_to_node(struct pci_bus *bus);
#else
static inline int pcibus_to_node(struct pci_bus *bus)
{
	return -1;
}
#endif

#define cpumask_of_pcibus(bus)	(pcibus_to_node(bus) == -1 ?		\
				 cpu_all_mask :				\
				 cpumask_of_node(pcibus_to_node(bus)))

extern int cpu_distance(__be32 *cpu1_assoc, __be32 *cpu2_assoc);
extern int __node_distance(int, int);
#define node_distance(a, b) __node_distance(a, b)

extern void __init dump_numa_cpu_topology(void);

extern int sysfs_add_device_to_node(struct device *dev, int nid);
extern void sysfs_remove_device_from_node(struct device *dev, int nid);

static inline void update_numa_cpu_lookup_table(unsigned int cpu, int node)
{
	numa_cpu_lookup_table[cpu] = node;
}

static inline int early_cpu_to_node(int cpu)
{
	int nid;

	nid = numa_cpu_lookup_table[cpu];

	/*
	 * Fall back to node 0 if nid is unset (it should be, except bugs).
	 * This allows callers to safely do NODE_DATA(early_cpu_to_node(cpu)).
	 */
	return (nid < 0) ? 0 : nid;
}

int of_drconf_to_nid_single(struct drmem_lmb *lmb);

#else

static inline int early_cpu_to_node(int cpu) { return 0; }

static inline void dump_numa_cpu_topology(void) {}

static inline int sysfs_add_device_to_node(struct device *dev, int nid)
{
	return 0;
}

static inline void sysfs_remove_device_from_node(struct device *dev,
						int nid)
{
}

static inline void update_numa_cpu_lookup_table(unsigned int cpu, int node) {}

static inline int cpu_distance(__be32 *cpu1_assoc, __be32 *cpu2_assoc)
{
	return 0;
}

static inline int of_drconf_to_nid_single(struct drmem_lmb *lmb)
{
	return first_online_node;
}

#endif /* CONFIG_NUMA */

#if defined(CONFIG_NUMA) && defined(CONFIG_PPC_SPLPAR)
extern int find_and_online_cpu_nid(int cpu);
extern int cpu_to_coregroup_id(int cpu);
#else
static inline int find_and_online_cpu_nid(int cpu)
{
	return 0;
}

static inline int cpu_to_coregroup_id(int cpu)
{
#ifdef CONFIG_SMP
	return cpu_to_core_id(cpu);
#else
	return 0;
#endif
}

#endif /* CONFIG_NUMA && CONFIG_PPC_SPLPAR */

#include <asm-generic/topology.h>

#ifdef CONFIG_SMP
#include <asm/cputable.h>

#ifdef CONFIG_PPC64
#include <asm/smp.h>

#define topology_physical_package_id(cpu)	(cpu_to_chip_id(cpu))

#define topology_sibling_cpumask(cpu)	(per_cpu(cpu_sibling_map, cpu))
#define topology_core_cpumask(cpu)	(cpu_cpu_mask(cpu))
#define topology_core_id(cpu)		(cpu_to_core_id(cpu))

#endif
#endif

#endif /* __KERNEL__ */
#endif	/* _ASM_POWERPC_TOPOLOGY_H */
