/*
 * V4L2 Driver for PXA camera host
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
#include <linux/mutex.h>
#include <linux/clk.h>

#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/soc_camera.h>

#include <linux/videodev2.h>

#include <asm/dma.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/camera.h>

#define PXA_CAM_VERSION_CODE KERNEL_VERSION(0, 0, 5)
#define PXA_CAM_DRV_NAME "pxa27x-camera"

#define CICR0_SIM_MP	(0 << 24)
#define CICR0_SIM_SP	(1 << 24)
#define CICR0_SIM_MS	(2 << 24)
#define CICR0_SIM_EP	(3 << 24)
#define CICR0_SIM_ES	(4 << 24)

#define CICR1_DW_VAL(x)   ((x) & CICR1_DW)	    /* Data bus width */
#define CICR1_PPL_VAL(x)  (((x) << 15) & CICR1_PPL) /* Pixels per line */

#define CICR2_BLW_VAL(x)  (((x) << 24) & CICR2_BLW) /* Beginning-of-line pixel clock wait count */
#define CICR2_ELW_VAL(x)  (((x) << 16) & CICR2_ELW) /* End-of-line pixel clock wait count */
#define CICR2_HSW_VAL(x)  (((x) << 10) & CICR2_HSW) /* Horizontal sync pulse width */
#define CICR2_BFPW_VAL(x) (((x) << 3) & CICR2_BFPW) /* Beginning-of-frame pixel clock wait count */
#define CICR2_FSW_VAL(x)  (((x) << 0) & CICR2_FSW)  /* Frame stabilization wait count */

#define CICR3_BFW_VAL(x)  (((x) << 24) & CICR3_BFW) /* Beginning-of-frame line clock wait count  */
#define CICR3_EFW_VAL(x)  (((x) << 16) & CICR3_EFW) /* End-of-frame line clock wait count */
#define CICR3_VSW_VAL(x)  (((x) << 11) & CICR3_VSW) /* Vertical sync pulse width */
#define CICR3_LPF_VAL(x)  (((x) << 0) & CICR3_LPF)  /* Lines per frame */

#define CICR0_IRQ_MASK (CICR0_TOM | CICR0_RDAVM | CICR0_FEM | CICR0_EOLM | \
			CICR0_PERRM | CICR0_QDM | CICR0_CDM | CICR0_SOFM | \
			CICR0_EOFM | CICR0_FOM)

static DEFINE_MUTEX(camera_lock);

/*
 * Structures
 */

/* buffer for one video frame */
struct pxa_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	const struct soc_camera_data_format        *fmt;

	/* our descriptor list needed for the PXA DMA engine */
	dma_addr_t		sg_dma;
	struct pxa_dma_desc	*sg_cpu;
	size_t			sg_size;
	int			inwork;
};

struct pxa_framebuffer_queue {
	dma_addr_t		sg_last_dma;
	struct pxa_dma_desc	*sg_last_cpu;
};

struct pxa_camera_dev {
	struct device		*dev;
	/* PXA27x is only supposed to handle one camera on its Quick Capture
	 * interface. If anyone ever builds hardware to enable more than
	 * one camera, they will have to modify this driver too */
	struct soc_camera_device *icd;
	struct clk		*clk;

	unsigned int		irq;
	void __iomem		*base;
	unsigned int		dma_chan_y;

	struct pxacamera_platform_data *pdata;
	struct resource		*res;
	unsigned long		platform_flags;
	unsigned long		platform_mclk_10khz;

	struct list_head	capture;

	spinlock_t		lock;

	struct pxa_buffer	*active;
};

static const char *pxa_cam_driver_description = "PXA_Camera";

static unsigned int vid_limit = 16;	/* Video memory limit, in Mb */

/*
 *  Videobuf operations
 */
static int pxa_videobuf_setup(struct videobuf_queue *vq, unsigned int *count,
			      unsigned int *size)
{
	struct soc_camera_device *icd = vq->priv_data;

	dev_dbg(&icd->dev, "count=%d, size=%d\n", *count, *size);

	*size = icd->width * icd->height * ((icd->current_fmt->depth + 7) >> 3);

	if (0 == *count)
		*count = 32;
	while (*size * *count > vid_limit * 1024 * 1024)
		(*count)--;

	return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct pxa_buffer *buf)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici =
		to_soc_camera_host(icd->dev.parent);
	struct pxa_camera_dev *pcdev = ici->priv;
	struct videobuf_dmabuf *dma = videobuf_to_dma(&buf->vb);

	BUG_ON(in_interrupt());

	dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %d\n", __FUNCTION__,
		&buf->vb, buf->vb.baddr, buf->vb.bsize);

	/* This waits until this buffer is out of danger, i.e., until it is no
	 * longer in STATE_QUEUED or STATE_ACTIVE */
	videobuf_waiton(&buf->vb, 0, 0);
	videobuf_dma_unmap(vq, dma);
	videobuf_dma_free(dma);

	if (buf->sg_cpu)
		dma_free_coherent(pcdev->dev, buf->sg_size, buf->sg_cpu,
				  buf->sg_dma);
	buf->sg_cpu = NULL;

	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

static int pxa_videobuf_prepare(struct videobuf_queue *vq,
		struct videobuf_buffer *vb, enum v4l2_field field)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici =
		to_soc_camera_host(icd->dev.parent);
	struct pxa_camera_dev *pcdev = ici->priv;
	struct pxa_buffer *buf = container_of(vb, struct pxa_buffer, vb);
	int i, ret;

	dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %d\n", __FUNCTION__,
		vb, vb->baddr, vb->bsize);

	/* Added list head initialization on alloc */
	WARN_ON(!list_empty(&vb->queue));

#ifdef DEBUG
	/* This can be useful if you want to see if we actually fill
	 * the buffer with something */
	memset((void *)vb->baddr, 0xaa, vb->bsize);
#endif

	BUG_ON(NULL == icd->current_fmt);

	/* I think, in buf_prepare you only have to protect global data,
	 * the actual buffer is yours */
	buf->inwork = 1;

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
		unsigned int size = vb->size;
		struct videobuf_dmabuf *dma = videobuf_to_dma(vb);

		ret = videobuf_iolock(vq, vb, NULL);
		if (ret)
			goto fail;

		if (buf->sg_cpu)
			dma_free_coherent(pcdev->dev, buf->sg_size, buf->sg_cpu,
					  buf->sg_dma);

		buf->sg_size = (dma->sglen + 1) * sizeof(struct pxa_dma_desc);
		buf->sg_cpu = dma_alloc_coherent(pcdev->dev, buf->sg_size,
						 &buf->sg_dma, GFP_KERNEL);
		if (!buf->sg_cpu) {
			ret = -ENOMEM;
			goto fail;
		}

		dev_dbg(&icd->dev, "nents=%d size: %d sg=0x%p\n",
			dma->sglen, size, dma->sglist);
		for (i = 0; i < dma->sglen; i++) {
			struct scatterlist *sg = dma->sglist;
			unsigned int dma_len = sg_dma_len(&sg[i]), xfer_len;

			/* CIBR0 */
			buf->sg_cpu[i].dsadr = pcdev->res->start + 0x28;
			buf->sg_cpu[i].dtadr = sg_dma_address(&sg[i]);
			/* PXA270 Developer's Manual 27.4.4.1:
			 * round up to 8 bytes */
			xfer_len = (min(dma_len, size) + 7) & ~7;
			if (xfer_len & 7)
				dev_err(&icd->dev, "Unaligned buffer: "
					"dma_len %u, size %u\n", dma_len, size);
			buf->sg_cpu[i].dcmd = DCMD_FLOWSRC | DCMD_BURST8 |
				DCMD_INCTRGADDR | xfer_len;
			size -= dma_len;
			buf->sg_cpu[i].ddadr = buf->sg_dma + (i + 1) *
					sizeof(struct pxa_dma_desc);
		}
		buf->sg_cpu[dma->sglen - 1].ddadr = DDADR_STOP;
		buf->sg_cpu[dma->sglen - 1].dcmd |= DCMD_ENDIRQEN;

		vb->state = VIDEOBUF_PREPARED;
	}

	buf->inwork = 0;

	return 0;

fail:
	free_buffer(vq, buf);
out:
	buf->inwork = 0;
	return ret;
}

static void pxa_videobuf_queue(struct videobuf_queue *vq,
			       struct videobuf_buffer *vb)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici =
		to_soc_camera_host(icd->dev.parent);
	struct pxa_camera_dev *pcdev = ici->priv;
	struct pxa_buffer *buf = container_of(vb, struct pxa_buffer, vb);
	struct pxa_buffer *active;
	struct videobuf_dmabuf *dma = videobuf_to_dma(vb);
	int nents = dma->sglen;
	unsigned long flags;

	dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %d\n", __FUNCTION__,
		vb, vb->baddr, vb->bsize);
	spin_lock_irqsave(&pcdev->lock, flags);

	list_add_tail(&vb->queue, &pcdev->capture);

	vb->state = VIDEOBUF_ACTIVE;
	active = pcdev->active;

	if (!active) {
		CIFR |= CIFR_RESET_F;
		DDADR(pcdev->dma_chan_y) = buf->sg_dma;
		DCSR(pcdev->dma_chan_y) = DCSR_RUN;
		pcdev->active = buf;
		CICR0 |= CICR0_ENB;
	} else {
		struct videobuf_dmabuf *active_dma =
			videobuf_to_dma(&active->vb);
		/* Stop DMA engine */
		DCSR(pcdev->dma_chan_y) = 0;

		/* Add the descriptors we just initialized to the currently
		 * running chain
		 */
		active->sg_cpu[active_dma->sglen - 1].ddadr = buf->sg_dma;

		/* Setup a dummy descriptor with the DMA engines current
		 * state
		 */
		/* CIBR0 */
		buf->sg_cpu[nents].dsadr = pcdev->res->start + 0x28;
		buf->sg_cpu[nents].dtadr = DTADR(pcdev->dma_chan_y);
		buf->sg_cpu[nents].dcmd = DCMD(pcdev->dma_chan_y);

		if (DDADR(pcdev->dma_chan_y) == DDADR_STOP) {
			/* The DMA engine is on the last descriptor, set the
			 * next descriptors address to the descriptors
			 * we just initialized
			 */
			buf->sg_cpu[nents].ddadr = buf->sg_dma;
		} else {
			buf->sg_cpu[nents].ddadr = DDADR(pcdev->dma_chan_y);
		}

		/* The next descriptor is the dummy descriptor */
		DDADR(pcdev->dma_chan_y) = buf->sg_dma + nents *
			sizeof(struct pxa_dma_desc);

#ifdef DEBUG
		if (CISR & CISR_IFO_0) {
			dev_warn(pcdev->dev, "FIFO overrun\n");
			DDADR(pcdev->dma_chan_y) = pcdev->active->sg_dma;

			CICR0 &= ~CICR0_ENB;
			CIFR |= CIFR_RESET_F;
			DCSR(pcdev->dma_chan_y) = DCSR_RUN;
			CICR0 |= CICR0_ENB;
		} else
#endif
			DCSR(pcdev->dma_chan_y) = DCSR_RUN;
	}

	spin_unlock_irqrestore(&pcdev->lock, flags);

}

static void pxa_videobuf_release(struct videobuf_queue *vq,
				 struct videobuf_buffer *vb)
{
	struct pxa_buffer *buf = container_of(vb, struct pxa_buffer, vb);
#ifdef DEBUG
	struct soc_camera_device *icd = vq->priv_data;

	dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %d\n", __FUNCTION__,
		vb, vb->baddr, vb->bsize);

	switch (vb->state) {
	case VIDEOBUF_ACTIVE:
		dev_dbg(&icd->dev, "%s (active)\n", __FUNCTION__);
		break;
	case VIDEOBUF_QUEUED:
		dev_dbg(&icd->dev, "%s (queued)\n", __FUNCTION__);
		break;
	case VIDEOBUF_PREPARED:
		dev_dbg(&icd->dev, "%s (prepared)\n", __FUNCTION__);
		break;
	default:
		dev_dbg(&icd->dev, "%s (unknown)\n", __FUNCTION__);
		break;
	}
#endif

	free_buffer(vq, buf);
}

static void pxa_camera_dma_irq_y(int channel, void *data)
{
	struct pxa_camera_dev *pcdev = data;
	struct pxa_buffer *buf;
	unsigned long flags;
	unsigned int status;
	struct videobuf_buffer *vb;

	spin_lock_irqsave(&pcdev->lock, flags);

	status = DCSR(pcdev->dma_chan_y);
	DCSR(pcdev->dma_chan_y) = status;

	if (status & DCSR_BUSERR) {
		dev_err(pcdev->dev, "DMA Bus Error IRQ!\n");
		goto out;
	}

	if (!(status & DCSR_ENDINTR)) {
		dev_err(pcdev->dev, "Unknown DMA IRQ source, "
			"status: 0x%08x\n", status);
		goto out;
	}

	if (!pcdev->active) {
		dev_err(pcdev->dev, "DMA End IRQ with no active buffer!\n");
		goto out;
	}

	vb = &pcdev->active->vb;
	buf = container_of(vb, struct pxa_buffer, vb);
	WARN_ON(buf->inwork || list_empty(&vb->queue));
	dev_dbg(pcdev->dev, "%s (vb=0x%p) 0x%08lx %d\n", __FUNCTION__,
		vb, vb->baddr, vb->bsize);

	/* _init is used to debug races, see comment in pxa_camera_reqbufs() */
	list_del_init(&vb->queue);
	vb->state = VIDEOBUF_DONE;
	do_gettimeofday(&vb->ts);
	vb->field_count++;
	wake_up(&vb->done);

	if (list_empty(&pcdev->capture)) {
		pcdev->active = NULL;
		DCSR(pcdev->dma_chan_y) = 0;
		CICR0 &= ~CICR0_ENB;
		goto out;
	}

	pcdev->active = list_entry(pcdev->capture.next, struct pxa_buffer,
				   vb.queue);

out:
	spin_unlock_irqrestore(&pcdev->lock, flags);
}

static struct videobuf_queue_ops pxa_videobuf_ops = {
	.buf_setup      = pxa_videobuf_setup,
	.buf_prepare    = pxa_videobuf_prepare,
	.buf_queue      = pxa_videobuf_queue,
	.buf_release    = pxa_videobuf_release,
};

static int mclk_get_divisor(struct pxa_camera_dev *pcdev)
{
	unsigned int mclk_10khz = pcdev->platform_mclk_10khz;
	unsigned long div;
	unsigned long lcdclk;

	lcdclk = clk_get_rate(pcdev->clk) / 10000;

	/* We verify platform_mclk_10khz != 0, so if anyone breaks it, here
	 * they get a nice Oops */
	div = (lcdclk + 2 * mclk_10khz - 1) / (2 * mclk_10khz) - 1;

	dev_dbg(pcdev->dev, "LCD clock %lukHz, target freq %dkHz, "
		"divisor %lu\n", lcdclk * 10, mclk_10khz * 10, div);

	return div;
}

static void pxa_camera_activate(struct pxa_camera_dev *pcdev)
{
	struct pxacamera_platform_data *pdata = pcdev->pdata;
	u32 cicr4 = 0;

	dev_dbg(pcdev->dev, "Registered platform device at %p data %p\n",
		pcdev, pdata);

	if (pdata && pdata->init) {
		dev_dbg(pcdev->dev, "%s: Init gpios\n", __FUNCTION__);
		pdata->init(pcdev->dev);
	}

	if (pdata && pdata->power) {
		dev_dbg(pcdev->dev, "%s: Power on camera\n", __FUNCTION__);
		pdata->power(pcdev->dev, 1);
	}

	if (pdata && pdata->reset) {
		dev_dbg(pcdev->dev, "%s: Releasing camera reset\n",
			__FUNCTION__);
		pdata->reset(pcdev->dev, 1);
	}

	CICR0 = 0x3FF;   /* disable all interrupts */

	if (pcdev->platform_flags & PXA_CAMERA_PCLK_EN)
		cicr4 |= CICR4_PCLK_EN;
	if (pcdev->platform_flags & PXA_CAMERA_MCLK_EN)
		cicr4 |= CICR4_MCLK_EN;
	if (pcdev->platform_flags & PXA_CAMERA_PCP)
		cicr4 |= CICR4_PCP;
	if (pcdev->platform_flags & PXA_CAMERA_HSP)
		cicr4 |= CICR4_HSP;
	if (pcdev->platform_flags & PXA_CAMERA_VSP)
		cicr4 |= CICR4_VSP;

	CICR4 = mclk_get_divisor(pcdev) | cicr4;

	clk_enable(pcdev->clk);
}

static void pxa_camera_deactivate(struct pxa_camera_dev *pcdev)
{
	struct pxacamera_platform_data *board = pcdev->pdata;

	clk_disable(pcdev->clk);

	if (board && board->reset) {
		dev_dbg(pcdev->dev, "%s: Asserting camera reset\n",
			__FUNCTION__);
		board->reset(pcdev->dev, 0);
	}

	if (board && board->power) {
		dev_dbg(pcdev->dev, "%s: Power off camera\n", __FUNCTION__);
		board->power(pcdev->dev, 0);
	}
}

static irqreturn_t pxa_camera_irq(int irq, void *data)
{
	struct pxa_camera_dev *pcdev = data;
	unsigned int status = CISR;

	dev_dbg(pcdev->dev, "Camera interrupt status 0x%x\n", status);

	CISR = status;

	return IRQ_HANDLED;
}

/* The following two functions absolutely depend on the fact, that
 * there can be only one camera on PXA quick capture interface */
static int pxa_camera_add_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct pxa_camera_dev *pcdev = ici->priv;
	int ret;

	mutex_lock(&camera_lock);

	if (pcdev->icd) {
		ret = -EBUSY;
		goto ebusy;
	}

	dev_info(&icd->dev, "PXA Camera driver attached to camera %d\n",
		 icd->devnum);

	pxa_camera_activate(pcdev);
	ret = icd->ops->init(icd);

	if (!ret)
		pcdev->icd = icd;

ebusy:
	mutex_unlock(&camera_lock);

	return ret;
}

static void pxa_camera_remove_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct pxa_camera_dev *pcdev = ici->priv;

	BUG_ON(icd != pcdev->icd);

	dev_info(&icd->dev, "PXA Camera driver detached from camera %d\n",
		 icd->devnum);

	/* disable capture, disable interrupts */
	CICR0 = 0x3ff;
	/* Stop DMA engine */
	DCSR(pcdev->dma_chan_y) = 0;

	icd->ops->release(icd);

	pxa_camera_deactivate(pcdev);

	pcdev->icd = NULL;
}

static int pxa_camera_set_capture_format(struct soc_camera_device *icd,
					 __u32 pixfmt, struct v4l2_rect *rect)
{
	struct soc_camera_host *ici =
		to_soc_camera_host(icd->dev.parent);
	struct pxa_camera_dev *pcdev = ici->priv;
	unsigned int datawidth = 0, dw, bpp;
	u32 cicr0, cicr4 = 0;
	int ret;

	/* If requested data width is supported by the platform, use it */
	switch (icd->cached_datawidth) {
	case 10:
		if (pcdev->platform_flags & PXA_CAMERA_DATAWIDTH_10)
			datawidth = IS_DATAWIDTH_10;
		break;
	case 9:
		if (pcdev->platform_flags & PXA_CAMERA_DATAWIDTH_9)
			datawidth = IS_DATAWIDTH_9;
		break;
	case 8:
		if (pcdev->platform_flags & PXA_CAMERA_DATAWIDTH_8)
			datawidth = IS_DATAWIDTH_8;
	}
	if (!datawidth)
		return -EINVAL;

	ret = icd->ops->set_capture_format(icd, pixfmt, rect,
			datawidth |
			(pcdev->platform_flags & PXA_CAMERA_MASTER ?
			 IS_MASTER : 0) |
			(pcdev->platform_flags & PXA_CAMERA_HSP ?
			 0 : IS_HSYNC_ACTIVE_HIGH) |
			(pcdev->platform_flags & PXA_CAMERA_VSP ?
			 0 : IS_VSYNC_ACTIVE_HIGH) |
			(pcdev->platform_flags & PXA_CAMERA_PCP ?
			 0 : IS_PCLK_SAMPLE_RISING));
	if (ret < 0)
		return ret;

	/* Datawidth is now guaranteed to be equal to one of the three values.
	 * We fix bit-per-pixel equal to data-width... */
	switch (datawidth) {
	case IS_DATAWIDTH_10:
		icd->cached_datawidth = 10;
		dw = 4;
		bpp = 0x40;
		break;
	case IS_DATAWIDTH_9:
		icd->cached_datawidth = 9;
		dw = 3;
		bpp = 0x20;
		break;
	default:
		/* Actually it can only be 8 now,
		 * default is just to silence compiler warnings */
	case IS_DATAWIDTH_8:
		icd->cached_datawidth = 8;
		dw = 2;
		bpp = 0;
	}

	if (pcdev->platform_flags & PXA_CAMERA_PCLK_EN)
		cicr4 |= CICR4_PCLK_EN;
	if (pcdev->platform_flags & PXA_CAMERA_MCLK_EN)
		cicr4 |= CICR4_MCLK_EN;
	if (pcdev->platform_flags & PXA_CAMERA_PCP)
		cicr4 |= CICR4_PCP;
	if (pcdev->platform_flags & PXA_CAMERA_HSP)
		cicr4 |= CICR4_HSP;
	if (pcdev->platform_flags & PXA_CAMERA_VSP)
		cicr4 |= CICR4_VSP;

	cicr0 = CICR0;
	if (cicr0 & CICR0_ENB)
		CICR0 = cicr0 & ~CICR0_ENB;
	CICR1 = CICR1_PPL_VAL(rect->width - 1) | bpp | dw;
	CICR2 = 0;
	CICR3 = CICR3_LPF_VAL(rect->height - 1) |
		CICR3_BFW_VAL(min((unsigned short)255, icd->y_skip_top));
	CICR4 = mclk_get_divisor(pcdev) | cicr4;

	/* CIF interrupts are not used, only DMA */
	CICR0 = (pcdev->platform_flags & PXA_CAMERA_MASTER ?
		 CICR0_SIM_MP : (CICR0_SL_CAP_EN | CICR0_SIM_SP)) |
		CICR0_DMAEN | CICR0_IRQ_MASK | (cicr0 & CICR0_ENB);

	return 0;
}

static int pxa_camera_try_fmt_cap(struct soc_camera_host *ici,
				  struct v4l2_format *f)
{
	/* limit to pxa hardware capabilities */
	if (f->fmt.pix.height < 32)
		f->fmt.pix.height = 32;
	if (f->fmt.pix.height > 2048)
		f->fmt.pix.height = 2048;
	if (f->fmt.pix.width < 48)
		f->fmt.pix.width = 48;
	if (f->fmt.pix.width > 2048)
		f->fmt.pix.width = 2048;
	f->fmt.pix.width &= ~0x01;

	return 0;
}

static int pxa_camera_reqbufs(struct soc_camera_file *icf,
			      struct v4l2_requestbuffers *p)
{
	int i;

	/* This is for locking debugging only. I removed spinlocks and now I
	 * check whether .prepare is ever called on a linked buffer, or whether
	 * a dma IRQ can occur for an in-work or unlinked buffer. Until now
	 * it hadn't triggered */
	for (i = 0; i < p->count; i++) {
		struct pxa_buffer *buf = container_of(icf->vb_vidq.bufs[i],
						      struct pxa_buffer, vb);
		buf->inwork = 0;
		INIT_LIST_HEAD(&buf->vb.queue);
	}

	return 0;
}

static unsigned int pxa_camera_poll(struct file *file, poll_table *pt)
{
	struct soc_camera_file *icf = file->private_data;
	struct pxa_buffer *buf;

	buf = list_entry(icf->vb_vidq.stream.next, struct pxa_buffer,
			 vb.stream);

	poll_wait(file, &buf->vb.done, pt);

	if (buf->vb.state == VIDEOBUF_DONE ||
	    buf->vb.state == VIDEOBUF_ERROR)
		return POLLIN|POLLRDNORM;

	return 0;
}

static int pxa_camera_querycap(struct soc_camera_host *ici,
			       struct v4l2_capability *cap)
{
	/* cap->name is set by the firendly caller:-> */
	strlcpy(cap->card, pxa_cam_driver_description, sizeof(cap->card));
	cap->version = PXA_CAM_VERSION_CODE;
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

	return 0;
}

/* Should beallocated dynamically too, but we have only one. */
static struct soc_camera_host pxa_soc_camera_host = {
	.drv_name		= PXA_CAM_DRV_NAME,
	.vbq_ops		= &pxa_videobuf_ops,
	.add			= pxa_camera_add_device,
	.remove			= pxa_camera_remove_device,
	.msize			= sizeof(struct pxa_buffer),
	.set_capture_format	= pxa_camera_set_capture_format,
	.try_fmt_cap		= pxa_camera_try_fmt_cap,
	.reqbufs		= pxa_camera_reqbufs,
	.poll			= pxa_camera_poll,
	.querycap		= pxa_camera_querycap,
};

static int pxa_camera_probe(struct platform_device *pdev)
{
	struct pxa_camera_dev *pcdev;
	struct resource *res;
	void __iomem *base;
	unsigned int irq;
	int err = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!res || !irq) {
		err = -ENODEV;
		goto exit;
	}

	pcdev = kzalloc(sizeof(*pcdev), GFP_KERNEL);
	if (!pcdev) {
		dev_err(&pdev->dev, "Could not allocate pcdev\n");
		err = -ENOMEM;
		goto exit;
	}

	pcdev->clk = clk_get(&pdev->dev, "CAMCLK");
	if (IS_ERR(pcdev->clk)) {
		err = PTR_ERR(pcdev->clk);
		goto exit_kfree;
	}

	dev_set_drvdata(&pdev->dev, pcdev);
	pcdev->res = res;

	pcdev->pdata = pdev->dev.platform_data;
	pcdev->platform_flags = pcdev->pdata->flags;
	if (!pcdev->platform_flags & (PXA_CAMERA_DATAWIDTH_8 |
			PXA_CAMERA_DATAWIDTH_9 | PXA_CAMERA_DATAWIDTH_10)) {
		/* Platform hasn't set available data widths. This is bad.
		 * Warn and use a default. */
		dev_warn(&pdev->dev, "WARNING! Platform hasn't set available "
			 "data widths, using default 10 bit\n");
		pcdev->platform_flags |= PXA_CAMERA_DATAWIDTH_10;
	}
	pcdev->platform_mclk_10khz = pcdev->pdata->mclk_10khz;
	if (!pcdev->platform_mclk_10khz) {
		dev_warn(&pdev->dev,
			 "mclk_10khz == 0! Please, fix your platform data. "
			 "Using default 20MHz\n");
		pcdev->platform_mclk_10khz = 2000;
	}

	INIT_LIST_HEAD(&pcdev->capture);
	spin_lock_init(&pcdev->lock);

	/*
	 * Request the regions.
	 */
	if (!request_mem_region(res->start, res->end - res->start + 1,
				PXA_CAM_DRV_NAME)) {
		err = -EBUSY;
		goto exit_clk;
	}

	base = ioremap(res->start, res->end - res->start + 1);
	if (!base) {
		err = -ENOMEM;
		goto exit_release;
	}
	pcdev->irq = irq;
	pcdev->base = base;
	pcdev->dev = &pdev->dev;

	/* request dma */
	pcdev->dma_chan_y = pxa_request_dma("CI_Y", DMA_PRIO_HIGH,
					    pxa_camera_dma_irq_y, pcdev);
	if (pcdev->dma_chan_y < 0) {
		dev_err(pcdev->dev, "Can't request DMA for Y\n");
		err = -ENOMEM;
		goto exit_iounmap;
	}
	dev_dbg(pcdev->dev, "got DMA channel %d\n", pcdev->dma_chan_y);

	DRCMR68 = pcdev->dma_chan_y  | DRCMR_MAPVLD;

	/* request irq */
	err = request_irq(pcdev->irq, pxa_camera_irq, 0, PXA_CAM_DRV_NAME,
			  pcdev);
	if (err) {
		dev_err(pcdev->dev, "Camera interrupt register failed \n");
		goto exit_free_dma;
	}

	pxa_soc_camera_host.priv	= pcdev;
	pxa_soc_camera_host.dev.parent	= &pdev->dev;
	pxa_soc_camera_host.nr		= pdev->id;
	err = soc_camera_host_register(&pxa_soc_camera_host, THIS_MODULE);
	if (err)
		goto exit_free_irq;

	return 0;

exit_free_irq:
	free_irq(pcdev->irq, pcdev);
exit_free_dma:
	pxa_free_dma(pcdev->dma_chan_y);
exit_iounmap:
	iounmap(base);
exit_release:
	release_mem_region(res->start, res->end - res->start + 1);
exit_clk:
	clk_put(pcdev->clk);
exit_kfree:
	kfree(pcdev);
exit:
	return err;
}

static int __devexit pxa_camera_remove(struct platform_device *pdev)
{
	struct pxa_camera_dev *pcdev = platform_get_drvdata(pdev);
	struct resource *res;

	clk_put(pcdev->clk);

	pxa_free_dma(pcdev->dma_chan_y);
	free_irq(pcdev->irq, pcdev);

	soc_camera_host_unregister(&pxa_soc_camera_host);

	iounmap(pcdev->base);

	res = pcdev->res;
	release_mem_region(res->start, res->end - res->start + 1);

	kfree(pcdev);

	dev_info(&pdev->dev, "PXA Camera driver unloaded\n");

	return 0;
}

static struct platform_driver pxa_camera_driver = {
	.driver 	= {
		.name	= PXA_CAM_DRV_NAME,
	},
	.probe		= pxa_camera_probe,
	.remove		= __exit_p(pxa_camera_remove),
};


static int __devinit pxa_camera_init(void)
{
	return platform_driver_register(&pxa_camera_driver);
}

static void __exit pxa_camera_exit(void)
{
	return platform_driver_unregister(&pxa_camera_driver);
}

module_init(pxa_camera_init);
module_exit(pxa_camera_exit);

MODULE_DESCRIPTION("PXA27x SoC Camera Host driver");
MODULE_AUTHOR("Guennadi Liakhovetski <kernel@pengutronix.de>");
MODULE_LICENSE("GPL");
