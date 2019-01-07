/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM_TOPOLOGY_H
#define _ASM_ARM_TOPOLOGY_H

#ifdef CONFIG_ARM_CPU_TOPOLOGY

#include <linux/cpumask.h>

struct cputopo_arm {
	int thread_id;
	int core_id;
	int socket_id;
	cpumask_t thread_sibling;
	cpumask_t core_sibling;
};

extern struct cputopo_arm cpu_topology[NR_CPUS];

#define topology_physical_package_id(cpu)	(cpu_topology[cpu].socket_id)
#define topology_core_id(cpu)		(cpu_topology[cpu].core_id)
#define topology_core_cpumask(cpu)	(&cpu_topology[cpu].core_sibling)
#define topology_sibling_cpumask(cpu)	(&cpu_topology[cpu].thread_sibling)

void init_cpu_topology(void);
void store_cpu_topology(unsigned int cpuid);
const struct cpumask *cpu_coregroup_mask(int cpu);

#include <linux/arch_topology.h>

/* Replace task scheduler's default frequency-invariant accounting */
#define arch_scale_freq_capacity topology_get_freq_scale

/* Replace task scheduler's default cpu-invariant accounting */
#define arch_scale_cpu_capacity topology_get_cpu_scale

/* Enable topology flag updates */
#define arch_update_cpu_topology topology_update_cpu_topology

#else

static inline void init_cpu_topology(void) { }
static inline void store_cpu_topology(unsigned int cpuid) { }

#endif

#include <asm-generic/topology.h>

#endif /* _ASM_ARM_TOPOLOGY_H */
