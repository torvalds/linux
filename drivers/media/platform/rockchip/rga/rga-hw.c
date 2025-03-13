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

struct rga_corners_addr_offset {
	struct rga_addr_offset left_top;
	struct rga_addr_offset right_top;
	struct rga_addr_offset left_bottom;
	struct rga_addr_offset right_bottom;
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

static struct rga_corners_addr_offset
rga_get_addr_offset(struct rga_frame *frm, struct rga_addr_offset *offset,
		    unsigned int x, unsigned int y, unsigned int w, unsigned int h)
{
	struct rga_corners_addr_offset offsets;
	struct rga_addr_offset *lt, *lb, *rt, *rb;
	unsigned int x_div = 0,
		     y_div = 0, uv_stride = 0, pixel_width = 0;

	lt = &offsets.left_top;
	lb = &offsets.left_bottom;
	rt = &offsets.right_top;
	rb = &offsets.right_bottom;

	x_div = frm->fmt->x_div;
	y_div = frm->fmt->y_div;
	uv_stride = frm->stride / x_div;
	pixel_width = frm->stride / frm->width;

	lt->y_off = offset->y_off + y * frm->stride + x * pixel_width;
	lt->u_off = offset->u_off + (y / y_div) * uv_stride + x / x_div;
	lt->v_off = offset->v_off + (y / y_div) * uv_stride + x / x_div;

	lb->y_off = lt->y_off + (h - 1) * frm->stride;
	lb->u_off = lt->u_off + (h / y_div - 1) * uv_stride;
	lb->v_off = lt->v_off + (h / y_div - 1) * uv_stride;

	rt->y_off = lt->y_off + (w - 1) * pixel_width;
	rt->u_off = lt->u_off + w / x_div - 1;
	rt->v_off = lt->v_off + w / x_div - 1;

	rb->y_off = lb->y_off + (w - 1) * pixel_width;
	rb->u_off = lb->u_off + w / x_div - 1;
	rb->v_off = lb->v_off + w / x_div - 1;

	return offsets;
}

static struct rga_addr_offset *rga_lookup_draw_pos(struct
		rga_corners_addr_offset
		* offsets, u32 rotate_mode,
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

	if (!offsets)
		return NULL;

	switch (rot_mir_point_matrix[rotate_mode][mirr_mode]) {
	case LT:
		return &offsets->left_top;
	case LB:
		return &offsets->left_bottom;
	case RT:
		return &offsets->right_top;
	case RB:
		return &offsets->right_bottom;
	}

	return NULL;
}

static void rga_cmd_set_src_addr(struct rga_ctx *ctx, dma_addr_t dma_addr)
{
	struct rockchip_rga *rga = ctx->rga;
	u32 *dest = rga->cmdbuf_virt;
	unsigned int reg;

	reg = RGA_MMU_SRC_BASE - RGA_MODE_BASE_REG;
	dest[reg >> 2] = dma_addr >> 4;

	reg = RGA_MMU_CTRL1 - RGA_MODE_BASE_REG;
	dest[reg >> 2] |= 0x7;
}

static void rga_cmd_set_src1_addr(struct rga_ctx *ctx, dma_addr_t dma_addr)
{
	struct rockchip_rga *rga = ctx->rga;
	u32 *dest = rga->cmdbuf_virt;
	unsigned int reg;

	reg = RGA_MMU_SRC1_BASE - RGA_MODE_BASE_REG;
	dest[reg >> 2] = dma_addr >> 4;

	reg = RGA_MMU_CTRL1 - RGA_MODE_BASE_REG;
	dest[reg >> 2] |= 0x7 << 4;
}

static void rga_cmd_set_dst_addr(struct rga_ctx *ctx, dma_addr_t dma_addr)
{
	struct rockchip_rga *rga = ctx->rga;
	u32 *dest = rga->cmdbuf_virt;
	unsigned int reg;

	reg = RGA_MMU_DST_BASE - RGA_MODE_BASE_REG;
	dest[reg >> 2] = dma_addr >> 4;

	reg = RGA_MMU_CTRL1 - RGA_MODE_BASE_REG;
	dest[reg >> 2] |= 0x7 << 8;
}

static void rga_cmd_set_trans_info(struct rga_ctx *ctx)
{
	struct rockchip_rga *rga = ctx->rga;
	u32 *dest = rga->cmdbuf_virt;
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

	src_info.data.format = ctx->in.fmt->hw_format;
	src_info.data.swap = ctx->in.fmt->color_swap;
	dst_info.data.format = ctx->out.fmt->hw_format;
	dst_info.data.swap = ctx->out.fmt->color_swap;

	/*
	 * CSC mode must only be set when the colorspace families differ between
	 * input and output. It must remain unset (zeroed) if both are the same.
	 */

	if (RGA_COLOR_FMT_IS_YUV(ctx->in.fmt->hw_format) &&
	    RGA_COLOR_FMT_IS_RGB(ctx->out.fmt->hw_format)) {
		switch (ctx->in.colorspace) {
		case V4L2_COLORSPACE_REC709:
			src_info.data.csc_mode = RGA_SRC_CSC_MODE_BT709_R0;
			break;
		default:
			src_info.data.csc_mode = RGA_SRC_CSC_MODE_BT601_R0;
			break;
		}
	}

	if (RGA_COLOR_FMT_IS_RGB(ctx->in.fmt->hw_format) &&
	    RGA_COLOR_FMT_IS_YUV(ctx->out.fmt->hw_format)) {
		switch (ctx->out.colorspace) {
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
	src_vir_info.data.vir_stride = ctx->in.stride >> 2;
	src_vir_info.data.vir_width = ctx->in.stride >> 2;

	src_act_info.data.act_height = src_h - 1;
	src_act_info.data.act_width = src_w - 1;

	dst_vir_info.data.vir_stride = ctx->out.stride >> 2;
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
				 struct rga_addr_offset *offset)
{
	struct rga_corners_addr_offset src_offsets;
	struct rockchip_rga *rga = ctx->rga;
	u32 *dest = rga->cmdbuf_virt;
	unsigned int src_h, src_w, src_x, src_y;

	src_h = ctx->in.crop.height;
	src_w = ctx->in.crop.width;
	src_x = ctx->in.crop.left;
	src_y = ctx->in.crop.top;

	/*
	 * Calculate the source framebuffer base address with offset pixel.
	 */
	src_offsets = rga_get_addr_offset(&ctx->in, offset,
					  src_x, src_y, src_w, src_h);

	dest[(RGA_SRC_Y_RGB_BASE_ADDR - RGA_MODE_BASE_REG) >> 2] =
		src_offsets.left_top.y_off;
	dest[(RGA_SRC_CB_BASE_ADDR - RGA_MODE_BASE_REG) >> 2] =
		src_offsets.left_top.u_off;
	dest[(RGA_SRC_CR_BASE_ADDR - RGA_MODE_BASE_REG) >> 2] =
		src_offsets.left_top.v_off;
}

static void rga_cmd_set_dst_info(struct rga_ctx *ctx,
				 struct rga_addr_offset *offset)
{
	struct rga_addr_offset *dst_offset;
	struct rga_corners_addr_offset offsets;
	struct rockchip_rga *rga = ctx->rga;
	u32 *dest = rga->cmdbuf_virt;
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
	offsets = rga_get_addr_offset(&ctx->out, offset, dst_x, dst_y, dst_w, dst_h);
	dst_offset = rga_lookup_draw_pos(&offsets, mir_mode, rot_mode);

	dest[(RGA_DST_Y_RGB_BASE_ADDR - RGA_MODE_BASE_REG) >> 2] =
		dst_offset->y_off;
	dest[(RGA_DST_CB_BASE_ADDR - RGA_MODE_BASE_REG) >> 2] =
		dst_offset->u_off;
	dest[(RGA_DST_CR_BASE_ADDR - RGA_MODE_BASE_REG) >> 2] =
		dst_offset->v_off;
}

static void rga_cmd_set_mode(struct rga_ctx *ctx)
{
	struct rockchip_rga *rga = ctx->rga;
	u32 *dest = rga->cmdbuf_virt;
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

	memset(rga->cmdbuf_virt, 0, RGA_CMDBUF_SIZE * 4);

	rga_cmd_set_src_addr(ctx, src->dma_desc_pa);
	/*
	 * Due to hardware bug,
	 * src1 mmu also should be configured when using alpha blending.
	 */
	rga_cmd_set_src1_addr(ctx, dst->dma_desc_pa);

	rga_cmd_set_dst_addr(ctx, dst->dma_desc_pa);
	rga_cmd_set_mode(ctx);

	rga_cmd_set_src_info(ctx, &src->offset);
	rga_cmd_set_dst_info(ctx, &dst->offset);
	rga_cmd_set_trans_info(ctx);

	rga_write(rga, RGA_CMD_BASE, rga->cmdbuf_phy);

	/* sync CMD buf for RGA */
	dma_sync_single_for_device(rga->dev, rga->cmdbuf_phy,
		PAGE_SIZE, DMA_BIDIRECTIONAL);
}

void rga_hw_start(struct rockchip_rga *rga,
		  struct rga_vb_buffer *src, struct rga_vb_buffer *dst)
{
	struct rga_ctx *ctx = rga->curr;

	rga_cmd_set(ctx, src, dst);

	rga_write(rga, RGA_SYS_CTRL, 0x00);

	rga_write(rga, RGA_SYS_CTRL, 0x22);

	rga_write(rga, RGA_INT, 0x600);

	rga_write(rga, RGA_CMD_CTRL, 0x1);
}
