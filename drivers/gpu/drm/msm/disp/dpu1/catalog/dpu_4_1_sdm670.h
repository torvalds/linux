/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022. Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2023, Richard Acayan. All rights reserved.
 */

#ifndef _DPU_4_1_SDM670_H
#define _DPU_4_1_SDM670_H

static const struct dpu_mdp_cfg sdm670_mdp = {
	.name = "top_0",
	.base = 0x0, .len = 0x45c,
	.features = BIT(DPU_MDP_AUDIO_SELECT),
	.clk_ctrls = {
		[DPU_CLK_CTRL_VIG0] = { .reg_off = 0x2ac, .bit_off = 0 },
		[DPU_CLK_CTRL_VIG1] = { .reg_off = 0x2b4, .bit_off = 0 },
		[DPU_CLK_CTRL_DMA0] = { .reg_off = 0x2ac, .bit_off = 8 },
		[DPU_CLK_CTRL_DMA1] = { .reg_off = 0x2b4, .bit_off = 8 },
		[DPU_CLK_CTRL_DMA2] = { .reg_off = 0x2bc, .bit_off = 8 },
	},
};

static const struct dpu_sspp_cfg sdm670_sspp[] = {
	{
		.name = "sspp_0", .id = SSPP_VIG0,
		.base = 0x4000, .len = 0x1c8,
		.features = VIG_SDM845_MASK_SDMA,
		.sblk = &dpu_vig_sblk_qseed3_1_3,
		.xin_id = 0,
		.type = SSPP_TYPE_VIG,
		.clk_ctrl = DPU_CLK_CTRL_VIG0,
	}, {
		.name = "sspp_1", .id = SSPP_VIG1,
		.base = 0x6000, .len = 0x1c8,
		.features = VIG_SDM845_MASK_SDMA,
		.sblk = &dpu_vig_sblk_qseed3_1_3,
		.xin_id = 4,
		.type = SSPP_TYPE_VIG,
		.clk_ctrl = DPU_CLK_CTRL_VIG0,
	}, {
		.name = "sspp_8", .id = SSPP_DMA0,
		.base = 0x24000, .len = 0x1c8,
		.features = DMA_SDM845_MASK_SDMA,
		.sblk = &dpu_dma_sblk,
		.xin_id = 1,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA0,
	}, {
		.name = "sspp_9", .id = SSPP_DMA1,
		.base = 0x26000, .len = 0x1c8,
		.features = DMA_CURSOR_SDM845_MASK_SDMA,
		.sblk = &dpu_dma_sblk,
		.xin_id = 5,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA1,
	}, {
		.name = "sspp_10", .id = SSPP_DMA2,
		.base = 0x28000, .len = 0x1c8,
		.features = DMA_CURSOR_SDM845_MASK_SDMA,
		.sblk = &dpu_dma_sblk,
		.xin_id = 9,
		.type = SSPP_TYPE_DMA,
		.clk_ctrl = DPU_CLK_CTRL_DMA2,
	},
};

static const struct dpu_dsc_cfg sdm670_dsc[] = {
	{
		.name = "dsc_0", .id = DSC_0,
		.base = 0x80000, .len = 0x140,
	}, {
		.name = "dsc_1", .id = DSC_1,
		.base = 0x80400, .len = 0x140,
	},
};

static const struct dpu_mdss_version sdm670_mdss_ver = {
	.core_major_ver = 4,
	.core_minor_ver = 1,
};

const struct dpu_mdss_cfg dpu_sdm670_cfg = {
	.mdss_ver = &sdm670_mdss_ver,
	.caps = &sdm845_dpu_caps,
	.mdp = &sdm670_mdp,
	.ctl_count = ARRAY_SIZE(sdm845_ctl),
	.ctl = sdm845_ctl,
	.sspp_count = ARRAY_SIZE(sdm670_sspp),
	.sspp = sdm670_sspp,
	.mixer_count = ARRAY_SIZE(sdm845_lm),
	.mixer = sdm845_lm,
	.pingpong_count = ARRAY_SIZE(sdm845_pp),
	.pingpong = sdm845_pp,
	.dsc_count = ARRAY_SIZE(sdm670_dsc),
	.dsc = sdm670_dsc,
	.intf_count = ARRAY_SIZE(sdm845_intf),
	.intf = sdm845_intf,
	.vbif_count = ARRAY_SIZE(sdm845_vbif),
	.vbif = sdm845_vbif,
	.perf = &sdm845_perf_data,
};

#endif
