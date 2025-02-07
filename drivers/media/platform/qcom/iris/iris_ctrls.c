// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <media/v4l2-mem2mem.h>
#include "iris_ctrls.h"
#include "iris_instance.h"

static inline bool iris_valid_cap_id(enum platform_inst_fw_cap_type cap_id)
{
	return cap_id >= 1 && cap_id < INST_FW_CAP_MAX;
}

static enum platform_inst_fw_cap_type iris_get_cap_id(u32 id)
{
	switch (id) {
	case V4L2_CID_MPEG_VIDEO_DECODER_MPEG4_DEBLOCK_FILTER:
		return DEBLOCK;
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
		return PROFILE;
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
		return LEVEL;
	default:
		return INST_FW_CAP_MAX;
	}
}

static u32 iris_get_v4l2_id(enum platform_inst_fw_cap_type cap_id)
{
	if (!iris_valid_cap_id(cap_id))
		return 0;

	switch (cap_id) {
	case DEBLOCK:
		return V4L2_CID_MPEG_VIDEO_DECODER_MPEG4_DEBLOCK_FILTER;
	case PROFILE:
		return V4L2_CID_MPEG_VIDEO_H264_PROFILE;
	case LEVEL:
		return V4L2_CID_MPEG_VIDEO_H264_LEVEL;
	default:
		return 0;
	}
}

static int iris_vdec_op_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct iris_inst *inst = container_of(ctrl->handler, struct iris_inst, ctrl_handler);
	enum platform_inst_fw_cap_type cap_id;
	struct platform_inst_fw_cap *cap;
	struct vb2_queue *q;

	cap = &inst->fw_caps[0];
	cap_id = iris_get_cap_id(ctrl->id);
	if (!iris_valid_cap_id(cap_id))
		return -EINVAL;

	q = v4l2_m2m_get_src_vq(inst->m2m_ctx);
	if (vb2_is_streaming(q) &&
	    (!(inst->fw_caps[cap_id].flags & CAP_FLAG_DYNAMIC_ALLOWED)))
		return -EINVAL;

	cap[cap_id].flags |= CAP_FLAG_CLIENT_SET;

	inst->fw_caps[cap_id].value = ctrl->val;

	return 0;
}

static const struct v4l2_ctrl_ops iris_ctrl_ops = {
	.s_ctrl = iris_vdec_op_s_ctrl,
};

int iris_ctrls_init(struct iris_inst *inst)
{
	struct platform_inst_fw_cap *cap = &inst->fw_caps[0];
	u32 num_ctrls = 0, ctrl_idx = 0, idx = 0;
	u32 v4l2_id;
	int ret;

	for (idx = 1; idx < INST_FW_CAP_MAX; idx++) {
		if (iris_get_v4l2_id(cap[idx].cap_id))
			num_ctrls++;
	}
	if (!num_ctrls)
		return -EINVAL;

	/* Adding 1 to num_ctrls to include V4L2_CID_MIN_BUFFERS_FOR_CAPTURE */

	ret = v4l2_ctrl_handler_init(&inst->ctrl_handler, num_ctrls + 1);
	if (ret)
		return ret;

	for (idx = 1; idx < INST_FW_CAP_MAX; idx++) {
		struct v4l2_ctrl *ctrl;

		v4l2_id = iris_get_v4l2_id(cap[idx].cap_id);
		if (!v4l2_id)
			continue;

		if (ctrl_idx >= num_ctrls) {
			ret = -EINVAL;
			goto error;
		}

		if (cap[idx].flags & CAP_FLAG_MENU) {
			ctrl = v4l2_ctrl_new_std_menu(&inst->ctrl_handler,
						      &iris_ctrl_ops,
						      v4l2_id,
						      cap[idx].max,
						      ~(cap[idx].step_or_mask),
						      cap[idx].value);
		} else {
			ctrl = v4l2_ctrl_new_std(&inst->ctrl_handler,
						 &iris_ctrl_ops,
						 v4l2_id,
						 cap[idx].min,
						 cap[idx].max,
						 cap[idx].step_or_mask,
						 cap[idx].value);
		}
		if (!ctrl) {
			ret = -EINVAL;
			goto error;
		}

		ctrl_idx++;
	}

	v4l2_ctrl_new_std(&inst->ctrl_handler, NULL,
			  V4L2_CID_MIN_BUFFERS_FOR_CAPTURE, 1, 32, 1, 4);

	ret = inst->ctrl_handler.error;
	if (ret)
		goto error;

	return 0;
error:
	v4l2_ctrl_handler_free(&inst->ctrl_handler);

	return ret;
}

void iris_session_init_caps(struct iris_core *core)
{
	struct platform_inst_fw_cap *caps;
	u32 i, num_cap, cap_id;

	caps = core->iris_platform_data->inst_fw_caps;
	num_cap = core->iris_platform_data->inst_fw_caps_size;

	for (i = 0; i < num_cap; i++) {
		cap_id = caps[i].cap_id;
		if (!iris_valid_cap_id(cap_id))
			continue;

		core->inst_fw_caps[cap_id].cap_id = caps[i].cap_id;
		core->inst_fw_caps[cap_id].min = caps[i].min;
		core->inst_fw_caps[cap_id].max = caps[i].max;
		core->inst_fw_caps[cap_id].step_or_mask = caps[i].step_or_mask;
		core->inst_fw_caps[cap_id].value = caps[i].value;
		core->inst_fw_caps[cap_id].flags = caps[i].flags;
		core->inst_fw_caps[cap_id].hfi_id = caps[i].hfi_id;
	}
}
