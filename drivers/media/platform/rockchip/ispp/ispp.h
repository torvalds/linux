/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISPP_ISPP_H
#define _RKISPP_ISPP_H

#include "common.h"

#define GRP_ID_ISPP		BIT(0)

enum rkispp_pad {
	RKISPP_PAD_SINK,
	RKISPP_PAD_SINK_PARAMS,
	RKISPP_PAD_SOURCE,
	RKISPP_PAD_SOURCE_STATS,
	RKISPP_PAD_MAX
};

enum rkispp_state {
	ISPP_STOP = 0,
	ISPP_START,
};

struct isppsd_fmt {
	u32 mbus_code;
	u32 fourcc;
	u32 width;
	u32 height;
	u8 wr_fmt;
};

struct rkispp_subdev {
	struct rkispp_device *dev;
	struct v4l2_subdev sd;
	struct v4l2_subdev *remote_sd;
	struct media_pad pads[RKISPP_PAD_MAX];
	struct v4l2_mbus_framefmt in_fmt;
	struct isppsd_fmt out_fmt;
	u32 frm_sync_seq;
	/* timestamp in ns */
	u64 frame_timestamp;
	enum rkispp_state state;
};

u32 cal_fec_mesh(u32 width, u32 height, u32 mode);

int rkispp_register_subdev(struct rkispp_device *dev,
			   struct v4l2_device *v4l2_dev);
void rkispp_unregister_subdev(struct rkispp_device *dev);
#endif
