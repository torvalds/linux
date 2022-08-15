// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#include <linux/kfifo.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-sg.h>
#include <media/videobuf2-vmalloc.h>	/* for ISP statistics */
#include "dev.h"
#include "isp_stats.h"
#include "isp_stats_v1x.h"
#include "isp_stats_v2x.h"
#include "isp_stats_v21.h"
#include "isp_stats_v3x.h"
#include "isp_stats_v32.h"

#define STATS_NAME DRIVER_NAME "-statistics"
#define RKISP_ISP_STATS_REQ_BUFS_MIN 2
#define RKISP_ISP_STATS_REQ_BUFS_MAX 8

static int rkisp_stats_enum_fmt_meta_cap(struct file *file, void *priv,
					  struct v4l2_fmtdesc *f)
{
	struct video_device *video = video_devdata(file);
	struct rkisp_isp_stats_vdev *stats_vdev = video_get_drvdata(video);

	if (f->index > 0 || f->type != video->queue->type)
		return -EINVAL;

	f->pixelformat = stats_vdev->vdev_fmt.fmt.meta.dataformat;
	return 0;
}

static int rkisp_stats_g_fmt_meta_cap(struct file *file, void *priv,
				       struct v4l2_format *f)
{
	struct video_device *video = video_devdata(file);
	struct rkisp_isp_stats_vdev *stats_vdev = video_get_drvdata(video);
	struct v4l2_meta_format *meta = &f->fmt.meta;

	if (f->type != video->queue->type)
		return -EINVAL;

	memset(meta, 0, sizeof(*meta));
	meta->dataformat = stats_vdev->vdev_fmt.fmt.meta.dataformat;
	meta->buffersize = stats_vdev->vdev_fmt.fmt.meta.buffersize;

	return 0;
}

static int rkisp_stats_querycap(struct file *file,
				 void *priv, struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);
	struct rkisp_isp_stats_vdev *stats_vdev = video_get_drvdata(vdev);

	strcpy(cap->driver, DRIVER_NAME);
	snprintf(cap->driver, sizeof(cap->driver),
		 "%s_v%d", DRIVER_NAME,
		 stats_vdev->dev->isp_ver >> 4);
	strlcpy(cap->card, vdev->name, sizeof(cap->card));
	strlcpy(cap->bus_info, "platform: " DRIVER_NAME, sizeof(cap->bus_info));
	cap->version = RKISP_DRIVER_VERSION;
	return 0;
}

/* ISP video device IOCTLs */
static const struct v4l2_ioctl_ops rkisp_stats_ioctl = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_enum_fmt_meta_cap = rkisp_stats_enum_fmt_meta_cap,
	.vidioc_g_fmt_meta_cap = rkisp_stats_g_fmt_meta_cap,
	.vidioc_s_fmt_meta_cap = rkisp_stats_g_fmt_meta_cap,
	.vidioc_try_fmt_meta_cap = rkisp_stats_g_fmt_meta_cap,
	.vidioc_querycap = rkisp_stats_querycap
};

static int rkisp_stats_fh_open(struct file *filp)
{
	struct rkisp_isp_stats_vdev *stats = video_drvdata(filp);
	int ret;

	ret = v4l2_fh_open(filp);
	if (!ret) {
		ret = v4l2_pipeline_pm_get(&stats->vnode.vdev.entity);
		if (ret < 0)
			vb2_fop_release(filp);
	}

	return ret;
}

static int rkisp_stats_fop_release(struct file *file)
{
	struct rkisp_isp_stats_vdev *stats = video_drvdata(file);
	int ret;

	ret = vb2_fop_release(file);
	if (!ret)
		v4l2_pipeline_pm_put(&stats->vnode.vdev.entity);
	return ret;
}

struct v4l2_file_operations rkisp_stats_fops = {
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.open = rkisp_stats_fh_open,
	.release = rkisp_stats_fop_release
};

static int rkisp_stats_vb2_queue_setup(struct vb2_queue *vq,
					unsigned int *num_buffers,
					unsigned int *num_planes,
					unsigned int sizes[],
					struct device *alloc_ctxs[])
{
	struct rkisp_isp_stats_vdev *stats_vdev = vq->drv_priv;

	*num_planes = 1;

	*num_buffers = clamp_t(u32, *num_buffers, RKISP_ISP_STATS_REQ_BUFS_MIN,
			       RKISP_ISP_STATS_REQ_BUFS_MAX);

	sizes[0] = stats_vdev->vdev_fmt.fmt.meta.buffersize;
	INIT_LIST_HEAD(&stats_vdev->stat);

	return 0;
}

static void rkisp_stats_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkisp_buffer *stats_buf = to_rkisp_buffer(vbuf);
	struct vb2_queue *vq = vb->vb2_queue;
	struct rkisp_isp_stats_vdev *stats_dev = vq->drv_priv;
	u32 size = stats_dev->vdev_fmt.fmt.meta.buffersize;
	unsigned long flags;

	stats_buf->vaddr[0] = vb2_plane_vaddr(vb, 0);
	if (stats_dev->dev->isp_ver == ISP_V32) {
		struct sg_table *sgt = vb2_dma_sg_plane_desc(vb, 0);

		stats_buf->buff_addr[0] = sg_dma_address(sgt->sgl);
	}
	if (stats_buf->vaddr[0])
		memset(stats_buf->vaddr[0], 0, size);
	spin_lock_irqsave(&stats_dev->rd_lock, flags);
	if (stats_dev->dev->isp_ver == ISP_V32 && stats_dev->dev->is_pre_on) {
		struct rkisp32_isp_stat_buffer *buf = stats_dev->stats_buf[0].vaddr;

		if (buf && !buf->frame_id && buf->meas_type && stats_buf->vaddr[0]) {
			memcpy(stats_buf->vaddr[0], buf, sizeof(struct rkisp32_isp_stat_buffer));
			buf->meas_type = 0;
			vb2_set_plane_payload(vb, 0, sizeof(struct rkisp32_isp_stat_buffer));
			vbuf->sequence = buf->frame_id;
			spin_unlock_irqrestore(&stats_dev->rd_lock, flags);
			vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
			return;
		}
	}
	list_add_tail(&stats_buf->queue, &stats_dev->stat);
	spin_unlock_irqrestore(&stats_dev->rd_lock, flags);
}

static void rkisp_stats_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct rkisp_isp_stats_vdev *stats_vdev = vq->drv_priv;
	struct rkisp_buffer *buf;
	unsigned long flags;
	int i;

	/* Make sure no new work queued in isr before draining wq */
	spin_lock_irqsave(&stats_vdev->irq_lock, flags);
	stats_vdev->streamon = false;
	spin_unlock_irqrestore(&stats_vdev->irq_lock, flags);

	tasklet_disable(&stats_vdev->rd_tasklet);

	spin_lock_irqsave(&stats_vdev->rd_lock, flags);
	for (i = 0; i < RKISP_ISP_STATS_REQ_BUFS_MAX; i++) {
		if (list_empty(&stats_vdev->stat))
			break;
		buf = list_first_entry(&stats_vdev->stat,
				       struct rkisp_buffer, queue);
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	if (stats_vdev->cur_buf) {
		vb2_buffer_done(&stats_vdev->cur_buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		if (stats_vdev->cur_buf == stats_vdev->nxt_buf)
			stats_vdev->nxt_buf = NULL;
		stats_vdev->cur_buf = NULL;
	}
	if (stats_vdev->nxt_buf) {
		vb2_buffer_done(&stats_vdev->nxt_buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		stats_vdev->nxt_buf = NULL;
	}
	spin_unlock_irqrestore(&stats_vdev->rd_lock, flags);

	stats_vdev->ae_meas_done_next = false;
	stats_vdev->af_meas_done_next = false;
}

static int
rkisp_stats_vb2_start_streaming(struct vb2_queue *queue,
				 unsigned int count)
{
	struct rkisp_isp_stats_vdev *stats_vdev = queue->drv_priv;

	stats_vdev->rdbk_drop = false;
	stats_vdev->cur_buf = NULL;
	stats_vdev->ops->rdbk_enable(stats_vdev, false);
	stats_vdev->streamon = true;
	kfifo_reset(&stats_vdev->rd_kfifo);
	tasklet_enable(&stats_vdev->rd_tasklet);

	return 0;
}

static struct vb2_ops rkisp_stats_vb2_ops = {
	.queue_setup = rkisp_stats_vb2_queue_setup,
	.buf_queue = rkisp_stats_vb2_buf_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.stop_streaming = rkisp_stats_vb2_stop_streaming,
	.start_streaming = rkisp_stats_vb2_start_streaming,
};

static int rkisp_stats_init_vb2_queue(struct vb2_queue *q,
				       struct rkisp_isp_stats_vdev *stats_vdev)
{
	q->type = V4L2_BUF_TYPE_META_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->drv_priv = stats_vdev;
	q->ops = &rkisp_stats_vb2_ops;
	if (stats_vdev->dev->isp_ver == ISP_V32) {
		q->mem_ops = stats_vdev->dev->hw_dev->mem_ops;
		if (stats_vdev->dev->hw_dev->is_dma_contig)
			q->dma_attrs = DMA_ATTR_FORCE_CONTIGUOUS;
	} else {
		q->mem_ops = &vb2_vmalloc_memops;
	}
	q->buf_struct_size = sizeof(struct rkisp_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &stats_vdev->dev->iqlock;
	q->dev = stats_vdev->dev->dev;
	return vb2_queue_init(q);
}

static void rkisp_stats_readout_task(unsigned long data)
{
	unsigned int out = 0;
	struct rkisp_isp_readout_work work;
	struct rkisp_isp_stats_vdev *vdev =
		(struct rkisp_isp_stats_vdev *)data;

	while (!kfifo_is_empty(&vdev->rd_kfifo)) {
		out = kfifo_out(&vdev->rd_kfifo,
				&work, sizeof(work));
		if (!out)
			break;

		if (work.readout == RKISP_ISP_READOUT_MEAS)
			vdev->ops->send_meas(vdev, &work);
	}
}

static void rkisp_init_stats_vdev(struct rkisp_isp_stats_vdev *stats_vdev)
{
	stats_vdev->rd_buf_idx = 0;
	stats_vdev->wr_buf_idx = 0;
	memset(stats_vdev->stats_buf, 0, sizeof(stats_vdev->stats_buf));

	if (stats_vdev->dev->isp_ver <= ISP_V13)
		rkisp_init_stats_vdev_v1x(stats_vdev);
	else if (stats_vdev->dev->isp_ver == ISP_V21)
		rkisp_init_stats_vdev_v21(stats_vdev);
	else if (stats_vdev->dev->isp_ver == ISP_V20)
		rkisp_init_stats_vdev_v2x(stats_vdev);
	else if (stats_vdev->dev->isp_ver == ISP_V30)
		rkisp_init_stats_vdev_v3x(stats_vdev);
	else
		rkisp_init_stats_vdev_v32(stats_vdev);
}

static void rkisp_uninit_stats_vdev(struct rkisp_isp_stats_vdev *stats_vdev)
{
	if (stats_vdev->dev->isp_ver <= ISP_V13)
		rkisp_uninit_stats_vdev_v1x(stats_vdev);
	else if (stats_vdev->dev->isp_ver == ISP_V21)
		rkisp_uninit_stats_vdev_v21(stats_vdev);
	else if (stats_vdev->dev->isp_ver == ISP_V20)
		rkisp_uninit_stats_vdev_v2x(stats_vdev);
	else if (stats_vdev->dev->isp_ver == ISP_V30)
		rkisp_uninit_stats_vdev_v3x(stats_vdev);
	else
		rkisp_uninit_stats_vdev_v32(stats_vdev);
}

void rkisp_stats_rdbk_enable(struct rkisp_isp_stats_vdev *stats_vdev, bool en)
{
	stats_vdev->ops->rdbk_enable(stats_vdev, en);
}

void rkisp_stats_first_ddr_config(struct rkisp_isp_stats_vdev *stats_vdev)
{
	if (stats_vdev->dev->isp_ver == ISP_V20)
		rkisp_stats_first_ddr_config_v2x(stats_vdev);
	else if (stats_vdev->dev->isp_ver == ISP_V21)
		rkisp_stats_first_ddr_config_v21(stats_vdev);
	else if (stats_vdev->dev->isp_ver == ISP_V30)
		rkisp_stats_first_ddr_config_v3x(stats_vdev);
	else if (stats_vdev->dev->isp_ver == ISP_V32)
		rkisp_stats_first_ddr_config_v32(stats_vdev);
}

void rkisp_stats_next_ddr_config(struct rkisp_isp_stats_vdev *stats_vdev)
{
	if (stats_vdev->dev->isp_ver == ISP_V32)
		rkisp_stats_next_ddr_config_v32(stats_vdev);
}

void rkisp_stats_isr(struct rkisp_isp_stats_vdev *stats_vdev,
		      u32 isp_ris, u32 isp3a_ris)
{
	stats_vdev->ops->isr_hdl(stats_vdev, isp_ris, isp3a_ris);
}

int rkisp_register_stats_vdev(struct rkisp_isp_stats_vdev *stats_vdev,
			      struct v4l2_device *v4l2_dev,
			      struct rkisp_device *dev)
{
	int ret;
	struct rkisp_vdev_node *node = &stats_vdev->vnode;
	struct video_device *vdev = &node->vdev;
	struct media_entity *source, *sink;

	stats_vdev->dev = dev;
	INIT_LIST_HEAD(&stats_vdev->stat);
	spin_lock_init(&stats_vdev->irq_lock);
	spin_lock_init(&stats_vdev->rd_lock);

	strlcpy(vdev->name, STATS_NAME, sizeof(vdev->name));

	vdev->ioctl_ops = &rkisp_stats_ioctl;
	vdev->fops = &rkisp_stats_fops;
	vdev->release = video_device_release_empty;
	vdev->lock = &dev->iqlock;
	vdev->v4l2_dev = v4l2_dev;
	vdev->queue = &node->buf_queue;
	vdev->device_caps = V4L2_CAP_META_CAPTURE | V4L2_CAP_STREAMING;
	vdev->vfl_dir =  VFL_DIR_RX;
	rkisp_stats_init_vb2_queue(vdev->queue, stats_vdev);
	rkisp_init_stats_vdev(stats_vdev);
	video_set_drvdata(vdev, stats_vdev);

	node->pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vdev->entity, 1, &node->pad);
	if (ret < 0)
		goto err_release_queue;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		dev_err(&vdev->dev,
			"could not register Video for Linux device\n");
		goto err_cleanup_media_entity;
	}

	source = &dev->isp_sdev.sd.entity;
	sink = &stats_vdev->vnode.vdev.entity;
	ret = media_create_pad_link(source, RKISP_ISP_PAD_SOURCE_STATS,
		sink, 0, MEDIA_LNK_FL_ENABLED);
	if (ret < 0)
		goto err_unregister_video;

	ret = kfifo_alloc(&stats_vdev->rd_kfifo,
			  RKISP_READOUT_WORK_SIZE,
			  GFP_KERNEL);
	if (ret) {
		dev_err(&vdev->dev,
			"kfifo_alloc failed with error %d\n",
			ret);
		goto err_unregister_video;
	}

	tasklet_init(&stats_vdev->rd_tasklet,
		     rkisp_stats_readout_task,
		     (unsigned long)stats_vdev);
	tasklet_disable(&stats_vdev->rd_tasklet);

	return 0;

err_unregister_video:
	video_unregister_device(vdev);
err_cleanup_media_entity:
	media_entity_cleanup(&vdev->entity);
err_release_queue:
	vb2_queue_release(vdev->queue);
	rkisp_uninit_stats_vdev(stats_vdev);
	return ret;
}

void rkisp_unregister_stats_vdev(struct rkisp_isp_stats_vdev *stats_vdev)
{
	struct rkisp_vdev_node *node = &stats_vdev->vnode;
	struct video_device *vdev = &node->vdev;

	kfifo_free(&stats_vdev->rd_kfifo);
	tasklet_kill(&stats_vdev->rd_tasklet);
	video_unregister_device(vdev);
	media_entity_cleanup(&vdev->entity);
	vb2_queue_release(vdev->queue);
	rkisp_uninit_stats_vdev(stats_vdev);
}

