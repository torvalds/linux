// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2015
 * Authors: Yannick Fertre <yannick.fertre@st.com>
 *          Hugues Fruchet <hugues.fruchet@st.com>
 */

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>

#include "hva.h"
#include "hva-hw.h"

#define MIN_FRAMES	1
#define MIN_STREAMS	1

#define HVA_MIN_WIDTH	32
#define HVA_MAX_WIDTH	1920
#define HVA_MIN_HEIGHT	32
#define HVA_MAX_HEIGHT	1920

/* HVA requires a 16x16 pixels alignment for frames */
#define HVA_WIDTH_ALIGNMENT	16
#define HVA_HEIGHT_ALIGNMENT	16

#define HVA_DEFAULT_WIDTH	HVA_MIN_WIDTH
#define	HVA_DEFAULT_HEIGHT	HVA_MIN_HEIGHT
#define HVA_DEFAULT_FRAME_NUM	1
#define HVA_DEFAULT_FRAME_DEN	30

#define to_type_str(type) (type == V4L2_BUF_TYPE_VIDEO_OUTPUT ? \
			   "frame" : "stream")

#define fh_to_ctx(f)    (container_of(f, struct hva_ctx, fh))

/* registry of available encoders */
static const struct hva_enc *hva_encoders[] = {
	&nv12h264enc,
	&nv21h264enc,
};

static inline int frame_size(u32 w, u32 h, u32 fmt)
{
	switch (fmt) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		return (w * h * 3) / 2;
	default:
		return 0;
	}
}

static inline int frame_stride(u32 w, u32 fmt)
{
	switch (fmt) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		return w;
	default:
		return 0;
	}
}

static inline int frame_alignment(u32 fmt)
{
	switch (fmt) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		/* multiple of 2 */
		return 2;
	default:
		return 1;
	}
}

static inline int estimated_stream_size(u32 w, u32 h)
{
	/*
	 * HVA only encodes in YUV420 format, whatever the frame format.
	 * A compression ratio of 2 is assumed: thus, the maximum size
	 * of a stream is estimated to ((width x height x 3 / 2) / 2)
	 */
	return (w * h * 3) / 4;
}

static void set_default_params(struct hva_ctx *ctx)
{
	struct hva_frameinfo *frameinfo = &ctx->frameinfo;
	struct hva_streaminfo *streaminfo = &ctx->streaminfo;

	frameinfo->pixelformat = V4L2_PIX_FMT_NV12;
	frameinfo->width = HVA_DEFAULT_WIDTH;
	frameinfo->height = HVA_DEFAULT_HEIGHT;
	frameinfo->aligned_width = ALIGN(frameinfo->width,
					 HVA_WIDTH_ALIGNMENT);
	frameinfo->aligned_height = ALIGN(frameinfo->height,
					  HVA_HEIGHT_ALIGNMENT);
	frameinfo->size = frame_size(frameinfo->aligned_width,
				     frameinfo->aligned_height,
				     frameinfo->pixelformat);

	streaminfo->streamformat = V4L2_PIX_FMT_H264;
	streaminfo->width = HVA_DEFAULT_WIDTH;
	streaminfo->height = HVA_DEFAULT_HEIGHT;

	ctx->colorspace = V4L2_COLORSPACE_REC709;
	ctx->xfer_func = V4L2_XFER_FUNC_DEFAULT;
	ctx->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	ctx->quantization = V4L2_QUANTIZATION_DEFAULT;

	ctx->max_stream_size = estimated_stream_size(streaminfo->width,
						     streaminfo->height);
}

static const struct hva_enc *hva_find_encoder(struct hva_ctx *ctx,
					      u32 pixelformat,
					      u32 streamformat)
{
	struct hva_dev *hva = ctx_to_hdev(ctx);
	const struct hva_enc *enc;
	unsigned int i;

	for (i = 0; i < hva->nb_of_encoders; i++) {
		enc = hva->encoders[i];
		if ((enc->pixelformat == pixelformat) &&
		    (enc->streamformat == streamformat))
			return enc;
	}

	return NULL;
}

static void register_format(u32 format, u32 formats[], u32 *nb_of_formats)
{
	u32 i;
	bool found = false;

	for (i = 0; i < *nb_of_formats; i++) {
		if (format == formats[i]) {
			found = true;
			break;
		}
	}

	if (!found)
		formats[(*nb_of_formats)++] = format;
}

static void register_formats(struct hva_dev *hva)
{
	unsigned int i;

	for (i = 0; i < hva->nb_of_encoders; i++) {
		register_format(hva->encoders[i]->pixelformat,
				hva->pixelformats,
				&hva->nb_of_pixelformats);

		register_format(hva->encoders[i]->streamformat,
				hva->streamformats,
				&hva->nb_of_streamformats);
	}
}

static void register_encoders(struct hva_dev *hva)
{
	struct device *dev = hva_to_dev(hva);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(hva_encoders); i++) {
		if (hva->nb_of_encoders >= HVA_MAX_ENCODERS) {
			dev_dbg(dev,
				"%s failed to register %s encoder (%d maximum reached)\n",
				HVA_PREFIX, hva_encoders[i]->name,
				HVA_MAX_ENCODERS);
			return;
		}

		hva->encoders[hva->nb_of_encoders++] = hva_encoders[i];
		dev_info(dev, "%s %s encoder registered\n", HVA_PREFIX,
			 hva_encoders[i]->name);
	}
}

static int hva_open_encoder(struct hva_ctx *ctx, u32 streamformat,
			    u32 pixelformat, struct hva_enc **penc)
{
	struct hva_dev *hva = ctx_to_hdev(ctx);
	struct device *dev = ctx_to_dev(ctx);
	struct hva_enc *enc;
	int ret;

	/* find an encoder which can deal with these formats */
	enc = (struct hva_enc *)hva_find_encoder(ctx, pixelformat,
						 streamformat);
	if (!enc) {
		dev_err(dev, "%s no encoder found matching %4.4s => %4.4s\n",
			ctx->name, (char *)&pixelformat, (char *)&streamformat);
		return -EINVAL;
	}

	dev_dbg(dev, "%s one encoder matching %4.4s => %4.4s\n",
		ctx->name, (char *)&pixelformat, (char *)&streamformat);

	/* update instance name */
	snprintf(ctx->name, sizeof(ctx->name), "[%3d:%4.4s]",
		 hva->instance_id, (char *)&streamformat);

	/* open encoder instance */
	ret = enc->open(ctx);
	if (ret) {
		dev_err(dev, "%s failed to open encoder instance (%d)\n",
			ctx->name, ret);
		return ret;
	}

	dev_dbg(dev, "%s %s encoder opened\n", ctx->name, enc->name);

	*penc = enc;

	return ret;
}

static void hva_dbg_summary(struct hva_ctx *ctx)
{
	struct device *dev = ctx_to_dev(ctx);
	struct hva_streaminfo *stream = &ctx->streaminfo;
	struct hva_frameinfo *frame = &ctx->frameinfo;

	if (!(ctx->flags & HVA_FLAG_STREAMINFO))
		return;

	dev_dbg(dev, "%s %4.4s %dx%d > %4.4s %dx%d %s %s: %d frames encoded, %d system errors, %d encoding errors, %d frame errors\n",
		ctx->name,
		(char *)&frame->pixelformat,
		frame->aligned_width, frame->aligned_height,
		(char *)&stream->streamformat,
		stream->width, stream->height,
		stream->profile, stream->level,
		ctx->encoded_frames,
		ctx->sys_errors,
		ctx->encode_errors,
		ctx->frame_errors);
}

/*
 * V4L2 ioctl operations
 */

static int hva_querycap(struct file *file, void *priv,
			struct v4l2_capability *cap)
{
	struct hva_ctx *ctx = fh_to_ctx(file->private_data);
	struct hva_dev *hva = ctx_to_hdev(ctx);

	strscpy(cap->driver, HVA_NAME, sizeof(cap->driver));
	strscpy(cap->card, hva->vdev->name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 hva->pdev->name);

	return 0;
}

static int hva_enum_fmt_stream(struct file *file, void *priv,
			       struct v4l2_fmtdesc *f)
{
	struct hva_ctx *ctx = fh_to_ctx(file->private_data);
	struct hva_dev *hva = ctx_to_hdev(ctx);

	if (unlikely(f->index >= hva->nb_of_streamformats))
		return -EINVAL;

	f->pixelformat = hva->streamformats[f->index];

	return 0;
}

static int hva_enum_fmt_frame(struct file *file, void *priv,
			      struct v4l2_fmtdesc *f)
{
	struct hva_ctx *ctx = fh_to_ctx(file->private_data);
	struct hva_dev *hva = ctx_to_hdev(ctx);

	if (unlikely(f->index >= hva->nb_of_pixelformats))
		return -EINVAL;

	f->pixelformat = hva->pixelformats[f->index];

	return 0;
}

static int hva_g_fmt_stream(struct file *file, void *fh, struct v4l2_format *f)
{
	struct hva_ctx *ctx = fh_to_ctx(file->private_data);
	struct hva_streaminfo *streaminfo = &ctx->streaminfo;

	f->fmt.pix.width = streaminfo->width;
	f->fmt.pix.height = streaminfo->height;
	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.colorspace = ctx->colorspace;
	f->fmt.pix.xfer_func = ctx->xfer_func;
	f->fmt.pix.ycbcr_enc = ctx->ycbcr_enc;
	f->fmt.pix.quantization = ctx->quantization;
	f->fmt.pix.pixelformat = streaminfo->streamformat;
	f->fmt.pix.bytesperline = 0;
	f->fmt.pix.sizeimage = ctx->max_stream_size;

	return 0;
}

static int hva_g_fmt_frame(struct file *file, void *fh, struct v4l2_format *f)
{
	struct hva_ctx *ctx = fh_to_ctx(file->private_data);
	struct hva_frameinfo *frameinfo = &ctx->frameinfo;

	f->fmt.pix.width = frameinfo->width;
	f->fmt.pix.height = frameinfo->height;
	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.colorspace = ctx->colorspace;
	f->fmt.pix.xfer_func = ctx->xfer_func;
	f->fmt.pix.ycbcr_enc = ctx->ycbcr_enc;
	f->fmt.pix.quantization = ctx->quantization;
	f->fmt.pix.pixelformat = frameinfo->pixelformat;
	f->fmt.pix.bytesperline = frame_stride(frameinfo->aligned_width,
					       frameinfo->pixelformat);
	f->fmt.pix.sizeimage = frameinfo->size;

	return 0;
}

static int hva_try_fmt_stream(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct hva_ctx *ctx = fh_to_ctx(file->private_data);
	struct device *dev = ctx_to_dev(ctx);
	struct v4l2_pix_format *pix = &f->fmt.pix;
	u32 streamformat = pix->pixelformat;
	const struct hva_enc *enc;
	u32 width, height;
	u32 stream_size;

	enc = hva_find_encoder(ctx, ctx->frameinfo.pixelformat, streamformat);
	if (!enc) {
		dev_dbg(dev,
			"%s V4L2 TRY_FMT (CAPTURE): unsupported format %.4s\n",
			ctx->name, (char *)&pix->pixelformat);
		return -EINVAL;
	}

	width = pix->width;
	height = pix->height;
	if (ctx->flags & HVA_FLAG_FRAMEINFO) {
		/*
		 * if the frame resolution is already fixed, only allow the
		 * same stream resolution
		 */
		pix->width = ctx->frameinfo.width;
		pix->height = ctx->frameinfo.height;
		if ((pix->width != width) || (pix->height != height))
			dev_dbg(dev,
				"%s V4L2 TRY_FMT (CAPTURE): resolution updated %dx%d -> %dx%d to fit frame resolution\n",
				ctx->name, width, height,
				pix->width, pix->height);
	} else {
		/* adjust width & height */
		v4l_bound_align_image(&pix->width,
				      HVA_MIN_WIDTH, enc->max_width,
				      0,
				      &pix->height,
				      HVA_MIN_HEIGHT, enc->max_height,
				      0,
				      0);

		if ((pix->width != width) || (pix->height != height))
			dev_dbg(dev,
				"%s V4L2 TRY_FMT (CAPTURE): resolution updated %dx%d -> %dx%d to fit min/max/alignment\n",
				ctx->name, width, height,
				pix->width, pix->height);
	}

	stream_size = estimated_stream_size(pix->width, pix->height);
	if (pix->sizeimage < stream_size)
		pix->sizeimage = stream_size;

	pix->bytesperline = 0;
	pix->colorspace = ctx->colorspace;
	pix->xfer_func = ctx->xfer_func;
	pix->ycbcr_enc = ctx->ycbcr_enc;
	pix->quantization = ctx->quantization;
	pix->field = V4L2_FIELD_NONE;

	return 0;
}

static int hva_try_fmt_frame(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct hva_ctx *ctx = fh_to_ctx(file->private_data);
	struct device *dev = ctx_to_dev(ctx);
	struct v4l2_pix_format *pix = &f->fmt.pix;
	u32 pixelformat = pix->pixelformat;
	const struct hva_enc *enc;
	u32 width, height;

	enc = hva_find_encoder(ctx, pixelformat, ctx->streaminfo.streamformat);
	if (!enc) {
		dev_dbg(dev,
			"%s V4L2 TRY_FMT (OUTPUT): unsupported format %.4s\n",
			ctx->name, (char *)&pixelformat);
		return -EINVAL;
	}

	/* adjust width & height */
	width = pix->width;
	height = pix->height;
	v4l_bound_align_image(&pix->width,
			      HVA_MIN_WIDTH, HVA_MAX_WIDTH,
			      frame_alignment(pixelformat) - 1,
			      &pix->height,
			      HVA_MIN_HEIGHT, HVA_MAX_HEIGHT,
			      frame_alignment(pixelformat) - 1,
			      0);

	if ((pix->width != width) || (pix->height != height))
		dev_dbg(dev,
			"%s V4L2 TRY_FMT (OUTPUT): resolution updated %dx%d -> %dx%d to fit min/max/alignment\n",
			ctx->name, width, height, pix->width, pix->height);

	width = ALIGN(pix->width, HVA_WIDTH_ALIGNMENT);
	height = ALIGN(pix->height, HVA_HEIGHT_ALIGNMENT);

	if (!pix->colorspace) {
		pix->colorspace = V4L2_COLORSPACE_REC709;
		pix->xfer_func = V4L2_XFER_FUNC_DEFAULT;
		pix->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
		pix->quantization = V4L2_QUANTIZATION_DEFAULT;
	}

	pix->bytesperline = frame_stride(width, pixelformat);
	pix->sizeimage = frame_size(width, height, pixelformat);
	pix->field = V4L2_FIELD_NONE;

	return 0;
}

static int hva_s_fmt_stream(struct file *file, void *fh, struct v4l2_format *f)
{
	struct hva_ctx *ctx = fh_to_ctx(file->private_data);
	struct device *dev = ctx_to_dev(ctx);
	struct vb2_queue *vq;
	int ret;

	ret = hva_try_fmt_stream(file, fh, f);
	if (ret) {
		dev_dbg(dev, "%s V4L2 S_FMT (CAPTURE): unsupported format %.4s\n",
			ctx->name, (char *)&f->fmt.pix.pixelformat);
		return ret;
	}

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (vb2_is_streaming(vq)) {
		dev_dbg(dev, "%s V4L2 S_FMT (CAPTURE): queue busy\n",
			ctx->name);
		return -EBUSY;
	}

	ctx->max_stream_size = f->fmt.pix.sizeimage;
	ctx->streaminfo.width = f->fmt.pix.width;
	ctx->streaminfo.height = f->fmt.pix.height;
	ctx->streaminfo.streamformat = f->fmt.pix.pixelformat;
	ctx->flags |= HVA_FLAG_STREAMINFO;

	return 0;
}

static int hva_s_fmt_frame(struct file *file, void *fh, struct v4l2_format *f)
{
	struct hva_ctx *ctx = fh_to_ctx(file->private_data);
	struct device *dev = ctx_to_dev(ctx);
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct vb2_queue *vq;
	int ret;

	ret = hva_try_fmt_frame(file, fh, f);
	if (ret) {
		dev_dbg(dev, "%s V4L2 S_FMT (OUTPUT): unsupported format %.4s\n",
			ctx->name, (char *)&pix->pixelformat);
		return ret;
	}

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (vb2_is_streaming(vq)) {
		dev_dbg(dev, "%s V4L2 S_FMT (OUTPUT): queue busy\n", ctx->name);
		return -EBUSY;
	}

	ctx->colorspace = pix->colorspace;
	ctx->xfer_func = pix->xfer_func;
	ctx->ycbcr_enc = pix->ycbcr_enc;
	ctx->quantization = pix->quantization;

	ctx->frameinfo.aligned_width = ALIGN(pix->width, HVA_WIDTH_ALIGNMENT);
	ctx->frameinfo.aligned_height = ALIGN(pix->height,
					      HVA_HEIGHT_ALIGNMENT);
	ctx->frameinfo.size = pix->sizeimage;
	ctx->frameinfo.pixelformat = pix->pixelformat;
	ctx->frameinfo.width = pix->width;
	ctx->frameinfo.height = pix->height;
	ctx->flags |= HVA_FLAG_FRAMEINFO;

	return 0;
}

static int hva_g_parm(struct file *file, void *fh, struct v4l2_streamparm *sp)
{
	struct hva_ctx *ctx = fh_to_ctx(file->private_data);
	struct v4l2_fract *time_per_frame = &ctx->ctrls.time_per_frame;

	if (sp->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	sp->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	sp->parm.output.timeperframe.numerator = time_per_frame->numerator;
	sp->parm.output.timeperframe.denominator =
		time_per_frame->denominator;

	return 0;
}

static int hva_s_parm(struct file *file, void *fh, struct v4l2_streamparm *sp)
{
	struct hva_ctx *ctx = fh_to_ctx(file->private_data);
	struct v4l2_fract *time_per_frame = &ctx->ctrls.time_per_frame;

	if (sp->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	if (!sp->parm.output.timeperframe.numerator ||
	    !sp->parm.output.timeperframe.denominator)
		return hva_g_parm(file, fh, sp);

	sp->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	time_per_frame->numerator = sp->parm.output.timeperframe.numerator;
	time_per_frame->denominator =
		sp->parm.output.timeperframe.denominator;

	return 0;
}

static int hva_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct hva_ctx *ctx = fh_to_ctx(file->private_data);
	struct device *dev = ctx_to_dev(ctx);

	if (buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		/*
		 * depending on the targeted compressed video format, the
		 * capture buffer might contain headers (e.g. H.264 SPS/PPS)
		 * filled in by the driver client; the size of these data is
		 * copied from the bytesused field of the V4L2 buffer in the
		 * payload field of the hva stream buffer
		 */
		struct vb2_queue *vq;
		struct hva_stream *stream;
		struct vb2_buffer *vb2_buf;

		vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, buf->type);

		if (buf->index >= vq->num_buffers) {
			dev_dbg(dev, "%s buffer index %d out of range (%d)\n",
				ctx->name, buf->index, vq->num_buffers);
			return -EINVAL;
		}

		vb2_buf = vb2_get_buffer(vq, buf->index);
		stream = to_hva_stream(to_vb2_v4l2_buffer(vb2_buf));
		stream->bytesused = buf->bytesused;
	}

	return v4l2_m2m_qbuf(file, ctx->fh.m2m_ctx, buf);
}

/* V4L2 ioctl ops */
static const struct v4l2_ioctl_ops hva_ioctl_ops = {
	.vidioc_querycap		= hva_querycap,
	.vidioc_enum_fmt_vid_cap	= hva_enum_fmt_stream,
	.vidioc_enum_fmt_vid_out	= hva_enum_fmt_frame,
	.vidioc_g_fmt_vid_cap		= hva_g_fmt_stream,
	.vidioc_g_fmt_vid_out		= hva_g_fmt_frame,
	.vidioc_try_fmt_vid_cap		= hva_try_fmt_stream,
	.vidioc_try_fmt_vid_out		= hva_try_fmt_frame,
	.vidioc_s_fmt_vid_cap		= hva_s_fmt_stream,
	.vidioc_s_fmt_vid_out		= hva_s_fmt_frame,
	.vidioc_g_parm			= hva_g_parm,
	.vidioc_s_parm			= hva_s_parm,
	.vidioc_reqbufs			= v4l2_m2m_ioctl_reqbufs,
	.vidioc_create_bufs             = v4l2_m2m_ioctl_create_bufs,
	.vidioc_querybuf		= v4l2_m2m_ioctl_querybuf,
	.vidioc_expbuf			= v4l2_m2m_ioctl_expbuf,
	.vidioc_qbuf			= hva_qbuf,
	.vidioc_dqbuf			= v4l2_m2m_ioctl_dqbuf,
	.vidioc_streamon		= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff		= v4l2_m2m_ioctl_streamoff,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

/*
 * V4L2 control operations
 */

static int hva_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct hva_ctx *ctx = container_of(ctrl->handler, struct hva_ctx,
					   ctrl_handler);
	struct device *dev = ctx_to_dev(ctx);

	dev_dbg(dev, "%s S_CTRL: id = %d, val = %d\n", ctx->name,
		ctrl->id, ctrl->val);

	switch (ctrl->id) {
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		ctx->ctrls.bitrate_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		ctx->ctrls.gop_size = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		ctx->ctrls.bitrate = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_ASPECT:
		ctx->ctrls.aspect = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
		ctx->ctrls.profile = ctrl->val;
		snprintf(ctx->streaminfo.profile,
			 sizeof(ctx->streaminfo.profile),
			 "%s profile",
			 v4l2_ctrl_get_menu(ctrl->id)[ctrl->val]);
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
		ctx->ctrls.level = ctrl->val;
		snprintf(ctx->streaminfo.level,
			 sizeof(ctx->streaminfo.level),
			 "level %s",
			 v4l2_ctrl_get_menu(ctrl->id)[ctrl->val]);
		break;
	case V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE:
		ctx->ctrls.entropy_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_CPB_SIZE:
		ctx->ctrls.cpb_size = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM:
		ctx->ctrls.dct8x8 = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_MIN_QP:
		ctx->ctrls.qpmin = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_MAX_QP:
		ctx->ctrls.qpmax = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_ENABLE:
		ctx->ctrls.vui_sar = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_IDC:
		ctx->ctrls.vui_sar_idc = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_SEI_FRAME_PACKING:
		ctx->ctrls.sei_fp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE:
		ctx->ctrls.sei_fp_type = ctrl->val;
		break;
	default:
		dev_dbg(dev, "%s S_CTRL: invalid control (id = %d)\n",
			ctx->name, ctrl->id);
		return -EINVAL;
	}

	return 0;
}

/* V4L2 control ops */
static const struct v4l2_ctrl_ops hva_ctrl_ops = {
	.s_ctrl = hva_s_ctrl,
};

static int hva_ctrls_setup(struct hva_ctx *ctx)
{
	struct device *dev = ctx_to_dev(ctx);
	u64 mask;
	enum v4l2_mpeg_video_h264_sei_fp_arrangement_type sei_fp_type =
		V4L2_MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE_TOP_BOTTOM;

	v4l2_ctrl_handler_init(&ctx->ctrl_handler, 15);

	v4l2_ctrl_new_std_menu(&ctx->ctrl_handler, &hva_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_BITRATE_MODE,
			       V4L2_MPEG_VIDEO_BITRATE_MODE_CBR,
			       0,
			       V4L2_MPEG_VIDEO_BITRATE_MODE_CBR);

	v4l2_ctrl_new_std(&ctx->ctrl_handler, &hva_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_GOP_SIZE,
			  1, 60, 1, 16);

	v4l2_ctrl_new_std(&ctx->ctrl_handler, &hva_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_BITRATE,
			  1000, 60000000, 1000, 20000000);

	mask = ~(1 << V4L2_MPEG_VIDEO_ASPECT_1x1);
	v4l2_ctrl_new_std_menu(&ctx->ctrl_handler, &hva_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_ASPECT,
			       V4L2_MPEG_VIDEO_ASPECT_1x1,
			       mask,
			       V4L2_MPEG_VIDEO_ASPECT_1x1);

	mask = ~((1 << V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE) |
		 (1 << V4L2_MPEG_VIDEO_H264_PROFILE_MAIN) |
		 (1 << V4L2_MPEG_VIDEO_H264_PROFILE_HIGH) |
		 (1 << V4L2_MPEG_VIDEO_H264_PROFILE_STEREO_HIGH));
	v4l2_ctrl_new_std_menu(&ctx->ctrl_handler, &hva_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_H264_PROFILE,
			       V4L2_MPEG_VIDEO_H264_PROFILE_STEREO_HIGH,
			       mask,
			       V4L2_MPEG_VIDEO_H264_PROFILE_HIGH);

	v4l2_ctrl_new_std_menu(&ctx->ctrl_handler, &hva_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_H264_LEVEL,
			       V4L2_MPEG_VIDEO_H264_LEVEL_4_2,
			       0,
			       V4L2_MPEG_VIDEO_H264_LEVEL_4_0);

	v4l2_ctrl_new_std_menu(&ctx->ctrl_handler, &hva_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE,
			       V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC,
			       0,
			       V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC);

	v4l2_ctrl_new_std(&ctx->ctrl_handler, &hva_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_CPB_SIZE,
			  1, 10000, 1, 3000);

	v4l2_ctrl_new_std(&ctx->ctrl_handler, &hva_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM,
			  0, 1, 1, 0);

	v4l2_ctrl_new_std(&ctx->ctrl_handler, &hva_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_MIN_QP,
			  0, 51, 1, 5);

	v4l2_ctrl_new_std(&ctx->ctrl_handler, &hva_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_MAX_QP,
			  0, 51, 1, 51);

	v4l2_ctrl_new_std(&ctx->ctrl_handler, &hva_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_ENABLE,
			  0, 1, 1, 1);

	mask = ~(1 << V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_1x1);
	v4l2_ctrl_new_std_menu(&ctx->ctrl_handler, &hva_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_IDC,
			       V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_1x1,
			       mask,
			       V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_1x1);

	v4l2_ctrl_new_std(&ctx->ctrl_handler, &hva_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_SEI_FRAME_PACKING,
			  0, 1, 1, 0);

	mask = ~(1 << sei_fp_type);
	v4l2_ctrl_new_std_menu(&ctx->ctrl_handler, &hva_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE,
			       sei_fp_type,
			       mask,
			       sei_fp_type);

	if (ctx->ctrl_handler.error) {
		int err = ctx->ctrl_handler.error;

		dev_dbg(dev, "%s controls setup failed (%d)\n",
			ctx->name, err);
		v4l2_ctrl_handler_free(&ctx->ctrl_handler);
		return err;
	}

	v4l2_ctrl_handler_setup(&ctx->ctrl_handler);

	/* set default time per frame */
	ctx->ctrls.time_per_frame.numerator = HVA_DEFAULT_FRAME_NUM;
	ctx->ctrls.time_per_frame.denominator = HVA_DEFAULT_FRAME_DEN;

	return 0;
}

/*
 * mem-to-mem operations
 */

static void hva_run_work(struct work_struct *work)
{
	struct hva_ctx *ctx = container_of(work, struct hva_ctx, run_work);
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	const struct hva_enc *enc = ctx->enc;
	struct hva_frame *frame;
	struct hva_stream *stream;
	int ret;

	/* protect instance against reentrancy */
	mutex_lock(&ctx->lock);

#ifdef CONFIG_VIDEO_STI_HVA_DEBUGFS
	hva_dbg_perf_begin(ctx);
#endif

	src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

	frame = to_hva_frame(src_buf);
	stream = to_hva_stream(dst_buf);
	frame->vbuf.sequence = ctx->frame_num++;

	ret = enc->encode(ctx, frame, stream);

	vb2_set_plane_payload(&dst_buf->vb2_buf, 0, stream->bytesused);
	if (ret) {
		v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);
		v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_ERROR);
	} else {
		/* propagate frame timestamp */
		dst_buf->vb2_buf.timestamp = src_buf->vb2_buf.timestamp;
		dst_buf->field = V4L2_FIELD_NONE;
		dst_buf->sequence = ctx->stream_num - 1;

		ctx->encoded_frames++;

#ifdef CONFIG_VIDEO_STI_HVA_DEBUGFS
		hva_dbg_perf_end(ctx, stream);
#endif

		v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);
		v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_DONE);
	}

	mutex_unlock(&ctx->lock);

	v4l2_m2m_job_finish(ctx->hva_dev->m2m_dev, ctx->fh.m2m_ctx);
}

static void hva_device_run(void *priv)
{
	struct hva_ctx *ctx = priv;
	struct hva_dev *hva = ctx_to_hdev(ctx);

	queue_work(hva->work_queue, &ctx->run_work);
}

static void hva_job_abort(void *priv)
{
	struct hva_ctx *ctx = priv;
	struct device *dev = ctx_to_dev(ctx);

	dev_dbg(dev, "%s aborting job\n", ctx->name);

	ctx->aborting = true;
}

static int hva_job_ready(void *priv)
{
	struct hva_ctx *ctx = priv;
	struct device *dev = ctx_to_dev(ctx);

	if (!v4l2_m2m_num_src_bufs_ready(ctx->fh.m2m_ctx)) {
		dev_dbg(dev, "%s job not ready: no frame buffers\n",
			ctx->name);
		return 0;
	}

	if (!v4l2_m2m_num_dst_bufs_ready(ctx->fh.m2m_ctx)) {
		dev_dbg(dev, "%s job not ready: no stream buffers\n",
			ctx->name);
		return 0;
	}

	if (ctx->aborting) {
		dev_dbg(dev, "%s job not ready: aborting\n", ctx->name);
		return 0;
	}

	return 1;
}

/* mem-to-mem ops */
static const struct v4l2_m2m_ops hva_m2m_ops = {
	.device_run	= hva_device_run,
	.job_abort	= hva_job_abort,
	.job_ready	= hva_job_ready,
};

/*
 * VB2 queue operations
 */

static int hva_queue_setup(struct vb2_queue *vq,
			   unsigned int *num_buffers, unsigned int *num_planes,
			   unsigned int sizes[], struct device *alloc_devs[])
{
	struct hva_ctx *ctx = vb2_get_drv_priv(vq);
	struct device *dev = ctx_to_dev(ctx);
	unsigned int size;

	dev_dbg(dev, "%s %s queue setup: num_buffers %d\n", ctx->name,
		to_type_str(vq->type), *num_buffers);

	size = vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT ?
		ctx->frameinfo.size : ctx->max_stream_size;

	if (*num_planes)
		return sizes[0] < size ? -EINVAL : 0;

	/* only one plane supported */
	*num_planes = 1;
	sizes[0] = size;

	return 0;
}

static int hva_buf_prepare(struct vb2_buffer *vb)
{
	struct hva_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct device *dev = ctx_to_dev(ctx);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		struct hva_frame *frame = to_hva_frame(vbuf);

		if (vbuf->field == V4L2_FIELD_ANY)
			vbuf->field = V4L2_FIELD_NONE;
		if (vbuf->field != V4L2_FIELD_NONE) {
			dev_dbg(dev,
				"%s frame[%d] prepare: %d field not supported\n",
				ctx->name, vb->index, vbuf->field);
			return -EINVAL;
		}

		if (!frame->prepared) {
			/* get memory addresses */
			frame->vaddr = vb2_plane_vaddr(&vbuf->vb2_buf, 0);
			frame->paddr = vb2_dma_contig_plane_dma_addr(
					&vbuf->vb2_buf, 0);
			frame->info = ctx->frameinfo;
			frame->prepared = true;

			dev_dbg(dev,
				"%s frame[%d] prepared; virt=%p, phy=%pad\n",
				ctx->name, vb->index,
				frame->vaddr, &frame->paddr);
		}
	} else {
		struct hva_stream *stream = to_hva_stream(vbuf);

		if (!stream->prepared) {
			/* get memory addresses */
			stream->vaddr = vb2_plane_vaddr(&vbuf->vb2_buf, 0);
			stream->paddr = vb2_dma_contig_plane_dma_addr(
					&vbuf->vb2_buf, 0);
			stream->size = vb2_plane_size(&vbuf->vb2_buf, 0);
			stream->prepared = true;

			dev_dbg(dev,
				"%s stream[%d] prepared; virt=%p, phy=%pad\n",
				ctx->name, vb->index,
				stream->vaddr, &stream->paddr);
		}
	}

	return 0;
}

static void hva_buf_queue(struct vb2_buffer *vb)
{
	struct hva_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	if (ctx->fh.m2m_ctx)
		v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static int hva_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct hva_ctx *ctx = vb2_get_drv_priv(vq);
	struct hva_dev *hva = ctx_to_hdev(ctx);
	struct device *dev = ctx_to_dev(ctx);
	struct vb2_v4l2_buffer *vbuf;
	int ret;
	unsigned int i;
	bool found = false;

	dev_dbg(dev, "%s %s start streaming\n", ctx->name,
		to_type_str(vq->type));

	/* open encoder when both start_streaming have been called */
	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		if (!vb2_start_streaming_called(&ctx->fh.m2m_ctx->cap_q_ctx.q))
			return 0;
	} else {
		if (!vb2_start_streaming_called(&ctx->fh.m2m_ctx->out_q_ctx.q))
			return 0;
	}

	/* store the instance context in the instances array */
	for (i = 0; i < HVA_MAX_INSTANCES; i++) {
		if (!hva->instances[i]) {
			hva->instances[i] = ctx;
			/* save the context identifier in the context */
			ctx->id = i;
			found = true;
			break;
		}
	}

	if (!found) {
		dev_err(dev, "%s maximum instances reached\n", ctx->name);
		ret = -ENOMEM;
		goto err;
	}

	hva->nb_of_instances++;

	if (!ctx->enc) {
		ret = hva_open_encoder(ctx,
				       ctx->streaminfo.streamformat,
				       ctx->frameinfo.pixelformat,
				       &ctx->enc);
		if (ret < 0)
			goto err_ctx;
	}

	return 0;

err_ctx:
	hva->instances[ctx->id] = NULL;
	hva->nb_of_instances--;
err:
	if (vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		/* return of all pending buffers to vb2 (in queued state) */
		while ((vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx)))
			v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_QUEUED);
	} else {
		/* return of all pending buffers to vb2 (in queued state) */
		while ((vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx)))
			v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_QUEUED);
	}

	ctx->sys_errors++;

	return ret;
}

static void hva_stop_streaming(struct vb2_queue *vq)
{
	struct hva_ctx *ctx = vb2_get_drv_priv(vq);
	struct hva_dev *hva = ctx_to_hdev(ctx);
	struct device *dev = ctx_to_dev(ctx);
	const struct hva_enc *enc = ctx->enc;
	struct vb2_v4l2_buffer *vbuf;

	dev_dbg(dev, "%s %s stop streaming\n", ctx->name,
		to_type_str(vq->type));

	if (vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		/* return of all pending buffers to vb2 (in error state) */
		ctx->frame_num = 0;
		while ((vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx)))
			v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
	} else {
		/* return of all pending buffers to vb2 (in error state) */
		ctx->stream_num = 0;
		while ((vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx)))
			v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
	}

	if ((V4L2_TYPE_IS_OUTPUT(vq->type) &&
	     vb2_is_streaming(&ctx->fh.m2m_ctx->cap_q_ctx.q)) ||
	    (V4L2_TYPE_IS_CAPTURE(vq->type) &&
	     vb2_is_streaming(&ctx->fh.m2m_ctx->out_q_ctx.q))) {
		dev_dbg(dev, "%s %s out=%d cap=%d\n",
			ctx->name, to_type_str(vq->type),
			vb2_is_streaming(&ctx->fh.m2m_ctx->out_q_ctx.q),
			vb2_is_streaming(&ctx->fh.m2m_ctx->cap_q_ctx.q));
		return;
	}

	/* close encoder when both stop_streaming have been called */
	if (enc) {
		dev_dbg(dev, "%s %s encoder closed\n", ctx->name, enc->name);
		enc->close(ctx);
		ctx->enc = NULL;

		/* clear instance context in instances array */
		hva->instances[ctx->id] = NULL;
		hva->nb_of_instances--;
	}

	ctx->aborting = false;
}

/* VB2 queue ops */
static const struct vb2_ops hva_qops = {
	.queue_setup		= hva_queue_setup,
	.buf_prepare		= hva_buf_prepare,
	.buf_queue		= hva_buf_queue,
	.start_streaming	= hva_start_streaming,
	.stop_streaming		= hva_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

/*
 * V4L2 file operations
 */

static int queue_init(struct hva_ctx *ctx, struct vb2_queue *vq)
{
	vq->io_modes = VB2_MMAP | VB2_DMABUF;
	vq->drv_priv = ctx;
	vq->ops = &hva_qops;
	vq->mem_ops = &vb2_dma_contig_memops;
	vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	vq->lock = &ctx->hva_dev->lock;

	return vb2_queue_init(vq);
}

static int hva_queue_init(void *priv, struct vb2_queue *src_vq,
			  struct vb2_queue *dst_vq)
{
	struct hva_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->buf_struct_size = sizeof(struct hva_frame);
	src_vq->min_buffers_needed = MIN_FRAMES;
	src_vq->dev = ctx->hva_dev->dev;

	ret = queue_init(ctx, src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->buf_struct_size = sizeof(struct hva_stream);
	dst_vq->min_buffers_needed = MIN_STREAMS;
	dst_vq->dev = ctx->hva_dev->dev;

	return queue_init(ctx, dst_vq);
}

static int hva_open(struct file *file)
{
	struct hva_dev *hva = video_drvdata(file);
	struct device *dev = hva_to_dev(hva);
	struct hva_ctx *ctx;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		ret = -ENOMEM;
		goto out;
	}
	ctx->hva_dev = hva;

	INIT_WORK(&ctx->run_work, hva_run_work);
	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	ret = hva_ctrls_setup(ctx);
	if (ret) {
		dev_err(dev, "%s [x:x] failed to setup controls\n",
			HVA_PREFIX);
		ctx->sys_errors++;
		goto err_fh;
	}
	ctx->fh.ctrl_handler = &ctx->ctrl_handler;

	mutex_init(&ctx->lock);

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(hva->m2m_dev, ctx,
					    &hva_queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		dev_err(dev, "%s failed to initialize m2m context (%d)\n",
			HVA_PREFIX, ret);
		ctx->sys_errors++;
		goto err_ctrls;
	}

	/* set the instance name */
	mutex_lock(&hva->lock);
	hva->instance_id++;
	snprintf(ctx->name, sizeof(ctx->name), "[%3d:----]",
		 hva->instance_id);
	mutex_unlock(&hva->lock);

	/* default parameters for frame and stream */
	set_default_params(ctx);

#ifdef CONFIG_VIDEO_STI_HVA_DEBUGFS
	hva_dbg_ctx_create(ctx);
#endif

	dev_info(dev, "%s encoder instance created\n", ctx->name);

	return 0;

err_ctrls:
	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
err_fh:
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
out:
	return ret;
}

static int hva_release(struct file *file)
{
	struct hva_ctx *ctx = fh_to_ctx(file->private_data);
	struct hva_dev *hva = ctx_to_hdev(ctx);
	struct device *dev = ctx_to_dev(ctx);
	const struct hva_enc *enc = ctx->enc;

	if (enc) {
		dev_dbg(dev, "%s %s encoder closed\n", ctx->name, enc->name);
		enc->close(ctx);
		ctx->enc = NULL;

		/* clear instance context in instances array */
		hva->instances[ctx->id] = NULL;
		hva->nb_of_instances--;
	}

	/* trace a summary of instance before closing (debug purpose) */
	hva_dbg_summary(ctx);

	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);

	v4l2_ctrl_handler_free(&ctx->ctrl_handler);

	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);

#ifdef CONFIG_VIDEO_STI_HVA_DEBUGFS
	hva_dbg_ctx_remove(ctx);
#endif

	dev_info(dev, "%s encoder instance released\n", ctx->name);

	kfree(ctx);

	return 0;
}

/* V4L2 file ops */
static const struct v4l2_file_operations hva_fops = {
	.owner			= THIS_MODULE,
	.open			= hva_open,
	.release		= hva_release,
	.unlocked_ioctl		= video_ioctl2,
	.mmap			= v4l2_m2m_fop_mmap,
	.poll			= v4l2_m2m_fop_poll,
};

/*
 * Platform device operations
 */

static int hva_register_device(struct hva_dev *hva)
{
	int ret;
	struct video_device *vdev;
	struct device *dev;

	if (!hva)
		return -ENODEV;
	dev = hva_to_dev(hva);

	hva->m2m_dev = v4l2_m2m_init(&hva_m2m_ops);
	if (IS_ERR(hva->m2m_dev)) {
		dev_err(dev, "%s failed to initialize v4l2-m2m device\n",
			HVA_PREFIX);
		ret = PTR_ERR(hva->m2m_dev);
		goto err;
	}

	vdev = video_device_alloc();
	if (!vdev) {
		dev_err(dev, "%s failed to allocate video device\n",
			HVA_PREFIX);
		ret = -ENOMEM;
		goto err_m2m_release;
	}

	vdev->fops = &hva_fops;
	vdev->ioctl_ops = &hva_ioctl_ops;
	vdev->release = video_device_release;
	vdev->lock = &hva->lock;
	vdev->vfl_dir = VFL_DIR_M2M;
	vdev->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M;
	vdev->v4l2_dev = &hva->v4l2_dev;
	snprintf(vdev->name, sizeof(vdev->name), "%s%lx", HVA_NAME,
		 hva->ip_version);

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(dev, "%s failed to register video device\n",
			HVA_PREFIX);
		goto err_vdev_release;
	}

	hva->vdev = vdev;
	video_set_drvdata(vdev, hva);
	return 0;

err_vdev_release:
	video_device_release(vdev);
err_m2m_release:
	v4l2_m2m_release(hva->m2m_dev);
err:
	return ret;
}

static void hva_unregister_device(struct hva_dev *hva)
{
	if (!hva)
		return;

	if (hva->m2m_dev)
		v4l2_m2m_release(hva->m2m_dev);

	video_unregister_device(hva->vdev);
}

static int hva_probe(struct platform_device *pdev)
{
	struct hva_dev *hva;
	struct device *dev = &pdev->dev;
	int ret;

	hva = devm_kzalloc(dev, sizeof(*hva), GFP_KERNEL);
	if (!hva) {
		ret = -ENOMEM;
		goto err;
	}

	ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	hva->dev = dev;
	hva->pdev = pdev;
	platform_set_drvdata(pdev, hva);

	mutex_init(&hva->lock);

	/* probe hardware */
	ret = hva_hw_probe(pdev, hva);
	if (ret)
		goto err;

	/* register all available encoders */
	register_encoders(hva);

	/* register all supported formats */
	register_formats(hva);

	/* register on V4L2 */
	ret = v4l2_device_register(dev, &hva->v4l2_dev);
	if (ret) {
		dev_err(dev, "%s %s failed to register V4L2 device\n",
			HVA_PREFIX, HVA_NAME);
		goto err_hw;
	}

#ifdef CONFIG_VIDEO_STI_HVA_DEBUGFS
	hva_debugfs_create(hva);
#endif

	hva->work_queue = create_workqueue(HVA_NAME);
	if (!hva->work_queue) {
		dev_err(dev, "%s %s failed to allocate work queue\n",
			HVA_PREFIX, HVA_NAME);
		ret = -ENOMEM;
		goto err_v4l2;
	}

	/* register device */
	ret = hva_register_device(hva);
	if (ret)
		goto err_work_queue;

	dev_info(dev, "%s %s registered as /dev/video%d\n", HVA_PREFIX,
		 HVA_NAME, hva->vdev->num);

	return 0;

err_work_queue:
	destroy_workqueue(hva->work_queue);
err_v4l2:
#ifdef CONFIG_VIDEO_STI_HVA_DEBUGFS
	hva_debugfs_remove(hva);
#endif
	v4l2_device_unregister(&hva->v4l2_dev);
err_hw:
	hva_hw_remove(hva);
err:
	return ret;
}

static void hva_remove(struct platform_device *pdev)
{
	struct hva_dev *hva = platform_get_drvdata(pdev);
	struct device *dev = hva_to_dev(hva);

	hva_unregister_device(hva);

	destroy_workqueue(hva->work_queue);

	hva_hw_remove(hva);

#ifdef CONFIG_VIDEO_STI_HVA_DEBUGFS
	hva_debugfs_remove(hva);
#endif

	v4l2_device_unregister(&hva->v4l2_dev);

	dev_info(dev, "%s %s removed\n", HVA_PREFIX, pdev->name);
}

/* PM ops */
static const struct dev_pm_ops hva_pm_ops = {
	.runtime_suspend	= hva_hw_runtime_suspend,
	.runtime_resume		= hva_hw_runtime_resume,
};

static const struct of_device_id hva_match_types[] = {
	{
	 .compatible = "st,st-hva",
	},
	{ /* end node */ }
};

MODULE_DEVICE_TABLE(of, hva_match_types);

static struct platform_driver hva_driver = {
	.probe  = hva_probe,
	.remove_new = hva_remove,
	.driver = {
		.name		= HVA_NAME,
		.of_match_table	= hva_match_types,
		.pm		= &hva_pm_ops,
		},
};

module_platform_driver(hva_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yannick Fertre <yannick.fertre@st.com>");
MODULE_DESCRIPTION("STMicroelectronics HVA video encoder V4L2 driver");
