/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 *
 */

#ifndef __MAXIM4C_VIDEO_PIPE_H__
#define __MAXIM4C_VIDEO_PIPE_H__

#include "maxim4c_i2c.h"

/* Maxim4c Video Pipe In ID: 0 ~ 3 */
enum {
	MAXIM4C_PIPE_I_ID_X = 0,
	MAXIM4C_PIPE_I_ID_Y,
	MAXIM4C_PIPE_I_ID_Z,
	MAXIM4C_PIPE_I_ID_U,
	MAXIM4C_PIPE_I_ID_MAX,
};

/* Maxim4c Video Pipe Out ID: 0 ~ 7 */
enum {
	MAXIM4C_PIPE_O_ID_0 = 0,
	MAXIM4C_PIPE_O_ID_1,
	MAXIM4C_PIPE_O_ID_2,
	MAXIM4C_PIPE_O_ID_3,
	MAXIM4C_PIPE_O_ID_4,
	MAXIM4C_PIPE_O_ID_5,
	MAXIM4C_PIPE_O_ID_6,
	MAXIM4C_PIPE_O_ID_7,
	MAXIM4C_PIPE_O_ID_MAX,
};

/* Maxim4c Video Pipe Out Config */
struct maxim4c_pipe_cfg {
	u8 pipe_enable;
	u8 pipe_idx;
	u8 link_idx;

	struct maxim4c_i2c_init_seq pipe_init_seq;
};

typedef struct maxim4c_video_pipe {
	u8 pipe_enable_mask;

	struct maxim4c_pipe_cfg pipe_cfg[MAXIM4C_PIPE_O_ID_MAX];
	struct maxim4c_i2c_init_seq parallel_init_seq;
} maxim4c_video_pipe_t;

#endif /* __MAXIM4C_VIDEO_PIPE_H__ */
