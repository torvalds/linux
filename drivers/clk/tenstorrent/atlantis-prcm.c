// SPDX-License-Identifier: GPL-2.0-only
/*
 * Tenstorrent Atlantis PRCM Clock Driver
 *
 * Copyright (c) 2026 Tenstorrent
 */

#include <dt-bindings/clock/tenstorrent,atlantis-prcm-rcpu.h>
#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/* RCPU Clock Register Offsets */
#define PLL_RCPU_CFG_REG	0x0000
#define PLL_NOCC_CFG_REG	0x0004
#define NOCC_CLK_CFG_REG	0x0008
#define RCPU_DIV_CFG_REG	0x000C
#define RCPU_BLK_CG_REG		0x0014
#define LSIO_BLK_CG_REG		0x0018
#define PLL_RCPU_EN_REG		0x011C
#define PLL_NOCC_EN_REG		0x0120
#define BUS_CG_REG		0x01FC

/* PLL Bit Definitions */
#define PLL_CFG_EN_BIT		BIT(0)
#define PLL_CFG_BYPASS_BIT	BIT(1)
#define PLL_CFG_REFDIV_MASK	GENMASK(7, 2)
#define PLL_CFG_REFDIV_SHIFT	2
#define PLL_CFG_POSTDIV1_MASK	GENMASK(10, 8)
#define PLL_CFG_POSTDIV1_SHIFT	8
#define PLL_CFG_POSTDIV2_MASK	GENMASK(13, 11)
#define PLL_CFG_POSTDIV2_SHIFT	11
#define PLL_CFG_FBDIV_MASK	GENMASK(25, 14)
#define PLL_CFG_FBDIV_SHIFT	14
#define PLL_CFG_LKDT_BIT	BIT(30)
#define PLL_CFG_LOCK_BIT	BIT(31)
#define PLL_LOCK_TIMEOUT_US	1000
#define PLL_BYPASS_WAIT_US	500

struct atlantis_clk_common {
	int clkid;
	struct regmap *regmap;
	struct clk_hw hw;
};

static inline struct atlantis_clk_common *
hw_to_atlantis_clk_common(struct clk_hw *hw)
{
	return container_of(hw, struct atlantis_clk_common, hw);
}

struct atlantis_clk_mux_config {
	u8 shift;
	u8 width;
	u32 reg_offset;
};

struct atlantis_clk_mux {
	struct atlantis_clk_common common;
	struct atlantis_clk_mux_config config;
};

struct atlantis_clk_gate_config {
	u32 reg_offset;
	u32 enable;
};

struct atlantis_clk_gate {
	struct atlantis_clk_common common;
	struct atlantis_clk_gate_config config;
};

struct atlantis_clk_divider_config {
	u8 shift;
	u8 width;
	u32 flags;
	u32 reg_offset;
};

struct atlantis_clk_divider {
	struct atlantis_clk_common common;
	struct atlantis_clk_divider_config config;
};

struct atlantis_clk_pll_config {
	u32 tbl_num;
	u32 reg_offset;
	u32 en_reg_offset;
	u32 cg_reg_offset;
	u32 cg_reg_enable;
};

/* Models a PLL with Bypass Functionality and Enable Bit + an optional Gate Clock at it's output */
struct atlantis_clk_pll {
	struct atlantis_clk_common common;
	struct atlantis_clk_pll_config config;
};

struct atlantis_clk_gate_shared_config {
	u32 reg_offset;
	u32 enable;
	unsigned int *share_count;
	spinlock_t *refcount_lock;
};

struct atlantis_clk_gate_shared {
	struct atlantis_clk_common common;
	struct atlantis_clk_gate_shared_config config;
};

struct atlantis_clk_fixed_factor_config {
	unsigned int mult;
	unsigned int div;
};

struct atlantis_clk_fixed_factor {
	struct atlantis_clk_fixed_factor_config config;
	struct atlantis_clk_common common;
};

static inline struct atlantis_clk_mux *hw_to_atlantis_clk_mux(struct clk_hw *hw)
{
	struct atlantis_clk_common *common = hw_to_atlantis_clk_common(hw);

	return container_of(common, struct atlantis_clk_mux, common);
}

static inline struct atlantis_clk_gate *
hw_to_atlantis_clk_gate(struct clk_hw *hw)
{
	struct atlantis_clk_common *common = hw_to_atlantis_clk_common(hw);

	return container_of(common, struct atlantis_clk_gate, common);
}

static inline struct atlantis_clk_divider *
hw_to_atlantis_clk_divider(struct clk_hw *hw)
{
	struct atlantis_clk_common *common = hw_to_atlantis_clk_common(hw);

	return container_of(common, struct atlantis_clk_divider, common);
}

static inline struct atlantis_clk_pll *hw_to_atlantis_pll(struct clk_hw *hw)
{
	struct atlantis_clk_common *common = hw_to_atlantis_clk_common(hw);

	return container_of(common, struct atlantis_clk_pll, common);
}

static inline struct atlantis_clk_gate_shared *
hw_to_atlantis_clk_gate_shared(struct clk_hw *hw)
{
	struct atlantis_clk_common *common = hw_to_atlantis_clk_common(hw);

	return container_of(common, struct atlantis_clk_gate_shared, common);
}

static inline struct atlantis_clk_fixed_factor *
hw_to_atlantis_clk_fixed_factor(struct clk_hw *hw)
{
	struct atlantis_clk_common *common = hw_to_atlantis_clk_common(hw);

	return container_of(common, struct atlantis_clk_fixed_factor, common);
}

static u8 atlantis_clk_mux_get_parent(struct clk_hw *hw)
{
	struct atlantis_clk_mux *mux = hw_to_atlantis_clk_mux(hw);
	u32 val;

	regmap_read(mux->common.regmap, mux->config.reg_offset, &val);
	val >>= mux->config.shift;
	val &= (BIT(mux->config.width) - 1);

	return val;
}

static int atlantis_clk_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct atlantis_clk_mux *mux = hw_to_atlantis_clk_mux(hw);
	u32 val = index;

	return regmap_update_bits(mux->common.regmap, mux->config.reg_offset,
				  (BIT(mux->config.width) - 1) << mux->config.shift,
				  val << mux->config.shift);
}

static int atlantis_clk_mux_determine_rate(struct clk_hw *hw,
					   struct clk_rate_request *req)
{
	return clk_mux_determine_rate_flags(hw, req, hw->init->flags);
}

static const struct clk_ops atlantis_clk_mux_ops = {
	.get_parent = atlantis_clk_mux_get_parent,
	.set_parent = atlantis_clk_mux_set_parent,
	.determine_rate = atlantis_clk_mux_determine_rate,
};

static int atlantis_clk_gate_endisable(struct clk_hw *hw, int enable)
{
	struct atlantis_clk_gate *gate = hw_to_atlantis_clk_gate(hw);

	return regmap_assign_bits(gate->common.regmap,
				  gate->config.reg_offset,
				  gate->config.enable, enable);
}

static int atlantis_clk_gate_enable(struct clk_hw *hw)
{
	return atlantis_clk_gate_endisable(hw, 1);
}

static void atlantis_clk_gate_disable(struct clk_hw *hw)
{
	atlantis_clk_gate_endisable(hw, 0);
}

static int atlantis_clk_gate_is_enabled(struct clk_hw *hw)
{
	struct atlantis_clk_gate *gate = hw_to_atlantis_clk_gate(hw);

	return regmap_test_bits(gate->common.regmap, gate->config.reg_offset, gate->config.enable);
}

static const struct clk_ops atlantis_clk_gate_ops = {
	.enable = atlantis_clk_gate_enable,
	.disable = atlantis_clk_gate_disable,
	.is_enabled = atlantis_clk_gate_is_enabled,
};

static unsigned long atlantis_clk_divider_recalc_rate(struct clk_hw *hw,
						      unsigned long parent_rate)
{
	struct atlantis_clk_divider *divider = hw_to_atlantis_clk_divider(hw);
	u32 val;

	regmap_read(divider->common.regmap, divider->config.reg_offset, &val);

	val >>= divider->config.shift;
	val &= ((1 << (divider->config.width)) - 1);

	return DIV_ROUND_UP_ULL((u64)parent_rate, val + 1);
}

static const struct clk_ops atlantis_clk_divider_ops = {
	.recalc_rate = atlantis_clk_divider_recalc_rate,
};

static unsigned long
atlantis_clk_fixed_factor_recalc_rate(struct clk_hw *hw,
				      unsigned long parent_rate)
{
	struct atlantis_clk_fixed_factor *factor =
		hw_to_atlantis_clk_fixed_factor(hw);
	unsigned long long rate;

	rate = (unsigned long long)parent_rate * factor->config.mult;
	do_div(rate, factor->config.div);

	return (unsigned long)rate;
}

static const struct clk_ops atlantis_clk_fixed_factor_ops = {
	.recalc_rate = atlantis_clk_fixed_factor_recalc_rate,
};

static int atlantis_clk_pll_is_enabled(struct clk_hw *hw)
{
	struct atlantis_clk_pll *pll = hw_to_atlantis_pll(hw);
	u32 val, en_val, cg_val;

	regmap_read(pll->common.regmap, pll->config.reg_offset, &val);
	regmap_read(pll->common.regmap, pll->config.en_reg_offset, &en_val);
	regmap_read(pll->common.regmap, pll->config.cg_reg_offset, &cg_val);

	/* Check if PLL is powered on, locked, not bypassed and Gate clk is enabled */
	return !!(en_val & PLL_CFG_EN_BIT) && !!(val & PLL_CFG_LOCK_BIT) &&
	       (!pll->config.cg_reg_enable || (cg_val & pll->config.cg_reg_enable)) &&
	       !(val & PLL_CFG_BYPASS_BIT);
}

static int atlantis_clk_pll_enable(struct clk_hw *hw)
{
	struct atlantis_clk_pll *pll = hw_to_atlantis_pll(hw);
	u32 val, en_val, cg_val;
	int ret;

	regmap_read(pll->common.regmap, pll->config.reg_offset, &val);
	regmap_read(pll->common.regmap, pll->config.en_reg_offset, &en_val);
	regmap_read(pll->common.regmap, pll->config.cg_reg_offset, &cg_val);

	/* Check if PLL is already enabled, locked, not bypassed and Gate clk is enabled */
	if ((en_val & PLL_CFG_EN_BIT) && (val & PLL_CFG_LOCK_BIT) &&
	    (!pll->config.cg_reg_enable || (cg_val & pll->config.cg_reg_enable)) &&
	    !(val & PLL_CFG_BYPASS_BIT)) {
		return 0;
	}

	/* Step 1: Set bypass mode first */
	regmap_update_bits(pll->common.regmap, pll->config.reg_offset,
			   PLL_CFG_BYPASS_BIT, PLL_CFG_BYPASS_BIT);

	/* Step 2: Enable PLL (clear then set power bit) */
	regmap_update_bits(pll->common.regmap, pll->config.en_reg_offset,
			   PLL_CFG_EN_BIT, 0);

	regmap_update_bits(pll->common.regmap, pll->config.en_reg_offset,
			   PLL_CFG_EN_BIT, PLL_CFG_EN_BIT);

	/* Step 3: Wait for PLL lock */
	ret = regmap_read_poll_timeout(pll->common.regmap,
				       pll->config.reg_offset, val,
				       val & PLL_CFG_LOCK_BIT,
				       PLL_BYPASS_WAIT_US, PLL_LOCK_TIMEOUT_US);
	if (ret) {
		pr_err("PLL failed to lock within timeout\n");
		return ret;
	}

	/* Step 4: Switch from bypass to PLL output */
	regmap_update_bits(pll->common.regmap, pll->config.reg_offset,
			   PLL_CFG_BYPASS_BIT, 0);

	/* Enable Gate clk at PLL Output */
	return regmap_update_bits(pll->common.regmap, pll->config.cg_reg_offset,
				  pll->config.cg_reg_enable,
				  pll->config.cg_reg_enable);
}

static void atlantis_clk_pll_disable(struct clk_hw *hw)
{
	struct atlantis_clk_pll *pll = hw_to_atlantis_pll(hw);

	/* Step 1: Switch to bypass mode before disabling */
	regmap_update_bits(pll->common.regmap, pll->config.reg_offset,
			   PLL_CFG_BYPASS_BIT, PLL_CFG_BYPASS_BIT);
	/* Step 2: Power down PLL */
	regmap_update_bits(pll->common.regmap, pll->config.en_reg_offset,
			   PLL_CFG_EN_BIT, 0);
}

static unsigned long atlantis_clk_pll_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct atlantis_clk_pll *pll = hw_to_atlantis_pll(hw);

	u32 val, refdiv, fbdiv, postdiv1, postdiv2;
	u64 fout;

	regmap_read(pll->common.regmap, pll->config.reg_offset, &val);

	if (val & PLL_CFG_BYPASS_BIT)
		return parent_rate;

	refdiv = FIELD_GET(PLL_CFG_REFDIV_MASK, val);
	fbdiv = FIELD_GET(PLL_CFG_FBDIV_MASK, val);
	postdiv1 = FIELD_GET(PLL_CFG_POSTDIV1_MASK, val);
	postdiv2 = FIELD_GET(PLL_CFG_POSTDIV2_MASK, val);

	if (!refdiv)
		refdiv = 1;
	if (!postdiv1)
		postdiv1 = 1;
	if (!postdiv2)
		postdiv2 = 1;
	if (!fbdiv)
		return 0;

	fout = div64_u64((u64)parent_rate * fbdiv,
			 refdiv * postdiv1 * postdiv2);

	return fout;
}

static const struct clk_ops atlantis_clk_pll_ops = {
	.enable = atlantis_clk_pll_enable,
	.disable = atlantis_clk_pll_disable,
	.recalc_rate = atlantis_clk_pll_recalc_rate,
	.is_enabled = atlantis_clk_pll_is_enabled,
};

static int atlantis_clk_gate_shared_enable(struct clk_hw *hw)
{
	struct atlantis_clk_gate_shared *gate =
		hw_to_atlantis_clk_gate_shared(hw);
	bool need_enable;

	scoped_guard(spinlock_irqsave, gate->config.refcount_lock)
	{
		need_enable = (*gate->config.share_count) == 0;
		if (need_enable) {
			regmap_set_bits(gate->common.regmap,
					gate->config.reg_offset,
					gate->config.enable);

			if (!regmap_test_bits(gate->common.regmap,
					      gate->config.reg_offset,
					      gate->config.enable)) {
				pr_warn("%s: gate enable %d failed to enable\n",
					clk_hw_get_name(hw), gate->config.enable);
				return -EIO;
			}
		}
		(*gate->config.share_count)++;
	}

	return 0;
}

static void atlantis_clk_gate_shared_disable(struct clk_hw *hw)
{
	struct atlantis_clk_gate_shared *gate =
		hw_to_atlantis_clk_gate_shared(hw);

	scoped_guard(spinlock_irqsave, gate->config.refcount_lock)
	{
		if (WARN_ON(*gate->config.share_count == 0))
			return;
		if (--(*gate->config.share_count) > 0)
			return;

		regmap_clear_bits(gate->common.regmap,
				  gate->config.reg_offset,
				  gate->config.enable);
	}
}

static int atlantis_clk_gate_shared_is_enabled(struct clk_hw *hw)
{
	struct atlantis_clk_gate_shared *gate =
		hw_to_atlantis_clk_gate_shared(hw);

	return regmap_test_bits(gate->common.regmap, gate->config.reg_offset, gate->config.enable);
}

static void atlantis_clk_gate_shared_disable_unused(struct clk_hw *hw)
{
	struct atlantis_clk_gate_shared *gate =
		hw_to_atlantis_clk_gate_shared(hw);

	scoped_guard(spinlock_irqsave, gate->config.refcount_lock)
	{
		if (*gate->config.share_count == 0)
			regmap_clear_bits(gate->common.regmap,
					  gate->config.reg_offset,
					  gate->config.enable);
	}
}

static const struct clk_ops atlantis_clk_gate_shared_ops = {
	.enable = atlantis_clk_gate_shared_enable,
	.disable = atlantis_clk_gate_shared_disable,
	.disable_unused = atlantis_clk_gate_shared_disable_unused,
	.is_enabled = atlantis_clk_gate_shared_is_enabled,
};

#define ATLANTIS_PLL_CONFIG(_reg_offset, _en_reg_offset, _cg_reg_offset, \
			    _cg_reg_enable)                              \
	{                                                                \
		.reg_offset = (_reg_offset),                             \
		.en_reg_offset = (_en_reg_offset),                       \
		.cg_reg_offset = (_cg_reg_offset),                       \
		.cg_reg_enable = (_cg_reg_enable),                       \
	}

#define ATLANTIS_PLL_DEFINE(_clkid, _name, _parent, _reg_offset,               \
			    _en_reg_offset, _cg_reg_offset, _cg_reg_enable,    \
			    _flags)                                            \
	static struct atlantis_clk_pll _name = {                               \
		.config = ATLANTIS_PLL_CONFIG(_reg_offset, _en_reg_offset,     \
					      _cg_reg_offset, _cg_reg_enable), \
		.common = { .clkid = _clkid,                                   \
			    .hw.init = CLK_HW_INIT_PARENTS_DATA(               \
				    #_name, _parent, &atlantis_clk_pll_ops,    \
				    _flags) },                                 \
	}
#define ATLANTIS_MUX_CONFIG(_shift, _width, _reg_offset)                    \
	{                                                                   \
		.shift = _shift, .width = _width, .reg_offset = _reg_offset \
	}

#define ATLANTIS_MUX_DEFINE(_clkid, _name, _parents, _reg_offset, _shift,    \
			    _width, _flags)                                  \
	static struct atlantis_clk_mux _name = {                             \
		.config = ATLANTIS_MUX_CONFIG(_shift, _width, _reg_offset),  \
		.common = { .clkid = _clkid,                                 \
			    .hw.init = CLK_HW_INIT_PARENTS_DATA(             \
				    #_name, _parents, &atlantis_clk_mux_ops, \
				    _flags) }                                \
	}

#define ATLANTIS_DIVIDER_CONFIG(_shift, _width, _flags, _reg_offset) \
	{                                                            \
		.shift = _shift, .width = _width, .flags = _flags,   \
		.reg_offset = _reg_offset                            \
	}

#define ATLANTIS_DIVIDER_DEFINE(_clkid, _name, _parent, _reg_offset, _shift, \
				_width, _divflags, _flags)                   \
	static struct atlantis_clk_divider _name = {                         \
		.config = ATLANTIS_DIVIDER_CONFIG(_shift, _width, _divflags, \
						  _reg_offset),              \
		.common = { .clkid = _clkid,                                 \
			    .hw.init = CLK_HW_INIT_HW(                       \
				    #_name, &_parent.common.hw,              \
				    &atlantis_clk_divider_ops, _flags) }     \
	}
#define ATLANTIS_GATE_CONFIG(_enable, _reg_offset)           \
	{                                                    \
		.enable = _enable, .reg_offset = _reg_offset \
	}

#define ATLANTIS_GATE_DEFINE(_clkid, _name, _parent, _reg_offset, _enable, \
			     _flags)                                       \
	static struct atlantis_clk_gate _name = {                          \
		.config = ATLANTIS_GATE_CONFIG(_enable, _reg_offset),      \
		.common = { .clkid = _clkid,                               \
			    .hw.init = CLK_HW_INIT_HW(                     \
				    #_name, &_parent.common.hw,            \
				    &atlantis_clk_gate_ops, _flags) }      \
	}
#define ATLANTIS_GATE_SHARED_CONFIG(_reg_offset, _enable, _share_count)      \
	{                                                                    \
		.reg_offset = _reg_offset, .enable = _enable,                \
		.share_count = _share_count, .refcount_lock = &refcount_lock \
	}
#define ATLANTIS_GATE_SHARED_DEFINE(_clkid, _name, _parent, _reg_offset,     \
				    _enable, _share_count, _flags)           \
	static struct atlantis_clk_gate_shared _name = {                     \
		.config = ATLANTIS_GATE_SHARED_CONFIG(_reg_offset, _enable,  \
						      _share_count),         \
		.common = { .clkid = _clkid,                                 \
			    .hw.init = CLK_HW_INIT_HW(                       \
				    #_name, &_parent.common.hw,              \
				    &atlantis_clk_gate_shared_ops, _flags) } \
	}
#define ATLANTIS_FIXED_FACTOR_DEFINE(_clkid, _name, _parent, _mult, _div,     \
				     _flags)                                  \
	static struct atlantis_clk_fixed_factor _name = {                     \
		.config = { .mult = _mult, .div = _div },                     \
		.common = { .clkid = _clkid,                                  \
			    .hw.init = CLK_HW_INIT_HW(                        \
				    #_name, &_parent.common.hw,               \
				    &atlantis_clk_fixed_factor_ops, _flags) } \
	}

static DEFINE_SPINLOCK(refcount_lock); /* Lock for refcount value accesses */

static const struct regmap_config atlantis_prcm_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xFFFC,
	.cache_type = REGCACHE_NONE,
};

struct atlantis_prcm_data {
	struct clk_hw **hws;
	size_t num;
	const char *reset_name;
};

static const struct clk_parent_data osc_24m_clk[] = {
	{ .index = 0 },
};

ATLANTIS_PLL_DEFINE(CLK_RCPU_PLL, rcpu_pll_clk, osc_24m_clk, PLL_RCPU_CFG_REG,
		    PLL_RCPU_EN_REG, BUS_CG_REG, 0, /* No Gate Clk at Output */
		    CLK_GET_RATE_NOCACHE | CLK_IS_CRITICAL);

static const struct clk_parent_data rcpu_root_parents[] = {
	{ .index = 0 },
	{ .hw = &rcpu_pll_clk.common.hw },
};

ATLANTIS_MUX_DEFINE(CLK_RCPU_ROOT, rcpu_root_clk, rcpu_root_parents,
		    RCPU_DIV_CFG_REG, 0, 1, CLK_SET_RATE_NO_REPARENT);

ATLANTIS_DIVIDER_DEFINE(CLK_RCPU_DIV2, rcpu_div2_clk, rcpu_root_clk,
			RCPU_DIV_CFG_REG, 2, 4, 0, 0);
ATLANTIS_DIVIDER_DEFINE(CLK_RCPU_DIV4, rcpu_div4_clk, rcpu_root_clk,
			RCPU_DIV_CFG_REG, 7, 4, 0, 0);
ATLANTIS_DIVIDER_DEFINE(CLK_RCPU_RTC, rcpu_rtc_clk, rcpu_div4_clk,
			RCPU_DIV_CFG_REG, 12, 6, 0, 0);

ATLANTIS_GATE_DEFINE(CLK_SMNDMA0_ACLK, rcpu_dma0_clk, rcpu_div2_clk,
		     RCPU_BLK_CG_REG, BIT(0), 0);
ATLANTIS_GATE_DEFINE(CLK_SMNDMA1_ACLK, rcpu_dma1_clk, rcpu_div2_clk,
		     RCPU_BLK_CG_REG, BIT(1), 0);
ATLANTIS_GATE_DEFINE(CLK_WDT0_PCLK, sl_wdt0_pclk, rcpu_div4_clk,
		     RCPU_BLK_CG_REG, BIT(2), 0);
ATLANTIS_GATE_DEFINE(CLK_WDT1_PCLK, sl_wdt1_pclk, rcpu_div4_clk,
		     RCPU_BLK_CG_REG, BIT(3), 0);
ATLANTIS_GATE_DEFINE(CLK_TIMER_PCLK, sl_timer_pclk, rcpu_div4_clk,
		     RCPU_BLK_CG_REG, BIT(4), 0);
ATLANTIS_GATE_DEFINE(CLK_PVTC_PCLK, sl_pvtc_pclk, rcpu_div4_clk,
		     RCPU_BLK_CG_REG, BIT(12), 0);
ATLANTIS_GATE_DEFINE(CLK_PMU_PCLK, sl_pmu_pclk, rcpu_div4_clk, RCPU_BLK_CG_REG,
		     BIT(13), 0);
ATLANTIS_GATE_DEFINE(CLK_MAILBOX_HCLK, rcpu_ipc_clk, rcpu_div2_clk,
		     RCPU_BLK_CG_REG, BIT(14), 0);
ATLANTIS_GATE_DEFINE(CLK_SEC_SPACC_HCLK, sec_spacc_hclk, rcpu_div2_clk,
		     RCPU_BLK_CG_REG, BIT(26), 0);
ATLANTIS_GATE_DEFINE(CLK_SEC_OTP_HCLK, sec_otp_hclk, rcpu_div2_clk,
		     RCPU_BLK_CG_REG, BIT(28), 0);
ATLANTIS_GATE_DEFINE(CLK_TRNG_PCLK, sec_trng_pclk, rcpu_div4_clk,
		     RCPU_BLK_CG_REG, BIT(29), 0);
ATLANTIS_GATE_DEFINE(CLK_SEC_CRC_HCLK, sec_crc_hclk, rcpu_div2_clk,
		     RCPU_BLK_CG_REG, BIT(30), 0);

ATLANTIS_FIXED_FACTOR_DEFINE(CLK_SMN_HCLK, rcpu_smn_hclk, rcpu_div2_clk, 1, 1,
			     0);
ATLANTIS_FIXED_FACTOR_DEFINE(CLK_AHB0_HCLK, rcpu_ahb0_hclk, rcpu_div2_clk, 1, 1,
			     0);

ATLANTIS_FIXED_FACTOR_DEFINE(CLK_SMN_PCLK, rcpu_smn_pclk, rcpu_div4_clk, 1, 1,
			     0);

ATLANTIS_FIXED_FACTOR_DEFINE(CLK_SMN_CLK, rcpu_smn_clk, rcpu_root_clk, 1, 1, 0);
ATLANTIS_FIXED_FACTOR_DEFINE(CLK_SCRATCHPAD_CLK, rcpu_scratchpad_aclk,
			     rcpu_root_clk, 1, 1, 0);
ATLANTIS_FIXED_FACTOR_DEFINE(CLK_RCPU_CORE_CLK, rcpu_core_clk, rcpu_root_clk, 1,
			     1, 0);
ATLANTIS_FIXED_FACTOR_DEFINE(CLK_RCPU_ROM_CLK, rcpu_rom_aclk, rcpu_root_clk, 1,
			     1, 0);

static struct atlantis_clk_fixed_factor
	otp_load_clk = { .config = { .mult = 1, .div = 1 },
			 .common = {
				 .clkid = CLK_OTP_LOAD_CLK,
				 .hw.init = CLK_HW_INIT_PARENTS_DATA(
					 "otp_load_clk", osc_24m_clk,
					 &atlantis_clk_fixed_factor_ops,
					 CLK_SET_RATE_NO_REPARENT),
			 } };

ATLANTIS_PLL_DEFINE(CLK_NOC_PLL, nocc_pll_clk, osc_24m_clk, PLL_NOCC_CFG_REG,
		    PLL_NOCC_EN_REG, BUS_CG_REG, BIT(0),
		    CLK_GET_RATE_NOCACHE | CLK_IS_CRITICAL);

static const struct clk_parent_data nocc_mux_parents[] = {
	{ .index = 0 },
	{ .hw = &nocc_pll_clk.common.hw },
};

ATLANTIS_MUX_DEFINE(CLK_NOCC_CLK, nocc_clk, nocc_mux_parents, NOCC_CLK_CFG_REG,
		    0, 1, CLK_SET_RATE_NO_REPARENT);

ATLANTIS_DIVIDER_DEFINE(CLK_NOCC_DIV2, nocc_div2_clk, nocc_clk,
			NOCC_CLK_CFG_REG, 1, 4, 0, 0);
ATLANTIS_DIVIDER_DEFINE(CLK_NOCC_DIV4, nocc_div4_clk, nocc_clk,
			NOCC_CLK_CFG_REG, 5, 4, 0, 0);
ATLANTIS_DIVIDER_DEFINE(CLK_NOCC_RTC, nocc_rtc_clk, nocc_div4_clk,
			NOCC_CLK_CFG_REG, 9, 6, 0, 0);
ATLANTIS_DIVIDER_DEFINE(CLK_NOCC_CAN, nocc_can_clk, nocc_clk, NOCC_CLK_CFG_REG,
			15, 4, 0, 0);

static unsigned int refcnt_qspi;
ATLANTIS_GATE_SHARED_DEFINE(CLK_QSPI_SCLK, lsio_qspi_sclk, nocc_clk,
			    LSIO_BLK_CG_REG, BIT(0), &refcnt_qspi, 0);
ATLANTIS_GATE_SHARED_DEFINE(CLK_QSPI_HCLK, lsio_qspi_hclk, nocc_div2_clk,
			    LSIO_BLK_CG_REG, BIT(0), &refcnt_qspi, 0);
ATLANTIS_GATE_DEFINE(CLK_I2C0_PCLK, lsio_i2c0_pclk, nocc_div4_clk,
		     LSIO_BLK_CG_REG, BIT(1), 0);
ATLANTIS_GATE_DEFINE(CLK_I2C1_PCLK, lsio_i2c1_pclk, nocc_div4_clk,
		     LSIO_BLK_CG_REG, BIT(2), 0);
ATLANTIS_GATE_DEFINE(CLK_I2C2_PCLK, lsio_i2c2_pclk, nocc_div4_clk,
		     LSIO_BLK_CG_REG, BIT(3), 0);
ATLANTIS_GATE_DEFINE(CLK_I2C3_PCLK, lsio_i2c3_pclk, nocc_div4_clk,
		     LSIO_BLK_CG_REG, BIT(4), 0);
ATLANTIS_GATE_DEFINE(CLK_I2C4_PCLK, lsio_i2c4_pclk, nocc_div4_clk,
		     LSIO_BLK_CG_REG, BIT(5), 0);

ATLANTIS_GATE_DEFINE(CLK_UART0_PCLK, lsio_uart0_pclk, nocc_div4_clk,
		     LSIO_BLK_CG_REG, BIT(6), 0);
ATLANTIS_GATE_DEFINE(CLK_UART1_PCLK, lsio_uart1_pclk, nocc_div4_clk,
		     LSIO_BLK_CG_REG, BIT(7), 0);
ATLANTIS_GATE_DEFINE(CLK_UART2_PCLK, lsio_uart2_pclk, nocc_div4_clk,
		     LSIO_BLK_CG_REG, BIT(8), 0);
ATLANTIS_GATE_DEFINE(CLK_UART3_PCLK, lsio_uart3_pclk, nocc_div4_clk,
		     LSIO_BLK_CG_REG, BIT(9), 0);
ATLANTIS_GATE_DEFINE(CLK_UART4_PCLK, lsio_uart4_pclk, nocc_div4_clk,
		     LSIO_BLK_CG_REG, BIT(10), 0);
ATLANTIS_GATE_DEFINE(CLK_SPI0_PCLK, lsio_spi0_pclk, nocc_div4_clk,
		     LSIO_BLK_CG_REG, BIT(11), 0);
ATLANTIS_GATE_DEFINE(CLK_SPI1_PCLK, lsio_spi1_pclk, nocc_div4_clk,
		     LSIO_BLK_CG_REG, BIT(12), 0);
ATLANTIS_GATE_DEFINE(CLK_SPI2_PCLK, lsio_spi2_pclk, nocc_div4_clk,
		     LSIO_BLK_CG_REG, BIT(13), 0);
ATLANTIS_GATE_DEFINE(CLK_SPI3_PCLK, lsio_spi3_pclk, nocc_div4_clk,
		     LSIO_BLK_CG_REG, BIT(14), 0);
ATLANTIS_GATE_DEFINE(CLK_GPIO_PCLK, lsio_gpio_pclk, nocc_div4_clk,
		     LSIO_BLK_CG_REG, BIT(15), 0);

static unsigned int refcnt_can0;
ATLANTIS_GATE_SHARED_DEFINE(CLK_CAN0_HCLK, lsio_can0_hclk, nocc_div2_clk,
			    LSIO_BLK_CG_REG, BIT(17), &refcnt_can0, 0);
ATLANTIS_GATE_SHARED_DEFINE(CLK_CAN0_CLK, lsio_can0_clk, nocc_can_clk,
			    LSIO_BLK_CG_REG, BIT(17), &refcnt_can0, 0);

static unsigned int refcnt_can1;
ATLANTIS_GATE_SHARED_DEFINE(CLK_CAN1_HCLK, lsio_can1_hclk, nocc_div2_clk,
			    LSIO_BLK_CG_REG, BIT(18), &refcnt_can1, 0);
ATLANTIS_GATE_SHARED_DEFINE(CLK_CAN1_CLK, lsio_can1_clk, nocc_can_clk,
			    LSIO_BLK_CG_REG, BIT(18), &refcnt_can1, 0);

ATLANTIS_FIXED_FACTOR_DEFINE(CLK_CAN0_TIMER_CLK, lsio_can0_timer_clk,
				 nocc_rtc_clk, 1, 1, 0);
ATLANTIS_FIXED_FACTOR_DEFINE(CLK_CAN1_TIMER_CLK, lsio_can1_timer_clk,
				 nocc_rtc_clk, 1, 1, 0);

static struct clk_hw *atlantis_rcpu_clks[] = {
	[CLK_RCPU_PLL]		= &rcpu_pll_clk.common.hw,
	[CLK_RCPU_ROOT]		= &rcpu_root_clk.common.hw,
	[CLK_RCPU_DIV2]		= &rcpu_div2_clk.common.hw,
	[CLK_RCPU_DIV4]		= &rcpu_div4_clk.common.hw,
	[CLK_RCPU_RTC]		= &rcpu_rtc_clk.common.hw,
	[CLK_SMNDMA0_ACLK]	= &rcpu_dma0_clk.common.hw,
	[CLK_SMNDMA1_ACLK]	= &rcpu_dma1_clk.common.hw,
	[CLK_WDT0_PCLK]		= &sl_wdt0_pclk.common.hw,
	[CLK_WDT1_PCLK]		= &sl_wdt1_pclk.common.hw,
	[CLK_TIMER_PCLK]	= &sl_timer_pclk.common.hw,
	[CLK_PVTC_PCLK]		= &sl_pvtc_pclk.common.hw,
	[CLK_PMU_PCLK]		= &sl_pmu_pclk.common.hw,
	[CLK_MAILBOX_HCLK]	= &rcpu_ipc_clk.common.hw,
	[CLK_SEC_SPACC_HCLK]	= &sec_spacc_hclk.common.hw,
	[CLK_SEC_OTP_HCLK]	= &sec_otp_hclk.common.hw,
	[CLK_TRNG_PCLK]		= &sec_trng_pclk.common.hw,
	[CLK_SEC_CRC_HCLK]	= &sec_crc_hclk.common.hw,
	[CLK_SMN_HCLK]		= &rcpu_smn_hclk.common.hw,
	[CLK_AHB0_HCLK]		= &rcpu_ahb0_hclk.common.hw,
	[CLK_SMN_PCLK]		= &rcpu_smn_pclk.common.hw,
	[CLK_SMN_CLK]		= &rcpu_smn_clk.common.hw,
	[CLK_SCRATCHPAD_CLK]	= &rcpu_scratchpad_aclk.common.hw,
	[CLK_RCPU_CORE_CLK]	= &rcpu_core_clk.common.hw,
	[CLK_RCPU_ROM_CLK]	= &rcpu_rom_aclk.common.hw,
	[CLK_OTP_LOAD_CLK]	= &otp_load_clk.common.hw,
	[CLK_NOC_PLL]		= &nocc_pll_clk.common.hw,
	[CLK_NOCC_CLK]		= &nocc_clk.common.hw,
	[CLK_NOCC_DIV2]		= &nocc_div2_clk.common.hw,
	[CLK_NOCC_DIV4]		= &nocc_div4_clk.common.hw,
	[CLK_NOCC_RTC]		= &nocc_rtc_clk.common.hw,
	[CLK_NOCC_CAN]		= &nocc_can_clk.common.hw,
	[CLK_QSPI_SCLK]		= &lsio_qspi_sclk.common.hw,
	[CLK_QSPI_HCLK]		= &lsio_qspi_hclk.common.hw,
	[CLK_I2C0_PCLK]		= &lsio_i2c0_pclk.common.hw,
	[CLK_I2C1_PCLK]		= &lsio_i2c1_pclk.common.hw,
	[CLK_I2C2_PCLK]		= &lsio_i2c2_pclk.common.hw,
	[CLK_I2C3_PCLK]		= &lsio_i2c3_pclk.common.hw,
	[CLK_I2C4_PCLK]		= &lsio_i2c4_pclk.common.hw,
	[CLK_UART0_PCLK]	= &lsio_uart0_pclk.common.hw,
	[CLK_UART1_PCLK]	= &lsio_uart1_pclk.common.hw,
	[CLK_UART2_PCLK]	= &lsio_uart2_pclk.common.hw,
	[CLK_UART3_PCLK]	= &lsio_uart3_pclk.common.hw,
	[CLK_UART4_PCLK]	= &lsio_uart4_pclk.common.hw,
	[CLK_SPI0_PCLK]		= &lsio_spi0_pclk.common.hw,
	[CLK_SPI1_PCLK]		= &lsio_spi1_pclk.common.hw,
	[CLK_SPI2_PCLK]		= &lsio_spi2_pclk.common.hw,
	[CLK_SPI3_PCLK]		= &lsio_spi3_pclk.common.hw,
	[CLK_GPIO_PCLK]		= &lsio_gpio_pclk.common.hw,
	[CLK_CAN0_HCLK]		= &lsio_can0_hclk.common.hw,
	[CLK_CAN0_CLK]		= &lsio_can0_clk.common.hw,
	[CLK_CAN1_HCLK]		= &lsio_can1_hclk.common.hw,
	[CLK_CAN1_CLK]		= &lsio_can1_clk.common.hw,
	[CLK_CAN0_TIMER_CLK]	= &lsio_can0_timer_clk.common.hw,
	[CLK_CAN1_TIMER_CLK]	= &lsio_can1_timer_clk.common.hw,
};

static const struct atlantis_prcm_data atlantis_prcm_rcpu_data = {
	.hws = atlantis_rcpu_clks,
	.num = ARRAY_SIZE(atlantis_rcpu_clks),
	.reset_name = "rcpu-reset"
};

static int atlantis_prcm_clocks_register(struct device *dev,
					 struct regmap *regmap,
					 const struct atlantis_prcm_data *data)
{
	struct clk_hw_onecell_data *clk_data;
	int i, ret;

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws, data->num),
				GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = data->num;

	for (i = 0; i < data->num; i++) {
		struct clk_hw *hw = data->hws[i];
		struct atlantis_clk_common *common =
			hw_to_atlantis_clk_common(hw);
		common->regmap = regmap;

		ret = devm_clk_hw_register(dev, hw);
		if (ret)
			return ret;

		clk_data->hws[common->clkid] = hw;
	}

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, clk_data);
}

static int atlantis_prcm_probe(struct platform_device *pdev)
{
	const struct atlantis_prcm_data *data;
	struct auxiliary_device *reset_adev;
	struct regmap *regmap;
	void __iomem *base;
	struct device *dev = &pdev->dev;
	int ret;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return dev_err_probe(dev, PTR_ERR(base),
				     "Failed to map registers\n");

	regmap = devm_regmap_init_mmio(dev, base, &atlantis_prcm_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to init regmap\n");

	data = of_device_get_match_data(dev);

	ret = atlantis_prcm_clocks_register(dev, regmap, data);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register clocks\n");

	reset_adev = devm_auxiliary_device_create(dev, data->reset_name, NULL);
	if (!reset_adev)
		return dev_err_probe(dev, -ENODEV, "failed to register resets\n");

	return 0;
}

static const struct of_device_id atlantis_prcm_of_match[] = {
	{
		.compatible = "tenstorrent,atlantis-prcm-rcpu",
		.data = &atlantis_prcm_rcpu_data,
	},
	{}

};
MODULE_DEVICE_TABLE(of, atlantis_prcm_of_match);

static struct platform_driver atlantis_prcm_driver = {
	.probe = atlantis_prcm_probe,
	.driver = {
		.name = "atlantis-prcm",
		.of_match_table = atlantis_prcm_of_match,
	},
};
module_platform_driver(atlantis_prcm_driver);

MODULE_DESCRIPTION("Tenstorrent Atlantis PRCM Clock Controller Driver");
MODULE_AUTHOR("Anirudh Srinivasan <asrinivasan@oss.tenstorrent.com>");
MODULE_LICENSE("GPL");
