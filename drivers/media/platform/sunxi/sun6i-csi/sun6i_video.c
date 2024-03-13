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
#include "sun6i_video.h"

/* This is got from BSP sources. */
#define MIN_WIDTH	(32)
#define MIN_HEIGHT	(32)
#define MAX_WIDTH	(4800)
#define MAX_HEIGHT	(4800)

/* Helpers */

static struct v4l2_subdev *
sun6i_video_remote_subdev(struct sun6i_video *video, u32 *pad)
{
	struct media_pad *remote;

	remote = media_pad_remote_pad_first(&video->pad);

	if (!remote || !is_media_entity_v4l2_subdev(remote->entity))
		return NULL;

	if (pad)
		*pad = remote->index;

	return media_entity_to_v4l2_subdev(remote->entity);
}

/* Format */

static const u32 sun6i_video_formats[] = {
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

static bool sun6i_video_format_check(u32 format)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sun6i_video_formats); i++)
		if (sun6i_video_formats[i] == format)
			return true;

	return false;
}

/* Video */

static void sun6i_video_buffer_configure(struct sun6i_csi_device *csi_dev,
					 struct sun6i_csi_buffer *csi_buffer)
{
	csi_buffer->queued_to_csi = true;
	sun6i_csi_update_buf_addr(csi_dev, csi_buffer->dma_addr);
}

static void sun6i_video_configure(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_video *video = &csi_dev->video;
	struct sun6i_csi_config config = { 0 };

	config.pixelformat = video->format.fmt.pix.pixelformat;
	config.code = video->mbus_code;
	config.field = video->format.fmt.pix.field;
	config.width = video->format.fmt.pix.width;
	config.height = video->format.fmt.pix.height;

	sun6i_csi_update_config(csi_dev, &config);
}

/* Queue */

static int sun6i_video_queue_setup(struct vb2_queue *queue,
				   unsigned int *buffers_count,
				   unsigned int *planes_count,
				   unsigned int sizes[],
				   struct device *alloc_devs[])
{
	struct sun6i_csi_device *csi_dev = vb2_get_drv_priv(queue);
	struct sun6i_video *video = &csi_dev->video;
	unsigned int size = video->format.fmt.pix.sizeimage;

	if (*planes_count)
		return sizes[0] < size ? -EINVAL : 0;

	*planes_count = 1;
	sizes[0] = size;

	return 0;
}

static int sun6i_video_buffer_prepare(struct vb2_buffer *buffer)
{
	struct sun6i_csi_device *csi_dev = vb2_get_drv_priv(buffer->vb2_queue);
	struct sun6i_video *video = &csi_dev->video;
	struct v4l2_device *v4l2_dev = &csi_dev->v4l2.v4l2_dev;
	struct vb2_v4l2_buffer *v4l2_buffer = to_vb2_v4l2_buffer(buffer);
	struct sun6i_csi_buffer *csi_buffer =
		container_of(v4l2_buffer, struct sun6i_csi_buffer, v4l2_buffer);
	unsigned long size = video->format.fmt.pix.sizeimage;

	if (vb2_plane_size(buffer, 0) < size) {
		v4l2_err(v4l2_dev, "buffer too small (%lu < %lu)\n",
			 vb2_plane_size(buffer, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(buffer, 0, size);

	csi_buffer->dma_addr = vb2_dma_contig_plane_dma_addr(buffer, 0);
	v4l2_buffer->field = video->format.fmt.pix.field;

	return 0;
}

static void sun6i_video_buffer_queue(struct vb2_buffer *buffer)
{
	struct sun6i_csi_device *csi_dev = vb2_get_drv_priv(buffer->vb2_queue);
	struct sun6i_video *video = &csi_dev->video;
	struct vb2_v4l2_buffer *v4l2_buffer = to_vb2_v4l2_buffer(buffer);
	struct sun6i_csi_buffer *csi_buffer =
		container_of(v4l2_buffer, struct sun6i_csi_buffer, v4l2_buffer);
	unsigned long flags;

	spin_lock_irqsave(&video->dma_queue_lock, flags);
	csi_buffer->queued_to_csi = false;
	list_add_tail(&csi_buffer->list, &video->dma_queue);
	spin_unlock_irqrestore(&video->dma_queue_lock, flags);
}

static int sun6i_video_start_streaming(struct vb2_queue *queue,
				       unsigned int count)
{
	struct sun6i_csi_device *csi_dev = vb2_get_drv_priv(queue);
	struct sun6i_video *video = &csi_dev->video;
	struct video_device *video_dev = &video->video_dev;
	struct sun6i_csi_buffer *buf;
	struct sun6i_csi_buffer *next_buf;
	struct v4l2_subdev *subdev;
	unsigned long flags;
	int ret;

	video->sequence = 0;

	ret = video_device_pipeline_alloc_start(video_dev);
	if (ret < 0)
		goto error_dma_queue_flush;

	if (video->mbus_code == 0) {
		ret = -EINVAL;
		goto error_media_pipeline;
	}

	subdev = sun6i_video_remote_subdev(video, NULL);
	if (!subdev) {
		ret = -EINVAL;
		goto error_media_pipeline;
	}

	sun6i_video_configure(csi_dev);

	spin_lock_irqsave(&video->dma_queue_lock, flags);

	buf = list_first_entry(&video->dma_queue,
			       struct sun6i_csi_buffer, list);
	sun6i_video_buffer_configure(csi_dev, buf);

	sun6i_csi_set_stream(csi_dev, true);

	/*
	 * CSI will lookup the next dma buffer for next frame before the
	 * current frame done IRQ triggered. This is not documented
	 * but reported by Ondřej Jirman.
	 * The BSP code has workaround for this too. It skip to mark the
	 * first buffer as frame done for VB2 and pass the second buffer
	 * to CSI in the first frame done ISR call. Then in second frame
	 * done ISR call, it mark the first buffer as frame done for VB2
	 * and pass the third buffer to CSI. And so on. The bad thing is
	 * that the first buffer will be written twice and the first frame
	 * is dropped even the queued buffer is sufficient.
	 * So, I make some improvement here. Pass the next buffer to CSI
	 * just follow starting the CSI. In this case, the first frame
	 * will be stored in first buffer, second frame in second buffer.
	 * This method is used to avoid dropping the first frame, it
	 * would also drop frame when lacking of queued buffer.
	 */
	next_buf = list_next_entry(buf, list);
	sun6i_video_buffer_configure(csi_dev, next_buf);

	spin_unlock_irqrestore(&video->dma_queue_lock, flags);

	ret = v4l2_subdev_call(subdev, video, s_stream, 1);
	if (ret && ret != -ENOIOCTLCMD)
		goto error_stream;

	return 0;

error_stream:
	sun6i_csi_set_stream(csi_dev, false);

error_media_pipeline:
	video_device_pipeline_stop(video_dev);

error_dma_queue_flush:
	spin_lock_irqsave(&video->dma_queue_lock, flags);
	list_for_each_entry(buf, &video->dma_queue, list)
		vb2_buffer_done(&buf->v4l2_buffer.vb2_buf,
				VB2_BUF_STATE_QUEUED);
	INIT_LIST_HEAD(&video->dma_queue);
	spin_unlock_irqrestore(&video->dma_queue_lock, flags);

	return ret;
}

static void sun6i_video_stop_streaming(struct vb2_queue *queue)
{
	struct sun6i_csi_device *csi_dev = vb2_get_drv_priv(queue);
	struct sun6i_video *video = &csi_dev->video;
	struct v4l2_subdev *subdev;
	unsigned long flags;
	struct sun6i_csi_buffer *buf;

	subdev = sun6i_video_remote_subdev(video, NULL);
	if (subdev)
		v4l2_subdev_call(subdev, video, s_stream, 0);

	sun6i_csi_set_stream(csi_dev, false);

	video_device_pipeline_stop(&video->video_dev);

	/* Release all active buffers */
	spin_lock_irqsave(&video->dma_queue_lock, flags);
	list_for_each_entry(buf, &video->dma_queue, list)
		vb2_buffer_done(&buf->v4l2_buffer.vb2_buf, VB2_BUF_STATE_ERROR);
	INIT_LIST_HEAD(&video->dma_queue);
	spin_unlock_irqrestore(&video->dma_queue_lock, flags);
}

void sun6i_video_frame_done(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_video *video = &csi_dev->video;
	struct sun6i_csi_buffer *buf;
	struct sun6i_csi_buffer *next_buf;
	struct vb2_v4l2_buffer *v4l2_buffer;

	spin_lock(&video->dma_queue_lock);

	buf = list_first_entry(&video->dma_queue,
			       struct sun6i_csi_buffer, list);
	if (list_is_last(&buf->list, &video->dma_queue)) {
		dev_dbg(csi_dev->dev, "Frame dropped!\n");
		goto complete;
	}

	next_buf = list_next_entry(buf, list);
	/* If a new buffer (#next_buf) had not been queued to CSI, the old
	 * buffer (#buf) is still holding by CSI for storing the next
	 * frame. So, we queue a new buffer (#next_buf) to CSI then wait
	 * for next ISR call.
	 */
	if (!next_buf->queued_to_csi) {
		sun6i_video_buffer_configure(csi_dev, next_buf);
		dev_dbg(csi_dev->dev, "Frame dropped!\n");
		goto complete;
	}

	list_del(&buf->list);
	v4l2_buffer = &buf->v4l2_buffer;
	v4l2_buffer->vb2_buf.timestamp = ktime_get_ns();
	v4l2_buffer->sequence = video->sequence;
	vb2_buffer_done(&v4l2_buffer->vb2_buf, VB2_BUF_STATE_DONE);

	/* Prepare buffer for next frame but one.  */
	if (!list_is_last(&next_buf->list, &video->dma_queue)) {
		next_buf = list_next_entry(next_buf, list);
		sun6i_video_buffer_configure(csi_dev, next_buf);
	} else {
		dev_dbg(csi_dev->dev, "Next frame will be dropped!\n");
	}

complete:
	video->sequence++;
	spin_unlock(&video->dma_queue_lock);
}

static const struct vb2_ops sun6i_video_queue_ops = {
	.queue_setup		= sun6i_video_queue_setup,
	.buf_prepare		= sun6i_video_buffer_prepare,
	.buf_queue		= sun6i_video_buffer_queue,
	.start_streaming	= sun6i_video_start_streaming,
	.stop_streaming		= sun6i_video_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

/* V4L2 Device */

static int sun6i_video_querycap(struct file *file, void *private,
				struct v4l2_capability *capability)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);
	struct video_device *video_dev = &csi_dev->video.video_dev;

	strscpy(capability->driver, SUN6I_CSI_NAME, sizeof(capability->driver));
	strscpy(capability->card, video_dev->name, sizeof(capability->card));
	snprintf(capability->bus_info, sizeof(capability->bus_info),
		 "platform:%s", dev_name(csi_dev->dev));

	return 0;
}

static int sun6i_video_enum_fmt(struct file *file, void *private,
				struct v4l2_fmtdesc *fmtdesc)
{
	u32 index = fmtdesc->index;

	if (index >= ARRAY_SIZE(sun6i_video_formats))
		return -EINVAL;

	fmtdesc->pixelformat = sun6i_video_formats[index];

	return 0;
}

static int sun6i_video_g_fmt(struct file *file, void *private,
			     struct v4l2_format *format)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);
	struct sun6i_video *video = &csi_dev->video;

	*format = video->format;

	return 0;
}

static int sun6i_video_format_try(struct sun6i_video *video,
				  struct v4l2_format *format)
{
	struct v4l2_pix_format *pix_format = &format->fmt.pix;
	int bpp;

	if (!sun6i_video_format_check(pix_format->pixelformat))
		pix_format->pixelformat = sun6i_video_formats[0];

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

static int sun6i_video_format_set(struct sun6i_video *video,
				  struct v4l2_format *format)
{
	int ret;

	ret = sun6i_video_format_try(video, format);
	if (ret)
		return ret;

	video->format = *format;

	return 0;
}

static int sun6i_video_s_fmt(struct file *file, void *private,
			     struct v4l2_format *format)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);
	struct sun6i_video *video = &csi_dev->video;

	if (vb2_is_busy(&video->queue))
		return -EBUSY;

	return sun6i_video_format_set(video, format);
}

static int sun6i_video_try_fmt(struct file *file, void *private,
			       struct v4l2_format *format)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);
	struct sun6i_video *video = &csi_dev->video;

	return sun6i_video_format_try(video, format);
}

static int sun6i_video_enum_input(struct file *file, void *private,
				  struct v4l2_input *input)
{
	if (input->index != 0)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(input->name, "Camera", sizeof(input->name));

	return 0;
}

static int sun6i_video_g_input(struct file *file, void *private,
			       unsigned int *index)
{
	*index = 0;

	return 0;
}

static int sun6i_video_s_input(struct file *file, void *private,
			       unsigned int index)
{
	if (index != 0)
		return -EINVAL;

	return 0;
}

static const struct v4l2_ioctl_ops sun6i_video_ioctl_ops = {
	.vidioc_querycap		= sun6i_video_querycap,

	.vidioc_enum_fmt_vid_cap	= sun6i_video_enum_fmt,
	.vidioc_g_fmt_vid_cap		= sun6i_video_g_fmt,
	.vidioc_s_fmt_vid_cap		= sun6i_video_s_fmt,
	.vidioc_try_fmt_vid_cap		= sun6i_video_try_fmt,

	.vidioc_enum_input		= sun6i_video_enum_input,
	.vidioc_g_input			= sun6i_video_g_input,
	.vidioc_s_input			= sun6i_video_s_input,

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

static int sun6i_video_open(struct file *file)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);
	struct sun6i_video *video = &csi_dev->video;
	int ret = 0;

	if (mutex_lock_interruptible(&video->lock))
		return -ERESTARTSYS;

	ret = v4l2_fh_open(file);
	if (ret < 0)
		goto error_lock;

	ret = v4l2_pipeline_pm_get(&video->video_dev.entity);
	if (ret < 0)
		goto error_v4l2_fh;

	/* Power on at first open. */
	if (v4l2_fh_is_singular_file(file)) {
		ret = sun6i_csi_set_power(csi_dev, true);
		if (ret < 0)
			goto error_v4l2_fh;
	}

	mutex_unlock(&video->lock);

	return 0;

error_v4l2_fh:
	v4l2_fh_release(file);

error_lock:
	mutex_unlock(&video->lock);

	return ret;
}

static int sun6i_video_close(struct file *file)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);
	struct sun6i_video *video = &csi_dev->video;
	bool last_close;

	mutex_lock(&video->lock);

	last_close = v4l2_fh_is_singular_file(file);

	_vb2_fop_release(file, NULL);
	v4l2_pipeline_pm_put(&video->video_dev.entity);

	/* Power off at last close. */
	if (last_close)
		sun6i_csi_set_power(csi_dev, false);

	mutex_unlock(&video->lock);

	return 0;
}

static const struct v4l2_file_operations sun6i_video_fops = {
	.owner		= THIS_MODULE,
	.open		= sun6i_video_open,
	.release	= sun6i_video_close,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= vb2_fop_mmap,
	.poll		= vb2_fop_poll
};

/* Media Entity */

static int sun6i_video_link_validate_get_format(struct media_pad *pad,
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

static int sun6i_video_link_validate(struct media_link *link)
{
	struct video_device *vdev = container_of(link->sink->entity,
						 struct video_device, entity);
	struct sun6i_csi_device *csi_dev = video_get_drvdata(vdev);
	struct sun6i_video *video = &csi_dev->video;
	struct v4l2_subdev_format source_fmt;
	int ret;

	video->mbus_code = 0;

	if (!media_pad_remote_pad_first(link->sink->entity->pads)) {
		dev_info(csi_dev->dev, "video node %s pad not connected\n",
			 vdev->name);
		return -ENOLINK;
	}

	ret = sun6i_video_link_validate_get_format(link->source, &source_fmt);
	if (ret < 0)
		return ret;

	if (!sun6i_csi_is_format_supported(csi_dev,
					   video->format.fmt.pix.pixelformat,
					   source_fmt.format.code)) {
		dev_err(csi_dev->dev,
			"Unsupported pixformat: 0x%x with mbus code: 0x%x!\n",
			video->format.fmt.pix.pixelformat,
			source_fmt.format.code);
		return -EPIPE;
	}

	if (source_fmt.format.width != video->format.fmt.pix.width ||
	    source_fmt.format.height != video->format.fmt.pix.height) {
		dev_err(csi_dev->dev,
			"Wrong width or height %ux%u (%ux%u expected)\n",
			video->format.fmt.pix.width, video->format.fmt.pix.height,
			source_fmt.format.width, source_fmt.format.height);
		return -EPIPE;
	}

	video->mbus_code = source_fmt.format.code;

	return 0;
}

static const struct media_entity_operations sun6i_video_media_ops = {
	.link_validate = sun6i_video_link_validate
};

/* Video */

int sun6i_video_setup(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_video *video = &csi_dev->video;
	struct v4l2_device *v4l2_dev = &csi_dev->v4l2.v4l2_dev;
	struct video_device *video_dev = &video->video_dev;
	struct vb2_queue *queue = &video->queue;
	struct media_pad *pad = &video->pad;
	struct v4l2_format format = { 0 };
	struct v4l2_pix_format *pix_format = &format.fmt.pix;
	int ret;

	/* Media Entity */

	video_dev->entity.ops = &sun6i_video_media_ops;

	/* Media Pad */

	pad->flags = MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;

	ret = media_entity_pads_init(&video_dev->entity, 1, pad);
	if (ret < 0)
		return ret;

	/* DMA queue */

	INIT_LIST_HEAD(&video->dma_queue);
	spin_lock_init(&video->dma_queue_lock);

	video->sequence = 0;

	/* Queue */

	mutex_init(&video->lock);

	queue->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	queue->io_modes = VB2_MMAP | VB2_DMABUF;
	queue->buf_struct_size = sizeof(struct sun6i_csi_buffer);
	queue->ops = &sun6i_video_queue_ops;
	queue->mem_ops = &vb2_dma_contig_memops;
	queue->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	queue->lock = &video->lock;
	queue->dev = csi_dev->dev;
	queue->drv_priv = csi_dev;

	/* Make sure non-dropped frame. */
	queue->min_buffers_needed = 3;

	ret = vb2_queue_init(queue);
	if (ret) {
		v4l2_err(v4l2_dev, "failed to initialize vb2 queue: %d\n", ret);
		goto error_media_entity;
	}

	/* V4L2 Format */

	format.type = queue->type;
	pix_format->pixelformat = sun6i_video_formats[0];
	pix_format->width = 1280;
	pix_format->height = 720;
	pix_format->field = V4L2_FIELD_NONE;

	sun6i_video_format_set(video, &format);

	/* Video Device */

	strscpy(video_dev->name, SUN6I_CSI_NAME, sizeof(video_dev->name));
	video_dev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	video_dev->vfl_dir = VFL_DIR_RX;
	video_dev->release = video_device_release_empty;
	video_dev->fops = &sun6i_video_fops;
	video_dev->ioctl_ops = &sun6i_video_ioctl_ops;
	video_dev->v4l2_dev = v4l2_dev;
	video_dev->queue = queue;
	video_dev->lock = &video->lock;

	video_set_drvdata(video_dev, csi_dev);

	ret = video_register_device(video_dev, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "failed to register video device: %d\n",
			 ret);
		goto error_media_entity;
	}

	return 0;

error_media_entity:
	media_entity_cleanup(&video_dev->entity);

	mutex_destroy(&video->lock);

	return ret;
}

void sun6i_video_cleanup(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_video *video = &csi_dev->video;
	struct video_device *video_dev = &video->video_dev;

	vb2_video_unregister_device(video_dev);
	media_entity_cleanup(&video_dev->entity);
	mutex_destroy(&video->lock);
}
