/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef __TEGRA_VIDEO_H__
#define __TEGRA_VIDEO_H__

#include <linux/host1x.h>

#include <media/media-device.h>
#include <media/v4l2-device.h>

#include "vi.h"
#include "csi.h"

struct tegra_video_device {
	struct v4l2_device v4l2_dev;
	struct media_device media_dev;
	struct tegra_vi *vi;
	struct tegra_csi *csi;
};

int tegra_v4l2_nodes_setup_tpg(struct tegra_video_device *vid);
void tegra_v4l2_nodes_cleanup_tpg(struct tegra_video_device *vid);

extern struct platform_driver tegra_vi_driver;
extern struct platform_driver tegra_csi_driver;
#endif
