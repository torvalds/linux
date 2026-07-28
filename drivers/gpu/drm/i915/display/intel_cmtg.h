/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 Intel Corporation
 */

#ifndef __INTEL_CMTG_H__
#define __INTEL_CMTG_H__

#include <linux/types.h>

struct intel_atomic_state;
struct intel_display;
struct intel_crtc_state;

enum set_timing_type {
	MODESET = 0,
	LRR
};

void intel_cmtg_disable(const struct intel_crtc_state *crtc_state);
void intel_cmtg_set_m_n(const struct intel_crtc_state *crtc_state);
void intel_cmtg_set_vrr_timings(const struct intel_crtc_state *crtc_state);
void intel_cmtg_set_vrr_ctl(const struct intel_crtc_state *crtc_state);
void intel_cmtg_set_timings(const struct intel_crtc_state *crtc_state, enum set_timing_type type);
void intel_cmtg_set_clk_select(const struct intel_crtc_state *crtc_state);
void intel_cmtg_sanitize(struct intel_display *display);
bool intel_cmtg_is_allowed(const struct intel_crtc_state *crtc_state);
void intel_cmtg_program(struct intel_atomic_state *state);

#endif /* __INTEL_CMTG_H__ */
