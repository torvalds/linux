// SPDX-License-Identifier: GPL-2.0
/* Copyright 2023 Collabora ltd. */
/* Copyright 2023 Amazon.com, Inc. or its affiliates. */

#include <linux/debugfs.h>
#include <linux/platform_device.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_file.h>
#include <drm/panfrost_drm.h>

#include "panfrost_device.h"
#include "panfrost_gpu.h"
#include "panfrost_debugfs.h"

void panfrost_debugfs_init(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	struct panfrost_device *pfdev = platform_get_drvdata(to_platform_device(dev->dev));

	debugfs_create_atomic_t("profile", 0600, minor->debugfs_root, &pfdev->profile_mode);
}
