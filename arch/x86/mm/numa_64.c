/*
 * Generic VM initialization for x86-64 NUMA setups.
 * Copyright 2002,2003 Andi Kleen, SuSE Labs.
 */
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
#include <linux/acpi.h>

#include <asm/e820.h>
#include <asm/proto.h>
#include <asm/dma.h>
#include <asm/numa.h>
#include <asm/acpi.h>
#include <asm/amd_nb.h>

struct numa_memblk {
	u64			start;
	u64			end;
	int			nid;
};

struct numa_meminfo {
	int			nr_blks;
	struct numa_memblk	blk[NR_NODE_MEMBLKS];
};

struct pglist_data *node_data[MAX_NUMNODES] __read_mostly;
EXPORT_SYMBOL(node_data);

nodemask_t numa_nodes_parsed __initdata;

struct memnode memnode;

static unsigned long __initdata nodemap_addr;
static unsigned long __initdata nodemap_size;

static struct numa_meminfo numa_meminfo __initdata;

static int numa_distance_cnt;
static u8 *numa_distance;

#ifdef CONFIG_NUMA_EMU
static bool numa_emu_dist;
#endif

/*
 * Given a shift value, try to populate memnodemap[]
 * Returns :
 * 1 if OK
 * 0 if memnodmap[] too small (of shift too small)
 * -1 if node overlap or lost ram (shift too big)
 */
static int __init populate_memnodemap(const struct numa_meminfo *mi, int shift)
{
	unsigned long addr, end;
	int i, res = -1;

	memset(memnodemap, 0xff, sizeof(s16)*memnodemapsize);
	for (i = 0; i < mi->nr_blks; i++) {
		addr = mi->blk[i].start;
		end = mi->blk[i].end;
		if (addr >= end)
			continue;
		if ((end >> shift) >= memnodemapsize)
			return 0;
		do {
			if (memnodemap[addr >> shift] != NUMA_NO_NODE)
				return -1;
			memnodemap[addr >> shift] = mi->blk[i].nid;
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
	nodemap_addr = memblock_find_in_range(addr, get_max_mapped(),
				      nodemap_size, L1_CACHE_BYTES);
	if (nodemap_addr == MEMBLOCK_ERROR) {
		printk(KERN_ERR
		       "NUMA: Unable to allocate Memory to Node hash map\n");
		nodemap_addr = nodemap_size = 0;
		return -1;
	}
	memnodemap = phys_to_virt(nodemap_addr);
	memblock_x86_reserve_range(nodemap_addr, nodemap_addr + nodemap_size, "MEMNODEMAP");

	printk(KERN_DEBUG "NUMA: Allocated memnodemap from %lx - %lx\n",
	       nodemap_addr, nodemap_addr + nodemap_size);
	return 0;
}

/*
 * The LSB of all start and end addresses in the node map is the value of the
 * maximum possible shift.
 */
static int __init extract_lsb_from_nodes(const struct numa_meminfo *mi)
{
	int i, nodes_used = 0;
	unsigned long start, end;
	unsigned long bitfield = 0, memtop = 0;

	for (i = 0; i < mi->nr_blks; i++) {
		start = mi->blk[i].start;
		end = mi->blk[i].end;
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

static int __init compute_hash_shift(const struct numa_meminfo *mi)
{
	int shift;

	shift = extract_lsb_from_nodes(mi);
	if (allocate_cachealigned_memnodemap())
		return -1;
	printk(KERN_DEBUG "NUMA: Using %d for the hash shift.\n",
		shift);

	if (populate_memnodemap(mi, shift) != 1) {
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
	unsigned long mem;

	/*
	 * put it on high as possible
	 * something will go with NODE_DATA
	 */
	if (start < (MAX_DMA_PFN<<PAGE_SHIFT))
		start = MAX_DMA_PFN<<PAGE_SHIFT;
	if (start < (MAX_DMA32_PFN<<PAGE_SHIFT) &&
	    end > (MAX_DMA32_PFN<<PAGE_SHIFT))
		start = MAX_DMA32_PFN<<PAGE_SHIFT;
	mem = memblock_x86_find_in_range_node(nodeid, start, end, size, align);
	if (mem != MEMBLOCK_ERROR)
		return __va(mem);

	/* extend the search scope */
	end = max_pfn_mapped << PAGE_SHIFT;
	start = MAX_DMA_PFN << PAGE_SHIFT;
	mem = memblock_find_in_range(start, end, size, align);
	if (mem != MEMBLOCK_ERROR)
		return __va(mem);

	printk(KERN_ERR "Cannot find %lu bytes in node %d\n",
		       size, nodeid);

	return NULL;
}

static int __init numa_add_memblk_to(int nid, u64 start, u64 end,
				     struct numa_meminfo *mi)
{
	/* ignore zero length blks */
	if (start == end)
		return 0;

	/* whine about and ignore invalid blks */
	if (start > end || nid < 0 || nid >= MAX_NUMNODES) {
		pr_warning("NUMA: Warning: invalid memblk node %d (%Lx-%Lx)\n",
			   nid, start, end);
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

static void __init numa_remove_memblk_from(int idx, struct numa_meminfo *mi)
{
	mi->nr_blks--;
	memmove(&mi->blk[idx], &mi->blk[idx + 1],
		(mi->nr_blks - idx) * sizeof(mi->blk[0]));
}

int __init numa_add_memblk(int nid, u64 start, u64 end)
{
	return numa_add_memblk_to(nid, start, end, &numa_meminfo);
}

/* Initialize bootmem allocator for a node */
void __init
setup_node_bootmem(int nodeid, unsigned long start, unsigned long end)
{
	unsigned long start_pfn, last_pfn, nodedata_phys;
	const int pgdat_size = roundup(sizeof(pg_data_t), PAGE_SIZE);
	int nid;

	if (!end)
		return;

	/*
	 * Don't confuse VM with a node that doesn't have the
	 * minimum amount of memory:
	 */
	if (end && (end - start) < NODE_MIN_SIZE)
		return;

	start = roundup(start, ZONE_ALIGN);

	printk(KERN_INFO "Initmem setup node %d %016lx-%016lx\n", nodeid,
	       start, end);

	start_pfn = start >> PAGE_SHIFT;
	last_pfn = end >> PAGE_SHIFT;

	node_data[nodeid] = early_node_mem(nodeid, start, end, pgdat_size,
					   SMP_CACHE_BYTES);
	if (node_data[nodeid] == NULL)
		return;
	nodedata_phys = __pa(node_data[nodeid]);
	memblock_x86_reserve_range(nodedata_phys, nodedata_phys + pgdat_size, "NODE_DATA");
	printk(KERN_INFO "  NODE_DATA [%016lx - %016lx]\n", nodedata_phys,
		nodedata_phys + pgdat_size - 1);
	nid = phys_to_nid(nodedata_phys);
	if (nid != nodeid)
		printk(KERN_INFO "    NODE_DATA(%d) on node %d\n", nodeid, nid);

	memset(NODE_DATA(nodeid), 0, sizeof(pg_data_t));
	NODE_DATA(nodeid)->node_id = nodeid;
	NODE_DATA(nodeid)->node_start_pfn = start_pfn;
	NODE_DATA(nodeid)->node_spanned_pages = last_pfn - start_pfn;

	node_set_online(nodeid);
}

static int __init numa_cleanup_meminfo(struct numa_meminfo *mi)
{
	const u64 low = 0;
	const u64 high = (u64)max_pfn << PAGE_SHIFT;
	int i, j, k;

	for (i = 0; i < mi->nr_blks; i++) {
		struct numa_memblk *bi = &mi->blk[i];

		/* make sure all blocks are inside the limits */
		bi->start = max(bi->start, low);
		bi->end = min(bi->end, high);

		/* and there's no empty block */
		if (bi->start == bi->end) {
			numa_remove_memblk_from(i--, mi);
			continue;
		}

		for (j = i + 1; j < mi->nr_blks; j++) {
			struct numa_memblk *bj = &mi->blk[j];
			unsigned long start, end;

			/*
			 * See whether there are overlapping blocks.  Whine
			 * about but allow overlaps of the same nid.  They
			 * will be merged below.
			 */
			if (bi->end > bj->start && bi->start < bj->end) {
				if (bi->nid != bj->nid) {
					pr_err("NUMA: node %d (%Lx-%Lx) overlaps with node %d (%Lx-%Lx)\n",
					       bi->nid, bi->start, bi->end,
					       bj->nid, bj->start, bj->end);
					return -EINVAL;
				}
				pr_warning("NUMA: Warning: node %d (%Lx-%Lx) overlaps with itself (%Lx-%Lx)\n",
					   bi->nid, bi->start, bi->end,
					   bj->start, bj->end);
			}

			/*
			 * Join together blocks on the same node, holes
			 * between which don't overlap with memory on other
			 * nodes.
			 */
			if (bi->nid != bj->nid)
				continue;
			start = max(min(bi->start, bj->start), low);
			end = min(max(bi->end, bj->end), high);
			for (k = 0; k < mi->nr_blks; k++) {
				struct numa_memblk *bk = &mi->blk[k];

				if (bi->nid == bk->nid)
					continue;
				if (start < bk->end && end > bk->start)
					break;
			}
			if (k < mi->nr_blks)
				continue;
			printk(KERN_INFO "NUMA: Node %d [%Lx,%Lx) + [%Lx,%Lx) -> [%lx,%lx)\n",
			       bi->nid, bi->start, bi->end, bj->start, bj->end,
			       start, end);
			bi->start = start;
			bi->end = end;
			numa_remove_memblk_from(j--, mi);
		}
	}

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

/*
 * Reset distance table.  The current table is freed.  The next
 * numa_set_distance() call will create a new one.
 */
static void __init numa_reset_distance(void)
{
	size_t size;

	size = numa_distance_cnt * sizeof(numa_distance[0]);
	memblock_x86_free_range(__pa(numa_distance),
				__pa(numa_distance) + size);
	numa_distance = NULL;
	numa_distance_cnt = 0;
}

/*
 * Set the distance between node @from to @to to @distance.  If distance
 * table doesn't exist, one which is large enough to accomodate all the
 * currently known nodes will be created.
 */
void __init numa_set_distance(int from, int to, int distance)
{
	if (!numa_distance) {
		nodemask_t nodes_parsed;
		size_t size;
		int i, j, cnt = 0;
		u64 phys;

		/* size the new table and allocate it */
		nodes_parsed = numa_nodes_parsed;
		numa_nodemask_from_meminfo(&nodes_parsed, &numa_meminfo);

		for_each_node_mask(i, nodes_parsed)
			cnt = i;
		size = ++cnt * sizeof(numa_distance[0]);

		phys = memblock_find_in_range(0,
					      (u64)max_pfn_mapped << PAGE_SHIFT,
					      size, PAGE_SIZE);
		if (phys == MEMBLOCK_ERROR) {
			pr_warning("NUMA: Warning: can't allocate distance table!\n");
			/* don't retry until explicitly reset */
			numa_distance = (void *)1LU;
			return;
		}
		memblock_x86_reserve_range(phys, phys + size, "NUMA DIST");

		numa_distance = __va(phys);
		numa_distance_cnt = cnt;

		/* fill with the default distances */
		for (i = 0; i < cnt; i++)
			for (j = 0; j < cnt; j++)
				numa_distance[i * cnt + j] = i == j ?
					LOCAL_DISTANCE : REMOTE_DISTANCE;
		printk(KERN_DEBUG "NUMA: Initialized distance table, cnt=%d\n", cnt);
	}

	if (from >= numa_distance_cnt || to >= numa_distance_cnt) {
		printk_once(KERN_DEBUG "NUMA: Debug: distance out of bound, from=%d to=%d distance=%d\n",
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
#if defined(CONFIG_ACPI_NUMA) && defined(CONFIG_NUMA_EMU)
	if (numa_emu_dist)
		return acpi_emu_node_distance(from, to);
#endif
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
	unsigned long numaram, e820ram;
	int i;

	numaram = 0;
	for (i = 0; i < mi->nr_blks; i++) {
		unsigned long s = mi->blk[i].start >> PAGE_SHIFT;
		unsigned long e = mi->blk[i].end >> PAGE_SHIFT;
		numaram += e - s;
		numaram -= __absent_pages_in_range(mi->blk[i].nid, s, e);
		if ((long)numaram < 0)
			numaram = 0;
	}

	e820ram = max_pfn - (memblock_x86_hole_size(0,
					max_pfn << PAGE_SHIFT) >> PAGE_SHIFT);
	/* We seem to lose 3 pages somewhere. Allow 1M of slack. */
	if ((long)(e820ram - numaram) >= (1 << (20 - PAGE_SHIFT))) {
		printk(KERN_ERR "NUMA: nodes only cover %luMB of your %luMB e820 RAM. Not used.\n",
		       (numaram << PAGE_SHIFT) >> 20,
		       (e820ram << PAGE_SHIFT) >> 20);
		return false;
	}
	return true;
}

static int __init numa_register_memblks(struct numa_meminfo *mi)
{
	int i, j, nid;

	/* Account for nodes with cpus and no memory */
	node_possible_map = numa_nodes_parsed;
	numa_nodemask_from_meminfo(&node_possible_map, mi);
	if (WARN_ON(nodes_empty(node_possible_map)))
		return -EINVAL;

	memnode_shift = compute_hash_shift(mi);
	if (memnode_shift < 0) {
		printk(KERN_ERR "NUMA: No NUMA node hash function found. Contact maintainer\n");
		return -EINVAL;
	}

	for (i = 0; i < mi->nr_blks; i++)
		memblock_x86_register_active_regions(mi->blk[i].nid,
					mi->blk[i].start >> PAGE_SHIFT,
					mi->blk[i].end >> PAGE_SHIFT);

	/* for out of order entries */
	sort_node_map();
	if (!numa_meminfo_cover_memory(mi))
		return -EINVAL;

	init_memory_mapping_high();

	/*
	 * Finally register nodes.  Do it twice in case setup_node_bootmem
	 * missed one due to missing bootmem.
	 */
	for (i = 0; i < 2; i++) {
		for_each_node_mask(nid, node_possible_map) {
			u64 start = (u64)max_pfn << PAGE_SHIFT;
			u64 end = 0;

			if (node_online(nid))
				continue;

			for (j = 0; j < mi->nr_blks; j++) {
				if (nid != mi->blk[j].nid)
					continue;
				start = min(mi->blk[j].start, start);
				end = max(mi->blk[j].end, end);
			}

			if (start < end)
				setup_node_bootmem(nid, start, end);
		}
	}

	return 0;
}

#ifdef CONFIG_NUMA_EMU
/* Numa emulation */
static int emu_nid_to_phys[MAX_NUMNODES] __cpuinitdata;
static char *emu_cmdline __initdata;

void __init numa_emu_cmdline(char *str)
{
	emu_cmdline = str;
}

static int __init emu_find_memblk_by_nid(int nid, const struct numa_meminfo *mi)
{
	int i;

	for (i = 0; i < mi->nr_blks; i++)
		if (mi->blk[i].nid == nid)
			return i;
	return -ENOENT;
}

int __init find_node_by_addr(unsigned long addr)
{
	const struct numa_meminfo *mi = &numa_meminfo;
	int i;

	for (i = 0; i < mi->nr_blks; i++) {
		/*
		 * Find the real node that this emulated node appears on.  For
		 * the sake of simplicity, we only use a real node's starting
		 * address to determine which emulated node it appears on.
		 */
		if (addr >= mi->blk[i].start && addr < mi->blk[i].end)
			return mi->blk[i].nid;
	}
	return NUMA_NO_NODE;
}

static void __init fake_physnodes(int acpi, int amd,
				  const struct numa_meminfo *ei)
{
	static struct bootnode nodes[MAX_NUMNODES] __initdata;
	int i, nr_nodes = 0;

	for (i = 0; i < ei->nr_blks; i++) {
		int nid = ei->blk[i].nid;

		if (nodes[nid].start == nodes[nid].end) {
			nodes[nid].start = ei->blk[i].start;
			nodes[nid].end = ei->blk[i].end;
			nr_nodes++;
		} else {
			nodes[nid].start = min(ei->blk[i].start, nodes[nid].start);
			nodes[nid].end = max(ei->blk[i].end, nodes[nid].end);
		}
	}

	BUG_ON(acpi && amd);
#ifdef CONFIG_ACPI_NUMA
	if (acpi)
		acpi_fake_nodes(nodes, nr_nodes);
#endif
#ifdef CONFIG_AMD_NUMA
	if (amd)
		amd_fake_nodes(nodes, nr_nodes);
#endif
	if (!acpi && !amd)
		for (i = 0; i < nr_cpu_ids; i++)
			numa_set_node(i, 0);
}

/*
 * Sets up nid to range from @start to @end.  The return value is -errno if
 * something went wrong, 0 otherwise.
 */
static int __init emu_setup_memblk(struct numa_meminfo *ei,
				   struct numa_meminfo *pi,
				   int nid, int phys_blk, u64 size)
{
	struct numa_memblk *eb = &ei->blk[ei->nr_blks];
	struct numa_memblk *pb = &pi->blk[phys_blk];

	if (ei->nr_blks >= NR_NODE_MEMBLKS) {
		pr_err("NUMA: Too many emulated memblks, failing emulation\n");
		return -EINVAL;
	}

	ei->nr_blks++;
	eb->start = pb->start;
	eb->end = pb->start + size;
	eb->nid = nid;

	if (emu_nid_to_phys[nid] == NUMA_NO_NODE)
		emu_nid_to_phys[nid] = pb->nid;

	pb->start += size;
	if (pb->start >= pb->end) {
		WARN_ON_ONCE(pb->start > pb->end);
		numa_remove_memblk_from(phys_blk, pi);
	}

	printk(KERN_INFO "Faking node %d at %016Lx-%016Lx (%LuMB)\n", nid,
	       eb->start, eb->end, (eb->end - eb->start) >> 20);
	return 0;
}

/*
 * Sets up nr_nodes fake nodes interleaved over physical nodes ranging from addr
 * to max_addr.  The return value is the number of nodes allocated.
 */
static int __init split_nodes_interleave(struct numa_meminfo *ei,
					 struct numa_meminfo *pi,
					 u64 addr, u64 max_addr, int nr_nodes)
{
	nodemask_t physnode_mask = NODE_MASK_NONE;
	u64 size;
	int big;
	int nid = 0;
	int i, ret;

	if (nr_nodes <= 0)
		return -1;
	if (nr_nodes > MAX_NUMNODES) {
		pr_info("numa=fake=%d too large, reducing to %d\n",
			nr_nodes, MAX_NUMNODES);
		nr_nodes = MAX_NUMNODES;
	}

	size = (max_addr - addr - memblock_x86_hole_size(addr, max_addr)) / nr_nodes;
	/*
	 * Calculate the number of big nodes that can be allocated as a result
	 * of consolidating the remainder.
	 */
	big = ((size & ~FAKE_NODE_MIN_HASH_MASK) * nr_nodes) /
		FAKE_NODE_MIN_SIZE;

	size &= FAKE_NODE_MIN_HASH_MASK;
	if (!size) {
		pr_err("Not enough memory for each node.  "
			"NUMA emulation disabled.\n");
		return -1;
	}

	for (i = 0; i < pi->nr_blks; i++)
		node_set(pi->blk[i].nid, physnode_mask);

	/*
	 * Continue to fill physical nodes with fake nodes until there is no
	 * memory left on any of them.
	 */
	while (nodes_weight(physnode_mask)) {
		for_each_node_mask(i, physnode_mask) {
			u64 dma32_end = PFN_PHYS(MAX_DMA32_PFN);
			u64 start, limit, end;
			int phys_blk;

			phys_blk = emu_find_memblk_by_nid(i, pi);
			if (phys_blk < 0) {
				node_clear(i, physnode_mask);
				continue;
			}
			start = pi->blk[phys_blk].start;
			limit = pi->blk[phys_blk].end;
			end = start + size;

			if (nid < big)
				end += FAKE_NODE_MIN_SIZE;

			/*
			 * Continue to add memory to this fake node if its
			 * non-reserved memory is less than the per-node size.
			 */
			while (end - start -
			       memblock_x86_hole_size(start, end) < size) {
				end += FAKE_NODE_MIN_SIZE;
				if (end > limit) {
					end = limit;
					break;
				}
			}

			/*
			 * If there won't be at least FAKE_NODE_MIN_SIZE of
			 * non-reserved memory in ZONE_DMA32 for the next node,
			 * this one must extend to the boundary.
			 */
			if (end < dma32_end && dma32_end - end -
			    memblock_x86_hole_size(end, dma32_end) < FAKE_NODE_MIN_SIZE)
				end = dma32_end;

			/*
			 * If there won't be enough non-reserved memory for the
			 * next node, this one must extend to the end of the
			 * physical node.
			 */
			if (limit - end -
			    memblock_x86_hole_size(end, limit) < size)
				end = limit;

			ret = emu_setup_memblk(ei, pi, nid++ % nr_nodes,
					       phys_blk,
					       min(end, limit) - start);
			if (ret < 0)
				return ret;
		}
	}
	return 0;
}

/*
 * Returns the end address of a node so that there is at least `size' amount of
 * non-reserved memory or `max_addr' is reached.
 */
static u64 __init find_end_of_node(u64 start, u64 max_addr, u64 size)
{
	u64 end = start + size;

	while (end - start - memblock_x86_hole_size(start, end) < size) {
		end += FAKE_NODE_MIN_SIZE;
		if (end > max_addr) {
			end = max_addr;
			break;
		}
	}
	return end;
}

/*
 * Sets up fake nodes of `size' interleaved over physical nodes ranging from
 * `addr' to `max_addr'.  The return value is the number of nodes allocated.
 */
static int __init split_nodes_size_interleave(struct numa_meminfo *ei,
					      struct numa_meminfo *pi,
					      u64 addr, u64 max_addr, u64 size)
{
	nodemask_t physnode_mask = NODE_MASK_NONE;
	u64 min_size;
	int nid = 0;
	int i, ret;

	if (!size)
		return -1;
	/*
	 * The limit on emulated nodes is MAX_NUMNODES, so the size per node is
	 * increased accordingly if the requested size is too small.  This
	 * creates a uniform distribution of node sizes across the entire
	 * machine (but not necessarily over physical nodes).
	 */
	min_size = (max_addr - addr - memblock_x86_hole_size(addr, max_addr)) /
						MAX_NUMNODES;
	min_size = max(min_size, FAKE_NODE_MIN_SIZE);
	if ((min_size & FAKE_NODE_MIN_HASH_MASK) < min_size)
		min_size = (min_size + FAKE_NODE_MIN_SIZE) &
						FAKE_NODE_MIN_HASH_MASK;
	if (size < min_size) {
		pr_err("Fake node size %LuMB too small, increasing to %LuMB\n",
			size >> 20, min_size >> 20);
		size = min_size;
	}
	size &= FAKE_NODE_MIN_HASH_MASK;

	for (i = 0; i < pi->nr_blks; i++)
		node_set(pi->blk[i].nid, physnode_mask);

	/*
	 * Fill physical nodes with fake nodes of size until there is no memory
	 * left on any of them.
	 */
	while (nodes_weight(physnode_mask)) {
		for_each_node_mask(i, physnode_mask) {
			u64 dma32_end = MAX_DMA32_PFN << PAGE_SHIFT;
			u64 start, limit, end;
			int phys_blk;

			phys_blk = emu_find_memblk_by_nid(i, pi);
			if (phys_blk < 0) {
				node_clear(i, physnode_mask);
				continue;
			}
			start = pi->blk[phys_blk].start;
			limit = pi->blk[phys_blk].end;

			end = find_end_of_node(start, limit, size);
			/*
			 * If there won't be at least FAKE_NODE_MIN_SIZE of
			 * non-reserved memory in ZONE_DMA32 for the next node,
			 * this one must extend to the boundary.
			 */
			if (end < dma32_end && dma32_end - end -
			    memblock_x86_hole_size(end, dma32_end) < FAKE_NODE_MIN_SIZE)
				end = dma32_end;

			/*
			 * If there won't be enough non-reserved memory for the
			 * next node, this one must extend to the end of the
			 * physical node.
			 */
			if (limit - end -
			    memblock_x86_hole_size(end, limit) < size)
				end = limit;

			ret = emu_setup_memblk(ei, pi, nid++ % MAX_NUMNODES,
					       phys_blk,
					       min(end, limit) - start);
			if (ret < 0)
				return ret;
		}
	}
	return 0;
}

/*
 * Sets up the system RAM area from start_pfn to last_pfn according to the
 * numa=fake command-line option.
 */
static bool __init numa_emulation(int acpi, int amd)
{
	static struct numa_meminfo ei __initdata;
	static struct numa_meminfo pi __initdata;
	const u64 max_addr = max_pfn << PAGE_SHIFT;
	int i, j, ret;

	memset(&ei, 0, sizeof(ei));
	pi = numa_meminfo;

	for (i = 0; i < MAX_NUMNODES; i++)
		emu_nid_to_phys[i] = NUMA_NO_NODE;

	/*
	 * If the numa=fake command-line contains a 'M' or 'G', it represents
	 * the fixed node size.  Otherwise, if it is just a single number N,
	 * split the system RAM into N fake nodes.
	 */
	if (strchr(emu_cmdline, 'M') || strchr(emu_cmdline, 'G')) {
		u64 size;

		size = memparse(emu_cmdline, &emu_cmdline);
		ret = split_nodes_size_interleave(&ei, &pi, 0, max_addr, size);
	} else {
		unsigned long n;

		n = simple_strtoul(emu_cmdline, NULL, 0);
		ret = split_nodes_interleave(&ei, &pi, 0, max_addr, n);
	}

	if (ret < 0)
		return false;

	if (numa_cleanup_meminfo(&ei) < 0) {
		pr_warning("NUMA: Warning: constructed meminfo invalid, disabling emulation\n");
		return false;
	}

	/* commit */
	numa_meminfo = ei;

	/*
	 * Transform __apicid_to_node table to use emulated nids by
	 * reverse-mapping phys_nid.  The maps should always exist but fall
	 * back to zero just in case.
	 */
	for (i = 0; i < ARRAY_SIZE(__apicid_to_node); i++) {
		if (__apicid_to_node[i] == NUMA_NO_NODE)
			continue;
		for (j = 0; j < ARRAY_SIZE(emu_nid_to_phys); j++)
			if (__apicid_to_node[i] == emu_nid_to_phys[j])
				break;
		__apicid_to_node[i] = j < ARRAY_SIZE(emu_nid_to_phys) ? j : 0;
	}

	/* make sure all emulated nodes are mapped to a physical node */
	for (i = 0; i < ARRAY_SIZE(emu_nid_to_phys); i++)
		if (emu_nid_to_phys[i] == NUMA_NO_NODE)
			emu_nid_to_phys[i] = 0;

	fake_physnodes(acpi, amd, &ei);
	numa_emu_dist = true;
	return true;
}
#endif /* CONFIG_NUMA_EMU */

static int dummy_numa_init(void)
{
	printk(KERN_INFO "%s\n",
	       numa_off ? "NUMA turned off" : "No NUMA configuration found");
	printk(KERN_INFO "Faking a node at %016lx-%016lx\n",
	       0LU, max_pfn << PAGE_SHIFT);

	node_set(0, numa_nodes_parsed);
	numa_add_memblk(0, 0, (u64)max_pfn << PAGE_SHIFT);

	return 0;
}

void __init initmem_init(void)
{
	int (*numa_init[])(void) = { [2] = dummy_numa_init };
	int i, j;

	if (!numa_off) {
#ifdef CONFIG_ACPI_NUMA
		numa_init[0] = x86_acpi_numa_init;
#endif
#ifdef CONFIG_AMD_NUMA
		numa_init[1] = amd_numa_init;
#endif
	}

	for (i = 0; i < ARRAY_SIZE(numa_init); i++) {
		if (!numa_init[i])
			continue;

		for (j = 0; j < MAX_LOCAL_APIC; j++)
			set_apicid_to_node(j, NUMA_NO_NODE);

		nodes_clear(numa_nodes_parsed);
		nodes_clear(node_possible_map);
		nodes_clear(node_online_map);
		memset(&numa_meminfo, 0, sizeof(numa_meminfo));
		remove_all_active_ranges();
		numa_reset_distance();

		if (numa_init[i]() < 0)
			continue;

		if (numa_cleanup_meminfo(&numa_meminfo) < 0)
			continue;
#ifdef CONFIG_NUMA_EMU
		/*
		 * If requested, try emulation.  If emulation is not used,
		 * build identity emu_nid_to_phys[] for numa_add_cpu()
		 */
		if (!emu_cmdline || !numa_emulation(i == 0, i == 1))
			for (j = 0; j < ARRAY_SIZE(emu_nid_to_phys); j++)
				emu_nid_to_phys[j] = j;
#endif
		if (numa_register_memblks(&numa_meminfo) < 0)
			continue;

		for (j = 0; j < nr_cpu_ids; j++) {
			int nid = early_cpu_to_node(j);

			if (nid == NUMA_NO_NODE)
				continue;
			if (!node_online(nid))
				numa_clear_node(j);
		}
		numa_init_array();
		return;
	}
	BUG();
}

unsigned long __init numa_free_all_bootmem(void)
{
	unsigned long pages = 0;
	int i;

	for_each_online_node(i)
		pages += free_all_bootmem_node(NODE_DATA(i));

	pages += free_all_memory_core_early(MAX_NUMNODES);

	return pages;
}

int __cpuinit numa_cpu_node(int cpu)
{
	int apicid = early_per_cpu(x86_cpu_to_apicid, cpu);

	if (apicid != BAD_APICID)
		return __apicid_to_node[apicid];
	return NUMA_NO_NODE;
}

/*
 * UGLINESS AHEAD: Currently, CONFIG_NUMA_EMU is 64bit only and makes use
 * of 64bit specific data structures.  The distinction is artificial and
 * should be removed.  numa_{add|remove}_cpu() are implemented in numa.c
 * for both 32 and 64bit when CONFIG_NUMA_EMU is disabled but here when
 * enabled.
 *
 * NUMA emulation is planned to be made generic and the following and other
 * related code should be moved to numa.c.
 */
#ifdef CONFIG_NUMA_EMU
# ifndef CONFIG_DEBUG_PER_CPU_MAPS
void __cpuinit numa_add_cpu(int cpu)
{
	int physnid, nid;

	nid = numa_cpu_node(cpu);
	if (nid == NUMA_NO_NODE)
		nid = early_cpu_to_node(cpu);
	BUG_ON(nid == NUMA_NO_NODE || !node_online(nid));

	physnid = emu_nid_to_phys[nid];

	/*
	 * Map the cpu to each emulated node that is allocated on the physical
	 * node of the cpu's apic id.
	 */
	for_each_online_node(nid)
		if (emu_nid_to_phys[nid] == physnid)
			cpumask_set_cpu(cpu, node_to_cpumask_map[nid]);
}

void __cpuinit numa_remove_cpu(int cpu)
{
	int i;

	for_each_online_node(i)
		cpumask_clear_cpu(cpu, node_to_cpumask_map[i]);
}
# else	/* !CONFIG_DEBUG_PER_CPU_MAPS */
static void __cpuinit numa_set_cpumask(int cpu, int enable)
{
	struct cpumask *mask;
	int nid, physnid, i;

	nid = early_cpu_to_node(cpu);
	if (nid == NUMA_NO_NODE) {
		/* early_cpu_to_node() already emits a warning and trace */
		return;
	}

	physnid = emu_nid_to_phys[nid];

	for_each_online_node(i) {
		if (emu_nid_to_phys[nid] != physnid)
			continue;

		mask = debug_cpumask_set_cpu(cpu, enable);
		if (!mask)
			return;

		if (enable)
			cpumask_set_cpu(cpu, mask);
		else
			cpumask_clear_cpu(cpu, mask);
	}
}

void __cpuinit numa_add_cpu(int cpu)
{
	numa_set_cpumask(cpu, 1);
}

void __cpuinit numa_remove_cpu(int cpu)
{
	numa_set_cpumask(cpu, 0);
}
# endif	/* !CONFIG_DEBUG_PER_CPU_MAPS */
#endif	/* CONFIG_NUMA_EMU */
