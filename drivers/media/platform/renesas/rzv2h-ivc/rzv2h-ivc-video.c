// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/V2H(P) Input Video Control Block driver
 *
 * Copyright (C) 2025 Ideas on Board Oy
 */

#include "rzv2h-ivc.h"

#include <linux/cleanup.h>
#include <linux/iopoll.h>
#include <linux/lockdep.h>
#include <linux/media-bus-format.h>
#include <linux/minmax.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>

#include <media/mipi-csi2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>

#define RZV2H_IVC_FIXED_HBLANK			0x20
#define RZV2H_IVC_MIN_VBLANK(hts)		max(0x1b, 15 + (120501 / (hts)))

struct rzv2h_ivc_buf {
	struct vb2_v4l2_buffer vb;
	struct list_head queue;
	dma_addr_t addr;
};

#define to_rzv2h_ivc_buf(vbuf) \
	container_of(vbuf, struct rzv2h_ivc_buf, vb)

static const struct rzv2h_ivc_format rzv2h_ivc_formats[] = {
	{
		.fourcc = V4L2_PIX_FMT_SBGGR8,
		.mbus_codes = {
			MEDIA_BUS_FMT_SBGGR8_1X8,
		},
		.dtype = MIPI_CSI2_DT_RAW8,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.mbus_codes = {
			MEDIA_BUS_FMT_SGBRG8_1X8,
		},
		.dtype = MIPI_CSI2_DT_RAW8,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG8,
		.mbus_codes = {
			MEDIA_BUS_FMT_SGRBG8_1X8,
		},
		.dtype = MIPI_CSI2_DT_RAW8,
	},
	{
		.fourcc = V4L2_PIX_FMT_SRGGB8,
		.mbus_codes = {
			MEDIA_BUS_FMT_SRGGB8_1X8,
		},
		.dtype = MIPI_CSI2_DT_RAW8,
	},
	{
		.fourcc = V4L2_PIX_FMT_RAW_CRU10,
		.mbus_codes = {
			MEDIA_BUS_FMT_SBGGR10_1X10,
			MEDIA_BUS_FMT_SGBRG10_1X10,
			MEDIA_BUS_FMT_SGRBG10_1X10,
			MEDIA_BUS_FMT_SRGGB10_1X10
		},
		.dtype = MIPI_CSI2_DT_RAW10,
	},
	{
		.fourcc = V4L2_PIX_FMT_RAW_CRU12,
		.mbus_codes = {
			MEDIA_BUS_FMT_SBGGR12_1X12,
			MEDIA_BUS_FMT_SGBRG12_1X12,
			MEDIA_BUS_FMT_SGRBG12_1X12,
			MEDIA_BUS_FMT_SRGGB12_1X12
		},
		.dtype = MIPI_CSI2_DT_RAW12,
	},
	{
		.fourcc = V4L2_PIX_FMT_RAW_CRU14,
		.mbus_codes = {
			MEDIA_BUS_FMT_SBGGR14_1X14,
			MEDIA_BUS_FMT_SGBRG14_1X14,
			MEDIA_BUS_FMT_SGRBG14_1X14,
			MEDIA_BUS_FMT_SRGGB14_1X14
		},
		.dtype = MIPI_CSI2_DT_RAW14,
	},
	{
		.fourcc = V4L2_PIX_FMT_SBGGR16,
		.mbus_codes = {
			MEDIA_BUS_FMT_SBGGR16_1X16,
		},
		.dtype = MIPI_CSI2_DT_RAW16,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG16,
		.mbus_codes = {
			MEDIA_BUS_FMT_SGBRG16_1X16,
		},
		.dtype = MIPI_CSI2_DT_RAW16,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG16,
		.mbus_codes = {
			MEDIA_BUS_FMT_SGRBG16_1X16,
		},
		.dtype = MIPI_CSI2_DT_RAW16,
	},
	{
		.fourcc = V4L2_PIX_FMT_SRGGB16,
		.mbus_codes = {
			MEDIA_BUS_FMT_SRGGB16_1X16,
		},
		.dtype = MIPI_CSI2_DT_RAW16,
	},
};

void rzv2h_ivc_buffer_done(struct rzv2h_ivc *ivc)
{
	struct rzv2h_ivc_buf *buf;

	lockdep_assert_in_irq();

	scoped_guard(spinlock, &ivc->buffers.lock) {
		if (!ivc->buffers.curr)
			return;

		buf = ivc->buffers.curr;
		ivc->buffers.curr = NULL;
	}

	buf->vb.sequence = ivc->buffers.sequence++;
	vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
}

static void rzv2h_ivc_transfer_buffer(struct work_struct *work)
{
	struct rzv2h_ivc *ivc = container_of(work, struct rzv2h_ivc,
					     buffers.work);
	struct rzv2h_ivc_buf *buf;

	/* Setup buffers */
	scoped_guard(spinlock_irqsave, &ivc->buffers.lock) {
		buf = list_first_entry_or_null(&ivc->buffers.queue,
					       struct rzv2h_ivc_buf, queue);
	}

	if (!buf)
		return;

	list_del(&buf->queue);

	ivc->buffers.curr = buf;
	buf->addr = vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 0);
	rzv2h_ivc_write(ivc, RZV2H_IVC_REG_AXIRX_SADDL_P0, buf->addr);

	scoped_guard(spinlock_irqsave, &ivc->spinlock) {
		ivc->vvalid_ifp = 2;
	}
	rzv2h_ivc_write(ivc, RZV2H_IVC_REG_FM_FRCON, 0x1);
}

static int rzv2h_ivc_queue_setup(struct vb2_queue *q, unsigned int *num_buffers,
				 unsigned int *num_planes, unsigned int sizes[],
				 struct device *alloc_devs[])
{
	struct rzv2h_ivc *ivc = vb2_get_drv_priv(q);

	if (*num_planes && *num_planes > 1)
		return -EINVAL;

	if (sizes[0] && sizes[0] < ivc->format.pix.plane_fmt[0].sizeimage)
		return -EINVAL;

	*num_planes = 1;

	if (!sizes[0])
		sizes[0] = ivc->format.pix.plane_fmt[0].sizeimage;

	return 0;
}

static void rzv2h_ivc_buf_queue(struct vb2_buffer *vb)
{
	struct rzv2h_ivc *ivc = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rzv2h_ivc_buf *buf = to_rzv2h_ivc_buf(vbuf);

	scoped_guard(spinlock_irq, &ivc->buffers.lock) {
		list_add_tail(&buf->queue, &ivc->buffers.queue);
	}

	scoped_guard(spinlock_irq, &ivc->spinlock) {
		if (vb2_is_streaming(vb->vb2_queue) && !ivc->vvalid_ifp)
			queue_work(ivc->buffers.async_wq, &ivc->buffers.work);
	}
}

static void rzv2h_ivc_format_configure(struct rzv2h_ivc *ivc)
{
	const struct rzv2h_ivc_format *fmt = ivc->format.fmt;
	struct v4l2_pix_format_mplane *pix = &ivc->format.pix;
	unsigned int vblank;
	unsigned int hts;

	/* Currently only CRU packed pixel formats are supported */
	rzv2h_ivc_write(ivc, RZV2H_IVC_REG_AXIRX_PXFMT,
			RZV2H_IVC_INPUT_FMT_CRU_PACKED);

	rzv2h_ivc_update_bits(ivc, RZV2H_IVC_REG_AXIRX_PXFMT,
			      RZV2H_IVC_PXFMT_DTYPE, fmt->dtype);

	rzv2h_ivc_write(ivc, RZV2H_IVC_REG_AXIRX_HSIZE, pix->width);
	rzv2h_ivc_write(ivc, RZV2H_IVC_REG_AXIRX_VSIZE, pix->height);
	rzv2h_ivc_write(ivc, RZV2H_IVC_REG_AXIRX_STRD,
			pix->plane_fmt[0].bytesperline);

	/*
	 * The ISP has minimum vertical blanking requirements that must be
	 * adhered to by the IVC. The minimum is a function of the Iridix blocks
	 * clocking requirements and the width of the image and horizontal
	 * blanking, but if we assume the worst case then it boils down to the
	 * below (plus one to the numerator to ensure the answer is rounded up)
	 */

	hts = pix->width + RZV2H_IVC_FIXED_HBLANK;
	vblank = RZV2H_IVC_MIN_VBLANK(hts);

	rzv2h_ivc_write(ivc, RZV2H_IVC_REG_AXIRX_BLANK,
			RZV2H_IVC_VBLANK(vblank));
}

static void rzv2h_ivc_return_buffers(struct rzv2h_ivc *ivc,
				     enum vb2_buffer_state state)
{
	struct rzv2h_ivc_buf *buf, *tmp;

	guard(spinlock_irqsave)(&ivc->buffers.lock);

	if (ivc->buffers.curr) {
		vb2_buffer_done(&ivc->buffers.curr->vb.vb2_buf, state);
		ivc->buffers.curr = NULL;
	}

	list_for_each_entry_safe(buf, tmp, &ivc->buffers.queue, queue) {
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
}

static int rzv2h_ivc_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct rzv2h_ivc *ivc = vb2_get_drv_priv(q);
	int ret;

	ivc->buffers.sequence = 0;
	ivc->vvalid_ifp = 0;

	ret = pm_runtime_resume_and_get(ivc->dev);
	if (ret)
		goto err_return_buffers;

	ret = video_device_pipeline_alloc_start(&ivc->vdev.dev);
	if (ret) {
		dev_err(ivc->dev, "failed to start media pipeline\n");
		goto err_pm_runtime_put;
	}

	rzv2h_ivc_format_configure(ivc);

	queue_work(ivc->buffers.async_wq, &ivc->buffers.work);

	return 0;

err_pm_runtime_put:
	pm_runtime_put(ivc->dev);
err_return_buffers:
	rzv2h_ivc_return_buffers(ivc, VB2_BUF_STATE_QUEUED);

	return ret;
}

static void rzv2h_ivc_stop_streaming(struct vb2_queue *q)
{
	struct rzv2h_ivc *ivc = vb2_get_drv_priv(q);
	u32 val = 0;

	rzv2h_ivc_write(ivc, RZV2H_IVC_REG_FM_STOP, 0x1);
	readl_poll_timeout(ivc->base + RZV2H_IVC_REG_FM_STOP,
			   val, !val, 10 * USEC_PER_MSEC, 250 * USEC_PER_MSEC);

	rzv2h_ivc_return_buffers(ivc, VB2_BUF_STATE_ERROR);
	video_device_pipeline_stop(&ivc->vdev.dev);
	pm_runtime_put_autosuspend(ivc->dev);
}

static const struct vb2_ops rzv2h_ivc_vb2_ops = {
	.queue_setup		= &rzv2h_ivc_queue_setup,
	.buf_queue		= &rzv2h_ivc_buf_queue,
	.start_streaming	= &rzv2h_ivc_start_streaming,
	.stop_streaming		= &rzv2h_ivc_stop_streaming,
};

static const struct rzv2h_ivc_format *
rzv2h_ivc_format_from_pixelformat(u32 fourcc)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(rzv2h_ivc_formats); i++)
		if (fourcc == rzv2h_ivc_formats[i].fourcc)
			return &rzv2h_ivc_formats[i];

	return &rzv2h_ivc_formats[0];
}

static int rzv2h_ivc_enum_fmt_vid_out(struct file *file, void *fh,
				      struct v4l2_fmtdesc *f)
{
	if (f->index >= ARRAY_SIZE(rzv2h_ivc_formats))
		return -EINVAL;

	f->pixelformat = rzv2h_ivc_formats[f->index].fourcc;
	return 0;
}

static int rzv2h_ivc_g_fmt_vid_out(struct file *file, void *fh,
				   struct v4l2_format *f)
{
	struct rzv2h_ivc *ivc = video_drvdata(file);

	f->fmt.pix_mp = ivc->format.pix;

	return 0;
}

static void rzv2h_ivc_try_fmt(struct v4l2_pix_format_mplane *pix,
			      const struct rzv2h_ivc_format *fmt)
{
	pix->pixelformat = fmt->fourcc;

	pix->width = clamp(pix->width, RZV2H_IVC_MIN_WIDTH,
			   RZV2H_IVC_MAX_WIDTH);
	pix->height = clamp(pix->height, RZV2H_IVC_MIN_HEIGHT,
			    RZV2H_IVC_MAX_HEIGHT);

	pix->field = V4L2_FIELD_NONE;
	pix->colorspace = V4L2_COLORSPACE_RAW;
	pix->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(pix->colorspace);
	pix->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
							  pix->colorspace,
							  pix->ycbcr_enc);

	v4l2_fill_pixfmt_mp(pix, pix->pixelformat, pix->width, pix->height);
}

static void rzv2h_ivc_set_format(struct rzv2h_ivc *ivc,
				 struct v4l2_pix_format_mplane *pix)
{
	const struct rzv2h_ivc_format *fmt;

	fmt = rzv2h_ivc_format_from_pixelformat(pix->pixelformat);

	rzv2h_ivc_try_fmt(pix, fmt);
	ivc->format.pix = *pix;
	ivc->format.fmt = fmt;
}

static int rzv2h_ivc_s_fmt_vid_out(struct file *file, void *fh,
				   struct v4l2_format *f)
{
	struct rzv2h_ivc *ivc = video_drvdata(file);
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;

	if (vb2_is_busy(&ivc->vdev.vb2q))
		return -EBUSY;

	rzv2h_ivc_set_format(ivc, pix);

	return 0;
}

static int rzv2h_ivc_try_fmt_vid_out(struct file *file, void *fh,
				     struct v4l2_format *f)
{
	const struct rzv2h_ivc_format *fmt;

	fmt = rzv2h_ivc_format_from_pixelformat(f->fmt.pix.pixelformat);
	rzv2h_ivc_try_fmt(&f->fmt.pix_mp, fmt);

	return 0;
}

static int rzv2h_ivc_querycap(struct file *file, void *fh,
			      struct v4l2_capability *cap)
{
	strscpy(cap->driver, "rzv2h-ivc", sizeof(cap->driver));
	strscpy(cap->card, "Renesas Input Video Control", sizeof(cap->card));

	return 0;
}

static const struct v4l2_ioctl_ops rzv2h_ivc_v4l2_ioctl_ops = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_enum_fmt_vid_out = rzv2h_ivc_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_out_mplane = rzv2h_ivc_g_fmt_vid_out,
	.vidioc_s_fmt_vid_out_mplane = rzv2h_ivc_s_fmt_vid_out,
	.vidioc_try_fmt_vid_out_mplane = rzv2h_ivc_try_fmt_vid_out,
	.vidioc_querycap = rzv2h_ivc_querycap,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct v4l2_file_operations rzv2h_ivc_v4l2_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
};

int rzv2h_ivc_init_vdev(struct rzv2h_ivc *ivc, struct v4l2_device *v4l2_dev)
{
	struct v4l2_pix_format_mplane pix = { };
	struct video_device *vdev;
	struct vb2_queue *vb2q;
	int ret;

	spin_lock_init(&ivc->buffers.lock);
	INIT_LIST_HEAD(&ivc->buffers.queue);
	INIT_WORK(&ivc->buffers.work, rzv2h_ivc_transfer_buffer);

	ivc->buffers.async_wq = alloc_workqueue("rzv2h-ivc", 0, 0);
	if (!ivc->buffers.async_wq)
		return -EINVAL;

	/* Initialise vb2 queue */
	vb2q = &ivc->vdev.vb2q;
	vb2q->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	vb2q->io_modes = VB2_MMAP | VB2_DMABUF;
	vb2q->drv_priv = ivc;
	vb2q->mem_ops = &vb2_dma_contig_memops;
	vb2q->ops = &rzv2h_ivc_vb2_ops;
	vb2q->buf_struct_size = sizeof(struct rzv2h_ivc_buf);
	vb2q->min_queued_buffers = 0;
	vb2q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	vb2q->lock = &ivc->lock;
	vb2q->dev = ivc->dev;

	ret = vb2_queue_init(vb2q);
	if (ret) {
		dev_err(ivc->dev, "vb2 queue init failed\n");
		goto err_destroy_workqueue;
	}

	/* Initialise Video Device */
	vdev = &ivc->vdev.dev;
	strscpy(vdev->name, "rzv2h-ivc", sizeof(vdev->name));
	vdev->release = video_device_release_empty;
	vdev->fops = &rzv2h_ivc_v4l2_fops;
	vdev->ioctl_ops = &rzv2h_ivc_v4l2_ioctl_ops;
	vdev->lock = &ivc->lock;
	vdev->v4l2_dev = v4l2_dev;
	vdev->queue = vb2q;
	vdev->device_caps = V4L2_CAP_VIDEO_OUTPUT_MPLANE | V4L2_CAP_STREAMING;
	vdev->vfl_dir = VFL_DIR_TX;
	video_set_drvdata(vdev, ivc);

	pix.pixelformat = V4L2_PIX_FMT_SRGGB16;
	pix.width = RZV2H_IVC_DEFAULT_WIDTH;
	pix.height = RZV2H_IVC_DEFAULT_HEIGHT;
	rzv2h_ivc_set_format(ivc, &pix);

	ivc->vdev.pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&ivc->vdev.dev.entity, 1, &ivc->vdev.pad);
	if (ret)
		goto err_release_vb2q;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(ivc->dev, "failed to register IVC video device\n");
		goto err_cleanup_vdev_entity;
	}

	ret = media_create_pad_link(&vdev->entity, 0, &ivc->subdev.sd.entity,
				    RZV2H_IVC_SUBDEV_SINK_PAD,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret) {
		dev_err(ivc->dev, "failed to create media link\n");
		goto err_unregister_vdev;
	}

	return 0;

err_unregister_vdev:
	video_unregister_device(vdev);
err_cleanup_vdev_entity:
	media_entity_cleanup(&vdev->entity);
err_release_vb2q:
	vb2_queue_release(vb2q);
err_destroy_workqueue:
	destroy_workqueue(ivc->buffers.async_wq);

	return ret;
}

void rzv2h_deinit_video_dev_and_queue(struct rzv2h_ivc *ivc)
{
	struct video_device *vdev = &ivc->vdev.dev;
	struct vb2_queue *vb2q = &ivc->vdev.vb2q;

	vb2_video_unregister_device(vdev);
	media_entity_cleanup(&vdev->entity);
	vb2_queue_release(vb2q);
}
