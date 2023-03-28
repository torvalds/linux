/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_HW_MDSS_H
#define _DPU_HW_MDSS_H

#include <linux/kernel.h>
#include <linux/err.h>

#include "msm_drv.h"

#define DPU_DBG_NAME			"dpu"

#define DPU_NONE                        0

#ifndef DPU_CSC_MATRIX_COEFF_SIZE
#define DPU_CSC_MATRIX_COEFF_SIZE	9
#endif

#ifndef DPU_CSC_CLAMP_SIZE
#define DPU_CSC_CLAMP_SIZE		6
#endif

#ifndef DPU_CSC_BIAS_SIZE
#define DPU_CSC_BIAS_SIZE		3
#endif

#ifndef DPU_MAX_PLANES
#define DPU_MAX_PLANES			4
#endif

#define PIPES_PER_STAGE			2
#ifndef DPU_MAX_DE_CURVES
#define DPU_MAX_DE_CURVES		3
#endif

enum dpu_format_flags {
	DPU_FORMAT_FLAG_YUV_BIT,
	DPU_FORMAT_FLAG_DX_BIT,
	DPU_FORMAT_FLAG_COMPRESSED_BIT,
	DPU_FORMAT_FLAG_BIT_MAX,
};

#define DPU_FORMAT_FLAG_YUV		BIT(DPU_FORMAT_FLAG_YUV_BIT)
#define DPU_FORMAT_FLAG_DX		BIT(DPU_FORMAT_FLAG_DX_BIT)
#define DPU_FORMAT_FLAG_COMPRESSED	BIT(DPU_FORMAT_FLAG_COMPRESSED_BIT)
#define DPU_FORMAT_IS_YUV(X)		\
	(test_bit(DPU_FORMAT_FLAG_YUV_BIT, (X)->flag))
#define DPU_FORMAT_IS_DX(X)		\
	(test_bit(DPU_FORMAT_FLAG_DX_BIT, (X)->flag))
#define DPU_FORMAT_IS_LINEAR(X)		((X)->fetch_mode == DPU_FETCH_LINEAR)
#define DPU_FORMAT_IS_TILE(X) \
	(((X)->fetch_mode == DPU_FETCH_UBWC) && \
			!test_bit(DPU_FORMAT_FLAG_COMPRESSED_BIT, (X)->flag))
#define DPU_FORMAT_IS_UBWC(X) \
	(((X)->fetch_mode == DPU_FETCH_UBWC) && \
			test_bit(DPU_FORMAT_FLAG_COMPRESSED_BIT, (X)->flag))

#define DPU_BLEND_FG_ALPHA_FG_CONST	(0 << 0)
#define DPU_BLEND_FG_ALPHA_BG_CONST	(1 << 0)
#define DPU_BLEND_FG_ALPHA_FG_PIXEL	(2 << 0)
#define DPU_BLEND_FG_ALPHA_BG_PIXEL	(3 << 0)
#define DPU_BLEND_FG_INV_ALPHA		(1 << 2)
#define DPU_BLEND_FG_MOD_ALPHA		(1 << 3)
#define DPU_BLEND_FG_INV_MOD_ALPHA	(1 << 4)
#define DPU_BLEND_FG_TRANSP_EN		(1 << 5)
#define DPU_BLEND_BG_ALPHA_FG_CONST	(0 << 8)
#define DPU_BLEND_BG_ALPHA_BG_CONST	(1 << 8)
#define DPU_BLEND_BG_ALPHA_FG_PIXEL	(2 << 8)
#define DPU_BLEND_BG_ALPHA_BG_PIXEL	(3 << 8)
#define DPU_BLEND_BG_INV_ALPHA		(1 << 10)
#define DPU_BLEND_BG_MOD_ALPHA		(1 << 11)
#define DPU_BLEND_BG_INV_MOD_ALPHA	(1 << 12)
#define DPU_BLEND_BG_TRANSP_EN		(1 << 13)

#define DPU_VSYNC0_SOURCE_GPIO		0
#define DPU_VSYNC1_SOURCE_GPIO		1
#define DPU_VSYNC2_SOURCE_GPIO		2
#define DPU_VSYNC_SOURCE_INTF_0		3
#define DPU_VSYNC_SOURCE_INTF_1		4
#define DPU_VSYNC_SOURCE_INTF_2		5
#define DPU_VSYNC_SOURCE_INTF_3		6
#define DPU_VSYNC_SOURCE_WD_TIMER_4	11
#define DPU_VSYNC_SOURCE_WD_TIMER_3	12
#define DPU_VSYNC_SOURCE_WD_TIMER_2	13
#define DPU_VSYNC_SOURCE_WD_TIMER_1	14
#define DPU_VSYNC_SOURCE_WD_TIMER_0	15

enum dpu_hw_blk_type {
	DPU_HW_BLK_TOP = 0,
	DPU_HW_BLK_SSPP,
	DPU_HW_BLK_LM,
	DPU_HW_BLK_CTL,
	DPU_HW_BLK_PINGPONG,
	DPU_HW_BLK_INTF,
	DPU_HW_BLK_WB,
	DPU_HW_BLK_DSPP,
	DPU_HW_BLK_MERGE_3D,
	DPU_HW_BLK_DSC,
	DPU_HW_BLK_MAX,
};

enum dpu_mdp {
	MDP_TOP = 0x1,
	MDP_MAX,
};

enum dpu_sspp {
	SSPP_NONE,
	SSPP_VIG0,
	SSPP_VIG1,
	SSPP_VIG2,
	SSPP_VIG3,
	SSPP_RGB0,
	SSPP_RGB1,
	SSPP_RGB2,
	SSPP_RGB3,
	SSPP_DMA0,
	SSPP_DMA1,
	SSPP_DMA2,
	SSPP_DMA3,
	SSPP_DMA4,
	SSPP_DMA5,
	SSPP_CURSOR0,
	SSPP_CURSOR1,
	SSPP_MAX
};

enum dpu_sspp_type {
	SSPP_TYPE_VIG,
	SSPP_TYPE_RGB,
	SSPP_TYPE_DMA,
	SSPP_TYPE_CURSOR,
	SSPP_TYPE_MAX
};

enum dpu_lm {
	LM_0 = 1,
	LM_1,
	LM_2,
	LM_3,
	LM_4,
	LM_5,
	LM_6,
	LM_MAX
};

enum dpu_stage {
	DPU_STAGE_BASE = 0,
	DPU_STAGE_0,
	DPU_STAGE_1,
	DPU_STAGE_2,
	DPU_STAGE_3,
	DPU_STAGE_4,
	DPU_STAGE_5,
	DPU_STAGE_6,
	DPU_STAGE_7,
	DPU_STAGE_8,
	DPU_STAGE_9,
	DPU_STAGE_10,
	DPU_STAGE_MAX
};
enum dpu_dspp {
	DSPP_0 = 1,
	DSPP_1,
	DSPP_2,
	DSPP_3,
	DSPP_MAX
};

enum dpu_ctl {
	CTL_0 = 1,
	CTL_1,
	CTL_2,
	CTL_3,
	CTL_4,
	CTL_5,
	CTL_MAX
};

enum dpu_dsc {
	DSC_NONE = 0,
	DSC_0,
	DSC_1,
	DSC_2,
	DSC_3,
	DSC_4,
	DSC_5,
	DSC_MAX
};

enum dpu_pingpong {
	PINGPONG_0 = 1,
	PINGPONG_1,
	PINGPONG_2,
	PINGPONG_3,
	PINGPONG_4,
	PINGPONG_5,
	PINGPONG_6,
	PINGPONG_7,
	PINGPONG_S0,
	PINGPONG_MAX
};

enum dpu_merge_3d {
	MERGE_3D_0 = 1,
	MERGE_3D_1,
	MERGE_3D_2,
	MERGE_3D_3,
	MERGE_3D_MAX
};

enum dpu_intf {
	INTF_0 = 1,
	INTF_1,
	INTF_2,
	INTF_3,
	INTF_4,
	INTF_5,
	INTF_6,
	INTF_7,
	INTF_8,
	INTF_MAX
};

/*
 * Historically these values correspond to the values written to the
 * DISP_INTF_SEL register, which had to programmed manually. On newer MDP
 * generations this register is NOP, but we keep the values for historical
 * reasons.
 */
enum dpu_intf_type {
	INTF_NONE = 0x0,
	INTF_DSI = 0x1,
	INTF_HDMI = 0x3,
	INTF_LCDC = 0x5,
	/* old eDP found on 8x74 and 8x84 */
	INTF_EDP = 0x9,
	/* both DP and eDP,  handled by the new DP driver */
	INTF_DP = 0xa,

	/* virtual interfaces */
	INTF_WB = 0x100,
};

enum dpu_intf_mode {
	INTF_MODE_NONE = 0,
	INTF_MODE_CMD,
	INTF_MODE_VIDEO,
	INTF_MODE_WB_BLOCK,
	INTF_MODE_WB_LINE,
	INTF_MODE_MAX
};

enum dpu_wb {
	WB_0 = 1,
	WB_1,
	WB_2,
	WB_3,
	WB_MAX
};

enum dpu_cwb {
	CWB_0 = 0x1,
	CWB_1,
	CWB_2,
	CWB_3,
	CWB_MAX
};

enum dpu_wd_timer {
	WD_TIMER_0 = 0x1,
	WD_TIMER_1,
	WD_TIMER_2,
	WD_TIMER_3,
	WD_TIMER_4,
	WD_TIMER_5,
	WD_TIMER_MAX
};

enum dpu_vbif {
	VBIF_RT,
	VBIF_NRT,
	VBIF_MAX,
};

/**
 * DPU HW,Component order color map
 */
enum {
	C0_G_Y = 0,
	C1_B_Cb = 1,
	C2_R_Cr = 2,
	C3_ALPHA = 3
};

/**
 * enum dpu_plane_type - defines how the color component pixel packing
 * @DPU_PLANE_INTERLEAVED   : Color components in single plane
 * @DPU_PLANE_PLANAR        : Color component in separate planes
 * @DPU_PLANE_PSEUDO_PLANAR : Chroma components interleaved in separate plane
 */
enum dpu_plane_type {
	DPU_PLANE_INTERLEAVED,
	DPU_PLANE_PLANAR,
	DPU_PLANE_PSEUDO_PLANAR,
};

/**
 * enum dpu_chroma_samp_type - chroma sub-samplng type
 * @DPU_CHROMA_RGB   : No chroma subsampling
 * @DPU_CHROMA_H2V1  : Chroma pixels are horizontally subsampled
 * @DPU_CHROMA_H1V2  : Chroma pixels are vertically subsampled
 * @DPU_CHROMA_420   : 420 subsampling
 */
enum dpu_chroma_samp_type {
	DPU_CHROMA_RGB,
	DPU_CHROMA_H2V1,
	DPU_CHROMA_H1V2,
	DPU_CHROMA_420
};

/**
 * dpu_fetch_type - Defines How DPU HW fetches data
 * @DPU_FETCH_LINEAR   : fetch is line by line
 * @DPU_FETCH_TILE     : fetches data in Z order from a tile
 * @DPU_FETCH_UBWC     : fetch and decompress data
 */
enum dpu_fetch_type {
	DPU_FETCH_LINEAR,
	DPU_FETCH_TILE,
	DPU_FETCH_UBWC
};

/**
 * Value of enum chosen to fit the number of bits
 * expected by the HW programming.
 */
enum {
	COLOR_ALPHA_1BIT = 0,
	COLOR_ALPHA_4BIT = 1,
	COLOR_4BIT = 0,
	COLOR_5BIT = 1, /* No 5-bit Alpha */
	COLOR_6BIT = 2, /* 6-Bit Alpha also = 2 */
	COLOR_8BIT = 3, /* 8-Bit Alpha also = 3 */
};

/**
 * enum dpu_3d_blend_mode
 * Desribes how the 3d data is blended
 * @BLEND_3D_NONE      : 3d blending not enabled
 * @BLEND_3D_FRAME_INT : Frame interleaving
 * @BLEND_3D_H_ROW_INT : Horizontal row interleaving
 * @BLEND_3D_V_ROW_INT : vertical row interleaving
 * @BLEND_3D_COL_INT   : column interleaving
 * @BLEND_3D_MAX       :
 */
enum dpu_3d_blend_mode {
	BLEND_3D_NONE = 0,
	BLEND_3D_FRAME_INT,
	BLEND_3D_H_ROW_INT,
	BLEND_3D_V_ROW_INT,
	BLEND_3D_COL_INT,
	BLEND_3D_MAX
};

/** struct dpu_format - defines the format configuration which
 * allows DPU HW to correctly fetch and decode the format
 * @base: base msm_format structure containing fourcc code
 * @fetch_planes: how the color components are packed in pixel format
 * @element: element color ordering
 * @bits: element bit widths
 * @chroma_sample: chroma sub-samplng type
 * @unpack_align_msb: unpack aligned, 0 to LSB, 1 to MSB
 * @unpack_tight: 0 for loose, 1 for tight
 * @unpack_count: 0 = 1 component, 1 = 2 component
 * @bpp: bytes per pixel
 * @alpha_enable: whether the format has an alpha channel
 * @num_planes: number of planes (including meta data planes)
 * @fetch_mode: linear, tiled, or ubwc hw fetch behavior
 * @flag: usage bit flags
 * @tile_width: format tile width
 * @tile_height: format tile height
 */
struct dpu_format {
	struct msm_format base;
	enum dpu_plane_type fetch_planes;
	u8 element[DPU_MAX_PLANES];
	u8 bits[DPU_MAX_PLANES];
	enum dpu_chroma_samp_type chroma_sample;
	u8 unpack_align_msb;
	u8 unpack_tight;
	u8 unpack_count;
	u8 bpp;
	u8 alpha_enable;
	u8 num_planes;
	enum dpu_fetch_type fetch_mode;
	DECLARE_BITMAP(flag, DPU_FORMAT_FLAG_BIT_MAX);
	u16 tile_width;
	u16 tile_height;
};
#define to_dpu_format(x) container_of(x, struct dpu_format, base)

/**
 * struct dpu_hw_fmt_layout - format information of the source pixel data
 * @format: pixel format parameters
 * @num_planes: number of planes (including meta data planes)
 * @width: image width
 * @height: image height
 * @total_size: total size in bytes
 * @plane_addr: address of each plane
 * @plane_size: length of each plane
 * @plane_pitch: pitch of each plane
 */
struct dpu_hw_fmt_layout {
	const struct dpu_format *format;
	uint32_t num_planes;
	uint32_t width;
	uint32_t height;
	uint32_t total_size;
	uint32_t plane_addr[DPU_MAX_PLANES];
	uint32_t plane_size[DPU_MAX_PLANES];
	uint32_t plane_pitch[DPU_MAX_PLANES];
};

struct dpu_csc_cfg {
	/* matrix coefficients in S15.16 format */
	uint32_t csc_mv[DPU_CSC_MATRIX_COEFF_SIZE];
	uint32_t csc_pre_bv[DPU_CSC_BIAS_SIZE];
	uint32_t csc_post_bv[DPU_CSC_BIAS_SIZE];
	uint32_t csc_pre_lv[DPU_CSC_CLAMP_SIZE];
	uint32_t csc_post_lv[DPU_CSC_CLAMP_SIZE];
};

/**
 * struct dpu_mdss_color - mdss color description
 * color 0 : green
 * color 1 : blue
 * color 2 : red
 * color 3 : alpha
 */
struct dpu_mdss_color {
	u32 color_0;
	u32 color_1;
	u32 color_2;
	u32 color_3;
};

/*
 * Define bit masks for h/w logging.
 */
#define DPU_DBG_MASK_NONE     (1 << 0)
#define DPU_DBG_MASK_INTF     (1 << 1)
#define DPU_DBG_MASK_LM       (1 << 2)
#define DPU_DBG_MASK_CTL      (1 << 3)
#define DPU_DBG_MASK_PINGPONG (1 << 4)
#define DPU_DBG_MASK_SSPP     (1 << 5)
#define DPU_DBG_MASK_WB       (1 << 6)
#define DPU_DBG_MASK_TOP      (1 << 7)
#define DPU_DBG_MASK_VBIF     (1 << 8)
#define DPU_DBG_MASK_ROT      (1 << 9)
#define DPU_DBG_MASK_DSPP     (1 << 10)
#define DPU_DBG_MASK_DSC      (1 << 11)

#endif  /* _DPU_HW_MDSS_H */
