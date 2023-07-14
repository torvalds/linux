/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#ifndef MTK_DRM_CRTC_H
#define MTK_DRM_CRTC_H

#include <drm/drm_crtc.h>
#include "mtk_drm_ddp_comp.h"
#include "mtk_drm_plane.h"

#define MTK_LUT_SIZE	512
#define MTK_MAX_BPC	10
#define MTK_MIN_BPC	3

void mtk_drm_crtc_commit(struct drm_crtc *crtc);
int mtk_drm_crtc_create(struct drm_device *drm_dev,
			const unsigned int *path,
			unsigned int path_len,
			int priv_data_index);
int mtk_drm_crtc_plane_check(struct drm_crtc *crtc, struct drm_plane *plane,
			     struct mtk_plane_state *state);
void mtk_drm_crtc_async_update(struct drm_crtc *crtc, struct drm_plane *plane,
			       struct drm_atomic_state *plane_state);
struct device *mtk_drm_crtc_dma_dev_get(struct drm_crtc *crtc);

#endif /* MTK_DRM_CRTC_H */
