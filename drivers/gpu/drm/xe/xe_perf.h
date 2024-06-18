/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_PERF_H_
#define _XE_PERF_H_

struct drm_device;
struct drm_file;

int xe_perf_ioctl(struct drm_device *dev, void *data, struct drm_file *file);

#endif
