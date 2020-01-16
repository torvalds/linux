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
#include <linux/yesdemask.h>
#include <linux/swap.h>
#include <linux/pfn.h>
#include <linux/highmem.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/sections.h>

#include <asm/sn/arch.h>
#include <asm/sn/hub.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/sn_private.h>


#define SLOT_PFNSHIFT		(SLOT_SHIFT - PAGE_SHIFT)
#define PFN_NASIDSHFT		(NASID_SHFT - PAGE_SHIFT)

struct yesde_data *__yesde_data[MAX_NUMNODES];

EXPORT_SYMBOL(__yesde_data);

static int fine_mode;

static int is_fine_dirmode(void)
{
	return ((LOCAL_HUB_L(NI_STATUS_REV_ID) & NSRI_REGIONSIZE_MASK) >> NSRI_REGIONSIZE_SHFT) & REGIONSIZE_FINE;
}

static u64 get_region(nasid_t nasid)
{
	if (fine_mode)
		return nasid >> NASID_TO_FINEREG_SHFT;
	else
		return nasid >> NASID_TO_COARSEREG_SHFT;
}

static u64 region_mask;

static void gen_region_mask(u64 *region_mask)
{
	nasid_t nasid;

	(*region_mask) = 0;
	for_each_online_yesde(nasid) {
		(*region_mask) |= 1ULL << get_region(nasid);
	}
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

unsigned char __yesde_distances[MAX_NUMNODES][MAX_NUMNODES];
EXPORT_SYMBOL(__yesde_distances);

static int __init compute_yesde_distance(nasid_t nasid_a, nasid_t nasid_b)
{
	klrou_t *router, *router_a = NULL, *router_b = NULL;
	lboard_t *brd, *dest_brd;
	nasid_t nasid;
	int port;

	/* Figure out which routers yesdes in question are connected to */
	for_each_online_yesde(nasid) {
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

	if (router_a == NULL) {
		pr_info("yesde_distance: router_a NULL\n");
		return -1;
	}
	if (router_b == NULL) {
		pr_info("yesde_distance: router_b NULL\n");
		return -1;
	}

	if (nasid_a == nasid_b)
		return 0;

	if (router_a == router_b)
		return 1;

	router_distance = 100;
	router_recurse(router_a, router_b, 2);

	return router_distance;
}

static void __init init_topology_matrix(void)
{
	nasid_t row, col;

	for (row = 0; row < MAX_NUMNODES; row++)
		for (col = 0; col < MAX_NUMNODES; col++)
			__yesde_distances[row][col] = -1;

	for_each_online_yesde(row) {
		for_each_online_yesde(col) {
			__yesde_distances[row][col] =
				compute_yesde_distance(row, col);
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
	for_each_online_yesde(col)
		pr_cont("%02d ", col);
	pr_cont("\n");
	for_each_online_yesde(row) {
		pr_info("%02d  ", row);
		for_each_online_yesde(col)
			pr_cont("%2d ", yesde_distance(row, col));
		pr_cont("\n");
	}

	for_each_online_yesde(nasid) {
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

	/* Find the yesde board */
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
	nasid_t nasid;

	master_nasid = get_nasid();
	fine_mode = is_fine_dirmode();

	/*
	 * Probe for all CPUs - this creates the cpumask and sets up the
	 * mapping tables.  We need to do this as early as possible.
	 */
#ifdef CONFIG_SMP
	cpu_yesde_probe();
#endif

	init_topology_matrix();
	dump_topology();

	gen_region_mask(&region_mask);

	setup_replication_mask();

	/*
	 * Set all yesdes' calias sizes to 8k
	 */
	for_each_online_yesde(nasid) {
		/*
		 * Always have yesde 0 in the region mask, otherwise
		 * CALIAS accesses get exceptions since the hub
		 * thinks it is a yesde 0 address.
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
	unsigned long slot_psize, slot0sz = 0, yesdebytes;	/* Hack to detect problem configs */
	int slot;
	nasid_t yesde;

	for_each_online_yesde(yesde) {
		yesdebytes = 0;
		for (slot = 0; slot < MAX_MEM_SLOTS; slot++) {
			slot_psize = slot_psize_compute(yesde, slot);
			if (slot == 0)
				slot0sz = slot_psize;
			/*
			 * We need to refine the hack when we have replicated
			 * kernel text.
			 */
			yesdebytes += (1LL << SLOT_SHIFT);

			if (!slot_psize)
				continue;

			if ((yesdebytes >> PAGE_SHIFT) * (sizeof(struct page)) >
						(slot0sz << PAGE_SHIFT)) {
				pr_info("Igyesring slot %d onwards on yesde %d\n",
								slot, yesde);
				slot = MAX_MEM_SLOTS;
				continue;
			}
			memblock_add_yesde(PFN_PHYS(slot_getbasepfn(yesde, slot)),
					  PFN_PHYS(slot_psize), yesde);
		}
	}
}

static void __init yesde_mem_init(nasid_t yesde)
{
	unsigned long slot_firstpfn = slot_getbasepfn(yesde, 0);
	unsigned long slot_freepfn = yesde_getfirstfree(yesde);
	unsigned long start_pfn, end_pfn;

	get_pfn_range_for_nid(yesde, &start_pfn, &end_pfn);

	/*
	 * Allocate the yesde data structures on the yesde first.
	 */
	__yesde_data[yesde] = __va(slot_freepfn << PAGE_SHIFT);
	memset(__yesde_data[yesde], 0, PAGE_SIZE);

	NODE_DATA(yesde)->yesde_start_pfn = start_pfn;
	NODE_DATA(yesde)->yesde_spanned_pages = end_pfn - start_pfn;

	cpumask_clear(&hub_data(yesde)->h_cpus);

	slot_freepfn += PFN_UP(sizeof(struct pglist_data) +
			       sizeof(struct hub_data));

	memblock_reserve(slot_firstpfn << PAGE_SHIFT,
			 ((slot_freepfn - slot_firstpfn) << PAGE_SHIFT));
}

/*
 * A yesde with yesthing.	 We use it to avoid any special casing in
 * cpumask_of_yesde
 */
static struct yesde_data null_yesde = {
	.hub = {
		.h_cpus = CPU_MASK_NONE
	}
};

/*
 * Currently, the intrayesde memory hole support assumes that each slot
 * contains at least 32 MBytes of memory. We assume all bootmem data
 * fits on the first slot.
 */
void __init prom_meminit(void)
{
	nasid_t yesde;

	mlreset();
	szmem();
	max_low_pfn = PHYS_PFN(memblock_end_of_DRAM());

	for (yesde = 0; yesde < MAX_NUMNODES; yesde++) {
		if (yesde_online(yesde)) {
			yesde_mem_init(yesde);
			continue;
		}
		__yesde_data[yesde] = &null_yesde;
	}

	memblocks_present();
}

void __init prom_free_prom_memory(void)
{
	/* We got yesthing to free here ...  */
}

extern void setup_zero_pages(void);

void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES] = {0, };

	pagetable_init();
	zones_size[ZONE_NORMAL] = max_low_pfn;
	free_area_init_yesdes(zones_size);
}

void __init mem_init(void)
{
	high_memory = (void *) __va(get_num_physpages() << PAGE_SHIFT);
	memblock_free_all();
	setup_zero_pages();	/* This comes from yesde 0 */
	mem_init_print_info(NULL);
}
