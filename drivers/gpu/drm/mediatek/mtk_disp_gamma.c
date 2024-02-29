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

#define DISP_GAMMA_EN				0x0000
#define GAMMA_EN					BIT(0)
#define DISP_GAMMA_CFG				0x0020
#define GAMMA_RELAY_MODE				BIT(0)
#define GAMMA_LUT_EN					BIT(1)
#define GAMMA_DITHERING					BIT(2)
#define GAMMA_LUT_TYPE					BIT(2)
#define DISP_GAMMA_SIZE				0x0030
#define DISP_GAMMA_SIZE_HSIZE				GENMASK(28, 16)
#define DISP_GAMMA_SIZE_VSIZE				GENMASK(12, 0)
#define DISP_GAMMA_BANK				0x0100
#define DISP_GAMMA_BANK_BANK				GENMASK(1, 0)
#define DISP_GAMMA_BANK_DATA_MODE			BIT(2)
#define DISP_GAMMA_LUT				0x0700
#define DISP_GAMMA_LUT1				0x0b00

/* For 10 bit LUT layout, R/G/B are in the same register */
#define DISP_GAMMA_LUT_10BIT_R			GENMASK(29, 20)
#define DISP_GAMMA_LUT_10BIT_G			GENMASK(19, 10)
#define DISP_GAMMA_LUT_10BIT_B			GENMASK(9, 0)

/* For 12 bit LUT layout, R/G are in LUT, B is in LUT1 */
#define DISP_GAMMA_LUT_12BIT_R			GENMASK(11, 0)
#define DISP_GAMMA_LUT_12BIT_G			GENMASK(23, 12)
#define DISP_GAMMA_LUT_12BIT_B			GENMASK(11, 0)

struct mtk_disp_gamma_data {
	bool has_dither;
	bool lut_diff;
	u16 lut_bank_size;
	u16 lut_size;
	u8 lut_bits;
};

/**
 * struct mtk_disp_gamma - Display Gamma driver structure
 * @clk:      clock for DISP_GAMMA block
 * @regs:     MMIO registers base
 * @cmdq_reg: CMDQ Client register
 * @data:     platform data for DISP_GAMMA
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

unsigned int mtk_gamma_get_lut_size(struct device *dev)
{
	struct mtk_disp_gamma *gamma = dev_get_drvdata(dev);

	if (gamma && gamma->data)
		return gamma->data->lut_size;
	return 0;
}

static bool mtk_gamma_lut_is_descending(struct drm_color_lut *lut, u32 lut_size)
{
	u64 first, last;
	int last_entry = lut_size - 1;

	first = lut[0].red + lut[0].green + lut[0].blue;
	last = lut[last_entry].red + lut[last_entry].green + lut[last_entry].blue;

	return !!(first > last);
}

/*
 * SoCs supporting 12-bits LUTs are using a new register layout that does
 * always support (by HW) both 12-bits and 10-bits LUT but, on those, we
 * ignore the support for 10-bits in this driver and always use 12-bits.
 *
 * Summarizing:
 * - SoC HW support 9/10-bits LUT only
 *   - Old register layout
 *     - 10-bits LUT supported
 *     - 9-bits LUT not supported
 * - SoC HW support both 10/12bits LUT
 *   - New register layout
 *     - 12-bits LUT supported
 *     - 10-its LUT not supported
 */
void mtk_gamma_set(struct device *dev, struct drm_crtc_state *state)
{
	struct mtk_disp_gamma *gamma = dev_get_drvdata(dev);
	void __iomem *lut0_base = gamma->regs + DISP_GAMMA_LUT;
	void __iomem *lut1_base = gamma->regs + DISP_GAMMA_LUT1;
	u32 cfg_val, data_mode, lbank_val, word[2];
	u8 lut_bits = gamma->data->lut_bits;
	int cur_bank, num_lut_banks;
	struct drm_color_lut *lut;
	unsigned int i;

	/* If there's no gamma lut there's nothing to do here. */
	if (!state->gamma_lut)
		return;

	num_lut_banks = gamma->data->lut_size / gamma->data->lut_bank_size;
	lut = (struct drm_color_lut *)state->gamma_lut->data;

	/* Switch to 12 bits data mode if supported */
	data_mode = FIELD_PREP(DISP_GAMMA_BANK_DATA_MODE, !!(lut_bits == 12));

	for (cur_bank = 0; cur_bank < num_lut_banks; cur_bank++) {

		/* Switch gamma bank and set data mode before writing LUT */
		if (num_lut_banks > 1) {
			lbank_val = FIELD_PREP(DISP_GAMMA_BANK_BANK, cur_bank);
			lbank_val |= data_mode;
			writel(lbank_val, gamma->regs + DISP_GAMMA_BANK);
		}

		for (i = 0; i < gamma->data->lut_bank_size; i++) {
			int n = cur_bank * gamma->data->lut_bank_size + i;
			struct drm_color_lut diff, hwlut;

			hwlut.red = drm_color_lut_extract(lut[n].red, lut_bits);
			hwlut.green = drm_color_lut_extract(lut[n].green, lut_bits);
			hwlut.blue = drm_color_lut_extract(lut[n].blue, lut_bits);

			if (!gamma->data->lut_diff || (i % 2 == 0)) {
				if (lut_bits == 12) {
					word[0] = FIELD_PREP(DISP_GAMMA_LUT_12BIT_R, hwlut.red);
					word[0] |= FIELD_PREP(DISP_GAMMA_LUT_12BIT_G, hwlut.green);
					word[1] = FIELD_PREP(DISP_GAMMA_LUT_12BIT_B, hwlut.blue);
				} else {
					word[0] = FIELD_PREP(DISP_GAMMA_LUT_10BIT_R, hwlut.red);
					word[0] |= FIELD_PREP(DISP_GAMMA_LUT_10BIT_G, hwlut.green);
					word[0] |= FIELD_PREP(DISP_GAMMA_LUT_10BIT_B, hwlut.blue);
				}
			} else {
				diff.red = lut[n].red - lut[n - 1].red;
				diff.red = drm_color_lut_extract(diff.red, lut_bits);

				diff.green = lut[n].green - lut[n - 1].green;
				diff.green = drm_color_lut_extract(diff.green, lut_bits);

				diff.blue = lut[n].blue - lut[n - 1].blue;
				diff.blue = drm_color_lut_extract(diff.blue, lut_bits);

				if (lut_bits == 12) {
					word[0] = FIELD_PREP(DISP_GAMMA_LUT_12BIT_R, diff.red);
					word[0] |= FIELD_PREP(DISP_GAMMA_LUT_12BIT_G, diff.green);
					word[1] = FIELD_PREP(DISP_GAMMA_LUT_12BIT_B, diff.blue);
				} else {
					word[0] = FIELD_PREP(DISP_GAMMA_LUT_10BIT_R, diff.red);
					word[0] |= FIELD_PREP(DISP_GAMMA_LUT_10BIT_G, diff.green);
					word[0] |= FIELD_PREP(DISP_GAMMA_LUT_10BIT_B, diff.blue);
				}
			}
			writel(word[0], lut0_base + i * 4);
			if (lut_bits == 12)
				writel(word[1], lut1_base + i * 4);
		}
	}

	cfg_val = readl(gamma->regs + DISP_GAMMA_CFG);

	if (!gamma->data->has_dither) {
		/* Descending or Rising LUT */
		if (mtk_gamma_lut_is_descending(lut, gamma->data->lut_size - 1))
			cfg_val |= FIELD_PREP(GAMMA_LUT_TYPE, 1);
		else
			cfg_val &= ~GAMMA_LUT_TYPE;
	}

	/* Enable the gamma table */
	cfg_val |= FIELD_PREP(GAMMA_LUT_EN, 1);

	/* Disable RELAY mode to pass the processed image */
	cfg_val &= ~GAMMA_RELAY_MODE;

	writel(cfg_val, gamma->regs + DISP_GAMMA_CFG);
}

void mtk_gamma_config(struct device *dev, unsigned int w,
		      unsigned int h, unsigned int vrefresh,
		      unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_gamma *gamma = dev_get_drvdata(dev);
	u32 sz;

	sz = FIELD_PREP(DISP_GAMMA_SIZE_HSIZE, w);
	sz |= FIELD_PREP(DISP_GAMMA_SIZE_VSIZE, h);

	mtk_ddp_write(cmdq_pkt, sz, &gamma->cmdq_reg, gamma->regs, DISP_GAMMA_SIZE);
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

static void mtk_disp_gamma_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_disp_gamma_component_ops);
}

static const struct mtk_disp_gamma_data mt8173_gamma_driver_data = {
	.has_dither = true,
	.lut_bank_size = 512,
	.lut_bits = 10,
	.lut_size = 512,
};

static const struct mtk_disp_gamma_data mt8183_gamma_driver_data = {
	.lut_bank_size = 512,
	.lut_bits = 10,
	.lut_diff = true,
	.lut_size = 512,
};

static const struct mtk_disp_gamma_data mt8195_gamma_driver_data = {
	.lut_bank_size = 256,
	.lut_bits = 12,
	.lut_diff = true,
	.lut_size = 1024,
};

static const struct of_device_id mtk_disp_gamma_driver_dt_match[] = {
	{ .compatible = "mediatek,mt8173-disp-gamma",
	  .data = &mt8173_gamma_driver_data},
	{ .compatible = "mediatek,mt8183-disp-gamma",
	  .data = &mt8183_gamma_driver_data},
	{ .compatible = "mediatek,mt8195-disp-gamma",
	  .data = &mt8195_gamma_driver_data},
	{},
};
MODULE_DEVICE_TABLE(of, mtk_disp_gamma_driver_dt_match);

struct platform_driver mtk_disp_gamma_driver = {
	.probe		= mtk_disp_gamma_probe,
	.remove_new	= mtk_disp_gamma_remove,
	.driver		= {
		.name	= "mediatek-disp-gamma",
		.owner	= THIS_MODULE,
		.of_match_table = mtk_disp_gamma_driver_dt_match,
	},
};
