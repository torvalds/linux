/*
 * linux/drivers/media/platform/s5p-mfc/s5p_mfc_cmd_v6.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
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
#include "s5p_mfc_opr.h"
#include "s5p_mfc_cmd_v6.h"

static int s5p_mfc_cmd_host2risc_v6(struct s5p_mfc_dev *dev, int cmd,
				struct s5p_mfc_cmd_args *args)
{
	mfc_debug(2, "Issue the command: %d\n", cmd);

	/* Reset RISC2HOST command */
	mfc_write(dev, 0x0, S5P_FIMV_RISC2HOST_CMD_V6);

	/* Issue the command */
	mfc_write(dev, cmd, S5P_FIMV_HOST2RISC_CMD_V6);
	mfc_write(dev, 0x1, S5P_FIMV_HOST2RISC_INT_V6);

	return 0;
}

static int s5p_mfc_sys_init_cmd_v6(struct s5p_mfc_dev *dev)
{
	struct s5p_mfc_cmd_args h2r_args;
	struct s5p_mfc_buf_size_v6 *buf_size = dev->variant->buf_size->priv;

	s5p_mfc_hw_call(dev->mfc_ops, alloc_dev_context_buffer, dev);
	mfc_write(dev, dev->ctx_buf.dma, S5P_FIMV_CONTEXT_MEM_ADDR_V6);
	mfc_write(dev, buf_size->dev_ctx, S5P_FIMV_CONTEXT_MEM_SIZE_V6);
	return s5p_mfc_cmd_host2risc_v6(dev, S5P_FIMV_H2R_CMD_SYS_INIT_V6,
					&h2r_args);
}

static int s5p_mfc_sleep_cmd_v6(struct s5p_mfc_dev *dev)
{
	struct s5p_mfc_cmd_args h2r_args;

	memset(&h2r_args, 0, sizeof(struct s5p_mfc_cmd_args));
	return s5p_mfc_cmd_host2risc_v6(dev, S5P_FIMV_H2R_CMD_SLEEP_V6,
			&h2r_args);
}

static int s5p_mfc_wakeup_cmd_v6(struct s5p_mfc_dev *dev)
{
	struct s5p_mfc_cmd_args h2r_args;

	memset(&h2r_args, 0, sizeof(struct s5p_mfc_cmd_args));
	return s5p_mfc_cmd_host2risc_v6(dev, S5P_FIMV_H2R_CMD_WAKEUP_V6,
					&h2r_args);
}

/* Open a new instance and get its number */
static int s5p_mfc_open_inst_cmd_v6(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_cmd_args h2r_args;
	int codec_type;

	mfc_debug(2, "Requested codec mode: %d\n", ctx->codec_mode);
	dev->curr_ctx = ctx->num;
	switch (ctx->codec_mode) {
	case S5P_MFC_CODEC_H264_DEC:
		codec_type = S5P_FIMV_CODEC_H264_DEC_V6;
		break;
	case S5P_MFC_CODEC_H264_MVC_DEC:
		codec_type = S5P_FIMV_CODEC_H264_MVC_DEC_V6;
		break;
	case S5P_MFC_CODEC_VC1_DEC:
		codec_type = S5P_FIMV_CODEC_VC1_DEC_V6;
		break;
	case S5P_MFC_CODEC_MPEG4_DEC:
		codec_type = S5P_FIMV_CODEC_MPEG4_DEC_V6;
		break;
	case S5P_MFC_CODEC_MPEG2_DEC:
		codec_type = S5P_FIMV_CODEC_MPEG2_DEC_V6;
		break;
	case S5P_MFC_CODEC_H263_DEC:
		codec_type = S5P_FIMV_CODEC_H263_DEC_V6;
		break;
	case S5P_MFC_CODEC_VC1RCV_DEC:
		codec_type = S5P_FIMV_CODEC_VC1RCV_DEC_V6;
		break;
	case S5P_MFC_CODEC_VP8_DEC:
		codec_type = S5P_FIMV_CODEC_VP8_DEC_V6;
		break;
	case S5P_MFC_CODEC_H264_ENC:
		codec_type = S5P_FIMV_CODEC_H264_ENC_V6;
		break;
	case S5P_MFC_CODEC_H264_MVC_ENC:
		codec_type = S5P_FIMV_CODEC_H264_MVC_ENC_V6;
		break;
	case S5P_MFC_CODEC_MPEG4_ENC:
		codec_type = S5P_FIMV_CODEC_MPEG4_ENC_V6;
		break;
	case S5P_MFC_CODEC_H263_ENC:
		codec_type = S5P_FIMV_CODEC_H263_ENC_V6;
		break;
	case S5P_MFC_CODEC_VP8_ENC:
		codec_type = S5P_FIMV_CODEC_VP8_ENC_V7;
		break;
	default:
		codec_type = S5P_FIMV_CODEC_NONE_V6;
	}
	mfc_write(dev, codec_type, S5P_FIMV_CODEC_TYPE_V6);
	mfc_write(dev, ctx->ctx.dma, S5P_FIMV_CONTEXT_MEM_ADDR_V6);
	mfc_write(dev, ctx->ctx.size, S5P_FIMV_CONTEXT_MEM_SIZE_V6);
	mfc_write(dev, 0, S5P_FIMV_D_CRC_CTRL_V6); /* no crc */

	return s5p_mfc_cmd_host2risc_v6(dev, S5P_FIMV_H2R_CMD_OPEN_INSTANCE_V6,
					&h2r_args);
}

/* Close instance */
static int s5p_mfc_close_inst_cmd_v6(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_cmd_args h2r_args;
	int ret = 0;

	dev->curr_ctx = ctx->num;
	if (ctx->state != MFCINST_FREE) {
		mfc_write(dev, ctx->inst_no, S5P_FIMV_INSTANCE_ID_V6);
		ret = s5p_mfc_cmd_host2risc_v6(dev,
					S5P_FIMV_H2R_CMD_CLOSE_INSTANCE_V6,
					&h2r_args);
	} else {
		ret = -EINVAL;
	}

	return ret;
}

/* Initialize cmd function pointers for MFC v6 */
static struct s5p_mfc_hw_cmds s5p_mfc_cmds_v6 = {
	.cmd_host2risc = s5p_mfc_cmd_host2risc_v6,
	.sys_init_cmd = s5p_mfc_sys_init_cmd_v6,
	.sleep_cmd = s5p_mfc_sleep_cmd_v6,
	.wakeup_cmd = s5p_mfc_wakeup_cmd_v6,
	.open_inst_cmd = s5p_mfc_open_inst_cmd_v6,
	.close_inst_cmd = s5p_mfc_close_inst_cmd_v6,
};

struct s5p_mfc_hw_cmds *s5p_mfc_init_hw_cmds_v6(void)
{
	return &s5p_mfc_cmds_v6;
}
