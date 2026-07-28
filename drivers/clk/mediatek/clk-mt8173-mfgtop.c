// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 Google LLC
 * Author: Chen-Yu Tsai <wenst@chromium.org>
 *
 * Based on driver in downstream ChromeOS v5.15 kernel.
 *
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Chiawen Lee <chiawen.lee@mediatek.com>
 */

#include <dt-bindings/clock/mt8173-clk.h>

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs mfg_cg_regs = {
	.sta_ofs = 0x0000,
	.clr_ofs = 0x0008,
	.set_ofs = 0x0004,
};

#define GATE_MFG(_id, _name, _parent, _shift, _flags)	\
	GATE_MTK_FLAGS(_id, _name, _parent, &mfg_cg_regs, _shift, &mtk_clk_gate_ops_setclr, _flags)

/* TODO: The block actually has dividers for the core and mem clocks. */
static const struct mtk_gate mfg_clks[] = {
	GATE_MFG(CLK_MFG_AXI, "mfg_axi", "axi_mfg_in_sel", 0, CLK_SET_RATE_PARENT),
	GATE_MFG(CLK_MFG_MEM, "mfg_mem", "mem_mfg_in_sel", 1, CLK_SET_RATE_PARENT),
	GATE_MFG(CLK_MFG_G3D, "mfg_g3d", "mfg_sel", 2, CLK_SET_RATE_PARENT),
	GATE_MFG(CLK_MFG_26M, "mfg_26m", "clk26m", 3, 0),
};

struct mt8173_mfgtop_data {
	struct clk_hw_onecell_data *clk_data;
	struct regmap *regmap;
	struct generic_pm_domain genpd;
	struct of_phandle_args parent_pd, child_pd;
	struct clk *clk_26m;
};

/* Delay count in clock cycles */
#define MFG_ACTIVE_POWER_CON0	0x24
 #define RST_B_DELAY_CNT	GENMASK(7, 0)	/* pwr_rst_b de-assert delay during power-up */
 #define CLK_EN_DELAY_CNT	GENMASK(15, 8)	/* CLK_DIS deassert delay during power-up */
 #define CLK_DIS_DELAY_CNT	GENMASK(23, 16)	/* CLK_DIS assert delay during power-down */
 #define FORCE_ABORT		BIT(30)		/* write 1 to force abort a power event */
 #define ACTIVE_PWRCTL_EN	BIT(31)		/* enable ACTIVE_POWER */

#define MFG_ACTIVE_POWER_CON1	0x28
 #define PWR_ON_S_DELAY_CNT	GENMASK(7, 0)	/* pwr_on_s assert delay during power-up */
 #define ISO_DELAY_CNT		GENMASK(15, 8)	/* ISO assert delay during power-down */
 #define ISOOFF_DELAY_CNT	GENMASK(23, 16)	/* ISO de-assert delay during power-up */
 #define RST_DELAY_CNT		GENMASK(31, 24) /* pwr_rsb_b assert delay during power-down */

static int clk_mt8173_mfgtop_power_on(struct generic_pm_domain *domain)
{
	struct mt8173_mfgtop_data *data = container_of(domain, struct mt8173_mfgtop_data, genpd);
	int ret;

	/* drives internal power management */
	ret = clk_prepare_enable(data->clk_26m);
	if (ret)
		return ret;

	/* Power on/off delays for various signals */
	regmap_write(data->regmap, MFG_ACTIVE_POWER_CON0,
		     FIELD_PREP(RST_B_DELAY_CNT, 77) |
		     FIELD_PREP(CLK_EN_DELAY_CNT, 61) |
		     FIELD_PREP(CLK_DIS_DELAY_CNT, 60) |
		     FIELD_PREP(ACTIVE_PWRCTL_EN, 0));
	regmap_write(data->regmap, MFG_ACTIVE_POWER_CON1,
		     FIELD_PREP(PWR_ON_S_DELAY_CNT, 11) |
		     FIELD_PREP(ISO_DELAY_CNT, 68) |
		     FIELD_PREP(ISOOFF_DELAY_CNT, 69) |
		     FIELD_PREP(RST_DELAY_CNT, 77));

	/* Magic numbers related to core switch sequence and delays */
	regmap_write(data->regmap, 0xe0, 0x7a710184);
	regmap_write(data->regmap, 0xe4, 0x835f6856);
	regmap_write(data->regmap, 0xe8, 0x002b0234);
	regmap_write(data->regmap, 0xec, 0x80000000);
	regmap_write(data->regmap, 0xa0, 0x08000000);

	return 0;
}

static int clk_mt8173_mfgtop_power_off(struct generic_pm_domain *domain)
{
	struct mt8173_mfgtop_data *data = container_of(domain, struct mt8173_mfgtop_data, genpd);

	/* Magic numbers related to core switch sequence and delays */
	regmap_write(data->regmap, 0xec, 0);

	/* drives internal power management */
	clk_disable_unprepare(data->clk_26m);

	return 0;
}

static int clk_mt8173_mfgtop_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct mt8173_mfgtop_data *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);

	data->clk_data = mtk_devm_alloc_clk_data(dev, ARRAY_SIZE(mfg_clks));
	if (!data->clk_data)
		return -ENOMEM;

	/* MTK clock gates also uses regmap */
	data->regmap = device_node_to_regmap(node);
	if (IS_ERR(data->regmap))
		return dev_err_probe(dev, PTR_ERR(data->regmap), "Failed to get regmap\n");

	data->child_pd.np = node;
	data->child_pd.args_count = 0;
	ret = of_parse_phandle_with_args(node, "power-domains", "#power-domain-cells", 0,
					 &data->parent_pd);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to parse power domain\n");

	devm_pm_runtime_enable(dev);
	/*
	 * Do a pm_runtime_resume_and_get() to workaround a possible
	 * deadlock between clk_register() and the genpd framework.
	 */
	ret = pm_runtime_resume_and_get(dev);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to runtime resume device\n");
		goto put_of_node;
	}

	ret = mtk_clk_register_gates(dev, node, mfg_clks, ARRAY_SIZE(mfg_clks),
				     data->clk_data);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to register clock gates\n");
		goto put_pm_runtime;
	}

	data->clk_26m = clk_hw_get_clk(data->clk_data->hws[CLK_MFG_26M], "26m");
	if (IS_ERR(data->clk_26m)) {
		ret = dev_err_probe(dev, PTR_ERR(data->clk_26m), "Failed to get 26 MHz clock\n");
		goto unregister_clks;
	}

	ret = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, data->clk_data);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to add clk OF provider\n");
		goto put_26m_clk;
	}

	data->genpd.name = "mfg-top";
	data->genpd.power_on = clk_mt8173_mfgtop_power_on;
	data->genpd.power_off = clk_mt8173_mfgtop_power_off;
	ret = pm_genpd_init(&data->genpd, NULL, true);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to add power domain\n");
		goto del_clk_provider;
	}

	ret = of_genpd_add_provider_simple(node, &data->genpd);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to add power domain OF provider\n");
		goto remove_pd;
	}

	ret = of_genpd_add_subdomain(&data->parent_pd, &data->child_pd);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to link PM domains\n");
		goto del_pd_provider;
	}

	pm_runtime_put(dev);
	return 0;

del_pd_provider:
	of_genpd_del_provider(node);
remove_pd:
	pm_genpd_remove(&data->genpd);
del_clk_provider:
	of_clk_del_provider(node);
put_26m_clk:
	clk_put(data->clk_26m);
unregister_clks:
	mtk_clk_unregister_gates(mfg_clks, ARRAY_SIZE(mfg_clks), data->clk_data);
put_pm_runtime:
	pm_runtime_put_sync(dev);
put_of_node:
	of_node_put(data->parent_pd.np);
	return ret;
}

static void clk_mt8173_mfgtop_remove(struct platform_device *pdev)
{
	struct mt8173_mfgtop_data *data = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;

	of_genpd_remove_subdomain(&data->parent_pd, &data->child_pd);
	of_genpd_del_provider(node);
	pm_genpd_remove(&data->genpd);
	of_clk_del_provider(node);
	clk_put(data->clk_26m);
	mtk_clk_unregister_gates(mfg_clks, ARRAY_SIZE(mfg_clks), data->clk_data);
	of_node_put(data->parent_pd.np);
}

static const struct of_device_id of_match_clk_mt8173_mfgtop[] = {
	{ .compatible = "mediatek,mt8173-mfgtop" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8173_mfgtop);

static struct platform_driver clk_mt8173_mfgtop_drv = {
	.probe = clk_mt8173_mfgtop_probe,
	.remove = clk_mt8173_mfgtop_remove,
	.driver = {
		.name = "clk-mt8173-mfgtop",
		.of_match_table = of_match_clk_mt8173_mfgtop,
	},
};
module_platform_driver(clk_mt8173_mfgtop_drv);

MODULE_DESCRIPTION("MediaTek MT8173 mfgtop clock driver");
MODULE_LICENSE("GPL");
