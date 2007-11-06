/*
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>

#include "cx88.h"

static unsigned int vbibufs = 4;
module_param(vbibufs,int,0644);
MODULE_PARM_DESC(vbibufs,"number of vbi buffers, range 2-32");

static unsigned int vbi_debug = 0;
module_param(vbi_debug,int,0644);
MODULE_PARM_DESC(vbi_debug,"enable debug messages [vbi]");

#define dprintk(level,fmt, arg...)	if (vbi_debug >= level) \
	printk(KERN_DEBUG "%s: " fmt, dev->core->name , ## arg)

/* ------------------------------------------------------------------ */

int cx8800_vbi_fmt (struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct cx8800_fh  *fh   = priv;
	struct cx8800_dev *dev  = fh->dev;

	f->fmt.vbi.samples_per_line = VBI_LINE_LENGTH;
	f->fmt.vbi.sample_format = V4L2_PIX_FMT_GREY;
	f->fmt.vbi.offset = 244;
	f->fmt.vbi.count[0] = VBI_LINE_COUNT;
	f->fmt.vbi.count[1] = VBI_LINE_COUNT;

	if (dev->core->tvnorm & V4L2_STD_525_60) {
		/* ntsc */
		f->fmt.vbi.sampling_rate = 28636363;
		f->fmt.vbi.start[0] = 10;
		f->fmt.vbi.start[1] = 273;

	} else if (dev->core->tvnorm & V4L2_STD_625_50) {
		/* pal */
		f->fmt.vbi.sampling_rate = 35468950;
		f->fmt.vbi.start[0] = 7 -1;
		f->fmt.vbi.start[1] = 319 -1;
	}
	return 0;
}

static int cx8800_start_vbi_dma(struct cx8800_dev    *dev,
			 struct cx88_dmaqueue *q,
			 struct cx88_buffer   *buf)
{
	struct cx88_core *core = dev->core;

	/* setup fifo + format */
	cx88_sram_channel_setup(dev->core, &cx88_sram_channels[SRAM_CH24],
				buf->vb.width, buf->risc.dma);

	cx_write(MO_VBOS_CONTROL, ( (1 << 18) |  // comb filter delay fixup
				    (1 << 15) |  // enable vbi capture
				    (1 << 11) ));

	/* reset counter */
	cx_write(MO_VBI_GPCNTRL, GP_COUNT_CONTROL_RESET);
	q->count = 1;

	/* enable irqs */
	cx_set(MO_PCI_INTMSK, core->pci_irqmask | PCI_INT_VIDINT);
	cx_set(MO_VID_INTMSK, 0x0f0088);

	/* enable capture */
	cx_set(VID_CAPTURE_CONTROL,0x18);

	/* start dma */
	cx_set(MO_DEV_CNTRL2, (1<<5));
	cx_set(MO_VID_DMACNTRL, 0x88);

	return 0;
}

int cx8800_stop_vbi_dma(struct cx8800_dev *dev)
{
	struct cx88_core *core = dev->core;

	/* stop dma */
	cx_clear(MO_VID_DMACNTRL, 0x88);

	/* disable capture */
	cx_clear(VID_CAPTURE_CONTROL,0x18);

	/* disable irqs */
	cx_clear(MO_PCI_INTMSK, PCI_INT_VIDINT);
	cx_clear(MO_VID_INTMSK, 0x0f0088);
	return 0;
}

int cx8800_restart_vbi_queue(struct cx8800_dev    *dev,
			     struct cx88_dmaqueue *q)
{
	struct cx88_buffer *buf;

	if (list_empty(&q->active))
		return 0;

	buf = list_entry(q->active.next, struct cx88_buffer, vb.queue);
	dprintk(2,"restart_queue [%p/%d]: restart dma\n",
		buf, buf->vb.i);
	cx8800_start_vbi_dma(dev, q, buf);
	list_for_each_entry(buf, &q->active, vb.queue)
		buf->count = q->count++;
	mod_timer(&q->timeout, jiffies+BUFFER_TIMEOUT);
	return 0;
}

void cx8800_vbi_timeout(unsigned long data)
{
	struct cx8800_dev *dev = (struct cx8800_dev*)data;
	struct cx88_core *core = dev->core;
	struct cx88_dmaqueue *q = &dev->vbiq;
	struct cx88_buffer *buf;
	unsigned long flags;

	cx88_sram_channel_dump(dev->core, &cx88_sram_channels[SRAM_CH24]);

	cx_clear(MO_VID_DMACNTRL, 0x88);
	cx_clear(VID_CAPTURE_CONTROL, 0x18);

	spin_lock_irqsave(&dev->slock,flags);
	while (!list_empty(&q->active)) {
		buf = list_entry(q->active.next, struct cx88_buffer, vb.queue);
		list_del(&buf->vb.queue);
		buf->vb.state = VIDEOBUF_ERROR;
		wake_up(&buf->vb.done);
		printk("%s/0: [%p/%d] timeout - dma=0x%08lx\n", dev->core->name,
		       buf, buf->vb.i, (unsigned long)buf->risc.dma);
	}
	cx8800_restart_vbi_queue(dev,q);
	spin_unlock_irqrestore(&dev->slock,flags);
}

/* ------------------------------------------------------------------ */

static int
vbi_setup(struct videobuf_queue *q, unsigned int *count, unsigned int *size)
{
	*size = VBI_LINE_COUNT * VBI_LINE_LENGTH * 2;
	if (0 == *count)
		*count = vbibufs;
	if (*count < 2)
		*count = 2;
	if (*count > 32)
		*count = 32;
	return 0;
}

static int
vbi_prepare(struct videobuf_queue *q, struct videobuf_buffer *vb,
	    enum v4l2_field field)
{
	struct cx8800_fh   *fh  = q->priv_data;
	struct cx8800_dev  *dev = fh->dev;
	struct cx88_buffer *buf = container_of(vb,struct cx88_buffer,vb);
	unsigned int size;
	int rc;

	size = VBI_LINE_COUNT * VBI_LINE_LENGTH * 2;
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < size)
		return -EINVAL;

	if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
		struct videobuf_dmabuf *dma=videobuf_to_dma(&buf->vb);
		buf->vb.width  = VBI_LINE_LENGTH;
		buf->vb.height = VBI_LINE_COUNT;
		buf->vb.size   = size;
		buf->vb.field  = V4L2_FIELD_SEQ_TB;

		if (0 != (rc = videobuf_iolock(q,&buf->vb,NULL)))
			goto fail;
		cx88_risc_buffer(dev->pci, &buf->risc,
				 dma->sglist,
				 0, buf->vb.width * buf->vb.height,
				 buf->vb.width, 0,
				 buf->vb.height);
	}
	buf->vb.state = VIDEOBUF_PREPARED;
	return 0;

 fail:
	cx88_free_buffer(q,buf);
	return rc;
}

static void
vbi_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct cx88_buffer    *buf = container_of(vb,struct cx88_buffer,vb);
	struct cx88_buffer    *prev;
	struct cx8800_fh      *fh   = vq->priv_data;
	struct cx8800_dev     *dev  = fh->dev;
	struct cx88_dmaqueue  *q    = &dev->vbiq;

	/* add jump to stopper */
	buf->risc.jmp[0] = cpu_to_le32(RISC_JUMP | RISC_IRQ1 | RISC_CNT_INC);
	buf->risc.jmp[1] = cpu_to_le32(q->stopper.dma);

	if (list_empty(&q->active)) {
		list_add_tail(&buf->vb.queue,&q->active);
		cx8800_start_vbi_dma(dev, q, buf);
		buf->vb.state = VIDEOBUF_ACTIVE;
		buf->count    = q->count++;
		mod_timer(&q->timeout, jiffies+BUFFER_TIMEOUT);
		dprintk(2,"[%p/%d] vbi_queue - first active\n",
			buf, buf->vb.i);

	} else {
		prev = list_entry(q->active.prev, struct cx88_buffer, vb.queue);
		list_add_tail(&buf->vb.queue,&q->active);
		buf->vb.state = VIDEOBUF_ACTIVE;
		buf->count    = q->count++;
		prev->risc.jmp[1] = cpu_to_le32(buf->risc.dma);
		dprintk(2,"[%p/%d] buffer_queue - append to active\n",
			buf, buf->vb.i);
	}
}

static void vbi_release(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct cx88_buffer *buf = container_of(vb,struct cx88_buffer,vb);

	cx88_free_buffer(q,buf);
}

struct videobuf_queue_ops cx8800_vbi_qops = {
	.buf_setup    = vbi_setup,
	.buf_prepare  = vbi_prepare,
	.buf_queue    = vbi_queue,
	.buf_release  = vbi_release,
};

/* ------------------------------------------------------------------ */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
