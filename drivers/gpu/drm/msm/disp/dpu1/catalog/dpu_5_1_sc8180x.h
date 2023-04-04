/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022. Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2015-2018, 2020 The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_5_1_SC8180X_H
#define _DPU_5_1_SC8180X_H

static const struct dpu_caps sc8180x_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.max_mixer_blendstages = 0xb,
	.qseed_type = DPU_SSPP_SCALER_QSEED3,
	.has_src_split = true,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.has_3d_merge = true,
	.max_linewidth = 4096,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
	.max_hdeci_exp = MAX_HORZ_DECIMATION,
	.max_vdeci_exp = MAX_VERT_DECIMATION,
};

static const struct dpu_ubwc_cfg sc8180x_ubwc_cfg = {
	.ubwc_version = DPU_HW_UBWC_VER_30,
	.highest_bank_bit = 0x3,
};

static const struct dpu_mdp_cfg sc8180x_mdp[] = {
	{
	.name = "top_0", .id = MDP_TOP,
	.base = 0x0, .len = 0x45c,
	.features = BIT(DPU_MDP_AUDIO_SELECT),
	.clk_ctrls[DPU_CLK_CTRL_VIG0] = { .reg_off = 0x2ac, .bit_off = 0 },
	.clk_ctrls[DPU_CLK_CTRL_VIG1] = { .reg_off = 0x2b4, .bit_off = 0 },
	.clk_ctrls[DPU_CLK_CTRL_VIG2] = { .reg_off = 0x2bc, .bit_off = 0 },
	.clk_ctrls[DPU_CLK_CTRL_VIG3] = { .reg_off = 0x2c4, .bit_off = 0 },
	.clk_ctrls[DPU_CLK_CTRL_DMA0] = { .reg_off = 0x2ac, .bit_off = 8 },
	.clk_ctrls[DPU_CLK_CTRL_DMA1] = { .reg_off = 0x2b4, .bit_off = 8 },
	.clk_ctrls[DPU_CLK_CTRL_DMA2] = { .reg_off = 0x2bc, .bit_off = 8 },
	.clk_ctrls[DPU_CLK_CTRL_DMA3] = { .reg_off = 0x2c4, .bit_off = 8 },
	},
};

static const struct dpu_intf_cfg sc8180x_intf[] = {
	INTF_BLK("intf_0", INTF_0, 0x6a000, 0x280, INTF_DP, MSM_DP_CONTROLLER_0, 24, INTF_SC7180_MASK, MDP_SSPP_TOP0_INTR, 24, 25),
	INTF_BLK("intf_1", INTF_1, 0x6a800, 0x2bc, INTF_DSI, 0, 24, INTF_SC7180_MASK, MDP_SSPP_TOP0_INTR, 26, 27),
	INTF_BLK("intf_2", INTF_2, 0x6b000, 0x2bc, INTF_DSI, 1, 24, INTF_SC7180_MASK, MDP_SSPP_TOP0_INTR, 28, 29),
	/* INTF_3 is for MST, wired to INTF_DP 0 and 1, use dummy index until this is supported */
	INTF_BLK("intf_3", INTF_3, 0x6b800, 0x280, INTF_DP, 999, 24, INTF_SC7180_MASK, MDP_SSPP_TOP0_INTR, 30, 31),
	INTF_BLK("intf_4", INTF_4, 0x6c000, 0x280, INTF_DP, MSM_DP_CONTROLLER_1, 24, INTF_SC7180_MASK, MDP_SSPP_TOP0_INTR, 20, 21),
	INTF_BLK("intf_5", INTF_5, 0x6c800, 0x280, INTF_DP, MSM_DP_CONTROLLER_2, 24, INTF_SC7180_MASK, MDP_SSPP_TOP0_INTR, 22, 23),
};

static const struct dpu_perf_cfg sc8180x_perf_data = {
	.max_bw_low = 9600000,
	.max_bw_high = 9600000,
	.min_core_ib = 2400000,
	.min_llcc_ib = 800000,
	.min_dram_ib = 800000,
	.danger_lut_tbl = {0xf, 0xffff, 0x0},
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

static const struct dpu_mdss_cfg sc8180x_dpu_cfg = {
	.caps = &sc8180x_dpu_caps,
	.ubwc = &sc8180x_ubwc_cfg,
	.mdp_count = ARRAY_SIZE(sc8180x_mdp),
	.mdp = sc8180x_mdp,
	.ctl_count = ARRAY_SIZE(sm8150_ctl),
	.ctl = sm8150_ctl,
	.sspp_count = ARRAY_SIZE(sdm845_sspp),
	.sspp = sdm845_sspp,
	.mixer_count = ARRAY_SIZE(sm8150_lm),
	.mixer = sm8150_lm,
	.pingpong_count = ARRAY_SIZE(sm8150_pp),
	.pingpong = sm8150_pp,
	.merge_3d_count = ARRAY_SIZE(sm8150_merge_3d),
	.merge_3d = sm8150_merge_3d,
	.intf_count = ARRAY_SIZE(sc8180x_intf),
	.intf = sc8180x_intf,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.reg_dma_count = 1,
	.dma_cfg = &sm8150_regdma,
	.perf = &sc8180x_perf_data,
	.mdss_irqs = IRQ_SC8180X_MASK,
};

#endif
