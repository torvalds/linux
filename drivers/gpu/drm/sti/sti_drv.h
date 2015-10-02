/*
 * Copyright (C) STMicroelectronics SA 2014
 * Author: Benjamin Gaignard <benjamin.gaignard@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef _STI_DRV_H_
#define _STI_DRV_H_

#include <drm/drmP.h>

struct sti_compositor;
struct sti_tvout;

/**
 * STI drm private structure
 * This structure is stored as private in the drm_device
 *
 * @compo:                 compositor
 * @plane_zorder_property: z-order property for CRTC planes
 * @drm_dev:               drm device
 */
struct sti_private {
	struct sti_compositor *compo;
	struct drm_property *plane_zorder_property;
	struct drm_device *drm_dev;

	struct {
		struct drm_atomic_state *state;
		struct work_struct work;
		struct mutex lock;
	} commit;
};

#endif
