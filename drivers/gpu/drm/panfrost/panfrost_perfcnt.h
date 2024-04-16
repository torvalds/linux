/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2019 Collabora Ltd */
#ifndef __PANFROST_PERFCNT_H__
#define __PANFROST_PERFCNT_H__

#include "panfrost_device.h"

void panfrost_perfcnt_sample_done(struct panfrost_device *pfdev);
void panfrost_perfcnt_clean_cache_done(struct panfrost_device *pfdev);
int panfrost_perfcnt_init(struct panfrost_device *pfdev);
void panfrost_perfcnt_fini(struct panfrost_device *pfdev);
void panfrost_perfcnt_close(struct drm_file *file_priv);
int panfrost_ioctl_perfcnt_enable(struct drm_device *dev, void *data,
				  struct drm_file *file_priv);
int panfrost_ioctl_perfcnt_dump(struct drm_device *dev, void *data,
				struct drm_file *file_priv);

#endif
