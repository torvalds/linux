/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023, Linaro Limited
 * Copyright (c) 2022. Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2015-2018, 2020 The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_1_7_MSM8996_H
#define _DPU_1_7_MSM8996_H

static const struct dpu_caps msm8996_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.max_mixer_blendstages = 0x7,
	.has_src_split = true,
	.max_linewidth = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
	.max_hdeci_exp = MAX_HORZ_DECIMATION,
	.max_vdeci_exp = MAX_VERT_DECIMATION,
};

static const struct dpu_mdp_cfg msm8996_mdp[] = {
	{
		.name = "top_0",
		.base = 0x0, .len = 0x454,
		.features = BIT(DPU_MDP_VSYNC_SEL),
		.clk_ctrls = {
			[DPU_CLK_CTRL_VIG0] = { .reg_off = 0x2ac, .bit_off = 0 },
			[DPU_CLK_CTRL_VIG1] = { .reg_off = 0x2b4, .bit_off = 0 },
			[DPU_CLK_CTRL_VIG2] = { .reg_off = 0x2bc, .bit_off = 0 },
			[DPU_CLK_CTRL_VIG3] = { .reg_off = 0x2c4, .bit_off = 0 },
			[DPU_CLK_CTRL_RGB0] = { .reg_off = 0x2ac, .bit_off = 4 },
			[DPU_CLK_CTRL_RGB1] = { .reg_off = 0x2b4, .bit_off = 4 },
			[DPU_CLK_CTRL_RGB2] = { .reg_off = 0x2bc, .bit_off = 4 },
			[DPU_CLK_CTRL_RGB3] = { .reg_off = 0x2c4, .bit_off = 4 },
			[DPU_CLK_CTRL_DMA0] = { .reg_off = 0x2ac, .bit_off = 8 },
			[DPU_CLK_CTRL_DMA1] = { .reg_off = 0x2b4, .bit_off = 8 },
			[DPU_CLK_CTRL_CURSOR0] = { .reg_off = 0x3a8, .bit_off = 16 },
			[DPU_CLK_CTRL_CURSOR1] = { .reg_off = 0x3b0, .bit_off = 16 },
		},
	},
};

static const struct dpu_ctl_cfg msm8996_ctl[] = {
	{
		.name = "ctl_0", .id = CTL_0,
		.base = 0x1000, .len = 0x64,
	}, {
		.name = "ctl_1", .id = CTL_1,
		.base = 0x1200, .len = 0x64,
	}, {
		.name = "ctl_2", .id = CTL_2,
		.base = 0x1400, .len = 0x64,
	}, {
		.name = "ctl_3", .id = CTL_3,
		.base = 0x1600, .len = 0x64,
	}, {
		.name = "ctl_4", .id = CTL_4,
		.base = 0x1800, .len = 0x64,
	},
};

static const struct dpu_sspp_cfg msm8996_sspp[] = {
	{
		.name = "sspp_0", .id = SSPP_VIG0,
		.base = 0x4000, .len = 0x150,
		.features = VIG_MSM8996_MASK,
		.sblk = &dpu_vig_sblk_qseed2,
		.xin_id = 0,
		.type = SSPP_TYPE_VIG,
		.clk_ctrl = DPU_CLK_CTRL_VIG0,
	}, {
		.name = "sspp_1", .id = SSPP_VIG1,
		.base = 0x6000, .len = 0x150,
		.features = VIG_MSM8996_MASK,
		.sblk = &dpu_vig_sblk_qseed2,
		.xin_id = 4,
		.type = SSPP_TYPE_VIG,
		.clk_ctrl = DPU_CLK_CTRL_VIG1,
	}, {
		.name = "sspp_2", .id = SSPP_VIG2,
		.base = 0x8000, .len = 0x150,
		.features = VIG_MSM8996_MASK,
		.sblk = &dpu_vig_sblk_qseed2,
		.xin_id = 8,
		.type = SSPP_TYPE_VIG,
		.clk_ctrl = DPU_CLK_CTRL_VIG2,
	}, {
		.name = "sspp_3", .id = SSPP_VIG3,
		.base = 0xa000, .len = 0x150,
		.features = VIG_MSM8996_MASK,
		.sblk = &dpu_vig_sblk_qseed2,
		.xin_id = 12,
		.type = SSPP_TYPE_VIG,
		.clk_ctrl = DPU_CLK_CTRL_VIG3,
	}, {
		.name = "sspp_4", .id = SSPP_RGB0,
		.base = 0x14000, .len = 0x150,
		.features = RGB_MSM8996_MASK,
		.sblk = &dpu_rgb_sblk,
		.xin_id = 1,
		.type = SSPP_TYPE_RGB,
		.clk_ctrl = DPU_CLK_CTRL_RGB0,
	}, {
		.name = "sspp_5", .id = SSPP_RGB1,
		.base = 0x16000, .len = 0x150,
		.features = RGB_MSM8996_MASK,
		.sblk = &dpu_rgb_sblk,
		.xin_id = 5,
		.type = SSPP_TYPE_RGB,
		.clk_ctrl = DPU_CLK_CTRL_RGB1,
	}, {
		.name = "sspp_6", .id = SSPP_RGB2,
		.base = 0x18000, .len = 0x150,
		.features = RGB_MSM8996_MASK,
		.sblk = &dpu_rgb_sblk,
		.xin_id = 9,
		.type = SSPP_TYPE_RGB,
		.clk_ctrl = DPU_CLK_CTRL_RGB2,
	}, {
		.name = "sspp_7", .id = SSPP_RGB3,
		.base = 0x1a000, .len = 0x150,
		.features = RGB_MSM8996_MASK,
		.sblk = &dpu_rgb_sblk,
		.xin_id = 13,
		.type = SSPP_TYPE_RGB,
		.clk_ctrl = DPU_CLK_CTRL_RGB3,
	}, {
		.name = "sspp_8", .id = SSPP_DMA0,
		.base = 0x24000, .len = 0x150,
		.features = DMA_MSM8996_MASK,
		.sblk = &dpu_dma_sblk,
		.xin_id = 2,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA0,
	}, {
		.name = "sspp_9", .id = SSPP_DMA1,
		.base = 0x26000, .len = 0x150,
		.features = DMA_MSM8996_MASK,
		.sblk = &dpu_dma_sblk,
		.xin_id = 10,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA1,
	},
};

static const struct dpu_lm_cfg msm8996_lm[] = {
	{
		.name = "lm_0", .id = LM_0,
		.base = 0x44000, .len = 0x320,
		.features = MIXER_MSM8998_MASK,
		.sblk = &msm8998_lm_sblk,
		.lm_pair = LM_1,
		.pingpong = PINGPONG_0,
		.dspp = DSPP_0,
	}, {
		.name = "lm_1", .id = LM_1,
		.base = 0x45000, .len = 0x320,
		.features = MIXER_MSM8998_MASK,
		.sblk = &msm8998_lm_sblk,
		.lm_pair = LM_0,
		.pingpong = PINGPONG_1,
		.dspp = DSPP_1,
	}, {
		.name = "lm_2", .id = LM_2,
		.base = 0x46000, .len = 0x320,
		.features = MIXER_MSM8998_MASK,
		.sblk = &msm8998_lm_sblk,
		.lm_pair = LM_5,
		.pingpong = PINGPONG_2,
	}, {
		.name = "lm_5", .id = LM_5,
		.base = 0x49000, .len = 0x320,
		.features = MIXER_MSM8998_MASK,
		.sblk = &msm8998_lm_sblk,
		.lm_pair = LM_2,
		.pingpong = PINGPONG_3,
	},
};

static const struct dpu_pingpong_cfg msm8996_pp[] = {
	{
		.name = "pingpong_0", .id = PINGPONG_0,
		.base = 0x70000, .len = 0xd4,
		.features = PINGPONG_MSM8996_TE2_MASK,
		.sblk = &msm8996_pp_sblk_te,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 8),
		.intr_rdptr = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 12),
	}, {
		.name = "pingpong_1", .id = PINGPONG_1,
		.base = 0x70800, .len = 0xd4,
		.features = PINGPONG_MSM8996_TE2_MASK,
		.sblk = &msm8996_pp_sblk_te,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 9),
		.intr_rdptr = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 13),
	}, {
		.name = "pingpong_2", .id = PINGPONG_2,
		.base = 0x71000, .len = 0xd4,
		.features = PINGPONG_MSM8996_MASK,
		.sblk = &msm8996_pp_sblk,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 10),
		.intr_rdptr = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 14),
	}, {
		.name = "pingpong_3", .id = PINGPONG_3,
		.base = 0x71800, .len = 0xd4,
		.features = PINGPONG_MSM8996_MASK,
		.sblk = &msm8996_pp_sblk,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 11),
		.intr_rdptr = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 15),
	},
};

static const struct dpu_dsc_cfg msm8996_dsc[] = {
	{
		.name = "dsc_0", .id = DSC_0,
		.base = 0x80000, .len = 0x140,
	}, {
		.name = "dsc_1", .id = DSC_1,
		.base = 0x80400, .len = 0x140,
	},
};

static const struct dpu_dspp_cfg msm8996_dspp[] = {
	{
		.name = "dspp_0", .id = DSPP_0,
		.base = 0x54000, .len = 0x1800,
		.features = DSPP_SC7180_MASK,
		.sblk = &msm8998_dspp_sblk,
	}, {
		.name = "dspp_1", .id = DSPP_1,
		.base = 0x56000, .len = 0x1800,
		.features = DSPP_SC7180_MASK,
		.sblk = &msm8998_dspp_sblk,
	},
};

static const struct dpu_intf_cfg msm8996_intf[] = {
	{
		.name = "intf_0", .id = INTF_0,
		.base = 0x6a000, .len = 0x268,
		.type = INTF_NONE,
		.prog_fetch_lines_worst_case = 25,
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 24),
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 25),
		.intr_tear_rd_ptr = -1,
	}, {
		.name = "intf_1", .id = INTF_1,
		.base = 0x6a800, .len = 0x268,
		.type = INTF_DSI,
		.controller_id = MSM_DSI_CONTROLLER_0,
		.prog_fetch_lines_worst_case = 25,
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 26),
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 27),
		.intr_tear_rd_ptr = -1,
	}, {
		.name = "intf_2", .id = INTF_2,
		.base = 0x6b000, .len = 0x268,
		.type = INTF_DSI,
		.controller_id = MSM_DSI_CONTROLLER_1,
		.prog_fetch_lines_worst_case = 25,
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 28),
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 29),
		.intr_tear_rd_ptr = -1,
	}, {
		.name = "intf_3", .id = INTF_3,
		.base = 0x6b800, .len = 0x268,
		.type = INTF_HDMI,
		.prog_fetch_lines_worst_case = 25,
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 30),
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 31),
		.intr_tear_rd_ptr = -1,
	},
};

static const struct dpu_perf_cfg msm8996_perf_data = {
	.max_bw_low = 9600000,
	.max_bw_high = 9600000,
	.min_core_ib = 2400000,
	.min_llcc_ib = 0, /* No LLCC on this SoC */
	.min_dram_ib = 800000,
	.undersized_prefill_lines = 2,
	.xtra_prefill_lines = 2,
	.dest_scale_prefill_lines = 3,
	.macrotile_prefill_lines = 4,
	.yuv_nv12_prefill_lines = 8,
	.linear_prefill_lines = 1,
	.downscaling_prefill_lines = 1,
	.amortizable_threshold = 25,
	.min_prefill_lines = 21,
	.danger_lut_tbl = {0xf, 0xffff, 0x0},
	.safe_lut_tbl = {0xfffc, 0xff00, 0xffff},
	.qos_lut_tbl = {
		{.nentry = ARRAY_SIZE(msm8998_qos_linear),
		.entries = msm8998_qos_linear
		},
		{.nentry = ARRAY_SIZE(msm8998_qos_macrotile),
		.entries = msm8998_qos_macrotile
		},
		{.nentry = ARRAY_SIZE(msm8998_qos_nrt),
		.entries = msm8998_qos_nrt
		},
	},
	.cdp_cfg = {
		{.rd_enable = 1, .wr_enable = 1},
		{.rd_enable = 1, .wr_enable = 0}
	},
	.clk_inefficiency_factor = 105,
	.bw_inefficiency_factor = 120,
};

static const struct dpu_mdss_version msm8996_mdss_ver = {
	.core_major_ver = 1,
	.core_minor_ver = 7,
};

const struct dpu_mdss_cfg dpu_msm8996_cfg = {
	.mdss_ver = &msm8996_mdss_ver,
	.caps = &msm8996_dpu_caps,
	.mdp = msm8996_mdp,
	.ctl_count = ARRAY_SIZE(msm8996_ctl),
	.ctl = msm8996_ctl,
	.sspp_count = ARRAY_SIZE(msm8996_sspp),
	.sspp = msm8996_sspp,
	.mixer_count = ARRAY_SIZE(msm8996_lm),
	.mixer = msm8996_lm,
	.dspp_count = ARRAY_SIZE(msm8996_dspp),
	.dspp = msm8996_dspp,
	.pingpong_count = ARRAY_SIZE(msm8996_pp),
	.pingpong = msm8996_pp,
	.dsc_count = ARRAY_SIZE(msm8996_dsc),
	.dsc = msm8996_dsc,
	.intf_count = ARRAY_SIZE(msm8996_intf),
	.intf = msm8996_intf,
	.vbif_count = ARRAY_SIZE(msm8996_vbif),
	.vbif = msm8996_vbif,
	.perf = &msm8996_perf_data,
};

#endif
