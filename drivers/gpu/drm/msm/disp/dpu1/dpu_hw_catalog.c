// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__
#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include "dpu_hw_mdss.h"
#include "dpu_hw_interrupts.h"
#include "dpu_hw_catalog.h"
#include "dpu_kms.h"

#define VIG_BASE_MASK \
	(BIT(DPU_SSPP_QOS) |\
	BIT(DPU_SSPP_CDP) |\
	BIT(DPU_SSPP_TS_PREFILL) | BIT(DPU_SSPP_EXCL_RECT))

#define VIG_MASK \
	(VIG_BASE_MASK | \
	BIT(DPU_SSPP_CSC_10BIT))

#define VIG_MSM8998_MASK \
	(VIG_MASK | BIT(DPU_SSPP_SCALER_QSEED3))

#define VIG_SDM845_MASK \
	(VIG_MASK | BIT(DPU_SSPP_QOS_8LVL) | BIT(DPU_SSPP_SCALER_QSEED3))

#define VIG_SDM845_MASK_SDMA \
	(VIG_SDM845_MASK | BIT(DPU_SSPP_SMART_DMA_V2))

#define VIG_SC7180_MASK \
	(VIG_MASK | BIT(DPU_SSPP_QOS_8LVL) | BIT(DPU_SSPP_SCALER_QSEED4))

#define VIG_SC7180_MASK_SDMA \
	(VIG_SC7180_MASK | BIT(DPU_SSPP_SMART_DMA_V2))

#define VIG_QCM2290_MASK (VIG_BASE_MASK | BIT(DPU_SSPP_QOS_8LVL))

#define DMA_MSM8998_MASK \
	(BIT(DPU_SSPP_QOS) |\
	BIT(DPU_SSPP_TS_PREFILL) | BIT(DPU_SSPP_TS_PREFILL_REC1) |\
	BIT(DPU_SSPP_CDP) | BIT(DPU_SSPP_EXCL_RECT))

#define VIG_SC7280_MASK \
	(VIG_SC7180_MASK | BIT(DPU_SSPP_INLINE_ROTATION))

#define VIG_SC7280_MASK_SDMA \
	(VIG_SC7280_MASK | BIT(DPU_SSPP_SMART_DMA_V2))

#define DMA_SDM845_MASK \
	(BIT(DPU_SSPP_QOS) | BIT(DPU_SSPP_QOS_8LVL) |\
	BIT(DPU_SSPP_TS_PREFILL) | BIT(DPU_SSPP_TS_PREFILL_REC1) |\
	BIT(DPU_SSPP_CDP) | BIT(DPU_SSPP_EXCL_RECT))

#define DMA_CURSOR_SDM845_MASK \
	(DMA_SDM845_MASK | BIT(DPU_SSPP_CURSOR))

#define DMA_SDM845_MASK_SDMA \
	(DMA_SDM845_MASK | BIT(DPU_SSPP_SMART_DMA_V2))

#define DMA_CURSOR_SDM845_MASK_SDMA \
	(DMA_CURSOR_SDM845_MASK | BIT(DPU_SSPP_SMART_DMA_V2))

#define DMA_CURSOR_MSM8998_MASK \
	(DMA_MSM8998_MASK | BIT(DPU_SSPP_CURSOR))

#define MIXER_MSM8998_MASK \
	(BIT(DPU_MIXER_SOURCESPLIT))

#define MIXER_SDM845_MASK \
	(BIT(DPU_MIXER_SOURCESPLIT) | BIT(DPU_DIM_LAYER) | BIT(DPU_MIXER_COMBINED_ALPHA))

#define MIXER_QCM2290_MASK \
	(BIT(DPU_DIM_LAYER) | BIT(DPU_MIXER_COMBINED_ALPHA))

#define PINGPONG_SDM845_MASK \
	(BIT(DPU_PINGPONG_DITHER) | BIT(DPU_PINGPONG_TE) | BIT(DPU_PINGPONG_DSC))

#define PINGPONG_SDM845_TE2_MASK \
	(PINGPONG_SDM845_MASK | BIT(DPU_PINGPONG_TE2))

#define PINGPONG_SM8150_MASK \
	(BIT(DPU_PINGPONG_DITHER) | BIT(DPU_PINGPONG_DSC))

#define CTL_SC7280_MASK \
	(BIT(DPU_CTL_ACTIVE_CFG) | \
	 BIT(DPU_CTL_FETCH_ACTIVE) | \
	 BIT(DPU_CTL_VM_CFG) | \
	 BIT(DPU_CTL_DSPP_SUB_BLOCK_FLUSH))

#define CTL_SM8550_MASK \
	(CTL_SC7280_MASK | BIT(DPU_CTL_HAS_LAYER_EXT4))

#define DSPP_SC7180_MASK BIT(DPU_DSPP_PCC)

#define INTF_SC7180_MASK \
	(BIT(DPU_INTF_INPUT_CTRL) | \
	 BIT(DPU_INTF_TE) | \
	 BIT(DPU_INTF_STATUS_SUPPORTED) | \
	 BIT(DPU_DATA_HCTL_EN))

#define INTF_SC7280_MASK (INTF_SC7180_MASK | BIT(DPU_INTF_DATA_COMPRESS))

#define WB_SM8250_MASK (BIT(DPU_WB_LINE_MODE) | \
			 BIT(DPU_WB_UBWC) | \
			 BIT(DPU_WB_YUV_CONFIG) | \
			 BIT(DPU_WB_PIPE_ALPHA) | \
			 BIT(DPU_WB_XY_ROI_OFFSET) | \
			 BIT(DPU_WB_QOS) | \
			 BIT(DPU_WB_QOS_8LVL) | \
			 BIT(DPU_WB_CDP) | \
			 BIT(DPU_WB_INPUT_CTRL))

#define DEFAULT_PIXEL_RAM_SIZE		(50 * 1024)
#define DEFAULT_DPU_LINE_WIDTH		2048
#define DEFAULT_DPU_OUTPUT_LINE_WIDTH	2560

#define MAX_HORZ_DECIMATION	4
#define MAX_VERT_DECIMATION	4

#define MAX_UPSCALE_RATIO	20
#define MAX_DOWNSCALE_RATIO	4
#define SSPP_UNITY_SCALE	1

#define STRCAT(X, Y) (X Y)

static const uint32_t plane_formats[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_BGRA5551,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_RGBX5551,
	DRM_FORMAT_BGRX5551,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ABGR4444,
	DRM_FORMAT_RGBA4444,
	DRM_FORMAT_BGRA4444,
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_XBGR4444,
	DRM_FORMAT_RGBX4444,
	DRM_FORMAT_BGRX4444,
};

static const uint32_t plane_formats_yuv[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_BGRA5551,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_RGBX5551,
	DRM_FORMAT_BGRX5551,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ABGR4444,
	DRM_FORMAT_RGBA4444,
	DRM_FORMAT_BGRA4444,
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_XBGR4444,
	DRM_FORMAT_RGBX4444,
	DRM_FORMAT_BGRX4444,

	DRM_FORMAT_P010,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
	DRM_FORMAT_NV16,
	DRM_FORMAT_NV61,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YVU420,
};

static const u32 rotation_v2_formats[] = {
	DRM_FORMAT_NV12,
	/* TODO add formats after validation */
};

static const uint32_t wb2_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_RGBX5551,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_RGBA4444,
	DRM_FORMAT_RGBX4444,
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_BGRA5551,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_BGRX5551,
	DRM_FORMAT_ABGR4444,
	DRM_FORMAT_BGRA4444,
	DRM_FORMAT_BGRX4444,
	DRM_FORMAT_XBGR4444,
};

/*************************************************************
 * SSPP sub blocks config
 *************************************************************/

/* SSPP common configuration */
#define _VIG_SBLK(num, sdma_pri, qseed_ver) \
	{ \
	.maxdwnscale = MAX_DOWNSCALE_RATIO, \
	.maxupscale = MAX_UPSCALE_RATIO, \
	.smart_dma_priority = sdma_pri, \
	.scaler_blk = {.name = STRCAT("sspp_scaler", num), \
		.id = qseed_ver, \
		.base = 0xa00, .len = 0xa0,}, \
	.csc_blk = {.name = STRCAT("sspp_csc", num), \
		.id = DPU_SSPP_CSC_10BIT, \
		.base = 0x1a00, .len = 0x100,}, \
	.format_list = plane_formats_yuv, \
	.num_formats = ARRAY_SIZE(plane_formats_yuv), \
	.virt_format_list = plane_formats, \
	.virt_num_formats = ARRAY_SIZE(plane_formats), \
	.rotation_cfg = NULL, \
	}

#define _VIG_SBLK_ROT(num, sdma_pri, qseed_ver, rot_cfg) \
	{ \
	.maxdwnscale = MAX_DOWNSCALE_RATIO, \
	.maxupscale = MAX_UPSCALE_RATIO, \
	.smart_dma_priority = sdma_pri, \
	.scaler_blk = {.name = STRCAT("sspp_scaler", num), \
		.id = qseed_ver, \
		.base = 0xa00, .len = 0xa0,}, \
	.csc_blk = {.name = STRCAT("sspp_csc", num), \
		.id = DPU_SSPP_CSC_10BIT, \
		.base = 0x1a00, .len = 0x100,}, \
	.format_list = plane_formats_yuv, \
	.num_formats = ARRAY_SIZE(plane_formats_yuv), \
	.virt_format_list = plane_formats, \
	.virt_num_formats = ARRAY_SIZE(plane_formats), \
	.rotation_cfg = rot_cfg, \
	}

#define _DMA_SBLK(num, sdma_pri) \
	{ \
	.maxdwnscale = SSPP_UNITY_SCALE, \
	.maxupscale = SSPP_UNITY_SCALE, \
	.smart_dma_priority = sdma_pri, \
	.format_list = plane_formats, \
	.num_formats = ARRAY_SIZE(plane_formats), \
	.virt_format_list = plane_formats, \
	.virt_num_formats = ARRAY_SIZE(plane_formats), \
	}

static const struct dpu_sspp_sub_blks msm8998_vig_sblk_0 =
				_VIG_SBLK("0", 0, DPU_SSPP_SCALER_QSEED3);
static const struct dpu_sspp_sub_blks msm8998_vig_sblk_1 =
				_VIG_SBLK("1", 0, DPU_SSPP_SCALER_QSEED3);
static const struct dpu_sspp_sub_blks msm8998_vig_sblk_2 =
				_VIG_SBLK("2", 0, DPU_SSPP_SCALER_QSEED3);
static const struct dpu_sspp_sub_blks msm8998_vig_sblk_3 =
				_VIG_SBLK("3", 0, DPU_SSPP_SCALER_QSEED3);

static const struct dpu_rotation_cfg dpu_rot_sc7280_cfg_v2 = {
	.rot_maxheight = 1088,
	.rot_num_formats = ARRAY_SIZE(rotation_v2_formats),
	.rot_format_list = rotation_v2_formats,
};

static const struct dpu_sspp_sub_blks sdm845_vig_sblk_0 =
				_VIG_SBLK("0", 5, DPU_SSPP_SCALER_QSEED3);
static const struct dpu_sspp_sub_blks sdm845_vig_sblk_1 =
				_VIG_SBLK("1", 6, DPU_SSPP_SCALER_QSEED3);
static const struct dpu_sspp_sub_blks sdm845_vig_sblk_2 =
				_VIG_SBLK("2", 7, DPU_SSPP_SCALER_QSEED3);
static const struct dpu_sspp_sub_blks sdm845_vig_sblk_3 =
				_VIG_SBLK("3", 8, DPU_SSPP_SCALER_QSEED3);

static const struct dpu_sspp_sub_blks sdm845_dma_sblk_0 = _DMA_SBLK("8", 1);
static const struct dpu_sspp_sub_blks sdm845_dma_sblk_1 = _DMA_SBLK("9", 2);
static const struct dpu_sspp_sub_blks sdm845_dma_sblk_2 = _DMA_SBLK("10", 3);
static const struct dpu_sspp_sub_blks sdm845_dma_sblk_3 = _DMA_SBLK("11", 4);

static const struct dpu_sspp_sub_blks sc7180_vig_sblk_0 =
				_VIG_SBLK("0", 4, DPU_SSPP_SCALER_QSEED4);

static const struct dpu_sspp_sub_blks sc7280_vig_sblk_0 =
			_VIG_SBLK_ROT("0", 4, DPU_SSPP_SCALER_QSEED4, &dpu_rot_sc7280_cfg_v2);

static const struct dpu_sspp_sub_blks sm6115_vig_sblk_0 =
				_VIG_SBLK("0", 2, DPU_SSPP_SCALER_QSEED4);

static const struct dpu_sspp_sub_blks sm8250_vig_sblk_0 =
				_VIG_SBLK("0", 5, DPU_SSPP_SCALER_QSEED4);
static const struct dpu_sspp_sub_blks sm8250_vig_sblk_1 =
				_VIG_SBLK("1", 6, DPU_SSPP_SCALER_QSEED4);
static const struct dpu_sspp_sub_blks sm8250_vig_sblk_2 =
				_VIG_SBLK("2", 7, DPU_SSPP_SCALER_QSEED4);
static const struct dpu_sspp_sub_blks sm8250_vig_sblk_3 =
				_VIG_SBLK("3", 8, DPU_SSPP_SCALER_QSEED4);

static const struct dpu_sspp_sub_blks sm8550_vig_sblk_0 =
				_VIG_SBLK("0", 7, DPU_SSPP_SCALER_QSEED4);
static const struct dpu_sspp_sub_blks sm8550_vig_sblk_1 =
				_VIG_SBLK("1", 8, DPU_SSPP_SCALER_QSEED4);
static const struct dpu_sspp_sub_blks sm8550_vig_sblk_2 =
				_VIG_SBLK("2", 9, DPU_SSPP_SCALER_QSEED4);
static const struct dpu_sspp_sub_blks sm8550_vig_sblk_3 =
				_VIG_SBLK("3", 10, DPU_SSPP_SCALER_QSEED4);
static const struct dpu_sspp_sub_blks sm8550_dma_sblk_4 = _DMA_SBLK("12", 5);
static const struct dpu_sspp_sub_blks sm8550_dma_sblk_5 = _DMA_SBLK("13", 6);

#define _VIG_SBLK_NOSCALE(num, sdma_pri) \
	{ \
	.maxdwnscale = SSPP_UNITY_SCALE, \
	.maxupscale = SSPP_UNITY_SCALE, \
	.smart_dma_priority = sdma_pri, \
	.format_list = plane_formats_yuv, \
	.num_formats = ARRAY_SIZE(plane_formats_yuv), \
	.virt_format_list = plane_formats, \
	.virt_num_formats = ARRAY_SIZE(plane_formats), \
	}

static const struct dpu_sspp_sub_blks qcm2290_vig_sblk_0 = _VIG_SBLK_NOSCALE("0", 2);
static const struct dpu_sspp_sub_blks qcm2290_dma_sblk_0 = _DMA_SBLK("8", 1);

/*************************************************************
 * MIXER sub blocks config
 *************************************************************/

/* MSM8998 */

static const struct dpu_lm_sub_blks msm8998_lm_sblk = {
	.maxwidth = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.maxblendstages = 7, /* excluding base layer */
	.blendstage_base = { /* offsets relative to mixer base */
		0x20, 0x50, 0x80, 0xb0, 0x230,
		0x260, 0x290
	},
};

/* SDM845 */

static const struct dpu_lm_sub_blks sdm845_lm_sblk = {
	.maxwidth = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.maxblendstages = 11, /* excluding base layer */
	.blendstage_base = { /* offsets relative to mixer base */
		0x20, 0x38, 0x50, 0x68, 0x80, 0x98,
		0xb0, 0xc8, 0xe0, 0xf8, 0x110
	},
};

/* SC7180 */

static const struct dpu_lm_sub_blks sc7180_lm_sblk = {
	.maxwidth = DEFAULT_DPU_OUTPUT_LINE_WIDTH,
	.maxblendstages = 7, /* excluding base layer */
	.blendstage_base = { /* offsets relative to mixer base */
		0x20, 0x38, 0x50, 0x68, 0x80, 0x98, 0xb0
	},
};

/* QCM2290 */

static const struct dpu_lm_sub_blks qcm2290_lm_sblk = {
	.maxwidth = DEFAULT_DPU_LINE_WIDTH,
	.maxblendstages = 4, /* excluding base layer */
	.blendstage_base = { /* offsets relative to mixer base */
		0x20, 0x38, 0x50, 0x68
	},
};

/*************************************************************
 * DSPP sub blocks config
 *************************************************************/
static const struct dpu_dspp_sub_blks msm8998_dspp_sblk = {
	.pcc = {.id = DPU_DSPP_PCC, .base = 0x1700,
		.len = 0x90, .version = 0x10007},
};

static const struct dpu_dspp_sub_blks sdm845_dspp_sblk = {
	.pcc = {.id = DPU_DSPP_PCC, .base = 0x1700,
		.len = 0x90, .version = 0x40000},
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

static const struct dpu_pingpong_sub_blks sc7280_pp_sblk = {
	.dither = {.id = DPU_PINGPONG_DITHER, .base = 0xe0,
	.len = 0x20, .version = 0x20000},
};

/*************************************************************
 * DSC sub blocks config
 *************************************************************/
static const struct dpu_dsc_sub_blks dsc_sblk_0 = {
	.enc = {.base = 0x100, .len = 0x100},
	.ctl = {.base = 0xF00, .len = 0x10},
};

static const struct dpu_dsc_sub_blks dsc_sblk_1 = {
	.enc = {.base = 0x200, .len = 0x100},
	.ctl = {.base = 0xF80, .len = 0x10},
};

/*************************************************************
 * VBIF sub blocks config
 *************************************************************/
/* VBIF QOS remap */
static const u32 msm8998_rt_pri_lvl[] = {1, 2, 2, 2};
static const u32 msm8998_nrt_pri_lvl[] = {1, 1, 1, 1};
static const u32 sdm845_rt_pri_lvl[] = {3, 3, 4, 4, 5, 5, 6, 6};
static const u32 sdm845_nrt_pri_lvl[] = {3, 3, 3, 3, 3, 3, 3, 3};

static const struct dpu_vbif_dynamic_ot_cfg msm8998_ot_rdwr_cfg[] = {
	{
		.pps = 1920 * 1080 * 30,
		.ot_limit = 2,
	},
	{
		.pps = 1920 * 1080 * 60,
		.ot_limit = 4,
	},
	{
		.pps = 3840 * 2160 * 30,
		.ot_limit = 16,
	},
};

static const struct dpu_vbif_cfg msm8998_vbif[] = {
	{
	.name = "vbif_rt", .id = VBIF_RT,
	.base = 0, .len = 0x1040,
	.default_ot_rd_limit = 32,
	.default_ot_wr_limit = 32,
	.features = BIT(DPU_VBIF_QOS_REMAP) | BIT(DPU_VBIF_QOS_OTLIM),
	.xin_halt_timeout = 0x4000,
	.qos_rp_remap_size = 0x20,
	.dynamic_ot_rd_tbl = {
		.count = ARRAY_SIZE(msm8998_ot_rdwr_cfg),
		.cfg = msm8998_ot_rdwr_cfg,
		},
	.dynamic_ot_wr_tbl = {
		.count = ARRAY_SIZE(msm8998_ot_rdwr_cfg),
		.cfg = msm8998_ot_rdwr_cfg,
		},
	.qos_rt_tbl = {
		.npriority_lvl = ARRAY_SIZE(msm8998_rt_pri_lvl),
		.priority_lvl = msm8998_rt_pri_lvl,
		},
	.qos_nrt_tbl = {
		.npriority_lvl = ARRAY_SIZE(msm8998_nrt_pri_lvl),
		.priority_lvl = msm8998_nrt_pri_lvl,
		},
	.memtype_count = 14,
	.memtype = {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2},
	},
};

static const struct dpu_vbif_cfg sdm845_vbif[] = {
	{
	.name = "vbif_rt", .id = VBIF_RT,
	.base = 0, .len = 0x1040,
	.features = BIT(DPU_VBIF_QOS_REMAP),
	.xin_halt_timeout = 0x4000,
	.qos_rp_remap_size = 0x40,
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

/*************************************************************
 * PERF data config
 *************************************************************/

/* SSPP QOS LUTs */
static const struct dpu_qos_lut_entry msm8998_qos_linear[] = {
	{.fl = 4,  .lut = 0x1b},
	{.fl = 5,  .lut = 0x5b},
	{.fl = 6,  .lut = 0x15b},
	{.fl = 7,  .lut = 0x55b},
	{.fl = 8,  .lut = 0x155b},
	{.fl = 9,  .lut = 0x555b},
	{.fl = 10, .lut = 0x1555b},
	{.fl = 11, .lut = 0x5555b},
	{.fl = 12, .lut = 0x15555b},
	{.fl = 0,  .lut = 0x55555b}
};

static const struct dpu_qos_lut_entry sdm845_qos_linear[] = {
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

static const struct dpu_qos_lut_entry msm8998_qos_macrotile[] = {
	{.fl = 10, .lut = 0x1aaff},
	{.fl = 11, .lut = 0x5aaff},
	{.fl = 12, .lut = 0x15aaff},
	{.fl = 0,  .lut = 0x55aaff},
};

static const struct dpu_qos_lut_entry sc7180_qos_linear[] = {
	{.fl = 0, .lut = 0x0011222222335777},
};

static const struct dpu_qos_lut_entry sm6350_qos_linear_macrotile[] = {
	{.fl = 0, .lut = 0x0011223445566777 },
};

static const struct dpu_qos_lut_entry sm8150_qos_linear[] = {
	{.fl = 0, .lut = 0x0011222222223357 },
};

static const struct dpu_qos_lut_entry sc8180x_qos_linear[] = {
	{.fl = 4, .lut = 0x0000000000000357 },
};

static const struct dpu_qos_lut_entry qcm2290_qos_linear[] = {
	{.fl = 0, .lut = 0x0011222222335777},
};

static const struct dpu_qos_lut_entry sdm845_qos_macrotile[] = {
	{.fl = 10, .lut = 0x344556677},
	{.fl = 11, .lut = 0x3344556677},
	{.fl = 12, .lut = 0x23344556677},
	{.fl = 13, .lut = 0x223344556677},
	{.fl = 14, .lut = 0x1223344556677},
	{.fl = 0, .lut = 0x112233344556677},
};

static const struct dpu_qos_lut_entry sc7180_qos_macrotile[] = {
	{.fl = 0, .lut = 0x0011223344556677},
};

static const struct dpu_qos_lut_entry sc8180x_qos_macrotile[] = {
	{.fl = 10, .lut = 0x0000000344556677},
};

static const struct dpu_qos_lut_entry msm8998_qos_nrt[] = {
	{.fl = 0, .lut = 0x0},
};

static const struct dpu_qos_lut_entry sdm845_qos_nrt[] = {
	{.fl = 0, .lut = 0x0},
};

static const struct dpu_qos_lut_entry sc7180_qos_nrt[] = {
	{.fl = 0, .lut = 0x0},
};

/*************************************************************
 * Hardware catalog
 *************************************************************/

#include "catalog/dpu_3_0_msm8998.h"

#include "catalog/dpu_4_0_sdm845.h"

#include "catalog/dpu_5_0_sm8150.h"
#include "catalog/dpu_5_1_sc8180x.h"

#include "catalog/dpu_6_0_sm8250.h"
#include "catalog/dpu_6_2_sc7180.h"
#include "catalog/dpu_6_3_sm6115.h"
#include "catalog/dpu_6_4_sm6350.h"
#include "catalog/dpu_6_5_qcm2290.h"
#include "catalog/dpu_6_9_sm6375.h"

#include "catalog/dpu_7_0_sm8350.h"
#include "catalog/dpu_7_2_sc7280.h"

#include "catalog/dpu_8_0_sc8280xp.h"
#include "catalog/dpu_8_1_sm8450.h"

#include "catalog/dpu_9_0_sm8550.h"
