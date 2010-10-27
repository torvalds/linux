/*
 * drivers/media/video/omap24xxcam.c
 *
 * OMAP 2 camera block driver.
 *
 * Copyright (C) 2004 MontaVista Software, Inc.
 * Copyright (C) 2004 Texas Instruments.
 * Copyright (C) 2007-2008 Nokia Corporation.
 *
 * Contact: Sakari Ailus <sakari.ailus@nokia.com>
 *
 * Based on code from Andy Lowe <source@mvista.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/videodev2.h>
#include <linux/pci.h>		/* needed for videobufs */
#include <linux/version.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>

#include "omap24xxcam.h"

#define OMAP24XXCAM_VERSION KERNEL_VERSION(0, 0, 0)

#define RESET_TIMEOUT_NS 10000

static void omap24xxcam_reset(struct omap24xxcam_device *cam);
static int omap24xxcam_sensor_if_enable(struct omap24xxcam_device *cam);
static void omap24xxcam_device_unregister(struct v4l2_int_device *s);
static int omap24xxcam_remove(struct platform_device *pdev);

/* module parameters */
static int video_nr = -1;	/* video device minor (-1 ==> auto assign) */
/*
 * Maximum amount of memory to use for capture buffers.
 * Default is 4800KB, enough to double-buffer SXGA.
 */
static int capture_mem = 1280 * 960 * 2 * 2;

static struct v4l2_int_device omap24xxcam;

/*
 *
 * Clocks.
 *
 */

static void omap24xxcam_clock_put(struct omap24xxcam_device *cam)
{
	if (cam->ick != NULL && !IS_ERR(cam->ick))
		clk_put(cam->ick);
	if (cam->fck != NULL && !IS_ERR(cam->fck))
		clk_put(cam->fck);

	cam->ick = cam->fck = NULL;
}

static int omap24xxcam_clock_get(struct omap24xxcam_device *cam)
{
	int rval = 0;

	cam->fck = clk_get(cam->dev, "fck");
	if (IS_ERR(cam->fck)) {
		dev_err(cam->dev, "can't get camera fck");
		rval = PTR_ERR(cam->fck);
		omap24xxcam_clock_put(cam);
		return rval;
	}

	cam->ick = clk_get(cam->dev, "ick");
	if (IS_ERR(cam->ick)) {
		dev_err(cam->dev, "can't get camera ick");
		rval = PTR_ERR(cam->ick);
		omap24xxcam_clock_put(cam);
	}

	return rval;
}

static void omap24xxcam_clock_on(struct omap24xxcam_device *cam)
{
	clk_enable(cam->fck);
	clk_enable(cam->ick);
}

static void omap24xxcam_clock_off(struct omap24xxcam_device *cam)
{
	clk_disable(cam->fck);
	clk_disable(cam->ick);
}

/*
 *
 * Camera core
 *
 */

/*
 * Set xclk.
 *
 * To disable xclk, use value zero.
 */
static void omap24xxcam_core_xclk_set(const struct omap24xxcam_device *cam,
				      u32 xclk)
{
	if (xclk) {
		u32 divisor = CAM_MCLK / xclk;

		if (divisor == 1)
			omap24xxcam_reg_out(cam->mmio_base + CC_REG_OFFSET,
					    CC_CTRL_XCLK,
					    CC_CTRL_XCLK_DIV_BYPASS);
		else
			omap24xxcam_reg_out(cam->mmio_base + CC_REG_OFFSET,
					    CC_CTRL_XCLK, divisor);
	} else
		omap24xxcam_reg_out(cam->mmio_base + CC_REG_OFFSET,
				    CC_CTRL_XCLK, CC_CTRL_XCLK_DIV_STABLE_LOW);
}

static void omap24xxcam_core_hwinit(const struct omap24xxcam_device *cam)
{
	/*
	 * Setting the camera core AUTOIDLE bit causes problems with frame
	 * synchronization, so we will clear the AUTOIDLE bit instead.
	 */
	omap24xxcam_reg_out(cam->mmio_base + CC_REG_OFFSET, CC_SYSCONFIG,
			    CC_SYSCONFIG_AUTOIDLE);

	/* program the camera interface DMA packet size */
	omap24xxcam_reg_out(cam->mmio_base + CC_REG_OFFSET, CC_CTRL_DMA,
			    CC_CTRL_DMA_EN | (DMA_THRESHOLD / 4 - 1));

	/* enable camera core error interrupts */
	omap24xxcam_reg_out(cam->mmio_base + CC_REG_OFFSET, CC_IRQENABLE,
			    CC_IRQENABLE_FW_ERR_IRQ
			    | CC_IRQENABLE_FSC_ERR_IRQ
			    | CC_IRQENABLE_SSC_ERR_IRQ
			    | CC_IRQENABLE_FIFO_OF_IRQ);
}

/*
 * Enable the camera core.
 *
 * Data transfer to the camera DMA starts from next starting frame.
 */
static void omap24xxcam_core_enable(const struct omap24xxcam_device *cam)
{

	omap24xxcam_reg_out(cam->mmio_base + CC_REG_OFFSET, CC_CTRL,
			    cam->cc_ctrl);
}

/*
 * Disable camera core.
 *
 * The data transfer will be stopped immediately (CC_CTRL_CC_RST). The
 * core internal state machines will be reset. Use
 * CC_CTRL_CC_FRAME_TRIG instead if you want to transfer the current
 * frame completely.
 */
static void omap24xxcam_core_disable(const struct omap24xxcam_device *cam)
{
	omap24xxcam_reg_out(cam->mmio_base + CC_REG_OFFSET, CC_CTRL,
			    CC_CTRL_CC_RST);
}

/* Interrupt service routine for camera core interrupts. */
static void omap24xxcam_core_isr(struct omap24xxcam_device *cam)
{
	u32 cc_irqstatus;
	const u32 cc_irqstatus_err =
		CC_IRQSTATUS_FW_ERR_IRQ
		| CC_IRQSTATUS_FSC_ERR_IRQ
		| CC_IRQSTATUS_SSC_ERR_IRQ
		| CC_IRQSTATUS_FIFO_UF_IRQ
		| CC_IRQSTATUS_FIFO_OF_IRQ;

	cc_irqstatus = omap24xxcam_reg_in(cam->mmio_base + CC_REG_OFFSET,
					  CC_IRQSTATUS);
	omap24xxcam_reg_out(cam->mmio_base + CC_REG_OFFSET, CC_IRQSTATUS,
			    cc_irqstatus);

	if (cc_irqstatus & cc_irqstatus_err
	    && !atomic_read(&cam->in_reset)) {
		dev_dbg(cam->dev, "resetting camera, cc_irqstatus 0x%x\n",
			cc_irqstatus);
		omap24xxcam_reset(cam);
	}
}

/*
 *
 * videobuf_buffer handling.
 *
 * Memory for mmapped videobuf_buffers is not allocated
 * conventionally, but by several kmalloc allocations and then
 * creating the scatterlist on our own. User-space buffers are handled
 * normally.
 *
 */

/*
 * Free the memory-mapped buffer memory allocated for a
 * videobuf_buffer and the associated scatterlist.
 */
static void omap24xxcam_vbq_free_mmap_buffer(struct videobuf_buffer *vb)
{
	struct videobuf_dmabuf *dma = videobuf_to_dma(vb);
	size_t alloc_size;
	struct page *page;
	int i;

	if (dma->sglist == NULL)
		return;

	i = dma->sglen;
	while (i) {
		i--;
		alloc_size = sg_dma_len(&dma->sglist[i]);
		page = sg_page(&dma->sglist[i]);
		do {
			ClearPageReserved(page++);
		} while (alloc_size -= PAGE_SIZE);
		__free_pages(sg_page(&dma->sglist[i]),
			     get_order(sg_dma_len(&dma->sglist[i])));
	}

	kfree(dma->sglist);
	dma->sglist = NULL;
}

/* Release all memory related to the videobuf_queue. */
static void omap24xxcam_vbq_free_mmap_buffers(struct videobuf_queue *vbq)
{
	int i;

	mutex_lock(&vbq->vb_lock);

	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (NULL == vbq->bufs[i])
			continue;
		if (V4L2_MEMORY_MMAP != vbq->bufs[i]->memory)
			continue;
		vbq->ops->buf_release(vbq, vbq->bufs[i]);
		omap24xxcam_vbq_free_mmap_buffer(vbq->bufs[i]);
		kfree(vbq->bufs[i]);
		vbq->bufs[i] = NULL;
	}

	mutex_unlock(&vbq->vb_lock);

	videobuf_mmap_free(vbq);
}

/*
 * Allocate physically as contiguous as possible buffer for video
 * frame and allocate and build DMA scatter-gather list for it.
 */
static int omap24xxcam_vbq_alloc_mmap_buffer(struct videobuf_buffer *vb)
{
	unsigned int order;
	size_t alloc_size, size = vb->bsize; /* vb->bsize is page aligned */
	struct page *page;
	int max_pages, err = 0, i = 0;
	struct videobuf_dmabuf *dma = videobuf_to_dma(vb);

	/*
	 * allocate maximum size scatter-gather list. Note this is
	 * overhead. We may not use as many entries as we allocate
	 */
	max_pages = vb->bsize >> PAGE_SHIFT;
	dma->sglist = kcalloc(max_pages, sizeof(*dma->sglist), GFP_KERNEL);
	if (dma->sglist == NULL) {
		err = -ENOMEM;
		goto out;
	}

	while (size) {
		order = get_order(size);
		/*
		 * do not over-allocate even if we would get larger
		 * contiguous chunk that way
		 */
		if ((PAGE_SIZE << order) > size)
			order--;

		/* try to allocate as many contiguous pages as possible */
		page = alloc_pages(GFP_KERNEL | GFP_DMA, order);
		/* if allocation fails, try to allocate smaller amount */
		while (page == NULL) {
			order--;
			page = alloc_pages(GFP_KERNEL | GFP_DMA, order);
			if (page == NULL && !order) {
				err = -ENOMEM;
				goto out;
			}
		}
		size -= (PAGE_SIZE << order);

		/* append allocated chunk of pages into scatter-gather list */
		sg_set_page(&dma->sglist[i], page, PAGE_SIZE << order, 0);
		dma->sglen++;
		i++;

		alloc_size = (PAGE_SIZE << order);

		/* clear pages before giving them to user space */
		memset(page_address(page), 0, alloc_size);

		/* mark allocated pages reserved */
		do {
			SetPageReserved(page++);
		} while (alloc_size -= PAGE_SIZE);
	}
	/*
	 * REVISIT: not fully correct to assign nr_pages == sglen but
	 * video-buf is passing nr_pages for e.g. unmap_sg calls
	 */
	dma->nr_pages = dma->sglen;
	dma->direction = PCI_DMA_FROMDEVICE;

	return 0;

out:
	omap24xxcam_vbq_free_mmap_buffer(vb);
	return err;
}

static int omap24xxcam_vbq_alloc_mmap_buffers(struct videobuf_queue *vbq,
					      unsigned int count)
{
	int i, err = 0;
	struct omap24xxcam_fh *fh =
		container_of(vbq, struct omap24xxcam_fh, vbq);

	mutex_lock(&vbq->vb_lock);

	for (i = 0; i < count; i++) {
		err = omap24xxcam_vbq_alloc_mmap_buffer(vbq->bufs[i]);
		if (err)
			goto out;
		dev_dbg(fh->cam->dev, "sglen is %d for buffer %d\n",
			videobuf_to_dma(vbq->bufs[i])->sglen, i);
	}

	mutex_unlock(&vbq->vb_lock);

	return 0;
out:
	while (i) {
		i--;
		omap24xxcam_vbq_free_mmap_buffer(vbq->bufs[i]);
	}

	mutex_unlock(&vbq->vb_lock);

	return err;
}

/*
 * This routine is called from interrupt context when a scatter-gather DMA
 * transfer of a videobuf_buffer completes.
 */
static void omap24xxcam_vbq_complete(struct omap24xxcam_sgdma *sgdma,
				     u32 csr, void *arg)
{
	struct omap24xxcam_device *cam =
		container_of(sgdma, struct omap24xxcam_device, sgdma);
	struct omap24xxcam_fh *fh = cam->streaming->private_data;
	struct videobuf_buffer *vb = (struct videobuf_buffer *)arg;
	const u32 csr_error = CAMDMA_CSR_MISALIGNED_ERR
		| CAMDMA_CSR_SUPERVISOR_ERR | CAMDMA_CSR_SECURE_ERR
		| CAMDMA_CSR_TRANS_ERR | CAMDMA_CSR_DROP;
	unsigned long flags;

	spin_lock_irqsave(&cam->core_enable_disable_lock, flags);
	if (--cam->sgdma_in_queue == 0)
		omap24xxcam_core_disable(cam);
	spin_unlock_irqrestore(&cam->core_enable_disable_lock, flags);

	do_gettimeofday(&vb->ts);
	vb->field_count = atomic_add_return(2, &fh->field_count);
	if (csr & csr_error) {
		vb->state = VIDEOBUF_ERROR;
		if (!atomic_read(&fh->cam->in_reset)) {
			dev_dbg(cam->dev, "resetting camera, csr 0x%x\n", csr);
			omap24xxcam_reset(cam);
		}
	} else
		vb->state = VIDEOBUF_DONE;
	wake_up(&vb->done);
}

static void omap24xxcam_vbq_release(struct videobuf_queue *vbq,
				    struct videobuf_buffer *vb)
{
	struct videobuf_dmabuf *dma = videobuf_to_dma(vb);

	/* wait for buffer, especially to get out of the sgdma queue */
	videobuf_waiton(vb, 0, 0);
	if (vb->memory == V4L2_MEMORY_MMAP) {
		dma_unmap_sg(vbq->dev, dma->sglist, dma->sglen,
			     dma->direction);
		dma->direction = DMA_NONE;
	} else {
		videobuf_dma_unmap(vbq->dev, videobuf_to_dma(vb));
		videobuf_dma_free(videobuf_to_dma(vb));
	}

	vb->state = VIDEOBUF_NEEDS_INIT;
}

/*
 * Limit the number of available kernel image capture buffers based on the
 * number requested, the currently selected image size, and the maximum
 * amount of memory permitted for kernel capture buffers.
 */
static int omap24xxcam_vbq_setup(struct videobuf_queue *vbq, unsigned int *cnt,
				 unsigned int *size)
{
	struct omap24xxcam_fh *fh = vbq->priv_data;

	if (*cnt <= 0)
		*cnt = VIDEO_MAX_FRAME;	/* supply a default number of buffers */

	if (*cnt > VIDEO_MAX_FRAME)
		*cnt = VIDEO_MAX_FRAME;

	*size = fh->pix.sizeimage;

	/* accessing fh->cam->capture_mem is ok, it's constant */
	if (*size * *cnt > fh->cam->capture_mem)
		*cnt = fh->cam->capture_mem / *size;

	return 0;
}

static int omap24xxcam_dma_iolock(struct videobuf_queue *vbq,
				  struct videobuf_dmabuf *dma)
{
	int err = 0;

	dma->direction = PCI_DMA_FROMDEVICE;
	if (!dma_map_sg(vbq->dev, dma->sglist, dma->sglen, dma->direction)) {
		kfree(dma->sglist);
		dma->sglist = NULL;
		dma->sglen = 0;
		err = -EIO;
	}

	return err;
}

static int omap24xxcam_vbq_prepare(struct videobuf_queue *vbq,
				   struct videobuf_buffer *vb,
				   enum v4l2_field field)
{
	struct omap24xxcam_fh *fh = vbq->priv_data;
	int err = 0;

	/*
	 * Accessing pix here is okay since it's constant while
	 * streaming is on (and we only get called then).
	 */
	if (vb->baddr) {
		/* This is a userspace buffer. */
		if (fh->pix.sizeimage > vb->bsize) {
			/* The buffer isn't big enough. */
			err = -EINVAL;
		} else
			vb->size = fh->pix.sizeimage;
	} else {
		if (vb->state != VIDEOBUF_NEEDS_INIT) {
			/*
			 * We have a kernel bounce buffer that has
			 * already been allocated.
			 */
			if (fh->pix.sizeimage > vb->size) {
				/*
				 * The image size has been changed to
				 * a larger size since this buffer was
				 * allocated, so we need to free and
				 * reallocate it.
				 */
				omap24xxcam_vbq_release(vbq, vb);
				vb->size = fh->pix.sizeimage;
			}
		} else {
			/* We need to allocate a new kernel bounce buffer. */
			vb->size = fh->pix.sizeimage;
		}
	}

	if (err)
		return err;

	vb->width = fh->pix.width;
	vb->height = fh->pix.height;
	vb->field = field;

	if (vb->state == VIDEOBUF_NEEDS_INIT) {
		if (vb->memory == V4L2_MEMORY_MMAP)
			/*
			 * we have built the scatter-gather list by ourself so
			 * do the scatter-gather mapping as well
			 */
			err = omap24xxcam_dma_iolock(vbq, videobuf_to_dma(vb));
		else
			err = videobuf_iolock(vbq, vb, NULL);
	}

	if (!err)
		vb->state = VIDEOBUF_PREPARED;
	else
		omap24xxcam_vbq_release(vbq, vb);

	return err;
}

static void omap24xxcam_vbq_queue(struct videobuf_queue *vbq,
				  struct videobuf_buffer *vb)
{
	struct omap24xxcam_fh *fh = vbq->priv_data;
	struct omap24xxcam_device *cam = fh->cam;
	enum videobuf_state state = vb->state;
	unsigned long flags;
	int err;

	/*
	 * FIXME: We're marking the buffer active since we have no
	 * pretty way of marking it active exactly when the
	 * scatter-gather transfer starts.
	 */
	vb->state = VIDEOBUF_ACTIVE;

	err = omap24xxcam_sgdma_queue(&fh->cam->sgdma,
				      videobuf_to_dma(vb)->sglist,
				      videobuf_to_dma(vb)->sglen, vb->size,
				      omap24xxcam_vbq_complete, vb);

	if (!err) {
		spin_lock_irqsave(&cam->core_enable_disable_lock, flags);
		if (++cam->sgdma_in_queue == 1
		    && !atomic_read(&cam->in_reset))
			omap24xxcam_core_enable(cam);
		spin_unlock_irqrestore(&cam->core_enable_disable_lock, flags);
	} else {
		/*
		 * Oops. We're not supposed to get any errors here.
		 * The only way we could get an error is if we ran out
		 * of scatter-gather DMA slots, but we are supposed to
		 * have at least as many scatter-gather DMA slots as
		 * video buffers so that can't happen.
		 */
		dev_err(cam->dev, "failed to queue a video buffer for dma!\n");
		dev_err(cam->dev, "likely a bug in the driver!\n");
		vb->state = state;
	}
}

static struct videobuf_queue_ops omap24xxcam_vbq_ops = {
	.buf_setup   = omap24xxcam_vbq_setup,
	.buf_prepare = omap24xxcam_vbq_prepare,
	.buf_queue   = omap24xxcam_vbq_queue,
	.buf_release = omap24xxcam_vbq_release,
};

/*
 *
 * OMAP main camera system
 *
 */

/*
 * Reset camera block to power-on state.
 */
static void omap24xxcam_poweron_reset(struct omap24xxcam_device *cam)
{
	int max_loop = RESET_TIMEOUT_NS;

	/* Reset whole camera subsystem */
	omap24xxcam_reg_out(cam->mmio_base,
			    CAM_SYSCONFIG,
			    CAM_SYSCONFIG_SOFTRESET);

	/* Wait till it's finished */
	while (!(omap24xxcam_reg_in(cam->mmio_base, CAM_SYSSTATUS)
		 & CAM_SYSSTATUS_RESETDONE)
	       && --max_loop) {
		ndelay(1);
	}

	if (!(omap24xxcam_reg_in(cam->mmio_base, CAM_SYSSTATUS)
	      & CAM_SYSSTATUS_RESETDONE))
		dev_err(cam->dev, "camera soft reset timeout\n");
}

/*
 * (Re)initialise the camera block.
 */
static void omap24xxcam_hwinit(struct omap24xxcam_device *cam)
{
	omap24xxcam_poweron_reset(cam);

	/* set the camera subsystem autoidle bit */
	omap24xxcam_reg_out(cam->mmio_base, CAM_SYSCONFIG,
			    CAM_SYSCONFIG_AUTOIDLE);

	/* set the camera MMU autoidle bit */
	omap24xxcam_reg_out(cam->mmio_base,
			    CAMMMU_REG_OFFSET + CAMMMU_SYSCONFIG,
			    CAMMMU_SYSCONFIG_AUTOIDLE);

	omap24xxcam_core_hwinit(cam);

	omap24xxcam_dma_hwinit(&cam->sgdma.dma);
}

/*
 * Callback for dma transfer stalling.
 */
static void omap24xxcam_stalled_dma_reset(unsigned long data)
{
	struct omap24xxcam_device *cam = (struct omap24xxcam_device *)data;

	if (!atomic_read(&cam->in_reset)) {
		dev_dbg(cam->dev, "dma stalled, resetting camera\n");
		omap24xxcam_reset(cam);
	}
}

/*
 * Stop capture. Mark we're doing a reset, stop DMA transfers and
 * core. (No new scatter-gather transfers will be queued whilst
 * in_reset is non-zero.)
 *
 * If omap24xxcam_capture_stop is called from several places at
 * once, only the first call will have an effect. Similarly, the last
 * call omap24xxcam_streaming_cont will have effect.
 *
 * Serialisation is ensured by using cam->core_enable_disable_lock.
 */
static void omap24xxcam_capture_stop(struct omap24xxcam_device *cam)
{
	unsigned long flags;

	spin_lock_irqsave(&cam->core_enable_disable_lock, flags);

	if (atomic_inc_return(&cam->in_reset) != 1) {
		spin_unlock_irqrestore(&cam->core_enable_disable_lock, flags);
		return;
	}

	omap24xxcam_core_disable(cam);

	spin_unlock_irqrestore(&cam->core_enable_disable_lock, flags);

	omap24xxcam_sgdma_sync(&cam->sgdma);
}

/*
 * Reset and continue streaming.
 *
 * Note: Resetting the camera FIFO via the CC_RST bit in the CC_CTRL
 * register is supposed to be sufficient to recover from a camera
 * interface error, but it doesn't seem to be enough. If we only do
 * that then subsequent image captures are out of sync by either one
 * or two times DMA_THRESHOLD bytes. Resetting and re-initializing the
 * entire camera subsystem prevents the problem with frame
 * synchronization.
 */
static void omap24xxcam_capture_cont(struct omap24xxcam_device *cam)
{
	unsigned long flags;

	spin_lock_irqsave(&cam->core_enable_disable_lock, flags);

	if (atomic_read(&cam->in_reset) != 1)
		goto out;

	omap24xxcam_hwinit(cam);

	omap24xxcam_sensor_if_enable(cam);

	omap24xxcam_sgdma_process(&cam->sgdma);

	if (cam->sgdma_in_queue)
		omap24xxcam_core_enable(cam);

out:
	atomic_dec(&cam->in_reset);
	spin_unlock_irqrestore(&cam->core_enable_disable_lock, flags);
}

static ssize_t
omap24xxcam_streaming_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct omap24xxcam_device *cam = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", cam->streaming ?  "active" : "inactive");
}
static DEVICE_ATTR(streaming, S_IRUGO, omap24xxcam_streaming_show, NULL);

/*
 * Stop capture and restart it. I.e. reset the camera during use.
 */
static void omap24xxcam_reset(struct omap24xxcam_device *cam)
{
	omap24xxcam_capture_stop(cam);
	omap24xxcam_capture_cont(cam);
}

/*
 * The main interrupt handler.
 */
static irqreturn_t omap24xxcam_isr(int irq, void *arg)
{
	struct omap24xxcam_device *cam = (struct omap24xxcam_device *)arg;
	u32 irqstatus;
	unsigned int irqhandled = 0;

	irqstatus = omap24xxcam_reg_in(cam->mmio_base, CAM_IRQSTATUS);

	if (irqstatus &
	    (CAM_IRQSTATUS_DMA_IRQ2 | CAM_IRQSTATUS_DMA_IRQ1
	     | CAM_IRQSTATUS_DMA_IRQ0)) {
		omap24xxcam_dma_isr(&cam->sgdma.dma);
		irqhandled = 1;
	}
	if (irqstatus & CAM_IRQSTATUS_CC_IRQ) {
		omap24xxcam_core_isr(cam);
		irqhandled = 1;
	}
	if (irqstatus & CAM_IRQSTATUS_MMU_IRQ)
		dev_err(cam->dev, "unhandled camera MMU interrupt!\n");

	return IRQ_RETVAL(irqhandled);
}

/*
 *
 * Sensor handling.
 *
 */

/*
 * Enable the external sensor interface. Try to negotiate interface
 * parameters with the sensor and start using the new ones. The calls
 * to sensor_if_enable and sensor_if_disable need not to be balanced.
 */
static int omap24xxcam_sensor_if_enable(struct omap24xxcam_device *cam)
{
	int rval;
	struct v4l2_ifparm p;

	rval = vidioc_int_g_ifparm(cam->sdev, &p);
	if (rval) {
		dev_err(cam->dev, "vidioc_int_g_ifparm failed with %d\n", rval);
		return rval;
	}

	cam->if_type = p.if_type;

	cam->cc_ctrl = CC_CTRL_CC_EN;

	switch (p.if_type) {
	case V4L2_IF_TYPE_BT656:
		if (p.u.bt656.frame_start_on_rising_vs)
			cam->cc_ctrl |= CC_CTRL_NOBT_SYNCHRO;
		if (p.u.bt656.bt_sync_correct)
			cam->cc_ctrl |= CC_CTRL_BT_CORRECT;
		if (p.u.bt656.swap)
			cam->cc_ctrl |= CC_CTRL_PAR_ORDERCAM;
		if (p.u.bt656.latch_clk_inv)
			cam->cc_ctrl |= CC_CTRL_PAR_CLK_POL;
		if (p.u.bt656.nobt_hs_inv)
			cam->cc_ctrl |= CC_CTRL_NOBT_HS_POL;
		if (p.u.bt656.nobt_vs_inv)
			cam->cc_ctrl |= CC_CTRL_NOBT_VS_POL;

		switch (p.u.bt656.mode) {
		case V4L2_IF_TYPE_BT656_MODE_NOBT_8BIT:
			cam->cc_ctrl |= CC_CTRL_PAR_MODE_NOBT8;
			break;
		case V4L2_IF_TYPE_BT656_MODE_NOBT_10BIT:
			cam->cc_ctrl |= CC_CTRL_PAR_MODE_NOBT10;
			break;
		case V4L2_IF_TYPE_BT656_MODE_NOBT_12BIT:
			cam->cc_ctrl |= CC_CTRL_PAR_MODE_NOBT12;
			break;
		case V4L2_IF_TYPE_BT656_MODE_BT_8BIT:
			cam->cc_ctrl |= CC_CTRL_PAR_MODE_BT8;
			break;
		case V4L2_IF_TYPE_BT656_MODE_BT_10BIT:
			cam->cc_ctrl |= CC_CTRL_PAR_MODE_BT10;
			break;
		default:
			dev_err(cam->dev,
				"bt656 interface mode %d not supported\n",
				p.u.bt656.mode);
			return -EINVAL;
		}
		/*
		 * The clock rate that the sensor wants has changed.
		 * We have to adjust the xclk from OMAP 2 side to
		 * match the sensor's wish as closely as possible.
		 */
		if (p.u.bt656.clock_curr != cam->if_u.bt656.xclk) {
			u32 xclk = p.u.bt656.clock_curr;
			u32 divisor;

			if (xclk == 0)
				return -EINVAL;

			if (xclk > CAM_MCLK)
				xclk = CAM_MCLK;

			divisor = CAM_MCLK / xclk;
			if (divisor * xclk < CAM_MCLK)
				divisor++;
			if (CAM_MCLK / divisor < p.u.bt656.clock_min
			    && divisor > 1)
				divisor--;
			if (divisor > 30)
				divisor = 30;

			xclk = CAM_MCLK / divisor;

			if (xclk < p.u.bt656.clock_min
			    || xclk > p.u.bt656.clock_max)
				return -EINVAL;

			cam->if_u.bt656.xclk = xclk;
		}
		omap24xxcam_core_xclk_set(cam, cam->if_u.bt656.xclk);
		break;
	default:
		/* FIXME: how about other interfaces? */
		dev_err(cam->dev, "interface type %d not supported\n",
			p.if_type);
		return -EINVAL;
	}

	return 0;
}

static void omap24xxcam_sensor_if_disable(const struct omap24xxcam_device *cam)
{
	switch (cam->if_type) {
	case V4L2_IF_TYPE_BT656:
		omap24xxcam_core_xclk_set(cam, 0);
		break;
	}
}

/*
 * Initialise the sensor hardware.
 */
static int omap24xxcam_sensor_init(struct omap24xxcam_device *cam)
{
	int err = 0;
	struct v4l2_int_device *sdev = cam->sdev;

	omap24xxcam_clock_on(cam);
	err = omap24xxcam_sensor_if_enable(cam);
	if (err) {
		dev_err(cam->dev, "sensor interface could not be enabled at "
			"initialisation, %d\n", err);
		cam->sdev = NULL;
		goto out;
	}

	/* power up sensor during sensor initialization */
	vidioc_int_s_power(sdev, 1);

	err = vidioc_int_dev_init(sdev);
	if (err) {
		dev_err(cam->dev, "cannot initialize sensor, error %d\n", err);
		/* Sensor init failed --- it's nonexistent to us! */
		cam->sdev = NULL;
		goto out;
	}

	dev_info(cam->dev, "sensor is %s\n", sdev->name);

out:
	omap24xxcam_sensor_if_disable(cam);
	omap24xxcam_clock_off(cam);

	vidioc_int_s_power(sdev, 0);

	return err;
}

static void omap24xxcam_sensor_exit(struct omap24xxcam_device *cam)
{
	if (cam->sdev)
		vidioc_int_dev_exit(cam->sdev);
}

static void omap24xxcam_sensor_disable(struct omap24xxcam_device *cam)
{
	omap24xxcam_sensor_if_disable(cam);
	omap24xxcam_clock_off(cam);
	vidioc_int_s_power(cam->sdev, 0);
}

/*
 * Power-up and configure camera sensor. It's ready for capturing now.
 */
static int omap24xxcam_sensor_enable(struct omap24xxcam_device *cam)
{
	int rval;

	omap24xxcam_clock_on(cam);

	omap24xxcam_sensor_if_enable(cam);

	rval = vidioc_int_s_power(cam->sdev, 1);
	if (rval)
		goto out;

	rval = vidioc_int_init(cam->sdev);
	if (rval)
		goto out;

	return 0;

out:
	omap24xxcam_sensor_disable(cam);

	return rval;
}

static void omap24xxcam_sensor_reset_work(struct work_struct *work)
{
	struct omap24xxcam_device *cam =
		container_of(work, struct omap24xxcam_device,
			     sensor_reset_work);

	if (atomic_read(&cam->reset_disable))
		return;

	omap24xxcam_capture_stop(cam);

	if (vidioc_int_reset(cam->sdev) == 0) {
		vidioc_int_init(cam->sdev);
	} else {
		/* Can't reset it by vidioc_int_reset. */
		omap24xxcam_sensor_disable(cam);
		omap24xxcam_sensor_enable(cam);
	}

	omap24xxcam_capture_cont(cam);
}

/*
 *
 * IOCTL interface.
 *
 */

static int vidioc_querycap(struct file *file, void *fh,
			   struct v4l2_capability *cap)
{
	struct omap24xxcam_fh *ofh = fh;
	struct omap24xxcam_device *cam = ofh->cam;

	strlcpy(cap->driver, CAM_NAME, sizeof(cap->driver));
	strlcpy(cap->card, cam->vfd->name, sizeof(cap->card));
	cap->version = OMAP24XXCAM_VERSION;
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *fh,
				   struct v4l2_fmtdesc *f)
{
	struct omap24xxcam_fh *ofh = fh;
	struct omap24xxcam_device *cam = ofh->cam;
	int rval;

	rval = vidioc_int_enum_fmt_cap(cam->sdev, f);

	return rval;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct omap24xxcam_fh *ofh = fh;
	struct omap24xxcam_device *cam = ofh->cam;
	int rval;

	mutex_lock(&cam->mutex);
	rval = vidioc_int_g_fmt_cap(cam->sdev, f);
	mutex_unlock(&cam->mutex);

	return rval;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct omap24xxcam_fh *ofh = fh;
	struct omap24xxcam_device *cam = ofh->cam;
	int rval;

	mutex_lock(&cam->mutex);
	if (cam->streaming) {
		rval = -EBUSY;
		goto out;
	}

	rval = vidioc_int_s_fmt_cap(cam->sdev, f);

out:
	mutex_unlock(&cam->mutex);

	if (!rval) {
		mutex_lock(&ofh->vbq.vb_lock);
		ofh->pix = f->fmt.pix;
		mutex_unlock(&ofh->vbq.vb_lock);
	}

	memset(f, 0, sizeof(*f));
	vidioc_g_fmt_vid_cap(file, fh, f);

	return rval;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *fh,
				  struct v4l2_format *f)
{
	struct omap24xxcam_fh *ofh = fh;
	struct omap24xxcam_device *cam = ofh->cam;
	int rval;

	mutex_lock(&cam->mutex);
	rval = vidioc_int_try_fmt_cap(cam->sdev, f);
	mutex_unlock(&cam->mutex);

	return rval;
}

static int vidioc_reqbufs(struct file *file, void *fh,
			  struct v4l2_requestbuffers *b)
{
	struct omap24xxcam_fh *ofh = fh;
	struct omap24xxcam_device *cam = ofh->cam;
	int rval;

	mutex_lock(&cam->mutex);
	if (cam->streaming) {
		mutex_unlock(&cam->mutex);
		return -EBUSY;
	}

	omap24xxcam_vbq_free_mmap_buffers(&ofh->vbq);
	mutex_unlock(&cam->mutex);

	rval = videobuf_reqbufs(&ofh->vbq, b);

	/*
	 * Either videobuf_reqbufs failed or the buffers are not
	 * memory-mapped (which would need special attention).
	 */
	if (rval < 0 || b->memory != V4L2_MEMORY_MMAP)
		goto out;

	rval = omap24xxcam_vbq_alloc_mmap_buffers(&ofh->vbq, rval);
	if (rval)
		omap24xxcam_vbq_free_mmap_buffers(&ofh->vbq);

out:
	return rval;
}

static int vidioc_querybuf(struct file *file, void *fh,
			   struct v4l2_buffer *b)
{
	struct omap24xxcam_fh *ofh = fh;

	return videobuf_querybuf(&ofh->vbq, b);
}

static int vidioc_qbuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct omap24xxcam_fh *ofh = fh;

	return videobuf_qbuf(&ofh->vbq, b);
}

static int vidioc_dqbuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct omap24xxcam_fh *ofh = fh;
	struct omap24xxcam_device *cam = ofh->cam;
	struct videobuf_buffer *vb;
	int rval;

videobuf_dqbuf_again:
	rval = videobuf_dqbuf(&ofh->vbq, b, file->f_flags & O_NONBLOCK);
	if (rval)
		goto out;

	vb = ofh->vbq.bufs[b->index];

	mutex_lock(&cam->mutex);
	/* _needs_reset returns -EIO if reset is required. */
	rval = vidioc_int_g_needs_reset(cam->sdev, (void *)vb->baddr);
	mutex_unlock(&cam->mutex);
	if (rval == -EIO)
		schedule_work(&cam->sensor_reset_work);
	else
		rval = 0;

out:
	/*
	 * This is a hack. We don't want to show -EIO to the user
	 * space. Requeue the buffer and try again if we're not doing
	 * this in non-blocking mode.
	 */
	if (rval == -EIO) {
		videobuf_qbuf(&ofh->vbq, b);
		if (!(file->f_flags & O_NONBLOCK))
			goto videobuf_dqbuf_again;
		/*
		 * We don't have a videobuf_buffer now --- maybe next
		 * time...
		 */
		rval = -EAGAIN;
	}

	return rval;
}

static int vidioc_streamon(struct file *file, void *fh, enum v4l2_buf_type i)
{
	struct omap24xxcam_fh *ofh = fh;
	struct omap24xxcam_device *cam = ofh->cam;
	int rval;

	mutex_lock(&cam->mutex);
	if (cam->streaming) {
		rval = -EBUSY;
		goto out;
	}

	rval = omap24xxcam_sensor_if_enable(cam);
	if (rval) {
		dev_dbg(cam->dev, "vidioc_int_g_ifparm failed\n");
		goto out;
	}

	rval = videobuf_streamon(&ofh->vbq);
	if (!rval) {
		cam->streaming = file;
		sysfs_notify(&cam->dev->kobj, NULL, "streaming");
	}

out:
	mutex_unlock(&cam->mutex);

	return rval;
}

static int vidioc_streamoff(struct file *file, void *fh, enum v4l2_buf_type i)
{
	struct omap24xxcam_fh *ofh = fh;
	struct omap24xxcam_device *cam = ofh->cam;
	struct videobuf_queue *q = &ofh->vbq;
	int rval;

	atomic_inc(&cam->reset_disable);

	flush_scheduled_work();

	rval = videobuf_streamoff(q);
	if (!rval) {
		mutex_lock(&cam->mutex);
		cam->streaming = NULL;
		mutex_unlock(&cam->mutex);
		sysfs_notify(&cam->dev->kobj, NULL, "streaming");
	}

	atomic_dec(&cam->reset_disable);

	return rval;
}

static int vidioc_enum_input(struct file *file, void *fh,
			     struct v4l2_input *inp)
{
	if (inp->index > 0)
		return -EINVAL;

	strlcpy(inp->name, "camera", sizeof(inp->name));
	inp->type = V4L2_INPUT_TYPE_CAMERA;

	return 0;
}

static int vidioc_g_input(struct file *file, void *fh, unsigned int *i)
{
	*i = 0;

	return 0;
}

static int vidioc_s_input(struct file *file, void *fh, unsigned int i)
{
	if (i > 0)
		return -EINVAL;

	return 0;
}

static int vidioc_queryctrl(struct file *file, void *fh,
			    struct v4l2_queryctrl *a)
{
	struct omap24xxcam_fh *ofh = fh;
	struct omap24xxcam_device *cam = ofh->cam;
	int rval;

	rval = vidioc_int_queryctrl(cam->sdev, a);

	return rval;
}

static int vidioc_g_ctrl(struct file *file, void *fh,
			 struct v4l2_control *a)
{
	struct omap24xxcam_fh *ofh = fh;
	struct omap24xxcam_device *cam = ofh->cam;
	int rval;

	mutex_lock(&cam->mutex);
	rval = vidioc_int_g_ctrl(cam->sdev, a);
	mutex_unlock(&cam->mutex);

	return rval;
}

static int vidioc_s_ctrl(struct file *file, void *fh,
			 struct v4l2_control *a)
{
	struct omap24xxcam_fh *ofh = fh;
	struct omap24xxcam_device *cam = ofh->cam;
	int rval;

	mutex_lock(&cam->mutex);
	rval = vidioc_int_s_ctrl(cam->sdev, a);
	mutex_unlock(&cam->mutex);

	return rval;
}

static int vidioc_g_parm(struct file *file, void *fh,
			 struct v4l2_streamparm *a) {
	struct omap24xxcam_fh *ofh = fh;
	struct omap24xxcam_device *cam = ofh->cam;
	int rval;

	mutex_lock(&cam->mutex);
	rval = vidioc_int_g_parm(cam->sdev, a);
	mutex_unlock(&cam->mutex);

	return rval;
}

static int vidioc_s_parm(struct file *file, void *fh,
			 struct v4l2_streamparm *a)
{
	struct omap24xxcam_fh *ofh = fh;
	struct omap24xxcam_device *cam = ofh->cam;
	struct v4l2_streamparm old_streamparm;
	int rval;

	mutex_lock(&cam->mutex);
	if (cam->streaming) {
		rval = -EBUSY;
		goto out;
	}

	old_streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	rval = vidioc_int_g_parm(cam->sdev, &old_streamparm);
	if (rval)
		goto out;

	rval = vidioc_int_s_parm(cam->sdev, a);
	if (rval)
		goto out;

	rval = omap24xxcam_sensor_if_enable(cam);
	/*
	 * Revert to old streaming parameters if enabling sensor
	 * interface with the new ones failed.
	 */
	if (rval)
		vidioc_int_s_parm(cam->sdev, &old_streamparm);

out:
	mutex_unlock(&cam->mutex);

	return rval;
}

/*
 *
 * File operations.
 *
 */

static unsigned int omap24xxcam_poll(struct file *file,
				     struct poll_table_struct *wait)
{
	struct omap24xxcam_fh *fh = file->private_data;
	struct omap24xxcam_device *cam = fh->cam;
	struct videobuf_buffer *vb;

	mutex_lock(&cam->mutex);
	if (cam->streaming != file) {
		mutex_unlock(&cam->mutex);
		return POLLERR;
	}
	mutex_unlock(&cam->mutex);

	mutex_lock(&fh->vbq.vb_lock);
	if (list_empty(&fh->vbq.stream)) {
		mutex_unlock(&fh->vbq.vb_lock);
		return POLLERR;
	}
	vb = list_entry(fh->vbq.stream.next, struct videobuf_buffer, stream);
	mutex_unlock(&fh->vbq.vb_lock);

	poll_wait(file, &vb->done, wait);

	if (vb->state == VIDEOBUF_DONE || vb->state == VIDEOBUF_ERROR)
		return POLLIN | POLLRDNORM;

	return 0;
}

static int omap24xxcam_mmap_buffers(struct file *file,
				    struct vm_area_struct *vma)
{
	struct omap24xxcam_fh *fh = file->private_data;
	struct omap24xxcam_device *cam = fh->cam;
	struct videobuf_queue *vbq = &fh->vbq;
	unsigned int first, last, size, i, j;
	int err = 0;

	mutex_lock(&cam->mutex);
	if (cam->streaming) {
		mutex_unlock(&cam->mutex);
		return -EBUSY;
	}
	mutex_unlock(&cam->mutex);
	mutex_lock(&vbq->vb_lock);

	/* look for first buffer to map */
	for (first = 0; first < VIDEO_MAX_FRAME; first++) {
		if (NULL == vbq->bufs[first])
			continue;
		if (V4L2_MEMORY_MMAP != vbq->bufs[first]->memory)
			continue;
		if (vbq->bufs[first]->boff == (vma->vm_pgoff << PAGE_SHIFT))
			break;
	}

	/* look for last buffer to map */
	for (size = 0, last = first; last < VIDEO_MAX_FRAME; last++) {
		if (NULL == vbq->bufs[last])
			continue;
		if (V4L2_MEMORY_MMAP != vbq->bufs[last]->memory)
			continue;
		size += vbq->bufs[last]->bsize;
		if (size == (vma->vm_end - vma->vm_start))
			break;
	}

	size = 0;
	for (i = first; i <= last && i < VIDEO_MAX_FRAME; i++) {
		struct videobuf_dmabuf *dma = videobuf_to_dma(vbq->bufs[i]);

		for (j = 0; j < dma->sglen; j++) {
			err = remap_pfn_range(
				vma, vma->vm_start + size,
				page_to_pfn(sg_page(&dma->sglist[j])),
				sg_dma_len(&dma->sglist[j]), vma->vm_page_prot);
			if (err)
				goto out;
			size += sg_dma_len(&dma->sglist[j]);
		}
	}

out:
	mutex_unlock(&vbq->vb_lock);

	return err;
}

static int omap24xxcam_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct omap24xxcam_fh *fh = file->private_data;
	int rval;

	/* let the video-buf mapper check arguments and set-up structures */
	rval = videobuf_mmap_mapper(&fh->vbq, vma);
	if (rval)
		return rval;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	/* do mapping to our allocated buffers */
	rval = omap24xxcam_mmap_buffers(file, vma);
	/*
	 * In case of error, free vma->vm_private_data allocated by
	 * videobuf_mmap_mapper.
	 */
	if (rval)
		kfree(vma->vm_private_data);

	return rval;
}

static int omap24xxcam_open(struct file *file)
{
	struct omap24xxcam_device *cam = omap24xxcam.priv;
	struct omap24xxcam_fh *fh;
	struct v4l2_format format;

	if (!cam || !cam->vfd)
		return -ENODEV;

	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (fh == NULL)
		return -ENOMEM;

	mutex_lock(&cam->mutex);
	if (cam->sdev == NULL || !try_module_get(cam->sdev->module)) {
		mutex_unlock(&cam->mutex);
		goto out_try_module_get;
	}

	if (atomic_inc_return(&cam->users) == 1) {
		omap24xxcam_hwinit(cam);
		if (omap24xxcam_sensor_enable(cam)) {
			mutex_unlock(&cam->mutex);
			goto out_omap24xxcam_sensor_enable;
		}
	}
	mutex_unlock(&cam->mutex);

	fh->cam = cam;
	mutex_lock(&cam->mutex);
	vidioc_int_g_fmt_cap(cam->sdev, &format);
	mutex_unlock(&cam->mutex);
	/* FIXME: how about fh->pix when there are more users? */
	fh->pix = format.fmt.pix;

	file->private_data = fh;

	spin_lock_init(&fh->vbq_lock);

	videobuf_queue_sg_init(&fh->vbq, &omap24xxcam_vbq_ops, NULL,
				&fh->vbq_lock, V4L2_BUF_TYPE_VIDEO_CAPTURE,
				V4L2_FIELD_NONE,
				sizeof(struct videobuf_buffer), fh);

	return 0;

out_omap24xxcam_sensor_enable:
	omap24xxcam_poweron_reset(cam);
	module_put(cam->sdev->module);

out_try_module_get:
	kfree(fh);

	return -ENODEV;
}

static int omap24xxcam_release(struct file *file)
{
	struct omap24xxcam_fh *fh = file->private_data;
	struct omap24xxcam_device *cam = fh->cam;

	atomic_inc(&cam->reset_disable);

	flush_scheduled_work();

	/* stop streaming capture */
	videobuf_streamoff(&fh->vbq);

	mutex_lock(&cam->mutex);
	if (cam->streaming == file) {
		cam->streaming = NULL;
		mutex_unlock(&cam->mutex);
		sysfs_notify(&cam->dev->kobj, NULL, "streaming");
	} else {
		mutex_unlock(&cam->mutex);
	}

	atomic_dec(&cam->reset_disable);

	omap24xxcam_vbq_free_mmap_buffers(&fh->vbq);

	/*
	 * Make sure the reset work we might have scheduled is not
	 * pending! It may be run *only* if we have users. (And it may
	 * not be scheduled anymore since streaming is already
	 * disabled.)
	 */
	flush_scheduled_work();

	mutex_lock(&cam->mutex);
	if (atomic_dec_return(&cam->users) == 0) {
		omap24xxcam_sensor_disable(cam);
		omap24xxcam_poweron_reset(cam);
	}
	mutex_unlock(&cam->mutex);

	file->private_data = NULL;

	module_put(cam->sdev->module);
	kfree(fh);

	return 0;
}

static struct v4l2_file_operations omap24xxcam_fops = {
	.ioctl	 = video_ioctl2,
	.poll	 = omap24xxcam_poll,
	.mmap	 = omap24xxcam_mmap,
	.open	 = omap24xxcam_open,
	.release = omap24xxcam_release,
};

/*
 *
 * Power management.
 *
 */

#ifdef CONFIG_PM
static int omap24xxcam_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct omap24xxcam_device *cam = platform_get_drvdata(pdev);

	if (atomic_read(&cam->users) == 0)
		return 0;

	if (!atomic_read(&cam->reset_disable))
		omap24xxcam_capture_stop(cam);

	omap24xxcam_sensor_disable(cam);
	omap24xxcam_poweron_reset(cam);

	return 0;
}

static int omap24xxcam_resume(struct platform_device *pdev)
{
	struct omap24xxcam_device *cam = platform_get_drvdata(pdev);

	if (atomic_read(&cam->users) == 0)
		return 0;

	omap24xxcam_hwinit(cam);
	omap24xxcam_sensor_enable(cam);

	if (!atomic_read(&cam->reset_disable))
		omap24xxcam_capture_cont(cam);

	return 0;
}
#endif /* CONFIG_PM */

static const struct v4l2_ioctl_ops omap24xxcam_ioctl_fops = {
	.vidioc_querycap	= vidioc_querycap,
	.vidioc_enum_fmt_vid_cap	= vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap	= vidioc_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	= vidioc_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap	= vidioc_try_fmt_vid_cap,
	.vidioc_reqbufs		= vidioc_reqbufs,
	.vidioc_querybuf	= vidioc_querybuf,
	.vidioc_qbuf		= vidioc_qbuf,
	.vidioc_dqbuf		= vidioc_dqbuf,
	.vidioc_streamon	= vidioc_streamon,
	.vidioc_streamoff	= vidioc_streamoff,
	.vidioc_enum_input	= vidioc_enum_input,
	.vidioc_g_input		= vidioc_g_input,
	.vidioc_s_input		= vidioc_s_input,
	.vidioc_queryctrl	= vidioc_queryctrl,
	.vidioc_g_ctrl		= vidioc_g_ctrl,
	.vidioc_s_ctrl		= vidioc_s_ctrl,
	.vidioc_g_parm		= vidioc_g_parm,
	.vidioc_s_parm		= vidioc_s_parm,
};

/*
 *
 * Camera device (i.e. /dev/video).
 *
 */

static int omap24xxcam_device_register(struct v4l2_int_device *s)
{
	struct omap24xxcam_device *cam = s->u.slave->master->priv;
	struct video_device *vfd;
	int rval;

	/* We already have a slave. */
	if (cam->sdev)
		return -EBUSY;

	cam->sdev = s;

	if (device_create_file(cam->dev, &dev_attr_streaming) != 0) {
		dev_err(cam->dev, "could not register sysfs entry\n");
		rval = -EBUSY;
		goto err;
	}

	/* initialize the video_device struct */
	vfd = cam->vfd = video_device_alloc();
	if (!vfd) {
		dev_err(cam->dev, "could not allocate video device struct\n");
		rval = -ENOMEM;
		goto err;
	}
	vfd->release = video_device_release;

	vfd->parent = cam->dev;

	strlcpy(vfd->name, CAM_NAME, sizeof(vfd->name));
	vfd->fops		 = &omap24xxcam_fops;
	vfd->ioctl_ops		 = &omap24xxcam_ioctl_fops;

	omap24xxcam_hwinit(cam);

	rval = omap24xxcam_sensor_init(cam);
	if (rval)
		goto err;

	if (video_register_device(vfd, VFL_TYPE_GRABBER, video_nr) < 0) {
		dev_err(cam->dev, "could not register V4L device\n");
		rval = -EBUSY;
		goto err;
	}

	omap24xxcam_poweron_reset(cam);

	dev_info(cam->dev, "registered device %s\n",
		 video_device_node_name(vfd));

	return 0;

err:
	omap24xxcam_device_unregister(s);

	return rval;
}

static void omap24xxcam_device_unregister(struct v4l2_int_device *s)
{
	struct omap24xxcam_device *cam = s->u.slave->master->priv;

	omap24xxcam_sensor_exit(cam);

	if (cam->vfd) {
		if (!video_is_registered(cam->vfd)) {
			/*
			 * The device was never registered, so release the
			 * video_device struct directly.
			 */
			video_device_release(cam->vfd);
		} else {
			/*
			 * The unregister function will release the
			 * video_device struct as well as
			 * unregistering it.
			 */
			video_unregister_device(cam->vfd);
		}
		cam->vfd = NULL;
	}

	device_remove_file(cam->dev, &dev_attr_streaming);

	cam->sdev = NULL;
}

static struct v4l2_int_master omap24xxcam_master = {
	.attach = omap24xxcam_device_register,
	.detach = omap24xxcam_device_unregister,
};

static struct v4l2_int_device omap24xxcam = {
	.module	= THIS_MODULE,
	.name	= CAM_NAME,
	.type	= v4l2_int_type_master,
	.u	= {
		.master = &omap24xxcam_master
	},
};

/*
 *
 * Driver initialisation and deinitialisation.
 *
 */

static int __devinit omap24xxcam_probe(struct platform_device *pdev)
{
	struct omap24xxcam_device *cam;
	struct resource *mem;
	int irq;

	cam = kzalloc(sizeof(*cam), GFP_KERNEL);
	if (!cam) {
		dev_err(&pdev->dev, "could not allocate memory\n");
		goto err;
	}

	platform_set_drvdata(pdev, cam);

	cam->dev = &pdev->dev;

	/*
	 * Impose a lower limit on the amount of memory allocated for
	 * capture. We require at least enough memory to double-buffer
	 * QVGA (300KB).
	 */
	if (capture_mem < 320 * 240 * 2 * 2)
		capture_mem = 320 * 240 * 2 * 2;
	cam->capture_mem = capture_mem;

	/* request the mem region for the camera registers */
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(cam->dev, "no mem resource?\n");
		goto err;
	}
	if (!request_mem_region(mem->start, (mem->end - mem->start) + 1,
				pdev->name)) {
		dev_err(cam->dev,
			"cannot reserve camera register I/O region\n");
		goto err;
	}
	cam->mmio_base_phys = mem->start;
	cam->mmio_size = (mem->end - mem->start) + 1;

	/* map the region */
	cam->mmio_base = (unsigned long)
		ioremap_nocache(cam->mmio_base_phys, cam->mmio_size);
	if (!cam->mmio_base) {
		dev_err(cam->dev, "cannot map camera register I/O region\n");
		goto err;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(cam->dev, "no irq for camera?\n");
		goto err;
	}

	/* install the interrupt service routine */
	if (request_irq(irq, omap24xxcam_isr, 0, CAM_NAME, cam)) {
		dev_err(cam->dev,
			"could not install interrupt service routine\n");
		goto err;
	}
	cam->irq = irq;

	if (omap24xxcam_clock_get(cam))
		goto err;

	INIT_WORK(&cam->sensor_reset_work, omap24xxcam_sensor_reset_work);

	mutex_init(&cam->mutex);
	spin_lock_init(&cam->core_enable_disable_lock);

	omap24xxcam_sgdma_init(&cam->sgdma,
			       cam->mmio_base + CAMDMA_REG_OFFSET,
			       omap24xxcam_stalled_dma_reset,
			       (unsigned long)cam);

	omap24xxcam.priv = cam;

	if (v4l2_int_device_register(&omap24xxcam))
		goto err;

	return 0;

err:
	omap24xxcam_remove(pdev);
	return -ENODEV;
}

static int omap24xxcam_remove(struct platform_device *pdev)
{
	struct omap24xxcam_device *cam = platform_get_drvdata(pdev);

	if (!cam)
		return 0;

	if (omap24xxcam.priv != NULL)
		v4l2_int_device_unregister(&omap24xxcam);
	omap24xxcam.priv = NULL;

	omap24xxcam_clock_put(cam);

	if (cam->irq) {
		free_irq(cam->irq, cam);
		cam->irq = 0;
	}

	if (cam->mmio_base) {
		iounmap((void *)cam->mmio_base);
		cam->mmio_base = 0;
	}

	if (cam->mmio_base_phys) {
		release_mem_region(cam->mmio_base_phys, cam->mmio_size);
		cam->mmio_base_phys = 0;
	}

	kfree(cam);

	return 0;
}

static struct platform_driver omap24xxcam_driver = {
	.probe	 = omap24xxcam_probe,
	.remove	 = omap24xxcam_remove,
#ifdef CONFIG_PM
	.suspend = omap24xxcam_suspend,
	.resume	 = omap24xxcam_resume,
#endif
	.driver	 = {
		.name = CAM_NAME,
		.owner = THIS_MODULE,
	},
};

/*
 *
 * Module initialisation and deinitialisation
 *
 */

static int __init omap24xxcam_init(void)
{
	return platform_driver_register(&omap24xxcam_driver);
}

static void __exit omap24xxcam_cleanup(void)
{
	platform_driver_unregister(&omap24xxcam_driver);
}

MODULE_AUTHOR("Sakari Ailus <sakari.ailus@nokia.com>");
MODULE_DESCRIPTION("OMAP24xx Video for Linux camera driver");
MODULE_LICENSE("GPL");
module_param(video_nr, int, 0);
MODULE_PARM_DESC(video_nr,
		 "Minor number for video device (-1 ==> auto assign)");
module_param(capture_mem, int, 0);
MODULE_PARM_DESC(capture_mem, "Maximum amount of memory for capture "
		 "buffers (default 4800kiB)");

module_init(omap24xxcam_init);
module_exit(omap24xxcam_cleanup);
