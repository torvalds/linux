/*
 * Copyright (c) 2015 Linaro Ltd.
 * Author: Pi-Cheng Chen <pi-cheng.chen@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __DRV_CLK_CPUMUX_H
#define __DRV_CLK_CPUMUX_H

struct mtk_clk_cpumux {
	struct clk_hw	hw;
	struct regmap	*regmap;
	u32		reg;
	u32		mask;
	u8		shift;
};

int mtk_clk_register_cpumuxes(struct device_node *node,
			      const struct mtk_composite *clks, int num,
			      struct clk_onecell_data *clk_data);

#endif /* __DRV_CLK_CPUMUX_H */
