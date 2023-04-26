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

static u32 dp_parser_link_frequencies(struct device_node *of_node)
{
	struct device_node *endpoint;
	u64 frequency = 0;
	int cnt;

	endpoint = of_graph_get_endpoint_by_regs(of_node, 1, 0); /* port@1 */
	if (!endpoint)
		return 0;

	cnt = of_property_count_u64_elems(endpoint, "link-frequencies");

	if (cnt > 0)
		of_property_read_u64_index(endpoint, "link-frequencies",
						cnt - 1, &frequency);
	of_node_put(endpoint);

	do_div(frequency,
		10 * /* from symbol rate to link rate */
		1000); /* kbytes */

	return frequency;
}

static int dp_parser_misc(struct dp_parser *parser)
{
	struct device_node *of_node = parser->pdev->dev.of_node;
	int cnt;

	/*
	 * data-lanes is the property of dp_out endpoint
	 */
	cnt = drm_of_get_data_lanes_count_ep(of_node, 1, 0, 1, DP_MAX_NUM_DP_LANES);
	if (cnt < 0) {
		/* legacy code, data-lanes is the property of mdss_dp node */
		cnt = drm_of_get_data_lanes_count(of_node, 1, DP_MAX_NUM_DP_LANES);
	}

	if (cnt > 0)
		parser->max_dp_lanes = cnt;
	else
		parser->max_dp_lanes = DP_MAX_NUM_DP_LANES; /* 4 lanes */

	parser->max_dp_link_rate = dp_parser_link_frequencies(of_node);
	if (!parser->max_dp_link_rate)
		parser->max_dp_link_rate = DP_LINK_RATE_HBR2;

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
	core_power->clocks = devm_kcalloc(dev,
			core_power->num_clk, sizeof(struct clk_bulk_data),
			GFP_KERNEL);
	if (!core_power->clocks)
		return -ENOMEM;

	/* Initialize the CTRL power module */
	if (ctrl_clk_count == 0) {
		DRM_ERROR("no ctrl clocks are defined\n");
		return -EINVAL;
	}

	ctrl_power->num_clk = ctrl_clk_count;
	ctrl_power->clocks = devm_kcalloc(dev,
			ctrl_power->num_clk, sizeof(struct clk_bulk_data),
			GFP_KERNEL);
	if (!ctrl_power->clocks) {
		ctrl_power->num_clk = 0;
		return -ENOMEM;
	}

	/* Initialize the STREAM power module */
	if (stream_clk_count == 0) {
		DRM_ERROR("no stream (pixel) clocks are defined\n");
		return -EINVAL;
	}

	stream_power->num_clk = stream_clk_count;
	stream_power->clocks = devm_kcalloc(dev,
			stream_power->num_clk, sizeof(struct clk_bulk_data),
			GFP_KERNEL);
	if (!stream_power->clocks) {
		stream_power->num_clk = 0;
		return -ENOMEM;
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
			core_power->clocks[core_clk_index].id = devm_kstrdup(dev, clk_name, GFP_KERNEL);
			core_clk_index++;
		} else if (dp_parser_check_prefix("stream", clk_name) &&
				stream_clk_index < stream_clk_count) {
			stream_power->clocks[stream_clk_index].id = devm_kstrdup(dev, clk_name, GFP_KERNEL);
			stream_clk_index++;
		} else if (dp_parser_check_prefix("ctrl", clk_name) &&
			   ctrl_clk_index < ctrl_clk_count) {
			ctrl_power->clocks[ctrl_clk_index].id = devm_kstrdup(dev, clk_name, GFP_KERNEL);
			ctrl_clk_index++;
		}
	}

	return 0;
}

int devm_dp_parser_find_next_bridge(struct device *dev, struct dp_parser *parser)
{
	struct platform_device *pdev = parser->pdev;
	struct drm_bridge *bridge;

	bridge = devm_drm_of_get_bridge(dev, pdev->dev.of_node, 1, 0);
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
