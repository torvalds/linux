// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Intel Corporation

#include <linux/module.h>
#include <linux/pm_runtime.h>

#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>

#include "ipu3.h"
#include "ipu3-dmamap.h"

/******************** v4l2_subdev_ops ********************/

#define IPU3_RUNNING_MODE_VIDEO		0
#define IPU3_RUNNING_MODE_STILL		1

static int imgu_subdev_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imgu_v4l2_subdev *imgu_sd = container_of(sd,
							struct imgu_v4l2_subdev,
							subdev);
	struct imgu_device *imgu = v4l2_get_subdevdata(sd);
	struct imgu_media_pipe *imgu_pipe = &imgu->imgu_pipe[imgu_sd->pipe];
	struct v4l2_rect try_crop = {
		.top = 0,
		.left = 0,
	};
	unsigned int i;

	try_crop.width =
		imgu_pipe->nodes[IMGU_NODE_IN].vdev_fmt.fmt.pix_mp.width;
	try_crop.height =
		imgu_pipe->nodes[IMGU_NODE_IN].vdev_fmt.fmt.pix_mp.height;

	/* Initialize try_fmt */
	for (i = 0; i < IMGU_NODE_NUM; i++) {
		struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_get_try_format(sd, fh->state, i);

		try_fmt->width = try_crop.width;
		try_fmt->height = try_crop.height;
		try_fmt->code = imgu_pipe->nodes[i].pad_fmt.code;
		try_fmt->field = V4L2_FIELD_NONE;
	}

	*v4l2_subdev_get_try_crop(sd, fh->state, IMGU_NODE_IN) = try_crop;
	*v4l2_subdev_get_try_compose(sd, fh->state, IMGU_NODE_IN) = try_crop;

	return 0;
}

static int imgu_subdev_s_stream(struct v4l2_subdev *sd, int enable)
{
	int i;
	unsigned int node;
	int r = 0;
	struct imgu_device *imgu = v4l2_get_subdevdata(sd);
	struct imgu_v4l2_subdev *imgu_sd = container_of(sd,
							struct imgu_v4l2_subdev,
							subdev);
	unsigned int pipe = imgu_sd->pipe;
	struct device *dev = &imgu->pci_dev->dev;
	struct v4l2_pix_format_mplane *fmts[IPU3_CSS_QUEUES] = { NULL };
	struct v4l2_rect *rects[IPU3_CSS_RECTS] = { NULL };
	struct imgu_css_pipe *css_pipe = &imgu->css.pipes[pipe];
	struct imgu_media_pipe *imgu_pipe = &imgu->imgu_pipe[pipe];

	dev_dbg(dev, "%s %d for pipe %u", __func__, enable, pipe);
	/* grab ctrl after streamon and return after off */
	v4l2_ctrl_grab(imgu_sd->ctrl, enable);

	if (!enable) {
		imgu_sd->active = false;
		return 0;
	}

	for (i = 0; i < IMGU_NODE_NUM; i++)
		imgu_pipe->queue_enabled[i] = imgu_pipe->nodes[i].enabled;

	/* This is handled specially */
	imgu_pipe->queue_enabled[IPU3_CSS_QUEUE_PARAMS] = false;

	/* Initialize CSS formats */
	for (i = 0; i < IPU3_CSS_QUEUES; i++) {
		node = imgu_map_node(imgu, i);
		/* No need to reconfig meta nodes */
		if (node == IMGU_NODE_STAT_3A || node == IMGU_NODE_PARAMS)
			continue;
		fmts[i] = imgu_pipe->queue_enabled[node] ?
			&imgu_pipe->nodes[node].vdev_fmt.fmt.pix_mp : NULL;
	}

	/* Enable VF output only when VF queue requested by user */
	css_pipe->vf_output_en = false;
	if (imgu_pipe->nodes[IMGU_NODE_VF].enabled)
		css_pipe->vf_output_en = true;

	if (atomic_read(&imgu_sd->running_mode) == IPU3_RUNNING_MODE_VIDEO)
		css_pipe->pipe_id = IPU3_CSS_PIPE_ID_VIDEO;
	else
		css_pipe->pipe_id = IPU3_CSS_PIPE_ID_CAPTURE;

	dev_dbg(dev, "IPU3 pipe %u pipe_id %u", pipe, css_pipe->pipe_id);

	rects[IPU3_CSS_RECT_EFFECTIVE] = &imgu_sd->rect.eff;
	rects[IPU3_CSS_RECT_BDS] = &imgu_sd->rect.bds;
	rects[IPU3_CSS_RECT_GDC] = &imgu_sd->rect.gdc;

	r = imgu_css_fmt_set(&imgu->css, fmts, rects, pipe);
	if (r) {
		dev_err(dev, "failed to set initial formats pipe %u with (%d)",
			pipe, r);
		return r;
	}

	imgu_sd->active = true;

	return 0;
}

static int imgu_subdev_get_fmt(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state,
			       struct v4l2_subdev_format *fmt)
{
	struct imgu_device *imgu = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf;
	struct imgu_media_pipe *imgu_pipe;
	u32 pad = fmt->pad;
	struct imgu_v4l2_subdev *imgu_sd = container_of(sd,
							struct imgu_v4l2_subdev,
							subdev);
	unsigned int pipe = imgu_sd->pipe;

	imgu_pipe = &imgu->imgu_pipe[pipe];
	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		fmt->format = imgu_pipe->nodes[pad].pad_fmt;
	} else {
		mf = v4l2_subdev_get_try_format(sd, sd_state, pad);
		fmt->format = *mf;
	}

	return 0;
}

static int imgu_subdev_set_fmt(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state,
			       struct v4l2_subdev_format *fmt)
{
	struct imgu_media_pipe *imgu_pipe;
	struct imgu_device *imgu = v4l2_get_subdevdata(sd);
	struct imgu_v4l2_subdev *imgu_sd = container_of(sd,
							struct imgu_v4l2_subdev,
							subdev);
	struct v4l2_mbus_framefmt *mf;
	u32 pad = fmt->pad;
	unsigned int pipe = imgu_sd->pipe;

	dev_dbg(&imgu->pci_dev->dev, "set subdev %u pad %u fmt to [%ux%u]",
		pipe, pad, fmt->format.width, fmt->format.height);

	imgu_pipe = &imgu->imgu_pipe[pipe];
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		mf = v4l2_subdev_get_try_format(sd, sd_state, pad);
	else
		mf = &imgu_pipe->nodes[pad].pad_fmt;

	fmt->format.code = mf->code;
	/* Clamp the w and h based on the hardware capabilities */
	if (imgu_sd->subdev_pads[pad].flags & MEDIA_PAD_FL_SOURCE) {
		fmt->format.width = clamp(fmt->format.width,
					  IPU3_OUTPUT_MIN_WIDTH,
					  IPU3_OUTPUT_MAX_WIDTH);
		fmt->format.height = clamp(fmt->format.height,
					   IPU3_OUTPUT_MIN_HEIGHT,
					   IPU3_OUTPUT_MAX_HEIGHT);
	} else {
		fmt->format.width = clamp(fmt->format.width,
					  IPU3_INPUT_MIN_WIDTH,
					  IPU3_INPUT_MAX_WIDTH);
		fmt->format.height = clamp(fmt->format.height,
					   IPU3_INPUT_MIN_HEIGHT,
					   IPU3_INPUT_MAX_HEIGHT);
	}

	*mf = fmt->format;

	return 0;
}

static int imgu_subdev_get_selection(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *sd_state,
				     struct v4l2_subdev_selection *sel)
{
	struct imgu_v4l2_subdev *imgu_sd =
		container_of(sd, struct imgu_v4l2_subdev, subdev);

	if (sel->pad != IMGU_NODE_IN)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		if (sel->which == V4L2_SUBDEV_FORMAT_TRY)
			sel->r = *v4l2_subdev_get_try_crop(sd, sd_state,
							   sel->pad);
		else
			sel->r = imgu_sd->rect.eff;
		return 0;
	case V4L2_SEL_TGT_COMPOSE:
		if (sel->which == V4L2_SUBDEV_FORMAT_TRY)
			sel->r = *v4l2_subdev_get_try_compose(sd, sd_state,
							      sel->pad);
		else
			sel->r = imgu_sd->rect.bds;
		return 0;
	default:
		return -EINVAL;
	}
}

static int imgu_subdev_set_selection(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *sd_state,
				     struct v4l2_subdev_selection *sel)
{
	struct imgu_device *imgu = v4l2_get_subdevdata(sd);
	struct imgu_v4l2_subdev *imgu_sd = container_of(sd,
							struct imgu_v4l2_subdev,
							subdev);
	struct v4l2_rect *rect, *try_sel;

	dev_dbg(&imgu->pci_dev->dev,
		 "set subdev %u sel which %u target 0x%4x rect [%ux%u]",
		 imgu_sd->pipe, sel->which, sel->target,
		 sel->r.width, sel->r.height);

	if (sel->pad != IMGU_NODE_IN)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		try_sel = v4l2_subdev_get_try_crop(sd, sd_state, sel->pad);
		rect = &imgu_sd->rect.eff;
		break;
	case V4L2_SEL_TGT_COMPOSE:
		try_sel = v4l2_subdev_get_try_compose(sd, sd_state, sel->pad);
		rect = &imgu_sd->rect.bds;
		break;
	default:
		return -EINVAL;
	}

	if (sel->which == V4L2_SUBDEV_FORMAT_TRY)
		*try_sel = sel->r;
	else
		*rect = sel->r;

	return 0;
}

/******************** media_entity_operations ********************/

static int imgu_link_setup(struct media_entity *entity,
			   const struct media_pad *local,
			   const struct media_pad *remote, u32 flags)
{
	struct imgu_media_pipe *imgu_pipe;
	struct v4l2_subdev *sd = container_of(entity, struct v4l2_subdev,
					      entity);
	struct imgu_device *imgu = v4l2_get_subdevdata(sd);
	struct imgu_v4l2_subdev *imgu_sd = container_of(sd,
							struct imgu_v4l2_subdev,
							subdev);
	unsigned int pipe = imgu_sd->pipe;
	u32 pad = local->index;

	WARN_ON(pad >= IMGU_NODE_NUM);

	dev_dbg(&imgu->pci_dev->dev, "pipe %u pad %u is %s", pipe, pad,
		 flags & MEDIA_LNK_FL_ENABLED ? "enabled" : "disabled");

	imgu_pipe = &imgu->imgu_pipe[pipe];
	imgu_pipe->nodes[pad].enabled = flags & MEDIA_LNK_FL_ENABLED;

	/* enable input node to enable the pipe */
	if (pad != IMGU_NODE_IN)
		return 0;

	if (flags & MEDIA_LNK_FL_ENABLED)
		__set_bit(pipe, imgu->css.enabled_pipes);
	else
		__clear_bit(pipe, imgu->css.enabled_pipes);

	dev_dbg(&imgu->pci_dev->dev, "pipe %u is %s", pipe,
		 flags & MEDIA_LNK_FL_ENABLED ? "enabled" : "disabled");

	return 0;
}

/******************** vb2_ops ********************/

static int imgu_vb2_buf_init(struct vb2_buffer *vb)
{
	struct sg_table *sg = vb2_dma_sg_plane_desc(vb, 0);
	struct imgu_device *imgu = vb2_get_drv_priv(vb->vb2_queue);
	struct imgu_buffer *buf = container_of(vb,
		struct imgu_buffer, vid_buf.vbb.vb2_buf);
	struct imgu_video_device *node =
		container_of(vb->vb2_queue, struct imgu_video_device, vbq);
	unsigned int queue = imgu_node_to_queue(node->id);

	if (queue == IPU3_CSS_QUEUE_PARAMS)
		return 0;

	return imgu_dmamap_map_sg(imgu, sg->sgl, sg->nents, &buf->map);
}

/* Called when each buffer is freed */
static void imgu_vb2_buf_cleanup(struct vb2_buffer *vb)
{
	struct imgu_device *imgu = vb2_get_drv_priv(vb->vb2_queue);
	struct imgu_buffer *buf = container_of(vb,
		struct imgu_buffer, vid_buf.vbb.vb2_buf);
	struct imgu_video_device *node =
		container_of(vb->vb2_queue, struct imgu_video_device, vbq);
	unsigned int queue = imgu_node_to_queue(node->id);

	if (queue == IPU3_CSS_QUEUE_PARAMS)
		return;

	imgu_dmamap_unmap(imgu, &buf->map);
}

/* Transfer buffer ownership to me */
static void imgu_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct imgu_device *imgu = vb2_get_drv_priv(vb->vb2_queue);
	struct imgu_video_device *node =
		container_of(vb->vb2_queue, struct imgu_video_device, vbq);
	unsigned int queue = imgu_node_to_queue(node->id);
	struct imgu_buffer *buf = container_of(vb, struct imgu_buffer,
					       vid_buf.vbb.vb2_buf);
	unsigned long need_bytes;
	unsigned long payload = vb2_get_plane_payload(vb, 0);

	if (vb->vb2_queue->type == V4L2_BUF_TYPE_META_CAPTURE ||
	    vb->vb2_queue->type == V4L2_BUF_TYPE_META_OUTPUT)
		need_bytes = node->vdev_fmt.fmt.meta.buffersize;
	else
		need_bytes = node->vdev_fmt.fmt.pix_mp.plane_fmt[0].sizeimage;

	if (queue == IPU3_CSS_QUEUE_PARAMS && payload && payload < need_bytes) {
		dev_err(&imgu->pci_dev->dev, "invalid data size for params.");
		vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
		return;
	}

	mutex_lock(&imgu->lock);
	if (queue != IPU3_CSS_QUEUE_PARAMS)
		imgu_css_buf_init(&buf->css_buf, queue, buf->map.daddr);

	list_add_tail(&buf->vid_buf.list, &node->buffers);
	mutex_unlock(&imgu->lock);

	vb2_set_plane_payload(vb, 0, need_bytes);

	mutex_lock(&imgu->streaming_lock);
	if (imgu->streaming)
		imgu_queue_buffers(imgu, false, node->pipe);
	mutex_unlock(&imgu->streaming_lock);

	dev_dbg(&imgu->pci_dev->dev, "%s for pipe %u node %u", __func__,
		node->pipe, node->id);
}

static int imgu_vb2_queue_setup(struct vb2_queue *vq,
				unsigned int *num_buffers,
				unsigned int *num_planes,
				unsigned int sizes[],
				struct device *alloc_devs[])
{
	struct imgu_device *imgu = vb2_get_drv_priv(vq);
	struct imgu_video_device *node =
		container_of(vq, struct imgu_video_device, vbq);
	const struct v4l2_format *fmt = &node->vdev_fmt;
	unsigned int size;

	*num_buffers = clamp_val(*num_buffers, 1, VB2_MAX_FRAME);
	alloc_devs[0] = &imgu->pci_dev->dev;

	if (vq->type == V4L2_BUF_TYPE_META_CAPTURE ||
	    vq->type == V4L2_BUF_TYPE_META_OUTPUT)
		size = fmt->fmt.meta.buffersize;
	else
		size = fmt->fmt.pix_mp.plane_fmt[0].sizeimage;

	if (*num_planes) {
		if (sizes[0] < size)
			return -EINVAL;
		size = sizes[0];
	}

	*num_planes = 1;
	sizes[0] = size;

	/* Initialize buffer queue */
	INIT_LIST_HEAD(&node->buffers);

	return 0;
}

/* Check if all enabled video nodes are streaming, exception ignored */
static bool imgu_all_nodes_streaming(struct imgu_device *imgu,
				     struct imgu_video_device *except)
{
	unsigned int i, pipe, p;
	struct imgu_video_device *node;
	struct device *dev = &imgu->pci_dev->dev;

	pipe = except->pipe;
	if (!test_bit(pipe, imgu->css.enabled_pipes)) {
		dev_warn(&imgu->pci_dev->dev,
			 "pipe %u link is not ready yet", pipe);
		return false;
	}

	for_each_set_bit(p, imgu->css.enabled_pipes, IMGU_MAX_PIPE_NUM) {
		for (i = 0; i < IMGU_NODE_NUM; i++) {
			node = &imgu->imgu_pipe[p].nodes[i];
			dev_dbg(dev, "%s pipe %u queue %u name %s enabled = %u",
				__func__, p, i, node->name, node->enabled);
			if (node == except)
				continue;
			if (node->enabled && !vb2_start_streaming_called(&node->vbq))
				return false;
		}
	}

	return true;
}

static void imgu_return_all_buffers(struct imgu_device *imgu,
				    struct imgu_video_device *node,
				    enum vb2_buffer_state state)
{
	struct imgu_vb2_buffer *b, *b0;

	/* Return all buffers */
	mutex_lock(&imgu->lock);
	list_for_each_entry_safe(b, b0, &node->buffers, list) {
		list_del(&b->list);
		vb2_buffer_done(&b->vbb.vb2_buf, state);
	}
	mutex_unlock(&imgu->lock);
}

static int imgu_vb2_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct imgu_media_pipe *imgu_pipe;
	struct imgu_device *imgu = vb2_get_drv_priv(vq);
	struct device *dev = &imgu->pci_dev->dev;
	struct imgu_video_device *node =
		container_of(vq, struct imgu_video_device, vbq);
	int r;
	unsigned int pipe;

	dev_dbg(dev, "%s node name %s pipe %u id %u", __func__,
		node->name, node->pipe, node->id);

	mutex_lock(&imgu->streaming_lock);
	if (imgu->streaming) {
		r = -EBUSY;
		mutex_unlock(&imgu->streaming_lock);
		goto fail_return_bufs;
	}
	mutex_unlock(&imgu->streaming_lock);

	if (!node->enabled) {
		dev_err(dev, "IMGU node is not enabled");
		r = -EINVAL;
		goto fail_return_bufs;
	}

	pipe = node->pipe;
	imgu_pipe = &imgu->imgu_pipe[pipe];
	r = media_pipeline_start(&node->vdev.entity, &imgu_pipe->pipeline);
	if (r < 0)
		goto fail_return_bufs;

	if (!imgu_all_nodes_streaming(imgu, node))
		return 0;

	for_each_set_bit(pipe, imgu->css.enabled_pipes, IMGU_MAX_PIPE_NUM) {
		r = v4l2_subdev_call(&imgu->imgu_pipe[pipe].imgu_sd.subdev,
				     video, s_stream, 1);
		if (r < 0)
			goto fail_stop_pipeline;
	}

	/* Start streaming of the whole pipeline now */
	dev_dbg(dev, "IMGU streaming is ready to start");
	mutex_lock(&imgu->streaming_lock);
	r = imgu_s_stream(imgu, true);
	if (!r)
		imgu->streaming = true;
	mutex_unlock(&imgu->streaming_lock);

	return 0;

fail_stop_pipeline:
	media_pipeline_stop(&node->vdev.entity);
fail_return_bufs:
	imgu_return_all_buffers(imgu, node, VB2_BUF_STATE_QUEUED);

	return r;
}

static void imgu_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct imgu_media_pipe *imgu_pipe;
	struct imgu_device *imgu = vb2_get_drv_priv(vq);
	struct device *dev = &imgu->pci_dev->dev;
	struct imgu_video_device *node =
		container_of(vq, struct imgu_video_device, vbq);
	int r;
	unsigned int pipe;

	WARN_ON(!node->enabled);

	pipe = node->pipe;
	dev_dbg(dev, "Try to stream off node [%u][%u]", pipe, node->id);
	imgu_pipe = &imgu->imgu_pipe[pipe];
	r = v4l2_subdev_call(&imgu_pipe->imgu_sd.subdev, video, s_stream, 0);
	if (r)
		dev_err(&imgu->pci_dev->dev,
			"failed to stop subdev streaming\n");

	mutex_lock(&imgu->streaming_lock);
	/* Was this the first node with streaming disabled? */
	if (imgu->streaming && imgu_all_nodes_streaming(imgu, node)) {
		/* Yes, really stop streaming now */
		dev_dbg(dev, "IMGU streaming is ready to stop");
		r = imgu_s_stream(imgu, false);
		if (!r)
			imgu->streaming = false;
	}

	imgu_return_all_buffers(imgu, node, VB2_BUF_STATE_ERROR);
	mutex_unlock(&imgu->streaming_lock);

	media_pipeline_stop(&node->vdev.entity);
}

/******************** v4l2_ioctl_ops ********************/

#define VID_CAPTURE	0
#define VID_OUTPUT	1
#define DEF_VID_CAPTURE	0
#define DEF_VID_OUTPUT	1

struct imgu_fmt {
	u32	fourcc;
	u16	type; /* VID_CAPTURE or VID_OUTPUT not both */
};

/* format descriptions for capture and preview */
static const struct imgu_fmt formats[] = {
	{ V4L2_PIX_FMT_NV12, VID_CAPTURE },
	{ V4L2_PIX_FMT_IPU3_SGRBG10, VID_OUTPUT },
	{ V4L2_PIX_FMT_IPU3_SBGGR10, VID_OUTPUT },
	{ V4L2_PIX_FMT_IPU3_SGBRG10, VID_OUTPUT },
	{ V4L2_PIX_FMT_IPU3_SRGGB10, VID_OUTPUT },
};

/* Find the first matched format, return default if not found */
static const struct imgu_fmt *find_format(struct v4l2_format *f, u32 type)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		if (formats[i].fourcc == f->fmt.pix_mp.pixelformat &&
		    formats[i].type == type)
			return &formats[i];
	}

	return type == VID_CAPTURE ? &formats[DEF_VID_CAPTURE] :
				     &formats[DEF_VID_OUTPUT];
}

static int imgu_vidioc_querycap(struct file *file, void *fh,
				struct v4l2_capability *cap)
{
	struct imgu_device *imgu = video_drvdata(file);

	strscpy(cap->driver, IMGU_NAME, sizeof(cap->driver));
	strscpy(cap->card, IMGU_NAME, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "PCI:%s",
		 pci_name(imgu->pci_dev));

	return 0;
}

static int enum_fmts(struct v4l2_fmtdesc *f, u32 type)
{
	unsigned int i, j;

	if (f->mbus_code != 0 && f->mbus_code != MEDIA_BUS_FMT_FIXED)
		return -EINVAL;

	for (i = j = 0; i < ARRAY_SIZE(formats); ++i) {
		if (formats[i].type == type) {
			if (j == f->index)
				break;
			++j;
		}
	}

	if (i < ARRAY_SIZE(formats)) {
		f->pixelformat = formats[i].fourcc;
		return 0;
	}

	return -EINVAL;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	return enum_fmts(f, VID_CAPTURE);
}

static int vidioc_enum_fmt_vid_out(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	if (f->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	return enum_fmts(f, VID_OUTPUT);
}

/* Propagate forward always the format from the CIO2 subdev */
static int imgu_vidioc_g_fmt(struct file *file, void *fh,
			     struct v4l2_format *f)
{
	struct imgu_video_device *node = file_to_intel_imgu_node(file);

	f->fmt = node->vdev_fmt.fmt;

	return 0;
}

/*
 * Set input/output format. Unless it is just a try, this also resets
 * selections (ie. effective and BDS resolutions) to defaults.
 */
static int imgu_fmt(struct imgu_device *imgu, unsigned int pipe, int node,
		    struct v4l2_format *f, bool try)
{
	struct device *dev = &imgu->pci_dev->dev;
	struct v4l2_pix_format_mplane *fmts[IPU3_CSS_QUEUES] = { NULL };
	struct v4l2_rect *rects[IPU3_CSS_RECTS] = { NULL };
	struct v4l2_mbus_framefmt pad_fmt;
	unsigned int i, css_q;
	int ret;
	struct imgu_css_pipe *css_pipe = &imgu->css.pipes[pipe];
	struct imgu_media_pipe *imgu_pipe = &imgu->imgu_pipe[pipe];
	struct imgu_v4l2_subdev *imgu_sd = &imgu_pipe->imgu_sd;

	dev_dbg(dev, "set fmt node [%u][%u](try = %u)", pipe, node, try);

	for (i = 0; i < IMGU_NODE_NUM; i++)
		dev_dbg(dev, "IMGU pipe %u node %u enabled = %u",
			pipe, i, imgu_pipe->nodes[i].enabled);

	if (imgu_pipe->nodes[IMGU_NODE_VF].enabled)
		css_pipe->vf_output_en = true;

	if (atomic_read(&imgu_sd->running_mode) == IPU3_RUNNING_MODE_VIDEO)
		css_pipe->pipe_id = IPU3_CSS_PIPE_ID_VIDEO;
	else
		css_pipe->pipe_id = IPU3_CSS_PIPE_ID_CAPTURE;

	dev_dbg(dev, "IPU3 pipe %u pipe_id = %u", pipe, css_pipe->pipe_id);

	css_q = imgu_node_to_queue(node);
	for (i = 0; i < IPU3_CSS_QUEUES; i++) {
		unsigned int inode = imgu_map_node(imgu, i);

		/* Skip the meta node */
		if (inode == IMGU_NODE_STAT_3A || inode == IMGU_NODE_PARAMS)
			continue;

		/* CSS expects some format on OUT queue */
		if (i != IPU3_CSS_QUEUE_OUT &&
		    !imgu_pipe->nodes[inode].enabled && !try) {
			fmts[i] = NULL;
			continue;
		}

		if (i == css_q) {
			fmts[i] = &f->fmt.pix_mp;
			continue;
		}

		if (try) {
			fmts[i] = kmemdup(&imgu_pipe->nodes[inode].vdev_fmt.fmt.pix_mp,
					  sizeof(struct v4l2_pix_format_mplane),
					  GFP_KERNEL);
			if (!fmts[i]) {
				ret = -ENOMEM;
				goto out;
			}
		} else {
			fmts[i] = &imgu_pipe->nodes[inode].vdev_fmt.fmt.pix_mp;
		}

	}

	if (!try) {
		/* eff and bds res got by imgu_s_sel */
		struct imgu_v4l2_subdev *imgu_sd = &imgu_pipe->imgu_sd;

		rects[IPU3_CSS_RECT_EFFECTIVE] = &imgu_sd->rect.eff;
		rects[IPU3_CSS_RECT_BDS] = &imgu_sd->rect.bds;
		rects[IPU3_CSS_RECT_GDC] = &imgu_sd->rect.gdc;

		/* suppose that pad fmt was set by subdev s_fmt before */
		pad_fmt = imgu_pipe->nodes[IMGU_NODE_IN].pad_fmt;
		rects[IPU3_CSS_RECT_GDC]->width = pad_fmt.width;
		rects[IPU3_CSS_RECT_GDC]->height = pad_fmt.height;
	}

	if (!fmts[css_q]) {
		ret = -EINVAL;
		goto out;
	}

	if (try)
		ret = imgu_css_fmt_try(&imgu->css, fmts, rects, pipe);
	else
		ret = imgu_css_fmt_set(&imgu->css, fmts, rects, pipe);

	/* ret is the binary number in the firmware blob */
	if (ret < 0)
		goto out;

	/*
	 * imgu doesn't set the node to the value given by user
	 * before we return success from this function, so set it here.
	 */
	if (!try)
		imgu_pipe->nodes[node].vdev_fmt.fmt.pix_mp = f->fmt.pix_mp;

out:
	if (try) {
		for (i = 0; i < IPU3_CSS_QUEUES; i++)
			if (i != css_q)
				kfree(fmts[i]);
	}

	return ret;
}

static int imgu_try_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pixm = &f->fmt.pix_mp;
	const struct imgu_fmt *fmt;

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		fmt = find_format(f, VID_CAPTURE);
	else if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		fmt = find_format(f, VID_OUTPUT);
	else
		return -EINVAL;

	pixm->pixelformat = fmt->fourcc;

	return 0;
}

static int imgu_vidioc_try_fmt(struct file *file, void *fh,
			       struct v4l2_format *f)
{
	struct imgu_device *imgu = video_drvdata(file);
	struct device *dev = &imgu->pci_dev->dev;
	struct imgu_video_device *node = file_to_intel_imgu_node(file);
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	int r;

	dev_dbg(dev, "%s [%ux%u] for node %u\n", __func__,
		pix_mp->width, pix_mp->height, node->id);

	r = imgu_try_fmt(file, fh, f);
	if (r)
		return r;

	return imgu_fmt(imgu, node->pipe, node->id, f, true);
}

static int imgu_vidioc_s_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct imgu_device *imgu = video_drvdata(file);
	struct device *dev = &imgu->pci_dev->dev;
	struct imgu_video_device *node = file_to_intel_imgu_node(file);
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	int r;

	dev_dbg(dev, "%s [%ux%u] for node %u\n", __func__,
		pix_mp->width, pix_mp->height, node->id);

	r = imgu_try_fmt(file, fh, f);
	if (r)
		return r;

	return imgu_fmt(imgu, node->pipe, node->id, f, false);
}

struct imgu_meta_fmt {
	__u32 fourcc;
	char *name;
};

/* From drivers/media/v4l2-core/v4l2-ioctl.c */
static const struct imgu_meta_fmt meta_fmts[] = {
	{ V4L2_META_FMT_IPU3_PARAMS, "IPU3 processing parameters" },
	{ V4L2_META_FMT_IPU3_STAT_3A, "IPU3 3A statistics" },
};

static int imgu_meta_enum_format(struct file *file, void *fh,
				 struct v4l2_fmtdesc *fmt)
{
	struct imgu_video_device *node = file_to_intel_imgu_node(file);
	unsigned int i = fmt->type == V4L2_BUF_TYPE_META_OUTPUT ? 0 : 1;

	/* Each node is dedicated to only one meta format */
	if (fmt->index > 0 || fmt->type != node->vbq.type)
		return -EINVAL;

	if (fmt->mbus_code != 0 && fmt->mbus_code != MEDIA_BUS_FMT_FIXED)
		return -EINVAL;

	strscpy(fmt->description, meta_fmts[i].name, sizeof(fmt->description));
	fmt->pixelformat = meta_fmts[i].fourcc;

	return 0;
}

static int imgu_vidioc_g_meta_fmt(struct file *file, void *fh,
				  struct v4l2_format *f)
{
	struct imgu_video_device *node = file_to_intel_imgu_node(file);

	if (f->type != node->vbq.type)
		return -EINVAL;

	f->fmt = node->vdev_fmt.fmt;

	return 0;
}

/******************** function pointers ********************/

static struct v4l2_subdev_internal_ops imgu_subdev_internal_ops = {
	.open = imgu_subdev_open,
};

static const struct v4l2_subdev_core_ops imgu_subdev_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops imgu_subdev_video_ops = {
	.s_stream = imgu_subdev_s_stream,
};

static const struct v4l2_subdev_pad_ops imgu_subdev_pad_ops = {
	.link_validate = v4l2_subdev_link_validate_default,
	.get_fmt = imgu_subdev_get_fmt,
	.set_fmt = imgu_subdev_set_fmt,
	.get_selection = imgu_subdev_get_selection,
	.set_selection = imgu_subdev_set_selection,
};

static const struct v4l2_subdev_ops imgu_subdev_ops = {
	.core = &imgu_subdev_core_ops,
	.video = &imgu_subdev_video_ops,
	.pad = &imgu_subdev_pad_ops,
};

static const struct media_entity_operations imgu_media_ops = {
	.link_setup = imgu_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

/****************** vb2_ops of the Q ********************/

static const struct vb2_ops imgu_vb2_ops = {
	.buf_init = imgu_vb2_buf_init,
	.buf_cleanup = imgu_vb2_buf_cleanup,
	.buf_queue = imgu_vb2_buf_queue,
	.queue_setup = imgu_vb2_queue_setup,
	.start_streaming = imgu_vb2_start_streaming,
	.stop_streaming = imgu_vb2_stop_streaming,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
};

/****************** v4l2_file_operations *****************/

static const struct v4l2_file_operations imgu_v4l2_fops = {
	.unlocked_ioctl = video_ioctl2,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
};

/******************** v4l2_ioctl_ops ********************/

static const struct v4l2_ioctl_ops imgu_v4l2_ioctl_ops = {
	.vidioc_querycap = imgu_vidioc_querycap,

	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap_mplane = imgu_vidioc_g_fmt,
	.vidioc_s_fmt_vid_cap_mplane = imgu_vidioc_s_fmt,
	.vidioc_try_fmt_vid_cap_mplane = imgu_vidioc_try_fmt,

	.vidioc_enum_fmt_vid_out = vidioc_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_out_mplane = imgu_vidioc_g_fmt,
	.vidioc_s_fmt_vid_out_mplane = imgu_vidioc_s_fmt,
	.vidioc_try_fmt_vid_out_mplane = imgu_vidioc_try_fmt,

	/* buffer queue management */
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_expbuf = vb2_ioctl_expbuf,
};

static const struct v4l2_ioctl_ops imgu_v4l2_meta_ioctl_ops = {
	.vidioc_querycap = imgu_vidioc_querycap,

	/* meta capture */
	.vidioc_enum_fmt_meta_cap = imgu_meta_enum_format,
	.vidioc_g_fmt_meta_cap = imgu_vidioc_g_meta_fmt,
	.vidioc_s_fmt_meta_cap = imgu_vidioc_g_meta_fmt,
	.vidioc_try_fmt_meta_cap = imgu_vidioc_g_meta_fmt,

	/* meta output */
	.vidioc_enum_fmt_meta_out = imgu_meta_enum_format,
	.vidioc_g_fmt_meta_out = imgu_vidioc_g_meta_fmt,
	.vidioc_s_fmt_meta_out = imgu_vidioc_g_meta_fmt,
	.vidioc_try_fmt_meta_out = imgu_vidioc_g_meta_fmt,

	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_expbuf = vb2_ioctl_expbuf,
};

static int imgu_sd_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imgu_v4l2_subdev *imgu_sd =
		container_of(ctrl->handler, struct imgu_v4l2_subdev, ctrl_handler);
	struct imgu_device *imgu = v4l2_get_subdevdata(&imgu_sd->subdev);
	struct device *dev = &imgu->pci_dev->dev;

	dev_dbg(dev, "set val %d to ctrl 0x%8x for subdev %u",
		ctrl->val, ctrl->id, imgu_sd->pipe);

	switch (ctrl->id) {
	case V4L2_CID_INTEL_IPU3_MODE:
		atomic_set(&imgu_sd->running_mode, ctrl->val);
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct v4l2_ctrl_ops imgu_subdev_ctrl_ops = {
	.s_ctrl = imgu_sd_s_ctrl,
};

static const char * const imgu_ctrl_mode_strings[] = {
	"Video mode",
	"Still mode",
};

static const struct v4l2_ctrl_config imgu_subdev_ctrl_mode = {
	.ops = &imgu_subdev_ctrl_ops,
	.id = V4L2_CID_INTEL_IPU3_MODE,
	.name = "IPU3 Pipe Mode",
	.type = V4L2_CTRL_TYPE_MENU,
	.max = ARRAY_SIZE(imgu_ctrl_mode_strings) - 1,
	.def = IPU3_RUNNING_MODE_VIDEO,
	.qmenu = imgu_ctrl_mode_strings,
};

/******************** Framework registration ********************/

/* helper function to config node's video properties */
static void imgu_node_to_v4l2(u32 node, struct video_device *vdev,
			      struct v4l2_format *f)
{
	u32 cap;

	/* Should not happen */
	WARN_ON(node >= IMGU_NODE_NUM);

	switch (node) {
	case IMGU_NODE_IN:
		cap = V4L2_CAP_VIDEO_OUTPUT_MPLANE;
		f->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		vdev->ioctl_ops = &imgu_v4l2_ioctl_ops;
		break;
	case IMGU_NODE_PARAMS:
		cap = V4L2_CAP_META_OUTPUT;
		f->type = V4L2_BUF_TYPE_META_OUTPUT;
		f->fmt.meta.dataformat = V4L2_META_FMT_IPU3_PARAMS;
		vdev->ioctl_ops = &imgu_v4l2_meta_ioctl_ops;
		imgu_css_meta_fmt_set(&f->fmt.meta);
		break;
	case IMGU_NODE_STAT_3A:
		cap = V4L2_CAP_META_CAPTURE;
		f->type = V4L2_BUF_TYPE_META_CAPTURE;
		f->fmt.meta.dataformat = V4L2_META_FMT_IPU3_STAT_3A;
		vdev->ioctl_ops = &imgu_v4l2_meta_ioctl_ops;
		imgu_css_meta_fmt_set(&f->fmt.meta);
		break;
	default:
		cap = V4L2_CAP_VIDEO_CAPTURE_MPLANE;
		f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		vdev->ioctl_ops = &imgu_v4l2_ioctl_ops;
	}

	vdev->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_IO_MC | cap;
}

static int imgu_v4l2_subdev_register(struct imgu_device *imgu,
				     struct imgu_v4l2_subdev *imgu_sd,
				     unsigned int pipe)
{
	int i, r;
	struct v4l2_ctrl_handler *hdl = &imgu_sd->ctrl_handler;
	struct imgu_media_pipe *imgu_pipe = &imgu->imgu_pipe[pipe];

	/* Initialize subdev media entity */
	r = media_entity_pads_init(&imgu_sd->subdev.entity, IMGU_NODE_NUM,
				   imgu_sd->subdev_pads);
	if (r) {
		dev_err(&imgu->pci_dev->dev,
			"failed initialize subdev media entity (%d)\n", r);
		return r;
	}
	imgu_sd->subdev.entity.ops = &imgu_media_ops;
	for (i = 0; i < IMGU_NODE_NUM; i++) {
		imgu_sd->subdev_pads[i].flags = imgu_pipe->nodes[i].output ?
			MEDIA_PAD_FL_SINK : MEDIA_PAD_FL_SOURCE;
	}

	/* Initialize subdev */
	v4l2_subdev_init(&imgu_sd->subdev, &imgu_subdev_ops);
	imgu_sd->subdev.entity.function = MEDIA_ENT_F_PROC_VIDEO_STATISTICS;
	imgu_sd->subdev.internal_ops = &imgu_subdev_internal_ops;
	imgu_sd->subdev.flags = V4L2_SUBDEV_FL_HAS_DEVNODE |
				V4L2_SUBDEV_FL_HAS_EVENTS;
	snprintf(imgu_sd->subdev.name, sizeof(imgu_sd->subdev.name),
		 "%s %u", IMGU_NAME, pipe);
	v4l2_set_subdevdata(&imgu_sd->subdev, imgu);
	atomic_set(&imgu_sd->running_mode, IPU3_RUNNING_MODE_VIDEO);
	v4l2_ctrl_handler_init(hdl, 1);
	imgu_sd->subdev.ctrl_handler = hdl;
	imgu_sd->ctrl = v4l2_ctrl_new_custom(hdl, &imgu_subdev_ctrl_mode, NULL);
	if (hdl->error) {
		r = hdl->error;
		dev_err(&imgu->pci_dev->dev,
			"failed to create subdev v4l2 ctrl with err %d", r);
		goto fail_subdev;
	}
	r = v4l2_device_register_subdev(&imgu->v4l2_dev, &imgu_sd->subdev);
	if (r) {
		dev_err(&imgu->pci_dev->dev,
			"failed initialize subdev (%d)\n", r);
		goto fail_subdev;
	}

	imgu_sd->pipe = pipe;
	return 0;

fail_subdev:
	v4l2_ctrl_handler_free(imgu_sd->subdev.ctrl_handler);
	media_entity_cleanup(&imgu_sd->subdev.entity);

	return r;
}

static int imgu_v4l2_node_setup(struct imgu_device *imgu, unsigned int pipe,
				int node_num)
{
	int r;
	u32 flags;
	struct v4l2_mbus_framefmt def_bus_fmt = { 0 };
	struct v4l2_pix_format_mplane def_pix_fmt = { 0 };
	struct device *dev = &imgu->pci_dev->dev;
	struct imgu_media_pipe *imgu_pipe = &imgu->imgu_pipe[pipe];
	struct v4l2_subdev *sd = &imgu_pipe->imgu_sd.subdev;
	struct imgu_video_device *node = &imgu_pipe->nodes[node_num];
	struct video_device *vdev = &node->vdev;
	struct vb2_queue *vbq = &node->vbq;

	/* Initialize formats to default values */
	def_bus_fmt.width = 1920;
	def_bus_fmt.height = 1080;
	def_bus_fmt.code = MEDIA_BUS_FMT_FIXED;
	def_bus_fmt.field = V4L2_FIELD_NONE;
	def_bus_fmt.colorspace = V4L2_COLORSPACE_RAW;
	def_bus_fmt.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	def_bus_fmt.quantization = V4L2_QUANTIZATION_DEFAULT;
	def_bus_fmt.xfer_func = V4L2_XFER_FUNC_DEFAULT;

	def_pix_fmt.width = def_bus_fmt.width;
	def_pix_fmt.height = def_bus_fmt.height;
	def_pix_fmt.field = def_bus_fmt.field;
	def_pix_fmt.num_planes = 1;
	def_pix_fmt.plane_fmt[0].bytesperline = def_pix_fmt.width * 2;
	def_pix_fmt.plane_fmt[0].sizeimage =
		def_pix_fmt.height * def_pix_fmt.plane_fmt[0].bytesperline;
	def_pix_fmt.flags = 0;
	def_pix_fmt.colorspace = def_bus_fmt.colorspace;
	def_pix_fmt.ycbcr_enc = def_bus_fmt.ycbcr_enc;
	def_pix_fmt.quantization = def_bus_fmt.quantization;
	def_pix_fmt.xfer_func = def_bus_fmt.xfer_func;

	/* Initialize miscellaneous variables */
	mutex_init(&node->lock);
	INIT_LIST_HEAD(&node->buffers);

	/* Initialize formats to default values */
	node->pad_fmt = def_bus_fmt;
	node->id = node_num;
	node->pipe = pipe;
	imgu_node_to_v4l2(node_num, vdev, &node->vdev_fmt);
	if (node->vdev_fmt.type ==
	    V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE ||
	    node->vdev_fmt.type ==
	    V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		def_pix_fmt.pixelformat = node->output ?
			V4L2_PIX_FMT_IPU3_SGRBG10 :
			V4L2_PIX_FMT_NV12;
		node->vdev_fmt.fmt.pix_mp = def_pix_fmt;
	}

	/* Initialize media entities */
	r = media_entity_pads_init(&vdev->entity, 1, &node->vdev_pad);
	if (r) {
		dev_err(dev, "failed initialize media entity (%d)\n", r);
		mutex_destroy(&node->lock);
		return r;
	}
	node->vdev_pad.flags = node->output ?
		MEDIA_PAD_FL_SOURCE : MEDIA_PAD_FL_SINK;
	vdev->entity.ops = NULL;

	/* Initialize vbq */
	vbq->type = node->vdev_fmt.type;
	vbq->io_modes = VB2_USERPTR | VB2_MMAP | VB2_DMABUF;
	vbq->ops = &imgu_vb2_ops;
	vbq->mem_ops = &vb2_dma_sg_memops;
	if (imgu->buf_struct_size <= 0)
		imgu->buf_struct_size =
			sizeof(struct imgu_vb2_buffer);
	vbq->buf_struct_size = imgu->buf_struct_size;
	vbq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	/* can streamon w/o buffers */
	vbq->min_buffers_needed = 0;
	vbq->drv_priv = imgu;
	vbq->lock = &node->lock;
	r = vb2_queue_init(vbq);
	if (r) {
		dev_err(dev, "failed to initialize video queue (%d)", r);
		media_entity_cleanup(&vdev->entity);
		return r;
	}

	/* Initialize vdev */
	snprintf(vdev->name, sizeof(vdev->name), "%s %u %s",
		 IMGU_NAME, pipe, node->name);
	vdev->release = video_device_release_empty;
	vdev->fops = &imgu_v4l2_fops;
	vdev->lock = &node->lock;
	vdev->v4l2_dev = &imgu->v4l2_dev;
	vdev->queue = &node->vbq;
	vdev->vfl_dir = node->output ? VFL_DIR_TX : VFL_DIR_RX;
	video_set_drvdata(vdev, imgu);
	r = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (r) {
		dev_err(dev, "failed to register video device (%d)", r);
		media_entity_cleanup(&vdev->entity);
		return r;
	}

	/* Create link between video node and the subdev pad */
	flags = 0;
	if (node->enabled)
		flags |= MEDIA_LNK_FL_ENABLED;
	if (node->output) {
		r = media_create_pad_link(&vdev->entity, 0, &sd->entity,
					  node_num, flags);
	} else {
		if (node->id == IMGU_NODE_OUT) {
			flags |= MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE;
			node->enabled = true;
		}

		r = media_create_pad_link(&sd->entity, node_num, &vdev->entity,
					  0, flags);
	}
	if (r) {
		dev_err(dev, "failed to create pad link (%d)", r);
		video_unregister_device(vdev);
		return r;
	}

	return 0;
}

static void imgu_v4l2_nodes_cleanup_pipe(struct imgu_device *imgu,
					 unsigned int pipe, int node)
{
	int i;
	struct imgu_media_pipe *imgu_pipe = &imgu->imgu_pipe[pipe];

	for (i = 0; i < node; i++) {
		video_unregister_device(&imgu_pipe->nodes[i].vdev);
		media_entity_cleanup(&imgu_pipe->nodes[i].vdev.entity);
		mutex_destroy(&imgu_pipe->nodes[i].lock);
	}
}

static int imgu_v4l2_nodes_setup_pipe(struct imgu_device *imgu, int pipe)
{
	int i;

	for (i = 0; i < IMGU_NODE_NUM; i++) {
		int r = imgu_v4l2_node_setup(imgu, pipe, i);

		if (r) {
			imgu_v4l2_nodes_cleanup_pipe(imgu, pipe, i);
			return r;
		}
	}
	return 0;
}

static void imgu_v4l2_subdev_cleanup(struct imgu_device *imgu, unsigned int i)
{
	struct imgu_media_pipe *imgu_pipe = &imgu->imgu_pipe[i];

	v4l2_device_unregister_subdev(&imgu_pipe->imgu_sd.subdev);
	v4l2_ctrl_handler_free(imgu_pipe->imgu_sd.subdev.ctrl_handler);
	media_entity_cleanup(&imgu_pipe->imgu_sd.subdev.entity);
}

static void imgu_v4l2_cleanup_pipes(struct imgu_device *imgu, unsigned int pipe)
{
	int i;

	for (i = 0; i < pipe; i++) {
		imgu_v4l2_nodes_cleanup_pipe(imgu, i, IMGU_NODE_NUM);
		imgu_v4l2_subdev_cleanup(imgu, i);
	}
}

static int imgu_v4l2_register_pipes(struct imgu_device *imgu)
{
	struct imgu_media_pipe *imgu_pipe;
	int i, r;

	for (i = 0; i < IMGU_MAX_PIPE_NUM; i++) {
		imgu_pipe = &imgu->imgu_pipe[i];
		r = imgu_v4l2_subdev_register(imgu, &imgu_pipe->imgu_sd, i);
		if (r) {
			dev_err(&imgu->pci_dev->dev,
				"failed to register subdev%u ret (%d)\n", i, r);
			goto pipes_cleanup;
		}
		r = imgu_v4l2_nodes_setup_pipe(imgu, i);
		if (r) {
			imgu_v4l2_subdev_cleanup(imgu, i);
			goto pipes_cleanup;
		}
	}

	return 0;

pipes_cleanup:
	imgu_v4l2_cleanup_pipes(imgu, i);
	return r;
}

int imgu_v4l2_register(struct imgu_device *imgu)
{
	int r;

	/* Initialize miscellaneous variables */
	imgu->streaming = false;

	/* Set up media device */
	media_device_pci_init(&imgu->media_dev, imgu->pci_dev, IMGU_NAME);

	/* Set up v4l2 device */
	imgu->v4l2_dev.mdev = &imgu->media_dev;
	imgu->v4l2_dev.ctrl_handler = NULL;
	r = v4l2_device_register(&imgu->pci_dev->dev, &imgu->v4l2_dev);
	if (r) {
		dev_err(&imgu->pci_dev->dev,
			"failed to register V4L2 device (%d)\n", r);
		goto fail_v4l2_dev;
	}

	r = imgu_v4l2_register_pipes(imgu);
	if (r) {
		dev_err(&imgu->pci_dev->dev,
			"failed to register pipes (%d)\n", r);
		goto fail_v4l2_pipes;
	}

	r = v4l2_device_register_subdev_nodes(&imgu->v4l2_dev);
	if (r) {
		dev_err(&imgu->pci_dev->dev,
			"failed to register subdevs (%d)\n", r);
		goto fail_subdevs;
	}

	r = media_device_register(&imgu->media_dev);
	if (r) {
		dev_err(&imgu->pci_dev->dev,
			"failed to register media device (%d)\n", r);
		goto fail_subdevs;
	}

	return 0;

fail_subdevs:
	imgu_v4l2_cleanup_pipes(imgu, IMGU_MAX_PIPE_NUM);
fail_v4l2_pipes:
	v4l2_device_unregister(&imgu->v4l2_dev);
fail_v4l2_dev:
	media_device_cleanup(&imgu->media_dev);

	return r;
}

int imgu_v4l2_unregister(struct imgu_device *imgu)
{
	media_device_unregister(&imgu->media_dev);
	imgu_v4l2_cleanup_pipes(imgu, IMGU_MAX_PIPE_NUM);
	v4l2_device_unregister(&imgu->v4l2_dev);
	media_device_cleanup(&imgu->media_dev);

	return 0;
}

void imgu_v4l2_buffer_done(struct vb2_buffer *vb,
			   enum vb2_buffer_state state)
{
	struct imgu_vb2_buffer *b =
		container_of(vb, struct imgu_vb2_buffer, vbb.vb2_buf);

	list_del(&b->list);
	vb2_buffer_done(&b->vbb.vb2_buf, state);
}
