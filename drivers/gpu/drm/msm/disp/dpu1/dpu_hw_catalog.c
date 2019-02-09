/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__
#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include "dpu_hw_mdss.h"
#include "dpu_hw_catalog.h"
#include "dpu_hw_catalog_format.h"
#include "dpu_kms.h"

#define VIG_SDM845_MASK \
	(BIT(DPU_SSPP_SRC) | BIT(DPU_SSPP_SCALER_QSEED3) | BIT(DPU_SSPP_QOS) |\
	BIT(DPU_SSPP_CSC_10BIT) | BIT(DPU_SSPP_CDP) | BIT(DPU_SSPP_QOS_8LVL) |\
	BIT(DPU_SSPP_TS_PREFILL) | BIT(DPU_SSPP_EXCL_RECT))

#define DMA_SDM845_MASK \
	(BIT(DPU_SSPP_SRC) | BIT(DPU_SSPP_QOS) | BIT(DPU_SSPP_QOS_8LVL) |\
	BIT(DPU_SSPP_TS_PREFILL) | BIT(DPU_SSPP_TS_PREFILL_REC1) |\
	BIT(DPU_SSPP_CDP) | BIT(DPU_SSPP_EXCL_RECT))

#define MIXER_SDM845_MASK \
	(BIT(DPU_MIXER_SOURCESPLIT) | BIT(DPU_DIM_LAYER))

#define PINGPONG_SDM845_MASK BIT(DPU_PINGPONG_DITHER)

#define PINGPONG_SDM845_SPLIT_MASK \
	(PINGPONG_SDM845_MASK | BIT(DPU_PINGPONG_TE2))

#define DEFAULT_PIXEL_RAM_SIZE		(50 * 1024)
#define DEFAULT_DPU_LINE_WIDTH		2048
#define DEFAULT_DPU_OUTPUT_LINE_WIDTH	2560

#define MAX_HORZ_DECIMATION	4
#define MAX_VERT_DECIMATION	4

#define MAX_UPSCALE_RATIO	20
#define MAX_DOWNSCALE_RATIO	4
#define SSPP_UNITY_SCALE	1

#define STRCAT(X, Y) (X Y)

/*************************************************************
 * DPU sub blocks config
 *************************************************************/
/* DPU top level caps */
static const struct dpu_caps sdm845_dpu_caps = {
	.max_mixer_width = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.max_mixer_blendstages = 0xb,
	.qseed_type = DPU_SSPP_SCALER_QSEED3,
	.smart_dma_rev = DPU_SSPP_SMART_DMA_V2,
	.ubwc_version = DPU_HW_UBWC_VER_20,
	.has_src_split = true,
	.has_dim_layer = true,
	.has_idle_pc = true,
};

static struct dpu_mdp_cfg sdm845_mdp[] = {
	{
	.name = "top_0", .id = MDP_TOP,
	.base = 0x0, .len = 0x45C,
	.features = 0,
	.highest_bank_bit = 0x2,
	.has_dest_scaler = true,
	.clk_ctrls[DPU_CLK_CTRL_VIG0] = {
			.reg_off = 0x2AC, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG1] = {
			.reg_off = 0x2B4, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG2] = {
			.reg_off = 0x2BC, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_VIG3] = {
			.reg_off = 0x2C4, .bit_off = 0},
	.clk_ctrls[DPU_CLK_CTRL_DMA0] = {
			.reg_off = 0x2AC, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_DMA1] = {
			.reg_off = 0x2B4, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR0] = {
			.reg_off = 0x2BC, .bit_off = 8},
	.clk_ctrls[DPU_CLK_CTRL_CURSOR1] = {
			.reg_off = 0x2C4, .bit_off = 8},
	},
};

/*************************************************************
 * CTL sub blocks config
 *************************************************************/
static struct dpu_ctl_cfg sdm845_ctl[] = {
	{
	.name = "ctl_0", .id = CTL_0,
	.base = 0x1000, .len = 0xE4,
	.features = BIT(DPU_CTL_SPLIT_DISPLAY)
	},
	{
	.name = "ctl_1", .id = CTL_1,
	.base = 0x1200, .len = 0xE4,
	.features = BIT(DPU_CTL_SPLIT_DISPLAY)
	},
	{
	.name = "ctl_2", .id = CTL_2,
	.base = 0x1400, .len = 0xE4,
	.features = 0
	},
	{
	.name = "ctl_3", .id = CTL_3,
	.base = 0x1600, .len = 0xE4,
	.features = 0
	},
	{
	.name = "ctl_4", .id = CTL_4,
	.base = 0x1800, .len = 0xE4,
	.features = 0
	},
};

/*************************************************************
 * SSPP sub blocks config
 *************************************************************/

/* SSPP common configuration */
static const struct dpu_sspp_blks_common sdm845_sspp_common = {
	.maxlinewidth = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE,
	.maxhdeciexp = MAX_HORZ_DECIMATION,
	.maxvdeciexp = MAX_VERT_DECIMATION,
};

#define _VIG_SBLK(num, sdma_pri) \
	{ \
	.common = &sdm845_sspp_common, \
	.maxdwnscale = MAX_DOWNSCALE_RATIO, \
	.maxupscale = MAX_UPSCALE_RATIO, \
	.smart_dma_priority = sdma_pri, \
	.src_blk = {.name = STRCAT("sspp_src_", num), \
		.id = DPU_SSPP_SRC, .base = 0x00, .len = 0x150,}, \
	.scaler_blk = {.name = STRCAT("sspp_scaler", num), \
		.id = DPU_SSPP_SCALER_QSEED3, \
		.base = 0xa00, .len = 0xa0,}, \
	.csc_blk = {.name = STRCAT("sspp_csc", num), \
		.id = DPU_SSPP_CSC_10BIT, \
		.base = 0x1a00, .len = 0x100,}, \
	.format_list = plane_formats_yuv, \
	.virt_format_list = plane_formats, \
	}

#define _DMA_SBLK(num, sdma_pri) \
	{ \
	.common = &sdm845_sspp_common, \
	.maxdwnscale = SSPP_UNITY_SCALE, \
	.maxupscale = SSPP_UNITY_SCALE, \
	.smart_dma_priority = sdma_pri, \
	.src_blk = {.name = STRCAT("sspp_src_", num), \
		.id = DPU_SSPP_SRC, .base = 0x00, .len = 0x150,}, \
	.format_list = plane_formats, \
	.virt_format_list = plane_formats, \
	}

static const struct dpu_sspp_sub_blks sdm845_vig_sblk_0 = _VIG_SBLK("0", 5);
static const struct dpu_sspp_sub_blks sdm845_vig_sblk_1 = _VIG_SBLK("1", 6);
static const struct dpu_sspp_sub_blks sdm845_vig_sblk_2 = _VIG_SBLK("2", 7);
static const struct dpu_sspp_sub_blks sdm845_vig_sblk_3 = _VIG_SBLK("3", 8);

static const struct dpu_sspp_sub_blks sdm845_dma_sblk_0 = _DMA_SBLK("8", 1);
static const struct dpu_sspp_sub_blks sdm845_dma_sblk_1 = _DMA_SBLK("9", 2);
static const struct dpu_sspp_sub_blks sdm845_dma_sblk_2 = _DMA_SBLK("10", 3);
static const struct dpu_sspp_sub_blks sdm845_dma_sblk_3 = _DMA_SBLK("11", 4);

#define SSPP_VIG_BLK(_name, _id, _base, _sblk, _xinid, _clkctrl) \
	{ \
	.name = _name, .id = _id, \
	.base = _base, .len = 0x1c8, \
	.features = VIG_SDM845_MASK, \
	.sblk = &_sblk, \
	.xin_id = _xinid, \
	.type = SSPP_TYPE_VIG, \
	.clk_ctrl = _clkctrl \
	}

#define SSPP_DMA_BLK(_name, _id, _base, _sblk, _xinid, _clkctrl) \
	{ \
	.name = _name, .id = _id, \
	.base = _base, .len = 0x1c8, \
	.features = DMA_SDM845_MASK, \
	.sblk = &_sblk, \
	.xin_id = _xinid, \
	.type = SSPP_TYPE_DMA, \
	.clk_ctrl = _clkctrl \
	}

static struct dpu_sspp_cfg sdm845_sspp[] = {
	SSPP_VIG_BLK("sspp_0", SSPP_VIG0, 0x4000,
		sdm845_vig_sblk_0, 0, DPU_CLK_CTRL_VIG0),
	SSPP_VIG_BLK("sspp_1", SSPP_VIG1, 0x6000,
		sdm845_vig_sblk_1, 4, DPU_CLK_CTRL_VIG1),
	SSPP_VIG_BLK("sspp_2", SSPP_VIG2, 0x8000,
		sdm845_vig_sblk_2, 8, DPU_CLK_CTRL_VIG2),
	SSPP_VIG_BLK("sspp_3", SSPP_VIG3, 0xa000,
		sdm845_vig_sblk_3, 12, DPU_CLK_CTRL_VIG3),
	SSPP_DMA_BLK("sspp_8", SSPP_DMA0, 0x24000,
		sdm845_dma_sblk_0, 1, DPU_CLK_CTRL_DMA0),
	SSPP_DMA_BLK("sspp_9", SSPP_DMA1, 0x26000,
		sdm845_dma_sblk_1, 5, DPU_CLK_CTRL_DMA1),
	SSPP_DMA_BLK("sspp_10", SSPP_DMA2, 0x28000,
		sdm845_dma_sblk_2, 9, DPU_CLK_CTRL_CURSOR0),
	SSPP_DMA_BLK("sspp_11", SSPP_DMA3, 0x2a000,
		sdm845_dma_sblk_3, 13, DPU_CLK_CTRL_CURSOR1),
};

/*************************************************************
 * MIXER sub blocks config
 *************************************************************/
static const struct dpu_lm_sub_blks sdm845_lm_sblk = {
	.maxwidth = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.maxblendstages = 11, /* excluding base layer */
	.blendstage_base = { /* offsets relative to mixer base */
		0x20, 0x38, 0x50, 0x68, 0x80, 0x98,
		0xb0, 0xc8, 0xe0, 0xf8, 0x110
	},
};

#define LM_BLK(_name, _id, _base, _ds, _pp, _lmpair) \
	{ \
	.name = _name, .id = _id, \
	.base = _base, .len = 0x320, \
	.features = MIXER_SDM845_MASK, \
	.sblk = &sdm845_lm_sblk, \
	.ds = _ds, \
	.pingpong = _pp, \
	.lm_pair_mask = (1 << _lmpair) \
	}

static struct dpu_lm_cfg sdm845_lm[] = {
	LM_BLK("lm_0", LM_0, 0x44000, DS_0, PINGPONG_0, LM_1),
	LM_BLK("lm_1", LM_1, 0x45000, DS_1, PINGPONG_1, LM_0),
	LM_BLK("lm_2", LM_2, 0x46000, DS_MAX, PINGPONG_2, LM_5),
	LM_BLK("lm_3", LM_3, 0x0, DS_MAX, PINGPONG_MAX, 0),
	LM_BLK("lm_4", LM_4, 0x0, DS_MAX, PINGPONG_MAX, 0),
	LM_BLK("lm_5", LM_5, 0x49000, DS_MAX, PINGPONG_3, LM_2),
};

/*************************************************************
 * DS sub blocks config
 *************************************************************/
static const struct dpu_ds_top_cfg sdm845_ds_top = {
	.name = "ds_top_0", .id = DS_TOP,
	.base = 0x60000, .len = 0xc,
	.maxinputwidth = DEFAULT_DPU_LINE_WIDTH,
	.maxoutputwidth = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.maxupscale = MAX_UPSCALE_RATIO,
};

#define DS_BLK(_name, _id, _base) \
	{\
	.name = _name, .id = _id, \
	.base = _base, .len = 0x800, \
	.features = DPU_SSPP_SCALER_QSEED3, \
	.top = &sdm845_ds_top \
	}

static struct dpu_ds_cfg sdm845_ds[] = {
	DS_BLK("ds_0", DS_0, 0x800),
	DS_BLK("ds_1", DS_1, 0x1000),
};

/*************************************************************
 * PINGPONG sub blocks config
 *************************************************************/
static const struct dpu_pingpong_sub_blks sdm845_pp_sblk_te = {
	.te2 = {.id = DPU_PINGPONG_TE2, .base = 0x2000, .len = 0x0,
		.version = 0x1},
	.dither = {.id = DPU_PINGPONG_DITHER, .base = 0x30e0,
		.len = 0x20, .version = 0x10000},
};

static const struct dpu_pingpong_sub_blks sdm845_pp_sblk = {
	.dither = {.id = DPU_PINGPONG_DITHER, .base = 0x30e0,
		.len = 0x20, .version = 0x10000},
};

#define PP_BLK_TE(_name, _id, _base) \
	{\
	.name = _name, .id = _id, \
	.base = _base, .len = 0xd4, \
	.features = PINGPONG_SDM845_SPLIT_MASK, \
	.sblk = &sdm845_pp_sblk_te \
	}
#define PP_BLK(_name, _id, _base) \
	{\
	.name = _name, .id = _id, \
	.base = _base, .len = 0xd4, \
	.features = PINGPONG_SDM845_MASK, \
	.sblk = &sdm845_pp_sblk \
	}

static struct dpu_pingpong_cfg sdm845_pp[] = {
	PP_BLK_TE("pingpong_0", PINGPONG_0, 0x70000),
	PP_BLK_TE("pingpong_1", PINGPONG_1, 0x70800),
	PP_BLK("pingpong_2", PINGPONG_2, 0x71000),
	PP_BLK("pingpong_3", PINGPONG_3, 0x71800),
};

/*************************************************************
 * INTF sub blocks config
 *************************************************************/
#define INTF_BLK(_name, _id, _base, _type, _ctrl_id) \
	{\
	.name = _name, .id = _id, \
	.base = _base, .len = 0x280, \
	.type = _type, \
	.controller_id = _ctrl_id, \
	.prog_fetch_lines_worst_case = 24 \
	}

static struct dpu_intf_cfg sdm845_intf[] = {
	INTF_BLK("intf_0", INTF_0, 0x6A000, INTF_DP, 0),
	INTF_BLK("intf_1", INTF_1, 0x6A800, INTF_DSI, 0),
	INTF_BLK("intf_2", INTF_2, 0x6B000, INTF_DSI, 1),
	INTF_BLK("intf_3", INTF_3, 0x6B800, INTF_DP, 1),
};

/*************************************************************
 * CDM sub blocks config
 *************************************************************/
static struct dpu_cdm_cfg sdm845_cdm[] = {
	{
	.name = "cdm_0", .id = CDM_0,
	.base = 0x79200, .len = 0x224,
	.features = 0,
	.intf_connect = BIT(INTF_3),
	},
};

/*************************************************************
 * VBIF sub blocks config
 *************************************************************/
/* VBIF QOS remap */
static u32 sdm845_rt_pri_lvl[] = {3, 3, 4, 4, 5, 5, 6, 6};
static u32 sdm845_nrt_pri_lvl[] = {3, 3, 3, 3, 3, 3, 3, 3};

static struct dpu_vbif_cfg sdm845_vbif[] = {
	{
	.name = "vbif_0", .id = VBIF_0,
	.base = 0, .len = 0x1040,
	.features = BIT(DPU_VBIF_QOS_REMAP),
	.xin_halt_timeout = 0x4000,
	.qos_rt_tbl = {
		.npriority_lvl = ARRAY_SIZE(sdm845_rt_pri_lvl),
		.priority_lvl = sdm845_rt_pri_lvl,
		},
	.qos_nrt_tbl = {
		.npriority_lvl = ARRAY_SIZE(sdm845_nrt_pri_lvl),
		.priority_lvl = sdm845_nrt_pri_lvl,
		},
	.memtype_count = 14,
	.memtype = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3},
	},
};

static struct dpu_reg_dma_cfg sdm845_regdma = {
	.base = 0x0, .version = 0x1, .trigger_sel_off = 0x119c
};

/*************************************************************
 * PERF data config
 *************************************************************/

/* SSPP QOS LUTs */
static struct dpu_qos_lut_entry sdm845_qos_linear[] = {
	{.fl = 4, .lut = 0x357},
	{.fl = 5, .lut = 0x3357},
	{.fl = 6, .lut = 0x23357},
	{.fl = 7, .lut = 0x223357},
	{.fl = 8, .lut = 0x2223357},
	{.fl = 9, .lut = 0x22223357},
	{.fl = 10, .lut = 0x222223357},
	{.fl = 11, .lut = 0x2222223357},
	{.fl = 12, .lut = 0x22222223357},
	{.fl = 13, .lut = 0x222222223357},
	{.fl = 14, .lut = 0x1222222223357},
	{.fl = 0, .lut = 0x11222222223357}
};

static struct dpu_qos_lut_entry sdm845_qos_macrotile[] = {
	{.fl = 10, .lut = 0x344556677},
	{.fl = 11, .lut = 0x3344556677},
	{.fl = 12, .lut = 0x23344556677},
	{.fl = 13, .lut = 0x223344556677},
	{.fl = 14, .lut = 0x1223344556677},
	{.fl = 0, .lut = 0x112233344556677},
};

static struct dpu_qos_lut_entry sdm845_qos_nrt[] = {
	{.fl = 0, .lut = 0x0},
};

static struct dpu_perf_cfg sdm845_perf_data = {
	.max_bw_low = 6800000,
	.max_bw_high = 6800000,
	.min_core_ib = 2400000,
	.min_llcc_ib = 800000,
	.min_dram_ib = 800000,
	.core_ib_ff = "6.0",
	.core_clk_ff = "1.0",
	.comp_ratio_rt =
	"NV12/5/1/1.23 AB24/5/1/1.23 XB24/5/1/1.23",
	.comp_ratio_nrt =
	"NV12/5/1/1.25 AB24/5/1/1.25 XB24/5/1/1.25",
	.undersized_prefill_lines = 2,
	.xtra_prefill_lines = 2,
	.dest_scale_prefill_lines = 3,
	.macrotile_prefill_lines = 4,
	.yuv_nv12_prefill_lines = 8,
	.linear_prefill_lines = 1,
	.downscaling_prefill_lines = 1,
	.amortizable_threshold = 25,
	.min_prefill_lines = 24,
	.danger_lut_tbl = {0xf, 0xffff, 0x0},
	.qos_lut_tbl = {
		{.nentry = ARRAY_SIZE(sdm845_qos_linear),
		.entries = sdm845_qos_linear
		},
		{.nentry = ARRAY_SIZE(sdm845_qos_macrotile),
		.entries = sdm845_qos_macrotile
		},
		{.nentry = ARRAY_SIZE(sdm845_qos_nrt),
		.entries = sdm845_qos_nrt
		},
	},
	.cdp_cfg = {
		{.rd_enable = 1, .wr_enable = 1},
		{.rd_enable = 1, .wr_enable = 0}
	},
};

/*************************************************************
 * Hardware catalog init
 *************************************************************/

/*
 * sdm845_cfg_init(): populate sdm845 dpu sub-blocks reg offsets
 * and instance counts.
 */
static void sdm845_cfg_init(struct dpu_mdss_cfg *dpu_cfg)
{
	*dpu_cfg = (struct dpu_mdss_cfg){
		.caps = &sdm845_dpu_caps,
		.mdp_count = ARRAY_SIZE(sdm845_mdp),
		.mdp = sdm845_mdp,
		.ctl_count = ARRAY_SIZE(sdm845_ctl),
		.ctl = sdm845_ctl,
		.sspp_count = ARRAY_SIZE(sdm845_sspp),
		.sspp = sdm845_sspp,
		.mixer_count = ARRAY_SIZE(sdm845_lm),
		.mixer = sdm845_lm,
		.ds_count = ARRAY_SIZE(sdm845_ds),
		.ds = sdm845_ds,
		.pingpong_count = ARRAY_SIZE(sdm845_pp),
		.pingpong = sdm845_pp,
		.cdm_count = ARRAY_SIZE(sdm845_cdm),
		.cdm = sdm845_cdm,
		.intf_count = ARRAY_SIZE(sdm845_intf),
		.intf = sdm845_intf,
		.vbif_count = ARRAY_SIZE(sdm845_vbif),
		.vbif = sdm845_vbif,
		.reg_dma_count = 1,
		.dma_cfg = sdm845_regdma,
		.perf = sdm845_perf_data,
	};
}

static struct dpu_mdss_hw_cfg_handler cfg_handler[] = {
	{ .hw_rev = DPU_HW_VER_400, .cfg_init = sdm845_cfg_init},
	{ .hw_rev = DPU_HW_VER_401, .cfg_init = sdm845_cfg_init},
};

void dpu_hw_catalog_deinit(struct dpu_mdss_cfg *dpu_cfg)
{
	kfree(dpu_cfg);
}

struct dpu_mdss_cfg *dpu_hw_catalog_init(u32 hw_rev)
{
	int i;
	struct dpu_mdss_cfg *dpu_cfg;

	dpu_cfg = kzalloc(sizeof(*dpu_cfg), GFP_KERNEL);
	if (!dpu_cfg)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < ARRAY_SIZE(cfg_handler); i++) {
		if (cfg_handler[i].hw_rev == hw_rev) {
			cfg_handler[i].cfg_init(dpu_cfg);
			dpu_cfg->hwversion = hw_rev;
			return dpu_cfg;
		}
	}

	DPU_ERROR("unsupported chipset id:%X\n", hw_rev);
	dpu_hw_catalog_deinit(dpu_cfg);
	return ERR_PTR(-ENODEV);
}

