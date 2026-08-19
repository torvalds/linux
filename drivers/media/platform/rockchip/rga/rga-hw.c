// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 * Author: Jacob Chen <jacob-chen@iotwrt.com>
 */

#include <linux/pm_runtime.h>

#include "rga-hw.h"
#include "rga.h"

enum e_rga_start_pos {
	LT = 0,
	LB = 1,
	RT = 2,
	RB = 3,
};

struct rga_corners_addrs {
	struct rga_addrs left_top;
	struct rga_addrs right_top;
	struct rga_addrs left_bottom;
	struct rga_addrs right_bottom;
};

static unsigned int rga_get_scaling(unsigned int src, unsigned int dst)
{
	/*
	 * The rga hw scaling factor is a normalized inverse of the
	 * scaling factor.
	 * For example: When source width is 100 and destination width is 200
	 * (scaling of 2x), then the hw factor is NC * 100 / 200.
	 * The normalization factor (NC) is 2^16 = 0x10000.
	 */

	return (src > dst) ? ((dst << 16) / src) : ((src << 16) / dst);
}

static struct rga_corners_addrs
rga_get_corner_addrs(struct rga_frame *frm, struct rga_addrs *addrs,
		     unsigned int x, unsigned int y, unsigned int w, unsigned int h)
{
	struct rga_corners_addrs corner_addrs;
	struct rga_addrs *lt, *lb, *rt, *rb;
	const struct v4l2_format_info *format_info;
	unsigned int x_div = 0,
		     y_div = 0, y_stride = 0, uv_stride = 0, pixel_width = 0;

	lt = &corner_addrs.left_top;
	lb = &corner_addrs.left_bottom;
	rt = &corner_addrs.right_top;
	rb = &corner_addrs.right_bottom;

	format_info = v4l2_format_info(frm->pix.pixelformat);
	/* x_div is only used for the u/v planes.
	 * When the format doesn't have these, use 1 to avoid a division by zero.
	 */
	if (format_info->bpp[1])
		x_div = format_info->hdiv * format_info->bpp_div[1] /
			format_info->bpp[1];
	else
		x_div = 1;
	y_div = format_info->vdiv;
	y_stride = frm->pix.plane_fmt[0].bytesperline;
	uv_stride = y_stride / x_div;
	pixel_width = y_stride / frm->pix.width;

	lt->y_addr = addrs->y_addr + y * y_stride + x * pixel_width;
	lt->u_addr = addrs->u_addr + (y / y_div) * uv_stride + x / x_div;
	lt->v_addr = addrs->v_addr + (y / y_div) * uv_stride + x / x_div;

	lb->y_addr = lt->y_addr + (h - 1) * y_stride;
	lb->u_addr = lt->u_addr + (h / y_div - 1) * uv_stride;
	lb->v_addr = lt->v_addr + (h / y_div - 1) * uv_stride;

	rt->y_addr = lt->y_addr + (w - 1) * pixel_width;
	rt->u_addr = lt->u_addr + w / x_div - 1;
	rt->v_addr = lt->v_addr + w / x_div - 1;

	rb->y_addr = lb->y_addr + (w - 1) * pixel_width;
	rb->u_addr = lb->u_addr + w / x_div - 1;
	rb->v_addr = lb->v_addr + w / x_div - 1;

	return corner_addrs;
}

static struct rga_addrs *rga_lookup_draw_pos(struct rga_corners_addrs *corner_addrs,
					     u32 rotate_mode,
					     u32 mirr_mode)
{
	static enum e_rga_start_pos rot_mir_point_matrix[4][4] = {
		{
			LT, RT, LB, RB,
		},
		{
			RT, LT, RB, LB,
		},
		{
			RB, LB, RT, LT,
		},
		{
			LB, RB, LT, RT,
		},
	};

	if (!corner_addrs)
		return NULL;

	switch (rot_mir_point_matrix[rotate_mode][mirr_mode]) {
	case LT:
		return &corner_addrs->left_top;
	case LB:
		return &corner_addrs->left_bottom;
	case RT:
		return &corner_addrs->right_top;
	case RB:
		return &corner_addrs->right_bottom;
	}

	return NULL;
}

static void rga_cmd_set_src_addr(struct rga_ctx *ctx, dma_addr_t dma_addr)
{
	u32 *dest = ctx->cmdbuf_virt;
	unsigned int reg;

	reg = RGA_MMU_SRC_BASE - RGA_MODE_BASE_REG;
	dest[reg >> 2] = dma_addr >> 4;

	reg = RGA_MMU_CTRL1 - RGA_MODE_BASE_REG;
	dest[reg >> 2] |= 0x7;
}

static void rga_cmd_set_src1_addr(struct rga_ctx *ctx, dma_addr_t dma_addr)
{
	u32 *dest = ctx->cmdbuf_virt;
	unsigned int reg;

	reg = RGA_MMU_SRC1_BASE - RGA_MODE_BASE_REG;
	dest[reg >> 2] = dma_addr >> 4;

	reg = RGA_MMU_CTRL1 - RGA_MODE_BASE_REG;
	dest[reg >> 2] |= 0x7 << 4;
}

static void rga_cmd_set_dst_addr(struct rga_ctx *ctx, dma_addr_t dma_addr)
{
	u32 *dest = ctx->cmdbuf_virt;
	unsigned int reg;

	reg = RGA_MMU_DST_BASE - RGA_MODE_BASE_REG;
	dest[reg >> 2] = dma_addr >> 4;

	reg = RGA_MMU_CTRL1 - RGA_MODE_BASE_REG;
	dest[reg >> 2] |= 0x7 << 8;
}

static void rga_cmd_set_trans_info(struct rga_ctx *ctx)
{
	struct rockchip_rga *rga = ctx->rga;
	u32 *dest = ctx->cmdbuf_virt;
	unsigned int scale_dst_w, scale_dst_h;
	unsigned int src_h, src_w, dst_h, dst_w;
	union rga_src_info src_info;
	union rga_dst_info dst_info;
	union rga_src_x_factor x_factor;
	union rga_src_y_factor y_factor;
	union rga_src_vir_info src_vir_info;
	union rga_src_act_info src_act_info;
	union rga_dst_vir_info dst_vir_info;
	union rga_dst_act_info dst_act_info;
	u32 in_stride, out_stride;
	struct rga_fmt *in_fmt = ctx->in.fmt;
	struct rga_fmt *out_fmt = ctx->out.fmt;

	src_h = ctx->in.crop.height;
	src_w = ctx->in.crop.width;
	dst_h = ctx->out.crop.height;
	dst_w = ctx->out.crop.width;

	src_info.val = dest[(RGA_SRC_INFO - RGA_MODE_BASE_REG) >> 2];
	dst_info.val = dest[(RGA_DST_INFO - RGA_MODE_BASE_REG) >> 2];
	x_factor.val = dest[(RGA_SRC_X_FACTOR - RGA_MODE_BASE_REG) >> 2];
	y_factor.val = dest[(RGA_SRC_Y_FACTOR - RGA_MODE_BASE_REG) >> 2];
	src_vir_info.val = dest[(RGA_SRC_VIR_INFO - RGA_MODE_BASE_REG) >> 2];
	src_act_info.val = dest[(RGA_SRC_ACT_INFO - RGA_MODE_BASE_REG) >> 2];
	dst_vir_info.val = dest[(RGA_DST_VIR_INFO - RGA_MODE_BASE_REG) >> 2];
	dst_act_info.val = dest[(RGA_DST_ACT_INFO - RGA_MODE_BASE_REG) >> 2];

	src_info.data.format = in_fmt->hw_format;
	src_info.data.swap = in_fmt->color_swap;
	dst_info.data.format = out_fmt->hw_format;
	dst_info.data.swap = out_fmt->color_swap;

	/*
	 * CSC mode must only be set when the colorspace families differ between
	 * input and output. It must remain unset (zeroed) if both are the same.
	 */

	if (RGA_COLOR_FMT_IS_YUV(in_fmt->hw_format) &&
	    RGA_COLOR_FMT_IS_RGB(out_fmt->hw_format)) {
		switch (ctx->in.pix.colorspace) {
		case V4L2_COLORSPACE_REC709:
			src_info.data.csc_mode = RGA_SRC_CSC_MODE_BT709_R0;
			break;
		default:
			src_info.data.csc_mode = RGA_SRC_CSC_MODE_BT601_R0;
			break;
		}
	}

	if (RGA_COLOR_FMT_IS_RGB(in_fmt->hw_format) &&
	    RGA_COLOR_FMT_IS_YUV(out_fmt->hw_format)) {
		switch (ctx->out.pix.colorspace) {
		case V4L2_COLORSPACE_REC709:
			dst_info.data.csc_mode = RGA_SRC_CSC_MODE_BT709_R0;
			break;
		default:
			dst_info.data.csc_mode = RGA_DST_CSC_MODE_BT601_R0;
			break;
		}
	}

	if (ctx->vflip)
		src_info.data.mir_mode |= RGA_SRC_MIRR_MODE_X;

	if (ctx->hflip)
		src_info.data.mir_mode |= RGA_SRC_MIRR_MODE_Y;

	switch (ctx->rotate) {
	case 90:
		src_info.data.rot_mode = RGA_SRC_ROT_MODE_90_DEGREE;
		break;
	case 180:
		src_info.data.rot_mode = RGA_SRC_ROT_MODE_180_DEGREE;
		break;
	case 270:
		src_info.data.rot_mode = RGA_SRC_ROT_MODE_270_DEGREE;
		break;
	default:
		src_info.data.rot_mode = RGA_SRC_ROT_MODE_0_DEGREE;
		break;
	}

	/*
	 * Calculate the up/down scaling mode/factor.
	 *
	 * RGA used to scale the picture first, and then rotate second,
	 * so we need to swap the w/h when rotate degree is 90/270.
	 */
	if (src_info.data.rot_mode == RGA_SRC_ROT_MODE_90_DEGREE ||
	    src_info.data.rot_mode == RGA_SRC_ROT_MODE_270_DEGREE) {
		if (rga->version.major == 0 || rga->version.minor == 0) {
			if (dst_w == src_h)
				src_h -= 8;
			if (abs(src_w - dst_h) < 16)
				src_w -= 16;
		}

		scale_dst_h = dst_w;
		scale_dst_w = dst_h;
	} else {
		scale_dst_w = dst_w;
		scale_dst_h = dst_h;
	}

	if (src_w == scale_dst_w) {
		src_info.data.hscl_mode = RGA_SRC_HSCL_MODE_NO;
		x_factor.val = 0;
	} else if (src_w > scale_dst_w) {
		src_info.data.hscl_mode = RGA_SRC_HSCL_MODE_DOWN;
		x_factor.data.down_scale_factor =
			rga_get_scaling(src_w, scale_dst_w) + 1;
	} else {
		src_info.data.hscl_mode = RGA_SRC_HSCL_MODE_UP;
		x_factor.data.up_scale_factor =
			rga_get_scaling(src_w - 1, scale_dst_w - 1);
	}

	if (src_h == scale_dst_h) {
		src_info.data.vscl_mode = RGA_SRC_VSCL_MODE_NO;
		y_factor.val = 0;
	} else if (src_h > scale_dst_h) {
		src_info.data.vscl_mode = RGA_SRC_VSCL_MODE_DOWN;
		y_factor.data.down_scale_factor =
			rga_get_scaling(src_h, scale_dst_h) + 1;
	} else {
		src_info.data.vscl_mode = RGA_SRC_VSCL_MODE_UP;
		y_factor.data.up_scale_factor =
			rga_get_scaling(src_h - 1, scale_dst_h - 1);
	}

	/*
	 * Calculate the framebuffer virtual strides and active size,
	 * note that the step of vir_stride / vir_width is 4 byte words
	 */
	in_stride = ctx->in.pix.plane_fmt[0].bytesperline;
	src_vir_info.data.vir_stride = in_stride >> 2;
	src_vir_info.data.vir_width = in_stride >> 2;

	src_act_info.data.act_height = src_h - 1;
	src_act_info.data.act_width = src_w - 1;

	out_stride = ctx->out.pix.plane_fmt[0].bytesperline;
	dst_vir_info.data.vir_stride = out_stride >> 2;
	dst_act_info.data.act_height = dst_h - 1;
	dst_act_info.data.act_width = dst_w - 1;

	dest[(RGA_SRC_X_FACTOR - RGA_MODE_BASE_REG) >> 2] = x_factor.val;
	dest[(RGA_SRC_Y_FACTOR - RGA_MODE_BASE_REG) >> 2] = y_factor.val;
	dest[(RGA_SRC_VIR_INFO - RGA_MODE_BASE_REG) >> 2] = src_vir_info.val;
	dest[(RGA_SRC_ACT_INFO - RGA_MODE_BASE_REG) >> 2] = src_act_info.val;

	dest[(RGA_SRC_INFO - RGA_MODE_BASE_REG) >> 2] = src_info.val;

	dest[(RGA_DST_VIR_INFO - RGA_MODE_BASE_REG) >> 2] = dst_vir_info.val;
	dest[(RGA_DST_ACT_INFO - RGA_MODE_BASE_REG) >> 2] = dst_act_info.val;

	dest[(RGA_DST_INFO - RGA_MODE_BASE_REG) >> 2] = dst_info.val;
}

static void rga_cmd_set_src_info(struct rga_ctx *ctx,
				 struct rga_addrs *addrs)
{
	struct rga_corners_addrs src_corner_addrs;
	u32 *dest = ctx->cmdbuf_virt;
	unsigned int src_h, src_w, src_x, src_y;

	src_h = ctx->in.crop.height;
	src_w = ctx->in.crop.width;
	src_x = ctx->in.crop.left;
	src_y = ctx->in.crop.top;

	/*
	 * Calculate the source framebuffer base address with offset pixel.
	 */
	src_corner_addrs = rga_get_corner_addrs(&ctx->in, addrs,
						src_x, src_y, src_w, src_h);

	dest[(RGA_SRC_Y_RGB_BASE_ADDR - RGA_MODE_BASE_REG) >> 2] =
		src_corner_addrs.left_top.y_addr;
	dest[(RGA_SRC_CB_BASE_ADDR - RGA_MODE_BASE_REG) >> 2] =
		src_corner_addrs.left_top.u_addr;
	dest[(RGA_SRC_CR_BASE_ADDR - RGA_MODE_BASE_REG) >> 2] =
		src_corner_addrs.left_top.v_addr;
}

static void rga_cmd_set_dst_info(struct rga_ctx *ctx,
				 struct rga_addrs *addrs)
{
	struct rga_addrs *dst_addrs;
	struct rga_corners_addrs corner_addrs;
	u32 *dest = ctx->cmdbuf_virt;
	unsigned int dst_h, dst_w, dst_x, dst_y;
	unsigned int mir_mode = 0;
	unsigned int rot_mode = 0;

	dst_h = ctx->out.crop.height;
	dst_w = ctx->out.crop.width;
	dst_x = ctx->out.crop.left;
	dst_y = ctx->out.crop.top;

	if (ctx->vflip)
		mir_mode |= RGA_SRC_MIRR_MODE_X;
	if (ctx->hflip)
		mir_mode |= RGA_SRC_MIRR_MODE_Y;

	switch (ctx->rotate) {
	case 90:
		rot_mode = RGA_SRC_ROT_MODE_90_DEGREE;
		break;
	case 180:
		rot_mode = RGA_SRC_ROT_MODE_180_DEGREE;
		break;
	case 270:
		rot_mode = RGA_SRC_ROT_MODE_270_DEGREE;
		break;
	default:
		rot_mode = RGA_SRC_ROT_MODE_0_DEGREE;
		break;
	}

	/*
	 * Configure the dest framebuffer base address with pixel offset.
	 */
	corner_addrs = rga_get_corner_addrs(&ctx->out, addrs, dst_x, dst_y, dst_w, dst_h);
	dst_addrs = rga_lookup_draw_pos(&corner_addrs, rot_mode, mir_mode);

	dest[(RGA_DST_Y_RGB_BASE_ADDR - RGA_MODE_BASE_REG) >> 2] =
		dst_addrs->y_addr;
	dest[(RGA_DST_CB_BASE_ADDR - RGA_MODE_BASE_REG) >> 2] =
		dst_addrs->u_addr;
	dest[(RGA_DST_CR_BASE_ADDR - RGA_MODE_BASE_REG) >> 2] =
		dst_addrs->v_addr;
}

static void rga_cmd_set_mode(struct rga_ctx *ctx)
{
	u32 *dest = ctx->cmdbuf_virt;
	union rga_mode_ctrl mode;
	union rga_alpha_ctrl0 alpha_ctrl0;
	union rga_alpha_ctrl1 alpha_ctrl1;

	mode.val = 0;
	alpha_ctrl0.val = 0;
	alpha_ctrl1.val = 0;

	mode.data.gradient_sat = 1;
	mode.data.render = RGA_MODE_RENDER_BITBLT;
	mode.data.bitblt = RGA_MODE_BITBLT_MODE_SRC_TO_DST;

	/* disable alpha blending */
	dest[(RGA_ALPHA_CTRL0 - RGA_MODE_BASE_REG) >> 2] = alpha_ctrl0.val;
	dest[(RGA_ALPHA_CTRL1 - RGA_MODE_BASE_REG) >> 2] = alpha_ctrl1.val;

	dest[(RGA_MODE_CTRL - RGA_MODE_BASE_REG) >> 2] = mode.val;
}

static void rga_cmd_set(struct rga_ctx *ctx,
			struct rga_vb_buffer *src, struct rga_vb_buffer *dst)
{
	struct rockchip_rga *rga = ctx->rga;

	rga_cmd_set_src_addr(ctx, src->dma_desc_pa);
	/*
	 * Due to hardware bug,
	 * src1 mmu also should be configured when using alpha blending.
	 */
	rga_cmd_set_src1_addr(ctx, dst->dma_desc_pa);

	rga_cmd_set_dst_addr(ctx, dst->dma_desc_pa);

	rga_cmd_set_src_info(ctx, &src->dma_addrs);
	rga_cmd_set_dst_info(ctx, &dst->dma_addrs);

	rga_write(rga, RGA_CMD_BASE, ctx->cmdbuf_phy);

	/* sync CMD buf for RGA */
	dma_sync_single_for_device(rga->dev, ctx->cmdbuf_phy,
				   PAGE_SIZE, DMA_BIDIRECTIONAL);
}

static void rga_hw_setup_cmdbuf(struct rga_ctx *ctx)
{
	memset(ctx->cmdbuf_virt, 0, RGA_CMDBUF_SIZE);

	rga_cmd_set_mode(ctx);
	rga_cmd_set_trans_info(ctx);
}

static void rga_hw_start(struct rockchip_rga *rga,
			 struct rga_vb_buffer *src,  struct rga_vb_buffer *dst)
{
	struct rga_ctx *ctx = rga->curr;

	rga_cmd_set(ctx, src, dst);

	rga_write(rga, RGA_SYS_CTRL, 0x00);

	rga_write(rga, RGA_SYS_CTRL, 0x22);

	rga_write(rga, RGA_INT, 0x600);

	rga_write(rga, RGA_CMD_CTRL, 0x1);
}

static bool rga_handle_irq(struct rockchip_rga *rga)
{
	int intr;

	intr = rga_read(rga, RGA_INT) & 0xf;

	rga_mod(rga, RGA_INT, intr << 4, 0xf << 4);

	return intr & RGA_INT_COMMAND_FINISHED;
}

static void rga_get_version(struct rockchip_rga *rga)
{
	rga->version.major = (rga_read(rga, RGA_VERSION_INFO) >> 24) & 0xFF;
	rga->version.minor = (rga_read(rga, RGA_VERSION_INFO) >> 20) & 0x0F;
}

static struct rga_fmt formats[] = {
	{
		.fourcc = V4L2_PIX_FMT_ARGB32,
		.color_swap = RGA_COLOR_ALPHA_SWAP,
		.hw_format = RGA_COLOR_FMT_ABGR8888,
	},
	{
		.fourcc = V4L2_PIX_FMT_ABGR32,
		.color_swap = RGA_COLOR_RB_SWAP,
		.hw_format = RGA_COLOR_FMT_ABGR8888,
	},
	{
		.fourcc = V4L2_PIX_FMT_XBGR32,
		.color_swap = RGA_COLOR_RB_SWAP,
		.hw_format = RGA_COLOR_FMT_XBGR8888,
	},
	{
		.fourcc = V4L2_PIX_FMT_RGB24,
		.color_swap = RGA_COLOR_NONE_SWAP,
		.hw_format = RGA_COLOR_FMT_RGB888,
	},
	{
		.fourcc = V4L2_PIX_FMT_BGR24,
		.color_swap = RGA_COLOR_RB_SWAP,
		.hw_format = RGA_COLOR_FMT_RGB888,
	},
	{
		.fourcc = V4L2_PIX_FMT_ARGB444,
		.color_swap = RGA_COLOR_RB_SWAP,
		.hw_format = RGA_COLOR_FMT_ABGR4444,
	},
	{
		.fourcc = V4L2_PIX_FMT_ARGB555,
		.color_swap = RGA_COLOR_RB_SWAP,
		.hw_format = RGA_COLOR_FMT_ABGR1555,
	},
	{
		.fourcc = V4L2_PIX_FMT_RGB565,
		.color_swap = RGA_COLOR_RB_SWAP,
		.hw_format = RGA_COLOR_FMT_BGR565,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV21,
		.color_swap = RGA_COLOR_UV_SWAP,
		.hw_format = RGA_COLOR_FMT_YUV420SP,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV61,
		.color_swap = RGA_COLOR_UV_SWAP,
		.hw_format = RGA_COLOR_FMT_YUV422SP,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.color_swap = RGA_COLOR_NONE_SWAP,
		.hw_format = RGA_COLOR_FMT_YUV420SP,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV12M,
		.color_swap = RGA_COLOR_NONE_SWAP,
		.hw_format = RGA_COLOR_FMT_YUV420SP,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV16,
		.color_swap = RGA_COLOR_NONE_SWAP,
		.hw_format = RGA_COLOR_FMT_YUV422SP,
	},
	{
		.fourcc = V4L2_PIX_FMT_YUV420,
		.color_swap = RGA_COLOR_NONE_SWAP,
		.hw_format = RGA_COLOR_FMT_YUV420P,
	},
	{
		.fourcc = V4L2_PIX_FMT_YUV422P,
		.color_swap = RGA_COLOR_NONE_SWAP,
		.hw_format = RGA_COLOR_FMT_YUV422P,
	},
	{
		.fourcc = V4L2_PIX_FMT_YVU420,
		.color_swap = RGA_COLOR_UV_SWAP,
		.hw_format = RGA_COLOR_FMT_YUV420P,
	},
};

static void *rga_adjust_and_map_format(struct rga_ctx *ctx,
				       struct v4l2_pix_format_mplane *format,
				       bool is_output)
{
	unsigned int i;

	if (!format)
		return &formats[0];

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		if (formats[i].fourcc == format->pixelformat)
			return &formats[i];
	}

	format->pixelformat = formats[0].fourcc;
	return &formats[0];
}

static int rga_enum_format(struct v4l2_fmtdesc *f)
{
	if (f->index >= ARRAY_SIZE(formats))
		return -EINVAL;

	f->pixelformat = formats[f->index].fourcc;
	return 0;
}

const struct rga_hw rga2_hw = {
	.card_type = "rga2",
	.has_internal_iommu = true,
	.cmdbuf_size = RGA_CMDBUF_SIZE,
	.min_width = MIN_WIDTH,
	.max_width = MAX_WIDTH,
	.min_height = MIN_HEIGHT,
	.max_height = MAX_HEIGHT,
	.max_scaling_factor = MAX_SCALING_FACTOR,
	.stride_alignment = 4,
	.features = RGA_FEATURE_FLIP
		  | RGA_FEATURE_ROTATE
		  | RGA_FEATURE_BG_COLOR,

	.setup_cmdbuf = rga_hw_setup_cmdbuf,
	.start = rga_hw_start,
	.handle_irq = rga_handle_irq,
	.get_version = rga_get_version,
	.adjust_and_map_format = rga_adjust_and_map_format,
	.enum_format = rga_enum_format,
};
