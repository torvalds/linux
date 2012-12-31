/*
 * linux/drivers/media/video/samsung/mfc5x/mfc_cmd.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Command interface for Samsung MFC (Multi Function Codec - FIMV) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MFC_CMD_H
#define __MFC_CMD_H __FILE__

#include <linux/interrupt.h>

#include "mfc_dev.h"

#define MAX_H2R_ARG		4
#define H2R_CMD_TIMEOUT		100	/* ms */
#define H2R_INT_TIMEOUT		5000	/* ms */
#define CODEC_INT_TIMEOUT	1000	/* ms */

enum mfc_h2r_cmd {
	H2R_NOP		= 0,
	OPEN_CH		= 1,
	CLOSE_CH	= 2,
	SYS_INIT	= 3,
	FLUSH		= 4,
	SLEEP		= 5,
	WAKEUP		= 6,
	CONTINUE_ENC	= 7,
	ABORT_ENC	= 8,
};

enum mfc_codec_cmd {
	SEQ_HEADER		= 1,
	FRAME_START		= 2,
	LAST_SEQ		= 3,
	INIT_BUFFERS		= 4,
	FRAME_START_REALLOC	= 5,
	FRAME_BATCH_START	= 6,
};

enum mfc_r2h_ret {
	R2H_NOP			= 0,
	OPEN_CH_RET		= 1,
	CLOSE_CH_RET		= 2,

	SEQ_DONE_RET		= 4,
	FRAME_DONE_RET		= 5,
	SLICE_DONE_RET		= 6,
	ENC_COMPLETE_RET	= 7,
	SYS_INIT_RET		= 8,
	FW_STATUS_RET		= 9,
	SLEEP_RET		= 10,
	WAKEUP_RET		= 11,
	FLUSH_CMD_RET		= 12,
	ABORT_RET		= 13,
	BATCH_ENC_RET		= 14,
	INIT_BUFFERS_RET	= 15,
	EDFU_INIT_RET		= 16,

	ERR_RET			= 32,
};

struct mfc_cmd_args {
	unsigned int	arg[MAX_H2R_ARG];
};

irqreturn_t mfc_irq(int irq, void *dev_id);

int mfc_cmd_fw_start(struct mfc_dev *dev);
int mfc_cmd_sys_init(struct mfc_dev *dev);
int mfc_cmd_sys_sleep(struct mfc_dev *dev);
int mfc_cmd_sys_wakeup(struct mfc_dev *dev);

int mfc_cmd_inst_open(struct mfc_inst_ctx *ctx);
int mfc_cmd_inst_close(struct mfc_inst_ctx *ctx);
int mfc_cmd_seq_start(struct mfc_inst_ctx *ctx);
int mfc_cmd_init_buffers(struct mfc_inst_ctx *ctx);
int mfc_cmd_frame_start(struct mfc_inst_ctx *ctx);
int mfc_cmd_slice_start(struct mfc_inst_ctx *ctx);

#endif /* __MFC_CMD_H */
