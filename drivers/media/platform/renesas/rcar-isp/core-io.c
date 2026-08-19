// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-isp.h>
#include <media/v4l2-mc.h>

#include <linux/media/dreamchip/rppx1-config.h>

#include "risp-core.h"

#define risp_io_err(d, fmt, arg...)         dev_err((d)->core->dev, fmt, ##arg)

static struct risp_buffer *risp_io_vb2buf(struct vb2_v4l2_buffer *vb)
{
	return container_of(vb, struct risp_buffer, vb);
}

static int risp_io_open(struct file *file)
{
	struct rcar_isp_core_io *io = video_drvdata(file);
	int ret;

	ret = mutex_lock_interruptible(&io->lock);
	if (ret)
		return ret;

	file->private_data = io;

	ret = v4l2_fh_open(file);
	if (ret)
		goto err_unlock;

	ret = v4l2_pipeline_pm_get(&io->vdev.entity);
	if (ret < 0)
		goto err_open;

	mutex_unlock(&io->lock);

	return 0;
err_open:
	v4l2_fh_release(file);
err_unlock:
	mutex_unlock(&io->lock);

	return ret;
}

static int risp_io_release(struct file *file)
{
	struct rcar_isp_core_io *io = video_drvdata(file);
	int ret;

	mutex_lock(&io->lock);

	ret = _vb2_fop_release(file, NULL);

	v4l2_pipeline_pm_put(&io->vdev.entity);

	mutex_unlock(&io->lock);

	return ret;
}

static const struct v4l2_file_operations risp_io_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= video_ioctl2,
	.open		= risp_io_open,
	.release	= risp_io_release,
	.poll		= vb2_fop_poll,
	.mmap		= vb2_fop_mmap,
	.read		= vb2_fop_read,
};

/* -----------------------------------------------------------------------------
 * Common queue
 */

static int risp_io_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
			       unsigned int *nplanes, unsigned int sizes[],
			       struct device *alloc_devs[])

{
	struct rcar_isp_core_io *io = vb2_get_drv_priv(vq);

	if (V4L2_TYPE_IS_MULTIPLANAR(vq->type)) {
		const struct v4l2_pix_format_mplane *pix = &io->format.fmt.pix_mp;

		if (*nplanes) {
			if (*nplanes > pix->num_planes)
				return -EINVAL;

			for (unsigned int i = 0; i < pix->num_planes; i++)
				if (sizes[i] < pix->plane_fmt[i].sizeimage)
					return -EINVAL;

			return 0;
		}

		*nplanes = pix->num_planes;
		for (unsigned int i = 0; i < pix->num_planes; i++)
			sizes[i] = pix->plane_fmt[i].sizeimage;
	} else {
		if (*nplanes) {
			if (sizes[0] < io->format.fmt.meta.buffersize)
				return -EINVAL;

			return 0;
		}

		*nplanes = 1;
		sizes[0] = io->format.fmt.meta.buffersize;
	}

	/* Initialize buffer queue */
	INIT_LIST_HEAD(&io->buffers);

	return 0;
};

static int risp_io_buffer_prepare_set(struct rcar_isp_core_io *io,
				      struct vb2_buffer *vb, unsigned int plane,
				      unsigned long size)
{
	if (vb2_plane_size(vb, plane) < size) {
		risp_io_err(io, "Buffer too small (%lu < %lu)\n",
			    vb2_plane_size(vb, plane), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, plane, size);

	return 0;
}

static int risp_io_buffer_prepare(struct vb2_buffer *vb)
{
	struct rcar_isp_core_io *io = vb2_get_drv_priv(vb->vb2_queue);

	if (V4L2_TYPE_IS_MULTIPLANAR(vb->vb2_queue->type)) {
		const struct v4l2_pix_format_mplane *pix = &io->format.fmt.pix_mp;
		int ret = 0;

		for (unsigned int i = 0; i < pix->num_planes; i++) {
			ret = risp_io_buffer_prepare_set(io, vb, i,
							 pix->plane_fmt[i].sizeimage);
			if (ret)
				break;
		}

		return ret;
	}

	return risp_io_buffer_prepare_set(io, vb, 0,
					  io->format.fmt.meta.buffersize);
}

static void risp_io_buffer_queue(struct vb2_buffer *vb)
{
	struct rcar_isp_core_io *io = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct risp_buffer *buf = risp_io_vb2buf(vbuf);

	guard(mutex)(&io->core->io_lock);

	list_add_tail(&buf->list, &io->buffers);

	if (risp_core_job_prepare(io->core))
		risp_io_err(io, "Failed to prepare job\n");
}

static void risp_io_return_buffers(struct rcar_isp_core_io *io,
				   enum vb2_buffer_state state)
{
	struct risp_buffer *buf, *node;

	lockdep_assert_held(&io->core->io_lock);

	list_for_each_entry_safe(buf, node, &io->buffers, list) {
		vb2_buffer_done(&buf->vb.vb2_buf, state);
		list_del(&buf->list);
	}
}

static int risp_io_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct rcar_isp_core_io *io = vb2_get_drv_priv(vq);
	int ret;

	scoped_guard(mutex, &io->core->io_lock) {
		if (io->core->io[RISP_CORE_INPUT1].format.fmt.pix_mp.width !=
		    io->core->io[RISP_CORE_OUTPUT1].format.fmt.pix_mp.width ||
		    io->core->io[RISP_CORE_INPUT1].format.fmt.pix_mp.height !=
		    io->core->io[RISP_CORE_OUTPUT1].format.fmt.pix_mp.height) {
			risp_io_return_buffers(io, VB2_BUF_STATE_QUEUED);
			return -EPIPE;
		}

		io->streaming = true;
	}

	ret = risp_core_start_streaming(io->core);
	if (ret) {
		guard(mutex)(&io->core->io_lock);

		risp_io_return_buffers(io, VB2_BUF_STATE_QUEUED);
		return ret;
	}

	return 0;
}

static void risp_io_stop_streaming(struct vb2_queue *vq)
{
	struct rcar_isp_core_io *io = vb2_get_drv_priv(vq);

	scoped_guard(mutex, &io->core->io_lock) {
		io->streaming = false;
		risp_core_stop_streaming(io->core);
		risp_io_return_buffers(io, VB2_BUF_STATE_ERROR);
	}

	/*
	 * Wait for buffers part of the jobs not yet processed. Note that this
	 * might complete buffers out of order.
	 */
	vb2_wait_for_all_buffers(&io->queue);
}

/* -----------------------------------------------------------------------------
 * Common V4L2 IOCTLs
 */

static int risp_io_querycap(struct file *file, void *priv,
			    struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);

	strscpy(cap->driver, KBUILD_MODNAME, sizeof(cap->driver));
	strscpy(cap->card, vdev->name, sizeof(cap->card));

	return 0;
}

/* -----------------------------------------------------------------------------
 * Input Exposure
 */

static int risp_io_input_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
				     unsigned int *nplanes, unsigned int sizes[],
				     struct device *alloc_devs[])

{
	struct rcar_isp_core_io *io = vb2_get_drv_priv(vq);
	struct rcar_isp_core *core = io->core;
	struct device *bus_master;
	int ret;

	ret = risp_io_queue_setup(vq, nbuffers, nplanes, sizes, alloc_devs);
	if (ret)
		return ret;

	bus_master = vsp1_isp_get_bus_master(core->vspx.dev);
	if (IS_ERR_OR_NULL(bus_master)) {
		risp_io_err(io, "Missing reference to bus-master device\n");
		return -EINVAL;
	}

	/*
	 * Allocate buffers using the bus_master device associated with the
	 * VSPX associated to this ISP instance.
	 */
	alloc_devs[0] = bus_master;

	return 0;
};

static const struct vb2_ops risp_io_input_qops = {
	.queue_setup		= risp_io_input_queue_setup,
	.buf_prepare		= risp_io_buffer_prepare,
	.buf_queue		= risp_io_buffer_queue,
	.start_streaming	= risp_io_start_streaming,
	.stop_streaming		= risp_io_stop_streaming,
};

static const struct v4l2_pix_format_mplane risp_io_input_default_format = {
	.width = 1920,
	.height = 1080,
	.field = V4L2_FIELD_NONE,
	.pixelformat = V4L2_PIX_FMT_SGRBG8,
	.colorspace = V4L2_COLORSPACE_RAW,
	.xfer_func = V4L2_XFER_FUNC_NONE,
	.ycbcr_enc = V4L2_YCBCR_ENC_601,
	.quantization = V4L2_QUANTIZATION_FULL_RANGE,
	.num_planes = 1,
	.plane_fmt = {
		[0] = {
			.sizeimage = 1920 * 1080,
			.bytesperline = 1920,
		},
	},
};

static const struct risp_io_input_format {
	unsigned int fourcc;
	unsigned int bpp;
} risp_io_input_formats[] = {
	{ .fourcc = V4L2_PIX_FMT_SBGGR8,	.bpp = 1 },
	{ .fourcc = V4L2_PIX_FMT_SGBRG8,	.bpp = 1 },
	{ .fourcc = V4L2_PIX_FMT_SGRBG8,	.bpp = 1 },
	{ .fourcc = V4L2_PIX_FMT_SRGGB8,	.bpp = 1 },
	{ .fourcc = V4L2_PIX_FMT_SBGGR10,	.bpp = 2 },
	{ .fourcc = V4L2_PIX_FMT_SGBRG10,	.bpp = 2 },
	{ .fourcc = V4L2_PIX_FMT_SGRBG10,	.bpp = 2 },
	{ .fourcc = V4L2_PIX_FMT_SRGGB10,	.bpp = 2 },
	{ .fourcc = V4L2_PIX_FMT_SBGGR12,	.bpp = 2 },
	{ .fourcc = V4L2_PIX_FMT_SGBRG12,	.bpp = 2 },
	{ .fourcc = V4L2_PIX_FMT_SGRBG12,	.bpp = 2 },
	{ .fourcc = V4L2_PIX_FMT_SRGGB12,	.bpp = 2 },
};

static void risp_io_input_try_format(struct rcar_isp_core_io *io,
				     struct v4l2_pix_format_mplane *pix)
{
	unsigned int bpp = 0;

	v4l_bound_align_image(&pix->width, 128, 5120, 2,
			      &pix->height, 128, 4096, 2, 0);

	for (unsigned int i = 0; i < ARRAY_SIZE(risp_io_input_formats); i++) {
		if (risp_io_input_formats[i].fourcc == pix->pixelformat) {
			bpp = risp_io_input_formats[i].bpp;
			break;
		}
	}

	if (!bpp) {
		pix->pixelformat = risp_io_input_formats[0].fourcc;
		bpp = risp_io_input_formats[0].bpp;
	}

	pix->field = V4L2_FIELD_NONE;
	pix->colorspace = V4L2_COLORSPACE_RAW;

	pix->num_planes = 1;
	pix->plane_fmt[0].bytesperline = pix->width * bpp;
	pix->plane_fmt[0].sizeimage = pix->plane_fmt[0].bytesperline * pix->height;
}

static int risp_io_input_enum_fmt(struct file *file, void *priv,
				  struct v4l2_fmtdesc *f)
{
	if (f->index >= ARRAY_SIZE(risp_io_input_formats))
		return -EINVAL;

	f->pixelformat = risp_io_input_formats[f->index].fourcc;

	return 0;
}

static int risp_io_input_g_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct rcar_isp_core_io *io = video_drvdata(file);

	if (f->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	f->fmt.pix_mp = io->format.fmt.pix_mp;

	return 0;
}

static int risp_io_input_s_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct rcar_isp_core_io *io = video_drvdata(file);

	if (vb2_is_busy(&io->queue))
		return -EBUSY;

	if (f->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	risp_io_input_try_format(io, &f->fmt.pix_mp);

	io->format.fmt.pix_mp = f->fmt.pix_mp;

	return 0;
}

static int risp_io_input_try_fmt(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct rcar_isp_core_io *io = video_drvdata(file);

	risp_io_input_try_format(io, &f->fmt.pix_mp);

	return 0;
}

static int risp_io_input_enum_framesizes(struct file *file, void *fh,
					 struct v4l2_frmsizeenum *fsize)
{
	bool found = false;

	if (fsize->index != 0)
		return -EINVAL;

	for (unsigned int i = 0; i < ARRAY_SIZE(risp_io_input_formats); i++) {
		if (risp_io_input_formats[i].fourcc == fsize->pixel_format) {
			found = true;
			break;
		}
	}

	if (!found)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;

	fsize->stepwise.min_width = 128;
	fsize->stepwise.max_width = 5120;
	fsize->stepwise.step_width = 2;

	fsize->stepwise.min_height = 128;
	fsize->stepwise.max_height = 4096;
	fsize->stepwise.step_height = 2;

	return 0;
}

static const struct v4l2_ioctl_ops risp_io_input_ioctl_ops = {
	.vidioc_querycap		= risp_io_querycap,

	.vidioc_enum_fmt_vid_out	= risp_io_input_enum_fmt,
	.vidioc_g_fmt_vid_out_mplane	= risp_io_input_g_fmt,
	.vidioc_s_fmt_vid_out_mplane	= risp_io_input_s_fmt,
	.vidioc_try_fmt_vid_out_mplane	= risp_io_input_try_fmt,
	.vidioc_enum_framesizes		= risp_io_input_enum_framesizes,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
};

/* -----------------------------------------------------------------------------
 * Parameters
 *
 */

static int risp_io_params_buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct risp_buffer *buf = risp_io_vb2buf(vbuf);
	struct rcar_isp_core_io *io = vb2_get_drv_priv(vb->vb2_queue);
	struct rcar_isp_core *core = io->core;
	size_t size;
	int ret;

	memset(&buf->vsp_buffer, 0, sizeof(buf->vsp_buffer));

	size = RISP_IO_PARAMS_BUF_SIZE;
	ret = vsp1_isp_alloc_buffer(core->vspx.dev, size, &buf->vsp_buffer);
	if (ret)
		return -EINVAL;

	memset(buf->vsp_buffer.cpu_addr, 0, RISP_IO_PARAMS_BUF_SIZE);

	return 0;
}

static void risp_io_params_buf_cleanup(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct risp_buffer *buf = risp_io_vb2buf(vbuf);
	struct rcar_isp_core_io *io = vb2_get_drv_priv(vb->vb2_queue);
	struct rcar_isp_core *core = io->core;

	vsp1_isp_free_buffer(core->vspx.dev, &buf->vsp_buffer);
}

struct risp_conf_dma_write_desc {
	u32 *buf;
	u32 base;
	unsigned int count;
};

static int risp_conf_dma_prepare(void *priv, u32 offset, u32 value)
{
	struct risp_conf_dma_write_desc *desc = priv;

	/* Bounds check, 8 bytes = address (4)+ value (4). */
	if ((desc->count + 1) * 8 > RISP_IO_PARAMS_BUF_SIZE)
		return -ENOMEM;

	(*desc->buf++) = desc->base | offset;
	(*desc->buf++) = value;

	desc->count++;

	return 0;
}

static int risp_io_params_buffer_prepare(struct vb2_buffer *vb)
{
	struct rcar_isp_core_io *io = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct risp_buffer *buf = risp_io_vb2buf(vbuf);
	struct risp_conf_dma_write_desc desc;
	u32 *cpu_addr;
	int ret;

	/* Prepare params. */
	cpu_addr = (u32 *)buf->vsp_buffer.cpu_addr;

	desc.buf = cpu_addr + 2;
	desc.base = io->core->rppaddr;
	desc.count = 0;

	/* Fill params body. */
	ret = rppx1_params(io->core->rpp, vb, io->format.fmt.meta.buffersize,
			   risp_conf_dma_prepare, &desc);
	if (ret)
		return ret;

	/* Fill params header. */
	cpu_addr[0] = desc.count;
	cpu_addr[1] = 0x0;

	return 0;
}

static const struct vb2_ops risp_io_params_qops = {
	.queue_setup		= risp_io_queue_setup,
	.buf_init		= risp_io_params_buf_init,
	.buf_cleanup		= risp_io_params_buf_cleanup,
	.buf_prepare		= risp_io_params_buffer_prepare,
	.buf_queue		= risp_io_buffer_queue,
	.start_streaming	= risp_io_start_streaming,
	.stop_streaming		= risp_io_stop_streaming,
};

static const struct v4l2_meta_format risp_io_params_default_format = {
	.dataformat = V4L2_META_FMT_RPPX1_PARAMS,
	.buffersize = v4l2_isp_buffer_size(RPPX1_PARAMS_MAX_SIZE),
};

static int risp_io_params_enum_fmt(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	struct rcar_isp_core_io *io = video_drvdata(file);

	if (f->type != V4L2_BUF_TYPE_META_OUTPUT || f->index)
		return -EINVAL;

	f->pixelformat = io->format.fmt.meta.dataformat;

	return 0;
}

static int risp_io_params_g_fmt(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct rcar_isp_core_io *io = video_drvdata(file);
	struct v4l2_meta_format *meta = &f->fmt.meta;

	if (f->type != V4L2_BUF_TYPE_META_OUTPUT)
		return -EINVAL;

	*meta = io->format.fmt.meta;

	return 0;
}

static int risp_io_params_s_fmt(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct rcar_isp_core_io *io = video_drvdata(file);

	if (vb2_is_busy(&io->queue))
		return -EBUSY;

	return risp_io_params_g_fmt(file, priv, f);
}

static const struct v4l2_ioctl_ops risp_io_params_ioctl_ops = {
	.vidioc_querycap		= risp_io_querycap,

	.vidioc_enum_fmt_meta_out	= risp_io_params_enum_fmt,
	.vidioc_g_fmt_meta_out		= risp_io_params_g_fmt,
	.vidioc_s_fmt_meta_out		= risp_io_params_s_fmt,
	.vidioc_try_fmt_meta_out	= risp_io_params_g_fmt,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
};

/* -----------------------------------------------------------------------------
 * Statistics
 */

static const struct vb2_ops risp_io_stats_qops = {
	.queue_setup		= risp_io_queue_setup,
	.buf_prepare		= risp_io_buffer_prepare,
	.buf_queue		= risp_io_buffer_queue,
	.start_streaming	= risp_io_start_streaming,
	.stop_streaming		= risp_io_stop_streaming,
};

static const struct v4l2_meta_format risp_io_stats_default_format = {
	.dataformat = V4L2_META_FMT_RPPX1_STATS,
	.buffersize = v4l2_isp_buffer_size(RPPX1_STATS_MAX_SIZE),
};

static int risp_io_stats_enum_fmt(struct file *file, void *priv,
				  struct v4l2_fmtdesc *f)
{
	struct rcar_isp_core_io *io = video_drvdata(file);

	if (f->type != V4L2_BUF_TYPE_META_CAPTURE || f->index)
		return -EINVAL;

	f->pixelformat = io->format.fmt.meta.dataformat;

	return 0;
}

static int risp_io_stats_g_fmt(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct rcar_isp_core_io *io = video_drvdata(file);
	struct v4l2_meta_format *meta = &f->fmt.meta;

	if (f->type != V4L2_BUF_TYPE_META_CAPTURE)
		return -EINVAL;

	*meta = io->format.fmt.meta;

	return 0;
}

static int risp_io_stats_s_fmt(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct rcar_isp_core_io *io = video_drvdata(file);

	if (vb2_is_busy(&io->queue))
		return -EBUSY;

	return risp_io_stats_g_fmt(file, priv, f);
}

static const struct v4l2_ioctl_ops risp_io_stats_ioctl_ops = {
	.vidioc_querycap		= risp_io_querycap,

	.vidioc_enum_fmt_meta_cap	= risp_io_stats_enum_fmt,
	.vidioc_g_fmt_meta_cap		= risp_io_stats_g_fmt,
	.vidioc_s_fmt_meta_cap		= risp_io_stats_s_fmt,
	.vidioc_try_fmt_meta_cap	= risp_io_stats_g_fmt,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
};

/* -----------------------------------------------------------------------------
 * Video capture
 */

static const struct vb2_ops risp_io_capture_qops = {
	.queue_setup		= risp_io_queue_setup,
	.buf_prepare		= risp_io_buffer_prepare,
	.buf_queue		= risp_io_buffer_queue,
	.start_streaming	= risp_io_start_streaming,
	.stop_streaming		= risp_io_stop_streaming,
};

static const struct v4l2_pix_format_mplane risp_io_capture_default_format = {
	.width = 1920,
	.height = 1080,
	.pixelformat = V4L2_PIX_FMT_XBGR32,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_SRGB,
	.ycbcr_enc = V4L2_YCBCR_ENC_601,
	.quantization = V4L2_QUANTIZATION_FULL_RANGE,
	.xfer_func = V4L2_XFER_FUNC_SRGB,
	.num_planes = 1,
	.plane_fmt = {
		[0] = {
			.bytesperline = ALIGN(1920 * 4, 256),
			.sizeimage = ALIGN(1920 * 4, 256) * 1080,
		},
	},
};

static void risp_io_capture_try_format(struct rcar_isp_core_io *io,
				       struct v4l2_pix_format_mplane *pix)
{
	v4l_bound_align_image(&pix->width, 128, 5120, 2,
			      &pix->height, 128, 4096, 2, 0);

	pix->field = V4L2_FIELD_NONE;
	pix->colorspace = V4L2_COLORSPACE_SRGB;
	pix->ycbcr_enc = V4L2_YCBCR_ENC_601;
	pix->xfer_func = V4L2_XFER_FUNC_SRGB;

	switch (pix->pixelformat) {
	case V4L2_PIX_FMT_NV16M:
		pix->quantization = V4L2_QUANTIZATION_LIM_RANGE;
		pix->num_planes = 2;
		pix->plane_fmt[0].bytesperline = ALIGN(pix->width, 256);
		pix->plane_fmt[0].sizeimage = pix->plane_fmt[0].bytesperline * pix->height;
		pix->plane_fmt[1].bytesperline = ALIGN(pix->width, 256);
		pix->plane_fmt[1].sizeimage = pix->plane_fmt[1].bytesperline * pix->height;
		break;
	default:
		pix->pixelformat = V4L2_PIX_FMT_XBGR32;
		pix->quantization = V4L2_QUANTIZATION_FULL_RANGE;
		pix->num_planes = 1;
		pix->plane_fmt[0].bytesperline = ALIGN(pix->width * 4, 256);
		pix->plane_fmt[0].sizeimage = pix->plane_fmt[0].bytesperline * pix->height;
		break;
	}
}

static int risp_io_capture_enum_fmt(struct file *file, void *priv,
				    struct v4l2_fmtdesc *f)
{
	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	switch (f->index) {
	case 0:
		f->pixelformat = V4L2_PIX_FMT_NV16M;
		break;
	case 1:
		f->pixelformat = V4L2_PIX_FMT_XBGR32;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int risp_io_capture_g_fmt(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct rcar_isp_core_io *io = video_drvdata(file);

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	f->fmt.pix_mp = io->format.fmt.pix_mp;

	return 0;
}

static int risp_io_capture_s_fmt(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct rcar_isp_core_io *io = video_drvdata(file);

	if (vb2_is_busy(&io->queue))
		return -EBUSY;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	risp_io_capture_try_format(io, &f->fmt.pix_mp);

	io->format.fmt.pix_mp = f->fmt.pix_mp;

	return 0;
}

static int risp_io_capture_try_fmt(struct file *file, void *fh,
				   struct v4l2_format *f)
{
	struct rcar_isp_core_io *io = video_drvdata(file);

	risp_io_capture_try_format(io, &f->fmt.pix_mp);

	return 0;
}

static int risp_io_capture_enum_framesizes(struct file *file, void *fh,
					   struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index != 0)
		return -EINVAL;

	switch (fsize->pixel_format) {
	case V4L2_PIX_FMT_NV16M:
	case V4L2_PIX_FMT_XBGR32:
		break;
	default:
		return -EINVAL;
	}

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;

	fsize->stepwise.min_width = 128;
	fsize->stepwise.max_width = 5120;
	fsize->stepwise.step_width = 2;

	fsize->stepwise.min_height = 128;
	fsize->stepwise.max_height = 4096;
	fsize->stepwise.step_height = 2;

	return 0;
}

static const struct v4l2_ioctl_ops risp_io_capture_ioctl_ops = {
	.vidioc_querycap		= risp_io_querycap,

	.vidioc_enum_fmt_vid_cap	= risp_io_capture_enum_fmt,
	.vidioc_g_fmt_vid_cap_mplane	= risp_io_capture_g_fmt,
	.vidioc_s_fmt_vid_cap_mplane	= risp_io_capture_s_fmt,
	.vidioc_try_fmt_vid_cap_mplane	= risp_io_capture_try_fmt,
	.vidioc_enum_framesizes		= risp_io_capture_enum_framesizes,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
};

/* -----------------------------------------------------------------------------
 * Create and remove IO video devices
 */

int risp_core_io_create(struct device *dev, struct rcar_isp_core *core,
			struct rcar_isp_core_io *io, unsigned int pad)
{
	struct video_device *vdev = &io->vdev;
	struct vb2_queue *q = &io->queue;
	int ret;

	switch (pad) {
	case RISP_CORE_INPUT1:
		snprintf(vdev->name, sizeof(vdev->name), "%s %s input1",
			 KBUILD_MODNAME, dev_name(dev));
		vdev->vfl_dir = VFL_DIR_TX;
		vdev->device_caps = V4L2_CAP_VIDEO_OUTPUT_MPLANE;
		vdev->ioctl_ops = &risp_io_input_ioctl_ops;

		q->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		q->ops = &risp_io_input_qops;

		io->pad.flags = MEDIA_PAD_FL_SOURCE;
		io->format.fmt.pix_mp = risp_io_input_default_format;
		break;

	case RISP_CORE_PARAMS:
		snprintf(vdev->name, sizeof(vdev->name), "%s %s params",
			 KBUILD_MODNAME, dev_name(dev));
		vdev->vfl_dir = VFL_DIR_TX;
		vdev->device_caps = V4L2_CAP_META_OUTPUT;
		vdev->ioctl_ops = &risp_io_params_ioctl_ops;

		q->type = V4L2_BUF_TYPE_META_OUTPUT;
		q->ops = &risp_io_params_qops;

		io->pad.flags = MEDIA_PAD_FL_SOURCE;
		io->format.fmt.meta = risp_io_params_default_format;
		break;

	case RISP_CORE_STATS:
		snprintf(vdev->name, sizeof(vdev->name), "%s %s stats",
			 KBUILD_MODNAME, dev_name(dev));
		vdev->vfl_dir = VFL_DIR_RX;
		vdev->device_caps = V4L2_CAP_META_CAPTURE;
		vdev->ioctl_ops = &risp_io_stats_ioctl_ops;

		q->type = V4L2_BUF_TYPE_META_CAPTURE;
		q->ops = &risp_io_stats_qops;

		io->pad.flags = MEDIA_PAD_FL_SINK;
		io->format.fmt.meta = risp_io_stats_default_format;
		break;

	case RISP_CORE_OUTPUT1:
		snprintf(vdev->name, sizeof(vdev->name), "%s %s output1",
			 KBUILD_MODNAME, dev_name(dev));
		vdev->vfl_dir = VFL_DIR_RX;
		vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE;
		vdev->ioctl_ops = &risp_io_capture_ioctl_ops;

		q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		q->ops = &risp_io_capture_qops;

		io->pad.flags = MEDIA_PAD_FL_SINK;
		io->format.fmt.pix_mp = risp_io_capture_default_format;
		break;
	}

	io->core = core;

	mutex_init(&io->lock);
	INIT_LIST_HEAD(&io->buffers);

	/* Create media graph pad. */
	ret = media_entity_pads_init(&io->vdev.entity, 1, &io->pad);
	if (ret)
		return ret;

	/* Create queue */
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->lock = &io->lock;
	q->drv_priv = io;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct risp_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->dev = dev;

	ret = vb2_queue_init(q);
	if (ret < 0) {
		risp_io_err(io, "Failed to initialize VB2 queue\n");
		return ret;
	}

	/* Create video device */
	vdev->v4l2_dev = &core->v4l2_dev;
	vdev->queue = &io->queue;

	vdev->release = video_device_release_empty;
	vdev->lock = &io->lock;
	vdev->fops = &risp_io_fops;

	vdev->device_caps |= V4L2_CAP_STREAMING | V4L2_CAP_IO_MC;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		risp_io_err(io, "Failed to register video device\n");
		return ret;
	}

	video_set_drvdata(&io->vdev, io);

	v4l2_info(&core->v4l2_dev, "Device registered as %s\n",
		  video_device_node_name(vdev));

	switch (pad) {
	case RISP_CORE_INPUT1:
	case RISP_CORE_PARAMS:
		ret = media_create_pad_link(&io->vdev.entity, 0,
					    &core->subdev.entity, pad,
					    MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE);
		break;
	case RISP_CORE_STATS:
	case RISP_CORE_OUTPUT1:
		ret = media_create_pad_link(&core->subdev.entity, pad,
					    &io->vdev.entity, 0,
					    MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE);
		break;
	}

	return ret;
}

void risp_core_io_destroy(struct rcar_isp_core_io *io)
{
	if (!video_is_registered(&io->vdev))
		return;

	vb2_video_unregister_device(&io->vdev);
}
