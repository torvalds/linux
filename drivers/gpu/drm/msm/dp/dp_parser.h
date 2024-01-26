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

#endif
