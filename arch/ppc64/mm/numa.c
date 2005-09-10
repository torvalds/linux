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
#include <asm/lmb.h>
#include <asm/machdep.h>
#include <asm/abs_addr.h>

static int numa_enabled = 1;

static int numa_debug;
#define dbg(args...) if (numa_debug) { printk(KERN_INFO args); }

#ifdef DEBUG_NUMA
#define ARRAY_INITIALISER -1
#else
#define ARRAY_INITIALISER 0
#endif

int numa_cpu_lookup_table[NR_CPUS] = { [ 0 ... (NR_CPUS - 1)] =
	ARRAY_INITIALISER};
char *numa_memory_lookup_table;
cpumask_t numa_cpumask_lookup_table[MAX_NUMNODES];
int nr_cpus_in_node[MAX_NUMNODES] = { [0 ... (MAX_NUMNODES -1)] = 0};

struct pglist_data *node_data[MAX_NUMNODES];
bootmem_data_t __initdata plat_node_bdata[MAX_NUMNODES];
static int min_common_depth;

/*
 * We need somewhere to store start/span for each node until we have
 * allocated the real node_data structures.
 */
static struct {
	unsigned long node_start_pfn;
	unsigned long node_end_pfn;
	unsigned long node_present_pages;
} init_node_data[MAX_NUMNODES] __initdata;

EXPORT_SYMBOL(node_data);
EXPORT_SYMBOL(numa_cpu_lookup_table);
EXPORT_SYMBOL(numa_memory_lookup_table);
EXPORT_SYMBOL(numa_cpumask_lookup_table);
EXPORT_SYMBOL(nr_cpus_in_node);

static inline void map_cpu_to_node(int cpu, int node)
{
	numa_cpu_lookup_table[cpu] = node;
	if (!(cpu_isset(cpu, numa_cpumask_lookup_table[node]))) {
		cpu_set(cpu, numa_cpumask_lookup_table[node]);
		nr_cpus_in_node[node]++;
	}
}

#ifdef CONFIG_HOTPLUG_CPU
static void unmap_cpu_from_node(unsigned long cpu)
{
	int node = numa_cpu_lookup_table[cpu];

	dbg("removing cpu %lu from node %d\n", cpu, node);

	if (cpu_isset(cpu, numa_cpumask_lookup_table[node])) {
		cpu_clear(cpu, numa_cpumask_lookup_table[node]);
		nr_cpus_in_node[node]--;
	} else {
		printk(KERN_ERR "WARNING: cpu %lu not found in node %d\n",
		       cpu, node);
	}
}
#endif /* CONFIG_HOTPLUG_CPU */

static struct device_node * __devinit find_cpu_node(unsigned int cpu)
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

static int __init get_mem_addr_cells(void)
{
	struct device_node *memory = NULL;
	int rc;

	memory = of_find_node_by_type(memory, "memory");
	if (!memory)
		return 0; /* it won't matter */

	rc = prom_n_addr_cells(memory);
	return rc;
}

static int __init get_mem_size_cells(void)
{
	struct device_node *memory = NULL;
	int rc;

	memory = of_find_node_by_type(memory, "memory");
	if (!memory)
		return 0; /* it won't matter */
	rc = prom_n_size_cells(memory);
	return rc;
}

static unsigned long read_n_cells(int n, unsigned int **buf)
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
static unsigned long __init numa_enforce_memory_limit(unsigned long start, unsigned long size)
{
	/*
	 * We use lmb_end_of_DRAM() in here instead of memory_limit because
	 * we've already adjusted it for the limit and it takes care of
	 * having memory holes below the limit.
	 */
	extern unsigned long memory_limit;

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
	int addr_cells, size_cells;
	int max_domain = 0;
	long entries = lmb_end_of_DRAM() >> MEMORY_INCREMENT_SHIFT;
	unsigned long i;

	if (numa_enabled == 0) {
		printk(KERN_WARNING "NUMA disabled by user\n");
		return -1;
	}

	numa_memory_lookup_table =
		(char *)abs_to_virt(lmb_alloc(entries * sizeof(char), 1));
	memset(numa_memory_lookup_table, 0, entries * sizeof(char));

	for (i = 0; i < entries ; i++)
		numa_memory_lookup_table[i] = ARRAY_INITIALISER;

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

	addr_cells = get_mem_addr_cells();
	size_cells = get_mem_size_cells();
	memory = NULL;
	while ((memory = of_find_node_by_type(memory, "memory")) != NULL) {
		unsigned long start;
		unsigned long size;
		int numa_domain;
		int ranges;
		unsigned int *memcell_buf;
		unsigned int len;

		memcell_buf = (unsigned int *)get_property(memory, "reg", &len);
		if (!memcell_buf || len <= 0)
			continue;

		ranges = memory->n_addrs;
new_range:
		/* these are order-sensitive, and modify the buffer pointer */
		start = read_n_cells(addr_cells, &memcell_buf);
		size = read_n_cells(size_cells, &memcell_buf);

		start = _ALIGN_DOWN(start, MEMORY_INCREMENT);
		size = _ALIGN_UP(size, MEMORY_INCREMENT);

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

		if (! (size = numa_enforce_memory_limit(start, size))) {
			if (--ranges)
				goto new_range;
			else
				continue;
		}

		/*
		 * Initialize new node struct, or add to an existing one.
		 */
		if (init_node_data[numa_domain].node_end_pfn) {
			if ((start / PAGE_SIZE) <
			    init_node_data[numa_domain].node_start_pfn)
				init_node_data[numa_domain].node_start_pfn =
					start / PAGE_SIZE;
			if (((start / PAGE_SIZE) + (size / PAGE_SIZE)) >
			    init_node_data[numa_domain].node_end_pfn)
				init_node_data[numa_domain].node_end_pfn =
					(start / PAGE_SIZE) +
					(size / PAGE_SIZE);

			init_node_data[numa_domain].node_present_pages +=
				size / PAGE_SIZE;
		} else {
			node_set_online(numa_domain);

			init_node_data[numa_domain].node_start_pfn =
				start / PAGE_SIZE;
			init_node_data[numa_domain].node_end_pfn =
				init_node_data[numa_domain].node_start_pfn +
				size / PAGE_SIZE;
			init_node_data[numa_domain].node_present_pages =
				size / PAGE_SIZE;
		}

		for (i = start ; i < (start+size); i += MEMORY_INCREMENT)
			numa_memory_lookup_table[i >> MEMORY_INCREMENT_SHIFT] =
				numa_domain;

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
	unsigned long i;

	printk(KERN_INFO "Top of RAM: 0x%lx, Total RAM: 0x%lx\n",
	       top_of_ram, total_ram);
	printk(KERN_INFO "Memory hole size: %ldMB\n",
	       (top_of_ram - total_ram) >> 20);

	if (!numa_memory_lookup_table) {
		long entries = top_of_ram >> MEMORY_INCREMENT_SHIFT;
		numa_memory_lookup_table =
			(char *)abs_to_virt(lmb_alloc(entries * sizeof(char), 1));
		memset(numa_memory_lookup_table, 0, entries * sizeof(char));
		for (i = 0; i < entries ; i++)
			numa_memory_lookup_table[i] = ARRAY_INITIALISER;
	}

	map_cpu_to_node(boot_cpuid, 0);

	node_set_online(0);

	init_node_data[0].node_start_pfn = 0;
	init_node_data[0].node_end_pfn = lmb_end_of_DRAM() / PAGE_SIZE;
	init_node_data[0].node_present_pages = total_ram / PAGE_SIZE;

	for (i = 0 ; i < top_of_ram; i += MEMORY_INCREMENT)
		numa_memory_lookup_table[i >> MEMORY_INCREMENT_SHIFT] = 0;
}

static void __init dump_numa_topology(void)
{
	unsigned int node;
	unsigned int count;

	if (min_common_depth == -1 || !numa_enabled)
		return;

	for_each_online_node(node) {
		unsigned long i;

		printk(KERN_INFO "Node %d Memory:", node);

		count = 0;

		for (i = 0; i < lmb_end_of_DRAM(); i += MEMORY_INCREMENT) {
			if (numa_memory_lookup_table[i >> MEMORY_INCREMENT_SHIFT] == node) {
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
	return;
}

/*
 * Allocate some memory, satisfying the lmb or bootmem allocator where
 * required. nid is the preferred node and end is the physical address of
 * the highest address in the node.
 *
 * Returns the physical address of the memory.
 */
static unsigned long careful_allocation(int nid, unsigned long size,
					unsigned long align, unsigned long end)
{
	unsigned long ret = lmb_alloc_base(size, align, end);

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
	if (pa_to_nid(ret) < nid) {
		nid = pa_to_nid(ret);
		ret = (unsigned long)__alloc_bootmem_node(NODE_DATA(nid),
				size, align, 0);

		if (!ret)
			panic("numa.c: cannot allocate %lu bytes on node %d",
			      size, nid);

		ret = virt_to_abs(ret);

		dbg("alloc_bootmem %lx %lx\n", ret, size);
	}

	return ret;
}

void __init do_init_bootmem(void)
{
	int nid;
	int addr_cells, size_cells;
	struct device_node *memory = NULL;
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
		dump_numa_topology();

	register_cpu_notifier(&ppc64_numa_nb);

	for_each_online_node(nid) {
		unsigned long start_paddr, end_paddr;
		int i;
		unsigned long bootmem_paddr;
		unsigned long bootmap_pages;

		start_paddr = init_node_data[nid].node_start_pfn * PAGE_SIZE;
		end_paddr = init_node_data[nid].node_end_pfn * PAGE_SIZE;

		/* Allocate the node structure node local if possible */
		NODE_DATA(nid) = (struct pglist_data *)careful_allocation(nid,
					sizeof(struct pglist_data),
					SMP_CACHE_BYTES, end_paddr);
		NODE_DATA(nid) = abs_to_virt(NODE_DATA(nid));
		memset(NODE_DATA(nid), 0, sizeof(struct pglist_data));

  		dbg("node %d\n", nid);
		dbg("NODE_DATA() = %p\n", NODE_DATA(nid));

		NODE_DATA(nid)->bdata = &plat_node_bdata[nid];
		NODE_DATA(nid)->node_start_pfn =
			init_node_data[nid].node_start_pfn;
		NODE_DATA(nid)->node_spanned_pages =
			end_paddr - start_paddr;

		if (NODE_DATA(nid)->node_spanned_pages == 0)
  			continue;

  		dbg("start_paddr = %lx\n", start_paddr);
  		dbg("end_paddr = %lx\n", end_paddr);

		bootmap_pages = bootmem_bootmap_pages((end_paddr - start_paddr) >> PAGE_SHIFT);

		bootmem_paddr = careful_allocation(nid,
				bootmap_pages << PAGE_SHIFT,
				PAGE_SIZE, end_paddr);
		memset(abs_to_virt(bootmem_paddr), 0,
		       bootmap_pages << PAGE_SHIFT);
		dbg("bootmap_paddr = %lx\n", bootmem_paddr);

		init_bootmem_node(NODE_DATA(nid), bootmem_paddr >> PAGE_SHIFT,
				  start_paddr >> PAGE_SHIFT,
				  end_paddr >> PAGE_SHIFT);

		/*
		 * We need to do another scan of all memory sections to
		 * associate memory with the correct node.
		 */
		addr_cells = get_mem_addr_cells();
		size_cells = get_mem_size_cells();
		memory = NULL;
		while ((memory = of_find_node_by_type(memory, "memory")) != NULL) {
			unsigned long mem_start, mem_size;
			int numa_domain, ranges;
			unsigned int *memcell_buf;
			unsigned int len;

			memcell_buf = (unsigned int *)get_property(memory, "reg", &len);
			if (!memcell_buf || len <= 0)
				continue;

			ranges = memory->n_addrs;	/* ranges in cell */
new_range:
			mem_start = read_n_cells(addr_cells, &memcell_buf);
			mem_size = read_n_cells(size_cells, &memcell_buf);
			if (numa_enabled) {
				numa_domain = of_node_numa_domain(memory);
				if (numa_domain  >= MAX_NUMNODES)
					numa_domain = 0;
			} else
				numa_domain =  0;

			if (numa_domain != nid)
				continue;

			mem_size = numa_enforce_memory_limit(mem_start, mem_size);
  			if (mem_size) {
  				dbg("free_bootmem %lx %lx\n", mem_start, mem_size);
  				free_bootmem_node(NODE_DATA(nid), mem_start, mem_size);
			}

			if (--ranges)		/* process all ranges in cell */
				goto new_range;
		}

		/*
		 * Mark reserved regions on this node
		 */
		for (i = 0; i < lmb.reserved.cnt; i++) {
			unsigned long physbase = lmb.reserved.region[i].base;
			unsigned long size = lmb.reserved.region[i].size;

			if (pa_to_nid(physbase) != nid &&
			    pa_to_nid(physbase+size-1) != nid)
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
		/*
		 * This loop may look famaliar, but we have to do it again
		 * after marking our reserved memory to mark memory present
		 * for sparsemem.
		 */
		addr_cells = get_mem_addr_cells();
		size_cells = get_mem_size_cells();
		memory = NULL;
		while ((memory = of_find_node_by_type(memory, "memory")) != NULL) {
			unsigned long mem_start, mem_size;
			int numa_domain, ranges;
			unsigned int *memcell_buf;
			unsigned int len;

			memcell_buf = (unsigned int *)get_property(memory, "reg", &len);
			if (!memcell_buf || len <= 0)
				continue;

			ranges = memory->n_addrs;	/* ranges in cell */
new_range2:
			mem_start = read_n_cells(addr_cells, &memcell_buf);
			mem_size = read_n_cells(size_cells, &memcell_buf);
			if (numa_enabled) {
				numa_domain = of_node_numa_domain(memory);
				if (numa_domain  >= MAX_NUMNODES)
					numa_domain = 0;
			} else
				numa_domain =  0;

			if (numa_domain != nid)
				continue;

			mem_size = numa_enforce_memory_limit(mem_start, mem_size);
			memory_present(numa_domain, mem_start >> PAGE_SHIFT,
				       (mem_start + mem_size) >> PAGE_SHIFT);

			if (--ranges)		/* process all ranges in cell */
				goto new_range2;
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
		unsigned long start_pfn;
		unsigned long end_pfn;

		start_pfn = init_node_data[nid].node_start_pfn;
		end_pfn = init_node_data[nid].node_end_pfn;

		zones_size[ZONE_DMA] = end_pfn - start_pfn;
		zholes_size[ZONE_DMA] = zones_size[ZONE_DMA] -
			init_node_data[nid].node_present_pages;

		dbg("free_area_init node %d %lx %lx (hole: %lx)\n", nid,
		    zones_size[ZONE_DMA], start_pfn, zholes_size[ZONE_DMA]);

		free_area_init_node(nid, NODE_DATA(nid), zones_size,
							start_pfn, zholes_size);
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
