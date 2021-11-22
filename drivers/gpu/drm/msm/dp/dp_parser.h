/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_PARSER_H_
#define _DP_PARSER_H_

#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-dp.h>

#include "dpu_io_util.h"
#include "msm_drv.h"

#define DP_LABEL "MDSS DP DISPLAY"
#define DP_MAX_PIXEL_CLK_KHZ	675000
#define DP_MAX_NUM_DP_LANES	4

enum dp_pm_type {
	DP_CORE_PM,
	DP_CTRL_PM,
	DP_STREAM_PM,
	DP_PHY_PM,
	DP_MAX_PM
};

struct dss_io_region {
	size_t len;
	void __iomem *base;
};

struct dss_io_data {
	struct dss_io_region ahb;
	struct dss_io_region aux;
	struct dss_io_region link;
	struct dss_io_region p0;
};

static inline const char *dp_parser_pm_name(enum dp_pm_type module)
{
	switch (module) {
	case DP_CORE_PM:	return "DP_CORE_PM";
	case DP_CTRL_PM:	return "DP_CTRL_PM";
	case DP_STREAM_PM:	return "DP_STREAM_PM";
	case DP_PHY_PM:		return "DP_PHY_PM";
	default:		return "???";
	}
}

/**
 * struct dp_display_data  - display related device tree data.
 *
 * @ctrl_node: referece to controller device
 * @phy_node:  reference to phy device
 * @is_active: is the controller currently active
 * @name: name of the display
 * @display_type: type of the display
 */
struct dp_display_data {
	struct device_node *ctrl_node;
	struct device_node *phy_node;
	bool is_active;
	const char *name;
	const char *display_type;
};

/**
 * struct dp_ctrl_resource - controller's IO related data
 *
 * @dp_controller: Display Port controller mapped memory address
 * @phy_io: phy's mapped memory address
 */
struct dp_io {
	struct dss_io_data dp_controller;
	struct phy *phy;
	union phy_configure_opts phy_opts;
};

/**
 * struct dp_pinctrl - DP's pin control
 *
 * @pin: pin-controller's instance
 * @state_active: active state pin control
 * @state_hpd_active: hpd active state pin control
 * @state_suspend: suspend state pin control
 */
struct dp_pinctrl {
	struct pinctrl *pin;
	struct pinctrl_state *state_active;
	struct pinctrl_state *state_hpd_active;
	struct pinctrl_state *state_suspend;
};

#define DP_DEV_REGULATOR_MAX	4

/* Regulators for DP devices */
struct dp_reg_entry {
	char name[32];
	int enable_load;
	int disable_load;
};

struct dp_regulator_cfg {
	int num;
	struct dp_reg_entry regs[DP_DEV_REGULATOR_MAX];
};

/**
 * struct dp_parser - DP parser's data exposed to clients
 *
 * @pdev: platform data of the client
 * @mp: gpio, regulator and clock related data
 * @pinctrl: pin-control related data
 * @disp_data: controller's display related data
 * @parse: function to be called by client to parse device tree.
 */
struct dp_parser {
	struct platform_device *pdev;
	struct dss_module_power mp[DP_MAX_PM];
	struct dp_pinctrl pinctrl;
	struct dp_io io;
	struct dp_display_data disp_data;
	const struct dp_regulator_cfg *regulator_cfg;
	u32 max_dp_lanes;
	struct drm_bridge *panel_bridge;

	int (*parse)(struct dp_parser *parser, int connector_type);
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
