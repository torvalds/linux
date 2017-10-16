/*
 *
 * device driver for philips saa7134 based TV cards
 * video4linux video interface
 *
 * (c) 2001,02 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include "saa7134.h"
#include "saa7134-reg.h"

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>

/* ------------------------------------------------------------------ */

static unsigned int vbi_debug;
module_param(vbi_debug, int, 0644);
MODULE_PARM_DESC(vbi_debug,"enable debug messages [vbi]");

static unsigned int vbibufs = 4;
module_param(vbibufs, int, 0444);
MODULE_PARM_DESC(vbibufs,"number of vbi buffers, range 2-32");

#define vbi_dbg(fmt, arg...) do { \
	if (vbi_debug) \
		printk(KERN_DEBUG pr_fmt("vbi: " fmt), ## arg); \
	} while (0)

/* ------------------------------------------------------------------ */

#define VBI_LINE_COUNT     17
#define VBI_LINE_LENGTH  2048
#define VBI_SCALE       0x200

static void task_init(struct saa7134_dev *dev, struct saa7134_buf *buf,
		      int task)
{
	struct saa7134_tvnorm *norm = dev->tvnorm;

	/* setup video scaler */
	saa_writeb(SAA7134_VBI_H_START1(task), norm->h_start     &  0xff);
	saa_writeb(SAA7134_VBI_H_START2(task), norm->h_start     >> 8);
	saa_writeb(SAA7134_VBI_H_STOP1(task),  norm->h_stop      &  0xff);
	saa_writeb(SAA7134_VBI_H_STOP2(task),  norm->h_stop      >> 8);
	saa_writeb(SAA7134_VBI_V_START1(task), norm->vbi_v_start_0 &  0xff);
	saa_writeb(SAA7134_VBI_V_START2(task), norm->vbi_v_start_0 >> 8);
	saa_writeb(SAA7134_VBI_V_STOP1(task),  norm->vbi_v_stop_0  &  0xff);
	saa_writeb(SAA7134_VBI_V_STOP2(task),  norm->vbi_v_stop_0  >> 8);

	saa_writeb(SAA7134_VBI_H_SCALE_INC1(task),        VBI_SCALE & 0xff);
	saa_writeb(SAA7134_VBI_H_SCALE_INC2(task),        VBI_SCALE >> 8);
	saa_writeb(SAA7134_VBI_PHASE_OFFSET_LUMA(task),   0x00);
	saa_writeb(SAA7134_VBI_PHASE_OFFSET_CHROMA(task), 0x00);

	saa_writeb(SAA7134_VBI_H_LEN1(task), dev->vbi_hlen & 0xff);
	saa_writeb(SAA7134_VBI_H_LEN2(task), dev->vbi_hlen >> 8);
	saa_writeb(SAA7134_VBI_V_LEN1(task), dev->vbi_vlen & 0xff);
	saa_writeb(SAA7134_VBI_V_LEN2(task), dev->vbi_vlen >> 8);

	saa_andorb(SAA7134_DATA_PATH(task), 0xc0, 0x00);
}

/* ------------------------------------------------------------------ */

static int buffer_activate(struct saa7134_dev *dev,
			   struct saa7134_buf *buf,
			   struct saa7134_buf *next)
{
	struct saa7134_dmaqueue *dmaq = buf->vb2.vb2_buf.vb2_queue->drv_priv;
	unsigned long control, base;

	vbi_dbg("buffer_activate [%p]\n", buf);
	buf->top_seen = 0;

	task_init(dev, buf, TASK_A);
	task_init(dev, buf, TASK_B);
	saa_writeb(SAA7134_OFMT_DATA_A, 0x06);
	saa_writeb(SAA7134_OFMT_DATA_B, 0x06);

	/* DMA: setup channel 2+3 (= VBI Task A+B) */
	base    = saa7134_buffer_base(buf);
	control = SAA7134_RS_CONTROL_BURST_16 |
		SAA7134_RS_CONTROL_ME |
		(dmaq->pt.dma >> 12);
	saa_writel(SAA7134_RS_BA1(2), base);
	saa_writel(SAA7134_RS_BA2(2), base + dev->vbi_hlen * dev->vbi_vlen);
	saa_writel(SAA7134_RS_PITCH(2), dev->vbi_hlen);
	saa_writel(SAA7134_RS_CONTROL(2), control);
	saa_writel(SAA7134_RS_BA1(3), base);
	saa_writel(SAA7134_RS_BA2(3), base + dev->vbi_hlen * dev->vbi_vlen);
	saa_writel(SAA7134_RS_PITCH(3), dev->vbi_hlen);
	saa_writel(SAA7134_RS_CONTROL(3), control);

	/* start DMA */
	saa7134_set_dmabits(dev);
	mod_timer(&dmaq->timeout, jiffies + BUFFER_TIMEOUT);

	return 0;
}

static int buffer_prepare(struct vb2_buffer *vb2)
{
	struct saa7134_dmaqueue *dmaq = vb2->vb2_queue->drv_priv;
	struct saa7134_dev *dev = dmaq->dev;
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb2);
	struct saa7134_buf *buf = container_of(vbuf, struct saa7134_buf, vb2);
	struct sg_table *dma = vb2_dma_sg_plane_desc(vb2, 0);
	unsigned int size;

	if (dma->sgl->offset) {
		pr_err("The buffer is not page-aligned\n");
		return -EINVAL;
	}
	size = dev->vbi_hlen * dev->vbi_vlen * 2;
	if (vb2_plane_size(vb2, 0) < size)
		return -EINVAL;

	vb2_set_plane_payload(vb2, 0, size);

	return saa7134_pgtable_build(dev->pci, &dmaq->pt, dma->sgl, dma->nents,
				    saa7134_buffer_startpage(buf));
}

static int queue_setup(struct vb2_queue *q,
			   unsigned int *nbuffers, unsigned int *nplanes,
			   unsigned int sizes[], struct device *alloc_devs[])
{
	struct saa7134_dmaqueue *dmaq = q->drv_priv;
	struct saa7134_dev *dev = dmaq->dev;
	unsigned int size;

	dev->vbi_vlen = dev->tvnorm->vbi_v_stop_0 - dev->tvnorm->vbi_v_start_0 + 1;
	if (dev->vbi_vlen > VBI_LINE_COUNT)
		dev->vbi_vlen = VBI_LINE_COUNT;
	dev->vbi_hlen = VBI_LINE_LENGTH;
	size = dev->vbi_hlen * dev->vbi_vlen * 2;

	*nbuffers = saa7134_buffer_count(size, *nbuffers);
	*nplanes = 1;
	sizes[0] = size;
	return 0;
}

static int buffer_init(struct vb2_buffer *vb2)
{
	struct saa7134_dmaqueue *dmaq = vb2->vb2_queue->drv_priv;
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb2);
	struct saa7134_buf *buf = container_of(vbuf, struct saa7134_buf, vb2);

	dmaq->curr = NULL;
	buf->activate = buffer_activate;
	return 0;
}

const struct vb2_ops saa7134_vbi_qops = {
	.queue_setup	= queue_setup,
	.buf_init	= buffer_init,
	.buf_prepare	= buffer_prepare,
	.buf_queue	= saa7134_vb2_buffer_queue,
	.wait_prepare	= vb2_ops_wait_prepare,
	.wait_finish	= vb2_ops_wait_finish,
	.start_streaming = saa7134_vb2_start_streaming,
	.stop_streaming = saa7134_vb2_stop_streaming,
};

/* ------------------------------------------------------------------ */

int saa7134_vbi_init1(struct saa7134_dev *dev)
{
	INIT_LIST_HEAD(&dev->vbi_q.queue);
	timer_setup(&dev->vbi_q.timeout, saa7134_buffer_timeout, 0);
	dev->vbi_q.dev              = dev;

	if (vbibufs < 2)
		vbibufs = 2;
	if (vbibufs > VIDEO_MAX_FRAME)
		vbibufs = VIDEO_MAX_FRAME;
	return 0;
}

int saa7134_vbi_fini(struct saa7134_dev *dev)
{
	/* nothing */
	return 0;
}

void saa7134_irq_vbi_done(struct saa7134_dev *dev, unsigned long status)
{
	spin_lock(&dev->slock);
	if (dev->vbi_q.curr) {
		/* make sure we have seen both fields */
		if ((status & 0x10) == 0x00) {
			dev->vbi_q.curr->top_seen = 1;
			goto done;
		}
		if (!dev->vbi_q.curr->top_seen)
			goto done;

		saa7134_buffer_finish(dev, &dev->vbi_q, VB2_BUF_STATE_DONE);
	}
	saa7134_buffer_next(dev, &dev->vbi_q);

 done:
	spin_unlock(&dev->slock);
}
