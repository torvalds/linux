/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

static struct platform_inst_fw_cap inst_fw_cap_qcs8300[] = {
	{
		.cap_id = PROFILE,
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
		.set = iris_set_u32_enum,
	},
	{
		.cap_id = LEVEL,
		.min = V4L2_MPEG_VIDEO_H264_LEVEL_1_0,
		.max = V4L2_MPEG_VIDEO_H264_LEVEL_6_2,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_0) |
			BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1B)  |
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
		.max = PIPE_2,
		.step_or_mask = 1,
		.value = PIPE_2,
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

static struct platform_inst_caps platform_inst_cap_qcs8300 = {
	.min_frame_width = 96,
	.max_frame_width = 4096,
	.min_frame_height = 96,
	.max_frame_height = 4096,
	.max_mbpf = (4096 * 2176) / 256,
	.mb_cycles_vpp = 200,
	.mb_cycles_fw = 326389,
	.mb_cycles_fw_vpp = 44156,
	.num_comv = 0,
};
