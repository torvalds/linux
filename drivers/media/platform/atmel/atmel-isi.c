// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011 Atmel Corporation
 * Josh Wu, <josh.wu@atmel.com>
 *
 * Based on previous work by Lars Haring, <lars.haring@atmel.com>
 * and Sedji Gaouaou
 * Based on the bttv driver for Bt848 with respective copyright holders
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/of.h>

#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-image-sizes.h>

#include "atmel-isi.h"

#define MAX_SUPPORT_WIDTH		2048U
#define MAX_SUPPORT_HEIGHT		2048U
#define MIN_FRAME_RATE			15
#define FRAME_INTERVAL_MILLI_SEC	(1000 / MIN_FRAME_RATE)

/* Frame buffer descriptor */
struct fbd {
	/* Physical address of the frame buffer */
	u32 fb_address;
	/* DMA Control Register(only in HISI2) */
	u32 dma_ctrl;
	/* Physical address of the next fbd */
	u32 next_fbd_address;
};

static void set_dma_ctrl(struct fbd *fb_desc, u32 ctrl)
{
	fb_desc->dma_ctrl = ctrl;
}

struct isi_dma_desc {
	struct list_head list;
	struct fbd *p_fbd;
	dma_addr_t fbd_phys;
};

/* Frame buffer data */
struct frame_buffer {
	struct vb2_v4l2_buffer vb;
	struct isi_dma_desc *p_dma_desc;
	struct list_head list;
};

struct isi_graph_entity {
	struct device_node *node;

	struct v4l2_async_subdev asd;
	struct v4l2_subdev *subdev;
};

/*
 * struct isi_format - ISI media bus format information
 * @fourcc:		Fourcc code for this format
 * @mbus_code:		V4L2 media bus format code.
 * @bpp:		Bytes per pixel (when stored in memory)
 * @swap:		Byte swap configuration value
 * @support:		Indicates format supported by subdev
 * @skip:		Skip duplicate format supported by subdev
 */
struct isi_format {
	u32	fourcc;
	u32	mbus_code;
	u8	bpp;
	u32	swap;
};


struct atmel_isi {
	/* Protects the access of variables shared with the ISR */
	spinlock_t			irqlock;
	struct device			*dev;
	void __iomem			*regs;

	int				sequence;

	/* Allocate descriptors for dma buffer use */
	struct fbd			*p_fb_descriptors;
	dma_addr_t			fb_descriptors_phys;
	struct				list_head dma_desc_head;
	struct isi_dma_desc		dma_desc[VIDEO_MAX_FRAME];
	bool				enable_preview_path;

	struct completion		complete;
	/* ISI peripheral clock */
	struct clk			*pclk;
	unsigned int			irq;

	struct isi_platform_data	pdata;
	u16				width_flags;	/* max 12 bits */

	struct list_head		video_buffer_list;
	struct frame_buffer		*active;

	struct v4l2_device		v4l2_dev;
	struct video_device		*vdev;
	struct v4l2_async_notifier	notifier;
	struct isi_graph_entity		entity;
	struct v4l2_format		fmt;

	const struct isi_format		**user_formats;
	unsigned int			num_user_formats;
	const struct isi_format		*current_fmt;

	struct mutex			lock;
	struct vb2_queue		queue;
};

#define notifier_to_isi(n) container_of(n, struct atmel_isi, notifier)

static void isi_writel(struct atmel_isi *isi, u32 reg, u32 val)
{
	writel(val, isi->regs + reg);
}
static u32 isi_readl(struct atmel_isi *isi, u32 reg)
{
	return readl(isi->regs + reg);
}

static void configure_geometry(struct atmel_isi *isi)
{
	u32 cfg2, psize;
	u32 fourcc = isi->current_fmt->fourcc;

	isi->enable_preview_path = fourcc == V4L2_PIX_FMT_RGB565 ||
				   fourcc == V4L2_PIX_FMT_RGB32 ||
				   fourcc == V4L2_PIX_FMT_Y16;

	/* According to sensor's output format to set cfg2 */
	cfg2 = isi->current_fmt->swap;

	isi_writel(isi, ISI_CTRL, ISI_CTRL_DIS);
	/* Set width */
	cfg2 |= ((isi->fmt.fmt.pix.width - 1) << ISI_CFG2_IM_HSIZE_OFFSET) &
			ISI_CFG2_IM_HSIZE_MASK;
	/* Set height */
	cfg2 |= ((isi->fmt.fmt.pix.height - 1) << ISI_CFG2_IM_VSIZE_OFFSET)
			& ISI_CFG2_IM_VSIZE_MASK;
	isi_writel(isi, ISI_CFG2, cfg2);

	/* No down sampling, preview size equal to sensor output size */
	psize = ((isi->fmt.fmt.pix.width - 1) << ISI_PSIZE_PREV_HSIZE_OFFSET) &
		ISI_PSIZE_PREV_HSIZE_MASK;
	psize |= ((isi->fmt.fmt.pix.height - 1) << ISI_PSIZE_PREV_VSIZE_OFFSET) &
		ISI_PSIZE_PREV_VSIZE_MASK;
	isi_writel(isi, ISI_PSIZE, psize);
	isi_writel(isi, ISI_PDECF, ISI_PDECF_NO_SAMPLING);
}

static irqreturn_t atmel_isi_handle_streaming(struct atmel_isi *isi)
{
	if (isi->active) {
		struct vb2_v4l2_buffer *vbuf = &isi->active->vb;
		struct frame_buffer *buf = isi->active;

		list_del_init(&buf->list);
		vbuf->vb2_buf.timestamp = ktime_get_ns();
		vbuf->sequence = isi->sequence++;
		vbuf->field = V4L2_FIELD_NONE;
		vb2_buffer_done(&vbuf->vb2_buf, VB2_BUF_STATE_DONE);
	}

	if (list_empty(&isi->video_buffer_list)) {
		isi->active = NULL;
	} else {
		/* start next dma frame. */
		isi->active = list_entry(isi->video_buffer_list.next,
					struct frame_buffer, list);
		if (!isi->enable_preview_path) {
			isi_writel(isi, ISI_DMA_C_DSCR,
				(u32)isi->active->p_dma_desc->fbd_phys);
			isi_writel(isi, ISI_DMA_C_CTRL,
				ISI_DMA_CTRL_FETCH | ISI_DMA_CTRL_DONE);
			isi_writel(isi, ISI_DMA_CHER, ISI_DMA_CHSR_C_CH);
		} else {
			isi_writel(isi, ISI_DMA_P_DSCR,
				(u32)isi->active->p_dma_desc->fbd_phys);
			isi_writel(isi, ISI_DMA_P_CTRL,
				ISI_DMA_CTRL_FETCH | ISI_DMA_CTRL_DONE);
			isi_writel(isi, ISI_DMA_CHER, ISI_DMA_CHSR_P_CH);
		}
	}
	return IRQ_HANDLED;
}

/* ISI interrupt service routine */
static irqreturn_t isi_interrupt(int irq, void *dev_id)
{
	struct atmel_isi *isi = dev_id;
	u32 status, mask, pending;
	irqreturn_t ret = IRQ_NONE;

	spin_lock(&isi->irqlock);

	status = isi_readl(isi, ISI_STATUS);
	mask = isi_readl(isi, ISI_INTMASK);
	pending = status & mask;

	if (pending & ISI_CTRL_SRST) {
		complete(&isi->complete);
		isi_writel(isi, ISI_INTDIS, ISI_CTRL_SRST);
		ret = IRQ_HANDLED;
	} else if (pending & ISI_CTRL_DIS) {
		complete(&isi->complete);
		isi_writel(isi, ISI_INTDIS, ISI_CTRL_DIS);
		ret = IRQ_HANDLED;
	} else {
		if (likely(pending & ISI_SR_CXFR_DONE) ||
				likely(pending & ISI_SR_PXFR_DONE))
			ret = atmel_isi_handle_streaming(isi);
	}

	spin_unlock(&isi->irqlock);
	return ret;
}

#define	WAIT_ISI_RESET		1
#define	WAIT_ISI_DISABLE	0
static int atmel_isi_wait_status(struct atmel_isi *isi, int wait_reset)
{
	unsigned long timeout;
	/*
	 * The reset or disable will only succeed if we have a
	 * pixel clock from the camera.
	 */
	init_completion(&isi->complete);

	if (wait_reset) {
		isi_writel(isi, ISI_INTEN, ISI_CTRL_SRST);
		isi_writel(isi, ISI_CTRL, ISI_CTRL_SRST);
	} else {
		isi_writel(isi, ISI_INTEN, ISI_CTRL_DIS);
		isi_writel(isi, ISI_CTRL, ISI_CTRL_DIS);
	}

	timeout = wait_for_completion_timeout(&isi->complete,
			msecs_to_jiffies(500));
	if (timeout == 0)
		return -ETIMEDOUT;

	return 0;
}

/* ------------------------------------------------------------------
	Videobuf operations
   ------------------------------------------------------------------*/
static int queue_setup(struct vb2_queue *vq,
				unsigned int *nbuffers, unsigned int *nplanes,
				unsigned int sizes[], struct device *alloc_devs[])
{
	struct atmel_isi *isi = vb2_get_drv_priv(vq);
	unsigned long size;

	size = isi->fmt.fmt.pix.sizeimage;

	/* Make sure the image size is large enough. */
	if (*nplanes)
		return sizes[0] < size ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = size;

	isi->active = NULL;

	dev_dbg(isi->dev, "%s, count=%d, size=%ld\n", __func__,
		*nbuffers, size);

	return 0;
}

static int buffer_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct frame_buffer *buf = container_of(vbuf, struct frame_buffer, vb);

	buf->p_dma_desc = NULL;
	INIT_LIST_HEAD(&buf->list);

	return 0;
}

static int buffer_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct frame_buffer *buf = container_of(vbuf, struct frame_buffer, vb);
	struct atmel_isi *isi = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long size;
	struct isi_dma_desc *desc;

	size = isi->fmt.fmt.pix.sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(isi->dev, "%s data will not fit into plane (%lu < %lu)\n",
				__func__, vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, size);

	if (!buf->p_dma_desc) {
		if (list_empty(&isi->dma_desc_head)) {
			dev_err(isi->dev, "Not enough dma descriptors.\n");
			return -EINVAL;
		} else {
			/* Get an available descriptor */
			desc = list_entry(isi->dma_desc_head.next,
						struct isi_dma_desc, list);
			/* Delete the descriptor since now it is used */
			list_del_init(&desc->list);

			/* Initialize the dma descriptor */
			desc->p_fbd->fb_address =
					vb2_dma_contig_plane_dma_addr(vb, 0);
			desc->p_fbd->next_fbd_address = 0;
			set_dma_ctrl(desc->p_fbd, ISI_DMA_CTRL_WB);

			buf->p_dma_desc = desc;
		}
	}
	return 0;
}

static void buffer_cleanup(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct atmel_isi *isi = vb2_get_drv_priv(vb->vb2_queue);
	struct frame_buffer *buf = container_of(vbuf, struct frame_buffer, vb);

	/* This descriptor is available now and we add to head list */
	if (buf->p_dma_desc)
		list_add(&buf->p_dma_desc->list, &isi->dma_desc_head);
}

static void start_dma(struct atmel_isi *isi, struct frame_buffer *buffer)
{
	u32 ctrl, cfg1;

	cfg1 = isi_readl(isi, ISI_CFG1);
	/* Enable irq: cxfr for the codec path, pxfr for the preview path */
	isi_writel(isi, ISI_INTEN,
			ISI_SR_CXFR_DONE | ISI_SR_PXFR_DONE);

	/* Check if already in a frame */
	if (!isi->enable_preview_path) {
		if (isi_readl(isi, ISI_STATUS) & ISI_CTRL_CDC) {
			dev_err(isi->dev, "Already in frame handling.\n");
			return;
		}

		isi_writel(isi, ISI_DMA_C_DSCR,
				(u32)buffer->p_dma_desc->fbd_phys);
		isi_writel(isi, ISI_DMA_C_CTRL,
				ISI_DMA_CTRL_FETCH | ISI_DMA_CTRL_DONE);
		isi_writel(isi, ISI_DMA_CHER, ISI_DMA_CHSR_C_CH);
	} else {
		isi_writel(isi, ISI_DMA_P_DSCR,
				(u32)buffer->p_dma_desc->fbd_phys);
		isi_writel(isi, ISI_DMA_P_CTRL,
				ISI_DMA_CTRL_FETCH | ISI_DMA_CTRL_DONE);
		isi_writel(isi, ISI_DMA_CHER, ISI_DMA_CHSR_P_CH);
	}

	cfg1 &= ~ISI_CFG1_FRATE_DIV_MASK;
	/* Enable linked list */
	cfg1 |= isi->pdata.frate | ISI_CFG1_DISCR;

	/* Enable ISI */
	ctrl = ISI_CTRL_EN;

	if (!isi->enable_preview_path)
		ctrl |= ISI_CTRL_CDC;

	isi_writel(isi, ISI_CTRL, ctrl);
	isi_writel(isi, ISI_CFG1, cfg1);
}

static void buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct atmel_isi *isi = vb2_get_drv_priv(vb->vb2_queue);
	struct frame_buffer *buf = container_of(vbuf, struct frame_buffer, vb);
	unsigned long flags = 0;

	spin_lock_irqsave(&isi->irqlock, flags);
	list_add_tail(&buf->list, &isi->video_buffer_list);

	if (!isi->active) {
		isi->active = buf;
		if (vb2_is_streaming(vb->vb2_queue))
			start_dma(isi, buf);
	}
	spin_unlock_irqrestore(&isi->irqlock, flags);
}

static int start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct atmel_isi *isi = vb2_get_drv_priv(vq);
	struct frame_buffer *buf, *node;
	int ret;

	pm_runtime_get_sync(isi->dev);

	/* Enable stream on the sub device */
	ret = v4l2_subdev_call(isi->entity.subdev, video, s_stream, 1);
	if (ret && ret != -ENOIOCTLCMD) {
		dev_err(isi->dev, "stream on failed in subdev\n");
		goto err_start_stream;
	}

	/* Reset ISI */
	ret = atmel_isi_wait_status(isi, WAIT_ISI_RESET);
	if (ret < 0) {
		dev_err(isi->dev, "Reset ISI timed out\n");
		goto err_reset;
	}
	/* Disable all interrupts */
	isi_writel(isi, ISI_INTDIS, (u32)~0UL);

	isi->sequence = 0;
	configure_geometry(isi);

	spin_lock_irq(&isi->irqlock);
	/* Clear any pending interrupt */
	isi_readl(isi, ISI_STATUS);

	start_dma(isi, isi->active);
	spin_unlock_irq(&isi->irqlock);

	return 0;

err_reset:
	v4l2_subdev_call(isi->entity.subdev, video, s_stream, 0);

err_start_stream:
	pm_runtime_put(isi->dev);

	spin_lock_irq(&isi->irqlock);
	isi->active = NULL;
	/* Release all active buffers */
	list_for_each_entry_safe(buf, node, &isi->video_buffer_list, list) {
		list_del_init(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_QUEUED);
	}
	spin_unlock_irq(&isi->irqlock);

	return ret;
}

/* abort streaming and wait for last buffer */
static void stop_streaming(struct vb2_queue *vq)
{
	struct atmel_isi *isi = vb2_get_drv_priv(vq);
	struct frame_buffer *buf, *node;
	int ret = 0;
	unsigned long timeout;

	/* Disable stream on the sub device */
	ret = v4l2_subdev_call(isi->entity.subdev, video, s_stream, 0);
	if (ret && ret != -ENOIOCTLCMD)
		dev_err(isi->dev, "stream off failed in subdev\n");

	spin_lock_irq(&isi->irqlock);
	isi->active = NULL;
	/* Release all active buffers */
	list_for_each_entry_safe(buf, node, &isi->video_buffer_list, list) {
		list_del_init(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irq(&isi->irqlock);

	if (!isi->enable_preview_path) {
		timeout = jiffies + (FRAME_INTERVAL_MILLI_SEC * HZ) / 1000;
		/* Wait until the end of the current frame. */
		while ((isi_readl(isi, ISI_STATUS) & ISI_CTRL_CDC) &&
				time_before(jiffies, timeout))
			msleep(1);

		if (time_after(jiffies, timeout))
			dev_err(isi->dev,
				"Timeout waiting for finishing codec request\n");
	}

	/* Disable interrupts */
	isi_writel(isi, ISI_INTDIS,
			ISI_SR_CXFR_DONE | ISI_SR_PXFR_DONE);

	/* Disable ISI and wait for it is done */
	ret = atmel_isi_wait_status(isi, WAIT_ISI_DISABLE);
	if (ret < 0)
		dev_err(isi->dev, "Disable ISI timed out\n");

	pm_runtime_put(isi->dev);
}

static const struct vb2_ops isi_video_qops = {
	.queue_setup		= queue_setup,
	.buf_init		= buffer_init,
	.buf_prepare		= buffer_prepare,
	.buf_cleanup		= buffer_cleanup,
	.buf_queue		= buffer_queue,
	.start_streaming	= start_streaming,
	.stop_streaming		= stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

static int isi_g_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *fmt)
{
	struct atmel_isi *isi = video_drvdata(file);

	*fmt = isi->fmt;

	return 0;
}

static const struct isi_format *find_format_by_fourcc(struct atmel_isi *isi,
						      unsigned int fourcc)
{
	unsigned int num_formats = isi->num_user_formats;
	const struct isi_format *fmt;
	unsigned int i;

	for (i = 0; i < num_formats; i++) {
		fmt = isi->user_formats[i];
		if (fmt->fourcc == fourcc)
			return fmt;
	}

	return NULL;
}

static void isi_try_fse(struct atmel_isi *isi, const struct isi_format *isi_fmt,
			struct v4l2_subdev_pad_config *pad_cfg)
{
	int ret;
	struct v4l2_subdev_frame_size_enum fse = {
		.code = isi_fmt->mbus_code,
		.which = V4L2_SUBDEV_FORMAT_TRY,
	};

	ret = v4l2_subdev_call(isi->entity.subdev, pad, enum_frame_size,
			       pad_cfg, &fse);
	/*
	 * Attempt to obtain format size from subdev. If not available,
	 * just use the maximum ISI can receive.
	 */
	if (ret) {
		pad_cfg->try_crop.width = MAX_SUPPORT_WIDTH;
		pad_cfg->try_crop.height = MAX_SUPPORT_HEIGHT;
	} else {
		pad_cfg->try_crop.width = fse.max_width;
		pad_cfg->try_crop.height = fse.max_height;
	}
}

static int isi_try_fmt(struct atmel_isi *isi, struct v4l2_format *f,
		       const struct isi_format **current_fmt)
{
	const struct isi_format *isi_fmt;
	struct v4l2_pix_format *pixfmt = &f->fmt.pix;
	struct v4l2_subdev_pad_config pad_cfg = {};
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
	};
	int ret;

	isi_fmt = find_format_by_fourcc(isi, pixfmt->pixelformat);
	if (!isi_fmt) {
		isi_fmt = isi->user_formats[isi->num_user_formats - 1];
		pixfmt->pixelformat = isi_fmt->fourcc;
	}

	/* Limit to Atmel ISI hardware capabilities */
	pixfmt->width = clamp(pixfmt->width, 0U, MAX_SUPPORT_WIDTH);
	pixfmt->height = clamp(pixfmt->height, 0U, MAX_SUPPORT_HEIGHT);

	v4l2_fill_mbus_format(&format.format, pixfmt, isi_fmt->mbus_code);

	isi_try_fse(isi, isi_fmt, &pad_cfg);

	ret = v4l2_subdev_call(isi->entity.subdev, pad, set_fmt,
			       &pad_cfg, &format);
	if (ret < 0)
		return ret;

	v4l2_fill_pix_format(pixfmt, &format.format);

	pixfmt->field = V4L2_FIELD_NONE;
	pixfmt->bytesperline = pixfmt->width * isi_fmt->bpp;
	pixfmt->sizeimage = pixfmt->bytesperline * pixfmt->height;

	if (current_fmt)
		*current_fmt = isi_fmt;

	return 0;
}

static int isi_set_fmt(struct atmel_isi *isi, struct v4l2_format *f)
{
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	const struct isi_format *current_fmt;
	int ret;

	ret = isi_try_fmt(isi, f, &current_fmt);
	if (ret)
		return ret;

	v4l2_fill_mbus_format(&format.format, &f->fmt.pix,
			      current_fmt->mbus_code);
	ret = v4l2_subdev_call(isi->entity.subdev, pad,
			       set_fmt, NULL, &format);
	if (ret < 0)
		return ret;

	isi->fmt = *f;
	isi->current_fmt = current_fmt;

	return 0;
}

static int isi_s_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct atmel_isi *isi = video_drvdata(file);

	if (vb2_is_streaming(&isi->queue))
		return -EBUSY;

	return isi_set_fmt(isi, f);
}

static int isi_try_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct atmel_isi *isi = video_drvdata(file);

	return isi_try_fmt(isi, f, NULL);
}

static int isi_enum_fmt_vid_cap(struct file *file, void  *priv,
				struct v4l2_fmtdesc *f)
{
	struct atmel_isi *isi = video_drvdata(file);

	if (f->index >= isi->num_user_formats)
		return -EINVAL;

	f->pixelformat = isi->user_formats[f->index]->fourcc;
	return 0;
}

static int isi_querycap(struct file *file, void *priv,
			struct v4l2_capability *cap)
{
	strscpy(cap->driver, "atmel-isi", sizeof(cap->driver));
	strscpy(cap->card, "Atmel Image Sensor Interface", sizeof(cap->card));
	strscpy(cap->bus_info, "platform:isi", sizeof(cap->bus_info));
	return 0;
}

static int isi_enum_input(struct file *file, void *priv,
			   struct v4l2_input *i)
{
	if (i->index != 0)
		return -EINVAL;

	i->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(i->name, "Camera", sizeof(i->name));
	return 0;
}

static int isi_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int isi_s_input(struct file *file, void *priv, unsigned int i)
{
	if (i > 0)
		return -EINVAL;
	return 0;
}

static int isi_g_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct atmel_isi *isi = video_drvdata(file);

	return v4l2_g_parm_cap(video_devdata(file), isi->entity.subdev, a);
}

static int isi_s_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct atmel_isi *isi = video_drvdata(file);

	return v4l2_s_parm_cap(video_devdata(file), isi->entity.subdev, a);
}

static int isi_enum_framesizes(struct file *file, void *fh,
			       struct v4l2_frmsizeenum *fsize)
{
	struct atmel_isi *isi = video_drvdata(file);
	const struct isi_format *isi_fmt;
	struct v4l2_subdev_frame_size_enum fse = {
		.index = fsize->index,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	isi_fmt = find_format_by_fourcc(isi, fsize->pixel_format);
	if (!isi_fmt)
		return -EINVAL;

	fse.code = isi_fmt->mbus_code;

	ret = v4l2_subdev_call(isi->entity.subdev, pad, enum_frame_size,
			       NULL, &fse);
	if (ret)
		return ret;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = fse.max_width;
	fsize->discrete.height = fse.max_height;

	return 0;
}

static int isi_enum_frameintervals(struct file *file, void *fh,
				    struct v4l2_frmivalenum *fival)
{
	struct atmel_isi *isi = video_drvdata(file);
	const struct isi_format *isi_fmt;
	struct v4l2_subdev_frame_interval_enum fie = {
		.index = fival->index,
		.width = fival->width,
		.height = fival->height,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	isi_fmt = find_format_by_fourcc(isi, fival->pixel_format);
	if (!isi_fmt)
		return -EINVAL;

	fie.code = isi_fmt->mbus_code;

	ret = v4l2_subdev_call(isi->entity.subdev, pad,
			       enum_frame_interval, NULL, &fie);
	if (ret)
		return ret;

	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete = fie.interval;

	return 0;
}

static void isi_camera_set_bus_param(struct atmel_isi *isi)
{
	u32 cfg1 = 0;

	/* set bus param for ISI */
	if (isi->pdata.hsync_act_low)
		cfg1 |= ISI_CFG1_HSYNC_POL_ACTIVE_LOW;
	if (isi->pdata.vsync_act_low)
		cfg1 |= ISI_CFG1_VSYNC_POL_ACTIVE_LOW;
	if (isi->pdata.pclk_act_falling)
		cfg1 |= ISI_CFG1_PIXCLK_POL_ACTIVE_FALLING;
	if (isi->pdata.has_emb_sync)
		cfg1 |= ISI_CFG1_EMB_SYNC;
	if (isi->pdata.full_mode)
		cfg1 |= ISI_CFG1_FULL_MODE;

	cfg1 |= ISI_CFG1_THMASK_BEATS_16;

	/* Enable PM and peripheral clock before operate isi registers */
	pm_runtime_get_sync(isi->dev);

	isi_writel(isi, ISI_CTRL, ISI_CTRL_DIS);
	isi_writel(isi, ISI_CFG1, cfg1);

	pm_runtime_put(isi->dev);
}

/* -----------------------------------------------------------------------*/
static int atmel_isi_parse_dt(struct atmel_isi *isi,
			struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct v4l2_fwnode_endpoint ep = { .bus_type = 0 };
	int err;

	/* Default settings for ISI */
	isi->pdata.full_mode = 1;
	isi->pdata.frate = ISI_CFG1_FRATE_CAPTURE_ALL;

	np = of_graph_get_next_endpoint(np, NULL);
	if (!np) {
		dev_err(&pdev->dev, "Could not find the endpoint\n");
		return -EINVAL;
	}

	err = v4l2_fwnode_endpoint_parse(of_fwnode_handle(np), &ep);
	of_node_put(np);
	if (err) {
		dev_err(&pdev->dev, "Could not parse the endpoint\n");
		return err;
	}

	switch (ep.bus.parallel.bus_width) {
	case 8:
		isi->pdata.data_width_flags = ISI_DATAWIDTH_8;
		break;
	case 10:
		isi->pdata.data_width_flags =
				ISI_DATAWIDTH_8 | ISI_DATAWIDTH_10;
		break;
	default:
		dev_err(&pdev->dev, "Unsupported bus width: %d\n",
				ep.bus.parallel.bus_width);
		return -EINVAL;
	}

	if (ep.bus.parallel.flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)
		isi->pdata.hsync_act_low = true;
	if (ep.bus.parallel.flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)
		isi->pdata.vsync_act_low = true;
	if (ep.bus.parallel.flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)
		isi->pdata.pclk_act_falling = true;

	if (ep.bus_type == V4L2_MBUS_BT656)
		isi->pdata.has_emb_sync = true;

	return 0;
}

static int isi_open(struct file *file)
{
	struct atmel_isi *isi = video_drvdata(file);
	struct v4l2_subdev *sd = isi->entity.subdev;
	int ret;

	if (mutex_lock_interruptible(&isi->lock))
		return -ERESTARTSYS;

	ret = v4l2_fh_open(file);
	if (ret < 0)
		goto unlock;

	if (!v4l2_fh_is_singular_file(file))
		goto fh_rel;

	ret = v4l2_subdev_call(sd, core, s_power, 1);
	if (ret < 0 && ret != -ENOIOCTLCMD)
		goto fh_rel;

	ret = isi_set_fmt(isi, &isi->fmt);
	if (ret)
		v4l2_subdev_call(sd, core, s_power, 0);
fh_rel:
	if (ret)
		v4l2_fh_release(file);
unlock:
	mutex_unlock(&isi->lock);
	return ret;
}

static int isi_release(struct file *file)
{
	struct atmel_isi *isi = video_drvdata(file);
	struct v4l2_subdev *sd = isi->entity.subdev;
	bool fh_singular;
	int ret;

	mutex_lock(&isi->lock);

	fh_singular = v4l2_fh_is_singular_file(file);

	ret = _vb2_fop_release(file, NULL);

	if (fh_singular)
		v4l2_subdev_call(sd, core, s_power, 0);

	mutex_unlock(&isi->lock);

	return ret;
}

static const struct v4l2_ioctl_ops isi_ioctl_ops = {
	.vidioc_querycap		= isi_querycap,

	.vidioc_try_fmt_vid_cap		= isi_try_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= isi_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= isi_s_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap	= isi_enum_fmt_vid_cap,

	.vidioc_enum_input		= isi_enum_input,
	.vidioc_g_input			= isi_g_input,
	.vidioc_s_input			= isi_s_input,

	.vidioc_g_parm			= isi_g_parm,
	.vidioc_s_parm			= isi_s_parm,
	.vidioc_enum_framesizes		= isi_enum_framesizes,
	.vidioc_enum_frameintervals	= isi_enum_frameintervals,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,

	.vidioc_log_status		= v4l2_ctrl_log_status,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

static const struct v4l2_file_operations isi_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= video_ioctl2,
	.open		= isi_open,
	.release	= isi_release,
	.poll		= vb2_fop_poll,
	.mmap		= vb2_fop_mmap,
	.read		= vb2_fop_read,
};

static int isi_set_default_fmt(struct atmel_isi *isi)
{
	struct v4l2_format f = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.fmt.pix = {
			.width		= VGA_WIDTH,
			.height		= VGA_HEIGHT,
			.field		= V4L2_FIELD_NONE,
			.pixelformat	= isi->user_formats[0]->fourcc,
		},
	};
	int ret;

	ret = isi_try_fmt(isi, &f, NULL);
	if (ret)
		return ret;
	isi->current_fmt = isi->user_formats[0];
	isi->fmt = f;
	return 0;
}

static const struct isi_format isi_formats[] = {
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.mbus_code = MEDIA_BUS_FMT_YUYV8_2X8,
		.bpp = 2,
		.swap = ISI_CFG2_YCC_SWAP_DEFAULT,
	}, {
		.fourcc = V4L2_PIX_FMT_YUYV,
		.mbus_code = MEDIA_BUS_FMT_YVYU8_2X8,
		.bpp = 2,
		.swap = ISI_CFG2_YCC_SWAP_MODE_1,
	}, {
		.fourcc = V4L2_PIX_FMT_YUYV,
		.mbus_code = MEDIA_BUS_FMT_UYVY8_2X8,
		.bpp = 2,
		.swap = ISI_CFG2_YCC_SWAP_MODE_2,
	}, {
		.fourcc = V4L2_PIX_FMT_YUYV,
		.mbus_code = MEDIA_BUS_FMT_VYUY8_2X8,
		.bpp = 2,
		.swap = ISI_CFG2_YCC_SWAP_MODE_3,
	}, {
		.fourcc = V4L2_PIX_FMT_RGB565,
		.mbus_code = MEDIA_BUS_FMT_YUYV8_2X8,
		.bpp = 2,
		.swap = ISI_CFG2_YCC_SWAP_MODE_2,
	}, {
		.fourcc = V4L2_PIX_FMT_RGB565,
		.mbus_code = MEDIA_BUS_FMT_YVYU8_2X8,
		.bpp = 2,
		.swap = ISI_CFG2_YCC_SWAP_MODE_3,
	}, {
		.fourcc = V4L2_PIX_FMT_RGB565,
		.mbus_code = MEDIA_BUS_FMT_UYVY8_2X8,
		.bpp = 2,
		.swap = ISI_CFG2_YCC_SWAP_DEFAULT,
	}, {
		.fourcc = V4L2_PIX_FMT_RGB565,
		.mbus_code = MEDIA_BUS_FMT_VYUY8_2X8,
		.bpp = 2,
		.swap = ISI_CFG2_YCC_SWAP_MODE_1,
	}, {
		.fourcc = V4L2_PIX_FMT_GREY,
		.mbus_code = MEDIA_BUS_FMT_Y10_1X10,
		.bpp = 1,
		.swap = ISI_CFG2_GS_MODE_2_PIXEL | ISI_CFG2_GRAYSCALE,
	}, {
		.fourcc = V4L2_PIX_FMT_Y16,
		.mbus_code = MEDIA_BUS_FMT_Y10_1X10,
		.bpp = 2,
		.swap = ISI_CFG2_GS_MODE_2_PIXEL | ISI_CFG2_GRAYSCALE,
	},
};

static int isi_formats_init(struct atmel_isi *isi)
{
	const struct isi_format *isi_fmts[ARRAY_SIZE(isi_formats)];
	unsigned int num_fmts = 0, i, j;
	struct v4l2_subdev *subdev = isi->entity.subdev;
	struct v4l2_subdev_mbus_code_enum mbus_code = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	while (!v4l2_subdev_call(subdev, pad, enum_mbus_code,
				 NULL, &mbus_code)) {
		for (i = 0; i < ARRAY_SIZE(isi_formats); i++) {
			if (isi_formats[i].mbus_code != mbus_code.code)
				continue;

			/* Code supported, have we got this fourcc yet? */
			for (j = 0; j < num_fmts; j++)
				if (isi_fmts[j]->fourcc == isi_formats[i].fourcc)
					/* Already available */
					break;
			if (j == num_fmts)
				/* new */
				isi_fmts[num_fmts++] = isi_formats + i;
		}
		mbus_code.index++;
	}

	if (!num_fmts)
		return -ENXIO;

	isi->num_user_formats = num_fmts;
	isi->user_formats = devm_kcalloc(isi->dev,
					 num_fmts, sizeof(struct isi_format *),
					 GFP_KERNEL);
	if (!isi->user_formats)
		return -ENOMEM;

	memcpy(isi->user_formats, isi_fmts,
	       num_fmts * sizeof(struct isi_format *));
	isi->current_fmt = isi->user_formats[0];

	return 0;
}

static int isi_graph_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct atmel_isi *isi = notifier_to_isi(notifier);
	int ret;

	isi->vdev->ctrl_handler = isi->entity.subdev->ctrl_handler;
	ret = isi_formats_init(isi);
	if (ret) {
		dev_err(isi->dev, "No supported mediabus format found\n");
		return ret;
	}
	isi_camera_set_bus_param(isi);

	ret = isi_set_default_fmt(isi);
	if (ret) {
		dev_err(isi->dev, "Could not set default format\n");
		return ret;
	}

	ret = video_register_device(isi->vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(isi->dev, "Failed to register video device\n");
		return ret;
	}

	dev_dbg(isi->dev, "Device registered as %s\n",
		video_device_node_name(isi->vdev));
	return 0;
}

static void isi_graph_notify_unbind(struct v4l2_async_notifier *notifier,
				     struct v4l2_subdev *sd,
				     struct v4l2_async_subdev *asd)
{
	struct atmel_isi *isi = notifier_to_isi(notifier);

	dev_dbg(isi->dev, "Removing %s\n", video_device_node_name(isi->vdev));

	/* Checks internally if vdev have been init or not */
	video_unregister_device(isi->vdev);
}

static int isi_graph_notify_bound(struct v4l2_async_notifier *notifier,
				   struct v4l2_subdev *subdev,
				   struct v4l2_async_subdev *asd)
{
	struct atmel_isi *isi = notifier_to_isi(notifier);

	dev_dbg(isi->dev, "subdev %s bound\n", subdev->name);

	isi->entity.subdev = subdev;

	return 0;
}

static const struct v4l2_async_notifier_operations isi_graph_notify_ops = {
	.bound = isi_graph_notify_bound,
	.unbind = isi_graph_notify_unbind,
	.complete = isi_graph_notify_complete,
};

static int isi_graph_parse(struct atmel_isi *isi, struct device_node *node)
{
	struct device_node *ep = NULL;
	struct device_node *remote;

	ep = of_graph_get_next_endpoint(node, ep);
	if (!ep)
		return -EINVAL;

	remote = of_graph_get_remote_port_parent(ep);
	of_node_put(ep);
	if (!remote)
		return -EINVAL;

	/* Remote node to connect */
	isi->entity.node = remote;
	isi->entity.asd.match_type = V4L2_ASYNC_MATCH_FWNODE;
	isi->entity.asd.match.fwnode = of_fwnode_handle(remote);
	return 0;
}

static int isi_graph_init(struct atmel_isi *isi)
{
	int ret;

	/* Parse the graph to extract a list of subdevice DT nodes. */
	ret = isi_graph_parse(isi, isi->dev->of_node);
	if (ret < 0) {
		dev_err(isi->dev, "Graph parsing failed\n");
		return ret;
	}

	v4l2_async_notifier_init(&isi->notifier);

	ret = v4l2_async_notifier_add_subdev(&isi->notifier, &isi->entity.asd);
	if (ret) {
		of_node_put(isi->entity.node);
		return ret;
	}

	isi->notifier.ops = &isi_graph_notify_ops;

	ret = v4l2_async_notifier_register(&isi->v4l2_dev, &isi->notifier);
	if (ret < 0) {
		dev_err(isi->dev, "Notifier registration failed\n");
		v4l2_async_notifier_cleanup(&isi->notifier);
		return ret;
	}

	return 0;
}


static int atmel_isi_probe(struct platform_device *pdev)
{
	int irq;
	struct atmel_isi *isi;
	struct vb2_queue *q;
	struct resource *regs;
	int ret, i;

	isi = devm_kzalloc(&pdev->dev, sizeof(struct atmel_isi), GFP_KERNEL);
	if (!isi)
		return -ENOMEM;

	isi->pclk = devm_clk_get(&pdev->dev, "isi_clk");
	if (IS_ERR(isi->pclk))
		return PTR_ERR(isi->pclk);

	ret = atmel_isi_parse_dt(isi, pdev);
	if (ret)
		return ret;

	isi->active = NULL;
	isi->dev = &pdev->dev;
	mutex_init(&isi->lock);
	spin_lock_init(&isi->irqlock);
	INIT_LIST_HEAD(&isi->video_buffer_list);
	INIT_LIST_HEAD(&isi->dma_desc_head);

	q = &isi->queue;

	/* Initialize the top-level structure */
	ret = v4l2_device_register(&pdev->dev, &isi->v4l2_dev);
	if (ret)
		return ret;

	isi->vdev = video_device_alloc();
	if (!isi->vdev) {
		ret = -ENOMEM;
		goto err_vdev_alloc;
	}

	/* video node */
	isi->vdev->fops = &isi_fops;
	isi->vdev->v4l2_dev = &isi->v4l2_dev;
	isi->vdev->queue = &isi->queue;
	strscpy(isi->vdev->name, KBUILD_MODNAME, sizeof(isi->vdev->name));
	isi->vdev->release = video_device_release;
	isi->vdev->ioctl_ops = &isi_ioctl_ops;
	isi->vdev->lock = &isi->lock;
	isi->vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
		V4L2_CAP_READWRITE;
	video_set_drvdata(isi->vdev, isi);

	/* buffer queue */
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_READ | VB2_DMABUF;
	q->lock = &isi->lock;
	q->drv_priv = isi;
	q->buf_struct_size = sizeof(struct frame_buffer);
	q->ops = &isi_video_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_buffers_needed = 2;
	q->dev = &pdev->dev;

	ret = vb2_queue_init(q);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to initialize VB2 queue\n");
		goto err_vb2_queue;
	}
	isi->p_fb_descriptors = dma_alloc_coherent(&pdev->dev,
				sizeof(struct fbd) * VIDEO_MAX_FRAME,
				&isi->fb_descriptors_phys,
				GFP_KERNEL);
	if (!isi->p_fb_descriptors) {
		dev_err(&pdev->dev, "Can't allocate descriptors!\n");
		ret = -ENOMEM;
		goto err_dma_alloc;
	}

	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		isi->dma_desc[i].p_fbd = isi->p_fb_descriptors + i;
		isi->dma_desc[i].fbd_phys = isi->fb_descriptors_phys +
					i * sizeof(struct fbd);
		list_add(&isi->dma_desc[i].list, &isi->dma_desc_head);
	}

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	isi->regs = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(isi->regs)) {
		ret = PTR_ERR(isi->regs);
		goto err_ioremap;
	}

	if (isi->pdata.data_width_flags & ISI_DATAWIDTH_8)
		isi->width_flags = 1 << 7;
	if (isi->pdata.data_width_flags & ISI_DATAWIDTH_10)
		isi->width_flags |= 1 << 9;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto err_req_irq;
	}

	ret = devm_request_irq(&pdev->dev, irq, isi_interrupt, 0, "isi", isi);
	if (ret) {
		dev_err(&pdev->dev, "Unable to request irq %d\n", irq);
		goto err_req_irq;
	}
	isi->irq = irq;

	ret = isi_graph_init(isi);
	if (ret < 0)
		goto err_req_irq;

	pm_suspend_ignore_children(&pdev->dev, true);
	pm_runtime_enable(&pdev->dev);
	platform_set_drvdata(pdev, isi);
	return 0;

err_req_irq:
err_ioremap:
	dma_free_coherent(&pdev->dev,
			sizeof(struct fbd) * VIDEO_MAX_FRAME,
			isi->p_fb_descriptors,
			isi->fb_descriptors_phys);
err_dma_alloc:
err_vb2_queue:
	video_device_release(isi->vdev);
err_vdev_alloc:
	v4l2_device_unregister(&isi->v4l2_dev);

	return ret;
}

static int atmel_isi_remove(struct platform_device *pdev)
{
	struct atmel_isi *isi = platform_get_drvdata(pdev);

	dma_free_coherent(&pdev->dev,
			sizeof(struct fbd) * VIDEO_MAX_FRAME,
			isi->p_fb_descriptors,
			isi->fb_descriptors_phys);
	pm_runtime_disable(&pdev->dev);
	v4l2_async_notifier_unregister(&isi->notifier);
	v4l2_async_notifier_cleanup(&isi->notifier);
	v4l2_device_unregister(&isi->v4l2_dev);

	return 0;
}

#ifdef CONFIG_PM
static int atmel_isi_runtime_suspend(struct device *dev)
{
	struct atmel_isi *isi = dev_get_drvdata(dev);

	clk_disable_unprepare(isi->pclk);

	return 0;
}
static int atmel_isi_runtime_resume(struct device *dev)
{
	struct atmel_isi *isi = dev_get_drvdata(dev);

	return clk_prepare_enable(isi->pclk);
}
#endif /* CONFIG_PM */

static const struct dev_pm_ops atmel_isi_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(atmel_isi_runtime_suspend,
				atmel_isi_runtime_resume, NULL)
};

static const struct of_device_id atmel_isi_of_match[] = {
	{ .compatible = "atmel,at91sam9g45-isi" },
	{ }
};
MODULE_DEVICE_TABLE(of, atmel_isi_of_match);

static struct platform_driver atmel_isi_driver = {
	.driver		= {
		.name = "atmel_isi",
		.of_match_table = of_match_ptr(atmel_isi_of_match),
		.pm	= &atmel_isi_dev_pm_ops,
	},
	.probe		= atmel_isi_probe,
	.remove		= atmel_isi_remove,
};

module_platform_driver(atmel_isi_driver);

MODULE_AUTHOR("Josh Wu <josh.wu@atmel.com>");
MODULE_DESCRIPTION("The V4L2 driver for Atmel Linux");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("video");
