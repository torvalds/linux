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
#include <linux/memblock.h>
#include <linux/io.h>
#include <linux/omapfb.h>

#include <mach/hardware.h>
#include <asm/mach/map.h>

#include <plat/board.h>

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

void omapfb_set_platform_data(struct omapfb_platform_data *data)
{
}

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

/*
 * Get the region_idx`th region from board config/ATAG and convert it to
 * our internal format.
 */
static int __init get_fbmem_region(int region_idx, struct omapfb_mem_region *rg)
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

static int valid_sdram(unsigned long addr, unsigned long size)
{
	return memblock_is_region_memory(addr, size);
}

static int reserve_sdram(unsigned long addr, unsigned long size)
{
	if (memblock_is_region_reserved(addr, size))
		return -EBUSY;
	if (memblock_reserve(addr, size))
		return -ENOMEM;
	return 0;
}

/*
 * Called from map_io. We need to call to this early enough so that we
 * can reserve the fixed SDRAM regions before VM could get hold of them.
 */
void __init omapfb_reserve_sdram_memblock(void)
{
	unsigned long reserved = 0;
	int i;

	if (config_invalid)
		return;

	for (i = 0; ; i++) {
		struct omapfb_mem_region rg;

		if (get_fbmem_region(i, &rg) < 0)
			break;

		if (i == OMAPFB_PLANE_NUM) {
			pr_err("Extraneous FB mem configuration entries\n");
			config_invalid = 1;
			return;
		}

		/* Check if it's our memory type. */
		if (rg.type != OMAPFB_MEMTYPE_SDRAM)
			continue;

		/* Check if the region falls within SDRAM */
		if (rg.paddr && !valid_sdram(rg.paddr, rg.size))
			continue;

		if (rg.size == 0) {
			pr_err("Zero size for FB region %d\n", i);
			config_invalid = 1;
			return;
		}

		if (rg.paddr) {
			if (reserve_sdram(rg.paddr, rg.size)) {
				pr_err("Trying to use reserved memory for FB region %d\n",
					i);
				config_invalid = 1;
				return;
			}
			reserved += rg.size;
		}

		if (omapfb_config.mem_desc.region[i].size) {
			pr_err("FB region %d already set\n", i);
			config_invalid = 1;
			return;
		}

		omapfb_config.mem_desc.region[i] = rg;
		configured_regions++;
	}
	omapfb_config.mem_desc.region_cnt = i;
	if (reserved)
		pr_info("Reserving %lu bytes SDRAM for frame buffer\n",
			 reserved);
}

void omapfb_set_ctrl_platform_data(void *data)
{
	omapfb_config.ctrl_platform_data = data;
}

static int __init omap_init_fb(void)
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

#elif defined(CONFIG_FB_OMAP2) || defined(CONFIG_FB_OMAP2_MODULE)

static u64 omap_fb_dma_mask = ~(u32)0;
static struct omapfb_platform_data omapfb_config;

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

void omapfb_set_platform_data(struct omapfb_platform_data *data)
{
	omapfb_config = *data;
}

static int __init omap_init_fb(void)
{
	return platform_device_register(&omap_fb_device);
}

arch_initcall(omap_init_fb);

void omapfb_reserve_sdram_memblock(void)
{
}

#else

void omapfb_set_platform_data(struct omapfb_platform_data *data)
{
}

void omapfb_reserve_sdram_memblock(void)
{
}

#endif
