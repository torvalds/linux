/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_EXEC_H_
#define _XE_EXEC_H_

struct drm_device;
struct drm_file;

int xe_exec_ioctl(struct drm_device *dev, void *data, struct drm_file *file);

#endif
