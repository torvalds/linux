// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

#include <linux/iopoll.h>

#include <drm/drm_blend.h>
#include <drm/drm_print.h>
#include <drm/intel/display_parent_interface.h>

#include "intel_crtc.h"
#include "intel_de.h"
#include "intel_display_core.h"
#include "intel_display_regs.h"
#include "intel_display_types.h"
#include "intel_fb.h"
#include "intel_frontbuffer.h"
#include "intel_initial_plane.h"
#include "intel_parent.h"
#include "intel_plane.h"

struct intel_initial_plane_configs {
	struct intel_initial_plane_config config[I915_MAX_PIPES];
};

void intel_initial_plane_vblank_wait(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	u32 start_ts, end_ts;
	int ret;

	/* xe doesn't have interrupts enabled this early */
	if (intel_parent_irq_enabled(display)) {
		intel_crtc_wait_for_next_vblank(crtc);
		return;
	}

	start_ts = intel_de_read(display, PIPE_FRMTMSTMP(crtc->pipe));

	ret = poll_timeout_us(end_ts = intel_de_read(display, PIPE_FRMTMSTMP(crtc->pipe)),
			      end_ts != start_ts, 1000, 1000 * 1000, false);
	if (ret)
		drm_warn(display->drm, "[CRTC:%d:%s] early vblank wait timed out\n",
			 crtc->base.base.id, crtc->base.name);
}

static const struct intel_plane_state *
intel_reuse_initial_plane_obj(struct intel_crtc *this,
			      const struct intel_initial_plane_configs *all_plane_configs)
{
	struct intel_display *display = to_intel_display(this);
	struct intel_crtc *crtc;

	for_each_intel_crtc(display, crtc) {
		struct intel_plane *plane =
			to_intel_plane(crtc->base.primary);
		const struct intel_plane_state *plane_state =
			to_intel_plane_state(plane->base.state);
		const struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		if (!crtc_state->hw.active)
			continue;

		if (!plane_state->ggtt_vma)
			continue;

		if (all_plane_configs->config[this->pipe].base ==
		    all_plane_configs->config[crtc->pipe].base)
			return plane_state;
	}

	return NULL;
}

static struct drm_gem_object *
intel_alloc_initial_plane_obj(struct intel_display *display,
			      struct intel_initial_plane_config *plane_config)
{
	struct drm_framebuffer *fb = plane_config->fb;

	switch (fb->modifier) {
	case DRM_FORMAT_MOD_LINEAR:
		break;
	case I915_FORMAT_MOD_X_TILED:
	case I915_FORMAT_MOD_Y_TILED:
		/* fenced region needed for linear CPU access to tiled FB */
		if (intel_parent_has_fenced_regions(display))
			break;
		fallthrough;
	default:
		drm_dbg_kms(display->drm, "Unsupported modifier for initial FB: 0x%llx\n",
			    fb->modifier);
		return NULL;
	}

	/*
	 * Would need to preserve the DPT, its GGTT
	 * mapping, and the actual FB memory.
	 */
	if (intel_fb_modifier_uses_dpt(display, fb->modifier)) {
		drm_dbg_kms(display->drm, "DPT not supported for initial FB\n");
		return NULL;
	}

	/*
	 * Would need to preserve the 270 degree rotated
	 * GGTT mapping used by the display hardware.
	 */
	if (drm_rotation_90_or_270(plane_config->rotation)) {
		drm_dbg_kms(display->drm, "90/270 degree rotation not supported for initial FB\n");
		return NULL;
	}

	return display->parent->initial_plane->alloc_obj(display->drm, plane_config);
}

static void
intel_find_initial_plane_obj(struct intel_crtc *crtc,
			     struct intel_initial_plane_configs *all_plane_configs)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_initial_plane_config *plane_config = &all_plane_configs->config[crtc->pipe];
	struct intel_plane *plane = to_intel_plane(crtc->base.primary);
	struct intel_plane_state *plane_state = to_intel_plane_state(plane->base.state);
	struct drm_framebuffer *fb;
	struct i915_vma *vma;
	int ret;

	/*
	 * TODO:
	 *   Disable planes if get_initial_plane_config() failed.
	 *   Make sure things work if the surface base is not page aligned.
	 */
	if (!plane_config->fb)
		return;

	if (intel_alloc_initial_plane_obj(display, plane_config)) {
		fb = plane_config->fb;
		vma = plane_config->vma;
	} else {
		const struct intel_plane_state *other_plane_state;

		other_plane_state = intel_reuse_initial_plane_obj(crtc, all_plane_configs);
		if (!other_plane_state)
			goto nofb;

		fb = other_plane_state->hw.fb;
		vma = other_plane_state->ggtt_vma;
	}

	plane_state->uapi.rotation = plane_config->rotation;
	intel_fb_fill_view(to_intel_framebuffer(fb),
			   plane_state->uapi.rotation, &plane_state->view);

	ret = display->parent->initial_plane->setup(plane->base.state, plane_config, fb, vma);
	if (ret)
		goto nofb;

	plane_state->uapi.src_x = 0;
	plane_state->uapi.src_y = 0;
	plane_state->uapi.src_w = fb->width << 16;
	plane_state->uapi.src_h = fb->height << 16;

	plane_state->uapi.crtc_x = 0;
	plane_state->uapi.crtc_y = 0;
	plane_state->uapi.crtc_w = fb->width;
	plane_state->uapi.crtc_h = fb->height;

	plane_state->uapi.fb = fb;
	drm_framebuffer_get(fb);

	plane_state->uapi.crtc = &crtc->base;
	intel_plane_copy_uapi_to_hw_state(plane_state, plane_state, crtc);

	atomic_or(plane->frontbuffer_bit, &to_intel_frontbuffer(fb)->bits);

	return;

nofb:
	/*
	 * We've failed to reconstruct the BIOS FB.  Current display state
	 * indicates that the primary plane is visible, but has a NULL FB,
	 * which will lead to problems later if we don't fix it up.  The
	 * simplest solution is to just disable the primary plane now and
	 * pretend the BIOS never had it enabled.
	 */
	intel_plane_disable_noatomic(crtc, plane);
}

static void plane_config_fini(struct intel_display *display,
			      struct intel_initial_plane_config *plane_config)
{
	if (plane_config->fb) {
		struct drm_framebuffer *fb = plane_config->fb;

		/* We may only have the stub and not a full framebuffer */
		if (drm_framebuffer_read_refcount(fb))
			drm_framebuffer_put(fb);
		else
			kfree(fb);
	}

	display->parent->initial_plane->config_fini(plane_config);
}

void intel_initial_plane_config(struct intel_display *display)
{
	struct intel_initial_plane_configs all_plane_configs = {};
	struct intel_crtc *crtc;

	for_each_intel_crtc(display, crtc) {
		const struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct intel_initial_plane_config *plane_config =
			&all_plane_configs.config[crtc->pipe];

		if (!crtc_state->hw.active)
			continue;

		/*
		 * Note that reserving the BIOS fb up front prevents us
		 * from stuffing other stolen allocations like the ring
		 * on top.  This prevents some ugliness at boot time, and
		 * can even allow for smooth boot transitions if the BIOS
		 * fb is large enough for the active pipe configuration.
		 */
		display->modeset.funcs->get_initial_plane_config(crtc, plane_config);

		/*
		 * If the fb is shared between multiple heads, we'll
		 * just get the first one.
		 */
		intel_find_initial_plane_obj(crtc, &all_plane_configs);

		if (display->modeset.funcs->fixup_initial_plane_config(crtc, plane_config))
			intel_initial_plane_vblank_wait(crtc);

		plane_config_fini(display, plane_config);
	}
}
