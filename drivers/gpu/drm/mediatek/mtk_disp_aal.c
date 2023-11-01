// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/soc/mediatek/mtk-cmdq.h>

#include "mtk_disp_drv.h"
#include "mtk_drm_crtc.h"
#include "mtk_drm_ddp_comp.h"
#include "mtk_drm_drv.h"

#define DISP_AAL_EN				0x0000
#define AAL_EN						BIT(0)
#define DISP_AAL_CFG				0x0020
#define AAL_RELAY_MODE					BIT(0)
#define AAL_GAMMA_LUT_EN				BIT(1)
#define DISP_AAL_SIZE				0x0030
#define DISP_AAL_SIZE_HSIZE				GENMASK(28, 16)
#define DISP_AAL_SIZE_VSIZE				GENMASK(12, 0)
#define DISP_AAL_OUTPUT_SIZE			0x04d8
#define DISP_AAL_GAMMA_LUT			0x0700
#define DISP_AAL_GAMMA_LUT_R				GENMASK(29, 20)
#define DISP_AAL_GAMMA_LUT_G				GENMASK(19, 10)
#define DISP_AAL_GAMMA_LUT_B				GENMASK(9, 0)
#define DISP_AAL_LUT_BITS			10
#define DISP_AAL_LUT_SIZE			512

struct mtk_disp_aal_data {
	bool has_gamma;
};

 /**
  * struct mtk_disp_aal - Display Adaptive Ambient Light driver structure
  * @clk:      clock for DISP_AAL controller
  * @regs:     MMIO registers base
  * @cmdq_reg: CMDQ Client register
  * @data:     platform specific data for DISP_AAL
  */
struct mtk_disp_aal {
	struct clk *clk;
	void __iomem *regs;
	struct cmdq_client_reg cmdq_reg;
	const struct mtk_disp_aal_data *data;
};

int mtk_aal_clk_enable(struct device *dev)
{
	struct mtk_disp_aal *aal = dev_get_drvdata(dev);

	return clk_prepare_enable(aal->clk);
}

void mtk_aal_clk_disable(struct device *dev)
{
	struct mtk_disp_aal *aal = dev_get_drvdata(dev);

	clk_disable_unprepare(aal->clk);
}

void mtk_aal_config(struct device *dev, unsigned int w,
			   unsigned int h, unsigned int vrefresh,
			   unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_aal *aal = dev_get_drvdata(dev);
	u32 sz;

	sz = FIELD_PREP(DISP_AAL_SIZE_HSIZE, w);
	sz |= FIELD_PREP(DISP_AAL_SIZE_VSIZE, h);

	mtk_ddp_write(cmdq_pkt, sz, &aal->cmdq_reg, aal->regs, DISP_AAL_SIZE);
	mtk_ddp_write(cmdq_pkt, sz, &aal->cmdq_reg, aal->regs, DISP_AAL_OUTPUT_SIZE);
}

/**
 * mtk_aal_gamma_get_lut_size() - Get gamma LUT size for AAL
 * @dev: Pointer to struct device
 *
 * Return: 0 if gamma control not supported in AAL or gamma LUT size
 */
unsigned int mtk_aal_gamma_get_lut_size(struct device *dev)
{
	struct mtk_disp_aal *aal = dev_get_drvdata(dev);

	if (aal->data && aal->data->has_gamma)
		return DISP_AAL_LUT_SIZE;
	return 0;
}

void mtk_aal_gamma_set(struct device *dev, struct drm_crtc_state *state)
{
	struct mtk_disp_aal *aal = dev_get_drvdata(dev);
	struct drm_color_lut *lut;
	unsigned int i;
	u32 cfg_val;

	/* If gamma is not supported in AAL, go out immediately */
	if (!(aal->data && aal->data->has_gamma))
		return;

	/* Also, if there's no gamma lut there's nothing to do here. */
	if (!state->gamma_lut)
		return;

	lut = (struct drm_color_lut *)state->gamma_lut->data;
	for (i = 0; i < DISP_AAL_LUT_SIZE; i++) {
		struct drm_color_lut hwlut = {
			.red = drm_color_lut_extract(lut[i].red, DISP_AAL_LUT_BITS),
			.green = drm_color_lut_extract(lut[i].green, DISP_AAL_LUT_BITS),
			.blue = drm_color_lut_extract(lut[i].blue, DISP_AAL_LUT_BITS)
		};
		u32 word;

		word = FIELD_PREP(DISP_AAL_GAMMA_LUT_R, hwlut.red);
		word |= FIELD_PREP(DISP_AAL_GAMMA_LUT_G, hwlut.green);
		word |= FIELD_PREP(DISP_AAL_GAMMA_LUT_B, hwlut.blue);
		writel(word, aal->regs + DISP_AAL_GAMMA_LUT + i * 4);
	}

	cfg_val = readl(aal->regs + DISP_AAL_CFG);

	/* Enable the gamma table */
	cfg_val |= FIELD_PREP(AAL_GAMMA_LUT_EN, 1);

	/* Disable RELAY mode to pass the processed image */
	cfg_val &= ~AAL_RELAY_MODE;

	writel(cfg_val, aal->regs + DISP_AAL_CFG);
}

void mtk_aal_start(struct device *dev)
{
	struct mtk_disp_aal *aal = dev_get_drvdata(dev);

	writel(AAL_EN, aal->regs + DISP_AAL_EN);
}

void mtk_aal_stop(struct device *dev)
{
	struct mtk_disp_aal *aal = dev_get_drvdata(dev);

	writel_relaxed(0x0, aal->regs + DISP_AAL_EN);
}

static int mtk_disp_aal_bind(struct device *dev, struct device *master,
			       void *data)
{
	return 0;
}

static void mtk_disp_aal_unbind(struct device *dev, struct device *master,
				  void *data)
{
}

static const struct component_ops mtk_disp_aal_component_ops = {
	.bind	= mtk_disp_aal_bind,
	.unbind = mtk_disp_aal_unbind,
};

static int mtk_disp_aal_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_disp_aal *priv;
	struct resource *res;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "failed to get aal clk\n");
		return PTR_ERR(priv->clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->regs)) {
		dev_err(dev, "failed to ioremap aal\n");
		return PTR_ERR(priv->regs);
	}

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	ret = cmdq_dev_get_client_reg(dev, &priv->cmdq_reg, 0);
	if (ret)
		dev_dbg(dev, "get mediatek,gce-client-reg fail!\n");
#endif

	priv->data = of_device_get_match_data(dev);
	platform_set_drvdata(pdev, priv);

	ret = component_add(dev, &mtk_disp_aal_component_ops);
	if (ret)
		dev_err(dev, "Failed to add component: %d\n", ret);

	return ret;
}

static void mtk_disp_aal_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_disp_aal_component_ops);
}

static const struct mtk_disp_aal_data mt8173_aal_driver_data = {
	.has_gamma = true,
};

static const struct of_device_id mtk_disp_aal_driver_dt_match[] = {
	{ .compatible = "mediatek,mt8173-disp-aal", .data = &mt8173_aal_driver_data },
	{ .compatible = "mediatek,mt8183-disp-aal" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_disp_aal_driver_dt_match);

struct platform_driver mtk_disp_aal_driver = {
	.probe		= mtk_disp_aal_probe,
	.remove_new	= mtk_disp_aal_remove,
	.driver		= {
		.name	= "mediatek-disp-aal",
		.owner	= THIS_MODULE,
		.of_match_table = mtk_disp_aal_driver_dt_match,
	},
};
