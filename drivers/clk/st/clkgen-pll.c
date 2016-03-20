/*
 * Copyright (C) 2014 STMicroelectronics (R&D) Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

/*
 * Authors:
 * Stephen Gallimore <stephen.gallimore@st.com>,
 * Pankaj Dev <pankaj.dev@st.com>.
 */

#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/iopoll.h>

#include "clkgen.h"

static DEFINE_SPINLOCK(clkgena_c32_odf_lock);
DEFINE_SPINLOCK(clkgen_a9_lock);

/*
 * Common PLL configuration register bits for PLL800 and PLL1600 C65
 */
#define C65_MDIV_PLL800_MASK	(0xff)
#define C65_MDIV_PLL1600_MASK	(0x7)
#define C65_NDIV_MASK		(0xff)
#define C65_PDIV_MASK		(0x7)

/*
 * PLL configuration register bits for PLL3200 C32
 */
#define C32_NDIV_MASK (0xff)
#define C32_IDF_MASK (0x7)
#define C32_ODF_MASK (0x3f)
#define C32_LDF_MASK (0x7f)
#define C32_CP_MASK (0x1f)

#define C32_MAX_ODFS (4)

/*
 * PLL configuration register bits for PLL4600 C28
 */
#define C28_NDIV_MASK (0xff)
#define C28_IDF_MASK (0x7)
#define C28_ODF_MASK (0x3f)

struct clkgen_pll_data {
	struct clkgen_field pdn_status;
	struct clkgen_field pdn_ctrl;
	struct clkgen_field locked_status;
	struct clkgen_field mdiv;
	struct clkgen_field ndiv;
	struct clkgen_field pdiv;
	struct clkgen_field idf;
	struct clkgen_field ldf;
	struct clkgen_field cp;
	unsigned int num_odfs;
	struct clkgen_field odf[C32_MAX_ODFS];
	struct clkgen_field odf_gate[C32_MAX_ODFS];
	bool switch2pll_en;
	struct clkgen_field switch2pll;
	spinlock_t *lock;
	const struct clk_ops *ops;
};

static const struct clk_ops st_pll1600c65_ops;
static const struct clk_ops st_pll800c65_ops;
static const struct clk_ops stm_pll3200c32_ops;
static const struct clk_ops stm_pll3200c32_a9_ops;
static const struct clk_ops st_pll1200c32_ops;
static const struct clk_ops stm_pll4600c28_ops;

static const struct clkgen_pll_data st_pll1600c65_ax = {
	.pdn_status	= CLKGEN_FIELD(0x0, 0x1,			19),
	.pdn_ctrl	= CLKGEN_FIELD(0x10,	0x1,			0),
	.locked_status	= CLKGEN_FIELD(0x0, 0x1,			31),
	.mdiv		= CLKGEN_FIELD(0x0, C65_MDIV_PLL1600_MASK,	0),
	.ndiv		= CLKGEN_FIELD(0x0, C65_NDIV_MASK,		8),
	.ops		= &st_pll1600c65_ops
};

static const struct clkgen_pll_data st_pll800c65_ax = {
	.pdn_status	= CLKGEN_FIELD(0x0,	0x1,			19),
	.pdn_ctrl	= CLKGEN_FIELD(0xC,	0x1,			1),
	.locked_status	= CLKGEN_FIELD(0x0,	0x1,			31),
	.mdiv		= CLKGEN_FIELD(0x0,	C65_MDIV_PLL800_MASK,	0),
	.ndiv		= CLKGEN_FIELD(0x0,	C65_NDIV_MASK,		8),
	.pdiv		= CLKGEN_FIELD(0x0,	C65_PDIV_MASK,		16),
	.ops		= &st_pll800c65_ops
};

static const struct clkgen_pll_data st_pll3200c32_a1x_0 = {
	.pdn_status	= CLKGEN_FIELD(0x0,	0x1,			31),
	.pdn_ctrl	= CLKGEN_FIELD(0x18,	0x1,			0),
	.locked_status	= CLKGEN_FIELD(0x4,	0x1,			31),
	.ndiv		= CLKGEN_FIELD(0x0,	C32_NDIV_MASK,		0x0),
	.idf		= CLKGEN_FIELD(0x4,	C32_IDF_MASK,		0x0),
	.num_odfs = 4,
	.odf =	{	CLKGEN_FIELD(0x54,	C32_ODF_MASK,		4),
			CLKGEN_FIELD(0x54,	C32_ODF_MASK,		10),
			CLKGEN_FIELD(0x54,	C32_ODF_MASK,		16),
			CLKGEN_FIELD(0x54,	C32_ODF_MASK,		22) },
	.odf_gate = {	CLKGEN_FIELD(0x54,	0x1,			0),
			CLKGEN_FIELD(0x54,	0x1,			1),
			CLKGEN_FIELD(0x54,	0x1,			2),
			CLKGEN_FIELD(0x54,	0x1,			3) },
	.ops		= &stm_pll3200c32_ops,
};

static const struct clkgen_pll_data st_pll3200c32_a1x_1 = {
	.pdn_status	= CLKGEN_FIELD(0xC,	0x1,			31),
	.pdn_ctrl	= CLKGEN_FIELD(0x18,	0x1,			1),
	.locked_status	= CLKGEN_FIELD(0x10,	0x1,			31),
	.ndiv		= CLKGEN_FIELD(0xC,	C32_NDIV_MASK,		0x0),
	.idf		= CLKGEN_FIELD(0x10,	C32_IDF_MASK,		0x0),
	.num_odfs = 4,
	.odf = {	CLKGEN_FIELD(0x58,	C32_ODF_MASK,		4),
			CLKGEN_FIELD(0x58,	C32_ODF_MASK,		10),
			CLKGEN_FIELD(0x58,	C32_ODF_MASK,		16),
			CLKGEN_FIELD(0x58,	C32_ODF_MASK,		22) },
	.odf_gate = {	CLKGEN_FIELD(0x58,	0x1,			0),
			CLKGEN_FIELD(0x58,	0x1,			1),
			CLKGEN_FIELD(0x58,	0x1,			2),
			CLKGEN_FIELD(0x58,	0x1,			3) },
	.ops		= &stm_pll3200c32_ops,
};

/* 415 specific */
static const struct clkgen_pll_data st_pll3200c32_a9_415 = {
	.pdn_status	= CLKGEN_FIELD(0x0,	0x1,			0),
	.pdn_ctrl	= CLKGEN_FIELD(0x0,	0x1,			0),
	.locked_status	= CLKGEN_FIELD(0x6C,	0x1,			0),
	.ndiv		= CLKGEN_FIELD(0x0,	C32_NDIV_MASK,		9),
	.idf		= CLKGEN_FIELD(0x0,	C32_IDF_MASK,		22),
	.num_odfs = 1,
	.odf =		{ CLKGEN_FIELD(0x0,	C32_ODF_MASK,		3) },
	.odf_gate =	{ CLKGEN_FIELD(0x0,	0x1,			28) },
	.ops		= &stm_pll3200c32_ops,
};

static const struct clkgen_pll_data st_pll3200c32_ddr_415 = {
	.pdn_status	= CLKGEN_FIELD(0x0,	0x1,			0),
	.pdn_ctrl	= CLKGEN_FIELD(0x0,	0x1,			0),
	.locked_status	= CLKGEN_FIELD(0x100,	0x1,			0),
	.ndiv		= CLKGEN_FIELD(0x8,	C32_NDIV_MASK,		0),
	.idf		= CLKGEN_FIELD(0x0,	C32_IDF_MASK,		25),
	.num_odfs = 2,
	.odf		= { CLKGEN_FIELD(0x8,	C32_ODF_MASK,		8),
			    CLKGEN_FIELD(0x8,	C32_ODF_MASK,		14) },
	.odf_gate	= { CLKGEN_FIELD(0x4,	0x1,			28),
			    CLKGEN_FIELD(0x4,	0x1,			29) },
	.ops		= &stm_pll3200c32_ops,
};

static const struct clkgen_pll_data st_pll1200c32_gpu_415 = {
	.pdn_status	= CLKGEN_FIELD(0x4,	0x1,			0),
	.pdn_ctrl	= CLKGEN_FIELD(0x4,	0x1,			0),
	.locked_status	= CLKGEN_FIELD(0x168,	0x1,			0),
	.ldf		= CLKGEN_FIELD(0x0,	C32_LDF_MASK,		3),
	.idf		= CLKGEN_FIELD(0x0,	C32_IDF_MASK,		0),
	.num_odfs = 0,
	.odf		= { CLKGEN_FIELD(0x0,	C32_ODF_MASK,		10) },
	.ops		= &st_pll1200c32_ops,
};

/* 416 specific */
static const struct clkgen_pll_data st_pll3200c32_a9_416 = {
	.pdn_status	= CLKGEN_FIELD(0x0,	0x1,			0),
	.pdn_ctrl	= CLKGEN_FIELD(0x0,	0x1,			0),
	.locked_status	= CLKGEN_FIELD(0x6C,	0x1,			0),
	.ndiv		= CLKGEN_FIELD(0x8,	C32_NDIV_MASK,		0),
	.idf		= CLKGEN_FIELD(0x0,	C32_IDF_MASK,		25),
	.num_odfs = 1,
	.odf		= { CLKGEN_FIELD(0x8,	C32_ODF_MASK,		8) },
	.odf_gate	= { CLKGEN_FIELD(0x4,	0x1,			28) },
	.ops		= &stm_pll3200c32_ops,
};

static const struct clkgen_pll_data st_pll3200c32_ddr_416 = {
	.pdn_status	= CLKGEN_FIELD(0x0,	0x1,			0),
	.pdn_ctrl	= CLKGEN_FIELD(0x0,	0x1,			0),
	.locked_status	= CLKGEN_FIELD(0x10C,	0x1,			0),
	.ndiv		= CLKGEN_FIELD(0x8,	C32_NDIV_MASK,		0),
	.idf		= CLKGEN_FIELD(0x0,	C32_IDF_MASK,		25),
	.num_odfs = 2,
	.odf		= { CLKGEN_FIELD(0x8,	C32_ODF_MASK,		8),
			    CLKGEN_FIELD(0x8,	C32_ODF_MASK,		14) },
	.odf_gate	= { CLKGEN_FIELD(0x4,	0x1,			28),
			    CLKGEN_FIELD(0x4,	0x1,			29) },
	.ops		= &stm_pll3200c32_ops,
};

static const struct clkgen_pll_data st_pll1200c32_gpu_416 = {
	.pdn_status	= CLKGEN_FIELD(0x8E4,	0x1,			3),
	.pdn_ctrl	= CLKGEN_FIELD(0x8E4,	0x1,			3),
	.locked_status	= CLKGEN_FIELD(0x90C,	0x1,			0),
	.ldf		= CLKGEN_FIELD(0x0,	C32_LDF_MASK,		3),
	.idf		= CLKGEN_FIELD(0x0,	C32_IDF_MASK,		0),
	.num_odfs = 0,
	.odf		= { CLKGEN_FIELD(0x0,	C32_ODF_MASK,		10) },
	.ops		= &st_pll1200c32_ops,
};

static const struct clkgen_pll_data st_pll3200c32_407_a0 = {
	/* 407 A0 */
	.pdn_status	= CLKGEN_FIELD(0x2a0,	0x1,			8),
	.pdn_ctrl	= CLKGEN_FIELD(0x2a0,	0x1,			8),
	.locked_status	= CLKGEN_FIELD(0x2a0,	0x1,			24),
	.ndiv		= CLKGEN_FIELD(0x2a4,	C32_NDIV_MASK,		16),
	.idf		= CLKGEN_FIELD(0x2a4,	C32_IDF_MASK,		0x0),
	.num_odfs = 1,
	.odf		= { CLKGEN_FIELD(0x2b4, C32_ODF_MASK,		0) },
	.odf_gate	= { CLKGEN_FIELD(0x2b4,	0x1,			6) },
	.ops		= &stm_pll3200c32_ops,
};

static const struct clkgen_pll_data st_pll3200c32_cx_0 = {
	/* 407 C0 PLL0 */
	.pdn_status	= CLKGEN_FIELD(0x2a0,	0x1,			8),
	.pdn_ctrl	= CLKGEN_FIELD(0x2a0,	0x1,			8),
	.locked_status	= CLKGEN_FIELD(0x2a0,	0x1,			24),
	.ndiv		= CLKGEN_FIELD(0x2a4,	C32_NDIV_MASK,		16),
	.idf		= CLKGEN_FIELD(0x2a4,	C32_IDF_MASK,		0x0),
	.num_odfs = 1,
	.odf		= { CLKGEN_FIELD(0x2b4, C32_ODF_MASK,		0) },
	.odf_gate	= { CLKGEN_FIELD(0x2b4, 0x1,			6) },
	.ops		= &stm_pll3200c32_ops,
};

static const struct clkgen_pll_data st_pll3200c32_cx_1 = {
	/* 407 C0 PLL1 */
	.pdn_status	= CLKGEN_FIELD(0x2c8,	0x1,			8),
	.pdn_ctrl	= CLKGEN_FIELD(0x2c8,	0x1,			8),
	.locked_status	= CLKGEN_FIELD(0x2c8,	0x1,			24),
	.ndiv		= CLKGEN_FIELD(0x2cc,	C32_NDIV_MASK,		16),
	.idf		= CLKGEN_FIELD(0x2cc,	C32_IDF_MASK,		0x0),
	.num_odfs = 1,
	.odf		= { CLKGEN_FIELD(0x2dc, C32_ODF_MASK,		0) },
	.odf_gate	= { CLKGEN_FIELD(0x2dc, 0x1,			6) },
	.ops		= &stm_pll3200c32_ops,
};

static const struct clkgen_pll_data st_pll3200c32_407_a9 = {
	/* 407 A9 */
	.pdn_status	= CLKGEN_FIELD(0x1a8,	0x1,			0),
	.pdn_ctrl	= CLKGEN_FIELD(0x1a8,	0x1,			0),
	.locked_status	= CLKGEN_FIELD(0x87c,	0x1,			0),
	.ndiv		= CLKGEN_FIELD(0x1b0,	C32_NDIV_MASK,		0),
	.idf		= CLKGEN_FIELD(0x1a8,	C32_IDF_MASK,		25),
	.num_odfs = 1,
	.odf		= { CLKGEN_FIELD(0x1b0, C32_ODF_MASK,		8) },
	.odf_gate	= { CLKGEN_FIELD(0x1ac, 0x1,			28) },
	.switch2pll_en	= true,
	.cp		= CLKGEN_FIELD(0x1a8,	C32_CP_MASK,		1),
	.switch2pll	= CLKGEN_FIELD(0x1a4,	0x1,			1),
	.lock = &clkgen_a9_lock,
	.ops		= &stm_pll3200c32_a9_ops,
};

static struct clkgen_pll_data st_pll4600c28_418_a9 = {
	/* 418 A9 */
	.pdn_status	= CLKGEN_FIELD(0x1a8,	0x1,			0),
	.pdn_ctrl	= CLKGEN_FIELD(0x1a8,	0x1,			0),
	.locked_status	= CLKGEN_FIELD(0x87c,	0x1,			0),
	.ndiv		= CLKGEN_FIELD(0x1b0,	C28_NDIV_MASK,		0),
	.idf		= CLKGEN_FIELD(0x1a8,	C28_IDF_MASK,		25),
	.num_odfs = 1,
	.odf		= { CLKGEN_FIELD(0x1b0, C28_ODF_MASK,		8) },
	.odf_gate	= { CLKGEN_FIELD(0x1ac, 0x1,			28) },
	.switch2pll_en	= true,
	.switch2pll	= CLKGEN_FIELD(0x1a4,	0x1,			1),
	.lock		= &clkgen_a9_lock,
	.ops		= &stm_pll4600c28_ops,
};

/**
 * DOC: Clock Generated by PLL, rate set and enabled by bootloader
 *
 * Traits of this clock:
 * prepare - clk_(un)prepare only ensures parent is (un)prepared
 * enable - clk_enable/disable only ensures parent is enabled
 * rate - rate is fixed. No clk_set_rate support
 * parent - fixed parent.  No clk_set_parent support
 */

/**
 * PLL clock that is integrated in the ClockGenA instances on the STiH415
 * and STiH416.
 *
 * @hw: handle between common and hardware-specific interfaces.
 * @type: PLL instance type.
 * @regs_base: base of the PLL configuration register(s).
 *
 */
struct clkgen_pll {
	struct clk_hw		hw;
	struct clkgen_pll_data	*data;
	void __iomem		*regs_base;
	spinlock_t	*lock;

	u32 ndiv;
	u32 idf;
	u32 odf;
	u32 cp;
};

#define to_clkgen_pll(_hw) container_of(_hw, struct clkgen_pll, hw)

struct stm_pll {
	unsigned long mdiv;
	unsigned long ndiv;
	unsigned long pdiv;
	unsigned long odf;
	unsigned long idf;
	unsigned long ldf;
	unsigned long cp;
};

static int clkgen_pll_is_locked(struct clk_hw *hw)
{
	struct clkgen_pll *pll = to_clkgen_pll(hw);
	u32 locked = CLKGEN_READ(pll, locked_status);

	return !!locked;
}

static int clkgen_pll_is_enabled(struct clk_hw *hw)
{
	struct clkgen_pll *pll = to_clkgen_pll(hw);
	u32 poweroff = CLKGEN_READ(pll, pdn_status);
	return !poweroff;
}

static int __clkgen_pll_enable(struct clk_hw *hw)
{
	struct clkgen_pll *pll = to_clkgen_pll(hw);
	void __iomem *base =  pll->regs_base;
	struct clkgen_field *field = &pll->data->locked_status;
	int ret = 0;
	u32 reg;

	if (clkgen_pll_is_enabled(hw))
		return 0;

	CLKGEN_WRITE(pll, pdn_ctrl, 0);

	ret = readl_relaxed_poll_timeout(base + field->offset, reg,
			!!((reg >> field->shift) & field->mask),  0, 10000);

	if (!ret) {
		if (pll->data->switch2pll_en)
			CLKGEN_WRITE(pll, switch2pll, 0);

		pr_debug("%s:%s enabled\n", __clk_get_name(hw->clk), __func__);
	}

	return ret;
}

static int clkgen_pll_enable(struct clk_hw *hw)
{
	struct clkgen_pll *pll = to_clkgen_pll(hw);
	unsigned long flags = 0;
	int ret = 0;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	ret = __clkgen_pll_enable(hw);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	return ret;
}

static void __clkgen_pll_disable(struct clk_hw *hw)
{
	struct clkgen_pll *pll = to_clkgen_pll(hw);

	if (!clkgen_pll_is_enabled(hw))
		return;

	if (pll->data->switch2pll_en)
		CLKGEN_WRITE(pll, switch2pll, 1);

	CLKGEN_WRITE(pll, pdn_ctrl, 1);

	pr_debug("%s:%s disabled\n", __clk_get_name(hw->clk), __func__);
}

static void clkgen_pll_disable(struct clk_hw *hw)
{
	struct clkgen_pll *pll = to_clkgen_pll(hw);
	unsigned long flags = 0;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	__clkgen_pll_disable(hw);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);
}

static unsigned long recalc_stm_pll800c65(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clkgen_pll *pll = to_clkgen_pll(hw);
	unsigned long mdiv, ndiv, pdiv;
	unsigned long rate;
	uint64_t res;

	if (!clkgen_pll_is_enabled(hw) || !clkgen_pll_is_locked(hw))
		return 0;

	pdiv = CLKGEN_READ(pll, pdiv);
	mdiv = CLKGEN_READ(pll, mdiv);
	ndiv = CLKGEN_READ(pll, ndiv);

	if (!mdiv)
		mdiv++; /* mdiv=0 or 1 => MDIV=1 */

	res = (uint64_t)2 * (uint64_t)parent_rate * (uint64_t)ndiv;
	rate = (unsigned long)div64_u64(res, mdiv * (1 << pdiv));

	pr_debug("%s:%s rate %lu\n", clk_hw_get_name(hw), __func__, rate);

	return rate;

}

static unsigned long recalc_stm_pll1600c65(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clkgen_pll *pll = to_clkgen_pll(hw);
	unsigned long mdiv, ndiv;
	unsigned long rate;

	if (!clkgen_pll_is_enabled(hw) || !clkgen_pll_is_locked(hw))
		return 0;

	mdiv = CLKGEN_READ(pll, mdiv);
	ndiv = CLKGEN_READ(pll, ndiv);

	if (!mdiv)
		mdiv = 1;

	/* Note: input is divided by 1000 to avoid overflow */
	rate = ((2 * (parent_rate / 1000) * ndiv) / mdiv) * 1000;

	pr_debug("%s:%s rate %lu\n", clk_hw_get_name(hw), __func__, rate);

	return rate;
}

static int clk_pll3200c32_get_params(unsigned long input, unsigned long output,
			  struct stm_pll *pll)
{
	unsigned long i, n;
	unsigned long deviation = ~0;
	unsigned long new_freq;
	long new_deviation;
	/* Charge pump table: highest ndiv value for cp=6 to 25 */
	static const unsigned char cp_table[] = {
		48, 56, 64, 72, 80, 88, 96, 104, 112, 120,
		128, 136, 144, 152, 160, 168, 176, 184, 192
	};

	/* Output clock range: 800Mhz to 1600Mhz */
	if (output < 800000000 || output > 1600000000)
		return -EINVAL;

	input /= 1000;
	output /= 1000;

	for (i = 1; i <= 7 && deviation; i++) {
		n = i * output / (2 * input);

		/* Checks */
		if (n < 8)
			continue;
		if (n > 200)
			break;

		new_freq = (input * 2 * n) / i;

		new_deviation = abs(new_freq - output);

		if (!new_deviation || new_deviation < deviation) {
			pll->idf  = i;
			pll->ndiv = n;
			deviation = new_deviation;
		}
	}

	if (deviation == ~0) /* No solution found */
		return -EINVAL;

	/* Computing recommended charge pump value */
	for (pll->cp = 6; pll->ndiv > cp_table[pll->cp-6]; (pll->cp)++)
		;

	return 0;
}

static int clk_pll3200c32_get_rate(unsigned long input, struct stm_pll *pll,
			unsigned long *rate)
{
	if (!pll->idf)
		pll->idf = 1;

	*rate = ((2 * (input / 1000) * pll->ndiv) / pll->idf) * 1000;

	return 0;
}

static unsigned long recalc_stm_pll3200c32(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clkgen_pll *pll = to_clkgen_pll(hw);
	unsigned long ndiv, idf;
	unsigned long rate = 0;

	if (!clkgen_pll_is_enabled(hw) || !clkgen_pll_is_locked(hw))
		return 0;

	ndiv = CLKGEN_READ(pll, ndiv);
	idf = CLKGEN_READ(pll, idf);

	if (idf)
		/* Note: input is divided to avoid overflow */
		rate = ((2 * (parent_rate/1000) * ndiv) / idf) * 1000;

	pr_debug("%s:%s rate %lu\n", clk_hw_get_name(hw), __func__, rate);

	return rate;
}

static long round_rate_stm_pll3200c32(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	struct stm_pll params;

	if (!clk_pll3200c32_get_params(*prate, rate, &params))
		clk_pll3200c32_get_rate(*prate, &params, &rate);
	else {
		pr_debug("%s: %s rate %ld Invalid\n", __func__,
			 __clk_get_name(hw->clk), rate);
		return 0;
	}

	pr_debug("%s: %s new rate %ld [ndiv=%u] [idf=%u]\n",
		 __func__, __clk_get_name(hw->clk),
		 rate, (unsigned int)params.ndiv,
		 (unsigned int)params.idf);

	return rate;
}

static int set_rate_stm_pll3200c32(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct clkgen_pll *pll = to_clkgen_pll(hw);
	struct stm_pll params;
	long hwrate = 0;
	unsigned long flags = 0;

	if (!rate || !parent_rate)
		return -EINVAL;

	if (!clk_pll3200c32_get_params(parent_rate, rate, &params))
		clk_pll3200c32_get_rate(parent_rate, &params, &hwrate);

	pr_debug("%s: %s new rate %ld [ndiv=0x%x] [idf=0x%x]\n",
		 __func__, __clk_get_name(hw->clk),
		 hwrate, (unsigned int)params.ndiv,
		 (unsigned int)params.idf);

	if (!hwrate)
		return -EINVAL;

	pll->ndiv = params.ndiv;
	pll->idf = params.idf;
	pll->cp = params.cp;

	__clkgen_pll_disable(hw);

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	CLKGEN_WRITE(pll, ndiv, pll->ndiv);
	CLKGEN_WRITE(pll, idf, pll->idf);
	CLKGEN_WRITE(pll, cp, pll->cp);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	__clkgen_pll_enable(hw);

	return 0;
}

static unsigned long recalc_stm_pll1200c32(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clkgen_pll *pll = to_clkgen_pll(hw);
	unsigned long odf, ldf, idf;
	unsigned long rate;

	if (!clkgen_pll_is_enabled(hw) || !clkgen_pll_is_locked(hw))
		return 0;

	odf = CLKGEN_READ(pll, odf[0]);
	ldf = CLKGEN_READ(pll, ldf);
	idf = CLKGEN_READ(pll, idf);

	if (!idf) /* idf==0 means 1 */
		idf = 1;
	if (!odf) /* odf==0 means 1 */
		odf = 1;

	/* Note: input is divided by 1000 to avoid overflow */
	rate = (((parent_rate / 1000) * ldf) / (odf * idf)) * 1000;

	pr_debug("%s:%s rate %lu\n", clk_hw_get_name(hw), __func__, rate);

	return rate;
}

/* PLL output structure
 * FVCO >> /2 >> FVCOBY2 (no output)
 *                 |> Divider (ODF) >> PHI
 *
 * FVCOby2 output = (input * 2 * NDIV) / IDF (assuming FRAC_CONTROL==L)
 *
 * Rules:
 *   4Mhz <= INFF input <= 350Mhz
 *   4Mhz <= INFIN (INFF / IDF) <= 50Mhz
 *   19.05Mhz <= FVCOby2 output (PHI w ODF=1) <= 3000Mhz
 *   1 <= i (register/dec value for IDF) <= 7
 *   8 <= n (register/dec value for NDIV) <= 246
 */

static int clk_pll4600c28_get_params(unsigned long input, unsigned long output,
			  struct stm_pll *pll)
{

	unsigned long i, infin, n;
	unsigned long deviation = ~0;
	unsigned long new_freq, new_deviation;

	/* Output clock range: 19Mhz to 3000Mhz */
	if (output < 19000000 || output > 3000000000u)
		return -EINVAL;

	/* For better jitter, IDF should be smallest and NDIV must be maximum */
	for (i = 1; i <= 7 && deviation; i++) {
		/* INFIN checks */
		infin = input / i;
		if (infin < 4000000 || infin > 50000000)
			continue;	/* Invalid case */

		n = output / (infin * 2);
		if (n < 8 || n > 246)
			continue;	/* Invalid case */
		if (n < 246)
			n++;	/* To work around 'y' when n=x.y */

		for (; n >= 8 && deviation; n--) {
			new_freq = infin * 2 * n;
			if (new_freq < output)
				break;	/* Optimization: shorting loop */

			new_deviation = new_freq - output;
			if (!new_deviation || new_deviation < deviation) {
				pll->idf  = i;
				pll->ndiv = n;
				deviation = new_deviation;
			}
		}
	}

	if (deviation == ~0) /* No solution found */
		return -EINVAL;

	return 0;
}

static int clk_pll4600c28_get_rate(unsigned long input, struct stm_pll *pll,
			unsigned long *rate)
{
	if (!pll->idf)
		pll->idf = 1;

	*rate = (input / pll->idf) * 2 * pll->ndiv;

	return 0;
}

static unsigned long recalc_stm_pll4600c28(struct clk_hw *hw,
				    unsigned long parent_rate)
{
	struct clkgen_pll *pll = to_clkgen_pll(hw);
	struct stm_pll params;
	unsigned long rate;

	if (!clkgen_pll_is_enabled(hw) || !clkgen_pll_is_locked(hw))
		return 0;

	params.ndiv = CLKGEN_READ(pll, ndiv);
	params.idf = CLKGEN_READ(pll, idf);

	clk_pll4600c28_get_rate(parent_rate, &params, &rate);

	pr_debug("%s:%s rate %lu\n", __clk_get_name(hw->clk), __func__, rate);

	return rate;
}

static long round_rate_stm_pll4600c28(struct clk_hw *hw, unsigned long rate,
				      unsigned long *prate)
{
	struct stm_pll params;

	if (!clk_pll4600c28_get_params(*prate, rate, &params)) {
		clk_pll4600c28_get_rate(*prate, &params, &rate);
	} else {
		pr_debug("%s: %s rate %ld Invalid\n", __func__,
			 __clk_get_name(hw->clk), rate);
		return 0;
	}

	pr_debug("%s: %s new rate %ld [ndiv=%u] [idf=%u]\n",
		 __func__, __clk_get_name(hw->clk),
		 rate, (unsigned int)params.ndiv,
		 (unsigned int)params.idf);

	return rate;
}

static int set_rate_stm_pll4600c28(struct clk_hw *hw, unsigned long rate,
				   unsigned long parent_rate)
{
	struct clkgen_pll *pll = to_clkgen_pll(hw);
	struct stm_pll params;
	long hwrate;
	unsigned long flags = 0;

	if (!rate || !parent_rate)
		return -EINVAL;

	if (!clk_pll4600c28_get_params(parent_rate, rate, &params)) {
		clk_pll4600c28_get_rate(parent_rate, &params, &hwrate);
	} else {
		pr_debug("%s: %s rate %ld Invalid\n", __func__,
			 __clk_get_name(hw->clk), rate);
		return -EINVAL;
	}

	pr_debug("%s: %s new rate %ld [ndiv=0x%x] [idf=0x%x]\n",
		 __func__, __clk_get_name(hw->clk),
		 hwrate, (unsigned int)params.ndiv,
		 (unsigned int)params.idf);

	if (!hwrate)
		return -EINVAL;

	pll->ndiv = params.ndiv;
	pll->idf = params.idf;

	__clkgen_pll_disable(hw);

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	CLKGEN_WRITE(pll, ndiv, pll->ndiv);
	CLKGEN_WRITE(pll, idf, pll->idf);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	__clkgen_pll_enable(hw);

	return 0;
}

static const struct clk_ops st_pll1600c65_ops = {
	.enable		= clkgen_pll_enable,
	.disable	= clkgen_pll_disable,
	.is_enabled	= clkgen_pll_is_enabled,
	.recalc_rate	= recalc_stm_pll1600c65,
};

static const struct clk_ops st_pll800c65_ops = {
	.enable		= clkgen_pll_enable,
	.disable	= clkgen_pll_disable,
	.is_enabled	= clkgen_pll_is_enabled,
	.recalc_rate	= recalc_stm_pll800c65,
};

static const struct clk_ops stm_pll3200c32_ops = {
	.enable		= clkgen_pll_enable,
	.disable	= clkgen_pll_disable,
	.is_enabled	= clkgen_pll_is_enabled,
	.recalc_rate	= recalc_stm_pll3200c32,
};

static const struct clk_ops stm_pll3200c32_a9_ops = {
	.enable		= clkgen_pll_enable,
	.disable	= clkgen_pll_disable,
	.is_enabled	= clkgen_pll_is_enabled,
	.recalc_rate	= recalc_stm_pll3200c32,
	.round_rate	= round_rate_stm_pll3200c32,
	.set_rate	= set_rate_stm_pll3200c32,
};

static const struct clk_ops st_pll1200c32_ops = {
	.enable		= clkgen_pll_enable,
	.disable	= clkgen_pll_disable,
	.is_enabled	= clkgen_pll_is_enabled,
	.recalc_rate	= recalc_stm_pll1200c32,
};

static const struct clk_ops stm_pll4600c28_ops = {
	.enable		= clkgen_pll_enable,
	.disable	= clkgen_pll_disable,
	.is_enabled	= clkgen_pll_is_enabled,
	.recalc_rate	= recalc_stm_pll4600c28,
	.round_rate	= round_rate_stm_pll4600c28,
	.set_rate	= set_rate_stm_pll4600c28,
};

static struct clk * __init clkgen_pll_register(const char *parent_name,
				struct clkgen_pll_data	*pll_data,
				void __iomem *reg,
				const char *clk_name, spinlock_t *lock)
{
	struct clkgen_pll *pll;
	struct clk *clk;
	struct clk_init_data init;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	init.name = clk_name;
	init.ops = pll_data->ops;

	init.flags = CLK_IS_BASIC | CLK_GET_RATE_NOCACHE;
	init.parent_names = &parent_name;
	init.num_parents  = 1;

	pll->data = pll_data;
	pll->regs_base = reg;
	pll->hw.init = &init;
	pll->lock = lock;

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk)) {
		kfree(pll);
		return clk;
	}

	pr_debug("%s: parent %s rate %lu\n",
			__clk_get_name(clk),
			__clk_get_name(clk_get_parent(clk)),
			clk_get_rate(clk));

	return clk;
}

static struct clk * __init clkgen_c65_lsdiv_register(const char *parent_name,
						     const char *clk_name)
{
	struct clk *clk;

	clk = clk_register_fixed_factor(NULL, clk_name, parent_name, 0, 1, 2);
	if (IS_ERR(clk))
		return clk;

	pr_debug("%s: parent %s rate %lu\n",
			__clk_get_name(clk),
			__clk_get_name(clk_get_parent(clk)),
			clk_get_rate(clk));
	return clk;
}

static void __iomem * __init clkgen_get_register_base(
				struct device_node *np)
{
	struct device_node *pnode;
	void __iomem *reg = NULL;

	pnode = of_get_parent(np);
	if (!pnode)
		return NULL;

	reg = of_iomap(pnode, 0);

	of_node_put(pnode);
	return reg;
}

#define CLKGENAx_PLL0_OFFSET 0x0
#define CLKGENAx_PLL1_OFFSET 0x4

static void __init clkgena_c65_pll_setup(struct device_node *np)
{
	const int num_pll_outputs = 3;
	struct clk_onecell_data *clk_data;
	const char *parent_name;
	void __iomem *reg;
	const char *clk_name;

	parent_name = of_clk_get_parent_name(np, 0);
	if (!parent_name)
		return;

	reg = clkgen_get_register_base(np);
	if (!reg)
		return;

	clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		return;

	clk_data->clk_num = num_pll_outputs;
	clk_data->clks = kzalloc(clk_data->clk_num * sizeof(struct clk *),
				 GFP_KERNEL);

	if (!clk_data->clks)
		goto err;

	if (of_property_read_string_index(np, "clock-output-names",
					  0, &clk_name))
		goto err;

	/*
	 * PLL0 HS (high speed) output
	 */
	clk_data->clks[0] = clkgen_pll_register(parent_name,
			(struct clkgen_pll_data *) &st_pll1600c65_ax,
			reg + CLKGENAx_PLL0_OFFSET, clk_name, NULL);

	if (IS_ERR(clk_data->clks[0]))
		goto err;

	if (of_property_read_string_index(np, "clock-output-names",
					  1, &clk_name))
		goto err;

	/*
	 * PLL0 LS (low speed) output, which is a fixed divide by 2 of the
	 * high speed output.
	 */
	clk_data->clks[1] = clkgen_c65_lsdiv_register(__clk_get_name
						      (clk_data->clks[0]),
						      clk_name);

	if (IS_ERR(clk_data->clks[1]))
		goto err;

	if (of_property_read_string_index(np, "clock-output-names",
					  2, &clk_name))
		goto err;

	/*
	 * PLL1 output
	 */
	clk_data->clks[2] = clkgen_pll_register(parent_name,
			(struct clkgen_pll_data *) &st_pll800c65_ax,
			reg + CLKGENAx_PLL1_OFFSET, clk_name, NULL);

	if (IS_ERR(clk_data->clks[2]))
		goto err;

	of_clk_add_provider(np, of_clk_src_onecell_get, clk_data);
	return;

err:
	kfree(clk_data->clks);
	kfree(clk_data);
}
CLK_OF_DECLARE(clkgena_c65_plls,
	       "st,clkgena-plls-c65", clkgena_c65_pll_setup);

static struct clk * __init clkgen_odf_register(const char *parent_name,
					       void __iomem *reg,
					       struct clkgen_pll_data *pll_data,
					       int odf,
					       spinlock_t *odf_lock,
					       const char *odf_name)
{
	struct clk *clk;
	unsigned long flags;
	struct clk_gate *gate;
	struct clk_divider *div;

	flags = CLK_GET_RATE_NOCACHE | CLK_SET_RATE_PARENT;

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	gate->flags = CLK_GATE_SET_TO_DISABLE;
	gate->reg = reg + pll_data->odf_gate[odf].offset;
	gate->bit_idx = pll_data->odf_gate[odf].shift;
	gate->lock = odf_lock;

	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div) {
		kfree(gate);
		return ERR_PTR(-ENOMEM);
	}

	div->flags = CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO;
	div->reg = reg + pll_data->odf[odf].offset;
	div->shift = pll_data->odf[odf].shift;
	div->width = fls(pll_data->odf[odf].mask);
	div->lock = odf_lock;

	clk = clk_register_composite(NULL, odf_name, &parent_name, 1,
				     NULL, NULL,
				     &div->hw, &clk_divider_ops,
				     &gate->hw, &clk_gate_ops,
				     flags);
	if (IS_ERR(clk))
		return clk;

	pr_debug("%s: parent %s rate %lu\n",
			__clk_get_name(clk),
			__clk_get_name(clk_get_parent(clk)),
			clk_get_rate(clk));
	return clk;
}

static const struct of_device_id c32_pll_of_match[] = {
	{
		.compatible = "st,plls-c32-a1x-0",
		.data = &st_pll3200c32_a1x_0,
	},
	{
		.compatible = "st,plls-c32-a1x-1",
		.data = &st_pll3200c32_a1x_1,
	},
	{
		.compatible = "st,stih415-plls-c32-a9",
		.data = &st_pll3200c32_a9_415,
	},
	{
		.compatible = "st,stih415-plls-c32-ddr",
		.data = &st_pll3200c32_ddr_415,
	},
	{
		.compatible = "st,stih416-plls-c32-a9",
		.data = &st_pll3200c32_a9_416,
	},
	{
		.compatible = "st,stih416-plls-c32-ddr",
		.data = &st_pll3200c32_ddr_416,
	},
	{
		.compatible = "st,stih407-plls-c32-a0",
		.data = &st_pll3200c32_407_a0,
	},
	{
		.compatible = "st,plls-c32-cx_0",
		.data = &st_pll3200c32_cx_0,
	},
	{
		.compatible = "st,plls-c32-cx_1",
		.data = &st_pll3200c32_cx_1,
	},
	{
		.compatible = "st,stih407-plls-c32-a9",
		.data = &st_pll3200c32_407_a9,
	},
	{
		.compatible = "st,stih418-plls-c28-a9",
		.data = &st_pll4600c28_418_a9,
	},
	{}
};

static void __init clkgen_c32_pll_setup(struct device_node *np)
{
	const struct of_device_id *match;
	struct clk *clk;
	const char *parent_name, *pll_name;
	void __iomem *pll_base;
	int num_odfs, odf;
	struct clk_onecell_data *clk_data;
	struct clkgen_pll_data	*data;

	match = of_match_node(c32_pll_of_match, np);
	if (!match) {
		pr_err("%s: No matching data\n", __func__);
		return;
	}

	data = (struct clkgen_pll_data *) match->data;

	parent_name = of_clk_get_parent_name(np, 0);
	if (!parent_name)
		return;

	pll_base = clkgen_get_register_base(np);
	if (!pll_base)
		return;

	clk = clkgen_pll_register(parent_name, data, pll_base, np->name,
				  data->lock);
	if (IS_ERR(clk))
		return;

	pll_name = __clk_get_name(clk);

	num_odfs = data->num_odfs;

	clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		return;

	clk_data->clk_num = num_odfs;
	clk_data->clks = kzalloc(clk_data->clk_num * sizeof(struct clk *),
				 GFP_KERNEL);

	if (!clk_data->clks)
		goto err;

	for (odf = 0; odf < num_odfs; odf++) {
		struct clk *clk;
		const char *clk_name;

		if (of_property_read_string_index(np, "clock-output-names",
						  odf, &clk_name))
			return;

		clk = clkgen_odf_register(pll_name, pll_base, data,
				odf, &clkgena_c32_odf_lock, clk_name);
		if (IS_ERR(clk))
			goto err;

		clk_data->clks[odf] = clk;
	}

	of_clk_add_provider(np, of_clk_src_onecell_get, clk_data);
	return;

err:
	kfree(pll_name);
	kfree(clk_data->clks);
	kfree(clk_data);
}
CLK_OF_DECLARE(clkgen_c32_pll, "st,clkgen-plls-c32", clkgen_c32_pll_setup);

static const struct of_device_id c32_gpu_pll_of_match[] = {
	{
		.compatible = "st,stih415-gpu-pll-c32",
		.data = &st_pll1200c32_gpu_415,
	},
	{
		.compatible = "st,stih416-gpu-pll-c32",
		.data = &st_pll1200c32_gpu_416,
	},
	{}
};

static void __init clkgengpu_c32_pll_setup(struct device_node *np)
{
	const struct of_device_id *match;
	struct clk *clk;
	const char *parent_name;
	void __iomem *reg;
	const char *clk_name;
	struct clkgen_pll_data	*data;

	match = of_match_node(c32_gpu_pll_of_match, np);
	if (!match) {
		pr_err("%s: No matching data\n", __func__);
		return;
	}

	data = (struct clkgen_pll_data *)match->data;

	parent_name = of_clk_get_parent_name(np, 0);
	if (!parent_name)
		return;

	reg = clkgen_get_register_base(np);
	if (!reg)
		return;

	if (of_property_read_string_index(np, "clock-output-names",
					  0, &clk_name))
		return;

	/*
	 * PLL 1200MHz output
	 */
	clk = clkgen_pll_register(parent_name, data, reg, clk_name, data->lock);

	if (!IS_ERR(clk))
		of_clk_add_provider(np, of_clk_src_simple_get, clk);

	return;
}
CLK_OF_DECLARE(clkgengpu_c32_pll,
	       "st,clkgengpu-pll-c32", clkgengpu_c32_pll_setup);
