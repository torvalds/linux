// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Benjamin Gaignard <benjamin.gaignard@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          for STMicroelectronics.
 */

#include <drm/drmP.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include "sti_compositor.h"
#include "sti_drv.h"
#include "sti_plane.h"

const char *sti_plane_to_str(struct sti_plane *plane)
{
	switch (plane->desc) {
	case STI_GDP_0:
		return "GDP0";
	case STI_GDP_1:
		return "GDP1";
	case STI_GDP_2:
		return "GDP2";
	case STI_GDP_3:
		return "GDP3";
	case STI_HQVDP_0:
		return "HQVDP0";
	case STI_CURSOR:
		return "CURSOR";
	default:
		return "<UNKNOWN PLANE>";
	}
}

#define STI_FPS_INTERVAL_MS     3000

void sti_plane_update_fps(struct sti_plane *plane,
			  bool new_frame,
			  bool new_field)
{
	struct drm_plane_state *state = plane->drm_plane.state;
	ktime_t now;
	struct sti_fps_info *fps;
	int fpks, fipks, ms_since_last, num_frames, num_fields;

	now = ktime_get();

	/* Compute number of frame updates */
	fps = &plane->fps_info;

	if (new_field)
		fps->curr_field_counter++;

	/* do not perform fps calcul if new_frame is false */
	if (!new_frame)
		return;

	fps->curr_frame_counter++;
	ms_since_last = ktime_to_ms(ktime_sub(now, fps->last_timestamp));
	num_frames = fps->curr_frame_counter - fps->last_frame_counter;

	if (num_frames <= 0  || ms_since_last < STI_FPS_INTERVAL_MS)
		return;

	fps->last_timestamp = now;
	fps->last_frame_counter = fps->curr_frame_counter;

	if (state->fb) {
		fpks = (num_frames * 1000000) / ms_since_last;
		snprintf(plane->fps_info.fps_str, FPS_LENGTH,
			 "%-8s %4dx%-4d %.4s @ %3d.%-3.3d fps (%s)",
			 plane->drm_plane.name,
			 state->fb->width,
			 state->fb->height,
			 (char *)&state->fb->format->format,
			 fpks / 1000, fpks % 1000,
			 sti_plane_to_str(plane));
	}

	if (fps->curr_field_counter) {
		/* Compute number of field updates */
		num_fields = fps->curr_field_counter - fps->last_field_counter;
		fps->last_field_counter = fps->curr_field_counter;
		fipks = (num_fields * 1000000) / ms_since_last;
		snprintf(plane->fps_info.fips_str,
			 FPS_LENGTH, " - %3d.%-3.3d field/sec",
			 fipks / 1000, fipks % 1000);
	} else {
		plane->fps_info.fips_str[0] = '\0';
	}

	if (fps->output)
		DRM_INFO("%s%s\n",
			 plane->fps_info.fps_str,
			 plane->fps_info.fips_str);
}

static int sti_plane_get_default_zpos(enum drm_plane_type type)
{
	switch (type) {
	case DRM_PLANE_TYPE_PRIMARY:
		return 0;
	case DRM_PLANE_TYPE_OVERLAY:
		return 1;
	case DRM_PLANE_TYPE_CURSOR:
		return 7;
	}
	return 0;
}

void sti_plane_reset(struct drm_plane *plane)
{
	drm_atomic_helper_plane_reset(plane);
	plane->state->zpos = sti_plane_get_default_zpos(plane->type);
}

static void sti_plane_attach_zorder_property(struct drm_plane *drm_plane,
					     enum drm_plane_type type)
{
	int zpos = sti_plane_get_default_zpos(type);

	switch (type) {
	case DRM_PLANE_TYPE_PRIMARY:
	case DRM_PLANE_TYPE_OVERLAY:
		drm_plane_create_zpos_property(drm_plane, zpos, 0, 6);
		break;
	case DRM_PLANE_TYPE_CURSOR:
		drm_plane_create_zpos_immutable_property(drm_plane, zpos);
		break;
	}
}

void sti_plane_init_property(struct sti_plane *plane,
			     enum drm_plane_type type)
{
	sti_plane_attach_zorder_property(&plane->drm_plane, type);

	DRM_DEBUG_DRIVER("drm plane:%d mapped to %s\n",
			 plane->drm_plane.base.id, sti_plane_to_str(plane));
}
