// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Ming Hsiu Tsai <minghsiu.tsai@mediatek.com>
 *         Rick Chang <rick.chang@mediatek.com>
 *         Xia Jiang <xia.jiang@mediatek.com>
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

#include "mtk_jpeg_enc_hw.h"
#include "mtk_jpeg_dec_hw.h"
#include "mtk_jpeg_core.h"
#include "mtk_jpeg_dec_parse.h"

static struct mtk_jpeg_fmt mtk_jpeg_enc_formats[] = {
	{
		.fourcc		= V4L2_PIX_FMT_JPEG,
		.colplanes	= 1,
		.flags		= MTK_JPEG_FMT_FLAG_CAPTURE,
	},
	{
		.fourcc		= V4L2_PIX_FMT_NV12M,
		.hw_format	= JPEG_ENC_YUV_FORMAT_NV12,
		.h_sample	= {4, 4},
		.v_sample	= {4, 2},
		.colplanes	= 2,
		.h_align	= 4,
		.v_align	= 4,
		.flags		= MTK_JPEG_FMT_FLAG_OUTPUT,
	},
	{
		.fourcc		= V4L2_PIX_FMT_NV21M,
		.hw_format	= JEPG_ENC_YUV_FORMAT_NV21,
		.h_sample	= {4, 4},
		.v_sample	= {4, 2},
		.colplanes	= 2,
		.h_align	= 4,
		.v_align	= 4,
		.flags		= MTK_JPEG_FMT_FLAG_OUTPUT,
	},
	{
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.hw_format	= JPEG_ENC_YUV_FORMAT_YUYV,
		.h_sample	= {8},
		.v_sample	= {4},
		.colplanes	= 1,
		.h_align	= 5,
		.v_align	= 3,
		.flags		= MTK_JPEG_FMT_FLAG_OUTPUT,
	},
	{
		.fourcc		= V4L2_PIX_FMT_YVYU,
		.hw_format	= JPEG_ENC_YUV_FORMAT_YVYU,
		.h_sample	= {8},
		.v_sample	= {4},
		.colplanes	= 1,
		.h_align	= 5,
		.v_align	= 3,
		.flags		= MTK_JPEG_FMT_FLAG_OUTPUT,
	},
};

static struct mtk_jpeg_fmt mtk_jpeg_dec_formats[] = {
	{
		.fourcc		= V4L2_PIX_FMT_JPEG,
		.colplanes	= 1,
		.flags		= MTK_JPEG_FMT_FLAG_OUTPUT,
	},
	{
		.fourcc		= V4L2_PIX_FMT_YUV420M,
		.h_sample	= {4, 2, 2},
		.v_sample	= {4, 2, 2},
		.colplanes	= 3,
		.h_align	= 5,
		.v_align	= 4,
		.flags		= MTK_JPEG_FMT_FLAG_CAPTURE,
	},
	{
		.fourcc		= V4L2_PIX_FMT_YUV422M,
		.h_sample	= {4, 2, 2},
		.v_sample	= {4, 4, 4},
		.colplanes	= 3,
		.h_align	= 5,
		.v_align	= 3,
		.flags		= MTK_JPEG_FMT_FLAG_CAPTURE,
	},
};

#define MTK_JPEG_ENC_NUM_FORMATS ARRAY_SIZE(mtk_jpeg_enc_formats)
#define MTK_JPEG_DEC_NUM_FORMATS ARRAY_SIZE(mtk_jpeg_dec_formats)
#define MTK_JPEG_MAX_RETRY_TIME 5000

enum {
	MTK_JPEG_BUF_FLAGS_INIT			= 0,
	MTK_JPEG_BUF_FLAGS_LAST_FRAME		= 1,
};

static int debug;
module_param(debug, int, 0644);

static inline struct mtk_jpeg_ctx *ctrl_to_ctx(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct mtk_jpeg_ctx, ctrl_hdl);
}

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

	strscpy(cap->driver, jpeg->variant->dev_name, sizeof(cap->driver));
	strscpy(cap->card, jpeg->variant->dev_name, sizeof(cap->card));

	return 0;
}

static int vidioc_jpeg_enc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mtk_jpeg_ctx *ctx = ctrl_to_ctx(ctrl);

	switch (ctrl->id) {
	case V4L2_CID_JPEG_RESTART_INTERVAL:
		ctx->restart_interval = ctrl->val;
		break;
	case V4L2_CID_JPEG_COMPRESSION_QUALITY:
		ctx->enc_quality = ctrl->val;
		break;
	case V4L2_CID_JPEG_ACTIVE_MARKER:
		ctx->enable_exif = ctrl->val & V4L2_JPEG_ACTIVE_MARKER_APP1;
		break;
	}

	return 0;
}

static const struct v4l2_ctrl_ops mtk_jpeg_enc_ctrl_ops = {
	.s_ctrl = vidioc_jpeg_enc_s_ctrl,
};

static int mtk_jpeg_enc_ctrls_setup(struct mtk_jpeg_ctx *ctx)
{
	const struct v4l2_ctrl_ops *ops = &mtk_jpeg_enc_ctrl_ops;
	struct v4l2_ctrl_handler *handler = &ctx->ctrl_hdl;

	v4l2_ctrl_handler_init(handler, 3);

	v4l2_ctrl_new_std(handler, ops, V4L2_CID_JPEG_RESTART_INTERVAL, 0, 100,
			  1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_JPEG_COMPRESSION_QUALITY, 48,
			  100, 1, 90);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_JPEG_ACTIVE_MARKER, 0,
			  V4L2_JPEG_ACTIVE_MARKER_APP1, 0, 0);

	if (handler->error) {
		v4l2_ctrl_handler_free(&ctx->ctrl_hdl);
		return handler->error;
	}

	v4l2_ctrl_handler_setup(&ctx->ctrl_hdl);

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
	struct mtk_jpeg_ctx *ctx = mtk_jpeg_fh_to_ctx(priv);
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;

	return mtk_jpeg_enum_fmt(jpeg->variant->formats,
				 jpeg->variant->num_formats, f,
				 MTK_JPEG_FMT_FLAG_CAPTURE);
}

static int mtk_jpeg_enum_fmt_vid_out(struct file *file, void *priv,
				     struct v4l2_fmtdesc *f)
{
	struct mtk_jpeg_ctx *ctx = mtk_jpeg_fh_to_ctx(priv);
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;

	return mtk_jpeg_enum_fmt(jpeg->variant->formats,
				 jpeg->variant->num_formats, f,
				 MTK_JPEG_FMT_FLAG_OUTPUT);
}

static struct mtk_jpeg_q_data *mtk_jpeg_get_q_data(struct mtk_jpeg_ctx *ctx,
						   enum v4l2_buf_type type)
{
	if (V4L2_TYPE_IS_OUTPUT(type))
		return &ctx->out_q;
	return &ctx->cap_q;
}

static struct mtk_jpeg_fmt *
mtk_jpeg_find_format(struct mtk_jpeg_fmt *mtk_jpeg_formats, int num_formats,
		     u32 pixelformat, unsigned int fmt_type)
{
	unsigned int k;
	struct mtk_jpeg_fmt *fmt;

	for (k = 0; k < num_formats; k++) {
		fmt = &mtk_jpeg_formats[k];

		if (fmt->fourcc == pixelformat && fmt->flags & fmt_type)
			return fmt;
	}

	return NULL;
}

static int mtk_jpeg_try_fmt_mplane(struct v4l2_pix_format_mplane *pix_mp,
				   struct mtk_jpeg_fmt *fmt)
{
	int i;

	pix_mp->field = V4L2_FIELD_NONE;

	pix_mp->num_planes = fmt->colplanes;
	pix_mp->pixelformat = fmt->fourcc;

	if (fmt->fourcc == V4L2_PIX_FMT_JPEG) {
		struct v4l2_plane_pix_format *pfmt = &pix_mp->plane_fmt[0];

		pix_mp->height = clamp(pix_mp->height, MTK_JPEG_MIN_HEIGHT,
				       MTK_JPEG_MAX_HEIGHT);
		pix_mp->width = clamp(pix_mp->width, MTK_JPEG_MIN_WIDTH,
				      MTK_JPEG_MAX_WIDTH);

		pfmt->bytesperline = 0;
		/* Source size must be aligned to 128 */
		pfmt->sizeimage = round_up(pfmt->sizeimage, 128);
		if (pfmt->sizeimage == 0)
			pfmt->sizeimage = MTK_JPEG_DEFAULT_SIZEIMAGE;
		return 0;
	}

	/* other fourcc */
	pix_mp->height = clamp(round_up(pix_mp->height, fmt->v_align),
			       MTK_JPEG_MIN_HEIGHT, MTK_JPEG_MAX_HEIGHT);
	pix_mp->width = clamp(round_up(pix_mp->width, fmt->h_align),
			      MTK_JPEG_MIN_WIDTH, MTK_JPEG_MAX_WIDTH);

	for (i = 0; i < fmt->colplanes; i++) {
		struct v4l2_plane_pix_format *pfmt = &pix_mp->plane_fmt[i];
		u32 stride = pix_mp->width * fmt->h_sample[i] / 4;
		u32 h = pix_mp->height * fmt->v_sample[i] / 4;

		pfmt->bytesperline = stride;
		pfmt->sizeimage = stride * h;
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

	pix_mp->width = q_data->pix_mp.width;
	pix_mp->height = q_data->pix_mp.height;
	pix_mp->field = V4L2_FIELD_NONE;
	pix_mp->pixelformat = q_data->fmt->fourcc;
	pix_mp->num_planes = q_data->fmt->colplanes;
	pix_mp->colorspace = q_data->pix_mp.colorspace;
	pix_mp->ycbcr_enc = q_data->pix_mp.ycbcr_enc;
	pix_mp->xfer_func = q_data->pix_mp.xfer_func;
	pix_mp->quantization = q_data->pix_mp.quantization;

	v4l2_dbg(1, debug, &jpeg->v4l2_dev, "(%d) g_fmt:%c%c%c%c wxh:%ux%u\n",
		 f->type,
		 (pix_mp->pixelformat & 0xff),
		 (pix_mp->pixelformat >>  8 & 0xff),
		 (pix_mp->pixelformat >> 16 & 0xff),
		 (pix_mp->pixelformat >> 24 & 0xff),
		 pix_mp->width, pix_mp->height);

	for (i = 0; i < pix_mp->num_planes; i++) {
		struct v4l2_plane_pix_format *pfmt = &pix_mp->plane_fmt[i];

		pfmt->bytesperline = q_data->pix_mp.plane_fmt[i].bytesperline;
		pfmt->sizeimage = q_data->pix_mp.plane_fmt[i].sizeimage;

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
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;
	struct mtk_jpeg_fmt *fmt;

	fmt = mtk_jpeg_find_format(jpeg->variant->formats,
				   jpeg->variant->num_formats,
				   f->fmt.pix_mp.pixelformat,
				   MTK_JPEG_FMT_FLAG_CAPTURE);
	if (!fmt)
		fmt = ctx->cap_q.fmt;

	v4l2_dbg(2, debug, &ctx->jpeg->v4l2_dev, "(%d) try_fmt:%c%c%c%c\n",
		 f->type,
		 (fmt->fourcc & 0xff),
		 (fmt->fourcc >>  8 & 0xff),
		 (fmt->fourcc >> 16 & 0xff),
		 (fmt->fourcc >> 24 & 0xff));

	if (ctx->state != MTK_JPEG_INIT) {
		mtk_jpeg_g_fmt_vid_mplane(file, priv, f);
		return 0;
	}

	return mtk_jpeg_try_fmt_mplane(&f->fmt.pix_mp, fmt);
}

static int mtk_jpeg_try_fmt_vid_out_mplane(struct file *file, void *priv,
					   struct v4l2_format *f)
{
	struct mtk_jpeg_ctx *ctx = mtk_jpeg_fh_to_ctx(priv);
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;
	struct mtk_jpeg_fmt *fmt;

	fmt = mtk_jpeg_find_format(jpeg->variant->formats,
				   jpeg->variant->num_formats,
				   f->fmt.pix_mp.pixelformat,
				   MTK_JPEG_FMT_FLAG_OUTPUT);
	if (!fmt)
		fmt = ctx->out_q.fmt;

	v4l2_dbg(2, debug, &ctx->jpeg->v4l2_dev, "(%d) try_fmt:%c%c%c%c\n",
		 f->type,
		 (fmt->fourcc & 0xff),
		 (fmt->fourcc >>  8 & 0xff),
		 (fmt->fourcc >> 16 & 0xff),
		 (fmt->fourcc >> 24 & 0xff));

	if (ctx->state != MTK_JPEG_INIT) {
		mtk_jpeg_g_fmt_vid_mplane(file, priv, f);
		return 0;
	}

	return mtk_jpeg_try_fmt_mplane(&f->fmt.pix_mp, fmt);
}

static int mtk_jpeg_s_fmt_mplane(struct mtk_jpeg_ctx *ctx,
				 struct v4l2_format *f, unsigned int fmt_type)
{
	struct vb2_queue *vq;
	struct mtk_jpeg_q_data *q_data = NULL;
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;
	int i;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = mtk_jpeg_get_q_data(ctx, f->type);

	if (vb2_is_busy(vq)) {
		v4l2_err(&jpeg->v4l2_dev, "queue busy\n");
		return -EBUSY;
	}

	q_data->fmt = mtk_jpeg_find_format(jpeg->variant->formats,
					   jpeg->variant->num_formats,
					   pix_mp->pixelformat, fmt_type);
	q_data->pix_mp.width = pix_mp->width;
	q_data->pix_mp.height = pix_mp->height;
	q_data->enc_crop_rect.width = pix_mp->width;
	q_data->enc_crop_rect.height = pix_mp->height;
	q_data->pix_mp.colorspace = V4L2_COLORSPACE_SRGB;
	q_data->pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_601;
	q_data->pix_mp.xfer_func = V4L2_XFER_FUNC_SRGB;
	q_data->pix_mp.quantization = V4L2_QUANTIZATION_FULL_RANGE;

	v4l2_dbg(1, debug, &jpeg->v4l2_dev, "(%d) s_fmt:%c%c%c%c wxh:%ux%u\n",
		 f->type,
		 (q_data->fmt->fourcc & 0xff),
		 (q_data->fmt->fourcc >>  8 & 0xff),
		 (q_data->fmt->fourcc >> 16 & 0xff),
		 (q_data->fmt->fourcc >> 24 & 0xff),
		 q_data->pix_mp.width, q_data->pix_mp.height);

	for (i = 0; i < q_data->fmt->colplanes; i++) {
		q_data->pix_mp.plane_fmt[i].bytesperline =
					pix_mp->plane_fmt[i].bytesperline;
		q_data->pix_mp.plane_fmt[i].sizeimage =
					pix_mp->plane_fmt[i].sizeimage;

		v4l2_dbg(1, debug, &jpeg->v4l2_dev,
			 "plane[%d] bpl=%u, size=%u\n",
			 i, q_data->pix_mp.plane_fmt[i].bytesperline,
			 q_data->pix_mp.plane_fmt[i].sizeimage);
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

	return mtk_jpeg_s_fmt_mplane(mtk_jpeg_fh_to_ctx(priv), f,
				     MTK_JPEG_FMT_FLAG_OUTPUT);
}

static int mtk_jpeg_s_fmt_vid_cap_mplane(struct file *file, void *priv,
					 struct v4l2_format *f)
{
	int ret;

	ret = mtk_jpeg_try_fmt_vid_cap_mplane(file, priv, f);
	if (ret)
		return ret;

	return mtk_jpeg_s_fmt_mplane(mtk_jpeg_fh_to_ctx(priv), f,
				     MTK_JPEG_FMT_FLAG_CAPTURE);
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
	}

	return v4l2_ctrl_subscribe_event(fh, sub);
}

static int mtk_jpeg_enc_g_selection(struct file *file, void *priv,
				    struct v4l2_selection *s)
{
	struct mtk_jpeg_ctx *ctx = mtk_jpeg_fh_to_ctx(priv);

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
		s->r = ctx->out_q.enc_crop_rect;
		break;
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		s->r.width = ctx->out_q.pix_mp.width;
		s->r.height = ctx->out_q.pix_mp.height;
		s->r.left = 0;
		s->r.top = 0;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int mtk_jpeg_dec_g_selection(struct file *file, void *priv,
				    struct v4l2_selection *s)
{
	struct mtk_jpeg_ctx *ctx = mtk_jpeg_fh_to_ctx(priv);

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		s->r.width = ctx->out_q.pix_mp.width;
		s->r.height = ctx->out_q.pix_mp.height;
		s->r.left = 0;
		s->r.top = 0;
		break;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_PADDED:
		s->r.width = ctx->cap_q.pix_mp.width;
		s->r.height = ctx->cap_q.pix_mp.height;
		s->r.left = 0;
		s->r.top = 0;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int mtk_jpeg_enc_s_selection(struct file *file, void *priv,
				    struct v4l2_selection *s)
{
	struct mtk_jpeg_ctx *ctx = mtk_jpeg_fh_to_ctx(priv);

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = min(s->r.width, ctx->out_q.pix_mp.width);
		s->r.height = min(s->r.height, ctx->out_q.pix_mp.height);
		ctx->out_q.enc_crop_rect = s->r;
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

	vb = vq->bufs[buf->index];
	jpeg_src_buf = mtk_jpeg_vb2_to_srcbuf(vb);
	jpeg_src_buf->bs_size = buf->m.planes[0].bytesused;

end:
	return v4l2_m2m_qbuf(file, fh->m2m_ctx, buf);
}

static const struct v4l2_ioctl_ops mtk_jpeg_enc_ioctl_ops = {
	.vidioc_querycap                = mtk_jpeg_querycap,
	.vidioc_enum_fmt_vid_cap	= mtk_jpeg_enum_fmt_vid_cap,
	.vidioc_enum_fmt_vid_out	= mtk_jpeg_enum_fmt_vid_out,
	.vidioc_try_fmt_vid_cap_mplane	= mtk_jpeg_try_fmt_vid_cap_mplane,
	.vidioc_try_fmt_vid_out_mplane	= mtk_jpeg_try_fmt_vid_out_mplane,
	.vidioc_g_fmt_vid_cap_mplane    = mtk_jpeg_g_fmt_vid_mplane,
	.vidioc_g_fmt_vid_out_mplane    = mtk_jpeg_g_fmt_vid_mplane,
	.vidioc_s_fmt_vid_cap_mplane    = mtk_jpeg_s_fmt_vid_cap_mplane,
	.vidioc_s_fmt_vid_out_mplane    = mtk_jpeg_s_fmt_vid_out_mplane,
	.vidioc_qbuf                    = v4l2_m2m_ioctl_qbuf,
	.vidioc_subscribe_event         = mtk_jpeg_subscribe_event,
	.vidioc_g_selection		= mtk_jpeg_enc_g_selection,
	.vidioc_s_selection		= mtk_jpeg_enc_s_selection,

	.vidioc_create_bufs		= v4l2_m2m_ioctl_create_bufs,
	.vidioc_prepare_buf		= v4l2_m2m_ioctl_prepare_buf,
	.vidioc_reqbufs                 = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf                = v4l2_m2m_ioctl_querybuf,
	.vidioc_dqbuf                   = v4l2_m2m_ioctl_dqbuf,
	.vidioc_expbuf                  = v4l2_m2m_ioctl_expbuf,
	.vidioc_streamon                = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff               = v4l2_m2m_ioctl_streamoff,

	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,

	.vidioc_encoder_cmd		= v4l2_m2m_ioctl_encoder_cmd,
	.vidioc_try_encoder_cmd		= v4l2_m2m_ioctl_try_encoder_cmd,
};

static const struct v4l2_ioctl_ops mtk_jpeg_dec_ioctl_ops = {
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
	.vidioc_g_selection		= mtk_jpeg_dec_g_selection,

	.vidioc_create_bufs		= v4l2_m2m_ioctl_create_bufs,
	.vidioc_prepare_buf		= v4l2_m2m_ioctl_prepare_buf,
	.vidioc_reqbufs                 = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf                = v4l2_m2m_ioctl_querybuf,
	.vidioc_dqbuf                   = v4l2_m2m_ioctl_dqbuf,
	.vidioc_expbuf                  = v4l2_m2m_ioctl_expbuf,
	.vidioc_streamon                = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff               = v4l2_m2m_ioctl_streamoff,

	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,

	.vidioc_decoder_cmd = v4l2_m2m_ioctl_decoder_cmd,
	.vidioc_try_decoder_cmd = v4l2_m2m_ioctl_try_decoder_cmd,
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

	if (*num_planes) {
		for (i = 0; i < *num_planes; i++)
			if (sizes[i] < q_data->pix_mp.plane_fmt[i].sizeimage)
				return -EINVAL;
		return 0;
	}

	*num_planes = q_data->fmt->colplanes;
	for (i = 0; i < q_data->fmt->colplanes; i++) {
		sizes[i] =  q_data->pix_mp.plane_fmt[i].sizeimage;
		v4l2_dbg(1, debug, &jpeg->v4l2_dev, "sizeimage[%d]=%u\n",
			 i, sizes[i]);
	}

	return 0;
}

static int mtk_jpeg_buf_prepare(struct vb2_buffer *vb)
{
	struct mtk_jpeg_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct mtk_jpeg_q_data *q_data = NULL;
	struct v4l2_plane_pix_format plane_fmt = {};
	int i;

	q_data = mtk_jpeg_get_q_data(ctx, vb->vb2_queue->type);
	if (!q_data)
		return -EINVAL;

	for (i = 0; i < q_data->fmt->colplanes; i++) {
		plane_fmt = q_data->pix_mp.plane_fmt[i];
		if (ctx->enable_exif &&
		    q_data->fmt->fourcc == V4L2_PIX_FMT_JPEG)
			vb2_set_plane_payload(vb, i, plane_fmt.sizeimage +
					      MTK_JPEG_MAX_EXIF_SIZE);
		else
			vb2_set_plane_payload(vb, i,  plane_fmt.sizeimage);
	}

	return 0;
}

static bool mtk_jpeg_check_resolution_change(struct mtk_jpeg_ctx *ctx,
					     struct mtk_jpeg_dec_param *param)
{
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;
	struct mtk_jpeg_q_data *q_data;

	q_data = &ctx->out_q;
	if (q_data->pix_mp.width != param->pic_w ||
	    q_data->pix_mp.height != param->pic_h) {
		v4l2_dbg(1, debug, &jpeg->v4l2_dev, "Picture size change\n");
		return true;
	}

	q_data = &ctx->cap_q;
	if (q_data->fmt !=
	    mtk_jpeg_find_format(jpeg->variant->formats,
				 jpeg->variant->num_formats, param->dst_fourcc,
				 MTK_JPEG_FMT_FLAG_CAPTURE)) {
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
	q_data->pix_mp.width = param->pic_w;
	q_data->pix_mp.height = param->pic_h;

	q_data = &ctx->cap_q;
	q_data->pix_mp.width = param->dec_w;
	q_data->pix_mp.height = param->dec_h;
	q_data->fmt = mtk_jpeg_find_format(jpeg->variant->formats,
					   jpeg->variant->num_formats,
					   param->dst_fourcc,
					   MTK_JPEG_FMT_FLAG_CAPTURE);

	for (i = 0; i < q_data->fmt->colplanes; i++) {
		q_data->pix_mp.plane_fmt[i].bytesperline = param->mem_stride[i];
		q_data->pix_mp.plane_fmt[i].sizeimage = param->comp_size[i];
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

static void mtk_jpeg_enc_buf_queue(struct vb2_buffer *vb)
{
	struct mtk_jpeg_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;

	v4l2_dbg(2, debug, &jpeg->v4l2_dev, "(%d) buf_q id=%d, vb=%p\n",
		 vb->vb2_queue->type, vb->index, vb);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, to_vb2_v4l2_buffer(vb));
}

static void mtk_jpeg_dec_buf_queue(struct vb2_buffer *vb)
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

static void mtk_jpeg_enc_stop_streaming(struct vb2_queue *q)
{
	struct mtk_jpeg_ctx *ctx = vb2_get_drv_priv(q);
	struct vb2_v4l2_buffer *vb;

	while ((vb = mtk_jpeg_buf_remove(ctx, q->type)))
		v4l2_m2m_buf_done(vb, VB2_BUF_STATE_ERROR);
}

static void mtk_jpeg_dec_stop_streaming(struct vb2_queue *q)
{
	struct mtk_jpeg_ctx *ctx = vb2_get_drv_priv(q);
	struct vb2_v4l2_buffer *vb;

	/*
	 * STREAMOFF is an acknowledgment for source change event.
	 * Before STREAMOFF, we still have to return the old resolution and
	 * subsampling. Update capture queue when the stream is off.
	 */
	if (ctx->state == MTK_JPEG_SOURCE_CHANGE &&
	    V4L2_TYPE_IS_CAPTURE(q->type)) {
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
}

static const struct vb2_ops mtk_jpeg_dec_qops = {
	.queue_setup        = mtk_jpeg_queue_setup,
	.buf_prepare        = mtk_jpeg_buf_prepare,
	.buf_queue          = mtk_jpeg_dec_buf_queue,
	.wait_prepare       = vb2_ops_wait_prepare,
	.wait_finish        = vb2_ops_wait_finish,
	.stop_streaming     = mtk_jpeg_dec_stop_streaming,
};

static const struct vb2_ops mtk_jpeg_enc_qops = {
	.queue_setup        = mtk_jpeg_queue_setup,
	.buf_prepare        = mtk_jpeg_buf_prepare,
	.buf_queue          = mtk_jpeg_enc_buf_queue,
	.wait_prepare       = vb2_ops_wait_prepare,
	.wait_finish        = vb2_ops_wait_finish,
	.stop_streaming     = mtk_jpeg_enc_stop_streaming,
};

static void mtk_jpeg_set_dec_src(struct mtk_jpeg_ctx *ctx,
				 struct vb2_buffer *src_buf,
				 struct mtk_jpeg_bs *bs)
{
	bs->str_addr = vb2_dma_contig_plane_dma_addr(src_buf, 0);
	bs->end_addr = bs->str_addr +
		       round_up(vb2_get_plane_payload(src_buf, 0), 16);
	bs->size = round_up(vb2_plane_size(src_buf, 0), 128);
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

static void mtk_jpeg_enc_device_run(void *priv)
{
	struct mtk_jpeg_ctx *ctx = priv;
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	enum vb2_buffer_state buf_state = VB2_BUF_STATE_ERROR;
	unsigned long flags;
	int ret;

	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	ret = pm_runtime_resume_and_get(jpeg->dev);
	if (ret < 0)
		goto enc_end;

	schedule_delayed_work(&jpeg->job_timeout_work,
			msecs_to_jiffies(MTK_JPEG_HW_TIMEOUT_MSEC));

	spin_lock_irqsave(&jpeg->hw_lock, flags);

	/*
	 * Resetting the hardware every frame is to ensure that all the
	 * registers are cleared. This is a hardware requirement.
	 */
	mtk_jpeg_enc_reset(jpeg->reg_base);

	mtk_jpeg_set_enc_src(ctx, jpeg->reg_base, &src_buf->vb2_buf);
	mtk_jpeg_set_enc_dst(ctx, jpeg->reg_base, &dst_buf->vb2_buf);
	mtk_jpeg_set_enc_params(ctx, jpeg->reg_base);
	mtk_jpeg_enc_start(jpeg->reg_base);
	spin_unlock_irqrestore(&jpeg->hw_lock, flags);
	return;

enc_end:
	v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	v4l2_m2m_buf_done(src_buf, buf_state);
	v4l2_m2m_buf_done(dst_buf, buf_state);
	v4l2_m2m_job_finish(jpeg->m2m_dev, ctx->fh.m2m_ctx);
}

static void mtk_jpeg_multicore_enc_device_run(void *priv)
{
	struct mtk_jpeg_ctx *ctx = priv;
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;

	queue_work(jpeg->workqueue, &ctx->jpeg_work);
}

static void mtk_jpeg_multicore_dec_device_run(void *priv)
{
	struct mtk_jpeg_ctx *ctx = priv;
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;

	queue_work(jpeg->workqueue, &ctx->jpeg_work);
}

static void mtk_jpeg_dec_device_run(void *priv)
{
	struct mtk_jpeg_ctx *ctx = priv;
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	enum vb2_buffer_state buf_state = VB2_BUF_STATE_ERROR;
	unsigned long flags;
	struct mtk_jpeg_src_buf *jpeg_src_buf;
	struct mtk_jpeg_bs bs;
	struct mtk_jpeg_fb fb;
	int ret;

	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	jpeg_src_buf = mtk_jpeg_vb2_to_srcbuf(&src_buf->vb2_buf);

	if (mtk_jpeg_check_resolution_change(ctx, &jpeg_src_buf->dec_param)) {
		mtk_jpeg_queue_src_chg_event(ctx);
		ctx->state = MTK_JPEG_SOURCE_CHANGE;
		v4l2_m2m_job_finish(jpeg->m2m_dev, ctx->fh.m2m_ctx);
		return;
	}

	ret = pm_runtime_resume_and_get(jpeg->dev);
	if (ret < 0)
		goto dec_end;

	schedule_delayed_work(&jpeg->job_timeout_work,
			      msecs_to_jiffies(MTK_JPEG_HW_TIMEOUT_MSEC));

	mtk_jpeg_set_dec_src(ctx, &src_buf->vb2_buf, &bs);
	if (mtk_jpeg_set_dec_dst(ctx, &jpeg_src_buf->dec_param, &dst_buf->vb2_buf, &fb))
		goto dec_end;

	spin_lock_irqsave(&jpeg->hw_lock, flags);
	mtk_jpeg_dec_reset(jpeg->reg_base);
	mtk_jpeg_dec_set_config(jpeg->reg_base,
				&jpeg_src_buf->dec_param,
				jpeg_src_buf->bs_size,
				&bs,
				&fb);
	mtk_jpeg_dec_start(jpeg->reg_base);
	spin_unlock_irqrestore(&jpeg->hw_lock, flags);
	return;

dec_end:
	v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	v4l2_m2m_buf_done(src_buf, buf_state);
	v4l2_m2m_buf_done(dst_buf, buf_state);
	v4l2_m2m_job_finish(jpeg->m2m_dev, ctx->fh.m2m_ctx);
}

static int mtk_jpeg_dec_job_ready(void *priv)
{
	struct mtk_jpeg_ctx *ctx = priv;

	return (ctx->state == MTK_JPEG_RUNNING) ? 1 : 0;
}

static const struct v4l2_m2m_ops mtk_jpeg_enc_m2m_ops = {
	.device_run = mtk_jpeg_enc_device_run,
};

static const struct v4l2_m2m_ops mtk_jpeg_multicore_enc_m2m_ops = {
	.device_run = mtk_jpeg_multicore_enc_device_run,
};

static const struct v4l2_m2m_ops mtk_jpeg_multicore_dec_m2m_ops = {
	.device_run = mtk_jpeg_multicore_dec_device_run,
};

static const struct v4l2_m2m_ops mtk_jpeg_dec_m2m_ops = {
	.device_run = mtk_jpeg_dec_device_run,
	.job_ready  = mtk_jpeg_dec_job_ready,
};

static int mtk_jpeg_queue_init(void *priv, struct vb2_queue *src_vq,
			       struct vb2_queue *dst_vq)
{
	struct mtk_jpeg_ctx *ctx = priv;
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_DMABUF | VB2_MMAP;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct mtk_jpeg_src_buf);
	src_vq->ops = jpeg->variant->qops;
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
	dst_vq->ops = jpeg->variant->qops;
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

	ret = clk_bulk_prepare_enable(jpeg->variant->num_clks,
				      jpeg->variant->clks);
	if (ret)
		dev_err(jpeg->dev, "Failed to open jpeg clk: %d\n", ret);
}

static void mtk_jpeg_clk_off(struct mtk_jpeg_dev *jpeg)
{
	clk_bulk_disable_unprepare(jpeg->variant->num_clks,
				   jpeg->variant->clks);
}

static void mtk_jpeg_set_default_params(struct mtk_jpeg_ctx *ctx)
{
	struct mtk_jpeg_q_data *q = &ctx->out_q;
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;

	ctx->fh.ctrl_handler = &ctx->ctrl_hdl;
	q->pix_mp.colorspace = V4L2_COLORSPACE_SRGB;
	q->pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_601;
	q->pix_mp.quantization = V4L2_QUANTIZATION_FULL_RANGE;
	q->pix_mp.xfer_func = V4L2_XFER_FUNC_SRGB;

	q->fmt = mtk_jpeg_find_format(jpeg->variant->formats,
				      jpeg->variant->num_formats,
				      jpeg->variant->out_q_default_fourcc,
				      MTK_JPEG_FMT_FLAG_OUTPUT);
	q->pix_mp.width = MTK_JPEG_MIN_WIDTH;
	q->pix_mp.height = MTK_JPEG_MIN_HEIGHT;
	mtk_jpeg_try_fmt_mplane(&q->pix_mp, q->fmt);

	q = &ctx->cap_q;
	q->fmt = mtk_jpeg_find_format(jpeg->variant->formats,
				      jpeg->variant->num_formats,
				      jpeg->variant->cap_q_default_fourcc,
				      MTK_JPEG_FMT_FLAG_CAPTURE);
	q->pix_mp.colorspace = V4L2_COLORSPACE_SRGB;
	q->pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_601;
	q->pix_mp.quantization = V4L2_QUANTIZATION_FULL_RANGE;
	q->pix_mp.xfer_func = V4L2_XFER_FUNC_SRGB;
	q->pix_mp.width = MTK_JPEG_MIN_WIDTH;
	q->pix_mp.height = MTK_JPEG_MIN_HEIGHT;

	mtk_jpeg_try_fmt_mplane(&q->pix_mp, q->fmt);
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

	INIT_WORK(&ctx->jpeg_work, jpeg->variant->jpeg_worker);
	INIT_LIST_HEAD(&ctx->dst_done_queue);
	spin_lock_init(&ctx->done_queue_lock);
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

	if (jpeg->variant->cap_q_default_fourcc == V4L2_PIX_FMT_JPEG) {
		ret = mtk_jpeg_enc_ctrls_setup(ctx);
		if (ret) {
			v4l2_err(&jpeg->v4l2_dev, "Failed to setup jpeg enc controls\n");
			goto error;
		}
	} else {
		v4l2_ctrl_handler_init(&ctx->ctrl_hdl, 0);
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
	v4l2_ctrl_handler_free(&ctx->ctrl_hdl);
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

static void mtk_jpeg_job_timeout_work(struct work_struct *work)
{
	struct mtk_jpeg_dev *jpeg = container_of(work, struct mtk_jpeg_dev,
						 job_timeout_work.work);
	struct mtk_jpeg_ctx *ctx;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;

	ctx = v4l2_m2m_get_curr_priv(jpeg->m2m_dev);
	src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

	jpeg->variant->hw_reset(jpeg->reg_base);

	pm_runtime_put(jpeg->dev);

	v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);
	v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_ERROR);
	v4l2_m2m_job_finish(jpeg->m2m_dev, ctx->fh.m2m_ctx);
}

static int mtk_jpeg_single_core_init(struct platform_device *pdev,
				     struct mtk_jpeg_dev *jpeg_dev)
{
	struct mtk_jpeg_dev *jpeg = jpeg_dev;
	int jpeg_irq, ret;

	INIT_DELAYED_WORK(&jpeg->job_timeout_work,
			  mtk_jpeg_job_timeout_work);

	jpeg->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(jpeg->reg_base)) {
		ret = PTR_ERR(jpeg->reg_base);
		return ret;
	}

	jpeg_irq = platform_get_irq(pdev, 0);
	if (jpeg_irq < 0)
		return jpeg_irq;

	ret = devm_request_irq(&pdev->dev,
			       jpeg_irq,
			       jpeg->variant->irq_handler,
			       0,
			       pdev->name, jpeg);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request jpeg_irq %d (%d)\n",
			jpeg_irq, ret);
		return ret;
	}

	ret = devm_clk_bulk_get(jpeg->dev,
				jpeg->variant->num_clks,
				jpeg->variant->clks);
	if (ret) {
		dev_err(&pdev->dev, "Failed to init clk\n");
		return ret;
	}

	return 0;
}

static int mtk_jpeg_probe(struct platform_device *pdev)
{
	struct mtk_jpeg_dev *jpeg;
	struct device_node *child;
	int num_child = 0;
	int ret;

	jpeg = devm_kzalloc(&pdev->dev, sizeof(*jpeg), GFP_KERNEL);
	if (!jpeg)
		return -ENOMEM;

	mutex_init(&jpeg->lock);
	spin_lock_init(&jpeg->hw_lock);
	jpeg->dev = &pdev->dev;
	jpeg->variant = of_device_get_match_data(jpeg->dev);

	ret = devm_of_platform_populate(&pdev->dev);
	if (ret) {
		v4l2_err(&jpeg->v4l2_dev, "Master of platform populate failed.");
		return -EINVAL;
	}

	if (!jpeg->variant->multi_core) {
		ret = mtk_jpeg_single_core_init(pdev, jpeg);
		if (ret) {
			v4l2_err(&jpeg->v4l2_dev, "mtk_jpeg_single_core_init failed.");
			return -EINVAL;
		}
	} else {
		init_waitqueue_head(&jpeg->hw_wq);

		for_each_child_of_node(pdev->dev.of_node, child)
			num_child++;

		atomic_set(&jpeg->hw_rdy, num_child);
		atomic_set(&jpeg->hw_index, 0);

		jpeg->workqueue = alloc_ordered_workqueue(MTK_JPEG_NAME,
							  WQ_MEM_RECLAIM
							  | WQ_FREEZABLE);
		if (!jpeg->workqueue)
			return -EINVAL;
	}

	ret = v4l2_device_register(&pdev->dev, &jpeg->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register v4l2 device\n");
		return -EINVAL;
	}

	jpeg->m2m_dev = v4l2_m2m_init(jpeg->variant->m2m_ops);

	if (IS_ERR(jpeg->m2m_dev)) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(jpeg->m2m_dev);
		goto err_m2m_init;
	}

	jpeg->vdev = video_device_alloc();
	if (!jpeg->vdev) {
		ret = -ENOMEM;
		goto err_vfd_jpeg_alloc;
	}
	snprintf(jpeg->vdev->name, sizeof(jpeg->vdev->name),
		 "%s", jpeg->variant->dev_name);
	jpeg->vdev->fops = &mtk_jpeg_fops;
	jpeg->vdev->ioctl_ops = jpeg->variant->ioctl_ops;
	jpeg->vdev->minor = -1;
	jpeg->vdev->release = video_device_release;
	jpeg->vdev->lock = &jpeg->lock;
	jpeg->vdev->v4l2_dev = &jpeg->v4l2_dev;
	jpeg->vdev->vfl_dir = VFL_DIR_M2M;
	jpeg->vdev->device_caps = V4L2_CAP_STREAMING |
				  V4L2_CAP_VIDEO_M2M_MPLANE;

	ret = video_register_device(jpeg->vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to register video device\n");
		goto err_vfd_jpeg_register;
	}

	video_set_drvdata(jpeg->vdev, jpeg);
	v4l2_info(&jpeg->v4l2_dev,
		  "%s device registered as /dev/video%d (%d,%d)\n",
		  jpeg->variant->dev_name, jpeg->vdev->num,
		  VIDEO_MAJOR, jpeg->vdev->minor);

	platform_set_drvdata(pdev, jpeg);

	pm_runtime_enable(&pdev->dev);

	return 0;

err_vfd_jpeg_register:
	video_device_release(jpeg->vdev);

err_vfd_jpeg_alloc:
	v4l2_m2m_release(jpeg->m2m_dev);

err_m2m_init:
	v4l2_device_unregister(&jpeg->v4l2_dev);

	return ret;
}

static void mtk_jpeg_remove(struct platform_device *pdev)
{
	struct mtk_jpeg_dev *jpeg = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	video_unregister_device(jpeg->vdev);
	v4l2_m2m_release(jpeg->m2m_dev);
	v4l2_device_unregister(&jpeg->v4l2_dev);
}

static __maybe_unused int mtk_jpeg_pm_suspend(struct device *dev)
{
	struct mtk_jpeg_dev *jpeg = dev_get_drvdata(dev);

	mtk_jpeg_clk_off(jpeg);

	return 0;
}

static __maybe_unused int mtk_jpeg_pm_resume(struct device *dev)
{
	struct mtk_jpeg_dev *jpeg = dev_get_drvdata(dev);

	mtk_jpeg_clk_on(jpeg);

	return 0;
}

static __maybe_unused int mtk_jpeg_suspend(struct device *dev)
{
	struct mtk_jpeg_dev *jpeg = dev_get_drvdata(dev);

	v4l2_m2m_suspend(jpeg->m2m_dev);
	return pm_runtime_force_suspend(dev);
}

static __maybe_unused int mtk_jpeg_resume(struct device *dev)
{
	struct mtk_jpeg_dev *jpeg = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret < 0)
		return ret;

	v4l2_m2m_resume(jpeg->m2m_dev);
	return ret;
}

static const struct dev_pm_ops mtk_jpeg_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mtk_jpeg_suspend, mtk_jpeg_resume)
	SET_RUNTIME_PM_OPS(mtk_jpeg_pm_suspend, mtk_jpeg_pm_resume, NULL)
};

static int mtk_jpegenc_get_hw(struct mtk_jpeg_ctx *ctx)
{
	struct mtk_jpegenc_comp_dev *comp_jpeg;
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;
	unsigned long flags;
	int hw_id = -1;
	int i;

	spin_lock_irqsave(&jpeg->hw_lock, flags);
	for (i = 0; i < MTK_JPEGENC_HW_MAX; i++) {
		comp_jpeg = jpeg->enc_hw_dev[i];
		if (comp_jpeg->hw_state == MTK_JPEG_HW_IDLE) {
			hw_id = i;
			comp_jpeg->hw_state = MTK_JPEG_HW_BUSY;
			break;
		}
	}
	spin_unlock_irqrestore(&jpeg->hw_lock, flags);

	return hw_id;
}

static int mtk_jpegenc_set_hw_param(struct mtk_jpeg_ctx *ctx,
				    int hw_id,
				    struct vb2_v4l2_buffer *src_buf,
				    struct vb2_v4l2_buffer *dst_buf)
{
	struct mtk_jpegenc_comp_dev *jpeg = ctx->jpeg->enc_hw_dev[hw_id];

	jpeg->hw_param.curr_ctx = ctx;
	jpeg->hw_param.src_buffer = src_buf;
	jpeg->hw_param.dst_buffer = dst_buf;

	return 0;
}

static int mtk_jpegenc_put_hw(struct mtk_jpeg_dev *jpeg, int hw_id)
{
	unsigned long flags;

	spin_lock_irqsave(&jpeg->hw_lock, flags);
	jpeg->enc_hw_dev[hw_id]->hw_state = MTK_JPEG_HW_IDLE;
	spin_unlock_irqrestore(&jpeg->hw_lock, flags);

	return 0;
}

static int mtk_jpegdec_get_hw(struct mtk_jpeg_ctx *ctx)
{
	struct mtk_jpegdec_comp_dev *comp_jpeg;
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;
	unsigned long flags;
	int hw_id = -1;
	int i;

	spin_lock_irqsave(&jpeg->hw_lock, flags);
	for (i = 0; i < MTK_JPEGDEC_HW_MAX; i++) {
		comp_jpeg = jpeg->dec_hw_dev[i];
		if (comp_jpeg->hw_state == MTK_JPEG_HW_IDLE) {
			hw_id = i;
			comp_jpeg->hw_state = MTK_JPEG_HW_BUSY;
			break;
		}
	}
	spin_unlock_irqrestore(&jpeg->hw_lock, flags);

	return hw_id;
}

static int mtk_jpegdec_put_hw(struct mtk_jpeg_dev *jpeg, int hw_id)
{
	unsigned long flags;

	spin_lock_irqsave(&jpeg->hw_lock, flags);
	jpeg->dec_hw_dev[hw_id]->hw_state =
		MTK_JPEG_HW_IDLE;
	spin_unlock_irqrestore(&jpeg->hw_lock, flags);

	return 0;
}

static int mtk_jpegdec_set_hw_param(struct mtk_jpeg_ctx *ctx,
				    int hw_id,
				    struct vb2_v4l2_buffer *src_buf,
				    struct vb2_v4l2_buffer *dst_buf)
{
	struct mtk_jpegdec_comp_dev *jpeg =
		ctx->jpeg->dec_hw_dev[hw_id];

	jpeg->hw_param.curr_ctx = ctx;
	jpeg->hw_param.src_buffer = src_buf;
	jpeg->hw_param.dst_buffer = dst_buf;

	return 0;
}

static irqreturn_t mtk_jpeg_enc_done(struct mtk_jpeg_dev *jpeg)
{
	struct mtk_jpeg_ctx *ctx;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	enum vb2_buffer_state buf_state = VB2_BUF_STATE_ERROR;
	u32 result_size;

	ctx = v4l2_m2m_get_curr_priv(jpeg->m2m_dev);
	if (!ctx) {
		v4l2_err(&jpeg->v4l2_dev, "Context is NULL\n");
		return IRQ_HANDLED;
	}

	src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

	result_size = mtk_jpeg_enc_get_file_size(jpeg->reg_base);
	vb2_set_plane_payload(&dst_buf->vb2_buf, 0, result_size);

	buf_state = VB2_BUF_STATE_DONE;

	v4l2_m2m_buf_done(src_buf, buf_state);
	v4l2_m2m_buf_done(dst_buf, buf_state);
	v4l2_m2m_job_finish(jpeg->m2m_dev, ctx->fh.m2m_ctx);
	pm_runtime_put(ctx->jpeg->dev);
	return IRQ_HANDLED;
}

static void mtk_jpegenc_worker(struct work_struct *work)
{
	struct mtk_jpegenc_comp_dev *comp_jpeg[MTK_JPEGENC_HW_MAX];
	enum vb2_buffer_state buf_state = VB2_BUF_STATE_ERROR;
	struct mtk_jpeg_src_buf *jpeg_dst_buf;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	int ret, i, hw_id = 0;
	unsigned long flags;

	struct mtk_jpeg_ctx *ctx = container_of(work,
		struct mtk_jpeg_ctx,
		jpeg_work);
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;

	for (i = 0; i < MTK_JPEGENC_HW_MAX; i++)
		comp_jpeg[i] = jpeg->enc_hw_dev[i];
	i = 0;

retry_select:
	hw_id = mtk_jpegenc_get_hw(ctx);
	if (hw_id < 0) {
		ret = wait_event_interruptible(jpeg->hw_wq,
					       atomic_read(&jpeg->hw_rdy) > 0);
		if (ret != 0 || (i++ > MTK_JPEG_MAX_RETRY_TIME)) {
			dev_err(jpeg->dev, "%s : %d, all HW are busy\n",
				__func__, __LINE__);
			v4l2_m2m_job_finish(jpeg->m2m_dev, ctx->fh.m2m_ctx);
			return;
		}

		goto retry_select;
	}

	atomic_dec(&jpeg->hw_rdy);
	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	if (!src_buf)
		goto getbuf_fail;

	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	if (!dst_buf)
		goto getbuf_fail;

	v4l2_m2m_buf_copy_metadata(src_buf, dst_buf, true);

	mtk_jpegenc_set_hw_param(ctx, hw_id, src_buf, dst_buf);
	ret = pm_runtime_get_sync(comp_jpeg[hw_id]->dev);
	if (ret < 0) {
		dev_err(jpeg->dev, "%s : %d, pm_runtime_get_sync fail !!!\n",
			__func__, __LINE__);
		goto enc_end;
	}

	ret = clk_prepare_enable(comp_jpeg[hw_id]->venc_clk.clks->clk);
	if (ret) {
		dev_err(jpeg->dev, "%s : %d, jpegenc clk_prepare_enable fail\n",
			__func__, __LINE__);
		goto enc_end;
	}

	v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

	schedule_delayed_work(&comp_jpeg[hw_id]->job_timeout_work,
			      msecs_to_jiffies(MTK_JPEG_HW_TIMEOUT_MSEC));

	spin_lock_irqsave(&comp_jpeg[hw_id]->hw_lock, flags);
	jpeg_dst_buf = mtk_jpeg_vb2_to_srcbuf(&dst_buf->vb2_buf);
	jpeg_dst_buf->curr_ctx = ctx;
	jpeg_dst_buf->frame_num = ctx->total_frame_num;
	ctx->total_frame_num++;
	mtk_jpeg_enc_reset(comp_jpeg[hw_id]->reg_base);
	mtk_jpeg_set_enc_dst(ctx,
			     comp_jpeg[hw_id]->reg_base,
			     &dst_buf->vb2_buf);
	mtk_jpeg_set_enc_src(ctx,
			     comp_jpeg[hw_id]->reg_base,
			     &src_buf->vb2_buf);
	mtk_jpeg_set_enc_params(ctx, comp_jpeg[hw_id]->reg_base);
	mtk_jpeg_enc_start(comp_jpeg[hw_id]->reg_base);
	v4l2_m2m_job_finish(jpeg->m2m_dev, ctx->fh.m2m_ctx);
	spin_unlock_irqrestore(&comp_jpeg[hw_id]->hw_lock, flags);

	return;

enc_end:
	v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	v4l2_m2m_buf_done(src_buf, buf_state);
	v4l2_m2m_buf_done(dst_buf, buf_state);
getbuf_fail:
	atomic_inc(&jpeg->hw_rdy);
	mtk_jpegenc_put_hw(jpeg, hw_id);
	v4l2_m2m_job_finish(jpeg->m2m_dev, ctx->fh.m2m_ctx);
}

static void mtk_jpegdec_worker(struct work_struct *work)
{
	struct mtk_jpeg_ctx *ctx = container_of(work, struct mtk_jpeg_ctx,
		jpeg_work);
	struct mtk_jpegdec_comp_dev *comp_jpeg[MTK_JPEGDEC_HW_MAX];
	enum vb2_buffer_state buf_state = VB2_BUF_STATE_ERROR;
	struct mtk_jpeg_src_buf *jpeg_src_buf, *jpeg_dst_buf;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	struct mtk_jpeg_dev *jpeg = ctx->jpeg;
	int ret, i, hw_id = 0;
	struct mtk_jpeg_bs bs;
	struct mtk_jpeg_fb fb;
	unsigned long flags;

	for (i = 0; i < MTK_JPEGDEC_HW_MAX; i++)
		comp_jpeg[i] = jpeg->dec_hw_dev[i];
	i = 0;

retry_select:
	hw_id = mtk_jpegdec_get_hw(ctx);
	if (hw_id < 0) {
		ret = wait_event_interruptible_timeout(jpeg->hw_wq,
						       atomic_read(&jpeg->hw_rdy) > 0,
						       MTK_JPEG_HW_TIMEOUT_MSEC);
		if (ret != 0 || (i++ > MTK_JPEG_MAX_RETRY_TIME)) {
			dev_err(jpeg->dev, "%s : %d, all HW are busy\n",
				__func__, __LINE__);
			v4l2_m2m_job_finish(jpeg->m2m_dev, ctx->fh.m2m_ctx);
			return;
		}

		goto retry_select;
	}

	atomic_dec(&jpeg->hw_rdy);
	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	if (!src_buf)
		goto getbuf_fail;

	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	if (!dst_buf)
		goto getbuf_fail;

	v4l2_m2m_buf_copy_metadata(src_buf, dst_buf, true);
	jpeg_src_buf = mtk_jpeg_vb2_to_srcbuf(&src_buf->vb2_buf);
	jpeg_dst_buf = mtk_jpeg_vb2_to_srcbuf(&dst_buf->vb2_buf);

	if (mtk_jpeg_check_resolution_change(ctx,
					     &jpeg_src_buf->dec_param)) {
		mtk_jpeg_queue_src_chg_event(ctx);
		ctx->state = MTK_JPEG_SOURCE_CHANGE;
		goto getbuf_fail;
	}

	jpeg_src_buf->curr_ctx = ctx;
	jpeg_src_buf->frame_num = ctx->total_frame_num;
	jpeg_dst_buf->curr_ctx = ctx;
	jpeg_dst_buf->frame_num = ctx->total_frame_num;

	mtk_jpegdec_set_hw_param(ctx, hw_id, src_buf, dst_buf);
	ret = pm_runtime_get_sync(comp_jpeg[hw_id]->dev);
	if (ret < 0) {
		dev_err(jpeg->dev, "%s : %d, pm_runtime_get_sync fail !!!\n",
			__func__, __LINE__);
		goto dec_end;
	}

	ret = clk_prepare_enable(comp_jpeg[hw_id]->jdec_clk.clks->clk);
	if (ret) {
		dev_err(jpeg->dev, "%s : %d, jpegdec clk_prepare_enable fail\n",
			__func__, __LINE__);
		goto clk_end;
	}

	v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

	schedule_delayed_work(&comp_jpeg[hw_id]->job_timeout_work,
			      msecs_to_jiffies(MTK_JPEG_HW_TIMEOUT_MSEC));

	mtk_jpeg_set_dec_src(ctx, &src_buf->vb2_buf, &bs);
	if (mtk_jpeg_set_dec_dst(ctx,
				 &jpeg_src_buf->dec_param,
				 &dst_buf->vb2_buf, &fb)) {
		dev_err(jpeg->dev, "%s : %d, mtk_jpeg_set_dec_dst fail\n",
			__func__, __LINE__);
		goto setdst_end;
	}

	spin_lock_irqsave(&comp_jpeg[hw_id]->hw_lock, flags);
	ctx->total_frame_num++;
	mtk_jpeg_dec_reset(comp_jpeg[hw_id]->reg_base);
	mtk_jpeg_dec_set_config(comp_jpeg[hw_id]->reg_base,
				&jpeg_src_buf->dec_param,
				jpeg_src_buf->bs_size,
				&bs,
				&fb);
	mtk_jpeg_dec_start(comp_jpeg[hw_id]->reg_base);
	v4l2_m2m_job_finish(jpeg->m2m_dev, ctx->fh.m2m_ctx);
	spin_unlock_irqrestore(&comp_jpeg[hw_id]->hw_lock, flags);

	return;

setdst_end:
	clk_disable_unprepare(comp_jpeg[hw_id]->jdec_clk.clks->clk);
clk_end:
	pm_runtime_put(comp_jpeg[hw_id]->dev);
dec_end:
	v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	v4l2_m2m_buf_done(src_buf, buf_state);
	v4l2_m2m_buf_done(dst_buf, buf_state);
getbuf_fail:
	atomic_inc(&jpeg->hw_rdy);
	mtk_jpegdec_put_hw(jpeg, hw_id);
	v4l2_m2m_job_finish(jpeg->m2m_dev, ctx->fh.m2m_ctx);
}

static irqreturn_t mtk_jpeg_enc_irq(int irq, void *priv)
{
	struct mtk_jpeg_dev *jpeg = priv;
	u32 irq_status;
	irqreturn_t ret = IRQ_NONE;

	cancel_delayed_work(&jpeg->job_timeout_work);

	irq_status = readl(jpeg->reg_base + JPEG_ENC_INT_STS) &
		     JPEG_ENC_INT_STATUS_MASK_ALLIRQ;
	if (irq_status)
		writel(0, jpeg->reg_base + JPEG_ENC_INT_STS);

	if (!(irq_status & JPEG_ENC_INT_STATUS_DONE))
		return ret;

	ret = mtk_jpeg_enc_done(jpeg);
	return ret;
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

	cancel_delayed_work(&jpeg->job_timeout_work);

	dec_ret = mtk_jpeg_dec_get_int_status(jpeg->reg_base);
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
		mtk_jpeg_dec_reset(jpeg->reg_base);

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
	pm_runtime_put(ctx->jpeg->dev);
	return IRQ_HANDLED;
}

static struct clk_bulk_data mtk_jpeg_clocks[] = {
	{ .id = "jpgenc" },
};

static struct clk_bulk_data mt8173_jpeg_dec_clocks[] = {
	{ .id = "jpgdec-smi" },
	{ .id = "jpgdec" },
};

static const struct mtk_jpeg_variant mt8173_jpeg_drvdata = {
	.clks = mt8173_jpeg_dec_clocks,
	.num_clks = ARRAY_SIZE(mt8173_jpeg_dec_clocks),
	.formats = mtk_jpeg_dec_formats,
	.num_formats = MTK_JPEG_DEC_NUM_FORMATS,
	.qops = &mtk_jpeg_dec_qops,
	.irq_handler = mtk_jpeg_dec_irq,
	.hw_reset = mtk_jpeg_dec_reset,
	.m2m_ops = &mtk_jpeg_dec_m2m_ops,
	.dev_name = "mtk-jpeg-dec",
	.ioctl_ops = &mtk_jpeg_dec_ioctl_ops,
	.out_q_default_fourcc = V4L2_PIX_FMT_JPEG,
	.cap_q_default_fourcc = V4L2_PIX_FMT_YUV420M,
};

static const struct mtk_jpeg_variant mtk_jpeg_drvdata = {
	.clks = mtk_jpeg_clocks,
	.num_clks = ARRAY_SIZE(mtk_jpeg_clocks),
	.formats = mtk_jpeg_enc_formats,
	.num_formats = MTK_JPEG_ENC_NUM_FORMATS,
	.qops = &mtk_jpeg_enc_qops,
	.irq_handler = mtk_jpeg_enc_irq,
	.hw_reset = mtk_jpeg_enc_reset,
	.m2m_ops = &mtk_jpeg_enc_m2m_ops,
	.dev_name = "mtk-jpeg-enc",
	.ioctl_ops = &mtk_jpeg_enc_ioctl_ops,
	.out_q_default_fourcc = V4L2_PIX_FMT_YUYV,
	.cap_q_default_fourcc = V4L2_PIX_FMT_JPEG,
	.multi_core = false,
};

static struct mtk_jpeg_variant mtk8195_jpegenc_drvdata = {
	.formats = mtk_jpeg_enc_formats,
	.num_formats = MTK_JPEG_ENC_NUM_FORMATS,
	.qops = &mtk_jpeg_enc_qops,
	.m2m_ops = &mtk_jpeg_multicore_enc_m2m_ops,
	.dev_name = "mtk-jpeg-enc",
	.ioctl_ops = &mtk_jpeg_enc_ioctl_ops,
	.out_q_default_fourcc = V4L2_PIX_FMT_YUYV,
	.cap_q_default_fourcc = V4L2_PIX_FMT_JPEG,
	.multi_core = true,
	.jpeg_worker = mtk_jpegenc_worker,
};

static const struct mtk_jpeg_variant mtk8195_jpegdec_drvdata = {
	.formats = mtk_jpeg_dec_formats,
	.num_formats = MTK_JPEG_DEC_NUM_FORMATS,
	.qops = &mtk_jpeg_dec_qops,
	.m2m_ops = &mtk_jpeg_multicore_dec_m2m_ops,
	.dev_name = "mtk-jpeg-dec",
	.ioctl_ops = &mtk_jpeg_dec_ioctl_ops,
	.out_q_default_fourcc = V4L2_PIX_FMT_JPEG,
	.cap_q_default_fourcc = V4L2_PIX_FMT_YUV420M,
	.multi_core = true,
	.jpeg_worker = mtk_jpegdec_worker,
};

static const struct of_device_id mtk_jpeg_match[] = {
	{
		.compatible = "mediatek,mt8173-jpgdec",
		.data = &mt8173_jpeg_drvdata,
	},
	{
		.compatible = "mediatek,mt2701-jpgdec",
		.data = &mt8173_jpeg_drvdata,
	},
	{
		.compatible = "mediatek,mtk-jpgenc",
		.data = &mtk_jpeg_drvdata,
	},
	{
		.compatible = "mediatek,mt8195-jpgenc",
		.data = &mtk8195_jpegenc_drvdata,
	},
	{
		.compatible = "mediatek,mt8195-jpgdec",
		.data = &mtk8195_jpegdec_drvdata,
	},
	{},
};

MODULE_DEVICE_TABLE(of, mtk_jpeg_match);

static struct platform_driver mtk_jpeg_driver = {
	.probe = mtk_jpeg_probe,
	.remove_new = mtk_jpeg_remove,
	.driver = {
		.name           = MTK_JPEG_NAME,
		.of_match_table = mtk_jpeg_match,
		.pm             = &mtk_jpeg_pm_ops,
	},
};

module_platform_driver(mtk_jpeg_driver);

MODULE_DESCRIPTION("MediaTek JPEG codec driver");
MODULE_LICENSE("GPL v2");
