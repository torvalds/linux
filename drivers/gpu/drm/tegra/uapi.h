/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2020 NVIDIA Corporation */

#ifndef _TEGRA_DRM_UAPI_H
#define _TEGRA_DRM_UAPI_H

#include <linux/dma-mapping.h>
#include <linux/idr.h>
#include <linux/kref.h>
#include <linux/xarray.h>

#include <drm/drm.h>

struct drm_file;
struct drm_device;

struct tegra_drm_file {
	/* Legacy UAPI state */
	struct idr legacy_contexts;
	struct mutex lock;

	/* New UAPI state */
	struct xarray contexts;
};

struct tegra_drm_mapping {
	struct kref ref;

	struct device *dev;
	struct host1x_bo *bo;
	struct sg_table *sgt;
	enum dma_data_direction direction;
	dma_addr_t iova;
	dma_addr_t iova_end;
};

int tegra_drm_ioctl_channel_open(struct drm_device *drm, void *data,
				 struct drm_file *file);
int tegra_drm_ioctl_channel_close(struct drm_device *drm, void *data,
				  struct drm_file *file);
int tegra_drm_ioctl_channel_map(struct drm_device *drm, void *data,
				struct drm_file *file);
int tegra_drm_ioctl_channel_unmap(struct drm_device *drm, void *data,
				  struct drm_file *file);
int tegra_drm_ioctl_channel_submit(struct drm_device *drm, void *data,
				   struct drm_file *file);

void tegra_drm_uapi_close_file(struct tegra_drm_file *file);
void tegra_drm_mapping_put(struct tegra_drm_mapping *mapping);

#endif
