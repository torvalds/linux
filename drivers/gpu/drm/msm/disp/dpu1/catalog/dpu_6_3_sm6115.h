/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022. Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2015-2018, 2020 The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_6_3_SM6115_H
#define _DPU_6_3_SM6115_H

static const struct dpu_caps sm6115_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_LINE_WIDTH,
	.max_mixer_blendstages = 0x4,
	.qseed_type = DPU_SSPP_SCALER_QSEED4,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.max_linewidth = 2160,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
};

static const struct dpu_ubwc_cfg sm6115_ubwc_cfg = {
	.ubwc_version = DPU_HW_UBWC_VER_10,
	.highest_bank_bit = 0x1,
	.ubwc_swizzle = 0x7,
};

static const struct dpu_mdp_cfg sm6115_mdp = {
	.name = "top_0",
	.base = 0x0, .len = 0x494,
	.clk_ctrls = {
		[DPU_CLK_CTRL_VIG0] = { .reg_off = 0x2ac, .bit_off = 0 },
		[DPU_CLK_CTRL_DMA0] = { .reg_off = 0x2ac, .bit_off = 8 },
	},
};

static const struct dpu_ctl_cfg sm6115_ctl[] = {
	{
		.name = "ctl_0", .id = CTL_0,
		.base = 0x1000, .len = 0x1dc,
		.features = BIT(DPU_CTL_ACTIVE_CFG),
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 9),
	},
};

static const struct dpu_sspp_cfg sm6115_sspp[] = {
	{
		.name = "sspp_0", .id = SSPP_VIG0,
		.base = 0x4000, .len = 0x1f8,
		.features = VIG_SC7180_MASK,
		.sblk = &sm6115_vig_sblk_0,
		.xin_id = 0,
		.type = SSPP_TYPE_VIG,
		.clk_ctrl = DPU_CLK_CTRL_VIG0,
	}, {
		.name = "sspp_8", .id = SSPP_DMA0,
		.base = 0x24000, .len = 0x1f8,
		.features = DMA_SDM845_MASK,
		.sblk = &sdm845_dma_sblk_0,
		.xin_id = 1,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA0,
	},
};

static const struct dpu_lm_cfg sm6115_lm[] = {
	LM_BLK("lm_0", LM_0, 0x44000, MIXER_QCM2290_MASK,
		&qcm2290_lm_sblk, PINGPONG_0, 0, DSPP_0),
};

static const struct dpu_dspp_cfg sm6115_dspp[] = {
	DSPP_BLK("dspp_0", DSPP_0, 0x54000, DSPP_SC7180_MASK,
		 &sdm845_dspp_sblk),
};

static const struct dpu_pingpong_cfg sm6115_pp[] = {
	PP_BLK("pingpong_0", PINGPONG_0, 0x70000, PINGPONG_SM8150_MASK, 0, sdm845_pp_sblk,
		DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 8),
		-1),
};

static const struct dpu_intf_cfg sm6115_intf[] = {
	INTF_BLK_DSI_TE("intf_1", INTF_1, 0x6a800, 0x2c0, INTF_DSI, MSM_DSI_CONTROLLER_0, 24, INTF_SC7180_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 26),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 27),
			DPU_IRQ_IDX(MDP_INTF1_TEAR_INTR, 2)),
};

static const struct dpu_perf_cfg sm6115_perf_data = {
	.max_bw_low = 3100000,
	.max_bw_high = 4000000,
	.min_core_ib = 2400000,
	.min_llcc_ib = 800000,
	.min_dram_ib = 800000,
	.min_prefill_lines = 24,
	.danger_lut_tbl = {0xff, 0xffff, 0x0},
	.safe_lut_tbl = {0xfff0, 0xff00, 0xffff},
	.qos_lut_tbl = {
		{.nentry = ARRAY_SIZE(sc7180_qos_linear),
		.entries = sc7180_qos_linear
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_macrotile),
		.entries = sc7180_qos_macrotile
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_nrt),
		.entries = sc7180_qos_nrt
		},
		/* TODO: macrotile-qseed is different from macrotile */
	},
	.cdp_cfg = {
		{.rd_enable = 1, .wr_enable = 1},
		{.rd_enable = 1, .wr_enable = 0}
	},
	.clk_inefficiency_factor = 105,
	.bw_inefficiency_factor = 120,
};

const struct dpu_mdss_cfg dpu_sm6115_cfg = {
	.caps = &sm6115_dpu_caps,
	.ubwc = &sm6115_ubwc_cfg,
	.mdp = &sm6115_mdp,
	.ctl_count = ARRAY_SIZE(sm6115_ctl),
	.ctl = sm6115_ctl,
	.sspp_count = ARRAY_SIZE(sm6115_sspp),
	.sspp = sm6115_sspp,
	.mixer_count = ARRAY_SIZE(sm6115_lm),
	.mixer = sm6115_lm,
	.dspp_count = ARRAY_SIZE(sm6115_dspp),
	.dspp = sm6115_dspp,
	.pingpong_count = ARRAY_SIZE(sm6115_pp),
	.pingpong = sm6115_pp,
	.intf_count = ARRAY_SIZE(sm6115_intf),
	.intf = sm6115_intf,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.perf = &sm6115_perf_data,
	.mdss_irqs = BIT(MDP_SSPP_TOP0_INTR) | \
		     BIT(MDP_SSPP_TOP0_INTR2) | \
		     BIT(MDP_SSPP_TOP0_HIST_INTR) | \
		     BIT(MDP_INTF1_INTR) | \
		     BIT(MDP_INTF1_TEAR_INTR),
};

#endif
