/*
 * linux/drivers/media/video/s5p-mfc/s5p_mfc_inst.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "s5p_mfc_common.h"
#include "s5p_mfc_cmd.h"
#include "s5p_mfc_debug.h"
#include "s5p_mfc_intr.h"

int s5p_mfc_open_inst(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	int ret;

	/* Preparing decoding - getting instance number */
	mfc_debug(2, "Getting instance number\n");
	dev->curr_ctx = ctx->num;
	s5p_mfc_clean_ctx_int_flags(ctx);
	ret = s5p_mfc_open_inst_cmd(ctx);
	if (ret) {
		mfc_err("Failed to create a new instance.\n");
		ctx->state = MFCINST_ERROR;
	}
	return ret;
}

int s5p_mfc_close_inst(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	int ret;

	/* Closing decoding instance  */
	mfc_debug(2, "Returning instance number\n");
	dev->curr_ctx = ctx->num;
	s5p_mfc_clean_ctx_int_flags(ctx);
	ret = s5p_mfc_close_inst_cmd(ctx);
	if (ret) {
		mfc_err("Failed to return an instance.\n");
		ctx->state = MFCINST_ERROR;
		return ret;
	}
	return ret;
}

