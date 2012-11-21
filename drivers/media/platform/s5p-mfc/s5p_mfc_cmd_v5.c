/*
 * linux/drivers/media/platform/s5p-mfc/s5p_mfc_cmd_v5.c
 *
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "regs-mfc.h"
#include "s5p_mfc_cmd.h"
#include "s5p_mfc_common.h"
#include "s5p_mfc_debug.h"

/* This function is used to send a command to the MFC */
int s5p_mfc_cmd_host2risc_v5(struct s5p_mfc_dev *dev, int cmd,
				struct s5p_mfc_cmd_args *args)
{
	int cur_cmd;
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(MFC_BW_TIMEOUT);
	/* wait until host to risc command register becomes 'H2R_CMD_EMPTY' */
	do {
		if (time_after(jiffies, timeout)) {
			mfc_err("Timeout while waiting for hardware\n");
			return -EIO;
		}
		cur_cmd = mfc_read(dev, S5P_FIMV_HOST2RISC_CMD);
	} while (cur_cmd != S5P_FIMV_H2R_CMD_EMPTY);
	mfc_write(dev, args->arg[0], S5P_FIMV_HOST2RISC_ARG1);
	mfc_write(dev, args->arg[1], S5P_FIMV_HOST2RISC_ARG2);
	mfc_write(dev, args->arg[2], S5P_FIMV_HOST2RISC_ARG3);
	mfc_write(dev, args->arg[3], S5P_FIMV_HOST2RISC_ARG4);
	/* Issue the command */
	mfc_write(dev, cmd, S5P_FIMV_HOST2RISC_CMD);
	return 0;
}

/* Initialize the MFC */
int s5p_mfc_sys_init_cmd_v5(struct s5p_mfc_dev *dev)
{
	struct s5p_mfc_cmd_args h2r_args;

	memset(&h2r_args, 0, sizeof(struct s5p_mfc_cmd_args));
	h2r_args.arg[0] = dev->fw_size;
	return s5p_mfc_cmd_host2risc_v5(dev, S5P_FIMV_H2R_CMD_SYS_INIT,
			&h2r_args);
}

/* Suspend the MFC hardware */
int s5p_mfc_sleep_cmd_v5(struct s5p_mfc_dev *dev)
{
	struct s5p_mfc_cmd_args h2r_args;

	memset(&h2r_args, 0, sizeof(struct s5p_mfc_cmd_args));
	return s5p_mfc_cmd_host2risc_v5(dev, S5P_FIMV_H2R_CMD_SLEEP, &h2r_args);
}

/* Wake up the MFC hardware */
int s5p_mfc_wakeup_cmd_v5(struct s5p_mfc_dev *dev)
{
	struct s5p_mfc_cmd_args h2r_args;

	memset(&h2r_args, 0, sizeof(struct s5p_mfc_cmd_args));
	return s5p_mfc_cmd_host2risc_v5(dev, S5P_FIMV_H2R_CMD_WAKEUP,
			&h2r_args);
}


int s5p_mfc_open_inst_cmd_v5(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_cmd_args h2r_args;
	int ret;

	/* Preparing decoding - getting instance number */
	mfc_debug(2, "Getting instance number (codec: %d)\n", ctx->codec_mode);
	dev->curr_ctx = ctx->num;
	memset(&h2r_args, 0, sizeof(struct s5p_mfc_cmd_args));
	switch (ctx->codec_mode) {
	case S5P_MFC_CODEC_H264_DEC:
		h2r_args.arg[0] = S5P_FIMV_CODEC_H264_DEC;
		break;
	case S5P_MFC_CODEC_VC1_DEC:
		h2r_args.arg[0] = S5P_FIMV_CODEC_VC1_DEC;
		break;
	case S5P_MFC_CODEC_MPEG4_DEC:
		h2r_args.arg[0] = S5P_FIMV_CODEC_MPEG4_DEC;
		break;
	case S5P_MFC_CODEC_MPEG2_DEC:
		h2r_args.arg[0] = S5P_FIMV_CODEC_MPEG2_DEC;
		break;
	case S5P_MFC_CODEC_H263_DEC:
		h2r_args.arg[0] = S5P_FIMV_CODEC_H263_DEC;
		break;
	case S5P_MFC_CODEC_VC1RCV_DEC:
		h2r_args.arg[0] = S5P_FIMV_CODEC_VC1RCV_DEC;
		break;
	case S5P_MFC_CODEC_H264_ENC:
		h2r_args.arg[0] = S5P_FIMV_CODEC_H264_ENC;
		break;
	case S5P_MFC_CODEC_MPEG4_ENC:
		h2r_args.arg[0] = S5P_FIMV_CODEC_MPEG4_ENC;
		break;
	case S5P_MFC_CODEC_H263_ENC:
		h2r_args.arg[0] = S5P_FIMV_CODEC_H263_ENC;
		break;
	default:
		h2r_args.arg[0] = S5P_FIMV_CODEC_NONE;
	};
	h2r_args.arg[1] = 0; /* no crc & no pixelcache */
	h2r_args.arg[2] = ctx->ctx.ofs;
	h2r_args.arg[3] = ctx->ctx.size;
	ret = s5p_mfc_cmd_host2risc_v5(dev, S5P_FIMV_H2R_CMD_OPEN_INSTANCE,
								&h2r_args);
	if (ret) {
		mfc_err("Failed to create a new instance\n");
		ctx->state = MFCINST_ERROR;
	}
	return ret;
}

int s5p_mfc_close_inst_cmd_v5(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_cmd_args h2r_args;
	int ret;

	if (ctx->state == MFCINST_FREE) {
		mfc_err("Instance already returned\n");
		ctx->state = MFCINST_ERROR;
		return -EINVAL;
	}
	/* Closing decoding instance  */
	mfc_debug(2, "Returning instance number %d\n", ctx->inst_no);
	dev->curr_ctx = ctx->num;
	memset(&h2r_args, 0, sizeof(struct s5p_mfc_cmd_args));
	h2r_args.arg[0] = ctx->inst_no;
	ret = s5p_mfc_cmd_host2risc_v5(dev, S5P_FIMV_H2R_CMD_CLOSE_INSTANCE,
								&h2r_args);
	if (ret) {
		mfc_err("Failed to return an instance\n");
		ctx->state = MFCINST_ERROR;
		return -EINVAL;
	}
	return 0;
}

/* Initialize cmd function pointers for MFC v5 */
static struct s5p_mfc_hw_cmds s5p_mfc_cmds_v5 = {
	.cmd_host2risc = s5p_mfc_cmd_host2risc_v5,
	.sys_init_cmd = s5p_mfc_sys_init_cmd_v5,
	.sleep_cmd = s5p_mfc_sleep_cmd_v5,
	.wakeup_cmd = s5p_mfc_wakeup_cmd_v5,
	.open_inst_cmd = s5p_mfc_open_inst_cmd_v5,
	.close_inst_cmd = s5p_mfc_close_inst_cmd_v5,
};

struct s5p_mfc_hw_cmds *s5p_mfc_init_hw_cmds_v5(void)
{
	return &s5p_mfc_cmds_v5;
}
