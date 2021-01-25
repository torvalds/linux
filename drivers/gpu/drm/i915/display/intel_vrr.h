/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_VRR_H__
#define __INTEL_VRR_H__

#include <linux/types.h>

struct drm_connector;

bool intel_vrr_is_capable(struct drm_connector *connector);

#endif /* __INTEL_VRR_H__ */
