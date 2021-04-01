// SPDX-License-Identifier: GPL-2.0
/*
 * Hantro VPU codec driver
 *
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 *	Jeffy Chen <jeffy.chen@rock-chips.com>
 */

#include <linux/clk.h>

#include "hantro.h"
#include "hantro_jpeg.h"
#include "hantro_h1_regs.h"

#define RK3288_ACLK_MAX_FREQ (400 * 1000 * 1000)

/*
 * Supported formats.
 */

static const struct hantro_fmt rk3288_vpu_enc_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_YUV420M,
		.codec_mode = HANTRO_MODE_NONE,
		.enc_fmt = RK3288_VPU_ENC_FMT_YUV420P,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV12M,
		.codec_mode = HANTRO_MODE_NONE,
		.enc_fmt = RK3288_VPU_ENC_FMT_YUV420SP,
	},
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.codec_mode = HANTRO_MODE_NONE,
		.enc_fmt = RK3288_VPU_ENC_FMT_YUYV422,
	},
	{
		.fourcc = V4L2_PIX_FMT_UYVY,
		.codec_mode = HANTRO_MODE_NONE,
		.enc_fmt = RK3288_VPU_ENC_FMT_UYVY422,
	},
	{
		.fourcc = V4L2_PIX_FMT_JPEG,
		.codec_mode = HANTRO_MODE_JPEG_ENC,
		.max_depth = 2,
		.header_size = JPEG_HEADER_SIZE,
		.frmsize = {
			.min_width = 96,
			.max_width = 8192,
			.step_width = MB_DIM,
			.min_height = 32,
			.max_height = 8192,
			.step_height = MB_DIM,
		},
	},
};

static const struct hantro_fmt rk3288_vpu_postproc_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.codec_mode = HANTRO_MODE_NONE,
	},
};

static const struct hantro_fmt rk3288_vpu_dec_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.codec_mode = HANTRO_MODE_NONE,
	},
	{
		.fourcc = V4L2_PIX_FMT_H264_SLICE,
		.codec_mode = HANTRO_MODE_H264_DEC,
		.max_depth = 2,
		.frmsize = {
			.min_width = 48,
			.max_width = 4096,
			.step_width = MB_DIM,
			.min_height = 48,
			.max_height = 2304,
			.step_height = MB_DIM,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_MPEG2_SLICE,
		.codec_mode = HANTRO_MODE_MPEG2_DEC,
		.max_depth = 2,
		.frmsize = {
			.min_width = 48,
			.max_width = 1920,
			.step_width = MB_DIM,
			.min_height = 48,
			.max_height = 1088,
			.step_height = MB_DIM,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_VP8_FRAME,
		.codec_mode = HANTRO_MODE_VP8_DEC,
		.max_depth = 2,
		.frmsize = {
			.min_width = 48,
			.max_width = 3840,
			.step_width = MB_DIM,
			.min_height = 48,
			.max_height = 2160,
			.step_height = MB_DIM,
		},
	},
};

static irqreturn_t rk3288_vepu_irq(int irq, void *dev_id)
{
	struct hantro_dev *vpu = dev_id;
	enum vb2_buffer_state state;
	u32 status;

	status = vepu_read(vpu, H1_REG_INTERRUPT);
	state = (status & H1_REG_INTERRUPT_FRAME_RDY) ?
		VB2_BUF_STATE_DONE : VB2_BUF_STATE_ERROR;

	vepu_write(vpu, 0, H1_REG_INTERRUPT);
	vepu_write(vpu, 0, H1_REG_AXI_CTRL);

	hantro_irq_done(vpu, state);

	return IRQ_HANDLED;
}

static int rk3288_vpu_hw_init(struct hantro_dev *vpu)
{
	/* Bump ACLK to max. possible freq. to improve performance. */
	clk_set_rate(vpu->clocks[0].clk, RK3288_ACLK_MAX_FREQ);
	return 0;
}

static void rk3288_vpu_enc_reset(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;

	vepu_write(vpu, H1_REG_INTERRUPT_DIS_BIT, H1_REG_INTERRUPT);
	vepu_write(vpu, 0, H1_REG_ENC_CTRL);
	vepu_write(vpu, 0, H1_REG_AXI_CTRL);
}

/*
 * Supported codec ops.
 */

static const struct hantro_codec_ops rk3288_vpu_codec_ops[] = {
	[HANTRO_MODE_JPEG_ENC] = {
		.run = hantro_h1_jpeg_enc_run,
		.reset = rk3288_vpu_enc_reset,
		.init = hantro_jpeg_enc_init,
		.done = hantro_jpeg_enc_done,
		.exit = hantro_jpeg_enc_exit,
	},
	[HANTRO_MODE_H264_DEC] = {
		.run = hantro_g1_h264_dec_run,
		.reset = hantro_g1_reset,
		.init = hantro_h264_dec_init,
		.exit = hantro_h264_dec_exit,
	},
	[HANTRO_MODE_MPEG2_DEC] = {
		.run = hantro_g1_mpeg2_dec_run,
		.reset = hantro_g1_reset,
		.init = hantro_mpeg2_dec_init,
		.exit = hantro_mpeg2_dec_exit,
	},
	[HANTRO_MODE_VP8_DEC] = {
		.run = hantro_g1_vp8_dec_run,
		.reset = hantro_g1_reset,
		.init = hantro_vp8_dec_init,
		.exit = hantro_vp8_dec_exit,
	},
};

/*
 * VPU variant.
 */

static const struct hantro_irq rk3288_irqs[] = {
	{ "vepu", rk3288_vepu_irq },
	{ "vdpu", hantro_g1_irq },
};

static const char * const rk3288_clk_names[] = {
	"aclk", "hclk"
};

const struct hantro_variant rk3288_vpu_variant = {
	.enc_offset = 0x0,
	.enc_fmts = rk3288_vpu_enc_fmts,
	.num_enc_fmts = ARRAY_SIZE(rk3288_vpu_enc_fmts),
	.dec_offset = 0x400,
	.dec_fmts = rk3288_vpu_dec_fmts,
	.num_dec_fmts = ARRAY_SIZE(rk3288_vpu_dec_fmts),
	.postproc_fmts = rk3288_vpu_postproc_fmts,
	.num_postproc_fmts = ARRAY_SIZE(rk3288_vpu_postproc_fmts),
	.postproc_regs = &hantro_g1_postproc_regs,
	.codec = HANTRO_JPEG_ENCODER | HANTRO_MPEG2_DECODER |
		 HANTRO_VP8_DECODER | HANTRO_H264_DECODER,
	.codec_ops = rk3288_vpu_codec_ops,
	.irqs = rk3288_irqs,
	.num_irqs = ARRAY_SIZE(rk3288_irqs),
	.init = rk3288_vpu_hw_init,
	.clk_names = rk3288_clk_names,
	.num_clocks = ARRAY_SIZE(rk3288_clk_names)
};
