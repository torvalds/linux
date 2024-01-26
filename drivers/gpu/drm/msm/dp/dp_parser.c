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

static int dp_parser_ctrl_res(struct dp_parser *parser)
{
	struct platform_device *pdev = parser->pdev;

	parser->phy = devm_phy_get(&pdev->dev, "dp");
	if (IS_ERR(parser->phy))
		return PTR_ERR(parser->phy);

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

	return 0;
}

struct dp_parser *dp_parser_get(struct platform_device *pdev)
{
	struct dp_parser *parser;
	int ret;

	parser = devm_kzalloc(&pdev->dev, sizeof(*parser), GFP_KERNEL);
	if (!parser)
		return ERR_PTR(-ENOMEM);

	parser->pdev = pdev;

	ret = dp_parser_parse(parser);
	if (ret) {
		dev_err(&pdev->dev, "device tree parsing failed\n");
		return ERR_PTR(ret);
	}

	return parser;
}
