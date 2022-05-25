// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive JH7110 vout Clock Driver
 *
 * Copyright (C) 2022 Xingyu Wu <xingyu.wu@starfivetech.com>
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
#include <soc/starfive/jh7110_pmu.h>
#include <linux/reset.h>

#include <dt-bindings/clock/starfive-jh7110-vout.h>
#include "clk-starfive-jh7110.h"

static const struct jh7110_clk_data jh7110_clk_vout_data[] __initconst = {
	//divider
	JH7110__DIV(JH7110_APB, "apb", 8, JH7110_DISP_AHB),
	JH7110__DIV(JH7110_DC8200_PIX0, "dc8200_pix0", 63, JH7110_DISP_ROOT),
	JH7110__DIV(JH7110_DSI_SYS, "dsi_sys", 31, JH7110_DISP_ROOT),
	JH7110__DIV(JH7110_TX_ESC, "tx_esc", 31, JH7110_DISP_AHB),
	//dc8200
	JH7110_GATE(JH7110_U0_DC8200_CLK_AXI, "u0_dc8200_clk_axi",
			GATE_FLAG_NORMAL, JH7110_DISP_AXI),
	JH7110_GATE(JH7110_U0_DC8200_CLK_CORE, "u0_dc8200_clk_core",
			GATE_FLAG_NORMAL, JH7110_DISP_AXI),
	JH7110_GATE(JH7110_U0_DC8200_CLK_AHB, "u0_dc8200_clk_ahb",
			GATE_FLAG_NORMAL, JH7110_DISP_AHB),
	JH7110_GMUX(JH7110_U0_DC8200_CLK_PIX0, "u0_dc8200_clk_pix0",
			GATE_FLAG_NORMAL, PARENT_NUMS_2,
			JH7110_DC8200_PIX0,
			JH7110_HDMITX0_PIXELCLK),
	JH7110_GMUX(JH7110_U0_DC8200_CLK_PIX1, "u0_dc8200_clk_pix1",
			GATE_FLAG_NORMAL, PARENT_NUMS_2,
			JH7110_DC8200_PIX0,
			JH7110_HDMITX0_PIXELCLK),
	
	JH7110_GMUX(JH7110_DOM_VOUT_TOP_LCD_CLK, "dom_vout_top_lcd_clk",
			GATE_FLAG_NORMAL, PARENT_NUMS_2,
			JH7110_U0_DC8200_CLK_PIX0_OUT,
			JH7110_U0_DC8200_CLK_PIX1_OUT),
	//dsiTx
	JH7110_GATE(JH7110_U0_CDNS_DSITX_CLK_APB, "u0_cdns_dsiTx_clk_apb",
			GATE_FLAG_NORMAL, JH7110_DSI_SYS),
	JH7110_GATE(JH7110_U0_CDNS_DSITX_CLK_SYS, "u0_cdns_dsiTx_clk_sys",
			GATE_FLAG_NORMAL, JH7110_DSI_SYS),
	JH7110_GMUX(JH7110_U0_CDNS_DSITX_CLK_DPI, "u0_cdns_dsiTx_clk_api",
			GATE_FLAG_NORMAL, PARENT_NUMS_2,
			JH7110_DC8200_PIX0,
			JH7110_HDMITX0_PIXELCLK),
	JH7110_GATE(JH7110_U0_CDNS_DSITX_CLK_TXESC, "u0_cdns_dsiTx_clk_txesc",
			GATE_FLAG_NORMAL, JH7110_TX_ESC),
	//mipitx DPHY
	JH7110_GATE(JH7110_U0_MIPITX_DPHY_CLK_TXESC, "u0_mipitx_dphy_clk_txesc",
			GATE_FLAG_NORMAL, JH7110_TX_ESC),
	//hdmi
	JH7110_GATE(JH7110_U0_HDMI_TX_CLK_MCLK, "u0_hdmi_tx_clk_mclk",
			GATE_FLAG_NORMAL, JH7110_HDMITX0_MCLK),
	JH7110_GATE(JH7110_U0_HDMI_TX_CLK_BCLK, "u0_hdmi_tx_clk_bclk",
			GATE_FLAG_NORMAL, JH7110_HDMITX0_SCK),
	JH7110_GATE(JH7110_U0_HDMI_TX_CLK_SYS, "u0_hdmi_tx_clk_sys",
			GATE_FLAG_NORMAL, JH7110_DISP_APB),
};

static struct clk_hw *jh7110_vout_clk_get(struct of_phandle_args *clkspec,
					void *data)
{
	struct jh7110_clk_priv *priv = data;
	unsigned int idx = clkspec->args[0];

	if (idx < JH7110_CLK_VOUT_REG_END)
		return &priv->reg[idx].hw;

	if (idx < JH7110_CLK_VOUT_END)
		return priv->pll[PLL_OFV(idx)];

	return ERR_PTR(-EINVAL);
}

static int __init clk_starfive_jh7110_vout_probe(struct platform_device *pdev)
{
	struct jh7110_clk_priv *priv;
	unsigned int idx;
	struct clk *clk_vout_src;
	struct clk *clk_vout_top_ahb;
	struct reset_control *rst_vout_src;
	int ret = 0;

	priv = devm_kzalloc(&pdev->dev, struct_size(priv,
				reg, JH7110_DISP_ROOT), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->rmw_lock);
	priv->dev = &pdev->dev;
	priv->vout_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->vout_base))
		return PTR_ERR(priv->vout_base);

	starfive_power_domain_set(POWER_DOMAIN_VOUT, 1);

	clk_vout_src = devm_clk_get(priv->dev, "vout_src");
	if (!IS_ERR(clk_vout_src)){
		ret = clk_prepare_enable(clk_vout_src);
		if(ret){
			dev_err(priv->dev, "clk_vout_src enable failed\n");
			goto clk_src_enable_failed;
		}
	}else{
		dev_err(priv->dev, "clk_vout_src get failed\n");
		return PTR_ERR(clk_vout_src);
	}

	rst_vout_src = devm_reset_control_get_exclusive(
			priv->dev, "vout_src");
	if (!IS_ERR(rst_vout_src)) {
		ret = reset_control_deassert(rst_vout_src);
		if(ret){
			dev_err(priv->dev, "rst_vout_src deassert failed.\n");
			goto rst_src_deassert_failed;
		}
	}else{
		dev_err(priv->dev, "rst_vout_src get failed.\n");
		ret = PTR_ERR(rst_vout_src);
		goto rst_src_get_failed;
	}

	clk_vout_top_ahb = devm_clk_get(priv->dev, "vout_top_ahb");
	if (!IS_ERR(clk_vout_top_ahb)){
		ret = clk_prepare_enable(clk_vout_top_ahb);
		if(ret){
			dev_err(priv->dev, "clk_vout_top_ahb enable failed\n");
			goto clk_ahb_enable_failed;
		}
	}else{
		dev_err(priv->dev, "clk_vout_top_ahb get failed\n");
		ret = PTR_ERR(clk_vout_top_ahb);
		goto clk_ahb_get_failed;
	}

	//source
	priv->pll[PLL_OFV(JH7110_DISP_ROOT)] =
			devm_clk_hw_register_fixed_factor(
			priv->dev, "disp_root",
			"u0_dom_vout_top_clk_dom_vout_top_clk_vout_src",
			0, 1, 1);
	priv->pll[PLL_OFV(JH7110_DISP_AXI)] =
			devm_clk_hw_register_fixed_factor(
			priv->dev, "disp_axi",
			"u0_dom_vout_top_clk_dom_vout_top_clk_vout_axi",
			0, 1, 1);
	priv->pll[PLL_OFV(JH7110_DISP_AHB)] =
			devm_clk_hw_register_fixed_factor(
			priv->dev, "disp_ahb",
			"u0_dom_vout_top_clk_dom_vout_top_clk_vout_ahb",
			0, 1, 1);
	priv->pll[PLL_OFV(JH7110_HDMI_PHY_REF)] =
			devm_clk_hw_register_fixed_factor(
			priv->dev, "hdmi_phy_ref",
			"u0_dom_vout_top_clk_dom_vout_top_clk_hdmiphy_ref",
			0, 1, 1);
	priv->pll[PLL_OFV(JH7110_HDMITX0_MCLK)] =
			devm_clk_hw_register_fixed_factor(
			priv->dev, "hdmitx0_mclk",
			"u0_dom_vout_top_clk_dom_vout_top_clk_hdmitx0_mclk",
			0, 1, 1);
	priv->pll[PLL_OFV(JH7110_HDMITX0_SCK)] =
			devm_clk_hw_register_fixed_factor(
			priv->dev, "hdmitx0_sck",
			"u0_dom_vout_top_clk_dom_vout_top_clk_hdmitx0_bclk",
			0, 1, 1);
	
	priv->pll[PLL_OFV(JH7110_MIPI_DPHY_REF)] =
			devm_clk_hw_register_fixed_factor(
			priv->dev, "mipi_dphy_ref",
			"u0_dom_vout_top_clk_dom_vout_top_clk_mipiphy_ref",
			0, 1, 1);
	//divider
	priv->pll[PLL_OFV(JH7110_U0_PCLK_MUX_BIST_PCLK)] =
			devm_clk_hw_register_fixed_factor(
			priv->dev, "u0_pclk_mux_bist_pclk",
			"u0_dom_vout_top_clk_dom_vout_top_bist_pclk",
			0, 1, 1);
	priv->pll[PLL_OFV(JH7110_DISP_APB)] =
			clk_hw_register_fixed_rate(priv->dev,
			"disp_apb", NULL, 0, 51200000);
	priv->pll[PLL_OFV(JH7110_U0_PCLK_MUX_FUNC_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_pclk_mux_func_pclk", "apb", 0, 1, 1);
	//bus
	priv->pll[PLL_OFV(JH7110_U0_DOM_VOUT_CRG_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_dom_vout_crg_pclk", "disp_apb", 0, 1, 1);
	priv->pll[PLL_OFV(JH7110_U0_DOM_VOUT_SYSCON_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_dom_vout_syscon_pclk", "disp_apb", 0, 1, 1);
	priv->pll[PLL_OFV(JH7110_U0_SAIF_AMBA_DOM_VOUT_AHB_DEC_CLK_AHB)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_saif_amba_dom_vout_ahb_dec_clk_ahb",
			"disp_ahb", 0, 1, 1);
	priv->pll[PLL_OFV(JH7110_U0_AHB2APB_CLK_AHB)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_ahb2apb_clk_ahb", "disp_ahb", 0, 1, 1);
	priv->pll[PLL_OFV(JH7110_U0_P2P_ASYNC_CLK_APBS)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_p2p_async_clk_apbs", "disp_apb", 0, 1, 1);
	//dsiTx
	priv->pll[PLL_OFV(JH7110_U0_CDNS_DSITX_CLK_RXESC)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_cdns_dsiTx_clk_rxesc",
			"mipitx_dphy_rxesc", 0, 1, 1);
	priv->pll[PLL_OFV(JH7110_U0_CDNS_DSITX_CLK_TXBYTEHS)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_cdns_dsiTx_clk_txbytehs",
			"mipitx_dphy_txbytehs", 0, 1, 1);
	//mipitx DPHY
	priv->pll[PLL_OFV(JH7110_U0_MIPITX_DPHY_CLK_SYS)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_mipitx_dphy_clk_sys", "disp_apb", 0, 1, 1);
	priv->pll[PLL_OFV(JH7110_U0_MIPITX_DPHY_CLK_DPHY_REF)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_mipitx_dphy_clk_dphy_ref",
			"mipi_dphy_ref", 0, 1, 1);
	priv->pll[PLL_OFV(JH7110_U0_MIPITX_APBIF_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_mipitx_apbif_pclk", "disp_apb", 0, 1, 1);
	//hdmi
	priv->pll[PLL_OFV(JH7110_HDMI_TX_CLK_REF)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_hdmi_tx_clk_ref", "hdmi_phy_ref", 0, 1, 1);
	
	priv->pll[PLL_OFV(JH7110_U0_DC8200_CLK_PIX0_OUT)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_dc8200_clk_pix0_out",
			"u0_dc8200_clk_pix0", 0, 1, 1);
	priv->pll[PLL_OFV(JH7110_U0_DC8200_CLK_PIX1_OUT)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_dc8200_clk_pix1_out",
			"u0_dc8200_clk_pix1", 0, 1, 1);

	for (idx = 0; idx < JH7110_DISP_ROOT; idx++) {
		u32 max = jh7110_clk_vout_data[idx].max;
		struct clk_parent_data parents[2] = {};
		struct clk_init_data init = {
			.name = jh7110_clk_vout_data[idx].name,
			.ops = starfive_jh7110_clk_ops(max),
			.parent_data = parents,
			.num_parents = ((max & JH7110_CLK_MUX_MASK) \
					>> JH7110_CLK_MUX_SHIFT) + 1,
			.flags = jh7110_clk_vout_data[idx].flags,
		};
		struct jh7110_clk *clk = &priv->reg[idx];
		unsigned int i;

		for (i = 0; i < init.num_parents; i++) {
			unsigned int pidx = jh7110_clk_vout_data[idx].parents[i];

			if (pidx < JH7110_DISP_ROOT)
				parents[i].hw = &priv->reg[pidx].hw;
			else if (pidx < JH7110_CLK_VOUT_END)
				parents[i].hw = \
					priv->pll[PLL_OFV(pidx)];
			else if (pidx == JH7110_HDMITX0_PIXELCLK)
				parents[i].fw_name = "hdmitx0_pixelclk";
			else if (pidx == JH7110_MIPITX_DPHY_RXESC)
				parents[i].fw_name = "mipitx_dphy_rxesc";
			else if (pidx == JH7110_MIPITX_DPHY_TXBYTEHS)
				parents[i].fw_name = "mipitx_dphy_txbytehs";
			else if (pidx == JH7110_U0_DC8200_CLK_PIX0_OUT)
				parents[i].fw_name = "u0_dc8200_clk_pix0_out";
			else if (pidx == JH7110_U0_DC8200_CLK_PIX1_OUT)
				parents[i].fw_name = "u0_dc8200_clk_pix1_out";
		}

		clk->hw.init = &init;
		clk->idx = idx;
		clk->max_div = max & JH7110_CLK_DIV_MASK;
		clk->reg_flags = JH7110_CLK_VOUT_FLAG;

		ret = devm_clk_hw_register(priv->dev, &clk->hw);
		if (ret)
			return ret;
	}

	ret = devm_of_clk_add_hw_provider(priv->dev, jh7110_vout_clk_get, priv);
	if (ret)
		return ret;

	devm_clk_put(priv->dev, clk_vout_src);
	reset_control_put(rst_vout_src);
	devm_clk_put(priv->dev, clk_vout_top_ahb);

	dev_info(&pdev->dev,"starfive JH7110 clk_vout init successfully.");
	return 0;

clk_ahb_enable_failed:
	devm_clk_put(priv->dev, clk_vout_top_ahb);
clk_ahb_get_failed:
	reset_control_assert(rst_vout_src);
rst_src_deassert_failed:
	reset_control_put(rst_vout_src);
rst_src_get_failed:
	clk_disable_unprepare(clk_vout_src);
clk_src_enable_failed:
	devm_clk_put(priv->dev, clk_vout_src);

	return ret;

}

static const struct of_device_id clk_starfive_jh7110_vout_match[] = {	
		{.compatible = "starfive,jh7110-clk-vout" },
		{ /* sentinel */ }
};

static struct platform_driver clk_starfive_jh7110_vout_driver = {
	.probe = clk_starfive_jh7110_vout_probe,
		.driver = {
		.name = "clk-starfive-jh7110-vout",
		.of_match_table = clk_starfive_jh7110_vout_match,
	},
};
module_platform_driver(clk_starfive_jh7110_vout_driver);

MODULE_AUTHOR("Xingyu Wu <xingyu.wu@starfivetech.com>");
MODULE_DESCRIPTION("StarFive JH7110 vout clock driver");
MODULE_LICENSE("GPL");
