// SPDX-License-Identifier: GPL-2.0
/*
* Copyright (c) 2016 MediaTek Inc.
* Author: Tiffany Lin <tiffany.lin@mediatek.com>
*/

#include <linux/errno.h>
#include <linux/wait.h>

#include "../decoder/mtk_vcodec_dec_drv.h"
#include "../encoder/mtk_vcodec_enc_drv.h"
#include "mtk_vcodec_intr.h"

int mtk_vcodec_wait_for_done_ctx(void *priv, int command, unsigned int timeout_ms,
				 unsigned int hw_id)
{
	int instance_type = *((int *)priv);
	long timeout_jiff, ret;
	int ctx_id, ctx_type, status = 0;
	int *ctx_int_cond, *ctx_int_type;
	wait_queue_head_t *ctx_queue;
	struct platform_device *pdev;

	if (instance_type == DECODER) {
		struct mtk_vcodec_dec_ctx *ctx;

		ctx = priv;
		ctx_id = ctx->id;
		ctx_type = ctx->type;
		ctx_int_cond = ctx->int_cond;
		ctx_int_type = ctx->int_type;
		ctx_queue = ctx->queue;
		pdev = ctx->dev->plat_dev;
	} else {
		struct mtk_vcodec_enc_ctx *ctx;

		ctx = priv;
		ctx_id = ctx->id;
		ctx_type = ctx->type;
		ctx_int_cond = ctx->int_cond;
		ctx_int_type = ctx->int_type;
		ctx_queue = ctx->queue;
		pdev = ctx->dev->plat_dev;
	}

	timeout_jiff = msecs_to_jiffies(timeout_ms);
	ret = wait_event_interruptible_timeout(ctx_queue[hw_id],
					       ctx_int_cond[hw_id],
					       timeout_jiff);

	if (!ret) {
		status = -1;	/* timeout */
		dev_err(&pdev->dev, "[%d] cmd=%d, type=%d, dec timeout=%ums (%d %d)",
			ctx_id, command, ctx_type, timeout_ms,
			ctx_int_cond[hw_id], ctx_int_type[hw_id]);
	} else if (-ERESTARTSYS == ret) {
		status = -1;
		dev_err(&pdev->dev, "[%d] cmd=%d, type=%d, dec inter fail (%d %d)",
			ctx_id, command, ctx_type,
			ctx_int_cond[hw_id], ctx_int_type[hw_id]);
	}

	ctx_int_cond[hw_id] = 0;
	ctx_int_type[hw_id] = 0;

	return status;
}
EXPORT_SYMBOL(mtk_vcodec_wait_for_done_ctx);
