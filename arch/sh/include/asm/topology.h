/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SH_TOPOLOGY_H
#define _ASM_SH_TOPOLOGY_H

#define mc_capable()    (1)

const struct cpumask *cpu_coregroup_mask(int cpu);

extern cpumask_t cpu_core_map[NR_CPUS];

#define topology_core_cpumask(cpu)	(&cpu_core_map[cpu])

#include <asm-generic/topology.h>

#endif /* _ASM_SH_TOPOLOGY_H */
