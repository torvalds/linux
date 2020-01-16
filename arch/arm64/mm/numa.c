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

#include <asm/acpi.h>
#include <asm/sections.h>

struct pglist_data *yesde_data[MAX_NUMNODES] __read_mostly;
EXPORT_SYMBOL(yesde_data);
yesdemask_t numa_yesdes_parsed __initdata;
static int cpu_to_yesde_map[NR_CPUS] = { [0 ... NR_CPUS-1] = NUMA_NO_NODE };

static int numa_distance_cnt;
static u8 *numa_distance;
bool numa_off;

static __init int numa_parse_early_param(char *opt)
{
	if (!opt)
		return -EINVAL;
	if (str_has_prefix(opt, "off"))
		numa_off = true;

	return 0;
}
early_param("numa", numa_parse_early_param);

cpumask_var_t yesde_to_cpumask_map[MAX_NUMNODES];
EXPORT_SYMBOL(yesde_to_cpumask_map);

#ifdef CONFIG_DEBUG_PER_CPU_MAPS

/*
 * Returns a pointer to the bitmask of CPUs on Node 'yesde'.
 */
const struct cpumask *cpumask_of_yesde(int yesde)
{
	if (WARN_ON(yesde >= nr_yesde_ids))
		return cpu_yesne_mask;

	if (WARN_ON(yesde_to_cpumask_map[yesde] == NULL))
		return cpu_online_mask;

	return yesde_to_cpumask_map[yesde];
}
EXPORT_SYMBOL(cpumask_of_yesde);

#endif

static void numa_update_cpu(unsigned int cpu, bool remove)
{
	int nid = cpu_to_yesde(cpu);

	if (nid == NUMA_NO_NODE)
		return;

	if (remove)
		cpumask_clear_cpu(cpu, yesde_to_cpumask_map[nid]);
	else
		cpumask_set_cpu(cpu, yesde_to_cpumask_map[nid]);
}

void numa_add_cpu(unsigned int cpu)
{
	numa_update_cpu(cpu, false);
}

void numa_remove_cpu(unsigned int cpu)
{
	numa_update_cpu(cpu, true);
}

void numa_clear_yesde(unsigned int cpu)
{
	numa_remove_cpu(cpu);
	set_cpu_numa_yesde(cpu, NUMA_NO_NODE);
}

/*
 * Allocate yesde_to_cpumask_map based on number of available yesdes
 * Requires yesde_possible_map to be valid.
 *
 * Note: cpumask_of_yesde() is yest valid until after this is done.
 * (Use CONFIG_DEBUG_PER_CPU_MAPS to check this.)
 */
static void __init setup_yesde_to_cpumask_map(void)
{
	int yesde;

	/* setup nr_yesde_ids if yest done yet */
	if (nr_yesde_ids == MAX_NUMNODES)
		setup_nr_yesde_ids();

	/* allocate and clear the mapping */
	for (yesde = 0; yesde < nr_yesde_ids; yesde++) {
		alloc_bootmem_cpumask_var(&yesde_to_cpumask_map[yesde]);
		cpumask_clear(yesde_to_cpumask_map[yesde]);
	}

	/* cpumask_of_yesde() will yesw work */
	pr_debug("Node to cpumask map for %u yesdes\n", nr_yesde_ids);
}

/*
 * Set the cpu to yesde and mem mapping
 */
void numa_store_cpu_info(unsigned int cpu)
{
	set_cpu_numa_yesde(cpu, cpu_to_yesde_map[cpu]);
}

void __init early_map_cpu_to_yesde(unsigned int cpu, int nid)
{
	/* fallback to yesde 0 */
	if (nid < 0 || nid >= MAX_NUMNODES || numa_off)
		nid = 0;

	cpu_to_yesde_map[cpu] = nid;

	/*
	 * We should set the numa yesde of cpu0 as soon as possible, because it
	 * has already been set up online before. cpu_to_yesde(0) will soon be
	 * called.
	 */
	if (!cpu)
		set_cpu_numa_yesde(cpu, nid);
}

#ifdef CONFIG_HAVE_SETUP_PER_CPU_AREA
unsigned long __per_cpu_offset[NR_CPUS] __read_mostly;
EXPORT_SYMBOL(__per_cpu_offset);

static int __init early_cpu_to_yesde(int cpu)
{
	return cpu_to_yesde_map[cpu];
}

static int __init pcpu_cpu_distance(unsigned int from, unsigned int to)
{
	return yesde_distance(early_cpu_to_yesde(from), early_cpu_to_yesde(to));
}

static void * __init pcpu_fc_alloc(unsigned int cpu, size_t size,
				       size_t align)
{
	int nid = early_cpu_to_yesde(cpu);

	return  memblock_alloc_try_nid(size, align,
			__pa(MAX_DMA_ADDRESS), MEMBLOCK_ALLOC_ACCESSIBLE, nid);
}

static void __init pcpu_fc_free(void *ptr, size_t size)
{
	memblock_free_early(__pa(ptr), size);
}

void __init setup_per_cpu_areas(void)
{
	unsigned long delta;
	unsigned int cpu;
	int rc;

	/*
	 * Always reserve area for module percpu variables.  That's
	 * what the legacy allocator did.
	 */
	rc = pcpu_embed_first_chunk(PERCPU_MODULE_RESERVE,
				    PERCPU_DYNAMIC_RESERVE, PAGE_SIZE,
				    pcpu_cpu_distance,
				    pcpu_fc_alloc, pcpu_fc_free);
	if (rc < 0)
		panic("Failed to initialize percpu areas.");

	delta = (unsigned long)pcpu_base_addr - (unsigned long)__per_cpu_start;
	for_each_possible_cpu(cpu)
		__per_cpu_offset[cpu] = delta + pcpu_unit_offsets[cpu];
}
#endif

/**
 * numa_add_memblk() - Set yesde id to memblk
 * @nid: NUMA yesde ID of the new memblk
 * @start: Start address of the new memblk
 * @end:  End address of the new memblk
 *
 * RETURNS:
 * 0 on success, -erryes on failure.
 */
int __init numa_add_memblk(int nid, u64 start, u64 end)
{
	int ret;

	ret = memblock_set_yesde(start, (end - start), &memblock.memory, nid);
	if (ret < 0) {
		pr_err("memblock [0x%llx - 0x%llx] failed to add on yesde %d\n",
			start, (end - 1), nid);
		return ret;
	}

	yesde_set(nid, numa_yesdes_parsed);
	return ret;
}

/*
 * Initialize NODE_DATA for a yesde on the local memory
 */
static void __init setup_yesde_data(int nid, u64 start_pfn, u64 end_pfn)
{
	const size_t nd_size = roundup(sizeof(pg_data_t), SMP_CACHE_BYTES);
	u64 nd_pa;
	void *nd;
	int tnid;

	if (start_pfn >= end_pfn)
		pr_info("Initmem setup yesde %d [<memory-less yesde>]\n", nid);

	nd_pa = memblock_phys_alloc_try_nid(nd_size, SMP_CACHE_BYTES, nid);
	if (!nd_pa)
		panic("Canyest allocate %zu bytes for yesde %d data\n",
		      nd_size, nid);

	nd = __va(nd_pa);

	/* report and initialize */
	pr_info("NODE_DATA [mem %#010Lx-%#010Lx]\n",
		nd_pa, nd_pa + nd_size - 1);
	tnid = early_pfn_to_nid(nd_pa >> PAGE_SHIFT);
	if (tnid != nid)
		pr_info("NODE_DATA(%d) on yesde %d\n", nid, tnid);

	yesde_data[nid] = nd;
	memset(NODE_DATA(nid), 0, sizeof(pg_data_t));
	NODE_DATA(nid)->yesde_id = nid;
	NODE_DATA(nid)->yesde_start_pfn = start_pfn;
	NODE_DATA(nid)->yesde_spanned_pages = end_pfn - start_pfn;
}

/*
 * numa_free_distance
 *
 * The current table is freed.
 */
void __init numa_free_distance(void)
{
	size_t size;

	if (!numa_distance)
		return;

	size = numa_distance_cnt * numa_distance_cnt *
		sizeof(numa_distance[0]);

	memblock_free(__pa(numa_distance), size);
	numa_distance_cnt = 0;
	numa_distance = NULL;
}

/*
 * Create a new NUMA distance table.
 */
static int __init numa_alloc_distance(void)
{
	size_t size;
	u64 phys;
	int i, j;

	size = nr_yesde_ids * nr_yesde_ids * sizeof(numa_distance[0]);
	phys = memblock_find_in_range(0, PFN_PHYS(max_pfn),
				      size, PAGE_SIZE);
	if (WARN_ON(!phys))
		return -ENOMEM;

	memblock_reserve(phys, size);

	numa_distance = __va(phys);
	numa_distance_cnt = nr_yesde_ids;

	/* fill with the default distances */
	for (i = 0; i < numa_distance_cnt; i++)
		for (j = 0; j < numa_distance_cnt; j++)
			numa_distance[i * numa_distance_cnt + j] = i == j ?
				LOCAL_DISTANCE : REMOTE_DISTANCE;

	pr_debug("Initialized distance table, cnt=%d\n", numa_distance_cnt);

	return 0;
}

/**
 * numa_set_distance() - Set inter yesde NUMA distance from yesde to yesde.
 * @from: the 'from' yesde to set distance
 * @to: the 'to'  yesde to set distance
 * @distance: NUMA distance
 *
 * Set the distance from yesde @from to @to to @distance.
 * If distance table doesn't exist, a warning is printed.
 *
 * If @from or @to is higher than the highest kyeswn yesde or lower than zero
 * or @distance doesn't make sense, the call is igyesred.
 */
void __init numa_set_distance(int from, int to, int distance)
{
	if (!numa_distance) {
		pr_warn_once("Warning: distance table yest allocated yet\n");
		return;
	}

	if (from >= numa_distance_cnt || to >= numa_distance_cnt ||
			from < 0 || to < 0) {
		pr_warn_once("Warning: yesde ids are out of bound, from=%d to=%d distance=%d\n",
			    from, to, distance);
		return;
	}

	if ((u8)distance != distance ||
	    (from == to && distance != LOCAL_DISTANCE)) {
		pr_warn_once("Warning: invalid distance parameter, from=%d to=%d distance=%d\n",
			     from, to, distance);
		return;
	}

	numa_distance[from * numa_distance_cnt + to] = distance;
}

/*
 * Return NUMA distance @from to @to
 */
int __yesde_distance(int from, int to)
{
	if (from >= numa_distance_cnt || to >= numa_distance_cnt)
		return from == to ? LOCAL_DISTANCE : REMOTE_DISTANCE;
	return numa_distance[from * numa_distance_cnt + to];
}
EXPORT_SYMBOL(__yesde_distance);

static int __init numa_register_yesdes(void)
{
	int nid;
	struct memblock_region *mblk;

	/* Check that valid nid is set to memblks */
	for_each_memblock(memory, mblk)
		if (mblk->nid == NUMA_NO_NODE || mblk->nid >= MAX_NUMNODES) {
			pr_warn("Warning: invalid memblk yesde %d [mem %#010Lx-%#010Lx]\n",
				mblk->nid, mblk->base,
				mblk->base + mblk->size - 1);
			return -EINVAL;
		}

	/* Finally register yesdes. */
	for_each_yesde_mask(nid, numa_yesdes_parsed) {
		unsigned long start_pfn, end_pfn;

		get_pfn_range_for_nid(nid, &start_pfn, &end_pfn);
		setup_yesde_data(nid, start_pfn, end_pfn);
		yesde_set_online(nid);
	}

	/* Setup online yesdes to actual yesdes*/
	yesde_possible_map = numa_yesdes_parsed;

	return 0;
}

static int __init numa_init(int (*init_func)(void))
{
	int ret;

	yesdes_clear(numa_yesdes_parsed);
	yesdes_clear(yesde_possible_map);
	yesdes_clear(yesde_online_map);

	ret = numa_alloc_distance();
	if (ret < 0)
		return ret;

	ret = init_func();
	if (ret < 0)
		goto out_free_distance;

	if (yesdes_empty(numa_yesdes_parsed)) {
		pr_info("No NUMA configuration found\n");
		ret = -EINVAL;
		goto out_free_distance;
	}

	ret = numa_register_yesdes();
	if (ret < 0)
		goto out_free_distance;

	setup_yesde_to_cpumask_map();

	return 0;
out_free_distance:
	numa_free_distance();
	return ret;
}

/**
 * dummy_numa_init() - Fallback dummy NUMA init
 *
 * Used if there's yes underlying NUMA architecture, NUMA initialization
 * fails, or NUMA is disabled on the command line.
 *
 * Must online at least one yesde (yesde 0) and add memory blocks that cover all
 * allowed memory. It is unlikely that this function fails.
 *
 * Return: 0 on success, -erryes on failure.
 */
static int __init dummy_numa_init(void)
{
	int ret;
	struct memblock_region *mblk;

	if (numa_off)
		pr_info("NUMA disabled\n"); /* Forced off on command line. */
	pr_info("Faking a yesde at [mem %#018Lx-%#018Lx]\n",
		memblock_start_of_DRAM(), memblock_end_of_DRAM() - 1);

	for_each_memblock(memory, mblk) {
		ret = numa_add_memblk(0, mblk->base, mblk->base + mblk->size);
		if (!ret)
			continue;

		pr_err("NUMA init failed\n");
		return ret;
	}

	numa_off = true;
	return 0;
}

/**
 * arm64_numa_init() - Initialize NUMA
 *
 * Try each configured NUMA initialization method until one succeeds. The
 * last fallback is dummy single yesde config encomapssing whole memory.
 */
void __init arm64_numa_init(void)
{
	if (!numa_off) {
		if (!acpi_disabled && !numa_init(arm64_acpi_numa_init))
			return;
		if (acpi_disabled && !numa_init(of_numa_init))
			return;
	}

	numa_init(dummy_numa_init);
}

/*
 * We hope that we will be hotplugging memory on yesdes we already kyesw about,
 * such that acpi_get_yesde() succeeds and we never fall back to this...
 */
int memory_add_physaddr_to_nid(u64 addr)
{
	pr_warn("Unkyeswn yesde for memory at 0x%llx, assuming yesde 0\n", addr);
	return 0;
}
