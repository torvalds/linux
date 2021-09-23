/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd.
 * Author: Sandy Huang <hjc@rock-chips.com>
 */

#ifndef ROCKCHIP_DRM_DEBUGFS_H
#define ROCKCHIP_DRM_DEBUGFS_H

/**
 * struct vop_dump_info - vop dump plane info structure
 *
 * Store plane info used to write display data to /data/vop_buf/
 *
 */
struct vop_dump_info {
	/* @win_id: vop hard win index */
	u8 win_id;
	/* @area_id: vop hard area index inside win */
	u8 area_id;
	/* @AFBC_flag: indicate the buffer compress by gpu or not */
	bool AFBC_flag;
	/* @yuv_format: indicate yuv format or not */
	bool yuv_format;
	/* @pitches: the buffer pitch size */
	u32 pitches;
	/* @height: the buffer pitch height */
	u32 height;
	/* @info: DRM format info */
	const struct drm_format_info *format;
	/* @offset: the buffer offset */
	unsigned long offset;
	/* @num_pages: the pages number */
	unsigned long num_pages;
	/* @pages: store the buffer all pages */
	struct page **pages;
};

/**
 * struct vop_dump_list - store all buffer info per frame
 *
 * one frame maybe multiple buffer, all will be stored here.
 *
 */
struct vop_dump_list {
	struct list_head entry;
	struct vop_dump_info dump_info;
};

enum vop_dump_status {
	DUMP_DISABLE = 0,
	DUMP_KEEP
};

#if defined(CONFIG_ROCKCHIP_DRM_DEBUG)
int rockchip_drm_add_dump_buffer(struct drm_crtc *crtc, struct dentry *root);
int rockchip_drm_dump_plane_buffer(struct vop_dump_info *dump_info, int frame_count);
#else
static inline int
rockchip_drm_add_dump_buffer(struct drm_crtc *crtc, struct dentry *root)
{
	return 0;
}

static inline int
rockchip_drm_dump_plane_buffer(struct vop_dump_info *dump_info, int frame_count)
{
	return 0;
}
#endif

#endif
