// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: PC Chen <pc.chen@mediatek.com>
 *         Tiffany Lin <tiffany.lin@mediatek.com>
 */

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "vdec_drv_if.h"
#include "mtk_vcodec_dec.h"
#include "vdec_drv_base.h"
#include "mtk_vcodec_dec_pm.h"

int vdec_if_init(struct mtk_vcodec_ctx *ctx, unsigned int fourcc)
{
	int ret = 0;

	switch (fourcc) {
	case V4L2_PIX_FMT_H264_SLICE:
		ctx->dec_if = &vdec_h264_slice_if;
		break;
	case V4L2_PIX_FMT_H264:
		ctx->dec_if = &vdec_h264_if;
		ctx->hw_id = MTK_VDEC_CORE;
		break;
	case V4L2_PIX_FMT_VP8:
		ctx->dec_if = &vdec_vp8_if;
		ctx->hw_id = MTK_VDEC_CORE;
		break;
	case V4L2_PIX_FMT_VP9:
		ctx->dec_if = &vdec_vp9_if;
		ctx->hw_id = MTK_VDEC_CORE;
		break;
	default:
		return -EINVAL;
	}

	mtk_vdec_lock(ctx);
	mtk_vcodec_dec_clock_on(ctx->dev, ctx->hw_id);
	ret = ctx->dec_if->init(ctx);
	mtk_vcodec_dec_clock_off(ctx->dev, ctx->hw_id);
	mtk_vdec_unlock(ctx);

	return ret;
}

int vdec_if_decode(struct mtk_vcodec_ctx *ctx, struct mtk_vcodec_mem *bs,
		   struct vdec_fb *fb, bool *res_chg)
{
	int ret = 0;

	if (bs) {
		if ((bs->dma_addr & 63) != 0) {
			mtk_v4l2_err("bs dma_addr should 64 byte align");
			return -EINVAL;
		}
	}

	if (fb) {
		if (((fb->base_y.dma_addr & 511) != 0) ||
		    ((fb->base_c.dma_addr & 511) != 0)) {
			mtk_v4l2_err("frame buffer dma_addr should 512 byte align");
			return -EINVAL;
		}
	}

	if (!ctx->drv_handle)
		return -EIO;

	mtk_vdec_lock(ctx);

	mtk_vcodec_set_curr_ctx(ctx->dev, ctx, ctx->hw_id);
	mtk_vcodec_dec_clock_on(ctx->dev, ctx->hw_id);
	ret = ctx->dec_if->decode(ctx->drv_handle, bs, fb, res_chg);
	mtk_vcodec_dec_clock_off(ctx->dev, ctx->hw_id);
	mtk_vcodec_set_curr_ctx(ctx->dev, NULL, ctx->hw_id);

	mtk_vdec_unlock(ctx);

	return ret;
}

int vdec_if_get_param(struct mtk_vcodec_ctx *ctx, enum vdec_get_param_type type,
		      void *out)
{
	int ret = 0;

	if (!ctx->drv_handle)
		return -EIO;

	mtk_vdec_lock(ctx);
	ret = ctx->dec_if->get_param(ctx->drv_handle, type, out);
	mtk_vdec_unlock(ctx);

	return ret;
}

void vdec_if_deinit(struct mtk_vcodec_ctx *ctx)
{
	if (!ctx->drv_handle)
		return;

	mtk_vdec_lock(ctx);
	mtk_vcodec_dec_clock_on(ctx->dev, ctx->hw_id);
	ctx->dec_if->deinit(ctx->drv_handle);
	mtk_vcodec_dec_clock_off(ctx->dev, ctx->hw_id);
	mtk_vdec_unlock(ctx);

	ctx->drv_handle = NULL;
}
