/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2020 NVIDIA Corporation */

#ifndef _TEGRA_DRM_UAPI_SUBMIT_H
#define _TEGRA_DRM_UAPI_SUBMIT_H

struct tegra_drm_used_mapping {
	struct tegra_drm_mapping *mapping;
	u32 flags;
};

struct tegra_drm_submit_data {
	struct tegra_drm_used_mapping *used_mappings;
	u32 num_used_mappings;
};

#endif
