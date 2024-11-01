/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __I915_IOCTL_H__
#define __I915_IOCTL_H__

struct drm_device;
struct drm_file;

int i915_reg_read_ioctl(struct drm_device *dev, void *data, struct drm_file *file);

#endif /* __I915_IOCTL_H__ */
