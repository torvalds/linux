/* linux/drivers/media/video/samsung/fimc/fimc_overlay.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * V4L2 Overlay device support file for Samsung Camera Interface (FIMC) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/slab.h>
#include <linux/bootmem.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <plat/media.h>

#include "fimc.h"

int fimc_try_fmt_overlay(struct file *filp, void *fh, struct v4l2_format *f)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ctx_id = ((struct fimc_prv_data *)fh)->ctx_id;
	struct fimc_ctx *ctx;

	u32 is_rotate = 0;
	ctx = &ctrl->out->ctx[ctx_id];

	fimc_info1("%s: top(%d) left(%d) width(%d) height(%d)\n", __func__,
			f->fmt.win.w.top, f->fmt.win.w.left,
			f->fmt.win.w.width, f->fmt.win.w.height);

	if (ctx->overlay.mode == FIMC_OVLY_NONE_SINGLE_BUF ||
		(ctx->overlay.mode == FIMC_OVLY_NONE_MULTI_BUF))
		return 0;

	/* Check Overlay Size : Overlay size must be smaller than LCD size. */
	is_rotate = fimc_mapping_rot_flip(ctx->rotate, ctx->flip);
	if (is_rotate & FIMC_ROT) {	/* Landscape mode */
		if (f->fmt.win.w.width > ctrl->fb.lcd_vres) {
			fimc_warn("The width is changed %d -> %d\n",
				f->fmt.win.w.width, ctrl->fb.lcd_vres);
				f->fmt.win.w.width = ctrl->fb.lcd_vres;
		}

		if (f->fmt.win.w.height > ctrl->fb.lcd_hres) {
			fimc_warn("The height is changed %d -> %d\n",
				f->fmt.win.w.height, ctrl->fb.lcd_hres);
				f->fmt.win.w.height = ctrl->fb.lcd_hres;
		}
	} else {			/* Portrait mode */
		if (f->fmt.win.w.width > ctrl->fb.lcd_hres) {
			fimc_warn("The width is changed %d -> %d\n",
				f->fmt.win.w.width, ctrl->fb.lcd_hres);
				f->fmt.win.w.width = ctrl->fb.lcd_hres;
		}

		if (f->fmt.win.w.height > ctrl->fb.lcd_vres) {
			fimc_warn("The height is changed %d -> %d\n",
				f->fmt.win.w.height, ctrl->fb.lcd_vres);
				f->fmt.win.w.height = ctrl->fb.lcd_vres;
		}
	}

	return 0;
}

int fimc_g_fmt_vid_overlay(struct file *filp, void *fh, struct v4l2_format *f)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ctx_id = ((struct fimc_prv_data *)fh)->ctx_id;
	struct fimc_ctx *ctx;

	ctx = &ctrl->out->ctx[ctx_id];

	fimc_info1("%s: called\n", __func__);

	f->fmt.win = ctx->win;

	return 0;
}

static int fimc_check_pos(struct fimc_control *ctrl,
			  struct fimc_ctx *ctx,
			  struct v4l2_format *f)
{
	if (ctx->win.w.width != f->fmt.win.w.width) {
		fimc_err("%s: cannot change width(%d,%d)\n", __func__,
				ctx->win.w.width, f->fmt.win.w.width);
		return -EINVAL;
	} else if (ctx->win.w.height != f->fmt.win.w.height) {
		fimc_err("%s: cannot change height(%d,%d)\n", __func__,
				ctx->win.w.height, f->fmt.win.w.height);
		return -EINVAL;
	}

	return 0;
}

static int fimc_change_fifo_position(struct fimc_control *ctrl,
				     struct fimc_ctx *ctx) {
	struct v4l2_rect fimd_rect;
	struct s3cfb_user_window window;
	int ret = -1;

	memset(&fimd_rect, 0, sizeof(struct v4l2_rect));

	ret = fimc_fimd_rect(ctrl, ctx, &fimd_rect);
	if (ret < 0) {
		fimc_err("fimc_fimd_rect fail\n");
		return -EINVAL;
	}

	/* Update WIN position */
	window.x = fimd_rect.left;
	window.y = fimd_rect.top;
	ret = s3cfb_direct_ioctl(ctrl->id, S3CFB_WIN_POSITION,
			(unsigned long)&window);
	if (ret < 0) {
		fimc_err("direct_ioctl(S3CFB_WIN_POSITION) fail\n");
		return -EINVAL;
	}

	return 0;
}

int fimc_s_fmt_vid_overlay(struct file *filp, void *fh, struct v4l2_format *f)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ctx_id = ((struct fimc_prv_data *)fh)->ctx_id;
	struct fimc_ctx *ctx;
	int ret = -1;
	ctx = &ctrl->out->ctx[ctx_id];

	fimc_info1("%s: called\n", __func__);

	switch (ctx->status) {
	case FIMC_STREAMON:
		ret = fimc_check_pos(ctrl, ctx, f);
		if (ret < 0) {
			fimc_err("When FIMC is running, "
				"you can only move the position.\n");
			return -EBUSY;
		}

		ret = fimc_try_fmt_overlay(filp, fh, f);
		if (ret < 0)
			return ret;

		ctx->win = f->fmt.win;
		fimc_change_fifo_position(ctrl, ctx);

		break;
	case FIMC_STREAMOFF:
		ret = fimc_try_fmt_overlay(filp, fh, f);
		if (ret < 0)
			return ret;
		ctx->win = f->fmt.win;

		break;

	default:
		fimc_err("FIMC is running\n");
		fimc_err("%s::FIMC is running(%d)\n", __func__, ctx->status);
		return -EBUSY;
	}

	return ret;
}

int fimc_g_fbuf(struct file *filp, void *fh, struct v4l2_framebuffer *fb)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ctx_id = ((struct fimc_prv_data *)fh)->ctx_id;
	struct fimc_ctx *ctx;
	u32 bpp = 1, format;

	ctx = &ctrl->out->ctx[ctx_id];

	fimc_info1("%s: called\n", __func__);

	fb->capability = ctx->fbuf.capability;
	fb->flags = 0;
	fb->base = ctx->fbuf.base;

	fb->fmt.width = ctx->fbuf.fmt.width;
	fb->fmt.height = ctx->fbuf.fmt.height;
	fb->fmt.pixelformat = ctx->fbuf.fmt.pixelformat;
	format = ctx->fbuf.fmt.pixelformat;

	switch (format) {
	case V4L2_PIX_FMT_YUV420: /* fall through */
	case V4L2_PIX_FMT_YVU420: /* fall through */
	case V4L2_PIX_FMT_NV12:
		bpp = 1;
		break;
	case V4L2_PIX_FMT_RGB565:
		bpp = 2;
		break;
	case V4L2_PIX_FMT_RGB32:
		bpp = 4;
		break;
	}

	ctx->fbuf.fmt.bytesperline = fb->fmt.width * bpp;
	fb->fmt.bytesperline = ctx->fbuf.fmt.bytesperline;
	fb->fmt.sizeimage = ctx->fbuf.fmt.sizeimage;
	fb->fmt.colorspace = V4L2_COLORSPACE_SMPTE170M;
	fb->fmt.priv = 0;

	return 0;
}

int fimc_s_fbuf(struct file *filp, void *fh, struct v4l2_framebuffer *fb)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ctx_id = ((struct fimc_prv_data *)fh)->ctx_id;
	struct fimc_ctx *ctx;
	u32 bpp = 1;
	u32 format = fb->fmt.pixelformat;
	ctx = &ctrl->out->ctx[ctx_id];

	fimc_info1("%s: called. width(%d), height(%d)\n",
			__func__, fb->fmt.width, fb->fmt.height);

	ctx->fbuf.capability = V4L2_FBUF_CAP_EXTERNOVERLAY;
	ctx->fbuf.flags = 0;
	ctx->fbuf.base = fb->base;

	if (ctx->overlay.mode == FIMC_OVLY_NONE_MULTI_BUF) {
		ctx->fbuf.fmt.width = fb->fmt.width;
		ctx->fbuf.fmt.height = fb->fmt.height;
		ctx->fbuf.fmt.pixelformat = fb->fmt.pixelformat;

		switch (format) {
		case V4L2_PIX_FMT_YUV420: /* fall through */
		case V4L2_PIX_FMT_NV12:
			bpp = 1;
			break;
		case V4L2_PIX_FMT_RGB565:
			bpp = 2;
			break;
		case V4L2_PIX_FMT_RGB32:
			bpp = 4;
			break;
		}

		ctx->fbuf.fmt.bytesperline = fb->fmt.width * bpp;
		ctx->fbuf.fmt.sizeimage = fb->fmt.sizeimage;
		ctx->fbuf.fmt.colorspace = V4L2_COLORSPACE_SMPTE170M;
		ctx->fbuf.fmt.priv = 0;
	} else if (fb->base) {
		ctx->fbuf.fmt.width = fb->fmt.width;
		ctx->fbuf.fmt.height = fb->fmt.height;
		ctx->fbuf.fmt.pixelformat = fb->fmt.pixelformat;

		switch (format) {
		case V4L2_PIX_FMT_YUV420:	/* fall through */
		case V4L2_PIX_FMT_YVU420:	/* fall through */
		case V4L2_PIX_FMT_NV12:
			bpp = 1;
			break;
		case V4L2_PIX_FMT_RGB565:
			bpp = 2;
			break;
		case V4L2_PIX_FMT_RGB32:
			bpp = 4;
			break;
		}

		ctx->fbuf.fmt.bytesperline = fb->fmt.width * bpp;
		ctx->fbuf.fmt.sizeimage = fb->fmt.sizeimage;
		ctx->fbuf.fmt.colorspace = V4L2_COLORSPACE_SMPTE170M;
		ctx->fbuf.fmt.priv = 0;

		ctx->overlay.mode = FIMC_OVLY_NONE_SINGLE_BUF;
	} else {
		ctx->overlay.mode = FIMC_OVLY_NOT_FIXED;
	}

	return 0;
}
