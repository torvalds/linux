// SPDX-License-Identifier: GPL-2.0
/*
 * V4L2 Driver for Renesas Capture Engine Unit (CEU) interface
 * Copyright (C) 2017-2018 Jacopo Mondi <jacopo+renesas@jmondi.org>
 *
 * Based on soc-camera driver "soc_camera/sh_mobile_ceu_camera.c"
 * Copyright (C) 2008 Magnus Damm
 *
 * Based on V4L2 Driver for PXA camera host - "pxa_camera.c",
 * Copyright (C) 2006, Sascha Hauer, Pengutronix
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/videodev2.h>

#include <media/v4l2-async.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mediabus.h>
#include <media/videobuf2-dma-contig.h>

#include <media/drv-intf/renesas-ceu.h>

#define DRIVER_NAME	"renesas-ceu"

/* CEU registers offsets and masks. */
#define CEU_CAPSR	0x00 /* Capture start register			*/
#define CEU_CAPCR	0x04 /* Capture control register		*/
#define CEU_CAMCR	0x08 /* Capture interface control register	*/
#define CEU_CAMOR	0x10 /* Capture interface offset register	*/
#define CEU_CAPWR	0x14 /* Capture interface width register	*/
#define CEU_CAIFR	0x18 /* Capture interface input format register */
#define CEU_CRCNTR	0x28 /* CEU register control register		*/
#define CEU_CRCMPR	0x2c /* CEU register forcible control register	*/
#define CEU_CFLCR	0x30 /* Capture filter control register		*/
#define CEU_CFSZR	0x34 /* Capture filter size clip register	*/
#define CEU_CDWDR	0x38 /* Capture destination width register	*/
#define CEU_CDAYR	0x3c /* Capture data address Y register		*/
#define CEU_CDACR	0x40 /* Capture data address C register		*/
#define CEU_CFWCR	0x5c /* Firewall operation control register	*/
#define CEU_CDOCR	0x64 /* Capture data output control register	*/
#define CEU_CEIER	0x70 /* Capture event interrupt enable register	*/
#define CEU_CETCR	0x74 /* Capture event flag clear register	*/
#define CEU_CSTSR	0x7c /* Capture status register			*/
#define CEU_CSRTR	0x80 /* Capture software reset register		*/

/* Data synchronous fetch mode. */
#define CEU_CAMCR_JPEG			BIT(4)

/* Input components ordering: CEU_CAMCR.DTARY field. */
#define CEU_CAMCR_DTARY_8_UYVY		(0x00 << 8)
#define CEU_CAMCR_DTARY_8_VYUY		(0x01 << 8)
#define CEU_CAMCR_DTARY_8_YUYV		(0x02 << 8)
#define CEU_CAMCR_DTARY_8_YVYU		(0x03 << 8)
/* TODO: input components ordering for 16 bits input. */

/* Bus transfer MTU. */
#define CEU_CAPCR_BUS_WIDTH256		(0x3 << 20)

/* Bus width configuration. */
#define CEU_CAMCR_DTIF_16BITS		BIT(12)

/* No downsampling to planar YUV420 in image fetch mode. */
#define CEU_CDOCR_NO_DOWSAMPLE		BIT(4)

/* Swap all input data in 8-bit, 16-bits and 32-bits units (Figure 46.45). */
#define CEU_CDOCR_SWAP_ENDIANNESS	(7)

/* Capture reset and enable bits. */
#define CEU_CAPSR_CPKIL			BIT(16)
#define CEU_CAPSR_CE			BIT(0)

/* CEU operating flag bit. */
#define CEU_CAPCR_CTNCP			BIT(16)
#define CEU_CSTRST_CPTON		BIT(0)

/* Platform specific IRQ source flags. */
#define CEU_CETCR_ALL_IRQS_RZ		0x397f313
#define CEU_CETCR_ALL_IRQS_SH4		0x3d7f313

/* Prohibited register access interrupt bit. */
#define CEU_CETCR_IGRW			BIT(4)
/* One-frame capture end interrupt. */
#define CEU_CEIER_CPE			BIT(0)
/* VBP error. */
#define CEU_CEIER_VBP			BIT(20)
#define CEU_CEIER_MASK			(CEU_CEIER_CPE | CEU_CEIER_VBP)

#define CEU_MAX_WIDTH	2560
#define CEU_MAX_HEIGHT	1920
#define CEU_MAX_BPL	8188
#define CEU_W_MAX(w)	((w) < CEU_MAX_WIDTH ? (w) : CEU_MAX_WIDTH)
#define CEU_H_MAX(h)	((h) < CEU_MAX_HEIGHT ? (h) : CEU_MAX_HEIGHT)

/*
 * ceu_bus_fmt - describe a 8-bits yuyv format the sensor can produce
 *
 * @mbus_code: bus format code
 * @fmt_order: CEU_CAMCR.DTARY ordering of input components (Y, Cb, Cr)
 * @fmt_order_swap: swapped CEU_CAMCR.DTARY ordering of input components
 *		    (Y, Cr, Cb)
 * @swapped: does Cr appear before Cb?
 * @bps: number of bits sent over bus for each sample
 * @bpp: number of bits per pixels unit
 */
struct ceu_mbus_fmt {
	u32	mbus_code;
	u32	fmt_order;
	u32	fmt_order_swap;
	bool	swapped;
	u8	bps;
	u8	bpp;
};

/*
 * ceu_buffer - Link vb2 buffer to the list of available buffers.
 */
struct ceu_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head queue;
};

static inline struct ceu_buffer *vb2_to_ceu(struct vb2_v4l2_buffer *vbuf)
{
	return container_of(vbuf, struct ceu_buffer, vb);
}

/*
 * ceu_subdev - Wraps v4l2 sub-device and provides async subdevice.
 */
struct ceu_subdev {
	struct v4l2_async_subdev asd;
	struct v4l2_subdev *v4l2_sd;

	/* per-subdevice mbus configuration options */
	unsigned int mbus_flags;
	struct ceu_mbus_fmt mbus_fmt;
};

static struct ceu_subdev *to_ceu_subdev(struct v4l2_async_subdev *asd)
{
	return container_of(asd, struct ceu_subdev, asd);
}

/*
 * ceu_device - CEU device instance
 */
struct ceu_device {
	struct device		*dev;
	struct video_device	vdev;
	struct v4l2_device	v4l2_dev;

	/* subdevices descriptors */
	struct ceu_subdev	**subdevs;
	/* the subdevice currently in use */
	struct ceu_subdev	*sd;
	unsigned int		sd_index;
	unsigned int		num_sd;

	/* platform specific mask with all IRQ sources flagged */
	u32			irq_mask;

	/* currently configured field and pixel format */
	enum v4l2_field	field;
	struct v4l2_pix_format_mplane v4l2_pix;

	/* async subdev notification helpers */
	struct v4l2_async_notifier notifier;

	/* vb2 queue, capture buffer list and active buffer pointer */
	struct vb2_queue	vb2_vq;
	struct list_head	capture;
	struct vb2_v4l2_buffer	*active;
	unsigned int		sequence;

	/* mlock - lock access to interface reset and vb2 queue */
	struct mutex	mlock;

	/* lock - lock access to capture buffer queue and active buffer */
	spinlock_t	lock;

	/* base - CEU memory base address */
	void __iomem	*base;
};

static inline struct ceu_device *v4l2_to_ceu(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct ceu_device, v4l2_dev);
}

/* --- CEU memory output formats --- */

/*
 * ceu_fmt - describe a memory output format supported by CEU interface.
 *
 * @fourcc: memory layout fourcc format code
 * @bpp: number of bits for each pixel stored in memory
 */
struct ceu_fmt {
	u32	fourcc;
	u32	bpp;
};

/*
 * ceu_format_list - List of supported memory output formats
 *
 * If sensor provides any YUYV bus format, all the following planar memory
 * formats are available thanks to CEU re-ordering and sub-sampling
 * capabilities.
 */
static const struct ceu_fmt ceu_fmt_list[] = {
	{
		.fourcc	= V4L2_PIX_FMT_NV16,
		.bpp	= 16,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV61,
		.bpp	= 16,
	},
	{
		.fourcc	= V4L2_PIX_FMT_NV12,
		.bpp	= 12,
	},
	{
		.fourcc	= V4L2_PIX_FMT_NV21,
		.bpp	= 12,
	},
	{
		.fourcc	= V4L2_PIX_FMT_YUYV,
		.bpp	= 16,
	},
	{
		.fourcc	= V4L2_PIX_FMT_UYVY,
		.bpp	= 16,
	},
	{
		.fourcc	= V4L2_PIX_FMT_YVYU,
		.bpp	= 16,
	},
	{
		.fourcc	= V4L2_PIX_FMT_VYUY,
		.bpp	= 16,
	},
};

static const struct ceu_fmt *get_ceu_fmt_from_fourcc(unsigned int fourcc)
{
	const struct ceu_fmt *fmt = &ceu_fmt_list[0];
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ceu_fmt_list); i++, fmt++)
		if (fmt->fourcc == fourcc)
			return fmt;

	return NULL;
}

static bool ceu_fmt_mplane(struct v4l2_pix_format_mplane *pix)
{
	switch (pix->pixelformat) {
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_VYUY:
		return false;
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		return true;
	default:
		return false;
	}
}

/* --- CEU HW operations --- */

static void ceu_write(struct ceu_device *priv, unsigned int reg_offs, u32 data)
{
	iowrite32(data, priv->base + reg_offs);
}

static u32 ceu_read(struct ceu_device *priv, unsigned int reg_offs)
{
	return ioread32(priv->base + reg_offs);
}

/*
 * ceu_soft_reset() - Software reset the CEU interface.
 * @ceu_device: CEU device.
 *
 * Returns 0 for success, -EIO for error.
 */
static int ceu_soft_reset(struct ceu_device *ceudev)
{
	unsigned int i;

	ceu_write(ceudev, CEU_CAPSR, CEU_CAPSR_CPKIL);

	for (i = 0; i < 100; i++) {
		if (!(ceu_read(ceudev, CEU_CSTSR) & CEU_CSTRST_CPTON))
			break;
		udelay(1);
	}

	if (i == 100) {
		dev_err(ceudev->dev, "soft reset time out\n");
		return -EIO;
	}

	for (i = 0; i < 100; i++) {
		if (!(ceu_read(ceudev, CEU_CAPSR) & CEU_CAPSR_CPKIL))
			return 0;
		udelay(1);
	}

	/* If we get here, CEU has not reset properly. */
	return -EIO;
}

/* --- CEU Capture Operations --- */

/*
 * ceu_hw_config() - Configure CEU interface registers.
 */
static int ceu_hw_config(struct ceu_device *ceudev)
{
	u32 camcr, cdocr, cfzsr, cdwdr, capwr;
	struct v4l2_pix_format_mplane *pix = &ceudev->v4l2_pix;
	struct ceu_subdev *ceu_sd = ceudev->sd;
	struct ceu_mbus_fmt *mbus_fmt = &ceu_sd->mbus_fmt;
	unsigned int mbus_flags = ceu_sd->mbus_flags;

	/* Start configuring CEU registers */
	ceu_write(ceudev, CEU_CAIFR, 0);
	ceu_write(ceudev, CEU_CFWCR, 0);
	ceu_write(ceudev, CEU_CRCNTR, 0);
	ceu_write(ceudev, CEU_CRCMPR, 0);

	/* Set the frame capture period for both image capture and data sync. */
	capwr = (pix->height << 16) | pix->width * mbus_fmt->bpp / 8;

	/*
	 * Swap input data endianness by default.
	 * In data fetch mode bytes are received in chunks of 8 bytes.
	 * D0, D1, D2, D3, D4, D5, D6, D7 (D0 received first)
	 * The data is however by default written to memory in reverse order:
	 * D7, D6, D5, D4, D3, D2, D1, D0 (D7 written to lowest byte)
	 *
	 * Use CEU_CDOCR[2:0] to swap data ordering.
	 */
	cdocr = CEU_CDOCR_SWAP_ENDIANNESS;

	/*
	 * Configure CAMCR and CDOCR:
	 * match input components ordering with memory output format and
	 * handle downsampling to YUV420.
	 *
	 * If the memory output planar format is 'swapped' (Cr before Cb) and
	 * input format is not, use the swapped version of CAMCR.DTARY.
	 *
	 * If the memory output planar format is not 'swapped' (Cb before Cr)
	 * and input format is, use the swapped version of CAMCR.DTARY.
	 *
	 * CEU by default downsample to planar YUV420 (CDCOR[4] = 0).
	 * If output is planar YUV422 set CDOCR[4] = 1
	 *
	 * No downsample for data fetch sync mode.
	 */
	switch (pix->pixelformat) {
	/* Data fetch sync mode */
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
		camcr	= CEU_CAMCR_JPEG;
		cdocr	|= CEU_CDOCR_NO_DOWSAMPLE;
		cfzsr	= (pix->height << 16) | pix->width;
		cdwdr	= pix->plane_fmt[0].bytesperline;
		break;

	/* Non-swapped planar image capture mode. */
	case V4L2_PIX_FMT_NV16:
		cdocr	|= CEU_CDOCR_NO_DOWSAMPLE;
		fallthrough;
	case V4L2_PIX_FMT_NV12:
		if (mbus_fmt->swapped)
			camcr = mbus_fmt->fmt_order_swap;
		else
			camcr = mbus_fmt->fmt_order;

		cfzsr	= (pix->height << 16) | pix->width;
		cdwdr	= pix->width;
		break;

	/* Swapped planar image capture mode. */
	case V4L2_PIX_FMT_NV61:
		cdocr	|= CEU_CDOCR_NO_DOWSAMPLE;
		fallthrough;
	case V4L2_PIX_FMT_NV21:
		if (mbus_fmt->swapped)
			camcr = mbus_fmt->fmt_order;
		else
			camcr = mbus_fmt->fmt_order_swap;

		cfzsr	= (pix->height << 16) | pix->width;
		cdwdr	= pix->width;
		break;

	default:
		return -EINVAL;
	}

	camcr |= mbus_flags & V4L2_MBUS_VSYNC_ACTIVE_LOW ? 1 << 1 : 0;
	camcr |= mbus_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW ? 1 << 0 : 0;

	/* TODO: handle 16 bit bus width with DTIF bit in CAMCR */
	ceu_write(ceudev, CEU_CAMCR, camcr);
	ceu_write(ceudev, CEU_CDOCR, cdocr);
	ceu_write(ceudev, CEU_CAPCR, CEU_CAPCR_BUS_WIDTH256);

	/*
	 * TODO: make CAMOR offsets configurable.
	 * CAMOR wants to know the number of blanks between a VS/HS signal
	 * and valid data. This value should actually come from the sensor...
	 */
	ceu_write(ceudev, CEU_CAMOR, 0);

	/* TODO: 16 bit bus width require re-calculation of cdwdr and cfzsr */
	ceu_write(ceudev, CEU_CAPWR, capwr);
	ceu_write(ceudev, CEU_CFSZR, cfzsr);
	ceu_write(ceudev, CEU_CDWDR, cdwdr);

	return 0;
}

/*
 * ceu_capture() - Trigger start of a capture sequence.
 *
 * Program the CEU DMA registers with addresses where to transfer image data.
 */
static int ceu_capture(struct ceu_device *ceudev)
{
	struct v4l2_pix_format_mplane *pix = &ceudev->v4l2_pix;
	dma_addr_t phys_addr_top;

	phys_addr_top =
		vb2_dma_contig_plane_dma_addr(&ceudev->active->vb2_buf, 0);
	ceu_write(ceudev, CEU_CDAYR, phys_addr_top);

	/* Ignore CbCr plane for non multi-planar image formats. */
	if (ceu_fmt_mplane(pix)) {
		phys_addr_top =
			vb2_dma_contig_plane_dma_addr(&ceudev->active->vb2_buf,
						      1);
		ceu_write(ceudev, CEU_CDACR, phys_addr_top);
	}

	/*
	 * Trigger new capture start: once for each frame, as we work in
	 * one-frame capture mode.
	 */
	ceu_write(ceudev, CEU_CAPSR, CEU_CAPSR_CE);

	return 0;
}

static irqreturn_t ceu_irq(int irq, void *data)
{
	struct ceu_device *ceudev = data;
	struct vb2_v4l2_buffer *vbuf;
	struct ceu_buffer *buf;
	u32 status;

	/* Clean interrupt status. */
	status = ceu_read(ceudev, CEU_CETCR);
	ceu_write(ceudev, CEU_CETCR, ~ceudev->irq_mask);

	/* Unexpected interrupt. */
	if (!(status & CEU_CEIER_MASK))
		return IRQ_NONE;

	spin_lock(&ceudev->lock);

	/* Stale interrupt from a released buffer, ignore it. */
	vbuf = ceudev->active;
	if (!vbuf) {
		spin_unlock(&ceudev->lock);
		return IRQ_HANDLED;
	}

	/*
	 * When a VBP interrupt occurs, no capture end interrupt will occur
	 * and the image of that frame is not captured correctly.
	 */
	if (status & CEU_CEIER_VBP) {
		dev_err(ceudev->dev, "VBP interrupt: abort capture\n");
		goto error_irq_out;
	}

	/* Prepare to return the 'previous' buffer. */
	vbuf->vb2_buf.timestamp = ktime_get_ns();
	vbuf->sequence = ceudev->sequence++;
	vbuf->field = ceudev->field;

	/* Prepare a new 'active' buffer and trigger a new capture. */
	if (!list_empty(&ceudev->capture)) {
		buf = list_first_entry(&ceudev->capture, struct ceu_buffer,
				       queue);
		list_del(&buf->queue);
		ceudev->active = &buf->vb;

		ceu_capture(ceudev);
	}

	/* Return the 'previous' buffer. */
	vb2_buffer_done(&vbuf->vb2_buf, VB2_BUF_STATE_DONE);

	spin_unlock(&ceudev->lock);

	return IRQ_HANDLED;

error_irq_out:
	/* Return the 'previous' buffer and all queued ones. */
	vb2_buffer_done(&vbuf->vb2_buf, VB2_BUF_STATE_ERROR);

	list_for_each_entry(buf, &ceudev->capture, queue)
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);

	spin_unlock(&ceudev->lock);

	return IRQ_HANDLED;
}

/* --- CEU Videobuf2 operations --- */

static void ceu_update_plane_sizes(struct v4l2_plane_pix_format *plane,
				   unsigned int bpl, unsigned int szimage)
{
	memset(plane, 0, sizeof(*plane));

	plane->sizeimage = szimage;
	if (plane->bytesperline < bpl || plane->bytesperline > CEU_MAX_BPL)
		plane->bytesperline = bpl;
}

/*
 * ceu_calc_plane_sizes() - Fill per-plane 'struct v4l2_plane_pix_format'
 *			    information according to the currently configured
 *			    pixel format.
 * @ceu_device: CEU device.
 * @ceu_fmt: Active image format.
 * @pix: Pixel format information (store line width and image sizes)
 */
static void ceu_calc_plane_sizes(struct ceu_device *ceudev,
				 const struct ceu_fmt *ceu_fmt,
				 struct v4l2_pix_format_mplane *pix)
{
	unsigned int bpl, szimage;

	switch (pix->pixelformat) {
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_VYUY:
		pix->num_planes	= 1;
		bpl		= pix->width * ceu_fmt->bpp / 8;
		szimage		= pix->height * bpl;
		ceu_update_plane_sizes(&pix->plane_fmt[0], bpl, szimage);
		break;

	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		pix->num_planes	= 2;
		bpl		= pix->width;
		szimage		= pix->height * pix->width;
		ceu_update_plane_sizes(&pix->plane_fmt[0], bpl, szimage);
		ceu_update_plane_sizes(&pix->plane_fmt[1], bpl, szimage / 2);
		break;

	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
	default:
		pix->num_planes	= 2;
		bpl		= pix->width;
		szimage		= pix->height * pix->width;
		ceu_update_plane_sizes(&pix->plane_fmt[0], bpl, szimage);
		ceu_update_plane_sizes(&pix->plane_fmt[1], bpl, szimage);
		break;
	}
}

/*
 * ceu_vb2_setup() - is called to check whether the driver can accept the
 *		     requested number of buffers and to fill in plane sizes
 *		     for the current frame format, if required.
 */
static int ceu_vb2_setup(struct vb2_queue *vq, unsigned int *count,
			 unsigned int *num_planes, unsigned int sizes[],
			 struct device *alloc_devs[])
{
	struct ceu_device *ceudev = vb2_get_drv_priv(vq);
	struct v4l2_pix_format_mplane *pix = &ceudev->v4l2_pix;
	unsigned int i;

	/* num_planes is set: just check plane sizes. */
	if (*num_planes) {
		for (i = 0; i < pix->num_planes; i++)
			if (sizes[i] < pix->plane_fmt[i].sizeimage)
				return -EINVAL;

		return 0;
	}

	/* num_planes not set: called from REQBUFS, just set plane sizes. */
	*num_planes = pix->num_planes;
	for (i = 0; i < pix->num_planes; i++)
		sizes[i] = pix->plane_fmt[i].sizeimage;

	return 0;
}

static void ceu_vb2_queue(struct vb2_buffer *vb)
{
	struct ceu_device *ceudev = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct ceu_buffer *buf = vb2_to_ceu(vbuf);
	unsigned long irqflags;

	spin_lock_irqsave(&ceudev->lock, irqflags);
	list_add_tail(&buf->queue, &ceudev->capture);
	spin_unlock_irqrestore(&ceudev->lock, irqflags);
}

static int ceu_vb2_prepare(struct vb2_buffer *vb)
{
	struct ceu_device *ceudev = vb2_get_drv_priv(vb->vb2_queue);
	struct v4l2_pix_format_mplane *pix = &ceudev->v4l2_pix;
	unsigned int i;

	for (i = 0; i < pix->num_planes; i++) {
		if (vb2_plane_size(vb, i) < pix->plane_fmt[i].sizeimage) {
			dev_err(ceudev->dev,
				"Plane size too small (%lu < %u)\n",
				vb2_plane_size(vb, i),
				pix->plane_fmt[i].sizeimage);
			return -EINVAL;
		}

		vb2_set_plane_payload(vb, i, pix->plane_fmt[i].sizeimage);
	}

	return 0;
}

static int ceu_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct ceu_device *ceudev = vb2_get_drv_priv(vq);
	struct v4l2_subdev *v4l2_sd = ceudev->sd->v4l2_sd;
	struct ceu_buffer *buf;
	unsigned long irqflags;
	int ret;

	/* Program the CEU interface according to the CEU image format. */
	ret = ceu_hw_config(ceudev);
	if (ret)
		goto error_return_bufs;

	ret = v4l2_subdev_call(v4l2_sd, video, s_stream, 1);
	if (ret && ret != -ENOIOCTLCMD) {
		dev_dbg(ceudev->dev,
			"Subdevice failed to start streaming: %d\n", ret);
		goto error_return_bufs;
	}

	spin_lock_irqsave(&ceudev->lock, irqflags);
	ceudev->sequence = 0;

	/* Grab the first available buffer and trigger the first capture. */
	buf = list_first_entry(&ceudev->capture, struct ceu_buffer,
			       queue);
	if (!buf) {
		spin_unlock_irqrestore(&ceudev->lock, irqflags);
		dev_dbg(ceudev->dev,
			"No buffer available for capture.\n");
		goto error_stop_sensor;
	}

	list_del(&buf->queue);
	ceudev->active = &buf->vb;

	/* Clean and program interrupts for first capture. */
	ceu_write(ceudev, CEU_CETCR, ~ceudev->irq_mask);
	ceu_write(ceudev, CEU_CEIER, CEU_CEIER_MASK);

	ceu_capture(ceudev);

	spin_unlock_irqrestore(&ceudev->lock, irqflags);

	return 0;

error_stop_sensor:
	v4l2_subdev_call(v4l2_sd, video, s_stream, 0);

error_return_bufs:
	spin_lock_irqsave(&ceudev->lock, irqflags);
	list_for_each_entry(buf, &ceudev->capture, queue)
		vb2_buffer_done(&ceudev->active->vb2_buf,
				VB2_BUF_STATE_QUEUED);
	ceudev->active = NULL;
	spin_unlock_irqrestore(&ceudev->lock, irqflags);

	return ret;
}

static void ceu_stop_streaming(struct vb2_queue *vq)
{
	struct ceu_device *ceudev = vb2_get_drv_priv(vq);
	struct v4l2_subdev *v4l2_sd = ceudev->sd->v4l2_sd;
	struct ceu_buffer *buf;
	unsigned long irqflags;

	/* Clean and disable interrupt sources. */
	ceu_write(ceudev, CEU_CETCR,
		  ceu_read(ceudev, CEU_CETCR) & ceudev->irq_mask);
	ceu_write(ceudev, CEU_CEIER, CEU_CEIER_MASK);

	v4l2_subdev_call(v4l2_sd, video, s_stream, 0);

	spin_lock_irqsave(&ceudev->lock, irqflags);
	if (ceudev->active) {
		vb2_buffer_done(&ceudev->active->vb2_buf,
				VB2_BUF_STATE_ERROR);
		ceudev->active = NULL;
	}

	/* Release all queued buffers. */
	list_for_each_entry(buf, &ceudev->capture, queue)
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	INIT_LIST_HEAD(&ceudev->capture);

	spin_unlock_irqrestore(&ceudev->lock, irqflags);

	ceu_soft_reset(ceudev);
}

static const struct vb2_ops ceu_vb2_ops = {
	.queue_setup		= ceu_vb2_setup,
	.buf_queue		= ceu_vb2_queue,
	.buf_prepare		= ceu_vb2_prepare,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
	.start_streaming	= ceu_start_streaming,
	.stop_streaming		= ceu_stop_streaming,
};

/* --- CEU image formats handling --- */

/*
 * __ceu_try_fmt() - test format on CEU and sensor
 * @ceudev: The CEU device.
 * @v4l2_fmt: format to test.
 * @sd_mbus_code: the media bus code accepted by the subdevice; output param.
 *
 * Returns 0 for success, < 0 for errors.
 */
static int __ceu_try_fmt(struct ceu_device *ceudev, struct v4l2_format *v4l2_fmt,
			 u32 *sd_mbus_code)
{
	struct ceu_subdev *ceu_sd = ceudev->sd;
	struct v4l2_pix_format_mplane *pix = &v4l2_fmt->fmt.pix_mp;
	struct v4l2_subdev *v4l2_sd = ceu_sd->v4l2_sd;
	struct v4l2_subdev_pad_config pad_cfg;
	struct v4l2_subdev_state pad_state = {
		.pads = &pad_cfg
		};
	const struct ceu_fmt *ceu_fmt;
	u32 mbus_code_old;
	u32 mbus_code;
	int ret;

	/*
	 * Set format on sensor sub device: bus format used to produce memory
	 * format is selected depending on YUV component ordering or
	 * at initialization time.
	 */
	struct v4l2_subdev_format sd_format = {
		.which	= V4L2_SUBDEV_FORMAT_TRY,
	};

	mbus_code_old = ceu_sd->mbus_fmt.mbus_code;

	switch (pix->pixelformat) {
	case V4L2_PIX_FMT_YUYV:
		mbus_code = MEDIA_BUS_FMT_YUYV8_2X8;
		break;
	case V4L2_PIX_FMT_UYVY:
		mbus_code = MEDIA_BUS_FMT_UYVY8_2X8;
		break;
	case V4L2_PIX_FMT_YVYU:
		mbus_code = MEDIA_BUS_FMT_YVYU8_2X8;
		break;
	case V4L2_PIX_FMT_VYUY:
		mbus_code = MEDIA_BUS_FMT_VYUY8_2X8;
		break;
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		mbus_code = ceu_sd->mbus_fmt.mbus_code;
		break;

	default:
		pix->pixelformat = V4L2_PIX_FMT_NV16;
		mbus_code = ceu_sd->mbus_fmt.mbus_code;
		break;
	}

	ceu_fmt = get_ceu_fmt_from_fourcc(pix->pixelformat);

	/* CFSZR requires height and width to be 4-pixel aligned. */
	v4l_bound_align_image(&pix->width, 2, CEU_MAX_WIDTH, 4,
			      &pix->height, 4, CEU_MAX_HEIGHT, 4, 0);

	v4l2_fill_mbus_format_mplane(&sd_format.format, pix);

	/*
	 * Try with the mbus_code matching YUYV components ordering first,
	 * if that one fails, fallback to default selected at initialization
	 * time.
	 */
	sd_format.format.code = mbus_code;
	ret = v4l2_subdev_call(v4l2_sd, pad, set_fmt, &pad_state, &sd_format);
	if (ret) {
		if (ret == -EINVAL) {
			/* fallback */
			sd_format.format.code = mbus_code_old;
			ret = v4l2_subdev_call(v4l2_sd, pad, set_fmt,
					       &pad_state, &sd_format);
		}

		if (ret)
			return ret;
	}

	/* Apply size returned by sensor as the CEU can't scale. */
	v4l2_fill_pix_format_mplane(pix, &sd_format.format);

	/* Calculate per-plane sizes based on image format. */
	ceu_calc_plane_sizes(ceudev, ceu_fmt, pix);

	/* Report to caller the configured mbus format. */
	*sd_mbus_code = sd_format.format.code;

	return 0;
}

/*
 * ceu_try_fmt() - Wrapper for __ceu_try_fmt; discard configured mbus_fmt
 */
static int ceu_try_fmt(struct ceu_device *ceudev, struct v4l2_format *v4l2_fmt)
{
	u32 mbus_code;

	return __ceu_try_fmt(ceudev, v4l2_fmt, &mbus_code);
}

/*
 * ceu_set_fmt() - Apply the supplied format to both sensor and CEU
 */
static int ceu_set_fmt(struct ceu_device *ceudev, struct v4l2_format *v4l2_fmt)
{
	struct ceu_subdev *ceu_sd = ceudev->sd;
	struct v4l2_subdev *v4l2_sd = ceu_sd->v4l2_sd;
	u32 mbus_code;
	int ret;

	/*
	 * Set format on sensor sub device: bus format used to produce memory
	 * format is selected at initialization time.
	 */
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	ret = __ceu_try_fmt(ceudev, v4l2_fmt, &mbus_code);
	if (ret)
		return ret;

	format.format.code = mbus_code;
	v4l2_fill_mbus_format_mplane(&format.format, &v4l2_fmt->fmt.pix_mp);
	ret = v4l2_subdev_call(v4l2_sd, pad, set_fmt, NULL, &format);
	if (ret)
		return ret;

	ceudev->v4l2_pix = v4l2_fmt->fmt.pix_mp;
	ceudev->field = V4L2_FIELD_NONE;

	return 0;
}

/*
 * ceu_set_default_fmt() - Apply default NV16 memory output format with VGA
 *			   sizes.
 */
static int ceu_set_default_fmt(struct ceu_device *ceudev)
{
	int ret;

	struct v4l2_format v4l2_fmt = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.fmt.pix_mp = {
			.width		= VGA_WIDTH,
			.height		= VGA_HEIGHT,
			.field		= V4L2_FIELD_NONE,
			.pixelformat	= V4L2_PIX_FMT_NV16,
			.num_planes	= 2,
			.plane_fmt	= {
				[0]	= {
					.sizeimage = VGA_WIDTH * VGA_HEIGHT * 2,
					.bytesperline = VGA_WIDTH * 2,
				},
				[1]	= {
					.sizeimage = VGA_WIDTH * VGA_HEIGHT * 2,
					.bytesperline = VGA_WIDTH * 2,
				},
			},
		},
	};

	ret = ceu_try_fmt(ceudev, &v4l2_fmt);
	if (ret)
		return ret;

	ceudev->v4l2_pix = v4l2_fmt.fmt.pix_mp;
	ceudev->field = V4L2_FIELD_NONE;

	return 0;
}

/*
 * ceu_init_mbus_fmt() - Query sensor for supported formats and initialize
 *			 CEU media bus format used to produce memory formats.
 *
 * Find out if sensor can produce a permutation of 8-bits YUYV bus format.
 * From a single 8-bits YUYV bus format the CEU can produce several memory
 * output formats:
 * - NV[12|21|16|61] through image fetch mode;
 * - YUYV422 if sensor provides YUYV422
 *
 * TODO: Other YUYV422 permutations through data fetch sync mode and DTARY
 * TODO: Binary data (eg. JPEG) and raw formats through data fetch sync mode
 */
static int ceu_init_mbus_fmt(struct ceu_device *ceudev)
{
	struct ceu_subdev *ceu_sd = ceudev->sd;
	struct ceu_mbus_fmt *mbus_fmt = &ceu_sd->mbus_fmt;
	struct v4l2_subdev *v4l2_sd = ceu_sd->v4l2_sd;
	bool yuyv_bus_fmt = false;

	struct v4l2_subdev_mbus_code_enum sd_mbus_fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.index = 0,
	};

	/* Find out if sensor can produce any permutation of 8-bits YUYV422. */
	while (!yuyv_bus_fmt &&
	       !v4l2_subdev_call(v4l2_sd, pad, enum_mbus_code,
				 NULL, &sd_mbus_fmt)) {
		switch (sd_mbus_fmt.code) {
		case MEDIA_BUS_FMT_YUYV8_2X8:
		case MEDIA_BUS_FMT_YVYU8_2X8:
		case MEDIA_BUS_FMT_UYVY8_2X8:
		case MEDIA_BUS_FMT_VYUY8_2X8:
			yuyv_bus_fmt = true;
			break;
		default:
			/*
			 * Only support 8-bits YUYV bus formats at the moment;
			 *
			 * TODO: add support for binary formats (data sync
			 * fetch mode).
			 */
			break;
		}

		sd_mbus_fmt.index++;
	}

	if (!yuyv_bus_fmt)
		return -ENXIO;

	/*
	 * Save the first encountered YUYV format as "mbus_fmt" and use it
	 * to output all planar YUV422 and YUV420 (NV*) formats to memory as
	 * well as for data synch fetch mode (YUYV - YVYU etc. ).
	 */
	mbus_fmt->mbus_code	= sd_mbus_fmt.code;
	mbus_fmt->bps		= 8;

	/* Annotate the selected bus format components ordering. */
	switch (sd_mbus_fmt.code) {
	case MEDIA_BUS_FMT_YUYV8_2X8:
		mbus_fmt->fmt_order		= CEU_CAMCR_DTARY_8_YUYV;
		mbus_fmt->fmt_order_swap	= CEU_CAMCR_DTARY_8_YVYU;
		mbus_fmt->swapped		= false;
		mbus_fmt->bpp			= 16;
		break;

	case MEDIA_BUS_FMT_YVYU8_2X8:
		mbus_fmt->fmt_order		= CEU_CAMCR_DTARY_8_YVYU;
		mbus_fmt->fmt_order_swap	= CEU_CAMCR_DTARY_8_YUYV;
		mbus_fmt->swapped		= true;
		mbus_fmt->bpp			= 16;
		break;

	case MEDIA_BUS_FMT_UYVY8_2X8:
		mbus_fmt->fmt_order		= CEU_CAMCR_DTARY_8_UYVY;
		mbus_fmt->fmt_order_swap	= CEU_CAMCR_DTARY_8_VYUY;
		mbus_fmt->swapped		= false;
		mbus_fmt->bpp			= 16;
		break;

	case MEDIA_BUS_FMT_VYUY8_2X8:
		mbus_fmt->fmt_order		= CEU_CAMCR_DTARY_8_VYUY;
		mbus_fmt->fmt_order_swap	= CEU_CAMCR_DTARY_8_UYVY;
		mbus_fmt->swapped		= true;
		mbus_fmt->bpp			= 16;
		break;
	}

	return 0;
}

/* --- Runtime PM Handlers --- */

/*
 * ceu_runtime_resume() - soft-reset the interface and turn sensor power on.
 */
static int __maybe_unused ceu_runtime_resume(struct device *dev)
{
	struct ceu_device *ceudev = dev_get_drvdata(dev);
	struct v4l2_subdev *v4l2_sd = ceudev->sd->v4l2_sd;

	v4l2_subdev_call(v4l2_sd, core, s_power, 1);

	ceu_soft_reset(ceudev);

	return 0;
}

/*
 * ceu_runtime_suspend() - disable capture and interrupts and soft-reset.
 *			   Turn sensor power off.
 */
static int __maybe_unused ceu_runtime_suspend(struct device *dev)
{
	struct ceu_device *ceudev = dev_get_drvdata(dev);
	struct v4l2_subdev *v4l2_sd = ceudev->sd->v4l2_sd;

	v4l2_subdev_call(v4l2_sd, core, s_power, 0);

	ceu_write(ceudev, CEU_CEIER, 0);
	ceu_soft_reset(ceudev);

	return 0;
}

/* --- File Operations --- */

static int ceu_open(struct file *file)
{
	struct ceu_device *ceudev = video_drvdata(file);
	int ret;

	ret = v4l2_fh_open(file);
	if (ret)
		return ret;

	mutex_lock(&ceudev->mlock);
	/* Causes soft-reset and sensor power on on first open */
	ret = pm_runtime_resume_and_get(ceudev->dev);
	mutex_unlock(&ceudev->mlock);

	return ret;
}

static int ceu_release(struct file *file)
{
	struct ceu_device *ceudev = video_drvdata(file);

	vb2_fop_release(file);

	mutex_lock(&ceudev->mlock);
	/* Causes soft-reset and sensor power down on last close */
	pm_runtime_put(ceudev->dev);
	mutex_unlock(&ceudev->mlock);

	return 0;
}

static const struct v4l2_file_operations ceu_fops = {
	.owner			= THIS_MODULE,
	.open			= ceu_open,
	.release		= ceu_release,
	.unlocked_ioctl		= video_ioctl2,
	.mmap			= vb2_fop_mmap,
	.poll			= vb2_fop_poll,
};

/* --- Video Device IOCTLs --- */

static int ceu_querycap(struct file *file, void *priv,
			struct v4l2_capability *cap)
{
	struct ceu_device *ceudev = video_drvdata(file);

	strscpy(cap->card, "Renesas CEU", sizeof(cap->card));
	strscpy(cap->driver, DRIVER_NAME, sizeof(cap->driver));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:renesas-ceu-%s", dev_name(ceudev->dev));

	return 0;
}

static int ceu_enum_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_fmtdesc *f)
{
	const struct ceu_fmt *fmt;

	if (f->index >= ARRAY_SIZE(ceu_fmt_list))
		return -EINVAL;

	fmt = &ceu_fmt_list[f->index];
	f->pixelformat = fmt->fourcc;

	return 0;
}

static int ceu_try_fmt_vid_cap(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct ceu_device *ceudev = video_drvdata(file);

	return ceu_try_fmt(ceudev, f);
}

static int ceu_s_fmt_vid_cap(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct ceu_device *ceudev = video_drvdata(file);

	if (vb2_is_streaming(&ceudev->vb2_vq))
		return -EBUSY;

	return ceu_set_fmt(ceudev, f);
}

static int ceu_g_fmt_vid_cap(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct ceu_device *ceudev = video_drvdata(file);

	f->fmt.pix_mp = ceudev->v4l2_pix;

	return 0;
}

static int ceu_enum_input(struct file *file, void *priv,
			  struct v4l2_input *inp)
{
	struct ceu_device *ceudev = video_drvdata(file);
	struct ceu_subdev *ceusd;

	if (inp->index >= ceudev->num_sd)
		return -EINVAL;

	ceusd = ceudev->subdevs[inp->index];

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = 0;
	snprintf(inp->name, sizeof(inp->name), "Camera%u: %s",
		 inp->index, ceusd->v4l2_sd->name);

	return 0;
}

static int ceu_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct ceu_device *ceudev = video_drvdata(file);

	*i = ceudev->sd_index;

	return 0;
}

static int ceu_s_input(struct file *file, void *priv, unsigned int i)
{
	struct ceu_device *ceudev = video_drvdata(file);
	struct ceu_subdev *ceu_sd_old;
	int ret;

	if (i >= ceudev->num_sd)
		return -EINVAL;

	if (vb2_is_streaming(&ceudev->vb2_vq))
		return -EBUSY;

	if (i == ceudev->sd_index)
		return 0;

	ceu_sd_old = ceudev->sd;
	ceudev->sd = ceudev->subdevs[i];

	/*
	 * Make sure we can generate output image formats and apply
	 * default one.
	 */
	ret = ceu_init_mbus_fmt(ceudev);
	if (ret) {
		ceudev->sd = ceu_sd_old;
		return -EINVAL;
	}

	ret = ceu_set_default_fmt(ceudev);
	if (ret) {
		ceudev->sd = ceu_sd_old;
		return -EINVAL;
	}

	/* Now that we're sure we can use the sensor, power off the old one. */
	v4l2_subdev_call(ceu_sd_old->v4l2_sd, core, s_power, 0);
	v4l2_subdev_call(ceudev->sd->v4l2_sd, core, s_power, 1);

	ceudev->sd_index = i;

	return 0;
}

static int ceu_g_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct ceu_device *ceudev = video_drvdata(file);

	return v4l2_g_parm_cap(video_devdata(file), ceudev->sd->v4l2_sd, a);
}

static int ceu_s_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct ceu_device *ceudev = video_drvdata(file);

	return v4l2_s_parm_cap(video_devdata(file), ceudev->sd->v4l2_sd, a);
}

static int ceu_enum_framesizes(struct file *file, void *fh,
			       struct v4l2_frmsizeenum *fsize)
{
	struct ceu_device *ceudev = video_drvdata(file);
	struct ceu_subdev *ceu_sd = ceudev->sd;
	const struct ceu_fmt *ceu_fmt;
	struct v4l2_subdev *v4l2_sd = ceu_sd->v4l2_sd;
	int ret;

	struct v4l2_subdev_frame_size_enum fse = {
		.code	= ceu_sd->mbus_fmt.mbus_code,
		.index	= fsize->index,
		.which	= V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	/* Just check if user supplied pixel format is supported. */
	ceu_fmt = get_ceu_fmt_from_fourcc(fsize->pixel_format);
	if (!ceu_fmt)
		return -EINVAL;

	ret = v4l2_subdev_call(v4l2_sd, pad, enum_frame_size,
			       NULL, &fse);
	if (ret)
		return ret;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = CEU_W_MAX(fse.max_width);
	fsize->discrete.height = CEU_H_MAX(fse.max_height);

	return 0;
}

static int ceu_enum_frameintervals(struct file *file, void *fh,
				   struct v4l2_frmivalenum *fival)
{
	struct ceu_device *ceudev = video_drvdata(file);
	struct ceu_subdev *ceu_sd = ceudev->sd;
	const struct ceu_fmt *ceu_fmt;
	struct v4l2_subdev *v4l2_sd = ceu_sd->v4l2_sd;
	int ret;

	struct v4l2_subdev_frame_interval_enum fie = {
		.code	= ceu_sd->mbus_fmt.mbus_code,
		.index = fival->index,
		.width = fival->width,
		.height = fival->height,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	/* Just check if user supplied pixel format is supported. */
	ceu_fmt = get_ceu_fmt_from_fourcc(fival->pixel_format);
	if (!ceu_fmt)
		return -EINVAL;

	ret = v4l2_subdev_call(v4l2_sd, pad, enum_frame_interval, NULL,
			       &fie);
	if (ret)
		return ret;

	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete = fie.interval;

	return 0;
}

static const struct v4l2_ioctl_ops ceu_ioctl_ops = {
	.vidioc_querycap		= ceu_querycap,

	.vidioc_enum_fmt_vid_cap	= ceu_enum_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap_mplane	= ceu_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap_mplane	= ceu_s_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap_mplane	= ceu_g_fmt_vid_cap,

	.vidioc_enum_input		= ceu_enum_input,
	.vidioc_g_input			= ceu_g_input,
	.vidioc_s_input			= ceu_s_input,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,

	.vidioc_g_parm			= ceu_g_parm,
	.vidioc_s_parm			= ceu_s_parm,
	.vidioc_enum_framesizes		= ceu_enum_framesizes,
	.vidioc_enum_frameintervals	= ceu_enum_frameintervals,

	.vidioc_log_status              = v4l2_ctrl_log_status,
	.vidioc_subscribe_event         = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event       = v4l2_event_unsubscribe,
};

/*
 * ceu_vdev_release() - release CEU video device memory when last reference
 *			to this driver is closed
 */
static void ceu_vdev_release(struct video_device *vdev)
{
	struct ceu_device *ceudev = video_get_drvdata(vdev);

	kfree(ceudev);
}

static int ceu_notify_bound(struct v4l2_async_notifier *notifier,
			    struct v4l2_subdev *v4l2_sd,
			    struct v4l2_async_subdev *asd)
{
	struct v4l2_device *v4l2_dev = notifier->v4l2_dev;
	struct ceu_device *ceudev = v4l2_to_ceu(v4l2_dev);
	struct ceu_subdev *ceu_sd = to_ceu_subdev(asd);

	ceu_sd->v4l2_sd = v4l2_sd;
	ceudev->num_sd++;

	return 0;
}

static int ceu_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct v4l2_device *v4l2_dev = notifier->v4l2_dev;
	struct ceu_device *ceudev = v4l2_to_ceu(v4l2_dev);
	struct video_device *vdev = &ceudev->vdev;
	struct vb2_queue *q = &ceudev->vb2_vq;
	struct v4l2_subdev *v4l2_sd;
	int ret;

	/* Initialize vb2 queue. */
	q->type			= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q->io_modes		= VB2_MMAP | VB2_DMABUF;
	q->drv_priv		= ceudev;
	q->ops			= &ceu_vb2_ops;
	q->mem_ops		= &vb2_dma_contig_memops;
	q->buf_struct_size	= sizeof(struct ceu_buffer);
	q->timestamp_flags	= V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_buffers_needed	= 2;
	q->lock			= &ceudev->mlock;
	q->dev			= ceudev->v4l2_dev.dev;

	ret = vb2_queue_init(q);
	if (ret)
		return ret;

	/*
	 * Make sure at least one sensor is primary and use it to initialize
	 * ceu formats.
	 */
	if (!ceudev->sd) {
		ceudev->sd = ceudev->subdevs[0];
		ceudev->sd_index = 0;
	}

	v4l2_sd = ceudev->sd->v4l2_sd;

	ret = ceu_init_mbus_fmt(ceudev);
	if (ret)
		return ret;

	ret = ceu_set_default_fmt(ceudev);
	if (ret)
		return ret;

	/* Register the video device. */
	strscpy(vdev->name, DRIVER_NAME, sizeof(vdev->name));
	vdev->v4l2_dev		= v4l2_dev;
	vdev->lock		= &ceudev->mlock;
	vdev->queue		= &ceudev->vb2_vq;
	vdev->ctrl_handler	= v4l2_sd->ctrl_handler;
	vdev->fops		= &ceu_fops;
	vdev->ioctl_ops		= &ceu_ioctl_ops;
	vdev->release		= ceu_vdev_release;
	vdev->device_caps	= V4L2_CAP_VIDEO_CAPTURE_MPLANE |
				  V4L2_CAP_STREAMING;
	video_set_drvdata(vdev, ceudev);

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		v4l2_err(vdev->v4l2_dev,
			 "video_register_device failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct v4l2_async_notifier_operations ceu_notify_ops = {
	.bound		= ceu_notify_bound,
	.complete	= ceu_notify_complete,
};

/*
 * ceu_init_async_subdevs() - Initialize CEU subdevices and async_subdevs in
 *                           ceu device. Both DT and platform data parsing use
 *                           this routine.
 *
 * Returns 0 for success, -ENOMEM for failure.
 */
static int ceu_init_async_subdevs(struct ceu_device *ceudev, unsigned int n_sd)
{
	/* Reserve memory for 'n_sd' ceu_subdev descriptors. */
	ceudev->subdevs = devm_kcalloc(ceudev->dev, n_sd,
				       sizeof(*ceudev->subdevs), GFP_KERNEL);
	if (!ceudev->subdevs)
		return -ENOMEM;

	ceudev->sd = NULL;
	ceudev->sd_index = 0;
	ceudev->num_sd = 0;

	return 0;
}

/*
 * ceu_parse_platform_data() - Initialize async_subdevices using platform
 *			       device provided data.
 */
static int ceu_parse_platform_data(struct ceu_device *ceudev,
				   const struct ceu_platform_data *pdata)
{
	const struct ceu_async_subdev *async_sd;
	struct ceu_subdev *ceu_sd;
	unsigned int i;
	int ret;

	if (pdata->num_subdevs == 0)
		return -ENODEV;

	ret = ceu_init_async_subdevs(ceudev, pdata->num_subdevs);
	if (ret)
		return ret;

	for (i = 0; i < pdata->num_subdevs; i++) {

		/* Setup the ceu subdevice and the async subdevice. */
		async_sd = &pdata->subdevs[i];
		ceu_sd = v4l2_async_notifier_add_i2c_subdev(&ceudev->notifier,
				async_sd->i2c_adapter_id,
				async_sd->i2c_address,
				struct ceu_subdev);
		if (IS_ERR(ceu_sd)) {
			v4l2_async_notifier_cleanup(&ceudev->notifier);
			return PTR_ERR(ceu_sd);
		}
		ceu_sd->mbus_flags = async_sd->flags;
		ceudev->subdevs[i] = ceu_sd;
	}

	return pdata->num_subdevs;
}

/*
 * ceu_parse_dt() - Initialize async_subdevs parsing device tree graph.
 */
static int ceu_parse_dt(struct ceu_device *ceudev)
{
	struct device_node *of = ceudev->dev->of_node;
	struct device_node *ep;
	struct ceu_subdev *ceu_sd;
	unsigned int i;
	int num_ep;
	int ret;

	num_ep = of_graph_get_endpoint_count(of);
	if (!num_ep)
		return -ENODEV;

	ret = ceu_init_async_subdevs(ceudev, num_ep);
	if (ret)
		return ret;

	for (i = 0; i < num_ep; i++) {
		struct v4l2_fwnode_endpoint fw_ep = {
			.bus_type = V4L2_MBUS_PARALLEL,
			.bus = {
				.parallel = {
					.flags = V4L2_MBUS_HSYNC_ACTIVE_HIGH |
						 V4L2_MBUS_VSYNC_ACTIVE_HIGH,
					.bus_width = 8,
				},
			},
		};

		ep = of_graph_get_endpoint_by_regs(of, 0, i);
		if (!ep) {
			dev_err(ceudev->dev,
				"No subdevice connected on endpoint %u.\n", i);
			ret = -ENODEV;
			goto error_cleanup;
		}

		ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep), &fw_ep);
		if (ret) {
			dev_err(ceudev->dev,
				"Unable to parse endpoint #%u: %d.\n", i, ret);
			goto error_cleanup;
		}

		/* Setup the ceu subdevice and the async subdevice. */
		ceu_sd = v4l2_async_notifier_add_fwnode_remote_subdev(
				&ceudev->notifier, of_fwnode_handle(ep),
				struct ceu_subdev);
		if (IS_ERR(ceu_sd)) {
			ret = PTR_ERR(ceu_sd);
			goto error_cleanup;
		}
		ceu_sd->mbus_flags = fw_ep.bus.parallel.flags;
		ceudev->subdevs[i] = ceu_sd;

		of_node_put(ep);
	}

	return num_ep;

error_cleanup:
	v4l2_async_notifier_cleanup(&ceudev->notifier);
	of_node_put(ep);
	return ret;
}

/*
 * struct ceu_data - Platform specific CEU data
 * @irq_mask: CETCR mask with all interrupt sources enabled. The mask differs
 *	      between SH4 and RZ platforms.
 */
struct ceu_data {
	u32 irq_mask;
};

static const struct ceu_data ceu_data_rz = {
	.irq_mask = CEU_CETCR_ALL_IRQS_RZ,
};

static const struct ceu_data ceu_data_sh4 = {
	.irq_mask = CEU_CETCR_ALL_IRQS_SH4,
};

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ceu_of_match[] = {
	{ .compatible = "renesas,r7s72100-ceu", .data = &ceu_data_rz },
	{ .compatible = "renesas,r8a7740-ceu", .data = &ceu_data_rz },
	{ }
};
MODULE_DEVICE_TABLE(of, ceu_of_match);
#endif

static int ceu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct ceu_data *ceu_data;
	struct ceu_device *ceudev;
	struct resource *res;
	unsigned int irq;
	int num_subdevs;
	int ret;

	ceudev = kzalloc(sizeof(*ceudev), GFP_KERNEL);
	if (!ceudev)
		return -ENOMEM;

	platform_set_drvdata(pdev, ceudev);
	ceudev->dev = dev;

	INIT_LIST_HEAD(&ceudev->capture);
	spin_lock_init(&ceudev->lock);
	mutex_init(&ceudev->mlock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ceudev->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ceudev->base)) {
		ret = PTR_ERR(ceudev->base);
		goto error_free_ceudev;
	}

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		goto error_free_ceudev;
	irq = ret;

	ret = devm_request_irq(dev, irq, ceu_irq,
			       0, dev_name(dev), ceudev);
	if (ret) {
		dev_err(&pdev->dev, "Unable to request CEU interrupt.\n");
		goto error_free_ceudev;
	}

	pm_runtime_enable(dev);

	ret = v4l2_device_register(dev, &ceudev->v4l2_dev);
	if (ret)
		goto error_pm_disable;

	v4l2_async_notifier_init(&ceudev->notifier);

	if (IS_ENABLED(CONFIG_OF) && dev->of_node) {
		ceu_data = of_device_get_match_data(dev);
		num_subdevs = ceu_parse_dt(ceudev);
	} else if (dev->platform_data) {
		/* Assume SH4 if booting with platform data. */
		ceu_data = &ceu_data_sh4;
		num_subdevs = ceu_parse_platform_data(ceudev,
						      dev->platform_data);
	} else {
		num_subdevs = -EINVAL;
	}

	if (num_subdevs < 0) {
		ret = num_subdevs;
		goto error_v4l2_unregister;
	}
	ceudev->irq_mask = ceu_data->irq_mask;

	ceudev->notifier.v4l2_dev	= &ceudev->v4l2_dev;
	ceudev->notifier.ops		= &ceu_notify_ops;
	ret = v4l2_async_notifier_register(&ceudev->v4l2_dev,
					   &ceudev->notifier);
	if (ret)
		goto error_cleanup;

	dev_info(dev, "Renesas Capture Engine Unit %s\n", dev_name(dev));

	return 0;

error_cleanup:
	v4l2_async_notifier_cleanup(&ceudev->notifier);
error_v4l2_unregister:
	v4l2_device_unregister(&ceudev->v4l2_dev);
error_pm_disable:
	pm_runtime_disable(dev);
error_free_ceudev:
	kfree(ceudev);

	return ret;
}

static int ceu_remove(struct platform_device *pdev)
{
	struct ceu_device *ceudev = platform_get_drvdata(pdev);

	pm_runtime_disable(ceudev->dev);

	v4l2_async_notifier_unregister(&ceudev->notifier);

	v4l2_async_notifier_cleanup(&ceudev->notifier);

	v4l2_device_unregister(&ceudev->v4l2_dev);

	video_unregister_device(&ceudev->vdev);

	return 0;
}

static const struct dev_pm_ops ceu_pm_ops = {
	SET_RUNTIME_PM_OPS(ceu_runtime_suspend,
			   ceu_runtime_resume,
			   NULL)
};

static struct platform_driver ceu_driver = {
	.driver		= {
		.name	= DRIVER_NAME,
		.pm	= &ceu_pm_ops,
		.of_match_table = of_match_ptr(ceu_of_match),
	},
	.probe		= ceu_probe,
	.remove		= ceu_remove,
};

module_platform_driver(ceu_driver);

MODULE_DESCRIPTION("Renesas CEU camera driver");
MODULE_AUTHOR("Jacopo Mondi <jacopo+renesas@jmondi.org>");
MODULE_LICENSE("GPL v2");
