// SPDX-License-Identifier: GPL-2.0
/*
 * Imagination E5010 JPEG Encoder driver.
 *
 * TODO: Add MMU and memory tiling support
 *
 * Copyright (C) 2023 Texas Instruments Incorporated - https://www.ti.com/
 *
 * Author: David Huang <d-huang@ti.com>
 * Author: Devarsh Thakkar <devarsht@ti.com>
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <media/jpeg.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-jpeg.h>
#include <media/v4l2-rect.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-v4l2.h>
#include "e5010-jpeg-enc.h"
#include "e5010-jpeg-enc-hw.h"

/* forward declarations */
static const struct of_device_id e5010_of_match[];

static const struct v4l2_file_operations e5010_fops;

static const struct v4l2_ioctl_ops e5010_ioctl_ops;

static const struct vb2_ops e5010_video_ops;

static const struct v4l2_m2m_ops e5010_m2m_ops;

static struct e5010_fmt e5010_formats[] = {
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.subsampling = V4L2_JPEG_CHROMA_SUBSAMPLING_420,
		.chroma_order = CHROMA_ORDER_CB_CR,
		.frmsize = { MIN_DIMENSION, MAX_DIMENSION, 64,
			     MIN_DIMENSION, MAX_DIMENSION, 8 },
	},
	{
		.fourcc = V4L2_PIX_FMT_NV12M,
		.num_planes = 2,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.subsampling = V4L2_JPEG_CHROMA_SUBSAMPLING_420,
		.chroma_order = CHROMA_ORDER_CB_CR,
		.frmsize = { MIN_DIMENSION, MAX_DIMENSION, 64,
			     MIN_DIMENSION, MAX_DIMENSION, 8 },
	},
	{
		.fourcc = V4L2_PIX_FMT_NV21,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.subsampling = V4L2_JPEG_CHROMA_SUBSAMPLING_420,
		.chroma_order = CHROMA_ORDER_CR_CB,
		.frmsize = { MIN_DIMENSION, MAX_DIMENSION, 64,
			     MIN_DIMENSION, MAX_DIMENSION, 8 },
	},
	{
		.fourcc = V4L2_PIX_FMT_NV21M,
		.num_planes = 2,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.subsampling = V4L2_JPEG_CHROMA_SUBSAMPLING_420,
		.chroma_order = CHROMA_ORDER_CR_CB,
		.frmsize = { MIN_DIMENSION, MAX_DIMENSION, 64,
			     MIN_DIMENSION, MAX_DIMENSION, 8 },
	},
	{
		.fourcc = V4L2_PIX_FMT_NV16,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.subsampling = V4L2_JPEG_CHROMA_SUBSAMPLING_422,
		.chroma_order = CHROMA_ORDER_CB_CR,
		.frmsize = { MIN_DIMENSION, MAX_DIMENSION, 64,
			     MIN_DIMENSION, MAX_DIMENSION, 8 },
	},
	{
		.fourcc = V4L2_PIX_FMT_NV16M,
		.num_planes = 2,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.subsampling = V4L2_JPEG_CHROMA_SUBSAMPLING_422,
		.chroma_order = CHROMA_ORDER_CB_CR,
		.frmsize = { MIN_DIMENSION, MAX_DIMENSION, 64,
			     MIN_DIMENSION, MAX_DIMENSION, 8 },

	},
	{
		.fourcc = V4L2_PIX_FMT_NV61,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.subsampling = V4L2_JPEG_CHROMA_SUBSAMPLING_422,
		.chroma_order = CHROMA_ORDER_CR_CB,
		.frmsize = { MIN_DIMENSION, MAX_DIMENSION, 64,
			     MIN_DIMENSION, MAX_DIMENSION, 8 },
	},
	{
		.fourcc = V4L2_PIX_FMT_NV61M,
		.num_planes = 2,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.subsampling = V4L2_JPEG_CHROMA_SUBSAMPLING_422,
		.chroma_order = CHROMA_ORDER_CR_CB,
		.frmsize = { MIN_DIMENSION, MAX_DIMENSION, 64,
			     MIN_DIMENSION, MAX_DIMENSION, 8 },
	},
	{
		.fourcc = V4L2_PIX_FMT_JPEG,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.subsampling = 0,
		.chroma_order = 0,
		.frmsize = { MIN_DIMENSION, MAX_DIMENSION, 16,
			     MIN_DIMENSION, MAX_DIMENSION, 8 },
	},
};

static unsigned int debug;
module_param(debug, uint, 0644);
MODULE_PARM_DESC(debug, "debug level");

#define dprintk(dev, lvl, fmt, arg...) \
	v4l2_dbg(lvl, debug, &(dev)->v4l2_dev, "%s: " fmt, __func__, ## arg)

static const struct v4l2_event e5010_eos_event = {
	.type = V4L2_EVENT_EOS
};

static const char *type_name(enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		return "Output";
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		return "Capture";
	default:
		return "Invalid";
	}
}

static struct e5010_q_data *get_queue(struct e5010_context *ctx, enum v4l2_buf_type type)
{
	return (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ? &ctx->out_queue : &ctx->cap_queue;
}

static void calculate_qp_tables(struct e5010_context *ctx)
{
	long long luminosity, contrast;
	int quality, i;

	quality = 50 - ctx->quality;

	luminosity = LUMINOSITY * quality / 50;
	contrast = CONTRAST * quality / 50;

	if (quality > 0) {
		luminosity *= INCREASE;
		contrast *= INCREASE;
	}

	for (i = 0; i < V4L2_JPEG_PIXELS_IN_BLOCK; i++) {
		long long delta = v4l2_jpeg_ref_table_chroma_qt[i] * contrast + luminosity;
		int val = (int)(v4l2_jpeg_ref_table_chroma_qt[i] + delta);

		clamp(val, 1, 255);
		ctx->chroma_qp[i] = quality == -50 ? 1 : val;

		delta = v4l2_jpeg_ref_table_luma_qt[i] * contrast + luminosity;
		val = (int)(v4l2_jpeg_ref_table_luma_qt[i] + delta);
		clamp(val, 1, 255);
		ctx->luma_qp[i] = quality == -50 ? 1 : val;
	}

	ctx->update_qp = true;
}

static int update_qp_tables(struct e5010_context *ctx)
{
	struct e5010_dev *e5010 = ctx->e5010;
	int i, ret = 0;
	u32 lvalue, cvalue;

	lvalue = 0;
	cvalue = 0;

	for (i = 0; i < QP_TABLE_SIZE; i++) {
		lvalue |= ctx->luma_qp[i] << (8 * (i % 4));
		cvalue |= ctx->chroma_qp[i] << (8 * (i % 4));
		if (i % 4 == 3) {
			ret |= e5010_hw_set_qpvalue(e5010->core_base,
							JASPER_LUMA_QUANTIZATION_TABLE0_OFFSET
							+ QP_TABLE_FIELD_OFFSET * ((i - 3) / 4),
							lvalue);
			ret |= e5010_hw_set_qpvalue(e5010->core_base,
							JASPER_CHROMA_QUANTIZATION_TABLE0_OFFSET
							+ QP_TABLE_FIELD_OFFSET * ((i - 3) / 4),
							cvalue);
			lvalue = 0;
			cvalue = 0;
		}
	}

	return ret;
}

static int e5010_set_input_subsampling(void __iomem *core_base, int subsampling)
{
	switch (subsampling) {
	case V4L2_JPEG_CHROMA_SUBSAMPLING_420:
		return e5010_hw_set_input_subsampling(core_base, SUBSAMPLING_420);
	case V4L2_JPEG_CHROMA_SUBSAMPLING_422:
		return e5010_hw_set_input_subsampling(core_base, SUBSAMPLING_422);
	default:
		return -EINVAL;
	};
}

static int e5010_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	strscpy(cap->driver, E5010_MODULE_NAME, sizeof(cap->driver));
	strscpy(cap->card, E5010_MODULE_NAME, sizeof(cap->card));

	return 0;
}

static struct e5010_fmt *find_format(struct v4l2_format *f)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(e5010_formats); ++i) {
		if (e5010_formats[i].fourcc == f->fmt.pix_mp.pixelformat &&
		    e5010_formats[i].type == f->type)
			return &e5010_formats[i];
	}

	return NULL;
}

static int e5010_enum_fmt(struct file *file, void *priv, struct v4l2_fmtdesc *f)
{
	int i, index = 0;
	struct e5010_fmt *fmt = NULL;
	struct e5010_context *ctx = file->private_data;

	if (!V4L2_TYPE_IS_MULTIPLANAR(f->type)) {
		v4l2_err(&ctx->e5010->v4l2_dev, "ENUMFMT with Invalid type: %d\n", f->type);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(e5010_formats); ++i) {
		if (e5010_formats[i].type == f->type) {
			if (index == f->index) {
				fmt = &e5010_formats[i];
				break;
			}
			index++;
		}
	}

	if (!fmt)
		return -EINVAL;

	f->pixelformat = fmt->fourcc;
	return 0;
}

static int e5010_g_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct e5010_context *ctx = file->private_data;
	struct e5010_q_data *queue;
	int i;
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct v4l2_plane_pix_format *plane_fmt = pix_mp->plane_fmt;

	queue = get_queue(ctx, f->type);

	pix_mp->flags = 0;
	pix_mp->field = V4L2_FIELD_NONE;
	pix_mp->pixelformat = queue->fmt->fourcc;
	pix_mp->width = queue->width_adjusted;
	pix_mp->height = queue->height_adjusted;
	pix_mp->num_planes = queue->fmt->num_planes;

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		if (!pix_mp->colorspace)
			pix_mp->colorspace = V4L2_COLORSPACE_SRGB;

		for (i = 0; i < queue->fmt->num_planes; i++) {
			plane_fmt[i].sizeimage = queue->sizeimage[i];
			plane_fmt[i].bytesperline = queue->bytesperline[i];
		}

	} else {
		pix_mp->colorspace = V4L2_COLORSPACE_JPEG;
		plane_fmt[0].bytesperline = 0;
		plane_fmt[0].sizeimage = queue->sizeimage[0];
	}
	pix_mp->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	pix_mp->xfer_func = V4L2_XFER_FUNC_DEFAULT;
	pix_mp->quantization = V4L2_QUANTIZATION_DEFAULT;

	return 0;
}

static int e5010_jpeg_try_fmt(struct v4l2_format *f, struct e5010_context *ctx)
{
	struct e5010_fmt *fmt;
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct v4l2_plane_pix_format *plane_fmt = pix_mp->plane_fmt;

	fmt = find_format(f);
	if (!fmt) {
		if (V4L2_TYPE_IS_OUTPUT(f->type))
			pix_mp->pixelformat = V4L2_PIX_FMT_NV12;
		else
			pix_mp->pixelformat = V4L2_PIX_FMT_JPEG;
		fmt = find_format(f);
		if (!fmt)
			return -EINVAL;
	}

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		if (!pix_mp->colorspace)
			pix_mp->colorspace = V4L2_COLORSPACE_JPEG;
		if (!pix_mp->ycbcr_enc)
			pix_mp->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
		if (!pix_mp->quantization)
			pix_mp->quantization = V4L2_QUANTIZATION_DEFAULT;
		if (!pix_mp->xfer_func)
			pix_mp->xfer_func = V4L2_XFER_FUNC_DEFAULT;

		v4l2_apply_frmsize_constraints(&pix_mp->width,
					       &pix_mp->height,
					       &fmt->frmsize);

		v4l2_fill_pixfmt_mp(pix_mp, pix_mp->pixelformat,
				    pix_mp->width, pix_mp->height);

	} else {
		pix_mp->colorspace = V4L2_COLORSPACE_JPEG;
		pix_mp->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
		pix_mp->quantization = V4L2_QUANTIZATION_DEFAULT;
		pix_mp->xfer_func = V4L2_XFER_FUNC_DEFAULT;
		v4l2_apply_frmsize_constraints(&pix_mp->width,
					       &pix_mp->height,
					       &fmt->frmsize);
		plane_fmt[0].sizeimage = pix_mp->width * pix_mp->height * JPEG_MAX_BYTES_PER_PIXEL;
		plane_fmt[0].sizeimage += HEADER_SIZE;
		plane_fmt[0].bytesperline = 0;
		pix_mp->pixelformat = fmt->fourcc;
		pix_mp->num_planes = fmt->num_planes;
	}
	pix_mp->flags = 0;
	pix_mp->field = V4L2_FIELD_NONE;

	dprintk(ctx->e5010, 2,
		"ctx: 0x%p: format type %s:, wxh: %dx%d (plane0 : %d bytes, plane1 : %d bytes),fmt: %c%c%c%c\n",
		ctx, type_name(f->type), pix_mp->width, pix_mp->height,
		plane_fmt[0].sizeimage, plane_fmt[1].sizeimage,
		(fmt->fourcc & 0xff),
		(fmt->fourcc >>  8) & 0xff,
		(fmt->fourcc >> 16) & 0xff,
		(fmt->fourcc >> 24) & 0xff);

	return 0;
}

static int e5010_try_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct e5010_context *ctx = file->private_data;

	return e5010_jpeg_try_fmt(f, ctx);
}

static int e5010_s_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct e5010_context *ctx = file->private_data;
	struct vb2_queue *vq;
	int ret = 0, i = 0;
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct v4l2_plane_pix_format *plane_fmt = pix_mp->plane_fmt;
	struct e5010_q_data *queue;
	struct e5010_fmt *fmt;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	if (vb2_is_busy(vq)) {
		v4l2_err(&ctx->e5010->v4l2_dev, "queue busy\n");
		return -EBUSY;
	}

	ret = e5010_jpeg_try_fmt(f, ctx);
	if (ret)
		return ret;

	fmt = find_format(f);
	queue = get_queue(ctx, f->type);

	queue->fmt = fmt;
	queue->width = pix_mp->width;
	queue->height = pix_mp->height;

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		for (i = 0; i < fmt->num_planes; i++) {
			queue->bytesperline[i] = plane_fmt[i].bytesperline;
			queue->sizeimage[i] = plane_fmt[i].sizeimage;
		}
		queue->crop.left = 0;
		queue->crop.top = 0;
		queue->crop.width = queue->width;
		queue->crop.height = queue->height;
	} else {
		queue->sizeimage[0] = plane_fmt[0].sizeimage;
		queue->sizeimage[1] = 0;
		queue->bytesperline[0] = 0;
		queue->bytesperline[1] = 0;
	}

	return 0;
}

static int e5010_enum_framesizes(struct file *file, void *priv, struct v4l2_frmsizeenum *fsize)
{
	struct v4l2_format f;
	struct e5010_fmt *fmt;

	if (fsize->index != 0)
		return -EINVAL;

	f.fmt.pix_mp.pixelformat = fsize->pixel_format;
	if (f.fmt.pix_mp.pixelformat ==  V4L2_PIX_FMT_JPEG)
		f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	else
		f.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

	fmt = find_format(&f);
	if (!fmt)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise = fmt->frmsize;
	fsize->reserved[0] = 0;
	fsize->reserved[1] = 0;

	return 0;
}

static int e5010_g_selection(struct file *file, void *fh, struct v4l2_selection *s)
{
	struct e5010_context *ctx = file->private_data;
	struct e5010_q_data *queue;

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	queue = get_queue(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);

	switch (s->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = queue->width;
		s->r.height = queue->height;
		break;
	case V4L2_SEL_TGT_CROP:
		memcpy(&s->r, &queue->crop, sizeof(s->r));
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int e5010_s_selection(struct file *file, void *fh, struct v4l2_selection *s)
{
	struct e5010_context *ctx = file->private_data;
	struct e5010_q_data *queue;
	struct vb2_queue *vq;
	struct v4l2_rect base_rect;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, s->type);
	if (!vq)
		return -EINVAL;

	if (vb2_is_streaming(vq))
		return -EBUSY;

	if (s->target != V4L2_SEL_TGT_CROP ||
	    s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	queue = get_queue(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	base_rect.top = 0;
	base_rect.left = 0;
	base_rect.width = queue->width;
	base_rect.height = queue->height;

	switch (s->flags) {
	case 0:
		s->r.width = round_down(s->r.width, queue->fmt->frmsize.step_width);
		s->r.height = round_down(s->r.height, queue->fmt->frmsize.step_height);
		s->r.left = round_down(s->r.left, queue->fmt->frmsize.step_width);
		s->r.top = round_down(s->r.top, 2);

		if (s->r.left + s->r.width > queue->width)
			s->r.width = round_down(s->r.width + s->r.left - queue->width,
						queue->fmt->frmsize.step_width);
		if (s->r.top + s->r.height > queue->height)
			s->r.top = round_down(s->r.top + s->r.height - queue->height, 2);
		break;
	case V4L2_SEL_FLAG_GE:
		s->r.width = round_up(s->r.width, queue->fmt->frmsize.step_width);
		s->r.height = round_up(s->r.height, queue->fmt->frmsize.step_height);
		s->r.left = round_up(s->r.left, queue->fmt->frmsize.step_width);
		s->r.top = round_up(s->r.top, 2);
		break;
	case V4L2_SEL_FLAG_LE:
		s->r.width = round_down(s->r.width, queue->fmt->frmsize.step_width);
		s->r.height = round_down(s->r.height, queue->fmt->frmsize.step_height);
		s->r.left = round_down(s->r.left, queue->fmt->frmsize.step_width);
		s->r.top = round_down(s->r.top, 2);
		break;
	case V4L2_SEL_FLAG_LE | V4L2_SEL_FLAG_GE:
		if (!IS_ALIGNED(s->r.width, queue->fmt->frmsize.step_width) ||
		    !IS_ALIGNED(s->r.height, queue->fmt->frmsize.step_height) ||
		    !IS_ALIGNED(s->r.left, queue->fmt->frmsize.step_width) ||
		    !IS_ALIGNED(s->r.top, 2))
			return -ERANGE;
		break;
	default:
		return -EINVAL;
	}

	if (!v4l2_rect_enclosed(&s->r, &base_rect))
		return -ERANGE;

	memcpy(&queue->crop, &s->r, sizeof(s->r));

	if (!v4l2_rect_equal(&s->r, &base_rect))
		queue->crop_set = true;

	dprintk(ctx->e5010, 2, "ctx: 0x%p: crop rectangle: w: %d, h : %d, l : %d, t : %d\n",
		ctx, queue->crop.width, queue->crop.height, queue->crop.left, queue->crop.top);

	return 0;
}

static int e5010_subscribe_event(struct v4l2_fh *fh, const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subscribe_event(fh, sub);
	default:
		return -EINVAL;
	}

	return 0;
}

static int queue_init(void *priv, struct vb2_queue *src_vq, struct vb2_queue *dst_vq)
{
	struct e5010_context *ctx = priv;
	struct e5010_dev *e5010 = ctx->e5010;
	int ret = 0;

	/* src_vq */
	memset(src_vq, 0, sizeof(*src_vq));
	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct e5010_buffer);
	src_vq->ops = &e5010_video_ops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &e5010->mutex;
	src_vq->dev = e5010->v4l2_dev.dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	/* dst_vq */
	memset(dst_vq, 0, sizeof(*dst_vq));
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct e5010_buffer);
	dst_vq->ops = &e5010_video_ops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &e5010->mutex;
	dst_vq->dev = e5010->v4l2_dev.dev;

	ret = vb2_queue_init(dst_vq);
	if (ret) {
		vb2_queue_release(src_vq);
		return ret;
	}

	return 0;
}

static int e5010_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct e5010_context *ctx =
		container_of(ctrl->handler, struct e5010_context, ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_JPEG_COMPRESSION_QUALITY:
		ctx->quality = ctrl->val;
		calculate_qp_tables(ctx);
		dprintk(ctx->e5010, 2, "ctx: 0x%p compression quality set to : %d\n", ctx,
			ctx->quality);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops e5010_ctrl_ops = {
	.s_ctrl = e5010_s_ctrl,
};

static void e5010_encode_ctrls(struct e5010_context *ctx)
{
	v4l2_ctrl_new_std(&ctx->ctrl_handler, &e5010_ctrl_ops,
			  V4L2_CID_JPEG_COMPRESSION_QUALITY, 1, 100, 1, 75);
}

static int e5010_ctrls_setup(struct e5010_context *ctx)
{
	int err;

	v4l2_ctrl_handler_init(&ctx->ctrl_handler, 1);

	e5010_encode_ctrls(ctx);

	if (ctx->ctrl_handler.error) {
		err = ctx->ctrl_handler.error;
		v4l2_ctrl_handler_free(&ctx->ctrl_handler);

		return err;
	}

	err = v4l2_ctrl_handler_setup(&ctx->ctrl_handler);
	if (err)
		v4l2_ctrl_handler_free(&ctx->ctrl_handler);

	return err;
}

static void e5010_jpeg_set_default_params(struct e5010_context *ctx)
{
	struct e5010_q_data *queue;
	struct v4l2_format f;
	struct e5010_fmt *fmt;
	struct v4l2_pix_format_mplane *pix_mp = &f.fmt.pix_mp;
	struct v4l2_plane_pix_format *plane_fmt = pix_mp->plane_fmt;
	int i = 0;

	f.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	f.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
	fmt = find_format(&f);
	queue = &ctx->out_queue;
	queue->fmt = fmt;
	queue->width = DEFAULT_WIDTH;
	queue->height = DEFAULT_HEIGHT;
	pix_mp->width = queue->width;
	pix_mp->height = queue->height;
	queue->crop.left = 0;
	queue->crop.top = 0;
	queue->crop.width = queue->width;
	queue->crop.height = queue->height;
	v4l2_apply_frmsize_constraints(&pix_mp->width,
				       &pix_mp->height,
				       &fmt->frmsize);
	v4l2_fill_pixfmt_mp(pix_mp, pix_mp->pixelformat,
			    pix_mp->width, pix_mp->height);
	for (i = 0; i < fmt->num_planes; i++) {
		queue->bytesperline[i] = plane_fmt[i].bytesperline;
		queue->sizeimage[i] = plane_fmt[i].sizeimage;
	}
	queue->width_adjusted = pix_mp->width;
	queue->height_adjusted = pix_mp->height;

	f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	f.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_JPEG;
	fmt = find_format(&f);
	queue = &ctx->cap_queue;
	queue->fmt = fmt;
	queue->width = DEFAULT_WIDTH;
	queue->height = DEFAULT_HEIGHT;
	pix_mp->width = queue->width;
	pix_mp->height = queue->height;
	v4l2_apply_frmsize_constraints(&pix_mp->width,
				       &pix_mp->height,
				       &fmt->frmsize);
	queue->sizeimage[0] = pix_mp->width * pix_mp->height * JPEG_MAX_BYTES_PER_PIXEL;
	queue->sizeimage[0] += HEADER_SIZE;
	queue->sizeimage[1] = 0;
	queue->bytesperline[0] = 0;
	queue->bytesperline[1] = 0;
	queue->width_adjusted = pix_mp->width;
	queue->height_adjusted = pix_mp->height;
}

static int e5010_open(struct file *file)
{
	struct e5010_dev *e5010 = video_drvdata(file);
	struct video_device *vdev = video_devdata(file);
	struct e5010_context *ctx;
	int ret = 0;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (mutex_lock_interruptible(&e5010->mutex)) {
		ret = -ERESTARTSYS;
		goto free;
	}

	v4l2_fh_init(&ctx->fh, vdev);
	file->private_data = ctx;
	v4l2_fh_add(&ctx->fh);

	ctx->e5010 = e5010;
	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(e5010->m2m_dev, ctx, queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		v4l2_err(&e5010->v4l2_dev, "failed to init m2m ctx\n");
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto exit;
	}

	ret = e5010_ctrls_setup(ctx);
	if (ret) {
		v4l2_err(&e5010->v4l2_dev, "failed to setup e5010 jpeg controls\n");
		goto err_ctrls_setup;
	}
	ctx->fh.ctrl_handler = &ctx->ctrl_handler;

	e5010_jpeg_set_default_params(ctx);

	dprintk(e5010, 1, "Created instance: 0x%p, m2m_ctx: 0x%p\n", ctx, ctx->fh.m2m_ctx);

	mutex_unlock(&e5010->mutex);
	return 0;

err_ctrls_setup:
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
exit:
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	mutex_unlock(&e5010->mutex);
free:
	kfree(ctx);
	return ret;
}

static int e5010_release(struct file *file)
{
	struct e5010_dev *e5010 = video_drvdata(file);
	struct e5010_context *ctx = file->private_data;

	dprintk(e5010, 1, "Releasing instance: 0x%p, m2m_ctx: 0x%p\n", ctx, ctx->fh.m2m_ctx);
	mutex_lock(&e5010->mutex);
	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
	mutex_unlock(&e5010->mutex);

	return 0;
}

static void header_write(struct e5010_context *ctx, u8 *addr, unsigned int *offset,
			 unsigned int no_bytes, unsigned long bits)
{
	u8 *w_addr = addr + *offset;
	int i;

	if ((*offset + no_bytes) > HEADER_SIZE) {
		v4l2_warn(&ctx->e5010->v4l2_dev, "%s: %s: %d: Problem writing header. %d > HEADER_SIZE %d\n",
			  __FILE__, __func__, __LINE__, *offset + no_bytes, HEADER_SIZE);
		return;
	}

	for (i = no_bytes - 1; i >= 0; i--)
		*(w_addr++) = ((u8 *)&bits)[i];

	*offset += no_bytes;
}

static void encode_marker_segment(struct e5010_context *ctx, void *addr, unsigned int *offset)
{
	u8 *buffer = (u8 *)addr;
	int i;

	header_write(ctx, buffer, offset, 2, START_OF_IMAGE);
	header_write(ctx, buffer, offset, 2, DQT_MARKER);
	header_write(ctx, buffer, offset, 3, LQPQ << 4);
	for (i = 0; i < V4L2_JPEG_PIXELS_IN_BLOCK; i++)
		header_write(ctx, buffer, offset, 1, ctx->luma_qp[v4l2_jpeg_zigzag_scan_index[i]]);

	header_write(ctx, buffer, offset, 2, DQT_MARKER);
	header_write(ctx, buffer, offset, 3, (LQPQ << 4) | 1);
	for (i = 0; i < V4L2_JPEG_PIXELS_IN_BLOCK; i++)
		header_write(ctx, buffer, offset, 1,
			     ctx->chroma_qp[v4l2_jpeg_zigzag_scan_index[i]]);

	/* Huffman tables */
	header_write(ctx, buffer, offset, 2, DHT_MARKER);
	header_write(ctx, buffer, offset, 2, LH_DC);
	header_write(ctx, buffer, offset, 1, V4L2_JPEG_LUM_HT | V4L2_JPEG_DC_HT);
	for (i = 0 ; i < V4L2_JPEG_REF_HT_DC_LEN; i++)
		header_write(ctx, buffer, offset, 1, v4l2_jpeg_ref_table_luma_dc_ht[i]);

	header_write(ctx, buffer, offset, 2, DHT_MARKER);
	header_write(ctx, buffer, offset, 2, LH_AC);
	header_write(ctx, buffer, offset, 1, V4L2_JPEG_LUM_HT | V4L2_JPEG_AC_HT);
	for (i = 0 ; i < V4L2_JPEG_REF_HT_AC_LEN; i++)
		header_write(ctx, buffer, offset, 1, v4l2_jpeg_ref_table_luma_ac_ht[i]);

	header_write(ctx, buffer, offset, 2, DHT_MARKER);
	header_write(ctx, buffer, offset, 2, LH_DC);
	header_write(ctx, buffer, offset, 1, V4L2_JPEG_CHR_HT | V4L2_JPEG_DC_HT);
	for (i = 0 ; i < V4L2_JPEG_REF_HT_DC_LEN; i++)
		header_write(ctx, buffer, offset, 1, v4l2_jpeg_ref_table_chroma_dc_ht[i]);

	header_write(ctx, buffer, offset, 2, DHT_MARKER);
	header_write(ctx, buffer, offset, 2, LH_AC);
	header_write(ctx, buffer, offset, 1, V4L2_JPEG_CHR_HT | V4L2_JPEG_AC_HT);
	for (i = 0 ; i < V4L2_JPEG_REF_HT_AC_LEN; i++)
		header_write(ctx, buffer, offset, 1, v4l2_jpeg_ref_table_chroma_ac_ht[i]);
}

static void encode_frame_header(struct e5010_context *ctx, void *addr, unsigned int *offset)
{
	u8 *buffer = (u8 *)addr;

	header_write(ctx, buffer, offset, 2, SOF_BASELINE_DCT);
	header_write(ctx, buffer, offset, 2, 8 + (3 * UC_NUM_COMP));
	header_write(ctx, buffer, offset, 1, PRECISION);
	header_write(ctx, buffer, offset, 2, ctx->out_queue.crop.height);
	header_write(ctx, buffer, offset, 2, ctx->out_queue.crop.width);
	header_write(ctx, buffer, offset, 1, UC_NUM_COMP);

	/* Luma details */
	header_write(ctx, buffer, offset, 1, 1);
	if (ctx->out_queue.fmt->subsampling == V4L2_JPEG_CHROMA_SUBSAMPLING_422)
		header_write(ctx, buffer, offset, 1,
			     HORZ_SAMPLING_FACTOR | (VERT_SAMPLING_FACTOR_422));
	else
		header_write(ctx, buffer, offset, 1,
			     HORZ_SAMPLING_FACTOR | (VERT_SAMPLING_FACTOR_420));
	header_write(ctx, buffer, offset, 1, 0);
	/* Chroma details */
	header_write(ctx, buffer, offset, 1, 2);
	header_write(ctx, buffer, offset, 1, (HORZ_SAMPLING_FACTOR >> 1) | 1);
	header_write(ctx, buffer, offset, 1, 1);
	header_write(ctx, buffer, offset, 1, 3);
	header_write(ctx, buffer, offset, 1, (HORZ_SAMPLING_FACTOR >> 1) | 1);
	header_write(ctx, buffer, offset, 1, 1);
}

static void jpg_encode_sos_header(struct e5010_context *ctx, void *addr, unsigned int *offset)
{
	u8 *buffer = (u8 *)addr;
	int i;

	header_write(ctx, buffer, offset, 2, START_OF_SCAN);
	header_write(ctx, buffer, offset, 2, 6 + (COMPONENTS_IN_SCAN << 1));
	header_write(ctx, buffer, offset, 1, COMPONENTS_IN_SCAN);

	for (i = 0; i < COMPONENTS_IN_SCAN; i++) {
		header_write(ctx, buffer, offset, 1, i + 1);
		if (i == 0)
			header_write(ctx, buffer, offset, 1, 0);
		else
			header_write(ctx, buffer, offset, 1, 17);
	}

	header_write(ctx, buffer, offset, 1, 0);
	header_write(ctx, buffer, offset, 1, 63);
	header_write(ctx, buffer, offset, 1, 0);
}

static void write_header(struct e5010_context *ctx, void *addr)
{
	unsigned int offset = 0;

	encode_marker_segment(ctx, addr, &offset);
	encode_frame_header(ctx, addr, &offset);
	jpg_encode_sos_header(ctx, addr, &offset);
}

static irqreturn_t e5010_irq(int irq, void *data)
{
	struct e5010_dev *e5010 = data;
	struct e5010_context *ctx;
	int output_size;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	bool pic_done, out_addr_err;

	spin_lock(&e5010->hw_lock);
	pic_done = e5010_hw_pic_done_irq(e5010->core_base);
	out_addr_err = e5010_hw_output_address_irq(e5010->core_base);

	if (!pic_done && !out_addr_err) {
		spin_unlock(&e5010->hw_lock);
		return IRQ_NONE;
	}

	ctx = v4l2_m2m_get_curr_priv(e5010->m2m_dev);
	if (WARN_ON(!ctx))
		goto job_unlock;

	dst_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	if (!dst_buf || !src_buf) {
		v4l2_err(&e5010->v4l2_dev, "ctx: 0x%p No source or destination buffer\n", ctx);
		goto job_unlock;
	}

	if (out_addr_err) {
		e5010_hw_clear_output_error(e5010->core_base, 1);
		v4l2_warn(&e5010->v4l2_dev,
			  "ctx: 0x%p Output bitstream size exceeded max size\n", ctx);
		v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);
		vb2_set_plane_payload(&dst_buf->vb2_buf, 0, dst_buf->planes[0].length);
		v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_ERROR);
		if (v4l2_m2m_is_last_draining_src_buf(ctx->fh.m2m_ctx, src_buf)) {
			dst_buf->flags |= V4L2_BUF_FLAG_LAST;
			v4l2_m2m_mark_stopped(ctx->fh.m2m_ctx);
			v4l2_event_queue_fh(&ctx->fh, &e5010_eos_event);
			dprintk(e5010, 2, "ctx: 0x%p Sending EOS\n", ctx);
		}
	}

	if (pic_done) {
		e5010_hw_clear_picture_done(e5010->core_base, 1);
		dprintk(e5010, 3, "ctx: 0x%p Got output bitstream of size %d bytes\n",
			ctx, readl(e5010->core_base + JASPER_OUTPUT_SIZE_OFFSET));

		if (v4l2_m2m_is_last_draining_src_buf(ctx->fh.m2m_ctx, src_buf)) {
			dst_buf->flags |= V4L2_BUF_FLAG_LAST;
			v4l2_m2m_mark_stopped(ctx->fh.m2m_ctx);
			v4l2_event_queue_fh(&ctx->fh, &e5010_eos_event);
			dprintk(e5010, 2, "ctx: 0x%p Sending EOS\n", ctx);
		}
		v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);
		output_size = e5010_hw_get_output_size(e5010->core_base);
		vb2_set_plane_payload(&dst_buf->vb2_buf, 0, output_size + HEADER_SIZE);
		v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_DONE);
		dprintk(e5010, 3,
			"ctx: 0x%p frame done for dst_buf->sequence: %d src_buf->sequence: %d\n",
			ctx, dst_buf->sequence, src_buf->sequence);
	}

	v4l2_m2m_job_finish(e5010->m2m_dev, ctx->fh.m2m_ctx);
	dprintk(e5010, 3, "ctx: 0x%p Finish job\n", ctx);

job_unlock:
	spin_unlock(&e5010->hw_lock);
	return IRQ_HANDLED;
}

static int e5010_init_device(struct e5010_dev *e5010)
{
	int ret = 0;

	/*TODO: Set MMU in bypass mode until support for the same is added in driver*/
	e5010_hw_bypass_mmu(e5010->mmu_base, 1);

	if (e5010_hw_enable_auto_clock_gating(e5010->core_base, 1))
		v4l2_warn(&e5010->v4l2_dev, "failed to enable auto clock gating\n");

	if (e5010_hw_enable_manual_clock_gating(e5010->core_base, 0))
		v4l2_warn(&e5010->v4l2_dev, "failed to disable manual clock gating\n");

	if (e5010_hw_enable_crc_check(e5010->core_base, 0))
		v4l2_warn(&e5010->v4l2_dev, "failed to disable CRC check\n");

	if (e5010_hw_enable_output_address_error_irq(e5010->core_base, 1))
		v4l2_err(&e5010->v4l2_dev, "failed to enable Output Address Error interrupts\n");

	ret = e5010_hw_set_input_source_to_memory(e5010->core_base, 1);
	if (ret) {
		v4l2_err(&e5010->v4l2_dev, "failed to set input source to memory\n");
		return ret;
	}

	ret = e5010_hw_enable_picture_done_irq(e5010->core_base, 1);
	if (ret)
		v4l2_err(&e5010->v4l2_dev, "failed to enable Picture Done interrupts\n");

	return ret;
}

static int e5010_probe(struct platform_device *pdev)
{
	struct e5010_dev *e5010;
	int irq, ret = 0;
	struct device *dev = &pdev->dev;

	ret = dma_set_mask(dev, DMA_BIT_MASK(32));
	if (ret)
		return dev_err_probe(dev, ret, "32-bit consistent DMA enable failed\n");

	e5010 = devm_kzalloc(dev, sizeof(*e5010), GFP_KERNEL);
	if (!e5010)
		return -ENOMEM;

	platform_set_drvdata(pdev, e5010);

	e5010->dev = dev;

	mutex_init(&e5010->mutex);
	spin_lock_init(&e5010->hw_lock);

	e5010->vdev = video_device_alloc();
	if (!e5010->vdev) {
		dev_err(dev, "failed to allocate video device\n");
		return -ENOMEM;
	}

	snprintf(e5010->vdev->name, sizeof(e5010->vdev->name), "%s", E5010_MODULE_NAME);
	e5010->vdev->fops = &e5010_fops;
	e5010->vdev->ioctl_ops = &e5010_ioctl_ops;
	e5010->vdev->minor = -1;
	e5010->vdev->release = video_device_release;
	e5010->vdev->vfl_dir = VFL_DIR_M2M;
	e5010->vdev->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;
	e5010->vdev->v4l2_dev = &e5010->v4l2_dev;
	e5010->vdev->lock = &e5010->mutex;

	ret = v4l2_device_register(dev, &e5010->v4l2_dev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register v4l2 device\n");

	e5010->m2m_dev = v4l2_m2m_init(&e5010_m2m_ops);
	if (IS_ERR(e5010->m2m_dev)) {
		ret = PTR_ERR(e5010->m2m_dev);
		e5010->m2m_dev = NULL;
		dev_err_probe(dev, ret, "failed to init mem2mem device\n");
		goto fail_after_v4l2_register;
	}

	video_set_drvdata(e5010->vdev, e5010);

	e5010->core_base = devm_platform_ioremap_resource_byname(pdev, "core");
	if (IS_ERR(e5010->core_base)) {
		ret = PTR_ERR(e5010->core_base);
		dev_err_probe(dev, ret, "Missing 'core' resources area\n");
		goto fail_after_v4l2_register;
	}

	e5010->mmu_base = devm_platform_ioremap_resource_byname(pdev, "mmu");
	if (IS_ERR(e5010->mmu_base)) {
		ret = PTR_ERR(e5010->mmu_base);
		dev_err_probe(dev, ret, "Missing 'mmu' resources area\n");
		goto fail_after_v4l2_register;
	}

	e5010->last_context_run = NULL;

	irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(dev, irq, e5010_irq, 0,
			       E5010_MODULE_NAME, e5010);
	if (ret) {
		dev_err_probe(dev, ret, "failed to register IRQ %d\n", irq);
		goto fail_after_v4l2_register;
	}

	e5010->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(e5010->clk)) {
		ret = PTR_ERR(e5010->clk);
		dev_err_probe(dev, ret, "failed to get clock\n");
		goto fail_after_v4l2_register;
	}

	pm_runtime_enable(dev);

	ret = video_register_device(e5010->vdev, VFL_TYPE_VIDEO, 0);
	if (ret) {
		dev_err_probe(dev, ret, "failed to register video device\n");
		goto fail_after_video_register_device;
	}

	v4l2_info(&e5010->v4l2_dev, "Device registered as /dev/video%d\n",
		  e5010->vdev->num);

	return 0;

fail_after_video_register_device:
	v4l2_m2m_release(e5010->m2m_dev);
fail_after_v4l2_register:
	v4l2_device_unregister(&e5010->v4l2_dev);
	return ret;
}

static void e5010_remove(struct platform_device *pdev)
{
	struct e5010_dev *e5010 = platform_get_drvdata(pdev);

	pm_runtime_disable(e5010->dev);
	video_unregister_device(e5010->vdev);
	v4l2_m2m_release(e5010->m2m_dev);
	v4l2_device_unregister(&e5010->v4l2_dev);
}

static void e5010_vb2_buffers_return(struct vb2_queue *q, enum vb2_buffer_state state)
{
	struct vb2_v4l2_buffer *vbuf;
	struct e5010_context *ctx = vb2_get_drv_priv(q);

	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		while ((vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx))) {
			dprintk(ctx->e5010, 2, "ctx: 0x%p, buf type %s | index %d\n",
				ctx, type_name(vbuf->vb2_buf.type), vbuf->vb2_buf.index);
			v4l2_m2m_buf_done(vbuf, state);
		}
	} else {
		while ((vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx))) {
			dprintk(ctx->e5010, 2, "ctx: 0x%p, buf type %s | index %d\n",
				ctx, type_name(vbuf->vb2_buf.type), vbuf->vb2_buf.index);
			vb2_set_plane_payload(&vbuf->vb2_buf, 0, 0);
			v4l2_m2m_buf_done(vbuf, state);
		}
	}
}

static int e5010_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers, unsigned int *nplanes,
			     unsigned int sizes[], struct device *alloc_devs[])
{
	struct e5010_context *ctx = vb2_get_drv_priv(vq);
	struct e5010_q_data *queue;
	int i;

	queue = get_queue(ctx, vq->type);

	if (*nplanes) {
		if (*nplanes != queue->fmt->num_planes)
			return -EINVAL;
		for (i = 0; i < *nplanes; i++) {
			if (sizes[i] < queue->sizeimage[i])
				return -EINVAL;
		}
		return 0;
	}

	*nplanes = queue->fmt->num_planes;
	for (i = 0; i < *nplanes; i++)
		sizes[i] = queue->sizeimage[i];

	dprintk(ctx->e5010, 2,
		"ctx: 0x%p, type %s, buffer(s): %d, planes %d, plane1: bytes %d plane2: %d bytes\n",
		ctx, type_name(vq->type), *nbuffers, *nplanes, sizes[0], sizes[1]);

	return 0;
}

static void e5010_buf_finish(struct vb2_buffer *vb)
{
	struct e5010_context *ctx = vb2_get_drv_priv(vb->vb2_queue);
	void *d_addr;

	if (vb->state != VB2_BUF_STATE_DONE || V4L2_TYPE_IS_OUTPUT(vb->vb2_queue->type))
		return;

	d_addr = vb2_plane_vaddr(vb, 0);
	write_header(ctx, d_addr);
}

static int e5010_buf_out_validate(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct e5010_context *ctx = vb2_get_drv_priv(vb->vb2_queue);

	if (vbuf->field != V4L2_FIELD_NONE)
		dprintk(ctx->e5010, 1, "ctx: 0x%p, field isn't supported\n", ctx);

	vbuf->field = V4L2_FIELD_NONE;

	return 0;
}

static int e5010_buf_prepare(struct vb2_buffer *vb)
{
	struct e5010_context *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct e5010_q_data *queue;
	int i;

	vbuf->field = V4L2_FIELD_NONE;

	queue = get_queue(ctx, vb->vb2_queue->type);

	for (i = 0; i < queue->fmt->num_planes; i++) {
		if (vb2_plane_size(vb, i) < (unsigned long)queue->sizeimage[i]) {
			v4l2_err(&ctx->e5010->v4l2_dev, "plane %d too small (%lu < %lu)", i,
				 vb2_plane_size(vb, i), (unsigned long)queue->sizeimage[i]);

			return -EINVAL;
		}
	}

	if (V4L2_TYPE_IS_CAPTURE(vb->vb2_queue->type)) {
		vb2_set_plane_payload(vb, 0, 0);
		vb2_set_plane_payload(vb, 1, 0);
	}

	return 0;
}

static void e5010_buf_queue(struct vb2_buffer *vb)
{
	struct e5010_context *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	if (V4L2_TYPE_IS_CAPTURE(vb->vb2_queue->type) &&
	    vb2_is_streaming(vb->vb2_queue) &&
	    v4l2_m2m_dst_buf_is_last(ctx->fh.m2m_ctx)) {
		struct e5010_q_data *queue = get_queue(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

		vbuf->sequence = queue->sequence++;
		v4l2_m2m_last_buffer_done(ctx->fh.m2m_ctx, vbuf);
		v4l2_event_queue_fh(&ctx->fh, &e5010_eos_event);
		return;
	}

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static int e5010_encoder_cmd(struct file *file, void *priv,
			     struct v4l2_encoder_cmd *cmd)
{
	struct e5010_context *ctx = file->private_data;
	int ret;
	struct vb2_queue *cap_vq;

	cap_vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	ret = v4l2_m2m_ioctl_try_encoder_cmd(file, &ctx->fh, cmd);
	if (ret < 0)
		return ret;

	if (!vb2_is_streaming(v4l2_m2m_get_src_vq(ctx->fh.m2m_ctx)) ||
	    !vb2_is_streaming(v4l2_m2m_get_dst_vq(ctx->fh.m2m_ctx)))
		return 0;

	ret = v4l2_m2m_ioctl_encoder_cmd(file, &ctx->fh, cmd);
	if (ret < 0)
		return ret;

	if (cmd->cmd == V4L2_ENC_CMD_STOP &&
	    v4l2_m2m_has_stopped(ctx->fh.m2m_ctx))
		v4l2_event_queue_fh(&ctx->fh, &e5010_eos_event);

	if (cmd->cmd == V4L2_ENC_CMD_START &&
	    v4l2_m2m_has_stopped(ctx->fh.m2m_ctx))
		vb2_clear_last_buffer_dequeued(cap_vq);

	return 0;
}

static int e5010_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct e5010_context *ctx = vb2_get_drv_priv(q);
	int ret;

	struct e5010_q_data *queue = get_queue(ctx, q->type);

	v4l2_m2m_update_start_streaming_state(ctx->fh.m2m_ctx, q);
	queue->sequence = 0;

	ret = pm_runtime_resume_and_get(ctx->e5010->dev);
	if (ret < 0) {
		v4l2_err(&ctx->e5010->v4l2_dev, "failed to power up jpeg\n");
		goto fail;
	}

	ret = e5010_init_device(ctx->e5010);
	if (ret) {
		v4l2_err(&ctx->e5010->v4l2_dev, "failed to Enable e5010 device\n");
		goto fail;
	}

	return 0;

fail:
	e5010_vb2_buffers_return(q, VB2_BUF_STATE_QUEUED);

	return ret;
}

static void e5010_stop_streaming(struct vb2_queue *q)
{
	struct e5010_context *ctx = vb2_get_drv_priv(q);

	e5010_vb2_buffers_return(q, VB2_BUF_STATE_ERROR);

	if (V4L2_TYPE_IS_OUTPUT(q->type))
		v4l2_m2m_update_stop_streaming_state(ctx->fh.m2m_ctx, q);

	if (V4L2_TYPE_IS_OUTPUT(q->type) &&
	    v4l2_m2m_has_stopped(ctx->fh.m2m_ctx)) {
		v4l2_event_queue_fh(&ctx->fh, &e5010_eos_event);
	}

	pm_runtime_put_sync(ctx->e5010->dev);
}

static void e5010_device_run(void *priv)
{
	struct e5010_context *ctx = priv;
	struct e5010_dev *e5010 = ctx->e5010;
	struct vb2_v4l2_buffer *s_vb, *d_vb;
	u32 reg = 0;
	int ret = 0, luma_crop_offset = 0, chroma_crop_offset = 0;
	unsigned long flags;
	int num_planes = ctx->out_queue.fmt->num_planes;

	spin_lock_irqsave(&e5010->hw_lock, flags);
	s_vb = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	WARN_ON(!s_vb);
	d_vb = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	WARN_ON(!d_vb);
	if (!s_vb || !d_vb)
		goto no_ready_buf_err;

	s_vb->sequence = ctx->out_queue.sequence++;
	d_vb->sequence = ctx->cap_queue.sequence++;

	v4l2_m2m_buf_copy_metadata(s_vb, d_vb, false);

	if (ctx != e5010->last_context_run || ctx->update_qp) {
		dprintk(e5010, 1, "ctx updated: 0x%p -> 0x%p, updating qp tables\n",
			e5010->last_context_run, ctx);
		ret = update_qp_tables(ctx);
	}

	if (ret) {
		ctx->update_qp = true;
		v4l2_err(&e5010->v4l2_dev, "failed to update QP tables\n");
		goto device_busy_err;
	} else {
		e5010->last_context_run = ctx;
		ctx->update_qp = false;
	}

	/* Set I/O Buffer addresses */
	reg = (u32)vb2_dma_contig_plane_dma_addr(&s_vb->vb2_buf, 0);

	if (ctx->out_queue.crop_set) {
		luma_crop_offset = ctx->out_queue.bytesperline[0] * ctx->out_queue.crop.top +
				   ctx->out_queue.crop.left;

		if (ctx->out_queue.fmt->subsampling == V4L2_JPEG_CHROMA_SUBSAMPLING_422) {
			chroma_crop_offset =
				ctx->out_queue.bytesperline[0] * ctx->out_queue.crop.top
				+ ctx->out_queue.crop.left;
		} else {
			chroma_crop_offset =
				ctx->out_queue.bytesperline[0] * ctx->out_queue.crop.top / 2
				+ ctx->out_queue.crop.left;
		}

		dprintk(e5010, 1, "Luma crop offset : %x, chroma crop offset : %x\n",
			luma_crop_offset, chroma_crop_offset);
	}

	ret = e5010_hw_set_input_luma_addr(e5010->core_base, reg + luma_crop_offset);
	if (ret || !reg) {
		v4l2_err(&e5010->v4l2_dev, "failed to set input luma address\n");
		goto device_busy_err;
	}

	if (num_planes == 1)
		reg += (ctx->out_queue.bytesperline[0]) * (ctx->out_queue.height);
	else
		reg = (u32)vb2_dma_contig_plane_dma_addr(&s_vb->vb2_buf, 1);

	dprintk(e5010, 3,
		"ctx: 0x%p, luma_addr: 0x%x, chroma_addr: 0x%x, out_addr: 0x%x\n",
		ctx, (u32)vb2_dma_contig_plane_dma_addr(&s_vb->vb2_buf, 0) + luma_crop_offset,
		reg + chroma_crop_offset, (u32)vb2_dma_contig_plane_dma_addr(&d_vb->vb2_buf, 0));

	dprintk(e5010, 3,
		"ctx: 0x%p, buf indices: src_index: %d, dst_index: %d\n",
		ctx, s_vb->vb2_buf.index, d_vb->vb2_buf.index);

	ret = e5010_hw_set_input_chroma_addr(e5010->core_base, reg + chroma_crop_offset);
	if (ret || !reg) {
		v4l2_err(&e5010->v4l2_dev, "failed to set input chroma address\n");
		goto device_busy_err;
	}

	reg = (u32)vb2_dma_contig_plane_dma_addr(&d_vb->vb2_buf, 0);
	reg += HEADER_SIZE;
	ret = e5010_hw_set_output_base_addr(e5010->core_base, reg);
	if (ret || !reg) {
		v4l2_err(&e5010->v4l2_dev, "failed to set output base address\n");
		goto device_busy_err;
	}

	/* Set input settings */
	ret = e5010_hw_set_horizontal_size(e5010->core_base, ctx->out_queue.crop.width - 1);
	if (ret) {
		v4l2_err(&e5010->v4l2_dev, "failed to set input width\n");
		goto device_busy_err;
	}

	ret = e5010_hw_set_vertical_size(e5010->core_base, ctx->out_queue.crop.height - 1);
	if (ret) {
		v4l2_err(&e5010->v4l2_dev, "failed to set input width\n");
		goto device_busy_err;
	}

	ret = e5010_hw_set_luma_stride(e5010->core_base, ctx->out_queue.bytesperline[0]);
	if (ret) {
		v4l2_err(&e5010->v4l2_dev, "failed to set luma stride\n");
		goto device_busy_err;
	}

	ret = e5010_hw_set_chroma_stride(e5010->core_base, ctx->out_queue.bytesperline[0]);
	if (ret) {
		v4l2_err(&e5010->v4l2_dev, "failed to set chroma stride\n");
		goto device_busy_err;
	}

	ret = e5010_set_input_subsampling(e5010->core_base, ctx->out_queue.fmt->subsampling);
	if (ret) {
		v4l2_err(&e5010->v4l2_dev, "failed to set input subsampling\n");
		goto device_busy_err;
	}

	ret = e5010_hw_set_chroma_order(e5010->core_base, ctx->out_queue.fmt->chroma_order);
	if (ret) {
		v4l2_err(&e5010->v4l2_dev, "failed to set chroma order\n");
		goto device_busy_err;
	}

	e5010_hw_set_output_max_size(e5010->core_base, d_vb->planes[0].length);
	e5010_hw_encode_start(e5010->core_base, 1);

	spin_unlock_irqrestore(&e5010->hw_lock, flags);

	return;

device_busy_err:
	e5010_reset(e5010->dev, e5010->core_base, e5010->mmu_base);

no_ready_buf_err:
	if (s_vb) {
		v4l2_m2m_src_buf_remove_by_buf(ctx->fh.m2m_ctx, s_vb);
		v4l2_m2m_buf_done(s_vb, VB2_BUF_STATE_ERROR);
	}

	if (d_vb) {
		v4l2_m2m_dst_buf_remove_by_buf(ctx->fh.m2m_ctx, d_vb);
		/* Payload set to 1 since 0 payload can trigger EOS */
		vb2_set_plane_payload(&d_vb->vb2_buf, 0, 1);
		v4l2_m2m_buf_done(d_vb, VB2_BUF_STATE_ERROR);
	}
	v4l2_m2m_job_finish(e5010->m2m_dev, ctx->fh.m2m_ctx);
	spin_unlock_irqrestore(&e5010->hw_lock, flags);
}

#ifdef CONFIG_PM
static int e5010_runtime_resume(struct device *dev)
{
	struct e5010_dev *e5010 = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(e5010->clk);
	if (ret < 0) {
		v4l2_err(&e5010->v4l2_dev, "failed to enable clock\n");
		return ret;
	}

	return 0;
}

static int e5010_runtime_suspend(struct device *dev)
{
	struct e5010_dev *e5010 = dev_get_drvdata(dev);

	clk_disable_unprepare(e5010->clk);

	return 0;
}
#endif

#ifdef CONFIG_PM_SLEEP
static int e5010_suspend(struct device *dev)
{
	struct e5010_dev *e5010 = dev_get_drvdata(dev);

	v4l2_m2m_suspend(e5010->m2m_dev);

	return pm_runtime_force_suspend(dev);
}

static int e5010_resume(struct device *dev)
{
	struct e5010_dev *e5010 = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret < 0)
		return ret;

	ret = e5010_init_device(e5010);
	if (ret) {
		dev_err(dev, "Failed to re-enable e5010 device\n");
		return ret;
	}

	v4l2_m2m_resume(e5010->m2m_dev);

	return ret;
}
#endif

static const struct dev_pm_ops	e5010_pm_ops = {
	SET_RUNTIME_PM_OPS(e5010_runtime_suspend,
			   e5010_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(e5010_suspend, e5010_resume)
};

static const struct v4l2_ioctl_ops e5010_ioctl_ops = {
	.vidioc_querycap = e5010_querycap,

	.vidioc_enum_fmt_vid_cap = e5010_enum_fmt,
	.vidioc_g_fmt_vid_cap_mplane = e5010_g_fmt,
	.vidioc_try_fmt_vid_cap_mplane = e5010_try_fmt,
	.vidioc_s_fmt_vid_cap_mplane = e5010_s_fmt,

	.vidioc_enum_fmt_vid_out = e5010_enum_fmt,
	.vidioc_g_fmt_vid_out_mplane = e5010_g_fmt,
	.vidioc_try_fmt_vid_out_mplane = e5010_try_fmt,
	.vidioc_s_fmt_vid_out_mplane = e5010_s_fmt,

	.vidioc_g_selection = e5010_g_selection,
	.vidioc_s_selection = e5010_s_selection,

	.vidioc_reqbufs = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf = v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf = v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf = v4l2_m2m_ioctl_dqbuf,
	.vidioc_expbuf = v4l2_m2m_ioctl_expbuf,
	.vidioc_create_bufs = v4l2_m2m_ioctl_create_bufs,
	.vidioc_prepare_buf = v4l2_m2m_ioctl_prepare_buf,

	.vidioc_streamon = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff = v4l2_m2m_ioctl_streamoff,
	.vidioc_log_status = v4l2_ctrl_log_status,

	.vidioc_subscribe_event = e5010_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
	.vidioc_try_encoder_cmd = v4l2_m2m_ioctl_try_encoder_cmd,
	.vidioc_encoder_cmd = e5010_encoder_cmd,

	.vidioc_enum_framesizes = e5010_enum_framesizes,
};

static const struct vb2_ops e5010_video_ops = {
	.queue_setup = e5010_queue_setup,
	.buf_queue = e5010_buf_queue,
	.buf_finish = e5010_buf_finish,
	.buf_prepare = e5010_buf_prepare,
	.buf_out_validate = e5010_buf_out_validate,
	.start_streaming = e5010_start_streaming,
	.stop_streaming = e5010_stop_streaming,
};

static const struct v4l2_file_operations e5010_fops = {
	.owner = THIS_MODULE,
	.open = e5010_open,
	.release = e5010_release,
	.poll = v4l2_m2m_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = v4l2_m2m_fop_mmap,
};

static const struct v4l2_m2m_ops e5010_m2m_ops = {
	.device_run = e5010_device_run,
};

static const struct of_device_id e5010_of_match[] = {
	{.compatible = "img,e5010-jpeg-enc"},   { /* end */},
};
MODULE_DEVICE_TABLE(of, e5010_of_match);

static struct platform_driver e5010_driver = {
	.probe = e5010_probe,
	.remove = e5010_remove,
	.driver = {
		.name = E5010_MODULE_NAME,
		.of_match_table = e5010_of_match,
		.pm = &e5010_pm_ops,
	},
};
module_platform_driver(e5010_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Imagination E5010 JPEG encoder driver");
