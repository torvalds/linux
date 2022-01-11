// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2011 - 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Samsung EXYNOS5 SoC series G-Scaler driver
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/bug.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <media/v4l2-ioctl.h>

#include "gsc-core.h"

static const struct gsc_fmt gsc_formats[] = {
	{
		.pixelformat	= V4L2_PIX_FMT_RGB565X,
		.depth		= { 16 },
		.color		= GSC_RGB,
		.num_planes	= 1,
		.num_comp	= 1,
	}, {
		.pixelformat	= V4L2_PIX_FMT_BGR32,
		.depth		= { 32 },
		.color		= GSC_RGB,
		.num_planes	= 1,
		.num_comp	= 1,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YUYV,
		.depth		= { 16 },
		.color		= GSC_YUV422,
		.yorder		= GSC_LSB_Y,
		.corder		= GSC_CBCR,
		.num_planes	= 1,
		.num_comp	= 1,
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_2X8,
	}, {
		.pixelformat	= V4L2_PIX_FMT_UYVY,
		.depth		= { 16 },
		.color		= GSC_YUV422,
		.yorder		= GSC_LSB_C,
		.corder		= GSC_CBCR,
		.num_planes	= 1,
		.num_comp	= 1,
		.mbus_code	= MEDIA_BUS_FMT_UYVY8_2X8,
	}, {
		.pixelformat	= V4L2_PIX_FMT_VYUY,
		.depth		= { 16 },
		.color		= GSC_YUV422,
		.yorder		= GSC_LSB_C,
		.corder		= GSC_CRCB,
		.num_planes	= 1,
		.num_comp	= 1,
		.mbus_code	= MEDIA_BUS_FMT_VYUY8_2X8,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YVYU,
		.depth		= { 16 },
		.color		= GSC_YUV422,
		.yorder		= GSC_LSB_Y,
		.corder		= GSC_CRCB,
		.num_planes	= 1,
		.num_comp	= 1,
		.mbus_code	= MEDIA_BUS_FMT_YVYU8_2X8,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YUV32,
		.depth		= { 32 },
		.color		= GSC_YUV444,
		.yorder		= GSC_LSB_Y,
		.corder		= GSC_CBCR,
		.num_planes	= 1,
		.num_comp	= 1,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YUV422P,
		.depth		= { 16 },
		.color		= GSC_YUV422,
		.yorder		= GSC_LSB_Y,
		.corder		= GSC_CBCR,
		.num_planes	= 1,
		.num_comp	= 3,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV16,
		.depth		= { 16 },
		.color		= GSC_YUV422,
		.yorder		= GSC_LSB_Y,
		.corder		= GSC_CBCR,
		.num_planes	= 1,
		.num_comp	= 2,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV16M,
		.depth		= { 8, 8 },
		.color		= GSC_YUV422,
		.yorder		= GSC_LSB_Y,
		.corder		= GSC_CBCR,
		.num_planes	= 2,
		.num_comp	= 2,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV61,
		.depth		= { 16 },
		.color		= GSC_YUV422,
		.yorder		= GSC_LSB_Y,
		.corder		= GSC_CRCB,
		.num_planes	= 1,
		.num_comp	= 2,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV61M,
		.depth		= { 8, 8 },
		.color		= GSC_YUV422,
		.yorder		= GSC_LSB_Y,
		.corder		= GSC_CRCB,
		.num_planes	= 2,
		.num_comp	= 2,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YUV420,
		.depth		= { 12 },
		.color		= GSC_YUV420,
		.yorder		= GSC_LSB_Y,
		.corder		= GSC_CBCR,
		.num_planes	= 1,
		.num_comp	= 3,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YVU420,
		.depth		= { 12 },
		.color		= GSC_YUV420,
		.yorder		= GSC_LSB_Y,
		.corder		= GSC_CRCB,
		.num_planes	= 1,
		.num_comp	= 3,

	}, {
		.pixelformat	= V4L2_PIX_FMT_NV12,
		.depth		= { 12 },
		.color		= GSC_YUV420,
		.yorder		= GSC_LSB_Y,
		.corder		= GSC_CBCR,
		.num_planes	= 1,
		.num_comp	= 2,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV21,
		.depth		= { 12 },
		.color		= GSC_YUV420,
		.yorder		= GSC_LSB_Y,
		.corder		= GSC_CRCB,
		.num_planes	= 1,
		.num_comp	= 2,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV21M,
		.depth		= { 8, 4 },
		.color		= GSC_YUV420,
		.yorder		= GSC_LSB_Y,
		.corder		= GSC_CRCB,
		.num_planes	= 2,
		.num_comp	= 2,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV12M,
		.depth		= { 8, 4 },
		.color		= GSC_YUV420,
		.yorder		= GSC_LSB_Y,
		.corder		= GSC_CBCR,
		.num_planes	= 2,
		.num_comp	= 2,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YUV420M,
		.depth		= { 8, 2, 2 },
		.color		= GSC_YUV420,
		.yorder		= GSC_LSB_Y,
		.corder		= GSC_CBCR,
		.num_planes	= 3,
		.num_comp	= 3,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YVU420M,
		.depth		= { 8, 2, 2 },
		.color		= GSC_YUV420,
		.yorder		= GSC_LSB_Y,
		.corder		= GSC_CRCB,
		.num_planes	= 3,
		.num_comp	= 3,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV12MT_16X16,
		.depth		= { 8, 4 },
		.color		= GSC_YUV420,
		.yorder		= GSC_LSB_Y,
		.corder		= GSC_CBCR,
		.num_planes	= 2,
		.num_comp	= 2,
	}
};

const struct gsc_fmt *get_format(int index)
{
	if (index >= ARRAY_SIZE(gsc_formats))
		return NULL;

	return (struct gsc_fmt *)&gsc_formats[index];
}

const struct gsc_fmt *find_fmt(u32 *pixelformat, u32 *mbus_code, u32 index)
{
	const struct gsc_fmt *fmt, *def_fmt = NULL;
	unsigned int i;

	if (index >= ARRAY_SIZE(gsc_formats))
		return NULL;

	for (i = 0; i < ARRAY_SIZE(gsc_formats); ++i) {
		fmt = get_format(i);
		if (pixelformat && fmt->pixelformat == *pixelformat)
			return fmt;
		if (mbus_code && fmt->mbus_code == *mbus_code)
			return fmt;
		if (index == i)
			def_fmt = fmt;
	}
	return def_fmt;

}

void gsc_set_frame_size(struct gsc_frame *frame, int width, int height)
{
	frame->f_width	= width;
	frame->f_height	= height;
	frame->crop.width = width;
	frame->crop.height = height;
	frame->crop.left = 0;
	frame->crop.top = 0;
}

int gsc_cal_prescaler_ratio(struct gsc_variant *var, u32 src, u32 dst,
								u32 *ratio)
{
	if ((dst > src) || (dst >= src / var->poly_sc_down_max)) {
		*ratio = 1;
		return 0;
	}

	if ((src / var->poly_sc_down_max / var->pre_sc_down_max) > dst) {
		pr_err("Exceeded maximum downscaling ratio (1/16))");
		return -EINVAL;
	}

	*ratio = (dst > (src / 8)) ? 2 : 4;

	return 0;
}

void gsc_get_prescaler_shfactor(u32 hratio, u32 vratio, u32 *sh)
{
	if (hratio == 4 && vratio == 4)
		*sh = 4;
	else if ((hratio == 4 && vratio == 2) ||
		 (hratio == 2 && vratio == 4))
		*sh = 3;
	else if ((hratio == 4 && vratio == 1) ||
		 (hratio == 1 && vratio == 4) ||
		 (hratio == 2 && vratio == 2))
		*sh = 2;
	else if (hratio == 1 && vratio == 1)
		*sh = 0;
	else
		*sh = 1;
}

void gsc_check_src_scale_info(struct gsc_variant *var,
				struct gsc_frame *s_frame, u32 *wratio,
				 u32 tx, u32 ty, u32 *hratio)
{
	int remainder = 0, walign, halign;

	if (is_yuv420(s_frame->fmt->color)) {
		walign = GSC_SC_ALIGN_4;
		halign = GSC_SC_ALIGN_4;
	} else if (is_yuv422(s_frame->fmt->color)) {
		walign = GSC_SC_ALIGN_4;
		halign = GSC_SC_ALIGN_2;
	} else {
		walign = GSC_SC_ALIGN_2;
		halign = GSC_SC_ALIGN_2;
	}

	remainder = s_frame->crop.width % (*wratio * walign);
	if (remainder) {
		s_frame->crop.width -= remainder;
		gsc_cal_prescaler_ratio(var, s_frame->crop.width, tx, wratio);
		pr_info("cropped src width size is recalculated from %d to %d",
			s_frame->crop.width + remainder, s_frame->crop.width);
	}

	remainder = s_frame->crop.height % (*hratio * halign);
	if (remainder) {
		s_frame->crop.height -= remainder;
		gsc_cal_prescaler_ratio(var, s_frame->crop.height, ty, hratio);
		pr_info("cropped src height size is recalculated from %d to %d",
			s_frame->crop.height + remainder, s_frame->crop.height);
	}
}

int gsc_enum_fmt(struct v4l2_fmtdesc *f)
{
	const struct gsc_fmt *fmt;

	fmt = find_fmt(NULL, NULL, f->index);
	if (!fmt)
		return -EINVAL;

	f->pixelformat = fmt->pixelformat;

	return 0;
}

static int get_plane_info(struct gsc_frame *frm, u32 addr, u32 *index, u32 *ret_addr)
{
	if (frm->addr.y == addr) {
		*index = 0;
		*ret_addr = frm->addr.y;
	} else if (frm->addr.cb == addr) {
		*index = 1;
		*ret_addr = frm->addr.cb;
	} else if (frm->addr.cr == addr) {
		*index = 2;
		*ret_addr = frm->addr.cr;
	} else {
		pr_err("Plane address is wrong");
		return -EINVAL;
	}
	return 0;
}

void gsc_set_prefbuf(struct gsc_dev *gsc, struct gsc_frame *frm)
{
	u32 f_chk_addr, f_chk_len, s_chk_addr, s_chk_len;
	f_chk_addr = f_chk_len = s_chk_addr = s_chk_len = 0;

	f_chk_addr = frm->addr.y;
	f_chk_len = frm->payload[0];
	if (frm->fmt->num_planes == 2) {
		s_chk_addr = frm->addr.cb;
		s_chk_len = frm->payload[1];
	} else if (frm->fmt->num_planes == 3) {
		u32 low_addr, low_plane, mid_addr, mid_plane;
		u32 high_addr, high_plane;
		u32 t_min, t_max;

		t_min = min3(frm->addr.y, frm->addr.cb, frm->addr.cr);
		if (get_plane_info(frm, t_min, &low_plane, &low_addr))
			return;
		t_max = max3(frm->addr.y, frm->addr.cb, frm->addr.cr);
		if (get_plane_info(frm, t_max, &high_plane, &high_addr))
			return;

		mid_plane = 3 - (low_plane + high_plane);
		if (mid_plane == 0)
			mid_addr = frm->addr.y;
		else if (mid_plane == 1)
			mid_addr = frm->addr.cb;
		else if (mid_plane == 2)
			mid_addr = frm->addr.cr;
		else
			return;

		f_chk_addr = low_addr;
		if (mid_addr + frm->payload[mid_plane] - low_addr >
		    high_addr + frm->payload[high_plane] - mid_addr) {
			f_chk_len = frm->payload[low_plane];
			s_chk_addr = mid_addr;
			s_chk_len = high_addr +
					frm->payload[high_plane] - mid_addr;
		} else {
			f_chk_len = mid_addr +
					frm->payload[mid_plane] - low_addr;
			s_chk_addr = high_addr;
			s_chk_len = frm->payload[high_plane];
		}
	}
	pr_debug("f_addr = 0x%08x, f_len = %d, s_addr = 0x%08x, s_len = %d\n",
			f_chk_addr, f_chk_len, s_chk_addr, s_chk_len);
}

int gsc_try_fmt_mplane(struct gsc_ctx *ctx, struct v4l2_format *f)
{
	struct gsc_dev *gsc = ctx->gsc_dev;
	struct gsc_variant *variant = gsc->variant;
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	const struct gsc_fmt *fmt;
	u32 max_w, max_h, mod_x, mod_y;
	u32 min_w, min_h, tmp_w, tmp_h;
	int i;

	pr_debug("user put w: %d, h: %d", pix_mp->width, pix_mp->height);

	fmt = find_fmt(&pix_mp->pixelformat, NULL, 0);
	if (!fmt) {
		pr_err("pixelformat format (0x%X) invalid\n",
						pix_mp->pixelformat);
		return -EINVAL;
	}

	if (pix_mp->field == V4L2_FIELD_ANY)
		pix_mp->field = V4L2_FIELD_NONE;
	else if (pix_mp->field != V4L2_FIELD_NONE) {
		pr_debug("Not supported field order(%d)\n", pix_mp->field);
		return -EINVAL;
	}

	max_w = variant->pix_max->target_rot_dis_w;
	max_h = variant->pix_max->target_rot_dis_h;

	mod_x = ffs(variant->pix_align->org_w) - 1;
	if (is_yuv420(fmt->color))
		mod_y = ffs(variant->pix_align->org_h) - 1;
	else
		mod_y = ffs(variant->pix_align->org_h) - 2;

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		min_w = variant->pix_min->org_w;
		min_h = variant->pix_min->org_h;
	} else {
		min_w = variant->pix_min->target_rot_dis_w;
		min_h = variant->pix_min->target_rot_dis_h;
		pix_mp->colorspace = ctx->out_colorspace;
	}

	pr_debug("mod_x: %d, mod_y: %d, max_w: %d, max_h = %d",
			mod_x, mod_y, max_w, max_h);

	/* To check if image size is modified to adjust parameter against
	   hardware abilities */
	tmp_w = pix_mp->width;
	tmp_h = pix_mp->height;

	v4l_bound_align_image(&pix_mp->width, min_w, max_w, mod_x,
		&pix_mp->height, min_h, max_h, mod_y, 0);
	if (tmp_w != pix_mp->width || tmp_h != pix_mp->height)
		pr_debug("Image size has been modified from %dx%d to %dx%d\n",
			 tmp_w, tmp_h, pix_mp->width, pix_mp->height);

	pix_mp->num_planes = fmt->num_planes;

	if (V4L2_TYPE_IS_OUTPUT(f->type))
		ctx->out_colorspace = pix_mp->colorspace;

	for (i = 0; i < pix_mp->num_planes; ++i) {
		struct v4l2_plane_pix_format *plane_fmt = &pix_mp->plane_fmt[i];
		u32 bpl = plane_fmt->bytesperline;

		if (fmt->num_comp == 1 && /* Packed */
		    (bpl == 0 || (bpl * 8 / fmt->depth[i]) < pix_mp->width))
			bpl = pix_mp->width * fmt->depth[i] / 8;

		if (fmt->num_comp > 1 && /* Planar */
		    (bpl == 0 || bpl < pix_mp->width))
			bpl = pix_mp->width;

		if (i != 0 && fmt->num_comp == 3)
			bpl /= 2;

		plane_fmt->bytesperline = bpl;
		plane_fmt->sizeimage = max(pix_mp->width * pix_mp->height *
					   fmt->depth[i] / 8,
					   plane_fmt->sizeimage);
		pr_debug("[%d]: bpl: %d, sizeimage: %d",
				i, bpl, pix_mp->plane_fmt[i].sizeimage);
	}

	return 0;
}

int gsc_g_fmt_mplane(struct gsc_ctx *ctx, struct v4l2_format *f)
{
	struct gsc_frame *frame;
	struct v4l2_pix_format_mplane *pix_mp;
	int i;

	frame = ctx_get_frame(ctx, f->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	pix_mp = &f->fmt.pix_mp;

	pix_mp->width		= frame->f_width;
	pix_mp->height		= frame->f_height;
	pix_mp->field		= V4L2_FIELD_NONE;
	pix_mp->pixelformat	= frame->fmt->pixelformat;
	pix_mp->num_planes	= frame->fmt->num_planes;
	pix_mp->colorspace = ctx->out_colorspace;

	for (i = 0; i < pix_mp->num_planes; ++i) {
		pix_mp->plane_fmt[i].bytesperline = (frame->f_width *
			frame->fmt->depth[i]) / 8;
		pix_mp->plane_fmt[i].sizeimage =
			 pix_mp->plane_fmt[i].bytesperline * frame->f_height;
	}

	return 0;
}

void gsc_check_crop_change(u32 tmp_w, u32 tmp_h, u32 *w, u32 *h)
{
	if (tmp_w != *w || tmp_h != *h) {
		pr_info("Cropped size has been modified from %dx%d to %dx%d",
							*w, *h, tmp_w, tmp_h);
		*w = tmp_w;
		*h = tmp_h;
	}
}

int gsc_try_selection(struct gsc_ctx *ctx, struct v4l2_selection *s)
{
	struct gsc_frame *f;
	struct gsc_dev *gsc = ctx->gsc_dev;
	struct gsc_variant *variant = gsc->variant;
	u32 mod_x = 0, mod_y = 0, tmp_w, tmp_h;
	u32 min_w, min_h, max_w, max_h;

	if (s->r.top < 0 || s->r.left < 0) {
		pr_err("doesn't support negative values for top & left\n");
		return -EINVAL;
	}
	pr_debug("user put w: %d, h: %d", s->r.width, s->r.height);

	if (s->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		f = &ctx->d_frame;
	else if (s->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		f = &ctx->s_frame;
	else
		return -EINVAL;

	max_w = f->f_width;
	max_h = f->f_height;
	tmp_w = s->r.width;
	tmp_h = s->r.height;

	if (V4L2_TYPE_IS_OUTPUT(s->type)) {
		if ((is_yuv422(f->fmt->color) && f->fmt->num_comp == 1) ||
		    is_rgb(f->fmt->color))
			min_w = 32;
		else
			min_w = 64;
		if ((is_yuv422(f->fmt->color) && f->fmt->num_comp == 3) ||
		    is_yuv420(f->fmt->color))
			min_h = 32;
		else
			min_h = 16;
	} else {
		if (is_yuv420(f->fmt->color) || is_yuv422(f->fmt->color))
			mod_x = ffs(variant->pix_align->target_w) - 1;
		if (is_yuv420(f->fmt->color))
			mod_y = ffs(variant->pix_align->target_h) - 1;
		if (ctx->gsc_ctrls.rotate->val == 90 ||
		    ctx->gsc_ctrls.rotate->val == 270) {
			max_w = f->f_height;
			max_h = f->f_width;
			min_w = variant->pix_min->target_rot_en_w;
			min_h = variant->pix_min->target_rot_en_h;
			tmp_w = s->r.height;
			tmp_h = s->r.width;
		} else {
			min_w = variant->pix_min->target_rot_dis_w;
			min_h = variant->pix_min->target_rot_dis_h;
		}
	}
	pr_debug("mod_x: %d, mod_y: %d, min_w: %d, min_h = %d",
					mod_x, mod_y, min_w, min_h);
	pr_debug("tmp_w : %d, tmp_h : %d", tmp_w, tmp_h);

	v4l_bound_align_image(&tmp_w, min_w, max_w, mod_x,
			      &tmp_h, min_h, max_h, mod_y, 0);

	if (V4L2_TYPE_IS_CAPTURE(s->type) &&
	    (ctx->gsc_ctrls.rotate->val == 90 ||
	     ctx->gsc_ctrls.rotate->val == 270))
		gsc_check_crop_change(tmp_h, tmp_w,
					&s->r.width, &s->r.height);
	else
		gsc_check_crop_change(tmp_w, tmp_h,
					&s->r.width, &s->r.height);


	/* adjust left/top if cropping rectangle is out of bounds */
	/* Need to add code to algin left value with 2's multiple */
	if (s->r.left + tmp_w > max_w)
		s->r.left = max_w - tmp_w;
	if (s->r.top + tmp_h > max_h)
		s->r.top = max_h - tmp_h;

	if ((is_yuv420(f->fmt->color) || is_yuv422(f->fmt->color)) &&
	    s->r.left & 1)
		s->r.left -= 1;

	pr_debug("Aligned l:%d, t:%d, w:%d, h:%d, f_w: %d, f_h: %d",
		 s->r.left, s->r.top, s->r.width, s->r.height, max_w, max_h);

	return 0;
}

int gsc_check_scaler_ratio(struct gsc_variant *var, int sw, int sh, int dw,
			   int dh, int rot, int out_path)
{
	int tmp_w, tmp_h, sc_down_max;

	if (out_path == GSC_DMA)
		sc_down_max = var->sc_down_max;
	else
		sc_down_max = var->local_sc_down;

	if (rot == 90 || rot == 270) {
		tmp_w = dh;
		tmp_h = dw;
	} else {
		tmp_w = dw;
		tmp_h = dh;
	}

	if ((sw / tmp_w) > sc_down_max ||
	    (sh / tmp_h) > sc_down_max ||
	    (tmp_w / sw) > var->sc_up_max ||
	    (tmp_h / sh) > var->sc_up_max)
		return -EINVAL;

	return 0;
}

int gsc_set_scaler_info(struct gsc_ctx *ctx)
{
	struct gsc_scaler *sc = &ctx->scaler;
	struct gsc_frame *s_frame = &ctx->s_frame;
	struct gsc_frame *d_frame = &ctx->d_frame;
	struct gsc_variant *variant = ctx->gsc_dev->variant;
	struct device *dev = &ctx->gsc_dev->pdev->dev;
	int tx, ty;
	int ret;

	ret = gsc_check_scaler_ratio(variant, s_frame->crop.width,
		s_frame->crop.height, d_frame->crop.width, d_frame->crop.height,
		ctx->gsc_ctrls.rotate->val, ctx->out_path);
	if (ret) {
		pr_err("out of scaler range");
		return ret;
	}

	if (ctx->gsc_ctrls.rotate->val == 90 ||
	    ctx->gsc_ctrls.rotate->val == 270) {
		ty = d_frame->crop.width;
		tx = d_frame->crop.height;
	} else {
		tx = d_frame->crop.width;
		ty = d_frame->crop.height;
	}

	if (tx <= 0 || ty <= 0) {
		dev_err(dev, "Invalid target size: %dx%d", tx, ty);
		return -EINVAL;
	}

	ret = gsc_cal_prescaler_ratio(variant, s_frame->crop.width,
				      tx, &sc->pre_hratio);
	if (ret) {
		pr_err("Horizontal scale ratio is out of range");
		return ret;
	}

	ret = gsc_cal_prescaler_ratio(variant, s_frame->crop.height,
				      ty, &sc->pre_vratio);
	if (ret) {
		pr_err("Vertical scale ratio is out of range");
		return ret;
	}

	gsc_check_src_scale_info(variant, s_frame, &sc->pre_hratio,
				 tx, ty, &sc->pre_vratio);

	gsc_get_prescaler_shfactor(sc->pre_hratio, sc->pre_vratio,
				   &sc->pre_shfactor);

	sc->main_hratio = (s_frame->crop.width << 16) / tx;
	sc->main_vratio = (s_frame->crop.height << 16) / ty;

	pr_debug("scaler input/output size : sx = %d, sy = %d, tx = %d, ty = %d",
			s_frame->crop.width, s_frame->crop.height, tx, ty);
	pr_debug("scaler ratio info : pre_shfactor : %d, pre_h : %d",
			sc->pre_shfactor, sc->pre_hratio);
	pr_debug("pre_v :%d, main_h : %d, main_v : %d",
			sc->pre_vratio, sc->main_hratio, sc->main_vratio);

	return 0;
}

static int __gsc_s_ctrl(struct gsc_ctx *ctx, struct v4l2_ctrl *ctrl)
{
	struct gsc_dev *gsc = ctx->gsc_dev;
	struct gsc_variant *variant = gsc->variant;
	unsigned int flags = GSC_DST_FMT | GSC_SRC_FMT;
	int ret = 0;

	if (ctrl->flags & V4L2_CTRL_FLAG_INACTIVE)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		ctx->hflip = ctrl->val;
		break;

	case V4L2_CID_VFLIP:
		ctx->vflip = ctrl->val;
		break;

	case V4L2_CID_ROTATE:
		if ((ctx->state & flags) == flags) {
			ret = gsc_check_scaler_ratio(variant,
					ctx->s_frame.crop.width,
					ctx->s_frame.crop.height,
					ctx->d_frame.crop.width,
					ctx->d_frame.crop.height,
					ctx->gsc_ctrls.rotate->val,
					ctx->out_path);

			if (ret)
				return -EINVAL;
		}

		ctx->rotation = ctrl->val;
		break;

	case V4L2_CID_ALPHA_COMPONENT:
		ctx->d_frame.alpha = ctrl->val;
		break;
	}

	ctx->state |= GSC_PARAMS;
	return 0;
}

static int gsc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gsc_ctx *ctx = ctrl_to_ctx(ctrl);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&ctx->gsc_dev->slock, flags);
	ret = __gsc_s_ctrl(ctx, ctrl);
	spin_unlock_irqrestore(&ctx->gsc_dev->slock, flags);

	return ret;
}

static const struct v4l2_ctrl_ops gsc_ctrl_ops = {
	.s_ctrl = gsc_s_ctrl,
};

int gsc_ctrls_create(struct gsc_ctx *ctx)
{
	if (ctx->ctrls_rdy) {
		pr_err("Control handler of this context was created already");
		return 0;
	}

	v4l2_ctrl_handler_init(&ctx->ctrl_handler, GSC_MAX_CTRL_NUM);

	ctx->gsc_ctrls.rotate = v4l2_ctrl_new_std(&ctx->ctrl_handler,
				&gsc_ctrl_ops, V4L2_CID_ROTATE, 0, 270, 90, 0);
	ctx->gsc_ctrls.hflip = v4l2_ctrl_new_std(&ctx->ctrl_handler,
				&gsc_ctrl_ops, V4L2_CID_HFLIP, 0, 1, 1, 0);
	ctx->gsc_ctrls.vflip = v4l2_ctrl_new_std(&ctx->ctrl_handler,
				&gsc_ctrl_ops, V4L2_CID_VFLIP, 0, 1, 1, 0);
	ctx->gsc_ctrls.global_alpha = v4l2_ctrl_new_std(&ctx->ctrl_handler,
			&gsc_ctrl_ops, V4L2_CID_ALPHA_COMPONENT, 0, 255, 1, 0);

	ctx->ctrls_rdy = ctx->ctrl_handler.error == 0;

	if (ctx->ctrl_handler.error) {
		int err = ctx->ctrl_handler.error;
		v4l2_ctrl_handler_free(&ctx->ctrl_handler);
		pr_err("Failed to create G-Scaler control handlers");
		return err;
	}

	return 0;
}

void gsc_ctrls_delete(struct gsc_ctx *ctx)
{
	if (ctx->ctrls_rdy) {
		v4l2_ctrl_handler_free(&ctx->ctrl_handler);
		ctx->ctrls_rdy = false;
	}
}

/* The color format (num_comp, num_planes) must be already configured. */
int gsc_prepare_addr(struct gsc_ctx *ctx, struct vb2_buffer *vb,
			struct gsc_frame *frame, struct gsc_addr *addr)
{
	int ret = 0;
	u32 pix_size;

	if ((vb == NULL) || (frame == NULL))
		return -EINVAL;

	pix_size = frame->f_width * frame->f_height;

	pr_debug("num_planes= %d, num_comp= %d, pix_size= %d",
		frame->fmt->num_planes, frame->fmt->num_comp, pix_size);

	addr->y = vb2_dma_contig_plane_dma_addr(vb, 0);

	if (frame->fmt->num_planes == 1) {
		switch (frame->fmt->num_comp) {
		case 1:
			addr->cb = 0;
			addr->cr = 0;
			break;
		case 2:
			/* decompose Y into Y/Cb */
			addr->cb = (dma_addr_t)(addr->y + pix_size);
			addr->cr = 0;
			break;
		case 3:
			/* decompose Y into Y/Cb/Cr */
			addr->cb = (dma_addr_t)(addr->y + pix_size);
			if (GSC_YUV420 == frame->fmt->color)
				addr->cr = (dma_addr_t)(addr->cb
						+ (pix_size >> 2));
			else /* 422 */
				addr->cr = (dma_addr_t)(addr->cb
						+ (pix_size >> 1));
			break;
		default:
			pr_err("Invalid the number of color planes");
			return -EINVAL;
		}
	} else {
		if (frame->fmt->num_planes >= 2)
			addr->cb = vb2_dma_contig_plane_dma_addr(vb, 1);

		if (frame->fmt->num_planes == 3)
			addr->cr = vb2_dma_contig_plane_dma_addr(vb, 2);
	}

	if ((frame->fmt->pixelformat == V4L2_PIX_FMT_VYUY) ||
		(frame->fmt->pixelformat == V4L2_PIX_FMT_YVYU) ||
		(frame->fmt->pixelformat == V4L2_PIX_FMT_YVU420) ||
		(frame->fmt->pixelformat == V4L2_PIX_FMT_YVU420M))
		swap(addr->cb, addr->cr);

	pr_debug("ADDR: y= %pad  cb= %pad cr= %pad ret= %d",
		&addr->y, &addr->cb, &addr->cr, ret);

	return ret;
}

static irqreturn_t gsc_irq_handler(int irq, void *priv)
{
	struct gsc_dev *gsc = priv;
	struct gsc_ctx *ctx;
	int gsc_irq;

	gsc_irq = gsc_hw_get_irq_status(gsc);
	gsc_hw_clear_irq(gsc, gsc_irq);

	if (gsc_irq == GSC_IRQ_OVERRUN) {
		pr_err("Local path input over-run interrupt has occurred!\n");
		return IRQ_HANDLED;
	}

	spin_lock(&gsc->slock);

	if (test_and_clear_bit(ST_M2M_PEND, &gsc->state)) {

		gsc_hw_enable_control(gsc, false);

		if (test_and_clear_bit(ST_M2M_SUSPENDING, &gsc->state)) {
			set_bit(ST_M2M_SUSPENDED, &gsc->state);
			wake_up(&gsc->irq_queue);
			goto isr_unlock;
		}
		ctx = v4l2_m2m_get_curr_priv(gsc->m2m.m2m_dev);

		if (!ctx || !ctx->m2m_ctx)
			goto isr_unlock;

		spin_unlock(&gsc->slock);
		gsc_m2m_job_finish(ctx, VB2_BUF_STATE_DONE);

		/* wake_up job_abort, stop_streaming */
		if (ctx->state & GSC_CTX_STOP_REQ) {
			ctx->state &= ~GSC_CTX_STOP_REQ;
			wake_up(&gsc->irq_queue);
		}
		return IRQ_HANDLED;
	}

isr_unlock:
	spin_unlock(&gsc->slock);
	return IRQ_HANDLED;
}

static struct gsc_pix_max gsc_v_100_max = {
	.org_scaler_bypass_w	= 8192,
	.org_scaler_bypass_h	= 8192,
	.org_scaler_input_w	= 4800,
	.org_scaler_input_h	= 3344,
	.real_rot_dis_w		= 4800,
	.real_rot_dis_h		= 3344,
	.real_rot_en_w		= 2047,
	.real_rot_en_h		= 2047,
	.target_rot_dis_w	= 4800,
	.target_rot_dis_h	= 3344,
	.target_rot_en_w	= 2016,
	.target_rot_en_h	= 2016,
};

static struct gsc_pix_max gsc_v_5250_max = {
	.org_scaler_bypass_w	= 8192,
	.org_scaler_bypass_h	= 8192,
	.org_scaler_input_w	= 4800,
	.org_scaler_input_h	= 3344,
	.real_rot_dis_w		= 4800,
	.real_rot_dis_h		= 3344,
	.real_rot_en_w		= 2016,
	.real_rot_en_h		= 2016,
	.target_rot_dis_w	= 4800,
	.target_rot_dis_h	= 3344,
	.target_rot_en_w	= 2016,
	.target_rot_en_h	= 2016,
};

static struct gsc_pix_max gsc_v_5420_max = {
	.org_scaler_bypass_w	= 8192,
	.org_scaler_bypass_h	= 8192,
	.org_scaler_input_w	= 4800,
	.org_scaler_input_h	= 3344,
	.real_rot_dis_w		= 4800,
	.real_rot_dis_h		= 3344,
	.real_rot_en_w		= 2048,
	.real_rot_en_h		= 2048,
	.target_rot_dis_w	= 4800,
	.target_rot_dis_h	= 3344,
	.target_rot_en_w	= 2016,
	.target_rot_en_h	= 2016,
};

static struct gsc_pix_max gsc_v_5433_max = {
	.org_scaler_bypass_w	= 8192,
	.org_scaler_bypass_h	= 8192,
	.org_scaler_input_w	= 4800,
	.org_scaler_input_h	= 3344,
	.real_rot_dis_w		= 4800,
	.real_rot_dis_h		= 3344,
	.real_rot_en_w		= 2047,
	.real_rot_en_h		= 2047,
	.target_rot_dis_w	= 4800,
	.target_rot_dis_h	= 3344,
	.target_rot_en_w	= 2016,
	.target_rot_en_h	= 2016,
};

static struct gsc_pix_min gsc_v_100_min = {
	.org_w			= 64,
	.org_h			= 32,
	.real_w			= 64,
	.real_h			= 32,
	.target_rot_dis_w	= 64,
	.target_rot_dis_h	= 32,
	.target_rot_en_w	= 32,
	.target_rot_en_h	= 16,
};

static struct gsc_pix_align gsc_v_100_align = {
	.org_h			= 16,
	.org_w			= 16, /* yuv420 : 16, others : 8 */
	.offset_h		= 2,  /* yuv420/422 : 2, others : 1 */
	.real_w			= 16, /* yuv420/422 : 4~16, others : 2~8 */
	.real_h			= 16, /* yuv420 : 4~16, others : 1 */
	.target_w		= 2,  /* yuv420/422 : 2, others : 1 */
	.target_h		= 2,  /* yuv420 : 2, others : 1 */
};

static struct gsc_variant gsc_v_100_variant = {
	.pix_max		= &gsc_v_100_max,
	.pix_min		= &gsc_v_100_min,
	.pix_align		= &gsc_v_100_align,
	.in_buf_cnt		= 32,
	.out_buf_cnt		= 32,
	.sc_up_max		= 8,
	.sc_down_max		= 16,
	.poly_sc_down_max	= 4,
	.pre_sc_down_max	= 4,
	.local_sc_down		= 2,
};

static struct gsc_variant gsc_v_5250_variant = {
	.pix_max		= &gsc_v_5250_max,
	.pix_min		= &gsc_v_100_min,
	.pix_align		= &gsc_v_100_align,
	.in_buf_cnt		= 32,
	.out_buf_cnt		= 32,
	.sc_up_max		= 8,
	.sc_down_max		= 16,
	.poly_sc_down_max	= 4,
	.pre_sc_down_max	= 4,
	.local_sc_down		= 2,
};

static struct gsc_variant gsc_v_5420_variant = {
	.pix_max		= &gsc_v_5420_max,
	.pix_min		= &gsc_v_100_min,
	.pix_align		= &gsc_v_100_align,
	.in_buf_cnt		= 32,
	.out_buf_cnt		= 32,
	.sc_up_max		= 8,
	.sc_down_max		= 16,
	.poly_sc_down_max	= 4,
	.pre_sc_down_max	= 4,
	.local_sc_down		= 2,
};

static struct gsc_variant gsc_v_5433_variant = {
	.pix_max		= &gsc_v_5433_max,
	.pix_min		= &gsc_v_100_min,
	.pix_align		= &gsc_v_100_align,
	.in_buf_cnt		= 32,
	.out_buf_cnt		= 32,
	.sc_up_max		= 8,
	.sc_down_max		= 16,
	.poly_sc_down_max	= 4,
	.pre_sc_down_max	= 4,
	.local_sc_down		= 2,
};

static struct gsc_driverdata gsc_v_100_drvdata = {
	.variant = {
		[0] = &gsc_v_100_variant,
		[1] = &gsc_v_100_variant,
		[2] = &gsc_v_100_variant,
		[3] = &gsc_v_100_variant,
	},
	.num_entities = 4,
	.clk_names = { "gscl" },
	.num_clocks = 1,
};

static struct gsc_driverdata gsc_v_5250_drvdata = {
	.variant = {
		[0] = &gsc_v_5250_variant,
		[1] = &gsc_v_5250_variant,
		[2] = &gsc_v_5250_variant,
		[3] = &gsc_v_5250_variant,
	},
	.num_entities = 4,
	.clk_names = { "gscl" },
	.num_clocks = 1,
};

static struct gsc_driverdata gsc_v_5420_drvdata = {
	.variant = {
		[0] = &gsc_v_5420_variant,
		[1] = &gsc_v_5420_variant,
	},
	.num_entities = 2,
	.clk_names = { "gscl" },
	.num_clocks = 1,
};

static struct gsc_driverdata gsc_5433_drvdata = {
	.variant = {
		[0] = &gsc_v_5433_variant,
		[1] = &gsc_v_5433_variant,
		[2] = &gsc_v_5433_variant,
	},
	.num_entities = 3,
	.clk_names = { "pclk", "aclk", "aclk_xiu", "aclk_gsclbend" },
	.num_clocks = 4,
};

static const struct of_device_id exynos_gsc_match[] = {
	{
		.compatible = "samsung,exynos5250-gsc",
		.data = &gsc_v_5250_drvdata,
	},
	{
		.compatible = "samsung,exynos5420-gsc",
		.data = &gsc_v_5420_drvdata,
	},
	{
		.compatible = "samsung,exynos5433-gsc",
		.data = &gsc_5433_drvdata,
	},
	{
		.compatible = "samsung,exynos5-gsc",
		.data = &gsc_v_100_drvdata,
	},
	{},
};
MODULE_DEVICE_TABLE(of, exynos_gsc_match);

static int gsc_probe(struct platform_device *pdev)
{
	struct gsc_dev *gsc;
	struct device *dev = &pdev->dev;
	const struct gsc_driverdata *drv_data = of_device_get_match_data(dev);
	int irq;
	int ret;
	int i;

	gsc = devm_kzalloc(dev, sizeof(struct gsc_dev), GFP_KERNEL);
	if (!gsc)
		return -ENOMEM;

	ret = of_alias_get_id(pdev->dev.of_node, "gsc");
	if (ret < 0)
		return ret;

	if (drv_data == &gsc_v_100_drvdata)
		dev_info(dev, "compatible 'exynos5-gsc' is deprecated\n");

	gsc->id = ret;
	if (gsc->id >= drv_data->num_entities) {
		dev_err(dev, "Invalid platform device id: %d\n", gsc->id);
		return -EINVAL;
	}

	gsc->num_clocks = drv_data->num_clocks;
	gsc->variant = drv_data->variant[gsc->id];
	gsc->pdev = pdev;

	init_waitqueue_head(&gsc->irq_queue);
	spin_lock_init(&gsc->slock);
	mutex_init(&gsc->lock);

	gsc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gsc->regs))
		return PTR_ERR(gsc->regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	for (i = 0; i < gsc->num_clocks; i++) {
		gsc->clock[i] = devm_clk_get(dev, drv_data->clk_names[i]);
		if (IS_ERR(gsc->clock[i])) {
			dev_err(dev, "failed to get clock: %s\n",
				drv_data->clk_names[i]);
			return PTR_ERR(gsc->clock[i]);
		}
	}

	for (i = 0; i < gsc->num_clocks; i++) {
		ret = clk_prepare_enable(gsc->clock[i]);
		if (ret) {
			dev_err(dev, "clock prepare failed for clock: %s\n",
				drv_data->clk_names[i]);
			while (--i >= 0)
				clk_disable_unprepare(gsc->clock[i]);
			return ret;
		}
	}

	ret = devm_request_irq(dev, irq, gsc_irq_handler,
			       0, pdev->name, gsc);
	if (ret) {
		dev_err(dev, "failed to install irq (%d)\n", ret);
		goto err_clk;
	}

	ret = v4l2_device_register(dev, &gsc->v4l2_dev);
	if (ret)
		goto err_clk;

	ret = gsc_register_m2m_device(gsc);
	if (ret)
		goto err_v4l2;

	platform_set_drvdata(pdev, gsc);

	gsc_hw_set_sw_reset(gsc);
	gsc_wait_reset(gsc);

	vb2_dma_contig_set_max_seg_size(dev, DMA_BIT_MASK(32));

	dev_dbg(dev, "gsc-%d registered successfully\n", gsc->id);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;

err_v4l2:
	v4l2_device_unregister(&gsc->v4l2_dev);
err_clk:
	for (i = gsc->num_clocks - 1; i >= 0; i--)
		clk_disable_unprepare(gsc->clock[i]);
	return ret;
}

static int gsc_remove(struct platform_device *pdev)
{
	struct gsc_dev *gsc = platform_get_drvdata(pdev);
	int i;

	gsc_unregister_m2m_device(gsc);
	v4l2_device_unregister(&gsc->v4l2_dev);

	vb2_dma_contig_clear_max_seg_size(&pdev->dev);

	pm_runtime_disable(&pdev->dev);

	if (!pm_runtime_status_suspended(&pdev->dev))
		for (i = 0; i < gsc->num_clocks; i++)
			clk_disable_unprepare(gsc->clock[i]);

	pm_runtime_set_suspended(&pdev->dev);

	dev_dbg(&pdev->dev, "%s driver unloaded\n", pdev->name);
	return 0;
}

#ifdef CONFIG_PM
static int gsc_m2m_suspend(struct gsc_dev *gsc)
{
	unsigned long flags;
	int timeout;

	spin_lock_irqsave(&gsc->slock, flags);
	if (!gsc_m2m_pending(gsc)) {
		spin_unlock_irqrestore(&gsc->slock, flags);
		return 0;
	}
	clear_bit(ST_M2M_SUSPENDED, &gsc->state);
	set_bit(ST_M2M_SUSPENDING, &gsc->state);
	spin_unlock_irqrestore(&gsc->slock, flags);

	timeout = wait_event_timeout(gsc->irq_queue,
			     test_bit(ST_M2M_SUSPENDED, &gsc->state),
			     GSC_SHUTDOWN_TIMEOUT);

	clear_bit(ST_M2M_SUSPENDING, &gsc->state);
	return timeout == 0 ? -EAGAIN : 0;
}

static void gsc_m2m_resume(struct gsc_dev *gsc)
{
	struct gsc_ctx *ctx;
	unsigned long flags;

	spin_lock_irqsave(&gsc->slock, flags);
	/* Clear for full H/W setup in first run after resume */
	ctx = gsc->m2m.ctx;
	gsc->m2m.ctx = NULL;
	spin_unlock_irqrestore(&gsc->slock, flags);

	if (test_and_clear_bit(ST_M2M_SUSPENDED, &gsc->state))
		gsc_m2m_job_finish(ctx, VB2_BUF_STATE_ERROR);
}

static int gsc_runtime_resume(struct device *dev)
{
	struct gsc_dev *gsc = dev_get_drvdata(dev);
	int ret = 0;
	int i;

	pr_debug("gsc%d: state: 0x%lx\n", gsc->id, gsc->state);

	for (i = 0; i < gsc->num_clocks; i++) {
		ret = clk_prepare_enable(gsc->clock[i]);
		if (ret) {
			while (--i >= 0)
				clk_disable_unprepare(gsc->clock[i]);
			return ret;
		}
	}

	gsc_hw_set_sw_reset(gsc);
	gsc_wait_reset(gsc);
	gsc_m2m_resume(gsc);

	return 0;
}

static int gsc_runtime_suspend(struct device *dev)
{
	struct gsc_dev *gsc = dev_get_drvdata(dev);
	int ret = 0;
	int i;

	ret = gsc_m2m_suspend(gsc);
	if (ret)
		return ret;

	for (i = gsc->num_clocks - 1; i >= 0; i--)
		clk_disable_unprepare(gsc->clock[i]);

	pr_debug("gsc%d: state: 0x%lx\n", gsc->id, gsc->state);
	return ret;
}
#endif

static const struct dev_pm_ops gsc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(gsc_runtime_suspend, gsc_runtime_resume, NULL)
};

static struct platform_driver gsc_driver = {
	.probe		= gsc_probe,
	.remove		= gsc_remove,
	.driver = {
		.name	= GSC_MODULE_NAME,
		.pm	= &gsc_pm_ops,
		.of_match_table = exynos_gsc_match,
	}
};

module_platform_driver(gsc_driver);

MODULE_AUTHOR("Hyunwong Kim <khw0178.kim@samsung.com>");
MODULE_DESCRIPTION("Samsung EXYNOS5 Soc series G-Scaler driver");
MODULE_LICENSE("GPL");
