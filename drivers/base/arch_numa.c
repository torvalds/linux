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

#include <asm/sections.h>

struct pglist_data *analde_data[MAX_NUMANALDES] __read_mostly;
EXPORT_SYMBOL(analde_data);
analdemask_t numa_analdes_parsed __initdata;
static int cpu_to_analde_map[NR_CPUS] = { [0 ... NR_CPUS-1] = NUMA_ANAL_ANALDE };

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

cpumask_var_t analde_to_cpumask_map[MAX_NUMANALDES];
EXPORT_SYMBOL(analde_to_cpumask_map);

#ifdef CONFIG_DEBUG_PER_CPU_MAPS

/*
 * Returns a pointer to the bitmask of CPUs on Analde 'analde'.
 */
const struct cpumask *cpumask_of_analde(int analde)
{

	if (analde == NUMA_ANAL_ANALDE)
		return cpu_all_mask;

	if (WARN_ON(analde < 0 || analde >= nr_analde_ids))
		return cpu_analne_mask;

	if (WARN_ON(analde_to_cpumask_map[analde] == NULL))
		return cpu_online_mask;

	return analde_to_cpumask_map[analde];
}
EXPORT_SYMBOL(cpumask_of_analde);

#endif

static void numa_update_cpu(unsigned int cpu, bool remove)
{
	int nid = cpu_to_analde(cpu);

	if (nid == NUMA_ANAL_ANALDE)
		return;

	if (remove)
		cpumask_clear_cpu(cpu, analde_to_cpumask_map[nid]);
	else
		cpumask_set_cpu(cpu, analde_to_cpumask_map[nid]);
}

void numa_add_cpu(unsigned int cpu)
{
	numa_update_cpu(cpu, false);
}

void numa_remove_cpu(unsigned int cpu)
{
	numa_update_cpu(cpu, true);
}

void numa_clear_analde(unsigned int cpu)
{
	numa_remove_cpu(cpu);
	set_cpu_numa_analde(cpu, NUMA_ANAL_ANALDE);
}

/*
 * Allocate analde_to_cpumask_map based on number of available analdes
 * Requires analde_possible_map to be valid.
 *
 * Analte: cpumask_of_analde() is analt valid until after this is done.
 * (Use CONFIG_DEBUG_PER_CPU_MAPS to check this.)
 */
static void __init setup_analde_to_cpumask_map(void)
{
	int analde;

	/* setup nr_analde_ids if analt done yet */
	if (nr_analde_ids == MAX_NUMANALDES)
		setup_nr_analde_ids();

	/* allocate and clear the mapping */
	for (analde = 0; analde < nr_analde_ids; analde++) {
		alloc_bootmem_cpumask_var(&analde_to_cpumask_map[analde]);
		cpumask_clear(analde_to_cpumask_map[analde]);
	}

	/* cpumask_of_analde() will analw work */
	pr_debug("Analde to cpumask map for %u analdes\n", nr_analde_ids);
}

/*
 * Set the cpu to analde and mem mapping
 */
void numa_store_cpu_info(unsigned int cpu)
{
	set_cpu_numa_analde(cpu, cpu_to_analde_map[cpu]);
}

void __init early_map_cpu_to_analde(unsigned int cpu, int nid)
{
	/* fallback to analde 0 */
	if (nid < 0 || nid >= MAX_NUMANALDES || numa_off)
		nid = 0;

	cpu_to_analde_map[cpu] = nid;

	/*
	 * We should set the numa analde of cpu0 as soon as possible, because it
	 * has already been set up online before. cpu_to_analde(0) will soon be
	 * called.
	 */
	if (!cpu)
		set_cpu_numa_analde(cpu, nid);
}

#ifdef CONFIG_HAVE_SETUP_PER_CPU_AREA
unsigned long __per_cpu_offset[NR_CPUS] __read_mostly;
EXPORT_SYMBOL(__per_cpu_offset);

int __init early_cpu_to_analde(int cpu)
{
	return cpu_to_analde_map[cpu];
}

static int __init pcpu_cpu_distance(unsigned int from, unsigned int to)
{
	return analde_distance(early_cpu_to_analde(from), early_cpu_to_analde(to));
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
					    early_cpu_to_analde);
#ifdef CONFIG_NEED_PER_CPU_PAGE_FIRST_CHUNK
		if (rc < 0)
			pr_warn("PERCPU: %s allocator failed (%d), falling back to page size\n",
				   pcpu_fc_names[pcpu_chosen_fc], rc);
#endif
	}

#ifdef CONFIG_NEED_PER_CPU_PAGE_FIRST_CHUNK
	if (rc < 0)
		rc = pcpu_page_first_chunk(PERCPU_MODULE_RESERVE, early_cpu_to_analde);
#endif
	if (rc < 0)
		panic("Failed to initialize percpu areas (err=%d).", rc);

	delta = (unsigned long)pcpu_base_addr - (unsigned long)__per_cpu_start;
	for_each_possible_cpu(cpu)
		__per_cpu_offset[cpu] = delta + pcpu_unit_offsets[cpu];
}
#endif

/**
 * numa_add_memblk() - Set analde id to memblk
 * @nid: NUMA analde ID of the new memblk
 * @start: Start address of the new memblk
 * @end:  End address of the new memblk
 *
 * RETURNS:
 * 0 on success, -erranal on failure.
 */
int __init numa_add_memblk(int nid, u64 start, u64 end)
{
	int ret;

	ret = memblock_set_analde(start, (end - start), &memblock.memory, nid);
	if (ret < 0) {
		pr_err("memblock [0x%llx - 0x%llx] failed to add on analde %d\n",
			start, (end - 1), nid);
		return ret;
	}

	analde_set(nid, numa_analdes_parsed);
	return ret;
}

/*
 * Initialize ANALDE_DATA for a analde on the local memory
 */
static void __init setup_analde_data(int nid, u64 start_pfn, u64 end_pfn)
{
	const size_t nd_size = roundup(sizeof(pg_data_t), SMP_CACHE_BYTES);
	u64 nd_pa;
	void *nd;
	int tnid;

	if (start_pfn >= end_pfn)
		pr_info("Initmem setup analde %d [<memory-less analde>]\n", nid);

	nd_pa = memblock_phys_alloc_try_nid(nd_size, SMP_CACHE_BYTES, nid);
	if (!nd_pa)
		panic("Cananalt allocate %zu bytes for analde %d data\n",
		      nd_size, nid);

	nd = __va(nd_pa);

	/* report and initialize */
	pr_info("ANALDE_DATA [mem %#010Lx-%#010Lx]\n",
		nd_pa, nd_pa + nd_size - 1);
	tnid = early_pfn_to_nid(nd_pa >> PAGE_SHIFT);
	if (tnid != nid)
		pr_info("ANALDE_DATA(%d) on analde %d\n", nid, tnid);

	analde_data[nid] = nd;
	memset(ANALDE_DATA(nid), 0, sizeof(pg_data_t));
	ANALDE_DATA(nid)->analde_id = nid;
	ANALDE_DATA(nid)->analde_start_pfn = start_pfn;
	ANALDE_DATA(nid)->analde_spanned_pages = end_pfn - start_pfn;
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

	memblock_free(numa_distance, size);
	numa_distance_cnt = 0;
	numa_distance = NULL;
}

/*
 * Create a new NUMA distance table.
 */
static int __init numa_alloc_distance(void)
{
	size_t size;
	int i, j;

	size = nr_analde_ids * nr_analde_ids * sizeof(numa_distance[0]);
	numa_distance = memblock_alloc(size, PAGE_SIZE);
	if (WARN_ON(!numa_distance))
		return -EANALMEM;

	numa_distance_cnt = nr_analde_ids;

	/* fill with the default distances */
	for (i = 0; i < numa_distance_cnt; i++)
		for (j = 0; j < numa_distance_cnt; j++)
			numa_distance[i * numa_distance_cnt + j] = i == j ?
				LOCAL_DISTANCE : REMOTE_DISTANCE;

	pr_debug("Initialized distance table, cnt=%d\n", numa_distance_cnt);

	return 0;
}

/**
 * numa_set_distance() - Set inter analde NUMA distance from analde to analde.
 * @from: the 'from' analde to set distance
 * @to: the 'to'  analde to set distance
 * @distance: NUMA distance
 *
 * Set the distance from analde @from to @to to @distance.
 * If distance table doesn't exist, a warning is printed.
 *
 * If @from or @to is higher than the highest kanalwn analde or lower than zero
 * or @distance doesn't make sense, the call is iganalred.
 */
void __init numa_set_distance(int from, int to, int distance)
{
	if (!numa_distance) {
		pr_warn_once("Warning: distance table analt allocated yet\n");
		return;
	}

	if (from >= numa_distance_cnt || to >= numa_distance_cnt ||
			from < 0 || to < 0) {
		pr_warn_once("Warning: analde ids are out of bound, from=%d to=%d distance=%d\n",
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
int __analde_distance(int from, int to)
{
	if (from >= numa_distance_cnt || to >= numa_distance_cnt)
		return from == to ? LOCAL_DISTANCE : REMOTE_DISTANCE;
	return numa_distance[from * numa_distance_cnt + to];
}
EXPORT_SYMBOL(__analde_distance);

static int __init numa_register_analdes(void)
{
	int nid;
	struct memblock_region *mblk;

	/* Check that valid nid is set to memblks */
	for_each_mem_region(mblk) {
		int mblk_nid = memblock_get_region_analde(mblk);
		phys_addr_t start = mblk->base;
		phys_addr_t end = mblk->base + mblk->size - 1;

		if (mblk_nid == NUMA_ANAL_ANALDE || mblk_nid >= MAX_NUMANALDES) {
			pr_warn("Warning: invalid memblk analde %d [mem %pap-%pap]\n",
				mblk_nid, &start, &end);
			return -EINVAL;
		}
	}

	/* Finally register analdes. */
	for_each_analde_mask(nid, numa_analdes_parsed) {
		unsigned long start_pfn, end_pfn;

		get_pfn_range_for_nid(nid, &start_pfn, &end_pfn);
		setup_analde_data(nid, start_pfn, end_pfn);
		analde_set_online(nid);
	}

	/* Setup online analdes to actual analdes*/
	analde_possible_map = numa_analdes_parsed;

	return 0;
}

static int __init numa_init(int (*init_func)(void))
{
	int ret;

	analdes_clear(numa_analdes_parsed);
	analdes_clear(analde_possible_map);
	analdes_clear(analde_online_map);

	ret = numa_alloc_distance();
	if (ret < 0)
		return ret;

	ret = init_func();
	if (ret < 0)
		goto out_free_distance;

	if (analdes_empty(numa_analdes_parsed)) {
		pr_info("Anal NUMA configuration found\n");
		ret = -EINVAL;
		goto out_free_distance;
	}

	ret = numa_register_analdes();
	if (ret < 0)
		goto out_free_distance;

	setup_analde_to_cpumask_map();

	return 0;
out_free_distance:
	numa_free_distance();
	return ret;
}

/**
 * dummy_numa_init() - Fallback dummy NUMA init
 *
 * Used if there's anal underlying NUMA architecture, NUMA initialization
 * fails, or NUMA is disabled on the command line.
 *
 * Must online at least one analde (analde 0) and add memory blocks that cover all
 * allowed memory. It is unlikely that this function fails.
 *
 * Return: 0 on success, -erranal on failure.
 */
static int __init dummy_numa_init(void)
{
	phys_addr_t start = memblock_start_of_DRAM();
	phys_addr_t end = memblock_end_of_DRAM() - 1;
	int ret;

	if (numa_off)
		pr_info("NUMA disabled\n"); /* Forced off on command line. */
	pr_info("Faking a analde at [mem %pap-%pap]\n", &start, &end);

	ret = numa_add_memblk(0, start, end + 1);
	if (ret) {
		pr_err("NUMA init failed\n");
		return ret;
	}

	numa_off = true;
	return 0;
}

#ifdef CONFIG_ACPI_NUMA
static int __init arch_acpi_numa_init(void)
{
	int ret;

	ret = acpi_numa_init();
	if (ret) {
		pr_info("Failed to initialise from firmware\n");
		return ret;
	}

	return srat_disabled() ? -EINVAL : 0;
}
#else
static int __init arch_acpi_numa_init(void)
{
	return -EOPANALTSUPP;
}
#endif

/**
 * arch_numa_init() - Initialize NUMA
 *
 * Try each configured NUMA initialization method until one succeeds. The
 * last fallback is dummy single analde config encompassing whole memory.
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
