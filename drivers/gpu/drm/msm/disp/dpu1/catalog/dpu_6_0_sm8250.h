/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022. Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2015-2018, 2020 The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_6_0_SM8250_H
#define _DPU_6_0_SM8250_H

static const struct dpu_caps sm8250_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.max_mixer_blendstages = 0xb,
	.qseed_type = DPU_SSPP_SCALER_QSEED4,
	.has_src_split = true,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.has_3d_merge = true,
	.max_linewidth = 4096,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
};

static const struct dpu_ubwc_cfg sm8250_ubwc_cfg = {
	.ubwc_version = DPU_HW_UBWC_VER_40,
	.highest_bank_bit = 0x3, /* TODO: 2 for LP_DDR4 */
	.ubwc_swizzle = 0x6,
};

static const struct dpu_mdp_cfg sm8250_mdp[] = {
	{
	.name = "top_0", .id = MDP_TOP,
	.base = 0x0, .len = 0x494,
	.features = 0,
	.clk_ctrls[DPU_CLK_CTRL_VIG0] = { .reg_off = 0x2ac, .bit_off = 0 },
	.clk_ctrls[DPU_CLK_CTRL_VIG1] = { .reg_off = 0x2b4, .bit_off = 0 },
	.clk_ctrls[DPU_CLK_CTRL_VIG2] = { .reg_off = 0x2bc, .bit_off = 0 },
	.clk_ctrls[DPU_CLK_CTRL_VIG3] = { .reg_off = 0x2c4, .bit_off = 0 },
	.clk_ctrls[DPU_CLK_CTRL_DMA0] = { .reg_off = 0x2ac, .bit_off = 8 },
	.clk_ctrls[DPU_CLK_CTRL_DMA1] = { .reg_off = 0x2b4, .bit_off = 8 },
	.clk_ctrls[DPU_CLK_CTRL_DMA2] = { .reg_off = 0x2bc, .bit_off = 8 },
	.clk_ctrls[DPU_CLK_CTRL_DMA3] = { .reg_off = 0x2c4, .bit_off = 8 },
	.clk_ctrls[DPU_CLK_CTRL_REG_DMA] = { .reg_off = 0x2bc, .bit_off = 20 },
	.clk_ctrls[DPU_CLK_CTRL_WB2] = { .reg_off = 0x3b8, .bit_off = 24 },
	},
};

/* FIXME: get rid of DPU_CTL_SPLIT_DISPLAY in favour of proper ACTIVE_CTL support */
static const struct dpu_ctl_cfg sm8250_ctl[] = {
	{
	.name = "ctl_0", .id = CTL_0,
	.base = 0x1000, .len = 0x1e0,
	.features = BIT(DPU_CTL_ACTIVE_CFG) | BIT(DPU_CTL_SPLIT_DISPLAY),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 9),
	},
	{
	.name = "ctl_1", .id = CTL_1,
	.base = 0x1200, .len = 0x1e0,
	.features = BIT(DPU_CTL_ACTIVE_CFG) | BIT(DPU_CTL_SPLIT_DISPLAY),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 10),
	},
	{
	.name = "ctl_2", .id = CTL_2,
	.base = 0x1400, .len = 0x1e0,
	.features = BIT(DPU_CTL_ACTIVE_CFG),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 11),
	},
	{
	.name = "ctl_3", .id = CTL_3,
	.base = 0x1600, .len = 0x1e0,
	.features = BIT(DPU_CTL_ACTIVE_CFG),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 12),
	},
	{
	.name = "ctl_4", .id = CTL_4,
	.base = 0x1800, .len = 0x1e0,
	.features = BIT(DPU_CTL_ACTIVE_CFG),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 13),
	},
	{
	.name = "ctl_5", .id = CTL_5,
	.base = 0x1a00, .len = 0x1e0,
	.features = BIT(DPU_CTL_ACTIVE_CFG),
	.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 23),
	},
};

static const struct dpu_sspp_cfg sm8250_sspp[] = {
	SSPP_BLK("sspp_0", SSPP_VIG0, 0x4000, 0x1f8, VIG_SC7180_MASK_SDMA,
		sm8250_vig_sblk_0, 0, SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG0),
	SSPP_BLK("sspp_1", SSPP_VIG1, 0x6000, 0x1f8, VIG_SC7180_MASK_SDMA,
		sm8250_vig_sblk_1, 4, SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG1),
	SSPP_BLK("sspp_2", SSPP_VIG2, 0x8000, 0x1f8, VIG_SC7180_MASK_SDMA,
		sm8250_vig_sblk_2, 8, SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG2),
	SSPP_BLK("sspp_3", SSPP_VIG3, 0xa000, 0x1f8, VIG_SC7180_MASK_SDMA,
		sm8250_vig_sblk_3, 12, SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG3),
	SSPP_BLK("sspp_8", SSPP_DMA0, 0x24000, 0x1f8, DMA_SDM845_MASK_SDMA,
		sdm845_dma_sblk_0, 1, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA0),
	SSPP_BLK("sspp_9", SSPP_DMA1, 0x26000, 0x1f8, DMA_SDM845_MASK_SDMA,
		sdm845_dma_sblk_1, 5, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA1),
	SSPP_BLK("sspp_10", SSPP_DMA2, 0x28000, 0x1f8, DMA_CURSOR_SDM845_MASK_SDMA,
		sdm845_dma_sblk_2, 9, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA2),
	SSPP_BLK("sspp_11", SSPP_DMA3, 0x2a000, 0x1f8, DMA_CURSOR_SDM845_MASK_SDMA,
		sdm845_dma_sblk_3, 13, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA3),
};

static const struct dpu_lm_cfg sm8250_lm[] = {
	LM_BLK("lm_0", LM_0, 0x44000, MIXER_SDM845_MASK,
		&sdm845_lm_sblk, PINGPONG_0, LM_1, DSPP_0),
	LM_BLK("lm_1", LM_1, 0x45000, MIXER_SDM845_MASK,
		&sdm845_lm_sblk, PINGPONG_1, LM_0, DSPP_1),
	LM_BLK("lm_2", LM_2, 0x46000, MIXER_SDM845_MASK,
		&sdm845_lm_sblk, PINGPONG_2, LM_3, 0),
	LM_BLK("lm_3", LM_3, 0x47000, MIXER_SDM845_MASK,
		&sdm845_lm_sblk, PINGPONG_3, LM_2, 0),
	LM_BLK("lm_4", LM_4, 0x48000, MIXER_SDM845_MASK,
		&sdm845_lm_sblk, PINGPONG_4, LM_5, 0),
	LM_BLK("lm_5", LM_5, 0x49000, MIXER_SDM845_MASK,
		&sdm845_lm_sblk, PINGPONG_5, LM_4, 0),
};

static const struct dpu_dspp_cfg sm8250_dspp[] = {
	DSPP_BLK("dspp_0", DSPP_0, 0x54000, DSPP_SC7180_MASK,
		 &sdm845_dspp_sblk),
	DSPP_BLK("dspp_1", DSPP_1, 0x56000, DSPP_SC7180_MASK,
		 &sdm845_dspp_sblk),
	DSPP_BLK("dspp_2", DSPP_2, 0x58000, DSPP_SC7180_MASK,
		 &sdm845_dspp_sblk),
	DSPP_BLK("dspp_3", DSPP_3, 0x5a000, DSPP_SC7180_MASK,
		 &sdm845_dspp_sblk),
};

static const struct dpu_pingpong_cfg sm8250_pp[] = {
	PP_BLK("pingpong_0", PINGPONG_0, 0x70000, PINGPONG_SM8150_MASK, MERGE_3D_0, sdm845_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 8),
			-1),
	PP_BLK("pingpong_1", PINGPONG_1, 0x70800, PINGPONG_SM8150_MASK, MERGE_3D_0, sdm845_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 9),
			-1),
	PP_BLK("pingpong_2", PINGPONG_2, 0x71000, PINGPONG_SM8150_MASK, MERGE_3D_1, sdm845_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 10),
			-1),
	PP_BLK("pingpong_3", PINGPONG_3, 0x71800, PINGPONG_SM8150_MASK, MERGE_3D_1, sdm845_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 11),
			-1),
	PP_BLK("pingpong_4", PINGPONG_4, 0x72000, PINGPONG_SM8150_MASK, MERGE_3D_2, sdm845_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 30),
			-1),
	PP_BLK("pingpong_5", PINGPONG_5, 0x72800, PINGPONG_SM8150_MASK, MERGE_3D_2, sdm845_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 31),
			-1),
};

static const struct dpu_merge_3d_cfg sm8250_merge_3d[] = {
	MERGE_3D_BLK("merge_3d_0", MERGE_3D_0, 0x83000),
	MERGE_3D_BLK("merge_3d_1", MERGE_3D_1, 0x83100),
	MERGE_3D_BLK("merge_3d_2", MERGE_3D_2, 0x83200),
};

static const struct dpu_dsc_cfg sm8250_dsc[] = {
	DSC_BLK("dsc_0", DSC_0, 0x80000, BIT(DPU_DSC_OUTPUT_CTRL)),
	DSC_BLK("dsc_1", DSC_1, 0x80400, BIT(DPU_DSC_OUTPUT_CTRL)),
	DSC_BLK("dsc_2", DSC_2, 0x80800, BIT(DPU_DSC_OUTPUT_CTRL)),
	DSC_BLK("dsc_3", DSC_3, 0x80c00, BIT(DPU_DSC_OUTPUT_CTRL)),
};

static const struct dpu_intf_cfg sm8250_intf[] = {
	INTF_BLK("intf_0", INTF_0, 0x6a000, 0x280, INTF_DP, MSM_DP_CONTROLLER_0, 24, INTF_SC7180_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 24),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 25)),
	INTF_BLK_DSI_TE("intf_1", INTF_1, 0x6a800, 0x2c0, INTF_DSI, MSM_DSI_CONTROLLER_0, 24, INTF_SC7180_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 26),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 27),
			DPU_IRQ_IDX(MDP_INTF1_TEAR_INTR, 2)),
	INTF_BLK_DSI_TE("intf_2", INTF_2, 0x6b000, 0x2c0, INTF_DSI, MSM_DSI_CONTROLLER_1, 24, INTF_SC7180_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 28),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 29),
			DPU_IRQ_IDX(MDP_INTF2_TEAR_INTR, 2)),
	INTF_BLK("intf_3", INTF_3, 0x6b800, 0x280, INTF_DP, MSM_DP_CONTROLLER_1, 24, INTF_SC7180_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 30),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 31)),
};

static const struct dpu_wb_cfg sm8250_wb[] = {
	WB_BLK("wb_2", WB_2, 0x65000, WB_SM8250_MASK, DPU_CLK_CTRL_WB2, 6,
			VBIF_RT, MDP_SSPP_TOP0_INTR, 4096, 4),
};

static const struct dpu_perf_cfg sm8250_perf_data = {
	.max_bw_low = 13700000,
	.max_bw_high = 16600000,
	.min_core_ib = 4800000,
	.min_llcc_ib = 0,
	.min_dram_ib = 800000,
	.min_prefill_lines = 35,
	.danger_lut_tbl = {0xf, 0xffff, 0x0},
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

const struct dpu_mdss_cfg dpu_sm8250_cfg = {
	.caps = &sm8250_dpu_caps,
	.ubwc = &sm8250_ubwc_cfg,
	.mdp_count = ARRAY_SIZE(sm8250_mdp),
	.mdp = sm8250_mdp,
	.ctl_count = ARRAY_SIZE(sm8250_ctl),
	.ctl = sm8250_ctl,
	.sspp_count = ARRAY_SIZE(sm8250_sspp),
	.sspp = sm8250_sspp,
	.mixer_count = ARRAY_SIZE(sm8250_lm),
	.mixer = sm8250_lm,
	.dspp_count = ARRAY_SIZE(sm8250_dspp),
	.dspp = sm8250_dspp,
	.dsc_count = ARRAY_SIZE(sm8250_dsc),
	.dsc = sm8250_dsc,
	.pingpong_count = ARRAY_SIZE(sm8250_pp),
	.pingpong = sm8250_pp,
	.merge_3d_count = ARRAY_SIZE(sm8250_merge_3d),
	.merge_3d = sm8250_merge_3d,
	.intf_count = ARRAY_SIZE(sm8250_intf),
	.intf = sm8250_intf,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.wb_count = ARRAY_SIZE(sm8250_wb),
	.wb = sm8250_wb,
	.perf = &sm8250_perf_data,
	.mdss_irqs = BIT(MDP_SSPP_TOP0_INTR) | \
		     BIT(MDP_SSPP_TOP0_INTR2) | \
		     BIT(MDP_SSPP_TOP0_HIST_INTR) | \
		     BIT(MDP_INTF0_INTR) | \
		     BIT(MDP_INTF1_INTR) | \
		     BIT(MDP_INTF1_TEAR_INTR) | \
		     BIT(MDP_INTF2_INTR) | \
		     BIT(MDP_INTF2_TEAR_INTR) | \
		     BIT(MDP_INTF3_INTR) | \
		     BIT(MDP_INTF4_INTR),
};

#endif
