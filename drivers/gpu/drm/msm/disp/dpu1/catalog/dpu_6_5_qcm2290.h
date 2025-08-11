/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022. Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2015-2018, 2020 The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_6_5_QCM2290_H
#define _DPU_6_5_QCM2290_H

static const struct dpu_caps qcm2290_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_LINE_WIDTH,
	.max_mixer_blendstages = 0x4,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.max_linewidth = 2160,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
};

static const struct dpu_mdp_cfg qcm2290_mdp = {
	.name = "top_0",
	.base = 0x0, .len = 0x494,
	.clk_ctrls = {
		[DPU_CLK_CTRL_VIG0] = { .reg_off = 0x2ac, .bit_off = 0 },
		[DPU_CLK_CTRL_DMA0] = { .reg_off = 0x2ac, .bit_off = 8 },
	},
};

static const struct dpu_ctl_cfg qcm2290_ctl[] = {
	{
		.name = "ctl_0", .id = CTL_0,
		.base = 0x1000, .len = 0x1dc,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 9),
	},
};

static const struct dpu_sspp_cfg qcm2290_sspp[] = {
	{
		.name = "sspp_0", .id = SSPP_VIG0,
		.base = 0x4000, .len = 0x1f8,
		.features = VIG_QCM2290_MASK,
		.sblk = &dpu_vig_sblk_noscale,
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

static const struct dpu_lm_cfg qcm2290_lm[] = {
	{
		.name = "lm_0", .id = LM_0,
		.base = 0x44000, .len = 0x320,
		.sblk = &qcm2290_lm_sblk,
		.pingpong = PINGPONG_0,
		.dspp = DSPP_0,
	},
};

static const struct dpu_dspp_cfg qcm2290_dspp[] = {
	{
		.name = "dspp_0", .id = DSPP_0,
		.base = 0x54000, .len = 0x1800,
		.sblk = &sdm845_dspp_sblk,
	},
};

static const struct dpu_pingpong_cfg qcm2290_pp[] = {
	{
		.name = "pingpong_0", .id = PINGPONG_0,
		.base = 0x70000, .len = 0xd4,
		.sblk = &sdm845_pp_sblk,
		.merge_3d = 0,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 8),
	},
};

static const struct dpu_intf_cfg qcm2290_intf[] = {
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

static const struct dpu_perf_cfg qcm2290_perf_data = {
	.max_bw_low = 2700000,
	.max_bw_high = 2700000,
	.min_core_ib = 1300000,
	.min_llcc_ib = 0,
	.min_dram_ib = 1600000,
	.min_prefill_lines = 24,
	.danger_lut_tbl = {0xff, 0x0, 0x0},
	.safe_lut_tbl = {0xfff0, 0x0, 0x0},
	.qos_lut_tbl = {
		{.nentry = ARRAY_SIZE(qcm2290_qos_linear),
		.entries = qcm2290_qos_linear
		},
	},
	.cdp_cfg = {
		{.rd_enable = 1, .wr_enable = 1},
		{.rd_enable = 1, .wr_enable = 0}
	},
	.clk_inefficiency_factor = 105,
	.bw_inefficiency_factor = 120,
};

static const struct dpu_mdss_version qcm2290_mdss_ver = {
	.core_major_ver = 6,
	.core_minor_ver = 5,
};

const struct dpu_mdss_cfg dpu_qcm2290_cfg = {
	.mdss_ver = &qcm2290_mdss_ver,
	.caps = &qcm2290_dpu_caps,
	.mdp = &qcm2290_mdp,
	.ctl_count = ARRAY_SIZE(qcm2290_ctl),
	.ctl = qcm2290_ctl,
	.sspp_count = ARRAY_SIZE(qcm2290_sspp),
	.sspp = qcm2290_sspp,
	.mixer_count = ARRAY_SIZE(qcm2290_lm),
	.mixer = qcm2290_lm,
	.dspp_count = ARRAY_SIZE(qcm2290_dspp),
	.dspp = qcm2290_dspp,
	.pingpong_count = ARRAY_SIZE(qcm2290_pp),
	.pingpong = qcm2290_pp,
	.intf_count = ARRAY_SIZE(qcm2290_intf),
	.intf = qcm2290_intf,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.perf = &qcm2290_perf_data,
};

#endif
