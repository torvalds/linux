// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/G2L Clock Pulse Generator
 *
 * Copyright (C) 2021 Renesas Electronics Corp.
 *
 * Based on renesas-cpg-mssr.c
 *
 * Copyright (C) 2015 Glider bvba
 * Copyright (C) 2013 Ideas On Board SPRL
 * Copyright (C) 2015 Renesas Electronics Corp.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clk/renesas.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_domain.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/units.h>

#include <dt-bindings/clock/renesas-cpg-mssr.h>

#include "rzg2l-cpg.h"

#ifdef DEBUG
#define WARN_DEBUG(x)	WARN_ON(x)
#else
#define WARN_DEBUG(x)	do { } while (0)
#endif

#define GET_SHIFT(val)		((val >> 12) & 0xff)
#define GET_WIDTH(val)		((val >> 8) & 0xf)

#define KDIV(val)		((s16)FIELD_GET(GENMASK(31, 16), val))
#define MDIV(val)		FIELD_GET(GENMASK(15, 6), val)
#define PDIV(val)		FIELD_GET(GENMASK(5, 0), val)
#define SDIV(val)		FIELD_GET(GENMASK(2, 0), val)

#define RZG3S_DIV_P		GENMASK(28, 26)
#define RZG3S_DIV_M		GENMASK(25, 22)
#define RZG3S_DIV_NI		GENMASK(21, 13)
#define RZG3S_DIV_NF		GENMASK(12, 1)

#define CLK_ON_R(reg)		(reg)
#define CLK_MON_R(reg)		(0x180 + (reg))
#define CLK_RST_R(reg)		(reg)
#define CLK_MRST_R(reg)		(0x180 + (reg))

#define GET_REG_OFFSET(val)		((val >> 20) & 0xfff)
#define GET_REG_SAMPLL_CLK1(val)	((val >> 22) & 0xfff)
#define GET_REG_SAMPLL_CLK2(val)	((val >> 12) & 0xfff)

#define CPG_WEN_BIT		BIT(16)

#define MAX_VCLK_FREQ		(148500000)

/**
 * struct clk_hw_data - clock hardware data
 * @hw: clock hw
 * @conf: clock configuration (register offset, shift, width)
 * @sconf: clock status configuration (register offset, shift, width)
 * @priv: CPG private data structure
 */
struct clk_hw_data {
	struct clk_hw hw;
	u32 conf;
	u32 sconf;
	struct rzg2l_cpg_priv *priv;
};

#define to_clk_hw_data(_hw)	container_of(_hw, struct clk_hw_data, hw)

/**
 * struct sd_mux_hw_data - SD MUX clock hardware data
 * @hw_data: clock hw data
 * @mtable: clock mux table
 */
struct sd_mux_hw_data {
	struct clk_hw_data hw_data;
	const u32 *mtable;
};

#define to_sd_mux_hw_data(_hw)	container_of(_hw, struct sd_mux_hw_data, hw_data)

/**
 * struct div_hw_data - divider clock hardware data
 * @hw_data: clock hw data
 * @dtable: pointer to divider table
 * @invalid_rate: invalid rate for divider
 * @max_rate: maximum rate for divider
 * @width: divider width
 */
struct div_hw_data {
	struct clk_hw_data hw_data;
	const struct clk_div_table *dtable;
	unsigned long invalid_rate;
	unsigned long max_rate;
	u32 width;
};

#define to_div_hw_data(_hw)	container_of(_hw, struct div_hw_data, hw_data)

struct rzg2l_pll5_param {
	u32 pl5_fracin;
	u8 pl5_refdiv;
	u8 pl5_intin;
	u8 pl5_postdiv1;
	u8 pl5_postdiv2;
	u8 pl5_spread;
};

struct rzg2l_pll5_mux_dsi_div_param {
	u8 clksrc;
	u8 dsi_div_a;
	u8 dsi_div_b;
};

/**
 * struct rzg2l_cpg_priv - Clock Pulse Generator Private Data
 *
 * @rcdev: Reset controller entity
 * @dev: CPG device
 * @base: CPG register block base address
 * @rmw_lock: protects register accesses
 * @clks: Array containing all Core and Module Clocks
 * @num_core_clks: Number of Core Clocks in clks[]
 * @num_mod_clks: Number of Module Clocks in clks[]
 * @num_resets: Number of Module Resets in info->resets[]
 * @last_dt_core_clk: ID of the last Core Clock exported to DT
 * @info: Pointer to platform data
 * @genpd: PM domain
 * @mux_dsi_div_params: pll5 mux and dsi div parameters
 */
struct rzg2l_cpg_priv {
	struct reset_controller_dev rcdev;
	struct device *dev;
	void __iomem *base;
	spinlock_t rmw_lock;

	struct clk **clks;
	unsigned int num_core_clks;
	unsigned int num_mod_clks;
	unsigned int num_resets;
	unsigned int last_dt_core_clk;

	const struct rzg2l_cpg_info *info;

	struct generic_pm_domain genpd;

	struct rzg2l_pll5_mux_dsi_div_param mux_dsi_div_params;
};

static void rzg2l_cpg_del_clk_provider(void *data)
{
	of_clk_del_provider(data);
}

/* Must be called in atomic context. */
static int rzg2l_cpg_wait_clk_update_done(void __iomem *base, u32 conf)
{
	u32 bitmask = GENMASK(GET_WIDTH(conf) - 1, 0) << GET_SHIFT(conf);
	u32 off = GET_REG_OFFSET(conf);
	u32 val;

	return readl_poll_timeout_atomic(base + off, val, !(val & bitmask), 10, 200);
}

int rzg2l_cpg_sd_clk_mux_notifier(struct notifier_block *nb, unsigned long event,
				  void *data)
{
	struct clk_notifier_data *cnd = data;
	struct clk_hw *hw = __clk_get_hw(cnd->clk);
	struct clk_hw_data *clk_hw_data = to_clk_hw_data(hw);
	struct rzg2l_cpg_priv *priv = clk_hw_data->priv;
	u32 off = GET_REG_OFFSET(clk_hw_data->conf);
	u32 shift = GET_SHIFT(clk_hw_data->conf);
	const u32 clk_src_266 = 3;
	unsigned long flags;
	int ret;

	if (event != PRE_RATE_CHANGE || (cnd->new_rate / MEGA == 266))
		return NOTIFY_DONE;

	spin_lock_irqsave(&priv->rmw_lock, flags);

	/*
	 * As per the HW manual, we should not directly switch from 533 MHz to
	 * 400 MHz and vice versa. To change the setting from 2’b01 (533 MHz)
	 * to 2’b10 (400 MHz) or vice versa, Switch to 2’b11 (266 MHz) first,
	 * and then switch to the target setting (2’b01 (533 MHz) or 2’b10
	 * (400 MHz)).
	 * Setting a value of '0' to the SEL_SDHI0_SET or SEL_SDHI1_SET clock
	 * switching register is prohibited.
	 * The clock mux has 3 input clocks(533 MHz, 400 MHz, and 266 MHz), and
	 * the index to value mapping is done by adding 1 to the index.
	 */

	writel((CPG_WEN_BIT | clk_src_266) << shift, priv->base + off);

	/* Wait for the update done. */
	ret = rzg2l_cpg_wait_clk_update_done(priv->base, clk_hw_data->sconf);

	spin_unlock_irqrestore(&priv->rmw_lock, flags);

	if (ret)
		dev_err(priv->dev, "failed to switch to safe clk source\n");

	return notifier_from_errno(ret);
}

int rzg3s_cpg_div_clk_notifier(struct notifier_block *nb, unsigned long event,
			       void *data)
{
	struct clk_notifier_data *cnd = data;
	struct clk_hw *hw = __clk_get_hw(cnd->clk);
	struct clk_hw_data *clk_hw_data = to_clk_hw_data(hw);
	struct div_hw_data *div_hw_data = to_div_hw_data(clk_hw_data);
	struct rzg2l_cpg_priv *priv = clk_hw_data->priv;
	u32 off = GET_REG_OFFSET(clk_hw_data->conf);
	u32 shift = GET_SHIFT(clk_hw_data->conf);
	unsigned long flags;
	int ret = 0;
	u32 val;

	if (event != PRE_RATE_CHANGE || !div_hw_data->invalid_rate ||
	    div_hw_data->invalid_rate % cnd->new_rate)
		return NOTIFY_DONE;

	spin_lock_irqsave(&priv->rmw_lock, flags);

	val = readl(priv->base + off);
	val >>= shift;
	val &= GENMASK(GET_WIDTH(clk_hw_data->conf) - 1, 0);

	/*
	 * There are different constraints for the user of this notifiers as follows:
	 * 1/ SD div cannot be 1 (val == 0) if parent rate is 800MHz
	 * 2/ OCTA / SPI div cannot be 1 (val == 0) if parent rate is 400MHz
	 * As SD can have only one parent having 800MHz and OCTA div can have
	 * only one parent having 400MHz we took into account the parent rate
	 * at the beginning of function (by checking invalid_rate % new_rate).
	 * Now it is time to check the hardware divider and update it accordingly.
	 */
	if (!val) {
		writel((CPG_WEN_BIT | 1) << shift, priv->base + off);
		/* Wait for the update done. */
		ret = rzg2l_cpg_wait_clk_update_done(priv->base, clk_hw_data->sconf);
	}

	spin_unlock_irqrestore(&priv->rmw_lock, flags);

	if (ret)
		dev_err(priv->dev, "Failed to downgrade the div\n");

	return notifier_from_errno(ret);
}

static int rzg2l_register_notifier(struct clk_hw *hw, const struct cpg_core_clk *core,
				   struct rzg2l_cpg_priv *priv)
{
	struct notifier_block *nb;

	if (!core->notifier)
		return 0;

	nb = devm_kzalloc(priv->dev, sizeof(*nb), GFP_KERNEL);
	if (!nb)
		return -ENOMEM;

	nb->notifier_call = core->notifier;

	return clk_notifier_register(hw->clk, nb);
}

static unsigned long rzg3s_div_clk_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct clk_hw_data *clk_hw_data = to_clk_hw_data(hw);
	struct div_hw_data *div_hw_data = to_div_hw_data(clk_hw_data);
	struct rzg2l_cpg_priv *priv = clk_hw_data->priv;
	u32 val;

	val = readl(priv->base + GET_REG_OFFSET(clk_hw_data->conf));
	val >>= GET_SHIFT(clk_hw_data->conf);
	val &= GENMASK(GET_WIDTH(clk_hw_data->conf) - 1, 0);

	return divider_recalc_rate(hw, parent_rate, val, div_hw_data->dtable,
				   CLK_DIVIDER_ROUND_CLOSEST, div_hw_data->width);
}

static int rzg3s_div_clk_determine_rate(struct clk_hw *hw, struct clk_rate_request *req)
{
	struct clk_hw_data *clk_hw_data = to_clk_hw_data(hw);
	struct div_hw_data *div_hw_data = to_div_hw_data(clk_hw_data);

	if (div_hw_data->max_rate && req->rate > div_hw_data->max_rate)
		req->rate = div_hw_data->max_rate;

	return divider_determine_rate(hw, req, div_hw_data->dtable, div_hw_data->width,
				      CLK_DIVIDER_ROUND_CLOSEST);
}

static int rzg3s_div_clk_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	struct clk_hw_data *clk_hw_data = to_clk_hw_data(hw);
	struct div_hw_data *div_hw_data = to_div_hw_data(clk_hw_data);
	struct rzg2l_cpg_priv *priv = clk_hw_data->priv;
	u32 off = GET_REG_OFFSET(clk_hw_data->conf);
	u32 shift = GET_SHIFT(clk_hw_data->conf);
	unsigned long flags;
	u32 val;
	int ret;

	val = divider_get_val(rate, parent_rate, div_hw_data->dtable, div_hw_data->width,
			      CLK_DIVIDER_ROUND_CLOSEST);

	spin_lock_irqsave(&priv->rmw_lock, flags);
	writel((CPG_WEN_BIT | val) << shift, priv->base + off);
	/* Wait for the update done. */
	ret = rzg2l_cpg_wait_clk_update_done(priv->base, clk_hw_data->sconf);
	spin_unlock_irqrestore(&priv->rmw_lock, flags);

	return ret;
}

static const struct clk_ops rzg3s_div_clk_ops = {
	.recalc_rate = rzg3s_div_clk_recalc_rate,
	.determine_rate = rzg3s_div_clk_determine_rate,
	.set_rate = rzg3s_div_clk_set_rate,
};

static struct clk * __init
rzg3s_cpg_div_clk_register(const struct cpg_core_clk *core, struct clk **clks,
			   void __iomem *base, struct rzg2l_cpg_priv *priv)
{
	struct div_hw_data *div_hw_data;
	struct clk_init_data init = {};
	const struct clk_div_table *clkt;
	struct clk_hw *clk_hw;
	const struct clk *parent;
	const char *parent_name;
	u32 max = 0;
	int ret;

	parent = clks[core->parent & 0xffff];
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	parent_name = __clk_get_name(parent);

	div_hw_data = devm_kzalloc(priv->dev, sizeof(*div_hw_data), GFP_KERNEL);
	if (!div_hw_data)
		return ERR_PTR(-ENOMEM);

	init.name = core->name;
	init.flags = core->flag;
	init.ops = &rzg3s_div_clk_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	/* Get the maximum divider to retrieve div width. */
	for (clkt = core->dtable; clkt->div; clkt++) {
		if (max < clkt->div)
			max = clkt->div;
	}

	div_hw_data->hw_data.priv = priv;
	div_hw_data->hw_data.conf = core->conf;
	div_hw_data->hw_data.sconf = core->sconf;
	div_hw_data->dtable = core->dtable;
	div_hw_data->invalid_rate = core->invalid_rate;
	div_hw_data->max_rate = core->max_rate;
	div_hw_data->width = fls(max) - 1;

	clk_hw = &div_hw_data->hw_data.hw;
	clk_hw->init = &init;

	ret = devm_clk_hw_register(priv->dev, clk_hw);
	if (ret)
		return ERR_PTR(ret);

	ret = rzg2l_register_notifier(clk_hw, core, priv);
	if (ret) {
		dev_err(priv->dev, "Failed to register notifier for %s\n",
			core->name);
		return ERR_PTR(ret);
	}

	return clk_hw->clk;
}

static struct clk * __init
rzg2l_cpg_div_clk_register(const struct cpg_core_clk *core,
			   struct clk **clks,
			   void __iomem *base,
			   struct rzg2l_cpg_priv *priv)
{
	struct device *dev = priv->dev;
	const struct clk *parent;
	const char *parent_name;
	struct clk_hw *clk_hw;

	parent = clks[core->parent & 0xffff];
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	parent_name = __clk_get_name(parent);

	if (core->dtable)
		clk_hw = clk_hw_register_divider_table(dev, core->name,
						       parent_name, 0,
						       base + GET_REG_OFFSET(core->conf),
						       GET_SHIFT(core->conf),
						       GET_WIDTH(core->conf),
						       core->flag,
						       core->dtable,
						       &priv->rmw_lock);
	else
		clk_hw = clk_hw_register_divider(dev, core->name,
						 parent_name, 0,
						 base + GET_REG_OFFSET(core->conf),
						 GET_SHIFT(core->conf),
						 GET_WIDTH(core->conf),
						 core->flag, &priv->rmw_lock);

	if (IS_ERR(clk_hw))
		return ERR_CAST(clk_hw);

	return clk_hw->clk;
}

static struct clk * __init
rzg2l_cpg_mux_clk_register(const struct cpg_core_clk *core,
			   void __iomem *base,
			   struct rzg2l_cpg_priv *priv)
{
	const struct clk_hw *clk_hw;

	clk_hw = devm_clk_hw_register_mux(priv->dev, core->name,
					  core->parent_names, core->num_parents,
					  core->flag,
					  base + GET_REG_OFFSET(core->conf),
					  GET_SHIFT(core->conf),
					  GET_WIDTH(core->conf),
					  core->mux_flags, &priv->rmw_lock);
	if (IS_ERR(clk_hw))
		return ERR_CAST(clk_hw);

	return clk_hw->clk;
}

static int rzg2l_cpg_sd_clk_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_hw_data *clk_hw_data = to_clk_hw_data(hw);
	struct sd_mux_hw_data *sd_mux_hw_data = to_sd_mux_hw_data(clk_hw_data);
	struct rzg2l_cpg_priv *priv = clk_hw_data->priv;
	u32 off = GET_REG_OFFSET(clk_hw_data->conf);
	u32 shift = GET_SHIFT(clk_hw_data->conf);
	unsigned long flags;
	u32 val;
	int ret;

	val = clk_mux_index_to_val(sd_mux_hw_data->mtable, CLK_MUX_ROUND_CLOSEST, index);

	spin_lock_irqsave(&priv->rmw_lock, flags);

	writel((CPG_WEN_BIT | val) << shift, priv->base + off);

	/* Wait for the update done. */
	ret = rzg2l_cpg_wait_clk_update_done(priv->base, clk_hw_data->sconf);

	spin_unlock_irqrestore(&priv->rmw_lock, flags);

	if (ret)
		dev_err(priv->dev, "Failed to switch parent\n");

	return ret;
}

static u8 rzg2l_cpg_sd_clk_mux_get_parent(struct clk_hw *hw)
{
	struct clk_hw_data *clk_hw_data = to_clk_hw_data(hw);
	struct sd_mux_hw_data *sd_mux_hw_data = to_sd_mux_hw_data(clk_hw_data);
	struct rzg2l_cpg_priv *priv = clk_hw_data->priv;
	u32 val;

	val = readl(priv->base + GET_REG_OFFSET(clk_hw_data->conf));
	val >>= GET_SHIFT(clk_hw_data->conf);
	val &= GENMASK(GET_WIDTH(clk_hw_data->conf) - 1, 0);

	return clk_mux_val_to_index(hw, sd_mux_hw_data->mtable, CLK_MUX_ROUND_CLOSEST, val);
}

static const struct clk_ops rzg2l_cpg_sd_clk_mux_ops = {
	.determine_rate = __clk_mux_determine_rate_closest,
	.set_parent	= rzg2l_cpg_sd_clk_mux_set_parent,
	.get_parent	= rzg2l_cpg_sd_clk_mux_get_parent,
};

static struct clk * __init
rzg2l_cpg_sd_mux_clk_register(const struct cpg_core_clk *core,
			      void __iomem *base,
			      struct rzg2l_cpg_priv *priv)
{
	struct sd_mux_hw_data *sd_mux_hw_data;
	struct clk_init_data init;
	struct clk_hw *clk_hw;
	int ret;

	sd_mux_hw_data = devm_kzalloc(priv->dev, sizeof(*sd_mux_hw_data), GFP_KERNEL);
	if (!sd_mux_hw_data)
		return ERR_PTR(-ENOMEM);

	sd_mux_hw_data->hw_data.priv = priv;
	sd_mux_hw_data->hw_data.conf = core->conf;
	sd_mux_hw_data->hw_data.sconf = core->sconf;
	sd_mux_hw_data->mtable = core->mtable;

	init.name = core->name;
	init.ops = &rzg2l_cpg_sd_clk_mux_ops;
	init.flags = core->flag;
	init.num_parents = core->num_parents;
	init.parent_names = core->parent_names;

	clk_hw = &sd_mux_hw_data->hw_data.hw;
	clk_hw->init = &init;

	ret = devm_clk_hw_register(priv->dev, clk_hw);
	if (ret)
		return ERR_PTR(ret);

	ret = rzg2l_register_notifier(clk_hw, core, priv);
	if (ret) {
		dev_err(priv->dev, "Failed to register notifier for %s\n",
			core->name);
		return ERR_PTR(ret);
	}

	return clk_hw->clk;
}

static unsigned long
rzg2l_cpg_get_foutpostdiv_rate(struct rzg2l_pll5_param *params,
			       unsigned long rate)
{
	unsigned long foutpostdiv_rate;

	params->pl5_intin = rate / MEGA;
	params->pl5_fracin = div_u64(((u64)rate % MEGA) << 24, MEGA);
	params->pl5_refdiv = 2;
	params->pl5_postdiv1 = 1;
	params->pl5_postdiv2 = 1;
	params->pl5_spread = 0x16;

	foutpostdiv_rate =
		EXTAL_FREQ_IN_MEGA_HZ * MEGA / params->pl5_refdiv *
		((((params->pl5_intin << 24) + params->pl5_fracin)) >> 24) /
		(params->pl5_postdiv1 * params->pl5_postdiv2);

	return foutpostdiv_rate;
}

struct dsi_div_hw_data {
	struct clk_hw hw;
	u32 conf;
	unsigned long rate;
	struct rzg2l_cpg_priv *priv;
};

#define to_dsi_div_hw_data(_hw)	container_of(_hw, struct dsi_div_hw_data, hw)

static unsigned long rzg2l_cpg_dsi_div_recalc_rate(struct clk_hw *hw,
						   unsigned long parent_rate)
{
	struct dsi_div_hw_data *dsi_div = to_dsi_div_hw_data(hw);
	unsigned long rate = dsi_div->rate;

	if (!rate)
		rate = parent_rate;

	return rate;
}

static unsigned long rzg2l_cpg_get_vclk_parent_rate(struct clk_hw *hw,
						    unsigned long rate)
{
	struct dsi_div_hw_data *dsi_div = to_dsi_div_hw_data(hw);
	struct rzg2l_cpg_priv *priv = dsi_div->priv;
	struct rzg2l_pll5_param params;
	unsigned long parent_rate;

	parent_rate = rzg2l_cpg_get_foutpostdiv_rate(&params, rate);

	if (priv->mux_dsi_div_params.clksrc)
		parent_rate /= 2;

	return parent_rate;
}

static int rzg2l_cpg_dsi_div_determine_rate(struct clk_hw *hw,
					    struct clk_rate_request *req)
{
	if (req->rate > MAX_VCLK_FREQ)
		req->rate = MAX_VCLK_FREQ;

	req->best_parent_rate = rzg2l_cpg_get_vclk_parent_rate(hw, req->rate);

	return 0;
}

static int rzg2l_cpg_dsi_div_set_rate(struct clk_hw *hw,
				      unsigned long rate,
				      unsigned long parent_rate)
{
	struct dsi_div_hw_data *dsi_div = to_dsi_div_hw_data(hw);
	struct rzg2l_cpg_priv *priv = dsi_div->priv;

	/*
	 * MUX -->DIV_DSI_{A,B} -->M3 -->VCLK
	 *
	 * Based on the dot clock, the DSI divider clock sets the divider value,
	 * calculates the pll parameters for generating FOUTPOSTDIV and the clk
	 * source for the MUX and propagates that info to the parents.
	 */

	if (!rate || rate > MAX_VCLK_FREQ)
		return -EINVAL;

	dsi_div->rate = rate;
	writel(CPG_PL5_SDIV_DIV_DSI_A_WEN | CPG_PL5_SDIV_DIV_DSI_B_WEN |
	       (priv->mux_dsi_div_params.dsi_div_a << 0) |
	       (priv->mux_dsi_div_params.dsi_div_b << 8),
	       priv->base + CPG_PL5_SDIV);

	return 0;
}

static const struct clk_ops rzg2l_cpg_dsi_div_ops = {
	.recalc_rate = rzg2l_cpg_dsi_div_recalc_rate,
	.determine_rate = rzg2l_cpg_dsi_div_determine_rate,
	.set_rate = rzg2l_cpg_dsi_div_set_rate,
};

static struct clk * __init
rzg2l_cpg_dsi_div_clk_register(const struct cpg_core_clk *core,
			       struct clk **clks,
			       struct rzg2l_cpg_priv *priv)
{
	struct dsi_div_hw_data *clk_hw_data;
	const struct clk *parent;
	const char *parent_name;
	struct clk_init_data init;
	struct clk_hw *clk_hw;
	int ret;

	parent = clks[core->parent & 0xffff];
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	clk_hw_data = devm_kzalloc(priv->dev, sizeof(*clk_hw_data), GFP_KERNEL);
	if (!clk_hw_data)
		return ERR_PTR(-ENOMEM);

	clk_hw_data->priv = priv;

	parent_name = __clk_get_name(parent);
	init.name = core->name;
	init.ops = &rzg2l_cpg_dsi_div_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	clk_hw = &clk_hw_data->hw;
	clk_hw->init = &init;

	ret = devm_clk_hw_register(priv->dev, clk_hw);
	if (ret)
		return ERR_PTR(ret);

	return clk_hw->clk;
}

struct pll5_mux_hw_data {
	struct clk_hw hw;
	u32 conf;
	unsigned long rate;
	struct rzg2l_cpg_priv *priv;
};

#define to_pll5_mux_hw_data(_hw)	container_of(_hw, struct pll5_mux_hw_data, hw)

static int rzg2l_cpg_pll5_4_clk_mux_determine_rate(struct clk_hw *hw,
						   struct clk_rate_request *req)
{
	struct clk_hw *parent;
	struct pll5_mux_hw_data *hwdata = to_pll5_mux_hw_data(hw);
	struct rzg2l_cpg_priv *priv = hwdata->priv;

	parent = clk_hw_get_parent_by_index(hw, priv->mux_dsi_div_params.clksrc);
	req->best_parent_hw = parent;
	req->best_parent_rate = req->rate;

	return 0;
}

static int rzg2l_cpg_pll5_4_clk_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct pll5_mux_hw_data *hwdata = to_pll5_mux_hw_data(hw);
	struct rzg2l_cpg_priv *priv = hwdata->priv;

	/*
	 * FOUTPOSTDIV--->|
	 *  |             | -->MUX -->DIV_DSIA_B -->M3 -->VCLK
	 *  |--FOUT1PH0-->|
	 *
	 * Based on the dot clock, the DSI divider clock calculates the parent
	 * rate and clk source for the MUX. It propagates that info to
	 * pll5_4_clk_mux which sets the clock source for DSI divider clock.
	 */

	writel(CPG_OTHERFUNC1_REG_RES0_ON_WEN | index,
	       priv->base + CPG_OTHERFUNC1_REG);

	return 0;
}

static u8 rzg2l_cpg_pll5_4_clk_mux_get_parent(struct clk_hw *hw)
{
	struct pll5_mux_hw_data *hwdata = to_pll5_mux_hw_data(hw);
	struct rzg2l_cpg_priv *priv = hwdata->priv;

	return readl(priv->base + GET_REG_OFFSET(hwdata->conf));
}

static const struct clk_ops rzg2l_cpg_pll5_4_clk_mux_ops = {
	.determine_rate = rzg2l_cpg_pll5_4_clk_mux_determine_rate,
	.set_parent	= rzg2l_cpg_pll5_4_clk_mux_set_parent,
	.get_parent	= rzg2l_cpg_pll5_4_clk_mux_get_parent,
};

static struct clk * __init
rzg2l_cpg_pll5_4_mux_clk_register(const struct cpg_core_clk *core,
				  struct rzg2l_cpg_priv *priv)
{
	struct pll5_mux_hw_data *clk_hw_data;
	struct clk_init_data init;
	struct clk_hw *clk_hw;
	int ret;

	clk_hw_data = devm_kzalloc(priv->dev, sizeof(*clk_hw_data), GFP_KERNEL);
	if (!clk_hw_data)
		return ERR_PTR(-ENOMEM);

	clk_hw_data->priv = priv;
	clk_hw_data->conf = core->conf;

	init.name = core->name;
	init.ops = &rzg2l_cpg_pll5_4_clk_mux_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.num_parents = core->num_parents;
	init.parent_names = core->parent_names;

	clk_hw = &clk_hw_data->hw;
	clk_hw->init = &init;

	ret = devm_clk_hw_register(priv->dev, clk_hw);
	if (ret)
		return ERR_PTR(ret);

	return clk_hw->clk;
}

struct sipll5 {
	struct clk_hw hw;
	u32 conf;
	unsigned long foutpostdiv_rate;
	struct rzg2l_cpg_priv *priv;
};

#define to_sipll5(_hw)	container_of(_hw, struct sipll5, hw)

static unsigned long rzg2l_cpg_get_vclk_rate(struct clk_hw *hw,
					     unsigned long rate)
{
	struct sipll5 *sipll5 = to_sipll5(hw);
	struct rzg2l_cpg_priv *priv = sipll5->priv;
	unsigned long vclk;

	vclk = rate / ((1 << priv->mux_dsi_div_params.dsi_div_a) *
		       (priv->mux_dsi_div_params.dsi_div_b + 1));

	if (priv->mux_dsi_div_params.clksrc)
		vclk /= 2;

	return vclk;
}

static unsigned long rzg2l_cpg_sipll5_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct sipll5 *sipll5 = to_sipll5(hw);
	unsigned long pll5_rate = sipll5->foutpostdiv_rate;

	if (!pll5_rate)
		pll5_rate = parent_rate;

	return pll5_rate;
}

static long rzg2l_cpg_sipll5_round_rate(struct clk_hw *hw,
					unsigned long rate,
					unsigned long *parent_rate)
{
	return rate;
}

static int rzg2l_cpg_sipll5_set_rate(struct clk_hw *hw,
				     unsigned long rate,
				     unsigned long parent_rate)
{
	struct sipll5 *sipll5 = to_sipll5(hw);
	struct rzg2l_cpg_priv *priv = sipll5->priv;
	struct rzg2l_pll5_param params;
	unsigned long vclk_rate;
	int ret;
	u32 val;

	/*
	 *  OSC --> PLL5 --> FOUTPOSTDIV-->|
	 *                   |             | -->MUX -->DIV_DSIA_B -->M3 -->VCLK
	 *                   |--FOUT1PH0-->|
	 *
	 * Based on the dot clock, the DSI divider clock calculates the parent
	 * rate and the pll5 parameters for generating FOUTPOSTDIV. It propagates
	 * that info to sipll5 which sets parameters for generating FOUTPOSTDIV.
	 *
	 * OSC --> PLL5 --> FOUTPOSTDIV
	 */

	if (!rate)
		return -EINVAL;

	vclk_rate = rzg2l_cpg_get_vclk_rate(hw, rate);
	sipll5->foutpostdiv_rate =
		rzg2l_cpg_get_foutpostdiv_rate(&params, vclk_rate);

	/* Put PLL5 into standby mode */
	writel(CPG_SIPLL5_STBY_RESETB_WEN, priv->base + CPG_SIPLL5_STBY);
	ret = readl_poll_timeout(priv->base + CPG_SIPLL5_MON, val,
				 !(val & CPG_SIPLL5_MON_PLL5_LOCK), 100, 250000);
	if (ret) {
		dev_err(priv->dev, "failed to release pll5 lock");
		return ret;
	}

	/* Output clock setting 1 */
	writel((params.pl5_postdiv1 << 0) | (params.pl5_postdiv2 << 4) |
	       (params.pl5_refdiv << 8), priv->base + CPG_SIPLL5_CLK1);

	/* Output clock setting, SSCG modulation value setting 3 */
	writel((params.pl5_fracin << 8), priv->base + CPG_SIPLL5_CLK3);

	/* Output clock setting 4 */
	writel(CPG_SIPLL5_CLK4_RESV_LSB | (params.pl5_intin << 16),
	       priv->base + CPG_SIPLL5_CLK4);

	/* Output clock setting 5 */
	writel(params.pl5_spread, priv->base + CPG_SIPLL5_CLK5);

	/* PLL normal mode setting */
	writel(CPG_SIPLL5_STBY_DOWNSPREAD_WEN | CPG_SIPLL5_STBY_SSCG_EN_WEN |
	       CPG_SIPLL5_STBY_RESETB_WEN | CPG_SIPLL5_STBY_RESETB,
	       priv->base + CPG_SIPLL5_STBY);

	/* PLL normal mode transition, output clock stability check */
	ret = readl_poll_timeout(priv->base + CPG_SIPLL5_MON, val,
				 (val & CPG_SIPLL5_MON_PLL5_LOCK), 100, 250000);
	if (ret) {
		dev_err(priv->dev, "failed to lock pll5");
		return ret;
	}

	return 0;
}

static const struct clk_ops rzg2l_cpg_sipll5_ops = {
	.recalc_rate = rzg2l_cpg_sipll5_recalc_rate,
	.round_rate = rzg2l_cpg_sipll5_round_rate,
	.set_rate = rzg2l_cpg_sipll5_set_rate,
};

static struct clk * __init
rzg2l_cpg_sipll5_register(const struct cpg_core_clk *core,
			  struct clk **clks,
			  struct rzg2l_cpg_priv *priv)
{
	const struct clk *parent;
	struct clk_init_data init;
	const char *parent_name;
	struct sipll5 *sipll5;
	struct clk_hw *clk_hw;
	int ret;

	parent = clks[core->parent & 0xffff];
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	sipll5 = devm_kzalloc(priv->dev, sizeof(*sipll5), GFP_KERNEL);
	if (!sipll5)
		return ERR_PTR(-ENOMEM);

	init.name = core->name;
	parent_name = __clk_get_name(parent);
	init.ops = &rzg2l_cpg_sipll5_ops;
	init.flags = 0;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	sipll5->hw.init = &init;
	sipll5->conf = core->conf;
	sipll5->priv = priv;

	writel(CPG_SIPLL5_STBY_SSCG_EN_WEN | CPG_SIPLL5_STBY_RESETB_WEN |
	       CPG_SIPLL5_STBY_RESETB, priv->base + CPG_SIPLL5_STBY);

	clk_hw = &sipll5->hw;
	clk_hw->init = &init;

	ret = devm_clk_hw_register(priv->dev, clk_hw);
	if (ret)
		return ERR_PTR(ret);

	priv->mux_dsi_div_params.clksrc = 1; /* Use clk src 1 for DSI */
	priv->mux_dsi_div_params.dsi_div_a = 1; /* Divided by 2 */
	priv->mux_dsi_div_params.dsi_div_b = 2; /* Divided by 3 */

	return clk_hw->clk;
}

struct pll_clk {
	struct clk_hw hw;
	unsigned int conf;
	unsigned int type;
	void __iomem *base;
	struct rzg2l_cpg_priv *priv;
};

#define to_pll(_hw)	container_of(_hw, struct pll_clk, hw)

static unsigned long rzg2l_cpg_pll_clk_recalc_rate(struct clk_hw *hw,
						   unsigned long parent_rate)
{
	struct pll_clk *pll_clk = to_pll(hw);
	struct rzg2l_cpg_priv *priv = pll_clk->priv;
	unsigned int val1, val2;
	u64 rate;

	if (pll_clk->type != CLK_TYPE_SAM_PLL)
		return parent_rate;

	val1 = readl(priv->base + GET_REG_SAMPLL_CLK1(pll_clk->conf));
	val2 = readl(priv->base + GET_REG_SAMPLL_CLK2(pll_clk->conf));

	rate = mul_u64_u32_shr(parent_rate, (MDIV(val1) << 16) + KDIV(val1),
			       16 + SDIV(val2));

	return DIV_ROUND_CLOSEST_ULL(rate, PDIV(val1));
}

static const struct clk_ops rzg2l_cpg_pll_ops = {
	.recalc_rate = rzg2l_cpg_pll_clk_recalc_rate,
};

static unsigned long rzg3s_cpg_pll_clk_recalc_rate(struct clk_hw *hw,
						   unsigned long parent_rate)
{
	struct pll_clk *pll_clk = to_pll(hw);
	struct rzg2l_cpg_priv *priv = pll_clk->priv;
	u32 nir, nfr, mr, pr, val;
	u64 rate;

	if (pll_clk->type != CLK_TYPE_G3S_PLL)
		return parent_rate;

	val = readl(priv->base + GET_REG_SAMPLL_CLK1(pll_clk->conf));

	pr = 1 << FIELD_GET(RZG3S_DIV_P, val);
	/* Hardware interprets values higher than 8 as p = 16. */
	if (pr > 8)
		pr = 16;

	mr  = FIELD_GET(RZG3S_DIV_M, val) + 1;
	nir = FIELD_GET(RZG3S_DIV_NI, val) + 1;
	nfr = FIELD_GET(RZG3S_DIV_NF, val);

	rate = mul_u64_u32_shr(parent_rate, 4096 * nir + nfr, 12);

	return DIV_ROUND_CLOSEST_ULL(rate, (mr * pr));
}

static const struct clk_ops rzg3s_cpg_pll_ops = {
	.recalc_rate = rzg3s_cpg_pll_clk_recalc_rate,
};

static struct clk * __init
rzg2l_cpg_pll_clk_register(const struct cpg_core_clk *core,
			   struct clk **clks,
			   void __iomem *base,
			   struct rzg2l_cpg_priv *priv,
			   const struct clk_ops *ops)
{
	struct device *dev = priv->dev;
	const struct clk *parent;
	struct clk_init_data init;
	const char *parent_name;
	struct pll_clk *pll_clk;

	parent = clks[core->parent & 0xffff];
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	pll_clk = devm_kzalloc(dev, sizeof(*pll_clk), GFP_KERNEL);
	if (!pll_clk)
		return ERR_PTR(-ENOMEM);

	parent_name = __clk_get_name(parent);
	init.name = core->name;
	init.ops = ops;
	init.flags = 0;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	pll_clk->hw.init = &init;
	pll_clk->conf = core->conf;
	pll_clk->base = base;
	pll_clk->priv = priv;
	pll_clk->type = core->type;

	return clk_register(NULL, &pll_clk->hw);
}

static struct clk
*rzg2l_cpg_clk_src_twocell_get(struct of_phandle_args *clkspec,
			       void *data)
{
	unsigned int clkidx = clkspec->args[1];
	struct rzg2l_cpg_priv *priv = data;
	struct device *dev = priv->dev;
	const char *type;
	struct clk *clk;

	switch (clkspec->args[0]) {
	case CPG_CORE:
		type = "core";
		if (clkidx > priv->last_dt_core_clk) {
			dev_err(dev, "Invalid %s clock index %u\n", type, clkidx);
			return ERR_PTR(-EINVAL);
		}
		clk = priv->clks[clkidx];
		break;

	case CPG_MOD:
		type = "module";
		if (clkidx >= priv->num_mod_clks) {
			dev_err(dev, "Invalid %s clock index %u\n", type,
				clkidx);
			return ERR_PTR(-EINVAL);
		}
		clk = priv->clks[priv->num_core_clks + clkidx];
		break;

	default:
		dev_err(dev, "Invalid CPG clock type %u\n", clkspec->args[0]);
		return ERR_PTR(-EINVAL);
	}

	if (IS_ERR(clk))
		dev_err(dev, "Cannot get %s clock %u: %ld", type, clkidx,
			PTR_ERR(clk));
	else
		dev_dbg(dev, "clock (%u, %u) is %pC at %lu Hz\n",
			clkspec->args[0], clkspec->args[1], clk,
			clk_get_rate(clk));
	return clk;
}

static void __init
rzg2l_cpg_register_core_clk(const struct cpg_core_clk *core,
			    const struct rzg2l_cpg_info *info,
			    struct rzg2l_cpg_priv *priv)
{
	struct clk *clk = ERR_PTR(-EOPNOTSUPP), *parent;
	struct device *dev = priv->dev;
	unsigned int id = core->id, div = core->div;
	const char *parent_name;

	WARN_DEBUG(id >= priv->num_core_clks);
	WARN_DEBUG(PTR_ERR(priv->clks[id]) != -ENOENT);

	if (!core->name) {
		/* Skip NULLified clock */
		return;
	}

	switch (core->type) {
	case CLK_TYPE_IN:
		clk = of_clk_get_by_name(priv->dev->of_node, core->name);
		break;
	case CLK_TYPE_FF:
		WARN_DEBUG(core->parent >= priv->num_core_clks);
		parent = priv->clks[core->parent];
		if (IS_ERR(parent)) {
			clk = parent;
			goto fail;
		}

		parent_name = __clk_get_name(parent);
		clk = clk_register_fixed_factor(NULL, core->name,
						parent_name, CLK_SET_RATE_PARENT,
						core->mult, div);
		break;
	case CLK_TYPE_SAM_PLL:
		clk = rzg2l_cpg_pll_clk_register(core, priv->clks, priv->base, priv,
						 &rzg2l_cpg_pll_ops);
		break;
	case CLK_TYPE_G3S_PLL:
		clk = rzg2l_cpg_pll_clk_register(core, priv->clks, priv->base, priv,
						 &rzg3s_cpg_pll_ops);
		break;
	case CLK_TYPE_SIPLL5:
		clk = rzg2l_cpg_sipll5_register(core, priv->clks, priv);
		break;
	case CLK_TYPE_DIV:
		clk = rzg2l_cpg_div_clk_register(core, priv->clks,
						 priv->base, priv);
		break;
	case CLK_TYPE_G3S_DIV:
		clk = rzg3s_cpg_div_clk_register(core, priv->clks, priv->base, priv);
		break;
	case CLK_TYPE_MUX:
		clk = rzg2l_cpg_mux_clk_register(core, priv->base, priv);
		break;
	case CLK_TYPE_SD_MUX:
		clk = rzg2l_cpg_sd_mux_clk_register(core, priv->base, priv);
		break;
	case CLK_TYPE_PLL5_4_MUX:
		clk = rzg2l_cpg_pll5_4_mux_clk_register(core, priv);
		break;
	case CLK_TYPE_DSI_DIV:
		clk = rzg2l_cpg_dsi_div_clk_register(core, priv->clks, priv);
		break;
	default:
		goto fail;
	}

	if (IS_ERR_OR_NULL(clk))
		goto fail;

	dev_dbg(dev, "Core clock %pC at %lu Hz\n", clk, clk_get_rate(clk));
	priv->clks[id] = clk;
	return;

fail:
	dev_err(dev, "Failed to register %s clock %s: %ld\n", "core",
		core->name, PTR_ERR(clk));
}

/**
 * struct mstp_clock - MSTP gating clock
 *
 * @hw: handle between common and hardware-specific interfaces
 * @off: register offset
 * @bit: ON/MON bit
 * @enabled: soft state of the clock, if it is coupled with another clock
 * @priv: CPG/MSTP private data
 * @sibling: pointer to the other coupled clock
 */
struct mstp_clock {
	struct clk_hw hw;
	u16 off;
	u8 bit;
	bool enabled;
	struct rzg2l_cpg_priv *priv;
	struct mstp_clock *sibling;
};

#define to_mod_clock(_hw) container_of(_hw, struct mstp_clock, hw)

static int rzg2l_mod_clock_endisable(struct clk_hw *hw, bool enable)
{
	struct mstp_clock *clock = to_mod_clock(hw);
	struct rzg2l_cpg_priv *priv = clock->priv;
	unsigned int reg = clock->off;
	struct device *dev = priv->dev;
	u32 bitmask = BIT(clock->bit);
	u32 value;
	int error;

	if (!clock->off) {
		dev_dbg(dev, "%pC does not support ON/OFF\n",  hw->clk);
		return 0;
	}

	dev_dbg(dev, "CLK_ON 0x%x/%pC %s\n", CLK_ON_R(reg), hw->clk,
		enable ? "ON" : "OFF");

	value = bitmask << 16;
	if (enable)
		value |= bitmask;

	writel(value, priv->base + CLK_ON_R(reg));

	if (!enable)
		return 0;

	if (!priv->info->has_clk_mon_regs)
		return 0;

	error = readl_poll_timeout_atomic(priv->base + CLK_MON_R(reg), value,
					  value & bitmask, 0, 10);
	if (error)
		dev_err(dev, "Failed to enable CLK_ON %p\n",
			priv->base + CLK_ON_R(reg));

	return error;
}

static int rzg2l_mod_clock_enable(struct clk_hw *hw)
{
	struct mstp_clock *clock = to_mod_clock(hw);

	if (clock->sibling) {
		struct rzg2l_cpg_priv *priv = clock->priv;
		unsigned long flags;
		bool enabled;

		spin_lock_irqsave(&priv->rmw_lock, flags);
		enabled = clock->sibling->enabled;
		clock->enabled = true;
		spin_unlock_irqrestore(&priv->rmw_lock, flags);
		if (enabled)
			return 0;
	}

	return rzg2l_mod_clock_endisable(hw, true);
}

static void rzg2l_mod_clock_disable(struct clk_hw *hw)
{
	struct mstp_clock *clock = to_mod_clock(hw);

	if (clock->sibling) {
		struct rzg2l_cpg_priv *priv = clock->priv;
		unsigned long flags;
		bool enabled;

		spin_lock_irqsave(&priv->rmw_lock, flags);
		enabled = clock->sibling->enabled;
		clock->enabled = false;
		spin_unlock_irqrestore(&priv->rmw_lock, flags);
		if (enabled)
			return;
	}

	rzg2l_mod_clock_endisable(hw, false);
}

static int rzg2l_mod_clock_is_enabled(struct clk_hw *hw)
{
	struct mstp_clock *clock = to_mod_clock(hw);
	struct rzg2l_cpg_priv *priv = clock->priv;
	u32 bitmask = BIT(clock->bit);
	u32 value;

	if (!clock->off) {
		dev_dbg(priv->dev, "%pC does not support ON/OFF\n",  hw->clk);
		return 1;
	}

	if (clock->sibling)
		return clock->enabled;

	if (priv->info->has_clk_mon_regs)
		value = readl(priv->base + CLK_MON_R(clock->off));
	else
		value = readl(priv->base + clock->off);

	return value & bitmask;
}

static const struct clk_ops rzg2l_mod_clock_ops = {
	.enable = rzg2l_mod_clock_enable,
	.disable = rzg2l_mod_clock_disable,
	.is_enabled = rzg2l_mod_clock_is_enabled,
};

static struct mstp_clock
*rzg2l_mod_clock_get_sibling(struct mstp_clock *clock,
			     struct rzg2l_cpg_priv *priv)
{
	struct clk_hw *hw;
	unsigned int i;

	for (i = 0; i < priv->num_mod_clks; i++) {
		struct mstp_clock *clk;

		if (priv->clks[priv->num_core_clks + i] == ERR_PTR(-ENOENT))
			continue;

		hw = __clk_get_hw(priv->clks[priv->num_core_clks + i]);
		clk = to_mod_clock(hw);
		if (clock->off == clk->off && clock->bit == clk->bit)
			return clk;
	}

	return NULL;
}

static void __init
rzg2l_cpg_register_mod_clk(const struct rzg2l_mod_clk *mod,
			   const struct rzg2l_cpg_info *info,
			   struct rzg2l_cpg_priv *priv)
{
	struct mstp_clock *clock = NULL;
	struct device *dev = priv->dev;
	unsigned int id = mod->id;
	struct clk_init_data init;
	struct clk *parent, *clk;
	const char *parent_name;
	unsigned int i;

	WARN_DEBUG(id < priv->num_core_clks);
	WARN_DEBUG(id >= priv->num_core_clks + priv->num_mod_clks);
	WARN_DEBUG(mod->parent >= priv->num_core_clks + priv->num_mod_clks);
	WARN_DEBUG(PTR_ERR(priv->clks[id]) != -ENOENT);

	if (!mod->name) {
		/* Skip NULLified clock */
		return;
	}

	parent = priv->clks[mod->parent];
	if (IS_ERR(parent)) {
		clk = parent;
		goto fail;
	}

	clock = devm_kzalloc(dev, sizeof(*clock), GFP_KERNEL);
	if (!clock) {
		clk = ERR_PTR(-ENOMEM);
		goto fail;
	}

	init.name = mod->name;
	init.ops = &rzg2l_mod_clock_ops;
	init.flags = CLK_SET_RATE_PARENT;
	for (i = 0; i < info->num_crit_mod_clks; i++)
		if (id == info->crit_mod_clks[i]) {
			dev_dbg(dev, "CPG %s setting CLK_IS_CRITICAL\n",
				mod->name);
			init.flags |= CLK_IS_CRITICAL;
			break;
		}

	parent_name = __clk_get_name(parent);
	init.parent_names = &parent_name;
	init.num_parents = 1;

	clock->off = mod->off;
	clock->bit = mod->bit;
	clock->priv = priv;
	clock->hw.init = &init;

	clk = clk_register(NULL, &clock->hw);
	if (IS_ERR(clk))
		goto fail;

	dev_dbg(dev, "Module clock %pC at %lu Hz\n", clk, clk_get_rate(clk));
	priv->clks[id] = clk;

	if (mod->is_coupled) {
		struct mstp_clock *sibling;

		clock->enabled = rzg2l_mod_clock_is_enabled(&clock->hw);
		sibling = rzg2l_mod_clock_get_sibling(clock, priv);
		if (sibling) {
			clock->sibling = sibling;
			sibling->sibling = clock;
		}
	}

	return;

fail:
	dev_err(dev, "Failed to register %s clock %s: %ld\n", "module",
		mod->name, PTR_ERR(clk));
}

#define rcdev_to_priv(x)	container_of(x, struct rzg2l_cpg_priv, rcdev)

static int rzg2l_cpg_assert(struct reset_controller_dev *rcdev,
			    unsigned long id)
{
	struct rzg2l_cpg_priv *priv = rcdev_to_priv(rcdev);
	const struct rzg2l_cpg_info *info = priv->info;
	unsigned int reg = info->resets[id].off;
	u32 mask = BIT(info->resets[id].bit);
	s8 monbit = info->resets[id].monbit;
	u32 value = mask << 16;

	dev_dbg(rcdev->dev, "assert id:%ld offset:0x%x\n", id, CLK_RST_R(reg));

	writel(value, priv->base + CLK_RST_R(reg));

	if (info->has_clk_mon_regs) {
		reg = CLK_MRST_R(reg);
	} else if (monbit >= 0) {
		reg = CPG_RST_MON;
		mask = BIT(monbit);
	} else {
		/* Wait for at least one cycle of the RCLK clock (@ ca. 32 kHz) */
		udelay(35);
		return 0;
	}

	return readl_poll_timeout_atomic(priv->base + reg, value,
					 value & mask, 10, 200);
}

static int rzg2l_cpg_deassert(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct rzg2l_cpg_priv *priv = rcdev_to_priv(rcdev);
	const struct rzg2l_cpg_info *info = priv->info;
	unsigned int reg = info->resets[id].off;
	u32 mask = BIT(info->resets[id].bit);
	s8 monbit = info->resets[id].monbit;
	u32 value = (mask << 16) | mask;

	dev_dbg(rcdev->dev, "deassert id:%ld offset:0x%x\n", id,
		CLK_RST_R(reg));

	writel(value, priv->base + CLK_RST_R(reg));

	if (info->has_clk_mon_regs) {
		reg = CLK_MRST_R(reg);
	} else if (monbit >= 0) {
		reg = CPG_RST_MON;
		mask = BIT(monbit);
	} else {
		/* Wait for at least one cycle of the RCLK clock (@ ca. 32 kHz) */
		udelay(35);
		return 0;
	}

	return readl_poll_timeout_atomic(priv->base + reg, value,
					 !(value & mask), 10, 200);
}

static int rzg2l_cpg_reset(struct reset_controller_dev *rcdev,
			   unsigned long id)
{
	int ret;

	ret = rzg2l_cpg_assert(rcdev, id);
	if (ret)
		return ret;

	return rzg2l_cpg_deassert(rcdev, id);
}

static int rzg2l_cpg_status(struct reset_controller_dev *rcdev,
			    unsigned long id)
{
	struct rzg2l_cpg_priv *priv = rcdev_to_priv(rcdev);
	const struct rzg2l_cpg_info *info = priv->info;
	s8 monbit = info->resets[id].monbit;
	unsigned int reg;
	u32 bitmask;

	if (info->has_clk_mon_regs) {
		reg = CLK_MRST_R(info->resets[id].off);
		bitmask = BIT(info->resets[id].bit);
	} else if (monbit >= 0) {
		reg = CPG_RST_MON;
		bitmask = BIT(monbit);
	} else {
		return -ENOTSUPP;
	}

	return !!(readl(priv->base + reg) & bitmask);
}

static const struct reset_control_ops rzg2l_cpg_reset_ops = {
	.reset = rzg2l_cpg_reset,
	.assert = rzg2l_cpg_assert,
	.deassert = rzg2l_cpg_deassert,
	.status = rzg2l_cpg_status,
};

static int rzg2l_cpg_reset_xlate(struct reset_controller_dev *rcdev,
				 const struct of_phandle_args *reset_spec)
{
	struct rzg2l_cpg_priv *priv = rcdev_to_priv(rcdev);
	const struct rzg2l_cpg_info *info = priv->info;
	unsigned int id = reset_spec->args[0];

	if (id >= rcdev->nr_resets || !info->resets[id].off) {
		dev_err(rcdev->dev, "Invalid reset index %u\n", id);
		return -EINVAL;
	}

	return id;
}

static int rzg2l_cpg_reset_controller_register(struct rzg2l_cpg_priv *priv)
{
	priv->rcdev.ops = &rzg2l_cpg_reset_ops;
	priv->rcdev.of_node = priv->dev->of_node;
	priv->rcdev.dev = priv->dev;
	priv->rcdev.of_reset_n_cells = 1;
	priv->rcdev.of_xlate = rzg2l_cpg_reset_xlate;
	priv->rcdev.nr_resets = priv->num_resets;

	return devm_reset_controller_register(priv->dev, &priv->rcdev);
}

static bool rzg2l_cpg_is_pm_clk(struct rzg2l_cpg_priv *priv,
				const struct of_phandle_args *clkspec)
{
	const struct rzg2l_cpg_info *info = priv->info;
	unsigned int id;
	unsigned int i;

	if (clkspec->args_count != 2)
		return false;

	if (clkspec->args[0] != CPG_MOD)
		return false;

	id = clkspec->args[1] + info->num_total_core_clks;
	for (i = 0; i < info->num_no_pm_mod_clks; i++) {
		if (info->no_pm_mod_clks[i] == id)
			return false;
	}

	return true;
}

static int rzg2l_cpg_attach_dev(struct generic_pm_domain *domain, struct device *dev)
{
	struct rzg2l_cpg_priv *priv = container_of(domain, struct rzg2l_cpg_priv, genpd);
	struct device_node *np = dev->of_node;
	struct of_phandle_args clkspec;
	bool once = true;
	struct clk *clk;
	int error;
	int i = 0;

	while (!of_parse_phandle_with_args(np, "clocks", "#clock-cells", i,
					   &clkspec)) {
		if (rzg2l_cpg_is_pm_clk(priv, &clkspec)) {
			if (once) {
				once = false;
				error = pm_clk_create(dev);
				if (error) {
					of_node_put(clkspec.np);
					goto err;
				}
			}
			clk = of_clk_get_from_provider(&clkspec);
			of_node_put(clkspec.np);
			if (IS_ERR(clk)) {
				error = PTR_ERR(clk);
				goto fail_destroy;
			}

			error = pm_clk_add_clk(dev, clk);
			if (error) {
				dev_err(dev, "pm_clk_add_clk failed %d\n",
					error);
				goto fail_put;
			}
		} else {
			of_node_put(clkspec.np);
		}
		i++;
	}

	return 0;

fail_put:
	clk_put(clk);

fail_destroy:
	pm_clk_destroy(dev);
err:
	return error;
}

static void rzg2l_cpg_detach_dev(struct generic_pm_domain *unused, struct device *dev)
{
	if (!pm_clk_no_clocks(dev))
		pm_clk_destroy(dev);
}

static void rzg2l_cpg_genpd_remove(void *data)
{
	pm_genpd_remove(data);
}

static int __init rzg2l_cpg_add_clk_domain(struct rzg2l_cpg_priv *priv)
{
	struct device *dev = priv->dev;
	struct device_node *np = dev->of_node;
	struct generic_pm_domain *genpd = &priv->genpd;
	int ret;

	genpd->name = np->name;
	genpd->flags = GENPD_FLAG_PM_CLK | GENPD_FLAG_ALWAYS_ON |
		       GENPD_FLAG_ACTIVE_WAKEUP;
	genpd->attach_dev = rzg2l_cpg_attach_dev;
	genpd->detach_dev = rzg2l_cpg_detach_dev;
	ret = pm_genpd_init(genpd, &pm_domain_always_on_gov, false);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, rzg2l_cpg_genpd_remove, genpd);
	if (ret)
		return ret;

	return of_genpd_add_provider_simple(np, genpd);
}

static int __init rzg2l_cpg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const struct rzg2l_cpg_info *info;
	struct rzg2l_cpg_priv *priv;
	unsigned int nclks, i;
	struct clk **clks;
	int error;

	info = of_device_get_match_data(dev);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->info = info;
	spin_lock_init(&priv->rmw_lock);

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	nclks = info->num_total_core_clks + info->num_hw_mod_clks;
	clks = devm_kmalloc_array(dev, nclks, sizeof(*clks), GFP_KERNEL);
	if (!clks)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);
	priv->clks = clks;
	priv->num_core_clks = info->num_total_core_clks;
	priv->num_mod_clks = info->num_hw_mod_clks;
	priv->num_resets = info->num_resets;
	priv->last_dt_core_clk = info->last_dt_core_clk;

	for (i = 0; i < nclks; i++)
		clks[i] = ERR_PTR(-ENOENT);

	for (i = 0; i < info->num_core_clks; i++)
		rzg2l_cpg_register_core_clk(&info->core_clks[i], info, priv);

	for (i = 0; i < info->num_mod_clks; i++)
		rzg2l_cpg_register_mod_clk(&info->mod_clks[i], info, priv);

	error = of_clk_add_provider(np, rzg2l_cpg_clk_src_twocell_get, priv);
	if (error)
		return error;

	error = devm_add_action_or_reset(dev, rzg2l_cpg_del_clk_provider, np);
	if (error)
		return error;

	error = rzg2l_cpg_add_clk_domain(priv);
	if (error)
		return error;

	error = rzg2l_cpg_reset_controller_register(priv);
	if (error)
		return error;

	return 0;
}

static const struct of_device_id rzg2l_cpg_match[] = {
#ifdef CONFIG_CLK_R9A07G043
	{
		.compatible = "renesas,r9a07g043-cpg",
		.data = &r9a07g043_cpg_info,
	},
#endif
#ifdef CONFIG_CLK_R9A07G044
	{
		.compatible = "renesas,r9a07g044-cpg",
		.data = &r9a07g044_cpg_info,
	},
#endif
#ifdef CONFIG_CLK_R9A07G054
	{
		.compatible = "renesas,r9a07g054-cpg",
		.data = &r9a07g054_cpg_info,
	},
#endif
#ifdef CONFIG_CLK_R9A08G045
	{
		.compatible = "renesas,r9a08g045-cpg",
		.data = &r9a08g045_cpg_info,
	},
#endif
#ifdef CONFIG_CLK_R9A09G011
	{
		.compatible = "renesas,r9a09g011-cpg",
		.data = &r9a09g011_cpg_info,
	},
#endif
	{ /* sentinel */ }
};

static struct platform_driver rzg2l_cpg_driver = {
	.driver		= {
		.name	= "rzg2l-cpg",
		.of_match_table = rzg2l_cpg_match,
	},
};

static int __init rzg2l_cpg_init(void)
{
	return platform_driver_probe(&rzg2l_cpg_driver, rzg2l_cpg_probe);
}

subsys_initcall(rzg2l_cpg_init);

MODULE_DESCRIPTION("Renesas RZ/G2L CPG Driver");
