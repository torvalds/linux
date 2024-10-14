// SPDX-License-Identifier: GPL-2.0-only
/*
 * NUMA support, based on the x86 implementation.
 *
 * Copyright (C) 2015 Cavium Inc.
 * Author: Ganapatrao Kulkarni <gkulkarni@cavium.com>
 */

#define pr_fmt(fmt) "NUMA: " fmt

#include <linux/acpi.h>
#include <linux/memblock.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/numa_memblks.h>

#include <asm/sections.h>

static int cpu_to_node_map[NR_CPUS] = { [0 ... NR_CPUS-1] = NUMA_NO_NODE };

bool numa_off;

static __init int numa_parse_early_param(char *opt)
{
	if (!opt)
		return -EINVAL;
	if (str_has_prefix(opt, "off"))
		numa_off = true;
	if (!strncmp(opt, "fake=", 5))
		return numa_emu_cmdline(opt + 5);

	return 0;
}
early_param("numa", numa_parse_early_param);

cpumask_var_t node_to_cpumask_map[MAX_NUMNODES];
EXPORT_SYMBOL(node_to_cpumask_map);

#ifdef CONFIG_DEBUG_PER_CPU_MAPS

/*
 * Returns a pointer to the bitmask of CPUs on Node 'node'.
 */
const struct cpumask *cpumask_of_node(int node)
{

	if (node == NUMA_NO_NODE)
		return cpu_all_mask;

	if (WARN_ON(node < 0 || node >= nr_node_ids))
		return cpu_none_mask;

	if (WARN_ON(node_to_cpumask_map[node] == NULL))
		return cpu_online_mask;

	return node_to_cpumask_map[node];
}
EXPORT_SYMBOL(cpumask_of_node);

#endif

#ifndef CONFIG_NUMA_EMU
static void numa_update_cpu(unsigned int cpu, bool remove)
{
	int nid = cpu_to_node(cpu);

	if (nid == NUMA_NO_NODE)
		return;

	if (remove)
		cpumask_clear_cpu(cpu, node_to_cpumask_map[nid]);
	else
		cpumask_set_cpu(cpu, node_to_cpumask_map[nid]);
}

void numa_add_cpu(unsigned int cpu)
{
	numa_update_cpu(cpu, false);
}

void numa_remove_cpu(unsigned int cpu)
{
	numa_update_cpu(cpu, true);
}
#endif

void numa_clear_node(unsigned int cpu)
{
	numa_remove_cpu(cpu);
	set_cpu_numa_node(cpu, NUMA_NO_NODE);
}

/*
 * Allocate node_to_cpumask_map based on number of available nodes
 * Requires node_possible_map to be valid.
 *
 * Note: cpumask_of_node() is not valid until after this is done.
 * (Use CONFIG_DEBUG_PER_CPU_MAPS to check this.)
 */
static void __init setup_node_to_cpumask_map(void)
{
	int node;

	/* setup nr_node_ids if not done yet */
	if (nr_node_ids == MAX_NUMNODES)
		setup_nr_node_ids();

	/* allocate and clear the mapping */
	for (node = 0; node < nr_node_ids; node++) {
		alloc_bootmem_cpumask_var(&node_to_cpumask_map[node]);
		cpumask_clear(node_to_cpumask_map[node]);
	}

	/* cpumask_of_node() will now work */
	pr_debug("Node to cpumask map for %u nodes\n", nr_node_ids);
}

/*
 * Set the cpu to node and mem mapping
 */
void numa_store_cpu_info(unsigned int cpu)
{
	set_cpu_numa_node(cpu, cpu_to_node_map[cpu]);
}

void __init early_map_cpu_to_node(unsigned int cpu, int nid)
{
	/* fallback to node 0 */
	if (nid < 0 || nid >= MAX_NUMNODES || numa_off)
		nid = 0;

	cpu_to_node_map[cpu] = nid;

	/*
	 * We should set the numa node of cpu0 as soon as possible, because it
	 * has already been set up online before. cpu_to_node(0) will soon be
	 * called.
	 */
	if (!cpu)
		set_cpu_numa_node(cpu, nid);
}

#ifdef CONFIG_HAVE_SETUP_PER_CPU_AREA
unsigned long __per_cpu_offset[NR_CPUS] __read_mostly;
EXPORT_SYMBOL(__per_cpu_offset);

int early_cpu_to_node(int cpu)
{
	return cpu_to_node_map[cpu];
}

static int __init pcpu_cpu_distance(unsigned int from, unsigned int to)
{
	return node_distance(early_cpu_to_node(from), early_cpu_to_node(to));
}

void __init setup_per_cpu_areas(void)
{
	unsigned long delta;
	unsigned int cpu;
	int rc = -EINVAL;

	if (pcpu_chosen_fc != PCPU_FC_PAGE) {
		/*
		 * Always reserve area for module percpu variables.  That's
		 * what the legacy allocator did.
		 */
		rc = pcpu_embed_first_chunk(PERCPU_MODULE_RESERVE,
					    PERCPU_DYNAMIC_RESERVE, PAGE_SIZE,
					    pcpu_cpu_distance,
					    early_cpu_to_node);
#ifdef CONFIG_NEED_PER_CPU_PAGE_FIRST_CHUNK
		if (rc < 0)
			pr_warn("PERCPU: %s allocator failed (%d), falling back to page size\n",
				   pcpu_fc_names[pcpu_chosen_fc], rc);
#endif
	}

#ifdef CONFIG_NEED_PER_CPU_PAGE_FIRST_CHUNK
	if (rc < 0)
		rc = pcpu_page_first_chunk(PERCPU_MODULE_RESERVE, early_cpu_to_node);
#endif
	if (rc < 0)
		panic("Failed to initialize percpu areas (err=%d).", rc);

	delta = (unsigned long)pcpu_base_addr - (unsigned long)__per_cpu_start;
	for_each_possible_cpu(cpu)
		__per_cpu_offset[cpu] = delta + pcpu_unit_offsets[cpu];
}
#endif

/*
 * Initialize NODE_DATA for a node on the local memory
 */
static void __init setup_node_data(int nid, u64 start_pfn, u64 end_pfn)
{
	if (start_pfn >= end_pfn)
		pr_info("Initmem setup node %d [<memory-less node>]\n", nid);

	alloc_node_data(nid);

	NODE_DATA(nid)->node_id = nid;
	NODE_DATA(nid)->node_start_pfn = start_pfn;
	NODE_DATA(nid)->node_spanned_pages = end_pfn - start_pfn;
}

static int __init numa_register_nodes(void)
{
	int nid;

	/* Finally register nodes. */
	for_each_node_mask(nid, numa_nodes_parsed) {
		unsigned long start_pfn, end_pfn;

		get_pfn_range_for_nid(nid, &start_pfn, &end_pfn);
		setup_node_data(nid, start_pfn, end_pfn);
		node_set_online(nid);
	}

	/* Setup online nodes to actual nodes*/
	node_possible_map = numa_nodes_parsed;

	return 0;
}

static int __init numa_init(int (*init_func)(void))
{
	int ret;

	nodes_clear(numa_nodes_parsed);
	nodes_clear(node_possible_map);
	nodes_clear(node_online_map);

	ret = numa_memblks_init(init_func, /* memblock_force_top_down */ false);
	if (ret < 0)
		goto out_free_distance;

	if (nodes_empty(numa_nodes_parsed)) {
		pr_info("No NUMA configuration found\n");
		ret = -EINVAL;
		goto out_free_distance;
	}

	ret = numa_register_nodes();
	if (ret < 0)
		goto out_free_distance;

	setup_node_to_cpumask_map();

	return 0;
out_free_distance:
	numa_reset_distance();
	return ret;
}

/**
 * dummy_numa_init() - Fallback dummy NUMA init
 *
 * Used if there's no underlying NUMA architecture, NUMA initialization
 * fails, or NUMA is disabled on the command line.
 *
 * Must online at least one node (node 0) and add memory blocks that cover all
 * allowed memory. It is unlikely that this function fails.
 *
 * Return: 0 on success, -errno on failure.
 */
static int __init dummy_numa_init(void)
{
	phys_addr_t start = memblock_start_of_DRAM();
	phys_addr_t end = memblock_end_of_DRAM() - 1;
	int ret;

	if (numa_off)
		pr_info("NUMA disabled\n"); /* Forced off on command line. */
	pr_info("Faking a node at [mem %pap-%pap]\n", &start, &end);

	ret = numa_add_memblk(0, start, end + 1);
	if (ret) {
		pr_err("NUMA init failed\n");
		return ret;
	}
	node_set(0, numa_nodes_parsed);

	numa_off = true;
	return 0;
}

#ifdef CONFIG_ACPI_NUMA
static int __init arch_acpi_numa_init(void)
{
	int ret;

	ret = acpi_numa_init();
	if (ret) {
		pr_debug("Failed to initialise from firmware\n");
		return ret;
	}

	return srat_disabled() ? -EINVAL : 0;
}
#else
static int __init arch_acpi_numa_init(void)
{
	return -EOPNOTSUPP;
}
#endif

/**
 * arch_numa_init() - Initialize NUMA
 *
 * Try each configured NUMA initialization method until one succeeds. The
 * last fallback is dummy single node config encompassing whole memory.
 */
void __init arch_numa_init(void)
{
	if (!numa_off) {
		if (!acpi_disabled && !numa_init(arch_acpi_numa_init))
			return;
		if (acpi_disabled && !numa_init(of_numa_init))
			return;
	}

	numa_init(dummy_numa_init);
}

#ifdef CONFIG_NUMA_EMU
void __init numa_emu_update_cpu_to_node(int *emu_nid_to_phys,
					unsigned int nr_emu_nids)
{
	int i, j;

	/*
	 * Transform cpu_to_node_map table to use emulated nids by
	 * reverse-mapping phys_nid.  The maps should always exist but fall
	 * back to zero just in case.
	 */
	for (i = 0; i < ARRAY_SIZE(cpu_to_node_map); i++) {
		if (cpu_to_node_map[i] == NUMA_NO_NODE)
			continue;
		for (j = 0; j < nr_emu_nids; j++)
			if (cpu_to_node_map[i] == emu_nid_to_phys[j])
				break;
		cpu_to_node_map[i] = j < nr_emu_nids ? j : 0;
	}
}

u64 __init numa_emu_dma_end(void)
{
	return memblock_start_of_DRAM() + SZ_4G;
}

void debug_cpumask_set_cpu(unsigned int cpu, int node, bool enable)
{
	struct cpumask *mask;

	if (node == NUMA_NO_NODE)
		return;

	mask = node_to_cpumask_map[node];
	if (!cpumask_available(mask)) {
		pr_err("node_to_cpumask_map[%i] NULL\n", node);
		dump_stack();
		return;
	}

	if (enable)
		cpumask_set_cpu(cpu, mask);
	else
		cpumask_clear_cpu(cpu, mask);

	pr_debug("%s cpu %d node %d: mask now %*pbl\n",
		 enable ? "numa_add_cpu" : "numa_remove_cpu",
		 cpu, node, cpumask_pr_args(mask));
}
#endif /* CONFIG_NUMA_EMU */
