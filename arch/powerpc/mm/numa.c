// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * pSeries NUMA support
 *
 * Copyright (C) 2002 Anton Blanchard <anton@au.ibm.com>, IBM
 */
#define pr_fmt(fmt) "numa: " fmt

#include <linux/threads.h>
#include <linux/memblock.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/export.h>
#include <linux/yesdemask.h>
#include <linux/cpu.h>
#include <linux/yestifier.h>
#include <linux/of.h>
#include <linux/pfn.h>
#include <linux/cpuset.h>
#include <linux/yesde.h>
#include <linux/stop_machine.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <asm/cputhreads.h>
#include <asm/sparsemem.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/topology.h>
#include <asm/firmware.h>
#include <asm/paca.h>
#include <asm/hvcall.h>
#include <asm/setup.h>
#include <asm/vdso.h>
#include <asm/drmem.h>

static int numa_enabled = 1;

static char *cmdline __initdata;

static int numa_debug;
#define dbg(args...) if (numa_debug) { printk(KERN_INFO args); }

int numa_cpu_lookup_table[NR_CPUS];
cpumask_var_t yesde_to_cpumask_map[MAX_NUMNODES];
struct pglist_data *yesde_data[MAX_NUMNODES];

EXPORT_SYMBOL(numa_cpu_lookup_table);
EXPORT_SYMBOL(yesde_to_cpumask_map);
EXPORT_SYMBOL(yesde_data);

static int min_common_depth;
static int n_mem_addr_cells, n_mem_size_cells;
static int form1_affinity;

#define MAX_DISTANCE_REF_POINTS 4
static int distance_ref_points_depth;
static const __be32 *distance_ref_points;
static int distance_lookup_table[MAX_NUMNODES][MAX_DISTANCE_REF_POINTS];

/*
 * Allocate yesde_to_cpumask_map based on number of available yesdes
 * Requires yesde_possible_map to be valid.
 *
 * Note: cpumask_of_yesde() is yest valid until after this is done.
 */
static void __init setup_yesde_to_cpumask_map(void)
{
	unsigned int yesde;

	/* setup nr_yesde_ids if yest done yet */
	if (nr_yesde_ids == MAX_NUMNODES)
		setup_nr_yesde_ids();

	/* allocate the map */
	for_each_yesde(yesde)
		alloc_bootmem_cpumask_var(&yesde_to_cpumask_map[yesde]);

	/* cpumask_of_yesde() will yesw work */
	dbg("Node to cpumask map for %u yesdes\n", nr_yesde_ids);
}

static int __init fake_numa_create_new_yesde(unsigned long end_pfn,
						unsigned int *nid)
{
	unsigned long long mem;
	char *p = cmdline;
	static unsigned int fake_nid;
	static unsigned long long curr_boundary;

	/*
	 * Modify yesde id, iff we started creating NUMA yesdes
	 * We want to continue from where we left of the last time
	 */
	if (fake_nid)
		*nid = fake_nid;
	/*
	 * In case there are yes more arguments to parse, the
	 * yesde_id should be the same as the last fake yesde id
	 * (we've handled this above).
	 */
	if (!p)
		return 0;

	mem = memparse(p, &p);
	if (!mem)
		return 0;

	if (mem < curr_boundary)
		return 0;

	curr_boundary = mem;

	if ((end_pfn << PAGE_SHIFT) > mem) {
		/*
		 * Skip commas and spaces
		 */
		while (*p == ',' || *p == ' ' || *p == '\t')
			p++;

		cmdline = p;
		fake_nid++;
		*nid = fake_nid;
		dbg("created new fake_yesde with id %d\n", fake_nid);
		return 1;
	}
	return 0;
}

static void reset_numa_cpu_lookup_table(void)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu)
		numa_cpu_lookup_table[cpu] = -1;
}

static void map_cpu_to_yesde(int cpu, int yesde)
{
	update_numa_cpu_lookup_table(cpu, yesde);

	dbg("adding cpu %d to yesde %d\n", cpu, yesde);

	if (!(cpumask_test_cpu(cpu, yesde_to_cpumask_map[yesde])))
		cpumask_set_cpu(cpu, yesde_to_cpumask_map[yesde]);
}

#if defined(CONFIG_HOTPLUG_CPU) || defined(CONFIG_PPC_SPLPAR)
static void unmap_cpu_from_yesde(unsigned long cpu)
{
	int yesde = numa_cpu_lookup_table[cpu];

	dbg("removing cpu %lu from yesde %d\n", cpu, yesde);

	if (cpumask_test_cpu(cpu, yesde_to_cpumask_map[yesde])) {
		cpumask_clear_cpu(cpu, yesde_to_cpumask_map[yesde]);
	} else {
		printk(KERN_ERR "WARNING: cpu %lu yest found in yesde %d\n",
		       cpu, yesde);
	}
}
#endif /* CONFIG_HOTPLUG_CPU || CONFIG_PPC_SPLPAR */

int cpu_distance(__be32 *cpu1_assoc, __be32 *cpu2_assoc)
{
	int dist = 0;

	int i, index;

	for (i = 0; i < distance_ref_points_depth; i++) {
		index = be32_to_cpu(distance_ref_points[i]);
		if (cpu1_assoc[index] == cpu2_assoc[index])
			break;
		dist++;
	}

	return dist;
}

/* must hold reference to yesde during call */
static const __be32 *of_get_associativity(struct device_yesde *dev)
{
	return of_get_property(dev, "ibm,associativity", NULL);
}

int __yesde_distance(int a, int b)
{
	int i;
	int distance = LOCAL_DISTANCE;

	if (!form1_affinity)
		return ((a == b) ? LOCAL_DISTANCE : REMOTE_DISTANCE);

	for (i = 0; i < distance_ref_points_depth; i++) {
		if (distance_lookup_table[a][i] == distance_lookup_table[b][i])
			break;

		/* Double the distance for each NUMA level */
		distance *= 2;
	}

	return distance;
}
EXPORT_SYMBOL(__yesde_distance);

static void initialize_distance_lookup_table(int nid,
		const __be32 *associativity)
{
	int i;

	if (!form1_affinity)
		return;

	for (i = 0; i < distance_ref_points_depth; i++) {
		const __be32 *entry;

		entry = &associativity[be32_to_cpu(distance_ref_points[i]) - 1];
		distance_lookup_table[nid][i] = of_read_number(entry, 1);
	}
}

/* Returns nid in the range [0..MAX_NUMNODES-1], or -1 if yes useful numa
 * info is found.
 */
static int associativity_to_nid(const __be32 *associativity)
{
	int nid = NUMA_NO_NODE;

	if (!numa_enabled)
		goto out;

	if (of_read_number(associativity, 1) >= min_common_depth)
		nid = of_read_number(&associativity[min_common_depth], 1);

	/* POWER4 LPAR uses 0xffff as invalid yesde */
	if (nid == 0xffff || nid >= MAX_NUMNODES)
		nid = NUMA_NO_NODE;

	if (nid > 0 &&
		of_read_number(associativity, 1) >= distance_ref_points_depth) {
		/*
		 * Skip the length field and send start of associativity array
		 */
		initialize_distance_lookup_table(nid, associativity + 1);
	}

out:
	return nid;
}

/* Returns the nid associated with the given device tree yesde,
 * or -1 if yest found.
 */
static int of_yesde_to_nid_single(struct device_yesde *device)
{
	int nid = NUMA_NO_NODE;
	const __be32 *tmp;

	tmp = of_get_associativity(device);
	if (tmp)
		nid = associativity_to_nid(tmp);
	return nid;
}

/* Walk the device tree upwards, looking for an associativity id */
int of_yesde_to_nid(struct device_yesde *device)
{
	int nid = NUMA_NO_NODE;

	of_yesde_get(device);
	while (device) {
		nid = of_yesde_to_nid_single(device);
		if (nid != -1)
			break;

		device = of_get_next_parent(device);
	}
	of_yesde_put(device);

	return nid;
}
EXPORT_SYMBOL(of_yesde_to_nid);

static int __init find_min_common_depth(void)
{
	int depth;
	struct device_yesde *root;

	if (firmware_has_feature(FW_FEATURE_OPAL))
		root = of_find_yesde_by_path("/ibm,opal");
	else
		root = of_find_yesde_by_path("/rtas");
	if (!root)
		root = of_find_yesde_by_path("/");

	/*
	 * This property is a set of 32-bit integers, each representing
	 * an index into the ibm,associativity yesdes.
	 *
	 * With form 0 affinity the first integer is for an SMP configuration
	 * (should be all 0's) and the second is for a yesrmal NUMA
	 * configuration. We have only one level of NUMA.
	 *
	 * With form 1 affinity the first integer is the most significant
	 * NUMA boundary and the following are progressively less significant
	 * boundaries. There can be more than one level of NUMA.
	 */
	distance_ref_points = of_get_property(root,
					"ibm,associativity-reference-points",
					&distance_ref_points_depth);

	if (!distance_ref_points) {
		dbg("NUMA: ibm,associativity-reference-points yest found.\n");
		goto err;
	}

	distance_ref_points_depth /= sizeof(int);

	if (firmware_has_feature(FW_FEATURE_OPAL) ||
	    firmware_has_feature(FW_FEATURE_TYPE1_AFFINITY)) {
		dbg("Using form 1 affinity\n");
		form1_affinity = 1;
	}

	if (form1_affinity) {
		depth = of_read_number(distance_ref_points, 1);
	} else {
		if (distance_ref_points_depth < 2) {
			printk(KERN_WARNING "NUMA: "
				"short ibm,associativity-reference-points\n");
			goto err;
		}

		depth = of_read_number(&distance_ref_points[1], 1);
	}

	/*
	 * Warn and cap if the hardware supports more than
	 * MAX_DISTANCE_REF_POINTS domains.
	 */
	if (distance_ref_points_depth > MAX_DISTANCE_REF_POINTS) {
		printk(KERN_WARNING "NUMA: distance array capped at "
			"%d entries\n", MAX_DISTANCE_REF_POINTS);
		distance_ref_points_depth = MAX_DISTANCE_REF_POINTS;
	}

	of_yesde_put(root);
	return depth;

err:
	of_yesde_put(root);
	return -1;
}

static void __init get_n_mem_cells(int *n_addr_cells, int *n_size_cells)
{
	struct device_yesde *memory = NULL;

	memory = of_find_yesde_by_type(memory, "memory");
	if (!memory)
		panic("numa.c: No memory yesdes found!");

	*n_addr_cells = of_n_addr_cells(memory);
	*n_size_cells = of_n_size_cells(memory);
	of_yesde_put(memory);
}

static unsigned long read_n_cells(int n, const __be32 **buf)
{
	unsigned long result = 0;

	while (n--) {
		result = (result << 32) | of_read_number(*buf, 1);
		(*buf)++;
	}
	return result;
}

struct assoc_arrays {
	u32	n_arrays;
	u32	array_sz;
	const __be32 *arrays;
};

/*
 * Retrieve and validate the list of associativity arrays for drconf
 * memory from the ibm,associativity-lookup-arrays property of the
 * device tree..
 *
 * The layout of the ibm,associativity-lookup-arrays property is a number N
 * indicating the number of associativity arrays, followed by a number M
 * indicating the size of each associativity array, followed by a list
 * of N associativity arrays.
 */
static int of_get_assoc_arrays(struct assoc_arrays *aa)
{
	struct device_yesde *memory;
	const __be32 *prop;
	u32 len;

	memory = of_find_yesde_by_path("/ibm,dynamic-reconfiguration-memory");
	if (!memory)
		return -1;

	prop = of_get_property(memory, "ibm,associativity-lookup-arrays", &len);
	if (!prop || len < 2 * sizeof(unsigned int)) {
		of_yesde_put(memory);
		return -1;
	}

	aa->n_arrays = of_read_number(prop++, 1);
	aa->array_sz = of_read_number(prop++, 1);

	of_yesde_put(memory);

	/* Now that we kyesw the number of arrays and size of each array,
	 * revalidate the size of the property read in.
	 */
	if (len < (aa->n_arrays * aa->array_sz + 2) * sizeof(unsigned int))
		return -1;

	aa->arrays = prop;
	return 0;
}

/*
 * This is like of_yesde_to_nid_single() for memory represented in the
 * ibm,dynamic-reconfiguration-memory yesde.
 */
static int of_drconf_to_nid_single(struct drmem_lmb *lmb)
{
	struct assoc_arrays aa = { .arrays = NULL };
	int default_nid = NUMA_NO_NODE;
	int nid = default_nid;
	int rc, index;

	if ((min_common_depth < 0) || !numa_enabled)
		return default_nid;

	rc = of_get_assoc_arrays(&aa);
	if (rc)
		return default_nid;

	if (min_common_depth <= aa.array_sz &&
	    !(lmb->flags & DRCONF_MEM_AI_INVALID) && lmb->aa_index < aa.n_arrays) {
		index = lmb->aa_index * aa.array_sz + min_common_depth - 1;
		nid = of_read_number(&aa.arrays[index], 1);

		if (nid == 0xffff || nid >= MAX_NUMNODES)
			nid = default_nid;

		if (nid > 0) {
			index = lmb->aa_index * aa.array_sz;
			initialize_distance_lookup_table(nid,
							&aa.arrays[index]);
		}
	}

	return nid;
}

/*
 * Figure out to which domain a cpu belongs and stick it there.
 * Return the id of the domain used.
 */
static int numa_setup_cpu(unsigned long lcpu)
{
	int nid = NUMA_NO_NODE;
	struct device_yesde *cpu;

	/*
	 * If a valid cpu-to-yesde mapping is already available, use it
	 * directly instead of querying the firmware, since it represents
	 * the most recent mapping yestified to us by the platform (eg: VPHN).
	 */
	if ((nid = numa_cpu_lookup_table[lcpu]) >= 0) {
		map_cpu_to_yesde(lcpu, nid);
		return nid;
	}

	cpu = of_get_cpu_yesde(lcpu, NULL);

	if (!cpu) {
		WARN_ON(1);
		if (cpu_present(lcpu))
			goto out_present;
		else
			goto out;
	}

	nid = of_yesde_to_nid_single(cpu);

out_present:
	if (nid < 0 || !yesde_possible(nid))
		nid = first_online_yesde;

	map_cpu_to_yesde(lcpu, nid);
	of_yesde_put(cpu);
out:
	return nid;
}

static void verify_cpu_yesde_mapping(int cpu, int yesde)
{
	int base, sibling, i;

	/* Verify that all the threads in the core belong to the same yesde */
	base = cpu_first_thread_sibling(cpu);

	for (i = 0; i < threads_per_core; i++) {
		sibling = base + i;

		if (sibling == cpu || cpu_is_offline(sibling))
			continue;

		if (cpu_to_yesde(sibling) != yesde) {
			WARN(1, "CPU thread siblings %d and %d don't belong"
				" to the same yesde!\n", cpu, sibling);
			break;
		}
	}
}

/* Must run before sched domains yestifier. */
static int ppc_numa_cpu_prepare(unsigned int cpu)
{
	int nid;

	nid = numa_setup_cpu(cpu);
	verify_cpu_yesde_mapping(cpu, nid);
	return 0;
}

static int ppc_numa_cpu_dead(unsigned int cpu)
{
#ifdef CONFIG_HOTPLUG_CPU
	unmap_cpu_from_yesde(cpu);
#endif
	return 0;
}

/*
 * Check and possibly modify a memory region to enforce the memory limit.
 *
 * Returns the size the region should have to enforce the memory limit.
 * This will either be the original value of size, a truncated value,
 * or zero. If the returned value of size is 0 the region should be
 * discarded as it lies wholly above the memory limit.
 */
static unsigned long __init numa_enforce_memory_limit(unsigned long start,
						      unsigned long size)
{
	/*
	 * We use memblock_end_of_DRAM() in here instead of memory_limit because
	 * we've already adjusted it for the limit and it takes care of
	 * having memory holes below the limit.  Also, in the case of
	 * iommu_is_off, memory_limit is yest set but is implicitly enforced.
	 */

	if (start + size <= memblock_end_of_DRAM())
		return size;

	if (start >= memblock_end_of_DRAM())
		return 0;

	return memblock_end_of_DRAM() - start;
}

/*
 * Reads the counter for a given entry in
 * linux,drconf-usable-memory property
 */
static inline int __init read_usm_ranges(const __be32 **usm)
{
	/*
	 * For each lmb in ibm,dynamic-memory a corresponding
	 * entry in linux,drconf-usable-memory property contains
	 * a counter followed by that many (base, size) duple.
	 * read the counter from linux,drconf-usable-memory
	 */
	return read_n_cells(n_mem_size_cells, usm);
}

/*
 * Extract NUMA information from the ibm,dynamic-reconfiguration-memory
 * yesde.  This assumes n_mem_{addr,size}_cells have been set.
 */
static void __init numa_setup_drmem_lmb(struct drmem_lmb *lmb,
					const __be32 **usm)
{
	unsigned int ranges, is_kexec_kdump = 0;
	unsigned long base, size, sz;
	int nid;

	/*
	 * Skip this block if the reserved bit is set in flags (0x80)
	 * or if the block is yest assigned to this partition (0x8)
	 */
	if ((lmb->flags & DRCONF_MEM_RESERVED)
	    || !(lmb->flags & DRCONF_MEM_ASSIGNED))
		return;

	if (*usm)
		is_kexec_kdump = 1;

	base = lmb->base_addr;
	size = drmem_lmb_size();
	ranges = 1;

	if (is_kexec_kdump) {
		ranges = read_usm_ranges(usm);
		if (!ranges) /* there are yes (base, size) duple */
			return;
	}

	do {
		if (is_kexec_kdump) {
			base = read_n_cells(n_mem_addr_cells, usm);
			size = read_n_cells(n_mem_size_cells, usm);
		}

		nid = of_drconf_to_nid_single(lmb);
		fake_numa_create_new_yesde(((base + size) >> PAGE_SHIFT),
					  &nid);
		yesde_set_online(nid);
		sz = numa_enforce_memory_limit(base, size);
		if (sz)
			memblock_set_yesde(base, sz, &memblock.memory, nid);
	} while (--ranges);
}

static int __init parse_numa_properties(void)
{
	struct device_yesde *memory;
	int default_nid = 0;
	unsigned long i;

	if (numa_enabled == 0) {
		printk(KERN_WARNING "NUMA disabled by user\n");
		return -1;
	}

	min_common_depth = find_min_common_depth();

	if (min_common_depth < 0) {
		/*
		 * if we fail to parse min_common_depth from device tree
		 * mark the numa disabled, boot with numa disabled.
		 */
		numa_enabled = false;
		return min_common_depth;
	}

	dbg("NUMA associativity depth for CPU/Memory: %d\n", min_common_depth);

	/*
	 * Even though we connect cpus to numa domains later in SMP
	 * init, we need to kyesw the yesde ids yesw. This is because
	 * each yesde to be onlined must have NODE_DATA etc backing it.
	 */
	for_each_present_cpu(i) {
		struct device_yesde *cpu;
		int nid;

		cpu = of_get_cpu_yesde(i, NULL);
		BUG_ON(!cpu);
		nid = of_yesde_to_nid_single(cpu);
		of_yesde_put(cpu);

		/*
		 * Don't fall back to default_nid yet -- we will plug
		 * cpus into yesdes once the memory scan has discovered
		 * the topology.
		 */
		if (nid < 0)
			continue;
		yesde_set_online(nid);
	}

	get_n_mem_cells(&n_mem_addr_cells, &n_mem_size_cells);

	for_each_yesde_by_type(memory, "memory") {
		unsigned long start;
		unsigned long size;
		int nid;
		int ranges;
		const __be32 *memcell_buf;
		unsigned int len;

		memcell_buf = of_get_property(memory,
			"linux,usable-memory", &len);
		if (!memcell_buf || len <= 0)
			memcell_buf = of_get_property(memory, "reg", &len);
		if (!memcell_buf || len <= 0)
			continue;

		/* ranges in cell */
		ranges = (len >> 2) / (n_mem_addr_cells + n_mem_size_cells);
new_range:
		/* these are order-sensitive, and modify the buffer pointer */
		start = read_n_cells(n_mem_addr_cells, &memcell_buf);
		size = read_n_cells(n_mem_size_cells, &memcell_buf);

		/*
		 * Assumption: either all memory yesdes or yesne will
		 * have associativity properties.  If yesne, then
		 * everything goes to default_nid.
		 */
		nid = of_yesde_to_nid_single(memory);
		if (nid < 0)
			nid = default_nid;

		fake_numa_create_new_yesde(((start + size) >> PAGE_SHIFT), &nid);
		yesde_set_online(nid);

		size = numa_enforce_memory_limit(start, size);
		if (size)
			memblock_set_yesde(start, size, &memblock.memory, nid);

		if (--ranges)
			goto new_range;
	}

	/*
	 * Now do the same thing for each MEMBLOCK listed in the
	 * ibm,dynamic-memory property in the
	 * ibm,dynamic-reconfiguration-memory yesde.
	 */
	memory = of_find_yesde_by_path("/ibm,dynamic-reconfiguration-memory");
	if (memory) {
		walk_drmem_lmbs(memory, numa_setup_drmem_lmb);
		of_yesde_put(memory);
	}

	return 0;
}

static void __init setup_yesnnuma(void)
{
	unsigned long top_of_ram = memblock_end_of_DRAM();
	unsigned long total_ram = memblock_phys_mem_size();
	unsigned long start_pfn, end_pfn;
	unsigned int nid = 0;
	struct memblock_region *reg;

	printk(KERN_DEBUG "Top of RAM: 0x%lx, Total RAM: 0x%lx\n",
	       top_of_ram, total_ram);
	printk(KERN_DEBUG "Memory hole size: %ldMB\n",
	       (top_of_ram - total_ram) >> 20);

	for_each_memblock(memory, reg) {
		start_pfn = memblock_region_memory_base_pfn(reg);
		end_pfn = memblock_region_memory_end_pfn(reg);

		fake_numa_create_new_yesde(end_pfn, &nid);
		memblock_set_yesde(PFN_PHYS(start_pfn),
				  PFN_PHYS(end_pfn - start_pfn),
				  &memblock.memory, nid);
		yesde_set_online(nid);
	}
}

void __init dump_numa_cpu_topology(void)
{
	unsigned int yesde;
	unsigned int cpu, count;

	if (!numa_enabled)
		return;

	for_each_online_yesde(yesde) {
		pr_info("Node %d CPUs:", yesde);

		count = 0;
		/*
		 * If we used a CPU iterator here we would miss printing
		 * the holes in the cpumap.
		 */
		for (cpu = 0; cpu < nr_cpu_ids; cpu++) {
			if (cpumask_test_cpu(cpu,
					yesde_to_cpumask_map[yesde])) {
				if (count == 0)
					pr_cont(" %u", cpu);
				++count;
			} else {
				if (count > 1)
					pr_cont("-%u", cpu - 1);
				count = 0;
			}
		}

		if (count > 1)
			pr_cont("-%u", nr_cpu_ids - 1);
		pr_cont("\n");
	}
}

/* Initialize NODE_DATA for a yesde on the local memory */
static void __init setup_yesde_data(int nid, u64 start_pfn, u64 end_pfn)
{
	u64 spanned_pages = end_pfn - start_pfn;
	const size_t nd_size = roundup(sizeof(pg_data_t), SMP_CACHE_BYTES);
	u64 nd_pa;
	void *nd;
	int tnid;

	nd_pa = memblock_phys_alloc_try_nid(nd_size, SMP_CACHE_BYTES, nid);
	if (!nd_pa)
		panic("Canyest allocate %zu bytes for yesde %d data\n",
		      nd_size, nid);

	nd = __va(nd_pa);

	/* report and initialize */
	pr_info("  NODE_DATA [mem %#010Lx-%#010Lx]\n",
		nd_pa, nd_pa + nd_size - 1);
	tnid = early_pfn_to_nid(nd_pa >> PAGE_SHIFT);
	if (tnid != nid)
		pr_info("    NODE_DATA(%d) on yesde %d\n", nid, tnid);

	yesde_data[nid] = nd;
	memset(NODE_DATA(nid), 0, sizeof(pg_data_t));
	NODE_DATA(nid)->yesde_id = nid;
	NODE_DATA(nid)->yesde_start_pfn = start_pfn;
	NODE_DATA(nid)->yesde_spanned_pages = spanned_pages;
}

static void __init find_possible_yesdes(void)
{
	struct device_yesde *rtas;
	u32 numyesdes, i;

	if (!numa_enabled)
		return;

	rtas = of_find_yesde_by_path("/rtas");
	if (!rtas)
		return;

	if (of_property_read_u32_index(rtas,
				"ibm,max-associativity-domains",
				min_common_depth, &numyesdes))
		goto out;

	for (i = 0; i < numyesdes; i++) {
		if (!yesde_possible(i))
			yesde_set(i, yesde_possible_map);
	}

out:
	of_yesde_put(rtas);
}

void __init mem_topology_setup(void)
{
	int cpu;

	if (parse_numa_properties())
		setup_yesnnuma();

	/*
	 * Modify the set of possible NUMA yesdes to reflect information
	 * available about the set of online yesdes, and the set of yesdes
	 * that we expect to make use of for this platform's affinity
	 * calculations.
	 */
	yesdes_and(yesde_possible_map, yesde_possible_map, yesde_online_map);

	find_possible_yesdes();

	setup_yesde_to_cpumask_map();

	reset_numa_cpu_lookup_table();

	for_each_present_cpu(cpu)
		numa_setup_cpu(cpu);
}

void __init initmem_init(void)
{
	int nid;

	max_low_pfn = memblock_end_of_DRAM() >> PAGE_SHIFT;
	max_pfn = max_low_pfn;

	memblock_dump_all();

	for_each_online_yesde(nid) {
		unsigned long start_pfn, end_pfn;

		get_pfn_range_for_nid(nid, &start_pfn, &end_pfn);
		setup_yesde_data(nid, start_pfn, end_pfn);
		sparse_memory_present_with_active_regions(nid);
	}

	sparse_init();

	/*
	 * We need the numa_cpu_lookup_table to be accurate for all CPUs,
	 * even before we online them, so that we can use cpu_to_{yesde,mem}
	 * early in boot, cf. smp_prepare_cpus().
	 * _yescalls() + manual invocation is used because cpuhp is yest yet
	 * initialized for the boot CPU.
	 */
	cpuhp_setup_state_yescalls(CPUHP_POWER_NUMA_PREPARE, "powerpc/numa:prepare",
				  ppc_numa_cpu_prepare, ppc_numa_cpu_dead);
}

static int __init early_numa(char *p)
{
	if (!p)
		return 0;

	if (strstr(p, "off"))
		numa_enabled = 0;

	if (strstr(p, "debug"))
		numa_debug = 1;

	p = strstr(p, "fake=");
	if (p)
		cmdline = p + strlen("fake=");

	return 0;
}
early_param("numa", early_numa);

/*
 * The platform can inform us through one of several mechanisms
 * (post-migration device tree updates, PRRN or VPHN) that the NUMA
 * assignment of a resource has changed. This controls whether we act
 * on that. Disabled by default.
 */
static bool topology_updates_enabled;

static int __init early_topology_updates(char *p)
{
	if (!p)
		return 0;

	if (!strcmp(p, "on")) {
		pr_warn("Caution: enabling topology updates\n");
		topology_updates_enabled = true;
	}

	return 0;
}
early_param("topology_updates", early_topology_updates);

#ifdef CONFIG_MEMORY_HOTPLUG
/*
 * Find the yesde associated with a hot added memory section for
 * memory represented in the device tree by the property
 * ibm,dynamic-reconfiguration-memory/ibm,dynamic-memory.
 */
static int hot_add_drconf_scn_to_nid(unsigned long scn_addr)
{
	struct drmem_lmb *lmb;
	unsigned long lmb_size;
	int nid = NUMA_NO_NODE;

	lmb_size = drmem_lmb_size();

	for_each_drmem_lmb(lmb) {
		/* skip this block if it is reserved or yest assigned to
		 * this partition */
		if ((lmb->flags & DRCONF_MEM_RESERVED)
		    || !(lmb->flags & DRCONF_MEM_ASSIGNED))
			continue;

		if ((scn_addr < lmb->base_addr)
		    || (scn_addr >= (lmb->base_addr + lmb_size)))
			continue;

		nid = of_drconf_to_nid_single(lmb);
		break;
	}

	return nid;
}

/*
 * Find the yesde associated with a hot added memory section for memory
 * represented in the device tree as a yesde (i.e. memory@XXXX) for
 * each memblock.
 */
static int hot_add_yesde_scn_to_nid(unsigned long scn_addr)
{
	struct device_yesde *memory;
	int nid = NUMA_NO_NODE;

	for_each_yesde_by_type(memory, "memory") {
		unsigned long start, size;
		int ranges;
		const __be32 *memcell_buf;
		unsigned int len;

		memcell_buf = of_get_property(memory, "reg", &len);
		if (!memcell_buf || len <= 0)
			continue;

		/* ranges in cell */
		ranges = (len >> 2) / (n_mem_addr_cells + n_mem_size_cells);

		while (ranges--) {
			start = read_n_cells(n_mem_addr_cells, &memcell_buf);
			size = read_n_cells(n_mem_size_cells, &memcell_buf);

			if ((scn_addr < start) || (scn_addr >= (start + size)))
				continue;

			nid = of_yesde_to_nid_single(memory);
			break;
		}

		if (nid >= 0)
			break;
	}

	of_yesde_put(memory);

	return nid;
}

/*
 * Find the yesde associated with a hot added memory section.  Section
 * corresponds to a SPARSEMEM section, yest an MEMBLOCK.  It is assumed that
 * sections are fully contained within a single MEMBLOCK.
 */
int hot_add_scn_to_nid(unsigned long scn_addr)
{
	struct device_yesde *memory = NULL;
	int nid;

	if (!numa_enabled)
		return first_online_yesde;

	memory = of_find_yesde_by_path("/ibm,dynamic-reconfiguration-memory");
	if (memory) {
		nid = hot_add_drconf_scn_to_nid(scn_addr);
		of_yesde_put(memory);
	} else {
		nid = hot_add_yesde_scn_to_nid(scn_addr);
	}

	if (nid < 0 || !yesde_possible(nid))
		nid = first_online_yesde;

	return nid;
}

static u64 hot_add_drconf_memory_max(void)
{
	struct device_yesde *memory = NULL;
	struct device_yesde *dn = NULL;
	const __be64 *lrdr = NULL;

	dn = of_find_yesde_by_path("/rtas");
	if (dn) {
		lrdr = of_get_property(dn, "ibm,lrdr-capacity", NULL);
		of_yesde_put(dn);
		if (lrdr)
			return be64_to_cpup(lrdr);
	}

	memory = of_find_yesde_by_path("/ibm,dynamic-reconfiguration-memory");
	if (memory) {
		of_yesde_put(memory);
		return drmem_lmb_memory_max();
	}
	return 0;
}

/*
 * memory_hotplug_max - return max address of memory that may be added
 *
 * This is currently only used on systems that support drconfig memory
 * hotplug.
 */
u64 memory_hotplug_max(void)
{
        return max(hot_add_drconf_memory_max(), memblock_end_of_DRAM());
}
#endif /* CONFIG_MEMORY_HOTPLUG */

/* Virtual Processor Home Node (VPHN) support */
#ifdef CONFIG_PPC_SPLPAR
struct topology_update_data {
	struct topology_update_data *next;
	unsigned int cpu;
	int old_nid;
	int new_nid;
};

#define TOPOLOGY_DEF_TIMER_SECS	60

static u8 vphn_cpu_change_counts[NR_CPUS][MAX_DISTANCE_REF_POINTS];
static cpumask_t cpu_associativity_changes_mask;
static int vphn_enabled;
static int prrn_enabled;
static void reset_topology_timer(void);
static int topology_timer_secs = 1;
static int topology_inited;

/*
 * Change polling interval for associativity changes.
 */
int timed_topology_update(int nsecs)
{
	if (vphn_enabled) {
		if (nsecs > 0)
			topology_timer_secs = nsecs;
		else
			topology_timer_secs = TOPOLOGY_DEF_TIMER_SECS;

		reset_topology_timer();
	}

	return 0;
}

/*
 * Store the current values of the associativity change counters in the
 * hypervisor.
 */
static void setup_cpu_associativity_change_counters(void)
{
	int cpu;

	/* The VPHN feature supports a maximum of 8 reference points */
	BUILD_BUG_ON(MAX_DISTANCE_REF_POINTS > 8);

	for_each_possible_cpu(cpu) {
		int i;
		u8 *counts = vphn_cpu_change_counts[cpu];
		volatile u8 *hypervisor_counts = lppaca_of(cpu).vphn_assoc_counts;

		for (i = 0; i < distance_ref_points_depth; i++)
			counts[i] = hypervisor_counts[i];
	}
}

/*
 * The hypervisor maintains a set of 8 associativity change counters in
 * the VPA of each cpu that correspond to the associativity levels in the
 * ibm,associativity-reference-points property. When an associativity
 * level changes, the corresponding counter is incremented.
 *
 * Set a bit in cpu_associativity_changes_mask for each cpu whose home
 * yesde associativity levels have changed.
 *
 * Returns the number of cpus with unhandled associativity changes.
 */
static int update_cpu_associativity_changes_mask(void)
{
	int cpu;
	cpumask_t *changes = &cpu_associativity_changes_mask;

	for_each_possible_cpu(cpu) {
		int i, changed = 0;
		u8 *counts = vphn_cpu_change_counts[cpu];
		volatile u8 *hypervisor_counts = lppaca_of(cpu).vphn_assoc_counts;

		for (i = 0; i < distance_ref_points_depth; i++) {
			if (hypervisor_counts[i] != counts[i]) {
				counts[i] = hypervisor_counts[i];
				changed = 1;
			}
		}
		if (changed) {
			cpumask_or(changes, changes, cpu_sibling_mask(cpu));
			cpu = cpu_last_thread_sibling(cpu);
		}
	}

	return cpumask_weight(changes);
}

/*
 * Retrieve the new associativity information for a virtual processor's
 * home yesde.
 */
static long vphn_get_associativity(unsigned long cpu,
					__be32 *associativity)
{
	long rc;

	rc = hcall_vphn(get_hard_smp_processor_id(cpu),
				VPHN_FLAG_VCPU, associativity);

	switch (rc) {
	case H_FUNCTION:
		printk_once(KERN_INFO
			"VPHN is yest supported. Disabling polling...\n");
		stop_topology_update();
		break;
	case H_HARDWARE:
		printk(KERN_ERR
			"hcall_vphn() experienced a hardware fault "
			"preventing VPHN. Disabling polling...\n");
		stop_topology_update();
		break;
	case H_SUCCESS:
		dbg("VPHN hcall succeeded. Reset polling...\n");
		timed_topology_update(0);
		break;
	}

	return rc;
}

int find_and_online_cpu_nid(int cpu)
{
	__be32 associativity[VPHN_ASSOC_BUFSIZE] = {0};
	int new_nid;

	/* Use associativity from first thread for all siblings */
	if (vphn_get_associativity(cpu, associativity))
		return cpu_to_yesde(cpu);

	new_nid = associativity_to_nid(associativity);
	if (new_nid < 0 || !yesde_possible(new_nid))
		new_nid = first_online_yesde;

	if (NODE_DATA(new_nid) == NULL) {
#ifdef CONFIG_MEMORY_HOTPLUG
		/*
		 * Need to ensure that NODE_DATA is initialized for a yesde from
		 * available memory (see memblock_alloc_try_nid). If unable to
		 * init the yesde, then default to nearest yesde that has memory
		 * installed. Skip onlining a yesde if the subsystems are yest
		 * yet initialized.
		 */
		if (!topology_inited || try_online_yesde(new_nid))
			new_nid = first_online_yesde;
#else
		/*
		 * Default to using the nearest yesde that has memory installed.
		 * Otherwise, it would be necessary to patch the kernel MM code
		 * to deal with more memoryless-yesde error conditions.
		 */
		new_nid = first_online_yesde;
#endif
	}

	pr_debug("%s:%d cpu %d nid %d\n", __FUNCTION__, __LINE__,
		cpu, new_nid);
	return new_nid;
}

/*
 * Update the CPU maps and sysfs entries for a single CPU when its NUMA
 * characteristics change. This function doesn't perform any locking and is
 * only safe to call from stop_machine().
 */
static int update_cpu_topology(void *data)
{
	struct topology_update_data *update;
	unsigned long cpu;

	if (!data)
		return -EINVAL;

	cpu = smp_processor_id();

	for (update = data; update; update = update->next) {
		int new_nid = update->new_nid;
		if (cpu != update->cpu)
			continue;

		unmap_cpu_from_yesde(cpu);
		map_cpu_to_yesde(cpu, new_nid);
		set_cpu_numa_yesde(cpu, new_nid);
		set_cpu_numa_mem(cpu, local_memory_yesde(new_nid));
		vdso_getcpu_init();
	}

	return 0;
}

static int update_lookup_table(void *data)
{
	struct topology_update_data *update;

	if (!data)
		return -EINVAL;

	/*
	 * Upon topology update, the numa-cpu lookup table needs to be updated
	 * for all threads in the core, including offline CPUs, to ensure that
	 * future hotplug operations respect the cpu-to-yesde associativity
	 * properly.
	 */
	for (update = data; update; update = update->next) {
		int nid, base, j;

		nid = update->new_nid;
		base = cpu_first_thread_sibling(update->cpu);

		for (j = 0; j < threads_per_core; j++) {
			update_numa_cpu_lookup_table(base + j, nid);
		}
	}

	return 0;
}

/*
 * Update the yesde maps and sysfs entries for each cpu whose home yesde
 * has changed. Returns 1 when the topology has changed, and 0 otherwise.
 *
 * cpus_locked says whether we already hold cpu_hotplug_lock.
 */
int numa_update_cpu_topology(bool cpus_locked)
{
	unsigned int cpu, sibling, changed = 0;
	struct topology_update_data *updates, *ud;
	cpumask_t updated_cpus;
	struct device *dev;
	int weight, new_nid, i = 0;

	if (!prrn_enabled && !vphn_enabled && topology_inited)
		return 0;

	weight = cpumask_weight(&cpu_associativity_changes_mask);
	if (!weight)
		return 0;

	updates = kcalloc(weight, sizeof(*updates), GFP_KERNEL);
	if (!updates)
		return 0;

	cpumask_clear(&updated_cpus);

	for_each_cpu(cpu, &cpu_associativity_changes_mask) {
		/*
		 * If siblings aren't flagged for changes, updates list
		 * will be too short. Skip on this update and set for next
		 * update.
		 */
		if (!cpumask_subset(cpu_sibling_mask(cpu),
					&cpu_associativity_changes_mask)) {
			pr_info("Sibling bits yest set for associativity "
					"change, cpu%d\n", cpu);
			cpumask_or(&cpu_associativity_changes_mask,
					&cpu_associativity_changes_mask,
					cpu_sibling_mask(cpu));
			cpu = cpu_last_thread_sibling(cpu);
			continue;
		}

		new_nid = find_and_online_cpu_nid(cpu);

		if (new_nid == numa_cpu_lookup_table[cpu]) {
			cpumask_andyest(&cpu_associativity_changes_mask,
					&cpu_associativity_changes_mask,
					cpu_sibling_mask(cpu));
			dbg("Assoc chg gives same yesde %d for cpu%d\n",
					new_nid, cpu);
			cpu = cpu_last_thread_sibling(cpu);
			continue;
		}

		for_each_cpu(sibling, cpu_sibling_mask(cpu)) {
			ud = &updates[i++];
			ud->next = &updates[i];
			ud->cpu = sibling;
			ud->new_nid = new_nid;
			ud->old_nid = numa_cpu_lookup_table[sibling];
			cpumask_set_cpu(sibling, &updated_cpus);
		}
		cpu = cpu_last_thread_sibling(cpu);
	}

	/*
	 * Prevent processing of 'updates' from overflowing array
	 * where last entry filled in a 'next' pointer.
	 */
	if (i)
		updates[i-1].next = NULL;

	pr_debug("Topology update for the following CPUs:\n");
	if (cpumask_weight(&updated_cpus)) {
		for (ud = &updates[0]; ud; ud = ud->next) {
			pr_debug("cpu %d moving from yesde %d "
					  "to %d\n", ud->cpu,
					  ud->old_nid, ud->new_nid);
		}
	}

	/*
	 * In cases where we have yesthing to update (because the updates list
	 * is too short or because the new topology is same as the old one),
	 * skip invoking update_cpu_topology() via stop-machine(). This is
	 * necessary (and yest just a fast-path optimization) since stop-machine
	 * can end up electing a random CPU to run update_cpu_topology(), and
	 * thus trick us into setting up incorrect cpu-yesde mappings (since
	 * 'updates' is kzalloc()'ed).
	 *
	 * And for the similar reason, we will skip all the following updating.
	 */
	if (!cpumask_weight(&updated_cpus))
		goto out;

	if (cpus_locked)
		stop_machine_cpuslocked(update_cpu_topology, &updates[0],
					&updated_cpus);
	else
		stop_machine(update_cpu_topology, &updates[0], &updated_cpus);

	/*
	 * Update the numa-cpu lookup table with the new mappings, even for
	 * offline CPUs. It is best to perform this update from the stop-
	 * machine context.
	 */
	if (cpus_locked)
		stop_machine_cpuslocked(update_lookup_table, &updates[0],
					cpumask_of(raw_smp_processor_id()));
	else
		stop_machine(update_lookup_table, &updates[0],
			     cpumask_of(raw_smp_processor_id()));

	for (ud = &updates[0]; ud; ud = ud->next) {
		unregister_cpu_under_yesde(ud->cpu, ud->old_nid);
		register_cpu_under_yesde(ud->cpu, ud->new_nid);

		dev = get_cpu_device(ud->cpu);
		if (dev)
			kobject_uevent(&dev->kobj, KOBJ_CHANGE);
		cpumask_clear_cpu(ud->cpu, &cpu_associativity_changes_mask);
		changed = 1;
	}

out:
	kfree(updates);
	return changed;
}

int arch_update_cpu_topology(void)
{
	return numa_update_cpu_topology(true);
}

static void topology_work_fn(struct work_struct *work)
{
	rebuild_sched_domains();
}
static DECLARE_WORK(topology_work, topology_work_fn);

static void topology_schedule_update(void)
{
	schedule_work(&topology_work);
}

static void topology_timer_fn(struct timer_list *unused)
{
	if (prrn_enabled && cpumask_weight(&cpu_associativity_changes_mask))
		topology_schedule_update();
	else if (vphn_enabled) {
		if (update_cpu_associativity_changes_mask() > 0)
			topology_schedule_update();
		reset_topology_timer();
	}
}
static struct timer_list topology_timer;

static void reset_topology_timer(void)
{
	if (vphn_enabled)
		mod_timer(&topology_timer, jiffies + topology_timer_secs * HZ);
}

#ifdef CONFIG_SMP

static int dt_update_callback(struct yestifier_block *nb,
				unsigned long action, void *data)
{
	struct of_reconfig_data *update = data;
	int rc = NOTIFY_DONE;

	switch (action) {
	case OF_RECONFIG_UPDATE_PROPERTY:
		if (of_yesde_is_type(update->dn, "cpu") &&
		    !of_prop_cmp(update->prop->name, "ibm,associativity")) {
			u32 core_id;
			of_property_read_u32(update->dn, "reg", &core_id);
			rc = dlpar_cpu_readd(core_id);
			rc = NOTIFY_OK;
		}
		break;
	}

	return rc;
}

static struct yestifier_block dt_update_nb = {
	.yestifier_call = dt_update_callback,
};

#endif

/*
 * Start polling for associativity changes.
 */
int start_topology_update(void)
{
	int rc = 0;

	if (!topology_updates_enabled)
		return 0;

	if (firmware_has_feature(FW_FEATURE_PRRN)) {
		if (!prrn_enabled) {
			prrn_enabled = 1;
#ifdef CONFIG_SMP
			rc = of_reconfig_yestifier_register(&dt_update_nb);
#endif
		}
	}
	if (firmware_has_feature(FW_FEATURE_VPHN) &&
		   lppaca_shared_proc(get_lppaca())) {
		if (!vphn_enabled) {
			vphn_enabled = 1;
			setup_cpu_associativity_change_counters();
			timer_setup(&topology_timer, topology_timer_fn,
				    TIMER_DEFERRABLE);
			reset_topology_timer();
		}
	}

	pr_info("Starting topology update%s%s\n",
		(prrn_enabled ? " prrn_enabled" : ""),
		(vphn_enabled ? " vphn_enabled" : ""));

	return rc;
}

/*
 * Disable polling for VPHN associativity changes.
 */
int stop_topology_update(void)
{
	int rc = 0;

	if (!topology_updates_enabled)
		return 0;

	if (prrn_enabled) {
		prrn_enabled = 0;
#ifdef CONFIG_SMP
		rc = of_reconfig_yestifier_unregister(&dt_update_nb);
#endif
	}
	if (vphn_enabled) {
		vphn_enabled = 0;
		rc = del_timer_sync(&topology_timer);
	}

	pr_info("Stopping topology update\n");

	return rc;
}

int prrn_is_enabled(void)
{
	return prrn_enabled;
}

void __init shared_proc_topology_init(void)
{
	if (lppaca_shared_proc(get_lppaca())) {
		bitmap_fill(cpumask_bits(&cpu_associativity_changes_mask),
			    nr_cpumask_bits);
		numa_update_cpu_topology(false);
	}
}

static int topology_read(struct seq_file *file, void *v)
{
	if (vphn_enabled || prrn_enabled)
		seq_puts(file, "on\n");
	else
		seq_puts(file, "off\n");

	return 0;
}

static int topology_open(struct iyesde *iyesde, struct file *file)
{
	return single_open(file, topology_read, NULL);
}

static ssize_t topology_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *off)
{
	char kbuf[4]; /* "on" or "off" plus null. */
	int read_len;

	read_len = count < 3 ? count : 3;
	if (copy_from_user(kbuf, buf, read_len))
		return -EINVAL;

	kbuf[read_len] = '\0';

	if (!strncmp(kbuf, "on", 2)) {
		topology_updates_enabled = true;
		start_topology_update();
	} else if (!strncmp(kbuf, "off", 3)) {
		stop_topology_update();
		topology_updates_enabled = false;
	} else
		return -EINVAL;

	return count;
}

static const struct file_operations topology_ops = {
	.read = seq_read,
	.write = topology_write,
	.open = topology_open,
	.release = single_release
};

static int topology_update_init(void)
{
	start_topology_update();

	if (vphn_enabled)
		topology_schedule_update();

	if (!proc_create("powerpc/topology_updates", 0644, NULL, &topology_ops))
		return -ENOMEM;

	topology_inited = 1;
	return 0;
}
device_initcall(topology_update_init);
#endif /* CONFIG_PPC_SPLPAR */
