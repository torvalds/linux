// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
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
#include "iris_platform_sm8550.h"
#include "iris_platform_sm8650.h"
#include "iris_platform_sm8750.h"
#include "iris_platform_x1p42100.h"

static const struct iris_firmware_desc iris_vpu30_p4_s6_gen2_desc = {
	.firmware_data = &iris_hfi_gen2_data,
	.get_vpu_buffer_size = iris_vpu_buf_size,
	.fwname = "qcom/vpu/vpu30_p4_s6.mbn",
};

static const struct iris_firmware_desc iris_vpu30_p4_gen2_desc = {
	.firmware_data = &iris_hfi_gen2_data,
	.get_vpu_buffer_size = iris_vpu_buf_size,
	.fwname = "qcom/vpu/vpu30_p4.mbn",
};

static const struct iris_firmware_desc iris_vpu30_p1_gen2_desc = {
	.firmware_data = &iris_hfi_gen2_data,
	.get_vpu_buffer_size = iris_vpu_buf_size,
	.fwname = "qcom/vpu/vpu30_p1_s7.mbn",
};

static const struct iris_firmware_desc iris_vpu33_p4_gen2_desc = {
	.firmware_data = &iris_hfi_gen2_data,
	.get_vpu_buffer_size = iris_vpu33_buf_size,
	.fwname = "qcom/vpu/vpu33_p4.mbn",
};

static const struct iris_firmware_desc iris_vpu35_p4_gen2_desc = {
	.firmware_data = &iris_hfi_gen2_data,
	.get_vpu_buffer_size = iris_vpu33_buf_size,
	.fwname = "qcom/vpu/vpu35_p4.mbn",
};

static const u32 iris_fmts_vpu3x_dec[] = {
	[IRIS_FMT_H264] = V4L2_PIX_FMT_H264,
	[IRIS_FMT_HEVC] = V4L2_PIX_FMT_HEVC,
	[IRIS_FMT_VP9] = V4L2_PIX_FMT_VP9,
	[IRIS_FMT_AV1] = V4L2_PIX_FMT_AV1,
};

static const struct icc_info iris_icc_info_vpu3x[] = {
	{ "cpu-cfg",    1000, 1000     },
	{ "video-mem",  1000, 15000000 },
};

static const struct bw_info iris_bw_table_dec_vpu3x[] = {
	{ ((4096 * 2160) / 256) * 60, 1608000 },
	{ ((4096 * 2160) / 256) * 30,  826000 },
	{ ((1920 * 1080) / 256) * 60,  567000 },
	{ ((1920 * 1080) / 256) * 30,  294000 },
};

static const char * const iris_pmdomain_table_vpu3x[] = { "venus", "vcodec0" };

static const char * const iris_opp_pd_table_vpu3x[] = { "mxc", "mmcx" };

static const char * const iris_opp_clk_table_vpu3x[] = {
	"vcodec0_core",
	NULL,
};

static const struct tz_cp_config tz_cp_config_vpu3[] = {
	{
		.cp_start = 0,
		.cp_size = 0x25800000,
		.cp_nonpixel_start = 0x01000000,
		.cp_nonpixel_size = 0x24800000,
	},
};

/*
 * Shares most of SM8550 data except:
 * - inst_caps to platform_inst_cap_qcs8300
 */
const struct iris_platform_data qcs8300_data = {
	.firmware_desc = &iris_vpu30_p4_s6_gen2_desc,
	.vpu_ops = &iris_vpu3_ops,
	.icc_tbl = iris_icc_info_vpu3x,
	.icc_tbl_size = ARRAY_SIZE(iris_icc_info_vpu3x),
	.clk_rst_tbl = sm8550_clk_reset_table,
	.clk_rst_tbl_size = ARRAY_SIZE(sm8550_clk_reset_table),
	.bw_tbl_dec = iris_bw_table_dec_vpu3x,
	.bw_tbl_dec_size = ARRAY_SIZE(iris_bw_table_dec_vpu3x),
	.pmdomain_tbl = iris_pmdomain_table_vpu3x,
	.pmdomain_tbl_size = ARRAY_SIZE(iris_pmdomain_table_vpu3x),
	.opp_pd_tbl = iris_opp_pd_table_vpu3x,
	.opp_pd_tbl_size = ARRAY_SIZE(iris_opp_pd_table_vpu3x),
	.clk_tbl = sm8550_clk_table,
	.clk_tbl_size = ARRAY_SIZE(sm8550_clk_table),
	.opp_clk_tbl = iris_opp_clk_table_vpu3x,
	/* Upper bound of DMA address range */
	.dma_mask = 0xe0000000 - 1,
	.inst_iris_fmts = iris_fmts_vpu3x_dec,
	.inst_iris_fmts_size = ARRAY_SIZE(iris_fmts_vpu3x_dec),
	.inst_caps = &platform_inst_cap_qcs8300,
	.tz_cp_config_data = tz_cp_config_vpu3,
	.tz_cp_config_data_size = ARRAY_SIZE(tz_cp_config_vpu3),
	.num_vpp_pipe = 2,
	.max_session_count = 16,
	.max_core_mbpf = ((4096 * 2176) / 256) * 4,
	.max_core_mbps = (((3840 * 2176) / 256) * 120),
};

const struct iris_platform_data sm8550_data = {
	.firmware_desc = &iris_vpu30_p4_gen2_desc,
	.vpu_ops = &iris_vpu3_ops,
	.icc_tbl = iris_icc_info_vpu3x,
	.icc_tbl_size = ARRAY_SIZE(iris_icc_info_vpu3x),
	.clk_rst_tbl = sm8550_clk_reset_table,
	.clk_rst_tbl_size = ARRAY_SIZE(sm8550_clk_reset_table),
	.bw_tbl_dec = iris_bw_table_dec_vpu3x,
	.bw_tbl_dec_size = ARRAY_SIZE(iris_bw_table_dec_vpu3x),
	.pmdomain_tbl = iris_pmdomain_table_vpu3x,
	.pmdomain_tbl_size = ARRAY_SIZE(iris_pmdomain_table_vpu3x),
	.opp_pd_tbl = iris_opp_pd_table_vpu3x,
	.opp_pd_tbl_size = ARRAY_SIZE(iris_opp_pd_table_vpu3x),
	.clk_tbl = sm8550_clk_table,
	.clk_tbl_size = ARRAY_SIZE(sm8550_clk_table),
	.opp_clk_tbl = iris_opp_clk_table_vpu3x,
	/* Upper bound of DMA address range */
	.dma_mask = 0xe0000000 - 1,
	.inst_iris_fmts = iris_fmts_vpu3x_dec,
	.inst_iris_fmts_size = ARRAY_SIZE(iris_fmts_vpu3x_dec),
	.inst_caps = &platform_inst_cap_sm8550,
	.tz_cp_config_data = tz_cp_config_vpu3,
	.tz_cp_config_data_size = ARRAY_SIZE(tz_cp_config_vpu3),
	.num_vpp_pipe = 4,
	.max_session_count = 16,
	.max_core_mbpf = NUM_MBS_8K * 2,
	.max_core_mbps = ((7680 * 4320) / 256) * 60,
};

/*
 * Shares most of SM8550 data except:
 * - vpu_ops to iris_vpu33_ops
 * - clk_rst_tbl to sm8650_clk_reset_table
 * - controller_rst_tbl to sm8650_controller_reset_table
 */
const struct iris_platform_data sm8650_data = {
	.firmware_desc = &iris_vpu33_p4_gen2_desc,
	.vpu_ops = &iris_vpu33_ops,
	.icc_tbl = iris_icc_info_vpu3x,
	.icc_tbl_size = ARRAY_SIZE(iris_icc_info_vpu3x),
	.clk_rst_tbl = sm8650_clk_reset_table,
	.clk_rst_tbl_size = ARRAY_SIZE(sm8650_clk_reset_table),
	.controller_rst_tbl = sm8650_controller_reset_table,
	.controller_rst_tbl_size = ARRAY_SIZE(sm8650_controller_reset_table),
	.bw_tbl_dec = iris_bw_table_dec_vpu3x,
	.bw_tbl_dec_size = ARRAY_SIZE(iris_bw_table_dec_vpu3x),
	.pmdomain_tbl = iris_pmdomain_table_vpu3x,
	.pmdomain_tbl_size = ARRAY_SIZE(iris_pmdomain_table_vpu3x),
	.opp_pd_tbl = iris_opp_pd_table_vpu3x,
	.opp_pd_tbl_size = ARRAY_SIZE(iris_opp_pd_table_vpu3x),
	.clk_tbl = sm8550_clk_table,
	.clk_tbl_size = ARRAY_SIZE(sm8550_clk_table),
	.opp_clk_tbl = iris_opp_clk_table_vpu3x,
	/* Upper bound of DMA address range */
	.dma_mask = 0xe0000000 - 1,
	.inst_iris_fmts = iris_fmts_vpu3x_dec,
	.inst_iris_fmts_size = ARRAY_SIZE(iris_fmts_vpu3x_dec),
	.inst_caps = &platform_inst_cap_sm8550,
	.tz_cp_config_data = tz_cp_config_vpu3,
	.tz_cp_config_data_size = ARRAY_SIZE(tz_cp_config_vpu3),
	.num_vpp_pipe = 4,
	.max_session_count = 16,
	.max_core_mbpf = NUM_MBS_8K * 2,
	.max_core_mbps = ((7680 * 4320) / 256) * 60,
};

const struct iris_platform_data sm8750_data = {
	.firmware_desc = &iris_vpu35_p4_gen2_desc,
	.vpu_ops = &iris_vpu35_ops,
	.icc_tbl = iris_icc_info_vpu3x,
	.icc_tbl_size = ARRAY_SIZE(iris_icc_info_vpu3x),
	.clk_rst_tbl = sm8750_clk_reset_table,
	.clk_rst_tbl_size = ARRAY_SIZE(sm8750_clk_reset_table),
	.bw_tbl_dec = iris_bw_table_dec_vpu3x,
	.bw_tbl_dec_size = ARRAY_SIZE(iris_bw_table_dec_vpu3x),
	.pmdomain_tbl = iris_pmdomain_table_vpu3x,
	.pmdomain_tbl_size = ARRAY_SIZE(iris_pmdomain_table_vpu3x),
	.opp_pd_tbl = iris_opp_pd_table_vpu3x,
	.opp_pd_tbl_size = ARRAY_SIZE(iris_opp_pd_table_vpu3x),
	.clk_tbl = sm8750_clk_table,
	.clk_tbl_size = ARRAY_SIZE(sm8750_clk_table),
	.opp_clk_tbl = iris_opp_clk_table_vpu3x,
	/* Upper bound of DMA address range */
	.dma_mask = 0xe0000000 - 1,
	.inst_iris_fmts = iris_fmts_vpu3x_dec,
	.inst_iris_fmts_size = ARRAY_SIZE(iris_fmts_vpu3x_dec),
	.inst_caps = &platform_inst_cap_sm8550,
	.tz_cp_config_data = tz_cp_config_vpu3,
	.tz_cp_config_data_size = ARRAY_SIZE(tz_cp_config_vpu3),
	.num_vpp_pipe = 4,
	.max_session_count = 16,
	.max_core_mbpf = NUM_MBS_8K * 2,
	.max_core_mbps = ((7680 * 4320) / 256) * 60,
};

/*
 * Shares most of SM8550 data except:
 * - clk_tbl and opp_clk_tbl for x1p42100
 * - different firmware
 * - different num_vpp_pipe
 */
const struct iris_platform_data x1p42100_data = {
	.firmware_desc = &iris_vpu30_p1_gen2_desc,
	.vpu_ops = &iris_vpu3_ops,
	.icc_tbl = iris_icc_info_vpu3x,
	.icc_tbl_size = ARRAY_SIZE(iris_icc_info_vpu3x),
	.clk_rst_tbl = sm8550_clk_reset_table,
	.clk_rst_tbl_size = ARRAY_SIZE(sm8550_clk_reset_table),
	.bw_tbl_dec = iris_bw_table_dec_vpu3x,
	.bw_tbl_dec_size = ARRAY_SIZE(iris_bw_table_dec_vpu3x),
	.pmdomain_tbl = iris_pmdomain_table_vpu3x,
	.pmdomain_tbl_size = ARRAY_SIZE(iris_pmdomain_table_vpu3x),
	.opp_pd_tbl = iris_opp_pd_table_vpu3x,
	.opp_pd_tbl_size = ARRAY_SIZE(iris_opp_pd_table_vpu3x),
	.clk_tbl = x1p42100_clk_table,
	.clk_tbl_size = ARRAY_SIZE(x1p42100_clk_table),
	.opp_clk_tbl = x1p42100_opp_clk_table,
	/* Upper bound of DMA address range */
	.dma_mask = 0xe0000000 - 1,
	.inst_iris_fmts = iris_fmts_vpu3x_dec,
	.inst_iris_fmts_size = ARRAY_SIZE(iris_fmts_vpu3x_dec),
	.inst_caps = &platform_inst_cap_sm8550,
	.tz_cp_config_data = tz_cp_config_vpu3,
	.tz_cp_config_data_size = ARRAY_SIZE(tz_cp_config_vpu3),
	.num_vpp_pipe = 1,
	.max_session_count = 16,
	.max_core_mbpf = NUM_MBS_8K * 2,
	.max_core_mbps = ((7680 * 4320) / 256) * 60,
};
