/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_POWER_H_
#define _DP_POWER_H_

#include "dp_parser.h"

/**
 * sruct dp_power - DisplayPort's power related data
 *
 * @init: initializes the regulators/core clocks/GPIOs/pinctrl
 * @deinit: turns off the regulators/core clocks/GPIOs/pinctrl
 * @clk_enable: enable/disable the DP clocks
 * @set_pixel_clk_parent: set the parent of DP pixel clock
 */
struct dp_power {
	bool core_clks_on;
	bool link_clks_on;
	bool stream_clks_on;
};

/**
 * dp_power_init() - enable power supplies for display controller
 *
 * @power: instance of power module
 * @flip: bool for flipping gpio direction
 * return: 0 if success or error if failure.
 *
 * This API will turn on the regulators and configures gpio's
 * aux/hpd.
 */
int dp_power_init(struct dp_power *power, bool flip);

/**
 * dp_power_deinit() - turn off regulators and gpios.
 *
 * @power: instance of power module
 * return: 0 for success
 *
 * This API turns off power and regulators.
 */
int dp_power_deinit(struct dp_power *power);

/**
 * dp_power_clk_status() - display controller clocks status
 *
 * @power: instance of power module
 * @pm_type: type of pm, core/ctrl/phy
 * return: status of power clocks
 *
 * This API return status of DP clocks
 */

int dp_power_clk_status(struct dp_power *dp_power, enum dp_pm_type pm_type);

/**
 * dp_power_clk_enable() - enable display controller clocks
 *
 * @power: instance of power module
 * @pm_type: type of pm, core/ctrl/phy
 * @enable: enables or disables
 * return: pointer to allocated power module data
 *
 * This API will call setrate and enable for DP clocks
 */

int dp_power_clk_enable(struct dp_power *power, enum dp_pm_type pm_type,
				bool enable);

/**
 * dp_power_client_init() - initialize clock and regulator modules
 *
 * @power: instance of power module
 * return: 0 for success, error for failure.
 *
 * This API will configure the DisplayPort's clocks and regulator
 * modules.
 */
int dp_power_client_init(struct dp_power *power);

/**
 * dp_power_clinet_deinit() - de-initialize clock and regulator modules
 *
 * @power: instance of power module
 * return: 0 for success, error for failure.
 *
 * This API will de-initialize the DisplayPort's clocks and regulator
 * modules.
 */
void dp_power_client_deinit(struct dp_power *power);

/**
 * dp_power_get() - configure and get the DisplayPort power module data
 *
 * @parser: instance of parser module
 * return: pointer to allocated power module data
 *
 * This API will configure the DisplayPort's power module and provides
 * methods to be called by the client to configure the power related
 * modules.
 */
struct dp_power *dp_power_get(struct device *dev, struct dp_parser *parser);

#endif /* _DP_POWER_H_ */
