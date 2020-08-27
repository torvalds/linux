// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/device.h>

#include "dp_parser.h"
#include "dp_pll.h"
#include "dp_pll_private.h"

static int dp_pll_get_phy_io(struct dp_parser *parser)
{
	struct dp_io *io = &parser->io;

	io->usb3_dp_com.base = ioremap(REG_USB3_DP_COM_REGION_BASE,
					REG_USB3_DP_COM_REGION_SIZE);
	if (!io->usb3_dp_com.base) {
		DRM_ERROR("unable to map USB3 DP COM IO\n");
		return -EIO;
	}

	/* ToDo(user): DP PLL and DP PHY will not be part of
	 * DP driver eventually so for now Hardcode Base and offsets
	 * of PHY registers so we can remove them from dts and bindings
	 */
	io->phy_reg.base = ioremap(REG_DP_PHY_REGION_BASE,
					REG_DP_PHY_REGION_SIZE);
	if (!io->phy_reg.base) {
		DRM_ERROR("DP PHY io region mapping failed\n");
		return -EIO;
	}
	io->phy_reg.len = REG_DP_PHY_REGION_SIZE;

	return 0;
}

static int msm_dp_pll_init(struct msm_dp_pll *pll,
			enum msm_dp_pll_type type, int id)
{
	struct device *dev = &pll->pdev->dev;
	int ret = 0;

	switch (type) {
	case MSM_DP_PLL_10NM:
		ret = msm_dp_pll_10nm_init(pll, id);
		break;
	default:
		DRM_DEV_ERROR(dev, "%s: Wrong PLL type %d\n", __func__, type);
		return -ENXIO;
	}

	if (ret) {
		DRM_DEV_ERROR(dev, "%s: failed to init DP PLL\n", __func__);
		return ret;
	}

	pll->type = type;

	DRM_DEBUG_DP("DP:%d PLL registered", id);

	return ret;
}

struct msm_dp_pll *dp_pll_get(struct dp_pll_in *pll_in)
{
	struct msm_dp_pll *dp_pll;
	struct dp_parser *parser = pll_in->parser;
	struct dp_io_pll *pll_io;
	int ret;

	dp_pll = devm_kzalloc(&pll_in->pdev->dev, sizeof(*dp_pll), GFP_KERNEL);
	if (!dp_pll)
		return ERR_PTR(-ENOMEM);

	pll_io = &dp_pll->pll_io;
	dp_pll->pdev = pll_in->pdev;

	dp_pll_get_phy_io(parser);

	pll_io->pll_base = parser->io.phy_reg.base + DP_PHY_PLL_OFFSET;
	pll_io->phy_base = parser->io.phy_reg.base + DP_PHY_REG_OFFSET;
	pll_io->ln_tx0_base = parser->io.phy_reg.base + DP_PHY_LN_TX0_OFFSET;
	pll_io->ln_tx1_base = parser->io.phy_reg.base + DP_PHY_LN_TX1_OFFSET;

	ret = msm_dp_pll_init(dp_pll, MSM_DP_PLL_10NM, 0);
	if (ret) {
		kfree(dp_pll);
		return ERR_PTR(ret);
	}

	return dp_pll;
}

void dp_pll_put(struct msm_dp_pll *dp_pll)
{
	if (dp_pll->type == MSM_DP_PLL_10NM)
		msm_dp_pll_10nm_deinit(dp_pll);
}
