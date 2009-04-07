/*
 * Generic VM initialization for x86-64 NUMA setups.
 * Copyright 2002,2003 Andi Kleen, SuSE Labs.
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mmzone.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/nodemask.h>
#include <linux/sched.h>

#include <asm/e820.h>
#include <asm/proto.h>
#include <asm/dma.h>
#include <asm/numa.h>
#include <asm/acpi.h>
#include <asm/k8.h>

struct pglist_data *node_data[MAX_NUMNODES] __read_mostly;
EXPORT_SYMBOL(node_data);

struct memnode memnode;

s16 apicid_to_node[MAX_LOCAL_APIC] __cpuinitdata = {
	[0 ... MAX_LOCAL_APIC-1] = NUMA_NO_NODE
};

int numa_off __initdata;
static unsigned long __initdata nodemap_addr;
static unsigned long __initdata nodemap_size;

DEFINE_PER_CPU(int, node_number) = 0;
EXPORT_PER_CPU_SYMBOL(node_number);

/*
 * Map cpu index to node index
 */
DEFINE_EARLY_PER_CPU(int, x86_cpu_to_node_map, NUMA_NO_NODE);
EXPORT_EARLY_PER_CPU_SYMBOL(x86_cpu_to_node_map);

/*
 * Given a shift value, try to populate memnodemap[]
 * Returns :
 * 1 if OK
 * 0 if memnodmap[] too small (of shift too small)
 * -1 if node overlap or lost ram (shift too big)
 */
static int __init populate_memnodemap(const struct bootnode *nodes,
				      int numnodes, int shift, int *nodeids)
{
	unsigned long addr, end;
	int i, res = -1;

	memset(memnodemap, 0xff, sizeof(s16)*memnodemapsize);
	for (i = 0; i < numnodes; i++) {
		addr = nodes[i].start;
		end = nodes[i].end;
		if (addr >= end)
			continue;
		if ((end >> shift) >= memnodemapsize)
			return 0;
		do {
			if (memnodemap[addr >> shift] != NUMA_NO_NODE)
				return -1;

			if (!nodeids)
				memnodemap[addr >> shift] = i;
			else
				memnodemap[addr >> shift] = nodeids[i];

			addr += (1UL << shift);
		} while (addr < end);
		res = 1;
	}
	return res;
}

static int __init allocate_cachealigned_memnodemap(void)
{
	unsigned long addr;

	memnodemap = memnode.embedded_map;
	if (memnodemapsize <= ARRAY_SIZE(memnode.embedded_map))
		return 0;

	addr = 0x8000;
	nodemap_size = roundup(sizeof(s16) * memnodemapsize, L1_CACHE_BYTES);
	nodemap_addr = find_e820_area(addr, max_pfn<<PAGE_SHIFT,
				      nodemap_size, L1_CACHE_BYTES);
	if (nodemap_addr == -1UL) {
		printk(KERN_ERR
		       "NUMA: Unable to allocate Memory to Node hash map\n");
		nodemap_addr = nodemap_size = 0;
		return -1;
	}
	memnodemap = phys_to_virt(nodemap_addr);
	reserve_early(nodemap_addr, nodemap_addr + nodemap_size, "MEMNODEMAP");

	printk(KERN_DEBUG "NUMA: Allocated memnodemap from %lx - %lx\n",
	       nodemap_addr, nodemap_addr + nodemap_size);
	return 0;
}

/*
 * The LSB of all start and end addresses in the node map is the value of the
 * maximum possible shift.
 */
static int __init extract_lsb_from_nodes(const struct bootnode *nodes,
					 int numnodes)
{
	int i, nodes_used = 0;
	unsigned long start, end;
	unsigned long bitfield = 0, memtop = 0;

	for (i = 0; i < numnodes; i++) {
		start = nodes[i].start;
		end = nodes[i].end;
		if (start >= end)
			continue;
		bitfield |= start;
		nodes_used++;
		if (end > memtop)
			memtop = end;
	}
	if (nodes_used <= 1)
		i = 63;
	else
		i = find_first_bit(&bitfield, sizeof(unsigned long)*8);
	memnodemapsize = (memtop >> i)+1;
	return i;
}

int __init compute_hash_shift(struct bootnode *nodes, int numnodes,
			      int *nodeids)
{
	int shift;

	shift = extract_lsb_from_nodes(nodes, numnodes);
	if (allocate_cachealigned_memnodemap())
		return -1;
	printk(KERN_DEBUG "NUMA: Using %d for the hash shift.\n",
		shift);

	if (populate_memnodemap(nodes, numnodes, shift, nodeids) != 1) {
		printk(KERN_INFO "Your memory is not aligned you need to "
		       "rebuild your kernel with a bigger NODEMAPSIZE "
		       "shift=%d\n", shift);
		return -1;
	}
	return shift;
}

int __meminit  __early_pfn_to_nid(unsigned long pfn)
{
	return phys_to_nid(pfn << PAGE_SHIFT);
}

static void * __init early_node_mem(int nodeid, unsigned long start,
				    unsigned long end, unsigned long size,
				    unsigned long align)
{
	unsigned long mem = find_e820_area(start, end, size, align);
	void *ptr;

	if (mem != -1L)
		return __va(mem);

	ptr = __alloc_bootmem_nopanic(size, align, __pa(MAX_DMA_ADDRESS));
	if (ptr == NULL) {
		printk(KERN_ERR "Cannot find %lu bytes in node %d\n",
		       size, nodeid);
		return NULL;
	}
	return ptr;
}

/* Initialize bootmem allocator for a node */
void __init setup_node_bootmem(int nodeid, unsigned long start,
			       unsigned long end)
{
	unsigned long start_pfn, last_pfn, bootmap_pages, bootmap_size;
	unsigned long bootmap_start, nodedata_phys;
	void *bootmap;
	const int pgdat_size = roundup(sizeof(pg_data_t), PAGE_SIZE);
	int nid;

	start = roundup(start, ZONE_ALIGN);

	printk(KERN_INFO "Bootmem setup node %d %016lx-%016lx\n", nodeid,
	       start, end);

	start_pfn = start >> PAGE_SHIFT;
	last_pfn = end >> PAGE_SHIFT;

	node_data[nodeid] = early_node_mem(nodeid, start, end, pgdat_size,
					   SMP_CACHE_BYTES);
	if (node_data[nodeid] == NULL)
		return;
	nodedata_phys = __pa(node_data[nodeid]);
	printk(KERN_INFO "  NODE_DATA [%016lx - %016lx]\n", nodedata_phys,
		nodedata_phys + pgdat_size - 1);

	memset(NODE_DATA(nodeid), 0, sizeof(pg_data_t));
	NODE_DATA(nodeid)->bdata = &bootmem_node_data[nodeid];
	NODE_DATA(nodeid)->node_start_pfn = start_pfn;
	NODE_DATA(nodeid)->node_spanned_pages = last_pfn - start_pfn;

	/*
	 * Find a place for the bootmem map
	 * nodedata_phys could be on other nodes by alloc_bootmem,
	 * so need to sure bootmap_start not to be small, otherwise
	 * early_node_mem will get that with find_e820_area instead
	 * of alloc_bootmem, that could clash with reserved range
	 */
	bootmap_pages = bootmem_bootmap_pages(last_pfn - start_pfn);
	nid = phys_to_nid(nodedata_phys);
	if (nid == nodeid)
		bootmap_start = roundup(nodedata_phys + pgdat_size, PAGE_SIZE);
	else
		bootmap_start = roundup(start, PAGE_SIZE);
	/*
	 * SMP_CACHE_BYTES could be enough, but init_bootmem_node like
	 * to use that to align to PAGE_SIZE
	 */
	bootmap = early_node_mem(nodeid, bootmap_start, end,
				 bootmap_pages<<PAGE_SHIFT, PAGE_SIZE);
	if (bootmap == NULL)  {
		if (nodedata_phys < start || nodedata_phys >= end)
			free_bootmem(nodedata_phys, pgdat_size);
		node_data[nodeid] = NULL;
		return;
	}
	bootmap_start = __pa(bootmap);

	bootmap_size = init_bootmem_node(NODE_DATA(nodeid),
					 bootmap_start >> PAGE_SHIFT,
					 start_pfn, last_pfn);

	printk(KERN_INFO "  bootmap [%016lx -  %016lx] pages %lx\n",
		 bootmap_start, bootmap_start + bootmap_size - 1,
		 bootmap_pages);

	free_bootmem_with_active_regions(nodeid, end);

	/*
	 * convert early reserve to bootmem reserve earlier
	 * otherwise early_node_mem could use early reserved mem
	 * on previous node
	 */
	early_res_to_bootmem(start, end);

	/*
	 * in some case early_node_mem could use alloc_bootmem
	 * to get range on other node, don't reserve that again
	 */
	if (nid != nodeid)
		printk(KERN_INFO "    NODE_DATA(%d) on node %d\n", nodeid, nid);
	else
		reserve_bootmem_node(NODE_DATA(nodeid), nodedata_phys,
					pgdat_size, BOOTMEM_DEFAULT);
	nid = phys_to_nid(bootmap_start);
	if (nid != nodeid)
		printk(KERN_INFO "    bootmap(%d) on node %d\n", nodeid, nid);
	else
		reserve_bootmem_node(NODE_DATA(nodeid), bootmap_start,
				 bootmap_pages<<PAGE_SHIFT, BOOTMEM_DEFAULT);

#ifdef CONFIG_ACPI_NUMA
	srat_reserve_add_area(nodeid);
#endif
	node_set_online(nodeid);
}

/*
 * There are unfortunately some poorly designed mainboards around that
 * only connect memory to a single CPU. This breaks the 1:1 cpu->node
 * mapping. To avoid this fill in the mapping for all possible CPUs,
 * as the number of CPUs is not known yet. We round robin the existing
 * nodes.
 */
void __init numa_init_array(void)
{
	int rr, i;

	rr = first_node(node_online_map);
	for (i = 0; i < nr_cpu_ids; i++) {
		if (early_cpu_to_node(i) != NUMA_NO_NODE)
			continue;
		numa_set_node(i, rr);
		rr = next_node(rr, node_online_map);
		if (rr == MAX_NUMNODES)
			rr = first_node(node_online_map);
	}
}

#ifdef CONFIG_NUMA_EMU
/* Numa emulation */
static char *cmdline __initdata;

/*
 * Setups up nid to range from addr to addr + size.  If the end
 * boundary is greater than max_addr, then max_addr is used instead.
 * The return value is 0 if there is additional memory left for
 * allocation past addr and -1 otherwise.  addr is adjusted to be at
 * the end of the node.
 */
static int __init setup_node_range(int nid, struct bootnode *nodes, u64 *addr,
				   u64 size, u64 max_addr)
{
	int ret = 0;

	nodes[nid].start = *addr;
	*addr += size;
	if (*addr >= max_addr) {
		*addr = max_addr;
		ret = -1;
	}
	nodes[nid].end = *addr;
	node_set(nid, node_possible_map);
	printk(KERN_INFO "Faking node %d at %016Lx-%016Lx (%LuMB)\n", nid,
	       nodes[nid].start, nodes[nid].end,
	       (nodes[nid].end - nodes[nid].start) >> 20);
	return ret;
}

/*
 * Splits num_nodes nodes up equally starting at node_start.  The return value
 * is the number of nodes split up and addr is adjusted to be at the end of the
 * last node allocated.
 */
static int __init split_nodes_equally(struct bootnode *nodes, u64 *addr,
				      u64 max_addr, int node_start,
				      int num_nodes)
{
	unsigned int big;
	u64 size;
	int i;

	if (num_nodes <= 0)
		return -1;
	if (num_nodes > MAX_NUMNODES)
		num_nodes = MAX_NUMNODES;
	size = (max_addr - *addr - e820_hole_size(*addr, max_addr)) /
	       num_nodes;
	/*
	 * Calculate the number of big nodes that can be allocated as a result
	 * of consolidating the leftovers.
	 */
	big = ((size & ~FAKE_NODE_MIN_HASH_MASK) * num_nodes) /
	      FAKE_NODE_MIN_SIZE;

	/* Round down to nearest FAKE_NODE_MIN_SIZE. */
	size &= FAKE_NODE_MIN_HASH_MASK;
	if (!size) {
		printk(KERN_ERR "Not enough memory for each node.  "
		       "NUMA emulation disabled.\n");
		return -1;
	}

	for (i = node_start; i < num_nodes + node_start; i++) {
		u64 end = *addr + size;

		if (i < big)
			end += FAKE_NODE_MIN_SIZE;
		/*
		 * The final node can have the remaining system RAM.  Other
		 * nodes receive roughly the same amount of available pages.
		 */
		if (i == num_nodes + node_start - 1)
			end = max_addr;
		else
			while (end - *addr - e820_hole_size(*addr, end) <
			       size) {
				end += FAKE_NODE_MIN_SIZE;
				if (end > max_addr) {
					end = max_addr;
					break;
				}
			}
		if (setup_node_range(i, nodes, addr, end - *addr, max_addr) < 0)
			break;
	}
	return i - node_start + 1;
}

/*
 * Splits the remaining system RAM into chunks of size.  The remaining memory is
 * always assigned to a final node and can be asymmetric.  Returns the number of
 * nodes split.
 */
static int __init split_nodes_by_size(struct bootnode *nodes, u64 *addr,
				      u64 max_addr, int node_start, u64 size)
{
	int i = node_start;
	size = (size << 20) & FAKE_NODE_MIN_HASH_MASK;
	while (!setup_node_range(i++, nodes, addr, size, max_addr))
		;
	return i - node_start;
}

/*
 * Sets up the system RAM area from start_pfn to last_pfn according to the
 * numa=fake command-line option.
 */
static struct bootnode nodes[MAX_NUMNODES] __initdata;

static int __init numa_emulation(unsigned long start_pfn, unsigned long last_pfn)
{
	u64 size, addr = start_pfn << PAGE_SHIFT;
	u64 max_addr = last_pfn << PAGE_SHIFT;
	int num_nodes = 0, num = 0, coeff_flag, coeff = -1, i;

	memset(&nodes, 0, sizeof(nodes));
	/*
	 * If the numa=fake command-line is just a single number N, split the
	 * system RAM into N fake nodes.
	 */
	if (!strchr(cmdline, '*') && !strchr(cmdline, ',')) {
		long n = simple_strtol(cmdline, NULL, 0);

		num_nodes = split_nodes_equally(nodes, &addr, max_addr, 0, n);
		if (num_nodes < 0)
			return num_nodes;
		goto out;
	}

	/* Parse the command line. */
	for (coeff_flag = 0; ; cmdline++) {
		if (*cmdline && isdigit(*cmdline)) {
			num = num * 10 + *cmdline - '0';
			continue;
		}
		if (*cmdline == '*') {
			if (num > 0)
				coeff = num;
			coeff_flag = 1;
		}
		if (!*cmdline || *cmdline == ',') {
			if (!coeff_flag)
				coeff = 1;
			/*
			 * Round down to the nearest FAKE_NODE_MIN_SIZE.
			 * Command-line coefficients are in megabytes.
			 */
			size = ((u64)num << 20) & FAKE_NODE_MIN_HASH_MASK;
			if (size)
				for (i = 0; i < coeff; i++, num_nodes++)
					if (setup_node_range(num_nodes, nodes,
						&addr, size, max_addr) < 0)
						goto done;
			if (!*cmdline)
				break;
			coeff_flag = 0;
			coeff = -1;
		}
		num = 0;
	}
done:
	if (!num_nodes)
		return -1;
	/* Fill remainder of system RAM, if appropriate. */
	if (addr < max_addr) {
		if (coeff_flag && coeff < 0) {
			/* Split remaining nodes into num-sized chunks */
			num_nodes += split_nodes_by_size(nodes, &addr, max_addr,
							 num_nodes, num);
			goto out;
		}
		switch (*(cmdline - 1)) {
		case '*':
			/* Split remaining nodes into coeff chunks */
			if (coeff <= 0)
				break;
			num_nodes += split_nodes_equally(nodes, &addr, max_addr,
							 num_nodes, coeff);
			break;
		case ',':
			/* Do not allocate remaining system RAM */
			break;
		default:
			/* Give one final node */
			setup_node_range(num_nodes, nodes, &addr,
					 max_addr - addr, max_addr);
			num_nodes++;
		}
	}
out:
	memnode_shift = compute_hash_shift(nodes, num_nodes, NULL);
	if (memnode_shift < 0) {
		memnode_shift = 0;
		printk(KERN_ERR "No NUMA hash function found.  NUMA emulation "
		       "disabled.\n");
		return -1;
	}

	/*
	 * We need to vacate all active ranges that may have been registered by
	 * SRAT and set acpi_numa to -1 so that srat_disabled() always returns
	 * true.  NUMA emulation has succeeded so we will not scan ACPI nodes.
	 */
	remove_all_active_ranges();
#ifdef CONFIG_ACPI_NUMA
	acpi_numa = -1;
#endif
	for_each_node_mask(i, node_possible_map) {
		e820_register_active_regions(i, nodes[i].start >> PAGE_SHIFT,
						nodes[i].end >> PAGE_SHIFT);
		setup_node_bootmem(i, nodes[i].start, nodes[i].end);
	}
	acpi_fake_nodes(nodes, num_nodes);
	numa_init_array();
	return 0;
}
#endif /* CONFIG_NUMA_EMU */

void __init initmem_init(unsigned long start_pfn, unsigned long last_pfn)
{
	int i;

	nodes_clear(node_possible_map);
	nodes_clear(node_online_map);

#ifdef CONFIG_NUMA_EMU
	if (cmdline && !numa_emulation(start_pfn, last_pfn))
		return;
	nodes_clear(node_possible_map);
	nodes_clear(node_online_map);
#endif

#ifdef CONFIG_ACPI_NUMA
	if (!numa_off && !acpi_scan_nodes(start_pfn << PAGE_SHIFT,
					  last_pfn << PAGE_SHIFT))
		return;
	nodes_clear(node_possible_map);
	nodes_clear(node_online_map);
#endif

#ifdef CONFIG_K8_NUMA
	if (!numa_off && !k8_scan_nodes(start_pfn<<PAGE_SHIFT,
					last_pfn<<PAGE_SHIFT))
		return;
	nodes_clear(node_possible_map);
	nodes_clear(node_online_map);
#endif
	printk(KERN_INFO "%s\n",
	       numa_off ? "NUMA turned off" : "No NUMA configuration found");

	printk(KERN_INFO "Faking a node at %016lx-%016lx\n",
	       start_pfn << PAGE_SHIFT,
	       last_pfn << PAGE_SHIFT);
	/* setup dummy node covering all memory */
	memnode_shift = 63;
	memnodemap = memnode.embedded_map;
	memnodemap[0] = 0;
	node_set_online(0);
	node_set(0, node_possible_map);
	for (i = 0; i < nr_cpu_ids; i++)
		numa_set_node(i, 0);
	e820_register_active_regions(0, start_pfn, last_pfn);
	setup_node_bootmem(0, start_pfn << PAGE_SHIFT, last_pfn << PAGE_SHIFT);
}

unsigned long __init numa_free_all_bootmem(void)
{
	unsigned long pages = 0;
	int i;

	for_each_online_node(i)
		pages += free_all_bootmem_node(NODE_DATA(i));

	return pages;
}

void __init paging_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];

	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));
	max_zone_pfns[ZONE_DMA] = MAX_DMA_PFN;
	max_zone_pfns[ZONE_DMA32] = MAX_DMA32_PFN;
	max_zone_pfns[ZONE_NORMAL] = max_pfn;

	sparse_memory_present_with_active_regions(MAX_NUMNODES);
	sparse_init();

	free_area_init_nodes(max_zone_pfns);
}

static __init int numa_setup(char *opt)
{
	if (!opt)
		return -EINVAL;
	if (!strncmp(opt, "off", 3))
		numa_off = 1;
#ifdef CONFIG_NUMA_EMU
	if (!strncmp(opt, "fake=", 5))
		cmdline = opt + 5;
#endif
#ifdef CONFIG_ACPI_NUMA
	if (!strncmp(opt, "noacpi", 6))
		acpi_numa = -1;
	if (!strncmp(opt, "hotadd=", 7))
		hotadd_percent = simple_strtoul(opt+7, NULL, 10);
#endif
	return 0;
}
early_param("numa", numa_setup);

#ifdef CONFIG_NUMA
/*
 * Setup early cpu_to_node.
 *
 * Populate cpu_to_node[] only if x86_cpu_to_apicid[],
 * and apicid_to_node[] tables have valid entries for a CPU.
 * This means we skip cpu_to_node[] initialisation for NUMA
 * emulation and faking node case (when running a kernel compiled
 * for NUMA on a non NUMA box), which is OK as cpu_to_node[]
 * is already initialized in a round robin manner at numa_init_array,
 * prior to this call, and this initialization is good enough
 * for the fake NUMA cases.
 *
 * Called before the per_cpu areas are setup.
 */
void __init init_cpu_to_node(void)
{
	int cpu;
	u16 *cpu_to_apicid = early_per_cpu_ptr(x86_cpu_to_apicid);

	BUG_ON(cpu_to_apicid == NULL);

	for_each_possible_cpu(cpu) {
		int node;
		u16 apicid = cpu_to_apicid[cpu];

		if (apicid == BAD_APICID)
			continue;
		node = apicid_to_node[apicid];
		if (node == NUMA_NO_NODE)
			continue;
		if (!node_online(node))
			continue;
		numa_set_node(cpu, node);
	}
}
#endif


void __cpuinit numa_set_node(int cpu, int node)
{
	int *cpu_to_node_map = early_per_cpu_ptr(x86_cpu_to_node_map);

	/* early setting, no percpu area yet */
	if (cpu_to_node_map) {
		cpu_to_node_map[cpu] = node;
		return;
	}

#ifdef CONFIG_DEBUG_PER_CPU_MAPS
	if (cpu >= nr_cpu_ids || !cpu_possible(cpu)) {
		printk(KERN_ERR "numa_set_node: invalid cpu# (%d)\n", cpu);
		dump_stack();
		return;
	}
#endif
	per_cpu(x86_cpu_to_node_map, cpu) = node;

	if (node != NUMA_NO_NODE)
		per_cpu(node_number, cpu) = node;
}

void __cpuinit numa_clear_node(int cpu)
{
	numa_set_node(cpu, NUMA_NO_NODE);
}

#ifndef CONFIG_DEBUG_PER_CPU_MAPS

void __cpuinit numa_add_cpu(int cpu)
{
	cpumask_set_cpu(cpu, node_to_cpumask_map[early_cpu_to_node(cpu)]);
}

void __cpuinit numa_remove_cpu(int cpu)
{
	cpumask_clear_cpu(cpu, node_to_cpumask_map[early_cpu_to_node(cpu)]);
}

#else /* CONFIG_DEBUG_PER_CPU_MAPS */

/*
 * --------- debug versions of the numa functions ---------
 */
static void __cpuinit numa_set_cpumask(int cpu, int enable)
{
	int node = early_cpu_to_node(cpu);
	struct cpumask *mask;
	char buf[64];

	mask = node_to_cpumask_map[node];
	if (mask == NULL) {
		printk(KERN_ERR "node_to_cpumask_map[%i] NULL\n", node);
		dump_stack();
		return;
	}

	if (enable)
		cpumask_set_cpu(cpu, mask);
	else
		cpumask_clear_cpu(cpu, mask);

	cpulist_scnprintf(buf, sizeof(buf), mask);
	printk(KERN_DEBUG "%s cpu %d node %d: mask now %s\n",
		enable ? "numa_add_cpu" : "numa_remove_cpu", cpu, node, buf);
}

void __cpuinit numa_add_cpu(int cpu)
{
	numa_set_cpumask(cpu, 1);
}

void __cpuinit numa_remove_cpu(int cpu)
{
	numa_set_cpumask(cpu, 0);
}

int cpu_to_node(int cpu)
{
	if (early_per_cpu_ptr(x86_cpu_to_node_map)) {
		printk(KERN_WARNING
			"cpu_to_node(%d): usage too early!\n", cpu);
		dump_stack();
		return early_per_cpu_ptr(x86_cpu_to_node_map)[cpu];
	}
	return per_cpu(x86_cpu_to_node_map, cpu);
}
EXPORT_SYMBOL(cpu_to_node);

/*
 * Same function as cpu_to_node() but used if called before the
 * per_cpu areas are setup.
 */
int early_cpu_to_node(int cpu)
{
	if (early_per_cpu_ptr(x86_cpu_to_node_map))
		return early_per_cpu_ptr(x86_cpu_to_node_map)[cpu];

	if (!cpu_possible(cpu)) {
		printk(KERN_WARNING
			"early_cpu_to_node(%d): no per_cpu area!\n", cpu);
		dump_stack();
		return NUMA_NO_NODE;
	}
	return per_cpu(x86_cpu_to_node_map, cpu);
}

/*
 * --------- end of debug versions of the numa functions ---------
 */

#endif /* CONFIG_DEBUG_PER_CPU_MAPS */
