/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_UTILS_H__
#define __IRIS_UTILS_H__

struct iris_core;
#include "iris_buffer.h"

struct iris_hfi_rect_desc {
	u32 left;
	u32 top;
	u32 width;
	u32 height;
};

struct iris_hfi_frame_info {
	u32 picture_type;
	u32 no_output;
	u32 data_corrupt;
	u32 overflow;
};

struct iris_ts_metadata {
	u64 ts_ns;
	u64 ts_us;
	u32 flags;
	struct v4l2_timecode tc;
};

#define NUM_MBS_PER_FRAME(height, width) \
	(DIV_ROUND_UP(height, 16) * DIV_ROUND_UP(width, 16))

static inline enum iris_buffer_type iris_v4l2_type_to_driver(u32 type)
{
	if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return BUF_INPUT;
	else
		return BUF_OUTPUT;
}

bool iris_res_is_less_than(u32 width, u32 height,
			   u32 ref_width, u32 ref_height);
int iris_get_mbpf(struct iris_inst *inst);
bool iris_split_mode_enabled(struct iris_inst *inst);
struct iris_inst *iris_get_instance(struct iris_core *core, u32 session_id);
void iris_helper_buffers_done(struct iris_inst *inst, unsigned int type,
			      enum vb2_buffer_state state);
int iris_wait_for_session_response(struct iris_inst *inst, bool is_flush);
int iris_check_core_mbpf(struct iris_inst *inst);
int iris_check_core_mbps(struct iris_inst *inst);

#endif
