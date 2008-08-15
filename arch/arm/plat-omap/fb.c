/*
 * File: arch/arm/plat-omap/fb.c
 *
 * Framebuffer device registration for TI OMAP platforms
 *
 * Copyright (C) 2006 Nokia Corporation
 * Author: Imre Deak <imre.deak@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/bootmem.h>

#include <mach/hardware.h>
#include <asm/io.h>
#include <asm/mach/map.h>

#include <mach/board.h>
#include <mach/sram.h>
#include <mach/omapfb.h>

#if defined(CONFIG_FB_OMAP) || defined(CONFIG_FB_OMAP_MODULE)

static struct omapfb_platform_data omapfb_config;
static int config_invalid;
static int configured_regions;

static u64 omap_fb_dma_mask = ~(u32)0;

static struct platform_device omap_fb_device = {
	.name		= "omapfb",
	.id		= -1,
	.dev = {
		.dma_mask		= &omap_fb_dma_mask,
		.coherent_dma_mask	= ~(u32)0,
		.platform_data		= &omapfb_config,
	},
	.num_resources = 0,
};

static inline int ranges_overlap(unsigned long start1, unsigned long size1,
				 unsigned long start2, unsigned long size2)
{
	return (start1 >= start2 && start1 < start2 + size2) ||
	       (start2 >= start1 && start2 < start1 + size1);
}

static inline int range_included(unsigned long start1, unsigned long size1,
				 unsigned long start2, unsigned long size2)
{
	return start1 >= start2 && start1 + size1 <= start2 + size2;
}


/* Check if there is an overlapping region. */
static int fbmem_region_reserved(unsigned long start, size_t size)
{
	struct omapfb_mem_region *rg;
	int i;

	rg = &omapfb_config.mem_desc.region[0];
	for (i = 0; i < OMAPFB_PLANE_NUM; i++, rg++) {
		if (!rg->paddr)
			/* Empty slot. */
			continue;
		if (ranges_overlap(start, size, rg->paddr, rg->size))
			return 1;
	}
	return 0;
}

/*
 * Get the region_idx`th region from board config/ATAG and convert it to
 * our internal format.
 */
static int get_fbmem_region(int region_idx, struct omapfb_mem_region *rg)
{
	const struct omap_fbmem_config	*conf;
	u32				paddr;

	conf = omap_get_nr_config(OMAP_TAG_FBMEM,
				  struct omap_fbmem_config, region_idx);
	if (conf == NULL)
		return -ENOENT;

	paddr = conf->start;
	/*
	 * Low bits encode the page allocation mode, if high bits
	 * are zero. Otherwise we need a page aligned fixed
	 * address.
	 */
	memset(rg, 0, sizeof(*rg));
	rg->type = paddr & ~PAGE_MASK;
	rg->paddr = paddr & PAGE_MASK;
	rg->size = PAGE_ALIGN(conf->size);
	return 0;
}

static int set_fbmem_region_type(struct omapfb_mem_region *rg, int mem_type,
				  unsigned long mem_start,
				  unsigned long mem_size)
{
	/*
	 * Check if the configuration specifies the type explicitly.
	 * type = 0 && paddr = 0, a default don't care case maps to
	 * the SDRAM type.
	 */
	if (rg->type || (!rg->type && !rg->paddr))
		return 0;
	if (ranges_overlap(rg->paddr, rg->size, mem_start, mem_size)) {
		rg->type = mem_type;
		return 0;
	}
	/* Can't determine it. */
	return -1;
}

static int check_fbmem_region(int region_idx, struct omapfb_mem_region *rg,
			      unsigned long start_avail, unsigned size_avail)
{
	unsigned long	paddr = rg->paddr;
	size_t		size = rg->size;

	if (rg->type > OMAPFB_MEMTYPE_MAX) {
		printk(KERN_ERR
			"Invalid start address for FB region %d\n", region_idx);
		return -EINVAL;
	}

	if (!rg->size) {
		printk(KERN_ERR "Zero size for FB region %d\n", region_idx);
		return -EINVAL;
	}

	if (!paddr)
		/* Allocate this dynamically, leave paddr 0 for now. */
		return 0;

	/*
	 * Fixed region for the given RAM range. Check if it's already
	 * reserved by the FB code or someone else.
	 */
	if (fbmem_region_reserved(paddr, size) ||
	    !range_included(paddr, size, start_avail, size_avail)) {
		printk(KERN_ERR "Trying to use reserved memory "
			"for FB region %d\n", region_idx);
		return -EINVAL;
	}

	return 0;
}

/*
 * Called from map_io. We need to call to this early enough so that we
 * can reserve the fixed SDRAM regions before VM could get hold of them.
 */
void __init omapfb_reserve_sdram(void)
{
	struct bootmem_data	*bdata;
	unsigned long		sdram_start, sdram_size;
	unsigned long		reserved;
	int			i;

	if (config_invalid)
		return;

	bdata = NODE_DATA(0)->bdata;
	sdram_start = bdata->node_min_pfn << PAGE_SHIFT;
	sdram_size = (bdata->node_low_pfn << PAGE_SHIFT) - sdram_start;
	reserved = 0;
	for (i = 0; ; i++) {
		struct omapfb_mem_region	rg;

		if (get_fbmem_region(i, &rg) < 0)
			break;
		if (i == OMAPFB_PLANE_NUM) {
			printk(KERN_ERR
				"Extraneous FB mem configuration entries\n");
			config_invalid = 1;
			return;
		}
		/* Check if it's our memory type. */
		if (set_fbmem_region_type(&rg, OMAPFB_MEMTYPE_SDRAM,
				          sdram_start, sdram_size) < 0 ||
		    (rg.type != OMAPFB_MEMTYPE_SDRAM))
			continue;
		BUG_ON(omapfb_config.mem_desc.region[i].size);
		if (check_fbmem_region(i, &rg, sdram_start, sdram_size) < 0) {
			config_invalid = 1;
			return;
		}
		if (rg.paddr)
			reserve_bootmem(rg.paddr, rg.size, BOOTMEM_DEFAULT);
		reserved += rg.size;
		omapfb_config.mem_desc.region[i] = rg;
		configured_regions++;
	}
	omapfb_config.mem_desc.region_cnt = i;
	if (reserved)
		pr_info("Reserving %lu bytes SDRAM for frame buffer\n",
			 reserved);
}

/*
 * Called at sram init time, before anything is pushed to the SRAM stack.
 * Because of the stack scheme, we will allocate everything from the
 * start of the lowest address region to the end of SRAM. This will also
 * include padding for page alignment and possible holes between regions.
 *
 * As opposed to the SDRAM case, we'll also do any dynamic allocations at
 * this point, since the driver built as a module would have problem with
 * freeing / reallocating the regions.
 */
unsigned long omapfb_reserve_sram(unsigned long sram_pstart,
				  unsigned long sram_vstart,
				  unsigned long sram_size,
				  unsigned long pstart_avail,
				  unsigned long size_avail)
{
	struct omapfb_mem_region	rg;
	unsigned long			pend_avail;
	unsigned long			reserved;
	int				i;

	if (config_invalid)
		return 0;

	reserved = 0;
	pend_avail = pstart_avail + size_avail;
	for (i = 0; ; i++) {
		if (get_fbmem_region(i, &rg) < 0)
			break;
		if (i == OMAPFB_PLANE_NUM) {
			printk(KERN_ERR
				"Extraneous FB mem configuration entries\n");
			config_invalid = 1;
			return 0;
		}

		/* Check if it's our memory type. */
		if (set_fbmem_region_type(&rg, OMAPFB_MEMTYPE_SRAM,
				          sram_pstart, sram_size) < 0 ||
		    (rg.type != OMAPFB_MEMTYPE_SRAM))
			continue;
		BUG_ON(omapfb_config.mem_desc.region[i].size);

		if (check_fbmem_region(i, &rg, pstart_avail, size_avail) < 0) {
			config_invalid = 1;
			return 0;
		}

		if (!rg.paddr) {
			/* Dynamic allocation */
			if ((size_avail & PAGE_MASK) < rg.size) {
				printk("Not enough SRAM for FB region %d\n",
					i);
				config_invalid = 1;
				return 0;
			}
			size_avail = (size_avail - rg.size) & PAGE_MASK;
			rg.paddr = pstart_avail + size_avail;
		}
		/* Reserve everything above the start of the region. */
		if (pend_avail - rg.paddr > reserved)
			reserved = pend_avail - rg.paddr;
		size_avail = pend_avail - reserved - pstart_avail;

		/*
		 * We have a kernel mapping for this already, so the
		 * driver won't have to make one.
		 */
		rg.vaddr = (void *)(sram_vstart + rg.paddr - sram_pstart);
		omapfb_config.mem_desc.region[i] = rg;
		configured_regions++;
	}
	omapfb_config.mem_desc.region_cnt = i;
	if (reserved)
		pr_info("Reserving %lu bytes SRAM for frame buffer\n",
			 reserved);
	return reserved;
}

void omapfb_set_ctrl_platform_data(void *data)
{
	omapfb_config.ctrl_platform_data = data;
}

static inline int omap_init_fb(void)
{
	const struct omap_lcd_config *conf;

	if (config_invalid)
		return 0;
	if (configured_regions != omapfb_config.mem_desc.region_cnt) {
		printk(KERN_ERR "Invalid FB mem configuration entries\n");
		return 0;
	}
	conf = omap_get_config(OMAP_TAG_LCD, struct omap_lcd_config);
	if (conf == NULL) {
		if (configured_regions)
			/* FB mem config, but no LCD config? */
			printk(KERN_ERR "Missing LCD configuration\n");
		return 0;
	}
	omapfb_config.lcd = *conf;

	return platform_device_register(&omap_fb_device);
}

arch_initcall(omap_init_fb);

#else

void omapfb_reserve_sdram(void) {}
unsigned long omapfb_reserve_sram(unsigned long sram_pstart,
				  unsigned long sram_vstart,
				  unsigned long sram_size,
				  unsigned long start_avail,
				  unsigned long size_avail)
{
	return 0;
}


#endif
