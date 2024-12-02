/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CLK_STARFIVE_JH7100_H
#define __CLK_STARFIVE_JH7100_H

#include <linux/bits.h>
#include <linux/clk-provider.h>

/* register fields */
#define JH7100_CLK_ENABLE	BIT(31)
#define JH7100_CLK_INVERT	BIT(30)
#define JH7100_CLK_MUX_MASK	GENMASK(27, 24)
#define JH7100_CLK_MUX_SHIFT	24
#define JH7100_CLK_DIV_MASK	GENMASK(23, 0)
#define JH7100_CLK_FRAC_MASK	GENMASK(15, 8)
#define JH7100_CLK_FRAC_SHIFT	8
#define JH7100_CLK_INT_MASK	GENMASK(7, 0)

/* fractional divider min/max */
#define JH7100_CLK_FRAC_MIN	100UL
#define JH7100_CLK_FRAC_MAX	25599UL

/* clock data */
struct jh7100_clk_data {
	const char *name;
	unsigned long flags;
	u32 max;
	u8 parents[4];
};

#define JH7100_GATE(_idx, _name, _flags, _parent) [_idx] = {			\
	.name = _name,								\
	.flags = CLK_SET_RATE_PARENT | (_flags),				\
	.max = JH7100_CLK_ENABLE,						\
	.parents = { [0] = _parent },						\
}

#define JH7100__DIV(_idx, _name, _max, _parent) [_idx] = {			\
	.name = _name,								\
	.flags = 0,								\
	.max = _max,								\
	.parents = { [0] = _parent },						\
}

#define JH7100_GDIV(_idx, _name, _flags, _max, _parent) [_idx] = {		\
	.name = _name,								\
	.flags = _flags,							\
	.max = JH7100_CLK_ENABLE | (_max),					\
	.parents = { [0] = _parent },						\
}

#define JH7100_FDIV(_idx, _name, _parent) [_idx] = {				\
	.name = _name,								\
	.flags = 0,								\
	.max = JH7100_CLK_FRAC_MAX,						\
	.parents = { [0] = _parent },						\
}

#define JH7100__MUX(_idx, _name, _nparents, ...) [_idx] = {			\
	.name = _name,								\
	.flags = 0,								\
	.max = ((_nparents) - 1) << JH7100_CLK_MUX_SHIFT,			\
	.parents = { __VA_ARGS__ },						\
}

#define JH7100_GMUX(_idx, _name, _flags, _nparents, ...) [_idx] = {		\
	.name = _name,								\
	.flags = _flags,							\
	.max = JH7100_CLK_ENABLE |						\
		(((_nparents) - 1) << JH7100_CLK_MUX_SHIFT),			\
	.parents = { __VA_ARGS__ },						\
}

#define JH7100_MDIV(_idx, _name, _max, _nparents, ...) [_idx] = {		\
	.name = _name,								\
	.flags = 0,								\
	.max = (((_nparents) - 1) << JH7100_CLK_MUX_SHIFT) | (_max),		\
	.parents = { __VA_ARGS__ },						\
}

#define JH7100__GMD(_idx, _name, _flags, _max, _nparents, ...) [_idx] = {	\
	.name = _name,								\
	.flags = _flags,							\
	.max = JH7100_CLK_ENABLE |						\
		(((_nparents) - 1) << JH7100_CLK_MUX_SHIFT) | (_max),		\
	.parents = { __VA_ARGS__ },						\
}

#define JH7100__INV(_idx, _name, _parent) [_idx] = {				\
	.name = _name,								\
	.flags = CLK_SET_RATE_PARENT,						\
	.max = JH7100_CLK_INVERT,						\
	.parents = { [0] = _parent },						\
}

struct jh7100_clk {
	struct clk_hw hw;
	unsigned int idx;
	unsigned int max_div;
};

struct jh7100_clk_priv {
	/* protect clk enable and set rate/parent from happening at the same time */
	spinlock_t rmw_lock;
	struct device *dev;
	void __iomem *base;
	struct clk_hw *pll[3];
	struct jh7100_clk reg[];
};

const struct clk_ops *starfive_jh7100_clk_ops(u32 max);

#endif
