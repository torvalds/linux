/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
 */

#ifndef __DSI_PLL_H__
#define __DSI_PLL_H__

#include <linux/clk-provider.h>
#include <linux/delay.h>

#include "dsi.h"

#define NUM_DSI_CLOCKS_MAX	6

struct msm_dsi_pll {
	struct clk_hw	clk_hw;
	bool		pll_on;
	bool		state_saved;

	unsigned long	min_rate;
	unsigned long	max_rate;

	const struct msm_dsi_phy_cfg *cfg;
};

#define hw_clk_to_pll(x) container_of(x, struct msm_dsi_pll, clk_hw)

static inline void pll_write(void __iomem *reg, u32 data)
{
	msm_writel(data, reg);
}

static inline u32 pll_read(const void __iomem *reg)
{
	return msm_readl(reg);
}

static inline void pll_write_udelay(void __iomem *reg, u32 data, u32 delay_us)
{
	pll_write(reg, data);
	udelay(delay_us);
}

static inline void pll_write_ndelay(void __iomem *reg, u32 data, u32 delay_ns)
{
	pll_write((reg), data);
	ndelay(delay_ns);
}

/*
 * DSI PLL Helper functions
 */

/* clock callbacks */
long msm_dsi_pll_helper_clk_round_rate(struct clk_hw *hw,
		unsigned long rate, unsigned long *parent_rate);
int msm_dsi_pll_helper_clk_prepare(struct clk_hw *hw);
void msm_dsi_pll_helper_clk_unprepare(struct clk_hw *hw);
/* misc */
void msm_dsi_pll_helper_unregister_clks(struct platform_device *pdev,
					struct clk **clks, u32 num_clks);

#endif /* __DSI_PLL_H__ */

