// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip Video Decoder driver
 *
 * Copyright (C) 2019 Collabora, Ltd.
 *
 * Based on rkvdec driver by Google LLC. (Tomasz Figa <tfiga@chromium.org>)
 * Based on s5p-mfc driver by Samsung Electronics Co., Ltd.
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/workqueue.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>

#include "rkvdec.h"
#include "rkvdec-regs.h"

static const struct rkvdec_ctrl_desc rkvdec_h264_ctrl_descs[] = {
	{
		.per_request = true,
		.mandatory = true,
		.cfg.id = V4L2_CID_MPEG_VIDEO_H264_DECODE_PARAMS,
	},
	{
		.per_request = true,
		.mandatory = true,
		.cfg.id = V4L2_CID_MPEG_VIDEO_H264_SLICE_PARAMS,
	},
	{
		.per_request = true,
		.mandatory = true,
		.cfg.id = V4L2_CID_MPEG_VIDEO_H264_SPS,
	},
	{
		.per_request = true,
		.mandatory = true,
		.cfg.id = V4L2_CID_MPEG_VIDEO_H264_PPS,
	},
	{
		.per_request = true,
		.mandatory = true,
		.cfg.id = V4L2_CID_MPEG_VIDEO_H264_SCALING_MATRIX,
	},
	{
		.mandatory = true,
		.cfg.id = V4L2_CID_MPEG_VIDEO_H264_DECODE_MODE,
		.cfg.min = V4L2_MPEG_VIDEO_H264_DECODE_MODE_FRAME_BASED,
		.cfg.max = V4L2_MPEG_VIDEO_H264_DECODE_MODE_FRAME_BASED,
		.cfg.def = V4L2_MPEG_VIDEO_H264_DECODE_MODE_FRAME_BASED,
	},
	{
		.mandatory = true,
		.cfg.id = V4L2_CID_MPEG_VIDEO_H264_START_CODE,
		.cfg.min = V4L2_MPEG_VIDEO_H264_START_CODE_ANNEX_B,
		.cfg.def = V4L2_MPEG_VIDEO_H264_START_CODE_ANNEX_B,
		.cfg.max = V4L2_MPEG_VIDEO_H264_START_CODE_ANNEX_B,
	},
};

static const struct rkvdec_ctrls rkvdec_h264_ctrls = {
	.ctrls = rkvdec_h264_ctrl_descs,
	.num_ctrls = ARRAY_SIZE(rkvdec_h264_ctrl_descs),
};

static const u32 rkvdec_h264_decoded_fmts[] = {
	V4L2_PIX_FMT_NV12,
};

static const struct rkvdec_coded_fmt_desc rkvdec_coded_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_H264_SLICE,
		.frmsize = {
			.min_width = 48,
			.max_width = 4096,
			.step_width = 16,
			.min_height = 48,
			.max_height = 2304,
			.step_height = 16,
		},
		.ctrls = &rkvdec_h264_ctrls,
		.ops = &rkvdec_h264_fmt_ops,
		.num_decoded_fmts = ARRAY_SIZE(rkvdec_h264_decoded_fmts),
		.decoded_fmts = rkvdec_h264_decoded_fmts,
	}
};

static const struct rkvdec_coded_fmt_desc *
rkvdec_find_coded_fmt_desc(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rkvdec_coded_fmts); i++) {
		if (rkvdec_coded_fmts[i].fourcc == fourcc)
			return &rkvdec_coded_fmts[i];
	}

	return NULL;
}

static void rkvdec_reset_fmt(struct rkvdec_ctx *ctx, struct v4l2_format *f,
			     u32 fourcc)
{
	memset(f, 0, sizeof(*f));
	f->fmt.pix_mp.pixelformat = fourcc;
	f->fmt.pix_mp.field = V4L2_FIELD_NONE;
	f->fmt.pix_mp.colorspace = V4L2_COLORSPACE_REC709,
	f->fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	f->fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT;
	f->fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;
}

static void rkvdec_reset_coded_fmt(struct rkvdec_ctx *ctx)
{
	struct v4l2_format *f = &ctx->coded_fmt;

	ctx->coded_fmt_desc = &rkvdec_coded_fmts[0];
	rkvdec_reset_fmt(ctx, f, ctx->coded_fmt_desc->fourcc);

	f->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	f->fmt.pix_mp.width = ctx->coded_fmt_desc->frmsize.min_width;
	f->fmt.pix_mp.height = ctx->coded_fmt_desc->frmsize.min_height;

	if (ctx->coded_fmt_desc->ops->adjust_fmt)
		ctx->coded_fmt_desc->ops->adjust_fmt(ctx, f);
}

static void rkvdec_reset_decoded_fmt(struct rkvdec_ctx *ctx)
{
	struct v4l2_format *f = &ctx->decoded_fmt;

	rkvdec_reset_fmt(ctx, f, ctx->coded_fmt_desc->decoded_fmts[0]);
	f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	v4l2_fill_pixfmt_mp(&f->fmt.pix_mp,
			    ctx->coded_fmt_desc->decoded_fmts[0],
			    ctx->coded_fmt.fmt.pix_mp.width,
			    ctx->coded_fmt.fmt.pix_mp.height);
	f->fmt.pix_mp.plane_fmt[0].sizeimage += 128 *
		DIV_ROUND_UP(f->fmt.pix_mp.width, 16) *
		DIV_ROUND_UP(f->fmt.pix_mp.height, 16);
}

static int rkvdec_enum_framesizes(struct file *file, void *priv,
				  struct v4l2_frmsizeenum *fsize)
{
	const struct rkvdec_coded_fmt_desc *fmt;

	if (fsize->index != 0)
		return -EINVAL;

	fmt = rkvdec_find_coded_fmt_desc(fsize->pixel_format);
	if (!fmt)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise = fmt->frmsize;
	return 0;
}

static int rkvdec_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct rkvdec_dev *rkvdec = video_drvdata(file);
	struct video_device *vdev = video_devdata(file);

	strscpy(cap->driver, rkvdec->dev->driver->name,
		sizeof(cap->driver));
	strscpy(cap->card, vdev->name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 rkvdec->dev->driver->name);
	return 0;
}

static int rkvdec_try_capture_fmt(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct rkvdec_ctx *ctx = fh_to_rkvdec_ctx(priv);
	const struct rkvdec_coded_fmt_desc *coded_desc;
	unsigned int i;

	/*
	 * The codec context should point to a coded format desc, if the format
	 * on the coded end has not been set yet, it should point to the
	 * default value.
	 */
	coded_desc = ctx->coded_fmt_desc;
	if (WARN_ON(!coded_desc))
		return -EINVAL;

	for (i = 0; i < coded_desc->num_decoded_fmts; i++) {
		if (coded_desc->decoded_fmts[i] == pix_mp->pixelformat)
			break;
	}

	if (i == coded_desc->num_decoded_fmts)
		pix_mp->pixelformat = coded_desc->decoded_fmts[0];

	/* Always apply the frmsize constraint of the coded end. */
	v4l2_apply_frmsize_constraints(&pix_mp->width,
				       &pix_mp->height,
				       &coded_desc->frmsize);

	v4l2_fill_pixfmt_mp(pix_mp, pix_mp->pixelformat,
			    pix_mp->width, pix_mp->height);
	pix_mp->plane_fmt[0].sizeimage +=
		128 *
		DIV_ROUND_UP(pix_mp->width, 16) *
		DIV_ROUND_UP(pix_mp->height, 16);
	pix_mp->field = V4L2_FIELD_NONE;

	return 0;
}

static int rkvdec_try_output_fmt(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct rkvdec_ctx *ctx = fh_to_rkvdec_ctx(priv);
	const struct rkvdec_coded_fmt_desc *desc;

	desc = rkvdec_find_coded_fmt_desc(pix_mp->pixelformat);
	if (!desc) {
		pix_mp->pixelformat = rkvdec_coded_fmts[0].fourcc;
		desc = &rkvdec_coded_fmts[0];
	}

	v4l2_apply_frmsize_constraints(&pix_mp->width,
				       &pix_mp->height,
				       &desc->frmsize);

	pix_mp->field = V4L2_FIELD_NONE;
	/* All coded formats are considered single planar for now. */
	pix_mp->num_planes = 1;

	if (desc->ops->adjust_fmt) {
		int ret;

		ret = desc->ops->adjust_fmt(ctx, f);
		if (ret)
			return ret;
	}

	return 0;
}

static int rkvdec_s_fmt(struct file *file, void *priv,
			struct v4l2_format *f,
			int (*try_fmt)(struct file *, void *,
				       struct v4l2_format *))
{
	struct rkvdec_ctx *ctx = fh_to_rkvdec_ctx(priv);
	struct vb2_queue *vq;

	if (!try_fmt)
		return -EINVAL;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (vb2_is_busy(vq))
		return -EBUSY;

	return try_fmt(file, priv, f);
}

static int rkvdec_s_capture_fmt(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct rkvdec_ctx *ctx = fh_to_rkvdec_ctx(priv);
	int ret;

	ret = rkvdec_s_fmt(file, priv, f, rkvdec_try_capture_fmt);
	if (ret)
		return ret;

	ctx->decoded_fmt = *f;
	return 0;
}

static int rkvdec_s_output_fmt(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct rkvdec_ctx *ctx = fh_to_rkvdec_ctx(priv);
	struct v4l2_m2m_ctx *m2m_ctx = ctx->fh.m2m_ctx;
	const struct rkvdec_coded_fmt_desc *desc;
	struct v4l2_format *cap_fmt;
	struct vb2_queue *peer_vq;
	int ret;

	/*
	 * Since format change on the OUTPUT queue will reset the CAPTURE
	 * queue, we can't allow doing so when the CAPTURE queue has buffers
	 * allocated.
	 */
	peer_vq = v4l2_m2m_get_vq(m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (vb2_is_busy(peer_vq))
		return -EBUSY;

	ret = rkvdec_s_fmt(file, priv, f, rkvdec_try_output_fmt);
	if (ret)
		return ret;

	desc = rkvdec_find_coded_fmt_desc(f->fmt.pix_mp.pixelformat);
	if (!desc)
		return -EINVAL;
	ctx->coded_fmt_desc = desc;
	ctx->coded_fmt = *f;

	/*
	 * Current decoded format might have become invalid with newly
	 * selected codec, so reset it to default just to be safe and
	 * keep internal driver state sane. User is mandated to set
	 * the decoded format again after we return, so we don't need
	 * anything smarter.
	 *
	 * Note that this will propagates any size changes to the decoded format.
	 */
	rkvdec_reset_decoded_fmt(ctx);

	/* Propagate colorspace information to capture. */
	cap_fmt = &ctx->decoded_fmt;
	cap_fmt->fmt.pix_mp.colorspace = f->fmt.pix_mp.colorspace;
	cap_fmt->fmt.pix_mp.xfer_func = f->fmt.pix_mp.xfer_func;
	cap_fmt->fmt.pix_mp.ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
	cap_fmt->fmt.pix_mp.quantization = f->fmt.pix_mp.quantization;

	return 0;
}

static int rkvdec_g_output_fmt(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct rkvdec_ctx *ctx = fh_to_rkvdec_ctx(priv);

	*f = ctx->coded_fmt;
	return 0;
}

static int rkvdec_g_capture_fmt(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct rkvdec_ctx *ctx = fh_to_rkvdec_ctx(priv);

	*f = ctx->decoded_fmt;
	return 0;
}

static int rkvdec_enum_output_fmt(struct file *file, void *priv,
				  struct v4l2_fmtdesc *f)
{
	if (f->index >= ARRAY_SIZE(rkvdec_coded_fmts))
		return -EINVAL;

	f->pixelformat = rkvdec_coded_fmts[f->index].fourcc;
	return 0;
}

static int rkvdec_enum_capture_fmt(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	struct rkvdec_ctx *ctx = fh_to_rkvdec_ctx(priv);

	if (WARN_ON(!ctx->coded_fmt_desc))
		return -EINVAL;

	if (f->index >= ctx->coded_fmt_desc->num_decoded_fmts)
		return -EINVAL;

	f->pixelformat = ctx->coded_fmt_desc->decoded_fmts[f->index];
	return 0;
}

static const struct v4l2_ioctl_ops rkvdec_ioctl_ops = {
	.vidioc_querycap = rkvdec_querycap,
	.vidioc_enum_framesizes = rkvdec_enum_framesizes,

	.vidioc_try_fmt_vid_cap_mplane = rkvdec_try_capture_fmt,
	.vidioc_try_fmt_vid_out_mplane = rkvdec_try_output_fmt,
	.vidioc_s_fmt_vid_out_mplane = rkvdec_s_output_fmt,
	.vidioc_s_fmt_vid_cap_mplane = rkvdec_s_capture_fmt,
	.vidioc_g_fmt_vid_out_mplane = rkvdec_g_output_fmt,
	.vidioc_g_fmt_vid_cap_mplane = rkvdec_g_capture_fmt,
	.vidioc_enum_fmt_vid_out = rkvdec_enum_output_fmt,
	.vidioc_enum_fmt_vid_cap = rkvdec_enum_capture_fmt,

	.vidioc_reqbufs = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf = v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf = v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf = v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf = v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs = v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf = v4l2_m2m_ioctl_expbuf,

	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,

	.vidioc_streamon = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff = v4l2_m2m_ioctl_streamoff,
};

static int rkvdec_queue_setup(struct vb2_queue *vq, unsigned int *num_buffers,
			      unsigned int *num_planes, unsigned int sizes[],
			      struct device *alloc_devs[])
{
	struct rkvdec_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_format *f;
	unsigned int i;

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		f = &ctx->coded_fmt;
	else
		f = &ctx->decoded_fmt;

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

	return 0;
}

static int rkvdec_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct rkvdec_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_format *f;
	unsigned int i;

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		f = &ctx->coded_fmt;
	else
		f = &ctx->decoded_fmt;

	for (i = 0; i < f->fmt.pix_mp.num_planes; ++i) {
		u32 sizeimage = f->fmt.pix_mp.plane_fmt[i].sizeimage;

		if (vb2_plane_size(vb, i) < sizeimage)
			return -EINVAL;
	}
	vb2_set_plane_payload(vb, 0, f->fmt.pix_mp.plane_fmt[0].sizeimage);
	return 0;
}

static void rkvdec_buf_queue(struct vb2_buffer *vb)
{
	struct rkvdec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static int rkvdec_buf_out_validate(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	vbuf->field = V4L2_FIELD_NONE;
	return 0;
}

static void rkvdec_buf_request_complete(struct vb2_buffer *vb)
{
	struct rkvdec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_ctrl_request_complete(vb->req_obj.req, &ctx->ctrl_hdl);
}

static int rkvdec_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct rkvdec_ctx *ctx = vb2_get_drv_priv(q);
	const struct rkvdec_coded_fmt_desc *desc;
	int ret;

	if (!V4L2_TYPE_IS_OUTPUT(q->type))
		return 0;

	desc = ctx->coded_fmt_desc;
	if (WARN_ON(!desc))
		return -EINVAL;

	if (desc->ops->start) {
		ret = desc->ops->start(ctx);
		if (ret)
			return ret;
	}

	return 0;
}

static void rkvdec_queue_cleanup(struct vb2_queue *vq, u32 state)
{
	struct rkvdec_ctx *ctx = vb2_get_drv_priv(vq);

	while (true) {
		struct vb2_v4l2_buffer *vbuf;

		if (V4L2_TYPE_IS_OUTPUT(vq->type))
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

		if (!vbuf)
			break;

		v4l2_ctrl_request_complete(vbuf->vb2_buf.req_obj.req,
					   &ctx->ctrl_hdl);
		v4l2_m2m_buf_done(vbuf, state);
	}
}

static void rkvdec_stop_streaming(struct vb2_queue *q)
{
	struct rkvdec_ctx *ctx = vb2_get_drv_priv(q);

	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		const struct rkvdec_coded_fmt_desc *desc = ctx->coded_fmt_desc;

		if (WARN_ON(!desc))
			return;

		if (desc->ops->stop)
			desc->ops->stop(ctx);
	}

	rkvdec_queue_cleanup(q, VB2_BUF_STATE_ERROR);
}

static const struct vb2_ops rkvdec_queue_ops = {
	.queue_setup = rkvdec_queue_setup,
	.buf_prepare = rkvdec_buf_prepare,
	.buf_queue = rkvdec_buf_queue,
	.buf_out_validate = rkvdec_buf_out_validate,
	.buf_request_complete = rkvdec_buf_request_complete,
	.start_streaming = rkvdec_start_streaming,
	.stop_streaming = rkvdec_stop_streaming,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
};

static int rkvdec_request_validate(struct media_request *req)
{
	struct media_request_object *obj;
	const struct rkvdec_ctrls *ctrls;
	struct v4l2_ctrl_handler *hdl;
	struct rkvdec_ctx *ctx = NULL;
	unsigned int count, i;
	int ret;

	list_for_each_entry(obj, &req->objects, list) {
		if (vb2_request_object_is_buffer(obj)) {
			struct vb2_buffer *vb;

			vb = container_of(obj, struct vb2_buffer, req_obj);
			ctx = vb2_get_drv_priv(vb->vb2_queue);
			break;
		}
	}

	if (!ctx)
		return -EINVAL;

	count = vb2_request_buffer_cnt(req);
	if (!count)
		return -ENOENT;
	else if (count > 1)
		return -EINVAL;

	hdl = v4l2_ctrl_request_hdl_find(req, &ctx->ctrl_hdl);
	if (!hdl)
		return -ENOENT;

	ret = 0;
	ctrls = ctx->coded_fmt_desc->ctrls;
	for (i = 0; ctrls && i < ctrls->num_ctrls; i++) {
		u32 id = ctrls->ctrls[i].cfg.id;
		struct v4l2_ctrl *ctrl;

		if (!ctrls->ctrls[i].per_request || !ctrls->ctrls[i].mandatory)
			continue;

		ctrl = v4l2_ctrl_request_hdl_ctrl_find(hdl, id);
		if (!ctrl) {
			ret = -ENOENT;
			break;
		}
	}

	v4l2_ctrl_request_hdl_put(hdl);

	if (ret)
		return ret;

	return vb2_request_validate(req);
}

static const struct media_device_ops rkvdec_media_ops = {
	.req_validate = rkvdec_request_validate,
	.req_queue = v4l2_m2m_request_queue,
};

static void rkvdec_job_finish_no_pm(struct rkvdec_ctx *ctx,
				    enum vb2_buffer_state result)
{
	if (ctx->coded_fmt_desc->ops->done) {
		struct vb2_v4l2_buffer *src_buf, *dst_buf;

		src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
		dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
		ctx->coded_fmt_desc->ops->done(ctx, src_buf, dst_buf, result);
	}

	v4l2_m2m_buf_done_and_job_finish(ctx->dev->m2m_dev, ctx->fh.m2m_ctx,
					 result);
}

static void rkvdec_job_finish(struct rkvdec_ctx *ctx,
			      enum vb2_buffer_state result)
{
	struct rkvdec_dev *rkvdec = ctx->dev;

	pm_runtime_mark_last_busy(rkvdec->dev);
	pm_runtime_put_autosuspend(rkvdec->dev);
	rkvdec_job_finish_no_pm(ctx, result);
}

void rkvdec_run_preamble(struct rkvdec_ctx *ctx, struct rkvdec_run *run)
{
	struct media_request *src_req;

	memset(run, 0, sizeof(*run));

	run->bufs.src = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	run->bufs.dst = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	/* Apply request(s) controls if needed. */
	src_req = run->bufs.src->vb2_buf.req_obj.req;
	if (src_req)
		v4l2_ctrl_request_setup(src_req, &ctx->ctrl_hdl);

	v4l2_m2m_buf_copy_metadata(run->bufs.src, run->bufs.dst, true);
}

void rkvdec_run_postamble(struct rkvdec_ctx *ctx, struct rkvdec_run *run)
{
	struct media_request *src_req = run->bufs.src->vb2_buf.req_obj.req;

	if (src_req)
		v4l2_ctrl_request_complete(src_req, &ctx->ctrl_hdl);
}

static void rkvdec_device_run(void *priv)
{
	struct rkvdec_ctx *ctx = priv;
	struct rkvdec_dev *rkvdec = ctx->dev;
	const struct rkvdec_coded_fmt_desc *desc = ctx->coded_fmt_desc;
	int ret;

	if (WARN_ON(!desc))
		return;

	ret = pm_runtime_get_sync(rkvdec->dev);
	if (ret < 0) {
		rkvdec_job_finish_no_pm(ctx, VB2_BUF_STATE_ERROR);
		return;
	}

	ret = desc->ops->run(ctx);
	if (ret)
		rkvdec_job_finish(ctx, VB2_BUF_STATE_ERROR);
}

static struct v4l2_m2m_ops rkvdec_m2m_ops = {
	.device_run = rkvdec_device_run,
};

static int rkvdec_queue_init(void *priv,
			     struct vb2_queue *src_vq,
			     struct vb2_queue *dst_vq)
{
	struct rkvdec_ctx *ctx = priv;
	struct rkvdec_dev *rkvdec = ctx->dev;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->ops = &rkvdec_queue_ops;
	src_vq->mem_ops = &vb2_dma_contig_memops;

	/*
	 * Driver does mostly sequential access, so sacrifice TLB efficiency
	 * for faster allocation. Also, no CPU access on the source queue,
	 * so no kernel mapping needed.
	 */
	src_vq->dma_attrs = DMA_ATTR_ALLOC_SINGLE_PAGES |
			    DMA_ATTR_NO_KERNEL_MAPPING;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &rkvdec->vdev_lock;
	src_vq->dev = rkvdec->v4l2_dev.dev;
	src_vq->supports_requests = true;
	src_vq->requires_requests = true;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->bidirectional = true;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->dma_attrs = DMA_ATTR_ALLOC_SINGLE_PAGES |
			    DMA_ATTR_NO_KERNEL_MAPPING;
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->ops = &rkvdec_queue_ops;
	dst_vq->buf_struct_size = sizeof(struct rkvdec_decoded_buffer);
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &rkvdec->vdev_lock;
	dst_vq->dev = rkvdec->v4l2_dev.dev;

	return vb2_queue_init(dst_vq);
}

static int rkvdec_add_ctrls(struct rkvdec_ctx *ctx,
			    const struct rkvdec_ctrls *ctrls)
{
	unsigned int i;

	for (i = 0; i < ctrls->num_ctrls; i++) {
		const struct v4l2_ctrl_config *cfg = &ctrls->ctrls[i].cfg;

		v4l2_ctrl_new_custom(&ctx->ctrl_hdl, cfg, ctx);
		if (ctx->ctrl_hdl.error)
			return ctx->ctrl_hdl.error;
	}

	return 0;
}

static int rkvdec_init_ctrls(struct rkvdec_ctx *ctx)
{
	unsigned int i, nctrls = 0;
	int ret;

	for (i = 0; i < ARRAY_SIZE(rkvdec_coded_fmts); i++)
		nctrls += rkvdec_coded_fmts[i].ctrls->num_ctrls;

	v4l2_ctrl_handler_init(&ctx->ctrl_hdl, nctrls);

	for (i = 0; i < ARRAY_SIZE(rkvdec_coded_fmts); i++) {
		ret = rkvdec_add_ctrls(ctx, rkvdec_coded_fmts[i].ctrls);
		if (ret)
			goto err_free_handler;
	}

	ret = v4l2_ctrl_handler_setup(&ctx->ctrl_hdl);
	if (ret)
		goto err_free_handler;

	ctx->fh.ctrl_handler = &ctx->ctrl_hdl;
	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(&ctx->ctrl_hdl);
	return ret;
}

static int rkvdec_open(struct file *filp)
{
	struct rkvdec_dev *rkvdec = video_drvdata(filp);
	struct rkvdec_ctx *ctx;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = rkvdec;
	rkvdec_reset_coded_fmt(ctx);
	rkvdec_reset_decoded_fmt(ctx);
	v4l2_fh_init(&ctx->fh, video_devdata(filp));

	ret = rkvdec_init_ctrls(ctx);
	if (ret)
		goto err_free_ctx;

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(rkvdec->m2m_dev, ctx,
					    rkvdec_queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto err_cleanup_ctrls;
	}

	filp->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	return 0;

err_cleanup_ctrls:
	v4l2_ctrl_handler_free(&ctx->ctrl_hdl);

err_free_ctx:
	kfree(ctx);
	return ret;
}

static int rkvdec_release(struct file *filp)
{
	struct rkvdec_ctx *ctx = fh_to_rkvdec_ctx(filp->private_data);

	v4l2_fh_del(&ctx->fh);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	v4l2_ctrl_handler_free(&ctx->ctrl_hdl);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);

	return 0;
}

static const struct v4l2_file_operations rkvdec_fops = {
	.owner = THIS_MODULE,
	.open = rkvdec_open,
	.release = rkvdec_release,
	.poll = v4l2_m2m_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = v4l2_m2m_fop_mmap,
};

static int rkvdec_v4l2_init(struct rkvdec_dev *rkvdec)
{
	int ret;

	ret = v4l2_device_register(rkvdec->dev, &rkvdec->v4l2_dev);
	if (ret) {
		dev_err(rkvdec->dev, "Failed to register V4L2 device\n");
		return ret;
	}

	rkvdec->m2m_dev = v4l2_m2m_init(&rkvdec_m2m_ops);
	if (IS_ERR(rkvdec->m2m_dev)) {
		v4l2_err(&rkvdec->v4l2_dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(rkvdec->m2m_dev);
		goto err_unregister_v4l2;
	}

	rkvdec->mdev.dev = rkvdec->dev;
	strscpy(rkvdec->mdev.model, "rkvdec", sizeof(rkvdec->mdev.model));
	strscpy(rkvdec->mdev.bus_info, "platform:rkvdec",
		sizeof(rkvdec->mdev.bus_info));
	media_device_init(&rkvdec->mdev);
	rkvdec->mdev.ops = &rkvdec_media_ops;
	rkvdec->v4l2_dev.mdev = &rkvdec->mdev;

	rkvdec->vdev.lock = &rkvdec->vdev_lock;
	rkvdec->vdev.v4l2_dev = &rkvdec->v4l2_dev;
	rkvdec->vdev.fops = &rkvdec_fops;
	rkvdec->vdev.release = video_device_release_empty;
	rkvdec->vdev.vfl_dir = VFL_DIR_M2M;
	rkvdec->vdev.device_caps = V4L2_CAP_STREAMING |
				   V4L2_CAP_VIDEO_M2M_MPLANE;
	rkvdec->vdev.ioctl_ops = &rkvdec_ioctl_ops;
	video_set_drvdata(&rkvdec->vdev, rkvdec);
	strscpy(rkvdec->vdev.name, "rkvdec", sizeof(rkvdec->vdev.name));

	ret = video_register_device(&rkvdec->vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		v4l2_err(&rkvdec->v4l2_dev, "Failed to register video device\n");
		goto err_cleanup_mc;
	}

	ret = v4l2_m2m_register_media_controller(rkvdec->m2m_dev, &rkvdec->vdev,
						 MEDIA_ENT_F_PROC_VIDEO_DECODER);
	if (ret) {
		v4l2_err(&rkvdec->v4l2_dev,
			 "Failed to initialize V4L2 M2M media controller\n");
		goto err_unregister_vdev;
	}

	ret = media_device_register(&rkvdec->mdev);
	if (ret) {
		v4l2_err(&rkvdec->v4l2_dev, "Failed to register media device\n");
		goto err_unregister_mc;
	}

	return 0;

err_unregister_mc:
	v4l2_m2m_unregister_media_controller(rkvdec->m2m_dev);

err_unregister_vdev:
	video_unregister_device(&rkvdec->vdev);

err_cleanup_mc:
	media_device_cleanup(&rkvdec->mdev);
	v4l2_m2m_release(rkvdec->m2m_dev);

err_unregister_v4l2:
	v4l2_device_unregister(&rkvdec->v4l2_dev);
	return ret;
}

static void rkvdec_v4l2_cleanup(struct rkvdec_dev *rkvdec)
{
	media_device_unregister(&rkvdec->mdev);
	v4l2_m2m_unregister_media_controller(rkvdec->m2m_dev);
	video_unregister_device(&rkvdec->vdev);
	media_device_cleanup(&rkvdec->mdev);
	v4l2_m2m_release(rkvdec->m2m_dev);
	v4l2_device_unregister(&rkvdec->v4l2_dev);
}

static irqreturn_t rkvdec_irq_handler(int irq, void *priv)
{
	struct rkvdec_dev *rkvdec = priv;
	enum vb2_buffer_state state;
	u32 status;

	status = readl(rkvdec->regs + RKVDEC_REG_INTERRUPT);
	state = (status & RKVDEC_RDY_STA) ?
		VB2_BUF_STATE_DONE : VB2_BUF_STATE_ERROR;

	writel(0, rkvdec->regs + RKVDEC_REG_INTERRUPT);
	if (cancel_delayed_work(&rkvdec->watchdog_work)) {
		struct rkvdec_ctx *ctx;

		ctx = v4l2_m2m_get_curr_priv(rkvdec->m2m_dev);
		rkvdec_job_finish(ctx, state);
	}

	return IRQ_HANDLED;
}

static void rkvdec_watchdog_func(struct work_struct *work)
{
	struct rkvdec_dev *rkvdec;
	struct rkvdec_ctx *ctx;

	rkvdec = container_of(to_delayed_work(work), struct rkvdec_dev,
			      watchdog_work);
	ctx = v4l2_m2m_get_curr_priv(rkvdec->m2m_dev);
	if (ctx) {
		dev_err(rkvdec->dev, "Frame processing timed out!\n");
		writel(RKVDEC_IRQ_DIS, rkvdec->regs + RKVDEC_REG_INTERRUPT);
		writel(0, rkvdec->regs + RKVDEC_REG_SYSCTRL);
		rkvdec_job_finish(ctx, VB2_BUF_STATE_ERROR);
	}
}

static const struct of_device_id of_rkvdec_match[] = {
	{ .compatible = "rockchip,rk3399-vdec" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_rkvdec_match);

static const char * const rkvdec_clk_names[] = {
	"axi", "ahb", "cabac", "core"
};

static int rkvdec_probe(struct platform_device *pdev)
{
	struct rkvdec_dev *rkvdec;
	struct resource *res;
	unsigned int i;
	int ret, irq;

	rkvdec = devm_kzalloc(&pdev->dev, sizeof(*rkvdec), GFP_KERNEL);
	if (!rkvdec)
		return -ENOMEM;

	platform_set_drvdata(pdev, rkvdec);
	rkvdec->dev = &pdev->dev;
	mutex_init(&rkvdec->vdev_lock);
	INIT_DELAYED_WORK(&rkvdec->watchdog_work, rkvdec_watchdog_func);

	rkvdec->clocks = devm_kcalloc(&pdev->dev, ARRAY_SIZE(rkvdec_clk_names),
				      sizeof(*rkvdec->clocks), GFP_KERNEL);
	if (!rkvdec->clocks)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(rkvdec_clk_names); i++)
		rkvdec->clocks[i].id = rkvdec_clk_names[i];

	ret = devm_clk_bulk_get(&pdev->dev, ARRAY_SIZE(rkvdec_clk_names),
				rkvdec->clocks);
	if (ret)
		return ret;

	/*
	 * Bump ACLK to max. possible freq. (500 MHz) to improve performance
	 * When 4k video playback.
	 */
	clk_set_rate(rkvdec->clocks[0].clk, 500 * 1000 * 1000);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rkvdec->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rkvdec->regs))
		return PTR_ERR(rkvdec->regs);

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "Could not set DMA coherent mask.\n");
		return ret;
	}

	vb2_dma_contig_set_max_seg_size(&pdev->dev, DMA_BIT_MASK(32));

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(&pdev->dev, "Could not get vdec IRQ\n");
		return -ENXIO;
	}

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					rkvdec_irq_handler, IRQF_ONESHOT,
					dev_name(&pdev->dev), rkvdec);
	if (ret) {
		dev_err(&pdev->dev, "Could not request vdec IRQ\n");
		return ret;
	}

	pm_runtime_set_autosuspend_delay(&pdev->dev, 100);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = rkvdec_v4l2_init(rkvdec);
	if (ret)
		goto err_disable_runtime_pm;

	return 0;

err_disable_runtime_pm:
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static int rkvdec_remove(struct platform_device *pdev)
{
	struct rkvdec_dev *rkvdec = platform_get_drvdata(pdev);

	rkvdec_v4l2_cleanup(rkvdec);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	return 0;
}

#ifdef CONFIG_PM
static int rkvdec_runtime_resume(struct device *dev)
{
	struct rkvdec_dev *rkvdec = dev_get_drvdata(dev);

	return clk_bulk_prepare_enable(ARRAY_SIZE(rkvdec_clk_names),
				       rkvdec->clocks);
}

static int rkvdec_runtime_suspend(struct device *dev)
{
	struct rkvdec_dev *rkvdec = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(ARRAY_SIZE(rkvdec_clk_names),
				   rkvdec->clocks);
	return 0;
}
#endif

static const struct dev_pm_ops rkvdec_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(rkvdec_runtime_suspend, rkvdec_runtime_resume, NULL)
};

static struct platform_driver rkvdec_driver = {
	.probe = rkvdec_probe,
	.remove = rkvdec_remove,
	.driver = {
		   .name = "rkvdec",
		   .of_match_table = of_match_ptr(of_rkvdec_match),
		   .pm = &rkvdec_pm_ops,
	},
};
module_platform_driver(rkvdec_driver);

MODULE_AUTHOR("Boris Brezillon <boris.brezillon@collabora.com>");
MODULE_DESCRIPTION("Rockchip Video Decoder driver");
MODULE_LICENSE("GPL v2");
