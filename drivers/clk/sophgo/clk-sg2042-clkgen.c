// SPDX-License-Identifier: GPL-2.0
/*
 * Sophgo SG2042 Clock Generator Driver
 *
 * Copyright (C) 2024 Sophgo Technology Inc.
 * Copyright (C) 2024 Chen Wang <unicorn_wang@outlook.com>
 */

#include <linux/array_size.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <asm/div64.h>

#include <dt-bindings/clock/sophgo,sg2042-clkgen.h>

#include "clk-sg2042.h"

/* Registers defined in SYS_CTRL */
#define R_PLL_BEGIN		0xC0
#define R_PLL_STAT		(0xC0 - R_PLL_BEGIN)
#define R_PLL_CLKEN_CONTROL	(0xC4 - R_PLL_BEGIN)
#define R_MPLL_CONTROL		(0xE8 - R_PLL_BEGIN)
#define R_FPLL_CONTROL		(0xF4 - R_PLL_BEGIN)
#define R_DPLL0_CONTROL		(0xF8 - R_PLL_BEGIN)
#define R_DPLL1_CONTROL		(0xFC - R_PLL_BEGIN)

/* Registers defined in CLOCK */
#define R_CLKENREG0		0x00
#define R_CLKENREG1		0x04
#define R_CLKSELREG0		0x20
#define R_CLKDIVREG0		0x40
#define R_CLKDIVREG1		0x44
#define R_CLKDIVREG2		0x48
#define R_CLKDIVREG3		0x4C
#define R_CLKDIVREG4		0x50
#define R_CLKDIVREG5		0x54
#define R_CLKDIVREG6		0x58
#define R_CLKDIVREG7		0x5C
#define R_CLKDIVREG8		0x60
#define R_CLKDIVREG9		0x64
#define R_CLKDIVREG10		0x68
#define R_CLKDIVREG11		0x6C
#define R_CLKDIVREG12		0x70
#define R_CLKDIVREG13		0x74
#define R_CLKDIVREG14		0x78
#define R_CLKDIVREG15		0x7C
#define R_CLKDIVREG16		0x80
#define R_CLKDIVREG17		0x84
#define R_CLKDIVREG18		0x88
#define R_CLKDIVREG19		0x8C
#define R_CLKDIVREG20		0x90
#define R_CLKDIVREG21		0x94
#define R_CLKDIVREG22		0x98
#define R_CLKDIVREG23		0x9C
#define R_CLKDIVREG24		0xA0
#define R_CLKDIVREG25		0xA4
#define R_CLKDIVREG26		0xA8
#define R_CLKDIVREG27		0xAC
#define R_CLKDIVREG28		0xB0
#define R_CLKDIVREG29		0xB4
#define R_CLKDIVREG30		0xB8

/* All following shift value are the same for all DIV registers */
#define SHIFT_DIV_RESET_CTRL	0
#define SHIFT_DIV_FACTOR_SEL	3
#define SHIFT_DIV_FACTOR	16

/**
 * struct sg2042_divider_clock - Divider clock
 * @hw:			clk_hw for initialization
 * @id:			used to map clk_onecell_data
 * @reg:		used for readl/writel.
 *			**NOTE**: DIV registers are ALL in CLOCK!
 * @lock:		spinlock to protect register access, modification of
 *			frequency can only be served one at the time
 * @offset_ctrl:	offset of divider control registers
 * @shift:		shift of "Clock Divider Factor" in divider control register
 * @width:		width of "Clock Divider Factor" in divider control register
 * @div_flags:		private flags for this clock, not for framework-specific
 * @initval:		In the divider control register, we can configure whether
 *			to use the value of "Clock Divider Factor" or just use
 *			the initial value pre-configured by IC. BIT[3] controls
 *			this and by default (value is 0), means initial value
 *			is used.
 *			**NOTE** that we cannot read the initial value (default
 *			value when poweron) and default value of "Clock Divider
 *			Factor" is zero, which I think is a hardware design flaw
 *			and should be sync-ed with the initial value. So in
 *			software we have to add a configuration item (initval)
 *			to manually configure this value and use it when BIT[3]
 *			is zero.
 */
struct sg2042_divider_clock {
	struct clk_hw hw;

	unsigned int id;

	void __iomem *reg;
	/* protect register access */
	spinlock_t *lock;

	u32 offset_ctrl;
	u8 shift;
	u8 width;
	u8 div_flags;
	u32 initval;
};

#define to_sg2042_clk_divider(_hw)	\
	container_of(_hw, struct sg2042_divider_clock, hw)

/**
 * struct sg2042_gate_clock - Gate clock
 * @hw:			clk_hw for initialization
 * @id:			used to map clk_onecell_data
 * @offset_enable:	offset of gate enable registers
 * @bit_idx:		which bit in the register controls gating of this clock
 */
struct sg2042_gate_clock {
	struct clk_hw hw;

	unsigned int id;

	u32 offset_enable;
	u8 bit_idx;
};

/**
 * struct sg2042_mux_clock - Mux clock
 * @hw:			clk_hw for initialization
 * @id:			used to map clk_onecell_data
 * @offset_select:	offset of mux selection registers
 *			**NOTE**: MUX registers are ALL in CLOCK!
 * @shift:		shift of "Clock Select" in mux selection register
 * @width:		width of "Clock Select" in mux selection register
 * @clk_nb:		used for notification
 * @original_index:	set by notifier callback
 */
struct sg2042_mux_clock {
	struct clk_hw hw;

	unsigned int id;

	u32 offset_select;
	u8 shift;
	u8 width;

	struct notifier_block clk_nb;
	u8 original_index;
};

#define to_sg2042_mux_nb(_nb) container_of(_nb, struct sg2042_mux_clock, clk_nb)

static unsigned long sg2042_clk_divider_recalc_rate(struct clk_hw *hw,
						    unsigned long parent_rate)
{
	struct sg2042_divider_clock *divider = to_sg2042_clk_divider(hw);
	unsigned long ret_rate;
	u32 val;

	if (!(readl(divider->reg) & BIT(SHIFT_DIV_FACTOR_SEL))) {
		val = divider->initval;
	} else {
		val = readl(divider->reg) >> divider->shift;
		val &= clk_div_mask(divider->width);
	}

	ret_rate = divider_recalc_rate(hw, parent_rate, val, NULL,
				       divider->div_flags, divider->width);

	pr_debug("--> %s: divider_recalc_rate: ret_rate = %ld\n",
		 clk_hw_get_name(hw), ret_rate);
	return ret_rate;
}

static long sg2042_clk_divider_round_rate(struct clk_hw *hw,
					  unsigned long rate,
					  unsigned long *prate)
{
	struct sg2042_divider_clock *divider = to_sg2042_clk_divider(hw);
	unsigned long ret_rate;
	u32 bestdiv;

	/* if read only, just return current value */
	if (divider->div_flags & CLK_DIVIDER_READ_ONLY) {
		if (!(readl(divider->reg) & BIT(SHIFT_DIV_FACTOR_SEL))) {
			bestdiv = divider->initval;
		} else {
			bestdiv = readl(divider->reg) >> divider->shift;
			bestdiv &= clk_div_mask(divider->width);
		}
		ret_rate = DIV_ROUND_UP_ULL((u64)*prate, bestdiv);
	} else {
		ret_rate = divider_round_rate(hw, rate, prate, NULL,
					      divider->width, divider->div_flags);
	}

	pr_debug("--> %s: divider_round_rate: val = %ld\n",
		 clk_hw_get_name(hw), ret_rate);
	return ret_rate;
}

static int sg2042_clk_divider_set_rate(struct clk_hw *hw,
				       unsigned long rate,
				       unsigned long parent_rate)
{
	struct sg2042_divider_clock *divider = to_sg2042_clk_divider(hw);
	unsigned long flags = 0;
	u32 val, val2, value;

	value = divider_get_val(rate, parent_rate, NULL,
				divider->width, divider->div_flags);

	if (divider->lock)
		spin_lock_irqsave(divider->lock, flags);
	else
		__acquire(divider->lock);

	/*
	 * The sequence of clock frequency modification is:
	 * Assert to reset divider.
	 * Modify the value of Clock Divide Factor (and High Wide if needed).
	 * De-assert to restore divided clock with new frequency.
	 */
	val = readl(divider->reg);

	/* assert */
	val &= ~BIT(SHIFT_DIV_RESET_CTRL);
	writel(val, divider->reg);

	if (divider->div_flags & CLK_DIVIDER_HIWORD_MASK) {
		val = clk_div_mask(divider->width) << (divider->shift + 16);
	} else {
		val = readl(divider->reg);
		val &= ~(clk_div_mask(divider->width) << divider->shift);
	}
	val |= value << divider->shift;
	val |= BIT(SHIFT_DIV_FACTOR_SEL);
	writel(val, divider->reg);
	val2 = val;

	/* de-assert */
	val |= BIT(SHIFT_DIV_RESET_CTRL);
	writel(val, divider->reg);

	if (divider->lock)
		spin_unlock_irqrestore(divider->lock, flags);
	else
		__release(divider->lock);

	pr_debug("--> %s: divider_set_rate: register val = 0x%x\n",
		 clk_hw_get_name(hw), val2);
	return 0;
}

static const struct clk_ops sg2042_clk_divider_ops = {
	.recalc_rate = sg2042_clk_divider_recalc_rate,
	.round_rate = sg2042_clk_divider_round_rate,
	.set_rate = sg2042_clk_divider_set_rate,
};

static const struct clk_ops sg2042_clk_divider_ro_ops = {
	.recalc_rate = sg2042_clk_divider_recalc_rate,
	.round_rate = sg2042_clk_divider_round_rate,
};

/*
 * Clock initialization macro naming rules:
 * FW: use CLK_HW_INIT_FW_NAME
 * HW: use CLK_HW_INIT_HW
 * HWS: use CLK_HW_INIT_HWS
 * RO: means Read-Only
 */
#define SG2042_DIV_FW(_id, _name, _parent,				\
		      _r_ctrl, _shift, _width,				\
		      _div_flag, _initval) {				\
		.id = _id,						\
		.hw.init = CLK_HW_INIT_FW_NAME(				\
				_name,					\
				_parent,				\
				&sg2042_clk_divider_ops,		\
				0),					\
		.offset_ctrl = _r_ctrl,					\
		.shift = _shift,					\
		.width = _width,					\
		.div_flags = _div_flag,					\
		.initval = _initval,					\
	}

#define SG2042_DIV_FW_RO(_id, _name, _parent,				\
		  _r_ctrl, _shift, _width,				\
		  _div_flag, _initval) {				\
		.id = _id,						\
		.hw.init = CLK_HW_INIT_FW_NAME(				\
				_name,					\
				_parent,				\
				&sg2042_clk_divider_ro_ops,		\
				0),					\
		.offset_ctrl = _r_ctrl,					\
		.shift = _shift,					\
		.width = _width,					\
		.div_flags = (_div_flag) | CLK_DIVIDER_READ_ONLY,	\
		.initval = _initval,					\
	}

#define SG2042_DIV_HW(_id, _name, _parent,				\
		      _r_ctrl, _shift, _width,				\
		      _div_flag, _initval) {				\
		.id = _id,						\
		.hw.init = CLK_HW_INIT_HW(				\
				_name,					\
				_parent,				\
				&sg2042_clk_divider_ops,		\
				0),					\
		.offset_ctrl = _r_ctrl,					\
		.shift = _shift,					\
		.width = _width,					\
		.div_flags = _div_flag,					\
		.initval = _initval,					\
	}

#define SG2042_DIV_HW_RO(_id, _name, _parent,				\
			 _r_ctrl, _shift, _width,			\
			 _div_flag, _initval) {				\
		.id = _id,						\
		.hw.init = CLK_HW_INIT_HW(				\
				_name,					\
				_parent,				\
				&sg2042_clk_divider_ro_ops,		\
				0),					\
		.offset_ctrl = _r_ctrl,					\
		.shift = _shift,					\
		.width = _width,					\
		.div_flags = (_div_flag) | CLK_DIVIDER_READ_ONLY,	\
		.initval = _initval,					\
	}

#define SG2042_DIV_HWS(_id, _name, _parent,				\
		       _r_ctrl, _shift, _width,				\
		       _div_flag, _initval) {				\
		.id = _id,						\
		.hw.init = CLK_HW_INIT_HWS(				\
				_name,					\
				_parent,				\
				&sg2042_clk_divider_ops,		\
				0),					\
		.offset_ctrl = _r_ctrl,					\
		.shift = _shift,					\
		.width = _width,					\
		.div_flags = _div_flag,					\
		.initval = _initval,					\
	}

#define SG2042_DIV_HWS_RO(_id, _name, _parent,				\
			  _r_ctrl, _shift, _width,			\
			  _div_flag, _initval) {			\
		.id = _id,						\
		.hw.init = CLK_HW_INIT_HWS(				\
				_name,					\
				_parent,				\
				&sg2042_clk_divider_ro_ops,		\
				0),					\
		.offset_ctrl = _r_ctrl,					\
		.shift = _shift,					\
		.width = _width,					\
		.div_flags = (_div_flag) | CLK_DIVIDER_READ_ONLY,	\
		.initval = _initval,					\
	}

#define SG2042_GATE_HWS(_id, _name, _parent, _flags,	\
			_r_enable, _bit_idx) {		\
		.id = _id,				\
		.hw.init = CLK_HW_INIT_HWS(		\
				_name,			\
				_parent,		\
				NULL,			\
				_flags),		\
		.offset_enable = _r_enable,		\
		.bit_idx = _bit_idx,			\
	}

#define SG2042_GATE_HW(_id, _name, _parent, _flags,	\
		       _r_enable, _bit_idx) {		\
		.id = _id,				\
		.hw.init = CLK_HW_INIT_HW(		\
				_name,			\
				_parent,		\
				NULL,			\
				_flags),		\
		.offset_enable = _r_enable,		\
		.bit_idx = _bit_idx,			\
	}

#define SG2042_GATE_FW(_id, _name, _parent, _flags,	\
		       _r_enable, _bit_idx) {		\
		.id = _id,				\
		.hw.init = CLK_HW_INIT_FW_NAME(		\
				_name,			\
				_parent,		\
				NULL,			\
				_flags),		\
		.offset_enable = _r_enable,		\
		.bit_idx = _bit_idx,			\
	}

#define SG2042_MUX(_id, _name, _parents, _flags, _r_select, _shift, _width) { \
		.id = _id,					\
		.hw.init = CLK_HW_INIT_PARENTS_HW(		\
				_name,				\
				_parents,			\
				NULL,				\
				_flags),			\
		.offset_select = _r_select,			\
		.shift = _shift,				\
		.width = _width,				\
	}

/*
 * Clock items in the array are sorted according to the clock-tree diagram,
 * from top to bottom, from upstream to downstream. Read TRM for details.
 */

/* updated during probe/registration */
static const struct clk_hw *clk_gate_ddr01_div0[] = { NULL };
static const struct clk_hw *clk_gate_ddr01_div1[] = { NULL };
static const struct clk_hw *clk_gate_ddr23_div0[] = { NULL };
static const struct clk_hw *clk_gate_ddr23_div1[] = { NULL };
static const struct clk_hw *clk_gate_rp_cpu_normal_div0[] = { NULL };
static const struct clk_hw *clk_gate_rp_cpu_normal_div1[] = { NULL };
static const struct clk_hw *clk_gate_axi_ddr_div0[] = { NULL };
static const struct clk_hw *clk_gate_axi_ddr_div1[] = { NULL };

static const struct sg2042_gate_clock sg2042_gate_clks_level_1[] = {
	SG2042_GATE_FW(GATE_CLK_DDR01_DIV0, "clk_gate_ddr01_div0", "dpll0",
		       CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
		       R_CLKDIVREG27, 4),
	SG2042_GATE_FW(GATE_CLK_DDR01_DIV1, "clk_gate_ddr01_div1", "fpll",
		       CLK_IS_CRITICAL,
		       R_CLKDIVREG28, 4),

	SG2042_GATE_FW(GATE_CLK_DDR23_DIV0, "clk_gate_ddr23_div0", "dpll1",
		       CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
		       R_CLKDIVREG29, 4),
	SG2042_GATE_FW(GATE_CLK_DDR23_DIV1, "clk_gate_ddr23_div1", "fpll",
		       CLK_IS_CRITICAL,
		       R_CLKDIVREG30, 4),

	SG2042_GATE_FW(GATE_CLK_RP_CPU_NORMAL_DIV0,
		       "clk_gate_rp_cpu_normal_div0", "mpll",
		       CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
		       R_CLKDIVREG0, 4),
	SG2042_GATE_FW(GATE_CLK_RP_CPU_NORMAL_DIV1,
		       "clk_gate_rp_cpu_normal_div1", "fpll",
		       CLK_IS_CRITICAL,
		       R_CLKDIVREG1, 4),

	SG2042_GATE_FW(GATE_CLK_AXI_DDR_DIV0, "clk_gate_axi_ddr_div0", "mpll",
		       CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
		       R_CLKDIVREG25, 4),
	SG2042_GATE_FW(GATE_CLK_AXI_DDR_DIV1, "clk_gate_axi_ddr_div1", "fpll",
		       CLK_IS_CRITICAL,
		       R_CLKDIVREG26, 4),
};

#define DEF_DIVFLAG (CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO)

static struct sg2042_divider_clock sg2042_div_clks_level_1[] = {
	SG2042_DIV_HWS_RO(DIV_CLK_DPLL0_DDR01_0,
			  "clk_div_ddr01_0", clk_gate_ddr01_div0,
			  R_CLKDIVREG27, 16, 5, DEF_DIVFLAG, 1),
	SG2042_DIV_HWS_RO(DIV_CLK_FPLL_DDR01_1,
			  "clk_div_ddr01_1", clk_gate_ddr01_div1,
			  R_CLKDIVREG28, 16, 5, DEF_DIVFLAG, 1),

	SG2042_DIV_HWS_RO(DIV_CLK_DPLL1_DDR23_0,
			  "clk_div_ddr23_0", clk_gate_ddr23_div0,
			  R_CLKDIVREG29, 16, 5, DEF_DIVFLAG, 1),
	SG2042_DIV_HWS_RO(DIV_CLK_FPLL_DDR23_1,
			  "clk_div_ddr23_1", clk_gate_ddr23_div1,
			  R_CLKDIVREG30, 16, 5, DEF_DIVFLAG, 1),

	SG2042_DIV_HWS(DIV_CLK_MPLL_RP_CPU_NORMAL_0,
		       "clk_div_rp_cpu_normal_0", clk_gate_rp_cpu_normal_div0,
		       R_CLKDIVREG0, 16, 5, DEF_DIVFLAG, 1),
	SG2042_DIV_HWS(DIV_CLK_FPLL_RP_CPU_NORMAL_1,
		       "clk_div_rp_cpu_normal_1", clk_gate_rp_cpu_normal_div1,
		       R_CLKDIVREG1, 16, 5, DEF_DIVFLAG, 1),

	SG2042_DIV_HWS(DIV_CLK_MPLL_AXI_DDR_0,
		       "clk_div_axi_ddr_0", clk_gate_axi_ddr_div0,
		       R_CLKDIVREG25, 16, 5, DEF_DIVFLAG, 2),
	SG2042_DIV_HWS(DIV_CLK_FPLL_AXI_DDR_1,
		       "clk_div_axi_ddr_1", clk_gate_axi_ddr_div1,
		       R_CLKDIVREG26, 16, 5, DEF_DIVFLAG, 1),
};

/*
 * Note: regarding names for mux clock, "0/1" or "div0/div1" means the
 * first/second parent input source, not the register value.
 * For example:
 * "clk_div_ddr01_0" is the name of Clock divider 0 control of DDR01, and
 * "clk_gate_ddr01_div0" is the gate clock in front of the "clk_div_ddr01_0",
 * they are both controlled by register CLKDIVREG27;
 * "clk_div_ddr01_1" is the name of Clock divider 1 control of DDR01, and
 * "clk_gate_ddr01_div1" is the gate clock in front of the "clk_div_ddr01_1",
 * they are both controlled by register CLKDIVREG28;
 * While for register value of mux selection, use Clock Select for DDR01’s clock
 * as example, see CLKSELREG0, bit[2].
 * 1: Select in_dpll0_clk as clock source, correspondng to the parent input
 *    source from "clk_div_ddr01_0".
 * 0: Select in_fpll_clk as clock source, corresponding to the parent input
 *    source from "clk_div_ddr01_1".
 * So we need a table to define the array of register values corresponding to
 * the parent index and tell CCF about this when registering mux clock.
 */
static const u32 sg2042_mux_table[] = {1, 0};

/* Aliases just for easy reading */
#define clk_div_ddr01_0		(&sg2042_div_clks_level_1[0].hw)
#define clk_div_ddr01_1		(&sg2042_div_clks_level_1[1].hw)
#define clk_div_ddr23_0		(&sg2042_div_clks_level_1[2].hw)
#define clk_div_ddr23_1		(&sg2042_div_clks_level_1[3].hw)
#define clk_div_rp_cpu_normal_0	(&sg2042_div_clks_level_1[4].hw)
#define clk_div_rp_cpu_normal_1	(&sg2042_div_clks_level_1[5].hw)
#define clk_div_axi_ddr_0	(&sg2042_div_clks_level_1[6].hw)
#define clk_div_axi_ddr_1	(&sg2042_div_clks_level_1[7].hw)

static const struct clk_hw *clk_mux_ddr01_p[] = {
	clk_div_ddr01_0,
	clk_div_ddr01_1,
};

static const struct clk_hw *clk_mux_ddr23_p[] = {
	clk_div_ddr23_0,
	clk_div_ddr23_1,
};

static const struct clk_hw *clk_mux_rp_cpu_normal_p[] = {
	clk_div_rp_cpu_normal_0,
	clk_div_rp_cpu_normal_1,
};

static const struct clk_hw *clk_mux_axi_ddr_p[] = {
	clk_div_axi_ddr_0,
	clk_div_axi_ddr_1,
};

/* Mux clocks to be updated during probe/registration */
static const struct clk_hw *clk_mux_ddr01[] = { NULL };
static const struct clk_hw *clk_mux_ddr23[] = { NULL };
static const struct clk_hw *clk_mux_rp_cpu_normal[] = { NULL };
static const struct clk_hw *clk_mux_axi_ddr[] = { NULL };

static struct sg2042_mux_clock sg2042_mux_clks[] = {
	SG2042_MUX(MUX_CLK_DDR01, "clk_mux_ddr01", clk_mux_ddr01_p,
		   CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT | CLK_MUX_READ_ONLY,
		   R_CLKSELREG0, 2, 1),
	SG2042_MUX(MUX_CLK_DDR23, "clk_mux_ddr23", clk_mux_ddr23_p,
		   CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT | CLK_MUX_READ_ONLY,
		   R_CLKSELREG0, 3, 1),
	SG2042_MUX(MUX_CLK_RP_CPU_NORMAL, "clk_mux_rp_cpu_normal", clk_mux_rp_cpu_normal_p,
		   CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
		   R_CLKSELREG0, 0, 1),
	SG2042_MUX(MUX_CLK_AXI_DDR, "clk_mux_axi_ddr", clk_mux_axi_ddr_p,
		   CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
		   R_CLKSELREG0, 1, 1),
};

/* Aliases just for easy reading */
#define clk_div_top_rp_cmn_div2	(&sg2042_div_clks_level_2[0].hw)
#define clk_div_50m_a53		(&sg2042_div_clks_level_2[1].hw)
#define clk_div_timer1		(&sg2042_div_clks_level_2[2].hw)
#define clk_div_timer2		(&sg2042_div_clks_level_2[3].hw)
#define clk_div_timer3		(&sg2042_div_clks_level_2[4].hw)
#define clk_div_timer4		(&sg2042_div_clks_level_2[5].hw)
#define clk_div_timer5		(&sg2042_div_clks_level_2[6].hw)
#define clk_div_timer6		(&sg2042_div_clks_level_2[7].hw)
#define clk_div_timer7		(&sg2042_div_clks_level_2[8].hw)
#define clk_div_timer8		(&sg2042_div_clks_level_2[9].hw)
#define clk_div_uart_500m	(&sg2042_div_clks_level_2[10].hw)
#define clk_div_ahb_lpc		(&sg2042_div_clks_level_2[11].hw)
#define clk_div_efuse		(&sg2042_div_clks_level_2[12].hw)
#define clk_div_tx_eth0		(&sg2042_div_clks_level_2[13].hw)
#define clk_div_ptp_ref_i_eth0	(&sg2042_div_clks_level_2[14].hw)
#define clk_div_ref_eth0	(&sg2042_div_clks_level_2[15].hw)
#define clk_div_emmc		(&sg2042_div_clks_level_2[16].hw)
#define clk_div_sd		(&sg2042_div_clks_level_2[17].hw)
#define clk_div_top_axi0	(&sg2042_div_clks_level_2[18].hw)
#define clk_div_100k_emmc	(&sg2042_div_clks_level_2[19].hw)
#define clk_div_100k_sd		(&sg2042_div_clks_level_2[20].hw)
#define clk_div_gpio_db		(&sg2042_div_clks_level_2[21].hw)
#define clk_div_top_axi_hsperi	(&sg2042_div_clks_level_2[22].hw)

static struct sg2042_divider_clock sg2042_div_clks_level_2[] = {
	SG2042_DIV_HWS(DIV_CLK_FPLL_TOP_RP_CMN_DIV2,
		       "clk_div_top_rp_cmn_div2", clk_mux_rp_cpu_normal,
		       R_CLKDIVREG3, 16, 16, DEF_DIVFLAG, 2),

	SG2042_DIV_FW(DIV_CLK_FPLL_50M_A53, "clk_div_50m_a53", "fpll",
		      R_CLKDIVREG2, 16, 8, DEF_DIVFLAG, 20),
	/* downstream of div_50m_a53 */
	SG2042_DIV_HW(DIV_CLK_FPLL_DIV_TIMER1, "clk_div_timer1", clk_div_50m_a53,
		      R_CLKDIVREG6, 16, 16, DEF_DIVFLAG, 1),
	SG2042_DIV_HW(DIV_CLK_FPLL_DIV_TIMER2, "clk_div_timer2", clk_div_50m_a53,
		      R_CLKDIVREG7, 16, 16, DEF_DIVFLAG, 1),
	SG2042_DIV_HW(DIV_CLK_FPLL_DIV_TIMER3, "clk_div_timer3", clk_div_50m_a53,
		      R_CLKDIVREG8, 16, 16, DEF_DIVFLAG, 1),
	SG2042_DIV_HW(DIV_CLK_FPLL_DIV_TIMER4, "clk_div_timer4", clk_div_50m_a53,
		      R_CLKDIVREG9, 16, 16, DEF_DIVFLAG, 1),
	SG2042_DIV_HW(DIV_CLK_FPLL_DIV_TIMER5, "clk_div_timer5", clk_div_50m_a53,
		      R_CLKDIVREG10, 16, 16, DEF_DIVFLAG, 1),
	SG2042_DIV_HW(DIV_CLK_FPLL_DIV_TIMER6, "clk_div_timer6", clk_div_50m_a53,
		      R_CLKDIVREG11, 16, 16, DEF_DIVFLAG, 1),
	SG2042_DIV_HW(DIV_CLK_FPLL_DIV_TIMER7, "clk_div_timer7", clk_div_50m_a53,
		      R_CLKDIVREG12, 16, 16, DEF_DIVFLAG, 1),
	SG2042_DIV_HW(DIV_CLK_FPLL_DIV_TIMER8, "clk_div_timer8", clk_div_50m_a53,
		      R_CLKDIVREG13, 16, 16, DEF_DIVFLAG, 1),

	/*
	 * Set clk_div_uart_500m as RO, because the width of CLKDIVREG4 is too
	 * narrow for us to produce 115200. Use UART internal divider directly.
	 */
	SG2042_DIV_FW_RO(DIV_CLK_FPLL_UART_500M, "clk_div_uart_500m", "fpll",
			 R_CLKDIVREG4, 16, 7, DEF_DIVFLAG, 2),
	SG2042_DIV_FW(DIV_CLK_FPLL_AHB_LPC, "clk_div_ahb_lpc", "fpll",
		      R_CLKDIVREG5, 16, 16, DEF_DIVFLAG, 5),
	SG2042_DIV_FW(DIV_CLK_FPLL_EFUSE, "clk_div_efuse", "fpll",
		      R_CLKDIVREG14, 16, 7, DEF_DIVFLAG, 40),
	SG2042_DIV_FW(DIV_CLK_FPLL_TX_ETH0, "clk_div_tx_eth0", "fpll",
		      R_CLKDIVREG16, 16, 11, DEF_DIVFLAG, 8),
	SG2042_DIV_FW(DIV_CLK_FPLL_PTP_REF_I_ETH0,
		      "clk_div_ptp_ref_i_eth0", "fpll",
		      R_CLKDIVREG17, 16, 8, DEF_DIVFLAG, 20),
	SG2042_DIV_FW(DIV_CLK_FPLL_REF_ETH0, "clk_div_ref_eth0", "fpll",
		      R_CLKDIVREG18, 16, 8, DEF_DIVFLAG, 40),
	SG2042_DIV_FW(DIV_CLK_FPLL_EMMC, "clk_div_emmc", "fpll",
		      R_CLKDIVREG19, 16, 5, DEF_DIVFLAG, 10),
	SG2042_DIV_FW(DIV_CLK_FPLL_SD, "clk_div_sd", "fpll",
		      R_CLKDIVREG21, 16, 5, DEF_DIVFLAG, 10),

	SG2042_DIV_FW(DIV_CLK_FPLL_TOP_AXI0, "clk_div_top_axi0", "fpll",
		      R_CLKDIVREG23, 16, 5, DEF_DIVFLAG, 10),
	/* downstream of div_top_axi0 */
	SG2042_DIV_HW(DIV_CLK_FPLL_100K_EMMC, "clk_div_100k_emmc", clk_div_top_axi0,
		      R_CLKDIVREG20, 16, 16, DEF_DIVFLAG, 1000),
	SG2042_DIV_HW(DIV_CLK_FPLL_100K_SD, "clk_div_100k_sd", clk_div_top_axi0,
		      R_CLKDIVREG22, 16, 16, DEF_DIVFLAG, 1000),
	SG2042_DIV_HW(DIV_CLK_FPLL_GPIO_DB, "clk_div_gpio_db", clk_div_top_axi0,
		      R_CLKDIVREG15, 16, 16, DEF_DIVFLAG, 1000),

	SG2042_DIV_FW(DIV_CLK_FPLL_TOP_AXI_HSPERI,
		      "clk_div_top_axi_hsperi", "fpll",
		      R_CLKDIVREG24, 16, 5, DEF_DIVFLAG, 4),
};

/* Gate clocks to be updated during probe/registration */
static const struct clk_hw *clk_gate_rp_cpu_normal[] = { NULL };
static const struct clk_hw *clk_gate_top_rp_cmn_div2[] = { NULL };

static const struct sg2042_gate_clock sg2042_gate_clks_level_2[] = {
	SG2042_GATE_HWS(GATE_CLK_DDR01, "clk_gate_ddr01", clk_mux_ddr01,
			CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			R_CLKENREG1, 14),

	SG2042_GATE_HWS(GATE_CLK_DDR23, "clk_gate_ddr23", clk_mux_ddr23,
			CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			R_CLKENREG1, 15),

	SG2042_GATE_HWS(GATE_CLK_RP_CPU_NORMAL,
			"clk_gate_rp_cpu_normal", clk_mux_rp_cpu_normal,
			CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			R_CLKENREG0, 0),

	SG2042_GATE_HWS(GATE_CLK_AXI_DDR, "clk_gate_axi_ddr", clk_mux_axi_ddr,
			CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			R_CLKENREG1, 13),

	/* upon are gate clocks directly downstream of muxes */

	/* downstream of clk_div_top_rp_cmn_div2 */
	SG2042_GATE_HW(GATE_CLK_TOP_RP_CMN_DIV2,
		       "clk_gate_top_rp_cmn_div2", clk_div_top_rp_cmn_div2,
		       CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, R_CLKENREG0, 2),
	SG2042_GATE_HWS(GATE_CLK_HSDMA, "clk_gate_hsdma", clk_gate_top_rp_cmn_div2,
			CLK_SET_RATE_PARENT, R_CLKENREG1, 10),

	/*
	 * downstream of clk_gate_rp_cpu_normal
	 *
	 * FIXME: there should be one 1/2 DIV between clk_gate_rp_cpu_normal
	 * and clk_gate_axi_pcie0/clk_gate_axi_pcie1.
	 * But the 1/2 DIV is fixed and no configurable register exported, so
	 * when reading from these two clocks, the rate value are still the
	 * same as that of clk_gate_rp_cpu_normal, it's not correct.
	 * This just affects the value read.
	 */
	SG2042_GATE_HWS(GATE_CLK_AXI_PCIE0,
			"clk_gate_axi_pcie0", clk_gate_rp_cpu_normal,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, R_CLKENREG1, 8),
	SG2042_GATE_HWS(GATE_CLK_AXI_PCIE1,
			"clk_gate_axi_pcie1", clk_gate_rp_cpu_normal,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, R_CLKENREG1, 9),

	/* downstream of div_50m_a53 */
	SG2042_GATE_HW(GATE_CLK_A53_50M, "clk_gate_a53_50m", clk_div_50m_a53,
		       CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, R_CLKENREG0, 1),
	SG2042_GATE_HW(GATE_CLK_TIMER1, "clk_gate_timer1", clk_div_timer1,
		       CLK_SET_RATE_PARENT, R_CLKENREG0, 12),
	SG2042_GATE_HW(GATE_CLK_TIMER2, "clk_gate_timer2", clk_div_timer2,
		       CLK_SET_RATE_PARENT, R_CLKENREG0, 13),
	SG2042_GATE_HW(GATE_CLK_TIMER3, "clk_gate_timer3", clk_div_timer3,
		       CLK_SET_RATE_PARENT, R_CLKENREG0, 14),
	SG2042_GATE_HW(GATE_CLK_TIMER4, "clk_gate_timer4", clk_div_timer4,
		       CLK_SET_RATE_PARENT, R_CLKENREG0, 15),
	SG2042_GATE_HW(GATE_CLK_TIMER5, "clk_gate_timer5", clk_div_timer5,
		       CLK_SET_RATE_PARENT, R_CLKENREG0, 16),
	SG2042_GATE_HW(GATE_CLK_TIMER6, "clk_gate_timer6", clk_div_timer6,
		       CLK_SET_RATE_PARENT, R_CLKENREG0, 17),
	SG2042_GATE_HW(GATE_CLK_TIMER7, "clk_gate_timer7", clk_div_timer7,
		       CLK_SET_RATE_PARENT, R_CLKENREG0, 18),
	SG2042_GATE_HW(GATE_CLK_TIMER8, "clk_gate_timer8", clk_div_timer8,
		       CLK_SET_RATE_PARENT, R_CLKENREG0, 19),

	/* gate clocks downstream from div clocks one-to-one */
	SG2042_GATE_HW(GATE_CLK_UART_500M, "clk_gate_uart_500m", clk_div_uart_500m,
		       CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, R_CLKENREG0, 4),
	SG2042_GATE_HW(GATE_CLK_AHB_LPC, "clk_gate_ahb_lpc", clk_div_ahb_lpc,
		       CLK_SET_RATE_PARENT, R_CLKENREG0, 7),
	SG2042_GATE_HW(GATE_CLK_EFUSE, "clk_gate_efuse", clk_div_efuse,
		       CLK_SET_RATE_PARENT, R_CLKENREG0, 20),
	SG2042_GATE_HW(GATE_CLK_TX_ETH0, "clk_gate_tx_eth0", clk_div_tx_eth0,
		       CLK_SET_RATE_PARENT, R_CLKENREG0, 30),
	SG2042_GATE_HW(GATE_CLK_PTP_REF_I_ETH0,
		       "clk_gate_ptp_ref_i_eth0", clk_div_ptp_ref_i_eth0,
		       CLK_SET_RATE_PARENT, R_CLKENREG1, 0),
	SG2042_GATE_HW(GATE_CLK_REF_ETH0, "clk_gate_ref_eth0", clk_div_ref_eth0,
		       CLK_SET_RATE_PARENT, R_CLKENREG1, 1),
	SG2042_GATE_HW(GATE_CLK_EMMC_100M, "clk_gate_emmc", clk_div_emmc,
		       CLK_SET_RATE_PARENT, R_CLKENREG1, 3),
	SG2042_GATE_HW(GATE_CLK_SD_100M, "clk_gate_sd", clk_div_sd,
		       CLK_SET_RATE_PARENT, R_CLKENREG1, 6),

	/* downstream of clk_div_top_axi0 */
	SG2042_GATE_HW(GATE_CLK_AHB_ROM, "clk_gate_ahb_rom", clk_div_top_axi0,
		       0, R_CLKENREG0, 8),
	SG2042_GATE_HW(GATE_CLK_AHB_SF, "clk_gate_ahb_sf", clk_div_top_axi0,
		       0, R_CLKENREG0, 9),
	SG2042_GATE_HW(GATE_CLK_AXI_SRAM, "clk_gate_axi_sram", clk_div_top_axi0,
		       CLK_IGNORE_UNUSED, R_CLKENREG0, 10),
	SG2042_GATE_HW(GATE_CLK_APB_TIMER, "clk_gate_apb_timer", clk_div_top_axi0,
		       CLK_IGNORE_UNUSED, R_CLKENREG0, 11),
	SG2042_GATE_HW(GATE_CLK_APB_EFUSE, "clk_gate_apb_efuse", clk_div_top_axi0,
		       0, R_CLKENREG0, 21),
	SG2042_GATE_HW(GATE_CLK_APB_GPIO, "clk_gate_apb_gpio", clk_div_top_axi0,
		       0, R_CLKENREG0, 22),
	SG2042_GATE_HW(GATE_CLK_APB_GPIO_INTR,
		       "clk_gate_apb_gpio_intr", clk_div_top_axi0,
		       CLK_IS_CRITICAL, R_CLKENREG0, 23),
	SG2042_GATE_HW(GATE_CLK_APB_I2C, "clk_gate_apb_i2c", clk_div_top_axi0,
		       0, R_CLKENREG0, 26),
	SG2042_GATE_HW(GATE_CLK_APB_WDT, "clk_gate_apb_wdt", clk_div_top_axi0,
		       0, R_CLKENREG0, 27),
	SG2042_GATE_HW(GATE_CLK_APB_PWM, "clk_gate_apb_pwm", clk_div_top_axi0,
		       0, R_CLKENREG0, 28),
	SG2042_GATE_HW(GATE_CLK_APB_RTC, "clk_gate_apb_rtc", clk_div_top_axi0,
		       0, R_CLKENREG0, 29),
	SG2042_GATE_HW(GATE_CLK_TOP_AXI0, "clk_gate_top_axi0", clk_div_top_axi0,
		       CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
		       R_CLKENREG1, 11),
	/* downstream of DIV clocks which are sourced from clk_div_top_axi0 */
	SG2042_GATE_HW(GATE_CLK_GPIO_DB, "clk_gate_gpio_db", clk_div_gpio_db,
		       CLK_SET_RATE_PARENT, R_CLKENREG0, 24),
	SG2042_GATE_HW(GATE_CLK_100K_EMMC, "clk_gate_100k_emmc", clk_div_100k_emmc,
		       CLK_SET_RATE_PARENT, R_CLKENREG1, 4),
	SG2042_GATE_HW(GATE_CLK_100K_SD, "clk_gate_100k_sd", clk_div_100k_sd,
		       CLK_SET_RATE_PARENT, R_CLKENREG1, 7),

	/* downstream of clk_div_top_axi_hsperi */
	SG2042_GATE_HW(GATE_CLK_SYSDMA_AXI,
		       "clk_gate_sysdma_axi", clk_div_top_axi_hsperi,
		       CLK_SET_RATE_PARENT, R_CLKENREG0, 3),
	SG2042_GATE_HW(GATE_CLK_APB_UART,
		       "clk_gate_apb_uart", clk_div_top_axi_hsperi,
		       CLK_SET_RATE_PARENT, R_CLKENREG0, 5),
	SG2042_GATE_HW(GATE_CLK_AXI_DBG_I2C,
		       "clk_gate_axi_dbg_i2c", clk_div_top_axi_hsperi,
		       CLK_SET_RATE_PARENT, R_CLKENREG0, 6),
	SG2042_GATE_HW(GATE_CLK_APB_SPI,
		       "clk_gate_apb_spi", clk_div_top_axi_hsperi,
		       CLK_SET_RATE_PARENT, R_CLKENREG0, 25),
	SG2042_GATE_HW(GATE_CLK_AXI_ETH0,
		       "clk_gate_axi_eth0", clk_div_top_axi_hsperi,
		       CLK_SET_RATE_PARENT, R_CLKENREG0, 31),
	SG2042_GATE_HW(GATE_CLK_AXI_EMMC,
		       "clk_gate_axi_emmc", clk_div_top_axi_hsperi,
		       CLK_SET_RATE_PARENT, R_CLKENREG1, 2),
	SG2042_GATE_HW(GATE_CLK_AXI_SD,
		       "clk_gate_axi_sd", clk_div_top_axi_hsperi,
		       CLK_SET_RATE_PARENT, R_CLKENREG1, 5),
	SG2042_GATE_HW(GATE_CLK_TOP_AXI_HSPERI,
		       "clk_gate_top_axi_hsperi", clk_div_top_axi_hsperi,
		       CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
		       R_CLKENREG1, 12),
};

static DEFINE_SPINLOCK(sg2042_clk_lock);

static int sg2042_clk_register_divs(struct device *dev,
				    struct sg2042_clk_data *clk_data,
				    struct sg2042_divider_clock div_clks[],
				    int num_div_clks)
{
	struct sg2042_divider_clock *div;
	struct clk_hw *hw;
	int i, ret = 0;

	for (i = 0; i < num_div_clks; i++) {
		div = &div_clks[i];

		if (div->div_flags & CLK_DIVIDER_HIWORD_MASK) {
			if (div->width + div->shift > 16) {
				pr_warn("divider value exceeds LOWORD field\n");
				ret = -EINVAL;
				break;
			}
		}

		div->reg = clk_data->iobase + div->offset_ctrl;
		div->lock = &sg2042_clk_lock;

		hw = &div->hw;
		ret = devm_clk_hw_register(dev, hw);
		if (ret) {
			pr_err("failed to register clock %s\n", div->hw.init->name);
			break;
		}

		clk_data->onecell_data.hws[div->id] = hw;
	}

	return ret;
}

static int sg2042_clk_register_gates(struct device *dev,
				     struct sg2042_clk_data *clk_data,
				     const struct sg2042_gate_clock gate_clks[],
				     int num_gate_clks)
{
	const struct sg2042_gate_clock *gate;
	struct clk_hw *hw;
	int i, ret = 0;

	for (i = 0; i < num_gate_clks; i++) {
		gate = &gate_clks[i];
		hw = __devm_clk_hw_register_gate
			(dev,
			 NULL,
			 gate->hw.init->name,
			 NULL,
			 gate->hw.init->parent_hws[0],
			 NULL,
			 gate->hw.init->flags,
			 clk_data->iobase + gate->offset_enable,
			 gate->bit_idx,
			 0,
			 &sg2042_clk_lock);
		if (IS_ERR(hw)) {
			pr_err("failed to register clock %s\n", gate->hw.init->name);
			ret = PTR_ERR(hw);
			break;
		}

		clk_data->onecell_data.hws[gate->id] = hw;

		/* Updated some clocks which take the role of parent */
		switch (gate->id) {
		case GATE_CLK_RP_CPU_NORMAL:
			*clk_gate_rp_cpu_normal = hw;
			break;
		case GATE_CLK_TOP_RP_CMN_DIV2:
			*clk_gate_top_rp_cmn_div2 = hw;
			break;
		}
	}

	return ret;
}

static int sg2042_clk_register_gates_fw(struct device *dev,
					struct sg2042_clk_data *clk_data,
					const struct sg2042_gate_clock gate_clks[],
					int num_gate_clks)
{
	const struct sg2042_gate_clock *gate;
	struct clk_hw *hw;
	int i, ret = 0;

	for (i = 0; i < num_gate_clks; i++) {
		gate = &gate_clks[i];
		hw = devm_clk_hw_register_gate_parent_data
			(dev,
			 gate->hw.init->name,
			 gate->hw.init->parent_data,
			 gate->hw.init->flags,
			 clk_data->iobase + gate->offset_enable,
			 gate->bit_idx,
			 0,
			 &sg2042_clk_lock);
		if (IS_ERR(hw)) {
			pr_err("failed to register clock %s\n", gate->hw.init->name);
			ret = PTR_ERR(hw);
			break;
		}

		clk_data->onecell_data.hws[gate->id] = hw;

		/* Updated some clocks which take the role of parent */
		switch (gate->id) {
		case GATE_CLK_DDR01_DIV0:
			*clk_gate_ddr01_div0 = hw;
			break;
		case GATE_CLK_DDR01_DIV1:
			*clk_gate_ddr01_div1 = hw;
			break;
		case GATE_CLK_DDR23_DIV0:
			*clk_gate_ddr23_div0 = hw;
			break;
		case GATE_CLK_DDR23_DIV1:
			*clk_gate_ddr23_div1 = hw;
			break;
		case GATE_CLK_RP_CPU_NORMAL_DIV0:
			*clk_gate_rp_cpu_normal_div0 = hw;
			break;
		case GATE_CLK_RP_CPU_NORMAL_DIV1:
			*clk_gate_rp_cpu_normal_div1 = hw;
			break;
		case GATE_CLK_AXI_DDR_DIV0:
			*clk_gate_axi_ddr_div0 = hw;
			break;
		case GATE_CLK_AXI_DDR_DIV1:
			*clk_gate_axi_ddr_div1 = hw;
			break;
		}
	}

	return ret;
}

static int sg2042_mux_notifier_cb(struct notifier_block *nb,
				  unsigned long event,
				  void *data)
{
	struct sg2042_mux_clock *mux = to_sg2042_mux_nb(nb);
	const struct clk_ops *ops = &clk_mux_ops;
	struct clk_notifier_data *ndata = data;
	struct clk_hw *hw;
	int ret = 0;

	hw = __clk_get_hw(ndata->clk);

	/* To switch to fpll before changing rate and restore after that */
	if (event == PRE_RATE_CHANGE) {
		mux->original_index = ops->get_parent(hw);

		/*
		 * "1" is the array index of the second parent input source of
		 * mux. For SG2042, it's fpll for all mux clocks.
		 * "0" is the array index of the frist parent input source of
		 * mux, For SG2042, it's mpll.
		 * FIXME, any good idea to avoid magic number?
		 */
		if (mux->original_index == 0)
			ret = ops->set_parent(hw, 1);
	} else if (event == POST_RATE_CHANGE) {
		ret = ops->set_parent(hw, mux->original_index);
	}

	return notifier_from_errno(ret);
}

static int sg2042_clk_register_muxs(struct device *dev,
				    struct sg2042_clk_data *clk_data,
				    struct sg2042_mux_clock mux_clks[],
				    int num_mux_clks)
{
	struct sg2042_mux_clock *mux;
	struct clk_hw *hw;
	int i, ret = 0;

	for (i = 0; i < num_mux_clks; i++) {
		mux = &mux_clks[i];

		hw = __devm_clk_hw_register_mux
			(dev,
			 NULL,
			 mux->hw.init->name,
			 mux->hw.init->num_parents,
			 NULL,
			 mux->hw.init->parent_hws,
			 NULL,
			 mux->hw.init->flags,
			 clk_data->iobase + mux->offset_select,
			 mux->shift,
			 BIT(mux->width) - 1,
			 0,
			 sg2042_mux_table,
			 &sg2042_clk_lock);
		if (IS_ERR(hw)) {
			pr_err("failed to register clock %s\n", mux->hw.init->name);
			ret = PTR_ERR(hw);
			break;
		}

		clk_data->onecell_data.hws[mux->id] = hw;

		/* Updated some clocks which takes the role of parent */
		switch (mux->id) {
		case MUX_CLK_DDR01:
			*clk_mux_ddr01 = hw;
			break;
		case MUX_CLK_DDR23:
			*clk_mux_ddr23 = hw;
			break;
		case MUX_CLK_RP_CPU_NORMAL:
			*clk_mux_rp_cpu_normal = hw;
			break;
		case MUX_CLK_AXI_DDR:
			*clk_mux_axi_ddr = hw;
			break;
		}

		/*
		 * FIXME: Theoretically, we should set parent for the
		 * mux, but seems hardware has done this for us with
		 * default value, so we don't set parent again here.
		 */

		if (!(mux->hw.init->flags & CLK_MUX_READ_ONLY)) {
			mux->clk_nb.notifier_call = sg2042_mux_notifier_cb;
			ret = devm_clk_notifier_register(dev, hw->clk, &mux->clk_nb);
			if (ret) {
				pr_err("failed to register clock notifier for %s\n",
				       mux->hw.init->name);
				break;
			}
		}
	}

	return ret;
}

static int sg2042_init_clkdata(struct platform_device *pdev,
			       int num_clks,
			       struct sg2042_clk_data **pp_clk_data)
{
	struct sg2042_clk_data *clk_data = NULL;

	clk_data = devm_kzalloc(&pdev->dev,
				struct_size(clk_data, onecell_data.hws, num_clks),
				GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->iobase = devm_platform_ioremap_resource(pdev, 0);
	if (WARN_ON(IS_ERR(clk_data->iobase)))
		return PTR_ERR(clk_data->iobase);

	clk_data->onecell_data.num = num_clks;

	*pp_clk_data = clk_data;

	return 0;
}

static int sg2042_clkgen_probe(struct platform_device *pdev)
{
	struct sg2042_clk_data *clk_data = NULL;
	int num_clks;
	int ret;

	num_clks = ARRAY_SIZE(sg2042_div_clks_level_1) +
		   ARRAY_SIZE(sg2042_div_clks_level_2) +
		   ARRAY_SIZE(sg2042_gate_clks_level_1) +
		   ARRAY_SIZE(sg2042_gate_clks_level_2) +
		   ARRAY_SIZE(sg2042_mux_clks);

	ret = sg2042_init_clkdata(pdev, num_clks, &clk_data);
	if (ret)
		goto error_out;

	/* level-1 gates */
	ret = sg2042_clk_register_gates_fw(&pdev->dev, clk_data,
					   sg2042_gate_clks_level_1,
					   ARRAY_SIZE(sg2042_gate_clks_level_1));
	if (ret)
		goto error_out;

	/* level-1 div */
	ret = sg2042_clk_register_divs(&pdev->dev, clk_data, sg2042_div_clks_level_1,
				       ARRAY_SIZE(sg2042_div_clks_level_1));
	if (ret)
		goto error_out;

	/* mux */
	ret = sg2042_clk_register_muxs(&pdev->dev, clk_data, sg2042_mux_clks,
				       ARRAY_SIZE(sg2042_mux_clks));
	if (ret)
		goto error_out;

	/* level 2 div */
	ret = sg2042_clk_register_divs(&pdev->dev, clk_data, sg2042_div_clks_level_2,
				       ARRAY_SIZE(sg2042_div_clks_level_2));
	if (ret)
		goto error_out;

	/* level 2 gate */
	ret = sg2042_clk_register_gates(&pdev->dev, clk_data, sg2042_gate_clks_level_2,
					ARRAY_SIZE(sg2042_gate_clks_level_2));
	if (ret)
		goto error_out;

	return devm_of_clk_add_hw_provider(&pdev->dev,
					   of_clk_hw_onecell_get,
					   &clk_data->onecell_data);

error_out:
	pr_err("%s failed error number %d\n", __func__, ret);
	return ret;
}

static const struct of_device_id sg2042_clkgen_match[] = {
	{ .compatible = "sophgo,sg2042-clkgen" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sg2042_clkgen_match);

static struct platform_driver sg2042_clkgen_driver = {
	.probe = sg2042_clkgen_probe,
	.driver = {
		.name = "clk-sophgo-sg2042-clkgen",
		.of_match_table = sg2042_clkgen_match,
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(sg2042_clkgen_driver);

MODULE_AUTHOR("Chen Wang");
MODULE_DESCRIPTION("Sophgo SG2042 clock generator driver");
MODULE_LICENSE("GPL");
