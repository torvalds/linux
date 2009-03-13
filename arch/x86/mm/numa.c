/* Common code for 32 and 64-bit NUMA */
#include <linux/topology.h>
#include <linux/module.h>
#include <linux/bootmem.h>

#ifdef CONFIG_DEBUG_PER_CPU_MAPS
# define DBG(x...) printk(KERN_DEBUG x)
#else
# define DBG(x...)
#endif

/*
 * Which logical CPUs are on which nodes
 */
cpumask_t *node_to_cpumask_map;
EXPORT_SYMBOL(node_to_cpumask_map);

/*
 * Allocate node_to_cpumask_map based on number of available nodes
 * Requires node_possible_map to be valid.
 *
 * Note: node_to_cpumask() is not valid until after this is done.
 * (Use CONFIG_DEBUG_PER_CPU_MAPS to check this.)
 */
void __init setup_node_to_cpumask_map(void)
{
	unsigned int node, num = 0;
	cpumask_t *map;

	/* setup nr_node_ids if not done yet */
	if (nr_node_ids == MAX_NUMNODES) {
		for_each_node_mask(node, node_possible_map)
			num = node;
		nr_node_ids = num + 1;
	}

	/* allocate the map */
	map = alloc_bootmem_low(nr_node_ids * sizeof(cpumask_t));
	DBG("node_to_cpumask_map at %p for %d nodes\n", map, nr_node_ids);

	pr_debug("Node to cpumask map at %p for %d nodes\n",
		 map, nr_node_ids);

	/* node_to_cpumask() will now work */
	node_to_cpumask_map = map;
}

#ifdef CONFIG_DEBUG_PER_CPU_MAPS
/*
 * Returns a pointer to the bitmask of CPUs on Node 'node'.
 */
const cpumask_t *cpumask_of_node(int node)
{
	if (node_to_cpumask_map == NULL) {
		printk(KERN_WARNING
			"cpumask_of_node(%d): no node_to_cpumask_map!\n",
			node);
		dump_stack();
		return cpu_online_mask;
	}
	if (node >= nr_node_ids) {
		printk(KERN_WARNING
			"cpumask_of_node(%d): node > nr_node_ids(%d)\n",
			node, nr_node_ids);
		dump_stack();
		return cpu_none_mask;
	}
	return &node_to_cpumask_map[node];
}
EXPORT_SYMBOL(cpumask_of_node);
#endif
