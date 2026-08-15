/* SPDX-License-Identifier: MIT */
/* Copyright © 2026 Intel Corporation */

#ifndef __XE_DISPLAY_BO_H__
#define __XE_DISPLAY_BO_H__

#include <linux/types.h>

struct xe_device;

bool xe_display_bo_fbdev_prefer_stolen(struct xe_device *xe, unsigned int size);

extern const struct intel_display_bo_interface xe_display_bo_interface;

#endif
