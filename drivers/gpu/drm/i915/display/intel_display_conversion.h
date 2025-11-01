/* SPDX-License-Identifier: MIT */
/* Copyright © 2024 Intel Corporation */

/*
 * This header is for transitional struct intel_display conversion helpers only.
 */

#ifndef __INTEL_DISPLAY_CONVERSION__
#define __INTEL_DISPLAY_CONVERSION__

struct drm_device;
struct intel_display;

struct intel_display *__drm_to_display(struct drm_device *drm);

#endif /* __INTEL_DISPLAY_CONVERSION__ */
