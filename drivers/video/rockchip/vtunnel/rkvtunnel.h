/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd
 */
#ifndef __ROCKCHIP_VIDEO_TUNNEL_H__
#define __ROCKCHIP_VIDEO_TUNNEL_H__

#include <linux/ioctl.h>
#include <linux/types.h>

#define MAX_BUF_HANDLE_FDS		16
#define MAX_BUF_HANDLE_INTS		128

#define RKVT_IOC_MAGIC			'V'
#define RKVT_IOWR(nr, type)		_IOWR(RKVT_IOC_MAGIC, nr, type)

#define RKVT_IOC_ALLOC_ID		RKVT_IOWR(0x0, struct rkvt_alloc_id_data)
#define RKVT_IOC_FREE_ID		RKVT_IOWR(0x1, struct rkvt_alloc_id_data)
#define RKVT_IOC_CTRL			RKVT_IOWR(0x2, struct rkvt_ctrl_data)
#define RKVT_IOC_QUEUE_BUF		RKVT_IOWR(0x3, struct rkvt_buf_data)
#define RKVT_IOC_DEQUE_BUF		RKVT_IOWR(0x4, struct rkvt_buf_data)
#define RKVT_IOC_CANCEL_BUF		RKVT_IOWR(0x5, struct rkvt_buf_data)
#define RKVT_IOC_ACQUIRE_BUF		RKVT_IOWR(0x6, struct rkvt_buf_data)
#define RKVT_IOC_RELEASE_BUF		RKVT_IOWR(0x7, struct rkvt_buf_data)

// caller type
enum rkvt_caller_e {
	RKVT_CALLER_PRODUCER,
	RKVT_CALLER_CONSUMER,
	RKVT_CALLER_BUTT,
};

// video tunnel caller control
enum rkvt_ctrl_cmd_e {
	RKVT_CTRL_CONNECT,
	RKVT_CTRL_DISCONNECT,
	RKVT_CTRL_RESET,
	RKVT_CTRL_HAS_CONSUMER,
	RKVT_CTRL_BUTT,
};

struct rkvt_alloc_id_data {
	int vt_id;
};

struct rkvt_ctrl_data {
	int vt_id;
	enum rkvt_caller_e caller;
	enum rkvt_ctrl_cmd_e ctrl_cmd;
	int ctrl_data;
};

struct rkvt_rect {
	int left;
	int top;
	int right;
	int bottom;
};

struct rkvt_buf_base {
	int vt_id;
	int fence_fd;
	int buf_status;
	int num_fds;     /* number of file-descriptors at &data[0] */
	int num_ints;    /* number of ints at &data[numFds] */
	int reserved;
	int fds[MAX_BUF_HANDLE_FDS];
	int ints[MAX_BUF_HANDLE_INTS];
	int64_t priv_data;
	uint64_t expected_present_time;
	uint64_t buffer_id;
	struct rkvt_rect crop;
};

struct rkvt_buf_data {
	int vt_id;
	int timeout_ms;		/* 0: non block, negative: block, other: timeout ms */
	struct rkvt_buf_base base;
};

#endif
