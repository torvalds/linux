/*
 * linux/arch/arm/plat-omap/common.c
 *
 * Code common to all OMAP machines.
 * The file is created by Tony Lindgren <tony@atomide.com>
 *
 * Copyright (C) 2009 Texas Instruments
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/omapfb.h>

#include <plat/common.h>
#include <plat/board.h>
#include <plat/vram.h>
#include <plat/dsp.h>

#include <plat/omap-secure.h>


#define NO_LENGTH_CHECK 0xffffffff

struct omap_board_config_kernel *omap_board_config __initdata;
int omap_board_config_size;

static const void *__init get_config(u16 tag, size_t len,
		int skip, size_t *len_out)
{
	struct omap_board_config_kernel *kinfo = NULL;
	int i;

	/* Try to find the config from the board-specific structures
	 * in the kernel. */
	for (i = 0; i < omap_board_config_size; i++) {
		if (omap_board_config[i].tag == tag) {
			if (skip == 0) {
				kinfo = &omap_board_config[i];
				break;
			} else {
				skip--;
			}
		}
	}
	if (kinfo == NULL)
		return NULL;
	return kinfo->data;
}

const void *__init __omap_get_config(u16 tag, size_t len, int nr)
{
        return get_config(tag, len, nr, NULL);
}

const void *__init omap_get_var_config(u16 tag, size_t *len)
{
        return get_config(tag, NO_LENGTH_CHECK, 0, len);
}

void __init omap_reserve(void)
{
	omapfb_reserve_sdram_memblock();
	omap_vram_reserve_sdram_memblock();
	omap_dsp_reserve_sdram_memblock();
	omap_secure_ram_reserve_memblock();
}

void __init omap_init_consistent_dma_size(void)
{
#ifdef CONFIG_FB_OMAP_CONSISTENT_DMA_SIZE
	init_consistent_dma_size(CONFIG_FB_OMAP_CONSISTENT_DMA_SIZE << 20);
#endif
}
