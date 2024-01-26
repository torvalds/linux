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
