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
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/bootmem.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/mach-types.h>
#include <asm/mach/map.h>

#include <asm/arch/board.h>
#include <asm/arch/sram.h>
#include <asm/arch/omapfb.h>

#if defined(CONFIG_FB_OMAP) || defined(CONFIG_FB_OMAP_MODULE)

static struct omapfb_platform_data omapfb_config;

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

/* called from map_io */
void omapfb_reserve_mem(void)
{
	const struct omap_fbmem_config *fbmem_conf;
	unsigned long total_size;
	int i;

	if (!omap_fb_sram_valid) {
		/* FBMEM SRAM configuration was already found to be invalid.
		 * Ignore the whole configuration block. */
		omapfb_config.mem_desc.region_cnt = 0;
		return;
	}

	i = 0;
	total_size = 0;
	while ((fbmem_conf = omap_get_nr_config(OMAP_TAG_FBMEM,
				struct omap_fbmem_config, i)) != NULL) {
		unsigned long start;
		unsigned long size;

		if (i == OMAPFB_PLANE_NUM) {
			printk(KERN_ERR "ignoring extra plane info\n");
			break;
		}
		start = fbmem_conf->start;
		size  = fbmem_conf->size;
		omapfb_config.mem_desc.region[i].paddr = start;
		omapfb_config.mem_desc.region[i].size = size;
		if (omap_fb_sram_plane != i && start) {
			reserve_bootmem(start, size);
			total_size += size;
		}
		i++;
	}
	omapfb_config.mem_desc.region_cnt = i;
	if (total_size)
		pr_info("Reserving %lu bytes SDRAM for frame buffer\n",
			 total_size);

}

void omapfb_set_ctrl_platform_data(void *data)
{
	omapfb_config.ctrl_platform_data = data;
}

static inline int omap_init_fb(void)
{
	const struct omap_lcd_config *conf;

	conf = omap_get_config(OMAP_TAG_LCD, struct omap_lcd_config);
	if (conf == NULL)
		return 0;

	omapfb_config.lcd = *conf;

	return platform_device_register(&omap_fb_device);
}

arch_initcall(omap_init_fb);

#else

void omapfb_reserve_mem(void) {}

#endif


