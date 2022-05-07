/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef __VS_TYPE_H__
#define __VS_TYPE_H__

#include <linux/version.h>

#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
#include <drm/drmP.h>
#endif
#include <drm/drm_plane.h>
#include <drm/drm_plane_helper.h>

struct vs_plane_info {
	const char *name;
	u8 id;
	enum drm_plane_type type;
	unsigned int num_formats;
	const u32 *formats;
	u8 num_modifiers;
	const u64 *modifiers;
	unsigned int min_width;
	unsigned int min_height;
	unsigned int max_width;
	unsigned int max_height;
	unsigned int rotation;
	unsigned int blend_mode;
	unsigned int color_encoding;

	/* 0 means no de-gamma LUT */
	unsigned int degamma_size;

	int min_scale; /* 16.16 fixed point */
	int max_scale; /* 16.16 fixed point */

	/* default zorder value,
	 * and 255 means unsupported zorder capability
	 */
	u8	 zpos;

	bool watermark;
	bool color_mgmt;
	bool roi;
};

struct vs_dc_info {
	const char *name;

	u8 panel_num;

	/* planes */
	u8 plane_num;
	const struct vs_plane_info *planes;

	u8 layer_num;
	unsigned int max_bpc;
	unsigned int color_formats;

	/* 0 means no gamma LUT */
	u16 gamma_size;
	u8 gamma_bits;

	u16 pitch_alignment;

	bool pipe_sync;
	bool mmu_prefetch;
	bool background;
	bool panel_sync;
	bool cap_dec;
};

#endif /* __VS_TYPE_H__ */
