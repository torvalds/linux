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
#include <linux/bootmem.h>
#include <linux/swap.h>
#include <linux/initrd.h>
#include <linux/pfn.h>
#include <linux/module.h>

#include <asm/hwrpb.h>
#include <asm/pgalloc.h>

pg_data_t node_data[MAX_NUMNODES];
EXPORT_SYMBOL(node_data);

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
setup_memory_node(int nid, void *kernel_end)
{
	extern unsigned long mem_size_limit;
	struct memclust_struct * cluster;
	struct memdesc_struct * memdesc;
	unsigned long start_kernel_pfn, end_kernel_pfn;
	unsigned long bootmap_size, bootmap_pages, bootmap_start;
	unsigned long start, end;
	unsigned long node_pfn_start, node_pfn_end;
	unsigned long node_min_pfn, node_max_pfn;
	int i;
	unsigned long node_datasz = PFN_UP(sizeof(pg_data_t));
	int show_init = 0;

	/* Find the bounds of current node */
	node_pfn_start = (node_mem_start(nid)) >> PAGE_SHIFT;
	node_pfn_end = node_pfn_start + (node_mem_size(nid) >> PAGE_SHIFT);
	
	/* Find free clusters, and init and free the bootmem accordingly.  */
	memdesc = (struct memdesc_struct *)
	  (hwrpb->mddt_offset + (unsigned long) hwrpb);

	/* find the bounds of this node (node_min_pfn/node_max_pfn) */
	node_min_pfn = ~0UL;
	node_max_pfn = 0UL;
	for_each_mem_cluster(memdesc, cluster, i) {
		/* Bit 0 is console/PALcode reserved.  Bit 1 is
		   non-volatile memory -- we might want to mark
		   this for later.  */
		if (cluster->usage & 3)
			continue;

		start = cluster->start_pfn;
		end = start + cluster->numpages;

		if (start >= node_pfn_end || end <= node_pfn_start)
			continue;

		if (!show_init) {
			show_init = 1;
			printk("Initializing bootmem allocator on Node ID %d\n", nid);
		}
		printk(" memcluster %2d, usage %1lx, start %8lu, end %8lu\n",
		       i, cluster->usage, cluster->start_pfn,
		       cluster->start_pfn + cluster->numpages);

		if (start < node_pfn_start)
			start = node_pfn_start;
		if (end > node_pfn_end)
			end = node_pfn_end;

		if (start < node_min_pfn)
			node_min_pfn = start;
		if (end > node_max_pfn)
			node_max_pfn = end;
	}

	if (mem_size_limit && node_max_pfn > mem_size_limit) {
		static int msg_shown = 0;
		if (!msg_shown) {
			msg_shown = 1;
			printk("setup: forcing memory size to %ldK (from %ldK).\n",
			       mem_size_limit << (PAGE_SHIFT - 10),
			       node_max_pfn    << (PAGE_SHIFT - 10));
		}
		node_max_pfn = mem_size_limit;
	}

	if (node_min_pfn >= node_max_pfn)
		return;

	/* Update global {min,max}_low_pfn from node information. */
	if (node_min_pfn < min_low_pfn)
		min_low_pfn = node_min_pfn;
	if (node_max_pfn > max_low_pfn)
		max_pfn = max_low_pfn = node_max_pfn;

	num_physpages += node_max_pfn - node_min_pfn;

#if 0 /* we'll try this one again in a little while */
	/* Cute trick to make sure our local node data is on local memory */
	node_data[nid] = (pg_data_t *)(__va(node_min_pfn << PAGE_SHIFT));
#endif
	/* Quasi-mark the pg_data_t as in-use */
	node_min_pfn += node_datasz;
	if (node_min_pfn >= node_max_pfn) {
		printk(" not enough mem to reserve NODE_DATA");
		return;
	}
	NODE_DATA(nid)->bdata = &bootmem_node_data[nid];

	printk(" Detected node memory:   start %8lu, end %8lu\n",
	       node_min_pfn, node_max_pfn);

	DBGDCONT(" DISCONTIG: node_data[%d]   is at 0x%p\n", nid, NODE_DATA(nid));
	DBGDCONT(" DISCONTIG: NODE_DATA(%d)->bdata is at 0x%p\n", nid, NODE_DATA(nid)->bdata);

	/* Find the bounds of kernel memory.  */
	start_kernel_pfn = PFN_DOWN(KERNEL_START_PHYS);
	end_kernel_pfn = PFN_UP(virt_to_phys(kernel_end));
	bootmap_start = -1;

	if (!nid && (node_max_pfn < end_kernel_pfn || node_min_pfn > start_kernel_pfn))
		panic("kernel loaded out of ram");

	/* Zone start phys-addr must be 2^(MAX_ORDER-1) aligned.
	   Note that we round this down, not up - node memory
	   has much larger alignment than 8Mb, so it's safe. */
	node_min_pfn &= ~((1UL << (MAX_ORDER-1))-1);

	/* We need to know how many physically contiguous pages
	   we'll need for the bootmap.  */
	bootmap_pages = bootmem_bootmap_pages(node_max_pfn-node_min_pfn);

	/* Now find a good region where to allocate the bootmap.  */
	for_each_mem_cluster(memdesc, cluster, i) {
		if (cluster->usage & 3)
			continue;

		start = cluster->start_pfn;
		end = start + cluster->numpages;

		if (start >= node_max_pfn || end <= node_min_pfn)
			continue;

		if (end > node_max_pfn)
			end = node_max_pfn;
		if (start < node_min_pfn)
			start = node_min_pfn;

		if (start < start_kernel_pfn) {
			if (end > end_kernel_pfn
			    && end - end_kernel_pfn >= bootmap_pages) {
				bootmap_start = end_kernel_pfn;
				break;
			} else if (end > start_kernel_pfn)
				end = start_kernel_pfn;
		} else if (start < end_kernel_pfn)
			start = end_kernel_pfn;
		if (end - start >= bootmap_pages) {
			bootmap_start = start;
			break;
		}
	}

	if (bootmap_start == -1)
		panic("couldn't find a contiguous place for the bootmap");

	/* Allocate the bootmap and mark the whole MM as reserved.  */
	bootmap_size = init_bootmem_node(NODE_DATA(nid), bootmap_start,
					 node_min_pfn, node_max_pfn);
	DBGDCONT(" bootmap_start %lu, bootmap_size %lu, bootmap_pages %lu\n",
		 bootmap_start, bootmap_size, bootmap_pages);

	/* Mark the free regions.  */
	for_each_mem_cluster(memdesc, cluster, i) {
		if (cluster->usage & 3)
			continue;

		start = cluster->start_pfn;
		end = cluster->start_pfn + cluster->numpages;

		if (start >= node_max_pfn || end <= node_min_pfn)
			continue;

		if (end > node_max_pfn)
			end = node_max_pfn;
		if (start < node_min_pfn)
			start = node_min_pfn;

		if (start < start_kernel_pfn) {
			if (end > end_kernel_pfn) {
				free_bootmem_node(NODE_DATA(nid), PFN_PHYS(start),
					     (PFN_PHYS(start_kernel_pfn)
					      - PFN_PHYS(start)));
				printk(" freeing pages %ld:%ld\n",
				       start, start_kernel_pfn);
				start = end_kernel_pfn;
			} else if (end > start_kernel_pfn)
				end = start_kernel_pfn;
		} else if (start < end_kernel_pfn)
			start = end_kernel_pfn;
		if (start >= end)
			continue;

		free_bootmem_node(NODE_DATA(nid), PFN_PHYS(start), PFN_PHYS(end) - PFN_PHYS(start));
		printk(" freeing pages %ld:%ld\n", start, end);
	}

	/* Reserve the bootmap memory.  */
	reserve_bootmem_node(NODE_DATA(nid), PFN_PHYS(bootmap_start),
			bootmap_size, BOOTMEM_DEFAULT);
	printk(" reserving pages %ld:%ld\n", bootmap_start, bootmap_start+PFN_UP(bootmap_size));

	node_set_online(nid);
}

void __init
setup_memory(void *kernel_end)
{
	int nid;

	show_mem_layout();

	nodes_clear(node_online_map);

	min_low_pfn = ~0UL;
	max_low_pfn = 0UL;
	for (nid = 0; nid < MAX_NUMNODES; nid++)
		setup_memory_node(nid, kernel_end);

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
			reserve_bootmem_node(NODE_DATA(nid),
					     virt_to_phys((void *)initrd_start),
					     INITRD_SIZE, BOOTMEM_DEFAULT);
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
	 * in the NUMA model, for now we convert it to a pfn and
	 * we interpret this pfn as a local per-node information.
	 * This issue isn't very important since none of these machines
	 * have legacy ISA slots anyways.
	 */
	dma_local_pfn = virt_to_phys((char *)MAX_DMA_ADDRESS) >> PAGE_SHIFT;

	for_each_online_node(nid) {
		bootmem_data_t *bdata = &bootmem_node_data[nid];
		unsigned long start_pfn = bdata->node_min_pfn;
		unsigned long end_pfn = bdata->node_low_pfn;

		if (dma_local_pfn >= end_pfn - start_pfn)
			zones_size[ZONE_DMA] = end_pfn - start_pfn;
		else {
			zones_size[ZONE_DMA] = dma_local_pfn;
			zones_size[ZONE_NORMAL] = (end_pfn - start_pfn) - dma_local_pfn;
		}
		node_set_state(nid, N_NORMAL_MEMORY);
		free_area_init_node(nid, zones_size, start_pfn, NULL);
	}

	/* Initialize the kernel's ZERO_PGE. */
	memset((void *)ZERO_PGE, 0, PAGE_SIZE);
}

void __init mem_init(void)
{
	unsigned long codesize, reservedpages, datasize, initsize, pfn;
	extern int page_is_ram(unsigned long) __init;
	extern char _text, _etext, _data, _edata;
	extern char __init_begin, __init_end;
	unsigned long nid, i;
	high_memory = (void *) __va(max_low_pfn << PAGE_SHIFT);

	reservedpages = 0;
	for_each_online_node(nid) {
		/*
		 * This will free up the bootmem, ie, slot 0 memory
		 */
		totalram_pages += free_all_bootmem_node(NODE_DATA(nid));

		pfn = NODE_DATA(nid)->node_start_pfn;
		for (i = 0; i < node_spanned_pages(nid); i++, pfn++)
			if (page_is_ram(pfn) &&
			    PageReserved(nid_page_nr(nid, i)))
				reservedpages++;
	}

	codesize =  (unsigned long) &_etext - (unsigned long) &_text;
	datasize =  (unsigned long) &_edata - (unsigned long) &_data;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;

	printk("Memory: %luk/%luk available (%luk kernel code, %luk reserved, "
	       "%luk data, %luk init)\n",
	       nr_free_pages() << (PAGE_SHIFT-10),
	       num_physpages << (PAGE_SHIFT-10),
	       codesize >> 10,
	       reservedpages << (PAGE_SHIFT-10),
	       datasize >> 10,
	       initsize >> 10);
#if 0
	mem_stress();
#endif
}
