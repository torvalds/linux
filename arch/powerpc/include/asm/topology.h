/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_TOPOLOGY_H
#define _ASM_POWERPC_TOPOLOGY_H
#ifdef __KERNEL__


struct device;
struct device_node;

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

extern int __node_distance(int, int);
#define node_distance(a, b) __node_distance(a, b)

extern void __init dump_numa_cpu_topology(void);

extern int sysfs_add_device_to_node(struct device *dev, int nid);
extern void sysfs_remove_device_from_node(struct device *dev, int nid);
extern int numa_update_cpu_topology(bool cpus_locked);

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

static inline int numa_update_cpu_topology(bool cpus_locked)
{
	return 0;
}
#endif /* CONFIG_NUMA */

#if defined(CONFIG_NUMA) && defined(CONFIG_PPC_SPLPAR)
extern int start_topology_update(void);
extern int stop_topology_update(void);
extern int prrn_is_enabled(void);
#else
static inline int start_topology_update(void)
{
	return 0;
}
static inline int stop_topology_update(void)
{
	return 0;
}
static inline int prrn_is_enabled(void)
{
	return 0;
}
#endif /* CONFIG_NUMA && CONFIG_PPC_SPLPAR */

#if defined(CONFIG_HOTPLUG_CPU) || defined(CONFIG_NEED_MULTIPLE_NODES)
#if defined(CONFIG_PPC_SPLPAR)
extern int timed_topology_update(int nsecs);
#else
#define	timed_topology_update(nsecs)
#endif /* CONFIG_PPC_SPLPAR */
#endif /* CONFIG_HOTPLUG_CPU || CONFIG_NEED_MULTIPLE_NODES */

#include <asm-generic/topology.h>

#ifdef CONFIG_SMP
#include <asm/cputable.h>

#ifdef CONFIG_PPC64
#include <asm/smp.h>

#define topology_physical_package_id(cpu)	(cpu_to_chip_id(cpu))
#define topology_sibling_cpumask(cpu)	(per_cpu(cpu_sibling_map, cpu))
#define topology_core_cpumask(cpu)	(per_cpu(cpu_core_map, cpu))
#define topology_core_id(cpu)		(cpu_to_core_id(cpu))
#endif
#endif

#endif /* __KERNEL__ */
#endif	/* _ASM_POWERPC_TOPOLOGY_H */
