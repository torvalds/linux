/*
 * Copyright (C) STMicroelectronics SA 2014
 * Author: Benjamin Gaignard <benjamin.gaignard@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef _STI_DRM_PLANE_H_
#define _STI_DRM_PLANE_H_

#include <drm/drmP.h>

struct sti_layer;

struct drm_plane *sti_drm_plane_init(struct drm_device *dev,
		struct sti_layer *layer,
		unsigned int possible_crtcs,
		enum drm_plane_type type);
#endif
