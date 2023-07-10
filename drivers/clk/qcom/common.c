// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2014, 2017-2021, The Linux Foundation.
 * All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/export.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/clk-provider.h>
#include <linux/reset-controller.h>
#include <linux/of.h>
#include <linux/clk/qcom.h>
#include <linux/clk.h>
#include <linux/interconnect.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>
#include <linux/mfd/syscon.h>

#include "common.h"
#include "clk-opp.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "reset.h"
#include "gdsc.h"
#include "vdd-level.h"
#include "clk-debug.h"

struct qcom_cc {
	struct qcom_reset_controller reset;
	struct clk_regmap **rclks;
	size_t num_rclks;
	struct clk_hw **clk_hws;
	size_t num_clk_hws;
};

int qcom_clk_crm_init(struct device *dev, struct clk_crm *crm)
{
	char prop_name[32];

	if (!crm)
		return -EINVAL;

	if (!crm->initialized) {
		snprintf(prop_name, sizeof(prop_name), "qcom,%s-crmc", crm->name);

		if (of_find_property(dev->of_node, prop_name, NULL)) {
			crm->regmap_crmc =
				syscon_regmap_lookup_by_phandle(dev->of_node,
								prop_name);
			if (IS_ERR(crm->regmap_crmc)) {
				dev_err(dev, "%s regmap error\n", prop_name);
				return PTR_ERR(crm->regmap_crmc);
			}
		}

		if (crm->name) {
			crm->dev = crm_get_device(crm->name);
			if (IS_ERR(crm->dev)) {
				pr_err("%s Failed to get crm dev=%s, ret=%d\n",
				       __func__, crm->name, PTR_ERR(crm->dev));
				return PTR_ERR(crm->dev);
			}
		}

		crm->initialized = true;
	}

	return 0;
}
EXPORT_SYMBOL(qcom_clk_crm_init);

static int qcom_find_freq_index(const struct freq_tbl *f, unsigned long rate)
{
	int index;

	for (index = 0; f->freq; f++, index++) {
		if (rate <= f->freq)
			return index;
	}

	return index - 1;
}

int qcom_find_crm_freq_index(const struct freq_tbl *f, unsigned long rate)
{
	if (!f || !f->freq)
		return -EINVAL;

	/*
	 * If rate is 0 return PERF_OL 0 index
	 */
	if (!rate)
		return 0;

	/*
	 * Return PERF_OL index + 1 as PERF_OL 0 is
	 * treated as CLK OFF as per LUT population
	 */
	return qcom_find_freq_index(f, rate) + 1;
}
EXPORT_SYMBOL(qcom_find_crm_freq_index);

const
struct freq_tbl *qcom_find_freq(const struct freq_tbl *f, unsigned long rate)
{
	if (!f)
		return NULL;

	if (!f->freq)
		return f;

	for (; f->freq; f++)
		if (rate <= f->freq)
			return f;

	/* Default to our fastest rate */
	return f - 1;
}
EXPORT_SYMBOL_GPL(qcom_find_freq);

const struct freq_tbl *qcom_find_freq_floor(const struct freq_tbl *f,
					    unsigned long rate)
{
	const struct freq_tbl *best = NULL;

	for ( ; f->freq; f++) {
		if (rate >= f->freq)
			best = f;
		else
			break;
	}

	return best;
}
EXPORT_SYMBOL_GPL(qcom_find_freq_floor);

int qcom_find_src_index(struct clk_hw *hw, const struct parent_map *map, u8 src)
{
	int i, num_parents = clk_hw_get_num_parents(hw);

	for (i = 0; i < num_parents; i++)
		if (src == map[i].src)
			return i;

	return -ENOENT;
}
EXPORT_SYMBOL_GPL(qcom_find_src_index);

int qcom_find_cfg_index(struct clk_hw *hw, const struct parent_map *map, u8 cfg)
{
	int i, num_parents = clk_hw_get_num_parents(hw);

	for (i = 0; i < num_parents; i++)
		if (cfg == map[i].cfg)
			return i;

	return -ENOENT;
}
EXPORT_SYMBOL_GPL(qcom_find_cfg_index);

struct regmap *
qcom_cc_map(struct platform_device *pdev, const struct qcom_cc_desc *desc)
{
	void __iomem *base;
	struct device *dev = &pdev->dev;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return ERR_CAST(base);

	return devm_regmap_init_mmio(dev, base, desc->config);
}
EXPORT_SYMBOL_GPL(qcom_cc_map);

void
qcom_pll_set_fsm_mode(struct regmap *map, u32 reg, u8 bias_count, u8 lock_count)
{
	u32 val;
	u32 mask;

	/* De-assert reset to FSM */
	regmap_update_bits(map, reg, PLL_VOTE_FSM_RESET, 0);

	/* Program bias count and lock count */
	val = bias_count << PLL_BIAS_COUNT_SHIFT |
		lock_count << PLL_LOCK_COUNT_SHIFT;
	mask = PLL_BIAS_COUNT_MASK << PLL_BIAS_COUNT_SHIFT;
	mask |= PLL_LOCK_COUNT_MASK << PLL_LOCK_COUNT_SHIFT;
	regmap_update_bits(map, reg, mask, val);

	/* Enable PLL FSM voting */
	regmap_update_bits(map, reg, PLL_VOTE_FSM_ENA, PLL_VOTE_FSM_ENA);
}
EXPORT_SYMBOL_GPL(qcom_pll_set_fsm_mode);

static void qcom_cc_gdsc_unregister(void *data)
{
	gdsc_unregister(data);
}

/*
 * Backwards compatibility with old DTs. Register a pass-through factor 1/1
 * clock to translate 'path' clk into 'name' clk and register the 'path'
 * clk as a fixed rate clock if it isn't present.
 */
static int _qcom_cc_register_board_clk(struct device *dev, const char *path,
				       const char *name, unsigned long rate,
				       bool add_factor)
{
	struct device_node *node = NULL;
	struct device_node *clocks_node;
	struct clk_fixed_factor *factor;
	struct clk_fixed_rate *fixed;
	struct clk_init_data init_data = { };
	int ret;

	clocks_node = of_find_node_by_path("/clocks");
	if (clocks_node) {
		node = of_get_child_by_name(clocks_node, path);
		of_node_put(clocks_node);
	}

	if (!node) {
		fixed = devm_kzalloc(dev, sizeof(*fixed), GFP_KERNEL);
		if (!fixed)
			return -EINVAL;

		fixed->fixed_rate = rate;
		fixed->hw.init = &init_data;

		init_data.name = path;
		init_data.ops = &clk_fixed_rate_ops;

		ret = devm_clk_hw_register(dev, &fixed->hw);
		if (ret)
			return ret;
	}
	of_node_put(node);

	if (add_factor) {
		factor = devm_kzalloc(dev, sizeof(*factor), GFP_KERNEL);
		if (!factor)
			return -EINVAL;

		factor->mult = factor->div = 1;
		factor->hw.init = &init_data;

		init_data.name = name;
		init_data.parent_names = &path;
		init_data.num_parents = 1;
		init_data.flags = 0;
		init_data.ops = &clk_fixed_factor_ops;

		ret = devm_clk_hw_register(dev, &factor->hw);
		if (ret)
			return ret;
	}

	return 0;
}

int qcom_cc_register_board_clk(struct device *dev, const char *path,
			       const char *name, unsigned long rate)
{
	bool add_factor = true;

	/*
	 * TODO: The RPM clock driver currently does not support the xo clock.
	 * When xo is added to the RPM clock driver, we should change this
	 * function to skip registration of xo factor clocks.
	 */

	return _qcom_cc_register_board_clk(dev, path, name, rate, add_factor);
}
EXPORT_SYMBOL_GPL(qcom_cc_register_board_clk);

int qcom_cc_register_sleep_clk(struct device *dev)
{
	return _qcom_cc_register_board_clk(dev, "sleep_clk", "sleep_clk_src",
					   32768, true);
}
EXPORT_SYMBOL_GPL(qcom_cc_register_sleep_clk);

/* Drop 'protected-clocks' from the list of clocks to register */
static void qcom_cc_drop_protected(struct device *dev, struct qcom_cc *cc)
{
	struct device_node *np = dev->of_node;
	struct property *prop;
	const __be32 *p;
	u32 i;

	of_property_for_each_u32(np, "protected-clocks", prop, p, i) {
		if (i >= cc->num_rclks)
			continue;

		cc->rclks[i] = NULL;
	}
}

/* Set QCOM_CLK_IS_CRITICAL on clocks specified in dt */
static void qcom_cc_set_critical(struct device *dev, struct qcom_cc *cc)
{
	struct of_phandle_args args;
	struct device_node *np;
	struct property *prop;
	const __be32 *p;
	u32 clock_idx;
	u32 i;
	int cnt;

	of_property_for_each_u32(dev->of_node, "qcom,critical-clocks", prop, p, i) {
		if (i >= cc->num_rclks)
			continue;

		if (cc->rclks[i])
			cc->rclks[i]->flags |= QCOM_CLK_IS_CRITICAL;
	}

	of_property_for_each_u32(dev->of_node, "qcom,critical-devices", prop, p, i) {
		for (np = of_find_node_by_phandle(i); np; np = of_get_parent(np)) {
			if (!of_property_read_bool(np, "clocks")) {
				of_node_put(np);
				continue;
			}

			cnt = of_count_phandle_with_args(np, "clocks", "#clock-cells");

			for (i = 0; i < cnt; i++) {
				of_parse_phandle_with_args(np, "clocks", "#clock-cells",
							   i, &args);
				clock_idx = args.args[0];

				if (args.np != dev->of_node || clock_idx >= cc->num_rclks)
					continue;

				if (cc->rclks[clock_idx])
					cc->rclks[clock_idx]->flags |= QCOM_CLK_IS_CRITICAL;
				of_node_put(args.np);
			}

			of_node_put(np);
		}
	}
}

static struct clk_hw *qcom_cc_clk_hw_get(struct of_phandle_args *clkspec,
					 void *data)
{
	struct qcom_cc *cc = data;
	unsigned int idx = clkspec->args[0];

	if (idx < cc->num_clk_hws && cc->clk_hws[idx])
		return cc->clk_hws[idx];

	if (idx >= cc->num_rclks) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return cc->rclks[idx] ? &cc->rclks[idx]->hw : NULL;
}

int qcom_cc_really_probe(struct platform_device *pdev,
			 const struct qcom_cc_desc *desc, struct regmap *regmap)
{
	int i, ret;
	struct device *dev = &pdev->dev;
	struct qcom_reset_controller *reset;
	struct qcom_cc *cc;
	struct gdsc_desc *scd;
	size_t num_clks = desc->num_clks;
	struct clk_regmap **rclks = desc->clks;
	size_t num_clk_hws = desc->num_clk_hws;
	struct clk_hw **clk_hws = desc->clk_hws;

	cc = devm_kzalloc(dev, sizeof(*cc), GFP_KERNEL);
	if (!cc)
		return -ENOMEM;

	reset = &cc->reset;
	reset->rcdev.of_node = dev->of_node;
	reset->rcdev.ops = &qcom_reset_ops;
	reset->rcdev.owner = dev->driver->owner;
	reset->rcdev.nr_resets = desc->num_resets;
	reset->regmap = regmap;
	reset->reset_map = desc->resets;

	ret = clk_regulator_init(&pdev->dev, desc);
	if (ret)
		return ret;

	ret = clk_vdd_proxy_vote(&pdev->dev, desc);
	if (ret)
		goto deinit_clk_regulator;

	if (desc->num_resets) {
		ret = devm_reset_controller_register(dev, &reset->rcdev);
		if (ret)
			goto proxy_unvote;
	}

	if (desc->gdscs && desc->num_gdscs) {
		scd = devm_kzalloc(dev, sizeof(*scd), GFP_KERNEL);
		if (!scd) {
			ret = -ENOMEM;
			goto proxy_unvote;
		}
		scd->dev = dev;
		scd->scs = desc->gdscs;
		scd->num = desc->num_gdscs;
		ret = gdsc_register(scd, &reset->rcdev, regmap);
		if (ret)
			goto proxy_unvote;
		ret = devm_add_action_or_reset(dev, qcom_cc_gdsc_unregister,
					       scd);
		if (ret)
			goto proxy_unvote;
	}

	cc->rclks = rclks;
	cc->num_rclks = num_clks;
	cc->clk_hws = clk_hws;
	cc->num_clk_hws = num_clk_hws;

	qcom_cc_drop_protected(dev, cc);
	qcom_cc_set_critical(dev, cc);

	for (i = 0; i < num_clk_hws; i++) {
		if (!clk_hws[i])
			continue;

		ret = devm_clk_hw_register(dev, clk_hws[i]);
		if (ret)
			goto proxy_unvote;
	}

	for (i = 0; i < num_clks; i++) {
		if (!rclks[i])
			continue;

		ret = devm_clk_register_regmap(dev, rclks[i]);
		if (ret)
			goto proxy_unvote;

		clk_hw_populate_clock_opp_table(dev->of_node, &rclks[i]->hw);

		/*
		 * Critical clocks are enabled by devm_clk_register_regmap()
		 * and registration skipped. So remove from rclks so that the
		 * get() callback returns NULL and client requests are stubbed.
		 */
		if (rclks[i]->flags & QCOM_CLK_IS_CRITICAL)
			rclks[i] = NULL;
	}

	ret = devm_of_clk_add_hw_provider(dev, qcom_cc_clk_hw_get, cc);
	if (ret)
		goto proxy_unvote;

	return 0;

proxy_unvote:
	clk_vdd_proxy_unvote(dev, desc);
deinit_clk_regulator:
	clk_regulator_deinit(desc);
	return ret;
}
EXPORT_SYMBOL_GPL(qcom_cc_really_probe);

int qcom_cc_probe(struct platform_device *pdev, const struct qcom_cc_desc *desc)
{
	struct regmap *regmap;

	regmap = qcom_cc_map(pdev, desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return qcom_cc_really_probe(pdev, desc, regmap);
}
EXPORT_SYMBOL_GPL(qcom_cc_probe);

int qcom_cc_probe_by_index(struct platform_device *pdev, int index,
			   const struct qcom_cc_desc *desc)
{
	struct regmap *regmap;
	void __iomem *base;

	base = devm_platform_ioremap_resource(pdev, index);
	if (IS_ERR(base))
		return -ENOMEM;

	regmap = devm_regmap_init_mmio(&pdev->dev, base, desc->config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return qcom_cc_really_probe(pdev, desc, regmap);
}
EXPORT_SYMBOL_GPL(qcom_cc_probe_by_index);

void qcom_cc_sync_state(struct device *dev, const struct qcom_cc_desc *desc)
{
	dev_info(dev, "sync-state\n");
	clk_sync_state(dev);

	clk_vdd_proxy_unvote(dev, desc);
}
EXPORT_SYMBOL(qcom_cc_sync_state);

int qcom_clk_crm_set_rate(struct clk *clk,
			  enum crm_drv_type client_type, u32 client_idx,
			  u32 pwr_st, unsigned long rate)
{
	struct clk_hw *hw;
	int ret;

	if (!clk)
		return -EINVAL;

	do {
		hw = __clk_get_hw(clk);

		if (clk_is_regmap_clk(hw)) {
			struct clk_regmap *rclk = to_clk_regmap(hw);

			if (rclk->ops && rclk->ops->set_crm_rate) {
				ret = rclk->ops->set_crm_rate(hw, client_type,
							      client_idx, pwr_st, rate);
				return ret;
			}
		}

	} while ((clk = clk_get_parent(hw->clk)));

	return -EINVAL;
}
EXPORT_SYMBOL(qcom_clk_crm_set_rate);

int qcom_clk_get_voltage(struct clk *clk, unsigned long rate)
{
	struct clk_regmap *rclk;
	struct clk_hw *hw = __clk_get_hw(clk);
	int vdd_level;

	if (!clk_is_regmap_clk(hw))
		return -EINVAL;

	rclk = to_clk_regmap(hw);
	vdd_level = clk_find_vdd_level(hw, &rclk->vdd_data, rate);
	if (vdd_level < 0)
		return vdd_level;

	return clk_get_vdd_voltage(&rclk->vdd_data, vdd_level);
}
EXPORT_SYMBOL(qcom_clk_get_voltage);

int qcom_clk_set_flags(struct clk *clk, unsigned long flags)
{
	struct clk_regmap *rclk;
	struct clk_hw *hw;

	if (IS_ERR_OR_NULL(clk))
		return 0;

	hw = __clk_get_hw(clk);
	if (IS_ERR_OR_NULL(hw))
		return -EINVAL;

	if (!clk_is_regmap_clk(hw))
		return -EINVAL;

	rclk = to_clk_regmap(hw);
	if (rclk->ops && rclk->ops->set_flags)
		return rclk->ops->set_flags(hw, flags);

	return 0;
}
EXPORT_SYMBOL(qcom_clk_set_flags);

int qcom_cc_runtime_init(struct platform_device *pdev,
			 struct qcom_cc_desc *desc)
{
	struct device *dev = &pdev->dev;
	struct clk *clk;
	int ret;

	clk = clk_get_optional(dev, "iface");
	if (IS_ERR(clk)) {
		if (PTR_ERR(clk) != -EPROBE_DEFER)
			dev_err(dev, "unable to get iface clock\n");
		return PTR_ERR(clk);
	}
	clk_put(clk);

	ret = clk_regulator_init(dev, desc);
	if (ret)
		return ret;

	desc->path = of_icc_get(dev, NULL);
	if (IS_ERR(desc->path)) {
		if (PTR_ERR(desc->path) != -EPROBE_DEFER)
			dev_err(dev, "error getting path\n");
		ret = PTR_ERR(desc->path);
		goto deinit_clk_regulator;
	}

	platform_set_drvdata(pdev, desc);
	pm_runtime_enable(dev);

	ret = pm_clk_create(dev);
	if (ret)
		goto disable_pm_runtime;

	ret = pm_clk_add(dev, "iface");
	if (ret < 0) {
		dev_err(dev, "failed to acquire iface clock\n");
		goto destroy_pm_clk;
	}

	return 0;

destroy_pm_clk:
	pm_clk_destroy(dev);

disable_pm_runtime:
	pm_runtime_disable(dev);
	icc_put(desc->path);
deinit_clk_regulator:
	clk_regulator_deinit(desc);

	return ret;
}
EXPORT_SYMBOL(qcom_cc_runtime_init);

int qcom_cc_runtime_resume(struct device *dev)
{
	struct qcom_cc_desc *desc = dev_get_drvdata(dev);
	struct clk_vdd_class_data vdd_data = {0};
	int ret;
	int i;

	for (i = 0; i < desc->num_clk_regulators; i++) {
		vdd_data.vdd_class = desc->clk_regulators[i];
		if (!vdd_data.vdd_class)
			continue;

		ret = clk_vote_vdd_level(&vdd_data, 1);
		if (ret) {
			dev_warn(dev, "%s: failed to vote voltage\n", __func__);
			return ret;
		}
	}

	if (desc->path) {
		ret = icc_set_bw(desc->path, 0, 1);
		if (ret) {
			dev_warn(dev, "%s: failed to vote bw\n", __func__);
			return ret;
		}
	}

	ret = pm_clk_resume(dev);
	if (ret)
		dev_warn(dev, "%s: failed to enable clocks\n", __func__);

	return ret;
}
EXPORT_SYMBOL(qcom_cc_runtime_resume);

int qcom_cc_runtime_suspend(struct device *dev)
{
	struct qcom_cc_desc *desc = dev_get_drvdata(dev);
	struct clk_vdd_class_data vdd_data = {0};
	int ret;
	int i;

	ret = pm_clk_suspend(dev);
	if (ret)
		dev_warn(dev, "%s: failed to disable clocks\n", __func__);

	if (desc->path) {
		ret = icc_set_bw(desc->path, 0, 0);
		if (ret)
			dev_warn(dev, "%s: failed to unvote bw\n", __func__);
	}

	for (i = 0; i < desc->num_clk_regulators; i++) {
		vdd_data.vdd_class = desc->clk_regulators[i];
		if (!vdd_data.vdd_class)
			continue;

		ret = clk_unvote_vdd_level(&vdd_data, 1);
		if (ret)
			dev_warn(dev, "%s: failed to unvote voltage\n",
				 __func__);
	}

	return 0;
}
EXPORT_SYMBOL(qcom_cc_runtime_suspend);

static void __exit qcom_clk_exit(void)
{
	clk_debug_exit();
}
module_exit(qcom_clk_exit);

MODULE_DESCRIPTION("Common QCOM clock control library");
MODULE_LICENSE("GPL v2");
