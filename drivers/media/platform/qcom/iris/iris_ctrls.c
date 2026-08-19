// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <media/v4l2-mem2mem.h>

#include "iris_ctrls.h"
#include "iris_hfi_gen1_defines.h"
#include "iris_hfi_gen2_defines.h"
#include "iris_instance.h"

#define CABAC_MAX_BITRATE 160000000
#define CAVLC_MAX_BITRATE 220000000

static inline bool iris_valid_cap_id(enum platform_inst_fw_cap_type cap_id)
{
	return cap_id >= 1 && cap_id < INST_FW_CAP_MAX;
}

static enum platform_inst_fw_cap_type iris_get_cap_id(u32 id)
{
	switch (id) {
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
		return PROFILE_H264;
	case V4L2_CID_MPEG_VIDEO_HEVC_PROFILE:
		return PROFILE_HEVC;
	case V4L2_CID_MPEG_VIDEO_VP9_PROFILE:
		return PROFILE_VP9;
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
		return LEVEL_H264;
	case V4L2_CID_MPEG_VIDEO_HEVC_LEVEL:
		return LEVEL_HEVC;
	case V4L2_CID_MPEG_VIDEO_VP9_LEVEL:
		return LEVEL_VP9;
	case V4L2_CID_MPEG_VIDEO_HEVC_TIER:
		return TIER;
	case V4L2_CID_MPEG_VIDEO_HEADER_MODE:
		return HEADER_MODE;
	case V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR:
		return PREPEND_SPSPPS_TO_IDR;
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		return BITRATE;
	case V4L2_CID_MPEG_VIDEO_BITRATE_PEAK:
		return BITRATE_PEAK;
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		return BITRATE_MODE;
	case V4L2_CID_MPEG_VIDEO_FRAME_SKIP_MODE:
		return FRAME_SKIP_MODE;
	case V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE:
		return FRAME_RC_ENABLE;
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		return GOP_SIZE;
	case V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE:
		return ENTROPY_MODE;
	case V4L2_CID_MPEG_VIDEO_H264_MIN_QP:
		return MIN_FRAME_QP_H264;
	case V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP:
		return MIN_FRAME_QP_HEVC;
	case V4L2_CID_MPEG_VIDEO_H264_MAX_QP:
		return MAX_FRAME_QP_H264;
	case V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP:
		return MAX_FRAME_QP_HEVC;
	case V4L2_CID_MPEG_VIDEO_H264_I_FRAME_MIN_QP:
		return I_FRAME_MIN_QP_H264;
	case V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_MIN_QP:
		return I_FRAME_MIN_QP_HEVC;
	case V4L2_CID_MPEG_VIDEO_H264_P_FRAME_MIN_QP:
		return P_FRAME_MIN_QP_H264;
	case V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_MIN_QP:
		return P_FRAME_MIN_QP_HEVC;
	case V4L2_CID_MPEG_VIDEO_H264_B_FRAME_MIN_QP:
		return B_FRAME_MIN_QP_H264;
	case V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_MIN_QP:
		return B_FRAME_MIN_QP_HEVC;
	case V4L2_CID_MPEG_VIDEO_H264_I_FRAME_MAX_QP:
		return I_FRAME_MAX_QP_H264;
	case V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_MAX_QP:
		return I_FRAME_MAX_QP_HEVC;
	case V4L2_CID_MPEG_VIDEO_H264_P_FRAME_MAX_QP:
		return P_FRAME_MAX_QP_H264;
	case V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_MAX_QP:
		return P_FRAME_MAX_QP_HEVC;
	case V4L2_CID_MPEG_VIDEO_H264_B_FRAME_MAX_QP:
		return B_FRAME_MAX_QP_H264;
	case V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_MAX_QP:
		return B_FRAME_MAX_QP_HEVC;
	case V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP:
		return I_FRAME_QP_H264;
	case V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_QP:
		return I_FRAME_QP_HEVC;
	case V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP:
		return P_FRAME_QP_H264;
	case V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_QP:
		return P_FRAME_QP_HEVC;
	case V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP:
		return B_FRAME_QP_H264;
	case V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_QP:
		return B_FRAME_QP_HEVC;
	case V4L2_CID_MPEG_VIDEO_AV1_PROFILE:
		return PROFILE_AV1;
	case V4L2_CID_MPEG_VIDEO_AV1_LEVEL:
		return LEVEL_AV1;
	case V4L2_CID_ROTATE:
		return ROTATION;
	case V4L2_CID_HFLIP:
		return HFLIP;
	case V4L2_CID_VFLIP:
		return VFLIP;
	case V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE:
		return IR_TYPE;
	case V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD:
		return IR_PERIOD;
	case V4L2_CID_MPEG_VIDEO_LTR_COUNT:
		return LTR_COUNT;
	case V4L2_CID_MPEG_VIDEO_USE_LTR_FRAMES:
		return USE_LTR;
	case V4L2_CID_MPEG_VIDEO_FRAME_LTR_INDEX:
		return MARK_LTR;
	case V4L2_CID_MPEG_VIDEO_B_FRAMES:
		return B_FRAME;
	case V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING:
		return LAYER_ENABLE;
	case V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_TYPE:
		return LAYER_TYPE_H264;
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_TYPE:
		return LAYER_TYPE_HEVC;
	case V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_LAYER:
		return LAYER_COUNT_H264;
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_LAYER:
		return LAYER_COUNT_HEVC;
	case V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L0_BR:
		return LAYER0_BITRATE_H264;
	case V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L1_BR:
		return LAYER1_BITRATE_H264;
	case V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L2_BR:
		return LAYER2_BITRATE_H264;
	case V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L3_BR:
		return LAYER3_BITRATE_H264;
	case V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L4_BR:
		return LAYER4_BITRATE_H264;
	case V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L5_BR:
		return LAYER5_BITRATE_H264;
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L0_BR:
		return LAYER0_BITRATE_HEVC;
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L1_BR:
		return LAYER1_BITRATE_HEVC;
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L2_BR:
		return LAYER2_BITRATE_HEVC;
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L3_BR:
		return LAYER3_BITRATE_HEVC;
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L4_BR:
		return LAYER4_BITRATE_HEVC;
	case V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L5_BR:
		return LAYER5_BITRATE_HEVC;
	default:
		return INST_FW_CAP_MAX;
	}
}

static u32 iris_get_v4l2_id(enum platform_inst_fw_cap_type cap_id)
{
	if (!iris_valid_cap_id(cap_id))
		return 0;

	switch (cap_id) {
	case PROFILE_H264:
		return V4L2_CID_MPEG_VIDEO_H264_PROFILE;
	case PROFILE_HEVC:
		return V4L2_CID_MPEG_VIDEO_HEVC_PROFILE;
	case PROFILE_VP9:
		return V4L2_CID_MPEG_VIDEO_VP9_PROFILE;
	case LEVEL_H264:
		return V4L2_CID_MPEG_VIDEO_H264_LEVEL;
	case LEVEL_HEVC:
		return V4L2_CID_MPEG_VIDEO_HEVC_LEVEL;
	case LEVEL_VP9:
		return V4L2_CID_MPEG_VIDEO_VP9_LEVEL;
	case TIER:
		return V4L2_CID_MPEG_VIDEO_HEVC_TIER;
	case HEADER_MODE:
		return V4L2_CID_MPEG_VIDEO_HEADER_MODE;
	case PREPEND_SPSPPS_TO_IDR:
		return V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR;
	case BITRATE:
		return V4L2_CID_MPEG_VIDEO_BITRATE;
	case BITRATE_PEAK:
		return V4L2_CID_MPEG_VIDEO_BITRATE_PEAK;
	case BITRATE_MODE:
		return V4L2_CID_MPEG_VIDEO_BITRATE_MODE;
	case FRAME_SKIP_MODE:
		return V4L2_CID_MPEG_VIDEO_FRAME_SKIP_MODE;
	case FRAME_RC_ENABLE:
		return V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE;
	case GOP_SIZE:
		return V4L2_CID_MPEG_VIDEO_GOP_SIZE;
	case ENTROPY_MODE:
		return V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE;
	case MIN_FRAME_QP_H264:
		return V4L2_CID_MPEG_VIDEO_H264_MIN_QP;
	case MIN_FRAME_QP_HEVC:
		return V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP;
	case MAX_FRAME_QP_H264:
		return V4L2_CID_MPEG_VIDEO_H264_MAX_QP;
	case MAX_FRAME_QP_HEVC:
		return V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP;
	case I_FRAME_MIN_QP_H264:
		return V4L2_CID_MPEG_VIDEO_H264_I_FRAME_MIN_QP;
	case I_FRAME_MIN_QP_HEVC:
		return V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_MIN_QP;
	case P_FRAME_MIN_QP_H264:
		return V4L2_CID_MPEG_VIDEO_H264_P_FRAME_MIN_QP;
	case P_FRAME_MIN_QP_HEVC:
		return V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_MIN_QP;
	case B_FRAME_MIN_QP_H264:
		return V4L2_CID_MPEG_VIDEO_H264_B_FRAME_MIN_QP;
	case B_FRAME_MIN_QP_HEVC:
		return V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_MIN_QP;
	case I_FRAME_MAX_QP_H264:
		return V4L2_CID_MPEG_VIDEO_H264_I_FRAME_MAX_QP;
	case I_FRAME_MAX_QP_HEVC:
		return V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_MAX_QP;
	case P_FRAME_MAX_QP_H264:
		return V4L2_CID_MPEG_VIDEO_H264_P_FRAME_MAX_QP;
	case P_FRAME_MAX_QP_HEVC:
		return V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_MAX_QP;
	case B_FRAME_MAX_QP_H264:
		return V4L2_CID_MPEG_VIDEO_H264_B_FRAME_MAX_QP;
	case B_FRAME_MAX_QP_HEVC:
		return V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_MAX_QP;
	case I_FRAME_QP_H264:
		return V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP;
	case I_FRAME_QP_HEVC:
		return V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_QP;
	case P_FRAME_QP_H264:
		return V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP;
	case P_FRAME_QP_HEVC:
		return V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_QP;
	case B_FRAME_QP_H264:
		return V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP;
	case B_FRAME_QP_HEVC:
		return V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_QP;
	case PROFILE_AV1:
		return V4L2_CID_MPEG_VIDEO_AV1_PROFILE;
	case LEVEL_AV1:
		return V4L2_CID_MPEG_VIDEO_AV1_LEVEL;
	case ROTATION:
		return V4L2_CID_ROTATE;
	case HFLIP:
		return V4L2_CID_HFLIP;
	case VFLIP:
		return V4L2_CID_VFLIP;
	case IR_TYPE:
		return V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE;
	case IR_PERIOD:
		return V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD;
	case LTR_COUNT:
		return V4L2_CID_MPEG_VIDEO_LTR_COUNT;
	case USE_LTR:
		return V4L2_CID_MPEG_VIDEO_USE_LTR_FRAMES;
	case MARK_LTR:
		return V4L2_CID_MPEG_VIDEO_FRAME_LTR_INDEX;
	case B_FRAME:
		return V4L2_CID_MPEG_VIDEO_B_FRAMES;
	case LAYER_ENABLE:
		return V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING;
	case LAYER_TYPE_H264:
		return V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_TYPE;
	case LAYER_TYPE_HEVC:
		return V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_TYPE;
	case LAYER_COUNT_H264:
		return V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_LAYER;
	case LAYER_COUNT_HEVC:
		return V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_LAYER;
	case LAYER0_BITRATE_H264:
		return V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L0_BR;
	case LAYER1_BITRATE_H264:
		return V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L1_BR;
	case LAYER2_BITRATE_H264:
		return V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L2_BR;
	case LAYER3_BITRATE_H264:
		return V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L3_BR;
	case LAYER4_BITRATE_H264:
		return V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L4_BR;
	case LAYER5_BITRATE_H264:
		return V4L2_CID_MPEG_VIDEO_H264_HIER_CODING_L5_BR;
	case LAYER0_BITRATE_HEVC:
		return V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L0_BR;
	case LAYER1_BITRATE_HEVC:
		return V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L1_BR;
	case LAYER2_BITRATE_HEVC:
		return V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L2_BR;
	case LAYER3_BITRATE_HEVC:
		return V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L3_BR;
	case LAYER4_BITRATE_HEVC:
		return V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L4_BR;
	case LAYER5_BITRATE_HEVC:
		return V4L2_CID_MPEG_VIDEO_HEVC_HIER_CODING_L5_BR;
	default:
		return 0;
	}
}

static int iris_op_s_ctrl(struct v4l2_ctrl *ctrl)
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

	if (vb2_is_streaming(q)) {
		if (cap[cap_id].set)
			cap[cap_id].set(inst, cap_id);
	}

	return 0;
}

static const struct v4l2_ctrl_ops iris_ctrl_ops = {
	.s_ctrl = iris_op_s_ctrl,
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

	/* Adding 1 to num_ctrls to include
	 * V4L2_CID_MIN_BUFFERS_FOR_CAPTURE for decoder and
	 * V4L2_CID_MIN_BUFFERS_FOR_OUTPUT for encoder
	 */

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

	if (inst->domain == DECODER) {
		v4l2_ctrl_new_std(&inst->ctrl_handler, NULL,
				  V4L2_CID_MIN_BUFFERS_FOR_CAPTURE, 1, 32, 1, 4);
	} else {
		v4l2_ctrl_new_std(&inst->ctrl_handler, NULL,
				  V4L2_CID_MIN_BUFFERS_FOR_OUTPUT, 1, 32, 1, 4);
	}

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
	const struct platform_inst_fw_cap *caps;
	u32 i, num_cap, cap_id;

	caps = core->iris_firmware_data->inst_fw_caps_dec;
	num_cap = core->iris_firmware_data->inst_fw_caps_dec_size;

	for (i = 0; i < num_cap; i++) {
		cap_id = caps[i].cap_id;
		if (!iris_valid_cap_id(cap_id))
			continue;

		core->inst_fw_caps_dec[cap_id].cap_id = caps[i].cap_id;
		core->inst_fw_caps_dec[cap_id].step_or_mask = caps[i].step_or_mask;
		core->inst_fw_caps_dec[cap_id].flags = caps[i].flags;
		core->inst_fw_caps_dec[cap_id].hfi_id = caps[i].hfi_id;
		core->inst_fw_caps_dec[cap_id].set = caps[i].set;

		if (cap_id == PIPE) {
			core->inst_fw_caps_dec[cap_id].value =
				core->iris_platform_data->num_vpp_pipe;
			core->inst_fw_caps_dec[cap_id].min =
				core->iris_platform_data->num_vpp_pipe;
			core->inst_fw_caps_dec[cap_id].max =
				core->iris_platform_data->num_vpp_pipe;
		} else {
			core->inst_fw_caps_dec[cap_id].min = caps[i].min;
			core->inst_fw_caps_dec[cap_id].max = caps[i].max;
			core->inst_fw_caps_dec[cap_id].value = caps[i].value;
		}
	}

	caps = core->iris_firmware_data->inst_fw_caps_enc;
	num_cap = core->iris_firmware_data->inst_fw_caps_enc_size;

	for (i = 0; i < num_cap; i++) {
		cap_id = caps[i].cap_id;
		if (!iris_valid_cap_id(cap_id))
			continue;

		core->inst_fw_caps_enc[cap_id].cap_id = caps[i].cap_id;
		core->inst_fw_caps_enc[cap_id].min = caps[i].min;
		core->inst_fw_caps_enc[cap_id].max = caps[i].max;
		core->inst_fw_caps_enc[cap_id].step_or_mask = caps[i].step_or_mask;
		core->inst_fw_caps_enc[cap_id].value = caps[i].value;
		core->inst_fw_caps_enc[cap_id].flags = caps[i].flags;
		core->inst_fw_caps_enc[cap_id].hfi_id = caps[i].hfi_id;
		core->inst_fw_caps_enc[cap_id].set = caps[i].set;
	}
}

static u32 iris_get_port_info(struct iris_inst *inst,
			      enum platform_inst_fw_cap_type cap_id)
{
	if (inst->domain == DECODER) {
		if (inst->fw_caps[cap_id].flags & CAP_FLAG_INPUT_PORT)
			return HFI_PORT_BITSTREAM;
		else if (inst->fw_caps[cap_id].flags & CAP_FLAG_OUTPUT_PORT)
			return HFI_PORT_RAW;
	} else {
		if (inst->fw_caps[cap_id].flags & CAP_FLAG_INPUT_PORT)
			return HFI_PORT_RAW;
		else if (inst->fw_caps[cap_id].flags & CAP_FLAG_OUTPUT_PORT)
			return HFI_PORT_BITSTREAM;
	}

	return HFI_PORT_NONE;
}

int iris_set_u32_enum(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 hfi_value = inst->fw_caps[cap_id].value;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;

	return hfi_ops->session_set_property(inst, hfi_id,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_U32_ENUM,
					     &hfi_value, sizeof(u32));
}

int iris_set_u32(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 hfi_value = inst->fw_caps[cap_id].value;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;

	return hfi_ops->session_set_property(inst, hfi_id,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_U32,
					     &hfi_value, sizeof(u32));
}

int iris_set_stage(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	struct v4l2_format *inp_f = inst->fmt_src;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	u32 height = inp_f->fmt.pix_mp.height;
	u32 width = inp_f->fmt.pix_mp.width;
	u32 work_mode = STAGE_2;

	if (inst->domain == DECODER) {
		if (iris_res_is_less_than(width, height, 1280, 720))
			work_mode = STAGE_1;
	}

	return hfi_ops->session_set_property(inst, hfi_id,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_U32,
					     &work_mode, sizeof(u32));
}

int iris_set_pipe(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 work_route = inst->fw_caps[PIPE].value;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;

	return hfi_ops->session_set_property(inst, hfi_id,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_U32,
					     &work_route, sizeof(u32));
}

int iris_set_profile(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 hfi_id, hfi_value;

	if (inst->codec == V4L2_PIX_FMT_H264) {
		hfi_id = inst->fw_caps[PROFILE_H264].hfi_id;
		hfi_value = inst->fw_caps[PROFILE_H264].value;
	} else {
		hfi_id = inst->fw_caps[PROFILE_HEVC].hfi_id;
		hfi_value = inst->fw_caps[PROFILE_HEVC].value;
	}

	return hfi_ops->session_set_property(inst, hfi_id,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_U32_ENUM,
					     &hfi_value, sizeof(u32));
}

int iris_set_level(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 hfi_id, hfi_value;

	if (inst->codec == V4L2_PIX_FMT_H264) {
		hfi_id = inst->fw_caps[LEVEL_H264].hfi_id;
		hfi_value = inst->fw_caps[LEVEL_H264].value;
	} else {
		hfi_id = inst->fw_caps[LEVEL_HEVC].hfi_id;
		hfi_value = inst->fw_caps[LEVEL_HEVC].value;
	}

	return hfi_ops->session_set_property(inst, hfi_id,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_U32_ENUM,
					     &hfi_value, sizeof(u32));
}

int iris_set_profile_level_gen1(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	struct hfi_profile_level pl;

	if (inst->codec == V4L2_PIX_FMT_H264) {
		pl.profile = inst->fw_caps[PROFILE_H264].value;
		pl.level = inst->fw_caps[LEVEL_H264].value;
	} else {
		pl.profile = inst->fw_caps[PROFILE_HEVC].value;
		pl.level = inst->fw_caps[LEVEL_HEVC].value;
	}

	return hfi_ops->session_set_property(inst, hfi_id,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_U32_ENUM,
					     &pl, sizeof(u32));
}

int iris_set_header_mode_gen1(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 header_mode = inst->fw_caps[cap_id].value;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	u32 hfi_val;

	if (header_mode == V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE)
		hfi_val = 0;
	else
		hfi_val = 1;

	return hfi_ops->session_set_property(inst, hfi_id,
					 HFI_HOST_FLAGS_NONE,
				     iris_get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32,
				     &hfi_val, sizeof(u32));
}

int iris_set_header_mode_gen2(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 prepend_sps_pps = inst->fw_caps[PREPEND_SPSPPS_TO_IDR].value;
	u32 header_mode = inst->fw_caps[cap_id].value;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	u32 hfi_val;

	if (prepend_sps_pps)
		hfi_val = HFI_SEQ_HEADER_PREFIX_WITH_SYNC_FRAME;
	else if (header_mode == V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME)
		hfi_val = HFI_SEQ_HEADER_JOINED_WITH_1ST_FRAME;
	else
		hfi_val = HFI_SEQ_HEADER_SEPERATE_FRAME;

	return hfi_ops->session_set_property(inst, hfi_id,
					 HFI_HOST_FLAGS_NONE,
				     iris_get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32_ENUM,
				     &hfi_val, sizeof(u32));
}

int iris_set_bitrate_gen1(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 entropy_mode = inst->fw_caps[ENTROPY_MODE].value;
	u32 bitrate = inst->fw_caps[cap_id].value;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	struct hfi_bitrate hfi_val;
	u32 max_bitrate;

	if (!(inst->fw_caps[cap_id].flags & CAP_FLAG_CLIENT_SET) && cap_id != BITRATE)
		return -EINVAL;

	if (inst->codec == V4L2_PIX_FMT_HEVC) {
		max_bitrate = CABAC_MAX_BITRATE;
	} else {
		if (entropy_mode == V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC)
			max_bitrate = CABAC_MAX_BITRATE;
		else
			max_bitrate = CAVLC_MAX_BITRATE;
	}

	hfi_val.bitrate = min(bitrate, max_bitrate);

	switch (cap_id) {
	case BITRATE:
	case LAYER0_BITRATE_H264:
		hfi_val.layer_id = 0;
		break;
	case LAYER1_BITRATE_H264:
		hfi_val.layer_id = 1;
		break;
	case LAYER2_BITRATE_H264:
		hfi_val.layer_id = 2;
		break;
	case LAYER3_BITRATE_H264:
		hfi_val.layer_id = 3;
		break;
	case LAYER4_BITRATE_H264:
		hfi_val.layer_id = 4;
		break;
	case LAYER5_BITRATE_H264:
		hfi_val.layer_id = 5;
		break;
	default:
		return -EINVAL;
	}

	if (hfi_val.layer_id > 0 && !inst->fw_caps[LAYER_ENABLE].value)
		return -EINVAL;

	return hfi_ops->session_set_property(inst, hfi_id,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_STRUCTURE,
					     &hfi_val, sizeof(hfi_val));
}

int iris_set_bitrate_gen2(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 entropy_mode = inst->fw_caps[ENTROPY_MODE].value;
	u32 bitrate = inst->fw_caps[cap_id].value;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	u32 max_bitrate;

	if (inst->codec == V4L2_PIX_FMT_HEVC)
		max_bitrate = CABAC_MAX_BITRATE;

	if (entropy_mode == V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC)
		max_bitrate = CABAC_MAX_BITRATE;
	else
		max_bitrate = CAVLC_MAX_BITRATE;

	bitrate = min(bitrate, max_bitrate);

	return hfi_ops->session_set_property(inst, hfi_id,
					 HFI_HOST_FLAGS_NONE,
				     iris_get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32,
				     &bitrate, sizeof(u32));
}

int iris_set_peak_bitrate(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 rc_mode = inst->fw_caps[BITRATE_MODE].value;
	u32 peak_bitrate = inst->fw_caps[cap_id].value;
	u32 bitrate = inst->fw_caps[BITRATE].value;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;

	if (rc_mode != V4L2_MPEG_VIDEO_BITRATE_MODE_CBR)
		return 0;

	if (inst->fw_caps[cap_id].flags & CAP_FLAG_CLIENT_SET) {
		if (peak_bitrate < bitrate)
			peak_bitrate = bitrate;
	} else {
		peak_bitrate = bitrate;
	}

	inst->fw_caps[cap_id].value = peak_bitrate;

	return hfi_ops->session_set_property(inst, hfi_id,
					 HFI_HOST_FLAGS_NONE,
				     iris_get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32,
				     &peak_bitrate, sizeof(u32));
}

int iris_set_bitrate_mode_gen1(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 bitrate_mode = inst->fw_caps[BITRATE_MODE].value;
	u32 frame_rc = inst->fw_caps[FRAME_RC_ENABLE].value;
	u32 frame_skip = inst->fw_caps[FRAME_SKIP_MODE].value;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	u32 rc_mode = 0;

	if (!frame_rc)
		rc_mode = HFI_RATE_CONTROL_OFF;
	else if (bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR)
		rc_mode = frame_skip ? HFI_RATE_CONTROL_VBR_VFR : HFI_RATE_CONTROL_VBR_CFR;
	else if (bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_CBR)
		rc_mode = frame_skip ? HFI_RATE_CONTROL_CBR_VFR : HFI_RATE_CONTROL_CBR_CFR;
	else if (bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_CQ)
		rc_mode = HFI_RATE_CONTROL_CQ;

	inst->hfi_rc_type = rc_mode;

	return hfi_ops->session_set_property(inst, hfi_id,
					 HFI_HOST_FLAGS_NONE,
				     iris_get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32_ENUM,
				     &rc_mode, sizeof(u32));
}

int iris_set_bitrate_mode_gen2(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 bitrate_mode = inst->fw_caps[BITRATE_MODE].value;
	u32 frame_rc = inst->fw_caps[FRAME_RC_ENABLE].value;
	u32 frame_skip = inst->fw_caps[FRAME_SKIP_MODE].value;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	u32 rc_mode = 0;

	if (!frame_rc)
		rc_mode = HFI_RC_OFF;
	else if (bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR)
		rc_mode = HFI_RC_VBR_CFR;
	else if (bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_CBR)
		rc_mode = frame_skip ? HFI_RC_CBR_VFR : HFI_RC_CBR_CFR;
	else if (bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_CQ)
		rc_mode = HFI_RC_CQ;

	inst->hfi_rc_type = rc_mode;

	return hfi_ops->session_set_property(inst, hfi_id,
					 HFI_HOST_FLAGS_NONE,
				     iris_get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32_ENUM,
				     &rc_mode, sizeof(u32));
}

int iris_set_entropy_mode_gen1(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 entropy_mode = inst->fw_caps[cap_id].value;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	u32 hfi_val;

	if (inst->codec != V4L2_PIX_FMT_H264)
		return 0;

	hfi_val = (entropy_mode == V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC) ?
		HFI_H264_ENTROPY_CAVLC : HFI_H264_ENTROPY_CABAC;

	return hfi_ops->session_set_property(inst, hfi_id,
					 HFI_HOST_FLAGS_NONE,
				     iris_get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32,
				     &hfi_val, sizeof(u32));
}

int iris_set_entropy_mode_gen2(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 entropy_mode = inst->fw_caps[cap_id].value;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	u32 profile;

	if (inst->codec != V4L2_PIX_FMT_H264)
		return 0;

	profile = inst->fw_caps[PROFILE_H264].value;

	if (profile == V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE ||
	    profile == V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE)
		entropy_mode = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC;

	inst->fw_caps[cap_id].value = entropy_mode;

	return hfi_ops->session_set_property(inst, hfi_id,
					 HFI_HOST_FLAGS_NONE,
				     iris_get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32,
				     &entropy_mode, sizeof(u32));
}

int iris_set_min_qp(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 i_qp_enable = 0, p_qp_enable = 0, b_qp_enable = 0;
	u32 i_frame_qp = 0, p_frame_qp = 0, b_frame_qp = 0;
	u32 min_qp_enable = 0, client_qp_enable = 0;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	u32 hfi_val;

	if (inst->codec == V4L2_PIX_FMT_H264) {
		if (inst->fw_caps[MIN_FRAME_QP_H264].flags & CAP_FLAG_CLIENT_SET)
			min_qp_enable = 1;
		if (min_qp_enable ||
		    (inst->fw_caps[I_FRAME_MIN_QP_H264].flags & CAP_FLAG_CLIENT_SET))
			i_qp_enable = 1;
		if (min_qp_enable ||
		    (inst->fw_caps[P_FRAME_MIN_QP_H264].flags & CAP_FLAG_CLIENT_SET))
			p_qp_enable = 1;
		if (min_qp_enable ||
		    (inst->fw_caps[B_FRAME_MIN_QP_H264].flags & CAP_FLAG_CLIENT_SET))
			b_qp_enable = 1;
	} else {
		if (inst->fw_caps[MIN_FRAME_QP_HEVC].flags & CAP_FLAG_CLIENT_SET)
			min_qp_enable = 1;
		if (min_qp_enable ||
		    (inst->fw_caps[I_FRAME_MIN_QP_HEVC].flags & CAP_FLAG_CLIENT_SET))
			i_qp_enable = 1;
		if (min_qp_enable ||
		    (inst->fw_caps[P_FRAME_MIN_QP_HEVC].flags & CAP_FLAG_CLIENT_SET))
			p_qp_enable = 1;
		if (min_qp_enable ||
		    (inst->fw_caps[B_FRAME_MIN_QP_HEVC].flags & CAP_FLAG_CLIENT_SET))
			b_qp_enable = 1;
	}

	client_qp_enable = i_qp_enable | p_qp_enable << 1 | b_qp_enable << 2;
	if (!client_qp_enable)
		return 0;

	if (inst->codec == V4L2_PIX_FMT_H264) {
		i_frame_qp = max(inst->fw_caps[I_FRAME_MIN_QP_H264].value,
				 inst->fw_caps[MIN_FRAME_QP_H264].value);
		p_frame_qp = max(inst->fw_caps[P_FRAME_MIN_QP_H264].value,
				 inst->fw_caps[MIN_FRAME_QP_H264].value);
		b_frame_qp = max(inst->fw_caps[B_FRAME_MIN_QP_H264].value,
				 inst->fw_caps[MIN_FRAME_QP_H264].value);
	} else {
		i_frame_qp = max(inst->fw_caps[I_FRAME_MIN_QP_HEVC].value,
				 inst->fw_caps[MIN_FRAME_QP_HEVC].value);
		p_frame_qp = max(inst->fw_caps[P_FRAME_MIN_QP_HEVC].value,
				 inst->fw_caps[MIN_FRAME_QP_HEVC].value);
		b_frame_qp = max(inst->fw_caps[B_FRAME_MIN_QP_HEVC].value,
				 inst->fw_caps[MIN_FRAME_QP_HEVC].value);
	}

	hfi_val = i_frame_qp | p_frame_qp << 8 | b_frame_qp << 16 | client_qp_enable << 24;

	return hfi_ops->session_set_property(inst, hfi_id,
					 HFI_HOST_FLAGS_NONE,
				     iris_get_port_info(inst, cap_id),
				     HFI_PAYLOAD_32_PACKED,
				     &hfi_val, sizeof(u32));
}

int iris_set_max_qp(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 i_qp_enable = 0, p_qp_enable = 0, b_qp_enable = 0;
	u32 max_qp_enable = 0, client_qp_enable;
	u32 i_frame_qp, p_frame_qp, b_frame_qp;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	u32 hfi_val;

	if (inst->codec == V4L2_PIX_FMT_H264) {
		if (inst->fw_caps[MAX_FRAME_QP_H264].flags & CAP_FLAG_CLIENT_SET)
			max_qp_enable = 1;
		if (max_qp_enable ||
		    (inst->fw_caps[I_FRAME_MAX_QP_H264].flags & CAP_FLAG_CLIENT_SET))
			i_qp_enable = 1;
		if (max_qp_enable ||
		    (inst->fw_caps[P_FRAME_MAX_QP_H264].flags & CAP_FLAG_CLIENT_SET))
			p_qp_enable = 1;
		if (max_qp_enable ||
		    (inst->fw_caps[B_FRAME_MAX_QP_H264].flags & CAP_FLAG_CLIENT_SET))
			b_qp_enable = 1;
	} else {
		if (inst->fw_caps[MAX_FRAME_QP_HEVC].flags & CAP_FLAG_CLIENT_SET)
			max_qp_enable = 1;
		if (max_qp_enable ||
		    (inst->fw_caps[I_FRAME_MAX_QP_HEVC].flags & CAP_FLAG_CLIENT_SET))
			i_qp_enable = 1;
		if (max_qp_enable ||
		    (inst->fw_caps[P_FRAME_MAX_QP_HEVC].flags & CAP_FLAG_CLIENT_SET))
			p_qp_enable = 1;
		if (max_qp_enable ||
		    (inst->fw_caps[B_FRAME_MAX_QP_HEVC].flags & CAP_FLAG_CLIENT_SET))
			b_qp_enable = 1;
	}

	client_qp_enable = i_qp_enable | p_qp_enable << 1 | b_qp_enable << 2;
	if (!client_qp_enable)
		return 0;

	if (inst->codec == V4L2_PIX_FMT_H264) {
		i_frame_qp = min(inst->fw_caps[I_FRAME_MAX_QP_H264].value,
				 inst->fw_caps[MAX_FRAME_QP_H264].value);
		p_frame_qp = min(inst->fw_caps[P_FRAME_MAX_QP_H264].value,
				 inst->fw_caps[MAX_FRAME_QP_H264].value);
		b_frame_qp = min(inst->fw_caps[B_FRAME_MAX_QP_H264].value,
				 inst->fw_caps[MAX_FRAME_QP_H264].value);
	} else {
		i_frame_qp = min(inst->fw_caps[I_FRAME_MAX_QP_HEVC].value,
				 inst->fw_caps[MAX_FRAME_QP_HEVC].value);
		p_frame_qp = min(inst->fw_caps[P_FRAME_MAX_QP_HEVC].value,
				 inst->fw_caps[MAX_FRAME_QP_HEVC].value);
		b_frame_qp = min(inst->fw_caps[B_FRAME_MAX_QP_HEVC].value,
				 inst->fw_caps[MAX_FRAME_QP_HEVC].value);
	}

	hfi_val = i_frame_qp | p_frame_qp << 8 | b_frame_qp << 16 |
		client_qp_enable << 24;

	return hfi_ops->session_set_property(inst, hfi_id,
					 HFI_HOST_FLAGS_NONE,
				     iris_get_port_info(inst, cap_id),
				     HFI_PAYLOAD_32_PACKED,
				     &hfi_val, sizeof(u32));
}

int iris_set_frame_qp(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 i_qp_enable = 0, p_qp_enable = 0, b_qp_enable = 0, client_qp_enable;
	u32 i_frame_qp, p_frame_qp, b_frame_qp;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	struct vb2_queue *q;
	u32 hfi_val;

	q = v4l2_m2m_get_dst_vq(inst->m2m_ctx);
	if (vb2_is_streaming(q)) {
		if (inst->hfi_rc_type != HFI_RC_OFF)
			return 0;
	}

	if (inst->hfi_rc_type == HFI_RC_OFF) {
		i_qp_enable = 1;
		p_qp_enable = 1;
		b_qp_enable = 1;
	} else {
		if (inst->codec == V4L2_PIX_FMT_H264) {
			if (inst->fw_caps[I_FRAME_QP_H264].flags & CAP_FLAG_CLIENT_SET)
				i_qp_enable = 1;
			if (inst->fw_caps[P_FRAME_QP_H264].flags & CAP_FLAG_CLIENT_SET)
				p_qp_enable = 1;
			if (inst->fw_caps[B_FRAME_QP_H264].flags & CAP_FLAG_CLIENT_SET)
				b_qp_enable = 1;
		} else {
			if (inst->fw_caps[I_FRAME_QP_HEVC].flags & CAP_FLAG_CLIENT_SET)
				i_qp_enable = 1;
			if (inst->fw_caps[P_FRAME_QP_HEVC].flags & CAP_FLAG_CLIENT_SET)
				p_qp_enable = 1;
			if (inst->fw_caps[B_FRAME_QP_HEVC].flags & CAP_FLAG_CLIENT_SET)
				b_qp_enable = 1;
		}
	}

	client_qp_enable = i_qp_enable | p_qp_enable << 1 | b_qp_enable << 2;
	if (!client_qp_enable)
		return 0;

	if (inst->codec == V4L2_PIX_FMT_H264) {
		i_frame_qp = inst->fw_caps[I_FRAME_QP_H264].value;
		p_frame_qp = inst->fw_caps[P_FRAME_QP_H264].value;
		b_frame_qp = inst->fw_caps[B_FRAME_QP_H264].value;
	} else {
		i_frame_qp = inst->fw_caps[I_FRAME_QP_HEVC].value;
		p_frame_qp = inst->fw_caps[P_FRAME_QP_HEVC].value;
		b_frame_qp = inst->fw_caps[B_FRAME_QP_HEVC].value;
	}

	hfi_val = i_frame_qp | p_frame_qp << 8 | b_frame_qp << 16 |
		client_qp_enable << 24;

	return hfi_ops->session_set_property(inst, hfi_id,
					 HFI_HOST_FLAGS_NONE,
				     iris_get_port_info(inst, cap_id),
				     HFI_PAYLOAD_32_PACKED,
				     &hfi_val, sizeof(u32));
}

int iris_set_qp_range(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	struct hfi_quantization_range_v2 range;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;

	if (inst->codec == V4L2_PIX_FMT_HEVC) {
		range.min_qp.qp_packed = inst->fw_caps[MIN_FRAME_QP_HEVC].value;
		range.max_qp.qp_packed = inst->fw_caps[MAX_FRAME_QP_HEVC].value;
	} else {
		range.min_qp.qp_packed = inst->fw_caps[MIN_FRAME_QP_H264].value;
		range.max_qp.qp_packed = inst->fw_caps[MAX_FRAME_QP_H264].value;
	}

	return hfi_ops->session_set_property(inst, hfi_id,
					 HFI_HOST_FLAGS_NONE,
				     iris_get_port_info(inst, cap_id),
				     HFI_PAYLOAD_32_PACKED,
				     &range, sizeof(range));
}

int iris_set_rotation(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	u32 hfi_val;

	switch (inst->fw_caps[cap_id].value) {
	case 0:
		hfi_val = HFI_ROTATION_NONE;
		return 0;
	case 90:
		hfi_val = HFI_ROTATION_90;
		break;
	case 180:
		hfi_val = HFI_ROTATION_180;
		break;
	case 270:
		hfi_val = HFI_ROTATION_270;
		break;
	default:
		return -EINVAL;
	}

	return hfi_ops->session_set_property(inst, hfi_id,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_U32,
					     &hfi_val, sizeof(u32));
}

int iris_set_flip(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	u32 hfi_val = HFI_DISABLE_FLIP;

	if (inst->fw_caps[HFLIP].value)
		hfi_val |= HFI_HORIZONTAL_FLIP;

	if (inst->fw_caps[VFLIP].value)
		hfi_val |= HFI_VERTICAL_FLIP;

	return hfi_ops->session_set_property(inst, hfi_id,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_U32_ENUM,
					     &hfi_val, sizeof(u32));
}

int iris_set_ir_period_gen1(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	struct v4l2_pix_format_mplane *fmt = &inst->fmt_dst->fmt.pix_mp;
	u32 codec_align = inst->codec == V4L2_PIX_FMT_HEVC ? 32 : 16;
	u32 ir_period = inst->fw_caps[cap_id].value;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	struct hfi_intra_refresh hfi_val;

	if (!ir_period)
		return -EINVAL;

	if (inst->fw_caps[IR_TYPE].value ==
			V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE_RANDOM) {
		hfi_val.mode = HFI_INTRA_REFRESH_RANDOM;
	} else if (inst->fw_caps[IR_TYPE].value ==
			V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE_CYCLIC) {
		hfi_val.mode = HFI_INTRA_REFRESH_CYCLIC;
	} else {
		return -EINVAL;
	}

	/*
	 * Calculate the number of macroblocks in a frame,
	 * then determine how many macroblocks need to be
	 * refreshed within one ir_period.
	 */
	hfi_val.mbs = (fmt->width / codec_align) * (fmt->height / codec_align);
	hfi_val.mbs /= ir_period;

	return hfi_ops->session_set_property(inst, hfi_id,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_STRUCTURE,
					     &hfi_val, sizeof(hfi_val));
}

int iris_set_ir_period_gen2(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	struct vb2_queue *q = v4l2_m2m_get_dst_vq(inst->m2m_ctx);
	u32 ir_period = inst->fw_caps[cap_id].value;
	u32 ir_type = 0;

	if (inst->fw_caps[IR_TYPE].value ==
			V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE_RANDOM) {
		if (vb2_is_streaming(q))
			return 0;
		ir_type = HFI_PROP_IR_RANDOM_PERIOD;
	} else if (inst->fw_caps[IR_TYPE].value ==
			V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE_CYCLIC) {
		ir_type = HFI_PROP_IR_CYCLIC_PERIOD;
	} else {
		return -EINVAL;
	}

	return hfi_ops->session_set_property(inst, ir_type,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_U32,
					     &ir_period, sizeof(u32));
}

int iris_set_ltr_count_gen1(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 ltr_count = inst->fw_caps[cap_id].value;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	struct hfi_ltr_mode ltr_mode;

	if (!ltr_count)
		return -EINVAL;

	ltr_mode.count = ltr_count;
	ltr_mode.mode = HFI_LTR_MODE_MANUAL;
	ltr_mode.trust_mode = 1;

	return hfi_ops->session_set_property(inst, hfi_id,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_STRUCTURE,
					     &ltr_mode, sizeof(ltr_mode));
}

int iris_set_use_ltr(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	struct vb2_queue *sq = v4l2_m2m_get_src_vq(inst->m2m_ctx);
	struct vb2_queue *dq = v4l2_m2m_get_dst_vq(inst->m2m_ctx);
	u32 ltr_count = inst->fw_caps[LTR_COUNT].value;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	struct hfi_ltr_use ltr_use;

	if (!vb2_is_streaming(sq) && !vb2_is_streaming(dq))
		return -EINVAL;

	if (!ltr_count)
		return -EINVAL;

	ltr_use.ref_ltr = inst->fw_caps[cap_id].value;
	ltr_use.use_constrnt = true;
	ltr_use.frames = 0;

	return hfi_ops->session_set_property(inst, hfi_id,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_STRUCTURE,
					     &ltr_use, sizeof(ltr_use));
}

int iris_set_mark_ltr(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	struct vb2_queue *sq = v4l2_m2m_get_src_vq(inst->m2m_ctx);
	struct vb2_queue *dq = v4l2_m2m_get_dst_vq(inst->m2m_ctx);
	u32 ltr_count = inst->fw_caps[LTR_COUNT].value;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	struct hfi_ltr_mark ltr_mark;

	if (!vb2_is_streaming(sq) && !vb2_is_streaming(dq))
		return -EINVAL;

	if (!ltr_count)
		return -EINVAL;

	ltr_mark.mark_frame = inst->fw_caps[cap_id].value;

	return hfi_ops->session_set_property(inst, hfi_id,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_STRUCTURE,
					     &ltr_mark, sizeof(ltr_mark));
}

int iris_set_ltr_count_gen2(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 ltr_count = inst->fw_caps[cap_id].value;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;

	if (!ltr_count)
		return -EINVAL;

	if (inst->hfi_rc_type == HFI_RC_CBR_VFR ||
	    inst->hfi_rc_type == HFI_RC_CBR_CFR ||
	    inst->hfi_rc_type == HFI_RC_OFF) {
		inst->fw_caps[LTR_COUNT].value = 0;
		return -EINVAL;
	}

	return hfi_ops->session_set_property(inst, hfi_id,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_U32,
					     &ltr_count, sizeof(u32));
}

int iris_set_use_and_mark_ltr(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	struct vb2_queue *sq = v4l2_m2m_get_src_vq(inst->m2m_ctx);
	struct vb2_queue *dq = v4l2_m2m_get_dst_vq(inst->m2m_ctx);
	u32 ltr_count = inst->fw_caps[LTR_COUNT].value;
	u32 hfi_val = inst->fw_caps[cap_id].value;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;

	if (!vb2_is_streaming(sq) && !vb2_is_streaming(dq))
		return -EINVAL;

	if (!ltr_count || hfi_val == INVALID_DEFAULT_MARK_OR_USE_LTR)
		return -EINVAL;

	return hfi_ops->session_set_property(inst, hfi_id,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_U32,
					     &hfi_val, sizeof(u32));
}

int iris_set_intra_period(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 gop_size = inst->fw_caps[GOP_SIZE].value;
	u32 b_frame = inst->fw_caps[B_FRAME].value;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	struct hfi_intra_period intra_period;

	if (!gop_size || b_frame >= gop_size)
		return -EINVAL;

	/*
	 * intra_period represents the length of a GOP, which includes both P-frames
	 * and B-frames. The counts of P-frames and B-frames within a GOP must be
	 * communicated to the firmware.
	 */
	intra_period.pframes = (gop_size - 1) / (b_frame + 1);
	intra_period.bframes = b_frame;

	return hfi_ops->session_set_property(inst, hfi_id,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_STRUCTURE,
					     &intra_period, sizeof(intra_period));
}

int iris_set_layer_type(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 layer_enable = inst->fw_caps[LAYER_ENABLE].value;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	u32 layer_type;

	if (inst->hfi_rc_type == HFI_RATE_CONTROL_CQ ||
	    inst->hfi_rc_type == HFI_RATE_CONTROL_OFF)
		return -EINVAL;

	if (inst->codec == V4L2_PIX_FMT_H264) {
		if (!layer_enable || !inst->fw_caps[LAYER_COUNT_H264].value)
			return -EINVAL;

		if (inst->fw_caps[LAYER_TYPE_H264].value ==
			V4L2_MPEG_VIDEO_H264_HIERARCHICAL_CODING_P) {
			if (inst->hfi_rc_type == HFI_RC_VBR_CFR)
				layer_type = HFI_HIER_P_HYBRID_LTR;
			else
				layer_type = HFI_HIER_P_SLIDING_WINDOW;
		} else if (inst->fw_caps[LAYER_TYPE_H264].value ==
			V4L2_MPEG_VIDEO_H264_HIERARCHICAL_CODING_B) {
			if (inst->hfi_rc_type == HFI_RC_VBR_CFR)
				layer_type = HFI_HIER_B;
			else
				return -EINVAL;
		} else {
			return -EINVAL;
		}
	} else if (inst->codec == V4L2_PIX_FMT_HEVC) {
		if (!inst->fw_caps[LAYER_COUNT_HEVC].value)
			return -EINVAL;

		if (inst->fw_caps[LAYER_TYPE_HEVC].value ==
			V4L2_MPEG_VIDEO_HEVC_HIERARCHICAL_CODING_P) {
			layer_type = HFI_HIER_P_SLIDING_WINDOW;
		} else if (inst->fw_caps[LAYER_TYPE_HEVC].value ==
			V4L2_MPEG_VIDEO_HEVC_HIERARCHICAL_CODING_B) {
			if (inst->hfi_rc_type == HFI_RC_VBR_CFR)
				layer_type = HFI_HIER_B;
			else
				return -EINVAL;
		} else {
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}

	inst->hfi_layer_type = layer_type;

	return hfi_ops->session_set_property(inst, hfi_id,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_U32_ENUM,
					     &layer_type, sizeof(u32));
}

int iris_set_layer_count_gen1(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	struct vb2_queue *sq = v4l2_m2m_get_src_vq(inst->m2m_ctx);
	struct vb2_queue *dq = v4l2_m2m_get_dst_vq(inst->m2m_ctx);
	u32 layer_enable = inst->fw_caps[LAYER_ENABLE].value;
	u32 layer_count = inst->fw_caps[cap_id].value;
	u32 hfi_id, ret;

	if (!layer_enable || !layer_count)
		return -EINVAL;

	inst->hfi_layer_count = layer_count;

	if (!vb2_is_streaming(sq) && !vb2_is_streaming(dq)) {
		hfi_id = HFI_PROPERTY_PARAM_VENC_HIER_P_MAX_NUM_ENH_LAYER;
		ret = hfi_ops->session_set_property(inst, hfi_id,
						    HFI_HOST_FLAGS_NONE,
						    iris_get_port_info(inst, cap_id),
						    HFI_PAYLOAD_U32,
						    &layer_count, sizeof(u32));
		if (ret)
			return ret;
	}

	hfi_id = inst->fw_caps[cap_id].hfi_id;
	return hfi_ops->session_set_property(inst, hfi_id,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_U32,
					     &layer_count, sizeof(u32));
}

int iris_set_layer_count_gen2(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	u32 layer_type = inst->hfi_layer_type;
	u32 layer_count, layer_count_max;

	layer_count = (inst->codec == V4L2_PIX_FMT_H264) ?
		inst->fw_caps[LAYER_COUNT_H264].value :
		inst->fw_caps[LAYER_COUNT_HEVC].value;

	if (!layer_count)
		return -EINVAL;

	if (layer_type == HFI_HIER_B) {
		layer_count_max = MAX_LAYER_HB;
	} else if (layer_type == HFI_HIER_P_HYBRID_LTR) {
		layer_count_max = MAX_AVC_LAYER_HP_HYBRID_LTR;
	} else if (layer_type == HFI_HIER_P_SLIDING_WINDOW) {
		if (inst->codec == V4L2_PIX_FMT_H264) {
			layer_count_max = MAX_AVC_LAYER_HP_SLIDING_WINDOW;
		} else {
			if (inst->hfi_rc_type == HFI_RC_VBR_CFR)
				layer_count_max = MAX_HEVC_VBR_LAYER_HP_SLIDING_WINDOW;
			else
				layer_count_max = MAX_HEVC_LAYER_HP_SLIDING_WINDOW;
		}
	} else {
		return -EINVAL;
	}

	if (layer_count > layer_count_max)
		layer_count = layer_count_max;

	layer_count += 1; /* base layer */
	inst->hfi_layer_count = layer_count;

	return hfi_ops->session_set_property(inst, hfi_id,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_U32,
					     &layer_count, sizeof(u32));
}

int iris_set_layer_bitrate(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	struct vb2_queue *sq = v4l2_m2m_get_src_vq(inst->m2m_ctx);
	struct vb2_queue *dq = v4l2_m2m_get_dst_vq(inst->m2m_ctx);
	u32 hfi_id = inst->fw_caps[cap_id].hfi_id;
	u32 bitrate = inst->fw_caps[cap_id].value;

	/* ignore layer bitrate when total bitrate is set */
	if (inst->fw_caps[BITRATE].flags & CAP_FLAG_CLIENT_SET)
		return 0;

	if (!(inst->fw_caps[cap_id].flags & CAP_FLAG_CLIENT_SET))
		return -EINVAL;

	if (!vb2_is_streaming(sq) && !vb2_is_streaming(dq))
		return -EINVAL;

	return hfi_ops->session_set_property(inst, hfi_id,
					     HFI_HOST_FLAGS_NONE,
					     iris_get_port_info(inst, cap_id),
					     HFI_PAYLOAD_U32,
					     &bitrate, sizeof(u32));
}

int iris_set_properties(struct iris_inst *inst, u32 plane)
{
	const struct iris_hfi_session_ops *hfi_ops = inst->hfi_session_ops;
	struct platform_inst_fw_cap *cap;
	int ret;
	u32 i;

	ret = hfi_ops->session_set_config_params(inst, plane);
	if (ret)
		return ret;

	for (i = 1; i < INST_FW_CAP_MAX; i++) {
		cap = &inst->fw_caps[i];
		if (!iris_valid_cap_id(cap->cap_id))
			continue;

		if (cap->cap_id && cap->set)
			cap->set(inst, i);
	}

	return 0;
}
