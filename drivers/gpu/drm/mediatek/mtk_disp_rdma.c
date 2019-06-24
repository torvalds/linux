// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#include <drm/drmP.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

#include "mtk_drm_crtc.h"
#include "mtk_drm_ddp_comp.h"

#define DISP_REG_RDMA_INT_ENABLE		0x0000
#define DISP_REG_RDMA_INT_STATUS		0x0004
#define RDMA_TARGET_LINE_INT				BIT(5)
#define RDMA_FIFO_UNDERFLOW_INT				BIT(4)
#define RDMA_EOF_ABNORMAL_INT				BIT(3)
#define RDMA_FRAME_END_INT				BIT(2)
#define RDMA_FRAME_START_INT				BIT(1)
#define RDMA_REG_UPDATE_INT				BIT(0)
#define DISP_REG_RDMA_GLOBAL_CON		0x0010
#define RDMA_ENGINE_EN					BIT(0)
#define RDMA_MODE_MEMORY				BIT(1)
#define DISP_REG_RDMA_SIZE_CON_0		0x0014
#define RDMA_MATRIX_ENABLE				BIT(17)
#define RDMA_MATRIX_INT_MTX_SEL				GENMASK(23, 20)
#define RDMA_MATRIX_INT_MTX_BT601_to_RGB		(6 << 20)
#define DISP_REG_RDMA_SIZE_CON_1		0x0018
#define DISP_REG_RDMA_TARGET_LINE		0x001c
#define DISP_RDMA_MEM_CON			0x0024
#define MEM_MODE_INPUT_FORMAT_RGB565			(0x000 << 4)
#define MEM_MODE_INPUT_FORMAT_RGB888			(0x001 << 4)
#define MEM_MODE_INPUT_FORMAT_RGBA8888			(0x002 << 4)
#define MEM_MODE_INPUT_FORMAT_ARGB8888			(0x003 << 4)
#define MEM_MODE_INPUT_FORMAT_UYVY			(0x004 << 4)
#define MEM_MODE_INPUT_FORMAT_YUYV			(0x005 << 4)
#define MEM_MODE_INPUT_SWAP				BIT(8)
#define DISP_RDMA_MEM_SRC_PITCH			0x002c
#define DISP_RDMA_MEM_GMC_SETTING_0		0x0030
#define DISP_REG_RDMA_FIFO_CON			0x0040
#define RDMA_FIFO_UNDERFLOW_EN				BIT(31)
#define RDMA_FIFO_PSEUDO_SIZE(bytes)			(((bytes) / 16) << 16)
#define RDMA_OUTPUT_VALID_FIFO_THRESHOLD(bytes)		((bytes) / 16)
#define RDMA_FIFO_SIZE(rdma)			((rdma)->data->fifo_size)
#define DISP_RDMA_MEM_START_ADDR		0x0f00

#define RDMA_MEM_GMC				0x40402020

struct mtk_disp_rdma_data {
	unsigned int fifo_size;
};

/**
 * struct mtk_disp_rdma - DISP_RDMA driver structure
 * @ddp_comp - structure containing type enum and hardware resources
 * @crtc - associated crtc to report irq events to
 */
struct mtk_disp_rdma {
	struct mtk_ddp_comp		ddp_comp;
	struct drm_crtc			*crtc;
	const struct mtk_disp_rdma_data	*data;
};

static inline struct mtk_disp_rdma *comp_to_rdma(struct mtk_ddp_comp *comp)
{
	return container_of(comp, struct mtk_disp_rdma, ddp_comp);
}

static irqreturn_t mtk_disp_rdma_irq_handler(int irq, void *dev_id)
{
	struct mtk_disp_rdma *priv = dev_id;
	struct mtk_ddp_comp *rdma = &priv->ddp_comp;

	/* Clear frame completion interrupt */
	writel(0x0, rdma->regs + DISP_REG_RDMA_INT_STATUS);

	if (!priv->crtc)
		return IRQ_NONE;

	mtk_crtc_ddp_irq(priv->crtc, rdma);

	return IRQ_HANDLED;
}

static void rdma_update_bits(struct mtk_ddp_comp *comp, unsigned int reg,
			     unsigned int mask, unsigned int val)
{
	unsigned int tmp = readl(comp->regs + reg);

	tmp = (tmp & ~mask) | (val & mask);
	writel(tmp, comp->regs + reg);
}

static void mtk_rdma_enable_vblank(struct mtk_ddp_comp *comp,
				   struct drm_crtc *crtc)
{
	struct mtk_disp_rdma *rdma = comp_to_rdma(comp);

	rdma->crtc = crtc;
	rdma_update_bits(comp, DISP_REG_RDMA_INT_ENABLE, RDMA_FRAME_END_INT,
			 RDMA_FRAME_END_INT);
}

static void mtk_rdma_disable_vblank(struct mtk_ddp_comp *comp)
{
	struct mtk_disp_rdma *rdma = comp_to_rdma(comp);

	rdma->crtc = NULL;
	rdma_update_bits(comp, DISP_REG_RDMA_INT_ENABLE, RDMA_FRAME_END_INT, 0);
}

static void mtk_rdma_start(struct mtk_ddp_comp *comp)
{
	rdma_update_bits(comp, DISP_REG_RDMA_GLOBAL_CON, RDMA_ENGINE_EN,
			 RDMA_ENGINE_EN);
}

static void mtk_rdma_stop(struct mtk_ddp_comp *comp)
{
	rdma_update_bits(comp, DISP_REG_RDMA_GLOBAL_CON, RDMA_ENGINE_EN, 0);
}

static void mtk_rdma_config(struct mtk_ddp_comp *comp, unsigned int width,
			    unsigned int height, unsigned int vrefresh,
			    unsigned int bpc)
{
	unsigned int threshold;
	unsigned int reg;
	struct mtk_disp_rdma *rdma = comp_to_rdma(comp);

	rdma_update_bits(comp, DISP_REG_RDMA_SIZE_CON_0, 0xfff, width);
	rdma_update_bits(comp, DISP_REG_RDMA_SIZE_CON_1, 0xfffff, height);

	/*
	 * Enable FIFO underflow since DSI and DPI can't be blocked.
	 * Keep the FIFO pseudo size reset default of 8 KiB. Set the
	 * output threshold to 6 microseconds with 7/6 overhead to
	 * account for blanking, and with a pixel depth of 4 bytes:
	 */
	threshold = width * height * vrefresh * 4 * 7 / 1000000;
	reg = RDMA_FIFO_UNDERFLOW_EN |
	      RDMA_FIFO_PSEUDO_SIZE(RDMA_FIFO_SIZE(rdma)) |
	      RDMA_OUTPUT_VALID_FIFO_THRESHOLD(threshold);
	writel(reg, comp->regs + DISP_REG_RDMA_FIFO_CON);
}

static unsigned int rdma_fmt_convert(struct mtk_disp_rdma *rdma,
				     unsigned int fmt)
{
	/* The return value in switch "MEM_MODE_INPUT_FORMAT_XXX"
	 * is defined in mediatek HW data sheet.
	 * The alphabet order in XXX is no relation to data
	 * arrangement in memory.
	 */
	switch (fmt) {
	default:
	case DRM_FORMAT_RGB565:
		return MEM_MODE_INPUT_FORMAT_RGB565;
	case DRM_FORMAT_BGR565:
		return MEM_MODE_INPUT_FORMAT_RGB565 | MEM_MODE_INPUT_SWAP;
	case DRM_FORMAT_RGB888:
		return MEM_MODE_INPUT_FORMAT_RGB888;
	case DRM_FORMAT_BGR888:
		return MEM_MODE_INPUT_FORMAT_RGB888 | MEM_MODE_INPUT_SWAP;
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_RGBA8888:
		return MEM_MODE_INPUT_FORMAT_ARGB8888;
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_BGRA8888:
		return MEM_MODE_INPUT_FORMAT_ARGB8888 | MEM_MODE_INPUT_SWAP;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		return MEM_MODE_INPUT_FORMAT_RGBA8888;
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		return MEM_MODE_INPUT_FORMAT_RGBA8888 | MEM_MODE_INPUT_SWAP;
	case DRM_FORMAT_UYVY:
		return MEM_MODE_INPUT_FORMAT_UYVY;
	case DRM_FORMAT_YUYV:
		return MEM_MODE_INPUT_FORMAT_YUYV;
	}
}

static unsigned int mtk_rdma_layer_nr(struct mtk_ddp_comp *comp)
{
	return 1;
}

static void mtk_rdma_layer_config(struct mtk_ddp_comp *comp, unsigned int idx,
				  struct mtk_plane_state *state)
{
	struct mtk_disp_rdma *rdma = comp_to_rdma(comp);
	struct mtk_plane_pending_state *pending = &state->pending;
	unsigned int addr = pending->addr;
	unsigned int pitch = pending->pitch & 0xffff;
	unsigned int fmt = pending->format;
	unsigned int con;

	con = rdma_fmt_convert(rdma, fmt);
	writel_relaxed(con, comp->regs + DISP_RDMA_MEM_CON);

	if (fmt == DRM_FORMAT_UYVY || fmt == DRM_FORMAT_YUYV) {
		rdma_update_bits(comp, DISP_REG_RDMA_SIZE_CON_0,
				 RDMA_MATRIX_ENABLE, RDMA_MATRIX_ENABLE);
		rdma_update_bits(comp, DISP_REG_RDMA_SIZE_CON_0,
				 RDMA_MATRIX_INT_MTX_SEL,
				 RDMA_MATRIX_INT_MTX_BT601_to_RGB);
	} else {
		rdma_update_bits(comp, DISP_REG_RDMA_SIZE_CON_0,
				 RDMA_MATRIX_ENABLE, 0);
	}

	writel_relaxed(addr, comp->regs + DISP_RDMA_MEM_START_ADDR);
	writel_relaxed(pitch, comp->regs + DISP_RDMA_MEM_SRC_PITCH);
	writel(RDMA_MEM_GMC, comp->regs + DISP_RDMA_MEM_GMC_SETTING_0);
	rdma_update_bits(comp, DISP_REG_RDMA_GLOBAL_CON,
			 RDMA_MODE_MEMORY, RDMA_MODE_MEMORY);
}

static const struct mtk_ddp_comp_funcs mtk_disp_rdma_funcs = {
	.config = mtk_rdma_config,
	.start = mtk_rdma_start,
	.stop = mtk_rdma_stop,
	.enable_vblank = mtk_rdma_enable_vblank,
	.disable_vblank = mtk_rdma_disable_vblank,
	.layer_nr = mtk_rdma_layer_nr,
	.layer_config = mtk_rdma_layer_config,
};

static int mtk_disp_rdma_bind(struct device *dev, struct device *master,
			      void *data)
{
	struct mtk_disp_rdma *priv = dev_get_drvdata(dev);
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

static void mtk_disp_rdma_unbind(struct device *dev, struct device *master,
				 void *data)
{
	struct mtk_disp_rdma *priv = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;

	mtk_ddp_comp_unregister(drm_dev, &priv->ddp_comp);
}

static const struct component_ops mtk_disp_rdma_component_ops = {
	.bind	= mtk_disp_rdma_bind,
	.unbind = mtk_disp_rdma_unbind,
};

static int mtk_disp_rdma_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_disp_rdma *priv;
	int comp_id;
	int irq;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	comp_id = mtk_ddp_comp_get_id(dev->of_node, MTK_DISP_RDMA);
	if (comp_id < 0) {
		dev_err(dev, "Failed to identify by alias: %d\n", comp_id);
		return comp_id;
	}

	ret = mtk_ddp_comp_init(dev, dev->of_node, &priv->ddp_comp, comp_id,
				&mtk_disp_rdma_funcs);
	if (ret) {
		dev_err(dev, "Failed to initialize component: %d\n", ret);
		return ret;
	}

	/* Disable and clear pending interrupts */
	writel(0x0, priv->ddp_comp.regs + DISP_REG_RDMA_INT_ENABLE);
	writel(0x0, priv->ddp_comp.regs + DISP_REG_RDMA_INT_STATUS);

	ret = devm_request_irq(dev, irq, mtk_disp_rdma_irq_handler,
			       IRQF_TRIGGER_NONE, dev_name(dev), priv);
	if (ret < 0) {
		dev_err(dev, "Failed to request irq %d: %d\n", irq, ret);
		return ret;
	}

	priv->data = of_device_get_match_data(dev);

	platform_set_drvdata(pdev, priv);

	ret = component_add(dev, &mtk_disp_rdma_component_ops);
	if (ret)
		dev_err(dev, "Failed to add component: %d\n", ret);

	return ret;
}

static int mtk_disp_rdma_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_disp_rdma_component_ops);

	return 0;
}

static const struct mtk_disp_rdma_data mt2701_rdma_driver_data = {
	.fifo_size = SZ_4K,
};

static const struct mtk_disp_rdma_data mt8173_rdma_driver_data = {
	.fifo_size = SZ_8K,
};

static const struct of_device_id mtk_disp_rdma_driver_dt_match[] = {
	{ .compatible = "mediatek,mt2701-disp-rdma",
	  .data = &mt2701_rdma_driver_data},
	{ .compatible = "mediatek,mt8173-disp-rdma",
	  .data = &mt8173_rdma_driver_data},
	{},
};
MODULE_DEVICE_TABLE(of, mtk_disp_rdma_driver_dt_match);

struct platform_driver mtk_disp_rdma_driver = {
	.probe		= mtk_disp_rdma_probe,
	.remove		= mtk_disp_rdma_remove,
	.driver		= {
		.name	= "mediatek-disp-rdma",
		.owner	= THIS_MODULE,
		.of_match_table = mtk_disp_rdma_driver_dt_match,
	},
};
