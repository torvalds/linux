/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_CDCLK_H__
#define __INTEL_CDCLK_H__

#include <linux/types.h>

enum pipe;
struct intel_atomic_state;
struct intel_cdclk_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_display;

struct intel_cdclk_config {
	unsigned int cdclk, vco, ref, bypass;
	u8 voltage_level;
	/* This field is only valid for Xe2LPD and above. */
	bool joined_mbus;
};

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
void intel_cdclk_update_hw_state(struct intel_display *display);
void intel_cdclk_crtc_disable_noatomic(struct intel_crtc *crtc);

#define to_intel_cdclk_state(global_state) \
	container_of_const((global_state), struct intel_cdclk_state, base)

#define intel_atomic_get_old_cdclk_state(state) \
	to_intel_cdclk_state(intel_atomic_get_old_global_obj_state(state, &to_intel_display(state)->cdclk.obj))
#define intel_atomic_get_new_cdclk_state(state) \
	to_intel_cdclk_state(intel_atomic_get_new_global_obj_state(state, &to_intel_display(state)->cdclk.obj))

int intel_cdclk_init(struct intel_display *display);
void intel_cdclk_debugfs_register(struct intel_display *display);

int intel_cdclk_logical(const struct intel_cdclk_state *cdclk_state);
int intel_cdclk_actual(const struct intel_cdclk_state *cdclk_state);
int intel_cdclk_actual_voltage_level(const struct intel_cdclk_state *cdclk_state);
int intel_cdclk_min_cdclk(const struct intel_cdclk_state *cdclk_state, enum pipe pipe);
int intel_cdclk_bw_min_cdclk(const struct intel_cdclk_state *cdclk_state);
bool intel_cdclk_pmdemand_needs_update(struct intel_atomic_state *state);
void intel_cdclk_force_min_cdclk(struct intel_cdclk_state *cdclk_state, int force_min_cdclk);
void intel_cdclk_read_hw(struct intel_display *display);

#endif /* __INTEL_CDCLK_H__ */
