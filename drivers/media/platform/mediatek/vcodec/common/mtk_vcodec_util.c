// SPDX-License-Identifier: GPL-2.0
/*
* Copyright (c) 2016 MediaTek Inc.
* Author: PC Chen <pc.chen@mediatek.com>
*	Tiffany Lin <tiffany.lin@mediatek.com>
*/

#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include "../decoder/mtk_vcodec_dec_drv.h"
#include "../encoder/mtk_vcodec_enc_drv.h"
#include "../decoder/mtk_vcodec_dec_hw.h"

#if defined(CONFIG_DEBUG_FS)
int mtk_vcodec_dbg;
EXPORT_SYMBOL(mtk_vcodec_dbg);

int mtk_v4l2_dbg_level;
EXPORT_SYMBOL(mtk_v4l2_dbg_level);
#endif

void __iomem *mtk_vcodec_get_reg_addr(void __iomem **reg_base, unsigned int reg_idx)
{
	if (reg_idx >= NUM_MAX_VCODEC_REG_BASE) {
		pr_err(MTK_DBG_V4L2_STR "Invalid arguments, reg_idx=%d", reg_idx);
		return NULL;
	}
	return reg_base[reg_idx];
}
EXPORT_SYMBOL(mtk_vcodec_get_reg_addr);

int mtk_vcodec_write_vdecsys(struct mtk_vcodec_dec_ctx *ctx, unsigned int reg,
			     unsigned int val)
{
	struct mtk_vcodec_dec_dev *dev = ctx->dev;

	if (dev->vdecsys_regmap)
		return regmap_write(dev->vdecsys_regmap, reg, val);

	writel(val, dev->reg_base[VDEC_SYS] + reg);

	return 0;
}
EXPORT_SYMBOL(mtk_vcodec_write_vdecsys);

int mtk_vcodec_mem_alloc(void *priv, struct mtk_vcodec_mem *mem)
{
	unsigned long size = mem->size;
	struct mtk_vcodec_dec_ctx *ctx = priv;
	struct device *dev = &ctx->dev->plat_dev->dev;

	mem->va = dma_alloc_coherent(dev, size, &mem->dma_addr, GFP_KERNEL);
	if (!mem->va) {
		mtk_v4l2_vdec_err(ctx, "%s dma_alloc size=%ld failed!", dev_name(dev), size);
		return -ENOMEM;
	}

	mtk_v4l2_vdec_dbg(3, ctx, "[%d]  - va      = %p", ctx->id, mem->va);
	mtk_v4l2_vdec_dbg(3, ctx, "[%d]  - dma     = 0x%lx", ctx->id,
			  (unsigned long)mem->dma_addr);
	mtk_v4l2_vdec_dbg(3, ctx, "[%d]    size = 0x%lx", ctx->id, size);

	return 0;
}
EXPORT_SYMBOL(mtk_vcodec_mem_alloc);

void mtk_vcodec_mem_free(void *priv, struct mtk_vcodec_mem *mem)
{
	unsigned long size = mem->size;
	struct mtk_vcodec_dec_ctx *ctx = priv;
	struct device *dev = &ctx->dev->plat_dev->dev;

	if (!mem->va) {
		mtk_v4l2_vdec_err(ctx, "%s dma_free size=%ld failed!", dev_name(dev), size);
		return;
	}

	mtk_v4l2_vdec_dbg(3, ctx, "[%d]  - va      = %p", ctx->id, mem->va);
	mtk_v4l2_vdec_dbg(3, ctx, "[%d]  - dma     = 0x%lx", ctx->id,
			  (unsigned long)mem->dma_addr);
	mtk_v4l2_vdec_dbg(3, ctx, "[%d]    size = 0x%lx", ctx->id, size);

	dma_free_coherent(dev, size, mem->va, mem->dma_addr);
	mem->va = NULL;
	mem->dma_addr = 0;
	mem->size = 0;
}
EXPORT_SYMBOL(mtk_vcodec_mem_free);

void *mtk_vcodec_get_hw_dev(struct mtk_vcodec_dec_dev *dev, int hw_idx)
{
	if (hw_idx >= MTK_VDEC_HW_MAX || hw_idx < 0 || !dev->subdev_dev[hw_idx]) {
		dev_err(&dev->plat_dev->dev, "hw idx is out of range:%d", hw_idx);
		return NULL;
	}

	return dev->subdev_dev[hw_idx];
}
EXPORT_SYMBOL(mtk_vcodec_get_hw_dev);

void mtk_vcodec_set_curr_ctx(struct mtk_vcodec_dec_dev *vdec_dev,
			     struct mtk_vcodec_dec_ctx *ctx, int hw_idx)
{
	unsigned long flags;
	struct mtk_vdec_hw_dev *subdev_dev;

	spin_lock_irqsave(&vdec_dev->irqlock, flags);
	if (vdec_dev->vdec_pdata->is_subdev_supported) {
		subdev_dev = mtk_vcodec_get_hw_dev(vdec_dev, hw_idx);
		if (!subdev_dev) {
			dev_err(&vdec_dev->plat_dev->dev, "Failed to get hw dev");
			spin_unlock_irqrestore(&vdec_dev->irqlock, flags);
			return;
		}
		subdev_dev->curr_ctx = ctx;
	} else {
		vdec_dev->curr_ctx = ctx;
	}
	spin_unlock_irqrestore(&vdec_dev->irqlock, flags);
}
EXPORT_SYMBOL(mtk_vcodec_set_curr_ctx);

struct mtk_vcodec_dec_ctx *mtk_vcodec_get_curr_ctx(struct mtk_vcodec_dec_dev *vdec_dev,
						   unsigned int hw_idx)
{
	unsigned long flags;
	struct mtk_vcodec_dec_ctx *ctx;
	struct mtk_vdec_hw_dev *subdev_dev;

	spin_lock_irqsave(&vdec_dev->irqlock, flags);
	if (vdec_dev->vdec_pdata->is_subdev_supported) {
		subdev_dev = mtk_vcodec_get_hw_dev(vdec_dev, hw_idx);
		if (!subdev_dev) {
			dev_err(&vdec_dev->plat_dev->dev, "Failed to get hw dev");
			spin_unlock_irqrestore(&vdec_dev->irqlock, flags);
			return NULL;
		}
		ctx = subdev_dev->curr_ctx;
	} else {
		ctx = vdec_dev->curr_ctx;
	}
	spin_unlock_irqrestore(&vdec_dev->irqlock, flags);
	return ctx;
}
EXPORT_SYMBOL(mtk_vcodec_get_curr_ctx);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Mediatek video codec driver");
