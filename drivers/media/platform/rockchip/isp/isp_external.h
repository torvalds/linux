/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_EXTERNAL_H
#define _RKISP_EXTERNAL_H


#define RKISP_VICAP_CMD_MODE \
	_IOW('V', BASE_VIDIOC_PRIVATE + 0, struct rkisp_vicap_mode)

#define RKISP_VICAP_CMD_INIT_BUF \
	 _IOW('V', BASE_VIDIOC_PRIVATE + 1, int)

#define RKISP_VICAP_BUF_CNT 1
#define RKISP_RX_BUF_POOL_MAX (RKISP_VICAP_BUF_CNT * 3)

struct rkisp_vicap_mode {
	char *name;
	bool is_rdbk;
};

enum rx_buf_type {
	BUF_SHORT,
	BUF_MIDDLE,
	BUF_LONG,
};

struct rkisp_rx_buf_pool {
	struct rkisp_rx_buf *dbufs;
	void *mem_priv;
	dma_addr_t dma;
	void *vaddr;
};

struct rkisp_rx_buf {
	struct list_head list;
	struct dma_buf *dbuf;
	enum rx_buf_type type;
	u64 timestamp;
	u32 sequence;
	bool is_init;
};

#endif
