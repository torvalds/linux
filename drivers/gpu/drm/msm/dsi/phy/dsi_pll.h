/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
 */

#ifndef __DSI_PLL_H__
#define __DSI_PLL_H__

#include <linux/delay.h>

#include "dsi.h"

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

#endif /* __DSI_PLL_H__ */

