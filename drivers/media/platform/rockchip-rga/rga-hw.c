/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Jacob Chen <jacob-chen@iotwrt.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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

struct rga_addr_offset {
	unsigned int y_off;
	unsigned int u_off;
	unsigned int v_off;
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
rga_get_addr_offset(struct rga_frame *frm, unsigned int x, unsigned int y,
		    unsigned int w, unsigned int h)
{
	struct rga_corners_addr_offset offsets;
	struct rga_addr_offset *lt, *lb, *rt, *rb;
	unsigned int x_div = 0,
		     y_div = 0, uv_stride = 0, pixel_width = 0, uv_factor = 0;

	lt = &offsets.left_top;
	lb = &offsets.left_bottom;
	rt = &offsets.right_top;
	rb = &offsets.right_bottom;

	x_div = frm->fmt->x_div;
	y_div = frm->fmt->y_div;
	uv_factor = frm->fmt->uv_factor;
	uv_stride = frm->stride / x_div;
	pixel_width = frm->stride / frm->width;

	lt->y_off = y * frm->stride + x * pixel_width;
	lt->u_off =
		frm->width * frm->height + (y / y_div) * uv_stride + x / x_div;
	lt->v_off = lt->u_off + frm->width * frm->height / uv_factor;

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

static unsigned int rga_get_quantization(u32 format, u32 colorspace,
					 u32 quantization)
{
	/*
	 * The default for R'G'B' quantization is full range by default.
	 * For Y'CbCr the quantization is limited range by default.
	 */
	if (format >= RGA_COLOR_FMT_YUV422SP &&
		quantization == V4L2_QUANTIZATION_DEFAULT) {
		quantization = V4L2_QUANTIZATION_LIM_RANGE;
	} else if (format < RGA_COLOR_FMT_YUV422SP &&
		quantization == V4L2_QUANTIZATION_DEFAULT) {
		quantization = V4L2_QUANTIZATION_FULL_RANGE;
	}

	if (colorspace == V4L2_COLORSPACE_REC709)
		return RGA_CSC_MODE_BT709_R0;

	if (quantization == V4L2_QUANTIZATION_LIM_RANGE)
		return RGA_CSC_MODE_BT601_R0;
	else if (quantization == V4L2_QUANTIZATION_FULL_RANGE)
		return RGA_CSC_MODE_BT601_R1;

	return RGA_CSC_MODE_BYPASS;
}

static void rga_cmd_set_src_addr(struct rga_ctx *ctx, void *mmu_pages)
{
	struct rockchip_rga *rga = ctx->rga;
	u32 *dest = rga->cmdbuf_virt;
	unsigned int reg;

	reg = RGA_MMU_SRC_BASE - RGA_MODE_BASE_REG;
	dest[reg >> 2] = virt_to_phys(mmu_pages) >> 4;

	reg = RGA_MMU_CTRL1 - RGA_MODE_BASE_REG;
	dest[reg >> 2] |= 0x7;
}

static void rga_cmd_set_src1_addr(struct rga_ctx *ctx, void *mmu_pages)
{
	struct rockchip_rga *rga = ctx->rga;
	u32 *dest = rga->cmdbuf_virt;
	unsigned int reg;

	reg = RGA_MMU_SRC1_BASE - RGA_MODE_BASE_REG;
	dest[reg >> 2] = virt_to_phys(mmu_pages) >> 4;

	reg = RGA_MMU_CTRL1 - RGA_MODE_BASE_REG;
	dest[reg >> 2] |= 0x7 << 4;
}

static void rga_cmd_set_dst_addr(struct rga_ctx *ctx, void *mmu_pages)
{
	struct rockchip_rga *rga = ctx->rga;
	u32 *dest = rga->cmdbuf_virt;
	unsigned int reg;

	reg = RGA_MMU_DST_BASE - RGA_MODE_BASE_REG;
	dest[reg >> 2] = virt_to_phys(mmu_pages) >> 4;

	reg = RGA_MMU_CTRL1 - RGA_MODE_BASE_REG;
	dest[reg >> 2] |= 0x7 << 8;
}

static void rga_cmd_set_trans_info(struct rga_ctx *ctx)
{
	struct rockchip_rga *rga = ctx->rga;
	u32 *dest = rga->cmdbuf_virt;
	unsigned int scale_dst_w, scale_dst_h;
	unsigned int src_h, src_w, src_x, src_y, dst_h, dst_w, dst_x, dst_y;
	union rga_src_info src_info;
	union rga_dst_info dst_info;
	union rga_src_x_factor x_factor;
	union rga_src_y_factor y_factor;
	union rga_src_vir_info src_vir_info;
	union rga_src_act_info src_act_info;
	union rga_dst_vir_info dst_vir_info;
	union rga_dst_act_info dst_act_info;

	struct rga_addr_offset *dst_offset;
	struct rga_corners_addr_offset offsets;
	struct rga_corners_addr_offset src_offsets;

	src_h = ctx->in.crop.height;
	src_w = ctx->in.crop.width;
	src_x = ctx->in.crop.left;
	src_y = ctx->in.crop.top;
	dst_h = ctx->out.crop.height;
	dst_w = ctx->out.crop.width;
	dst_x = ctx->out.crop.left;
	dst_y = ctx->out.crop.top;

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

	/* yuv -> rgb */
	if (ctx->in.fmt->hw_format >= RGA_COLOR_FMT_YUV422SP &&
		ctx->out.fmt->hw_format < RGA_COLOR_FMT_YUV422SP)
		src_info.data.csc_mode =
			rga_get_quantization(ctx->in.fmt->hw_format,
				ctx->in.colorspace, ctx->in.quantization);

	/* (yuv -> rgb) -> yuv or rgb -> yuv */
	if (ctx->out.fmt->hw_format >= RGA_COLOR_FMT_YUV422SP)
		dst_info.data.csc_mode =
			rga_get_quantization(ctx->in.fmt->hw_format,
				ctx->out.colorspace, ctx->out.quantization);

	if (ctx->op == V4L2_PORTER_DUFF_CLEAR) {
		/*
		 * Configure the target color to foreground color.
		 */
		dest[(RGA_SRC_FG_COLOR - RGA_MODE_BASE_REG) >> 2] =
			ctx->fill_color;
		dst_vir_info.data.vir_stride = ctx->out.stride >> 2;
		dst_act_info.data.act_height = dst_h - 1;
		dst_act_info.data.act_width = dst_w - 1;

		offsets = rga_get_addr_offset(&ctx->out, dst_x, dst_y,
					      dst_w, dst_h);
		dst_offset = &offsets.left_top;

		goto write_dst;
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
	 * Cacluate the up/down scaling mode/factor.
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
	 * Cacluate the framebuffer virtual strides and active size,
	 * note that the step of vir_stride / vir_width is 4 byte words
	 */
	src_vir_info.data.vir_stride = ctx->in.stride >> 2;
	src_vir_info.data.vir_width = ctx->in.stride >> 2;

	src_act_info.data.act_height = src_h - 1;
	src_act_info.data.act_width = src_w - 1;

	dst_vir_info.data.vir_stride = ctx->out.stride >> 2;
	dst_act_info.data.act_height = dst_h - 1;
	dst_act_info.data.act_width = dst_w - 1;

	/*
	 * Cacluate the source framebuffer base address with offset pixel.
	 */
	src_offsets = rga_get_addr_offset(&ctx->in, src_x, src_y,
					  src_w, src_h);

	/*
	 * Configure the dest framebuffer base address with pixel offset.
	 */
	offsets = rga_get_addr_offset(&ctx->out, dst_x, dst_y, dst_w, dst_h);
	dst_offset = rga_lookup_draw_pos(&offsets, src_info.data.rot_mode,
					 src_info.data.mir_mode);

	dest[(RGA_SRC_Y_RGB_BASE_ADDR - RGA_MODE_BASE_REG) >> 2] =
		src_offsets.left_top.y_off;
	dest[(RGA_SRC_CB_BASE_ADDR - RGA_MODE_BASE_REG) >> 2] =
		src_offsets.left_top.u_off;
	dest[(RGA_SRC_CR_BASE_ADDR - RGA_MODE_BASE_REG) >> 2] =
		src_offsets.left_top.v_off;

	dest[(RGA_SRC_X_FACTOR - RGA_MODE_BASE_REG) >> 2] = x_factor.val;
	dest[(RGA_SRC_Y_FACTOR - RGA_MODE_BASE_REG) >> 2] = y_factor.val;
	dest[(RGA_SRC_VIR_INFO - RGA_MODE_BASE_REG) >> 2] = src_vir_info.val;
	dest[(RGA_SRC_ACT_INFO - RGA_MODE_BASE_REG) >> 2] = src_act_info.val;

	dest[(RGA_SRC_INFO - RGA_MODE_BASE_REG) >> 2] = src_info.val;

write_dst:
	dest[(RGA_DST_Y_RGB_BASE_ADDR - RGA_MODE_BASE_REG) >> 2] =
		dst_offset->y_off;
	dest[(RGA_DST_CB_BASE_ADDR - RGA_MODE_BASE_REG) >> 2] =
		dst_offset->u_off;
	dest[(RGA_DST_CR_BASE_ADDR - RGA_MODE_BASE_REG) >> 2] =
		dst_offset->v_off;

	dest[(RGA_DST_VIR_INFO - RGA_MODE_BASE_REG) >> 2] = dst_vir_info.val;
	dest[(RGA_DST_ACT_INFO - RGA_MODE_BASE_REG) >> 2] = dst_act_info.val;

	dest[(RGA_DST_INFO - RGA_MODE_BASE_REG) >> 2] = dst_info.val;
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

	switch (ctx->op) {
	case V4L2_PORTER_DUFF_CLEAR:
		mode.data.gradient_sat = 1;
		mode.data.render = RGA_MODE_RENDER_RECTANGLE_FILL;
		mode.data.cf_rop4_pat = RGA_MODE_CF_ROP4_SOLID;
		mode.data.bitblt = RGA_MODE_BITBLT_MODE_SRC_TO_DST;
		break;
	case V4L2_PORTER_DUFF_DST:
	case V4L2_PORTER_DUFF_DSTATOP:
	case V4L2_PORTER_DUFF_DSTIN:
	case V4L2_PORTER_DUFF_DSTOUT:
	case V4L2_PORTER_DUFF_DSTOVER:
	case V4L2_PORTER_DUFF_SRCATOP:
	case V4L2_PORTER_DUFF_SRCIN:
	case V4L2_PORTER_DUFF_SRCOUT:
	case V4L2_PORTER_DUFF_SRCOVER:
		mode.data.gradient_sat = 1;
		mode.data.render = RGA_MODE_RENDER_BITBLT;
		mode.data.bitblt = RGA_MODE_BITBLT_MODE_SRC_TO_DST;

		alpha_ctrl0.data.rop_en = 1;
		alpha_ctrl0.data.rop_mode = RGA_ALPHA_ROP_MODE_3;
		alpha_ctrl0.data.rop_select = RGA_ALPHA_SELECT_ALPHA;

		alpha_ctrl1.data.dst_alpha_cal_m0 = RGA_ALPHA_CAL_NORMAL;
		alpha_ctrl1.data.src_alpha_cal_m0 = RGA_ALPHA_CAL_NORMAL;
		alpha_ctrl1.data.dst_alpha_cal_m1 = RGA_ALPHA_CAL_NORMAL;
		alpha_ctrl1.data.src_alpha_cal_m1 = RGA_ALPHA_CAL_NORMAL;
		break;
	default:
		mode.data.gradient_sat = 1;
		mode.data.render = RGA_MODE_RENDER_BITBLT;
		mode.data.bitblt = RGA_MODE_BITBLT_MODE_SRC_TO_DST;
		break;
	}

	switch (ctx->op) {
	case V4L2_PORTER_DUFF_DST:
		/* A=Dst.a */
		alpha_ctrl1.data.src_factor_m1 = RGA_ALPHA_FACTOR_ZERO;

		alpha_ctrl1.data.dst_alpha_m1 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.dst_blend_m1 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.dst_factor_m1 = RGA_ALPHA_FACTOR_ONE;

		/* C=Dst.c */
		alpha_ctrl1.data.src_factor_m0 = RGA_ALPHA_FACTOR_ZERO;

		alpha_ctrl1.data.dst_color_m0 = RGA_ALPHA_COLOR_NORMAL;
		alpha_ctrl1.data.dst_alpha_m0 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.dst_blend_m0 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.dst_factor_m0 = RGA_ALPHA_FACTOR_ONE;
		break;
	case V4L2_PORTER_DUFF_DSTATOP:
		/* A=Src.a */
		alpha_ctrl1.data.src_alpha_m1 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.src_blend_m1 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.src_factor_m1 = RGA_ALPHA_FACTOR_ONE;

		alpha_ctrl1.data.dst_factor_m1 = RGA_ALPHA_FACTOR_ZERO;

		/* C=Src.a*Dst.c+Src.c*(1.0-Dst.a) */
		alpha_ctrl1.data.src_color_m0 = RGA_ALPHA_COLOR_NORMAL;
		alpha_ctrl1.data.src_alpha_m0 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.src_blend_m0 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.src_factor_m0 = RGA_ALPHA_FACTOR_OTHER_REVERSE;

		alpha_ctrl1.data.dst_color_m0 = RGA_ALPHA_COLOR_NORMAL;
		alpha_ctrl1.data.dst_alpha_m0 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.dst_blend_m0 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.dst_factor_m0 = RGA_ALPHA_FACTOR_OTHER;
		break;
	case V4L2_PORTER_DUFF_DSTIN:
		/* A=Dst.a*Src.a */
		alpha_ctrl1.data.src_alpha_m1 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.src_blend_m1 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.src_factor_m1 = RGA_ALPHA_FACTOR_ZERO;

		alpha_ctrl1.data.dst_alpha_m1 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.dst_blend_m1 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.dst_factor_m1 = RGA_ALPHA_FACTOR_OTHER;

		/* C=Dst.c*Src.a */
		alpha_ctrl1.data.src_color_m0 = RGA_ALPHA_COLOR_NORMAL;
		alpha_ctrl1.data.src_alpha_m0 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.src_blend_m0 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.src_factor_m0 = RGA_ALPHA_FACTOR_ZERO;

		alpha_ctrl1.data.dst_color_m0 = RGA_ALPHA_COLOR_NORMAL;
		alpha_ctrl1.data.dst_alpha_m0 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.dst_blend_m0 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.dst_factor_m0 = RGA_ALPHA_FACTOR_OTHER;
		break;
	case V4L2_PORTER_DUFF_DSTOUT:
		/* A=Dst.a*(1.0-Src.a) */
		alpha_ctrl1.data.src_alpha_m1 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.src_blend_m1 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.src_factor_m1 = RGA_ALPHA_FACTOR_ZERO;

		alpha_ctrl1.data.dst_alpha_m1 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.dst_blend_m1 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.dst_factor_m1 = RGA_ALPHA_FACTOR_OTHER_REVERSE;

		/* C=Dst.c*(1.0-Src.a) */
		alpha_ctrl1.data.src_color_m0 = RGA_ALPHA_COLOR_NORMAL;
		alpha_ctrl1.data.src_alpha_m0 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.src_blend_m0 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.src_factor_m0 = RGA_ALPHA_FACTOR_ZERO;

		alpha_ctrl1.data.dst_color_m0 = RGA_ALPHA_COLOR_NORMAL;
		alpha_ctrl1.data.dst_alpha_m0 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.dst_blend_m0 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.dst_factor_m0 = RGA_ALPHA_FACTOR_OTHER_REVERSE;
		break;
	case V4L2_PORTER_DUFF_DSTOVER:
		/* A=Src.a+Dst.a*(1.0-Src.a) */
		alpha_ctrl1.data.src_alpha_m1 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.src_blend_m1 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.src_factor_m1 = RGA_ALPHA_FACTOR_ONE;

		alpha_ctrl1.data.dst_alpha_m1 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.dst_blend_m1 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.dst_factor_m1 = RGA_ALPHA_FACTOR_OTHER_REVERSE;

		/* C=Dst.c+Src.c*(1.0-Dst.a) */
		alpha_ctrl1.data.src_color_m0 = RGA_ALPHA_COLOR_NORMAL;
		alpha_ctrl1.data.src_alpha_m0 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.src_blend_m0 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.src_factor_m0 = RGA_ALPHA_FACTOR_OTHER_REVERSE;

		alpha_ctrl1.data.dst_color_m0 = RGA_ALPHA_COLOR_NORMAL;
		alpha_ctrl1.data.dst_alpha_m0 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.dst_blend_m0 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.dst_factor_m0 = RGA_ALPHA_FACTOR_ONE;
		break;
	case V4L2_PORTER_DUFF_SRCATOP:
		/* A=Dst.a */
		alpha_ctrl1.data.src_factor_m1 = RGA_ALPHA_FACTOR_ZERO;

		alpha_ctrl1.data.dst_alpha_m1 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.dst_blend_m1 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.dst_factor_m1 = RGA_ALPHA_FACTOR_ONE;

		/* C=Dst.a*Src.c+Dst.c*(1.0-Src.a) */
		alpha_ctrl1.data.src_color_m0 = RGA_ALPHA_COLOR_NORMAL;
		alpha_ctrl1.data.src_alpha_m0 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.src_blend_m0 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.src_factor_m0 = RGA_ALPHA_FACTOR_OTHER;

		alpha_ctrl1.data.dst_color_m0 = RGA_ALPHA_COLOR_NORMAL;
		alpha_ctrl1.data.dst_alpha_m0 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.dst_blend_m0 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.dst_factor_m0 = RGA_ALPHA_FACTOR_OTHER_REVERSE;
		break;
	case V4L2_PORTER_DUFF_SRCIN:
		/* A=Src.a*Dst.a */
		alpha_ctrl1.data.src_alpha_m1 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.src_blend_m1 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.src_factor_m1 = RGA_ALPHA_FACTOR_ZERO;

		alpha_ctrl1.data.dst_alpha_m1 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.dst_blend_m1 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.dst_factor_m1 = RGA_ALPHA_FACTOR_OTHER;

		/* C=Src.c*Dst.a */
		alpha_ctrl1.data.src_color_m0 = RGA_ALPHA_COLOR_NORMAL;
		alpha_ctrl1.data.src_alpha_m0 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.src_blend_m0 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.src_factor_m0 = RGA_ALPHA_FACTOR_OTHER;

		alpha_ctrl1.data.dst_color_m0 = RGA_ALPHA_COLOR_NORMAL;
		alpha_ctrl1.data.dst_alpha_m0 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.dst_blend_m0 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.dst_factor_m0 = RGA_ALPHA_FACTOR_ZERO;
		break;
	case V4L2_PORTER_DUFF_SRCOUT:
		/* A=Src.a*(1.0-Dst.a) */
		alpha_ctrl1.data.src_alpha_m1 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.src_blend_m1 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.src_factor_m1 = RGA_ALPHA_FACTOR_OTHER_REVERSE;

		alpha_ctrl1.data.dst_alpha_m1 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.dst_blend_m1 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.dst_factor_m1 = RGA_ALPHA_FACTOR_ZERO;

		/* C=Src.c*(1.0-Dst.a) */
		alpha_ctrl1.data.src_color_m0 = RGA_ALPHA_COLOR_NORMAL;
		alpha_ctrl1.data.src_alpha_m0 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.src_blend_m0 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.src_factor_m0 = RGA_ALPHA_FACTOR_OTHER_REVERSE;

		alpha_ctrl1.data.dst_color_m0 = RGA_ALPHA_COLOR_NORMAL;
		alpha_ctrl1.data.dst_alpha_m0 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.dst_blend_m0 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.dst_factor_m0 = RGA_ALPHA_FACTOR_ZERO;
		break;
	case V4L2_PORTER_DUFF_SRCOVER:
		/* A=Src.a+Dst.a*(1.0-Src.a) */
		alpha_ctrl1.data.src_alpha_m1 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.src_blend_m1 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.src_factor_m1 = RGA_ALPHA_FACTOR_ONE;

		alpha_ctrl1.data.dst_alpha_m1 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.dst_blend_m1 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.dst_factor_m1 = RGA_ALPHA_FACTOR_OTHER_REVERSE;

		/* C=Src.c+Dst.c*(1.0-Src.a) */
		alpha_ctrl1.data.src_color_m0 = RGA_ALPHA_COLOR_NORMAL;
		alpha_ctrl1.data.src_alpha_m0 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.src_blend_m0 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.src_factor_m0 = RGA_ALPHA_FACTOR_ONE;

		alpha_ctrl1.data.dst_color_m0 = RGA_ALPHA_COLOR_NORMAL;
		alpha_ctrl1.data.dst_alpha_m0 = RGA_ALPHA_NORMAL;
		alpha_ctrl1.data.dst_blend_m0 = RGA_ALPHA_BLEND_NORMAL;
		alpha_ctrl1.data.dst_factor_m0 = RGA_ALPHA_FACTOR_OTHER_REVERSE;
		break;
	default:
		break;
	}

	dest[(RGA_ALPHA_CTRL0 - RGA_MODE_BASE_REG) >> 2] = alpha_ctrl0.val;
	dest[(RGA_ALPHA_CTRL1 - RGA_MODE_BASE_REG) >> 2] = alpha_ctrl1.val;

	dest[(RGA_MODE_CTRL - RGA_MODE_BASE_REG) >> 2] = mode.val;
}

void rga_cmd_set(struct rga_ctx *ctx)
{
	struct rockchip_rga *rga = ctx->rga;

	memset(rga->cmdbuf_virt, 0, RGA_CMDBUF_SIZE * 4);

	if (ctx->op != V4L2_PORTER_DUFF_CLEAR) {
		rga_cmd_set_src_addr(ctx, rga->src_mmu_pages);
		/*
		 * Due to hardware bug,
		 * src1 mmu also should be configured when using alpha blending.
		 */
		rga_cmd_set_src1_addr(ctx, rga->dst_mmu_pages);
	}
	rga_cmd_set_dst_addr(ctx, rga->dst_mmu_pages);
	rga_cmd_set_mode(ctx);

	rga_cmd_set_trans_info(ctx);

	rga_write(rga, RGA_CMD_BASE, rga->cmdbuf_phy);
}

void rga_start(struct rockchip_rga *rga)
{
	/* sync CMD buf for RGA */
	dma_sync_single_for_device(rga->dev, rga->cmdbuf_phy,
				   PAGE_SIZE, DMA_BIDIRECTIONAL);

	rga_write(rga, RGA_SYS_CTRL, 0x00);

	rga_write(rga, RGA_SYS_CTRL, 0x22);

	rga_write(rga, RGA_INT, 0x600);

	rga_write(rga, RGA_CMD_CTRL, 0x1);
}
