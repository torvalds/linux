/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2019-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#ifndef _LOGICVC_LAYER_H_
#define _LOGICVC_LAYER_H_

#include <linux/of.h>
#include <linux/types.h>
#include <drm/drm_plane.h>

#define LOGICVC_LAYER_COLORSPACE_RGB		0
#define LOGICVC_LAYER_COLORSPACE_YUV		1

#define LOGICVC_LAYER_ALPHA_LAYER		0
#define LOGICVC_LAYER_ALPHA_PIXEL		1

struct logicvc_layer_buffer_setup {
	u8 buffer_sel;
	u16 voffset;
	u16 hoffset;
};

struct logicvc_layer_config {
	u32 colorspace;
	u32 depth;
	u32 alpha_mode;
	u32 base_offset;
	u32 buffer_offset;
	bool primary;
};

struct logicvc_layer_formats {
	u32 colorspace;
	u32 depth;
	bool alpha;
	uint32_t *formats;
};

struct logicvc_layer {
	struct logicvc_layer_config config;
	struct logicvc_layer_formats *formats;
	struct device_node *of_node;

	struct drm_plane drm_plane;
	struct list_head list;
	u32 index;
};

int logicvc_layer_buffer_find_setup(struct logicvc_drm *logicvc,
				    struct logicvc_layer *layer,
				    struct drm_plane_state *state,
				    struct logicvc_layer_buffer_setup *setup);
struct logicvc_layer *logicvc_layer_get_from_index(struct logicvc_drm *logicvc,
						   u32 index);
struct logicvc_layer *logicvc_layer_get_from_type(struct logicvc_drm *logicvc,
						  enum drm_plane_type type);
struct logicvc_layer *logicvc_layer_get_primary(struct logicvc_drm *logicvc);
void logicvc_layers_attach_crtc(struct logicvc_drm *logicvc);
int logicvc_layers_init(struct logicvc_drm *logicvc);

#endif
