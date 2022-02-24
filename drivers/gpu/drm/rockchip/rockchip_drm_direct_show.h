/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 * Author: Sandy Huang <hjc@rock-chips.com>
 */

#ifndef ROCKCHIP_DRM_DIRECT_SHOW_H
#define ROCKCHIP_DRM_DIRECT_SHOW_H

#include <linux/dma-direction.h>
#include <linux/memblock.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_fb.h"
#include "rockchip_drm_gem.h"

struct rockchip_drm_direct_show_buffer {
	/* input */
	u32 width;
	u32 height;
	u32 pixel_format;
	u32 flag; /* default 0 is scattered buffer, set ROCKCHIP_BO_CONTIG is continue CMA buffer */

	/* output */
	u32 bpp;		/* bits num per pixel */
	u32 pitch[3];		/* byte num for each line */
	void *vir_addr[3];	/* kernel virtual address, default use vir_addr[0] for RGB format */
	dma_addr_t phy_addr[3];	/* physical address when alloc continue cma buffer or secure buffer */
	struct rockchip_gem_object *rk_gem_obj;
	struct drm_framebuffer *fb;
	int dmabuf_fd;		/* export dmabuf_fd used by other module */
};

struct rockchip_drm_direct_show_commit_info {
	struct drm_crtc *crtc;
	struct drm_plane *plane;
	struct rockchip_drm_direct_show_buffer *buffer;
	u32 src_x;
	u32 src_y;
	u32 src_w;
	u32 src_h;
	u32 dst_x;
	u32 dst_y;
	u32 dst_w;
	u32 dst_h;
	bool top_zpos;
};

struct drm_device *rockchip_drm_get_dev(void);
int rockchip_drm_direct_show_alloc_buffer(struct drm_device *drm,
					  struct rockchip_drm_direct_show_buffer *buffer);
void rockchip_drm_direct_show_free_buffer(struct drm_device *drm,
					  struct rockchip_drm_direct_show_buffer *buffer);
struct drm_crtc *rockchip_drm_direct_show_get_crtc(struct drm_device *drm);
struct drm_plane *rockchip_drm_direct_show_get_plane(struct drm_device *drm, char *name);
int rockchip_drm_direct_show_commit(struct drm_device *drm,
				    struct rockchip_drm_direct_show_commit_info *commit_info);
int rockchip_drm_direct_show_disable_plane(struct drm_device *drm, struct drm_plane *plane);

#endif
