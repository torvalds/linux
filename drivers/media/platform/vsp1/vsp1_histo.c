// SPDX-License-Identifier: GPL-2.0+
/*
 * vsp1_histo.c  --  R-Car VSP1 Histogram API
 *
 * Copyright (C) 2016 Renesas Electronics Corporation
 * Copyright (C) 2016 Laurent Pinchart
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/device.h>
#include <linux/gfp.h>

#include <media/v4l2-ioctl.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-vmalloc.h>

#include "vsp1.h"
#include "vsp1_histo.h"
#include "vsp1_pipe.h"

#define HISTO_MIN_SIZE				4U
#define HISTO_MAX_SIZE				8192U

/* -----------------------------------------------------------------------------
 * Buffer Operations
 */

static inline struct vsp1_histogram_buffer *
to_vsp1_histogram_buffer(struct vb2_v4l2_buffer *vbuf)
{
	return container_of(vbuf, struct vsp1_histogram_buffer, buf);
}

struct vsp1_histogram_buffer *
vsp1_histogram_buffer_get(struct vsp1_histogram *histo)
{
	struct vsp1_histogram_buffer *buf = NULL;
	unsigned long flags;

	spin_lock_irqsave(&histo->irqlock, flags);

	if (list_empty(&histo->irqqueue))
		goto done;

	buf = list_first_entry(&histo->irqqueue, struct vsp1_histogram_buffer,
			       queue);
	list_del(&buf->queue);
	histo->readout = true;

done:
	spin_unlock_irqrestore(&histo->irqlock, flags);
	return buf;
}

void vsp1_histogram_buffer_complete(struct vsp1_histogram *histo,
				    struct vsp1_histogram_buffer *buf,
				    size_t size)
{
	struct vsp1_pipeline *pipe = histo->entity.pipe;
	unsigned long flags;

	/*
	 * The pipeline pointer is guaranteed to be valid as this function is
	 * called from the frame completion interrupt handler, which can only
	 * occur when video streaming is active.
	 */
	buf->buf.sequence = pipe->sequence;
	buf->buf.vb2_buf.timestamp = ktime_get_ns();
	vb2_set_plane_payload(&buf->buf.vb2_buf, 0, size);
	vb2_buffer_done(&buf->buf.vb2_buf, VB2_BUF_STATE_DONE);

	spin_lock_irqsave(&histo->irqlock, flags);
	histo->readout = false;
	wake_up(&histo->wait_queue);
	spin_unlock_irqrestore(&histo->irqlock, flags);
}

/* -----------------------------------------------------------------------------
 * videobuf2 Queue Operations
 */

static int histo_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
			     unsigned int *nplanes, unsigned int sizes[],
			     struct device *alloc_devs[])
{
	struct vsp1_histogram *histo = vb2_get_drv_priv(vq);

	if (*nplanes) {
		if (*nplanes != 1)
			return -EINVAL;

		if (sizes[0] < histo->data_size)
			return -EINVAL;

		return 0;
	}

	*nplanes = 1;
	sizes[0] = histo->data_size;

	return 0;
}

static int histo_buffer_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vsp1_histogram *histo = vb2_get_drv_priv(vb->vb2_queue);
	struct vsp1_histogram_buffer *buf = to_vsp1_histogram_buffer(vbuf);

	if (vb->num_planes != 1)
		return -EINVAL;

	if (vb2_plane_size(vb, 0) < histo->data_size)
		return -EINVAL;

	buf->addr = vb2_plane_vaddr(vb, 0);

	return 0;
}

static void histo_buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vsp1_histogram *histo = vb2_get_drv_priv(vb->vb2_queue);
	struct vsp1_histogram_buffer *buf = to_vsp1_histogram_buffer(vbuf);
	unsigned long flags;

	spin_lock_irqsave(&histo->irqlock, flags);
	list_add_tail(&buf->queue, &histo->irqqueue);
	spin_unlock_irqrestore(&histo->irqlock, flags);
}

static int histo_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	return 0;
}

static void histo_stop_streaming(struct vb2_queue *vq)
{
	struct vsp1_histogram *histo = vb2_get_drv_priv(vq);
	struct vsp1_histogram_buffer *buffer;
	unsigned long flags;

	spin_lock_irqsave(&histo->irqlock, flags);

	/* Remove all buffers from the IRQ queue. */
	list_for_each_entry(buffer, &histo->irqqueue, queue)
		vb2_buffer_done(&buffer->buf.vb2_buf, VB2_BUF_STATE_ERROR);
	INIT_LIST_HEAD(&histo->irqqueue);

	/* Wait for the buffer being read out (if any) to complete. */
	wait_event_lock_irq(histo->wait_queue, !histo->readout, histo->irqlock);

	spin_unlock_irqrestore(&histo->irqlock, flags);
}

static const struct vb2_ops histo_video_queue_qops = {
	.queue_setup = histo_queue_setup,
	.buf_prepare = histo_buffer_prepare,
	.buf_queue = histo_buffer_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.start_streaming = histo_start_streaming,
	.stop_streaming = histo_stop_streaming,
};

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static int histo_enum_mbus_code(struct v4l2_subdev *subdev,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_mbus_code_enum *code)
{
	struct vsp1_histogram *histo = subdev_to_histo(subdev);

	if (code->pad == HISTO_PAD_SOURCE) {
		code->code = MEDIA_BUS_FMT_FIXED;
		return 0;
	}

	return vsp1_subdev_enum_mbus_code(subdev, cfg, code, histo->formats,
					  histo->num_formats);
}

static int histo_enum_frame_size(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->pad != HISTO_PAD_SINK)
		return -EINVAL;

	return vsp1_subdev_enum_frame_size(subdev, cfg, fse, HISTO_MIN_SIZE,
					   HISTO_MIN_SIZE, HISTO_MAX_SIZE,
					   HISTO_MAX_SIZE);
}

static int histo_get_selection(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_selection *sel)
{
	struct vsp1_histogram *histo = subdev_to_histo(subdev);
	struct v4l2_subdev_pad_config *config;
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;
	int ret = 0;

	if (sel->pad != HISTO_PAD_SINK)
		return -EINVAL;

	mutex_lock(&histo->entity.lock);

	config = vsp1_entity_get_pad_config(&histo->entity, cfg, sel->which);
	if (!config) {
		ret = -EINVAL;
		goto done;
	}

	switch (sel->target) {
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		crop = vsp1_entity_get_pad_selection(&histo->entity, config,
						     HISTO_PAD_SINK,
						     V4L2_SEL_TGT_CROP);
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = crop->width;
		sel->r.height = crop->height;
		break;

	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		format = vsp1_entity_get_pad_format(&histo->entity, config,
						    HISTO_PAD_SINK);
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = format->width;
		sel->r.height = format->height;
		break;

	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_CROP:
		sel->r = *vsp1_entity_get_pad_selection(&histo->entity, config,
							sel->pad, sel->target);
		break;

	default:
		ret = -EINVAL;
		break;
	}

done:
	mutex_unlock(&histo->entity.lock);
	return ret;
}

static int histo_set_crop(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_pad_config *config,
			 struct v4l2_subdev_selection *sel)
{
	struct vsp1_histogram *histo = subdev_to_histo(subdev);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *selection;

	/* The crop rectangle must be inside the input frame. */
	format = vsp1_entity_get_pad_format(&histo->entity, config,
					    HISTO_PAD_SINK);
	sel->r.left = clamp_t(unsigned int, sel->r.left, 0, format->width - 1);
	sel->r.top = clamp_t(unsigned int, sel->r.top, 0, format->height - 1);
	sel->r.width = clamp_t(unsigned int, sel->r.width, HISTO_MIN_SIZE,
			       format->width - sel->r.left);
	sel->r.height = clamp_t(unsigned int, sel->r.height, HISTO_MIN_SIZE,
				format->height - sel->r.top);

	/* Set the crop rectangle and reset the compose rectangle. */
	selection = vsp1_entity_get_pad_selection(&histo->entity, config,
						  sel->pad, V4L2_SEL_TGT_CROP);
	*selection = sel->r;

	selection = vsp1_entity_get_pad_selection(&histo->entity, config,
						  sel->pad,
						  V4L2_SEL_TGT_COMPOSE);
	*selection = sel->r;

	return 0;
}

static int histo_set_compose(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_pad_config *config,
			     struct v4l2_subdev_selection *sel)
{
	struct vsp1_histogram *histo = subdev_to_histo(subdev);
	struct v4l2_rect *compose;
	struct v4l2_rect *crop;
	unsigned int ratio;

	/*
	 * The compose rectangle is used to configure downscaling, the top left
	 * corner is fixed to (0,0) and the size to 1/2 or 1/4 of the crop
	 * rectangle.
	 */
	sel->r.left = 0;
	sel->r.top = 0;

	crop = vsp1_entity_get_pad_selection(&histo->entity, config, sel->pad,
					     V4L2_SEL_TGT_CROP);

	/*
	 * Clamp the width and height to acceptable values first and then
	 * compute the closest rounded dividing ratio.
	 *
	 * Ratio	Rounded ratio
	 * --------------------------
	 * [1.0 1.5[	1
	 * [1.5 3.0[	2
	 * [3.0 4.0]	4
	 *
	 * The rounded ratio can be computed using
	 *
	 * 1 << (ceil(ratio * 2) / 3)
	 */
	sel->r.width = clamp(sel->r.width, crop->width / 4, crop->width);
	ratio = 1 << (crop->width * 2 / sel->r.width / 3);
	sel->r.width = crop->width / ratio;


	sel->r.height = clamp(sel->r.height, crop->height / 4, crop->height);
	ratio = 1 << (crop->height * 2 / sel->r.height / 3);
	sel->r.height = crop->height / ratio;

	compose = vsp1_entity_get_pad_selection(&histo->entity, config,
						sel->pad,
						V4L2_SEL_TGT_COMPOSE);
	*compose = sel->r;

	return 0;
}

static int histo_set_selection(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_selection *sel)
{
	struct vsp1_histogram *histo = subdev_to_histo(subdev);
	struct v4l2_subdev_pad_config *config;
	int ret;

	if (sel->pad != HISTO_PAD_SINK)
		return -EINVAL;

	mutex_lock(&histo->entity.lock);

	config = vsp1_entity_get_pad_config(&histo->entity, cfg, sel->which);
	if (!config) {
		ret = -EINVAL;
		goto done;
	}

	if (sel->target == V4L2_SEL_TGT_CROP)
		ret = histo_set_crop(subdev, config, sel);
	else if (sel->target == V4L2_SEL_TGT_COMPOSE)
		ret = histo_set_compose(subdev, config, sel);
	else
		ret = -EINVAL;

done:
	mutex_unlock(&histo->entity.lock);
	return ret;
}

static int histo_get_format(struct v4l2_subdev *subdev,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_format *fmt)
{
	if (fmt->pad == HISTO_PAD_SOURCE) {
		fmt->format.code = MEDIA_BUS_FMT_FIXED;
		fmt->format.width = 0;
		fmt->format.height = 0;
		fmt->format.field = V4L2_FIELD_NONE;
		fmt->format.colorspace = V4L2_COLORSPACE_RAW;
		return 0;
	}

	return vsp1_subdev_get_pad_format(subdev, cfg, fmt);
}

static int histo_set_format(struct v4l2_subdev *subdev,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_format *fmt)
{
	struct vsp1_histogram *histo = subdev_to_histo(subdev);

	if (fmt->pad != HISTO_PAD_SINK)
		return histo_get_format(subdev, cfg, fmt);

	return vsp1_subdev_set_pad_format(subdev, cfg, fmt,
					  histo->formats, histo->num_formats,
					  HISTO_MIN_SIZE, HISTO_MIN_SIZE,
					  HISTO_MAX_SIZE, HISTO_MAX_SIZE);
}

static const struct v4l2_subdev_pad_ops histo_pad_ops = {
	.enum_mbus_code = histo_enum_mbus_code,
	.enum_frame_size = histo_enum_frame_size,
	.get_fmt = histo_get_format,
	.set_fmt = histo_set_format,
	.get_selection = histo_get_selection,
	.set_selection = histo_set_selection,
};

static const struct v4l2_subdev_ops histo_ops = {
	.pad    = &histo_pad_ops,
};

/* -----------------------------------------------------------------------------
 * V4L2 ioctls
 */

static int histo_v4l2_querycap(struct file *file, void *fh,
			       struct v4l2_capability *cap)
{
	struct v4l2_fh *vfh = file->private_data;
	struct vsp1_histogram *histo = vdev_to_histo(vfh->vdev);

	cap->capabilities = V4L2_CAP_DEVICE_CAPS | V4L2_CAP_STREAMING
			  | V4L2_CAP_VIDEO_CAPTURE_MPLANE
			  | V4L2_CAP_VIDEO_OUTPUT_MPLANE
			  | V4L2_CAP_META_CAPTURE;

	strscpy(cap->driver, "vsp1", sizeof(cap->driver));
	strscpy(cap->card, histo->video.name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 dev_name(histo->entity.vsp1->dev));

	return 0;
}

static int histo_v4l2_enum_format(struct file *file, void *fh,
				  struct v4l2_fmtdesc *f)
{
	struct v4l2_fh *vfh = file->private_data;
	struct vsp1_histogram *histo = vdev_to_histo(vfh->vdev);

	if (f->index > 0 || f->type != histo->queue.type)
		return -EINVAL;

	f->pixelformat = histo->meta_format;

	return 0;
}

static int histo_v4l2_get_format(struct file *file, void *fh,
				 struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct vsp1_histogram *histo = vdev_to_histo(vfh->vdev);
	struct v4l2_meta_format *meta = &format->fmt.meta;

	if (format->type != histo->queue.type)
		return -EINVAL;

	memset(meta, 0, sizeof(*meta));

	meta->dataformat = histo->meta_format;
	meta->buffersize = histo->data_size;

	return 0;
}

static const struct v4l2_ioctl_ops histo_v4l2_ioctl_ops = {
	.vidioc_querycap		= histo_v4l2_querycap,
	.vidioc_enum_fmt_meta_cap	= histo_v4l2_enum_format,
	.vidioc_g_fmt_meta_cap		= histo_v4l2_get_format,
	.vidioc_s_fmt_meta_cap		= histo_v4l2_get_format,
	.vidioc_try_fmt_meta_cap	= histo_v4l2_get_format,
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
};

/* -----------------------------------------------------------------------------
 * V4L2 File Operations
 */

static const struct v4l2_file_operations histo_v4l2_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
};

static void vsp1_histogram_cleanup(struct vsp1_histogram *histo)
{
	if (video_is_registered(&histo->video))
		video_unregister_device(&histo->video);

	media_entity_cleanup(&histo->video.entity);
}

void vsp1_histogram_destroy(struct vsp1_entity *entity)
{
	struct vsp1_histogram *histo = subdev_to_histo(&entity->subdev);

	vsp1_histogram_cleanup(histo);
}

int vsp1_histogram_init(struct vsp1_device *vsp1, struct vsp1_histogram *histo,
			enum vsp1_entity_type type, const char *name,
			const struct vsp1_entity_operations *ops,
			const unsigned int *formats, unsigned int num_formats,
			size_t data_size, u32 meta_format)
{
	int ret;

	histo->formats = formats;
	histo->num_formats = num_formats;
	histo->data_size = data_size;
	histo->meta_format = meta_format;

	histo->pad.flags = MEDIA_PAD_FL_SINK;
	histo->video.vfl_dir = VFL_DIR_RX;

	mutex_init(&histo->lock);
	spin_lock_init(&histo->irqlock);
	INIT_LIST_HEAD(&histo->irqqueue);
	init_waitqueue_head(&histo->wait_queue);

	/* Initialize the VSP entity... */
	histo->entity.ops = ops;
	histo->entity.type = type;

	ret = vsp1_entity_init(vsp1, &histo->entity, name, 2, &histo_ops,
			       MEDIA_ENT_F_PROC_VIDEO_STATISTICS);
	if (ret < 0)
		return ret;

	/* ... and the media entity... */
	ret = media_entity_pads_init(&histo->video.entity, 1, &histo->pad);
	if (ret < 0)
		return ret;

	/* ... and the video node... */
	histo->video.v4l2_dev = &vsp1->v4l2_dev;
	histo->video.fops = &histo_v4l2_fops;
	snprintf(histo->video.name, sizeof(histo->video.name),
		 "%s histo", histo->entity.subdev.name);
	histo->video.vfl_type = VFL_TYPE_GRABBER;
	histo->video.release = video_device_release_empty;
	histo->video.ioctl_ops = &histo_v4l2_ioctl_ops;
	histo->video.device_caps = V4L2_CAP_META_CAPTURE | V4L2_CAP_STREAMING;

	video_set_drvdata(&histo->video, histo);

	/* ... and the buffers queue... */
	histo->queue.type = V4L2_BUF_TYPE_META_CAPTURE;
	histo->queue.io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	histo->queue.lock = &histo->lock;
	histo->queue.drv_priv = histo;
	histo->queue.buf_struct_size = sizeof(struct vsp1_histogram_buffer);
	histo->queue.ops = &histo_video_queue_qops;
	histo->queue.mem_ops = &vb2_vmalloc_memops;
	histo->queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	histo->queue.dev = vsp1->dev;
	ret = vb2_queue_init(&histo->queue);
	if (ret < 0) {
		dev_err(vsp1->dev, "failed to initialize vb2 queue\n");
		goto error;
	}

	/* ... and register the video device. */
	histo->video.queue = &histo->queue;
	ret = video_register_device(&histo->video, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		dev_err(vsp1->dev, "failed to register video device\n");
		goto error;
	}

	return 0;

error:
	vsp1_histogram_cleanup(histo);
	return ret;
}
