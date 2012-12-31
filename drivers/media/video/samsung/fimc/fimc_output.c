/* linux/drivers/media/video/samsung/fimc/fimc_output.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * V4L2 Output device support file for Samsung Camera Interface (FIMC) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/slab.h>
#include <linux/bootmem.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/videodev2.h>
#include <linux/videodev2_samsung.h>
#include <media/videobuf-core.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/mman.h>
#include <plat/media.h>
#include <linux/clk.h>
#include <linux/delay.h>

#include "fimc.h"
#include "fimc-ipc.h"

static __u32 fimc_get_pixel_format_type(__u32 pixelformat)
{
	switch (pixelformat) {
	case V4L2_PIX_FMT_RGB32:
	case V4L2_PIX_FMT_RGB565:
		return FIMC_RGB;

	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_NV12T:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		return FIMC_YUV420;

	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
	case V4L2_PIX_FMT_YUV422P:
		return FIMC_YUV422;

	default:
		return FIMC_YUV444;
	}
}

void fimc_outdev_set_src_addr(struct fimc_control *ctrl, dma_addr_t *base)
{
	fimc_hwset_addr_change_disable(ctrl);
	fimc_hwset_input_address(ctrl, base);
	fimc_hwset_addr_change_enable(ctrl);
}

int fimc_outdev_start_camif(void *param)
{
	struct fimc_control *ctrl = (struct fimc_control *)param;

	fimc_hwset_start_scaler(ctrl);
	fimc_hwset_enable_capture(ctrl, 0);	/* bypass disable */
	fimc_hwset_start_input_dma(ctrl);

	return 0;
}

static int fimc_outdev_stop_camif(void *param)
{
	struct fimc_control *ctrl = (struct fimc_control *)param;

	fimc_hwset_stop_input_dma(ctrl);
	fimc_hwset_disable_autoload(ctrl);
	fimc_hwset_stop_scaler(ctrl);
	fimc_hwset_disable_capture(ctrl);

	return 0;
}

static int fimc_outdev_stop_fifo(struct fimc_control *ctrl,
				 struct fimc_ctx *ctx)
{
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
	int ret = -1;

	fimc_dbg("%s: called\n", __func__);

	if (pdata->hw_ver == 0x40) {		/* to support C100 */
		ret = ctrl->fb.close_fifo(ctrl->id, fimc_outdev_stop_camif,
				(void *)ctrl);
		if (ret < 0)
			fimc_err("FIMD FIFO close fail\n");
	} else if ((pdata->hw_ver == 0x43) || (pdata->hw_ver >= 0x50)) {
		ret = ctrl->fb.close_fifo(ctrl->id, NULL, NULL);
		if (ret < 0)
			fimc_err("FIMD FIFO close fail\n");
		fimc_hw_wait_winoff(ctrl);
		fimc_outdev_stop_camif(ctrl);
		fimc_hw_wait_stop_input_dma(ctrl);
#if defined(CONFIG_VIDEO_IPC)
		if (ctx->pix.field == V4L2_FIELD_INTERLACED_TB)
			ipc_stop();
#endif
	}

	return 0;
}

int fimc_outdev_stop_streaming(struct fimc_control *ctrl, struct fimc_ctx *ctx)
{
	int ret = 0;

	fimc_dbg("%s: called\n", __func__);

	switch (ctx->overlay.mode) {
	case FIMC_OVLY_FIFO:
		ctx->status = FIMC_READY_OFF;
		fimc_outdev_stop_fifo(ctrl, ctx);
		break;
	case FIMC_OVLY_DMA_AUTO:		/* fall through */
	case FIMC_OVLY_DMA_MANUAL:
		if (ctx->status == FIMC_STREAMON_IDLE)
			ctx->status = FIMC_STREAMOFF;
		else
			ctx->status = FIMC_READY_OFF;
		break;
	case FIMC_OVLY_NONE_SINGLE_BUF:		/* fall through */
	case FIMC_OVLY_NONE_MULTI_BUF:
		if (ctx->status == FIMC_STREAMON_IDLE)
			ctx->status = FIMC_STREAMOFF;
		else
			ctx->status = FIMC_READY_OFF;

		ret = wait_event_timeout(ctrl->wq,
					(ctx->status == FIMC_STREAMOFF),
					FIMC_ONESHOT_TIMEOUT);
		if (ret == 0) {
			if (ctrl->out == NULL) {
				fimc_err("%s: ctrl->out is changed to null\n",
						__func__);
				return -EINVAL;
			}
			fimc_dump_context(ctrl, ctx);
			fimc_err("fail %s: %d\n", __func__, ctx->ctx_num);
		}

		break;
	default:
		break;
	}

	return 0;
}

static int fimc_outdev_dma_auto_dst_resize(struct v4l2_rect *org)
{
	if (org->width % 16)
		org->width = org->width + 16 - (org->width % 16);

	return 0;
}

int fimc_outdev_resume_dma(struct fimc_control *ctrl, struct fimc_ctx *ctx)
{
	struct v4l2_rect fimd_rect, fimd_rect_virtual;
	struct fb_var_screeninfo var;
	struct s3cfb_user_window window;
	int ret = -1, idx;
	u32 id = ctrl->id;

	memset(&fimd_rect, 0, sizeof(struct v4l2_rect));
	ret = fimc_fimd_rect(ctrl, ctx, &fimd_rect);
	if (ret < 0) {
		fimc_err("fimc_fimd_rect fail\n");
		return -EINVAL;
	}

	/* Support any size */
	memcpy(&fimd_rect_virtual, &fimd_rect, sizeof(fimd_rect));
	fimc_outdev_dma_auto_dst_resize(&fimd_rect_virtual);

	/* Get WIN var_screeninfo */
	ret = s3cfb_direct_ioctl(id, FBIOGET_VSCREENINFO,
						(unsigned long)&var);
	if (ret < 0) {
		fimc_err("direct_ioctl(FBIOGET_VSCREENINFO) fail\n");
		return -EINVAL;
	}

	/* window path : DMA */
	ret = s3cfb_direct_ioctl(id, S3CFB_SET_WIN_PATH, DATA_PATH_DMA);
	if (ret < 0) {
		fimc_err("direct_ioctl(S3CFB_SET_WIN_PATH) fail\n");
		return -EINVAL;
	}

	/* Don't allocate the memory. */
	ret = s3cfb_direct_ioctl(id, S3CFB_SET_WIN_MEM, DMA_MEM_OTHER);
	if (ret < 0) {
		fimc_err("direct_ioctl(S3CFB_SET_WIN_MEM) fail\n");
		return -EINVAL;
	}

	/* Update WIN size */
	var.xres_virtual = fimd_rect_virtual.width;
	var.yres_virtual = fimd_rect_virtual.height;
	var.xres = fimd_rect.width;
	var.yres = fimd_rect.height;

	ret = s3cfb_direct_ioctl(id, FBIOPUT_VSCREENINFO,
						(unsigned long)&var);
	if (ret < 0) {
		fimc_err("direct_ioctl(FBIOPUT_VSCREENINFO) fail\n");
		return -EINVAL;
	}

	/* Update WIN position */
	window.x = fimd_rect.left;
	window.y = fimd_rect.top;
	ret = s3cfb_direct_ioctl(id, S3CFB_WIN_POSITION,
			(unsigned long)&window);
	if (ret < 0) {
		fimc_err("direct_ioctl(S3CFB_WIN_POSITION) fail\n");
		return -EINVAL;
	}

	idx = ctx->outq[0];
	if (idx == -1) {
		fimc_err("out going queue is empty.\n");
		return -EINVAL;
	}

	ret = s3cfb_direct_ioctl(ctrl->id, S3CFB_SET_WIN_ADDR,
			(unsigned long)ctx->dst[idx].base[FIMC_ADDR_Y]);
	if (ret < 0) {
		fimc_err("direct_ioctl(S3CFB_SET_WIN_ADDR) fail\n");
		return -EINVAL;
	}

#if 0
	/* Remarked for better screen display
	 * when dynamic screen size change is requested
	 */
	ret = s3cfb_direct_ioctl(ctrl->id, S3CFB_SET_WIN_ON,
							(unsigned long)NULL);
	if (ret < 0) {
		fimc_err("direct_ioctl(S3CFB_SET_WIN_ON) fail\n");
		return -EINVAL;
	}
#endif

	ctrl->fb.is_enable = 1;

	return 0;
}

static void fimc_init_out_buf(struct fimc_ctx *ctx)
{
	int i;

	for (i = 0; i < FIMC_OUTBUFS; i++) {
		ctx->src[i].state = VIDEOBUF_IDLE;
		ctx->src[i].flags = 0x0;

		ctx->inq[i] = -1;
		ctx->outq[i] = -1;
	}
}

static int fimc_outdev_set_src_buf(struct fimc_control *ctrl,
				   struct fimc_ctx *ctx)
{
	u32 width = ctx->pix.width;
	u32 height = ctx->pix.height;
	u32 format = ctx->pix.pixelformat;
	u32 y_size = width * height;
	u32 cb_size = 0, cr_size = 0;
	u32 i, size;
	dma_addr_t *curr = &ctrl->mem.curr;

	switch (format) {
	case V4L2_PIX_FMT_RGB32:
		size = PAGE_ALIGN(y_size << 2);
		break;
	case V4L2_PIX_FMT_RGB565:	/* fall through */
	case V4L2_PIX_FMT_YUYV:
		size = PAGE_ALIGN(y_size << 1);
		break;
	case V4L2_PIX_FMT_NV16:
		cb_size = y_size;
		size = PAGE_ALIGN(y_size + cb_size);
		break;
	case V4L2_PIX_FMT_YUV420:
		cb_size = y_size >> 2;
		cr_size = y_size >> 2;
		size = PAGE_ALIGN(y_size + cb_size + cr_size);
		break;
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		cb_size = y_size >> 1;
		size = PAGE_ALIGN(y_size + cb_size);
		break;
	case V4L2_PIX_FMT_NV12T:
		fimc_get_nv12t_size(width, height, &y_size, &cb_size);
		size = PAGE_ALIGN(y_size + cb_size);
		break;
	default:
		fimc_err("%s: Invalid pixelformt : %d\n", __func__, format);
		return -EINVAL;
	}

	if ((*curr + size * FIMC_OUTBUFS) > (ctrl->mem.base + ctrl->mem.size)) {
		fimc_err("%s: Reserved memory is not sufficient\n", __func__);
		fimc_err("ctrl->mem.base = 0x%x\n", ctrl->mem.base);
		fimc_err("ctrl->mem.size = 0x%x\n", ctrl->mem.size);
		fimc_err("*curr          = 0x%x\n", *curr);
		fimc_err("size           = 0x%x\n", size);
		fimc_err("FIMC_OUTBUFS   = 0x%x\n", FIMC_OUTBUFS);
		return -EINVAL;
	}

	/* Initialize source buffer addr */
	switch (format) {
	case V4L2_PIX_FMT_RGB565:	/* fall through */
	case V4L2_PIX_FMT_RGB32:	/* fall through */
	case V4L2_PIX_FMT_YUYV:
		for (i = 0; i < FIMC_OUTBUFS; i++) {
			ctx->src[i].base[FIMC_ADDR_Y] = *curr;
			ctx->src[i].length[FIMC_ADDR_Y] = size;
			ctx->src[i].base[FIMC_ADDR_CB] = 0;
			ctx->src[i].length[FIMC_ADDR_CB] = 0;
			ctx->src[i].base[FIMC_ADDR_CR] = 0;
			ctx->src[i].length[FIMC_ADDR_CR] = 0;
			*curr += size;
		}
		break;
	case V4L2_PIX_FMT_NV16:
		for (i = 0; i < FIMC_OUTBUFS; i++) {
			ctx->src[i].base[FIMC_ADDR_Y] = *curr;
			ctx->src[i].length[FIMC_ADDR_Y] = y_size;
			ctx->src[i].base[FIMC_ADDR_CB] = *curr + y_size;
			ctx->src[i].length[FIMC_ADDR_CB] = cb_size;
			ctx->src[i].base[FIMC_ADDR_CR] = 0;
			ctx->src[i].length[FIMC_ADDR_CR] = 0;
			*curr += size;
		}
		break;
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		for (i = 0; i < FIMC_OUTBUFS; i++) {
			ctx->src[i].base[FIMC_ADDR_Y] = *curr;
			ctx->src[i].length[FIMC_ADDR_Y] = y_size;
			ctx->src[i].base[FIMC_ADDR_CB] = *curr + y_size;
			ctx->src[i].length[FIMC_ADDR_CB] = cb_size;
			ctx->src[i].base[FIMC_ADDR_CR] = 0;
			ctx->src[i].length[FIMC_ADDR_CR] = 0;
			*curr += size;
		}
		break;
	case V4L2_PIX_FMT_NV12T:
		for (i = 0; i < FIMC_OUTBUFS; i++) {
			ctx->src[i].base[FIMC_ADDR_Y] = *curr;
			ctx->src[i].base[FIMC_ADDR_CB] = *curr + y_size;
			ctx->src[i].length[FIMC_ADDR_Y] = y_size;
			ctx->src[i].length[FIMC_ADDR_CB] = cb_size;
			ctx->src[i].base[FIMC_ADDR_CR] = 0;
			ctx->src[i].length[FIMC_ADDR_CR] = 0;
			*curr += size;
		}
		break;
	case V4L2_PIX_FMT_YUV420:
		for (i = 0; i < FIMC_OUTBUFS; i++) {
			ctx->src[i].base[FIMC_ADDR_Y] = *curr;
			ctx->src[i].base[FIMC_ADDR_CB] = *curr + y_size;
			ctx->src[i].base[FIMC_ADDR_CR] = *curr + y_size + cb_size;
			ctx->src[i].length[FIMC_ADDR_Y] = y_size;
			ctx->src[i].length[FIMC_ADDR_CB] = cb_size;
			ctx->src[i].length[FIMC_ADDR_CR] = cr_size;
			*curr += size;
		}
		break;

	default:
		fimc_err("%s: Invalid pixelformt : %d\n", __func__, format);
		return -EINVAL;
	}

	return 0;
}

static int fimc_outdev_set_dst_buf(struct fimc_control *ctrl,
				   struct fimc_ctx *ctx)
{
	dma_addr_t *curr = &ctrl->mem.curr;
	dma_addr_t end;
	u32 width = ctrl->fb.lcd_hres;
	u32 height = ctrl->fb.lcd_vres;
	u32 i, size;

	end = ctrl->mem.base + ctrl->mem.size;
	size = PAGE_ALIGN(width * height * 4);

	if ((*curr + (size * FIMC_OUTBUFS)) > end) {
		fimc_err("%s: Reserved memory is not sufficient\n", __func__);
		fimc_err("ctrl->mem.base = 0x%x\n", ctrl->mem.base);
		fimc_err("ctrl->mem.size = 0x%x\n", ctrl->mem.size);
		fimc_err("*curr          = 0x%x\n", *curr);
		fimc_err("size           = 0x%x\n", size);
		fimc_err("FIMC_OUTBUFS   = 0x%x\n", FIMC_OUTBUFS);
		return -EINVAL;
	}

	/* Initialize destination buffer addr */
	for (i = 0; i < FIMC_OUTBUFS; i++) {
		ctx->dst[i].base[FIMC_ADDR_Y] = *curr;
		ctx->dst[i].length[FIMC_ADDR_Y] = size;
		ctx->dst[i].base[FIMC_ADDR_CB] = 0;
		ctx->dst[i].length[FIMC_ADDR_CB] = 0;
		ctx->dst[i].base[FIMC_ADDR_CR] = 0;
		ctx->dst[i].length[FIMC_ADDR_CR] = 0;
		*curr += size;
	}

	return 0;
}

static int fimc_set_rot_degree(struct fimc_control *ctrl,
			       struct fimc_ctx *ctx,
			       int degree)
{
	switch (degree) {
	case 0:		/* fall through */
	case 90:	/* fall through */
	case 180:	/* fall through */
	case 270:
		ctx->rotate = degree;
		break;

	default:
		fimc_err("Invalid rotate value : %d\n", degree);
		return -EINVAL;
	}

	return 0;
}

int fimc_outdev_check_param(struct fimc_control *ctrl,
			    struct fimc_ctx *ctx)
{
	struct v4l2_rect dst, bound;
	u32 rot = 0;
	int ret = 0, i, exclusive = 0;

	rot = fimc_mapping_rot_flip(ctx->rotate, ctx->flip);
	dst.top = ctx->win.w.top;
	dst.left = ctx->win.w.left;
	dst.width = ctx->win.w.width;
	dst.height = ctx->win.w.height;

	switch (ctx->overlay.mode) {
	case FIMC_OVLY_FIFO:		/* fall through */
	case FIMC_OVLY_DMA_AUTO:	/* fall through */
	case FIMC_OVLY_DMA_MANUAL:
		if (rot & FIMC_ROT) {
			bound.width = ctrl->fb.lcd_vres;
			bound.height = ctrl->fb.lcd_hres;
		} else {
			bound.width = ctrl->fb.lcd_hres;
			bound.height = ctrl->fb.lcd_vres;
		}
		break;
	case FIMC_OVLY_NONE_SINGLE_BUF:	/* fall through */
	case FIMC_OVLY_NONE_MULTI_BUF:
		bound.width = ctx->fbuf.fmt.width;
		bound.height = ctx->fbuf.fmt.height;
		break;

	default:
		fimc_err("%s: invalid ovelay mode.\n", __func__);
		return -EINVAL;
	}

	if ((dst.left + dst.width) > bound.width) {
		fimc_err("Horizontal position setting is failed\n");
		fimc_err("\tleft = %d, width = %d, bound width = %d,\n",
				dst.left, dst.width, bound.width);
		ret = -EINVAL;
	} else if ((dst.top + dst.height) > bound.height) {
		fimc_err("Vertical position setting is failed\n");
		fimc_err("\ttop = %d, height = %d, bound height = %d,\n",
				dst.top, dst.height, bound.height);
		ret = -EINVAL;
	}

	if ((ctx->status != FIMC_STREAMOFF) &&
	    (ctx->status != FIMC_READY_ON) &&
	    (ctx->status != FIMC_STREAMON_IDLE) &&
	    (ctx->status != FIMC_ON_IDLE_SLEEP)) {
		fimc_err("%s: FIMC is running\n", __func__);
		return -EBUSY;
	}

	/* check other open instance */
	for (i = 0; i < FIMC_MAX_CTXS; i++) {
		switch (ctrl->out->ctx[i].overlay.mode) {
		case FIMC_OVLY_FIFO:		/* fall through */
		case FIMC_OVLY_DMA_AUTO:	/* fall through */
		case FIMC_OVLY_DMA_MANUAL:
			exclusive++;
			break;
		case FIMC_OVLY_NONE_SINGLE_BUF:	/* fall through */
		case FIMC_OVLY_NONE_MULTI_BUF:	/* fall through */
		case FIMC_OVLY_NOT_FIXED:
			break;
		}
	}

	if (exclusive > 1) {
		for (i = 0; i < FIMC_MAX_CTXS; i++)
			fimc_err("%s: ctx %d mode = %d", __func__, i,
					ctrl->out->ctx[i].overlay.mode);
		return -EBUSY;
	}

	return ret;
}

static void fimc_outdev_set_src_format(struct fimc_control *ctrl,
				       u32 pixfmt, enum v4l2_field field)
{
	fimc_hwset_input_burst_cnt(ctrl, 4);
	fimc_hwset_input_colorspace(ctrl, pixfmt);
	fimc_hwset_input_yuv(ctrl, pixfmt);
	fimc_hwset_input_rgb(ctrl, pixfmt);
	fimc_hwset_intput_field(ctrl, field);
	fimc_hwset_ext_rgb(ctrl, 1);
	fimc_hwset_input_addr_style(ctrl, pixfmt);
}

static void fimc_outdev_set_dst_format(struct fimc_control *ctrl,
				       struct v4l2_pix_format *pixfmt)
{
	fimc_hwset_output_colorspace(ctrl, pixfmt->pixelformat);
	fimc_hwset_output_yuv(ctrl, pixfmt->pixelformat);
	fimc_hwset_output_rgb(ctrl, pixfmt->pixelformat);
	fimc_hwset_output_scan(ctrl, pixfmt);
	fimc_hwset_output_addr_style(ctrl, pixfmt->pixelformat);
}

static void fimc_outdev_set_format(struct fimc_control *ctrl,
				   struct fimc_ctx *ctx)
{
	struct v4l2_pix_format pixfmt;
	memset(&pixfmt, 0, sizeof(pixfmt));

	fimc_outdev_set_src_format(ctrl, ctx->pix.pixelformat, ctx->pix.field);

	switch (ctx->overlay.mode) {
	case FIMC_OVLY_FIFO:		/* fall through */
	case FIMC_OVLY_DMA_AUTO:	/* fall through */
	case FIMC_OVLY_DMA_MANUAL:	/* Non-destructive overlay mode */
		if (ctx->pix.field == V4L2_FIELD_NONE) {
			pixfmt.pixelformat = V4L2_PIX_FMT_RGB32;
			pixfmt.field = V4L2_FIELD_NONE;
		} else if (ctx->pix.field == V4L2_FIELD_INTERLACED_TB) {
			pixfmt.pixelformat = V4L2_PIX_FMT_YUV444;
			pixfmt.field = V4L2_FIELD_INTERLACED_TB;
		} else if (ctx->pix.field == V4L2_FIELD_ANY) {
			pixfmt.pixelformat = V4L2_PIX_FMT_RGB32;
			pixfmt.field = V4L2_FIELD_NONE;
		}

		break;
	case FIMC_OVLY_NONE_SINGLE_BUF:	/* fall through */
	case FIMC_OVLY_NONE_MULTI_BUF:	/* Destructive overlay mode */
		pixfmt.pixelformat = ctx->fbuf.fmt.pixelformat;
		pixfmt.field = V4L2_FIELD_NONE;

		break;
	default:
		fimc_err("Invalid overlay mode %d\n", ctx->overlay.mode);
		break;
	}

	fimc_outdev_set_dst_format(ctrl, &pixfmt);
}

static void fimc_outdev_set_path(struct fimc_control *ctrl,
				 struct fimc_ctx *ctx)
{
	/* source path */
	fimc_hwset_input_source(ctrl, FIMC_SRC_MSDMA);

	if (ctx->overlay.mode == FIMC_OVLY_FIFO) {
		fimc_hwset_enable_lcdfifo(ctrl);
		fimc_hwset_enable_autoload(ctrl);
	} else {
		fimc_hwset_disable_lcdfifo(ctrl);
		fimc_hwset_disable_autoload(ctrl);
	}
}

static void fimc_outdev_set_rot(struct fimc_control *ctrl,
				struct fimc_ctx *ctx)
{
	u32 rot = ctx->rotate;
	u32 flip = ctx->flip;

	if (ctx->overlay.mode == FIMC_OVLY_FIFO) {
		fimc_hwset_input_rot(ctrl, rot, flip);
		fimc_hwset_input_flip(ctrl, rot, flip);
		fimc_hwset_output_rot_flip(ctrl, 0, 0);
	} else {
		fimc_hwset_input_rot(ctrl, 0, 0);
		fimc_hwset_input_flip(ctrl, 0, 0);
		fimc_hwset_output_rot_flip(ctrl, rot, flip);
	}
}

static void fimc_outdev_set_src_dma_offset(struct fimc_control *ctrl,
					   struct fimc_ctx *ctx)
{
	struct v4l2_rect bound, crop;
	u32 pixfmt = ctx->pix.pixelformat;

	bound.width = ctx->pix.width;
	bound.height = ctx->pix.height;

	crop.left = ctx->crop.left;
	crop.top = ctx->crop.top;
	crop.width = ctx->crop.width;
	crop.height = ctx->crop.height;

	fimc_hwset_input_offset(ctrl, pixfmt, &bound, &crop);
}

static int fimc4x_outdev_check_src_size(struct fimc_control *ctrl,
					struct fimc_ctx *ctx,
					struct v4l2_rect *real,
					struct v4l2_rect *org)
{
	u32 rot = ctx->rotate;

	if ((ctx->overlay.mode == FIMC_OVLY_FIFO) &&
	    ((rot == 90) || (rot == 270))) {
		/* Input Rotator */
		if (real->height % 16) {
			fimc_err("SRC Real_H(%d): multiple of 16 !\n",
					real->height);
			return -EINVAL;
		}

		if (ctx->sc.pre_hratio) {
			if (real->height % (ctx->sc.pre_hratio * 4)) {
				fimc_err("SRC Real_H(%d): multiple of "
						"4*pre_hratio(%d)\n",
						real->height,
						ctx->sc.pre_hratio);
				return -EINVAL;
			}
		}

		if (ctx->sc.pre_vratio) {
			if (real->width % ctx->sc.pre_vratio) {
				fimc_err("SRC Real_W(%d): multiple of "
						"pre_vratio(%d)\n",
						real->width,
						ctx->sc.pre_vratio);
				return -EINVAL;
			}
		}

		if (real->height < 16) {
			fimc_err("SRC Real_H(%d): Min 16\n", real->height);
			return -EINVAL;
		}
		if (real->width < 8) {
			fimc_err("SRC Real_W(%d): Min 8\n", real->width);
			return -EINVAL;
		}
	} else {
		/* No Input Rotator */
		if (real->height < 8) {
			fimc_err("SRC Real_H(%d): Min 8\n", real->height);
			return -EINVAL;
		}

		if (real->width < 16) {
			fimc_err("SRC Real_W(%d): Min 16\n", real->width);
			return -EINVAL;
		}

		if (real->width > ctrl->limit->real_w_no_rot) {
			fimc_err("SRC REAL_W(%d): Real_W <= %d\n", real->width,
					ctrl->limit->real_w_no_rot);
			return -EINVAL;
		}
	}

	if (org->height < real->height) {
		fimc_err("SRC Org_H(%d): larger than Real_H(%d)\n",
				org->height, real->height);
		return -EINVAL;
	}

	if (org->width < real->width) {
		fimc_err("SRC Org_W: Org_W(%d) >= Real_W(%d)\n", org->width,
				real->width);
		return -EINVAL;
	}

	if (ctx->sc.pre_vratio) {
		if (real->height % ctx->sc.pre_vratio) {
			fimc_err("SRC Real_H(%d): multi of pre_vratio(%d)!\n",
					real->height, ctx->sc.pre_vratio);
			return -EINVAL;
		}
	}

	if (real->width % 16) {
		fimc_err("SRC Real_W(%d): multiple of 16 !\n", real->width);
		return -EINVAL;
	}

	if (ctx->sc.pre_hratio) {
		if (real->width % (ctx->sc.pre_hratio * 4)) {
			fimc_err("SRC Real_W(%d): "
					"multiple of 4 * pre_hratio(%d)!\n",
					real->width, ctx->sc.pre_hratio);
			return -EINVAL;
		}
	}

	if (org->width % 16) {
		fimc_err("SRC Org_W(%d): multiple of 16\n", org->width);
		return -EINVAL;
	}

	if (org->height < 8) {
		fimc_err("SRC Org_H(%d): Min 8\n", org->height);
		return -EINVAL;
	}

	return 0;
}

static int fimc50_outdev_check_src_size(struct fimc_control *ctrl,
					struct fimc_ctx *ctx,
					struct v4l2_rect *real,
					struct v4l2_rect *org)
{
	u32 rot = ctx->rotate;
	u32 pixelformat = ctx->pix.pixelformat;

	if ((ctx->overlay.mode == FIMC_OVLY_FIFO) &&
	    ((rot == 90) || (rot == 270))) {
		/* Input Rotator */
		switch (pixelformat) {
		case V4L2_PIX_FMT_YUV422P:	/* fall through */
		case V4L2_PIX_FMT_YVU420:
			if (real->height % 2) {
				fimc_err("SRC Real_H(%d): multiple of 2\n",
						real->height);
				return -EINVAL;
			}
		}

		if (real->height < 16) {
			fimc_err("SRC Real_H(%d): Min 16\n", real->height);
			return -EINVAL;
		}
		if (real->width < 8) {
			fimc_err("SRC Real_W(%d): Min 8\n", real->width);
			return -EINVAL;
		}
	} else {
		/* No Input Rotator */
		if (real->height < 8) {
			fimc_err("SRC Real_H(%d): Min 8\n", real->height);
			return -EINVAL;
		}

		if (real->width < 16) {
			fimc_err("SRC Real_W(%d): Min 16\n", real->width);
			return -EINVAL;
		}

		if (real->width > ctrl->limit->real_w_no_rot) {
			fimc_err("SRC REAL_W(%d): Real_W <= %d\n", real->width,
					ctrl->limit->real_w_no_rot);
			return -EINVAL;
		}
	}

	if (org->height < real->height) {
		fimc_err("SRC Org_H: larger than Real_H, "
				"org %dx%d, real %dx%d\n",
				org->width, org->height,
				real->width, real->height);
		return -EINVAL;
	}

	if (org->width < real->width) {
		fimc_err("SRC Org_W: Org_W(%d) >= Real_W(%d)\n",
				org->width, real->width);
		return -EINVAL;
	}

	if (ctx->pix.field == V4L2_FIELD_INTERLACED_TB) {
		switch (pixelformat) {
		case V4L2_PIX_FMT_YUV444:	/* fall through */
		case V4L2_PIX_FMT_RGB32:
			if (real->height % 2) {
				fimc_err("SRC Real_H(%d): multiple of 2\n",
						real->height);
				return -EINVAL;
			}
			break;
		case V4L2_PIX_FMT_YUV422P:
			if (real->height % 2) {
				fimc_err("SRC Real_H(%d): multiple of 2\n",
						real->height);
				return -EINVAL;
			} else if (real->width % 2) {
				fimc_err("SRC Real_H(%d): multiple of 2\n",
						real->width);
				return -EINVAL;
			}
			break;
		case V4L2_PIX_FMT_YVU420:
			if (real->height % 4) {
				fimc_err("SRC Real_H(%d): multiple of 4\n",
						real->height);
				return -EINVAL;
			} else if (real->width % 2) {
				fimc_err("SRC Real_H(%d): multiple of 2\n",
						real->width);
				return -EINVAL;
			}
			break;
		}
	} else if (ctx->pix.field == V4L2_FIELD_NONE) {
		if (pixelformat == V4L2_PIX_FMT_YUV422P) {
			if (real->height % 2) {
				fimc_err("SRC Real_H(%d): multiple of 2\n",
						real->height);
				return -EINVAL;
			} else if (real->width % 2) {
				fimc_err("SRC Real_H(%d): multiple of 2\n",
						real->width);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static int fimc_outdev_set_src_dma_size(struct fimc_control *ctrl,
					struct fimc_ctx *ctx)
{
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
	struct v4l2_rect real, org;
	int ret = 0;

	real.width = ctx->crop.width;
	real.height = ctx->crop.height;
	org.width = ctx->pix.width;
	org.height = ctx->pix.height;

	if (pdata->hw_ver >= 0x50)
		ret = fimc50_outdev_check_src_size(ctrl, ctx, &real, &org);
	else
		ret = fimc4x_outdev_check_src_size(ctrl, ctx, &real, &org);

	if (ret < 0)
		return ret;

	fimc_hwset_org_input_size(ctrl, org.width, org.height);
	fimc_hwset_real_input_size(ctrl, real.width, real.height);

	return 0;
}

static void fimc_outdev_set_dst_dma_offset(struct fimc_control *ctrl,
					   struct fimc_ctx *ctx)
{
	struct v4l2_rect bound, win;
	struct v4l2_rect *w = &ctx->win.w;
	u32 pixfmt = ctx->fbuf.fmt.pixelformat;

	memset(&bound, 0, sizeof(bound));
	memset(&win, 0, sizeof(win));

	switch (ctx->rotate) {
	case 0:
		bound.width = ctx->fbuf.fmt.width;
		bound.height = ctx->fbuf.fmt.height;

		win.left = w->left;
		win.top = w->top;
		win.width = w->width;
		win.height = w->height;

		break;
	case 90:
		bound.width = ctx->fbuf.fmt.height;
		bound.height = ctx->fbuf.fmt.width;

		win.left = ctx->fbuf.fmt.height - (w->height + w->top);
		win.top = w->left;
		win.width = w->height;
		win.height = w->width;

		break;
	case 180:
		bound.width = ctx->fbuf.fmt.width;
		bound.height = ctx->fbuf.fmt.height;

		win.left = ctx->fbuf.fmt.width - (w->left + w->width);
		win.top = ctx->fbuf.fmt.height - (w->top + w->height);
		win.width = w->width;
		win.height = w->height;

		break;
	case 270:
		bound.width = ctx->fbuf.fmt.height;
		bound.height = ctx->fbuf.fmt.width;

		win.left = ctx->win.w.top;
		win.top = ctx->fbuf.fmt.width - (w->left + w->width);
		win.width = w->height;
		win.height = w->width;

		break;
	default:
		fimc_err("Rotation degree is invalid(%d)\n", ctx->rotate);
		break;
	}

	switch (ctx->overlay.mode) {
	case FIMC_OVLY_FIFO:		/* fall through */
	case FIMC_OVLY_DMA_AUTO:	/* fall through */
	case FIMC_OVLY_DMA_MANUAL:
		memset(&bound, 0, sizeof(bound));
		memset(&win, 0, sizeof(win));
		fimc_hwset_output_offset(ctrl, pixfmt, &bound, &win);
		break;
	default:
		fimc_hwset_output_offset(ctrl, pixfmt, &bound, &win);
		break;
	}

	fimc_dbg("bound:width(%d), height(%d)\n", bound.width, bound.height);
	fimc_dbg("win:width(%d), height(%d)\n", win.width, win.height);
	fimc_dbg("win:top(%d), left(%d)\n", win.top, win.left);
}

static int fimc_outdev_check_dst_size(struct fimc_control *ctrl,
				      struct fimc_ctx *ctx,
				      struct v4l2_rect *real,
				      struct v4l2_rect *org)
{
	u32 rot = ctx->rotate;
	__u32 pixel_type;

	pixel_type = fimc_get_pixel_format_type(ctx->fbuf.fmt.pixelformat);

	if (FIMC_YUV420 == pixel_type && real->height % 2) {
		fimc_err("DST Real_H(%d): even number for YUV420 formats\n",
				real->height);
		return -EINVAL;
	}

	if ((ctx->overlay.mode != FIMC_OVLY_FIFO) &&
	    ((rot == 90) || (rot == 270))) {
		/* Use Output Rotator */
		if (org->height < real->width) {
			fimc_err("DST Org_H: Org_H(%d) >= Real_W(%d)\n",
					org->height, real->width);
			return -EINVAL;
		}

		if (org->width < real->height) {
			fimc_err("DST Org_W: Org_W(%d) >= Real_H(%d)\n",
					org->width, real->height);
			return -EINVAL;
		}

		if (real->height > ctrl->limit->trg_h_rot) {
			fimc_err("DST REAL_H: Real_H(%d) <= %d\n", real->height,
					ctrl->limit->trg_h_rot);
			return -EINVAL;
		}
	} else if (ctx->overlay.mode != FIMC_OVLY_FIFO) {
		/* No Output Rotator */
		if (org->height < 8) {
			fimc_err("DST Org_H(%d): Min 8\n", org->height);
			return -EINVAL;
		}

		if (org->height < real->height) {
			fimc_err("DST Org_H: Org_H(%d) >= Real_H(%d)\n",
					org->height, real->height);
			return -EINVAL;
		}
		/*
		if (org->width % 8) {
			fimc_err("DST Org_W: multiple of 8\n");
			return -EINVAL;
		}*/

		if (org->width < real->width) {
			fimc_err("DST Org_W: Org_W(%d) >= Real_W(%d)\n",
					org->width, real->width);
			return -EINVAL;
		}

		if (real->height > ctrl->limit->trg_h_no_rot) {
			fimc_err("DST REAL_H: Real_H(%d) <= %d\n", real->height,
					ctrl->limit->trg_h_no_rot);
			return -EINVAL;
		}
	}

	return 0;
}

static int fimc_outdev_set_dst_dma_size(struct fimc_control *ctrl,
					struct fimc_ctx *ctx)
{
	struct v4l2_rect org, real;
	int ret = -1;

	memset(&org, 0, sizeof(org));
	memset(&real, 0, sizeof(real));

	switch (ctx->overlay.mode) {
	case FIMC_OVLY_NONE_MULTI_BUF:	/* fall through */
	case FIMC_OVLY_NONE_SINGLE_BUF:
		real.width = ctx->win.w.width;
		real.height = ctx->win.w.height;

		switch (ctx->rotate) {
		case 0:		/* fall through */
		case 180:
			org.width = ctx->fbuf.fmt.width;
			org.height = ctx->fbuf.fmt.height;
			break;
		case 90:	/* fall through */
		case 270:
			org.width = ctx->fbuf.fmt.height;
			org.height = ctx->fbuf.fmt.width;
			break;
		default:
			fimc_err("Rotation degree is invalid(%d)\n",
					ctx->rotate);
			break;
		}

		break;

	case FIMC_OVLY_DMA_MANUAL:	/* fall through */
	case FIMC_OVLY_DMA_AUTO:
		real.width = ctx->win.w.width;
		real.height = ctx->win.w.height;

		switch (ctx->rotate) {
		case 0:		/* fall through */
		case 180:
			org.width = ctx->win.w.width;
			org.height = ctx->win.w.height;
			break;
		case 90:	/* fall through */
		case 270:
			org.width = ctx->win.w.height;
			org.height = ctx->win.w.width;
			break;
		default:
			fimc_err("Rotation degree is invalid(%d)\n",
					ctx->rotate);
			break;
		}

		break;
	case FIMC_OVLY_FIFO:
		switch (ctx->rotate) {
		case 0:		/* fall through */
		case 180:
			real.width = ctx->win.w.width;
			real.height = ctx->win.w.height;
			org.width = ctrl->fb.lcd_hres;
			org.height = ctrl->fb.lcd_vres;
			break;
		case 90:	/* fall through */
		case 270:
			real.width = ctx->win.w.height;
			real.height = ctx->win.w.width;
			org.width = ctrl->fb.lcd_vres;
			org.height = ctrl->fb.lcd_hres;
			break;

		default:
			fimc_err("Rotation degree is invalid(%d)\n",
					ctx->rotate);
			break;
		}

		break;
	default:
		break;
	}

	if (ctx->overlay.mode == FIMC_OVLY_DMA_AUTO)
		fimc_outdev_dma_auto_dst_resize(&org);

	fimc_dbg("DST org: width(%d), height(%d)\n", org.width, org.height);
	fimc_dbg("DST real: width(%d), height(%d)\n", real.width, real.height);

	ret = fimc_outdev_check_dst_size(ctrl, ctx, &real, &org);
	if (ret < 0)
		return ret;

	fimc_hwset_output_size(ctrl, real.width, real.height);
	fimc_hwset_output_area(ctrl, real.width, real.height);
	fimc_hwset_org_output_size(ctrl, org.width, org.height);
	fimc_hwset_ext_output_size(ctrl, real.width, real.height);

	return 0;
}

static void fimc_outdev_calibrate_scale_info(struct fimc_control *ctrl,
					     struct fimc_ctx *ctx,
					     struct v4l2_rect *src,
					     struct v4l2_rect *dst)
{
	if (ctx->overlay.mode != FIMC_OVLY_FIFO) {
		/* OUTPUT ROTATOR */
		src->width = ctx->crop.width;
		src->height = ctx->crop.height;
		dst->width = ctx->win.w.width;
		dst->height = ctx->win.w.height;
	} else {
		/* INPUT ROTATOR */
		switch (ctx->rotate) {
		case 0:		/* fall through */
		case 180:
			src->width = ctx->crop.width;
			src->height = ctx->crop.height;
			dst->width = ctx->win.w.width;
			dst->height = ctx->win.w.height;
			break;
		case 90:	/* fall through */
		case 270:
			src->width = ctx->crop.height;
			src->height = ctx->crop.width;
			dst->width = ctx->win.w.height;
			dst->height = ctx->win.w.width;
			break;
		default:
			fimc_err("Rotation degree is invalid(%d)\n",
					ctx->rotate);
			break;
		}
	}

	fimc_dbg("src->width(%d), src->height(%d)\n", src->width, src->height);
	fimc_dbg("dst->width(%d), dst->height(%d)\n", dst->width, dst->height);
}

static int fimc_outdev_check_scaler(struct fimc_control *ctrl,
				    struct fimc_ctx *ctx,
				    struct v4l2_rect *src,
				    struct v4l2_rect *dst)
{
	u32 pixels = 0, dstfmt = 0;

	/* Check scaler limitation */
	if (ctx->sc.pre_dst_width > ctrl->limit->pre_dst_w) {
		fimc_err("MAX PreDstWidth(%d) is %d\n", ctx->sc.pre_dst_width,
					ctrl->limit->pre_dst_w);
		return -EDOM;
	}

	/* SRC width double boundary check */
	switch (ctx->pix.pixelformat) {
	case V4L2_PIX_FMT_RGB32:
		pixels = 1;
		break;
	case V4L2_PIX_FMT_YUYV:		/* fall through */
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_NV16:
		pixels = 2;
		break;
	case V4L2_PIX_FMT_YUV420:	/* fall through */
	case V4L2_PIX_FMT_YVU420:	/* fall through */
	case V4L2_PIX_FMT_NV12:		/* fall through */
	case V4L2_PIX_FMT_NV21:		/* fall through */
	case V4L2_PIX_FMT_NV12T:
		pixels = 8;
		break;
	default:
		fimc_err("Invalid color format(0x%x)\n", ctx->pix.pixelformat);
		return -EINVAL;
	}

	if (src->width % pixels) {
		fimc_err("source width(%d) multiple of %d pixels\n", src->width,
				pixels);
		return -EDOM;
	}

	/* DST width double boundary check */
	switch (ctx->overlay.mode) {
	case FIMC_OVLY_FIFO:			/* fall through */
	case FIMC_OVLY_DMA_AUTO:		/* fall through */
	case FIMC_OVLY_DMA_MANUAL:
		dstfmt = V4L2_PIX_FMT_RGB32;
		break;
	case FIMC_OVLY_NONE_SINGLE_BUF:		/* fall through */
	case FIMC_OVLY_NONE_MULTI_BUF:
		dstfmt = ctx->fbuf.fmt.pixelformat;
		break;
	default:
		break;
	}

	switch (dstfmt) {
	case V4L2_PIX_FMT_RGB32:
		pixels = 1;
		break;
	case V4L2_PIX_FMT_RGB565:
		pixels = 2;
		break;
	case V4L2_PIX_FMT_YUV420:	/* fall through */
	case V4L2_PIX_FMT_YVU420:	/* fall through */
	case V4L2_PIX_FMT_NV12:		/* fall through */
	case V4L2_PIX_FMT_NV12M:		/* fall through */
	case V4L2_PIX_FMT_NV21:		/* fall through */
	case V4L2_PIX_FMT_NV12T:
		pixels = 8;
		break;
	default:
		fimc_err("Invalid color format(0x%x)\n", dstfmt);
		return -EINVAL;
	}

	if (dst->width % pixels) {
		fimc_err("source width(%d) multiple of %d pixels\n",
				dst->width, pixels);
		return -EDOM;
	}

	return 0;
}

static int fimc_outdev_set_scaler(struct fimc_control *ctrl,
				  struct fimc_ctx *ctx)
{
	struct v4l2_rect src, dst;
	int ret = 0;
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);

	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));

	fimc_outdev_calibrate_scale_info(ctrl, ctx, &src, &dst);

	ret = fimc_get_scaler_factor(src.width, dst.width,
			&ctx->sc.pre_hratio, &ctx->sc.hfactor);
	if (ret < 0) {
		fimc_err("Fail : Out of Width scale range(%d, %d)\n",
				src.width, dst.width);
		return ret;
	}

	ret = fimc_get_scaler_factor(src.height, dst.height,
			&ctx->sc.pre_vratio, &ctx->sc.vfactor);
	if (ret < 0) {
		fimc_err("Fail : Out of Height scale range(%d, %d)\n",
				src.height, dst.height);
		return ret;
	}

	if (src.width == src.height) {
		if ((src.width * 10 / dst.width) >= 15 &&
					(src.width * 10 / dst.width) < 20) {
			ctx->sc.pre_hratio = 2;
			ctx->sc.hfactor = 1;
		}
		if ((src.height * 10 / dst.height) >= 15 &&
					(src.height * 10 / dst.height) < 20) {
			ctx->sc.pre_vratio = 2;
			ctx->sc.vfactor = 1;
		}
	}

	ctx->sc.pre_dst_width = src.width / ctx->sc.pre_hratio;
	ctx->sc.pre_dst_height = src.height / ctx->sc.pre_vratio;

	if (pdata->hw_ver >= 0x50) {
		ctx->sc.main_hratio = (src.width << 14) /
					(dst.width << ctx->sc.hfactor);
		ctx->sc.main_vratio = (src.height << 14) /
					(dst.height << ctx->sc.vfactor);
	} else {
		ctx->sc.main_hratio = (src.width << 8) /
					(dst.width << ctx->sc.hfactor);
		ctx->sc.main_vratio = (src.height << 8) /
					(dst.height << ctx->sc.vfactor);
	}

	fimc_dbg("pre_hratio(%d), hfactor(%d), pre_vratio(%d), vfactor(%d)\n",
		ctx->sc.pre_hratio, ctx->sc.hfactor,
		ctx->sc.pre_vratio, ctx->sc.vfactor);


	fimc_dbg("pre_dst_width(%d), main_hratio(%d), "
		"pre_dst_height(%d), main_vratio(%d)\n",
		ctx->sc.pre_dst_width, ctx->sc.main_hratio,
		ctx->sc.pre_dst_height, ctx->sc.main_vratio);

	ctx->sc.bypass = 0;	/* Input DMA cannot support scaler bypass. */
	ctx->sc.scaleup_h = (dst.width >= src.width) ? 1 : 0;
	ctx->sc.scaleup_v = (dst.height >= src.height) ? 1 : 0;
	ctx->sc.shfactor = 10 - (ctx->sc.hfactor + ctx->sc.vfactor);

	if (pdata->hw_ver < 0x50) {
		ret = fimc_outdev_check_scaler(ctrl, ctx, &src, &dst);
		if (ret < 0)
			return ret;
	}

	fimc_hwset_prescaler(ctrl, &ctx->sc);
	fimc_hwset_scaler(ctrl, &ctx->sc);

	return 0;
}

int fimc_outdev_set_ctx_param(struct fimc_control *ctrl, struct fimc_ctx *ctx)
{
	int ret;
#if defined(CONFIG_VIDEO_IPC)
	u32 use_ipc = 0;
	struct v4l2_rect src, dst;
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
#endif

	fimc_hwset_reset(ctrl);
	fimc_hwset_enable_irq(ctrl, 0, 1);

#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
	fimc_hwset_output_buf_sequence_all(ctrl, FRAME_SEQ);
#endif

	fimc_outdev_set_format(ctrl, ctx);
	fimc_outdev_set_path(ctrl, ctx);
	fimc_outdev_set_rot(ctrl, ctx);

	fimc_outdev_set_src_dma_offset(ctrl, ctx);
	ret = fimc_outdev_set_src_dma_size(ctrl, ctx);
	if (ret < 0)
		return ret;

	fimc_outdev_set_dst_dma_offset(ctrl, ctx);

	ret = fimc_outdev_set_dst_dma_size(ctrl, ctx);
	if (ret < 0)
		return ret;

	ret = fimc_outdev_set_scaler(ctrl, ctx);
	if (ret < 0)
		return ret;

#if defined(CONFIG_VIDEO_IPC)
	if (ctx->overlay.mode == FIMC_OVLY_FIFO)
		if (ctx->pix.field == V4L2_FIELD_INTERLACED_TB)
			use_ipc = 1;

	if (use_ipc) {
		fimc_outdev_calibrate_scale_info(ctrl, ctx, &src, &dst);
		ret = ipc_init(dst.width, dst.height/2, IPC_2D);
		if (ret < 0)
			return ret;
	}
#endif

	return 0;
}

int fimc_fimd_rect(const struct fimc_control *ctrl,
		   const struct fimc_ctx *ctx,
		   struct v4l2_rect *fimd_rect)
{
	switch (ctx->rotate) {
	case 0:
		fimd_rect->left = ctx->win.w.left;
		fimd_rect->top = ctx->win.w.top;
		fimd_rect->width = ctx->win.w.width;
		fimd_rect->height = ctx->win.w.height;

		break;

	case 90:
		fimd_rect->left = ctrl->fb.lcd_hres -
				(ctx->win.w.top + ctx->win.w.height);
		fimd_rect->top = ctx->win.w.left;
		fimd_rect->width = ctx->win.w.height;
		fimd_rect->height = ctx->win.w.width;

		break;

	case 180:
		fimd_rect->left = ctrl->fb.lcd_hres -
				(ctx->win.w.left + ctx->win.w.width);
		fimd_rect->top = ctrl->fb.lcd_vres -
				(ctx->win.w.top + ctx->win.w.height);
		fimd_rect->width = ctx->win.w.width;
		fimd_rect->height = ctx->win.w.height;

		break;

	case 270:
		fimd_rect->left = ctx->win.w.top;
		fimd_rect->top = ctrl->fb.lcd_vres -
				(ctx->win.w.left + ctx->win.w.width);
		fimd_rect->width = ctx->win.w.height;
		fimd_rect->height = ctx->win.w.width;

		break;

	default:
		fimc_err("Rotation degree is invalid(%d)\n", ctx->rotate);
		return -EINVAL;

		break;
	}

	return 0;
}

int fimc_start_fifo(struct fimc_control *ctrl, struct fimc_ctx *ctx)
{
	struct v4l2_rect fimd_rect;
	struct fb_var_screeninfo var;
	struct s3cfb_user_window window;
	int ret = -1;
	u32 id = ctrl->id;

	memset(&fimd_rect, 0, sizeof(struct v4l2_rect));
	ret = fimc_fimd_rect(ctrl, ctx, &fimd_rect);
	if (ret < 0) {
		fimc_err("fimc_fimd_rect fail\n");
		return -EINVAL;
	}

	/* Get WIN var_screeninfo */
	ret = s3cfb_direct_ioctl(id, FBIOGET_VSCREENINFO,
						(unsigned long)&var);
	if (ret < 0) {
		fimc_err("direct_ioctl(FBIOGET_VSCREENINFO) fail\n");
		return -EINVAL;
	}

	/* Don't allocate the memory. */
	if (ctx->pix.field == V4L2_FIELD_NONE)
		ret = s3cfb_direct_ioctl(id,
					S3CFB_SET_WIN_PATH, DATA_PATH_FIFO);
	else if (ctx->pix.field == V4L2_FIELD_INTERLACED_TB)
		ret = s3cfb_direct_ioctl(id,
					S3CFB_SET_WIN_PATH, DATA_PATH_IPC);
	if (ret < 0) {
		fimc_err("direct_ioctl(S3CFB_SET_WIN_PATH) fail\n");
		return -EINVAL;
	}

	ret = s3cfb_direct_ioctl(id, S3CFB_SET_WIN_MEM, DMA_MEM_NONE);
	if (ret < 0) {
		fimc_err("direct_ioctl(S3CFB_SET_WIN_MEM) fail\n");
		return -EINVAL;
	}

	ret = s3cfb_direct_ioctl(id, S3CFB_SET_WIN_ADDR, 0x00000000);
	if (ret < 0) {
		fimc_err("direct_ioctl(S3CFB_SET_WIN_ADDR) fail\n");
		return -EINVAL;
	}

	/* Update WIN size */
	var.xres_virtual = fimd_rect.width;
	var.yres_virtual = fimd_rect.height;
	var.xres = fimd_rect.width;
	var.yres = fimd_rect.height;
	ret = s3cfb_direct_ioctl(id, FBIOPUT_VSCREENINFO,
					(unsigned long)&var);
	if (ret < 0) {
		fimc_err("direct_ioctl(FBIOPUT_VSCREENINFO) fail\n");
		return -EINVAL;
	}

	/* Update WIN position */
	window.x = fimd_rect.left;
	window.y = fimd_rect.top;
	ret = s3cfb_direct_ioctl(id, S3CFB_WIN_POSITION,
			(unsigned long)&window);
	if (ret < 0) {
		fimc_err("direct_ioctl(S3CFB_WIN_POSITION) fail\n");
		return -EINVAL;
	}

	/* Open WIN FIFO */
	ret = ctrl->fb.open_fifo(id, 0, fimc_outdev_start_camif, (void *)ctrl);
	if (ret < 0) {
		fimc_err("FIMD FIFO close fail\n");
		return -EINVAL;
	}

	return 0;
}

int fimc_outdev_overlay_buf(struct file *filp,
			    struct fimc_control *ctrl,
			    struct fimc_ctx *ctx)
{
	int ret = 0, i;
	struct fimc_overlay_buf *buf;

	buf = &ctx->overlay.buf;

	for (i = 0; i < FIMC_OUTBUFS; i++) {
		ctx->overlay.req_idx = i;
		buf->size[i] = ctx->dst[i].length[0];
		buf->phy_addr[i] = ctx->dst[i].base[0];
		buf->vir_addr[i] = do_mmap(filp, 0, buf->size[i],
					PROT_READ|PROT_WRITE, MAP_SHARED, 0);
		if (buf->vir_addr[i] == -EINVAL) {
			fimc_err("%s: fail\n", __func__);
			return -EINVAL;
		}

		fimc_dbg("idx : %d, size(0x%08x), phy_addr(0x%08x), "
				"vir_addr(0x%08x)\n", i, buf->size[i],
				buf->phy_addr[i], buf->vir_addr[i]);
	}

	ctx->overlay.req_idx = -1;

	return ret;
}

int fimc_reqbufs_output(void *fh, struct v4l2_requestbuffers *b)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	struct fimc_ctx *ctx;
	struct fimc_overlay_buf *buf;
	struct mm_struct *mm = current->mm;
	enum fimc_overlay_mode mode;
	int ctx_id = ((struct fimc_prv_data *)fh)->ctx_id;
	int ret = -1, i;

	ctx = &ctrl->out->ctx[ctx_id];
	buf = &ctx->overlay.buf;
	mode = ctx->overlay.mode;

	fimc_info1("%s: called\n", __func__);

	if (ctx->status != FIMC_STREAMOFF) {
		fimc_err("%s: FIMC is running\n", __func__);
		return -EBUSY;
	}

	if (ctx->is_requested == 1 && b->count != 0) {
		fimc_err("Buffers were already requested\n");
		return -EBUSY;
	}

	if (b->count > FIMC_OUTBUFS) {
		fimc_warn("The buffer count is modified by driver "
				"from %d to %d\n", b->count, FIMC_OUTBUFS);
		b->count = FIMC_OUTBUFS;
	}

#if (!defined(CONFIG_EXYNOS_DEV_PD) || !defined(CONFIG_PM_RUNTIME))
	fimc_hwset_output_buf_sequence_all(ctrl, FRAME_SEQ);
#endif

	fimc_init_out_buf(ctx);
	ctx->is_requested = 0;

	if (b->count == 0) {
		ctrl->mem.curr = ctrl->mem.base;

		switch (ctx->overlay.mode) {
		case FIMC_OVLY_DMA_AUTO:	/* fall through */
		case FIMC_OVLY_DMA_MANUAL:
			for (i = 0; i < FIMC_OUTBUFS; i++) {
				if (buf->vir_addr[i]) {
					ret = do_munmap(mm,
							buf->vir_addr[i],
							buf->size[i]);
					if (ret < 0)
						fimc_err("%s: do_munmap fail. "
						"vir_addr[%d](0x%08x)\n",
						__func__, i, buf->vir_addr[i]);
				}
			}
			break;
		default:
			break;
		}
	} else {
		/* initialize source buffers */
		if (b->memory == V4L2_MEMORY_MMAP) {
			ret = fimc_outdev_set_src_buf(ctrl, ctx);
			ctx->overlay.req_idx = FIMC_MMAP_IDX;
			if (ret)
				return ret;
		} else if (b->memory == V4L2_MEMORY_USERPTR) {
			if (mode == FIMC_OVLY_DMA_AUTO ||
					mode == FIMC_OVLY_NOT_FIXED)
				ctx->overlay.req_idx = FIMC_USERPTR_IDX;
		}

		ctx->is_requested = 1;
	}

	ctx->buf_num = b->count;

	return 0;
}

int fimc_querybuf_output(void *fh, struct v4l2_buffer *b)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	struct fimc_ctx *ctx;
	int ctx_id = ((struct fimc_prv_data *)fh)->ctx_id;
	u32 buf_length = 0;

	fimc_info1("%s: called\n", __func__);

	ctx = &ctrl->out->ctx[ctx_id];
	if (ctx->status != FIMC_STREAMOFF) {
		fimc_err("%s: FIMC is running\n", __func__);
		return -EBUSY;
	}

	if (b->index > ctx->buf_num) {
		fimc_err("The index is out of bounds. You requested %d buffers."
			"But requested index is %d\n", ctx->buf_num, b->index);
		return -EINVAL;
	}

	b->flags = ctx->src[b->index].flags;
	b->m.offset = b->index * PAGE_SIZE;
	buf_length = ctx->src[b->index].length[FIMC_ADDR_Y] +
		ctx->src[b->index].length[FIMC_ADDR_CB] +
		ctx->src[b->index].length[FIMC_ADDR_CR];
	b->length = PAGE_ALIGN(buf_length);

	return 0;
}

int fimc_g_ctrl_output(void *fh, struct v4l2_control *c)
{
	struct fimc_ctx *ctx;
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ctx_id = ((struct fimc_prv_data *)fh)->ctx_id;
	struct s3c_platform_fimc *pdata	= to_fimc_plat(ctrl->dev);
	ctx = &ctrl->out->ctx[ctx_id];

	if (ctx->status != FIMC_STREAMOFF) {
		fimc_err("%s: FIMC is running\n", __func__);
		return -EBUSY;
	}

	switch (c->id) {
	case V4L2_CID_ROTATION:
		c->value = ctx->rotate;
		break;

	case V4L2_CID_HFLIP:
		if (ctx->flip & V4L2_CID_HFLIP)
			c->value = 1;
		else
			c->value = 0;
		break;

	case V4L2_CID_VFLIP:
		if (ctx->flip & V4L2_CID_VFLIP)
			c->value = 1;
		else
			c->value = 0;
		break;

	case V4L2_CID_OVERLAY_VADDR0:
		c->value = ctx->overlay.buf.vir_addr[0];
		break;

	case V4L2_CID_OVERLAY_VADDR1:
		c->value = ctx->overlay.buf.vir_addr[1];
		break;

	case V4L2_CID_OVERLAY_VADDR2:
		c->value = ctx->overlay.buf.vir_addr[2];
		break;

	case V4L2_CID_OVERLAY_AUTO:
		if (ctx->overlay.mode == FIMC_OVLY_DMA_AUTO)
			c->value = 1;
		else
			c->value = 0;
		break;

	case V4L2_CID_RESERVED_MEM_BASE_ADDR:
		c->value = ctrl->mem.base;
		break;

	case V4L2_CID_FIMC_VERSION:
		c->value = pdata->hw_ver;
		break;

	default:
		fimc_err("Invalid control id: %d\n", c->id);
		return -EINVAL;
	}

	return 0;
}

static int fimc_set_dst_info(struct fimc_control *ctrl,
			     struct fimc_ctx *ctx,
			     struct fimc_buf *fimc_buf)
{
	struct fimc_buf *buf;
	int i;

	for (i = 0; i < ctx->buf_num; i++) {
		buf = &fimc_buf[i];
		ctx->dst[i].base[FIMC_ADDR_Y] = buf->base[FIMC_ADDR_Y];
		ctx->dst[i].length[FIMC_ADDR_Y] = buf->length[FIMC_ADDR_Y];

		ctx->dst[i].base[FIMC_ADDR_CB] = buf->base[FIMC_ADDR_CB];
		ctx->dst[i].length[FIMC_ADDR_CB] = buf->length[FIMC_ADDR_CB];

		ctx->dst[i].base[FIMC_ADDR_CR] = buf->base[FIMC_ADDR_CR];
		ctx->dst[i].length[FIMC_ADDR_CR] = buf->length[FIMC_ADDR_CR];
	}

	for (i = ctx->buf_num; i < FIMC_OUTBUFS; i++) {
		ctx->dst[i].base[FIMC_ADDR_Y] = 0;
		ctx->dst[i].length[FIMC_ADDR_Y] = 0;

		ctx->dst[i].base[FIMC_ADDR_CB] = 0;
		ctx->dst[i].length[FIMC_ADDR_CB] = 0;

		ctx->dst[i].base[FIMC_ADDR_CR] = 0;
		ctx->dst[i].length[FIMC_ADDR_CR] = 0;
	}

	/* for debugging */
	for (i = 0; i < FIMC_OUTBUFS; i++) {
		fimc_dbg("dst[%d]: base[0]=0x%08x, size[0]=0x%08x\n",
			i, ctx->dst[i].base[0], ctx->dst[i].length[0]);

		fimc_dbg("dst[%d]: base[1]=0x%08x, size[1]=0x%08x\n",
			i, ctx->dst[i].base[1], ctx->dst[i].length[2]);

		fimc_dbg("dst[%d]: base[2]=0x%08x, size[2]=0x%08x\n",
			i, ctx->dst[i].base[1], ctx->dst[i].length[2]);
	}

	return 0;
}
int fimc_s_ctrl_output(struct file *filp, void *fh, struct v4l2_control *c)
{
	struct fimc_ctx *ctx;
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ctx_id = ((struct fimc_prv_data *)fh)->ctx_id;
	int ret = 0;

	ctx = &ctrl->out->ctx[ctx_id];
	if (ctx->status != FIMC_STREAMOFF) {
		fimc_err("%s: FIMC is running\n", __func__);
		return -EBUSY;
	}

	switch (c->id) {
	case V4L2_CID_ROTATION:
		ret = fimc_set_rot_degree(ctrl, ctx, c->value);

		break;
	case V4L2_CID_HFLIP:
		if (c->value)
			ctx->flip |= FIMC_YFLIP;
		else
			ctx->flip &= ~FIMC_YFLIP;

		break;
	case V4L2_CID_VFLIP:
		if (c->value)
			ctx->flip |= FIMC_XFLIP;
		else
			ctx->flip &= ~FIMC_XFLIP;

		break;
	case V4L2_CID_OVERLAY_AUTO:
		if (c->value == 1) {
			ctx->overlay.mode = FIMC_OVLY_DMA_AUTO;
		} else {
			ctx->overlay.mode = FIMC_OVLY_DMA_MANUAL;
			ret = fimc_outdev_set_dst_buf(ctrl, ctx);
			fimc_outdev_overlay_buf(filp, ctrl, ctx);
		}

		break;
	case V4L2_CID_OVLY_MODE:
		ctx->overlay.mode = c->value;

		break;
	case V4L2_CID_DST_INFO:
		ret = fimc_set_dst_info(ctrl, ctx,
					(struct fimc_buf *)c->value);
		break;
	case V4L2_CID_GET_PHY_SRC_YADDR:
		c->value = ctx->src[c->value].base[FIMC_ADDR_Y];
		break;
	case V4L2_CID_GET_PHY_SRC_CADDR:
		c->value = ctx->src[c->value].base[FIMC_ADDR_CB];
		break;
	default:
		fimc_err("Invalid control id: %d\n", c->id);
		ret = -EINVAL;
	}

	return ret;
}

int fimc_cropcap_output(void *fh, struct v4l2_cropcap *a)
{
	struct fimc_ctx *ctx;
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ctx_id = ((struct fimc_prv_data *)fh)->ctx_id;
	u32 is_rotate = 0, max_w = 0, max_h = 0, pixelformat;

	fimc_info1("%s: called\n", __func__);

	ctx = &ctrl->out->ctx[ctx_id];
	pixelformat = ctx->pix.pixelformat;
	if (ctx->status != FIMC_STREAMOFF) {
		fimc_err("%s: FIMC is running\n", __func__);
		return -EBUSY;
	}

	is_rotate = fimc_mapping_rot_flip(ctx->rotate, ctx->flip);
	switch (pixelformat) {
	case V4L2_PIX_FMT_NV12:		/* fall through */
	case V4L2_PIX_FMT_NV21:		/* fall through */
	case V4L2_PIX_FMT_NV12T:	/* fall through */
	case V4L2_PIX_FMT_YUYV:		/* fall through */
	case V4L2_PIX_FMT_NV16:		/* fall through */
	case V4L2_PIX_FMT_YUV420:	/* fall through */
	case V4L2_PIX_FMT_YVU420:	/* fall through */
		max_w = FIMC_SRC_MAX_W;
		max_h = FIMC_SRC_MAX_H;
		break;
	case V4L2_PIX_FMT_RGB32:	/* fall through */
	case V4L2_PIX_FMT_RGB565:	/* fall through */
		if (is_rotate & FIMC_ROT) {		/* Landscape mode */
			max_w = ctrl->fb.lcd_vres;
			max_h = ctrl->fb.lcd_hres;
		} else {				/* Portrait */
			max_w = ctrl->fb.lcd_hres;
			max_h = ctrl->fb.lcd_vres;
		}

		break;
	default:
		fimc_warn("Supported format : V4L2_PIX_FMT_YUYV, "
			"V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_NV12T, "
			"V4L2_PIX_FMT_NV16, V4L2_PIX_FMT_RGB32, "
			"V4L2_PIX_FMT_RGB565\n");
		return -EINVAL;
	}

	/* crop bounds */
	ctx->cropcap.bounds.left = 0;
	ctx->cropcap.bounds.top = 0;
	ctx->cropcap.bounds.width = max_w;
	ctx->cropcap.bounds.height = max_h;

	/* crop default values */
	ctx->cropcap.defrect.left = 0;
	ctx->cropcap.defrect.top = 0;
	ctx->cropcap.defrect.width = max_w;
	ctx->cropcap.defrect.height = max_h;

	/* crop pixel aspec values */
	/* To Do : Have to modify but I don't know the meaning. */
	ctx->cropcap.pixelaspect.numerator = 16;
	ctx->cropcap.pixelaspect.denominator = 9;

	a->bounds = ctx->cropcap.bounds;
	a->defrect = ctx->cropcap.defrect;
	a->pixelaspect = ctx->cropcap.pixelaspect;

	return 0;
}

int fimc_g_crop_output(void *fh, struct v4l2_crop *a)
{
	struct fimc_ctx *ctx;
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ctx_id = ((struct fimc_prv_data *)fh)->ctx_id;

	ctx = &ctrl->out->ctx[ctx_id];

	fimc_info1("%s: called\n", __func__);

	mutex_lock(&ctrl->v4l2_lock);
	a->c.left   = ctx->crop.left;
	a->c.top    = ctx->crop.top;
	a->c.width  = ctx->crop.width;
	a->c.height = ctx->crop.height;
	mutex_unlock(&ctrl->v4l2_lock);

	return 0;
}

int fimc_s_crop_output(void *fh, struct v4l2_crop *a)
{
	struct fimc_ctx *ctx;
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ctx_id = ((struct fimc_prv_data *)fh)->ctx_id;

	fimc_info1("%s: called: left(%d), top(%d), width(%d), height(%d),\n",
		__func__, a->c.left, a->c.top, a->c.width, a->c.height);

	ctx = &ctrl->out->ctx[ctx_id];
	if (ctx->status != FIMC_STREAMOFF) {
		fimc_err("%s: FIMC is running\n", __func__);
		return -EBUSY;
	}

	/* Check arguments : widht and height */
	if ((a->c.width < 0) || (a->c.height < 0)) {
		fimc_err("The crop rect must be bigger than 0\n");
		fimc_err("width = %d, height = %d\n", a->c.width, a->c.height);
		return -EINVAL;
	}

	if ((a->c.width > FIMC_SRC_MAX_W) || (a->c.height > FIMC_SRC_MAX_H)) {
		fimc_err("The crop width/height must be smaller than "
			"%d and %d\n", FIMC_SRC_MAX_W, FIMC_SRC_MAX_H);
		fimc_err("width = %d, height = %d\n", a->c.width, a->c.height);
		return -EINVAL;
	}

	/* Check arguments : left and top */
	if ((a->c.left < 0) || (a->c.top < 0)) {
		fimc_err("The crop left, top must be  bigger than 0\n");
		fimc_err("left = %d, top = %d\n", a->c.left, a->c.top);
		return -EINVAL;
	}

	if ((a->c.left > FIMC_SRC_MAX_W) || (a->c.top > FIMC_SRC_MAX_H)) {
		fimc_err("The crop left, top must be smaller than %d, %d\n",
			FIMC_SRC_MAX_W, FIMC_SRC_MAX_H);
		fimc_err("left = %d, top = %d\n", a->c.left, a->c.top);
		return -EINVAL;
	}

	if ((a->c.left + a->c.width) > FIMC_SRC_MAX_W) {
		fimc_err("The crop rect must be in bound rect\n");
		fimc_err("left = %d, width = %d\n", a->c.left, a->c.width);
		return -EINVAL;
	}

	if ((a->c.top + a->c.height) > FIMC_SRC_MAX_H) {
		fimc_err("The crop rect must be in bound rect\n");
		fimc_err("top = %d, width = %d\n", a->c.top, a->c.height);
		return -EINVAL;
	}

	ctx->crop.left = a->c.left;
	ctx->crop.top = a->c.top;
	ctx->crop.width = a->c.width;
	ctx->crop.height = a->c.height;

	return 0;
}

int fimc_streamon_output(void *fh)
{
	struct fimc_ctx *ctx;
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ctx_id = ((struct fimc_prv_data *)fh)->ctx_id;
	int ret = -1;

	fimc_info1("%s: called\n", __func__);

	ctx = &ctrl->out->ctx[ctx_id];
	if (ctx->overlay.mode == FIMC_OVLY_NOT_FIXED)
		ctx->overlay.mode = FIMC_OVLY_MODE;

	/* initialize destination buffers */
	if (ctx->overlay.mode == FIMC_OVLY_DMA_AUTO) {
		ret = fimc_outdev_set_dst_buf(ctrl, ctx);
		if (ret)
			return ret;
	}

	ret = fimc_outdev_check_param(ctrl, ctx);
	if (ret < 0) {
		fimc_err("Fail: fimc_outdev_check_param\n");
		return ret;
	}

	ctx->status = FIMC_READY_ON;
	if (ctrl->status == FIMC_STREAMOFF)
		ctrl->status = FIMC_READY_ON;

	return ret;
}

void fimc_outdev_init_idxs(struct fimc_control *ctrl)
{
	ctrl->out->idxs.prev.ctx = -1;
	ctrl->out->idxs.prev.idx = -1;
	ctrl->out->idxs.active.ctx = -1;
	ctrl->out->idxs.active.idx = -1;
	ctrl->out->idxs.next.ctx = -1;
	ctrl->out->idxs.next.idx = -1;
}

int fimc_streamoff_output(void *fh)
{
	struct fimc_ctx *ctx;
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ctx_id = ((struct fimc_prv_data *)fh)->ctx_id;
	int ret = -1, i = 0, off_cnt = 0;
	struct s3cfb_user_window window;
	fimc_info1("%s: called\n", __func__);

	ctx = &ctrl->out->ctx[ctx_id];
	/* Move it to here to ignore fimc_irq_out_dma operation. */
	ctx->status = FIMC_STREAMOFF;

	if (ctx->overlay.mode == FIMC_OVLY_DMA_AUTO ||
			ctx->overlay.mode == FIMC_OVLY_DMA_MANUAL) {
		/* Need some delay to waiting reamined operation */
		msleep(100);

		ret = s3cfb_direct_ioctl(ctrl->id, S3CFB_SET_WIN_OFF,
			(unsigned long)NULL);
		if (ret < 0) {
			fimc_err("direct_ioctl(S3CFB_SET_WIN_OFF) fail\n");
			return -EINVAL;
		}

		/* reset WIN position */
		memset(&window, 0, sizeof(window));
		ret = s3cfb_direct_ioctl(ctrl->id, S3CFB_WIN_POSITION,
				(unsigned long)&window);
		if (ret < 0) {
			fimc_err("direct_ioctl(S3CFB_WIN_POSITION) fail\n");
			return -EINVAL;
		}

		ret = s3cfb_direct_ioctl(ctrl->id, S3CFB_SET_WIN_ADDR, 0x00000000);
		if (ret < 0) {
			fimc_err("direct_ioctl(S3CFB_SET_WIN_ADDR) fail\n");
			return -EINVAL;
		}

		ret = s3cfb_direct_ioctl(ctrl->id, S3CFB_SET_WIN_MEM, DMA_MEM_NONE);
		if (ret < 0) {
			fimc_err("direct_ioctl(S3CFB_SET_WIN_MEM) fail\n");
			return -EINVAL;
		}

		ctrl->fb.is_enable = 0;
	}

	ret = fimc_init_in_queue(ctrl, ctx);
	if (ret < 0) {
		fimc_err("Fail: fimc_init_in_queue\n");
		return -EINVAL;
	}

	ret = fimc_init_out_queue(ctrl, ctx);
	if (ret < 0) {
		fimc_err("Fail: fimc_init_out_queue\n");
		return -EINVAL;
	}

	/* Make all buffers DQUEUED state. */
	for (i = 0; i < FIMC_OUTBUFS; i++) {
		ctx->src[i].state = VIDEOBUF_IDLE;
		ctx->src[i].flags = V4L2_BUF_FLAG_MAPPED;
	}

	if (ctrl->out->last_ctx == ctx->ctx_num)
		ctrl->out->last_ctx = -1;

	if (ctx->overlay.mode == FIMC_OVLY_DMA_AUTO) {
		ctrl->mem.curr = ctx->dst[0].base[FIMC_ADDR_Y];
		for (i = 0; i < FIMC_OUTBUFS; i++) {
			ctx->dst[i].base[FIMC_ADDR_Y] = 0;
			ctx->dst[i].length[FIMC_ADDR_Y] = 0;

			ctx->dst[i].base[FIMC_ADDR_CB] = 0;
			ctx->dst[i].length[FIMC_ADDR_CB] = 0;

			ctx->dst[i].base[FIMC_ADDR_CR] = 0;
			ctx->dst[i].length[FIMC_ADDR_CR] = 0;
		}
	}

	/* check all ctx to change ctrl->status from streamon to streamoff */
	for (i = 0; i < FIMC_MAX_CTXS; i++) {
		if (ctrl->out->ctx[i].status == FIMC_STREAMOFF)
			off_cnt++;
	}

#if (!defined(CONFIG_EXYNOS_DEV_PD) || !defined(CONFIG_PM_RUNTIME))
	if (off_cnt == FIMC_MAX_CTXS) {
		ctrl->status = FIMC_STREAMOFF;
		fimc_outdev_init_idxs(ctrl);
		fimc_outdev_stop_camif(ctrl);
	}
#endif

	return 0;
}

static int fimc_outdev_start_operation(struct fimc_control *ctrl,
				       struct fimc_ctx *ctx, int idx)
{
	int ret = 0;
	unsigned long spin_flags;

	spin_lock_irqsave(&ctrl->out->slock, spin_flags);
	ret = fimc_outdev_start_camif(ctrl);
	if (ret < 0) {
		fimc_err("Fail: fimc_start_camif\n");
		return -EINVAL;
	}

	ctrl->out->idxs.active.idx = idx;
	ctrl->out->idxs.active.ctx = ctx->ctx_num;

	ctrl->status = FIMC_STREAMON;
	ctx->status = FIMC_STREAMON;
	spin_unlock_irqrestore(&ctrl->out->slock, spin_flags);

	return ret;
}

static int fimc_qbuf_output_single_buf(struct fimc_control *ctrl,
				       struct fimc_ctx *ctx,
				       int idx)
{
	struct fimc_buf_set buf_set;	/* destination addr */
	u32 format = ctx->fbuf.fmt.pixelformat;
	u32 width = ctx->fbuf.fmt.width;
	u32 height = ctx->fbuf.fmt.height;
	u32 y_size = width * height;
	u32 c_size = y_size >> 2;
	int ret = -1, i, cfg;
	u32 rot = ctx->rotate;

	fimc_outdev_set_src_addr(ctrl, ctx->src[idx].base);

	memset(&buf_set, 0x00, sizeof(buf_set));

	switch (format) {
	case V4L2_PIX_FMT_RGB32:
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_YUYV:
		buf_set.base[FIMC_ADDR_Y] = (dma_addr_t)ctx->fbuf.base;
		break;
	case V4L2_PIX_FMT_YUV420:
		buf_set.base[FIMC_ADDR_Y] = (dma_addr_t)ctx->fbuf.base;
		buf_set.base[FIMC_ADDR_CB] = buf_set.base[FIMC_ADDR_Y] + y_size;
		buf_set.base[FIMC_ADDR_CR] = buf_set.base[FIMC_ADDR_CB] + c_size;
		break;
	case V4L2_PIX_FMT_YVU420:
		buf_set.base[FIMC_ADDR_Y] = (dma_addr_t)ctx->fbuf.base;
		buf_set.base[FIMC_ADDR_CR] = buf_set.base[FIMC_ADDR_Y] + y_size;
		buf_set.base[FIMC_ADDR_CB] = buf_set.base[FIMC_ADDR_CR] + c_size;
		break;
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV16:
		buf_set.base[FIMC_ADDR_Y] = (dma_addr_t)ctx->fbuf.base;
		buf_set.base[FIMC_ADDR_CB] = buf_set.base[FIMC_ADDR_Y] + y_size;
		break;
	case V4L2_PIX_FMT_NV12M:
		buf_set.base[FIMC_ADDR_Y] = (dma_addr_t)ctx->fbuf.base;
		buf_set.base[FIMC_ADDR_CB] =
			ALIGN(buf_set.base[FIMC_ADDR_Y] + y_size, PAGE_SIZE - 1);
		break;
	case V4L2_PIX_FMT_NV12T:
		if (rot == 0 || rot == 180)
			fimc_get_nv12t_size(width, height, &y_size, &c_size);
		else
			fimc_get_nv12t_size(height, width, &y_size, &c_size);
		buf_set.base[FIMC_ADDR_Y] = (dma_addr_t)ctx->fbuf.base;
		buf_set.base[FIMC_ADDR_CB] = buf_set.base[FIMC_ADDR_Y] + y_size;
		break;
	default:
		fimc_err("%s: Invalid pixelformt : %d\n", __func__, format);
		return -EINVAL;
	}
	cfg = fimc_hwget_output_buf_sequence(ctrl);

	for (i = 0; i < FIMC_PHYBUFS; i++) {
		if (check_bit(cfg, i))
			fimc_hwset_output_address(ctrl, &buf_set, i);
	}

	ret = fimc_outdev_start_operation(ctrl, ctx, idx);
	if (ret < 0) {
		fimc_err("Fail: fimc_start_operation\n");
		return -EINVAL;
	}

	return 0;
}

static int fimc_qbuf_output_multi_buf(struct fimc_control *ctrl,
				      struct fimc_ctx *ctx,
				      int idx)
{
	struct fimc_buf_set buf_set;	/* destination addr */
	u32 format = ctx->fbuf.fmt.pixelformat;
	int ret = -1, i, cfg;

	fimc_outdev_set_src_addr(ctrl, ctx->src[idx].base);

	memset(&buf_set, 0x00, sizeof(buf_set));

	switch (format) {
	case V4L2_PIX_FMT_RGB32:
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_YUYV:
		buf_set.base[FIMC_ADDR_Y] = ctx->dst[idx].base[FIMC_ADDR_Y];
		break;
	case V4L2_PIX_FMT_YUV420:
		buf_set.base[FIMC_ADDR_Y] = ctx->dst[idx].base[FIMC_ADDR_Y];
		buf_set.base[FIMC_ADDR_CB] = ctx->dst[idx].base[FIMC_ADDR_CB];
		buf_set.base[FIMC_ADDR_CR] = ctx->dst[idx].base[FIMC_ADDR_CR];
		break;
	case V4L2_PIX_FMT_NV12:		/* fall through */
	case V4L2_PIX_FMT_NV21:		/* fall through */
	case V4L2_PIX_FMT_NV16:		/* fall through */
	case V4L2_PIX_FMT_NV12T:
		buf_set.base[FIMC_ADDR_Y] = ctx->dst[idx].base[FIMC_ADDR_Y];
		buf_set.base[FIMC_ADDR_CB] = ctx->dst[idx].base[FIMC_ADDR_CB];
		break;
	default:
		fimc_err("%s: Invalid pixelformt : %d\n", __func__, format);
		return -EINVAL;
	}

	cfg = fimc_hwget_output_buf_sequence(ctrl);

	for (i = 0; i < FIMC_PHYBUFS; i++) {
		if (check_bit(cfg, i))
			fimc_hwset_output_address(ctrl, &buf_set, i);
	}

	ret = fimc_outdev_start_operation(ctrl, ctx, idx);
	if (ret < 0) {
		fimc_err("Fail: fimc_start_operation\n");
		return -EINVAL;
	}

	return 0;
}

static int fimc_qbuf_output_dma_auto(struct fimc_control *ctrl,
				      struct fimc_ctx *ctx,
				      int idx)
{
	struct fb_var_screeninfo var;
	struct s3cfb_user_window window;
	struct v4l2_rect fimd_rect, fimd_rect_virtual;
	struct fimc_buf_set buf_set;	/* destination addr */
	u32 id = ctrl->id;
	int ret = -1, i, cfg;

	switch (ctx->status) {
	case FIMC_READY_ON:
		memset(&fimd_rect, 0, sizeof(struct v4l2_rect));
		ret = fimc_fimd_rect(ctrl, ctx, &fimd_rect);
		if (ret < 0) {
			fimc_err("fimc_fimd_rect fail\n");
			return -EINVAL;
		}

		/* Support any size */
		memcpy(&fimd_rect_virtual, &fimd_rect, sizeof(fimd_rect));
		fimc_outdev_dma_auto_dst_resize(&fimd_rect_virtual);

		/* Get WIN var_screeninfo */
		ret = s3cfb_direct_ioctl(id, FBIOGET_VSCREENINFO,
						(unsigned long)&var);
		if (ret < 0) {
			fimc_err("direct_ioctl(FBIOGET_VSCREENINFO) fail\n");
			return -EINVAL;
		}
		/* window path : DMA */
		ret = s3cfb_direct_ioctl(id, S3CFB_SET_WIN_PATH,
							DATA_PATH_DMA);
		if (ret < 0) {
			fimc_err("direct_ioctl(S3CFB_SET_WIN_PATH) fail\n");
			return -EINVAL;
		}

		/* Don't allocate the memory. */
		ret = s3cfb_direct_ioctl(id, S3CFB_SET_WIN_MEM, DMA_MEM_OTHER);
		if (ret < 0) {
			fimc_err("direct_ioctl(S3CFB_SET_WIN_MEM) fail\n");
			return -EINVAL;
		}

		/* Update WIN size */
		var.xres_virtual = fimd_rect_virtual.width;
		var.yres_virtual = fimd_rect_virtual.height;
		var.xres = fimd_rect.width;
		var.yres = fimd_rect.height;

		ret = s3cfb_direct_ioctl(id, FBIOPUT_VSCREENINFO,
							(unsigned long)&var);
		if (ret < 0) {
			fimc_err("direct_ioctl(FBIOPUT_VSCREENINFO) fail\n");
			return -EINVAL;
		}

		/* Update WIN position */
		window.x = fimd_rect.left;
		window.y = fimd_rect.top;
		ret = s3cfb_direct_ioctl(id, S3CFB_WIN_POSITION,
				(unsigned long)&window);
		if (ret < 0) {
			fimc_err("direct_ioctl(S3CFB_WIN_POSITION) fail\n");
			return -EINVAL;
		}

		/* fall through */

	case FIMC_STREAMON_IDLE:
		fimc_outdev_set_src_addr(ctrl, ctx->src[idx].base);

		memset(&buf_set, 0x00, sizeof(buf_set));
		buf_set.base[FIMC_ADDR_Y] = ctx->dst[idx].base[FIMC_ADDR_Y];
		cfg = fimc_hwget_output_buf_sequence(ctrl);

		for (i = 0; i < FIMC_PHYBUFS; i++) {
			if (check_bit(cfg, i))
				fimc_hwset_output_address(ctrl, &buf_set, i);
		}

		ret = fimc_outdev_start_operation(ctrl, ctx, idx);
		if (ret < 0) {
			fimc_err("Fail: fimc_start_operation\n");
			return -EINVAL;
		}
		break;

	default:
		break;
	}

	return 0;
}

static int fimc_qbuf_output_dma_manual(struct fimc_control *ctrl,
				      struct fimc_ctx *ctx,
				      int idx)
{
	struct fimc_buf_set buf_set;	/* destination addr */
	int ret = -1, i, cfg;

	fimc_outdev_set_src_addr(ctrl, ctx->src[idx].base);

	memset(&buf_set, 0x00, sizeof(buf_set));
	buf_set.base[FIMC_ADDR_Y] = ctx->dst[idx].base[FIMC_ADDR_Y];
	cfg = fimc_hwget_output_buf_sequence(ctrl);

	for (i = 0; i < FIMC_PHYBUFS; i++) {
		if (check_bit(cfg, i))
			fimc_hwset_output_address(ctrl, &buf_set, i);
	}

	ret = fimc_outdev_start_camif(ctrl);
	if (ret < 0) {
		fimc_err("Fail: fimc_start_camif\n");
		return -EINVAL;
	}

	ctrl->out->idxs.active.idx = idx;
	ctrl->out->idxs.active.ctx = ctx->ctx_num;

	ctrl->status = FIMC_STREAMON;
	ctx->status = FIMC_STREAMON;

	return 0;
}

static int fimc_qbuf_output_fifo(struct fimc_control *ctrl,
				      struct fimc_ctx *ctx,
				      int idx)
{
	int ret = -1;

#if defined(CONFIG_VIDEO_IPC)
	if (ctx->pix.field == V4L2_FIELD_INTERLACED_TB)
		ipc_start();
#endif

	fimc_outdev_set_src_addr(ctrl, ctx->src[idx].base);

	ret = fimc_start_fifo(ctrl, ctx);
	if (ret < 0) {
		fimc_err("Fail: fimc_start_fifo\n");
		return -EINVAL;
	}

	ctrl->out->idxs.active.idx = idx;
	ctrl->out->idxs.active.ctx = ctx->ctx_num;

	ctrl->status = FIMC_STREAMON;
	ctx->status = FIMC_STREAMON;

	return 0;
}

static int fimc_update_in_queue_addr(struct fimc_control *ctrl,
				     struct fimc_ctx *ctx,
				     u32 idx, dma_addr_t *addr)
{
	if (idx >= FIMC_OUTBUFS) {
		fimc_err("%s: Failed\n", __func__);
		return -EINVAL;
	}

	ctx->src[idx].base[FIMC_ADDR_Y] = addr[FIMC_ADDR_Y];
	if (ctx->pix.pixelformat == V4L2_PIX_FMT_YVU420) {
		ctx->src[idx].base[FIMC_ADDR_CB] = addr[FIMC_ADDR_CR];
		ctx->src[idx].base[FIMC_ADDR_CR] = addr[FIMC_ADDR_CB];
	} else {
		ctx->src[idx].base[FIMC_ADDR_CB] = addr[FIMC_ADDR_CB];
		ctx->src[idx].base[FIMC_ADDR_CR] = addr[FIMC_ADDR_CR];
	}

	return 0;
}

int fimc_qbuf_output(void *fh, struct v4l2_buffer *b)
{
	struct fimc_buf *buf = (struct fimc_buf *)b->m.userptr;
	struct fimc_ctx *ctx;
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ctx_id = ((struct fimc_prv_data *)fh)->ctx_id;
	int idx, ctx_num;
	int ret = -1;

	ctx = &ctrl->out->ctx[ctx_id];
	fimc_info2("ctx(%d) queued idx = %d\n", ctx->ctx_num, b->index);
	if (ctx->status == FIMC_STREAMOFF) {
		printk("%s:: can not queue is FIMC_STREAMOFF status \n",
				__func__);
		return ret;
	}

	if (b->index > ctx->buf_num) {
		fimc_err("The index is out of bounds. "
			"You requested %d buffers. "
			"But you set the index as %d\n",
			ctx->buf_num, b->index);
		return -EINVAL;
	}

	/* Check the buffer state if the state is VIDEOBUF_IDLE. */
	if (ctx->src[b->index].state != VIDEOBUF_IDLE) {
		fimc_err("The index(%d) buffer must be dequeued state(%d)\n",
				 b->index, ctx->src[b->index].state);
		return -EINVAL;
	}

	if ((ctrl->status == FIMC_READY_ON) ||
	    (ctrl->status == FIMC_STREAMON) ||
	    (ctrl->status == FIMC_STREAMON_IDLE)) {
		if (b->memory == V4L2_MEMORY_USERPTR) {
			ret = fimc_update_in_queue_addr(ctrl, ctx, b->index, buf->base);
			if (ret < 0)
				return ret;
		}

#if defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME)
		pm_runtime_get_sync(ctrl->dev);
#endif

		/* Attach the buffer to the incoming queue. */
		ret = fimc_push_inq(ctrl, ctx, b->index);
		if (ret < 0) {
			fimc_err("Fail: fimc_push_inq\n");
#if defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME)
			pm_runtime_put_sync(ctrl->dev);
#endif
			return -EINVAL;
		}
	}

	if ((ctrl->status == FIMC_READY_ON) ||
	    (ctrl->status == FIMC_STREAMON_IDLE)) {
		ret = fimc_pop_inq(ctrl, &ctx_num, &idx);
		if (ret < 0) {
			fimc_err("Fail: fimc_pop_inq\n");
			return -EINVAL;
		}

		ctx = &ctrl->out->ctx[ctx_num];
		if (ctx_num != ctrl->out->last_ctx) {
			ctrl->out->last_ctx = ctx->ctx_num;
			ret = fimc_outdev_set_ctx_param(ctrl, ctx);
			if (ret < 0) {
				ctx->src[b->index].state = VIDEOBUF_IDLE;
				ctrl->out->last_ctx = -1;
				fimc_err("Fail: fimc_outdev_set_ctx_param\n");
				return -EINVAL;
			}
		}

		switch (ctx->overlay.mode) {
		case FIMC_OVLY_FIFO:
			ret = fimc_qbuf_output_fifo(ctrl, ctx, idx);
			break;
		case FIMC_OVLY_DMA_AUTO:
			ret = fimc_qbuf_output_dma_auto(ctrl, ctx, idx);
			break;
		case FIMC_OVLY_DMA_MANUAL:
			ret = fimc_qbuf_output_dma_manual(ctrl, ctx, idx);
			break;
		case FIMC_OVLY_NONE_SINGLE_BUF:
			ret = fimc_qbuf_output_single_buf(ctrl, ctx, idx);
			break;
		case FIMC_OVLY_NONE_MULTI_BUF:
			ret = fimc_qbuf_output_multi_buf(ctrl, ctx, idx);
			break;
		default:
			break;
		}
	}

	return ret;
}

int fimc_dqbuf_output(void *fh, struct v4l2_buffer *b)
{
	struct fimc_ctx *ctx;
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ctx_id = ((struct fimc_prv_data *)fh)->ctx_id;
	int idx = -1, ret = -1;

	ctx = &ctrl->out->ctx[ctx_id];
	ret = fimc_pop_outq(ctrl, ctx, &idx);
	if (ret < 0) {
		ret = wait_event_timeout(ctrl->wq, (ctx->outq[0] != -1),
							FIMC_DQUEUE_TIMEOUT);
		if (ret == 0) {
			fimc_dump_context(ctrl, ctx);
			fimc_err("[0] out_queue is empty\n");
			return -EAGAIN;
		} else if (ret == -ERESTARTSYS) {
			fimc_print_signal(ctrl);
		} else {
			/* Normal case */
			ret = fimc_pop_outq(ctrl, ctx, &idx);
			if (ret < 0) {
				fimc_err("[1] out_queue is empty\n");
				fimc_dump_context(ctrl, ctx);
				return -EINVAL;
			}
		}
	}

	b->index = idx;

	fimc_info2("ctx(%d) dqueued idx = %d\n", ctx->ctx_num, b->index);

	return ret;
}

int fimc_g_fmt_vid_out(struct file *filp, void *fh, struct v4l2_format *f)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	struct fimc_outinfo *out = ctrl->out;
	struct fimc_ctx *ctx;
	int ctx_id = ((struct fimc_prv_data *)fh)->ctx_id;
	int i, j;
	int in_use;

	fimc_info1("%s: called\n", __func__);

	if (!out) {
		out = kzalloc(sizeof(*out), GFP_KERNEL);
		if (!out) {
			fimc_err("%s: no memory for outdev info\n", __func__);
			return -ENOMEM;
		}
		ctrl->out = out;

		/* init: struct fimc_outinfo */
		out->last_ctx = -1;

		spin_lock_init(&ctrl->out->lock_in);
		spin_lock_init(&ctrl->out->lock_out);
		spin_lock_init(&ctrl->out->slock);

		for (i = 0; i < FIMC_INQUEUES; i++) {
			ctrl->out->inq[i].ctx = -1;
			ctrl->out->inq[i].idx = -1;
		}

		for (i = 0; i < FIMC_MAX_CTXS; i++) {
			ctx = &ctrl->out->ctx[i];
			ctx->ctx_num = i;
			ctx->overlay.mode = FIMC_OVLY_NOT_FIXED;
			ctx->status = FIMC_STREAMOFF;

			for (j = 0; j < FIMC_OUTBUFS; j++) {
				ctx->inq[j] = -1;
				ctx->outq[j] = -1;
			}
		}

		ctrl->out->idxs.prev.ctx = -1;
		ctrl->out->idxs.prev.idx = -1;
		ctrl->out->idxs.active.ctx = -1;
		ctrl->out->idxs.active.idx = -1;
		ctrl->out->idxs.next.ctx = -1;
		ctrl->out->idxs.next.idx = -1;

		in_use = atomic_read(&ctrl->in_use);
		for (i = 0; i < in_use; i++)
			ctrl->out->ctx_used[i] = true;
		for (i = in_use; i < FIMC_MAX_CTXS; i++)
			ctrl->out->ctx_used[i] = false;
	}

	f->fmt.pix = ctrl->out->ctx[ctx_id].pix;

	return 0;
}

int fimc_try_fmt_vid_out(struct file *filp, void *fh, struct v4l2_format *f)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ctx_id = ((struct fimc_prv_data *)fh)->ctx_id;
	struct fimc_ctx *ctx;
	u32 format = f->fmt.pix.pixelformat;

	fimc_info1("%s: called. width(%d), height(%d)\n", __func__,
				f->fmt.pix.width, f->fmt.pix.height);

	ctx = &ctrl->out->ctx[ctx_id];
	if (ctx->status != FIMC_STREAMOFF) {
		fimc_err("%s: FIMC is running\n", __func__);
		return -EBUSY;
	}

	/* Check pixel format */
	switch (format) {
	case V4L2_PIX_FMT_NV12:		/* fall through */
	case V4L2_PIX_FMT_NV21:		/* fall through */
	case V4L2_PIX_FMT_NV12T:	/* fall through */
	case V4L2_PIX_FMT_YUYV:		/* fall through */
	case V4L2_PIX_FMT_NV16:		/* fall through */
	case V4L2_PIX_FMT_YUV420:	/* fall through */
	case V4L2_PIX_FMT_RGB32:	/* fall through */
	case V4L2_PIX_FMT_RGB565:	/* fall through */
		break;
	default:
		fimc_warn("Supported format : V4L2_PIX_FMT_YUYV, "
			"V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_NV12T, "
			"V4L2_PIX_FMT_RGB32, V4L2_PIX_FMT_RGB565\n");
		fimc_warn("Changed format : V4L2_PIX_FMT_RGB32\n");
		f->fmt.pix.pixelformat = V4L2_PIX_FMT_RGB32;
		return -EINVAL;
	}

	/* Fill the return value. */
	switch (format) {
	case V4L2_PIX_FMT_RGB32:
		f->fmt.pix.bytesperline = f->fmt.pix.width << 2;
		break;
	case V4L2_PIX_FMT_YUYV:		/* fall through */
	case V4L2_PIX_FMT_NV16:		/* fall through */
	case V4L2_PIX_FMT_YUV420:	/* fall through */
	case V4L2_PIX_FMT_RGB565:
		f->fmt.pix.bytesperline = f->fmt.pix.width << 1;
		break;
	case V4L2_PIX_FMT_NV12:		/* fall through */
	case V4L2_PIX_FMT_NV21:		/* fall through */
	case V4L2_PIX_FMT_NV12T:
		f->fmt.pix.bytesperline = (f->fmt.pix.width * 3) >> 1;
		break;

	default:
		/* dummy value*/
		f->fmt.pix.bytesperline = f->fmt.pix.width;
	}

	f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * f->fmt.pix.height;
	ctx->crop.left = 0;
	ctx->crop.top = 0;
	ctx->crop.width = f->fmt.pix.width;
	ctx->crop.height = f->fmt.pix.height;

	return 0;
}

int fimc_s_fmt_vid_out(struct file *filp, void *fh, struct v4l2_format *f)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ctx_id = ((struct fimc_prv_data *)fh)->ctx_id;
	struct fimc_ctx *ctx;
	int ret = -1;

	fimc_info1("%s: called\n", __func__);

	/* Check stream status */
	ctx = &ctrl->out->ctx[ctx_id];
	if (ctx->status != FIMC_STREAMOFF) {
		fimc_err("%s: FIMC is running\n", __func__);
		return -EBUSY;
	}

	ret = fimc_try_fmt_vid_out(filp, fh, f);
	if (ret < 0)
		return ret;

	ctx->pix = f->fmt.pix;

	return ret;
}

int fimc_init_in_queue(struct fimc_control *ctrl, struct fimc_ctx *ctx)
{
	struct fimc_idx swap_queue[FIMC_INQUEUES];
	int swap_cnt = 0, i;
	unsigned long spin_flags;

	spin_lock_irqsave(&ctrl->out->lock_in, spin_flags);

	/* init incoming queue */
	for (i = 0; i < FIMC_OUTBUFS; i++)
		ctx->inq[i] = -1;

	/* init common incoming queue */
	for (i = 0; i < FIMC_INQUEUES; i++) {
		if (ctrl->out->inq[i].ctx != ctx->ctx_num) {
			swap_queue[swap_cnt].ctx = ctrl->out->inq[i].ctx;
			swap_queue[swap_cnt].idx = ctrl->out->inq[i].idx;
			swap_cnt++;
		}

		ctrl->out->inq[i].ctx = -1;
		ctrl->out->inq[i].idx = -1;
	}

	/* restore common incoming queue */
	for (i = 0; i < swap_cnt; i++) {
		ctrl->out->inq[i].ctx = swap_queue[i].ctx;
		ctrl->out->inq[i].idx = swap_queue[i].idx;
 }

	spin_unlock_irqrestore(&ctrl->out->lock_in, spin_flags);

	return 0;
}

int fimc_init_out_queue(struct fimc_control *ctrl, struct fimc_ctx *ctx)
{
	unsigned long spin_flags;
	int i;

	spin_lock_irqsave(&ctrl->out->lock_out, spin_flags);

	/* Init incoming queue */
	for (i = 0; i < FIMC_OUTBUFS; i++)
		ctx->outq[i] = -1;

	spin_unlock_irqrestore(&ctrl->out->lock_out, spin_flags);

	return 0;
}

int fimc_push_inq(struct fimc_control *ctrl, struct fimc_ctx *ctx, int idx)
{
	struct fimc_idx swap_common_inq[FIMC_INQUEUES];
	int swap_queue[FIMC_OUTBUFS];
	int i;
	unsigned long spin_flags;

	fimc_dbg("%s: idx = %d\n", __func__, idx);

	if (ctrl->out->inq[FIMC_INQUEUES-1].idx != -1) {
		fimc_err("FULL: common incoming queue(%d)\n",
				ctrl->out->inq[FIMC_INQUEUES-1].idx);
		return -EBUSY;
	}

	spin_lock_irqsave(&ctrl->out->lock_in, spin_flags);

	/* ctx own incoming queue */
	/* Backup original queue */
	for (i = 0; i < FIMC_OUTBUFS; i++)
		swap_queue[i] = ctx->inq[i];

	/* Attach new idx */
	ctx->inq[0] = idx;
	ctx->src[idx].state = VIDEOBUF_QUEUED;
	ctx->src[idx].flags = V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_QUEUED;

	/* Shift the origonal queue */
	for (i = 1; i < FIMC_OUTBUFS; i++)
		ctx->inq[i] = swap_queue[i-1];

	/* Common incoming queue */
	/* Backup original queue */
	for (i = 0; i < FIMC_INQUEUES; i++) {
		swap_common_inq[i].ctx = ctrl->out->inq[i].ctx;
		swap_common_inq[i].idx = ctrl->out->inq[i].idx;
	}

	/* Attach new idx */
	ctrl->out->inq[0].ctx = ctx->ctx_num;
	ctrl->out->inq[0].idx = idx;

	/* Shift the origonal queue */
	for (i = 1; i < FIMC_INQUEUES; i++) {
		ctrl->out->inq[i].ctx = swap_common_inq[i-1].ctx;
		ctrl->out->inq[i].idx = swap_common_inq[i-1].idx;
	}

	spin_unlock_irqrestore(&ctrl->out->lock_in, spin_flags);

	return 0;
}

int fimc_pop_inq(struct fimc_control *ctrl, int *ctx_num, int *idx)
{
	struct fimc_ctx *ctx;
	unsigned long spin_flags;
	int i, ret = 0;
	int ctx_idx = -1;

	spin_lock_irqsave(&ctrl->out->lock_in, spin_flags);

	/* find valid index from common incoming queue */
	for (i = (FIMC_INQUEUES-1); i >= 0; i--) {
		if (ctrl->out->inq[i].ctx != -1) {
			*ctx_num = ctrl->out->inq[i].ctx;
			*idx = ctrl->out->inq[i].idx;
			ctrl->out->inq[i].ctx = -1;
			ctrl->out->inq[i].idx = -1;
			break;
		}
	}

	/* common incoming queue is empty. */
	if (i < 0) {
		spin_unlock_irqrestore(&ctrl->out->lock_in, spin_flags);
		return -EINVAL;
	}

	/* find valid index from incoming queue. */
	ctx = &ctrl->out->ctx[*ctx_num];
	for (i = (FIMC_OUTBUFS-1); i >= 0; i--) {
		if (ctx->inq[i] != -1) {
			ctx_idx = ctx->inq[i];
			ctx->inq[i] = -1;
			ctx->src[ctx_idx].state = VIDEOBUF_ACTIVE;
			ctx->src[ctx_idx].flags = V4L2_BUF_FLAG_MAPPED;
			break;
		}
	}

	if (*idx != ctx_idx)
		fimc_err("common inq(%d) vs inq(%d) mismatch\n", *idx, ctx_idx);

	/* incoming queue is empty. */
	if (i < 0)
		ret = -EINVAL;
	else
		fimc_dbg("%s: index = %d\n", __func__, *idx);

	spin_unlock_irqrestore(&ctrl->out->lock_in, spin_flags);

	return ret;
}

int fimc_push_outq(struct fimc_control *ctrl, struct fimc_ctx *ctx, int idx)
{
	unsigned long spin_flags;
	int swap_queue[FIMC_OUTBUFS];
	int i;

	fimc_dbg("%s: index = %d\n", __func__, idx);

	spin_lock_irqsave(&ctrl->out->lock_out, spin_flags);

	/* Backup original queue */
	for (i = 0; i < FIMC_OUTBUFS; i++)
		swap_queue[i] = ctx->outq[i];

	/* Attach new index */
	ctx->outq[0] = idx;
	ctx->src[idx].state = VIDEOBUF_DONE;
	ctx->src[idx].flags = V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_DONE;

	/* Shift the origonal queue */
	for (i = 1; i < FIMC_OUTBUFS; i++)
		ctx->outq[i] = swap_queue[i-1];

	spin_unlock_irqrestore(&ctrl->out->lock_out, spin_flags);

	return 0;
}

int fimc_pop_outq(struct fimc_control *ctrl, struct fimc_ctx *ctx, int *idx)
{
	unsigned long spin_flags;
	int i, ret = 0;

	spin_lock_irqsave(&ctrl->out->lock_out, spin_flags);

	/* Find last valid idx in outgoing queue. */
	for (i = (FIMC_OUTBUFS-1); i >= 0; i--) {
		if (ctx->outq[i] != -1) {
			*idx = ctx->outq[i];
			ctx->outq[i] = -1;
			ctx->src[*idx].state = VIDEOBUF_IDLE;
			ctx->src[*idx].flags = V4L2_BUF_FLAG_MAPPED;
			break;
		}
	}

	/* outgoing queue is empty. */
	if (i < 0) {
		ret = -EINVAL;
		fimc_dbg("%s: outgoing queue : %d, %d, %d\n", __func__,
				ctx->outq[0], ctx->outq[1], ctx->outq[2]);
	} else
		fimc_dbg("%s: idx = %d\n", __func__, *idx);


	spin_unlock_irqrestore(&ctrl->out->lock_out, spin_flags);

	return ret;
}

void fimc_dump_context(struct fimc_control *ctrl, struct fimc_ctx *ctx)
{
	int i = 0;

	fimc_err("ctx%d, ctrl->status: %d, ctx->status: %d\n",
		ctx->ctx_num, ctrl->status, ctx->status);

	for (i = 0; i < FIMC_INQUEUES; i++)
		fimc_err("ctrl->inq[%d]: ctx(%d) idx(%d)\n",
			i, ctrl->out->inq[i].ctx, ctrl->out->inq[i].idx);

	for (i = 0; i < FIMC_OUTBUFS; i++)
		fimc_err("inq[%d] = %d\n", i, ctx->inq[i]);

	for (i = 0; i < FIMC_OUTBUFS; i++)
		fimc_err("outq[%d] = %d\n", i, ctx->outq[i]);

	fimc_err("state : prev.ctx(%d), prev.idx(%d) "
			"active.ctx(%d), active.idx(%d) "
			"next.ctx(%d), next.idx(%d)\n",
		ctrl->out->idxs.prev.ctx, ctrl->out->idxs.prev.idx,
		ctrl->out->idxs.active.ctx, ctrl->out->idxs.active.idx,
		ctrl->out->idxs.next.ctx, ctrl->out->idxs.next.idx);
}

void fimc_print_signal(struct fimc_control *ctrl)
{
	if (signal_pending(current)) {
		fimc_dbg(".pend=%.8lx shpend=%.8lx\n",
			current->pending.signal.sig[0],
			current->signal->shared_pending.signal.sig[0]);
	} else {
		fimc_dbg(":pend=%.8lx shpend=%.8lx\n",
			current->pending.signal.sig[0],
			current->signal->shared_pending.signal.sig[0]);
	}
}
