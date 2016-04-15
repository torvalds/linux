/*
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include "cx88.h"

static unsigned int vbi_debug;
module_param(vbi_debug,int,0644);
MODULE_PARM_DESC(vbi_debug,"enable debug messages [vbi]");

#define dprintk(level,fmt, arg...)	if (vbi_debug >= level) \
	printk(KERN_DEBUG "%s: " fmt, dev->core->name , ## arg)

/* ------------------------------------------------------------------ */

int cx8800_vbi_fmt (struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct cx8800_dev *dev = video_drvdata(file);

	f->fmt.vbi.samples_per_line = VBI_LINE_LENGTH;
	f->fmt.vbi.sample_format = V4L2_PIX_FMT_GREY;
	f->fmt.vbi.offset = 244;

	if (dev->core->tvnorm & V4L2_STD_525_60) {
		/* ntsc */
		f->fmt.vbi.sampling_rate = 28636363;
		f->fmt.vbi.start[0] = 10;
		f->fmt.vbi.start[1] = 273;
		f->fmt.vbi.count[0] = VBI_LINE_NTSC_COUNT;
		f->fmt.vbi.count[1] = VBI_LINE_NTSC_COUNT;

	} else if (dev->core->tvnorm & V4L2_STD_625_50) {
		/* pal */
		f->fmt.vbi.sampling_rate = 35468950;
		f->fmt.vbi.start[0] = V4L2_VBI_ITU_625_F1_START + 5;
		f->fmt.vbi.start[1] = V4L2_VBI_ITU_625_F2_START + 5;
		f->fmt.vbi.count[0] = VBI_LINE_PAL_COUNT;
		f->fmt.vbi.count[1] = VBI_LINE_PAL_COUNT;
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
				VBI_LINE_LENGTH, buf->risc.dma);

	cx_write(MO_VBOS_CONTROL, ( (1 << 18) |  // comb filter delay fixup
				    (1 << 15) |  // enable vbi capture
				    (1 << 11) ));

	/* reset counter */
	cx_write(MO_VBI_GPCNTRL, GP_COUNT_CONTROL_RESET);
	q->count = 0;

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

void cx8800_stop_vbi_dma(struct cx8800_dev *dev)
{
	struct cx88_core *core = dev->core;

	/* stop dma */
	cx_clear(MO_VID_DMACNTRL, 0x88);

	/* disable capture */
	cx_clear(VID_CAPTURE_CONTROL,0x18);

	/* disable irqs */
	cx_clear(MO_PCI_INTMSK, PCI_INT_VIDINT);
	cx_clear(MO_VID_INTMSK, 0x0f0088);
}

int cx8800_restart_vbi_queue(struct cx8800_dev    *dev,
			     struct cx88_dmaqueue *q)
{
	struct cx88_buffer *buf;

	if (list_empty(&q->active))
		return 0;

	buf = list_entry(q->active.next, struct cx88_buffer, list);
	dprintk(2,"restart_queue [%p/%d]: restart dma\n",
		buf, buf->vb.vb2_buf.index);
	cx8800_start_vbi_dma(dev, q, buf);
	return 0;
}

/* ------------------------------------------------------------------ */

static int queue_setup(struct vb2_queue *q,
			   unsigned int *num_buffers, unsigned int *num_planes,
			   unsigned int sizes[], struct device *alloc_devs[])
{
	struct cx8800_dev *dev = q->drv_priv;

	*num_planes = 1;
	if (dev->core->tvnorm & V4L2_STD_525_60)
		sizes[0] = VBI_LINE_NTSC_COUNT * VBI_LINE_LENGTH * 2;
	else
		sizes[0] = VBI_LINE_PAL_COUNT * VBI_LINE_LENGTH * 2;
	return 0;
}


static int buffer_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct cx8800_dev *dev = vb->vb2_queue->drv_priv;
	struct cx88_buffer *buf = container_of(vbuf, struct cx88_buffer, vb);
	struct sg_table *sgt = vb2_dma_sg_plane_desc(vb, 0);
	unsigned int lines;
	unsigned int size;

	if (dev->core->tvnorm & V4L2_STD_525_60)
		lines = VBI_LINE_NTSC_COUNT;
	else
		lines = VBI_LINE_PAL_COUNT;
	size = lines * VBI_LINE_LENGTH * 2;
	if (vb2_plane_size(vb, 0) < size)
		return -EINVAL;
	vb2_set_plane_payload(vb, 0, size);

	cx88_risc_buffer(dev->pci, &buf->risc, sgt->sgl,
			 0, VBI_LINE_LENGTH * lines,
			 VBI_LINE_LENGTH, 0,
			 lines);
	return 0;
}

static void buffer_finish(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct cx8800_dev *dev = vb->vb2_queue->drv_priv;
	struct cx88_buffer *buf = container_of(vbuf, struct cx88_buffer, vb);
	struct cx88_riscmem *risc = &buf->risc;

	if (risc->cpu)
		pci_free_consistent(dev->pci, risc->size, risc->cpu, risc->dma);
	memset(risc, 0, sizeof(*risc));
}

static void buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct cx8800_dev *dev = vb->vb2_queue->drv_priv;
	struct cx88_buffer    *buf = container_of(vbuf, struct cx88_buffer, vb);
	struct cx88_buffer    *prev;
	struct cx88_dmaqueue  *q    = &dev->vbiq;

	/* add jump to start */
	buf->risc.cpu[1] = cpu_to_le32(buf->risc.dma + 8);
	buf->risc.jmp[0] = cpu_to_le32(RISC_JUMP | RISC_CNT_INC);
	buf->risc.jmp[1] = cpu_to_le32(buf->risc.dma + 8);

	if (list_empty(&q->active)) {
		list_add_tail(&buf->list, &q->active);
		cx8800_start_vbi_dma(dev, q, buf);
		dprintk(2,"[%p/%d] vbi_queue - first active\n",
			buf, buf->vb.vb2_buf.index);

	} else {
		buf->risc.cpu[0] |= cpu_to_le32(RISC_IRQ1);
		prev = list_entry(q->active.prev, struct cx88_buffer, list);
		list_add_tail(&buf->list, &q->active);
		prev->risc.jmp[1] = cpu_to_le32(buf->risc.dma);
		dprintk(2,"[%p/%d] buffer_queue - append to active\n",
			buf, buf->vb.vb2_buf.index);
	}
}

static int start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct cx8800_dev *dev = q->drv_priv;
	struct cx88_dmaqueue *dmaq = &dev->vbiq;
	struct cx88_buffer *buf = list_entry(dmaq->active.next,
			struct cx88_buffer, list);

	cx8800_start_vbi_dma(dev, dmaq, buf);
	return 0;
}

static void stop_streaming(struct vb2_queue *q)
{
	struct cx8800_dev *dev = q->drv_priv;
	struct cx88_core *core = dev->core;
	struct cx88_dmaqueue *dmaq = &dev->vbiq;
	unsigned long flags;

	cx_clear(MO_VID_DMACNTRL, 0x11);
	cx_clear(VID_CAPTURE_CONTROL, 0x06);
	cx8800_stop_vbi_dma(dev);
	spin_lock_irqsave(&dev->slock, flags);
	while (!list_empty(&dmaq->active)) {
		struct cx88_buffer *buf = list_entry(dmaq->active.next,
			struct cx88_buffer, list);

		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&dev->slock, flags);
}

const struct vb2_ops cx8800_vbi_qops = {
	.queue_setup    = queue_setup,
	.buf_prepare  = buffer_prepare,
	.buf_finish = buffer_finish,
	.buf_queue    = buffer_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.start_streaming = start_streaming,
	.stop_streaming = stop_streaming,
};
