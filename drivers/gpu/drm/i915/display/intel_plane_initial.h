/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __INTEL_PLANE_INITIAL_H__
#define __INTEL_PLANE_INITIAL_H__

struct intel_crtc;
struct intel_display;

void intel_initial_plane_config(struct intel_display *display);
void intel_plane_initial_vblank_wait(struct intel_crtc *crtc);

#endif
