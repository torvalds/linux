/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_PPS_H__
#define __INTEL_PPS_H__

#include <linux/types.h>

#include "intel_wakeref.h"

struct drm_i915_private;
struct intel_connector;
struct intel_crtc_state;
struct intel_dp;
struct intel_encoder;

intel_wakeref_t intel_pps_lock(struct intel_dp *intel_dp);
intel_wakeref_t intel_pps_unlock(struct intel_dp *intel_dp, intel_wakeref_t wakeref);

#define with_intel_pps_lock(dp, wf)						\
	for ((wf) = intel_pps_lock(dp); (wf); (wf) = intel_pps_unlock((dp), (wf)))

void intel_dp_check_edp(struct intel_dp *intel_dp);
void intel_pps_backlight_on(struct intel_dp *intel_dp);
void intel_pps_backlight_off(struct intel_dp *intel_dp);
void intel_pps_backlight_power(struct intel_connector *connector, bool enable);

bool intel_pps_vdd_on_unlocked(struct intel_dp *intel_dp);
void intel_pps_vdd_off_unlocked(struct intel_dp *intel_dp, bool sync);
void intel_pps_on_unlocked(struct intel_dp *intel_dp);
void intel_pps_off_unlocked(struct intel_dp *intel_dp);

void intel_pps_vdd_on(struct intel_dp *intel_dp);
void intel_pps_on(struct intel_dp *intel_dp);
void intel_pps_off(struct intel_dp *intel_dp);
void intel_pps_vdd_off_sync(struct intel_dp *intel_dp);
bool intel_pps_have_power(struct intel_dp *intel_dp);

void wait_panel_power_cycle(struct intel_dp *intel_dp);

void intel_pps_init(struct intel_dp *intel_dp);
void intel_pps_encoder_reset(struct intel_dp *intel_dp);
void intel_power_sequencer_reset(struct drm_i915_private *i915);

void vlv_init_panel_power_sequencer(struct intel_encoder *encoder,
				    const struct intel_crtc_state *crtc_state);

#endif /* __INTEL_PPS_H__ */
