// SPDX-License-Identifier: GPL-2.0
/*
 * Contains the driver implementation for the V4L2 stateless interface.
 */

#include <linux/debugfs.h>
#include <linux/font.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-vmalloc.h>
#include <media/videobuf2-v4l2.h>

#include "visl-video.h"

#include "visl.h"
#include "visl-debugfs.h"

#define MIN_CODED_SZ (1024U * 256U)

static void visl_set_current_codec(struct visl_ctx *ctx)
{
	u32 fourcc = ctx->coded_fmt.fmt.pix_mp.pixelformat;

	switch (fourcc) {
	case V4L2_PIX_FMT_FWHT_STATELESS:
		ctx->current_codec = VISL_CODEC_FWHT;
		break;
	case V4L2_PIX_FMT_MPEG2_SLICE:
		ctx->current_codec = VISL_CODEC_MPEG2;
		break;
	case V4L2_PIX_FMT_VP8_FRAME:
		ctx->current_codec = VISL_CODEC_VP8;
		break;
	case V4L2_PIX_FMT_VP9_FRAME:
		ctx->current_codec = VISL_CODEC_VP9;
		break;
	case V4L2_PIX_FMT_H264_SLICE:
		ctx->current_codec = VISL_CODEC_H264;
		break;
	case V4L2_PIX_FMT_HEVC_SLICE:
		ctx->current_codec = VISL_CODEC_HEVC;
		break;
	default:
		dprintk(ctx->dev, "Warning: unsupported fourcc: %d\n", fourcc);
		ctx->current_codec = VISL_CODEC_NONE;
		break;
	}
}

static void visl_print_fmt(struct visl_ctx *ctx, const struct v4l2_format *f)
{
	const struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	u32 i;

	dprintk(ctx->dev, "width: %d\n", pix_mp->width);
	dprintk(ctx->dev, "height: %d\n", pix_mp->height);
	dprintk(ctx->dev, "pixelformat: %c%c%c%c\n",
		pix_mp->pixelformat,
		(pix_mp->pixelformat >> 8) & 0xff,
		(pix_mp->pixelformat >> 16) & 0xff,
		(pix_mp->pixelformat >> 24) & 0xff);

	dprintk(ctx->dev, "field: %d\n", pix_mp->field);
	dprintk(ctx->dev, "colorspace: %d\n", pix_mp->colorspace);
	dprintk(ctx->dev, "num_planes: %d\n", pix_mp->num_planes);
	dprintk(ctx->dev, "flags: %d\n", pix_mp->flags);
	dprintk(ctx->dev, "quantization: %d\n", pix_mp->quantization);
	dprintk(ctx->dev, "xfer_func: %d\n", pix_mp->xfer_func);

	for (i = 0; i < pix_mp->num_planes; i++) {
		dprintk(ctx->dev,
			"plane[%d]: sizeimage: %d\n", i, pix_mp->plane_fmt[i].sizeimage);
		dprintk(ctx->dev,
			"plane[%d]: bytesperline: %d\n", i, pix_mp->plane_fmt[i].bytesperline);
	}
}

static int visl_tpg_init(struct visl_ctx *ctx)
{
	const struct font_desc *font;
	const char *font_name = "VGA8x16";
	int ret;
	u32 width = ctx->decoded_fmt.fmt.pix_mp.width;
	u32 height = ctx->decoded_fmt.fmt.pix_mp.height;
	struct v4l2_pix_format_mplane *f = &ctx->decoded_fmt.fmt.pix_mp;

	tpg_free(&ctx->tpg);

	font = find_font(font_name);
	if (font) {
		tpg_init(&ctx->tpg, width, height);

		ret = tpg_alloc(&ctx->tpg, width);
		if (ret)
			goto err_alloc;

		tpg_set_font(font->data);
		ret = tpg_s_fourcc(&ctx->tpg,
				   f->pixelformat);

		if (!ret)
			goto err_fourcc;

		tpg_reset_source(&ctx->tpg, width, height, f->field);

		tpg_s_pattern(&ctx->tpg, TPG_PAT_75_COLORBAR);

		tpg_s_field(&ctx->tpg, f->field, false);
		tpg_s_colorspace(&ctx->tpg, f->colorspace);
		tpg_s_ycbcr_enc(&ctx->tpg, f->ycbcr_enc);
		tpg_s_quantization(&ctx->tpg, f->quantization);
		tpg_s_xfer_func(&ctx->tpg, f->xfer_func);
	} else {
		v4l2_err(&ctx->dev->v4l2_dev,
			 "Font %s not found\n", font_name);

		return -EINVAL;
	}

	dprintk(ctx->dev, "Initialized the V4L2 test pattern generator, w=%d, h=%d, max_w=%d\n",
		width, height, width);

	return 0;
err_alloc:
	return ret;
err_fourcc:
	tpg_free(&ctx->tpg);
	return ret;
}

static const u32 visl_decoded_fmts[] = {
	V4L2_PIX_FMT_NV12,
	V4L2_PIX_FMT_YUV420,
};

const struct visl_coded_format_desc visl_coded_fmts[] = {
	{
		.pixelformat = V4L2_PIX_FMT_FWHT_STATELESS,
		.frmsize = {
			.min_width = 640,
			.max_width = 4096,
			.step_width = 1,
			.min_height = 360,
			.max_height = 2160,
			.step_height = 1,
		},
		.ctrls = &visl_fwht_ctrls,
		.num_decoded_fmts = ARRAY_SIZE(visl_decoded_fmts),
		.decoded_fmts = visl_decoded_fmts,
	},
	{
		.pixelformat = V4L2_PIX_FMT_MPEG2_SLICE,
		.frmsize = {
			.min_width = 16,
			.max_width = 1920,
			.step_width = 1,
			.min_height = 16,
			.max_height = 1152,
			.step_height = 1,
		},
		.ctrls = &visl_mpeg2_ctrls,
		.num_decoded_fmts = ARRAY_SIZE(visl_decoded_fmts),
		.decoded_fmts = visl_decoded_fmts,
	},
	{
		.pixelformat = V4L2_PIX_FMT_VP8_FRAME,
		.frmsize = {
			.min_width = 64,
			.max_width = 16383,
			.step_width = 1,
			.min_height = 64,
			.max_height = 16383,
			.step_height = 1,
		},
		.ctrls = &visl_vp8_ctrls,
		.num_decoded_fmts = ARRAY_SIZE(visl_decoded_fmts),
		.decoded_fmts = visl_decoded_fmts,
	},
	{
		.pixelformat = V4L2_PIX_FMT_VP9_FRAME,
		.frmsize = {
			.min_width = 64,
			.max_width = 8192,
			.step_width = 1,
			.min_height = 64,
			.max_height = 4352,
			.step_height = 1,
		},
		.ctrls = &visl_vp9_ctrls,
		.num_decoded_fmts = ARRAY_SIZE(visl_decoded_fmts),
		.decoded_fmts = visl_decoded_fmts,
	},
	{
		.pixelformat = V4L2_PIX_FMT_H264_SLICE,
		.frmsize = {
			.min_width = 64,
			.max_width = 4096,
			.step_width = 1,
			.min_height = 64,
			.max_height = 2304,
			.step_height = 1,
		},
		.ctrls = &visl_h264_ctrls,
		.num_decoded_fmts = ARRAY_SIZE(visl_decoded_fmts),
		.decoded_fmts = visl_decoded_fmts,
	},
	{
		.pixelformat = V4L2_PIX_FMT_HEVC_SLICE,
		.frmsize = {
			.min_width = 64,
			.max_width = 4096,
			.step_width = 1,
			.min_height = 64,
			.max_height = 2304,
			.step_height = 1,
		},
		.ctrls = &visl_hevc_ctrls,
		.num_decoded_fmts = ARRAY_SIZE(visl_decoded_fmts),
		.decoded_fmts = visl_decoded_fmts,
	},
};

const size_t num_coded_fmts = ARRAY_SIZE(visl_coded_fmts);

static const struct visl_coded_format_desc*
visl_find_coded_fmt_desc(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(visl_coded_fmts); i++) {
		if (visl_coded_fmts[i].pixelformat == fourcc)
			return &visl_coded_fmts[i];
	}

	return NULL;
}

static void visl_init_fmt(struct v4l2_format *f, u32 fourcc)
{	memset(f, 0, sizeof(*f));
	f->fmt.pix_mp.pixelformat = fourcc;
	f->fmt.pix_mp.field = V4L2_FIELD_NONE;
	f->fmt.pix_mp.colorspace = V4L2_COLORSPACE_REC709;
	f->fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	f->fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT;
	f->fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;
}

static void visl_reset_coded_fmt(struct visl_ctx *ctx)
{
	struct v4l2_format *f = &ctx->coded_fmt;
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;

	ctx->coded_format_desc = &visl_coded_fmts[0];
	visl_init_fmt(f, ctx->coded_format_desc->pixelformat);

	f->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	f->fmt.pix_mp.width = ctx->coded_format_desc->frmsize.min_width;
	f->fmt.pix_mp.height = ctx->coded_format_desc->frmsize.min_height;

	pix_mp->num_planes = 1;
	pix_mp->plane_fmt[0].sizeimage = pix_mp->width * pix_mp->height * 8;

	dprintk(ctx->dev, "OUTPUT format was set to:\n");
	visl_print_fmt(ctx, &ctx->coded_fmt);

	visl_set_current_codec(ctx);
}

static int visl_reset_decoded_fmt(struct visl_ctx *ctx)
{
	struct v4l2_format *f = &ctx->decoded_fmt;
	u32 decoded_fmt = ctx->coded_format_desc[0].decoded_fmts[0];

	visl_init_fmt(f, decoded_fmt);

	f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	v4l2_fill_pixfmt_mp(&f->fmt.pix_mp,
			    ctx->coded_format_desc->decoded_fmts[0],
			    ctx->coded_fmt.fmt.pix_mp.width,
			    ctx->coded_fmt.fmt.pix_mp.height);

	dprintk(ctx->dev, "CAPTURE format was set to:\n");
	visl_print_fmt(ctx, &ctx->decoded_fmt);

	return visl_tpg_init(ctx);
}

int visl_set_default_format(struct visl_ctx *ctx)
{
	visl_reset_coded_fmt(ctx);
	return visl_reset_decoded_fmt(ctx);
}

static struct visl_q_data *get_q_data(struct visl_ctx *ctx,
				      enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		return &ctx->q_data[V4L2_M2M_SRC];
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		return &ctx->q_data[V4L2_M2M_DST];
	default:
		break;
	}
	return NULL;
}

static int visl_querycap(struct file *file, void *priv,
			 struct v4l2_capability *cap)
{
	strscpy(cap->driver, VISL_NAME, sizeof(cap->driver));
	strscpy(cap->card, VISL_NAME, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", VISL_NAME);

	return 0;
}

static int visl_enum_fmt_vid_cap(struct file *file, void *priv,
				 struct v4l2_fmtdesc *f)
{
	struct visl_ctx *ctx = visl_file_to_ctx(file);

	if (f->index >= ctx->coded_format_desc->num_decoded_fmts)
		return -EINVAL;

	f->pixelformat = ctx->coded_format_desc->decoded_fmts[f->index];
	return 0;
}

static int visl_enum_fmt_vid_out(struct file *file, void *priv,
				 struct v4l2_fmtdesc *f)
{
	if (f->index >= ARRAY_SIZE(visl_coded_fmts))
		return -EINVAL;

	f->pixelformat = visl_coded_fmts[f->index].pixelformat;
	return 0;
}

static int visl_g_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct visl_ctx *ctx = visl_file_to_ctx(file);
	*f = ctx->decoded_fmt;

	return 0;
}

static int visl_g_fmt_vid_out(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct visl_ctx *ctx = visl_file_to_ctx(file);

	*f = ctx->coded_fmt;
	return 0;
}

static int visl_try_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct visl_ctx *ctx = visl_file_to_ctx(file);
	const struct visl_coded_format_desc *coded_desc;
	unsigned int i;

	coded_desc = ctx->coded_format_desc;

	for (i = 0; i < coded_desc->num_decoded_fmts; i++) {
		if (coded_desc->decoded_fmts[i] == pix_mp->pixelformat)
			break;
	}

	if (i == coded_desc->num_decoded_fmts)
		pix_mp->pixelformat = coded_desc->decoded_fmts[0];

	v4l2_apply_frmsize_constraints(&pix_mp->width,
				       &pix_mp->height,
				       &coded_desc->frmsize);

	v4l2_fill_pixfmt_mp(pix_mp, pix_mp->pixelformat,
			    pix_mp->width, pix_mp->height);

	pix_mp->field = V4L2_FIELD_NONE;

	return 0;
}

static int visl_try_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	const struct visl_coded_format_desc *coded_desc;

	coded_desc = visl_find_coded_fmt_desc(pix_mp->pixelformat);
	if (!coded_desc) {
		pix_mp->pixelformat = visl_coded_fmts[0].pixelformat;
		coded_desc = &visl_coded_fmts[0];
	}

	v4l2_apply_frmsize_constraints(&pix_mp->width,
				       &pix_mp->height,
				       &coded_desc->frmsize);

	pix_mp->field = V4L2_FIELD_NONE;
	pix_mp->num_planes = 1;

	if (pix_mp->plane_fmt[0].sizeimage == 0)
		pix_mp->plane_fmt[0].sizeimage = max(MIN_CODED_SZ,
						     pix_mp->width * pix_mp->height * 3);

	return 0;
}

static int visl_s_fmt_vid_out(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct visl_ctx *ctx = visl_file_to_ctx(file);
	struct v4l2_m2m_ctx *m2m_ctx = ctx->fh.m2m_ctx;
	const struct visl_coded_format_desc *desc;
	struct vb2_queue *peer_vq;
	int ret;

	peer_vq = v4l2_m2m_get_vq(m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (vb2_is_busy(peer_vq))
		return -EBUSY;

	dprintk(ctx->dev, "Trying to set the OUTPUT format to:\n");
	visl_print_fmt(ctx, f);

	ret = visl_try_fmt_vid_out(file, priv, f);
	if (ret)
		return ret;

	desc = visl_find_coded_fmt_desc(f->fmt.pix_mp.pixelformat);
	ctx->coded_format_desc = desc;
	ctx->coded_fmt = *f;

	ret = visl_reset_decoded_fmt(ctx);
	if (ret)
		return ret;

	ctx->decoded_fmt.fmt.pix_mp.colorspace = f->fmt.pix_mp.colorspace;
	ctx->decoded_fmt.fmt.pix_mp.xfer_func = f->fmt.pix_mp.xfer_func;
	ctx->decoded_fmt.fmt.pix_mp.ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
	ctx->decoded_fmt.fmt.pix_mp.quantization = f->fmt.pix_mp.quantization;

	dprintk(ctx->dev, "OUTPUT format was set to:\n");
	visl_print_fmt(ctx, &ctx->coded_fmt);

	visl_set_current_codec(ctx);
	return 0;
}

static int visl_s_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct visl_ctx *ctx = visl_file_to_ctx(file);
	int ret;

	dprintk(ctx->dev, "Trying to set the CAPTURE format to:\n");
	visl_print_fmt(ctx, f);

	ret = visl_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	ctx->decoded_fmt = *f;

	dprintk(ctx->dev, "CAPTURE format was set to:\n");
	visl_print_fmt(ctx, &ctx->decoded_fmt);

	visl_tpg_init(ctx);
	return 0;
}

static int visl_enum_framesizes(struct file *file, void *priv,
				struct v4l2_frmsizeenum *fsize)
{
	const struct visl_coded_format_desc *fmt;
	struct visl_ctx *ctx = visl_file_to_ctx(file);

	if (fsize->index != 0)
		return -EINVAL;

	fmt = visl_find_coded_fmt_desc(fsize->pixel_format);
	if (!fmt) {
		dprintk(ctx->dev,
			"Unsupported format for the OUTPUT queue: %d\n",
			fsize->pixel_format);

		return -EINVAL;
	}

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise = fmt->frmsize;
	return 0;
}

const struct v4l2_ioctl_ops visl_ioctl_ops = {
	.vidioc_querycap		= visl_querycap,
	.vidioc_enum_framesizes		= visl_enum_framesizes,

	.vidioc_enum_fmt_vid_cap	= visl_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap_mplane	= visl_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap_mplane	= visl_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap_mplane	= visl_s_fmt_vid_cap,

	.vidioc_enum_fmt_vid_out	= visl_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_out_mplane	= visl_g_fmt_vid_out,
	.vidioc_try_fmt_vid_out_mplane	= visl_try_fmt_vid_out,
	.vidioc_s_fmt_vid_out_mplane	= visl_s_fmt_vid_out,

	.vidioc_reqbufs			= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf		= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf			= v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf			= v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf		= v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs		= v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf			= v4l2_m2m_ioctl_expbuf,

	.vidioc_streamon		= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff		= v4l2_m2m_ioctl_streamoff,

	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

static int visl_queue_setup(struct vb2_queue *vq,
			    unsigned int *nbuffers,
			    unsigned int *num_planes,
			    unsigned int sizes[],
			    struct device *alloc_devs[])
{
	struct visl_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_format *f;
	u32 i;
	char *qname;

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		f = &ctx->coded_fmt;
		qname = "Output";
	} else {
		f = &ctx->decoded_fmt;
		qname = "Capture";
	}

	if (*num_planes) {
		if (*num_planes != f->fmt.pix_mp.num_planes)
			return -EINVAL;

		for (i = 0; i < f->fmt.pix_mp.num_planes; i++) {
			if (sizes[i] < f->fmt.pix_mp.plane_fmt[i].sizeimage)
				return -EINVAL;
		}
	} else {
		*num_planes = f->fmt.pix_mp.num_planes;
		for (i = 0; i < f->fmt.pix_mp.num_planes; i++)
			sizes[i] = f->fmt.pix_mp.plane_fmt[i].sizeimage;
	}

	dprintk(ctx->dev, "%s: %d buffer(s) requested, num_planes=%d.\n",
		qname, *nbuffers, *num_planes);

	for (i = 0; i < f->fmt.pix_mp.num_planes; i++)
		dprintk(ctx->dev, "plane[%d].sizeimage=%d\n",
			i, f->fmt.pix_mp.plane_fmt[i].sizeimage);

	return 0;
}

static void visl_queue_cleanup(struct vb2_queue *vq, u32 state)
{
	struct visl_ctx *ctx = vb2_get_drv_priv(vq);
	struct vb2_v4l2_buffer *vbuf;

	dprintk(ctx->dev, "Cleaning up queues\n");
	for (;;) {
		if (V4L2_TYPE_IS_OUTPUT(vq->type))
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

		if (!vbuf)
			break;

		v4l2_ctrl_request_complete(vbuf->vb2_buf.req_obj.req,
					   &ctx->hdl);
		dprintk(ctx->dev, "Marked request %p as complete\n",
			vbuf->vb2_buf.req_obj.req);

		v4l2_m2m_buf_done(vbuf, state);
		dprintk(ctx->dev,
			"Marked buffer %llu as done, state is %d\n",
			vbuf->vb2_buf.timestamp,
			state);
	}
}

static int visl_buf_out_validate(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	vbuf->field = V4L2_FIELD_NONE;
	return 0;
}

static int visl_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct visl_ctx *ctx = vb2_get_drv_priv(vq);
	u32 plane_sz = vb2_plane_size(vb, 0);
	struct v4l2_pix_format *pix_fmt;

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		pix_fmt = &ctx->coded_fmt.fmt.pix;
	} else {
		pix_fmt = &ctx->decoded_fmt.fmt.pix;
		vb2_set_plane_payload(vb, 0, pix_fmt->sizeimage);
	}

	if (plane_sz < pix_fmt->sizeimage) {
		v4l2_err(&ctx->dev->v4l2_dev, "plane[0] size is %d, sizeimage is %d\n",
			 plane_sz, pix_fmt->sizeimage);
		return -EINVAL;
	}

	return 0;
}

static int visl_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct visl_ctx *ctx = vb2_get_drv_priv(vq);
	struct visl_q_data *q_data = get_q_data(ctx, vq->type);
	int rc = 0;

	if (!q_data) {
		rc = -EINVAL;
		goto err;
	}

	q_data->sequence = 0;

	if (V4L2_TYPE_IS_CAPTURE(vq->type)) {
		ctx->capture_streamon_jiffies = get_jiffies_64();
		return 0;
	}

	if (WARN_ON(!ctx->coded_format_desc)) {
		rc =  -EINVAL;
		goto err;
	}

	return 0;

err:
	visl_queue_cleanup(vq, VB2_BUF_STATE_QUEUED);
	return rc;
}

static void visl_stop_streaming(struct vb2_queue *vq)
{
	struct visl_ctx *ctx = vb2_get_drv_priv(vq);

	dprintk(ctx->dev, "Stop streaming\n");
	visl_queue_cleanup(vq, VB2_BUF_STATE_ERROR);

	if (!keep_bitstream_buffers)
		visl_debugfs_clear_bitstream(ctx->dev);
}

static void visl_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct visl_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static void visl_buf_request_complete(struct vb2_buffer *vb)
{
	struct visl_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_ctrl_request_complete(vb->req_obj.req, &ctx->hdl);
}

static const struct vb2_ops visl_qops = {
	.queue_setup          = visl_queue_setup,
	.buf_out_validate     = visl_buf_out_validate,
	.buf_prepare          = visl_buf_prepare,
	.buf_queue            = visl_buf_queue,
	.start_streaming      = visl_start_streaming,
	.stop_streaming       = visl_stop_streaming,
	.wait_prepare         = vb2_ops_wait_prepare,
	.wait_finish          = vb2_ops_wait_finish,
	.buf_request_complete = visl_buf_request_complete,
};

int visl_queue_init(void *priv, struct vb2_queue *src_vq,
		    struct vb2_queue *dst_vq)
{
	struct visl_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &visl_qops;
	src_vq->mem_ops = &vb2_vmalloc_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->vb_mutex;
	src_vq->supports_requests = true;
	src_vq->subsystem_flags |= VB2_V4L2_FL_SUPPORTS_M2M_HOLD_CAPTURE_BUF;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &visl_qops;
	dst_vq->mem_ops = &vb2_vmalloc_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->vb_mutex;

	return vb2_queue_init(dst_vq);
}

int visl_request_validate(struct media_request *req)
{
	struct media_request_object *obj;
	struct visl_ctx *ctx = NULL;
	unsigned int count;

	list_for_each_entry(obj, &req->objects, list) {
		struct vb2_buffer *vb;

		if (vb2_request_object_is_buffer(obj)) {
			vb = container_of(obj, struct vb2_buffer, req_obj);
			ctx = vb2_get_drv_priv(vb->vb2_queue);

			break;
		}
	}

	if (!ctx)
		return -ENOENT;

	count = vb2_request_buffer_cnt(req);
	if (!count) {
		v4l2_err(&ctx->dev->v4l2_dev,
			 "No buffer was provided with the request\n");
		return -ENOENT;
	} else if (count > 1) {
		v4l2_err(&ctx->dev->v4l2_dev,
			 "More than one buffer was provided with the request\n");
		return -EINVAL;
	}

	return vb2_request_validate(req);
}
