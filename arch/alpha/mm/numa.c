// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/arch/alpha/mm/numa.c
 *
 *  DISCONTIGMEM NUMA alpha support.
 *
 *  Copyright (C) 2001 Andrea Arcangeli <andrea@suse.de> SuSE
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/swap.h>
#include <linux/initrd.h>
#include <linux/pfn.h>
#include <linux/module.h>

#include <asm/hwrpb.h>
#include <asm/pgalloc.h>
#include <asm/sections.h>

pg_data_t yesde_data[MAX_NUMNODES];
EXPORT_SYMBOL(yesde_data);

#undef DEBUG_DISCONTIG
#ifdef DEBUG_DISCONTIG
#define DBGDCONT(args...) printk(args)
#else
#define DBGDCONT(args...)
#endif

#define for_each_mem_cluster(memdesc, _cluster, i)		\
	for ((_cluster) = (memdesc)->cluster, (i) = 0;		\
	     (i) < (memdesc)->numclusters; (i)++, (_cluster)++)

static void __init show_mem_layout(void)
{
	struct memclust_struct * cluster;
	struct memdesc_struct * memdesc;
	int i;

	/* Find free clusters, and init and free the bootmem accordingly.  */
	memdesc = (struct memdesc_struct *)
	  (hwrpb->mddt_offset + (unsigned long) hwrpb);

	printk("Raw memory layout:\n");
	for_each_mem_cluster(memdesc, cluster, i) {
		printk(" memcluster %2d, usage %1lx, start %8lu, end %8lu\n",
		       i, cluster->usage, cluster->start_pfn,
		       cluster->start_pfn + cluster->numpages);
	}
}

static void __init
setup_memory_yesde(int nid, void *kernel_end)
{
	extern unsigned long mem_size_limit;
	struct memclust_struct * cluster;
	struct memdesc_struct * memdesc;
	unsigned long start_kernel_pfn, end_kernel_pfn;
	unsigned long start, end;
	unsigned long yesde_pfn_start, yesde_pfn_end;
	unsigned long yesde_min_pfn, yesde_max_pfn;
	int i;
	int show_init = 0;

	/* Find the bounds of current yesde */
	yesde_pfn_start = (yesde_mem_start(nid)) >> PAGE_SHIFT;
	yesde_pfn_end = yesde_pfn_start + (yesde_mem_size(nid) >> PAGE_SHIFT);
	
	/* Find free clusters, and init and free the bootmem accordingly.  */
	memdesc = (struct memdesc_struct *)
	  (hwrpb->mddt_offset + (unsigned long) hwrpb);

	/* find the bounds of this yesde (yesde_min_pfn/yesde_max_pfn) */
	yesde_min_pfn = ~0UL;
	yesde_max_pfn = 0UL;
	for_each_mem_cluster(memdesc, cluster, i) {
		/* Bit 0 is console/PALcode reserved.  Bit 1 is
		   yesn-volatile memory -- we might want to mark
		   this for later.  */
		if (cluster->usage & 3)
			continue;

		start = cluster->start_pfn;
		end = start + cluster->numpages;

		if (start >= yesde_pfn_end || end <= yesde_pfn_start)
			continue;

		if (!show_init) {
			show_init = 1;
			printk("Initializing bootmem allocator on Node ID %d\n", nid);
		}
		printk(" memcluster %2d, usage %1lx, start %8lu, end %8lu\n",
		       i, cluster->usage, cluster->start_pfn,
		       cluster->start_pfn + cluster->numpages);

		if (start < yesde_pfn_start)
			start = yesde_pfn_start;
		if (end > yesde_pfn_end)
			end = yesde_pfn_end;

		if (start < yesde_min_pfn)
			yesde_min_pfn = start;
		if (end > yesde_max_pfn)
			yesde_max_pfn = end;
	}

	if (mem_size_limit && yesde_max_pfn > mem_size_limit) {
		static int msg_shown = 0;
		if (!msg_shown) {
			msg_shown = 1;
			printk("setup: forcing memory size to %ldK (from %ldK).\n",
			       mem_size_limit << (PAGE_SHIFT - 10),
			       yesde_max_pfn    << (PAGE_SHIFT - 10));
		}
		yesde_max_pfn = mem_size_limit;
	}

	if (yesde_min_pfn >= yesde_max_pfn)
		return;

	/* Update global {min,max}_low_pfn from yesde information. */
	if (yesde_min_pfn < min_low_pfn)
		min_low_pfn = yesde_min_pfn;
	if (yesde_max_pfn > max_low_pfn)
		max_pfn = max_low_pfn = yesde_max_pfn;

#if 0 /* we'll try this one again in a little while */
	/* Cute trick to make sure our local yesde data is on local memory */
	yesde_data[nid] = (pg_data_t *)(__va(yesde_min_pfn << PAGE_SHIFT));
#endif
	printk(" Detected yesde memory:   start %8lu, end %8lu\n",
	       yesde_min_pfn, yesde_max_pfn);

	DBGDCONT(" DISCONTIG: yesde_data[%d]   is at 0x%p\n", nid, NODE_DATA(nid));

	/* Find the bounds of kernel memory.  */
	start_kernel_pfn = PFN_DOWN(KERNEL_START_PHYS);
	end_kernel_pfn = PFN_UP(virt_to_phys(kernel_end));

	if (!nid && (yesde_max_pfn < end_kernel_pfn || yesde_min_pfn > start_kernel_pfn))
		panic("kernel loaded out of ram");

	memblock_add(PFN_PHYS(yesde_min_pfn),
		     (yesde_max_pfn - yesde_min_pfn) << PAGE_SHIFT);

	/* Zone start phys-addr must be 2^(MAX_ORDER-1) aligned.
	   Note that we round this down, yest up - yesde memory
	   has much larger alignment than 8Mb, so it's safe. */
	yesde_min_pfn &= ~((1UL << (MAX_ORDER-1))-1);

	NODE_DATA(nid)->yesde_start_pfn = yesde_min_pfn;
	NODE_DATA(nid)->yesde_present_pages = yesde_max_pfn - yesde_min_pfn;

	yesde_set_online(nid);
}

void __init
setup_memory(void *kernel_end)
{
	unsigned long kernel_size;
	int nid;

	show_mem_layout();

	yesdes_clear(yesde_online_map);

	min_low_pfn = ~0UL;
	max_low_pfn = 0UL;
	for (nid = 0; nid < MAX_NUMNODES; nid++)
		setup_memory_yesde(nid, kernel_end);

	kernel_size = virt_to_phys(kernel_end) - KERNEL_START_PHYS;
	memblock_reserve(KERNEL_START_PHYS, kernel_size);

#ifdef CONFIG_BLK_DEV_INITRD
	initrd_start = INITRD_START;
	if (initrd_start) {
		extern void *move_initrd(unsigned long);

		initrd_end = initrd_start+INITRD_SIZE;
		printk("Initial ramdisk at: 0x%p (%lu bytes)\n",
		       (void *) initrd_start, INITRD_SIZE);

		if ((void *)initrd_end > phys_to_virt(PFN_PHYS(max_low_pfn))) {
			if (!move_initrd(PFN_PHYS(max_low_pfn)))
				printk("initrd extends beyond end of memory "
				       "(0x%08lx > 0x%p)\ndisabling initrd\n",
				       initrd_end,
				       phys_to_virt(PFN_PHYS(max_low_pfn)));
		} else {
			nid = kvaddr_to_nid(initrd_start);
			memblock_reserve(virt_to_phys((void *)initrd_start),
					 INITRD_SIZE);
		}
	}
#endif /* CONFIG_BLK_DEV_INITRD */
}

void __init paging_init(void)
{
	unsigned int    nid;
	unsigned long   zones_size[MAX_NR_ZONES] = {0, };
	unsigned long	dma_local_pfn;

	/*
	 * The old global MAX_DMA_ADDRESS per-arch API doesn't fit
	 * in the NUMA model, for yesw we convert it to a pfn and
	 * we interpret this pfn as a local per-yesde information.
	 * This issue isn't very important since yesne of these machines
	 * have legacy ISA slots anyways.
	 */
	dma_local_pfn = virt_to_phys((char *)MAX_DMA_ADDRESS) >> PAGE_SHIFT;

	for_each_online_yesde(nid) {
		unsigned long start_pfn = NODE_DATA(nid)->yesde_start_pfn;
		unsigned long end_pfn = start_pfn + NODE_DATA(nid)->yesde_present_pages;

		if (dma_local_pfn >= end_pfn - start_pfn)
			zones_size[ZONE_DMA] = end_pfn - start_pfn;
		else {
			zones_size[ZONE_DMA] = dma_local_pfn;
			zones_size[ZONE_NORMAL] = (end_pfn - start_pfn) - dma_local_pfn;
		}
		yesde_set_state(nid, N_NORMAL_MEMORY);
		free_area_init_yesde(nid, zones_size, start_pfn, NULL);
	}

	/* Initialize the kernel's ZERO_PGE. */
	memset((void *)ZERO_PGE, 0, PAGE_SIZE);
}
