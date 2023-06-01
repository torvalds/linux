// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive JH7110 stg Clock Generator Driver
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

static const struct jh7110_clk_data jh7110_clk_stg_data[] __initconst = {
	//hifi4
	JH7110_GATE(JH7110_HIFI4_CLK_CORE, "u0_hifi4_clk_core",
			GATE_FLAG_NORMAL, JH7110_HIFI4_CORE),
	//usb
	JH7110_GATE(JH7110_USB0_CLK_USB_APB, "u0_cdn_usb_clk_usb_apb",
			GATE_FLAG_NORMAL, JH7110_STG_APB),
	JH7110_GATE(JH7110_USB0_CLK_UTMI_APB, "u0_cdn_usb_clk_utmi_apb",
			GATE_FLAG_NORMAL, JH7110_STG_APB),
	JH7110_GATE(JH7110_USB0_CLK_AXI, "u0_cdn_usb_clk_axi",
			GATE_FLAG_NORMAL, JH7110_STG_AXIAHB),
	JH7110_GDIV(JH7110_USB0_CLK_LPM, "u0_cdn_usb_clk_lpm",
			GATE_FLAG_NORMAL, 2, JH7110_OSC),
	JH7110_GDIV(JH7110_USB0_CLK_STB, "u0_cdn_usb_clk_stb",
			GATE_FLAG_NORMAL, 4, JH7110_OSC),
	JH7110_GATE(JH7110_USB0_CLK_APP_125, "u0_cdn_usb_clk_app_125",
			GATE_FLAG_NORMAL, JH7110_USB_125M),
	JH7110__DIV(JH7110_USB0_REFCLK, "u0_cdn_usb_refclk", 2, JH7110_OSC),
	//pci-e
	JH7110_GATE(JH7110_PCIE0_CLK_AXI_MST0, "u0_plda_pcie_clk_axi_mst0",
			GATE_FLAG_NORMAL, JH7110_STG_AXIAHB),
	JH7110_GATE(JH7110_PCIE0_CLK_APB, "u0_plda_pcie_clk_apb",
			GATE_FLAG_NORMAL, JH7110_STG_APB),
	JH7110_GATE(JH7110_PCIE0_CLK_TL, "u0_plda_pcie_clk_tl",
			GATE_FLAG_NORMAL, JH7110_STG_AXIAHB),
	JH7110_GATE(JH7110_PCIE1_CLK_AXI_MST0, "u1_plda_pcie_clk_axi_mst0",
			GATE_FLAG_NORMAL, JH7110_STG_AXIAHB),
	JH7110_GATE(JH7110_PCIE1_CLK_APB, "u1_plda_pcie_clk_apb",
			GATE_FLAG_NORMAL, JH7110_STG_APB),
	JH7110_GATE(JH7110_PCIE1_CLK_TL, "u1_plda_pcie_clk_tl",
			GATE_FLAG_NORMAL, JH7110_STG_AXIAHB),
	JH7110_GATE(JH7110_PCIE01_SLV_DEC_MAINCLK, "u0_pcie01_slv_dec_mainclk",
			CLK_IGNORE_UNUSED, JH7110_STG_AXIAHB),
	//security
	JH7110_GATE(JH7110_SEC_HCLK, "u0_sec_top_hclk",
			GATE_FLAG_NORMAL, JH7110_STG_AXIAHB),
	JH7110_GATE(JH7110_SEC_MISCAHB_CLK, "u0_sec_top_miscahb_clk",
			GATE_FLAG_NORMAL, JH7110_STG_AXIAHB),
	//stg mtrx
	JH7110_GATE(JH7110_STG_MTRX_GRP0_CLK_MAIN, "u0_stg_mtrx_grp0_clk_main",
			CLK_IGNORE_UNUSED, JH7110_CPU_BUS),
	JH7110_GATE(JH7110_STG_MTRX_GRP0_CLK_BUS, "u0_stg_mtrx_grp0_clk_bus",
			CLK_IGNORE_UNUSED, JH7110_NOCSTG_BUS),
	JH7110_GATE(JH7110_STG_MTRX_GRP0_CLK_STG, "u0_stg_mtrx_grp0_clk_stg",
			CLK_IGNORE_UNUSED, JH7110_STG_AXIAHB),
	JH7110_GATE(JH7110_STG_MTRX_GRP1_CLK_MAIN, "u0_stg_mtrx_grp1_clk_main",
			CLK_IGNORE_UNUSED, JH7110_CPU_BUS),
	JH7110_GATE(JH7110_STG_MTRX_GRP1_CLK_BUS, "u0_stg_mtrx_grp1_clk_bus",
			CLK_IGNORE_UNUSED, JH7110_NOCSTG_BUS),
	JH7110_GATE(JH7110_STG_MTRX_GRP1_CLK_STG, "u0_stg_mtrx_grp1_clk_stg",
			CLK_IGNORE_UNUSED, JH7110_STG_AXIAHB),
	JH7110_GATE(JH7110_STG_MTRX_GRP1_CLK_HIFI, "u0_stg_mtrx_grp1_clk_hifi",
			CLK_IGNORE_UNUSED, JH7110_HIFI4_AXI),
	//e24_rvpi
	JH7110_GDIV(JH7110_E2_RTC_CLK, "u0_e2_sft7110_rtc_clk",
			GATE_FLAG_NORMAL, 24, JH7110_OSC),
	JH7110_GATE(JH7110_E2_CLK_CORE, "u0_e2_sft7110_clk_core",
			CLK_IGNORE_UNUSED, JH7110_STG_AXIAHB),
	JH7110_GATE(JH7110_E2_CLK_DBG, "u0_e2_sft7110_clk_dbg",
			GATE_FLAG_NORMAL, JH7110_STG_AXIAHB),
	//dw_sgdma1p
	JH7110_GATE(JH7110_DMA1P_CLK_AXI, "u0_dw_dma1p_8ch_56hs_clk_axi",
			GATE_FLAG_NORMAL, JH7110_STG_AXIAHB),
	JH7110_GATE(JH7110_DMA1P_CLK_AHB, "u0_dw_dma1p_8ch_56hs_clk_ahb",
			GATE_FLAG_NORMAL, JH7110_STG_AXIAHB),
};

int __init clk_starfive_jh7110_stg_init(struct platform_device *pdev,
						struct jh7110_clk_priv *priv)
{
	unsigned int idx;
	int ret = 0;

	priv->stg_base = devm_platform_ioremap_resource_byname(pdev, "stg");
	if (IS_ERR(priv->stg_base))
		return PTR_ERR(priv->stg_base);

	priv->pll[PLL_OF(JH7110_PCIE0_CLK_AXI_SLV0)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_plda_pcie_clk_axi_slv0",
			"u0_plda_pcie_clk_axi_mst0", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_PCIE0_CLK_AXI_SLV)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_plda_pcie_clk_axi_slv",
			"u0_plda_pcie_clk_axi_mst0", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_PCIE0_CLK_OSC)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_plda_pcie_clk_osc", "osc", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_PCIE1_CLK_AXI_SLV0)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u1_plda_pcie_clk_axi_slv0",
			"u1_plda_pcie_clk_axi_mst0", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_PCIE1_CLK_AXI_SLV)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u1_plda_pcie_clk_axi_slv",
			"u1_plda_pcie_clk_axi_mst0", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_PCIE1_CLK_OSC)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u1_plda_pcie_clk_osc", "osc", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_E2_IRQ_SYNC_CLK_CORE)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_e2_sft7110_irq_sync_clk_core",
			"stg_axiahb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_STG_CRG_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_stg_crg_pclk", "stg_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_STG_SYSCON_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_stg_syscon_pclk", "stg_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_STG_APB)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"stg_apb", "apb_bus", 0, 1, 1);

	for (idx = JH7110_CLK_SYS_REG_END; idx < JH7110_CLK_STG_REG_END; idx++) {
		u32 max = jh7110_clk_stg_data[idx].max;
		struct clk_parent_data parents[4] = {};
		struct clk_init_data init = {
			.name = jh7110_clk_stg_data[idx].name,
			.ops = starfive_jh7110_clk_ops(max),
			.parent_data = parents,
			.num_parents = ((max & JH7110_CLK_MUX_MASK) >>
					JH7110_CLK_MUX_SHIFT) + 1,
			.flags = jh7110_clk_stg_data[idx].flags,
		};
		struct jh7110_clk *clk = &priv->reg[idx];
		unsigned int i;

		for (i = 0; i < init.num_parents; i++) {
			unsigned int pidx = jh7110_clk_stg_data[idx].parents[i];

			if (pidx < JH7110_CLK_REG_END )
				parents[i].hw = &priv->reg[pidx].hw;
			else if ((pidx < JH7110_CLK_STG_END) &&
				(pidx > (JH7110_CLK_SYS_END - 1)))
				parents[i].hw = priv->pll[PLL_OF(pidx)];
			else if (pidx == JH7110_OSC)
				parents[i].fw_name = "osc";
		}

		clk->hw.init = &init;
		clk->idx = idx;
		clk->max_div = max & JH7110_CLK_DIV_MASK;
		clk->reg_flags = JH7110_CLK_STG_FLAG;

		ret = devm_clk_hw_register(priv->dev, &clk->hw);
		if (ret)
			return ret;
	}

	dev_dbg(&pdev->dev, "starfive JH7110 clk_stg init successfully.");
	return 0;
}
