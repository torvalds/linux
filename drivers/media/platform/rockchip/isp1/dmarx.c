// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>
#include <linux/dma-iommu.h>
#include "dev.h"
#include "regs.h"

#define CIF_ISP_REQ_BUFS_MIN 1

static const struct capture_fmt dmarx_fmts[] = {
	/* bayer raw */
	{
		.fourcc = V4L2_PIX_FMT_SRGGB8,
		.mbus_code = MEDIA_BUS_FMT_SRGGB8_1X8,
		.fmt_type = FMT_BAYER,
		.bpp = { 8 },
		.mplanes = 1,
		.write_format = CIF_MI_DMA_CTRL_READ_FMT_PACKED,
		.output_format = CIF_MI_DMA_CTRL_RGB_BAYER_8BIT,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG8,
		.mbus_code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.fmt_type = FMT_BAYER,
		.bpp = { 8 },
		.mplanes = 1,
		.write_format = CIF_MI_DMA_CTRL_READ_FMT_PACKED,
		.output_format = CIF_MI_DMA_CTRL_RGB_BAYER_8BIT,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.mbus_code = MEDIA_BUS_FMT_SGBRG8_1X8,
		.fmt_type = FMT_BAYER,
		.bpp = { 8 },
		.mplanes = 1,
		.write_format = CIF_MI_DMA_CTRL_READ_FMT_PACKED,
		.output_format = CIF_MI_DMA_CTRL_RGB_BAYER_8BIT,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR8,
		.mbus_code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.fmt_type = FMT_BAYER,
		.bpp = { 8 },
		.mplanes = 1,
		.write_format = CIF_MI_DMA_CTRL_READ_FMT_PACKED,
		.output_format = CIF_MI_DMA_CTRL_RGB_BAYER_8BIT,
	}, { /* 12bit used, 4 lower bits of LSB unused */
		.fourcc = V4L2_PIX_FMT_SBGGR16,
		.mbus_code = MEDIA_BUS_FMT_SBGGR12_1X12,
		.fmt_type = FMT_BAYER,
		.bpp = { 16 },
		.mplanes = 1,
		.write_format = CIF_MI_DMA_CTRL_READ_FMT_PACKED,
		.output_format = CIF_MI_DMA_CTRL_RGB_BAYER_16BIT,
	}, {
		.fourcc = V4L2_PIX_FMT_YUYV,
		.mbus_code = MEDIA_BUS_FMT_YUYV8_2X8,
		.fmt_type = FMT_YUV,
		.bpp = { 16 },
		.cplanes = 1,
		.mplanes = 1,
		.uv_swap = 0,
		.write_format = CIF_MI_DMA_CTRL_READ_FMT_PACKED,
		.output_format = CIF_MI_DMA_CTRL_FMT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_YVYU,
		.mbus_code = MEDIA_BUS_FMT_YVYU8_2X8,
		.fmt_type = FMT_YUV,
		.bpp = { 16 },
		.cplanes = 1,
		.mplanes = 1,
		.uv_swap = 0,
		.write_format = CIF_MI_DMA_CTRL_READ_FMT_PACKED,
		.output_format = CIF_MI_DMA_CTRL_FMT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_UYVY,
		.mbus_code = MEDIA_BUS_FMT_UYVY8_2X8,
		.fmt_type = FMT_YUV,
		.bpp = { 16 },
		.cplanes = 1,
		.mplanes = 1,
		.uv_swap = 0,
		.write_format = CIF_MI_DMA_CTRL_READ_FMT_PACKED,
		.output_format = CIF_MI_DMA_CTRL_FMT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_VYUY,
		.mbus_code = MEDIA_BUS_FMT_VYUY8_2X8,
		.fmt_type = FMT_YUV,
		.bpp = { 16 },
		.cplanes = 1,
		.mplanes = 1,
		.uv_swap = 0,
		.write_format = CIF_MI_DMA_CTRL_READ_FMT_PACKED,
		.output_format = CIF_MI_DMA_CTRL_FMT_YUV422,
	}
};

static struct stream_config rkisp1_dmarx_stream_config = {
	.fmts = dmarx_fmts,
	.fmt_size = ARRAY_SIZE(dmarx_fmts),
	.mi = {
		.y_size_init = CIF_MI_DMA_Y_PIC_SIZE,
		.y_base_ad_init = CIF_MI_DMA_Y_PIC_START_AD,
		.cb_base_ad_init = CIF_MI_DMA_CB_PIC_START_AD,
		.cr_base_ad_init = CIF_MI_DMA_CR_PIC_START_AD,
	},
};

static const
struct capture_fmt *find_fmt(struct rkisp1_stream *stream,
			     const u32 pixelfmt)
{
	const struct capture_fmt *fmt;
	int i;

	for (i = 0; i < stream->config->fmt_size; i++) {
		fmt = &stream->config->fmts[i];
		if (fmt->fourcc == pixelfmt)
			return fmt;
	}
	return NULL;
}

static int dmarx_config_mi(struct rkisp1_stream *stream)
{
	struct rkisp1_device *dev = stream->ispdev;
	void __iomem *base = dev->base_addr;
	struct capture_fmt *dmarx_in_fmt = &stream->out_isp_fmt;

	v4l2_dbg(1, rkisp1_debug, &dev->v4l2_dev,
		 "%s %dx%x y_stride:%d\n", __func__,
		 stream->out_fmt.width,
		 stream->out_fmt.height,
		 stream->u.dmarx.y_stride);

	mi_set_y_size(stream, stream->out_fmt.width *
		      stream->out_fmt.height);
	dmarx_set_y_width(base, stream->out_fmt.width);
	dmarx_set_y_line_length(base, stream->u.dmarx.y_stride);

	mi_dmarx_ready_enable(stream);
	if (dmarx_in_fmt->uv_swap)
		dmarx_set_uv_swap(base);

	dmarx_ctrl(base,
		dmarx_in_fmt->write_format |
		dmarx_in_fmt->output_format |
		CIF_MI_DMA_CTRL_BURST_LEN_LUM_16 |
		CIF_MI_DMA_CTRL_BURST_LEN_CHROM_16);
	return 0;
}

static void update_dmarx(struct rkisp1_stream *stream)
{
	void __iomem *base = stream->ispdev->base_addr;

	if (stream->curr_buf) {
		mi_set_y_addr(stream,
			stream->curr_buf->buff_addr[RKISP1_PLANE_Y]);
		mi_set_cb_addr(stream,
			stream->curr_buf->buff_addr[RKISP1_PLANE_CB]);
		mi_set_cr_addr(stream,
			stream->curr_buf->buff_addr[RKISP1_PLANE_CR]);
		mi_dmarx_start(base);
		stream->frame_end = false;
	}
}

static void dmarx_stop_mi(struct rkisp1_stream *stream)
{
	mi_dmarx_ready_disable(stream);
}

static struct streams_ops rkisp1_dmarx_streams_ops = {
	.config_mi = dmarx_config_mi,
	.stop_mi = dmarx_stop_mi,
	.update_mi = update_dmarx,
};

static int dmarx_frame_end(struct rkisp1_stream *stream)
{
	unsigned long lock_flags = 0;

	if (stream->curr_buf) {
		vb2_buffer_done(&stream->curr_buf->vb.vb2_buf,
			VB2_BUF_STATE_DONE);
		stream->curr_buf = NULL;
	}

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	if (!list_empty(&stream->buf_queue)) {
		stream->curr_buf =
			list_first_entry(&stream->buf_queue,
					struct rkisp1_buffer,
					queue);
		list_del(&stream->curr_buf->queue);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);

	stream->ops->update_mi(stream);
	return 0;
}

/***************************** vb2 operations*******************************/

static void dmarx_stop(struct rkisp1_stream *stream)
{
	struct rkisp1_device *dev = stream->ispdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	int ret = 0;

	stream->stopping = true;
	if (dev->isp_state == ISP_START &&
		!stream->frame_end) {
		ret = wait_event_timeout(stream->done,
					 !stream->streaming,
					 msecs_to_jiffies(500));
		if (!ret)
			v4l2_warn(v4l2_dev,
				  "dmarx:%d waiting on event return error %d\n",
				  stream->id, ret);
	}
	if (stream->ops->stop_mi)
		stream->ops->stop_mi(stream);
	stream->stopping = false;
	stream->streaming = false;
	stream->frame_end = false;
}

static int dmarx_start(struct rkisp1_stream *stream)
{
	int ret;

	ret = stream->ops->config_mi(stream);
	if (ret)
		return ret;

	stream->curr_buf = NULL;
	dmarx_frame_end(stream);
	stream->streaming = true;
	return 0;
}

static int rkisp1_queue_setup(struct vb2_queue *queue,
			      unsigned int *num_buffers,
			      unsigned int *num_planes,
			      unsigned int sizes[],
			      struct device *alloc_ctxs[])
{
	struct rkisp1_stream *stream = queue->drv_priv;
	struct rkisp1_device *dev = stream->ispdev;
	const struct v4l2_pix_format_mplane *pixm = NULL;
	const struct capture_fmt *isp_fmt = NULL;
	u32 i;

	pixm = &stream->out_fmt;
	isp_fmt = &stream->out_isp_fmt;

	*num_planes = isp_fmt->mplanes;

	for (i = 0; i < isp_fmt->mplanes; i++) {
		const struct v4l2_plane_pix_format *plane_fmt;

		plane_fmt = &pixm->plane_fmt[i];
		sizes[i] = plane_fmt->sizeimage;
	}

	v4l2_dbg(1, rkisp1_debug, &dev->v4l2_dev, "%s count %d, size %d\n",
		 v4l2_type_names[queue->type], *num_buffers, sizes[0]);

	return 0;
}

/*
 * The vb2_buffer are stored in rkisp1_buffer, in order to unify
 * mplane buffer and none-mplane buffer.
 */
static void rkisp1_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkisp1_buffer *ispbuf = to_rkisp1_buffer(vbuf);
	struct vb2_queue *queue = vb->vb2_queue;
	struct rkisp1_stream *stream = queue->drv_priv;
	unsigned long lock_flags = 0;
	struct v4l2_pix_format_mplane *pixm = &stream->out_fmt;
	struct capture_fmt *isp_fmt = &stream->out_isp_fmt;
	int i;

	memset(ispbuf->buff_addr, 0, sizeof(ispbuf->buff_addr));
	for (i = 0; i < isp_fmt->mplanes; i++)
		ispbuf->buff_addr[i] = vb2_dma_contig_plane_dma_addr(vb, i);

	/*
	 * NOTE: plane_fmt[0].sizeimage is total size of all planes for single
	 * memory plane formats, so calculate the size explicitly.
	 */
	if (isp_fmt->mplanes == 1) {
		for (i = 0; i < isp_fmt->cplanes - 1; i++) {
			ispbuf->buff_addr[i + 1] = (i == 0) ?
				ispbuf->buff_addr[i] +
				pixm->plane_fmt[i].bytesperline *
				pixm->height :
				ispbuf->buff_addr[i] +
				pixm->plane_fmt[i].sizeimage;
		}
	}

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	if (stream->streaming &&
	    list_empty(&stream->buf_queue) &&
	    !stream->curr_buf) {
		stream->curr_buf = ispbuf;
		stream->ops->update_mi(stream);
	} else {
		list_add_tail(&ispbuf->queue, &stream->buf_queue);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);
}

static void dmarx_stop_streaming(struct vb2_queue *queue)
{
	struct rkisp1_stream *stream = queue->drv_priv;
	struct rkisp1_buffer *buf;
	unsigned long lock_flags = 0;

	dmarx_stop(stream);

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	if (stream->curr_buf) {
		list_add_tail(&stream->curr_buf->queue, &stream->buf_queue);
		stream->curr_buf = NULL;
	}
	while (!list_empty(&stream->buf_queue)) {
		buf = list_first_entry(&stream->buf_queue,
			struct rkisp1_buffer, queue);
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);
}

static int dmarx_start_streaming(struct vb2_queue *queue,
				 unsigned int count)
{
	struct rkisp1_stream *stream = queue->drv_priv;
	struct rkisp1_device *dev = stream->ispdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	int ret = 0;

	if (atomic_read(&dev->open_cnt) < 2) {
		v4l2_err(v4l2_dev,
			 "other stream should enable first\n");
		return -EINVAL;
	}

	if (WARN_ON(stream->streaming))
		return -EBUSY;

	ret = dmarx_start(stream);
	if (ret < 0)
		v4l2_err(v4l2_dev,
			 "start dmarx stream:%d failed\n",
			 stream->id);

	return ret;
}

static struct vb2_ops dmarx_vb2_ops = {
	.queue_setup = rkisp1_queue_setup,
	.buf_queue = rkisp1_buf_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.stop_streaming = dmarx_stop_streaming,
	.start_streaming = dmarx_start_streaming,
};

static int rkisp_init_vb2_queue(struct vb2_queue *q,
				struct rkisp1_stream *stream,
				enum v4l2_buf_type buf_type)
{
	q->type = buf_type;
	q->io_modes = VB2_MMAP | VB2_DMABUF | VB2_USERPTR;
	q->drv_priv = stream;
	q->ops = &dmarx_vb2_ops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct rkisp1_buffer);
	q->min_buffers_needed = CIF_ISP_REQ_BUFS_MIN;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &stream->ispdev->apilock;

	return vb2_queue_init(q);
}

static int rkisp1_set_fmt(struct rkisp1_stream *stream,
			   struct v4l2_pix_format_mplane *pixm,
			   bool try)
{
	const struct capture_fmt *fmt;
	unsigned int imagsize = 0;
	unsigned int planes;
	u32 xsubs = 1, ysubs = 1;
	unsigned int i;

	fmt = find_fmt(stream, pixm->pixelformat);
	if (!fmt) {
		v4l2_err(&stream->ispdev->v4l2_dev,
			 "nonsupport pixelformat:%c%c%c%c\n",
			 pixm->pixelformat,
			 pixm->pixelformat >> 8,
			 pixm->pixelformat >> 16,
			 pixm->pixelformat >> 24);
		return -EINVAL;
	}

	pixm->num_planes = fmt->mplanes;
	pixm->field = V4L2_FIELD_NONE;
	if (!pixm->quantization)
		pixm->quantization = V4L2_QUANTIZATION_FULL_RANGE;

	/* calculate size */
	fcc_xysubs(fmt->fourcc, &xsubs, &ysubs);
	planes = fmt->cplanes ? fmt->cplanes : fmt->mplanes;
	for (i = 0; i < planes; i++) {
		struct v4l2_plane_pix_format *plane_fmt;
		unsigned int width, height, bytesperline;

		plane_fmt = pixm->plane_fmt + i;

		if (i == 0) {
			width = pixm->width;
			height = pixm->height;
		} else {
			width = pixm->width / xsubs;
			height = pixm->height / ysubs;
		}

		bytesperline = width * DIV_ROUND_UP(fmt->bpp[i], 8);
		/* stride is only available for sp stream and y plane */
		if (i != 0 ||
		    plane_fmt->bytesperline < bytesperline)
			plane_fmt->bytesperline = bytesperline;

		plane_fmt->sizeimage = plane_fmt->bytesperline * height;

		imagsize += plane_fmt->sizeimage;
	}

	/* convert to non-MPLANE format.
	 * it's important since we want to unify none-MPLANE
	 * and MPLANE.
	 */
	if (fmt->mplanes == 1)
		pixm->plane_fmt[0].sizeimage = imagsize;

	if (!try) {
		stream->out_isp_fmt = *fmt;
		stream->out_fmt = *pixm;

		stream->u.dmarx.y_stride =
			pixm->plane_fmt[0].bytesperline /
			DIV_ROUND_UP(fmt->bpp[0], 8);

		v4l2_dbg(1, rkisp1_debug, &stream->ispdev->v4l2_dev,
			 "%s: stream: %d req(%d, %d) out(%d, %d)\n", __func__,
			 stream->id, pixm->width, pixm->height,
			 stream->out_fmt.width, stream->out_fmt.height);
	}

	return 0;
}

/************************* v4l2_file_operations***************************/

static const struct v4l2_file_operations rkisp1_fops = {
	.open = rkisp1_fh_open,
	.release = rkisp1_fop_release,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
};

static int rkisp1_try_fmt_vid_out_mplane(struct file *file, void *fh,
					 struct v4l2_format *f)
{
	struct rkisp1_stream *stream = video_drvdata(file);

	return rkisp1_set_fmt(stream, &f->fmt.pix_mp, true);
}

static int rkisp1_enum_fmt_vid_out_mplane(struct file *file, void *priv,
					  struct v4l2_fmtdesc *f)
{
	struct rkisp1_stream *stream = video_drvdata(file);
	const struct capture_fmt *fmt = NULL;

	if (f->index >= stream->config->fmt_size)
		return -EINVAL;

	fmt = &stream->config->fmts[f->index];
	f->pixelformat = fmt->fourcc;

	return 0;
}

static int rkisp1_s_fmt_vid_out_mplane(struct file *file,
				       void *priv, struct v4l2_format *f)
{
	struct rkisp1_stream *stream = video_drvdata(file);
	struct video_device *vdev = &stream->vnode.vdev;
	struct rkisp1_vdev_node *node = vdev_to_node(vdev);
	struct rkisp1_device *dev = stream->ispdev;

	if (vb2_is_busy(&node->buf_queue)) {
		v4l2_err(&dev->v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	return rkisp1_set_fmt(stream, &f->fmt.pix_mp, false);
}

static int rkisp1_g_fmt_vid_out_mplane(struct file *file, void *fh,
				       struct v4l2_format *f)
{
	struct rkisp1_stream *stream = video_drvdata(file);

	f->fmt.pix_mp = stream->out_fmt;

	return 0;
}

static int rkisp1_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct rkisp1_stream *stream = video_drvdata(file);
	struct device *dev = stream->ispdev->dev;
	struct video_device *vdev = video_devdata(file);

	strlcpy(cap->card, vdev->name, sizeof(cap->card));
	snprintf(cap->driver, sizeof(cap->driver),
		 "%s_v%d", dev->driver->name,
		 stream->ispdev->isp_ver >> 4);
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", dev_name(dev));

	return 0;
}

static const struct v4l2_ioctl_ops rkisp1_dmarx_ioctl = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_try_fmt_vid_out_mplane = rkisp1_try_fmt_vid_out_mplane,
	.vidioc_enum_fmt_vid_out_mplane = rkisp1_enum_fmt_vid_out_mplane,
	.vidioc_s_fmt_vid_out_mplane = rkisp1_s_fmt_vid_out_mplane,
	.vidioc_g_fmt_vid_out_mplane = rkisp1_g_fmt_vid_out_mplane,
	.vidioc_querycap = rkisp1_querycap,
};

static void rkisp1_unregister_dmarx_video(struct rkisp1_stream *stream)
{
	media_entity_cleanup(&stream->vnode.vdev.entity);
	video_unregister_device(&stream->vnode.vdev);
}

static int rkisp1_register_dmarx_video(struct rkisp1_stream *stream)
{
	struct rkisp1_device *dev = stream->ispdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct video_device *vdev = &stream->vnode.vdev;
	struct rkisp1_vdev_node *node;
	int ret = 0;

	node = vdev_to_node(vdev);

	vdev->release = video_device_release_empty;
	vdev->fops = &rkisp1_fops;
	vdev->minor = -1;
	vdev->v4l2_dev = v4l2_dev;
	vdev->lock = &dev->apilock;
	video_set_drvdata(vdev, stream);

	vdev->ioctl_ops = &rkisp1_dmarx_ioctl;
	vdev->device_caps = V4L2_CAP_VIDEO_OUTPUT_MPLANE |
			    V4L2_CAP_STREAMING;
	vdev->vfl_dir = VFL_DIR_TX;
	node->pad.flags = MEDIA_PAD_FL_SOURCE;

	rkisp_init_vb2_queue(&node->buf_queue, stream,
		V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	vdev->queue = &node->buf_queue;

	ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		v4l2_err(v4l2_dev,
			 "video register failed with error %d\n", ret);
		return ret;
	}

	ret = media_entity_pads_init(&vdev->entity, 1, &node->pad);
	if (ret < 0)
		goto unreg;

	return 0;
unreg:
	video_unregister_device(vdev);
	return ret;
}

/****************  Interrupter Handler ****************/

void rkisp1_dmarx_isr(u32 mis_val, struct rkisp1_device *dev)
{
	void __iomem *base = dev->base_addr;
	struct rkisp1_stream *stream;

	if (mis_val & CIF_MI_DMA_READY) {
		stream = &dev->dmarx_dev.stream[RKISP1_STREAM_DMARX];
		stream->frame_end = true;
		writel(CIF_MI_DMA_READY, base + CIF_MI_ICR);

		if (stream->stopping) {
			stream->stopping = false;
			stream->streaming = false;
			wake_up(&stream->done);
		} else {
			dmarx_frame_end(stream);
		}
	}
}

int rkisp1_register_dmarx_vdev(struct rkisp1_device *dev)
{
	struct rkisp1_dmarx_device *dmarx_dev = &dev->dmarx_dev;
	struct rkisp1_stream *stream;
	struct video_device *vdev;
	struct media_entity *source, *sink;
	int ret = 0;

	memset(dmarx_dev, 0, sizeof(*dmarx_dev));
	dmarx_dev->ispdev = dev;

	if (dev->isp_ver <= ISP_V13) {
		stream = &dmarx_dev->stream[RKISP1_STREAM_DMARX];
		INIT_LIST_HEAD(&stream->buf_queue);
		init_waitqueue_head(&stream->done);
		spin_lock_init(&stream->vbq_lock);
		stream->id = RKISP1_STREAM_DMARX;
		stream->ispdev = dev;
		stream->ops = &rkisp1_dmarx_streams_ops;
		stream->config = &rkisp1_dmarx_stream_config;
		vdev = &stream->vnode.vdev;
		strlcpy(vdev->name, DMA_VDEV_NAME, sizeof(vdev->name));
		ret = rkisp1_register_dmarx_video(stream);
		if (ret < 0)
			return ret;

		/* dmarx links -> isp subdev */
		source = &vdev->entity;
		sink = &dev->isp_sdev.sd.entity;
		ret = media_create_pad_link(source, 0,
			sink, RKISP1_ISP_PAD_SINK, 0);
	}

	return ret;
}

void rkisp1_unregister_dmarx_vdev(struct rkisp1_device *dev)
{
	struct rkisp1_dmarx_device *dmarx_dev = &dev->dmarx_dev;
	struct rkisp1_stream *stream;

	if (dev->isp_ver <= ISP_V13) {
		stream = &dmarx_dev->stream[RKISP1_STREAM_DMARX];
		rkisp1_unregister_dmarx_video(stream);
	}
}
