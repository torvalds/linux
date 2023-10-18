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
#include "isp_params_v3x.h"
#include "isp_params_v32.h"
#include "regs.h"

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
	cap->version = RKISP_DRIVER_VERSION;
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
	struct rkisp_device *dev = params_vdev->dev;
	void *first_param;
	unsigned long flags;
	unsigned int cur_frame_id = -1;

	cur_frame_id = atomic_read(&dev->isp_sdev.frm_sync_seq) - 1;
	if (params_vdev->first_params) {
		first_param = vb2_plane_vaddr(vb, 0);
		params_vdev->ops->save_first_param(params_vdev, first_param);
		params_vdev->is_first_cfg = true;
		vbuf->sequence = cur_frame_id;
		vb2_buffer_done(&params_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		params_vdev->first_params = false;
		wake_up(&params_vdev->dev->sync_onoff);
		if (dev->is_first_double) {
			dev_info(dev->dev, "first params for fast\n");
			dev->is_first_double = false;
			dev->sw_rd_cnt = 0;
			if (dev->hw_dev->unite == ISP_UNITE_ONE) {
				dev->unite_index = ISP_UNITE_LEFT;
				dev->sw_rd_cnt += dev->hw_dev->is_multi_overflow ? 3 : 1;
			}
			params_vdev->rdbk_times = dev->sw_rd_cnt + 1;
			rkisp_trigger_read_back(dev, false, false, false);
		}
		dev_info(dev->dev, "first params buf queue\n");
		return;
	}

	if (dev->procfs.mode &
	    (RKISP_PROCFS_FIL_AIQ | RKISP_PROCFS_FIL_SW)) {
		vb2_buffer_done(&params_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		return;
	}

	params_buf->vaddr[0] = vb2_plane_vaddr(vb, 0);
	spin_lock_irqsave(&params_vdev->config_lock, flags);
	list_add_tail(&params_buf->queue, &params_vdev->params);
	spin_unlock_irqrestore(&params_vdev->config_lock, flags);

	if (params_vdev->dev->is_first_double) {
		struct isp32_isp_params_cfg *params = params_buf->vaddr[0];
		struct rkisp_buffer *buf;

		if (!(params->module_cfg_update & ISP32_MODULE_RTT_FST))
			return;
		spin_lock_irqsave(&params_vdev->config_lock, flags);
		while (!list_empty(&params_vdev->params)) {
			buf = list_first_entry(&params_vdev->params,
					       struct rkisp_buffer, queue);
			if (buf == params_buf)
				break;
			list_del(&buf->queue);
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		}
		spin_unlock_irqrestore(&params_vdev->config_lock, flags);
		dev_info(params_vdev->dev->dev,
			 "first params:%d for rtt resume\n", params->frame_id);
		params_vdev->dev->is_first_double = false;
		rkisp_trigger_read_back(params_vdev->dev, false, false, false);
	}
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

		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
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
	params_vdev->afaemode_en = false;
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

	if (!params->dev->is_probe_end)
		return -EINVAL;

	ret = v4l2_fh_open(filp);
	if (!ret) {
		ret = v4l2_pipeline_pm_get(&params->vnode.vdev.entity);
		if (ret < 0)
			vb2_fop_release(filp);
	}

	return ret;
}

static int rkisp_params_fop_release(struct file *file)
{
	struct rkisp_isp_params_vdev *params = video_drvdata(file);
	int ret;

	ret = vb2_fop_release(file);
	if (!ret)
		v4l2_pipeline_pm_put(&params->vnode.vdev.entity);
	return ret;
}

static __poll_t rkisp_params_fop_poll(struct file *file, poll_table *wait)
{
	struct video_device *vdev = video_devdata(file);

	/* buf done or subscribe event */
	if (vdev->queue->owner == file->private_data)
		return vb2_fop_poll(file, wait);
	else
		return v4l2_ctrl_poll(file, wait);
}

struct v4l2_file_operations rkisp_params_fops = {
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
	.poll = rkisp_params_fop_poll,
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
	int ret;

	if (params_vdev->dev->isp_ver <= ISP_V13)
		ret = rkisp_init_params_vdev_v1x(params_vdev);
	else if (params_vdev->dev->isp_ver == ISP_V21)
		ret = rkisp_init_params_vdev_v21(params_vdev);
	else if (params_vdev->dev->isp_ver == ISP_V20)
		ret = rkisp_init_params_vdev_v2x(params_vdev);
	else if (params_vdev->dev->isp_ver == ISP_V30)
		ret = rkisp_init_params_vdev_v3x(params_vdev);
	else
		ret = rkisp_init_params_vdev_v32(params_vdev);

	params_vdev->vdev_fmt.fmt.meta.dataformat =
		V4L2_META_FMT_RK_ISP1_PARAMS;
	if (params_vdev->ops && params_vdev->ops->get_param_size)
		params_vdev->ops->get_param_size(params_vdev,
			&params_vdev->vdev_fmt.fmt.meta.buffersize);
	return ret;
}

static void rkisp_uninit_params_vdev(struct rkisp_isp_params_vdev *params_vdev)
{
	if (params_vdev->dev->isp_ver <= ISP_V13)
		rkisp_uninit_params_vdev_v1x(params_vdev);
	else if (params_vdev->dev->isp_ver == ISP_V21)
		rkisp_uninit_params_vdev_v21(params_vdev);
	else if (params_vdev->dev->isp_ver == ISP_V20)
		rkisp_uninit_params_vdev_v2x(params_vdev);
	else if (params_vdev->dev->isp_ver == ISP_V30)
		rkisp_uninit_params_vdev_v3x(params_vdev);
	else
		rkisp_uninit_params_vdev_v32(params_vdev);
}

void rkisp_params_cfg(struct rkisp_isp_params_vdev *params_vdev, u32 frame_id)
{
	if (params_vdev->ops->param_cfg)
		params_vdev->ops->param_cfg(params_vdev, frame_id, RKISP_PARAMS_IMD);
}

void rkisp_params_cfgsram(struct rkisp_isp_params_vdev *params_vdev, bool is_check)
{
	if (is_check) {
		if (params_vdev->dev->procfs.mode & RKISP_PROCFS_FIL_SW)
			return;

		/* multi device to switch sram config */
		if (params_vdev->dev->hw_dev->is_single)
			return;
	}
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
	struct rkisp_device *dev = params_vdev->dev;

	if (!params_vdev->is_first_cfg)
		return;
	params_vdev->is_first_cfg = false;
	params_vdev->quantization = quantization;
	params_vdev->raw_type = in_fmt->bayer_pat;
	params_vdev->in_mbus_code = in_fmt->mbus_code;
	params_vdev->ops->first_cfg(params_vdev);
	/* update selfpath range if it output rgb format */
	if (params_vdev->quantization != quantization) {
		struct rkisp_stream *stream = &dev->cap_dev.stream[RKISP_STREAM_SP];
		u32 mask = CIF_MI_SP_Y_FULL_YUV2RGB | CIF_MI_SP_CBCR_FULL_YUV2RGB;

		quantization = params_vdev->quantization;
		if (stream->streaming &&
		    stream->out_isp_fmt.fmt_type == FMT_RGB)
			rkisp_unite_set_bits(dev, ISP3X_MI_WR_CTRL, mask,
					     quantization == V4L2_QUANTIZATION_FULL_RANGE ?
					     mask : 0, false);
		dev->isp_sdev.quantization = quantization;
	}
}

/* Not called when the camera active, thus not isr protection. */
void rkisp_params_disable_isp(struct rkisp_isp_params_vdev *params_vdev)
{
	if (params_vdev->ops->disable_isp)
		params_vdev->ops->disable_isp(params_vdev);
}

void rkisp_params_get_meshbuf_inf(struct rkisp_isp_params_vdev *params_vdev,
				  void *meshbuf)
{
	if (params_vdev->ops->get_meshbuf_inf)
		params_vdev->ops->get_meshbuf_inf(params_vdev, meshbuf);
}

int rkisp_params_set_meshbuf_size(struct rkisp_isp_params_vdev *params_vdev,
				  void *meshsize)
{
	if (params_vdev->ops->set_meshbuf_size)
		return params_vdev->ops->set_meshbuf_size(params_vdev,
							  meshsize);
	else
		return -EINVAL;
}

void rkisp_params_meshbuf_free(struct rkisp_isp_params_vdev *params_vdev, u64 id)
{
	/* isp working no to free buf */
	if (params_vdev->ops->free_meshbuf &&
	    !(params_vdev->dev->isp_state & ISP_START))
		params_vdev->ops->free_meshbuf(params_vdev, id);
}

void rkisp_params_stream_stop(struct rkisp_isp_params_vdev *params_vdev)
{
	/* isp stop to free buf */
	if (params_vdev->ops->stream_stop)
		params_vdev->ops->stream_stop(params_vdev);
	if (params_vdev->ops->fop_release)
		params_vdev->ops->fop_release(params_vdev);
}

bool rkisp_params_check_bigmode(struct rkisp_isp_params_vdev *params_vdev)
{
	if (params_vdev->ops->check_bigmode)
		return params_vdev->ops->check_bigmode(params_vdev);

	return 0;
}

int rkisp_params_info2ddr_cfg(struct rkisp_isp_params_vdev *params_vdev,
			       void *arg)
{
	int ret = -EINVAL;

	if (params_vdev->ops->info2ddr_cfg)
		ret = params_vdev->ops->info2ddr_cfg(params_vdev, arg);

	return ret;
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
	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
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

