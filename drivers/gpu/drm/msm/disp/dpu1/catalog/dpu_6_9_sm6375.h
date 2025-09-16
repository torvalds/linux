/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022. Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2015-2018, 2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Limited
 */

#ifndef _DPU_6_9_SM6375_H
#define _DPU_6_9_SM6375_H

static const struct dpu_caps sm6375_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_LINE_WIDTH,
	.max_mixer_blendstages = 0x4,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.max_linewidth = 2160,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
};

static const struct dpu_mdp_cfg sm6375_mdp = {
	.name = "top_0",
	.base = 0x0, .len = 0x494,
	.clk_ctrls = {
		[DPU_CLK_CTRL_VIG0] = { .reg_off = 0x2ac, .bit_off = 0 },
		[DPU_CLK_CTRL_DMA0] = { .reg_off = 0x2ac, .bit_off = 8 },
	},
};

static const struct dpu_ctl_cfg sm6375_ctl[] = {
	{
		.name = "ctl_0", .id = CTL_0,
		.base = 0x1000, .len = 0x1dc,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 9),
	},
};

static const struct dpu_sspp_cfg sm6375_sspp[] = {
	{
		.name = "sspp_0", .id = SSPP_VIG0,
		.base = 0x4000, .len = 0x1f8,
		.features = VIG_SDM845_MASK_NO_SDMA,
		.sblk = &dpu_vig_sblk_qseed3_3_0,
		.xin_id = 0,
		.type = SSPP_TYPE_VIG,
		.clk_ctrl = DPU_CLK_CTRL_VIG0,
	}, {
		.name = "sspp_8", .id = SSPP_DMA0,
		.base = 0x24000, .len = 0x1f8,
		.features = DMA_SDM845_MASK_NO_SDMA,
		.sblk = &dpu_dma_sblk,
		.xin_id = 1,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA0,
	},
};

static const struct dpu_lm_cfg sm6375_lm[] = {
	{
		.name = "lm_0", .id = LM_0,
		.base = 0x44000, .len = 0x320,
		.sblk = &qcm2290_lm_sblk,
		.lm_pair = 0,
		.pingpong = PINGPONG_0,
		.dspp = DSPP_0,
	},
};

static const struct dpu_dspp_cfg sm6375_dspp[] = {
	{
		.name = "dspp_0", .id = DSPP_0,
		.base = 0x54000, .len = 0x1800,
		.sblk = &sdm845_dspp_sblk,
	},
};

static const struct dpu_pingpong_cfg sm6375_pp[] = {
	{
		.name = "pingpong_0", .id = PINGPONG_0,
		.base = 0x70000, .len = 0xd4,
		.sblk = &sdm845_pp_sblk,
		.merge_3d = 0,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 8),
	},
};

static const struct dpu_dsc_cfg sm6375_dsc[] = {
	{
		.name = "dsc_0", .id = DSC_0,
		.base = 0x80000, .len = 0x140,
	},
};

static const struct dpu_intf_cfg sm6375_intf[] = {
	{
		.name = "intf_1", .id = INTF_1,
		.base = 0x6a800, .len = 0x2c0,
		.type = INTF_DSI,
		.controller_id = MSM_DSI_CONTROLLER_0,
		.prog_fetch_lines_worst_case = 24,
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 26),
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 27),
		.intr_tear_rd_ptr = DPU_IRQ_IDX(MDP_INTF1_TEAR_INTR, 2),
	},
};

static const struct dpu_perf_cfg sm6375_perf_data = {
	.max_bw_low = 5200000,
	.max_bw_high = 6200000,
	.min_core_ib = 2500000,
	.min_llcc_ib = 0, /* No LLCC on this SoC */
	.min_dram_ib = 1600000,
	.min_prefill_lines = 24,
	/* TODO: confirm danger_lut_tbl */
	.danger_lut_tbl = {0xffff, 0xffff, 0x0},
	.safe_lut_tbl = {0xfe00, 0xfe00, 0xffff},
	.qos_lut_tbl = {
		{.nentry = ARRAY_SIZE(sm6350_qos_linear_macrotile),
		.entries = sm6350_qos_linear_macrotile
		},
		{.nentry = ARRAY_SIZE(sm6350_qos_linear_macrotile),
		.entries = sm6350_qos_linear_macrotile
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_nrt),
		.entries = sc7180_qos_nrt
		},
	},
	.cdp_cfg = {
		{.rd_enable = 1, .wr_enable = 1},
		{.rd_enable = 1, .wr_enable = 0}
	},
	.clk_inefficiency_factor = 105,
	.bw_inefficiency_factor = 120,
};

static const struct dpu_mdss_version sm6375_mdss_ver = {
	.core_major_ver = 6,
	.core_minor_ver = 9,
};

const struct dpu_mdss_cfg dpu_sm6375_cfg = {
	.mdss_ver = &sm6375_mdss_ver,
	.caps = &sm6375_dpu_caps,
	.mdp = &sm6375_mdp,
	.ctl_count = ARRAY_SIZE(sm6375_ctl),
	.ctl = sm6375_ctl,
	.sspp_count = ARRAY_SIZE(sm6375_sspp),
	.sspp = sm6375_sspp,
	.mixer_count = ARRAY_SIZE(sm6375_lm),
	.mixer = sm6375_lm,
	.dspp_count = ARRAY_SIZE(sm6375_dspp),
	.dspp = sm6375_dspp,
	.dsc_count = ARRAY_SIZE(sm6375_dsc),
	.dsc = sm6375_dsc,
	.pingpong_count = ARRAY_SIZE(sm6375_pp),
	.pingpong = sm6375_pp,
	.intf_count = ARRAY_SIZE(sm6375_intf),
	.intf = sm6375_intf,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.perf = &sm6375_perf_data,
};

#endif
