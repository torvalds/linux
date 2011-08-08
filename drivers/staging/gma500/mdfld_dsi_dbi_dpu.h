/*
 * Copyright © 2010 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 * jim liu <jim.liu@intel.com>
 * Jackie Li<yaodong.li@intel.com>
 */

#ifndef __MDFLD_DSI_DBI_DPU_H__
#define __MDFLD_DSI_DBI_DPU_H__

#include "mdfld_dsi_dbi.h"

typedef enum {
	MDFLD_PLANEA,
	MDFLD_PLANEC,
	MDFLD_CURSORA,
	MDFLD_CURSORC,
	MDFLD_OVERLAYA,
	MDFLD_OVERLAYC,
	MDFLD_PLANE_NUM,
} mdfld_plane_t;

#define MDFLD_PIPEA_PLANE_MASK	0x15
#define MDFLD_PIPEC_PLANE_MASK	0x2A

struct mdfld_cursor_info {
	int x, y;
	int size;
};

#define MDFLD_CURSOR_SIZE	64

/*
 * enter DSR mode if screen has no update for 2 frames.
 */
#define MDFLD_MAX_IDLE_COUNT	2

struct mdfld_dbi_dpu_info {
	struct drm_device *dev;
	/* Lock */
	spinlock_t dpu_update_lock;

	/* Cursor postion */
	struct mdfld_cursor_info cursors[2];

	/* Damaged area for each plane */
	struct psb_drm_dpu_rect damaged_rects[MDFLD_PLANE_NUM];

	/* Final damaged area */
	struct psb_drm_dpu_rect damage_pipea;
	struct psb_drm_dpu_rect damage_pipec;

	/* Pending */
	u32 pending;

	/* DPU timer */
	struct timer_list dpu_timer;
	spinlock_t dpu_timer_lock;

	/* DPU idle count */
	u32 idle_count;

	/* DSI outputs */
	struct mdfld_dsi_dbi_output *dbi_outputs[2];
	int dbi_output_num;
};

static inline int mdfld_dpu_region_extent(struct psb_drm_dpu_rect *origin,
			 struct psb_drm_dpu_rect *rect)
{
	int x1, y1, x2, y2;

	x1 = origin->x + origin->width;
	y1 = origin->y + origin->height;

	x2 = rect->x + rect->width;
	y2 = rect->y + rect->height;

	origin->x = min(origin->x, rect->x);
	origin->y = min(origin->y, rect->y);
	origin->width = max(x1, x2) - origin->x;
	origin->height = max(y1, y2) - origin->y;

	return 0;
}

static inline void mdfld_check_boundary(struct mdfld_dbi_dpu_info *dpu_info,
				struct psb_drm_dpu_rect *rect)
{
	if (rect->x < 0)
		rect->x = 0;
	if (rect->y < 0)
		rect->y = 0;

	if (rect->x + rect->width > 864)
		rect->width = 864 - rect->x;
	if (rect->y + rect->height > 480)
		rect->height = 480 - rect->height;

	if (!rect->width)
		rect->width = 1;
	if (!rect->height)
		rect->height = 1;
}

static inline void mdfld_dpu_init_damage(struct mdfld_dbi_dpu_info *dpu_info,
				int pipe)
{
	struct psb_drm_dpu_rect *rect;

	if (pipe == 0)
		rect = &dpu_info->damage_pipea;
	else
		rect = &dpu_info->damage_pipec;

	rect->x = 864;
	rect->y = 480;
	rect->width = -864;
	rect->height = -480;
}

extern int mdfld_dsi_dbi_dsr_off(struct drm_device *dev,
				struct psb_drm_dpu_rect *rect);
extern int mdfld_dbi_dpu_report_damage(struct drm_device *dev,
				mdfld_plane_t plane,
				struct psb_drm_dpu_rect *rect);
extern int mdfld_dbi_dpu_report_fullscreen_damage(struct drm_device *dev);
extern int mdfld_dpu_exit_dsr(struct drm_device *dev);
extern void mdfld_dbi_dpu_timer_start(struct mdfld_dbi_dpu_info *dpu_info);
extern int mdfld_dbi_dpu_init(struct drm_device *dev);
extern void mdfld_dbi_dpu_exit(struct drm_device *dev);
extern void mdfld_dpu_update_panel(struct drm_device *dev);

#endif /*__MDFLD_DSI_DBI_DPU_H__*/
