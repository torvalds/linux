// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/pm_runtime.h>
#include <media/v4l2-mem2mem.h>

#include "iris_instance.h"
#include "iris_utils.h"

bool iris_res_is_less_than(u32 width, u32 height,
			   u32 ref_width, u32 ref_height)
{
	u32 num_mbs = NUM_MBS_PER_FRAME(height, width);
	u32 max_side = max(ref_width, ref_height);

	if (num_mbs < NUM_MBS_PER_FRAME(ref_height, ref_width) &&
	    width < max_side &&
	    height < max_side)
		return true;

	return false;
}

int iris_get_mbpf(struct iris_inst *inst)
{
	struct v4l2_format *inp_f = inst->fmt_src;
	u32 height = max(inp_f->fmt.pix_mp.height, inst->crop.height);
	u32 width = max(inp_f->fmt.pix_mp.width, inst->crop.width);

	return NUM_MBS_PER_FRAME(height, width);
}

bool iris_split_mode_enabled(struct iris_inst *inst)
{
	return inst->fmt_dst->fmt.pix_mp.pixelformat == V4L2_PIX_FMT_NV12;
}

void iris_helper_buffers_done(struct iris_inst *inst, unsigned int type,
			      enum vb2_buffer_state state)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct vb2_v4l2_buffer *buf;

	if (V4L2_TYPE_IS_OUTPUT(type)) {
		while ((buf = v4l2_m2m_src_buf_remove(m2m_ctx)))
			v4l2_m2m_buf_done(buf, state);
	} else if (V4L2_TYPE_IS_CAPTURE(type)) {
		while ((buf = v4l2_m2m_dst_buf_remove(m2m_ctx)))
			v4l2_m2m_buf_done(buf, state);
	}
}

int iris_wait_for_session_response(struct iris_inst *inst, bool is_flush)
{
	struct iris_core *core = inst->core;
	u32 hw_response_timeout_val;
	struct completion *done;
	int ret;

	hw_response_timeout_val = core->iris_platform_data->hw_response_timeout;
	done = is_flush ? &inst->flush_completion : &inst->completion;

	mutex_unlock(&inst->lock);
	ret = wait_for_completion_timeout(done, msecs_to_jiffies(hw_response_timeout_val));
	mutex_lock(&inst->lock);
	if (!ret) {
		iris_inst_change_state(inst, IRIS_INST_ERROR);
		return -ETIMEDOUT;
	}

	return 0;
}

struct iris_inst *iris_get_instance(struct iris_core *core, u32 session_id)
{
	struct iris_inst *inst;

	mutex_lock(&core->lock);
	list_for_each_entry(inst, &core->instances, list) {
		if (inst->session_id == session_id) {
			mutex_unlock(&core->lock);
			return inst;
		}
	}

	mutex_unlock(&core->lock);
	return NULL;
}
