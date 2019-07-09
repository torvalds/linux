/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_COLOR_H__
#define __INTEL_COLOR_H__

struct intel_crtc_state;
struct intel_crtc;

void intel_color_init(struct intel_crtc *crtc);
int intel_color_check(struct intel_crtc_state *crtc_state);
void intel_color_commit(const struct intel_crtc_state *crtc_state);
void intel_color_load_luts(const struct intel_crtc_state *crtc_state);

#endif /* __INTEL_COLOR_H__ */
