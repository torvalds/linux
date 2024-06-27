/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_PERF_H_
#define _XE_PERF_H_

#include <linux/types.h>

struct drm_device;
struct drm_file;

extern u32 xe_perf_stream_paranoid;

int xe_perf_ioctl(struct drm_device *dev, void *data, struct drm_file *file);
int xe_perf_sysctl_register(void);
void xe_perf_sysctl_unregister(void);

#endif
