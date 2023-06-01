// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive JH7110 Isp Clock Driver
 *
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 * Author: Xingyu Wu <xingyu.wu@starfivetech.com>
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/reset.h>
#include <linux/reset-controller.h>
#include <dt-bindings/clock/starfive-jh7110-isp.h>
#include <linux/pm_runtime.h>

#include "clk-starfive-jh7110.h"

/* external clocks */
#define JH7110_ISP_TOP_CLK_ISPCORE_2X_CLKGEN	(JH7110_CLK_ISP_END + 0)
#define JH7110_ISP_TOP_CLK_ISP_AXI_CLKGEN	(JH7110_CLK_ISP_END + 1)
#define JH7110_ISP_TOP_CLK_BIST_APB_CLKGEN	(JH7110_CLK_ISP_END + 2)
#define JH7110_ISP_TOP_CLK_DVP_CLKGEN		(JH7110_CLK_ISP_END + 3)

struct isp_init_crg {
	int num_clks;
	struct clk_bulk_data *clks;
	struct reset_control *rsts;
};

static const struct jh7110_clk_data jh7110_clk_isp_data[] __initconst = {
	//syscon
	JH7110__DIV(JH7110_DOM4_APB_FUNC, "dom4_apb_func",
			15, JH7110_ISP_TOP_CLK_ISP_AXI_CLKGEN),
	//crg
	JH7110__DIV(JH7110_MIPI_RX0_PXL, "mipi_rx0_pxl",
			8, JH7110_ISP_TOP_CLK_ISPCORE_2X_CLKGEN),
	JH7110__INV(JH7110_DVP_INV, "dvp_inv", JH7110_ISP_TOP_CLK_DVP_CLKGEN),
	//vin
	JH7110__DIV(JH7110_U0_M31DPHY_CFGCLK_IN, "u0_m31dphy_cfgclk_in",
			16, JH7110_ISP_TOP_CLK_ISPCORE_2X_CLKGEN),
	JH7110__DIV(JH7110_U0_M31DPHY_REFCLK_IN, "u0_m31dphy_refclk_in",
			16, JH7110_ISP_TOP_CLK_ISPCORE_2X_CLKGEN),
	JH7110__DIV(JH7110_U0_M31DPHY_TXCLKESC_LAN0, "u0_m31dphy_txclkesc_lan0",
			60, JH7110_ISP_TOP_CLK_ISPCORE_2X_CLKGEN),
	JH7110_GATE(JH7110_U0_VIN_PCLK, "u0_vin_pclk",
			CLK_IGNORE_UNUSED, JH7110_DOM4_APB),
	JH7110__DIV(JH7110_U0_VIN_SYS_CLK, "u0_vin_sys_clk",
			8, JH7110_ISP_TOP_CLK_ISPCORE_2X_CLKGEN),
	JH7110_GATE(JH7110_U0_VIN_PIXEL_CLK_IF0, "u0_vin_pixel_clk_if0",
			CLK_IGNORE_UNUSED, JH7110_MIPI_RX0_PXL),
	JH7110_GATE(JH7110_U0_VIN_PIXEL_CLK_IF1, "u0_vin_pixel_clk_if1",
			CLK_IGNORE_UNUSED, JH7110_MIPI_RX0_PXL),
	JH7110_GATE(JH7110_U0_VIN_PIXEL_CLK_IF2, "u0_vin_pixel_clk_if2",
			CLK_IGNORE_UNUSED, JH7110_MIPI_RX0_PXL),
	JH7110_GATE(JH7110_U0_VIN_PIXEL_CLK_IF3, "u0_vin_pixel_clk_if3",
			CLK_IGNORE_UNUSED, JH7110_MIPI_RX0_PXL),
	JH7110__MUX(JH7110_U0_VIN_CLK_P_AXIWR, "u0_vin_clk_p_axiwr",
			PARENT_NUMS_2,
			JH7110_MIPI_RX0_PXL,
			JH7110_DVP_INV),
	//ispv2_top_wrapper
	JH7110_GMUX(JH7110_U0_ISPV2_TOP_WRAPPER_CLK_C,
			"u0_ispv2_top_wrapper_clk_c",
			CLK_IGNORE_UNUSED, PARENT_NUMS_2,
			JH7110_MIPI_RX0_PXL,
			JH7110_DVP_INV),
};

static struct clk_bulk_data isp_top_clks[] = {
	{ .id = "u0_dom_isp_top_clk_dom_isp_top_clk_ispcore_2x" },
	{ .id = "u0_dom_isp_top_clk_dom_isp_top_clk_isp_axi" },
};

static int jh7110_isp_crg_get(struct device *dev, struct isp_init_crg *crg)
{
	int ret;

	crg->rsts = devm_reset_control_array_get_shared(dev);
	if (IS_ERR(crg->rsts)) {
		dev_err(dev, "rst get failed\n");
		return PTR_ERR(crg->rsts);
	}

	crg->clks = isp_top_clks;
	crg->num_clks = ARRAY_SIZE(isp_top_clks);
	ret = clk_bulk_get(dev, crg->num_clks, crg->clks);
	if (ret) {
		dev_err(dev, "clks get failed: %d\n", ret);
		goto clks_get_failed;
	}

	return 0;

clks_get_failed:
	reset_control_assert(crg->rsts);
	reset_control_put(crg->rsts);

	return ret;
}

static int jh7110_isp_crg_enable(struct device *dev, struct isp_init_crg *crg, bool enable)
{
	int ret;

	dev_dbg(dev, "starfive jh7110 isp clk&rst %sable\n", enable ? "en":"dis");
	if (enable) {
		ret = reset_control_deassert(crg->rsts);
		if (ret) {
			dev_err(dev, "rst deassert failed: %d\n", ret);
			goto crg_failed;
		}

		ret = clk_bulk_prepare_enable(crg->num_clks, crg->clks);
		if (ret) {
			dev_err(dev, "clks enable failed: %d\n", ret);
			goto crg_failed;
		}
	} else {
		clk_bulk_disable_unprepare(crg->num_clks, crg->clks);
	}

	return 0;
crg_failed:
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int clk_isp_system_suspend(struct device *dev)
{
	return pm_runtime_force_suspend(dev);
}

static int clk_isp_system_resume(struct device *dev)
{
	return pm_runtime_force_resume(dev);
}
#endif

#ifdef CONFIG_PM
static int clk_isp_runtime_suspend(struct device *dev)
{
	struct isp_init_crg *crg = dev_get_drvdata(dev);

	return jh7110_isp_crg_enable(dev, crg, false);
}

static int clk_isp_runtime_resume(struct device *dev)
{
	struct isp_init_crg *crg = dev_get_drvdata(dev);

	return jh7110_isp_crg_enable(dev, crg, true);
}
#endif

static const struct dev_pm_ops clk_isp_pm_ops = {
	SET_RUNTIME_PM_OPS(clk_isp_runtime_suspend, clk_isp_runtime_resume, NULL)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(clk_isp_system_suspend, clk_isp_system_resume)
};

static struct clk_hw *jh7110_isp_clk_get(struct of_phandle_args *clkspec,
					void *data)
{
	struct jh7110_clk_priv *priv = data;
	unsigned int idx = clkspec->args[0];

	if (idx < JH7110_CLK_ISP_REG_END)
		return &priv->reg[idx].hw;

	if (idx < JH7110_CLK_ISP_END)
		return priv->pll[PLL_OFI(idx)];

	return ERR_PTR(-EINVAL);
}

static int __init clk_starfive_jh7110_isp_probe(struct platform_device *pdev)
{
	struct jh7110_clk_priv *priv;
	struct isp_init_crg *crg;
	unsigned int idx;
	int ret = 0;

	priv = devm_kzalloc(&pdev->dev, struct_size(priv, reg,
				JH7110_CLK_ISP_END), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->rmw_lock);
	priv->dev = &pdev->dev;
	priv->isp_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->isp_base))
		return PTR_ERR(priv->isp_base);

	crg = devm_kzalloc(&pdev->dev, sizeof(*crg), GFP_KERNEL);
	if (!crg)
		return -ENOMEM;
	dev_set_drvdata(&pdev->dev, crg);

	ret = jh7110_isp_crg_get(&pdev->dev, crg);
	if (ret)
		goto init_failed;

	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, 50);
	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get pm runtime: %d\n", ret);
		goto init_failed;
	}

	priv->pll[PLL_OFI(JH7110_U3_PCLK_MUX_FUNC_PCLK)] =
			devm_clk_hw_register_fixed_factor(
			priv->dev, "u3_pclk_mux_func_pclk",
			"dom4_apb_func", 0, 1, 1);
	priv->pll[PLL_OFI(JH7110_U3_PCLK_MUX_BIST_PCLK)] =
			devm_clk_hw_register_fixed_factor(
			priv->dev, "u3_pclk_mux_bist_pclk",
			"u0_dom_isp_top_clk_dom_isp_top_clk_bist_apb", 0, 1, 1);
	priv->pll[PLL_OFI(JH7110_DOM4_APB)] =
			devm_clk_hw_register_fixed_factor(priv->dev, "dom4_apb",
			"u3_pclk_mux_pclk", 0, 1, 1);
	//vin
	priv->pll[PLL_OFI(JH7110_U0_VIN_PCLK_FREE)] =
			devm_clk_hw_register_fixed_factor(
			priv->dev, "u0_vin_pclk_free",
			"dom4_apb", 0, 1, 1);
	priv->pll[PLL_OFI(JH7110_U0_VIN_CLK_P_AXIRD)] =
			devm_clk_hw_register_fixed_factor(
			priv->dev, "u0_vin_clk_p_axird",
			"mipi_rx0_pxl", 0, 1, 1);
	priv->pll[PLL_OFI(JH7110_U0_VIN_ACLK)] =
			devm_clk_hw_register_fixed_factor(
			priv->dev, "u0_vin_ACLK",
			"u0_dom_isp_top_clk_dom_isp_top_clk_isp_axi", 0, 1, 1);
	priv->pll[PLL_OFI(JH7110_U0_ISPV2_TOP_WRAPPER_CLK_ISP_AXI_IN)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_ispv2_top_wrapper_clk_isp_axi_in",
			"u0_dom_isp_top_clk_dom_isp_top_clk_isp_axi", 0, 1, 1);
	priv->pll[PLL_OFI(JH7110_U0_ISPV2_TOP_WRAPPER_CLK_ISP_X2)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_ispv2_top_wrapper_clk_isp_x2",
			"u0_dom_isp_top_clk_dom_isp_top_clk_ispcore_2x",
			0, 1, 1);
	//wrapper
	priv->pll[PLL_OFI(JH7110_U0_ISPV2_TOP_WRAPPER_CLK_ISP)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_ispv2_top_wrapper_clk_isp",
			"u0_dom_isp_top_clk_dom_isp_top_clk_isp_axi", 0, 1, 1);
	priv->pll[PLL_OFI(JH7110_U0_ISPV2_TOP_WRAPPER_CLK_P)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_ispv2_top_wrapper_clk_p",
			"mipi_rx0_pxl", 0, 1, 1);
	priv->pll[PLL_OFI(JH7110_U0_CRG_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_crg_pclk", "dom4_apb", 0, 1, 1);
	priv->pll[PLL_OFI(JH7110_U0_SYSCON_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_syscon_pclk", "dom4_apb", 0, 1, 1);
	priv->pll[PLL_OFI(JH7110_U0_M31DPHY_APBCFG_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_m31dphy_apbcfg_pclk", "dom4_apb", 0, 1, 1);
	priv->pll[PLL_OFI(JH7110_U0_AXI2APB_BRIDGE_CLK_DOM4_APB)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_axi2apb_bridge_clk_dom4_apb", "dom4_apb", 0, 1, 1);
	priv->pll[PLL_OFI(JH7110_U0_AXI2APB_BRIDGE_ISP_AXI4SLV_CLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_axi2apb_bridge_isp_axi4slv_clk",
			"u0_dom_isp_top_clk_dom_isp_top_clk_isp_axi", 0, 1, 1);
	priv->pll[PLL_OFI(JH7110_U3_PCLK_MUX_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u3_pclk_mux_pclk", "u3_pclk_mux_func_pclk", 0, 1, 1);

	for (idx = 0; idx < JH7110_CLK_ISP_REG_END; idx++) {
		u32 max = jh7110_clk_isp_data[idx].max;
		struct clk_parent_data parents[2] = {};
		struct clk_init_data init = {
			.name = jh7110_clk_isp_data[idx].name,
			.ops = starfive_jh7110_clk_ops(max),
			.parent_data = parents,
			.num_parents = ((max & JH7110_CLK_MUX_MASK) >>
					JH7110_CLK_MUX_SHIFT) + 1,
			.flags = jh7110_clk_isp_data[idx].flags,
		};
		struct jh7110_clk *clk = &priv->reg[idx];
		unsigned int i;
		char *fw_name[4] = {
			"u0_dom_isp_top_clk_dom_isp_top_clk_ispcore_2x",
			"u0_dom_isp_top_clk_dom_isp_top_clk_isp_axi",
			"u0_dom_isp_top_clk_dom_isp_top_clk_bist_apb",
			"u0_dom_isp_top_clk_dom_isp_top_clk_dvp"
		};

		for (i = 0; i < init.num_parents; i++) {
			unsigned int pidx = jh7110_clk_isp_data[idx].parents[i];

			if (pidx < JH7110_CLK_ISP_REG_END)
				parents[i].hw = &priv->reg[pidx].hw;
			else if (pidx < JH7110_CLK_ISP_END)
				parents[i].hw = priv->pll[PLL_OFI(pidx)];
			else if (pidx == JH7110_ISP_TOP_CLK_ISPCORE_2X_CLKGEN)
				parents[i].fw_name = fw_name[0];
			else if (pidx == JH7110_ISP_TOP_CLK_ISP_AXI_CLKGEN)
				parents[i].fw_name = fw_name[1];
			else if (pidx == JH7110_ISP_TOP_CLK_BIST_APB_CLKGEN)
				parents[i].fw_name = fw_name[2];
			else if (pidx == JH7110_ISP_TOP_CLK_DVP_CLKGEN)
				parents[i].fw_name = fw_name[3];
		}

		clk->hw.init = &init;
		clk->idx = idx;
		clk->max_div = max & JH7110_CLK_DIV_MASK;
		clk->reg_flags = JH7110_CLK_ISP_FLAG;

		ret = devm_clk_hw_register(priv->dev, &clk->hw);
		if (ret)
			goto init_failed;
	}

	ret = devm_of_clk_add_hw_provider(priv->dev, jh7110_isp_clk_get, priv);
	if (ret)
		goto init_failed;

	pm_runtime_put_sync(&pdev->dev);

	dev_info(&pdev->dev, "starfive JH7110 clk_isp init successfully.");
	return 0;

init_failed:
	return ret;
}

static const struct of_device_id clk_starfive_jh7110_isp_match[] = {
		{.compatible = "starfive,jh7110-clk-isp" },
		{ /* sentinel */ }
};

static struct platform_driver clk_starfive_jh7110_isp_driver = {
	.probe = clk_starfive_jh7110_isp_probe,
		.driver = {
		.name = "clk-starfive-jh7110-isp",
		.of_match_table = clk_starfive_jh7110_isp_match,
		.pm	= &clk_isp_pm_ops,
	},
};
module_platform_driver(clk_starfive_jh7110_isp_driver);

MODULE_AUTHOR("Xingyu Wu <xingyu.wu@starfivetech.com>");
MODULE_DESCRIPTION("StarFive JH7110 isp clock driver");
MODULE_LICENSE("GPL");
