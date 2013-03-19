#ifndef _ASM_METAG_TOPOLOGY_H
#define _ASM_METAG_TOPOLOGY_H

#ifdef CONFIG_NUMA

/* sched_domains SD_NODE_INIT for Meta machines */
#define SD_NODE_INIT (struct sched_domain) {		\
	.parent			= NULL,			\
	.child			= NULL,			\
	.groups			= NULL,			\
	.min_interval		= 8,			\
	.max_interval		= 32,			\
	.busy_factor		= 32,			\
	.imbalance_pct		= 125,			\
	.cache_nice_tries	= 2,			\
	.busy_idx		= 3,			\
	.idle_idx		= 2,			\
	.newidle_idx		= 0,			\
	.wake_idx		= 0,			\
	.forkexec_idx		= 0,			\
	.flags			= SD_LOAD_BALANCE	\
				| SD_BALANCE_FORK	\
				| SD_BALANCE_EXEC	\
				| SD_BALANCE_NEWIDLE	\
				| SD_SERIALIZE,		\
	.last_balance		= jiffies,		\
	.balance_interval	= 1,			\
	.nr_balance_failed	= 0,			\
}

#define cpu_to_node(cpu)	((void)(cpu), 0)
#define parent_node(node)	((void)(node), 0)

#define cpumask_of_node(node)	((void)node, cpu_online_mask)

#define pcibus_to_node(bus)	((void)(bus), -1)
#define cpumask_of_pcibus(bus)	(pcibus_to_node(bus) == -1 ? \
					cpu_all_mask : \
					cpumask_of_node(pcibus_to_node(bus)))

#endif

#define mc_capable()    (1)

const struct cpumask *cpu_coregroup_mask(unsigned int cpu);

extern cpumask_t cpu_core_map[NR_CPUS];

#define topology_core_cpumask(cpu)	(&cpu_core_map[cpu])

#include <asm-generic/topology.h>

#endif /* _ASM_METAG_TOPOLOGY_H */
