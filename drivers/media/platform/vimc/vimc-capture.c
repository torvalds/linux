/*
 * vimc-capture.c Virtual Media Controller Driver
 *
 * Copyright (C) 2015-2017 Helen Koike <helen.fornazier@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/component.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>

#include "vimc-common.h"

#define VIMC_CAP_DRV_NAME "vimc-capture"

struct vimc_cap_device {
	struct vimc_ent_device ved;
	struct video_device vdev;
	struct device *dev;
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
	struct media_pipeline pipe;
};

static const struct v4l2_pix_format fmt_default = {
	.width = 640,
	.height = 480,
	.pixelformat = V4L2_PIX_FMT_RGB24,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_DEFAULT,
};

struct vimc_cap_buffer {
	/*
	 * struct vb2_v4l2_buffer must be the first element
	 * the videobuf2 framework will allocate this struct based on
	 * buf_struct_size and use the first sizeof(struct vb2_buffer) bytes of
	 * memory as a vb2_buffer
	 */
	struct vb2_v4l2_buffer vb2;
	struct list_head list;
};

static int vimc_cap_querycap(struct file *file, void *priv,
			     struct v4l2_capability *cap)
{
	struct vimc_cap_device *vcap = video_drvdata(file);

	strscpy(cap->driver, KBUILD_MODNAME, sizeof(cap->driver));
	strscpy(cap->card, KBUILD_MODNAME, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", vcap->vdev.v4l2_dev->name);

	return 0;
}

static void vimc_cap_get_format(struct vimc_ent_device *ved,
				struct v4l2_pix_format *fmt)
{
	struct vimc_cap_device *vcap = container_of(ved, struct vimc_cap_device,
						    ved);

	*fmt = vcap->format;
}

static int vimc_cap_g_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct vimc_cap_device *vcap = video_drvdata(file);

	f->fmt.pix = vcap->format;

	return 0;
}

static int vimc_cap_try_fmt_vid_cap(struct file *file, void *priv,
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

	return 0;
}

static int vimc_cap_s_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct vimc_cap_device *vcap = video_drvdata(file);

	/* Do not change the format while stream is on */
	if (vb2_is_busy(&vcap->queue))
		return -EBUSY;

	vimc_cap_try_fmt_vid_cap(file, priv, f);

	dev_dbg(vcap->dev, "%s: format update: "
		"old:%dx%d (0x%x, %d, %d, %d, %d) "
		"new:%dx%d (0x%x, %d, %d, %d, %d)\n", vcap->vdev.name,
		/* old */
		vcap->format.width, vcap->format.height,
		vcap->format.pixelformat, vcap->format.colorspace,
		vcap->format.quantization, vcap->format.xfer_func,
		vcap->format.ycbcr_enc,
		/* new */
		f->fmt.pix.width, f->fmt.pix.height,
		f->fmt.pix.pixelformat,	f->fmt.pix.colorspace,
		f->fmt.pix.quantization, f->fmt.pix.xfer_func,
		f->fmt.pix.ycbcr_enc);

	vcap->format = f->fmt.pix;

	return 0;
}

static int vimc_cap_enum_fmt_vid_cap(struct file *file, void *priv,
				     struct v4l2_fmtdesc *f)
{
	const struct vimc_pix_map *vpix = vimc_pix_map_by_index(f->index);

	if (!vpix)
		return -EINVAL;

	f->pixelformat = vpix->pixelformat;

	return 0;
}

static int vimc_cap_enum_framesizes(struct file *file, void *fh,
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
	fsize->stepwise.step_width = 2;
	fsize->stepwise.step_height = 2;

	return 0;
}

static const struct v4l2_file_operations vimc_cap_fops = {
	.owner		= THIS_MODULE,
	.open		= v4l2_fh_open,
	.release	= vb2_fop_release,
	.read           = vb2_fop_read,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap           = vb2_fop_mmap,
};

static const struct v4l2_ioctl_ops vimc_cap_ioctl_ops = {
	.vidioc_querycap = vimc_cap_querycap,

	.vidioc_g_fmt_vid_cap = vimc_cap_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = vimc_cap_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = vimc_cap_try_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap = vimc_cap_enum_fmt_vid_cap,
	.vidioc_enum_framesizes = vimc_cap_enum_framesizes,

	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
};

static void vimc_cap_return_all_buffers(struct vimc_cap_device *vcap,
					enum vb2_buffer_state state)
{
	struct vimc_cap_buffer *vbuf, *node;

	spin_lock(&vcap->qlock);

	list_for_each_entry_safe(vbuf, node, &vcap->buf_list, list) {
		list_del(&vbuf->list);
		vb2_buffer_done(&vbuf->vb2.vb2_buf, state);
	}

	spin_unlock(&vcap->qlock);
}

static int vimc_cap_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct vimc_cap_device *vcap = vb2_get_drv_priv(vq);
	struct media_entity *entity = &vcap->vdev.entity;
	int ret;

	vcap->sequence = 0;

	/* Start the media pipeline */
	ret = media_pipeline_start(entity, &vcap->pipe);
	if (ret) {
		vimc_cap_return_all_buffers(vcap, VB2_BUF_STATE_QUEUED);
		return ret;
	}

	/* Enable streaming from the pipe */
	ret = vimc_pipeline_s_stream(&vcap->vdev.entity, 1);
	if (ret) {
		media_pipeline_stop(entity);
		vimc_cap_return_all_buffers(vcap, VB2_BUF_STATE_QUEUED);
		return ret;
	}

	return 0;
}

/*
 * Stop the stream engine. Any remaining buffers in the stream queue are
 * dequeued and passed on to the vb2 framework marked as STATE_ERROR.
 */
static void vimc_cap_stop_streaming(struct vb2_queue *vq)
{
	struct vimc_cap_device *vcap = vb2_get_drv_priv(vq);

	/* Disable streaming from the pipe */
	vimc_pipeline_s_stream(&vcap->vdev.entity, 0);

	/* Stop the media pipeline */
	media_pipeline_stop(&vcap->vdev.entity);

	/* Release all active buffers */
	vimc_cap_return_all_buffers(vcap, VB2_BUF_STATE_ERROR);
}

static void vimc_cap_buf_queue(struct vb2_buffer *vb2_buf)
{
	struct vimc_cap_device *vcap = vb2_get_drv_priv(vb2_buf->vb2_queue);
	struct vimc_cap_buffer *buf = container_of(vb2_buf,
						   struct vimc_cap_buffer,
						   vb2.vb2_buf);

	spin_lock(&vcap->qlock);
	list_add_tail(&buf->list, &vcap->buf_list);
	spin_unlock(&vcap->qlock);
}

static int vimc_cap_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
				unsigned int *nplanes, unsigned int sizes[],
				struct device *alloc_devs[])
{
	struct vimc_cap_device *vcap = vb2_get_drv_priv(vq);

	if (*nplanes)
		return sizes[0] < vcap->format.sizeimage ? -EINVAL : 0;
	/* We don't support multiplanes for now */
	*nplanes = 1;
	sizes[0] = vcap->format.sizeimage;

	return 0;
}

static int vimc_cap_buffer_prepare(struct vb2_buffer *vb)
{
	struct vimc_cap_device *vcap = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long size = vcap->format.sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(vcap->dev, "%s: buffer too small (%lu < %lu)\n",
			vcap->vdev.name, vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}
	return 0;
}

static const struct vb2_ops vimc_cap_qops = {
	.start_streaming	= vimc_cap_start_streaming,
	.stop_streaming		= vimc_cap_stop_streaming,
	.buf_queue		= vimc_cap_buf_queue,
	.queue_setup		= vimc_cap_queue_setup,
	.buf_prepare		= vimc_cap_buffer_prepare,
	/*
	 * Since q->lock is set we can use the standard
	 * vb2_ops_wait_prepare/finish helper functions.
	 */
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

static const struct media_entity_operations vimc_cap_mops = {
	.link_validate		= vimc_link_validate,
};

static void vimc_cap_comp_unbind(struct device *comp, struct device *master,
				 void *master_data)
{
	struct vimc_ent_device *ved = dev_get_drvdata(comp);
	struct vimc_cap_device *vcap = container_of(ved, struct vimc_cap_device,
						    ved);

	vb2_queue_release(&vcap->queue);
	media_entity_cleanup(ved->ent);
	video_unregister_device(&vcap->vdev);
	vimc_pads_cleanup(vcap->ved.pads);
	kfree(vcap);
}

static void vimc_cap_process_frame(struct vimc_ent_device *ved,
				   struct media_pad *sink, const void *frame)
{
	struct vimc_cap_device *vcap = container_of(ved, struct vimc_cap_device,
						    ved);
	struct vimc_cap_buffer *vimc_buf;
	void *vbuf;

	spin_lock(&vcap->qlock);

	/* Get the first entry of the list */
	vimc_buf = list_first_entry_or_null(&vcap->buf_list,
					    typeof(*vimc_buf), list);
	if (!vimc_buf) {
		spin_unlock(&vcap->qlock);
		return;
	}

	/* Remove this entry from the list */
	list_del(&vimc_buf->list);

	spin_unlock(&vcap->qlock);

	/* Fill the buffer */
	vimc_buf->vb2.vb2_buf.timestamp = ktime_get_ns();
	vimc_buf->vb2.sequence = vcap->sequence++;
	vimc_buf->vb2.field = vcap->format.field;

	vbuf = vb2_plane_vaddr(&vimc_buf->vb2.vb2_buf, 0);

	memcpy(vbuf, frame, vcap->format.sizeimage);

	/* Set it as ready */
	vb2_set_plane_payload(&vimc_buf->vb2.vb2_buf, 0,
			      vcap->format.sizeimage);
	vb2_buffer_done(&vimc_buf->vb2.vb2_buf, VB2_BUF_STATE_DONE);
}

static int vimc_cap_comp_bind(struct device *comp, struct device *master,
			      void *master_data)
{
	struct v4l2_device *v4l2_dev = master_data;
	struct vimc_platform_data *pdata = comp->platform_data;
	const struct vimc_pix_map *vpix;
	struct vimc_cap_device *vcap;
	struct video_device *vdev;
	struct vb2_queue *q;
	int ret;

	/* Allocate the vimc_cap_device struct */
	vcap = kzalloc(sizeof(*vcap), GFP_KERNEL);
	if (!vcap)
		return -ENOMEM;

	/* Allocate the pads */
	vcap->ved.pads =
		vimc_pads_init(1, (const unsigned long[1]) {MEDIA_PAD_FL_SINK});
	if (IS_ERR(vcap->ved.pads)) {
		ret = PTR_ERR(vcap->ved.pads);
		goto err_free_vcap;
	}

	/* Initialize the media entity */
	vcap->vdev.entity.name = pdata->entity_name;
	vcap->vdev.entity.function = MEDIA_ENT_F_IO_V4L;
	ret = media_entity_pads_init(&vcap->vdev.entity,
				     1, vcap->ved.pads);
	if (ret)
		goto err_clean_pads;

	/* Initialize the lock */
	mutex_init(&vcap->lock);

	/* Initialize the vb2 queue */
	q = &vcap->queue;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->drv_priv = vcap;
	q->buf_struct_size = sizeof(struct vimc_cap_buffer);
	q->ops = &vimc_cap_qops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_buffers_needed = 2;
	q->lock = &vcap->lock;

	ret = vb2_queue_init(q);
	if (ret) {
		dev_err(comp, "%s: vb2 queue init failed (err=%d)\n",
			pdata->entity_name, ret);
		goto err_clean_m_ent;
	}

	/* Initialize buffer list and its lock */
	INIT_LIST_HEAD(&vcap->buf_list);
	spin_lock_init(&vcap->qlock);

	/* Set default frame format */
	vcap->format = fmt_default;
	vpix = vimc_pix_map_by_pixelformat(vcap->format.pixelformat);
	vcap->format.bytesperline = vcap->format.width * vpix->bpp;
	vcap->format.sizeimage = vcap->format.bytesperline *
				 vcap->format.height;

	/* Fill the vimc_ent_device struct */
	vcap->ved.ent = &vcap->vdev.entity;
	vcap->ved.process_frame = vimc_cap_process_frame;
	vcap->ved.vdev_get_format = vimc_cap_get_format;
	dev_set_drvdata(comp, &vcap->ved);
	vcap->dev = comp;

	/* Initialize the video_device struct */
	vdev = &vcap->vdev;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	vdev->entity.ops = &vimc_cap_mops;
	vdev->release = video_device_release_empty;
	vdev->fops = &vimc_cap_fops;
	vdev->ioctl_ops = &vimc_cap_ioctl_ops;
	vdev->lock = &vcap->lock;
	vdev->queue = q;
	vdev->v4l2_dev = v4l2_dev;
	vdev->vfl_dir = VFL_DIR_RX;
	strscpy(vdev->name, pdata->entity_name, sizeof(vdev->name));
	video_set_drvdata(vdev, &vcap->ved);

	/* Register the video_device with the v4l2 and the media framework */
	ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	if (ret) {
		dev_err(comp, "%s: video register failed (err=%d)\n",
			vcap->vdev.name, ret);
		goto err_release_queue;
	}

	return 0;

err_release_queue:
	vb2_queue_release(q);
err_clean_m_ent:
	media_entity_cleanup(&vcap->vdev.entity);
err_clean_pads:
	vimc_pads_cleanup(vcap->ved.pads);
err_free_vcap:
	kfree(vcap);

	return ret;
}

static const struct component_ops vimc_cap_comp_ops = {
	.bind = vimc_cap_comp_bind,
	.unbind = vimc_cap_comp_unbind,
};

static int vimc_cap_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &vimc_cap_comp_ops);
}

static int vimc_cap_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &vimc_cap_comp_ops);

	return 0;
}

static const struct platform_device_id vimc_cap_driver_ids[] = {
	{
		.name           = VIMC_CAP_DRV_NAME,
	},
	{ }
};

static struct platform_driver vimc_cap_pdrv = {
	.probe		= vimc_cap_probe,
	.remove		= vimc_cap_remove,
	.id_table	= vimc_cap_driver_ids,
	.driver		= {
		.name	= VIMC_CAP_DRV_NAME,
	},
};

module_platform_driver(vimc_cap_pdrv);

MODULE_DEVICE_TABLE(platform, vimc_cap_driver_ids);

MODULE_DESCRIPTION("Virtual Media Controller Driver (VIMC) Capture");
MODULE_AUTHOR("Helen Mae Koike Fornazier <helen.fornazier@gmail.com>");
MODULE_LICENSE("GPL");
