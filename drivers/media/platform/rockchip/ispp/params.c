// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mc.h>
#include <linux/rkisp1-config.h>
#include <uapi/linux/rk-video-format.h>
#include "dev.h"
#include "regs.h"

#define RKISP1_ISP_PARAMS_REQ_BUFS_MIN	2
#define RKISP1_ISP_PARAMS_REQ_BUFS_MAX	8

static int rkispp_params_enum_fmt_meta_out(struct file *file, void *priv,
					   struct v4l2_fmtdesc *f)
{
	struct video_device *video = video_devdata(file);
	struct rkispp_params_vdev *params_vdev = video_get_drvdata(video);

	if (f->index > 0 || f->type != video->queue->type)
		return -EINVAL;

	f->pixelformat = params_vdev->vdev_fmt.fmt.meta.dataformat;

	return 0;
}

static int rkispp_params_g_fmt_meta_out(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct video_device *video = video_devdata(file);
	struct rkispp_params_vdev *params_vdev = video_get_drvdata(video);
	struct v4l2_meta_format *meta = &f->fmt.meta;

	if (f->type != video->queue->type)
		return -EINVAL;

	memset(meta, 0, sizeof(*meta));
	meta->dataformat = params_vdev->vdev_fmt.fmt.meta.dataformat;
	meta->buffersize = params_vdev->vdev_fmt.fmt.meta.buffersize;

	return 0;
}

static int rkispp_params_querycap(struct file *file,
				  void *priv, struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);
	struct rkispp_params_vdev *params_vdev = video_get_drvdata(vdev);

	snprintf(cap->driver, sizeof(cap->driver),
		 "%s_v%d", DRIVER_NAME,
		 params_vdev->dev->ispp_ver >> 4);
	strlcpy(cap->card, vdev->name, sizeof(cap->card));
	strlcpy(cap->bus_info, "platform: " DRIVER_NAME, sizeof(cap->bus_info));

	return 0;
}

static int rkispp_params_subs_evt(struct v4l2_fh *fh,
				  const struct v4l2_event_subscription *sub)
{
	struct rkispp_params_vdev *params_vdev = video_get_drvdata(fh->vdev);

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

static int rkispp_params_unsubs_evt(struct v4l2_fh *fh,
				    const struct v4l2_event_subscription *sub)
{
	struct rkispp_params_vdev *params_vdev = video_get_drvdata(fh->vdev);

	params_vdev->is_subs_evt = false;
	return v4l2_event_unsubscribe(fh, sub);
}

static const struct v4l2_ioctl_ops rkispp_params_ioctl = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_enum_fmt_meta_out = rkispp_params_enum_fmt_meta_out,
	.vidioc_g_fmt_meta_out = rkispp_params_g_fmt_meta_out,
	.vidioc_s_fmt_meta_out = rkispp_params_g_fmt_meta_out,
	.vidioc_try_fmt_meta_out = rkispp_params_g_fmt_meta_out,
	.vidioc_querycap = rkispp_params_querycap,
	.vidioc_subscribe_event = rkispp_params_subs_evt,
	.vidioc_unsubscribe_event = rkispp_params_unsubs_evt,
};

static int
rkispp_param_init_fecbuf(struct rkispp_params_vdev *params,
			 struct rkispp_fecbuf_size *fecsize)
{
	struct rkispp_device *pp_dev = params->dev;
	struct rkispp_fec_head *fec_data;
	u32 width, height, mesh_size, buf_size;
	int i, ret;

	width = fecsize->meas_width;
	height = fecsize->meas_height;
	mesh_size = cal_fec_mesh(width, height, fecsize->meas_mode);
	buf_size = ALIGN(sizeof(struct rkispp_fec_head), 16);
	buf_size += 2 * (ALIGN(mesh_size * 2, 16) + ALIGN(mesh_size, 16));

	if (fecsize->buf_cnt > FEC_MESH_BUF_MAX)
		params->buf_cnt = FEC_MESH_BUF_MAX;
	else if (fecsize->buf_cnt > 0)
		params->buf_cnt = fecsize->buf_cnt;
	else
		params->buf_cnt = FEC_MESH_BUF_NUM;

	params->buf_fec_idx = 0;
	for (i = 0; i < params->buf_cnt; i++) {
		params->buf_fec[i].is_need_vaddr = true;
		params->buf_fec[i].is_need_dbuf = true;
		params->buf_fec[i].is_need_dmafd = true;
		params->buf_fec[i].size = PAGE_ALIGN(buf_size);
		ret = rkispp_allow_buffer(params->dev, &params->buf_fec[i]);
		if (ret) {
			dev_err(pp_dev->dev, "can not alloc fec buffer\n");
			return ret;
		}

		fec_data = (struct rkispp_fec_head *)params->buf_fec[i].vaddr;
		fec_data->stat = FEC_BUF_INIT;
		fec_data->meshxf_oft = ALIGN(sizeof(struct rkispp_fec_head), 16);
		fec_data->meshyf_oft = fec_data->meshxf_oft + ALIGN(mesh_size, 16);
		fec_data->meshxi_oft = fec_data->meshyf_oft + ALIGN(mesh_size, 16);
		fec_data->meshyi_oft = fec_data->meshxi_oft + ALIGN(mesh_size * 2, 16);

		if (!i) {
			u32 val, dma_addr = params->buf_fec[i].dma_addr;

			val = dma_addr + fec_data->meshxf_oft;
			rkispp_write(pp_dev, RKISPP_FEC_MESH_XFRA_BASE, val);
			val = dma_addr + fec_data->meshyf_oft;
			rkispp_write(pp_dev, RKISPP_FEC_MESH_YFRA_BASE, val);
			val = dma_addr + fec_data->meshxi_oft;
			rkispp_write(pp_dev, RKISPP_FEC_MESH_XINT_BASE, val);
			val = dma_addr + fec_data->meshyi_oft;
			rkispp_write(pp_dev, RKISPP_FEC_MESH_YINT_BASE, val);
		}
		v4l2_dbg(1, rkispp_debug, &pp_dev->v4l2_dev,
			 "%s idx:%d fd:%d dma:%pad offset xf:0x%x yf:0x%x xi:0x%x yi:0x%x\n",
			 __func__, i, params->buf_fec[i].dma_fd, &params->buf_fec[i].dma_addr,
			 fec_data->meshxf_oft, fec_data->meshyf_oft,
			 fec_data->meshxi_oft, fec_data->meshyi_oft);
	}

	return 0;
}

static void
rkispp_param_deinit_fecbuf(struct rkispp_params_vdev *params)
{
	int i;

	params->buf_fec_idx = 0;
	for (i = 0; i < FEC_MESH_BUF_MAX; i++)
		rkispp_free_buffer(params->dev, &params->buf_fec[i]);
}

static int rkispp_params_vb2_queue_setup(struct vb2_queue *vq,
					 unsigned int *num_buffers,
					 unsigned int *num_planes,
					 unsigned int sizes[],
					 struct device *alloc_ctxs[])
{
	struct rkispp_params_vdev *params_vdev = vq->drv_priv;
	struct rkispp_device *dev = params_vdev->dev;

	*num_buffers = clamp_t(u32, *num_buffers,
			       RKISP1_ISP_PARAMS_REQ_BUFS_MIN,
			       RKISP1_ISP_PARAMS_REQ_BUFS_MAX);

	*num_planes = 1;
	if (dev->ispp_ver == ISPP_V10) {
		switch (params_vdev->vdev_id) {
		case PARAM_VDEV_TNR:
			sizes[0] = sizeof(struct rkispp_params_tnrcfg);
			break;
		case PARAM_VDEV_NR:
			sizes[0] = sizeof(struct rkispp_params_nrcfg);
			break;
		case PARAM_VDEV_FEC:
		default:
			sizes[0] = sizeof(struct rkispp_params_feccfg);
			break;
		}
	} else if (dev->ispp_ver == ISPP_V20) {
		sizes[0] = sizeof(struct fec_params_cfg);
	}

	INIT_LIST_HEAD(&params_vdev->params);
	params_vdev->first_params = true;

	return 0;
}

static void rkispp_params_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct rkispp_params_vdev *params_vdev = vq->drv_priv;

	params_vdev->params_ops->rkispp_params_vb2_buf_queue(vb);
}

static void rkispp_params_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct rkispp_params_vdev *params_vdev = vq->drv_priv;
	struct rkispp_buffer *buf;
	unsigned long flags;
	int i;

	/* stop params input firstly */
	spin_lock_irqsave(&params_vdev->config_lock, flags);
	params_vdev->streamon = false;
	wake_up(&params_vdev->dev->sync_onoff);
	spin_unlock_irqrestore(&params_vdev->config_lock, flags);

	for (i = 0; i < RKISP1_ISP_PARAMS_REQ_BUFS_MAX; i++) {
		spin_lock_irqsave(&params_vdev->config_lock, flags);
		if (!list_empty(&params_vdev->params)) {
			buf = list_first_entry(&params_vdev->params,
					       struct rkispp_buffer, queue);
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
		vb2_buffer_done(&params_vdev->cur_buf->vb.vb2_buf,
				VB2_BUF_STATE_ERROR);
		params_vdev->cur_buf = NULL;
	}
}

static int
rkispp_params_vb2_start_streaming(struct vb2_queue *queue, unsigned int count)
{
	struct rkispp_params_vdev *params_vdev = queue->drv_priv;
	unsigned long flags;

	spin_lock_irqsave(&params_vdev->config_lock, flags);
	params_vdev->streamon = true;
	spin_unlock_irqrestore(&params_vdev->config_lock, flags);

	return 0;
}

static int
rkispp_param_fh_open(struct file *filp)
{
	struct rkispp_params_vdev *params = video_drvdata(filp);
	struct rkispp_device *isppdev = params->dev;
	int ret;

	ret = v4l2_fh_open(filp);
	if (!ret) {
		ret = v4l2_pipeline_pm_get(&params->vnode.vdev.entity);
		if (ret < 0) {
			v4l2_err(&isppdev->v4l2_dev,
				 "pipeline power on failed %d\n", ret);
			goto ERR;
		}
	}

	return 0;

ERR:
	vb2_fop_release(filp);
	return ret;
}

static int
rkispp_param_fh_release(struct file *filp)
{
	struct rkispp_params_vdev *params = video_drvdata(filp);
	struct video_device *vdev = video_devdata(filp);
	int ret;

	if (filp->private_data == vdev->queue->owner)
		rkispp_param_deinit_fecbuf(params);

	ret = vb2_fop_release(filp);
	if (!ret)
		v4l2_pipeline_pm_put(&params->vnode.vdev.entity);
	return ret;
}

static struct vb2_ops rkispp_params_vb2_ops = {
	.queue_setup = rkispp_params_vb2_queue_setup,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.buf_queue = rkispp_params_vb2_buf_queue,
	.start_streaming = rkispp_params_vb2_start_streaming,
	.stop_streaming = rkispp_params_vb2_stop_streaming,
};

struct v4l2_file_operations rkispp_params_fops = {
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.open = rkispp_param_fh_open,
	.release = rkispp_param_fh_release,
};

static int
rkispp_params_init_vb2_queue(struct vb2_queue *q,
			     struct rkispp_params_vdev *params_vdev)
{
	q->type = V4L2_BUF_TYPE_META_OUTPUT;
	q->io_modes = VB2_MMAP | VB2_DMABUF | VB2_USERPTR;
	q->drv_priv = params_vdev;
	q->ops = &rkispp_params_vb2_ops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->buf_struct_size = sizeof(struct rkispp_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &params_vdev->dev->iqlock;
	q->dev = params_vdev->dev->hw_dev->dev;

	return vb2_queue_init(q);
}

void rkispp_params_get_fecbuf_inf(struct rkispp_params_vdev *params_vdev,
				  struct rkispp_fecbuf_info *fecbuf)
{
	int i;

	if (params_vdev->vdev_id != PARAM_VDEV_FEC)
		return;

	for (i = 0; i < FEC_MESH_BUF_MAX; i++) {
		fecbuf->buf_fd[i] = -1;
		fecbuf->buf_size[i] = 0;
	}

	for (i = 0; i < params_vdev->buf_cnt; i++) {
		fecbuf->buf_fd[i] = params_vdev->buf_fec[i].dma_fd;
		fecbuf->buf_size[i] = params_vdev->buf_fec[i].size;
	}
}

void rkispp_params_set_fecbuf_size(struct rkispp_params_vdev *params_vdev,
				   struct rkispp_fecbuf_size *fecsize)
{
	if (params_vdev->vdev_id != PARAM_VDEV_FEC)
		return;

	rkispp_param_deinit_fecbuf(params_vdev);
	rkispp_param_init_fecbuf(params_vdev, fecsize);
}

static int rkispp_register_params_vdev(struct rkispp_device *dev,
				       enum rkispp_paramvdev_id vdev_id)
{
	struct rkispp_params_vdev *params_vdev = &dev->params_vdev[vdev_id];
	struct rkispp_vdev_node *node = &params_vdev->vnode;
	struct video_device *vdev = &node->vdev;
	int ret;

	params_vdev->dev = dev;
	params_vdev->is_subs_evt = false;
	params_vdev->vdev_id = vdev_id;

	if (dev->ispp_ver == ISPP_V10)
		rkispp_params_init_ops_v10(params_vdev);
	if (dev->ispp_ver == ISPP_V20)
		rkispp_params_init_ops_v20(params_vdev);
	spin_lock_init(&params_vdev->config_lock);
	switch (vdev_id) {
	case PARAM_VDEV_TNR:
		strncpy(vdev->name, "rkispp_tnr_params", sizeof(vdev->name) - 1);
		break;
	case PARAM_VDEV_NR:
		strncpy(vdev->name, "rkispp_nr_params", sizeof(vdev->name) - 1);
		break;
	case PARAM_VDEV_FEC:
	default:
		params_vdev->buf_cnt = FEC_MESH_BUF_NUM;
		strncpy(vdev->name, "rkispp_fec_params", sizeof(vdev->name) - 1);
		break;
	}

	video_set_drvdata(vdev, params_vdev);
	vdev->ioctl_ops = &rkispp_params_ioctl;
	vdev->fops = &rkispp_params_fops;
	vdev->release = video_device_release_empty;
	/*
	 * Provide a mutex to v4l2 core. It will be used
	 * to protect all fops and v4l2 ioctls.
	 */
	vdev->lock = &dev->iqlock;
	vdev->v4l2_dev = &dev->v4l2_dev;
	vdev->queue = &node->buf_queue;
	vdev->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_META_OUTPUT;
	vdev->vfl_dir = VFL_DIR_TX;
	rkispp_params_init_vb2_queue(vdev->queue, params_vdev);
	params_vdev->vdev_fmt.fmt.meta.dataformat =
		V4L2_META_FMT_RK_ISPP_PARAMS;

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
	return 0;

err_cleanup_media_entity:
	media_entity_cleanup(&vdev->entity);
err_release_queue:
	vb2_queue_release(vdev->queue);

	return ret;
}

static void rkispp_unregister_params_vdev(struct rkispp_device *dev,
					  enum rkispp_paramvdev_id vdev_id)
{
	struct rkispp_params_vdev *params_vdev = &dev->params_vdev[vdev_id];
	struct rkispp_vdev_node *node = &params_vdev->vnode;
	struct video_device *vdev = &node->vdev;

	video_unregister_device(vdev);
	media_entity_cleanup(&vdev->entity);
	vb2_queue_release(vdev->queue);
}

int rkispp_register_params_vdevs(struct rkispp_device *dev)
{
	int ret = 0;

	ret = rkispp_register_params_vdev(dev, PARAM_VDEV_FEC);
	if (ret)
		return ret;

	if (dev->ispp_ver == ISPP_V10) {
		ret = rkispp_register_params_vdev(dev, PARAM_VDEV_TNR);
		if (ret) {
			rkispp_unregister_params_vdev(dev, PARAM_VDEV_FEC);
			return ret;
		}

		ret = rkispp_register_params_vdev(dev, PARAM_VDEV_NR);
		if (ret) {
			rkispp_unregister_params_vdev(dev, PARAM_VDEV_FEC);
			rkispp_unregister_params_vdev(dev, PARAM_VDEV_TNR);
			return ret;
		}
	}

	return ret;
}

void rkispp_unregister_params_vdevs(struct rkispp_device *dev)
{
	rkispp_unregister_params_vdev(dev, PARAM_VDEV_FEC);
	if (dev->ispp_ver == ISPP_V10) {
		rkispp_unregister_params_vdev(dev, PARAM_VDEV_TNR);
		rkispp_unregister_params_vdev(dev, PARAM_VDEV_NR);
	}
}
