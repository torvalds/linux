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
	struct platform_inst_fw_cap *caps;
	u32 i, num_cap, cap_id;

	caps = core->iris_platform_data->inst_fw_caps_dec;
	num_cap = core->iris_platform_data->inst_fw_caps_dec_size;

	for (i = 0; i < num_cap; i++) {
		cap_id = caps[i].cap_id;
		if (!iris_valid_cap_id(cap_id))
			continue;

		core->inst_fw_caps_dec[cap_id].cap_id = caps[i].cap_id;
		core->inst_fw_caps_dec[cap_id].min = caps[i].min;
		core->inst_fw_caps_dec[cap_id].max = caps[i].max;
		core->inst_fw_caps_dec[cap_id].step_or_mask = caps[i].step_or_mask;
		core->inst_fw_caps_dec[cap_id].value = caps[i].value;
		core->inst_fw_caps_dec[cap_id].flags = caps[i].flags;
		core->inst_fw_caps_dec[cap_id].hfi_id = caps[i].hfi_id;
		core->inst_fw_caps_dec[cap_id].set = caps[i].set;
	}

	caps = core->iris_platform_data->inst_fw_caps_enc;
	num_cap = core->iris_platform_data->inst_fw_caps_enc_size;

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
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
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
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
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
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
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
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
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
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
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
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
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
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
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
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
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
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
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

int iris_set_bitrate(struct iris_inst *inst, enum platform_inst_fw_cap_type cap_id)
{
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
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
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
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
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
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
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
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
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
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
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
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
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
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
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
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
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
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
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
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

int iris_set_properties(struct iris_inst *inst, u32 plane)
{
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
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
