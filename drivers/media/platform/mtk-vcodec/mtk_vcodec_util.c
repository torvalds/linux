// SPDX-License-Identifier: GPL-2.0-only
/*
* Copyright (c) 2016 MediaTek Inc.
* Author: PC Chen <pc.chen@mediatek.com>
*	Tiffany Lin <tiffany.lin@mediatek.com>
*/

#include <linux/module.h>

#include "mtk_vcodec_drv.h"
#include "mtk_vcodec_util.h"
#include "mtk_vpu.h"

/* For encoder, this will enable logs in venc/*/
bool mtk_vcodec_dbg;
EXPORT_SYMBOL(mtk_vcodec_dbg);

/* The log level of v4l2 encoder or decoder driver.
 * That is, files under mtk-vcodec/.
 */
int mtk_v4l2_dbg_level;
EXPORT_SYMBOL(mtk_v4l2_dbg_level);

void __iomem *mtk_vcodec_get_reg_addr(struct mtk_vcodec_ctx *data,
					unsigned int reg_idx)
{
	struct mtk_vcodec_ctx *ctx = (struct mtk_vcodec_ctx *)data;

	if (!data || reg_idx >= NUM_MAX_VCODEC_REG_BASE) {
		mtk_v4l2_err("Invalid arguments, reg_idx=%d", reg_idx);
		return NULL;
	}
	return ctx->dev->reg_base[reg_idx];
}
EXPORT_SYMBOL(mtk_vcodec_get_reg_addr);

int mtk_vcodec_mem_alloc(struct mtk_vcodec_ctx *data,
			struct mtk_vcodec_mem *mem)
{
	unsigned long size = mem->size;
	struct mtk_vcodec_ctx *ctx = (struct mtk_vcodec_ctx *)data;
	struct device *dev = &ctx->dev->plat_dev->dev;

	mem->va = dma_alloc_coherent(dev, size, &mem->dma_addr, GFP_KERNEL);
	if (!mem->va) {
		mtk_v4l2_err("%s dma_alloc size=%ld failed!", dev_name(dev),
			     size);
		return -ENOMEM;
	}

	mtk_v4l2_debug(3, "[%d]  - va      = %p", ctx->id, mem->va);
	mtk_v4l2_debug(3, "[%d]  - dma     = 0x%lx", ctx->id,
		       (unsigned long)mem->dma_addr);
	mtk_v4l2_debug(3, "[%d]    size = 0x%lx", ctx->id, size);

	return 0;
}
EXPORT_SYMBOL(mtk_vcodec_mem_alloc);

void mtk_vcodec_mem_free(struct mtk_vcodec_ctx *data,
			struct mtk_vcodec_mem *mem)
{
	unsigned long size = mem->size;
	struct mtk_vcodec_ctx *ctx = (struct mtk_vcodec_ctx *)data;
	struct device *dev = &ctx->dev->plat_dev->dev;

	if (!mem->va) {
		mtk_v4l2_err("%s dma_free size=%ld failed!", dev_name(dev),
			     size);
		return;
	}

	mtk_v4l2_debug(3, "[%d]  - va      = %p", ctx->id, mem->va);
	mtk_v4l2_debug(3, "[%d]  - dma     = 0x%lx", ctx->id,
		       (unsigned long)mem->dma_addr);
	mtk_v4l2_debug(3, "[%d]    size = 0x%lx", ctx->id, size);

	dma_free_coherent(dev, size, mem->va, mem->dma_addr);
	mem->va = NULL;
	mem->dma_addr = 0;
	mem->size = 0;
}
EXPORT_SYMBOL(mtk_vcodec_mem_free);

void mtk_vcodec_set_curr_ctx(struct mtk_vcodec_dev *dev,
	struct mtk_vcodec_ctx *ctx)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->irqlock, flags);
	dev->curr_ctx = ctx;
	spin_unlock_irqrestore(&dev->irqlock, flags);
}
EXPORT_SYMBOL(mtk_vcodec_set_curr_ctx);

struct mtk_vcodec_ctx *mtk_vcodec_get_curr_ctx(struct mtk_vcodec_dev *dev)
{
	unsigned long flags;
	struct mtk_vcodec_ctx *ctx;

	spin_lock_irqsave(&dev->irqlock, flags);
	ctx = dev->curr_ctx;
	spin_unlock_irqrestore(&dev->irqlock, flags);
	return ctx;
}
EXPORT_SYMBOL(mtk_vcodec_get_curr_ctx);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Mediatek video codec driver");
