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
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "saa7134.h"
#include "saa7134-reg.h"

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>

/* ------------------------------------------------------------------ */

static unsigned int ts_debug;
module_param(ts_debug, int, 0644);
MODULE_PARM_DESC(ts_debug,"enable debug messages [ts]");

#define ts_dbg(fmt, arg...) do { \
	if (ts_debug) \
		printk(KERN_DEBUG pr_fmt("ts: " fmt), ## arg); \
	} while (0)

/* ------------------------------------------------------------------ */
static int buffer_activate(struct saa7134_dev *dev,
			   struct saa7134_buf *buf,
			   struct saa7134_buf *next)
{

	ts_dbg("buffer_activate [%p]", buf);
	buf->top_seen = 0;

	if (!dev->ts_started)
		dev->ts_field = V4L2_FIELD_TOP;

	if (NULL == next)
		next = buf;
	if (V4L2_FIELD_TOP == dev->ts_field) {
		ts_dbg("- [top]     buf=%p next=%p\n", buf, next);
		saa_writel(SAA7134_RS_BA1(5),saa7134_buffer_base(buf));
		saa_writel(SAA7134_RS_BA2(5),saa7134_buffer_base(next));
		dev->ts_field = V4L2_FIELD_BOTTOM;
	} else {
		ts_dbg("- [bottom]  buf=%p next=%p\n", buf, next);
		saa_writel(SAA7134_RS_BA1(5),saa7134_buffer_base(next));
		saa_writel(SAA7134_RS_BA2(5),saa7134_buffer_base(buf));
		dev->ts_field = V4L2_FIELD_TOP;
	}

	/* start DMA */
	saa7134_set_dmabits(dev);

	mod_timer(&dev->ts_q.timeout, jiffies+TS_BUFFER_TIMEOUT);

	if (!dev->ts_started)
		saa7134_ts_start(dev);

	return 0;
}

int saa7134_ts_buffer_init(struct vb2_buffer *vb2)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb2);
	struct saa7134_dmaqueue *dmaq = vb2->vb2_queue->drv_priv;
	struct saa7134_buf *buf = container_of(vbuf, struct saa7134_buf, vb2);

	dmaq->curr = NULL;
	buf->activate = buffer_activate;

	return 0;
}
EXPORT_SYMBOL_GPL(saa7134_ts_buffer_init);

int saa7134_ts_buffer_prepare(struct vb2_buffer *vb2)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb2);
	struct saa7134_dmaqueue *dmaq = vb2->vb2_queue->drv_priv;
	struct saa7134_dev *dev = dmaq->dev;
	struct saa7134_buf *buf = container_of(vbuf, struct saa7134_buf, vb2);
	struct sg_table *dma = vb2_dma_sg_plane_desc(vb2, 0);
	unsigned int lines, llength, size;

	ts_dbg("buffer_prepare [%p]\n", buf);

	llength = TS_PACKET_SIZE;
	lines = dev->ts.nr_packets;

	size = lines * llength;
	if (vb2_plane_size(vb2, 0) < size)
		return -EINVAL;

	vb2_set_plane_payload(vb2, 0, size);
	vbuf->field = dev->field;

	return saa7134_pgtable_build(dev->pci, &dmaq->pt, dma->sgl, dma->nents,
				    saa7134_buffer_startpage(buf));
}
EXPORT_SYMBOL_GPL(saa7134_ts_buffer_prepare);

int saa7134_ts_queue_setup(struct vb2_queue *q,
			   unsigned int *nbuffers, unsigned int *nplanes,
			   unsigned int sizes[], void *alloc_ctxs[])
{
	struct saa7134_dmaqueue *dmaq = q->drv_priv;
	struct saa7134_dev *dev = dmaq->dev;
	int size = TS_PACKET_SIZE * dev->ts.nr_packets;

	if (0 == *nbuffers)
		*nbuffers = dev->ts.nr_bufs;
	*nbuffers = saa7134_buffer_count(size, *nbuffers);
	if (*nbuffers < 3)
		*nbuffers = 3;
	*nplanes = 1;
	sizes[0] = size;
	alloc_ctxs[0] = dev->alloc_ctx;
	return 0;
}
EXPORT_SYMBOL_GPL(saa7134_ts_queue_setup);

int saa7134_ts_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct saa7134_dmaqueue *dmaq = vq->drv_priv;
	struct saa7134_dev *dev = dmaq->dev;

	/*
	 * Planar video capture and TS share the same DMA channel,
	 * so only one can be active at a time.
	 */
	if (vb2_is_busy(&dev->video_vbq) && dev->fmt->planar) {
		struct saa7134_buf *buf, *tmp;

		list_for_each_entry_safe(buf, tmp, &dmaq->queue, entry) {
			list_del(&buf->entry);
			vb2_buffer_done(&buf->vb2.vb2_buf,
					VB2_BUF_STATE_QUEUED);
		}
		if (dmaq->curr) {
			vb2_buffer_done(&dmaq->curr->vb2.vb2_buf,
					VB2_BUF_STATE_QUEUED);
			dmaq->curr = NULL;
		}
		return -EBUSY;
	}
	dmaq->seq_nr = 0;
	return 0;
}
EXPORT_SYMBOL_GPL(saa7134_ts_start_streaming);

void saa7134_ts_stop_streaming(struct vb2_queue *vq)
{
	struct saa7134_dmaqueue *dmaq = vq->drv_priv;
	struct saa7134_dev *dev = dmaq->dev;

	saa7134_ts_stop(dev);
	saa7134_stop_streaming(dev, dmaq);
}
EXPORT_SYMBOL_GPL(saa7134_ts_stop_streaming);

struct vb2_ops saa7134_ts_qops = {
	.queue_setup	= saa7134_ts_queue_setup,
	.buf_init	= saa7134_ts_buffer_init,
	.buf_prepare	= saa7134_ts_buffer_prepare,
	.buf_queue	= saa7134_vb2_buffer_queue,
	.wait_prepare	= vb2_ops_wait_prepare,
	.wait_finish	= vb2_ops_wait_finish,
	.stop_streaming = saa7134_ts_stop_streaming,
};
EXPORT_SYMBOL_GPL(saa7134_ts_qops);

/* ----------------------------------------------------------- */
/* exported stuff                                              */

static unsigned int tsbufs = 8;
module_param(tsbufs, int, 0444);
MODULE_PARM_DESC(tsbufs, "number of ts buffers for read/write IO, range 2-32");

static unsigned int ts_nr_packets = 64;
module_param(ts_nr_packets, int, 0444);
MODULE_PARM_DESC(ts_nr_packets,"size of a ts buffers (in ts packets)");

int saa7134_ts_init_hw(struct saa7134_dev *dev)
{
	/* deactivate TS softreset */
	saa_writeb(SAA7134_TS_SERIAL1, 0x00);
	/* TSSOP high active, TSVAL high active, TSLOCK ignored */
	saa_writeb(SAA7134_TS_PARALLEL, 0x6c);
	saa_writeb(SAA7134_TS_PARALLEL_SERIAL, (TS_PACKET_SIZE-1));
	saa_writeb(SAA7134_TS_DMA0, ((dev->ts.nr_packets-1)&0xff));
	saa_writeb(SAA7134_TS_DMA1, (((dev->ts.nr_packets-1)>>8)&0xff));
	/* TSNOPIT=0, TSCOLAP=0 */
	saa_writeb(SAA7134_TS_DMA2,
		((((dev->ts.nr_packets-1)>>16)&0x3f) | 0x00));

	return 0;
}

int saa7134_ts_init1(struct saa7134_dev *dev)
{
	/* sanitycheck insmod options */
	if (tsbufs < 2)
		tsbufs = 2;
	if (tsbufs > VIDEO_MAX_FRAME)
		tsbufs = VIDEO_MAX_FRAME;
	if (ts_nr_packets < 4)
		ts_nr_packets = 4;
	if (ts_nr_packets > 312)
		ts_nr_packets = 312;
	dev->ts.nr_bufs    = tsbufs;
	dev->ts.nr_packets = ts_nr_packets;

	INIT_LIST_HEAD(&dev->ts_q.queue);
	init_timer(&dev->ts_q.timeout);
	dev->ts_q.timeout.function = saa7134_buffer_timeout;
	dev->ts_q.timeout.data     = (unsigned long)(&dev->ts_q);
	dev->ts_q.dev              = dev;
	dev->ts_q.need_two         = 1;
	dev->ts_started            = 0;
	saa7134_pgtable_alloc(dev->pci, &dev->ts_q.pt);

	/* init TS hw */
	saa7134_ts_init_hw(dev);

	return 0;
}

/* Function for stop TS */
int saa7134_ts_stop(struct saa7134_dev *dev)
{
	ts_dbg("TS stop\n");

	if (!dev->ts_started)
		return 0;

	/* Stop TS stream */
	switch (saa7134_boards[dev->board].ts_type) {
	case SAA7134_MPEG_TS_PARALLEL:
		saa_writeb(SAA7134_TS_PARALLEL, 0x6c);
		dev->ts_started = 0;
		break;
	case SAA7134_MPEG_TS_SERIAL:
		saa_writeb(SAA7134_TS_SERIAL0, 0x40);
		dev->ts_started = 0;
		break;
	}
	return 0;
}

/* Function for start TS */
int saa7134_ts_start(struct saa7134_dev *dev)
{
	ts_dbg("TS start\n");

	if (WARN_ON(dev->ts_started))
		return 0;

	/* dma: setup channel 5 (= TS) */
	saa_writeb(SAA7134_TS_DMA0, (dev->ts.nr_packets - 1) & 0xff);
	saa_writeb(SAA7134_TS_DMA1,
		((dev->ts.nr_packets - 1) >> 8) & 0xff);
	/* TSNOPIT=0, TSCOLAP=0 */
	saa_writeb(SAA7134_TS_DMA2,
		(((dev->ts.nr_packets - 1) >> 16) & 0x3f) | 0x00);
	saa_writel(SAA7134_RS_PITCH(5), TS_PACKET_SIZE);
	saa_writel(SAA7134_RS_CONTROL(5), SAA7134_RS_CONTROL_BURST_16 |
					  SAA7134_RS_CONTROL_ME |
					  (dev->ts_q.pt.dma >> 12));

	/* reset hardware TS buffers */
	saa_writeb(SAA7134_TS_SERIAL1, 0x00);
	saa_writeb(SAA7134_TS_SERIAL1, 0x03);
	saa_writeb(SAA7134_TS_SERIAL1, 0x00);
	saa_writeb(SAA7134_TS_SERIAL1, 0x01);

	/* TS clock non-inverted */
	saa_writeb(SAA7134_TS_SERIAL1, 0x00);

	/* Start TS stream */
	switch (saa7134_boards[dev->board].ts_type) {
	case SAA7134_MPEG_TS_PARALLEL:
		saa_writeb(SAA7134_TS_SERIAL0, 0x40);
		saa_writeb(SAA7134_TS_PARALLEL, 0xec |
			(saa7134_boards[dev->board].ts_force_val << 4));
		break;
	case SAA7134_MPEG_TS_SERIAL:
		saa_writeb(SAA7134_TS_SERIAL0, 0xd8);
		saa_writeb(SAA7134_TS_PARALLEL, 0x6c |
			(saa7134_boards[dev->board].ts_force_val << 4));
		saa_writeb(SAA7134_TS_PARALLEL_SERIAL, 0xbc);
		saa_writeb(SAA7134_TS_SERIAL1, 0x02);
		break;
	}

	dev->ts_started = 1;

	return 0;
}

int saa7134_ts_fini(struct saa7134_dev *dev)
{
	saa7134_pgtable_free(dev->pci, &dev->ts_q.pt);
	return 0;
}

void saa7134_irq_ts_done(struct saa7134_dev *dev, unsigned long status)
{
	enum v4l2_field field;

	spin_lock(&dev->slock);
	if (dev->ts_q.curr) {
		field = dev->ts_field;
		if (field != V4L2_FIELD_TOP) {
			if ((status & 0x100000) != 0x000000)
				goto done;
		} else {
			if ((status & 0x100000) != 0x100000)
				goto done;
		}
		saa7134_buffer_finish(dev, &dev->ts_q, VB2_BUF_STATE_DONE);
	}
	saa7134_buffer_next(dev,&dev->ts_q);

 done:
	spin_unlock(&dev->slock);
}
