/*
 * pSeries NUMA support
 *
 * Copyright (C) 2002 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/threads.h>
#include <linux/bootmem.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/export.h>
#include <linux/nodemask.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/memblock.h>
#include <linux/of.h>
#include <linux/pfn.h>
#include <linux/cpuset.h>
#include <linux/node.h>
#include <asm/sparsemem.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/firmware.h>
#include <asm/paca.h>
#include <asm/hvcall.h>
#include <asm/setup.h>

static int numa_enabled = 1;

static char *cmdline __initdata;

static int numa_debug;
#define dbg(args...) if (numa_debug) { printk(KERN_INFO args); }

int numa_cpu_lookup_table[NR_CPUS];
cpumask_var_t node_to_cpumask_map[MAX_NUMNODES];
struct pglist_data *node_data[MAX_NUMNODES];

EXPORT_SYMBOL(numa_cpu_lookup_table);
EXPORT_SYMBOL(node_to_cpumask_map);
EXPORT_SYMBOL(node_data);

static int min_common_depth;
static int n_mem_addr_cells, n_mem_size_cells;
static int form1_affinity;

#define MAX_DISTANCE_REF_POINTS 4
static int distance_ref_points_depth;
static const unsigned int *distance_ref_points;
static int distance_lookup_table[MAX_NUMNODES][MAX_DISTANCE_REF_POINTS];

/*
 * Allocate node_to_cpumask_map based on number of available nodes
 * Requires node_possible_map to be valid.
 *
 * Note: cpumask_of_node() is not valid until after this is done.
 */
static void __init setup_node_to_cpumask_map(void)
{
	unsigned int node, num = 0;

	/* setup nr_node_ids if not done yet */
	if (nr_node_ids == MAX_NUMNODES) {
		for_each_node_mask(node, node_possible_map)
			num = node;
		nr_node_ids = num + 1;
	}

	/* allocate the map */
	for (node = 0; node < nr_node_ids; node++)
		alloc_bootmem_cpumask_var(&node_to_cpumask_map[node]);

	/* cpumask_of_node() will now work */
	dbg("Node to cpumask map for %d nodes\n", nr_node_ids);
}

static int __cpuinit fake_numa_create_new_node(unsigned long end_pfn,
						unsigned int *nid)
{
	unsigned long long mem;
	char *p = cmdline;
	static unsigned int fake_nid;
	static unsigned long long curr_boundary;

	/*
	 * Modify node id, iff we started creating NUMA nodes
	 * We want to continue from where we left of the last time
	 */
	if (fake_nid)
		*nid = fake_nid;
	/*
	 * In case there are no more arguments to parse, the
	 * node_id should be the same as the last fake node id
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
		dbg("created new fake_node with id %d\n", fake_nid);
		return 1;
	}
	return 0;
}

/*
 * get_node_active_region - Return active region containing pfn
 * Active range returned is empty if none found.
 * @pfn: The page to return the region for
 * @node_ar: Returned set to the active region containing @pfn
 */
static void __init get_node_active_region(unsigned long pfn,
					  struct node_active_region *node_ar)
{
	unsigned long start_pfn, end_pfn;
	int i, nid;

	for_each_mem_pfn_range(i, MAX_NUMNODES, &start_pfn, &end_pfn, &nid) {
		if (pfn >= start_pfn && pfn < end_pfn) {
			node_ar->nid = nid;
			node_ar->start_pfn = start_pfn;
			node_ar->end_pfn = end_pfn;
			break;
		}
	}
}

static void map_cpu_to_node(int cpu, int node)
{
	numa_cpu_lookup_table[cpu] = node;

	dbg("adding cpu %d to node %d\n", cpu, node);

	if (!(cpumask_test_cpu(cpu, node_to_cpumask_map[node])))
		cpumask_set_cpu(cpu, node_to_cpumask_map[node]);
}

#if defined(CONFIG_HOTPLUG_CPU) || defined(CONFIG_PPC_SPLPAR)
static void unmap_cpu_from_node(unsigned long cpu)
{
	int node = numa_cpu_lookup_table[cpu];

	dbg("removing cpu %lu from node %d\n", cpu, node);

	if (cpumask_test_cpu(cpu, node_to_cpumask_map[node])) {
		cpumask_clear_cpu(cpu, node_to_cpumask_map[node]);
	} else {
		printk(KERN_ERR "WARNING: cpu %lu not found in node %d\n",
		       cpu, node);
	}
}
#endif /* CONFIG_HOTPLUG_CPU || CONFIG_PPC_SPLPAR */

/* must hold reference to node during call */
static const int *of_get_associativity(struct device_node *dev)
{
	return of_get_property(dev, "ibm,associativity", NULL);
}

/*
 * Returns the property linux,drconf-usable-memory if
 * it exists (the property exists only in kexec/kdump kernels,
 * added by kexec-tools)
 */
static const u32 *of_get_usable_memory(struct device_node *memory)
{
	const u32 *prop;
	u32 len;
	prop = of_get_property(memory, "linux,drconf-usable-memory", &len);
	if (!prop || len < sizeof(unsigned int))
		return 0;
	return prop;
}

int __node_distance(int a, int b)
{
	int i;
	int distance = LOCAL_DISTANCE;

	if (!form1_affinity)
		return distance;

	for (i = 0; i < distance_ref_points_depth; i++) {
		if (distance_lookup_table[a][i] == distance_lookup_table[b][i])
			break;

		/* Double the distance for each NUMA level */
		distance *= 2;
	}

	return distance;
}

static void initialize_distance_lookup_table(int nid,
		const unsigned int *associativity)
{
	int i;

	if (!form1_affinity)
		return;

	for (i = 0; i < distance_ref_points_depth; i++) {
		distance_lookup_table[nid][i] =
			associativity[distance_ref_points[i]];
	}
}

/* Returns nid in the range [0..MAX_NUMNODES-1], or -1 if no useful numa
 * info is found.
 */
static int associativity_to_nid(const unsigned int *associativity)
{
	int nid = -1;

	if (min_common_depth == -1)
		goto out;

	if (associativity[0] >= min_common_depth)
		nid = associativity[min_common_depth];

	/* POWER4 LPAR uses 0xffff as invalid node */
	if (nid == 0xffff || nid >= MAX_NUMNODES)
		nid = -1;

	if (nid > 0 && associativity[0] >= distance_ref_points_depth)
		initialize_distance_lookup_table(nid, associativity);

out:
	return nid;
}

/* Returns the nid associated with the given device tree node,
 * or -1 if not found.
 */
static int of_node_to_nid_single(struct device_node *device)
{
	int nid = -1;
	const unsigned int *tmp;

	tmp = of_get_associativity(device);
	if (tmp)
		nid = associativity_to_nid(tmp);
	return nid;
}

/* Walk the device tree upwards, looking for an associativity id */
int of_node_to_nid(struct device_node *device)
{
	struct device_node *tmp;
	int nid = -1;

	of_node_get(device);
	while (device) {
		nid = of_node_to_nid_single(device);
		if (nid != -1)
			break;

	        tmp = device;
		device = of_get_parent(tmp);
		of_node_put(tmp);
	}
	of_node_put(device);

	return nid;
}
EXPORT_SYMBOL_GPL(of_node_to_nid);

static int __init find_min_common_depth(void)
{
	int depth;
	struct device_node *chosen;
	struct device_node *root;
	const char *vec5;

	if (firmware_has_feature(FW_FEATURE_OPAL))
		root = of_find_node_by_path("/ibm,opal");
	else
		root = of_find_node_by_path("/rtas");
	if (!root)
		root = of_find_node_by_path("/");

	/*
	 * This property is a set of 32-bit integers, each representing
	 * an index into the ibm,associativity nodes.
	 *
	 * With form 0 affinity the first integer is for an SMP configuration
	 * (should be all 0's) and the second is for a normal NUMA
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
		dbg("NUMA: ibm,associativity-reference-points not found.\n");
		goto err;
	}

	distance_ref_points_depth /= sizeof(int);

#define VEC5_AFFINITY_BYTE	5
#define VEC5_AFFINITY		0x80

	if (firmware_has_feature(FW_FEATURE_OPAL))
		form1_affinity = 1;
	else {
		chosen = of_find_node_by_path("/chosen");
		if (chosen) {
			vec5 = of_get_property(chosen,
					       "ibm,architecture-vec-5", NULL);
			if (vec5 && (vec5[VEC5_AFFINITY_BYTE] &
							VEC5_AFFINITY)) {
				dbg("Using form 1 affinity\n");
				form1_affinity = 1;
			}

			of_node_put(chosen);
		}
	}

	if (form1_affinity) {
		depth = distance_ref_points[0];
	} else {
		if (distance_ref_points_depth < 2) {
			printk(KERN_WARNING "NUMA: "
				"short ibm,associativity-reference-points\n");
			goto err;
		}

		depth = distance_ref_points[1];
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

	of_node_put(root);
	return depth;

err:
	of_node_put(root);
	return -1;
}

static void __init get_n_mem_cells(int *n_addr_cells, int *n_size_cells)
{
	struct device_node *memory = NULL;

	memory = of_find_node_by_type(memory, "memory");
	if (!memory)
		panic("numa.c: No memory nodes found!");

	*n_addr_cells = of_n_addr_cells(memory);
	*n_size_cells = of_n_size_cells(memory);
	of_node_put(memory);
}

static unsigned long read_n_cells(int n, const unsigned int **buf)
{
	unsigned long result = 0;

	while (n--) {
		result = (result << 32) | **buf;
		(*buf)++;
	}
	return result;
}

struct of_drconf_cell {
	u64	base_addr;
	u32	drc_index;
	u32	reserved;
	u32	aa_index;
	u32	flags;
};

#define DRCONF_MEM_ASSIGNED	0x00000008
#define DRCONF_MEM_AI_INVALID	0x00000040
#define DRCONF_MEM_RESERVED	0x00000080

/*
 * Read the next memblock list entry from the ibm,dynamic-memory property
 * and return the information in the provided of_drconf_cell structure.
 */
static void read_drconf_cell(struct of_drconf_cell *drmem, const u32 **cellp)
{
	const u32 *cp;

	drmem->base_addr = read_n_cells(n_mem_addr_cells, cellp);

	cp = *cellp;
	drmem->drc_index = cp[0];
	drmem->reserved = cp[1];
	drmem->aa_index = cp[2];
	drmem->flags = cp[3];

	*cellp = cp + 4;
}

/*
 * Retrieve and validate the ibm,dynamic-memory property of the device tree.
 *
 * The layout of the ibm,dynamic-memory property is a number N of memblock
 * list entries followed by N memblock list entries.  Each memblock list entry
 * contains information as laid out in the of_drconf_cell struct above.
 */
static int of_get_drconf_memory(struct device_node *memory, const u32 **dm)
{
	const u32 *prop;
	u32 len, entries;

	prop = of_get_property(memory, "ibm,dynamic-memory", &len);
	if (!prop || len < sizeof(unsigned int))
		return 0;

	entries = *prop++;

	/* Now that we know the number of entries, revalidate the size
	 * of the property read in to ensure we have everything
	 */
	if (len < (entries * (n_mem_addr_cells + 4) + 1) * sizeof(unsigned int))
		return 0;

	*dm = prop;
	return entries;
}

/*
 * Retrieve and validate the ibm,lmb-size property for drconf memory
 * from the device tree.
 */
static u64 of_get_lmb_size(struct device_node *memory)
{
	const u32 *prop;
	u32 len;

	prop = of_get_property(memory, "ibm,lmb-size", &len);
	if (!prop || len < sizeof(unsigned int))
		return 0;

	return read_n_cells(n_mem_size_cells, &prop);
}

struct assoc_arrays {
	u32	n_arrays;
	u32	array_sz;
	const u32 *arrays;
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
static int of_get_assoc_arrays(struct device_node *memory,
			       struct assoc_arrays *aa)
{
	const u32 *prop;
	u32 len;

	prop = of_get_property(memory, "ibm,associativity-lookup-arrays", &len);
	if (!prop || len < 2 * sizeof(unsigned int))
		return -1;

	aa->n_arrays = *prop++;
	aa->array_sz = *prop++;

	/* Now that we know the number of arrays and size of each array,
	 * revalidate the size of the property read in.
	 */
	if (len < (aa->n_arrays * aa->array_sz + 2) * sizeof(unsigned int))
		return -1;

	aa->arrays = prop;
	return 0;
}

/*
 * This is like of_node_to_nid_single() for memory represented in the
 * ibm,dynamic-reconfiguration-memory node.
 */
static int of_drconf_to_nid_single(struct of_drconf_cell *drmem,
				   struct assoc_arrays *aa)
{
	int default_nid = 0;
	int nid = default_nid;
	int index;

	if (min_common_depth > 0 && min_common_depth <= aa->array_sz &&
	    !(drmem->flags & DRCONF_MEM_AI_INVALID) &&
	    drmem->aa_index < aa->n_arrays) {
		index = drmem->aa_index * aa->array_sz + min_common_depth - 1;
		nid = aa->arrays[index];

		if (nid == 0xffff || nid >= MAX_NUMNODES)
			nid = default_nid;
	}

	return nid;
}

/*
 * Figure out to which domain a cpu belongs and stick it there.
 * Return the id of the domain used.
 */
static int __cpuinit numa_setup_cpu(unsigned long lcpu)
{
	int nid = 0;
	struct device_node *cpu = of_get_cpu_node(lcpu, NULL);

	if (!cpu) {
		WARN_ON(1);
		goto out;
	}

	nid = of_node_to_nid_single(cpu);

	if (nid < 0 || !node_online(nid))
		nid = first_online_node;
out:
	map_cpu_to_node(lcpu, nid);

	of_node_put(cpu);

	return nid;
}

static int __cpuinit cpu_numa_callback(struct notifier_block *nfb,
			     unsigned long action,
			     void *hcpu)
{
	unsigned long lcpu = (unsigned long)hcpu;
	int ret = NOTIFY_DONE;

	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		numa_setup_cpu(lcpu);
		ret = NOTIFY_OK;
		break;
#ifdef CONFIG_HOTPLUG_CPU
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
		unmap_cpu_from_node(lcpu);
		break;
		ret = NOTIFY_OK;
#endif
	}
	return ret;
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
	 * iommu_is_off, memory_limit is not set but is implicitly enforced.
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
static inline int __init read_usm_ranges(const u32 **usm)
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
 * node.  This assumes n_mem_{addr,size}_cells have been set.
 */
static void __init parse_drconf_memory(struct device_node *memory)
{
	const u32 *uninitialized_var(dm), *usm;
	unsigned int n, rc, ranges, is_kexec_kdump = 0;
	unsigned long lmb_size, base, size, sz;
	int nid;
	struct assoc_arrays aa = { .arrays = NULL };

	n = of_get_drconf_memory(memory, &dm);
	if (!n)
		return;

	lmb_size = of_get_lmb_size(memory);
	if (!lmb_size)
		return;

	rc = of_get_assoc_arrays(memory, &aa);
	if (rc)
		return;

	/* check if this is a kexec/kdump kernel */
	usm = of_get_usable_memory(memory);
	if (usm != NULL)
		is_kexec_kdump = 1;

	for (; n != 0; --n) {
		struct of_drconf_cell drmem;

		read_drconf_cell(&drmem, &dm);

		/* skip this block if the reserved bit is set in flags (0x80)
		   or if the block is not assigned to this partition (0x8) */
		if ((drmem.flags & DRCONF_MEM_RESERVED)
		    || !(drmem.flags & DRCONF_MEM_ASSIGNED))
			continue;

		base = drmem.base_addr;
		size = lmb_size;
		ranges = 1;

		if (is_kexec_kdump) {
			ranges = read_usm_ranges(&usm);
			if (!ranges) /* there are no (base, size) duple */
				continue;
		}
		do {
			if (is_kexec_kdump) {
				base = read_n_cells(n_mem_addr_cells, &usm);
				size = read_n_cells(n_mem_size_cells, &usm);
			}
			nid = of_drconf_to_nid_single(&drmem, &aa);
			fake_numa_create_new_node(
				((base + size) >> PAGE_SHIFT),
					   &nid);
			node_set_online(nid);
			sz = numa_enforce_memory_limit(base, size);
			if (sz)
				memblock_set_node(base, sz, nid);
		} while (--ranges);
	}
}

static int __init parse_numa_properties(void)
{
	struct device_node *memory;
	int default_nid = 0;
	unsigned long i;

	if (numa_enabled == 0) {
		printk(KERN_WARNING "NUMA disabled by user\n");
		return -1;
	}

	min_common_depth = find_min_common_depth();

	if (min_common_depth < 0)
		return min_common_depth;

	dbg("NUMA associativity depth for CPU/Memory: %d\n", min_common_depth);

	/*
	 * Even though we connect cpus to numa domains later in SMP
	 * init, we need to know the node ids now. This is because
	 * each node to be onlined must have NODE_DATA etc backing it.
	 */
	for_each_present_cpu(i) {
		struct device_node *cpu;
		int nid;

		cpu = of_get_cpu_node(i, NULL);
		BUG_ON(!cpu);
		nid = of_node_to_nid_single(cpu);
		of_node_put(cpu);

		/*
		 * Don't fall back to default_nid yet -- we will plug
		 * cpus into nodes once the memory scan has discovered
		 * the topology.
		 */
		if (nid < 0)
			continue;
		node_set_online(nid);
	}

	get_n_mem_cells(&n_mem_addr_cells, &n_mem_size_cells);

	for_each_node_by_type(memory, "memory") {
		unsigned long start;
		unsigned long size;
		int nid;
		int ranges;
		const unsigned int *memcell_buf;
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
		 * Assumption: either all memory nodes or none will
		 * have associativity properties.  If none, then
		 * everything goes to default_nid.
		 */
		nid = of_node_to_nid_single(memory);
		if (nid < 0)
			nid = default_nid;

		fake_numa_create_new_node(((start + size) >> PAGE_SHIFT), &nid);
		node_set_online(nid);

		if (!(size = numa_enforce_memory_limit(start, size))) {
			if (--ranges)
				goto new_range;
			else
				continue;
		}

		memblock_set_node(start, size, nid);

		if (--ranges)
			goto new_range;
	}

	/*
	 * Now do the same thing for each MEMBLOCK listed in the
	 * ibm,dynamic-memory property in the
	 * ibm,dynamic-reconfiguration-memory node.
	 */
	memory = of_find_node_by_path("/ibm,dynamic-reconfiguration-memory");
	if (memory)
		parse_drconf_memory(memory);

	return 0;
}

static void __init setup_nonnuma(void)
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

		fake_numa_create_new_node(end_pfn, &nid);
		memblock_set_node(PFN_PHYS(start_pfn),
				  PFN_PHYS(end_pfn - start_pfn), nid);
		node_set_online(nid);
	}
}

void __init dump_numa_cpu_topology(void)
{
	unsigned int node;
	unsigned int cpu, count;

	if (min_common_depth == -1 || !numa_enabled)
		return;

	for_each_online_node(node) {
		printk(KERN_DEBUG "Node %d CPUs:", node);

		count = 0;
		/*
		 * If we used a CPU iterator here we would miss printing
		 * the holes in the cpumap.
		 */
		for (cpu = 0; cpu < nr_cpu_ids; cpu++) {
			if (cpumask_test_cpu(cpu,
					node_to_cpumask_map[node])) {
				if (count == 0)
					printk(" %u", cpu);
				++count;
			} else {
				if (count > 1)
					printk("-%u", cpu - 1);
				count = 0;
			}
		}

		if (count > 1)
			printk("-%u", nr_cpu_ids - 1);
		printk("\n");
	}
}

static void __init dump_numa_memory_topology(void)
{
	unsigned int node;
	unsigned int count;

	if (min_common_depth == -1 || !numa_enabled)
		return;

	for_each_online_node(node) {
		unsigned long i;

		printk(KERN_DEBUG "Node %d Memory:", node);

		count = 0;

		for (i = 0; i < memblock_end_of_DRAM();
		     i += (1 << SECTION_SIZE_BITS)) {
			if (early_pfn_to_nid(i >> PAGE_SHIFT) == node) {
				if (count == 0)
					printk(" 0x%lx", i);
				++count;
			} else {
				if (count > 0)
					printk("-0x%lx", i);
				count = 0;
			}
		}

		if (count > 0)
			printk("-0x%lx", i);
		printk("\n");
	}
}

/*
 * Allocate some memory, satisfying the memblock or bootmem allocator where
 * required. nid is the preferred node and end is the physical address of
 * the highest address in the node.
 *
 * Returns the virtual address of the memory.
 */
static void __init *careful_zallocation(int nid, unsigned long size,
				       unsigned long align,
				       unsigned long end_pfn)
{
	void *ret;
	int new_nid;
	unsigned long ret_paddr;

	ret_paddr = __memblock_alloc_base(size, align, end_pfn << PAGE_SHIFT);

	/* retry over all memory */
	if (!ret_paddr)
		ret_paddr = __memblock_alloc_base(size, align, memblock_end_of_DRAM());

	if (!ret_paddr)
		panic("numa.c: cannot allocate %lu bytes for node %d",
		      size, nid);

	ret = __va(ret_paddr);

	/*
	 * We initialize the nodes in numeric order: 0, 1, 2...
	 * and hand over control from the MEMBLOCK allocator to the
	 * bootmem allocator.  If this function is called for
	 * node 5, then we know that all nodes <5 are using the
	 * bootmem allocator instead of the MEMBLOCK allocator.
	 *
	 * So, check the nid from which this allocation came
	 * and double check to see if we need to use bootmem
	 * instead of the MEMBLOCK.  We don't free the MEMBLOCK memory
	 * since it would be useless.
	 */
	new_nid = early_pfn_to_nid(ret_paddr >> PAGE_SHIFT);
	if (new_nid < nid) {
		ret = __alloc_bootmem_node(NODE_DATA(new_nid),
				size, align, 0);

		dbg("alloc_bootmem %p %lx\n", ret, size);
	}

	memset(ret, 0, size);
	return ret;
}

static struct notifier_block __cpuinitdata ppc64_numa_nb = {
	.notifier_call = cpu_numa_callback,
	.priority = 1 /* Must run before sched domains notifier. */
};

static void __init mark_reserved_regions_for_nid(int nid)
{
	struct pglist_data *node = NODE_DATA(nid);
	struct memblock_region *reg;

	for_each_memblock(reserved, reg) {
		unsigned long physbase = reg->base;
		unsigned long size = reg->size;
		unsigned long start_pfn = physbase >> PAGE_SHIFT;
		unsigned long end_pfn = PFN_UP(physbase + size);
		struct node_active_region node_ar;
		unsigned long node_end_pfn = node->node_start_pfn +
					     node->node_spanned_pages;

		/*
		 * Check to make sure that this memblock.reserved area is
		 * within the bounds of the node that we care about.
		 * Checking the nid of the start and end points is not
		 * sufficient because the reserved area could span the
		 * entire node.
		 */
		if (end_pfn <= node->node_start_pfn ||
		    start_pfn >= node_end_pfn)
			continue;

		get_node_active_region(start_pfn, &node_ar);
		while (start_pfn < end_pfn &&
			node_ar.start_pfn < node_ar.end_pfn) {
			unsigned long reserve_size = size;
			/*
			 * if reserved region extends past active region
			 * then trim size to active region
			 */
			if (end_pfn > node_ar.end_pfn)
				reserve_size = (node_ar.end_pfn << PAGE_SHIFT)
					- physbase;
			/*
			 * Only worry about *this* node, others may not
			 * yet have valid NODE_DATA().
			 */
			if (node_ar.nid == nid) {
				dbg("reserve_bootmem %lx %lx nid=%d\n",
					physbase, reserve_size, node_ar.nid);
				reserve_bootmem_node(NODE_DATA(node_ar.nid),
						physbase, reserve_size,
						BOOTMEM_DEFAULT);
			}
			/*
			 * if reserved region is contained in the active region
			 * then done.
			 */
			if (end_pfn <= node_ar.end_pfn)
				break;

			/*
			 * reserved region extends past the active region
			 *   get next active region that contains this
			 *   reserved region
			 */
			start_pfn = node_ar.end_pfn;
			physbase = start_pfn << PAGE_SHIFT;
			size = size - reserve_size;
			get_node_active_region(start_pfn, &node_ar);
		}
	}
}


void __init do_init_bootmem(void)
{
	int nid;

	min_low_pfn = 0;
	max_low_pfn = memblock_end_of_DRAM() >> PAGE_SHIFT;
	max_pfn = max_low_pfn;

	if (parse_numa_properties())
		setup_nonnuma();
	else
		dump_numa_memory_topology();

	for_each_online_node(nid) {
		unsigned long start_pfn, end_pfn;
		void *bootmem_vaddr;
		unsigned long bootmap_pages;

		get_pfn_range_for_nid(nid, &start_pfn, &end_pfn);

		/*
		 * Allocate the node structure node local if possible
		 *
		 * Be careful moving this around, as it relies on all
		 * previous nodes' bootmem to be initialized and have
		 * all reserved areas marked.
		 */
		NODE_DATA(nid) = careful_zallocation(nid,
					sizeof(struct pglist_data),
					SMP_CACHE_BYTES, end_pfn);

  		dbg("node %d\n", nid);
		dbg("NODE_DATA() = %p\n", NODE_DATA(nid));

		NODE_DATA(nid)->bdata = &bootmem_node_data[nid];
		NODE_DATA(nid)->node_start_pfn = start_pfn;
		NODE_DATA(nid)->node_spanned_pages = end_pfn - start_pfn;

		if (NODE_DATA(nid)->node_spanned_pages == 0)
  			continue;

  		dbg("start_paddr = %lx\n", start_pfn << PAGE_SHIFT);
  		dbg("end_paddr = %lx\n", end_pfn << PAGE_SHIFT);

		bootmap_pages = bootmem_bootmap_pages(end_pfn - start_pfn);
		bootmem_vaddr = careful_zallocation(nid,
					bootmap_pages << PAGE_SHIFT,
					PAGE_SIZE, end_pfn);

		dbg("bootmap_vaddr = %p\n", bootmem_vaddr);

		init_bootmem_node(NODE_DATA(nid),
				  __pa(bootmem_vaddr) >> PAGE_SHIFT,
				  start_pfn, end_pfn);

		free_bootmem_with_active_regions(nid, end_pfn);
		/*
		 * Be very careful about moving this around.  Future
		 * calls to careful_zallocation() depend on this getting
		 * done correctly.
		 */
		mark_reserved_regions_for_nid(nid);
		sparse_memory_present_with_active_regions(nid);
	}

	init_bootmem_done = 1;

	/*
	 * Now bootmem is initialised we can create the node to cpumask
	 * lookup tables and setup the cpu callback to populate them.
	 */
	setup_node_to_cpumask_map();

	register_cpu_notifier(&ppc64_numa_nb);
	cpu_numa_callback(&ppc64_numa_nb, CPU_UP_PREPARE,
			  (void *)(unsigned long)boot_cpuid);
}

void __init paging_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];
	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));
	max_zone_pfns[ZONE_DMA] = memblock_end_of_DRAM() >> PAGE_SHIFT;
	free_area_init_nodes(max_zone_pfns);
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

#ifdef CONFIG_MEMORY_HOTPLUG
/*
 * Find the node associated with a hot added memory section for
 * memory represented in the device tree by the property
 * ibm,dynamic-reconfiguration-memory/ibm,dynamic-memory.
 */
static int hot_add_drconf_scn_to_nid(struct device_node *memory,
				     unsigned long scn_addr)
{
	const u32 *dm;
	unsigned int drconf_cell_cnt, rc;
	unsigned long lmb_size;
	struct assoc_arrays aa;
	int nid = -1;

	drconf_cell_cnt = of_get_drconf_memory(memory, &dm);
	if (!drconf_cell_cnt)
		return -1;

	lmb_size = of_get_lmb_size(memory);
	if (!lmb_size)
		return -1;

	rc = of_get_assoc_arrays(memory, &aa);
	if (rc)
		return -1;

	for (; drconf_cell_cnt != 0; --drconf_cell_cnt) {
		struct of_drconf_cell drmem;

		read_drconf_cell(&drmem, &dm);

		/* skip this block if it is reserved or not assigned to
		 * this partition */
		if ((drmem.flags & DRCONF_MEM_RESERVED)
		    || !(drmem.flags & DRCONF_MEM_ASSIGNED))
			continue;

		if ((scn_addr < drmem.base_addr)
		    || (scn_addr >= (drmem.base_addr + lmb_size)))
			continue;

		nid = of_drconf_to_nid_single(&drmem, &aa);
		break;
	}

	return nid;
}

/*
 * Find the node associated with a hot added memory section for memory
 * represented in the device tree as a node (i.e. memory@XXXX) for
 * each memblock.
 */
int hot_add_node_scn_to_nid(unsigned long scn_addr)
{
	struct device_node *memory;
	int nid = -1;

	for_each_node_by_type(memory, "memory") {
		unsigned long start, size;
		int ranges;
		const unsigned int *memcell_buf;
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

			nid = of_node_to_nid_single(memory);
			break;
		}

		if (nid >= 0)
			break;
	}

	of_node_put(memory);

	return nid;
}

/*
 * Find the node associated with a hot added memory section.  Section
 * corresponds to a SPARSEMEM section, not an MEMBLOCK.  It is assumed that
 * sections are fully contained within a single MEMBLOCK.
 */
int hot_add_scn_to_nid(unsigned long scn_addr)
{
	struct device_node *memory = NULL;
	int nid, found = 0;

	if (!numa_enabled || (min_common_depth < 0))
		return first_online_node;

	memory = of_find_node_by_path("/ibm,dynamic-reconfiguration-memory");
	if (memory) {
		nid = hot_add_drconf_scn_to_nid(memory, scn_addr);
		of_node_put(memory);
	} else {
		nid = hot_add_node_scn_to_nid(scn_addr);
	}

	if (nid < 0 || !node_online(nid))
		nid = first_online_node;

	if (NODE_DATA(nid)->node_spanned_pages)
		return nid;

	for_each_online_node(nid) {
		if (NODE_DATA(nid)->node_spanned_pages) {
			found = 1;
			break;
		}
	}

	BUG_ON(!found);
	return nid;
}

static u64 hot_add_drconf_memory_max(void)
{
        struct device_node *memory = NULL;
        unsigned int drconf_cell_cnt = 0;
        u64 lmb_size = 0;
        const u32 *dm = 0;

        memory = of_find_node_by_path("/ibm,dynamic-reconfiguration-memory");
        if (memory) {
                drconf_cell_cnt = of_get_drconf_memory(memory, &dm);
                lmb_size = of_get_lmb_size(memory);
                of_node_put(memory);
        }
        return lmb_size * drconf_cell_cnt;
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
static u8 vphn_cpu_change_counts[NR_CPUS][MAX_DISTANCE_REF_POINTS];
static cpumask_t cpu_associativity_changes_mask;
static int vphn_enabled;
static void set_topology_timer(void);

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
		volatile u8 *hypervisor_counts = lppaca[cpu].vphn_assoc_counts;

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
 * node associativity levels have changed.
 *
 * Returns the number of cpus with unhandled associativity changes.
 */
static int update_cpu_associativity_changes_mask(void)
{
	int cpu, nr_cpus = 0;
	cpumask_t *changes = &cpu_associativity_changes_mask;

	cpumask_clear(changes);

	for_each_possible_cpu(cpu) {
		int i, changed = 0;
		u8 *counts = vphn_cpu_change_counts[cpu];
		volatile u8 *hypervisor_counts = lppaca[cpu].vphn_assoc_counts;

		for (i = 0; i < distance_ref_points_depth; i++) {
			if (hypervisor_counts[i] != counts[i]) {
				counts[i] = hypervisor_counts[i];
				changed = 1;
			}
		}
		if (changed) {
			cpumask_set_cpu(cpu, changes);
			nr_cpus++;
		}
	}

	return nr_cpus;
}

/*
 * 6 64-bit registers unpacked into 12 32-bit associativity values. To form
 * the complete property we have to add the length in the first cell.
 */
#define VPHN_ASSOC_BUFSIZE (6*sizeof(u64)/sizeof(u32) + 1)

/*
 * Convert the associativity domain numbers returned from the hypervisor
 * to the sequence they would appear in the ibm,associativity property.
 */
static int vphn_unpack_associativity(const long *packed, unsigned int *unpacked)
{
	int i, nr_assoc_doms = 0;
	const u16 *field = (const u16*) packed;

#define VPHN_FIELD_UNUSED	(0xffff)
#define VPHN_FIELD_MSB		(0x8000)
#define VPHN_FIELD_MASK		(~VPHN_FIELD_MSB)

	for (i = 1; i < VPHN_ASSOC_BUFSIZE; i++) {
		if (*field == VPHN_FIELD_UNUSED) {
			/* All significant fields processed, and remaining
			 * fields contain the reserved value of all 1's.
			 * Just store them.
			 */
			unpacked[i] = *((u32*)field);
			field += 2;
		} else if (*field & VPHN_FIELD_MSB) {
			/* Data is in the lower 15 bits of this field */
			unpacked[i] = *field & VPHN_FIELD_MASK;
			field++;
			nr_assoc_doms++;
		} else {
			/* Data is in the lower 15 bits of this field
			 * concatenated with the next 16 bit field
			 */
			unpacked[i] = *((u32*)field);
			field += 2;
			nr_assoc_doms++;
		}
	}

	/* The first cell contains the length of the property */
	unpacked[0] = nr_assoc_doms;

	return nr_assoc_doms;
}

/*
 * Retrieve the new associativity information for a virtual processor's
 * home node.
 */
static long hcall_vphn(unsigned long cpu, unsigned int *associativity)
{
	long rc;
	long retbuf[PLPAR_HCALL9_BUFSIZE] = {0};
	u64 flags = 1;
	int hwcpu = get_hard_smp_processor_id(cpu);

	rc = plpar_hcall9(H_HOME_NODE_ASSOCIATIVITY, retbuf, flags, hwcpu);
	vphn_unpack_associativity(retbuf, associativity);

	return rc;
}

static long vphn_get_associativity(unsigned long cpu,
					unsigned int *associativity)
{
	long rc;

	rc = hcall_vphn(cpu, associativity);

	switch (rc) {
	case H_FUNCTION:
		printk(KERN_INFO
			"VPHN is not supported. Disabling polling...\n");
		stop_topology_update();
		break;
	case H_HARDWARE:
		printk(KERN_ERR
			"hcall_vphn() experienced a hardware fault "
			"preventing VPHN. Disabling polling...\n");
		stop_topology_update();
	}

	return rc;
}

/*
 * Update the node maps and sysfs entries for each cpu whose home node
 * has changed.
 */
int arch_update_cpu_topology(void)
{
	int cpu, nid, old_nid;
	unsigned int associativity[VPHN_ASSOC_BUFSIZE] = {0};
	struct device *dev;

	for_each_cpu(cpu,&cpu_associativity_changes_mask) {
		vphn_get_associativity(cpu, associativity);
		nid = associativity_to_nid(associativity);

		if (nid < 0 || !node_online(nid))
			nid = first_online_node;

		old_nid = numa_cpu_lookup_table[cpu];

		/* Disable hotplug while we update the cpu
		 * masks and sysfs.
		 */
		get_online_cpus();
		unregister_cpu_under_node(cpu, old_nid);
		unmap_cpu_from_node(cpu);
		map_cpu_to_node(cpu, nid);
		register_cpu_under_node(cpu, nid);
		put_online_cpus();

		dev = get_cpu_device(cpu);
		if (dev)
			kobject_uevent(&dev->kobj, KOBJ_CHANGE);
	}

	return 1;
}

static void topology_work_fn(struct work_struct *work)
{
	rebuild_sched_domains();
}
static DECLARE_WORK(topology_work, topology_work_fn);

void topology_schedule_update(void)
{
	schedule_work(&topology_work);
}

static void topology_timer_fn(unsigned long ignored)
{
	if (!vphn_enabled)
		return;
	if (update_cpu_associativity_changes_mask() > 0)
		topology_schedule_update();
	set_topology_timer();
}
static struct timer_list topology_timer =
	TIMER_INITIALIZER(topology_timer_fn, 0, 0);

static void set_topology_timer(void)
{
	topology_timer.data = 0;
	topology_timer.expires = jiffies + 60 * HZ;
	add_timer(&topology_timer);
}

/*
 * Start polling for VPHN associativity changes.
 */
int start_topology_update(void)
{
	int rc = 0;

	/* Disabled until races with load balancing are fixed */
	if (0 && firmware_has_feature(FW_FEATURE_VPHN) &&
	    get_lppaca()->shared_proc) {
		vphn_enabled = 1;
		setup_cpu_associativity_change_counters();
		init_timer_deferrable(&topology_timer);
		set_topology_timer();
		rc = 1;
	}

	return rc;
}
__initcall(start_topology_update);

/*
 * Disable polling for VPHN associativity changes.
 */
int stop_topology_update(void)
{
	vphn_enabled = 0;
	return del_timer_sync(&topology_timer);
}
#endif /* CONFIG_PPC_SPLPAR */
