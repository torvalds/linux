// SPDX-License-Identifier: GPL-2.0-only
#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/interconnect-clk.h>
#include <linux/interconnect-provider.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,apcs-msm8953.h>

#include "clk-branch.h"
#include "clk-alpha-pll.h"
#include "clk-regmap-mux-div.h"

static bool apcs_is_sdm632;

static struct regmap *apcs_get_regmap(struct device *dev, const char *name,
				      unsigned int max_reg)
{
	struct regmap_config cfg = { 0 };
	void __iomem *mem;

	cfg.fast_io = true;
	cfg.reg_bits = cfg.val_bits = 32;
	cfg.reg_stride = 4;
	cfg.max_register = max_reg;
	cfg.name = name;

	mem = devm_platform_ioremap_resource_byname(to_platform_device(dev), name);
	if (IS_ERR(mem))
		return ERR_PTR(PTR_ERR(mem));

	return devm_regmap_init_mmio(dev, mem, &cfg);
}

static const u8 apcs_common_cpu_pll_regs[PLL_OFF_MAX_REGS] = {
	[PLL_OFF_L_VAL]		= 0x08,
	[PLL_OFF_ALPHA_VAL]	= 0x10,
	[PLL_OFF_USER_CTL]	= 0x18,
	[PLL_OFF_CONFIG_CTL]	= 0x20,
	[PLL_OFF_CONFIG_CTL_U]	= 0x24,
	[PLL_OFF_STATUS]	= 0x28,
	[PLL_OFF_TEST_CTL]	= 0x30,
	[PLL_OFF_TEST_CTL_U]	= 0x34,
};

static const struct alpha_pll_config apcs_common_cpu_pll_config = {
	.l			= 39,
	.config_ctl_val		= 0x200d4828,
	.config_ctl_hi_val	= 0x6,
	.test_ctl_val		= 0x1c000000,
	.test_ctl_hi_val	= 0x4000,
	.main_output_mask	= BIT(0),
	.early_output_mask	= BIT(3),
};

#define USER_CTL_U_LATCH_IFACE_MASK	BIT(11)
#define USER_CTL_U_CAL_L_MASK		GENMASK(31, 16)

static const struct alpha_pll_config apcs_sdm632_cci_pll_config = {
	.l			= 39,
	.vco_mask		= GENMASK(21, 20),
	.vco_val		= 2 << 20,
	.config_ctl_val		= 0x4001055b,
	.early_output_mask	= BIT(3),
};

static const struct pll_vco sdm632_pwr_vco = VCO(0, 614400000, 2016000000);
static const struct pll_vco sdm632_perf_vco = VCO(0, 633600000, 2016000000);
static const struct pll_vco sdm632_cci_vco = VCO(2, 500000000, 1000000000);
static const struct pll_vco msm8953_vco = VCO(0, 652800000, 2208000000);

static int apcs_register_pll(struct device *dev, int id, struct clk_hw **hws)
{
	const struct alpha_pll_config *pll_config = &apcs_common_cpu_pll_config;
	static const struct clk_parent_data pdata = { .fw_name = "osc" };
	struct clk_init_data init = { 0 };
	struct clk_alpha_pll *apll;
	struct regmap *rmap;

	/* Only SDM/SDA632 has multiple APCS PLLs */
	if (!apcs_is_sdm632)
		id = APCS_CPU0_PLL;

	if (hws[id])
		return 0;

	apll = devm_kzalloc(dev, sizeof(*apll), GFP_KERNEL);
	if (!apll)
		return -ENOMEM;

	init.num_parents = 1;
	init.parent_data = &pdata;
	init.ops = &clk_alpha_pll_huayra_ops;

	apll->clkr.hw.init = &init;
	apll->flags = SUPPORTS_DYNAMIC_UPDATE;
	/* CPU PLLs on those SoCs are clamping L value internally so they
	 * can't be overclocked much. We would need to adjust max_freq by speed
	 * bin here but it would only matter if we'd use dividers other than 1.
	 */
	apll->vco_table = apcs_is_sdm632 ? &sdm632_pwr_vco : &msm8953_vco;
	apll->num_vco = 1;
	apll->regs = apcs_common_cpu_pll_regs;

	switch (id) {
	case APCS_CPU0_PLL:
		init.name = "apcs-cpu0-pll";
		rmap = apcs_get_regmap(dev, "cpu0_pll", 0x34);
		break;
	case APCS_CPU4_PLL:
		init.name = "apcs-cpu4-pll";
		apll->vco_table = &sdm632_perf_vco;
		rmap = apcs_get_regmap(dev, "cpu4_pll", 0x34);
		break;
	case APCS_CCI_PLL:
		init.name = "apcs-cci-pll";
		init.ops = &clk_alpha_pll_stromer_ops;
		apll->vco_table = &sdm632_cci_vco;
		apll->regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT];
		pll_config = &apcs_sdm632_cci_pll_config;
		rmap = apcs_get_regmap(dev, "cci_pll", 0x24);
		break;
	}

	if (IS_ERR(rmap))
		return PTR_ERR(rmap);

	apll->clkr.regmap = rmap;

	clk_alpha_pll_configure(apll, rmap, pll_config);

	if (id == APCS_CCI_PLL)
		regmap_update_bits(rmap, apll->regs[PLL_OFF_USER_CTL_U],
				   USER_CTL_U_CAL_L_MASK | USER_CTL_U_LATCH_IFACE_MASK,
				   FIELD_PREP(USER_CTL_U_CAL_L_MASK, pll_config->l));

	hws[id] = &apll->clkr.hw;
	if (!apcs_is_sdm632)
		hws[APCS_CCI_PLL] = hws[APCS_CPU4_PLL] = hws[id];

	return devm_clk_hw_register(dev, hws[id]);
}

enum apcs_mux_parents {
	APCS_PLL,
	GCC_PLL0,
};

static const u32 apcs_mux_parent_map[] = {
	[APCS_PLL] = 5,
	[GCC_PLL0] = 4,
	/* [GCC_PLL2_EARLY_DIV] = 6
	 * [APCS_PLL_POSTDIV] = 3
	 * [GCC_PLL2] = 2,
	 * [GCC_PLL4] = 1,
	 * [XO] = 0,
	 */
};

struct apcs_md_clk {
	struct clk_regmap_mux_div md;
	struct notifier_block pll_nb;
	struct clk_notifier_data pll_change;
	unsigned int apcs_pll_fixed_factor;
};

#define to_apcs_md(_hw) container_of(_hw, struct apcs_md_clk, md.clkr.hw)

static struct clk_ops apcs_md_ops;

static int apcs_msm8953_determine_rate(struct clk_hw *hw,
				       struct clk_rate_request *req)
{
	struct apcs_md_clk *clk = to_apcs_md(hw);
	unsigned long hdiv = clk->apcs_pll_fixed_factor;
	/* On anything other than SDM632 CCI clock we force
	 * APCS PLL with fixed divider
	 */
	if (!hdiv)
		return clk_regmap_mux_div_ops.determine_rate(hw, req);

	req->best_parent_hw = clk_hw_get_parent_by_index(hw, APCS_PLL);
	req->best_parent_rate = clk_hw_round_rate(req->best_parent_hw,
						  mult_frac(req->rate, hdiv, 2));
	req->rate = mult_frac(req->best_parent_rate, 2, hdiv);
	return 0;
}

static int apcs_pll_notifier(struct notifier_block *nb, unsigned long action,
			     void *data)
{
	struct apcs_md_clk *apclk = container_of(nb, struct apcs_md_clk, pll_nb);

	apclk->pll_change.old_rate = apclk->pll_change.new_rate = 0;

	if (action == PRE_RATE_CHANGE && data)
		apclk->pll_change = ((struct clk_notifier_data *) data)[0];

	return NOTIFY_OK;
}

static int apcs_mux_div_notifier(struct notifier_block *nb,
	unsigned long action, void *data)
{
	struct apcs_md_clk *apclk = container_of(nb, struct apcs_md_clk, md.clk_nb);
	const struct clk_ops *ops = &clk_regmap_mux_div_ops;
	struct clk_notifier_data *pll_nd = &apclk->pll_change;
	struct clk_hw *hw = &apclk->md.clkr.hw;
	struct clk_notifier_data *clk_nd = data;
	unsigned long imd_rate, max_allowed_rate;

	if (action != PRE_RATE_CHANGE ||
	    clk_hw_get_parent_index(hw) != APCS_PLL ||
	    pll_nd->old_rate >= pll_nd->new_rate)
		return NOTIFY_OK;

	max_allowed_rate = max(clk_nd->old_rate, clk_nd->new_rate);
	imd_rate = ops->recalc_rate(hw, pll_nd->new_rate);
	if (imd_rate > max_allowed_rate) {
		ops->set_rate(hw, max_allowed_rate, pll_nd->new_rate);
		WARN_ON(ops->recalc_rate(hw, pll_nd->new_rate) > max_allowed_rate);
	}

	return NOTIFY_OK;
}

static int apcs_register_mux_div(struct device *dev, int id, struct clk_hw **hws,
				     struct regmap *rmap)
{
	struct clk_parent_data pdata[ARRAY_SIZE(apcs_mux_parent_map)] = {0};
	struct clk_init_data init = {0};
	struct clk_regmap_mux_div *md;
	struct apcs_md_clk *apclk;
	int ret, pll_id;

	apclk = devm_kzalloc(dev, sizeof(*apclk), GFP_KERNEL);
	if (!apclk)
		return -ENOMEM;

	apclk->pll_nb.notifier_call = apcs_pll_notifier;
	apclk->apcs_pll_fixed_factor = 2;

	md = &apclk->md;
	md->clk_nb.notifier_call = apcs_mux_div_notifier;
	md->clkr.regmap = rmap;
	md->parent_map = apcs_mux_parent_map;
	md->hid_width = 5;
	md->src_shift = 8;
	md->src_width = 3;
	md->clkr.hw.init = &init;

	init.num_parents = ARRAY_SIZE(pdata);
	init.parent_data = pdata;
	init.ops = &apcs_md_ops;
	init.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL;

	switch (id) {
	case APCS_CPU0_CLK_SRC:
		init.name = "apcs-cpu0-clk-src";
		pll_id = APCS_CPU0_PLL;
		break;
	case APCS_CPU4_CLK_SRC:
		init.name = "apcs-cpu4-clk-src";
		pll_id = APCS_CPU4_PLL;
		break;
	case APCS_CCI_CLK_SRC:
		init.name = "apcs-cci-clk-src";
		pll_id = APCS_CCI_PLL;
		apclk->apcs_pll_fixed_factor = apcs_is_sdm632 ? 0 : 5;
		break;
	}

	ret = apcs_register_pll(dev, pll_id, hws);
	if (ret)
		return 0;

	pdata[APCS_PLL].hw = hws[pll_id];
	pdata[GCC_PLL0].fw_name = "gpll0";

	hws[id] = &md->clkr.hw;

	ret = devm_clk_hw_register(dev, hws[id]);
	if (ret)
		return ret;

	ret = devm_clk_notifier_register(dev, hws[pll_id]->clk, &apclk->pll_nb);
	if (ret)
		return ret;

	return devm_clk_notifier_register(dev, hws[id]->clk, &md->clk_nb);
}

static int apcs_register_clk(struct device *dev, int id, struct clk_hw **hws)
{
	struct clk_parent_data pdata = {0};
	struct clk_init_data init = {0};
	struct regmap *rmap = ERR_PTR(-EINVAL);
	struct clk_branch *gate;
	int ret, src_id;

	gate = devm_kzalloc(dev, sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return -ENOMEM;

	init.num_parents = 1;
	init.parent_data = &pdata;
	init.ops = &clk_branch2_ops;
	init.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL;

	switch (id) {
	case APCS_CPU0_CLK:
		init.name = "apcs-cpu0-clk";
		src_id = APCS_CPU0_CLK_SRC;
		rmap = apcs_get_regmap(dev, "cpu0_rcg_cbr", 0x8);
		break;
	case APCS_CPU4_CLK:
		init.name = "apcs-cpu4-clk";
		src_id = APCS_CPU4_CLK_SRC;
		rmap = apcs_get_regmap(dev, "cpu4_rcg_cbr", 0x8);
		break;
	case APCS_CCI_CLK:
		init.name = "apcs-cci-clk";
		src_id = APCS_CCI_CLK_SRC;
		rmap = apcs_get_regmap(dev, "cci_rcg_cbr", 0x8);
		if (!apcs_is_sdm632)
			init.flags = CLK_IS_CRITICAL;
		break;
	}

	if (IS_ERR(rmap))
		return PTR_ERR(rmap);

	ret = apcs_register_mux_div(dev, src_id, hws, rmap);
	if (ret)
		return ret;

	pdata.hw = hws[src_id];

	gate->halt_reg = 8;
	gate->halt_check = BRANCH_HALT;
	gate->clkr.enable_reg = 8;
	gate->clkr.enable_mask = BIT(0);
	gate->clkr.hw.init = &init;
	gate->clkr.regmap = rmap;

	hws[id] = &gate->clkr.hw;

	return devm_clk_hw_register(dev, hws[id]);
}

#ifdef CONFIG_INTERCONNECT
static void __maybe_unused apcs_msm8953_icc_deinit(void *data)
{
	struct icc_provider *provider = data;

	icc_clk_unregister(provider);
}

static int apcs_msm8953_icc_init(struct device *dev,
				 struct clk_hw *cci_hw)
{
	struct icc_clk_data data = {
		.name = "cci_bimc",
		.clk = devm_clk_hw_get_clk(dev, cci_hw, "cci-icc"),
		.opp = apcs_is_sdm632,
		.master_id = APCS_MAS_CCI,
		.slave_id = APCS_SLV_BIMC,
	};
	struct icc_provider *iprov;
	int ret;

	if (data.opp) {
		ret = devm_pm_opp_set_clkname(dev, "cci-icc");
		if (ret)
			return ret;

		ret = devm_pm_opp_of_add_table(dev);
		if (ret)
			return ret;
	}

	iprov = icc_clk_register(dev, 0xcc1, 1, &data);
	if (IS_ERR(iprov))
		return PTR_ERR(iprov);

	return devm_add_action_or_reset(dev, apcs_msm8953_icc_deinit, iprov);
}

#define apcs_icc_sync_state icc_sync_state
#else
#define apcs_msm8953_icc_init(dev, clk) 0
#define apcs_icc_sync_state NULL
#endif

static int apcs_msm8953_probe(struct platform_device *pdev)
{
	const int clks[] = { APCS_CPU0_CLK, APCS_CPU4_CLK, APCS_CCI_CLK };
	struct device *dev = &pdev->dev;
	struct clk_hw_onecell_data *data;
	int ret, i;

	apcs_is_sdm632 = of_machine_is_compatible("qcom,sdm632");

	apcs_md_ops = clk_regmap_mux_div_ops;
	apcs_md_ops.determine_rate = apcs_msm8953_determine_rate;

	data = devm_kzalloc(dev, struct_size(data, hws, APCS_NUM_CLOCKS),
		       GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->num = APCS_NUM_CLOCKS;

	for (i = 0; i < ARRAY_SIZE(clks); i++) {
		ret = apcs_register_clk(dev, clks[i], data->hws);
		if (ret)
			return ret;
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, data);
	if (ret)
		return ret;

	return apcs_msm8953_icc_init(dev, data->hws[APCS_CCI_CLK]);
}

static const struct of_device_id apcs_msm8953_match_table[] = {
	{ .compatible = "qcom,apcs-cc-msm8953", },
	{},
};

static struct platform_driver apcscc_msm8953_driver = {
	.probe = apcs_msm8953_probe,
	.driver = {
		.name = "qcom-apcs-cc-msm8953",
		.of_match_table = apcs_msm8953_match_table,
		.sync_state = apcs_icc_sync_state,
	},
};
module_platform_driver(apcscc_msm8953_driver);

MODULE_DEVICE_TABLE(of, apcs_msm8953_match_table);
MODULE_AUTHOR("Vladimir Lypak <vladimir.lypak@gmail.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Qualcomm MSM8953 APCS clock driver");
