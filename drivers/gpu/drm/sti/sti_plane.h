/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) STMicroelectronics SA 2014
 * Author: Benjamin Gaignard <benjamin.gaignard@st.com> for STMicroelectronics.
 */

#ifndef _STI_PLANE_H_
#define _STI_PLANE_H_

#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>

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

enum sti_plane_status {
	STI_PLANE_READY,
	STI_PLANE_UPDATED,
	STI_PLANE_DISABLING,
	STI_PLANE_FLUSHING,
	STI_PLANE_DISABLED,
};

#define FPS_LENGTH 128
struct sti_fps_info {
	bool output;
	unsigned int curr_frame_counter;
	unsigned int last_frame_counter;
	unsigned int curr_field_counter;
	unsigned int last_field_counter;
	ktime_t	     last_timestamp;
	char fps_str[FPS_LENGTH];
	char fips_str[FPS_LENGTH];
};

/**
 * STI plane structure
 *
 * @plane:              drm plane it is bound to (if any)
 * @desc:               plane type & id
 * @status:             to know the status of the plane
 * @fps_info:           frame per second info
 */
struct sti_plane {
	struct drm_plane drm_plane;
	enum sti_plane_desc desc;
	enum sti_plane_status status;
	struct sti_fps_info fps_info;
};

const char *sti_plane_to_str(struct sti_plane *plane);
void sti_plane_update_fps(struct sti_plane *plane,
			  bool new_frame,
			  bool new_field);

void sti_plane_init_property(struct sti_plane *plane,
			     enum drm_plane_type type);
#endif
