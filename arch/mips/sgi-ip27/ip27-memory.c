/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000, 05 by Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2000 by Silicon Graphics, Inc.
 * Copyright (C) 2004 by Christoph Hellwig
 *
 * On SGI IP27 the ARC memory configuration data is completely bogus but
 * alternate easier to use mechanisms are available.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/export.h>
#include <linux/nodemask.h>
#include <linux/swap.h>
#include <linux/pfn.h>
#include <linux/highmem.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/sections.h>
#include <asm/sgialib.h>

#include <asm/sn/arch.h>
#include <asm/sn/agent.h>
#include <asm/sn/klconfig.h>

#include "ip27-common.h"

#define SLOT_PFNSHIFT		(SLOT_SHIFT - PAGE_SHIFT)
#define PFN_NASIDSHFT		(NASID_SHFT - PAGE_SHIFT)

struct node_data *__node_data[MAX_NUMNODES];

EXPORT_SYMBOL(__node_data);

static u64 gen_region_mask(void)
{
	int region_shift;
	u64 region_mask;
	nasid_t nasid;

	region_shift = get_region_shift();
	region_mask = 0;
	for_each_online_node(nasid)
		region_mask |= BIT_ULL(nasid >> region_shift);

	return region_mask;
}

#define rou_rflag	rou_flags

static int router_distance;

static void router_recurse(klrou_t *router_a, klrou_t *router_b, int depth)
{
	klrou_t *router;
	lboard_t *brd;
	int	port;

	if (router_a->rou_rflag == 1)
		return;

	if (depth >= router_distance)
		return;

	router_a->rou_rflag = 1;

	for (port = 1; port <= MAX_ROUTER_PORTS; port++) {
		if (router_a->rou_port[port].port_nasid == INVALID_NASID)
			continue;

		brd = (lboard_t *)NODE_OFFSET_TO_K0(
			router_a->rou_port[port].port_nasid,
			router_a->rou_port[port].port_offset);

		if (brd->brd_type == KLTYPE_ROUTER) {
			router = (klrou_t *)NODE_OFFSET_TO_K0(NASID_GET(brd), brd->brd_compts[0]);
			if (router == router_b) {
				if (depth < router_distance)
					router_distance = depth;
			}
			else
				router_recurse(router, router_b, depth + 1);
		}
	}

	router_a->rou_rflag = 0;
}

unsigned char __node_distances[MAX_NUMNODES][MAX_NUMNODES];
EXPORT_SYMBOL(__node_distances);

static int __init compute_node_distance(nasid_t nasid_a, nasid_t nasid_b)
{
	klrou_t *router, *router_a = NULL, *router_b = NULL;
	lboard_t *brd, *dest_brd;
	nasid_t nasid;
	int port;

	/* Figure out which routers nodes in question are connected to */
	for_each_online_node(nasid) {
		brd = find_lboard_class((lboard_t *)KL_CONFIG_INFO(nasid),
					KLTYPE_ROUTER);

		if (!brd)
			continue;

		do {
			if (brd->brd_flags & DUPLICATE_BOARD)
				continue;

			router = (klrou_t *)NODE_OFFSET_TO_K0(NASID_GET(brd), brd->brd_compts[0]);
			router->rou_rflag = 0;

			for (port = 1; port <= MAX_ROUTER_PORTS; port++) {
				if (router->rou_port[port].port_nasid == INVALID_NASID)
					continue;

				dest_brd = (lboard_t *)NODE_OFFSET_TO_K0(
					router->rou_port[port].port_nasid,
					router->rou_port[port].port_offset);

				if (dest_brd->brd_type == KLTYPE_IP27) {
					if (dest_brd->brd_nasid == nasid_a)
						router_a = router;
					if (dest_brd->brd_nasid == nasid_b)
						router_b = router;
				}
			}

		} while ((brd = find_lboard_class(KLCF_NEXT(brd), KLTYPE_ROUTER)));
	}

	if (nasid_a == nasid_b)
		return LOCAL_DISTANCE;

	if (router_a == router_b)
		return LOCAL_DISTANCE + 1;

	if (router_a == NULL) {
		pr_info("node_distance: router_a NULL\n");
		return 255;
	}
	if (router_b == NULL) {
		pr_info("node_distance: router_b NULL\n");
		return 255;
	}

	router_distance = 100;
	router_recurse(router_a, router_b, 2);

	return LOCAL_DISTANCE + router_distance;
}

static void __init init_topology_matrix(void)
{
	nasid_t row, col;

	for (row = 0; row < MAX_NUMNODES; row++)
		for (col = 0; col < MAX_NUMNODES; col++)
			__node_distances[row][col] = -1;

	for_each_online_node(row) {
		for_each_online_node(col) {
			__node_distances[row][col] =
				compute_node_distance(row, col);
		}
	}
}

static void __init dump_topology(void)
{
	nasid_t nasid;
	lboard_t *brd, *dest_brd;
	int port;
	int router_num = 0;
	klrou_t *router;
	nasid_t row, col;

	pr_info("************** Topology ********************\n");

	pr_info("    ");
	for_each_online_node(col)
		pr_cont("%02d ", col);
	pr_cont("\n");
	for_each_online_node(row) {
		pr_info("%02d  ", row);
		for_each_online_node(col)
			pr_cont("%2d ", node_distance(row, col));
		pr_cont("\n");
	}

	for_each_online_node(nasid) {
		brd = find_lboard_class((lboard_t *)KL_CONFIG_INFO(nasid),
					KLTYPE_ROUTER);

		if (!brd)
			continue;

		do {
			if (brd->brd_flags & DUPLICATE_BOARD)
				continue;
			pr_cont("Router %d:", router_num);
			router_num++;

			router = (klrou_t *)NODE_OFFSET_TO_K0(NASID_GET(brd), brd->brd_compts[0]);

			for (port = 1; port <= MAX_ROUTER_PORTS; port++) {
				if (router->rou_port[port].port_nasid == INVALID_NASID)
					continue;

				dest_brd = (lboard_t *)NODE_OFFSET_TO_K0(
					router->rou_port[port].port_nasid,
					router->rou_port[port].port_offset);

				if (dest_brd->brd_type == KLTYPE_IP27)
					pr_cont(" %d", dest_brd->brd_nasid);
				if (dest_brd->brd_type == KLTYPE_ROUTER)
					pr_cont(" r");
			}
			pr_cont("\n");

		} while ( (brd = find_lboard_class(KLCF_NEXT(brd), KLTYPE_ROUTER)) );
	}
}

static unsigned long __init slot_getbasepfn(nasid_t nasid, int slot)
{
	return ((unsigned long)nasid << PFN_NASIDSHFT) | (slot << SLOT_PFNSHIFT);
}

static unsigned long __init slot_psize_compute(nasid_t nasid, int slot)
{
	lboard_t *brd;
	klmembnk_t *banks;
	unsigned long size;

	/* Find the node board */
	brd = find_lboard((lboard_t *)KL_CONFIG_INFO(nasid), KLTYPE_IP27);
	if (!brd)
		return 0;

	/* Get the memory bank structure */
	banks = (klmembnk_t *) find_first_component(brd, KLSTRUCT_MEMBNK);
	if (!banks)
		return 0;

	/* Size in _Megabytes_ */
	size = (unsigned long)banks->membnk_bnksz[slot/4];

	/* hack for 128 dimm banks */
	if (size <= 128) {
		if (slot % 4 == 0) {
			size <<= 20;		/* size in bytes */
			return size >> PAGE_SHIFT;
		} else
			return 0;
	} else {
		size /= 4;
		size <<= 20;
		return size >> PAGE_SHIFT;
	}
}

static void __init mlreset(void)
{
	u64 region_mask;
	nasid_t nasid;

	master_nasid = get_nasid();

	/*
	 * Probe for all CPUs - this creates the cpumask and sets up the
	 * mapping tables.  We need to do this as early as possible.
	 */
#ifdef CONFIG_SMP
	cpu_node_probe();
#endif

	init_topology_matrix();
	dump_topology();

	region_mask = gen_region_mask();

	setup_replication_mask();

	/*
	 * Set all nodes' calias sizes to 8k
	 */
	for_each_online_node(nasid) {
		/*
		 * Always have node 0 in the region mask, otherwise
		 * CALIAS accesses get exceptions since the hub
		 * thinks it is a node 0 address.
		 */
		REMOTE_HUB_S(nasid, PI_REGION_PRESENT, (region_mask | 1));
		REMOTE_HUB_S(nasid, PI_CALIAS_SIZE, PI_CALIAS_SIZE_0);

#ifdef LATER
		/*
		 * Set up all hubs to have a big window pointing at
		 * widget 0. Memory mode, widget 0, offset 0
		 */
		REMOTE_HUB_S(nasid, IIO_ITTE(SWIN0_BIGWIN),
			((HUB_PIO_MAP_TO_MEM << IIO_ITTE_IOSP_SHIFT) |
			(0 << IIO_ITTE_WIDGET_SHIFT)));
#endif
	}
}

static void __init szmem(void)
{
	unsigned long slot_psize, slot0sz = 0, nodebytes;	/* Hack to detect problem configs */
	int slot;
	nasid_t node;

	for_each_online_node(node) {
		nodebytes = 0;
		for (slot = 0; slot < MAX_MEM_SLOTS; slot++) {
			slot_psize = slot_psize_compute(node, slot);
			if (slot == 0)
				slot0sz = slot_psize;
			/*
			 * We need to refine the hack when we have replicated
			 * kernel text.
			 */
			nodebytes += (1LL << SLOT_SHIFT);

			if (!slot_psize)
				continue;

			if ((nodebytes >> PAGE_SHIFT) * (sizeof(struct page)) >
						(slot0sz << PAGE_SHIFT)) {
				pr_info("Ignoring slot %d onwards on node %d\n",
								slot, node);
				slot = MAX_MEM_SLOTS;
				continue;
			}
			memblock_add_node(PFN_PHYS(slot_getbasepfn(node, slot)),
					  PFN_PHYS(slot_psize), node,
					  MEMBLOCK_NONE);
		}
	}
}

static void __init node_mem_init(nasid_t node)
{
	unsigned long slot_firstpfn = slot_getbasepfn(node, 0);
	unsigned long slot_freepfn = node_getfirstfree(node);
	unsigned long start_pfn, end_pfn;

	get_pfn_range_for_nid(node, &start_pfn, &end_pfn);

	/*
	 * Allocate the node data structures on the node first.
	 */
	__node_data[node] = __va(slot_freepfn << PAGE_SHIFT);
	memset(__node_data[node], 0, PAGE_SIZE);

	NODE_DATA(node)->node_start_pfn = start_pfn;
	NODE_DATA(node)->node_spanned_pages = end_pfn - start_pfn;

	cpumask_clear(&hub_data(node)->h_cpus);

	slot_freepfn += PFN_UP(sizeof(struct pglist_data) +
			       sizeof(struct hub_data));

	memblock_reserve(slot_firstpfn << PAGE_SHIFT,
			 ((slot_freepfn - slot_firstpfn) << PAGE_SHIFT));
}

/*
 * A node with nothing.	 We use it to avoid any special casing in
 * cpumask_of_node
 */
static struct node_data null_node = {
	.hub = {
		.h_cpus = CPU_MASK_NONE
	}
};

/*
 * Currently, the intranode memory hole support assumes that each slot
 * contains at least 32 MBytes of memory. We assume all bootmem data
 * fits on the first slot.
 */
void __init prom_meminit(void)
{
	nasid_t node;

	mlreset();
	szmem();
	max_low_pfn = PHYS_PFN(memblock_end_of_DRAM());

	for (node = 0; node < MAX_NUMNODES; node++) {
		if (node_online(node)) {
			node_mem_init(node);
			continue;
		}
		__node_data[node] = &null_node;
	}
}

extern void setup_zero_pages(void);

void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES] = {0, };

	pagetable_init();
	zones_size[ZONE_NORMAL] = max_low_pfn;
	free_area_init(zones_size);
}

void __init mem_init(void)
{
	high_memory = (void *) __va(get_num_physpages() << PAGE_SHIFT);
	memblock_free_all();
	setup_zero_pages();	/* This comes from node 0 */
}

pg_data_t * __init arch_alloc_nodedata(int nid)
{
	return memblock_alloc(sizeof(pg_data_t), SMP_CACHE_BYTES);
}

void arch_refresh_nodedata(int nid, pg_data_t *pgdat)
{
	__node_data[nid] = (struct node_data *)pgdat;
}
