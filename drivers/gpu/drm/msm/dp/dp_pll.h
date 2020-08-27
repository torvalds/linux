/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
 */

#ifndef __DP_PLL_H
#define __DP_PLL_H

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "dpu_io_util.h"
#include "msm_drv.h"
#include "dp_parser.h"

#define PLL_REG_W(base, offset, data)	\
				writel((data), (base) + (offset))
#define PLL_REG_R(base, offset)	readl((base) + (offset))

enum msm_dp_pll_type {
	MSM_DP_PLL_10NM,
	MSM_DP_PLL_MAX
};

struct dp_pll_in {
	struct platform_device *pdev;
	struct dp_parser *parser;
};

struct dp_io_pll {
	void __iomem *pll_base;
	void __iomem *phy_base;
	void __iomem *ln_tx0_base;
	void __iomem *ln_tx1_base;
};

struct msm_dp_pll {
	enum msm_dp_pll_type type;
	bool		pll_on;

	struct dp_io_pll pll_io;

	/* clock-provider: */
	struct clk_hw_onecell_data *hw_data;

	struct platform_device *pdev;
	void *priv;

	/* Pll specific resources like GPIO, power supply, clocks, etc*/
	struct dss_module_power mp;
	int (*get_provider)(struct msm_dp_pll *pll,
			struct clk **link_clk_provider,
			struct clk **pixel_clk_provider);
};

struct msm_dp_pll *dp_pll_get(struct dp_pll_in *pll_in);

void dp_pll_put(struct msm_dp_pll *dp_pll);

#endif /* __DP_PLL_H */
