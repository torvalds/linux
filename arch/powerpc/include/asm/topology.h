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

#define parent_node(node)	(node)

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

#else

static inline void dump_numa_cpu_topology(void) {}

static inline int sysfs_add_device_to_node(struct device *dev, int nid)
{
	return 0;
}

static inline void sysfs_remove_device_from_node(struct device *dev,
						int nid)
{
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
