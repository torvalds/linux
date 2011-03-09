/*
 * V4L2 Driver for i.MX27/i.MX25 camera host
 *
 * Copyright (C) 2008, Sascha Hauer, Pengutronix
 * Copyright (C) 2010, Baruch Siach, Orex Computed Radiography
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
#include <linux/slab.h>
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
#include <media/videobuf-core.h>
#include <media/videobuf-dma-contig.h>
#include <media/soc_camera.h>
#include <media/soc_mediabus.h>

#include <linux/videodev2.h>

#include <mach/mx2_cam.h>
#ifdef CONFIG_MACH_MX27
#include <mach/dma-mx1-mx2.h>
#endif
#include <mach/hardware.h>

#include <asm/dma.h>

#define MX2_CAM_DRV_NAME "mx2-camera"
#define MX2_CAM_VERSION_CODE KERNEL_VERSION(0, 0, 5)
#define MX2_CAM_DRIVER_DESCRIPTION "i.MX2x_Camera"

/* reset values */
#define CSICR1_RESET_VAL	0x40000800
#define CSICR2_RESET_VAL	0x0
#define CSICR3_RESET_VAL	0x0

/* csi control reg 1 */
#define CSICR1_SWAP16_EN	(1 << 31)
#define CSICR1_EXT_VSYNC	(1 << 30)
#define CSICR1_EOF_INTEN	(1 << 29)
#define CSICR1_PRP_IF_EN	(1 << 28)
#define CSICR1_CCIR_MODE	(1 << 27)
#define CSICR1_COF_INTEN	(1 << 26)
#define CSICR1_SF_OR_INTEN	(1 << 25)
#define CSICR1_RF_OR_INTEN	(1 << 24)
#define CSICR1_STATFF_LEVEL	(3 << 22)
#define CSICR1_STATFF_INTEN	(1 << 21)
#define CSICR1_RXFF_LEVEL(l)	(((l) & 3) << 19)	/* MX27 */
#define CSICR1_FB2_DMA_INTEN	(1 << 20)		/* MX25 */
#define CSICR1_FB1_DMA_INTEN	(1 << 19)		/* MX25 */
#define CSICR1_RXFF_INTEN	(1 << 18)
#define CSICR1_SOF_POL		(1 << 17)
#define CSICR1_SOF_INTEN	(1 << 16)
#define CSICR1_MCLKDIV(d)	(((d) & 0xF) << 12)
#define CSICR1_HSYNC_POL	(1 << 11)
#define CSICR1_CCIR_EN		(1 << 10)
#define CSICR1_MCLKEN		(1 << 9)
#define CSICR1_FCC		(1 << 8)
#define CSICR1_PACK_DIR		(1 << 7)
#define CSICR1_CLR_STATFIFO	(1 << 6)
#define CSICR1_CLR_RXFIFO	(1 << 5)
#define CSICR1_GCLK_MODE	(1 << 4)
#define CSICR1_INV_DATA		(1 << 3)
#define CSICR1_INV_PCLK		(1 << 2)
#define CSICR1_REDGE		(1 << 1)

#define SHIFT_STATFF_LEVEL	22
#define SHIFT_RXFF_LEVEL	19
#define SHIFT_MCLKDIV		12

/* control reg 3 */
#define CSICR3_FRMCNT		(0xFFFF << 16)
#define CSICR3_FRMCNT_RST	(1 << 15)
#define CSICR3_DMA_REFLASH_RFF	(1 << 14)
#define CSICR3_DMA_REFLASH_SFF	(1 << 13)
#define CSICR3_DMA_REQ_EN_RFF	(1 << 12)
#define CSICR3_DMA_REQ_EN_SFF	(1 << 11)
#define CSICR3_RXFF_LEVEL(l)	(((l) & 7) << 4)	/* MX25 */
#define CSICR3_CSI_SUP		(1 << 3)
#define CSICR3_ZERO_PACK_EN	(1 << 2)
#define CSICR3_ECC_INT_EN	(1 << 1)
#define CSICR3_ECC_AUTO_EN	(1 << 0)

#define SHIFT_FRMCNT		16

/* csi status reg */
#define CSISR_SFF_OR_INT	(1 << 25)
#define CSISR_RFF_OR_INT	(1 << 24)
#define CSISR_STATFF_INT	(1 << 21)
#define CSISR_DMA_TSF_FB2_INT	(1 << 20)	/* MX25 */
#define CSISR_DMA_TSF_FB1_INT	(1 << 19)	/* MX25 */
#define CSISR_RXFF_INT		(1 << 18)
#define CSISR_EOF_INT		(1 << 17)
#define CSISR_SOF_INT		(1 << 16)
#define CSISR_F2_INT		(1 << 15)
#define CSISR_F1_INT		(1 << 14)
#define CSISR_COF_INT		(1 << 13)
#define CSISR_ECC_INT		(1 << 1)
#define CSISR_DRDY		(1 << 0)

#define CSICR1			0x00
#define CSICR2			0x04
#define CSISR			(cpu_is_mx27() ? 0x08 : 0x18)
#define CSISTATFIFO		0x0c
#define CSIRFIFO		0x10
#define CSIRXCNT		0x14
#define CSICR3			(cpu_is_mx27() ? 0x1C : 0x08)
#define CSIDMASA_STATFIFO	0x20
#define CSIDMATA_STATFIFO	0x24
#define CSIDMASA_FB1		0x28
#define CSIDMASA_FB2		0x2c
#define CSIFBUF_PARA		0x30
#define CSIIMAG_PARA		0x34

/* EMMA PrP */
#define PRP_CNTL			0x00
#define PRP_INTR_CNTL			0x04
#define PRP_INTRSTATUS			0x08
#define PRP_SOURCE_Y_PTR		0x0c
#define PRP_SOURCE_CB_PTR		0x10
#define PRP_SOURCE_CR_PTR		0x14
#define PRP_DEST_RGB1_PTR		0x18
#define PRP_DEST_RGB2_PTR		0x1c
#define PRP_DEST_Y_PTR			0x20
#define PRP_DEST_CB_PTR			0x24
#define PRP_DEST_CR_PTR			0x28
#define PRP_SRC_FRAME_SIZE		0x2c
#define PRP_DEST_CH1_LINE_STRIDE	0x30
#define PRP_SRC_PIXEL_FORMAT_CNTL	0x34
#define PRP_CH1_PIXEL_FORMAT_CNTL	0x38
#define PRP_CH1_OUT_IMAGE_SIZE		0x3c
#define PRP_CH2_OUT_IMAGE_SIZE		0x40
#define PRP_SRC_LINE_STRIDE		0x44
#define PRP_CSC_COEF_012		0x48
#define PRP_CSC_COEF_345		0x4c
#define PRP_CSC_COEF_678		0x50
#define PRP_CH1_RZ_HORI_COEF1		0x54
#define PRP_CH1_RZ_HORI_COEF2		0x58
#define PRP_CH1_RZ_HORI_VALID		0x5c
#define PRP_CH1_RZ_VERT_COEF1		0x60
#define PRP_CH1_RZ_VERT_COEF2		0x64
#define PRP_CH1_RZ_VERT_VALID		0x68
#define PRP_CH2_RZ_HORI_COEF1		0x6c
#define PRP_CH2_RZ_HORI_COEF2		0x70
#define PRP_CH2_RZ_HORI_VALID		0x74
#define PRP_CH2_RZ_VERT_COEF1		0x78
#define PRP_CH2_RZ_VERT_COEF2		0x7c
#define PRP_CH2_RZ_VERT_VALID		0x80

#define PRP_CNTL_CH1EN		(1 << 0)
#define PRP_CNTL_CH2EN		(1 << 1)
#define PRP_CNTL_CSIEN		(1 << 2)
#define PRP_CNTL_DATA_IN_YUV420	(0 << 3)
#define PRP_CNTL_DATA_IN_YUV422	(1 << 3)
#define PRP_CNTL_DATA_IN_RGB16	(2 << 3)
#define PRP_CNTL_DATA_IN_RGB32	(3 << 3)
#define PRP_CNTL_CH1_OUT_RGB8	(0 << 5)
#define PRP_CNTL_CH1_OUT_RGB16	(1 << 5)
#define PRP_CNTL_CH1_OUT_RGB32	(2 << 5)
#define PRP_CNTL_CH1_OUT_YUV422	(3 << 5)
#define PRP_CNTL_CH2_OUT_YUV420	(0 << 7)
#define PRP_CNTL_CH2_OUT_YUV422 (1 << 7)
#define PRP_CNTL_CH2_OUT_YUV444	(2 << 7)
#define PRP_CNTL_CH1_LEN	(1 << 9)
#define PRP_CNTL_CH2_LEN	(1 << 10)
#define PRP_CNTL_SKIP_FRAME	(1 << 11)
#define PRP_CNTL_SWRST		(1 << 12)
#define PRP_CNTL_CLKEN		(1 << 13)
#define PRP_CNTL_WEN		(1 << 14)
#define PRP_CNTL_CH1BYP		(1 << 15)
#define PRP_CNTL_IN_TSKIP(x)	((x) << 16)
#define PRP_CNTL_CH1_TSKIP(x)	((x) << 19)
#define PRP_CNTL_CH2_TSKIP(x)	((x) << 22)
#define PRP_CNTL_INPUT_FIFO_LEVEL(x)	((x) << 25)
#define PRP_CNTL_RZ_FIFO_LEVEL(x)	((x) << 27)
#define PRP_CNTL_CH2B1EN	(1 << 29)
#define PRP_CNTL_CH2B2EN	(1 << 30)
#define PRP_CNTL_CH2FEN		(1 << 31)

/* IRQ Enable and status register */
#define PRP_INTR_RDERR		(1 << 0)
#define PRP_INTR_CH1WERR	(1 << 1)
#define PRP_INTR_CH2WERR	(1 << 2)
#define PRP_INTR_CH1FC		(1 << 3)
#define PRP_INTR_CH2FC		(1 << 5)
#define PRP_INTR_LBOVF		(1 << 7)
#define PRP_INTR_CH2OVF		(1 << 8)

#define mx27_camera_emma(pcdev)	(cpu_is_mx27() && pcdev->use_emma)

#define MAX_VIDEO_MEM	16

struct mx2_camera_dev {
	struct device		*dev;
	struct soc_camera_host	soc_host;
	struct soc_camera_device *icd;
	struct clk		*clk_csi, *clk_emma;

	unsigned int		irq_csi, irq_emma;
	void __iomem		*base_csi, *base_emma;
	unsigned long		base_dma;

	struct mx2_camera_platform_data *pdata;
	struct resource		*res_csi, *res_emma;
	unsigned long		platform_flags;

	struct list_head	capture;
	struct list_head	active_bufs;

	spinlock_t		lock;

	int			dma;
	struct mx2_buffer	*active;
	struct mx2_buffer	*fb1_active;
	struct mx2_buffer	*fb2_active;

	int			use_emma;

	u32			csicr1;

	void			*discard_buffer;
	dma_addr_t		discard_buffer_dma;
	size_t			discard_size;
};

/* buffer for one video frame */
struct mx2_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer		vb;

	enum v4l2_mbus_pixelcode	code;

	int bufnum;
};

static void mx2_camera_deactivate(struct mx2_camera_dev *pcdev)
{
	unsigned long flags;

	clk_disable(pcdev->clk_csi);
	writel(0, pcdev->base_csi + CSICR1);
	if (mx27_camera_emma(pcdev)) {
		writel(0, pcdev->base_emma + PRP_CNTL);
	} else if (cpu_is_mx25()) {
		spin_lock_irqsave(&pcdev->lock, flags);
		pcdev->fb1_active = NULL;
		pcdev->fb2_active = NULL;
		writel(0, pcdev->base_csi + CSIDMASA_FB1);
		writel(0, pcdev->base_csi + CSIDMASA_FB2);
		spin_unlock_irqrestore(&pcdev->lock, flags);
	}
}

/*
 * The following two functions absolutely depend on the fact, that
 * there can be only one camera on mx2 camera sensor interface
 */
static int mx2_camera_add_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct mx2_camera_dev *pcdev = ici->priv;
	int ret;
	u32 csicr1;

	if (pcdev->icd)
		return -EBUSY;

	ret = clk_enable(pcdev->clk_csi);
	if (ret < 0)
		return ret;

	csicr1 = CSICR1_MCLKEN;

	if (mx27_camera_emma(pcdev)) {
		csicr1 |= CSICR1_PRP_IF_EN | CSICR1_FCC |
			CSICR1_RXFF_LEVEL(0);
	} else if (cpu_is_mx27())
		csicr1 |= CSICR1_SOF_INTEN | CSICR1_RXFF_LEVEL(2);

	pcdev->csicr1 = csicr1;
	writel(pcdev->csicr1, pcdev->base_csi + CSICR1);

	pcdev->icd = icd;

	dev_info(icd->dev.parent, "Camera driver attached to camera %d\n",
		 icd->devnum);

	return 0;
}

static void mx2_camera_remove_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct mx2_camera_dev *pcdev = ici->priv;

	BUG_ON(icd != pcdev->icd);

	dev_info(icd->dev.parent, "Camera driver detached from camera %d\n",
		 icd->devnum);

	mx2_camera_deactivate(pcdev);

	if (pcdev->discard_buffer) {
		dma_free_coherent(ici->v4l2_dev.dev, pcdev->discard_size,
				pcdev->discard_buffer,
				pcdev->discard_buffer_dma);
		pcdev->discard_buffer = NULL;
	}

	pcdev->icd = NULL;
}

#ifdef CONFIG_MACH_MX27
static void mx27_camera_dma_enable(struct mx2_camera_dev *pcdev)
{
	u32 tmp;

	imx_dma_enable(pcdev->dma);

	tmp = readl(pcdev->base_csi + CSICR1);
	tmp |= CSICR1_RF_OR_INTEN;
	writel(tmp, pcdev->base_csi + CSICR1);
}

static irqreturn_t mx27_camera_irq(int irq_csi, void *data)
{
	struct mx2_camera_dev *pcdev = data;
	u32 status = readl(pcdev->base_csi + CSISR);

	if (status & CSISR_SOF_INT && pcdev->active) {
		u32 tmp;

		tmp = readl(pcdev->base_csi + CSICR1);
		writel(tmp | CSICR1_CLR_RXFIFO, pcdev->base_csi + CSICR1);
		mx27_camera_dma_enable(pcdev);
	}

	writel(CSISR_SOF_INT | CSISR_RFF_OR_INT, pcdev->base_csi + CSISR);

	return IRQ_HANDLED;
}
#else
static irqreturn_t mx27_camera_irq(int irq_csi, void *data)
{
	return IRQ_NONE;
}
#endif /* CONFIG_MACH_MX27 */

static void mx25_camera_frame_done(struct mx2_camera_dev *pcdev, int fb,
		int state)
{
	struct videobuf_buffer *vb;
	struct mx2_buffer *buf;
	struct mx2_buffer **fb_active = fb == 1 ? &pcdev->fb1_active :
		&pcdev->fb2_active;
	u32 fb_reg = fb == 1 ? CSIDMASA_FB1 : CSIDMASA_FB2;
	unsigned long flags;

	spin_lock_irqsave(&pcdev->lock, flags);

	if (*fb_active == NULL)
		goto out;

	vb = &(*fb_active)->vb;
	dev_dbg(pcdev->dev, "%s (vb=0x%p) 0x%08lx %d\n", __func__,
		vb, vb->baddr, vb->bsize);

	vb->state = state;
	do_gettimeofday(&vb->ts);
	vb->field_count++;

	wake_up(&vb->done);

	if (list_empty(&pcdev->capture)) {
		buf = NULL;
		writel(0, pcdev->base_csi + fb_reg);
	} else {
		buf = list_entry(pcdev->capture.next, struct mx2_buffer,
				vb.queue);
		vb = &buf->vb;
		list_del(&vb->queue);
		vb->state = VIDEOBUF_ACTIVE;
		writel(videobuf_to_dma_contig(vb), pcdev->base_csi + fb_reg);
	}

	*fb_active = buf;

out:
	spin_unlock_irqrestore(&pcdev->lock, flags);
}

static irqreturn_t mx25_camera_irq(int irq_csi, void *data)
{
	struct mx2_camera_dev *pcdev = data;
	u32 status = readl(pcdev->base_csi + CSISR);

	if (status & CSISR_DMA_TSF_FB1_INT)
		mx25_camera_frame_done(pcdev, 1, VIDEOBUF_DONE);
	else if (status & CSISR_DMA_TSF_FB2_INT)
		mx25_camera_frame_done(pcdev, 2, VIDEOBUF_DONE);

	/* FIXME: handle CSISR_RFF_OR_INT */

	writel(status, pcdev->base_csi + CSISR);

	return IRQ_HANDLED;
}

/*
 *  Videobuf operations
 */
static int mx2_videobuf_setup(struct videobuf_queue *vq, unsigned int *count,
			      unsigned int *size)
{
	struct soc_camera_device *icd = vq->priv_data;
	int bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
			icd->current_fmt->host_fmt);

	dev_dbg(&icd->dev, "count=%d, size=%d\n", *count, *size);

	if (bytes_per_line < 0)
		return bytes_per_line;

	*size = bytes_per_line * icd->user_height;

	if (0 == *count)
		*count = 32;
	if (*size * *count > MAX_VIDEO_MEM * 1024 * 1024)
		*count = (MAX_VIDEO_MEM * 1024 * 1024) / *size;

	return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct mx2_buffer *buf)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct videobuf_buffer *vb = &buf->vb;

	dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %d\n", __func__,
		vb, vb->baddr, vb->bsize);

	/*
	 * This waits until this buffer is out of danger, i.e., until it is no
	 * longer in state VIDEOBUF_QUEUED or VIDEOBUF_ACTIVE
	 */
	videobuf_waiton(vq, vb, 0, 0);

	videobuf_dma_contig_free(vq, vb);
	dev_dbg(&icd->dev, "%s freed\n", __func__);

	vb->state = VIDEOBUF_NEEDS_INIT;
}

static int mx2_videobuf_prepare(struct videobuf_queue *vq,
		struct videobuf_buffer *vb, enum v4l2_field field)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct mx2_buffer *buf = container_of(vb, struct mx2_buffer, vb);
	int bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
			icd->current_fmt->host_fmt);
	int ret = 0;

	dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %d\n", __func__,
		vb, vb->baddr, vb->bsize);

	if (bytes_per_line < 0)
		return bytes_per_line;

#ifdef DEBUG
	/*
	 * This can be useful if you want to see if we actually fill
	 * the buffer with something
	 */
	memset((void *)vb->baddr, 0xaa, vb->bsize);
#endif

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

	vb->size = bytes_per_line * vb->height;
	if (vb->baddr && vb->bsize < vb->size) {
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

static void mx2_videobuf_queue(struct videobuf_queue *vq,
			       struct videobuf_buffer *vb)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici =
		to_soc_camera_host(icd->dev.parent);
	struct mx2_camera_dev *pcdev = ici->priv;
	struct mx2_buffer *buf = container_of(vb, struct mx2_buffer, vb);
	unsigned long flags;

	dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %d\n", __func__,
		vb, vb->baddr, vb->bsize);

	spin_lock_irqsave(&pcdev->lock, flags);

	vb->state = VIDEOBUF_QUEUED;
	list_add_tail(&vb->queue, &pcdev->capture);

	if (mx27_camera_emma(pcdev)) {
		goto out;
#ifdef CONFIG_MACH_MX27
	} else if (cpu_is_mx27()) {
		int ret;

		if (pcdev->active == NULL) {
			ret = imx_dma_setup_single(pcdev->dma,
					videobuf_to_dma_contig(vb), vb->size,
					(u32)pcdev->base_dma + 0x10,
					DMA_MODE_READ);
			if (ret) {
				vb->state = VIDEOBUF_ERROR;
				wake_up(&vb->done);
				goto out;
			}

			vb->state = VIDEOBUF_ACTIVE;
			pcdev->active = buf;
		}
#endif
	} else { /* cpu_is_mx25() */
		u32 csicr3, dma_inten = 0;

		if (pcdev->fb1_active == NULL) {
			writel(videobuf_to_dma_contig(vb),
					pcdev->base_csi + CSIDMASA_FB1);
			pcdev->fb1_active = buf;
			dma_inten = CSICR1_FB1_DMA_INTEN;
		} else if (pcdev->fb2_active == NULL) {
			writel(videobuf_to_dma_contig(vb),
					pcdev->base_csi + CSIDMASA_FB2);
			pcdev->fb2_active = buf;
			dma_inten = CSICR1_FB2_DMA_INTEN;
		}

		if (dma_inten) {
			list_del(&vb->queue);
			vb->state = VIDEOBUF_ACTIVE;

			csicr3 = readl(pcdev->base_csi + CSICR3);

			/* Reflash DMA */
			writel(csicr3 | CSICR3_DMA_REFLASH_RFF,
					pcdev->base_csi + CSICR3);

			/* clear & enable interrupts */
			writel(dma_inten, pcdev->base_csi + CSISR);
			pcdev->csicr1 |= dma_inten;
			writel(pcdev->csicr1, pcdev->base_csi + CSICR1);

			/* enable DMA */
			csicr3 |= CSICR3_DMA_REQ_EN_RFF | CSICR3_RXFF_LEVEL(1);
			writel(csicr3, pcdev->base_csi + CSICR3);
		}
	}

out:
	spin_unlock_irqrestore(&pcdev->lock, flags);
}

static void mx2_videobuf_release(struct videobuf_queue *vq,
				 struct videobuf_buffer *vb)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct mx2_camera_dev *pcdev = ici->priv;
	struct mx2_buffer *buf = container_of(vb, struct mx2_buffer, vb);
	unsigned long flags;

#ifdef DEBUG
	dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %d\n", __func__,
		vb, vb->baddr, vb->bsize);

	switch (vb->state) {
	case VIDEOBUF_ACTIVE:
		dev_info(&icd->dev, "%s (active)\n", __func__);
		break;
	case VIDEOBUF_QUEUED:
		dev_info(&icd->dev, "%s (queued)\n", __func__);
		break;
	case VIDEOBUF_PREPARED:
		dev_info(&icd->dev, "%s (prepared)\n", __func__);
		break;
	default:
		dev_info(&icd->dev, "%s (unknown) %d\n", __func__,
				vb->state);
		break;
	}
#endif

	/*
	 * Terminate only queued but inactive buffers. Active buffers are
	 * released when they become inactive after videobuf_waiton().
	 *
	 * FIXME: implement forced termination of active buffers for mx27 and
	 * mx27 eMMA, so that the user won't get stuck in an uninterruptible
	 * state. This requires a specific handling for each of the these DMA
	 * types.
	 */
	spin_lock_irqsave(&pcdev->lock, flags);
	if (vb->state == VIDEOBUF_QUEUED) {
		list_del(&vb->queue);
		vb->state = VIDEOBUF_ERROR;
	} else if (cpu_is_mx25() && vb->state == VIDEOBUF_ACTIVE) {
		if (pcdev->fb1_active == buf) {
			pcdev->csicr1 &= ~CSICR1_FB1_DMA_INTEN;
			writel(0, pcdev->base_csi + CSIDMASA_FB1);
			pcdev->fb1_active = NULL;
		} else if (pcdev->fb2_active == buf) {
			pcdev->csicr1 &= ~CSICR1_FB2_DMA_INTEN;
			writel(0, pcdev->base_csi + CSIDMASA_FB2);
			pcdev->fb2_active = NULL;
		}
		writel(pcdev->csicr1, pcdev->base_csi + CSICR1);
		vb->state = VIDEOBUF_ERROR;
	}
	spin_unlock_irqrestore(&pcdev->lock, flags);

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops mx2_videobuf_ops = {
	.buf_setup      = mx2_videobuf_setup,
	.buf_prepare    = mx2_videobuf_prepare,
	.buf_queue      = mx2_videobuf_queue,
	.buf_release    = mx2_videobuf_release,
};

static void mx2_camera_init_videobuf(struct videobuf_queue *q,
			      struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct mx2_camera_dev *pcdev = ici->priv;

	videobuf_queue_dma_contig_init(q, &mx2_videobuf_ops, pcdev->dev,
			&pcdev->lock, V4L2_BUF_TYPE_VIDEO_CAPTURE,
			V4L2_FIELD_NONE, sizeof(struct mx2_buffer),
			icd, &icd->video_lock);
}

#define MX2_BUS_FLAGS	(SOCAM_DATAWIDTH_8 | \
			SOCAM_MASTER | \
			SOCAM_VSYNC_ACTIVE_HIGH | \
			SOCAM_VSYNC_ACTIVE_LOW | \
			SOCAM_HSYNC_ACTIVE_HIGH | \
			SOCAM_HSYNC_ACTIVE_LOW | \
			SOCAM_PCLK_SAMPLE_RISING | \
			SOCAM_PCLK_SAMPLE_FALLING | \
			SOCAM_DATA_ACTIVE_HIGH | \
			SOCAM_DATA_ACTIVE_LOW)

static int mx27_camera_emma_prp_reset(struct mx2_camera_dev *pcdev)
{
	u32 cntl;
	int count = 0;

	cntl = readl(pcdev->base_emma + PRP_CNTL);
	writel(PRP_CNTL_SWRST, pcdev->base_emma + PRP_CNTL);
	while (count++ < 100) {
		if (!(readl(pcdev->base_emma + PRP_CNTL) & PRP_CNTL_SWRST))
			return 0;
		barrier();
		udelay(1);
	}

	return -ETIMEDOUT;
}

static void mx27_camera_emma_buf_init(struct soc_camera_device *icd,
		int bytesperline)
{
	struct soc_camera_host *ici =
		to_soc_camera_host(icd->dev.parent);
	struct mx2_camera_dev *pcdev = ici->priv;

	writel(pcdev->discard_buffer_dma,
			pcdev->base_emma + PRP_DEST_RGB1_PTR);
	writel(pcdev->discard_buffer_dma,
			pcdev->base_emma + PRP_DEST_RGB2_PTR);

	/*
	 * We only use the EMMA engine to get rid of the broken
	 * DMA Engine. No color space consversion at the moment.
	 * We set the incomming and outgoing pixelformat to an
	 * 16 Bit wide format and adjust the bytesperline
	 * accordingly. With this configuration the inputdata
	 * will not be changed by the emma and could be any type
	 * of 16 Bit Pixelformat.
	 */
	writel(PRP_CNTL_CH1EN |
			PRP_CNTL_CSIEN |
			PRP_CNTL_DATA_IN_RGB16 |
			PRP_CNTL_CH1_OUT_RGB16 |
			PRP_CNTL_CH1_LEN |
			PRP_CNTL_CH1BYP |
			PRP_CNTL_CH1_TSKIP(0) |
			PRP_CNTL_IN_TSKIP(0),
			pcdev->base_emma + PRP_CNTL);

	writel(((bytesperline >> 1) << 16) | icd->user_height,
			pcdev->base_emma + PRP_SRC_FRAME_SIZE);
	writel(((bytesperline >> 1) << 16) | icd->user_height,
			pcdev->base_emma + PRP_CH1_OUT_IMAGE_SIZE);
	writel(bytesperline,
			pcdev->base_emma + PRP_DEST_CH1_LINE_STRIDE);
	writel(0x2ca00565, /* RGB565 */
			pcdev->base_emma + PRP_SRC_PIXEL_FORMAT_CNTL);
	writel(0x2ca00565, /* RGB565 */
			pcdev->base_emma + PRP_CH1_PIXEL_FORMAT_CNTL);

	/* Enable interrupts */
	writel(PRP_INTR_RDERR |
			PRP_INTR_CH1WERR |
			PRP_INTR_CH2WERR |
			PRP_INTR_CH1FC |
			PRP_INTR_CH2FC |
			PRP_INTR_LBOVF |
			PRP_INTR_CH2OVF,
			pcdev->base_emma + PRP_INTR_CNTL);
}

static int mx2_camera_set_bus_param(struct soc_camera_device *icd,
		__u32 pixfmt)
{
	struct soc_camera_host *ici =
		to_soc_camera_host(icd->dev.parent);
	struct mx2_camera_dev *pcdev = ici->priv;
	unsigned long camera_flags, common_flags;
	int ret = 0;
	int bytesperline;
	u32 csicr1 = pcdev->csicr1;

	camera_flags = icd->ops->query_bus_param(icd);

	common_flags = soc_camera_bus_param_compatible(camera_flags,
				MX2_BUS_FLAGS);
	if (!common_flags)
		return -EINVAL;

	if ((common_flags & SOCAM_HSYNC_ACTIVE_HIGH) &&
	    (common_flags & SOCAM_HSYNC_ACTIVE_LOW)) {
		if (pcdev->platform_flags & MX2_CAMERA_HSYNC_HIGH)
			common_flags &= ~SOCAM_HSYNC_ACTIVE_LOW;
		else
			common_flags &= ~SOCAM_HSYNC_ACTIVE_HIGH;
	}

	if ((common_flags & SOCAM_PCLK_SAMPLE_RISING) &&
	    (common_flags & SOCAM_PCLK_SAMPLE_FALLING)) {
		if (pcdev->platform_flags & MX2_CAMERA_PCLK_SAMPLE_RISING)
			common_flags &= ~SOCAM_PCLK_SAMPLE_FALLING;
		else
			common_flags &= ~SOCAM_PCLK_SAMPLE_RISING;
	}

	ret = icd->ops->set_bus_param(icd, common_flags);
	if (ret < 0)
		return ret;

	if (common_flags & SOCAM_PCLK_SAMPLE_RISING)
		csicr1 |= CSICR1_REDGE;
	if (common_flags & SOCAM_VSYNC_ACTIVE_HIGH)
		csicr1 |= CSICR1_SOF_POL;
	if (common_flags & SOCAM_HSYNC_ACTIVE_HIGH)
		csicr1 |= CSICR1_HSYNC_POL;
	if (pcdev->platform_flags & MX2_CAMERA_SWAP16)
		csicr1 |= CSICR1_SWAP16_EN;
	if (pcdev->platform_flags & MX2_CAMERA_EXT_VSYNC)
		csicr1 |= CSICR1_EXT_VSYNC;
	if (pcdev->platform_flags & MX2_CAMERA_CCIR)
		csicr1 |= CSICR1_CCIR_EN;
	if (pcdev->platform_flags & MX2_CAMERA_CCIR_INTERLACE)
		csicr1 |= CSICR1_CCIR_MODE;
	if (pcdev->platform_flags & MX2_CAMERA_GATED_CLOCK)
		csicr1 |= CSICR1_GCLK_MODE;
	if (pcdev->platform_flags & MX2_CAMERA_INV_DATA)
		csicr1 |= CSICR1_INV_DATA;
	if (pcdev->platform_flags & MX2_CAMERA_PACK_DIR_MSB)
		csicr1 |= CSICR1_PACK_DIR;

	pcdev->csicr1 = csicr1;

	bytesperline = soc_mbus_bytes_per_line(icd->user_width,
			icd->current_fmt->host_fmt);
	if (bytesperline < 0)
		return bytesperline;

	if (mx27_camera_emma(pcdev)) {
		ret = mx27_camera_emma_prp_reset(pcdev);
		if (ret)
			return ret;

		if (pcdev->discard_buffer)
			dma_free_coherent(ici->v4l2_dev.dev,
				pcdev->discard_size, pcdev->discard_buffer,
				pcdev->discard_buffer_dma);

		/*
		 * I didn't manage to properly enable/disable the prp
		 * on a per frame basis during running transfers,
		 * thus we allocate a buffer here and use it to
		 * discard frames when no buffer is available.
		 * Feel free to work on this ;)
		 */
		pcdev->discard_size = icd->user_height * bytesperline;
		pcdev->discard_buffer = dma_alloc_coherent(ici->v4l2_dev.dev,
				pcdev->discard_size, &pcdev->discard_buffer_dma,
				GFP_KERNEL);
		if (!pcdev->discard_buffer)
			return -ENOMEM;

		mx27_camera_emma_buf_init(icd, bytesperline);
	} else if (cpu_is_mx25()) {
		writel((bytesperline * icd->user_height) >> 2,
				pcdev->base_csi + CSIRXCNT);
		writel((bytesperline << 16) | icd->user_height,
				pcdev->base_csi + CSIIMAG_PARA);
	}

	writel(pcdev->csicr1, pcdev->base_csi + CSICR1);

	return 0;
}

static int mx2_camera_set_crop(struct soc_camera_device *icd,
				struct v4l2_crop *a)
{
	struct v4l2_rect *rect = &a->c;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_mbus_framefmt mf;
	int ret;

	soc_camera_limit_side(&rect->left, &rect->width, 0, 2, 4096);
	soc_camera_limit_side(&rect->top, &rect->height, 0, 2, 4096);

	ret = v4l2_subdev_call(sd, video, s_crop, a);
	if (ret < 0)
		return ret;

	/* The capture device might have changed its output  */
	ret = v4l2_subdev_call(sd, video, g_mbus_fmt, &mf);
	if (ret < 0)
		return ret;

	dev_dbg(icd->dev.parent, "Sensor cropped %dx%d\n",
		mf.width, mf.height);

	icd->user_width		= mf.width;
	icd->user_height	= mf.height;

	return ret;
}

static int mx2_camera_set_fmt(struct soc_camera_device *icd,
			       struct v4l2_format *f)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	int ret;

	xlate = soc_camera_xlate_by_fourcc(icd, pix->pixelformat);
	if (!xlate) {
		dev_warn(icd->dev.parent, "Format %x not found\n",
				pix->pixelformat);
		return -EINVAL;
	}

	mf.width	= pix->width;
	mf.height	= pix->height;
	mf.field	= pix->field;
	mf.colorspace	= pix->colorspace;
	mf.code		= xlate->code;

	ret = v4l2_subdev_call(sd, video, s_mbus_fmt, &mf);
	if (ret < 0 && ret != -ENOIOCTLCMD)
		return ret;

	if (mf.code != xlate->code)
		return -EINVAL;

	pix->width		= mf.width;
	pix->height		= mf.height;
	pix->field		= mf.field;
	pix->colorspace		= mf.colorspace;
	icd->current_fmt	= xlate;

	return 0;
}

static int mx2_camera_try_fmt(struct soc_camera_device *icd,
				  struct v4l2_format *f)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	__u32 pixfmt = pix->pixelformat;
	unsigned int width_limit;
	int ret;

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (pixfmt && !xlate) {
		dev_warn(icd->dev.parent, "Format %x not found\n", pixfmt);
		return -EINVAL;
	}

	/* FIXME: implement MX27 limits */

	/* limit to MX25 hardware capabilities */
	if (cpu_is_mx25()) {
		if (xlate->host_fmt->bits_per_sample <= 8)
			width_limit = 0xffff * 4;
		else
			width_limit = 0xffff * 2;
		/* CSIIMAG_PARA limit */
		if (pix->width > width_limit)
			pix->width = width_limit;
		if (pix->height > 0xffff)
			pix->height = 0xffff;

		pix->bytesperline = soc_mbus_bytes_per_line(pix->width,
				xlate->host_fmt);
		if (pix->bytesperline < 0)
			return pix->bytesperline;
		pix->sizeimage = pix->height * pix->bytesperline;
		if (pix->sizeimage > (4 * 0x3ffff)) { /* CSIRXCNT limit */
			dev_warn(icd->dev.parent,
					"Image size (%u) above limit\n",
					pix->sizeimage);
			return -EINVAL;
		}
	}

	/* limit to sensor capabilities */
	mf.width	= pix->width;
	mf.height	= pix->height;
	mf.field	= pix->field;
	mf.colorspace	= pix->colorspace;
	mf.code		= xlate->code;

	ret = v4l2_subdev_call(sd, video, try_mbus_fmt, &mf);
	if (ret < 0)
		return ret;

	if (mf.field == V4L2_FIELD_ANY)
		mf.field = V4L2_FIELD_NONE;
	if (mf.field != V4L2_FIELD_NONE) {
		dev_err(icd->dev.parent, "Field type %d unsupported.\n",
				mf.field);
		return -EINVAL;
	}

	pix->width	= mf.width;
	pix->height	= mf.height;
	pix->field	= mf.field;
	pix->colorspace	= mf.colorspace;

	return 0;
}

static int mx2_camera_querycap(struct soc_camera_host *ici,
			       struct v4l2_capability *cap)
{
	/* cap->name is set by the friendly caller:-> */
	strlcpy(cap->card, MX2_CAM_DRIVER_DESCRIPTION, sizeof(cap->card));
	cap->version = MX2_CAM_VERSION_CODE;
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

	return 0;
}

static int mx2_camera_reqbufs(struct soc_camera_device *icd,
			      struct v4l2_requestbuffers *p)
{
	int i;

	for (i = 0; i < p->count; i++) {
		struct mx2_buffer *buf = container_of(icd->vb_vidq.bufs[i],
						      struct mx2_buffer, vb);
		INIT_LIST_HEAD(&buf->vb.queue);
	}

	return 0;
}

#ifdef CONFIG_MACH_MX27
static void mx27_camera_frame_done(struct mx2_camera_dev *pcdev, int state)
{
	struct videobuf_buffer *vb;
	struct mx2_buffer *buf;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pcdev->lock, flags);

	if (!pcdev->active) {
		dev_err(pcdev->dev, "%s called with no active buffer!\n",
				__func__);
		goto out;
	}

	vb = &pcdev->active->vb;
	buf = container_of(vb, struct mx2_buffer, vb);
	WARN_ON(list_empty(&vb->queue));
	dev_dbg(pcdev->dev, "%s (vb=0x%p) 0x%08lx %d\n", __func__,
		vb, vb->baddr, vb->bsize);

	/* _init is used to debug races, see comment in pxa_camera_reqbufs() */
	list_del_init(&vb->queue);
	vb->state = state;
	do_gettimeofday(&vb->ts);
	vb->field_count++;

	wake_up(&vb->done);

	if (list_empty(&pcdev->capture)) {
		pcdev->active = NULL;
		goto out;
	}

	pcdev->active = list_entry(pcdev->capture.next,
			struct mx2_buffer, vb.queue);

	vb = &pcdev->active->vb;
	vb->state = VIDEOBUF_ACTIVE;

	ret = imx_dma_setup_single(pcdev->dma, videobuf_to_dma_contig(vb),
			vb->size, (u32)pcdev->base_dma + 0x10, DMA_MODE_READ);

	if (ret) {
		vb->state = VIDEOBUF_ERROR;
		pcdev->active = NULL;
		wake_up(&vb->done);
	}

out:
	spin_unlock_irqrestore(&pcdev->lock, flags);
}

static void mx27_camera_dma_err_callback(int channel, void *data, int err)
{
	struct mx2_camera_dev *pcdev = data;

	mx27_camera_frame_done(pcdev, VIDEOBUF_ERROR);
}

static void mx27_camera_dma_callback(int channel, void *data)
{
	struct mx2_camera_dev *pcdev = data;

	mx27_camera_frame_done(pcdev, VIDEOBUF_DONE);
}

#define DMA_REQ_CSI_RX          31 /* FIXME: Add this to a resource */

static int __devinit mx27_camera_dma_init(struct platform_device *pdev,
		struct mx2_camera_dev *pcdev)
{
	int err;

	pcdev->dma = imx_dma_request_by_prio("CSI RX DMA", DMA_PRIO_HIGH);
	if (pcdev->dma < 0) {
		dev_err(&pdev->dev, "%s failed to request DMA channel\n",
				__func__);
		return pcdev->dma;
	}

	err = imx_dma_setup_handlers(pcdev->dma, mx27_camera_dma_callback,
					mx27_camera_dma_err_callback, pcdev);
	if (err) {
		dev_err(&pdev->dev, "%s failed to set DMA callback\n",
				__func__);
		goto err_out;
	}

	err = imx_dma_config_channel(pcdev->dma,
			IMX_DMA_MEMSIZE_32 | IMX_DMA_TYPE_FIFO,
			IMX_DMA_MEMSIZE_32 | IMX_DMA_TYPE_LINEAR,
			DMA_REQ_CSI_RX, 1);
	if (err) {
		dev_err(&pdev->dev, "%s failed to config DMA channel\n",
				__func__);
		goto err_out;
	}

	imx_dma_config_burstlen(pcdev->dma, 64);

	return 0;

err_out:
	imx_dma_free(pcdev->dma);

	return err;
}
#endif /* CONFIG_MACH_MX27 */

static unsigned int mx2_camera_poll(struct file *file, poll_table *pt)
{
	struct soc_camera_device *icd = file->private_data;

	return videobuf_poll_stream(file, &icd->vb_vidq, pt);
}

static struct soc_camera_host_ops mx2_soc_camera_host_ops = {
	.owner		= THIS_MODULE,
	.add		= mx2_camera_add_device,
	.remove		= mx2_camera_remove_device,
	.set_fmt	= mx2_camera_set_fmt,
	.set_crop	= mx2_camera_set_crop,
	.try_fmt	= mx2_camera_try_fmt,
	.init_videobuf	= mx2_camera_init_videobuf,
	.reqbufs	= mx2_camera_reqbufs,
	.poll		= mx2_camera_poll,
	.querycap	= mx2_camera_querycap,
	.set_bus_param	= mx2_camera_set_bus_param,
};

static void mx27_camera_frame_done_emma(struct mx2_camera_dev *pcdev,
		int bufnum, int state)
{
	struct mx2_buffer *buf;
	struct videobuf_buffer *vb;
	unsigned long phys;

	if (!list_empty(&pcdev->active_bufs)) {
		buf = list_entry(pcdev->active_bufs.next,
			struct mx2_buffer, vb.queue);

		BUG_ON(buf->bufnum != bufnum);

		vb = &buf->vb;
#ifdef DEBUG
		phys = videobuf_to_dma_contig(vb);
		if (readl(pcdev->base_emma + PRP_DEST_RGB1_PTR + 4 * bufnum)
				!= phys) {
			dev_err(pcdev->dev, "%p != %p\n", phys,
					readl(pcdev->base_emma +
						PRP_DEST_RGB1_PTR +
						4 * bufnum));
		}
#endif
		dev_dbg(pcdev->dev, "%s (vb=0x%p) 0x%08lx %d\n", __func__, vb,
				vb->baddr, vb->bsize);

		list_del(&vb->queue);
		vb->state = state;
		do_gettimeofday(&vb->ts);
		vb->field_count++;

		wake_up(&vb->done);
	}

	if (list_empty(&pcdev->capture)) {
		writel(pcdev->discard_buffer_dma, pcdev->base_emma +
				PRP_DEST_RGB1_PTR + 4 * bufnum);
		return;
	}

	buf = list_entry(pcdev->capture.next,
			struct mx2_buffer, vb.queue);

	buf->bufnum = !bufnum;

	list_move_tail(pcdev->capture.next, &pcdev->active_bufs);

	vb = &buf->vb;
	vb->state = VIDEOBUF_ACTIVE;

	phys = videobuf_to_dma_contig(vb);
	writel(phys, pcdev->base_emma + PRP_DEST_RGB1_PTR + 4 * bufnum);
}

static irqreturn_t mx27_camera_emma_irq(int irq_emma, void *data)
{
	struct mx2_camera_dev *pcdev = data;
	unsigned int status = readl(pcdev->base_emma + PRP_INTRSTATUS);
	struct mx2_buffer *buf;

	if (status & (1 << 7)) { /* overflow */
		u32 cntl;
		/*
		 * We only disable channel 1 here since this is the only
		 * enabled channel
		 *
		 * FIXME: the correct DMA overflow handling should be resetting
		 * the buffer, returning an error frame, and continuing with
		 * the next one.
		 */
		cntl = readl(pcdev->base_emma + PRP_CNTL);
		writel(cntl & ~PRP_CNTL_CH1EN, pcdev->base_emma + PRP_CNTL);
		writel(cntl, pcdev->base_emma + PRP_CNTL);
	}
	if ((status & (3 << 5)) == (3 << 5)
			&& !list_empty(&pcdev->active_bufs)) {
		/*
		 * Both buffers have triggered, process the one we're expecting
		 * to first
		 */
		buf = list_entry(pcdev->active_bufs.next,
			struct mx2_buffer, vb.queue);
		mx27_camera_frame_done_emma(pcdev, buf->bufnum, VIDEOBUF_DONE);
		status &= ~(1 << (6 - buf->bufnum)); /* mark processed */
	}
	if (status & (1 << 6))
		mx27_camera_frame_done_emma(pcdev, 0, VIDEOBUF_DONE);
	if (status & (1 << 5))
		mx27_camera_frame_done_emma(pcdev, 1, VIDEOBUF_DONE);

	writel(status, pcdev->base_emma + PRP_INTRSTATUS);

	return IRQ_HANDLED;
}

static int __devinit mx27_camera_emma_init(struct mx2_camera_dev *pcdev)
{
	struct resource *res_emma = pcdev->res_emma;
	int err = 0;

	if (!request_mem_region(res_emma->start, resource_size(res_emma),
				MX2_CAM_DRV_NAME)) {
		err = -EBUSY;
		goto out;
	}

	pcdev->base_emma = ioremap(res_emma->start, resource_size(res_emma));
	if (!pcdev->base_emma) {
		err = -ENOMEM;
		goto exit_release;
	}

	err = request_irq(pcdev->irq_emma, mx27_camera_emma_irq, 0,
			MX2_CAM_DRV_NAME, pcdev);
	if (err) {
		dev_err(pcdev->dev, "Camera EMMA interrupt register failed \n");
		goto exit_iounmap;
	}

	pcdev->clk_emma = clk_get(NULL, "emma");
	if (IS_ERR(pcdev->clk_emma)) {
		err = PTR_ERR(pcdev->clk_emma);
		goto exit_free_irq;
	}

	clk_enable(pcdev->clk_emma);

	err = mx27_camera_emma_prp_reset(pcdev);
	if (err)
		goto exit_clk_emma_put;

	return err;

exit_clk_emma_put:
	clk_disable(pcdev->clk_emma);
	clk_put(pcdev->clk_emma);
exit_free_irq:
	free_irq(pcdev->irq_emma, pcdev);
exit_iounmap:
	iounmap(pcdev->base_emma);
exit_release:
	release_mem_region(res_emma->start, resource_size(res_emma));
out:
	return err;
}

static int __devinit mx2_camera_probe(struct platform_device *pdev)
{
	struct mx2_camera_dev *pcdev;
	struct resource *res_csi, *res_emma;
	void __iomem *base_csi;
	int irq_csi, irq_emma;
	irq_handler_t mx2_cam_irq_handler = cpu_is_mx25() ? mx25_camera_irq
		: mx27_camera_irq;
	int err = 0;

	dev_dbg(&pdev->dev, "initialising\n");

	res_csi = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq_csi = platform_get_irq(pdev, 0);
	if (res_csi == NULL || irq_csi < 0) {
		dev_err(&pdev->dev, "Missing platform resources data\n");
		err = -ENODEV;
		goto exit;
	}

	pcdev = kzalloc(sizeof(*pcdev), GFP_KERNEL);
	if (!pcdev) {
		dev_err(&pdev->dev, "Could not allocate pcdev\n");
		err = -ENOMEM;
		goto exit;
	}

	pcdev->clk_csi = clk_get(&pdev->dev, NULL);
	if (IS_ERR(pcdev->clk_csi)) {
		err = PTR_ERR(pcdev->clk_csi);
		goto exit_kfree;
	}

	dev_dbg(&pdev->dev, "Camera clock frequency: %ld\n",
			clk_get_rate(pcdev->clk_csi));

	/* Initialize DMA */
#ifdef CONFIG_MACH_MX27
	if (cpu_is_mx27()) {
		err = mx27_camera_dma_init(pdev, pcdev);
		if (err)
			goto exit_clk_put;
	}
#endif /* CONFIG_MACH_MX27 */

	pcdev->res_csi = res_csi;
	pcdev->pdata = pdev->dev.platform_data;
	if (pcdev->pdata) {
		long rate;

		pcdev->platform_flags = pcdev->pdata->flags;

		rate = clk_round_rate(pcdev->clk_csi, pcdev->pdata->clk * 2);
		if (rate <= 0) {
			err = -ENODEV;
			goto exit_dma_free;
		}
		err = clk_set_rate(pcdev->clk_csi, rate);
		if (err < 0)
			goto exit_dma_free;
	}

	INIT_LIST_HEAD(&pcdev->capture);
	INIT_LIST_HEAD(&pcdev->active_bufs);
	spin_lock_init(&pcdev->lock);

	/*
	 * Request the regions.
	 */
	if (!request_mem_region(res_csi->start, resource_size(res_csi),
				MX2_CAM_DRV_NAME)) {
		err = -EBUSY;
		goto exit_dma_free;
	}

	base_csi = ioremap(res_csi->start, resource_size(res_csi));
	if (!base_csi) {
		err = -ENOMEM;
		goto exit_release;
	}
	pcdev->irq_csi = irq_csi;
	pcdev->base_csi = base_csi;
	pcdev->base_dma = res_csi->start;
	pcdev->dev = &pdev->dev;

	err = request_irq(pcdev->irq_csi, mx2_cam_irq_handler, 0,
			MX2_CAM_DRV_NAME, pcdev);
	if (err) {
		dev_err(pcdev->dev, "Camera interrupt register failed \n");
		goto exit_iounmap;
	}

	if (cpu_is_mx27()) {
		/* EMMA support */
		res_emma = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		irq_emma = platform_get_irq(pdev, 1);

		if (res_emma && irq_emma >= 0) {
			dev_info(&pdev->dev, "Using EMMA\n");
			pcdev->use_emma = 1;
			pcdev->res_emma = res_emma;
			pcdev->irq_emma = irq_emma;
			if (mx27_camera_emma_init(pcdev))
				goto exit_free_irq;
		}
	}

	pcdev->soc_host.drv_name	= MX2_CAM_DRV_NAME,
	pcdev->soc_host.ops		= &mx2_soc_camera_host_ops,
	pcdev->soc_host.priv		= pcdev;
	pcdev->soc_host.v4l2_dev.dev	= &pdev->dev;
	pcdev->soc_host.nr		= pdev->id;
	err = soc_camera_host_register(&pcdev->soc_host);
	if (err)
		goto exit_free_emma;

	dev_info(&pdev->dev, "MX2 Camera (CSI) driver probed, clock frequency: %ld\n",
			clk_get_rate(pcdev->clk_csi));

	return 0;

exit_free_emma:
	if (mx27_camera_emma(pcdev)) {
		free_irq(pcdev->irq_emma, pcdev);
		clk_disable(pcdev->clk_emma);
		clk_put(pcdev->clk_emma);
		iounmap(pcdev->base_emma);
		release_mem_region(res_emma->start, resource_size(res_emma));
	}
exit_free_irq:
	free_irq(pcdev->irq_csi, pcdev);
exit_iounmap:
	iounmap(base_csi);
exit_release:
	release_mem_region(res_csi->start, resource_size(res_csi));
exit_dma_free:
#ifdef CONFIG_MACH_MX27
	if (cpu_is_mx27())
		imx_dma_free(pcdev->dma);
exit_clk_put:
	clk_put(pcdev->clk_csi);
#endif /* CONFIG_MACH_MX27 */
exit_kfree:
	kfree(pcdev);
exit:
	return err;
}

static int __devexit mx2_camera_remove(struct platform_device *pdev)
{
	struct soc_camera_host *soc_host = to_soc_camera_host(&pdev->dev);
	struct mx2_camera_dev *pcdev = container_of(soc_host,
			struct mx2_camera_dev, soc_host);
	struct resource *res;

	clk_put(pcdev->clk_csi);
#ifdef CONFIG_MACH_MX27
	if (cpu_is_mx27())
		imx_dma_free(pcdev->dma);
#endif /* CONFIG_MACH_MX27 */
	free_irq(pcdev->irq_csi, pcdev);
	if (mx27_camera_emma(pcdev))
		free_irq(pcdev->irq_emma, pcdev);

	soc_camera_host_unregister(&pcdev->soc_host);

	iounmap(pcdev->base_csi);

	if (mx27_camera_emma(pcdev)) {
		clk_disable(pcdev->clk_emma);
		clk_put(pcdev->clk_emma);
		iounmap(pcdev->base_emma);
		res = pcdev->res_emma;
		release_mem_region(res->start, resource_size(res));
	}

	res = pcdev->res_csi;
	release_mem_region(res->start, resource_size(res));

	kfree(pcdev);

	dev_info(&pdev->dev, "MX2 Camera driver unloaded\n");

	return 0;
}

static struct platform_driver mx2_camera_driver = {
	.driver 	= {
		.name	= MX2_CAM_DRV_NAME,
	},
	.remove		= __devexit_p(mx2_camera_remove),
};


static int __init mx2_camera_init(void)
{
	return platform_driver_probe(&mx2_camera_driver, &mx2_camera_probe);
}

static void __exit mx2_camera_exit(void)
{
	return platform_driver_unregister(&mx2_camera_driver);
}

module_init(mx2_camera_init);
module_exit(mx2_camera_exit);

MODULE_DESCRIPTION("i.MX27/i.MX25 SoC Camera Host driver");
MODULE_AUTHOR("Sascha Hauer <sha@pengutronix.de>");
MODULE_LICENSE("GPL");
