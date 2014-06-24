/*
 * SoC-camera host driver for Renesas R-Car VIN unit
 *
 * Copyright (C) 2011-2013 Renesas Solutions Corp.
 * Copyright (C) 2013 Cogent Embedded, Inc., <source@cogentembedded.com>
 *
 * Based on V4L2 Driver for SuperH Mobile CEU interface "sh_mobile_ceu_camera.c"
 *
 * Copyright (C) 2008 Magnus Damm
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_data/camera-rcar.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/soc_camera.h>
#include <media/soc_mediabus.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>

#include "soc_scale_crop.h"

#define DRV_NAME "rcar_vin"

/* Register offsets for R-Car VIN */
#define VNMC_REG	0x00	/* Video n Main Control Register */
#define VNMS_REG	0x04	/* Video n Module Status Register */
#define VNFC_REG	0x08	/* Video n Frame Capture Register */
#define VNSLPRC_REG	0x0C	/* Video n Start Line Pre-Clip Register */
#define VNELPRC_REG	0x10	/* Video n End Line Pre-Clip Register */
#define VNSPPRC_REG	0x14	/* Video n Start Pixel Pre-Clip Register */
#define VNEPPRC_REG	0x18	/* Video n End Pixel Pre-Clip Register */
#define VNSLPOC_REG	0x1C	/* Video n Start Line Post-Clip Register */
#define VNELPOC_REG	0x20	/* Video n End Line Post-Clip Register */
#define VNSPPOC_REG	0x24	/* Video n Start Pixel Post-Clip Register */
#define VNEPPOC_REG	0x28	/* Video n End Pixel Post-Clip Register */
#define VNIS_REG	0x2C	/* Video n Image Stride Register */
#define VNMB_REG(m)	(0x30 + ((m) << 2)) /* Video n Memory Base m Register */
#define VNIE_REG	0x40	/* Video n Interrupt Enable Register */
#define VNINTS_REG	0x44	/* Video n Interrupt Status Register */
#define VNSI_REG	0x48	/* Video n Scanline Interrupt Register */
#define VNMTC_REG	0x4C	/* Video n Memory Transfer Control Register */
#define VNYS_REG	0x50	/* Video n Y Scale Register */
#define VNXS_REG	0x54	/* Video n X Scale Register */
#define VNDMR_REG	0x58	/* Video n Data Mode Register */
#define VNDMR2_REG	0x5C	/* Video n Data Mode Register 2 */
#define VNUVAOF_REG	0x60	/* Video n UV Address Offset Register */

/* Register bit fields for R-Car VIN */
/* Video n Main Control Register bits */
#define VNMC_FOC		(1 << 21)
#define VNMC_YCAL		(1 << 19)
#define VNMC_INF_YUV8_BT656	(0 << 16)
#define VNMC_INF_YUV8_BT601	(1 << 16)
#define VNMC_INF_YUV10_BT656	(2 << 16)
#define VNMC_INF_YUV10_BT601	(3 << 16)
#define VNMC_INF_YUV16		(5 << 16)
#define VNMC_VUP		(1 << 10)
#define VNMC_IM_ODD		(0 << 3)
#define VNMC_IM_ODD_EVEN	(1 << 3)
#define VNMC_IM_EVEN		(2 << 3)
#define VNMC_IM_FULL		(3 << 3)
#define VNMC_BPS		(1 << 1)
#define VNMC_ME			(1 << 0)

/* Video n Module Status Register bits */
#define VNMS_FBS_MASK		(3 << 3)
#define VNMS_FBS_SHIFT		3
#define VNMS_AV			(1 << 1)
#define VNMS_CA			(1 << 0)

/* Video n Frame Capture Register bits */
#define VNFC_C_FRAME		(1 << 1)
#define VNFC_S_FRAME		(1 << 0)

/* Video n Interrupt Enable Register bits */
#define VNIE_FIE		(1 << 4)
#define VNIE_EFE		(1 << 1)

/* Video n Data Mode Register bits */
#define VNDMR_EXRGB		(1 << 8)
#define VNDMR_BPSM		(1 << 4)
#define VNDMR_DTMD_YCSEP	(1 << 1)
#define VNDMR_DTMD_ARGB1555	(1 << 0)

/* Video n Data Mode Register 2 bits */
#define VNDMR2_VPS		(1 << 30)
#define VNDMR2_HPS		(1 << 29)
#define VNDMR2_FTEV		(1 << 17)

#define VIN_MAX_WIDTH		2048
#define VIN_MAX_HEIGHT		2048

enum chip_id {
	RCAR_GEN2,
	RCAR_H1,
	RCAR_M1,
	RCAR_E1,
};

enum rcar_vin_state {
	STOPPED = 0,
	RUNNING,
	STOPPING,
};

struct rcar_vin_priv {
	void __iomem			*base;
	spinlock_t			lock;
	int				sequence;
	/* State of the VIN module in capturing mode */
	enum rcar_vin_state		state;
	struct rcar_vin_platform_data	*pdata;
	struct soc_camera_host		ici;
	struct list_head		capture;
#define MAX_BUFFER_NUM			3
	struct vb2_buffer		*queue_buf[MAX_BUFFER_NUM];
	struct vb2_alloc_ctx		*alloc_ctx;
	enum v4l2_field			field;
	unsigned int			vb_count;
	unsigned int			nr_hw_slots;
	bool				request_to_stop;
	struct completion		capture_stop;
	enum chip_id			chip;
};

#define is_continuous_transfer(priv)	(priv->vb_count > MAX_BUFFER_NUM)

struct rcar_vin_buffer {
	struct vb2_buffer		vb;
	struct list_head		list;
};

#define to_buf_list(vb2_buffer)	(&container_of(vb2_buffer, \
						       struct rcar_vin_buffer, \
						       vb)->list)

struct rcar_vin_cam {
	/* VIN offsets within the camera output, before the VIN scaler */
	unsigned int			vin_left;
	unsigned int			vin_top;
	/* Client output, as seen by the VIN */
	unsigned int			width;
	unsigned int			height;
	/*
	 * User window from S_CROP / G_CROP, produced by client cropping and
	 * scaling, VIN scaling and VIN cropping, mapped back onto the client
	 * input window
	 */
	struct v4l2_rect		subrect;
	/* Camera cropping rectangle */
	struct v4l2_rect		rect;
	const struct soc_mbus_pixelfmt	*extra_fmt;
};

/*
 * .queue_setup() is called to check whether the driver can accept the requested
 * number of buffers and to fill in plane sizes for the current frame format if
 * required
 */
static int rcar_vin_videobuf_setup(struct vb2_queue *vq,
				   const struct v4l2_format *fmt,
				   unsigned int *count,
				   unsigned int *num_planes,
				   unsigned int sizes[], void *alloc_ctxs[])
{
	struct soc_camera_device *icd = soc_camera_from_vb2q(vq);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct rcar_vin_priv *priv = ici->priv;

	if (fmt) {
		const struct soc_camera_format_xlate *xlate;
		unsigned int bytes_per_line;
		int ret;

		xlate = soc_camera_xlate_by_fourcc(icd,
						   fmt->fmt.pix.pixelformat);
		if (!xlate)
			return -EINVAL;
		ret = soc_mbus_bytes_per_line(fmt->fmt.pix.width,
					      xlate->host_fmt);
		if (ret < 0)
			return ret;

		bytes_per_line = max_t(u32, fmt->fmt.pix.bytesperline, ret);

		ret = soc_mbus_image_size(xlate->host_fmt, bytes_per_line,
					  fmt->fmt.pix.height);
		if (ret < 0)
			return ret;

		sizes[0] = max_t(u32, fmt->fmt.pix.sizeimage, ret);
	} else {
		/* Called from VIDIOC_REQBUFS or in compatibility mode */
		sizes[0] = icd->sizeimage;
	}

	alloc_ctxs[0] = priv->alloc_ctx;

	if (!vq->num_buffers)
		priv->sequence = 0;

	if (!*count)
		*count = 2;
	priv->vb_count = *count;

	*num_planes = 1;

	/* Number of hardware slots */
	if (is_continuous_transfer(priv))
		priv->nr_hw_slots = MAX_BUFFER_NUM;
	else
		priv->nr_hw_slots = 1;

	dev_dbg(icd->parent, "count=%d, size=%u\n", *count, sizes[0]);

	return 0;
}

static int rcar_vin_setup(struct rcar_vin_priv *priv)
{
	struct soc_camera_device *icd = priv->ici.icd;
	struct rcar_vin_cam *cam = icd->host_priv;
	u32 vnmc, dmr, interrupts;
	bool progressive = false, output_is_yuv = false;

	switch (priv->field) {
	case V4L2_FIELD_TOP:
		vnmc = VNMC_IM_ODD;
		break;
	case V4L2_FIELD_BOTTOM:
		vnmc = VNMC_IM_EVEN;
		break;
	case V4L2_FIELD_INTERLACED:
	case V4L2_FIELD_INTERLACED_TB:
		vnmc = VNMC_IM_FULL;
		break;
	case V4L2_FIELD_INTERLACED_BT:
		vnmc = VNMC_IM_FULL | VNMC_FOC;
		break;
	case V4L2_FIELD_NONE:
		if (is_continuous_transfer(priv)) {
			vnmc = VNMC_IM_ODD_EVEN;
			progressive = true;
		} else {
			vnmc = VNMC_IM_ODD;
		}
		break;
	default:
		vnmc = VNMC_IM_ODD;
		break;
	}

	/* input interface */
	switch (icd->current_fmt->code) {
	case V4L2_MBUS_FMT_YUYV8_1X16:
		/* BT.601/BT.1358 16bit YCbCr422 */
		vnmc |= VNMC_INF_YUV16;
		break;
	case V4L2_MBUS_FMT_YUYV8_2X8:
		/* BT.656 8bit YCbCr422 or BT.601 8bit YCbCr422 */
		vnmc |= priv->pdata->flags & RCAR_VIN_BT656 ?
			VNMC_INF_YUV8_BT656 : VNMC_INF_YUV8_BT601;
		break;
	case V4L2_MBUS_FMT_YUYV10_2X10:
		/* BT.656 10bit YCbCr422 or BT.601 10bit YCbCr422 */
		vnmc |= priv->pdata->flags & RCAR_VIN_BT656 ?
			VNMC_INF_YUV10_BT656 : VNMC_INF_YUV10_BT601;
		break;
	default:
		break;
	}

	/* output format */
	switch (icd->current_fmt->host_fmt->fourcc) {
	case V4L2_PIX_FMT_NV16:
		iowrite32(ALIGN(cam->width * cam->height, 0x80),
			  priv->base + VNUVAOF_REG);
		dmr = VNDMR_DTMD_YCSEP;
		output_is_yuv = true;
		break;
	case V4L2_PIX_FMT_YUYV:
		dmr = VNDMR_BPSM;
		output_is_yuv = true;
		break;
	case V4L2_PIX_FMT_UYVY:
		dmr = 0;
		output_is_yuv = true;
		break;
	case V4L2_PIX_FMT_RGB555X:
		dmr = VNDMR_DTMD_ARGB1555;
		break;
	case V4L2_PIX_FMT_RGB565:
		dmr = 0;
		break;
	case V4L2_PIX_FMT_RGB32:
		if (priv->chip == RCAR_GEN2 || priv->chip == RCAR_H1 ||
		    priv->chip == RCAR_E1) {
			dmr = VNDMR_EXRGB;
			break;
		}
	default:
		dev_warn(icd->parent, "Invalid fourcc format (0x%x)\n",
			 icd->current_fmt->host_fmt->fourcc);
		return -EINVAL;
	}

	/* Always update on field change */
	vnmc |= VNMC_VUP;

	/* If input and output use the same colorspace, use bypass mode */
	if (output_is_yuv)
		vnmc |= VNMC_BPS;

	/* progressive or interlaced mode */
	interrupts = progressive ? VNIE_FIE | VNIE_EFE : VNIE_EFE;

	/* ack interrupts */
	iowrite32(interrupts, priv->base + VNINTS_REG);
	/* enable interrupts */
	iowrite32(interrupts, priv->base + VNIE_REG);
	/* start capturing */
	iowrite32(dmr, priv->base + VNDMR_REG);
	iowrite32(vnmc | VNMC_ME, priv->base + VNMC_REG);

	return 0;
}

static void rcar_vin_capture(struct rcar_vin_priv *priv)
{
	if (is_continuous_transfer(priv))
		/* Continuous Frame Capture Mode */
		iowrite32(VNFC_C_FRAME, priv->base + VNFC_REG);
	else
		/* Single Frame Capture Mode */
		iowrite32(VNFC_S_FRAME, priv->base + VNFC_REG);
}

static void rcar_vin_request_capture_stop(struct rcar_vin_priv *priv)
{
	priv->state = STOPPING;

	/* set continuous & single transfer off */
	iowrite32(0, priv->base + VNFC_REG);
	/* disable capture (release DMA buffer), reset */
	iowrite32(ioread32(priv->base + VNMC_REG) & ~VNMC_ME,
		  priv->base + VNMC_REG);

	/* update the status if stopped already */
	if (!(ioread32(priv->base + VNMS_REG) & VNMS_CA))
		priv->state = STOPPED;
}

static int rcar_vin_get_free_hw_slot(struct rcar_vin_priv *priv)
{
	int slot;

	for (slot = 0; slot < priv->nr_hw_slots; slot++)
		if (priv->queue_buf[slot] == NULL)
			return slot;

	return -1;
}

static int rcar_vin_hw_ready(struct rcar_vin_priv *priv)
{
	/* Ensure all HW slots are filled */
	return rcar_vin_get_free_hw_slot(priv) < 0 ? 1 : 0;
}

/* Moves a buffer from the queue to the HW slots */
static int rcar_vin_fill_hw_slot(struct rcar_vin_priv *priv)
{
	struct vb2_buffer *vb;
	dma_addr_t phys_addr_top;
	int slot;

	if (list_empty(&priv->capture))
		return 0;

	/* Find a free HW slot */
	slot = rcar_vin_get_free_hw_slot(priv);
	if (slot < 0)
		return 0;

	vb = &list_entry(priv->capture.next, struct rcar_vin_buffer, list)->vb;
	list_del_init(to_buf_list(vb));
	priv->queue_buf[slot] = vb;
	phys_addr_top = vb2_dma_contig_plane_dma_addr(vb, 0);
	iowrite32(phys_addr_top, priv->base + VNMB_REG(slot));

	return 1;
}

static void rcar_vin_videobuf_queue(struct vb2_buffer *vb)
{
	struct soc_camera_device *icd = soc_camera_from_vb2q(vb->vb2_queue);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct rcar_vin_priv *priv = ici->priv;
	unsigned long size;

	size = icd->sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(icd->parent, "Buffer #%d too small (%lu < %lu)\n",
			vb->v4l2_buf.index, vb2_plane_size(vb, 0), size);
		goto error;
	}

	vb2_set_plane_payload(vb, 0, size);

	dev_dbg(icd->parent, "%s (vb=0x%p) 0x%p %lu\n", __func__,
		vb, vb2_plane_vaddr(vb, 0), vb2_get_plane_payload(vb, 0));

	spin_lock_irq(&priv->lock);

	list_add_tail(to_buf_list(vb), &priv->capture);
	rcar_vin_fill_hw_slot(priv);

	/* If we weren't running, and have enough buffers, start capturing! */
	if (priv->state != RUNNING && rcar_vin_hw_ready(priv)) {
		if (rcar_vin_setup(priv)) {
			/* Submit error */
			list_del_init(to_buf_list(vb));
			spin_unlock_irq(&priv->lock);
			goto error;
		}
		priv->request_to_stop = false;
		init_completion(&priv->capture_stop);
		priv->state = RUNNING;
		rcar_vin_capture(priv);
	}

	spin_unlock_irq(&priv->lock);

	return;

error:
	vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
}

static void rcar_vin_videobuf_release(struct vb2_buffer *vb)
{
	struct soc_camera_device *icd = soc_camera_from_vb2q(vb->vb2_queue);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct rcar_vin_priv *priv = ici->priv;
	unsigned int i;
	int buf_in_use = 0;

	spin_lock_irq(&priv->lock);

	/* Is the buffer in use by the VIN hardware? */
	for (i = 0; i < MAX_BUFFER_NUM; i++) {
		if (priv->queue_buf[i] == vb) {
			buf_in_use = 1;
			break;
		}
	}

	if (buf_in_use) {
		while (priv->state != STOPPED) {

			/* issue stop if running */
			if (priv->state == RUNNING)
				rcar_vin_request_capture_stop(priv);

			/* wait until capturing has been stopped */
			if (priv->state == STOPPING) {
				priv->request_to_stop = true;
				spin_unlock_irq(&priv->lock);
				wait_for_completion(&priv->capture_stop);
				spin_lock_irq(&priv->lock);
			}
		}
		/*
		 * Capturing has now stopped. The buffer we have been asked
		 * to release could be any of the current buffers in use, so
		 * release all buffers that are in use by HW
		 */
		for (i = 0; i < MAX_BUFFER_NUM; i++) {
			if (priv->queue_buf[i]) {
				vb2_buffer_done(priv->queue_buf[i],
					VB2_BUF_STATE_ERROR);
				priv->queue_buf[i] = NULL;
			}
		}
	} else {
		list_del_init(to_buf_list(vb));
	}

	spin_unlock_irq(&priv->lock);
}

static int rcar_vin_videobuf_init(struct vb2_buffer *vb)
{
	INIT_LIST_HEAD(to_buf_list(vb));
	return 0;
}

static int rcar_vin_stop_streaming(struct vb2_queue *vq)
{
	struct soc_camera_device *icd = soc_camera_from_vb2q(vq);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct rcar_vin_priv *priv = ici->priv;
	struct list_head *buf_head, *tmp;

	spin_lock_irq(&priv->lock);
	list_for_each_safe(buf_head, tmp, &priv->capture)
		list_del_init(buf_head);
	spin_unlock_irq(&priv->lock);

	return 0;
}

static struct vb2_ops rcar_vin_vb2_ops = {
	.queue_setup	= rcar_vin_videobuf_setup,
	.buf_init	= rcar_vin_videobuf_init,
	.buf_cleanup	= rcar_vin_videobuf_release,
	.buf_queue	= rcar_vin_videobuf_queue,
	.stop_streaming	= rcar_vin_stop_streaming,
	.wait_prepare	= soc_camera_unlock,
	.wait_finish	= soc_camera_lock,
};

static irqreturn_t rcar_vin_irq(int irq, void *data)
{
	struct rcar_vin_priv *priv = data;
	u32 int_status;
	bool can_run = false, hw_stopped;
	int slot;
	unsigned int handled = 0;

	spin_lock(&priv->lock);

	int_status = ioread32(priv->base + VNINTS_REG);
	if (!int_status)
		goto done;
	/* ack interrupts */
	iowrite32(int_status, priv->base + VNINTS_REG);
	handled = 1;

	/* nothing to do if capture status is 'STOPPED' */
	if (priv->state == STOPPED)
		goto done;

	hw_stopped = !(ioread32(priv->base + VNMS_REG) & VNMS_CA);

	if (!priv->request_to_stop) {
		if (is_continuous_transfer(priv))
			slot = (ioread32(priv->base + VNMS_REG) &
				VNMS_FBS_MASK) >> VNMS_FBS_SHIFT;
		else
			slot = 0;

		priv->queue_buf[slot]->v4l2_buf.field = priv->field;
		priv->queue_buf[slot]->v4l2_buf.sequence = priv->sequence++;
		do_gettimeofday(&priv->queue_buf[slot]->v4l2_buf.timestamp);
		vb2_buffer_done(priv->queue_buf[slot], VB2_BUF_STATE_DONE);
		priv->queue_buf[slot] = NULL;

		if (priv->state != STOPPING)
			can_run = rcar_vin_fill_hw_slot(priv);

		if (hw_stopped || !can_run) {
			priv->state = STOPPED;
		} else if (is_continuous_transfer(priv) &&
			   list_empty(&priv->capture) &&
			   priv->state == RUNNING) {
			/*
			 * The continuous capturing requires an explicit stop
			 * operation when there is no buffer to be set into
			 * the VnMBm registers.
			 */
			rcar_vin_request_capture_stop(priv);
		} else {
			rcar_vin_capture(priv);
		}

	} else if (hw_stopped) {
		priv->state = STOPPED;
		priv->request_to_stop = false;
		complete(&priv->capture_stop);
	}

done:
	spin_unlock(&priv->lock);

	return IRQ_RETVAL(handled);
}

static int rcar_vin_add_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct rcar_vin_priv *priv = ici->priv;
	int i;

	for (i = 0; i < MAX_BUFFER_NUM; i++)
		priv->queue_buf[i] = NULL;

	pm_runtime_get_sync(ici->v4l2_dev.dev);

	dev_dbg(icd->parent, "R-Car VIN driver attached to camera %d\n",
		icd->devnum);

	return 0;
}

static void rcar_vin_remove_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct rcar_vin_priv *priv = ici->priv;
	struct vb2_buffer *vb;
	int i;

	/* disable capture, disable interrupts */
	iowrite32(ioread32(priv->base + VNMC_REG) & ~VNMC_ME,
		  priv->base + VNMC_REG);
	iowrite32(0, priv->base + VNIE_REG);

	priv->state = STOPPED;
	priv->request_to_stop = false;

	/* make sure active buffer is cancelled */
	spin_lock_irq(&priv->lock);
	for (i = 0; i < MAX_BUFFER_NUM; i++) {
		vb = priv->queue_buf[i];
		if (vb) {
			list_del_init(to_buf_list(vb));
			vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
		}
	}
	spin_unlock_irq(&priv->lock);

	pm_runtime_put(ici->v4l2_dev.dev);

	dev_dbg(icd->parent, "R-Car VIN driver detached from camera %d\n",
		icd->devnum);
}

/* Called with .host_lock held */
static int rcar_vin_clock_start(struct soc_camera_host *ici)
{
	/* VIN does not have "mclk" */
	return 0;
}

/* Called with .host_lock held */
static void rcar_vin_clock_stop(struct soc_camera_host *ici)
{
	/* VIN does not have "mclk" */
}

/* rect is guaranteed to not exceed the scaled camera rectangle */
static int rcar_vin_set_rect(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct rcar_vin_cam *cam = icd->host_priv;
	struct rcar_vin_priv *priv = ici->priv;
	unsigned int left_offset, top_offset;
	unsigned char dsize = 0;
	struct v4l2_rect *cam_subrect = &cam->subrect;

	dev_dbg(icd->parent, "Crop %ux%u@%u:%u\n",
		icd->user_width, icd->user_height, cam->vin_left, cam->vin_top);

	left_offset = cam->vin_left;
	top_offset = cam->vin_top;

	if (icd->current_fmt->host_fmt->fourcc == V4L2_PIX_FMT_RGB32 &&
	    priv->chip == RCAR_E1)
		dsize = 1;

	dev_dbg(icd->parent, "Cam %ux%u@%u:%u\n",
		cam->width, cam->height, cam->vin_left, cam->vin_top);
	dev_dbg(icd->parent, "Cam subrect %ux%u@%u:%u\n",
		cam_subrect->width, cam_subrect->height,
		cam_subrect->left, cam_subrect->top);

	/* Set Start/End Pixel/Line Pre-Clip */
	iowrite32(left_offset << dsize, priv->base + VNSPPRC_REG);
	iowrite32((left_offset + cam->width - 1) << dsize,
		  priv->base + VNEPPRC_REG);
	switch (priv->field) {
	case V4L2_FIELD_INTERLACED:
	case V4L2_FIELD_INTERLACED_TB:
	case V4L2_FIELD_INTERLACED_BT:
		iowrite32(top_offset / 2, priv->base + VNSLPRC_REG);
		iowrite32((top_offset + cam->height) / 2 - 1,
			  priv->base + VNELPRC_REG);
		break;
	default:
		iowrite32(top_offset, priv->base + VNSLPRC_REG);
		iowrite32(top_offset + cam->height - 1,
			  priv->base + VNELPRC_REG);
		break;
	}

	/* Set Start/End Pixel/Line Post-Clip */
	iowrite32(0, priv->base + VNSPPOC_REG);
	iowrite32(0, priv->base + VNSLPOC_REG);
	iowrite32((cam_subrect->width - 1) << dsize, priv->base + VNEPPOC_REG);
	switch (priv->field) {
	case V4L2_FIELD_INTERLACED:
	case V4L2_FIELD_INTERLACED_TB:
	case V4L2_FIELD_INTERLACED_BT:
		iowrite32(cam_subrect->height / 2 - 1,
			  priv->base + VNELPOC_REG);
		break;
	default:
		iowrite32(cam_subrect->height - 1, priv->base + VNELPOC_REG);
		break;
	}

	iowrite32(ALIGN(cam->width, 0x10), priv->base + VNIS_REG);

	return 0;
}

static void capture_stop_preserve(struct rcar_vin_priv *priv, u32 *vnmc)
{
	*vnmc = ioread32(priv->base + VNMC_REG);
	/* module disable */
	iowrite32(*vnmc & ~VNMC_ME, priv->base + VNMC_REG);
}

static void capture_restore(struct rcar_vin_priv *priv, u32 vnmc)
{
	unsigned long timeout = jiffies + 10 * HZ;

	/*
	 * Wait until the end of the current frame. It can take a long time,
	 * but if it has been aborted by a MRST1 reset, it should exit sooner.
	 */
	while ((ioread32(priv->base + VNMS_REG) & VNMS_AV) &&
		time_before(jiffies, timeout))
		msleep(1);

	if (time_after(jiffies, timeout)) {
		dev_err(priv->ici.v4l2_dev.dev,
			"Timeout waiting for frame end! Interface problem?\n");
		return;
	}

	iowrite32(vnmc, priv->base + VNMC_REG);
}

#define VIN_MBUS_FLAGS	(V4L2_MBUS_MASTER |		\
			 V4L2_MBUS_PCLK_SAMPLE_RISING |	\
			 V4L2_MBUS_HSYNC_ACTIVE_HIGH |	\
			 V4L2_MBUS_HSYNC_ACTIVE_LOW |	\
			 V4L2_MBUS_VSYNC_ACTIVE_HIGH |	\
			 V4L2_MBUS_VSYNC_ACTIVE_LOW |	\
			 V4L2_MBUS_DATA_ACTIVE_HIGH)

static int rcar_vin_set_bus_param(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct rcar_vin_priv *priv = ici->priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_mbus_config cfg;
	unsigned long common_flags;
	u32 vnmc;
	u32 val;
	int ret;

	capture_stop_preserve(priv, &vnmc);

	ret = v4l2_subdev_call(sd, video, g_mbus_config, &cfg);
	if (!ret) {
		common_flags = soc_mbus_config_compatible(&cfg, VIN_MBUS_FLAGS);
		if (!common_flags) {
			dev_warn(icd->parent,
				 "MBUS flags incompatible: camera 0x%x, host 0x%x\n",
				 cfg.flags, VIN_MBUS_FLAGS);
			return -EINVAL;
		}
	} else if (ret != -ENOIOCTLCMD) {
		return ret;
	} else {
		common_flags = VIN_MBUS_FLAGS;
	}

	/* Make choises, based on platform preferences */
	if ((common_flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH) &&
	    (common_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)) {
		if (priv->pdata->flags & RCAR_VIN_HSYNC_ACTIVE_LOW)
			common_flags &= ~V4L2_MBUS_HSYNC_ACTIVE_HIGH;
		else
			common_flags &= ~V4L2_MBUS_HSYNC_ACTIVE_LOW;
	}

	if ((common_flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH) &&
	    (common_flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)) {
		if (priv->pdata->flags & RCAR_VIN_VSYNC_ACTIVE_LOW)
			common_flags &= ~V4L2_MBUS_VSYNC_ACTIVE_HIGH;
		else
			common_flags &= ~V4L2_MBUS_VSYNC_ACTIVE_LOW;
	}

	cfg.flags = common_flags;
	ret = v4l2_subdev_call(sd, video, s_mbus_config, &cfg);
	if (ret < 0 && ret != -ENOIOCTLCMD)
		return ret;

	val = priv->field == V4L2_FIELD_NONE ? VNDMR2_FTEV : 0;
	if (!(common_flags & V4L2_MBUS_VSYNC_ACTIVE_LOW))
		val |= VNDMR2_VPS;
	if (!(common_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW))
		val |= VNDMR2_HPS;
	iowrite32(val, priv->base + VNDMR2_REG);

	ret = rcar_vin_set_rect(icd);
	if (ret < 0)
		return ret;

	capture_restore(priv, vnmc);

	return 0;
}

static int rcar_vin_try_bus_param(struct soc_camera_device *icd,
				  unsigned char buswidth)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_mbus_config cfg;
	int ret;

	ret = v4l2_subdev_call(sd, video, g_mbus_config, &cfg);
	if (ret == -ENOIOCTLCMD)
		return 0;
	else if (ret)
		return ret;

	if (buswidth > 24)
		return -EINVAL;

	/* check is there common mbus flags */
	ret = soc_mbus_config_compatible(&cfg, VIN_MBUS_FLAGS);
	if (ret)
		return 0;

	dev_warn(icd->parent,
		"MBUS flags incompatible: camera 0x%x, host 0x%x\n",
		 cfg.flags, VIN_MBUS_FLAGS);

	return -EINVAL;
}

static bool rcar_vin_packing_supported(const struct soc_mbus_pixelfmt *fmt)
{
	return	fmt->packing == SOC_MBUS_PACKING_NONE ||
		(fmt->bits_per_sample > 8 &&
		 fmt->packing == SOC_MBUS_PACKING_EXTEND16);
}

static const struct soc_mbus_pixelfmt rcar_vin_formats[] = {
	{
		.fourcc			= V4L2_PIX_FMT_NV16,
		.name			= "NV16",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_LE,
		.layout			= SOC_MBUS_LAYOUT_PLANAR_Y_C,
	},
	{
		.fourcc			= V4L2_PIX_FMT_UYVY,
		.name			= "UYVY",
		.bits_per_sample	= 16,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
		.layout			= SOC_MBUS_LAYOUT_PACKED,
	},
	{
		.fourcc			= V4L2_PIX_FMT_RGB565,
		.name			= "RGB565",
		.bits_per_sample	= 16,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
		.layout			= SOC_MBUS_LAYOUT_PACKED,
	},
	{
		.fourcc			= V4L2_PIX_FMT_RGB555X,
		.name			= "ARGB1555",
		.bits_per_sample	= 16,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
		.layout			= SOC_MBUS_LAYOUT_PACKED,
	},
	{
		.fourcc			= V4L2_PIX_FMT_RGB32,
		.name			= "RGB888",
		.bits_per_sample	= 32,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
		.layout			= SOC_MBUS_LAYOUT_PACKED,
	},
};

static int rcar_vin_get_formats(struct soc_camera_device *icd, unsigned int idx,
				struct soc_camera_format_xlate *xlate)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct device *dev = icd->parent;
	int ret, k, n;
	int formats = 0;
	struct rcar_vin_cam *cam;
	enum v4l2_mbus_pixelcode code;
	const struct soc_mbus_pixelfmt *fmt;

	ret = v4l2_subdev_call(sd, video, enum_mbus_fmt, idx, &code);
	if (ret < 0)
		return 0;

	fmt = soc_mbus_get_fmtdesc(code);
	if (!fmt) {
		dev_warn(dev, "unsupported format code #%u: %d\n", idx, code);
		return 0;
	}

	ret = rcar_vin_try_bus_param(icd, fmt->bits_per_sample);
	if (ret < 0)
		return 0;

	if (!icd->host_priv) {
		struct v4l2_mbus_framefmt mf;
		struct v4l2_rect rect;
		struct device *dev = icd->parent;
		int shift;

		ret = v4l2_subdev_call(sd, video, g_mbus_fmt, &mf);
		if (ret < 0)
			return ret;

		/* Cache current client geometry */
		ret = soc_camera_client_g_rect(sd, &rect);
		if (ret == -ENOIOCTLCMD) {
			/* Sensor driver doesn't support cropping */
			rect.left = 0;
			rect.top = 0;
			rect.width = mf.width;
			rect.height = mf.height;
		} else if (ret < 0) {
			return ret;
		}

		/*
		 * If sensor proposes too large format then try smaller ones:
		 * 1280x960, 640x480, 320x240
		 */
		for (shift = 0; shift < 3; shift++) {
			if (mf.width <= VIN_MAX_WIDTH &&
			    mf.height <= VIN_MAX_HEIGHT)
				break;

			mf.width = 1280 >> shift;
			mf.height = 960 >> shift;
			ret = v4l2_device_call_until_err(sd->v4l2_dev,
							 soc_camera_grp_id(icd),
							 video, s_mbus_fmt,
							 &mf);
			if (ret < 0)
				return ret;
		}

		if (shift == 3) {
			dev_err(dev,
				"Failed to configure the client below %ux%x\n",
				mf.width, mf.height);
			return -EIO;
		}

		dev_dbg(dev, "camera fmt %ux%u\n", mf.width, mf.height);

		cam = kzalloc(sizeof(*cam), GFP_KERNEL);
		if (!cam)
			return -ENOMEM;
		/*
		 * We are called with current camera crop,
		 * initialise subrect with it
		 */
		cam->rect = rect;
		cam->subrect = rect;
		cam->width = mf.width;
		cam->height = mf.height;

		icd->host_priv = cam;
	} else {
		cam = icd->host_priv;
	}

	/* Beginning of a pass */
	if (!idx)
		cam->extra_fmt = NULL;

	switch (code) {
	case V4L2_MBUS_FMT_YUYV8_1X16:
	case V4L2_MBUS_FMT_YUYV8_2X8:
	case V4L2_MBUS_FMT_YUYV10_2X10:
		if (cam->extra_fmt)
			break;

		/* Add all our formats that can be generated by VIN */
		cam->extra_fmt = rcar_vin_formats;

		n = ARRAY_SIZE(rcar_vin_formats);
		formats += n;
		for (k = 0; xlate && k < n; k++, xlate++) {
			xlate->host_fmt = &rcar_vin_formats[k];
			xlate->code = code;
			dev_dbg(dev, "Providing format %s using code %d\n",
				rcar_vin_formats[k].name, code);
		}
		break;
	default:
		if (!rcar_vin_packing_supported(fmt))
			return 0;

		dev_dbg(dev, "Providing format %s in pass-through mode\n",
			fmt->name);
		break;
	}

	/* Generic pass-through */
	formats++;
	if (xlate) {
		xlate->host_fmt = fmt;
		xlate->code = code;
		xlate++;
	}

	return formats;
}

static void rcar_vin_put_formats(struct soc_camera_device *icd)
{
	kfree(icd->host_priv);
	icd->host_priv = NULL;
}

static int rcar_vin_set_crop(struct soc_camera_device *icd,
			     const struct v4l2_crop *a)
{
	struct v4l2_crop a_writable = *a;
	const struct v4l2_rect *rect = &a_writable.c;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct rcar_vin_priv *priv = ici->priv;
	struct v4l2_crop cam_crop;
	struct rcar_vin_cam *cam = icd->host_priv;
	struct v4l2_rect *cam_rect = &cam_crop.c;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct device *dev = icd->parent;
	struct v4l2_mbus_framefmt mf;
	u32 vnmc;
	int ret, i;

	dev_dbg(dev, "S_CROP(%ux%u@%u:%u)\n", rect->width, rect->height,
		rect->left, rect->top);

	/* During camera cropping its output window can change too, stop VIN */
	capture_stop_preserve(priv, &vnmc);
	dev_dbg(dev, "VNMC_REG 0x%x\n", vnmc);

	/* Apply iterative camera S_CROP for new input window. */
	ret = soc_camera_client_s_crop(sd, &a_writable, &cam_crop,
				       &cam->rect, &cam->subrect);
	if (ret < 0)
		return ret;

	dev_dbg(dev, "camera cropped to %ux%u@%u:%u\n",
		cam_rect->width, cam_rect->height,
		cam_rect->left, cam_rect->top);

	/* On success cam_crop contains current camera crop */

	/* Retrieve camera output window */
	ret = v4l2_subdev_call(sd, video, g_mbus_fmt, &mf);
	if (ret < 0)
		return ret;

	if (mf.width > VIN_MAX_WIDTH || mf.height > VIN_MAX_HEIGHT)
		return -EINVAL;

	/* Cache camera output window */
	cam->width = mf.width;
	cam->height = mf.height;

	icd->user_width  = cam->width;
	icd->user_height = cam->height;

	cam->vin_left = rect->left & ~1;
	cam->vin_top = rect->top & ~1;

	/* Use VIN cropping to crop to the new window. */
	ret = rcar_vin_set_rect(icd);
	if (ret < 0)
		return ret;

	cam->subrect = *rect;

	dev_dbg(dev, "VIN cropped to %ux%u@%u:%u\n",
		icd->user_width, icd->user_height,
		cam->vin_left, cam->vin_top);

	/* Restore capture */
	for (i = 0; i < MAX_BUFFER_NUM; i++) {
		if (priv->queue_buf[i] && priv->state == STOPPED) {
			vnmc |= VNMC_ME;
			break;
		}
	}
	capture_restore(priv, vnmc);

	/* Even if only camera cropping succeeded */
	return ret;
}

static int rcar_vin_get_crop(struct soc_camera_device *icd,
			     struct v4l2_crop *a)
{
	struct rcar_vin_cam *cam = icd->host_priv;

	a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->c = cam->subrect;

	return 0;
}

/* Similar to set_crop multistage iterative algorithm */
static int rcar_vin_set_fmt(struct soc_camera_device *icd,
			    struct v4l2_format *f)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct rcar_vin_priv *priv = ici->priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct rcar_vin_cam *cam = icd->host_priv;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	struct device *dev = icd->parent;
	__u32 pixfmt = pix->pixelformat;
	const struct soc_camera_format_xlate *xlate;
	unsigned int vin_sub_width = 0, vin_sub_height = 0;
	int ret;
	bool can_scale;
	enum v4l2_field field;
	v4l2_std_id std;

	dev_dbg(dev, "S_FMT(pix=0x%x, %ux%u)\n",
		pixfmt, pix->width, pix->height);

	switch (pix->field) {
	default:
		pix->field = V4L2_FIELD_NONE;
		/* fall-through */
	case V4L2_FIELD_NONE:
	case V4L2_FIELD_TOP:
	case V4L2_FIELD_BOTTOM:
	case V4L2_FIELD_INTERLACED_TB:
	case V4L2_FIELD_INTERLACED_BT:
		field = pix->field;
		break;
	case V4L2_FIELD_INTERLACED:
		/* Query for standard if not explicitly mentioned _TB/_BT */
		ret = v4l2_subdev_call(sd, video, querystd, &std);
		if (ret < 0)
			std = V4L2_STD_625_50;

		field = std & V4L2_STD_625_50 ? V4L2_FIELD_INTERLACED_TB :
						V4L2_FIELD_INTERLACED_BT;
		break;
	}

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (!xlate) {
		dev_warn(dev, "Format %x not found\n", pixfmt);
		return -EINVAL;
	}
	/* Calculate client output geometry */
	soc_camera_calc_client_output(icd, &cam->rect, &cam->subrect, pix, &mf,
				      12);
	mf.field = pix->field;
	mf.colorspace = pix->colorspace;
	mf.code	 = xlate->code;

	switch (pixfmt) {
	case V4L2_PIX_FMT_RGB32:
		can_scale = priv->chip != RCAR_E1;
		break;
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_RGB555X:
		can_scale = true;
		break;
	default:
		can_scale = false;
		break;
	}

	dev_dbg(dev, "request camera output %ux%u\n", mf.width, mf.height);

	ret = soc_camera_client_scale(icd, &cam->rect, &cam->subrect,
				      &mf, &vin_sub_width, &vin_sub_height,
				      can_scale, 12);

	/* Done with the camera. Now see if we can improve the result */
	dev_dbg(dev, "Camera %d fmt %ux%u, requested %ux%u\n",
		ret, mf.width, mf.height, pix->width, pix->height);

	if (ret == -ENOIOCTLCMD)
		dev_dbg(dev, "Sensor doesn't support scaling\n");
	else if (ret < 0)
		return ret;

	if (mf.code != xlate->code)
		return -EINVAL;

	/* Prepare VIN crop */
	cam->width = mf.width;
	cam->height = mf.height;

	/* Use VIN scaling to scale to the requested user window. */

	/* We cannot scale up */
	if (pix->width > vin_sub_width)
		vin_sub_width = pix->width;

	if (pix->height > vin_sub_height)
		vin_sub_height = pix->height;

	pix->colorspace = mf.colorspace;

	if (!can_scale) {
		pix->width = vin_sub_width;
		pix->height = vin_sub_height;
	}

	/*
	 * We have calculated CFLCR, the actual configuration will be performed
	 * in rcar_vin_set_bus_param()
	 */

	dev_dbg(dev, "W: %u : %u, H: %u : %u\n",
		vin_sub_width, pix->width, vin_sub_height, pix->height);

	icd->current_fmt = xlate;

	priv->field = field;

	return 0;
}

static int rcar_vin_try_fmt(struct soc_camera_device *icd,
			    struct v4l2_format *f)
{
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_mbus_framefmt mf;
	__u32 pixfmt = pix->pixelformat;
	int width, height;
	int ret;

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (!xlate) {
		xlate = icd->current_fmt;
		dev_dbg(icd->parent, "Format %x not found, keeping %x\n",
			pixfmt, xlate->host_fmt->fourcc);
		pixfmt = xlate->host_fmt->fourcc;
		pix->pixelformat = pixfmt;
		pix->colorspace = icd->colorspace;
	}

	/* FIXME: calculate using depth and bus width */
	v4l_bound_align_image(&pix->width, 2, VIN_MAX_WIDTH, 1,
			      &pix->height, 4, VIN_MAX_HEIGHT, 2, 0);

	width = pix->width;
	height = pix->height;

	/* let soc-camera calculate these values */
	pix->bytesperline = 0;
	pix->sizeimage = 0;

	/* limit to sensor capabilities */
	mf.width = pix->width;
	mf.height = pix->height;
	mf.field = pix->field;
	mf.code = xlate->code;
	mf.colorspace = pix->colorspace;

	ret = v4l2_device_call_until_err(sd->v4l2_dev, soc_camera_grp_id(icd),
					 video, try_mbus_fmt, &mf);
	if (ret < 0)
		return ret;

	pix->width = mf.width;
	pix->height = mf.height;
	pix->field = mf.field;
	pix->colorspace = mf.colorspace;

	if (pixfmt == V4L2_PIX_FMT_NV16) {
		/* FIXME: check against rect_max after converting soc-camera */
		/* We can scale precisely, need a bigger image from camera */
		if (pix->width < width || pix->height < height) {
			/*
			 * We presume, the sensor behaves sanely, i.e. if
			 * requested a bigger rectangle, it will not return a
			 * smaller one.
			 */
			mf.width = VIN_MAX_WIDTH;
			mf.height = VIN_MAX_HEIGHT;
			ret = v4l2_device_call_until_err(sd->v4l2_dev,
							 soc_camera_grp_id(icd),
							 video, try_mbus_fmt,
							 &mf);
			if (ret < 0) {
				dev_err(icd->parent,
					"client try_fmt() = %d\n", ret);
				return ret;
			}
		}
		/* We will scale exactly */
		if (mf.width > width)
			pix->width = width;
		if (mf.height > height)
			pix->height = height;
	}

	return ret;
}

static unsigned int rcar_vin_poll(struct file *file, poll_table *pt)
{
	struct soc_camera_device *icd = file->private_data;

	return vb2_poll(&icd->vb2_vidq, file, pt);
}

static int rcar_vin_querycap(struct soc_camera_host *ici,
			     struct v4l2_capability *cap)
{
	strlcpy(cap->card, "R_Car_VIN", sizeof(cap->card));
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	return 0;
}

static int rcar_vin_init_videobuf2(struct vb2_queue *vq,
				   struct soc_camera_device *icd)
{
	vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vq->io_modes = VB2_MMAP | VB2_USERPTR;
	vq->drv_priv = icd;
	vq->ops = &rcar_vin_vb2_ops;
	vq->mem_ops = &vb2_dma_contig_memops;
	vq->buf_struct_size = sizeof(struct rcar_vin_buffer);
	vq->timestamp_flags  = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;

	return vb2_queue_init(vq);
}

static struct soc_camera_host_ops rcar_vin_host_ops = {
	.owner		= THIS_MODULE,
	.add		= rcar_vin_add_device,
	.remove		= rcar_vin_remove_device,
	.clock_start	= rcar_vin_clock_start,
	.clock_stop	= rcar_vin_clock_stop,
	.get_formats	= rcar_vin_get_formats,
	.put_formats	= rcar_vin_put_formats,
	.get_crop	= rcar_vin_get_crop,
	.set_crop	= rcar_vin_set_crop,
	.try_fmt	= rcar_vin_try_fmt,
	.set_fmt	= rcar_vin_set_fmt,
	.poll		= rcar_vin_poll,
	.querycap	= rcar_vin_querycap,
	.set_bus_param	= rcar_vin_set_bus_param,
	.init_videobuf2	= rcar_vin_init_videobuf2,
};

static struct platform_device_id rcar_vin_id_table[] = {
	{ "r8a7791-vin",  RCAR_GEN2 },
	{ "r8a7790-vin",  RCAR_GEN2 },
	{ "r8a7779-vin",  RCAR_H1 },
	{ "r8a7778-vin",  RCAR_M1 },
	{ "uPD35004-vin", RCAR_E1 },
	{},
};
MODULE_DEVICE_TABLE(platform, rcar_vin_id_table);

static int rcar_vin_probe(struct platform_device *pdev)
{
	struct rcar_vin_priv *priv;
	struct resource *mem;
	struct rcar_vin_platform_data *pdata;
	int irq, ret;

	pdata = pdev->dev.platform_data;
	if (!pdata || !pdata->flags) {
		dev_err(&pdev->dev, "platform data not set\n");
		return -EINVAL;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (mem == NULL)
		return -EINVAL;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0)
		return -EINVAL;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct rcar_vin_priv),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	ret = devm_request_irq(&pdev->dev, irq, rcar_vin_irq, IRQF_SHARED,
			       dev_name(&pdev->dev), priv);
	if (ret)
		return ret;

	priv->alloc_ctx = vb2_dma_contig_init_ctx(&pdev->dev);
	if (IS_ERR(priv->alloc_ctx))
		return PTR_ERR(priv->alloc_ctx);

	priv->ici.priv = priv;
	priv->ici.v4l2_dev.dev = &pdev->dev;
	priv->ici.nr = pdev->id;
	priv->ici.drv_name = dev_name(&pdev->dev);
	priv->ici.ops = &rcar_vin_host_ops;

	priv->pdata = pdata;
	priv->chip = pdev->id_entry->driver_data;
	spin_lock_init(&priv->lock);
	INIT_LIST_HEAD(&priv->capture);

	priv->state = STOPPED;

	pm_suspend_ignore_children(&pdev->dev, true);
	pm_runtime_enable(&pdev->dev);

	ret = soc_camera_host_register(&priv->ici);
	if (ret)
		goto cleanup;

	return 0;

cleanup:
	pm_runtime_disable(&pdev->dev);
	vb2_dma_contig_cleanup_ctx(priv->alloc_ctx);

	return ret;
}

static int rcar_vin_remove(struct platform_device *pdev)
{
	struct soc_camera_host *soc_host = to_soc_camera_host(&pdev->dev);
	struct rcar_vin_priv *priv = container_of(soc_host,
						  struct rcar_vin_priv, ici);

	soc_camera_host_unregister(soc_host);
	pm_runtime_disable(&pdev->dev);
	vb2_dma_contig_cleanup_ctx(priv->alloc_ctx);

	return 0;
}

static struct platform_driver rcar_vin_driver = {
	.probe		= rcar_vin_probe,
	.remove		= rcar_vin_remove,
	.driver		= {
		.name		= DRV_NAME,
		.owner		= THIS_MODULE,
	},
	.id_table	= rcar_vin_id_table,
};

module_platform_driver(rcar_vin_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rcar_vin");
MODULE_DESCRIPTION("Renesas R-Car VIN camera host driver");
