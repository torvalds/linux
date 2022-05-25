// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/of_gpio.h>
#include <linux/phy/phy.h>

#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_bridge.h>

#include "dp_parser.h"
#include "dp_reg.h"

#define DP_DEFAULT_AHB_OFFSET	0x0000
#define DP_DEFAULT_AHB_SIZE	0x0200
#define DP_DEFAULT_AUX_OFFSET	0x0200
#define DP_DEFAULT_AUX_SIZE	0x0200
#define DP_DEFAULT_LINK_OFFSET	0x0400
#define DP_DEFAULT_LINK_SIZE	0x0C00
#define DP_DEFAULT_P0_OFFSET	0x1000
#define DP_DEFAULT_P0_SIZE	0x0400

static const struct dp_regulator_cfg sdm845_dp_reg_cfg = {
	.num = 2,
	.regs = {
		{"vdda-1p2", 21800, 4 },	/* 1.2 V */
		{"vdda-0p9", 36000, 32 },	/* 0.9 V */
	},
};

static void __iomem *dp_ioremap(struct platform_device *pdev, int idx, size_t *len)
{
	struct resource *res;
	void __iomem *base;

	base = devm_platform_get_and_ioremap_resource(pdev, idx, &res);
	if (!IS_ERR(base))
		*len = resource_size(res);

	return base;
}

static int dp_parser_ctrl_res(struct dp_parser *parser)
{
	struct platform_device *pdev = parser->pdev;
	struct dp_io *io = &parser->io;
	struct dss_io_data *dss = &io->dp_controller;

	dss->ahb.base = dp_ioremap(pdev, 0, &dss->ahb.len);
	if (IS_ERR(dss->ahb.base))
		return PTR_ERR(dss->ahb.base);

	dss->aux.base = dp_ioremap(pdev, 1, &dss->aux.len);
	if (IS_ERR(dss->aux.base)) {
		/*
		 * The initial binding had a single reg, but in order to
		 * support variation in the sub-region sizes this was split.
		 * dp_ioremap() will fail with -EINVAL here if only a single
		 * reg is specified, so fill in the sub-region offsets and
		 * lengths based on this single region.
		 */
		if (PTR_ERR(dss->aux.base) == -EINVAL) {
			if (dss->ahb.len < DP_DEFAULT_P0_OFFSET + DP_DEFAULT_P0_SIZE) {
				DRM_ERROR("legacy memory region not large enough\n");
				return -EINVAL;
			}

			dss->ahb.len = DP_DEFAULT_AHB_SIZE;
			dss->aux.base = dss->ahb.base + DP_DEFAULT_AUX_OFFSET;
			dss->aux.len = DP_DEFAULT_AUX_SIZE;
			dss->link.base = dss->ahb.base + DP_DEFAULT_LINK_OFFSET;
			dss->link.len = DP_DEFAULT_LINK_SIZE;
			dss->p0.base = dss->ahb.base + DP_DEFAULT_P0_OFFSET;
			dss->p0.len = DP_DEFAULT_P0_SIZE;
		} else {
			DRM_ERROR("unable to remap aux region: %pe\n", dss->aux.base);
			return PTR_ERR(dss->aux.base);
		}
	} else {
		dss->link.base = dp_ioremap(pdev, 2, &dss->link.len);
		if (IS_ERR(dss->link.base)) {
			DRM_ERROR("unable to remap link region: %pe\n", dss->link.base);
			return PTR_ERR(dss->link.base);
		}

		dss->p0.base = dp_ioremap(pdev, 3, &dss->p0.len);
		if (IS_ERR(dss->p0.base)) {
			DRM_ERROR("unable to remap p0 region: %pe\n", dss->p0.base);
			return PTR_ERR(dss->p0.base);
		}
	}

	io->phy = devm_phy_get(&pdev->dev, "dp");
	if (IS_ERR(io->phy))
		return PTR_ERR(io->phy);

	return 0;
}

static int dp_parser_misc(struct dp_parser *parser)
{
	struct device_node *of_node = parser->pdev->dev.of_node;
	int len = 0;
	const char *data_lane_property = "data-lanes";

	len = of_property_count_elems_of_size(of_node,
			 data_lane_property, sizeof(u32));
	if (len < 0) {
		DRM_WARN("Invalid property %s, default max DP lanes = %d\n",
				data_lane_property, DP_MAX_NUM_DP_LANES);
		len = DP_MAX_NUM_DP_LANES;
	}

	parser->max_dp_lanes = len;
	return 0;
}

static inline bool dp_parser_check_prefix(const char *clk_prefix,
						const char *clk_name)
{
	return !strncmp(clk_prefix, clk_name, strlen(clk_prefix));
}

static int dp_parser_init_clk_data(struct dp_parser *parser)
{
	int num_clk, i, rc;
	int core_clk_count = 0, ctrl_clk_count = 0, stream_clk_count = 0;
	const char *clk_name;
	struct device *dev = &parser->pdev->dev;
	struct dss_module_power *core_power = &parser->mp[DP_CORE_PM];
	struct dss_module_power *ctrl_power = &parser->mp[DP_CTRL_PM];
	struct dss_module_power *stream_power = &parser->mp[DP_STREAM_PM];

	num_clk = of_property_count_strings(dev->of_node, "clock-names");
	if (num_clk <= 0) {
		DRM_ERROR("no clocks are defined\n");
		return -EINVAL;
	}

	for (i = 0; i < num_clk; i++) {
		rc = of_property_read_string_index(dev->of_node,
				"clock-names", i, &clk_name);
		if (rc < 0)
			return rc;

		if (dp_parser_check_prefix("core", clk_name))
			core_clk_count++;

		if (dp_parser_check_prefix("ctrl", clk_name))
			ctrl_clk_count++;

		if (dp_parser_check_prefix("stream", clk_name))
			stream_clk_count++;
	}

	/* Initialize the CORE power module */
	if (core_clk_count == 0) {
		DRM_ERROR("no core clocks are defined\n");
		return -EINVAL;
	}

	core_power->num_clk = core_clk_count;
	core_power->clk_config = devm_kzalloc(dev,
			sizeof(struct dss_clk) * core_power->num_clk,
			GFP_KERNEL);
	if (!core_power->clk_config)
		return -EINVAL;

	/* Initialize the CTRL power module */
	if (ctrl_clk_count == 0) {
		DRM_ERROR("no ctrl clocks are defined\n");
		return -EINVAL;
	}

	ctrl_power->num_clk = ctrl_clk_count;
	ctrl_power->clk_config = devm_kzalloc(dev,
			sizeof(struct dss_clk) * ctrl_power->num_clk,
			GFP_KERNEL);
	if (!ctrl_power->clk_config) {
		ctrl_power->num_clk = 0;
		return -EINVAL;
	}

	/* Initialize the STREAM power module */
	if (stream_clk_count == 0) {
		DRM_ERROR("no stream (pixel) clocks are defined\n");
		return -EINVAL;
	}

	stream_power->num_clk = stream_clk_count;
	stream_power->clk_config = devm_kzalloc(dev,
			sizeof(struct dss_clk) * stream_power->num_clk,
			GFP_KERNEL);
	if (!stream_power->clk_config) {
		stream_power->num_clk = 0;
		return -EINVAL;
	}

	return 0;
}

static int dp_parser_clock(struct dp_parser *parser)
{
	int rc = 0, i = 0;
	int num_clk = 0;
	int core_clk_index = 0, ctrl_clk_index = 0, stream_clk_index = 0;
	int core_clk_count = 0, ctrl_clk_count = 0, stream_clk_count = 0;
	const char *clk_name;
	struct device *dev = &parser->pdev->dev;
	struct dss_module_power *core_power = &parser->mp[DP_CORE_PM];
	struct dss_module_power *ctrl_power = &parser->mp[DP_CTRL_PM];
	struct dss_module_power *stream_power = &parser->mp[DP_STREAM_PM];

	rc =  dp_parser_init_clk_data(parser);
	if (rc) {
		DRM_ERROR("failed to initialize power data %d\n", rc);
		return -EINVAL;
	}

	core_clk_count = core_power->num_clk;
	ctrl_clk_count = ctrl_power->num_clk;
	stream_clk_count = stream_power->num_clk;

	num_clk = core_clk_count + ctrl_clk_count + stream_clk_count;

	for (i = 0; i < num_clk; i++) {
		rc = of_property_read_string_index(dev->of_node, "clock-names",
				i, &clk_name);
		if (rc) {
			DRM_ERROR("error reading clock-names %d\n", rc);
			return rc;
		}
		if (dp_parser_check_prefix("core", clk_name) &&
				core_clk_index < core_clk_count) {
			struct dss_clk *clk =
				&core_power->clk_config[core_clk_index];
			strlcpy(clk->clk_name, clk_name, sizeof(clk->clk_name));
			clk->type = DSS_CLK_AHB;
			core_clk_index++;
		} else if (dp_parser_check_prefix("stream", clk_name) &&
				stream_clk_index < stream_clk_count) {
			struct dss_clk *clk =
				&stream_power->clk_config[stream_clk_index];
			strlcpy(clk->clk_name, clk_name, sizeof(clk->clk_name));
			clk->type = DSS_CLK_PCLK;
			stream_clk_index++;
		} else if (dp_parser_check_prefix("ctrl", clk_name) &&
			   ctrl_clk_index < ctrl_clk_count) {
			struct dss_clk *clk =
				&ctrl_power->clk_config[ctrl_clk_index];
			strlcpy(clk->clk_name, clk_name, sizeof(clk->clk_name));
			ctrl_clk_index++;
			if (dp_parser_check_prefix("ctrl_link", clk_name) ||
			    dp_parser_check_prefix("stream_pixel", clk_name))
				clk->type = DSS_CLK_PCLK;
			else
				clk->type = DSS_CLK_AHB;
		}
	}

	return 0;
}

int dp_parser_find_next_bridge(struct dp_parser *parser)
{
	struct device *dev = &parser->pdev->dev;
	struct drm_bridge *bridge;

	bridge = devm_drm_of_get_bridge(dev, dev->of_node, 1, 0);
	if (IS_ERR(bridge))
		return PTR_ERR(bridge);

	parser->next_bridge = bridge;

	return 0;
}

static int dp_parser_parse(struct dp_parser *parser)
{
	int rc = 0;

	if (!parser) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	rc = dp_parser_ctrl_res(parser);
	if (rc)
		return rc;

	rc = dp_parser_misc(parser);
	if (rc)
		return rc;

	rc = dp_parser_clock(parser);
	if (rc)
		return rc;

	/* Map the corresponding regulator information according to
	 * version. Currently, since we only have one supported platform,
	 * mapping the regulator directly.
	 */
	parser->regulator_cfg = &sdm845_dp_reg_cfg;

	return 0;
}

struct dp_parser *dp_parser_get(struct platform_device *pdev)
{
	struct dp_parser *parser;

	parser = devm_kzalloc(&pdev->dev, sizeof(*parser), GFP_KERNEL);
	if (!parser)
		return ERR_PTR(-ENOMEM);

	parser->parse = dp_parser_parse;
	parser->pdev = pdev;

	return parser;
}
