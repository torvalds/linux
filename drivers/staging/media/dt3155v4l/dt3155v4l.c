/***************************************************************************
 *   Copyright (C) 2006-2010 by Marin Mitov                                *
 *   mitov@issp.bas.bg                                                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <linux/module.h>
#include <linux/version.h>
#include <linux/stringify.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-common.h>
#include <media/videobuf2-dma-contig.h>

#include "dt3155v4l.h"

#define DT3155_DEVICE_ID 0x1223

/* DT3155_CHUNK_SIZE is 4M (2^22) 8 full size buffers */
#define DT3155_CHUNK_SIZE (1U << 22)

#define DT3155_COH_FLAGS (GFP_KERNEL | GFP_DMA32 | __GFP_COLD | __GFP_NOWARN)

#define DT3155_BUF_SIZE (768 * 576)

#ifdef CONFIG_DT3155_STREAMING
#define DT3155_CAPTURE_METHOD V4L2_CAP_STREAMING
#else
#define DT3155_CAPTURE_METHOD V4L2_CAP_READWRITE
#endif

/*  global initializers (for all boards)  */
#ifdef CONFIG_DT3155_CCIR
static const u8 csr2_init = VT_50HZ;
#define DT3155_CURRENT_NORM V4L2_STD_625_50
static const unsigned int img_width = 768;
static const unsigned int img_height = 576;
static const unsigned int frames_per_sec = 25;
static const struct v4l2_fmtdesc frame_std[] = {
	{
	.index = 0,
	.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	.flags = 0,
	.description = "CCIR/50Hz 8 bits gray",
	.pixelformat = V4L2_PIX_FMT_GREY,
	},
};
#else
static const u8 csr2_init = VT_60HZ;
#define DT3155_CURRENT_NORM V4L2_STD_525_60
static const unsigned int img_width = 640;
static const unsigned int img_height = 480;
static const unsigned int frames_per_sec = 30;
static const struct v4l2_fmtdesc frame_std[] = {
	{
	.index = 0,
	.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	.flags = 0,
	.description = "RS-170/60Hz 8 bits gray",
	.pixelformat = V4L2_PIX_FMT_GREY,
	},
};
#endif

#define NUM_OF_FORMATS ARRAY_SIZE(frame_std)

static u8 config_init = ACQ_MODE_EVEN;

/**
 * read_i2c_reg - reads an internal i2c register
 *
 * @addr:	dt3155 mmio base address
 * @index:	index (internal address) of register to read
 * @data:	pointer to byte the read data will be placed in
 *
 * returns:	zero on success or error code
 *
 * This function starts reading the specified (by index) register
 * and busy waits for the process to finish. The result is placed
 * in a byte pointed by data.
 */
static int
read_i2c_reg(void __iomem *addr, u8 index, u8 *data)
{
	u32 tmp = index;

	iowrite32((tmp<<17) | IIC_READ, addr + IIC_CSR2);
	mmiowb();
	udelay(45); /* wait at least 43 usec for NEW_CYCLE to clear */
	if (ioread32(addr + IIC_CSR2) & NEW_CYCLE)
		return -EIO; /* error: NEW_CYCLE not cleared */
	tmp = ioread32(addr + IIC_CSR1);
	if (tmp & DIRECT_ABORT) {
		/* reset DIRECT_ABORT bit */
		iowrite32(DIRECT_ABORT, addr + IIC_CSR1);
		return -EIO; /* error: DIRECT_ABORT set */
	}
	*data = tmp>>24;
	return 0;
}

/**
 * write_i2c_reg - writes to an internal i2c register
 *
 * @addr:	dt3155 mmio base address
 * @index:	index (internal address) of register to read
 * @data:	data to be written
 *
 * returns:	zero on success or error code
 *
 * This function starts writting the specified (by index) register
 * and busy waits for the process to finish.
 */
static int
write_i2c_reg(void __iomem *addr, u8 index, u8 data)
{
	u32 tmp = index;

	iowrite32((tmp<<17) | IIC_WRITE | data, addr + IIC_CSR2);
	mmiowb();
	udelay(65); /* wait at least 63 usec for NEW_CYCLE to clear */
	if (ioread32(addr + IIC_CSR2) & NEW_CYCLE)
		return -EIO; /* error: NEW_CYCLE not cleared */
	if (ioread32(addr + IIC_CSR1) & DIRECT_ABORT) {
		/* reset DIRECT_ABORT bit */
		iowrite32(DIRECT_ABORT, addr + IIC_CSR1);
		return -EIO; /* error: DIRECT_ABORT set */
	}
	return 0;
}

/**
 * write_i2c_reg_nowait - writes to an internal i2c register
 *
 * @addr:	dt3155 mmio base address
 * @index:	index (internal address) of register to read
 * @data:	data to be written
 *
 * This function starts writting the specified (by index) register
 * and then returns.
 */
static void write_i2c_reg_nowait(void __iomem *addr, u8 index, u8 data)
{
	u32 tmp = index;

	iowrite32((tmp<<17) | IIC_WRITE | data, addr + IIC_CSR2);
	mmiowb();
}

/**
 * wait_i2c_reg - waits the read/write to finish
 *
 * @addr:	dt3155 mmio base address
 *
 * returns:	zero on success or error code
 *
 * This function waits reading/writting to finish.
 */
static int wait_i2c_reg(void __iomem *addr)
{
	if (ioread32(addr + IIC_CSR2) & NEW_CYCLE)
		udelay(65); /* wait at least 63 usec for NEW_CYCLE to clear */
	if (ioread32(addr + IIC_CSR2) & NEW_CYCLE)
		return -EIO; /* error: NEW_CYCLE not cleared */
	if (ioread32(addr + IIC_CSR1) & DIRECT_ABORT) {
		/* reset DIRECT_ABORT bit */
		iowrite32(DIRECT_ABORT, addr + IIC_CSR1);
		return -EIO; /* error: DIRECT_ABORT set */
	}
	return 0;
}

static int
dt3155_start_acq(struct dt3155_priv *pd)
{
	struct vb2_buffer *vb = pd->curr_buf;
	dma_addr_t dma_addr;

	dma_addr = vb2_dma_contig_plane_dma_addr(vb, 0);
	iowrite32(dma_addr, pd->regs + EVEN_DMA_START);
	iowrite32(dma_addr + img_width, pd->regs + ODD_DMA_START);
	iowrite32(img_width, pd->regs + EVEN_DMA_STRIDE);
	iowrite32(img_width, pd->regs + ODD_DMA_STRIDE);
	/* enable interrupts, clear all irq flags */
	iowrite32(FLD_START_EN | FLD_END_ODD_EN | FLD_START |
			FLD_END_EVEN | FLD_END_ODD, pd->regs + INT_CSR);
	iowrite32(FIFO_EN | SRST | FLD_CRPT_ODD | FLD_CRPT_EVEN |
		  FLD_DN_ODD | FLD_DN_EVEN | CAP_CONT_EVEN | CAP_CONT_ODD,
							pd->regs + CSR1);
	wait_i2c_reg(pd->regs);
	write_i2c_reg(pd->regs, CONFIG, pd->config);
	write_i2c_reg(pd->regs, EVEN_CSR, CSR_ERROR | CSR_DONE);
	write_i2c_reg(pd->regs, ODD_CSR, CSR_ERROR | CSR_DONE);

	/*  start the board  */
	write_i2c_reg(pd->regs, CSR2, pd->csr2 | BUSY_EVEN | BUSY_ODD);
	return 0; /* success  */
}

/*
 *	driver-specific callbacks (vb2_ops)
 */
static int
dt3155_queue_setup(struct vb2_queue *q, const struct v4l2_format *fmt,
		unsigned int *num_buffers, unsigned int *num_planes,
		unsigned int sizes[], void *alloc_ctxs[])

{
	struct dt3155_priv *pd = vb2_get_drv_priv(q);
	void *ret;

	if (*num_buffers == 0)
		*num_buffers = 1;
	*num_planes = 1;
	sizes[0] = img_width * img_height;
	if (pd->q->alloc_ctx[0])
		return 0;
	ret = vb2_dma_contig_init_ctx(&pd->pdev->dev);
	if (IS_ERR(ret))
		return PTR_ERR(ret);
	pd->q->alloc_ctx[0] = ret;
	return 0;
}

static void
dt3155_wait_prepare(struct vb2_queue *q)
{
	struct dt3155_priv *pd = vb2_get_drv_priv(q);

	mutex_unlock(pd->vdev->lock);
}

static void
dt3155_wait_finish(struct vb2_queue *q)
{
	struct dt3155_priv *pd = vb2_get_drv_priv(q);

	mutex_lock(pd->vdev->lock);
}

static int
dt3155_buf_prepare(struct vb2_buffer *vb)
{
	vb2_set_plane_payload(vb, 0, img_width * img_height);
	return 0;
}

static void
dt3155_stop_streaming(struct vb2_queue *q)
{
	struct dt3155_priv *pd = vb2_get_drv_priv(q);
	struct vb2_buffer *vb;

	spin_lock_irq(&pd->lock);
	while (!list_empty(&pd->dmaq)) {
		vb = list_first_entry(&pd->dmaq, typeof(*vb), done_entry);
		list_del(&vb->done_entry);
		vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irq(&pd->lock);
	msleep(45); /* irq hendler will stop the hardware */
}

static void
dt3155_buf_queue(struct vb2_buffer *vb)
{
	struct dt3155_priv *pd = vb2_get_drv_priv(vb->vb2_queue);

	/*  pd->q->streaming = 1 when dt3155_buf_queue() is invoked  */
	spin_lock_irq(&pd->lock);
	if (pd->curr_buf)
		list_add_tail(&vb->done_entry, &pd->dmaq);
	else {
		pd->curr_buf = vb;
		dt3155_start_acq(pd);
	}
	spin_unlock_irq(&pd->lock);
}
/*
 *	end driver-specific callbacks
 */

static const struct vb2_ops q_ops = {
	.queue_setup = dt3155_queue_setup,
	.wait_prepare = dt3155_wait_prepare,
	.wait_finish = dt3155_wait_finish,
	.buf_prepare = dt3155_buf_prepare,
	.stop_streaming = dt3155_stop_streaming,
	.buf_queue = dt3155_buf_queue,
};

static irqreturn_t
dt3155_irq_handler_even(int irq, void *dev_id)
{
	struct dt3155_priv *ipd = dev_id;
	struct vb2_buffer *ivb;
	dma_addr_t dma_addr;
	u32 tmp;

	tmp = ioread32(ipd->regs + INT_CSR) & (FLD_START | FLD_END_ODD);
	if (!tmp)
		return IRQ_NONE;  /* not our irq */
	if ((tmp & FLD_START) && !(tmp & FLD_END_ODD)) {
		iowrite32(FLD_START_EN | FLD_END_ODD_EN | FLD_START,
							ipd->regs + INT_CSR);
		ipd->field_count++;
		return IRQ_HANDLED; /* start of field irq */
	}
	if ((tmp & FLD_START) && (tmp & FLD_END_ODD))
		ipd->stats.start_before_end++;
	/*	check for corrupted fields     */
/*	write_i2c_reg(ipd->regs, EVEN_CSR, CSR_ERROR | CSR_DONE);	*/
/*	write_i2c_reg(ipd->regs, ODD_CSR, CSR_ERROR | CSR_DONE);	*/
	tmp = ioread32(ipd->regs + CSR1) & (FLD_CRPT_EVEN | FLD_CRPT_ODD);
	if (tmp) {
		ipd->stats.corrupted_fields++;
		iowrite32(FIFO_EN | SRST | FLD_CRPT_ODD | FLD_CRPT_EVEN |
						FLD_DN_ODD | FLD_DN_EVEN |
						CAP_CONT_EVEN | CAP_CONT_ODD,
							ipd->regs + CSR1);
		mmiowb();
	}

	spin_lock(&ipd->lock);
	if (ipd->curr_buf) {
		v4l2_get_timestamp(&ipd->curr_buf->v4l2_buf.timestamp);
		ipd->curr_buf->v4l2_buf.sequence = (ipd->field_count) >> 1;
		vb2_buffer_done(ipd->curr_buf, VB2_BUF_STATE_DONE);
	}

	if (!ipd->q->streaming || list_empty(&ipd->dmaq))
		goto stop_dma;
	ivb = list_first_entry(&ipd->dmaq, typeof(*ivb), done_entry);
	list_del(&ivb->done_entry);
	ipd->curr_buf = ivb;
	dma_addr = vb2_dma_contig_plane_dma_addr(ivb, 0);
	iowrite32(dma_addr, ipd->regs + EVEN_DMA_START);
	iowrite32(dma_addr + img_width, ipd->regs + ODD_DMA_START);
	iowrite32(img_width, ipd->regs + EVEN_DMA_STRIDE);
	iowrite32(img_width, ipd->regs + ODD_DMA_STRIDE);
	mmiowb();
	/* enable interrupts, clear all irq flags */
	iowrite32(FLD_START_EN | FLD_END_ODD_EN | FLD_START |
			FLD_END_EVEN | FLD_END_ODD, ipd->regs + INT_CSR);
	spin_unlock(&ipd->lock);
	return IRQ_HANDLED;

stop_dma:
	ipd->curr_buf = NULL;
	/* stop the board */
	write_i2c_reg_nowait(ipd->regs, CSR2, ipd->csr2);
	iowrite32(FIFO_EN | SRST | FLD_CRPT_ODD | FLD_CRPT_EVEN |
		  FLD_DN_ODD | FLD_DN_EVEN, ipd->regs + CSR1);
	/* disable interrupts, clear all irq flags */
	iowrite32(FLD_START | FLD_END_EVEN | FLD_END_ODD, ipd->regs + INT_CSR);
	spin_unlock(&ipd->lock);
	return IRQ_HANDLED;
}

static int
dt3155_open(struct file *filp)
{
	int ret = 0;
	struct dt3155_priv *pd = video_drvdata(filp);

	if (mutex_lock_interruptible(&pd->mux))
		return -ERESTARTSYS;
	if (!pd->users) {
		pd->q = kzalloc(sizeof(*pd->q), GFP_KERNEL);
		if (!pd->q) {
			ret = -ENOMEM;
			goto err_alloc_queue;
		}
		pd->q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		pd->q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
		pd->q->io_modes = VB2_READ | VB2_MMAP;
		pd->q->ops = &q_ops;
		pd->q->mem_ops = &vb2_dma_contig_memops;
		pd->q->drv_priv = pd;
		pd->curr_buf = NULL;
		pd->field_count = 0;
		ret = vb2_queue_init(pd->q);
		if (ret < 0)
			goto err_request_irq;
		INIT_LIST_HEAD(&pd->dmaq);
		spin_lock_init(&pd->lock);
		/* disable all irqs, clear all irq flags */
		iowrite32(FLD_START | FLD_END_EVEN | FLD_END_ODD,
						pd->regs + INT_CSR);
		ret = request_irq(pd->pdev->irq, dt3155_irq_handler_even,
						IRQF_SHARED, DT3155_NAME, pd);
		if (ret)
			goto err_request_irq;
	}
	pd->users++;
	mutex_unlock(&pd->mux);
	return 0; /* success */
err_request_irq:
	kfree(pd->q);
	pd->q = NULL;
err_alloc_queue:
	mutex_unlock(&pd->mux);
	return ret;
}

static int
dt3155_release(struct file *filp)
{
	struct dt3155_priv *pd = video_drvdata(filp);

	mutex_lock(&pd->mux);
	pd->users--;
	BUG_ON(pd->users < 0);
	if (!pd->users) {
		vb2_queue_release(pd->q);
		free_irq(pd->pdev->irq, pd);
		if (pd->q->alloc_ctx[0])
			vb2_dma_contig_cleanup_ctx(pd->q->alloc_ctx[0]);
		kfree(pd->q);
		pd->q = NULL;
	}
	mutex_unlock(&pd->mux);
	return 0;
}

static ssize_t
dt3155_read(struct file *filp, char __user *user, size_t size, loff_t *loff)
{
	struct dt3155_priv *pd = video_drvdata(filp);
	ssize_t res;

	if (mutex_lock_interruptible(&pd->mux))
		return -ERESTARTSYS;
	res = vb2_read(pd->q, user, size, loff, filp->f_flags & O_NONBLOCK);
	mutex_unlock(&pd->mux);
	return res;
}

static unsigned int
dt3155_poll(struct file *filp, struct poll_table_struct *polltbl)
{
	struct dt3155_priv *pd = video_drvdata(filp);
	unsigned int res;

	mutex_lock(&pd->mux);
	res = vb2_poll(pd->q, filp, polltbl);
	mutex_unlock(&pd->mux);
	return res;
}

static int
dt3155_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct dt3155_priv *pd = video_drvdata(filp);
	int res;

	if (mutex_lock_interruptible(&pd->mux))
		return -ERESTARTSYS;
	res = vb2_mmap(pd->q, vma);
	mutex_unlock(&pd->mux);
	return res;
}

static const struct v4l2_file_operations dt3155_fops = {
	.owner = THIS_MODULE,
	.open = dt3155_open,
	.release = dt3155_release,
	.read = dt3155_read,
	.poll = dt3155_poll,
	.unlocked_ioctl = video_ioctl2, /* V4L2 ioctl handler */
	.mmap = dt3155_mmap,
};

static int
dt3155_ioc_streamon(struct file *filp, void *p, enum v4l2_buf_type type)
{
	struct dt3155_priv *pd = video_drvdata(filp);

	return vb2_streamon(pd->q, type);
}

static int
dt3155_ioc_streamoff(struct file *filp, void *p, enum v4l2_buf_type type)
{
	struct dt3155_priv *pd = video_drvdata(filp);

	return vb2_streamoff(pd->q, type);
}

static int
dt3155_ioc_querycap(struct file *filp, void *p, struct v4l2_capability *cap)
{
	struct dt3155_priv *pd = video_drvdata(filp);

	strcpy(cap->driver, DT3155_NAME);
	strcpy(cap->card, DT3155_NAME " frame grabber");
	sprintf(cap->bus_info, "PCI:%s", pci_name(pd->pdev));
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE |
				DT3155_CAPTURE_METHOD;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int
dt3155_ioc_enum_fmt_vid_cap(struct file *filp, void *p, struct v4l2_fmtdesc *f)
{
	if (f->index >= NUM_OF_FORMATS)
		return -EINVAL;
	*f = frame_std[f->index];
	return 0;
}

static int
dt3155_ioc_g_fmt_vid_cap(struct file *filp, void *p, struct v4l2_format *f)
{
	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	f->fmt.pix.width = img_width;
	f->fmt.pix.height = img_height;
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_GREY;
	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.bytesperline = f->fmt.pix.width;
	f->fmt.pix.sizeimage = f->fmt.pix.width * f->fmt.pix.height;
	f->fmt.pix.colorspace = 0;
	f->fmt.pix.priv = 0;
	return 0;
}

static int
dt3155_ioc_try_fmt_vid_cap(struct file *filp, void *p, struct v4l2_format *f)
{
	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (f->fmt.pix.width == img_width &&
		f->fmt.pix.height == img_height &&
		f->fmt.pix.pixelformat == V4L2_PIX_FMT_GREY &&
		f->fmt.pix.field == V4L2_FIELD_NONE &&
		f->fmt.pix.bytesperline == f->fmt.pix.width &&
		f->fmt.pix.sizeimage == f->fmt.pix.width * f->fmt.pix.height)
			return 0;
	else
		return -EINVAL;
}

static int
dt3155_ioc_s_fmt_vid_cap(struct file *filp, void *p, struct v4l2_format *f)
{
	return dt3155_ioc_g_fmt_vid_cap(filp, p, f);
}

static int
dt3155_ioc_reqbufs(struct file *filp, void *p, struct v4l2_requestbuffers *b)
{
	struct dt3155_priv *pd = video_drvdata(filp);

	return vb2_reqbufs(pd->q, b);
}

static int
dt3155_ioc_querybuf(struct file *filp, void *p, struct v4l2_buffer *b)
{
	struct dt3155_priv *pd = video_drvdata(filp);

	return vb2_querybuf(pd->q, b);
}

static int
dt3155_ioc_qbuf(struct file *filp, void *p, struct v4l2_buffer *b)
{
	struct dt3155_priv *pd = video_drvdata(filp);

	return vb2_qbuf(pd->q, b);
}

static int
dt3155_ioc_dqbuf(struct file *filp, void *p, struct v4l2_buffer *b)
{
	struct dt3155_priv *pd = video_drvdata(filp);

	return vb2_dqbuf(pd->q, b, filp->f_flags & O_NONBLOCK);
}

static int
dt3155_ioc_querystd(struct file *filp, void *p, v4l2_std_id *norm)
{
	*norm = DT3155_CURRENT_NORM;
	return 0;
}

static int
dt3155_ioc_g_std(struct file *filp, void *p, v4l2_std_id *norm)
{
	*norm = DT3155_CURRENT_NORM;
	return 0;
}

static int
dt3155_ioc_s_std(struct file *filp, void *p, v4l2_std_id norm)
{
	if (norm & DT3155_CURRENT_NORM)
		return 0;
	return -EINVAL;
}

static int
dt3155_ioc_enum_input(struct file *filp, void *p, struct v4l2_input *input)
{
	if (input->index)
		return -EINVAL;
	strcpy(input->name, "Coax in");
	input->type = V4L2_INPUT_TYPE_CAMERA;
	/*
	 * FIXME: input->std = 0 according to v4l2 API
	 * VIDIOC_G_STD, VIDIOC_S_STD, VIDIOC_QUERYSTD and VIDIOC_ENUMSTD
	 * should return -EINVAL
	 */
	input->std = DT3155_CURRENT_NORM;
	input->status = 0;/* FIXME: add sync detection & V4L2_IN_ST_NO_H_LOCK */
	return 0;
}

static int
dt3155_ioc_g_input(struct file *filp, void *p, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int
dt3155_ioc_s_input(struct file *filp, void *p, unsigned int i)
{
	if (i)
		return -EINVAL;
	return 0;
}

static int
dt3155_ioc_g_parm(struct file *filp, void *p, struct v4l2_streamparm *parms)
{
	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	parms->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	parms->parm.capture.capturemode = 0;
	parms->parm.capture.timeperframe.numerator = 1001;
	parms->parm.capture.timeperframe.denominator = frames_per_sec * 1000;
	parms->parm.capture.extendedmode = 0;
	parms->parm.capture.readbuffers = 1; /* FIXME: 2 buffers? */
	return 0;
}

static int
dt3155_ioc_s_parm(struct file *filp, void *p, struct v4l2_streamparm *parms)
{
	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	parms->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	parms->parm.capture.capturemode = 0;
	parms->parm.capture.timeperframe.numerator = 1001;
	parms->parm.capture.timeperframe.denominator = frames_per_sec * 1000;
	parms->parm.capture.extendedmode = 0;
	parms->parm.capture.readbuffers = 1; /* FIXME: 2 buffers? */
	return 0;
}

static const struct v4l2_ioctl_ops dt3155_ioctl_ops = {
	.vidioc_streamon = dt3155_ioc_streamon,
	.vidioc_streamoff = dt3155_ioc_streamoff,
	.vidioc_querycap = dt3155_ioc_querycap,
/*
	.vidioc_g_priority = dt3155_ioc_g_priority,
	.vidioc_s_priority = dt3155_ioc_s_priority,
*/
	.vidioc_enum_fmt_vid_cap = dt3155_ioc_enum_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = dt3155_ioc_try_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap = dt3155_ioc_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = dt3155_ioc_s_fmt_vid_cap,
	.vidioc_reqbufs = dt3155_ioc_reqbufs,
	.vidioc_querybuf = dt3155_ioc_querybuf,
	.vidioc_qbuf = dt3155_ioc_qbuf,
	.vidioc_dqbuf = dt3155_ioc_dqbuf,
	.vidioc_querystd = dt3155_ioc_querystd,
	.vidioc_g_std = dt3155_ioc_g_std,
	.vidioc_s_std = dt3155_ioc_s_std,
	.vidioc_enum_input = dt3155_ioc_enum_input,
	.vidioc_g_input = dt3155_ioc_g_input,
	.vidioc_s_input = dt3155_ioc_s_input,
/*
	.vidioc_queryctrl = dt3155_ioc_queryctrl,
	.vidioc_g_ctrl = dt3155_ioc_g_ctrl,
	.vidioc_s_ctrl = dt3155_ioc_s_ctrl,
	.vidioc_querymenu = dt3155_ioc_querymenu,
	.vidioc_g_ext_ctrls = dt3155_ioc_g_ext_ctrls,
	.vidioc_s_ext_ctrls = dt3155_ioc_s_ext_ctrls,
*/
	.vidioc_g_parm = dt3155_ioc_g_parm,
	.vidioc_s_parm = dt3155_ioc_s_parm,
/*
	.vidioc_cropcap = dt3155_ioc_cropcap,
	.vidioc_g_crop = dt3155_ioc_g_crop,
	.vidioc_s_crop = dt3155_ioc_s_crop,
	.vidioc_enum_framesizes = dt3155_ioc_enum_framesizes,
	.vidioc_enum_frameintervals = dt3155_ioc_enum_frameintervals,
*/
};

static int
dt3155_init_board(struct pci_dev *pdev)
{
	struct dt3155_priv *pd = pci_get_drvdata(pdev);
	void *buf_cpu;
	dma_addr_t buf_dma;
	int i;
	u8 tmp;

	pci_set_master(pdev); /* dt3155 needs it */

	/*  resetting the adapter  */
	iowrite32(FLD_CRPT_ODD | FLD_CRPT_EVEN | FLD_DN_ODD | FLD_DN_EVEN,
							pd->regs + CSR1);
	mmiowb();
	msleep(20);

	/*  initializing adaper registers  */
	iowrite32(FIFO_EN | SRST, pd->regs + CSR1);
	mmiowb();
	iowrite32(0xEEEEEE01, pd->regs + EVEN_PIXEL_FMT);
	iowrite32(0xEEEEEE01, pd->regs + ODD_PIXEL_FMT);
	iowrite32(0x00000020, pd->regs + FIFO_TRIGER);
	iowrite32(0x00000103, pd->regs + XFER_MODE);
	iowrite32(0, pd->regs + RETRY_WAIT_CNT);
	iowrite32(0, pd->regs + INT_CSR);
	iowrite32(1, pd->regs + EVEN_FLD_MASK);
	iowrite32(1, pd->regs + ODD_FLD_MASK);
	iowrite32(0, pd->regs + MASK_LENGTH);
	iowrite32(0x0005007C, pd->regs + FIFO_FLAG_CNT);
	iowrite32(0x01010101, pd->regs + IIC_CLK_DUR);
	mmiowb();

	/* verifying that we have a DT3155 board (not just a SAA7116 chip) */
	read_i2c_reg(pd->regs, DT_ID, &tmp);
	if (tmp != DT3155_ID)
		return -ENODEV;

	/* initialize AD LUT */
	write_i2c_reg(pd->regs, AD_ADDR, 0);
	for (i = 0; i < 256; i++)
		write_i2c_reg(pd->regs, AD_LUT, i);

	/* initialize ADC references */
	/* FIXME: pos_ref & neg_ref depend on VT_50HZ */
	write_i2c_reg(pd->regs, AD_ADDR, AD_CMD_REG);
	write_i2c_reg(pd->regs, AD_CMD, VIDEO_CNL_1 | SYNC_CNL_1 | SYNC_LVL_3);
	write_i2c_reg(pd->regs, AD_ADDR, AD_POS_REF);
	write_i2c_reg(pd->regs, AD_CMD, 34);
	write_i2c_reg(pd->regs, AD_ADDR, AD_NEG_REF);
	write_i2c_reg(pd->regs, AD_CMD, 0);

	/* initialize PM LUT */
	write_i2c_reg(pd->regs, CONFIG, pd->config | PM_LUT_PGM);
	for (i = 0; i < 256; i++) {
		write_i2c_reg(pd->regs, PM_LUT_ADDR, i);
		write_i2c_reg(pd->regs, PM_LUT_DATA, i);
	}
	write_i2c_reg(pd->regs, CONFIG, pd->config | PM_LUT_PGM | PM_LUT_SEL);
	for (i = 0; i < 256; i++) {
		write_i2c_reg(pd->regs, PM_LUT_ADDR, i);
		write_i2c_reg(pd->regs, PM_LUT_DATA, i);
	}
	write_i2c_reg(pd->regs, CONFIG, pd->config); /*  ACQ_MODE_EVEN  */

	/* select channel 1 for input and set sync level */
	write_i2c_reg(pd->regs, AD_ADDR, AD_CMD_REG);
	write_i2c_reg(pd->regs, AD_CMD, VIDEO_CNL_1 | SYNC_CNL_1 | SYNC_LVL_3);

	/* allocate memory, and initialize the DMA machine */
	buf_cpu = dma_alloc_coherent(&pdev->dev, DT3155_BUF_SIZE, &buf_dma,
								GFP_KERNEL);
	if (!buf_cpu)
		return -ENOMEM;
	iowrite32(buf_dma, pd->regs + EVEN_DMA_START);
	iowrite32(buf_dma, pd->regs + ODD_DMA_START);
	iowrite32(0, pd->regs + EVEN_DMA_STRIDE);
	iowrite32(0, pd->regs + ODD_DMA_STRIDE);

	/*  Perform a pseudo even field acquire    */
	iowrite32(FIFO_EN | SRST | CAP_CONT_ODD, pd->regs + CSR1);
	write_i2c_reg(pd->regs, CSR2, pd->csr2 | SYNC_SNTL);
	write_i2c_reg(pd->regs, CONFIG, pd->config);
	write_i2c_reg(pd->regs, EVEN_CSR, CSR_SNGL);
	write_i2c_reg(pd->regs, CSR2, pd->csr2 | BUSY_EVEN | SYNC_SNTL);
	msleep(100);
	read_i2c_reg(pd->regs, CSR2, &tmp);
	write_i2c_reg(pd->regs, EVEN_CSR, CSR_ERROR | CSR_SNGL | CSR_DONE);
	write_i2c_reg(pd->regs, ODD_CSR, CSR_ERROR | CSR_SNGL | CSR_DONE);
	write_i2c_reg(pd->regs, CSR2, pd->csr2);
	iowrite32(FIFO_EN | SRST | FLD_DN_EVEN | FLD_DN_ODD, pd->regs + CSR1);

	/*  deallocate memory  */
	dma_free_coherent(&pdev->dev, DT3155_BUF_SIZE, buf_cpu, buf_dma);
	if (tmp & BUSY_EVEN)
		return -EIO;
	return 0;
}

static struct video_device dt3155_vdev = {
	.name = DT3155_NAME,
	.fops = &dt3155_fops,
	.ioctl_ops = &dt3155_ioctl_ops,
	.minor = -1,
	.release = video_device_release,
	.tvnorms = DT3155_CURRENT_NORM,
};

/* same as in drivers/base/dma-coherent.c */
struct dma_coherent_mem {
	void		*virt_base;
	dma_addr_t	device_base;
	int		size;
	int		flags;
	unsigned long	*bitmap;
};

static int
dt3155_alloc_coherent(struct device *dev, size_t size, int flags)
{
	struct dma_coherent_mem *mem;
	dma_addr_t dev_base;
	int pages = size >> PAGE_SHIFT;
	int bitmap_size = BITS_TO_LONGS(pages) * sizeof(long);

	if ((flags & DMA_MEMORY_MAP) == 0)
		goto out;
	if (!size)
		goto out;
	if (dev->dma_mem)
		goto out;

	mem = kzalloc(sizeof(*mem), GFP_KERNEL);
	if (!mem)
		goto out;
	mem->virt_base = dma_alloc_coherent(dev, size, &dev_base,
							DT3155_COH_FLAGS);
	if (!mem->virt_base)
		goto err_alloc_coherent;
	mem->bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!mem->bitmap)
		goto err_bitmap;

	/* coherent_dma_mask is already set to 32 bits */
	mem->device_base = dev_base;
	mem->size = pages;
	mem->flags = flags;
	dev->dma_mem = mem;
	return DMA_MEMORY_MAP;

err_bitmap:
	dma_free_coherent(dev, size, mem->virt_base, dev_base);
err_alloc_coherent:
	kfree(mem);
out:
	return 0;
}

static void
dt3155_free_coherent(struct device *dev)
{
	struct dma_coherent_mem *mem = dev->dma_mem;

	if (!mem)
		return;
	dev->dma_mem = NULL;
	dma_free_coherent(dev, mem->size << PAGE_SHIFT,
					mem->virt_base, mem->device_base);
	kfree(mem->bitmap);
	kfree(mem);
}

static int
dt3155_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int err;
	struct dt3155_priv *pd;

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err)
		return -ENODEV;
	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;
	pd->vdev = video_device_alloc();
	if (!pd->vdev) {
		err = -ENOMEM;
		goto err_video_device_alloc;
	}
	*pd->vdev = dt3155_vdev;
	pci_set_drvdata(pdev, pd);    /* for use in dt3155_remove() */
	video_set_drvdata(pd->vdev, pd);  /* for use in video_fops */
	pd->users = 0;
	pd->pdev = pdev;
	INIT_LIST_HEAD(&pd->dmaq);
	mutex_init(&pd->mux);
	pd->vdev->lock = &pd->mux; /* for locking v4l2_file_operations */
	spin_lock_init(&pd->lock);
	pd->csr2 = csr2_init;
	pd->config = config_init;
	err = pci_enable_device(pdev);
	if (err)
		goto err_enable_dev;
	err = pci_request_region(pdev, 0, pci_name(pdev));
	if (err)
		goto err_req_region;
	pd->regs = pci_iomap(pdev, 0, pci_resource_len(pd->pdev, 0));
	if (!pd->regs) {
		err = -ENOMEM;
		goto err_pci_iomap;
	}
	err = dt3155_init_board(pdev);
	if (err)
		goto err_init_board;
	err = video_register_device(pd->vdev, VFL_TYPE_GRABBER, -1);
	if (err)
		goto err_init_board;
	if (dt3155_alloc_coherent(&pdev->dev, DT3155_CHUNK_SIZE,
							DMA_MEMORY_MAP))
		dev_info(&pdev->dev, "preallocated 8 buffers\n");
	dev_info(&pdev->dev, "/dev/video%i is ready\n", pd->vdev->minor);
	return 0;  /*   success   */

err_init_board:
	pci_iounmap(pdev, pd->regs);
err_pci_iomap:
	pci_release_region(pdev, 0);
err_req_region:
	pci_disable_device(pdev);
err_enable_dev:
	video_device_release(pd->vdev);
err_video_device_alloc:
	kfree(pd);
	return err;
}

static void
dt3155_remove(struct pci_dev *pdev)
{
	struct dt3155_priv *pd = pci_get_drvdata(pdev);

	dt3155_free_coherent(&pdev->dev);
	video_unregister_device(pd->vdev);
	pci_iounmap(pdev, pd->regs);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);
	/*
	 * video_device_release() is invoked automatically
	 * see: struct video_device dt3155_vdev
	 */
	kfree(pd);
}

static const struct pci_device_id pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, DT3155_DEVICE_ID) },
	{ 0, /* zero marks the end */ },
};
MODULE_DEVICE_TABLE(pci, pci_ids);

static struct pci_driver pci_driver = {
	.name = DT3155_NAME,
	.id_table = pci_ids,
	.probe = dt3155_probe,
	.remove = dt3155_remove,
};

module_pci_driver(pci_driver);

MODULE_DESCRIPTION("video4linux pci-driver for dt3155 frame grabber");
MODULE_AUTHOR("Marin Mitov <mitov@issp.bas.bg>");
MODULE_VERSION(DT3155_VERSION);
MODULE_LICENSE("GPL");
