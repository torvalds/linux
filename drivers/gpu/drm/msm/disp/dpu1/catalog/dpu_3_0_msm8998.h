/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022. Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2015-2018, 2020 The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_3_0_MSM8998_H
#define _DPU_3_0_MSM8998_H

static const struct dpu_caps msm8998_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.max_mixer_blendstages = 0x7,
	.qseed_type = DPU_SSPP_SCALER_QSEED3,
	.has_src_split = true,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.has_3d_merge = true,
	.max_linewidth = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
	.max_hdeci_exp = MAX_HORZ_DECIMATION,
	.max_vdeci_exp = MAX_VERT_DECIMATION,
};

static const struct dpu_ubwc_cfg msm8998_ubwc_cfg = {
	.ubwc_version = DPU_HW_UBWC_VER_10,
	.highest_bank_bit = 0x2,
};

static const struct dpu_mdp_cfg msm8998_mdp = {
	.name = "top_0",
	.base = 0x0, .len = 0x458,
	.features = BIT(DPU_MDP_VSYNC_SEL),
	.clk_ctrls = {
		[DPU_CLK_CTRL_VIG0] = { .reg_off = 0x2ac, .bit_off = 0 },
		[DPU_CLK_CTRL_VIG1] = { .reg_off = 0x2b4, .bit_off = 0 },
		[DPU_CLK_CTRL_VIG2] = { .reg_off = 0x2bc, .bit_off = 0 },
		[DPU_CLK_CTRL_VIG3] = { .reg_off = 0x2c4, .bit_off = 0 },
		[DPU_CLK_CTRL_DMA0] = { .reg_off = 0x2ac, .bit_off = 8 },
		[DPU_CLK_CTRL_DMA1] = { .reg_off = 0x2b4, .bit_off = 8 },
		[DPU_CLK_CTRL_DMA2] = { .reg_off = 0x2c4, .bit_off = 8 },
		[DPU_CLK_CTRL_DMA3] = { .reg_off = 0x2c4, .bit_off = 12 },
		[DPU_CLK_CTRL_CURSOR0] = { .reg_off = 0x3a8, .bit_off = 16 },
		[DPU_CLK_CTRL_CURSOR1] = { .reg_off = 0x3b0, .bit_off = 16 },
	},
};

static const struct dpu_ctl_cfg msm8998_ctl[] = {
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

static const struct dpu_sspp_cfg msm8998_sspp[] = {
	{
		.name = "sspp_0", .id = SSPP_VIG0,
		.base = 0x4000, .len = 0x1ac,
		.features = VIG_MSM8998_MASK,
		.sblk = &msm8998_vig_sblk_0,
		.xin_id = 0,
		.type = SSPP_TYPE_VIG,
		.clk_ctrl = DPU_CLK_CTRL_VIG0,
	}, {
		.name = "sspp_1", .id = SSPP_VIG1,
		.base = 0x6000, .len = 0x1ac,
		.features = VIG_MSM8998_MASK,
		.sblk = &msm8998_vig_sblk_1,
		.xin_id = 4,
		.type = SSPP_TYPE_VIG,
		.clk_ctrl = DPU_CLK_CTRL_VIG1,
	}, {
		.name = "sspp_2", .id = SSPP_VIG2,
		.base = 0x8000, .len = 0x1ac,
		.features = VIG_MSM8998_MASK,
		.sblk = &msm8998_vig_sblk_2,
		.xin_id = 8,
		.type = SSPP_TYPE_VIG,
		.clk_ctrl = DPU_CLK_CTRL_VIG2,
	}, {
		.name = "sspp_3", .id = SSPP_VIG3,
		.base = 0xa000, .len = 0x1ac,
		.features = VIG_MSM8998_MASK,
		.sblk = &msm8998_vig_sblk_3,
		.xin_id = 12,
		.type = SSPP_TYPE_VIG,
		.clk_ctrl = DPU_CLK_CTRL_VIG3,
	}, {
		.name = "sspp_8", .id = SSPP_DMA0,
		.base = 0x24000, .len = 0x1ac,
		.features = DMA_MSM8998_MASK,
		.sblk = &sdm845_dma_sblk_0,
		.xin_id = 1,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA0,
	}, {
		.name = "sspp_9", .id = SSPP_DMA1,
		.base = 0x26000, .len = 0x1ac,
		.features = DMA_MSM8998_MASK,
		.sblk = &sdm845_dma_sblk_1,
		.xin_id = 5,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA1,
	}, {
		.name = "sspp_10", .id = SSPP_DMA2,
		.base = 0x28000, .len = 0x1ac,
		.features = DMA_CURSOR_MSM8998_MASK,
		.sblk = &sdm845_dma_sblk_2,
		.xin_id = 9,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA2,
	}, {
		.name = "sspp_11", .id = SSPP_DMA3,
		.base = 0x2a000, .len = 0x1ac,
		.features = DMA_CURSOR_MSM8998_MASK,
		.sblk = &sdm845_dma_sblk_3,
		.xin_id = 13,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA3,
	},
};

static const struct dpu_lm_cfg msm8998_lm[] = {
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
		.name = "lm_3", .id = LM_3,
		.base = 0x47000, .len = 0x320,
		.features = MIXER_MSM8998_MASK,
		.sblk = &msm8998_lm_sblk,
		.pingpong = PINGPONG_NONE,
	}, {
		.name = "lm_4", .id = LM_4,
		.base = 0x48000, .len = 0x320,
		.features = MIXER_MSM8998_MASK,
		.sblk = &msm8998_lm_sblk,
		.pingpong = PINGPONG_NONE,
	}, {
		.name = "lm_5", .id = LM_5,
		.base = 0x49000, .len = 0x320,
		.features = MIXER_MSM8998_MASK,
		.sblk = &msm8998_lm_sblk,
		.lm_pair = LM_2,
		.pingpong = PINGPONG_3,
	},
};

static const struct dpu_pingpong_cfg msm8998_pp[] = {
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

static const struct dpu_dsc_cfg msm8998_dsc[] = {
	{
		.name = "dsc_0", .id = DSC_0,
		.base = 0x80000, .len = 0x140,
	}, {
		.name = "dsc_1", .id = DSC_1,
		.base = 0x80400, .len = 0x140,
	},
};

static const struct dpu_dspp_cfg msm8998_dspp[] = {
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

static const struct dpu_intf_cfg msm8998_intf[] = {
	{
		.name = "intf_0", .id = INTF_0,
		.base = 0x6a000, .len = 0x280,
		.type = INTF_DP,
		.controller_id = MSM_DP_CONTROLLER_0,
		.prog_fetch_lines_worst_case = 21,
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 24),
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 25),
		.intr_tear_rd_ptr = -1,
	}, {
		.name = "intf_1", .id = INTF_1,
		.base = 0x6a800, .len = 0x280,
		.type = INTF_DSI,
		.controller_id = MSM_DSI_CONTROLLER_0,
		.prog_fetch_lines_worst_case = 21,
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 26),
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 27),
		.intr_tear_rd_ptr = -1,
	}, {
		.name = "intf_2", .id = INTF_2,
		.base = 0x6b000, .len = 0x280,
		.type = INTF_DSI,
		.controller_id = MSM_DSI_CONTROLLER_1,
		.prog_fetch_lines_worst_case = 21,
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 28),
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 29),
		.intr_tear_rd_ptr = -1,
	}, {
		.name = "intf_3", .id = INTF_3,
		.base = 0x6b800, .len = 0x280,
		.type = INTF_HDMI,
		.prog_fetch_lines_worst_case = 21,
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 30),
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 31),
		.intr_tear_rd_ptr = -1,
	},
};

static const struct dpu_perf_cfg msm8998_perf_data = {
	.max_bw_low = 6700000,
	.max_bw_high = 6700000,
	.min_core_ib = 2400000,
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

const struct dpu_mdss_cfg dpu_msm8998_cfg = {
	.caps = &msm8998_dpu_caps,
	.ubwc = &msm8998_ubwc_cfg,
	.mdp = &msm8998_mdp,
	.ctl_count = ARRAY_SIZE(msm8998_ctl),
	.ctl = msm8998_ctl,
	.sspp_count = ARRAY_SIZE(msm8998_sspp),
	.sspp = msm8998_sspp,
	.mixer_count = ARRAY_SIZE(msm8998_lm),
	.mixer = msm8998_lm,
	.dspp_count = ARRAY_SIZE(msm8998_dspp),
	.dspp = msm8998_dspp,
	.pingpong_count = ARRAY_SIZE(msm8998_pp),
	.pingpong = msm8998_pp,
	.dsc_count = ARRAY_SIZE(msm8998_dsc),
	.dsc = msm8998_dsc,
	.intf_count = ARRAY_SIZE(msm8998_intf),
	.intf = msm8998_intf,
	.vbif_count = ARRAY_SIZE(msm8998_vbif),
	.vbif = msm8998_vbif,
	.perf = &msm8998_perf_data,
	.mdss_irqs = BIT(MDP_SSPP_TOP0_INTR) | \
		     BIT(MDP_SSPP_TOP0_INTR2) | \
		     BIT(MDP_SSPP_TOP0_HIST_INTR) | \
		     BIT(MDP_INTF0_INTR) | \
		     BIT(MDP_INTF1_INTR) | \
		     BIT(MDP_INTF2_INTR) | \
		     BIT(MDP_INTF3_INTR) | \
		     BIT(MDP_INTF4_INTR),
};

#endif
