/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_CDCLK_H__
#define __INTEL_CDCLK_H__

#include <linux/types.h>

#include "intel_display_limits.h"
#include "intel_global_state.h"

struct intel_atomic_state;
struct intel_crtc_state;
struct intel_display;

struct intel_cdclk_config {
	unsigned int cdclk, vco, ref, bypass;
	u8 voltage_level;
	/* This field is only valid for Xe2LPD and above. */
	bool joined_mbus;
};

struct intel_cdclk_state {
	struct intel_global_state base;

	/*
	 * Logical configuration of cdclk (used for all scaling,
	 * watermark, etc. calculations and checks). This is
	 * computed as if all enabled crtcs were active.
	 */
	struct intel_cdclk_config logical;

	/*
	 * Actual configuration of cdclk, can be different from the
	 * logical configuration only when all crtc's are DPMS off.
	 */
	struct intel_cdclk_config actual;

	/* minimum acceptable cdclk to satisfy bandwidth requirements */
	int bw_min_cdclk;
	/* minimum acceptable cdclk for each pipe */
	int min_cdclk[I915_MAX_PIPES];
	/* minimum acceptable voltage level for each pipe */
	u8 min_voltage_level[I915_MAX_PIPES];

	/* pipe to which cd2x update is synchronized */
	enum pipe pipe;

	/* forced minimum cdclk for glk+ audio w/a */
	int force_min_cdclk;

	/* bitmask of active pipes */
	u8 active_pipes;

	/* update cdclk with pipes disabled */
	bool disable_pipes;
};

int intel_crtc_compute_min_cdclk(const struct intel_crtc_state *crtc_state);
void intel_cdclk_init_hw(struct intel_display *display);
void intel_cdclk_uninit_hw(struct intel_display *display);
void intel_init_cdclk_hooks(struct intel_display *display);
void intel_update_max_cdclk(struct intel_display *display);
void intel_update_cdclk(struct intel_display *display);
u32 intel_read_rawclk(struct intel_display *display);
bool intel_cdclk_clock_changed(const struct intel_cdclk_config *a,
			       const struct intel_cdclk_config *b);
int intel_mdclk_cdclk_ratio(struct intel_display *display,
			    const struct intel_cdclk_config *cdclk_config);
bool intel_cdclk_is_decreasing_later(struct intel_atomic_state *state);
void intel_set_cdclk_pre_plane_update(struct intel_atomic_state *state);
void intel_set_cdclk_post_plane_update(struct intel_atomic_state *state);
void intel_cdclk_dump_config(struct intel_display *display,
			     const struct intel_cdclk_config *cdclk_config,
			     const char *context);
int intel_modeset_calc_cdclk(struct intel_atomic_state *state);
void intel_cdclk_get_cdclk(struct intel_display *display,
			   struct intel_cdclk_config *cdclk_config);
int intel_cdclk_atomic_check(struct intel_atomic_state *state,
			     bool *need_cdclk_calc);
int intel_cdclk_state_set_joined_mbus(struct intel_atomic_state *state, bool joined_mbus);
struct intel_cdclk_state *
intel_atomic_get_cdclk_state(struct intel_atomic_state *state);

#define to_intel_cdclk_state(global_state) \
	container_of_const((global_state), struct intel_cdclk_state, base)

#define intel_atomic_get_old_cdclk_state(state) \
	to_intel_cdclk_state(intel_atomic_get_old_global_obj_state(state, &to_intel_display(state)->cdclk.obj))
#define intel_atomic_get_new_cdclk_state(state) \
	to_intel_cdclk_state(intel_atomic_get_new_global_obj_state(state, &to_intel_display(state)->cdclk.obj))

int intel_cdclk_init(struct intel_display *display);
void intel_cdclk_debugfs_register(struct intel_display *display);

#endif /* __INTEL_CDCLK_H__ */
