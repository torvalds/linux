/*
 * Copyright (c) 2017 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

#include "mtk_drm_crtc.h"
#include "mtk_drm_ddp_comp.h"

#define DISP_COLOR_CFG_MAIN			0x0400
#define DISP_COLOR_START_MT2701			0x0f00
#define DISP_COLOR_START_MT8173			0x0c00
#define DISP_COLOR_START(comp)			((comp)->data->color_offset)
#define DISP_COLOR_WIDTH(comp)			(DISP_COLOR_START(comp) + 0x50)
#define DISP_COLOR_HEIGHT(comp)			(DISP_COLOR_START(comp) + 0x54)

#define COLOR_BYPASS_ALL			BIT(7)
#define COLOR_SEQ_SEL				BIT(13)

struct mtk_disp_color_data {
	unsigned int color_offset;
};

/**
 * struct mtk_disp_color - DISP_COLOR driver structure
 * @ddp_comp - structure containing type enum and hardware resources
 * @crtc - associated crtc to report irq events to
 */
struct mtk_disp_color {
	struct mtk_ddp_comp			ddp_comp;
	struct drm_crtc				*crtc;
	const struct mtk_disp_color_data	*data;
};

static inline struct mtk_disp_color *comp_to_color(struct mtk_ddp_comp *comp)
{
	return container_of(comp, struct mtk_disp_color, ddp_comp);
}

static void mtk_color_config(struct mtk_ddp_comp *comp, unsigned int w,
			     unsigned int h, unsigned int vrefresh,
			     unsigned int bpc)
{
	struct mtk_disp_color *color = comp_to_color(comp);

	writel(w, comp->regs + DISP_COLOR_WIDTH(color));
	writel(h, comp->regs + DISP_COLOR_HEIGHT(color));
}

static void mtk_color_start(struct mtk_ddp_comp *comp)
{
	struct mtk_disp_color *color = comp_to_color(comp);

	writel(COLOR_BYPASS_ALL | COLOR_SEQ_SEL,
	       comp->regs + DISP_COLOR_CFG_MAIN);
	writel(0x1, comp->regs + DISP_COLOR_START(color));
}

static const struct mtk_ddp_comp_funcs mtk_disp_color_funcs = {
	.config = mtk_color_config,
	.start = mtk_color_start,
};

static int mtk_disp_color_bind(struct device *dev, struct device *master,
			       void *data)
{
	struct mtk_disp_color *priv = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	int ret;

	ret = mtk_ddp_comp_register(drm_dev, &priv->ddp_comp);
	if (ret < 0) {
		dev_err(dev, "Failed to register component %pOF: %d\n",
			dev->of_node, ret);
		return ret;
	}

	return 0;
}

static void mtk_disp_color_unbind(struct device *dev, struct device *master,
				  void *data)
{
	struct mtk_disp_color *priv = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;

	mtk_ddp_comp_unregister(drm_dev, &priv->ddp_comp);
}

static const struct component_ops mtk_disp_color_component_ops = {
	.bind	= mtk_disp_color_bind,
	.unbind = mtk_disp_color_unbind,
};

static int mtk_disp_color_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_disp_color *priv;
	int comp_id;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	comp_id = mtk_ddp_comp_get_id(dev->of_node, MTK_DISP_COLOR);
	if (comp_id < 0) {
		dev_err(dev, "Failed to identify by alias: %d\n", comp_id);
		return comp_id;
	}

	ret = mtk_ddp_comp_init(dev, dev->of_node, &priv->ddp_comp, comp_id,
				&mtk_disp_color_funcs);
	if (ret) {
		dev_err(dev, "Failed to initialize component: %d\n", ret);
		return ret;
	}

	priv->data = of_device_get_match_data(dev);

	platform_set_drvdata(pdev, priv);

	ret = component_add(dev, &mtk_disp_color_component_ops);
	if (ret)
		dev_err(dev, "Failed to add component: %d\n", ret);

	return ret;
}

static int mtk_disp_color_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_disp_color_component_ops);

	return 0;
}

static const struct mtk_disp_color_data mt2701_color_driver_data = {
	.color_offset = DISP_COLOR_START_MT2701,
};

static const struct mtk_disp_color_data mt8173_color_driver_data = {
	.color_offset = DISP_COLOR_START_MT8173,
};

static const struct of_device_id mtk_disp_color_driver_dt_match[] = {
	{ .compatible = "mediatek,mt2701-disp-color",
	  .data = &mt2701_color_driver_data},
	{ .compatible = "mediatek,mt8173-disp-color",
	  .data = &mt8173_color_driver_data},
	{},
};
MODULE_DEVICE_TABLE(of, mtk_disp_color_driver_dt_match);

struct platform_driver mtk_disp_color_driver = {
	.probe		= mtk_disp_color_probe,
	.remove		= mtk_disp_color_remove,
	.driver		= {
		.name	= "mediatek-disp-color",
		.owner	= THIS_MODULE,
		.of_match_table = mtk_disp_color_driver_dt_match,
	},
};
