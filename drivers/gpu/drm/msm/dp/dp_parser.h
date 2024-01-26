/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_PARSER_H_
#define _DP_PARSER_H_

#include <linux/platform_device.h>

#include "msm_drv.h"

#define DP_MAX_PIXEL_CLK_KHZ	675000

/**
 * struct dp_parser - DP parser's data exposed to clients
 *
 * @pdev: platform data of the client
 * @phy: PHY handle
 */
struct dp_parser {
	struct platform_device *pdev;
	struct phy *phy;
	struct drm_bridge *next_bridge;
};

/**
 * dp_parser_get() - get the DP's device tree parser module
 *
 * @pdev: platform data of the client
 * return: pointer to dp_parser structure.
 *
 * This function provides client capability to parse the
 * device tree and populate the data structures. The data
 * related to clock, regulators, pin-control and other
 * can be parsed using this module.
 */
struct dp_parser *dp_parser_get(struct platform_device *pdev);

/**
 * devm_dp_parser_find_next_bridge() - find an additional bridge to DP
 *
 * @dev: device to tie bridge lifetime to
 * @parser: dp_parser data from client
 *
 * This function is used to find any additional bridge attached to
 * the DP controller. The eDP interface requires a panel bridge.
 *
 * Return: 0 if able to get the bridge, otherwise negative errno for failure.
 */
int devm_dp_parser_find_next_bridge(struct device *dev, struct dp_parser *parser);

#endif
