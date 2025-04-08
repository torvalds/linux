// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive JH7110 System-Top-Group Clock Driver
 *
 * Copyright (C) 2022 Emil Renner Berthing <kernel@esmil.dk>
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/starfive,jh7110-crg.h>

#include "clk-starfive-jh7110.h"

/* external clocks */
#define JH7110_STGCLK_OSC			(JH7110_STGCLK_END + 0)
#define JH7110_STGCLK_HIFI4_CORE		(JH7110_STGCLK_END + 1)
#define JH7110_STGCLK_STG_AXIAHB		(JH7110_STGCLK_END + 2)
#define JH7110_STGCLK_USB_125M			(JH7110_STGCLK_END + 3)
#define JH7110_STGCLK_CPU_BUS			(JH7110_STGCLK_END + 4)
#define JH7110_STGCLK_HIFI4_AXI			(JH7110_STGCLK_END + 5)
#define JH7110_STGCLK_NOCSTG_BUS		(JH7110_STGCLK_END + 6)
#define JH7110_STGCLK_APB_BUS			(JH7110_STGCLK_END + 7)
#define JH7110_STGCLK_EXT_END			(JH7110_STGCLK_END + 8)

static const struct jh71x0_clk_data jh7110_stgclk_data[] = {
	/* hifi4 */
	JH71X0_GATE(JH7110_STGCLK_HIFI4_CLK_CORE, "hifi4_clk_core", 0,
		    JH7110_STGCLK_HIFI4_CORE),
	/* usb */
	JH71X0_GATE(JH7110_STGCLK_USB0_APB, "usb0_apb", 0, JH7110_STGCLK_APB_BUS),
	JH71X0_GATE(JH7110_STGCLK_USB0_UTMI_APB, "usb0_utmi_apb", 0, JH7110_STGCLK_APB_BUS),
	JH71X0_GATE(JH7110_STGCLK_USB0_AXI, "usb0_axi", 0, JH7110_STGCLK_STG_AXIAHB),
	JH71X0_GDIV(JH7110_STGCLK_USB0_LPM, "usb0_lpm", 0, 2, JH7110_STGCLK_OSC),
	JH71X0_GDIV(JH7110_STGCLK_USB0_STB, "usb0_stb", 0, 4, JH7110_STGCLK_OSC),
	JH71X0_GATE(JH7110_STGCLK_USB0_APP_125, "usb0_app_125", 0, JH7110_STGCLK_USB_125M),
	JH71X0__DIV(JH7110_STGCLK_USB0_REFCLK, "usb0_refclk", 2, JH7110_STGCLK_OSC),
	/* pci-e */
	JH71X0_GATE(JH7110_STGCLK_PCIE0_AXI_MST0, "pcie0_axi_mst0", 0,
		    JH7110_STGCLK_STG_AXIAHB),
	JH71X0_GATE(JH7110_STGCLK_PCIE0_APB, "pcie0_apb", 0, JH7110_STGCLK_APB_BUS),
	JH71X0_GATE(JH7110_STGCLK_PCIE0_TL, "pcie0_tl", 0, JH7110_STGCLK_STG_AXIAHB),
	JH71X0_GATE(JH7110_STGCLK_PCIE1_AXI_MST0, "pcie1_axi_mst0", 0,
		    JH7110_STGCLK_STG_AXIAHB),
	JH71X0_GATE(JH7110_STGCLK_PCIE1_APB, "pcie1_apb", 0, JH7110_STGCLK_APB_BUS),
	JH71X0_GATE(JH7110_STGCLK_PCIE1_TL, "pcie1_tl", 0, JH7110_STGCLK_STG_AXIAHB),
	JH71X0_GATE(JH7110_STGCLK_PCIE_SLV_MAIN, "pcie_slv_main", CLK_IS_CRITICAL,
		    JH7110_STGCLK_STG_AXIAHB),
	/* security */
	JH71X0_GATE(JH7110_STGCLK_SEC_AHB, "sec_ahb", 0, JH7110_STGCLK_STG_AXIAHB),
	JH71X0_GATE(JH7110_STGCLK_SEC_MISC_AHB, "sec_misc_ahb", 0, JH7110_STGCLK_STG_AXIAHB),
	/* stg mtrx */
	JH71X0_GATE(JH7110_STGCLK_GRP0_MAIN, "mtrx_grp0_main", CLK_IS_CRITICAL,
		    JH7110_STGCLK_CPU_BUS),
	JH71X0_GATE(JH7110_STGCLK_GRP0_BUS, "mtrx_grp0_bus", CLK_IS_CRITICAL,
		    JH7110_STGCLK_NOCSTG_BUS),
	JH71X0_GATE(JH7110_STGCLK_GRP0_STG, "mtrx_grp0_stg", CLK_IS_CRITICAL,
		    JH7110_STGCLK_STG_AXIAHB),
	JH71X0_GATE(JH7110_STGCLK_GRP1_MAIN, "mtrx_grp1_main", CLK_IS_CRITICAL,
		    JH7110_STGCLK_CPU_BUS),
	JH71X0_GATE(JH7110_STGCLK_GRP1_BUS, "mtrx_grp1_bus", CLK_IS_CRITICAL,
		    JH7110_STGCLK_NOCSTG_BUS),
	JH71X0_GATE(JH7110_STGCLK_GRP1_STG, "mtrx_grp1_stg", CLK_IS_CRITICAL,
		    JH7110_STGCLK_STG_AXIAHB),
	JH71X0_GATE(JH7110_STGCLK_GRP1_HIFI, "mtrx_grp1_hifi", CLK_IS_CRITICAL,
		    JH7110_STGCLK_HIFI4_AXI),
	/* e24_rvpi */
	JH71X0_GDIV(JH7110_STGCLK_E2_RTC, "e2_rtc", 0, 24, JH7110_STGCLK_OSC),
	JH71X0_GATE(JH7110_STGCLK_E2_CORE, "e2_core", 0, JH7110_STGCLK_STG_AXIAHB),
	JH71X0_GATE(JH7110_STGCLK_E2_DBG, "e2_dbg", 0, JH7110_STGCLK_STG_AXIAHB),
	/* dw_sgdma1p */
	JH71X0_GATE(JH7110_STGCLK_DMA1P_AXI, "dma1p_axi", 0, JH7110_STGCLK_STG_AXIAHB),
	JH71X0_GATE(JH7110_STGCLK_DMA1P_AHB, "dma1p_ahb", 0, JH7110_STGCLK_STG_AXIAHB),
};

static int jh7110_stgcrg_probe(struct platform_device *pdev)
{
	struct jh71x0_clk_priv *priv;
	unsigned int idx;
	int ret;

	priv = devm_kzalloc(&pdev->dev, struct_size(priv, reg, JH7110_STGCLK_END),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->rmw_lock);
	priv->num_reg = JH7110_STGCLK_END;
	priv->dev = &pdev->dev;
	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	for (idx = 0; idx < JH7110_STGCLK_END; idx++) {
		u32 max = jh7110_stgclk_data[idx].max;
		struct clk_parent_data parents[4] = {};
		struct clk_init_data init = {
			.name = jh7110_stgclk_data[idx].name,
			.ops = starfive_jh71x0_clk_ops(max),
			.parent_data = parents,
			.num_parents =
				((max & JH71X0_CLK_MUX_MASK) >> JH71X0_CLK_MUX_SHIFT) + 1,
			.flags = jh7110_stgclk_data[idx].flags,
		};
		struct jh71x0_clk *clk = &priv->reg[idx];
		const char *fw_name[JH7110_STGCLK_EXT_END - JH7110_STGCLK_END] = {
			"osc",
			"hifi4_core",
			"stg_axiahb",
			"usb_125m",
			"cpu_bus",
			"hifi4_axi",
			"nocstg_bus",
			"apb_bus"
		};
		unsigned int i;

		for (i = 0; i < init.num_parents; i++) {
			unsigned int pidx = jh7110_stgclk_data[idx].parents[i];

			if (pidx < JH7110_STGCLK_END)
				parents[i].hw = &priv->reg[pidx].hw;
			else if (pidx < JH7110_STGCLK_EXT_END)
				parents[i].fw_name = fw_name[pidx - JH7110_STGCLK_END];
		}

		clk->hw.init = &init;
		clk->idx = idx;
		clk->max_div = max & JH71X0_CLK_DIV_MASK;

		ret = devm_clk_hw_register(&pdev->dev, &clk->hw);
		if (ret)
			return ret;
	}

	ret = devm_of_clk_add_hw_provider(&pdev->dev, jh71x0_clk_get, priv);
	if (ret)
		return ret;

	return jh7110_reset_controller_register(priv, "rst-stg", 2);
}

static const struct of_device_id jh7110_stgcrg_match[] = {
	{ .compatible = "starfive,jh7110-stgcrg" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jh7110_stgcrg_match);

static struct platform_driver jh7110_stgcrg_driver = {
	.probe = jh7110_stgcrg_probe,
	.driver = {
		.name = "clk-starfive-jh7110-stg",
		.of_match_table = jh7110_stgcrg_match,
	},
};
module_platform_driver(jh7110_stgcrg_driver);

MODULE_AUTHOR("Xingyu Wu <xingyu.wu@starfivetech.com>");
MODULE_AUTHOR("Emil Renner Berthing <kernel@esmil.dk>");
MODULE_DESCRIPTION("StarFive JH7110 System-Top-Group clock driver");
MODULE_LICENSE("GPL");
