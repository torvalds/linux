/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 SKIDATA GmbH
 * Author: Luca Ceresoli <luca.ceresoli@bootlin.com>
 */

#ifndef __TEGRA_VIP_H__
#define __TEGRA_VIP_H__

#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-subdev.h>

enum {
	TEGRA_VIP_PAD_SINK,
	TEGRA_VIP_PAD_SOURCE,
	TEGRA_VIP_PADS_NUM,
};

struct tegra_vip;

/**
 * struct tegra_vip_channel - Tegra VIP (parallel video capture) channel
 *
 * @subdev: V4L2 subdevice associated with this channel
 * @pads: media pads for the subdevice entity
 * @of_node: vip device tree node
 */
struct tegra_vip_channel {
	struct v4l2_subdev subdev;
	struct media_pad pads[TEGRA_VIP_PADS_NUM];
	struct device_node *of_node;
};

/**
 * struct tegra_vip_ops - Tegra VIP operations
 *
 * @vip_start_streaming: programs vip hardware to enable streaming.
 */
struct tegra_vip_ops {
	int (*vip_start_streaming)(struct tegra_vip_channel *vip_chan);
};

/**
 * struct tegra_vip_soc - NVIDIA Tegra VIP SoC structure
 *
 * @ops: vip hardware operations
 */
struct tegra_vip_soc {
	const struct tegra_vip_ops *ops;
};

/**
 * struct tegra_vip - NVIDIA Tegra VIP device structure
 *
 * @dev: device struct
 * @client: host1x_client struct
 * @soc: pointer to SoC data structure
 * @chan: the VIP channel
 */
struct tegra_vip {
	struct device *dev;
	struct host1x_client client;
	const struct tegra_vip_soc *soc;
	struct tegra_vip_channel chan;
};

#endif
