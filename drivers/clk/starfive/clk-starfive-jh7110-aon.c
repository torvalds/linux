// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive JH7110 aon Clock Generator Driver
 *
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 * Author: Xingyu Wu <xingyu.wu@starfivetech.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/starfive-jh7110-clkgen.h>
#include "clk-starfive-jh7110.h"

/* external clocks */
#define JH7110_OSC				(JH7110_CLK_END + 0)
/* aon external clocks */
#define JH7110_GMAC0_RMII_REFIN			(JH7110_CLK_END + 12)
#define JH7110_GMAC0_RGMII_RXIN			(JH7110_CLK_END + 13)
#define JH7110_CLK_RTC				(JH7110_CLK_END + 14)

static const struct jh7110_clk_data jh7110_clk_aon_data[] __initconst = {
	//source
	JH7110__DIV(JH7110_OSC_DIV4, "osc_div4", 4, JH7110_OSC),
	JH7110__MUX(JH7110_AON_APB_FUNC, "aon_apb_func", PARENT_NUMS_2,
			JH7110_OSC_DIV4,
			JH7110_OSC),
	//gmac5
	JH7110_GATE(JH7110_U0_GMAC5_CLK_AHB,
			"u0_dw_gmac5_axi64_clk_ahb",
			GATE_FLAG_NORMAL, JH7110_AON_AHB),
	JH7110_GATE(JH7110_U0_GMAC5_CLK_AXI,
			"u0_dw_gmac5_axi64_clk_axi",
			GATE_FLAG_NORMAL, JH7110_AON_AHB),
	JH7110__DIV(JH7110_GMAC0_RMII_RTX,
			"gmac0_rmii_rtx", 30, JH7110_GMAC0_RMII_REFIN),
	JH7110_GMUX(JH7110_U0_GMAC5_CLK_TX,
			"u0_dw_gmac5_axi64_clk_tx",
			GATE_FLAG_NORMAL, PARENT_NUMS_2,
			JH7110_GMAC0_GTXCLK,
			JH7110_GMAC0_RMII_RTX),
	JH7110__INV(JH7110_U0_GMAC5_CLK_TX_INV,
			"u0_dw_gmac5_axi64_clk_tx_inv",
			JH7110_U0_GMAC5_CLK_TX),
	JH7110__MUX(JH7110_U0_GMAC5_CLK_RX,
			"u0_dw_gmac5_axi64_clk_rx", PARENT_NUMS_2,
			JH7110_GMAC0_RGMII_RXIN,
			JH7110_GMAC0_RMII_RTX),
	JH7110__INV(JH7110_U0_GMAC5_CLK_RX_INV,
			"u0_dw_gmac5_axi64_clk_rx_inv",
			JH7110_U0_GMAC5_CLK_RX),
	//otpc
	JH7110_GATE(JH7110_OTPC_CLK_APB,
			"u0_otpc_clk_apb",
			CLK_IGNORE_UNUSED, JH7110_AON_APB),
	//rtc
	JH7110_GATE(JH7110_RTC_HMS_CLK_APB,
			"u0_rtc_hms_clk_apb",
			CLK_IGNORE_UNUSED, JH7110_AON_APB),
	JH7110__DIV(JH7110_RTC_INTERNAL,
			"rtc_internal", 1022, JH7110_OSC),
	JH7110__MUX(JH7110_RTC_HMS_CLK_OSC32K,
			"u0_rtc_hms_clk_osc32k", PARENT_NUMS_2,
			JH7110_CLK_RTC,
			JH7110_RTC_INTERNAL),
	JH7110_GATE(JH7110_RTC_HMS_CLK_CAL,
			"u0_rtc_hms_clk_cal",
			GATE_FLAG_NORMAL, JH7110_OSC),
};

int __init clk_starfive_jh7110_aon_init(struct platform_device *pdev,
						struct jh7110_clk_priv *priv)
{
	unsigned int idx;
	int ret = 0;

	priv->aon_base = devm_platform_ioremap_resource_byname(pdev, "aon");
	if (IS_ERR(priv->aon_base))
		return PTR_ERR(priv->aon_base);

	priv->pll[PLL_OF(JH7110_U0_GMAC5_CLK_PTP)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_dw_gmac5_axi64_clk_ptp", "gmac0_ptp", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_U0_GMAC5_CLK_RMII)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_dw_gmac5_axi64_clk_rmii",
			"gmac0_rmii_refin", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_AON_SYSCON_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_aon_syscon_pclk", "aon_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_AON_IOMUX_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_aon_iomux_pclk", "aon_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_AON_CRG_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_aon_crg_pclk", "aon_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_PMU_CLK_APB)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_pmu_clk_apb", "aon_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_PMU_CLK_WKUP)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_pmu_clk_wkup", "aon_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_RTC_HMS_CLK_OSC32K_G)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_rtc_hms_clk_osc32k_g",
			"u0_rtc_hms_clk_osc32k", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_32K_OUT)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"32k_out", "clk_rtc", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_RESET0_CTRL_CLK_SRC)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_reset_ctrl_clk_src", "osc", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_PCLK_MUX_FUNC_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u1_pclk_mux_func_pclk", "aon_apb_func", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_PCLK_MUX_BIST_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u1_pclk_mux_bist_pclk", "bist_apb", 0, 1, 1);

	for (idx = JH7110_CLK_STG_REG_END; idx < JH7110_CLK_REG_END; idx++) {
		u32 max = jh7110_clk_aon_data[idx].max;
		struct clk_parent_data parents[4] = {};
		struct clk_init_data init = {
			.name = jh7110_clk_aon_data[idx].name,
			.ops = starfive_jh7110_clk_ops(max),
			.parent_data = parents,
			.num_parents = ((max & JH7110_CLK_MUX_MASK) >>
					JH7110_CLK_MUX_SHIFT) + 1,
			.flags = jh7110_clk_aon_data[idx].flags,
		};
		struct jh7110_clk *clk = &priv->reg[idx];
		unsigned int i;

		for (i = 0; i < init.num_parents; i++) {
			unsigned int pidx = jh7110_clk_aon_data[idx].parents[i];

			if (pidx < JH7110_CLK_REG_END)
				parents[i].hw = &priv->reg[pidx].hw;
			else if ((pidx < JH7110_CLK_END) &&
				(pidx > JH7110_RTC_HMS_CLK_CAL))
				parents[i].hw = priv->pll[PLL_OF(pidx)];
			else if (pidx == JH7110_OSC)
				parents[i].fw_name = "osc";
			else if (pidx == JH7110_GMAC0_RMII_REFIN)
				parents[i].fw_name = "gmac0_rmii_refin";
			else if (pidx == JH7110_GMAC0_RGMII_RXIN)
				parents[i].fw_name = "gmac0_rgmii_rxin";
			else if (pidx == JH7110_CLK_RTC)
				parents[i].fw_name = "clk_rtc";
		}

		clk->hw.init = &init;
		clk->idx = idx;
		clk->max_div = max & JH7110_CLK_DIV_MASK;
		clk->reg_flags = JH7110_CLK_AON_FLAG;

		ret = devm_clk_hw_register(priv->dev, &clk->hw);
		if (ret)
			return ret;
	}

	dev_dbg(&pdev->dev, "starfive JH7110 clk_aon init successfully.");
	return 0;
}
