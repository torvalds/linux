// SPDX-License-Identifier: GPL-2.0
/*
 * NUMA support for s390
 *
 * NUMA emulation (aka fake NUMA) distributes the available memory to nodes
 * without using real topology information about the physical memory of the
 * machine.
 *
 * It distributes the available CPUs to nodes while respecting the original
 * machine topology information. This is done by trying to avoid to separate
 * CPUs which reside on the same book or even on the same MC.
 *
 * Because the current Linux scheduler code requires a stable cpu to node
 * mapping, cores are pinned to nodes when the first CPU thread is set online.
 *
 * Copyright IBM Corp. 2015
 */

#define KMSG_COMPONENT "numa_emu"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/cpumask.h>
#include <linux/memblock.h>
#include <linux/node.h>
#include <linux/memory.h>
#include <linux/slab.h>
#include <asm/smp.h>
#include <asm/topology.h>
#include "numa_mode.h"
#include "toptree.h"

/* Distances between the different system components */
#define DIST_EMPTY	0
#define DIST_CORE	1
#define DIST_MC		2
#define DIST_BOOK	3
#define DIST_DRAWER	4
#define DIST_MAX	5

/* Node distance reported to common code */
#define EMU_NODE_DIST	10

/* Node ID for free (not yet pinned) cores */
#define NODE_ID_FREE	-1

/* Different levels of toptree */
enum toptree_level {CORE, MC, BOOK, DRAWER, NODE, TOPOLOGY};

/* The two toptree IDs */
enum {TOPTREE_ID_PHYS, TOPTREE_ID_NUMA};

/* Number of NUMA nodes */
static int emu_nodes = 1;
/* NUMA stripe size */
static unsigned long emu_size;

/*
 * Node to core pinning information updates are protected by
 * "sched_domains_mutex".
 */
static struct {
	s32 to_node_id[CONFIG_NR_CPUS];	/* Pinned core to node mapping */
	int total;			/* Total number of pinned cores */
	int per_node_target;		/* Cores per node without extra cores */
	int per_node[MAX_NUMNODES];	/* Number of cores pinned to node */
} *emu_cores;

/*
 * Pin a core to a node
 */
static void pin_core_to_node(int core_id, int node_id)
{
	if (emu_cores->to_node_id[core_id] == NODE_ID_FREE) {
		emu_cores->per_node[node_id]++;
		emu_cores->to_node_id[core_id] = node_id;
		emu_cores->total++;
	} else {
		WARN_ON(emu_cores->to_node_id[core_id] != node_id);
	}
}

/*
 * Number of pinned cores of a node
 */
static int cores_pinned(struct toptree *node)
{
	return emu_cores->per_node[node->id];
}

/*
 * ID of the node where the core is pinned (or NODE_ID_FREE)
 */
static int core_pinned_to_node_id(struct toptree *core)
{
	return emu_cores->to_node_id[core->id];
}

/*
 * Number of cores in the tree that are not yet pinned
 */
static int cores_free(struct toptree *tree)
{
	struct toptree *core;
	int count = 0;

	toptree_for_each(core, tree, CORE) {
		if (core_pinned_to_node_id(core) == NODE_ID_FREE)
			count++;
	}
	return count;
}

/*
 * Return node of core
 */
static struct toptree *core_node(struct toptree *core)
{
	return core->parent->parent->parent->parent;
}

/*
 * Return drawer of core
 */
static struct toptree *core_drawer(struct toptree *core)
{
	return core->parent->parent->parent;
}

/*
 * Return book of core
 */
static struct toptree *core_book(struct toptree *core)
{
	return core->parent->parent;
}

/*
 * Return mc of core
 */
static struct toptree *core_mc(struct toptree *core)
{
	return core->parent;
}

/*
 * Distance between two cores
 */
static int dist_core_to_core(struct toptree *core1, struct toptree *core2)
{
	if (core_drawer(core1)->id != core_drawer(core2)->id)
		return DIST_DRAWER;
	if (core_book(core1)->id != core_book(core2)->id)
		return DIST_BOOK;
	if (core_mc(core1)->id != core_mc(core2)->id)
		return DIST_MC;
	/* Same core or sibling on same MC */
	return DIST_CORE;
}

/*
 * Distance of a node to a core
 */
static int dist_node_to_core(struct toptree *node, struct toptree *core)
{
	struct toptree *core_node;
	int dist_min = DIST_MAX;

	toptree_for_each(core_node, node, CORE)
		dist_min = min(dist_min, dist_core_to_core(core_node, core));
	return dist_min == DIST_MAX ? DIST_EMPTY : dist_min;
}

/*
 * Unify will delete empty nodes, therefore recreate nodes.
 */
static void toptree_unify_tree(struct toptree *tree)
{
	int nid;

	toptree_unify(tree);
	for (nid = 0; nid < emu_nodes; nid++)
		toptree_get_child(tree, nid);
}

/*
 * Find the best/nearest node for a given core and ensure that no node
 * gets more than "emu_cores->per_node_target + extra" cores.
 */
static struct toptree *node_for_core(struct toptree *numa, struct toptree *core,
				     int extra)
{
	struct toptree *node, *node_best = NULL;
	int dist_cur, dist_best, cores_target;

	cores_target = emu_cores->per_node_target + extra;
	dist_best = DIST_MAX;
	node_best = NULL;
	toptree_for_each(node, numa, NODE) {
		/* Already pinned cores must use their nodes */
		if (core_pinned_to_node_id(core) == node->id) {
			node_best = node;
			break;
		}
		/* Skip nodes that already have enough cores */
		if (cores_pinned(node) >= cores_target)
			continue;
		dist_cur = dist_node_to_core(node, core);
		if (dist_cur < dist_best) {
			dist_best = dist_cur;
			node_best = node;
		}
	}
	return node_best;
}

/*
 * Find the best node for each core with respect to "extra" core count
 */
static void toptree_to_numa_single(struct toptree *numa, struct toptree *phys,
				   int extra)
{
	struct toptree *node, *core, *tmp;

	toptree_for_each_safe(core, tmp, phys, CORE) {
		node = node_for_core(numa, core, extra);
		if (!node)
			return;
		toptree_move(core, node);
		pin_core_to_node(core->id, node->id);
	}
}

/*
 * Move structures of given level to specified NUMA node
 */
static void move_level_to_numa_node(struct toptree *node, struct toptree *phys,
				    enum toptree_level level, bool perfect)
{
	int cores_free, cores_target = emu_cores->per_node_target;
	struct toptree *cur, *tmp;

	toptree_for_each_safe(cur, tmp, phys, level) {
		cores_free = cores_target - toptree_count(node, CORE);
		if (perfect) {
			if (cores_free == toptree_count(cur, CORE))
				toptree_move(cur, node);
		} else {
			if (cores_free >= toptree_count(cur, CORE))
				toptree_move(cur, node);
		}
	}
}

/*
 * Move structures of a given level to NUMA nodes. If "perfect" is specified
 * move only perfectly fitting structures. Otherwise move also smaller
 * than needed structures.
 */
static void move_level_to_numa(struct toptree *numa, struct toptree *phys,
			       enum toptree_level level, bool perfect)
{
	struct toptree *node;

	toptree_for_each(node, numa, NODE)
		move_level_to_numa_node(node, phys, level, perfect);
}

/*
 * For the first run try to move the big structures
 */
static void toptree_to_numa_first(struct toptree *numa, struct toptree *phys)
{
	struct toptree *core;

	/* Always try to move perfectly fitting structures first */
	move_level_to_numa(numa, phys, DRAWER, true);
	move_level_to_numa(numa, phys, DRAWER, false);
	move_level_to_numa(numa, phys, BOOK, true);
	move_level_to_numa(numa, phys, BOOK, false);
	move_level_to_numa(numa, phys, MC, true);
	move_level_to_numa(numa, phys, MC, false);
	/* Now pin all the moved cores */
	toptree_for_each(core, numa, CORE)
		pin_core_to_node(core->id, core_node(core)->id);
}

/*
 * Allocate new topology and create required nodes
 */
static struct toptree *toptree_new(int id, int nodes)
{
	struct toptree *tree;
	int nid;

	tree = toptree_alloc(TOPOLOGY, id);
	if (!tree)
		goto fail;
	for (nid = 0; nid < nodes; nid++) {
		if (!toptree_get_child(tree, nid))
			goto fail;
	}
	return tree;
fail:
	panic("NUMA emulation could not allocate topology");
}

/*
 * Allocate and initialize core to node mapping
 */
static void __ref create_core_to_node_map(void)
{
	int i;

	emu_cores = memblock_alloc(sizeof(*emu_cores), 8);
	if (!emu_cores)
		panic("%s: Failed to allocate %zu bytes align=0x%x\n",
		      __func__, sizeof(*emu_cores), 8);
	for (i = 0; i < ARRAY_SIZE(emu_cores->to_node_id); i++)
		emu_cores->to_node_id[i] = NODE_ID_FREE;
}

/*
 * Move cores from physical topology into NUMA target topology
 * and try to keep as much of the physical topology as possible.
 */
static struct toptree *toptree_to_numa(struct toptree *phys)
{
	static int first = 1;
	struct toptree *numa;
	int cores_total;

	cores_total = emu_cores->total + cores_free(phys);
	emu_cores->per_node_target = cores_total / emu_nodes;
	numa = toptree_new(TOPTREE_ID_NUMA, emu_nodes);
	if (first) {
		toptree_to_numa_first(numa, phys);
		first = 0;
	}
	toptree_to_numa_single(numa, phys, 0);
	toptree_to_numa_single(numa, phys, 1);
	toptree_unify_tree(numa);

	WARN_ON(cpumask_weight(&phys->mask));
	return numa;
}

/*
 * Create a toptree out of the physical topology that we got from the hypervisor
 */
static struct toptree *toptree_from_topology(void)
{
	struct toptree *phys, *node, *drawer, *book, *mc, *core;
	struct cpu_topology_s390 *top;
	int cpu;

	phys = toptree_new(TOPTREE_ID_PHYS, 1);

	for_each_cpu(cpu, &cpus_with_topology) {
		top = &cpu_topology[cpu];
		node = toptree_get_child(phys, 0);
		drawer = toptree_get_child(node, top->drawer_id);
		book = toptree_get_child(drawer, top->book_id);
		mc = toptree_get_child(book, top->socket_id);
		core = toptree_get_child(mc, smp_get_base_cpu(cpu));
		if (!drawer || !book || !mc || !core)
			panic("NUMA emulation could not allocate memory");
		cpumask_set_cpu(cpu, &core->mask);
		toptree_update_mask(mc);
	}
	return phys;
}

/*
 * Add toptree core to topology and create correct CPU masks
 */
static void topology_add_core(struct toptree *core)
{
	struct cpu_topology_s390 *top;
	int cpu;

	for_each_cpu(cpu, &core->mask) {
		top = &cpu_topology[cpu];
		cpumask_copy(&top->thread_mask, &core->mask);
		cpumask_copy(&top->core_mask, &core_mc(core)->mask);
		cpumask_copy(&top->book_mask, &core_book(core)->mask);
		cpumask_copy(&top->drawer_mask, &core_drawer(core)->mask);
		cpumask_set_cpu(cpu, &node_to_cpumask_map[core_node(core)->id]);
		top->node_id = core_node(core)->id;
	}
}

/*
 * Apply toptree to topology and create CPU masks
 */
static void toptree_to_topology(struct toptree *numa)
{
	struct toptree *core;
	int i;

	/* Clear all node masks */
	for (i = 0; i < MAX_NUMNODES; i++)
		cpumask_clear(&node_to_cpumask_map[i]);

	/* Rebuild all masks */
	toptree_for_each(core, numa, CORE)
		topology_add_core(core);
}

/*
 * Show the node to core mapping
 */
static void print_node_to_core_map(void)
{
	int nid, cid;

	if (!numa_debug_enabled)
		return;
	printk(KERN_DEBUG "NUMA node to core mapping\n");
	for (nid = 0; nid < emu_nodes; nid++) {
		printk(KERN_DEBUG "  node %3d: ", nid);
		for (cid = 0; cid < ARRAY_SIZE(emu_cores->to_node_id); cid++) {
			if (emu_cores->to_node_id[cid] == nid)
				printk(KERN_CONT "%d ", cid);
		}
		printk(KERN_CONT "\n");
	}
}

static void pin_all_possible_cpus(void)
{
	int core_id, node_id, cpu;
	static int initialized;

	if (initialized)
		return;
	print_node_to_core_map();
	node_id = 0;
	for_each_possible_cpu(cpu) {
		core_id = smp_get_base_cpu(cpu);
		if (emu_cores->to_node_id[core_id] != NODE_ID_FREE)
			continue;
		pin_core_to_node(core_id, node_id);
		cpu_topology[cpu].node_id = node_id;
		node_id = (node_id + 1) % emu_nodes;
	}
	print_node_to_core_map();
	initialized = 1;
}

/*
 * Transfer physical topology into a NUMA topology and modify CPU masks
 * according to the NUMA topology.
 *
 * Must be called with "sched_domains_mutex" lock held.
 */
static void emu_update_cpu_topology(void)
{
	struct toptree *phys, *numa;

	if (emu_cores == NULL)
		create_core_to_node_map();
	phys = toptree_from_topology();
	numa = toptree_to_numa(phys);
	toptree_free(phys);
	toptree_to_topology(numa);
	toptree_free(numa);
	pin_all_possible_cpus();
}

/*
 * If emu_size is not set, use CONFIG_EMU_SIZE. Then round to minimum
 * alignment (needed for memory hotplug).
 */
static unsigned long emu_setup_size_adjust(unsigned long size)
{
	unsigned long size_new;

	size = size ? : CONFIG_EMU_SIZE;
	size_new = roundup(size, memory_block_size_bytes());
	if (size_new == size)
		return size;
	pr_warn("Increasing memory stripe size from %ld MB to %ld MB\n",
		size >> 20, size_new >> 20);
	return size_new;
}

/*
 * If we have not enough memory for the specified nodes, reduce the node count.
 */
static int emu_setup_nodes_adjust(int nodes)
{
	int nodes_max;

	nodes_max = memblock.memory.total_size / emu_size;
	nodes_max = max(nodes_max, 1);
	if (nodes_max >= nodes)
		return nodes;
	pr_warn("Not enough memory for %d nodes, reducing node count\n", nodes);
	return nodes_max;
}

/*
 * Early emu setup
 */
static void emu_setup(void)
{
	int nid;

	emu_size = emu_setup_size_adjust(emu_size);
	emu_nodes = emu_setup_nodes_adjust(emu_nodes);
	for (nid = 0; nid < emu_nodes; nid++)
		node_set(nid, node_possible_map);
	pr_info("Creating %d nodes with memory stripe size %ld MB\n",
		emu_nodes, emu_size >> 20);
}

/*
 * Return node id for given page number
 */
static int emu_pfn_to_nid(unsigned long pfn)
{
	return (pfn / (emu_size >> PAGE_SHIFT)) % emu_nodes;
}

/*
 * Return stripe size
 */
static unsigned long emu_align(void)
{
	return emu_size;
}

/*
 * Return distance between two nodes
 */
static int emu_distance(int node1, int node2)
{
	return (node1 != node2) * EMU_NODE_DIST;
}

/*
 * Define callbacks for generic s390 NUMA infrastructure
 */
const struct numa_mode numa_mode_emu = {
	.name = "emu",
	.setup = emu_setup,
	.update_cpu_topology = emu_update_cpu_topology,
	.__pfn_to_nid = emu_pfn_to_nid,
	.align = emu_align,
	.distance = emu_distance,
};

/*
 * Kernel parameter: emu_nodes=<n>
 */
static int __init early_parse_emu_nodes(char *p)
{
	int count;

	if (kstrtoint(p, 0, &count) != 0 || count <= 0)
		return 0;
	if (count <= 0)
		return 0;
	emu_nodes = min(count, MAX_NUMNODES);
	return 0;
}
early_param("emu_nodes", early_parse_emu_nodes);

/*
 * Kernel parameter: emu_size=[<n>[k|M|G|T]]
 */
static int __init early_parse_emu_size(char *p)
{
	emu_size = memparse(p, NULL);
	return 0;
}
early_param("emu_size", early_parse_emu_size);
