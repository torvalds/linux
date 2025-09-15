// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 Samsung Electronics Co., Ltd.
 * Author: Michal Wilczynski <m.wilczynski@samsung.com>
 */

#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/regmap.h>

#include <dt-bindings/reset/thead,th1520-reset.h>

 /* register offset in VOSYS_REGMAP */
#define TH1520_GPU_RST_CFG		0x0
#define TH1520_GPU_RST_CFG_MASK		GENMASK(1, 0)
#define TH1520_DPU_RST_CFG		0x4
#define TH1520_DSI0_RST_CFG		0x8
#define TH1520_DSI1_RST_CFG		0xc
#define TH1520_HDMI_RST_CFG		0x14

/* register values */
#define TH1520_GPU_SW_GPU_RST		BIT(0)
#define TH1520_GPU_SW_CLKGEN_RST	BIT(1)
#define TH1520_DPU_SW_DPU_HRST		BIT(0)
#define TH1520_DPU_SW_DPU_ARST		BIT(1)
#define TH1520_DPU_SW_DPU_CRST		BIT(2)
#define TH1520_DSI_SW_DSI_PRST		BIT(0)
#define TH1520_HDMI_SW_MAIN_RST		BIT(0)
#define TH1520_HDMI_SW_PRST		BIT(1)

struct th1520_reset_priv {
	struct reset_controller_dev rcdev;
	struct regmap *map;
};

struct th1520_reset_map {
	u32 bit;
	u32 reg;
};

static const struct th1520_reset_map th1520_resets[] = {
	[TH1520_RESET_ID_GPU] = {
		.bit = TH1520_GPU_SW_GPU_RST,
		.reg = TH1520_GPU_RST_CFG,
	},
	[TH1520_RESET_ID_GPU_CLKGEN] = {
		.bit = TH1520_GPU_SW_CLKGEN_RST,
		.reg = TH1520_GPU_RST_CFG,
	},
	[TH1520_RESET_ID_DPU_AHB] = {
		.bit = TH1520_DPU_SW_DPU_HRST,
		.reg = TH1520_DPU_RST_CFG,
	},
	[TH1520_RESET_ID_DPU_AXI] = {
		.bit = TH1520_DPU_SW_DPU_ARST,
		.reg = TH1520_DPU_RST_CFG,
	},
	[TH1520_RESET_ID_DPU_CORE] = {
		.bit = TH1520_DPU_SW_DPU_CRST,
		.reg = TH1520_DPU_RST_CFG,
	},
	[TH1520_RESET_ID_DSI0_APB] = {
		.bit = TH1520_DSI_SW_DSI_PRST,
		.reg = TH1520_DSI0_RST_CFG,
	},
	[TH1520_RESET_ID_DSI1_APB] = {
		.bit = TH1520_DSI_SW_DSI_PRST,
		.reg = TH1520_DSI1_RST_CFG,
	},
	[TH1520_RESET_ID_HDMI] = {
		.bit = TH1520_HDMI_SW_MAIN_RST,
		.reg = TH1520_HDMI_RST_CFG,
	},
	[TH1520_RESET_ID_HDMI_APB] = {
		.bit = TH1520_HDMI_SW_PRST,
		.reg = TH1520_HDMI_RST_CFG,
	},
};

static inline struct th1520_reset_priv *
to_th1520_reset(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct th1520_reset_priv, rcdev);
}

static int th1520_reset_assert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	struct th1520_reset_priv *priv = to_th1520_reset(rcdev);
	const struct th1520_reset_map *reset;

	reset = &th1520_resets[id];

	return regmap_update_bits(priv->map, reset->reg, reset->bit, 0);
}

static int th1520_reset_deassert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	struct th1520_reset_priv *priv = to_th1520_reset(rcdev);
	const struct th1520_reset_map *reset;

	reset = &th1520_resets[id];

	return regmap_update_bits(priv->map, reset->reg, reset->bit,
				  reset->bit);
}

static const struct reset_control_ops th1520_reset_ops = {
	.assert	= th1520_reset_assert,
	.deassert = th1520_reset_deassert,
};

static const struct regmap_config th1520_reset_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static int th1520_reset_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct th1520_reset_priv *priv;
	void __iomem *base;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	priv->map = devm_regmap_init_mmio(dev, base,
					  &th1520_reset_regmap_config);
	if (IS_ERR(priv->map))
		return PTR_ERR(priv->map);

	/* Initialize GPU resets to asserted state */
	ret = regmap_update_bits(priv->map, TH1520_GPU_RST_CFG,
				 TH1520_GPU_RST_CFG_MASK, 0);
	if (ret)
		return ret;

	priv->rcdev.owner = THIS_MODULE;
	priv->rcdev.nr_resets = ARRAY_SIZE(th1520_resets);
	priv->rcdev.ops = &th1520_reset_ops;
	priv->rcdev.of_node = dev->of_node;

	return devm_reset_controller_register(dev, &priv->rcdev);
}

static const struct of_device_id th1520_reset_match[] = {
	{ .compatible = "thead,th1520-reset" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, th1520_reset_match);

static struct platform_driver th1520_reset_driver = {
	.driver = {
		.name = "th1520-reset",
		.of_match_table = th1520_reset_match,
	},
	.probe = th1520_reset_probe,
};
module_platform_driver(th1520_reset_driver);

MODULE_AUTHOR("Michal Wilczynski <m.wilczynski@samsung.com>");
MODULE_DESCRIPTION("T-HEAD TH1520 SoC reset controller");
MODULE_LICENSE("GPL");
