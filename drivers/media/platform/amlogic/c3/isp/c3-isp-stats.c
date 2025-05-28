// SPDX-License-Identifier: (GPL-2.0-only OR MIT)
/*
 * Copyright (C) 2024 Amlogic, Inc. All rights reserved
 */

#include <linux/cleanup.h>
#include <linux/media/amlogic/c3-isp-config.h>
#include <linux/pm_runtime.h>

#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-dma-contig.h>

#include "c3-isp-common.h"
#include "c3-isp-regs.h"

/* Hardware configuration */

static void c3_isp_stats_cfg_dmawr_addr(struct c3_isp_stats *stats)
{
	u32 awb_dma_size = sizeof(struct c3_isp_awb_stats);
	u32 ae_dma_size = sizeof(struct c3_isp_ae_stats);
	u32 awb_dma_addr = stats->buff->dma_addr;
	u32 af_dma_addr;
	u32 ae_dma_addr;

	ae_dma_addr = awb_dma_addr + awb_dma_size;
	af_dma_addr = ae_dma_addr + ae_dma_size;

	c3_isp_update_bits(stats->isp, VIU_DMAWR_BADDR0,
			   VIU_DMAWR_BADDR0_AF_STATS_BASE_ADDR_MASK,
			   VIU_DMAWR_BADDR0_AF_STATS_BASE_ADDR(af_dma_addr));

	c3_isp_update_bits(stats->isp, VIU_DMAWR_BADDR1,
			   VIU_DMAWR_BADDR1_AWB_STATS_BASE_ADDR_MASK,
			   VIU_DMAWR_BADDR1_AWB_STATS_BASE_ADDR(awb_dma_addr));

	c3_isp_update_bits(stats->isp, VIU_DMAWR_BADDR2,
			   VIU_DMAWR_BADDR2_AE_STATS_BASE_ADDR_MASK,
			   VIU_DMAWR_BADDR2_AE_STATS_BASE_ADDR(ae_dma_addr));
}

static void c3_isp_stats_cfg_buff(struct c3_isp_stats *stats)
{
	stats->buff =
		list_first_entry_or_null(&stats->pending,
					 struct c3_isp_stats_buffer, list);
	if (stats->buff) {
		c3_isp_stats_cfg_dmawr_addr(stats);
		list_del(&stats->buff->list);
	}
}

void c3_isp_stats_pre_cfg(struct c3_isp_device *isp)
{
	struct c3_isp_stats *stats = &isp->stats;
	u32 dma_size;

	c3_isp_update_bits(stats->isp, ISP_AF_EN_CTRL,
			   ISP_AF_EN_CTRL_STAT_SEL_MASK,
			   ISP_AF_EN_CTRL_STAT_SEL_NEW);
	c3_isp_update_bits(stats->isp, ISP_AE_CTRL,
			   ISP_AE_CTRL_LUMA_MODE_MASK,
			   ISP_AE_CTRL_LUMA_MODE_FILTER);

	/* The unit of dma_size is 16 bytes */
	dma_size = sizeof(struct c3_isp_af_stats) / C3_ISP_DMA_SIZE_ALIGN_BYTES;
	c3_isp_update_bits(stats->isp, VIU_DMAWR_SIZE0,
			   VIU_DMAWR_SIZE0_AF_STATS_SIZE_MASK,
			   VIU_DMAWR_SIZE0_AF_STATS_SIZE(dma_size));

	dma_size = sizeof(struct c3_isp_awb_stats) /
		   C3_ISP_DMA_SIZE_ALIGN_BYTES;
	c3_isp_update_bits(stats->isp, VIU_DMAWR_SIZE0,
			   VIU_DMAWR_SIZE0_AWB_STATS_SIZE_MASK,
			   VIU_DMAWR_SIZE0_AWB_STATS_SIZE(dma_size));

	dma_size = sizeof(struct c3_isp_ae_stats) / C3_ISP_DMA_SIZE_ALIGN_BYTES;
	c3_isp_update_bits(stats->isp, VIU_DMAWR_SIZE1,
			   VIU_DMAWR_SIZE1_AE_STATS_SIZE_MASK,
			   VIU_DMAWR_SIZE1_AE_STATS_SIZE(dma_size));

	guard(spinlock_irqsave)(&stats->buff_lock);

	c3_isp_stats_cfg_buff(stats);
}

static int c3_isp_stats_querycap(struct file *file, void *fh,
				 struct v4l2_capability *cap)
{
	strscpy(cap->driver, C3_ISP_DRIVER_NAME, sizeof(cap->driver));
	strscpy(cap->card, "AML C3 ISP", sizeof(cap->card));

	return 0;
}

static int c3_isp_stats_enum_fmt(struct file *file, void *fh,
				 struct v4l2_fmtdesc *f)
{
	struct c3_isp_stats *stats = video_drvdata(file);

	if (f->index > 0 || f->type != stats->vb2_q.type)
		return -EINVAL;

	f->pixelformat = V4L2_META_FMT_C3ISP_STATS;

	return 0;
}

static int c3_isp_stats_g_fmt(struct file *file, void *fh,
			      struct v4l2_format *f)
{
	struct c3_isp_stats *stats = video_drvdata(file);

	f->fmt.meta = stats->vfmt.fmt.meta;

	return 0;
}

static const struct v4l2_ioctl_ops isp_stats_v4l2_ioctl_ops = {
	.vidioc_querycap                = c3_isp_stats_querycap,
	.vidioc_enum_fmt_meta_cap       = c3_isp_stats_enum_fmt,
	.vidioc_g_fmt_meta_cap          = c3_isp_stats_g_fmt,
	.vidioc_s_fmt_meta_cap          = c3_isp_stats_g_fmt,
	.vidioc_try_fmt_meta_cap        = c3_isp_stats_g_fmt,
	.vidioc_reqbufs	                = vb2_ioctl_reqbufs,
	.vidioc_querybuf                = vb2_ioctl_querybuf,
	.vidioc_qbuf                    = vb2_ioctl_qbuf,
	.vidioc_expbuf                  = vb2_ioctl_expbuf,
	.vidioc_dqbuf                   = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf             = vb2_ioctl_prepare_buf,
	.vidioc_create_bufs             = vb2_ioctl_create_bufs,
	.vidioc_streamon                = vb2_ioctl_streamon,
	.vidioc_streamoff               = vb2_ioctl_streamoff,
};

static const struct v4l2_file_operations isp_stats_v4l2_fops = {
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.poll = vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = vb2_fop_mmap,
};

static int c3_isp_stats_vb2_queue_setup(struct vb2_queue *q,
					unsigned int *num_buffers,
					unsigned int *num_planes,
					unsigned int sizes[],
					struct device *alloc_devs[])
{
	if (*num_planes) {
		if (*num_planes != 1)
			return -EINVAL;

		if (sizes[0] < sizeof(struct c3_isp_stats_info))
			return -EINVAL;

		return 0;
	}

	*num_planes = 1;
	sizes[0] = sizeof(struct c3_isp_stats_info);

	return 0;
}

static void c3_isp_stats_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *v4l2_buf = to_vb2_v4l2_buffer(vb);
	struct c3_isp_stats_buffer *buf =
			container_of(v4l2_buf, struct c3_isp_stats_buffer, vb);
	struct c3_isp_stats *stats = vb2_get_drv_priv(vb->vb2_queue);

	guard(spinlock_irqsave)(&stats->buff_lock);

	list_add_tail(&buf->list, &stats->pending);
}

static int c3_isp_stats_vb2_buf_prepare(struct vb2_buffer *vb)
{
	struct c3_isp_stats *stats = vb2_get_drv_priv(vb->vb2_queue);
	unsigned int size = stats->vfmt.fmt.meta.buffersize;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(stats->isp->dev,
			"User buffer too small (%ld < %u)\n",
			vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, size);

	return 0;
}

static int c3_isp_stats_vb2_buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *v4l2_buf = to_vb2_v4l2_buffer(vb);
	struct c3_isp_stats_buffer *buf =
			container_of(v4l2_buf, struct c3_isp_stats_buffer, vb);

	buf->dma_addr = vb2_dma_contig_plane_dma_addr(vb, 0);

	return 0;
}

static void c3_isp_stats_vb2_stop_streaming(struct vb2_queue *q)
{
	struct c3_isp_stats *stats = vb2_get_drv_priv(q);

	guard(spinlock_irqsave)(&stats->buff_lock);

	if (stats->buff) {
		vb2_buffer_done(&stats->buff->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		stats->buff = NULL;
	}

	while (!list_empty(&stats->pending)) {
		struct c3_isp_stats_buffer *buff;

		buff = list_first_entry(&stats->pending,
					struct c3_isp_stats_buffer, list);
		list_del(&buff->list);
		vb2_buffer_done(&buff->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
}

static const struct vb2_ops isp_stats_vb2_ops = {
	.queue_setup = c3_isp_stats_vb2_queue_setup,
	.buf_queue = c3_isp_stats_vb2_buf_queue,
	.buf_prepare = c3_isp_stats_vb2_buf_prepare,
	.buf_init = c3_isp_stats_vb2_buf_init,
	.stop_streaming = c3_isp_stats_vb2_stop_streaming,
};

int c3_isp_stats_register(struct c3_isp_device *isp)
{
	struct c3_isp_stats *stats = &isp->stats;
	struct video_device *vdev = &stats->vdev;
	struct vb2_queue *vb2_q = &stats->vb2_q;
	int ret;

	memset(stats, 0, sizeof(*stats));
	stats->vfmt.fmt.meta.dataformat = V4L2_META_FMT_C3ISP_STATS;
	stats->vfmt.fmt.meta.buffersize = sizeof(struct c3_isp_stats_info);
	stats->isp = isp;
	INIT_LIST_HEAD(&stats->pending);
	spin_lock_init(&stats->buff_lock);

	mutex_init(&stats->lock);

	snprintf(vdev->name, sizeof(vdev->name), "c3-isp-stats");
	vdev->fops = &isp_stats_v4l2_fops;
	vdev->ioctl_ops = &isp_stats_v4l2_ioctl_ops;
	vdev->v4l2_dev = &isp->v4l2_dev;
	vdev->lock = &stats->lock;
	vdev->minor = -1;
	vdev->queue = vb2_q;
	vdev->release = video_device_release_empty;
	vdev->device_caps = V4L2_CAP_META_CAPTURE | V4L2_CAP_STREAMING;
	vdev->vfl_dir = VFL_DIR_RX;
	video_set_drvdata(vdev, stats);

	vb2_q->drv_priv = stats;
	vb2_q->mem_ops = &vb2_dma_contig_memops;
	vb2_q->ops = &isp_stats_vb2_ops;
	vb2_q->type = V4L2_BUF_TYPE_META_CAPTURE;
	vb2_q->io_modes = VB2_DMABUF | VB2_MMAP;
	vb2_q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	vb2_q->buf_struct_size = sizeof(struct c3_isp_stats_buffer);
	vb2_q->dev = isp->dev;
	vb2_q->lock = &stats->lock;
	vb2_q->min_queued_buffers = 2;

	ret = vb2_queue_init(vb2_q);
	if (ret)
		goto err_destroy;

	stats->pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vdev->entity, 1, &stats->pad);
	if (ret)
		goto err_queue_release;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(isp->dev,
			"Failed to register %s: %d\n", vdev->name, ret);
		goto err_entity_cleanup;
	}

	return 0;

err_entity_cleanup:
	media_entity_cleanup(&vdev->entity);
err_queue_release:
	vb2_queue_release(vb2_q);
err_destroy:
	mutex_destroy(&stats->lock);
	return ret;
}

void c3_isp_stats_unregister(struct c3_isp_device *isp)
{
	struct c3_isp_stats *stats = &isp->stats;

	vb2_queue_release(&stats->vb2_q);
	media_entity_cleanup(&stats->vdev.entity);
	video_unregister_device(&stats->vdev);
	mutex_destroy(&stats->lock);
}

void c3_isp_stats_isr(struct c3_isp_device *isp)
{
	struct c3_isp_stats *stats = &isp->stats;

	guard(spinlock_irqsave)(&stats->buff_lock);

	if (stats->buff) {
		stats->buff->vb.sequence = stats->isp->frm_sequence;
		stats->buff->vb.vb2_buf.timestamp = ktime_get();
		stats->buff->vb.field = V4L2_FIELD_NONE;
		vb2_buffer_done(&stats->buff->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}

	c3_isp_stats_cfg_buff(stats);
}
