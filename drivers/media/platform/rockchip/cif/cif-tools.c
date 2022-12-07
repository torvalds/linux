// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd. */

#include <linux/kfifo.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-dma-sg.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <media/v4l2-event.h>
#include "dev.h"
#include "regs.h"
#include "mipi-csi2.h"
#include <media/v4l2-fwnode.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>

#define MEMORY_ALIGN_ROUND_UP_HEIGHT		16

#define TOOLS_MIN_WIDTH		4
#define TOOLS_MIN_HEIGHT	4
#define TOOLS_OUTPUT_STEP_WISE	1
#define CIF_TOOLS_REQ_BUFS_MIN	1

static const struct cif_output_fmt tools_out_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_SRGGB8,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 8 },
		.raw_bpp = 8,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG8,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 8 },
		.raw_bpp = 8,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 8 },
		.raw_bpp = 8,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR8,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 8 },
		.raw_bpp = 8,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB10,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 10,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW10,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG10,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 10,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW10,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG10,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 10,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW10,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR10,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 10,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW10,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB12,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 12,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW12,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG12,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 12,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW12,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG12,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 12,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW12,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR12,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 12,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW12,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}
};

static int rkcif_tools_enum_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_fmtdesc *f)
{
	const struct cif_output_fmt *fmt = NULL;

	if (f->index >= ARRAY_SIZE(tools_out_fmts))
		return -EINVAL;
	fmt = &tools_out_fmts[f->index];
	f->pixelformat = fmt->fourcc;
	return 0;
}

static int rkcif_tools_g_fmt_vid_cap_mplane(struct file *file, void *priv,
					    struct v4l2_format *f)
{
	struct rkcif_tools_vdev *tools_vdev = video_drvdata(file);

	f->fmt.pix_mp = tools_vdev->pixm;
	return 0;
}

static const struct
cif_output_fmt *rkcif_tools_find_output_fmt(u32 pixelfmt)
{
	const struct cif_output_fmt *fmt;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(tools_out_fmts); i++) {
		fmt = &tools_out_fmts[i];
		if (fmt->fourcc == pixelfmt)
			return fmt;
	}

	return NULL;
}

static int rkcif_tools_set_fmt(struct rkcif_tools_vdev *tools_vdev,
			       struct v4l2_pix_format_mplane *pixm,
			       bool try)
{
	struct rkcif_stream *stream = tools_vdev->stream;

	*pixm = stream->pixm;

	if (!try) {
		tools_vdev->tools_out_fmt = stream->cif_fmt_out;
		tools_vdev->pixm = *pixm;

		v4l2_dbg(3, rkcif_debug, &stream->cifdev->v4l2_dev,
			 "%s: req(%d, %d)\n", __func__,
			 pixm->width, pixm->height);
	}
	return 0;
}

static int rkcif_tools_s_fmt_vid_cap_mplane(struct file *file,
					    void *priv, struct v4l2_format *f)
{
	struct rkcif_tools_vdev *tools_vdev = video_drvdata(file);
	int ret = 0;

	if (vb2_is_busy(&tools_vdev->vnode.buf_queue)) {
		v4l2_err(&tools_vdev->cifdev->v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	ret = rkcif_tools_set_fmt(tools_vdev, &f->fmt.pix_mp, false);

	return ret;
}

static int rkcif_tools_querycap(struct file *file,
				void *priv, struct v4l2_capability *cap)
{
	struct rkcif_tools_vdev *tools_vdev = video_drvdata(file);
	struct device *dev = tools_vdev->cifdev->dev;

	strscpy(cap->driver, dev->driver->name, sizeof(cap->driver));
	strscpy(cap->card, dev->driver->name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", dev_name(dev));
	return 0;
}

static long rkcif_tools_ioctl_default(struct file *file, void *fh,
				    bool valid_prio, unsigned int cmd, void *arg)
{
	return 0;
}

static int rkcif_tools_enum_input(struct file *file, void *priv,
				  struct v4l2_input *input)
{

	if (input->index > 0)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(input->name, "Camera", sizeof(input->name));

	return 0;
}

static int rkcif_tools_try_fmt_vid_cap_mplane(struct file *file, void *fh,
					      struct v4l2_format *f)
{
	struct rkcif_tools_vdev *tools_vdev = video_drvdata(file);
	int ret = 0;

	ret = rkcif_tools_set_fmt(tools_vdev, &f->fmt.pix_mp, true);

	return ret;
}

static int rkcif_tools_enum_frameintervals(struct file *file, void *fh,
					   struct v4l2_frmivalenum *fival)
{
	struct rkcif_tools_vdev *tools_vdev = video_drvdata(file);
	struct rkcif_device *dev = tools_vdev->cifdev;
	struct rkcif_sensor_info *sensor = &dev->terminal_sensor;
	struct v4l2_subdev_frame_interval fi;
	int ret;

	if (fival->index != 0)
		return -EINVAL;

	if (!sensor || !sensor->sd) {
		/* TODO: active_sensor is NULL if using DMARX path */
		v4l2_err(&dev->v4l2_dev, "%s Not active sensor\n", __func__);
		return -ENODEV;
	}

	ret = v4l2_subdev_call(sensor->sd, video, g_frame_interval, &fi);
	if (ret && ret != -ENOIOCTLCMD) {
		return ret;
	} else if (ret == -ENOIOCTLCMD) {
		/* Set a default value for sensors not implements ioctl */
		fi.interval.numerator = 1;
		fi.interval.denominator = 30;
	}

	fival->type = V4L2_FRMIVAL_TYPE_CONTINUOUS;
	fival->stepwise.step.numerator = 1;
	fival->stepwise.step.denominator = 1;
	fival->stepwise.max.numerator = 1;
	fival->stepwise.max.denominator = 1;
	fival->stepwise.min.numerator = fi.interval.numerator;
	fival->stepwise.min.denominator = fi.interval.denominator;

	return 0;
}

static int rkcif_tools_enum_framesizes(struct file *file, void *prov,
				       struct v4l2_frmsizeenum *fsize)
{
	struct v4l2_frmsize_discrete *s = &fsize->discrete;
	struct rkcif_tools_vdev *tools_vdev = video_drvdata(file);
	struct rkcif_device *dev = tools_vdev->cifdev;
	struct v4l2_rect input_rect;
	struct rkcif_sensor_info *terminal_sensor = &dev->terminal_sensor;
	struct csi_channel_info csi_info;

	if (fsize->index >= ARRAY_SIZE(tools_out_fmts))
		return -EINVAL;

	if (!rkcif_tools_find_output_fmt(fsize->pixel_format))
		return -EINVAL;

	input_rect.width = RKCIF_DEFAULT_WIDTH;
	input_rect.height = RKCIF_DEFAULT_HEIGHT;

	if (terminal_sensor && terminal_sensor->sd)
		get_input_fmt(terminal_sensor->sd,
			      &input_rect, 0, &csi_info);

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	s->width = input_rect.width;
	s->height = input_rect.height;

	return 0;
}

/* ISP video device IOCTLs */
static const struct v4l2_ioctl_ops rkcif_tools_ioctl = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_enum_input = rkcif_tools_enum_input,
	.vidioc_enum_fmt_vid_cap = rkcif_tools_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap_mplane = rkcif_tools_g_fmt_vid_cap_mplane,
	.vidioc_s_fmt_vid_cap_mplane = rkcif_tools_s_fmt_vid_cap_mplane,
	.vidioc_try_fmt_vid_cap_mplane = rkcif_tools_try_fmt_vid_cap_mplane,
	.vidioc_querycap = rkcif_tools_querycap,
	.vidioc_enum_frameintervals = rkcif_tools_enum_frameintervals,
	.vidioc_enum_framesizes = rkcif_tools_enum_framesizes,
	.vidioc_default = rkcif_tools_ioctl_default,
};

static int rkcif_tools_fh_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct rkcif_vdev_node *vnode = vdev_to_node(vdev);
	struct rkcif_tools_vdev *tools_vdev = to_rkcif_tools_vdev(vnode);
	struct rkcif_device *cifdev = tools_vdev->cifdev;
	int ret;

	ret = rkcif_update_sensor_info(tools_vdev->stream);
	if (ret < 0) {
		v4l2_err(vdev,
			 "update sensor info failed %d\n",
			 ret);

		return ret;
	}

	ret = pm_runtime_resume_and_get(cifdev->dev);
	if (ret < 0)
		v4l2_err(&cifdev->v4l2_dev, "Failed to get runtime pm, %d\n",
			 ret);
	ret = v4l2_fh_open(file);
	if (!ret) {
		ret = v4l2_pipeline_pm_get(&vnode->vdev.entity);
		if (ret < 0)
			vb2_fop_release(file);
	}
	return ret;
}

static int rkcif_tools_fop_release(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct rkcif_vdev_node *vnode = vdev_to_node(vdev);
	struct rkcif_tools_vdev *tools_vdev = to_rkcif_tools_vdev(vnode);
	struct rkcif_device *cifdev = tools_vdev->cifdev;
	int ret = 0;

	ret = vb2_fop_release(file);
	if (!ret)
		v4l2_pipeline_pm_put(&vnode->vdev.entity);
	pm_runtime_put_sync(cifdev->dev);
	return 0;
}

struct v4l2_file_operations rkcif_tools_fops = {
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.open = rkcif_tools_fh_open,
	.release = rkcif_tools_fop_release
};

static int rkcif_tools_vb2_queue_setup(struct vb2_queue *queue,
				       unsigned int *num_buffers,
				       unsigned int *num_planes,
				       unsigned int sizes[],
				       struct device *alloc_ctxs[])
{
	struct rkcif_tools_vdev *tools_vdev = queue->drv_priv;
	struct rkcif_device *cif_dev = tools_vdev->cifdev;
	const struct v4l2_pix_format_mplane *pixm = NULL;
	const struct cif_output_fmt *cif_fmt;
	u32 i;
	const struct v4l2_plane_pix_format *plane_fmt;

	pixm = &tools_vdev->pixm;
	cif_fmt = tools_vdev->tools_out_fmt;
	*num_planes = cif_fmt->mplanes;

	for (i = 0; i < cif_fmt->mplanes; i++) {
		plane_fmt = &pixm->plane_fmt[i];
		sizes[i] = plane_fmt->sizeimage;
	}

	v4l2_dbg(1, rkcif_debug, &cif_dev->v4l2_dev, "%s count %d, size %d\n",
		 v4l2_type_names[queue->type], *num_buffers, sizes[0]);
	return 0;

}

static void rkcif_tools_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkcif_buffer *cifbuf = to_rkcif_buffer(vbuf);
	struct vb2_queue *queue = vb->vb2_queue;
	struct rkcif_tools_vdev *tools_vdev = queue->drv_priv;
	struct v4l2_pix_format_mplane *pixm = &tools_vdev->pixm;
	const struct cif_output_fmt *fmt = tools_vdev->tools_out_fmt;
	struct rkcif_hw *hw_dev = tools_vdev->cifdev->hw_dev;
	unsigned long lock_flags = 0;
	int i;

	memset(cifbuf->buff_addr, 0, sizeof(cifbuf->buff_addr));
	/* If mplanes > 1, every c-plane has its own m-plane,
	 * otherwise, multiple c-planes are in the same m-plane
	 */
	for (i = 0; i < fmt->mplanes; i++) {
		void *addr = vb2_plane_vaddr(vb, i);

		if (hw_dev->is_dma_sg_ops) {
			struct sg_table *sgt = vb2_dma_sg_plane_desc(vb, i);

			cifbuf->buff_addr[i] = sg_dma_address(sgt->sgl);
		} else {
			cifbuf->buff_addr[i] = vb2_dma_contig_plane_dma_addr(vb, i);
		}
		if (rkcif_debug && addr && !hw_dev->iommu_en) {
			memset(addr, 0, pixm->plane_fmt[i].sizeimage);
			v4l2_dbg(1, rkcif_debug, &tools_vdev->cifdev->v4l2_dev,
				 "Clear buffer, size: 0x%08x\n",
				 pixm->plane_fmt[i].sizeimage);
		}
	}

	if (fmt->mplanes == 1) {
		for (i = 0; i < fmt->cplanes - 1; i++)
			cifbuf->buff_addr[i + 1] = cifbuf->buff_addr[i] +
				pixm->plane_fmt[i].bytesperline * pixm->height;
	}
	spin_lock_irqsave(&tools_vdev->vbq_lock, lock_flags);
	list_add_tail(&cifbuf->queue, &tools_vdev->buf_head);
	spin_unlock_irqrestore(&tools_vdev->vbq_lock, lock_flags);
}

static int rkcif_tools_stop(struct rkcif_tools_vdev *tools_vdev)
{
	unsigned long flags;

	spin_lock_irqsave(&tools_vdev->vbq_lock, flags);
	tools_vdev->state = RKCIF_STATE_READY;
	spin_unlock_irqrestore(&tools_vdev->vbq_lock, flags);
	tools_vdev->frame_idx = 0;
	return 0;
}

static void rkcif_tools_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct rkcif_tools_vdev *tools_vdev = vq->drv_priv;
	struct rkcif_device *dev = tools_vdev->cifdev;
	struct rkcif_buffer *buf = NULL;
	struct rkcif_tools_buffer *tools_buf;
	int ret = 0;

	mutex_lock(&dev->tools_lock);

	tools_vdev->stopping = true;
	ret = wait_event_timeout(tools_vdev->wq_stopped,
				 tools_vdev->state != RKCIF_STATE_STREAMING,
				 msecs_to_jiffies(1000));
	if (!ret) {
		rkcif_tools_stop(tools_vdev);
		tools_vdev->stopping = false;
	}
	/* release buffers */
	if (tools_vdev->curr_buf)
		list_add_tail(&tools_vdev->curr_buf->queue, &tools_vdev->buf_head);

	tools_vdev->curr_buf = NULL;
	while (!list_empty(&tools_vdev->buf_head)) {
		buf = list_first_entry(&tools_vdev->buf_head,
				       struct rkcif_buffer, queue);
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	while (!list_empty(&tools_vdev->src_buf_head)) {
		tools_buf = list_first_entry(&tools_vdev->src_buf_head,
				       struct rkcif_tools_buffer, list);
		list_del(&tools_buf->list);
		kfree(tools_buf);
		tools_buf = NULL;
	}
	mutex_unlock(&dev->tools_lock);
}

static int rkcif_tools_start(struct rkcif_tools_vdev *tools_vdev)
{
	int ret = 0;
	struct rkcif_device *dev = tools_vdev->cifdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;

	mutex_lock(&dev->tools_lock);
	if (tools_vdev->state == RKCIF_STATE_STREAMING) {
		ret = -EBUSY;
		v4l2_err(v4l2_dev, "stream in busy state\n");
		goto destroy_buf;
	}

	tools_vdev->frame_idx = 0;
	tools_vdev->state = RKCIF_STATE_STREAMING;
	mutex_unlock(&dev->tools_lock);
	return 0;

destroy_buf:
	if (tools_vdev->curr_buf) {
		vb2_buffer_done(&tools_vdev->curr_buf->vb.vb2_buf,
				VB2_BUF_STATE_QUEUED);
		tools_vdev->curr_buf = NULL;
	}
	while (!list_empty(&tools_vdev->buf_head)) {
		struct rkcif_buffer *buf;

		buf = list_first_entry(&tools_vdev->buf_head,
				       struct rkcif_buffer, queue);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_QUEUED);
		list_del(&buf->queue);
	}
	mutex_unlock(&dev->tools_lock);
	return ret;
}

static int
rkcif_tools_vb2_start_streaming(struct vb2_queue *queue,
				unsigned int count)
{
	struct rkcif_tools_vdev *tools_vdev = queue->drv_priv;
	int ret = 0;

	ret = rkcif_tools_start(tools_vdev);
	if (ret)
		return -EINVAL;
	return 0;
}

static const struct vb2_ops rkcif_tools_vb2_ops = {
	.queue_setup = rkcif_tools_vb2_queue_setup,
	.buf_queue = rkcif_tools_vb2_buf_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.stop_streaming = rkcif_tools_vb2_stop_streaming,
	.start_streaming = rkcif_tools_vb2_start_streaming,
};

static int rkcif_tools_init_vb2_queue(struct vb2_queue *q,
				      struct rkcif_tools_vdev *tools_vdev,
				      enum v4l2_buf_type buf_type)
{
	struct rkcif_hw *hw_dev = tools_vdev->cifdev->hw_dev;

	q->type = buf_type;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->drv_priv = tools_vdev;
	q->ops = &rkcif_tools_vb2_ops;
	q->mem_ops = hw_dev->mem_ops;
	q->buf_struct_size = sizeof(struct rkcif_buffer);
	q->min_buffers_needed = CIF_TOOLS_REQ_BUFS_MIN;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &tools_vdev->vnode.vlock;
	q->dev = hw_dev->dev;
	q->allow_cache_hints = 1;
	q->bidirectional = 1;
	q->gfp_flags = GFP_DMA32;
	if (hw_dev->is_dma_contig)
		q->dma_attrs = DMA_ATTR_FORCE_CONTIGUOUS;
	return vb2_queue_init(q);
}

static void rkcif_tools_work(struct work_struct *work)
{
	struct rkcif_tools_work_struct *tools_work = container_of(work,
							    struct rkcif_tools_work_struct,
							    work);
	struct rkcif_tools_vdev *tools_vdev = container_of(tools_work,
						struct rkcif_tools_vdev,
						tools_work);
	struct rkcif_stream *stream = tools_vdev->stream;
	struct rkcif_tools_buffer *tools_buf;
	const struct cif_output_fmt *fmt = tools_vdev->tools_out_fmt;
	int i = 0;
	int wait_cnt = 0;
	bool is_find_tools_buf = false;

	if (!list_empty(&tools_vdev->src_buf_head)) {
		list_for_each_entry(tools_buf, &tools_vdev->src_buf_head, list) {
			if (tools_buf->vb == &tools_work->active_buf->vb) {
				is_find_tools_buf = true;
				break;
			}
		}
	}
	if (!is_find_tools_buf) {
		tools_buf = kzalloc(sizeof(struct rkcif_tools_buffer), GFP_KERNEL);
		tools_buf->vb = &tools_work->active_buf->vb;
		list_add_tail(&tools_buf->list, &tools_vdev->src_buf_head);
	}
	tools_buf->use_cnt = 2;
	rkcif_vb_done_oneframe(stream, &tools_work->active_buf->vb);

	if (tools_vdev->stopping) {
		rkcif_buf_queue(&tools_work->active_buf->vb.vb2_buf);
		while (tools_buf->use_cnt && wait_cnt < 20) {
			usleep_range(5000, 6000);
			wait_cnt++;
		}
		rkcif_tools_stop(tools_vdev);
		tools_vdev->stopping = false;
		wake_up(&tools_vdev->wq_stopped);
		return;
	}

	if (!list_empty(&tools_vdev->buf_head)) {
		tools_vdev->curr_buf = list_first_entry(&tools_vdev->buf_head,
						    struct rkcif_buffer, queue);
		if (!tools_vdev->curr_buf || tools_vdev->state != RKCIF_STATE_STREAMING) {
			rkcif_buf_queue(&tools_work->active_buf->vb.vb2_buf);
			return;
		}
		list_del(&tools_vdev->curr_buf->queue);

		/* Dequeue a filled buffer */
		for (i = 0; i < fmt->mplanes; i++) {
			u32 payload_size = tools_vdev->pixm.plane_fmt[i].sizeimage;
			void *src = vb2_plane_vaddr(&tools_work->active_buf->vb.vb2_buf, i);
			void *dst = vb2_plane_vaddr(&tools_vdev->curr_buf->vb.vb2_buf, i);

			if (!src || !dst)
				break;
			vb2_set_plane_payload(&tools_vdev->curr_buf->vb.vb2_buf, i,
					      payload_size);
			memcpy(dst, src, payload_size);
		}
		rkcif_buf_queue(&tools_work->active_buf->vb.vb2_buf);
		tools_vdev->curr_buf->vb.sequence = tools_work->frame_idx;
		tools_vdev->curr_buf->vb.vb2_buf.timestamp = tools_work->timestamp;
		vb2_buffer_done(&tools_vdev->curr_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		tools_vdev->curr_buf = NULL;
	} else {
		rkcif_buf_queue(&tools_work->active_buf->vb.vb2_buf);
	}

}

void rkcif_init_tools_vdev(struct rkcif_device *cif_dev, u32 ch)
{
	struct rkcif_tools_vdev *tools_vdev = &cif_dev->tools_vdev[ch];
	struct rkcif_stream *stream = &cif_dev->stream[ch];
	struct v4l2_pix_format_mplane pixm;

	memset(tools_vdev, 0, sizeof(*tools_vdev));
	memset(&pixm, 0, sizeof(pixm));
	tools_vdev->cifdev = cif_dev;
	tools_vdev->stream = stream;
	stream->tools_vdev = tools_vdev;
	tools_vdev->ch = ch;
	tools_vdev->frame_idx = 0;
	pixm.pixelformat = V4L2_PIX_FMT_SBGGR10;
	pixm.width = RKCIF_DEFAULT_WIDTH;
	pixm.height = RKCIF_DEFAULT_HEIGHT;
	tools_vdev->state = RKCIF_STATE_READY;
	INIT_LIST_HEAD(&tools_vdev->buf_head);
	INIT_LIST_HEAD(&tools_vdev->src_buf_head);
	spin_lock_init(&tools_vdev->vbq_lock);
	rkcif_tools_set_fmt(tools_vdev, &pixm, false);
	init_waitqueue_head(&tools_vdev->wq_stopped);
	INIT_WORK(&tools_vdev->tools_work.work, rkcif_tools_work);
}

static int rkcif_register_tools_vdev(struct rkcif_tools_vdev *tools_vdev, bool is_multi_input)
{
	int ret = 0;
	struct video_device *vdev = &tools_vdev->vnode.vdev;
	struct rkcif_vdev_node *node;
	char *vdev_name;

	switch (tools_vdev->ch) {
	case RKCIF_TOOLS_CH0:
		vdev_name = CIF_TOOLS_CH0_VDEV_NAME;
		break;
	case RKCIF_TOOLS_CH1:
		vdev_name = CIF_TOOLS_CH1_VDEV_NAME;
		break;
	case RKCIF_TOOLS_CH2:
		vdev_name = CIF_TOOLS_CH2_VDEV_NAME;
		break;
	default:
		ret = -EINVAL;
		v4l2_err(&tools_vdev->cifdev->v4l2_dev, "Invalid stream\n");
		goto err_cleanup_media_entity;
	}

	strscpy(vdev->name, vdev_name, sizeof(vdev->name));
	node = container_of(vdev, struct rkcif_vdev_node, vdev);
	mutex_init(&node->vlock);

	vdev->ioctl_ops = &rkcif_tools_ioctl;
	vdev->fops = &rkcif_tools_fops;
	vdev->release = video_device_release_empty;
	vdev->lock = &node->vlock;
	vdev->v4l2_dev = &tools_vdev->cifdev->v4l2_dev;
	vdev->queue = &node->buf_queue;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE |
			    V4L2_CAP_STREAMING;
	vdev->vfl_dir =  VFL_DIR_RX;
	node->pad.flags = MEDIA_PAD_FL_SINK;
	video_set_drvdata(vdev, tools_vdev);

	rkcif_tools_init_vb2_queue(&node->buf_queue,
				   tools_vdev,
				   V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	vdev->queue = &node->buf_queue;

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

static void rkcif_unregister_tools_vdev(struct rkcif_tools_vdev *tools_vdev)
{
	struct rkcif_vdev_node *node = &tools_vdev->vnode;
	struct video_device *vdev = &node->vdev;

	video_unregister_device(vdev);
	media_entity_cleanup(&vdev->entity);
	vb2_queue_release(vdev->queue);
}

int rkcif_register_tools_vdevs(struct rkcif_device *cif_dev,
			       int stream_num,
			       bool is_multi_input)
{
	struct rkcif_tools_vdev *tools_vdev;
	int i, j, ret;

	for (i = 0; i < stream_num; i++) {
		tools_vdev = &cif_dev->tools_vdev[i];
		ret = rkcif_register_tools_vdev(tools_vdev, is_multi_input);
		if (ret < 0)
			goto err;
	}

	return 0;
err:
	for (j = 0; j < i; j++) {
		tools_vdev = &cif_dev->tools_vdev[j];
		rkcif_unregister_tools_vdev(tools_vdev);
	}

	return ret;
}

void rkcif_unregister_tools_vdevs(struct rkcif_device *cif_dev,
				  int stream_num)
{
	struct rkcif_tools_vdev *tools_vdev;
	int i;

	for (i = 0; i < stream_num; i++) {
		tools_vdev = &cif_dev->tools_vdev[i];
		rkcif_unregister_tools_vdev(tools_vdev);
	}
}
