// SPDX-License-Identifier: GPL-2.0
/*
 * NUMA support for s390
 *
 * Implement NUMA core code.
 *
 * Copyright IBM Corp. 2015
 */

#define KMSG_COMPONENT "numa"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/mmzone.h>
#include <linux/cpumask.h>
#include <linux/memblock.h>
#include <linux/slab.h>
#include <linux/node.h>

#include <asm/numa.h>
#include "numa_mode.h"

pg_data_t *node_data[MAX_NUMNODES];
EXPORT_SYMBOL(node_data);

cpumask_t node_to_cpumask_map[MAX_NUMNODES];
EXPORT_SYMBOL(node_to_cpumask_map);

static void plain_setup(void)
{
	node_set(0, node_possible_map);
}

const struct numa_mode numa_mode_plain = {
	.name = "plain",
	.setup = plain_setup,
};

static const struct numa_mode *mode = &numa_mode_plain;

int numa_pfn_to_nid(unsigned long pfn)
{
	return mode->__pfn_to_nid ? mode->__pfn_to_nid(pfn) : 0;
}

void numa_update_cpu_topology(void)
{
	if (mode->update_cpu_topology)
		mode->update_cpu_topology();
}

int __node_distance(int a, int b)
{
	return mode->distance ? mode->distance(a, b) : 0;
}
EXPORT_SYMBOL(__node_distance);

int numa_debug_enabled;

/*
 * numa_setup_memory() - Assign bootmem to nodes
 *
 * The memory is first added to memblock without any respect to nodes.
 * This is fixed before remaining memblock memory is handed over to the
 * buddy allocator.
 * An important side effect is that large bootmem allocations might easily
 * cross node boundaries, which can be needed for large allocations with
 * smaller memory stripes in each node (i.e. when using NUMA emulation).
 *
 * Memory defines nodes:
 * Therefore this routine also sets the nodes online with memory.
 */
static void __init numa_setup_memory(void)
{
	unsigned long cur_base, align, end_of_dram;
	int nid = 0;

	end_of_dram = memblock_end_of_DRAM();
	align = mode->align ? mode->align() : ULONG_MAX;

	/*
	 * Step through all available memory and assign it to the nodes
	 * indicated by the mode implementation.
	 * All nodes which are seen here will be set online.
	 */
	cur_base = 0;
	do {
		nid = numa_pfn_to_nid(PFN_DOWN(cur_base));
		node_set_online(nid);
		memblock_set_node(cur_base, align, &memblock.memory, nid);
		cur_base += align;
	} while (cur_base < end_of_dram);

	/* Allocate and fill out node_data */
	for (nid = 0; nid < MAX_NUMNODES; nid++) {
		NODE_DATA(nid) = memblock_alloc(sizeof(pg_data_t), 8);
		if (!NODE_DATA(nid))
			panic("%s: Failed to allocate %zu bytes align=0x%x\n",
			      __func__, sizeof(pg_data_t), 8);
	}

	for_each_online_node(nid) {
		unsigned long start_pfn, end_pfn;
		unsigned long t_start, t_end;
		int i;

		start_pfn = ULONG_MAX;
		end_pfn = 0;
		for_each_mem_pfn_range(i, nid, &t_start, &t_end, NULL) {
			if (t_start < start_pfn)
				start_pfn = t_start;
			if (t_end > end_pfn)
				end_pfn = t_end;
		}
		NODE_DATA(nid)->node_spanned_pages = end_pfn - start_pfn;
		NODE_DATA(nid)->node_id = nid;
	}
}

/*
 * numa_setup() - Earliest initialization
 *
 * Assign the mode and call the mode's setup routine.
 */
void __init numa_setup(void)
{
	pr_info("NUMA mode: %s\n", mode->name);
	nodes_clear(node_possible_map);
	/* Initially attach all possible CPUs to node 0. */
	cpumask_copy(&node_to_cpumask_map[0], cpu_possible_mask);
	if (mode->setup)
		mode->setup();
	numa_setup_memory();
	memblock_dump_all();
}

/*
 * numa_init_late() - Initialization initcall
 *
 * Register NUMA nodes.
 */
static int __init numa_init_late(void)
{
	int nid;

	for_each_online_node(nid)
		register_one_node(nid);
	return 0;
}
arch_initcall(numa_init_late);

static int __init parse_debug(char *parm)
{
	numa_debug_enabled = 1;
	return 0;
}
early_param("numa_debug", parse_debug);

static int __init parse_numa(char *parm)
{
	if (strcmp(parm, numa_mode_plain.name) == 0)
		mode = &numa_mode_plain;
#ifdef CONFIG_NUMA_EMU
	if (strcmp(parm, numa_mode_emu.name) == 0)
		mode = &numa_mode_emu;
#endif
	return 0;
}
early_param("numa", parse_numa);
