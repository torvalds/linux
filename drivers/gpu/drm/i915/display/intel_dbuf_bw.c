// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <drm/drm_print.h>

#include "intel_dbuf_bw.h"
#include "intel_display_core.h"
#include "intel_display_types.h"
#include "skl_watermark.h"

struct intel_dbuf_bw {
	unsigned int max_bw[I915_MAX_DBUF_SLICES];
	u8 active_planes[I915_MAX_DBUF_SLICES];
};

struct intel_dbuf_bw_state {
	struct intel_global_state base;
	struct intel_dbuf_bw dbuf_bw[I915_MAX_PIPES];
};

struct intel_dbuf_bw_state *to_intel_dbuf_bw_state(struct intel_global_state *obj_state)
{
	return container_of(obj_state, struct intel_dbuf_bw_state, base);
}

struct intel_dbuf_bw_state *
intel_atomic_get_old_dbuf_bw_state(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_global_state *dbuf_bw_state;

	dbuf_bw_state = intel_atomic_get_old_global_obj_state(state, &display->dbuf_bw.obj);

	return to_intel_dbuf_bw_state(dbuf_bw_state);
}

struct intel_dbuf_bw_state *
intel_atomic_get_new_dbuf_bw_state(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_global_state *dbuf_bw_state;

	dbuf_bw_state = intel_atomic_get_new_global_obj_state(state, &display->dbuf_bw.obj);

	return to_intel_dbuf_bw_state(dbuf_bw_state);
}

struct intel_dbuf_bw_state *
intel_atomic_get_dbuf_bw_state(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_global_state *dbuf_bw_state;

	dbuf_bw_state = intel_atomic_get_global_obj_state(state, &display->dbuf_bw.obj);
	if (IS_ERR(dbuf_bw_state))
		return ERR_CAST(dbuf_bw_state);

	return to_intel_dbuf_bw_state(dbuf_bw_state);
}

static bool intel_dbuf_bw_changed(struct intel_display *display,
				  const struct intel_dbuf_bw *old_dbuf_bw,
				  const struct intel_dbuf_bw *new_dbuf_bw)
{
	enum dbuf_slice slice;

	for_each_dbuf_slice(display, slice) {
		if (old_dbuf_bw->max_bw[slice] != new_dbuf_bw->max_bw[slice] ||
		    old_dbuf_bw->active_planes[slice] != new_dbuf_bw->active_planes[slice])
			return true;
	}

	return false;
}

static bool intel_dbuf_bw_state_changed(struct intel_display *display,
					const struct intel_dbuf_bw_state *old_dbuf_bw_state,
					const struct intel_dbuf_bw_state *new_dbuf_bw_state)
{
	enum pipe pipe;

	for_each_pipe(display, pipe) {
		const struct intel_dbuf_bw *old_dbuf_bw =
			&old_dbuf_bw_state->dbuf_bw[pipe];
		const struct intel_dbuf_bw *new_dbuf_bw =
			&new_dbuf_bw_state->dbuf_bw[pipe];

		if (intel_dbuf_bw_changed(display, old_dbuf_bw, new_dbuf_bw))
			return true;
	}

	return false;
}

static void skl_plane_calc_dbuf_bw(struct intel_dbuf_bw *dbuf_bw,
				   struct intel_crtc *crtc,
				   enum plane_id plane_id,
				   const struct skl_ddb_entry *ddb,
				   unsigned int data_rate)
{
	struct intel_display *display = to_intel_display(crtc);
	unsigned int dbuf_mask = skl_ddb_dbuf_slice_mask(display, ddb);
	enum dbuf_slice slice;

	/*
	 * The arbiter can only really guarantee an
	 * equal share of the total bw to each plane.
	 */
	for_each_dbuf_slice_in_mask(display, slice, dbuf_mask) {
		dbuf_bw->max_bw[slice] = max(dbuf_bw->max_bw[slice], data_rate);
		dbuf_bw->active_planes[slice] |= BIT(plane_id);
	}
}

static void skl_crtc_calc_dbuf_bw(struct intel_dbuf_bw *dbuf_bw,
				  const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum plane_id plane_id;

	memset(dbuf_bw, 0, sizeof(*dbuf_bw));

	if (!crtc_state->hw.active)
		return;

	for_each_plane_id_on_crtc(crtc, plane_id) {
		/*
		 * We assume cursors are small enough
		 * to not cause bandwidth problems.
		 */
		if (plane_id == PLANE_CURSOR)
			continue;

		skl_plane_calc_dbuf_bw(dbuf_bw, crtc, plane_id,
				       &crtc_state->wm.skl.plane_ddb[plane_id],
				       crtc_state->data_rate[plane_id]);

		if (DISPLAY_VER(display) < 11)
			skl_plane_calc_dbuf_bw(dbuf_bw, crtc, plane_id,
					       &crtc_state->wm.skl.plane_ddb_y[plane_id],
					       crtc_state->data_rate[plane_id]);
	}
}

/* "Maximum Data Buffer Bandwidth" */
int intel_dbuf_bw_min_cdclk(struct intel_display *display,
			    const struct intel_dbuf_bw_state *dbuf_bw_state)
{
	unsigned int total_max_bw = 0;
	enum dbuf_slice slice;

	for_each_dbuf_slice(display, slice) {
		int num_active_planes = 0;
		unsigned int max_bw = 0;
		enum pipe pipe;

		/*
		 * The arbiter can only really guarantee an
		 * equal share of the total bw to each plane.
		 */
		for_each_pipe(display, pipe) {
			const struct intel_dbuf_bw *dbuf_bw = &dbuf_bw_state->dbuf_bw[pipe];

			max_bw = max(dbuf_bw->max_bw[slice], max_bw);
			num_active_planes += hweight8(dbuf_bw->active_planes[slice]);
		}
		max_bw *= num_active_planes;

		total_max_bw = max(total_max_bw, max_bw);
	}

	return DIV_ROUND_UP(total_max_bw, 64);
}

int intel_dbuf_bw_calc_min_cdclk(struct intel_atomic_state *state,
				 bool *need_cdclk_calc)
{
	struct intel_display *display = to_intel_display(state);
	struct intel_dbuf_bw_state *new_dbuf_bw_state = NULL;
	const struct intel_dbuf_bw_state *old_dbuf_bw_state = NULL;
	const struct intel_crtc_state *old_crtc_state;
	const struct intel_crtc_state *new_crtc_state;
	struct intel_crtc *crtc;
	int ret, i;

	if (DISPLAY_VER(display) < 9)
		return 0;

	for_each_oldnew_intel_crtc_in_state(state, crtc, old_crtc_state,
					    new_crtc_state, i) {
		struct intel_dbuf_bw old_dbuf_bw, new_dbuf_bw;

		skl_crtc_calc_dbuf_bw(&old_dbuf_bw, old_crtc_state);
		skl_crtc_calc_dbuf_bw(&new_dbuf_bw, new_crtc_state);

		if (!intel_dbuf_bw_changed(display, &old_dbuf_bw, &new_dbuf_bw))
			continue;

		new_dbuf_bw_state = intel_atomic_get_dbuf_bw_state(state);
		if (IS_ERR(new_dbuf_bw_state))
			return PTR_ERR(new_dbuf_bw_state);

		old_dbuf_bw_state = intel_atomic_get_old_dbuf_bw_state(state);

		new_dbuf_bw_state->dbuf_bw[crtc->pipe] = new_dbuf_bw;
	}

	if (!old_dbuf_bw_state)
		return 0;

	if (intel_dbuf_bw_state_changed(display, old_dbuf_bw_state, new_dbuf_bw_state)) {
		ret = intel_atomic_lock_global_state(&new_dbuf_bw_state->base);
		if (ret)
			return ret;
	}

	ret = intel_cdclk_update_dbuf_bw_min_cdclk(state,
						   intel_dbuf_bw_min_cdclk(display, old_dbuf_bw_state),
						   intel_dbuf_bw_min_cdclk(display, new_dbuf_bw_state),
						   need_cdclk_calc);
	if (ret)
		return ret;

	return 0;
}

void intel_dbuf_bw_update_hw_state(struct intel_display *display)
{
	struct intel_dbuf_bw_state *dbuf_bw_state =
		to_intel_dbuf_bw_state(display->dbuf_bw.obj.state);
	struct intel_crtc *crtc;

	if (DISPLAY_VER(display) < 9)
		return;

	for_each_intel_crtc(display->drm, crtc) {
		const struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		skl_crtc_calc_dbuf_bw(&dbuf_bw_state->dbuf_bw[crtc->pipe], crtc_state);
	}
}

void intel_dbuf_bw_crtc_disable_noatomic(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_dbuf_bw_state *dbuf_bw_state =
		to_intel_dbuf_bw_state(display->dbuf_bw.obj.state);
	enum pipe pipe = crtc->pipe;

	if (DISPLAY_VER(display) < 9)
		return;

	memset(&dbuf_bw_state->dbuf_bw[pipe], 0, sizeof(dbuf_bw_state->dbuf_bw[pipe]));
}

static struct intel_global_state *
intel_dbuf_bw_duplicate_state(struct intel_global_obj *obj)
{
	struct intel_dbuf_bw_state *state;

	state = kmemdup(obj->state, sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	return &state->base;
}

static void intel_dbuf_bw_destroy_state(struct intel_global_obj *obj,
					struct intel_global_state *state)
{
	kfree(state);
}

static const struct intel_global_state_funcs intel_dbuf_bw_funcs = {
	.atomic_duplicate_state = intel_dbuf_bw_duplicate_state,
	.atomic_destroy_state = intel_dbuf_bw_destroy_state,
};

int intel_dbuf_bw_init(struct intel_display *display)
{
	struct intel_dbuf_bw_state *state;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	intel_atomic_global_obj_init(display, &display->dbuf_bw.obj,
				     &state->base, &intel_dbuf_bw_funcs);

	return 0;
}
