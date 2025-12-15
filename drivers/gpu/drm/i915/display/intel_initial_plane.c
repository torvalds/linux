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

static void
intel_find_initial_plane_obj(struct intel_crtc *crtc,
			     struct intel_initial_plane_config plane_configs[])
{
	struct intel_display *display = to_intel_display(crtc);

	display->parent->initial_plane->find_obj(&crtc->base, plane_configs);
}

static void plane_config_fini(struct intel_display *display,
			      struct intel_initial_plane_config *plane_config)
{
	display->parent->initial_plane->config_fini(plane_config);
}

void intel_initial_plane_config(struct intel_display *display)
{
	struct intel_initial_plane_config plane_configs[I915_MAX_PIPES] = {};
	struct intel_crtc *crtc;

	for_each_intel_crtc(display->drm, crtc) {
		const struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct intel_initial_plane_config *plane_config =
			&plane_configs[crtc->pipe];

		if (!crtc_state->hw.active)
			continue;

		/*
		 * Note that reserving the BIOS fb up front prevents us
		 * from stuffing other stolen allocations like the ring
		 * on top.  This prevents some ugliness at boot time, and
		 * can even allow for smooth boot transitions if the BIOS
		 * fb is large enough for the active pipe configuration.
		 */
		display->funcs.display->get_initial_plane_config(crtc, plane_config);

		/*
		 * If the fb is shared between multiple heads, we'll
		 * just get the first one.
		 */
		intel_find_initial_plane_obj(crtc, plane_configs);

		if (display->funcs.display->fixup_initial_plane_config(crtc, plane_config))
			intel_initial_plane_vblank_wait(crtc);

		plane_config_fini(display, plane_config);
	}
}
