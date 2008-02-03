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

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "saa7134-reg.h"
#include "saa7134.h"

/* ------------------------------------------------------------------ */

static unsigned int vbi_debug  = 0;
module_param(vbi_debug, int, 0644);
MODULE_PARM_DESC(vbi_debug,"enable debug messages [vbi]");

static unsigned int vbibufs = 4;
module_param(vbibufs, int, 0444);
MODULE_PARM_DESC(vbibufs,"number of vbi buffers, range 2-32");

#define dprintk(fmt, arg...)	if (vbi_debug) \
	printk(KERN_DEBUG "%s/vbi: " fmt, dev->name , ## arg)

/* ------------------------------------------------------------------ */

#define VBI_LINE_COUNT     16
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

	saa_writeb(SAA7134_VBI_H_LEN1(task), buf->vb.width   & 0xff);
	saa_writeb(SAA7134_VBI_H_LEN2(task), buf->vb.width   >> 8);
	saa_writeb(SAA7134_VBI_V_LEN1(task), buf->vb.height  & 0xff);
	saa_writeb(SAA7134_VBI_V_LEN2(task), buf->vb.height  >> 8);

	saa_andorb(SAA7134_DATA_PATH(task), 0xc0, 0x00);
}

/* ------------------------------------------------------------------ */

static int buffer_activate(struct saa7134_dev *dev,
			   struct saa7134_buf *buf,
			   struct saa7134_buf *next)
{
	unsigned long control,base;

	dprintk("buffer_activate [%p]\n",buf);
	buf->vb.state = VIDEOBUF_ACTIVE;
	buf->top_seen = 0;

	task_init(dev,buf,TASK_A);
	task_init(dev,buf,TASK_B);
	saa_writeb(SAA7134_OFMT_DATA_A, 0x06);
	saa_writeb(SAA7134_OFMT_DATA_B, 0x06);

	/* DMA: setup channel 2+3 (= VBI Task A+B) */
	base    = saa7134_buffer_base(buf);
	control = SAA7134_RS_CONTROL_BURST_16 |
		SAA7134_RS_CONTROL_ME |
		(buf->pt->dma >> 12);
	saa_writel(SAA7134_RS_BA1(2),base);
	saa_writel(SAA7134_RS_BA2(2),base + buf->vb.size/2);
	saa_writel(SAA7134_RS_PITCH(2),buf->vb.width);
	saa_writel(SAA7134_RS_CONTROL(2),control);
	saa_writel(SAA7134_RS_BA1(3),base);
	saa_writel(SAA7134_RS_BA2(3),base + buf->vb.size/2);
	saa_writel(SAA7134_RS_PITCH(3),buf->vb.width);
	saa_writel(SAA7134_RS_CONTROL(3),control);

	/* start DMA */
	saa7134_set_dmabits(dev);
	mod_timer(&dev->vbi_q.timeout, jiffies+BUFFER_TIMEOUT);

	return 0;
}

static int buffer_prepare(struct videobuf_queue *q,
			  struct videobuf_buffer *vb,
			  enum v4l2_field field)
{
	struct saa7134_fh *fh   = q->priv_data;
	struct saa7134_dev *dev = fh->dev;
	struct saa7134_buf *buf = container_of(vb,struct saa7134_buf,vb);
	struct saa7134_tvnorm *norm = dev->tvnorm;
	unsigned int lines, llength, size;
	int err;

	lines   = norm->vbi_v_stop_0 - norm->vbi_v_start_0 +1;
	if (lines > VBI_LINE_COUNT)
		lines = VBI_LINE_COUNT;
	llength = VBI_LINE_LENGTH;
	size = lines * llength * 2;
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < size)
		return -EINVAL;

	if (buf->vb.size != size)
		saa7134_dma_free(q,buf);

	if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
		struct videobuf_dmabuf *dma=videobuf_to_dma(&buf->vb);

		buf->vb.width  = llength;
		buf->vb.height = lines;
		buf->vb.size   = size;
		buf->pt        = &fh->pt_vbi;

		err = videobuf_iolock(q,&buf->vb,NULL);
		if (err)
			goto oops;
		err = saa7134_pgtable_build(dev->pci,buf->pt,
					    dma->sglist,
					    dma->sglen,
					    saa7134_buffer_startpage(buf));
		if (err)
			goto oops;
	}
	buf->vb.state = VIDEOBUF_PREPARED;
	buf->activate = buffer_activate;
	buf->vb.field = field;
	return 0;

 oops:
	saa7134_dma_free(q,buf);
	return err;
}

static int
buffer_setup(struct videobuf_queue *q, unsigned int *count, unsigned int *size)
{
	struct saa7134_fh *fh   = q->priv_data;
	struct saa7134_dev *dev = fh->dev;
	int llength,lines;

	lines   = dev->tvnorm->vbi_v_stop_0 - dev->tvnorm->vbi_v_start_0 +1;
	llength = VBI_LINE_LENGTH;
	*size = lines * llength * 2;
	if (0 == *count)
		*count = vbibufs;
	*count = saa7134_buffer_count(*size,*count);
	return 0;
}

static void buffer_queue(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct saa7134_fh *fh = q->priv_data;
	struct saa7134_dev *dev = fh->dev;
	struct saa7134_buf *buf = container_of(vb,struct saa7134_buf,vb);

	saa7134_buffer_queue(dev,&dev->vbi_q,buf);
}

static void buffer_release(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct saa7134_buf *buf = container_of(vb,struct saa7134_buf,vb);

	saa7134_dma_free(q,buf);
}

struct videobuf_queue_ops saa7134_vbi_qops = {
	.buf_setup    = buffer_setup,
	.buf_prepare  = buffer_prepare,
	.buf_queue    = buffer_queue,
	.buf_release  = buffer_release,
};

/* ------------------------------------------------------------------ */

int saa7134_vbi_init1(struct saa7134_dev *dev)
{
	INIT_LIST_HEAD(&dev->vbi_q.queue);
	init_timer(&dev->vbi_q.timeout);
	dev->vbi_q.timeout.function = saa7134_buffer_timeout;
	dev->vbi_q.timeout.data     = (unsigned long)(&dev->vbi_q);
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
		dev->vbi_fieldcount++;
		/* make sure we have seen both fields */
		if ((status & 0x10) == 0x00) {
			dev->vbi_q.curr->top_seen = 1;
			goto done;
		}
		if (!dev->vbi_q.curr->top_seen)
			goto done;

		dev->vbi_q.curr->vb.field_count = dev->vbi_fieldcount;
		saa7134_buffer_finish(dev,&dev->vbi_q,VIDEOBUF_DONE);
	}
	saa7134_buffer_next(dev,&dev->vbi_q);

 done:
	spin_unlock(&dev->slock);
}

/* ----------------------------------------------------------- */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
