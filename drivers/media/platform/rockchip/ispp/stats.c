// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#include <linux/kfifo.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>	/* for ISP statistics */
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-dma-sg.h>
#include <media/v4l2-mc.h>
#include <uapi/linux/rk-video-format.h>
#include "dev.h"
#include "regs.h"
#include "stats.h"

#define RKISPP_STATS_REQ_BUFS_MIN 2
#define RKISPP_STATS_REQ_BUFS_MAX 8

static void update_addr(struct rkispp_stats_vdev *stats_vdev)
{
	struct rkispp_dummy_buffer *dummy_buf;
	u32 addr;

	if (stats_vdev->curr_buf) {
		addr = stats_vdev->curr_buf->buff_addr[0];
		rkispp_write(stats_vdev->dev, RKISPP_ORB_WR_BASE, addr);
	}

	if (!stats_vdev->curr_buf) {
		dummy_buf = &stats_vdev->dev->hw_dev->dummy_buf;
		if (!dummy_buf->mem_priv)
			return;

		rkispp_write(stats_vdev->dev, RKISPP_ORB_WR_BASE, dummy_buf->dma_addr);
	}
}

static int rkispp_stats_frame_end(struct rkispp_stats_vdev *stats_vdev)
{
	void __iomem *base = stats_vdev->dev->hw_dev->base_addr;
	struct rkispp_device *dev = stats_vdev->dev;
	struct rkispp_stream_vdev *vdev = &dev->stream_vdev;
	unsigned long lock_flags = 0;

	if (stats_vdev->curr_buf) {
		u32 payload_size = 0;
		u64 ns = ktime_get_ns();
		u32 cur_frame_id = stats_vdev->frame_id;
		struct rkispp_buffer *curr_buf = stats_vdev->curr_buf;
		void *vaddr = vb2_plane_vaddr(&curr_buf->vb.vb2_buf, 0);

		if (stats_vdev->vdev_id == STATS_VDEV_TNR) {
			struct rkispp_stats_tnrbuf *tnrbuf = vaddr;

			payload_size = sizeof(struct rkispp_stats_tnrbuf);
			tnrbuf->frame_id = cur_frame_id;
			tnrbuf->gain.index = -1;
			tnrbuf->gainkg.index = -1;
			if (vdev->tnr.cur_wr) {
				tnrbuf->gain.index = vdev->tnr.cur_wr->didx[GROUP_BUF_GAIN];
				tnrbuf->gain.size = vdev->tnr.cur_wr->dbuf[GROUP_BUF_GAIN]->size;
				tnrbuf->gainkg.index = vdev->tnr.buf.gain_kg.index;
				tnrbuf->gainkg.size = vdev->tnr.buf.gain_kg.size;
			}
		} else if (stats_vdev->vdev_id == STATS_VDEV_NR) {
			struct rkispp_stats_nrbuf *nrbuf = vaddr;

			payload_size = sizeof(struct rkispp_stats_nrbuf);
			nrbuf->total_num = readl(base + RKISPP_ORB_TOTAL_NUM);
			nrbuf->frame_id = cur_frame_id;
			nrbuf->image.index = -1;
			if (vdev->nr.cur_wr &&
			    (dev->stream_vdev.module_ens & ISPP_MODULE_FEC_ST) == ISPP_MODULE_FEC_ST) {
				nrbuf->image.index = vdev->nr.cur_wr->index;
				nrbuf->image.size = vdev->nr.cur_wr->size;
				v4l2_dbg(3, rkispp_debug, &dev->v4l2_dev,
					 "%s frame:%d nr output buf index:%d fd:%d dma:%pad\n",
					 __func__, cur_frame_id,
					 vdev->nr.cur_wr->index,
					 vdev->nr.cur_wr->dma_fd,
					 &vdev->nr.cur_wr->dma_addr);
			}
		}

		curr_buf->vb.vb2_buf.timestamp = ns;
		curr_buf->vb.sequence = cur_frame_id;
		vb2_set_plane_payload(&curr_buf->vb.vb2_buf, 0, payload_size);
		vb2_buffer_done(&curr_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		stats_vdev->curr_buf = NULL;
	}

	spin_lock_irqsave(&stats_vdev->irq_lock, lock_flags);
	if (!list_empty(&stats_vdev->stat)) {
		stats_vdev->curr_buf = list_first_entry(&stats_vdev->stat,
					struct rkispp_buffer, queue);
		list_del(&stats_vdev->curr_buf->queue);
	}
	spin_unlock_irqrestore(&stats_vdev->irq_lock, lock_flags);

	if (stats_vdev->vdev_id == STATS_VDEV_NR)
		update_addr(stats_vdev);
	return 0;
}

static int rkispp_stats_enum_fmt_meta_cap(struct file *file, void *priv,
					  struct v4l2_fmtdesc *f)
{
	struct video_device *video = video_devdata(file);
	struct rkispp_stats_vdev *stats_vdev = video_get_drvdata(video);

	if (f->index > 0 || f->type != video->queue->type)
		return -EINVAL;

	f->pixelformat = stats_vdev->vdev_fmt.fmt.meta.dataformat;
	return 0;
}

static int rkispp_stats_g_fmt_meta_cap(struct file *file, void *priv,
				       struct v4l2_format *f)
{
	struct video_device *video = video_devdata(file);
	struct rkispp_stats_vdev *stats_vdev = video_get_drvdata(video);
	struct v4l2_meta_format *meta = &f->fmt.meta;

	if (f->type != video->queue->type)
		return -EINVAL;

	memset(meta, 0, sizeof(*meta));
	meta->dataformat = stats_vdev->vdev_fmt.fmt.meta.dataformat;
	meta->buffersize = stats_vdev->vdev_fmt.fmt.meta.buffersize;

	return 0;
}

static int rkispp_stats_querycap(struct file *file,
				 void *priv, struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);
	struct rkispp_stats_vdev *stats_vdev = video_get_drvdata(vdev);

	strcpy(cap->driver, DRIVER_NAME);
	snprintf(cap->driver, sizeof(cap->driver),
		 "%s_v%d", DRIVER_NAME,
		 stats_vdev->dev->ispp_ver >> 4);
	strlcpy(cap->card, vdev->name, sizeof(cap->card));
	strlcpy(cap->bus_info, "platform: " DRIVER_NAME, sizeof(cap->bus_info));

	return 0;
}

static int rkispp_stats_fh_open(struct file *filp)
{
	struct rkispp_stats_vdev *stats = video_drvdata(filp);
	struct rkispp_device *isppdev = stats->dev;
	int ret;

	ret = v4l2_fh_open(filp);
	if (!ret) {
		ret = v4l2_pipeline_pm_get(&stats->vnode.vdev.entity);
		if (ret < 0) {
			v4l2_err(&isppdev->v4l2_dev,
				 "pipeline power on failed %d\n", ret);
			vb2_fop_release(filp);
		}
	}
	return ret;
}

static int rkispp_stats_fh_release(struct file *filp)
{
	struct rkispp_stats_vdev *stats = video_drvdata(filp);
	int ret;

	ret = vb2_fop_release(filp);
	if (!ret)
		v4l2_pipeline_pm_put(&stats->vnode.vdev.entity);
	return ret;
}

/* ISP video device IOCTLs */
static const struct v4l2_ioctl_ops rkispp_stats_ioctl = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_enum_fmt_meta_cap = rkispp_stats_enum_fmt_meta_cap,
	.vidioc_g_fmt_meta_cap = rkispp_stats_g_fmt_meta_cap,
	.vidioc_s_fmt_meta_cap = rkispp_stats_g_fmt_meta_cap,
	.vidioc_try_fmt_meta_cap = rkispp_stats_g_fmt_meta_cap,
	.vidioc_querycap = rkispp_stats_querycap
};

struct v4l2_file_operations rkispp_stats_fops = {
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.open = rkispp_stats_fh_open,
	.release = rkispp_stats_fh_release,
};

static int rkispp_stats_vb2_queue_setup(struct vb2_queue *vq,
					unsigned int *num_buffers,
					unsigned int *num_planes,
					unsigned int sizes[],
					struct device *alloc_ctxs[])
{
	struct rkispp_stats_vdev *stats_vdev = vq->drv_priv;

	*num_planes = 1;

	*num_buffers = clamp_t(u32, *num_buffers, RKISPP_STATS_REQ_BUFS_MIN,
			       RKISPP_STATS_REQ_BUFS_MAX);

	switch (stats_vdev->vdev_id) {
	case STATS_VDEV_TNR:
		sizes[0] = sizeof(struct rkispp_stats_tnrbuf);
		break;
	case STATS_VDEV_NR:
	default:
		sizes[0] = sizeof(struct rkispp_stats_nrbuf);
		break;
	}
	INIT_LIST_HEAD(&stats_vdev->stat);

	return 0;
}

static void rkispp_stats_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkispp_buffer *buf = to_rkispp_buffer(vbuf);
	struct vb2_queue *vq = vb->vb2_queue;
	struct rkispp_stats_vdev *stats_dev = vq->drv_priv;
	unsigned long lock_flags = 0;

	vb2_plane_vaddr(vb, 0);
	if (stats_dev->dev->hw_dev->is_dma_sg_ops) {
		struct sg_table *sgt = vb2_dma_sg_plane_desc(vb, 0);

		buf->buff_addr[0] = sg_dma_address(sgt->sgl);
	} else {
		buf->buff_addr[0] = vb2_dma_contig_plane_dma_addr(vb, 0);
	}
	spin_lock_irqsave(&stats_dev->irq_lock, lock_flags);
	list_add_tail(&buf->queue, &stats_dev->stat);
	spin_unlock_irqrestore(&stats_dev->irq_lock, lock_flags);
}

static void destroy_buf_queue(struct rkispp_stats_vdev *stats_vdev,
			      enum vb2_buffer_state state)
{
	struct rkispp_buffer *buf;

	if (stats_vdev->curr_buf) {
		list_add_tail(&stats_vdev->curr_buf->queue, &stats_vdev->stat);
		stats_vdev->curr_buf = NULL;
	}
	while (!list_empty(&stats_vdev->stat)) {
		buf = list_first_entry(&stats_vdev->stat,
			struct rkispp_buffer, queue);
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
}

static void rkispp_stats_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct rkispp_stats_vdev *stats_vdev = vq->drv_priv;
	unsigned long flags;

	spin_lock_irqsave(&stats_vdev->irq_lock, flags);
	stats_vdev->streamon = false;
	destroy_buf_queue(stats_vdev, VB2_BUF_STATE_ERROR);
	spin_unlock_irqrestore(&stats_vdev->irq_lock, flags);
}

static int
rkispp_stats_vb2_start_streaming(struct vb2_queue *queue,
				 unsigned int count)
{
	struct rkispp_stats_vdev *stats_vdev = queue->drv_priv;
	unsigned long flags;

	if (stats_vdev->streamon)
		return -EBUSY;

	/* config first buf */
	rkispp_stats_frame_end(stats_vdev);

	spin_lock_irqsave(&stats_vdev->irq_lock, flags);
	stats_vdev->streamon = true;
	spin_unlock_irqrestore(&stats_vdev->irq_lock, flags);

	return 0;
}

static struct vb2_ops rkispp_stats_vb2_ops = {
	.queue_setup = rkispp_stats_vb2_queue_setup,
	.buf_queue = rkispp_stats_vb2_buf_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.stop_streaming = rkispp_stats_vb2_stop_streaming,
	.start_streaming = rkispp_stats_vb2_start_streaming,
};

static int rkispp_stats_init_vb2_queue(struct vb2_queue *q,
				       struct rkispp_stats_vdev *stats_vdev)
{
	q->type = V4L2_BUF_TYPE_META_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->drv_priv = stats_vdev;
	q->ops = &rkispp_stats_vb2_ops;
	q->mem_ops = stats_vdev->dev->hw_dev->mem_ops;
	q->buf_struct_size = sizeof(struct rkispp_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &stats_vdev->dev->iqlock;
	q->dev = stats_vdev->dev->hw_dev->dev;
	if (stats_vdev->dev->hw_dev->is_dma_contig)
		q->dma_attrs = DMA_ATTR_FORCE_CONTIGUOUS;
	q->gfp_flags = GFP_DMA32;
	return vb2_queue_init(q);
}

void rkispp_stats_isr(struct rkispp_stats_vdev *stats_vdev)
{
	spin_lock(&stats_vdev->irq_lock);
	if (!stats_vdev->streamon) {
		spin_unlock(&stats_vdev->irq_lock);
		return;
	}
	spin_unlock(&stats_vdev->irq_lock);

	rkispp_stats_frame_end(stats_vdev);
}

static void rkispp_init_stats_vdev(struct rkispp_stats_vdev *stats_vdev)
{
	stats_vdev->vdev_fmt.fmt.meta.dataformat = V4L2_META_FMT_RK_ISPP_STAT;
	switch (stats_vdev->vdev_id) {
	case STATS_VDEV_TNR:
		stats_vdev->vdev_fmt.fmt.meta.buffersize =
			sizeof(struct rkispp_stats_tnrbuf);
		break;
	case STATS_VDEV_NR:
	default:
		stats_vdev->vdev_fmt.fmt.meta.buffersize =
			sizeof(struct rkispp_stats_nrbuf);
		break;
	}
}

static int rkispp_register_stats_vdev(struct rkispp_device *dev,
				      enum rkispp_statsvdev_id vdev_id)
{
	struct rkispp_stats_vdev *stats_vdev = &dev->stats_vdev[vdev_id];
	struct rkispp_vdev_node *node = &stats_vdev->vnode;
	struct video_device *vdev = &node->vdev;
	int ret;

	stats_vdev->dev = dev;
	stats_vdev->vdev_id = vdev_id;
	INIT_LIST_HEAD(&stats_vdev->stat);
	spin_lock_init(&stats_vdev->irq_lock);

	switch (vdev_id) {
	case STATS_VDEV_TNR:
		strncpy(vdev->name, "rkispp_tnr_stats", sizeof(vdev->name) - 1);
		break;
	case STATS_VDEV_NR:
	default:
		strncpy(vdev->name, "rkispp_nr_stats", sizeof(vdev->name) - 1);
		break;
	}

	vdev->ioctl_ops = &rkispp_stats_ioctl;
	vdev->fops = &rkispp_stats_fops;
	vdev->release = video_device_release_empty;
	vdev->lock = &dev->iqlock;
	vdev->v4l2_dev = &dev->v4l2_dev;
	vdev->queue = &node->buf_queue;
	vdev->device_caps = V4L2_CAP_META_CAPTURE | V4L2_CAP_STREAMING;
	vdev->vfl_dir =  VFL_DIR_RX;
	rkispp_stats_init_vb2_queue(vdev->queue, stats_vdev);
	rkispp_init_stats_vdev(stats_vdev);
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

	return 0;

err_cleanup_media_entity:
	media_entity_cleanup(&vdev->entity);
err_release_queue:
	vb2_queue_release(vdev->queue);
	return ret;
}

static void rkispp_unregister_stats_vdev(struct rkispp_device *dev,
					 enum rkispp_statsvdev_id vdev_id)
{
	struct rkispp_stats_vdev *stats_vdev = &dev->stats_vdev[vdev_id];
	struct rkispp_vdev_node *node = &stats_vdev->vnode;
	struct video_device *vdev = &node->vdev;

	video_unregister_device(vdev);
	media_entity_cleanup(&vdev->entity);
	vb2_queue_release(vdev->queue);
}

int rkispp_register_stats_vdevs(struct rkispp_device *dev)
{
	int ret = 0;

	if (dev->ispp_ver != ISPP_V10)
		return 0;

	ret = rkispp_register_stats_vdev(dev, STATS_VDEV_TNR);
	if (ret)
		return ret;

	ret = rkispp_register_stats_vdev(dev, STATS_VDEV_NR);
	if (ret) {
		rkispp_unregister_stats_vdev(dev, STATS_VDEV_TNR);
		return ret;
	}

	return ret;
}

void rkispp_unregister_stats_vdevs(struct rkispp_device *dev)
{
	if (dev->ispp_ver != ISPP_V10)
		return;
	rkispp_unregister_stats_vdev(dev, STATS_VDEV_TNR);
	rkispp_unregister_stats_vdev(dev, STATS_VDEV_NR);
}
