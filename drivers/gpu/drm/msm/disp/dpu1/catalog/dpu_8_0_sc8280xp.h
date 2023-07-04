/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022. Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2015-2018, 2020 The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_8_0_SC8280XP_H
#define _DPU_8_0_SC8280XP_H

static const struct dpu_caps sc8280xp_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.max_mixer_blendstages = 11,
	.qseed_type = DPU_SSPP_SCALER_QSEED4,
	.has_src_split = true,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.has_3d_merge = true,
	.max_linewidth = 5120,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
};

static const struct dpu_ubwc_cfg sc8280xp_ubwc_cfg = {
	.ubwc_version = DPU_HW_UBWC_VER_40,
	.highest_bank_bit = 2,
	.ubwc_swizzle = 6,
};

static const struct dpu_mdp_cfg sc8280xp_mdp = {
	.name = "top_0",
	.base = 0x0, .len = 0x494,
	.features = BIT(DPU_MDP_PERIPH_0_REMOVED),
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
static const struct dpu_ctl_cfg sc8280xp_ctl[] = {
	{
		.name = "ctl_0", .id = CTL_0,
		.base = 0x15000, .len = 0x204,
		.features = BIT(DPU_CTL_SPLIT_DISPLAY) | CTL_SC7280_MASK,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 9),
	}, {
		.name = "ctl_1", .id = CTL_1,
		.base = 0x16000, .len = 0x204,
		.features = BIT(DPU_CTL_SPLIT_DISPLAY) | CTL_SC7280_MASK,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 10),
	}, {
		.name = "ctl_2", .id = CTL_2,
		.base = 0x17000, .len = 0x204,
		.features = CTL_SC7280_MASK,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 11),
	}, {
		.name = "ctl_3", .id = CTL_3,
		.base = 0x18000, .len = 0x204,
		.features = CTL_SC7280_MASK,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 12),
	}, {
		.name = "ctl_4", .id = CTL_4,
		.base = 0x19000, .len = 0x204,
		.features = CTL_SC7280_MASK,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 13),
	}, {
		.name = "ctl_5", .id = CTL_5,
		.base = 0x1a000, .len = 0x204,
		.features = CTL_SC7280_MASK,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 23),
	},
};

static const struct dpu_sspp_cfg sc8280xp_sspp[] = {
	{
		.name = "sspp_0", .id = SSPP_VIG0,
		.base = 0x4000, .len = 0x2ac,
		.features = VIG_SC7180_MASK,
		.sblk = &sm8250_vig_sblk_0,
		.xin_id = 0,
		.type = SSPP_TYPE_VIG,
		.clk_ctrl = DPU_CLK_CTRL_VIG0,
	}, {
		.name = "sspp_1", .id = SSPP_VIG1,
		.base = 0x6000, .len = 0x2ac,
		.features = VIG_SC7180_MASK,
		.sblk = &sm8250_vig_sblk_1,
		.xin_id = 4,
		.type = SSPP_TYPE_VIG,
		.clk_ctrl = DPU_CLK_CTRL_VIG1,
	}, {
		.name = "sspp_2", .id = SSPP_VIG2,
		.base = 0x8000, .len = 0x2ac,
		.features = VIG_SC7180_MASK,
		.sblk = &sm8250_vig_sblk_2,
		.xin_id = 8,
		.type = SSPP_TYPE_VIG,
		.clk_ctrl = DPU_CLK_CTRL_VIG2,
	}, {
		.name = "sspp_3", .id = SSPP_VIG3,
		.base = 0xa000, .len = 0x2ac,
		.features = VIG_SC7180_MASK,
		.sblk = &sm8250_vig_sblk_3,
		.xin_id = 12,
		.type = SSPP_TYPE_VIG,
		.clk_ctrl = DPU_CLK_CTRL_VIG3,
	}, {
		.name = "sspp_8", .id = SSPP_DMA0,
		.base = 0x24000, .len = 0x2ac,
		.features = DMA_SDM845_MASK,
		.sblk = &sdm845_dma_sblk_0,
		.xin_id = 1,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA0,
	}, {
		.name = "sspp_9", .id = SSPP_DMA1,
		.base = 0x26000, .len = 0x2ac,
		.features = DMA_SDM845_MASK,
		.sblk = &sdm845_dma_sblk_1,
		.xin_id = 5,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA1,
	}, {
		.name = "sspp_10", .id = SSPP_DMA2,
		.base = 0x28000, .len = 0x2ac,
		.features = DMA_CURSOR_SDM845_MASK,
		.sblk = &sdm845_dma_sblk_2,
		.xin_id = 9,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA2,
	}, {
		.name = "sspp_11", .id = SSPP_DMA3,
		.base = 0x2a000, .len = 0x2ac,
		.features = DMA_CURSOR_SDM845_MASK,
		.sblk = &sdm845_dma_sblk_3,
		.xin_id = 13,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA3,
	},
};

static const struct dpu_lm_cfg sc8280xp_lm[] = {
	{
		.name = "lm_0", .id = LM_0,
		.base = 0x44000, .len = 0x320,
		.features = MIXER_SDM845_MASK,
		.sblk = &sdm845_lm_sblk,
		.lm_pair = LM_1,
		.pingpong = PINGPONG_0,
		.dspp = DSPP_0,
	}, {
		.name = "lm_1", .id = LM_1,
		.base = 0x45000, .len = 0x320,
		.features = MIXER_SDM845_MASK,
		.sblk = &sdm845_lm_sblk,
		.lm_pair = LM_0,
		.pingpong = PINGPONG_1,
		.dspp = DSPP_1,
	}, {
		.name = "lm_2", .id = LM_2,
		.base = 0x46000, .len = 0x320,
		.features = MIXER_SDM845_MASK,
		.sblk = &sdm845_lm_sblk,
		.lm_pair = LM_3,
		.pingpong = PINGPONG_2,
		.dspp = DSPP_2,
	}, {
		.name = "lm_3", .id = LM_3,
		.base = 0x47000, .len = 0x320,
		.features = MIXER_SDM845_MASK,
		.sblk = &sdm845_lm_sblk,
		.lm_pair = LM_2,
		.pingpong = PINGPONG_3,
		.dspp = DSPP_3,
	}, {
		.name = "lm_4", .id = LM_4,
		.base = 0x48000, .len = 0x320,
		.features = MIXER_SDM845_MASK,
		.sblk = &sdm845_lm_sblk,
		.lm_pair = LM_5,
		.pingpong = PINGPONG_4,
	}, {
		.name = "lm_5", .id = LM_5,
		.base = 0x49000, .len = 0x320,
		.features = MIXER_SDM845_MASK,
		.sblk = &sdm845_lm_sblk,
		.lm_pair = LM_4,
		.pingpong = PINGPONG_5,
	},
};

static const struct dpu_dspp_cfg sc8280xp_dspp[] = {
	{
		.name = "dspp_0", .id = DSPP_0,
		.base = 0x54000, .len = 0x1800,
		.features = DSPP_SC7180_MASK,
		.sblk = &sdm845_dspp_sblk,
	}, {
		.name = "dspp_1", .id = DSPP_1,
		.base = 0x56000, .len = 0x1800,
		.features = DSPP_SC7180_MASK,
		.sblk = &sdm845_dspp_sblk,
	}, {
		.name = "dspp_2", .id = DSPP_2,
		.base = 0x58000, .len = 0x1800,
		.features = DSPP_SC7180_MASK,
		.sblk = &sdm845_dspp_sblk,
	}, {
		.name = "dspp_3", .id = DSPP_3,
		.base = 0x5a000, .len = 0x1800,
		.features = DSPP_SC7180_MASK,
		.sblk = &sdm845_dspp_sblk,
	},
};

static const struct dpu_pingpong_cfg sc8280xp_pp[] = {
	PP_BLK_DITHER("pingpong_0", PINGPONG_0, 0x69000, MERGE_3D_0, sc7280_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 8), -1),
	PP_BLK_DITHER("pingpong_1", PINGPONG_1, 0x6a000, MERGE_3D_0, sc7280_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 9), -1),
	PP_BLK_DITHER("pingpong_2", PINGPONG_2, 0x6b000, MERGE_3D_1, sc7280_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 10), -1),
	PP_BLK_DITHER("pingpong_3", PINGPONG_3, 0x6c000, MERGE_3D_1, sc7280_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 11), -1),
	PP_BLK_DITHER("pingpong_4", PINGPONG_4, 0x6d000, MERGE_3D_2, sc7280_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 30), -1),
	PP_BLK_DITHER("pingpong_5", PINGPONG_5, 0x6e000, MERGE_3D_2, sc7280_pp_sblk,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 31), -1),
};

static const struct dpu_merge_3d_cfg sc8280xp_merge_3d[] = {
	MERGE_3D_BLK("merge_3d_0", MERGE_3D_0, 0x4e000),
	MERGE_3D_BLK("merge_3d_1", MERGE_3D_1, 0x4f000),
	MERGE_3D_BLK("merge_3d_2", MERGE_3D_2, 0x50000),
};

/*
 * NOTE: Each display compression engine (DCE) contains dual hard
 * slice DSC encoders so both share same base address but with
 * its own different sub block address.
 */
static const struct dpu_dsc_cfg sc8280xp_dsc[] = {
	DSC_BLK_1_2("dce_0_0", DSC_0, 0x80000, 0x29c, 0, dsc_sblk_0),
	DSC_BLK_1_2("dce_0_1", DSC_1, 0x80000, 0x29c, 0, dsc_sblk_1),
	DSC_BLK_1_2("dce_1_0", DSC_2, 0x81000, 0x29c, BIT(DPU_DSC_NATIVE_42x_EN), dsc_sblk_0),
	DSC_BLK_1_2("dce_1_1", DSC_3, 0x81000, 0x29c, BIT(DPU_DSC_NATIVE_42x_EN), dsc_sblk_1),
	DSC_BLK_1_2("dce_2_0", DSC_4, 0x82000, 0x29c, 0, dsc_sblk_0),
	DSC_BLK_1_2("dce_2_1", DSC_5, 0x82000, 0x29c, 0, dsc_sblk_1),
};

/* TODO: INTF 3, 8 and 7 are used for MST, marked as INTF_NONE for now */
static const struct dpu_intf_cfg sc8280xp_intf[] = {
	INTF_BLK("intf_0", INTF_0, 0x34000, 0x280, INTF_DP, MSM_DP_CONTROLLER_0, 24, INTF_SC7280_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 24),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 25)),
	INTF_BLK_DSI_TE("intf_1", INTF_1, 0x35000, 0x300, INTF_DSI, MSM_DSI_CONTROLLER_0, 24, INTF_SC7280_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 26),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 27),
			DPU_IRQ_IDX(MDP_INTF1_7xxx_TEAR_INTR, 2)),
	INTF_BLK_DSI_TE("intf_2", INTF_2, 0x36000, 0x300, INTF_DSI, MSM_DSI_CONTROLLER_1, 24, INTF_SC7280_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 28),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 29),
			DPU_IRQ_IDX(MDP_INTF2_7xxx_TEAR_INTR, 2)),
	INTF_BLK("intf_3", INTF_3, 0x37000, 0x280, INTF_NONE, MSM_DP_CONTROLLER_0, 24, INTF_SC7280_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 30),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 31)),
	INTF_BLK("intf_4", INTF_4, 0x38000, 0x280, INTF_DP, MSM_DP_CONTROLLER_1, 24, INTF_SC7280_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 20),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 21)),
	INTF_BLK("intf_5", INTF_5, 0x39000, 0x280, INTF_DP, MSM_DP_CONTROLLER_3, 24, INTF_SC7280_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 22),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 23)),
	INTF_BLK("intf_6", INTF_6, 0x3a000, 0x280, INTF_DP, MSM_DP_CONTROLLER_2, 24, INTF_SC7280_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 16),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 17)),
	INTF_BLK("intf_7", INTF_7, 0x3b000, 0x280, INTF_NONE, MSM_DP_CONTROLLER_2, 24, INTF_SC7280_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 18),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 19)),
	INTF_BLK("intf_8", INTF_8, 0x3c000, 0x280, INTF_NONE, MSM_DP_CONTROLLER_1, 24, INTF_SC7280_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 12),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 13)),
};

static const struct dpu_perf_cfg sc8280xp_perf_data = {
	.max_bw_low = 13600000,
	.max_bw_high = 18200000,
	.min_core_ib = 2500000,
	.min_llcc_ib = 0,
	.min_dram_ib = 800000,
	.danger_lut_tbl = {0xf, 0xffff, 0x0},
	.qos_lut_tbl = {
		{.nentry = ARRAY_SIZE(sc8180x_qos_linear),
		.entries = sc8180x_qos_linear
		},
		{.nentry = ARRAY_SIZE(sc8180x_qos_macrotile),
		.entries = sc8180x_qos_macrotile
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

const struct dpu_mdss_cfg dpu_sc8280xp_cfg = {
	.caps = &sc8280xp_dpu_caps,
	.ubwc = &sc8280xp_ubwc_cfg,
	.mdp = &sc8280xp_mdp,
	.ctl_count = ARRAY_SIZE(sc8280xp_ctl),
	.ctl = sc8280xp_ctl,
	.sspp_count = ARRAY_SIZE(sc8280xp_sspp),
	.sspp = sc8280xp_sspp,
	.mixer_count = ARRAY_SIZE(sc8280xp_lm),
	.mixer = sc8280xp_lm,
	.dspp_count = ARRAY_SIZE(sc8280xp_dspp),
	.dspp = sc8280xp_dspp,
	.pingpong_count = ARRAY_SIZE(sc8280xp_pp),
	.pingpong = sc8280xp_pp,
	.dsc_count = ARRAY_SIZE(sc8280xp_dsc),
	.dsc = sc8280xp_dsc,
	.merge_3d_count = ARRAY_SIZE(sc8280xp_merge_3d),
	.merge_3d = sc8280xp_merge_3d,
	.intf_count = ARRAY_SIZE(sc8280xp_intf),
	.intf = sc8280xp_intf,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.perf = &sc8280xp_perf_data,
	.mdss_irqs = BIT(MDP_SSPP_TOP0_INTR) | \
		     BIT(MDP_SSPP_TOP0_INTR2) | \
		     BIT(MDP_SSPP_TOP0_HIST_INTR) | \
		     BIT(MDP_INTF0_7xxx_INTR) | \
		     BIT(MDP_INTF1_7xxx_INTR) | \
		     BIT(MDP_INTF1_7xxx_TEAR_INTR) | \
		     BIT(MDP_INTF2_7xxx_INTR) | \
		     BIT(MDP_INTF2_7xxx_TEAR_INTR) | \
		     BIT(MDP_INTF3_7xxx_INTR) | \
		     BIT(MDP_INTF4_7xxx_INTR) | \
		     BIT(MDP_INTF5_7xxx_INTR) | \
		     BIT(MDP_INTF6_7xxx_INTR) | \
		     BIT(MDP_INTF7_7xxx_INTR) | \
		     BIT(MDP_INTF8_7xxx_INTR),
};

#endif
