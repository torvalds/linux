// SPDX-License-Identifier: GPL-2.0
/*
 * Sophgo SG2044 clock controller driver
 *
 * Copyright (C) 2025 Inochi Amaoto <inochiama@gmail.com>
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/math64.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>

#include <dt-bindings/clock/sophgo,sg2044-clk.h>

#define DIV_ASSERT		BIT(0)
#define DIV_FACTOR_REG_SOURCE	BIT(3)
#define DIV_BRANCH_EN		BIT(4)

#define DIV_ASSERT_TIME		2

struct sg2044_div_internal {
	u32	offset;
	u32	initval;
	u8	shift;
	u8	width;
	u16	flags;
};

struct sg2044_mux_internal {
	const u32	*table;
	u32		offset;
	u16		shift;
	u16		flags;
};

struct sg2044_gate_internal {
	u32	offset;
	u16	shift;
	u16	flags;
};

struct sg2044_clk_common {
	struct clk_hw	hw;
	void __iomem	*base;
	spinlock_t	*lock;
	unsigned int	id;
};

struct sg2044_div {
	struct sg2044_clk_common	common;
	struct sg2044_div_internal	div;
};

struct sg2044_mux {
	struct sg2044_clk_common	common;
	struct sg2044_mux_internal	mux;
	struct notifier_block		nb;
	u8				saved_parent;
};

struct sg2044_gate {
	struct sg2044_clk_common	common;
	struct sg2044_gate_internal	gate;
};

struct sg2044_clk_ctrl {
	spinlock_t			lock;
	struct clk_hw_onecell_data	data;
};

struct sg2044_clk_desc_data {
	struct sg2044_clk_common	* const *pll;
	struct sg2044_clk_common	* const *div;
	struct sg2044_clk_common	* const *mux;
	struct sg2044_clk_common	* const *gate;
	u16				num_pll;
	u16				num_div;
	u16				num_mux;
	u16				num_gate;
};

#define hw_to_sg2044_clk_common(_hw)					\
	container_of((_hw), struct sg2044_clk_common, hw)

static inline struct sg2044_div *hw_to_sg2044_div(struct clk_hw *hw)
{
	return container_of(hw_to_sg2044_clk_common(hw),
			    struct sg2044_div, common);
}

static u32 sg2044_div_get_reg_div(u32 reg, struct sg2044_div_internal *div)
{
	if ((reg & DIV_FACTOR_REG_SOURCE))
		return (reg >> div->shift) & clk_div_mask(div->width);

	return div->initval == 0 ? 1 : div->initval;
}

static unsigned long _sg2044_div_recalc_rate(struct sg2044_clk_common *common,
					     struct sg2044_div_internal *div,
					     unsigned long parent_rate)
{
	u32 reg = readl(common->base + div->offset);
	u32 val = sg2044_div_get_reg_div(reg, div);

	return divider_recalc_rate(&common->hw, parent_rate, val, NULL,
				   div->flags, div->width);
}

static unsigned long sg2044_div_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct sg2044_div *div = hw_to_sg2044_div(hw);

	return _sg2044_div_recalc_rate(&div->common, &div->div,
				       parent_rate);
}

static int _sg2044_div_determine_rate(struct sg2044_clk_common *common,
				      struct sg2044_div_internal *div,
				      struct clk_rate_request *req)
{
	if (div->flags & CLK_DIVIDER_READ_ONLY) {
		u32 reg = readl(common->base + div->offset);
		u32 val = sg2044_div_get_reg_div(reg, div);

		return divider_ro_determine_rate(&common->hw, req, NULL,
						 div->width, div->flags,
						 val);
	}

	return divider_determine_rate(&common->hw, req, NULL,
				      div->width, div->flags);
}

static int sg2044_div_determine_rate(struct clk_hw *hw,
				     struct clk_rate_request *req)
{
	struct sg2044_div *div = hw_to_sg2044_div(hw);

	return _sg2044_div_determine_rate(&div->common, &div->div, req);
}

static void sg2044_div_set_reg_div(struct sg2044_clk_common *common,
				   struct sg2044_div_internal *div,
				   u32 value)
{
	void __iomem *addr = common->base + div->offset;
	u32 reg;

	reg = readl(addr);

	/* assert */
	reg &= ~DIV_ASSERT;
	writel(reg, addr);

	/* set value */
	reg = readl(addr);
	reg &= ~(clk_div_mask(div->width) << div->shift);
	reg |= (value << div->shift) | DIV_FACTOR_REG_SOURCE;
	writel(reg, addr);

	/* de-assert */
	reg |= DIV_ASSERT;
	writel(reg, addr);
}

static int sg2044_div_set_rate(struct clk_hw *hw,
			       unsigned long rate, unsigned long parent_rate)
{
	struct sg2044_div *div = hw_to_sg2044_div(hw);
	u32 value;

	value = divider_get_val(rate, parent_rate, NULL,
				div->div.width, div->div.flags);

	guard(spinlock_irqsave)(div->common.lock);

	sg2044_div_set_reg_div(&div->common, &div->div, value);

	return 0;
}

static int sg2044_div_enable(struct clk_hw *hw)
{
	struct sg2044_div *div = hw_to_sg2044_div(hw);
	void __iomem *addr = div->common.base + div->div.offset;
	u32 value;

	guard(spinlock_irqsave)(div->common.lock);

	value = readl(addr);
	value |= DIV_BRANCH_EN;
	writel(value, addr);

	return 0;
}

static void sg2044_div_disable(struct clk_hw *hw)
{
	struct sg2044_div *div = hw_to_sg2044_div(hw);
	void __iomem *addr = div->common.base + div->div.offset;
	u32 value;

	guard(spinlock_irqsave)(div->common.lock);

	value = readl(addr);
	value &= ~DIV_BRANCH_EN;
	writel(value, addr);
}

static int sg2044_div_is_enabled(struct clk_hw *hw)
{
	struct sg2044_div *div = hw_to_sg2044_div(hw);

	return readl(div->common.base + div->div.offset) & DIV_BRANCH_EN;
}

static const struct clk_ops sg2044_gateable_div_ops = {
	.enable = sg2044_div_enable,
	.disable = sg2044_div_disable,
	.is_enabled = sg2044_div_is_enabled,
	.recalc_rate = sg2044_div_recalc_rate,
	.determine_rate = sg2044_div_determine_rate,
	.set_rate = sg2044_div_set_rate,
};

static const struct clk_ops sg2044_div_ops = {
	.recalc_rate = sg2044_div_recalc_rate,
	.determine_rate = sg2044_div_determine_rate,
	.set_rate = sg2044_div_set_rate,
};

static const struct clk_ops sg2044_div_ro_ops = {
	.recalc_rate = sg2044_div_recalc_rate,
	.determine_rate = sg2044_div_determine_rate,
};

static inline struct sg2044_mux *hw_to_sg2044_mux(struct clk_hw *hw)
{
	return container_of(hw_to_sg2044_clk_common(hw),
			    struct sg2044_mux, common);
}

static inline struct sg2044_mux *nb_to_sg2044_mux(struct notifier_block *nb)
{
	return container_of(nb, struct sg2044_mux, nb);
}

static const u32 sg2044_mux_table[] = {0, 1};

static int sg2044_mux_notifier_cb(struct notifier_block *nb,
				  unsigned long event,
				  void *data)
{
	struct sg2044_mux *mux = nb_to_sg2044_mux(nb);
	const struct clk_ops *ops = &clk_mux_ops;
	struct clk_notifier_data *ndata = data;
	struct clk_hw *hw = __clk_get_hw(ndata->clk);
	int ret = 0;

	if (event == PRE_RATE_CHANGE) {
		mux->saved_parent = ops->get_parent(hw);
		if (mux->saved_parent)
			ret = ops->set_parent(hw, 0);
	} else if (event == POST_RATE_CHANGE) {
		ret = ops->set_parent(hw, mux->saved_parent);
	}

	return notifier_from_errno(ret);
}

static inline struct sg2044_gate *hw_to_sg2044_gate(struct clk_hw *hw)
{
	return container_of(hw_to_sg2044_clk_common(hw),
			    struct sg2044_gate, common);
}

#define SG2044_CLK_COMMON_PDATA(_id, _name, _parents, _op, _flags)	\
	{								\
		.hw.init = CLK_HW_INIT_PARENTS_DATA(_name, _parents,	\
						    _op, (_flags)),	\
		.id = (_id),						\
	}

#define SG2044_CLK_COMMON_PHWS(_id, _name, _parents, _op, _flags)	\
	{								\
		.hw.init = CLK_HW_INIT_PARENTS_HW(_name, _parents,	\
						  _op, (_flags)),	\
		.id = (_id),						\
	}

#define DEFINE_SG2044_GATEABLE_DIV(_id, _name, _parent, _flags,		\
				   _div_offset, _div_shift, _div_width,	\
				   _div_flags, _div_initval)		\
	struct sg2044_div _name = {					\
		.common	= SG2044_CLK_COMMON_PDATA(_id, #_name, _parent,	\
						  &sg2044_gateable_div_ops,\
						  (_flags)),		\
		.div	= {						\
			.offset		= (_div_offset),		\
			.initval	= (_div_initval),		\
			.shift		= (_div_shift),			\
			.width		= (_div_width),			\
			.flags		= (_div_flags),			\
		},							\
	}

#define DEFINE_SG2044_DIV(_id, _name, _parent, _flags,			\
			  _div_offset, _div_shift, _div_width,		\
			  _div_flags, _div_initval)			\
	struct sg2044_div _name = {					\
		.common	= SG2044_CLK_COMMON_PHWS(_id, #_name, _parent,	\
						 &sg2044_div_ops,	\
						 (_flags)),		\
		.div	= {						\
			.offset		= (_div_offset),		\
			.initval	= (_div_initval),		\
			.shift		= (_div_shift),			\
			.width		= (_div_width),			\
			.flags		= (_div_flags),			\
		},							\
	}

#define DEFINE_SG2044_DIV_PDATA(_id, _name, _parent, _flags,		\
				_div_offset, _div_shift, _div_width,	\
				_div_flags, _div_initval)		\
	struct sg2044_div _name = {					\
		.common	= SG2044_CLK_COMMON_PDATA(_id, #_name, _parent,	\
						  &sg2044_div_ops,	\
						  (_flags)),		\
		.div	= {						\
			.offset		= (_div_offset),		\
			.initval	= (_div_initval),		\
			.shift		= (_div_shift),			\
			.width		= (_div_width),			\
			.flags		= (_div_flags),			\
		},							\
	}

#define DEFINE_SG2044_DIV_RO(_id, _name, _parent, _flags,		\
			     _div_offset, _div_shift, _div_width,	\
			     _div_flags, _div_initval)			\
	struct sg2044_div _name = {					\
		.common	= SG2044_CLK_COMMON_PDATA(_id, #_name, _parent,	\
						  &sg2044_div_ro_ops,	\
						  (_flags)),		\
		.div	= {						\
			.offset		= (_div_offset),		\
			.initval	= (_div_initval),		\
			.shift		= (_div_shift),			\
			.width		= (_div_width),			\
			.flags		= (_div_flags) | CLK_DIVIDER_READ_ONLY,\
		},							\
	}

#define DEFINE_SG2044_MUX(_id, _name, _parent, _flags,			\
			  _mux_offset, _mux_shift,			\
			  _mux_table, _mux_flags)			\
	struct sg2044_mux _name = {					\
		.common	= SG2044_CLK_COMMON_PDATA(_id, #_name, _parent,	\
						  &clk_mux_ops, (_flags)),\
		.mux	= {						\
			.table		= (_mux_table),			\
			.offset		= (_mux_offset),		\
			.shift		= (_mux_shift),			\
			.flags		= (_mux_flags),			\
		},							\
	}

#define DEFINE_SG2044_GATE(_id, _name, _parent, _flags,			\
			   _gate_offset, _gate_shift, _gate_flags)	\
	struct sg2044_gate _name = {					\
		.common	= SG2044_CLK_COMMON_PHWS(_id, #_name, _parent,	\
						 &clk_gate_ops, (_flags)),\
		.gate	= {						\
			.offset		= (_gate_offset),		\
			.shift		= (_gate_shift),		\
			.flags		= (_gate_flags),		\
		},							\
	}

static const struct clk_parent_data clk_fpll0_parent[] = {
	{ .fw_name = "fpll0" },
};

static const struct clk_parent_data clk_fpll1_parent[] = {
	{ .fw_name = "fpll1" },
};

static const struct clk_parent_data clk_fpll2_parent[] = {
	{ .fw_name = "fpll2" },
};

static const struct clk_parent_data clk_dpll0_parent[] = {
	{ .fw_name = "dpll0" },
};

static const struct clk_parent_data clk_dpll1_parent[] = {
	{ .fw_name = "dpll1" },
};

static const struct clk_parent_data clk_dpll2_parent[] = {
	{ .fw_name = "dpll2" },
};

static const struct clk_parent_data clk_dpll3_parent[] = {
	{ .fw_name = "dpll3" },
};

static const struct clk_parent_data clk_dpll4_parent[] = {
	{ .fw_name = "dpll4" },
};

static const struct clk_parent_data clk_dpll5_parent[] = {
	{ .fw_name = "dpll5" },
};

static const struct clk_parent_data clk_dpll6_parent[] = {
	{ .fw_name = "dpll6" },
};

static const struct clk_parent_data clk_dpll7_parent[] = {
	{ .fw_name = "dpll7" },
};

static const struct clk_parent_data clk_mpll0_parent[] = {
	{ .fw_name = "mpll0" },
};

static const struct clk_parent_data clk_mpll1_parent[] = {
	{ .fw_name = "mpll1" },
};

static const struct clk_parent_data clk_mpll2_parent[] = {
	{ .fw_name = "mpll2" },
};

static const struct clk_parent_data clk_mpll3_parent[] = {
	{ .fw_name = "mpll3" },
};

static const struct clk_parent_data clk_mpll4_parent[] = {
	{ .fw_name = "mpll4" },
};

static const struct clk_parent_data clk_mpll5_parent[] = {
	{ .fw_name = "mpll5" },
};

static DEFINE_SG2044_GATEABLE_DIV(CLK_DIV_AP_SYS_FIXED, clk_div_ap_sys_fixed,
				  clk_fpll0_parent, 0,
				  0x044, 16, 8,
				  CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO |
					CLK_IS_CRITICAL,
				  1);

static DEFINE_SG2044_GATEABLE_DIV(CLK_DIV_AP_SYS_MAIN, clk_div_ap_sys_main,
				  clk_mpll0_parent, 0,
				  0x040, 16, 8,
				  CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO |
					CLK_IS_CRITICAL,
				  1);

static DEFINE_SG2044_GATEABLE_DIV(CLK_DIV_RP_SYS_FIXED, clk_div_rp_sys_fixed,
				  clk_fpll0_parent, 0,
				  0x050, 16, 8,
				  CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO |
					CLK_IS_CRITICAL,
				  1);

static DEFINE_SG2044_GATEABLE_DIV(CLK_DIV_RP_SYS_MAIN, clk_div_rp_sys_main,
				  clk_mpll1_parent, 0,
				  0x04c, 16, 8,
				  CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO |
					CLK_IS_CRITICAL,
				  1);

static DEFINE_SG2044_GATEABLE_DIV(CLK_DIV_TPU_SYS_FIXED, clk_div_tpu_sys_fixed,
				  clk_fpll0_parent, 0,
				  0x058, 16, 8,
				  CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO |
					CLK_IS_CRITICAL,
				  2);

static DEFINE_SG2044_GATEABLE_DIV(CLK_DIV_TPU_SYS_MAIN, clk_div_tpu_sys_main,
				  clk_mpll2_parent, 0,
				  0x054, 16, 8,
				  CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO |
					CLK_IS_CRITICAL,
				  1);

static DEFINE_SG2044_GATEABLE_DIV(CLK_DIV_NOC_SYS_FIXED, clk_div_noc_sys_fixed,
				  clk_fpll0_parent, 0,
				  0x070, 16, 8,
				  CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO |
					CLK_IS_CRITICAL,
				  1);

static DEFINE_SG2044_GATEABLE_DIV(CLK_DIV_NOC_SYS_MAIN, clk_div_noc_sys_main,
				  clk_mpll3_parent, 0,
				  0x06c, 16, 8,
				  CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO |
					CLK_IS_CRITICAL,
				  1);

static DEFINE_SG2044_GATEABLE_DIV(CLK_DIV_VC_SRC0_FIXED, clk_div_vc_src0_fixed,
				  clk_fpll0_parent, 0,
				  0x078, 16, 8,
				  CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO |
					CLK_IS_CRITICAL,
				  2);

static DEFINE_SG2044_GATEABLE_DIV(CLK_DIV_VC_SRC0_MAIN, clk_div_vc_src0_main,
				  clk_mpll4_parent, 0,
				  0x074, 16, 8,
				  CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO |
					CLK_IS_CRITICAL,
				  1);

static DEFINE_SG2044_GATEABLE_DIV(CLK_DIV_VC_SRC1_FIXED, clk_div_vc_src1_fixed,
				  clk_fpll0_parent, 0,
				  0x080, 16, 8,
				  CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO |
					CLK_IS_CRITICAL,
				  3);

static DEFINE_SG2044_GATEABLE_DIV(CLK_DIV_VC_SRC1_MAIN, clk_div_vc_src1_main,
				  clk_mpll5_parent, 0,
				  0x07c, 16, 8,
				  CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO |
					CLK_IS_CRITICAL,
				  1);

static DEFINE_SG2044_GATEABLE_DIV(CLK_DIV_CXP_MAC_FIXED, clk_div_cxp_mac_fixed,
				  clk_fpll0_parent, 0,
				  0x088, 16, 8,
				  CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO |
					CLK_IS_CRITICAL,
				  2);

static DEFINE_SG2044_GATEABLE_DIV(CLK_DIV_CXP_MAC_MAIN, clk_div_cxp_mac_main,
				  clk_fpll1_parent, 0,
				  0x084, 16, 8,
				  CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO |
					CLK_IS_CRITICAL,
				  1);

static DEFINE_SG2044_DIV_RO(CLK_DIV_DDR0_FIXED, clk_div_ddr0_fixed,
			    clk_fpll0_parent, 0,
			    0x124, 16, 8,
			    CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			    2);

static DEFINE_SG2044_DIV_RO(CLK_DIV_DDR0_MAIN, clk_div_ddr0_main,
			    clk_dpll0_parent, 0,
			    0x120, 16, 8,
			    CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			    1);

static DEFINE_SG2044_DIV_RO(CLK_DIV_DDR1_FIXED, clk_div_ddr1_fixed,
			    clk_fpll0_parent, 0,
			    0x12c, 16, 8,
			    CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			    2);

static DEFINE_SG2044_DIV_RO(CLK_DIV_DDR1_MAIN, clk_div_ddr1_main,
			    clk_dpll1_parent, 0,
			    0x128, 16, 8,
			    CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			    1);

static DEFINE_SG2044_DIV_RO(CLK_DIV_DDR2_FIXED, clk_div_ddr2_fixed,
			    clk_fpll0_parent, 0,
			    0x134, 16, 8,
			    CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			    2);

static DEFINE_SG2044_DIV_RO(CLK_DIV_DDR2_MAIN, clk_div_ddr2_main,
			    clk_dpll2_parent, 0,
			    0x130, 16, 8,
			    CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			    1);

static DEFINE_SG2044_DIV_RO(CLK_DIV_DDR3_FIXED, clk_div_ddr3_fixed,
			    clk_fpll0_parent, 0,
			    0x13c, 16, 8,
			    CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			    2);

static DEFINE_SG2044_DIV_RO(CLK_DIV_DDR3_MAIN, clk_div_ddr3_main,
			    clk_dpll3_parent, 0,
			    0x138, 16, 8,
			    CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			    1);

static DEFINE_SG2044_DIV_RO(CLK_DIV_DDR4_FIXED, clk_div_ddr4_fixed,
			    clk_fpll0_parent, 0,
			    0x144, 16, 8,
			    CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			    2);

static DEFINE_SG2044_DIV_RO(CLK_DIV_DDR4_MAIN, clk_div_ddr4_main,
			    clk_dpll4_parent, 0,
			    0x140, 16, 8,
			    CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			    1);

static DEFINE_SG2044_DIV_RO(CLK_DIV_DDR5_FIXED, clk_div_ddr5_fixed,
			    clk_fpll0_parent, 0,
			    0x14c, 16, 8,
			    CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			    2);

static DEFINE_SG2044_DIV_RO(CLK_DIV_DDR5_MAIN, clk_div_ddr5_main,
			    clk_dpll5_parent, 0,
			    0x148, 16, 8,
			    CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			    1);

static DEFINE_SG2044_DIV_RO(CLK_DIV_DDR6_FIXED, clk_div_ddr6_fixed,
			    clk_fpll0_parent, 0,
			    0x154, 16, 8,
			    CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			    2);

static DEFINE_SG2044_DIV_RO(CLK_DIV_DDR6_MAIN, clk_div_ddr6_main,
			    clk_dpll6_parent, 0,
			    0x150, 16, 8,
			    CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			    1);

static DEFINE_SG2044_DIV_RO(CLK_DIV_DDR7_FIXED, clk_div_ddr7_fixed,
			    clk_fpll0_parent, 0,
			    0x15c, 16, 8,
			    CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			    2);

static DEFINE_SG2044_DIV_RO(CLK_DIV_DDR7_MAIN, clk_div_ddr7_main,
			    clk_dpll7_parent, 0,
			    0x158, 16, 8,
			    CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			    1);

static DEFINE_SG2044_DIV_PDATA(CLK_DIV_TOP_50M, clk_div_top_50m,
			       clk_fpll0_parent, 0,
			       0x048, 16, 8,
			       CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			       40);

static const struct clk_hw *clk_div_top_50m_parent[] = {
	&clk_div_top_50m.common.hw,
};

static DEFINE_SG2044_DIV_RO(CLK_DIV_TOP_AXI0, clk_div_top_axi0,
			    clk_fpll0_parent, 0,
			    0x118, 16, 8,
			    CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			    20);

static const struct clk_hw *clk_div_top_axi0_parent[] = {
	&clk_div_top_axi0.common.hw,
};

static DEFINE_SG2044_DIV_PDATA(CLK_DIV_TOP_AXI_HSPERI, clk_div_top_axi_hsperi,
			       clk_fpll0_parent, 0,
			       0x11c, 16, 8,
			       CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			       8);

static const struct clk_hw *clk_div_top_axi_hsperi_parent[] = {
	&clk_div_top_axi_hsperi.common.hw,
};

static DEFINE_SG2044_DIV(CLK_DIV_TIMER0, clk_div_timer0,
			 clk_div_top_50m_parent, 0,
			 0x0d0, 16, 16,
			 CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			 1);

static DEFINE_SG2044_DIV(CLK_DIV_TIMER1, clk_div_timer1,
			 clk_div_top_50m_parent, 0,
			 0x0d4, 16, 16,
			 CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			 1);

static DEFINE_SG2044_DIV(CLK_DIV_TIMER2, clk_div_timer2,
			 clk_div_top_50m_parent, 0,
			 0x0d8, 16, 16,
			 CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			 1);

static DEFINE_SG2044_DIV(CLK_DIV_TIMER3, clk_div_timer3,
			 clk_div_top_50m_parent, 0,
			 0x0dc, 16, 16,
			 CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			 1);

static DEFINE_SG2044_DIV(CLK_DIV_TIMER4, clk_div_timer4,
			 clk_div_top_50m_parent, 0,
			 0x0e0, 16, 16,
			 CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			 1);

static DEFINE_SG2044_DIV(CLK_DIV_TIMER5, clk_div_timer5,
			 clk_div_top_50m_parent, 0,
			 0x0e4, 16, 16,
			 CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			 1);

static DEFINE_SG2044_DIV(CLK_DIV_TIMER6, clk_div_timer6,
			 clk_div_top_50m_parent, 0,
			 0x0e8, 16, 16,
			 CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			 1);

static DEFINE_SG2044_DIV(CLK_DIV_TIMER7, clk_div_timer7,
			 clk_div_top_50m_parent, 0,
			 0x0ec, 16, 16,
			 CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			 1);

static DEFINE_SG2044_DIV_PDATA(CLK_DIV_CXP_TEST_PHY, clk_div_cxp_test_phy,
			       clk_fpll0_parent, 0,
			       0x064, 16, 8,
			       CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			       1);

static DEFINE_SG2044_DIV_PDATA(CLK_DIV_CXP_TEST_ETH_PHY, clk_div_cxp_test_eth_phy,
			       clk_fpll2_parent, 0,
			       0x068, 16, 8,
			       CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			       1);

static DEFINE_SG2044_DIV_PDATA(CLK_DIV_C2C0_TEST_PHY, clk_div_c2c0_test_phy,
			       clk_fpll0_parent, 0,
			       0x05c, 16, 8,
			       CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			       1);

static DEFINE_SG2044_DIV_PDATA(CLK_DIV_C2C1_TEST_PHY, clk_div_c2c1_test_phy,
			       clk_fpll0_parent, 0,
			       0x060, 16, 8,
			       CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			       1);

static DEFINE_SG2044_DIV_PDATA(CLK_DIV_PCIE_1G, clk_div_pcie_1g,
			       clk_fpll1_parent, 0,
			       0x160, 16, 8,
			       CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			       1);

static DEFINE_SG2044_DIV_PDATA(CLK_DIV_UART_500M, clk_div_uart_500m,
			       clk_fpll0_parent, 0,
			       0x0cc, 16, 8,
			       CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			       4);

static DEFINE_SG2044_DIV(CLK_DIV_GPIO_DB, clk_div_gpio_db,
			 clk_div_top_axi0_parent, 0,
			 0x0f8, 16, 16,
			 CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			 1000);

static DEFINE_SG2044_DIV_PDATA(CLK_DIV_SD, clk_div_sd,
			       clk_fpll0_parent, 0,
			       0x110, 16, 16,
			       CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			       5);

static DEFINE_SG2044_DIV(CLK_DIV_SD_100K, clk_div_sd_100k,
			 clk_div_top_axi0_parent, 0,
			 0x114, 16, 16,
			 CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			 1000);

static DEFINE_SG2044_DIV_PDATA(CLK_DIV_EMMC, clk_div_emmc,
			       clk_fpll0_parent, 0,
			       0x108, 16, 16,
			       CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			       5);

static DEFINE_SG2044_DIV(CLK_DIV_EMMC_100K, clk_div_emmc_100k,
			 clk_div_top_axi0_parent, 0,
			 0x10c, 16, 16,
			 CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			 1000);

static DEFINE_SG2044_DIV_PDATA(CLK_DIV_EFUSE, clk_div_efuse,
			       clk_fpll0_parent, 0,
			       0x0f4, 16, 8,
			       CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			       80);

static DEFINE_SG2044_DIV_PDATA(CLK_DIV_TX_ETH0, clk_div_tx_eth0,
			       clk_fpll0_parent, 0,
			       0x0fc, 16, 8,
			       CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			       16);

static DEFINE_SG2044_DIV_PDATA(CLK_DIV_PTP_REF_I_ETH0, clk_div_ptp_ref_i_eth0,
			       clk_fpll0_parent, 0,
			       0x100, 16, 8,
			       CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			       40);

static DEFINE_SG2044_DIV_PDATA(CLK_DIV_REF_ETH0, clk_div_ref_eth0,
			       clk_fpll0_parent, 0,
			       0x104, 16, 8,
			       CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			       80);

static DEFINE_SG2044_DIV_PDATA(CLK_DIV_PKA, clk_div_pka,
			       clk_fpll0_parent, 0,
			       0x0f0, 16, 8,
			       CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			       2);

static const struct clk_parent_data clk_mux_ddr0_parents[] = {
	{ .hw = &clk_div_ddr0_fixed.common.hw },
	{ .hw = &clk_div_ddr0_main.common.hw },
};

static DEFINE_SG2044_MUX(CLK_MUX_DDR0, clk_mux_ddr0,
			 clk_mux_ddr0_parents,
			 CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			 0x020, 7, sg2044_mux_table, CLK_MUX_READ_ONLY);

static const struct clk_parent_data clk_mux_ddr1_parents[] = {
	{ .hw = &clk_div_ddr1_fixed.common.hw },
	{ .hw = &clk_div_ddr1_main.common.hw },
};

static DEFINE_SG2044_MUX(CLK_MUX_DDR1, clk_mux_ddr1,
			 clk_mux_ddr1_parents,
			 CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			 0x020, 8, sg2044_mux_table, CLK_MUX_READ_ONLY);

static const struct clk_parent_data clk_mux_ddr2_parents[] = {
	{ .hw = &clk_div_ddr2_fixed.common.hw },
	{ .hw = &clk_div_ddr2_main.common.hw },
};

static DEFINE_SG2044_MUX(CLK_MUX_DDR2, clk_mux_ddr2,
			 clk_mux_ddr2_parents,
			 CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			 0x020, 9, sg2044_mux_table, CLK_MUX_READ_ONLY);

static const struct clk_parent_data clk_mux_ddr3_parents[] = {
	{ .hw = &clk_div_ddr3_fixed.common.hw },
	{ .hw = &clk_div_ddr3_main.common.hw },
};

static DEFINE_SG2044_MUX(CLK_MUX_DDR3, clk_mux_ddr3,
			 clk_mux_ddr3_parents,
			 CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			 0x020, 10, sg2044_mux_table, CLK_MUX_READ_ONLY);

static const struct clk_parent_data clk_mux_ddr4_parents[] = {
	{ .hw = &clk_div_ddr4_fixed.common.hw },
	{ .hw = &clk_div_ddr4_main.common.hw },
};

static DEFINE_SG2044_MUX(CLK_MUX_DDR4, clk_mux_ddr4,
			 clk_mux_ddr4_parents,
			 CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			 0x020, 11, sg2044_mux_table, CLK_MUX_READ_ONLY);

static const struct clk_parent_data clk_mux_ddr5_parents[] = {
	{ .hw = &clk_div_ddr5_fixed.common.hw },
	{ .hw = &clk_div_ddr5_main.common.hw },
};

static DEFINE_SG2044_MUX(CLK_MUX_DDR5, clk_mux_ddr5,
			 clk_mux_ddr5_parents,
			 CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			 0x020, 12, sg2044_mux_table, CLK_MUX_READ_ONLY);

static const struct clk_parent_data clk_mux_ddr6_parents[] = {
	{ .hw = &clk_div_ddr6_fixed.common.hw },
	{ .hw = &clk_div_ddr6_main.common.hw },
};

static DEFINE_SG2044_MUX(CLK_MUX_DDR6, clk_mux_ddr6,
			 clk_mux_ddr6_parents,
			 CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			 0x020, 13, sg2044_mux_table, CLK_MUX_READ_ONLY);

static const struct clk_parent_data clk_mux_ddr7_parents[] = {
	{ .hw = &clk_div_ddr7_fixed.common.hw },
	{ .hw = &clk_div_ddr7_main.common.hw },
};

static DEFINE_SG2044_MUX(CLK_MUX_DDR7, clk_mux_ddr7,
			 clk_mux_ddr7_parents,
			 CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			 0x020, 14, sg2044_mux_table, CLK_MUX_READ_ONLY);

static const struct clk_parent_data clk_mux_noc_sys_parents[] = {
	{ .hw = &clk_div_noc_sys_fixed.common.hw },
	{ .hw = &clk_div_noc_sys_main.common.hw },
};

static DEFINE_SG2044_MUX(CLK_MUX_NOC_SYS, clk_mux_noc_sys,
			 clk_mux_noc_sys_parents,
			 CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			 0x020, 3, sg2044_mux_table, 0);

static const struct clk_parent_data clk_mux_tpu_sys_parents[] = {
	{ .hw = &clk_div_tpu_sys_fixed.common.hw },
	{ .hw = &clk_div_tpu_sys_main.common.hw },
};

static DEFINE_SG2044_MUX(CLK_MUX_TPU_SYS, clk_mux_tpu_sys,
			 clk_mux_tpu_sys_parents,
			 CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			 0x020, 2, sg2044_mux_table, 0);

static const struct clk_parent_data clk_mux_rp_sys_parents[] = {
	{ .hw = &clk_div_rp_sys_fixed.common.hw },
	{ .hw = &clk_div_rp_sys_main.common.hw },
};

static DEFINE_SG2044_MUX(CLK_MUX_RP_SYS, clk_mux_rp_sys,
			 clk_mux_rp_sys_parents,
			 CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			 0x020, 1, sg2044_mux_table, 0);

static const struct clk_parent_data clk_mux_ap_sys_parents[] = {
	{ .hw = &clk_div_ap_sys_fixed.common.hw },
	{ .hw = &clk_div_ap_sys_main.common.hw },
};

static DEFINE_SG2044_MUX(CLK_MUX_AP_SYS, clk_mux_ap_sys,
			 clk_mux_ap_sys_parents,
			 CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			 0x020, 0, sg2044_mux_table, 0);

static const struct clk_parent_data clk_mux_vc_src0_parents[] = {
	{ .hw = &clk_div_vc_src0_fixed.common.hw },
	{ .hw = &clk_div_vc_src0_main.common.hw },
};

static DEFINE_SG2044_MUX(CLK_MUX_VC_SRC0, clk_mux_vc_src0,
			 clk_mux_vc_src0_parents,
			 CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			 0x020, 4, sg2044_mux_table, 0);

static const struct clk_parent_data clk_mux_vc_src1_parents[] = {
	{ .hw = &clk_div_vc_src1_fixed.common.hw },
	{ .hw = &clk_div_vc_src1_main.common.hw },
};

static DEFINE_SG2044_MUX(CLK_MUX_VC_SRC1, clk_mux_vc_src1,
			 clk_mux_vc_src1_parents,
			 CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			 0x020, 5, sg2044_mux_table, 0);

static const struct clk_parent_data clk_mux_cxp_mac_parents[] = {
	{ .hw = &clk_div_cxp_mac_fixed.common.hw },
	{ .hw = &clk_div_cxp_mac_main.common.hw },
};

static DEFINE_SG2044_MUX(CLK_MUX_CXP_MAC, clk_mux_cxp_mac,
			 clk_mux_cxp_mac_parents,
			 CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			 0x020, 6, sg2044_mux_table, 0);

static const struct clk_hw *clk_gate_ap_sys_parent[] = {
	&clk_mux_ap_sys.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_AP_SYS, clk_gate_ap_sys,
			  clk_gate_ap_sys_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x000, 0, 0);

static const struct clk_hw *clk_gate_rp_sys_parent[] = {
	&clk_mux_rp_sys.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_RP_SYS, clk_gate_rp_sys,
			  clk_gate_rp_sys_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x000, 2, 0);

static const struct clk_hw *clk_gate_tpu_sys_parent[] = {
	&clk_mux_tpu_sys.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_TPU_SYS, clk_gate_tpu_sys,
			  clk_gate_tpu_sys_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x000, 3, 0);

static const struct clk_hw *clk_gate_noc_sys_parent[] = {
	&clk_mux_noc_sys.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_NOC_SYS, clk_gate_noc_sys,
			  clk_gate_noc_sys_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x000, 8, 0);

static const struct clk_hw *clk_gate_vc_src0_parent[] = {
	&clk_mux_vc_src0.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_VC_SRC0, clk_gate_vc_src0,
			  clk_gate_vc_src0_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x000, 9, 0);

static const struct clk_hw *clk_gate_vc_src1_parent[] = {
	&clk_mux_vc_src1.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_VC_SRC1, clk_gate_vc_src1,
			  clk_gate_vc_src1_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x000, 10, 0);

static const struct clk_hw *clk_gate_ddr0_parent[] = {
	&clk_mux_ddr0.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_DDR0, clk_gate_ddr0,
			  clk_gate_ddr0_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x008, 7, 0);

static const struct clk_hw *clk_gate_ddr1_parent[] = {
	&clk_mux_ddr1.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_DDR1, clk_gate_ddr1,
			  clk_gate_ddr1_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x008, 8, 0);

static const struct clk_hw *clk_gate_ddr2_parent[] = {
	&clk_mux_ddr2.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_DDR2, clk_gate_ddr2,
			  clk_gate_ddr2_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x008, 9, 0);

static const struct clk_hw *clk_gate_ddr3_parent[] = {
	&clk_mux_ddr3.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_DDR3, clk_gate_ddr3,
			  clk_gate_ddr3_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x008, 10, 0);

static const struct clk_hw *clk_gate_ddr4_parent[] = {
	&clk_mux_ddr4.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_DDR4, clk_gate_ddr4,
			  clk_gate_ddr4_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x008, 11, 0);

static const struct clk_hw *clk_gate_ddr5_parent[] = {
	&clk_mux_ddr5.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_DDR5, clk_gate_ddr5,
			  clk_gate_ddr5_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x008, 12, 0);

static const struct clk_hw *clk_gate_ddr6_parent[] = {
	&clk_mux_ddr6.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_DDR6, clk_gate_ddr6,
			  clk_gate_ddr6_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x008, 13, 0);

static const struct clk_hw *clk_gate_ddr7_parent[] = {
	&clk_mux_ddr7.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_DDR7, clk_gate_ddr7,
			  clk_gate_ddr7_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x008, 14, 0);

static const struct clk_hw *clk_gate_top_50m_parent[] = {
	&clk_div_top_50m.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_TOP_50M, clk_gate_top_50m,
			  clk_gate_top_50m_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x000, 1, 0);

static const struct clk_hw *clk_gate_sc_rx_parent[] = {
	&clk_div_top_50m.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_SC_RX, clk_gate_sc_rx,
			  clk_gate_sc_rx_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x000, 12, 0);

static const struct clk_hw *clk_gate_sc_rx_x0y1_parent[] = {
	&clk_div_top_50m.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_SC_RX_X0Y1, clk_gate_sc_rx_x0y1,
			  clk_gate_sc_rx_x0y1_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x000, 13, 0);

static DEFINE_SG2044_GATE(CLK_GATE_TOP_AXI0, clk_gate_top_axi0,
			  clk_div_top_axi0_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x008, 5, 0);

static const struct clk_hw *clk_gate_mailbox_intc_parent[] = {
	&clk_gate_top_axi0.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_INTC0, clk_gate_intc0,
			  clk_gate_mailbox_intc_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x020, 20, 0);

static DEFINE_SG2044_GATE(CLK_GATE_INTC1, clk_gate_intc1,
			  clk_gate_mailbox_intc_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x020, 21, 0);

static DEFINE_SG2044_GATE(CLK_GATE_INTC2, clk_gate_intc2,
			  clk_gate_mailbox_intc_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x020, 22, 0);

static DEFINE_SG2044_GATE(CLK_GATE_INTC3, clk_gate_intc3,
			  clk_gate_mailbox_intc_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x020, 23, 0);

static DEFINE_SG2044_GATE(CLK_GATE_MAILBOX0, clk_gate_mailbox0,
			  clk_gate_mailbox_intc_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x020, 16, 0);

static DEFINE_SG2044_GATE(CLK_GATE_MAILBOX1, clk_gate_mailbox1,
			  clk_gate_mailbox_intc_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x020, 17, 0);

static DEFINE_SG2044_GATE(CLK_GATE_MAILBOX2, clk_gate_mailbox2,
			  clk_gate_mailbox_intc_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x020, 18, 0);

static DEFINE_SG2044_GATE(CLK_GATE_MAILBOX3, clk_gate_mailbox3,
			  clk_gate_mailbox_intc_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x020, 19, 0);

static DEFINE_SG2044_GATE(CLK_GATE_TOP_AXI_HSPERI, clk_gate_top_axi_hsperi,
			  clk_div_top_axi_hsperi_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x008, 6, 0);

static DEFINE_SG2044_GATE(CLK_GATE_APB_TIMER, clk_gate_apb_timer,
			  clk_div_top_axi0_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 7, 0);

static const struct clk_hw *clk_gate_timer0_parent[] = {
	&clk_div_timer0.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_TIMER0, clk_gate_timer0,
			  clk_gate_timer0_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 8, 0);

static const struct clk_hw *clk_gate_timer1_parent[] = {
	&clk_div_timer1.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_TIMER1, clk_gate_timer1,
			  clk_gate_timer1_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 9, 0);

static const struct clk_hw *clk_gate_timer2_parent[] = {
	&clk_div_timer2.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_TIMER2, clk_gate_timer2,
			  clk_gate_timer2_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 10, 0);

static const struct clk_hw *clk_gate_timer3_parent[] = {
	&clk_div_timer3.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_TIMER3, clk_gate_timer3,
			  clk_gate_timer3_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 11, 0);

static const struct clk_hw *clk_gate_timer4_parent[] = {
	&clk_div_timer4.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_TIMER4, clk_gate_timer4,
			  clk_gate_timer4_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 12, 0);

static const struct clk_hw *clk_gate_timer5_parent[] = {
	&clk_div_timer5.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_TIMER5, clk_gate_timer5,
			  clk_gate_timer5_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 13, 0);

static const struct clk_hw *clk_gate_timer6_parent[] = {
	&clk_div_timer6.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_TIMER6, clk_gate_timer6,
			  clk_gate_timer6_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 14, 0);

static const struct clk_hw *clk_gate_timer7_parent[] = {
	&clk_div_timer7.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_TIMER7, clk_gate_timer7,
			  clk_gate_timer7_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 15, 0);

static DEFINE_SG2044_GATE(CLK_GATE_CXP_CFG, clk_gate_cxp_cfg,
			  clk_div_top_axi0_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x000, 15, 0);

static const struct clk_hw *clk_gate_cxp_mac_parent[] = {
	&clk_mux_cxp_mac.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_CXP_MAC, clk_gate_cxp_mac,
			  clk_gate_cxp_mac_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x000, 14, 0);

static const struct clk_hw *clk_gate_cxp_test_phy_parent[] = {
	&clk_div_cxp_test_phy.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_CXP_TEST_PHY, clk_gate_cxp_test_phy,
			  clk_gate_cxp_test_phy_parent,
			  CLK_SET_RATE_PARENT,
			  0x000, 6, 0);

static const struct clk_hw *clk_gate_cxp_test_eth_phy_parent[] = {
	&clk_div_cxp_test_eth_phy.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_CXP_TEST_ETH_PHY, clk_gate_cxp_test_eth_phy,
			  clk_gate_cxp_test_eth_phy_parent,
			  CLK_SET_RATE_PARENT,
			  0x000, 7, 0);

static const struct clk_hw *clk_gate_pcie_1g_parent[] = {
	&clk_div_pcie_1g.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_PCIE_1G, clk_gate_pcie_1g,
			  clk_gate_pcie_1g_parent,
			  CLK_SET_RATE_PARENT,
			  0x008, 15, 0);

static const struct clk_hw *clk_gate_c2c0_test_phy_parent[] = {
	&clk_div_c2c0_test_phy.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_C2C0_TEST_PHY, clk_gate_c2c0_test_phy,
			  clk_gate_c2c0_test_phy_parent,
			  CLK_SET_RATE_PARENT,
			  0x000, 4, 0);

static const struct clk_hw *clk_gate_c2c1_test_phy_parent[] = {
	&clk_div_c2c1_test_phy.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_C2C1_TEST_PHY, clk_gate_c2c1_test_phy,
			  clk_gate_c2c1_test_phy_parent,
			  CLK_SET_RATE_PARENT,
			  0x000, 5, 0);

static const struct clk_hw *clk_gate_uart_500m_parent[] = {
	&clk_div_uart_500m.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_UART_500M, clk_gate_uart_500m,
			  clk_gate_uart_500m_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 1, 0);

static DEFINE_SG2044_GATE(CLK_GATE_APB_UART, clk_gate_apb_uart,
			  clk_div_top_axi_hsperi_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 2, 0);

static DEFINE_SG2044_GATE(CLK_GATE_APB_SPI, clk_gate_apb_spi,
			  clk_div_top_axi_hsperi_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 22, 0);

static DEFINE_SG2044_GATE(CLK_GATE_AHB_SPIFMC, clk_gate_ahb_spifmc,
			  clk_div_top_axi0_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 5, 0);

static DEFINE_SG2044_GATE(CLK_GATE_APB_I2C, clk_gate_apb_i2c,
			  clk_div_top_axi0_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x004, 23, 0);

static DEFINE_SG2044_GATE(CLK_GATE_AXI_DBG_I2C, clk_gate_axi_dbg_i2c,
			  clk_div_top_axi_hsperi_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 3, 0);

static const struct clk_hw *clk_gate_gpio_db_parent[] = {
	&clk_div_gpio_db.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_GPIO_DB, clk_gate_gpio_db,
			  clk_gate_gpio_db_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x004, 21, 0);

static DEFINE_SG2044_GATE(CLK_GATE_APB_GPIO_INTR, clk_gate_apb_gpio_intr,
			  clk_div_top_axi0_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x004, 20, 0);

static DEFINE_SG2044_GATE(CLK_GATE_APB_GPIO, clk_gate_apb_gpio,
			  clk_div_top_axi0_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x004, 19, 0);

static const struct clk_hw *clk_gate_sd_parent[] = {
	&clk_div_sd.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_SD, clk_gate_sd,
			  clk_gate_sd_parent,
			  CLK_SET_RATE_PARENT,
			  0x008, 3, 0);

static DEFINE_SG2044_GATE(CLK_GATE_AXI_SD, clk_gate_axi_sd,
			  clk_div_top_axi_hsperi_parent,
			  CLK_SET_RATE_PARENT,
			  0x008, 2, 0);

static const struct clk_hw *clk_gate_sd_100k_parent[] = {
	&clk_div_sd_100k.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_SD_100K, clk_gate_sd_100k,
			  clk_gate_sd_100k_parent,
			  CLK_SET_RATE_PARENT,
			  0x008, 4, 0);

static const struct clk_hw *clk_gate_emmc_parent[] = {
	&clk_div_emmc.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_EMMC, clk_gate_emmc,
			  clk_gate_emmc_parent,
			  CLK_SET_RATE_PARENT,
			  0x008, 0, 0);

static DEFINE_SG2044_GATE(CLK_GATE_AXI_EMMC, clk_gate_axi_emmc,
			  clk_div_top_axi_hsperi_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 31, 0);

static const struct clk_hw *clk_gate_emmc_100k_parent[] = {
	&clk_div_emmc_100k.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_EMMC_100K, clk_gate_emmc_100k,
			  clk_gate_emmc_100k_parent,
			  CLK_SET_RATE_PARENT,
			  0x008, 1, 0);

static const struct clk_hw *clk_gate_efuse_parent[] = {
	&clk_div_efuse.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_EFUSE, clk_gate_efuse,
			  clk_gate_efuse_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 17, 0);

static DEFINE_SG2044_GATE(CLK_GATE_APB_EFUSE, clk_gate_apb_efuse,
			  clk_div_top_axi0_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 18, 0);

static DEFINE_SG2044_GATE(CLK_GATE_SYSDMA_AXI, clk_gate_sysdma_axi,
			  clk_div_top_axi_hsperi_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 0, 0);

static const struct clk_hw *clk_gate_tx_eth0_parent[] = {
	&clk_div_tx_eth0.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_TX_ETH0, clk_gate_tx_eth0,
			  clk_gate_tx_eth0_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 27, 0);

static DEFINE_SG2044_GATE(CLK_GATE_AXI_ETH0, clk_gate_axi_eth0,
			  clk_div_top_axi_hsperi_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 28, 0);

static const struct clk_hw *clk_gate_ptp_ref_i_eth0_parent[] = {
	&clk_div_ptp_ref_i_eth0.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_PTP_REF_I_ETH0, clk_gate_ptp_ref_i_eth0,
			  clk_gate_ptp_ref_i_eth0_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 29, 0);

static const struct clk_hw *clk_gate_ref_eth0_parent[] = {
	&clk_div_ref_eth0.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_REF_ETH0, clk_gate_ref_eth0,
			  clk_gate_ref_eth0_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 30, 0);

static DEFINE_SG2044_GATE(CLK_GATE_APB_RTC, clk_gate_apb_rtc,
			  clk_div_top_axi0_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x004, 26, 0);

static DEFINE_SG2044_GATE(CLK_GATE_APB_PWM, clk_gate_apb_pwm,
			  clk_div_top_axi0_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 25, 0);

static DEFINE_SG2044_GATE(CLK_GATE_APB_WDT, clk_gate_apb_wdt,
			  clk_div_top_axi0_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 24, 0);

static DEFINE_SG2044_GATE(CLK_GATE_AXI_SRAM, clk_gate_axi_sram,
			  clk_div_top_axi0_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x004, 6, 0);

static DEFINE_SG2044_GATE(CLK_GATE_AHB_ROM, clk_gate_ahb_rom,
			  clk_div_top_axi0_parent,
			  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			  0x004, 4, 0);

static const struct clk_hw *clk_gate_pka_parent[] = {
	&clk_div_pka.common.hw,
};

static DEFINE_SG2044_GATE(CLK_GATE_PKA, clk_gate_pka,
			  clk_gate_pka_parent,
			  CLK_SET_RATE_PARENT,
			  0x004, 16, 0);

static struct sg2044_clk_common * const sg2044_div_commons[] = {
	&clk_div_ap_sys_fixed.common,
	&clk_div_ap_sys_main.common,
	&clk_div_rp_sys_fixed.common,
	&clk_div_rp_sys_main.common,
	&clk_div_tpu_sys_fixed.common,
	&clk_div_tpu_sys_main.common,
	&clk_div_noc_sys_fixed.common,
	&clk_div_noc_sys_main.common,
	&clk_div_vc_src0_fixed.common,
	&clk_div_vc_src0_main.common,
	&clk_div_vc_src1_fixed.common,
	&clk_div_vc_src1_main.common,
	&clk_div_cxp_mac_fixed.common,
	&clk_div_cxp_mac_main.common,
	&clk_div_ddr0_fixed.common,
	&clk_div_ddr0_main.common,
	&clk_div_ddr1_fixed.common,
	&clk_div_ddr1_main.common,
	&clk_div_ddr2_fixed.common,
	&clk_div_ddr2_main.common,
	&clk_div_ddr3_fixed.common,
	&clk_div_ddr3_main.common,
	&clk_div_ddr4_fixed.common,
	&clk_div_ddr4_main.common,
	&clk_div_ddr5_fixed.common,
	&clk_div_ddr5_main.common,
	&clk_div_ddr6_fixed.common,
	&clk_div_ddr6_main.common,
	&clk_div_ddr7_fixed.common,
	&clk_div_ddr7_main.common,
	&clk_div_top_50m.common,
	&clk_div_top_axi0.common,
	&clk_div_top_axi_hsperi.common,
	&clk_div_timer0.common,
	&clk_div_timer1.common,
	&clk_div_timer2.common,
	&clk_div_timer3.common,
	&clk_div_timer4.common,
	&clk_div_timer5.common,
	&clk_div_timer6.common,
	&clk_div_timer7.common,
	&clk_div_cxp_test_phy.common,
	&clk_div_cxp_test_eth_phy.common,
	&clk_div_c2c0_test_phy.common,
	&clk_div_c2c1_test_phy.common,
	&clk_div_pcie_1g.common,
	&clk_div_uart_500m.common,
	&clk_div_gpio_db.common,
	&clk_div_sd.common,
	&clk_div_sd_100k.common,
	&clk_div_emmc.common,
	&clk_div_emmc_100k.common,
	&clk_div_efuse.common,
	&clk_div_tx_eth0.common,
	&clk_div_ptp_ref_i_eth0.common,
	&clk_div_ref_eth0.common,
	&clk_div_pka.common,
};

static struct sg2044_clk_common * const sg2044_mux_commons[] = {
	&clk_mux_ddr0.common,
	&clk_mux_ddr1.common,
	&clk_mux_ddr2.common,
	&clk_mux_ddr3.common,
	&clk_mux_ddr4.common,
	&clk_mux_ddr5.common,
	&clk_mux_ddr6.common,
	&clk_mux_ddr7.common,
	&clk_mux_noc_sys.common,
	&clk_mux_tpu_sys.common,
	&clk_mux_rp_sys.common,
	&clk_mux_ap_sys.common,
	&clk_mux_vc_src0.common,
	&clk_mux_vc_src1.common,
	&clk_mux_cxp_mac.common,
};

static struct sg2044_clk_common * const sg2044_gate_commons[] = {
	&clk_gate_ap_sys.common,
	&clk_gate_rp_sys.common,
	&clk_gate_tpu_sys.common,
	&clk_gate_noc_sys.common,
	&clk_gate_vc_src0.common,
	&clk_gate_vc_src1.common,
	&clk_gate_ddr0.common,
	&clk_gate_ddr1.common,
	&clk_gate_ddr2.common,
	&clk_gate_ddr3.common,
	&clk_gate_ddr4.common,
	&clk_gate_ddr5.common,
	&clk_gate_ddr6.common,
	&clk_gate_ddr7.common,
	&clk_gate_top_50m.common,
	&clk_gate_sc_rx.common,
	&clk_gate_sc_rx_x0y1.common,
	&clk_gate_top_axi0.common,
	&clk_gate_intc0.common,
	&clk_gate_intc1.common,
	&clk_gate_intc2.common,
	&clk_gate_intc3.common,
	&clk_gate_mailbox0.common,
	&clk_gate_mailbox1.common,
	&clk_gate_mailbox2.common,
	&clk_gate_mailbox3.common,
	&clk_gate_top_axi_hsperi.common,
	&clk_gate_apb_timer.common,
	&clk_gate_timer0.common,
	&clk_gate_timer1.common,
	&clk_gate_timer2.common,
	&clk_gate_timer3.common,
	&clk_gate_timer4.common,
	&clk_gate_timer5.common,
	&clk_gate_timer6.common,
	&clk_gate_timer7.common,
	&clk_gate_cxp_cfg.common,
	&clk_gate_cxp_mac.common,
	&clk_gate_cxp_test_phy.common,
	&clk_gate_cxp_test_eth_phy.common,
	&clk_gate_pcie_1g.common,
	&clk_gate_c2c0_test_phy.common,
	&clk_gate_c2c1_test_phy.common,
	&clk_gate_uart_500m.common,
	&clk_gate_apb_uart.common,
	&clk_gate_apb_spi.common,
	&clk_gate_ahb_spifmc.common,
	&clk_gate_apb_i2c.common,
	&clk_gate_axi_dbg_i2c.common,
	&clk_gate_gpio_db.common,
	&clk_gate_apb_gpio_intr.common,
	&clk_gate_apb_gpio.common,
	&clk_gate_sd.common,
	&clk_gate_axi_sd.common,
	&clk_gate_sd_100k.common,
	&clk_gate_emmc.common,
	&clk_gate_axi_emmc.common,
	&clk_gate_emmc_100k.common,
	&clk_gate_efuse.common,
	&clk_gate_apb_efuse.common,
	&clk_gate_sysdma_axi.common,
	&clk_gate_tx_eth0.common,
	&clk_gate_axi_eth0.common,
	&clk_gate_ptp_ref_i_eth0.common,
	&clk_gate_ref_eth0.common,
	&clk_gate_apb_rtc.common,
	&clk_gate_apb_pwm.common,
	&clk_gate_apb_wdt.common,
	&clk_gate_axi_sram.common,
	&clk_gate_ahb_rom.common,
	&clk_gate_pka.common,
};

static void sg2044_clk_fix_init_parent(struct clk_hw **pdata,
				       const struct clk_init_data *init,
				       struct clk_hw_onecell_data *data)
{
	u8 i;
	const struct clk_hw *hw;
	const struct sg2044_clk_common *common;

	for (i = 0; i < init->num_parents; i++) {
		hw = init->parent_hws[i];
		common = hw_to_sg2044_clk_common(hw);

		WARN(!data->hws[common->id], "clk %u is not register\n",
		     common->id);
		pdata[i] = data->hws[common->id];
	}
}

static int sg2044_clk_init_ctrl(struct device *dev, void __iomem *reg,
				struct sg2044_clk_ctrl *ctrl,
				const struct sg2044_clk_desc_data *desc)
{
	int ret, i;
	struct clk_hw *hw;

	spin_lock_init(&ctrl->lock);

	for (i = 0; i < desc->num_div; i++) {
		struct sg2044_clk_common *common = desc->div[i];

		common->lock = &ctrl->lock;
		common->base = reg;

		ret = devm_clk_hw_register(dev, &common->hw);
		if (ret)
			return ret;

		ctrl->data.hws[common->id] = &common->hw;
	}

	for (i = 0; i < desc->num_mux; i++) {
		struct sg2044_clk_common *common = desc->mux[i];
		struct sg2044_mux *mux = hw_to_sg2044_mux(&common->hw);
		const struct clk_init_data *init = common->hw.init;

		common->lock = &ctrl->lock;
		common->base = reg;

		hw = devm_clk_hw_register_mux_parent_data_table(dev,
								init->name,
								init->parent_data,
								init->num_parents,
								init->flags,
								reg + mux->mux.offset,
								mux->mux.shift,
								1,
								mux->mux.flags,
								mux->mux.table,
								&ctrl->lock);
		if (IS_ERR(hw))
			return PTR_ERR(hw);

		if (!(mux->mux.flags & CLK_MUX_READ_ONLY)) {
			mux->nb.notifier_call = sg2044_mux_notifier_cb;
			ret = devm_clk_notifier_register(dev, hw->clk,
							 &mux->nb);
			if (ret < 0)
				return dev_err_probe(dev, ret,
						     "%s: failed to register notifier\n",
						     clk_hw_get_name(hw));
		}

		ctrl->data.hws[common->id] = hw;
	}

	for (i = 0; i < desc->num_gate; i++) {
		struct sg2044_clk_common *common = desc->gate[i];
		struct sg2044_gate *gate = hw_to_sg2044_gate(&common->hw);
		const struct clk_init_data *init = common->hw.init;
		struct clk_hw *parent_hws[1] = { };

		sg2044_clk_fix_init_parent(parent_hws, init, &ctrl->data);
		common->lock = &ctrl->lock;
		common->base = reg;

		hw = devm_clk_hw_register_gate_parent_hw(dev, init->name,
							 parent_hws[0],
							 init->flags,
							 reg + gate->gate.offset,
							 gate->gate.shift,
							 gate->gate.flags,
							 &ctrl->lock);
		if (IS_ERR(hw))
			return PTR_ERR(hw);

		ctrl->data.hws[common->id] = hw;
	}

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					   &ctrl->data);
}

static int sg2044_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sg2044_clk_ctrl *ctrl;
	const struct sg2044_clk_desc_data *desc;
	void __iomem *reg;
	u32 num_clks;

	reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	desc = device_get_match_data(dev);
	if (!desc)
		return dev_err_probe(dev, -EINVAL, "no match data for platform\n");

	num_clks = desc->num_div + desc->num_gate + desc->num_mux;

	ctrl = devm_kzalloc(dev, struct_size(ctrl, data.hws, num_clks), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	ctrl->data.num = num_clks;

	return sg2044_clk_init_ctrl(dev, reg, ctrl, desc);
}

static const struct sg2044_clk_desc_data sg2044_clk_desc_data = {
	.div = sg2044_div_commons,
	.mux = sg2044_mux_commons,
	.gate = sg2044_gate_commons,
	.num_div = ARRAY_SIZE(sg2044_div_commons),
	.num_mux = ARRAY_SIZE(sg2044_mux_commons),
	.num_gate = ARRAY_SIZE(sg2044_gate_commons),
};

static const struct of_device_id sg2044_clk_match[] = {
	{ .compatible = "sophgo,sg2044-clk", .data = &sg2044_clk_desc_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sg2044_clk_match);

static struct platform_driver sg2044_clk_driver = {
	.probe = sg2044_clk_probe,
	.driver = {
		.name = "sg2044-clk",
		.of_match_table = sg2044_clk_match,
	},
};
module_platform_driver(sg2044_clk_driver);

MODULE_AUTHOR("Inochi Amaoto <inochiama@gmail.com>");
MODULE_DESCRIPTION("Sophgo SG2044 clock driver");
MODULE_LICENSE("GPL");
