// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip VPU codec driver
 *
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 *	Jeffy Chen <jeffy.chen@rock-chips.com>
 */

#include <linux/clk.h>

#include "rockchip_vpu.h"
#include "rockchip_vpu_jpeg.h"
#include "rk3288_vpu_regs.h"

#define RK3288_ACLK_MAX_FREQ (400 * 1000 * 1000)

/*
 * Supported formats.
 */

static const struct rockchip_vpu_fmt rk3288_vpu_enc_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_YUV420M,
		.codec_mode = RK_VPU_MODE_NONE,
		.enc_fmt = RK3288_VPU_ENC_FMT_YUV420P,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV12M,
		.codec_mode = RK_VPU_MODE_NONE,
		.enc_fmt = RK3288_VPU_ENC_FMT_YUV420SP,
	},
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.codec_mode = RK_VPU_MODE_NONE,
		.enc_fmt = RK3288_VPU_ENC_FMT_YUYV422,
	},
	{
		.fourcc = V4L2_PIX_FMT_UYVY,
		.codec_mode = RK_VPU_MODE_NONE,
		.enc_fmt = RK3288_VPU_ENC_FMT_UYVY422,
	},
	{
		.fourcc = V4L2_PIX_FMT_JPEG,
		.codec_mode = RK_VPU_MODE_JPEG_ENC,
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

static irqreturn_t rk3288_vepu_irq(int irq, void *dev_id)
{
	struct rockchip_vpu_dev *vpu = dev_id;
	enum vb2_buffer_state state;
	u32 status, bytesused;

	status = vepu_read(vpu, VEPU_REG_INTERRUPT);
	bytesused = vepu_read(vpu, VEPU_REG_STR_BUF_LIMIT) / 8;
	state = (status & VEPU_REG_INTERRUPT_FRAME_RDY) ?
		VB2_BUF_STATE_DONE : VB2_BUF_STATE_ERROR;

	vepu_write(vpu, 0, VEPU_REG_INTERRUPT);
	vepu_write(vpu, 0, VEPU_REG_AXI_CTRL);

	rockchip_vpu_irq_done(vpu, bytesused, state);

	return IRQ_HANDLED;
}

static int rk3288_vpu_hw_init(struct rockchip_vpu_dev *vpu)
{
	/* Bump ACLK to max. possible freq. to improve performance. */
	clk_set_rate(vpu->clocks[0].clk, RK3288_ACLK_MAX_FREQ);
	return 0;
}

static void rk3288_vpu_enc_reset(struct rockchip_vpu_ctx *ctx)
{
	struct rockchip_vpu_dev *vpu = ctx->dev;

	vepu_write(vpu, VEPU_REG_INTERRUPT_DIS_BIT, VEPU_REG_INTERRUPT);
	vepu_write(vpu, 0, VEPU_REG_ENC_CTRL);
	vepu_write(vpu, 0, VEPU_REG_AXI_CTRL);
}

/*
 * Supported codec ops.
 */

static const struct rockchip_vpu_codec_ops rk3288_vpu_codec_ops[] = {
	[RK_VPU_MODE_JPEG_ENC] = {
		.run = rk3288_vpu_jpeg_enc_run,
		.reset = rk3288_vpu_enc_reset,
	},
};

/*
 * VPU variant.
 */

const struct rockchip_vpu_variant rk3288_vpu_variant = {
	.enc_offset = 0x0,
	.enc_fmts = rk3288_vpu_enc_fmts,
	.num_enc_fmts = ARRAY_SIZE(rk3288_vpu_enc_fmts),
	.codec_ops = rk3288_vpu_codec_ops,
	.codec = RK_VPU_CODEC_JPEG,
	.vepu_irq = rk3288_vepu_irq,
	.init = rk3288_vpu_hw_init,
	.clk_names = {"aclk", "hclk"},
	.num_clocks = 2
};
