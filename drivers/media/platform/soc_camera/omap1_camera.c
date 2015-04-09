/*
 * V4L2 SoC Camera driver for OMAP1 Camera Interface
 *
 * Copyright (C) 2010, Janusz Krzysztofik <jkrzyszt@tis.icnet.pl>
 *
 * Based on V4L2 Driver for i.MXL/i.MXL camera (CSI) host
 * Copyright (C) 2008, Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 * Copyright (C) 2009, Darius Augulis <augulis.darius@gmail.com>
 *
 * Based on PXA SoC camera driver
 * Copyright (C) 2006, Sascha Hauer, Pengutronix
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * Hardware specific bits initialy based on former work by Matt Callow
 * drivers/media/platform/omap/omap1510cam.c
 * Copyright (C) 2006 Matt Callow
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <media/omap1_camera.h>
#include <media/soc_camera.h>
#include <media/soc_mediabus.h>
#include <media/videobuf-dma-contig.h>
#include <media/videobuf-dma-sg.h>

#include <linux/omap-dma.h>


#define DRIVER_NAME		"omap1-camera"
#define DRIVER_VERSION		"0.0.2"

#define OMAP_DMA_CAMERA_IF_RX		20

/*
 * ---------------------------------------------------------------------------
 *  OMAP1 Camera Interface registers
 * ---------------------------------------------------------------------------
 */

#define REG_CTRLCLOCK		0x00
#define REG_IT_STATUS		0x04
#define REG_MODE		0x08
#define REG_STATUS		0x0C
#define REG_CAMDATA		0x10
#define REG_GPIO		0x14
#define REG_PEAK_COUNTER	0x18

/* CTRLCLOCK bit shifts */
#define LCLK_EN			BIT(7)
#define DPLL_EN			BIT(6)
#define MCLK_EN			BIT(5)
#define CAMEXCLK_EN		BIT(4)
#define POLCLK			BIT(3)
#define FOSCMOD_SHIFT		0
#define FOSCMOD_MASK		(0x7 << FOSCMOD_SHIFT)
#define FOSCMOD_12MHz		0x0
#define FOSCMOD_6MHz		0x2
#define FOSCMOD_9_6MHz		0x4
#define FOSCMOD_24MHz		0x5
#define FOSCMOD_8MHz		0x6

/* IT_STATUS bit shifts */
#define DATA_TRANSFER		BIT(5)
#define FIFO_FULL		BIT(4)
#define H_DOWN			BIT(3)
#define H_UP			BIT(2)
#define V_DOWN			BIT(1)
#define V_UP			BIT(0)

/* MODE bit shifts */
#define RAZ_FIFO		BIT(18)
#define EN_FIFO_FULL		BIT(17)
#define EN_NIRQ			BIT(16)
#define THRESHOLD_SHIFT		9
#define THRESHOLD_MASK		(0x7f << THRESHOLD_SHIFT)
#define DMA			BIT(8)
#define EN_H_DOWN		BIT(7)
#define EN_H_UP			BIT(6)
#define EN_V_DOWN		BIT(5)
#define EN_V_UP			BIT(4)
#define ORDERCAMD		BIT(3)

#define IRQ_MASK		(EN_V_UP | EN_V_DOWN | EN_H_UP | EN_H_DOWN | \
				 EN_NIRQ | EN_FIFO_FULL)

/* STATUS bit shifts */
#define HSTATUS			BIT(1)
#define VSTATUS			BIT(0)

/* GPIO bit shifts */
#define CAM_RST			BIT(0)

/* end of OMAP1 Camera Interface registers */


#define SOCAM_BUS_FLAGS	(V4L2_MBUS_MASTER | \
			V4L2_MBUS_HSYNC_ACTIVE_HIGH | V4L2_MBUS_VSYNC_ACTIVE_HIGH | \
			V4L2_MBUS_PCLK_SAMPLE_RISING | V4L2_MBUS_PCLK_SAMPLE_FALLING | \
			V4L2_MBUS_DATA_ACTIVE_HIGH)


#define FIFO_SIZE		((THRESHOLD_MASK >> THRESHOLD_SHIFT) + 1)
#define FIFO_SHIFT		__fls(FIFO_SIZE)

#define DMA_BURST_SHIFT		(1 + OMAP_DMA_DATA_BURST_4)
#define DMA_BURST_SIZE		(1 << DMA_BURST_SHIFT)

#define DMA_ELEMENT_SHIFT	OMAP_DMA_DATA_TYPE_S32
#define DMA_ELEMENT_SIZE	(1 << DMA_ELEMENT_SHIFT)

#define DMA_FRAME_SHIFT_CONTIG	(FIFO_SHIFT - 1)
#define DMA_FRAME_SHIFT_SG	DMA_BURST_SHIFT

#define DMA_FRAME_SHIFT(x)	((x) == OMAP1_CAM_DMA_CONTIG ? \
						DMA_FRAME_SHIFT_CONTIG : \
						DMA_FRAME_SHIFT_SG)
#define DMA_FRAME_SIZE(x)	(1 << DMA_FRAME_SHIFT(x))
#define DMA_SYNC		OMAP_DMA_SYNC_FRAME
#define THRESHOLD_LEVEL		DMA_FRAME_SIZE


#define MAX_VIDEO_MEM		4	/* arbitrary video memory limit in MB */


/*
 * Structures
 */

/* buffer for one video frame */
struct omap1_cam_buf {
	struct videobuf_buffer		vb;
	u32	code;
	int				inwork;
	struct scatterlist		*sgbuf;
	int				sgcount;
	int				bytes_left;
	enum videobuf_state		result;
};

struct omap1_cam_dev {
	struct soc_camera_host		soc_host;
	struct clk			*clk;

	unsigned int			irq;
	void __iomem			*base;

	int				dma_ch;

	struct omap1_cam_platform_data	*pdata;
	struct resource			*res;
	unsigned long			pflags;
	unsigned long			camexclk;

	struct list_head		capture;

	/* lock used to protect videobuf */
	spinlock_t			lock;

	/* Pointers to DMA buffers */
	struct omap1_cam_buf		*active;
	struct omap1_cam_buf		*ready;

	enum omap1_cam_vb_mode		vb_mode;
	int				(*mmap_mapper)(struct videobuf_queue *q,
						struct videobuf_buffer *buf,
						struct vm_area_struct *vma);

	u32				reg_cache[0];
};


static void cam_write(struct omap1_cam_dev *pcdev, u16 reg, u32 val)
{
	pcdev->reg_cache[reg / sizeof(u32)] = val;
	__raw_writel(val, pcdev->base + reg);
}

static u32 cam_read(struct omap1_cam_dev *pcdev, u16 reg, bool from_cache)
{
	return !from_cache ? __raw_readl(pcdev->base + reg) :
			pcdev->reg_cache[reg / sizeof(u32)];
}

#define CAM_READ(pcdev, reg) \
		cam_read(pcdev, REG_##reg, false)
#define CAM_WRITE(pcdev, reg, val) \
		cam_write(pcdev, REG_##reg, val)
#define CAM_READ_CACHE(pcdev, reg) \
		cam_read(pcdev, REG_##reg, true)

/*
 *  Videobuf operations
 */
static int omap1_videobuf_setup(struct videobuf_queue *vq, unsigned int *count,
		unsigned int *size)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct omap1_cam_dev *pcdev = ici->priv;

	*size = icd->sizeimage;

	if (!*count || *count < OMAP1_CAMERA_MIN_BUF_COUNT(pcdev->vb_mode))
		*count = OMAP1_CAMERA_MIN_BUF_COUNT(pcdev->vb_mode);

	if (*size * *count > MAX_VIDEO_MEM * 1024 * 1024)
		*count = (MAX_VIDEO_MEM * 1024 * 1024) / *size;

	dev_dbg(icd->parent,
			"%s: count=%d, size=%d\n", __func__, *count, *size);

	return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct omap1_cam_buf *buf,
		enum omap1_cam_vb_mode vb_mode)
{
	struct videobuf_buffer *vb = &buf->vb;

	BUG_ON(in_interrupt());

	videobuf_waiton(vq, vb, 0, 0);

	if (vb_mode == OMAP1_CAM_DMA_CONTIG) {
		videobuf_dma_contig_free(vq, vb);
	} else {
		struct soc_camera_device *icd = vq->priv_data;
		struct device *dev = icd->parent;
		struct videobuf_dmabuf *dma = videobuf_to_dma(vb);

		videobuf_dma_unmap(dev, dma);
		videobuf_dma_free(dma);
	}

	vb->state = VIDEOBUF_NEEDS_INIT;
}

static int omap1_videobuf_prepare(struct videobuf_queue *vq,
		struct videobuf_buffer *vb, enum v4l2_field field)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct omap1_cam_buf *buf = container_of(vb, struct omap1_cam_buf, vb);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct omap1_cam_dev *pcdev = ici->priv;
	int ret;

	WARN_ON(!list_empty(&vb->queue));

	BUG_ON(NULL == icd->current_fmt);

	buf->inwork = 1;

	if (buf->code != icd->current_fmt->code || vb->field != field ||
			vb->width  != icd->user_width ||
			vb->height != icd->user_height) {
		buf->code  = icd->current_fmt->code;
		vb->width  = icd->user_width;
		vb->height = icd->user_height;
		vb->field  = field;
		vb->state  = VIDEOBUF_NEEDS_INIT;
	}

	vb->size = icd->sizeimage;

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
	buf->inwork = 0;

	return 0;
fail:
	free_buffer(vq, buf, pcdev->vb_mode);
out:
	buf->inwork = 0;
	return ret;
}

static void set_dma_dest_params(int dma_ch, struct omap1_cam_buf *buf,
		enum omap1_cam_vb_mode vb_mode)
{
	dma_addr_t dma_addr;
	unsigned int block_size;

	if (vb_mode == OMAP1_CAM_DMA_CONTIG) {
		dma_addr = videobuf_to_dma_contig(&buf->vb);
		block_size = buf->vb.size;
	} else {
		if (WARN_ON(!buf->sgbuf)) {
			buf->result = VIDEOBUF_ERROR;
			return;
		}
		dma_addr = sg_dma_address(buf->sgbuf);
		if (WARN_ON(!dma_addr)) {
			buf->sgbuf = NULL;
			buf->result = VIDEOBUF_ERROR;
			return;
		}
		block_size = sg_dma_len(buf->sgbuf);
		if (WARN_ON(!block_size)) {
			buf->sgbuf = NULL;
			buf->result = VIDEOBUF_ERROR;
			return;
		}
		if (unlikely(buf->bytes_left < block_size))
			block_size = buf->bytes_left;
		if (WARN_ON(dma_addr & (DMA_FRAME_SIZE(vb_mode) *
				DMA_ELEMENT_SIZE - 1))) {
			dma_addr = ALIGN(dma_addr, DMA_FRAME_SIZE(vb_mode) *
					DMA_ELEMENT_SIZE);
			block_size &= ~(DMA_FRAME_SIZE(vb_mode) *
					DMA_ELEMENT_SIZE - 1);
		}
		buf->bytes_left -= block_size;
		buf->sgcount++;
	}

	omap_set_dma_dest_params(dma_ch,
		OMAP_DMA_PORT_EMIFF, OMAP_DMA_AMODE_POST_INC, dma_addr, 0, 0);
	omap_set_dma_transfer_params(dma_ch,
		OMAP_DMA_DATA_TYPE_S32, DMA_FRAME_SIZE(vb_mode),
		block_size >> (DMA_FRAME_SHIFT(vb_mode) + DMA_ELEMENT_SHIFT),
		DMA_SYNC, 0, 0);
}

static struct omap1_cam_buf *prepare_next_vb(struct omap1_cam_dev *pcdev)
{
	struct omap1_cam_buf *buf;

	/*
	 * If there is already a buffer pointed out by the pcdev->ready,
	 * (re)use it, otherwise try to fetch and configure a new one.
	 */
	buf = pcdev->ready;
	if (!buf) {
		if (list_empty(&pcdev->capture))
			return buf;
		buf = list_entry(pcdev->capture.next,
				struct omap1_cam_buf, vb.queue);
		buf->vb.state = VIDEOBUF_ACTIVE;
		pcdev->ready = buf;
		list_del_init(&buf->vb.queue);
	}

	if (pcdev->vb_mode == OMAP1_CAM_DMA_CONTIG) {
		/*
		 * In CONTIG mode, we can safely enter next buffer parameters
		 * into the DMA programming register set after the DMA
		 * has already been activated on the previous buffer
		 */
		set_dma_dest_params(pcdev->dma_ch, buf, pcdev->vb_mode);
	} else {
		/*
		 * In SG mode, the above is not safe since there are probably
		 * a bunch of sgbufs from previous sglist still pending.
		 * Instead, mark the sglist fresh for the upcoming
		 * try_next_sgbuf().
		 */
		buf->sgbuf = NULL;
	}

	return buf;
}

static struct scatterlist *try_next_sgbuf(int dma_ch, struct omap1_cam_buf *buf)
{
	struct scatterlist *sgbuf;

	if (likely(buf->sgbuf)) {
		/* current sglist is active */
		if (unlikely(!buf->bytes_left)) {
			/* indicate sglist complete */
			sgbuf = NULL;
		} else {
			/* process next sgbuf */
			sgbuf = sg_next(buf->sgbuf);
			if (WARN_ON(!sgbuf)) {
				buf->result = VIDEOBUF_ERROR;
			} else if (WARN_ON(!sg_dma_len(sgbuf))) {
				sgbuf = NULL;
				buf->result = VIDEOBUF_ERROR;
			}
		}
		buf->sgbuf = sgbuf;
	} else {
		/* sglist is fresh, initialize it before using */
		struct videobuf_dmabuf *dma = videobuf_to_dma(&buf->vb);

		sgbuf = dma->sglist;
		if (!(WARN_ON(!sgbuf))) {
			buf->sgbuf = sgbuf;
			buf->sgcount = 0;
			buf->bytes_left = buf->vb.size;
			buf->result = VIDEOBUF_DONE;
		}
	}
	if (sgbuf)
		/*
		 * Put our next sgbuf parameters (address, size)
		 * into the DMA programming register set.
		 */
		set_dma_dest_params(dma_ch, buf, OMAP1_CAM_DMA_SG);

	return sgbuf;
}

static void start_capture(struct omap1_cam_dev *pcdev)
{
	struct omap1_cam_buf *buf = pcdev->active;
	u32 ctrlclock = CAM_READ_CACHE(pcdev, CTRLCLOCK);
	u32 mode = CAM_READ_CACHE(pcdev, MODE) & ~EN_V_DOWN;

	if (WARN_ON(!buf))
		return;

	/*
	 * Enable start of frame interrupt, which we will use for activating
	 * our end of frame watchdog when capture actually starts.
	 */
	mode |= EN_V_UP;

	if (unlikely(ctrlclock & LCLK_EN))
		/* stop pixel clock before FIFO reset */
		CAM_WRITE(pcdev, CTRLCLOCK, ctrlclock & ~LCLK_EN);
	/* reset FIFO */
	CAM_WRITE(pcdev, MODE, mode | RAZ_FIFO);

	omap_start_dma(pcdev->dma_ch);

	if (pcdev->vb_mode == OMAP1_CAM_DMA_SG) {
		/*
		 * In SG mode, it's a good moment for fetching next sgbuf
		 * from the current sglist and, if available, already putting
		 * its parameters into the DMA programming register set.
		 */
		try_next_sgbuf(pcdev->dma_ch, buf);
	}

	/* (re)enable pixel clock */
	CAM_WRITE(pcdev, CTRLCLOCK, ctrlclock | LCLK_EN);
	/* release FIFO reset */
	CAM_WRITE(pcdev, MODE, mode);
}

static void suspend_capture(struct omap1_cam_dev *pcdev)
{
	u32 ctrlclock = CAM_READ_CACHE(pcdev, CTRLCLOCK);

	CAM_WRITE(pcdev, CTRLCLOCK, ctrlclock & ~LCLK_EN);
	omap_stop_dma(pcdev->dma_ch);
}

static void disable_capture(struct omap1_cam_dev *pcdev)
{
	u32 mode = CAM_READ_CACHE(pcdev, MODE);

	CAM_WRITE(pcdev, MODE, mode & ~(IRQ_MASK | DMA));
}

static void omap1_videobuf_queue(struct videobuf_queue *vq,
						struct videobuf_buffer *vb)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct omap1_cam_dev *pcdev = ici->priv;
	struct omap1_cam_buf *buf;
	u32 mode;

	list_add_tail(&vb->queue, &pcdev->capture);
	vb->state = VIDEOBUF_QUEUED;

	if (pcdev->active) {
		/*
		 * Capture in progress, so don't touch pcdev->ready even if
		 * empty. Since the transfer of the DMA programming register set
		 * content to the DMA working register set is done automatically
		 * by the DMA hardware, this can pretty well happen while we
		 * are keeping the lock here. Leave fetching it from the queue
		 * to be done when a next DMA interrupt occures instead.
		 */
		return;
	}

	WARN_ON(pcdev->ready);

	buf = prepare_next_vb(pcdev);
	if (WARN_ON(!buf))
		return;

	pcdev->active = buf;
	pcdev->ready = NULL;

	dev_dbg(icd->parent,
		"%s: capture not active, setup FIFO, start DMA\n", __func__);
	mode = CAM_READ_CACHE(pcdev, MODE) & ~THRESHOLD_MASK;
	mode |= THRESHOLD_LEVEL(pcdev->vb_mode) << THRESHOLD_SHIFT;
	CAM_WRITE(pcdev, MODE, mode | EN_FIFO_FULL | DMA);

	if (pcdev->vb_mode == OMAP1_CAM_DMA_SG) {
		/*
		 * In SG mode, the above prepare_next_vb() didn't actually
		 * put anything into the DMA programming register set,
		 * so we have to do it now, before activating DMA.
		 */
		try_next_sgbuf(pcdev->dma_ch, buf);
	}

	start_capture(pcdev);
}

static void omap1_videobuf_release(struct videobuf_queue *vq,
				 struct videobuf_buffer *vb)
{
	struct omap1_cam_buf *buf =
			container_of(vb, struct omap1_cam_buf, vb);
	struct soc_camera_device *icd = vq->priv_data;
	struct device *dev = icd->parent;
	struct soc_camera_host *ici = to_soc_camera_host(dev);
	struct omap1_cam_dev *pcdev = ici->priv;

	switch (vb->state) {
	case VIDEOBUF_DONE:
		dev_dbg(dev, "%s (done)\n", __func__);
		break;
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
		dev_dbg(dev, "%s (unknown %d)\n", __func__, vb->state);
		break;
	}

	free_buffer(vq, buf, pcdev->vb_mode);
}

static void videobuf_done(struct omap1_cam_dev *pcdev,
		enum videobuf_state result)
{
	struct omap1_cam_buf *buf = pcdev->active;
	struct videobuf_buffer *vb;
	struct device *dev = pcdev->soc_host.icd->parent;

	if (WARN_ON(!buf)) {
		suspend_capture(pcdev);
		disable_capture(pcdev);
		return;
	}

	if (result == VIDEOBUF_ERROR)
		suspend_capture(pcdev);

	vb = &buf->vb;
	if (waitqueue_active(&vb->done)) {
		if (!pcdev->ready && result != VIDEOBUF_ERROR) {
			/*
			 * No next buffer has been entered into the DMA
			 * programming register set on time (could be done only
			 * while the previous DMA interurpt was processed, not
			 * later), so the last DMA block, be it a whole buffer
			 * if in CONTIG or its last sgbuf if in SG mode, is
			 * about to be reused by the just autoreinitialized DMA
			 * engine, and overwritten with next frame data. Best we
			 * can do is stopping the capture as soon as possible,
			 * hopefully before the next frame start.
			 */
			suspend_capture(pcdev);
		}
		vb->state = result;
		v4l2_get_timestamp(&vb->ts);
		if (result != VIDEOBUF_ERROR)
			vb->field_count++;
		wake_up(&vb->done);

		/* shift in next buffer */
		buf = pcdev->ready;
		pcdev->active = buf;
		pcdev->ready = NULL;

		if (!buf) {
			/*
			 * No next buffer was ready on time (see above), so
			 * indicate error condition to force capture restart or
			 * stop, depending on next buffer already queued or not.
			 */
			result = VIDEOBUF_ERROR;
			prepare_next_vb(pcdev);

			buf = pcdev->ready;
			pcdev->active = buf;
			pcdev->ready = NULL;
		}
	} else if (pcdev->ready) {
		/*
		 * In both CONTIG and SG mode, the DMA engine has possibly
		 * been already autoreinitialized with the preprogrammed
		 * pcdev->ready buffer.  We can either accept this fact
		 * and just swap the buffers, or provoke an error condition
		 * and restart capture.  The former seems less intrusive.
		 */
		dev_dbg(dev, "%s: nobody waiting on videobuf, swap with next\n",
				__func__);
		pcdev->active = pcdev->ready;

		if (pcdev->vb_mode == OMAP1_CAM_DMA_SG) {
			/*
			 * In SG mode, we have to make sure that the buffer we
			 * are putting back into the pcdev->ready is marked
			 * fresh.
			 */
			buf->sgbuf = NULL;
		}
		pcdev->ready = buf;

		buf = pcdev->active;
	} else {
		/*
		 * No next buffer has been entered into
		 * the DMA programming register set on time.
		 */
		if (pcdev->vb_mode == OMAP1_CAM_DMA_CONTIG) {
			/*
			 * In CONTIG mode, the DMA engine has already been
			 * reinitialized with the current buffer. Best we can do
			 * is not touching it.
			 */
			dev_dbg(dev,
				"%s: nobody waiting on videobuf, reuse it\n",
				__func__);
		} else {
			/*
			 * In SG mode, the DMA engine has just been
			 * autoreinitialized with the last sgbuf from the
			 * current list. Restart capture in order to transfer
			 * next frame start into the first sgbuf, not the last
			 * one.
			 */
			if (result != VIDEOBUF_ERROR) {
				suspend_capture(pcdev);
				result = VIDEOBUF_ERROR;
			}
		}
	}

	if (!buf) {
		dev_dbg(dev, "%s: no more videobufs, stop capture\n", __func__);
		disable_capture(pcdev);
		return;
	}

	if (pcdev->vb_mode == OMAP1_CAM_DMA_CONTIG) {
		/*
		 * In CONTIG mode, the current buffer parameters had already
		 * been entered into the DMA programming register set while the
		 * buffer was fetched with prepare_next_vb(), they may have also
		 * been transferred into the runtime set and already active if
		 * the DMA still running.
		 */
	} else {
		/* In SG mode, extra steps are required */
		if (result == VIDEOBUF_ERROR)
			/* make sure we (re)use sglist from start on error */
			buf->sgbuf = NULL;

		/*
		 * In any case, enter the next sgbuf parameters into the DMA
		 * programming register set.  They will be used either during
		 * nearest DMA autoreinitialization or, in case of an error,
		 * on DMA startup below.
		 */
		try_next_sgbuf(pcdev->dma_ch, buf);
	}

	if (result == VIDEOBUF_ERROR) {
		dev_dbg(dev, "%s: videobuf error; reset FIFO, restart DMA\n",
				__func__);
		start_capture(pcdev);
		/*
		 * In SG mode, the above also resulted in the next sgbuf
		 * parameters being entered into the DMA programming register
		 * set, making them ready for next DMA autoreinitialization.
		 */
	}

	/*
	 * Finally, try fetching next buffer.
	 * In CONTIG mode, it will also enter it into the DMA programming
	 * register set, making it ready for next DMA autoreinitialization.
	 */
	prepare_next_vb(pcdev);
}

static void dma_isr(int channel, unsigned short status, void *data)
{
	struct omap1_cam_dev *pcdev = data;
	struct omap1_cam_buf *buf = pcdev->active;
	unsigned long flags;

	spin_lock_irqsave(&pcdev->lock, flags);

	if (WARN_ON(!buf)) {
		suspend_capture(pcdev);
		disable_capture(pcdev);
		goto out;
	}

	if (pcdev->vb_mode == OMAP1_CAM_DMA_CONTIG) {
		/*
		 * In CONTIG mode, assume we have just managed to collect the
		 * whole frame, hopefully before our end of frame watchdog is
		 * triggered. Then, all we have to do is disabling the watchdog
		 * for this frame, and calling videobuf_done() with success
		 * indicated.
		 */
		CAM_WRITE(pcdev, MODE,
				CAM_READ_CACHE(pcdev, MODE) & ~EN_V_DOWN);
		videobuf_done(pcdev, VIDEOBUF_DONE);
	} else {
		/*
		 * In SG mode, we have to process every sgbuf from the current
		 * sglist, one after another.
		 */
		if (buf->sgbuf) {
			/*
			 * Current sglist not completed yet, try fetching next
			 * sgbuf, hopefully putting it into the DMA programming
			 * register set, making it ready for next DMA
			 * autoreinitialization.
			 */
			try_next_sgbuf(pcdev->dma_ch, buf);
			if (buf->sgbuf)
				goto out;

			/*
			 * No more sgbufs left in the current sglist. This
			 * doesn't mean that the whole videobuffer is already
			 * complete, but only that the last sgbuf from the
			 * current sglist is about to be filled. It will be
			 * ready on next DMA interrupt, signalled with the
			 * buf->sgbuf set back to NULL.
			 */
			if (buf->result != VIDEOBUF_ERROR) {
				/*
				 * Video frame collected without errors so far,
				 * we can prepare for collecting a next one
				 * as soon as DMA gets autoreinitialized
				 * after the current (last) sgbuf is completed.
				 */
				buf = prepare_next_vb(pcdev);
				if (!buf)
					goto out;

				try_next_sgbuf(pcdev->dma_ch, buf);
				goto out;
			}
		}
		/* end of videobuf */
		videobuf_done(pcdev, buf->result);
	}

out:
	spin_unlock_irqrestore(&pcdev->lock, flags);
}

static irqreturn_t cam_isr(int irq, void *data)
{
	struct omap1_cam_dev *pcdev = data;
	struct device *dev = pcdev->soc_host.icd->parent;
	struct omap1_cam_buf *buf = pcdev->active;
	u32 it_status;
	unsigned long flags;

	it_status = CAM_READ(pcdev, IT_STATUS);
	if (!it_status)
		return IRQ_NONE;

	spin_lock_irqsave(&pcdev->lock, flags);

	if (WARN_ON(!buf)) {
		dev_warn(dev, "%s: unhandled camera interrupt, status == %#x\n",
			 __func__, it_status);
		suspend_capture(pcdev);
		disable_capture(pcdev);
		goto out;
	}

	if (unlikely(it_status & FIFO_FULL)) {
		dev_warn(dev, "%s: FIFO overflow\n", __func__);

	} else if (it_status & V_DOWN) {
		/* end of video frame watchdog */
		if (pcdev->vb_mode == OMAP1_CAM_DMA_CONTIG) {
			/*
			 * In CONTIG mode, the watchdog is disabled with
			 * successful DMA end of block interrupt, and reenabled
			 * on next frame start. If we get here, there is nothing
			 * to check, we must be out of sync.
			 */
		} else {
			if (buf->sgcount == 2) {
				/*
				 * If exactly 2 sgbufs from the next sglist have
				 * been programmed into the DMA engine (the
				 * first one already transferred into the DMA
				 * runtime register set, the second one still
				 * in the programming set), then we are in sync.
				 */
				goto out;
			}
		}
		dev_notice(dev, "%s: unexpected end of video frame\n",
				__func__);

	} else if (it_status & V_UP) {
		u32 mode;

		if (pcdev->vb_mode == OMAP1_CAM_DMA_CONTIG) {
			/*
			 * In CONTIG mode, we need this interrupt every frame
			 * in oredr to reenable our end of frame watchdog.
			 */
			mode = CAM_READ_CACHE(pcdev, MODE);
		} else {
			/*
			 * In SG mode, the below enabled end of frame watchdog
			 * is kept on permanently, so we can turn this one shot
			 * setup off.
			 */
			mode = CAM_READ_CACHE(pcdev, MODE) & ~EN_V_UP;
		}

		if (!(mode & EN_V_DOWN)) {
			/* (re)enable end of frame watchdog interrupt */
			mode |= EN_V_DOWN;
		}
		CAM_WRITE(pcdev, MODE, mode);
		goto out;

	} else {
		dev_warn(dev, "%s: unhandled camera interrupt, status == %#x\n",
				__func__, it_status);
		goto out;
	}

	videobuf_done(pcdev, VIDEOBUF_ERROR);
out:
	spin_unlock_irqrestore(&pcdev->lock, flags);
	return IRQ_HANDLED;
}

static struct videobuf_queue_ops omap1_videobuf_ops = {
	.buf_setup	= omap1_videobuf_setup,
	.buf_prepare	= omap1_videobuf_prepare,
	.buf_queue	= omap1_videobuf_queue,
	.buf_release	= omap1_videobuf_release,
};


/*
 * SOC Camera host operations
 */

static void sensor_reset(struct omap1_cam_dev *pcdev, bool reset)
{
	/* apply/release camera sensor reset if requested by platform data */
	if (pcdev->pflags & OMAP1_CAMERA_RST_HIGH)
		CAM_WRITE(pcdev, GPIO, reset);
	else if (pcdev->pflags & OMAP1_CAMERA_RST_LOW)
		CAM_WRITE(pcdev, GPIO, !reset);
}

static int omap1_cam_add_device(struct soc_camera_device *icd)
{
	dev_dbg(icd->parent, "OMAP1 Camera driver attached to camera %d\n",
			icd->devnum);

	return 0;
}

static void omap1_cam_remove_device(struct soc_camera_device *icd)
{
	dev_dbg(icd->parent,
		"OMAP1 Camera driver detached from camera %d\n", icd->devnum);
}

/*
 * The following two functions absolutely depend on the fact, that
 * there can be only one camera on OMAP1 camera sensor interface
 */
static int omap1_cam_clock_start(struct soc_camera_host *ici)
{
	struct omap1_cam_dev *pcdev = ici->priv;
	u32 ctrlclock;

	clk_enable(pcdev->clk);

	/* setup sensor clock */
	ctrlclock = CAM_READ(pcdev, CTRLCLOCK);
	ctrlclock &= ~(CAMEXCLK_EN | MCLK_EN | DPLL_EN);
	CAM_WRITE(pcdev, CTRLCLOCK, ctrlclock);

	ctrlclock &= ~FOSCMOD_MASK;
	switch (pcdev->camexclk) {
	case 6000000:
		ctrlclock |= CAMEXCLK_EN | FOSCMOD_6MHz;
		break;
	case 8000000:
		ctrlclock |= CAMEXCLK_EN | FOSCMOD_8MHz | DPLL_EN;
		break;
	case 9600000:
		ctrlclock |= CAMEXCLK_EN | FOSCMOD_9_6MHz | DPLL_EN;
		break;
	case 12000000:
		ctrlclock |= CAMEXCLK_EN | FOSCMOD_12MHz;
		break;
	case 24000000:
		ctrlclock |= CAMEXCLK_EN | FOSCMOD_24MHz | DPLL_EN;
	default:
		break;
	}
	CAM_WRITE(pcdev, CTRLCLOCK, ctrlclock & ~DPLL_EN);

	/* enable internal clock */
	ctrlclock |= MCLK_EN;
	CAM_WRITE(pcdev, CTRLCLOCK, ctrlclock);

	sensor_reset(pcdev, false);

	return 0;
}

static void omap1_cam_clock_stop(struct soc_camera_host *ici)
{
	struct omap1_cam_dev *pcdev = ici->priv;
	u32 ctrlclock;

	suspend_capture(pcdev);
	disable_capture(pcdev);

	sensor_reset(pcdev, true);

	/* disable and release system clocks */
	ctrlclock = CAM_READ_CACHE(pcdev, CTRLCLOCK);
	ctrlclock &= ~(MCLK_EN | DPLL_EN | CAMEXCLK_EN);
	CAM_WRITE(pcdev, CTRLCLOCK, ctrlclock);

	ctrlclock = (ctrlclock & ~FOSCMOD_MASK) | FOSCMOD_12MHz;
	CAM_WRITE(pcdev, CTRLCLOCK, ctrlclock);
	CAM_WRITE(pcdev, CTRLCLOCK, ctrlclock | MCLK_EN);

	CAM_WRITE(pcdev, CTRLCLOCK, ctrlclock & ~MCLK_EN);

	clk_disable(pcdev->clk);
}

/* Duplicate standard formats based on host capability of byte swapping */
static const struct soc_mbus_lookup omap1_cam_formats[] = {
{
	.code = MEDIA_BUS_FMT_UYVY8_2X8,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_YUYV,
		.name			= "YUYV",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_BE,
		.layout			= SOC_MBUS_LAYOUT_PACKED,
	},
}, {
	.code = MEDIA_BUS_FMT_VYUY8_2X8,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_YVYU,
		.name			= "YVYU",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_BE,
		.layout			= SOC_MBUS_LAYOUT_PACKED,
	},
}, {
	.code = MEDIA_BUS_FMT_YUYV8_2X8,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_UYVY,
		.name			= "UYVY",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_BE,
		.layout			= SOC_MBUS_LAYOUT_PACKED,
	},
}, {
	.code = MEDIA_BUS_FMT_YVYU8_2X8,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_VYUY,
		.name			= "VYUY",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_BE,
		.layout			= SOC_MBUS_LAYOUT_PACKED,
	},
}, {
	.code = MEDIA_BUS_FMT_RGB555_2X8_PADHI_BE,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_RGB555,
		.name			= "RGB555",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_BE,
		.layout			= SOC_MBUS_LAYOUT_PACKED,
	},
}, {
	.code = MEDIA_BUS_FMT_RGB555_2X8_PADHI_LE,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_RGB555X,
		.name			= "RGB555X",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_BE,
		.layout			= SOC_MBUS_LAYOUT_PACKED,
	},
}, {
	.code = MEDIA_BUS_FMT_RGB565_2X8_BE,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_RGB565,
		.name			= "RGB565",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_BE,
		.layout			= SOC_MBUS_LAYOUT_PACKED,
	},
}, {
	.code = MEDIA_BUS_FMT_RGB565_2X8_LE,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_RGB565X,
		.name			= "RGB565X",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_BE,
		.layout			= SOC_MBUS_LAYOUT_PACKED,
	},
},
};

static int omap1_cam_get_formats(struct soc_camera_device *icd,
		unsigned int idx, struct soc_camera_format_xlate *xlate)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct device *dev = icd->parent;
	int formats = 0, ret;
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
		dev_warn(dev, "%s: unsupported format code #%d: %d\n", __func__,
				idx, code.code);
		return 0;
	}

	/* Check support for the requested bits-per-sample */
	if (fmt->bits_per_sample != 8)
		return 0;

	switch (code.code) {
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YVYU8_2X8:
	case MEDIA_BUS_FMT_UYVY8_2X8:
	case MEDIA_BUS_FMT_VYUY8_2X8:
	case MEDIA_BUS_FMT_RGB555_2X8_PADHI_BE:
	case MEDIA_BUS_FMT_RGB555_2X8_PADHI_LE:
	case MEDIA_BUS_FMT_RGB565_2X8_BE:
	case MEDIA_BUS_FMT_RGB565_2X8_LE:
		formats++;
		if (xlate) {
			xlate->host_fmt	= soc_mbus_find_fmtdesc(code.code,
						omap1_cam_formats,
						ARRAY_SIZE(omap1_cam_formats));
			xlate->code	= code.code;
			xlate++;
			dev_dbg(dev,
				"%s: providing format %s as byte swapped code #%d\n",
				__func__, xlate->host_fmt->name, code.code);
		}
	default:
		if (xlate)
			dev_dbg(dev,
				"%s: providing format %s in pass-through mode\n",
				__func__, fmt->name);
	}
	formats++;
	if (xlate) {
		xlate->host_fmt	= fmt;
		xlate->code	= code.code;
		xlate++;
	}

	return formats;
}

static bool is_dma_aligned(s32 bytes_per_line, unsigned int height,
		enum omap1_cam_vb_mode vb_mode)
{
	int size = bytes_per_line * height;

	return IS_ALIGNED(bytes_per_line, DMA_ELEMENT_SIZE) &&
		IS_ALIGNED(size, DMA_FRAME_SIZE(vb_mode) * DMA_ELEMENT_SIZE);
}

static int dma_align(int *width, int *height,
		const struct soc_mbus_pixelfmt *fmt,
		enum omap1_cam_vb_mode vb_mode, bool enlarge)
{
	s32 bytes_per_line = soc_mbus_bytes_per_line(*width, fmt);

	if (bytes_per_line < 0)
		return bytes_per_line;

	if (!is_dma_aligned(bytes_per_line, *height, vb_mode)) {
		unsigned int pxalign = __fls(bytes_per_line / *width);
		unsigned int salign  = DMA_FRAME_SHIFT(vb_mode) +
				DMA_ELEMENT_SHIFT - pxalign;
		unsigned int incr    = enlarge << salign;

		v4l_bound_align_image(width, 1, *width + incr, 0,
				height, 1, *height + incr, 0, salign);
		return 0;
	}
	return 1;
}

#define subdev_call_with_sense(pcdev, dev, icd, sd, function, args...)		     \
({										     \
	struct soc_camera_sense sense = {					     \
		.master_clock		= pcdev->camexclk,			     \
		.pixel_clock_max	= 0,					     \
	};									     \
	int __ret;								     \
										     \
	if (pcdev->pdata)							     \
		sense.pixel_clock_max = pcdev->pdata->lclk_khz_max * 1000;	     \
	icd->sense = &sense;							     \
	__ret = v4l2_subdev_call(sd, video, function, ##args);			     \
	icd->sense = NULL;							     \
										     \
	if (sense.flags & SOCAM_SENSE_PCLK_CHANGED) {				     \
		if (sense.pixel_clock > sense.pixel_clock_max) {		     \
			dev_err(dev,						     \
				"%s: pixel clock %lu set by the camera too high!\n", \
				__func__, sense.pixel_clock);			     \
			__ret = -EINVAL;					     \
		}								     \
	}									     \
	__ret;									     \
})

static int set_mbus_format(struct omap1_cam_dev *pcdev, struct device *dev,
		struct soc_camera_device *icd, struct v4l2_subdev *sd,
		struct v4l2_mbus_framefmt *mf,
		const struct soc_camera_format_xlate *xlate)
{
	s32 bytes_per_line;
	int ret = subdev_call_with_sense(pcdev, dev, icd, sd, s_mbus_fmt, mf);

	if (ret < 0) {
		dev_err(dev, "%s: s_mbus_fmt failed\n", __func__);
		return ret;
	}

	if (mf->code != xlate->code) {
		dev_err(dev, "%s: unexpected pixel code change\n", __func__);
		return -EINVAL;
	}

	bytes_per_line = soc_mbus_bytes_per_line(mf->width, xlate->host_fmt);
	if (bytes_per_line < 0) {
		dev_err(dev, "%s: soc_mbus_bytes_per_line() failed\n",
				__func__);
		return bytes_per_line;
	}

	if (!is_dma_aligned(bytes_per_line, mf->height, pcdev->vb_mode)) {
		dev_err(dev, "%s: resulting geometry %ux%u not DMA aligned\n",
				__func__, mf->width, mf->height);
		return -EINVAL;
	}
	return 0;
}

static int omap1_cam_set_crop(struct soc_camera_device *icd,
			       const struct v4l2_crop *crop)
{
	const struct v4l2_rect *rect = &crop->c;
	const struct soc_camera_format_xlate *xlate = icd->current_fmt;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct device *dev = icd->parent;
	struct soc_camera_host *ici = to_soc_camera_host(dev);
	struct omap1_cam_dev *pcdev = ici->priv;
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	struct v4l2_mbus_framefmt *mf = &fmt.format;
	int ret;

	ret = subdev_call_with_sense(pcdev, dev, icd, sd, s_crop, crop);
	if (ret < 0) {
		dev_warn(dev, "%s: failed to crop to %ux%u@%u:%u\n", __func__,
			 rect->width, rect->height, rect->left, rect->top);
		return ret;
	}

	ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &fmt);
	if (ret < 0) {
		dev_warn(dev, "%s: failed to fetch current format\n", __func__);
		return ret;
	}

	ret = dma_align(&mf->width, &mf->height, xlate->host_fmt, pcdev->vb_mode,
			false);
	if (ret < 0) {
		dev_err(dev, "%s: failed to align %ux%u %s with DMA\n",
				__func__, mf->width, mf->height,
				xlate->host_fmt->name);
		return ret;
	}

	if (!ret) {
		/* sensor returned geometry not DMA aligned, trying to fix */
		ret = set_mbus_format(pcdev, dev, icd, sd, mf, xlate);
		if (ret < 0) {
			dev_err(dev, "%s: failed to set format\n", __func__);
			return ret;
		}
	}

	icd->user_width	 = mf->width;
	icd->user_height = mf->height;

	return 0;
}

static int omap1_cam_set_fmt(struct soc_camera_device *icd,
			      struct v4l2_format *f)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_camera_format_xlate *xlate;
	struct device *dev = icd->parent;
	struct soc_camera_host *ici = to_soc_camera_host(dev);
	struct omap1_cam_dev *pcdev = ici->priv;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	int ret;

	xlate = soc_camera_xlate_by_fourcc(icd, pix->pixelformat);
	if (!xlate) {
		dev_warn(dev, "%s: format %#x not found\n", __func__,
				pix->pixelformat);
		return -EINVAL;
	}

	mf.width	= pix->width;
	mf.height	= pix->height;
	mf.field	= pix->field;
	mf.colorspace	= pix->colorspace;
	mf.code		= xlate->code;

	ret = dma_align(&mf.width, &mf.height, xlate->host_fmt, pcdev->vb_mode,
			true);
	if (ret < 0) {
		dev_err(dev, "%s: failed to align %ux%u %s with DMA\n",
				__func__, pix->width, pix->height,
				xlate->host_fmt->name);
		return ret;
	}

	ret = set_mbus_format(pcdev, dev, icd, sd, &mf, xlate);
	if (ret < 0) {
		dev_err(dev, "%s: failed to set format\n", __func__);
		return ret;
	}

	pix->width	 = mf.width;
	pix->height	 = mf.height;
	pix->field	 = mf.field;
	pix->colorspace  = mf.colorspace;
	icd->current_fmt = xlate;

	return 0;
}

static int omap1_cam_try_fmt(struct soc_camera_device *icd,
			      struct v4l2_format *f)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	int ret;
	/* TODO: limit to mx1 hardware capabilities */

	xlate = soc_camera_xlate_by_fourcc(icd, pix->pixelformat);
	if (!xlate) {
		dev_warn(icd->parent, "Format %#x not found\n",
			 pix->pixelformat);
		return -EINVAL;
	}

	mf.width	= pix->width;
	mf.height	= pix->height;
	mf.field	= pix->field;
	mf.colorspace	= pix->colorspace;
	mf.code		= xlate->code;

	/* limit to sensor capabilities */
	ret = v4l2_subdev_call(sd, video, try_mbus_fmt, &mf);
	if (ret < 0)
		return ret;

	pix->width	= mf.width;
	pix->height	= mf.height;
	pix->field	= mf.field;
	pix->colorspace	= mf.colorspace;

	return 0;
}

static bool sg_mode;

/*
 * Local mmap_mapper wrapper,
 * used for detecting videobuf-dma-contig buffer allocation failures
 * and switching to videobuf-dma-sg automatically for future attempts.
 */
static int omap1_cam_mmap_mapper(struct videobuf_queue *q,
				  struct videobuf_buffer *buf,
				  struct vm_area_struct *vma)
{
	struct soc_camera_device *icd = q->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct omap1_cam_dev *pcdev = ici->priv;
	int ret;

	ret = pcdev->mmap_mapper(q, buf, vma);

	if (ret == -ENOMEM)
		sg_mode = true;

	return ret;
}

static void omap1_cam_init_videobuf(struct videobuf_queue *q,
				     struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct omap1_cam_dev *pcdev = ici->priv;

	if (!sg_mode)
		videobuf_queue_dma_contig_init(q, &omap1_videobuf_ops,
				icd->parent, &pcdev->lock,
				V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_FIELD_NONE,
				sizeof(struct omap1_cam_buf), icd, &ici->host_lock);
	else
		videobuf_queue_sg_init(q, &omap1_videobuf_ops,
				icd->parent, &pcdev->lock,
				V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_FIELD_NONE,
				sizeof(struct omap1_cam_buf), icd, &ici->host_lock);

	/* use videobuf mode (auto)selected with the module parameter */
	pcdev->vb_mode = sg_mode ? OMAP1_CAM_DMA_SG : OMAP1_CAM_DMA_CONTIG;

	/*
	 * Ensure we substitute the videobuf-dma-contig version of the
	 * mmap_mapper() callback with our own wrapper, used for switching
	 * automatically to videobuf-dma-sg on buffer allocation failure.
	 */
	if (!sg_mode && q->int_ops->mmap_mapper != omap1_cam_mmap_mapper) {
		pcdev->mmap_mapper = q->int_ops->mmap_mapper;
		q->int_ops->mmap_mapper = omap1_cam_mmap_mapper;
	}
}

static int omap1_cam_reqbufs(struct soc_camera_device *icd,
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
		struct omap1_cam_buf *buf = container_of(icd->vb_vidq.bufs[i],
						      struct omap1_cam_buf, vb);
		buf->inwork = 0;
		INIT_LIST_HEAD(&buf->vb.queue);
	}

	return 0;
}

static int omap1_cam_querycap(struct soc_camera_host *ici,
			       struct v4l2_capability *cap)
{
	/* cap->name is set by the friendly caller:-> */
	strlcpy(cap->card, "OMAP1 Camera", sizeof(cap->card));
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int omap1_cam_set_bus_param(struct soc_camera_device *icd)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct device *dev = icd->parent;
	struct soc_camera_host *ici = to_soc_camera_host(dev);
	struct omap1_cam_dev *pcdev = ici->priv;
	u32 pixfmt = icd->current_fmt->host_fmt->fourcc;
	const struct soc_camera_format_xlate *xlate;
	const struct soc_mbus_pixelfmt *fmt;
	struct v4l2_mbus_config cfg = {.type = V4L2_MBUS_PARALLEL,};
	unsigned long common_flags;
	u32 ctrlclock, mode;
	int ret;

	ret = v4l2_subdev_call(sd, video, g_mbus_config, &cfg);
	if (!ret) {
		common_flags = soc_mbus_config_compatible(&cfg, SOCAM_BUS_FLAGS);
		if (!common_flags) {
			dev_warn(dev,
				 "Flags incompatible: camera 0x%x, host 0x%x\n",
				 cfg.flags, SOCAM_BUS_FLAGS);
			return -EINVAL;
		}
	} else if (ret != -ENOIOCTLCMD) {
		return ret;
	} else {
		common_flags = SOCAM_BUS_FLAGS;
	}

	/* Make choices, possibly based on platform configuration */
	if ((common_flags & V4L2_MBUS_PCLK_SAMPLE_RISING) &&
			(common_flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)) {
		if (!pcdev->pdata ||
				pcdev->pdata->flags & OMAP1_CAMERA_LCLK_RISING)
			common_flags &= ~V4L2_MBUS_PCLK_SAMPLE_FALLING;
		else
			common_flags &= ~V4L2_MBUS_PCLK_SAMPLE_RISING;
	}

	cfg.flags = common_flags;
	ret = v4l2_subdev_call(sd, video, s_mbus_config, &cfg);
	if (ret < 0 && ret != -ENOIOCTLCMD) {
		dev_dbg(dev, "camera s_mbus_config(0x%lx) returned %d\n",
			common_flags, ret);
		return ret;
	}

	ctrlclock = CAM_READ_CACHE(pcdev, CTRLCLOCK);
	if (ctrlclock & LCLK_EN)
		CAM_WRITE(pcdev, CTRLCLOCK, ctrlclock & ~LCLK_EN);

	if (common_flags & V4L2_MBUS_PCLK_SAMPLE_RISING) {
		dev_dbg(dev, "CTRLCLOCK_REG |= POLCLK\n");
		ctrlclock |= POLCLK;
	} else {
		dev_dbg(dev, "CTRLCLOCK_REG &= ~POLCLK\n");
		ctrlclock &= ~POLCLK;
	}
	CAM_WRITE(pcdev, CTRLCLOCK, ctrlclock & ~LCLK_EN);

	if (ctrlclock & LCLK_EN)
		CAM_WRITE(pcdev, CTRLCLOCK, ctrlclock);

	/* select bus endianness */
	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	fmt = xlate->host_fmt;

	mode = CAM_READ(pcdev, MODE) & ~(RAZ_FIFO | IRQ_MASK | DMA);
	if (fmt->order == SOC_MBUS_ORDER_LE) {
		dev_dbg(dev, "MODE_REG &= ~ORDERCAMD\n");
		CAM_WRITE(pcdev, MODE, mode & ~ORDERCAMD);
	} else {
		dev_dbg(dev, "MODE_REG |= ORDERCAMD\n");
		CAM_WRITE(pcdev, MODE, mode | ORDERCAMD);
	}

	return 0;
}

static unsigned int omap1_cam_poll(struct file *file, poll_table *pt)
{
	struct soc_camera_device *icd = file->private_data;
	struct omap1_cam_buf *buf;

	buf = list_entry(icd->vb_vidq.stream.next, struct omap1_cam_buf,
			 vb.stream);

	poll_wait(file, &buf->vb.done, pt);

	if (buf->vb.state == VIDEOBUF_DONE ||
	    buf->vb.state == VIDEOBUF_ERROR)
		return POLLIN | POLLRDNORM;

	return 0;
}

static struct soc_camera_host_ops omap1_host_ops = {
	.owner		= THIS_MODULE,
	.add		= omap1_cam_add_device,
	.remove		= omap1_cam_remove_device,
	.clock_start	= omap1_cam_clock_start,
	.clock_stop	= omap1_cam_clock_stop,
	.get_formats	= omap1_cam_get_formats,
	.set_crop	= omap1_cam_set_crop,
	.set_fmt	= omap1_cam_set_fmt,
	.try_fmt	= omap1_cam_try_fmt,
	.init_videobuf	= omap1_cam_init_videobuf,
	.reqbufs	= omap1_cam_reqbufs,
	.querycap	= omap1_cam_querycap,
	.set_bus_param	= omap1_cam_set_bus_param,
	.poll		= omap1_cam_poll,
};

static int omap1_cam_probe(struct platform_device *pdev)
{
	struct omap1_cam_dev *pcdev;
	struct resource *res;
	struct clk *clk;
	void __iomem *base;
	unsigned int irq;
	int err = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!res || (int)irq <= 0) {
		err = -ENODEV;
		goto exit;
	}

	clk = clk_get(&pdev->dev, "armper_ck");
	if (IS_ERR(clk)) {
		err = PTR_ERR(clk);
		goto exit;
	}

	pcdev = kzalloc(sizeof(*pcdev) + resource_size(res), GFP_KERNEL);
	if (!pcdev) {
		dev_err(&pdev->dev, "Could not allocate pcdev\n");
		err = -ENOMEM;
		goto exit_put_clk;
	}

	pcdev->res = res;
	pcdev->clk = clk;

	pcdev->pdata = pdev->dev.platform_data;
	if (pcdev->pdata) {
		pcdev->pflags = pcdev->pdata->flags;
		pcdev->camexclk = pcdev->pdata->camexclk_khz * 1000;
	}

	switch (pcdev->camexclk) {
	case 6000000:
	case 8000000:
	case 9600000:
	case 12000000:
	case 24000000:
		break;
	default:
		/* pcdev->camexclk != 0 => pcdev->pdata != NULL */
		dev_warn(&pdev->dev,
				"Incorrect sensor clock frequency %ld kHz, "
				"should be one of 0, 6, 8, 9.6, 12 or 24 MHz, "
				"please correct your platform data\n",
				pcdev->pdata->camexclk_khz);
		pcdev->camexclk = 0;
	case 0:
		dev_info(&pdev->dev, "Not providing sensor clock\n");
	}

	INIT_LIST_HEAD(&pcdev->capture);
	spin_lock_init(&pcdev->lock);

	/*
	 * Request the region.
	 */
	if (!request_mem_region(res->start, resource_size(res), DRIVER_NAME)) {
		err = -EBUSY;
		goto exit_kfree;
	}

	base = ioremap(res->start, resource_size(res));
	if (!base) {
		err = -ENOMEM;
		goto exit_release;
	}
	pcdev->irq = irq;
	pcdev->base = base;

	sensor_reset(pcdev, true);

	err = omap_request_dma(OMAP_DMA_CAMERA_IF_RX, DRIVER_NAME,
			dma_isr, (void *)pcdev, &pcdev->dma_ch);
	if (err < 0) {
		dev_err(&pdev->dev, "Can't request DMA for OMAP1 Camera\n");
		err = -EBUSY;
		goto exit_iounmap;
	}
	dev_dbg(&pdev->dev, "got DMA channel %d\n", pcdev->dma_ch);

	/* preconfigure DMA */
	omap_set_dma_src_params(pcdev->dma_ch, OMAP_DMA_PORT_TIPB,
			OMAP_DMA_AMODE_CONSTANT, res->start + REG_CAMDATA,
			0, 0);
	omap_set_dma_dest_burst_mode(pcdev->dma_ch, OMAP_DMA_DATA_BURST_4);
	/* setup DMA autoinitialization */
	omap_dma_link_lch(pcdev->dma_ch, pcdev->dma_ch);

	err = request_irq(pcdev->irq, cam_isr, 0, DRIVER_NAME, pcdev);
	if (err) {
		dev_err(&pdev->dev, "Camera interrupt register failed\n");
		goto exit_free_dma;
	}

	pcdev->soc_host.drv_name	= DRIVER_NAME;
	pcdev->soc_host.ops		= &omap1_host_ops;
	pcdev->soc_host.priv		= pcdev;
	pcdev->soc_host.v4l2_dev.dev	= &pdev->dev;
	pcdev->soc_host.nr		= pdev->id;

	err = soc_camera_host_register(&pcdev->soc_host);
	if (err)
		goto exit_free_irq;

	dev_info(&pdev->dev, "OMAP1 Camera Interface driver loaded\n");

	return 0;

exit_free_irq:
	free_irq(pcdev->irq, pcdev);
exit_free_dma:
	omap_free_dma(pcdev->dma_ch);
exit_iounmap:
	iounmap(base);
exit_release:
	release_mem_region(res->start, resource_size(res));
exit_kfree:
	kfree(pcdev);
exit_put_clk:
	clk_put(clk);
exit:
	return err;
}

static int omap1_cam_remove(struct platform_device *pdev)
{
	struct soc_camera_host *soc_host = to_soc_camera_host(&pdev->dev);
	struct omap1_cam_dev *pcdev = container_of(soc_host,
					struct omap1_cam_dev, soc_host);
	struct resource *res;

	free_irq(pcdev->irq, pcdev);

	omap_free_dma(pcdev->dma_ch);

	soc_camera_host_unregister(soc_host);

	iounmap(pcdev->base);

	res = pcdev->res;
	release_mem_region(res->start, resource_size(res));

	clk_put(pcdev->clk);

	kfree(pcdev);

	dev_info(&pdev->dev, "OMAP1 Camera Interface driver unloaded\n");

	return 0;
}

static struct platform_driver omap1_cam_driver = {
	.driver		= {
		.name	= DRIVER_NAME,
	},
	.probe		= omap1_cam_probe,
	.remove		= omap1_cam_remove,
};

module_platform_driver(omap1_cam_driver);

module_param(sg_mode, bool, 0644);
MODULE_PARM_DESC(sg_mode, "videobuf mode, 0: dma-contig (default), 1: dma-sg");

MODULE_DESCRIPTION("OMAP1 Camera Interface driver");
MODULE_AUTHOR("Janusz Krzysztofik <jkrzyszt@tis.icnet.pl>");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRIVER_VERSION);
MODULE_ALIAS("platform:" DRIVER_NAME);
