// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI Camera Access Layer (CAL) - Video Device
 *
 * Copyright (c) 2015-2020 Texas Instruments Inc.
 *
 * Authors:
 *	Benoit Parrot <bparrot@ti.com>
 *	Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <linux/ioctl.h>
#include <linux/pm_runtime.h>
#include <linux/videodev2.h>

#include <media/media-device.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "cal.h"

/*  Print Four-character-code (FOURCC) */
static char *fourcc_to_str(u32 fmt)
{
	static char code[5];

	code[0] = (unsigned char)(fmt & 0xff);
	code[1] = (unsigned char)((fmt >> 8) & 0xff);
	code[2] = (unsigned char)((fmt >> 16) & 0xff);
	code[3] = (unsigned char)((fmt >> 24) & 0xff);
	code[4] = '\0';

	return code;
}

/* ------------------------------------------------------------------
 *	V4L2 Common IOCTLs
 * ------------------------------------------------------------------
 */

static int cal_querycap(struct file *file, void *priv,
			struct v4l2_capability *cap)
{
	strscpy(cap->driver, CAL_MODULE_NAME, sizeof(cap->driver));
	strscpy(cap->card, CAL_MODULE_NAME, sizeof(cap->card));

	return 0;
}

static int cal_g_fmt_vid_cap(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct cal_ctx *ctx = video_drvdata(file);

	*f = ctx->v_fmt;

	return 0;
}

/* ------------------------------------------------------------------
 *	V4L2 Video Node Centric IOCTLs
 * ------------------------------------------------------------------
 */

static const struct cal_format_info *find_format_by_pix(struct cal_ctx *ctx,
							u32 pixelformat)
{
	const struct cal_format_info *fmtinfo;
	unsigned int k;

	for (k = 0; k < ctx->num_active_fmt; k++) {
		fmtinfo = ctx->active_fmt[k];
		if (fmtinfo->fourcc == pixelformat)
			return fmtinfo;
	}

	return NULL;
}

static const struct cal_format_info *find_format_by_code(struct cal_ctx *ctx,
							 u32 code)
{
	const struct cal_format_info *fmtinfo;
	unsigned int k;

	for (k = 0; k < ctx->num_active_fmt; k++) {
		fmtinfo = ctx->active_fmt[k];
		if (fmtinfo->code == code)
			return fmtinfo;
	}

	return NULL;
}

static int cal_legacy_enum_fmt_vid_cap(struct file *file, void *priv,
				       struct v4l2_fmtdesc *f)
{
	struct cal_ctx *ctx = video_drvdata(file);
	const struct cal_format_info *fmtinfo;

	if (f->index >= ctx->num_active_fmt)
		return -EINVAL;

	fmtinfo = ctx->active_fmt[f->index];

	f->pixelformat = fmtinfo->fourcc;
	f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	return 0;
}

static int __subdev_get_format(struct cal_ctx *ctx,
			       struct v4l2_mbus_framefmt *fmt)
{
	struct v4l2_subdev_format sd_fmt;
	struct v4l2_mbus_framefmt *mbus_fmt = &sd_fmt.format;
	int ret;

	sd_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	sd_fmt.pad = 0;

	ret = v4l2_subdev_call(ctx->phy->source, pad, get_fmt, NULL, &sd_fmt);
	if (ret)
		return ret;

	*fmt = *mbus_fmt;

	ctx_dbg(1, ctx, "%s %dx%d code:%04X\n", __func__,
		fmt->width, fmt->height, fmt->code);

	return 0;
}

static int __subdev_set_format(struct cal_ctx *ctx,
			       struct v4l2_mbus_framefmt *fmt)
{
	struct v4l2_subdev_format sd_fmt;
	struct v4l2_mbus_framefmt *mbus_fmt = &sd_fmt.format;
	int ret;

	sd_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	sd_fmt.pad = 0;
	*mbus_fmt = *fmt;

	ret = v4l2_subdev_call(ctx->phy->source, pad, set_fmt, NULL, &sd_fmt);
	if (ret)
		return ret;

	ctx_dbg(1, ctx, "%s %dx%d code:%04X\n", __func__,
		fmt->width, fmt->height, fmt->code);

	return 0;
}

static void cal_calc_format_size(struct cal_ctx *ctx,
				 const struct cal_format_info *fmtinfo,
				 struct v4l2_format *f)
{
	u32 bpl, max_width;

	/*
	 * Maximum width is bound by the DMA max width in bytes.
	 * We need to recalculate the actual maxi width depending on the
	 * number of bytes per pixels required.
	 */
	max_width = CAL_MAX_WIDTH_BYTES / (ALIGN(fmtinfo->bpp, 8) >> 3);
	v4l_bound_align_image(&f->fmt.pix.width, 48, max_width, 2,
			      &f->fmt.pix.height, 32, CAL_MAX_HEIGHT_LINES,
			      0, 0);

	bpl = (f->fmt.pix.width * ALIGN(fmtinfo->bpp, 8)) >> 3;
	f->fmt.pix.bytesperline = ALIGN(bpl, 16);

	f->fmt.pix.sizeimage = f->fmt.pix.height *
			       f->fmt.pix.bytesperline;

	ctx_dbg(3, ctx, "%s: fourcc: %s size: %dx%d bpl:%d img_size:%d\n",
		__func__, fourcc_to_str(f->fmt.pix.pixelformat),
		f->fmt.pix.width, f->fmt.pix.height,
		f->fmt.pix.bytesperline, f->fmt.pix.sizeimage);
}

static int cal_legacy_try_fmt_vid_cap(struct file *file, void *priv,
				      struct v4l2_format *f)
{
	struct cal_ctx *ctx = video_drvdata(file);
	const struct cal_format_info *fmtinfo;
	struct v4l2_subdev_frame_size_enum fse;
	int found;

	fmtinfo = find_format_by_pix(ctx, f->fmt.pix.pixelformat);
	if (!fmtinfo) {
		ctx_dbg(3, ctx, "Fourcc format (0x%08x) not found.\n",
			f->fmt.pix.pixelformat);

		/* Just get the first one enumerated */
		fmtinfo = ctx->active_fmt[0];
		f->fmt.pix.pixelformat = fmtinfo->fourcc;
	}

	f->fmt.pix.field = ctx->v_fmt.fmt.pix.field;

	/* check for/find a valid width/height */
	found = false;
	fse.pad = 0;
	fse.code = fmtinfo->code;
	fse.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	for (fse.index = 0; ; fse.index++) {
		int ret;

		ret = v4l2_subdev_call(ctx->phy->source, pad, enum_frame_size,
				       NULL, &fse);
		if (ret)
			break;

		if ((f->fmt.pix.width == fse.max_width) &&
		    (f->fmt.pix.height == fse.max_height)) {
			found = true;
			break;
		} else if ((f->fmt.pix.width >= fse.min_width) &&
			 (f->fmt.pix.width <= fse.max_width) &&
			 (f->fmt.pix.height >= fse.min_height) &&
			 (f->fmt.pix.height <= fse.max_height)) {
			found = true;
			break;
		}
	}

	if (!found) {
		/* use existing values as default */
		f->fmt.pix.width = ctx->v_fmt.fmt.pix.width;
		f->fmt.pix.height =  ctx->v_fmt.fmt.pix.height;
	}

	/*
	 * Use current colorspace for now, it will get
	 * updated properly during s_fmt
	 */
	f->fmt.pix.colorspace = ctx->v_fmt.fmt.pix.colorspace;
	cal_calc_format_size(ctx, fmtinfo, f);
	return 0;
}

static int cal_legacy_s_fmt_vid_cap(struct file *file, void *priv,
				    struct v4l2_format *f)
{
	struct cal_ctx *ctx = video_drvdata(file);
	struct vb2_queue *q = &ctx->vb_vidq;
	struct v4l2_subdev_format sd_fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.pad = CAL_CAMERARX_PAD_SINK,
	};
	const struct cal_format_info *fmtinfo;
	int ret;

	if (vb2_is_busy(q)) {
		ctx_dbg(3, ctx, "%s device busy\n", __func__);
		return -EBUSY;
	}

	ret = cal_legacy_try_fmt_vid_cap(file, priv, f);
	if (ret < 0)
		return ret;

	fmtinfo = find_format_by_pix(ctx, f->fmt.pix.pixelformat);

	v4l2_fill_mbus_format(&sd_fmt.format, &f->fmt.pix, fmtinfo->code);

	ret = __subdev_set_format(ctx, &sd_fmt.format);
	if (ret)
		return ret;

	/* Just double check nothing has gone wrong */
	if (sd_fmt.format.code != fmtinfo->code) {
		ctx_dbg(3, ctx,
			"%s subdev changed format on us, this should not happen\n",
			__func__);
		return -EINVAL;
	}

	v4l2_fill_pix_format(&ctx->v_fmt.fmt.pix, &sd_fmt.format);
	ctx->v_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ctx->v_fmt.fmt.pix.pixelformat = fmtinfo->fourcc;
	ctx->v_fmt.fmt.pix.field = sd_fmt.format.field;
	cal_calc_format_size(ctx, fmtinfo, &ctx->v_fmt);

	v4l2_subdev_call(&ctx->phy->subdev, pad, set_fmt, NULL, &sd_fmt);

	ctx->fmtinfo = fmtinfo;
	*f = ctx->v_fmt;

	return 0;
}

static int cal_legacy_enum_framesizes(struct file *file, void *fh,
				      struct v4l2_frmsizeenum *fsize)
{
	struct cal_ctx *ctx = video_drvdata(file);
	const struct cal_format_info *fmtinfo;
	struct v4l2_subdev_frame_size_enum fse;
	int ret;

	/* check for valid format */
	fmtinfo = find_format_by_pix(ctx, fsize->pixel_format);
	if (!fmtinfo) {
		ctx_dbg(3, ctx, "Invalid pixel code: %x\n",
			fsize->pixel_format);
		return -EINVAL;
	}

	fse.index = fsize->index;
	fse.pad = 0;
	fse.code = fmtinfo->code;
	fse.which = V4L2_SUBDEV_FORMAT_ACTIVE;

	ret = v4l2_subdev_call(ctx->phy->source, pad, enum_frame_size, NULL,
			       &fse);
	if (ret)
		return ret;

	ctx_dbg(1, ctx, "%s: index: %d code: %x W:[%d,%d] H:[%d,%d]\n",
		__func__, fse.index, fse.code, fse.min_width, fse.max_width,
		fse.min_height, fse.max_height);

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = fse.max_width;
	fsize->discrete.height = fse.max_height;

	return 0;
}

static int cal_legacy_enum_input(struct file *file, void *priv,
				 struct v4l2_input *inp)
{
	if (inp->index > 0)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	sprintf(inp->name, "Camera %u", inp->index);
	return 0;
}

static int cal_legacy_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int cal_legacy_s_input(struct file *file, void *priv, unsigned int i)
{
	return i > 0 ? -EINVAL : 0;
}

/* timeperframe is arbitrary and continuous */
static int cal_legacy_enum_frameintervals(struct file *file, void *priv,
					  struct v4l2_frmivalenum *fival)
{
	struct cal_ctx *ctx = video_drvdata(file);
	const struct cal_format_info *fmtinfo;
	struct v4l2_subdev_frame_interval_enum fie = {
		.index = fival->index,
		.width = fival->width,
		.height = fival->height,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	fmtinfo = find_format_by_pix(ctx, fival->pixel_format);
	if (!fmtinfo)
		return -EINVAL;

	fie.code = fmtinfo->code;
	ret = v4l2_subdev_call(ctx->phy->source, pad, enum_frame_interval,
			       NULL, &fie);
	if (ret)
		return ret;
	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete = fie.interval;

	return 0;
}

static int cal_legacy_g_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct cal_ctx *ctx = video_drvdata(file);

	return v4l2_g_parm_cap(video_devdata(file), ctx->phy->source, a);
}

static int cal_legacy_s_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct cal_ctx *ctx = video_drvdata(file);

	return v4l2_s_parm_cap(video_devdata(file), ctx->phy->source, a);
}

static const struct v4l2_ioctl_ops cal_ioctl_legacy_ops = {
	.vidioc_querycap      = cal_querycap,
	.vidioc_enum_fmt_vid_cap  = cal_legacy_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap     = cal_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = cal_legacy_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap     = cal_legacy_s_fmt_vid_cap,
	.vidioc_enum_framesizes   = cal_legacy_enum_framesizes,
	.vidioc_reqbufs       = vb2_ioctl_reqbufs,
	.vidioc_create_bufs   = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf   = vb2_ioctl_prepare_buf,
	.vidioc_querybuf      = vb2_ioctl_querybuf,
	.vidioc_qbuf          = vb2_ioctl_qbuf,
	.vidioc_dqbuf         = vb2_ioctl_dqbuf,
	.vidioc_expbuf        = vb2_ioctl_expbuf,
	.vidioc_enum_input    = cal_legacy_enum_input,
	.vidioc_g_input       = cal_legacy_g_input,
	.vidioc_s_input       = cal_legacy_s_input,
	.vidioc_enum_frameintervals = cal_legacy_enum_frameintervals,
	.vidioc_streamon      = vb2_ioctl_streamon,
	.vidioc_streamoff     = vb2_ioctl_streamoff,
	.vidioc_log_status    = v4l2_ctrl_log_status,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
	.vidioc_g_parm		= cal_legacy_g_parm,
	.vidioc_s_parm		= cal_legacy_s_parm,
};

/* ------------------------------------------------------------------
 *	V4L2 Media Controller Centric IOCTLs
 * ------------------------------------------------------------------
 */

static int cal_mc_enum_fmt_vid_cap(struct file *file, void  *priv,
				   struct v4l2_fmtdesc *f)
{
	unsigned int i;
	unsigned int idx;

	if (f->index >= cal_num_formats)
		return -EINVAL;

	idx = 0;

	for (i = 0; i < cal_num_formats; ++i) {
		if (f->mbus_code && cal_formats[i].code != f->mbus_code)
			continue;

		if (idx == f->index) {
			f->pixelformat = cal_formats[i].fourcc;
			f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			return 0;
		}

		idx++;
	}

	return -EINVAL;
}

static void cal_mc_try_fmt(struct cal_ctx *ctx, struct v4l2_format *f,
			   const struct cal_format_info **info)
{
	struct v4l2_pix_format *format = &f->fmt.pix;
	const struct cal_format_info *fmtinfo;
	unsigned int bpp;

	/*
	 * Default to the first format if the requested pixel format code isn't
	 * supported.
	 */
	fmtinfo = cal_format_by_fourcc(f->fmt.pix.pixelformat);
	if (!fmtinfo)
		fmtinfo = &cal_formats[0];

	/*
	 * Clamp the size, update the pixel format. The field and colorspace are
	 * accepted as-is, except for V4L2_FIELD_ANY that is turned into
	 * V4L2_FIELD_NONE.
	 */
	bpp = ALIGN(fmtinfo->bpp, 8);

	format->width = clamp_t(unsigned int, format->width,
				CAL_MIN_WIDTH_BYTES * 8 / bpp,
				CAL_MAX_WIDTH_BYTES * 8 / bpp);
	format->height = clamp_t(unsigned int, format->height,
				 CAL_MIN_HEIGHT_LINES, CAL_MAX_HEIGHT_LINES);
	format->pixelformat = fmtinfo->fourcc;

	if (format->field == V4L2_FIELD_ANY)
		format->field = V4L2_FIELD_NONE;

	/*
	 * Calculate the number of bytes per line and the image size. The
	 * hardware stores the stride as a number of 16 bytes words, in a
	 * signed 15-bit value. Only 14 bits are thus usable.
	 */
	format->bytesperline = ALIGN(clamp(format->bytesperline,
					   format->width * bpp / 8,
					   ((1U << 14) - 1) * 16), 16);

	format->sizeimage = format->height * format->bytesperline;

	format->colorspace = ctx->v_fmt.fmt.pix.colorspace;

	if (info)
		*info = fmtinfo;

	ctx_dbg(3, ctx, "%s: %s %ux%u (bytesperline %u sizeimage %u)\n",
		__func__, fourcc_to_str(format->pixelformat),
		format->width, format->height,
		format->bytesperline, format->sizeimage);
}

static int cal_mc_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct cal_ctx *ctx = video_drvdata(file);

	cal_mc_try_fmt(ctx, f, NULL);
	return 0;
}

static int cal_mc_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct cal_ctx *ctx = video_drvdata(file);
	const struct cal_format_info *fmtinfo;

	if (vb2_is_busy(&ctx->vb_vidq)) {
		ctx_dbg(3, ctx, "%s device busy\n", __func__);
		return -EBUSY;
	}

	cal_mc_try_fmt(ctx, f, &fmtinfo);

	ctx->v_fmt = *f;
	ctx->fmtinfo = fmtinfo;

	return 0;
}

static int cal_mc_enum_framesizes(struct file *file, void *fh,
				  struct v4l2_frmsizeenum *fsize)
{
	struct cal_ctx *ctx = video_drvdata(file);
	const struct cal_format_info *fmtinfo;
	unsigned int bpp;

	if (fsize->index > 0)
		return -EINVAL;

	fmtinfo = cal_format_by_fourcc(fsize->pixel_format);
	if (!fmtinfo) {
		ctx_dbg(3, ctx, "Invalid pixel format 0x%08x\n",
			fsize->pixel_format);
		return -EINVAL;
	}

	bpp = ALIGN(fmtinfo->bpp, 8);

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise.min_width = CAL_MIN_WIDTH_BYTES * 8 / bpp;
	fsize->stepwise.max_width = CAL_MAX_WIDTH_BYTES * 8 / bpp;
	fsize->stepwise.step_width = 64 / bpp;
	fsize->stepwise.min_height = CAL_MIN_HEIGHT_LINES;
	fsize->stepwise.max_height = CAL_MAX_HEIGHT_LINES;
	fsize->stepwise.step_height = 1;

	return 0;
}

static const struct v4l2_ioctl_ops cal_ioctl_mc_ops = {
	.vidioc_querycap      = cal_querycap,
	.vidioc_enum_fmt_vid_cap  = cal_mc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap     = cal_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = cal_mc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap     = cal_mc_s_fmt_vid_cap,
	.vidioc_enum_framesizes   = cal_mc_enum_framesizes,
	.vidioc_reqbufs       = vb2_ioctl_reqbufs,
	.vidioc_create_bufs   = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf   = vb2_ioctl_prepare_buf,
	.vidioc_querybuf      = vb2_ioctl_querybuf,
	.vidioc_qbuf          = vb2_ioctl_qbuf,
	.vidioc_dqbuf         = vb2_ioctl_dqbuf,
	.vidioc_expbuf        = vb2_ioctl_expbuf,
	.vidioc_streamon      = vb2_ioctl_streamon,
	.vidioc_streamoff     = vb2_ioctl_streamoff,
	.vidioc_log_status    = v4l2_ctrl_log_status,
};

/* ------------------------------------------------------------------
 *	videobuf2 Common Operations
 * ------------------------------------------------------------------
 */

static int cal_queue_setup(struct vb2_queue *vq,
			   unsigned int *nbuffers, unsigned int *nplanes,
			   unsigned int sizes[], struct device *alloc_devs[])
{
	struct cal_ctx *ctx = vb2_get_drv_priv(vq);
	unsigned int size = ctx->v_fmt.fmt.pix.sizeimage;

	if (vq->num_buffers + *nbuffers < 3)
		*nbuffers = 3 - vq->num_buffers;

	if (*nplanes) {
		if (sizes[0] < size)
			return -EINVAL;
		size = sizes[0];
	}

	*nplanes = 1;
	sizes[0] = size;

	ctx_dbg(3, ctx, "nbuffers=%d, size=%d\n", *nbuffers, sizes[0]);

	return 0;
}

static int cal_buffer_prepare(struct vb2_buffer *vb)
{
	struct cal_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct cal_buffer *buf = container_of(vb, struct cal_buffer,
					      vb.vb2_buf);
	unsigned long size;

	size = ctx->v_fmt.fmt.pix.sizeimage;
	if (vb2_plane_size(vb, 0) < size) {
		ctx_err(ctx,
			"data will not fit into plane (%lu < %lu)\n",
			vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(&buf->vb.vb2_buf, 0, size);
	return 0;
}

static void cal_buffer_queue(struct vb2_buffer *vb)
{
	struct cal_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct cal_buffer *buf = container_of(vb, struct cal_buffer,
					      vb.vb2_buf);
	unsigned long flags;

	/* recheck locking */
	spin_lock_irqsave(&ctx->dma.lock, flags);
	list_add_tail(&buf->list, &ctx->dma.queue);
	spin_unlock_irqrestore(&ctx->dma.lock, flags);
}

static void cal_release_buffers(struct cal_ctx *ctx,
				enum vb2_buffer_state state)
{
	struct cal_buffer *buf, *tmp;

	/* Release all queued buffers. */
	spin_lock_irq(&ctx->dma.lock);

	list_for_each_entry_safe(buf, tmp, &ctx->dma.queue, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}

	if (ctx->dma.pending) {
		vb2_buffer_done(&ctx->dma.pending->vb.vb2_buf, state);
		ctx->dma.pending = NULL;
	}

	if (ctx->dma.active) {
		vb2_buffer_done(&ctx->dma.active->vb.vb2_buf, state);
		ctx->dma.active = NULL;
	}

	spin_unlock_irq(&ctx->dma.lock);
}

/* ------------------------------------------------------------------
 *	videobuf2 Operations
 * ------------------------------------------------------------------
 */

static int cal_video_check_format(struct cal_ctx *ctx)
{
	const struct v4l2_mbus_framefmt *format;
	struct media_pad *remote_pad;

	remote_pad = media_pad_remote_pad_first(&ctx->pad);
	if (!remote_pad)
		return -ENODEV;

	format = &ctx->phy->formats[remote_pad->index];

	if (ctx->fmtinfo->code != format->code ||
	    ctx->v_fmt.fmt.pix.height != format->height ||
	    ctx->v_fmt.fmt.pix.width != format->width ||
	    ctx->v_fmt.fmt.pix.field != format->field)
		return -EPIPE;

	return 0;
}

static int cal_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct cal_ctx *ctx = vb2_get_drv_priv(vq);
	struct cal_buffer *buf;
	dma_addr_t addr;
	int ret;

	ret = video_device_pipeline_alloc_start(&ctx->vdev);
	if (ret < 0) {
		ctx_err(ctx, "Failed to start media pipeline: %d\n", ret);
		goto error_release_buffers;
	}

	/*
	 * Verify that the currently configured format matches the output of
	 * the connected CAMERARX.
	 */
	ret = cal_video_check_format(ctx);
	if (ret < 0) {
		ctx_dbg(3, ctx,
			"Format mismatch between CAMERARX and video node\n");
		goto error_pipeline;
	}

	ret = cal_ctx_prepare(ctx);
	if (ret) {
		ctx_err(ctx, "Failed to prepare context: %d\n", ret);
		goto error_pipeline;
	}

	spin_lock_irq(&ctx->dma.lock);
	buf = list_first_entry(&ctx->dma.queue, struct cal_buffer, list);
	ctx->dma.active = buf;
	list_del(&buf->list);
	spin_unlock_irq(&ctx->dma.lock);

	addr = vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 0);

	ret = pm_runtime_resume_and_get(ctx->cal->dev);
	if (ret < 0)
		goto error_pipeline;

	cal_ctx_set_dma_addr(ctx, addr);
	cal_ctx_start(ctx);

	ret = v4l2_subdev_call(&ctx->phy->subdev, video, s_stream, 1);
	if (ret)
		goto error_stop;

	if (cal_debug >= 4)
		cal_quickdump_regs(ctx->cal);

	return 0;

error_stop:
	cal_ctx_stop(ctx);
	pm_runtime_put_sync(ctx->cal->dev);
	cal_ctx_unprepare(ctx);

error_pipeline:
	video_device_pipeline_stop(&ctx->vdev);
error_release_buffers:
	cal_release_buffers(ctx, VB2_BUF_STATE_QUEUED);

	return ret;
}

static void cal_stop_streaming(struct vb2_queue *vq)
{
	struct cal_ctx *ctx = vb2_get_drv_priv(vq);

	cal_ctx_stop(ctx);

	v4l2_subdev_call(&ctx->phy->subdev, video, s_stream, 0);

	pm_runtime_put_sync(ctx->cal->dev);

	cal_ctx_unprepare(ctx);

	cal_release_buffers(ctx, VB2_BUF_STATE_ERROR);

	video_device_pipeline_stop(&ctx->vdev);
}

static const struct vb2_ops cal_video_qops = {
	.queue_setup		= cal_queue_setup,
	.buf_prepare		= cal_buffer_prepare,
	.buf_queue		= cal_buffer_queue,
	.start_streaming	= cal_start_streaming,
	.stop_streaming		= cal_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

/* ------------------------------------------------------------------
 *	V4L2 Initialization and Registration
 * ------------------------------------------------------------------
 */

static const struct v4l2_file_operations cal_fops = {
	.owner		= THIS_MODULE,
	.open           = v4l2_fh_open,
	.release        = vb2_fop_release,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2, /* V4L2 ioctl handler */
	.mmap           = vb2_fop_mmap,
};

static int cal_ctx_v4l2_init_formats(struct cal_ctx *ctx)
{
	struct v4l2_mbus_framefmt mbus_fmt;
	const struct cal_format_info *fmtinfo;
	unsigned int i, j, k;
	int ret = 0;

	/* Enumerate sub device formats and enable all matching local formats */
	ctx->active_fmt = devm_kcalloc(ctx->cal->dev, cal_num_formats,
				       sizeof(*ctx->active_fmt), GFP_KERNEL);
	if (!ctx->active_fmt)
		return -ENOMEM;

	ctx->num_active_fmt = 0;

	for (j = 0, i = 0; ; ++j) {
		struct v4l2_subdev_mbus_code_enum mbus_code = {
			.index = j,
			.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		};

		ret = v4l2_subdev_call(ctx->phy->source, pad, enum_mbus_code,
				       NULL, &mbus_code);
		if (ret == -EINVAL)
			break;

		if (ret) {
			ctx_err(ctx, "Error enumerating mbus codes in subdev %s: %d\n",
				ctx->phy->source->name, ret);
			return ret;
		}

		ctx_dbg(2, ctx,
			"subdev %s: code: %04x idx: %u\n",
			ctx->phy->source->name, mbus_code.code, j);

		for (k = 0; k < cal_num_formats; k++) {
			fmtinfo = &cal_formats[k];

			if (mbus_code.code == fmtinfo->code) {
				ctx->active_fmt[i] = fmtinfo;
				ctx_dbg(2, ctx,
					"matched fourcc: %s: code: %04x idx: %u\n",
					fourcc_to_str(fmtinfo->fourcc),
					fmtinfo->code, i);
				ctx->num_active_fmt = ++i;
			}
		}
	}

	if (i == 0) {
		ctx_err(ctx, "No suitable format reported by subdev %s\n",
			ctx->phy->source->name);
		return -EINVAL;
	}

	ret = __subdev_get_format(ctx, &mbus_fmt);
	if (ret)
		return ret;

	fmtinfo = find_format_by_code(ctx, mbus_fmt.code);
	if (!fmtinfo) {
		ctx_dbg(3, ctx, "mbus code format (0x%08x) not found.\n",
			mbus_fmt.code);
		return -EINVAL;
	}

	/* Save current format */
	v4l2_fill_pix_format(&ctx->v_fmt.fmt.pix, &mbus_fmt);
	ctx->v_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ctx->v_fmt.fmt.pix.pixelformat = fmtinfo->fourcc;
	cal_calc_format_size(ctx, fmtinfo, &ctx->v_fmt);
	ctx->fmtinfo = fmtinfo;

	return 0;
}

static int cal_ctx_v4l2_init_mc_format(struct cal_ctx *ctx)
{
	const struct cal_format_info *fmtinfo;
	struct v4l2_pix_format *pix_fmt = &ctx->v_fmt.fmt.pix;

	fmtinfo = cal_format_by_code(MEDIA_BUS_FMT_UYVY8_2X8);
	if (!fmtinfo)
		return -EINVAL;

	pix_fmt->width = 640;
	pix_fmt->height = 480;
	pix_fmt->field = V4L2_FIELD_NONE;
	pix_fmt->colorspace = V4L2_COLORSPACE_SRGB;
	pix_fmt->ycbcr_enc = V4L2_YCBCR_ENC_601;
	pix_fmt->quantization = V4L2_QUANTIZATION_LIM_RANGE;
	pix_fmt->xfer_func = V4L2_XFER_FUNC_SRGB;
	pix_fmt->pixelformat = fmtinfo->fourcc;

	ctx->v_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	/* Save current format */
	cal_calc_format_size(ctx, fmtinfo, &ctx->v_fmt);
	ctx->fmtinfo = fmtinfo;

	return 0;
}

int cal_ctx_v4l2_register(struct cal_ctx *ctx)
{
	struct video_device *vfd = &ctx->vdev;
	int ret;

	if (!cal_mc_api) {
		struct v4l2_ctrl_handler *hdl = &ctx->ctrl_handler;

		ret = cal_ctx_v4l2_init_formats(ctx);
		if (ret) {
			ctx_err(ctx, "Failed to init formats: %d\n", ret);
			return ret;
		}

		ret = v4l2_ctrl_add_handler(hdl, ctx->phy->source->ctrl_handler,
					    NULL, true);
		if (ret < 0) {
			ctx_err(ctx, "Failed to add source ctrl handler\n");
			return ret;
		}
	} else {
		ret = cal_ctx_v4l2_init_mc_format(ctx);
		if (ret) {
			ctx_err(ctx, "Failed to init format: %d\n", ret);
			return ret;
		}
	}

	ret = video_register_device(vfd, VFL_TYPE_VIDEO, cal_video_nr);
	if (ret < 0) {
		ctx_err(ctx, "Failed to register video device\n");
		return ret;
	}

	ret = media_create_pad_link(&ctx->phy->subdev.entity,
				    CAL_CAMERARX_PAD_FIRST_SOURCE,
				    &vfd->entity, 0,
				    MEDIA_LNK_FL_IMMUTABLE |
				    MEDIA_LNK_FL_ENABLED);
	if (ret) {
		ctx_err(ctx, "Failed to create media link for context %u\n",
			ctx->dma_ctx);
		video_unregister_device(vfd);
		return ret;
	}

	ctx_info(ctx, "V4L2 device registered as %s\n",
		 video_device_node_name(vfd));

	return 0;
}

void cal_ctx_v4l2_unregister(struct cal_ctx *ctx)
{
	ctx_dbg(1, ctx, "unregistering %s\n",
		video_device_node_name(&ctx->vdev));

	video_unregister_device(&ctx->vdev);
}

int cal_ctx_v4l2_init(struct cal_ctx *ctx)
{
	struct video_device *vfd = &ctx->vdev;
	struct vb2_queue *q = &ctx->vb_vidq;
	int ret;

	INIT_LIST_HEAD(&ctx->dma.queue);
	spin_lock_init(&ctx->dma.lock);
	mutex_init(&ctx->mutex);
	init_waitqueue_head(&ctx->dma.wait);

	/* Initialize the vb2 queue. */
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->drv_priv = ctx;
	q->buf_struct_size = sizeof(struct cal_buffer);
	q->ops = &cal_video_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &ctx->mutex;
	q->min_buffers_needed = 3;
	q->dev = ctx->cal->dev;

	ret = vb2_queue_init(q);
	if (ret)
		return ret;

	/* Initialize the video device and media entity. */
	vfd->fops = &cal_fops;
	vfd->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING
			 | (cal_mc_api ? V4L2_CAP_IO_MC : 0);
	vfd->v4l2_dev = &ctx->cal->v4l2_dev;
	vfd->queue = q;
	snprintf(vfd->name, sizeof(vfd->name), "CAL output %u", ctx->dma_ctx);
	vfd->release = video_device_release_empty;
	vfd->ioctl_ops = cal_mc_api ? &cal_ioctl_mc_ops : &cal_ioctl_legacy_ops;
	vfd->lock = &ctx->mutex;
	video_set_drvdata(vfd, ctx);

	ctx->pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vfd->entity, 1, &ctx->pad);
	if (ret < 0)
		return ret;

	if (!cal_mc_api) {
		/* Initialize the control handler. */
		struct v4l2_ctrl_handler *hdl = &ctx->ctrl_handler;

		ret = v4l2_ctrl_handler_init(hdl, 11);
		if (ret < 0) {
			ctx_err(ctx, "Failed to init ctrl handler\n");
			goto error;
		}

		vfd->ctrl_handler = hdl;
	}

	return 0;

error:
	media_entity_cleanup(&vfd->entity);
	return ret;
}

void cal_ctx_v4l2_cleanup(struct cal_ctx *ctx)
{
	if (!cal_mc_api)
		v4l2_ctrl_handler_free(&ctx->ctrl_handler);

	media_entity_cleanup(&ctx->vdev.entity);
}
