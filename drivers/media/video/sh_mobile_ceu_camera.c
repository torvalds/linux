/*
 * V4L2 Driver for SuperH Mobile CEU interface
 *
 * Copyright (C) 2008 Magnus Damm
 *
 * Based on V4L2 Driver for PXA camera host - "pxa_camera.c",
 *
 * Copyright (C) 2006, Sascha Hauer, Pengutronix
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/time.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>

#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/soc_camera.h>
#include <media/sh_mobile_ceu.h>
#include <media/videobuf-dma-contig.h>

/* register offsets for sh7722 / sh7723 */

#define CAPSR  0x00
#define CAPCR  0x04
#define CAMCR  0x08
#define CMCYR  0x0c
#define CAMOR  0x10
#define CAPWR  0x14
#define CAIFR  0x18
#define CSTCR  0x20 /* not on sh7723 */
#define CSECR  0x24 /* not on sh7723 */
#define CRCNTR 0x28
#define CRCMPR 0x2c
#define CFLCR  0x30
#define CFSZR  0x34
#define CDWDR  0x38
#define CDAYR  0x3c
#define CDACR  0x40
#define CDBYR  0x44
#define CDBCR  0x48
#define CBDSR  0x4c
#define CFWCR  0x5c
#define CLFCR  0x60
#define CDOCR  0x64
#define CDDCR  0x68
#define CDDAR  0x6c
#define CEIER  0x70
#define CETCR  0x74
#define CSTSR  0x7c
#define CSRTR  0x80
#define CDSSR  0x84
#define CDAYR2 0x90
#define CDACR2 0x94
#define CDBYR2 0x98
#define CDBCR2 0x9c

static DEFINE_MUTEX(camera_lock);

/* per video frame buffer */
struct sh_mobile_ceu_buffer {
	struct videobuf_buffer vb; /* v4l buffer must be first */
	const struct soc_camera_data_format *fmt;
};

struct sh_mobile_ceu_dev {
	struct device *dev;
	struct soc_camera_host ici;
	struct soc_camera_device *icd;

	unsigned int irq;
	void __iomem *base;
	unsigned long video_limit;

	/* lock used to protect videobuf */
	spinlock_t lock;
	struct list_head capture;
	struct videobuf_buffer *active;

	struct sh_mobile_ceu_info *pdata;
};

static void ceu_write(struct sh_mobile_ceu_dev *priv,
		      unsigned long reg_offs, unsigned long data)
{
	iowrite32(data, priv->base + reg_offs);
}

static unsigned long ceu_read(struct sh_mobile_ceu_dev *priv,
			      unsigned long reg_offs)
{
	return ioread32(priv->base + reg_offs);
}

/*
 *  Videobuf operations
 */
static int sh_mobile_ceu_videobuf_setup(struct videobuf_queue *vq,
					unsigned int *count,
					unsigned int *size)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	int bytes_per_pixel = (icd->current_fmt->depth + 7) >> 3;

	*size = PAGE_ALIGN(icd->width * icd->height * bytes_per_pixel);

	if (0 == *count)
		*count = 2;

	if (pcdev->video_limit) {
		while (*size * *count > pcdev->video_limit)
			(*count)--;
	}

	dev_dbg(&icd->dev, "count=%d, size=%d\n", *count, *size);

	return 0;
}

static void free_buffer(struct videobuf_queue *vq,
			struct sh_mobile_ceu_buffer *buf)
{
	struct soc_camera_device *icd = vq->priv_data;

	dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %zd\n", __func__,
		&buf->vb, buf->vb.baddr, buf->vb.bsize);

	if (in_interrupt())
		BUG();

	videobuf_dma_contig_free(vq, &buf->vb);
	dev_dbg(&icd->dev, "%s freed\n", __func__);
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

static void sh_mobile_ceu_capture(struct sh_mobile_ceu_dev *pcdev)
{
	ceu_write(pcdev, CEIER, ceu_read(pcdev, CEIER) & ~1);
	ceu_write(pcdev, CETCR, ~ceu_read(pcdev, CETCR) & 0x0317f313);
	ceu_write(pcdev, CEIER, ceu_read(pcdev, CEIER) | 1);

	ceu_write(pcdev, CAPCR, ceu_read(pcdev, CAPCR) & ~0x10000);

	ceu_write(pcdev, CETCR, 0x0317f313 ^ 0x10);

	if (pcdev->active) {
		ceu_write(pcdev, CDAYR, videobuf_to_dma_contig(pcdev->active));
		ceu_write(pcdev, CAPSR, 0x1); /* start capture */
	}
}

static int sh_mobile_ceu_videobuf_prepare(struct videobuf_queue *vq,
					  struct videobuf_buffer *vb,
					  enum v4l2_field field)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct sh_mobile_ceu_buffer *buf;
	int ret;

	buf = container_of(vb, struct sh_mobile_ceu_buffer, vb);

	dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %zd\n", __func__,
		vb, vb->baddr, vb->bsize);

	/* Added list head initialization on alloc */
	WARN_ON(!list_empty(&vb->queue));

#ifdef DEBUG
	/* This can be useful if you want to see if we actually fill
	 * the buffer with something */
	memset((void *)vb->baddr, 0xaa, vb->bsize);
#endif

	BUG_ON(NULL == icd->current_fmt);

	if (buf->fmt	!= icd->current_fmt ||
	    vb->width	!= icd->width ||
	    vb->height	!= icd->height ||
	    vb->field	!= field) {
		buf->fmt	= icd->current_fmt;
		vb->width	= icd->width;
		vb->height	= icd->height;
		vb->field	= field;
		vb->state	= VIDEOBUF_NEEDS_INIT;
	}

	vb->size = vb->width * vb->height * ((buf->fmt->depth + 7) >> 3);
	if (0 != vb->baddr && vb->bsize < vb->size) {
		ret = -EINVAL;
		goto out;
	}

	if (vb->state == VIDEOBUF_NEEDS_INIT) {
		ret = videobuf_iolock(vq, vb, NULL);
		if (ret)
			goto fail;
		vb->state = VIDEOBUF_PREPARED;
	}

	return 0;
fail:
	free_buffer(vq, buf);
out:
	return ret;
}

static void sh_mobile_ceu_videobuf_queue(struct videobuf_queue *vq,
					 struct videobuf_buffer *vb)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	unsigned long flags;

	dev_dbg(&icd->dev, "%s (vb=0x%p) 0x%08lx %zd\n", __func__,
		vb, vb->baddr, vb->bsize);

	vb->state = VIDEOBUF_ACTIVE;
	spin_lock_irqsave(&pcdev->lock, flags);
	list_add_tail(&vb->queue, &pcdev->capture);

	if (!pcdev->active) {
		pcdev->active = vb;
		sh_mobile_ceu_capture(pcdev);
	}

	spin_unlock_irqrestore(&pcdev->lock, flags);
}

static void sh_mobile_ceu_videobuf_release(struct videobuf_queue *vq,
					   struct videobuf_buffer *vb)
{
	free_buffer(vq, container_of(vb, struct sh_mobile_ceu_buffer, vb));
}

static struct videobuf_queue_ops sh_mobile_ceu_videobuf_ops = {
	.buf_setup      = sh_mobile_ceu_videobuf_setup,
	.buf_prepare    = sh_mobile_ceu_videobuf_prepare,
	.buf_queue      = sh_mobile_ceu_videobuf_queue,
	.buf_release    = sh_mobile_ceu_videobuf_release,
};

static irqreturn_t sh_mobile_ceu_irq(int irq, void *data)
{
	struct sh_mobile_ceu_dev *pcdev = data;
	struct videobuf_buffer *vb;
	unsigned long flags;

	spin_lock_irqsave(&pcdev->lock, flags);

	vb = pcdev->active;
	list_del_init(&vb->queue);

	if (!list_empty(&pcdev->capture))
		pcdev->active = list_entry(pcdev->capture.next,
					   struct videobuf_buffer, queue);
	else
		pcdev->active = NULL;

	sh_mobile_ceu_capture(pcdev);

	vb->state = VIDEOBUF_DONE;
	do_gettimeofday(&vb->ts);
	vb->field_count++;
	wake_up(&vb->done);
	spin_unlock_irqrestore(&pcdev->lock, flags);

	return IRQ_HANDLED;
}

static int sh_mobile_ceu_add_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	int ret = -EBUSY;

	mutex_lock(&camera_lock);

	if (pcdev->icd)
		goto err;

	dev_info(&icd->dev,
		 "SuperH Mobile CEU driver attached to camera %d\n",
		 icd->devnum);

	if (pcdev->pdata->enable_camera)
		pcdev->pdata->enable_camera();

	ret = icd->ops->init(icd);
	if (ret)
		goto err;

	ceu_write(pcdev, CAPSR, 1 << 16); /* reset */
	while (ceu_read(pcdev, CSTSR) & 1)
		msleep(1);

	pcdev->icd = icd;
err:
	mutex_unlock(&camera_lock);

	return ret;
}

static void sh_mobile_ceu_remove_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;

	BUG_ON(icd != pcdev->icd);

	/* disable capture, disable interrupts */
	ceu_write(pcdev, CEIER, 0);
	ceu_write(pcdev, CAPSR, 1 << 16); /* reset */
	icd->ops->release(icd);
	if (pcdev->pdata->disable_camera)
		pcdev->pdata->disable_camera();

	dev_info(&icd->dev,
		 "SuperH Mobile CEU driver detached from camera %d\n",
		 icd->devnum);

	pcdev->icd = NULL;
}

static int sh_mobile_ceu_set_bus_param(struct soc_camera_device *icd,
				       __u32 pixfmt)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	int ret, buswidth, width, cfszr_width, cdwdr_width;
	unsigned long camera_flags, common_flags, value;

	camera_flags = icd->ops->query_bus_param(icd);
	common_flags = soc_camera_bus_param_compatible(camera_flags,
						       pcdev->pdata->flags);
	if (!common_flags)
		return -EINVAL;

	ret = icd->ops->set_bus_param(icd, common_flags);
	if (ret < 0)
		return ret;

	switch (common_flags & SOCAM_DATAWIDTH_MASK) {
	case SOCAM_DATAWIDTH_8:
		buswidth = 8;
		break;
	case SOCAM_DATAWIDTH_16:
		buswidth = 16;
		break;
	default:
		return -EINVAL;
	}

	ceu_write(pcdev, CRCNTR, 0);
	ceu_write(pcdev, CRCMPR, 0);

	value = 0x00000010;
	value |= (common_flags & SOCAM_VSYNC_ACTIVE_LOW) ? (1 << 1) : 0;
	value |= (common_flags & SOCAM_HSYNC_ACTIVE_LOW) ? (1 << 0) : 0;
	value |= (buswidth == 16) ? (1 << 12) : 0;
	ceu_write(pcdev, CAMCR, value);

	ceu_write(pcdev, CAPCR, 0x00300000);
	ceu_write(pcdev, CAIFR, 0);

	mdelay(1);

	width = icd->width * (icd->current_fmt->depth / 8);
	width = (buswidth == 16) ? width / 2 : width;
	cfszr_width = (buswidth == 8) ? width / 2 : width;
	cdwdr_width = (buswidth == 16) ? width * 2 : width;

	ceu_write(pcdev, CAMOR, 0);
	ceu_write(pcdev, CAPWR, (icd->height << 16) | width);
	ceu_write(pcdev, CFLCR, 0); /* data fetch mode - no scaling */
	ceu_write(pcdev, CFSZR, (icd->height << 16) | cfszr_width);
	ceu_write(pcdev, CLFCR, 0); /* data fetch mode - no lowpass filter */
	ceu_write(pcdev, CDOCR, 0x00000016);

	ceu_write(pcdev, CDWDR, cdwdr_width);
	ceu_write(pcdev, CFWCR, 0); /* keep "datafetch firewall" disabled */

	/* not in bundle mode: skip CBDSR, CDAYR2, CDACR2, CDBYR2, CDBCR2 */
	/* in data fetch mode: no need for CDACR, CDBYR, CDBCR */

	return 0;
}

static int sh_mobile_ceu_try_bus_param(struct soc_camera_device *icd,
				       __u32 pixfmt)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;
	unsigned long camera_flags, common_flags;

	camera_flags = icd->ops->query_bus_param(icd);
	common_flags = soc_camera_bus_param_compatible(camera_flags,
						       pcdev->pdata->flags);
	if (!common_flags)
		return -EINVAL;

	return 0;
}

static int sh_mobile_ceu_set_fmt_cap(struct soc_camera_device *icd,
				     __u32 pixfmt, struct v4l2_rect *rect)
{
	return icd->ops->set_fmt_cap(icd, pixfmt, rect);
}

static int sh_mobile_ceu_try_fmt_cap(struct soc_camera_device *icd,
				     struct v4l2_format *f)
{
	/* FIXME: calculate using depth and bus width */

	if (f->fmt.pix.height < 4)
		f->fmt.pix.height = 4;
	if (f->fmt.pix.height > 1920)
		f->fmt.pix.height = 1920;
	if (f->fmt.pix.width < 2)
		f->fmt.pix.width = 2;
	if (f->fmt.pix.width > 2560)
		f->fmt.pix.width = 2560;
	f->fmt.pix.width &= ~0x01;
	f->fmt.pix.height &= ~0x03;

	/* limit to sensor capabilities */
	return icd->ops->try_fmt_cap(icd, f);
}

static int sh_mobile_ceu_reqbufs(struct soc_camera_file *icf,
				 struct v4l2_requestbuffers *p)
{
	int i;

	/* This is for locking debugging only. I removed spinlocks and now I
	 * check whether .prepare is ever called on a linked buffer, or whether
	 * a dma IRQ can occur for an in-work or unlinked buffer. Until now
	 * it hadn't triggered */
	for (i = 0; i < p->count; i++) {
		struct sh_mobile_ceu_buffer *buf;

		buf = container_of(icf->vb_vidq.bufs[i],
				   struct sh_mobile_ceu_buffer, vb);
		INIT_LIST_HEAD(&buf->vb.queue);
	}

	return 0;
}

static unsigned int sh_mobile_ceu_poll(struct file *file, poll_table *pt)
{
	struct soc_camera_file *icf = file->private_data;
	struct sh_mobile_ceu_buffer *buf;

	buf = list_entry(icf->vb_vidq.stream.next,
			 struct sh_mobile_ceu_buffer, vb.stream);

	poll_wait(file, &buf->vb.done, pt);

	if (buf->vb.state == VIDEOBUF_DONE ||
	    buf->vb.state == VIDEOBUF_ERROR)
		return POLLIN|POLLRDNORM;

	return 0;
}

static int sh_mobile_ceu_querycap(struct soc_camera_host *ici,
				  struct v4l2_capability *cap)
{
	strlcpy(cap->card, "SuperH_Mobile_CEU", sizeof(cap->card));
	cap->version = KERNEL_VERSION(0, 0, 5);
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	return 0;
}

static void sh_mobile_ceu_init_videobuf(struct videobuf_queue *q,
					struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->dev.parent);
	struct sh_mobile_ceu_dev *pcdev = ici->priv;

	videobuf_queue_dma_contig_init(q,
				       &sh_mobile_ceu_videobuf_ops,
				       &ici->dev, &pcdev->lock,
				       V4L2_BUF_TYPE_VIDEO_CAPTURE,
				       V4L2_FIELD_NONE,
				       sizeof(struct sh_mobile_ceu_buffer),
				       icd);
}

static struct soc_camera_host_ops sh_mobile_ceu_host_ops = {
	.owner		= THIS_MODULE,
	.add		= sh_mobile_ceu_add_device,
	.remove		= sh_mobile_ceu_remove_device,
	.set_fmt_cap	= sh_mobile_ceu_set_fmt_cap,
	.try_fmt_cap	= sh_mobile_ceu_try_fmt_cap,
	.reqbufs	= sh_mobile_ceu_reqbufs,
	.poll		= sh_mobile_ceu_poll,
	.querycap	= sh_mobile_ceu_querycap,
	.try_bus_param	= sh_mobile_ceu_try_bus_param,
	.set_bus_param	= sh_mobile_ceu_set_bus_param,
	.init_videobuf	= sh_mobile_ceu_init_videobuf,
};

static int sh_mobile_ceu_probe(struct platform_device *pdev)
{
	struct sh_mobile_ceu_dev *pcdev;
	struct resource *res;
	void __iomem *base;
	unsigned int irq;
	int err = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!res || !irq) {
		dev_err(&pdev->dev, "Not enough CEU platform resources.\n");
		err = -ENODEV;
		goto exit;
	}

	pcdev = kzalloc(sizeof(*pcdev), GFP_KERNEL);
	if (!pcdev) {
		dev_err(&pdev->dev, "Could not allocate pcdev\n");
		err = -ENOMEM;
		goto exit;
	}

	platform_set_drvdata(pdev, pcdev);
	INIT_LIST_HEAD(&pcdev->capture);
	spin_lock_init(&pcdev->lock);

	pcdev->pdata = pdev->dev.platform_data;
	if (!pcdev->pdata) {
		err = -EINVAL;
		dev_err(&pdev->dev, "CEU platform data not set.\n");
		goto exit_kfree;
	}

	base = ioremap_nocache(res->start, res->end - res->start + 1);
	if (!base) {
		err = -ENXIO;
		dev_err(&pdev->dev, "Unable to ioremap CEU registers.\n");
		goto exit_kfree;
	}

	pcdev->irq = irq;
	pcdev->base = base;
	pcdev->video_limit = 0; /* only enabled if second resource exists */
	pcdev->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res) {
		err = dma_declare_coherent_memory(&pdev->dev, res->start,
						  res->start,
						  (res->end - res->start) + 1,
						  DMA_MEMORY_MAP |
						  DMA_MEMORY_EXCLUSIVE);
		if (!err) {
			dev_err(&pdev->dev, "Unable to declare CEU memory.\n");
			err = -ENXIO;
			goto exit_iounmap;
		}

		pcdev->video_limit = (res->end - res->start) + 1;
	}

	/* request irq */
	err = request_irq(pcdev->irq, sh_mobile_ceu_irq, IRQF_DISABLED,
			  pdev->dev.bus_id, pcdev);
	if (err) {
		dev_err(&pdev->dev, "Unable to register CEU interrupt.\n");
		goto exit_release_mem;
	}

	pcdev->ici.priv = pcdev;
	pcdev->ici.dev.parent = &pdev->dev;
	pcdev->ici.nr = pdev->id;
	pcdev->ici.drv_name = pdev->dev.bus_id,
	pcdev->ici.ops = &sh_mobile_ceu_host_ops,

	err = soc_camera_host_register(&pcdev->ici);
	if (err)
		goto exit_free_irq;

	return 0;

exit_free_irq:
	free_irq(pcdev->irq, pcdev);
exit_release_mem:
	if (platform_get_resource(pdev, IORESOURCE_MEM, 1))
		dma_release_declared_memory(&pdev->dev);
exit_iounmap:
	iounmap(base);
exit_kfree:
	kfree(pcdev);
exit:
	return err;
}

static int sh_mobile_ceu_remove(struct platform_device *pdev)
{
	struct sh_mobile_ceu_dev *pcdev = platform_get_drvdata(pdev);

	soc_camera_host_unregister(&pcdev->ici);
	free_irq(pcdev->irq, pcdev);
	if (platform_get_resource(pdev, IORESOURCE_MEM, 1))
		dma_release_declared_memory(&pdev->dev);
	iounmap(pcdev->base);
	kfree(pcdev);
	return 0;
}

static struct platform_driver sh_mobile_ceu_driver = {
	.driver 	= {
		.name	= "sh_mobile_ceu",
	},
	.probe		= sh_mobile_ceu_probe,
	.remove		= sh_mobile_ceu_remove,
};

static int __init sh_mobile_ceu_init(void)
{
	return platform_driver_register(&sh_mobile_ceu_driver);
}

static void __exit sh_mobile_ceu_exit(void)
{
	platform_driver_unregister(&sh_mobile_ceu_driver);
}

module_init(sh_mobile_ceu_init);
module_exit(sh_mobile_ceu_exit);

MODULE_DESCRIPTION("SuperH Mobile CEU driver");
MODULE_AUTHOR("Magnus Damm");
MODULE_LICENSE("GPL");
