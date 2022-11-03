// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2011-2018 Magewell Electronics Co., Ltd. (Nanjing)
 * All rights reserved.
 * Author: Yong Deng <yong.deng@magewell.com>
 */

#include <linux/of.h>

#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-v4l2.h>

#include "sun6i_csi.h"
#include "sun6i_csi_capture.h"

/* This is got from BSP sources. */
#define MIN_WIDTH	(32)
#define MIN_HEIGHT	(32)
#define MAX_WIDTH	(4800)
#define MAX_HEIGHT	(4800)

/* Helpers */

static struct v4l2_subdev *
sun6i_csi_capture_remote_subdev(struct sun6i_csi_capture *capture, u32 *pad)
{
	struct media_pad *remote;

	remote = media_pad_remote_pad_first(&capture->pad);

	if (!remote || !is_media_entity_v4l2_subdev(remote->entity))
		return NULL;

	if (pad)
		*pad = remote->index;

	return media_entity_to_v4l2_subdev(remote->entity);
}

/* Format */

static const u32 sun6i_csi_capture_formats[] = {
	V4L2_PIX_FMT_SBGGR8,
	V4L2_PIX_FMT_SGBRG8,
	V4L2_PIX_FMT_SGRBG8,
	V4L2_PIX_FMT_SRGGB8,
	V4L2_PIX_FMT_SBGGR10,
	V4L2_PIX_FMT_SGBRG10,
	V4L2_PIX_FMT_SGRBG10,
	V4L2_PIX_FMT_SRGGB10,
	V4L2_PIX_FMT_SBGGR12,
	V4L2_PIX_FMT_SGBRG12,
	V4L2_PIX_FMT_SGRBG12,
	V4L2_PIX_FMT_SRGGB12,
	V4L2_PIX_FMT_YUYV,
	V4L2_PIX_FMT_YVYU,
	V4L2_PIX_FMT_UYVY,
	V4L2_PIX_FMT_VYUY,
	V4L2_PIX_FMT_NV12_16L16,
	V4L2_PIX_FMT_NV12,
	V4L2_PIX_FMT_NV21,
	V4L2_PIX_FMT_YUV420,
	V4L2_PIX_FMT_YVU420,
	V4L2_PIX_FMT_NV16,
	V4L2_PIX_FMT_NV61,
	V4L2_PIX_FMT_YUV422P,
	V4L2_PIX_FMT_RGB565,
	V4L2_PIX_FMT_RGB565X,
	V4L2_PIX_FMT_JPEG,
};

static bool sun6i_csi_capture_format_check(u32 format)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sun6i_csi_capture_formats); i++)
		if (sun6i_csi_capture_formats[i] == format)
			return true;

	return false;
}

/* Capture */

static void
sun6i_csi_capture_buffer_configure(struct sun6i_csi_device *csi_dev,
				   struct sun6i_csi_buffer *csi_buffer)
{
	struct vb2_buffer *vb2_buffer;
	dma_addr_t address;

	vb2_buffer = &csi_buffer->v4l2_buffer.vb2_buf;
	address = vb2_dma_contig_plane_dma_addr(vb2_buffer, 0);

	sun6i_csi_update_buf_addr(csi_dev, address);
}

static void sun6i_csi_capture_configure(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_capture *capture = &csi_dev->capture;
	struct sun6i_csi_config config = { 0 };

	config.pixelformat = capture->format.fmt.pix.pixelformat;
	config.code = capture->mbus_code;
	config.field = capture->format.fmt.pix.field;
	config.width = capture->format.fmt.pix.width;
	config.height = capture->format.fmt.pix.height;

	sun6i_csi_update_config(csi_dev, &config);
}

/* State */

static void sun6i_csi_capture_state_cleanup(struct sun6i_csi_device *csi_dev,
					    bool error)
{
	struct sun6i_csi_capture_state *state = &csi_dev->capture.state;
	struct sun6i_csi_buffer **csi_buffer_states[] = {
		&state->pending, &state->current, &state->complete,
	};
	struct sun6i_csi_buffer *csi_buffer;
	struct vb2_buffer *vb2_buffer;
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&state->lock, flags);

	for (i = 0; i < ARRAY_SIZE(csi_buffer_states); i++) {
		csi_buffer = *csi_buffer_states[i];
		if (!csi_buffer)
			continue;

		vb2_buffer = &csi_buffer->v4l2_buffer.vb2_buf;
		vb2_buffer_done(vb2_buffer, error ? VB2_BUF_STATE_ERROR :
				VB2_BUF_STATE_QUEUED);

		*csi_buffer_states[i] = NULL;
	}

	list_for_each_entry(csi_buffer, &state->queue, list) {
		vb2_buffer = &csi_buffer->v4l2_buffer.vb2_buf;
		vb2_buffer_done(vb2_buffer, error ? VB2_BUF_STATE_ERROR :
				VB2_BUF_STATE_QUEUED);
	}

	INIT_LIST_HEAD(&state->queue);

	spin_unlock_irqrestore(&state->lock, flags);
}

static void sun6i_csi_capture_state_update(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_capture_state *state = &csi_dev->capture.state;
	struct sun6i_csi_buffer *csi_buffer;
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);

	if (list_empty(&state->queue))
		goto complete;

	if (state->pending)
		goto complete;

	csi_buffer = list_first_entry(&state->queue, struct sun6i_csi_buffer,
				      list);

	sun6i_csi_capture_buffer_configure(csi_dev, csi_buffer);

	list_del(&csi_buffer->list);

	state->pending = csi_buffer;

complete:
	spin_unlock_irqrestore(&state->lock, flags);
}

static void sun6i_csi_capture_state_complete(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_capture_state *state = &csi_dev->capture.state;
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);

	if (!state->pending)
		goto complete;

	state->complete = state->current;
	state->current = state->pending;
	state->pending = NULL;

	if (state->complete) {
		struct sun6i_csi_buffer *csi_buffer = state->complete;
		struct vb2_buffer *vb2_buffer =
			&csi_buffer->v4l2_buffer.vb2_buf;

		vb2_buffer->timestamp = ktime_get_ns();
		csi_buffer->v4l2_buffer.sequence = state->sequence;

		vb2_buffer_done(vb2_buffer, VB2_BUF_STATE_DONE);

		state->complete = NULL;
	}

complete:
	spin_unlock_irqrestore(&state->lock, flags);
}

void sun6i_csi_capture_frame_done(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_capture_state *state = &csi_dev->capture.state;
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);
	state->sequence++;
	spin_unlock_irqrestore(&state->lock, flags);
}

void sun6i_csi_capture_sync(struct sun6i_csi_device *csi_dev)
{
	sun6i_csi_capture_state_complete(csi_dev);
	sun6i_csi_capture_state_update(csi_dev);
}

/* Queue */

static int sun6i_csi_capture_queue_setup(struct vb2_queue *queue,
					 unsigned int *buffers_count,
					 unsigned int *planes_count,
					 unsigned int sizes[],
					 struct device *alloc_devs[])
{
	struct sun6i_csi_device *csi_dev = vb2_get_drv_priv(queue);
	unsigned int size = csi_dev->capture.format.fmt.pix.sizeimage;

	if (*planes_count)
		return sizes[0] < size ? -EINVAL : 0;

	*planes_count = 1;
	sizes[0] = size;

	return 0;
}

static int sun6i_csi_capture_buffer_prepare(struct vb2_buffer *buffer)
{
	struct sun6i_csi_device *csi_dev = vb2_get_drv_priv(buffer->vb2_queue);
	struct sun6i_csi_capture *capture = &csi_dev->capture;
	struct v4l2_device *v4l2_dev = &csi_dev->v4l2.v4l2_dev;
	struct vb2_v4l2_buffer *v4l2_buffer = to_vb2_v4l2_buffer(buffer);
	unsigned long size = capture->format.fmt.pix.sizeimage;

	if (vb2_plane_size(buffer, 0) < size) {
		v4l2_err(v4l2_dev, "buffer too small (%lu < %lu)\n",
			 vb2_plane_size(buffer, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(buffer, 0, size);

	v4l2_buffer->field = capture->format.fmt.pix.field;

	return 0;
}

static void sun6i_csi_capture_buffer_queue(struct vb2_buffer *buffer)
{
	struct sun6i_csi_device *csi_dev = vb2_get_drv_priv(buffer->vb2_queue);
	struct sun6i_csi_capture_state *state = &csi_dev->capture.state;
	struct vb2_v4l2_buffer *v4l2_buffer = to_vb2_v4l2_buffer(buffer);
	struct sun6i_csi_buffer *csi_buffer =
		container_of(v4l2_buffer, struct sun6i_csi_buffer, v4l2_buffer);
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);
	list_add_tail(&csi_buffer->list, &state->queue);
	spin_unlock_irqrestore(&state->lock, flags);
}

static int sun6i_csi_capture_start_streaming(struct vb2_queue *queue,
					     unsigned int count)
{
	struct sun6i_csi_device *csi_dev = vb2_get_drv_priv(queue);
	struct sun6i_csi_capture *capture = &csi_dev->capture;
	struct sun6i_csi_capture_state *state = &capture->state;
	struct video_device *video_dev = &capture->video_dev;
	struct v4l2_subdev *subdev;
	int ret;

	state->sequence = 0;

	ret = video_device_pipeline_alloc_start(video_dev);
	if (ret < 0)
		goto error_state;

	if (capture->mbus_code == 0) {
		ret = -EINVAL;
		goto error_media_pipeline;
	}

	subdev = sun6i_csi_capture_remote_subdev(capture, NULL);
	if (!subdev) {
		ret = -EINVAL;
		goto error_media_pipeline;
	}

	/* Configure */

	sun6i_csi_capture_configure(csi_dev);

	/* State Update */

	sun6i_csi_capture_state_update(csi_dev);

	/* Enable */

	sun6i_csi_set_stream(csi_dev, true);

	ret = v4l2_subdev_call(subdev, video, s_stream, 1);
	if (ret && ret != -ENOIOCTLCMD)
		goto error_stream;

	return 0;

error_stream:
	sun6i_csi_set_stream(csi_dev, false);

error_media_pipeline:
	video_device_pipeline_stop(video_dev);

error_state:
	sun6i_csi_capture_state_cleanup(csi_dev, false);

	return ret;
}

static void sun6i_csi_capture_stop_streaming(struct vb2_queue *queue)
{
	struct sun6i_csi_device *csi_dev = vb2_get_drv_priv(queue);
	struct sun6i_csi_capture *capture = &csi_dev->capture;
	struct v4l2_subdev *subdev;

	subdev = sun6i_csi_capture_remote_subdev(capture, NULL);
	if (subdev)
		v4l2_subdev_call(subdev, video, s_stream, 0);

	sun6i_csi_set_stream(csi_dev, false);

	video_device_pipeline_stop(&capture->video_dev);

	sun6i_csi_capture_state_cleanup(csi_dev, true);
}

static const struct vb2_ops sun6i_csi_capture_queue_ops = {
	.queue_setup		= sun6i_csi_capture_queue_setup,
	.buf_prepare		= sun6i_csi_capture_buffer_prepare,
	.buf_queue		= sun6i_csi_capture_buffer_queue,
	.start_streaming	= sun6i_csi_capture_start_streaming,
	.stop_streaming		= sun6i_csi_capture_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

/* V4L2 Device */

static int sun6i_csi_capture_querycap(struct file *file, void *private,
				      struct v4l2_capability *capability)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);
	struct video_device *video_dev = &csi_dev->capture.video_dev;

	strscpy(capability->driver, SUN6I_CSI_NAME, sizeof(capability->driver));
	strscpy(capability->card, video_dev->name, sizeof(capability->card));
	snprintf(capability->bus_info, sizeof(capability->bus_info),
		 "platform:%s", dev_name(csi_dev->dev));

	return 0;
}

static int sun6i_csi_capture_enum_fmt(struct file *file, void *private,
				      struct v4l2_fmtdesc *fmtdesc)
{
	u32 index = fmtdesc->index;

	if (index >= ARRAY_SIZE(sun6i_csi_capture_formats))
		return -EINVAL;

	fmtdesc->pixelformat = sun6i_csi_capture_formats[index];

	return 0;
}

static int sun6i_csi_capture_g_fmt(struct file *file, void *private,
				   struct v4l2_format *format)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);
	struct sun6i_csi_capture *capture = &csi_dev->capture;

	*format = capture->format;

	return 0;
}

static int sun6i_csi_capture_format_try(struct sun6i_csi_capture *capture,
					struct v4l2_format *format)
{
	struct v4l2_pix_format *pix_format = &format->fmt.pix;
	int bpp;

	if (!sun6i_csi_capture_format_check(pix_format->pixelformat))
		pix_format->pixelformat = sun6i_csi_capture_formats[0];

	v4l_bound_align_image(&pix_format->width, MIN_WIDTH, MAX_WIDTH, 1,
			      &pix_format->height, MIN_HEIGHT, MAX_WIDTH, 1, 1);

	bpp = sun6i_csi_get_bpp(pix_format->pixelformat);
	pix_format->bytesperline = (pix_format->width * bpp) >> 3;
	pix_format->sizeimage = pix_format->bytesperline * pix_format->height;

	if (pix_format->field == V4L2_FIELD_ANY)
		pix_format->field = V4L2_FIELD_NONE;

	if (pix_format->pixelformat == V4L2_PIX_FMT_JPEG)
		pix_format->colorspace = V4L2_COLORSPACE_JPEG;
	else
		pix_format->colorspace = V4L2_COLORSPACE_SRGB;

	pix_format->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	pix_format->quantization = V4L2_QUANTIZATION_DEFAULT;
	pix_format->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	return 0;
}

static int sun6i_csi_capture_format_set(struct sun6i_csi_capture *capture,
					struct v4l2_format *format)
{
	int ret;

	ret = sun6i_csi_capture_format_try(capture, format);
	if (ret)
		return ret;

	capture->format = *format;

	return 0;
}

static int sun6i_csi_capture_s_fmt(struct file *file, void *private,
				   struct v4l2_format *format)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);
	struct sun6i_csi_capture *capture = &csi_dev->capture;

	if (vb2_is_busy(&capture->queue))
		return -EBUSY;

	return sun6i_csi_capture_format_set(capture, format);
}

static int sun6i_csi_capture_try_fmt(struct file *file, void *private,
				     struct v4l2_format *format)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);
	struct sun6i_csi_capture *capture = &csi_dev->capture;

	return sun6i_csi_capture_format_try(capture, format);
}

static int sun6i_csi_capture_enum_input(struct file *file, void *private,
					struct v4l2_input *input)
{
	if (input->index != 0)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(input->name, "Camera", sizeof(input->name));

	return 0;
}

static int sun6i_csi_capture_g_input(struct file *file, void *private,
				     unsigned int *index)
{
	*index = 0;

	return 0;
}

static int sun6i_csi_capture_s_input(struct file *file, void *private,
				     unsigned int index)
{
	if (index != 0)
		return -EINVAL;

	return 0;
}

static const struct v4l2_ioctl_ops sun6i_csi_capture_ioctl_ops = {
	.vidioc_querycap		= sun6i_csi_capture_querycap,

	.vidioc_enum_fmt_vid_cap	= sun6i_csi_capture_enum_fmt,
	.vidioc_g_fmt_vid_cap		= sun6i_csi_capture_g_fmt,
	.vidioc_s_fmt_vid_cap		= sun6i_csi_capture_s_fmt,
	.vidioc_try_fmt_vid_cap		= sun6i_csi_capture_try_fmt,

	.vidioc_enum_input		= sun6i_csi_capture_enum_input,
	.vidioc_g_input			= sun6i_csi_capture_g_input,
	.vidioc_s_input			= sun6i_csi_capture_s_input,

	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
};

/* V4L2 File */

static int sun6i_csi_capture_open(struct file *file)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);
	struct sun6i_csi_capture *capture = &csi_dev->capture;
	int ret = 0;

	if (mutex_lock_interruptible(&capture->lock))
		return -ERESTARTSYS;

	ret = v4l2_fh_open(file);
	if (ret < 0)
		goto error_lock;

	ret = v4l2_pipeline_pm_get(&capture->video_dev.entity);
	if (ret < 0)
		goto error_v4l2_fh;

	/* Power on at first open. */
	if (v4l2_fh_is_singular_file(file)) {
		ret = sun6i_csi_set_power(csi_dev, true);
		if (ret < 0)
			goto error_v4l2_fh;
	}

	mutex_unlock(&capture->lock);

	return 0;

error_v4l2_fh:
	v4l2_fh_release(file);

error_lock:
	mutex_unlock(&capture->lock);

	return ret;
}

static int sun6i_csi_capture_close(struct file *file)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);
	struct sun6i_csi_capture *capture = &csi_dev->capture;
	bool last_close;

	mutex_lock(&capture->lock);

	last_close = v4l2_fh_is_singular_file(file);

	_vb2_fop_release(file, NULL);
	v4l2_pipeline_pm_put(&capture->video_dev.entity);

	/* Power off at last close. */
	if (last_close)
		sun6i_csi_set_power(csi_dev, false);

	mutex_unlock(&capture->lock);

	return 0;
}

static const struct v4l2_file_operations sun6i_csi_capture_fops = {
	.owner		= THIS_MODULE,
	.open		= sun6i_csi_capture_open,
	.release	= sun6i_csi_capture_close,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= vb2_fop_mmap,
	.poll		= vb2_fop_poll
};

/* Media Entity */

static int
sun6i_csi_capture_link_validate_get_format(struct media_pad *pad,
					   struct v4l2_subdev_format *fmt)
{
	if (is_media_entity_v4l2_subdev(pad->entity)) {
		struct v4l2_subdev *sd =
				media_entity_to_v4l2_subdev(pad->entity);

		fmt->which = V4L2_SUBDEV_FORMAT_ACTIVE;
		fmt->pad = pad->index;
		return v4l2_subdev_call(sd, pad, get_fmt, NULL, fmt);
	}

	return -EINVAL;
}

static int sun6i_csi_capture_link_validate(struct media_link *link)
{
	struct video_device *vdev = container_of(link->sink->entity,
						 struct video_device, entity);
	struct sun6i_csi_device *csi_dev = video_get_drvdata(vdev);
	struct sun6i_csi_capture *capture = &csi_dev->capture;
	struct v4l2_subdev_format source_fmt;
	int ret;

	capture->mbus_code = 0;

	if (!media_pad_remote_pad_first(link->sink->entity->pads)) {
		dev_info(csi_dev->dev, "capture node %s pad not connected\n",
			 vdev->name);
		return -ENOLINK;
	}

	ret = sun6i_csi_capture_link_validate_get_format(link->source,
							 &source_fmt);
	if (ret < 0)
		return ret;

	if (!sun6i_csi_is_format_supported(csi_dev,
					   capture->format.fmt.pix.pixelformat,
					   source_fmt.format.code)) {
		dev_err(csi_dev->dev,
			"Unsupported pixformat: 0x%x with mbus code: 0x%x!\n",
			capture->format.fmt.pix.pixelformat,
			source_fmt.format.code);
		return -EPIPE;
	}

	if (source_fmt.format.width != capture->format.fmt.pix.width ||
	    source_fmt.format.height != capture->format.fmt.pix.height) {
		dev_err(csi_dev->dev,
			"Wrong width or height %ux%u (%ux%u expected)\n",
			capture->format.fmt.pix.width,
			capture->format.fmt.pix.height,
			source_fmt.format.width, source_fmt.format.height);
		return -EPIPE;
	}

	capture->mbus_code = source_fmt.format.code;

	return 0;
}

static const struct media_entity_operations sun6i_csi_capture_media_ops = {
	.link_validate = sun6i_csi_capture_link_validate
};

/* Capture */

int sun6i_csi_capture_setup(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_capture *capture = &csi_dev->capture;
	struct sun6i_csi_capture_state *state = &capture->state;
	struct v4l2_device *v4l2_dev = &csi_dev->v4l2.v4l2_dev;
	struct v4l2_subdev *bridge_subdev = &csi_dev->bridge.subdev;
	struct video_device *video_dev = &capture->video_dev;
	struct vb2_queue *queue = &capture->queue;
	struct media_pad *pad = &capture->pad;
	struct v4l2_format format = { 0 };
	struct v4l2_pix_format *pix_format = &format.fmt.pix;
	int ret;

	/* State */

	INIT_LIST_HEAD(&state->queue);
	spin_lock_init(&state->lock);

	/* Media Entity */

	video_dev->entity.ops = &sun6i_csi_capture_media_ops;

	/* Media Pad */

	pad->flags = MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;

	ret = media_entity_pads_init(&video_dev->entity, 1, pad);
	if (ret < 0)
		return ret;

	/* Queue */

	mutex_init(&capture->lock);

	queue->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	queue->io_modes = VB2_MMAP | VB2_DMABUF;
	queue->buf_struct_size = sizeof(struct sun6i_csi_buffer);
	queue->ops = &sun6i_csi_capture_queue_ops;
	queue->mem_ops = &vb2_dma_contig_memops;
	queue->min_buffers_needed = 2;
	queue->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	queue->lock = &capture->lock;
	queue->dev = csi_dev->dev;
	queue->drv_priv = csi_dev;

	ret = vb2_queue_init(queue);
	if (ret) {
		v4l2_err(v4l2_dev, "failed to initialize vb2 queue: %d\n", ret);
		goto error_media_entity;
	}

	/* V4L2 Format */

	format.type = queue->type;
	pix_format->pixelformat = sun6i_csi_capture_formats[0];
	pix_format->width = 1280;
	pix_format->height = 720;
	pix_format->field = V4L2_FIELD_NONE;

	sun6i_csi_capture_format_set(capture, &format);

	/* Video Device */

	strscpy(video_dev->name, SUN6I_CSI_NAME, sizeof(video_dev->name));
	video_dev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	video_dev->vfl_dir = VFL_DIR_RX;
	video_dev->release = video_device_release_empty;
	video_dev->fops = &sun6i_csi_capture_fops;
	video_dev->ioctl_ops = &sun6i_csi_capture_ioctl_ops;
	video_dev->v4l2_dev = v4l2_dev;
	video_dev->queue = queue;
	video_dev->lock = &capture->lock;

	video_set_drvdata(video_dev, csi_dev);

	ret = video_register_device(video_dev, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "failed to register video device: %d\n",
			 ret);
		goto error_media_entity;
	}

	/* Media Pad Link */

	ret = media_create_pad_link(&bridge_subdev->entity,
				    SUN6I_CSI_BRIDGE_PAD_SOURCE,
				    &video_dev->entity, 0,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "failed to create %s:%u -> %s:%u link\n",
			 bridge_subdev->entity.name,
			 SUN6I_CSI_BRIDGE_PAD_SOURCE,
			 video_dev->entity.name, 0);
		goto error_video_device;
	}

	return 0;

error_video_device:
	vb2_video_unregister_device(video_dev);

error_media_entity:
	media_entity_cleanup(&video_dev->entity);

	mutex_destroy(&capture->lock);

	return ret;
}

void sun6i_csi_capture_cleanup(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_capture *capture = &csi_dev->capture;
	struct video_device *video_dev = &capture->video_dev;

	vb2_video_unregister_device(video_dev);
	media_entity_cleanup(&video_dev->entity);
	mutex_destroy(&capture->lock);
}
