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
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/time.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <linux/pm_runtime.h>

#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/soc_camera.h>
#include <media/sh_mobile_ceu.h>
#include <media/videobuf-dma-contig.h>

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
	struct videobuf_buffer vb; /* v4l buffer must be first */
	const struct soc_camera_data_format *fmt;
};

struct sh_mobile_ceu_dev {
	struct soc_camera_host ici;
	struct soc_camera_device *icd;

	unsigned int irq;
	void __iomem *base;
	unsigned long video_limit;

	/* lock used to protect videobuf */
	spinlock_t lock;
	struct list_head capture;
	struct videobuf_buffer *active;

	struct sh_mobile_ceu_info *pdata;

	u32 cflcr;

	unsigned int is_interlaced:1;
	unsigned int image_mode:1;
	unsigned int is_16bit:1;
};

struct sh_mobile_ceu_cam {
	struct v4l2_rect ceu_rect;
	unsigned int cam_width;
	unsigned int cam_height;
	const struct soc_camera_data_format *extra_fmt;
	const struct soc_camera_data_format *camera_fmt;
};

static unsigned long make_bus_param(struct sh_mobile_ceu_dev *pcdev)
{
	unsigned long flags;

	flags = SOCAM_MASTER |
		SOCAM_PCLK_SAMPLE_RISING |
		SOCAM_HSYNC_ACTIVE_HIGH |
		SOCAM_HSYNC_ACTIVE_LOW |
		SOCAM_VSYNC_ACTIVE_HIGH |
		SOCAM_VSYNC_ACTIVE_LOW |
		SOCAM_DATA_ACTIVE_HIGH;

	if (pcdev->pdata->flags & SH_CEU_FLAG_USE_8BIT_BUS)
		flags |= SOCAM_DATAWIDTH_8;

	if (pcdev->pdata->flags & SH_CEU_FLAG_USE_16BIT_BUS)
		flags |= SOCAM_DATAWIDTH_16;

	if (flags & SOCAM_DATAWIDTH_MASK)
		return flags;

	return 0;
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

/*
 *  Videobuf operations
 */
static int sh_mobile_ceu_videobuf_setup(struct videobuf_queue *vq,
					unsigned int *count,
					unsigned int *size)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	int bytes_per_pixel = (icd->current_fmt->depth + 7) >> 3;

	*size = PAGE_ALIGN(icd->user_width * icd->user_height *
			   bytes_per_pixel);

	if (0 == *count)
		*count = 2;

	if (pcdev->video_limit) {
		while (*size * *count > pcdev->video_limit)
			(*count)--;
	}

	dev_dbg(icd->dev.parent, "count=%d, size=%d\n", *count, *size);

	return 0;
}

static void free_buffer(struct videobuf_queue *vq,
			struct sh_mobile_ceu_buffer *buf)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct device *dev = icd->dev.parent;

	dev_dbg(dev, "%s (vb=0x%p) 0x%08lx %zd\n", __func__,
		&buf->vb, buf->vb.baddr, buf->vb.bsize);

	if (in_interrupt())
		BUG();

	videobuf_waiton(&buf->vb, 0, 0);
	videobuf_dma_contig_free(vq, &buf->vb);
	dev_dbg(dev, "%s freed\n", __func__);
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

#define CEU_CETCR_MAGIC 0x0317f313 /* acknowledge magical interrupt sources */
#define CEU_CETCR_IGRW (1 << 4) /* prohibited register access interrupt bit */
#define CEU_CEIER_CPEIE (1 << 0) /* one-frame capture end interrupt */
#define CEU_CAPCR_CTNCP (1 << 16) /* continuous capture mode (if set) */


static void sh_mobile_ceu_capture(struct sh_mobile_ceu_dev *pcdev)
{
	struct soc_camera_device *icd = pcdev->icd;
	dma_addr_t phys_addr_top, phys_addr_bottom;

	/* The hardware is _very_ picky about this sequence. Especially
	 * the CEU_CETCR_MAGIC value. It seems like we need to acknowledge
	 * several not-so-well documented interrupt sources in CETCR.
	 */
	ceu_write(pcdev, CEIER, ceu_read(pcdev, CEIER) & ~CEU_CEIER_CPEIE);
	ceu_write(pcdev, CETCR, ~ceu_read(pcdev, CETCR) & CEU_CETCR_MAGIC);
	ceu_write(pcdev, CEIER, ceu_read(pcdev, CEIER) | CEU_CEIER_CPEIE);
	ceu_write(pcdev, CAPCR, ceu_read(pcdev, CAPCR) & ~CEU_CAPCR_CTNCP);
	ceu_write(pcdev, CETCR, CEU_CETCR_MAGIC ^ CEU_CETCR_IGRW);

	if (!pcdev->active)
		return;

	phys_addr_top = videobuf_to_dma_contig(pcdev->active);
	ceu_write(pcdev, CDAYR, phys_addr_top);
	if (pcdev->is_interlaced) {
		phys_addr_bottom = phys_addr_top + icd->user_width;
		ceu_write(pcdev, CDBYR, phys_addr_bottom);
	}

	switch (icd->current_fmt->fourcc) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		phys_addr_top += icd->user_width *
			icd->user_height;
		ceu_write(pcdev, CDACR, phys_addr_top);
		if (pcdev->is_interlaced) {
			phys_addr_bottom = phys_addr_top +
				icd->user_width;
			ceu_write(pcdev, CDBCR, phys_addr_bottom);
		}
	}

	pcdev->active->state = VIDEOBUF_ACTIVE;
	ceu_write(pcdev, CAPSR, 0x1); /* start capture */
}

static int sh_mobile_ceu_videobuf_prepare(struct videobuf_queue *vq,
					  struct videobuf_buffer *vb,
					  enum v4l2_field field)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct sh_mobile_ceu_buffer *buf;
	int ret;

	buf = container_of(vb, struct sh_mobile_ceu_buffer, vb);

	dev_dbg(icd->dev.parent, "%s (vb=0x%p) 0x%08lx %zd\n", __func__,
		vb, vb->baddr, vb->bsize);

	/* Added list head initialization on alloc */
	WARN_ON(!list_empty(&vb->queue));

#ifdef DEBUG
	/* This can be useful if you want to see if we actually fill
	 * the buffer with something */
	memset((void *)vb->baddr, 0xaa, vb->bsize);
#endif

	BUG_ON(NULL == icd->current_fmt);

	if (buf->fmt	!= icd->current_fmt ||
	    vb->width	!= icd->user_width ||
	    vb->height	!= icd->user_height ||
	    vb->field	!= field) {
		buf->fmt	= icd->current_fmt;
		vb->width	= icd->user_width;
		vb->height	= icd->user_height;
		vb->field	= field;
		vb->state	= VIDEOBUF_NEEDS_INIT;
	}

	vb->size = vb->width * vb->height * ((buf->fmt->depth + 7) >> 3);
	if (0 != vb->baddr && vb->bsize < vb->size) {
		ret = -EINVAL;
		goto out;
	}

	if (vb->state == VIDEOBUF_NEEDS_INIT) {
		ret = videobuf_iolock(vq, vb, NULL);
		if (ret)
			goto fail;
		vb->state = VIDEOBUF_PREPARED;
	}

	return 0;
fail:
	free_buffer(vq, buf);
out:
	return ret;
}

/* Called under spinlock_irqsave(&pcdev->lock, ...) */
static void sh_mobile_ceu_videobuf_queue(struct videobuf_queue *vq,
					 struct videobuf_buffer *vb)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;

	dev_dbg(icd->dev.parent, "%s (vb=0x%p) 0x%08lx %zd\n", __func__,
		vb, vb->baddr, vb->bsize);

	vb->state = VIDEOBUF_QUEUED;
	list_add_tail(&vb->queue, &pcdev->capture);

	if (!pcdev->active) {
		pcdev->active = vb;
		sh_mobile_ceu_capture(pcdev);
	}
}

static void sh_mobile_ceu_videobuf_release(struct videobuf_queue *vq,
					   struct videobuf_buffer *vb)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	unsigned long flags;

	spin_lock_irqsave(&pcdev->lock, flags);

	if (pcdev->active == vb) {
		/* disable capture (release DMA buffer), reset */
		ceu_write(pcdev, CAPSR, 1 << 16);
		pcdev->active = NULL;
	}

	if ((vb->state == VIDEOBUF_ACTIVE || vb->state == VIDEOBUF_QUEUED) &&
	    !list_empty(&vb->queue)) {
		vb->state = VIDEOBUF_ERROR;
		list_del_init(&vb->queue);
	}

	spin_unlock_irqrestore(&pcdev->lock, flags);

	free_buffer(vq, container_of(vb, struct sh_mobile_ceu_buffer, vb));
}

static struct videobuf_queue_ops sh_mobile_ceu_videobuf_ops = {
	.buf_setup      = sh_mobile_ceu_videobuf_setup,
	.buf_prepare    = sh_mobile_ceu_videobuf_prepare,
	.buf_queue      = sh_mobile_ceu_videobuf_queue,
	.buf_release    = sh_mobile_ceu_videobuf_release,
};

static irqreturn_t sh_mobile_ceu_irq(int irq, void *data)
{
	struct sh_mobile_ceu_dev *pcdev = data;
	struct videobuf_buffer *vb;
	unsigned long flags;

	spin_lock_irqsave(&pcdev->lock, flags);

	vb = pcdev->active;
	if (!vb)
		/* Stale interrupt from a released buffer */
		goto out;

	list_del_init(&vb->queue);

	if (!list_empty(&pcdev->capture))
		pcdev->active = list_entry(pcdev->capture.next,
					   struct videobuf_buffer, queue);
	else
		pcdev->active = NULL;

	sh_mobile_ceu_capture(pcdev);

	vb->state = VIDEOBUF_DONE;
	do_gettimeofday(&vb->ts);
	vb->field_count++;
	wake_up(&vb->done);

out:
	spin_unlock_irqrestore(&pcdev->lock, flags);

	return IRQ_HANDLED;
}

/* Called with .video_lock held */
static int sh_mobile_ceu_add_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;

	if (pcdev->icd)
		return -EBUSY;

	dev_info(icd->dev.parent,
		 "SuperH Mobile CEU driver attached to camera %d\n",
		 icd->devnum);

	pm_runtime_get_sync(ici->v4l2_dev.dev);

	ceu_write(pcdev, CAPSR, 1 << 16); /* reset */
	while (ceu_read(pcdev, CSTSR) & 1)
		msleep(1);

	pcdev->icd = icd;

	return 0;
}

/* Called with .video_lock held */
static void sh_mobile_ceu_remove_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	unsigned long flags;

	BUG_ON(icd != pcdev->icd);

	/* disable capture, disable interrupts */
	ceu_write(pcdev, CEIER, 0);
	ceu_write(pcdev, CAPSR, 1 << 16); /* reset */

	/* make sure active buffer is canceled */
	spin_lock_irqsave(&pcdev->lock, flags);
	if (pcdev->active) {
		list_del(&pcdev->active->queue);
		pcdev->active->state = VIDEOBUF_ERROR;
		wake_up_all(&pcdev->active->done);
		pcdev->active = NULL;
	}
	spin_unlock_irqrestore(&pcdev->lock, flags);

	pm_runtime_put_sync(ici->v4l2_dev.dev);

	dev_info(icd->dev.parent,
		 "SuperH Mobile CEU driver detached from camera %d\n",
		 icd->devnum);

	pcdev->icd = NULL;
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
static void sh_mobile_ceu_set_rect(struct soc_camera_device *icd,
				   unsigned int out_width,
				   unsigned int out_height)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_cam *cam = icd->host_priv;
	struct v4l2_rect *rect = &cam->ceu_rect;
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	unsigned int height, width, cdwdr_width, in_width, in_height;
	unsigned int left_offset, top_offset;
	u32 camor;

	dev_dbg(icd->dev.parent, "Crop %ux%u@%u:%u\n",
		rect->width, rect->height, rect->left, rect->top);

	left_offset	= rect->left;
	top_offset	= rect->top;

	if (pcdev->image_mode) {
		in_width = rect->width;
		if (!pcdev->is_16bit) {
			in_width *= 2;
			left_offset *= 2;
		}
		width = cdwdr_width = out_width;
	} else {
		unsigned int w_factor = (icd->current_fmt->depth + 7) >> 3;

		width = out_width * w_factor / 2;

		if (!pcdev->is_16bit)
			w_factor *= 2;

		in_width = rect->width * w_factor / 2;
		left_offset = left_offset * w_factor / 2;

		cdwdr_width = width * 2;
	}

	height = out_height;
	in_height = rect->height;
	if (pcdev->is_interlaced) {
		height /= 2;
		in_height /= 2;
		top_offset /= 2;
		cdwdr_width *= 2;
	}

	/* Set CAMOR, CAPWR, CFSZR, take care of CDWDR */
	camor = left_offset | (top_offset << 16);

	dev_geo(icd->dev.parent,
		"CAMOR 0x%x, CAPWR 0x%x, CFSZR 0x%x, CDWDR 0x%x\n", camor,
		(in_height << 16) | in_width, (height << 16) | width,
		cdwdr_width);

	ceu_write(pcdev, CAMOR, camor);
	ceu_write(pcdev, CAPWR, (in_height << 16) | in_width);
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

static int sh_mobile_ceu_set_bus_param(struct soc_camera_device *icd,
				       __u32 pixfmt)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	int ret;
	unsigned long camera_flags, common_flags, value;
	int yuv_lineskip;
	struct sh_mobile_ceu_cam *cam = icd->host_priv;
	u32 capsr = capture_save_reset(pcdev);

	camera_flags = icd->ops->query_bus_param(icd);
	common_flags = soc_camera_bus_param_compatible(camera_flags,
						       make_bus_param(pcdev));
	if (!common_flags)
		return -EINVAL;

	ret = icd->ops->set_bus_param(icd, common_flags);
	if (ret < 0)
		return ret;

	switch (common_flags & SOCAM_DATAWIDTH_MASK) {
	case SOCAM_DATAWIDTH_8:
		pcdev->is_16bit = 0;
		break;
	case SOCAM_DATAWIDTH_16:
		pcdev->is_16bit = 1;
		break;
	default:
		return -EINVAL;
	}

	ceu_write(pcdev, CRCNTR, 0);
	ceu_write(pcdev, CRCMPR, 0);

	value = 0x00000010; /* data fetch by default */
	yuv_lineskip = 0;

	switch (icd->current_fmt->fourcc) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		yuv_lineskip = 1; /* skip for NV12/21, no skip for NV16/61 */
		/* fall-through */
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		switch (cam->camera_fmt->fourcc) {
		case V4L2_PIX_FMT_UYVY:
			value = 0x00000000; /* Cb0, Y0, Cr0, Y1 */
			break;
		case V4L2_PIX_FMT_VYUY:
			value = 0x00000100; /* Cr0, Y0, Cb0, Y1 */
			break;
		case V4L2_PIX_FMT_YUYV:
			value = 0x00000200; /* Y0, Cb0, Y1, Cr0 */
			break;
		case V4L2_PIX_FMT_YVYU:
			value = 0x00000300; /* Y0, Cr0, Y1, Cb0 */
			break;
		default:
			BUG();
		}
	}

	if (icd->current_fmt->fourcc == V4L2_PIX_FMT_NV21 ||
	    icd->current_fmt->fourcc == V4L2_PIX_FMT_NV61)
		value ^= 0x00000100; /* swap U, V to change from NV1x->NVx1 */

	value |= common_flags & SOCAM_VSYNC_ACTIVE_LOW ? 1 << 1 : 0;
	value |= common_flags & SOCAM_HSYNC_ACTIVE_LOW ? 1 << 0 : 0;
	value |= pcdev->is_16bit ? 1 << 12 : 0;
	ceu_write(pcdev, CAMCR, value);

	ceu_write(pcdev, CAPCR, 0x00300000);
	ceu_write(pcdev, CAIFR, pcdev->is_interlaced ? 0x101 : 0);

	sh_mobile_ceu_set_rect(icd, icd->user_width, icd->user_height);
	mdelay(1);

	ceu_write(pcdev, CFLCR, pcdev->cflcr);

	/* A few words about byte order (observed in Big Endian mode)
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
	value = 0x00000017;
	if (yuv_lineskip)
		value &= ~0x00000010; /* convert 4:2:2 -> 4:2:0 */

	ceu_write(pcdev, CDOCR, value);
	ceu_write(pcdev, CFWCR, 0); /* keep "datafetch firewall" disabled */

	dev_dbg(icd->dev.parent, "S_FMT successful for %c%c%c%c %ux%u\n",
		pixfmt & 0xff, (pixfmt >> 8) & 0xff,
		(pixfmt >> 16) & 0xff, (pixfmt >> 24) & 0xff,
		icd->user_width, icd->user_height);

	capture_restore(pcdev, capsr);

	/* not in bundle mode: skip CBDSR, CDAYR2, CDACR2, CDBYR2, CDBCR2 */
	return 0;
}

static int sh_mobile_ceu_try_bus_param(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	unsigned long camera_flags, common_flags;

	camera_flags = icd->ops->query_bus_param(icd);
	common_flags = soc_camera_bus_param_compatible(camera_flags,
						       make_bus_param(pcdev));
	if (!common_flags)
		return -EINVAL;

	return 0;
}

static const struct soc_camera_data_format sh_mobile_ceu_formats[] = {
	{
		.name		= "NV12",
		.depth		= 12,
		.fourcc		= V4L2_PIX_FMT_NV12,
		.colorspace	= V4L2_COLORSPACE_JPEG,
	},
	{
		.name		= "NV21",
		.depth		= 12,
		.fourcc		= V4L2_PIX_FMT_NV21,
		.colorspace	= V4L2_COLORSPACE_JPEG,
	},
	{
		.name		= "NV16",
		.depth		= 16,
		.fourcc		= V4L2_PIX_FMT_NV16,
		.colorspace	= V4L2_COLORSPACE_JPEG,
	},
	{
		.name		= "NV61",
		.depth		= 16,
		.fourcc		= V4L2_PIX_FMT_NV61,
		.colorspace	= V4L2_COLORSPACE_JPEG,
	},
};

static int sh_mobile_ceu_get_formats(struct soc_camera_device *icd, int idx,
				     struct soc_camera_format_xlate *xlate)
{
	struct device *dev = icd->dev.parent;
	int ret, k, n;
	int formats = 0;
	struct sh_mobile_ceu_cam *cam;

	ret = sh_mobile_ceu_try_bus_param(icd);
	if (ret < 0)
		return 0;

	if (!icd->host_priv) {
		cam = kzalloc(sizeof(*cam), GFP_KERNEL);
		if (!cam)
			return -ENOMEM;

		icd->host_priv = cam;
	} else {
		cam = icd->host_priv;
	}

	/* Beginning of a pass */
	if (!idx)
		cam->extra_fmt = NULL;

	switch (icd->formats[idx].fourcc) {
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
		if (cam->extra_fmt)
			goto add_single_format;

		/*
		 * Our case is simple so far: for any of the above four camera
		 * formats we add all our four synthesized NV* formats, so,
		 * just marking the device with a single flag suffices. If
		 * the format generation rules are more complex, you would have
		 * to actually hang your already added / counted formats onto
		 * the host_priv pointer and check whether the format you're
		 * going to add now is already there.
		 */
		cam->extra_fmt = (void *)sh_mobile_ceu_formats;

		n = ARRAY_SIZE(sh_mobile_ceu_formats);
		formats += n;
		for (k = 0; xlate && k < n; k++) {
			xlate->host_fmt = &sh_mobile_ceu_formats[k];
			xlate->cam_fmt = icd->formats + idx;
			xlate->buswidth = icd->formats[idx].depth;
			xlate++;
			dev_dbg(dev, "Providing format %s using %s\n",
				sh_mobile_ceu_formats[k].name,
				icd->formats[idx].name);
		}
	default:
add_single_format:
		/* Generic pass-through */
		formats++;
		if (xlate) {
			xlate->host_fmt = icd->formats + idx;
			xlate->cam_fmt = icd->formats + idx;
			xlate->buswidth = icd->formats[idx].depth;
			xlate++;
			dev_dbg(dev,
				"Providing format %s in pass-through mode\n",
				icd->formats[idx].name);
		}
	}

	return formats;
}

static void sh_mobile_ceu_put_formats(struct soc_camera_device *icd)
{
	kfree(icd->host_priv);
	icd->host_priv = NULL;
}

/* Check if any dimension of r1 is smaller than respective one of r2 */
static bool is_smaller(struct v4l2_rect *r1, struct v4l2_rect *r2)
{
	return r1->width < r2->width || r1->height < r2->height;
}

/* Check if r1 fails to cover r2 */
static bool is_inside(struct v4l2_rect *r1, struct v4l2_rect *r2)
{
	return r1->left > r2->left || r1->top > r2->top ||
		r1->left + r1->width < r2->left + r2->width ||
		r1->top + r1->height < r2->top + r2->height;
}

static unsigned int scale_down(unsigned int size, unsigned int scale)
{
	return (size * 4096 + scale / 2) / scale;
}

static unsigned int scale_up(unsigned int size, unsigned int scale)
{
	return (size * scale + 2048) / 4096;
}

static unsigned int calc_generic_scale(unsigned int input, unsigned int output)
{
	return (input * 4096 + output / 2) / output;
}

static int client_g_rect(struct v4l2_subdev *sd, struct v4l2_rect *rect)
{
	struct v4l2_crop crop;
	struct v4l2_cropcap cap;
	int ret;

	crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	ret = v4l2_subdev_call(sd, video, g_crop, &crop);
	if (!ret) {
		*rect = crop.c;
		return ret;
	}

	/* Camera driver doesn't support .g_crop(), assume default rectangle */
	cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	ret = v4l2_subdev_call(sd, video, cropcap, &cap);
	if (ret < 0)
		return ret;

	*rect = cap.defrect;

	return ret;
}

/*
 * The common for both scaling and cropping iterative approach is:
 * 1. try if the client can produce exactly what requested by the user
 * 2. if (1) failed, try to double the client image until we get one big enough
 * 3. if (2) failed, try to request the maximum image
 */
static int client_s_crop(struct v4l2_subdev *sd, struct v4l2_crop *crop,
			 struct v4l2_crop *cam_crop)
{
	struct v4l2_rect *rect = &crop->c, *cam_rect = &cam_crop->c;
	struct device *dev = sd->v4l2_dev->dev;
	struct v4l2_cropcap cap;
	int ret;
	unsigned int width, height;

	v4l2_subdev_call(sd, video, s_crop, crop);
	ret = client_g_rect(sd, cam_rect);
	if (ret < 0)
		return ret;

	/*
	 * Now cam_crop contains the current camera input rectangle, and it must
	 * be within camera cropcap bounds
	 */
	if (!memcmp(rect, cam_rect, sizeof(*rect))) {
		/* Even if camera S_CROP failed, but camera rectangle matches */
		dev_dbg(dev, "Camera S_CROP successful for %ux%u@%u:%u\n",
			rect->width, rect->height, rect->left, rect->top);
		return 0;
	}

	/* Try to fix cropping, that camera hasn't managed to set */
	dev_geo(dev, "Fix camera S_CROP for %ux%u@%u:%u to %ux%u@%u:%u\n",
		cam_rect->width, cam_rect->height,
		cam_rect->left, cam_rect->top,
		rect->width, rect->height, rect->left, rect->top);

	/* We need sensor maximum rectangle */
	ret = v4l2_subdev_call(sd, video, cropcap, &cap);
	if (ret < 0)
		return ret;

	soc_camera_limit_side(&rect->left, &rect->width, cap.bounds.left, 2,
			      cap.bounds.width);
	soc_camera_limit_side(&rect->top, &rect->height, cap.bounds.top, 4,
			      cap.bounds.height);

	/*
	 * Popular special case - some cameras can only handle fixed sizes like
	 * QVGA, VGA,... Take care to avoid infinite loop.
	 */
	width = max(cam_rect->width, 2);
	height = max(cam_rect->height, 2);

	while (!ret && (is_smaller(cam_rect, rect) ||
			is_inside(cam_rect, rect)) &&
	       (cap.bounds.width > width || cap.bounds.height > height)) {

		width *= 2;
		height *= 2;

		cam_rect->width = width;
		cam_rect->height = height;

		/*
		 * We do not know what capabilities the camera has to set up
		 * left and top borders. We could try to be smarter in iterating
		 * them, e.g., if camera current left is to the right of the
		 * target left, set it to the middle point between the current
		 * left and minimum left. But that would add too much
		 * complexity: we would have to iterate each border separately.
		 */
		if (cam_rect->left > rect->left)
			cam_rect->left = cap.bounds.left;

		if (cam_rect->left + cam_rect->width < rect->left + rect->width)
			cam_rect->width = rect->left + rect->width -
				cam_rect->left;

		if (cam_rect->top > rect->top)
			cam_rect->top = cap.bounds.top;

		if (cam_rect->top + cam_rect->height < rect->top + rect->height)
			cam_rect->height = rect->top + rect->height -
				cam_rect->top;

		v4l2_subdev_call(sd, video, s_crop, cam_crop);
		ret = client_g_rect(sd, cam_rect);
		dev_geo(dev, "Camera S_CROP %d for %ux%u@%u:%u\n", ret,
			cam_rect->width, cam_rect->height,
			cam_rect->left, cam_rect->top);
	}

	/* S_CROP must not modify the rectangle */
	if (is_smaller(cam_rect, rect) || is_inside(cam_rect, rect)) {
		/*
		 * The camera failed to configure a suitable cropping,
		 * we cannot use the current rectangle, set to max
		 */
		*cam_rect = cap.bounds;
		v4l2_subdev_call(sd, video, s_crop, cam_crop);
		ret = client_g_rect(sd, cam_rect);
		dev_geo(dev, "Camera S_CROP %d for max %ux%u@%u:%u\n", ret,
			cam_rect->width, cam_rect->height,
			cam_rect->left, cam_rect->top);
	}

	return ret;
}

static int get_camera_scales(struct v4l2_subdev *sd, struct v4l2_rect *rect,
			     unsigned int *scale_h, unsigned int *scale_v)
{
	struct v4l2_format f;
	int ret;

	f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	ret = v4l2_subdev_call(sd, video, g_fmt, &f);
	if (ret < 0)
		return ret;

	*scale_h = calc_generic_scale(rect->width, f.fmt.pix.width);
	*scale_v = calc_generic_scale(rect->height, f.fmt.pix.height);

	return 0;
}

static int get_camera_subwin(struct soc_camera_device *icd,
			     struct v4l2_rect *cam_subrect,
			     unsigned int cam_hscale, unsigned int cam_vscale)
{
	struct sh_mobile_ceu_cam *cam = icd->host_priv;
	struct v4l2_rect *ceu_rect = &cam->ceu_rect;

	if (!ceu_rect->width) {
		struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
		struct device *dev = icd->dev.parent;
		struct v4l2_format f;
		struct v4l2_pix_format *pix = &f.fmt.pix;
		int ret;
		/* First time */

		f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		ret = v4l2_subdev_call(sd, video, g_fmt, &f);
		if (ret < 0)
			return ret;

		dev_geo(dev, "camera fmt %ux%u\n", pix->width, pix->height);

		if (pix->width > 2560) {
			ceu_rect->width	 = 2560;
			ceu_rect->left	 = (pix->width - 2560) / 2;
		} else {
			ceu_rect->width	 = pix->width;
			ceu_rect->left	 = 0;
		}

		if (pix->height > 1920) {
			ceu_rect->height = 1920;
			ceu_rect->top	 = (pix->height - 1920) / 2;
		} else {
			ceu_rect->height = pix->height;
			ceu_rect->top	 = 0;
		}

		dev_geo(dev, "initialised CEU rect %ux%u@%u:%u\n",
			ceu_rect->width, ceu_rect->height,
			ceu_rect->left, ceu_rect->top);
	}

	cam_subrect->width	= scale_up(ceu_rect->width, cam_hscale);
	cam_subrect->left	= scale_up(ceu_rect->left, cam_hscale);
	cam_subrect->height	= scale_up(ceu_rect->height, cam_vscale);
	cam_subrect->top	= scale_up(ceu_rect->top, cam_vscale);

	return 0;
}

static int client_s_fmt(struct soc_camera_device *icd, struct v4l2_format *f,
			bool ceu_can_scale)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct device *dev = icd->dev.parent;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	unsigned int width = pix->width, height = pix->height, tmp_w, tmp_h;
	unsigned int max_width, max_height;
	struct v4l2_cropcap cap;
	int ret;

	cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	ret = v4l2_subdev_call(sd, video, cropcap, &cap);
	if (ret < 0)
		return ret;

	max_width = min(cap.bounds.width, 2560);
	max_height = min(cap.bounds.height, 1920);

	ret = v4l2_subdev_call(sd, video, s_fmt, f);
	if (ret < 0)
		return ret;

	dev_geo(dev, "camera scaled to %ux%u\n", pix->width, pix->height);

	if ((width == pix->width && height == pix->height) || !ceu_can_scale)
		return 0;

	/* Camera set a format, but geometry is not precise, try to improve */
	tmp_w = pix->width;
	tmp_h = pix->height;

	/* width <= max_width && height <= max_height - guaranteed by try_fmt */
	while ((width > tmp_w || height > tmp_h) &&
	       tmp_w < max_width && tmp_h < max_height) {
		tmp_w = min(2 * tmp_w, max_width);
		tmp_h = min(2 * tmp_h, max_height);
		pix->width = tmp_w;
		pix->height = tmp_h;
		ret = v4l2_subdev_call(sd, video, s_fmt, f);
		dev_geo(dev, "Camera scaled to %ux%u\n",
			pix->width, pix->height);
		if (ret < 0) {
			/* This shouldn't happen */
			dev_err(dev, "Client failed to set format: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

/**
 * @rect	- camera cropped rectangle
 * @sub_rect	- CEU cropped rectangle, mapped back to camera input area
 * @ceu_rect	- on output calculated CEU crop rectangle
 */
static int client_scale(struct soc_camera_device *icd, struct v4l2_rect *rect,
			struct v4l2_rect *sub_rect, struct v4l2_rect *ceu_rect,
			struct v4l2_format *f, bool ceu_can_scale)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct sh_mobile_ceu_cam *cam = icd->host_priv;
	struct device *dev = icd->dev.parent;
	struct v4l2_format f_tmp = *f;
	struct v4l2_pix_format *pix_tmp = &f_tmp.fmt.pix;
	unsigned int scale_h, scale_v;
	int ret;

	/* 5. Apply iterative camera S_FMT for camera user window. */
	ret = client_s_fmt(icd, &f_tmp, ceu_can_scale);
	if (ret < 0)
		return ret;

	dev_geo(dev, "5: camera scaled to %ux%u\n",
		pix_tmp->width, pix_tmp->height);

	/* 6. Retrieve camera output window (g_fmt) */

	/* unneeded - it is already in "f_tmp" */

	/* 7. Calculate new camera scales. */
	ret = get_camera_scales(sd, rect, &scale_h, &scale_v);
	if (ret < 0)
		return ret;

	dev_geo(dev, "7: camera scales %u:%u\n", scale_h, scale_v);

	cam->cam_width		= pix_tmp->width;
	cam->cam_height		= pix_tmp->height;
	f->fmt.pix.width	= pix_tmp->width;
	f->fmt.pix.height	= pix_tmp->height;

	/*
	 * 8. Calculate new CEU crop - apply camera scales to previously
	 *    calculated "effective" crop.
	 */
	ceu_rect->left = scale_down(sub_rect->left, scale_h);
	ceu_rect->width = scale_down(sub_rect->width, scale_h);
	ceu_rect->top = scale_down(sub_rect->top, scale_v);
	ceu_rect->height = scale_down(sub_rect->height, scale_v);

	dev_geo(dev, "8: new CEU rect %ux%u@%u:%u\n",
		ceu_rect->width, ceu_rect->height,
		ceu_rect->left, ceu_rect->top);

	return 0;
}

/* Get combined scales */
static int get_scales(struct soc_camera_device *icd,
		      unsigned int *scale_h, unsigned int *scale_v)
{
	struct sh_mobile_ceu_cam *cam = icd->host_priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_crop cam_crop;
	unsigned int width_in, height_in;
	int ret;

	cam_crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	ret = client_g_rect(sd, &cam_crop.c);
	if (ret < 0)
		return ret;

	ret = get_camera_scales(sd, &cam_crop.c, scale_h, scale_v);
	if (ret < 0)
		return ret;

	width_in = scale_up(cam->ceu_rect.width, *scale_h);
	height_in = scale_up(cam->ceu_rect.height, *scale_v);

	*scale_h = calc_generic_scale(cam->ceu_rect.width, icd->user_width);
	*scale_v = calc_generic_scale(cam->ceu_rect.height, icd->user_height);

	return 0;
}

/*
 * CEU can scale and crop, but we don't want to waste bandwidth and kill the
 * framerate by always requesting the maximum image from the client. See
 * Documentation/video4linux/sh_mobile_camera_ceu.txt for a description of
 * scaling and cropping algorithms and for the meaning of referenced here steps.
 */
static int sh_mobile_ceu_set_crop(struct soc_camera_device *icd,
				  struct v4l2_crop *a)
{
	struct v4l2_rect *rect = &a->c;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	struct v4l2_crop cam_crop;
	struct sh_mobile_ceu_cam *cam = icd->host_priv;
	struct v4l2_rect *cam_rect = &cam_crop.c, *ceu_rect = &cam->ceu_rect;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct device *dev = icd->dev.parent;
	struct v4l2_format f;
	struct v4l2_pix_format *pix = &f.fmt.pix;
	unsigned int scale_comb_h, scale_comb_v, scale_ceu_h, scale_ceu_v,
		out_width, out_height;
	u32 capsr, cflcr;
	int ret;

	/* 1. Calculate current combined scales. */
	ret = get_scales(icd, &scale_comb_h, &scale_comb_v);
	if (ret < 0)
		return ret;

	dev_geo(dev, "1: combined scales %u:%u\n", scale_comb_h, scale_comb_v);

	/* 2. Apply iterative camera S_CROP for new input window. */
	ret = client_s_crop(sd, a, &cam_crop);
	if (ret < 0)
		return ret;

	dev_geo(dev, "2: camera cropped to %ux%u@%u:%u\n",
		cam_rect->width, cam_rect->height,
		cam_rect->left, cam_rect->top);

	/* On success cam_crop contains current camera crop */

	/*
	 * 3. If old combined scales applied to new crop produce an impossible
	 *    user window, adjust scales to produce nearest possible window.
	 */
	out_width	= scale_down(rect->width, scale_comb_h);
	out_height	= scale_down(rect->height, scale_comb_v);

	if (out_width > 2560)
		out_width = 2560;
	else if (out_width < 2)
		out_width = 2;

	if (out_height > 1920)
		out_height = 1920;
	else if (out_height < 4)
		out_height = 4;

	dev_geo(dev, "3: Adjusted output %ux%u\n", out_width, out_height);

	/* 4. Use G_CROP to retrieve actual input window: already in cam_crop */

	/*
	 * 5. Using actual input window and calculated combined scales calculate
	 *    camera target output window.
	 */
	pix->width		= scale_down(cam_rect->width, scale_comb_h);
	pix->height		= scale_down(cam_rect->height, scale_comb_v);

	dev_geo(dev, "5: camera target %ux%u\n", pix->width, pix->height);

	/* 6. - 9. */
	pix->pixelformat	= cam->camera_fmt->fourcc;
	pix->colorspace		= cam->camera_fmt->colorspace;

	capsr = capture_save_reset(pcdev);
	dev_dbg(dev, "CAPSR 0x%x, CFLCR 0x%x\n", capsr, pcdev->cflcr);

	/* Make relative to camera rectangle */
	rect->left		-= cam_rect->left;
	rect->top		-= cam_rect->top;

	f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	ret = client_scale(icd, cam_rect, rect, ceu_rect, &f,
			   pcdev->image_mode && !pcdev->is_interlaced);

	dev_geo(dev, "6-9: %d\n", ret);

	/* 10. Use CEU cropping to crop to the new window. */
	sh_mobile_ceu_set_rect(icd, out_width, out_height);

	dev_geo(dev, "10: CEU cropped to %ux%u@%u:%u\n",
		ceu_rect->width, ceu_rect->height,
		ceu_rect->left, ceu_rect->top);

	/*
	 * 11. Calculate CEU scales from camera scales from results of (10) and
	 *     user window from (3)
	 */
	scale_ceu_h = calc_scale(ceu_rect->width, &out_width);
	scale_ceu_v = calc_scale(ceu_rect->height, &out_height);

	dev_geo(dev, "11: CEU scales %u:%u\n", scale_ceu_h, scale_ceu_v);

	/* 12. Apply CEU scales. */
	cflcr = scale_ceu_h | (scale_ceu_v << 16);
	if (cflcr != pcdev->cflcr) {
		pcdev->cflcr = cflcr;
		ceu_write(pcdev, CFLCR, cflcr);
	}

	/* Restore capture */
	if (pcdev->active)
		capsr |= 1;
	capture_restore(pcdev, capsr);

	icd->user_width = out_width;
	icd->user_height = out_height;

	/* Even if only camera cropping succeeded */
	return ret;
}

/* Similar to set_crop multistage iterative algorithm */
static int sh_mobile_ceu_set_fmt(struct soc_camera_device *icd,
				 struct v4l2_format *f)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	struct sh_mobile_ceu_cam *cam = icd->host_priv;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_format cam_f = *f;
	struct v4l2_pix_format *cam_pix = &cam_f.fmt.pix;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct device *dev = icd->dev.parent;
	__u32 pixfmt = pix->pixelformat;
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_crop cam_crop;
	struct v4l2_rect *cam_rect = &cam_crop.c, cam_subrect, ceu_rect;
	unsigned int scale_cam_h, scale_cam_v;
	u16 scale_v, scale_h;
	int ret;
	bool is_interlaced, image_mode;

	switch (pix->field) {
	case V4L2_FIELD_INTERLACED:
		is_interlaced = true;
		break;
	case V4L2_FIELD_ANY:
	default:
		pix->field = V4L2_FIELD_NONE;
		/* fall-through */
	case V4L2_FIELD_NONE:
		is_interlaced = false;
		break;
	}

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (!xlate) {
		dev_warn(dev, "Format %x not found\n", pixfmt);
		return -EINVAL;
	}

	/* 1. Calculate current camera scales. */
	cam_crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	ret = client_g_rect(sd, cam_rect);
	if (ret < 0)
		return ret;

	ret = get_camera_scales(sd, cam_rect, &scale_cam_h, &scale_cam_v);
	if (ret < 0)
		return ret;

	dev_geo(dev, "1: camera scales %u:%u\n", scale_cam_h, scale_cam_v);

	/*
	 * 2. Calculate "effective" input crop (sensor subwindow) - CEU crop
	 *    scaled back at current camera scales onto input window.
	 */
	ret = get_camera_subwin(icd, &cam_subrect, scale_cam_h, scale_cam_v);
	if (ret < 0)
		return ret;

	dev_geo(dev, "2: subwin %ux%u@%u:%u\n",
		cam_subrect.width, cam_subrect.height,
		cam_subrect.left, cam_subrect.top);

	/*
	 * 3. Calculate new combined scales from "effective" input window to
	 *    requested user window.
	 */
	scale_h = calc_generic_scale(cam_subrect.width, pix->width);
	scale_v = calc_generic_scale(cam_subrect.height, pix->height);

	dev_geo(dev, "3: scales %u:%u\n", scale_h, scale_v);

	/*
	 * 4. Calculate camera output window by applying combined scales to real
	 *    input window.
	 */
	cam_pix->width = scale_down(cam_rect->width, scale_h);
	cam_pix->height = scale_down(cam_rect->height, scale_v);
	cam_pix->pixelformat = xlate->cam_fmt->fourcc;

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

	dev_geo(dev, "4: camera output %ux%u\n",
		cam_pix->width, cam_pix->height);

	/* 5. - 9. */
	ret = client_scale(icd, cam_rect, &cam_subrect, &ceu_rect, &cam_f,
			   image_mode && !is_interlaced);

	dev_geo(dev, "5-9: client scale %d\n", ret);

	/* Done with the camera. Now see if we can improve the result */

	dev_dbg(dev, "Camera %d fmt %ux%u, requested %ux%u\n",
		ret, cam_pix->width, cam_pix->height, pix->width, pix->height);
	if (ret < 0)
		return ret;

	/* 10. Use CEU scaling to scale to the requested user window. */

	/* We cannot scale up */
	if (pix->width > cam_pix->width)
		pix->width = cam_pix->width;
	if (pix->width > ceu_rect.width)
		pix->width = ceu_rect.width;

	if (pix->height > cam_pix->height)
		pix->height = cam_pix->height;
	if (pix->height > ceu_rect.height)
		pix->height = ceu_rect.height;

	/* Let's rock: scale pix->{width x height} down to width x height */
	scale_h = calc_scale(ceu_rect.width, &pix->width);
	scale_v = calc_scale(ceu_rect.height, &pix->height);

	dev_geo(dev, "10: W: %u : 0x%x = %u, H: %u : 0x%x = %u\n",
		ceu_rect.width, scale_h, pix->width,
		ceu_rect.height, scale_v, pix->height);

	pcdev->cflcr = scale_h | (scale_v << 16);

	icd->buswidth = xlate->buswidth;
	icd->current_fmt = xlate->host_fmt;
	cam->camera_fmt = xlate->cam_fmt;
	cam->ceu_rect = ceu_rect;

	pcdev->is_interlaced = is_interlaced;
	pcdev->image_mode = image_mode;

	return 0;
}

static int sh_mobile_ceu_try_fmt(struct soc_camera_device *icd,
				 struct v4l2_format *f)
{
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	__u32 pixfmt = pix->pixelformat;
	int width, height;
	int ret;

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (!xlate) {
		dev_warn(icd->dev.parent, "Format %x not found\n", pixfmt);
		return -EINVAL;
	}

	/* FIXME: calculate using depth and bus width */

	v4l_bound_align_image(&pix->width, 2, 2560, 1,
			      &pix->height, 4, 1920, 2, 0);

	width = pix->width;
	height = pix->height;

	pix->bytesperline = pix->width *
		DIV_ROUND_UP(xlate->host_fmt->depth, 8);
	pix->sizeimage = pix->height * pix->bytesperline;

	pix->pixelformat = xlate->cam_fmt->fourcc;

	/* limit to sensor capabilities */
	ret = v4l2_subdev_call(sd, video, try_fmt, f);
	pix->pixelformat = pixfmt;
	if (ret < 0)
		return ret;

	switch (pixfmt) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		/* FIXME: check against rect_max after converting soc-camera */
		/* We can scale precisely, need a bigger image from camera */
		if (pix->width < width || pix->height < height) {
			int tmp_w = pix->width, tmp_h = pix->height;
			pix->width = 2560;
			pix->height = 1920;
			ret = v4l2_subdev_call(sd, video, try_fmt, f);
			if (ret < 0) {
				/* Shouldn't actually happen... */
				dev_err(icd->dev.parent,
					"FIXME: try_fmt() returned %d\n", ret);
				pix->width = tmp_w;
				pix->height = tmp_h;
			}
		}
		if (pix->width > width)
			pix->width = width;
		if (pix->height > height)
			pix->height = height;
	}

	return ret;
}

static int sh_mobile_ceu_reqbufs(struct soc_camera_file *icf,
				 struct v4l2_requestbuffers *p)
{
	int i;

	/* This is for locking debugging only. I removed spinlocks and now I
	 * check whether .prepare is ever called on a linked buffer, or whether
	 * a dma IRQ can occur for an in-work or unlinked buffer. Until now
	 * it hadn't triggered */
	for (i = 0; i < p->count; i++) {
		struct sh_mobile_ceu_buffer *buf;

		buf = container_of(icf->vb_vidq.bufs[i],
				   struct sh_mobile_ceu_buffer, vb);
		INIT_LIST_HEAD(&buf->vb.queue);
	}

	return 0;
}

static unsigned int sh_mobile_ceu_poll(struct file *file, poll_table *pt)
{
	struct soc_camera_file *icf = file->private_data;
	struct sh_mobile_ceu_buffer *buf;

	buf = list_entry(icf->vb_vidq.stream.next,
			 struct sh_mobile_ceu_buffer, vb.stream);

	poll_wait(file, &buf->vb.done, pt);

	if (buf->vb.state == VIDEOBUF_DONE ||
	    buf->vb.state == VIDEOBUF_ERROR)
		return POLLIN|POLLRDNORM;

	return 0;
}

static int sh_mobile_ceu_querycap(struct soc_camera_host *ici,
				  struct v4l2_capability *cap)
{
	strlcpy(cap->card, "SuperH_Mobile_CEU", sizeof(cap->card));
	cap->version = KERNEL_VERSION(0, 0, 5);
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	return 0;
}

static void sh_mobile_ceu_init_videobuf(struct videobuf_queue *q,
					struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;

	videobuf_queue_dma_contig_init(q,
				       &sh_mobile_ceu_videobuf_ops,
				       icd->dev.parent, &pcdev->lock,
				       V4L2_BUF_TYPE_VIDEO_CAPTURE,
				       pcdev->is_interlaced ?
				       V4L2_FIELD_INTERLACED : V4L2_FIELD_NONE,
				       sizeof(struct sh_mobile_ceu_buffer),
				       icd);
}

static int sh_mobile_ceu_get_ctrl(struct soc_camera_device *icd,
				  struct v4l2_control *ctrl)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	u32 val;

	switch (ctrl->id) {
	case V4L2_CID_SHARPNESS:
		val = ceu_read(pcdev, CLFCR);
		ctrl->value = val ^ 1;
		return 0;
	}
	return -ENOIOCTLCMD;
}

static int sh_mobile_ceu_set_ctrl(struct soc_camera_device *icd,
				  struct v4l2_control *ctrl)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;

	switch (ctrl->id) {
	case V4L2_CID_SHARPNESS:
		switch (icd->current_fmt->fourcc) {
		case V4L2_PIX_FMT_NV12:
		case V4L2_PIX_FMT_NV21:
		case V4L2_PIX_FMT_NV16:
		case V4L2_PIX_FMT_NV61:
			ceu_write(pcdev, CLFCR, !ctrl->value);
			return 0;
		}
		return -EINVAL;
	}
	return -ENOIOCTLCMD;
}

static const struct v4l2_queryctrl sh_mobile_ceu_controls[] = {
	{
		.id		= V4L2_CID_SHARPNESS,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Low-pass filter",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
	},
};

static struct soc_camera_host_ops sh_mobile_ceu_host_ops = {
	.owner		= THIS_MODULE,
	.add		= sh_mobile_ceu_add_device,
	.remove		= sh_mobile_ceu_remove_device,
	.get_formats	= sh_mobile_ceu_get_formats,
	.put_formats	= sh_mobile_ceu_put_formats,
	.set_crop	= sh_mobile_ceu_set_crop,
	.set_fmt	= sh_mobile_ceu_set_fmt,
	.try_fmt	= sh_mobile_ceu_try_fmt,
	.set_ctrl	= sh_mobile_ceu_set_ctrl,
	.get_ctrl	= sh_mobile_ceu_get_ctrl,
	.reqbufs	= sh_mobile_ceu_reqbufs,
	.poll		= sh_mobile_ceu_poll,
	.querycap	= sh_mobile_ceu_querycap,
	.set_bus_param	= sh_mobile_ceu_set_bus_param,
	.init_videobuf	= sh_mobile_ceu_init_videobuf,
	.controls	= sh_mobile_ceu_controls,
	.num_controls	= ARRAY_SIZE(sh_mobile_ceu_controls),
};

static int __devinit sh_mobile_ceu_probe(struct platform_device *pdev)
{
	struct sh_mobile_ceu_dev *pcdev;
	struct resource *res;
	void __iomem *base;
	unsigned int irq;
	int err = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!res || !irq) {
		dev_err(&pdev->dev, "Not enough CEU platform resources.\n");
		err = -ENODEV;
		goto exit;
	}

	pcdev = kzalloc(sizeof(*pcdev), GFP_KERNEL);
	if (!pcdev) {
		dev_err(&pdev->dev, "Could not allocate pcdev\n");
		err = -ENOMEM;
		goto exit;
	}

	INIT_LIST_HEAD(&pcdev->capture);
	spin_lock_init(&pcdev->lock);

	pcdev->pdata = pdev->dev.platform_data;
	if (!pcdev->pdata) {
		err = -EINVAL;
		dev_err(&pdev->dev, "CEU platform data not set.\n");
		goto exit_kfree;
	}

	base = ioremap_nocache(res->start, resource_size(res));
	if (!base) {
		err = -ENXIO;
		dev_err(&pdev->dev, "Unable to ioremap CEU registers.\n");
		goto exit_kfree;
	}

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
			err = -ENXIO;
			goto exit_iounmap;
		}

		pcdev->video_limit = resource_size(res);
	}

	/* request irq */
	err = request_irq(pcdev->irq, sh_mobile_ceu_irq, IRQF_DISABLED,
			  dev_name(&pdev->dev), pcdev);
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

	err = soc_camera_host_register(&pcdev->ici);
	if (err)
		goto exit_free_irq;

	return 0;

exit_free_irq:
	free_irq(pcdev->irq, pcdev);
exit_release_mem:
	if (platform_get_resource(pdev, IORESOURCE_MEM, 1))
		dma_release_declared_memory(&pdev->dev);
exit_iounmap:
	iounmap(base);
exit_kfree:
	kfree(pcdev);
exit:
	return err;
}

static int __devexit sh_mobile_ceu_remove(struct platform_device *pdev)
{
	struct soc_camera_host *soc_host = to_soc_camera_host(&pdev->dev);
	struct sh_mobile_ceu_dev *pcdev = container_of(soc_host,
					struct sh_mobile_ceu_dev, ici);

	soc_camera_host_unregister(soc_host);
	free_irq(pcdev->irq, pcdev);
	if (platform_get_resource(pdev, IORESOURCE_MEM, 1))
		dma_release_declared_memory(&pdev->dev);
	iounmap(pcdev->base);
	kfree(pcdev);
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

static struct dev_pm_ops sh_mobile_ceu_dev_pm_ops = {
	.runtime_suspend = sh_mobile_ceu_runtime_nop,
	.runtime_resume = sh_mobile_ceu_runtime_nop,
};

static struct platform_driver sh_mobile_ceu_driver = {
	.driver 	= {
		.name	= "sh_mobile_ceu",
		.pm	= &sh_mobile_ceu_dev_pm_ops,
	},
	.probe		= sh_mobile_ceu_probe,
	.remove		= __exit_p(sh_mobile_ceu_remove),
};

static int __init sh_mobile_ceu_init(void)
{
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
MODULE_ALIAS("platform:sh_mobile_ceu");
