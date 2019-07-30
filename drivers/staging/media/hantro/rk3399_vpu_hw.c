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
#include "rk3399_vpu_regs.h"

#define RK3399_ACLK_MAX_FREQ (400 * 1000 * 1000)

/*
 * Supported formats.
 */

static const struct hantro_fmt rk3399_vpu_enc_fmts[] = {
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
			.step_width = JPEG_MB_DIM,
			.min_height = 32,
			.max_height = 8192,
			.step_height = JPEG_MB_DIM,
		},
	},
};

static const struct hantro_fmt rk3399_vpu_dec_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.codec_mode = HANTRO_MODE_NONE,
	},
	{
		.fourcc = V4L2_PIX_FMT_MPEG2_SLICE,
		.codec_mode = HANTRO_MODE_MPEG2_DEC,
		.max_depth = 2,
		.frmsize = {
			.min_width = 48,
			.max_width = 1920,
			.step_width = MPEG2_MB_DIM,
			.min_height = 48,
			.max_height = 1088,
			.step_height = MPEG2_MB_DIM,
		},
	},
};

static irqreturn_t rk3399_vepu_irq(int irq, void *dev_id)
{
	struct hantro_dev *vpu = dev_id;
	enum vb2_buffer_state state;
	u32 status, bytesused;

	status = vepu_read(vpu, VEPU_REG_INTERRUPT);
	bytesused = vepu_read(vpu, VEPU_REG_STR_BUF_LIMIT) / 8;
	state = (status & VEPU_REG_INTERRUPT_FRAME_READY) ?
		VB2_BUF_STATE_DONE : VB2_BUF_STATE_ERROR;

	vepu_write(vpu, 0, VEPU_REG_INTERRUPT);
	vepu_write(vpu, 0, VEPU_REG_AXI_CTRL);

	hantro_irq_done(vpu, bytesused, state);

	return IRQ_HANDLED;
}

static irqreturn_t rk3399_vdpu_irq(int irq, void *dev_id)
{
	struct hantro_dev *vpu = dev_id;
	enum vb2_buffer_state state;
	u32 status;

	status = vdpu_read(vpu, VDPU_REG_INTERRUPT);
	state = (status & VDPU_REG_INTERRUPT_DEC_IRQ) ?
		VB2_BUF_STATE_DONE : VB2_BUF_STATE_ERROR;

	vdpu_write(vpu, 0, VDPU_REG_INTERRUPT);
	vdpu_write(vpu, 0, VDPU_REG_AXI_CTRL);

	hantro_irq_done(vpu, 0, state);

	return IRQ_HANDLED;
}

static int rk3399_vpu_hw_init(struct hantro_dev *vpu)
{
	/* Bump ACLK to max. possible freq. to improve performance. */
	clk_set_rate(vpu->clocks[0].clk, RK3399_ACLK_MAX_FREQ);
	return 0;
}

static void rk3399_vpu_enc_reset(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;

	vepu_write(vpu, VEPU_REG_INTERRUPT_DIS_BIT, VEPU_REG_INTERRUPT);
	vepu_write(vpu, 0, VEPU_REG_ENCODE_START);
	vepu_write(vpu, 0, VEPU_REG_AXI_CTRL);
}

static void rk3399_vpu_dec_reset(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;

	vdpu_write(vpu, VDPU_REG_INTERRUPT_DEC_IRQ_DIS, VDPU_REG_INTERRUPT);
	vdpu_write(vpu, 0, VDPU_REG_EN_FLAGS);
	vdpu_write(vpu, 1, VDPU_REG_SOFT_RESET);
}

/*
 * Supported codec ops.
 */

static const struct hantro_codec_ops rk3399_vpu_codec_ops[] = {
	[HANTRO_MODE_JPEG_ENC] = {
		.run = rk3399_vpu_jpeg_enc_run,
		.reset = rk3399_vpu_enc_reset,
		.init = hantro_jpeg_enc_init,
		.exit = hantro_jpeg_enc_exit,
	},
	[HANTRO_MODE_MPEG2_DEC] = {
		.run = rk3399_vpu_mpeg2_dec_run,
		.reset = rk3399_vpu_dec_reset,
		.init = hantro_mpeg2_dec_init,
		.exit = hantro_mpeg2_dec_exit,
	},
};

/*
 * VPU variant.
 */

static const struct hantro_irq rk3399_irqs[] = {
	{ "vepu", rk3399_vepu_irq },
	{ "vdpu", rk3399_vdpu_irq },
};

static const char * const rk3399_clk_names[] = {
	"aclk", "hclk"
};

const struct hantro_variant rk3399_vpu_variant = {
	.enc_offset = 0x0,
	.enc_fmts = rk3399_vpu_enc_fmts,
	.num_enc_fmts = ARRAY_SIZE(rk3399_vpu_enc_fmts),
	.dec_offset = 0x400,
	.dec_fmts = rk3399_vpu_dec_fmts,
	.num_dec_fmts = ARRAY_SIZE(rk3399_vpu_dec_fmts),
	.codec = HANTRO_JPEG_ENCODER | HANTRO_MPEG2_DECODER,
	.codec_ops = rk3399_vpu_codec_ops,
	.irqs = rk3399_irqs,
	.num_irqs = ARRAY_SIZE(rk3399_irqs),
	.init = rk3399_vpu_hw_init,
	.clk_names = rk3399_clk_names,
	.num_clocks = ARRAY_SIZE(rk3399_clk_names)
};
