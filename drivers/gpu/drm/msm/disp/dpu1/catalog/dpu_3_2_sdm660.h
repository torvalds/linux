/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023. Linaro Inc. All rights reserved.
 */

#ifndef _DPU_3_2_SDM660_H
#define _DPU_3_2_SDM660_H

static const struct dpu_caps sdm660_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.max_mixer_blendstages = 0x7,
	.has_src_split = true,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.has_3d_merge = true,
	.max_linewidth = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
	.max_hdeci_exp = MAX_HORZ_DECIMATION,
	.max_vdeci_exp = MAX_VERT_DECIMATION,
};

static const struct dpu_mdp_cfg sdm660_mdp = {
	.name = "top_0",
	.base = 0x0, .len = 0x458,
	.features = BIT(DPU_MDP_VSYNC_SEL),
	.clk_ctrls = {
		[DPU_CLK_CTRL_VIG0] = { .reg_off = 0x2ac, .bit_off = 0 },
		[DPU_CLK_CTRL_VIG1] = { .reg_off = 0x2b4, .bit_off = 0 },
		[DPU_CLK_CTRL_DMA0] = { .reg_off = 0x2ac, .bit_off = 8 },
		[DPU_CLK_CTRL_DMA1] = { .reg_off = 0x2b4, .bit_off = 8 },
		[DPU_CLK_CTRL_DMA2] = { .reg_off = 0x2c4, .bit_off = 8 },
		[DPU_CLK_CTRL_CURSOR0] = { .reg_off = 0x3a8, .bit_off = 16 },
	},
};

static const struct dpu_ctl_cfg sdm660_ctl[] = {
	{
		.name = "ctl_0", .id = CTL_0,
		.base = 0x1000, .len = 0x94,
		.features = BIT(DPU_CTL_SPLIT_DISPLAY),
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 9),
	}, {
		.name = "ctl_1", .id = CTL_1,
		.base = 0x1200, .len = 0x94,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 10),
	}, {
		.name = "ctl_2", .id = CTL_2,
		.base = 0x1400, .len = 0x94,
		.features = BIT(DPU_CTL_SPLIT_DISPLAY),
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 11),
	}, {
		.name = "ctl_3", .id = CTL_3,
		.base = 0x1600, .len = 0x94,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 12),
	}, {
		.name = "ctl_4", .id = CTL_4,
		.base = 0x1800, .len = 0x94,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 13),
	},
};

static const struct dpu_sspp_cfg sdm660_sspp[] = {
	{
		.name = "sspp_0", .id = SSPP_VIG0,
		.base = 0x4000, .len = 0x1ac,
		.features = VIG_MSM8998_MASK,
		.sblk = &dpu_vig_sblk_qseed3_1_2,
		.xin_id = 0,
		.type = SSPP_TYPE_VIG,
		.clk_ctrl = DPU_CLK_CTRL_VIG0,
	}, {
		.name = "sspp_1", .id = SSPP_VIG1,
		.base = 0x6000, .len = 0x1ac,
		.features = VIG_MSM8998_MASK,
		.sblk = &dpu_vig_sblk_qseed3_1_2,
		.xin_id = 4,
		.type = SSPP_TYPE_VIG,
		.clk_ctrl = DPU_CLK_CTRL_VIG1,
	}, {
		.name = "sspp_8", .id = SSPP_DMA0,
		.base = 0x24000, .len = 0x1ac,
		.features = DMA_MSM8998_MASK,
		.sblk = &dpu_dma_sblk,
		.xin_id = 1,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA0,
	}, {
		.name = "sspp_9", .id = SSPP_DMA1,
		.base = 0x26000, .len = 0x1ac,
		.features = DMA_MSM8998_MASK,
		.sblk = &dpu_dma_sblk,
		.xin_id = 5,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA1,
	}, {
		.name = "sspp_10", .id = SSPP_DMA2,
		.base = 0x28000, .len = 0x1ac,
		.features = DMA_CURSOR_MSM8998_MASK,
		.sblk = &dpu_dma_sblk,
		.xin_id = 9,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA2,
	},
};

static const struct dpu_lm_cfg sdm660_lm[] = {
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

static const struct dpu_pingpong_cfg sdm660_pp[] = {
	{
		.name = "pingpong_0", .id = PINGPONG_0,
		.base = 0x70000, .len = 0xd4,
		.features = PINGPONG_SDM845_TE2_MASK,
		.sblk = &sdm845_pp_sblk_te,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 8),
		.intr_rdptr = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 12),
	}, {
		.name = "pingpong_1", .id = PINGPONG_1,
		.base = 0x70800, .len = 0xd4,
		.features = PINGPONG_SDM845_TE2_MASK,
		.sblk = &sdm845_pp_sblk_te,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 9),
		.intr_rdptr = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 13),
	}, {
		.name = "pingpong_2", .id = PINGPONG_2,
		.base = 0x71000, .len = 0xd4,
		.features = PINGPONG_SDM845_MASK,
		.sblk = &sdm845_pp_sblk,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 10),
		.intr_rdptr = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 14),
	}, {
		.name = "pingpong_3", .id = PINGPONG_3,
		.base = 0x71800, .len = 0xd4,
		.features = PINGPONG_SDM845_MASK,
		.sblk = &sdm845_pp_sblk,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 11),
		.intr_rdptr = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 15),
	},
};

static const struct dpu_dsc_cfg sdm660_dsc[] = {
	{
		.name = "dsc_0", .id = DSC_0,
		.base = 0x80000, .len = 0x140,
	}, {
		.name = "dsc_1", .id = DSC_1,
		.base = 0x80400, .len = 0x140,
	},
};

static const struct dpu_dspp_cfg sdm660_dspp[] = {
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

static const struct dpu_intf_cfg sdm660_intf[] = {
	{
		.name = "intf_0", .id = INTF_0,
		.base = 0x6a000, .len = 0x280,
		.type = INTF_DP,
		.controller_id = MSM_DP_CONTROLLER_0,
		.prog_fetch_lines_worst_case = 21,
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 24),
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 25),
	}, {
		.name = "intf_1", .id = INTF_1,
		.base = 0x6a800, .len = 0x280,
		.type = INTF_DSI,
		.controller_id = MSM_DSI_CONTROLLER_0,
		.prog_fetch_lines_worst_case = 21,
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 26),
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 27),
	}, {
		.name = "intf_2", .id = INTF_2,
		.base = 0x6b000, .len = 0x280,
		.type = INTF_DSI,
		.controller_id = MSM_DSI_CONTROLLER_1,
		.prog_fetch_lines_worst_case = 21,
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 28),
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 29),
	},
};

static const struct dpu_perf_cfg sdm660_perf_data = {
	.max_bw_low = 6600000,
	.max_bw_high = 6600000,
	.min_core_ib = 3100000,
	.min_llcc_ib = 800000,
	.min_dram_ib = 800000,
	.undersized_prefill_lines = 2,
	.xtra_prefill_lines = 2,
	.dest_scale_prefill_lines = 3,
	.macrotile_prefill_lines = 4,
	.yuv_nv12_prefill_lines = 8,
	.linear_prefill_lines = 1,
	.downscaling_prefill_lines = 1,
	.amortizable_threshold = 25,
	.min_prefill_lines = 25,
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
	.clk_inefficiency_factor = 200,
	.bw_inefficiency_factor = 120,
};

static const struct dpu_mdss_version sdm660_mdss_ver = {
	.core_major_ver = 3,
	.core_minor_ver = 2,
};

const struct dpu_mdss_cfg dpu_sdm660_cfg = {
	.mdss_ver = &sdm660_mdss_ver,
	.caps = &sdm660_dpu_caps,
	.mdp = &sdm660_mdp,
	.cdm = &dpu_cdm_1_x_4_x,
	.ctl_count = ARRAY_SIZE(sdm660_ctl),
	.ctl = sdm660_ctl,
	.sspp_count = ARRAY_SIZE(sdm660_sspp),
	.sspp = sdm660_sspp,
	.mixer_count = ARRAY_SIZE(sdm660_lm),
	.mixer = sdm660_lm,
	.dspp_count = ARRAY_SIZE(sdm660_dspp),
	.dspp = sdm660_dspp,
	.pingpong_count = ARRAY_SIZE(sdm660_pp),
	.pingpong = sdm660_pp,
	.dsc_count = ARRAY_SIZE(sdm660_dsc),
	.dsc = sdm660_dsc,
	.intf_count = ARRAY_SIZE(sdm660_intf),
	.intf = sdm660_intf,
	.vbif_count = ARRAY_SIZE(msm8998_vbif),
	.vbif = msm8998_vbif,
	.perf = &sdm660_perf_data,
};

#endif
