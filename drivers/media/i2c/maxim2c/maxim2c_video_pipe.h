/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 *
 */

#ifndef __MAXIM2C_VIDEO_PIPE_H__
#define __MAXIM2C_VIDEO_PIPE_H__

#include "maxim2c_i2c.h"

/* Video Pipe In ID: 0 ~ 3 */
enum {
	MAXIM2C_PIPE_I_ID_X = 0,
	MAXIM2C_PIPE_I_ID_Y,
	MAXIM2C_PIPE_I_ID_Z,
	MAXIM2C_PIPE_I_ID_U,
	MAXIM2C_PIPE_I_ID_MAX,
};

/* Video Pipe Out ID: 0 ~ 1 */
enum {
	MAXIM2C_PIPE_O_ID_Y = 0,
	MAXIM2C_PIPE_O_ID_Z,
	MAXIM2C_PIPE_O_ID_MAX,
};

/* Video Pipe Out Config */
struct maxim2c_pipe_cfg {
	u8 pipe_enable;
	u8 pipe_idx;
	u8 link_idx;

	struct maxim2c_i2c_init_seq pipe_init_seq;
};

typedef struct maxim2c_video_pipe {
	u8 pipe_enable_mask;

	struct maxim2c_pipe_cfg pipe_cfg[MAXIM2C_PIPE_O_ID_MAX];
	struct maxim2c_i2c_init_seq parallel_init_seq;
} maxim2c_video_pipe_t;

#endif /* __MAXIM2C_VIDEO_PIPE_H__ */
