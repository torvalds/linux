// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI CSI2RX Shim Wrapper Driver
 *
 * Copyright (C) 2023 Texas Instruments Incorporated - https://www.ti.com/
 *
 * Author: Pratyush Yadav <p.yadav@ti.com>
 * Author: Jai Luthra <j-luthra@ti.com>
 */

#include <linux/bitfield.h>
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <media/mipi-csi2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-dma-contig.h>

#define TI_CSI2RX_MODULE_NAME		"j721e-csi2rx"

#define SHIM_CNTL			0x10
#define SHIM_CNTL_PIX_RST		BIT(0)

#define SHIM_DMACNTX			0x20
#define SHIM_DMACNTX_EN			BIT(31)
#define SHIM_DMACNTX_YUV422		GENMASK(27, 26)
#define SHIM_DMACNTX_SIZE		GENMASK(21, 20)
#define SHIM_DMACNTX_FMT		GENMASK(5, 0)
#define SHIM_DMACNTX_YUV422_MODE_11	3
#define SHIM_DMACNTX_SIZE_8		0
#define SHIM_DMACNTX_SIZE_16		1
#define SHIM_DMACNTX_SIZE_32		2

#define SHIM_PSI_CFG0			0x24
#define SHIM_PSI_CFG0_SRC_TAG		GENMASK(15, 0)
#define SHIM_PSI_CFG0_DST_TAG		GENMASK(31, 16)

#define PSIL_WORD_SIZE_BYTES		16
/*
 * There are no hard limits on the width or height. The DMA engine can handle
 * all sizes. The max width and height are arbitrary numbers for this driver.
 * Use 16K * 16K as the arbitrary limit. It is large enough that it is unlikely
 * the limit will be hit in practice.
 */
#define MAX_WIDTH_BYTES			SZ_16K
#define MAX_HEIGHT_LINES		SZ_16K

#define DRAIN_TIMEOUT_MS		50
#define DRAIN_BUFFER_SIZE		SZ_32K

struct ti_csi2rx_fmt {
	u32				fourcc;	/* Four character code. */
	u32				code;	/* Mbus code. */
	u32				csi_dt;	/* CSI Data type. */
	u8				bpp;	/* Bits per pixel. */
	u8				size;	/* Data size shift when unpacking. */
};

struct ti_csi2rx_buffer {
	/* Common v4l2 buffer. Must be first. */
	struct vb2_v4l2_buffer		vb;
	struct list_head		list;
	struct ti_csi2rx_dev		*csi;
};

enum ti_csi2rx_dma_state {
	TI_CSI2RX_DMA_STOPPED,	/* Streaming not started yet. */
	TI_CSI2RX_DMA_IDLE,	/* Streaming but no pending DMA operation. */
	TI_CSI2RX_DMA_ACTIVE,	/* Streaming and pending DMA operation. */
};

struct ti_csi2rx_dma {
	/* Protects all fields in this struct. */
	spinlock_t			lock;
	struct dma_chan			*chan;
	/* Buffers queued to the driver, waiting to be processed by DMA. */
	struct list_head		queue;
	enum ti_csi2rx_dma_state	state;
	/*
	 * Queue of buffers submitted to DMA engine.
	 */
	struct list_head		submitted;
	/* Buffer to drain stale data from PSI-L endpoint */
	struct {
		void			*vaddr;
		dma_addr_t		paddr;
		size_t			len;
	} drain;
};

struct ti_csi2rx_dev {
	struct device			*dev;
	void __iomem			*shim;
	struct v4l2_device		v4l2_dev;
	struct video_device		vdev;
	struct media_device		mdev;
	struct media_pipeline		pipe;
	struct media_pad		pad;
	struct v4l2_async_notifier	notifier;
	struct v4l2_subdev		*source;
	struct vb2_queue		vidq;
	struct mutex			mutex; /* To serialize ioctls. */
	struct v4l2_format		v_fmt;
	struct ti_csi2rx_dma		dma;
	u32				sequence;
};

static const struct ti_csi2rx_fmt ti_csi2rx_formats[] = {
	{
		.fourcc			= V4L2_PIX_FMT_YUYV,
		.code			= MEDIA_BUS_FMT_YUYV8_1X16,
		.csi_dt			= MIPI_CSI2_DT_YUV422_8B,
		.bpp			= 16,
		.size			= SHIM_DMACNTX_SIZE_8,
	}, {
		.fourcc			= V4L2_PIX_FMT_UYVY,
		.code			= MEDIA_BUS_FMT_UYVY8_1X16,
		.csi_dt			= MIPI_CSI2_DT_YUV422_8B,
		.bpp			= 16,
		.size			= SHIM_DMACNTX_SIZE_8,
	}, {
		.fourcc			= V4L2_PIX_FMT_YVYU,
		.code			= MEDIA_BUS_FMT_YVYU8_1X16,
		.csi_dt			= MIPI_CSI2_DT_YUV422_8B,
		.bpp			= 16,
		.size			= SHIM_DMACNTX_SIZE_8,
	}, {
		.fourcc			= V4L2_PIX_FMT_VYUY,
		.code			= MEDIA_BUS_FMT_VYUY8_1X16,
		.csi_dt			= MIPI_CSI2_DT_YUV422_8B,
		.bpp			= 16,
		.size			= SHIM_DMACNTX_SIZE_8,
	}, {
		.fourcc			= V4L2_PIX_FMT_SBGGR8,
		.code			= MEDIA_BUS_FMT_SBGGR8_1X8,
		.csi_dt			= MIPI_CSI2_DT_RAW8,
		.bpp			= 8,
		.size			= SHIM_DMACNTX_SIZE_8,
	}, {
		.fourcc			= V4L2_PIX_FMT_SGBRG8,
		.code			= MEDIA_BUS_FMT_SGBRG8_1X8,
		.csi_dt			= MIPI_CSI2_DT_RAW8,
		.bpp			= 8,
		.size			= SHIM_DMACNTX_SIZE_8,
	}, {
		.fourcc			= V4L2_PIX_FMT_SGRBG8,
		.code			= MEDIA_BUS_FMT_SGRBG8_1X8,
		.csi_dt			= MIPI_CSI2_DT_RAW8,
		.bpp			= 8,
		.size			= SHIM_DMACNTX_SIZE_8,
	}, {
		.fourcc			= V4L2_PIX_FMT_SRGGB8,
		.code			= MEDIA_BUS_FMT_SRGGB8_1X8,
		.csi_dt			= MIPI_CSI2_DT_RAW8,
		.bpp			= 8,
		.size			= SHIM_DMACNTX_SIZE_8,
	}, {
		.fourcc			= V4L2_PIX_FMT_GREY,
		.code			= MEDIA_BUS_FMT_Y8_1X8,
		.csi_dt			= MIPI_CSI2_DT_RAW8,
		.bpp			= 8,
		.size			= SHIM_DMACNTX_SIZE_8,
	}, {
		.fourcc			= V4L2_PIX_FMT_SBGGR10,
		.code			= MEDIA_BUS_FMT_SBGGR10_1X10,
		.csi_dt			= MIPI_CSI2_DT_RAW10,
		.bpp			= 16,
		.size			= SHIM_DMACNTX_SIZE_16,
	}, {
		.fourcc			= V4L2_PIX_FMT_SGBRG10,
		.code			= MEDIA_BUS_FMT_SGBRG10_1X10,
		.csi_dt			= MIPI_CSI2_DT_RAW10,
		.bpp			= 16,
		.size			= SHIM_DMACNTX_SIZE_16,
	}, {
		.fourcc			= V4L2_PIX_FMT_SGRBG10,
		.code			= MEDIA_BUS_FMT_SGRBG10_1X10,
		.csi_dt			= MIPI_CSI2_DT_RAW10,
		.bpp			= 16,
		.size			= SHIM_DMACNTX_SIZE_16,
	}, {
		.fourcc			= V4L2_PIX_FMT_SRGGB10,
		.code			= MEDIA_BUS_FMT_SRGGB10_1X10,
		.csi_dt			= MIPI_CSI2_DT_RAW10,
		.bpp			= 16,
		.size			= SHIM_DMACNTX_SIZE_16,
	}, {
		.fourcc			= V4L2_PIX_FMT_RGB565X,
		.code			= MEDIA_BUS_FMT_RGB565_1X16,
		.csi_dt			= MIPI_CSI2_DT_RGB565,
		.bpp			= 16,
		.size			= SHIM_DMACNTX_SIZE_16,
	}, {
		.fourcc			= V4L2_PIX_FMT_XBGR32,
		.code			= MEDIA_BUS_FMT_RGB888_1X24,
		.csi_dt			= MIPI_CSI2_DT_RGB888,
		.bpp			= 32,
		.size			= SHIM_DMACNTX_SIZE_32,
	}, {
		.fourcc			= V4L2_PIX_FMT_RGBX32,
		.code			= MEDIA_BUS_FMT_BGR888_1X24,
		.csi_dt			= MIPI_CSI2_DT_RGB888,
		.bpp			= 32,
		.size			= SHIM_DMACNTX_SIZE_32,
	},

	/* More formats can be supported but they are not listed for now. */
};

/* Forward declaration needed by ti_csi2rx_dma_callback. */
static int ti_csi2rx_start_dma(struct ti_csi2rx_dev *csi,
			       struct ti_csi2rx_buffer *buf);

static const struct ti_csi2rx_fmt *find_format_by_fourcc(u32 pixelformat)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ti_csi2rx_formats); i++) {
		if (ti_csi2rx_formats[i].fourcc == pixelformat)
			return &ti_csi2rx_formats[i];
	}

	return NULL;
}

static const struct ti_csi2rx_fmt *find_format_by_code(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ti_csi2rx_formats); i++) {
		if (ti_csi2rx_formats[i].code == code)
			return &ti_csi2rx_formats[i];
	}

	return NULL;
}

static void ti_csi2rx_fill_fmt(const struct ti_csi2rx_fmt *csi_fmt,
			       struct v4l2_format *v4l2_fmt)
{
	struct v4l2_pix_format *pix = &v4l2_fmt->fmt.pix;
	unsigned int pixels_in_word;

	pixels_in_word = PSIL_WORD_SIZE_BYTES * 8 / csi_fmt->bpp;

	/* Clamp width and height to sensible maximums (16K x 16K) */
	pix->width = clamp_t(unsigned int, pix->width,
			     pixels_in_word,
			     MAX_WIDTH_BYTES * 8 / csi_fmt->bpp);
	pix->height = clamp_t(unsigned int, pix->height, 1, MAX_HEIGHT_LINES);

	/* Width should be a multiple of transfer word-size */
	pix->width = rounddown(pix->width, pixels_in_word);

	v4l2_fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	pix->pixelformat = csi_fmt->fourcc;
	pix->bytesperline = pix->width * (csi_fmt->bpp / 8);
	pix->sizeimage = pix->bytesperline * pix->height;
}

static int ti_csi2rx_querycap(struct file *file, void *priv,
			      struct v4l2_capability *cap)
{
	strscpy(cap->driver, TI_CSI2RX_MODULE_NAME, sizeof(cap->driver));
	strscpy(cap->card, TI_CSI2RX_MODULE_NAME, sizeof(cap->card));

	return 0;
}

static int ti_csi2rx_enum_fmt_vid_cap(struct file *file, void *priv,
				      struct v4l2_fmtdesc *f)
{
	const struct ti_csi2rx_fmt *fmt = NULL;

	if (f->mbus_code) {
		/* 1-to-1 mapping between bus formats and pixel formats */
		if (f->index > 0)
			return -EINVAL;

		fmt = find_format_by_code(f->mbus_code);
	} else {
		if (f->index >= ARRAY_SIZE(ti_csi2rx_formats))
			return -EINVAL;

		fmt = &ti_csi2rx_formats[f->index];
	}

	if (!fmt)
		return -EINVAL;

	f->pixelformat = fmt->fourcc;
	memset(f->reserved, 0, sizeof(f->reserved));
	f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

static int ti_csi2rx_g_fmt_vid_cap(struct file *file, void *prov,
				   struct v4l2_format *f)
{
	struct ti_csi2rx_dev *csi = video_drvdata(file);

	*f = csi->v_fmt;

	return 0;
}

static int ti_csi2rx_try_fmt_vid_cap(struct file *file, void *priv,
				     struct v4l2_format *f)
{
	const struct ti_csi2rx_fmt *fmt;

	/*
	 * Default to the first format if the requested pixel format code isn't
	 * supported.
	 */
	fmt = find_format_by_fourcc(f->fmt.pix.pixelformat);
	if (!fmt)
		fmt = &ti_csi2rx_formats[0];

	/* Interlaced formats are not supported. */
	f->fmt.pix.field = V4L2_FIELD_NONE;

	ti_csi2rx_fill_fmt(fmt, f);

	return 0;
}

static int ti_csi2rx_s_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_format *f)
{
	struct ti_csi2rx_dev *csi = video_drvdata(file);
	struct vb2_queue *q = &csi->vidq;
	int ret;

	if (vb2_is_busy(q))
		return -EBUSY;

	ret = ti_csi2rx_try_fmt_vid_cap(file, priv, f);
	if (ret < 0)
		return ret;

	csi->v_fmt = *f;

	return 0;
}

static int ti_csi2rx_enum_framesizes(struct file *file, void *fh,
				     struct v4l2_frmsizeenum *fsize)
{
	const struct ti_csi2rx_fmt *fmt;
	unsigned int pixels_in_word;

	fmt = find_format_by_fourcc(fsize->pixel_format);
	if (!fmt || fsize->index != 0)
		return -EINVAL;

	/*
	 * Number of pixels in one PSI-L word. The transfer happens in multiples
	 * of PSI-L word sizes.
	 */
	pixels_in_word = PSIL_WORD_SIZE_BYTES * 8 / fmt->bpp;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise.min_width = pixels_in_word;
	fsize->stepwise.max_width = rounddown(MAX_WIDTH_BYTES * 8 / fmt->bpp,
					      pixels_in_word);
	fsize->stepwise.step_width = pixels_in_word;
	fsize->stepwise.min_height = 1;
	fsize->stepwise.max_height = MAX_HEIGHT_LINES;
	fsize->stepwise.step_height = 1;

	return 0;
}

static const struct v4l2_ioctl_ops csi_ioctl_ops = {
	.vidioc_querycap      = ti_csi2rx_querycap,
	.vidioc_enum_fmt_vid_cap = ti_csi2rx_enum_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = ti_csi2rx_try_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap = ti_csi2rx_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = ti_csi2rx_s_fmt_vid_cap,
	.vidioc_enum_framesizes = ti_csi2rx_enum_framesizes,
	.vidioc_reqbufs       = vb2_ioctl_reqbufs,
	.vidioc_create_bufs   = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf   = vb2_ioctl_prepare_buf,
	.vidioc_querybuf      = vb2_ioctl_querybuf,
	.vidioc_qbuf          = vb2_ioctl_qbuf,
	.vidioc_dqbuf         = vb2_ioctl_dqbuf,
	.vidioc_expbuf        = vb2_ioctl_expbuf,
	.vidioc_streamon      = vb2_ioctl_streamon,
	.vidioc_streamoff     = vb2_ioctl_streamoff,
};

static const struct v4l2_file_operations csi_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.read = vb2_fop_read,
	.poll = vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = vb2_fop_mmap,
};

static int csi_async_notifier_bound(struct v4l2_async_notifier *notifier,
				    struct v4l2_subdev *subdev,
				    struct v4l2_async_connection *asc)
{
	struct ti_csi2rx_dev *csi = dev_get_drvdata(notifier->v4l2_dev->dev);

	csi->source = subdev;

	return 0;
}

static int csi_async_notifier_complete(struct v4l2_async_notifier *notifier)
{
	struct ti_csi2rx_dev *csi = dev_get_drvdata(notifier->v4l2_dev->dev);
	struct video_device *vdev = &csi->vdev;
	int ret;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret)
		return ret;

	ret = v4l2_create_fwnode_links_to_pad(csi->source, &csi->pad,
					      MEDIA_LNK_FL_IMMUTABLE | MEDIA_LNK_FL_ENABLED);

	if (ret) {
		video_unregister_device(vdev);
		return ret;
	}

	ret = v4l2_device_register_subdev_nodes(&csi->v4l2_dev);
	if (ret)
		video_unregister_device(vdev);

	return ret;
}

static const struct v4l2_async_notifier_operations csi_async_notifier_ops = {
	.bound = csi_async_notifier_bound,
	.complete = csi_async_notifier_complete,
};

static int ti_csi2rx_notifier_register(struct ti_csi2rx_dev *csi)
{
	struct fwnode_handle *fwnode;
	struct v4l2_async_connection *asc;
	struct device_node *node;
	int ret;

	node = of_get_child_by_name(csi->dev->of_node, "csi-bridge");
	if (!node)
		return -EINVAL;

	fwnode = of_fwnode_handle(node);
	if (!fwnode) {
		of_node_put(node);
		return -EINVAL;
	}

	v4l2_async_nf_init(&csi->notifier, &csi->v4l2_dev);
	csi->notifier.ops = &csi_async_notifier_ops;

	asc = v4l2_async_nf_add_fwnode(&csi->notifier, fwnode,
				       struct v4l2_async_connection);
	of_node_put(node);
	if (IS_ERR(asc)) {
		v4l2_async_nf_cleanup(&csi->notifier);
		return PTR_ERR(asc);
	}

	ret = v4l2_async_nf_register(&csi->notifier);
	if (ret) {
		v4l2_async_nf_cleanup(&csi->notifier);
		return ret;
	}

	return 0;
}

static void ti_csi2rx_setup_shim(struct ti_csi2rx_dev *csi)
{
	const struct ti_csi2rx_fmt *fmt;
	unsigned int reg;

	fmt = find_format_by_fourcc(csi->v_fmt.fmt.pix.pixelformat);

	/* De-assert the pixel interface reset. */
	reg = SHIM_CNTL_PIX_RST;
	writel(reg, csi->shim + SHIM_CNTL);

	reg = SHIM_DMACNTX_EN;
	reg |= FIELD_PREP(SHIM_DMACNTX_FMT, fmt->csi_dt);

	/*
	 * The hardware assumes incoming YUV422 8-bit data on MIPI CSI2 bus
	 * follows the spec and is packed in the order U0 -> Y0 -> V0 -> Y1 ->
	 * ...
	 *
	 * There is an option to swap the bytes around before storing in
	 * memory, to achieve different pixel formats:
	 *
	 * Byte3 <----------- Byte0
	 * [ Y1 ][ V0 ][ Y0 ][ U0 ]	MODE 11
	 * [ Y1 ][ U0 ][ Y0 ][ V0 ]	MODE 10
	 * [ V0 ][ Y1 ][ U0 ][ Y0 ]	MODE 01
	 * [ U0 ][ Y1 ][ V0 ][ Y0 ]	MODE 00
	 *
	 * We don't have any requirement to change pixelformat from what is
	 * coming from the source, so we keep it in MODE 11, which does not
	 * swap any bytes when storing in memory.
	 */
	switch (fmt->fourcc) {
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
		reg |= FIELD_PREP(SHIM_DMACNTX_YUV422,
				  SHIM_DMACNTX_YUV422_MODE_11);
		break;
	default:
		/* Ignore if not YUV 4:2:2 */
		break;
	}

	reg |= FIELD_PREP(SHIM_DMACNTX_SIZE, fmt->size);

	writel(reg, csi->shim + SHIM_DMACNTX);

	reg = FIELD_PREP(SHIM_PSI_CFG0_SRC_TAG, 0) |
	      FIELD_PREP(SHIM_PSI_CFG0_DST_TAG, 0);
	writel(reg, csi->shim + SHIM_PSI_CFG0);
}

static void ti_csi2rx_drain_callback(void *param)
{
	struct completion *drain_complete = param;

	complete(drain_complete);
}

/*
 * Drain the stale data left at the PSI-L endpoint.
 *
 * This might happen if no buffers are queued in time but source is still
 * streaming. In multi-stream scenarios this can happen when one stream is
 * stopped but other is still streaming, and thus module-level pixel reset is
 * not asserted.
 *
 * To prevent that stale data corrupting the subsequent transactions, it is
 * required to issue DMA requests to drain it out.
 */
static int ti_csi2rx_drain_dma(struct ti_csi2rx_dev *csi)
{
	struct dma_async_tx_descriptor *desc;
	struct completion drain_complete;
	dma_cookie_t cookie;
	int ret;

	init_completion(&drain_complete);

	desc = dmaengine_prep_slave_single(csi->dma.chan, csi->dma.drain.paddr,
					   csi->dma.drain.len, DMA_DEV_TO_MEM,
					   DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		ret = -EIO;
		goto out;
	}

	desc->callback = ti_csi2rx_drain_callback;
	desc->callback_param = &drain_complete;

	cookie = dmaengine_submit(desc);
	ret = dma_submit_error(cookie);
	if (ret)
		goto out;

	dma_async_issue_pending(csi->dma.chan);

	if (!wait_for_completion_timeout(&drain_complete,
					 msecs_to_jiffies(DRAIN_TIMEOUT_MS))) {
		dmaengine_terminate_sync(csi->dma.chan);
		dev_dbg(csi->dev, "DMA transfer timed out for drain buffer\n");
		ret = -ETIMEDOUT;
		goto out;
	}
out:
	return ret;
}

static void ti_csi2rx_dma_callback(void *param)
{
	struct ti_csi2rx_buffer *buf = param;
	struct ti_csi2rx_dev *csi = buf->csi;
	struct ti_csi2rx_dma *dma = &csi->dma;
	unsigned long flags;

	/*
	 * TODO: Derive the sequence number from the CSI2RX frame number
	 * hardware monitor registers.
	 */
	buf->vb.vb2_buf.timestamp = ktime_get_ns();
	buf->vb.sequence = csi->sequence++;

	spin_lock_irqsave(&dma->lock, flags);

	WARN_ON(!list_is_first(&buf->list, &dma->submitted));
	vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	list_del(&buf->list);

	/* If there are more buffers to process then start their transfer. */
	while (!list_empty(&dma->queue)) {
		buf = list_entry(dma->queue.next, struct ti_csi2rx_buffer, list);

		if (ti_csi2rx_start_dma(csi, buf)) {
			dev_err(csi->dev, "Failed to queue the next buffer for DMA\n");
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		} else {
			list_move_tail(&buf->list, &dma->submitted);
		}
	}

	if (list_empty(&dma->submitted))
		dma->state = TI_CSI2RX_DMA_IDLE;

	spin_unlock_irqrestore(&dma->lock, flags);
}

static int ti_csi2rx_start_dma(struct ti_csi2rx_dev *csi,
			       struct ti_csi2rx_buffer *buf)
{
	unsigned long addr;
	struct dma_async_tx_descriptor *desc;
	size_t len = csi->v_fmt.fmt.pix.sizeimage;
	dma_cookie_t cookie;
	int ret = 0;

	addr = vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 0);
	desc = dmaengine_prep_slave_single(csi->dma.chan, addr, len,
					   DMA_DEV_TO_MEM,
					   DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc)
		return -EIO;

	desc->callback = ti_csi2rx_dma_callback;
	desc->callback_param = buf;

	cookie = dmaengine_submit(desc);
	ret = dma_submit_error(cookie);
	if (ret)
		return ret;

	dma_async_issue_pending(csi->dma.chan);

	return 0;
}

static void ti_csi2rx_stop_dma(struct ti_csi2rx_dev *csi)
{
	struct ti_csi2rx_dma *dma = &csi->dma;
	enum ti_csi2rx_dma_state state;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&dma->lock, flags);
	state = csi->dma.state;
	dma->state = TI_CSI2RX_DMA_STOPPED;
	spin_unlock_irqrestore(&dma->lock, flags);

	if (state != TI_CSI2RX_DMA_STOPPED) {
		/*
		 * Normal DMA termination does not clean up pending data on
		 * the endpoint if multiple streams are running and only one
		 * is stopped, as the module-level pixel reset cannot be
		 * enforced before terminating DMA.
		 */
		ret = ti_csi2rx_drain_dma(csi);
		if (ret && ret != -ETIMEDOUT)
			dev_warn(csi->dev,
				 "Failed to drain DMA. Next frame might be bogus\n");
	}

	ret = dmaengine_terminate_sync(csi->dma.chan);
	if (ret)
		dev_err(csi->dev, "Failed to stop DMA: %d\n", ret);
}

static void ti_csi2rx_cleanup_buffers(struct ti_csi2rx_dev *csi,
				      enum vb2_buffer_state state)
{
	struct ti_csi2rx_dma *dma = &csi->dma;
	struct ti_csi2rx_buffer *buf, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&dma->lock, flags);
	list_for_each_entry_safe(buf, tmp, &csi->dma.queue, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	list_for_each_entry_safe(buf, tmp, &csi->dma.submitted, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	spin_unlock_irqrestore(&dma->lock, flags);
}

static int ti_csi2rx_queue_setup(struct vb2_queue *q, unsigned int *nbuffers,
				 unsigned int *nplanes, unsigned int sizes[],
				 struct device *alloc_devs[])
{
	struct ti_csi2rx_dev *csi = vb2_get_drv_priv(q);
	unsigned int size = csi->v_fmt.fmt.pix.sizeimage;

	if (*nplanes) {
		if (sizes[0] < size)
			return -EINVAL;
		size = sizes[0];
	}

	*nplanes = 1;
	sizes[0] = size;

	return 0;
}

static int ti_csi2rx_buffer_prepare(struct vb2_buffer *vb)
{
	struct ti_csi2rx_dev *csi = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long size = csi->v_fmt.fmt.pix.sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(csi->dev, "Data will not fit into plane\n");
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, size);
	return 0;
}

static void ti_csi2rx_buffer_queue(struct vb2_buffer *vb)
{
	struct ti_csi2rx_dev *csi = vb2_get_drv_priv(vb->vb2_queue);
	struct ti_csi2rx_buffer *buf;
	struct ti_csi2rx_dma *dma = &csi->dma;
	bool restart_dma = false;
	unsigned long flags = 0;
	int ret;

	buf = container_of(vb, struct ti_csi2rx_buffer, vb.vb2_buf);
	buf->csi = csi;

	spin_lock_irqsave(&dma->lock, flags);
	/*
	 * Usually the DMA callback takes care of queueing the pending buffers.
	 * But if DMA has stalled due to lack of buffers, restart it now.
	 */
	if (dma->state == TI_CSI2RX_DMA_IDLE) {
		/*
		 * Do not restart DMA with the lock held because
		 * ti_csi2rx_drain_dma() might block for completion.
		 * There won't be a race on queueing DMA anyway since the
		 * callback is not being fired.
		 */
		restart_dma = true;
		dma->state = TI_CSI2RX_DMA_ACTIVE;
	} else {
		list_add_tail(&buf->list, &dma->queue);
	}
	spin_unlock_irqrestore(&dma->lock, flags);

	if (restart_dma) {
		/*
		 * Once frames start dropping, some data gets stuck in the DMA
		 * pipeline somewhere. So the first DMA transfer after frame
		 * drops gives a partial frame. This is obviously not useful to
		 * the application and will only confuse it. Issue a DMA
		 * transaction to drain that up.
		 */
		ret = ti_csi2rx_drain_dma(csi);
		if (ret && ret != -ETIMEDOUT)
			dev_warn(csi->dev,
				 "Failed to drain DMA. Next frame might be bogus\n");

		spin_lock_irqsave(&dma->lock, flags);
		ret = ti_csi2rx_start_dma(csi, buf);
		if (ret) {
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
			dma->state = TI_CSI2RX_DMA_IDLE;
			spin_unlock_irqrestore(&dma->lock, flags);
			dev_err(csi->dev, "Failed to start DMA: %d\n", ret);
		} else {
			list_add_tail(&buf->list, &dma->submitted);
			spin_unlock_irqrestore(&dma->lock, flags);
		}
	}
}

static int ti_csi2rx_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct ti_csi2rx_dev *csi = vb2_get_drv_priv(vq);
	struct ti_csi2rx_dma *dma = &csi->dma;
	struct ti_csi2rx_buffer *buf;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&dma->lock, flags);
	if (list_empty(&dma->queue))
		ret = -EIO;
	spin_unlock_irqrestore(&dma->lock, flags);
	if (ret)
		return ret;

	ret = video_device_pipeline_start(&csi->vdev, &csi->pipe);
	if (ret)
		goto err;

	ti_csi2rx_setup_shim(csi);

	csi->sequence = 0;

	spin_lock_irqsave(&dma->lock, flags);
	buf = list_entry(dma->queue.next, struct ti_csi2rx_buffer, list);

	ret = ti_csi2rx_start_dma(csi, buf);
	if (ret) {
		dev_err(csi->dev, "Failed to start DMA: %d\n", ret);
		spin_unlock_irqrestore(&dma->lock, flags);
		goto err_pipeline;
	}

	list_move_tail(&buf->list, &dma->submitted);
	dma->state = TI_CSI2RX_DMA_ACTIVE;
	spin_unlock_irqrestore(&dma->lock, flags);

	ret = v4l2_subdev_call(csi->source, video, s_stream, 1);
	if (ret)
		goto err_dma;

	return 0;

err_dma:
	ti_csi2rx_stop_dma(csi);
err_pipeline:
	video_device_pipeline_stop(&csi->vdev);
	writel(0, csi->shim + SHIM_CNTL);
	writel(0, csi->shim + SHIM_DMACNTX);
err:
	ti_csi2rx_cleanup_buffers(csi, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void ti_csi2rx_stop_streaming(struct vb2_queue *vq)
{
	struct ti_csi2rx_dev *csi = vb2_get_drv_priv(vq);
	int ret;

	video_device_pipeline_stop(&csi->vdev);

	writel(0, csi->shim + SHIM_CNTL);
	writel(0, csi->shim + SHIM_DMACNTX);

	ret = v4l2_subdev_call(csi->source, video, s_stream, 0);
	if (ret)
		dev_err(csi->dev, "Failed to stop subdev stream\n");

	ti_csi2rx_stop_dma(csi);
	ti_csi2rx_cleanup_buffers(csi, VB2_BUF_STATE_ERROR);
}

static const struct vb2_ops csi_vb2_qops = {
	.queue_setup = ti_csi2rx_queue_setup,
	.buf_prepare = ti_csi2rx_buffer_prepare,
	.buf_queue = ti_csi2rx_buffer_queue,
	.start_streaming = ti_csi2rx_start_streaming,
	.stop_streaming = ti_csi2rx_stop_streaming,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
};

static int ti_csi2rx_init_vb2q(struct ti_csi2rx_dev *csi)
{
	struct vb2_queue *q = &csi->vidq;
	int ret;

	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->drv_priv = csi;
	q->buf_struct_size = sizeof(struct ti_csi2rx_buffer);
	q->ops = &csi_vb2_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->dev = dmaengine_get_dma_device(csi->dma.chan);
	q->lock = &csi->mutex;
	q->min_queued_buffers = 1;

	ret = vb2_queue_init(q);
	if (ret)
		return ret;

	csi->vdev.queue = q;

	return 0;
}

static int ti_csi2rx_link_validate(struct media_link *link)
{
	struct media_entity *entity = link->sink->entity;
	struct video_device *vdev = media_entity_to_video_device(entity);
	struct ti_csi2rx_dev *csi = container_of(vdev, struct ti_csi2rx_dev, vdev);
	struct v4l2_pix_format *csi_fmt = &csi->v_fmt.fmt.pix;
	struct v4l2_subdev_format source_fmt = {
		.which	= V4L2_SUBDEV_FORMAT_ACTIVE,
		.pad	= link->source->index,
	};
	const struct ti_csi2rx_fmt *ti_fmt;
	int ret;

	ret = v4l2_subdev_call_state_active(csi->source, pad,
					    get_fmt, &source_fmt);
	if (ret)
		return ret;

	if (source_fmt.format.width != csi_fmt->width) {
		dev_dbg(csi->dev, "Width does not match (source %u, sink %u)\n",
			source_fmt.format.width, csi_fmt->width);
		return -EPIPE;
	}

	if (source_fmt.format.height != csi_fmt->height) {
		dev_dbg(csi->dev, "Height does not match (source %u, sink %u)\n",
			source_fmt.format.height, csi_fmt->height);
		return -EPIPE;
	}

	if (source_fmt.format.field != csi_fmt->field &&
	    csi_fmt->field != V4L2_FIELD_NONE) {
		dev_dbg(csi->dev, "Field does not match (source %u, sink %u)\n",
			source_fmt.format.field, csi_fmt->field);
		return -EPIPE;
	}

	ti_fmt = find_format_by_code(source_fmt.format.code);
	if (!ti_fmt) {
		dev_dbg(csi->dev, "Media bus format 0x%x not supported\n",
			source_fmt.format.code);
		return -EPIPE;
	}

	if (ti_fmt->fourcc != csi_fmt->pixelformat) {
		dev_dbg(csi->dev,
			"Cannot transform source fmt 0x%x to sink fmt 0x%x\n",
			ti_fmt->fourcc, csi_fmt->pixelformat);
		return -EPIPE;
	}

	return 0;
}

static const struct media_entity_operations ti_csi2rx_video_entity_ops = {
	.link_validate = ti_csi2rx_link_validate,
};

static int ti_csi2rx_init_dma(struct ti_csi2rx_dev *csi)
{
	struct dma_slave_config cfg = {
		.src_addr_width = DMA_SLAVE_BUSWIDTH_16_BYTES,
	};
	int ret;

	INIT_LIST_HEAD(&csi->dma.queue);
	INIT_LIST_HEAD(&csi->dma.submitted);
	spin_lock_init(&csi->dma.lock);

	csi->dma.state = TI_CSI2RX_DMA_STOPPED;

	csi->dma.chan = dma_request_chan(csi->dev, "rx0");
	if (IS_ERR(csi->dma.chan))
		return PTR_ERR(csi->dma.chan);

	ret = dmaengine_slave_config(csi->dma.chan, &cfg);
	if (ret) {
		dma_release_channel(csi->dma.chan);
		return ret;
	}

	csi->dma.drain.len = DRAIN_BUFFER_SIZE;
	csi->dma.drain.vaddr = dma_alloc_coherent(csi->dev, csi->dma.drain.len,
						  &csi->dma.drain.paddr,
						  GFP_KERNEL);
	if (!csi->dma.drain.vaddr)
		return -ENOMEM;

	return 0;
}

static int ti_csi2rx_v4l2_init(struct ti_csi2rx_dev *csi)
{
	struct media_device *mdev = &csi->mdev;
	struct video_device *vdev = &csi->vdev;
	const struct ti_csi2rx_fmt *fmt;
	struct v4l2_pix_format *pix_fmt = &csi->v_fmt.fmt.pix;
	int ret;

	fmt = find_format_by_fourcc(V4L2_PIX_FMT_UYVY);
	if (!fmt)
		return -EINVAL;

	pix_fmt->width = 640;
	pix_fmt->height = 480;
	pix_fmt->field = V4L2_FIELD_NONE;
	pix_fmt->colorspace = V4L2_COLORSPACE_SRGB;
	pix_fmt->ycbcr_enc = V4L2_YCBCR_ENC_601,
	pix_fmt->quantization = V4L2_QUANTIZATION_LIM_RANGE,
	pix_fmt->xfer_func = V4L2_XFER_FUNC_SRGB,

	ti_csi2rx_fill_fmt(fmt, &csi->v_fmt);

	mdev->dev = csi->dev;
	mdev->hw_revision = 1;
	strscpy(mdev->model, "TI-CSI2RX", sizeof(mdev->model));

	media_device_init(mdev);

	strscpy(vdev->name, TI_CSI2RX_MODULE_NAME, sizeof(vdev->name));
	vdev->v4l2_dev = &csi->v4l2_dev;
	vdev->vfl_dir = VFL_DIR_RX;
	vdev->fops = &csi_fops;
	vdev->ioctl_ops = &csi_ioctl_ops;
	vdev->release = video_device_release_empty;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
			    V4L2_CAP_IO_MC;
	vdev->lock = &csi->mutex;
	video_set_drvdata(vdev, csi);

	csi->pad.flags = MEDIA_PAD_FL_SINK;
	vdev->entity.ops = &ti_csi2rx_video_entity_ops;
	ret = media_entity_pads_init(&csi->vdev.entity, 1, &csi->pad);
	if (ret)
		return ret;

	csi->v4l2_dev.mdev = mdev;

	ret = v4l2_device_register(csi->dev, &csi->v4l2_dev);
	if (ret)
		return ret;

	ret = media_device_register(mdev);
	if (ret) {
		v4l2_device_unregister(&csi->v4l2_dev);
		media_device_cleanup(mdev);
		return ret;
	}

	return 0;
}

static void ti_csi2rx_cleanup_dma(struct ti_csi2rx_dev *csi)
{
	dma_free_coherent(csi->dev, csi->dma.drain.len,
			  csi->dma.drain.vaddr, csi->dma.drain.paddr);
	csi->dma.drain.vaddr = NULL;
	dma_release_channel(csi->dma.chan);
}

static void ti_csi2rx_cleanup_v4l2(struct ti_csi2rx_dev *csi)
{
	media_device_unregister(&csi->mdev);
	v4l2_device_unregister(&csi->v4l2_dev);
	media_device_cleanup(&csi->mdev);
}

static void ti_csi2rx_cleanup_subdev(struct ti_csi2rx_dev *csi)
{
	v4l2_async_nf_unregister(&csi->notifier);
	v4l2_async_nf_cleanup(&csi->notifier);
}

static void ti_csi2rx_cleanup_vb2q(struct ti_csi2rx_dev *csi)
{
	vb2_queue_release(&csi->vidq);
}

static int ti_csi2rx_probe(struct platform_device *pdev)
{
	struct ti_csi2rx_dev *csi;
	int ret;

	csi = devm_kzalloc(&pdev->dev, sizeof(*csi), GFP_KERNEL);
	if (!csi)
		return -ENOMEM;

	csi->dev = &pdev->dev;
	platform_set_drvdata(pdev, csi);

	mutex_init(&csi->mutex);
	csi->shim = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(csi->shim)) {
		ret = PTR_ERR(csi->shim);
		goto err_mutex;
	}

	ret = ti_csi2rx_init_dma(csi);
	if (ret)
		goto err_mutex;

	ret = ti_csi2rx_v4l2_init(csi);
	if (ret)
		goto err_dma;

	ret = ti_csi2rx_init_vb2q(csi);
	if (ret)
		goto err_v4l2;

	ret = ti_csi2rx_notifier_register(csi);
	if (ret)
		goto err_vb2q;

	ret = of_platform_populate(csi->dev->of_node, NULL, NULL, csi->dev);
	if (ret) {
		dev_err(csi->dev, "Failed to create children: %d\n", ret);
		goto err_subdev;
	}

	return 0;

err_subdev:
	ti_csi2rx_cleanup_subdev(csi);
err_vb2q:
	ti_csi2rx_cleanup_vb2q(csi);
err_v4l2:
	ti_csi2rx_cleanup_v4l2(csi);
err_dma:
	ti_csi2rx_cleanup_dma(csi);
err_mutex:
	mutex_destroy(&csi->mutex);
	return ret;
}

static void ti_csi2rx_remove(struct platform_device *pdev)
{
	struct ti_csi2rx_dev *csi = platform_get_drvdata(pdev);

	video_unregister_device(&csi->vdev);

	ti_csi2rx_cleanup_vb2q(csi);
	ti_csi2rx_cleanup_subdev(csi);
	ti_csi2rx_cleanup_v4l2(csi);
	ti_csi2rx_cleanup_dma(csi);

	mutex_destroy(&csi->mutex);
}

static const struct of_device_id ti_csi2rx_of_match[] = {
	{ .compatible = "ti,j721e-csi2rx-shim", },
	{ },
};
MODULE_DEVICE_TABLE(of, ti_csi2rx_of_match);

static struct platform_driver ti_csi2rx_pdrv = {
	.probe = ti_csi2rx_probe,
	.remove_new = ti_csi2rx_remove,
	.driver = {
		.name = TI_CSI2RX_MODULE_NAME,
		.of_match_table = ti_csi2rx_of_match,
	},
};

module_platform_driver(ti_csi2rx_pdrv);

MODULE_DESCRIPTION("TI J721E CSI2 RX Driver");
MODULE_AUTHOR("Jai Luthra <j-luthra@ti.com>");
MODULE_LICENSE("GPL");
