/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020-2021 Intel Corporation
 */

#ifndef __INTEL_FB_H__
#define __INTEL_FB_H__

#include <linux/types.h>

struct drm_framebuffer;

struct intel_plane_state;

bool is_ccs_plane(const struct drm_framebuffer *fb, int plane);
bool is_gen12_ccs_plane(const struct drm_framebuffer *fb, int plane);
bool is_gen12_ccs_cc_plane(const struct drm_framebuffer *fb, int plane);

bool is_surface_linear(const struct drm_framebuffer *fb, int color_plane);

int main_to_ccs_plane(const struct drm_framebuffer *fb, int main_plane);
int skl_ccs_to_main_plane(const struct drm_framebuffer *fb, int ccs_plane);
int skl_main_to_aux_plane(const struct drm_framebuffer *fb, int main_plane);

int intel_plane_check_stride(const struct intel_plane_state *plane_state);

#endif /* __INTEL_FB_H__ */
