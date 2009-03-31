#ifndef _ASM_SH_TOPOLOGY_H
#define _ASM_SH_TOPOLOGY_H

#ifdef CONFIG_NUMA

/* sched_domains SD_NODE_INIT for sh machines */
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
	.newidle_idx		= 2,			\
	.wake_idx		= 1,			\
	.forkexec_idx		= 1,			\
	.flags			= SD_LOAD_BALANCE	\
				| SD_BALANCE_FORK	\
				| SD_BALANCE_EXEC	\
				| SD_SERIALIZE		\
				| SD_WAKE_BALANCE,	\
	.last_balance		= jiffies,		\
	.balance_interval	= 1,			\
	.nr_balance_failed	= 0,			\
}

#define cpu_to_node(cpu)	((void)(cpu),0)
#define parent_node(node)	((void)(node),0)

#define node_to_cpumask(node)	((void)node, cpu_online_map)
#define cpumask_of_node(node)	((void)node, cpu_online_mask)

#define pcibus_to_node(bus)	((void)(bus), -1)
#define pcibus_to_cpumask(bus)	(pcibus_to_node(bus) == -1 ? \
					CPU_MASK_ALL : \
					node_to_cpumask(pcibus_to_node(bus)) \
				)
#endif

#include <asm-generic/topology.h>

#endif /* _ASM_SH_TOPOLOGY_H */
