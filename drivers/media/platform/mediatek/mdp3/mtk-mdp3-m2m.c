// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#include <linux/platform_device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-dma-contig.h>
#include "mtk-mdp3-m2m.h"

static inline struct mdp_m2m_ctx *fh_to_ctx(struct v4l2_fh *fh)
{
	return container_of(fh, struct mdp_m2m_ctx, fh);
}

static inline struct mdp_m2m_ctx *ctrl_to_ctx(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct mdp_m2m_ctx, ctrl_handler);
}

static inline struct mdp_frame *ctx_get_frame(struct mdp_m2m_ctx *ctx,
					      enum v4l2_buf_type type)
{
	if (V4L2_TYPE_IS_OUTPUT(type))
		return &ctx->curr_param.output;
	else
		return &ctx->curr_param.captures[0];
}

static inline void mdp_m2m_ctx_set_state(struct mdp_m2m_ctx *ctx, u32 state)
{
	atomic_or(state, &ctx->curr_param.state);
}

static inline bool mdp_m2m_ctx_is_state_set(struct mdp_m2m_ctx *ctx, u32 mask)
{
	return ((atomic_read(&ctx->curr_param.state) & mask) == mask);
}

static void mdp_m2m_process_done(void *priv, int vb_state)
{
	struct mdp_m2m_ctx *ctx = priv;
	struct vb2_v4l2_buffer *src_vbuf, *dst_vbuf;

	src_vbuf = (struct vb2_v4l2_buffer *)
			v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
	dst_vbuf = (struct vb2_v4l2_buffer *)
			v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);
	ctx->curr_param.frame_no = ctx->frame_count[MDP_M2M_SRC];
	src_vbuf->sequence = ctx->frame_count[MDP_M2M_SRC]++;
	dst_vbuf->sequence = ctx->frame_count[MDP_M2M_DST]++;
	v4l2_m2m_buf_copy_metadata(src_vbuf, dst_vbuf, true);

	v4l2_m2m_buf_done(src_vbuf, vb_state);
	v4l2_m2m_buf_done(dst_vbuf, vb_state);
	v4l2_m2m_job_finish(ctx->mdp_dev->m2m_dev, ctx->m2m_ctx);
}

static void mdp_m2m_device_run(void *priv)
{
	struct mdp_m2m_ctx *ctx = priv;
	struct mdp_frame *frame;
	struct vb2_v4l2_buffer *src_vb, *dst_vb;
	struct img_ipi_frameparam param = {};
	struct mdp_cmdq_param task = {};
	enum vb2_buffer_state vb_state = VB2_BUF_STATE_ERROR;
	int ret;

	if (mdp_m2m_ctx_is_state_set(ctx, MDP_M2M_CTX_ERROR)) {
		dev_err(&ctx->mdp_dev->pdev->dev,
			"mdp_m2m_ctx is in error state\n");
		goto worker_end;
	}

	param.frame_no = ctx->curr_param.frame_no;
	param.type = ctx->curr_param.type;
	param.num_inputs = 1;
	param.num_outputs = 1;

	frame = ctx_get_frame(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	src_vb = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	mdp_set_src_config(&param.inputs[0], frame, &src_vb->vb2_buf);

	frame = ctx_get_frame(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	dst_vb = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);
	mdp_set_dst_config(&param.outputs[0], frame, &dst_vb->vb2_buf);

	if (mdp_check_pp_enable(ctx->mdp_dev, frame))
		param.type = MDP_STREAM_TYPE_DUAL_BITBLT;

	ret = mdp_vpu_process(&ctx->mdp_dev->vpu, &param);
	if (ret) {
		dev_err(&ctx->mdp_dev->pdev->dev,
			"VPU MDP process failed: %d\n", ret);
		goto worker_end;
	}

	task.config = ctx->mdp_dev->vpu.config;
	task.param = &param;
	task.composes[0] = &frame->compose;
	task.cmdq_cb = NULL;
	task.cb_data = NULL;
	task.mdp_ctx = ctx;

	if (atomic_read(&ctx->mdp_dev->job_count)) {
		ret = wait_event_timeout(ctx->mdp_dev->callback_wq,
					 !atomic_read(&ctx->mdp_dev->job_count),
					 2 * HZ);
		if (ret == 0) {
			dev_err(&ctx->mdp_dev->pdev->dev,
				"%d jobs not yet done\n",
				atomic_read(&ctx->mdp_dev->job_count));
			goto worker_end;
		}
	}

	ret = mdp_cmdq_send(ctx->mdp_dev, &task);
	if (ret) {
		dev_err(&ctx->mdp_dev->pdev->dev,
			"CMDQ sendtask failed: %d\n", ret);
		goto worker_end;
	}

	return;

worker_end:
	mdp_m2m_process_done(ctx, vb_state);
}

static int mdp_m2m_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct mdp_m2m_ctx *ctx = vb2_get_drv_priv(q);
	struct mdp_frame *capture;
	struct vb2_queue *vq;
	int ret;
	bool out_streaming, cap_streaming;

	if (V4L2_TYPE_IS_OUTPUT(q->type))
		ctx->frame_count[MDP_M2M_SRC] = 0;

	if (V4L2_TYPE_IS_CAPTURE(q->type))
		ctx->frame_count[MDP_M2M_DST] = 0;

	capture = ctx_get_frame(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	vq = v4l2_m2m_get_src_vq(ctx->m2m_ctx);
	out_streaming = vb2_is_streaming(vq);
	vq = v4l2_m2m_get_dst_vq(ctx->m2m_ctx);
	cap_streaming = vb2_is_streaming(vq);

	/* Check to see if scaling ratio is within supported range */
	if ((V4L2_TYPE_IS_OUTPUT(q->type) && cap_streaming) ||
	    (V4L2_TYPE_IS_CAPTURE(q->type) && out_streaming)) {
		ret = mdp_check_scaling_ratio(&capture->crop.c,
					      &capture->compose,
					      capture->rotation,
					      ctx->curr_param.limit);
		if (ret) {
			dev_err(&ctx->mdp_dev->pdev->dev,
				"Out of scaling range\n");
			return ret;
		}
	}

	if (!mdp_m2m_ctx_is_state_set(ctx, MDP_VPU_INIT)) {
		ret = mdp_vpu_get_locked(ctx->mdp_dev);
		if (ret) {
			dev_err(&ctx->mdp_dev->pdev->dev,
				"VPU init failed %d\n", ret);
			return -EINVAL;
		}
		mdp_m2m_ctx_set_state(ctx, MDP_VPU_INIT);
	}

	return 0;
}

static struct vb2_v4l2_buffer *mdp_m2m_buf_remove(struct mdp_m2m_ctx *ctx,
						  unsigned int type)
{
	if (V4L2_TYPE_IS_OUTPUT(type))
		return (struct vb2_v4l2_buffer *)
			v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
	else
		return (struct vb2_v4l2_buffer *)
			v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);
}

static void mdp_m2m_stop_streaming(struct vb2_queue *q)
{
	struct mdp_m2m_ctx *ctx = vb2_get_drv_priv(q);
	struct vb2_v4l2_buffer *vb;

	vb = mdp_m2m_buf_remove(ctx, q->type);
	while (vb) {
		v4l2_m2m_buf_done(vb, VB2_BUF_STATE_ERROR);
		vb = mdp_m2m_buf_remove(ctx, q->type);
	}
}

static int mdp_m2m_queue_setup(struct vb2_queue *q,
			       unsigned int *num_buffers,
			       unsigned int *num_planes, unsigned int sizes[],
			       struct device *alloc_devs[])
{
	struct mdp_m2m_ctx *ctx = vb2_get_drv_priv(q);
	struct v4l2_pix_format_mplane *pix_mp;
	u32 i;

	pix_mp = &ctx_get_frame(ctx, q->type)->format.fmt.pix_mp;

	/* from VIDIOC_CREATE_BUFS */
	if (*num_planes) {
		if (*num_planes != pix_mp->num_planes)
			return -EINVAL;
		for (i = 0; i < pix_mp->num_planes; ++i)
			if (sizes[i] < pix_mp->plane_fmt[i].sizeimage)
				return -EINVAL;
	} else {/* from VIDIOC_REQBUFS */
		*num_planes = pix_mp->num_planes;
		for (i = 0; i < pix_mp->num_planes; ++i)
			sizes[i] = pix_mp->plane_fmt[i].sizeimage;
	}

	return 0;
}

static int mdp_m2m_buf_prepare(struct vb2_buffer *vb)
{
	struct mdp_m2m_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct v4l2_pix_format_mplane *pix_mp;
	struct vb2_v4l2_buffer *v4l2_buf = to_vb2_v4l2_buffer(vb);
	u32 i;

	v4l2_buf->field = V4L2_FIELD_NONE;

	if (V4L2_TYPE_IS_CAPTURE(vb->type)) {
		pix_mp = &ctx_get_frame(ctx, vb->type)->format.fmt.pix_mp;
		for (i = 0; i < pix_mp->num_planes; ++i) {
			vb2_set_plane_payload(vb, i,
					      pix_mp->plane_fmt[i].sizeimage);
		}
	}
	return 0;
}

static int mdp_m2m_buf_out_validate(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *v4l2_buf = to_vb2_v4l2_buffer(vb);

	v4l2_buf->field = V4L2_FIELD_NONE;

	return 0;
}

static void mdp_m2m_buf_queue(struct vb2_buffer *vb)
{
	struct mdp_m2m_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *v4l2_buf = to_vb2_v4l2_buffer(vb);

	v4l2_buf->field = V4L2_FIELD_NONE;

	v4l2_m2m_buf_queue(ctx->m2m_ctx, to_vb2_v4l2_buffer(vb));
}

static const struct vb2_ops mdp_m2m_qops = {
	.queue_setup	= mdp_m2m_queue_setup,
	.wait_prepare	= vb2_ops_wait_prepare,
	.wait_finish	= vb2_ops_wait_finish,
	.buf_prepare	= mdp_m2m_buf_prepare,
	.start_streaming = mdp_m2m_start_streaming,
	.stop_streaming	= mdp_m2m_stop_streaming,
	.buf_queue	= mdp_m2m_buf_queue,
	.buf_out_validate = mdp_m2m_buf_out_validate,
};

static int mdp_m2m_querycap(struct file *file, void *fh,
			    struct v4l2_capability *cap)
{
	strscpy(cap->driver, MDP_MODULE_NAME, sizeof(cap->driver));
	strscpy(cap->card, MDP_DEVICE_NAME, sizeof(cap->card));

	return 0;
}

static int mdp_m2m_enum_fmt_mplane(struct file *file, void *fh,
				   struct v4l2_fmtdesc *f)
{
	struct mdp_m2m_ctx *ctx = fh_to_ctx(fh);

	return mdp_enum_fmt_mplane(ctx->mdp_dev, f);
}

static int mdp_m2m_g_fmt_mplane(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct mdp_m2m_ctx *ctx = fh_to_ctx(fh);
	struct mdp_frame *frame;
	struct v4l2_pix_format_mplane *pix_mp;

	frame = ctx_get_frame(ctx, f->type);
	*f = frame->format;
	pix_mp = &f->fmt.pix_mp;
	pix_mp->colorspace = ctx->curr_param.colorspace;
	pix_mp->xfer_func = ctx->curr_param.xfer_func;
	pix_mp->ycbcr_enc = ctx->curr_param.ycbcr_enc;
	pix_mp->quantization = ctx->curr_param.quant;

	return 0;
}

static int mdp_m2m_s_fmt_mplane(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct mdp_m2m_ctx *ctx = fh_to_ctx(fh);
	struct mdp_frame *frame = ctx_get_frame(ctx, f->type);
	struct mdp_frame *capture;
	const struct mdp_format *fmt;
	struct vb2_queue *vq;

	fmt = mdp_try_fmt_mplane(ctx->mdp_dev, f, &ctx->curr_param, ctx->id);
	if (!fmt)
		return -EINVAL;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (vb2_is_busy(vq))
		return -EBUSY;

	frame->format = *f;
	frame->mdp_fmt = fmt;
	frame->ycbcr_prof = mdp_map_ycbcr_prof_mplane(f, fmt->mdp_color);
	frame->usage = V4L2_TYPE_IS_OUTPUT(f->type) ?
		MDP_BUFFER_USAGE_HW_READ : MDP_BUFFER_USAGE_MDP;

	capture = ctx_get_frame(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		capture->crop.c.left = 0;
		capture->crop.c.top = 0;
		capture->crop.c.width = f->fmt.pix_mp.width;
		capture->crop.c.height = f->fmt.pix_mp.height;
		ctx->curr_param.colorspace = f->fmt.pix_mp.colorspace;
		ctx->curr_param.ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
		ctx->curr_param.quant = f->fmt.pix_mp.quantization;
		ctx->curr_param.xfer_func = f->fmt.pix_mp.xfer_func;
	} else {
		capture->compose.left = 0;
		capture->compose.top = 0;
		capture->compose.width = f->fmt.pix_mp.width;
		capture->compose.height = f->fmt.pix_mp.height;
	}

	return 0;
}

static int mdp_m2m_try_fmt_mplane(struct file *file, void *fh,
				  struct v4l2_format *f)
{
	struct mdp_m2m_ctx *ctx = fh_to_ctx(fh);

	if (!mdp_try_fmt_mplane(ctx->mdp_dev, f, &ctx->curr_param, ctx->id))
		return -EINVAL;

	return 0;
}

static int mdp_m2m_g_selection(struct file *file, void *fh,
			       struct v4l2_selection *s)
{
	struct mdp_m2m_ctx *ctx = fh_to_ctx(fh);
	struct mdp_frame *frame;
	bool valid = false;

	if (s->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		valid = mdp_target_is_crop(s->target);
	else if (s->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		valid = mdp_target_is_compose(s->target);

	if (!valid)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
		if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		frame = ctx_get_frame(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
		s->r = frame->crop.c;
		return 0;
	case V4L2_SEL_TGT_COMPOSE:
		if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		frame = ctx_get_frame(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
		s->r = frame->compose;
		return 0;
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		frame = ctx_get_frame(ctx, s->type);
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = frame->format.fmt.pix_mp.width;
		s->r.height = frame->format.fmt.pix_mp.height;
		return 0;
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		frame = ctx_get_frame(ctx, s->type);
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = frame->format.fmt.pix_mp.width;
		s->r.height = frame->format.fmt.pix_mp.height;
		return 0;
	}
	return -EINVAL;
}

static int mdp_m2m_s_selection(struct file *file, void *fh,
			       struct v4l2_selection *s)
{
	struct mdp_m2m_ctx *ctx = fh_to_ctx(fh);
	struct mdp_frame *frame = ctx_get_frame(ctx, s->type);
	struct mdp_frame *capture;
	struct v4l2_rect r;
	struct device *dev = &ctx->mdp_dev->pdev->dev;
	bool valid = false;
	int ret;

	if (s->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		valid = (s->target == V4L2_SEL_TGT_CROP);
	else if (s->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		valid = (s->target == V4L2_SEL_TGT_COMPOSE);

	if (!valid) {
		dev_dbg(dev, "[%s:%d] invalid type:%u target:%u", __func__,
			ctx->id, s->type, s->target);
		return -EINVAL;
	}

	ret = mdp_try_crop(ctx, &r, s, frame);
	if (ret)
		return ret;
	capture = ctx_get_frame(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	if (mdp_target_is_crop(s->target))
		capture->crop.c = r;
	else
		capture->compose = r;

	s->r = r;

	return 0;
}

static const struct v4l2_ioctl_ops mdp_m2m_ioctl_ops = {
	.vidioc_querycap		= mdp_m2m_querycap,
	.vidioc_enum_fmt_vid_cap	= mdp_m2m_enum_fmt_mplane,
	.vidioc_enum_fmt_vid_out	= mdp_m2m_enum_fmt_mplane,
	.vidioc_g_fmt_vid_cap_mplane	= mdp_m2m_g_fmt_mplane,
	.vidioc_g_fmt_vid_out_mplane	= mdp_m2m_g_fmt_mplane,
	.vidioc_s_fmt_vid_cap_mplane	= mdp_m2m_s_fmt_mplane,
	.vidioc_s_fmt_vid_out_mplane	= mdp_m2m_s_fmt_mplane,
	.vidioc_try_fmt_vid_cap_mplane	= mdp_m2m_try_fmt_mplane,
	.vidioc_try_fmt_vid_out_mplane	= mdp_m2m_try_fmt_mplane,
	.vidioc_reqbufs			= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf		= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf			= v4l2_m2m_ioctl_qbuf,
	.vidioc_expbuf			= v4l2_m2m_ioctl_expbuf,
	.vidioc_dqbuf			= v4l2_m2m_ioctl_dqbuf,
	.vidioc_create_bufs		= v4l2_m2m_ioctl_create_bufs,
	.vidioc_streamon		= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff		= v4l2_m2m_ioctl_streamoff,
	.vidioc_g_selection		= mdp_m2m_g_selection,
	.vidioc_s_selection		= mdp_m2m_s_selection,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

static int mdp_m2m_queue_init(void *priv,
			      struct vb2_queue *src_vq,
			      struct vb2_queue *dst_vq)
{
	struct mdp_m2m_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->ops = &mdp_m2m_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->dev = &ctx->mdp_dev->pdev->dev;
	src_vq->lock = &ctx->ctx_lock;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->ops = &mdp_m2m_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->dev = &ctx->mdp_dev->pdev->dev;
	dst_vq->lock = &ctx->ctx_lock;

	return vb2_queue_init(dst_vq);
}

static int mdp_m2m_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mdp_m2m_ctx *ctx = ctrl_to_ctx(ctrl);
	struct mdp_frame *capture;

	capture = ctx_get_frame(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		capture->hflip = ctrl->val;
		break;
	case V4L2_CID_VFLIP:
		capture->vflip = ctrl->val;
		break;
	case V4L2_CID_ROTATE:
		capture->rotation = ctrl->val;
		break;
	}

	return 0;
}

static const struct v4l2_ctrl_ops mdp_m2m_ctrl_ops = {
	.s_ctrl	= mdp_m2m_s_ctrl,
};

static int mdp_m2m_ctrls_create(struct mdp_m2m_ctx *ctx)
{
	v4l2_ctrl_handler_init(&ctx->ctrl_handler, MDP_MAX_CTRLS);
	ctx->ctrls.hflip = v4l2_ctrl_new_std(&ctx->ctrl_handler,
					     &mdp_m2m_ctrl_ops, V4L2_CID_HFLIP,
					     0, 1, 1, 0);
	ctx->ctrls.vflip = v4l2_ctrl_new_std(&ctx->ctrl_handler,
					     &mdp_m2m_ctrl_ops, V4L2_CID_VFLIP,
					     0, 1, 1, 0);
	ctx->ctrls.rotate = v4l2_ctrl_new_std(&ctx->ctrl_handler,
					      &mdp_m2m_ctrl_ops,
					      V4L2_CID_ROTATE, 0, 270, 90, 0);

	if (ctx->ctrl_handler.error) {
		int err = ctx->ctrl_handler.error;

		v4l2_ctrl_handler_free(&ctx->ctrl_handler);
		dev_err(&ctx->mdp_dev->pdev->dev,
			"Failed to register controls\n");
		return err;
	}
	return 0;
}

static int mdp_m2m_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct mdp_dev *mdp = video_get_drvdata(vdev);
	struct mdp_m2m_ctx *ctx;
	struct device *dev = &mdp->pdev->dev;
	int ret;
	struct v4l2_format default_format = {};
	const struct mdp_limit *limit = mdp->mdp_data->def_limit;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (mutex_lock_interruptible(&mdp->m2m_lock)) {
		ret = -ERESTARTSYS;
		goto err_free_ctx;
	}

	ret = ida_alloc(&mdp->mdp_ida, GFP_KERNEL);
	if (ret < 0)
		goto err_unlock_mutex;
	ctx->id = ret;

	ctx->mdp_dev = mdp;

	v4l2_fh_init(&ctx->fh, vdev);
	file->private_data = &ctx->fh;
	ret = mdp_m2m_ctrls_create(ctx);
	if (ret)
		goto err_exit_fh;

	/* Use separate control handler per file handle */
	ctx->fh.ctrl_handler = &ctx->ctrl_handler;
	v4l2_fh_add(&ctx->fh);

	mutex_init(&ctx->ctx_lock);
	ctx->m2m_ctx = v4l2_m2m_ctx_init(mdp->m2m_dev, ctx, mdp_m2m_queue_init);
	if (IS_ERR(ctx->m2m_ctx)) {
		dev_err(dev, "Failed to initialize m2m context\n");
		ret = PTR_ERR(ctx->m2m_ctx);
		goto err_release_handler;
	}
	ctx->fh.m2m_ctx = ctx->m2m_ctx;

	ctx->curr_param.ctx = ctx;
	ret = mdp_frameparam_init(mdp, &ctx->curr_param);
	if (ret) {
		dev_err(dev, "Failed to initialize mdp parameter\n");
		goto err_release_m2m_ctx;
	}

	mutex_unlock(&mdp->m2m_lock);

	/* Default format */
	default_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	default_format.fmt.pix_mp.width = limit->out_limit.wmin;
	default_format.fmt.pix_mp.height = limit->out_limit.hmin;
	default_format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_YUV420M;
	mdp_m2m_s_fmt_mplane(file, &ctx->fh, &default_format);
	default_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	mdp_m2m_s_fmt_mplane(file, &ctx->fh, &default_format);

	dev_dbg(dev, "%s:[%d]", __func__, ctx->id);

	return 0;

err_release_m2m_ctx:
	v4l2_m2m_ctx_release(ctx->m2m_ctx);
err_release_handler:
	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	v4l2_fh_del(&ctx->fh);
err_exit_fh:
	v4l2_fh_exit(&ctx->fh);
	ida_free(&mdp->mdp_ida, ctx->id);
err_unlock_mutex:
	mutex_unlock(&mdp->m2m_lock);
err_free_ctx:
	kfree(ctx);

	return ret;
}

static int mdp_m2m_release(struct file *file)
{
	struct mdp_m2m_ctx *ctx = fh_to_ctx(file->private_data);
	struct mdp_dev *mdp = video_drvdata(file);
	struct device *dev = &mdp->pdev->dev;

	mutex_lock(&mdp->m2m_lock);
	v4l2_m2m_ctx_release(ctx->m2m_ctx);
	if (mdp_m2m_ctx_is_state_set(ctx, MDP_VPU_INIT))
		mdp_vpu_put_locked(mdp);

	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	ida_free(&mdp->mdp_ida, ctx->id);
	mutex_unlock(&mdp->m2m_lock);

	dev_dbg(dev, "%s:[%d]", __func__, ctx->id);
	kfree(ctx);

	return 0;
}

static const struct v4l2_file_operations mdp_m2m_fops = {
	.owner		= THIS_MODULE,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
	.open		= mdp_m2m_open,
	.release	= mdp_m2m_release,
};

static const struct v4l2_m2m_ops mdp_m2m_ops = {
	.device_run	= mdp_m2m_device_run,
};

int mdp_m2m_device_register(struct mdp_dev *mdp)
{
	struct device *dev = &mdp->pdev->dev;
	int ret = 0;

	mdp->m2m_vdev = video_device_alloc();
	if (!mdp->m2m_vdev) {
		dev_err(dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto err_video_alloc;
	}
	mdp->m2m_vdev->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE |
		V4L2_CAP_STREAMING;
	mdp->m2m_vdev->fops = &mdp_m2m_fops;
	mdp->m2m_vdev->ioctl_ops = &mdp_m2m_ioctl_ops;
	mdp->m2m_vdev->release = mdp_video_device_release;
	mdp->m2m_vdev->lock = &mdp->m2m_lock;
	mdp->m2m_vdev->vfl_dir = VFL_DIR_M2M;
	mdp->m2m_vdev->v4l2_dev = &mdp->v4l2_dev;
	snprintf(mdp->m2m_vdev->name, sizeof(mdp->m2m_vdev->name), "%s:m2m",
		 MDP_MODULE_NAME);
	video_set_drvdata(mdp->m2m_vdev, mdp);

	mdp->m2m_dev = v4l2_m2m_init(&mdp_m2m_ops);
	if (IS_ERR(mdp->m2m_dev)) {
		dev_err(dev, "Failed to initialize v4l2-m2m device\n");
		ret = PTR_ERR(mdp->m2m_dev);
		goto err_m2m_init;
	}

	ret = video_register_device(mdp->m2m_vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(dev, "Failed to register video device\n");
		goto err_video_register;
	}

	v4l2_info(&mdp->v4l2_dev, "Driver registered as /dev/video%d",
		  mdp->m2m_vdev->num);
	return 0;

err_video_register:
	v4l2_m2m_release(mdp->m2m_dev);
err_m2m_init:
	video_device_release(mdp->m2m_vdev);
err_video_alloc:

	return ret;
}

void mdp_m2m_device_unregister(struct mdp_dev *mdp)
{
	video_unregister_device(mdp->m2m_vdev);
}

void mdp_m2m_job_finish(struct mdp_m2m_ctx *ctx)
{
	enum vb2_buffer_state vb_state = VB2_BUF_STATE_DONE;

	mdp_m2m_process_done(ctx, vb_state);
}
