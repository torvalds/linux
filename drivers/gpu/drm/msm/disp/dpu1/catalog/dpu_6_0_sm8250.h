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

static const struct dpu_mdss_cfg sm8250_dpu_cfg = {
	.caps = &sm8250_dpu_caps,
	.ubwc = &sm8250_ubwc_cfg,
	.mdp_count = ARRAY_SIZE(sm8250_mdp),
	.mdp = sm8250_mdp,
	.ctl_count = ARRAY_SIZE(sm8150_ctl),
	.ctl = sm8150_ctl,
	.sspp_count = ARRAY_SIZE(sm8250_sspp),
	.sspp = sm8250_sspp,
	.mixer_count = ARRAY_SIZE(sm8150_lm),
	.mixer = sm8150_lm,
	.dspp_count = ARRAY_SIZE(sm8150_dspp),
	.dspp = sm8150_dspp,
	.dsc_count = ARRAY_SIZE(sm8150_dsc),
	.dsc = sm8150_dsc,
	.pingpong_count = ARRAY_SIZE(sm8150_pp),
	.pingpong = sm8150_pp,
	.merge_3d_count = ARRAY_SIZE(sm8150_merge_3d),
	.merge_3d = sm8150_merge_3d,
	.intf_count = ARRAY_SIZE(sm8150_intf),
	.intf = sm8150_intf,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.wb_count = ARRAY_SIZE(sm8250_wb),
	.wb = sm8250_wb,
	.reg_dma_count = 1,
	.dma_cfg = &sm8250_regdma,
	.perf = &sm8250_perf_data,
	.mdss_irqs = IRQ_SM8250_MASK,
};

#endif
