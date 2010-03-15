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
#include <linux/module.h>
#include <linux/nodemask.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/lmb.h>
#include <linux/of.h>
#include <linux/pfn.h>
#include <asm/sparsemem.h>
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/smp.h>

static int numa_enabled = 1;

static char *cmdline __initdata;

static int numa_debug;
#define dbg(args...) if (numa_debug) { printk(KERN_INFO args); }

int numa_cpu_lookup_table[NR_CPUS];
cpumask_t numa_cpumask_lookup_table[MAX_NUMNODES];
struct pglist_data *node_data[MAX_NUMNODES];

EXPORT_SYMBOL(numa_cpu_lookup_table);
EXPORT_SYMBOL(numa_cpumask_lookup_table);
EXPORT_SYMBOL(node_data);

static int min_common_depth;
static int n_mem_addr_cells, n_mem_size_cells;

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
 * get_active_region_work_fn - A helper function for get_node_active_region
 *	Returns datax set to the start_pfn and end_pfn if they contain
 *	the initial value of datax->start_pfn between them
 * @start_pfn: start page(inclusive) of region to check
 * @end_pfn: end page(exclusive) of region to check
 * @datax: comes in with ->start_pfn set to value to search for and
 *	goes out with active range if it contains it
 * Returns 1 if search value is in range else 0
 */
static int __init get_active_region_work_fn(unsigned long start_pfn,
					unsigned long end_pfn, void *datax)
{
	struct node_active_region *data;
	data = (struct node_active_region *)datax;

	if (start_pfn <= data->start_pfn && end_pfn > data->start_pfn) {
		data->start_pfn = start_pfn;
		data->end_pfn = end_pfn;
		return 1;
	}
	return 0;

}

/*
 * get_node_active_region - Return active region containing start_pfn
 * Active range returned is empty if none found.
 * @start_pfn: The page to return the region for.
 * @node_ar: Returned set to the active region containing start_pfn
 */
static void __init get_node_active_region(unsigned long start_pfn,
		       struct node_active_region *node_ar)
{
	int nid = early_pfn_to_nid(start_pfn);

	node_ar->nid = nid;
	node_ar->start_pfn = start_pfn;
	node_ar->end_pfn = start_pfn;
	work_with_active_regions(nid, get_active_region_work_fn, node_ar);
}

static void __cpuinit map_cpu_to_node(int cpu, int node)
{
	numa_cpu_lookup_table[cpu] = node;

	dbg("adding cpu %d to node %d\n", cpu, node);

	if (!(cpu_isset(cpu, numa_cpumask_lookup_table[node])))
		cpu_set(cpu, numa_cpumask_lookup_table[node]);
}

#ifdef CONFIG_HOTPLUG_CPU
static void unmap_cpu_from_node(unsigned long cpu)
{
	int node = numa_cpu_lookup_table[cpu];

	dbg("removing cpu %lu from node %d\n", cpu, node);

	if (cpu_isset(cpu, numa_cpumask_lookup_table[node])) {
		cpu_clear(cpu, numa_cpumask_lookup_table[node]);
	} else {
		printk(KERN_ERR "WARNING: cpu %lu not found in node %d\n",
		       cpu, node);
	}
}
#endif /* CONFIG_HOTPLUG_CPU */

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

/* Returns nid in the range [0..MAX_NUMNODES-1], or -1 if no useful numa
 * info is found.
 */
static int of_node_to_nid_single(struct device_node *device)
{
	int nid = -1;
	const unsigned int *tmp;

	if (min_common_depth == -1)
		goto out;

	tmp = of_get_associativity(device);
	if (!tmp)
		goto out;

	if (tmp[0] >= min_common_depth)
		nid = tmp[min_common_depth];

	/* POWER4 LPAR uses 0xffff as invalid node */
	if (nid == 0xffff || nid >= MAX_NUMNODES)
		nid = -1;
out:
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

/*
 * In theory, the "ibm,associativity" property may contain multiple
 * associativity lists because a resource may be multiply connected
 * into the machine.  This resource then has different associativity
 * characteristics relative to its multiple connections.  We ignore
 * this for now.  We also assume that all cpu and memory sets have
 * their distances represented at a common level.  This won't be
 * true for hierarchical NUMA.
 *
 * In any case the ibm,associativity-reference-points should give
 * the correct depth for a normal NUMA system.
 *
 * - Dave Hansen <haveblue@us.ibm.com>
 */
static int __init find_min_common_depth(void)
{
	int depth;
	const unsigned int *ref_points;
	struct device_node *rtas_root;
	unsigned int len;

	rtas_root = of_find_node_by_path("/rtas");

	if (!rtas_root)
		return -1;

	/*
	 * this property is 2 32-bit integers, each representing a level of
	 * depth in the associativity nodes.  The first is for an SMP
	 * configuration (should be all 0's) and the second is for a normal
	 * NUMA configuration.
	 */
	ref_points = of_get_property(rtas_root,
			"ibm,associativity-reference-points", &len);

	if ((len >= 2 * sizeof(unsigned int)) && ref_points) {
		depth = ref_points[1];
	} else {
		dbg("NUMA: ibm,associativity-reference-points not found.\n");
		depth = -1;
	}
	of_node_put(rtas_root);

	return depth;
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

static unsigned long __devinit read_n_cells(int n, const unsigned int **buf)
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
 * Read the next lmb list entry from the ibm,dynamic-memory property
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
 * Retreive and validate the ibm,dynamic-memory property of the device tree.
 *
 * The layout of the ibm,dynamic-memory property is a number N of lmb
 * list entries followed by N lmb list entries.  Each lmb list entry
 * contains information as layed out in the of_drconf_cell struct above.
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
 * Retreive and validate the ibm,lmb-size property for drconf memory
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
 * Retreive and validate the list of associativity arrays for drconf
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

	/* Now that we know the number of arrrays and size of each array,
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
 * discarded as it lies wholy above the memory limit.
 */
static unsigned long __init numa_enforce_memory_limit(unsigned long start,
						      unsigned long size)
{
	/*
	 * We use lmb_end_of_DRAM() in here instead of memory_limit because
	 * we've already adjusted it for the limit and it takes care of
	 * having memory holes below the limit.  Also, in the case of
	 * iommu_is_off, memory_limit is not set but is implicitly enforced.
	 */

	if (start + size <= lmb_end_of_DRAM())
		return size;

	if (start >= lmb_end_of_DRAM())
		return 0;

	return lmb_end_of_DRAM() - start;
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
	const u32 *dm, *usm;
	unsigned int n, rc, ranges, is_kexec_kdump = 0;
	unsigned long lmb_size, base, size, sz;
	int nid;
	struct assoc_arrays aa;

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
				add_active_range(nid, base >> PAGE_SHIFT,
						 (base >> PAGE_SHIFT)
						 + (sz >> PAGE_SHIFT));
		} while (--ranges);
	}
}

static int __init parse_numa_properties(void)
{
	struct device_node *cpu = NULL;
	struct device_node *memory = NULL;
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
	memory = NULL;
	while ((memory = of_find_node_by_type(memory, "memory")) != NULL) {
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

		add_active_range(nid, start >> PAGE_SHIFT,
				(start >> PAGE_SHIFT) + (size >> PAGE_SHIFT));

		if (--ranges)
			goto new_range;
	}

	/*
	 * Now do the same thing for each LMB listed in the ibm,dynamic-memory
	 * property in the ibm,dynamic-reconfiguration-memory node.
	 */
	memory = of_find_node_by_path("/ibm,dynamic-reconfiguration-memory");
	if (memory)
		parse_drconf_memory(memory);

	return 0;
}

static void __init setup_nonnuma(void)
{
	unsigned long top_of_ram = lmb_end_of_DRAM();
	unsigned long total_ram = lmb_phys_mem_size();
	unsigned long start_pfn, end_pfn;
	unsigned int i, nid = 0;

	printk(KERN_DEBUG "Top of RAM: 0x%lx, Total RAM: 0x%lx\n",
	       top_of_ram, total_ram);
	printk(KERN_DEBUG "Memory hole size: %ldMB\n",
	       (top_of_ram - total_ram) >> 20);

	for (i = 0; i < lmb.memory.cnt; ++i) {
		start_pfn = lmb.memory.region[i].base >> PAGE_SHIFT;
		end_pfn = start_pfn + lmb_size_pages(&lmb.memory, i);

		fake_numa_create_new_node(end_pfn, &nid);
		add_active_range(nid, start_pfn, end_pfn);
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
		for (cpu = 0; cpu < NR_CPUS; cpu++) {
			if (cpu_isset(cpu, numa_cpumask_lookup_table[node])) {
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
			printk("-%u", NR_CPUS - 1);
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

		for (i = 0; i < lmb_end_of_DRAM();
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
 * Allocate some memory, satisfying the lmb or bootmem allocator where
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

	ret_paddr = __lmb_alloc_base(size, align, end_pfn << PAGE_SHIFT);

	/* retry over all memory */
	if (!ret_paddr)
		ret_paddr = __lmb_alloc_base(size, align, lmb_end_of_DRAM());

	if (!ret_paddr)
		panic("numa.c: cannot allocate %lu bytes for node %d",
		      size, nid);

	ret = __va(ret_paddr);

	/*
	 * We initialize the nodes in numeric order: 0, 1, 2...
	 * and hand over control from the LMB allocator to the
	 * bootmem allocator.  If this function is called for
	 * node 5, then we know that all nodes <5 are using the
	 * bootmem allocator instead of the LMB allocator.
	 *
	 * So, check the nid from which this allocation came
	 * and double check to see if we need to use bootmem
	 * instead of the LMB.  We don't free the LMB memory
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

static void mark_reserved_regions_for_nid(int nid)
{
	struct pglist_data *node = NODE_DATA(nid);
	int i;

	for (i = 0; i < lmb.reserved.cnt; i++) {
		unsigned long physbase = lmb.reserved.region[i].base;
		unsigned long size = lmb.reserved.region[i].size;
		unsigned long start_pfn = physbase >> PAGE_SHIFT;
		unsigned long end_pfn = PFN_UP(physbase + size);
		struct node_active_region node_ar;
		unsigned long node_end_pfn = node->node_start_pfn +
					     node->node_spanned_pages;

		/*
		 * Check to make sure that this lmb.reserved area is
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
	max_low_pfn = lmb_end_of_DRAM() >> PAGE_SHIFT;
	max_pfn = max_low_pfn;

	if (parse_numa_properties())
		setup_nonnuma();
	else
		dump_numa_memory_topology();

	register_cpu_notifier(&ppc64_numa_nb);
	cpu_numa_callback(&ppc64_numa_nb, CPU_UP_PREPARE,
			  (void *)(unsigned long)boot_cpuid);

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
}

void __init paging_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];
	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));
	max_zone_pfns[ZONE_DMA] = lmb_end_of_DRAM() >> PAGE_SHIFT;
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
 * each lmb.
 */
int hot_add_node_scn_to_nid(unsigned long scn_addr)
{
	struct device_node *memory = NULL;
	int nid = -1;

	while ((memory = of_find_node_by_type(memory, "memory")) != NULL) {
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

		of_node_put(memory);
		if (nid >= 0)
			break;
	}

	return nid;
}

/*
 * Find the node associated with a hot added memory section.  Section
 * corresponds to a SPARSEMEM section, not an LMB.  It is assumed that
 * sections are fully contained within a single LMB.
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

#endif /* CONFIG_MEMORY_HOTPLUG */
