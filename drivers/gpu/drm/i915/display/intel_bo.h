/* SPDX-License-Identifier: MIT */
/* Copyright © 2024 Intel Corporation */

#ifndef __INTEL_BO__
#define __INTEL_BO__

#include <linux/types.h>

struct drm_gem_object;

bool intel_bo_is_tiled(struct drm_gem_object *obj);

#endif /* __INTEL_BO__ */
