// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <linux/errno.h>

#include <drm/xe_drm.h>

#include "xe_perf.h"

/**
 * xe_perf_ioctl - The top level perf layer ioctl
 * @dev: @drm_device
 * @data: pointer to struct @drm_xe_perf_param
 * @file: @drm_file
 *
 * The function is called for different perf streams types and allows execution
 * of different operations supported by those perf stream types.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_perf_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_xe_perf_param *arg = data;

	if (arg->extensions)
		return -EINVAL;

	switch (arg->perf_type) {
	default:
		return -EINVAL;
	}
}
