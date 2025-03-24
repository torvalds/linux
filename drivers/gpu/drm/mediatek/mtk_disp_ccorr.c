// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/soc/mediatek/mtk-cmdq.h>

#include "mtk_crtc.h"
#include "mtk_ddp_comp.h"
#include "mtk_disp_drv.h"
#include "mtk_drm_drv.h"

#define DISP_CCORR_EN				0x0000
#define CCORR_EN					BIT(0)
#define DISP_CCORR_CFG				0x0020
#define CCORR_RELAY_MODE				BIT(0)
#define CCORR_ENGINE_EN					BIT(1)
#define CCORR_GAMMA_OFF					BIT(2)
#define CCORR_WGAMUT_SRC_CLIP				BIT(3)
#define DISP_CCORR_SIZE				0x0030
#define DISP_CCORR_COEF_0			0x0080
#define DISP_CCORR_COEF_1			0x0084
#define DISP_CCORR_COEF_2			0x0088
#define DISP_CCORR_COEF_3			0x008C
#define DISP_CCORR_COEF_4			0x0090

struct mtk_disp_ccorr_data {
	u32 matrix_bits;
};

struct mtk_disp_ccorr {
	struct clk *clk;
	void __iomem *regs;
	struct cmdq_client_reg cmdq_reg;
	const struct mtk_disp_ccorr_data	*data;
};

int mtk_ccorr_clk_enable(struct device *dev)
{
	struct mtk_disp_ccorr *ccorr = dev_get_drvdata(dev);

	return clk_prepare_enable(ccorr->clk);
}

void mtk_ccorr_clk_disable(struct device *dev)
{
	struct mtk_disp_ccorr *ccorr = dev_get_drvdata(dev);

	clk_disable_unprepare(ccorr->clk);
}

void mtk_ccorr_config(struct device *dev, unsigned int w,
			     unsigned int h, unsigned int vrefresh,
			     unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_ccorr *ccorr = dev_get_drvdata(dev);

	mtk_ddp_write(cmdq_pkt, w << 16 | h, &ccorr->cmdq_reg, ccorr->regs,
		      DISP_CCORR_SIZE);
	mtk_ddp_write(cmdq_pkt, CCORR_ENGINE_EN, &ccorr->cmdq_reg, ccorr->regs,
		      DISP_CCORR_CFG);
}

void mtk_ccorr_start(struct device *dev)
{
	struct mtk_disp_ccorr *ccorr = dev_get_drvdata(dev);

	writel(CCORR_EN, ccorr->regs + DISP_CCORR_EN);
}

void mtk_ccorr_stop(struct device *dev)
{
	struct mtk_disp_ccorr *ccorr = dev_get_drvdata(dev);

	writel_relaxed(0x0, ccorr->regs + DISP_CCORR_EN);
}

/* Converts a DRM S31.32 value to the HW S1.n format. */
static u16 mtk_ctm_s31_32_to_s1_n(u64 in, u32 n)
{
	u16 r;

	/* Sign bit. */
	r = in & BIT_ULL(63) ? BIT(n + 1) : 0;

	if ((in & GENMASK_ULL(62, 33)) > 0) {
		/* identity value 0x100000000 -> 0x400(mt8183), */
		/* identity value 0x100000000 -> 0x800(mt8192), */
		/* if bigger this, set it to max 0x7ff. */
		r |= GENMASK(n, 0);
	} else {
		/* take the n+1 most important bits. */
		r |= (in >> (32 - n)) & GENMASK(n, 0);
	}

	return r;
}

void mtk_ccorr_ctm_set(struct device *dev, struct drm_crtc_state *state)
{
	struct mtk_disp_ccorr *ccorr = dev_get_drvdata(dev);
	struct drm_property_blob *blob = state->ctm;
	struct drm_color_ctm *ctm;
	const u64 *input;
	uint16_t coeffs[9] = { 0 };
	int i;
	struct cmdq_pkt *cmdq_pkt = NULL;
	u32 matrix_bits = ccorr->data->matrix_bits;

	if (!blob)
		return;

	ctm = (struct drm_color_ctm *)blob->data;
	input = ctm->matrix;

	for (i = 0; i < ARRAY_SIZE(coeffs); i++)
		coeffs[i] = mtk_ctm_s31_32_to_s1_n(input[i], matrix_bits);

	mtk_ddp_write(cmdq_pkt, coeffs[0] << 16 | coeffs[1],
		      &ccorr->cmdq_reg, ccorr->regs, DISP_CCORR_COEF_0);
	mtk_ddp_write(cmdq_pkt, coeffs[2] << 16 | coeffs[3],
		      &ccorr->cmdq_reg, ccorr->regs, DISP_CCORR_COEF_1);
	mtk_ddp_write(cmdq_pkt, coeffs[4] << 16 | coeffs[5],
		      &ccorr->cmdq_reg, ccorr->regs, DISP_CCORR_COEF_2);
	mtk_ddp_write(cmdq_pkt, coeffs[6] << 16 | coeffs[7],
		      &ccorr->cmdq_reg, ccorr->regs, DISP_CCORR_COEF_3);
	mtk_ddp_write(cmdq_pkt, coeffs[8] << 16,
		      &ccorr->cmdq_reg, ccorr->regs, DISP_CCORR_COEF_4);
}

static int mtk_disp_ccorr_bind(struct device *dev, struct device *master,
			       void *data)
{
	return 0;
}

static void mtk_disp_ccorr_unbind(struct device *dev, struct device *master,
				  void *data)
{
}

static const struct component_ops mtk_disp_ccorr_component_ops = {
	.bind	= mtk_disp_ccorr_bind,
	.unbind	= mtk_disp_ccorr_unbind,
};

static int mtk_disp_ccorr_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_disp_ccorr *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk),
				     "failed to get ccorr clk\n");

	priv->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->regs))
		return dev_err_probe(dev, PTR_ERR(priv->regs),
				     "failed to ioremap ccorr\n");

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	ret = cmdq_dev_get_client_reg(dev, &priv->cmdq_reg, 0);
	if (ret)
		dev_dbg(dev, "get mediatek,gce-client-reg fail!\n");
#endif

	priv->data = of_device_get_match_data(dev);
	platform_set_drvdata(pdev, priv);

	ret = component_add(dev, &mtk_disp_ccorr_component_ops);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add component\n");

	return 0;
}

static void mtk_disp_ccorr_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_disp_ccorr_component_ops);
}

static const struct mtk_disp_ccorr_data mt8183_ccorr_driver_data = {
	.matrix_bits = 10,
};

static const struct mtk_disp_ccorr_data mt8192_ccorr_driver_data = {
	.matrix_bits = 11,
};

static const struct of_device_id mtk_disp_ccorr_driver_dt_match[] = {
	{ .compatible = "mediatek,mt8183-disp-ccorr",
	  .data = &mt8183_ccorr_driver_data},
	{ .compatible = "mediatek,mt8192-disp-ccorr",
	  .data = &mt8192_ccorr_driver_data},
	{},
};
MODULE_DEVICE_TABLE(of, mtk_disp_ccorr_driver_dt_match);

struct platform_driver mtk_disp_ccorr_driver = {
	.probe		= mtk_disp_ccorr_probe,
	.remove		= mtk_disp_ccorr_remove,
	.driver		= {
		.name	= "mediatek-disp-ccorr",
		.of_match_table = mtk_disp_ccorr_driver_dt_match,
	},
};
