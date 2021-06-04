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
#include <media/videobuf2-dma-sg.h>
#include <linux/dma-iommu.h>
#include "dev.h"
#include "regs.h"

#define CIF_ISP_REQ_BUFS_MIN 0

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

static const struct capture_fmt rawrd_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_SRGGB8,
		.fmt_type = FMT_BAYER,
		.bpp = { 8 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG8,
		.fmt_type = FMT_BAYER,
		.bpp = { 8 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.fmt_type = FMT_BAYER,
		.bpp = { 8 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR8,
		.fmt_type = FMT_BAYER,
		.bpp = { 8 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_GREY,
		.fmt_type = FMT_BAYER,
		.bpp = { 8 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB10,
		.fmt_type = FMT_BAYER,
		.bpp = { 10 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG10,
		.fmt_type = FMT_BAYER,
		.bpp = { 10 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG10,
		.fmt_type = FMT_BAYER,
		.bpp = { 10 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR10,
		.fmt_type = FMT_BAYER,
		.bpp = { 10 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_Y10,
		.fmt_type = FMT_BAYER,
		.bpp = { 10 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB12,
		.fmt_type = FMT_BAYER,
		.bpp = { 12 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG12,
		.fmt_type = FMT_BAYER,
		.bpp = { 12 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG12,
		.fmt_type = FMT_BAYER,
		.bpp = { 12 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR12,
		.fmt_type = FMT_BAYER,
		.bpp = { 12 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_Y12,
		.fmt_type = FMT_BAYER,
		.bpp = { 12 },
		.mplanes = 1,
	}
};

static struct stream_config rkisp_dmarx_stream_config = {
	.fmts = dmarx_fmts,
	.fmt_size = ARRAY_SIZE(dmarx_fmts),
	.mi = {
		.y_size_init = CIF_MI_DMA_Y_PIC_SIZE,
		.y_base_ad_init = CIF_MI_DMA_Y_PIC_START_AD,
		.cb_base_ad_init = CIF_MI_DMA_CB_PIC_START_AD,
		.cr_base_ad_init = CIF_MI_DMA_CR_PIC_START_AD,
	},
};

static struct stream_config rkisp2_dmarx0_stream_config = {
	.fmts = rawrd_fmts,
	.fmt_size = ARRAY_SIZE(rawrd_fmts),
	.frame_end_id = RAW0_RD_FRAME,
	.mi = {
		.y_base_ad_init = MI_RAW0_RD_BASE,
		.y_base_ad_shd = MI_RAW0_RD_BASE_SHD,
		.length = MI_RAW0_RD_LENGTH,
	},
};

static struct stream_config rkisp2_dmarx1_stream_config = {
	.fmts = rawrd_fmts,
	.fmt_size = ARRAY_SIZE(rawrd_fmts),
	.frame_end_id = RAW1_RD_FRAME,
	.mi = {
		.y_base_ad_init = MI_RAW1_RD_BASE,
		.y_base_ad_shd = MI_RAW1_RD_BASE_SHD,
		.length = MI_RAW1_RD_LENGTH,
	},
};

static struct stream_config rkisp2_dmarx2_stream_config = {
	.fmts = rawrd_fmts,
	.fmt_size = ARRAY_SIZE(rawrd_fmts),
	.frame_end_id = RAW2_RD_FRAME,
	.mi = {
		.y_base_ad_init = MI_RAW2_RD_BASE,
		.y_base_ad_shd = MI_RAW2_RD_BASE_SHD,
		.length = MI_RAW2_RD_LENGTH,
	},
};

static const
struct capture_fmt *find_fmt(struct rkisp_stream *stream,
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

static int dmarx_config_mi(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	void __iomem *base = dev->base_addr;
	struct capture_fmt *dmarx_in_fmt = &stream->out_isp_fmt;

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "%s %dx%d y_stride:%d\n", __func__,
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

static void update_dmarx(struct rkisp_stream *stream)
{
	void __iomem *base = stream->ispdev->base_addr;

	if (stream->curr_buf) {
		mi_set_y_addr(stream,
			stream->curr_buf->buff_addr[RKISP_PLANE_Y]);
		mi_set_cb_addr(stream,
			stream->curr_buf->buff_addr[RKISP_PLANE_CB]);
		mi_set_cr_addr(stream,
			stream->curr_buf->buff_addr[RKISP_PLANE_CR]);
		mi_dmarx_start(base);
		stream->frame_end = false;
	}
}

static void dmarx_stop_mi(struct rkisp_stream *stream)
{
	mi_dmarx_ready_disable(stream);
}

static struct streams_ops rkisp_dmarx_streams_ops = {
	.config_mi = dmarx_config_mi,
	.stop_mi = dmarx_stop_mi,
	.update_mi = update_dmarx,
};

static int rawrd_config_mi(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	u32 val;

	val = rkisp_read(dev, CSI2RX_DATA_IDS_1, true);
	val &= ~SW_CSI_ID0(0xff);
	switch (stream->out_isp_fmt.fourcc) {
	case V4L2_PIX_FMT_SRGGB8:
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_GREY:
		val |= CIF_CSI2_DT_RAW8;
		break;
	case V4L2_PIX_FMT_SRGGB10:
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_Y10:
		val |= CIF_CSI2_DT_RAW10;
		break;
	default:
		val |= CIF_CSI2_DT_RAW12;
	}
	rkisp_write(dev, CSI2RX_DATA_IDS_1, val, false);
	rkisp_rawrd_set_pic_size(dev, stream->out_fmt.width,
				 stream->out_fmt.height);
	mi_raw_length(stream);
	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "%s id:%d 0x%x %dx%d\n", __func__,
		 stream->id, val,
		 stream->out_fmt.width,
		 stream->out_fmt.height);
	return 0;
}

static void update_rawrd(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	void __iomem *base = dev->base_addr;

	if (stream->curr_buf) {
		rkisp_write(dev, stream->config->mi.y_base_ad_init,
			    stream->curr_buf->buff_addr[RKISP_PLANE_Y],
			    false);
		stream->frame_end = false;
	} else if (dev->dmarx_dev.trigger == T_AUTO) {
		/* internal raw wr/rd buf rotate */
		struct rkisp_dummy_buffer *buf;
		u32 id, rawwr_addr, val;

		switch (stream->id) {
		case RKISP_STREAM_RAWRD2:
			id = dev->hdr.index[HDR_DMA2];
			rawwr_addr = MI_RAW2_WR_BASE_SHD;
			break;
		case RKISP_STREAM_RAWRD1:
			id = dev->hdr.index[HDR_DMA1];
			rawwr_addr = MI_RAW1_WR_BASE_SHD;
			break;
		case RKISP_STREAM_RAWRD0:
		default:
			id = dev->hdr.index[HDR_DMA0];
			rawwr_addr = MI_RAW0_WR_BASE_SHD;
		}
		if (dev->hdr.rx_cur_buf[id]) {
			hdr_qbuf(&dev->hdr.q_tx[id], dev->hdr.rx_cur_buf[id]);
			dev->hdr.rx_cur_buf[id] = NULL;
		}
		buf = hdr_dqbuf(&dev->hdr.q_rx[id]);
		if (buf) {
			val = buf->dma_addr;
			dev->hdr.rx_cur_buf[id] = buf;
		} else {
			val = readl(base + rawwr_addr);
		}
		mi_set_y_addr(stream, val);
	}
}

static struct streams_ops rkisp2_dmarx_streams_ops = {
	.config_mi = rawrd_config_mi,
	.update_mi = update_rawrd,
};

static int dmarx_frame_end(struct rkisp_stream *stream)
{
	unsigned long lock_flags = 0;

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	if (stream->curr_buf) {
		vb2_buffer_done(&stream->curr_buf->vb.vb2_buf,
			VB2_BUF_STATE_DONE);
		stream->curr_buf = NULL;
	}

	if (!list_empty(&stream->buf_queue)) {
		stream->curr_buf =
			list_first_entry(&stream->buf_queue,
					struct rkisp_buffer,
					queue);
		list_del(&stream->curr_buf->queue);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);

	stream->ops->update_mi(stream);
	return 0;
}

/***************************** vb2 operations*******************************/

static void dmarx_stop(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	int ret = 0;

	stream->stopping = true;
	if ((dev->isp_state & ISP_START) && !stream->frame_end) {
		ret = wait_event_timeout(stream->done,
					 !stream->streaming,
					 msecs_to_jiffies(100));
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

static int dmarx_start(struct rkisp_stream *stream)
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

static int rkisp_queue_setup(struct vb2_queue *queue,
			      unsigned int *num_buffers,
			      unsigned int *num_planes,
			      unsigned int sizes[],
			      struct device *alloc_ctxs[])
{
	struct rkisp_stream *stream = queue->drv_priv;
	struct rkisp_device *dev = stream->ispdev;
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

	rkisp_chk_tb_over(dev);
	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev, "%s count %d, size %d\n",
		 v4l2_type_names[queue->type], *num_buffers, sizes[0]);

	return 0;
}

/*
 * The vb2_buffer are stored in rkisp_buffer, in order to unify
 * mplane buffer and none-mplane buffer.
 */
static void rkisp_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkisp_buffer *ispbuf = to_rkisp_buffer(vbuf);
	struct vb2_queue *queue = vb->vb2_queue;
	struct rkisp_stream *stream = queue->drv_priv;
	unsigned long lock_flags = 0;
	struct v4l2_pix_format_mplane *pixm = &stream->out_fmt;
	struct capture_fmt *isp_fmt = &stream->out_isp_fmt;
	struct sg_table *sgt;
	int i;

	memset(ispbuf->buff_addr, 0, sizeof(ispbuf->buff_addr));
	for (i = 0; i < isp_fmt->mplanes; i++) {
		if (stream->ispdev->hw_dev->is_dma_sg_ops) {
			sgt = vb2_dma_sg_plane_desc(vb, i);
			ispbuf->buff_addr[i] = sg_dma_address(sgt->sgl);
		} else {
			ispbuf->buff_addr[i] = vb2_dma_contig_plane_dma_addr(vb, i);
		}
	}
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

	v4l2_dbg(2, rkisp_debug, &stream->ispdev->v4l2_dev,
		 "rx:%d queue buf:0x%x\n",
		 stream->id, ispbuf->buff_addr[0]);

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

static void destroy_buf_queue(struct rkisp_stream *stream,
			      enum vb2_buffer_state state)
{
	struct rkisp_buffer *buf;
	unsigned long lock_flags = 0;

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	if (stream->curr_buf) {
		list_add_tail(&stream->curr_buf->queue, &stream->buf_queue);
		stream->curr_buf = NULL;
	}
	while (!list_empty(&stream->buf_queue)) {
		buf = list_first_entry(&stream->buf_queue,
			struct rkisp_buffer, queue);
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);
}

static void dmarx_stop_streaming(struct vb2_queue *queue)
{
	struct rkisp_stream *stream = queue->drv_priv;

	if (!stream->streaming)
		return;

	dmarx_stop(stream);
	destroy_buf_queue(stream, VB2_BUF_STATE_ERROR);
}

static int dmarx_start_streaming(struct vb2_queue *queue,
				 unsigned int count)
{
	struct rkisp_stream *stream = queue->drv_priv;
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	int ret = -1;

	if (WARN_ON(stream->streaming))
		return -EBUSY;

	if (!stream->linked) {
		v4l2_err(v4l2_dev, "check video link\n");
		goto free_buf_queue;
	}

	ret = dmarx_start(stream);
	if (ret < 0) {
		v4l2_err(v4l2_dev,
			 "start dmarx stream:%d failed\n",
			 stream->id);
		goto free_buf_queue;
	}
	return 0;
free_buf_queue:
	destroy_buf_queue(stream, VB2_BUF_STATE_QUEUED);
	return ret;
}

static struct vb2_ops dmarx_vb2_ops = {
	.queue_setup = rkisp_queue_setup,
	.buf_queue = rkisp_buf_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.stop_streaming = dmarx_stop_streaming,
	.start_streaming = dmarx_start_streaming,
};

static int rkisp_init_vb2_queue(struct vb2_queue *q,
				struct rkisp_stream *stream,
				enum v4l2_buf_type buf_type)
{
	q->type = buf_type;
	q->io_modes = VB2_MMAP | VB2_DMABUF | VB2_USERPTR;
	q->drv_priv = stream;
	q->ops = &dmarx_vb2_ops;
	q->mem_ops = stream->ispdev->hw_dev->mem_ops;
	q->buf_struct_size = sizeof(struct rkisp_buffer);
	q->min_buffers_needed = CIF_ISP_REQ_BUFS_MIN;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &stream->apilock;
	q->dev = stream->ispdev->hw_dev->dev;
	q->allow_cache_hints = 1;
	q->bidirectional = 1;
	q->gfp_flags = GFP_DMA32;
	return vb2_queue_init(q);
}

static int rkisp_set_fmt(struct rkisp_stream *stream,
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
	rkisp_fcc_xysubs(fmt->fourcc, &xsubs, &ysubs);
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

		if (stream->ispdev->isp_ver == ISP_V20 &&
		    stream->id == RKISP_STREAM_RAWRD2 &&
		    fmt->fmt_type == FMT_BAYER)
			height += RKMODULE_EXTEND_LINE;

		if ((stream->ispdev->isp_ver == ISP_V20 ||
		     stream->ispdev->isp_ver == ISP_V21) &&
		    fmt->fmt_type == FMT_BAYER &&
		    !stream->ispdev->csi_dev.memory &&
		    stream->id != RKISP_STREAM_DMARX)
			bytesperline = ALIGN(width * fmt->bpp[i] / 8, 256);
		else
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

		v4l2_dbg(1, rkisp_debug, &stream->ispdev->v4l2_dev,
			 "%s: stream: %d req(%d, %d) out(%d, %d)\n", __func__,
			 stream->id, pixm->width, pixm->height,
			 stream->out_fmt.width, stream->out_fmt.height);
	}

	return 0;
}

/************************* v4l2_file_operations***************************/

static const struct v4l2_file_operations rkisp_fops = {
	.open = rkisp_fh_open,
	.release = rkisp_fop_release,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
};

static int rkisp_try_fmt_vid_out_mplane(struct file *file, void *fh,
					 struct v4l2_format *f)
{
	struct rkisp_stream *stream = video_drvdata(file);

	return rkisp_set_fmt(stream, &f->fmt.pix_mp, true);
}

static int rkisp_enum_fmt_vid_out_mplane(struct file *file, void *priv,
					  struct v4l2_fmtdesc *f)
{
	struct rkisp_stream *stream = video_drvdata(file);
	const struct capture_fmt *fmt = NULL;

	if (f->index >= stream->config->fmt_size)
		return -EINVAL;

	fmt = &stream->config->fmts[f->index];
	f->pixelformat = fmt->fourcc;

	return 0;
}

static int rkisp_s_fmt_vid_out_mplane(struct file *file,
				       void *priv, struct v4l2_format *f)
{
	struct rkisp_stream *stream = video_drvdata(file);
	struct video_device *vdev = &stream->vnode.vdev;
	struct rkisp_vdev_node *node = vdev_to_node(vdev);
	struct rkisp_device *dev = stream->ispdev;

	if (vb2_is_busy(&node->buf_queue)) {
		v4l2_err(&dev->v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	return rkisp_set_fmt(stream, &f->fmt.pix_mp, false);
}

static int rkisp_g_fmt_vid_out_mplane(struct file *file, void *fh,
				       struct v4l2_format *f)
{
	struct rkisp_stream *stream = video_drvdata(file);

	f->fmt.pix_mp = stream->out_fmt;

	return 0;
}

static int rkisp_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct rkisp_stream *stream = video_drvdata(file);
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

static const struct v4l2_ioctl_ops rkisp_dmarx_ioctl = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_try_fmt_vid_out_mplane = rkisp_try_fmt_vid_out_mplane,
	.vidioc_enum_fmt_vid_out_mplane = rkisp_enum_fmt_vid_out_mplane,
	.vidioc_s_fmt_vid_out_mplane = rkisp_s_fmt_vid_out_mplane,
	.vidioc_g_fmt_vid_out_mplane = rkisp_g_fmt_vid_out_mplane,
	.vidioc_querycap = rkisp_querycap,
};

static void rkisp_unregister_dmarx_video(struct rkisp_stream *stream)
{
	media_entity_cleanup(&stream->vnode.vdev.entity);
	video_unregister_device(&stream->vnode.vdev);
}

static int rkisp_register_dmarx_video(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct video_device *vdev = &stream->vnode.vdev;
	struct rkisp_vdev_node *node;
	int ret = 0;

	mutex_init(&stream->apilock);
	node = vdev_to_node(vdev);

	vdev->release = video_device_release_empty;
	vdev->fops = &rkisp_fops;
	vdev->minor = -1;
	vdev->v4l2_dev = v4l2_dev;
	vdev->lock = &stream->apilock;
	video_set_drvdata(vdev, stream);

	vdev->ioctl_ops = &rkisp_dmarx_ioctl;
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
			 "%s failed with error %d\n", __func__, ret);
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

void rkisp_dmarx_isr(u32 mis_val, struct rkisp_device *dev)
{
	void __iomem *base = dev->base_addr;
	struct rkisp_stream *stream;

	if (mis_val & CIF_MI_DMA_READY) {
		stream = &dev->dmarx_dev.stream[RKISP_STREAM_DMARX];
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

void rkisp2_rawrd_isr(u32 mis_val, struct rkisp_device *dev)
{
	struct rkisp_stream *stream;
	int i;

	for (i = RKISP_STREAM_RAWRD0; i < RKISP_MAX_DMARX_STREAM; i++) {
		stream = &dev->dmarx_dev.stream[i];
		if (!(mis_val & CIF_MI_FRAME(stream)))
			continue;
		stream->frame_end = true;
		if (stream->stopping) {
			stream->stopping = false;
			stream->streaming = false;
			wake_up(&stream->done);
		} else {
			dmarx_frame_end(stream);
		}
	}
}

static int dmarx_init(struct rkisp_device *dev, u32 id)
{
	struct rkisp_dmarx_device *dmarx_dev = &dev->dmarx_dev;
	struct rkisp_stream *stream;
	struct video_device *vdev;
	struct media_entity *source, *sink;
	int ret = 0;

	stream = &dmarx_dev->stream[id];
	INIT_LIST_HEAD(&stream->buf_queue);
	init_waitqueue_head(&stream->done);
	spin_lock_init(&stream->vbq_lock);
	stream->id = id;
	stream->ispdev = dev;
	vdev = &stream->vnode.vdev;
	stream->linked = false;

	switch (id) {
	case RKISP_STREAM_DMARX:
		strlcpy(vdev->name, DMA_VDEV_NAME,
			sizeof(vdev->name));
		stream->ops = &rkisp_dmarx_streams_ops;
		stream->config = &rkisp_dmarx_stream_config;
		break;
	case RKISP_STREAM_RAWRD0:
		strlcpy(vdev->name, DMARX0_VDEV_NAME,
			sizeof(vdev->name));
		stream->ops = &rkisp2_dmarx_streams_ops;
		stream->config = &rkisp2_dmarx0_stream_config;
		break;
	case RKISP_STREAM_RAWRD1:
		strlcpy(vdev->name, DMARX1_VDEV_NAME,
			sizeof(vdev->name));
		stream->ops = &rkisp2_dmarx_streams_ops;
		stream->config = &rkisp2_dmarx1_stream_config;
		break;
	case RKISP_STREAM_RAWRD2:
		strlcpy(vdev->name, DMARX2_VDEV_NAME,
			sizeof(vdev->name));
		stream->ops = &rkisp2_dmarx_streams_ops;
		stream->config = &rkisp2_dmarx2_stream_config;
		break;
	default:
		v4l2_err(&dev->v4l2_dev, "Invalid dmarx\n");
		return -EINVAL;
	}
	ret = rkisp_register_dmarx_video(stream);
	if (ret < 0)
		return ret;

	/* dmarx link -> isp subdev */
	source = &vdev->entity;
	sink = &dev->isp_sdev.sd.entity;
	return media_create_pad_link(source, 0, sink,
		RKISP_ISP_PAD_SINK, stream->linked);
}

void rkisp_dmarx_set_fmt(struct rkisp_stream *stream,
			 struct v4l2_pix_format_mplane pixm)
{
	rkisp_set_fmt(stream, &pixm, false);
}

void rkisp_rawrd_set_pic_size(struct rkisp_device *dev,
			      u32 width, u32 height)
{
	struct rkisp_isp_subdev *sdev = &dev->isp_sdev;

	/* rx height should equal to isp height + offset for read back mode */
	height = sdev->in_crop.top + sdev->in_crop.height;

	/* isp20 extend line for normal read back mode to fix internal bug */
	if (dev->isp_ver == ISP_V20 &&
	    sdev->in_fmt.fmt_type == FMT_BAYER &&
	    sdev->out_fmt.fmt_type != FMT_BAYER &&
	    dev->rd_mode == HDR_RDBK_FRAME1)
		height += RKMODULE_EXTEND_LINE;

	rkisp_write(dev, CSI2RX_RAW_RD_PIC_SIZE, height << 16 | width, false);
}

void rkisp_dmarx_get_frame(struct rkisp_device *dev, u32 *id,
			   u64 *sof_timestamp, u64 *timestamp,
			   bool sync)
{
	unsigned long flag = 0;
	u64 sof_time = 0, frame_timestamp = 0;
	u32 frame_id = 0;

	if (!dev->dmarx_dev.trigger && id) {
		*id = atomic_read(&dev->isp_sdev.frm_sync_seq) - 1;
		return;
	}

	spin_lock_irqsave(&dev->rdbk_lock, flag);
	if (sync) {
		frame_id = dev->dmarx_dev.cur_frame.id;
		sof_time = dev->dmarx_dev.cur_frame.sof_timestamp;
		frame_timestamp = dev->dmarx_dev.cur_frame.timestamp;
	} else {
		frame_id = dev->dmarx_dev.pre_frame.id;
		sof_time = dev->dmarx_dev.pre_frame.sof_timestamp;
		frame_timestamp = dev->dmarx_dev.pre_frame.timestamp;
	}
	spin_unlock_irqrestore(&dev->rdbk_lock, flag);
	if (id)
		*id = frame_id;
	if (sof_timestamp)
		*sof_timestamp = sof_time;
	if (timestamp)
		*timestamp = frame_timestamp;
}

int rkisp_register_dmarx_vdev(struct rkisp_device *dev)
{
	struct rkisp_dmarx_device *dmarx_dev = &dev->dmarx_dev;
	int ret = 0;

	memset(dmarx_dev, 0, sizeof(*dmarx_dev));
	dmarx_dev->ispdev = dev;

#ifdef RKISP_DMAREAD_EN
	ret = dmarx_init(dev, RKISP_STREAM_DMARX);
	if (ret < 0)
		goto err;
#endif
	if (dev->isp_ver == ISP_V20 || dev->isp_ver == ISP_V21) {
		ret = dmarx_init(dev, RKISP_STREAM_RAWRD0);
		if (ret < 0)
			goto err_free_dmarx;
		ret = dmarx_init(dev, RKISP_STREAM_RAWRD2);
		if (ret < 0)
			goto err_free_dmarx0;
	}
	if (dev->isp_ver == ISP_V20) {
		ret = dmarx_init(dev, RKISP_STREAM_RAWRD1);
		if (ret < 0)
			goto err_free_dmarx2;
	}

	return 0;
err_free_dmarx2:
	rkisp_unregister_dmarx_video(&dmarx_dev->stream[RKISP_STREAM_RAWRD2]);
err_free_dmarx0:
	rkisp_unregister_dmarx_video(&dmarx_dev->stream[RKISP_STREAM_RAWRD0]);
err_free_dmarx:
#ifdef RKISP_DMAREAD_EN
	rkisp_unregister_dmarx_video(&dmarx_dev->stream[RKISP_STREAM_DMARX]);
err:
#endif
	return ret;
}

void rkisp_unregister_dmarx_vdev(struct rkisp_device *dev)
{
	struct rkisp_dmarx_device *dmarx_dev = &dev->dmarx_dev;
	struct rkisp_stream *stream;

#ifdef RKISP_DMAREAD_EN
	stream = &dmarx_dev->stream[RKISP_STREAM_DMARX];
	rkisp_unregister_dmarx_video(stream);
#endif

	if (dev->isp_ver == ISP_V20 || dev->isp_ver == ISP_V21) {
		stream = &dmarx_dev->stream[RKISP_STREAM_RAWRD0];
		rkisp_unregister_dmarx_video(stream);

		stream = &dmarx_dev->stream[RKISP_STREAM_RAWRD2];
		rkisp_unregister_dmarx_video(stream);
	}
	if (dev->isp_ver == ISP_V20) {
		stream = &dmarx_dev->stream[RKISP_STREAM_RAWRD1];
		rkisp_unregister_dmarx_video(stream);
	}
}
