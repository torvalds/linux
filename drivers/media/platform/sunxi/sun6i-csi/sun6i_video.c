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

struct sun6i_csi_buffer {
	struct vb2_v4l2_buffer		vb;
	struct list_head		list;

	dma_addr_t			dma_addr;
	bool				queued_to_csi;
};

static const u32 supported_pixformats[] = {
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
	V4L2_PIX_FMT_HM12,
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

static bool is_pixformat_valid(unsigned int pixformat)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_pixformats); i++)
		if (supported_pixformats[i] == pixformat)
			return true;

	return false;
}

static struct v4l2_subdev *
sun6i_video_remote_subdev(struct sun6i_video *video, u32 *pad)
{
	struct media_pad *remote;

	remote = media_entity_remote_pad(&video->pad);

	if (!remote || !is_media_entity_v4l2_subdev(remote->entity))
		return NULL;

	if (pad)
		*pad = remote->index;

	return media_entity_to_v4l2_subdev(remote->entity);
}

static int sun6i_video_queue_setup(struct vb2_queue *vq,
				   unsigned int *nbuffers,
				   unsigned int *nplanes,
				   unsigned int sizes[],
				   struct device *alloc_devs[])
{
	struct sun6i_video *video = vb2_get_drv_priv(vq);
	unsigned int size = video->fmt.fmt.pix.sizeimage;

	if (*nplanes)
		return sizes[0] < size ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = size;

	return 0;
}

static int sun6i_video_buffer_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct sun6i_csi_buffer *buf =
			container_of(vbuf, struct sun6i_csi_buffer, vb);
	struct sun6i_video *video = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long size = video->fmt.fmt.pix.sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		v4l2_err(video->vdev.v4l2_dev, "buffer too small (%lu < %lu)\n",
			 vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, size);

	buf->dma_addr = vb2_dma_contig_plane_dma_addr(vb, 0);

	vbuf->field = video->fmt.fmt.pix.field;

	return 0;
}

static int sun6i_video_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct sun6i_video *video = vb2_get_drv_priv(vq);
	struct sun6i_csi_buffer *buf;
	struct sun6i_csi_buffer *next_buf;
	struct sun6i_csi_config config;
	struct v4l2_subdev *subdev;
	unsigned long flags;
	int ret;

	video->sequence = 0;

	ret = media_pipeline_start(&video->vdev.entity, &video->vdev.pipe);
	if (ret < 0)
		goto clear_dma_queue;

	if (video->mbus_code == 0) {
		ret = -EINVAL;
		goto stop_media_pipeline;
	}

	subdev = sun6i_video_remote_subdev(video, NULL);
	if (!subdev)
		goto stop_media_pipeline;

	config.pixelformat = video->fmt.fmt.pix.pixelformat;
	config.code = video->mbus_code;
	config.field = video->fmt.fmt.pix.field;
	config.width = video->fmt.fmt.pix.width;
	config.height = video->fmt.fmt.pix.height;

	ret = sun6i_csi_update_config(video->csi, &config);
	if (ret < 0)
		goto stop_media_pipeline;

	spin_lock_irqsave(&video->dma_queue_lock, flags);

	buf = list_first_entry(&video->dma_queue,
			       struct sun6i_csi_buffer, list);
	buf->queued_to_csi = true;
	sun6i_csi_update_buf_addr(video->csi, buf->dma_addr);

	sun6i_csi_set_stream(video->csi, true);

	/*
	 * CSI will lookup the next dma buffer for next frame before the
	 * the current frame done IRQ triggered. This is not documented
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
	next_buf->queued_to_csi = true;
	sun6i_csi_update_buf_addr(video->csi, next_buf->dma_addr);

	spin_unlock_irqrestore(&video->dma_queue_lock, flags);

	ret = v4l2_subdev_call(subdev, video, s_stream, 1);
	if (ret && ret != -ENOIOCTLCMD)
		goto stop_csi_stream;

	return 0;

stop_csi_stream:
	sun6i_csi_set_stream(video->csi, false);
stop_media_pipeline:
	media_pipeline_stop(&video->vdev.entity);
clear_dma_queue:
	spin_lock_irqsave(&video->dma_queue_lock, flags);
	list_for_each_entry(buf, &video->dma_queue, list)
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_QUEUED);
	INIT_LIST_HEAD(&video->dma_queue);
	spin_unlock_irqrestore(&video->dma_queue_lock, flags);

	return ret;
}

static void sun6i_video_stop_streaming(struct vb2_queue *vq)
{
	struct sun6i_video *video = vb2_get_drv_priv(vq);
	struct v4l2_subdev *subdev;
	unsigned long flags;
	struct sun6i_csi_buffer *buf;

	subdev = sun6i_video_remote_subdev(video, NULL);
	if (subdev)
		v4l2_subdev_call(subdev, video, s_stream, 0);

	sun6i_csi_set_stream(video->csi, false);

	media_pipeline_stop(&video->vdev.entity);

	/* Release all active buffers */
	spin_lock_irqsave(&video->dma_queue_lock, flags);
	list_for_each_entry(buf, &video->dma_queue, list)
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	INIT_LIST_HEAD(&video->dma_queue);
	spin_unlock_irqrestore(&video->dma_queue_lock, flags);
}

static void sun6i_video_buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct sun6i_csi_buffer *buf =
			container_of(vbuf, struct sun6i_csi_buffer, vb);
	struct sun6i_video *video = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long flags;

	spin_lock_irqsave(&video->dma_queue_lock, flags);
	buf->queued_to_csi = false;
	list_add_tail(&buf->list, &video->dma_queue);
	spin_unlock_irqrestore(&video->dma_queue_lock, flags);
}

void sun6i_video_frame_done(struct sun6i_video *video)
{
	struct sun6i_csi_buffer *buf;
	struct sun6i_csi_buffer *next_buf;
	struct vb2_v4l2_buffer *vbuf;

	spin_lock(&video->dma_queue_lock);

	buf = list_first_entry(&video->dma_queue,
			       struct sun6i_csi_buffer, list);
	if (list_is_last(&buf->list, &video->dma_queue)) {
		dev_dbg(video->csi->dev, "Frame dropped!\n");
		goto unlock;
	}

	next_buf = list_next_entry(buf, list);
	/* If a new buffer (#next_buf) had not been queued to CSI, the old
	 * buffer (#buf) is still holding by CSI for storing the next
	 * frame. So, we queue a new buffer (#next_buf) to CSI then wait
	 * for next ISR call.
	 */
	if (!next_buf->queued_to_csi) {
		next_buf->queued_to_csi = true;
		sun6i_csi_update_buf_addr(video->csi, next_buf->dma_addr);
		dev_dbg(video->csi->dev, "Frame dropped!\n");
		goto unlock;
	}

	list_del(&buf->list);
	vbuf = &buf->vb;
	vbuf->vb2_buf.timestamp = ktime_get_ns();
	vbuf->sequence = video->sequence;
	vb2_buffer_done(&vbuf->vb2_buf, VB2_BUF_STATE_DONE);

	/* Prepare buffer for next frame but one.  */
	if (!list_is_last(&next_buf->list, &video->dma_queue)) {
		next_buf = list_next_entry(next_buf, list);
		next_buf->queued_to_csi = true;
		sun6i_csi_update_buf_addr(video->csi, next_buf->dma_addr);
	} else {
		dev_dbg(video->csi->dev, "Next frame will be dropped!\n");
	}

unlock:
	video->sequence++;
	spin_unlock(&video->dma_queue_lock);
}

static const struct vb2_ops sun6i_csi_vb2_ops = {
	.queue_setup		= sun6i_video_queue_setup,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
	.buf_prepare		= sun6i_video_buffer_prepare,
	.start_streaming	= sun6i_video_start_streaming,
	.stop_streaming		= sun6i_video_stop_streaming,
	.buf_queue		= sun6i_video_buffer_queue,
};

static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct sun6i_video *video = video_drvdata(file);

	strscpy(cap->driver, "sun6i-video", sizeof(cap->driver));
	strscpy(cap->card, video->vdev.name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 video->csi->dev->of_node->name);

	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	u32 index = f->index;

	if (index >= ARRAY_SIZE(supported_pixformats))
		return -EINVAL;

	f->pixelformat = supported_pixformats[index];

	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct sun6i_video *video = video_drvdata(file);

	*fmt = video->fmt;

	return 0;
}

static int sun6i_video_try_fmt(struct sun6i_video *video,
			       struct v4l2_format *f)
{
	struct v4l2_pix_format *pixfmt = &f->fmt.pix;
	int bpp;

	if (!is_pixformat_valid(pixfmt->pixelformat))
		pixfmt->pixelformat = supported_pixformats[0];

	v4l_bound_align_image(&pixfmt->width, MIN_WIDTH, MAX_WIDTH, 1,
			      &pixfmt->height, MIN_HEIGHT, MAX_WIDTH, 1, 1);

	bpp = sun6i_csi_get_bpp(pixfmt->pixelformat);
	pixfmt->bytesperline = (pixfmt->width * bpp) >> 3;
	pixfmt->sizeimage = pixfmt->bytesperline * pixfmt->height;

	if (pixfmt->field == V4L2_FIELD_ANY)
		pixfmt->field = V4L2_FIELD_NONE;

	pixfmt->colorspace = V4L2_COLORSPACE_RAW;
	pixfmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	pixfmt->quantization = V4L2_QUANTIZATION_DEFAULT;
	pixfmt->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	return 0;
}

static int sun6i_video_set_fmt(struct sun6i_video *video, struct v4l2_format *f)
{
	int ret;

	ret = sun6i_video_try_fmt(video, f);
	if (ret)
		return ret;

	video->fmt = *f;

	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct sun6i_video *video = video_drvdata(file);

	if (vb2_is_busy(&video->vb2_vidq))
		return -EBUSY;

	return sun6i_video_set_fmt(video, f);
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct sun6i_video *video = video_drvdata(file);

	return sun6i_video_try_fmt(video, f);
}

static int vidioc_enum_input(struct file *file, void *fh,
			     struct v4l2_input *inp)
{
	if (inp->index != 0)
		return -EINVAL;

	strscpy(inp->name, "camera", sizeof(inp->name));
	inp->type = V4L2_INPUT_TYPE_CAMERA;

	return 0;
}

static int vidioc_g_input(struct file *file, void *fh, unsigned int *i)
{
	*i = 0;

	return 0;
}

static int vidioc_s_input(struct file *file, void *fh, unsigned int i)
{
	if (i != 0)
		return -EINVAL;

	return 0;
}

static const struct v4l2_ioctl_ops sun6i_video_ioctl_ops = {
	.vidioc_querycap		= vidioc_querycap,
	.vidioc_enum_fmt_vid_cap	= vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= vidioc_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= vidioc_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= vidioc_try_fmt_vid_cap,

	.vidioc_enum_input		= vidioc_enum_input,
	.vidioc_s_input			= vidioc_s_input,
	.vidioc_g_input			= vidioc_g_input,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,

	.vidioc_log_status		= v4l2_ctrl_log_status,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

/* -----------------------------------------------------------------------------
 * V4L2 file operations
 */
static int sun6i_video_open(struct file *file)
{
	struct sun6i_video *video = video_drvdata(file);
	int ret;

	if (mutex_lock_interruptible(&video->lock))
		return -ERESTARTSYS;

	ret = v4l2_fh_open(file);
	if (ret < 0)
		goto unlock;

	ret = v4l2_pipeline_pm_use(&video->vdev.entity, 1);
	if (ret < 0)
		goto fh_release;

	/* check if already powered */
	if (!v4l2_fh_is_singular_file(file))
		goto unlock;

	ret = sun6i_csi_set_power(video->csi, true);
	if (ret < 0)
		goto fh_release;

	mutex_unlock(&video->lock);
	return 0;

fh_release:
	v4l2_fh_release(file);
unlock:
	mutex_unlock(&video->lock);
	return ret;
}

static int sun6i_video_close(struct file *file)
{
	struct sun6i_video *video = video_drvdata(file);
	bool last_fh;

	mutex_lock(&video->lock);

	last_fh = v4l2_fh_is_singular_file(file);

	_vb2_fop_release(file, NULL);

	v4l2_pipeline_pm_use(&video->vdev.entity, 0);

	if (last_fh)
		sun6i_csi_set_power(video->csi, false);

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

/* -----------------------------------------------------------------------------
 * Media Operations
 */
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
	struct sun6i_video *video = video_get_drvdata(vdev);
	struct v4l2_subdev_format source_fmt;
	int ret;

	video->mbus_code = 0;

	if (!media_entity_remote_pad(link->sink->entity->pads)) {
		dev_info(video->csi->dev,
			 "video node %s pad not connected\n", vdev->name);
		return -ENOLINK;
	}

	ret = sun6i_video_link_validate_get_format(link->source, &source_fmt);
	if (ret < 0)
		return ret;

	if (!sun6i_csi_is_format_supported(video->csi,
					   video->fmt.fmt.pix.pixelformat,
					   source_fmt.format.code)) {
		dev_err(video->csi->dev,
			"Unsupported pixformat: 0x%x with mbus code: 0x%x!\n",
			video->fmt.fmt.pix.pixelformat,
			source_fmt.format.code);
		return -EPIPE;
	}

	if (source_fmt.format.width != video->fmt.fmt.pix.width ||
	    source_fmt.format.height != video->fmt.fmt.pix.height) {
		dev_err(video->csi->dev,
			"Wrong width or height %ux%u (%ux%u expected)\n",
			video->fmt.fmt.pix.width, video->fmt.fmt.pix.height,
			source_fmt.format.width, source_fmt.format.height);
		return -EPIPE;
	}

	video->mbus_code = source_fmt.format.code;

	return 0;
}

static const struct media_entity_operations sun6i_video_media_ops = {
	.link_validate = sun6i_video_link_validate
};

int sun6i_video_init(struct sun6i_video *video, struct sun6i_csi *csi,
		     const char *name)
{
	struct video_device *vdev = &video->vdev;
	struct vb2_queue *vidq = &video->vb2_vidq;
	struct v4l2_format fmt = { 0 };
	int ret;

	video->csi = csi;

	/* Initialize the media entity... */
	video->pad.flags = MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;
	vdev->entity.ops = &sun6i_video_media_ops;
	ret = media_entity_pads_init(&vdev->entity, 1, &video->pad);
	if (ret < 0)
		return ret;

	mutex_init(&video->lock);

	INIT_LIST_HEAD(&video->dma_queue);
	spin_lock_init(&video->dma_queue_lock);

	video->sequence = 0;

	/* Setup default format */
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.pixelformat = supported_pixformats[0];
	fmt.fmt.pix.width = 1280;
	fmt.fmt.pix.height = 720;
	fmt.fmt.pix.field = V4L2_FIELD_NONE;
	sun6i_video_set_fmt(video, &fmt);

	/* Initialize videobuf2 queue */
	vidq->type			= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vidq->io_modes			= VB2_MMAP | VB2_DMABUF;
	vidq->drv_priv			= video;
	vidq->buf_struct_size		= sizeof(struct sun6i_csi_buffer);
	vidq->ops			= &sun6i_csi_vb2_ops;
	vidq->mem_ops			= &vb2_dma_contig_memops;
	vidq->timestamp_flags		= V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	vidq->lock			= &video->lock;
	/* Make sure non-dropped frame */
	vidq->min_buffers_needed	= 3;
	vidq->dev			= csi->dev;

	ret = vb2_queue_init(vidq);
	if (ret) {
		v4l2_err(&csi->v4l2_dev, "vb2_queue_init failed: %d\n", ret);
		goto clean_entity;
	}

	/* Register video device */
	strscpy(vdev->name, name, sizeof(vdev->name));
	vdev->release		= video_device_release_empty;
	vdev->fops		= &sun6i_video_fops;
	vdev->ioctl_ops		= &sun6i_video_ioctl_ops;
	vdev->vfl_type		= VFL_TYPE_VIDEO;
	vdev->vfl_dir		= VFL_DIR_RX;
	vdev->v4l2_dev		= &csi->v4l2_dev;
	vdev->queue		= vidq;
	vdev->lock		= &video->lock;
	vdev->device_caps	= V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_CAPTURE;
	video_set_drvdata(vdev, video);

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		v4l2_err(&csi->v4l2_dev,
			 "video_register_device failed: %d\n", ret);
		goto release_vb2;
	}

	return 0;

release_vb2:
	vb2_queue_release(&video->vb2_vidq);
clean_entity:
	media_entity_cleanup(&video->vdev.entity);
	mutex_destroy(&video->lock);
	return ret;
}

void sun6i_video_cleanup(struct sun6i_video *video)
{
	video_unregister_device(&video->vdev);
	media_entity_cleanup(&video->vdev.entity);
	vb2_queue_release(&video->vb2_vidq);
	mutex_destroy(&video->lock);
}
