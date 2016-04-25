/* Common code for 32 and 64-bit NUMA */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/memblock.h>
#include <linux/mmzone.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/nodemask.h>
#include <linux/sched.h>
#include <linux/topology.h>

#include <asm/e820.h>
#include <asm/proto.h>
#include <asm/dma.h>
#include <asm/acpi.h>
#include <asm/amd_nb.h>

#include "numa_internal.h"

int __initdata numa_off;
nodemask_t numa_nodes_parsed __initdata;

struct pglist_data *node_data[MAX_NUMNODES] __read_mostly;
EXPORT_SYMBOL(node_data);

static struct numa_meminfo numa_meminfo
#ifndef CONFIG_MEMORY_HOTPLUG
__initdata
#endif
;

static int numa_distance_cnt;
static u8 *numa_distance;

static __init int numa_setup(char *opt)
{
	if (!opt)
		return -EINVAL;
	if (!strncmp(opt, "off", 3))
		numa_off = 1;
#ifdef CONFIG_NUMA_EMU
	if (!strncmp(opt, "fake=", 5))
		numa_emu_cmdline(opt + 5);
#endif
#ifdef CONFIG_ACPI_NUMA
	if (!strncmp(opt, "noacpi", 6))
		acpi_numa = -1;
#endif
	return 0;
}
early_param("numa", numa_setup);

/*
 * apicid, cpu, node mappings
 */
s16 __apicid_to_node[MAX_LOCAL_APIC] = {
	[0 ... MAX_LOCAL_APIC-1] = NUMA_NO_NODE
};

int numa_cpu_node(int cpu)
{
	int apicid = early_per_cpu(x86_cpu_to_apicid, cpu);

	if (apicid != BAD_APICID)
		return __apicid_to_node[apicid];
	return NUMA_NO_NODE;
}

cpumask_var_t node_to_cpumask_map[MAX_NUMNODES];
EXPORT_SYMBOL(node_to_cpumask_map);

/*
 * Map cpu index to node index
 */
DEFINE_EARLY_PER_CPU(int, x86_cpu_to_node_map, NUMA_NO_NODE);
EXPORT_EARLY_PER_CPU_SYMBOL(x86_cpu_to_node_map);

void numa_set_node(int cpu, int node)
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

	set_cpu_numa_node(cpu, node);
}

void numa_clear_node(int cpu)
{
	numa_set_node(cpu, NUMA_NO_NODE);
}

/*
 * Allocate node_to_cpumask_map based on number of available nodes
 * Requires node_possible_map to be valid.
 *
 * Note: cpumask_of_node() is not valid until after this is done.
 * (Use CONFIG_DEBUG_PER_CPU_MAPS to check this.)
 */
void __init setup_node_to_cpumask_map(void)
{
	unsigned int node;

	/* setup nr_node_ids if not done yet */
	if (nr_node_ids == MAX_NUMNODES)
		setup_nr_node_ids();

	/* allocate the map */
	for (node = 0; node < nr_node_ids; node++)
		alloc_bootmem_cpumask_var(&node_to_cpumask_map[node]);

	/* cpumask_of_node() will now work */
	pr_debug("Node to cpumask map for %d nodes\n", nr_node_ids);
}

static int __init numa_add_memblk_to(int nid, u64 start, u64 end,
				     struct numa_meminfo *mi)
{
	/* ignore zero length blks */
	if (start == end)
		return 0;

	/* whine about and ignore invalid blks */
	if (start > end || nid < 0 || nid >= MAX_NUMNODES) {
		pr_warning("NUMA: Warning: invalid memblk node %d [mem %#010Lx-%#010Lx]\n",
			   nid, start, end - 1);
		return 0;
	}

	if (mi->nr_blks >= NR_NODE_MEMBLKS) {
		pr_err("NUMA: too many memblk ranges\n");
		return -EINVAL;
	}

	mi->blk[mi->nr_blks].start = start;
	mi->blk[mi->nr_blks].end = end;
	mi->blk[mi->nr_blks].nid = nid;
	mi->nr_blks++;
	return 0;
}

/**
 * numa_remove_memblk_from - Remove one numa_memblk from a numa_meminfo
 * @idx: Index of memblk to remove
 * @mi: numa_meminfo to remove memblk from
 *
 * Remove @idx'th numa_memblk from @mi by shifting @mi->blk[] and
 * decrementing @mi->nr_blks.
 */
void __init numa_remove_memblk_from(int idx, struct numa_meminfo *mi)
{
	mi->nr_blks--;
	memmove(&mi->blk[idx], &mi->blk[idx + 1],
		(mi->nr_blks - idx) * sizeof(mi->blk[0]));
}

/**
 * numa_add_memblk - Add one numa_memblk to numa_meminfo
 * @nid: NUMA node ID of the new memblk
 * @start: Start address of the new memblk
 * @end: End address of the new memblk
 *
 * Add a new memblk to the default numa_meminfo.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
int __init numa_add_memblk(int nid, u64 start, u64 end)
{
	return numa_add_memblk_to(nid, start, end, &numa_meminfo);
}

/* Allocate NODE_DATA for a node on the local memory */
static void __init alloc_node_data(int nid)
{
	const size_t nd_size = roundup(sizeof(pg_data_t), PAGE_SIZE);
	u64 nd_pa;
	void *nd;
	int tnid;

	/*
	 * Allocate node data.  Try node-local memory and then any node.
	 * Never allocate in DMA zone.
	 */
	nd_pa = memblock_alloc_nid(nd_size, SMP_CACHE_BYTES, nid);
	if (!nd_pa) {
		nd_pa = __memblock_alloc_base(nd_size, SMP_CACHE_BYTES,
					      MEMBLOCK_ALLOC_ACCESSIBLE);
		if (!nd_pa) {
			pr_err("Cannot find %zu bytes in node %d\n",
			       nd_size, nid);
			return;
		}
	}
	nd = __va(nd_pa);

	/* report and initialize */
	printk(KERN_INFO "NODE_DATA(%d) allocated [mem %#010Lx-%#010Lx]\n", nid,
	       nd_pa, nd_pa + nd_size - 1);
	tnid = early_pfn_to_nid(nd_pa >> PAGE_SHIFT);
	if (tnid != nid)
		printk(KERN_INFO "    NODE_DATA(%d) on node %d\n", nid, tnid);

	node_data[nid] = nd;
	memset(NODE_DATA(nid), 0, sizeof(pg_data_t));

	node_set_online(nid);
}

/**
 * numa_cleanup_meminfo - Cleanup a numa_meminfo
 * @mi: numa_meminfo to clean up
 *
 * Sanitize @mi by merging and removing unncessary memblks.  Also check for
 * conflicts and clear unused memblks.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
int __init numa_cleanup_meminfo(struct numa_meminfo *mi)
{
	const u64 low = 0;
	const u64 high = PFN_PHYS(max_pfn);
	int i, j, k;

	/* first, trim all entries */
	for (i = 0; i < mi->nr_blks; i++) {
		struct numa_memblk *bi = &mi->blk[i];

		/* make sure all blocks are inside the limits */
		bi->start = max(bi->start, low);
		bi->end = min(bi->end, high);

		/* and there's no empty or non-exist block */
		if (bi->start >= bi->end ||
		    !memblock_overlaps_region(&memblock.memory,
			bi->start, bi->end - bi->start))
			numa_remove_memblk_from(i--, mi);
	}

	/* merge neighboring / overlapping entries */
	for (i = 0; i < mi->nr_blks; i++) {
		struct numa_memblk *bi = &mi->blk[i];

		for (j = i + 1; j < mi->nr_blks; j++) {
			struct numa_memblk *bj = &mi->blk[j];
			u64 start, end;

			/*
			 * See whether there are overlapping blocks.  Whine
			 * about but allow overlaps of the same nid.  They
			 * will be merged below.
			 */
			if (bi->end > bj->start && bi->start < bj->end) {
				if (bi->nid != bj->nid) {
					pr_err("NUMA: node %d [mem %#010Lx-%#010Lx] overlaps with node %d [mem %#010Lx-%#010Lx]\n",
					       bi->nid, bi->start, bi->end - 1,
					       bj->nid, bj->start, bj->end - 1);
					return -EINVAL;
				}
				pr_warning("NUMA: Warning: node %d [mem %#010Lx-%#010Lx] overlaps with itself [mem %#010Lx-%#010Lx]\n",
					   bi->nid, bi->start, bi->end - 1,
					   bj->start, bj->end - 1);
			}

			/*
			 * Join together blocks on the same node, holes
			 * between which don't overlap with memory on other
			 * nodes.
			 */
			if (bi->nid != bj->nid)
				continue;
			start = min(bi->start, bj->start);
			end = max(bi->end, bj->end);
			for (k = 0; k < mi->nr_blks; k++) {
				struct numa_memblk *bk = &mi->blk[k];

				if (bi->nid == bk->nid)
					continue;
				if (start < bk->end && end > bk->start)
					break;
			}
			if (k < mi->nr_blks)
				continue;
			printk(KERN_INFO "NUMA: Node %d [mem %#010Lx-%#010Lx] + [mem %#010Lx-%#010Lx] -> [mem %#010Lx-%#010Lx]\n",
			       bi->nid, bi->start, bi->end - 1, bj->start,
			       bj->end - 1, start, end - 1);
			bi->start = start;
			bi->end = end;
			numa_remove_memblk_from(j--, mi);
		}
	}

	/* clear unused ones */
	for (i = mi->nr_blks; i < ARRAY_SIZE(mi->blk); i++) {
		mi->blk[i].start = mi->blk[i].end = 0;
		mi->blk[i].nid = NUMA_NO_NODE;
	}

	return 0;
}

/*
 * Set nodes, which have memory in @mi, in *@nodemask.
 */
static void __init numa_nodemask_from_meminfo(nodemask_t *nodemask,
					      const struct numa_meminfo *mi)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mi->blk); i++)
		if (mi->blk[i].start != mi->blk[i].end &&
		    mi->blk[i].nid != NUMA_NO_NODE)
			node_set(mi->blk[i].nid, *nodemask);
}

/**
 * numa_reset_distance - Reset NUMA distance table
 *
 * The current table is freed.  The next numa_set_distance() call will
 * create a new one.
 */
void __init numa_reset_distance(void)
{
	size_t size = numa_distance_cnt * numa_distance_cnt * sizeof(numa_distance[0]);

	/* numa_distance could be 1LU marking allocation failure, test cnt */
	if (numa_distance_cnt)
		memblock_free(__pa(numa_distance), size);
	numa_distance_cnt = 0;
	numa_distance = NULL;	/* enable table creation */
}

static int __init numa_alloc_distance(void)
{
	nodemask_t nodes_parsed;
	size_t size;
	int i, j, cnt = 0;
	u64 phys;

	/* size the new table and allocate it */
	nodes_parsed = numa_nodes_parsed;
	numa_nodemask_from_meminfo(&nodes_parsed, &numa_meminfo);

	for_each_node_mask(i, nodes_parsed)
		cnt = i;
	cnt++;
	size = cnt * cnt * sizeof(numa_distance[0]);

	phys = memblock_find_in_range(0, PFN_PHYS(max_pfn_mapped),
				      size, PAGE_SIZE);
	if (!phys) {
		pr_warning("NUMA: Warning: can't allocate distance table!\n");
		/* don't retry until explicitly reset */
		numa_distance = (void *)1LU;
		return -ENOMEM;
	}
	memblock_reserve(phys, size);

	numa_distance = __va(phys);
	numa_distance_cnt = cnt;

	/* fill with the default distances */
	for (i = 0; i < cnt; i++)
		for (j = 0; j < cnt; j++)
			numa_distance[i * cnt + j] = i == j ?
				LOCAL_DISTANCE : REMOTE_DISTANCE;
	printk(KERN_DEBUG "NUMA: Initialized distance table, cnt=%d\n", cnt);

	return 0;
}

/**
 * numa_set_distance - Set NUMA distance from one NUMA to another
 * @from: the 'from' node to set distance
 * @to: the 'to'  node to set distance
 * @distance: NUMA distance
 *
 * Set the distance from node @from to @to to @distance.  If distance table
 * doesn't exist, one which is large enough to accommodate all the currently
 * known nodes will be created.
 *
 * If such table cannot be allocated, a warning is printed and further
 * calls are ignored until the distance table is reset with
 * numa_reset_distance().
 *
 * If @from or @to is higher than the highest known node or lower than zero
 * at the time of table creation or @distance doesn't make sense, the call
 * is ignored.
 * This is to allow simplification of specific NUMA config implementations.
 */
void __init numa_set_distance(int from, int to, int distance)
{
	if (!numa_distance && numa_alloc_distance() < 0)
		return;

	if (from >= numa_distance_cnt || to >= numa_distance_cnt ||
			from < 0 || to < 0) {
		pr_warn_once("NUMA: Warning: node ids are out of bound, from=%d to=%d distance=%d\n",
			    from, to, distance);
		return;
	}

	if ((u8)distance != distance ||
	    (from == to && distance != LOCAL_DISTANCE)) {
		pr_warn_once("NUMA: Warning: invalid distance parameter, from=%d to=%d distance=%d\n",
			     from, to, distance);
		return;
	}

	numa_distance[from * numa_distance_cnt + to] = distance;
}

int __node_distance(int from, int to)
{
	if (from >= numa_distance_cnt || to >= numa_distance_cnt)
		return from == to ? LOCAL_DISTANCE : REMOTE_DISTANCE;
	return numa_distance[from * numa_distance_cnt + to];
}
EXPORT_SYMBOL(__node_distance);

/*
 * Sanity check to catch more bad NUMA configurations (they are amazingly
 * common).  Make sure the nodes cover all memory.
 */
static bool __init numa_meminfo_cover_memory(const struct numa_meminfo *mi)
{
	u64 numaram, e820ram;
	int i;

	numaram = 0;
	for (i = 0; i < mi->nr_blks; i++) {
		u64 s = mi->blk[i].start >> PAGE_SHIFT;
		u64 e = mi->blk[i].end >> PAGE_SHIFT;
		numaram += e - s;
		numaram -= __absent_pages_in_range(mi->blk[i].nid, s, e);
		if ((s64)numaram < 0)
			numaram = 0;
	}

	e820ram = max_pfn - absent_pages_in_range(0, max_pfn);

	/* We seem to lose 3 pages somewhere. Allow 1M of slack. */
	if ((s64)(e820ram - numaram) >= (1 << (20 - PAGE_SHIFT))) {
		printk(KERN_ERR "NUMA: nodes only cover %LuMB of your %LuMB e820 RAM. Not used.\n",
		       (numaram << PAGE_SHIFT) >> 20,
		       (e820ram << PAGE_SHIFT) >> 20);
		return false;
	}
	return true;
}

/*
 * Mark all currently memblock-reserved physical memory (which covers the
 * kernel's own memory ranges) as hot-unswappable.
 */
static void __init numa_clear_kernel_node_hotplug(void)
{
	nodemask_t reserved_nodemask = NODE_MASK_NONE;
	struct memblock_region *mb_region;
	int i;

	/*
	 * We have to do some preprocessing of memblock regions, to
	 * make them suitable for reservation.
	 *
	 * At this time, all memory regions reserved by memblock are
	 * used by the kernel, but those regions are not split up
	 * along node boundaries yet, and don't necessarily have their
	 * node ID set yet either.
	 *
	 * So iterate over all memory known to the x86 architecture,
	 * and use those ranges to set the nid in memblock.reserved.
	 * This will split up the memblock regions along node
	 * boundaries and will set the node IDs as well.
	 */
	for (i = 0; i < numa_meminfo.nr_blks; i++) {
		struct numa_memblk *mb = numa_meminfo.blk + i;
		int ret;

		ret = memblock_set_node(mb->start, mb->end - mb->start, &memblock.reserved, mb->nid);
		WARN_ON_ONCE(ret);
	}

	/*
	 * Now go over all reserved memblock regions, to construct a
	 * node mask of all kernel reserved memory areas.
	 *
	 * [ Note, when booting with mem=nn[kMG] or in a kdump kernel,
	 *   numa_meminfo might not include all memblock.reserved
	 *   memory ranges, because quirks such as trim_snb_memory()
	 *   reserve specific pages for Sandy Bridge graphics. ]
	 */
	for_each_memblock(reserved, mb_region) {
		if (mb_region->nid != MAX_NUMNODES)
			node_set(mb_region->nid, reserved_nodemask);
	}

	/*
	 * Finally, clear the MEMBLOCK_HOTPLUG flag for all memory
	 * belonging to the reserved node mask.
	 *
	 * Note that this will include memory regions that reside
	 * on nodes that contain kernel memory - entire nodes
	 * become hot-unpluggable:
	 */
	for (i = 0; i < numa_meminfo.nr_blks; i++) {
		struct numa_memblk *mb = numa_meminfo.blk + i;

		if (!node_isset(mb->nid, reserved_nodemask))
			continue;

		memblock_clear_hotplug(mb->start, mb->end - mb->start);
	}
}

static int __init numa_register_memblks(struct numa_meminfo *mi)
{
	unsigned long uninitialized_var(pfn_align);
	int i, nid;

	/* Account for nodes with cpus and no memory */
	node_possible_map = numa_nodes_parsed;
	numa_nodemask_from_meminfo(&node_possible_map, mi);
	if (WARN_ON(nodes_empty(node_possible_map)))
		return -EINVAL;

	for (i = 0; i < mi->nr_blks; i++) {
		struct numa_memblk *mb = &mi->blk[i];
		memblock_set_node(mb->start, mb->end - mb->start,
				  &memblock.memory, mb->nid);
	}

	/*
	 * At very early time, the kernel have to use some memory such as
	 * loading the kernel image. We cannot prevent this anyway. So any
	 * node the kernel resides in should be un-hotpluggable.
	 *
	 * And when we come here, alloc node data won't fail.
	 */
	numa_clear_kernel_node_hotplug();

	/*
	 * If sections array is gonna be used for pfn -> nid mapping, check
	 * whether its granularity is fine enough.
	 */
#ifdef NODE_NOT_IN_PAGE_FLAGS
	pfn_align = node_map_pfn_alignment();
	if (pfn_align && pfn_align < PAGES_PER_SECTION) {
		printk(KERN_WARNING "Node alignment %LuMB < min %LuMB, rejecting NUMA config\n",
		       PFN_PHYS(pfn_align) >> 20,
		       PFN_PHYS(PAGES_PER_SECTION) >> 20);
		return -EINVAL;
	}
#endif
	if (!numa_meminfo_cover_memory(mi))
		return -EINVAL;

	/* Finally register nodes. */
	for_each_node_mask(nid, node_possible_map) {
		u64 start = PFN_PHYS(max_pfn);
		u64 end = 0;

		for (i = 0; i < mi->nr_blks; i++) {
			if (nid != mi->blk[i].nid)
				continue;
			start = min(mi->blk[i].start, start);
			end = max(mi->blk[i].end, end);
		}

		if (start >= end)
			continue;

		/*
		 * Don't confuse VM with a node that doesn't have the
		 * minimum amount of memory:
		 */
		if (end && (end - start) < NODE_MIN_SIZE)
			continue;

		alloc_node_data(nid);
	}

	/* Dump memblock with node info and return. */
	memblock_dump_all();
	return 0;
}

/*
 * There are unfortunately some poorly designed mainboards around that
 * only connect memory to a single CPU. This breaks the 1:1 cpu->node
 * mapping. To avoid this fill in the mapping for all possible CPUs,
 * as the number of CPUs is not known yet. We round robin the existing
 * nodes.
 */
static void __init numa_init_array(void)
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

static int __init numa_init(int (*init_func)(void))
{
	int i;
	int ret;

	for (i = 0; i < MAX_LOCAL_APIC; i++)
		set_apicid_to_node(i, NUMA_NO_NODE);

	nodes_clear(numa_nodes_parsed);
	nodes_clear(node_possible_map);
	nodes_clear(node_online_map);
	memset(&numa_meminfo, 0, sizeof(numa_meminfo));
	WARN_ON(memblock_set_node(0, ULLONG_MAX, &memblock.memory,
				  MAX_NUMNODES));
	WARN_ON(memblock_set_node(0, ULLONG_MAX, &memblock.reserved,
				  MAX_NUMNODES));
	/* In case that parsing SRAT failed. */
	WARN_ON(memblock_clear_hotplug(0, ULLONG_MAX));
	numa_reset_distance();

	ret = init_func();
	if (ret < 0)
		return ret;

	/*
	 * We reset memblock back to the top-down direction
	 * here because if we configured ACPI_NUMA, we have
	 * parsed SRAT in init_func(). It is ok to have the
	 * reset here even if we did't configure ACPI_NUMA
	 * or acpi numa init fails and fallbacks to dummy
	 * numa init.
	 */
	memblock_set_bottom_up(false);

	ret = numa_cleanup_meminfo(&numa_meminfo);
	if (ret < 0)
		return ret;

	numa_emulation(&numa_meminfo, numa_distance_cnt);

	ret = numa_register_memblks(&numa_meminfo);
	if (ret < 0)
		return ret;

	for (i = 0; i < nr_cpu_ids; i++) {
		int nid = early_cpu_to_node(i);

		if (nid == NUMA_NO_NODE)
			continue;
		if (!node_online(nid))
			numa_clear_node(i);
	}
	numa_init_array();

	return 0;
}

/**
 * dummy_numa_init - Fallback dummy NUMA init
 *
 * Used if there's no underlying NUMA architecture, NUMA initialization
 * fails, or NUMA is disabled on the command line.
 *
 * Must online at least one node and add memory blocks that cover all
 * allowed memory.  This function must not fail.
 */
static int __init dummy_numa_init(void)
{
	printk(KERN_INFO "%s\n",
	       numa_off ? "NUMA turned off" : "No NUMA configuration found");
	printk(KERN_INFO "Faking a node at [mem %#018Lx-%#018Lx]\n",
	       0LLU, PFN_PHYS(max_pfn) - 1);

	node_set(0, numa_nodes_parsed);
	numa_add_memblk(0, 0, PFN_PHYS(max_pfn));

	return 0;
}

/**
 * x86_numa_init - Initialize NUMA
 *
 * Try each configured NUMA initialization method until one succeeds.  The
 * last fallback is dummy single node config encomapssing whole memory and
 * never fails.
 */
void __init x86_numa_init(void)
{
	if (!numa_off) {
#ifdef CONFIG_ACPI_NUMA
		if (!numa_init(x86_acpi_numa_init))
			return;
#endif
#ifdef CONFIG_AMD_NUMA
		if (!numa_init(amd_numa_init))
			return;
#endif
	}

	numa_init(dummy_numa_init);
}

static __init int find_near_online_node(int node)
{
	int n, val;
	int min_val = INT_MAX;
	int best_node = -1;

	for_each_online_node(n) {
		val = node_distance(node, n);

		if (val < min_val) {
			min_val = val;
			best_node = n;
		}
	}

	return best_node;
}

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
		int node = numa_cpu_node(cpu);

		if (node == NUMA_NO_NODE)
			continue;
		if (!node_online(node))
			node = find_near_online_node(node);
		numa_set_node(cpu, node);
	}
}

#ifndef CONFIG_DEBUG_PER_CPU_MAPS

# ifndef CONFIG_NUMA_EMU
void numa_add_cpu(int cpu)
{
	cpumask_set_cpu(cpu, node_to_cpumask_map[early_cpu_to_node(cpu)]);
}

void numa_remove_cpu(int cpu)
{
	cpumask_clear_cpu(cpu, node_to_cpumask_map[early_cpu_to_node(cpu)]);
}
# endif	/* !CONFIG_NUMA_EMU */

#else	/* !CONFIG_DEBUG_PER_CPU_MAPS */

int __cpu_to_node(int cpu)
{
	if (early_per_cpu_ptr(x86_cpu_to_node_map)) {
		printk(KERN_WARNING
			"cpu_to_node(%d): usage too early!\n", cpu);
		dump_stack();
		return early_per_cpu_ptr(x86_cpu_to_node_map)[cpu];
	}
	return per_cpu(x86_cpu_to_node_map, cpu);
}
EXPORT_SYMBOL(__cpu_to_node);

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

void debug_cpumask_set_cpu(int cpu, int node, bool enable)
{
	struct cpumask *mask;

	if (node == NUMA_NO_NODE) {
		/* early_cpu_to_node() already emits a warning and trace */
		return;
	}
	mask = node_to_cpumask_map[node];
	if (!mask) {
		pr_err("node_to_cpumask_map[%i] NULL\n", node);
		dump_stack();
		return;
	}

	if (enable)
		cpumask_set_cpu(cpu, mask);
	else
		cpumask_clear_cpu(cpu, mask);

	printk(KERN_DEBUG "%s cpu %d node %d: mask now %*pbl\n",
		enable ? "numa_add_cpu" : "numa_remove_cpu",
		cpu, node, cpumask_pr_args(mask));
	return;
}

# ifndef CONFIG_NUMA_EMU
static void numa_set_cpumask(int cpu, bool enable)
{
	debug_cpumask_set_cpu(cpu, early_cpu_to_node(cpu), enable);
}

void numa_add_cpu(int cpu)
{
	numa_set_cpumask(cpu, true);
}

void numa_remove_cpu(int cpu)
{
	numa_set_cpumask(cpu, false);
}
# endif	/* !CONFIG_NUMA_EMU */

/*
 * Returns a pointer to the bitmask of CPUs on Node 'node'.
 */
const struct cpumask *cpumask_of_node(int node)
{
	if (node >= nr_node_ids) {
		printk(KERN_WARNING
			"cpumask_of_node(%d): node > nr_node_ids(%d)\n",
			node, nr_node_ids);
		dump_stack();
		return cpu_none_mask;
	}
	if (node_to_cpumask_map[node] == NULL) {
		printk(KERN_WARNING
			"cpumask_of_node(%d): no node_to_cpumask_map!\n",
			node);
		dump_stack();
		return cpu_online_mask;
	}
	return node_to_cpumask_map[node];
}
EXPORT_SYMBOL(cpumask_of_node);

#endif	/* !CONFIG_DEBUG_PER_CPU_MAPS */

#ifdef CONFIG_MEMORY_HOTPLUG
int memory_add_physaddr_to_nid(u64 start)
{
	struct numa_meminfo *mi = &numa_meminfo;
	int nid = mi->blk[0].nid;
	int i;

	for (i = 0; i < mi->nr_blks; i++)
		if (mi->blk[i].start <= start && mi->blk[i].end > start)
			nid = mi->blk[i].nid;
	return nid;
}
EXPORT_SYMBOL_GPL(memory_add_physaddr_to_nid);
#endif
