/*
 * V4L2 Driver for i.MX3x camera host
 *
 * Copyright (C) 2008
 * Guennadi Liakhovetski, DENX Software Engineering, <lg@denx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/videodev2.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/dma/ipu-dma.h>

#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/videobuf2-dma-contig.h>
#include <media/soc_camera.h>
#include <media/soc_mediabus.h>

#include <linux/platform_data/camera-mx3.h>
#include <linux/platform_data/dma-imx.h>

#define MX3_CAM_DRV_NAME "mx3-camera"

/* CMOS Sensor Interface Registers */
#define CSI_REG_START		0x60

#define CSI_SENS_CONF		(0x60 - CSI_REG_START)
#define CSI_SENS_FRM_SIZE	(0x64 - CSI_REG_START)
#define CSI_ACT_FRM_SIZE	(0x68 - CSI_REG_START)
#define CSI_OUT_FRM_CTRL	(0x6C - CSI_REG_START)
#define CSI_TST_CTRL		(0x70 - CSI_REG_START)
#define CSI_CCIR_CODE_1		(0x74 - CSI_REG_START)
#define CSI_CCIR_CODE_2		(0x78 - CSI_REG_START)
#define CSI_CCIR_CODE_3		(0x7C - CSI_REG_START)
#define CSI_FLASH_STROBE_1	(0x80 - CSI_REG_START)
#define CSI_FLASH_STROBE_2	(0x84 - CSI_REG_START)

#define CSI_SENS_CONF_VSYNC_POL_SHIFT		0
#define CSI_SENS_CONF_HSYNC_POL_SHIFT		1
#define CSI_SENS_CONF_DATA_POL_SHIFT		2
#define CSI_SENS_CONF_PIX_CLK_POL_SHIFT		3
#define CSI_SENS_CONF_SENS_PRTCL_SHIFT		4
#define CSI_SENS_CONF_SENS_CLKSRC_SHIFT		7
#define CSI_SENS_CONF_DATA_FMT_SHIFT		8
#define CSI_SENS_CONF_DATA_WIDTH_SHIFT		10
#define CSI_SENS_CONF_EXT_VSYNC_SHIFT		15
#define CSI_SENS_CONF_DIVRATIO_SHIFT		16

#define CSI_SENS_CONF_DATA_FMT_RGB_YUV444	(0UL << CSI_SENS_CONF_DATA_FMT_SHIFT)
#define CSI_SENS_CONF_DATA_FMT_YUV422		(2UL << CSI_SENS_CONF_DATA_FMT_SHIFT)
#define CSI_SENS_CONF_DATA_FMT_BAYER		(3UL << CSI_SENS_CONF_DATA_FMT_SHIFT)

#define MAX_VIDEO_MEM 16

struct mx3_camera_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_buffer			vb;
	struct list_head			queue;

	/* One descriptot per scatterlist (per frame) */
	struct dma_async_tx_descriptor		*txd;

	/* We have to "build" a scatterlist ourselves - one element per frame */
	struct scatterlist			sg;
};

/**
 * struct mx3_camera_dev - i.MX3x camera (CSI) object
 * @dev:		camera device, to which the coherent buffer is attached
 * @icd:		currently attached camera sensor
 * @clk:		pointer to clock
 * @base:		remapped register base address
 * @pdata:		platform data
 * @platform_flags:	platform flags
 * @mclk:		master clock frequency in Hz
 * @capture:		list of capture videobuffers
 * @lock:		protects video buffer lists
 * @active:		active video buffer
 * @idmac_channel:	array of pointers to IPU DMAC DMA channels
 * @soc_host:		embedded soc_host object
 */
struct mx3_camera_dev {
	/*
	 * i.MX3x is only supposed to handle one camera on its Camera Sensor
	 * Interface. If anyone ever builds hardware to enable more than one
	 * camera _simultaneously_, they will have to modify this driver too
	 */
	struct clk		*clk;

	void __iomem		*base;

	struct mx3_camera_pdata	*pdata;

	unsigned long		platform_flags;
	unsigned long		mclk;
	u16			width_flags;	/* max 15 bits */

	struct list_head	capture;
	spinlock_t		lock;		/* Protects video buffer lists */
	struct mx3_camera_buffer *active;
	size_t			buf_total;
	struct vb2_alloc_ctx	*alloc_ctx;
	enum v4l2_field		field;
	int			sequence;

	/* IDMAC / dmaengine interface */
	struct idmac_channel	*idmac_channel[1];	/* We need one channel */

	struct soc_camera_host	soc_host;
};

struct dma_chan_request {
	struct mx3_camera_dev	*mx3_cam;
	enum ipu_channel	id;
};

static u32 csi_reg_read(struct mx3_camera_dev *mx3, off_t reg)
{
	return __raw_readl(mx3->base + reg);
}

static void csi_reg_write(struct mx3_camera_dev *mx3, u32 value, off_t reg)
{
	__raw_writel(value, mx3->base + reg);
}

static struct mx3_camera_buffer *to_mx3_vb(struct vb2_buffer *vb)
{
	return container_of(vb, struct mx3_camera_buffer, vb);
}

/* Called from the IPU IDMAC ISR */
static void mx3_cam_dma_done(void *arg)
{
	struct idmac_tx_desc *desc = to_tx_desc(arg);
	struct dma_chan *chan = desc->txd.chan;
	struct idmac_channel *ichannel = to_idmac_chan(chan);
	struct mx3_camera_dev *mx3_cam = ichannel->client;

	dev_dbg(chan->device->dev, "callback cookie %d, active DMA 0x%08x\n",
		desc->txd.cookie, mx3_cam->active ? sg_dma_address(&mx3_cam->active->sg) : 0);

	spin_lock(&mx3_cam->lock);
	if (mx3_cam->active) {
		struct vb2_buffer *vb = &mx3_cam->active->vb;
		struct mx3_camera_buffer *buf = to_mx3_vb(vb);

		list_del_init(&buf->queue);
		v4l2_get_timestamp(&vb->v4l2_buf.timestamp);
		vb->v4l2_buf.field = mx3_cam->field;
		vb->v4l2_buf.sequence = mx3_cam->sequence++;
		vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
	}

	if (list_empty(&mx3_cam->capture)) {
		mx3_cam->active = NULL;
		spin_unlock(&mx3_cam->lock);

		/*
		 * stop capture - without further buffers IPU_CHA_BUF0_RDY will
		 * not get updated
		 */
		return;
	}

	mx3_cam->active = list_entry(mx3_cam->capture.next,
				     struct mx3_camera_buffer, queue);
	spin_unlock(&mx3_cam->lock);
}

/*
 * Videobuf operations
 */

/*
 * Calculate the __buffer__ (not data) size and number of buffers.
 */
static int mx3_videobuf_setup(struct vb2_queue *vq,
			const struct v4l2_format *fmt,
			unsigned int *count, unsigned int *num_planes,
			unsigned int sizes[], void *alloc_ctxs[])
{
	struct soc_camera_device *icd = soc_camera_from_vb2q(vq);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct mx3_camera_dev *mx3_cam = ici->priv;

	if (!mx3_cam->idmac_channel[0])
		return -EINVAL;

	if (fmt) {
		const struct soc_camera_format_xlate *xlate = soc_camera_xlate_by_fourcc(icd,
								fmt->fmt.pix.pixelformat);
		unsigned int bytes_per_line;
		int ret;

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

	alloc_ctxs[0] = mx3_cam->alloc_ctx;

	if (!vq->num_buffers)
		mx3_cam->sequence = 0;

	if (!*count)
		*count = 2;

	/* If *num_planes != 0, we have already verified *count. */
	if (!*num_planes &&
	    sizes[0] * *count + mx3_cam->buf_total > MAX_VIDEO_MEM * 1024 * 1024)
		*count = (MAX_VIDEO_MEM * 1024 * 1024 - mx3_cam->buf_total) /
			sizes[0];

	*num_planes = 1;

	return 0;
}

static enum pixel_fmt fourcc_to_ipu_pix(__u32 fourcc)
{
	/* Add more formats as need arises and test possibilities appear... */
	switch (fourcc) {
	case V4L2_PIX_FMT_RGB24:
		return IPU_PIX_FMT_RGB24;
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_RGB565:
	default:
		return IPU_PIX_FMT_GENERIC;
	}
}

static void mx3_videobuf_queue(struct vb2_buffer *vb)
{
	struct soc_camera_device *icd = soc_camera_from_vb2q(vb->vb2_queue);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct mx3_camera_dev *mx3_cam = ici->priv;
	struct mx3_camera_buffer *buf = to_mx3_vb(vb);
	struct scatterlist *sg = &buf->sg;
	struct dma_async_tx_descriptor *txd;
	struct idmac_channel *ichan = mx3_cam->idmac_channel[0];
	struct idmac_video_param *video = &ichan->params.video;
	const struct soc_mbus_pixelfmt *host_fmt = icd->current_fmt->host_fmt;
	unsigned long flags;
	dma_cookie_t cookie;
	size_t new_size;

	new_size = icd->sizeimage;

	if (vb2_plane_size(vb, 0) < new_size) {
		dev_err(icd->parent, "Buffer #%d too small (%lu < %zu)\n",
			vb->v4l2_buf.index, vb2_plane_size(vb, 0), new_size);
		goto error;
	}

	if (!buf->txd) {
		sg_dma_address(sg)	= vb2_dma_contig_plane_dma_addr(vb, 0);
		sg_dma_len(sg)		= new_size;

		txd = dmaengine_prep_slave_sg(
			&ichan->dma_chan, sg, 1, DMA_DEV_TO_MEM,
			DMA_PREP_INTERRUPT);
		if (!txd)
			goto error;

		txd->callback_param	= txd;
		txd->callback		= mx3_cam_dma_done;

		buf->txd		= txd;
	} else {
		txd = buf->txd;
	}

	vb2_set_plane_payload(vb, 0, new_size);

	/* This is the configuration of one sg-element */
	video->out_pixel_fmt = fourcc_to_ipu_pix(host_fmt->fourcc);

	if (video->out_pixel_fmt == IPU_PIX_FMT_GENERIC) {
		/*
		 * If the IPU DMA channel is configured to transfer generic
		 * 8-bit data, we have to set up the geometry parameters
		 * correctly, according to the current pixel format. The DMA
		 * horizontal parameters in this case are expressed in bytes,
		 * not in pixels.
		 */
		video->out_width	= icd->bytesperline;
		video->out_height	= icd->user_height;
		video->out_stride	= icd->bytesperline;
	} else {
		/*
		 * For IPU known formats the pixel unit will be managed
		 * successfully by the IPU code
		 */
		video->out_width	= icd->user_width;
		video->out_height	= icd->user_height;
		video->out_stride	= icd->user_width;
	}

#ifdef DEBUG
	/* helps to see what DMA actually has written */
	if (vb2_plane_vaddr(vb, 0))
		memset(vb2_plane_vaddr(vb, 0), 0xaa, vb2_get_plane_payload(vb, 0));
#endif

	spin_lock_irqsave(&mx3_cam->lock, flags);
	list_add_tail(&buf->queue, &mx3_cam->capture);

	if (!mx3_cam->active)
		mx3_cam->active = buf;

	spin_unlock_irq(&mx3_cam->lock);

	cookie = txd->tx_submit(txd);
	dev_dbg(icd->parent, "Submitted cookie %d DMA 0x%08x\n",
		cookie, sg_dma_address(&buf->sg));

	if (cookie >= 0)
		return;

	spin_lock_irq(&mx3_cam->lock);

	/* Submit error */
	list_del_init(&buf->queue);

	if (mx3_cam->active == buf)
		mx3_cam->active = NULL;

	spin_unlock_irqrestore(&mx3_cam->lock, flags);
error:
	vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
}

static void mx3_videobuf_release(struct vb2_buffer *vb)
{
	struct soc_camera_device *icd = soc_camera_from_vb2q(vb->vb2_queue);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct mx3_camera_dev *mx3_cam = ici->priv;
	struct mx3_camera_buffer *buf = to_mx3_vb(vb);
	struct dma_async_tx_descriptor *txd = buf->txd;
	unsigned long flags;

	dev_dbg(icd->parent,
		"Release%s DMA 0x%08x, queue %sempty\n",
		mx3_cam->active == buf ? " active" : "", sg_dma_address(&buf->sg),
		list_empty(&buf->queue) ? "" : "not ");

	spin_lock_irqsave(&mx3_cam->lock, flags);

	if (mx3_cam->active == buf)
		mx3_cam->active = NULL;

	/* Doesn't hurt also if the list is empty */
	list_del_init(&buf->queue);

	if (txd) {
		buf->txd = NULL;
		if (mx3_cam->idmac_channel[0])
			async_tx_ack(txd);
	}

	spin_unlock_irqrestore(&mx3_cam->lock, flags);

	mx3_cam->buf_total -= vb2_plane_size(vb, 0);
}

static int mx3_videobuf_init(struct vb2_buffer *vb)
{
	struct soc_camera_device *icd = soc_camera_from_vb2q(vb->vb2_queue);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct mx3_camera_dev *mx3_cam = ici->priv;
	struct mx3_camera_buffer *buf = to_mx3_vb(vb);

	if (!buf->txd) {
		/* This is for locking debugging only */
		INIT_LIST_HEAD(&buf->queue);
		sg_init_table(&buf->sg, 1);

		mx3_cam->buf_total += vb2_plane_size(vb, 0);
	}

	return 0;
}

static int mx3_stop_streaming(struct vb2_queue *q)
{
	struct soc_camera_device *icd = soc_camera_from_vb2q(q);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct mx3_camera_dev *mx3_cam = ici->priv;
	struct idmac_channel *ichan = mx3_cam->idmac_channel[0];
	struct mx3_camera_buffer *buf, *tmp;
	unsigned long flags;

	if (ichan) {
		struct dma_chan *chan = &ichan->dma_chan;
		chan->device->device_control(chan, DMA_PAUSE, 0);
	}

	spin_lock_irqsave(&mx3_cam->lock, flags);

	mx3_cam->active = NULL;

	list_for_each_entry_safe(buf, tmp, &mx3_cam->capture, queue) {
		list_del_init(&buf->queue);
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	}

	spin_unlock_irqrestore(&mx3_cam->lock, flags);

	return 0;
}

static struct vb2_ops mx3_videobuf_ops = {
	.queue_setup	= mx3_videobuf_setup,
	.buf_queue	= mx3_videobuf_queue,
	.buf_cleanup	= mx3_videobuf_release,
	.buf_init	= mx3_videobuf_init,
	.wait_prepare	= soc_camera_unlock,
	.wait_finish	= soc_camera_lock,
	.stop_streaming	= mx3_stop_streaming,
};

static int mx3_camera_init_videobuf(struct vb2_queue *q,
				     struct soc_camera_device *icd)
{
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->drv_priv = icd;
	q->ops = &mx3_videobuf_ops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct mx3_camera_buffer);
	q->timestamp_type = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;

	return vb2_queue_init(q);
}

/* First part of ipu_csi_init_interface() */
static void mx3_camera_activate(struct mx3_camera_dev *mx3_cam,
				struct soc_camera_device *icd)
{
	u32 conf;
	long rate;

	/* Set default size: ipu_csi_set_window_size() */
	csi_reg_write(mx3_cam, (640 - 1) | ((480 - 1) << 16), CSI_ACT_FRM_SIZE);
	/* ...and position to 0:0: ipu_csi_set_window_pos() */
	conf = csi_reg_read(mx3_cam, CSI_OUT_FRM_CTRL) & 0xffff0000;
	csi_reg_write(mx3_cam, conf, CSI_OUT_FRM_CTRL);

	/* We use only gated clock synchronisation mode so far */
	conf = 0 << CSI_SENS_CONF_SENS_PRTCL_SHIFT;

	/* Set generic data, platform-biggest bus-width */
	conf |= CSI_SENS_CONF_DATA_FMT_BAYER;

	if (mx3_cam->platform_flags & MX3_CAMERA_DATAWIDTH_15)
		conf |= 3 << CSI_SENS_CONF_DATA_WIDTH_SHIFT;
	else if (mx3_cam->platform_flags & MX3_CAMERA_DATAWIDTH_10)
		conf |= 2 << CSI_SENS_CONF_DATA_WIDTH_SHIFT;
	else if (mx3_cam->platform_flags & MX3_CAMERA_DATAWIDTH_8)
		conf |= 1 << CSI_SENS_CONF_DATA_WIDTH_SHIFT;
	else/* if (mx3_cam->platform_flags & MX3_CAMERA_DATAWIDTH_4)*/
		conf |= 0 << CSI_SENS_CONF_DATA_WIDTH_SHIFT;

	if (mx3_cam->platform_flags & MX3_CAMERA_CLK_SRC)
		conf |= 1 << CSI_SENS_CONF_SENS_CLKSRC_SHIFT;
	if (mx3_cam->platform_flags & MX3_CAMERA_EXT_VSYNC)
		conf |= 1 << CSI_SENS_CONF_EXT_VSYNC_SHIFT;
	if (mx3_cam->platform_flags & MX3_CAMERA_DP)
		conf |= 1 << CSI_SENS_CONF_DATA_POL_SHIFT;
	if (mx3_cam->platform_flags & MX3_CAMERA_PCP)
		conf |= 1 << CSI_SENS_CONF_PIX_CLK_POL_SHIFT;
	if (mx3_cam->platform_flags & MX3_CAMERA_HSP)
		conf |= 1 << CSI_SENS_CONF_HSYNC_POL_SHIFT;
	if (mx3_cam->platform_flags & MX3_CAMERA_VSP)
		conf |= 1 << CSI_SENS_CONF_VSYNC_POL_SHIFT;

	/* ipu_csi_init_interface() */
	csi_reg_write(mx3_cam, conf, CSI_SENS_CONF);

	clk_prepare_enable(mx3_cam->clk);
	rate = clk_round_rate(mx3_cam->clk, mx3_cam->mclk);
	dev_dbg(icd->parent, "Set SENS_CONF to %x, rate %ld\n", conf, rate);
	if (rate)
		clk_set_rate(mx3_cam->clk, rate);
}

/* Called with .host_lock held */
static int mx3_camera_add_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct mx3_camera_dev *mx3_cam = ici->priv;

	mx3_camera_activate(mx3_cam, icd);

	mx3_cam->buf_total = 0;

	dev_info(icd->parent, "MX3 Camera driver attached to camera %d\n",
		 icd->devnum);

	return 0;
}

/* Called with .host_lock held */
static void mx3_camera_remove_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct mx3_camera_dev *mx3_cam = ici->priv;
	struct idmac_channel **ichan = &mx3_cam->idmac_channel[0];

	if (*ichan) {
		dma_release_channel(&(*ichan)->dma_chan);
		*ichan = NULL;
	}

	clk_disable_unprepare(mx3_cam->clk);

	dev_info(icd->parent, "MX3 Camera driver detached from camera %d\n",
		 icd->devnum);
}

static int test_platform_param(struct mx3_camera_dev *mx3_cam,
			       unsigned char buswidth, unsigned long *flags)
{
	/*
	 * If requested data width is supported by the platform, use it or any
	 * possible lower value - i.MX31 is smart enough to shift bits
	 */
	if (buswidth > fls(mx3_cam->width_flags))
		return -EINVAL;

	/*
	 * Platform specified synchronization and pixel clock polarities are
	 * only a recommendation and are only used during probing. MX3x
	 * camera interface only works in master mode, i.e., uses HSYNC and
	 * VSYNC signals from the sensor
	 */
	*flags = V4L2_MBUS_MASTER |
		V4L2_MBUS_HSYNC_ACTIVE_HIGH |
		V4L2_MBUS_HSYNC_ACTIVE_LOW |
		V4L2_MBUS_VSYNC_ACTIVE_HIGH |
		V4L2_MBUS_VSYNC_ACTIVE_LOW |
		V4L2_MBUS_PCLK_SAMPLE_RISING |
		V4L2_MBUS_PCLK_SAMPLE_FALLING |
		V4L2_MBUS_DATA_ACTIVE_HIGH |
		V4L2_MBUS_DATA_ACTIVE_LOW;

	return 0;
}

static int mx3_camera_try_bus_param(struct soc_camera_device *icd,
				    const unsigned int depth)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct mx3_camera_dev *mx3_cam = ici->priv;
	struct v4l2_mbus_config cfg = {.type = V4L2_MBUS_PARALLEL,};
	unsigned long bus_flags, common_flags;
	int ret = test_platform_param(mx3_cam, depth, &bus_flags);

	dev_dbg(icd->parent, "request bus width %d bit: %d\n", depth, ret);

	if (ret < 0)
		return ret;

	ret = v4l2_subdev_call(sd, video, g_mbus_config, &cfg);
	if (!ret) {
		common_flags = soc_mbus_config_compatible(&cfg,
							  bus_flags);
		if (!common_flags) {
			dev_warn(icd->parent,
				 "Flags incompatible: camera 0x%x, host 0x%lx\n",
				 cfg.flags, bus_flags);
			return -EINVAL;
		}
	} else if (ret != -ENOIOCTLCMD) {
		return ret;
	}

	return 0;
}

static bool chan_filter(struct dma_chan *chan, void *arg)
{
	struct dma_chan_request *rq = arg;
	struct mx3_camera_pdata *pdata;

	if (!imx_dma_is_ipu(chan))
		return false;

	if (!rq)
		return false;

	pdata = rq->mx3_cam->soc_host.v4l2_dev.dev->platform_data;

	return rq->id == chan->chan_id &&
		pdata->dma_dev == chan->device->dev;
}

static const struct soc_mbus_pixelfmt mx3_camera_formats[] = {
	{
		.fourcc			= V4L2_PIX_FMT_SBGGR8,
		.name			= "Bayer BGGR (sRGB) 8 bit",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
		.layout			= SOC_MBUS_LAYOUT_PACKED,
	}, {
		.fourcc			= V4L2_PIX_FMT_GREY,
		.name			= "Monochrome 8 bit",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
		.layout			= SOC_MBUS_LAYOUT_PACKED,
	},
};

/* This will be corrected as we get more formats */
static bool mx3_camera_packing_supported(const struct soc_mbus_pixelfmt *fmt)
{
	return	fmt->packing == SOC_MBUS_PACKING_NONE ||
		(fmt->bits_per_sample == 8 &&
		 fmt->packing == SOC_MBUS_PACKING_2X8_PADHI) ||
		(fmt->bits_per_sample > 8 &&
		 fmt->packing == SOC_MBUS_PACKING_EXTEND16);
}

static int mx3_camera_get_formats(struct soc_camera_device *icd, unsigned int idx,
				  struct soc_camera_format_xlate *xlate)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct device *dev = icd->parent;
	int formats = 0, ret;
	enum v4l2_mbus_pixelcode code;
	const struct soc_mbus_pixelfmt *fmt;

	ret = v4l2_subdev_call(sd, video, enum_mbus_fmt, idx, &code);
	if (ret < 0)
		/* No more formats */
		return 0;

	fmt = soc_mbus_get_fmtdesc(code);
	if (!fmt) {
		dev_warn(icd->parent,
			 "Unsupported format code #%u: %d\n", idx, code);
		return 0;
	}

	/* This also checks support for the requested bits-per-sample */
	ret = mx3_camera_try_bus_param(icd, fmt->bits_per_sample);
	if (ret < 0)
		return 0;

	switch (code) {
	case V4L2_MBUS_FMT_SBGGR10_1X10:
		formats++;
		if (xlate) {
			xlate->host_fmt	= &mx3_camera_formats[0];
			xlate->code	= code;
			xlate++;
			dev_dbg(dev, "Providing format %s using code %d\n",
				mx3_camera_formats[0].name, code);
		}
		break;
	case V4L2_MBUS_FMT_Y10_1X10:
		formats++;
		if (xlate) {
			xlate->host_fmt	= &mx3_camera_formats[1];
			xlate->code	= code;
			xlate++;
			dev_dbg(dev, "Providing format %s using code %d\n",
				mx3_camera_formats[1].name, code);
		}
		break;
	default:
		if (!mx3_camera_packing_supported(fmt))
			return 0;
	}

	/* Generic pass-through */
	formats++;
	if (xlate) {
		xlate->host_fmt	= fmt;
		xlate->code	= code;
		dev_dbg(dev, "Providing format %c%c%c%c in pass-through mode\n",
			(fmt->fourcc >> (0*8)) & 0xFF,
			(fmt->fourcc >> (1*8)) & 0xFF,
			(fmt->fourcc >> (2*8)) & 0xFF,
			(fmt->fourcc >> (3*8)) & 0xFF);
		xlate++;
	}

	return formats;
}

static void configure_geometry(struct mx3_camera_dev *mx3_cam,
			       unsigned int width, unsigned int height,
			       const struct soc_mbus_pixelfmt *fmt)
{
	u32 ctrl, width_field, height_field;

	if (fourcc_to_ipu_pix(fmt->fourcc) == IPU_PIX_FMT_GENERIC) {
		/*
		 * As the CSI will be configured to output BAYER, here
		 * the width parameter count the number of samples to
		 * capture to complete the whole image width.
		 */
		unsigned int num, den;
		int ret = soc_mbus_samples_per_pixel(fmt, &num, &den);
		BUG_ON(ret < 0);
		width = width * num / den;
	}

	/* Setup frame size - this cannot be changed on-the-fly... */
	width_field = width - 1;
	height_field = height - 1;
	csi_reg_write(mx3_cam, width_field | (height_field << 16), CSI_SENS_FRM_SIZE);

	csi_reg_write(mx3_cam, width_field << 16, CSI_FLASH_STROBE_1);
	csi_reg_write(mx3_cam, (height_field << 16) | 0x22, CSI_FLASH_STROBE_2);

	csi_reg_write(mx3_cam, width_field | (height_field << 16), CSI_ACT_FRM_SIZE);

	/* ...and position */
	ctrl = csi_reg_read(mx3_cam, CSI_OUT_FRM_CTRL) & 0xffff0000;
	/* Sensor does the cropping */
	csi_reg_write(mx3_cam, ctrl | 0 | (0 << 8), CSI_OUT_FRM_CTRL);
}

static int acquire_dma_channel(struct mx3_camera_dev *mx3_cam)
{
	dma_cap_mask_t mask;
	struct dma_chan *chan;
	struct idmac_channel **ichan = &mx3_cam->idmac_channel[0];
	/* We have to use IDMAC_IC_7 for Bayer / generic data */
	struct dma_chan_request rq = {.mx3_cam = mx3_cam,
				      .id = IDMAC_IC_7};

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_cap_set(DMA_PRIVATE, mask);
	chan = dma_request_channel(mask, chan_filter, &rq);
	if (!chan)
		return -EBUSY;

	*ichan = to_idmac_chan(chan);
	(*ichan)->client = mx3_cam;

	return 0;
}

/*
 * FIXME: learn to use stride != width, then we can keep stride properly aligned
 * and support arbitrary (even) widths.
 */
static inline void stride_align(__u32 *width)
{
	if (ALIGN(*width, 8) < 4096)
		*width = ALIGN(*width, 8);
	else
		*width = *width &  ~7;
}

/*
 * As long as we don't implement host-side cropping and scaling, we can use
 * default g_crop and cropcap from soc_camera.c
 */
static int mx3_camera_set_crop(struct soc_camera_device *icd,
			       const struct v4l2_crop *a)
{
	struct v4l2_crop a_writable = *a;
	struct v4l2_rect *rect = &a_writable.c;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct mx3_camera_dev *mx3_cam = ici->priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct v4l2_mbus_framefmt mf;
	int ret;

	soc_camera_limit_side(&rect->left, &rect->width, 0, 2, 4096);
	soc_camera_limit_side(&rect->top, &rect->height, 0, 2, 4096);

	ret = v4l2_subdev_call(sd, video, s_crop, a);
	if (ret < 0)
		return ret;

	/* The capture device might have changed its output sizes */
	ret = v4l2_subdev_call(sd, video, g_mbus_fmt, &mf);
	if (ret < 0)
		return ret;

	if (mf.code != icd->current_fmt->code)
		return -EINVAL;

	if (mf.width & 7) {
		/* Ouch! We can only handle 8-byte aligned width... */
		stride_align(&mf.width);
		ret = v4l2_subdev_call(sd, video, s_mbus_fmt, &mf);
		if (ret < 0)
			return ret;
	}

	if (mf.width != icd->user_width || mf.height != icd->user_height)
		configure_geometry(mx3_cam, mf.width, mf.height,
				   icd->current_fmt->host_fmt);

	dev_dbg(icd->parent, "Sensor cropped %dx%d\n",
		mf.width, mf.height);

	icd->user_width		= mf.width;
	icd->user_height	= mf.height;

	return ret;
}

static int mx3_camera_set_fmt(struct soc_camera_device *icd,
			      struct v4l2_format *f)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct mx3_camera_dev *mx3_cam = ici->priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	int ret;

	xlate = soc_camera_xlate_by_fourcc(icd, pix->pixelformat);
	if (!xlate) {
		dev_warn(icd->parent, "Format %x not found\n",
			 pix->pixelformat);
		return -EINVAL;
	}

	stride_align(&pix->width);
	dev_dbg(icd->parent, "Set format %dx%d\n", pix->width, pix->height);

	/*
	 * Might have to perform a complete interface initialisation like in
	 * ipu_csi_init_interface() in mxc_v4l2_s_param(). Also consider
	 * mxc_v4l2_s_fmt()
	 */

	configure_geometry(mx3_cam, pix->width, pix->height, xlate->host_fmt);

	mf.width	= pix->width;
	mf.height	= pix->height;
	mf.field	= pix->field;
	mf.colorspace	= pix->colorspace;
	mf.code		= xlate->code;

	ret = v4l2_subdev_call(sd, video, s_mbus_fmt, &mf);
	if (ret < 0)
		return ret;

	if (mf.code != xlate->code)
		return -EINVAL;

	if (!mx3_cam->idmac_channel[0]) {
		ret = acquire_dma_channel(mx3_cam);
		if (ret < 0)
			return ret;
	}

	pix->width		= mf.width;
	pix->height		= mf.height;
	pix->field		= mf.field;
	mx3_cam->field		= mf.field;
	pix->colorspace		= mf.colorspace;
	icd->current_fmt	= xlate;

	dev_dbg(icd->parent, "Sensor set %dx%d\n", pix->width, pix->height);

	return ret;
}

static int mx3_camera_try_fmt(struct soc_camera_device *icd,
			      struct v4l2_format *f)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	__u32 pixfmt = pix->pixelformat;
	int ret;

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (pixfmt && !xlate) {
		dev_warn(icd->parent, "Format %x not found\n", pixfmt);
		return -EINVAL;
	}

	/* limit to MX3 hardware capabilities */
	if (pix->height > 4096)
		pix->height = 4096;
	if (pix->width > 4096)
		pix->width = 4096;

	/* limit to sensor capabilities */
	mf.width	= pix->width;
	mf.height	= pix->height;
	mf.field	= pix->field;
	mf.colorspace	= pix->colorspace;
	mf.code		= xlate->code;

	ret = v4l2_subdev_call(sd, video, try_mbus_fmt, &mf);
	if (ret < 0)
		return ret;

	pix->width	= mf.width;
	pix->height	= mf.height;
	pix->colorspace	= mf.colorspace;

	switch (mf.field) {
	case V4L2_FIELD_ANY:
		pix->field = V4L2_FIELD_NONE;
		break;
	case V4L2_FIELD_NONE:
		break;
	default:
		dev_err(icd->parent, "Field type %d unsupported.\n",
			mf.field);
		ret = -EINVAL;
	}

	return ret;
}

static int mx3_camera_reqbufs(struct soc_camera_device *icd,
			      struct v4l2_requestbuffers *p)
{
	return 0;
}

static unsigned int mx3_camera_poll(struct file *file, poll_table *pt)
{
	struct soc_camera_device *icd = file->private_data;

	return vb2_poll(&icd->vb2_vidq, file, pt);
}

static int mx3_camera_querycap(struct soc_camera_host *ici,
			       struct v4l2_capability *cap)
{
	/* cap->name is set by the firendly caller:-> */
	strlcpy(cap->card, "i.MX3x Camera", sizeof(cap->card));
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

	return 0;
}

static int mx3_camera_set_bus_param(struct soc_camera_device *icd)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct mx3_camera_dev *mx3_cam = ici->priv;
	struct v4l2_mbus_config cfg = {.type = V4L2_MBUS_PARALLEL,};
	u32 pixfmt = icd->current_fmt->host_fmt->fourcc;
	unsigned long bus_flags, common_flags;
	u32 dw, sens_conf;
	const struct soc_mbus_pixelfmt *fmt;
	int buswidth;
	int ret;
	const struct soc_camera_format_xlate *xlate;
	struct device *dev = icd->parent;

	fmt = soc_mbus_get_fmtdesc(icd->current_fmt->code);
	if (!fmt)
		return -EINVAL;

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (!xlate) {
		dev_warn(dev, "Format %x not found\n", pixfmt);
		return -EINVAL;
	}

	buswidth = fmt->bits_per_sample;
	ret = test_platform_param(mx3_cam, buswidth, &bus_flags);

	dev_dbg(dev, "requested bus width %d bit: %d\n", buswidth, ret);

	if (ret < 0)
		return ret;

	ret = v4l2_subdev_call(sd, video, g_mbus_config, &cfg);
	if (!ret) {
		common_flags = soc_mbus_config_compatible(&cfg,
							  bus_flags);
		if (!common_flags) {
			dev_warn(icd->parent,
				 "Flags incompatible: camera 0x%x, host 0x%lx\n",
				 cfg.flags, bus_flags);
			return -EINVAL;
		}
	} else if (ret != -ENOIOCTLCMD) {
		return ret;
	} else {
		common_flags = bus_flags;
	}

	dev_dbg(dev, "Flags cam: 0x%x host: 0x%lx common: 0x%lx\n",
		cfg.flags, bus_flags, common_flags);

	/* Make choices, based on platform preferences */
	if ((common_flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH) &&
	    (common_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)) {
		if (mx3_cam->platform_flags & MX3_CAMERA_HSP)
			common_flags &= ~V4L2_MBUS_HSYNC_ACTIVE_HIGH;
		else
			common_flags &= ~V4L2_MBUS_HSYNC_ACTIVE_LOW;
	}

	if ((common_flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH) &&
	    (common_flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)) {
		if (mx3_cam->platform_flags & MX3_CAMERA_VSP)
			common_flags &= ~V4L2_MBUS_VSYNC_ACTIVE_HIGH;
		else
			common_flags &= ~V4L2_MBUS_VSYNC_ACTIVE_LOW;
	}

	if ((common_flags & V4L2_MBUS_DATA_ACTIVE_HIGH) &&
	    (common_flags & V4L2_MBUS_DATA_ACTIVE_LOW)) {
		if (mx3_cam->platform_flags & MX3_CAMERA_DP)
			common_flags &= ~V4L2_MBUS_DATA_ACTIVE_HIGH;
		else
			common_flags &= ~V4L2_MBUS_DATA_ACTIVE_LOW;
	}

	if ((common_flags & V4L2_MBUS_PCLK_SAMPLE_RISING) &&
	    (common_flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)) {
		if (mx3_cam->platform_flags & MX3_CAMERA_PCP)
			common_flags &= ~V4L2_MBUS_PCLK_SAMPLE_RISING;
		else
			common_flags &= ~V4L2_MBUS_PCLK_SAMPLE_FALLING;
	}

	cfg.flags = common_flags;
	ret = v4l2_subdev_call(sd, video, s_mbus_config, &cfg);
	if (ret < 0 && ret != -ENOIOCTLCMD) {
		dev_dbg(dev, "camera s_mbus_config(0x%lx) returned %d\n",
			common_flags, ret);
		return ret;
	}

	/*
	 * So far only gated clock mode is supported. Add a line
	 *	(3 << CSI_SENS_CONF_SENS_PRTCL_SHIFT) |
	 * below and select the required mode when supporting other
	 * synchronisation protocols.
	 */
	sens_conf = csi_reg_read(mx3_cam, CSI_SENS_CONF) &
		~((1 << CSI_SENS_CONF_VSYNC_POL_SHIFT) |
		  (1 << CSI_SENS_CONF_HSYNC_POL_SHIFT) |
		  (1 << CSI_SENS_CONF_DATA_POL_SHIFT) |
		  (1 << CSI_SENS_CONF_PIX_CLK_POL_SHIFT) |
		  (3 << CSI_SENS_CONF_DATA_FMT_SHIFT) |
		  (3 << CSI_SENS_CONF_DATA_WIDTH_SHIFT));

	/* TODO: Support RGB and YUV formats */

	/* This has been set in mx3_camera_activate(), but we clear it above */
	sens_conf |= CSI_SENS_CONF_DATA_FMT_BAYER;

	if (common_flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)
		sens_conf |= 1 << CSI_SENS_CONF_PIX_CLK_POL_SHIFT;
	if (common_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)
		sens_conf |= 1 << CSI_SENS_CONF_HSYNC_POL_SHIFT;
	if (common_flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)
		sens_conf |= 1 << CSI_SENS_CONF_VSYNC_POL_SHIFT;
	if (common_flags & V4L2_MBUS_DATA_ACTIVE_LOW)
		sens_conf |= 1 << CSI_SENS_CONF_DATA_POL_SHIFT;

	/* Just do what we're asked to do */
	switch (xlate->host_fmt->bits_per_sample) {
	case 4:
		dw = 0 << CSI_SENS_CONF_DATA_WIDTH_SHIFT;
		break;
	case 8:
		dw = 1 << CSI_SENS_CONF_DATA_WIDTH_SHIFT;
		break;
	case 10:
		dw = 2 << CSI_SENS_CONF_DATA_WIDTH_SHIFT;
		break;
	default:
		/*
		 * Actually it can only be 15 now, default is just to silence
		 * compiler warnings
		 */
	case 15:
		dw = 3 << CSI_SENS_CONF_DATA_WIDTH_SHIFT;
	}

	csi_reg_write(mx3_cam, sens_conf | dw, CSI_SENS_CONF);

	dev_dbg(dev, "Set SENS_CONF to %x\n", sens_conf | dw);

	return 0;
}

static struct soc_camera_host_ops mx3_soc_camera_host_ops = {
	.owner		= THIS_MODULE,
	.add		= mx3_camera_add_device,
	.remove		= mx3_camera_remove_device,
	.set_crop	= mx3_camera_set_crop,
	.set_fmt	= mx3_camera_set_fmt,
	.try_fmt	= mx3_camera_try_fmt,
	.get_formats	= mx3_camera_get_formats,
	.init_videobuf2	= mx3_camera_init_videobuf,
	.reqbufs	= mx3_camera_reqbufs,
	.poll		= mx3_camera_poll,
	.querycap	= mx3_camera_querycap,
	.set_bus_param	= mx3_camera_set_bus_param,
};

static int mx3_camera_probe(struct platform_device *pdev)
{
	struct mx3_camera_dev *mx3_cam;
	struct resource *res;
	void __iomem *base;
	int err = 0;
	struct soc_camera_host *soc_host;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		err = -ENODEV;
		goto egetres;
	}

	mx3_cam = vzalloc(sizeof(*mx3_cam));
	if (!mx3_cam) {
		dev_err(&pdev->dev, "Could not allocate mx3 camera object\n");
		err = -ENOMEM;
		goto ealloc;
	}

	mx3_cam->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(mx3_cam->clk)) {
		err = PTR_ERR(mx3_cam->clk);
		goto eclkget;
	}

	mx3_cam->pdata = pdev->dev.platform_data;
	mx3_cam->platform_flags = mx3_cam->pdata->flags;
	if (!(mx3_cam->platform_flags & MX3_CAMERA_DATAWIDTH_MASK)) {
		/*
		 * Platform hasn't set available data widths. This is bad.
		 * Warn and use a default.
		 */
		dev_warn(&pdev->dev, "WARNING! Platform hasn't set available "
			 "data widths, using default 8 bit\n");
		mx3_cam->platform_flags |= MX3_CAMERA_DATAWIDTH_8;
	}
	if (mx3_cam->platform_flags & MX3_CAMERA_DATAWIDTH_4)
		mx3_cam->width_flags = 1 << 3;
	if (mx3_cam->platform_flags & MX3_CAMERA_DATAWIDTH_8)
		mx3_cam->width_flags |= 1 << 7;
	if (mx3_cam->platform_flags & MX3_CAMERA_DATAWIDTH_10)
		mx3_cam->width_flags |= 1 << 9;
	if (mx3_cam->platform_flags & MX3_CAMERA_DATAWIDTH_15)
		mx3_cam->width_flags |= 1 << 14;

	mx3_cam->mclk = mx3_cam->pdata->mclk_10khz * 10000;
	if (!mx3_cam->mclk) {
		dev_warn(&pdev->dev,
			 "mclk_10khz == 0! Please, fix your platform data. "
			 "Using default 20MHz\n");
		mx3_cam->mclk = 20000000;
	}

	/* list of video-buffers */
	INIT_LIST_HEAD(&mx3_cam->capture);
	spin_lock_init(&mx3_cam->lock);

	base = ioremap(res->start, resource_size(res));
	if (!base) {
		pr_err("Couldn't map %x@%x\n", resource_size(res), res->start);
		err = -ENOMEM;
		goto eioremap;
	}

	mx3_cam->base	= base;

	soc_host		= &mx3_cam->soc_host;
	soc_host->drv_name	= MX3_CAM_DRV_NAME;
	soc_host->ops		= &mx3_soc_camera_host_ops;
	soc_host->priv		= mx3_cam;
	soc_host->v4l2_dev.dev	= &pdev->dev;
	soc_host->nr		= pdev->id;

	mx3_cam->alloc_ctx = vb2_dma_contig_init_ctx(&pdev->dev);
	if (IS_ERR(mx3_cam->alloc_ctx)) {
		err = PTR_ERR(mx3_cam->alloc_ctx);
		goto eallocctx;
	}

	err = soc_camera_host_register(soc_host);
	if (err)
		goto ecamhostreg;

	/* IDMAC interface */
	dmaengine_get();

	return 0;

ecamhostreg:
	vb2_dma_contig_cleanup_ctx(mx3_cam->alloc_ctx);
eallocctx:
	iounmap(base);
eioremap:
	clk_put(mx3_cam->clk);
eclkget:
	vfree(mx3_cam);
ealloc:
egetres:
	return err;
}

static int mx3_camera_remove(struct platform_device *pdev)
{
	struct soc_camera_host *soc_host = to_soc_camera_host(&pdev->dev);
	struct mx3_camera_dev *mx3_cam = container_of(soc_host,
					struct mx3_camera_dev, soc_host);

	clk_put(mx3_cam->clk);

	soc_camera_host_unregister(soc_host);

	iounmap(mx3_cam->base);

	/*
	 * The channel has either not been allocated,
	 * or should have been released
	 */
	if (WARN_ON(mx3_cam->idmac_channel[0]))
		dma_release_channel(&mx3_cam->idmac_channel[0]->dma_chan);

	vb2_dma_contig_cleanup_ctx(mx3_cam->alloc_ctx);

	vfree(mx3_cam);

	dmaengine_put();

	return 0;
}

static struct platform_driver mx3_camera_driver = {
	.driver		= {
		.name	= MX3_CAM_DRV_NAME,
	},
	.probe		= mx3_camera_probe,
	.remove		= mx3_camera_remove,
};

module_platform_driver(mx3_camera_driver);

MODULE_DESCRIPTION("i.MX3x SoC Camera Host driver");
MODULE_AUTHOR("Guennadi Liakhovetski <lg@denx.de>");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("0.2.3");
MODULE_ALIAS("platform:" MX3_CAM_DRV_NAME);
