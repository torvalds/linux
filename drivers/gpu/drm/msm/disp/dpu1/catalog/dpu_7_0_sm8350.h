/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022. Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2015-2018, 2020 The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_7_0_SM8350_H
#define _DPU_7_0_SM8350_H

static const struct dpu_caps sm8350_dpu_caps = {
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

static const struct dpu_ubwc_cfg sm8350_ubwc_cfg = {
	.ubwc_version = DPU_HW_UBWC_VER_40,
	.highest_bank_bit = 0x3, /* TODO: 2 for LP_DDR4 */
};

static const struct dpu_mdp_cfg sm8350_mdp = {
	.name = "top_0",
	.base = 0x0, .len = 0x494,
	.clk_ctrls = {
		[DPU_CLK_CTRL_VIG0] = { .reg_off = 0x2ac, .bit_off = 0 },
		[DPU_CLK_CTRL_VIG1] = { .reg_off = 0x2b4, .bit_off = 0 },
		[DPU_CLK_CTRL_VIG2] = { .reg_off = 0x2bc, .bit_off = 0 },
		[DPU_CLK_CTRL_VIG3] = { .reg_off = 0x2c4, .bit_off = 0 },
		[DPU_CLK_CTRL_DMA0] = { .reg_off = 0x2ac, .bit_off = 8 },
		[DPU_CLK_CTRL_DMA1] = { .reg_off = 0x2b4, .bit_off = 8 },
		[DPU_CLK_CTRL_DMA2] = { .reg_off = 0x2bc, .bit_off = 8 },
		[DPU_CLK_CTRL_DMA3] = { .reg_off = 0x2c4, .bit_off = 8 },
		[DPU_CLK_CTRL_REG_DMA] = { .reg_off = 0x2bc, .bit_off = 20 },
	},
};

/* FIXME: get rid of DPU_CTL_SPLIT_DISPLAY in favour of proper ACTIVE_CTL support */
static const struct dpu_ctl_cfg sm8350_ctl[] = {
	{
		.name = "ctl_0", .id = CTL_0,
		.base = 0x15000, .len = 0x1e8,
		.features = BIT(DPU_CTL_SPLIT_DISPLAY) | CTL_SC7280_MASK,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 9),
	}, {
		.name = "ctl_1", .id = CTL_1,
		.base = 0x16000, .len = 0x1e8,
		.features = BIT(DPU_CTL_SPLIT_DISPLAY) | CTL_SC7280_MASK,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 10),
	}, {
		.name = "ctl_2", .id = CTL_2,
		.base = 0x17000, .len = 0x1e8,
		.features = CTL_SC7280_MASK,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 11),
	}, {
		.name = "ctl_3", .id = CTL_3,
		.base = 0x18000, .len = 0x1e8,
		.features = CTL_SC7280_MASK,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 12),
	}, {
		.name = "ctl_4", .id = CTL_4,
		.base = 0x19000, .len = 0x1e8,
		.features = CTL_SC7280_MASK,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 13),
	}, {
		.name = "ctl_5", .id = CTL_5,
		.base = 0x1a000, .len = 0x1e8,
		.features = CTL_SC7280_MASK,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 23),
	},
};

static const struct dpu_sspp_cfg sm8350_sspp[] = {
	SSPP_BLK("sspp_0", SSPP_VIG0, 0x4000, 0x1f8, VIG_SC7180_MASK,
		sm8250_vig_sblk_0, 0, SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG0),
	SSPP_BLK("sspp_1", SSPP_VIG1, 0x6000, 0x1f8, VIG_SC7180_MASK,
		sm8250_vig_sblk_1, 4, SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG1),
	SSPP_BLK("sspp_2", SSPP_VIG2, 0x8000, 0x1f8, VIG_SC7180_MASK,
		sm8250_vig_sblk_2, 8, SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG2),
	SSPP_BLK("sspp_3", SSPP_VIG3, 0xa000, 0x1f8, VIG_SC7180_MASK,
		sm8250_vig_sblk_3, 12, SSPP_TYPE_VIG, DPU_CLK_CTRL_VIG3),
	SSPP_BLK("sspp_8", SSPP_DMA0, 0x24000, 0x1f8, DMA_SDM845_MASK,
		sdm845_dma_sblk_0, 1, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA0),
	SSPP_BLK("sspp_9", SSPP_DMA1, 0x26000, 0x1f8, DMA_SDM845_MASK,
		sdm845_dma_sblk_1, 5, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA1),
	SSPP_BLK("sspp_10", SSPP_DMA2, 0x28000, 0x1f8, DMA_CURSOR_SDM845_MASK,
		sdm845_dma_sblk_2, 9, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA2),
	SSPP_BLK("sspp_11", SSPP_DMA3, 0x2a000, 0x1f8, DMA_CURSOR_SDM845_MASK,
		sdm845_dma_sblk_3, 13, SSPP_TYPE_DMA, DPU_CLK_CTRL_DMA3),
};

static const struct dpu_lm_cfg sm8350_lm[] = {
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

static const struct dpu_dspp_cfg sm8350_dspp[] = {
	DSPP_BLK("dspp_0", DSPP_0, 0x54000, DSPP_SC7180_MASK,
		 &sdm845_dspp_sblk),
	DSPP_BLK("dspp_1", DSPP_1, 0x56000, DSPP_SC7180_MASK,
		 &sdm845_dspp_sblk),
	DSPP_BLK("dspp_2", DSPP_2, 0x58000, DSPP_SC7180_MASK,
		 &sdm845_dspp_sblk),
	DSPP_BLK("dspp_3", DSPP_3, 0x5a000, DSPP_SC7180_MASK,
		 &sdm845_dspp_sblk),
};

static const struct dpu_pingpong_cfg sm8350_pp[] = {
	PP_BLK_DITHER("pingpong_0", PINGPONG_0, 0x69000, MERGE_3D_0, sc7280_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 8),
			-1),
	PP_BLK_DITHER("pingpong_1", PINGPONG_1, 0x6a000, MERGE_3D_0, sc7280_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 9),
			-1),
	PP_BLK_DITHER("pingpong_2", PINGPONG_2, 0x6b000, MERGE_3D_1, sc7280_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 10),
			-1),
	PP_BLK_DITHER("pingpong_3", PINGPONG_3, 0x6c000, MERGE_3D_1, sc7280_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 11),
			-1),
	PP_BLK_DITHER("pingpong_4", PINGPONG_4, 0x6d000, MERGE_3D_2, sc7280_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 30),
			-1),
	PP_BLK_DITHER("pingpong_5", PINGPONG_5, 0x6e000, MERGE_3D_2, sc7280_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 31),
			-1),
};

static const struct dpu_merge_3d_cfg sm8350_merge_3d[] = {
	MERGE_3D_BLK("merge_3d_0", MERGE_3D_0, 0x4e000),
	MERGE_3D_BLK("merge_3d_1", MERGE_3D_1, 0x4f000),
	MERGE_3D_BLK("merge_3d_2", MERGE_3D_2, 0x50000),
};

/*
 * NOTE: Each display compression engine (DCE) contains dual hard
 * slice DSC encoders so both share same base address but with
 * its own different sub block address.
 */
static const struct dpu_dsc_cfg sm8350_dsc[] = {
	DSC_BLK_1_2("dce_0_0", DSC_0, 0x80000, 0x29c, 0, dsc_sblk_0),
	DSC_BLK_1_2("dce_0_1", DSC_1, 0x80000, 0x29c, 0, dsc_sblk_1),
	DSC_BLK_1_2("dce_1_0", DSC_2, 0x81000, 0x29c, BIT(DPU_DSC_NATIVE_42x_EN), dsc_sblk_0),
	DSC_BLK_1_2("dce_1_1", DSC_3, 0x81000, 0x29c, BIT(DPU_DSC_NATIVE_42x_EN), dsc_sblk_1),
};

static const struct dpu_intf_cfg sm8350_intf[] = {
	INTF_BLK("intf_0", INTF_0, 0x34000, 0x280, INTF_DP, MSM_DP_CONTROLLER_0, 24, INTF_SC7280_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 24),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 25)),
	INTF_BLK_DSI_TE("intf_1", INTF_1, 0x35000, 0x2c4, INTF_DSI, MSM_DSI_CONTROLLER_0, 24, INTF_SC7280_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 26),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 27),
			DPU_IRQ_IDX(MDP_INTF1_7xxx_TEAR_INTR, 2)),
	INTF_BLK_DSI_TE("intf_2", INTF_2, 0x36000, 0x2c4, INTF_DSI, MSM_DSI_CONTROLLER_1, 24, INTF_SC7280_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 28),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 29),
			DPU_IRQ_IDX(MDP_INTF2_7xxx_TEAR_INTR, 2)),
	INTF_BLK("intf_3", INTF_3, 0x37000, 0x280, INTF_DP, MSM_DP_CONTROLLER_1, 24, INTF_SC7280_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 30),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 31)),
};

static const struct dpu_perf_cfg sm8350_perf_data = {
	.max_bw_low = 11800000,
	.max_bw_high = 15500000,
	.min_core_ib = 2500000,
	.min_llcc_ib = 0,
	.min_dram_ib = 800000,
	.min_prefill_lines = 40,
	/* FIXME: lut tables */
	.danger_lut_tbl = {0x3ffff, 0x3ffff, 0x0},
	.safe_lut_tbl = {0xfe00, 0xfe00, 0xffff},
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

const struct dpu_mdss_cfg dpu_sm8350_cfg = {
	.caps = &sm8350_dpu_caps,
	.ubwc = &sm8350_ubwc_cfg,
	.mdp = &sm8350_mdp,
	.ctl_count = ARRAY_SIZE(sm8350_ctl),
	.ctl = sm8350_ctl,
	.sspp_count = ARRAY_SIZE(sm8350_sspp),
	.sspp = sm8350_sspp,
	.mixer_count = ARRAY_SIZE(sm8350_lm),
	.mixer = sm8350_lm,
	.dspp_count = ARRAY_SIZE(sm8350_dspp),
	.dspp = sm8350_dspp,
	.pingpong_count = ARRAY_SIZE(sm8350_pp),
	.pingpong = sm8350_pp,
	.dsc_count = ARRAY_SIZE(sm8350_dsc),
	.dsc = sm8350_dsc,
	.merge_3d_count = ARRAY_SIZE(sm8350_merge_3d),
	.merge_3d = sm8350_merge_3d,
	.intf_count = ARRAY_SIZE(sm8350_intf),
	.intf = sm8350_intf,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.perf = &sm8350_perf_data,
	.mdss_irqs = BIT(MDP_SSPP_TOP0_INTR) | \
		     BIT(MDP_SSPP_TOP0_INTR2) | \
		     BIT(MDP_SSPP_TOP0_HIST_INTR) | \
		     BIT(MDP_INTF0_7xxx_INTR) | \
		     BIT(MDP_INTF1_7xxx_INTR) | \
		     BIT(MDP_INTF1_7xxx_TEAR_INTR) | \
		     BIT(MDP_INTF2_7xxx_INTR) | \
		     BIT(MDP_INTF2_7xxx_TEAR_INTR) | \
		     BIT(MDP_INTF3_7xxx_INTR),
};

#endif
