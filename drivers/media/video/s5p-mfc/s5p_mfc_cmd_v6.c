/*
 * linux/drivers/media/video/s5p-mfc/s5p_mfc_cmd_v6.c
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

#include "s5p_mfc_debug.h"
#include "s5p_mfc_reg.h"
#include "s5p_mfc_cmd.h"
#include "s5p_mfc_mem.h"

int s5p_mfc_cmd_host2risc(int cmd, struct s5p_mfc_cmd_args *args)
{
	mfc_debug(2, "Issue the command: %d\n", cmd);

	/* Reset RISC2HOST command */
	s5p_mfc_write_reg(0x0, S5P_FIMV_RISC2HOST_CMD);

	/* Issue the command */
	s5p_mfc_write_reg(cmd, S5P_FIMV_HOST2RISC_CMD);
	s5p_mfc_write_reg(0x1, S5P_FIMV_HOST2RISC_INT);

	return 0;
}

int s5p_mfc_sys_init_cmd(struct s5p_mfc_dev *dev)
{
	struct s5p_mfc_cmd_args h2r_args;
	struct s5p_mfc_buf_size_v6 *buf_size = dev->variant->buf_size->buf;
	int ret;

	mfc_debug_enter();

	s5p_mfc_alloc_dev_context_buffer(dev);

	s5p_mfc_write_reg(dev->ctx_buf.ofs, S5P_FIMV_CONTEXT_MEM_ADDR);
	s5p_mfc_write_reg(buf_size->dev_ctx, S5P_FIMV_CONTEXT_MEM_SIZE);

	ret = s5p_mfc_cmd_host2risc(S5P_FIMV_H2R_CMD_SYS_INIT, &h2r_args);

	mfc_debug_leave();

	return ret;
}

int s5p_mfc_sleep_cmd(struct s5p_mfc_dev *dev)
{
	struct s5p_mfc_cmd_args h2r_args;
	int ret;

	mfc_debug_enter();

	memset(&h2r_args, 0, sizeof(struct s5p_mfc_cmd_args));

	ret = s5p_mfc_cmd_host2risc(S5P_FIMV_H2R_CMD_SLEEP, &h2r_args);

	mfc_debug_leave();

	return ret;
}

int s5p_mfc_wakeup_cmd(struct s5p_mfc_dev *dev)
{
	struct s5p_mfc_cmd_args h2r_args;
	int ret;

	mfc_debug_enter();

	memset(&h2r_args, 0, sizeof(struct s5p_mfc_cmd_args));

	ret = s5p_mfc_cmd_host2risc(S5P_FIMV_H2R_CMD_WAKEUP, &h2r_args);

	mfc_debug_leave();

	return ret;
}

/* Open a new instance and get its number */
int s5p_mfc_open_inst_cmd(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_cmd_args h2r_args;
	struct s5p_mfc_dec *dec = ctx->dec_priv;
	int ret;

	mfc_debug_enter();

	mfc_debug(2, "Requested codec mode: %d\n", ctx->codec_mode);

	s5p_mfc_write_reg(ctx->codec_mode, S5P_FIMV_CODEC_TYPE);
	s5p_mfc_write_reg(ctx->ctx.ofs, S5P_FIMV_CONTEXT_MEM_ADDR);
	s5p_mfc_write_reg(ctx->ctx_buf_size, S5P_FIMV_CONTEXT_MEM_SIZE);
	if (ctx->type == MFCINST_DECODER)
		s5p_mfc_write_reg(dec->crc_enable, S5P_FIMV_D_CRC_CTRL);

	ret = s5p_mfc_cmd_host2risc(S5P_FIMV_H2R_CMD_OPEN_INSTANCE, &h2r_args);

	mfc_debug_leave();

	return ret;
}

/* Close instance */
int s5p_mfc_close_inst_cmd(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_cmd_args h2r_args;
	int ret = 0;

	mfc_debug_enter();

	if (ctx->state != MFCINST_FREE) {
		s5p_mfc_write_reg(ctx->inst_no, S5P_FIMV_INSTANCE_ID);

		ret = s5p_mfc_cmd_host2risc(S5P_FIMV_H2R_CMD_CLOSE_INSTANCE,
					    &h2r_args);
	} else {
		ret = -EINVAL;
	}

	mfc_debug_leave();

	return ret;
}
