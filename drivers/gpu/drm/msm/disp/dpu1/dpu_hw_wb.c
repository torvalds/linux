// SPDX-License-Identifier: GPL-2.0-only
 /*
  * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved
  */

#include "dpu_hw_mdss.h"
#include "dpu_hwio.h"
#include "dpu_hw_catalog.h"
#include "dpu_hw_wb.h"
#include "dpu_formats.h"
#include "dpu_kms.h"

#define WB_DST_FORMAT                         0x000
#define WB_DST_OP_MODE                        0x004
#define WB_DST_PACK_PATTERN                   0x008
#define WB_DST0_ADDR                          0x00C
#define WB_DST1_ADDR                          0x010
#define WB_DST2_ADDR                          0x014
#define WB_DST3_ADDR                          0x018
#define WB_DST_YSTRIDE0                       0x01C
#define WB_DST_YSTRIDE1                       0x020
#define WB_DST_YSTRIDE1                       0x020
#define WB_DST_DITHER_BITDEPTH                0x024
#define WB_DST_MATRIX_ROW0                    0x030
#define WB_DST_MATRIX_ROW1                    0x034
#define WB_DST_MATRIX_ROW2                    0x038
#define WB_DST_MATRIX_ROW3                    0x03C
#define WB_DST_WRITE_CONFIG                   0x048
#define WB_ROTATION_DNSCALER                  0x050
#define WB_ROTATOR_PIPE_DOWNSCALER            0x054
#define WB_N16_INIT_PHASE_X_C03               0x060
#define WB_N16_INIT_PHASE_X_C12               0x064
#define WB_N16_INIT_PHASE_Y_C03               0x068
#define WB_N16_INIT_PHASE_Y_C12               0x06C
#define WB_OUT_SIZE                           0x074
#define WB_ALPHA_X_VALUE                      0x078
#define WB_DANGER_LUT                         0x084
#define WB_SAFE_LUT                           0x088
#define WB_QOS_CTRL                           0x090
#define WB_CREQ_LUT_0                         0x098
#define WB_CREQ_LUT_1                         0x09C
#define WB_UBWC_STATIC_CTRL                   0x144
#define WB_MUX                                0x150
#define WB_CROP_CTRL                          0x154
#define WB_CROP_OFFSET                        0x158
#define WB_CSC_BASE                           0x260
#define WB_DST_ADDR_SW_STATUS                 0x2B0
#define WB_CDP_CNTL                           0x2B4
#define WB_OUT_IMAGE_SIZE                     0x2C0
#define WB_OUT_XY                             0x2C4

static void dpu_hw_wb_setup_outaddress(struct dpu_hw_wb *ctx,
		struct dpu_hw_wb_cfg *data)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;

	DPU_REG_WRITE(c, WB_DST0_ADDR, data->dest.plane_addr[0]);
	DPU_REG_WRITE(c, WB_DST1_ADDR, data->dest.plane_addr[1]);
	DPU_REG_WRITE(c, WB_DST2_ADDR, data->dest.plane_addr[2]);
	DPU_REG_WRITE(c, WB_DST3_ADDR, data->dest.plane_addr[3]);
}

static void dpu_hw_wb_setup_format(struct dpu_hw_wb *ctx,
		struct dpu_hw_wb_cfg *data)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;
	const struct dpu_format *fmt = data->dest.format;
	u32 dst_format, pattern, ystride0, ystride1, outsize, chroma_samp;
	u32 write_config = 0;
	u32 opmode = 0;
	u32 dst_addr_sw = 0;

	chroma_samp = fmt->chroma_sample;

	dst_format = (chroma_samp << 23) |
		(fmt->fetch_planes << 19) |
		(fmt->bits[C3_ALPHA] << 6) |
		(fmt->bits[C2_R_Cr] << 4) |
		(fmt->bits[C1_B_Cb] << 2) |
		(fmt->bits[C0_G_Y] << 0);

	if (fmt->bits[C3_ALPHA] || fmt->alpha_enable) {
		dst_format |= BIT(8); /* DSTC3_EN */
		if (!fmt->alpha_enable ||
			!(ctx->caps->features & BIT(DPU_WB_PIPE_ALPHA)))
			dst_format |= BIT(14); /* DST_ALPHA_X */
	}

	if (DPU_FORMAT_IS_YUV(fmt))
		dst_format |= BIT(15);

	pattern = (fmt->element[3] << 24) |
		(fmt->element[2] << 16) |
		(fmt->element[1] << 8)  |
		(fmt->element[0] << 0);

	dst_format |= (fmt->unpack_align_msb << 18) |
		(fmt->unpack_tight << 17) |
		((fmt->unpack_count - 1) << 12) |
		((fmt->bpp - 1) << 9);

	ystride0 = data->dest.plane_pitch[0] |
		(data->dest.plane_pitch[1] << 16);
	ystride1 = data->dest.plane_pitch[2] |
	(data->dest.plane_pitch[3] << 16);

	if (drm_rect_height(&data->roi) && drm_rect_width(&data->roi))
		outsize = (drm_rect_height(&data->roi) << 16) | drm_rect_width(&data->roi);
	else
		outsize = (data->dest.height << 16) | data->dest.width;

	DPU_REG_WRITE(c, WB_ALPHA_X_VALUE, 0xFF);
	DPU_REG_WRITE(c, WB_DST_FORMAT, dst_format);
	DPU_REG_WRITE(c, WB_DST_OP_MODE, opmode);
	DPU_REG_WRITE(c, WB_DST_PACK_PATTERN, pattern);
	DPU_REG_WRITE(c, WB_DST_YSTRIDE0, ystride0);
	DPU_REG_WRITE(c, WB_DST_YSTRIDE1, ystride1);
	DPU_REG_WRITE(c, WB_OUT_SIZE, outsize);
	DPU_REG_WRITE(c, WB_DST_WRITE_CONFIG, write_config);
	DPU_REG_WRITE(c, WB_DST_ADDR_SW_STATUS, dst_addr_sw);
}

static void dpu_hw_wb_roi(struct dpu_hw_wb *ctx, struct dpu_hw_wb_cfg *wb)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;
	u32 image_size, out_size, out_xy;

	image_size = (wb->dest.height << 16) | wb->dest.width;
	out_xy = 0;
	out_size = (drm_rect_height(&wb->roi) << 16) | drm_rect_width(&wb->roi);

	DPU_REG_WRITE(c, WB_OUT_IMAGE_SIZE, image_size);
	DPU_REG_WRITE(c, WB_OUT_XY, out_xy);
	DPU_REG_WRITE(c, WB_OUT_SIZE, out_size);
}

static void dpu_hw_wb_setup_qos_lut(struct dpu_hw_wb *ctx,
		struct dpu_hw_qos_cfg *cfg)
{
	if (!ctx || !cfg)
		return;

	_dpu_hw_setup_qos_lut(&ctx->hw, WB_DANGER_LUT,
			      test_bit(DPU_WB_QOS_8LVL, &ctx->caps->features),
			      cfg);
}

static void dpu_hw_wb_setup_cdp(struct dpu_hw_wb *ctx,
				const struct dpu_format *fmt,
				bool enable)
{
	if (!ctx)
		return;

	dpu_setup_cdp(&ctx->hw, WB_CDP_CNTL, fmt, enable);
}

static void dpu_hw_wb_bind_pingpong_blk(
		struct dpu_hw_wb *ctx,
		const enum dpu_pingpong pp)
{
	struct dpu_hw_blk_reg_map *c;
	int mux_cfg;

	if (!ctx)
		return;

	c = &ctx->hw;

	mux_cfg = DPU_REG_READ(c, WB_MUX);
	mux_cfg &= ~0xf;

	if (pp)
		mux_cfg |= (pp - PINGPONG_0) & 0x7;
	else
		mux_cfg |= 0xf;

	DPU_REG_WRITE(c, WB_MUX, mux_cfg);
}

static void _setup_wb_ops(struct dpu_hw_wb_ops *ops,
		unsigned long features)
{
	ops->setup_outaddress = dpu_hw_wb_setup_outaddress;
	ops->setup_outformat = dpu_hw_wb_setup_format;

	if (test_bit(DPU_WB_XY_ROI_OFFSET, &features))
		ops->setup_roi = dpu_hw_wb_roi;

	if (test_bit(DPU_WB_QOS, &features))
		ops->setup_qos_lut = dpu_hw_wb_setup_qos_lut;

	if (test_bit(DPU_WB_CDP, &features))
		ops->setup_cdp = dpu_hw_wb_setup_cdp;

	if (test_bit(DPU_WB_INPUT_CTRL, &features))
		ops->bind_pingpong_blk = dpu_hw_wb_bind_pingpong_blk;
}

struct dpu_hw_wb *dpu_hw_wb_init(const struct dpu_wb_cfg *cfg,
		void __iomem *addr)
{
	struct dpu_hw_wb *c;

	if (!addr)
		return ERR_PTR(-EINVAL);

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return ERR_PTR(-ENOMEM);

	c->hw.blk_addr = addr + cfg->base;
	c->hw.log_mask = DPU_DBG_MASK_WB;

	/* Assign ops */
	c->idx = cfg->id;
	c->caps = cfg;
	_setup_wb_ops(&c->ops, c->caps->features);

	return c;
}

void dpu_hw_wb_destroy(struct dpu_hw_wb *hw_wb)
{
	kfree(hw_wb);
}
