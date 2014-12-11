/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Benjamin Gaignard <benjamin.gaignard@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef _STI_LAYER_H_
#define _STI_LAYER_H_

#include <drm/drmP.h>

#define to_sti_layer(x) container_of(x, struct sti_layer, plane)

#define STI_LAYER_TYPE_SHIFT 8
#define STI_LAYER_TYPE_MASK (~((1<<STI_LAYER_TYPE_SHIFT)-1))

struct sti_layer;

enum sti_layer_type {
	STI_GDP = 1 << STI_LAYER_TYPE_SHIFT,
	STI_VID = 2 << STI_LAYER_TYPE_SHIFT,
	STI_CUR = 3 << STI_LAYER_TYPE_SHIFT,
	STI_BCK = 4 << STI_LAYER_TYPE_SHIFT
};

enum sti_layer_id_of_type {
	STI_ID_0 = 0,
	STI_ID_1 = 1,
	STI_ID_2 = 2,
	STI_ID_3 = 3
};

enum sti_layer_desc {
	STI_GDP_0       = STI_GDP | STI_ID_0,
	STI_GDP_1       = STI_GDP | STI_ID_1,
	STI_GDP_2       = STI_GDP | STI_ID_2,
	STI_GDP_3       = STI_GDP | STI_ID_3,
	STI_VID_0       = STI_VID | STI_ID_0,
	STI_VID_1       = STI_VID | STI_ID_1,
	STI_CURSOR      = STI_CUR,
	STI_BACK        = STI_BCK
};

/**
 * STI layer functions structure
 *
 * @get_formats:	get layer supported formats
 * @get_nb_formats:	get number of format supported
 * @init:               initialize the layer
 * @prepare:		prepare layer before rendering
 * @commit:		set layer for rendering
 * @disable:		disable layer
 */
struct sti_layer_funcs {
	const uint32_t* (*get_formats)(struct sti_layer *layer);
	unsigned int (*get_nb_formats)(struct sti_layer *layer);
	void (*init)(struct sti_layer *layer);
	int (*prepare)(struct sti_layer *layer, bool first_prepare);
	int (*commit)(struct sti_layer *layer);
	int (*disable)(struct sti_layer *layer);
};

/**
 * STI layer structure
 *
 * @plane:              drm plane it is bound to (if any)
 * @fb:                 drm fb it is bound to
 * @mode:               display mode
 * @desc:               layer type & id
 * @device:		driver device
 * @regs:		layer registers
 * @ops:                layer functions
 * @zorder:             layer z-order
 * @mixer_id:           id of the mixer used to display the layer
 * @enabled:            to know if the layer is active or not
 * @src_x src_y:        coordinates of the input (fb) area
 * @src_w src_h:        size of the input (fb) area
 * @dst_x dst_y:        coordinates of the output (crtc) area
 * @dst_w dst_h:        size of the output (crtc) area
 * @format:             format
 * @pitches:            pitch of 'planes' (eg: Y, U, V)
 * @offsets:            offset of 'planes'
 * @vaddr:              virtual address of the input buffer
 * @paddr:              physical address of the input buffer
 */
struct sti_layer {
	struct drm_plane plane;
	struct drm_framebuffer *fb;
	struct drm_display_mode *mode;
	enum sti_layer_desc desc;
	struct device *dev;
	void __iomem *regs;
	const struct sti_layer_funcs *ops;
	int zorder;
	int mixer_id;
	bool enabled;
	int src_x, src_y;
	int src_w, src_h;
	int dst_x, dst_y;
	int dst_w, dst_h;
	uint32_t format;
	unsigned int pitches[4];
	unsigned int offsets[4];
	void *vaddr;
	dma_addr_t paddr;
};

struct sti_layer *sti_layer_create(struct device *dev, int desc,
			void __iomem *baseaddr);
int sti_layer_prepare(struct sti_layer *layer, struct drm_framebuffer *fb,
			struct drm_display_mode *mode,
			int mixer_id,
			int dest_x, int dest_y,
			int dest_w, int dest_h,
			int src_x, int src_y,
			int src_w, int src_h);
int sti_layer_commit(struct sti_layer *layer);
int sti_layer_disable(struct sti_layer *layer);
const uint32_t *sti_layer_get_formats(struct sti_layer *layer);
unsigned int sti_layer_get_nb_formats(struct sti_layer *layer);
const char *sti_layer_to_str(struct sti_layer *layer);

#endif
