/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __INTEL_PPS_H__
#define __INTEL_PPS_H__

#include <linux/types.h>

#include "intel_wakeref.h"

enum pipe;
struct intel_connector;
struct intel_crtc_state;
struct intel_display;
struct intel_dp;
struct intel_encoder;

intel_wakeref_t intel_pps_lock(struct intel_dp *intel_dp);
intel_wakeref_t intel_pps_unlock(struct intel_dp *intel_dp, intel_wakeref_t wakeref);

#define with_intel_pps_lock(dp, wf)						\
	for ((wf) = intel_pps_lock(dp); (wf); (wf) = intel_pps_unlock((dp), (wf)))

void intel_pps_backlight_on(struct intel_dp *intel_dp);
void intel_pps_backlight_off(struct intel_dp *intel_dp);
void intel_pps_backlight_power(struct intel_connector *connector, bool enable);

bool intel_pps_vdd_on_unlocked(struct intel_dp *intel_dp);
void intel_pps_vdd_off_unlocked(struct intel_dp *intel_dp, bool sync);
void intel_pps_on_unlocked(struct intel_dp *intel_dp);
void intel_pps_off_unlocked(struct intel_dp *intel_dp);
void intel_pps_check_power_unlocked(struct intel_dp *intel_dp);

void intel_pps_vdd_on(struct intel_dp *intel_dp);
void intel_pps_vdd_off(struct intel_dp *intel_dp);
void intel_pps_on(struct intel_dp *intel_dp);
void intel_pps_off(struct intel_dp *intel_dp);
void intel_pps_vdd_off_sync(struct intel_dp *intel_dp);
bool intel_pps_have_panel_power_or_vdd(struct intel_dp *intel_dp);
void intel_pps_wait_power_cycle(struct intel_dp *intel_dp);

bool intel_pps_init(struct intel_dp *intel_dp);
void intel_pps_init_late(struct intel_dp *intel_dp);
void intel_pps_encoder_reset(struct intel_dp *intel_dp);

void vlv_pps_pipe_init(struct intel_dp *intel_dp);
void vlv_pps_pipe_reset(struct intel_dp *intel_dp);
enum pipe vlv_pps_backlight_initial_pipe(struct intel_dp *intel_dp);
void vlv_pps_port_enable_unlocked(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state);
void vlv_pps_port_disable(struct intel_encoder *encoder,
			  const struct intel_crtc_state *crtc_state);
void vlv_pps_reset_all(struct intel_display *display);
void bxt_pps_reset_all(struct intel_display *display);

void intel_pps_unlock_regs_wa(struct intel_display *display);
void intel_pps_setup(struct intel_display *display);

void intel_pps_connector_debugfs_add(struct intel_connector *connector);

void assert_pps_unlocked(struct intel_display *display, enum pipe pipe);

#endif /* __INTEL_PPS_H__ */
