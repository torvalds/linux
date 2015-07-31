/*
 * Copyright (C) STMicroelectronics SA 2014
 * Author: Benjamin Gaignard <benjamin.gaignard@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef _STI_PLANE_H_
#define _STI_PLANE_H_

#include <drm/drmP.h>

#define to_sti_plane(x) container_of(x, struct sti_plane, drm_plane)

#define STI_PLANE_TYPE_SHIFT 8
#define STI_PLANE_TYPE_MASK (~((1 << STI_PLANE_TYPE_SHIFT) - 1))

enum sti_plane_type {
	STI_GDP = 1 << STI_PLANE_TYPE_SHIFT,
	STI_VDP = 2 << STI_PLANE_TYPE_SHIFT,
	STI_CUR = 3 << STI_PLANE_TYPE_SHIFT,
	STI_BCK = 4 << STI_PLANE_TYPE_SHIFT
};

enum sti_plane_id_of_type {
	STI_ID_0 = 0,
	STI_ID_1 = 1,
	STI_ID_2 = 2,
	STI_ID_3 = 3
};

enum sti_plane_desc {
	STI_GDP_0       = STI_GDP | STI_ID_0,
	STI_GDP_1       = STI_GDP | STI_ID_1,
	STI_GDP_2       = STI_GDP | STI_ID_2,
	STI_GDP_3       = STI_GDP | STI_ID_3,
	STI_HQVDP_0     = STI_VDP | STI_ID_0,
	STI_CURSOR      = STI_CUR,
	STI_BACK        = STI_BCK
};

/**
 * STI plane structure
 *
 * @plane:              drm plane it is bound to (if any)
 * @fb:                 drm fb it is bound to
 * @mode:               display mode
 * @desc:               plane type & id
 * @ops:                plane functions
 * @zorder:             plane z-order
 * @mixer_id:           id of the mixer used to display the plane
 * @enabled:            to know if the plane is active or not
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
struct sti_plane {
	struct drm_plane drm_plane;
	struct drm_framebuffer *fb;
	struct drm_display_mode *mode;
	enum sti_plane_desc desc;
	const struct sti_plane_funcs *ops;
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

/**
 * STI plane functions structure
 *
 * @get_formats:     get plane supported formats
 * @get_nb_formats:  get number of format supported
 * @prepare:         prepare plane before rendering
 * @commit:          set plane for rendering
 * @disable:         disable plane
 */
struct sti_plane_funcs {
	const uint32_t* (*get_formats)(struct sti_plane *plane);
	unsigned int (*get_nb_formats)(struct sti_plane *plane);
	int (*prepare)(struct sti_plane *plane, bool first_prepare);
	int (*commit)(struct sti_plane *plane);
	int (*disable)(struct sti_plane *plane);
};

struct drm_plane *sti_plane_init(struct drm_device *dev,
				 struct sti_plane *sti_plane,
				 unsigned int possible_crtcs,
				 enum drm_plane_type type);
const char *sti_plane_to_str(struct sti_plane *plane);

#endif
