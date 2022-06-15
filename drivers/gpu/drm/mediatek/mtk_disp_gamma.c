// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/soc/mediatek/mtk-cmdq.h>

#include "mtk_disp_drv.h"
#include "mtk_drm_crtc.h"
#include "mtk_drm_ddp_comp.h"

#define DISP_GAMMA_EN				0x0000
#define GAMMA_EN					BIT(0)
#define DISP_GAMMA_CFG				0x0020
#define GAMMA_LUT_EN					BIT(1)
#define GAMMA_DITHERING					BIT(2)
#define DISP_GAMMA_SIZE				0x0030
#define DISP_GAMMA_LUT				0x0700

#define LUT_10BIT_MASK				0x03ff

struct mtk_disp_gamma_data {
	bool has_dither;
	bool lut_diff;
};

/*
 * struct mtk_disp_gamma - DISP_GAMMA driver structure
 */
struct mtk_disp_gamma {
	struct clk *clk;
	void __iomem *regs;
	struct cmdq_client_reg cmdq_reg;
	const struct mtk_disp_gamma_data *data;
};

int mtk_gamma_clk_enable(struct device *dev)
{
	struct mtk_disp_gamma *gamma = dev_get_drvdata(dev);

	return clk_prepare_enable(gamma->clk);
}

void mtk_gamma_clk_disable(struct device *dev)
{
	struct mtk_disp_gamma *gamma = dev_get_drvdata(dev);

	clk_disable_unprepare(gamma->clk);
}

void mtk_gamma_set_common(void __iomem *regs, struct drm_crtc_state *state, bool lut_diff)
{
	unsigned int i, reg;
	struct drm_color_lut *lut;
	void __iomem *lut_base;
	u32 word;
	u32 diff[3] = {0};

	if (state->gamma_lut) {
		reg = readl(regs + DISP_GAMMA_CFG);
		reg = reg | GAMMA_LUT_EN;
		writel(reg, regs + DISP_GAMMA_CFG);
		lut_base = regs + DISP_GAMMA_LUT;
		lut = (struct drm_color_lut *)state->gamma_lut->data;
		for (i = 0; i < MTK_LUT_SIZE; i++) {

			if (!lut_diff || (i % 2 == 0)) {
				word = (((lut[i].red >> 6) & LUT_10BIT_MASK) << 20) +
					(((lut[i].green >> 6) & LUT_10BIT_MASK) << 10) +
					((lut[i].blue >> 6) & LUT_10BIT_MASK);
			} else {
				diff[0] = (lut[i].red >> 6) - (lut[i - 1].red >> 6);
				diff[1] = (lut[i].green >> 6) - (lut[i - 1].green >> 6);
				diff[2] = (lut[i].blue >> 6) - (lut[i - 1].blue >> 6);

				word = ((diff[0] & LUT_10BIT_MASK) << 20) +
					((diff[1] & LUT_10BIT_MASK) << 10) +
					(diff[2] & LUT_10BIT_MASK);
			}
			writel(word, (lut_base + i * 4));
		}
	}
}

void mtk_gamma_set(struct device *dev, struct drm_crtc_state *state)
{
	struct mtk_disp_gamma *gamma = dev_get_drvdata(dev);
	bool lut_diff = false;

	if (gamma->data)
		lut_diff = gamma->data->lut_diff;

	mtk_gamma_set_common(gamma->regs, state, lut_diff);
}

void mtk_gamma_config(struct device *dev, unsigned int w,
		      unsigned int h, unsigned int vrefresh,
		      unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_gamma *gamma = dev_get_drvdata(dev);

	mtk_ddp_write(cmdq_pkt, h << 16 | w, &gamma->cmdq_reg, gamma->regs,
		      DISP_GAMMA_SIZE);
	if (gamma->data && gamma->data->has_dither)
		mtk_dither_set_common(gamma->regs, &gamma->cmdq_reg, bpc,
				      DISP_GAMMA_CFG, GAMMA_DITHERING, cmdq_pkt);
}

void mtk_gamma_start(struct device *dev)
{
	struct mtk_disp_gamma *gamma = dev_get_drvdata(dev);

	writel(GAMMA_EN, gamma->regs + DISP_GAMMA_EN);
}

void mtk_gamma_stop(struct device *dev)
{
	struct mtk_disp_gamma *gamma = dev_get_drvdata(dev);

	writel_relaxed(0x0, gamma->regs + DISP_GAMMA_EN);
}

static int mtk_disp_gamma_bind(struct device *dev, struct device *master,
			       void *data)
{
	return 0;
}

static void mtk_disp_gamma_unbind(struct device *dev, struct device *master,
				  void *data)
{
}

static const struct component_ops mtk_disp_gamma_component_ops = {
	.bind	= mtk_disp_gamma_bind,
	.unbind = mtk_disp_gamma_unbind,
};

static int mtk_disp_gamma_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_disp_gamma *priv;
	struct resource *res;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "failed to get gamma clk\n");
		return PTR_ERR(priv->clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->regs)) {
		dev_err(dev, "failed to ioremap gamma\n");
		return PTR_ERR(priv->regs);
	}

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	ret = cmdq_dev_get_client_reg(dev, &priv->cmdq_reg, 0);
	if (ret)
		dev_dbg(dev, "get mediatek,gce-client-reg fail!\n");
#endif

	priv->data = of_device_get_match_data(dev);
	platform_set_drvdata(pdev, priv);

	ret = component_add(dev, &mtk_disp_gamma_component_ops);
	if (ret)
		dev_err(dev, "Failed to add component: %d\n", ret);

	return ret;
}

static int mtk_disp_gamma_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_disp_gamma_component_ops);

	return 0;
}

static const struct mtk_disp_gamma_data mt8173_gamma_driver_data = {
	.has_dither = true,
};

static const struct mtk_disp_gamma_data mt8183_gamma_driver_data = {
	.lut_diff = true,
};

static const struct of_device_id mtk_disp_gamma_driver_dt_match[] = {
	{ .compatible = "mediatek,mt8173-disp-gamma",
	  .data = &mt8173_gamma_driver_data},
	{ .compatible = "mediatek,mt8183-disp-gamma",
	  .data = &mt8183_gamma_driver_data},
	{},
};
MODULE_DEVICE_TABLE(of, mtk_disp_gamma_driver_dt_match);

struct platform_driver mtk_disp_gamma_driver = {
	.probe		= mtk_disp_gamma_probe,
	.remove		= mtk_disp_gamma_remove,
	.driver		= {
		.name	= "mediatek-disp-gamma",
		.owner	= THIS_MODULE,
		.of_match_table = mtk_disp_gamma_driver_dt_match,
	},
};
