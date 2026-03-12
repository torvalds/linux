// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/printk.h>
#include <linux/soc/qcom/ubwc.h>

#include "dpu_hw_sspp.h"

/* >= v13 DPU */
/* CMN Registers -> Source Surface Processing Pipe Common SSPP registers */
/*      Name                                  Offset */
#define SSPP_CMN_CLK_CTRL                0x0
#define SSPP_CMN_CLK_STATUS              0x4
#define SSPP_CMN_MULTI_REC_OP_MODE       0x10
#define SSPP_CMN_ADDR_CONFIG             0x14
#define SSPP_CMN_CAC_CTRL                0x20
#define SSPP_CMN_SYS_CACHE_MODE          0x24
#define SSPP_CMN_QOS_CTRL                0x28

#define SSPP_CMN_FILL_LEVEL_SCALE                0x3c
#define SSPP_CMN_FILL_LEVELS                     0x40
#define SSPP_CMN_STATUS                          0x44
#define SSPP_CMN_FETCH_DMA_RD_OTS                0x48
#define SSPP_CMN_FETCH_DTB_WR_PLANE0             0x4c
#define SSPP_CMN_FETCH_DTB_WR_PLANE1             0x50
#define SSPP_CMN_FETCH_DTB_WR_PLANE2             0x54
#define SSPP_CMN_DTB_UNPACK_RD_PLANE0            0x58
#define SSPP_CMN_DTB_UNPACK_RD_PLANE1            0x5c
#define SSPP_CMN_DTB_UNPACK_RD_PLANE2            0x60
#define SSPP_CMN_UNPACK_LINE_COUNT               0x64
#define SSPP_CMN_TPG_CONTROL                     0x68
#define SSPP_CMN_TPG_CONFIG                      0x6c
#define SSPP_CMN_TPG_COMPONENT_LIMITS            0x70
#define SSPP_CMN_TPG_RECTANGLE                   0x74
#define SSPP_CMN_TPG_BLACK_WHITE_PATTERN_FRAMES  0x78
#define SSPP_CMN_TPG_RGB_MAPPING                 0x7c
#define SSPP_CMN_TPG_PATTERN_GEN_INIT_VAL        0x80

/*RECRegisterset*/
/*Name        Offset*/
#define SSPP_REC_SRC_FORMAT                             0x0
#define SSPP_REC_SRC_UNPACK_PATTERN                     0x4
#define SSPP_REC_SRC_OP_MODE                            0x8
#define SSPP_REC_SRC_CONSTANT_COLOR                     0xc
#define SSPP_REC_SRC_IMG_SIZE                           0x10
#define SSPP_REC_SRC_SIZE                               0x14
#define SSPP_REC_SRC_XY                                 0x18
#define SSPP_REC_OUT_SIZE                               0x1c
#define SSPP_REC_OUT_XY                                 0x20
#define SSPP_REC_SW_PIX_EXT_LR                          0x24
#define SSPP_REC_SW_PIX_EXT_TB                          0x28
#define SSPP_REC_SRC_SIZE_ODX                           0x30
#define SSPP_REC_SRC_XY_ODX                             0x34
#define SSPP_REC_OUT_SIZE_ODX                           0x38
#define SSPP_REC_OUT_XY_ODX                             0x3c
#define SSPP_REC_SW_PIX_EXT_LR_ODX                      0x40
#define SSPP_REC_SW_PIX_EXT_TB_ODX                      0x44
#define SSPP_REC_PRE_DOWN_SCALE                         0x48
#define SSPP_REC_SRC0_ADDR                              0x4c
#define SSPP_REC_SRC1_ADDR                              0x50
#define SSPP_REC_SRC2_ADDR                              0x54
#define SSPP_REC_SRC3_ADDR                              0x58
#define SSPP_REC_SRC_YSTRIDE0                           0x5c
#define SSPP_REC_SRC_YSTRIDE1                           0x60
#define SSPP_REC_CURRENT_SRC0_ADDR                      0x64
#define SSPP_REC_CURRENT_SRC1_ADDR                      0x68
#define SSPP_REC_CURRENT_SRC2_ADDR                      0x6c
#define SSPP_REC_CURRENT_SRC3_ADDR                      0x70
#define SSPP_REC_SRC_ADDR_SW_STATUS                     0x74
#define SSPP_REC_CDP_CNTL                               0x78
#define SSPP_REC_TRAFFIC_SHAPER                         0x7c
#define SSPP_REC_TRAFFIC_SHAPER_PREFILL                 0x80
#define SSPP_REC_PD_MEM_ALLOC                           0x84
#define SSPP_REC_QOS_CLAMP                              0x88
#define SSPP_REC_UIDLE_CTRL_VALUE                       0x8c
#define SSPP_REC_UBWC_STATIC_CTRL                       0x90
#define SSPP_REC_UBWC_STATIC_CTRL_OVERRIDE              0x94
#define SSPP_REC_UBWC_STATS_ROI                         0x98
#define SSPP_REC_UBWC_STATS_WORST_TILE_ROW_BW_ROI0      0x9c
#define SSPP_REC_UBWC_STATS_TOTAL_BW_ROI0               0xa0
#define SSPP_REC_UBWC_STATS_WORST_TILE_ROW_BW_ROI1      0xa4
#define SSPP_REC_UBWC_STATS_TOTAL_BW_ROI1               0xa8
#define SSPP_REC_UBWC_STATS_WORST_TILE_ROW_BW_ROI2      0xac
#define SSPP_REC_UBWC_STATS_TOTAL_BW_ROI2               0xb0
#define SSPP_REC_EXCL_REC_CTRL                          0xb4
#define SSPP_REC_EXCL_REC_SIZE                          0xb8
#define SSPP_REC_EXCL_REC_XY                            0xbc
#define SSPP_REC_LINE_INSERTION_CTRL                    0xc0
#define SSPP_REC_LINE_INSERTION_OUT_SIZE                0xc4
#define SSPP_REC_FETCH_PIPE_ACTIVE                      0xc8
#define SSPP_REC_META_ERROR_STATUS                      0xcc
#define SSPP_REC_UBWC_ERROR_STATUS                      0xd0
#define SSPP_REC_FLUSH_CTRL                             0xd4
#define SSPP_REC_INTR_EN                                0xd8
#define SSPP_REC_INTR_STATUS                            0xdc
#define SSPP_REC_INTR_CLEAR                             0xe0
#define SSPP_REC_HSYNC_STATUS                           0xe4
#define SSPP_REC_FP16_CONFIG                            0x150
#define SSPP_REC_FP16_CSC_MATRIX_COEFF_R_0              0x154
#define SSPP_REC_FP16_CSC_MATRIX_COEFF_R_1              0x158
#define SSPP_REC_FP16_CSC_MATRIX_COEFF_G_0              0x15c
#define SSPP_REC_FP16_CSC_MATRIX_COEFF_G_1              0x160
#define SSPP_REC_FP16_CSC_MATRIX_COEFF_B_0              0x164
#define SSPP_REC_FP16_CSC_MATRIX_COEFF_B_1              0x168
#define SSPP_REC_FP16_CSC_PRE_CLAMP_R                   0x16c
#define SSPP_REC_FP16_CSC_PRE_CLAMP_G                   0x170
#define SSPP_REC_FP16_CSC_PRE_CLAMP_B                   0x174
#define SSPP_REC_FP16_CSC_POST_CLAMP                    0x178

static inline u32 dpu_hw_sspp_calculate_rect_off(enum dpu_sspp_multirect_index rect_index,
						 struct dpu_hw_sspp *ctx)
{
	return (rect_index == DPU_SSPP_RECT_SOLO || rect_index == DPU_SSPP_RECT_0) ?
		ctx->cap->sblk->sspp_rec0_blk.base : ctx->cap->sblk->sspp_rec1_blk.base;
}

static void dpu_hw_sspp_setup_multirect_v13(struct dpu_sw_pipe *pipe)
{
	struct dpu_hw_sspp *ctx = pipe->sspp;

	if (!ctx)
		return;

	dpu_hw_setup_multirect_impl(pipe, ctx, SSPP_CMN_MULTI_REC_OP_MODE);
}

static void dpu_hw_sspp_setup_format_v13(struct dpu_sw_pipe *pipe,
					 const struct msm_format *fmt, u32 flags)
{
	struct dpu_hw_sspp *ctx = pipe->sspp;
	u32 op_mode_off, unpack_pat_off, format_off;
	u32 ubwc_ctrl_off, ubwc_err_off;
	u32 offset;

	if (!ctx || !fmt)
		return;

	offset = dpu_hw_sspp_calculate_rect_off(pipe->multirect_index, ctx);

	op_mode_off = offset + SSPP_REC_SRC_OP_MODE;
	unpack_pat_off = offset + SSPP_REC_SRC_UNPACK_PATTERN;
	format_off = offset + SSPP_REC_SRC_FORMAT;
	ubwc_ctrl_off = offset + SSPP_REC_UBWC_STATIC_CTRL;
	ubwc_err_off = offset + SSPP_REC_UBWC_ERROR_STATUS;

	dpu_hw_setup_format_impl(pipe, fmt, flags, ctx, op_mode_off,
				 unpack_pat_off, format_off, ubwc_ctrl_off, ubwc_err_off);
}

static void dpu_hw_sspp_setup_pe_config_v13(struct dpu_hw_sspp *ctx,
					    struct dpu_hw_pixel_ext *pe_ext)
{
	struct dpu_hw_blk_reg_map *c;
	u8 color;
	u32 lr_pe[4], tb_pe[4];
	const u32 bytemask = 0xff;
	u32 offset = ctx->cap->sblk->sspp_rec0_blk.base;

	if (!ctx || !pe_ext)
		return;

	c = &ctx->hw;
	/* program SW pixel extension override for all pipes*/
	for (color = 0; color < DPU_MAX_PLANES; color++) {
		/* color 2 has the same set of registers as color 1 */
		if (color == 2)
			continue;

		lr_pe[color] = ((pe_ext->right_ftch[color] & bytemask) << 24) |
			       ((pe_ext->right_rpt[color] & bytemask) << 16) |
			       ((pe_ext->left_ftch[color] & bytemask) << 8) |
			       (pe_ext->left_rpt[color] & bytemask);

		tb_pe[color] = ((pe_ext->btm_ftch[color] & bytemask) << 24) |
			       ((pe_ext->btm_rpt[color] & bytemask) << 16) |
			       ((pe_ext->top_ftch[color] & bytemask) << 8) |
			       (pe_ext->top_rpt[color] & bytemask);
	}

	/* color 0 */
	DPU_REG_WRITE(c, SSPP_REC_SW_PIX_EXT_LR + offset, lr_pe[0]);
	DPU_REG_WRITE(c, SSPP_REC_SW_PIX_EXT_TB + offset, tb_pe[0]);

	/* color 1 and color 2 */
	DPU_REG_WRITE(c, SSPP_REC_SW_PIX_EXT_LR_ODX + offset, lr_pe[1]);
	DPU_REG_WRITE(c, SSPP_REC_SW_PIX_EXT_TB_ODX + offset, tb_pe[1]);
}

static void dpu_hw_sspp_setup_rects_v13(struct dpu_sw_pipe *pipe,
					struct dpu_sw_pipe_cfg *cfg)
{
	struct dpu_hw_sspp *ctx = pipe->sspp;
	u32 src_size_off, src_xy_off, out_size_off, out_xy_off;
	u32 offset;

	if (!ctx || !cfg)
		return;

	offset = dpu_hw_sspp_calculate_rect_off(pipe->multirect_index, ctx);

	src_size_off = offset + SSPP_REC_SRC_SIZE;
	src_xy_off = offset + SSPP_REC_SRC_XY;
	out_size_off = offset + SSPP_REC_OUT_SIZE;
	out_xy_off = offset + SSPP_REC_OUT_XY;

	dpu_hw_setup_rects_impl(pipe, cfg, ctx, src_size_off,
				src_xy_off, out_size_off, out_xy_off);
}

static void dpu_hw_sspp_setup_sourceaddress_v13(struct dpu_sw_pipe *pipe,
						struct dpu_hw_fmt_layout *layout)
{
	struct dpu_hw_sspp *ctx = pipe->sspp;
	int i;
	u32 offset, ystride0, ystride1;

	if (!ctx)
		return;

	offset = dpu_hw_sspp_calculate_rect_off(pipe->multirect_index, ctx);

	for (i = 0; i < ARRAY_SIZE(layout->plane_addr); i++)
		DPU_REG_WRITE(&ctx->hw, offset + SSPP_REC_SRC0_ADDR + i * 0x4,
			      layout->plane_addr[i]);

	ystride0 = (layout->plane_pitch[0]) | (layout->plane_pitch[2] << 16);
	ystride1 = (layout->plane_pitch[1]) | (layout->plane_pitch[3] << 16);

	DPU_REG_WRITE(&ctx->hw, offset + SSPP_REC_SRC_YSTRIDE0, ystride0);
	DPU_REG_WRITE(&ctx->hw, offset + SSPP_REC_SRC_YSTRIDE1, ystride1);
}

static void dpu_hw_sspp_setup_solidfill_v13(struct dpu_sw_pipe *pipe, u32 color)
{
	struct dpu_hw_sspp *ctx = pipe->sspp;
	u32 const_clr_off;
	u32 offset;

	if (!ctx)
		return;

	offset = dpu_hw_sspp_calculate_rect_off(pipe->multirect_index, ctx);
	const_clr_off = offset + SSPP_REC_SRC_CONSTANT_COLOR;

	dpu_hw_setup_solidfill_impl(pipe, color, ctx, const_clr_off);
}

static void dpu_hw_sspp_setup_qos_lut_v13(struct dpu_hw_sspp *ctx,
					  struct dpu_hw_qos_cfg *cfg)
{
	if (!ctx || !cfg)
		return;

	dpu_hw_setup_qos_lut_v13(&ctx->hw, cfg);
}

static void dpu_hw_sspp_setup_qos_ctrl_v13(struct dpu_hw_sspp *ctx,
					   bool danger_safe_en)
{
	if (!ctx)
		return;

	dpu_hw_sspp_setup_qos_ctrl_impl(ctx, danger_safe_en, SSPP_CMN_QOS_CTRL);
}

static void dpu_hw_sspp_setup_cdp_v13(struct dpu_sw_pipe *pipe,
				      const struct msm_format *fmt,
				      bool enable)
{
	struct dpu_hw_sspp *ctx = pipe->sspp;
	u32 offset = 0;

	if (!ctx)
		return;

	offset = dpu_hw_sspp_calculate_rect_off(pipe->multirect_index, ctx);
	dpu_setup_cdp(&ctx->hw, offset + SSPP_REC_CDP_CNTL, fmt, enable);
}

static bool dpu_hw_sspp_setup_clk_force_ctrl_v13(struct dpu_hw_sspp *ctx, bool enable)
{
	static const struct dpu_clk_ctrl_reg sspp_clk_ctrl = {
		.reg_off = SSPP_CMN_CLK_CTRL,
		.bit_off = 0
	};

	return dpu_hw_clk_force_ctrl(&ctx->hw, &sspp_clk_ctrl, enable);
}

void dpu_hw_sspp_init_v13(struct dpu_hw_sspp *c,
			  unsigned long features, const struct dpu_mdss_version *mdss_rev)
{
	c->ops.setup_format = dpu_hw_sspp_setup_format_v13;
	c->ops.setup_rects = dpu_hw_sspp_setup_rects_v13;
	c->ops.setup_sourceaddress = dpu_hw_sspp_setup_sourceaddress_v13;
	c->ops.setup_solidfill = dpu_hw_sspp_setup_solidfill_v13;
	c->ops.setup_pe = dpu_hw_sspp_setup_pe_config_v13;

	if (test_bit(DPU_SSPP_QOS, &features)) {
		c->ops.setup_qos_lut = dpu_hw_sspp_setup_qos_lut_v13;
		c->ops.setup_qos_ctrl = dpu_hw_sspp_setup_qos_ctrl_v13;
	}

	if (test_bit(DPU_SSPP_CSC, &features) ||
		test_bit(DPU_SSPP_CSC_10BIT, &features))
		c->ops.setup_csc = dpu_hw_sspp_setup_csc;

	if (test_bit(DPU_SSPP_SMART_DMA_V1, &c->cap->features) ||
		test_bit(DPU_SSPP_SMART_DMA_V2, &c->cap->features))
		c->ops.setup_multirect = dpu_hw_sspp_setup_multirect_v13;

	if (test_bit(DPU_SSPP_SCALER_QSEED3_COMPATIBLE, &features))
		c->ops.setup_scaler = dpu_hw_sspp_setup_scaler3;

	if (test_bit(DPU_SSPP_CDP, &features))
		c->ops.setup_cdp = dpu_hw_sspp_setup_cdp_v13;

	c->ops.setup_clk_force_ctrl = dpu_hw_sspp_setup_clk_force_ctrl_v13;
}
