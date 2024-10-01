/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_WAIT_USER_FENCE_H_
#define _XE_WAIT_USER_FENCE_H_

struct drm_device;
struct drm_file;

int xe_wait_user_fence_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file);

#endif
