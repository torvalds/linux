// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_ctrls.h"
#include "iris_hfi_gen2.h"
#include "iris_hfi_gen2_defines.h"
#include "iris_platform_common.h"
#include "iris_vpu_buffer.h"
#include "iris_vpu_common.h"

#define WRAPPER_INTR_STATUS_A2HWD_BMSK		0x10

static const struct iris_firmware_desc iris_vpu_ar50lt_p1_gen1_s6_desc = {
	.firmware_data = &iris_hfi_gen1_ar50lt_data,
	.get_vpu_buffer_size = iris_vpu_ar50lt_gen1_buf_size,
	.fwname = "qcom/venus-6.0/venus.mbn",
};

static const struct iris_firmware_desc iris_vpu_ar50lt_p1_gen2_s6_desc = {
	.firmware_data = &iris_hfi_gen2_ar50lt_data,
	.get_vpu_buffer_size = iris_vpu_ar50lt_gen2_buf_size,
	.fwname = "qcom/vpu/ar50lt_p1_gen2_s6.mbn",
};

static const u32 iris_fmts_ar50lt_dec[] = {
	V4L2_PIX_FMT_H264,
	V4L2_PIX_FMT_HEVC,
	V4L2_PIX_FMT_VP9,
};

static const struct bw_info iris_bw_table_dec_ar50lt[] = {
	{ ((1920 * 1080) / 256) * 60, 1564000, },
	{ ((1920 * 1080) / 256) * 30,  791000, },
	{ ((1280 * 720) / 256) * 60,   688000, },
	{ ((1280 * 720) / 256) * 30,   347000, },
};

static const struct icc_info iris_icc_info_ar50lt[] = {
	{ "cpu-cfg",    1000, 1000     },
	{ "video-mem",  1000, 6500000  },
};

static const char * const iris_pmdomain_table_ar50lt[] = { "venus", "vcodec0" };

static const char * const iris_opp_pd_table_ar50lt[] = { "cx" };

static const struct platform_clk_data iris_clk_table_ar50lt[] = {
	{IRIS_CTRL_CLK,    "core"         },
	{IRIS_AXI_CLK,     "iface"        },
	{IRIS_AHB_CLK,     "bus"          },
	{IRIS_HW_CLK,      "vcodec0_core" },
	{IRIS_HW_AHB_CLK,  "vcodec0_bus"  },
	{IRIS_THROTTLE_CLK, "throttle"    },
};

static const char * const iris_opp_clk_table_ar50lt[] = {
	"vcodec0_core",
	NULL,
};

static const struct tz_cp_config tz_cp_config_ar50lt[] = {
	{
		.cp_start = 0,
		.cp_size = 0x25800000,
		.cp_nonpixel_start = 0x01000000,
		.cp_nonpixel_size = 0x24800000,
	},
};

static struct platform_inst_caps platform_inst_cap_ar50lt = {
	.min_frame_width = 128,
	.max_frame_width = 1920,
	.min_frame_height = 128,
	.max_frame_height = 1920,
	.max_mbpf = (1920 * 1088) / 256,
	.mb_cycles_vpp = 440,
	.mb_cycles_fw = 733003,
	.mb_cycles_fw_vpp = 225975,
	.max_frame_rate = 120,
	.max_operating_rate = 120,
};

const struct iris_platform_data qcm2290_data = {
	.firmware_desc_gen1 = &iris_vpu_ar50lt_p1_gen1_s6_desc,
	.firmware_desc_gen2 = &iris_vpu_ar50lt_p1_gen2_s6_desc,
	.vpu_ops = &iris_vpu_ar50lt_ops,
	.icc_tbl = iris_icc_info_ar50lt,
	.icc_tbl_size = ARRAY_SIZE(iris_icc_info_ar50lt),
	.bw_tbl_dec = iris_bw_table_dec_ar50lt,
	.bw_tbl_dec_size = ARRAY_SIZE(iris_bw_table_dec_ar50lt),
	.pmdomain_tbl = iris_pmdomain_table_ar50lt,
	.pmdomain_tbl_size = ARRAY_SIZE(iris_pmdomain_table_ar50lt),
	.opp_pd_tbl = iris_opp_pd_table_ar50lt,
	.opp_pd_tbl_size = ARRAY_SIZE(iris_opp_pd_table_ar50lt),
	.clk_tbl = iris_clk_table_ar50lt,
	.clk_tbl_size = ARRAY_SIZE(iris_clk_table_ar50lt),
	.opp_clk_tbl = iris_opp_clk_table_ar50lt,
	/* Upper bound of DMA address range */
	.dma_mask = 0xe0000000 - 1,
	.inst_iris_fmts = iris_fmts_ar50lt_dec,
	.inst_iris_fmts_size = ARRAY_SIZE(iris_fmts_ar50lt_dec),
	.inst_caps = &platform_inst_cap_ar50lt,
	.slice_caps = &iris_ar50lt_slice_caps,
	.tz_cp_config_data = tz_cp_config_ar50lt,
	.tz_cp_config_data_size = ARRAY_SIZE(tz_cp_config_ar50lt),
	.num_vpp_pipe = 1,
	.no_rpmh = true,
	.wd_intr_mask = WRAPPER_INTR_STATUS_A2HWD_BMSK,
	.icc_ib_multiplier = 2,
	.max_session_count = 8,
	.max_core_mbpf = ((1920 * 1088) / 256) * 4,
	/* Concurrency: 1080p@30 decode + 1080p@30 encode */
	/* Concurrency: 3 * 1080p@30 decode */
	.max_core_mbps = (((1920 * 1088) / 256) * 90),
};
