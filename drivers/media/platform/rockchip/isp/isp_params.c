// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>	/* for ISP params */
#include <media/v4l2-event.h>
#include <linux/rk-preisp.h>
#include "dev.h"
#include "isp_params.h"
#include "isp_params_v1x.h"
#include "isp_params_v2x.h"
#include "isp_params_v21.h"

#define PARAMS_NAME DRIVER_NAME "-input-params"
#define RKISP_ISP_PARAMS_REQ_BUFS_MIN	2
#define RKISP_ISP_PARAMS_REQ_BUFS_MAX	8

static int rkisp_params_enum_fmt_meta_out(struct file *file, void *priv,
					  struct v4l2_fmtdesc *f)
{
	struct video_device *video = video_devdata(file);
	struct rkisp_isp_params_vdev *params_vdev = video_get_drvdata(video);

	if (f->index > 0 || f->type != video->queue->type)
		return -EINVAL;

	f->pixelformat = params_vdev->vdev_fmt.fmt.meta.dataformat;

	return 0;
}

static int rkisp_params_g_fmt_meta_out(struct file *file, void *fh,
				       struct v4l2_format *f)
{
	struct video_device *video = video_devdata(file);
	struct rkisp_isp_params_vdev *params_vdev = video_get_drvdata(video);
	struct v4l2_meta_format *meta = &f->fmt.meta;

	if (f->type != video->queue->type)
		return -EINVAL;

	memset(meta, 0, sizeof(*meta));
	meta->dataformat = params_vdev->vdev_fmt.fmt.meta.dataformat;
	meta->buffersize = params_vdev->vdev_fmt.fmt.meta.buffersize;

	return 0;
}

static int rkisp_params_querycap(struct file *file,
				 void *priv, struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);
	struct rkisp_isp_params_vdev *params_vdev = video_get_drvdata(vdev);

	snprintf(cap->driver, sizeof(cap->driver),
		 "%s_v%d", DRIVER_NAME,
		 params_vdev->dev->isp_ver >> 4);
	strlcpy(cap->card, vdev->name, sizeof(cap->card));
	strlcpy(cap->bus_info, "platform: " DRIVER_NAME, sizeof(cap->bus_info));

	return 0;
}

static int rkisp_params_subs_evt(struct v4l2_fh *fh,
				 const struct v4l2_event_subscription *sub)
{
	struct rkisp_isp_params_vdev *params_vdev = video_get_drvdata(fh->vdev);

	if (sub->id != 0)
		return -EINVAL;

	switch (sub->type) {
	case CIFISP_V4L2_EVENT_STREAM_START:
	case CIFISP_V4L2_EVENT_STREAM_STOP:
		params_vdev->is_subs_evt = true;
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	default:
		return -EINVAL;
	}
}

static int rkisp_params_unsubs_evt(struct v4l2_fh *fh,
				   const struct v4l2_event_subscription *sub)
{
	struct rkisp_isp_params_vdev *params_vdev = video_get_drvdata(fh->vdev);

	params_vdev->is_subs_evt = false;
	return v4l2_event_unsubscribe(fh, sub);
}

/* ISP params video device IOCTLs */
static const struct v4l2_ioctl_ops rkisp_params_ioctl = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_enum_fmt_meta_out = rkisp_params_enum_fmt_meta_out,
	.vidioc_g_fmt_meta_out = rkisp_params_g_fmt_meta_out,
	.vidioc_s_fmt_meta_out = rkisp_params_g_fmt_meta_out,
	.vidioc_try_fmt_meta_out = rkisp_params_g_fmt_meta_out,
	.vidioc_querycap = rkisp_params_querycap,
	.vidioc_subscribe_event = rkisp_params_subs_evt,
	.vidioc_unsubscribe_event = rkisp_params_unsubs_evt,
};

static int rkisp_params_vb2_queue_setup(struct vb2_queue *vq,
					unsigned int *num_buffers,
					unsigned int *num_planes,
					unsigned int sizes[],
					struct device *alloc_ctxs[])
{
	struct rkisp_isp_params_vdev *params_vdev = vq->drv_priv;

	*num_buffers = clamp_t(u32, *num_buffers,
			       RKISP_ISP_PARAMS_REQ_BUFS_MIN,
			       RKISP_ISP_PARAMS_REQ_BUFS_MAX);

	*num_planes = 1;
	params_vdev->ops->get_param_size(params_vdev, sizes);

	INIT_LIST_HEAD(&params_vdev->params);
	params_vdev->first_params = true;

	return 0;
}

static void rkisp_params_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkisp_buffer *params_buf = to_rkisp_buffer(vbuf);
	struct vb2_queue *vq = vb->vb2_queue;
	struct rkisp_isp_params_vdev *params_vdev = vq->drv_priv;
	void *first_param;
	unsigned long flags;

	unsigned int cur_frame_id = -1;
	cur_frame_id = atomic_read(&params_vdev->dev->isp_sdev.frm_sync_seq) - 1;
	if (params_vdev->first_params) {
		first_param = vb2_plane_vaddr(vb, 0);
		params_vdev->ops->save_first_param(params_vdev, first_param);
		vbuf->sequence = cur_frame_id;
		vb2_buffer_done(&params_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		params_vdev->first_params = false;
		wake_up(&params_vdev->dev->sync_onoff);
		return;
	}

	params_buf->vaddr[0] = vb2_plane_vaddr(vb, 0);
	spin_lock_irqsave(&params_vdev->config_lock, flags);
	list_add_tail(&params_buf->queue, &params_vdev->params);
	spin_unlock_irqrestore(&params_vdev->config_lock, flags);
}

static void rkisp_params_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct rkisp_isp_params_vdev *params_vdev = vq->drv_priv;
	struct rkisp_device *dev = params_vdev->dev;
	struct rkisp_buffer *buf;
	unsigned long flags;
	int i;

	/* stop params input firstly */
	spin_lock_irqsave(&params_vdev->config_lock, flags);
	params_vdev->streamon = false;
	wake_up(&dev->sync_onoff);
	spin_unlock_irqrestore(&params_vdev->config_lock, flags);

	for (i = 0; i < RKISP_ISP_PARAMS_REQ_BUFS_MAX; i++) {
		spin_lock_irqsave(&params_vdev->config_lock, flags);
		if (!list_empty(&params_vdev->params)) {
			buf = list_first_entry(&params_vdev->params,
					       struct rkisp_buffer, queue);
			list_del(&buf->queue);
			spin_unlock_irqrestore(&params_vdev->config_lock,
					       flags);
		} else {
			spin_unlock_irqrestore(&params_vdev->config_lock,
					       flags);
			break;
		}

		if (buf)
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		buf = NULL;
	}

	if (params_vdev->cur_buf) {
		buf = params_vdev->cur_buf;
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		params_vdev->cur_buf = NULL;
	}

	rkisp_params_disable_isp(params_vdev);
	/* clean module params */
	params_vdev->ops->clear_first_param(params_vdev);
	params_vdev->rdbk_times = 0;
}

static int
rkisp_params_vb2_start_streaming(struct vb2_queue *queue, unsigned int count)
{
	struct rkisp_isp_params_vdev *params_vdev = queue->drv_priv;
	unsigned long flags;

	params_vdev->hdrtmo_en = false;
	params_vdev->cur_buf = NULL;
	spin_lock_irqsave(&params_vdev->config_lock, flags);
	params_vdev->streamon = true;
	spin_unlock_irqrestore(&params_vdev->config_lock, flags);

	return 0;
}

static struct vb2_ops rkisp_params_vb2_ops = {
	.queue_setup = rkisp_params_vb2_queue_setup,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.buf_queue = rkisp_params_vb2_buf_queue,
	.start_streaming = rkisp_params_vb2_start_streaming,
	.stop_streaming = rkisp_params_vb2_stop_streaming,

};

static int rkisp_params_fh_open(struct file *filp)
{
	struct rkisp_isp_params_vdev *params = video_drvdata(filp);
	int ret;

	ret = v4l2_fh_open(filp);
	if (!ret) {
		ret = v4l2_pipeline_pm_use(&params->vnode.vdev.entity, 1);
		if (ret < 0)
			vb2_fop_release(filp);
	}

	return ret;
}

static int rkisp_params_fop_release(struct file *file)
{
	struct rkisp_isp_params_vdev *params = video_drvdata(file);
	struct video_device *vdev = video_devdata(file);
	int ret;

	if (file->private_data == vdev->queue->owner && params->ops->fop_release)
		params->ops->fop_release(params);

	ret = vb2_fop_release(file);
	if (!ret) {
		ret = v4l2_pipeline_pm_use(&params->vnode.vdev.entity, 0);
		if (ret < 0)
			v4l2_err(&params->dev->v4l2_dev,
				 "set pipeline power failed %d\n", ret);
	}
	return ret;
}

struct v4l2_file_operations rkisp_params_fops = {
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.open = rkisp_params_fh_open,
	.release = rkisp_params_fop_release
};

static int
rkisp_params_init_vb2_queue(struct vb2_queue *q,
			    struct rkisp_isp_params_vdev *params_vdev)
{
	q->type = V4L2_BUF_TYPE_META_OUTPUT;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->drv_priv = params_vdev;
	q->ops = &rkisp_params_vb2_ops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->buf_struct_size = sizeof(struct rkisp_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &params_vdev->dev->iqlock;
	q->dev = params_vdev->dev->dev;

	return vb2_queue_init(q);
}

static int rkisp_init_params_vdev(struct rkisp_isp_params_vdev *params_vdev)
{
	params_vdev->vdev_fmt.fmt.meta.dataformat =
		V4L2_META_FMT_RK_ISP1_PARAMS;
	params_vdev->vdev_fmt.fmt.meta.buffersize =
		sizeof(struct rkisp1_isp_params_cfg);

	if (params_vdev->dev->isp_ver <= ISP_V13)
		return rkisp_init_params_vdev_v1x(params_vdev);
	else if (params_vdev->dev->isp_ver == ISP_V21)
		return rkisp_init_params_vdev_v21(params_vdev);
	else
		return rkisp_init_params_vdev_v2x(params_vdev);
}

static void rkisp_uninit_params_vdev(struct rkisp_isp_params_vdev *params_vdev)
{
	if (params_vdev->dev->isp_ver <= ISP_V13)
		rkisp_uninit_params_vdev_v1x(params_vdev);
	else if (params_vdev->dev->isp_ver == ISP_V21)
		rkisp_uninit_params_vdev_v21(params_vdev);
	else
		rkisp_uninit_params_vdev_v2x(params_vdev);
}

void rkisp_params_cfg(struct rkisp_isp_params_vdev *params_vdev, u32 frame_id)
{
	if (params_vdev->ops->param_cfg)
		params_vdev->ops->param_cfg(params_vdev, frame_id, RKISP_PARAMS_IMD);
}

void rkisp_params_cfgsram(struct rkisp_isp_params_vdev *params_vdev)
{
	if (params_vdev->ops->param_cfgsram)
		params_vdev->ops->param_cfgsram(params_vdev);
}

void rkisp_params_isr(struct rkisp_isp_params_vdev *params_vdev,
		      u32 isp_mis)
{
	params_vdev->ops->isr_hdl(params_vdev, isp_mis);
}

/* Not called when the camera active, thus not isr protection. */
void rkisp_params_first_cfg(struct rkisp_isp_params_vdev *params_vdev,
			    struct ispsd_in_fmt *in_fmt,
			    enum v4l2_quantization quantization)
{
	params_vdev->quantization = quantization;
	params_vdev->raw_type = in_fmt->bayer_pat;
	params_vdev->in_mbus_code = in_fmt->mbus_code;
	params_vdev->ops->first_cfg(params_vdev);
}

/* Not called when the camera active, thus not isr protection. */
void rkisp_params_disable_isp(struct rkisp_isp_params_vdev *params_vdev)
{
	params_vdev->ops->disable_isp(params_vdev);
}

void rkisp_params_get_ldchbuf_inf(struct rkisp_isp_params_vdev *params_vdev,
				  struct rkisp_ldchbuf_info *ldchbuf)
{
	params_vdev->ops->get_ldchbuf_inf(params_vdev, ldchbuf);
}

void rkisp_params_set_ldchbuf_size(struct rkisp_isp_params_vdev *params_vdev,
				   struct rkisp_ldchbuf_size *ldchsize)
{
	params_vdev->ops->set_ldchbuf_size(params_vdev, ldchsize);
}

void rkisp_params_stream_stop(struct rkisp_isp_params_vdev *params_vdev)
{
	if (params_vdev->ops->stream_stop)
		params_vdev->ops->stream_stop(params_vdev);
}

int rkisp_register_params_vdev(struct rkisp_isp_params_vdev *params_vdev,
				struct v4l2_device *v4l2_dev,
				struct rkisp_device *dev)
{
	int ret;
	struct rkisp_vdev_node *node = &params_vdev->vnode;
	struct video_device *vdev = &node->vdev;
	struct media_entity *source, *sink;

	params_vdev->dev = dev;
	params_vdev->is_subs_evt = false;
	spin_lock_init(&params_vdev->config_lock);

	strlcpy(vdev->name, PARAMS_NAME, sizeof(vdev->name));

	vdev->ioctl_ops = &rkisp_params_ioctl;
	vdev->fops = &rkisp_params_fops;
	vdev->release = video_device_release_empty;
	/*
	 * Provide a mutex to v4l2 core. It will be used
	 * to protect all fops and v4l2 ioctls.
	 */
	vdev->lock = &dev->iqlock;
	vdev->v4l2_dev = v4l2_dev;
	vdev->queue = &node->buf_queue;
	vdev->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_META_OUTPUT;
	vdev->vfl_dir = VFL_DIR_TX;
	rkisp_params_init_vb2_queue(vdev->queue, params_vdev);
	ret = rkisp_init_params_vdev(params_vdev);
	if (ret < 0)
		goto err_release_queue;
	video_set_drvdata(vdev, params_vdev);

	node->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&vdev->entity, 1, &node->pad);
	if (ret < 0)
		goto err_release_queue;
	ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		dev_err(&vdev->dev,
			"could not register Video for Linux device\n");
		goto err_cleanup_media_entity;
	}

	source = &params_vdev->vnode.vdev.entity;
	sink = &params_vdev->dev->isp_sdev.sd.entity;
	ret = media_create_pad_link(source, 0, sink,
		RKISP_ISP_PAD_SINK_PARAMS, MEDIA_LNK_FL_ENABLED);
	if (ret < 0)
		goto err_unregister_video;

	return 0;

err_unregister_video:
	video_unregister_device(vdev);
err_cleanup_media_entity:
	media_entity_cleanup(&vdev->entity);
err_release_queue:
	vb2_queue_release(vdev->queue);
	rkisp_uninit_params_vdev(params_vdev);
	return ret;
}

void rkisp_unregister_params_vdev(struct rkisp_isp_params_vdev *params_vdev)
{
	struct rkisp_vdev_node *node = &params_vdev->vnode;
	struct video_device *vdev = &node->vdev;

	video_unregister_device(vdev);
	media_entity_cleanup(&vdev->entity);
	vb2_queue_release(vdev->queue);
	rkisp_uninit_params_vdev(params_vdev);
}

