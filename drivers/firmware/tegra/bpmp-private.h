/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018, NVIDIA CORPORATION.
 */

#ifndef __FIRMWARE_TEGRA_BPMP_PRIVATE_H
#define __FIRMWARE_TEGRA_BPMP_PRIVATE_H

#include <soc/tegra/bpmp.h>

struct tegra_bpmp_ops {
	int (*init)(struct tegra_bpmp *bpmp);
	void (*deinit)(struct tegra_bpmp *bpmp);
	bool (*is_response_ready)(struct tegra_bpmp_channel *channel);
	bool (*is_request_ready)(struct tegra_bpmp_channel *channel);
	int (*ack_response)(struct tegra_bpmp_channel *channel);
	int (*ack_request)(struct tegra_bpmp_channel *channel);
	bool (*is_response_channel_free)(struct tegra_bpmp_channel *channel);
	bool (*is_request_channel_free)(struct tegra_bpmp_channel *channel);
	int (*post_response)(struct tegra_bpmp_channel *channel);
	int (*post_request)(struct tegra_bpmp_channel *channel);
	int (*ring_doorbell)(struct tegra_bpmp *bpmp);
	int (*resume)(struct tegra_bpmp *bpmp);
};

#if IS_ENABLED(CONFIG_ARCH_TEGRA_186_SOC) || \
    IS_ENABLED(CONFIG_ARCH_TEGRA_194_SOC) || \
    IS_ENABLED(CONFIG_ARCH_TEGRA_234_SOC)
extern const struct tegra_bpmp_ops tegra186_bpmp_ops;
#endif
#if IS_ENABLED(CONFIG_ARCH_TEGRA_210_SOC)
extern const struct tegra_bpmp_ops tegra210_bpmp_ops;
#endif

#endif
