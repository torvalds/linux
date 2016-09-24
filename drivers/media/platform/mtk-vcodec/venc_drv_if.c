/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Daniel Hsiao <daniel.hsiao@mediatek.com>
 *	Jungchang Tsao <jungchang.tsao@mediatek.com>
 *	Tiffany Lin <tiffany.lin@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "venc_drv_base.h"
#include "venc_drv_if.h"

#include "mtk_vcodec_enc.h"
#include "mtk_vcodec_enc_pm.h"
#include "mtk_vpu.h"

struct venc_common_if *get_h264_enc_comm_if(void);
struct venc_common_if *get_vp8_enc_comm_if(void);

int venc_if_init(struct mtk_vcodec_ctx *ctx, unsigned int fourcc)
{
	int ret = 0;

	switch (fourcc) {
	case V4L2_PIX_FMT_VP8:
		ctx->enc_if = get_vp8_enc_comm_if();
		break;
	case V4L2_PIX_FMT_H264:
		ctx->enc_if = get_h264_enc_comm_if();
		break;
	default:
		return -EINVAL;
	}

	mtk_venc_lock(ctx);
	mtk_vcodec_enc_clock_on(&ctx->dev->pm);
	ret = ctx->enc_if->init(ctx, (unsigned long *)&ctx->drv_handle);
	mtk_vcodec_enc_clock_off(&ctx->dev->pm);
	mtk_venc_unlock(ctx);

	return ret;
}

int venc_if_set_param(struct mtk_vcodec_ctx *ctx,
		enum venc_set_param_type type, struct venc_enc_param *in)
{
	int ret = 0;

	mtk_venc_lock(ctx);
	mtk_vcodec_enc_clock_on(&ctx->dev->pm);
	ret = ctx->enc_if->set_param(ctx->drv_handle, type, in);
	mtk_vcodec_enc_clock_off(&ctx->dev->pm);
	mtk_venc_unlock(ctx);

	return ret;
}

int venc_if_encode(struct mtk_vcodec_ctx *ctx,
		   enum venc_start_opt opt, struct venc_frm_buf *frm_buf,
		   struct mtk_vcodec_mem *bs_buf,
		   struct venc_done_result *result)
{
	int ret = 0;
	unsigned long flags;

	mtk_venc_lock(ctx);

	spin_lock_irqsave(&ctx->dev->irqlock, flags);
	ctx->dev->curr_ctx = ctx;
	spin_unlock_irqrestore(&ctx->dev->irqlock, flags);

	mtk_vcodec_enc_clock_on(&ctx->dev->pm);
	ret = ctx->enc_if->encode(ctx->drv_handle, opt, frm_buf,
				  bs_buf, result);
	mtk_vcodec_enc_clock_off(&ctx->dev->pm);

	spin_lock_irqsave(&ctx->dev->irqlock, flags);
	ctx->dev->curr_ctx = NULL;
	spin_unlock_irqrestore(&ctx->dev->irqlock, flags);

	mtk_venc_unlock(ctx);
	return ret;
}

int venc_if_deinit(struct mtk_vcodec_ctx *ctx)
{
	int ret = 0;

	if (ctx->drv_handle == 0)
		return 0;

	mtk_venc_lock(ctx);
	mtk_vcodec_enc_clock_on(&ctx->dev->pm);
	ret = ctx->enc_if->deinit(ctx->drv_handle);
	mtk_vcodec_enc_clock_off(&ctx->dev->pm);
	mtk_venc_unlock(ctx);

	ctx->drv_handle = 0;

	return ret;
}
