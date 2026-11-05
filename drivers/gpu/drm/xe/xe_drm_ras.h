/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */
#ifndef _XE_DRM_RAS_H_
#define _XE_DRM_RAS_H_

#include <linux/types.h>

struct xe_device;

#define for_each_error_severity(i)	\
	for (i = 0; i < DRM_XE_RAS_ERR_SEV_MAX; i++)

int xe_drm_ras_init(struct xe_device *xe);
void xe_drm_ras_event(struct xe_device *xe, u32 component, u32 severity, u32 value);

#endif
