// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <drm/drm_fourcc.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/soc/mediatek/mtk-cmdq.h>

#include "mtk_disp_drv.h"
#include "mtk_drm_drv.h"
#include "mtk_mdp_rdma.h"

#define MDP_RDMA_EN			0x000
#define FLD_ROT_ENABLE				BIT(0)
#define MDP_RDMA_RESET			0x008
#define MDP_RDMA_CON			0x020
#define FLD_OUTPUT_10B				BIT(5)
#define FLD_SIMPLE_MODE				BIT(4)
#define MDP_RDMA_GMCIF_CON		0x028
#define FLD_COMMAND_DIV				BIT(0)
#define FLD_EXT_PREULTRA_EN			BIT(3)
#define FLD_RD_REQ_TYPE				GENMASK(7, 4)
#define VAL_RD_REQ_TYPE_BURST_8_ACCESS		7
#define FLD_ULTRA_EN				GENMASK(13, 12)
#define VAL_ULTRA_EN_ENABLE			1
#define FLD_PRE_ULTRA_EN			GENMASK(17, 16)
#define VAL_PRE_ULTRA_EN_ENABLE			1
#define FLD_EXT_ULTRA_EN			BIT(18)
#define MDP_RDMA_SRC_CON		0x030
#define FLD_OUTPUT_ARGB				BIT(25)
#define FLD_BIT_NUMBER				GENMASK(19, 18)
#define FLD_SWAP				BIT(14)
#define FLD_UNIFORM_CONFIG			BIT(17)
#define RDMA_INPUT_10BIT			BIT(18)
#define FLD_SRC_FORMAT				GENMASK(3, 0)
#define MDP_RDMA_COMP_CON		0x038
#define FLD_AFBC_EN				BIT(22)
#define FLD_AFBC_YUV_TRANSFORM			BIT(21)
#define FLD_UFBDC_EN				BIT(12)
#define MDP_RDMA_MF_BKGD_SIZE_IN_BYTE	0x060
#define FLD_MF_BKGD_WB				GENMASK(22, 0)
#define MDP_RDMA_MF_SRC_SIZE		0x070
#define FLD_MF_SRC_H				GENMASK(30, 16)
#define FLD_MF_SRC_W				GENMASK(14, 0)
#define MDP_RDMA_MF_CLIP_SIZE		0x078
#define FLD_MF_CLIP_H				GENMASK(30, 16)
#define FLD_MF_CLIP_W				GENMASK(14, 0)
#define MDP_RDMA_SRC_OFFSET_0		0x118
#define FLD_SRC_OFFSET_0			GENMASK(31, 0)
#define MDP_RDMA_TRANSFORM_0		0x200
#define FLD_INT_MATRIX_SEL			GENMASK(27, 23)
#define FLD_TRANS_EN				BIT(16)
#define MDP_RDMA_SRC_BASE_0		0xf00
#define FLD_SRC_BASE_0				GENMASK(31, 0)

#define RDMA_CSC_FULL709_TO_RGB			5
#define RDMA_CSC_BT601_TO_RGB			6

static const u32 formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YUYV,
};

enum rdma_format {
	RDMA_INPUT_FORMAT_RGB565 = 0,
	RDMA_INPUT_FORMAT_RGB888 = 1,
	RDMA_INPUT_FORMAT_RGBA8888 = 2,
	RDMA_INPUT_FORMAT_ARGB8888 = 3,
	RDMA_INPUT_FORMAT_UYVY = 4,
	RDMA_INPUT_FORMAT_YUY2 = 5,
	RDMA_INPUT_FORMAT_Y8 = 7,
	RDMA_INPUT_FORMAT_YV12 = 8,
	RDMA_INPUT_FORMAT_UYVY_3PL = 9,
	RDMA_INPUT_FORMAT_NV12 = 12,
	RDMA_INPUT_FORMAT_UYVY_2PL = 13,
	RDMA_INPUT_FORMAT_Y410 = 14
};

struct mtk_mdp_rdma {
	void __iomem		*regs;
	struct clk		*clk;
	struct cmdq_client_reg	cmdq_reg;
};

static unsigned int rdma_fmt_convert(unsigned int fmt)
{
	switch (fmt) {
	default:
	case DRM_FORMAT_RGB565:
		return RDMA_INPUT_FORMAT_RGB565;
	case DRM_FORMAT_BGR565:
		return RDMA_INPUT_FORMAT_RGB565 | FLD_SWAP;
	case DRM_FORMAT_RGB888:
		return RDMA_INPUT_FORMAT_RGB888;
	case DRM_FORMAT_BGR888:
		return RDMA_INPUT_FORMAT_RGB888 | FLD_SWAP;
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_RGBA8888:
		return RDMA_INPUT_FORMAT_ARGB8888;
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_BGRA8888:
		return RDMA_INPUT_FORMAT_ARGB8888 | FLD_SWAP;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		return RDMA_INPUT_FORMAT_RGBA8888;
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		return RDMA_INPUT_FORMAT_RGBA8888 | FLD_SWAP;
	case DRM_FORMAT_ABGR2101010:
		return RDMA_INPUT_FORMAT_RGBA8888 | FLD_SWAP | RDMA_INPUT_10BIT;
	case DRM_FORMAT_ARGB2101010:
		return RDMA_INPUT_FORMAT_RGBA8888 | RDMA_INPUT_10BIT;
	case DRM_FORMAT_RGBA1010102:
		return RDMA_INPUT_FORMAT_ARGB8888 | FLD_SWAP | RDMA_INPUT_10BIT;
	case DRM_FORMAT_BGRA1010102:
		return RDMA_INPUT_FORMAT_ARGB8888 | RDMA_INPUT_10BIT;
	case DRM_FORMAT_UYVY:
		return RDMA_INPUT_FORMAT_UYVY;
	case DRM_FORMAT_YUYV:
		return RDMA_INPUT_FORMAT_YUY2;
	}
}

static unsigned int rdma_color_convert(unsigned int color_encoding)
{
	switch (color_encoding) {
	default:
	case DRM_COLOR_YCBCR_BT709:
		return RDMA_CSC_FULL709_TO_RGB;
	case DRM_COLOR_YCBCR_BT601:
		return RDMA_CSC_BT601_TO_RGB;
	}
}

static void mtk_mdp_rdma_fifo_config(struct device *dev, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_mdp_rdma *priv = dev_get_drvdata(dev);

	mtk_ddp_write_mask(cmdq_pkt, FLD_EXT_ULTRA_EN | VAL_PRE_ULTRA_EN_ENABLE << 16 |
			   VAL_ULTRA_EN_ENABLE << 12 | VAL_RD_REQ_TYPE_BURST_8_ACCESS << 4 |
			   FLD_EXT_PREULTRA_EN | FLD_COMMAND_DIV, &priv->cmdq_reg,
			   priv->regs, MDP_RDMA_GMCIF_CON, FLD_EXT_ULTRA_EN |
			   FLD_PRE_ULTRA_EN | FLD_ULTRA_EN | FLD_RD_REQ_TYPE |
			   FLD_EXT_PREULTRA_EN | FLD_COMMAND_DIV);
}

void mtk_mdp_rdma_start(struct device *dev, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_mdp_rdma *priv = dev_get_drvdata(dev);

	mtk_ddp_write_mask(cmdq_pkt, FLD_ROT_ENABLE, &priv->cmdq_reg,
			   priv->regs, MDP_RDMA_EN, FLD_ROT_ENABLE);
}

void mtk_mdp_rdma_stop(struct device *dev, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_mdp_rdma *priv = dev_get_drvdata(dev);

	mtk_ddp_write_mask(cmdq_pkt, 0, &priv->cmdq_reg,
			   priv->regs, MDP_RDMA_EN, FLD_ROT_ENABLE);
	mtk_ddp_write(cmdq_pkt, 1, &priv->cmdq_reg, priv->regs, MDP_RDMA_RESET);
	mtk_ddp_write(cmdq_pkt, 0, &priv->cmdq_reg, priv->regs, MDP_RDMA_RESET);
}

void mtk_mdp_rdma_config(struct device *dev, struct mtk_mdp_rdma_cfg *cfg,
			 struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_mdp_rdma *priv = dev_get_drvdata(dev);
	const struct drm_format_info *fmt_info = drm_format_info(cfg->fmt);
	bool csc_enable = fmt_info->is_yuv ? true : false;
	unsigned int src_pitch_y = cfg->pitch;
	unsigned int offset_y = 0;

	mtk_mdp_rdma_fifo_config(dev, cmdq_pkt);

	mtk_ddp_write_mask(cmdq_pkt, FLD_UNIFORM_CONFIG, &priv->cmdq_reg, priv->regs,
			   MDP_RDMA_SRC_CON, FLD_UNIFORM_CONFIG);
	mtk_ddp_write_mask(cmdq_pkt, rdma_fmt_convert(cfg->fmt), &priv->cmdq_reg, priv->regs,
			   MDP_RDMA_SRC_CON, FLD_SWAP | FLD_SRC_FORMAT | FLD_BIT_NUMBER);

	if (!csc_enable && fmt_info->has_alpha)
		mtk_ddp_write_mask(cmdq_pkt, FLD_OUTPUT_ARGB, &priv->cmdq_reg,
				   priv->regs, MDP_RDMA_SRC_CON, FLD_OUTPUT_ARGB);
	else
		mtk_ddp_write_mask(cmdq_pkt, 0, &priv->cmdq_reg, priv->regs,
				   MDP_RDMA_SRC_CON, FLD_OUTPUT_ARGB);

	mtk_ddp_write_mask(cmdq_pkt, cfg->addr0, &priv->cmdq_reg, priv->regs,
			   MDP_RDMA_SRC_BASE_0, FLD_SRC_BASE_0);

	mtk_ddp_write_mask(cmdq_pkt, src_pitch_y, &priv->cmdq_reg, priv->regs,
			   MDP_RDMA_MF_BKGD_SIZE_IN_BYTE, FLD_MF_BKGD_WB);

	mtk_ddp_write_mask(cmdq_pkt, 0, &priv->cmdq_reg, priv->regs, MDP_RDMA_COMP_CON,
			   FLD_AFBC_YUV_TRANSFORM | FLD_UFBDC_EN | FLD_AFBC_EN);
	mtk_ddp_write_mask(cmdq_pkt, FLD_OUTPUT_10B, &priv->cmdq_reg, priv->regs,
			   MDP_RDMA_CON, FLD_OUTPUT_10B);
	mtk_ddp_write_mask(cmdq_pkt, FLD_SIMPLE_MODE, &priv->cmdq_reg, priv->regs,
			   MDP_RDMA_CON, FLD_SIMPLE_MODE);
	if (csc_enable)
		mtk_ddp_write_mask(cmdq_pkt, rdma_color_convert(cfg->color_encoding) << 23,
				   &priv->cmdq_reg, priv->regs, MDP_RDMA_TRANSFORM_0,
				   FLD_INT_MATRIX_SEL);
	mtk_ddp_write_mask(cmdq_pkt, csc_enable << 16, &priv->cmdq_reg, priv->regs,
			   MDP_RDMA_TRANSFORM_0, FLD_TRANS_EN);

	offset_y  = cfg->x_left * fmt_info->cpp[0] + cfg->y_top * src_pitch_y;

	mtk_ddp_write_mask(cmdq_pkt, offset_y, &priv->cmdq_reg, priv->regs,
			   MDP_RDMA_SRC_OFFSET_0, FLD_SRC_OFFSET_0);
	mtk_ddp_write_mask(cmdq_pkt, cfg->width, &priv->cmdq_reg, priv->regs,
			   MDP_RDMA_MF_SRC_SIZE, FLD_MF_SRC_W);
	mtk_ddp_write_mask(cmdq_pkt, cfg->height << 16, &priv->cmdq_reg, priv->regs,
			   MDP_RDMA_MF_SRC_SIZE, FLD_MF_SRC_H);
	mtk_ddp_write_mask(cmdq_pkt, cfg->width, &priv->cmdq_reg, priv->regs,
			   MDP_RDMA_MF_CLIP_SIZE, FLD_MF_CLIP_W);
	mtk_ddp_write_mask(cmdq_pkt, cfg->height << 16, &priv->cmdq_reg, priv->regs,
			   MDP_RDMA_MF_CLIP_SIZE, FLD_MF_CLIP_H);
}

const u32 *mtk_mdp_rdma_get_formats(struct device *dev)
{
	return formats;
}

size_t mtk_mdp_rdma_get_num_formats(struct device *dev)
{
	return ARRAY_SIZE(formats);
}

int mtk_mdp_rdma_clk_enable(struct device *dev)
{
	struct mtk_mdp_rdma *rdma = dev_get_drvdata(dev);

	clk_prepare_enable(rdma->clk);
	return 0;
}

void mtk_mdp_rdma_clk_disable(struct device *dev)
{
	struct mtk_mdp_rdma *rdma = dev_get_drvdata(dev);

	clk_disable_unprepare(rdma->clk);
}

static int mtk_mdp_rdma_bind(struct device *dev, struct device *master,
			     void *data)
{
	return 0;
}

static void mtk_mdp_rdma_unbind(struct device *dev, struct device *master,
				void *data)
{
}

static const struct component_ops mtk_mdp_rdma_component_ops = {
	.bind	= mtk_mdp_rdma_bind,
	.unbind = mtk_mdp_rdma_unbind,
};

static int mtk_mdp_rdma_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct mtk_mdp_rdma *priv;
	int ret = 0;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->regs)) {
		dev_err(dev, "failed to ioremap rdma\n");
		return PTR_ERR(priv->regs);
	}

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "failed to get rdma clk\n");
		return PTR_ERR(priv->clk);
	}

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	ret = cmdq_dev_get_client_reg(dev, &priv->cmdq_reg, 0);
	if (ret)
		dev_dbg(dev, "get mediatek,gce-client-reg fail!\n");
#endif
	platform_set_drvdata(pdev, priv);

	pm_runtime_enable(dev);

	ret = component_add(dev, &mtk_mdp_rdma_component_ops);
	if (ret != 0) {
		pm_runtime_disable(dev);
		dev_err(dev, "Failed to add component: %d\n", ret);
	}
	return ret;
}

static int mtk_mdp_rdma_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_mdp_rdma_component_ops);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static const struct of_device_id mtk_mdp_rdma_driver_dt_match[] = {
	{ .compatible = "mediatek,mt8195-vdo1-rdma", },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_mdp_rdma_driver_dt_match);

struct platform_driver mtk_mdp_rdma_driver = {
	.probe = mtk_mdp_rdma_probe,
	.remove = mtk_mdp_rdma_remove,
	.driver = {
		.name = "mediatek-mdp-rdma",
		.owner = THIS_MODULE,
		.of_match_table = mtk_mdp_rdma_driver_dt_match,
	},
};
