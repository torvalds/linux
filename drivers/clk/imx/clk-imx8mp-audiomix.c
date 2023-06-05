// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for i.MX8M Plus Audio BLK_CTRL
 *
 * Copyright (C) 2022 Marek Vasut <marex@denx.de>
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/imx8mp-clock.h>

#include "clk.h"

#define CLKEN0			0x000
#define CLKEN1			0x004
#define SAI_MCLK_SEL(n)		(0x300 + 4 * (n))	/* n in 0..5 */
#define PDM_SEL			0x318
#define SAI_PLL_GNRL_CTL	0x400

#define SAIn_MCLK1_PARENT(n)						\
static const struct clk_parent_data					\
clk_imx8mp_audiomix_sai##n##_mclk1_parents[] = {			\
	{								\
		.fw_name = "sai"__stringify(n),				\
		.name = "sai"__stringify(n)				\
	}, {								\
		.fw_name = "sai"__stringify(n)"_mclk",			\
		.name = "sai"__stringify(n)"_mclk"			\
	},								\
}

SAIn_MCLK1_PARENT(1);
SAIn_MCLK1_PARENT(2);
SAIn_MCLK1_PARENT(3);
SAIn_MCLK1_PARENT(5);
SAIn_MCLK1_PARENT(6);
SAIn_MCLK1_PARENT(7);

static const struct clk_parent_data clk_imx8mp_audiomix_sai_mclk2_parents[] = {
	{ .fw_name = "sai1", .name = "sai1" },
	{ .fw_name = "sai2", .name = "sai2" },
	{ .fw_name = "sai3", .name = "sai3" },
	{ .name = "dummy" },
	{ .fw_name = "sai5", .name = "sai5" },
	{ .fw_name = "sai6", .name = "sai6" },
	{ .fw_name = "sai7", .name = "sai7" },
	{ .fw_name = "sai1_mclk", .name = "sai1_mclk" },
	{ .fw_name = "sai2_mclk", .name = "sai2_mclk" },
	{ .fw_name = "sai3_mclk", .name = "sai3_mclk" },
	{ .name = "dummy" },
	{ .fw_name = "sai5_mclk", .name = "sai5_mclk" },
	{ .fw_name = "sai6_mclk", .name = "sai6_mclk" },
	{ .fw_name = "sai7_mclk", .name = "sai7_mclk" },
	{ .fw_name = "spdif_extclk", .name = "spdif_extclk" },
	{ .name = "dummy" },
};

static const struct clk_parent_data clk_imx8mp_audiomix_pdm_parents[] = {
	{ .fw_name = "pdm", .name = "pdm" },
	{ .name = "sai_pll_out_div2" },
	{ .fw_name = "sai1_mclk", .name = "sai1_mclk" },
	{ .name = "dummy" },
};


static const struct clk_parent_data clk_imx8mp_audiomix_pll_parents[] = {
	{ .fw_name = "osc_24m", .name = "osc_24m" },
	{ .name = "dummy" },
	{ .name = "dummy" },
	{ .name = "dummy" },
};

static const struct clk_parent_data clk_imx8mp_audiomix_pll_bypass_sels[] = {
	{ .fw_name = "sai_pll", .name = "sai_pll" },
	{ .fw_name = "sai_pll_ref_sel", .name = "sai_pll_ref_sel" },
};

#define CLK_GATE(gname, cname)						\
	{								\
		gname"_cg",						\
		IMX8MP_CLK_AUDIOMIX_##cname,				\
		{ .fw_name = "ahb", .name = "ahb" }, NULL, 1,		\
		CLKEN0 + 4 * !!(IMX8MP_CLK_AUDIOMIX_##cname / 32),	\
		1, IMX8MP_CLK_AUDIOMIX_##cname % 32			\
	}

#define CLK_SAIn(n)							\
	{								\
		"sai"__stringify(n)"_mclk1_sel",			\
		IMX8MP_CLK_AUDIOMIX_SAI##n##_MCLK1_SEL, {},		\
		clk_imx8mp_audiomix_sai##n##_mclk1_parents,		\
		ARRAY_SIZE(clk_imx8mp_audiomix_sai##n##_mclk1_parents), \
		SAI_MCLK_SEL(n), 1, 0					\
	}, {								\
		"sai"__stringify(n)"_mclk2_sel",			\
		IMX8MP_CLK_AUDIOMIX_SAI##n##_MCLK2_SEL, {},		\
		clk_imx8mp_audiomix_sai_mclk2_parents,			\
		ARRAY_SIZE(clk_imx8mp_audiomix_sai_mclk2_parents),	\
		SAI_MCLK_SEL(n), 4, 1					\
	}, {								\
		"sai"__stringify(n)"_ipg_cg",				\
		IMX8MP_CLK_AUDIOMIX_SAI##n##_IPG,			\
		{ .fw_name = "ahb", .name = "ahb" }, NULL, 1,		\
		CLKEN0, 1, IMX8MP_CLK_AUDIOMIX_SAI##n##_IPG		\
	}, {								\
		"sai"__stringify(n)"_mclk1_cg",				\
		IMX8MP_CLK_AUDIOMIX_SAI##n##_MCLK1,			\
		{							\
			.fw_name = "sai"__stringify(n)"_mclk1_sel",	\
			.name = "sai"__stringify(n)"_mclk1_sel"		\
		}, NULL, 1,						\
		CLKEN0, 1, IMX8MP_CLK_AUDIOMIX_SAI##n##_MCLK1		\
	}, {								\
		"sai"__stringify(n)"_mclk2_cg",				\
		IMX8MP_CLK_AUDIOMIX_SAI##n##_MCLK2,			\
		{							\
			.fw_name = "sai"__stringify(n)"_mclk2_sel",	\
			.name = "sai"__stringify(n)"_mclk2_sel"		\
		}, NULL, 1,						\
		CLKEN0, 1, IMX8MP_CLK_AUDIOMIX_SAI##n##_MCLK2		\
	}, {								\
		"sai"__stringify(n)"_mclk3_cg",				\
		IMX8MP_CLK_AUDIOMIX_SAI##n##_MCLK3,			\
		{							\
			.fw_name = "sai_pll_out_div2",			\
			.name = "sai_pll_out_div2"			\
		}, NULL, 1,						\
		CLKEN0, 1, IMX8MP_CLK_AUDIOMIX_SAI##n##_MCLK3		\
	}

#define CLK_PDM								\
	{								\
		"pdm_sel", IMX8MP_CLK_AUDIOMIX_PDM_SEL, {},		\
		clk_imx8mp_audiomix_pdm_parents,			\
		ARRAY_SIZE(clk_imx8mp_audiomix_pdm_parents),		\
		PDM_SEL, 2, 0						\
	}

struct clk_imx8mp_audiomix_sel {
	const char			*name;
	int				clkid;
	const struct clk_parent_data	parent;		/* For gate */
	const struct clk_parent_data	*parents;	/* For mux */
	int				num_parents;
	u16				reg;
	u8				width;
	u8				shift;
};

static struct clk_imx8mp_audiomix_sel sels[] = {
	CLK_GATE("asrc", ASRC_IPG),
	CLK_GATE("pdm", PDM_IPG),
	CLK_GATE("earc", EARC_IPG),
	CLK_GATE("ocrama", OCRAMA_IPG),
	CLK_GATE("aud2htx", AUD2HTX_IPG),
	CLK_GATE("earc_phy", EARC_PHY),
	CLK_GATE("sdma2", SDMA2_ROOT),
	CLK_GATE("sdma3", SDMA3_ROOT),
	CLK_GATE("spba2", SPBA2_ROOT),
	CLK_GATE("dsp", DSP_ROOT),
	CLK_GATE("dspdbg", DSPDBG_ROOT),
	CLK_GATE("edma", EDMA_ROOT),
	CLK_GATE("audpll", AUDPLL_ROOT),
	CLK_GATE("mu2", MU2_ROOT),
	CLK_GATE("mu3", MU3_ROOT),
	CLK_PDM,
	CLK_SAIn(1),
	CLK_SAIn(2),
	CLK_SAIn(3),
	CLK_SAIn(5),
	CLK_SAIn(6),
	CLK_SAIn(7)
};

static int clk_imx8mp_audiomix_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *priv;
	struct device *dev = &pdev->dev;
	void __iomem *base;
	struct clk_hw *hw;
	int i;

	priv = devm_kzalloc(dev,
			    struct_size(priv, hws, IMX8MP_CLK_AUDIOMIX_END),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->num = IMX8MP_CLK_AUDIOMIX_END;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	for (i = 0; i < ARRAY_SIZE(sels); i++) {
		if (sels[i].num_parents == 1) {
			hw = devm_clk_hw_register_gate_parent_data(dev,
				sels[i].name, &sels[i].parent, 0,
				base + sels[i].reg, sels[i].shift, 0, NULL);
		} else {
			hw = devm_clk_hw_register_mux_parent_data_table(dev,
				sels[i].name, sels[i].parents,
				sels[i].num_parents, 0,
				base + sels[i].reg,
				sels[i].shift, sels[i].width,
				0, NULL, NULL);
		}

		if (IS_ERR(hw))
			return PTR_ERR(hw);

		priv->hws[sels[i].clkid] = hw;
	}

	/* SAI PLL */
	hw = devm_clk_hw_register_mux_parent_data_table(dev,
		"sai_pll_ref_sel", clk_imx8mp_audiomix_pll_parents,
		ARRAY_SIZE(clk_imx8mp_audiomix_pll_parents),
		CLK_SET_RATE_NO_REPARENT, base + SAI_PLL_GNRL_CTL,
		0, 2, 0, NULL, NULL);
	priv->hws[IMX8MP_CLK_AUDIOMIX_SAI_PLL_REF_SEL] = hw;

	hw = imx_dev_clk_hw_pll14xx(dev, "sai_pll", "sai_pll_ref_sel",
				    base + 0x400, &imx_1443x_pll);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	priv->hws[IMX8MP_CLK_AUDIOMIX_SAI_PLL] = hw;

	hw = devm_clk_hw_register_mux_parent_data_table(dev,
		"sai_pll_bypass", clk_imx8mp_audiomix_pll_bypass_sels,
		ARRAY_SIZE(clk_imx8mp_audiomix_pll_bypass_sels),
		CLK_SET_RATE_NO_REPARENT | CLK_SET_RATE_PARENT,
		base + SAI_PLL_GNRL_CTL, 16, 1, 0, NULL, NULL);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	priv->hws[IMX8MP_CLK_AUDIOMIX_SAI_PLL_BYPASS] = hw;

	hw = devm_clk_hw_register_gate(dev, "sai_pll_out", "sai_pll_bypass",
				       0, base + SAI_PLL_GNRL_CTL, 13,
				       0, NULL);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	priv->hws[IMX8MP_CLK_AUDIOMIX_SAI_PLL_OUT] = hw;

	hw = devm_clk_hw_register_fixed_factor(dev, "sai_pll_out_div2",
					       "sai_pll_out", 0, 1, 2);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	return devm_of_clk_add_hw_provider(&pdev->dev, of_clk_hw_onecell_get,
					   priv);
}

static const struct of_device_id clk_imx8mp_audiomix_of_match[] = {
	{ .compatible = "fsl,imx8mp-audio-blk-ctrl" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, clk_imx8mp_audiomix_of_match);

static struct platform_driver clk_imx8mp_audiomix_driver = {
	.probe	= clk_imx8mp_audiomix_probe,
	.driver = {
		.name = "imx8mp-audio-blk-ctrl",
		.of_match_table = clk_imx8mp_audiomix_of_match,
	},
};

module_platform_driver(clk_imx8mp_audiomix_driver);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_DESCRIPTION("Freescale i.MX8MP Audio Block Controller driver");
MODULE_LICENSE("GPL");
