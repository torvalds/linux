/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_TOPOLOGY_H
#define __ASM_TOPOLOGY_H

#include <linux/cpumask.h>

#ifdef CONFIG_NUMA
#include <asm/numa.h>

struct pci_bus;
int pcibus_to_node(struct pci_bus *bus);
#define cpumask_of_pcibus(bus)	(pcibus_to_node(bus) == -1 ?		\
				 cpu_all_mask :				\
				 cpumask_of_node(pcibus_to_node(bus)))

#endif /* CONFIG_NUMA */

#include <linux/arch_topology.h>

void update_freq_counters_refs(void);

/* Replace task scheduler's default frequency-invariant accounting */
#define arch_scale_freq_tick topology_scale_freq_tick
#define arch_set_freq_scale topology_set_freq_scale
#define arch_scale_freq_capacity topology_get_freq_scale
#define arch_scale_freq_invariant topology_scale_freq_invariant
#define arch_scale_freq_ref topology_get_freq_ref

#ifdef CONFIG_ACPI_CPPC_LIB
#define arch_init_invariance_cppc topology_init_cpu_capacity_cppc
#endif

/* Replace task scheduler's default cpu-invariant accounting */
#define arch_scale_cpu_capacity topology_get_cpu_scale

/* Enable topology flag updates */
#define arch_update_cpu_topology topology_update_cpu_topology

/* Replace task scheduler's default HW pressure API */
#define arch_scale_hw_pressure topology_get_hw_pressure
#define arch_update_hw_pressure	topology_update_hw_pressure

#include <asm-generic/topology.h>

#endif /* _ASM_ARM_TOPOLOGY_H */
