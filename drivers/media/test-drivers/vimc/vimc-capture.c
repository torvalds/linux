// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * vimc-capture.c Virtual Media Controller Driver
 *
 * Copyright (C) 2015-2017 Helen Koike <helen.fornazier@gmail.com>
 */

#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-vmalloc.h>

#include "vimc-common.h"
#include "vimc-streamer.h"

struct vimc_capture_device {
	struct vimc_ent_device ved;
	struct video_device vdev;
	struct v4l2_pix_format format;
	struct vb2_queue queue;
	struct list_head buf_list;
	/*
	 * NOTE: in a real driver, a spin lock must be used to access the
	 * queue because the frames are generated from a hardware interruption
	 * and the isr is not allowed to sleep.
	 * Even if it is not necessary a spinlock in the vimc driver, we
	 * use it here as a code reference
	 */
	spinlock_t qlock;
	struct mutex lock;
	u32 sequence;
	struct vimc_stream stream;
	struct media_pad pad;
};

static const struct v4l2_pix_format fmt_default = {
	.width = 640,
	.height = 480,
	.pixelformat = V4L2_PIX_FMT_RGB24,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_SRGB,
};

struct vimc_capture_buffer {
	/*
	 * struct vb2_v4l2_buffer must be the first element
	 * the videobuf2 framework will allocate this struct based on
	 * buf_struct_size and use the first sizeof(struct vb2_buffer) bytes of
	 * memory as a vb2_buffer
	 */
	struct vb2_v4l2_buffer vb2;
	struct list_head list;
};

static int vimc_capture_querycap(struct file *file, void *priv,
			     struct v4l2_capability *cap)
{
	strscpy(cap->driver, VIMC_PDEV_NAME, sizeof(cap->driver));
	strscpy(cap->card, KBUILD_MODNAME, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", VIMC_PDEV_NAME);

	return 0;
}

static void vimc_capture_get_format(struct vimc_ent_device *ved,
				struct v4l2_pix_format *fmt)
{
	struct vimc_capture_device *vcapture = container_of(ved, struct vimc_capture_device,
						    ved);

	*fmt = vcapture->format;
}

static int vimc_capture_g_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct vimc_capture_device *vcapture = video_drvdata(file);

	f->fmt.pix = vcapture->format;

	return 0;
}

static int vimc_capture_try_fmt_vid_cap(struct file *file, void *priv,
				    struct v4l2_format *f)
{
	struct v4l2_pix_format *format = &f->fmt.pix;
	const struct vimc_pix_map *vpix;

	format->width = clamp_t(u32, format->width, VIMC_FRAME_MIN_WIDTH,
				VIMC_FRAME_MAX_WIDTH) & ~1;
	format->height = clamp_t(u32, format->height, VIMC_FRAME_MIN_HEIGHT,
				 VIMC_FRAME_MAX_HEIGHT) & ~1;

	/* Don't accept a pixelformat that is not on the table */
	vpix = vimc_pix_map_by_pixelformat(format->pixelformat);
	if (!vpix) {
		format->pixelformat = fmt_default.pixelformat;
		vpix = vimc_pix_map_by_pixelformat(format->pixelformat);
	}
	/* TODO: Add support for custom bytesperline values */
	format->bytesperline = format->width * vpix->bpp;
	format->sizeimage = format->bytesperline * format->height;

	if (format->field == V4L2_FIELD_ANY)
		format->field = fmt_default.field;

	vimc_colorimetry_clamp(format);

	if (format->colorspace == V4L2_COLORSPACE_DEFAULT)
		format->colorspace = fmt_default.colorspace;

	return 0;
}

static int vimc_capture_s_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct vimc_capture_device *vcapture = video_drvdata(file);
	int ret;

	/* Do not change the format while stream is on */
	if (vb2_is_busy(&vcapture->queue))
		return -EBUSY;

	ret = vimc_capture_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	dev_dbg(vcapture->ved.dev, "%s: format update: "
		"old:%dx%d (0x%x, %d, %d, %d, %d) "
		"new:%dx%d (0x%x, %d, %d, %d, %d)\n", vcapture->vdev.name,
		/* old */
		vcapture->format.width, vcapture->format.height,
		vcapture->format.pixelformat, vcapture->format.colorspace,
		vcapture->format.quantization, vcapture->format.xfer_func,
		vcapture->format.ycbcr_enc,
		/* new */
		f->fmt.pix.width, f->fmt.pix.height,
		f->fmt.pix.pixelformat,	f->fmt.pix.colorspace,
		f->fmt.pix.quantization, f->fmt.pix.xfer_func,
		f->fmt.pix.ycbcr_enc);

	vcapture->format = f->fmt.pix;

	return 0;
}

static int vimc_capture_enum_fmt_vid_cap(struct file *file, void *priv,
				     struct v4l2_fmtdesc *f)
{
	const struct vimc_pix_map *vpix;

	if (f->mbus_code) {
		if (f->index > 0)
			return -EINVAL;

		vpix = vimc_pix_map_by_code(f->mbus_code);
	} else {
		vpix = vimc_pix_map_by_index(f->index);
	}

	if (!vpix)
		return -EINVAL;

	f->pixelformat = vpix->pixelformat;

	return 0;
}

static int vimc_capture_enum_framesizes(struct file *file, void *fh,
				    struct v4l2_frmsizeenum *fsize)
{
	const struct vimc_pix_map *vpix;

	if (fsize->index)
		return -EINVAL;

	/* Only accept code in the pix map table */
	vpix = vimc_pix_map_by_code(fsize->pixel_format);
	if (!vpix)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;
	fsize->stepwise.min_width = VIMC_FRAME_MIN_WIDTH;
	fsize->stepwise.max_width = VIMC_FRAME_MAX_WIDTH;
	fsize->stepwise.min_height = VIMC_FRAME_MIN_HEIGHT;
	fsize->stepwise.max_height = VIMC_FRAME_MAX_HEIGHT;
	fsize->stepwise.step_width = 1;
	fsize->stepwise.step_height = 1;

	return 0;
}

static const struct v4l2_file_operations vimc_capture_fops = {
	.owner		= THIS_MODULE,
	.open		= v4l2_fh_open,
	.release	= vb2_fop_release,
	.read           = vb2_fop_read,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap           = vb2_fop_mmap,
};

static const struct v4l2_ioctl_ops vimc_capture_ioctl_ops = {
	.vidioc_querycap = vimc_capture_querycap,

	.vidioc_g_fmt_vid_cap = vimc_capture_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = vimc_capture_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = vimc_capture_try_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap = vimc_capture_enum_fmt_vid_cap,
	.vidioc_enum_framesizes = vimc_capture_enum_framesizes,

	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_remove_bufs = vb2_ioctl_remove_bufs,
};

static void vimc_capture_return_all_buffers(struct vimc_capture_device *vcapture,
					enum vb2_buffer_state state)
{
	struct vimc_capture_buffer *vbuf, *node;

	spin_lock(&vcapture->qlock);

	list_for_each_entry_safe(vbuf, node, &vcapture->buf_list, list) {
		list_del(&vbuf->list);
		vb2_buffer_done(&vbuf->vb2.vb2_buf, state);
	}

	spin_unlock(&vcapture->qlock);
}

static int vimc_capture_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct vimc_capture_device *vcapture = vb2_get_drv_priv(vq);
	int ret;

	vcapture->sequence = 0;

	/* Start the media pipeline */
	ret = video_device_pipeline_start(&vcapture->vdev, &vcapture->stream.pipe);
	if (ret) {
		vimc_capture_return_all_buffers(vcapture, VB2_BUF_STATE_QUEUED);
		return ret;
	}

	ret = vimc_streamer_s_stream(&vcapture->stream, &vcapture->ved, 1);
	if (ret) {
		video_device_pipeline_stop(&vcapture->vdev);
		vimc_capture_return_all_buffers(vcapture, VB2_BUF_STATE_QUEUED);
		return ret;
	}

	return 0;
}

/*
 * Stop the stream engine. Any remaining buffers in the stream queue are
 * dequeued and passed on to the vb2 framework marked as STATE_ERROR.
 */
static void vimc_capture_stop_streaming(struct vb2_queue *vq)
{
	struct vimc_capture_device *vcapture = vb2_get_drv_priv(vq);

	vimc_streamer_s_stream(&vcapture->stream, &vcapture->ved, 0);

	/* Stop the media pipeline */
	video_device_pipeline_stop(&vcapture->vdev);

	/* Release all active buffers */
	vimc_capture_return_all_buffers(vcapture, VB2_BUF_STATE_ERROR);
}

static void vimc_capture_buf_queue(struct vb2_buffer *vb2_buf)
{
	struct vimc_capture_device *vcapture = vb2_get_drv_priv(vb2_buf->vb2_queue);
	struct vimc_capture_buffer *buf = container_of(vb2_buf,
						   struct vimc_capture_buffer,
						   vb2.vb2_buf);

	spin_lock(&vcapture->qlock);
	list_add_tail(&buf->list, &vcapture->buf_list);
	spin_unlock(&vcapture->qlock);
}

static int vimc_capture_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
				unsigned int *nplanes, unsigned int sizes[],
				struct device *alloc_devs[])
{
	struct vimc_capture_device *vcapture = vb2_get_drv_priv(vq);

	if (*nplanes)
		return sizes[0] < vcapture->format.sizeimage ? -EINVAL : 0;
	/* We don't support multiplanes for now */
	*nplanes = 1;
	sizes[0] = vcapture->format.sizeimage;

	return 0;
}

static int vimc_capture_buffer_prepare(struct vb2_buffer *vb)
{
	struct vimc_capture_device *vcapture = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long size = vcapture->format.sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(vcapture->ved.dev, "%s: buffer too small (%lu < %lu)\n",
			vcapture->vdev.name, vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}
	return 0;
}

static const struct vb2_ops vimc_capture_qops = {
	.start_streaming	= vimc_capture_start_streaming,
	.stop_streaming		= vimc_capture_stop_streaming,
	.buf_queue		= vimc_capture_buf_queue,
	.queue_setup		= vimc_capture_queue_setup,
	.buf_prepare		= vimc_capture_buffer_prepare,
	/*
	 * Since q->lock is set we can use the standard
	 * vb2_ops_wait_prepare/finish helper functions.
	 */
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

static const struct media_entity_operations vimc_capture_mops = {
	.link_validate		= vimc_vdev_link_validate,
};

static void vimc_capture_release(struct vimc_ent_device *ved)
{
	struct vimc_capture_device *vcapture =
		container_of(ved, struct vimc_capture_device, ved);

	media_entity_cleanup(vcapture->ved.ent);
	kfree(vcapture);
}

static void vimc_capture_unregister(struct vimc_ent_device *ved)
{
	struct vimc_capture_device *vcapture =
		container_of(ved, struct vimc_capture_device, ved);

	vb2_video_unregister_device(&vcapture->vdev);
}

static void *vimc_capture_process_frame(struct vimc_ent_device *ved,
				    const void *frame)
{
	struct vimc_capture_device *vcapture = container_of(ved, struct vimc_capture_device,
						    ved);
	struct vimc_capture_buffer *vimc_buf;
	void *vbuf;

	spin_lock(&vcapture->qlock);

	/* Get the first entry of the list */
	vimc_buf = list_first_entry_or_null(&vcapture->buf_list,
					    typeof(*vimc_buf), list);
	if (!vimc_buf) {
		spin_unlock(&vcapture->qlock);
		return ERR_PTR(-EAGAIN);
	}

	/* Remove this entry from the list */
	list_del(&vimc_buf->list);

	spin_unlock(&vcapture->qlock);

	/* Fill the buffer */
	vimc_buf->vb2.vb2_buf.timestamp = ktime_get_ns();
	vimc_buf->vb2.sequence = vcapture->sequence++;
	vimc_buf->vb2.field = vcapture->format.field;

	vbuf = vb2_plane_vaddr(&vimc_buf->vb2.vb2_buf, 0);

	memcpy(vbuf, frame, vcapture->format.sizeimage);

	/* Set it as ready */
	vb2_set_plane_payload(&vimc_buf->vb2.vb2_buf, 0,
			      vcapture->format.sizeimage);
	vb2_buffer_done(&vimc_buf->vb2.vb2_buf, VB2_BUF_STATE_DONE);
	return NULL;
}

static struct vimc_ent_device *vimc_capture_add(struct vimc_device *vimc,
					    const char *vcfg_name)
{
	struct v4l2_device *v4l2_dev = &vimc->v4l2_dev;
	const struct vimc_pix_map *vpix;
	struct vimc_capture_device *vcapture;
	struct video_device *vdev;
	struct vb2_queue *q;
	int ret;

	/* Allocate the vimc_capture_device struct */
	vcapture = kzalloc(sizeof(*vcapture), GFP_KERNEL);
	if (!vcapture)
		return ERR_PTR(-ENOMEM);

	/* Initialize the media entity */
	vcapture->vdev.entity.name = vcfg_name;
	vcapture->vdev.entity.function = MEDIA_ENT_F_IO_V4L;
	vcapture->pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vcapture->vdev.entity,
				     1, &vcapture->pad);
	if (ret)
		goto err_free_vcapture;

	/* Initialize the lock */
	mutex_init(&vcapture->lock);

	/* Initialize the vb2 queue */
	q = &vcapture->queue;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	if (vimc_allocator == VIMC_ALLOCATOR_VMALLOC)
		q->io_modes |= VB2_USERPTR;
	q->drv_priv = vcapture;
	q->buf_struct_size = sizeof(struct vimc_capture_buffer);
	q->ops = &vimc_capture_qops;
	q->mem_ops = vimc_allocator == VIMC_ALLOCATOR_DMA_CONTIG
		   ? &vb2_dma_contig_memops : &vb2_vmalloc_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_reqbufs_allocation = 2;
	q->lock = &vcapture->lock;
	q->dev = v4l2_dev->dev;

	ret = vb2_queue_init(q);
	if (ret) {
		dev_err(vimc->mdev.dev, "%s: vb2 queue init failed (err=%d)\n",
			vcfg_name, ret);
		goto err_clean_m_ent;
	}

	/* Initialize buffer list and its lock */
	INIT_LIST_HEAD(&vcapture->buf_list);
	spin_lock_init(&vcapture->qlock);

	/* Set default frame format */
	vcapture->format = fmt_default;
	vpix = vimc_pix_map_by_pixelformat(vcapture->format.pixelformat);
	vcapture->format.bytesperline = vcapture->format.width * vpix->bpp;
	vcapture->format.sizeimage = vcapture->format.bytesperline *
				 vcapture->format.height;

	/* Fill the vimc_ent_device struct */
	vcapture->ved.ent = &vcapture->vdev.entity;
	vcapture->ved.process_frame = vimc_capture_process_frame;
	vcapture->ved.vdev_get_format = vimc_capture_get_format;
	vcapture->ved.dev = vimc->mdev.dev;

	/* Initialize the video_device struct */
	vdev = &vcapture->vdev;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING
			  | V4L2_CAP_IO_MC;
	vdev->entity.ops = &vimc_capture_mops;
	vdev->release = video_device_release_empty;
	vdev->fops = &vimc_capture_fops;
	vdev->ioctl_ops = &vimc_capture_ioctl_ops;
	vdev->lock = &vcapture->lock;
	vdev->queue = q;
	vdev->v4l2_dev = v4l2_dev;
	vdev->vfl_dir = VFL_DIR_RX;
	strscpy(vdev->name, vcfg_name, sizeof(vdev->name));
	video_set_drvdata(vdev, &vcapture->ved);

	/* Register the video_device with the v4l2 and the media framework */
	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(vimc->mdev.dev, "%s: video register failed (err=%d)\n",
			vcapture->vdev.name, ret);
		goto err_clean_m_ent;
	}

	return &vcapture->ved;

err_clean_m_ent:
	media_entity_cleanup(&vcapture->vdev.entity);
err_free_vcapture:
	kfree(vcapture);

	return ERR_PTR(ret);
}

struct vimc_ent_type vimc_capture_type = {
	.add = vimc_capture_add,
	.unregister = vimc_capture_unregister,
	.release = vimc_capture_release
};
