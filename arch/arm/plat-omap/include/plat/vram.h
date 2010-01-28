/*
 * VRAM manager for OMAP
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#ifndef __OMAP_VRAM_H__
#define __OMAP_VRAM_H__

#include <linux/types.h>

#define OMAP_VRAM_MEMTYPE_SDRAM		0
#define OMAP_VRAM_MEMTYPE_SRAM		1
#define OMAP_VRAM_MEMTYPE_MAX		1

extern int omap_vram_add_region(unsigned long paddr, size_t size);
extern int omap_vram_free(unsigned long paddr, size_t size);
extern int omap_vram_alloc(int mtype, size_t size, unsigned long *paddr);
extern int omap_vram_reserve(unsigned long paddr, size_t size);
extern void omap_vram_get_info(unsigned long *vram, unsigned long *free_vram,
		unsigned long *largest_free_block);

#ifdef CONFIG_OMAP2_VRAM
extern void omap_vram_set_sdram_vram(u32 size, u32 start);
extern void omap_vram_set_sram_vram(u32 size, u32 start);

extern void omap_vram_reserve_sdram(void);
extern unsigned long omap_vram_reserve_sram(unsigned long sram_pstart,
					    unsigned long sram_vstart,
					    unsigned long sram_size,
					    unsigned long pstart_avail,
					    unsigned long size_avail);
#else
static inline void omap_vram_set_sdram_vram(u32 size, u32 start) { }
static inline void omap_vram_set_sram_vram(u32 size, u32 start) { }

static inline void omap_vram_reserve_sdram(void) { }
static inline unsigned long omap_vram_reserve_sram(unsigned long sram_pstart,
					    unsigned long sram_vstart,
					    unsigned long sram_size,
					    unsigned long pstart_avail,
					    unsigned long size_avail)
{
	return 0;
}
#endif

#endif
