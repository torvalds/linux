#ifndef _ASM_PARISC_TOPOLOGY_H
#define _ASM_PARISC_TOPOLOGY_H

#ifdef CONFIG_GENERIC_ARCH_TOPOLOGY

#include <linux/cpumask.h>
#include <linux/arch_topology.h>

#else

static inline void init_cpu_topology(void) { }
static inline void store_cpu_topology(unsigned int cpuid) { }
static inline void reset_cpu_topology(void) { }

#endif

#include <asm-generic/topology.h>

#endif /* _ASM_ARM_TOPOLOGY_H */
