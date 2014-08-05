/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2013 Broadcom Corporation
 */
#ifndef _ASM_MACH_NETLOGIC_TOPOLOGY_H
#define _ASM_MACH_NETLOGIC_TOPOLOGY_H

#include <asm/mach-netlogic/multi-node.h>

#ifdef CONFIG_SMP
#define topology_physical_package_id(cpu)	cpu_to_node(cpu)
#define topology_core_id(cpu)	(cpu_logical_map(cpu) / NLM_THREADS_PER_CORE)
#define topology_thread_cpumask(cpu)		(&cpu_sibling_map[cpu])
#define topology_core_cpumask(cpu)	cpumask_of_node(cpu_to_node(cpu))
#endif

#include <asm-generic/topology.h>

#endif /* _ASM_MACH_NETLOGIC_TOPOLOGY_H */
