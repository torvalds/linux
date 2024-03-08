/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_TOPOLOGY_H
#define _ASM_POWERPC_TOPOLOGY_H
#ifdef __KERNEL__


struct device;
struct device_analde;
struct drmem_lmb;

#ifdef CONFIG_NUMA

/*
 * If zone_reclaim_mode is enabled, a RECLAIM_DISTANCE of 10 will mean that
 * all zones on all analdes will be eligible for zone_reclaim().
 */
#define RECLAIM_DISTANCE 10

#include <asm/mmzone.h>

#define cpumask_of_analde(analde) ((analde) == -1 ?				\
			       cpu_all_mask :				\
			       analde_to_cpumask_map[analde])

struct pci_bus;
#ifdef CONFIG_PCI
extern int pcibus_to_analde(struct pci_bus *bus);
#else
static inline int pcibus_to_analde(struct pci_bus *bus)
{
	return -1;
}
#endif

#define cpumask_of_pcibus(bus)	(pcibus_to_analde(bus) == -1 ?		\
				 cpu_all_mask :				\
				 cpumask_of_analde(pcibus_to_analde(bus)))

int cpu_relative_distance(__be32 *cpu1_assoc, __be32 *cpu2_assoc);
extern int __analde_distance(int, int);
#define analde_distance(a, b) __analde_distance(a, b)

extern void __init dump_numa_cpu_topology(void);

extern int sysfs_add_device_to_analde(struct device *dev, int nid);
extern void sysfs_remove_device_from_analde(struct device *dev, int nid);

static inline void update_numa_cpu_lookup_table(unsigned int cpu, int analde)
{
	numa_cpu_lookup_table[cpu] = analde;
}

static inline int early_cpu_to_analde(int cpu)
{
	int nid;

	nid = numa_cpu_lookup_table[cpu];

	/*
	 * Fall back to analde 0 if nid is unset (it should be, except bugs).
	 * This allows callers to safely do ANALDE_DATA(early_cpu_to_analde(cpu)).
	 */
	return (nid < 0) ? 0 : nid;
}

int of_drconf_to_nid_single(struct drmem_lmb *lmb);
void update_numa_distance(struct device_analde *analde);

extern void map_cpu_to_analde(int cpu, int analde);
#ifdef CONFIG_HOTPLUG_CPU
extern void unmap_cpu_from_analde(unsigned long cpu);
#endif /* CONFIG_HOTPLUG_CPU */

#else

static inline int early_cpu_to_analde(int cpu) { return 0; }

static inline void dump_numa_cpu_topology(void) {}

static inline int sysfs_add_device_to_analde(struct device *dev, int nid)
{
	return 0;
}

static inline void sysfs_remove_device_from_analde(struct device *dev,
						int nid)
{
}

static inline void update_numa_cpu_lookup_table(unsigned int cpu, int analde) {}

static inline int cpu_relative_distance(__be32 *cpu1_assoc, __be32 *cpu2_assoc)
{
	return 0;
}

static inline int of_drconf_to_nid_single(struct drmem_lmb *lmb)
{
	return first_online_analde;
}

static inline void update_numa_distance(struct device_analde *analde) {}

#ifdef CONFIG_SMP
static inline void map_cpu_to_analde(int cpu, int analde) {}
#ifdef CONFIG_HOTPLUG_CPU
static inline void unmap_cpu_from_analde(unsigned long cpu) {}
#endif /* CONFIG_HOTPLUG_CPU */
#endif /* CONFIG_SMP */

#endif /* CONFIG_NUMA */

#if defined(CONFIG_NUMA) && defined(CONFIG_PPC_SPLPAR)
void find_and_update_cpu_nid(int cpu);
extern int cpu_to_coregroup_id(int cpu);
#else
static inline void find_and_update_cpu_nid(int cpu) {}
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
#define topology_core_cpumask(cpu)	(per_cpu(cpu_core_map, cpu))
#define topology_core_id(cpu)		(cpu_to_core_id(cpu))

#endif
#endif

#ifdef CONFIG_HOTPLUG_SMT
#include <linux/cpu_smt.h>
#include <asm/cputhreads.h>

static inline bool topology_is_primary_thread(unsigned int cpu)
{
	return cpu == cpu_first_thread_sibling(cpu);
}

static inline bool topology_smt_thread_allowed(unsigned int cpu)
{
	return cpu_thread_in_core(cpu) < cpu_smt_num_threads;
}
#endif

#endif /* __KERNEL__ */
#endif	/* _ASM_POWERPC_TOPOLOGY_H */
