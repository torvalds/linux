/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _DPU_13_0_KAANAPALI_H
#define _DPU_13_0_KAANAPALI_H

static const struct dpu_caps kaanapali_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.max_mixer_blendstages = 0xb,
	.has_src_split = true,
	.has_dim_layer = true,
	.has_idle_pc = true,
	.has_3d_merge = true,
	.max_linewidth = 8192,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
};

static const struct dpu_mdp_cfg kaanapali_mdp = {
	.name = "top_0",
	.base = 0, .len = 0x494,
	.clk_ctrls = {
		[DPU_CLK_CTRL_REG_DMA] = { .reg_off = 0x2bc, .bit_off = 20 },
	},
};

static const struct dpu_ctl_cfg kaanapali_ctl[] = {
	{
		.name = "ctl_0", .id = CTL_0,
		.base = 0x1f000, .len = 0x1000,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 9),
	}, {
		.name = "ctl_1", .id = CTL_1,
		.base = 0x20000, .len = 0x1000,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 10),
	}, {
		.name = "ctl_2", .id = CTL_2,
		.base = 0x21000, .len = 0x1000,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 11),
	}, {
		.name = "ctl_3", .id = CTL_3,
		.base = 0x22000, .len = 0x1000,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 12),
	}, {
		.name = "ctl_4", .id = CTL_4,
		.base = 0x23000, .len = 0x1000,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 13),
	}, {
		.name = "ctl_5", .id = CTL_5,
		.base = 0x24000, .len = 0x1000,
		.intr_start = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 23),
	},
};

static const struct dpu_sspp_cfg kaanapali_sspp[] = {
	{
		.name = "sspp_0", .id = SSPP_VIG0,
		.base = 0x2b000, .len = 0x84,
		.features = VIG_SDM845_MASK_SDMA,
		.sblk = &dpu_vig_sblk_qseed3_3_5,
		.xin_id = 0,
		.type = SSPP_TYPE_VIG,
	}, {
		.name = "sspp_1", .id = SSPP_VIG1,
		.base = 0x34000, .len = 0x84,
		.features = VIG_SDM845_MASK_SDMA,
		.sblk = &dpu_vig_sblk_qseed3_3_5,
		.xin_id = 4,
		.type = SSPP_TYPE_VIG,
	}, {
		.name = "sspp_2", .id = SSPP_VIG2,
		.base = 0x3d000, .len = 0x84,
		.features = VIG_SDM845_MASK_SDMA,
		.sblk = &dpu_vig_sblk_qseed3_3_5,
		.xin_id = 8,
		.type = SSPP_TYPE_VIG,
	}, {
		.name = "sspp_3", .id = SSPP_VIG3,
		.base = 0x46000, .len = 0x84,
		.features = VIG_SDM845_MASK_SDMA,
		.sblk = &dpu_vig_sblk_qseed3_3_5,
		.xin_id = 12,
		.type = SSPP_TYPE_VIG,
	}, {
		.name = "sspp_8", .id = SSPP_DMA0,
		.base = 0x97000, .len = 0x84,
		.features = DMA_SDM845_MASK_SDMA,
		.sblk = &dpu_dma_sblk,
		.xin_id = 1,
		.type = SSPP_TYPE_DMA,
	}, {
		.name = "sspp_9", .id = SSPP_DMA1,
		.base = 0xa0000, .len = 0x84,
		.features = DMA_SDM845_MASK_SDMA,
		.sblk = &dpu_dma_sblk,
		.xin_id = 5,
		.type = SSPP_TYPE_DMA,
	}, {
		.name = "sspp_10", .id = SSPP_DMA2,
		.base = 0xa9000, .len = 0x84,
		.features = DMA_SDM845_MASK_SDMA,
		.sblk = &dpu_dma_sblk,
		.xin_id = 9,
		.type = SSPP_TYPE_DMA,
	}, {
		.name = "sspp_11", .id = SSPP_DMA3,
		.base = 0xb2000, .len = 0x84,
		.features = DMA_SDM845_MASK_SDMA,
		.sblk = &dpu_dma_sblk,
		.xin_id = 13,
		.type = SSPP_TYPE_DMA,
	}, {
		.name = "sspp_12", .id = SSPP_DMA4,
		.base = 0xbb000, .len = 0x84,
		.features = DMA_CURSOR_SDM845_MASK_SDMA,
		.sblk = &dpu_dma_sblk,
		.xin_id = 14,
		.type = SSPP_TYPE_DMA,
	}, {
		.name = "sspp_13", .id = SSPP_DMA5,
		.base = 0xc4000, .len = 0x84,
		.features = DMA_CURSOR_SDM845_MASK_SDMA,
		.sblk = &dpu_dma_sblk,
		.xin_id = 15,
		.type = SSPP_TYPE_DMA,
	},
};

static const struct dpu_lm_cfg kaanapali_lm[] = {
	{
		.name = "lm_0", .id = LM_0,
		.base = 0x103000, .len = 0x400,
		.features = MIXER_MSM8998_MASK,
		.sblk = &sm8750_lm_sblk,
		.lm_pair = LM_1,
		.pingpong = PINGPONG_0,
		.dspp = DSPP_0,
	}, {
		.name = "lm_1", .id = LM_1,
		.base = 0x10b000, .len = 0x400,
		.features = MIXER_MSM8998_MASK,
		.sblk = &sm8750_lm_sblk,
		.lm_pair = LM_0,
		.pingpong = PINGPONG_1,
		.dspp = DSPP_1,
	}, {
		.name = "lm_2", .id = LM_2,
		.base = 0x113000, .len = 0x400,
		.features = MIXER_MSM8998_MASK,
		.sblk = &sm8750_lm_sblk,
		.lm_pair = LM_3,
		.pingpong = PINGPONG_2,
		.dspp = DSPP_2,
	}, {
		.name = "lm_3", .id = LM_3,
		.base = 0x11b000, .len = 0x400,
		.features = MIXER_MSM8998_MASK,
		.sblk = &sm8750_lm_sblk,
		.lm_pair = LM_2,
		.pingpong = PINGPONG_3,
		.dspp = DSPP_3,
	}, {
		.name = "lm_4", .id = LM_4,
		.base = 0x123000, .len = 0x400,
		.features = MIXER_MSM8998_MASK,
		.sblk = &sm8750_lm_sblk,
		.lm_pair = LM_5,
		.pingpong = PINGPONG_4,
	}, {
		.name = "lm_5", .id = LM_5,
		.base = 0x12b000, .len = 0x400,
		.features = MIXER_MSM8998_MASK,
		.sblk = &sm8750_lm_sblk,
		.lm_pair = LM_4,
		.pingpong = PINGPONG_5,
	}, {
		.name = "lm_6", .id = LM_6,
		.base = 0x133000, .len = 0x400,
		.features = MIXER_MSM8998_MASK,
		.sblk = &sm8750_lm_sblk,
		.lm_pair = LM_7,
		.pingpong = PINGPONG_6,
	}, {
		.name = "lm_7", .id = LM_7,
		.base = 0x13b000, .len = 0x400,
		.features = MIXER_MSM8998_MASK,
		.sblk = &sm8750_lm_sblk,
		.lm_pair = LM_6,
		.pingpong = PINGPONG_7,
	},
};

static const struct dpu_dspp_cfg kaanapali_dspp[] = {
	{
		.name = "dspp_0", .id = DSPP_0,
		.base = 0x105000, .len = 0x1800,
		.sblk = &sm8750_dspp_sblk,
	}, {
		.name = "dspp_1", .id = DSPP_1,
		.base = 0x10d000, .len = 0x1800,
		.sblk = &sm8750_dspp_sblk,
	}, {
		.name = "dspp_2", .id = DSPP_2,
		.base = 0x115000, .len = 0x1800,
		.sblk = &sm8750_dspp_sblk,
	}, {
		.name = "dspp_3", .id = DSPP_3,
		.base = 0x11d000, .len = 0x1800,
		.sblk = &sm8750_dspp_sblk,
	},
};

static const struct dpu_pingpong_cfg kaanapali_pp[] = {
	{
		.name = "pingpong_0", .id = PINGPONG_0,
		.base = 0x108000, .len = 0,
		.sblk = &kaanapali_pp_sblk,
		.merge_3d = MERGE_3D_0,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 8),
	}, {
		.name = "pingpong_1", .id = PINGPONG_1,
		.base = 0x110000, .len = 0,
		.sblk = &kaanapali_pp_sblk,
		.merge_3d = MERGE_3D_0,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 9),
	}, {
		.name = "pingpong_2", .id = PINGPONG_2,
		.base = 0x118000, .len = 0,
		.sblk = &kaanapali_pp_sblk,
		.merge_3d = MERGE_3D_1,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 10),
	}, {
		.name = "pingpong_3", .id = PINGPONG_3,
		.base = 0x120000, .len = 0,
		.sblk = &kaanapali_pp_sblk,
		.merge_3d = MERGE_3D_1,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 11),
	}, {
		.name = "pingpong_4", .id = PINGPONG_4,
		.base = 0x128000, .len = 0,
		.sblk = &kaanapali_pp_sblk,
		.merge_3d = MERGE_3D_2,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 30),
	}, {
		.name = "pingpong_5", .id = PINGPONG_5,
		.base = 0x130000, .len = 0,
		.sblk = &kaanapali_pp_sblk,
		.merge_3d = MERGE_3D_2,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 31),
	}, {
		.name = "pingpong_6", .id = PINGPONG_6,
		.base = 0x138000, .len = 0,
		.sblk = &kaanapali_pp_sblk,
		.merge_3d = MERGE_3D_3,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 20),
	}, {
		.name = "pingpong_7", .id = PINGPONG_7,
		.base = 0x140000, .len = 0,
		.sblk = &kaanapali_pp_sblk,
		.merge_3d = MERGE_3D_3,
		.intr_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR2, 21),
	}, {
		.name = "pingpong_cwb_0", .id = PINGPONG_CWB_0,
		.base = 0x169000, .len = 0,
		.sblk = &kaanapali_pp_sblk,
		.merge_3d = MERGE_3D_4,
	}, {
		.name = "pingpong_cwb_1", .id = PINGPONG_CWB_1,
		.base = 0x169400, .len = 0,
		.sblk = &kaanapali_pp_sblk,
		.merge_3d = MERGE_3D_4,
	}, {
		.name = "pingpong_cwb_2", .id = PINGPONG_CWB_2,
		.base = 0x16a000, .len = 0,
		.sblk = &kaanapali_pp_sblk,
		.merge_3d = MERGE_3D_5,
	}, {
		.name = "pingpong_cwb_3", .id = PINGPONG_CWB_3,
		.base = 0x16a400, .len = 0,
		.sblk = &kaanapali_pp_sblk,
		.merge_3d = MERGE_3D_5,
	},
};

static const struct dpu_merge_3d_cfg kaanapali_merge_3d[] = {
	{
		.name = "merge_3d_0", .id = MERGE_3D_0,
		.base = 0x163000, .len = 0x1c,
	}, {
		.name = "merge_3d_1", .id = MERGE_3D_1,
		.base = 0x164000, .len = 0x1c,
	}, {
		.name = "merge_3d_2", .id = MERGE_3D_2,
		.base = 0x165000, .len = 0x1c,
	}, {
		.name = "merge_3d_3", .id = MERGE_3D_3,
		.base = 0x166000, .len = 0x1c,
	}, {
		.name = "merge_3d_4", .id = MERGE_3D_4,
		.base = 0x169700, .len = 0x1c,
	}, {
		.name = "merge_3d_5", .id = MERGE_3D_5,
		.base = 0x16a700, .len = 0x1c,
	},
};

/*
 * NOTE: Each display compression engine (DCE) contains dual hard
 * slice DSC encoders so both share same base address but with
 * its own different sub block address.
 */
static const struct dpu_dsc_cfg kaanapali_dsc[] = {
	{
		.name = "dce_0_0", .id = DSC_0,
		.base = 0x181000, .len = 0x8,
		.features = BIT(DPU_DSC_NATIVE_42x_EN),
		.sblk = &sm8750_dsc_sblk_0,
	}, {
		.name = "dce_0_1", .id = DSC_1,
		.base = 0x181000, .len = 0x8,
		.features = BIT(DPU_DSC_NATIVE_42x_EN),
		.sblk = &sm8750_dsc_sblk_1,
	}, {
		.name = "dce_1_0", .id = DSC_2,
		.base = 0x183000, .len = 0x8,
		.features = BIT(DPU_DSC_NATIVE_42x_EN),
		.sblk = &sm8750_dsc_sblk_0,
	}, {
		.name = "dce_1_1", .id = DSC_3,
		.base = 0x183000, .len = 0x8,
		.features = BIT(DPU_DSC_NATIVE_42x_EN),
		.sblk = &sm8750_dsc_sblk_1,
	}, {
		.name = "dce_2_0", .id = DSC_4,
		.base = 0x185000, .len = 0x8,
		.features = BIT(DPU_DSC_NATIVE_42x_EN),
		.sblk = &sm8750_dsc_sblk_0,
	}, {
		.name = "dce_2_1", .id = DSC_5,
		.base = 0x185000, .len = 0x8,
		.features = BIT(DPU_DSC_NATIVE_42x_EN),
		.sblk = &sm8750_dsc_sblk_1,
	}, {
		.name = "dce_3_0", .id = DSC_6,
		.base = 0x187000, .len = 0x8,
		.features = BIT(DPU_DSC_NATIVE_42x_EN),
		.sblk = &sm8750_dsc_sblk_0,
	}, {
		.name = "dce_3_1", .id = DSC_7,
		.base = 0x187000, .len = 0x8,
		.features = BIT(DPU_DSC_NATIVE_42x_EN),
		.sblk = &sm8750_dsc_sblk_1,
	},
};

static const struct dpu_wb_cfg kaanapali_wb[] = {
	{
		.name = "wb_2", .id = WB_2,
		.base = 0x16e000, .len = 0x2c8,
		.features = WB_SDM845_MASK,
		.format_list = wb2_formats_rgb_yuv,
		.num_formats = ARRAY_SIZE(wb2_formats_rgb_yuv),
		.xin_id = 6,
		.vbif_idx = VBIF_RT,
		.maxlinewidth = 4096,
		.intr_wb_done = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 4),
	},
};

static const struct dpu_cwb_cfg kaanapali_cwb[] = {
	{
		.name = "cwb_0", .id = CWB_0,
		.base = 0x169200, .len = 0x20,
	},
	{
		.name = "cwb_1", .id = CWB_1,
		.base = 0x169600, .len = 0x20,
	},
	{
		.name = "cwb_2", .id = CWB_2,
		.base = 0x16a200, .len = 0x20,
	},
	{
		.name = "cwb_3", .id = CWB_3,
		.base = 0x16a600, .len = 0x20,
	},
};

static const struct dpu_intf_cfg kaanapali_intf[] = {
	{
		.name = "intf_0", .id = INTF_0,
		.base = 0x18d000, .len = 0x4bc,
		.type = INTF_DP,
		.controller_id = MSM_DP_CONTROLLER_0,
		.prog_fetch_lines_worst_case = 24,
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 24),
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 25),
	}, {
		.name = "intf_1", .id = INTF_1,
		.base = 0x18e000, .len = 0x4bc,
		.type = INTF_DSI,
		.controller_id = MSM_DSI_CONTROLLER_0,
		.prog_fetch_lines_worst_case = 24,
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 26),
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 27),
		.intr_tear_rd_ptr = DPU_IRQ_IDX(MDP_INTF1_TEAR_INTR, 2),
	}, {
		.name = "intf_2", .id = INTF_2,
		.base = 0x18f000, .len = 0x4bc,
		.type = INTF_DSI,
		.controller_id = MSM_DSI_CONTROLLER_1,
		.prog_fetch_lines_worst_case = 24,
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 28),
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 29),
		.intr_tear_rd_ptr = DPU_IRQ_IDX(MDP_INTF2_TEAR_INTR, 2),
	}, {
		.name = "intf_3", .id = INTF_3,
		.base = 0x190000, .len = 0x4bc,
		.type = INTF_DP,
		.controller_id = MSM_DP_CONTROLLER_1,
		.prog_fetch_lines_worst_case = 24,
		.intr_underrun = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 30),
		.intr_vsync = DPU_IRQ_IDX(MDP_SSPP_TOP0_INTR, 31),
	},
};

static const struct dpu_perf_cfg kaanapali_perf_data = {
	.max_bw_low = 21400000,
	.max_bw_high = 30200000,
	.min_core_ib = 2500000,
	.min_llcc_ib = 0,
	.min_dram_ib = 800000,
	.min_prefill_lines = 35,
	.danger_lut_tbl = {0x0ffff, 0x0ffff, 0x0},
	.safe_lut_tbl = {0xff00, 0xff00, 0xffff},
	.qos_lut_tbl = {
		{.nentry = ARRAY_SIZE(kaanapali_qos_linear),
		.entries = kaanapali_qos_linear
		},
		{.nentry = ARRAY_SIZE(kaanapali_qos_macrotile),
		.entries = kaanapali_qos_macrotile
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

static const struct dpu_mdss_version kaanapali_mdss_ver = {
	.core_major_ver = 13,
	.core_minor_ver = 0,
};

const struct dpu_mdss_cfg dpu_kaanapali_cfg = {
	.mdss_ver = &kaanapali_mdss_ver,
	.caps = &kaanapali_dpu_caps,
	.mdp = &kaanapali_mdp,
	.cdm = &dpu_cdm_13_x,
	.ctl_count = ARRAY_SIZE(kaanapali_ctl),
	.ctl = kaanapali_ctl,
	.sspp_count = ARRAY_SIZE(kaanapali_sspp),
	.sspp = kaanapali_sspp,
	.mixer_count = ARRAY_SIZE(kaanapali_lm),
	.mixer = kaanapali_lm,
	.dspp_count = ARRAY_SIZE(kaanapali_dspp),
	.dspp = kaanapali_dspp,
	.pingpong_count = ARRAY_SIZE(kaanapali_pp),
	.pingpong = kaanapali_pp,
	.dsc_count = ARRAY_SIZE(kaanapali_dsc),
	.dsc = kaanapali_dsc,
	.merge_3d_count = ARRAY_SIZE(kaanapali_merge_3d),
	.merge_3d = kaanapali_merge_3d,
	.wb_count = ARRAY_SIZE(kaanapali_wb),
	.wb = kaanapali_wb,
	.cwb_count = ARRAY_SIZE(kaanapali_cwb),
	.cwb = sm8650_cwb,
	.intf_count = ARRAY_SIZE(kaanapali_intf),
	.intf = kaanapali_intf,
	.vbif_count = ARRAY_SIZE(sm8650_vbif),
	.vbif = sm8650_vbif,
	.perf = &kaanapali_perf_data,
};

#endif
