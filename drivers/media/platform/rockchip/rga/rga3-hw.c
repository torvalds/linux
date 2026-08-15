// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025-2026 Pengutronix e.K.
 * Author: Sven Püschel <s.pueschel@pengutronix.de>
 */

#include <linux/pm_runtime.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/printk.h>

#include <media/v4l2-common.h>

#include "rga3-hw.h"
#include "rga.h"

static unsigned int rga3_get_scaling(unsigned int src, unsigned int dst)
{
	/*
	 * RGA3 scaling factor calculation as described in chapter 5.4.7 Resize
	 * of the TRM Part 2. The resulting scaling factor is a 16-bit value
	 * and therefore normalized with 2^16.
	 *
	 * While the TRM also mentions (dst-1)/(src-1) for the up-scaling case,
	 * it didn't work as the value always exceeds 16 bit. Flipping the
	 * factors results in a correct up-scaling. This is possible as the
	 * RGA3 has the RGA3_WIN_SCALE_XXX_UP bit to determine if it does
	 * an up or downscale.
	 *
	 * The scaling factor can potentially cause a slightly larger scaling
	 * (e.g. 1/2px larger scale and then cropped to the destination size).
	 * This can be seen when scaling 128x128px RGBA to 256x256px RGBA.
	 * The RGA2 scaling factor calculation (without the various +/-1
	 * doesn't work for the RGA3. It's assumed that this is an hardware
	 * accuracy limitation, as the vendor kernel driver uses the same
	 * scaling factor calculation.
	 *
	 * With a scaling factor of 1.0 the calculation technically also
	 * overflows 16 bit. This isn't relevant, as in this case the
	 * RGA3_WIN_SCALE_XXX_BYPASS bit completely skips the scaling operation.
	 */
	if (dst > src) {
		if (((src - 1) << 16) % (dst - 1) == 0)
			return ((src - 1) << 16) / (dst - 1) - 1;
		else
			return ((src - 1) << 16) / (dst - 1);
	} else {
		return ((dst - 1) << 16) / (src - 1) + 1;
	}
}

/*
 * Check if the given format can be captured, as the RGA3 doesn't support all
 * input formats also on it's output.
 */
static bool rga3_can_capture(const struct rga3_fmt *fmt)
{
	return fmt->hw_format <= RGA3_COLOR_FMT_LAST_OUTPUT;
}

/*
 * Map the transformations to the RGA3 command buffer.
 * Currently this is just the scaling settings and a fixed alpha value.
 */
static void rga3_cmd_set_trans_info(struct rga_ctx *ctx)
{
	u32 *cmd = ctx->cmdbuf_virt;
	unsigned int src_h, src_w, dst_h, dst_w;
	unsigned int reg;
	u16 hor_scl_fac, ver_scl_fac;
	const struct rga3_fmt *in = ctx->in.fmt;

	/* Support basic input cropping to support 1088px inputs */
	src_h = ctx->in.crop.height;
	src_w = ctx->in.crop.width;
	dst_h = ctx->out.pix.height;
	dst_w = ctx->out.pix.width;

	reg = RGA3_WIN0_RD_CTRL - RGA3_FIRST_CMD_REG;
	cmd[reg >> 2] |= FIELD_PREP(RGA3_WIN_SCALE_HOR_UP, dst_w > src_w)
		      |  FIELD_PREP(RGA3_WIN_SCALE_HOR_BYPASS, dst_w == src_w)
		      |  FIELD_PREP(RGA3_WIN_SCALE_VER_UP, dst_h > src_h)
		      |  FIELD_PREP(RGA3_WIN_SCALE_VER_BYPASS, dst_h == src_h);

	hor_scl_fac = rga3_get_scaling(src_w, dst_w);
	ver_scl_fac = rga3_get_scaling(src_h, dst_h);
	reg = RGA3_WIN0_SCL_FAC - RGA3_FIRST_CMD_REG;
	cmd[reg >> 2] = FIELD_PREP(RGA3_SCALE_HOR_FAC, hor_scl_fac)
		      | FIELD_PREP(RGA3_SCALE_VER_FAC, ver_scl_fac);

	if (v4l2_format_info(in->fourcc)->has_alpha) {
		/* copy alpha from input */
		reg = RGA3_OVLP_TOP_ALPHA - RGA3_FIRST_CMD_REG;
		cmd[reg >> 2] = FIELD_PREP(RGA3_ALPHA_SELECT_MODE, 1)
			      | FIELD_PREP(RGA3_ALPHA_BLEND_MODE, 1);
		reg = RGA3_OVLP_BOT_ALPHA - RGA3_FIRST_CMD_REG;
		cmd[reg >> 2] = FIELD_PREP(RGA3_ALPHA_SELECT_MODE, 1)
			      | FIELD_PREP(RGA3_ALPHA_BLEND_MODE, 1);
	} else {
		/* just use a 255 alpha value */
		reg = RGA3_OVLP_TOP_CTRL - RGA3_FIRST_CMD_REG;
		cmd[reg >> 2] = FIELD_PREP(RGA3_OVLP_GLOBAL_ALPHA, 0xff)
			      | FIELD_PREP(RGA3_OVLP_COLOR_MODE, 1);
		reg = RGA3_OVLP_BOT_CTRL - RGA3_FIRST_CMD_REG;
		cmd[reg >> 2] = FIELD_PREP(RGA3_OVLP_GLOBAL_ALPHA, 0xff)
			      | FIELD_PREP(RGA3_OVLP_COLOR_MODE, 1);
	}
}

static void rga3_cmd_set_win0_addr(struct rga_ctx *ctx,
				   const struct rga_addrs *addrs)
{
	u32 *cmd = ctx->cmdbuf_virt;
	unsigned int reg;

	reg = RGA3_WIN0_Y_BASE - RGA3_FIRST_CMD_REG;
	cmd[reg >> 2] = addrs->y_addr;
	reg = RGA3_WIN0_U_BASE - RGA3_FIRST_CMD_REG;
	cmd[reg >> 2] = addrs->u_addr;
}

static void rga3_cmd_set_wr_addr(struct rga_ctx *ctx,
				 const struct rga_addrs *addrs)
{
	u32 *cmd = ctx->cmdbuf_virt;
	unsigned int reg;

	reg = RGA3_WR_Y_BASE - RGA3_FIRST_CMD_REG;
	cmd[reg >> 2] = addrs->y_addr;
	reg = RGA3_WR_U_BASE - RGA3_FIRST_CMD_REG;
	cmd[reg >> 2] = addrs->u_addr;
}

/* Map the input pixel format to win0 of the comamnd buffer. */
static void rga3_cmd_set_win0_format(struct rga_ctx *ctx)
{
	u32 *cmd = ctx->cmdbuf_virt;
	const struct rga3_fmt *in = ctx->in.fmt;
	const struct rga3_fmt *out = ctx->out.fmt;
	const struct v4l2_format_info *in_fmt, *out_fmt;
	unsigned int act_h, act_w, src_h, src_w;
	bool r2y, y2r;
	u8 rd_format;
	const struct v4l2_pix_format_mplane *csc_pix;
	u8 csc_mode;
	unsigned int reg;

	act_h = ctx->in.pix.height;
	act_w = ctx->in.pix.width;
	/* Support basic input cropping to support 1088px inputs */
	src_h = ctx->in.crop.height;
	src_w = ctx->in.crop.width;

	in_fmt = v4l2_format_info(in->fourcc);
	out_fmt = v4l2_format_info(out->fourcc);
	r2y = v4l2_is_format_rgb(in_fmt) && v4l2_is_format_yuv(out_fmt);
	y2r = v4l2_is_format_yuv(in_fmt) && v4l2_is_format_rgb(out_fmt);

	/* The Hardware only supports formats with 1/2 planes */
	if (in_fmt->comp_planes == 2)
		rd_format = RGA3_RDWR_FORMAT_SEMI_PLANAR;
	else
		rd_format = RGA3_RDWR_FORMAT_INTERLEAVED;

	/* set pixel format and CSC */
	csc_pix = r2y ? &ctx->out.pix : &ctx->in.pix;
	switch (csc_pix->ycbcr_enc) {
	case V4L2_YCBCR_ENC_BT2020:
		csc_mode = RGA3_WIN_CSC_MODE_BT2020_L;
		break;
	case V4L2_YCBCR_ENC_709:
		csc_mode = RGA3_WIN_CSC_MODE_BT709_L;
		break;
	default: /* should be fixed to BT601 in adjust_and_map_format */
		if (csc_pix->quantization == V4L2_QUANTIZATION_LIM_RANGE)
			csc_mode = RGA3_WIN_CSC_MODE_BT601_L;
		else
			csc_mode = RGA3_WIN_CSC_MODE_BT601_F;
		break;
	}

	reg = RGA3_WIN0_RD_CTRL - RGA3_FIRST_CMD_REG;
	cmd[reg >> 2] |= FIELD_PREP(RGA3_WIN_ENABLE, 1)
		      |  FIELD_PREP(RGA3_WIN_PIC_FORMAT, in->hw_format)
		      |  FIELD_PREP(RGA3_WIN_YC_SWAP, in->yc_swap)
		      |  FIELD_PREP(RGA3_WIN_RBUV_SWAP, in->rbuv_swap)
		      |  FIELD_PREP(RGA3_WIN_RD_FORMAT, rd_format)
		      |  FIELD_PREP(RGA3_WIN_R2Y, r2y)
		      |  FIELD_PREP(RGA3_WIN_Y2R, y2r)
		      |  FIELD_PREP(RGA3_WIN_CSC_MODE, csc_mode);

	/* set stride */
	reg = RGA3_WIN0_VIR_STRIDE - RGA3_FIRST_CMD_REG;
	/* stride needs to be in words */
	cmd[reg >> 2] = ctx->in.pix.plane_fmt[0].bytesperline >> 2;
	reg = RGA3_WIN0_UV_VIR_STRIDE - RGA3_FIRST_CMD_REG;
	/* The Hardware only supports formats with 1/2 planes */
	if (ctx->in.pix.num_planes == 2)
		cmd[reg >> 2] = ctx->in.pix.plane_fmt[1].bytesperline >> 2;
	else
		cmd[reg >> 2] = ctx->in.pix.plane_fmt[0].bytesperline >> 2;

	/* set size */
	reg = RGA3_WIN0_ACT_SIZE - RGA3_FIRST_CMD_REG;
	cmd[reg >> 2] = FIELD_PREP(RGA3_WIDTH, act_w)
		      | FIELD_PREP(RGA3_HEIGHT, act_h);
	reg = RGA3_WIN0_SRC_SIZE - RGA3_FIRST_CMD_REG;
	cmd[reg >> 2] = FIELD_PREP(RGA3_WIDTH, src_w)
		      | FIELD_PREP(RGA3_HEIGHT, src_h);
}

/* Map the output pixel format to the command buffer */
static void rga3_cmd_set_wr_format(struct rga_ctx *ctx)
{
	u32 *cmd = ctx->cmdbuf_virt;
	const struct rga3_fmt *out = ctx->out.fmt;
	const struct v4l2_format_info *out_fmt;
	unsigned int dst_h, dst_w;
	u8 wr_format;
	unsigned int reg;

	dst_h = ctx->out.pix.height;
	dst_w = ctx->out.pix.width;

	out_fmt = v4l2_format_info(out->fourcc);

	/* The Hardware only supports formats with 1/2 planes */
	if (out_fmt->comp_planes == 2)
		wr_format = RGA3_RDWR_FORMAT_SEMI_PLANAR;
	else
		wr_format = RGA3_RDWR_FORMAT_INTERLEAVED;

	/* set pixel format */
	reg = RGA3_WR_CTRL - RGA3_FIRST_CMD_REG;
	cmd[reg >> 2] = FIELD_PREP(RGA3_WR_PIC_FORMAT, out->hw_format)
		     |  FIELD_PREP(RGA3_WR_YC_SWAP, out->yc_swap)
		     |  FIELD_PREP(RGA3_WR_RBUV_SWAP, out->rbuv_swap)
		     |  FIELD_PREP(RGA3_WR_FORMAT, wr_format)
	/* Use the max value to avoid limiting the write speed */
		     |  FIELD_PREP(RGA3_WR_SW_OUTSTANDING_MAX, 63);

	/* set stride */
	reg = RGA3_WR_VIR_STRIDE - RGA3_FIRST_CMD_REG;
	/* stride needs to be in words */
	cmd[reg >> 2] = ctx->out.pix.plane_fmt[0].bytesperline >> 2;
	reg = RGA3_WR_PL_VIR_STRIDE - RGA3_FIRST_CMD_REG;
	/* The Hardware only supports formats with 1/2 planes */
	if (ctx->out.pix.num_planes == 2)
		cmd[reg >> 2] = ctx->out.pix.plane_fmt[1].bytesperline >> 2;
	else
		cmd[reg >> 2] = ctx->out.pix.plane_fmt[0].bytesperline >> 2;

	/* Set size.
	 * As two inputs are not supported, we don't use win1.
	 * Therefore only set the size for win0.
	 */
	reg = RGA3_WIN0_DST_SIZE - RGA3_FIRST_CMD_REG;
	cmd[reg >> 2] = FIELD_PREP(RGA3_WIDTH, dst_w)
		      | FIELD_PREP(RGA3_HEIGHT, dst_h);
}

static void rga3_hw_setup_cmdbuf(struct rga_ctx *ctx)
{
	memset(ctx->cmdbuf_virt, 0, RGA3_CMDBUF_SIZE);

	rga3_cmd_set_win0_format(ctx);
	rga3_cmd_set_trans_info(ctx);
	rga3_cmd_set_wr_format(ctx);
}

static void rga3_hw_start(struct rockchip_rga *rga,
			  struct rga_vb_buffer *src, struct rga_vb_buffer *dst)
{
	struct rga_ctx *ctx = rga->curr;

	rga3_cmd_set_win0_addr(ctx, &src->dma_addrs);
	rga3_cmd_set_wr_addr(ctx, &dst->dma_addrs);

	rga_write(rga, RGA3_CMD_ADDR, ctx->cmdbuf_phy);

	/* sync CMD buf for RGA */
	dma_sync_single_for_device(rga->dev, ctx->cmdbuf_phy,
				   PAGE_SIZE, DMA_BIDIRECTIONAL);

	/* set to master mode and start the conversion */
	rga_write(rga, RGA3_SYS_CTRL,
		  FIELD_PREP(RGA3_CMD_MODE, RGA3_CMD_MODE_MASTER));
	rga_write(rga, RGA3_INT_EN, FIELD_PREP(RGA3_INT_FRM_DONE, 1));
	rga_write(rga, RGA3_CMD_CTRL,
		  FIELD_PREP(RGA3_CMD_LINE_START_PULSE, 1));
}

static bool rga3_handle_irq(struct rockchip_rga *rga)
{
	u32 intr;

	intr = rga_read(rga, RGA3_INT_RAW);
	/* clear all interrupts */
	rga_write(rga, RGA3_INT_CLR, intr);

	return FIELD_GET(RGA3_INT_FRM_DONE, intr);
}

static void rga3_get_version(struct rockchip_rga *rga)
{
	u32 version = rga_read(rga, RGA3_VERSION_NUM);

	rga->version.major = FIELD_GET(RGA3_VERSION_NUM_MAJOR, version);
	rga->version.minor = FIELD_GET(RGA3_VERSION_NUM_MINOR, version);
}

static struct rga3_fmt rga3_formats[] = {
	{
		.fourcc = V4L2_PIX_FMT_RGB24,
		.hw_format = RGA3_COLOR_FMT_BGR888,
		.rbuv_swap = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_BGR24,
		.hw_format = RGA3_COLOR_FMT_BGR888,
	},
	{
		.fourcc = V4L2_PIX_FMT_ABGR32,
		.hw_format = RGA3_COLOR_FMT_BGRA8888,
	},
	{
		.fourcc = V4L2_PIX_FMT_RGBA32,
		.hw_format = RGA3_COLOR_FMT_BGRA8888,
		.rbuv_swap = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_XBGR32,
		.hw_format = RGA3_COLOR_FMT_BGRA8888,
	},
	{
		.fourcc = V4L2_PIX_FMT_RGBX32,
		.hw_format = RGA3_COLOR_FMT_BGRA8888,
		.rbuv_swap = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_RGB565,
		.hw_format = RGA3_COLOR_FMT_BGR565,
		.rbuv_swap = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV12M,
		.hw_format = RGA3_COLOR_FMT_YUV420,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.hw_format = RGA3_COLOR_FMT_YUV420,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV21M,
		.hw_format = RGA3_COLOR_FMT_YUV420,
		.rbuv_swap = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV21,
		.hw_format = RGA3_COLOR_FMT_YUV420,
		.rbuv_swap = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV16M,
		.hw_format = RGA3_COLOR_FMT_YUV422,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV16,
		.hw_format = RGA3_COLOR_FMT_YUV422,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV61M,
		.hw_format = RGA3_COLOR_FMT_YUV422,
		.rbuv_swap = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV61,
		.hw_format = RGA3_COLOR_FMT_YUV422,
		.rbuv_swap = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.hw_format = RGA3_COLOR_FMT_YUV422,
		.yc_swap = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_YVYU,
		.hw_format = RGA3_COLOR_FMT_YUV422,
		.yc_swap = 1,
		.rbuv_swap = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_UYVY,
		.hw_format = RGA3_COLOR_FMT_YUV422,
	},
	{
		.fourcc = V4L2_PIX_FMT_VYUY,
		.hw_format = RGA3_COLOR_FMT_YUV422,
		.rbuv_swap = 1,
	},
	/* Input only formats last to keep rga3_enum_format simple */
	{
		.fourcc = V4L2_PIX_FMT_ARGB32,
		.hw_format = RGA3_COLOR_FMT_ABGR8888,
		.rbuv_swap = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_BGRA32,
		.hw_format = RGA3_COLOR_FMT_ABGR8888,
	},
	{
		.fourcc = V4L2_PIX_FMT_XRGB32,
		.hw_format = RGA3_COLOR_FMT_ABGR8888,
		.rbuv_swap = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_BGRX32,
		.hw_format = RGA3_COLOR_FMT_ABGR8888,
	},
};

static int rga3_enum_format(struct v4l2_fmtdesc *f)
{
	struct rga3_fmt *fmt;

	if (f->index >= ARRAY_SIZE(rga3_formats))
		return -EINVAL;

	fmt = &rga3_formats[f->index];
	if (V4L2_TYPE_IS_CAPTURE(f->type) && !rga3_can_capture(fmt))
		return -EINVAL;

	f->pixelformat = fmt->fourcc;
	return 0;
}

static void *rga3_adjust_and_map_format(struct rga_ctx *ctx,
					struct v4l2_pix_format_mplane *format,
					bool is_output)
{
	unsigned int i;
	const struct v4l2_format_info *format_info;
	const struct v4l2_pix_format_mplane *other_format;
	const struct v4l2_format_info *other_format_info;

	if (!format)
		return &rga3_formats[0];

	format_info = v4l2_format_info(format->pixelformat);
	other_format = is_output ? &ctx->out.pix : &ctx->in.pix;
	other_format_info = v4l2_format_info(other_format->pixelformat);

	if ((v4l2_is_format_rgb(format_info) &&
	     v4l2_is_format_yuv(other_format_info)) ||
	    (v4l2_is_format_yuv(format_info) &&
	     v4l2_is_format_rgb(other_format_info))) {
		/*
		 * The RGA3 only supports BT601, BT709 and BT2020 RGB<->YUV conversions
		 * Additionally BT709 and BT2020 only support limited range YUV.
		 */
		switch (format->ycbcr_enc) {
		case V4L2_YCBCR_ENC_601:
			/* supports full and limited range */
			break;
		case V4L2_YCBCR_ENC_709:
		case V4L2_YCBCR_ENC_BT2020:
			format->quantization = V4L2_QUANTIZATION_LIM_RANGE;
			break;
		default:
			format->ycbcr_enc = V4L2_YCBCR_ENC_601;
			format->quantization = V4L2_QUANTIZATION_FULL_RANGE;
			break;
		}
	}

	for (i = 0; i < ARRAY_SIZE(rga3_formats); i++) {
		if (!is_output && !rga3_can_capture(&rga3_formats[i]))
			continue;

		if (rga3_formats[i].fourcc == format->pixelformat)
			return &rga3_formats[i];
	}

	format->pixelformat = rga3_formats[0].fourcc;
	return &rga3_formats[0];
}

const struct rga_hw rga3_hw = {
	.card_type = "rga3",
	.has_internal_iommu = false,
	.cmdbuf_size = RGA3_CMDBUF_SIZE,
	.min_width = RGA3_MIN_WIDTH,
	.min_height = RGA3_MIN_HEIGHT,
	/* use output size, as it's a bit smaller than the input size */
	.max_width = RGA3_MAX_OUTPUT_WIDTH,
	.max_height = RGA3_MAX_OUTPUT_HEIGHT,
	.max_scaling_factor = RGA3_MAX_SCALING_FACTOR,
	.stride_alignment = 16,
	.features = 0,

	.setup_cmdbuf = rga3_hw_setup_cmdbuf,
	.start = rga3_hw_start,
	.handle_irq = rga3_handle_irq,
	.get_version = rga3_get_version,
	.enum_format = rga3_enum_format,
	.adjust_and_map_format = rga3_adjust_and_map_format,
};
