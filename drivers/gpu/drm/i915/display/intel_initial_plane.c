// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

#include <drm/intel/display_parent_interface.h>

#include "intel_display_core.h"
#include "intel_display_types.h"
#include "intel_initial_plane.h"

void intel_initial_plane_vblank_wait(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);

	display->parent->initial_plane->vblank_wait(&crtc->base);
}

void intel_initial_plane_config(struct intel_display *display)
{
	display->parent->initial_plane->config(display->drm);
}
