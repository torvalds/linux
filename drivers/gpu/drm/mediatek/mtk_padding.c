// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/soc/mediatek/mtk-cmdq.h>

#include "mtk_crtc.h"
#include "mtk_ddp_comp.h"
#include "mtk_disp_drv.h"

#define PADDING_CONTROL_REG	0x00
#define PADDING_BYPASS			BIT(0)
#define PADDING_ENABLE			BIT(1)
#define PADDING_PIC_SIZE_REG	0x04
#define PADDING_H_REG		0x08 /* horizontal */
#define PADDING_V_REG		0x0c /* vertical */
#define PADDING_COLOR_REG	0x10

/**
 * struct mtk_padding - Basic information of the Padding
 * @clk: Clock of the module
 * @reg: Virtual address of the Padding for CPU to access
 * @cmdq_reg: CMDQ setting of the Padding
 *
 * Every Padding should have different clock source, register base, and
 * CMDQ settings, we stored these differences all together.
 */
struct mtk_padding {
	struct clk		*clk;
	void __iomem		*reg;
	struct cmdq_client_reg	cmdq_reg;
};

int mtk_padding_clk_enable(struct device *dev)
{
	struct mtk_padding *padding = dev_get_drvdata(dev);

	return clk_prepare_enable(padding->clk);
}

void mtk_padding_clk_disable(struct device *dev)
{
	struct mtk_padding *padding = dev_get_drvdata(dev);

	clk_disable_unprepare(padding->clk);
}

void mtk_padding_start(struct device *dev)
{
	struct mtk_padding *padding = dev_get_drvdata(dev);

	writel(PADDING_ENABLE | PADDING_BYPASS,
	       padding->reg + PADDING_CONTROL_REG);

	/*
	 * Notice that even the padding is in bypass mode,
	 * all the settings must be cleared to 0 or
	 * undefined behaviors could happen
	 */
	writel(0, padding->reg + PADDING_PIC_SIZE_REG);
	writel(0, padding->reg + PADDING_H_REG);
	writel(0, padding->reg + PADDING_V_REG);
	writel(0, padding->reg + PADDING_COLOR_REG);
}

void mtk_padding_stop(struct device *dev)
{
	struct mtk_padding *padding = dev_get_drvdata(dev);

	writel(0, padding->reg + PADDING_CONTROL_REG);
}

static int mtk_padding_bind(struct device *dev, struct device *master, void *data)
{
	return 0;
}

static void mtk_padding_unbind(struct device *dev, struct device *master, void *data)
{
}

static const struct component_ops mtk_padding_component_ops = {
	.bind	= mtk_padding_bind,
	.unbind = mtk_padding_unbind,
};

static int mtk_padding_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_padding *priv;
	struct resource *res;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk),
				     "failed to get clk\n");

	priv->reg = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(priv->reg))
		return dev_err_probe(dev, PTR_ERR(priv->reg),
				     "failed to do ioremap\n");

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	ret = cmdq_dev_get_client_reg(dev, &priv->cmdq_reg, 0);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get gce client reg\n");
#endif

	platform_set_drvdata(pdev, priv);

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	ret = component_add(dev, &mtk_padding_component_ops);
	if (ret) {
		pm_runtime_disable(dev);
		return dev_err_probe(dev, ret, "failed to add component\n");
	}

	return 0;
}

static void mtk_padding_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_padding_component_ops);
}

static const struct of_device_id mtk_padding_driver_dt_match[] = {
	{ .compatible = "mediatek,mt8188-disp-padding" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_padding_driver_dt_match);

struct platform_driver mtk_padding_driver = {
	.probe		= mtk_padding_probe,
	.remove_new	= mtk_padding_remove,
	.driver		= {
		.name	= "mediatek-disp-padding",
		.of_match_table = mtk_padding_driver_dt_match,
	},
};
