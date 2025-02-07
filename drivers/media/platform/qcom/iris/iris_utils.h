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

#define NUM_MBS_PER_FRAME(height, width) \
	(DIV_ROUND_UP(height, 16) * DIV_ROUND_UP(width, 16))

static inline enum iris_buffer_type iris_v4l2_type_to_driver(u32 type)
{
	if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return BUF_INPUT;
	else
		return BUF_OUTPUT;
}

int iris_get_mbpf(struct iris_inst *inst);
struct iris_inst *iris_get_instance(struct iris_core *core, u32 session_id);
int iris_wait_for_session_response(struct iris_inst *inst, bool is_flush);

#endif
