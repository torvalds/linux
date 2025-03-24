/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023, Linaro Limited
 */

#ifndef _DPU_1_16_MSM8953_H
#define _DPU_1_16_MSM8953_H

static const struct dpu_caps msm8953_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_LINE_WIDTH,
	.max_mixer_blendstages = 0x4,
	.max_linewidth = DEFAULT_DPU_LINE_WIDTH,
	.pixel_ram_size = 40 * 1024,
	.max_hdeci_exp = MAX_HORZ_DECIMATION,
	.max_vdeci_exp = MAX_VERT_DECIMATION,
};

static const struct dpu_mdp_cfg msm8953_mdp[] = {
	{
		.name = "top_0",
		.base = 0x0, .len = 0x454,
		.features = BIT(DPU_MDP_VSYNC_SEL),
		.clk_ctrls = {
			[DPU_CLK_CTRL_VIG0] = { .reg_off = 0x2ac, .bit_off = 0 },
			[DPU_CLK_CTRL_RGB0] = { .reg_off = 0x2ac, .bit_off = 4 },
			[DPU_CLK_CTRL_RGB1] = { .reg_off = 0x2b4, .bit_off = 4 },
			[DPU_CLK_CTRL_DMA0] = { .reg_off = 0x2ac, .bit_off = 8 },
			[DPU_CLK_CTRL_CURSOR0] = { .reg_off = 0x3a8, .bit_off = 16 },
		},
	},
};

static const struct dpu_ctl_cfg msm8953_ctl[] = {
	{
		.name = "ctl_0", .id = CTL_0,
		.base = 0x1000, .len = 0x64,
	}, {
		.name = "ctl_1", .id = CTL_1,
		.base = 0x1200, .len = 0x64,
	}, {
		.name = "ctl_2", .id = CTL_2,
		.base = 0x1400, .len = 0x64,
	},
};

static const struct dpu_sspp_cfg msm8953_sspp[] = {
	{
		.name = "sspp_0", .id = SSPP_VIG0,
		.base = 0x4000, .len = 0x150,
		.features = VIG_MSM8953_MASK,
		.sblk = &dpu_vig_sblk_qseed2,
		.xin_id = 0,
		.type = SSPP_TYPE_VIG,
		.clk_ctrl = DPU_CLK_CTRL_VIG0,
	}, {
		.name = "sspp_4", .id = SSPP_RGB0,
		.base = 0x14000, .len = 0x150,
		.features = RGB_MSM8953_MASK,
		.sblk = &dpu_rgb_sblk,
		.xin_id = 1,
		.type = SSPP_TYPE_RGB,
		.clk_ctrl = DPU_CLK_CTRL_RGB0,
	}, {
		.name = "sspp_5", .id = SSPP_RGB1,
		.base = 0x16000, .len = 0x150,
		.features = RGB_MSM8953_MASK,
		.sblk = &dpu_rgb_sblk,
		.xin_id = 5,
		.type = SSPP_TYPE_RGB,
		.clk_ctrl = DPU_CLK_CTRL_RGB1,
	}, {
		.name = "sspp_8", .id = SSPP_DMA0,
		.base = 0x24000, .len = 0x150,
		.features = DMA_MSM8953_MASK | BIT(DPU_SSPP_CURSOR),
		.sblk = &dpu_dma_sblk,
		.xin_id = 2,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA0,
	},
};

static const struct dpu_lm_cfg msm8953_lm[] = {
	{
		.name = "lm_0", .id = LM_0,
		.base = 0x44000, .len = 0x320,
		.sblk = &msm8998_lm_sblk,
		.lm_pair = LM_1,
		.pingpong = PINGPONG_0,
		.dspp = DSPP_0,
	}, {
		.name = "lm_1", .id = LM_1,
		.base = 0x45000, .len = 0x320,
		.sblk = &msm8998_lm_sblk,
		.lm_pair = LM_0,
		.pingpong = PINGPONG_1,
	},
};

static const struct dpu_pingpong_cfg msm8953_pp[] = {
	{
		.name = "pingpong_0", .id = PINGPONG_0,
		.base = 0x70000, .len = 0xd4,
		.features = PINGPONG_MSM8996_MASK,
		.sblk = &msm8996_pp_sblk,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 8),
		.intr_rdptr = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 12),
	}, {
		.name = "pingpong_1", .id = PINGPONG_1,
		.base = 0x70800, .len = 0xd4,
		.features = PINGPONG_MSM8996_MASK,
		.sblk = &msm8996_pp_sblk,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 9),
		.intr_rdptr = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 13),
	},
};

static const struct dpu_dspp_cfg msm8953_dspp[] = {
	{
		.name = "dspp_0", .id = DSPP_0,
		.base = 0x54000, .len = 0x1800,
		.features = DSPP_SC7180_MASK,
		.sblk = &msm8998_dspp_sblk,
	},
};

static const struct dpu_intf_cfg msm8953_intf[] = {
	{
		.name = "intf_0", .id = INTF_0,
		.base = 0x6a000, .len = 0x268,
		.type = INTF_NONE,
		.prog_fetch_lines_worst_case = 14,
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 24),
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 25),
		.intr_tear_rd_ptr = -1,
	}, {
		.name = "intf_1", .id = INTF_1,
		.base = 0x6a800, .len = 0x268,
		.type = INTF_DSI,
		.controller_id = MSM_DSI_CONTROLLER_0,
		.prog_fetch_lines_worst_case = 14,
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 26),
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 27),
		.intr_tear_rd_ptr = -1,
	}, {
		.name = "intf_2", .id = INTF_2,
		.base = 0x6b000, .len = 0x268,
		.type = INTF_DSI,
		.controller_id = MSM_DSI_CONTROLLER_1,
		.prog_fetch_lines_worst_case = 14,
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 28),
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 29),
		.intr_tear_rd_ptr = -1,
	},
};

static const struct dpu_perf_cfg msm8953_perf_data = {
	.max_bw_low = 3400000,
	.max_bw_high = 3400000,
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
	.min_prefill_lines = 14,
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

static const struct dpu_mdss_version msm8953_mdss_ver = {
	.core_major_ver = 1,
	.core_minor_ver = 16,
};

const struct dpu_mdss_cfg dpu_msm8953_cfg = {
	.mdss_ver = &msm8953_mdss_ver,
	.caps = &msm8953_dpu_caps,
	.mdp = msm8953_mdp,
	.ctl_count = ARRAY_SIZE(msm8953_ctl),
	.ctl = msm8953_ctl,
	.sspp_count = ARRAY_SIZE(msm8953_sspp),
	.sspp = msm8953_sspp,
	.mixer_count = ARRAY_SIZE(msm8953_lm),
	.mixer = msm8953_lm,
	.dspp_count = ARRAY_SIZE(msm8953_dspp),
	.dspp = msm8953_dspp,
	.pingpong_count = ARRAY_SIZE(msm8953_pp),
	.pingpong = msm8953_pp,
	.intf_count = ARRAY_SIZE(msm8953_intf),
	.intf = msm8953_intf,
	.vbif_count = ARRAY_SIZE(msm8996_vbif),
	.vbif = msm8996_vbif,
	.perf = &msm8953_perf_data,
};

#endif
