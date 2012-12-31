/*
 * linux/drivers/media/video/s5p-mfc/s5p_mfc_cmd_v5.c
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
	int cur_cmd;
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(MFC_BW_TIMEOUT);

	/* wait until host to risc command register becomes 'H2R_CMD_EMPTY' */
	do {
		if (time_after(jiffies, timeout)) {
			mfc_err("Timeout while waiting for hardware.\n");
			return -EIO;
		}

		cur_cmd = s5p_mfc_read_reg(S5P_FIMV_HOST2RISC_CMD);
	} while (cur_cmd != S5P_FIMV_H2R_CMD_EMPTY);

	s5p_mfc_write_reg(args->arg[0], S5P_FIMV_HOST2RISC_ARG1);
	s5p_mfc_write_reg(args->arg[1], S5P_FIMV_HOST2RISC_ARG2);
	s5p_mfc_write_reg(args->arg[2], S5P_FIMV_HOST2RISC_ARG3);
	s5p_mfc_write_reg(args->arg[3], S5P_FIMV_HOST2RISC_ARG4);

	/* Issue the command */
	s5p_mfc_write_reg(cmd, S5P_FIMV_HOST2RISC_CMD);

	return 0;
}

int s5p_mfc_sys_init_cmd(struct s5p_mfc_dev *dev)
{
	struct s5p_mfc_cmd_args h2r_args;
	struct s5p_mfc_buf_size *buf_size = dev->variant->buf_size;
	int ret;

	mfc_debug_enter();

	memset(&h2r_args, 0, sizeof(struct s5p_mfc_cmd_args));
	h2r_args.arg[0] = buf_size->firmware_code;

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
	unsigned int crc = 0;
	struct s5p_mfc_dec *dec = ctx->dec_priv;
	int ret;

	mfc_debug_enter();

	mfc_debug(2, "Requested codec mode: %d\n", ctx->codec_mode);

	if (ctx->type == MFCINST_DECODER)
		crc = dec->crc_enable;

	memset(&h2r_args, 0, sizeof(struct s5p_mfc_cmd_args));
	h2r_args.arg[0] = ctx->codec_mode;
	h2r_args.arg[1] = crc << 31; /* no pixelcache */
	h2r_args.arg[2] = ctx->ctx.ofs;
	h2r_args.arg[3] = ctx->ctx_buf_size;

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
		memset(&h2r_args, 0, sizeof(struct s5p_mfc_cmd_args));
		h2r_args.arg[0] = ctx->inst_no;

		ret = s5p_mfc_cmd_host2risc(S5P_FIMV_H2R_CMD_CLOSE_INSTANCE,
					    &h2r_args);
	} else {
		ret = -EINVAL;
	}

	mfc_debug_leave();

	return ret;
}
