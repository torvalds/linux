/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_EXTERNAL_H
#define _RKISP_EXTERNAL_H

#define RKISP_VICAP_CMD_MODE \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 0, struct rkisp_vicap_mode)

#define RKISP_VICAP_CMD_INIT_BUF \
	 _IOW('V', BASE_VIDIOC_PRIVATE + 1, int)

#define RKISP_VICAP_CMD_RX_BUFFER_FREE \
	 _IOW('V', BASE_VIDIOC_PRIVATE + 2, struct rkisp_rx_buf)

#define RKISP_VICAP_BUF_CNT 3
#define RKISP_VICAP_BUF_CNT_MAX 8
#define RKISP_RX_BUF_POOL_MAX (RKISP_VICAP_BUF_CNT_MAX * 3)

struct rkisp_vicap_input {
	u8 merge_num;
	u8 index;
};

enum rkisp_vicap_link {
	RKISP_VICAP_ONLINE,
	RKISP_VICAP_RDBK_AIQ,
	RKISP_VICAP_RDBK_AUTO,
};

struct rkisp_vicap_mode {
	char *name;
	enum rkisp_vicap_link rdbk_mode;

	struct rkisp_vicap_input input;
};

enum rx_buf_type {
	BUF_SHORT,
	BUF_MIDDLE,
	BUF_LONG,
};

struct rkisp_rx_buf {
	struct list_head list;
	struct dma_buf *dbuf;
	dma_addr_t dma;
	u64 timestamp;
	u32 sequence;
	u32 type;
	u32 runtime_us;

	bool is_init;
	bool is_first;

	bool is_resmem;
	bool is_switch;

	bool is_uncompact;
};

#endif
