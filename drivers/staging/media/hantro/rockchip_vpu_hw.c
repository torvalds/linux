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
#include "hantro_g1_regs.h"
#include "hantro_h1_regs.h"
#include "rockchip_vpu2_regs.h"

#define RK3066_ACLK_MAX_FREQ (300 * 1000 * 1000)
#define RK3288_ACLK_MAX_FREQ (400 * 1000 * 1000)

/*
 * Supported formats.
 */

static const struct hantro_fmt rockchip_vpu_enc_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_YUV420M,
		.codec_mode = HANTRO_MODE_NONE,
		.enc_fmt = ROCKCHIP_VPU_ENC_FMT_YUV420P,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV12M,
		.codec_mode = HANTRO_MODE_NONE,
		.enc_fmt = ROCKCHIP_VPU_ENC_FMT_YUV420SP,
	},
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.codec_mode = HANTRO_MODE_NONE,
		.enc_fmt = ROCKCHIP_VPU_ENC_FMT_YUYV422,
	},
	{
		.fourcc = V4L2_PIX_FMT_UYVY,
		.codec_mode = HANTRO_MODE_NONE,
		.enc_fmt = ROCKCHIP_VPU_ENC_FMT_UYVY422,
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

static const struct hantro_fmt rockchip_vpu1_postproc_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.codec_mode = HANTRO_MODE_NONE,
		.postprocessed = true,
	},
};

static const struct hantro_fmt rk3066_vpu_dec_fmts[] = {
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
			.max_width = 1920,
			.step_width = MB_DIM,
			.min_height = 48,
			.max_height = 1088,
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
			.max_width = 1920,
			.step_width = MB_DIM,
			.min_height = 48,
			.max_height = 1088,
			.step_height = MB_DIM,
		},
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

static const struct hantro_fmt rk3399_vpu_dec_fmts[] = {
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
			.max_width = 1920,
			.step_width = MB_DIM,
			.min_height = 48,
			.max_height = 1088,
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

static irqreturn_t rockchip_vpu1_vepu_irq(int irq, void *dev_id)
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

static irqreturn_t rockchip_vpu2_vdpu_irq(int irq, void *dev_id)
{
	struct hantro_dev *vpu = dev_id;
	enum vb2_buffer_state state;
	u32 status;

	status = vdpu_read(vpu, VDPU_REG_INTERRUPT);
	state = (status & VDPU_REG_INTERRUPT_DEC_IRQ) ?
		VB2_BUF_STATE_DONE : VB2_BUF_STATE_ERROR;

	vdpu_write(vpu, 0, VDPU_REG_INTERRUPT);
	vdpu_write(vpu, 0, VDPU_REG_AXI_CTRL);

	hantro_irq_done(vpu, state);

	return IRQ_HANDLED;
}

static irqreturn_t rockchip_vpu2_vepu_irq(int irq, void *dev_id)
{
	struct hantro_dev *vpu = dev_id;
	enum vb2_buffer_state state;
	u32 status;

	status = vepu_read(vpu, VEPU_REG_INTERRUPT);
	state = (status & VEPU_REG_INTERRUPT_FRAME_READY) ?
		VB2_BUF_STATE_DONE : VB2_BUF_STATE_ERROR;

	vepu_write(vpu, 0, VEPU_REG_INTERRUPT);
	vepu_write(vpu, 0, VEPU_REG_AXI_CTRL);

	hantro_irq_done(vpu, state);

	return IRQ_HANDLED;
}

static int rk3036_vpu_hw_init(struct hantro_dev *vpu)
{
	/* Bump ACLK to max. possible freq. to improve performance. */
	clk_set_rate(vpu->clocks[0].clk, RK3066_ACLK_MAX_FREQ);
	return 0;
}

static int rk3066_vpu_hw_init(struct hantro_dev *vpu)
{
	/* Bump ACLKs to max. possible freq. to improve performance. */
	clk_set_rate(vpu->clocks[0].clk, RK3066_ACLK_MAX_FREQ);
	clk_set_rate(vpu->clocks[2].clk, RK3066_ACLK_MAX_FREQ);
	return 0;
}

static int rockchip_vpu_hw_init(struct hantro_dev *vpu)
{
	/* Bump ACLK to max. possible freq. to improve performance. */
	clk_set_rate(vpu->clocks[0].clk, RK3288_ACLK_MAX_FREQ);
	return 0;
}

static void rk3066_vpu_dec_reset(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;

	vdpu_write(vpu, G1_REG_INTERRUPT_DEC_IRQ_DIS, G1_REG_INTERRUPT);
	vdpu_write(vpu, G1_REG_CONFIG_DEC_CLK_GATE_E, G1_REG_CONFIG);
}

static void rockchip_vpu1_enc_reset(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;

	vepu_write(vpu, H1_REG_INTERRUPT_DIS_BIT, H1_REG_INTERRUPT);
	vepu_write(vpu, 0, H1_REG_ENC_CTRL);
	vepu_write(vpu, 0, H1_REG_AXI_CTRL);
}

static void rockchip_vpu2_dec_reset(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;

	vdpu_write(vpu, VDPU_REG_INTERRUPT_DEC_IRQ_DIS, VDPU_REG_INTERRUPT);
	vdpu_write(vpu, 0, VDPU_REG_EN_FLAGS);
	vdpu_write(vpu, 1, VDPU_REG_SOFT_RESET);
}

static void rockchip_vpu2_enc_reset(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;

	vepu_write(vpu, VEPU_REG_INTERRUPT_DIS_BIT, VEPU_REG_INTERRUPT);
	vepu_write(vpu, 0, VEPU_REG_ENCODE_START);
	vepu_write(vpu, 0, VEPU_REG_AXI_CTRL);
}

/*
 * Supported codec ops.
 */
static const struct hantro_codec_ops rk3036_vpu_codec_ops[] = {
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

static const struct hantro_codec_ops rk3066_vpu_codec_ops[] = {
	[HANTRO_MODE_JPEG_ENC] = {
		.run = hantro_h1_jpeg_enc_run,
		.reset = rockchip_vpu1_enc_reset,
		.init = hantro_jpeg_enc_init,
		.done = hantro_h1_jpeg_enc_done,
		.exit = hantro_jpeg_enc_exit,
	},
	[HANTRO_MODE_H264_DEC] = {
		.run = hantro_g1_h264_dec_run,
		.reset = rk3066_vpu_dec_reset,
		.init = hantro_h264_dec_init,
		.exit = hantro_h264_dec_exit,
	},
	[HANTRO_MODE_MPEG2_DEC] = {
		.run = hantro_g1_mpeg2_dec_run,
		.reset = rk3066_vpu_dec_reset,
		.init = hantro_mpeg2_dec_init,
		.exit = hantro_mpeg2_dec_exit,
	},
	[HANTRO_MODE_VP8_DEC] = {
		.run = hantro_g1_vp8_dec_run,
		.reset = rk3066_vpu_dec_reset,
		.init = hantro_vp8_dec_init,
		.exit = hantro_vp8_dec_exit,
	},
};

static const struct hantro_codec_ops rk3288_vpu_codec_ops[] = {
	[HANTRO_MODE_JPEG_ENC] = {
		.run = hantro_h1_jpeg_enc_run,
		.reset = rockchip_vpu1_enc_reset,
		.init = hantro_jpeg_enc_init,
		.done = hantro_h1_jpeg_enc_done,
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

static const struct hantro_codec_ops rk3399_vpu_codec_ops[] = {
	[HANTRO_MODE_JPEG_ENC] = {
		.run = rockchip_vpu2_jpeg_enc_run,
		.reset = rockchip_vpu2_enc_reset,
		.init = hantro_jpeg_enc_init,
		.done = rockchip_vpu2_jpeg_enc_done,
		.exit = hantro_jpeg_enc_exit,
	},
	[HANTRO_MODE_H264_DEC] = {
		.run = rockchip_vpu2_h264_dec_run,
		.reset = rockchip_vpu2_dec_reset,
		.init = hantro_h264_dec_init,
		.exit = hantro_h264_dec_exit,
	},
	[HANTRO_MODE_MPEG2_DEC] = {
		.run = rockchip_vpu2_mpeg2_dec_run,
		.reset = rockchip_vpu2_dec_reset,
		.init = hantro_mpeg2_dec_init,
		.exit = hantro_mpeg2_dec_exit,
	},
	[HANTRO_MODE_VP8_DEC] = {
		.run = rockchip_vpu2_vp8_dec_run,
		.reset = rockchip_vpu2_dec_reset,
		.init = hantro_vp8_dec_init,
		.exit = hantro_vp8_dec_exit,
	},
};

/*
 * VPU variant.
 */

static const struct hantro_irq rockchip_vdpu1_irqs[] = {
	{ "vdpu", hantro_g1_irq },
};

static const struct hantro_irq rockchip_vpu1_irqs[] = {
	{ "vepu", rockchip_vpu1_vepu_irq },
	{ "vdpu", hantro_g1_irq },
};

static const struct hantro_irq rockchip_vdpu2_irqs[] = {
	{ "vdpu", rockchip_vpu2_vdpu_irq },
};

static const struct hantro_irq rockchip_vpu2_irqs[] = {
	{ "vepu", rockchip_vpu2_vepu_irq },
	{ "vdpu", rockchip_vpu2_vdpu_irq },
};

static const char * const rk3066_vpu_clk_names[] = {
	"aclk_vdpu", "hclk_vdpu",
	"aclk_vepu", "hclk_vepu"
};

static const char * const rockchip_vpu_clk_names[] = {
	"aclk", "hclk"
};

/* VDPU1/VEPU1 */

const struct hantro_variant rk3036_vpu_variant = {
	.dec_offset = 0x400,
	.dec_fmts = rk3066_vpu_dec_fmts,
	.num_dec_fmts = ARRAY_SIZE(rk3066_vpu_dec_fmts),
	.postproc_fmts = rockchip_vpu1_postproc_fmts,
	.num_postproc_fmts = ARRAY_SIZE(rockchip_vpu1_postproc_fmts),
	.postproc_regs = &hantro_g1_postproc_regs,
	.codec = HANTRO_MPEG2_DECODER | HANTRO_VP8_DECODER |
		 HANTRO_H264_DECODER,
	.codec_ops = rk3036_vpu_codec_ops,
	.irqs = rockchip_vdpu1_irqs,
	.num_irqs = ARRAY_SIZE(rockchip_vdpu1_irqs),
	.init = rk3036_vpu_hw_init,
	.clk_names = rockchip_vpu_clk_names,
	.num_clocks = ARRAY_SIZE(rockchip_vpu_clk_names)
};

/*
 * Despite this variant has separate clocks for decoder and encoder,
 * it's still required to enable all four of them for either decoding
 * or encoding and we can't split it in separate g1/h1 variants.
 */
const struct hantro_variant rk3066_vpu_variant = {
	.enc_offset = 0x0,
	.enc_fmts = rockchip_vpu_enc_fmts,
	.num_enc_fmts = ARRAY_SIZE(rockchip_vpu_enc_fmts),
	.dec_offset = 0x400,
	.dec_fmts = rk3066_vpu_dec_fmts,
	.num_dec_fmts = ARRAY_SIZE(rk3066_vpu_dec_fmts),
	.postproc_fmts = rockchip_vpu1_postproc_fmts,
	.num_postproc_fmts = ARRAY_SIZE(rockchip_vpu1_postproc_fmts),
	.postproc_regs = &hantro_g1_postproc_regs,
	.codec = HANTRO_JPEG_ENCODER | HANTRO_MPEG2_DECODER |
		 HANTRO_VP8_DECODER | HANTRO_H264_DECODER,
	.codec_ops = rk3066_vpu_codec_ops,
	.irqs = rockchip_vpu1_irqs,
	.num_irqs = ARRAY_SIZE(rockchip_vpu1_irqs),
	.init = rk3066_vpu_hw_init,
	.clk_names = rk3066_vpu_clk_names,
	.num_clocks = ARRAY_SIZE(rk3066_vpu_clk_names)
};

const struct hantro_variant rk3288_vpu_variant = {
	.enc_offset = 0x0,
	.enc_fmts = rockchip_vpu_enc_fmts,
	.num_enc_fmts = ARRAY_SIZE(rockchip_vpu_enc_fmts),
	.dec_offset = 0x400,
	.dec_fmts = rk3288_vpu_dec_fmts,
	.num_dec_fmts = ARRAY_SIZE(rk3288_vpu_dec_fmts),
	.postproc_fmts = rockchip_vpu1_postproc_fmts,
	.num_postproc_fmts = ARRAY_SIZE(rockchip_vpu1_postproc_fmts),
	.postproc_regs = &hantro_g1_postproc_regs,
	.codec = HANTRO_JPEG_ENCODER | HANTRO_MPEG2_DECODER |
		 HANTRO_VP8_DECODER | HANTRO_H264_DECODER,
	.codec_ops = rk3288_vpu_codec_ops,
	.irqs = rockchip_vpu1_irqs,
	.num_irqs = ARRAY_SIZE(rockchip_vpu1_irqs),
	.init = rockchip_vpu_hw_init,
	.clk_names = rockchip_vpu_clk_names,
	.num_clocks = ARRAY_SIZE(rockchip_vpu_clk_names)
};

/* VDPU2/VEPU2 */

const struct hantro_variant rk3328_vpu_variant = {
	.dec_offset = 0x400,
	.dec_fmts = rk3399_vpu_dec_fmts,
	.num_dec_fmts = ARRAY_SIZE(rk3399_vpu_dec_fmts),
	.codec = HANTRO_MPEG2_DECODER | HANTRO_VP8_DECODER |
		 HANTRO_H264_DECODER,
	.codec_ops = rk3399_vpu_codec_ops,
	.irqs = rockchip_vdpu2_irqs,
	.num_irqs = ARRAY_SIZE(rockchip_vdpu2_irqs),
	.init = rockchip_vpu_hw_init,
	.clk_names = rockchip_vpu_clk_names,
	.num_clocks = ARRAY_SIZE(rockchip_vpu_clk_names),
};

const struct hantro_variant rk3399_vpu_variant = {
	.enc_offset = 0x0,
	.enc_fmts = rockchip_vpu_enc_fmts,
	.num_enc_fmts = ARRAY_SIZE(rockchip_vpu_enc_fmts),
	.dec_offset = 0x400,
	.dec_fmts = rk3399_vpu_dec_fmts,
	.num_dec_fmts = ARRAY_SIZE(rk3399_vpu_dec_fmts),
	.codec = HANTRO_JPEG_ENCODER | HANTRO_MPEG2_DECODER |
		 HANTRO_VP8_DECODER,
	.codec_ops = rk3399_vpu_codec_ops,
	.irqs = rockchip_vpu2_irqs,
	.num_irqs = ARRAY_SIZE(rockchip_vpu2_irqs),
	.init = rockchip_vpu_hw_init,
	.clk_names = rockchip_vpu_clk_names,
	.num_clocks = ARRAY_SIZE(rockchip_vpu_clk_names)
};

const struct hantro_variant px30_vpu_variant = {
	.enc_offset = 0x0,
	.enc_fmts = rockchip_vpu_enc_fmts,
	.num_enc_fmts = ARRAY_SIZE(rockchip_vpu_enc_fmts),
	.dec_offset = 0x400,
	.dec_fmts = rk3399_vpu_dec_fmts,
	.num_dec_fmts = ARRAY_SIZE(rk3399_vpu_dec_fmts),
	.codec = HANTRO_JPEG_ENCODER | HANTRO_MPEG2_DECODER |
		 HANTRO_VP8_DECODER | HANTRO_H264_DECODER,
	.codec_ops = rk3399_vpu_codec_ops,
	.irqs = rockchip_vpu2_irqs,
	.num_irqs = ARRAY_SIZE(rockchip_vpu2_irqs),
	.init = rk3036_vpu_hw_init,
	.clk_names = rockchip_vpu_clk_names,
	.num_clocks = ARRAY_SIZE(rockchip_vpu_clk_names)
};
