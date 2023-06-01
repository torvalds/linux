/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * StarFive JH7110 Clock Generator Driver
 *
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 * Author: Xingyu Wu <xingyu.wu@starfivetech.com>
 */

#ifndef _CLK_STARFIVE_JH7110_H_
#define _CLK_STARFIVE_JH7110_H_

#include <linux/bits.h>
#include <linux/clk-provider.h>
#include <linux/debugfs.h>
#include <dt-bindings/clock/starfive-jh7110-clkgen.h>
#include <dt-bindings/clock/starfive-jh7110-vout.h>
#include <dt-bindings/clock/starfive-jh7110-isp.h>
#include "clk-starfive-jh7110-pll.h"

/* register flags */
#define JH7110_CLK_SYS_FLAG	1
#define JH7110_CLK_STG_FLAG	2
#define JH7110_CLK_AON_FLAG	3
#define JH7110_CLK_VOUT_FLAG	4
#define JH7110_CLK_ISP_FLAG	5

/* register fields */
#define JH7110_CLK_ENABLE	BIT(31)
#define JH7110_CLK_INVERT	BIT(30)
#define JH7110_CLK_MUX_MASK	GENMASK(29, 24)
#define JH7110_CLK_MUX_SHIFT	24
#define JH7110_CLK_DIV_MASK	GENMASK(23, 0)

/* clkgen PLL CLOCK offset */
#define PLL_OF(x)	(x - JH7110_CLK_REG_END)
/* vout PLL CLOCK offset */
#define PLL_OFV(x)	(x - JH7110_CLK_VOUT_REG_END)
/* isp PLL CLOCK offset */
#define PLL_OFI(x)	(x - JH7110_CLK_ISP_REG_END)

#define GATE_FLAG_NORMAL  0

enum {
	PARENT_NUMS_1 = 1,
	PARENT_NUMS_2,
	PARENT_NUMS_3,
	PARENT_NUMS_4,
};

/* clock data */
struct jh7110_clk_data {
	const char *name;
	unsigned long flags;
	u32 max;
	u16 parents[4];
};

struct jh7110_clk {
	struct clk_hw hw;
	unsigned int idx;
	unsigned int max_div;
	unsigned int reg_flags;
	u32 saved_reg_value;
};

struct jh7110_clk_priv {
	/* protect clk enable and set rate/parent from happening at the same time */
	spinlock_t rmw_lock;
	struct device *dev;
	void __iomem *sys_base;
	void __iomem *stg_base;
	void __iomem *aon_base;
	void __iomem *vout_base;
	void __iomem *isp_base;
	struct clk_hw *pll[PLL_OF(JH7110_CLK_END)];
#ifdef CONFIG_CLK_STARFIVE_JH7110_PLL
	struct jh7110_clk_pll_data pll_priv[PLL_INDEX_MAX];
#endif
	struct jh7110_clk reg[];
};

#define JH7110_GATE(_idx, _name, _flags, _parent)\
[_idx] = {\
	.name = _name,\
	.flags = CLK_SET_RATE_PARENT | (_flags),\
	.max = JH7110_CLK_ENABLE,\
	.parents = { [0] = _parent },\
}

#define JH7110__DIV(_idx, _name, _max, _parent)\
[_idx] = {\
	.name = _name,\
	.flags = 0,\
	.max = _max,\
	.parents = { [0] = _parent },\
}

#define JH7110_GDIV(_idx, _name, _flags, _max, _parent)\
[_idx] = {\
	.name = _name,\
	.flags = _flags,\
	.max = JH7110_CLK_ENABLE | (_max),\
	.parents = { [0] = _parent },\
}

#define JH7110__MUX(_idx, _name, _nparents, ...)\
[_idx] = {\
	.name = _name,\
	.flags = 0,\
	.max = ((_nparents) - 1) << JH7110_CLK_MUX_SHIFT,\
	.parents = { __VA_ARGS__ },\
}

#define JH7110_GMUX(_idx, _name, _flags, _nparents, ...)\
[_idx] = {\
	.name = _name,\
	.flags = _flags,\
	.max = JH7110_CLK_ENABLE |	\
		(((_nparents) - 1) << JH7110_CLK_MUX_SHIFT),\
	.parents = { __VA_ARGS__ },\
}

#define JH7110_MDIV(_idx, _name, _max, _nparents, ...)\
[_idx] = {\
	.name = _name,\
	.flags = 0,\
	.max = (((_nparents) - 1) << JH7110_CLK_MUX_SHIFT) | (_max),\
	.parents = { __VA_ARGS__ },\
}

#define JH7110__GMD(_idx, _name, _flags, _max, _nparents, ...)\
[_idx] = {\
	.name = _name,\
	.flags = _flags,\
	.max = JH7110_CLK_ENABLE |	\
		(((_nparents) - 1) << JH7110_CLK_MUX_SHIFT) | (_max),\
	.parents = { __VA_ARGS__ },\
}

#define JH7110__INV(_idx, _name, _parent)\
[_idx] = {\
	.name = _name,\
	.flags = CLK_SET_RATE_PARENT,\
	.max = JH7110_CLK_INVERT,\
	.parents = { [0] = _parent },\
}

void __iomem *jh7110_clk_reg_addr_get(struct jh7110_clk *clk);
const struct clk_ops *starfive_jh7110_clk_ops(u32 max);

int __init clk_starfive_jh7110_sys_init(struct platform_device *pdev,
					struct jh7110_clk_priv *priv);
int __init clk_starfive_jh7110_stg_init(struct platform_device *pdev,
					struct jh7110_clk_priv *priv);
int __init clk_starfive_jh7110_aon_init(struct platform_device *pdev,
					struct jh7110_clk_priv *priv);

#endif
