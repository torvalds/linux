// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/soc/mediatek/mtk-cmdq.h>

#include "mtk_disp_drv.h"
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

/*
 * struct mtk_disp_rdma - DISP_RDMA driver structure
 * @data: local driver data
 */
struct mtk_disp_rdma {
	struct clk			*clk;
	void __iomem			*regs;
	struct cmdq_client_reg		cmdq_reg;
	const struct mtk_disp_rdma_data	*data;
	void				(*vblank_cb)(void *data);
	void				*vblank_cb_data;
	u32				fifo_size;
};

static irqreturn_t mtk_disp_rdma_irq_handler(int irq, void *dev_id)
{
	struct mtk_disp_rdma *priv = dev_id;

	/* Clear frame completion interrupt */
	writel(0x0, priv->regs + DISP_REG_RDMA_INT_STATUS);

	if (!priv->vblank_cb)
		return IRQ_NONE;

	priv->vblank_cb(priv->vblank_cb_data);

	return IRQ_HANDLED;
}

static void rdma_update_bits(struct device *dev, unsigned int reg,
			     unsigned int mask, unsigned int val)
{
	struct mtk_disp_rdma *rdma = dev_get_drvdata(dev);
	unsigned int tmp = readl(rdma->regs + reg);

	tmp = (tmp & ~mask) | (val & mask);
	writel(tmp, rdma->regs + reg);
}

void mtk_rdma_enable_vblank(struct device *dev,
			    void (*vblank_cb)(void *),
			    void *vblank_cb_data)
{
	struct mtk_disp_rdma *rdma = dev_get_drvdata(dev);

	rdma->vblank_cb = vblank_cb;
	rdma->vblank_cb_data = vblank_cb_data;
	rdma_update_bits(dev, DISP_REG_RDMA_INT_ENABLE, RDMA_FRAME_END_INT,
			 RDMA_FRAME_END_INT);
}

void mtk_rdma_disable_vblank(struct device *dev)
{
	struct mtk_disp_rdma *rdma = dev_get_drvdata(dev);

	rdma->vblank_cb = NULL;
	rdma->vblank_cb_data = NULL;
	rdma_update_bits(dev, DISP_REG_RDMA_INT_ENABLE, RDMA_FRAME_END_INT, 0);
}

int mtk_rdma_clk_enable(struct device *dev)
{
	struct mtk_disp_rdma *rdma = dev_get_drvdata(dev);

	return clk_prepare_enable(rdma->clk);
}

void mtk_rdma_clk_disable(struct device *dev)
{
	struct mtk_disp_rdma *rdma = dev_get_drvdata(dev);

	clk_disable_unprepare(rdma->clk);
}

void mtk_rdma_start(struct device *dev)
{
	rdma_update_bits(dev, DISP_REG_RDMA_GLOBAL_CON, RDMA_ENGINE_EN,
			 RDMA_ENGINE_EN);
}

void mtk_rdma_stop(struct device *dev)
{
	rdma_update_bits(dev, DISP_REG_RDMA_GLOBAL_CON, RDMA_ENGINE_EN, 0);
}

void mtk_rdma_config(struct device *dev, unsigned int width,
		     unsigned int height, unsigned int vrefresh,
		     unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	unsigned int threshold;
	unsigned int reg;
	struct mtk_disp_rdma *rdma = dev_get_drvdata(dev);
	u32 rdma_fifo_size;

	mtk_ddp_write_mask(cmdq_pkt, width, &rdma->cmdq_reg, rdma->regs,
			   DISP_REG_RDMA_SIZE_CON_0, 0xfff);
	mtk_ddp_write_mask(cmdq_pkt, height, &rdma->cmdq_reg, rdma->regs,
			   DISP_REG_RDMA_SIZE_CON_1, 0xfffff);

	if (rdma->fifo_size)
		rdma_fifo_size = rdma->fifo_size;
	else
		rdma_fifo_size = RDMA_FIFO_SIZE(rdma);

	/*
	 * Enable FIFO underflow since DSI and DPI can't be blocked.
	 * Keep the FIFO pseudo size reset default of 8 KiB. Set the
	 * output threshold to 70% of max fifo size to make sure the
	 * threhold will not overflow
	 */
	threshold = rdma_fifo_size * 7 / 10;
	reg = RDMA_FIFO_UNDERFLOW_EN |
	      RDMA_FIFO_PSEUDO_SIZE(rdma_fifo_size) |
	      RDMA_OUTPUT_VALID_FIFO_THRESHOLD(threshold);
	mtk_ddp_write(cmdq_pkt, reg, &rdma->cmdq_reg, rdma->regs, DISP_REG_RDMA_FIFO_CON);
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

unsigned int mtk_rdma_layer_nr(struct device *dev)
{
	return 1;
}

void mtk_rdma_layer_config(struct device *dev, unsigned int idx,
			   struct mtk_plane_state *state,
			   struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_rdma *rdma = dev_get_drvdata(dev);
	struct mtk_plane_pending_state *pending = &state->pending;
	unsigned int addr = pending->addr;
	unsigned int pitch = pending->pitch & 0xffff;
	unsigned int fmt = pending->format;
	unsigned int con;

	con = rdma_fmt_convert(rdma, fmt);
	mtk_ddp_write_relaxed(cmdq_pkt, con, &rdma->cmdq_reg, rdma->regs, DISP_RDMA_MEM_CON);

	if (fmt == DRM_FORMAT_UYVY || fmt == DRM_FORMAT_YUYV) {
		mtk_ddp_write_mask(cmdq_pkt, RDMA_MATRIX_ENABLE, &rdma->cmdq_reg, rdma->regs,
				   DISP_REG_RDMA_SIZE_CON_0,
				   RDMA_MATRIX_ENABLE);
		mtk_ddp_write_mask(cmdq_pkt, RDMA_MATRIX_INT_MTX_BT601_to_RGB,
				   &rdma->cmdq_reg, rdma->regs, DISP_REG_RDMA_SIZE_CON_0,
				   RDMA_MATRIX_INT_MTX_SEL);
	} else {
		mtk_ddp_write_mask(cmdq_pkt, 0, &rdma->cmdq_reg, rdma->regs,
				   DISP_REG_RDMA_SIZE_CON_0,
				   RDMA_MATRIX_ENABLE);
	}
	mtk_ddp_write_relaxed(cmdq_pkt, addr, &rdma->cmdq_reg, rdma->regs,
			      DISP_RDMA_MEM_START_ADDR);
	mtk_ddp_write_relaxed(cmdq_pkt, pitch, &rdma->cmdq_reg, rdma->regs,
			      DISP_RDMA_MEM_SRC_PITCH);
	mtk_ddp_write(cmdq_pkt, RDMA_MEM_GMC, &rdma->cmdq_reg, rdma->regs,
		      DISP_RDMA_MEM_GMC_SETTING_0);
	mtk_ddp_write_mask(cmdq_pkt, RDMA_MODE_MEMORY, &rdma->cmdq_reg, rdma->regs,
			   DISP_REG_RDMA_GLOBAL_CON, RDMA_MODE_MEMORY);

}

static int mtk_disp_rdma_bind(struct device *dev, struct device *master,
			      void *data)
{
	return 0;

}

static void mtk_disp_rdma_unbind(struct device *dev, struct device *master,
				 void *data)
{
}

static const struct component_ops mtk_disp_rdma_component_ops = {
	.bind	= mtk_disp_rdma_bind,
	.unbind = mtk_disp_rdma_unbind,
};

static int mtk_disp_rdma_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_disp_rdma *priv;
	struct resource *res;
	int irq;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "failed to get rdma clk\n");
		return PTR_ERR(priv->clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->regs)) {
		dev_err(dev, "failed to ioremap rdma\n");
		return PTR_ERR(priv->regs);
	}
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	ret = cmdq_dev_get_client_reg(dev, &priv->cmdq_reg, 0);
	if (ret)
		dev_dbg(dev, "get mediatek,gce-client-reg fail!\n");
#endif

	if (of_find_property(dev->of_node, "mediatek,rdma-fifo-size", &ret)) {
		ret = of_property_read_u32(dev->of_node,
					   "mediatek,rdma-fifo-size",
					   &priv->fifo_size);
		if (ret) {
			dev_err(dev, "Failed to get rdma fifo size\n");
			return ret;
		}
	}

	/* Disable and clear pending interrupts */
	writel(0x0, priv->regs + DISP_REG_RDMA_INT_ENABLE);
	writel(0x0, priv->regs + DISP_REG_RDMA_INT_STATUS);

	ret = devm_request_irq(dev, irq, mtk_disp_rdma_irq_handler,
			       IRQF_TRIGGER_NONE, dev_name(dev), priv);
	if (ret < 0) {
		dev_err(dev, "Failed to request irq %d: %d\n", irq, ret);
		return ret;
	}

	priv->data = of_device_get_match_data(dev);

	platform_set_drvdata(pdev, priv);

	pm_runtime_enable(dev);

	ret = component_add(dev, &mtk_disp_rdma_component_ops);
	if (ret) {
		pm_runtime_disable(dev);
		dev_err(dev, "Failed to add component: %d\n", ret);
	}

	return ret;
}

static int mtk_disp_rdma_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_disp_rdma_component_ops);

	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct mtk_disp_rdma_data mt2701_rdma_driver_data = {
	.fifo_size = SZ_4K,
};

static const struct mtk_disp_rdma_data mt8173_rdma_driver_data = {
	.fifo_size = SZ_8K,
};

static const struct mtk_disp_rdma_data mt8183_rdma_driver_data = {
	.fifo_size = 5 * SZ_1K,
};

static const struct mtk_disp_rdma_data mt8192_rdma_driver_data = {
	.fifo_size = 5 * SZ_1K,
};

static const struct of_device_id mtk_disp_rdma_driver_dt_match[] = {
	{ .compatible = "mediatek,mt2701-disp-rdma",
	  .data = &mt2701_rdma_driver_data},
	{ .compatible = "mediatek,mt8173-disp-rdma",
	  .data = &mt8173_rdma_driver_data},
	{ .compatible = "mediatek,mt8183-disp-rdma",
	  .data = &mt8183_rdma_driver_data},
	{ .compatible = "mediatek,mt8192-disp-rdma",
	  .data = &mt8192_rdma_driver_data},
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
