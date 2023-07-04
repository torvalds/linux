/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022. Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2015-2018, 2020 The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_7_2_SC7280_H
#define _DPU_7_2_SC7280_H

static const struct dpu_caps sc7280_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.max_mixer_blendstages = 0x7,
	.qseed_type = DPU_SSPP_SCALER_QSEED4,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.max_linewidth = 2400,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
};

static const struct dpu_ubwc_cfg sc7280_ubwc_cfg = {
	.ubwc_version = DPU_HW_UBWC_VER_30,
	.highest_bank_bit = 0x1,
	.ubwc_swizzle = 0x6,
};

static const struct dpu_mdp_cfg sc7280_mdp = {
	.name = "top_0",
	.base = 0x0, .len = 0x2014,
	.clk_ctrls = {
		[DPU_CLK_CTRL_VIG0] = { .reg_off = 0x2ac, .bit_off = 0 },
		[DPU_CLK_CTRL_DMA0] = { .reg_off = 0x2ac, .bit_off = 8 },
		[DPU_CLK_CTRL_DMA1] = { .reg_off = 0x2b4, .bit_off = 8 },
		[DPU_CLK_CTRL_DMA2] = { .reg_off = 0x2c4, .bit_off = 8 },
		[DPU_CLK_CTRL_WB2] = { .reg_off = 0x3b8, .bit_off = 24 },
	},
};

static const struct dpu_ctl_cfg sc7280_ctl[] = {
	{
		.name = "ctl_0", .id = CTL_0,
		.base = 0x15000, .len = 0x1e8,
		.features = CTL_SC7280_MASK,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 9),
	}, {
		.name = "ctl_1", .id = CTL_1,
		.base = 0x16000, .len = 0x1e8,
		.features = CTL_SC7280_MASK,
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
	},
};

static const struct dpu_sspp_cfg sc7280_sspp[] = {
	{
		.name = "sspp_0", .id = SSPP_VIG0,
		.base = 0x4000, .len = 0x1f8,
		.features = VIG_SC7280_MASK_SDMA,
		.sblk = &sc7280_vig_sblk_0,
		.xin_id = 0,
		.type = SSPP_TYPE_VIG,
		.clk_ctrl = DPU_CLK_CTRL_VIG0,
	}, {
		.name = "sspp_8", .id = SSPP_DMA0,
		.base = 0x24000, .len = 0x1f8,
		.features = DMA_SDM845_MASK_SDMA,
		.sblk = &sdm845_dma_sblk_0,
		.xin_id = 1,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA0,
	}, {
		.name = "sspp_9", .id = SSPP_DMA1,
		.base = 0x26000, .len = 0x1f8,
		.features = DMA_CURSOR_SDM845_MASK_SDMA,
		.sblk = &sdm845_dma_sblk_1,
		.xin_id = 5,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA1,
	}, {
		.name = "sspp_10", .id = SSPP_DMA2,
		.base = 0x28000, .len = 0x1f8,
		.features = DMA_CURSOR_SDM845_MASK_SDMA,
		.sblk = &sdm845_dma_sblk_2,
		.xin_id = 9,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA2,
	},
};

static const struct dpu_lm_cfg sc7280_lm[] = {
	{
		.name = "lm_0", .id = LM_0,
		.base = 0x44000, .len = 0x320,
		.features = MIXER_SDM845_MASK,
		.sblk = &sc7180_lm_sblk,
		.pingpong = PINGPONG_0,
		.dspp = DSPP_0,
	}, {
		.name = "lm_2", .id = LM_2,
		.base = 0x46000, .len = 0x320,
		.features = MIXER_SDM845_MASK,
		.sblk = &sc7180_lm_sblk,
		.lm_pair = LM_3,
		.pingpong = PINGPONG_2,
	}, {
		.name = "lm_3", .id = LM_3,
		.base = 0x47000, .len = 0x320,
		.features = MIXER_SDM845_MASK,
		.sblk = &sc7180_lm_sblk,
		.lm_pair = LM_2,
		.pingpong = PINGPONG_3,
	},
};

static const struct dpu_dspp_cfg sc7280_dspp[] = {
	{
		.name = "dspp_0", .id = DSPP_0,
		.base = 0x54000, .len = 0x1800,
		.features = DSPP_SC7180_MASK,
		.sblk = &sdm845_dspp_sblk,
	},
};

static const struct dpu_pingpong_cfg sc7280_pp[] = {
	{
		.name = "pingpong_0", .id = PINGPONG_0,
		.base = 0x69000, .len = 0,
		.features = BIT(DPU_PINGPONG_DITHER),
		.sblk = &sc7280_pp_sblk,
		.merge_3d = 0,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 8),
		.intr_rdptr = -1,
	}, {
		.name = "pingpong_1", .id = PINGPONG_1,
		.base = 0x6a000, .len = 0,
		.features = BIT(DPU_PINGPONG_DITHER),
		.sblk = &sc7280_pp_sblk,
		.merge_3d = 0,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 9),
		.intr_rdptr = -1,
	}, {
		.name = "pingpong_2", .id = PINGPONG_2,
		.base = 0x6b000, .len = 0,
		.features = BIT(DPU_PINGPONG_DITHER),
		.sblk = &sc7280_pp_sblk,
		.merge_3d = 0,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 10),
		.intr_rdptr = -1,
	}, {
		.name = "pingpong_3", .id = PINGPONG_3,
		.base = 0x6c000, .len = 0,
		.features = BIT(DPU_PINGPONG_DITHER),
		.sblk = &sc7280_pp_sblk,
		.merge_3d = 0,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 11),
		.intr_rdptr = -1,
	},
};

/* NOTE: sc7280 only has one DSC hard slice encoder */
static const struct dpu_dsc_cfg sc7280_dsc[] = {
	{
		.name = "dce_0_0", .id = DSC_0,
		.base = 0x80000, .len = 0x29c,
		.features = BIT(DPU_DSC_HW_REV_1_2) | BIT(DPU_DSC_NATIVE_42x_EN),
		.sblk = &dsc_sblk_0,
	},
};

static const struct dpu_wb_cfg sc7280_wb[] = {
	{
		.name = "wb_2", .id = WB_2,
		.base = 0x65000, .len = 0x2c8,
		.features = WB_SM8250_MASK,
		.format_list = wb2_formats,
		.num_formats = ARRAY_SIZE(wb2_formats),
		.clk_ctrl = DPU_CLK_CTRL_WB2,
		.xin_id = 6,
		.vbif_idx = VBIF_RT,
		.maxlinewidth = 4096,
		.intr_wb_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 4),
	},
};

static const struct dpu_intf_cfg sc7280_intf[] = {
	INTF_BLK("intf_0", INTF_0, 0x34000, 0x280, INTF_DP, MSM_DP_CONTROLLER_0, 24, INTF_SC7280_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 24),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 25)),
	INTF_BLK_DSI_TE("intf_1", INTF_1, 0x35000, 0x2c4, INTF_DSI, MSM_DSI_CONTROLLER_0, 24, INTF_SC7280_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 26),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 27),
			DPU_IRQ_IDX(MDP_INTF1_7xxx_TEAR_INTR, 2)),
	INTF_BLK("intf_5", INTF_5, 0x39000, 0x280, INTF_DP, MSM_DP_CONTROLLER_1, 24, INTF_SC7280_MASK,
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 22),
			DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 23)),
};

static const struct dpu_perf_cfg sc7280_perf_data = {
	.max_bw_low = 4700000,
	.max_bw_high = 8800000,
	.min_core_ib = 2500000,
	.min_llcc_ib = 0,
	.min_dram_ib = 1600000,
	.min_prefill_lines = 24,
	.danger_lut_tbl = {0xffff, 0xffff, 0x0},
	.safe_lut_tbl = {0xff00, 0xff00, 0xffff},
	.qos_lut_tbl = {
		{.nentry = ARRAY_SIZE(sc7180_qos_macrotile),
		.entries = sc7180_qos_macrotile
		},
		{.nentry = ARRAY_SIZE(sc7180_qos_macrotile),
		.entries = sc7180_qos_macrotile
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

const struct dpu_mdss_cfg dpu_sc7280_cfg = {
	.caps = &sc7280_dpu_caps,
	.ubwc = &sc7280_ubwc_cfg,
	.mdp = &sc7280_mdp,
	.ctl_count = ARRAY_SIZE(sc7280_ctl),
	.ctl = sc7280_ctl,
	.sspp_count = ARRAY_SIZE(sc7280_sspp),
	.sspp = sc7280_sspp,
	.dspp_count = ARRAY_SIZE(sc7280_dspp),
	.dspp = sc7280_dspp,
	.mixer_count = ARRAY_SIZE(sc7280_lm),
	.mixer = sc7280_lm,
	.pingpong_count = ARRAY_SIZE(sc7280_pp),
	.pingpong = sc7280_pp,
	.dsc_count = ARRAY_SIZE(sc7280_dsc),
	.dsc = sc7280_dsc,
	.wb_count = ARRAY_SIZE(sc7280_wb),
	.wb = sc7280_wb,
	.intf_count = ARRAY_SIZE(sc7280_intf),
	.intf = sc7280_intf,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.perf = &sc7280_perf_data,
	.mdss_irqs = BIT(MDP_SSPP_TOP0_INTR) | \
		     BIT(MDP_SSPP_TOP0_INTR2) | \
		     BIT(MDP_SSPP_TOP0_HIST_INTR) | \
		     BIT(MDP_INTF0_7xxx_INTR) | \
		     BIT(MDP_INTF1_7xxx_INTR) | \
		     BIT(MDP_INTF1_7xxx_TEAR_INTR) | \
		     BIT(MDP_INTF5_7xxx_INTR),
};

#endif
