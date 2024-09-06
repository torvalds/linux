// SPDX-License-Identifier: GPL-2.0
/*
 * LPASS Audio CC and Always ON CC Glitch Free Mux clock driver
 *
 * Copyright (c) 2020 Linaro Ltd.
 * Author: Srinivas Kandagatla <srinivas.kandagatla@linaro.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/qcom,sm8250-lpass-audiocc.h>
#include <dt-bindings/clock/qcom,sm8250-lpass-aoncc.h>

struct lpass_gfm {
	struct device *dev;
	void __iomem *base;
};

struct clk_gfm {
	unsigned int mux_reg;
	unsigned int mux_mask;
	struct clk_hw	hw;
	struct lpass_gfm *priv;
	void __iomem *gfm_mux;
};

#define to_clk_gfm(_hw) container_of(_hw, struct clk_gfm, hw)

static u8 clk_gfm_get_parent(struct clk_hw *hw)
{
	struct clk_gfm *clk = to_clk_gfm(hw);

	return readl(clk->gfm_mux) & clk->mux_mask;
}

static int clk_gfm_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_gfm *clk = to_clk_gfm(hw);
	unsigned int val;

	val = readl(clk->gfm_mux);

	if (index)
		val |= clk->mux_mask;
	else
		val &= ~clk->mux_mask;


	writel(val, clk->gfm_mux);

	return 0;
}

static const struct clk_ops clk_gfm_ops = {
	.get_parent = clk_gfm_get_parent,
	.set_parent = clk_gfm_set_parent,
	.determine_rate = __clk_mux_determine_rate,
};

static struct clk_gfm lpass_gfm_va_mclk = {
	.mux_reg = 0x20000,
	.mux_mask = BIT(0),
	.hw.init = &(struct clk_init_data) {
		.name = "VA_MCLK",
		.ops = &clk_gfm_ops,
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.num_parents = 2,
		.parent_data = (const struct clk_parent_data[]){
			{
				.index = 0,
				.fw_name = "LPASS_CLK_ID_TX_CORE_MCLK",
			}, {
				.index = 1,
				.fw_name = "LPASS_CLK_ID_VA_CORE_MCLK",
			},
		},
	},
};

static struct clk_gfm lpass_gfm_tx_npl = {
	.mux_reg = 0x20000,
	.mux_mask = BIT(0),
	.hw.init = &(struct clk_init_data) {
		.name = "TX_NPL",
		.ops = &clk_gfm_ops,
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.parent_data = (const struct clk_parent_data[]){
			{
				.index = 0,
				.fw_name = "LPASS_CLK_ID_TX_CORE_NPL_MCLK",
			}, {
				.index = 1,
				.fw_name = "LPASS_CLK_ID_VA_CORE_2X_MCLK",
			},
		},
		.num_parents = 2,
	},
};

static struct clk_gfm lpass_gfm_wsa_mclk = {
	.mux_reg = 0x220d8,
	.mux_mask = BIT(0),
	.hw.init = &(struct clk_init_data) {
		.name = "WSA_MCLK",
		.ops = &clk_gfm_ops,
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.parent_data = (const struct clk_parent_data[]){
			{
				.index = 0,
				.fw_name = "LPASS_CLK_ID_TX_CORE_MCLK",
			}, {
				.index = 1,
				.fw_name = "LPASS_CLK_ID_WSA_CORE_MCLK",
			},
		},
		.num_parents = 2,
	},
};

static struct clk_gfm lpass_gfm_wsa_npl = {
	.mux_reg = 0x220d8,
	.mux_mask = BIT(0),
	.hw.init = &(struct clk_init_data) {
		.name = "WSA_NPL",
		.ops = &clk_gfm_ops,
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.parent_data = (const struct clk_parent_data[]){
			{
				.index = 0,
				.fw_name = "LPASS_CLK_ID_TX_CORE_NPL_MCLK",
			}, {
				.index = 1,
				.fw_name = "LPASS_CLK_ID_WSA_CORE_NPL_MCLK",
			},
		},
		.num_parents = 2,
	},
};

static struct clk_gfm lpass_gfm_rx_mclk_mclk2 = {
	.mux_reg = 0x240d8,
	.mux_mask = BIT(0),
	.hw.init = &(struct clk_init_data) {
		.name = "RX_MCLK_MCLK2",
		.ops = &clk_gfm_ops,
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.parent_data = (const struct clk_parent_data[]){
			{
				.index = 0,
				.fw_name = "LPASS_CLK_ID_TX_CORE_MCLK",
			}, {
				.index = 1,
				.fw_name = "LPASS_CLK_ID_RX_CORE_MCLK",
			},
		},
		.num_parents = 2,
	},
};

static struct clk_gfm lpass_gfm_rx_npl = {
	.mux_reg = 0x240d8,
	.mux_mask = BIT(0),
	.hw.init = &(struct clk_init_data) {
		.name = "RX_NPL",
		.ops = &clk_gfm_ops,
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.parent_data = (const struct clk_parent_data[]){
			{
				.index = 0,
				.fw_name = "LPASS_CLK_ID_TX_CORE_NPL_MCLK",
			}, {
				.index = 1,
				.fw_name = "LPASS_CLK_ID_RX_CORE_NPL_MCLK",
			},
		},
		.num_parents = 2,
	},
};

static struct clk_gfm *aoncc_gfm_clks[] = {
	[LPASS_CDC_VA_MCLK]		= &lpass_gfm_va_mclk,
	[LPASS_CDC_TX_NPL]		= &lpass_gfm_tx_npl,
};

static struct clk_hw_onecell_data aoncc_hw_onecell_data = {
	.hws = {
		[LPASS_CDC_VA_MCLK]	= &lpass_gfm_va_mclk.hw,
		[LPASS_CDC_TX_NPL]	= &lpass_gfm_tx_npl.hw,
	},
	.num = ARRAY_SIZE(aoncc_gfm_clks),
};

static struct clk_gfm *audiocc_gfm_clks[] = {
	[LPASS_CDC_WSA_NPL]		= &lpass_gfm_wsa_npl,
	[LPASS_CDC_WSA_MCLK]		= &lpass_gfm_wsa_mclk,
	[LPASS_CDC_RX_NPL]		= &lpass_gfm_rx_npl,
	[LPASS_CDC_RX_MCLK_MCLK2]	= &lpass_gfm_rx_mclk_mclk2,
};

static struct clk_hw_onecell_data audiocc_hw_onecell_data = {
	.hws = {
		[LPASS_CDC_WSA_NPL]	= &lpass_gfm_wsa_npl.hw,
		[LPASS_CDC_WSA_MCLK]	= &lpass_gfm_wsa_mclk.hw,
		[LPASS_CDC_RX_NPL]	= &lpass_gfm_rx_npl.hw,
		[LPASS_CDC_RX_MCLK_MCLK2] = &lpass_gfm_rx_mclk_mclk2.hw,
	},
	.num = ARRAY_SIZE(audiocc_gfm_clks),
};

struct lpass_gfm_data {
	struct clk_hw_onecell_data *onecell_data;
	struct clk_gfm **gfm_clks;
};

static struct lpass_gfm_data audiocc_data = {
	.onecell_data = &audiocc_hw_onecell_data,
	.gfm_clks = audiocc_gfm_clks,
};

static struct lpass_gfm_data aoncc_data = {
	.onecell_data = &aoncc_hw_onecell_data,
	.gfm_clks = aoncc_gfm_clks,
};

static int lpass_gfm_clk_driver_probe(struct platform_device *pdev)
{
	const struct lpass_gfm_data *data;
	struct device *dev = &pdev->dev;
	struct clk_gfm *gfm;
	struct lpass_gfm *cc;
	int err, i;

	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	cc = devm_kzalloc(dev, sizeof(*cc), GFP_KERNEL);
	if (!cc)
		return -ENOMEM;

	cc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(cc->base))
		return PTR_ERR(cc->base);

	err = devm_pm_runtime_enable(dev);
	if (err)
		return err;

	err = devm_pm_clk_create(dev);
	if (err)
		return err;

	err = of_pm_clk_add_clks(dev);
	if (err < 0) {
		dev_dbg(dev, "Failed to get lpass core voting clocks\n");
		return err;
	}

	for (i = 0; i < data->onecell_data->num; i++) {
		if (!data->gfm_clks[i])
			continue;

		gfm = data->gfm_clks[i];
		gfm->priv = cc;
		gfm->gfm_mux = cc->base;
		gfm->gfm_mux = gfm->gfm_mux + data->gfm_clks[i]->mux_reg;

		err = devm_clk_hw_register(dev, &data->gfm_clks[i]->hw);
		if (err)
			return err;

	}

	err = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					  data->onecell_data);
	if (err)
		return err;

	return 0;
}

static const struct of_device_id lpass_gfm_clk_match_table[] = {
	{
		.compatible = "qcom,sm8250-lpass-aoncc",
		.data = &aoncc_data,
	},
	{
		.compatible = "qcom,sm8250-lpass-audiocc",
		.data = &audiocc_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, lpass_gfm_clk_match_table);

static const struct dev_pm_ops lpass_gfm_pm_ops = {
	SET_RUNTIME_PM_OPS(pm_clk_suspend, pm_clk_resume, NULL)
};

static struct platform_driver lpass_gfm_clk_driver = {
	.probe		= lpass_gfm_clk_driver_probe,
	.driver		= {
		.name	= "lpass-gfm-clk",
		.of_match_table = lpass_gfm_clk_match_table,
		.pm = &lpass_gfm_pm_ops,
	},
};
module_platform_driver(lpass_gfm_clk_driver);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("QTI SM8250 LPASS Glitch Free Mux clock driver");
