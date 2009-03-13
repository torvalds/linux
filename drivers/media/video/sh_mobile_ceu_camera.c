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
#include <linux/clk.h>

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

/* per video frame buffer */
struct sh_mobile_ceu_buffer {
	struct videobuf_buffer vb; /* v4l buffer must be first */
	const struct soc_camera_data_format *fmt;
};

struct sh_mobile_ceu_dev {
	struct device *dev;
	struct soc_camera_host ici;
	struct soc_camera_device *icd;

	unsigned int irq;
	void __iomem *base;
	struct clk *clk;
	unsigned long video_limit;

	/* lock used to protect videobuf */
	spinlock_t lock;
	struct list_head capture;
	struct videobuf_buffer *active;
	int is_interlaced;

	struct sh_mobile_ceu_info *pdata;

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

	*size = PAGE_ALIGN(icd->width * icd->height * bytes_per_pixel);

	if (0 == *count)
		*count = 2;

	if (pcdev->video_limit) {
		while (*size * *count > pcdev->video_limit)
			(*count)--;
	}

	dev_dbg(&icd->dev, "count=%d, size=%d\n", *count, *size);

	return 0;
}

static void free_buffer(struct videobuf_queue *vq,
			struct sh_mobile_ceu_buffer *buf)
{
	struct soc_camera_device *icd = vq->priv_data;

	dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %zd\n", __func__,
		&buf->vb, buf->vb.baddr, buf->vb.bsize);

	if (in_interrupt())
		BUG();

	videobuf_waiton(&buf->vb, 0, 0);
	videobuf_dma_contig_free(vq, &buf->vb);
	dev_dbg(&icd->dev, "%s freed\n", __func__);
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
		phys_addr_bottom = phys_addr_top + icd->width;
		ceu_write(pcdev, CDBYR, phys_addr_bottom);
	}

	switch (icd->current_fmt->fourcc) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		phys_addr_top += icd->width * icd->height;
		ceu_write(pcdev, CDACR, phys_addr_top);
		if (pcdev->is_interlaced) {
			phys_addr_bottom = phys_addr_top + icd->width;
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

	dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %zd\n", __func__,
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
	    vb->width	!= icd->width ||
	    vb->height	!= icd->height ||
	    vb->field	!= field) {
		buf->fmt	= icd->current_fmt;
		vb->width	= icd->width;
		vb->height	= icd->height;
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

static void sh_mobile_ceu_videobuf_queue(struct videobuf_queue *vq,
					 struct videobuf_buffer *vb)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	unsigned long flags;

	dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %zd\n", __func__,
		vb, vb->baddr, vb->bsize);

	vb->state = VIDEOBUF_QUEUED;
	spin_lock_irqsave(&pcdev->lock, flags);
	list_add_tail(&vb->queue, &pcdev->capture);

	if (!pcdev->active) {
		pcdev->active = vb;
		sh_mobile_ceu_capture(pcdev);
	}

	spin_unlock_irqrestore(&pcdev->lock, flags);
}

static void sh_mobile_ceu_videobuf_release(struct videobuf_queue *vq,
					   struct videobuf_buffer *vb)
{
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
	spin_unlock_irqrestore(&pcdev->lock, flags);

	return IRQ_HANDLED;
}

/* Called with .video_lock held */
static int sh_mobile_ceu_add_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	int ret = -EBUSY;

	if (pcdev->icd)
		goto err;

	dev_info(&icd->dev,
		 "SuperH Mobile CEU driver attached to camera %d\n",
		 icd->devnum);

	ret = icd->ops->init(icd);
	if (ret)
		goto err;

	clk_enable(pcdev->clk);

	ceu_write(pcdev, CAPSR, 1 << 16); /* reset */
	while (ceu_read(pcdev, CSTSR) & 1)
		msleep(1);

	pcdev->icd = icd;
err:
	return ret;
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

	clk_disable(pcdev->clk);

	icd->ops->release(icd);

	dev_info(&icd->dev,
		 "SuperH Mobile CEU driver detached from camera %d\n",
		 icd->devnum);

	pcdev->icd = NULL;
}

static int sh_mobile_ceu_set_bus_param(struct soc_camera_device *icd,
				       __u32 pixfmt)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	int ret, buswidth, width, height, cfszr_width, cdwdr_width;
	unsigned long camera_flags, common_flags, value;
	int yuv_mode, yuv_lineskip;

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
		buswidth = 8;
		break;
	case SOCAM_DATAWIDTH_16:
		buswidth = 16;
		break;
	default:
		return -EINVAL;
	}

	ceu_write(pcdev, CRCNTR, 0);
	ceu_write(pcdev, CRCMPR, 0);

	value = 0x00000010; /* data fetch by default */
	yuv_mode = yuv_lineskip = 0;

	switch (icd->current_fmt->fourcc) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		yuv_lineskip = 1; /* skip for NV12/21, no skip for NV16/61 */
		/* fall-through */
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		yuv_mode = 1;
		switch (pcdev->camera_fmt->fourcc) {
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
	value |= buswidth == 16 ? 1 << 12 : 0;
	ceu_write(pcdev, CAMCR, value);

	ceu_write(pcdev, CAPCR, 0x00300000);
	ceu_write(pcdev, CAIFR, pcdev->is_interlaced ? 0x101 : 0);

	mdelay(1);

	if (yuv_mode) {
		width = icd->width * 2;
		width = buswidth == 16 ? width / 2 : width;
		cfszr_width = cdwdr_width = icd->width;
	} else {
		width = icd->width * ((icd->current_fmt->depth + 7) >> 3);
		width = buswidth == 16 ? width / 2 : width;
		cfszr_width = buswidth == 8 ? width / 2 : width;
		cdwdr_width = buswidth == 16 ? width * 2 : width;
	}

	height = icd->height;
	if (pcdev->is_interlaced) {
		height /= 2;
		cdwdr_width *= 2;
	}

	ceu_write(pcdev, CAMOR, 0);
	ceu_write(pcdev, CAPWR, (height << 16) | width);
	ceu_write(pcdev, CFLCR, 0); /* no scaling */
	ceu_write(pcdev, CFSZR, (height << 16) | cfszr_width);
	ceu_write(pcdev, CLFCR, 0); /* no lowpass filter */

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

	ceu_write(pcdev, CDWDR, cdwdr_width);
	ceu_write(pcdev, CFWCR, 0); /* keep "datafetch firewall" disabled */

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
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	int ret, k, n;
	int formats = 0;

	ret = sh_mobile_ceu_try_bus_param(icd);
	if (ret < 0)
		return 0;

	/* Beginning of a pass */
	if (!idx)
		icd->host_priv = NULL;

	switch (icd->formats[idx].fourcc) {
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
		if (icd->host_priv)
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
		icd->host_priv = (void *)sh_mobile_ceu_formats;

		n = ARRAY_SIZE(sh_mobile_ceu_formats);
		formats += n;
		for (k = 0; xlate && k < n; k++) {
			xlate->host_fmt = &sh_mobile_ceu_formats[k];
			xlate->cam_fmt = icd->formats + idx;
			xlate->buswidth = icd->formats[idx].depth;
			xlate++;
			dev_dbg(&ici->dev, "Providing format %s using %s\n",
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
			dev_dbg(&ici->dev,
				"Providing format %s in pass-through mode\n",
				icd->formats[idx].name);
		}
	}

	return formats;
}

static int sh_mobile_ceu_set_crop(struct soc_camera_device *icd,
				  struct v4l2_rect *rect)
{
	return icd->ops->set_crop(icd, rect);
}

static int sh_mobile_ceu_set_fmt(struct soc_camera_device *icd,
				 struct v4l2_format *f)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	__u32 pixfmt = f->fmt.pix.pixelformat;
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_format cam_f = *f;
	int ret;

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (!xlate) {
		dev_warn(&ici->dev, "Format %x not found\n", pixfmt);
		return -EINVAL;
	}

	cam_f.fmt.pix.pixelformat = xlate->cam_fmt->fourcc;
	ret = icd->ops->set_fmt(icd, &cam_f);

	if (!ret) {
		icd->buswidth = xlate->buswidth;
		icd->current_fmt = xlate->host_fmt;
		pcdev->camera_fmt = xlate->cam_fmt;
	}

	return ret;
}

static int sh_mobile_ceu_try_fmt(struct soc_camera_device *icd,
				 struct v4l2_format *f)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	const struct soc_camera_format_xlate *xlate;
	__u32 pixfmt = f->fmt.pix.pixelformat;
	int ret;

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (!xlate) {
		dev_warn(&ici->dev, "Format %x not found\n", pixfmt);
		return -EINVAL;
	}

	/* FIXME: calculate using depth and bus width */

	if (f->fmt.pix.height < 4)
		f->fmt.pix.height = 4;
	if (f->fmt.pix.height > 1920)
		f->fmt.pix.height = 1920;
	if (f->fmt.pix.width < 2)
		f->fmt.pix.width = 2;
	if (f->fmt.pix.width > 2560)
		f->fmt.pix.width = 2560;
	f->fmt.pix.width &= ~0x01;
	f->fmt.pix.height &= ~0x03;

	f->fmt.pix.bytesperline = f->fmt.pix.width *
		DIV_ROUND_UP(xlate->host_fmt->depth, 8);
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;

	/* limit to sensor capabilities */
	ret = icd->ops->try_fmt(icd, f);
	if (ret < 0)
		return ret;

	switch (f->fmt.pix.field) {
	case V4L2_FIELD_INTERLACED:
		pcdev->is_interlaced = 1;
		break;
	case V4L2_FIELD_ANY:
		f->fmt.pix.field = V4L2_FIELD_NONE;
		/* fall-through */
	case V4L2_FIELD_NONE:
		pcdev->is_interlaced = 0;
		break;
	default:
		ret = -EINVAL;
		break;
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
				       &ici->dev, &pcdev->lock,
				       V4L2_BUF_TYPE_VIDEO_CAPTURE,
				       pcdev->is_interlaced ?
				       V4L2_FIELD_INTERLACED : V4L2_FIELD_NONE,
				       sizeof(struct sh_mobile_ceu_buffer),
				       icd);
}

static struct soc_camera_host_ops sh_mobile_ceu_host_ops = {
	.owner		= THIS_MODULE,
	.add		= sh_mobile_ceu_add_device,
	.remove		= sh_mobile_ceu_remove_device,
	.get_formats	= sh_mobile_ceu_get_formats,
	.set_crop	= sh_mobile_ceu_set_crop,
	.set_fmt	= sh_mobile_ceu_set_fmt,
	.try_fmt	= sh_mobile_ceu_try_fmt,
	.reqbufs	= sh_mobile_ceu_reqbufs,
	.poll		= sh_mobile_ceu_poll,
	.querycap	= sh_mobile_ceu_querycap,
	.set_bus_param	= sh_mobile_ceu_set_bus_param,
	.init_videobuf	= sh_mobile_ceu_init_videobuf,
};

static int sh_mobile_ceu_probe(struct platform_device *pdev)
{
	struct sh_mobile_ceu_dev *pcdev;
	struct resource *res;
	void __iomem *base;
	char clk_name[8];
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

	platform_set_drvdata(pdev, pcdev);
	INIT_LIST_HEAD(&pcdev->capture);
	spin_lock_init(&pcdev->lock);

	pcdev->pdata = pdev->dev.platform_data;
	if (!pcdev->pdata) {
		err = -EINVAL;
		dev_err(&pdev->dev, "CEU platform data not set.\n");
		goto exit_kfree;
	}

	base = ioremap_nocache(res->start, res->end - res->start + 1);
	if (!base) {
		err = -ENXIO;
		dev_err(&pdev->dev, "Unable to ioremap CEU registers.\n");
		goto exit_kfree;
	}

	pcdev->irq = irq;
	pcdev->base = base;
	pcdev->video_limit = 0; /* only enabled if second resource exists */
	pcdev->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res) {
		err = dma_declare_coherent_memory(&pdev->dev, res->start,
						  res->start,
						  (res->end - res->start) + 1,
						  DMA_MEMORY_MAP |
						  DMA_MEMORY_EXCLUSIVE);
		if (!err) {
			dev_err(&pdev->dev, "Unable to declare CEU memory.\n");
			err = -ENXIO;
			goto exit_iounmap;
		}

		pcdev->video_limit = (res->end - res->start) + 1;
	}

	/* request irq */
	err = request_irq(pcdev->irq, sh_mobile_ceu_irq, IRQF_DISABLED,
			  dev_name(&pdev->dev), pcdev);
	if (err) {
		dev_err(&pdev->dev, "Unable to register CEU interrupt.\n");
		goto exit_release_mem;
	}

	snprintf(clk_name, sizeof(clk_name), "ceu%d", pdev->id);
	pcdev->clk = clk_get(&pdev->dev, clk_name);
	if (IS_ERR(pcdev->clk)) {
		dev_err(&pdev->dev, "cannot get clock \"%s\"\n", clk_name);
		err = PTR_ERR(pcdev->clk);
		goto exit_free_irq;
	}

	pcdev->ici.priv = pcdev;
	pcdev->ici.dev.parent = &pdev->dev;
	pcdev->ici.nr = pdev->id;
	pcdev->ici.drv_name = dev_name(&pdev->dev);
	pcdev->ici.ops = &sh_mobile_ceu_host_ops;

	err = soc_camera_host_register(&pcdev->ici);
	if (err)
		goto exit_free_clk;

	return 0;

exit_free_clk:
	clk_put(pcdev->clk);
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

static int sh_mobile_ceu_remove(struct platform_device *pdev)
{
	struct sh_mobile_ceu_dev *pcdev = platform_get_drvdata(pdev);

	soc_camera_host_unregister(&pcdev->ici);
	clk_put(pcdev->clk);
	free_irq(pcdev->irq, pcdev);
	if (platform_get_resource(pdev, IORESOURCE_MEM, 1))
		dma_release_declared_memory(&pdev->dev);
	iounmap(pcdev->base);
	kfree(pcdev);
	return 0;
}

static struct platform_driver sh_mobile_ceu_driver = {
	.driver 	= {
		.name	= "sh_mobile_ceu",
	},
	.probe		= sh_mobile_ceu_probe,
	.remove		= sh_mobile_ceu_remove,
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
