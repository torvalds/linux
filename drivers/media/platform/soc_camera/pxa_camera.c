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
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/time.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dma/pxa-dma.h>

#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/videobuf-dma-sg.h>
#include <media/soc_camera.h>
#include <media/drv-intf/soc_mediabus.h>
#include <media/v4l2-of.h>

#include <linux/videodev2.h>

#include <linux/platform_data/media/camera-pxa.h>

#define PXA_CAM_VERSION "0.0.6"
#define PXA_CAM_DRV_NAME "pxa27x-camera"

/* Camera Interface */
#define CICR0		0x0000
#define CICR1		0x0004
#define CICR2		0x0008
#define CICR3		0x000C
#define CICR4		0x0010
#define CISR		0x0014
#define CIFR		0x0018
#define CITOR		0x001C
#define CIBR0		0x0028
#define CIBR1		0x0030
#define CIBR2		0x0038

#define CICR0_DMAEN	(1 << 31)	/* DMA request enable */
#define CICR0_PAR_EN	(1 << 30)	/* Parity enable */
#define CICR0_SL_CAP_EN	(1 << 29)	/* Capture enable for slave mode */
#define CICR0_ENB	(1 << 28)	/* Camera interface enable */
#define CICR0_DIS	(1 << 27)	/* Camera interface disable */
#define CICR0_SIM	(0x7 << 24)	/* Sensor interface mode mask */
#define CICR0_TOM	(1 << 9)	/* Time-out mask */
#define CICR0_RDAVM	(1 << 8)	/* Receive-data-available mask */
#define CICR0_FEM	(1 << 7)	/* FIFO-empty mask */
#define CICR0_EOLM	(1 << 6)	/* End-of-line mask */
#define CICR0_PERRM	(1 << 5)	/* Parity-error mask */
#define CICR0_QDM	(1 << 4)	/* Quick-disable mask */
#define CICR0_CDM	(1 << 3)	/* Disable-done mask */
#define CICR0_SOFM	(1 << 2)	/* Start-of-frame mask */
#define CICR0_EOFM	(1 << 1)	/* End-of-frame mask */
#define CICR0_FOM	(1 << 0)	/* FIFO-overrun mask */

#define CICR1_TBIT	(1 << 31)	/* Transparency bit */
#define CICR1_RGBT_CONV	(0x3 << 29)	/* RGBT conversion mask */
#define CICR1_PPL	(0x7ff << 15)	/* Pixels per line mask */
#define CICR1_RGB_CONV	(0x7 << 12)	/* RGB conversion mask */
#define CICR1_RGB_F	(1 << 11)	/* RGB format */
#define CICR1_YCBCR_F	(1 << 10)	/* YCbCr format */
#define CICR1_RGB_BPP	(0x7 << 7)	/* RGB bis per pixel mask */
#define CICR1_RAW_BPP	(0x3 << 5)	/* Raw bis per pixel mask */
#define CICR1_COLOR_SP	(0x3 << 3)	/* Color space mask */
#define CICR1_DW	(0x7 << 0)	/* Data width mask */

#define CICR2_BLW	(0xff << 24)	/* Beginning-of-line pixel clock
					   wait count mask */
#define CICR2_ELW	(0xff << 16)	/* End-of-line pixel clock
					   wait count mask */
#define CICR2_HSW	(0x3f << 10)	/* Horizontal sync pulse width mask */
#define CICR2_BFPW	(0x3f << 3)	/* Beginning-of-frame pixel clock
					   wait count mask */
#define CICR2_FSW	(0x7 << 0)	/* Frame stabilization
					   wait count mask */

#define CICR3_BFW	(0xff << 24)	/* Beginning-of-frame line clock
					   wait count mask */
#define CICR3_EFW	(0xff << 16)	/* End-of-frame line clock
					   wait count mask */
#define CICR3_VSW	(0x3f << 10)	/* Vertical sync pulse width mask */
#define CICR3_BFPW	(0x3f << 3)	/* Beginning-of-frame pixel clock
					   wait count mask */
#define CICR3_LPF	(0x7ff << 0)	/* Lines per frame mask */

#define CICR4_MCLK_DLY	(0x3 << 24)	/* MCLK Data Capture Delay mask */
#define CICR4_PCLK_EN	(1 << 23)	/* Pixel clock enable */
#define CICR4_PCP	(1 << 22)	/* Pixel clock polarity */
#define CICR4_HSP	(1 << 21)	/* Horizontal sync polarity */
#define CICR4_VSP	(1 << 20)	/* Vertical sync polarity */
#define CICR4_MCLK_EN	(1 << 19)	/* MCLK enable */
#define CICR4_FR_RATE	(0x7 << 8)	/* Frame rate mask */
#define CICR4_DIV	(0xff << 0)	/* Clock divisor mask */

#define CISR_FTO	(1 << 15)	/* FIFO time-out */
#define CISR_RDAV_2	(1 << 14)	/* Channel 2 receive data available */
#define CISR_RDAV_1	(1 << 13)	/* Channel 1 receive data available */
#define CISR_RDAV_0	(1 << 12)	/* Channel 0 receive data available */
#define CISR_FEMPTY_2	(1 << 11)	/* Channel 2 FIFO empty */
#define CISR_FEMPTY_1	(1 << 10)	/* Channel 1 FIFO empty */
#define CISR_FEMPTY_0	(1 << 9)	/* Channel 0 FIFO empty */
#define CISR_EOL	(1 << 8)	/* End of line */
#define CISR_PAR_ERR	(1 << 7)	/* Parity error */
#define CISR_CQD	(1 << 6)	/* Camera interface quick disable */
#define CISR_CDD	(1 << 5)	/* Camera interface disable done */
#define CISR_SOF	(1 << 4)	/* Start of frame */
#define CISR_EOF	(1 << 3)	/* End of frame */
#define CISR_IFO_2	(1 << 2)	/* FIFO overrun for Channel 2 */
#define CISR_IFO_1	(1 << 1)	/* FIFO overrun for Channel 1 */
#define CISR_IFO_0	(1 << 0)	/* FIFO overrun for Channel 0 */

#define CIFR_FLVL2	(0x7f << 23)	/* FIFO 2 level mask */
#define CIFR_FLVL1	(0x7f << 16)	/* FIFO 1 level mask */
#define CIFR_FLVL0	(0xff << 8)	/* FIFO 0 level mask */
#define CIFR_THL_0	(0x3 << 4)	/* Threshold Level for Channel 0 FIFO */
#define CIFR_RESET_F	(1 << 3)	/* Reset input FIFOs */
#define CIFR_FEN2	(1 << 2)	/* FIFO enable for channel 2 */
#define CIFR_FEN1	(1 << 1)	/* FIFO enable for channel 1 */
#define CIFR_FEN0	(1 << 0)	/* FIFO enable for channel 0 */

#define CICR0_SIM_MP	(0 << 24)
#define CICR0_SIM_SP	(1 << 24)
#define CICR0_SIM_MS	(2 << 24)
#define CICR0_SIM_EP	(3 << 24)
#define CICR0_SIM_ES	(4 << 24)

#define CICR1_DW_VAL(x)   ((x) & CICR1_DW)	    /* Data bus width */
#define CICR1_PPL_VAL(x)  (((x) << 15) & CICR1_PPL) /* Pixels per line */
#define CICR1_COLOR_SP_VAL(x)	(((x) << 3) & CICR1_COLOR_SP)	/* color space */
#define CICR1_RGB_BPP_VAL(x)	(((x) << 7) & CICR1_RGB_BPP)	/* bpp for rgb */
#define CICR1_RGBT_CONV_VAL(x)	(((x) << 29) & CICR1_RGBT_CONV)	/* rgbt conv */

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

/*
 * Structures
 */
enum pxa_camera_active_dma {
	DMA_Y = 0x1,
	DMA_U = 0x2,
	DMA_V = 0x4,
};

/* buffer for one video frame */
struct pxa_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer		vb;
	u32	code;
	/* our descriptor lists for Y, U and V channels */
	struct dma_async_tx_descriptor	*descs[3];
	dma_cookie_t			cookie[3];
	struct scatterlist		*sg[3];
	int				sg_len[3];
	int				inwork;
	enum pxa_camera_active_dma	active_dma;
};

struct pxa_camera_dev {
	struct soc_camera_host	soc_host;
	/*
	 * PXA27x is only supposed to handle one camera on its Quick Capture
	 * interface. If anyone ever builds hardware to enable more than
	 * one camera, they will have to modify this driver too
	 */
	struct clk		*clk;

	unsigned int		irq;
	void __iomem		*base;

	int			channels;
	struct dma_chan		*dma_chans[3];

	struct pxacamera_platform_data *pdata;
	struct resource		*res;
	unsigned long		platform_flags;
	unsigned long		ciclk;
	unsigned long		mclk;
	u32			mclk_divisor;
	u16			width_flags;	/* max 10 bits */

	struct list_head	capture;

	spinlock_t		lock;

	struct pxa_buffer	*active;
	struct tasklet_struct	task_eof;

	u32			save_cicr[5];
};

struct pxa_cam {
	unsigned long flags;
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

	dev_dbg(icd->parent, "count=%d, size=%d\n", *count, *size);

	*size = icd->sizeimage;

	if (0 == *count)
		*count = 32;
	if (*size * *count > vid_limit * 1024 * 1024)
		*count = (vid_limit * 1024 * 1024) / *size;

	return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct pxa_buffer *buf)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct videobuf_dmabuf *dma = videobuf_to_dma(&buf->vb);
	int i;

	BUG_ON(in_interrupt());

	dev_dbg(icd->parent, "%s (vb=0x%p) 0x%08lx %d\n", __func__,
		&buf->vb, buf->vb.baddr, buf->vb.bsize);

	/*
	 * This waits until this buffer is out of danger, i.e., until it is no
	 * longer in STATE_QUEUED or STATE_ACTIVE
	 */
	videobuf_waiton(vq, &buf->vb, 0, 0);

	for (i = 0; i < 3 && buf->descs[i]; i++) {
		dmaengine_desc_free(buf->descs[i]);
		kfree(buf->sg[i]);
		buf->descs[i] = NULL;
		buf->sg[i] = NULL;
		buf->sg_len[i] = 0;
	}
	videobuf_dma_unmap(vq->dev, dma);
	videobuf_dma_free(dma);

	buf->vb.state = VIDEOBUF_NEEDS_INIT;

	dev_dbg(icd->parent, "%s end (vb=0x%p) 0x%08lx %d\n", __func__,
		&buf->vb, buf->vb.baddr, buf->vb.bsize);
}

static void pxa_camera_dma_irq(struct pxa_camera_dev *pcdev,
			       enum pxa_camera_active_dma act_dma);

static void pxa_camera_dma_irq_y(void *data)
{
	struct pxa_camera_dev *pcdev = data;

	pxa_camera_dma_irq(pcdev, DMA_Y);
}

static void pxa_camera_dma_irq_u(void *data)
{
	struct pxa_camera_dev *pcdev = data;

	pxa_camera_dma_irq(pcdev, DMA_U);
}

static void pxa_camera_dma_irq_v(void *data)
{
	struct pxa_camera_dev *pcdev = data;

	pxa_camera_dma_irq(pcdev, DMA_V);
}

/**
 * pxa_init_dma_channel - init dma descriptors
 * @pcdev: pxa camera device
 * @buf: pxa buffer to find pxa dma channel
 * @dma: dma video buffer
 * @channel: dma channel (0 => 'Y', 1 => 'U', 2 => 'V')
 * @cibr: camera Receive Buffer Register
 * @size: bytes to transfer
 * @offset: offset in videobuffer of the first byte to transfer
 *
 * Prepares the pxa dma descriptors to transfer one camera channel.
 *
 * Returns 0 if success or -ENOMEM if no memory is available
 */
static int pxa_init_dma_channel(struct pxa_camera_dev *pcdev,
				struct pxa_buffer *buf,
				struct videobuf_dmabuf *dma, int channel,
				int cibr, int size, int offset)
{
	struct dma_chan *dma_chan = pcdev->dma_chans[channel];
	struct scatterlist *sg = buf->sg[channel];
	int sglen = buf->sg_len[channel];
	struct dma_async_tx_descriptor *tx;

	tx = dmaengine_prep_slave_sg(dma_chan, sg, sglen, DMA_DEV_TO_MEM,
				     DMA_PREP_INTERRUPT | DMA_CTRL_REUSE);
	if (!tx) {
		dev_err(pcdev->soc_host.v4l2_dev.dev,
			"dmaengine_prep_slave_sg failed\n");
		goto fail;
	}

	tx->callback_param = pcdev;
	switch (channel) {
	case 0:
		tx->callback = pxa_camera_dma_irq_y;
		break;
	case 1:
		tx->callback = pxa_camera_dma_irq_u;
		break;
	case 2:
		tx->callback = pxa_camera_dma_irq_v;
		break;
	}

	buf->descs[channel] = tx;
	return 0;
fail:
	kfree(sg);

	dev_dbg(pcdev->soc_host.v4l2_dev.dev,
		"%s (vb=0x%p) dma_tx=%p\n",
		__func__, &buf->vb, tx);

	return -ENOMEM;
}

static void pxa_videobuf_set_actdma(struct pxa_camera_dev *pcdev,
				    struct pxa_buffer *buf)
{
	buf->active_dma = DMA_Y;
	if (pcdev->channels == 3)
		buf->active_dma |= DMA_U | DMA_V;
}

/*
 * Please check the DMA prepared buffer structure in :
 *   Documentation/video4linux/pxa_camera.txt
 * Please check also in pxa_camera_check_link_miss() to understand why DMA chain
 * modification while DMA chain is running will work anyway.
 */
static int pxa_videobuf_prepare(struct videobuf_queue *vq,
		struct videobuf_buffer *vb, enum v4l2_field field)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct pxa_camera_dev *pcdev = ici->priv;
	struct device *dev = pcdev->soc_host.v4l2_dev.dev;
	struct pxa_buffer *buf = container_of(vb, struct pxa_buffer, vb);
	int ret;
	int size_y, size_u = 0, size_v = 0;
	size_t sizes[3];

	dev_dbg(dev, "%s (vb=0x%p) 0x%08lx %d\n", __func__,
		vb, vb->baddr, vb->bsize);

	/* Added list head initialization on alloc */
	WARN_ON(!list_empty(&vb->queue));

#ifdef DEBUG
	/*
	 * This can be useful if you want to see if we actually fill
	 * the buffer with something
	 */
	memset((void *)vb->baddr, 0xaa, vb->bsize);
#endif

	BUG_ON(NULL == icd->current_fmt);

	/*
	 * I think, in buf_prepare you only have to protect global data,
	 * the actual buffer is yours
	 */
	buf->inwork = 1;

	if (buf->code	!= icd->current_fmt->code ||
	    vb->width	!= icd->user_width ||
	    vb->height	!= icd->user_height ||
	    vb->field	!= field) {
		buf->code	= icd->current_fmt->code;
		vb->width	= icd->user_width;
		vb->height	= icd->user_height;
		vb->field	= field;
		vb->state	= VIDEOBUF_NEEDS_INIT;
	}

	vb->size = icd->sizeimage;
	if (0 != vb->baddr && vb->bsize < vb->size) {
		ret = -EINVAL;
		goto out;
	}

	if (vb->state == VIDEOBUF_NEEDS_INIT) {
		int size = vb->size;
		struct videobuf_dmabuf *dma = videobuf_to_dma(vb);

		ret = videobuf_iolock(vq, vb, NULL);
		if (ret)
			goto out;

		if (pcdev->channels == 3) {
			size_y = size / 2;
			size_u = size_v = size / 4;
		} else {
			size_y = size;
		}

		sizes[0] = size_y;
		sizes[1] = size_u;
		sizes[2] = size_v;
		ret = sg_split(dma->sglist, dma->sglen, 0, pcdev->channels,
			       sizes, buf->sg, buf->sg_len, GFP_KERNEL);
		if (ret < 0) {
			dev_err(dev, "sg_split failed: %d\n", ret);
			goto fail;
		}

		/* init DMA for Y channel */
		ret = pxa_init_dma_channel(pcdev, buf, dma, 0, CIBR0,
					   size_y, 0);
		if (ret) {
			dev_err(dev, "DMA initialization for Y/RGB failed\n");
			goto fail;
		}

		/* init DMA for U channel */
		if (size_u)
			ret = pxa_init_dma_channel(pcdev, buf, dma, 1, CIBR1,
						   size_u, size_y);
		if (ret) {
			dev_err(dev, "DMA initialization for U failed\n");
			goto fail;
		}

		/* init DMA for V channel */
		if (size_v)
			ret = pxa_init_dma_channel(pcdev, buf, dma, 2, CIBR2,
						   size_v, size_y + size_u);
		if (ret) {
			dev_err(dev, "DMA initialization for V failed\n");
			goto fail;
		}

		vb->state = VIDEOBUF_PREPARED;
	}

	buf->inwork = 0;
	pxa_videobuf_set_actdma(pcdev, buf);

	return 0;

fail:
	free_buffer(vq, buf);
out:
	buf->inwork = 0;
	return ret;
}

/**
 * pxa_dma_start_channels - start DMA channel for active buffer
 * @pcdev: pxa camera device
 *
 * Initialize DMA channels to the beginning of the active video buffer, and
 * start these channels.
 */
static void pxa_dma_start_channels(struct pxa_camera_dev *pcdev)
{
	int i;
	struct pxa_buffer *active;

	active = pcdev->active;

	for (i = 0; i < pcdev->channels; i++) {
		dev_dbg(pcdev->soc_host.v4l2_dev.dev,
			"%s (channel=%d)\n", __func__, i);
		dma_async_issue_pending(pcdev->dma_chans[i]);
	}
}

static void pxa_dma_stop_channels(struct pxa_camera_dev *pcdev)
{
	int i;

	for (i = 0; i < pcdev->channels; i++) {
		dev_dbg(pcdev->soc_host.v4l2_dev.dev,
			"%s (channel=%d)\n", __func__, i);
		dmaengine_terminate_all(pcdev->dma_chans[i]);
	}
}

static void pxa_dma_add_tail_buf(struct pxa_camera_dev *pcdev,
				 struct pxa_buffer *buf)
{
	int i;

	for (i = 0; i < pcdev->channels; i++) {
		buf->cookie[i] = dmaengine_submit(buf->descs[i]);
		dev_dbg(pcdev->soc_host.v4l2_dev.dev,
			"%s (channel=%d) : submit vb=%p cookie=%d\n",
			__func__, i, buf, buf->descs[i]->cookie);
	}
}

/**
 * pxa_camera_start_capture - start video capturing
 * @pcdev: camera device
 *
 * Launch capturing. DMA channels should not be active yet. They should get
 * activated at the end of frame interrupt, to capture only whole frames, and
 * never begin the capture of a partial frame.
 */
static void pxa_camera_start_capture(struct pxa_camera_dev *pcdev)
{
	unsigned long cicr0;

	dev_dbg(pcdev->soc_host.v4l2_dev.dev, "%s\n", __func__);
	__raw_writel(__raw_readl(pcdev->base + CISR), pcdev->base + CISR);
	/* Enable End-Of-Frame Interrupt */
	cicr0 = __raw_readl(pcdev->base + CICR0) | CICR0_ENB;
	cicr0 &= ~CICR0_EOFM;
	__raw_writel(cicr0, pcdev->base + CICR0);
}

static void pxa_camera_stop_capture(struct pxa_camera_dev *pcdev)
{
	unsigned long cicr0;

	pxa_dma_stop_channels(pcdev);

	cicr0 = __raw_readl(pcdev->base + CICR0) & ~CICR0_ENB;
	__raw_writel(cicr0, pcdev->base + CICR0);

	pcdev->active = NULL;
	dev_dbg(pcdev->soc_host.v4l2_dev.dev, "%s\n", __func__);
}

/* Called under spinlock_irqsave(&pcdev->lock, ...) */
static void pxa_videobuf_queue(struct videobuf_queue *vq,
			       struct videobuf_buffer *vb)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct pxa_camera_dev *pcdev = ici->priv;
	struct pxa_buffer *buf = container_of(vb, struct pxa_buffer, vb);

	dev_dbg(icd->parent, "%s (vb=0x%p) 0x%08lx %d active=%p\n",
		__func__, vb, vb->baddr, vb->bsize, pcdev->active);

	list_add_tail(&vb->queue, &pcdev->capture);

	vb->state = VIDEOBUF_ACTIVE;
	pxa_dma_add_tail_buf(pcdev, buf);

	if (!pcdev->active)
		pxa_camera_start_capture(pcdev);
}

static void pxa_videobuf_release(struct videobuf_queue *vq,
				 struct videobuf_buffer *vb)
{
	struct pxa_buffer *buf = container_of(vb, struct pxa_buffer, vb);
#ifdef DEBUG
	struct soc_camera_device *icd = vq->priv_data;
	struct device *dev = icd->parent;

	dev_dbg(dev, "%s (vb=0x%p) 0x%08lx %d\n", __func__,
		vb, vb->baddr, vb->bsize);

	switch (vb->state) {
	case VIDEOBUF_ACTIVE:
		dev_dbg(dev, "%s (active)\n", __func__);
		break;
	case VIDEOBUF_QUEUED:
		dev_dbg(dev, "%s (queued)\n", __func__);
		break;
	case VIDEOBUF_PREPARED:
		dev_dbg(dev, "%s (prepared)\n", __func__);
		break;
	default:
		dev_dbg(dev, "%s (unknown)\n", __func__);
		break;
	}
#endif

	free_buffer(vq, buf);
}

static void pxa_camera_wakeup(struct pxa_camera_dev *pcdev,
			      struct videobuf_buffer *vb,
			      struct pxa_buffer *buf)
{
	/* _init is used to debug races, see comment in pxa_camera_reqbufs() */
	list_del_init(&vb->queue);
	vb->state = VIDEOBUF_DONE;
	v4l2_get_timestamp(&vb->ts);
	vb->field_count++;
	wake_up(&vb->done);
	dev_dbg(pcdev->soc_host.v4l2_dev.dev, "%s dequeud buffer (vb=0x%p)\n",
		__func__, vb);

	if (list_empty(&pcdev->capture)) {
		pxa_camera_stop_capture(pcdev);
		return;
	}

	pcdev->active = list_entry(pcdev->capture.next,
				   struct pxa_buffer, vb.queue);
}

/**
 * pxa_camera_check_link_miss - check missed DMA linking
 * @pcdev: camera device
 *
 * The DMA chaining is done with DMA running. This means a tiny temporal window
 * remains, where a buffer is queued on the chain, while the chain is already
 * stopped. This means the tailed buffer would never be transferred by DMA.
 * This function restarts the capture for this corner case, where :
 *  - DADR() == DADDR_STOP
 *  - a videobuffer is queued on the pcdev->capture list
 *
 * Please check the "DMA hot chaining timeslice issue" in
 *   Documentation/video4linux/pxa_camera.txt
 *
 * Context: should only be called within the dma irq handler
 */
static void pxa_camera_check_link_miss(struct pxa_camera_dev *pcdev,
				       dma_cookie_t last_submitted,
				       dma_cookie_t last_issued)
{
	bool is_dma_stopped = last_submitted != last_issued;

	dev_dbg(pcdev->soc_host.v4l2_dev.dev,
		"%s : top queued buffer=%p, is_dma_stopped=%d\n",
		__func__, pcdev->active, is_dma_stopped);

	if (pcdev->active && is_dma_stopped)
		pxa_camera_start_capture(pcdev);
}

static void pxa_camera_dma_irq(struct pxa_camera_dev *pcdev,
			       enum pxa_camera_active_dma act_dma)
{
	struct device *dev = pcdev->soc_host.v4l2_dev.dev;
	struct pxa_buffer *buf, *last_buf;
	unsigned long flags;
	u32 camera_status, overrun;
	int chan;
	struct videobuf_buffer *vb;
	enum dma_status last_status;
	dma_cookie_t last_issued;

	spin_lock_irqsave(&pcdev->lock, flags);

	camera_status = __raw_readl(pcdev->base + CISR);
	dev_dbg(dev, "camera dma irq, cisr=0x%x dma=%d\n",
		camera_status, act_dma);
	overrun = CISR_IFO_0;
	if (pcdev->channels == 3)
		overrun |= CISR_IFO_1 | CISR_IFO_2;

	/*
	 * pcdev->active should not be NULL in DMA irq handler.
	 *
	 * But there is one corner case : if capture was stopped due to an
	 * overrun of channel 1, and at that same channel 2 was completed.
	 *
	 * When handling the overrun in DMA irq for channel 1, we'll stop the
	 * capture and restart it (and thus set pcdev->active to NULL). But the
	 * DMA irq handler will already be pending for channel 2. So on entering
	 * the DMA irq handler for channel 2 there will be no active buffer, yet
	 * that is normal.
	 */
	if (!pcdev->active)
		goto out;

	vb = &pcdev->active->vb;
	buf = container_of(vb, struct pxa_buffer, vb);
	WARN_ON(buf->inwork || list_empty(&vb->queue));

	/*
	 * It's normal if the last frame creates an overrun, as there
	 * are no more DMA descriptors to fetch from QCI fifos
	 */
	switch (act_dma) {
	case DMA_U:
		chan = 1;
		break;
	case DMA_V:
		chan = 2;
		break;
	default:
		chan = 0;
		break;
	}
	last_buf = list_entry(pcdev->capture.prev,
			      struct pxa_buffer, vb.queue);
	last_status = dma_async_is_tx_complete(pcdev->dma_chans[chan],
					       last_buf->cookie[chan],
					       NULL, &last_issued);
	if (camera_status & overrun &&
	    last_status != DMA_COMPLETE) {
		dev_dbg(dev, "FIFO overrun! CISR: %x\n",
			camera_status);
		pxa_camera_stop_capture(pcdev);
		list_for_each_entry(buf, &pcdev->capture, vb.queue)
			pxa_dma_add_tail_buf(pcdev, buf);
		pxa_camera_start_capture(pcdev);
		goto out;
	}
	buf->active_dma &= ~act_dma;
	if (!buf->active_dma) {
		pxa_camera_wakeup(pcdev, vb, buf);
		pxa_camera_check_link_miss(pcdev, last_buf->cookie[chan],
					   last_issued);
	}

out:
	spin_unlock_irqrestore(&pcdev->lock, flags);
}

static struct videobuf_queue_ops pxa_videobuf_ops = {
	.buf_setup      = pxa_videobuf_setup,
	.buf_prepare    = pxa_videobuf_prepare,
	.buf_queue      = pxa_videobuf_queue,
	.buf_release    = pxa_videobuf_release,
};

static void pxa_camera_init_videobuf(struct videobuf_queue *q,
			      struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct pxa_camera_dev *pcdev = ici->priv;

	/*
	 * We must pass NULL as dev pointer, then all pci_* dma operations
	 * transform to normal dma_* ones.
	 */
	videobuf_queue_sg_init(q, &pxa_videobuf_ops, NULL, &pcdev->lock,
				V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_FIELD_NONE,
				sizeof(struct pxa_buffer), icd, &ici->host_lock);
}

static u32 mclk_get_divisor(struct platform_device *pdev,
			    struct pxa_camera_dev *pcdev)
{
	unsigned long mclk = pcdev->mclk;
	struct device *dev = &pdev->dev;
	u32 div;
	unsigned long lcdclk;

	lcdclk = clk_get_rate(pcdev->clk);
	pcdev->ciclk = lcdclk;

	/* mclk <= ciclk / 4 (27.4.2) */
	if (mclk > lcdclk / 4) {
		mclk = lcdclk / 4;
		dev_warn(dev, "Limiting master clock to %lu\n", mclk);
	}

	/* We verify mclk != 0, so if anyone breaks it, here comes their Oops */
	div = (lcdclk + 2 * mclk - 1) / (2 * mclk) - 1;

	/* If we're not supplying MCLK, leave it at 0 */
	if (pcdev->platform_flags & PXA_CAMERA_MCLK_EN)
		pcdev->mclk = lcdclk / (2 * (div + 1));

	dev_dbg(dev, "LCD clock %luHz, target freq %luHz, divisor %u\n",
		lcdclk, mclk, div);

	return div;
}

static void recalculate_fifo_timeout(struct pxa_camera_dev *pcdev,
				     unsigned long pclk)
{
	/* We want a timeout > 1 pixel time, not ">=" */
	u32 ciclk_per_pixel = pcdev->ciclk / pclk + 1;

	__raw_writel(ciclk_per_pixel, pcdev->base + CITOR);
}

static void pxa_camera_activate(struct pxa_camera_dev *pcdev)
{
	u32 cicr4 = 0;

	/* disable all interrupts */
	__raw_writel(0x3ff, pcdev->base + CICR0);

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

	__raw_writel(pcdev->mclk_divisor | cicr4, pcdev->base + CICR4);

	if (pcdev->platform_flags & PXA_CAMERA_MCLK_EN)
		/* Initialise the timeout under the assumption pclk = mclk */
		recalculate_fifo_timeout(pcdev, pcdev->mclk);
	else
		/* "Safe default" - 13MHz */
		recalculate_fifo_timeout(pcdev, 13000000);

	clk_prepare_enable(pcdev->clk);
}

static void pxa_camera_deactivate(struct pxa_camera_dev *pcdev)
{
	clk_disable_unprepare(pcdev->clk);
}

static void pxa_camera_eof(unsigned long arg)
{
	struct pxa_camera_dev *pcdev = (struct pxa_camera_dev *)arg;
	unsigned long cifr;
	struct pxa_buffer *buf;
	struct videobuf_buffer *vb;

	dev_dbg(pcdev->soc_host.v4l2_dev.dev,
		"Camera interrupt status 0x%x\n",
		__raw_readl(pcdev->base + CISR));

	/* Reset the FIFOs */
	cifr = __raw_readl(pcdev->base + CIFR) | CIFR_RESET_F;
	__raw_writel(cifr, pcdev->base + CIFR);

	pcdev->active = list_first_entry(&pcdev->capture,
					 struct pxa_buffer, vb.queue);
	vb = &pcdev->active->vb;
	buf = container_of(vb, struct pxa_buffer, vb);
	pxa_videobuf_set_actdma(pcdev, buf);

	pxa_dma_start_channels(pcdev);
}

static irqreturn_t pxa_camera_irq(int irq, void *data)
{
	struct pxa_camera_dev *pcdev = data;
	unsigned long status, cicr0;

	status = __raw_readl(pcdev->base + CISR);
	dev_dbg(pcdev->soc_host.v4l2_dev.dev,
		"Camera interrupt status 0x%lx\n", status);

	if (!status)
		return IRQ_NONE;

	__raw_writel(status, pcdev->base + CISR);

	if (status & CISR_EOF) {
		cicr0 = __raw_readl(pcdev->base + CICR0) | CICR0_EOFM;
		__raw_writel(cicr0, pcdev->base + CICR0);
		tasklet_schedule(&pcdev->task_eof);
	}

	return IRQ_HANDLED;
}

static int pxa_camera_add_device(struct soc_camera_device *icd)
{
	dev_info(icd->parent, "PXA Camera driver attached to camera %d\n",
		 icd->devnum);

	return 0;
}

static void pxa_camera_remove_device(struct soc_camera_device *icd)
{
	dev_info(icd->parent, "PXA Camera driver detached from camera %d\n",
		 icd->devnum);
}

/*
 * The following two functions absolutely depend on the fact, that
 * there can be only one camera on PXA quick capture interface
 * Called with .host_lock held
 */
static int pxa_camera_clock_start(struct soc_camera_host *ici)
{
	struct pxa_camera_dev *pcdev = ici->priv;

	pxa_camera_activate(pcdev);

	return 0;
}

/* Called with .host_lock held */
static void pxa_camera_clock_stop(struct soc_camera_host *ici)
{
	struct pxa_camera_dev *pcdev = ici->priv;

	/* disable capture, disable interrupts */
	__raw_writel(0x3ff, pcdev->base + CICR0);

	/* Stop DMA engine */
	pxa_dma_stop_channels(pcdev);
	pxa_camera_deactivate(pcdev);
}

static int test_platform_param(struct pxa_camera_dev *pcdev,
			       unsigned char buswidth, unsigned long *flags)
{
	/*
	 * Platform specified synchronization and pixel clock polarities are
	 * only a recommendation and are only used during probing. The PXA270
	 * quick capture interface supports both.
	 */
	*flags = (pcdev->platform_flags & PXA_CAMERA_MASTER ?
		  V4L2_MBUS_MASTER : V4L2_MBUS_SLAVE) |
		V4L2_MBUS_HSYNC_ACTIVE_HIGH |
		V4L2_MBUS_HSYNC_ACTIVE_LOW |
		V4L2_MBUS_VSYNC_ACTIVE_HIGH |
		V4L2_MBUS_VSYNC_ACTIVE_LOW |
		V4L2_MBUS_DATA_ACTIVE_HIGH |
		V4L2_MBUS_PCLK_SAMPLE_RISING |
		V4L2_MBUS_PCLK_SAMPLE_FALLING;

	/* If requested data width is supported by the platform, use it */
	if ((1 << (buswidth - 1)) & pcdev->width_flags)
		return 0;

	return -EINVAL;
}

static void pxa_camera_setup_cicr(struct soc_camera_device *icd,
				  unsigned long flags, __u32 pixfmt)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct pxa_camera_dev *pcdev = ici->priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	unsigned long dw, bpp;
	u32 cicr0, cicr1, cicr2, cicr3, cicr4 = 0, y_skip_top;
	int ret = v4l2_subdev_call(sd, sensor, g_skip_top_lines, &y_skip_top);

	if (ret < 0)
		y_skip_top = 0;

	/*
	 * Datawidth is now guaranteed to be equal to one of the three values.
	 * We fix bit-per-pixel equal to data-width...
	 */
	switch (icd->current_fmt->host_fmt->bits_per_sample) {
	case 10:
		dw = 4;
		bpp = 0x40;
		break;
	case 9:
		dw = 3;
		bpp = 0x20;
		break;
	default:
		/*
		 * Actually it can only be 8 now,
		 * default is just to silence compiler warnings
		 */
	case 8:
		dw = 2;
		bpp = 0;
	}

	if (pcdev->platform_flags & PXA_CAMERA_PCLK_EN)
		cicr4 |= CICR4_PCLK_EN;
	if (pcdev->platform_flags & PXA_CAMERA_MCLK_EN)
		cicr4 |= CICR4_MCLK_EN;
	if (flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)
		cicr4 |= CICR4_PCP;
	if (flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)
		cicr4 |= CICR4_HSP;
	if (flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)
		cicr4 |= CICR4_VSP;

	cicr0 = __raw_readl(pcdev->base + CICR0);
	if (cicr0 & CICR0_ENB)
		__raw_writel(cicr0 & ~CICR0_ENB, pcdev->base + CICR0);

	cicr1 = CICR1_PPL_VAL(icd->user_width - 1) | bpp | dw;

	switch (pixfmt) {
	case V4L2_PIX_FMT_YUV422P:
		pcdev->channels = 3;
		cicr1 |= CICR1_YCBCR_F;
		/*
		 * Normally, pxa bus wants as input UYVY format. We allow all
		 * reorderings of the YUV422 format, as no processing is done,
		 * and the YUV stream is just passed through without any
		 * transformation. Note that UYVY is the only format that
		 * should be used if pxa framebuffer Overlay2 is used.
		 */
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
		cicr1 |= CICR1_COLOR_SP_VAL(2);
		break;
	case V4L2_PIX_FMT_RGB555:
		cicr1 |= CICR1_RGB_BPP_VAL(1) | CICR1_RGBT_CONV_VAL(2) |
			CICR1_TBIT | CICR1_COLOR_SP_VAL(1);
		break;
	case V4L2_PIX_FMT_RGB565:
		cicr1 |= CICR1_COLOR_SP_VAL(1) | CICR1_RGB_BPP_VAL(2);
		break;
	}

	cicr2 = 0;
	cicr3 = CICR3_LPF_VAL(icd->user_height - 1) |
		CICR3_BFW_VAL(min((u32)255, y_skip_top));
	cicr4 |= pcdev->mclk_divisor;

	__raw_writel(cicr1, pcdev->base + CICR1);
	__raw_writel(cicr2, pcdev->base + CICR2);
	__raw_writel(cicr3, pcdev->base + CICR3);
	__raw_writel(cicr4, pcdev->base + CICR4);

	/* CIF interrupts are not used, only DMA */
	cicr0 = (cicr0 & CICR0_ENB) | (pcdev->platform_flags & PXA_CAMERA_MASTER ?
		CICR0_SIM_MP : (CICR0_SL_CAP_EN | CICR0_SIM_SP));
	cicr0 |= CICR0_DMAEN | CICR0_IRQ_MASK;
	__raw_writel(cicr0, pcdev->base + CICR0);
}

static int pxa_camera_set_bus_param(struct soc_camera_device *icd)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct pxa_camera_dev *pcdev = ici->priv;
	struct v4l2_mbus_config cfg = {.type = V4L2_MBUS_PARALLEL,};
	u32 pixfmt = icd->current_fmt->host_fmt->fourcc;
	unsigned long bus_flags, common_flags;
	int ret;
	struct pxa_cam *cam = icd->host_priv;

	ret = test_platform_param(pcdev, icd->current_fmt->host_fmt->bits_per_sample,
				  &bus_flags);
	if (ret < 0)
		return ret;

	ret = v4l2_subdev_call(sd, video, g_mbus_config, &cfg);
	if (!ret) {
		common_flags = soc_mbus_config_compatible(&cfg,
							  bus_flags);
		if (!common_flags) {
			dev_warn(icd->parent,
				 "Flags incompatible: camera 0x%x, host 0x%lx\n",
				 cfg.flags, bus_flags);
			return -EINVAL;
		}
	} else if (ret != -ENOIOCTLCMD) {
		return ret;
	} else {
		common_flags = bus_flags;
	}

	pcdev->channels = 1;

	/* Make choises, based on platform preferences */
	if ((common_flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH) &&
	    (common_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)) {
		if (pcdev->platform_flags & PXA_CAMERA_HSP)
			common_flags &= ~V4L2_MBUS_HSYNC_ACTIVE_HIGH;
		else
			common_flags &= ~V4L2_MBUS_HSYNC_ACTIVE_LOW;
	}

	if ((common_flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH) &&
	    (common_flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)) {
		if (pcdev->platform_flags & PXA_CAMERA_VSP)
			common_flags &= ~V4L2_MBUS_VSYNC_ACTIVE_HIGH;
		else
			common_flags &= ~V4L2_MBUS_VSYNC_ACTIVE_LOW;
	}

	if ((common_flags & V4L2_MBUS_PCLK_SAMPLE_RISING) &&
	    (common_flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)) {
		if (pcdev->platform_flags & PXA_CAMERA_PCP)
			common_flags &= ~V4L2_MBUS_PCLK_SAMPLE_RISING;
		else
			common_flags &= ~V4L2_MBUS_PCLK_SAMPLE_FALLING;
	}

	cfg.flags = common_flags;
	ret = v4l2_subdev_call(sd, video, s_mbus_config, &cfg);
	if (ret < 0 && ret != -ENOIOCTLCMD) {
		dev_dbg(icd->parent, "camera s_mbus_config(0x%lx) returned %d\n",
			common_flags, ret);
		return ret;
	}

	cam->flags = common_flags;

	pxa_camera_setup_cicr(icd, common_flags, pixfmt);

	return 0;
}

static int pxa_camera_try_bus_param(struct soc_camera_device *icd,
				    unsigned char buswidth)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct pxa_camera_dev *pcdev = ici->priv;
	struct v4l2_mbus_config cfg = {.type = V4L2_MBUS_PARALLEL,};
	unsigned long bus_flags, common_flags;
	int ret = test_platform_param(pcdev, buswidth, &bus_flags);

	if (ret < 0)
		return ret;

	ret = v4l2_subdev_call(sd, video, g_mbus_config, &cfg);
	if (!ret) {
		common_flags = soc_mbus_config_compatible(&cfg,
							  bus_flags);
		if (!common_flags) {
			dev_warn(icd->parent,
				 "Flags incompatible: camera 0x%x, host 0x%lx\n",
				 cfg.flags, bus_flags);
			return -EINVAL;
		}
	} else if (ret == -ENOIOCTLCMD) {
		ret = 0;
	}

	return ret;
}

static const struct soc_mbus_pixelfmt pxa_camera_formats[] = {
	{
		.fourcc			= V4L2_PIX_FMT_YUV422P,
		.name			= "Planar YUV422 16 bit",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_LE,
		.layout			= SOC_MBUS_LAYOUT_PLANAR_2Y_U_V,
	},
};

/* This will be corrected as we get more formats */
static bool pxa_camera_packing_supported(const struct soc_mbus_pixelfmt *fmt)
{
	return	fmt->packing == SOC_MBUS_PACKING_NONE ||
		(fmt->bits_per_sample == 8 &&
		 fmt->packing == SOC_MBUS_PACKING_2X8_PADHI) ||
		(fmt->bits_per_sample > 8 &&
		 fmt->packing == SOC_MBUS_PACKING_EXTEND16);
}

static int pxa_camera_get_formats(struct soc_camera_device *icd, unsigned int idx,
				  struct soc_camera_format_xlate *xlate)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct device *dev = icd->parent;
	int formats = 0, ret;
	struct pxa_cam *cam;
	struct v4l2_subdev_mbus_code_enum code = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.index = idx,
	};
	const struct soc_mbus_pixelfmt *fmt;

	ret = v4l2_subdev_call(sd, pad, enum_mbus_code, NULL, &code);
	if (ret < 0)
		/* No more formats */
		return 0;

	fmt = soc_mbus_get_fmtdesc(code.code);
	if (!fmt) {
		dev_err(dev, "Invalid format code #%u: %d\n", idx, code.code);
		return 0;
	}

	/* This also checks support for the requested bits-per-sample */
	ret = pxa_camera_try_bus_param(icd, fmt->bits_per_sample);
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

	switch (code.code) {
	case MEDIA_BUS_FMT_UYVY8_2X8:
		formats++;
		if (xlate) {
			xlate->host_fmt	= &pxa_camera_formats[0];
			xlate->code	= code.code;
			xlate++;
			dev_dbg(dev, "Providing format %s using code %d\n",
				pxa_camera_formats[0].name, code.code);
		}
	case MEDIA_BUS_FMT_VYUY8_2X8:
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YVYU8_2X8:
	case MEDIA_BUS_FMT_RGB565_2X8_LE:
	case MEDIA_BUS_FMT_RGB555_2X8_PADHI_LE:
		if (xlate)
			dev_dbg(dev, "Providing format %s packed\n",
				fmt->name);
		break;
	default:
		if (!pxa_camera_packing_supported(fmt))
			return 0;
		if (xlate)
			dev_dbg(dev,
				"Providing format %s in pass-through mode\n",
				fmt->name);
	}

	/* Generic pass-through */
	formats++;
	if (xlate) {
		xlate->host_fmt	= fmt;
		xlate->code	= code.code;
		xlate++;
	}

	return formats;
}

static void pxa_camera_put_formats(struct soc_camera_device *icd)
{
	kfree(icd->host_priv);
	icd->host_priv = NULL;
}

static int pxa_camera_check_frame(u32 width, u32 height)
{
	/* limit to pxa hardware capabilities */
	return height < 32 || height > 2048 || width < 48 || width > 2048 ||
		(width & 0x01);
}

static int pxa_camera_set_crop(struct soc_camera_device *icd,
			       const struct v4l2_crop *a)
{
	const struct v4l2_rect *rect = &a->c;
	struct device *dev = icd->parent;
	struct soc_camera_host *ici = to_soc_camera_host(dev);
	struct pxa_camera_dev *pcdev = ici->priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct soc_camera_sense sense = {
		.master_clock = pcdev->mclk,
		.pixel_clock_max = pcdev->ciclk / 4,
	};
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	struct v4l2_mbus_framefmt *mf = &fmt.format;
	struct pxa_cam *cam = icd->host_priv;
	u32 fourcc = icd->current_fmt->host_fmt->fourcc;
	int ret;

	/* If PCLK is used to latch data from the sensor, check sense */
	if (pcdev->platform_flags & PXA_CAMERA_PCLK_EN)
		icd->sense = &sense;

	ret = v4l2_subdev_call(sd, video, s_crop, a);

	icd->sense = NULL;

	if (ret < 0) {
		dev_warn(dev, "Failed to crop to %ux%u@%u:%u\n",
			 rect->width, rect->height, rect->left, rect->top);
		return ret;
	}

	ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &fmt);
	if (ret < 0)
		return ret;

	if (pxa_camera_check_frame(mf->width, mf->height)) {
		/*
		 * Camera cropping produced a frame beyond our capabilities.
		 * FIXME: just extract a subframe, that we can process.
		 */
		v4l_bound_align_image(&mf->width, 48, 2048, 1,
			&mf->height, 32, 2048, 0,
			fourcc == V4L2_PIX_FMT_YUV422P ? 4 : 0);
		ret = v4l2_subdev_call(sd, pad, set_fmt, NULL, &fmt);
		if (ret < 0)
			return ret;

		if (pxa_camera_check_frame(mf->width, mf->height)) {
			dev_warn(icd->parent,
				 "Inconsistent state. Use S_FMT to repair\n");
			return -EINVAL;
		}
	}

	if (sense.flags & SOCAM_SENSE_PCLK_CHANGED) {
		if (sense.pixel_clock > sense.pixel_clock_max) {
			dev_err(dev,
				"pixel clock %lu set by the camera too high!",
				sense.pixel_clock);
			return -EIO;
		}
		recalculate_fifo_timeout(pcdev, sense.pixel_clock);
	}

	icd->user_width		= mf->width;
	icd->user_height	= mf->height;

	pxa_camera_setup_cicr(icd, cam->flags, fourcc);

	return ret;
}

static int pxa_camera_set_fmt(struct soc_camera_device *icd,
			      struct v4l2_format *f)
{
	struct device *dev = icd->parent;
	struct soc_camera_host *ici = to_soc_camera_host(dev);
	struct pxa_camera_dev *pcdev = ici->priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_camera_format_xlate *xlate = NULL;
	struct soc_camera_sense sense = {
		.master_clock = pcdev->mclk,
		.pixel_clock_max = pcdev->ciclk / 4,
	};
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	struct v4l2_mbus_framefmt *mf = &format.format;
	int ret;

	xlate = soc_camera_xlate_by_fourcc(icd, pix->pixelformat);
	if (!xlate) {
		dev_warn(dev, "Format %x not found\n", pix->pixelformat);
		return -EINVAL;
	}

	/* If PCLK is used to latch data from the sensor, check sense */
	if (pcdev->platform_flags & PXA_CAMERA_PCLK_EN)
		/* The caller holds a mutex. */
		icd->sense = &sense;

	mf->width	= pix->width;
	mf->height	= pix->height;
	mf->field	= pix->field;
	mf->colorspace	= pix->colorspace;
	mf->code	= xlate->code;

	ret = v4l2_subdev_call(sd, pad, set_fmt, NULL, &format);

	if (mf->code != xlate->code)
		return -EINVAL;

	icd->sense = NULL;

	if (ret < 0) {
		dev_warn(dev, "Failed to configure for format %x\n",
			 pix->pixelformat);
	} else if (pxa_camera_check_frame(mf->width, mf->height)) {
		dev_warn(dev,
			 "Camera driver produced an unsupported frame %dx%d\n",
			 mf->width, mf->height);
		ret = -EINVAL;
	} else if (sense.flags & SOCAM_SENSE_PCLK_CHANGED) {
		if (sense.pixel_clock > sense.pixel_clock_max) {
			dev_err(dev,
				"pixel clock %lu set by the camera too high!",
				sense.pixel_clock);
			return -EIO;
		}
		recalculate_fifo_timeout(pcdev, sense.pixel_clock);
	}

	if (ret < 0)
		return ret;

	pix->width		= mf->width;
	pix->height		= mf->height;
	pix->field		= mf->field;
	pix->colorspace		= mf->colorspace;
	icd->current_fmt	= xlate;

	return ret;
}

static int pxa_camera_try_fmt(struct soc_camera_device *icd,
			      struct v4l2_format *f)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_subdev_pad_config pad_cfg;
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
	};
	struct v4l2_mbus_framefmt *mf = &format.format;
	__u32 pixfmt = pix->pixelformat;
	int ret;

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (!xlate) {
		dev_warn(icd->parent, "Format %x not found\n", pixfmt);
		return -EINVAL;
	}

	/*
	 * Limit to pxa hardware capabilities.  YUV422P planar format requires
	 * images size to be a multiple of 16 bytes.  If not, zeros will be
	 * inserted between Y and U planes, and U and V planes, which violates
	 * the YUV422P standard.
	 */
	v4l_bound_align_image(&pix->width, 48, 2048, 1,
			      &pix->height, 32, 2048, 0,
			      pixfmt == V4L2_PIX_FMT_YUV422P ? 4 : 0);

	/* limit to sensor capabilities */
	mf->width	= pix->width;
	mf->height	= pix->height;
	/* Only progressive video supported so far */
	mf->field	= V4L2_FIELD_NONE;
	mf->colorspace	= pix->colorspace;
	mf->code	= xlate->code;

	ret = v4l2_subdev_call(sd, pad, set_fmt, &pad_cfg, &format);
	if (ret < 0)
		return ret;

	pix->width	= mf->width;
	pix->height	= mf->height;
	pix->colorspace	= mf->colorspace;

	switch (mf->field) {
	case V4L2_FIELD_ANY:
	case V4L2_FIELD_NONE:
		pix->field	= V4L2_FIELD_NONE;
		break;
	default:
		/* TODO: support interlaced at least in pass-through mode */
		dev_err(icd->parent, "Field type %d unsupported.\n",
			mf->field);
		return -EINVAL;
	}

	return ret;
}

static int pxa_camera_reqbufs(struct soc_camera_device *icd,
			      struct v4l2_requestbuffers *p)
{
	int i;

	/*
	 * This is for locking debugging only. I removed spinlocks and now I
	 * check whether .prepare is ever called on a linked buffer, or whether
	 * a dma IRQ can occur for an in-work or unlinked buffer. Until now
	 * it hadn't triggered
	 */
	for (i = 0; i < p->count; i++) {
		struct pxa_buffer *buf = container_of(icd->vb_vidq.bufs[i],
						      struct pxa_buffer, vb);
		buf->inwork = 0;
		INIT_LIST_HEAD(&buf->vb.queue);
	}

	return 0;
}

static unsigned int pxa_camera_poll(struct file *file, poll_table *pt)
{
	struct soc_camera_device *icd = file->private_data;
	struct pxa_buffer *buf;

	buf = list_entry(icd->vb_vidq.stream.next, struct pxa_buffer,
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
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int pxa_camera_suspend(struct device *dev)
{
	struct soc_camera_host *ici = to_soc_camera_host(dev);
	struct pxa_camera_dev *pcdev = ici->priv;
	int i = 0, ret = 0;

	pcdev->save_cicr[i++] = __raw_readl(pcdev->base + CICR0);
	pcdev->save_cicr[i++] = __raw_readl(pcdev->base + CICR1);
	pcdev->save_cicr[i++] = __raw_readl(pcdev->base + CICR2);
	pcdev->save_cicr[i++] = __raw_readl(pcdev->base + CICR3);
	pcdev->save_cicr[i++] = __raw_readl(pcdev->base + CICR4);

	if (pcdev->soc_host.icd) {
		struct v4l2_subdev *sd = soc_camera_to_subdev(pcdev->soc_host.icd);
		ret = v4l2_subdev_call(sd, core, s_power, 0);
		if (ret == -ENOIOCTLCMD)
			ret = 0;
	}

	return ret;
}

static int pxa_camera_resume(struct device *dev)
{
	struct soc_camera_host *ici = to_soc_camera_host(dev);
	struct pxa_camera_dev *pcdev = ici->priv;
	int i = 0, ret = 0;

	__raw_writel(pcdev->save_cicr[i++] & ~CICR0_ENB, pcdev->base + CICR0);
	__raw_writel(pcdev->save_cicr[i++], pcdev->base + CICR1);
	__raw_writel(pcdev->save_cicr[i++], pcdev->base + CICR2);
	__raw_writel(pcdev->save_cicr[i++], pcdev->base + CICR3);
	__raw_writel(pcdev->save_cicr[i++], pcdev->base + CICR4);

	if (pcdev->soc_host.icd) {
		struct v4l2_subdev *sd = soc_camera_to_subdev(pcdev->soc_host.icd);
		ret = v4l2_subdev_call(sd, core, s_power, 1);
		if (ret == -ENOIOCTLCMD)
			ret = 0;
	}

	/* Restart frame capture if active buffer exists */
	if (!ret && pcdev->active)
		pxa_camera_start_capture(pcdev);

	return ret;
}

static struct soc_camera_host_ops pxa_soc_camera_host_ops = {
	.owner		= THIS_MODULE,
	.add		= pxa_camera_add_device,
	.remove		= pxa_camera_remove_device,
	.clock_start	= pxa_camera_clock_start,
	.clock_stop	= pxa_camera_clock_stop,
	.set_crop	= pxa_camera_set_crop,
	.get_formats	= pxa_camera_get_formats,
	.put_formats	= pxa_camera_put_formats,
	.set_fmt	= pxa_camera_set_fmt,
	.try_fmt	= pxa_camera_try_fmt,
	.init_videobuf	= pxa_camera_init_videobuf,
	.reqbufs	= pxa_camera_reqbufs,
	.poll		= pxa_camera_poll,
	.querycap	= pxa_camera_querycap,
	.set_bus_param	= pxa_camera_set_bus_param,
};

static int pxa_camera_pdata_from_dt(struct device *dev,
				    struct pxa_camera_dev *pcdev)
{
	u32 mclk_rate;
	struct device_node *np = dev->of_node;
	struct v4l2_of_endpoint ep;
	int err = of_property_read_u32(np, "clock-frequency",
				       &mclk_rate);
	if (!err) {
		pcdev->platform_flags |= PXA_CAMERA_MCLK_EN;
		pcdev->mclk = mclk_rate;
	}

	np = of_graph_get_next_endpoint(np, NULL);
	if (!np) {
		dev_err(dev, "could not find endpoint\n");
		return -EINVAL;
	}

	err = v4l2_of_parse_endpoint(np, &ep);
	if (err) {
		dev_err(dev, "could not parse endpoint\n");
		goto out;
	}

	switch (ep.bus.parallel.bus_width) {
	case 4:
		pcdev->platform_flags |= PXA_CAMERA_DATAWIDTH_4;
		break;
	case 5:
		pcdev->platform_flags |= PXA_CAMERA_DATAWIDTH_5;
		break;
	case 8:
		pcdev->platform_flags |= PXA_CAMERA_DATAWIDTH_8;
		break;
	case 9:
		pcdev->platform_flags |= PXA_CAMERA_DATAWIDTH_9;
		break;
	case 10:
		pcdev->platform_flags |= PXA_CAMERA_DATAWIDTH_10;
		break;
	default:
		break;
	}

	if (ep.bus.parallel.flags & V4L2_MBUS_MASTER)
		pcdev->platform_flags |= PXA_CAMERA_MASTER;
	if (ep.bus.parallel.flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH)
		pcdev->platform_flags |= PXA_CAMERA_HSP;
	if (ep.bus.parallel.flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH)
		pcdev->platform_flags |= PXA_CAMERA_VSP;
	if (ep.bus.parallel.flags & V4L2_MBUS_PCLK_SAMPLE_RISING)
		pcdev->platform_flags |= PXA_CAMERA_PCLK_EN | PXA_CAMERA_PCP;
	if (ep.bus.parallel.flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)
		pcdev->platform_flags |= PXA_CAMERA_PCLK_EN;

out:
	of_node_put(np);

	return err;
}

static int pxa_camera_probe(struct platform_device *pdev)
{
	struct pxa_camera_dev *pcdev;
	struct resource *res;
	void __iomem *base;
	struct dma_slave_config config = {
		.src_addr_width = 0,
		.src_maxburst = 8,
		.direction = DMA_DEV_TO_MEM,
	};
	dma_cap_mask_t mask;
	struct pxad_param params;
	int irq;
	int err = 0, i;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!res || irq < 0)
		return -ENODEV;

	pcdev = devm_kzalloc(&pdev->dev, sizeof(*pcdev), GFP_KERNEL);
	if (!pcdev) {
		dev_err(&pdev->dev, "Could not allocate pcdev\n");
		return -ENOMEM;
	}

	pcdev->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pcdev->clk))
		return PTR_ERR(pcdev->clk);

	pcdev->res = res;

	pcdev->pdata = pdev->dev.platform_data;
	if (&pdev->dev.of_node && !pcdev->pdata) {
		err = pxa_camera_pdata_from_dt(&pdev->dev, pcdev);
	} else {
		pcdev->platform_flags = pcdev->pdata->flags;
		pcdev->mclk = pcdev->pdata->mclk_10khz * 10000;
	}
	if (err < 0)
		return err;

	if (!(pcdev->platform_flags & (PXA_CAMERA_DATAWIDTH_8 |
			PXA_CAMERA_DATAWIDTH_9 | PXA_CAMERA_DATAWIDTH_10))) {
		/*
		 * Platform hasn't set available data widths. This is bad.
		 * Warn and use a default.
		 */
		dev_warn(&pdev->dev, "WARNING! Platform hasn't set available "
			 "data widths, using default 10 bit\n");
		pcdev->platform_flags |= PXA_CAMERA_DATAWIDTH_10;
	}
	if (pcdev->platform_flags & PXA_CAMERA_DATAWIDTH_8)
		pcdev->width_flags = 1 << 7;
	if (pcdev->platform_flags & PXA_CAMERA_DATAWIDTH_9)
		pcdev->width_flags |= 1 << 8;
	if (pcdev->platform_flags & PXA_CAMERA_DATAWIDTH_10)
		pcdev->width_flags |= 1 << 9;
	if (!pcdev->mclk) {
		dev_warn(&pdev->dev,
			 "mclk == 0! Please, fix your platform data. "
			 "Using default 20MHz\n");
		pcdev->mclk = 20000000;
	}

	pcdev->mclk_divisor = mclk_get_divisor(pdev, pcdev);

	INIT_LIST_HEAD(&pcdev->capture);
	spin_lock_init(&pcdev->lock);

	/*
	 * Request the regions.
	 */
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	pcdev->irq = irq;
	pcdev->base = base;

	/* request dma */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_cap_set(DMA_PRIVATE, mask);

	params.prio = 0;
	params.drcmr = 68;
	pcdev->dma_chans[0] =
		dma_request_slave_channel_compat(mask, pxad_filter_fn,
						 &params, &pdev->dev, "CI_Y");
	if (!pcdev->dma_chans[0]) {
		dev_err(&pdev->dev, "Can't request DMA for Y\n");
		return -ENODEV;
	}

	params.drcmr = 69;
	pcdev->dma_chans[1] =
		dma_request_slave_channel_compat(mask, pxad_filter_fn,
						 &params, &pdev->dev, "CI_U");
	if (!pcdev->dma_chans[1]) {
		dev_err(&pdev->dev, "Can't request DMA for Y\n");
		goto exit_free_dma_y;
	}

	params.drcmr = 70;
	pcdev->dma_chans[2] =
		dma_request_slave_channel_compat(mask, pxad_filter_fn,
						 &params, &pdev->dev, "CI_V");
	if (!pcdev->dma_chans[2]) {
		dev_err(&pdev->dev, "Can't request DMA for V\n");
		goto exit_free_dma_u;
	}

	for (i = 0; i < 3; i++) {
		config.src_addr = pcdev->res->start + CIBR0 + i * 8;
		err = dmaengine_slave_config(pcdev->dma_chans[i], &config);
		if (err < 0) {
			dev_err(&pdev->dev, "dma slave config failed: %d\n",
				err);
			goto exit_free_dma;
		}
	}

	/* request irq */
	err = devm_request_irq(&pdev->dev, pcdev->irq, pxa_camera_irq, 0,
			       PXA_CAM_DRV_NAME, pcdev);
	if (err) {
		dev_err(&pdev->dev, "Camera interrupt register failed\n");
		goto exit_free_dma;
	}

	pcdev->soc_host.drv_name	= PXA_CAM_DRV_NAME;
	pcdev->soc_host.ops		= &pxa_soc_camera_host_ops;
	pcdev->soc_host.priv		= pcdev;
	pcdev->soc_host.v4l2_dev.dev	= &pdev->dev;
	pcdev->soc_host.nr		= pdev->id;
	tasklet_init(&pcdev->task_eof, pxa_camera_eof, (unsigned long)pcdev);

	err = soc_camera_host_register(&pcdev->soc_host);
	if (err)
		goto exit_free_dma;

	return 0;

exit_free_dma:
	dma_release_channel(pcdev->dma_chans[2]);
exit_free_dma_u:
	dma_release_channel(pcdev->dma_chans[1]);
exit_free_dma_y:
	dma_release_channel(pcdev->dma_chans[0]);
	return err;
}

static int pxa_camera_remove(struct platform_device *pdev)
{
	struct soc_camera_host *soc_host = to_soc_camera_host(&pdev->dev);
	struct pxa_camera_dev *pcdev = container_of(soc_host,
					struct pxa_camera_dev, soc_host);

	dma_release_channel(pcdev->dma_chans[0]);
	dma_release_channel(pcdev->dma_chans[1]);
	dma_release_channel(pcdev->dma_chans[2]);

	soc_camera_host_unregister(soc_host);

	dev_info(&pdev->dev, "PXA Camera driver unloaded\n");

	return 0;
}

static const struct dev_pm_ops pxa_camera_pm = {
	.suspend	= pxa_camera_suspend,
	.resume		= pxa_camera_resume,
};

static const struct of_device_id pxa_camera_of_match[] = {
	{ .compatible = "marvell,pxa270-qci", },
	{},
};
MODULE_DEVICE_TABLE(of, pxa_camera_of_match);

static struct platform_driver pxa_camera_driver = {
	.driver		= {
		.name	= PXA_CAM_DRV_NAME,
		.pm	= &pxa_camera_pm,
		.of_match_table = of_match_ptr(pxa_camera_of_match),
	},
	.probe		= pxa_camera_probe,
	.remove		= pxa_camera_remove,
};

module_platform_driver(pxa_camera_driver);

MODULE_DESCRIPTION("PXA27x SoC Camera Host driver");
MODULE_AUTHOR("Guennadi Liakhovetski <kernel@pengutronix.de>");
MODULE_LICENSE("GPL");
MODULE_VERSION(PXA_CAM_VERSION);
MODULE_ALIAS("platform:" PXA_CAM_DRV_NAME);
