// SPDX-License-Identifier: GPL-2.0
/*
* Copyright (c) 2016 MediaTek Inc.
* Author: Tiffany Lin <tiffany.lin@mediatek.com>
*/

#include <linux/errno.h>
#include <linux/wait.h>

#include "mtk_vcodec_drv.h"
#include "mtk_vcodec_intr.h"
#include "mtk_vcodec_util.h"

int mtk_vcodec_wait_for_done_ctx(struct mtk_vcodec_ctx *ctx,
				 int command, unsigned int timeout_ms,
				 unsigned int hw_id)
{
	long timeout_jiff, ret;
	int status = 0;

	timeout_jiff = msecs_to_jiffies(timeout_ms);
	ret = wait_event_interruptible_timeout(ctx->queue[hw_id],
					       ctx->int_cond[hw_id],
					       timeout_jiff);

	if (!ret) {
		status = -1;	/* timeout */
		mtk_v4l2_err("[%d] cmd=%d, type=%d, dec timeout=%ums (%d %d)",
			     ctx->id, command, ctx->type, timeout_ms,
			     ctx->int_cond[hw_id], ctx->int_type[hw_id]);
	} else if (-ERESTARTSYS == ret) {
		status = -1;
		mtk_v4l2_err("[%d] cmd=%d, type=%d, dec inter fail (%d %d)",
			     ctx->id, command, ctx->type,
			     ctx->int_cond[hw_id], ctx->int_type[hw_id]);
	}

	ctx->int_cond[hw_id] = 0;
	ctx->int_type[hw_id] = 0;

	return status;
}
EXPORT_SYMBOL(mtk_vcodec_wait_for_done_ctx);
