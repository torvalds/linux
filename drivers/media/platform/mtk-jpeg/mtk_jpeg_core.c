// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Ming Hsiu Tsai <minghsiu.tsai@mediatek.com>
 *         Rick Chang <rick.chang@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>
#include <soc/mediatek/smi.h>

#include "mtk_jpeg_hw.h"
#include "mtk_jpeg_core.h"
#include "mtk_jpeg_parse.h"

static struct mtk_jpeg_fmt mtk_jpeg_formats[] = {
	{
		.fourcc		= V4L2_PIX_FMT_JPEG,
		.colplanes	= 1,
		.flags		= MTK_JPEG_FMT_FLAG_DEC_OUTPUT,
	},
	{
		.fourcc		= V4L2_PIX_FMT_YUV420M,
		.h_sample	= {4, 2, 2},
		.v_sample	= {4, 2, 2},
		.colplanes	= 3,
		.h_align	= 5,
		.v_align	= 4,
		.flags		= MTK_JPEG_FMT_FLAG_DEC_CAPTURE,
	},
	{
		.fourcc		= V4L2_PIX_FMT_YUV422M,
		.h_sample	= {4, 2, 2},
		.v_sample	= {4, 4, 4},
		.colplanes	= 3,
		.h_align	= 5,
		.v_align	= 3,
		.flags		= MTK_JPEG_FMT_FLAG_DEC_CAPTURE,
	},
};

#define MTK_JPEG_NUM_FORMATS ARRAY_SIZE(mtk_jpeg_formats)

enum {
	MTK_JPEG_BUF_FLAGS_INIT			= 0,
	MTK_JPEG_BUF_FLAGS_LAST_FRAME		= 1,
};

struct mtk_jpeg_src_buf {
	struct vb2_v4l2_buffer b;
	struct list_head list;
	int flags;
	struct mtk_jpeg_dec_param dec_param;
};

static int debug;
module_param(debug, int, 0644);

static inline struct mtk_jpeg_ctx *mtk_jpeg_fh_to_ctx(struct v4l2_fh *fh)
{
	return container_of(fh, struct mtk_jpeg_ctx, fh);
}

static inline struct mtk_jpeg_src_buf *mtk_jpeg_vb2_to_srcbuf(
							struct vb2_buffer *vb)
{
	return container_of(to_vb2_v4l2_buffer(vb), struct mtk_jpeg_src_buf, b);
}

static int mtk_jpeg_querycap(struct file *file, void *priv,
			     struct v4l2_capability *cap)
{
	struct mtk_jpeg_dev *jpeg = video_drvdata(file);

	strscpy(cap->driver, MTK_JPEG_NAME " decoder", sizeof(cap->driver));
	strscpy(cap->card, MTK_JPEG_NAME " decoder", sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 dev_name(jpeg->dev));

	return 0;
}

static int mtk_jpeg_enum_fmt(struct mtk_jpeg_fmt *mtk_jpeg_formats, int n,
			     struct v4l2_fmtdesc *f, u32 type)
{
	int i, num = 0;

	for (i = 0; i < n; ++i) {
		if (mtk_jpeg_formats[i].flags & type) {
			if (num == f->index)
				break;
			++num;
		}
	}

	if (i >= n)
		return -EINVAL;

	f->pixelformat = mtk_jpeg_formats[i].fourcc;

	return 0;
}

static int mtk_jpeg_enum_fmt_vid_cap(struct file *file, void *priv,
				     struct v4l2_fmtdesc *f)
{
	return mtk_jpeg_enum_fmt(mtk_jpeg_formats, MTK_JPEG_NUM_FORMATS, f,
				 MTK_JPEG_FMT_FLAG_DEC_CAPTURE);
}

static int mtk_jpeg_enum_fmt_vid_out(struct file *file, void *priv,
				     struct v4l2_fmtdesc *f)
{
	return mtk_jpeg_enum_fmt(mtk_jpeg_formats, MTK_JPEG_NUM_FORMATS, f,
				 MTK_JPEG_FMT_FLAG_DEC_OUTPUT);
}

static struct mtk_jpeg_q_data *mtk_jpeg_get_q_data(struct mtk_jpeg_ctx *ctx,
						   enum v4l2_buf_type type)
{
	if (V4L2_TYPE_IS_OUTPUT(type))
		return &ctx->out_q;
	return &ctx->cap_q;
}

static struct mtk_jpeg_fmt *mtk_jpeg_find_format(struct mtk_jpeg_ctx *ctx,
						 u32 pixelformat,
						 unsigned int fmt_type)
{
	unsigned int k, fmt_flag;

	fmt_flag = (fmt_type == MTK_JPEG_FMT_TYPE_OUTPUT) ?
		   MTK_JPEG_FMT_FLAG_DEC_OUTPUT :
		   MTK_JPEG_FMT_FLAG_DEC_CAPTURE;

	for (k = 0; k < MTK_JPEG_NUM_FORMATS; k++) {
		struct mtk_jpeg_fmt *fmt = &mtk_jpeg_formats[k];

		if (fmt->fourcc == pixelformat && fmt->flags & fmt_flag)
			return fmt;
	}

	return NULL;
}

static void mtk_jpeg_bound_align_image(u32 *w, unsigned int wmin,
				       unsigned int wmax, unsigned int walign,
				       u32 *h, unsigned int hmin,
				       unsigned int hmax, unsigned int halign)
{
	int width, height, w_step, h_step;

	width = *w;
	height = *h;
	w_step = 1 << walign;
	h_step = 1 << halign;

	v4l_bound_align_image(w, wmin, wmax, walign, h, hmin, hmax, halign, 0);
	if (*w < width && (*w + w_step) <= wmax)
		*w += w_step;
	if (*h < height && (*h + h_step) <= hmax)
		*h += h_step;
}

static void mtk_jpeg_adjust_fmt_mplane(struct mtk_jpeg_ctx *ctx,
				       struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct mtk_jpeg_q_data *q_data;
	int i;

	q_data = mtk_jpeg_get_q_data(ctx, f->type);

	pix_mp->width = q_data->w;
	pix_mp->height = q_data->h;
	pix_mp->pixelformat = q_data->fmt->fourcc;
	pix_mp->num_planes = q_data->fmt->colplanes;

	for (i = 0; i < pix_mp->num_planes; i++) {
		pix_mp->plane_fmt[i].bytesperline = q_data->bytesperline[i];
		pix_mp->plane_fmt[i].sizeimage = q_data->sizeimage[i];
	}
}

static int mtk_jpeg_try_fmt_mplane(struct v4l2_format *f,
				   struct mtk_jpeg_fmt *fmt,
				   struct mtk_jpeg_ctx *ctx, int q_type)
{
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;
	int i;

	memset(pix_mp->reserved, 0, sizeof(pix_mp->reserved));
	pix_mp->field = V4L2_FIELD_NONE;

	if (ctx->state != MTK_JPEG_INIT) {
		mtk_jpeg_adjust_fmt_mplane(ctx, f);
		goto end;
	}

	pix_mp->num_planes = fmt->colplanes;
	pix_mp->pixelformat = fmt->fourcc;

	if (q_type == MTK_JPEG_FMT_TYPE_OUTPUT) {
		struct v4l2_plane_pix_format *pfmt = &pix_mp->plane_fmt[0];

		mtk_jpeg_bound_align_image(&pix_mp->width, MTK_JPEG_MIN_WIDTH,
					   MTK_JPEG_MAX_WIDTH, 0,
					   &pix_mp->height, MTK_JPEG_MIN_HEIGHT,
					   MTK_JPEG_MAX_HEIGHT, 0);

		memset(pfmt->reserved, 0, sizeof(pfmt->reserved));
		pfmt->bytesperline = 0;
		/* Source size must be aligned to 128 */
		pfmt->sizeimage = mtk_jpeg_align(pfmt->sizeimage, 128);
		if (pfmt->sizeimage == 0)
			pfmt->sizeimage = MTK_JPEG_DEFAULT_SIZEIMAGE;
		goto end;
	}

	/* type is MTK_JPEG_FMT_TYPE_CAPTURE */
	mtk_jpeg_bound_align_image(&pix_mp->width, MTK_JPEG_MIN_WIDTH,
				   MTK_JPEG_MAX_WIDTH, fmt->h_align,
				   &pix_mp->height, MTK_JPEG_MIN_HEIGHT,
				   MTK_JPEG_MAX_HEIGHT, fmt->v_align);

	for (i = 0; i < fmt->colplanes; i++) {
		struct v4l2_plane_pix_format *pfmt = &pix_mp->plane_fmt[i];
		u32 stride = pix_mp->width * fmt->h_sample[i] / 4;
		u32 h = pix_mp->height * fmt->v_sample[i] / 4;

		memset(pfmt->reserved, 0, sizeof(pfmt->reserved));
		pfmt->bytesperline = stride;
		pfmt->sizeimage = stride * h;
	}
end:
	v4l2_dbg(2, debug, &jpeg->v4l2_dev, "wxh:%ux%u\n",
		 pix_mp->width, pix_mp->height);
	for (i = 0; i < pix_mp->num_planes; i++) {
		v4l2_dbg(2, debug, &jpeg->v4l2_dev,
			 "plane[%d] bpl=%u, size=%u\n",
			 i,
			 pix_mp->plane_fmt[i].bytesperline,
			 pix_mp->plane_fmt[i].sizeimage);
	}
	return 0;
}

static int mtk_jpeg_g_fmt_vid_mplane(struct file *file, void *priv,
				     struct v4l2_format *f)
{
	struct vb2_queue *vq;
	struct mtk_jpeg_q_data *q_data = NULL;
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct mtk_jpeg_ctx *ctx = mtk_jpeg_fh_to_ctx(priv);
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;
	int i;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = mtk_jpeg_get_q_data(ctx, f->type);

	memset(pix_mp->reserved, 0, sizeof(pix_mp->reserved));
	pix_mp->width = q_data->w;
	pix_mp->height = q_data->h;
	pix_mp->field = V4L2_FIELD_NONE;
	pix_mp->pixelformat = q_data->fmt->fourcc;
	pix_mp->num_planes = q_data->fmt->colplanes;
	pix_mp->colorspace = ctx->colorspace;
	pix_mp->ycbcr_enc = ctx->ycbcr_enc;
	pix_mp->xfer_func = ctx->xfer_func;
	pix_mp->quantization = ctx->quantization;

	v4l2_dbg(1, debug, &jpeg->v4l2_dev, "(%d) g_fmt:%c%c%c%c wxh:%ux%u\n",
		 f->type,
		 (pix_mp->pixelformat & 0xff),
		 (pix_mp->pixelformat >>  8 & 0xff),
		 (pix_mp->pixelformat >> 16 & 0xff),
		 (pix_mp->pixelformat >> 24 & 0xff),
		 pix_mp->width, pix_mp->height);

	for (i = 0; i < pix_mp->num_planes; i++) {
		struct v4l2_plane_pix_format *pfmt = &pix_mp->plane_fmt[i];

		pfmt->bytesperline = q_data->bytesperline[i];
		pfmt->sizeimage = q_data->sizeimage[i];
		memset(pfmt->reserved, 0, sizeof(pfmt->reserved));

		v4l2_dbg(1, debug, &jpeg->v4l2_dev,
			 "plane[%d] bpl=%u, size=%u\n",
			 i,
			 pfmt->bytesperline,
			 pfmt->sizeimage);
	}
	return 0;
}

static int mtk_jpeg_try_fmt_vid_cap_mplane(struct file *file, void *priv,
					   struct v4l2_format *f)
{
	struct mtk_jpeg_ctx *ctx = mtk_jpeg_fh_to_ctx(priv);
	struct mtk_jpeg_fmt *fmt;

	fmt = mtk_jpeg_find_format(ctx, f->fmt.pix_mp.pixelformat,
				   MTK_JPEG_FMT_TYPE_CAPTURE);
	if (!fmt)
		fmt = ctx->cap_q.fmt;

	v4l2_dbg(2, debug, &ctx->jpeg->v4l2_dev, "(%d) try_fmt:%c%c%c%c\n",
		 f->type,
		 (fmt->fourcc & 0xff),
		 (fmt->fourcc >>  8 & 0xff),
		 (fmt->fourcc >> 16 & 0xff),
		 (fmt->fourcc >> 24 & 0xff));

	return mtk_jpeg_try_fmt_mplane(f, fmt, ctx, MTK_JPEG_FMT_TYPE_CAPTURE);
}

static int mtk_jpeg_try_fmt_vid_out_mplane(struct file *file, void *priv,
					   struct v4l2_format *f)
{
	struct mtk_jpeg_ctx *ctx = mtk_jpeg_fh_to_ctx(priv);
	struct mtk_jpeg_fmt *fmt;

	fmt = mtk_jpeg_find_format(ctx, f->fmt.pix_mp.pixelformat,
				   MTK_JPEG_FMT_TYPE_OUTPUT);
	if (!fmt)
		fmt = ctx->out_q.fmt;

	v4l2_dbg(2, debug, &ctx->jpeg->v4l2_dev, "(%d) try_fmt:%c%c%c%c\n",
		 f->type,
		 (fmt->fourcc & 0xff),
		 (fmt->fourcc >>  8 & 0xff),
		 (fmt->fourcc >> 16 & 0xff),
		 (fmt->fourcc >> 24 & 0xff));

	return mtk_jpeg_try_fmt_mplane(f, fmt, ctx, MTK_JPEG_FMT_TYPE_OUTPUT);
}

static int mtk_jpeg_s_fmt_mplane(struct mtk_jpeg_ctx *ctx,
				 struct v4l2_format *f)
{
	struct vb2_queue *vq;
	struct mtk_jpeg_q_data *q_data = NULL;
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;
	unsigned int f_type;
	int i;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = mtk_jpeg_get_q_data(ctx, f->type);

	if (vb2_is_busy(vq)) {
		v4l2_err(&jpeg->v4l2_dev, "queue busy\n");
		return -EBUSY;
	}

	f_type = V4L2_TYPE_IS_OUTPUT(f->type) ?
			 MTK_JPEG_FMT_TYPE_OUTPUT : MTK_JPEG_FMT_TYPE_CAPTURE;

	q_data->fmt = mtk_jpeg_find_format(ctx, pix_mp->pixelformat, f_type);
	q_data->w = pix_mp->width;
	q_data->h = pix_mp->height;
	ctx->colorspace = pix_mp->colorspace;
	ctx->ycbcr_enc = pix_mp->ycbcr_enc;
	ctx->xfer_func = pix_mp->xfer_func;
	ctx->quantization = pix_mp->quantization;

	v4l2_dbg(1, debug, &jpeg->v4l2_dev, "(%d) s_fmt:%c%c%c%c wxh:%ux%u\n",
		 f->type,
		 (q_data->fmt->fourcc & 0xff),
		 (q_data->fmt->fourcc >>  8 & 0xff),
		 (q_data->fmt->fourcc >> 16 & 0xff),
		 (q_data->fmt->fourcc >> 24 & 0xff),
		 q_data->w, q_data->h);

	for (i = 0; i < q_data->fmt->colplanes; i++) {
		q_data->bytesperline[i] = pix_mp->plane_fmt[i].bytesperline;
		q_data->sizeimage[i] = pix_mp->plane_fmt[i].sizeimage;

		v4l2_dbg(1, debug, &jpeg->v4l2_dev,
			 "plane[%d] bpl=%u, size=%u\n",
			 i, q_data->bytesperline[i], q_data->sizeimage[i]);
	}

	return 0;
}

static int mtk_jpeg_s_fmt_vid_out_mplane(struct file *file, void *priv,
					 struct v4l2_format *f)
{
	int ret;

	ret = mtk_jpeg_try_fmt_vid_out_mplane(file, priv, f);
	if (ret)
		return ret;

	return mtk_jpeg_s_fmt_mplane(mtk_jpeg_fh_to_ctx(priv), f);
}

static int mtk_jpeg_s_fmt_vid_cap_mplane(struct file *file, void *priv,
					 struct v4l2_format *f)
{
	int ret;

	ret = mtk_jpeg_try_fmt_vid_cap_mplane(file, priv, f);
	if (ret)
		return ret;

	return mtk_jpeg_s_fmt_mplane(mtk_jpeg_fh_to_ctx(priv), f);
}

static void mtk_jpeg_queue_src_chg_event(struct mtk_jpeg_ctx *ctx)
{
	static const struct v4l2_event ev_src_ch = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes =
		V4L2_EVENT_SRC_CH_RESOLUTION,
	};

	v4l2_event_queue_fh(&ctx->fh, &ev_src_ch);
}

static int mtk_jpeg_subscribe_event(struct v4l2_fh *fh,
				    const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subscribe(fh, sub);
	default:
		return -EINVAL;
	}
}

static int mtk_jpeg_g_selection(struct file *file, void *priv,
				struct v4l2_selection *s)
{
	struct mtk_jpeg_ctx *ctx = mtk_jpeg_fh_to_ctx(priv);

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		s->r.width = ctx->out_q.w;
		s->r.height = ctx->out_q.h;
		s->r.left = 0;
		s->r.top = 0;
		break;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_PADDED:
		s->r.width = ctx->cap_q.w;
		s->r.height = ctx->cap_q.h;
		s->r.left = 0;
		s->r.top = 0;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int mtk_jpeg_s_selection(struct file *file, void *priv,
				struct v4l2_selection *s)
{
	struct mtk_jpeg_ctx *ctx = mtk_jpeg_fh_to_ctx(priv);

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = ctx->out_q.w;
		s->r.height = ctx->out_q.h;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int mtk_jpeg_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct v4l2_fh *fh = file->private_data;
	struct mtk_jpeg_ctx *ctx = mtk_jpeg_fh_to_ctx(priv);
	struct vb2_queue *vq;
	struct vb2_buffer *vb;
	struct mtk_jpeg_src_buf *jpeg_src_buf;

	if (buf->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		goto end;

	vq = v4l2_m2m_get_vq(fh->m2m_ctx, buf->type);
	if (buf->index >= vq->num_buffers) {
		dev_err(ctx->jpeg->dev, "buffer index out of range\n");
		return -EINVAL;
	}

	vb = vb2_get_buffer(vq, buf->index);
	jpeg_src_buf = mtk_jpeg_vb2_to_srcbuf(vb);
	jpeg_src_buf->flags = (buf->m.planes[0].bytesused == 0) ?
		MTK_JPEG_BUF_FLAGS_LAST_FRAME : MTK_JPEG_BUF_FLAGS_INIT;
end:
	return v4l2_m2m_qbuf(file, fh->m2m_ctx, buf);
}

static const struct v4l2_ioctl_ops mtk_jpeg_ioctl_ops = {
	.vidioc_querycap                = mtk_jpeg_querycap,
	.vidioc_enum_fmt_vid_cap	= mtk_jpeg_enum_fmt_vid_cap,
	.vidioc_enum_fmt_vid_out	= mtk_jpeg_enum_fmt_vid_out,
	.vidioc_try_fmt_vid_cap_mplane	= mtk_jpeg_try_fmt_vid_cap_mplane,
	.vidioc_try_fmt_vid_out_mplane	= mtk_jpeg_try_fmt_vid_out_mplane,
	.vidioc_g_fmt_vid_cap_mplane    = mtk_jpeg_g_fmt_vid_mplane,
	.vidioc_g_fmt_vid_out_mplane    = mtk_jpeg_g_fmt_vid_mplane,
	.vidioc_s_fmt_vid_cap_mplane    = mtk_jpeg_s_fmt_vid_cap_mplane,
	.vidioc_s_fmt_vid_out_mplane    = mtk_jpeg_s_fmt_vid_out_mplane,
	.vidioc_qbuf                    = mtk_jpeg_qbuf,
	.vidioc_subscribe_event         = mtk_jpeg_subscribe_event,
	.vidioc_g_selection		= mtk_jpeg_g_selection,
	.vidioc_s_selection		= mtk_jpeg_s_selection,

	.vidioc_create_bufs		= v4l2_m2m_ioctl_create_bufs,
	.vidioc_prepare_buf		= v4l2_m2m_ioctl_prepare_buf,
	.vidioc_reqbufs                 = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf                = v4l2_m2m_ioctl_querybuf,
	.vidioc_dqbuf                   = v4l2_m2m_ioctl_dqbuf,
	.vidioc_expbuf                  = v4l2_m2m_ioctl_expbuf,
	.vidioc_streamon                = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff               = v4l2_m2m_ioctl_streamoff,

	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

static int mtk_jpeg_queue_setup(struct vb2_queue *q,
				unsigned int *num_buffers,
				unsigned int *num_planes,
				unsigned int sizes[],
				struct device *alloc_ctxs[])
{
	struct mtk_jpeg_ctx *ctx = vb2_get_drv_priv(q);
	struct mtk_jpeg_q_data *q_data = NULL;
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;
	int i;

	v4l2_dbg(1, debug, &jpeg->v4l2_dev, "(%d) buf_req count=%u\n",
		 q->type, *num_buffers);

	q_data = mtk_jpeg_get_q_data(ctx, q->type);
	if (!q_data)
		return -EINVAL;

	*num_planes = q_data->fmt->colplanes;
	for (i = 0; i < q_data->fmt->colplanes; i++) {
		sizes[i] = q_data->sizeimage[i];
		v4l2_dbg(1, debug, &jpeg->v4l2_dev, "sizeimage[%d]=%u\n",
			 i, sizes[i]);
	}

	return 0;
}

static int mtk_jpeg_buf_prepare(struct vb2_buffer *vb)
{
	struct mtk_jpeg_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct mtk_jpeg_q_data *q_data = NULL;
	int i;

	q_data = mtk_jpeg_get_q_data(ctx, vb->vb2_queue->type);
	if (!q_data)
		return -EINVAL;

	for (i = 0; i < q_data->fmt->colplanes; i++)
		vb2_set_plane_payload(vb, i, q_data->sizeimage[i]);

	return 0;
}

static bool mtk_jpeg_check_resolution_change(struct mtk_jpeg_ctx *ctx,
					     struct mtk_jpeg_dec_param *param)
{
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;
	struct mtk_jpeg_q_data *q_data;

	q_data = &ctx->out_q;
	if (q_data->w != param->pic_w || q_data->h != param->pic_h) {
		v4l2_dbg(1, debug, &jpeg->v4l2_dev, "Picture size change\n");
		return true;
	}

	q_data = &ctx->cap_q;
	if (q_data->fmt != mtk_jpeg_find_format(ctx, param->dst_fourcc,
						MTK_JPEG_FMT_TYPE_CAPTURE)) {
		v4l2_dbg(1, debug, &jpeg->v4l2_dev, "format change\n");
		return true;
	}
	return false;
}

static void mtk_jpeg_set_queue_data(struct mtk_jpeg_ctx *ctx,
				    struct mtk_jpeg_dec_param *param)
{
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;
	struct mtk_jpeg_q_data *q_data;
	int i;

	q_data = &ctx->out_q;
	q_data->w = param->pic_w;
	q_data->h = param->pic_h;

	q_data = &ctx->cap_q;
	q_data->w = param->dec_w;
	q_data->h = param->dec_h;
	q_data->fmt = mtk_jpeg_find_format(ctx,
					   param->dst_fourcc,
					   MTK_JPEG_FMT_TYPE_CAPTURE);

	for (i = 0; i < q_data->fmt->colplanes; i++) {
		q_data->bytesperline[i] = param->mem_stride[i];
		q_data->sizeimage[i] = param->comp_size[i];
	}

	v4l2_dbg(1, debug, &jpeg->v4l2_dev,
		 "set_parse cap:%c%c%c%c pic(%u, %u), buf(%u, %u)\n",
		 (param->dst_fourcc & 0xff),
		 (param->dst_fourcc >>  8 & 0xff),
		 (param->dst_fourcc >> 16 & 0xff),
		 (param->dst_fourcc >> 24 & 0xff),
		 param->pic_w, param->pic_h,
		 param->dec_w, param->dec_h);
}

static void mtk_jpeg_buf_queue(struct vb2_buffer *vb)
{
	struct mtk_jpeg_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct mtk_jpeg_dec_param *param;
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;
	struct mtk_jpeg_src_buf *jpeg_src_buf;
	bool header_valid;

	v4l2_dbg(2, debug, &jpeg->v4l2_dev, "(%d) buf_q id=%d, vb=%p\n",
		 vb->vb2_queue->type, vb->index, vb);

	if (vb->vb2_queue->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		goto end;

	jpeg_src_buf = mtk_jpeg_vb2_to_srcbuf(vb);
	param = &jpeg_src_buf->dec_param;
	memset(param, 0, sizeof(*param));

	if (jpeg_src_buf->flags & MTK_JPEG_BUF_FLAGS_LAST_FRAME) {
		v4l2_dbg(1, debug, &jpeg->v4l2_dev, "Got eos\n");
		goto end;
	}
	header_valid = mtk_jpeg_parse(param, (u8 *)vb2_plane_vaddr(vb, 0),
				      vb2_get_plane_payload(vb, 0));
	if (!header_valid) {
		v4l2_err(&jpeg->v4l2_dev, "Header invalid.\n");
		vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
		return;
	}

	if (ctx->state == MTK_JPEG_INIT) {
		struct vb2_queue *dst_vq = v4l2_m2m_get_vq(
			ctx->fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

		mtk_jpeg_queue_src_chg_event(ctx);
		mtk_jpeg_set_queue_data(ctx, param);
		ctx->state = vb2_is_streaming(dst_vq) ?
				MTK_JPEG_SOURCE_CHANGE : MTK_JPEG_RUNNING;
	}
end:
	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, to_vb2_v4l2_buffer(vb));
}

static struct vb2_v4l2_buffer *mtk_jpeg_buf_remove(struct mtk_jpeg_ctx *ctx,
				 enum v4l2_buf_type type)
{
	if (V4L2_TYPE_IS_OUTPUT(type))
		return v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	else
		return v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
}

static int mtk_jpeg_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct mtk_jpeg_ctx *ctx = vb2_get_drv_priv(q);
	struct vb2_v4l2_buffer *vb;
	int ret = 0;

	ret = pm_runtime_get_sync(ctx->jpeg->dev);
	if (ret < 0)
		goto err;

	return 0;
err:
	while ((vb = mtk_jpeg_buf_remove(ctx, q->type)))
		v4l2_m2m_buf_done(vb, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void mtk_jpeg_stop_streaming(struct vb2_queue *q)
{
	struct mtk_jpeg_ctx *ctx = vb2_get_drv_priv(q);
	struct vb2_v4l2_buffer *vb;

	/*
	 * STREAMOFF is an acknowledgment for source change event.
	 * Before STREAMOFF, we still have to return the old resolution and
	 * subsampling. Update capture queue when the stream is off.
	 */
	if (ctx->state == MTK_JPEG_SOURCE_CHANGE &&
	    !V4L2_TYPE_IS_OUTPUT(q->type)) {
		struct mtk_jpeg_src_buf *src_buf;

		vb = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
		src_buf = mtk_jpeg_vb2_to_srcbuf(&vb->vb2_buf);
		mtk_jpeg_set_queue_data(ctx, &src_buf->dec_param);
		ctx->state = MTK_JPEG_RUNNING;
	} else if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		ctx->state = MTK_JPEG_INIT;
	}

	while ((vb = mtk_jpeg_buf_remove(ctx, q->type)))
		v4l2_m2m_buf_done(vb, VB2_BUF_STATE_ERROR);

	pm_runtime_put_sync(ctx->jpeg->dev);
}

static const struct vb2_ops mtk_jpeg_qops = {
	.queue_setup        = mtk_jpeg_queue_setup,
	.buf_prepare        = mtk_jpeg_buf_prepare,
	.buf_queue          = mtk_jpeg_buf_queue,
	.wait_prepare       = vb2_ops_wait_prepare,
	.wait_finish        = vb2_ops_wait_finish,
	.start_streaming    = mtk_jpeg_start_streaming,
	.stop_streaming     = mtk_jpeg_stop_streaming,
};

static void mtk_jpeg_set_dec_src(struct mtk_jpeg_ctx *ctx,
				 struct vb2_buffer *src_buf,
				 struct mtk_jpeg_bs *bs)
{
	bs->str_addr = vb2_dma_contig_plane_dma_addr(src_buf, 0);
	bs->end_addr = bs->str_addr +
			 mtk_jpeg_align(vb2_get_plane_payload(src_buf, 0), 16);
	bs->size = mtk_jpeg_align(vb2_plane_size(src_buf, 0), 128);
}

static int mtk_jpeg_set_dec_dst(struct mtk_jpeg_ctx *ctx,
				struct mtk_jpeg_dec_param *param,
				struct vb2_buffer *dst_buf,
				struct mtk_jpeg_fb *fb)
{
	int i;

	if (param->comp_num != dst_buf->num_planes) {
		dev_err(ctx->jpeg->dev, "plane number mismatch (%u != %u)\n",
			param->comp_num, dst_buf->num_planes);
		return -EINVAL;
	}

	for (i = 0; i < dst_buf->num_planes; i++) {
		if (vb2_plane_size(dst_buf, i) < param->comp_size[i]) {
			dev_err(ctx->jpeg->dev,
				"buffer size is underflow (%lu < %u)\n",
				vb2_plane_size(dst_buf, 0),
				param->comp_size[i]);
			return -EINVAL;
		}
		fb->plane_addr[i] = vb2_dma_contig_plane_dma_addr(dst_buf, i);
	}

	return 0;
}

static void mtk_jpeg_device_run(void *priv)
{
	struct mtk_jpeg_ctx *ctx = priv;
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	enum vb2_buffer_state buf_state = VB2_BUF_STATE_ERROR;
	unsigned long flags;
	struct mtk_jpeg_src_buf *jpeg_src_buf;
	struct mtk_jpeg_bs bs;
	struct mtk_jpeg_fb fb;
	int i;

	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	jpeg_src_buf = mtk_jpeg_vb2_to_srcbuf(&src_buf->vb2_buf);

	if (jpeg_src_buf->flags & MTK_JPEG_BUF_FLAGS_LAST_FRAME) {
		for (i = 0; i < dst_buf->vb2_buf.num_planes; i++)
			vb2_set_plane_payload(&dst_buf->vb2_buf, i, 0);
		buf_state = VB2_BUF_STATE_DONE;
		goto dec_end;
	}

	if (mtk_jpeg_check_resolution_change(ctx, &jpeg_src_buf->dec_param)) {
		mtk_jpeg_queue_src_chg_event(ctx);
		ctx->state = MTK_JPEG_SOURCE_CHANGE;
		v4l2_m2m_job_finish(jpeg->m2m_dev, ctx->fh.m2m_ctx);
		return;
	}

	mtk_jpeg_set_dec_src(ctx, &src_buf->vb2_buf, &bs);
	if (mtk_jpeg_set_dec_dst(ctx, &jpeg_src_buf->dec_param, &dst_buf->vb2_buf, &fb))
		goto dec_end;

	spin_lock_irqsave(&jpeg->hw_lock, flags);
	mtk_jpeg_dec_reset(jpeg->dec_reg_base);
	mtk_jpeg_dec_set_config(jpeg->dec_reg_base,
				&jpeg_src_buf->dec_param, &bs, &fb);

	mtk_jpeg_dec_start(jpeg->dec_reg_base);
	spin_unlock_irqrestore(&jpeg->hw_lock, flags);
	return;

dec_end:
	v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	v4l2_m2m_buf_done(src_buf, buf_state);
	v4l2_m2m_buf_done(dst_buf, buf_state);
	v4l2_m2m_job_finish(jpeg->m2m_dev, ctx->fh.m2m_ctx);
}

static int mtk_jpeg_job_ready(void *priv)
{
	struct mtk_jpeg_ctx *ctx = priv;

	return (ctx->state == MTK_JPEG_RUNNING) ? 1 : 0;
}

static const struct v4l2_m2m_ops mtk_jpeg_m2m_ops = {
	.device_run = mtk_jpeg_device_run,
	.job_ready  = mtk_jpeg_job_ready,
};

static int mtk_jpeg_queue_init(void *priv, struct vb2_queue *src_vq,
			       struct vb2_queue *dst_vq)
{
	struct mtk_jpeg_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_DMABUF | VB2_MMAP;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct mtk_jpeg_src_buf);
	src_vq->ops = &mtk_jpeg_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->jpeg->lock;
	src_vq->dev = ctx->jpeg->dev;
	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_DMABUF | VB2_MMAP;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &mtk_jpeg_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->jpeg->lock;
	dst_vq->dev = ctx->jpeg->dev;
	ret = vb2_queue_init(dst_vq);

	return ret;
}

static void mtk_jpeg_clk_on(struct mtk_jpeg_dev *jpeg)
{
	int ret;

	ret = mtk_smi_larb_get(jpeg->larb);
	if (ret)
		dev_err(jpeg->dev, "mtk_smi_larb_get larbvdec fail %d\n", ret);
	clk_prepare_enable(jpeg->clk_jdec_smi);
	clk_prepare_enable(jpeg->clk_jdec);
}

static void mtk_jpeg_clk_off(struct mtk_jpeg_dev *jpeg)
{
	clk_disable_unprepare(jpeg->clk_jdec);
	clk_disable_unprepare(jpeg->clk_jdec_smi);
	mtk_smi_larb_put(jpeg->larb);
}

static irqreturn_t mtk_jpeg_dec_irq(int irq, void *priv)
{
	struct mtk_jpeg_dev *jpeg = priv;
	struct mtk_jpeg_ctx *ctx;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	struct mtk_jpeg_src_buf *jpeg_src_buf;
	enum vb2_buffer_state buf_state = VB2_BUF_STATE_ERROR;
	u32	dec_irq_ret;
	u32 dec_ret;
	int i;

	dec_ret = mtk_jpeg_dec_get_int_status(jpeg->dec_reg_base);
	dec_irq_ret = mtk_jpeg_dec_enum_result(dec_ret);
	ctx = v4l2_m2m_get_curr_priv(jpeg->m2m_dev);
	if (!ctx) {
		v4l2_err(&jpeg->v4l2_dev, "Context is NULL\n");
		return IRQ_HANDLED;
	}

	src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	jpeg_src_buf = mtk_jpeg_vb2_to_srcbuf(&src_buf->vb2_buf);

	if (dec_irq_ret >= MTK_JPEG_DEC_RESULT_UNDERFLOW)
		mtk_jpeg_dec_reset(jpeg->dec_reg_base);

	if (dec_irq_ret != MTK_JPEG_DEC_RESULT_EOF_DONE) {
		dev_err(jpeg->dev, "decode failed\n");
		goto dec_end;
	}

	for (i = 0; i < dst_buf->vb2_buf.num_planes; i++)
		vb2_set_plane_payload(&dst_buf->vb2_buf, i,
				      jpeg_src_buf->dec_param.comp_size[i]);

	buf_state = VB2_BUF_STATE_DONE;

dec_end:
	v4l2_m2m_buf_done(src_buf, buf_state);
	v4l2_m2m_buf_done(dst_buf, buf_state);
	v4l2_m2m_job_finish(jpeg->m2m_dev, ctx->fh.m2m_ctx);
	return IRQ_HANDLED;
}

static void mtk_jpeg_set_default_params(struct mtk_jpeg_ctx *ctx)
{
	struct mtk_jpeg_q_data *q = &ctx->out_q;
	int i;

	ctx->colorspace = V4L2_COLORSPACE_JPEG,
	ctx->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	ctx->quantization = V4L2_QUANTIZATION_DEFAULT;
	ctx->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	q->fmt = mtk_jpeg_find_format(ctx, V4L2_PIX_FMT_JPEG,
					      MTK_JPEG_FMT_TYPE_OUTPUT);
	q->w = MTK_JPEG_MIN_WIDTH;
	q->h = MTK_JPEG_MIN_HEIGHT;
	q->bytesperline[0] = 0;
	q->sizeimage[0] = MTK_JPEG_DEFAULT_SIZEIMAGE;

	q = &ctx->cap_q;
	q->fmt = mtk_jpeg_find_format(ctx, V4L2_PIX_FMT_YUV420M,
					      MTK_JPEG_FMT_TYPE_CAPTURE);
	q->w = MTK_JPEG_MIN_WIDTH;
	q->h = MTK_JPEG_MIN_HEIGHT;

	for (i = 0; i < q->fmt->colplanes; i++) {
		u32 stride = q->w * q->fmt->h_sample[i] / 4;
		u32 h = q->h * q->fmt->v_sample[i] / 4;

		q->bytesperline[i] = stride;
		q->sizeimage[i] = stride * h;
	}
}

static int mtk_jpeg_open(struct file *file)
{
	struct mtk_jpeg_dev *jpeg = video_drvdata(file);
	struct video_device *vfd = video_devdata(file);
	struct mtk_jpeg_ctx *ctx;
	int ret = 0;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (mutex_lock_interruptible(&jpeg->lock)) {
		ret = -ERESTARTSYS;
		goto free;
	}

	v4l2_fh_init(&ctx->fh, vfd);
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	ctx->jpeg = jpeg;
	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(jpeg->m2m_dev, ctx,
					    mtk_jpeg_queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto error;
	}

	mtk_jpeg_set_default_params(ctx);
	mutex_unlock(&jpeg->lock);
	return 0;

error:
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	mutex_unlock(&jpeg->lock);
free:
	kfree(ctx);
	return ret;
}

static int mtk_jpeg_release(struct file *file)
{
	struct mtk_jpeg_dev *jpeg = video_drvdata(file);
	struct mtk_jpeg_ctx *ctx = mtk_jpeg_fh_to_ctx(file->private_data);

	mutex_lock(&jpeg->lock);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
	mutex_unlock(&jpeg->lock);
	return 0;
}

static const struct v4l2_file_operations mtk_jpeg_fops = {
	.owner          = THIS_MODULE,
	.open           = mtk_jpeg_open,
	.release        = mtk_jpeg_release,
	.poll           = v4l2_m2m_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap           = v4l2_m2m_fop_mmap,
};

static int mtk_jpeg_clk_init(struct mtk_jpeg_dev *jpeg)
{
	struct device_node *node;
	struct platform_device *pdev;

	node = of_parse_phandle(jpeg->dev->of_node, "mediatek,larb", 0);
	if (!node)
		return -EINVAL;
	pdev = of_find_device_by_node(node);
	if (WARN_ON(!pdev)) {
		of_node_put(node);
		return -EINVAL;
	}
	of_node_put(node);

	jpeg->larb = &pdev->dev;

	jpeg->clk_jdec = devm_clk_get(jpeg->dev, "jpgdec");
	if (IS_ERR(jpeg->clk_jdec))
		return PTR_ERR(jpeg->clk_jdec);

	jpeg->clk_jdec_smi = devm_clk_get(jpeg->dev, "jpgdec-smi");
	return PTR_ERR_OR_ZERO(jpeg->clk_jdec_smi);
}

static int mtk_jpeg_probe(struct platform_device *pdev)
{
	struct mtk_jpeg_dev *jpeg;
	struct resource *res;
	int dec_irq;
	int ret;

	jpeg = devm_kzalloc(&pdev->dev, sizeof(*jpeg), GFP_KERNEL);
	if (!jpeg)
		return -ENOMEM;

	mutex_init(&jpeg->lock);
	spin_lock_init(&jpeg->hw_lock);
	jpeg->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	jpeg->dec_reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(jpeg->dec_reg_base)) {
		ret = PTR_ERR(jpeg->dec_reg_base);
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	dec_irq = platform_get_irq(pdev, 0);
	if (!res || dec_irq < 0) {
		dev_err(&pdev->dev, "Failed to get dec_irq %d.\n", dec_irq);
		ret = -EINVAL;
		return ret;
	}

	ret = devm_request_irq(&pdev->dev, dec_irq, mtk_jpeg_dec_irq, 0,
			       pdev->name, jpeg);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request dec_irq %d (%d)\n",
			dec_irq, ret);
		ret = -EINVAL;
		goto err_req_irq;
	}

	ret = mtk_jpeg_clk_init(jpeg);
	if (ret) {
		dev_err(&pdev->dev, "Failed to init clk, err %d\n", ret);
		goto err_clk_init;
	}

	ret = v4l2_device_register(&pdev->dev, &jpeg->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register v4l2 device\n");
		ret = -EINVAL;
		goto err_dev_register;
	}

	jpeg->m2m_dev = v4l2_m2m_init(&mtk_jpeg_m2m_ops);
	if (IS_ERR(jpeg->m2m_dev)) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(jpeg->m2m_dev);
		goto err_m2m_init;
	}

	jpeg->dec_vdev = video_device_alloc();
	if (!jpeg->dec_vdev) {
		ret = -ENOMEM;
		goto err_dec_vdev_alloc;
	}
	snprintf(jpeg->dec_vdev->name, sizeof(jpeg->dec_vdev->name),
		 "%s-dec", MTK_JPEG_NAME);
	jpeg->dec_vdev->fops = &mtk_jpeg_fops;
	jpeg->dec_vdev->ioctl_ops = &mtk_jpeg_ioctl_ops;
	jpeg->dec_vdev->minor = -1;
	jpeg->dec_vdev->release = video_device_release;
	jpeg->dec_vdev->lock = &jpeg->lock;
	jpeg->dec_vdev->v4l2_dev = &jpeg->v4l2_dev;
	jpeg->dec_vdev->vfl_dir = VFL_DIR_M2M;
	jpeg->dec_vdev->device_caps = V4L2_CAP_STREAMING |
				      V4L2_CAP_VIDEO_M2M_MPLANE;

	ret = video_register_device(jpeg->dec_vdev, VFL_TYPE_VIDEO, 3);
	if (ret) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to register video device\n");
		goto err_dec_vdev_register;
	}

	video_set_drvdata(jpeg->dec_vdev, jpeg);
	v4l2_info(&jpeg->v4l2_dev,
		  "decoder device registered as /dev/video%d (%d,%d)\n",
		  jpeg->dec_vdev->num, VIDEO_MAJOR, jpeg->dec_vdev->minor);

	platform_set_drvdata(pdev, jpeg);

	pm_runtime_enable(&pdev->dev);

	return 0;

err_dec_vdev_register:
	video_device_release(jpeg->dec_vdev);

err_dec_vdev_alloc:
	v4l2_m2m_release(jpeg->m2m_dev);

err_m2m_init:
	v4l2_device_unregister(&jpeg->v4l2_dev);

err_dev_register:

err_clk_init:

err_req_irq:

	return ret;
}

static int mtk_jpeg_remove(struct platform_device *pdev)
{
	struct mtk_jpeg_dev *jpeg = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	video_unregister_device(jpeg->dec_vdev);
	video_device_release(jpeg->dec_vdev);
	v4l2_m2m_release(jpeg->m2m_dev);
	v4l2_device_unregister(&jpeg->v4l2_dev);

	return 0;
}

static __maybe_unused int mtk_jpeg_pm_suspend(struct device *dev)
{
	struct mtk_jpeg_dev *jpeg = dev_get_drvdata(dev);

	mtk_jpeg_dec_reset(jpeg->dec_reg_base);
	mtk_jpeg_clk_off(jpeg);

	return 0;
}

static __maybe_unused int mtk_jpeg_pm_resume(struct device *dev)
{
	struct mtk_jpeg_dev *jpeg = dev_get_drvdata(dev);

	mtk_jpeg_clk_on(jpeg);
	mtk_jpeg_dec_reset(jpeg->dec_reg_base);

	return 0;
}

static __maybe_unused int mtk_jpeg_suspend(struct device *dev)
{
	int ret;

	if (pm_runtime_suspended(dev))
		return 0;

	ret = mtk_jpeg_pm_suspend(dev);
	return ret;
}

static __maybe_unused int mtk_jpeg_resume(struct device *dev)
{
	int ret;

	if (pm_runtime_suspended(dev))
		return 0;

	ret = mtk_jpeg_pm_resume(dev);

	return ret;
}

static const struct dev_pm_ops mtk_jpeg_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mtk_jpeg_suspend, mtk_jpeg_resume)
	SET_RUNTIME_PM_OPS(mtk_jpeg_pm_suspend, mtk_jpeg_pm_resume, NULL)
};

static const struct of_device_id mtk_jpeg_match[] = {
	{
		.compatible = "mediatek,mt8173-jpgdec",
		.data       = NULL,
	},
	{
		.compatible = "mediatek,mt2701-jpgdec",
		.data       = NULL,
	},
	{},
};

MODULE_DEVICE_TABLE(of, mtk_jpeg_match);

static struct platform_driver mtk_jpeg_driver = {
	.probe = mtk_jpeg_probe,
	.remove = mtk_jpeg_remove,
	.driver = {
		.name           = MTK_JPEG_NAME,
		.of_match_table = mtk_jpeg_match,
		.pm             = &mtk_jpeg_pm_ops,
	},
};

module_platform_driver(mtk_jpeg_driver);

MODULE_DESCRIPTION("MediaTek JPEG codec driver");
MODULE_LICENSE("GPL v2");
