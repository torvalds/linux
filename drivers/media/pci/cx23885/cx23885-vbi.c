// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Driver for the Conexant CX23885 PCIe bridge
 *
 *  Copyright (c) 2007 Steven Toth <stoth@linuxtv.org>
 */

#include "cx23885.h"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

static unsigned int vbibufs = 4;
module_param(vbibufs, int, 0644);
MODULE_PARM_DESC(vbibufs, "number of vbi buffers, range 2-32");

static unsigned int vbi_debug;
module_param(vbi_debug, int, 0644);
MODULE_PARM_DESC(vbi_debug, "enable debug messages [vbi]");

#define dprintk(level, fmt, arg...)\
	do { if (vbi_debug >= level)\
		printk(KERN_DEBUG pr_fmt("%s: vbi:" fmt), \
			__func__, ##arg); \
	} while (0)

/* ------------------------------------------------------------------ */

#define VBI_LINE_LENGTH 1440
#define VBI_NTSC_LINE_COUNT 12
#define VBI_PAL_LINE_COUNT 18


int cx23885_vbi_fmt(struct file *file, void *priv,
	struct v4l2_format *f)
{
	struct cx23885_dev *dev = video_drvdata(file);

	f->fmt.vbi.sampling_rate = 27000000;
	f->fmt.vbi.samples_per_line = VBI_LINE_LENGTH;
	f->fmt.vbi.sample_format = V4L2_PIX_FMT_GREY;
	f->fmt.vbi.offset = 0;
	f->fmt.vbi.flags = 0;
	if (dev->tvnorm & V4L2_STD_525_60) {
		/* ntsc */
		f->fmt.vbi.start[0] = V4L2_VBI_ITU_525_F1_START + 9;
		f->fmt.vbi.start[1] = V4L2_VBI_ITU_525_F2_START + 9;
		f->fmt.vbi.count[0] = VBI_NTSC_LINE_COUNT;
		f->fmt.vbi.count[1] = VBI_NTSC_LINE_COUNT;
	} else if (dev->tvnorm & V4L2_STD_625_50) {
		/* pal */
		f->fmt.vbi.start[0] = V4L2_VBI_ITU_625_F1_START + 5;
		f->fmt.vbi.start[1] = V4L2_VBI_ITU_625_F2_START + 5;
		f->fmt.vbi.count[0] = VBI_PAL_LINE_COUNT;
		f->fmt.vbi.count[1] = VBI_PAL_LINE_COUNT;
	}

	return 0;
}

/* We're given the Video Interrupt status register.
 * The cx23885_video_irq() func has already validated
 * the potential error bits, we just need to
 * deal with vbi payload and return indication if
 * we actually processed any payload.
 */
int cx23885_vbi_irq(struct cx23885_dev *dev, u32 status)
{
	u32 count;
	int handled = 0;

	if (status & VID_BC_MSK_VBI_RISCI1) {
		dprintk(1, "%s() VID_BC_MSK_VBI_RISCI1\n", __func__);
		spin_lock(&dev->slock);
		count = cx_read(VBI_A_GPCNT);
		cx23885_video_wakeup(dev, &dev->vbiq, count);
		spin_unlock(&dev->slock);
		handled++;
	}

	return handled;
}

static int cx23885_start_vbi_dma(struct cx23885_dev    *dev,
			 struct cx23885_dmaqueue *q,
			 struct cx23885_buffer   *buf)
{
	dprintk(1, "%s()\n", __func__);

	/* setup fifo + format */
	cx23885_sram_channel_setup(dev, &dev->sram_channels[SRAM_CH02],
				VBI_LINE_LENGTH, buf->risc.dma);

	/* reset counter */
	cx_write(VID_A_VBI_CTRL, 3);
	cx_write(VBI_A_GPCNT_CTL, 3);
	q->count = 0;

	/* enable irq */
	cx23885_irq_add_enable(dev, 0x01);
	cx_set(VID_A_INT_MSK, 0x000022);

	/* start dma */
	cx_set(DEV_CNTRL2, (1<<5));
	cx_set(VID_A_DMA_CTL, 0x22); /* FIFO and RISC enable */

	return 0;
}

/* ------------------------------------------------------------------ */

static int queue_setup(struct vb2_queue *q,
			   unsigned int *num_buffers, unsigned int *num_planes,
			   unsigned int sizes[], struct device *alloc_devs[])
{
	struct cx23885_dev *dev = q->drv_priv;
	unsigned lines = VBI_PAL_LINE_COUNT;

	if (dev->tvnorm & V4L2_STD_525_60)
		lines = VBI_NTSC_LINE_COUNT;
	*num_planes = 1;
	sizes[0] = lines * VBI_LINE_LENGTH * 2;
	return 0;
}

static int buffer_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct cx23885_dev *dev = vb->vb2_queue->drv_priv;
	struct cx23885_buffer *buf = container_of(vbuf,
		struct cx23885_buffer, vb);
	struct sg_table *sgt = vb2_dma_sg_plane_desc(vb, 0);
	unsigned lines = VBI_PAL_LINE_COUNT;

	if (dev->tvnorm & V4L2_STD_525_60)
		lines = VBI_NTSC_LINE_COUNT;

	if (vb2_plane_size(vb, 0) < lines * VBI_LINE_LENGTH * 2)
		return -EINVAL;
	vb2_set_plane_payload(vb, 0, lines * VBI_LINE_LENGTH * 2);

	cx23885_risc_vbibuffer(dev->pci, &buf->risc,
			 sgt->sgl,
			 0, VBI_LINE_LENGTH * lines,
			 VBI_LINE_LENGTH, 0,
			 lines);
	return 0;
}

static void buffer_finish(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct cx23885_buffer *buf = container_of(vbuf,
		struct cx23885_buffer, vb);

	cx23885_free_buffer(vb->vb2_queue->drv_priv, buf);
}

/*
 * The risc program for each buffer works as follows: it starts with a simple
 * 'JUMP to addr + 12', which is effectively a NOP. Then the code to DMA the
 * buffer follows and at the end we have a JUMP back to the start + 12 (skipping
 * the initial JUMP).
 *
 * This is the risc program of the first buffer to be queued if the active list
 * is empty and it just keeps DMAing this buffer without generating any
 * interrupts.
 *
 * If a new buffer is added then the initial JUMP in the code for that buffer
 * will generate an interrupt which signals that the previous buffer has been
 * DMAed successfully and that it can be returned to userspace.
 *
 * It also sets the final jump of the previous buffer to the start of the new
 * buffer, thus chaining the new buffer into the DMA chain. This is a single
 * atomic u32 write, so there is no race condition.
 *
 * The end-result of all this that you only get an interrupt when a buffer
 * is ready, so the control flow is very easy.
 */
static void buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct cx23885_dev *dev = vb->vb2_queue->drv_priv;
	struct cx23885_buffer *buf = container_of(vbuf,
			struct cx23885_buffer, vb);
	struct cx23885_buffer *prev;
	struct cx23885_dmaqueue *q = &dev->vbiq;
	unsigned long flags;

	buf->risc.cpu[1] = cpu_to_le32(buf->risc.dma + 12);
	buf->risc.jmp[0] = cpu_to_le32(RISC_JUMP | RISC_CNT_INC);
	buf->risc.jmp[1] = cpu_to_le32(buf->risc.dma + 12);
	buf->risc.jmp[2] = cpu_to_le32(0); /* bits 63-32 */

	if (list_empty(&q->active)) {
		spin_lock_irqsave(&dev->slock, flags);
		list_add_tail(&buf->queue, &q->active);
		spin_unlock_irqrestore(&dev->slock, flags);
		dprintk(2, "[%p/%d] vbi_queue - first active\n",
			buf, buf->vb.vb2_buf.index);

	} else {
		buf->risc.cpu[0] |= cpu_to_le32(RISC_IRQ1);
		prev = list_entry(q->active.prev, struct cx23885_buffer,
			queue);
		spin_lock_irqsave(&dev->slock, flags);
		list_add_tail(&buf->queue, &q->active);
		spin_unlock_irqrestore(&dev->slock, flags);
		prev->risc.jmp[1] = cpu_to_le32(buf->risc.dma);
		dprintk(2, "[%p/%d] buffer_queue - append to active\n",
			buf, buf->vb.vb2_buf.index);
	}
}

static int cx23885_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct cx23885_dev *dev = q->drv_priv;
	struct cx23885_dmaqueue *dmaq = &dev->vbiq;
	struct cx23885_buffer *buf = list_entry(dmaq->active.next,
			struct cx23885_buffer, queue);

	cx23885_start_vbi_dma(dev, dmaq, buf);
	return 0;
}

static void cx23885_stop_streaming(struct vb2_queue *q)
{
	struct cx23885_dev *dev = q->drv_priv;
	struct cx23885_dmaqueue *dmaq = &dev->vbiq;
	unsigned long flags;

	cx_clear(VID_A_DMA_CTL, 0x22); /* FIFO and RISC enable */
	spin_lock_irqsave(&dev->slock, flags);
	while (!list_empty(&dmaq->active)) {
		struct cx23885_buffer *buf = list_entry(dmaq->active.next,
			struct cx23885_buffer, queue);

		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&dev->slock, flags);
}


const struct vb2_ops cx23885_vbi_qops = {
	.queue_setup    = queue_setup,
	.buf_prepare  = buffer_prepare,
	.buf_finish = buffer_finish,
	.buf_queue    = buffer_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.start_streaming = cx23885_start_streaming,
	.stop_streaming = cx23885_stop_streaming,
};
