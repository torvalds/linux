/*
 * Copyright (c) 2015 MediaTek Inc.
 * Author: CK Hu <ck.hu@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _MTK_DRM_PLANE_H_
#define _MTK_DRM_PLANE_H_

#include <drm/drm_crtc.h>
#include <linux/types.h>

struct mtk_drm_plane {
	struct drm_plane		base;
	unsigned int			idx;
};

struct mtk_plane_pending_state {
	bool				config;
	bool				enable;
	dma_addr_t			addr;
	unsigned int			pitch;
	unsigned int			format;
	unsigned int			x;
	unsigned int			y;
	unsigned int			width;
	unsigned int			height;
	bool				dirty;
};

struct mtk_plane_state {
	struct drm_plane_state		base;
	struct mtk_plane_pending_state	pending;
};

static inline struct mtk_drm_plane *to_mtk_plane(struct drm_plane *plane)
{
	return container_of(plane, struct mtk_drm_plane, base);
}

static inline struct mtk_plane_state *
to_mtk_plane_state(struct drm_plane_state *state)
{
	return container_of(state, struct mtk_plane_state, base);
}

int mtk_plane_init(struct drm_device *dev, struct mtk_drm_plane *mtk_plane,
		   unsigned long possible_crtcs, enum drm_plane_type type,
		   unsigned int zpos);

#endif
