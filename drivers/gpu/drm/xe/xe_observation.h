/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_OBSERVATION_H_
#define _XE_OBSERVATION_H_

#include <linux/types.h>

struct drm_device;
struct drm_file;

extern u32 xe_observation_paranoid;

int xe_observation_ioctl(struct drm_device *dev, void *data, struct drm_file *file);
int xe_observation_sysctl_register(void);
void xe_observation_sysctl_unregister(void);

#endif
