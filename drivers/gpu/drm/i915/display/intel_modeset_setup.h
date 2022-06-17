/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_MODESET_SETUP_H__
#define __INTEL_MODESET_SETUP_H__

struct drm_device;
struct drm_modeset_acquire_ctx;

void intel_modeset_setup_hw_state(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx);

#endif /* __INTEL_MODESET_SETUP_H__ */
