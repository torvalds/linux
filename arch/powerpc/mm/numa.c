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
#define pr_fmt(fmt) "numa: " fmt

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
#include <linux/stop_machine.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <asm/cputhreads.h>
#include <asm/sparsemem.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/cputhreads.h>
#include <asm/topology.h>
#include <asm/firmware.h>
#include <asm/paca.h>
#include <asm/hvcall.h>
#include <asm/setup.h>
#include <asm/vdso.h>

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
static const __be32 *distance_ref_points;
static int distance_lookup_table[MAX_NUMNODES][MAX_DISTANCE_REF_POINTS];

/*
 * Allocate node_to_cpumask_map based on number of available nodes
 * Requires node_possible_map to be valid.
 *
 * Note: cpumask_of_node() is not valid until after this is done.
 */
static void __init setup_node_to_cpumask_map(void)
{
	unsigned int node;

	/* setup nr_node_ids if not done yet */
	if (nr_node_ids == MAX_NUMNODES)
		setup_nr_node_ids();

	/* allocate the map */
	for_each_node(node)
		alloc_bootmem_cpumask_var(&node_to_cpumask_map[node]);

	/* cpumask_of_node() will now work */
	dbg("Node to cpumask map for %d nodes\n", nr_node_ids);
}

static int __init fake_numa_create_new_node(unsigned long end_pfn,
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

static void reset_numa_cpu_lookup_table(void)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu)
		numa_cpu_lookup_table[cpu] = -1;
}

static void update_numa_cpu_lookup_table(unsigned int cpu, int node)
{
	numa_cpu_lookup_table[cpu] = node;
}

static void map_cpu_to_node(int cpu, int node)
{
	update_numa_cpu_lookup_table(cpu, node);

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
static const __be32 *of_get_associativity(struct device_node *dev)
{
	return of_get_property(dev, "ibm,associativity", NULL);
}

/*
 * Returns the property linux,drconf-usable-memory if
 * it exists (the property exists only in kexec/kdump kernels,
 * added by kexec-tools)
 */
static const __be32 *of_get_usable_memory(struct device_node *memory)
{
	const __be32 *prop;
	u32 len;
	prop = of_get_property(memory, "linux,drconf-usable-memory", &len);
	if (!prop || len < sizeof(unsigned int))
		return NULL;
	return prop;
}

int __node_distance(int a, int b)
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
EXPORT_SYMBOL(__node_distance);

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

/* Returns nid in the range [0..MAX_NUMNODES-1], or -1 if no useful numa
 * info is found.
 */
static int associativity_to_nid(const __be32 *associativity)
{
	int nid = -1;

	if (min_common_depth == -1)
		goto out;

	if (of_read_number(associativity, 1) >= min_common_depth)
		nid = of_read_number(&associativity[min_common_depth], 1);

	/* POWER4 LPAR uses 0xffff as invalid node */
	if (nid == 0xffff || nid >= MAX_NUMNODES)
		nid = -1;

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

/* Returns the nid associated with the given device tree node,
 * or -1 if not found.
 */
static int of_node_to_nid_single(struct device_node *device)
{
	int nid = -1;
	const __be32 *tmp;

	tmp = of_get_associativity(device);
	if (tmp)
		nid = associativity_to_nid(tmp);
	return nid;
}

/* Walk the device tree upwards, looking for an associativity id */
int of_node_to_nid(struct device_node *device)
{
	int nid = -1;

	of_node_get(device);
	while (device) {
		nid = of_node_to_nid_single(device);
		if (nid != -1)
			break;

		device = of_get_next_parent(device);
	}
	of_node_put(device);

	return nid;
}
EXPORT_SYMBOL_GPL(of_node_to_nid);

static int __init find_min_common_depth(void)
{
	int depth;
	struct device_node *root;

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

static unsigned long read_n_cells(int n, const __be32 **buf)
{
	unsigned long result = 0;

	while (n--) {
		result = (result << 32) | of_read_number(*buf, 1);
		(*buf)++;
	}
	return result;
}

/*
 * Read the next memblock list entry from the ibm,dynamic-memory property
 * and return the information in the provided of_drconf_cell structure.
 */
static void read_drconf_cell(struct of_drconf_cell *drmem, const __be32 **cellp)
{
	const __be32 *cp;

	drmem->base_addr = read_n_cells(n_mem_addr_cells, cellp);

	cp = *cellp;
	drmem->drc_index = of_read_number(cp, 1);
	drmem->reserved = of_read_number(&cp[1], 1);
	drmem->aa_index = of_read_number(&cp[2], 1);
	drmem->flags = of_read_number(&cp[3], 1);

	*cellp = cp + 4;
}

/*
 * Retrieve and validate the ibm,dynamic-memory property of the device tree.
 *
 * The layout of the ibm,dynamic-memory property is a number N of memblock
 * list entries followed by N memblock list entries.  Each memblock list entry
 * contains information as laid out in the of_drconf_cell struct above.
 */
static int of_get_drconf_memory(struct device_node *memory, const __be32 **dm)
{
	const __be32 *prop;
	u32 len, entries;

	prop = of_get_property(memory, "ibm,dynamic-memory", &len);
	if (!prop || len < sizeof(unsigned int))
		return 0;

	entries = of_read_number(prop++, 1);

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
	const __be32 *prop;
	u32 len;

	prop = of_get_property(memory, "ibm,lmb-size", &len);
	if (!prop || len < sizeof(unsigned int))
		return 0;

	return read_n_cells(n_mem_size_cells, &prop);
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
static int of_get_assoc_arrays(struct device_node *memory,
			       struct assoc_arrays *aa)
{
	const __be32 *prop;
	u32 len;

	prop = of_get_property(memory, "ibm,associativity-lookup-arrays", &len);
	if (!prop || len < 2 * sizeof(unsigned int))
		return -1;

	aa->n_arrays = of_read_number(prop++, 1);
	aa->array_sz = of_read_number(prop++, 1);

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
		nid = of_read_number(&aa->arrays[index], 1);

		if (nid == 0xffff || nid >= MAX_NUMNODES)
			nid = default_nid;

		if (nid > 0) {
			index = drmem->aa_index * aa->array_sz;
			initialize_distance_lookup_table(nid,
							&aa->arrays[index]);
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
	int nid = -1;
	struct device_node *cpu;

	/*
	 * If a valid cpu-to-node mapping is already available, use it
	 * directly instead of querying the firmware, since it represents
	 * the most recent mapping notified to us by the platform (eg: VPHN).
	 */
	if ((nid = numa_cpu_lookup_table[lcpu]) >= 0) {
		map_cpu_to_node(lcpu, nid);
		return nid;
	}

	cpu = of_get_cpu_node(lcpu, NULL);

	if (!cpu) {
		WARN_ON(1);
		if (cpu_present(lcpu))
			goto out_present;
		else
			goto out;
	}

	nid = of_node_to_nid_single(cpu);

out_present:
	if (nid < 0 || !node_online(nid))
		nid = first_online_node;

	map_cpu_to_node(lcpu, nid);
	of_node_put(cpu);
out:
	return nid;
}

static void verify_cpu_node_mapping(int cpu, int node)
{
	int base, sibling, i;

	/* Verify that all the threads in the core belong to the same node */
	base = cpu_first_thread_sibling(cpu);

	for (i = 0; i < threads_per_core; i++) {
		sibling = base + i;

		if (sibling == cpu || cpu_is_offline(sibling))
			continue;

		if (cpu_to_node(sibling) != node) {
			WARN(1, "CPU thread siblings %d and %d don't belong"
				" to the same node!\n", cpu, sibling);
			break;
		}
	}
}

static int cpu_numa_callback(struct notifier_block *nfb, unsigned long action,
			     void *hcpu)
{
	unsigned long lcpu = (unsigned long)hcpu;
	int ret = NOTIFY_DONE, nid;

	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		nid = numa_setup_cpu(lcpu);
		verify_cpu_node_mapping((int)lcpu, nid);
		ret = NOTIFY_OK;
		break;
#ifdef CONFIG_HOTPLUG_CPU
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
		unmap_cpu_from_node(lcpu);
		ret = NOTIFY_OK;
		break;
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
 * node.  This assumes n_mem_{addr,size}_cells have been set.
 */
static void __init parse_drconf_memory(struct device_node *memory)
{
	const __be32 *uninitialized_var(dm), *usm;
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
				memblock_set_node(base, sz,
						  &memblock.memory, nid);
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

		memblock_set_node(start, size, &memblock.memory, nid);

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
				  PFN_PHYS(end_pfn - start_pfn),
				  &memblock.memory, nid);
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

static struct notifier_block ppc64_numa_nb = {
	.notifier_call = cpu_numa_callback,
	.priority = 1 /* Must run before sched domains notifier. */
};

/* Initialize NODE_DATA for a node on the local memory */
static void __init setup_node_data(int nid, u64 start_pfn, u64 end_pfn)
{
	u64 spanned_pages = end_pfn - start_pfn;
	const size_t nd_size = roundup(sizeof(pg_data_t), SMP_CACHE_BYTES);
	u64 nd_pa;
	void *nd;
	int tnid;

	if (spanned_pages)
		pr_info("Initmem setup node %d [mem %#010Lx-%#010Lx]\n",
			nid, start_pfn << PAGE_SHIFT,
			(end_pfn << PAGE_SHIFT) - 1);
	else
		pr_info("Initmem setup node %d\n", nid);

	nd_pa = memblock_alloc_try_nid(nd_size, SMP_CACHE_BYTES, nid);
	nd = __va(nd_pa);

	/* report and initialize */
	pr_info("  NODE_DATA [mem %#010Lx-%#010Lx]\n",
		nd_pa, nd_pa + nd_size - 1);
	tnid = early_pfn_to_nid(nd_pa >> PAGE_SHIFT);
	if (tnid != nid)
		pr_info("    NODE_DATA(%d) on node %d\n", nid, tnid);

	node_data[nid] = nd;
	memset(NODE_DATA(nid), 0, sizeof(pg_data_t));
	NODE_DATA(nid)->node_id = nid;
	NODE_DATA(nid)->node_start_pfn = start_pfn;
	NODE_DATA(nid)->node_spanned_pages = spanned_pages;
}

void __init initmem_init(void)
{
	int nid, cpu;

	max_low_pfn = memblock_end_of_DRAM() >> PAGE_SHIFT;
	max_pfn = max_low_pfn;

	if (parse_numa_properties())
		setup_nonnuma();
	else
		dump_numa_memory_topology();

	memblock_dump_all();

	/*
	 * Reduce the possible NUMA nodes to the online NUMA nodes,
	 * since we do not support node hotplug. This ensures that  we
	 * lower the maximum NUMA node ID to what is actually present.
	 */
	nodes_and(node_possible_map, node_possible_map, node_online_map);

	for_each_online_node(nid) {
		unsigned long start_pfn, end_pfn;

		get_pfn_range_for_nid(nid, &start_pfn, &end_pfn);
		setup_node_data(nid, start_pfn, end_pfn);
		sparse_memory_present_with_active_regions(nid);
	}

	sparse_init();

	setup_node_to_cpumask_map();

	reset_numa_cpu_lookup_table();
	register_cpu_notifier(&ppc64_numa_nb);
	/*
	 * We need the numa_cpu_lookup_table to be accurate for all CPUs,
	 * even before we online them, so that we can use cpu_to_{node,mem}
	 * early in boot, cf. smp_prepare_cpus().
	 */
	for_each_present_cpu(cpu) {
		numa_setup_cpu((unsigned long)cpu);
	}
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

static bool topology_updates_enabled = true;

static int __init early_topology_updates(char *p)
{
	if (!p)
		return 0;

	if (!strcmp(p, "off")) {
		pr_info("Disabling topology updates\n");
		topology_updates_enabled = false;
	}

	return 0;
}
early_param("topology_updates", early_topology_updates);

#ifdef CONFIG_MEMORY_HOTPLUG
/*
 * Find the node associated with a hot added memory section for
 * memory represented in the device tree by the property
 * ibm,dynamic-reconfiguration-memory/ibm,dynamic-memory.
 */
static int hot_add_drconf_scn_to_nid(struct device_node *memory,
				     unsigned long scn_addr)
{
	const __be32 *dm;
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
static int hot_add_node_scn_to_nid(unsigned long scn_addr)
{
	struct device_node *memory;
	int nid = -1;

	for_each_node_by_type(memory, "memory") {
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
	const __be32 *dm = NULL;

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

#include "vphn.h"

struct topology_update_data {
	struct topology_update_data *next;
	unsigned int cpu;
	int old_nid;
	int new_nid;
};

static u8 vphn_cpu_change_counts[NR_CPUS][MAX_DISTANCE_REF_POINTS];
static cpumask_t cpu_associativity_changes_mask;
static int vphn_enabled;
static int prrn_enabled;
static void reset_topology_timer(void);

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
	int cpu;
	cpumask_t *changes = &cpu_associativity_changes_mask;

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
			cpumask_or(changes, changes, cpu_sibling_mask(cpu));
			cpu = cpu_last_thread_sibling(cpu);
		}
	}

	return cpumask_weight(changes);
}

/*
 * Retrieve the new associativity information for a virtual processor's
 * home node.
 */
static long hcall_vphn(unsigned long cpu, __be32 *associativity)
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
					__be32 *associativity)
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

		unmap_cpu_from_node(cpu);
		map_cpu_to_node(cpu, new_nid);
		set_cpu_numa_node(cpu, new_nid);
		set_cpu_numa_mem(cpu, local_memory_node(new_nid));
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
	 * future hotplug operations respect the cpu-to-node associativity
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
 * Update the node maps and sysfs entries for each cpu whose home node
 * has changed. Returns 1 when the topology has changed, and 0 otherwise.
 */
int arch_update_cpu_topology(void)
{
	unsigned int cpu, sibling, changed = 0;
	struct topology_update_data *updates, *ud;
	__be32 associativity[VPHN_ASSOC_BUFSIZE] = {0};
	cpumask_t updated_cpus;
	struct device *dev;
	int weight, new_nid, i = 0;

	if (!prrn_enabled && !vphn_enabled)
		return 0;

	weight = cpumask_weight(&cpu_associativity_changes_mask);
	if (!weight)
		return 0;

	updates = kzalloc(weight * (sizeof(*updates)), GFP_KERNEL);
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
			pr_info("Sibling bits not set for associativity "
					"change, cpu%d\n", cpu);
			cpumask_or(&cpu_associativity_changes_mask,
					&cpu_associativity_changes_mask,
					cpu_sibling_mask(cpu));
			cpu = cpu_last_thread_sibling(cpu);
			continue;
		}

		/* Use associativity from first thread for all siblings */
		vphn_get_associativity(cpu, associativity);
		new_nid = associativity_to_nid(associativity);
		if (new_nid < 0 || !node_online(new_nid))
			new_nid = first_online_node;

		if (new_nid == numa_cpu_lookup_table[cpu]) {
			cpumask_andnot(&cpu_associativity_changes_mask,
					&cpu_associativity_changes_mask,
					cpu_sibling_mask(cpu));
			cpu = cpu_last_thread_sibling(cpu);
			continue;
		}

		for_each_cpu(sibling, cpu_sibling_mask(cpu)) {
			ud = &updates[i++];
			ud->cpu = sibling;
			ud->new_nid = new_nid;
			ud->old_nid = numa_cpu_lookup_table[sibling];
			cpumask_set_cpu(sibling, &updated_cpus);
			if (i < weight)
				ud->next = &updates[i];
		}
		cpu = cpu_last_thread_sibling(cpu);
	}

	pr_debug("Topology update for the following CPUs:\n");
	if (cpumask_weight(&updated_cpus)) {
		for (ud = &updates[0]; ud; ud = ud->next) {
			pr_debug("cpu %d moving from node %d "
					  "to %d\n", ud->cpu,
					  ud->old_nid, ud->new_nid);
		}
	}

	/*
	 * In cases where we have nothing to update (because the updates list
	 * is too short or because the new topology is same as the old one),
	 * skip invoking update_cpu_topology() via stop-machine(). This is
	 * necessary (and not just a fast-path optimization) since stop-machine
	 * can end up electing a random CPU to run update_cpu_topology(), and
	 * thus trick us into setting up incorrect cpu-node mappings (since
	 * 'updates' is kzalloc()'ed).
	 *
	 * And for the similar reason, we will skip all the following updating.
	 */
	if (!cpumask_weight(&updated_cpus))
		goto out;

	stop_machine(update_cpu_topology, &updates[0], &updated_cpus);

	/*
	 * Update the numa-cpu lookup table with the new mappings, even for
	 * offline CPUs. It is best to perform this update from the stop-
	 * machine context.
	 */
	stop_machine(update_lookup_table, &updates[0],
					cpumask_of(raw_smp_processor_id()));

	for (ud = &updates[0]; ud; ud = ud->next) {
		unregister_cpu_under_node(ud->cpu, ud->old_nid);
		register_cpu_under_node(ud->cpu, ud->new_nid);

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

static void topology_work_fn(struct work_struct *work)
{
	rebuild_sched_domains();
}
static DECLARE_WORK(topology_work, topology_work_fn);

static void topology_schedule_update(void)
{
	schedule_work(&topology_work);
}

static void topology_timer_fn(unsigned long ignored)
{
	if (prrn_enabled && cpumask_weight(&cpu_associativity_changes_mask))
		topology_schedule_update();
	else if (vphn_enabled) {
		if (update_cpu_associativity_changes_mask() > 0)
			topology_schedule_update();
		reset_topology_timer();
	}
}
static struct timer_list topology_timer =
	TIMER_INITIALIZER(topology_timer_fn, 0, 0);

static void reset_topology_timer(void)
{
	topology_timer.data = 0;
	topology_timer.expires = jiffies + 60 * HZ;
	mod_timer(&topology_timer, topology_timer.expires);
}

#ifdef CONFIG_SMP

static void stage_topology_update(int core_id)
{
	cpumask_or(&cpu_associativity_changes_mask,
		&cpu_associativity_changes_mask, cpu_sibling_mask(core_id));
	reset_topology_timer();
}

static int dt_update_callback(struct notifier_block *nb,
				unsigned long action, void *data)
{
	struct of_reconfig_data *update = data;
	int rc = NOTIFY_DONE;

	switch (action) {
	case OF_RECONFIG_UPDATE_PROPERTY:
		if (!of_prop_cmp(update->dn->type, "cpu") &&
		    !of_prop_cmp(update->prop->name, "ibm,associativity")) {
			u32 core_id;
			of_property_read_u32(update->dn, "reg", &core_id);
			stage_topology_update(core_id);
			rc = NOTIFY_OK;
		}
		break;
	}

	return rc;
}

static struct notifier_block dt_update_nb = {
	.notifier_call = dt_update_callback,
};

#endif

/*
 * Start polling for associativity changes.
 */
int start_topology_update(void)
{
	int rc = 0;

	if (firmware_has_feature(FW_FEATURE_PRRN)) {
		if (!prrn_enabled) {
			prrn_enabled = 1;
			vphn_enabled = 0;
#ifdef CONFIG_SMP
			rc = of_reconfig_notifier_register(&dt_update_nb);
#endif
		}
	} else if (firmware_has_feature(FW_FEATURE_VPHN) &&
		   lppaca_shared_proc(get_lppaca())) {
		if (!vphn_enabled) {
			prrn_enabled = 0;
			vphn_enabled = 1;
			setup_cpu_associativity_change_counters();
			init_timer_deferrable(&topology_timer);
			reset_topology_timer();
		}
	}

	return rc;
}

/*
 * Disable polling for VPHN associativity changes.
 */
int stop_topology_update(void)
{
	int rc = 0;

	if (prrn_enabled) {
		prrn_enabled = 0;
#ifdef CONFIG_SMP
		rc = of_reconfig_notifier_unregister(&dt_update_nb);
#endif
	} else if (vphn_enabled) {
		vphn_enabled = 0;
		rc = del_timer_sync(&topology_timer);
	}

	return rc;
}

int prrn_is_enabled(void)
{
	return prrn_enabled;
}

static int topology_read(struct seq_file *file, void *v)
{
	if (vphn_enabled || prrn_enabled)
		seq_puts(file, "on\n");
	else
		seq_puts(file, "off\n");

	return 0;
}

static int topology_open(struct inode *inode, struct file *file)
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

	if (!strncmp(kbuf, "on", 2))
		start_topology_update();
	else if (!strncmp(kbuf, "off", 3))
		stop_topology_update();
	else
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
	/* Do not poll for changes if disabled at boot */
	if (topology_updates_enabled)
		start_topology_update();

	if (!proc_create("powerpc/topology_updates", 0644, NULL, &topology_ops))
		return -ENOMEM;

	return 0;
}
device_initcall(topology_update_init);
#endif /* CONFIG_PPC_SPLPAR */
