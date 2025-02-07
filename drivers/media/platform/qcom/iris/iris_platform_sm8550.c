// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_hfi_gen2.h"
#include "iris_hfi_gen2_defines.h"
#include "iris_platform_common.h"
#include "iris_vpu_common.h"

#define VIDEO_ARCH_LX 1

static struct platform_inst_fw_cap inst_fw_cap_sm8550[] = {
	{
		.cap_id = PROFILE,
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
	},
	{
		.cap_id = LEVEL,
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
	},
};

static struct platform_inst_caps platform_inst_cap_sm8550 = {
	.min_frame_width = 96,
	.max_frame_width = 8192,
	.min_frame_height = 96,
	.max_frame_height = 8192,
	.max_mbpf = (8192 * 4352) / 256,
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

struct iris_platform_data sm8550_data = {
	.get_instance = iris_hfi_gen2_get_instance,
	.init_hfi_command_ops = iris_hfi_gen2_command_ops_init,
	.init_hfi_response_ops = iris_hfi_gen2_response_ops_init,
	.vpu_ops = &iris_vpu3_ops,
	.set_preset_registers = iris_set_sm8550_preset_registers,
	.icc_tbl = sm8550_icc_table,
	.icc_tbl_size = ARRAY_SIZE(sm8550_icc_table),
	.clk_rst_tbl = sm8550_clk_reset_table,
	.clk_rst_tbl_size = ARRAY_SIZE(sm8550_clk_reset_table),
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
	.inst_fw_caps = inst_fw_cap_sm8550,
	.inst_fw_caps_size = ARRAY_SIZE(inst_fw_cap_sm8550),
	.tz_cp_config_data = &tz_cp_config_sm8550,
	.core_arch = VIDEO_ARCH_LX,
	.hw_response_timeout = HW_RESPONSE_TIMEOUT_VALUE,
	.ubwc_config = &ubwc_config_sm8550,
	.num_vpp_pipe = 4,
	.max_session_count = 16,
};
