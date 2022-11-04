// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner Hantro G2 VPU codec driver
 *
 * Copyright (C) 2021 Jernej Skrabec <jernej.skrabec@gmail.com>
 */

#include <linux/clk.h>

#include "hantro.h"

static const struct hantro_fmt sunxi_vpu_postproc_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.codec_mode = HANTRO_MODE_NONE,
		.postprocessed = true,
		.frmsize = {
			.min_width = FMT_MIN_WIDTH,
			.max_width = FMT_UHD_WIDTH,
			.step_width = 32,
			.min_height = FMT_MIN_HEIGHT,
			.max_height = FMT_UHD_HEIGHT,
			.step_height = 32,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_P010,
		.codec_mode = HANTRO_MODE_NONE,
		.postprocessed = true,
		.frmsize = {
			.min_width = FMT_MIN_WIDTH,
			.max_width = FMT_UHD_WIDTH,
			.step_width = 32,
			.min_height = FMT_MIN_HEIGHT,
			.max_height = FMT_UHD_HEIGHT,
			.step_height = 32,
		},
	},
};

static const struct hantro_fmt sunxi_vpu_dec_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_NV12_4L4,
		.codec_mode = HANTRO_MODE_NONE,
		.match_depth = true,
		.frmsize = {
			.min_width = FMT_MIN_WIDTH,
			.max_width = FMT_UHD_WIDTH,
			.step_width = 32,
			.min_height = FMT_MIN_HEIGHT,
			.max_height = FMT_UHD_HEIGHT,
			.step_height = 32,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_P010_4L4,
		.codec_mode = HANTRO_MODE_NONE,
		.match_depth = true,
		.frmsize = {
			.min_width = FMT_MIN_WIDTH,
			.max_width = FMT_UHD_WIDTH,
			.step_width = 32,
			.min_height = FMT_MIN_HEIGHT,
			.max_height = FMT_UHD_HEIGHT,
			.step_height = 32,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_VP9_FRAME,
		.codec_mode = HANTRO_MODE_VP9_DEC,
		.max_depth = 2,
		.frmsize = {
			.min_width = FMT_MIN_WIDTH,
			.max_width = FMT_UHD_WIDTH,
			.step_width = 32,
			.min_height = FMT_MIN_HEIGHT,
			.max_height = FMT_UHD_HEIGHT,
			.step_height = 32,
		},
	},
};

static int sunxi_vpu_hw_init(struct hantro_dev *vpu)
{
	clk_set_rate(vpu->clocks[0].clk, 300000000);

	return 0;
}

static void sunxi_vpu_reset(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;

	reset_control_reset(vpu->resets);
}

static const struct hantro_codec_ops sunxi_vpu_codec_ops[] = {
	[HANTRO_MODE_VP9_DEC] = {
		.run = hantro_g2_vp9_dec_run,
		.done = hantro_g2_vp9_dec_done,
		.reset = sunxi_vpu_reset,
		.init = hantro_vp9_dec_init,
		.exit = hantro_vp9_dec_exit,
	},
};

static const struct hantro_irq sunxi_irqs[] = {
	{ NULL, hantro_g2_irq },
};

static const char * const sunxi_clk_names[] = { "mod", "bus" };

const struct hantro_variant sunxi_vpu_variant = {
	.dec_fmts = sunxi_vpu_dec_fmts,
	.num_dec_fmts = ARRAY_SIZE(sunxi_vpu_dec_fmts),
	.postproc_fmts = sunxi_vpu_postproc_fmts,
	.num_postproc_fmts = ARRAY_SIZE(sunxi_vpu_postproc_fmts),
	.postproc_ops = &hantro_g2_postproc_ops,
	.codec = HANTRO_VP9_DECODER,
	.codec_ops = sunxi_vpu_codec_ops,
	.init = sunxi_vpu_hw_init,
	.irqs = sunxi_irqs,
	.num_irqs = ARRAY_SIZE(sunxi_irqs),
	.clk_names = sunxi_clk_names,
	.num_clocks = ARRAY_SIZE(sunxi_clk_names),
	.double_buffer = 1,
	.legacy_regs = 1,
	.late_postproc = 1,
};
