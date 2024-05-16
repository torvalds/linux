// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2021-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-v4l2.h>

#include "sun6i_isp.h"
#include "sun6i_isp_capture.h"
#include "sun6i_isp_proc.h"
#include "sun6i_isp_reg.h"

/* Helpers */

void sun6i_isp_capture_dimensions(struct sun6i_isp_device *isp_dev,
				  unsigned int *width, unsigned int *height)
{
	if (width)
		*width = isp_dev->capture.format.fmt.pix.width;
	if (height)
		*height = isp_dev->capture.format.fmt.pix.height;
}

void sun6i_isp_capture_format(struct sun6i_isp_device *isp_dev,
			      u32 *pixelformat)
{
	if (pixelformat)
		*pixelformat = isp_dev->capture.format.fmt.pix.pixelformat;
}

/* Format */

static const struct sun6i_isp_capture_format sun6i_isp_capture_formats[] = {
	{
		.pixelformat		= V4L2_PIX_FMT_NV12,
		.output_format		= SUN6I_ISP_OUTPUT_FMT_YUV420SP,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_NV21,
		.output_format		= SUN6I_ISP_OUTPUT_FMT_YVU420SP,
	},
};

const struct sun6i_isp_capture_format *
sun6i_isp_capture_format_find(u32 pixelformat)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sun6i_isp_capture_formats); i++)
		if (sun6i_isp_capture_formats[i].pixelformat == pixelformat)
			return &sun6i_isp_capture_formats[i];

	return NULL;
}

/* Capture */

static void
sun6i_isp_capture_buffer_configure(struct sun6i_isp_device *isp_dev,
				   struct sun6i_isp_buffer *isp_buffer)
{
	const struct v4l2_format_info *info;
	struct vb2_buffer *vb2_buffer;
	unsigned int width, height;
	unsigned int width_aligned;
	dma_addr_t address;
	u32 pixelformat;

	vb2_buffer = &isp_buffer->v4l2_buffer.vb2_buf;
	address = vb2_dma_contig_plane_dma_addr(vb2_buffer, 0);

	sun6i_isp_load_write(isp_dev, SUN6I_ISP_MCH_Y_ADDR0_REG,
			     SUN6I_ISP_ADDR_VALUE(address));

	sun6i_isp_capture_dimensions(isp_dev, &width, &height);
	sun6i_isp_capture_format(isp_dev, &pixelformat);

	info = v4l2_format_info(pixelformat);
	if (WARN_ON(!info))
		return;

	/* Stride needs to be aligned to 4. */
	width_aligned = ALIGN(width, 2);

	if (info->comp_planes > 1) {
		address += info->bpp[0] * width_aligned * height;

		sun6i_isp_load_write(isp_dev, SUN6I_ISP_MCH_U_ADDR0_REG,
				     SUN6I_ISP_ADDR_VALUE(address));
	}

	if (info->comp_planes > 2) {
		address += info->bpp[1] *
			   DIV_ROUND_UP(width_aligned, info->hdiv) *
			   DIV_ROUND_UP(height, info->vdiv);

		sun6i_isp_load_write(isp_dev, SUN6I_ISP_MCH_V_ADDR0_REG,
				     SUN6I_ISP_ADDR_VALUE(address));
	}
}

void sun6i_isp_capture_configure(struct sun6i_isp_device *isp_dev)
{
	unsigned int width, height;
	unsigned int stride_luma, stride_chroma;
	unsigned int stride_luma_div4, stride_chroma_div4 = 0;
	const struct sun6i_isp_capture_format *format;
	const struct v4l2_format_info *info;
	u32 pixelformat;

	sun6i_isp_capture_dimensions(isp_dev, &width, &height);
	sun6i_isp_capture_format(isp_dev, &pixelformat);

	format = sun6i_isp_capture_format_find(pixelformat);
	if (WARN_ON(!format))
		return;

	sun6i_isp_load_write(isp_dev, SUN6I_ISP_MCH_SIZE_CFG_REG,
			     SUN6I_ISP_MCH_SIZE_CFG_WIDTH(width) |
			     SUN6I_ISP_MCH_SIZE_CFG_HEIGHT(height));

	info = v4l2_format_info(pixelformat);
	if (WARN_ON(!info))
		return;

	stride_luma = width * info->bpp[0];
	stride_luma_div4 = DIV_ROUND_UP(stride_luma, 4);

	if (info->comp_planes > 1) {
		stride_chroma = width * info->bpp[1] / info->hdiv;
		stride_chroma_div4 = DIV_ROUND_UP(stride_chroma, 4);
	}

	sun6i_isp_load_write(isp_dev, SUN6I_ISP_MCH_CFG_REG,
			     SUN6I_ISP_MCH_CFG_EN |
			     SUN6I_ISP_MCH_CFG_OUTPUT_FMT(format->output_format) |
			     SUN6I_ISP_MCH_CFG_STRIDE_Y_DIV4(stride_luma_div4) |
			     SUN6I_ISP_MCH_CFG_STRIDE_UV_DIV4(stride_chroma_div4));
}

/* State */

static void sun6i_isp_capture_state_cleanup(struct sun6i_isp_device *isp_dev,
					    bool error)
{
	struct sun6i_isp_capture_state *state = &isp_dev->capture.state;
	struct sun6i_isp_buffer **isp_buffer_states[] = {
		&state->pending, &state->current, &state->complete,
	};
	struct sun6i_isp_buffer *isp_buffer;
	struct vb2_buffer *vb2_buffer;
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&state->lock, flags);

	for (i = 0; i < ARRAY_SIZE(isp_buffer_states); i++) {
		isp_buffer = *isp_buffer_states[i];
		if (!isp_buffer)
			continue;

		vb2_buffer = &isp_buffer->v4l2_buffer.vb2_buf;
		vb2_buffer_done(vb2_buffer, error ? VB2_BUF_STATE_ERROR :
				VB2_BUF_STATE_QUEUED);

		*isp_buffer_states[i] = NULL;
	}

	list_for_each_entry(isp_buffer, &state->queue, list) {
		vb2_buffer = &isp_buffer->v4l2_buffer.vb2_buf;
		vb2_buffer_done(vb2_buffer, error ? VB2_BUF_STATE_ERROR :
				VB2_BUF_STATE_QUEUED);
	}

	INIT_LIST_HEAD(&state->queue);

	spin_unlock_irqrestore(&state->lock, flags);
}

void sun6i_isp_capture_state_update(struct sun6i_isp_device *isp_dev,
				    bool *update)
{
	struct sun6i_isp_capture_state *state = &isp_dev->capture.state;
	struct sun6i_isp_buffer *isp_buffer;
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);

	if (list_empty(&state->queue))
		goto complete;

	if (state->pending)
		goto complete;

	isp_buffer = list_first_entry(&state->queue, struct sun6i_isp_buffer,
				      list);

	sun6i_isp_capture_buffer_configure(isp_dev, isp_buffer);

	list_del(&isp_buffer->list);

	state->pending = isp_buffer;

	if (update)
		*update = true;

complete:
	spin_unlock_irqrestore(&state->lock, flags);
}

void sun6i_isp_capture_state_complete(struct sun6i_isp_device *isp_dev)
{
	struct sun6i_isp_capture_state *state = &isp_dev->capture.state;
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);

	if (!state->pending)
		goto complete;

	state->complete = state->current;
	state->current = state->pending;
	state->pending = NULL;

	if (state->complete) {
		struct sun6i_isp_buffer *isp_buffer = state->complete;
		struct vb2_buffer *vb2_buffer =
			&isp_buffer->v4l2_buffer.vb2_buf;

		vb2_buffer->timestamp = ktime_get_ns();
		isp_buffer->v4l2_buffer.sequence = state->sequence;

		vb2_buffer_done(vb2_buffer, VB2_BUF_STATE_DONE);

		state->complete = NULL;
	}

complete:
	spin_unlock_irqrestore(&state->lock, flags);
}

void sun6i_isp_capture_finish(struct sun6i_isp_device *isp_dev)
{
	struct sun6i_isp_capture_state *state = &isp_dev->capture.state;
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);
	state->sequence++;
	spin_unlock_irqrestore(&state->lock, flags);
}

/* Queue */

static int sun6i_isp_capture_queue_setup(struct vb2_queue *queue,
					 unsigned int *buffers_count,
					 unsigned int *planes_count,
					 unsigned int sizes[],
					 struct device *alloc_devs[])
{
	struct sun6i_isp_device *isp_dev = vb2_get_drv_priv(queue);
	unsigned int size = isp_dev->capture.format.fmt.pix.sizeimage;

	if (*planes_count)
		return sizes[0] < size ? -EINVAL : 0;

	*planes_count = 1;
	sizes[0] = size;

	return 0;
}

static int sun6i_isp_capture_buffer_prepare(struct vb2_buffer *vb2_buffer)
{
	struct sun6i_isp_device *isp_dev =
		vb2_get_drv_priv(vb2_buffer->vb2_queue);
	struct v4l2_device *v4l2_dev = &isp_dev->v4l2.v4l2_dev;
	unsigned int size = isp_dev->capture.format.fmt.pix.sizeimage;

	if (vb2_plane_size(vb2_buffer, 0) < size) {
		v4l2_err(v4l2_dev, "buffer too small (%lu < %u)\n",
			 vb2_plane_size(vb2_buffer, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb2_buffer, 0, size);

	return 0;
}

static void sun6i_isp_capture_buffer_queue(struct vb2_buffer *vb2_buffer)
{
	struct sun6i_isp_device *isp_dev =
		vb2_get_drv_priv(vb2_buffer->vb2_queue);
	struct sun6i_isp_capture_state *state = &isp_dev->capture.state;
	struct vb2_v4l2_buffer *v4l2_buffer = to_vb2_v4l2_buffer(vb2_buffer);
	struct sun6i_isp_buffer *isp_buffer =
		container_of(v4l2_buffer, struct sun6i_isp_buffer, v4l2_buffer);
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);
	list_add_tail(&isp_buffer->list, &state->queue);
	spin_unlock_irqrestore(&state->lock, flags);

	/* Update the state to schedule our buffer as soon as possible. */
	if (state->streaming)
		sun6i_isp_state_update(isp_dev, false);
}

static int sun6i_isp_capture_start_streaming(struct vb2_queue *queue,
					     unsigned int count)
{
	struct sun6i_isp_device *isp_dev = vb2_get_drv_priv(queue);
	struct sun6i_isp_capture_state *state = &isp_dev->capture.state;
	struct video_device *video_dev = &isp_dev->capture.video_dev;
	struct v4l2_subdev *subdev = &isp_dev->proc.subdev;
	int ret;

	state->sequence = 0;

	ret = video_device_pipeline_alloc_start(video_dev);
	if (ret < 0)
		goto error_state;

	state->streaming = true;

	ret = v4l2_subdev_call(subdev, video, s_stream, 1);
	if (ret && ret != -ENOIOCTLCMD)
		goto error_streaming;

	return 0;

error_streaming:
	state->streaming = false;

	video_device_pipeline_stop(video_dev);

error_state:
	sun6i_isp_capture_state_cleanup(isp_dev, false);

	return ret;
}

static void sun6i_isp_capture_stop_streaming(struct vb2_queue *queue)
{
	struct sun6i_isp_device *isp_dev = vb2_get_drv_priv(queue);
	struct sun6i_isp_capture_state *state = &isp_dev->capture.state;
	struct video_device *video_dev = &isp_dev->capture.video_dev;
	struct v4l2_subdev *subdev = &isp_dev->proc.subdev;

	v4l2_subdev_call(subdev, video, s_stream, 0);

	state->streaming = false;

	video_device_pipeline_stop(video_dev);

	sun6i_isp_capture_state_cleanup(isp_dev, true);
}

static const struct vb2_ops sun6i_isp_capture_queue_ops = {
	.queue_setup		= sun6i_isp_capture_queue_setup,
	.buf_prepare		= sun6i_isp_capture_buffer_prepare,
	.buf_queue		= sun6i_isp_capture_buffer_queue,
	.start_streaming	= sun6i_isp_capture_start_streaming,
	.stop_streaming		= sun6i_isp_capture_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

/* Video Device */

static void sun6i_isp_capture_format_prepare(struct v4l2_format *format)
{
	struct v4l2_pix_format *pix_format = &format->fmt.pix;
	const struct v4l2_format_info *info;
	unsigned int width, height;
	unsigned int width_aligned;
	unsigned int i;

	v4l_bound_align_image(&pix_format->width, SUN6I_ISP_CAPTURE_WIDTH_MIN,
			      SUN6I_ISP_CAPTURE_WIDTH_MAX, 1,
			      &pix_format->height, SUN6I_ISP_CAPTURE_HEIGHT_MIN,
			      SUN6I_ISP_CAPTURE_HEIGHT_MAX, 1, 0);

	if (!sun6i_isp_capture_format_find(pix_format->pixelformat))
		pix_format->pixelformat =
			sun6i_isp_capture_formats[0].pixelformat;

	info = v4l2_format_info(pix_format->pixelformat);
	if (WARN_ON(!info))
		return;

	width = pix_format->width;
	height = pix_format->height;

	/* Stride needs to be aligned to 4. */
	width_aligned = ALIGN(width, 2);

	pix_format->bytesperline = width_aligned * info->bpp[0];
	pix_format->sizeimage = 0;

	for (i = 0; i < info->comp_planes; i++) {
		unsigned int hdiv = (i == 0) ? 1 : info->hdiv;
		unsigned int vdiv = (i == 0) ? 1 : info->vdiv;

		pix_format->sizeimage += info->bpp[i] *
					 DIV_ROUND_UP(width_aligned, hdiv) *
					 DIV_ROUND_UP(height, vdiv);
	}

	pix_format->field = V4L2_FIELD_NONE;

	pix_format->colorspace = V4L2_COLORSPACE_RAW;
	pix_format->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	pix_format->quantization = V4L2_QUANTIZATION_DEFAULT;
	pix_format->xfer_func = V4L2_XFER_FUNC_DEFAULT;
}

static int sun6i_isp_capture_querycap(struct file *file, void *private,
				      struct v4l2_capability *capability)
{
	struct sun6i_isp_device *isp_dev = video_drvdata(file);
	struct video_device *video_dev = &isp_dev->capture.video_dev;

	strscpy(capability->driver, SUN6I_ISP_NAME, sizeof(capability->driver));
	strscpy(capability->card, video_dev->name, sizeof(capability->card));
	snprintf(capability->bus_info, sizeof(capability->bus_info),
		 "platform:%s", dev_name(isp_dev->dev));

	return 0;
}

static int sun6i_isp_capture_enum_fmt(struct file *file, void *private,
				      struct v4l2_fmtdesc *fmtdesc)
{
	u32 index = fmtdesc->index;

	if (index >= ARRAY_SIZE(sun6i_isp_capture_formats))
		return -EINVAL;

	fmtdesc->pixelformat = sun6i_isp_capture_formats[index].pixelformat;

	return 0;
}

static int sun6i_isp_capture_g_fmt(struct file *file, void *private,
				   struct v4l2_format *format)
{
	struct sun6i_isp_device *isp_dev = video_drvdata(file);

	*format = isp_dev->capture.format;

	return 0;
}

static int sun6i_isp_capture_s_fmt(struct file *file, void *private,
				   struct v4l2_format *format)
{
	struct sun6i_isp_device *isp_dev = video_drvdata(file);

	if (vb2_is_busy(&isp_dev->capture.queue))
		return -EBUSY;

	sun6i_isp_capture_format_prepare(format);

	isp_dev->capture.format = *format;

	return 0;
}

static int sun6i_isp_capture_try_fmt(struct file *file, void *private,
				     struct v4l2_format *format)
{
	sun6i_isp_capture_format_prepare(format);

	return 0;
}

static int sun6i_isp_capture_enum_input(struct file *file, void *private,
					struct v4l2_input *input)
{
	if (input->index != 0)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(input->name, "Camera", sizeof(input->name));

	return 0;
}

static int sun6i_isp_capture_g_input(struct file *file, void *private,
				     unsigned int *index)
{
	*index = 0;

	return 0;
}

static int sun6i_isp_capture_s_input(struct file *file, void *private,
				     unsigned int index)
{
	if (index != 0)
		return -EINVAL;

	return 0;
}

static const struct v4l2_ioctl_ops sun6i_isp_capture_ioctl_ops = {
	.vidioc_querycap		= sun6i_isp_capture_querycap,

	.vidioc_enum_fmt_vid_cap	= sun6i_isp_capture_enum_fmt,
	.vidioc_g_fmt_vid_cap		= sun6i_isp_capture_g_fmt,
	.vidioc_s_fmt_vid_cap		= sun6i_isp_capture_s_fmt,
	.vidioc_try_fmt_vid_cap		= sun6i_isp_capture_try_fmt,

	.vidioc_enum_input		= sun6i_isp_capture_enum_input,
	.vidioc_g_input			= sun6i_isp_capture_g_input,
	.vidioc_s_input			= sun6i_isp_capture_s_input,

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

static int sun6i_isp_capture_open(struct file *file)
{
	struct sun6i_isp_device *isp_dev = video_drvdata(file);
	struct video_device *video_dev = &isp_dev->capture.video_dev;
	struct mutex *lock = &isp_dev->capture.lock;
	int ret;

	if (mutex_lock_interruptible(lock))
		return -ERESTARTSYS;

	ret = v4l2_pipeline_pm_get(&video_dev->entity);
	if (ret)
		goto error_mutex;

	ret = v4l2_fh_open(file);
	if (ret)
		goto error_pipeline;

	mutex_unlock(lock);

	return 0;

error_pipeline:
	v4l2_pipeline_pm_put(&video_dev->entity);

error_mutex:
	mutex_unlock(lock);

	return ret;
}

static int sun6i_isp_capture_release(struct file *file)
{
	struct sun6i_isp_device *isp_dev = video_drvdata(file);
	struct video_device *video_dev = &isp_dev->capture.video_dev;
	struct mutex *lock = &isp_dev->capture.lock;

	mutex_lock(lock);

	_vb2_fop_release(file, NULL);
	v4l2_pipeline_pm_put(&video_dev->entity);

	mutex_unlock(lock);

	return 0;
}

static const struct v4l2_file_operations sun6i_isp_capture_fops = {
	.owner		= THIS_MODULE,
	.open		= sun6i_isp_capture_open,
	.release	= sun6i_isp_capture_release,
	.unlocked_ioctl	= video_ioctl2,
	.poll		= vb2_fop_poll,
	.mmap		= vb2_fop_mmap,
};

/* Media Entity */

static int sun6i_isp_capture_link_validate(struct media_link *link)
{
	struct video_device *video_dev =
		media_entity_to_video_device(link->sink->entity);
	struct sun6i_isp_device *isp_dev = video_get_drvdata(video_dev);
	struct v4l2_device *v4l2_dev = &isp_dev->v4l2.v4l2_dev;
	unsigned int capture_width, capture_height;
	unsigned int proc_width, proc_height;

	sun6i_isp_capture_dimensions(isp_dev, &capture_width, &capture_height);
	sun6i_isp_proc_dimensions(isp_dev, &proc_width, &proc_height);

	/* No cropping/scaling is supported (yet). */
	if (capture_width != proc_width || capture_height != proc_height) {
		v4l2_err(v4l2_dev,
			 "invalid input/output dimensions: %ux%u/%ux%u\n",
			 proc_width, proc_height, capture_width,
			 capture_height);
		return -EINVAL;
	}

	return 0;
}

static const struct media_entity_operations sun6i_isp_capture_entity_ops = {
	.link_validate	= sun6i_isp_capture_link_validate,
};

/* Capture */

int sun6i_isp_capture_setup(struct sun6i_isp_device *isp_dev)
{
	struct sun6i_isp_capture *capture = &isp_dev->capture;
	struct sun6i_isp_capture_state *state = &capture->state;
	struct v4l2_device *v4l2_dev = &isp_dev->v4l2.v4l2_dev;
	struct v4l2_subdev *proc_subdev = &isp_dev->proc.subdev;
	struct video_device *video_dev = &capture->video_dev;
	struct vb2_queue *queue = &capture->queue;
	struct media_pad *pad = &capture->pad;
	struct v4l2_format *format = &capture->format;
	struct v4l2_pix_format *pix_format = &format->fmt.pix;
	int ret;

	/* State */

	INIT_LIST_HEAD(&state->queue);
	spin_lock_init(&state->lock);

	/* Media Entity */

	video_dev->entity.ops = &sun6i_isp_capture_entity_ops;

	/* Media Pads */

	pad->flags = MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;

	ret = media_entity_pads_init(&video_dev->entity, 1, pad);
	if (ret)
		goto error_mutex;

	/* Queue */

	mutex_init(&capture->lock);

	queue->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	queue->io_modes = VB2_MMAP | VB2_DMABUF;
	queue->buf_struct_size = sizeof(struct sun6i_isp_buffer);
	queue->ops = &sun6i_isp_capture_queue_ops;
	queue->mem_ops = &vb2_dma_contig_memops;
	queue->min_queued_buffers = 2;
	queue->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	queue->lock = &capture->lock;
	queue->dev = isp_dev->dev;
	queue->drv_priv = isp_dev;

	ret = vb2_queue_init(queue);
	if (ret) {
		v4l2_err(v4l2_dev, "failed to initialize vb2 queue: %d\n", ret);
		goto error_media_entity;
	}

	/* V4L2 Format */

	format->type = queue->type;
	pix_format->pixelformat = sun6i_isp_capture_formats[0].pixelformat;
	pix_format->width = 1280;
	pix_format->height = 720;

	sun6i_isp_capture_format_prepare(format);

	/* Video Device */

	strscpy(video_dev->name, SUN6I_ISP_CAPTURE_NAME,
		sizeof(video_dev->name));
	video_dev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	video_dev->vfl_dir = VFL_DIR_RX;
	video_dev->release = video_device_release_empty;
	video_dev->fops = &sun6i_isp_capture_fops;
	video_dev->ioctl_ops = &sun6i_isp_capture_ioctl_ops;
	video_dev->v4l2_dev = v4l2_dev;
	video_dev->queue = queue;
	video_dev->lock = &capture->lock;

	video_set_drvdata(video_dev, isp_dev);

	ret = video_register_device(video_dev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		v4l2_err(v4l2_dev, "failed to register video device: %d\n",
			 ret);
		goto error_media_entity;
	}

	/* Media Pad Link */

	ret = media_create_pad_link(&proc_subdev->entity,
				    SUN6I_ISP_PROC_PAD_SOURCE,
				    &video_dev->entity, 0,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "failed to create %s:%u -> %s:%u link\n",
			 proc_subdev->entity.name, SUN6I_ISP_PROC_PAD_SOURCE,
			 video_dev->entity.name, 0);
		goto error_video_device;
	}

	return 0;

error_video_device:
	vb2_video_unregister_device(video_dev);

error_media_entity:
	media_entity_cleanup(&video_dev->entity);

error_mutex:
	mutex_destroy(&capture->lock);

	return ret;
}

void sun6i_isp_capture_cleanup(struct sun6i_isp_device *isp_dev)
{
	struct sun6i_isp_capture *capture = &isp_dev->capture;
	struct video_device *video_dev = &capture->video_dev;

	vb2_video_unregister_device(video_dev);
	media_entity_cleanup(&video_dev->entity);
	mutex_destroy(&capture->lock);
}
