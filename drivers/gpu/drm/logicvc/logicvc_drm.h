/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2019-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#ifndef _LOGICVC_DRM_H_
#define _LOGICVC_DRM_H_

#include <linux/regmap.h>
#include <linux/types.h>
#include <drm/drm_device.h>

#define LOGICVC_DISPLAY_INTERFACE_RGB			0
#define LOGICVC_DISPLAY_INTERFACE_ITU656		1
#define LOGICVC_DISPLAY_INTERFACE_LVDS_4BITS		2
#define LOGICVC_DISPLAY_INTERFACE_LVDS_4BITS_CAMERA	3
#define LOGICVC_DISPLAY_INTERFACE_LVDS_3BITS		4
#define LOGICVC_DISPLAY_INTERFACE_DVI			5

#define LOGICVC_DISPLAY_COLORSPACE_RGB		0
#define LOGICVC_DISPLAY_COLORSPACE_YUV422	1
#define LOGICVC_DISPLAY_COLORSPACE_YUV444	2

#define logicvc_drm(d) \
	container_of(d, struct logicvc_drm, drm_dev)

struct logicvc_crtc;
struct logicvc_interface;

struct logicvc_drm_config {
	u32 display_interface;
	u32 display_colorspace;
	u32 display_depth;
	u32 row_stride;
	bool dithering;
	bool background_layer;
	bool layers_configurable;
	u32 layers_count;
};

struct logicvc_drm_caps {
	unsigned int major;
	unsigned int minor;
	char level;
	bool layer_address;
};

struct logicvc_drm {
	const struct logicvc_drm_caps *caps;
	struct logicvc_drm_config config;

	struct drm_device drm_dev;
	phys_addr_t reserved_mem_base;
	struct regmap *regmap;

	struct clk *vclk;
	struct clk *vclk2;
	struct clk *lvdsclk;
	struct clk *lvdsclkn;

	struct list_head layers_list;
	struct logicvc_crtc *crtc;
	struct logicvc_interface *interface;
};

#endif
