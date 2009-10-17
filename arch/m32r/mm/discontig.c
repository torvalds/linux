/*
 *  linux/arch/m32r/mm/discontig.c
 *
 *  Discontig memory support
 *
 *  Copyright (c) 2003  Hitoshi Yamamoto
 */

#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/mmzone.h>
#include <linux/initrd.h>
#include <linux/nodemask.h>
#include <linux/module.h>
#include <linux/pfn.h>

#include <asm/setup.h>

extern char _end[];

struct pglist_data *node_data[MAX_NUMNODES];
EXPORT_SYMBOL(node_data);

pg_data_t m32r_node_data[MAX_NUMNODES];

/* Memory profile */
typedef struct {
	unsigned long start_pfn;
	unsigned long pages;
	unsigned long holes;
	unsigned long free_pfn;
} mem_prof_t;
static mem_prof_t mem_prof[MAX_NUMNODES];

extern unsigned long memory_start;
extern unsigned long memory_end;

static void __init mem_prof_init(void)
{
	unsigned long start_pfn, holes, free_pfn;
	const unsigned long zone_alignment = 1UL << (MAX_ORDER - 1);
	unsigned long ul;
	mem_prof_t *mp;

	/* Node#0 SDRAM */
	mp = &mem_prof[0];
	mp->start_pfn = PFN_UP(CONFIG_MEMORY_START);
	mp->pages = PFN_DOWN(memory_end - memory_start);
	mp->holes = 0;
	mp->free_pfn = PFN_UP(__pa(_end));

	/* Node#1 internal SRAM */
	mp = &mem_prof[1];
	start_pfn = free_pfn = PFN_UP(CONFIG_IRAM_START);
	holes = 0;
	if (start_pfn & (zone_alignment - 1)) {
		ul = zone_alignment;
		while (start_pfn >= ul)
			ul += zone_alignment;

		start_pfn = ul - zone_alignment;
		holes = free_pfn - start_pfn;
	}

	mp->start_pfn = start_pfn;
	mp->pages = PFN_DOWN(CONFIG_IRAM_SIZE) + holes;
	mp->holes = holes;
	mp->free_pfn = PFN_UP(CONFIG_IRAM_START);
}

unsigned long __init setup_memory(void)
{
	unsigned long bootmap_size;
	unsigned long min_pfn;
	int nid;
	mem_prof_t *mp;

	max_low_pfn = 0;
	min_low_pfn = -1;

	mem_prof_init();

	for_each_online_node(nid) {
		mp = &mem_prof[nid];
		NODE_DATA(nid)=(pg_data_t *)&m32r_node_data[nid];
		NODE_DATA(nid)->bdata = &bootmem_node_data[nid];
		min_pfn = mp->start_pfn;
		max_pfn = mp->start_pfn + mp->pages;
		bootmap_size = init_bootmem_node(NODE_DATA(nid), mp->free_pfn,
			mp->start_pfn, max_pfn);

		free_bootmem_node(NODE_DATA(nid), PFN_PHYS(mp->start_pfn),
			PFN_PHYS(mp->pages));

		reserve_bootmem_node(NODE_DATA(nid), PFN_PHYS(mp->start_pfn),
			PFN_PHYS(mp->free_pfn - mp->start_pfn) + bootmap_size,
			BOOTMEM_DEFAULT);

		if (max_low_pfn < max_pfn)
			max_low_pfn = max_pfn;

		if (min_low_pfn > min_pfn)
			min_low_pfn = min_pfn;
	}

#ifdef CONFIG_BLK_DEV_INITRD
	if (LOADER_TYPE && INITRD_START) {
		if (INITRD_START + INITRD_SIZE <= PFN_PHYS(max_low_pfn)) {
			reserve_bootmem_node(NODE_DATA(0), INITRD_START,
				INITRD_SIZE, BOOTMEM_DEFAULT);
			initrd_start = INITRD_START + PAGE_OFFSET;
			initrd_end = initrd_start + INITRD_SIZE;
			printk("initrd:start[%08lx],size[%08lx]\n",
				initrd_start, INITRD_SIZE);
		} else {
			printk("initrd extends beyond end of memory "
				"(0x%08lx > 0x%08llx)\ndisabling initrd\n",
				INITRD_START + INITRD_SIZE,
			        (unsigned long long)PFN_PHYS(max_low_pfn));

			initrd_start = 0;
		}
	}
#endif	/* CONFIG_BLK_DEV_INITRD */

	return max_low_pfn;
}

#define START_PFN(nid)		(NODE_DATA(nid)->bdata->node_min_pfn)
#define MAX_LOW_PFN(nid)	(NODE_DATA(nid)->bdata->node_low_pfn)

unsigned long __init zone_sizes_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES], zholes_size[MAX_NR_ZONES];
	unsigned long low, start_pfn;
	unsigned long holes = 0;
	int nid, i;
	mem_prof_t *mp;

	for_each_online_node(nid) {
		mp = &mem_prof[nid];
		for (i = 0 ; i < MAX_NR_ZONES ; i++) {
			zones_size[i] = 0;
			zholes_size[i] = 0;
		}
		start_pfn = START_PFN(nid);
		low = MAX_LOW_PFN(nid);
		zones_size[ZONE_DMA] = low - start_pfn;
		zholes_size[ZONE_DMA] = mp->holes;
		holes += zholes_size[ZONE_DMA];

		free_area_init_node(nid, zones_size, start_pfn, zholes_size);
	}

	/*
	 * For test
	 *  Use all area of internal RAM.
	 *  see __alloc_pages()
	 */
	NODE_DATA(1)->node_zones->watermark[WMARK_MIN] = 0;
	NODE_DATA(1)->node_zones->watermark[WMARK_LOW] = 0;
	NODE_DATA(1)->node_zones->watermark[WMARK_HIGH] = 0;

	return holes;
}
