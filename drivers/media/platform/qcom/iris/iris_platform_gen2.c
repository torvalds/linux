// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2025 Linaro Ltd
 */

#include "iris_core.h"
#include "iris_ctrls.h"
#include "iris_hfi_gen2.h"
#include "iris_hfi_gen2_defines.h"
#include "iris_platform_common.h"
#include "iris_vpu_buffer.h"
#include "iris_vpu_common.h"

#include "iris_platform_qcs8300.h"
#include "iris_platform_sm8650.h"
#include "iris_platform_sm8750.h"

#define VIDEO_ARCH_LX 1
#define BITRATE_MAX				245000000

static struct platform_inst_fw_cap inst_fw_cap_sm8550_dec[] = {
	{
		.cap_id = PROFILE_H264,
		.min = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE,
		.max = V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_MAIN) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_HIGH) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH),
		.value = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH,
		.hfi_id = HFI_PROP_PROFILE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_u32_enum,
	},
	{
		.cap_id = PROFILE_HEVC,
		.min = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
		.max = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_STILL_PICTURE,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN) |
				BIT(V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_STILL_PICTURE),
		.value = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
		.hfi_id = HFI_PROP_PROFILE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_u32_enum,
	},
	{
		.cap_id = PROFILE_VP9,
		.min = V4L2_MPEG_VIDEO_VP9_PROFILE_0,
		.max = V4L2_MPEG_VIDEO_VP9_PROFILE_2,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_VP9_PROFILE_0) |
				BIT(V4L2_MPEG_VIDEO_VP9_PROFILE_2),
		.value = V4L2_MPEG_VIDEO_VP9_PROFILE_0,
		.hfi_id = HFI_PROP_PROFILE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_u32_enum,
	},
	{
		.cap_id = LEVEL_H264,
		.min = V4L2_MPEG_VIDEO_H264_LEVEL_1_0,
		.max = V4L2_MPEG_VIDEO_H264_LEVEL_6_2,
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
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_5_1) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_5_2) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_6_0) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_6_1) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_6_2),
		.value = V4L2_MPEG_VIDEO_H264_LEVEL_6_1,
		.hfi_id = HFI_PROP_LEVEL,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_u32_enum,
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
		.value = V4L2_MPEG_VIDEO_HEVC_LEVEL_6_1,
		.hfi_id = HFI_PROP_LEVEL,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_u32_enum,
	},
	{
		.cap_id = LEVEL_VP9,
		.min = V4L2_MPEG_VIDEO_VP9_LEVEL_1_0,
		.max = V4L2_MPEG_VIDEO_VP9_LEVEL_6_0,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_1_0) |
				BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_1_1) |
				BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_2_0) |
				BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_2_1) |
				BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_3_0) |
				BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_3_1) |
				BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_4_0) |
				BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_4_1) |
				BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_5_0) |
				BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_5_1) |
				BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_5_2) |
				BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_6_0),
		.value = V4L2_MPEG_VIDEO_VP9_LEVEL_6_0,
		.hfi_id = HFI_PROP_LEVEL,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_u32_enum,
	},
	{
		.cap_id = TIER,
		.min = V4L2_MPEG_VIDEO_HEVC_TIER_MAIN,
		.max = V4L2_MPEG_VIDEO_HEVC_TIER_HIGH,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_HEVC_TIER_MAIN) |
				BIT(V4L2_MPEG_VIDEO_HEVC_TIER_HIGH),
		.value = V4L2_MPEG_VIDEO_HEVC_TIER_HIGH,
		.hfi_id = HFI_PROP_TIER,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_u32_enum,
	},
	{
		.cap_id = INPUT_BUF_HOST_MAX_COUNT,
		.min = DEFAULT_MAX_HOST_BUF_COUNT,
		.max = DEFAULT_MAX_HOST_BURST_BUF_COUNT,
		.step_or_mask = 1,
		.value = DEFAULT_MAX_HOST_BUF_COUNT,
		.hfi_id = HFI_PROP_BUFFER_HOST_MAX_COUNT,
		.flags = CAP_FLAG_INPUT_PORT,
		.set = iris_set_u32,
	},
	{
		.cap_id = STAGE,
		.min = STAGE_1,
		.max = STAGE_2,
		.step_or_mask = 1,
		.value = STAGE_2,
		.hfi_id = HFI_PROP_STAGE,
		.set = iris_set_stage,
	},
	{
		.cap_id = PIPE,
		.min = PIPE_1,
		.max = PIPE_4,
		.step_or_mask = 1,
		.value = PIPE_4,
		.hfi_id = HFI_PROP_PIPE,
		.set = iris_set_pipe,
	},
	{
		.cap_id = POC,
		.min = 0,
		.max = 2,
		.step_or_mask = 1,
		.value = 1,
		.hfi_id = HFI_PROP_PIC_ORDER_CNT_TYPE,
	},
	{
		.cap_id = CODED_FRAMES,
		.min = CODED_FRAMES_PROGRESSIVE,
		.max = CODED_FRAMES_PROGRESSIVE,
		.step_or_mask = 0,
		.value = CODED_FRAMES_PROGRESSIVE,
		.hfi_id = HFI_PROP_CODED_FRAMES,
	},
	{
		.cap_id = BIT_DEPTH,
		.min = BIT_DEPTH_8,
		.max = BIT_DEPTH_8,
		.step_or_mask = 1,
		.value = BIT_DEPTH_8,
		.hfi_id = HFI_PROP_LUMA_CHROMA_BIT_DEPTH,
	},
	{
		.cap_id = RAP_FRAME,
		.min = 0,
		.max = 1,
		.step_or_mask = 1,
		.value = 1,
		.hfi_id = HFI_PROP_DEC_START_FROM_RAP_FRAME,
		.flags = CAP_FLAG_INPUT_PORT,
		.set = iris_set_u32,
	},
};

static struct platform_inst_fw_cap inst_fw_cap_sm8550_enc[] = {
	{
		.cap_id = PROFILE_H264,
		.min = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE,
		.max = V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_MAIN) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_HIGH),
		.value = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH,
		.hfi_id = HFI_PROP_PROFILE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_profile,
	},
	{
		.cap_id = PROFILE_HEVC,
		.min = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
		.max = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN) |
				BIT(V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_STILL_PICTURE) |
				BIT(V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10),
		.value = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
		.hfi_id = HFI_PROP_PROFILE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_profile,
	},
	{
		.cap_id = LEVEL_H264,
		.min = V4L2_MPEG_VIDEO_H264_LEVEL_1_0,
		.max = V4L2_MPEG_VIDEO_H264_LEVEL_6_0,
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
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_5_1) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_5_2) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_6_0),
		.value = V4L2_MPEG_VIDEO_H264_LEVEL_5_0,
		.hfi_id = HFI_PROP_LEVEL,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_level,
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
		.value = V4L2_MPEG_VIDEO_HEVC_LEVEL_5,
		.hfi_id = HFI_PROP_LEVEL,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_level,
	},
	{
		.cap_id = STAGE,
		.min = STAGE_1,
		.max = STAGE_2,
		.step_or_mask = 1,
		.value = STAGE_2,
		.hfi_id = HFI_PROP_STAGE,
		.set = iris_set_stage,
	},
	{
		.cap_id = HEADER_MODE,
		.min = V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE,
		.max = V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE) |
				BIT(V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME),
		.value = V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME,
		.hfi_id = HFI_PROP_SEQ_HEADER_MODE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_header_mode_gen2,
	},
	{
		.cap_id = PREPEND_SPSPPS_TO_IDR,
		.min = 0,
		.max = 1,
		.step_or_mask = 1,
		.value = 0,
	},
	{
		.cap_id = BITRATE,
		.min = 1,
		.max = BITRATE_MAX,
		.step_or_mask = 1,
		.value = BITRATE_DEFAULT,
		.hfi_id = HFI_PROP_TOTAL_BITRATE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_bitrate,
	},
	{
		.cap_id = BITRATE_PEAK,
		.min = 1,
		.max = BITRATE_MAX,
		.step_or_mask = 1,
		.value = BITRATE_DEFAULT,
		.hfi_id = HFI_PROP_TOTAL_PEAK_BITRATE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_peak_bitrate,
	},
	{
		.cap_id = BITRATE_MODE,
		.min = V4L2_MPEG_VIDEO_BITRATE_MODE_VBR,
		.max = V4L2_MPEG_VIDEO_BITRATE_MODE_CBR,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_BITRATE_MODE_VBR) |
				BIT(V4L2_MPEG_VIDEO_BITRATE_MODE_CBR),
		.value = V4L2_MPEG_VIDEO_BITRATE_MODE_VBR,
		.hfi_id = HFI_PROP_RATE_CONTROL,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_bitrate_mode_gen2,
	},
	{
		.cap_id = FRAME_SKIP_MODE,
		.min = V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_DISABLED,
		.max = V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_BUF_LIMIT,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_DISABLED) |
				BIT(V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_LEVEL_LIMIT) |
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
		.max = INT_MAX,
		.step_or_mask = 1,
		.value = 2 * DEFAULT_FPS - 1,
		.hfi_id = HFI_PROP_MAX_GOP_FRAMES,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_u32,
	},
	{
		.cap_id = ENTROPY_MODE,
		.min = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC,
		.max = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC) |
				BIT(V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC),
		.value = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC,
		.hfi_id = HFI_PROP_CABAC_SESSION,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_entropy_mode_gen2,
	},
	{
		.cap_id = MIN_FRAME_QP_H264,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MIN_QP_8BIT,
		.hfi_id = HFI_PROP_MIN_QP_PACKED,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_min_qp,
	},
	{
		.cap_id = MIN_FRAME_QP_HEVC,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MIN_QP_8BIT,
		.hfi_id = HFI_PROP_MIN_QP_PACKED,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_min_qp,
	},
	{
		.cap_id = MAX_FRAME_QP_H264,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MAX_QP,
		.hfi_id = HFI_PROP_MAX_QP_PACKED,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_max_qp,
	},
	{
		.cap_id = MAX_FRAME_QP_HEVC,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MAX_QP,
		.hfi_id = HFI_PROP_MAX_QP_PACKED,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_max_qp,
	},
	{
		.cap_id = I_FRAME_MIN_QP_H264,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MIN_QP_8BIT,
	},
	{
		.cap_id = I_FRAME_MIN_QP_HEVC,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MIN_QP_8BIT,
	},
	{
		.cap_id = P_FRAME_MIN_QP_H264,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MIN_QP_8BIT,
	},
	{
		.cap_id = P_FRAME_MIN_QP_HEVC,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MIN_QP_8BIT,
	},
	{
		.cap_id = B_FRAME_MIN_QP_H264,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MIN_QP_8BIT,
	},
	{
		.cap_id = B_FRAME_MIN_QP_HEVC,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MIN_QP_8BIT,
	},
	{
		.cap_id = I_FRAME_MAX_QP_H264,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MAX_QP,
	},
	{
		.cap_id = I_FRAME_MAX_QP_HEVC,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MAX_QP,
	},
	{
		.cap_id = P_FRAME_MAX_QP_H264,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MAX_QP,
	},
	{
		.cap_id = P_FRAME_MAX_QP_HEVC,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MAX_QP,
	},
	{
		.cap_id = B_FRAME_MAX_QP_H264,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MAX_QP,
	},
	{
		.cap_id = B_FRAME_MAX_QP_HEVC,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MAX_QP,
	},
	{
		.cap_id = I_FRAME_QP_H264,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = DEFAULT_QP,
		.hfi_id = HFI_PROP_QP_PACKED,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_frame_qp,
	},
	{
		.cap_id = I_FRAME_QP_HEVC,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = DEFAULT_QP,
		.hfi_id = HFI_PROP_QP_PACKED,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_frame_qp,
	},
	{
		.cap_id = P_FRAME_QP_H264,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = DEFAULT_QP,
		.hfi_id = HFI_PROP_QP_PACKED,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_frame_qp,
	},
	{
		.cap_id = P_FRAME_QP_HEVC,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = DEFAULT_QP,
		.hfi_id = HFI_PROP_QP_PACKED,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_frame_qp,
	},
	{
		.cap_id = B_FRAME_QP_H264,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = DEFAULT_QP,
		.hfi_id = HFI_PROP_QP_PACKED,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_frame_qp,
	},
	{
		.cap_id = B_FRAME_QP_HEVC,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = DEFAULT_QP,
		.hfi_id = HFI_PROP_QP_PACKED,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_frame_qp,
	},
	{
		.cap_id = INPUT_BUF_HOST_MAX_COUNT,
		.min = DEFAULT_MAX_HOST_BUF_COUNT,
		.max = DEFAULT_MAX_HOST_BURST_BUF_COUNT,
		.step_or_mask = 1,
		.value = DEFAULT_MAX_HOST_BUF_COUNT,
		.hfi_id = HFI_PROP_BUFFER_HOST_MAX_COUNT,
		.flags = CAP_FLAG_INPUT_PORT,
		.set = iris_set_u32,
	},
	{
		.cap_id = OUTPUT_BUF_HOST_MAX_COUNT,
		.min = DEFAULT_MAX_HOST_BUF_COUNT,
		.max = DEFAULT_MAX_HOST_BURST_BUF_COUNT,
		.step_or_mask = 1,
		.value = DEFAULT_MAX_HOST_BUF_COUNT,
		.hfi_id = HFI_PROP_BUFFER_HOST_MAX_COUNT,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_u32,
	},
};

static struct platform_inst_caps platform_inst_cap_sm8550 = {
	.min_frame_width = 96,
	.max_frame_width = 8192,
	.min_frame_height = 96,
	.max_frame_height = 8192,
	.max_mbpf = (8192 * 4352) / 256,
	.mb_cycles_vpp = 200,
	.mb_cycles_fw = 489583,
	.mb_cycles_fw_vpp = 66234,
	.num_comv = 0,
	.max_frame_rate = MAXIMUM_FPS,
	.max_operating_rate = MAXIMUM_FPS,
};

static void iris_set_sm8550_preset_registers(struct iris_core *core)
{
	writel(0x0, core->reg_base + 0xB0088);
}

static const struct icc_info sm8550_icc_table[] = {
	{ "cpu-cfg",    1000, 1000     },
	{ "video-mem",  1000, 15000000 },
};

static const char * const sm8550_clk_reset_table[] = { "bus" };

static const struct bw_info sm8550_bw_table_dec[] = {
	{ ((4096 * 2160) / 256) * 60, 1608000 },
	{ ((4096 * 2160) / 256) * 30,  826000 },
	{ ((1920 * 1080) / 256) * 60,  567000 },
	{ ((1920 * 1080) / 256) * 30,  294000 },
};

static const char * const sm8550_pmdomain_table[] = { "venus", "vcodec0" };

static const char * const sm8550_opp_pd_table[] = { "mxc", "mmcx" };

static const struct platform_clk_data sm8550_clk_table[] = {
	{IRIS_AXI_CLK,  "iface"        },
	{IRIS_CTRL_CLK, "core"         },
	{IRIS_HW_CLK,   "vcodec0_core" },
};

static struct ubwc_config_data ubwc_config_sm8550 = {
	.max_channels = 8,
	.mal_length = 32,
	.highest_bank_bit = 16,
	.bank_swzl_level = 0,
	.bank_swz2_level = 1,
	.bank_swz3_level = 1,
	.bank_spreading = 1,
};

static struct tz_cp_config tz_cp_config_sm8550 = {
	.cp_start = 0,
	.cp_size = 0x25800000,
	.cp_nonpixel_start = 0x01000000,
	.cp_nonpixel_size = 0x24800000,
};

static const u32 sm8550_vdec_input_config_params_default[] = {
	HFI_PROP_BITSTREAM_RESOLUTION,
	HFI_PROP_CROP_OFFSETS,
	HFI_PROP_LUMA_CHROMA_BIT_DEPTH,
	HFI_PROP_CODED_FRAMES,
	HFI_PROP_BUFFER_FW_MIN_OUTPUT_COUNT,
	HFI_PROP_PIC_ORDER_CNT_TYPE,
	HFI_PROP_PROFILE,
	HFI_PROP_LEVEL,
	HFI_PROP_SIGNAL_COLOR_INFO,
};

static const u32 sm8550_vdec_input_config_param_hevc[] = {
	HFI_PROP_BITSTREAM_RESOLUTION,
	HFI_PROP_CROP_OFFSETS,
	HFI_PROP_LUMA_CHROMA_BIT_DEPTH,
	HFI_PROP_BUFFER_FW_MIN_OUTPUT_COUNT,
	HFI_PROP_PROFILE,
	HFI_PROP_LEVEL,
	HFI_PROP_TIER,
	HFI_PROP_SIGNAL_COLOR_INFO,
};

static const u32 sm8550_vdec_input_config_param_vp9[] = {
	HFI_PROP_BITSTREAM_RESOLUTION,
	HFI_PROP_CROP_OFFSETS,
	HFI_PROP_LUMA_CHROMA_BIT_DEPTH,
	HFI_PROP_BUFFER_FW_MIN_OUTPUT_COUNT,
	HFI_PROP_PROFILE,
	HFI_PROP_LEVEL,
};

static const u32 sm8550_venc_input_config_params[] = {
	HFI_PROP_COLOR_FORMAT,
	HFI_PROP_RAW_RESOLUTION,
	HFI_PROP_CROP_OFFSETS,
	HFI_PROP_LINEAR_STRIDE_SCANLINE,
	HFI_PROP_SIGNAL_COLOR_INFO,
};

static const u32 sm8550_vdec_output_config_params[] = {
	HFI_PROP_COLOR_FORMAT,
	HFI_PROP_LINEAR_STRIDE_SCANLINE,
};

static const u32 sm8550_venc_output_config_params[] = {
	HFI_PROP_BITSTREAM_RESOLUTION,
	HFI_PROP_CROP_OFFSETS,
	HFI_PROP_FRAME_RATE,
};

static const u32 sm8550_vdec_subscribe_input_properties[] = {
	HFI_PROP_NO_OUTPUT,
};

static const u32 sm8550_vdec_subscribe_output_properties_avc[] = {
	HFI_PROP_PICTURE_TYPE,
	HFI_PROP_CABAC_SESSION,
};

static const u32 sm8550_vdec_subscribe_output_properties_hevc[] = {
	HFI_PROP_PICTURE_TYPE,
};

static const u32 sm8550_vdec_subscribe_output_properties_vp9[] = {
	HFI_PROP_PICTURE_TYPE,
};

static const u32 sm8550_dec_ip_int_buf_tbl[] = {
	BUF_BIN,
	BUF_COMV,
	BUF_NON_COMV,
	BUF_LINE,
};

static const u32 sm8550_dec_op_int_buf_tbl[] = {
	BUF_DPB,
};

static const u32 sm8550_enc_op_int_buf_tbl[] = {
	BUF_BIN,
	BUF_COMV,
	BUF_NON_COMV,
	BUF_LINE,
	BUF_SCRATCH_2,
};

struct iris_platform_data sm8550_data = {
	.get_instance = iris_hfi_gen2_get_instance,
	.init_hfi_command_ops = iris_hfi_gen2_command_ops_init,
	.init_hfi_response_ops = iris_hfi_gen2_response_ops_init,
	.get_vpu_buffer_size = iris_vpu_buf_size,
	.vpu_ops = &iris_vpu3_ops,
	.set_preset_registers = iris_set_sm8550_preset_registers,
	.icc_tbl = sm8550_icc_table,
	.icc_tbl_size = ARRAY_SIZE(sm8550_icc_table),
	.clk_rst_tbl = sm8550_clk_reset_table,
	.clk_rst_tbl_size = ARRAY_SIZE(sm8550_clk_reset_table),
	.bw_tbl_dec = sm8550_bw_table_dec,
	.bw_tbl_dec_size = ARRAY_SIZE(sm8550_bw_table_dec),
	.pmdomain_tbl = sm8550_pmdomain_table,
	.pmdomain_tbl_size = ARRAY_SIZE(sm8550_pmdomain_table),
	.opp_pd_tbl = sm8550_opp_pd_table,
	.opp_pd_tbl_size = ARRAY_SIZE(sm8550_opp_pd_table),
	.clk_tbl = sm8550_clk_table,
	.clk_tbl_size = ARRAY_SIZE(sm8550_clk_table),
	/* Upper bound of DMA address range */
	.dma_mask = 0xe0000000 - 1,
	.fwname = "qcom/vpu/vpu30_p4.mbn",
	.pas_id = IRIS_PAS_ID,
	.inst_caps = &platform_inst_cap_sm8550,
	.inst_fw_caps_dec = inst_fw_cap_sm8550_dec,
	.inst_fw_caps_dec_size = ARRAY_SIZE(inst_fw_cap_sm8550_dec),
	.inst_fw_caps_enc = inst_fw_cap_sm8550_enc,
	.inst_fw_caps_enc_size = ARRAY_SIZE(inst_fw_cap_sm8550_enc),
	.tz_cp_config_data = &tz_cp_config_sm8550,
	.core_arch = VIDEO_ARCH_LX,
	.hw_response_timeout = HW_RESPONSE_TIMEOUT_VALUE,
	.ubwc_config = &ubwc_config_sm8550,
	.num_vpp_pipe = 4,
	.max_session_count = 16,
	.max_core_mbpf = NUM_MBS_8K * 2,
	.max_core_mbps = ((7680 * 4320) / 256) * 60,
	.dec_input_config_params_default =
		sm8550_vdec_input_config_params_default,
	.dec_input_config_params_default_size =
		ARRAY_SIZE(sm8550_vdec_input_config_params_default),
	.dec_input_config_params_hevc =
		sm8550_vdec_input_config_param_hevc,
	.dec_input_config_params_hevc_size =
		ARRAY_SIZE(sm8550_vdec_input_config_param_hevc),
	.dec_input_config_params_vp9 =
		sm8550_vdec_input_config_param_vp9,
	.dec_input_config_params_vp9_size =
		ARRAY_SIZE(sm8550_vdec_input_config_param_vp9),
	.dec_output_config_params =
		sm8550_vdec_output_config_params,
	.dec_output_config_params_size =
		ARRAY_SIZE(sm8550_vdec_output_config_params),

	.enc_input_config_params =
		sm8550_venc_input_config_params,
	.enc_input_config_params_size =
		ARRAY_SIZE(sm8550_venc_input_config_params),
	.enc_output_config_params =
		sm8550_venc_output_config_params,
	.enc_output_config_params_size =
		ARRAY_SIZE(sm8550_venc_output_config_params),

	.dec_input_prop = sm8550_vdec_subscribe_input_properties,
	.dec_input_prop_size = ARRAY_SIZE(sm8550_vdec_subscribe_input_properties),
	.dec_output_prop_avc = sm8550_vdec_subscribe_output_properties_avc,
	.dec_output_prop_avc_size =
		ARRAY_SIZE(sm8550_vdec_subscribe_output_properties_avc),
	.dec_output_prop_hevc = sm8550_vdec_subscribe_output_properties_hevc,
	.dec_output_prop_hevc_size =
		ARRAY_SIZE(sm8550_vdec_subscribe_output_properties_hevc),
	.dec_output_prop_vp9 = sm8550_vdec_subscribe_output_properties_vp9,
	.dec_output_prop_vp9_size =
		ARRAY_SIZE(sm8550_vdec_subscribe_output_properties_vp9),

	.dec_ip_int_buf_tbl = sm8550_dec_ip_int_buf_tbl,
	.dec_ip_int_buf_tbl_size = ARRAY_SIZE(sm8550_dec_ip_int_buf_tbl),
	.dec_op_int_buf_tbl = sm8550_dec_op_int_buf_tbl,
	.dec_op_int_buf_tbl_size = ARRAY_SIZE(sm8550_dec_op_int_buf_tbl),

	.enc_op_int_buf_tbl = sm8550_enc_op_int_buf_tbl,
	.enc_op_int_buf_tbl_size = ARRAY_SIZE(sm8550_enc_op_int_buf_tbl),
};

/*
 * Shares most of SM8550 data except:
 * - vpu_ops to iris_vpu33_ops
 * - clk_rst_tbl to sm8650_clk_reset_table
 * - controller_rst_tbl to sm8650_controller_reset_table
 * - fwname to "qcom/vpu/vpu33_p4.mbn"
 */
struct iris_platform_data sm8650_data = {
	.get_instance = iris_hfi_gen2_get_instance,
	.init_hfi_command_ops = iris_hfi_gen2_command_ops_init,
	.init_hfi_response_ops = iris_hfi_gen2_response_ops_init,
	.get_vpu_buffer_size = iris_vpu33_buf_size,
	.vpu_ops = &iris_vpu33_ops,
	.set_preset_registers = iris_set_sm8550_preset_registers,
	.icc_tbl = sm8550_icc_table,
	.icc_tbl_size = ARRAY_SIZE(sm8550_icc_table),
	.clk_rst_tbl = sm8650_clk_reset_table,
	.clk_rst_tbl_size = ARRAY_SIZE(sm8650_clk_reset_table),
	.controller_rst_tbl = sm8650_controller_reset_table,
	.controller_rst_tbl_size = ARRAY_SIZE(sm8650_controller_reset_table),
	.bw_tbl_dec = sm8550_bw_table_dec,
	.bw_tbl_dec_size = ARRAY_SIZE(sm8550_bw_table_dec),
	.pmdomain_tbl = sm8550_pmdomain_table,
	.pmdomain_tbl_size = ARRAY_SIZE(sm8550_pmdomain_table),
	.opp_pd_tbl = sm8550_opp_pd_table,
	.opp_pd_tbl_size = ARRAY_SIZE(sm8550_opp_pd_table),
	.clk_tbl = sm8550_clk_table,
	.clk_tbl_size = ARRAY_SIZE(sm8550_clk_table),
	/* Upper bound of DMA address range */
	.dma_mask = 0xe0000000 - 1,
	.fwname = "qcom/vpu/vpu33_p4.mbn",
	.pas_id = IRIS_PAS_ID,
	.inst_caps = &platform_inst_cap_sm8550,
	.inst_fw_caps_dec = inst_fw_cap_sm8550_dec,
	.inst_fw_caps_dec_size = ARRAY_SIZE(inst_fw_cap_sm8550_dec),
	.inst_fw_caps_enc = inst_fw_cap_sm8550_enc,
	.inst_fw_caps_enc_size = ARRAY_SIZE(inst_fw_cap_sm8550_enc),
	.tz_cp_config_data = &tz_cp_config_sm8550,
	.core_arch = VIDEO_ARCH_LX,
	.hw_response_timeout = HW_RESPONSE_TIMEOUT_VALUE,
	.ubwc_config = &ubwc_config_sm8550,
	.num_vpp_pipe = 4,
	.max_session_count = 16,
	.max_core_mbpf = NUM_MBS_8K * 2,
	.max_core_mbps = ((7680 * 4320) / 256) * 60,
	.dec_input_config_params_default =
		sm8550_vdec_input_config_params_default,
	.dec_input_config_params_default_size =
		ARRAY_SIZE(sm8550_vdec_input_config_params_default),
	.dec_input_config_params_hevc =
		sm8550_vdec_input_config_param_hevc,
	.dec_input_config_params_hevc_size =
		ARRAY_SIZE(sm8550_vdec_input_config_param_hevc),
	.dec_input_config_params_vp9 =
		sm8550_vdec_input_config_param_vp9,
	.dec_input_config_params_vp9_size =
		ARRAY_SIZE(sm8550_vdec_input_config_param_vp9),
	.dec_output_config_params =
		sm8550_vdec_output_config_params,
	.dec_output_config_params_size =
		ARRAY_SIZE(sm8550_vdec_output_config_params),

	.enc_input_config_params =
		sm8550_venc_input_config_params,
	.enc_input_config_params_size =
		ARRAY_SIZE(sm8550_venc_input_config_params),
	.enc_output_config_params =
		sm8550_venc_output_config_params,
	.enc_output_config_params_size =
		ARRAY_SIZE(sm8550_venc_output_config_params),

	.dec_input_prop = sm8550_vdec_subscribe_input_properties,
	.dec_input_prop_size = ARRAY_SIZE(sm8550_vdec_subscribe_input_properties),
	.dec_output_prop_avc = sm8550_vdec_subscribe_output_properties_avc,
	.dec_output_prop_avc_size =
		ARRAY_SIZE(sm8550_vdec_subscribe_output_properties_avc),
	.dec_output_prop_hevc = sm8550_vdec_subscribe_output_properties_hevc,
	.dec_output_prop_hevc_size =
		ARRAY_SIZE(sm8550_vdec_subscribe_output_properties_hevc),
	.dec_output_prop_vp9 = sm8550_vdec_subscribe_output_properties_vp9,
	.dec_output_prop_vp9_size =
		ARRAY_SIZE(sm8550_vdec_subscribe_output_properties_vp9),

	.dec_ip_int_buf_tbl = sm8550_dec_ip_int_buf_tbl,
	.dec_ip_int_buf_tbl_size = ARRAY_SIZE(sm8550_dec_ip_int_buf_tbl),
	.dec_op_int_buf_tbl = sm8550_dec_op_int_buf_tbl,
	.dec_op_int_buf_tbl_size = ARRAY_SIZE(sm8550_dec_op_int_buf_tbl),

	.enc_op_int_buf_tbl = sm8550_enc_op_int_buf_tbl,
	.enc_op_int_buf_tbl_size = ARRAY_SIZE(sm8550_enc_op_int_buf_tbl),
};

struct iris_platform_data sm8750_data = {
	.get_instance = iris_hfi_gen2_get_instance,
	.init_hfi_command_ops = iris_hfi_gen2_command_ops_init,
	.init_hfi_response_ops = iris_hfi_gen2_response_ops_init,
	.vpu_ops = &iris_vpu35_ops,
	.set_preset_registers = iris_set_sm8550_preset_registers,
	.icc_tbl = sm8550_icc_table,
	.icc_tbl_size = ARRAY_SIZE(sm8550_icc_table),
	.clk_rst_tbl = sm8750_clk_reset_table,
	.clk_rst_tbl_size = ARRAY_SIZE(sm8750_clk_reset_table),
	.bw_tbl_dec = sm8550_bw_table_dec,
	.bw_tbl_dec_size = ARRAY_SIZE(sm8550_bw_table_dec),
	.pmdomain_tbl = sm8550_pmdomain_table,
	.pmdomain_tbl_size = ARRAY_SIZE(sm8550_pmdomain_table),
	.opp_pd_tbl = sm8550_opp_pd_table,
	.opp_pd_tbl_size = ARRAY_SIZE(sm8550_opp_pd_table),
	.clk_tbl = sm8750_clk_table,
	.clk_tbl_size = ARRAY_SIZE(sm8750_clk_table),
	/* Upper bound of DMA address range */
	.dma_mask = 0xe0000000 - 1,
	.fwname = "qcom/vpu/vpu35_p4.mbn",
	.pas_id = IRIS_PAS_ID,
	.inst_caps = &platform_inst_cap_sm8550,
	.inst_fw_caps_dec = inst_fw_cap_sm8550_dec,
	.inst_fw_caps_dec_size = ARRAY_SIZE(inst_fw_cap_sm8550_dec),
	.inst_fw_caps_enc = inst_fw_cap_sm8550_enc,
	.inst_fw_caps_enc_size = ARRAY_SIZE(inst_fw_cap_sm8550_enc),
	.tz_cp_config_data = &tz_cp_config_sm8550,
	.core_arch = VIDEO_ARCH_LX,
	.hw_response_timeout = HW_RESPONSE_TIMEOUT_VALUE,
	.ubwc_config = &ubwc_config_sm8550,
	.num_vpp_pipe = 4,
	.max_session_count = 16,
	.max_core_mbpf = NUM_MBS_8K * 2,
	.dec_input_config_params_default =
		sm8550_vdec_input_config_params_default,
	.dec_input_config_params_default_size =
		ARRAY_SIZE(sm8550_vdec_input_config_params_default),
	.dec_input_config_params_hevc =
		sm8550_vdec_input_config_param_hevc,
	.dec_input_config_params_hevc_size =
		ARRAY_SIZE(sm8550_vdec_input_config_param_hevc),
	.dec_input_config_params_vp9 =
		sm8550_vdec_input_config_param_vp9,
	.dec_input_config_params_vp9_size =
		ARRAY_SIZE(sm8550_vdec_input_config_param_vp9),
	.dec_output_config_params =
		sm8550_vdec_output_config_params,
	.dec_output_config_params_size =
		ARRAY_SIZE(sm8550_vdec_output_config_params),

	.enc_input_config_params =
		sm8550_venc_input_config_params,
	.enc_input_config_params_size =
		ARRAY_SIZE(sm8550_venc_input_config_params),
	.enc_output_config_params =
		sm8550_venc_output_config_params,
	.enc_output_config_params_size =
		ARRAY_SIZE(sm8550_venc_output_config_params),

	.dec_input_prop = sm8550_vdec_subscribe_input_properties,
	.dec_input_prop_size = ARRAY_SIZE(sm8550_vdec_subscribe_input_properties),
	.dec_output_prop_avc = sm8550_vdec_subscribe_output_properties_avc,
	.dec_output_prop_avc_size =
		ARRAY_SIZE(sm8550_vdec_subscribe_output_properties_avc),
	.dec_output_prop_hevc = sm8550_vdec_subscribe_output_properties_hevc,
	.dec_output_prop_hevc_size =
		ARRAY_SIZE(sm8550_vdec_subscribe_output_properties_hevc),
	.dec_output_prop_vp9 = sm8550_vdec_subscribe_output_properties_vp9,
	.dec_output_prop_vp9_size =
		ARRAY_SIZE(sm8550_vdec_subscribe_output_properties_vp9),

	.dec_ip_int_buf_tbl = sm8550_dec_ip_int_buf_tbl,
	.dec_ip_int_buf_tbl_size = ARRAY_SIZE(sm8550_dec_ip_int_buf_tbl),
	.dec_op_int_buf_tbl = sm8550_dec_op_int_buf_tbl,
	.dec_op_int_buf_tbl_size = ARRAY_SIZE(sm8550_dec_op_int_buf_tbl),

	.enc_op_int_buf_tbl = sm8550_enc_op_int_buf_tbl,
	.enc_op_int_buf_tbl_size = ARRAY_SIZE(sm8550_enc_op_int_buf_tbl),
};

/*
 * Shares most of SM8550 data except:
 * - inst_caps to platform_inst_cap_qcs8300
 * - inst_fw_caps to inst_fw_cap_qcs8300
 */
struct iris_platform_data qcs8300_data = {
	.get_instance = iris_hfi_gen2_get_instance,
	.init_hfi_command_ops = iris_hfi_gen2_command_ops_init,
	.init_hfi_response_ops = iris_hfi_gen2_response_ops_init,
	.get_vpu_buffer_size = iris_vpu_buf_size,
	.vpu_ops = &iris_vpu3_ops,
	.set_preset_registers = iris_set_sm8550_preset_registers,
	.icc_tbl = sm8550_icc_table,
	.icc_tbl_size = ARRAY_SIZE(sm8550_icc_table),
	.clk_rst_tbl = sm8550_clk_reset_table,
	.clk_rst_tbl_size = ARRAY_SIZE(sm8550_clk_reset_table),
	.bw_tbl_dec = sm8550_bw_table_dec,
	.bw_tbl_dec_size = ARRAY_SIZE(sm8550_bw_table_dec),
	.pmdomain_tbl = sm8550_pmdomain_table,
	.pmdomain_tbl_size = ARRAY_SIZE(sm8550_pmdomain_table),
	.opp_pd_tbl = sm8550_opp_pd_table,
	.opp_pd_tbl_size = ARRAY_SIZE(sm8550_opp_pd_table),
	.clk_tbl = sm8550_clk_table,
	.clk_tbl_size = ARRAY_SIZE(sm8550_clk_table),
	/* Upper bound of DMA address range */
	.dma_mask = 0xe0000000 - 1,
	.fwname = "qcom/vpu/vpu30_p4_s6.mbn",
	.pas_id = IRIS_PAS_ID,
	.inst_caps = &platform_inst_cap_qcs8300,
	.inst_fw_caps_dec = inst_fw_cap_qcs8300_dec,
	.inst_fw_caps_dec_size = ARRAY_SIZE(inst_fw_cap_qcs8300_dec),
	.inst_fw_caps_enc = inst_fw_cap_qcs8300_enc,
	.inst_fw_caps_enc_size = ARRAY_SIZE(inst_fw_cap_qcs8300_enc),
	.tz_cp_config_data = &tz_cp_config_sm8550,
	.core_arch = VIDEO_ARCH_LX,
	.hw_response_timeout = HW_RESPONSE_TIMEOUT_VALUE,
	.ubwc_config = &ubwc_config_sm8550,
	.num_vpp_pipe = 2,
	.max_session_count = 16,
	.max_core_mbpf = ((4096 * 2176) / 256) * 4,
	.max_core_mbps = (((3840 * 2176) / 256) * 120),
	.dec_input_config_params_default =
		sm8550_vdec_input_config_params_default,
	.dec_input_config_params_default_size =
		ARRAY_SIZE(sm8550_vdec_input_config_params_default),
	.dec_input_config_params_hevc =
		sm8550_vdec_input_config_param_hevc,
	.dec_input_config_params_hevc_size =
		ARRAY_SIZE(sm8550_vdec_input_config_param_hevc),
	.dec_input_config_params_vp9 =
		sm8550_vdec_input_config_param_vp9,
	.dec_input_config_params_vp9_size =
		ARRAY_SIZE(sm8550_vdec_input_config_param_vp9),
	.dec_output_config_params =
		sm8550_vdec_output_config_params,
	.dec_output_config_params_size =
		ARRAY_SIZE(sm8550_vdec_output_config_params),

	.enc_input_config_params =
		sm8550_venc_input_config_params,
	.enc_input_config_params_size =
		ARRAY_SIZE(sm8550_venc_input_config_params),
	.enc_output_config_params =
		sm8550_venc_output_config_params,
	.enc_output_config_params_size =
		ARRAY_SIZE(sm8550_venc_output_config_params),

	.dec_input_prop = sm8550_vdec_subscribe_input_properties,
	.dec_input_prop_size = ARRAY_SIZE(sm8550_vdec_subscribe_input_properties),
	.dec_output_prop_avc = sm8550_vdec_subscribe_output_properties_avc,
	.dec_output_prop_avc_size =
		ARRAY_SIZE(sm8550_vdec_subscribe_output_properties_avc),
	.dec_output_prop_hevc = sm8550_vdec_subscribe_output_properties_hevc,
	.dec_output_prop_hevc_size =
		ARRAY_SIZE(sm8550_vdec_subscribe_output_properties_hevc),
	.dec_output_prop_vp9 = sm8550_vdec_subscribe_output_properties_vp9,
	.dec_output_prop_vp9_size =
		ARRAY_SIZE(sm8550_vdec_subscribe_output_properties_vp9),

	.dec_ip_int_buf_tbl = sm8550_dec_ip_int_buf_tbl,
	.dec_ip_int_buf_tbl_size = ARRAY_SIZE(sm8550_dec_ip_int_buf_tbl),
	.dec_op_int_buf_tbl = sm8550_dec_op_int_buf_tbl,
	.dec_op_int_buf_tbl_size = ARRAY_SIZE(sm8550_dec_op_int_buf_tbl),

	.enc_op_int_buf_tbl = sm8550_enc_op_int_buf_tbl,
	.enc_op_int_buf_tbl_size = ARRAY_SIZE(sm8550_enc_op_int_buf_tbl),
};
