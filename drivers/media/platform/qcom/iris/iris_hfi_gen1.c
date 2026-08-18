// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_ctrls.h"
#include "iris_platform_common.h"
#include "iris_hfi_gen1.h"
#include "iris_hfi_gen1_defines.h"
#include "iris_vpu_buffer.h"

#define BITRATE_MIN		32000
#define BITRATE_MAX		160000000
#define BITRATE_STEP		100

static struct platform_inst_fw_cap inst_fw_cap_sm8250_dec[] = {
	{
		.cap_id = PIPE,
		/* .max, .min and .value are set via platform data */
		.step_or_mask = 1,
		.hfi_id = HFI_PROPERTY_PARAM_WORK_ROUTE,
		.set = iris_set_pipe,
	},
	{
		.cap_id = STAGE,
		.min = STAGE_1,
		.max = STAGE_2,
		.step_or_mask = 1,
		.value = STAGE_2,
		.hfi_id = HFI_PROPERTY_PARAM_WORK_MODE,
		.set = iris_set_stage,
	},
};

static const struct platform_inst_fw_cap inst_fw_cap_sm8250_enc[] = {
	{
		.cap_id = STAGE,
		.min = STAGE_1,
		.max = STAGE_2,
		.step_or_mask = 1,
		.value = STAGE_2,
		.hfi_id = HFI_PROPERTY_PARAM_WORK_MODE,
		.set = iris_set_stage,
	},
	{
		.cap_id = PROFILE_H264,
		.min = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE,
		.max = V4L2_MPEG_VIDEO_H264_PROFILE_MULTIVIEW_HIGH,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_MAIN) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_HIGH) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_STEREO_HIGH) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_MULTIVIEW_HIGH),
		.value = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH,
		.hfi_id = HFI_PROPERTY_PARAM_PROFILE_LEVEL_CURRENT,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_profile_level_gen1,
	},
	{
		.cap_id = PROFILE_HEVC,
		.min = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
		.max = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN) |
				BIT(V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_STILL_PICTURE) |
				BIT(V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10),
		.value = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
		.hfi_id = HFI_PROPERTY_PARAM_PROFILE_LEVEL_CURRENT,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_profile_level_gen1,
	},
	{
		.cap_id = LEVEL_H264,
		.min = V4L2_MPEG_VIDEO_H264_LEVEL_1_0,
		.max = V4L2_MPEG_VIDEO_H264_LEVEL_5_1,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_0) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1B) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_1) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_2) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_3) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_2_0) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_2_1) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_2_2) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_3_0) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_3_1) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_3_2) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_4_0) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_4_1) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_4_2) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_5_0) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_5_1),
		.value = V4L2_MPEG_VIDEO_H264_LEVEL_1_0,
		.hfi_id = HFI_PROPERTY_PARAM_PROFILE_LEVEL_CURRENT,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_profile_level_gen1,
	},
	{
		.cap_id = LEVEL_HEVC,
		.min = V4L2_MPEG_VIDEO_HEVC_LEVEL_1,
		.max = V4L2_MPEG_VIDEO_HEVC_LEVEL_6_2,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_1) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_2) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_2_1) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_3) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_3_1) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_4) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_4_1) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_5) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_5_2) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_6) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_6_1) |
				BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_6_2),
		.value = V4L2_MPEG_VIDEO_HEVC_LEVEL_1,
		.hfi_id = HFI_PROPERTY_PARAM_PROFILE_LEVEL_CURRENT,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_profile_level_gen1,
	},
	{
		.cap_id = HEADER_MODE,
		.min = V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE,
		.max = V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE) |
				BIT(V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME),
		.value = V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME,
		.hfi_id = HFI_PROPERTY_CONFIG_VENC_SYNC_FRAME_SEQUENCE_HEADER,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_header_mode_gen1,
	},
	{
		.cap_id = BITRATE,
		.min = BITRATE_MIN,
		.max = BITRATE_MAX,
		.step_or_mask = BITRATE_STEP,
		.value = BITRATE_DEFAULT,
		.hfi_id = HFI_PROPERTY_CONFIG_VENC_TARGET_BITRATE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_bitrate_gen1,
	},
	{
		.cap_id = BITRATE_MODE,
		.min = V4L2_MPEG_VIDEO_BITRATE_MODE_VBR,
		.max = V4L2_MPEG_VIDEO_BITRATE_MODE_CBR,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_BITRATE_MODE_VBR) |
				BIT(V4L2_MPEG_VIDEO_BITRATE_MODE_CBR),
		.value = V4L2_MPEG_VIDEO_BITRATE_MODE_VBR,
		.hfi_id = HFI_PROPERTY_PARAM_VENC_RATE_CONTROL,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_bitrate_mode_gen1,
	},
	{
		.cap_id = FRAME_SKIP_MODE,
		.min = V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_DISABLED,
		.max = V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_BUF_LIMIT,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_DISABLED) |
				BIT(V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_BUF_LIMIT),
		.value = V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_DISABLED,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
	},
	{
		.cap_id = FRAME_RC_ENABLE,
		.min = 0,
		.max = 1,
		.step_or_mask = 1,
		.value = 1,
	},
	{
		.cap_id = GOP_SIZE,
		.min = 0,
		.max = (1 << 16) - 1,
		.step_or_mask = 1,
		.value = 30,
		.set = iris_set_u32
	},
	{
		.cap_id = ENTROPY_MODE,
		.min = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC,
		.max = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC) |
				BIT(V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC),
		.value = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC,
		.hfi_id = HFI_PROPERTY_PARAM_VENC_H264_ENTROPY_CONTROL,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_entropy_mode_gen1,
	},
	{
		.cap_id = MIN_FRAME_QP_H264,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MIN_QP_8BIT,
		.hfi_id = HFI_PROPERTY_PARAM_VENC_SESSION_QP_RANGE_V2,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_qp_range,
	},
	{
		.cap_id = MIN_FRAME_QP_HEVC,
		.min = MIN_QP_8BIT,
		.max = MAX_QP_HEVC,
		.step_or_mask = 1,
		.value = MIN_QP_8BIT,
		.hfi_id = HFI_PROPERTY_PARAM_VENC_SESSION_QP_RANGE_V2,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_qp_range,
	},
	{
		.cap_id = MAX_FRAME_QP_H264,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MAX_QP,
		.hfi_id = HFI_PROPERTY_PARAM_VENC_SESSION_QP_RANGE_V2,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_qp_range,
	},
	{
		.cap_id = MAX_FRAME_QP_HEVC,
		.min = MIN_QP_8BIT,
		.max = MAX_QP_HEVC,
		.step_or_mask = 1,
		.value = MAX_QP_HEVC,
		.hfi_id = HFI_PROPERTY_PARAM_VENC_SESSION_QP_RANGE_V2,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_qp_range,
	},
	{
		.cap_id = IR_TYPE,
		.min = V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE_RANDOM,
		.max = V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE_CYCLIC,
		.step_or_mask = BIT(V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE_RANDOM) |
			BIT(V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE_CYCLIC),
		.value = V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE_RANDOM,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
	},
	{
		.cap_id = IR_PERIOD,
		.min = 0,
		.max = ((4096 * 2304) >> 8),
		.step_or_mask = 1,
		.value = 0,
		.hfi_id = HFI_PROPERTY_PARAM_VENC_INTRA_REFRESH,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_ir_period_gen1,
	},
	{
		.cap_id = LTR_COUNT,
		.min = 0,
		.max = MAX_LTR_FRAME_COUNT_GEN1,
		.step_or_mask = 1,
		.value = 0,
		.hfi_id = HFI_PROPERTY_PARAM_VENC_LTRMODE,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_ltr_count_gen1,
	},
	{
		.cap_id = USE_LTR,
		.min = 0,
		.max = ((1 << MAX_LTR_FRAME_COUNT_GEN1) - 1),
		.step_or_mask = 0,
		.value = 0,
		.hfi_id = HFI_PROPERTY_CONFIG_VENC_USELTRFRAME,
		.flags = CAP_FLAG_INPUT_PORT | CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_use_ltr,
	},
	{
		.cap_id = MARK_LTR,
		.min = 0,
		.max = (MAX_LTR_FRAME_COUNT_GEN1 - 1),
		.step_or_mask = 1,
		.value = 0,
		.hfi_id = HFI_PROPERTY_CONFIG_VENC_MARKLTRFRAME,
		.flags = CAP_FLAG_INPUT_PORT | CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_mark_ltr,
	},
	{
		.cap_id = B_FRAME,
		.min = 0,
		.max = 3,
		.step_or_mask = 1,
		.value = 0,
		.flags = CAP_FLAG_OUTPUT_PORT,
	},
	{
		.cap_id = INTRA_PERIOD,
		.min = 0,
		.max = 1,
		.step_or_mask = 1,
		.value = 0,
		.hfi_id = HFI_PROPERTY_CONFIG_VENC_INTRA_PERIOD,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_intra_period,
	},
	{
		.cap_id = LAYER_ENABLE,
		.min = 0,
		.max = 1,
		.step_or_mask = 1,
		.value = 0,
		.flags = CAP_FLAG_OUTPUT_PORT,
	},
	{
		.cap_id = LAYER_TYPE_H264,
		.min = V4L2_MPEG_VIDEO_H264_HIERARCHICAL_CODING_P,
		.max = V4L2_MPEG_VIDEO_H264_HIERARCHICAL_CODING_P,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_H264_HIERARCHICAL_CODING_P),
		.value = V4L2_MPEG_VIDEO_H264_HIERARCHICAL_CODING_P,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
	},
	{
		.cap_id = LAYER_COUNT_H264,
		.min = 0,
		.max = MAX_HIER_CODING_LAYER_GEN1,
		.step_or_mask = 1,
		.value = 0,
		.hfi_id = HFI_PROPERTY_CONFIG_VENC_HIER_P_ENH_LAYER,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_layer_count_gen1,
	},
	{
		.cap_id = LAYER0_BITRATE_H264,
		.min = 1,
		.max = BITRATE_MAX,
		.step_or_mask = 1,
		.value = BITRATE_DEFAULT,
		.hfi_id = HFI_PROPERTY_CONFIG_VENC_TARGET_BITRATE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_bitrate_gen1,
	},
	{
		.cap_id = LAYER1_BITRATE_H264,
		.min = 1,
		.max = BITRATE_MAX,
		.step_or_mask = 1,
		.value = BITRATE_DEFAULT,
		.hfi_id = HFI_PROPERTY_CONFIG_VENC_TARGET_BITRATE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_bitrate_gen1,
	},
	{
		.cap_id = LAYER2_BITRATE_H264,
		.min = 1,
		.max = BITRATE_MAX,
		.step_or_mask = 1,
		.value = BITRATE_DEFAULT,
		.hfi_id = HFI_PROPERTY_CONFIG_VENC_TARGET_BITRATE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_bitrate_gen1,
	},
	{
		.cap_id = LAYER3_BITRATE_H264,
		.min = 1,
		.max = BITRATE_MAX,
		.step_or_mask = 1,
		.value = BITRATE_DEFAULT,
		.hfi_id = HFI_PROPERTY_CONFIG_VENC_TARGET_BITRATE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_bitrate_gen1,
	},
	{
		.cap_id = LAYER4_BITRATE_H264,
		.min = 1,
		.max = BITRATE_MAX,
		.step_or_mask = 1,
		.value = BITRATE_DEFAULT,
		.hfi_id = HFI_PROPERTY_CONFIG_VENC_TARGET_BITRATE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_bitrate_gen1,
	},
	{
		.cap_id = LAYER5_BITRATE_H264,
		.min = 1,
		.max = BITRATE_MAX,
		.step_or_mask = 1,
		.value = BITRATE_DEFAULT,
		.hfi_id = HFI_PROPERTY_CONFIG_VENC_TARGET_BITRATE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_bitrate_gen1,
	},
};

static const u32 sm8250_vdec_input_config_param_default[] = {
	HFI_PROPERTY_CONFIG_VIDEOCORES_USAGE,
	HFI_PROPERTY_PARAM_UNCOMPRESSED_FORMAT_SELECT,
	HFI_PROPERTY_PARAM_UNCOMPRESSED_PLANE_ACTUAL_CONSTRAINTS_INFO,
	HFI_PROPERTY_PARAM_BUFFER_COUNT_ACTUAL,
	HFI_PROPERTY_PARAM_VDEC_MULTI_STREAM,
	HFI_PROPERTY_PARAM_FRAME_SIZE,
	HFI_PROPERTY_PARAM_BUFFER_SIZE_ACTUAL,
	HFI_PROPERTY_PARAM_BUFFER_ALLOC_MODE,
};

static const u32 sm8250_venc_input_config_param[] = {
	HFI_PROPERTY_CONFIG_FRAME_RATE,
	HFI_PROPERTY_PARAM_UNCOMPRESSED_PLANE_ACTUAL_INFO,
	HFI_PROPERTY_PARAM_FRAME_SIZE,
	HFI_PROPERTY_PARAM_UNCOMPRESSED_FORMAT_SELECT,
	HFI_PROPERTY_PARAM_BUFFER_COUNT_ACTUAL,
};

static const u32 sm8250_dec_ip_int_buf_tbl[] = {
	BUF_BIN,
	BUF_SCRATCH_1,
};

static const u32 sm8250_dec_op_int_buf_tbl[] = {
	BUF_DPB,
};

static const u32 sm8250_enc_ip_int_buf_tbl[] = {
	BUF_BIN,
	BUF_SCRATCH_1,
	BUF_SCRATCH_2,
};

const struct iris_firmware_data iris_hfi_gen1_data = {
	.init_hfi_ops = &iris_hfi_gen1_sys_ops_init,

	.inst_fw_caps_dec = inst_fw_cap_sm8250_dec,
	.inst_fw_caps_dec_size = ARRAY_SIZE(inst_fw_cap_sm8250_dec),
	.inst_fw_caps_enc = inst_fw_cap_sm8250_enc,
	.inst_fw_caps_enc_size = ARRAY_SIZE(inst_fw_cap_sm8250_enc),

	.dec_input_config_params_default =
		sm8250_vdec_input_config_param_default,
	.dec_input_config_params_default_size =
		ARRAY_SIZE(sm8250_vdec_input_config_param_default),
	.enc_input_config_params = sm8250_venc_input_config_param,
	.enc_input_config_params_size =
		ARRAY_SIZE(sm8250_venc_input_config_param),

	.dec_ip_int_buf_tbl = sm8250_dec_ip_int_buf_tbl,
	.dec_ip_int_buf_tbl_size = ARRAY_SIZE(sm8250_dec_ip_int_buf_tbl),
	.dec_op_int_buf_tbl = sm8250_dec_op_int_buf_tbl,
	.dec_op_int_buf_tbl_size = ARRAY_SIZE(sm8250_dec_op_int_buf_tbl),

	.enc_ip_int_buf_tbl = sm8250_enc_ip_int_buf_tbl,
	.enc_ip_int_buf_tbl_size = ARRAY_SIZE(sm8250_enc_ip_int_buf_tbl),
};
