// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2017 Linaro Ltd.
 */
#include <linux/types.h>
#include <media/v4l2-ctrls.h>

#include "core.h"
#include "venc.h"
#include "helpers.h"

#define BITRATE_MIN		32000
#define BITRATE_MAX		160000000
#define BITRATE_DEFAULT		1000000
#define BITRATE_DEFAULT_PEAK	(BITRATE_DEFAULT * 2)
#define BITRATE_STEP		100
#define SLICE_BYTE_SIZE_MAX	1024
#define SLICE_BYTE_SIZE_MIN	1024
#define SLICE_MB_SIZE_MAX	300
#define AT_SLICE_BOUNDARY	\
	V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_DISABLED_AT_SLICE_BOUNDARY
#define MAX_LTR_FRAME_COUNT 4

static int venc_calc_bpframes(u32 gop_size, u32 conseq_b, u32 *bf, u32 *pf)
{
	u32 half = (gop_size - 1) >> 1;
	u32 b, p, ratio;
	bool found = false;

	if (!gop_size)
		return -EINVAL;

	*bf = *pf = 0;

	if (!conseq_b) {
		*pf = gop_size -  1;
		return 0;
	}

	b = p = half;

	for (; b <= gop_size - 1; b++, p--) {
		if (b % p)
			continue;

		ratio = b / p;

		if (ratio == conseq_b) {
			found = true;
			break;
		}

		if (ratio > conseq_b)
			break;
	}

	if (!found)
		return -EINVAL;

	if (b + p + 1 != gop_size)
		return -EINVAL;

	*bf = b;
	*pf = p;

	return 0;
}

static int venc_op_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct venus_inst *inst = ctrl_to_inst(ctrl);
	struct venc_controls *ctr = &inst->controls.enc;
	struct hfi_enable en = { .enable = 1 };
	struct hfi_bitrate brate;
	struct hfi_ltr_use ltr_use;
	struct hfi_ltr_mark ltr_mark;
	u32 bframes;
	u32 ptype;
	int ret;

	switch (ctrl->id) {
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		ctr->bitrate_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		ctr->bitrate = ctrl->val;
		mutex_lock(&inst->lock);
		if (inst->streamon_out && inst->streamon_cap) {
			ptype = HFI_PROPERTY_CONFIG_VENC_TARGET_BITRATE;
			brate.bitrate = ctr->bitrate;
			brate.layer_id = 0;

			ret = hfi_session_set_property(inst, ptype, &brate);
			if (ret) {
				mutex_unlock(&inst->lock);
				return ret;
			}
		}
		mutex_unlock(&inst->lock);
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE_PEAK:
		ctr->bitrate_peak = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE:
		ctr->h264_entropy_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE:
		ctr->profile.mpeg4 = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
		ctr->profile.h264 = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_PROFILE:
		ctr->profile.hevc = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_VP8_PROFILE:
		ctr->profile.vp8 = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL:
		ctr->level.mpeg4 = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
		ctr->level.h264 = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_LEVEL:
		ctr->level.hevc = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP:
		ctr->h264_i_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP:
		ctr->h264_p_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP:
		ctr->h264_b_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_MIN_QP:
		ctr->h264_min_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_I_FRAME_MIN_QP:
		ctr->h264_i_min_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_P_FRAME_MIN_QP:
		ctr->h264_p_min_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_B_FRAME_MIN_QP:
		ctr->h264_b_min_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_MAX_QP:
		ctr->h264_max_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_I_FRAME_MAX_QP:
		ctr->h264_i_max_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_P_FRAME_MAX_QP:
		ctr->h264_p_max_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_B_FRAME_MAX_QP:
		ctr->h264_b_max_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_QP:
		ctr->hevc_i_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_QP:
		ctr->hevc_p_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_QP:
		ctr->hevc_b_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP:
		ctr->hevc_min_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_MIN_QP:
		ctr->hevc_i_min_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_MIN_QP:
		ctr->hevc_p_min_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_MIN_QP:
		ctr->hevc_b_min_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP:
		ctr->hevc_max_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_MAX_QP:
		ctr->hevc_i_max_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_MAX_QP:
		ctr->hevc_p_max_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_MAX_QP:
		ctr->hevc_b_max_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE:
		ctr->multi_slice_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_BYTES:
		ctr->multi_slice_max_bytes = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB:
		ctr->multi_slice_max_mb = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_ALPHA:
		ctr->h264_loop_filter_alpha = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_BETA:
		ctr->h264_loop_filter_beta = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE:
		ctr->h264_loop_filter_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEADER_MODE:
		ctr->header_mode = ctrl->val;
		mutex_lock(&inst->lock);
		if (inst->streamon_out && inst->streamon_cap) {
			if (ctrl->val == V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE)
				en.enable = 0;
			else
				en.enable = 1;
			ptype = HFI_PROPERTY_CONFIG_VENC_SYNC_FRAME_SEQUENCE_HEADER;
			ret = hfi_session_set_property(inst, ptype, &en);
			if (ret) {
				mutex_unlock(&inst->lock);
				return ret;
			}
		}
		mutex_unlock(&inst->lock);
		break;
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		ret = venc_calc_bpframes(ctrl->val, ctr->num_b_frames, &bframes,
					 &ctr->num_p_frames);
		if (ret)
			return ret;

		ctr->gop_size = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_I_PERIOD:
		ctr->h264_i_period = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_VPX_MIN_QP:
		ctr->vp8_min_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_VPX_MAX_QP:
		ctr->vp8_max_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_B_FRAMES:
		ret = venc_calc_bpframes(ctr->gop_size, ctrl->val, &bframes,
					 &ctr->num_p_frames);
		if (ret)
			return ret;

		ctr->num_b_frames = bframes;
		break;
	case V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME:
		mutex_lock(&inst->lock);
		if (inst->streamon_out && inst->streamon_cap) {
			ptype = HFI_PROPERTY_CONFIG_VENC_REQUEST_SYNC_FRAME;
			ret = hfi_session_set_property(inst, ptype, &en);

			if (ret) {
				mutex_unlock(&inst->lock);
				return ret;
			}
		}
		mutex_unlock(&inst->lock);
		break;
	case V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE:
		ctr->rc_enable = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_CONSTANT_QUALITY:
		ctr->const_quality = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_FRAME_SKIP_MODE:
		ctr->frame_skip_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_BASELAYER_PRIORITY_ID:
		ctr->base_priority_id = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_AU_DELIMITER:
		ctr->aud_enable = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_LTR_COUNT:
		ctr->ltr_count = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_FRAME_LTR_INDEX:
		mutex_lock(&inst->lock);
		if (inst->streamon_out && inst->streamon_cap) {
			ptype = HFI_PROPERTY_CONFIG_VENC_MARKLTRFRAME;
			ltr_mark.mark_frame = ctrl->val;
			ret = hfi_session_set_property(inst, ptype, &ltr_mark);
			if (ret) {
				mutex_unlock(&inst->lock);
				return ret;
			}
		}
		mutex_unlock(&inst->lock);
		break;
	case V4L2_CID_MPEG_VIDEO_USE_LTR_FRAMES:
		mutex_lock(&inst->lock);
		if (inst->streamon_out && inst->streamon_cap) {
			ptype = HFI_PROPERTY_CONFIG_VENC_USELTRFRAME;
			ltr_use.ref_ltr = ctrl->val;
			ltr_use.use_constrnt = true;
			ltr_use.frames = 0;
			ret = hfi_session_set_property(inst, ptype, &ltr_use);
			if (ret) {
				mutex_unlock(&inst->lock);
				return ret;
			}
		}
		mutex_unlock(&inst->lock);
		break;
	case V4L2_CID_COLORIMETRY_HDR10_CLL_INFO:
		ctr->cll = *ctrl->p_new.p_hdr10_cll;
		break;
	case V4L2_CID_COLORIMETRY_HDR10_MASTERING_DISPLAY:
		ctr->mastering = *ctrl->p_new.p_hdr10_mastering;
		break;
	case V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE:
		ctr->intra_refresh_type = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD:
		ctr->intra_refresh_period = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM:
		if (ctr->profile.h264 != V4L2_MPEG_VIDEO_H264_PROFILE_HIGH &&
		    ctr->profile.h264 != V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH)
			return -EINVAL;

		/*
		 * In video firmware, 8x8 transform is supported only for
		 * high profile(HP) and constrained high profile(CHP).
		 * If client wants to disable 8x8 transform for HP/CHP,
		 * it is better to set profile as main profile(MP).
		 * Because there is no difference between HP and MP
		 * if we disable 8x8 transform for HP.
		 */


		ctr->h264_8x8_transform = ctrl->val;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int venc_op_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct venus_inst *inst = ctrl_to_inst(ctrl);
	struct hfi_buffer_requirements bufreq;
	enum hfi_version ver = inst->core->res->hfi_version;
	int ret;

	switch (ctrl->id) {
	case V4L2_CID_MIN_BUFFERS_FOR_OUTPUT:
		ret = venus_helper_get_bufreq(inst, HFI_BUFFER_INPUT, &bufreq);
		if (!ret)
			ctrl->val = HFI_BUFREQ_COUNT_MIN(&bufreq, ver);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops venc_ctrl_ops = {
	.s_ctrl = venc_op_s_ctrl,
	.g_volatile_ctrl = venc_op_g_volatile_ctrl,
};

int venc_ctrl_init(struct venus_inst *inst)
{
	int ret;
	struct v4l2_ctrl_hdr10_mastering_display p_hdr10_mastering = {
		{ 34000, 13250, 7500 },
		{ 16000, 34500, 3000 }, 15635, 16450, 10000000, 500,
	};
	struct v4l2_ctrl_hdr10_cll_info p_hdr10_cll = { 1000, 400 };

	ret = v4l2_ctrl_handler_init(&inst->ctrl_handler, 59);
	if (ret)
		return ret;

	v4l2_ctrl_new_std_menu(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_BITRATE_MODE,
		V4L2_MPEG_VIDEO_BITRATE_MODE_CBR,
		~((1 << V4L2_MPEG_VIDEO_BITRATE_MODE_VBR) |
		  (1 << V4L2_MPEG_VIDEO_BITRATE_MODE_CBR) |
		  (1 << V4L2_MPEG_VIDEO_BITRATE_MODE_CQ)),
		V4L2_MPEG_VIDEO_BITRATE_MODE_VBR);

	v4l2_ctrl_new_std_menu(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE,
		V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC,
		0, V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC);

	v4l2_ctrl_new_std_menu(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE,
		V4L2_MPEG_VIDEO_MPEG4_PROFILE_ADVANCED_CODING_EFFICIENCY,
		~((1 << V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE) |
		  (1 << V4L2_MPEG_VIDEO_MPEG4_PROFILE_ADVANCED_SIMPLE)),
		V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE);

	v4l2_ctrl_new_std_menu(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL,
		V4L2_MPEG_VIDEO_MPEG4_LEVEL_5,
		0, V4L2_MPEG_VIDEO_MPEG4_LEVEL_0);

	v4l2_ctrl_new_std_menu(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_HEVC_PROFILE,
		V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10,
		~((1 << V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN) |
		  (1 << V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_STILL_PICTURE) |
		  (1 << V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10)),
		V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN);

	v4l2_ctrl_new_std_menu(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_HEVC_LEVEL,
		V4L2_MPEG_VIDEO_HEVC_LEVEL_6_2,
		0, V4L2_MPEG_VIDEO_HEVC_LEVEL_1);

	v4l2_ctrl_new_std_menu(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_PROFILE,
		V4L2_MPEG_VIDEO_H264_PROFILE_MULTIVIEW_HIGH,
		~((1 << V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE) |
		  (1 << V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE) |
		  (1 << V4L2_MPEG_VIDEO_H264_PROFILE_MAIN) |
		  (1 << V4L2_MPEG_VIDEO_H264_PROFILE_HIGH) |
		  (1 << V4L2_MPEG_VIDEO_H264_PROFILE_STEREO_HIGH) |
		  (1 << V4L2_MPEG_VIDEO_H264_PROFILE_MULTIVIEW_HIGH)),
		V4L2_MPEG_VIDEO_H264_PROFILE_HIGH);

	v4l2_ctrl_new_std_menu(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_LEVEL,
		V4L2_MPEG_VIDEO_H264_LEVEL_5_1,
		0, V4L2_MPEG_VIDEO_H264_LEVEL_1_0);

	v4l2_ctrl_new_std_menu(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE,
		AT_SLICE_BOUNDARY,
		0, V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_DISABLED);

	v4l2_ctrl_new_std_menu(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_HEADER_MODE,
		V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME,
		~((1 << V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE) |
		(1 << V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME)),
		V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME);

	v4l2_ctrl_new_std_menu(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE,
		V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_MAX_BYTES,
		0, V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_SINGLE);

	v4l2_ctrl_new_std_menu(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_VP8_PROFILE,
		V4L2_MPEG_VIDEO_VP8_PROFILE_3,
		0, V4L2_MPEG_VIDEO_VP8_PROFILE_0);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MIN_BUFFERS_FOR_OUTPUT, 4, 11, 1, 4);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_BITRATE, BITRATE_MIN, BITRATE_MAX,
		BITRATE_STEP, BITRATE_DEFAULT);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_BITRATE_PEAK, BITRATE_MIN, BITRATE_MAX,
		BITRATE_STEP, BITRATE_DEFAULT_PEAK);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP, 1, 51, 1, 26);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP, 1, 51, 1, 28);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP, 1, 51, 1, 30);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_MIN_QP, 1, 51, 1, 1);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_I_FRAME_MIN_QP, 1, 51, 1, 1);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM, 0, 1, 1, 1);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_P_FRAME_MIN_QP, 1, 51, 1, 1);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_B_FRAME_MIN_QP, 1, 51, 1, 1);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_MAX_QP, 1, 51, 1, 51);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_I_FRAME_MAX_QP, 1, 51, 1, 51);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_P_FRAME_MAX_QP, 1, 51, 1, 51);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_B_FRAME_MAX_QP, 1, 51, 1, 51);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_QP, 1, 63, 1, 26);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_QP, 1, 63, 1, 28);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_QP, 1, 63, 1, 30);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP, 1, 63, 1, 1);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_MIN_QP, 1, 63, 1, 1);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_MIN_QP, 1, 63, 1, 1);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_MIN_QP, 1, 63, 1, 1);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP, 1, 63, 1, 63);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_MAX_QP, 1, 63, 1, 63);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_MAX_QP, 1, 63, 1, 63);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_MAX_QP, 1, 63, 1, 63);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_BYTES, SLICE_BYTE_SIZE_MIN,
		SLICE_BYTE_SIZE_MAX, 1, SLICE_BYTE_SIZE_MIN);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB, 1,
		SLICE_MB_SIZE_MAX, 1, 1);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_ALPHA, -6, 6, 1, 0);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_BETA, -6, 6, 1, 0);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_GOP_SIZE, 0, (1 << 16) - 1, 1, 30);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_VPX_MIN_QP, 1, 128, 1, 1);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_VPX_MAX_QP, 1, 128, 1, 128);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_B_FRAMES, 0, 4, 1, 0);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_I_PERIOD, 0, (1 << 16) - 1, 1, 0);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME, 0, 0, 0, 0);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE, 0, 1, 1, 1);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_CONSTANT_QUALITY, 0, 100, 1, 0);

	v4l2_ctrl_new_std_menu(&inst->ctrl_handler, &venc_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_FRAME_SKIP_MODE,
			       V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_BUF_LIMIT,
			       ~((1 << V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_DISABLED) |
			       (1 << V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_BUF_LIMIT)),
			       V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_DISABLED);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_BASELAYER_PRIORITY_ID, 0,
			  6, 1, 0);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_AU_DELIMITER, 0, 1, 1, 0);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_USE_LTR_FRAMES, 0,
			  ((1 << MAX_LTR_FRAME_COUNT) - 1), 0, 0);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_LTR_COUNT, 0,
			  MAX_LTR_FRAME_COUNT, 1, 0);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_FRAME_LTR_INDEX, 0,
			  (MAX_LTR_FRAME_COUNT - 1), 1, 0);

	v4l2_ctrl_new_std_compound(&inst->ctrl_handler, &venc_ctrl_ops,
				   V4L2_CID_COLORIMETRY_HDR10_CLL_INFO,
				   v4l2_ctrl_ptr_create(&p_hdr10_cll));

	v4l2_ctrl_new_std_compound(&inst->ctrl_handler, &venc_ctrl_ops,
				   V4L2_CID_COLORIMETRY_HDR10_MASTERING_DISPLAY,
				   v4l2_ctrl_ptr_create((void *)&p_hdr10_mastering));

	v4l2_ctrl_new_std_menu(&inst->ctrl_handler, &venc_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE,
			       V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE_CYCLIC,
			       0, V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE_RANDOM);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &venc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD, 0,
			  ((4096 * 2304) >> 8), 1, 0);

	ret = inst->ctrl_handler.error;
	if (ret)
		goto err;

	ret = v4l2_ctrl_handler_setup(&inst->ctrl_handler);
	if (ret)
		goto err;

	return 0;
err:
	v4l2_ctrl_handler_free(&inst->ctrl_handler);
	return ret;
}

void venc_ctrl_deinit(struct venus_inst *inst)
{
	v4l2_ctrl_handler_free(&inst->ctrl_handler);
}
