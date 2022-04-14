// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016 Rockchip Electronics Co. Ltd.
 * Author: Lin Huang <hl@rock-chips.com>
 */

#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/rockchip/rockchip_sip.h>
#include <linux/slab.h>
#include <soc/rockchip/rockchip_sip.h>
#include <soc/rockchip/scpi.h>
#include <uapi/drm/drm_mode.h>
#ifdef CONFIG_ARM
#include <asm/psci.h>
#endif

#include "clk.h"

#define MHZ		(1000000)

struct rockchip_ddrclk {
	struct clk_hw	hw;
	void __iomem	*reg_base;
	int		mux_offset;
	int		mux_shift;
	int		mux_width;
	int		div_shift;
	int		div_width;
	int		ddr_flag;
};

#define to_rockchip_ddrclk_hw(hw) container_of(hw, struct rockchip_ddrclk, hw)

struct share_params_ddrclk {
	u32 hz;
	u32 lcdc_type;
};

struct rockchip_ddrclk_data {
	void __iomem *params;
	int (*dmcfreq_wait_complete)(void);
};

static struct rockchip_ddrclk_data ddr_data = {NULL, NULL};

void rockchip_set_ddrclk_params(void __iomem *params)
{
	ddr_data.params = params;
}
EXPORT_SYMBOL(rockchip_set_ddrclk_params);

void rockchip_set_ddrclk_dmcfreq_wait_complete(int (*func)(void))
{
	ddr_data.dmcfreq_wait_complete = func;
}
EXPORT_SYMBOL(rockchip_set_ddrclk_dmcfreq_wait_complete);

static int rockchip_ddrclk_sip_set_rate(struct clk_hw *hw, unsigned long drate,
					unsigned long prate)
{
	struct arm_smccc_res res;

	arm_smccc_smc(ROCKCHIP_SIP_DRAM_FREQ, drate, 0,
		      ROCKCHIP_SIP_CONFIG_DRAM_SET_RATE,
		      0, 0, 0, 0, &res);

	if (res.a0)
		return 0;
	else
		return -EPERM;
}

static unsigned long
rockchip_ddrclk_sip_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct arm_smccc_res res;

	arm_smccc_smc(ROCKCHIP_SIP_DRAM_FREQ, 0, 0,
		      ROCKCHIP_SIP_CONFIG_DRAM_GET_RATE,
		      0, 0, 0, 0, &res);

	return res.a0;
}

static long rockchip_ddrclk_sip_round_rate(struct clk_hw *hw,
					   unsigned long rate,
					   unsigned long *prate)
{
	struct arm_smccc_res res;

	arm_smccc_smc(ROCKCHIP_SIP_DRAM_FREQ, rate, 0,
		      ROCKCHIP_SIP_CONFIG_DRAM_ROUND_RATE,
		      0, 0, 0, 0, &res);

	return res.a0;
}

static u8 rockchip_ddrclk_get_parent(struct clk_hw *hw)
{
	struct rockchip_ddrclk *ddrclk = to_rockchip_ddrclk_hw(hw);
	u32 val;

	val = readl(ddrclk->reg_base +
			ddrclk->mux_offset) >> ddrclk->mux_shift;
	val &= GENMASK(ddrclk->mux_width - 1, 0);

	return val;
}

static const struct clk_ops rockchip_ddrclk_sip_ops = {
	.recalc_rate = rockchip_ddrclk_sip_recalc_rate,
	.set_rate = rockchip_ddrclk_sip_set_rate,
	.round_rate = rockchip_ddrclk_sip_round_rate,
	.get_parent = rockchip_ddrclk_get_parent,
};

static u32 ddr_clk_cached;

static int rockchip_ddrclk_scpi_set_rate(struct clk_hw *hw, unsigned long drate,
					 unsigned long prate)
{
	u32 ret;
	u32 lcdc_type = 0;
	struct share_params_ddrclk *p;

	p = (struct share_params_ddrclk *)ddr_data.params;
	if (p)
		lcdc_type = p->lcdc_type;

	ret = scpi_ddr_set_clk_rate(drate / MHZ, lcdc_type);
	if (ret) {
		ddr_clk_cached = ret;
		ret = 0;
	} else {
		ddr_clk_cached = 0;
		ret = -1;
	}

	return ret;
}

static unsigned long rockchip_ddrclk_scpi_recalc_rate(struct clk_hw *hw,
						      unsigned long parent_rate)
{
	if (ddr_clk_cached)
		return (MHZ * ddr_clk_cached);
	else
		return (MHZ * scpi_ddr_get_clk_rate());
}

static long rockchip_ddrclk_scpi_round_rate(struct clk_hw *hw,
					    unsigned long rate,
					    unsigned long *prate)
{
	rate = rate / MHZ;
	rate = (rate / 12) * 12;

	return (rate * MHZ);
}

static const struct clk_ops rockchip_ddrclk_scpi_ops __maybe_unused = {
	.recalc_rate = rockchip_ddrclk_scpi_recalc_rate,
	.set_rate = rockchip_ddrclk_scpi_set_rate,
	.round_rate = rockchip_ddrclk_scpi_round_rate,
	.get_parent = rockchip_ddrclk_get_parent,
};

static int rockchip_ddrclk_sip_set_rate_v2(struct clk_hw *hw,
					   unsigned long drate,
					   unsigned long prate)
{
	struct share_params_ddrclk *p;
	struct arm_smccc_res res;

	p = (struct share_params_ddrclk *)ddr_data.params;
	if (p)
		p->hz = drate;

	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_SET_RATE);

	if ((int)res.a1 == SIP_RET_SET_RATE_TIMEOUT) {
		if (ddr_data.dmcfreq_wait_complete)
			ddr_data.dmcfreq_wait_complete();
	}

	return res.a0;
}

static unsigned long rockchip_ddrclk_sip_recalc_rate_v2
			(struct clk_hw *hw, unsigned long parent_rate)
{
	struct arm_smccc_res res;

	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_GET_RATE);
	if (!res.a0)
		return res.a1;
	else
		return 0;
}

static long rockchip_ddrclk_sip_round_rate_v2(struct clk_hw *hw,
					      unsigned long rate,
					      unsigned long *prate)
{
	struct share_params_ddrclk *p;
	struct arm_smccc_res res;

	p = (struct share_params_ddrclk *)ddr_data.params;
	if (p)
		p->hz = rate;

	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_ROUND_RATE);
	if (!res.a0)
		return res.a1;
	else
		return 0;
}

static const struct clk_ops rockchip_ddrclk_sip_ops_v2 = {
	.recalc_rate = rockchip_ddrclk_sip_recalc_rate_v2,
	.set_rate = rockchip_ddrclk_sip_set_rate_v2,
	.round_rate = rockchip_ddrclk_sip_round_rate_v2,
	.get_parent = rockchip_ddrclk_get_parent,
};

struct clk *rockchip_clk_register_ddrclk(const char *name, int flags,
					 const char *const *parent_names,
					 u8 num_parents, int mux_offset,
					 int mux_shift, int mux_width,
					 int div_shift, int div_width,
					 int ddr_flag, void __iomem *reg_base)
{
	struct rockchip_ddrclk *ddrclk;
	struct clk_init_data init;
	struct clk *clk;

#ifdef CONFIG_ARM
	if (!psci_smp_available())
		return NULL;
#endif

	ddrclk = kzalloc(sizeof(*ddrclk), GFP_KERNEL);
	if (!ddrclk)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	init.flags = flags;
	init.flags |= CLK_SET_RATE_NO_REPARENT;

	switch (ddr_flag) {
#ifdef CONFIG_ROCKCHIP_DDRCLK_SIP
	case ROCKCHIP_DDRCLK_SIP:
		init.ops = &rockchip_ddrclk_sip_ops;
		break;
#endif
#ifdef CONFIG_ROCKCHIP_DDRCLK_SCPI
	case ROCKCHIP_DDRCLK_SCPI:
		init.ops = &rockchip_ddrclk_scpi_ops;
		break;
#endif
#ifdef CONFIG_ROCKCHIP_DDRCLK_SIP_V2
	case ROCKCHIP_DDRCLK_SIP_V2:
		init.ops = &rockchip_ddrclk_sip_ops_v2;
		break;
#endif
	default:
		pr_err("%s: unsupported ddrclk type %d\n", __func__, ddr_flag);
		kfree(ddrclk);
		return ERR_PTR(-EINVAL);
	}

	ddrclk->reg_base = reg_base;
	ddrclk->hw.init = &init;
	ddrclk->mux_offset = mux_offset;
	ddrclk->mux_shift = mux_shift;
	ddrclk->mux_width = mux_width;
	ddrclk->div_shift = div_shift;
	ddrclk->div_width = div_width;
	ddrclk->ddr_flag = ddr_flag;

	clk = clk_register(NULL, &ddrclk->hw);
	if (IS_ERR(clk))
		kfree(ddrclk);

	return clk;
}
EXPORT_SYMBOL_GPL(rockchip_clk_register_ddrclk);
