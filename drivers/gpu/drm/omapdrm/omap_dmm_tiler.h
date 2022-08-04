/*
 * Copyright (C) 2011 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Rob Clark <rob@ti.com>
 *         Andy Gross <andy.gross@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef OMAP_DMM_TILER_H
#define OMAP_DMM_TILER_H

#include "omap_drv.h"
#include "tcm.h"

enum tiler_fmt {
	TILFMT_8BIT = 0,
	TILFMT_16BIT,
	TILFMT_32BIT,
	TILFMT_PAGE,
	TILFMT_NFORMATS
};

struct pat_area {
	u32 x0:8;
	u32 y0:8;
	u32 x1:8;
	u32 y1:8;
};

struct tiler_block {
	struct list_head alloc_node;	/* node for global block list */
	struct tcm_area area;		/* area */
	enum tiler_fmt fmt;		/* format */
};

/* bits representing the same slot in DMM-TILER hw-block */
#define SLOT_WIDTH_BITS         6
#define SLOT_HEIGHT_BITS        6

/* bits reserved to describe coordinates in DMM-TILER hw-block */
#define CONT_WIDTH_BITS         14
#define CONT_HEIGHT_BITS        13

/* calculated constants */
#define TILER_PAGE              (1 << (SLOT_WIDTH_BITS + SLOT_HEIGHT_BITS))
#define TILER_WIDTH             (1 << (CONT_WIDTH_BITS - SLOT_WIDTH_BITS))
#define TILER_HEIGHT            (1 << (CONT_HEIGHT_BITS - SLOT_HEIGHT_BITS))

/*
Table 15-11. Coding and Description of TILER Orientations
S Y X	Description				Alternate description
0 0 0	0-degree view				Natural view
0 0 1	0-degree view with vertical mirror 	180-degree view with horizontal mirror
0 1 0	0-degree view with horizontal mirror 	180-degree view with vertical mirror
0 1 1	180-degree view
1 0 0	90-degree view with vertical mirror	270-degree view with horizontal mirror
1 0 1	270-degree view
1 1 0	90-degree view
1 1 1	90-degree view with horizontal mirror	270-degree view with vertical mirror
 */
#define MASK_XY_FLIP		(1 << 31)
#define MASK_Y_INVERT		(1 << 30)
#define MASK_X_INVERT		(1 << 29)
#define SHIFT_ACC_MODE		27
#define MASK_ACC_MODE		3

#define MASK(bits) ((1 << (bits)) - 1)

#define TILVIEW_8BIT    0x60000000u
#define TILVIEW_16BIT   (TILVIEW_8BIT  + VIEW_SIZE)
#define TILVIEW_32BIT   (TILVIEW_16BIT + VIEW_SIZE)
#define TILVIEW_PAGE    (TILVIEW_32BIT + VIEW_SIZE)
#define TILVIEW_END     (TILVIEW_PAGE  + VIEW_SIZE)

/* create tsptr by adding view orientation and access mode */
#define TIL_ADDR(x, orient, a)\
	((u32) (x) | (orient) | ((a) << SHIFT_ACC_MODE))

#ifdef CONFIG_DEBUG_FS
int tiler_map_show(struct seq_file *s, void *arg);
#endif

/* pin/unpin */
int tiler_pin(struct tiler_block *block, struct page **pages,
		u32 npages, u32 roll, bool wait);
int tiler_unpin(struct tiler_block *block);

/* reserve/release */
struct tiler_block *tiler_reserve_2d(enum tiler_fmt fmt, u16 w, u16 h,
				u16 align);
struct tiler_block *tiler_reserve_1d(size_t size);
int tiler_release(struct tiler_block *block);

/* utilities */
dma_addr_t tiler_ssptr(struct tiler_block *block);
dma_addr_t tiler_tsptr(struct tiler_block *block, u32 orient,
		u32 x, u32 y);
u32 tiler_stride(enum tiler_fmt fmt, u32 orient);
size_t tiler_size(enum tiler_fmt fmt, u16 w, u16 h);
size_t tiler_vsize(enum tiler_fmt fmt, u16 w, u16 h);
void tiler_align(enum tiler_fmt fmt, u16 *w, u16 *h);
u32 tiler_get_cpu_cache_flags(void);
bool dmm_is_available(void);

extern struct platform_driver omap_dmm_driver;

/* GEM bo flags -> tiler fmt */
static inline enum tiler_fmt gem2fmt(u32 flags)
{
	switch (flags & OMAP_BO_TILED_MASK) {
	case OMAP_BO_TILED_8:
		return TILFMT_8BIT;
	case OMAP_BO_TILED_16:
		return TILFMT_16BIT;
	case OMAP_BO_TILED_32:
		return TILFMT_32BIT;
	default:
		return TILFMT_PAGE;
	}
}

static inline bool validfmt(enum tiler_fmt fmt)
{
	switch (fmt) {
	case TILFMT_8BIT:
	case TILFMT_16BIT:
	case TILFMT_32BIT:
	case TILFMT_PAGE:
		return true;
	default:
		return false;
	}
}

#endif
