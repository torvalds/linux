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
#include <asm/sparsemem.h>
#include <asm/lmb.h>
#include <asm/system.h>
#include <asm/smp.h>

static int numa_enabled = 1;

static int numa_debug;
#define dbg(args...) if (numa_debug) { printk(KERN_INFO args); }

int numa_cpu_lookup_table[NR_CPUS];
cpumask_t numa_cpumask_lookup_table[MAX_NUMNODES];
struct pglist_data *node_data[MAX_NUMNODES];

EXPORT_SYMBOL(numa_cpu_lookup_table);
EXPORT_SYMBOL(numa_cpumask_lookup_table);
EXPORT_SYMBOL(node_data);

static bootmem_data_t __initdata plat_node_bdata[MAX_NUMNODES];
static int min_common_depth;
static int n_mem_addr_cells, n_mem_size_cells;

/*
 * We need somewhere to store start/end/node for each region until we have
 * allocated the real node_data structures.
 */
#define MAX_REGIONS	(MAX_LMB_REGIONS*2)
static struct {
	unsigned long start_pfn;
	unsigned long end_pfn;
	int nid;
} init_node_data[MAX_REGIONS] __initdata;

int __init early_pfn_to_nid(unsigned long pfn)
{
	unsigned int i;

	for (i = 0; init_node_data[i].end_pfn; i++) {
		unsigned long start_pfn = init_node_data[i].start_pfn;
		unsigned long end_pfn = init_node_data[i].end_pfn;

		if ((start_pfn <= pfn) && (pfn < end_pfn))
			return init_node_data[i].nid;
	}

	return -1;
}

void __init add_region(unsigned int nid, unsigned long start_pfn,
		       unsigned long pages)
{
	unsigned int i;

	dbg("add_region nid %d start_pfn 0x%lx pages 0x%lx\n",
		nid, start_pfn, pages);

	for (i = 0; init_node_data[i].end_pfn; i++) {
		if (init_node_data[i].nid != nid)
			continue;
		if (init_node_data[i].end_pfn == start_pfn) {
			init_node_data[i].end_pfn += pages;
			return;
		}
		if (init_node_data[i].start_pfn == (start_pfn + pages)) {
			init_node_data[i].start_pfn -= pages;
			return;
		}
	}

	/*
	 * Leave last entry NULL so we dont iterate off the end (we use
	 * entry.end_pfn to terminate the walk).
	 */
	if (i >= (MAX_REGIONS - 1)) {
		printk(KERN_ERR "WARNING: too many memory regions in "
				"numa code, truncating\n");
		return;
	}

	init_node_data[i].start_pfn = start_pfn;
	init_node_data[i].end_pfn = start_pfn + pages;
	init_node_data[i].nid = nid;
}

/* We assume init_node_data has no overlapping regions */
void __init get_region(unsigned int nid, unsigned long *start_pfn,
		       unsigned long *end_pfn, unsigned long *pages_present)
{
	unsigned int i;

	*start_pfn = -1UL;
	*end_pfn = *pages_present = 0;

	for (i = 0; init_node_data[i].end_pfn; i++) {
		if (init_node_data[i].nid != nid)
			continue;

		*pages_present += init_node_data[i].end_pfn -
			init_node_data[i].start_pfn;

		if (init_node_data[i].start_pfn < *start_pfn)
			*start_pfn = init_node_data[i].start_pfn;

		if (init_node_data[i].end_pfn > *end_pfn)
			*end_pfn = init_node_data[i].end_pfn;
	}

	/* We didnt find a matching region, return start/end as 0 */
	if (*start_pfn == -1UL)
		*start_pfn = 0;
}

static inline void map_cpu_to_node(int cpu, int node)
{
	numa_cpu_lookup_table[cpu] = node;

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

static struct device_node *find_cpu_node(unsigned int cpu)
{
	unsigned int hw_cpuid = get_hard_smp_processor_id(cpu);
	struct device_node *cpu_node = NULL;
	unsigned int *interrupt_server, *reg;
	int len;

	while ((cpu_node = of_find_node_by_type(cpu_node, "cpu")) != NULL) {
		/* Try interrupt server first */
		interrupt_server = (unsigned int *)get_property(cpu_node,
					"ibm,ppc-interrupt-server#s", &len);

		len = len / sizeof(u32);

		if (interrupt_server && (len > 0)) {
			while (len--) {
				if (interrupt_server[len] == hw_cpuid)
					return cpu_node;
			}
		} else {
			reg = (unsigned int *)get_property(cpu_node,
							   "reg", &len);
			if (reg && (len > 0) && (reg[0] == hw_cpuid))
				return cpu_node;
		}
	}

	return NULL;
}

/* must hold reference to node during call */
static int *of_get_associativity(struct device_node *dev)
{
	return (unsigned int *)get_property(dev, "ibm,associativity", NULL);
}

static int of_node_numa_domain(struct device_node *device)
{
	int numa_domain;
	unsigned int *tmp;

	if (min_common_depth == -1)
		return 0;

	tmp = of_get_associativity(device);
	if (tmp && (tmp[0] >= min_common_depth)) {
		numa_domain = tmp[min_common_depth];
	} else {
		dbg("WARNING: no NUMA information for %s\n",
		    device->full_name);
		numa_domain = 0;
	}
	return numa_domain;
}

/*
 * In theory, the "ibm,associativity" property may contain multiple
 * associativity lists because a resource may be multiply connected
 * into the machine.  This resource then has different associativity
 * characteristics relative to its multiple connections.  We ignore
 * this for now.  We also assume that all cpu and memory sets have
 * their distances represented at a common level.  This won't be
 * true for heirarchical NUMA.
 *
 * In any case the ibm,associativity-reference-points should give
 * the correct depth for a normal NUMA system.
 *
 * - Dave Hansen <haveblue@us.ibm.com>
 */
static int __init find_min_common_depth(void)
{
	int depth;
	unsigned int *ref_points;
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
	ref_points = (unsigned int *)get_property(rtas_root,
			"ibm,associativity-reference-points", &len);

	if ((len >= 1) && ref_points) {
		depth = ref_points[1];
	} else {
		dbg("WARNING: could not find NUMA "
		    "associativity reference point\n");
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

	*n_addr_cells = prom_n_addr_cells(memory);
	*n_size_cells = prom_n_size_cells(memory);
	of_node_put(memory);
}

static unsigned long __devinit read_n_cells(int n, unsigned int **buf)
{
	unsigned long result = 0;

	while (n--) {
		result = (result << 32) | **buf;
		(*buf)++;
	}
	return result;
}

/*
 * Figure out to which domain a cpu belongs and stick it there.
 * Return the id of the domain used.
 */
static int numa_setup_cpu(unsigned long lcpu)
{
	int numa_domain = 0;
	struct device_node *cpu = find_cpu_node(lcpu);

	if (!cpu) {
		WARN_ON(1);
		goto out;
	}

	numa_domain = of_node_numa_domain(cpu);

	if (numa_domain >= num_online_nodes()) {
		/*
		 * POWER4 LPAR uses 0xffff as invalid node,
		 * dont warn in this case.
		 */
		if (numa_domain != 0xffff)
			printk(KERN_ERR "WARNING: cpu %ld "
			       "maps to invalid NUMA node %d\n",
			       lcpu, numa_domain);
		numa_domain = 0;
	}
out:
	node_set_online(numa_domain);

	map_cpu_to_node(lcpu, numa_domain);

	of_node_put(cpu);

	return numa_domain;
}

static int cpu_numa_callback(struct notifier_block *nfb,
			     unsigned long action,
			     void *hcpu)
{
	unsigned long lcpu = (unsigned long)hcpu;
	int ret = NOTIFY_DONE;

	switch (action) {
	case CPU_UP_PREPARE:
		if (min_common_depth == -1 || !numa_enabled)
			map_cpu_to_node(lcpu, 0);
		else
			numa_setup_cpu(lcpu);
		ret = NOTIFY_OK;
		break;
#ifdef CONFIG_HOTPLUG_CPU
	case CPU_DEAD:
	case CPU_UP_CANCELED:
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
	 * having memory holes below the limit.
	 */

	if (! memory_limit)
		return size;

	if (start + size <= lmb_end_of_DRAM())
		return size;

	if (start >= lmb_end_of_DRAM())
		return 0;

	return lmb_end_of_DRAM() - start;
}

static int __init parse_numa_properties(void)
{
	struct device_node *cpu = NULL;
	struct device_node *memory = NULL;
	int max_domain;
	unsigned long i;

	if (numa_enabled == 0) {
		printk(KERN_WARNING "NUMA disabled by user\n");
		return -1;
	}

	min_common_depth = find_min_common_depth();

	dbg("NUMA associativity depth for CPU/Memory: %d\n", min_common_depth);
	if (min_common_depth < 0)
		return min_common_depth;

	max_domain = numa_setup_cpu(boot_cpuid);

	/*
	 * Even though we connect cpus to numa domains later in SMP init,
	 * we need to know the maximum node id now. This is because each
	 * node id must have NODE_DATA etc backing it.
	 * As a result of hotplug we could still have cpus appear later on
	 * with larger node ids. In that case we force the cpu into node 0.
	 */
	for_each_cpu(i) {
		int numa_domain;

		cpu = find_cpu_node(i);

		if (cpu) {
			numa_domain = of_node_numa_domain(cpu);
			of_node_put(cpu);

			if (numa_domain < MAX_NUMNODES &&
			    max_domain < numa_domain)
				max_domain = numa_domain;
		}
	}

	get_n_mem_cells(&n_mem_addr_cells, &n_mem_size_cells);
	memory = NULL;
	while ((memory = of_find_node_by_type(memory, "memory")) != NULL) {
		unsigned long start;
		unsigned long size;
		int numa_domain;
		int ranges;
		unsigned int *memcell_buf;
		unsigned int len;

		memcell_buf = (unsigned int *)get_property(memory,
			"linux,usable-memory", &len);
		if (!memcell_buf || len <= 0)
			memcell_buf =
				(unsigned int *)get_property(memory, "reg",
					&len);
		if (!memcell_buf || len <= 0)
			continue;

		/* ranges in cell */
		ranges = (len >> 2) / (n_mem_addr_cells + n_mem_size_cells);
new_range:
		/* these are order-sensitive, and modify the buffer pointer */
		start = read_n_cells(n_mem_addr_cells, &memcell_buf);
		size = read_n_cells(n_mem_size_cells, &memcell_buf);

		numa_domain = of_node_numa_domain(memory);

		if (numa_domain >= MAX_NUMNODES) {
			if (numa_domain != 0xffff)
				printk(KERN_ERR "WARNING: memory at %lx maps "
				       "to invalid NUMA node %d\n", start,
				       numa_domain);
			numa_domain = 0;
		}

		if (max_domain < numa_domain)
			max_domain = numa_domain;

		if (!(size = numa_enforce_memory_limit(start, size))) {
			if (--ranges)
				goto new_range;
			else
				continue;
		}

		add_region(numa_domain, start >> PAGE_SHIFT,
			   size >> PAGE_SHIFT);

		if (--ranges)
			goto new_range;
	}

	for (i = 0; i <= max_domain; i++)
		node_set_online(i);

	return 0;
}

static void __init setup_nonnuma(void)
{
	unsigned long top_of_ram = lmb_end_of_DRAM();
	unsigned long total_ram = lmb_phys_mem_size();
	unsigned int i;

	printk(KERN_INFO "Top of RAM: 0x%lx, Total RAM: 0x%lx\n",
	       top_of_ram, total_ram);
	printk(KERN_INFO "Memory hole size: %ldMB\n",
	       (top_of_ram - total_ram) >> 20);

	map_cpu_to_node(boot_cpuid, 0);
	for (i = 0; i < lmb.memory.cnt; ++i)
		add_region(0, lmb.memory.region[i].base >> PAGE_SHIFT,
			   lmb_size_pages(&lmb.memory, i));
	node_set_online(0);
}

void __init dump_numa_cpu_topology(void)
{
	unsigned int node;
	unsigned int cpu, count;

	if (min_common_depth == -1 || !numa_enabled)
		return;

	for_each_online_node(node) {
		printk(KERN_INFO "Node %d CPUs:", node);

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

		printk(KERN_INFO "Node %d Memory:", node);

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
 * Returns the physical address of the memory.
 */
static void __init *careful_allocation(int nid, unsigned long size,
				       unsigned long align,
				       unsigned long end_pfn)
{
	int new_nid;
	unsigned long ret = lmb_alloc_base(size, align, end_pfn << PAGE_SHIFT);

	/* retry over all memory */
	if (!ret)
		ret = lmb_alloc_base(size, align, lmb_end_of_DRAM());

	if (!ret)
		panic("numa.c: cannot allocate %lu bytes on node %d",
		      size, nid);

	/*
	 * If the memory came from a previously allocated node, we must
	 * retry with the bootmem allocator.
	 */
	new_nid = early_pfn_to_nid(ret >> PAGE_SHIFT);
	if (new_nid < nid) {
		ret = (unsigned long)__alloc_bootmem_node(NODE_DATA(new_nid),
				size, align, 0);

		if (!ret)
			panic("numa.c: cannot allocate %lu bytes on node %d",
			      size, new_nid);

		ret = __pa(ret);

		dbg("alloc_bootmem %lx %lx\n", ret, size);
	}

	return (void *)ret;
}

void __init do_init_bootmem(void)
{
	int nid;
	unsigned int i;
	static struct notifier_block ppc64_numa_nb = {
		.notifier_call = cpu_numa_callback,
		.priority = 1 /* Must run before sched domains notifier. */
	};

	min_low_pfn = 0;
	max_low_pfn = lmb_end_of_DRAM() >> PAGE_SHIFT;
	max_pfn = max_low_pfn;

	if (parse_numa_properties())
		setup_nonnuma();
	else
		dump_numa_memory_topology();

	register_cpu_notifier(&ppc64_numa_nb);

	for_each_online_node(nid) {
		unsigned long start_pfn, end_pfn, pages_present;
		unsigned long bootmem_paddr;
		unsigned long bootmap_pages;

		get_region(nid, &start_pfn, &end_pfn, &pages_present);

		/* Allocate the node structure node local if possible */
		NODE_DATA(nid) = careful_allocation(nid,
					sizeof(struct pglist_data),
					SMP_CACHE_BYTES, end_pfn);
		NODE_DATA(nid) = __va(NODE_DATA(nid));
		memset(NODE_DATA(nid), 0, sizeof(struct pglist_data));

  		dbg("node %d\n", nid);
		dbg("NODE_DATA() = %p\n", NODE_DATA(nid));

		NODE_DATA(nid)->bdata = &plat_node_bdata[nid];
		NODE_DATA(nid)->node_start_pfn = start_pfn;
		NODE_DATA(nid)->node_spanned_pages = end_pfn - start_pfn;

		if (NODE_DATA(nid)->node_spanned_pages == 0)
  			continue;

  		dbg("start_paddr = %lx\n", start_pfn << PAGE_SHIFT);
  		dbg("end_paddr = %lx\n", end_pfn << PAGE_SHIFT);

		bootmap_pages = bootmem_bootmap_pages(end_pfn - start_pfn);
		bootmem_paddr = (unsigned long)careful_allocation(nid,
					bootmap_pages << PAGE_SHIFT,
					PAGE_SIZE, end_pfn);
		memset(__va(bootmem_paddr), 0, bootmap_pages << PAGE_SHIFT);

		dbg("bootmap_paddr = %lx\n", bootmem_paddr);

		init_bootmem_node(NODE_DATA(nid), bootmem_paddr >> PAGE_SHIFT,
				  start_pfn, end_pfn);

		/* Add free regions on this node */
		for (i = 0; init_node_data[i].end_pfn; i++) {
			unsigned long start, end;

			if (init_node_data[i].nid != nid)
				continue;

			start = init_node_data[i].start_pfn << PAGE_SHIFT;
			end = init_node_data[i].end_pfn << PAGE_SHIFT;

			dbg("free_bootmem %lx %lx\n", start, end - start);
  			free_bootmem_node(NODE_DATA(nid), start, end - start);
		}

		/* Mark reserved regions on this node */
		for (i = 0; i < lmb.reserved.cnt; i++) {
			unsigned long physbase = lmb.reserved.region[i].base;
			unsigned long size = lmb.reserved.region[i].size;
			unsigned long start_paddr = start_pfn << PAGE_SHIFT;
			unsigned long end_paddr = end_pfn << PAGE_SHIFT;

			if (early_pfn_to_nid(physbase >> PAGE_SHIFT) != nid &&
			    early_pfn_to_nid((physbase+size-1) >> PAGE_SHIFT) != nid)
				continue;

			if (physbase < end_paddr &&
			    (physbase+size) > start_paddr) {
				/* overlaps */
				if (physbase < start_paddr) {
					size -= start_paddr - physbase;
					physbase = start_paddr;
				}

				if (size > end_paddr - physbase)
					size = end_paddr - physbase;

				dbg("reserve_bootmem %lx %lx\n", physbase,
				    size);
				reserve_bootmem_node(NODE_DATA(nid), physbase,
						     size);
			}
		}

		/* Add regions into sparsemem */
		for (i = 0; init_node_data[i].end_pfn; i++) {
			unsigned long start, end;

			if (init_node_data[i].nid != nid)
				continue;

			start = init_node_data[i].start_pfn;
			end = init_node_data[i].end_pfn;

			memory_present(nid, start, end);
		}
	}
}

void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES];
	unsigned long zholes_size[MAX_NR_ZONES];
	int nid;

	memset(zones_size, 0, sizeof(zones_size));
	memset(zholes_size, 0, sizeof(zholes_size));

	for_each_online_node(nid) {
		unsigned long start_pfn, end_pfn, pages_present;

		get_region(nid, &start_pfn, &end_pfn, &pages_present);

		zones_size[ZONE_DMA] = end_pfn - start_pfn;
		zholes_size[ZONE_DMA] = zones_size[ZONE_DMA] - pages_present;

		dbg("free_area_init node %d %lx %lx (hole: %lx)\n", nid,
		    zones_size[ZONE_DMA], start_pfn, zholes_size[ZONE_DMA]);

		free_area_init_node(nid, NODE_DATA(nid), zones_size, start_pfn,
				    zholes_size);
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

	return 0;
}
early_param("numa", early_numa);

#ifdef CONFIG_MEMORY_HOTPLUG
/*
 * Find the node associated with a hot added memory section.  Section
 * corresponds to a SPARSEMEM section, not an LMB.  It is assumed that
 * sections are fully contained within a single LMB.
 */
int hot_add_scn_to_nid(unsigned long scn_addr)
{
	struct device_node *memory = NULL;
	nodemask_t nodes;
	int numa_domain = 0;

	if (!numa_enabled || (min_common_depth < 0))
		return numa_domain;

	while ((memory = of_find_node_by_type(memory, "memory")) != NULL) {
		unsigned long start, size;
		int ranges;
		unsigned int *memcell_buf;
		unsigned int len;

		memcell_buf = (unsigned int *)get_property(memory, "reg", &len);
		if (!memcell_buf || len <= 0)
			continue;

		/* ranges in cell */
		ranges = (len >> 2) / (n_mem_addr_cells + n_mem_size_cells);
ha_new_range:
		start = read_n_cells(n_mem_addr_cells, &memcell_buf);
		size = read_n_cells(n_mem_size_cells, &memcell_buf);
		numa_domain = of_node_numa_domain(memory);

		/* Domains not present at boot default to 0 */
		if (!node_online(numa_domain))
			numa_domain = any_online_node(NODE_MASK_ALL);

		if ((scn_addr >= start) && (scn_addr < (start + size))) {
			of_node_put(memory);
			goto got_numa_domain;
		}

		if (--ranges)		/* process all ranges in cell */
			goto ha_new_range;
	}
	BUG();	/* section address should be found above */

	/* Temporary code to ensure that returned node is not empty */
got_numa_domain:
	nodes_setall(nodes);
	while (NODE_DATA(numa_domain)->node_spanned_pages == 0) {
		node_clear(numa_domain, nodes);
		numa_domain = any_online_node(nodes);
	}
	return numa_domain;
}
#endif /* CONFIG_MEMORY_HOTPLUG */
