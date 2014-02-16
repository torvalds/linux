/*
 * V4L2 Driver for SuperH Mobile CEU interface
 *
 * Copyright (C) 2008 Magnus Damm
 *
 * Based on V4L2 Driver for PXA camera host - "pxa_camera.c",
 *
 * Copyright (C) 2006, Sascha Hauer, Pengutronix
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <linux/pm_runtime.h>
#include <linux/sched.h>

#include <media/v4l2-async.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/soc_camera.h>
#include <media/sh_mobile_ceu.h>
#include <media/sh_mobile_csi2.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-mediabus.h>
#include <media/soc_mediabus.h>

#include "soc_scale_crop.h"

/* register offsets for sh7722 / sh7723 */

#define CAPSR  0x00 /* Capture start register */
#define CAPCR  0x04 /* Capture control register */
#define CAMCR  0x08 /* Capture interface control register */
#define CMCYR  0x0c /* Capture interface cycle  register */
#define CAMOR  0x10 /* Capture interface offset register */
#define CAPWR  0x14 /* Capture interface width register */
#define CAIFR  0x18 /* Capture interface input format register */
#define CSTCR  0x20 /* Camera strobe control register (<= sh7722) */
#define CSECR  0x24 /* Camera strobe emission count register (<= sh7722) */
#define CRCNTR 0x28 /* CEU register control register */
#define CRCMPR 0x2c /* CEU register forcible control register */
#define CFLCR  0x30 /* Capture filter control register */
#define CFSZR  0x34 /* Capture filter size clip register */
#define CDWDR  0x38 /* Capture destination width register */
#define CDAYR  0x3c /* Capture data address Y register */
#define CDACR  0x40 /* Capture data address C register */
#define CDBYR  0x44 /* Capture data bottom-field address Y register */
#define CDBCR  0x48 /* Capture data bottom-field address C register */
#define CBDSR  0x4c /* Capture bundle destination size register */
#define CFWCR  0x5c /* Firewall operation control register */
#define CLFCR  0x60 /* Capture low-pass filter control register */
#define CDOCR  0x64 /* Capture data output control register */
#define CDDCR  0x68 /* Capture data complexity level register */
#define CDDAR  0x6c /* Capture data complexity level address register */
#define CEIER  0x70 /* Capture event interrupt enable register */
#define CETCR  0x74 /* Capture event flag clear register */
#define CSTSR  0x7c /* Capture status register */
#define CSRTR  0x80 /* Capture software reset register */
#define CDSSR  0x84 /* Capture data size register */
#define CDAYR2 0x90 /* Capture data address Y register 2 */
#define CDACR2 0x94 /* Capture data address C register 2 */
#define CDBYR2 0x98 /* Capture data bottom-field address Y register 2 */
#define CDBCR2 0x9c /* Capture data bottom-field address C register 2 */

#undef DEBUG_GEOMETRY
#ifdef DEBUG_GEOMETRY
#define dev_geo	dev_info
#else
#define dev_geo	dev_dbg
#endif

/* per video frame buffer */
struct sh_mobile_ceu_buffer {
	struct vb2_buffer vb; /* v4l buffer must be first */
	struct list_head queue;
};

struct sh_mobile_ceu_dev {
	struct soc_camera_host ici;
	/* Asynchronous CSI2 linking */
	struct v4l2_async_subdev *csi2_asd;
	struct v4l2_subdev *csi2_sd;
	/* Synchronous probing compatibility */
	struct platform_device *csi2_pdev;

	unsigned int irq;
	void __iomem *base;
	size_t video_limit;
	size_t buf_total;

	spinlock_t lock;		/* Protects video buffer lists */
	struct list_head capture;
	struct vb2_buffer *active;
	struct vb2_alloc_ctx *alloc_ctx;

	struct sh_mobile_ceu_info *pdata;
	struct completion complete;

	u32 cflcr;

	/* static max sizes either from platform data or default */
	int max_width;
	int max_height;

	enum v4l2_field field;
	int sequence;
	unsigned long flags;

	unsigned int image_mode:1;
	unsigned int is_16bit:1;
	unsigned int frozen:1;
};

struct sh_mobile_ceu_cam {
	/* CEU offsets within the camera output, before the CEU scaler */
	unsigned int ceu_left;
	unsigned int ceu_top;
	/* Client output, as seen by the CEU */
	unsigned int width;
	unsigned int height;
	/*
	 * User window from S_CROP / G_CROP, produced by client cropping and
	 * scaling, CEU scaling and CEU cropping, mapped back onto the client
	 * input window
	 */
	struct v4l2_rect subrect;
	/* Camera cropping rectangle */
	struct v4l2_rect rect;
	const struct soc_mbus_pixelfmt *extra_fmt;
	enum v4l2_mbus_pixelcode code;
};

static struct sh_mobile_ceu_buffer *to_ceu_vb(struct vb2_buffer *vb)
{
	return container_of(vb, struct sh_mobile_ceu_buffer, vb);
}

static void ceu_write(struct sh_mobile_ceu_dev *priv,
		      unsigned long reg_offs, u32 data)
{
	iowrite32(data, priv->base + reg_offs);
}

static u32 ceu_read(struct sh_mobile_ceu_dev *priv, unsigned long reg_offs)
{
	return ioread32(priv->base + reg_offs);
}

static int sh_mobile_ceu_soft_reset(struct sh_mobile_ceu_dev *pcdev)
{
	int i, success = 0;

	ceu_write(pcdev, CAPSR, 1 << 16); /* reset */

	/* wait CSTSR.CPTON bit */
	for (i = 0; i < 1000; i++) {
		if (!(ceu_read(pcdev, CSTSR) & 1)) {
			success++;
			break;
		}
		udelay(1);
	}

	/* wait CAPSR.CPKIL bit */
	for (i = 0; i < 1000; i++) {
		if (!(ceu_read(pcdev, CAPSR) & (1 << 16))) {
			success++;
			break;
		}
		udelay(1);
	}

	if (2 != success) {
		dev_warn(pcdev->ici.v4l2_dev.dev, "soft reset time out\n");
		return -EIO;
	}

	return 0;
}

/*
 *  Videobuf operations
 */

/*
 * .queue_setup() is called to check, whether the driver can accept the
 *		  requested number of buffers and to fill in plane sizes
 *		  for the current frame format if required
 */
static int sh_mobile_ceu_videobuf_setup(struct vb2_queue *vq,
			const struct v4l2_format *fmt,
			unsigned int *count, unsigned int *num_planes,
			unsigned int sizes[], void *alloc_ctxs[])
{
	struct soc_camera_device *icd = container_of(vq, struct soc_camera_device, vb2_vidq);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;

	if (fmt) {
		const struct soc_camera_format_xlate *xlate = soc_camera_xlate_by_fourcc(icd,
								fmt->fmt.pix.pixelformat);
		unsigned int bytes_per_line;
		int ret;

		if (!xlate)
			return -EINVAL;

		ret = soc_mbus_bytes_per_line(fmt->fmt.pix.width,
					      xlate->host_fmt);
		if (ret < 0)
			return ret;

		bytes_per_line = max_t(u32, fmt->fmt.pix.bytesperline, ret);

		ret = soc_mbus_image_size(xlate->host_fmt, bytes_per_line,
					  fmt->fmt.pix.height);
		if (ret < 0)
			return ret;

		sizes[0] = max_t(u32, fmt->fmt.pix.sizeimage, ret);
	} else {
		/* Called from VIDIOC_REQBUFS or in compatibility mode */
		sizes[0] = icd->sizeimage;
	}

	alloc_ctxs[0] = pcdev->alloc_ctx;

	if (!vq->num_buffers)
		pcdev->sequence = 0;

	if (!*count)
		*count = 2;

	/* If *num_planes != 0, we have already verified *count. */
	if (pcdev->video_limit && !*num_planes) {
		size_t size = PAGE_ALIGN(sizes[0]) * *count;

		if (size + pcdev->buf_total > pcdev->video_limit)
			*count = (pcdev->video_limit - pcdev->buf_total) /
				PAGE_ALIGN(sizes[0]);
	}

	*num_planes = 1;

	dev_dbg(icd->parent, "count=%d, size=%u\n", *count, sizes[0]);

	return 0;
}

#define CEU_CETCR_MAGIC 0x0317f313 /* acknowledge magical interrupt sources */
#define CEU_CETCR_IGRW (1 << 4) /* prohibited register access interrupt bit */
#define CEU_CEIER_CPEIE (1 << 0) /* one-frame capture end interrupt */
#define CEU_CEIER_VBP   (1 << 20) /* vbp error */
#define CEU_CAPCR_CTNCP (1 << 16) /* continuous capture mode (if set) */
#define CEU_CEIER_MASK (CEU_CEIER_CPEIE | CEU_CEIER_VBP)


/*
 * return value doesn't reflex the success/failure to queue the new buffer,
 * but rather the status of the previous buffer.
 */
static int sh_mobile_ceu_capture(struct sh_mobile_ceu_dev *pcdev)
{
	struct soc_camera_device *icd = pcdev->ici.icd;
	dma_addr_t phys_addr_top, phys_addr_bottom;
	unsigned long top1, top2;
	unsigned long bottom1, bottom2;
	u32 status;
	bool planar;
	int ret = 0;

	/*
	 * The hardware is _very_ picky about this sequence. Especially
	 * the CEU_CETCR_MAGIC value. It seems like we need to acknowledge
	 * several not-so-well documented interrupt sources in CETCR.
	 */
	ceu_write(pcdev, CEIER, ceu_read(pcdev, CEIER) & ~CEU_CEIER_MASK);
	status = ceu_read(pcdev, CETCR);
	ceu_write(pcdev, CETCR, ~status & CEU_CETCR_MAGIC);
	if (!pcdev->frozen)
		ceu_write(pcdev, CEIER, ceu_read(pcdev, CEIER) | CEU_CEIER_MASK);
	ceu_write(pcdev, CAPCR, ceu_read(pcdev, CAPCR) & ~CEU_CAPCR_CTNCP);
	ceu_write(pcdev, CETCR, CEU_CETCR_MAGIC ^ CEU_CETCR_IGRW);

	/*
	 * When a VBP interrupt occurs, a capture end interrupt does not occur
	 * and the image of that frame is not captured correctly. So, soft reset
	 * is needed here.
	 */
	if (status & CEU_CEIER_VBP) {
		sh_mobile_ceu_soft_reset(pcdev);
		ret = -EIO;
	}

	if (pcdev->frozen) {
		complete(&pcdev->complete);
		return ret;
	}

	if (!pcdev->active)
		return ret;

	if (V4L2_FIELD_INTERLACED_BT == pcdev->field) {
		top1	= CDBYR;
		top2	= CDBCR;
		bottom1	= CDAYR;
		bottom2	= CDACR;
	} else {
		top1	= CDAYR;
		top2	= CDACR;
		bottom1	= CDBYR;
		bottom2	= CDBCR;
	}

	phys_addr_top = vb2_dma_contig_plane_dma_addr(pcdev->active, 0);

	switch (icd->current_fmt->host_fmt->fourcc) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		planar = true;
		break;
	default:
		planar = false;
	}

	ceu_write(pcdev, top1, phys_addr_top);
	if (V4L2_FIELD_NONE != pcdev->field) {
		phys_addr_bottom = phys_addr_top + icd->bytesperline;
		ceu_write(pcdev, bottom1, phys_addr_bottom);
	}

	if (planar) {
		phys_addr_top += icd->bytesperline * icd->user_height;
		ceu_write(pcdev, top2, phys_addr_top);
		if (V4L2_FIELD_NONE != pcdev->field) {
			phys_addr_bottom = phys_addr_top + icd->bytesperline;
			ceu_write(pcdev, bottom2, phys_addr_bottom);
		}
	}

	ceu_write(pcdev, CAPSR, 0x1); /* start capture */

	return ret;
}

static int sh_mobile_ceu_videobuf_prepare(struct vb2_buffer *vb)
{
	struct sh_mobile_ceu_buffer *buf = to_ceu_vb(vb);

	/* Added list head initialization on alloc */
	WARN(!list_empty(&buf->queue), "Buffer %p on queue!\n", vb);

	return 0;
}

static void sh_mobile_ceu_videobuf_queue(struct vb2_buffer *vb)
{
	struct soc_camera_device *icd = container_of(vb->vb2_queue, struct soc_camera_device, vb2_vidq);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	struct sh_mobile_ceu_buffer *buf = to_ceu_vb(vb);
	unsigned long size;

	size = icd->sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(icd->parent, "Buffer #%d too small (%lu < %lu)\n",
			vb->v4l2_buf.index, vb2_plane_size(vb, 0), size);
		goto error;
	}

	vb2_set_plane_payload(vb, 0, size);

	dev_dbg(icd->parent, "%s (vb=0x%p) 0x%p %lu\n", __func__,
		vb, vb2_plane_vaddr(vb, 0), vb2_get_plane_payload(vb, 0));

#ifdef DEBUG
	/*
	 * This can be useful if you want to see if we actually fill
	 * the buffer with something
	 */
	if (vb2_plane_vaddr(vb, 0))
		memset(vb2_plane_vaddr(vb, 0), 0xaa, vb2_get_plane_payload(vb, 0));
#endif

	spin_lock_irq(&pcdev->lock);
	list_add_tail(&buf->queue, &pcdev->capture);

	if (!pcdev->active) {
		/*
		 * Because there were no active buffer at this moment,
		 * we are not interested in the return value of
		 * sh_mobile_ceu_capture here.
		 */
		pcdev->active = vb;
		sh_mobile_ceu_capture(pcdev);
	}
	spin_unlock_irq(&pcdev->lock);

	return;

error:
	vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
}

static void sh_mobile_ceu_videobuf_release(struct vb2_buffer *vb)
{
	struct soc_camera_device *icd = container_of(vb->vb2_queue, struct soc_camera_device, vb2_vidq);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct sh_mobile_ceu_buffer *buf = to_ceu_vb(vb);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;

	spin_lock_irq(&pcdev->lock);

	if (pcdev->active == vb) {
		/* disable capture (release DMA buffer), reset */
		ceu_write(pcdev, CAPSR, 1 << 16);
		pcdev->active = NULL;
	}

	/*
	 * Doesn't hurt also if the list is empty, but it hurts, if queuing the
	 * buffer failed, and .buf_init() hasn't been called
	 */
	if (buf->queue.next)
		list_del_init(&buf->queue);

	pcdev->buf_total -= PAGE_ALIGN(vb2_plane_size(vb, 0));
	dev_dbg(icd->parent, "%s() %zu bytes buffers\n", __func__,
		pcdev->buf_total);

	spin_unlock_irq(&pcdev->lock);
}

static int sh_mobile_ceu_videobuf_init(struct vb2_buffer *vb)
{
	struct soc_camera_device *icd = container_of(vb->vb2_queue, struct soc_camera_device, vb2_vidq);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;

	pcdev->buf_total += PAGE_ALIGN(vb2_plane_size(vb, 0));
	dev_dbg(icd->parent, "%s() %zu bytes buffers\n", __func__,
		pcdev->buf_total);

	/* This is for locking debugging only */
	INIT_LIST_HEAD(&to_ceu_vb(vb)->queue);
	return 0;
}

static int sh_mobile_ceu_stop_streaming(struct vb2_queue *q)
{
	struct soc_camera_device *icd = container_of(q, struct soc_camera_device, vb2_vidq);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	struct list_head *buf_head, *tmp;

	spin_lock_irq(&pcdev->lock);

	pcdev->active = NULL;

	list_for_each_safe(buf_head, tmp, &pcdev->capture)
		list_del_init(buf_head);

	spin_unlock_irq(&pcdev->lock);

	return sh_mobile_ceu_soft_reset(pcdev);
}

static struct vb2_ops sh_mobile_ceu_videobuf_ops = {
	.queue_setup	= sh_mobile_ceu_videobuf_setup,
	.buf_prepare	= sh_mobile_ceu_videobuf_prepare,
	.buf_queue	= sh_mobile_ceu_videobuf_queue,
	.buf_cleanup	= sh_mobile_ceu_videobuf_release,
	.buf_init	= sh_mobile_ceu_videobuf_init,
	.wait_prepare	= soc_camera_unlock,
	.wait_finish	= soc_camera_lock,
	.stop_streaming	= sh_mobile_ceu_stop_streaming,
};

static irqreturn_t sh_mobile_ceu_irq(int irq, void *data)
{
	struct sh_mobile_ceu_dev *pcdev = data;
	struct vb2_buffer *vb;
	int ret;

	spin_lock(&pcdev->lock);

	vb = pcdev->active;
	if (!vb)
		/* Stale interrupt from a released buffer */
		goto out;

	list_del_init(&to_ceu_vb(vb)->queue);

	if (!list_empty(&pcdev->capture))
		pcdev->active = &list_entry(pcdev->capture.next,
					    struct sh_mobile_ceu_buffer, queue)->vb;
	else
		pcdev->active = NULL;

	ret = sh_mobile_ceu_capture(pcdev);
	v4l2_get_timestamp(&vb->v4l2_buf.timestamp);
	if (!ret) {
		vb->v4l2_buf.field = pcdev->field;
		vb->v4l2_buf.sequence = pcdev->sequence++;
	}
	vb2_buffer_done(vb, ret < 0 ? VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);

out:
	spin_unlock(&pcdev->lock);

	return IRQ_HANDLED;
}

static struct v4l2_subdev *find_csi2(struct sh_mobile_ceu_dev *pcdev)
{
	struct v4l2_subdev *sd;

	if (pcdev->csi2_sd)
		return pcdev->csi2_sd;

	if (pcdev->csi2_asd) {
		char name[] = "sh-mobile-csi2";
		v4l2_device_for_each_subdev(sd, &pcdev->ici.v4l2_dev)
			if (!strncmp(name, sd->name, sizeof(name) - 1)) {
				pcdev->csi2_sd = sd;
				return sd;
			}
	}

	return NULL;
}

static struct v4l2_subdev *csi2_subdev(struct sh_mobile_ceu_dev *pcdev,
				       struct soc_camera_device *icd)
{
	struct v4l2_subdev *sd = pcdev->csi2_sd;

	return sd && sd->grp_id == soc_camera_grp_id(icd) ? sd : NULL;
}

static int sh_mobile_ceu_add_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	struct v4l2_subdev *csi2_sd = find_csi2(pcdev);
	int ret;

	if (csi2_sd) {
		csi2_sd->grp_id = soc_camera_grp_id(icd);
		v4l2_set_subdev_hostdata(csi2_sd, icd);
	}

	ret = v4l2_subdev_call(csi2_sd, core, s_power, 1);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
		return ret;

	/*
	 * -ENODEV is special: either csi2_sd == NULL or the CSI-2 driver
	 * has not found this soc-camera device among its clients
	 */
	if (csi2_sd && ret == -ENODEV)
		csi2_sd->grp_id = 0;

	dev_info(icd->parent,
		 "SuperH Mobile CEU%s driver attached to camera %d\n",
		 csi2_sd && csi2_sd->grp_id ? "/CSI-2" : "", icd->devnum);

	return 0;
}

static void sh_mobile_ceu_remove_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	struct v4l2_subdev *csi2_sd = find_csi2(pcdev);

	dev_info(icd->parent,
		 "SuperH Mobile CEU driver detached from camera %d\n",
		 icd->devnum);

	v4l2_subdev_call(csi2_sd, core, s_power, 0);
}

/* Called with .host_lock held */
static int sh_mobile_ceu_clock_start(struct soc_camera_host *ici)
{
	struct sh_mobile_ceu_dev *pcdev = ici->priv;

	pm_runtime_get_sync(ici->v4l2_dev.dev);

	pcdev->buf_total = 0;

	sh_mobile_ceu_soft_reset(pcdev);

	return 0;
}

/* Called with .host_lock held */
static void sh_mobile_ceu_clock_stop(struct soc_camera_host *ici)
{
	struct sh_mobile_ceu_dev *pcdev = ici->priv;

	/* disable capture, disable interrupts */
	ceu_write(pcdev, CEIER, 0);
	sh_mobile_ceu_soft_reset(pcdev);

	/* make sure active buffer is canceled */
	spin_lock_irq(&pcdev->lock);
	if (pcdev->active) {
		list_del_init(&to_ceu_vb(pcdev->active)->queue);
		vb2_buffer_done(pcdev->active, VB2_BUF_STATE_ERROR);
		pcdev->active = NULL;
	}
	spin_unlock_irq(&pcdev->lock);

	pm_runtime_put(ici->v4l2_dev.dev);
}

/*
 * See chapter 29.4.12 "Capture Filter Control Register (CFLCR)"
 * in SH7722 Hardware Manual
 */
static unsigned int size_dst(unsigned int src, unsigned int scale)
{
	unsigned int mant_pre = scale >> 12;
	if (!src || !scale)
		return src;
	return ((mant_pre + 2 * (src - 1)) / (2 * mant_pre) - 1) *
		mant_pre * 4096 / scale + 1;
}

static u16 calc_scale(unsigned int src, unsigned int *dst)
{
	u16 scale;

	if (src == *dst)
		return 0;

	scale = (src * 4096 / *dst) & ~7;

	while (scale > 4096 && size_dst(src, scale) < *dst)
		scale -= 8;

	*dst = size_dst(src, scale);

	return scale;
}

/* rect is guaranteed to not exceed the scaled camera rectangle */
static void sh_mobile_ceu_set_rect(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct sh_mobile_ceu_cam *cam = icd->host_priv;
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	unsigned int height, width, cdwdr_width, in_width, in_height;
	unsigned int left_offset, top_offset;
	u32 camor;

	dev_geo(icd->parent, "Crop %ux%u@%u:%u\n",
		icd->user_width, icd->user_height, cam->ceu_left, cam->ceu_top);

	left_offset	= cam->ceu_left;
	top_offset	= cam->ceu_top;

	WARN_ON(icd->user_width & 3 || icd->user_height & 3);

	width = icd->user_width;

	if (pcdev->image_mode) {
		in_width = cam->width;
		if (!pcdev->is_16bit) {
			in_width *= 2;
			left_offset *= 2;
		}
	} else {
		unsigned int w_factor;

		switch (icd->current_fmt->host_fmt->packing) {
		case SOC_MBUS_PACKING_2X8_PADHI:
			w_factor = 2;
			break;
		default:
			w_factor = 1;
		}

		in_width = cam->width * w_factor;
		left_offset *= w_factor;
	}

	cdwdr_width = icd->bytesperline;

	height = icd->user_height;
	in_height = cam->height;
	if (V4L2_FIELD_NONE != pcdev->field) {
		height = (height / 2) & ~3;
		in_height /= 2;
		top_offset /= 2;
		cdwdr_width *= 2;
	}

	/* CSI2 special configuration */
	if (csi2_subdev(pcdev, icd)) {
		in_width = ((in_width - 2) * 2);
		left_offset *= 2;
	}

	/* Set CAMOR, CAPWR, CFSZR, take care of CDWDR */
	camor = left_offset | (top_offset << 16);

	dev_geo(icd->parent,
		"CAMOR 0x%x, CAPWR 0x%x, CFSZR 0x%x, CDWDR 0x%x\n", camor,
		(in_height << 16) | in_width, (height << 16) | width,
		cdwdr_width);

	ceu_write(pcdev, CAMOR, camor);
	ceu_write(pcdev, CAPWR, (in_height << 16) | in_width);
	/* CFSZR clipping is applied _after_ the scaling filter (CFLCR) */
	ceu_write(pcdev, CFSZR, (height << 16) | width);
	ceu_write(pcdev, CDWDR, cdwdr_width);
}

static u32 capture_save_reset(struct sh_mobile_ceu_dev *pcdev)
{
	u32 capsr = ceu_read(pcdev, CAPSR);
	ceu_write(pcdev, CAPSR, 1 << 16); /* reset, stop capture */
	return capsr;
}

static void capture_restore(struct sh_mobile_ceu_dev *pcdev, u32 capsr)
{
	unsigned long timeout = jiffies + 10 * HZ;

	/*
	 * Wait until the end of the current frame. It can take a long time,
	 * but if it has been aborted by a CAPSR reset, it shoule exit sooner.
	 */
	while ((ceu_read(pcdev, CSTSR) & 1) && time_before(jiffies, timeout))
		msleep(1);

	if (time_after(jiffies, timeout)) {
		dev_err(pcdev->ici.v4l2_dev.dev,
			"Timeout waiting for frame end! Interface problem?\n");
		return;
	}

	/* Wait until reset clears, this shall not hang... */
	while (ceu_read(pcdev, CAPSR) & (1 << 16))
		udelay(10);

	/* Anything to restore? */
	if (capsr & ~(1 << 16))
		ceu_write(pcdev, CAPSR, capsr);
}

/* Find the bus subdevice driver, e.g., CSI2 */
static struct v4l2_subdev *find_bus_subdev(struct sh_mobile_ceu_dev *pcdev,
					   struct soc_camera_device *icd)
{
	return csi2_subdev(pcdev, icd) ? : soc_camera_to_subdev(icd);
}

#define CEU_BUS_FLAGS (V4L2_MBUS_MASTER |	\
		V4L2_MBUS_PCLK_SAMPLE_RISING |	\
		V4L2_MBUS_HSYNC_ACTIVE_HIGH |	\
		V4L2_MBUS_HSYNC_ACTIVE_LOW |	\
		V4L2_MBUS_VSYNC_ACTIVE_HIGH |	\
		V4L2_MBUS_VSYNC_ACTIVE_LOW |	\
		V4L2_MBUS_DATA_ACTIVE_HIGH)

/* Capture is not running, no interrupts, no locking needed */
static int sh_mobile_ceu_set_bus_param(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	struct v4l2_subdev *sd = find_bus_subdev(pcdev, icd);
	struct sh_mobile_ceu_cam *cam = icd->host_priv;
	struct v4l2_mbus_config cfg = {.type = V4L2_MBUS_PARALLEL,};
	unsigned long value, common_flags = CEU_BUS_FLAGS;
	u32 capsr = capture_save_reset(pcdev);
	unsigned int yuv_lineskip;
	int ret;

	/*
	 * If the client doesn't implement g_mbus_config, we just use our
	 * platform data
	 */
	ret = v4l2_subdev_call(sd, video, g_mbus_config, &cfg);
	if (!ret) {
		common_flags = soc_mbus_config_compatible(&cfg,
							  common_flags);
		if (!common_flags)
			return -EINVAL;
	} else if (ret != -ENOIOCTLCMD) {
		return ret;
	}

	/* Make choises, based on platform preferences */
	if ((common_flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH) &&
	    (common_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)) {
		if (pcdev->flags & SH_CEU_FLAG_HSYNC_LOW)
			common_flags &= ~V4L2_MBUS_HSYNC_ACTIVE_HIGH;
		else
			common_flags &= ~V4L2_MBUS_HSYNC_ACTIVE_LOW;
	}

	if ((common_flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH) &&
	    (common_flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)) {
		if (pcdev->flags & SH_CEU_FLAG_VSYNC_LOW)
			common_flags &= ~V4L2_MBUS_VSYNC_ACTIVE_HIGH;
		else
			common_flags &= ~V4L2_MBUS_VSYNC_ACTIVE_LOW;
	}

	cfg.flags = common_flags;
	ret = v4l2_subdev_call(sd, video, s_mbus_config, &cfg);
	if (ret < 0 && ret != -ENOIOCTLCMD)
		return ret;

	if (icd->current_fmt->host_fmt->bits_per_sample > 8)
		pcdev->is_16bit = 1;
	else
		pcdev->is_16bit = 0;

	ceu_write(pcdev, CRCNTR, 0);
	ceu_write(pcdev, CRCMPR, 0);

	value = 0x00000010; /* data fetch by default */
	yuv_lineskip = 0x10;

	switch (icd->current_fmt->host_fmt->fourcc) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		/* convert 4:2:2 -> 4:2:0 */
		yuv_lineskip = 0; /* skip for NV12/21, no skip for NV16/61 */
		/* fall-through */
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		switch (cam->code) {
		case V4L2_MBUS_FMT_UYVY8_2X8:
			value = 0x00000000; /* Cb0, Y0, Cr0, Y1 */
			break;
		case V4L2_MBUS_FMT_VYUY8_2X8:
			value = 0x00000100; /* Cr0, Y0, Cb0, Y1 */
			break;
		case V4L2_MBUS_FMT_YUYV8_2X8:
			value = 0x00000200; /* Y0, Cb0, Y1, Cr0 */
			break;
		case V4L2_MBUS_FMT_YVYU8_2X8:
			value = 0x00000300; /* Y0, Cr0, Y1, Cb0 */
			break;
		default:
			BUG();
		}
	}

	if (icd->current_fmt->host_fmt->fourcc == V4L2_PIX_FMT_NV21 ||
	    icd->current_fmt->host_fmt->fourcc == V4L2_PIX_FMT_NV61)
		value ^= 0x00000100; /* swap U, V to change from NV1x->NVx1 */

	value |= common_flags & V4L2_MBUS_VSYNC_ACTIVE_LOW ? 1 << 1 : 0;
	value |= common_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW ? 1 << 0 : 0;

	if (csi2_subdev(pcdev, icd)) /* CSI2 mode */
		value |= 3 << 12;
	else if (pcdev->is_16bit)
		value |= 1 << 12;
	else if (pcdev->flags & SH_CEU_FLAG_LOWER_8BIT)
		value |= 2 << 12;

	ceu_write(pcdev, CAMCR, value);

	ceu_write(pcdev, CAPCR, 0x00300000);

	switch (pcdev->field) {
	case V4L2_FIELD_INTERLACED_TB:
		value = 0x101;
		break;
	case V4L2_FIELD_INTERLACED_BT:
		value = 0x102;
		break;
	default:
		value = 0;
		break;
	}
	ceu_write(pcdev, CAIFR, value);

	sh_mobile_ceu_set_rect(icd);
	mdelay(1);

	dev_geo(icd->parent, "CFLCR 0x%x\n", pcdev->cflcr);
	ceu_write(pcdev, CFLCR, pcdev->cflcr);

	/*
	 * A few words about byte order (observed in Big Endian mode)
	 *
	 * In data fetch mode bytes are received in chunks of 8 bytes.
	 * D0, D1, D2, D3, D4, D5, D6, D7 (D0 received first)
	 *
	 * The data is however by default written to memory in reverse order:
	 * D7, D6, D5, D4, D3, D2, D1, D0 (D7 written to lowest byte)
	 *
	 * The lowest three bits of CDOCR allows us to do swapping,
	 * using 7 we swap the data bytes to match the incoming order:
	 * D0, D1, D2, D3, D4, D5, D6, D7
	 */
	value = 0x00000007 | yuv_lineskip;

	ceu_write(pcdev, CDOCR, value);
	ceu_write(pcdev, CFWCR, 0); /* keep "datafetch firewall" disabled */

	capture_restore(pcdev, capsr);

	/* not in bundle mode: skip CBDSR, CDAYR2, CDACR2, CDBYR2, CDBCR2 */
	return 0;
}

static int sh_mobile_ceu_try_bus_param(struct soc_camera_device *icd,
				       unsigned char buswidth)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	struct v4l2_subdev *sd = find_bus_subdev(pcdev, icd);
	unsigned long common_flags = CEU_BUS_FLAGS;
	struct v4l2_mbus_config cfg = {.type = V4L2_MBUS_PARALLEL,};
	int ret;

	ret = v4l2_subdev_call(sd, video, g_mbus_config, &cfg);
	if (!ret)
		common_flags = soc_mbus_config_compatible(&cfg,
							  common_flags);
	else if (ret != -ENOIOCTLCMD)
		return ret;

	if (!common_flags || buswidth > 16)
		return -EINVAL;

	return 0;
}

static const struct soc_mbus_pixelfmt sh_mobile_ceu_formats[] = {
	{
		.fourcc			= V4L2_PIX_FMT_NV12,
		.name			= "NV12",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_1_5X8,
		.order			= SOC_MBUS_ORDER_LE,
		.layout			= SOC_MBUS_LAYOUT_PLANAR_2Y_C,
	}, {
		.fourcc			= V4L2_PIX_FMT_NV21,
		.name			= "NV21",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_1_5X8,
		.order			= SOC_MBUS_ORDER_LE,
		.layout			= SOC_MBUS_LAYOUT_PLANAR_2Y_C,
	}, {
		.fourcc			= V4L2_PIX_FMT_NV16,
		.name			= "NV16",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_LE,
		.layout			= SOC_MBUS_LAYOUT_PLANAR_Y_C,
	}, {
		.fourcc			= V4L2_PIX_FMT_NV61,
		.name			= "NV61",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_LE,
		.layout			= SOC_MBUS_LAYOUT_PLANAR_Y_C,
	},
};

/* This will be corrected as we get more formats */
static bool sh_mobile_ceu_packing_supported(const struct soc_mbus_pixelfmt *fmt)
{
	return	fmt->packing == SOC_MBUS_PACKING_NONE ||
		(fmt->bits_per_sample == 8 &&
		 fmt->packing == SOC_MBUS_PACKING_1_5X8) ||
		(fmt->bits_per_sample == 8 &&
		 fmt->packing == SOC_MBUS_PACKING_2X8_PADHI) ||
		(fmt->bits_per_sample > 8 &&
		 fmt->packing == SOC_MBUS_PACKING_EXTEND16);
}

static struct soc_camera_device *ctrl_to_icd(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct soc_camera_device,
							ctrl_handler);
}

static int sh_mobile_ceu_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct soc_camera_device *icd = ctrl_to_icd(ctrl);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;

	switch (ctrl->id) {
	case V4L2_CID_SHARPNESS:
		switch (icd->current_fmt->host_fmt->fourcc) {
		case V4L2_PIX_FMT_NV12:
		case V4L2_PIX_FMT_NV21:
		case V4L2_PIX_FMT_NV16:
		case V4L2_PIX_FMT_NV61:
			ceu_write(pcdev, CLFCR, !ctrl->val);
			return 0;
		}
		break;
	}

	return -EINVAL;
}

static const struct v4l2_ctrl_ops sh_mobile_ceu_ctrl_ops = {
	.s_ctrl = sh_mobile_ceu_s_ctrl,
};

static int sh_mobile_ceu_get_formats(struct soc_camera_device *icd, unsigned int idx,
				     struct soc_camera_format_xlate *xlate)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct device *dev = icd->parent;
	struct soc_camera_host *ici = to_soc_camera_host(dev);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	int ret, k, n;
	int formats = 0;
	struct sh_mobile_ceu_cam *cam;
	enum v4l2_mbus_pixelcode code;
	const struct soc_mbus_pixelfmt *fmt;

	ret = v4l2_subdev_call(sd, video, enum_mbus_fmt, idx, &code);
	if (ret < 0)
		/* No more formats */
		return 0;

	fmt = soc_mbus_get_fmtdesc(code);
	if (!fmt) {
		dev_warn(dev, "unsupported format code #%u: %d\n", idx, code);
		return 0;
	}

	if (!csi2_subdev(pcdev, icd)) {
		/* Are there any restrictions in the CSI-2 case? */
		ret = sh_mobile_ceu_try_bus_param(icd, fmt->bits_per_sample);
		if (ret < 0)
			return 0;
	}

	if (!icd->host_priv) {
		struct v4l2_mbus_framefmt mf;
		struct v4l2_rect rect;
		int shift = 0;

		/* Add our control */
		v4l2_ctrl_new_std(&icd->ctrl_handler, &sh_mobile_ceu_ctrl_ops,
				  V4L2_CID_SHARPNESS, 0, 1, 1, 1);
		if (icd->ctrl_handler.error)
			return icd->ctrl_handler.error;

		/* FIXME: subwindow is lost between close / open */

		/* Cache current client geometry */
		ret = soc_camera_client_g_rect(sd, &rect);
		if (ret < 0)
			return ret;

		/* First time */
		ret = v4l2_subdev_call(sd, video, g_mbus_fmt, &mf);
		if (ret < 0)
			return ret;

		/*
		 * All currently existing CEU implementations support 2560x1920
		 * or larger frames. If the sensor is proposing too big a frame,
		 * don't bother with possibly supportred by the CEU larger
		 * sizes, just try VGA multiples. If needed, this can be
		 * adjusted in the future.
		 */
		while ((mf.width > pcdev->max_width ||
			mf.height > pcdev->max_height) && shift < 4) {
			/* Try 2560x1920, 1280x960, 640x480, 320x240 */
			mf.width	= 2560 >> shift;
			mf.height	= 1920 >> shift;
			ret = v4l2_device_call_until_err(sd->v4l2_dev,
					soc_camera_grp_id(icd), video,
					s_mbus_fmt, &mf);
			if (ret < 0)
				return ret;
			shift++;
		}

		if (shift == 4) {
			dev_err(dev, "Failed to configure the client below %ux%x\n",
				mf.width, mf.height);
			return -EIO;
		}

		dev_geo(dev, "camera fmt %ux%u\n", mf.width, mf.height);

		cam = kzalloc(sizeof(*cam), GFP_KERNEL);
		if (!cam)
			return -ENOMEM;

		/* We are called with current camera crop, initialise subrect with it */
		cam->rect	= rect;
		cam->subrect	= rect;

		cam->width	= mf.width;
		cam->height	= mf.height;

		icd->host_priv = cam;
	} else {
		cam = icd->host_priv;
	}

	/* Beginning of a pass */
	if (!idx)
		cam->extra_fmt = NULL;

	switch (code) {
	case V4L2_MBUS_FMT_UYVY8_2X8:
	case V4L2_MBUS_FMT_VYUY8_2X8:
	case V4L2_MBUS_FMT_YUYV8_2X8:
	case V4L2_MBUS_FMT_YVYU8_2X8:
		if (cam->extra_fmt)
			break;

		/*
		 * Our case is simple so far: for any of the above four camera
		 * formats we add all our four synthesized NV* formats, so,
		 * just marking the device with a single flag suffices. If
		 * the format generation rules are more complex, you would have
		 * to actually hang your already added / counted formats onto
		 * the host_priv pointer and check whether the format you're
		 * going to add now is already there.
		 */
		cam->extra_fmt = sh_mobile_ceu_formats;

		n = ARRAY_SIZE(sh_mobile_ceu_formats);
		formats += n;
		for (k = 0; xlate && k < n; k++) {
			xlate->host_fmt	= &sh_mobile_ceu_formats[k];
			xlate->code	= code;
			xlate++;
			dev_dbg(dev, "Providing format %s using code %d\n",
				sh_mobile_ceu_formats[k].name, code);
		}
		break;
	default:
		if (!sh_mobile_ceu_packing_supported(fmt))
			return 0;
	}

	/* Generic pass-through */
	formats++;
	if (xlate) {
		xlate->host_fmt	= fmt;
		xlate->code	= code;
		xlate++;
		dev_dbg(dev, "Providing format %s in pass-through mode\n",
			fmt->name);
	}

	return formats;
}

static void sh_mobile_ceu_put_formats(struct soc_camera_device *icd)
{
	kfree(icd->host_priv);
	icd->host_priv = NULL;
}

#define scale_down(size, scale) soc_camera_shift_scale(size, 12, scale)
#define calc_generic_scale(in, out) soc_camera_calc_scale(in, 12, out)

/*
 * CEU can scale and crop, but we don't want to waste bandwidth and kill the
 * framerate by always requesting the maximum image from the client. See
 * Documentation/video4linux/sh_mobile_ceu_camera.txt for a description of
 * scaling and cropping algorithms and for the meaning of referenced here steps.
 */
static int sh_mobile_ceu_set_crop(struct soc_camera_device *icd,
				  const struct v4l2_crop *a)
{
	struct v4l2_crop a_writable = *a;
	const struct v4l2_rect *rect = &a_writable.c;
	struct device *dev = icd->parent;
	struct soc_camera_host *ici = to_soc_camera_host(dev);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	struct v4l2_crop cam_crop;
	struct sh_mobile_ceu_cam *cam = icd->host_priv;
	struct v4l2_rect *cam_rect = &cam_crop.c;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_mbus_framefmt mf;
	unsigned int scale_cam_h, scale_cam_v, scale_ceu_h, scale_ceu_v,
		out_width, out_height;
	int interm_width, interm_height;
	u32 capsr, cflcr;
	int ret;

	dev_geo(dev, "S_CROP(%ux%u@%u:%u)\n", rect->width, rect->height,
		rect->left, rect->top);

	/* During camera cropping its output window can change too, stop CEU */
	capsr = capture_save_reset(pcdev);
	dev_dbg(dev, "CAPSR 0x%x, CFLCR 0x%x\n", capsr, pcdev->cflcr);

	/*
	 * 1. - 2. Apply iterative camera S_CROP for new input window, read back
	 * actual camera rectangle.
	 */
	ret = soc_camera_client_s_crop(sd, &a_writable, &cam_crop,
				       &cam->rect, &cam->subrect);
	if (ret < 0)
		return ret;

	dev_geo(dev, "1-2: camera cropped to %ux%u@%u:%u\n",
		cam_rect->width, cam_rect->height,
		cam_rect->left, cam_rect->top);

	/* On success cam_crop contains current camera crop */

	/* 3. Retrieve camera output window */
	ret = v4l2_subdev_call(sd, video, g_mbus_fmt, &mf);
	if (ret < 0)
		return ret;

	if (mf.width > pcdev->max_width || mf.height > pcdev->max_height)
		return -EINVAL;

	/* 4. Calculate camera scales */
	scale_cam_h	= calc_generic_scale(cam_rect->width, mf.width);
	scale_cam_v	= calc_generic_scale(cam_rect->height, mf.height);

	/* Calculate intermediate window */
	interm_width	= scale_down(rect->width, scale_cam_h);
	interm_height	= scale_down(rect->height, scale_cam_v);

	if (interm_width < icd->user_width) {
		u32 new_scale_h;

		new_scale_h = calc_generic_scale(rect->width, icd->user_width);

		mf.width = scale_down(cam_rect->width, new_scale_h);
	}

	if (interm_height < icd->user_height) {
		u32 new_scale_v;

		new_scale_v = calc_generic_scale(rect->height, icd->user_height);

		mf.height = scale_down(cam_rect->height, new_scale_v);
	}

	if (interm_width < icd->user_width || interm_height < icd->user_height) {
		ret = v4l2_device_call_until_err(sd->v4l2_dev,
					soc_camera_grp_id(icd), video,
					s_mbus_fmt, &mf);
		if (ret < 0)
			return ret;

		dev_geo(dev, "New camera output %ux%u\n", mf.width, mf.height);
		scale_cam_h	= calc_generic_scale(cam_rect->width, mf.width);
		scale_cam_v	= calc_generic_scale(cam_rect->height, mf.height);
		interm_width	= scale_down(rect->width, scale_cam_h);
		interm_height	= scale_down(rect->height, scale_cam_v);
	}

	/* Cache camera output window */
	cam->width	= mf.width;
	cam->height	= mf.height;

	if (pcdev->image_mode) {
		out_width	= min(interm_width, icd->user_width);
		out_height	= min(interm_height, icd->user_height);
	} else {
		out_width	= interm_width;
		out_height	= interm_height;
	}

	/*
	 * 5. Calculate CEU scales from camera scales from results of (5) and
	 *    the user window
	 */
	scale_ceu_h	= calc_scale(interm_width, &out_width);
	scale_ceu_v	= calc_scale(interm_height, &out_height);

	dev_geo(dev, "5: CEU scales %u:%u\n", scale_ceu_h, scale_ceu_v);

	/* Apply CEU scales. */
	cflcr = scale_ceu_h | (scale_ceu_v << 16);
	if (cflcr != pcdev->cflcr) {
		pcdev->cflcr = cflcr;
		ceu_write(pcdev, CFLCR, cflcr);
	}

	icd->user_width	 = out_width & ~3;
	icd->user_height = out_height & ~3;
	/* Offsets are applied at the CEU scaling filter input */
	cam->ceu_left	 = scale_down(rect->left - cam_rect->left, scale_cam_h) & ~1;
	cam->ceu_top	 = scale_down(rect->top - cam_rect->top, scale_cam_v) & ~1;

	/* 6. Use CEU cropping to crop to the new window. */
	sh_mobile_ceu_set_rect(icd);

	cam->subrect = *rect;

	dev_geo(dev, "6: CEU cropped to %ux%u@%u:%u\n",
		icd->user_width, icd->user_height,
		cam->ceu_left, cam->ceu_top);

	/* Restore capture. The CE bit can be cleared by the hardware */
	if (pcdev->active)
		capsr |= 1;
	capture_restore(pcdev, capsr);

	/* Even if only camera cropping succeeded */
	return ret;
}

static int sh_mobile_ceu_get_crop(struct soc_camera_device *icd,
				  struct v4l2_crop *a)
{
	struct sh_mobile_ceu_cam *cam = icd->host_priv;

	a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->c = cam->subrect;

	return 0;
}

/* Similar to set_crop multistage iterative algorithm */
static int sh_mobile_ceu_set_fmt(struct soc_camera_device *icd,
				 struct v4l2_format *f)
{
	struct device *dev = icd->parent;
	struct soc_camera_host *ici = to_soc_camera_host(dev);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	struct sh_mobile_ceu_cam *cam = icd->host_priv;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	__u32 pixfmt = pix->pixelformat;
	const struct soc_camera_format_xlate *xlate;
	unsigned int ceu_sub_width = pcdev->max_width,
		ceu_sub_height = pcdev->max_height;
	u16 scale_v, scale_h;
	int ret;
	bool image_mode;
	enum v4l2_field field;

	switch (pix->field) {
	default:
		pix->field = V4L2_FIELD_NONE;
		/* fall-through */
	case V4L2_FIELD_INTERLACED_TB:
	case V4L2_FIELD_INTERLACED_BT:
	case V4L2_FIELD_NONE:
		field = pix->field;
		break;
	case V4L2_FIELD_INTERLACED:
		field = V4L2_FIELD_INTERLACED_TB;
		break;
	}

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (!xlate) {
		dev_warn(dev, "Format %x not found\n", pixfmt);
		return -EINVAL;
	}

	/* 1.-4. Calculate desired client output geometry */
	soc_camera_calc_client_output(icd, &cam->rect, &cam->subrect, pix, &mf, 12);
	mf.field	= pix->field;
	mf.colorspace	= pix->colorspace;
	mf.code		= xlate->code;

	switch (pixfmt) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		image_mode = true;
		break;
	default:
		image_mode = false;
	}

	dev_geo(dev, "S_FMT(pix=0x%x, fld 0x%x, code 0x%x, %ux%u)\n", pixfmt, mf.field, mf.code,
		pix->width, pix->height);

	dev_geo(dev, "4: request camera output %ux%u\n", mf.width, mf.height);

	/* 5. - 9. */
	ret = soc_camera_client_scale(icd, &cam->rect, &cam->subrect,
				&mf, &ceu_sub_width, &ceu_sub_height,
				image_mode && V4L2_FIELD_NONE == field, 12);

	dev_geo(dev, "5-9: client scale return %d\n", ret);

	/* Done with the camera. Now see if we can improve the result */

	dev_geo(dev, "fmt %ux%u, requested %ux%u\n",
		mf.width, mf.height, pix->width, pix->height);
	if (ret < 0)
		return ret;

	if (mf.code != xlate->code)
		return -EINVAL;

	/* 9. Prepare CEU crop */
	cam->width = mf.width;
	cam->height = mf.height;

	/* 10. Use CEU scaling to scale to the requested user window. */

	/* We cannot scale up */
	if (pix->width > ceu_sub_width)
		ceu_sub_width = pix->width;

	if (pix->height > ceu_sub_height)
		ceu_sub_height = pix->height;

	pix->colorspace = mf.colorspace;

	if (image_mode) {
		/* Scale pix->{width x height} down to width x height */
		scale_h		= calc_scale(ceu_sub_width, &pix->width);
		scale_v		= calc_scale(ceu_sub_height, &pix->height);
	} else {
		pix->width	= ceu_sub_width;
		pix->height	= ceu_sub_height;
		scale_h		= 0;
		scale_v		= 0;
	}

	pcdev->cflcr = scale_h | (scale_v << 16);

	/*
	 * We have calculated CFLCR, the actual configuration will be performed
	 * in sh_mobile_ceu_set_bus_param()
	 */

	dev_geo(dev, "10: W: %u : 0x%x = %u, H: %u : 0x%x = %u\n",
		ceu_sub_width, scale_h, pix->width,
		ceu_sub_height, scale_v, pix->height);

	cam->code		= xlate->code;
	icd->current_fmt	= xlate;

	pcdev->field = field;
	pcdev->image_mode = image_mode;

	/* CFSZR requirement */
	pix->width	&= ~3;
	pix->height	&= ~3;

	return 0;
}

#define CEU_CHDW_MAX	8188U	/* Maximum line stride */

static int sh_mobile_ceu_try_fmt(struct soc_camera_device *icd,
				 struct v4l2_format *f)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_mbus_framefmt mf;
	__u32 pixfmt = pix->pixelformat;
	int width, height;
	int ret;

	dev_geo(icd->parent, "TRY_FMT(pix=0x%x, %ux%u)\n",
		 pixfmt, pix->width, pix->height);

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (!xlate) {
		xlate = icd->current_fmt;
		dev_dbg(icd->parent, "Format %x not found, keeping %x\n",
			pixfmt, xlate->host_fmt->fourcc);
		pixfmt = xlate->host_fmt->fourcc;
		pix->pixelformat = pixfmt;
		pix->colorspace = icd->colorspace;
	}

	/* FIXME: calculate using depth and bus width */

	/* CFSZR requires height and width to be 4-pixel aligned */
	v4l_bound_align_image(&pix->width, 2, pcdev->max_width, 2,
			      &pix->height, 4, pcdev->max_height, 2, 0);

	width = pix->width;
	height = pix->height;

	/* limit to sensor capabilities */
	mf.width	= pix->width;
	mf.height	= pix->height;
	mf.field	= pix->field;
	mf.code		= xlate->code;
	mf.colorspace	= pix->colorspace;

	ret = v4l2_device_call_until_err(sd->v4l2_dev, soc_camera_grp_id(icd),
					 video, try_mbus_fmt, &mf);
	if (ret < 0)
		return ret;

	pix->width	= mf.width;
	pix->height	= mf.height;
	pix->field	= mf.field;
	pix->colorspace	= mf.colorspace;

	switch (pixfmt) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		/* FIXME: check against rect_max after converting soc-camera */
		/* We can scale precisely, need a bigger image from camera */
		if (pix->width < width || pix->height < height) {
			/*
			 * We presume, the sensor behaves sanely, i.e., if
			 * requested a bigger rectangle, it will not return a
			 * smaller one.
			 */
			mf.width = pcdev->max_width;
			mf.height = pcdev->max_height;
			ret = v4l2_device_call_until_err(sd->v4l2_dev,
					soc_camera_grp_id(icd), video,
					try_mbus_fmt, &mf);
			if (ret < 0) {
				/* Shouldn't actually happen... */
				dev_err(icd->parent,
					"FIXME: client try_fmt() = %d\n", ret);
				return ret;
			}
		}
		/* We will scale exactly */
		if (mf.width > width)
			pix->width = width;
		if (mf.height > height)
			pix->height = height;

		pix->bytesperline = max(pix->bytesperline, pix->width);
		pix->bytesperline = min(pix->bytesperline, CEU_CHDW_MAX);
		pix->bytesperline &= ~3;
		break;

	default:
		/* Configurable stride isn't supported in pass-through mode. */
		pix->bytesperline  = 0;
	}

	pix->width	&= ~3;
	pix->height	&= ~3;
	pix->sizeimage	= 0;

	dev_geo(icd->parent, "%s(): return %d, fmt 0x%x, %ux%u\n",
		__func__, ret, pix->pixelformat, pix->width, pix->height);

	return ret;
}

static int sh_mobile_ceu_set_livecrop(struct soc_camera_device *icd,
				      const struct v4l2_crop *a)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	u32 out_width = icd->user_width, out_height = icd->user_height;
	int ret;

	/* Freeze queue */
	pcdev->frozen = 1;
	/* Wait for frame */
	ret = wait_for_completion_interruptible(&pcdev->complete);
	/* Stop the client */
	ret = v4l2_subdev_call(sd, video, s_stream, 0);
	if (ret < 0)
		dev_warn(icd->parent,
			 "Client failed to stop the stream: %d\n", ret);
	else
		/* Do the crop, if it fails, there's nothing more we can do */
		sh_mobile_ceu_set_crop(icd, a);

	dev_geo(icd->parent, "Output after crop: %ux%u\n", icd->user_width, icd->user_height);

	if (icd->user_width != out_width || icd->user_height != out_height) {
		struct v4l2_format f = {
			.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE,
			.fmt.pix	= {
				.width		= out_width,
				.height		= out_height,
				.pixelformat	= icd->current_fmt->host_fmt->fourcc,
				.field		= pcdev->field,
				.colorspace	= icd->colorspace,
			},
		};
		ret = sh_mobile_ceu_set_fmt(icd, &f);
		if (!ret && (out_width != f.fmt.pix.width ||
			     out_height != f.fmt.pix.height))
			ret = -EINVAL;
		if (!ret) {
			icd->user_width		= out_width & ~3;
			icd->user_height	= out_height & ~3;
			ret = sh_mobile_ceu_set_bus_param(icd);
		}
	}

	/* Thaw the queue */
	pcdev->frozen = 0;
	spin_lock_irq(&pcdev->lock);
	sh_mobile_ceu_capture(pcdev);
	spin_unlock_irq(&pcdev->lock);
	/* Start the client */
	ret = v4l2_subdev_call(sd, video, s_stream, 1);
	return ret;
}

static unsigned int sh_mobile_ceu_poll(struct file *file, poll_table *pt)
{
	struct soc_camera_device *icd = file->private_data;

	return vb2_poll(&icd->vb2_vidq, file, pt);
}

static int sh_mobile_ceu_querycap(struct soc_camera_host *ici,
				  struct v4l2_capability *cap)
{
	strlcpy(cap->card, "SuperH_Mobile_CEU", sizeof(cap->card));
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	return 0;
}

static int sh_mobile_ceu_init_videobuf(struct vb2_queue *q,
				       struct soc_camera_device *icd)
{
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->drv_priv = icd;
	q->ops = &sh_mobile_ceu_videobuf_ops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct sh_mobile_ceu_buffer);
	q->timestamp_type = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;

	return vb2_queue_init(q);
}

static struct soc_camera_host_ops sh_mobile_ceu_host_ops = {
	.owner		= THIS_MODULE,
	.add		= sh_mobile_ceu_add_device,
	.remove		= sh_mobile_ceu_remove_device,
	.clock_start	= sh_mobile_ceu_clock_start,
	.clock_stop	= sh_mobile_ceu_clock_stop,
	.get_formats	= sh_mobile_ceu_get_formats,
	.put_formats	= sh_mobile_ceu_put_formats,
	.get_crop	= sh_mobile_ceu_get_crop,
	.set_crop	= sh_mobile_ceu_set_crop,
	.set_livecrop	= sh_mobile_ceu_set_livecrop,
	.set_fmt	= sh_mobile_ceu_set_fmt,
	.try_fmt	= sh_mobile_ceu_try_fmt,
	.poll		= sh_mobile_ceu_poll,
	.querycap	= sh_mobile_ceu_querycap,
	.set_bus_param	= sh_mobile_ceu_set_bus_param,
	.init_videobuf2	= sh_mobile_ceu_init_videobuf,
};

struct bus_wait {
	struct notifier_block	notifier;
	struct completion	completion;
	struct device		*dev;
};

static int bus_notify(struct notifier_block *nb,
		      unsigned long action, void *data)
{
	struct device *dev = data;
	struct bus_wait *wait = container_of(nb, struct bus_wait, notifier);

	if (wait->dev != dev)
		return NOTIFY_DONE;

	switch (action) {
	case BUS_NOTIFY_UNBOUND_DRIVER:
		/* Protect from module unloading */
		wait_for_completion(&wait->completion);
		return NOTIFY_OK;
	}
	return NOTIFY_DONE;
}

static int sh_mobile_ceu_probe(struct platform_device *pdev)
{
	struct sh_mobile_ceu_dev *pcdev;
	struct resource *res;
	void __iomem *base;
	unsigned int irq;
	int err, i;
	struct bus_wait wait = {
		.completion = COMPLETION_INITIALIZER_ONSTACK(wait.completion),
		.notifier.notifier_call = bus_notify,
	};
	struct sh_mobile_ceu_companion *csi2;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!res || (int)irq <= 0) {
		dev_err(&pdev->dev, "Not enough CEU platform resources.\n");
		return -ENODEV;
	}

	pcdev = devm_kzalloc(&pdev->dev, sizeof(*pcdev), GFP_KERNEL);
	if (!pcdev) {
		dev_err(&pdev->dev, "Could not allocate pcdev\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&pcdev->capture);
	spin_lock_init(&pcdev->lock);
	init_completion(&pcdev->complete);

	pcdev->pdata = pdev->dev.platform_data;
	if (!pcdev->pdata && !pdev->dev.of_node) {
		dev_err(&pdev->dev, "CEU platform data not set.\n");
		return -EINVAL;
	}

	/* TODO: implement per-device bus flags */
	if (pcdev->pdata) {
		pcdev->max_width = pcdev->pdata->max_width;
		pcdev->max_height = pcdev->pdata->max_height;
		pcdev->flags = pcdev->pdata->flags;
	}

	if (!pcdev->max_width) {
		unsigned int v;
		err = of_property_read_u32(pdev->dev.of_node, "renesas,max-width", &v);
		if (!err)
			pcdev->max_width = v;

		if (!pcdev->max_width)
			pcdev->max_width = 2560;
	}
	if (!pcdev->max_height) {
		unsigned int v;
		err = of_property_read_u32(pdev->dev.of_node, "renesas,max-height", &v);
		if (!err)
			pcdev->max_height = v;

		if (!pcdev->max_height)
			pcdev->max_height = 1920;
	}

	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	pcdev->irq = irq;
	pcdev->base = base;
	pcdev->video_limit = 0; /* only enabled if second resource exists */

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res) {
		err = dma_declare_coherent_memory(&pdev->dev, res->start,
						  res->start,
						  resource_size(res),
						  DMA_MEMORY_MAP |
						  DMA_MEMORY_EXCLUSIVE);
		if (!err) {
			dev_err(&pdev->dev, "Unable to declare CEU memory.\n");
			return -ENXIO;
		}

		pcdev->video_limit = resource_size(res);
	}

	/* request irq */
	err = devm_request_irq(&pdev->dev, pcdev->irq, sh_mobile_ceu_irq,
			       0, dev_name(&pdev->dev), pcdev);
	if (err) {
		dev_err(&pdev->dev, "Unable to register CEU interrupt.\n");
		goto exit_release_mem;
	}

	pm_suspend_ignore_children(&pdev->dev, true);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_resume(&pdev->dev);

	pcdev->ici.priv = pcdev;
	pcdev->ici.v4l2_dev.dev = &pdev->dev;
	pcdev->ici.nr = pdev->id;
	pcdev->ici.drv_name = dev_name(&pdev->dev);
	pcdev->ici.ops = &sh_mobile_ceu_host_ops;
	pcdev->ici.capabilities = SOCAM_HOST_CAP_STRIDE;

	pcdev->alloc_ctx = vb2_dma_contig_init_ctx(&pdev->dev);
	if (IS_ERR(pcdev->alloc_ctx)) {
		err = PTR_ERR(pcdev->alloc_ctx);
		goto exit_free_clk;
	}

	if (pcdev->pdata && pcdev->pdata->asd_sizes) {
		struct v4l2_async_subdev **asd;
		char name[] = "sh-mobile-csi2";
		int j;

		/*
		 * CSI2 interfacing: several groups can use CSI2, pick up the
		 * first one
		 */
		asd = pcdev->pdata->asd;
		for (j = 0; pcdev->pdata->asd_sizes[j]; j++) {
			for (i = 0; i < pcdev->pdata->asd_sizes[j]; i++, asd++) {
				dev_dbg(&pdev->dev, "%s(): subdev #%d, type %u\n",
					__func__, i, (*asd)->match_type);
				if ((*asd)->match_type == V4L2_ASYNC_MATCH_DEVNAME &&
				    !strncmp(name, (*asd)->match.device_name.name,
					     sizeof(name) - 1)) {
					pcdev->csi2_asd = *asd;
					break;
				}
			}
			if (pcdev->csi2_asd)
				break;
		}

		pcdev->ici.asd = pcdev->pdata->asd;
		pcdev->ici.asd_sizes = pcdev->pdata->asd_sizes;
	}

	/* Legacy CSI2 interfacing */
	csi2 = pcdev->pdata ? pcdev->pdata->csi2 : NULL;
	if (csi2) {
		/*
		 * TODO: remove this once all users are converted to
		 * asynchronous CSI2 probing. If it has to be kept, csi2
		 * platform device resources have to be added, using
		 * platform_device_add_resources()
		 */
		struct platform_device *csi2_pdev =
			platform_device_alloc("sh-mobile-csi2", csi2->id);
		struct sh_csi2_pdata *csi2_pdata = csi2->platform_data;

		if (!csi2_pdev) {
			err = -ENOMEM;
			goto exit_free_ctx;
		}

		pcdev->csi2_pdev		= csi2_pdev;

		err = platform_device_add_data(csi2_pdev, csi2_pdata,
					       sizeof(*csi2_pdata));
		if (err < 0)
			goto exit_pdev_put;

		csi2_pdev->resource		= csi2->resource;
		csi2_pdev->num_resources	= csi2->num_resources;

		err = platform_device_add(csi2_pdev);
		if (err < 0)
			goto exit_pdev_put;

		wait.dev = &csi2_pdev->dev;

		err = bus_register_notifier(&platform_bus_type, &wait.notifier);
		if (err < 0)
			goto exit_pdev_unregister;

		/*
		 * From this point the driver module will not unload, until
		 * we complete the completion.
		 */

		if (!csi2_pdev->dev.driver) {
			complete(&wait.completion);
			/* Either too late, or probing failed */
			bus_unregister_notifier(&platform_bus_type, &wait.notifier);
			err = -ENXIO;
			goto exit_pdev_unregister;
		}

		/*
		 * The module is still loaded, in the worst case it is hanging
		 * in device release on our completion. So, _now_ dereferencing
		 * the "owner" is safe!
		 */

		err = try_module_get(csi2_pdev->dev.driver->owner);

		/* Let notifier complete, if it has been locked */
		complete(&wait.completion);
		bus_unregister_notifier(&platform_bus_type, &wait.notifier);
		if (!err) {
			err = -ENODEV;
			goto exit_pdev_unregister;
		}

		pcdev->csi2_sd = platform_get_drvdata(csi2_pdev);
	}

	err = soc_camera_host_register(&pcdev->ici);
	if (err)
		goto exit_csi2_unregister;

	if (csi2) {
		err = v4l2_device_register_subdev(&pcdev->ici.v4l2_dev,
						  pcdev->csi2_sd);
		dev_dbg(&pdev->dev, "%s(): ret(register_subdev) = %d\n",
			__func__, err);
		if (err < 0)
			goto exit_host_unregister;
		/* v4l2_device_register_subdev() took a reference too */
		module_put(pcdev->csi2_sd->owner);
	}

	return 0;

exit_host_unregister:
	soc_camera_host_unregister(&pcdev->ici);
exit_csi2_unregister:
	if (csi2) {
		module_put(pcdev->csi2_pdev->dev.driver->owner);
exit_pdev_unregister:
		platform_device_del(pcdev->csi2_pdev);
exit_pdev_put:
		pcdev->csi2_pdev->resource = NULL;
		platform_device_put(pcdev->csi2_pdev);
	}
exit_free_ctx:
	vb2_dma_contig_cleanup_ctx(pcdev->alloc_ctx);
exit_free_clk:
	pm_runtime_disable(&pdev->dev);
exit_release_mem:
	if (platform_get_resource(pdev, IORESOURCE_MEM, 1))
		dma_release_declared_memory(&pdev->dev);
	return err;
}

static int sh_mobile_ceu_remove(struct platform_device *pdev)
{
	struct soc_camera_host *soc_host = to_soc_camera_host(&pdev->dev);
	struct sh_mobile_ceu_dev *pcdev = container_of(soc_host,
					struct sh_mobile_ceu_dev, ici);
	struct platform_device *csi2_pdev = pcdev->csi2_pdev;

	soc_camera_host_unregister(soc_host);
	pm_runtime_disable(&pdev->dev);
	if (platform_get_resource(pdev, IORESOURCE_MEM, 1))
		dma_release_declared_memory(&pdev->dev);
	vb2_dma_contig_cleanup_ctx(pcdev->alloc_ctx);
	if (csi2_pdev && csi2_pdev->dev.driver) {
		struct module *csi2_drv = csi2_pdev->dev.driver->owner;
		platform_device_del(csi2_pdev);
		csi2_pdev->resource = NULL;
		platform_device_put(csi2_pdev);
		module_put(csi2_drv);
	}

	return 0;
}

static int sh_mobile_ceu_runtime_nop(struct device *dev)
{
	/* Runtime PM callback shared between ->runtime_suspend()
	 * and ->runtime_resume(). Simply returns success.
	 *
	 * This driver re-initializes all registers after
	 * pm_runtime_get_sync() anyway so there is no need
	 * to save and restore registers here.
	 */
	return 0;
}

static const struct dev_pm_ops sh_mobile_ceu_dev_pm_ops = {
	.runtime_suspend = sh_mobile_ceu_runtime_nop,
	.runtime_resume = sh_mobile_ceu_runtime_nop,
};

static const struct of_device_id sh_mobile_ceu_of_match[] = {
	{ .compatible = "renesas,sh-mobile-ceu" },
	{ }
};
MODULE_DEVICE_TABLE(of, sh_mobile_ceu_of_match);

static struct platform_driver sh_mobile_ceu_driver = {
	.driver		= {
		.name	= "sh_mobile_ceu",
		.owner	= THIS_MODULE,
		.pm	= &sh_mobile_ceu_dev_pm_ops,
		.of_match_table = sh_mobile_ceu_of_match,
	},
	.probe		= sh_mobile_ceu_probe,
	.remove		= sh_mobile_ceu_remove,
};

static int __init sh_mobile_ceu_init(void)
{
	/* Whatever return code */
	request_module("sh_mobile_csi2");
	return platform_driver_register(&sh_mobile_ceu_driver);
}

static void __exit sh_mobile_ceu_exit(void)
{
	platform_driver_unregister(&sh_mobile_ceu_driver);
}

module_init(sh_mobile_ceu_init);
module_exit(sh_mobile_ceu_exit);

MODULE_DESCRIPTION("SuperH Mobile CEU driver");
MODULE_AUTHOR("Magnus Damm");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1.0");
MODULE_ALIAS("platform:sh_mobile_ceu");
