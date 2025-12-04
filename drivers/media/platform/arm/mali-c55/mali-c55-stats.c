// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Mali-C55 ISP Driver - 3A Statistics capture device
 *
 * Copyright (C) 2025 Ideas on Board Oy
 */

#include <linux/container_of.h>
#include <linux/dev_printk.h>
#include <linux/list.h>
#include <linux/media/arm/mali-c55-config.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include <media/media-entity.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "mali-c55-common.h"
#include "mali-c55-registers.h"

static const unsigned int metering_space_addrs[] = {
	[MALI_C55_CONFIG_PING] = 0x095ac,
	[MALI_C55_CONFIG_PONG] = 0x2156c,
};

static int mali_c55_stats_enum_fmt_meta_cap(struct file *file, void *fh,
					    struct v4l2_fmtdesc *f)
{
	if (f->index)
		return -EINVAL;

	f->pixelformat = V4L2_META_FMT_MALI_C55_STATS;

	return 0;
}

static int mali_c55_stats_g_fmt_meta_cap(struct file *file, void *fh,
					 struct v4l2_format *f)
{
	static const struct v4l2_meta_format mfmt = {
		.dataformat = V4L2_META_FMT_MALI_C55_STATS,
		.buffersize = sizeof(struct mali_c55_stats_buffer)
	};

	f->fmt.meta = mfmt;

	return 0;
}

static int mali_c55_stats_querycap(struct file *file,
				   void *priv, struct v4l2_capability *cap)
{
	strscpy(cap->driver, MALI_C55_DRIVER_NAME, sizeof(cap->driver));
	strscpy(cap->card, "ARM Mali-C55 ISP", sizeof(cap->card));

	return 0;
}

static const struct v4l2_ioctl_ops mali_c55_stats_v4l2_ioctl_ops = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_enum_fmt_meta_cap = mali_c55_stats_enum_fmt_meta_cap,
	.vidioc_g_fmt_meta_cap = mali_c55_stats_g_fmt_meta_cap,
	.vidioc_s_fmt_meta_cap = mali_c55_stats_g_fmt_meta_cap,
	.vidioc_try_fmt_meta_cap = mali_c55_stats_g_fmt_meta_cap,
	.vidioc_querycap = mali_c55_stats_querycap,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct v4l2_file_operations mali_c55_stats_v4l2_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
};

static int
mali_c55_stats_queue_setup(struct vb2_queue *q, unsigned int *num_buffers,
			   unsigned int *num_planes, unsigned int sizes[],
			   struct device *alloc_devs[])
{
	if (*num_planes && *num_planes > 1)
		return -EINVAL;

	if (sizes[0] && sizes[0] < sizeof(struct mali_c55_stats_buffer))
		return -EINVAL;

	*num_planes = 1;

	if (!sizes[0])
		sizes[0] = sizeof(struct mali_c55_stats_buffer);

	return 0;
}

static void mali_c55_stats_buf_queue(struct vb2_buffer *vb)
{
	struct mali_c55_stats *stats = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct mali_c55_stats_buf *buf = container_of(vbuf,
						struct mali_c55_stats_buf, vb);

	vb2_set_plane_payload(vb, 0, sizeof(struct mali_c55_stats_buffer));
	buf->segments_remaining = 2;
	buf->failed = false;

	spin_lock(&stats->buffers.lock);
	list_add_tail(&buf->queue, &stats->buffers.queue);
	spin_unlock(&stats->buffers.lock);
}

static void mali_c55_stats_return_buffers(struct mali_c55_stats *stats,
					  enum vb2_buffer_state state)
{
	struct mali_c55_stats_buf *buf, *tmp;

	guard(spinlock)(&stats->buffers.lock);

	list_for_each_entry_safe(buf, tmp, &stats->buffers.queue, queue) {
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
}

static int mali_c55_stats_start_streaming(struct vb2_queue *q,
					  unsigned int count)
{
	struct mali_c55_stats *stats = vb2_get_drv_priv(q);
	struct mali_c55 *mali_c55 = stats->mali_c55;
	int ret;

	ret = pm_runtime_resume_and_get(mali_c55->dev);
	if (ret)
		goto err_return_buffers;

	ret = video_device_pipeline_alloc_start(&stats->vdev);
	if (ret)
		goto err_pm_put;

	if (mali_c55_pipeline_ready(mali_c55)) {
		ret = v4l2_subdev_enable_streams(&mali_c55->isp.sd,
						 MALI_C55_ISP_PAD_SOURCE_VIDEO,
						 BIT(0));
		if (ret < 0)
			goto err_stop_pipeline;
	}

	return 0;

err_stop_pipeline:
	video_device_pipeline_stop(&stats->vdev);
err_pm_put:
	pm_runtime_put_autosuspend(mali_c55->dev);
err_return_buffers:
	mali_c55_stats_return_buffers(stats, VB2_BUF_STATE_QUEUED);

	return ret;
}

static void mali_c55_stats_stop_streaming(struct vb2_queue *q)
{
	struct mali_c55_stats *stats = vb2_get_drv_priv(q);
	struct mali_c55 *mali_c55 = stats->mali_c55;
	struct mali_c55_isp *isp = &mali_c55->isp;

	if (mali_c55_pipeline_ready(mali_c55)) {
		if (v4l2_subdev_is_streaming(&isp->sd))
			v4l2_subdev_disable_streams(&isp->sd,
						MALI_C55_ISP_PAD_SOURCE_VIDEO,
						BIT(0));
	}

	video_device_pipeline_stop(&stats->vdev);
	mali_c55_stats_return_buffers(stats, VB2_BUF_STATE_ERROR);

	pm_runtime_put_autosuspend(stats->mali_c55->dev);
}

static const struct vb2_ops mali_c55_stats_vb2_ops = {
	.queue_setup = mali_c55_stats_queue_setup,
	.buf_queue = mali_c55_stats_buf_queue,
	.start_streaming = mali_c55_stats_start_streaming,
	.stop_streaming = mali_c55_stats_stop_streaming,
};

static void mali_c55_stats_cpu_read(struct mali_c55_stats *stats,
				    struct mali_c55_stats_buf *buf,
				    enum mali_c55_config_spaces cfg_space)
{
	struct mali_c55 *mali_c55 = stats->mali_c55;
	const void __iomem *src;
	size_t length;
	void *dst;

	src = mali_c55->base + MALI_C55_REG_1024BIN_HIST;
	dst = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);
	memcpy_fromio(dst, src, MALI_C55_1024BIN_HIST_SIZE);

	src = mali_c55->base + metering_space_addrs[cfg_space];
	dst += MALI_C55_1024BIN_HIST_SIZE;
	length = sizeof(struct mali_c55_stats_buffer) - MALI_C55_1024BIN_HIST_SIZE;
	memcpy_fromio(dst, src, length);
}

void mali_c55_stats_fill_buffer(struct mali_c55 *mali_c55,
				enum mali_c55_config_spaces cfg_space)
{
	struct mali_c55_stats *stats = &mali_c55->stats;
	struct mali_c55_stats_buf *buf = NULL;

	spin_lock(&stats->buffers.lock);
	if (!list_empty(&stats->buffers.queue)) {
		buf = list_first_entry(&stats->buffers.queue,
				       struct mali_c55_stats_buf, queue);
		list_del(&buf->queue);
	}
	spin_unlock(&stats->buffers.lock);

	if (!buf)
		return;

	buf->vb.sequence = mali_c55->isp.frame_sequence;
	buf->vb.vb2_buf.timestamp = ktime_get_boottime_ns();

	mali_c55_stats_cpu_read(stats, buf, cfg_space);
	vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
}

void mali_c55_unregister_stats(struct mali_c55 *mali_c55)
{
	struct mali_c55_stats *stats = &mali_c55->stats;

	if (!video_is_registered(&stats->vdev))
		return;

	vb2_video_unregister_device(&stats->vdev);
	media_entity_cleanup(&stats->vdev.entity);

	mutex_destroy(&stats->lock);
}

int mali_c55_register_stats(struct mali_c55 *mali_c55)
{
	struct mali_c55_stats *stats = &mali_c55->stats;
	struct video_device *vdev = &stats->vdev;
	struct vb2_queue *vb2q = &stats->queue;
	int ret;

	mutex_init(&stats->lock);
	INIT_LIST_HEAD(&stats->buffers.queue);
	spin_lock_init(&stats->buffers.lock);

	stats->pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&stats->vdev.entity, 1, &stats->pad);
	if (ret)
		goto err_destroy_mutex;

	vb2q->type = V4L2_BUF_TYPE_META_CAPTURE;
	vb2q->io_modes = VB2_MMAP | VB2_DMABUF;
	vb2q->drv_priv = stats;
	vb2q->mem_ops = &vb2_dma_contig_memops;
	vb2q->ops = &mali_c55_stats_vb2_ops;
	vb2q->buf_struct_size = sizeof(struct mali_c55_stats_buf);
	vb2q->min_queued_buffers = 1;
	vb2q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	vb2q->lock = &stats->lock;
	vb2q->dev = mali_c55->dev;

	ret = vb2_queue_init(vb2q);
	if (ret) {
		dev_err(mali_c55->dev, "stats vb2 queue init failed\n");
		goto err_cleanup_entity;
	}

	strscpy(stats->vdev.name, "mali-c55 3a stats", sizeof(stats->vdev.name));
	vdev->release = video_device_release_empty;
	vdev->fops = &mali_c55_stats_v4l2_fops;
	vdev->ioctl_ops = &mali_c55_stats_v4l2_ioctl_ops;
	vdev->lock = &stats->lock;
	vdev->v4l2_dev = &mali_c55->v4l2_dev;
	vdev->queue = &stats->queue;
	vdev->device_caps = V4L2_CAP_META_CAPTURE | V4L2_CAP_STREAMING;
	vdev->vfl_dir = VFL_DIR_RX;
	video_set_drvdata(vdev, stats);

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(mali_c55->dev,
			"failed to register stats video device\n");
		goto err_release_vb2q;
	}

	stats->mali_c55 = mali_c55;

	return 0;

err_release_vb2q:
	vb2_queue_release(vb2q);
err_cleanup_entity:
	media_entity_cleanup(&stats->vdev.entity);
err_destroy_mutex:

	mutex_destroy(&stats->lock);

	return ret;
}
