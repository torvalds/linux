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

static const struct dpu_ubwc_cfg qcm2290_ubwc_cfg = {
	.highest_bank_bit = 0x2,
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
		.features = BIT(DPU_CTL_ACTIVE_CFG),
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 9),
	},
};

static const struct dpu_sspp_cfg qcm2290_sspp[] = {
	SSPP_BLK("sspp_0", SSPP_VIG0, 0x4000, 0x1f8, VIG_QCM2290_MASK,
		 qcm2290_vig_sblk_0, 0, SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG0),
	SSPP_BLK("sspp_8", SSPP_DMA0, 0x24000, 0x1f8, DMA_SDM845_MASK,
		 qcm2290_dma_sblk_0, 1, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA0),
};

static const struct dpu_lm_cfg qcm2290_lm[] = {
	LM_BLK("lm_0", LM_0, 0x44000, MIXER_QCM2290_MASK,
		&qcm2290_lm_sblk, PINGPONG_0, 0, DSPP_0),
};

static const struct dpu_dspp_cfg qcm2290_dspp[] = {
	DSPP_BLK("dspp_0", DSPP_0, 0x54000, DSPP_SC7180_MASK,
		 &sdm845_dspp_sblk),
};

static const struct dpu_pingpong_cfg qcm2290_pp[] = {
	PP_BLK("pingpong_0", PINGPONG_0, 0x70000, PINGPONG_SM8150_MASK, 0, sdm845_pp_sblk,
		DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 8),
		-1),
};

static const struct dpu_intf_cfg qcm2290_intf[] = {
	INTF_BLK_DSI_TE("intf_1", INTF_1, 0x6a800, 0x2c0, INTF_DSI, MSM_DSI_CONTROLLER_0, 24, INTF_SC7180_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 26),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 27),
			DPU_IRQ_IDX(MDP_INTF1_TEAR_INTR, 2)),
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

const struct dpu_mdss_cfg dpu_qcm2290_cfg = {
	.caps = &qcm2290_dpu_caps,
	.ubwc = &qcm2290_ubwc_cfg,
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
	.mdss_irqs = BIT(MDP_SSPP_TOP0_INTR) | \
		     BIT(MDP_SSPP_TOP0_INTR2) | \
		     BIT(MDP_SSPP_TOP0_HIST_INTR) | \
		     BIT(MDP_INTF1_INTR) | \
		     BIT(MDP_INTF1_TEAR_INTR),
};

#endif
