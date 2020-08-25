// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#include "dpu_hwio.h"
#include "dpu_hw_catalog.h"
#include "dpu_hw_lm.h"
#include "dpu_hw_sspp.h"
#include "dpu_kms.h"

#define DPU_FETCH_CONFIG_RESET_VALUE   0x00000087

/* DPU_SSPP_SRC */
#define SSPP_SRC_SIZE                      0x00
#define SSPP_SRC_XY                        0x08
#define SSPP_OUT_SIZE                      0x0c
#define SSPP_OUT_XY                        0x10
#define SSPP_SRC0_ADDR                     0x14
#define SSPP_SRC1_ADDR                     0x18
#define SSPP_SRC2_ADDR                     0x1C
#define SSPP_SRC3_ADDR                     0x20
#define SSPP_SRC_YSTRIDE0                  0x24
#define SSPP_SRC_YSTRIDE1                  0x28
#define SSPP_SRC_FORMAT                    0x30
#define SSPP_SRC_UNPACK_PATTERN            0x34
#define SSPP_SRC_OP_MODE                   0x38

/* SSPP_MULTIRECT*/
#define SSPP_SRC_SIZE_REC1                 0x16C
#define SSPP_SRC_XY_REC1                   0x168
#define SSPP_OUT_SIZE_REC1                 0x160
#define SSPP_OUT_XY_REC1                   0x164
#define SSPP_SRC_FORMAT_REC1               0x174
#define SSPP_SRC_UNPACK_PATTERN_REC1       0x178
#define SSPP_SRC_OP_MODE_REC1              0x17C
#define SSPP_MULTIRECT_OPMODE              0x170
#define SSPP_SRC_CONSTANT_COLOR_REC1       0x180
#define SSPP_EXCL_REC_SIZE_REC1            0x184
#define SSPP_EXCL_REC_XY_REC1              0x188

#define MDSS_MDP_OP_DEINTERLACE            BIT(22)
#define MDSS_MDP_OP_DEINTERLACE_ODD        BIT(23)
#define MDSS_MDP_OP_IGC_ROM_1              BIT(18)
#define MDSS_MDP_OP_IGC_ROM_0              BIT(17)
#define MDSS_MDP_OP_IGC_EN                 BIT(16)
#define MDSS_MDP_OP_FLIP_UD                BIT(14)
#define MDSS_MDP_OP_FLIP_LR                BIT(13)
#define MDSS_MDP_OP_BWC_EN                 BIT(0)
#define MDSS_MDP_OP_PE_OVERRIDE            BIT(31)
#define MDSS_MDP_OP_BWC_LOSSLESS           (0 << 1)
#define MDSS_MDP_OP_BWC_Q_HIGH             (1 << 1)
#define MDSS_MDP_OP_BWC_Q_MED              (2 << 1)

#define SSPP_SRC_CONSTANT_COLOR            0x3c
#define SSPP_EXCL_REC_CTL                  0x40
#define SSPP_UBWC_STATIC_CTRL              0x44
#define SSPP_FETCH_CONFIG                  0x048
#define SSPP_DANGER_LUT                    0x60
#define SSPP_SAFE_LUT                      0x64
#define SSPP_CREQ_LUT                      0x68
#define SSPP_QOS_CTRL                      0x6C
#define SSPP_DECIMATION_CONFIG             0xB4
#define SSPP_SRC_ADDR_SW_STATUS            0x70
#define SSPP_CREQ_LUT_0                    0x74
#define SSPP_CREQ_LUT_1                    0x78
#define SSPP_SW_PIX_EXT_C0_LR              0x100
#define SSPP_SW_PIX_EXT_C0_TB              0x104
#define SSPP_SW_PIX_EXT_C0_REQ_PIXELS      0x108
#define SSPP_SW_PIX_EXT_C1C2_LR            0x110
#define SSPP_SW_PIX_EXT_C1C2_TB            0x114
#define SSPP_SW_PIX_EXT_C1C2_REQ_PIXELS    0x118
#define SSPP_SW_PIX_EXT_C3_LR              0x120
#define SSPP_SW_PIX_EXT_C3_TB              0x124
#define SSPP_SW_PIX_EXT_C3_REQ_PIXELS      0x128
#define SSPP_TRAFFIC_SHAPER                0x130
#define SSPP_CDP_CNTL                      0x134
#define SSPP_UBWC_ERROR_STATUS             0x138
#define SSPP_TRAFFIC_SHAPER_PREFILL        0x150
#define SSPP_TRAFFIC_SHAPER_REC1_PREFILL   0x154
#define SSPP_TRAFFIC_SHAPER_REC1           0x158
#define SSPP_EXCL_REC_SIZE                 0x1B4
#define SSPP_EXCL_REC_XY                   0x1B8
#define SSPP_VIG_OP_MODE                   0x0
#define SSPP_VIG_CSC_10_OP_MODE            0x0
#define SSPP_TRAFFIC_SHAPER_BPC_MAX        0xFF

/* SSPP_QOS_CTRL */
#define SSPP_QOS_CTRL_VBLANK_EN            BIT(16)
#define SSPP_QOS_CTRL_DANGER_SAFE_EN       BIT(0)
#define SSPP_QOS_CTRL_DANGER_VBLANK_MASK   0x3
#define SSPP_QOS_CTRL_DANGER_VBLANK_OFF    4
#define SSPP_QOS_CTRL_CREQ_VBLANK_MASK     0x3
#define SSPP_QOS_CTRL_CREQ_VBLANK_OFF      20

/* DPU_SSPP_SCALER_QSEED2 */
#define SCALE_CONFIG                       0x04
#define COMP0_3_PHASE_STEP_X               0x10
#define COMP0_3_PHASE_STEP_Y               0x14
#define COMP1_2_PHASE_STEP_X               0x18
#define COMP1_2_PHASE_STEP_Y               0x1c
#define COMP0_3_INIT_PHASE_X               0x20
#define COMP0_3_INIT_PHASE_Y               0x24
#define COMP1_2_INIT_PHASE_X               0x28
#define COMP1_2_INIT_PHASE_Y               0x2C
#define VIG_0_QSEED2_SHARP                 0x30

/*
 * Definitions for ViG op modes
 */
#define VIG_OP_CSC_DST_DATAFMT BIT(19)
#define VIG_OP_CSC_SRC_DATAFMT BIT(18)
#define VIG_OP_CSC_EN          BIT(17)
#define VIG_OP_MEM_PROT_CONT   BIT(15)
#define VIG_OP_MEM_PROT_VAL    BIT(14)
#define VIG_OP_MEM_PROT_SAT    BIT(13)
#define VIG_OP_MEM_PROT_HUE    BIT(12)
#define VIG_OP_HIST            BIT(8)
#define VIG_OP_SKY_COL         BIT(7)
#define VIG_OP_FOIL            BIT(6)
#define VIG_OP_SKIN_COL        BIT(5)
#define VIG_OP_PA_EN           BIT(4)
#define VIG_OP_PA_SAT_ZERO_EXP BIT(2)
#define VIG_OP_MEM_PROT_BLEND  BIT(1)

/*
 * Definitions for CSC 10 op modes
 */
#define VIG_CSC_10_SRC_DATAFMT BIT(1)
#define VIG_CSC_10_EN          BIT(0)
#define CSC_10BIT_OFFSET       4

/* traffic shaper clock in Hz */
#define TS_CLK			19200000


static int _sspp_subblk_offset(struct dpu_hw_pipe *ctx,
		int s_id,
		u32 *idx)
{
	int rc = 0;
	const struct dpu_sspp_sub_blks *sblk = ctx->cap->sblk;

	if (!ctx)
		return -EINVAL;

	switch (s_id) {
	case DPU_SSPP_SRC:
		*idx = sblk->src_blk.base;
		break;
	case DPU_SSPP_SCALER_QSEED2:
	case DPU_SSPP_SCALER_QSEED3:
	case DPU_SSPP_SCALER_RGB:
		*idx = sblk->scaler_blk.base;
		break;
	case DPU_SSPP_CSC:
	case DPU_SSPP_CSC_10BIT:
		*idx = sblk->csc_blk.base;
		break;
	default:
		rc = -EINVAL;
	}

	return rc;
}

static void dpu_hw_sspp_setup_multirect(struct dpu_hw_pipe *ctx,
		enum dpu_sspp_multirect_index index,
		enum dpu_sspp_multirect_mode mode)
{
	u32 mode_mask;
	u32 idx;

	if (_sspp_subblk_offset(ctx, DPU_SSPP_SRC, &idx))
		return;

	if (index == DPU_SSPP_RECT_SOLO) {
		/**
		 * if rect index is RECT_SOLO, we cannot expect a
		 * virtual plane sharing the same SSPP id. So we go
		 * and disable multirect
		 */
		mode_mask = 0;
	} else {
		mode_mask = DPU_REG_READ(&ctx->hw, SSPP_MULTIRECT_OPMODE + idx);
		mode_mask |= index;
		if (mode == DPU_SSPP_MULTIRECT_TIME_MX)
			mode_mask |= BIT(2);
		else
			mode_mask &= ~BIT(2);
	}

	DPU_REG_WRITE(&ctx->hw, SSPP_MULTIRECT_OPMODE + idx, mode_mask);
}

static void _sspp_setup_opmode(struct dpu_hw_pipe *ctx,
		u32 mask, u8 en)
{
	u32 idx;
	u32 opmode;

	if (!test_bit(DPU_SSPP_SCALER_QSEED2, &ctx->cap->features) ||
		_sspp_subblk_offset(ctx, DPU_SSPP_SCALER_QSEED2, &idx) ||
		!test_bit(DPU_SSPP_CSC, &ctx->cap->features))
		return;

	opmode = DPU_REG_READ(&ctx->hw, SSPP_VIG_OP_MODE + idx);

	if (en)
		opmode |= mask;
	else
		opmode &= ~mask;

	DPU_REG_WRITE(&ctx->hw, SSPP_VIG_OP_MODE + idx, opmode);
}

static void _sspp_setup_csc10_opmode(struct dpu_hw_pipe *ctx,
		u32 mask, u8 en)
{
	u32 idx;
	u32 opmode;

	if (_sspp_subblk_offset(ctx, DPU_SSPP_CSC_10BIT, &idx))
		return;

	opmode = DPU_REG_READ(&ctx->hw, SSPP_VIG_CSC_10_OP_MODE + idx);
	if (en)
		opmode |= mask;
	else
		opmode &= ~mask;

	DPU_REG_WRITE(&ctx->hw, SSPP_VIG_CSC_10_OP_MODE + idx, opmode);
}

/**
 * Setup source pixel format, flip,
 */
static void dpu_hw_sspp_setup_format(struct dpu_hw_pipe *ctx,
		const struct dpu_format *fmt, u32 flags,
		enum dpu_sspp_multirect_index rect_mode)
{
	struct dpu_hw_blk_reg_map *c;
	u32 chroma_samp, unpack, src_format;
	u32 opmode = 0;
	u32 fast_clear = 0;
	u32 op_mode_off, unpack_pat_off, format_off;
	u32 idx;

	if (_sspp_subblk_offset(ctx, DPU_SSPP_SRC, &idx) || !fmt)
		return;

	if (rect_mode == DPU_SSPP_RECT_SOLO || rect_mode == DPU_SSPP_RECT_0) {
		op_mode_off = SSPP_SRC_OP_MODE;
		unpack_pat_off = SSPP_SRC_UNPACK_PATTERN;
		format_off = SSPP_SRC_FORMAT;
	} else {
		op_mode_off = SSPP_SRC_OP_MODE_REC1;
		unpack_pat_off = SSPP_SRC_UNPACK_PATTERN_REC1;
		format_off = SSPP_SRC_FORMAT_REC1;
	}

	c = &ctx->hw;
	opmode = DPU_REG_READ(c, op_mode_off + idx);
	opmode &= ~(MDSS_MDP_OP_FLIP_LR | MDSS_MDP_OP_FLIP_UD |
			MDSS_MDP_OP_BWC_EN | MDSS_MDP_OP_PE_OVERRIDE);

	if (flags & DPU_SSPP_FLIP_LR)
		opmode |= MDSS_MDP_OP_FLIP_LR;
	if (flags & DPU_SSPP_FLIP_UD)
		opmode |= MDSS_MDP_OP_FLIP_UD;

	chroma_samp = fmt->chroma_sample;
	if (flags & DPU_SSPP_SOURCE_ROTATED_90) {
		if (chroma_samp == DPU_CHROMA_H2V1)
			chroma_samp = DPU_CHROMA_H1V2;
		else if (chroma_samp == DPU_CHROMA_H1V2)
			chroma_samp = DPU_CHROMA_H2V1;
	}

	src_format = (chroma_samp << 23) | (fmt->fetch_planes << 19) |
		(fmt->bits[C3_ALPHA] << 6) | (fmt->bits[C2_R_Cr] << 4) |
		(fmt->bits[C1_B_Cb] << 2) | (fmt->bits[C0_G_Y] << 0);

	if (flags & DPU_SSPP_ROT_90)
		src_format |= BIT(11); /* ROT90 */

	if (fmt->alpha_enable && fmt->fetch_planes == DPU_PLANE_INTERLEAVED)
		src_format |= BIT(8); /* SRCC3_EN */

	if (flags & DPU_SSPP_SOLID_FILL)
		src_format |= BIT(22);

	unpack = (fmt->element[3] << 24) | (fmt->element[2] << 16) |
		(fmt->element[1] << 8) | (fmt->element[0] << 0);
	src_format |= ((fmt->unpack_count - 1) << 12) |
		(fmt->unpack_tight << 17) |
		(fmt->unpack_align_msb << 18) |
		((fmt->bpp - 1) << 9);

	if (fmt->fetch_mode != DPU_FETCH_LINEAR) {
		if (DPU_FORMAT_IS_UBWC(fmt))
			opmode |= MDSS_MDP_OP_BWC_EN;
		src_format |= (fmt->fetch_mode & 3) << 30; /*FRAME_FORMAT */
		DPU_REG_WRITE(c, SSPP_FETCH_CONFIG,
			DPU_FETCH_CONFIG_RESET_VALUE |
			ctx->mdp->highest_bank_bit << 18);
		switch (ctx->catalog->caps->ubwc_version) {
		case DPU_HW_UBWC_VER_10:
			/* TODO: UBWC v1 case */
			break;
		case DPU_HW_UBWC_VER_20:
			fast_clear = fmt->alpha_enable ? BIT(31) : 0;
			DPU_REG_WRITE(c, SSPP_UBWC_STATIC_CTRL,
					fast_clear | (ctx->mdp->ubwc_swizzle) |
					(ctx->mdp->highest_bank_bit << 4));
			break;
		case DPU_HW_UBWC_VER_30:
			DPU_REG_WRITE(c, SSPP_UBWC_STATIC_CTRL,
					BIT(30) | (ctx->mdp->ubwc_swizzle) |
					(ctx->mdp->highest_bank_bit << 4));
			break;
		case DPU_HW_UBWC_VER_40:
			DPU_REG_WRITE(c, SSPP_UBWC_STATIC_CTRL,
					DPU_FORMAT_IS_YUV(fmt) ? 0 : BIT(30));
			break;
		}
	}

	opmode |= MDSS_MDP_OP_PE_OVERRIDE;

	/* if this is YUV pixel format, enable CSC */
	if (DPU_FORMAT_IS_YUV(fmt))
		src_format |= BIT(15);

	if (DPU_FORMAT_IS_DX(fmt))
		src_format |= BIT(14);

	/* update scaler opmode, if appropriate */
	if (test_bit(DPU_SSPP_CSC, &ctx->cap->features))
		_sspp_setup_opmode(ctx, VIG_OP_CSC_EN | VIG_OP_CSC_SRC_DATAFMT,
			DPU_FORMAT_IS_YUV(fmt));
	else if (test_bit(DPU_SSPP_CSC_10BIT, &ctx->cap->features))
		_sspp_setup_csc10_opmode(ctx,
			VIG_CSC_10_EN | VIG_CSC_10_SRC_DATAFMT,
			DPU_FORMAT_IS_YUV(fmt));

	DPU_REG_WRITE(c, format_off + idx, src_format);
	DPU_REG_WRITE(c, unpack_pat_off + idx, unpack);
	DPU_REG_WRITE(c, op_mode_off + idx, opmode);

	/* clear previous UBWC error */
	DPU_REG_WRITE(c, SSPP_UBWC_ERROR_STATUS + idx, BIT(31));
}

static void dpu_hw_sspp_setup_pe_config(struct dpu_hw_pipe *ctx,
		struct dpu_hw_pixel_ext *pe_ext)
{
	struct dpu_hw_blk_reg_map *c;
	u8 color;
	u32 lr_pe[4], tb_pe[4], tot_req_pixels[4];
	const u32 bytemask = 0xff;
	const u32 shortmask = 0xffff;
	u32 idx;

	if (_sspp_subblk_offset(ctx, DPU_SSPP_SRC, &idx) || !pe_ext)
		return;

	c = &ctx->hw;

	/* program SW pixel extension override for all pipes*/
	for (color = 0; color < DPU_MAX_PLANES; color++) {
		/* color 2 has the same set of registers as color 1 */
		if (color == 2)
			continue;

		lr_pe[color] = ((pe_ext->right_ftch[color] & bytemask) << 24)|
			((pe_ext->right_rpt[color] & bytemask) << 16)|
			((pe_ext->left_ftch[color] & bytemask) << 8)|
			(pe_ext->left_rpt[color] & bytemask);

		tb_pe[color] = ((pe_ext->btm_ftch[color] & bytemask) << 24)|
			((pe_ext->btm_rpt[color] & bytemask) << 16)|
			((pe_ext->top_ftch[color] & bytemask) << 8)|
			(pe_ext->top_rpt[color] & bytemask);

		tot_req_pixels[color] = (((pe_ext->roi_h[color] +
			pe_ext->num_ext_pxls_top[color] +
			pe_ext->num_ext_pxls_btm[color]) & shortmask) << 16) |
			((pe_ext->roi_w[color] +
			pe_ext->num_ext_pxls_left[color] +
			pe_ext->num_ext_pxls_right[color]) & shortmask);
	}

	/* color 0 */
	DPU_REG_WRITE(c, SSPP_SW_PIX_EXT_C0_LR + idx, lr_pe[0]);
	DPU_REG_WRITE(c, SSPP_SW_PIX_EXT_C0_TB + idx, tb_pe[0]);
	DPU_REG_WRITE(c, SSPP_SW_PIX_EXT_C0_REQ_PIXELS + idx,
			tot_req_pixels[0]);

	/* color 1 and color 2 */
	DPU_REG_WRITE(c, SSPP_SW_PIX_EXT_C1C2_LR + idx, lr_pe[1]);
	DPU_REG_WRITE(c, SSPP_SW_PIX_EXT_C1C2_TB + idx, tb_pe[1]);
	DPU_REG_WRITE(c, SSPP_SW_PIX_EXT_C1C2_REQ_PIXELS + idx,
			tot_req_pixels[1]);

	/* color 3 */
	DPU_REG_WRITE(c, SSPP_SW_PIX_EXT_C3_LR + idx, lr_pe[3]);
	DPU_REG_WRITE(c, SSPP_SW_PIX_EXT_C3_TB + idx, lr_pe[3]);
	DPU_REG_WRITE(c, SSPP_SW_PIX_EXT_C3_REQ_PIXELS + idx,
			tot_req_pixels[3]);
}

static void _dpu_hw_sspp_setup_scaler3(struct dpu_hw_pipe *ctx,
		struct dpu_hw_pipe_cfg *sspp,
		struct dpu_hw_pixel_ext *pe,
		void *scaler_cfg)
{
	u32 idx;
	struct dpu_hw_scaler3_cfg *scaler3_cfg = scaler_cfg;

	(void)pe;
	if (_sspp_subblk_offset(ctx, DPU_SSPP_SCALER_QSEED3, &idx) || !sspp
		|| !scaler3_cfg || !ctx || !ctx->cap || !ctx->cap->sblk)
		return;

	dpu_hw_setup_scaler3(&ctx->hw, scaler3_cfg, idx,
			ctx->cap->sblk->scaler_blk.version,
			sspp->layout.format);
}

static u32 _dpu_hw_sspp_get_scaler3_ver(struct dpu_hw_pipe *ctx)
{
	u32 idx;

	if (!ctx || _sspp_subblk_offset(ctx, DPU_SSPP_SCALER_QSEED3, &idx))
		return 0;

	return dpu_hw_get_scaler3_ver(&ctx->hw, idx);
}

/**
 * dpu_hw_sspp_setup_rects()
 */
static void dpu_hw_sspp_setup_rects(struct dpu_hw_pipe *ctx,
		struct dpu_hw_pipe_cfg *cfg,
		enum dpu_sspp_multirect_index rect_index)
{
	struct dpu_hw_blk_reg_map *c;
	u32 src_size, src_xy, dst_size, dst_xy, ystride0, ystride1;
	u32 src_size_off, src_xy_off, out_size_off, out_xy_off;
	u32 idx;

	if (_sspp_subblk_offset(ctx, DPU_SSPP_SRC, &idx) || !cfg)
		return;

	c = &ctx->hw;

	if (rect_index == DPU_SSPP_RECT_SOLO || rect_index == DPU_SSPP_RECT_0) {
		src_size_off = SSPP_SRC_SIZE;
		src_xy_off = SSPP_SRC_XY;
		out_size_off = SSPP_OUT_SIZE;
		out_xy_off = SSPP_OUT_XY;
	} else {
		src_size_off = SSPP_SRC_SIZE_REC1;
		src_xy_off = SSPP_SRC_XY_REC1;
		out_size_off = SSPP_OUT_SIZE_REC1;
		out_xy_off = SSPP_OUT_XY_REC1;
	}


	/* src and dest rect programming */
	src_xy = (cfg->src_rect.y1 << 16) | cfg->src_rect.x1;
	src_size = (drm_rect_height(&cfg->src_rect) << 16) |
		   drm_rect_width(&cfg->src_rect);
	dst_xy = (cfg->dst_rect.y1 << 16) | cfg->dst_rect.x1;
	dst_size = (drm_rect_height(&cfg->dst_rect) << 16) |
		drm_rect_width(&cfg->dst_rect);

	if (rect_index == DPU_SSPP_RECT_SOLO) {
		ystride0 = (cfg->layout.plane_pitch[0]) |
			(cfg->layout.plane_pitch[1] << 16);
		ystride1 = (cfg->layout.plane_pitch[2]) |
			(cfg->layout.plane_pitch[3] << 16);
	} else {
		ystride0 = DPU_REG_READ(c, SSPP_SRC_YSTRIDE0 + idx);
		ystride1 = DPU_REG_READ(c, SSPP_SRC_YSTRIDE1 + idx);

		if (rect_index == DPU_SSPP_RECT_0) {
			ystride0 = (ystride0 & 0xFFFF0000) |
				(cfg->layout.plane_pitch[0] & 0x0000FFFF);
			ystride1 = (ystride1 & 0xFFFF0000)|
				(cfg->layout.plane_pitch[2] & 0x0000FFFF);
		} else {
			ystride0 = (ystride0 & 0x0000FFFF) |
				((cfg->layout.plane_pitch[0] << 16) &
				 0xFFFF0000);
			ystride1 = (ystride1 & 0x0000FFFF) |
				((cfg->layout.plane_pitch[2] << 16) &
				 0xFFFF0000);
		}
	}

	/* rectangle register programming */
	DPU_REG_WRITE(c, src_size_off + idx, src_size);
	DPU_REG_WRITE(c, src_xy_off + idx, src_xy);
	DPU_REG_WRITE(c, out_size_off + idx, dst_size);
	DPU_REG_WRITE(c, out_xy_off + idx, dst_xy);

	DPU_REG_WRITE(c, SSPP_SRC_YSTRIDE0 + idx, ystride0);
	DPU_REG_WRITE(c, SSPP_SRC_YSTRIDE1 + idx, ystride1);
}

static void dpu_hw_sspp_setup_sourceaddress(struct dpu_hw_pipe *ctx,
		struct dpu_hw_pipe_cfg *cfg,
		enum dpu_sspp_multirect_index rect_mode)
{
	int i;
	u32 idx;

	if (_sspp_subblk_offset(ctx, DPU_SSPP_SRC, &idx))
		return;

	if (rect_mode == DPU_SSPP_RECT_SOLO) {
		for (i = 0; i < ARRAY_SIZE(cfg->layout.plane_addr); i++)
			DPU_REG_WRITE(&ctx->hw, SSPP_SRC0_ADDR + idx + i * 0x4,
					cfg->layout.plane_addr[i]);
	} else if (rect_mode == DPU_SSPP_RECT_0) {
		DPU_REG_WRITE(&ctx->hw, SSPP_SRC0_ADDR + idx,
				cfg->layout.plane_addr[0]);
		DPU_REG_WRITE(&ctx->hw, SSPP_SRC2_ADDR + idx,
				cfg->layout.plane_addr[2]);
	} else {
		DPU_REG_WRITE(&ctx->hw, SSPP_SRC1_ADDR + idx,
				cfg->layout.plane_addr[0]);
		DPU_REG_WRITE(&ctx->hw, SSPP_SRC3_ADDR + idx,
				cfg->layout.plane_addr[2]);
	}
}

static void dpu_hw_sspp_setup_csc(struct dpu_hw_pipe *ctx,
		struct dpu_csc_cfg *data)
{
	u32 idx;
	bool csc10 = false;

	if (_sspp_subblk_offset(ctx, DPU_SSPP_CSC, &idx) || !data)
		return;

	if (test_bit(DPU_SSPP_CSC_10BIT, &ctx->cap->features)) {
		idx += CSC_10BIT_OFFSET;
		csc10 = true;
	}

	dpu_hw_csc_setup(&ctx->hw, idx, data, csc10);
}

static void dpu_hw_sspp_setup_solidfill(struct dpu_hw_pipe *ctx, u32 color, enum
		dpu_sspp_multirect_index rect_index)
{
	u32 idx;

	if (_sspp_subblk_offset(ctx, DPU_SSPP_SRC, &idx))
		return;

	if (rect_index == DPU_SSPP_RECT_SOLO || rect_index == DPU_SSPP_RECT_0)
		DPU_REG_WRITE(&ctx->hw, SSPP_SRC_CONSTANT_COLOR + idx, color);
	else
		DPU_REG_WRITE(&ctx->hw, SSPP_SRC_CONSTANT_COLOR_REC1 + idx,
				color);
}

static void dpu_hw_sspp_setup_danger_safe_lut(struct dpu_hw_pipe *ctx,
		struct dpu_hw_pipe_qos_cfg *cfg)
{
	u32 idx;

	if (_sspp_subblk_offset(ctx, DPU_SSPP_SRC, &idx))
		return;

	DPU_REG_WRITE(&ctx->hw, SSPP_DANGER_LUT + idx, cfg->danger_lut);
	DPU_REG_WRITE(&ctx->hw, SSPP_SAFE_LUT + idx, cfg->safe_lut);
}

static void dpu_hw_sspp_setup_creq_lut(struct dpu_hw_pipe *ctx,
		struct dpu_hw_pipe_qos_cfg *cfg)
{
	u32 idx;

	if (_sspp_subblk_offset(ctx, DPU_SSPP_SRC, &idx))
		return;

	if (ctx->cap && test_bit(DPU_SSPP_QOS_8LVL, &ctx->cap->features)) {
		DPU_REG_WRITE(&ctx->hw, SSPP_CREQ_LUT_0 + idx, cfg->creq_lut);
		DPU_REG_WRITE(&ctx->hw, SSPP_CREQ_LUT_1 + idx,
				cfg->creq_lut >> 32);
	} else {
		DPU_REG_WRITE(&ctx->hw, SSPP_CREQ_LUT + idx, cfg->creq_lut);
	}
}

static void dpu_hw_sspp_setup_qos_ctrl(struct dpu_hw_pipe *ctx,
		struct dpu_hw_pipe_qos_cfg *cfg)
{
	u32 idx;
	u32 qos_ctrl = 0;

	if (_sspp_subblk_offset(ctx, DPU_SSPP_SRC, &idx))
		return;

	if (cfg->vblank_en) {
		qos_ctrl |= ((cfg->creq_vblank &
				SSPP_QOS_CTRL_CREQ_VBLANK_MASK) <<
				SSPP_QOS_CTRL_CREQ_VBLANK_OFF);
		qos_ctrl |= ((cfg->danger_vblank &
				SSPP_QOS_CTRL_DANGER_VBLANK_MASK) <<
				SSPP_QOS_CTRL_DANGER_VBLANK_OFF);
		qos_ctrl |= SSPP_QOS_CTRL_VBLANK_EN;
	}

	if (cfg->danger_safe_en)
		qos_ctrl |= SSPP_QOS_CTRL_DANGER_SAFE_EN;

	DPU_REG_WRITE(&ctx->hw, SSPP_QOS_CTRL + idx, qos_ctrl);
}

static void dpu_hw_sspp_setup_cdp(struct dpu_hw_pipe *ctx,
		struct dpu_hw_pipe_cdp_cfg *cfg)
{
	u32 idx;
	u32 cdp_cntl = 0;

	if (!ctx || !cfg)
		return;

	if (_sspp_subblk_offset(ctx, DPU_SSPP_SRC, &idx))
		return;

	if (cfg->enable)
		cdp_cntl |= BIT(0);
	if (cfg->ubwc_meta_enable)
		cdp_cntl |= BIT(1);
	if (cfg->tile_amortize_enable)
		cdp_cntl |= BIT(2);
	if (cfg->preload_ahead == DPU_SSPP_CDP_PRELOAD_AHEAD_64)
		cdp_cntl |= BIT(3);

	DPU_REG_WRITE(&ctx->hw, SSPP_CDP_CNTL, cdp_cntl);
}

static void _setup_layer_ops(struct dpu_hw_pipe *c,
		unsigned long features)
{
	if (test_bit(DPU_SSPP_SRC, &features)) {
		c->ops.setup_format = dpu_hw_sspp_setup_format;
		c->ops.setup_rects = dpu_hw_sspp_setup_rects;
		c->ops.setup_sourceaddress = dpu_hw_sspp_setup_sourceaddress;
		c->ops.setup_solidfill = dpu_hw_sspp_setup_solidfill;
		c->ops.setup_pe = dpu_hw_sspp_setup_pe_config;
	}

	if (test_bit(DPU_SSPP_QOS, &features)) {
		c->ops.setup_danger_safe_lut =
			dpu_hw_sspp_setup_danger_safe_lut;
		c->ops.setup_creq_lut = dpu_hw_sspp_setup_creq_lut;
		c->ops.setup_qos_ctrl = dpu_hw_sspp_setup_qos_ctrl;
	}

	if (test_bit(DPU_SSPP_CSC, &features) ||
		test_bit(DPU_SSPP_CSC_10BIT, &features))
		c->ops.setup_csc = dpu_hw_sspp_setup_csc;

	if (test_bit(DPU_SSPP_SMART_DMA_V1, &c->cap->features) ||
		test_bit(DPU_SSPP_SMART_DMA_V2, &c->cap->features))
		c->ops.setup_multirect = dpu_hw_sspp_setup_multirect;

	if (test_bit(DPU_SSPP_SCALER_QSEED3, &features) ||
			test_bit(DPU_SSPP_SCALER_QSEED4, &features)) {
		c->ops.setup_scaler = _dpu_hw_sspp_setup_scaler3;
		c->ops.get_scaler_ver = _dpu_hw_sspp_get_scaler3_ver;
	}

	if (test_bit(DPU_SSPP_CDP, &features))
		c->ops.setup_cdp = dpu_hw_sspp_setup_cdp;
}

static const struct dpu_sspp_cfg *_sspp_offset(enum dpu_sspp sspp,
		void __iomem *addr,
		struct dpu_mdss_cfg *catalog,
		struct dpu_hw_blk_reg_map *b)
{
	int i;

	if ((sspp < SSPP_MAX) && catalog && addr && b) {
		for (i = 0; i < catalog->sspp_count; i++) {
			if (sspp == catalog->sspp[i].id) {
				b->base_off = addr;
				b->blk_off = catalog->sspp[i].base;
				b->length = catalog->sspp[i].len;
				b->hwversion = catalog->hwversion;
				b->log_mask = DPU_DBG_MASK_SSPP;
				return &catalog->sspp[i];
			}
		}
	}

	return ERR_PTR(-ENOMEM);
}

static struct dpu_hw_blk_ops dpu_hw_ops;

struct dpu_hw_pipe *dpu_hw_sspp_init(enum dpu_sspp idx,
		void __iomem *addr, struct dpu_mdss_cfg *catalog,
		bool is_virtual_pipe)
{
	struct dpu_hw_pipe *hw_pipe;
	const struct dpu_sspp_cfg *cfg;

	if (!addr || !catalog)
		return ERR_PTR(-EINVAL);

	hw_pipe = kzalloc(sizeof(*hw_pipe), GFP_KERNEL);
	if (!hw_pipe)
		return ERR_PTR(-ENOMEM);

	cfg = _sspp_offset(idx, addr, catalog, &hw_pipe->hw);
	if (IS_ERR_OR_NULL(cfg)) {
		kfree(hw_pipe);
		return ERR_PTR(-EINVAL);
	}

	/* Assign ops */
	hw_pipe->catalog = catalog;
	hw_pipe->mdp = &catalog->mdp[0];
	hw_pipe->idx = idx;
	hw_pipe->cap = cfg;
	_setup_layer_ops(hw_pipe, hw_pipe->cap->features);

	dpu_hw_blk_init(&hw_pipe->base, DPU_HW_BLK_SSPP, idx, &dpu_hw_ops);

	return hw_pipe;
}

void dpu_hw_sspp_destroy(struct dpu_hw_pipe *ctx)
{
	if (ctx)
		dpu_hw_blk_destroy(&ctx->base);
	kfree(ctx);
}

