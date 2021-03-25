/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020-2021 Intel Corporation
 */

#ifndef __INTEL_FB_H__
#define __INTEL_FB_H__

#include <linux/types.h>

struct drm_framebuffer;

bool is_ccs_plane(const struct drm_framebuffer *fb, int plane);
bool is_gen12_ccs_plane(const struct drm_framebuffer *fb, int plane);
bool is_gen12_ccs_cc_plane(const struct drm_framebuffer *fb, int plane);

#endif /* __INTEL_FB_H__ */
