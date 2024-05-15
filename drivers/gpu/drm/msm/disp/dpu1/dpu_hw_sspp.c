// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#include <linux/debugfs.h>

#include "dpu_hwio.h"
#include "dpu_hw_catalog.h"
#include "dpu_hw_lm.h"
#include "dpu_hw_sspp.h"
#include "dpu_kms.h"

#include "msm_mdss.h"

#include <drm/drm_file.h>
#include <drm/drm_managed.h>

#define DPU_FETCH_CONFIG_RESET_VALUE   0x00000087

/* SSPP registers */
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
#define SSPP_SRC_CONSTANT_COLOR            0x3c
#define SSPP_EXCL_REC_CTL                  0x40
#define SSPP_UBWC_STATIC_CTRL              0x44
#define SSPP_FETCH_CONFIG                  0x48
#define SSPP_DANGER_LUT                    0x60
#define SSPP_SAFE_LUT                      0x64
#define SSPP_CREQ_LUT                      0x68
#define SSPP_QOS_CTRL                      0x6C
#define SSPP_SRC_ADDR_SW_STATUS            0x70
#define SSPP_CREQ_LUT_0                    0x74
#define SSPP_CREQ_LUT_1                    0x78
#define SSPP_DECIMATION_CONFIG             0xB4
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
#define SSPP_CDP_CNTL_REC1                 0x13c
#define SSPP_TRAFFIC_SHAPER_PREFILL        0x150
#define SSPP_TRAFFIC_SHAPER_REC1_PREFILL   0x154
#define SSPP_TRAFFIC_SHAPER_REC1           0x158
#define SSPP_OUT_SIZE_REC1                 0x160
#define SSPP_OUT_XY_REC1                   0x164
#define SSPP_SRC_XY_REC1                   0x168
#define SSPP_SRC_SIZE_REC1                 0x16C
#define SSPP_MULTIRECT_OPMODE              0x170
#define SSPP_SRC_FORMAT_REC1               0x174
#define SSPP_SRC_UNPACK_PATTERN_REC1       0x178
#define SSPP_SRC_OP_MODE_REC1              0x17C
#define SSPP_SRC_CONSTANT_COLOR_REC1       0x180
#define SSPP_EXCL_REC_SIZE_REC1            0x184
#define SSPP_EXCL_REC_XY_REC1              0x188
#define SSPP_EXCL_REC_SIZE                 0x1B4
#define SSPP_EXCL_REC_XY                   0x1B8
#define SSPP_CLK_CTRL                      0x330

/* SSPP_SRC_OP_MODE & OP_MODE_REC1 */
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

/* SSPP_QOS_CTRL */
#define SSPP_QOS_CTRL_VBLANK_EN            BIT(16)
#define SSPP_QOS_CTRL_DANGER_SAFE_EN       BIT(0)
#define SSPP_QOS_CTRL_DANGER_VBLANK_MASK   0x3
#define SSPP_QOS_CTRL_DANGER_VBLANK_OFF    4
#define SSPP_QOS_CTRL_CREQ_VBLANK_MASK     0x3
#define SSPP_QOS_CTRL_CREQ_VBLANK_OFF      20

/* DPU_SSPP_SCALER_QSEED2 */
#define SSPP_VIG_OP_MODE                   0x0
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

/* SSPP_TRAFFIC_SHAPER and _REC1 */
#define SSPP_TRAFFIC_SHAPER_BPC_MAX        0xFF

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
#define SSPP_VIG_CSC_10_OP_MODE            0x0
#define VIG_CSC_10_SRC_DATAFMT BIT(1)
#define VIG_CSC_10_EN          BIT(0)
#define CSC_10BIT_OFFSET       4

/* traffic shaper clock in Hz */
#define TS_CLK			19200000


static void dpu_hw_sspp_setup_multirect(struct dpu_sw_pipe *pipe)
{
	struct dpu_hw_sspp *ctx = pipe->sspp;
	u32 mode_mask;

	if (!ctx)
		return;

	if (pipe->multirect_index == DPU_SSPP_RECT_SOLO) {
		/**
		 * if rect index is RECT_SOLO, we cannot expect a
		 * virtual plane sharing the same SSPP id. So we go
		 * and disable multirect
		 */
		mode_mask = 0;
	} else {
		mode_mask = DPU_REG_READ(&ctx->hw, SSPP_MULTIRECT_OPMODE);
		mode_mask |= pipe->multirect_index;
		if (pipe->multirect_mode == DPU_SSPP_MULTIRECT_TIME_MX)
			mode_mask |= BIT(2);
		else
			mode_mask &= ~BIT(2);
	}

	DPU_REG_WRITE(&ctx->hw, SSPP_MULTIRECT_OPMODE, mode_mask);
}

static void _sspp_setup_opmode(struct dpu_hw_sspp *ctx,
		u32 mask, u8 en)
{
	const struct dpu_sspp_sub_blks *sblk = ctx->cap->sblk;
	u32 opmode;

	if (!test_bit(DPU_SSPP_SCALER_QSEED2, &ctx->cap->features) ||
		!test_bit(DPU_SSPP_CSC, &ctx->cap->features))
		return;

	opmode = DPU_REG_READ(&ctx->hw, sblk->scaler_blk.base + SSPP_VIG_OP_MODE);

	if (en)
		opmode |= mask;
	else
		opmode &= ~mask;

	DPU_REG_WRITE(&ctx->hw, sblk->scaler_blk.base + SSPP_VIG_OP_MODE, opmode);
}

static void _sspp_setup_csc10_opmode(struct dpu_hw_sspp *ctx,
		u32 mask, u8 en)
{
	const struct dpu_sspp_sub_blks *sblk = ctx->cap->sblk;
	u32 opmode;

	opmode = DPU_REG_READ(&ctx->hw, sblk->csc_blk.base + SSPP_VIG_CSC_10_OP_MODE);
	if (en)
		opmode |= mask;
	else
		opmode &= ~mask;

	DPU_REG_WRITE(&ctx->hw, sblk->csc_blk.base + SSPP_VIG_CSC_10_OP_MODE, opmode);
}

/*
 * Setup source pixel format, flip,
 */
static void dpu_hw_sspp_setup_format(struct dpu_sw_pipe *pipe,
		const struct msm_format *fmt, u32 flags)
{
	struct dpu_hw_sspp *ctx = pipe->sspp;
	struct dpu_hw_blk_reg_map *c;
	u32 chroma_samp, unpack, src_format;
	u32 opmode = 0;
	u32 fast_clear = 0;
	u32 op_mode_off, unpack_pat_off, format_off;

	if (!ctx || !fmt)
		return;

	if (pipe->multirect_index == DPU_SSPP_RECT_SOLO ||
	    pipe->multirect_index == DPU_SSPP_RECT_0) {
		op_mode_off = SSPP_SRC_OP_MODE;
		unpack_pat_off = SSPP_SRC_UNPACK_PATTERN;
		format_off = SSPP_SRC_FORMAT;
	} else {
		op_mode_off = SSPP_SRC_OP_MODE_REC1;
		unpack_pat_off = SSPP_SRC_UNPACK_PATTERN_REC1;
		format_off = SSPP_SRC_FORMAT_REC1;
	}

	c = &ctx->hw;
	opmode = DPU_REG_READ(c, op_mode_off);
	opmode &= ~(MDSS_MDP_OP_FLIP_LR | MDSS_MDP_OP_FLIP_UD |
			MDSS_MDP_OP_BWC_EN | MDSS_MDP_OP_PE_OVERRIDE);

	if (flags & DPU_SSPP_FLIP_LR)
		opmode |= MDSS_MDP_OP_FLIP_LR;
	if (flags & DPU_SSPP_FLIP_UD)
		opmode |= MDSS_MDP_OP_FLIP_UD;

	chroma_samp = fmt->chroma_sample;
	if (flags & DPU_SSPP_SOURCE_ROTATED_90) {
		if (chroma_samp == CHROMA_H2V1)
			chroma_samp = CHROMA_H1V2;
		else if (chroma_samp == CHROMA_H1V2)
			chroma_samp = CHROMA_H2V1;
	}

	src_format = (chroma_samp << 23) | (fmt->fetch_type << 19) |
		(fmt->bpc_a << 6) | (fmt->bpc_r_cr << 4) |
		(fmt->bpc_b_cb << 2) | (fmt->bpc_g_y << 0);

	if (flags & DPU_SSPP_ROT_90)
		src_format |= BIT(11); /* ROT90 */

	if (fmt->alpha_enable && fmt->fetch_type == MDP_PLANE_INTERLEAVED)
		src_format |= BIT(8); /* SRCC3_EN */

	if (flags & DPU_SSPP_SOLID_FILL)
		src_format |= BIT(22);

	unpack = (fmt->element[3] << 24) | (fmt->element[2] << 16) |
		(fmt->element[1] << 8) | (fmt->element[0] << 0);
	src_format |= ((fmt->unpack_count - 1) << 12) |
		((fmt->flags & MSM_FORMAT_FLAG_UNPACK_TIGHT ? 1 : 0) << 17) |
		((fmt->flags & MSM_FORMAT_FLAG_UNPACK_ALIGN_MSB ? 1 : 0) << 18) |
		((fmt->bpp - 1) << 9);

	if (fmt->fetch_mode != MDP_FETCH_LINEAR) {
		if (MSM_FORMAT_IS_UBWC(fmt))
			opmode |= MDSS_MDP_OP_BWC_EN;
		src_format |= (fmt->fetch_mode & 3) << 30; /*FRAME_FORMAT */
		DPU_REG_WRITE(c, SSPP_FETCH_CONFIG,
			DPU_FETCH_CONFIG_RESET_VALUE |
			ctx->ubwc->highest_bank_bit << 18);
		switch (ctx->ubwc->ubwc_enc_version) {
		case UBWC_1_0:
			fast_clear = fmt->alpha_enable ? BIT(31) : 0;
			DPU_REG_WRITE(c, SSPP_UBWC_STATIC_CTRL,
					fast_clear | (ctx->ubwc->ubwc_swizzle & 0x1) |
					BIT(8) |
					(ctx->ubwc->highest_bank_bit << 4));
			break;
		case UBWC_2_0:
			fast_clear = fmt->alpha_enable ? BIT(31) : 0;
			DPU_REG_WRITE(c, SSPP_UBWC_STATIC_CTRL,
					fast_clear | (ctx->ubwc->ubwc_swizzle) |
					(ctx->ubwc->highest_bank_bit << 4));
			break;
		case UBWC_3_0:
			DPU_REG_WRITE(c, SSPP_UBWC_STATIC_CTRL,
					BIT(30) | (ctx->ubwc->ubwc_swizzle) |
					(ctx->ubwc->highest_bank_bit << 4));
			break;
		case UBWC_4_0:
			DPU_REG_WRITE(c, SSPP_UBWC_STATIC_CTRL,
					MSM_FORMAT_IS_YUV(fmt) ? 0 : BIT(30));
			break;
		}
	}

	opmode |= MDSS_MDP_OP_PE_OVERRIDE;

	/* if this is YUV pixel format, enable CSC */
	if (MSM_FORMAT_IS_YUV(fmt))
		src_format |= BIT(15);

	if (MSM_FORMAT_IS_DX(fmt))
		src_format |= BIT(14);

	/* update scaler opmode, if appropriate */
	if (test_bit(DPU_SSPP_CSC, &ctx->cap->features))
		_sspp_setup_opmode(ctx, VIG_OP_CSC_EN | VIG_OP_CSC_SRC_DATAFMT,
			MSM_FORMAT_IS_YUV(fmt));
	else if (test_bit(DPU_SSPP_CSC_10BIT, &ctx->cap->features))
		_sspp_setup_csc10_opmode(ctx,
			VIG_CSC_10_EN | VIG_CSC_10_SRC_DATAFMT,
			MSM_FORMAT_IS_YUV(fmt));

	DPU_REG_WRITE(c, format_off, src_format);
	DPU_REG_WRITE(c, unpack_pat_off, unpack);
	DPU_REG_WRITE(c, op_mode_off, opmode);

	/* clear previous UBWC error */
	DPU_REG_WRITE(c, SSPP_UBWC_ERROR_STATUS, BIT(31));
}

static void dpu_hw_sspp_setup_pe_config(struct dpu_hw_sspp *ctx,
		struct dpu_hw_pixel_ext *pe_ext)
{
	struct dpu_hw_blk_reg_map *c;
	u8 color;
	u32 lr_pe[4], tb_pe[4], tot_req_pixels[4];
	const u32 bytemask = 0xff;
	const u32 shortmask = 0xffff;

	if (!ctx || !pe_ext)
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
	DPU_REG_WRITE(c, SSPP_SW_PIX_EXT_C0_LR, lr_pe[0]);
	DPU_REG_WRITE(c, SSPP_SW_PIX_EXT_C0_TB, tb_pe[0]);
	DPU_REG_WRITE(c, SSPP_SW_PIX_EXT_C0_REQ_PIXELS,
			tot_req_pixels[0]);

	/* color 1 and color 2 */
	DPU_REG_WRITE(c, SSPP_SW_PIX_EXT_C1C2_LR, lr_pe[1]);
	DPU_REG_WRITE(c, SSPP_SW_PIX_EXT_C1C2_TB, tb_pe[1]);
	DPU_REG_WRITE(c, SSPP_SW_PIX_EXT_C1C2_REQ_PIXELS,
			tot_req_pixels[1]);

	/* color 3 */
	DPU_REG_WRITE(c, SSPP_SW_PIX_EXT_C3_LR, lr_pe[3]);
	DPU_REG_WRITE(c, SSPP_SW_PIX_EXT_C3_TB, lr_pe[3]);
	DPU_REG_WRITE(c, SSPP_SW_PIX_EXT_C3_REQ_PIXELS,
			tot_req_pixels[3]);
}

static void _dpu_hw_sspp_setup_scaler3(struct dpu_hw_sspp *ctx,
		struct dpu_hw_scaler3_cfg *scaler3_cfg,
		const struct msm_format *format)
{
	if (!ctx || !scaler3_cfg)
		return;

	dpu_hw_setup_scaler3(&ctx->hw, scaler3_cfg,
			ctx->cap->sblk->scaler_blk.base,
			ctx->cap->sblk->scaler_blk.version,
			format);
}

/*
 * dpu_hw_sspp_setup_rects()
 */
static void dpu_hw_sspp_setup_rects(struct dpu_sw_pipe *pipe,
		struct dpu_sw_pipe_cfg *cfg)
{
	struct dpu_hw_sspp *ctx = pipe->sspp;
	struct dpu_hw_blk_reg_map *c;
	u32 src_size, src_xy, dst_size, dst_xy;
	u32 src_size_off, src_xy_off, out_size_off, out_xy_off;

	if (!ctx || !cfg)
		return;

	c = &ctx->hw;

	if (pipe->multirect_index == DPU_SSPP_RECT_SOLO ||
	    pipe->multirect_index == DPU_SSPP_RECT_0) {
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

	/* rectangle register programming */
	DPU_REG_WRITE(c, src_size_off, src_size);
	DPU_REG_WRITE(c, src_xy_off, src_xy);
	DPU_REG_WRITE(c, out_size_off, dst_size);
	DPU_REG_WRITE(c, out_xy_off, dst_xy);
}

static void dpu_hw_sspp_setup_sourceaddress(struct dpu_sw_pipe *pipe,
		struct dpu_hw_fmt_layout *layout)
{
	struct dpu_hw_sspp *ctx = pipe->sspp;
	u32 ystride0, ystride1;
	int i;

	if (!ctx)
		return;

	if (pipe->multirect_index == DPU_SSPP_RECT_SOLO) {
		for (i = 0; i < ARRAY_SIZE(layout->plane_addr); i++)
			DPU_REG_WRITE(&ctx->hw, SSPP_SRC0_ADDR + i * 0x4,
					layout->plane_addr[i]);
	} else if (pipe->multirect_index == DPU_SSPP_RECT_0) {
		DPU_REG_WRITE(&ctx->hw, SSPP_SRC0_ADDR,
				layout->plane_addr[0]);
		DPU_REG_WRITE(&ctx->hw, SSPP_SRC2_ADDR,
				layout->plane_addr[2]);
	} else {
		DPU_REG_WRITE(&ctx->hw, SSPP_SRC1_ADDR,
				layout->plane_addr[0]);
		DPU_REG_WRITE(&ctx->hw, SSPP_SRC3_ADDR,
				layout->plane_addr[2]);
	}

	if (pipe->multirect_index == DPU_SSPP_RECT_SOLO) {
		ystride0 = (layout->plane_pitch[0]) |
			(layout->plane_pitch[1] << 16);
		ystride1 = (layout->plane_pitch[2]) |
			(layout->plane_pitch[3] << 16);
	} else {
		ystride0 = DPU_REG_READ(&ctx->hw, SSPP_SRC_YSTRIDE0);
		ystride1 = DPU_REG_READ(&ctx->hw, SSPP_SRC_YSTRIDE1);

		if (pipe->multirect_index == DPU_SSPP_RECT_0) {
			ystride0 = (ystride0 & 0xFFFF0000) |
				(layout->plane_pitch[0] & 0x0000FFFF);
			ystride1 = (ystride1 & 0xFFFF0000)|
				(layout->plane_pitch[2] & 0x0000FFFF);
		} else {
			ystride0 = (ystride0 & 0x0000FFFF) |
				((layout->plane_pitch[0] << 16) &
				 0xFFFF0000);
			ystride1 = (ystride1 & 0x0000FFFF) |
				((layout->plane_pitch[2] << 16) &
				 0xFFFF0000);
		}
	}

	DPU_REG_WRITE(&ctx->hw, SSPP_SRC_YSTRIDE0, ystride0);
	DPU_REG_WRITE(&ctx->hw, SSPP_SRC_YSTRIDE1, ystride1);
}

static void dpu_hw_sspp_setup_csc(struct dpu_hw_sspp *ctx,
		const struct dpu_csc_cfg *data)
{
	u32 offset;
	bool csc10 = false;

	if (!ctx || !data)
		return;

	offset = ctx->cap->sblk->csc_blk.base;

	if (test_bit(DPU_SSPP_CSC_10BIT, &ctx->cap->features)) {
		offset += CSC_10BIT_OFFSET;
		csc10 = true;
	}

	dpu_hw_csc_setup(&ctx->hw, offset, data, csc10);
}

static void dpu_hw_sspp_setup_solidfill(struct dpu_sw_pipe *pipe, u32 color)
{
	struct dpu_hw_sspp *ctx = pipe->sspp;
	struct dpu_hw_fmt_layout cfg;

	if (!ctx)
		return;

	/* cleanup source addresses */
	memset(&cfg, 0, sizeof(cfg));
	ctx->ops.setup_sourceaddress(pipe, &cfg);

	if (pipe->multirect_index == DPU_SSPP_RECT_SOLO ||
	    pipe->multirect_index == DPU_SSPP_RECT_0)
		DPU_REG_WRITE(&ctx->hw, SSPP_SRC_CONSTANT_COLOR, color);
	else
		DPU_REG_WRITE(&ctx->hw, SSPP_SRC_CONSTANT_COLOR_REC1,
				color);
}

static void dpu_hw_sspp_setup_qos_lut(struct dpu_hw_sspp *ctx,
				      struct dpu_hw_qos_cfg *cfg)
{
	if (!ctx || !cfg)
		return;

	_dpu_hw_setup_qos_lut(&ctx->hw, SSPP_DANGER_LUT,
			      test_bit(DPU_SSPP_QOS_8LVL, &ctx->cap->features),
			      cfg);
}

static void dpu_hw_sspp_setup_qos_ctrl(struct dpu_hw_sspp *ctx,
				       bool danger_safe_en)
{
	if (!ctx)
		return;

	DPU_REG_WRITE(&ctx->hw, SSPP_QOS_CTRL,
		      danger_safe_en ? SSPP_QOS_CTRL_DANGER_SAFE_EN : 0);
}

static void dpu_hw_sspp_setup_cdp(struct dpu_sw_pipe *pipe,
				  const struct msm_format *fmt,
				  bool enable)
{
	struct dpu_hw_sspp *ctx = pipe->sspp;
	u32 cdp_cntl_offset = 0;

	if (!ctx)
		return;

	if (pipe->multirect_index == DPU_SSPP_RECT_SOLO ||
	    pipe->multirect_index == DPU_SSPP_RECT_0)
		cdp_cntl_offset = SSPP_CDP_CNTL;
	else
		cdp_cntl_offset = SSPP_CDP_CNTL_REC1;

	dpu_setup_cdp(&ctx->hw, cdp_cntl_offset, fmt, enable);
}

static bool dpu_hw_sspp_setup_clk_force_ctrl(struct dpu_hw_sspp *ctx, bool enable)
{
	static const struct dpu_clk_ctrl_reg sspp_clk_ctrl = {
		.reg_off = SSPP_CLK_CTRL,
		.bit_off = 0
	};

	return dpu_hw_clk_force_ctrl(&ctx->hw, &sspp_clk_ctrl, enable);
}

static void _setup_layer_ops(struct dpu_hw_sspp *c,
		unsigned long features, const struct dpu_mdss_version *mdss_rev)
{
	c->ops.setup_format = dpu_hw_sspp_setup_format;
	c->ops.setup_rects = dpu_hw_sspp_setup_rects;
	c->ops.setup_sourceaddress = dpu_hw_sspp_setup_sourceaddress;
	c->ops.setup_solidfill = dpu_hw_sspp_setup_solidfill;
	c->ops.setup_pe = dpu_hw_sspp_setup_pe_config;

	if (test_bit(DPU_SSPP_QOS, &features)) {
		c->ops.setup_qos_lut = dpu_hw_sspp_setup_qos_lut;
		c->ops.setup_qos_ctrl = dpu_hw_sspp_setup_qos_ctrl;
	}

	if (test_bit(DPU_SSPP_CSC, &features) ||
		test_bit(DPU_SSPP_CSC_10BIT, &features))
		c->ops.setup_csc = dpu_hw_sspp_setup_csc;

	if (test_bit(DPU_SSPP_SMART_DMA_V1, &c->cap->features) ||
		test_bit(DPU_SSPP_SMART_DMA_V2, &c->cap->features))
		c->ops.setup_multirect = dpu_hw_sspp_setup_multirect;

	if (test_bit(DPU_SSPP_SCALER_QSEED3_COMPATIBLE, &features))
		c->ops.setup_scaler = _dpu_hw_sspp_setup_scaler3;

	if (test_bit(DPU_SSPP_CDP, &features))
		c->ops.setup_cdp = dpu_hw_sspp_setup_cdp;

	if (mdss_rev->core_major_ver >= 9)
		c->ops.setup_clk_force_ctrl = dpu_hw_sspp_setup_clk_force_ctrl;
}

#ifdef CONFIG_DEBUG_FS
int _dpu_hw_sspp_init_debugfs(struct dpu_hw_sspp *hw_pipe, struct dpu_kms *kms,
			      struct dentry *entry)
{
	const struct dpu_sspp_cfg *cfg = hw_pipe->cap;
	const struct dpu_sspp_sub_blks *sblk = cfg->sblk;
	struct dentry *debugfs_root;
	char sspp_name[32];

	snprintf(sspp_name, sizeof(sspp_name), "%d", hw_pipe->idx);

	/* create overall sub-directory for the pipe */
	debugfs_root =
		debugfs_create_dir(sspp_name, entry);

	/* don't error check these */
	debugfs_create_xul("features", 0600,
			debugfs_root, (unsigned long *)&hw_pipe->cap->features);

	/* add register dump support */
	dpu_debugfs_create_regset32("src_blk", 0400,
			debugfs_root,
			cfg->base,
			cfg->len,
			kms);

	if (sblk->scaler_blk.len)
		dpu_debugfs_create_regset32("scaler_blk", 0400,
				debugfs_root,
				sblk->scaler_blk.base + cfg->base,
				sblk->scaler_blk.len,
				kms);

	if (cfg->features & BIT(DPU_SSPP_CSC) ||
			cfg->features & BIT(DPU_SSPP_CSC_10BIT))
		dpu_debugfs_create_regset32("csc_blk", 0400,
				debugfs_root,
				sblk->csc_blk.base + cfg->base,
				sblk->csc_blk.len,
				kms);

	debugfs_create_u32("xin_id",
			0400,
			debugfs_root,
			(u32 *) &cfg->xin_id);
	debugfs_create_u32("clk_ctrl",
			0400,
			debugfs_root,
			(u32 *) &cfg->clk_ctrl);

	return 0;
}
#endif

struct dpu_hw_sspp *dpu_hw_sspp_init(struct drm_device *dev,
				     const struct dpu_sspp_cfg *cfg,
				     void __iomem *addr,
				     const struct msm_mdss_data *mdss_data,
				     const struct dpu_mdss_version *mdss_rev)
{
	struct dpu_hw_sspp *hw_pipe;

	if (!addr)
		return ERR_PTR(-EINVAL);

	hw_pipe = drmm_kzalloc(dev, sizeof(*hw_pipe), GFP_KERNEL);
	if (!hw_pipe)
		return ERR_PTR(-ENOMEM);

	hw_pipe->hw.blk_addr = addr + cfg->base;
	hw_pipe->hw.log_mask = DPU_DBG_MASK_SSPP;

	/* Assign ops */
	hw_pipe->ubwc = mdss_data;
	hw_pipe->idx = cfg->id;
	hw_pipe->cap = cfg;
	_setup_layer_ops(hw_pipe, hw_pipe->cap->features, mdss_rev);

	return hw_pipe;
}
