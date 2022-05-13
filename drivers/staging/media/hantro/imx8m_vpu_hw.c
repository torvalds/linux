// SPDX-License-Identifier: GPL-2.0
/*
 * Hantro VPU codec driver
 *
 * Copyright (C) 2019 Pengutronix, Philipp Zabel <kernel@pengutronix.de>
 */

#include <linux/clk.h>
#include <linux/delay.h>

#include "hantro.h"
#include "hantro_jpeg.h"
#include "hantro_g1_regs.h"
#include "hantro_g2_regs.h"

#define CTRL_SOFT_RESET		0x00
#define RESET_G1		BIT(1)
#define RESET_G2		BIT(0)

#define CTRL_CLOCK_ENABLE	0x04
#define CLOCK_G1		BIT(1)
#define CLOCK_G2		BIT(0)

#define CTRL_G1_DEC_FUSE	0x08
#define CTRL_G1_PP_FUSE		0x0c
#define CTRL_G2_DEC_FUSE	0x10

static void imx8m_soft_reset(struct hantro_dev *vpu, u32 reset_bits)
{
	u32 val;

	/* Assert */
	val = readl(vpu->ctrl_base + CTRL_SOFT_RESET);
	val &= ~reset_bits;
	writel(val, vpu->ctrl_base + CTRL_SOFT_RESET);

	udelay(2);

	/* Release */
	val = readl(vpu->ctrl_base + CTRL_SOFT_RESET);
	val |= reset_bits;
	writel(val, vpu->ctrl_base + CTRL_SOFT_RESET);
}

static void imx8m_clk_enable(struct hantro_dev *vpu, u32 clock_bits)
{
	u32 val;

	val = readl(vpu->ctrl_base + CTRL_CLOCK_ENABLE);
	val |= clock_bits;
	writel(val, vpu->ctrl_base + CTRL_CLOCK_ENABLE);
}

static int imx8mq_runtime_resume(struct hantro_dev *vpu)
{
	int ret;

	ret = clk_bulk_prepare_enable(vpu->variant->num_clocks, vpu->clocks);
	if (ret) {
		dev_err(vpu->dev, "Failed to enable clocks\n");
		return ret;
	}

	imx8m_soft_reset(vpu, RESET_G1 | RESET_G2);
	imx8m_clk_enable(vpu, CLOCK_G1 | CLOCK_G2);

	/* Set values of the fuse registers */
	writel(0xffffffff, vpu->ctrl_base + CTRL_G1_DEC_FUSE);
	writel(0xffffffff, vpu->ctrl_base + CTRL_G1_PP_FUSE);
	writel(0xffffffff, vpu->ctrl_base + CTRL_G2_DEC_FUSE);

	clk_bulk_disable_unprepare(vpu->variant->num_clocks, vpu->clocks);

	return 0;
}

/*
 * Supported formats.
 */

static const struct hantro_fmt imx8m_vpu_postproc_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.codec_mode = HANTRO_MODE_NONE,
		.postprocessed = true,
	},
};

static const struct hantro_fmt imx8m_vpu_dec_fmts[] = {
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
	{
		.fourcc = V4L2_PIX_FMT_H264_SLICE,
		.codec_mode = HANTRO_MODE_H264_DEC,
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

static const struct hantro_fmt imx8m_vpu_g2_postproc_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.codec_mode = HANTRO_MODE_NONE,
		.postprocessed = true,
	},
};

static const struct hantro_fmt imx8m_vpu_g2_dec_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_NV12_4L4,
		.codec_mode = HANTRO_MODE_NONE,
	},
	{
		.fourcc = V4L2_PIX_FMT_HEVC_SLICE,
		.codec_mode = HANTRO_MODE_HEVC_DEC,
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
	{
		.fourcc = V4L2_PIX_FMT_VP9_FRAME,
		.codec_mode = HANTRO_MODE_VP9_DEC,
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

static irqreturn_t imx8m_vpu_g1_irq(int irq, void *dev_id)
{
	struct hantro_dev *vpu = dev_id;
	enum vb2_buffer_state state;
	u32 status;

	status = vdpu_read(vpu, G1_REG_INTERRUPT);
	state = (status & G1_REG_INTERRUPT_DEC_RDY_INT) ?
		 VB2_BUF_STATE_DONE : VB2_BUF_STATE_ERROR;

	vdpu_write(vpu, 0, G1_REG_INTERRUPT);
	vdpu_write(vpu, G1_REG_CONFIG_DEC_CLK_GATE_E, G1_REG_CONFIG);

	hantro_irq_done(vpu, state);

	return IRQ_HANDLED;
}

static int imx8mq_vpu_hw_init(struct hantro_dev *vpu)
{
	vpu->ctrl_base = vpu->reg_bases[vpu->variant->num_regs - 1];

	return 0;
}

static void imx8m_vpu_g1_reset(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;

	imx8m_soft_reset(vpu, RESET_G1);
}

/*
 * Supported codec ops.
 */

static const struct hantro_codec_ops imx8mq_vpu_codec_ops[] = {
	[HANTRO_MODE_MPEG2_DEC] = {
		.run = hantro_g1_mpeg2_dec_run,
		.reset = imx8m_vpu_g1_reset,
		.init = hantro_mpeg2_dec_init,
		.exit = hantro_mpeg2_dec_exit,
	},
	[HANTRO_MODE_VP8_DEC] = {
		.run = hantro_g1_vp8_dec_run,
		.reset = imx8m_vpu_g1_reset,
		.init = hantro_vp8_dec_init,
		.exit = hantro_vp8_dec_exit,
	},
	[HANTRO_MODE_H264_DEC] = {
		.run = hantro_g1_h264_dec_run,
		.reset = imx8m_vpu_g1_reset,
		.init = hantro_h264_dec_init,
		.exit = hantro_h264_dec_exit,
	},
};

static const struct hantro_codec_ops imx8mq_vpu_g1_codec_ops[] = {
	[HANTRO_MODE_MPEG2_DEC] = {
		.run = hantro_g1_mpeg2_dec_run,
		.init = hantro_mpeg2_dec_init,
		.exit = hantro_mpeg2_dec_exit,
	},
	[HANTRO_MODE_VP8_DEC] = {
		.run = hantro_g1_vp8_dec_run,
		.init = hantro_vp8_dec_init,
		.exit = hantro_vp8_dec_exit,
	},
	[HANTRO_MODE_H264_DEC] = {
		.run = hantro_g1_h264_dec_run,
		.init = hantro_h264_dec_init,
		.exit = hantro_h264_dec_exit,
	},
};

static const struct hantro_codec_ops imx8mq_vpu_g2_codec_ops[] = {
	[HANTRO_MODE_HEVC_DEC] = {
		.run = hantro_g2_hevc_dec_run,
		.init = hantro_hevc_dec_init,
		.exit = hantro_hevc_dec_exit,
	},
	[HANTRO_MODE_VP9_DEC] = {
		.run = hantro_g2_vp9_dec_run,
		.done = hantro_g2_vp9_dec_done,
		.init = hantro_vp9_dec_init,
		.exit = hantro_vp9_dec_exit,
	},
};

/*
 * VPU variants.
 */

static const struct hantro_irq imx8mq_irqs[] = {
	{ "g1", imx8m_vpu_g1_irq },
};

static const struct hantro_irq imx8mq_g2_irqs[] = {
	{ "g2", hantro_g2_irq },
};

static const char * const imx8mq_clk_names[] = { "g1", "g2", "bus" };
static const char * const imx8mq_reg_names[] = { "g1", "g2", "ctrl" };
static const char * const imx8mq_g1_clk_names[] = { "g1" };
static const char * const imx8mq_g2_clk_names[] = { "g2" };

const struct hantro_variant imx8mq_vpu_variant = {
	.dec_fmts = imx8m_vpu_dec_fmts,
	.num_dec_fmts = ARRAY_SIZE(imx8m_vpu_dec_fmts),
	.postproc_fmts = imx8m_vpu_postproc_fmts,
	.num_postproc_fmts = ARRAY_SIZE(imx8m_vpu_postproc_fmts),
	.postproc_ops = &hantro_g1_postproc_ops,
	.codec = HANTRO_MPEG2_DECODER | HANTRO_VP8_DECODER |
		 HANTRO_H264_DECODER,
	.codec_ops = imx8mq_vpu_codec_ops,
	.init = imx8mq_vpu_hw_init,
	.runtime_resume = imx8mq_runtime_resume,
	.irqs = imx8mq_irqs,
	.num_irqs = ARRAY_SIZE(imx8mq_irqs),
	.clk_names = imx8mq_clk_names,
	.num_clocks = ARRAY_SIZE(imx8mq_clk_names),
	.reg_names = imx8mq_reg_names,
	.num_regs = ARRAY_SIZE(imx8mq_reg_names)
};

const struct hantro_variant imx8mq_vpu_g1_variant = {
	.dec_fmts = imx8m_vpu_dec_fmts,
	.num_dec_fmts = ARRAY_SIZE(imx8m_vpu_dec_fmts),
	.postproc_fmts = imx8m_vpu_postproc_fmts,
	.num_postproc_fmts = ARRAY_SIZE(imx8m_vpu_postproc_fmts),
	.postproc_ops = &hantro_g1_postproc_ops,
	.codec = HANTRO_MPEG2_DECODER | HANTRO_VP8_DECODER |
		 HANTRO_H264_DECODER,
	.codec_ops = imx8mq_vpu_g1_codec_ops,
	.irqs = imx8mq_irqs,
	.num_irqs = ARRAY_SIZE(imx8mq_irqs),
	.clk_names = imx8mq_g1_clk_names,
	.num_clocks = ARRAY_SIZE(imx8mq_g1_clk_names),
};

const struct hantro_variant imx8mq_vpu_g2_variant = {
	.dec_offset = 0x0,
	.dec_fmts = imx8m_vpu_g2_dec_fmts,
	.num_dec_fmts = ARRAY_SIZE(imx8m_vpu_g2_dec_fmts),
	.postproc_fmts = imx8m_vpu_g2_postproc_fmts,
	.num_postproc_fmts = ARRAY_SIZE(imx8m_vpu_g2_postproc_fmts),
	.postproc_ops = &hantro_g2_postproc_ops,
	.codec = HANTRO_HEVC_DECODER | HANTRO_VP9_DECODER,
	.codec_ops = imx8mq_vpu_g2_codec_ops,
	.irqs = imx8mq_g2_irqs,
	.num_irqs = ARRAY_SIZE(imx8mq_g2_irqs),
	.clk_names = imx8mq_g2_clk_names,
	.num_clocks = ARRAY_SIZE(imx8mq_g2_clk_names),
};

const struct hantro_variant imx8mm_vpu_g1_variant = {
	.dec_fmts = imx8m_vpu_dec_fmts,
	.num_dec_fmts = ARRAY_SIZE(imx8m_vpu_dec_fmts),
	.codec = HANTRO_MPEG2_DECODER | HANTRO_VP8_DECODER |
		 HANTRO_H264_DECODER,
	.codec_ops = imx8mq_vpu_g1_codec_ops,
	.irqs = imx8mq_irqs,
	.num_irqs = ARRAY_SIZE(imx8mq_irqs),
	.clk_names = imx8mq_g1_clk_names,
	.num_clocks = ARRAY_SIZE(imx8mq_g1_clk_names),
};
