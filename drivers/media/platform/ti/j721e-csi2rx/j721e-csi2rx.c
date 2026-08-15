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
#include <linux/pm_runtime.h>
#include <linux/property.h>

#include <media/cadence/cdns-csi2rx.h>
#include <media/mipi-csi2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-dma-contig.h>

#define TI_CSI2RX_MODULE_NAME		"j721e-csi2rx"

#define SHIM_CNTL			0x10
#define SHIM_CNTL_PIX_RST		BIT(0)

#define SHIM_DMACNTX(i)			(0x20 + ((i) * 0x20))
#define SHIM_DMACNTX_EN			BIT(31)
#define SHIM_DMACNTX_YUV422		GENMASK(27, 26)
#define SHIM_DMACNTX_DUAL_PCK_CFG	BIT(24)
#define SHIM_DMACNTX_SIZE		GENMASK(21, 20)
#define SHIM_DMACNTX_VC			GENMASK(9, 6)
#define SHIM_DMACNTX_FMT		GENMASK(5, 0)
#define SHIM_DMACNTX_YUV422_MODE_11	3
#define SHIM_DMACNTX_SIZE_8		0
#define SHIM_DMACNTX_SIZE_16		1
#define SHIM_DMACNTX_SIZE_32		2

#define SHIM_PSI_CFG0(i)		(0x24 + ((i) * 0x20))
#define SHIM_PSI_CFG0_SRC_TAG		GENMASK(15, 0)
#define SHIM_PSI_CFG0_DST_TAG		GENMASK(31, 16)

#define TI_CSI2RX_MAX_PIX_PER_CLK	4
#define TI_CSI2RX_MAX_CTX		32

/*
 * There are no hard limits on the width or height. The DMA engine can handle
 * all sizes. The max width and height are arbitrary numbers for this driver.
 * Use 16K * 16K as the arbitrary limit. It is large enough that it is unlikely
 * the limit will be hit in practice.
 */
#define MAX_WIDTH_BYTES			SZ_16K
#define MAX_HEIGHT_LINES		SZ_16K

#define TI_CSI2RX_PAD_SINK		0
#define TI_CSI2RX_PAD_FIRST_SOURCE	1
#define TI_CSI2RX_MAX_SOURCE_PADS	TI_CSI2RX_MAX_CTX
#define TI_CSI2RX_MAX_PADS		(1 + TI_CSI2RX_MAX_SOURCE_PADS)

#define DRAIN_TIMEOUT_MS		50
#define DRAIN_BUFFER_SIZE		SZ_32K

#define CSI2RX_BRIDGE_SOURCE_PAD	1

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
	struct ti_csi2rx_ctx		*ctx;
};

enum ti_csi2rx_dma_state {
	TI_CSI2RX_DMA_STOPPED,	/* Streaming not started yet. */
	TI_CSI2RX_DMA_ACTIVE,	/* Streaming and pending DMA operation. */
	TI_CSI2RX_DMA_DRAINING, /* Dumping all the data in drain buffer */
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
};

struct ti_csi2rx_dev;

struct ti_csi2rx_ctx {
	struct ti_csi2rx_dev		*csi;
	struct video_device		vdev;
	struct vb2_queue		vidq;
	struct mutex			mutex; /* To serialize ioctls. */
	struct v4l2_format		v_fmt;
	struct ti_csi2rx_dma		dma;
	struct media_pad		pad;
	struct completion		drain_complete;
	u32				sequence;
	u32				idx;
	u32				vc;
	u32				dt;
	u32				stream;
};

struct ti_csi2rx_dev {
	struct device			*dev;
	void __iomem			*shim;
	unsigned int			enable_count;
	unsigned int			num_ctx;
	struct v4l2_device		v4l2_dev;
	struct media_device		mdev;
	struct media_pipeline		pipe;
	struct media_pad		pads[TI_CSI2RX_MAX_PADS];
	struct v4l2_async_notifier	notifier;
	struct v4l2_subdev		*source;
	struct v4l2_subdev		subdev;
	struct ti_csi2rx_ctx		ctx[TI_CSI2RX_MAX_CTX];
	struct notifier_block		pm_notifier;
	u8				pix_per_clk;
	/* Buffer to drain stale data from PSI-L endpoint */
	struct {
		void			*vaddr;
		dma_addr_t		paddr;
		size_t			len;
	} drain;
};

static inline struct ti_csi2rx_dev *to_csi2rx_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ti_csi2rx_dev, subdev);
}

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
static int ti_csi2rx_start_dma(struct ti_csi2rx_ctx *ctx,
			       struct ti_csi2rx_buffer *buf);

/* Forward declarations needed by ti_csi2rx_drain_callback. */
static int ti_csi2rx_drain_dma(struct ti_csi2rx_ctx *ctx);
static int ti_csi2rx_dma_submit_pending(struct ti_csi2rx_ctx *ctx);

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

	/* Clamp width and height to sensible maximums (16K x 16K) */
	pix->width = clamp_t(unsigned int, pix->width,
			     1, MAX_WIDTH_BYTES * 8 / csi_fmt->bpp);
	pix->height = clamp_t(unsigned int, pix->height, 1, MAX_HEIGHT_LINES);

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

static int ti_csi2rx_g_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_format *f)
{
	struct ti_csi2rx_ctx *csi = video_drvdata(file);

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
	struct ti_csi2rx_ctx *csi = video_drvdata(file);
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

	fmt = find_format_by_fourcc(fsize->pixel_format);
	if (!fmt || fsize->index != 0)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise.min_width = 1;
	fsize->stepwise.max_width = MAX_WIDTH_BYTES * 8 / fmt->bpp;
	fsize->stepwise.step_width = 1;
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
	int ret, i;

	/* Create link from source to subdev */
	ret = media_create_pad_link(&csi->source->entity,
				    CSI2RX_BRIDGE_SOURCE_PAD,
				    &csi->subdev.entity,
				    TI_CSI2RX_PAD_SINK,
				    MEDIA_LNK_FL_IMMUTABLE |
				    MEDIA_LNK_FL_ENABLED);

	if (ret)
		return ret;

	/* Create and link video nodes for all DMA contexts */
	for (i = 0; i < csi->num_ctx; i++) {
		struct ti_csi2rx_ctx *ctx = &csi->ctx[i];
		struct video_device *vdev = &ctx->vdev;

		ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
		if (ret)
			goto unregister_dev;

		ret = media_create_pad_link(&csi->subdev.entity,
					    TI_CSI2RX_PAD_FIRST_SOURCE + ctx->idx,
					    &vdev->entity, 0,
					    MEDIA_LNK_FL_IMMUTABLE |
					    MEDIA_LNK_FL_ENABLED);
		if (ret) {
			video_unregister_device(vdev);
			goto unregister_dev;
		}
	}

	ret = v4l2_device_register_subdev_nodes(&csi->v4l2_dev);
	if (ret)
		goto unregister_dev;

	return 0;

unregister_dev:
	while (i--) {
		media_entity_remove_links(&csi->ctx[i].vdev.entity);
		video_unregister_device(&csi->ctx[i].vdev);
	}
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
	int ret;

	fwnode = fwnode_get_named_child_node(csi->dev->fwnode, "csi-bridge");
	if (!fwnode)
		return -EINVAL;

	v4l2_async_nf_init(&csi->notifier, &csi->v4l2_dev);
	csi->notifier.ops = &csi_async_notifier_ops;

	asc = v4l2_async_nf_add_fwnode(&csi->notifier, fwnode,
				       struct v4l2_async_connection);
	/*
	 * Calling v4l2_async_nf_add_fwnode grabs a refcount,
	 * so drop the one we got in fwnode_get_named_child_node
	 */
	fwnode_handle_put(fwnode);

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

/* Request maximum possible pixels per clock from the bridge */
static void ti_csi2rx_request_max_ppc(struct ti_csi2rx_dev *csi)
{
	u8 ppc = TI_CSI2RX_MAX_PIX_PER_CLK;
	struct media_pad *pad;
	int ret;

	pad = media_entity_remote_source_pad_unique(&csi->subdev.entity);
	if (IS_ERR(pad))
		return;

	ret = cdns_csi2rx_negotiate_ppc(csi->source, pad->index, &ppc);
	if (ret) {
		dev_warn(csi->dev, "NUM_PIXELS negotiation failed: %d\n", ret);
		csi->pix_per_clk = 1;
	} else {
		csi->pix_per_clk = ppc;
	}
}

static void ti_csi2rx_setup_shim(struct ti_csi2rx_ctx *ctx)
{
	struct ti_csi2rx_dev *csi = ctx->csi;
	const struct ti_csi2rx_fmt *fmt;
	unsigned int reg;

	fmt = find_format_by_fourcc(ctx->v_fmt.fmt.pix.pixelformat);

	/* Negotiate pixel count from the source */
	ti_csi2rx_request_max_ppc(csi);

	reg = SHIM_DMACNTX_EN;
	reg |= FIELD_PREP(SHIM_DMACNTX_FMT, ctx->dt);

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
		/* Multiple pixels are handled differently for packed YUV */
		if (csi->pix_per_clk == 2)
			reg |= SHIM_DMACNTX_DUAL_PCK_CFG;
		reg |= FIELD_PREP(SHIM_DMACNTX_SIZE, fmt->size);
		break;
	default:
		/* By default we change the shift size for multiple pixels */
		reg |= FIELD_PREP(SHIM_DMACNTX_SIZE,
				  fmt->size + (csi->pix_per_clk >> 1));
		break;
	}

	reg |= FIELD_PREP(SHIM_DMACNTX_VC, ctx->vc);

	writel(reg, csi->shim + SHIM_DMACNTX(ctx->idx));

	reg = FIELD_PREP(SHIM_PSI_CFG0_SRC_TAG, 0) |
	      FIELD_PREP(SHIM_PSI_CFG0_DST_TAG, 0);
	writel(reg, csi->shim + SHIM_PSI_CFG0(ctx->idx));
}

static void ti_csi2rx_drain_callback(void *param)
{
	struct ti_csi2rx_ctx *ctx = param;
	struct ti_csi2rx_dma *dma = &ctx->dma;
	unsigned long flags;

	spin_lock_irqsave(&dma->lock, flags);

	if (dma->state == TI_CSI2RX_DMA_STOPPED) {
		complete(&ctx->drain_complete);
		spin_unlock_irqrestore(&dma->lock, flags);
		return;
	}

	/*
	 * If dma->queue is empty, it indicates that no buffer has been
	 * provided by user space. In this case, initiate a transactions
	 * to drain the DMA. Since one drain of size DRAIN_BUFFER_SIZE
	 * will be done here, the subsequent frame will be a
	 * partial frame, with a size of frame_size - DRAIN_BUFFER_SIZE
	 */
	if (list_empty(&dma->queue)) {
		if (ti_csi2rx_drain_dma(ctx))
			dev_warn(ctx->csi->dev, "DMA drain failed\n");
	} else {
		ti_csi2rx_dma_submit_pending(ctx);
	}
	spin_unlock_irqrestore(&dma->lock, flags);
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
static int ti_csi2rx_drain_dma(struct ti_csi2rx_ctx *ctx)
{
	struct ti_csi2rx_dev *csi = ctx->csi;
	struct dma_async_tx_descriptor *desc;
	dma_cookie_t cookie;
	int ret;

	desc = dmaengine_prep_slave_single(ctx->dma.chan, csi->drain.paddr,
					   csi->drain.len, DMA_DEV_TO_MEM,
					   DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		ret = -EIO;
		goto out;
	}

	desc->callback = ti_csi2rx_drain_callback;
	desc->callback_param = ctx;

	cookie = dmaengine_submit(desc);
	ret = dma_submit_error(cookie);
	if (ret)
		goto out;

	dma_async_issue_pending(ctx->dma.chan);

out:
	return ret;
}

static int ti_csi2rx_dma_submit_pending(struct ti_csi2rx_ctx *ctx)
{
	struct ti_csi2rx_dma *dma = &ctx->dma;
	struct ti_csi2rx_buffer *buf;
	int ret = 0;

	/* If there are more buffers to process then start their transfer. */
	while (!list_empty(&dma->queue)) {
		buf = list_entry(dma->queue.next, struct ti_csi2rx_buffer, list);
		ret = ti_csi2rx_start_dma(ctx, buf);
		if (ret) {
			dev_err(ctx->csi->dev,
				"Failed to queue the next buffer for DMA\n");
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
			list_del(&buf->list);
		} else {
			list_move_tail(&buf->list, &dma->submitted);
		}
	}
	return ret;
}

static void ti_csi2rx_dma_callback(void *param)
{
	struct ti_csi2rx_buffer *buf = param;
	struct ti_csi2rx_ctx *ctx = buf->ctx;
	struct ti_csi2rx_dma *dma = &ctx->dma;
	unsigned long flags;

	/*
	 * TODO: Derive the sequence number from the CSI2RX frame number
	 * hardware monitor registers.
	 */
	buf->vb.vb2_buf.timestamp = ktime_get_ns();
	buf->vb.sequence = ctx->sequence++;

	spin_lock_irqsave(&dma->lock, flags);

	WARN_ON(!list_is_first(&buf->list, &dma->submitted));

	if (dma->state == TI_CSI2RX_DMA_DRAINING) {
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		dma->state = TI_CSI2RX_DMA_ACTIVE;
	} else {
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}

	list_del(&buf->list);

	ti_csi2rx_dma_submit_pending(ctx);

	if (list_empty(&dma->submitted)) {
		dma->state = TI_CSI2RX_DMA_DRAINING;
		if (ti_csi2rx_drain_dma(ctx))
			dev_warn(ctx->csi->dev,
				 "DMA drain failed on one of the transactions\n");
	}
	spin_unlock_irqrestore(&dma->lock, flags);
}

static int ti_csi2rx_start_dma(struct ti_csi2rx_ctx *ctx,
			       struct ti_csi2rx_buffer *buf)
{
	unsigned long addr;
	struct dma_async_tx_descriptor *desc;
	size_t len = ctx->v_fmt.fmt.pix.sizeimage;
	dma_cookie_t cookie;
	int ret = 0;

	addr = vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 0);
	desc = dmaengine_prep_slave_single(ctx->dma.chan, addr, len,
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

	dma_async_issue_pending(ctx->dma.chan);

	return 0;
}

static void ti_csi2rx_stop_dma(struct ti_csi2rx_ctx *ctx)
{
	struct ti_csi2rx_dma *dma = &ctx->dma;
	enum ti_csi2rx_dma_state state;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&dma->lock, flags);
	state = ctx->dma.state;
	dma->state = TI_CSI2RX_DMA_STOPPED;
	spin_unlock_irqrestore(&dma->lock, flags);

	init_completion(&ctx->drain_complete);

	if (state != TI_CSI2RX_DMA_STOPPED) {
		/*
		 * Normal DMA termination does not clean up pending data on
		 * the endpoint if multiple streams are running and only one
		 * is stopped, as the module-level pixel reset cannot be
		 * enforced before terminating DMA.
		 */
		ret = ti_csi2rx_drain_dma(ctx);
		if (ret)
			dev_warn(ctx->csi->dev,
				 "Failed to drain DMA. Next frame might be bogus\n");
	}

	/* We wait for the drain to complete so that the stream stops
	 * cleanly, making sure the shared hardware FIFO is cleared of
	 * data from the current stream. No more data will be coming from
	 * the source after this.
	 */
	wait_for_completion_timeout(&ctx->drain_complete,
				    msecs_to_jiffies(DRAIN_TIMEOUT_MS));

	ret = dmaengine_terminate_sync(ctx->dma.chan);
	if (ret)
		dev_err(ctx->csi->dev, "Failed to stop DMA: %d\n", ret);
}

static void ti_csi2rx_cleanup_buffers(struct ti_csi2rx_ctx *ctx,
				      enum vb2_buffer_state state)
{
	struct ti_csi2rx_dma *dma = &ctx->dma;
	struct ti_csi2rx_buffer *buf, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&dma->lock, flags);
	list_for_each_entry_safe(buf, tmp, &ctx->dma.queue, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	list_for_each_entry_safe(buf, tmp, &ctx->dma.submitted, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	spin_unlock_irqrestore(&dma->lock, flags);
}

static int ti_csi2rx_queue_setup(struct vb2_queue *q, unsigned int *nbuffers,
				 unsigned int *nplanes, unsigned int sizes[],
				 struct device *alloc_devs[])
{
	struct ti_csi2rx_ctx *ctx = vb2_get_drv_priv(q);
	unsigned int size = ctx->v_fmt.fmt.pix.sizeimage;

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
	struct ti_csi2rx_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long size = ctx->v_fmt.fmt.pix.sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(ctx->csi->dev, "Data will not fit into plane\n");
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, size);
	return 0;
}

static void ti_csi2rx_buffer_queue(struct vb2_buffer *vb)
{
	struct ti_csi2rx_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct ti_csi2rx_buffer *buf;
	struct ti_csi2rx_dma *dma = &ctx->dma;
	unsigned long flags = 0;

	buf = container_of(vb, struct ti_csi2rx_buffer, vb.vb2_buf);
	buf->ctx = ctx;

	spin_lock_irqsave(&dma->lock, flags);
	list_add_tail(&buf->list, &dma->queue);
	spin_unlock_irqrestore(&dma->lock, flags);
}

static int ti_csi2rx_get_stream(struct ti_csi2rx_ctx *ctx)
{
	struct ti_csi2rx_dev *csi = ctx->csi;
	struct media_pad *pad;
	struct v4l2_subdev_state *state;
	struct v4l2_subdev_route *r;

	/* Get the source pad connected to this ctx */
	pad = media_entity_remote_source_pad_unique(ctx->pad.entity);
	if (IS_ERR(pad)) {
		dev_err(csi->dev, "No pad connected to ctx %d\n", ctx->idx);
		return PTR_ERR(pad);
	}

	state = v4l2_subdev_get_locked_active_state(&csi->subdev);

	for_each_active_route(&state->routing, r) {
		if (r->source_pad == pad->index) {
			ctx->stream = r->sink_stream;
			return 0;
		}
	}

	/* No route found for this ctx */
	return -ENODEV;
}

static int ti_csi2rx_get_vc_and_dt(struct ti_csi2rx_ctx *ctx)
{
	struct ti_csi2rx_dev *csi = ctx->csi;
	struct ti_csi2rx_ctx *curr_ctx;
	struct v4l2_mbus_frame_desc fd;
	struct media_pad *source_pad;
	const struct ti_csi2rx_fmt *fmt;
	int ret;
	unsigned int i, j;

	/* Get the frame desc from source */
	source_pad = media_entity_remote_pad_unique(&csi->subdev.entity, MEDIA_PAD_FL_SOURCE);
	if (IS_ERR(source_pad))
		return PTR_ERR(source_pad);

	ret = v4l2_subdev_call(csi->source, pad, get_frame_desc, source_pad->index, &fd);
	if (ret) {
		if (ret == -ENOIOCTLCMD) {
			ctx->vc = 0;
			fmt = find_format_by_fourcc(ctx->v_fmt.fmt.pix.pixelformat);
			ctx->dt = fmt->csi_dt;
		}
		return ret;
	}

	if (fd.type != V4L2_MBUS_FRAME_DESC_TYPE_CSI2)
		return -EINVAL;

	for (i = 0; i < csi->num_ctx; i++) {
		curr_ctx = &csi->ctx[i];

		/* Capture VC 0 by default */
		curr_ctx->vc = 0;

		ret = ti_csi2rx_get_stream(curr_ctx);
		if (ret)
			continue;

		for (j = 0; j < fd.num_entries; j++) {
			if (curr_ctx->stream == fd.entry[j].stream) {
				curr_ctx->vc = fd.entry[j].bus.csi2.vc;
				curr_ctx->dt = fd.entry[j].bus.csi2.dt;
				break;
			}

			/* Return error if no matching stream found */
			if (j == fd.num_entries)
				return -EINVAL;
		}
	}

	return 0;
}

static int ti_csi2rx_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct ti_csi2rx_ctx *ctx = vb2_get_drv_priv(vq);
	struct ti_csi2rx_dev *csi = ctx->csi;
	struct ti_csi2rx_dma *dma = &ctx->dma;
	unsigned long flags;
	int ret;

	ret = pm_runtime_resume_and_get(csi->dev);
	if (ret)
		return ret;

	spin_lock_irqsave(&dma->lock, flags);
	if (list_empty(&dma->queue))
		ret = -EIO;
	spin_unlock_irqrestore(&dma->lock, flags);
	if (ret)
		goto err;

	ret = video_device_pipeline_start(&ctx->vdev, &csi->pipe);
	if (ret)
		goto err;

	/* Start stream 0, we don't allow multiple streams on the source pad */
	ret = v4l2_subdev_enable_streams(&csi->subdev,
					 TI_CSI2RX_PAD_FIRST_SOURCE + ctx->idx,
					 BIT_U64(0));
	if (ret)
		goto err_dma;

	return 0;

err_dma:
	ti_csi2rx_stop_dma(ctx);
	video_device_pipeline_stop(&ctx->vdev);
	writel(0, csi->shim + SHIM_CNTL);
	writel(0, csi->shim + SHIM_DMACNTX(ctx->idx));
err:
	ti_csi2rx_cleanup_buffers(ctx, VB2_BUF_STATE_QUEUED);
	pm_runtime_put(csi->dev);

	return ret;
}

static void ti_csi2rx_stop_streaming(struct vb2_queue *vq)
{
	struct ti_csi2rx_ctx *ctx = vb2_get_drv_priv(vq);
	struct ti_csi2rx_dev *csi = ctx->csi;
	int ret;

	video_device_pipeline_stop(&ctx->vdev);

	ret = v4l2_subdev_disable_streams(&csi->subdev,
					  TI_CSI2RX_PAD_FIRST_SOURCE + ctx->idx,
					  BIT_U64(0));
	if (ret)
		dev_err(csi->dev, "Failed to stop subdev stream\n");

	ti_csi2rx_stop_dma(ctx);
	ti_csi2rx_cleanup_buffers(ctx, VB2_BUF_STATE_ERROR);
	pm_runtime_put(csi->dev);
}

static const struct vb2_ops csi_vb2_qops = {
	.queue_setup = ti_csi2rx_queue_setup,
	.buf_prepare = ti_csi2rx_buffer_prepare,
	.buf_queue = ti_csi2rx_buffer_queue,
	.start_streaming = ti_csi2rx_start_streaming,
	.stop_streaming = ti_csi2rx_stop_streaming,
};

static int ti_csi2rx_enum_mbus_code(struct v4l2_subdev *subdev,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_mbus_code_enum *code_enum)
{
	if (code_enum->index >= ARRAY_SIZE(ti_csi2rx_formats))
		return -EINVAL;

	code_enum->code = ti_csi2rx_formats[code_enum->index].code;

	return 0;
}

static int ti_csi2rx_sd_set_fmt(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt;

	/* No transcoding, don't allow setting source fmt */
	if (format->pad > TI_CSI2RX_PAD_SINK)
		return v4l2_subdev_get_fmt(sd, state, format);

	if (!find_format_by_code(format->format.code))
		format->format.code = ti_csi2rx_formats[0].code;

	format->format.field = V4L2_FIELD_NONE;

	fmt = v4l2_subdev_state_get_format(state, format->pad, format->stream);
	*fmt = format->format;

	fmt = v4l2_subdev_state_get_opposite_stream_format(state, format->pad,
							   format->stream);
	if (!fmt)
		return -EINVAL;

	*fmt = format->format;

	return 0;
}

static int _ti_csi2rx_sd_set_routing(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *state,
				     struct v4l2_subdev_krouting *routing)
{
	int ret;

	static const struct v4l2_mbus_framefmt format = {
		.width = 640,
		.height = 480,
		.code = MEDIA_BUS_FMT_UYVY8_1X16,
		.field = V4L2_FIELD_NONE,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.ycbcr_enc = V4L2_YCBCR_ENC_601,
		.quantization = V4L2_QUANTIZATION_LIM_RANGE,
		.xfer_func = V4L2_XFER_FUNC_SRGB,
	};

	ret = v4l2_subdev_routing_validate(sd, routing,
					   V4L2_SUBDEV_ROUTING_ONLY_1_TO_1 |
					   V4L2_SUBDEV_ROUTING_NO_SOURCE_MULTIPLEXING);

	if (ret)
		return ret;

	/* Only stream ID 0 allowed on source pads */
	for (unsigned int i = 0; i < routing->num_routes; ++i) {
		const struct v4l2_subdev_route *route = &routing->routes[i];

		if (route->source_stream != 0)
			return -EINVAL;
	}

	ret = v4l2_subdev_set_routing_with_fmt(sd, state, routing, &format);

	return ret;
}

static int ti_csi2rx_sd_set_routing(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state,
				    enum v4l2_subdev_format_whence which,
				    struct v4l2_subdev_krouting *routing)
{
	struct ti_csi2rx_dev *csi = to_csi2rx_dev(sd);

	if (csi->enable_count > 0)
		return -EBUSY;

	return _ti_csi2rx_sd_set_routing(sd, state, routing);
}

static int ti_csi2rx_sd_init_state(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_route routes[] = { {
		.sink_pad = 0,
		.sink_stream = 0,
		.source_pad = TI_CSI2RX_PAD_FIRST_SOURCE,
		.source_stream = 0,
		.flags = V4L2_SUBDEV_ROUTE_FL_ACTIVE,
	} };

	struct v4l2_subdev_krouting routing = {
		.num_routes = 1,
		.routes = routes,
	};

	/* Initialize routing to single route to the fist source pad */
	return _ti_csi2rx_sd_set_routing(sd, state, &routing);
}

static int ti_csi2rx_sd_enable_streams(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state,
				       u32 pad, u64 streams_mask)
{
	struct ti_csi2rx_dev *csi = to_csi2rx_dev(sd);
	struct ti_csi2rx_ctx *ctx = &csi->ctx[pad - TI_CSI2RX_PAD_FIRST_SOURCE];
	struct ti_csi2rx_dma *dma = &ctx->dma;
	struct media_pad *remote_pad;
	unsigned long flags;
	u64 sink_streams;
	int ret = 0;
	unsigned int reg;

	ret = ti_csi2rx_get_stream(ctx);
	if (ret)
		return ret;

	/* Get the VC and DT for all enabled ctx on first stream start */
	if (!csi->enable_count) {
		ret = ti_csi2rx_get_vc_and_dt(ctx);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			return ret;

		/* De-assert the pixel interface reset. */
		reg = SHIM_CNTL_PIX_RST;
		writel(reg, csi->shim + SHIM_CNTL);
	}

	ti_csi2rx_setup_shim(ctx);
	ctx->sequence = 0;

	spin_lock_irqsave(&dma->lock, flags);

	ret = ti_csi2rx_dma_submit_pending(ctx);
	if (ret) {
		spin_unlock_irqrestore(&dma->lock, flags);
		return ret;
	}

	dma->state = TI_CSI2RX_DMA_ACTIVE;
	spin_unlock_irqrestore(&dma->lock, flags);

	remote_pad = media_entity_remote_source_pad_unique(&csi->subdev.entity);
	if (IS_ERR(remote_pad))
		return PTR_ERR(remote_pad);
	sink_streams = v4l2_subdev_state_xlate_streams(state, pad,
						       TI_CSI2RX_PAD_SINK,
						       &streams_mask);

	ret = v4l2_subdev_enable_streams(csi->source, remote_pad->index,
					 sink_streams);
	if (ret)
		return ret;

	csi->enable_count++;

	return 0;
}

static int ti_csi2rx_sd_disable_streams(struct v4l2_subdev *sd,
					struct v4l2_subdev_state *state,
					u32 pad, u64 streams_mask)
{
	struct ti_csi2rx_dev *csi = to_csi2rx_dev(sd);
	struct ti_csi2rx_ctx *ctx = &csi->ctx[pad - TI_CSI2RX_PAD_FIRST_SOURCE];
	struct media_pad *remote_pad;
	u64 sink_streams;
	int ret = 0;

	WARN_ON(csi->enable_count == 0);

	writel(0, csi->shim + SHIM_DMACNTX(ctx->idx));

	/* assert pixel reset to prevent stale data */
	if (csi->enable_count == 1)
		writel(0, csi->shim + SHIM_CNTL);

	remote_pad = media_entity_remote_source_pad_unique(&csi->subdev.entity);
	if (IS_ERR(remote_pad))
		return PTR_ERR(remote_pad);
	sink_streams = v4l2_subdev_state_xlate_streams(state, pad,
						       TI_CSI2RX_PAD_SINK,
						       &streams_mask);

	ret = v4l2_subdev_disable_streams(csi->source, remote_pad->index,
					  sink_streams);
	if (!ret)
		--csi->enable_count;

	return 0;
}

static const struct v4l2_subdev_pad_ops ti_csi2rx_subdev_pad_ops = {
	.enum_mbus_code	= ti_csi2rx_enum_mbus_code,
	.set_routing = ti_csi2rx_sd_set_routing,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = ti_csi2rx_sd_set_fmt,
	.enable_streams = ti_csi2rx_sd_enable_streams,
	.disable_streams = ti_csi2rx_sd_disable_streams,
};

static const struct v4l2_subdev_ops ti_csi2rx_subdev_ops = {
	.pad = &ti_csi2rx_subdev_pad_ops,
};

static const struct v4l2_subdev_internal_ops ti_csi2rx_internal_ops = {
	.init_state = ti_csi2rx_sd_init_state,
};

static void ti_csi2rx_cleanup_v4l2(struct ti_csi2rx_dev *csi)
{
	v4l2_subdev_cleanup(&csi->subdev);
	media_device_unregister(&csi->mdev);
	v4l2_device_unregister(&csi->v4l2_dev);
	media_device_cleanup(&csi->mdev);
}

static void ti_csi2rx_cleanup_notifier(struct ti_csi2rx_dev *csi)
{
	v4l2_async_nf_unregister(&csi->notifier);
	v4l2_async_nf_cleanup(&csi->notifier);
}

static void ti_csi2rx_cleanup_ctx(struct ti_csi2rx_ctx *ctx)
{
	if (!pm_runtime_status_suspended(ctx->csi->dev))
		dma_release_channel(ctx->dma.chan);

	vb2_queue_release(&ctx->vidq);

	video_unregister_device(&ctx->vdev);

	mutex_destroy(&ctx->mutex);
}

static int ti_csi2rx_init_vb2q(struct ti_csi2rx_ctx *ctx)
{
	struct vb2_queue *q = &ctx->vidq;
	int ret;

	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->drv_priv = ctx;
	q->buf_struct_size = sizeof(struct ti_csi2rx_buffer);
	q->ops = &csi_vb2_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->dev = dmaengine_get_dma_device(ctx->dma.chan);
	q->lock = &ctx->mutex;
	q->min_queued_buffers = 1;
	q->allow_cache_hints = 1;

	ret = vb2_queue_init(q);
	if (ret)
		return ret;

	ctx->vdev.queue = q;

	return 0;
}

static int ti_csi2rx_link_validate(struct media_link *link)
{
	struct media_entity *entity = link->sink->entity;
	struct video_device *vdev = media_entity_to_video_device(entity);
	struct ti_csi2rx_ctx *ctx = container_of(vdev, struct ti_csi2rx_ctx, vdev);
	struct ti_csi2rx_dev *csi = ctx->csi;
	struct v4l2_pix_format *csi_fmt = &ctx->v_fmt.fmt.pix;
	struct v4l2_mbus_framefmt *format;
	struct v4l2_subdev_state *state;
	const struct ti_csi2rx_fmt *ti_fmt;

	state = v4l2_subdev_lock_and_get_active_state(&csi->subdev);
	format = v4l2_subdev_state_get_format(state, link->source->index, 0);
	v4l2_subdev_unlock_state(state);

	if (!format) {
		dev_err(csi->dev,
			"No format present on \"%s\":%u:0\n",
			link->source->entity->name, link->source->index);
		return 0;
	}

	if (format->width != csi_fmt->width) {
		dev_dbg(csi->dev, "Width does not match (source %u, sink %u)\n",
			format->width, csi_fmt->width);
		return -EPIPE;
	}

	if (format->height != csi_fmt->height) {
		dev_dbg(csi->dev, "Height does not match (source %u, sink %u)\n",
			format->height, csi_fmt->height);
		return -EPIPE;
	}

	if (format->field != csi_fmt->field &&
	    csi_fmt->field != V4L2_FIELD_NONE) {
		dev_dbg(csi->dev, "Field does not match (source %u, sink %u)\n",
			format->field, csi_fmt->field);
		return -EPIPE;
	}

	ti_fmt = find_format_by_code(format->code);
	if (!ti_fmt) {
		dev_dbg(csi->dev, "Media bus format 0x%x not supported\n",
			format->code);
		return -EPIPE;
	}

	if (ti_fmt->fourcc != csi_fmt->pixelformat) {
		dev_dbg(csi->dev,
			"Cannot transform \"%s\":%u format %p4cc to %p4cc\n",
			link->source->entity->name, link->source->index,
			&ti_fmt->fourcc, &csi_fmt->pixelformat);
		return -EPIPE;
	}

	return 0;
}

static const struct media_entity_operations ti_csi2rx_video_entity_ops = {
	.link_validate = ti_csi2rx_link_validate,
};

static const struct media_entity_operations ti_csi2rx_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
	.has_pad_interdep = v4l2_subdev_has_pad_interdep,
};

static int ti_csi2rx_init_dma(struct ti_csi2rx_ctx *ctx)
{
	struct dma_slave_config cfg = {
		.src_addr_width = DMA_SLAVE_BUSWIDTH_16_BYTES,
	};
	char name[5];
	int ret;

	snprintf(name, sizeof(name), "rx%u", ctx->idx);
	ctx->dma.chan = dma_request_chan(ctx->csi->dev, name);
	if (IS_ERR(ctx->dma.chan))
		return PTR_ERR(ctx->dma.chan);

	ret = dmaengine_slave_config(ctx->dma.chan, &cfg);
	if (ret) {
		dma_release_channel(ctx->dma.chan);
		return ret;
	}

	return 0;
}

static int ti_csi2rx_v4l2_init(struct ti_csi2rx_dev *csi)
{
	struct media_device *mdev = &csi->mdev;
	struct v4l2_subdev *sd = &csi->subdev;
	int ret;

	mdev->dev = csi->dev;
	mdev->hw_revision = 1;
	strscpy(mdev->model, "TI-CSI2RX", sizeof(mdev->model));

	media_device_init(mdev);

	csi->v4l2_dev.mdev = mdev;

	ret = v4l2_device_register(csi->dev, &csi->v4l2_dev);
	if (ret)
		goto cleanup_media;

	ret = media_device_register(mdev);
	if (ret)
		goto unregister_v4l2;

	v4l2_subdev_init(sd, &ti_csi2rx_subdev_ops);
	sd->internal_ops = &ti_csi2rx_internal_ops;
	sd->entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	sd->flags = V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_STREAMS;
	strscpy(sd->name, dev_name(csi->dev), sizeof(sd->name));
	sd->dev = csi->dev;
	sd->entity.ops = &ti_csi2rx_subdev_entity_ops;

	csi->pads[TI_CSI2RX_PAD_SINK].flags = MEDIA_PAD_FL_SINK;

	for (unsigned int i = TI_CSI2RX_PAD_FIRST_SOURCE;
	     i < TI_CSI2RX_PAD_FIRST_SOURCE + csi->num_ctx; i++)
		csi->pads[i].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&sd->entity,
				     TI_CSI2RX_PAD_FIRST_SOURCE + csi->num_ctx,
				     csi->pads);
	if (ret)
		goto unregister_media;

	ret = v4l2_subdev_init_finalize(sd);
	if (ret)
		goto unregister_media;

	ret = v4l2_device_register_subdev(&csi->v4l2_dev, sd);
	if (ret)
		goto cleanup_subdev;

	return 0;

cleanup_subdev:
	v4l2_subdev_cleanup(sd);
unregister_media:
	media_device_unregister(mdev);
unregister_v4l2:
	v4l2_device_unregister(&csi->v4l2_dev);
cleanup_media:
	media_device_cleanup(mdev);

	return ret;
}

static int ti_csi2rx_init_ctx(struct ti_csi2rx_ctx *ctx)
{
	struct ti_csi2rx_dev *csi = ctx->csi;
	struct video_device *vdev = &ctx->vdev;
	const struct ti_csi2rx_fmt *fmt;
	struct v4l2_pix_format *pix_fmt = &ctx->v_fmt.fmt.pix;
	int ret;

	mutex_init(&ctx->mutex);

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

	ti_csi2rx_fill_fmt(fmt, &ctx->v_fmt);

	ctx->pad.flags = MEDIA_PAD_FL_SINK;
	vdev->entity.ops = &ti_csi2rx_video_entity_ops;
	ret = media_entity_pads_init(&ctx->vdev.entity, 1, &ctx->pad);
	if (ret)
		return ret;

	snprintf(vdev->name, sizeof(vdev->name), "%s context %u",
		 dev_name(csi->dev), ctx->idx);
	vdev->v4l2_dev = &csi->v4l2_dev;
	vdev->vfl_dir = VFL_DIR_RX;
	vdev->fops = &csi_fops;
	vdev->ioctl_ops = &csi_ioctl_ops;
	vdev->release = video_device_release_empty;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
			    V4L2_CAP_IO_MC;
	vdev->lock = &ctx->mutex;
	video_set_drvdata(vdev, ctx);

	INIT_LIST_HEAD(&ctx->dma.queue);
	INIT_LIST_HEAD(&ctx->dma.submitted);
	spin_lock_init(&ctx->dma.lock);
	ctx->dma.state = TI_CSI2RX_DMA_STOPPED;

	ret = ti_csi2rx_init_dma(ctx);
	if (ret)
		return ret;

	ret = ti_csi2rx_init_vb2q(ctx);
	if (ret)
		goto cleanup_dma;

	return 0;

cleanup_dma:
	dma_release_channel(ctx->dma.chan);
	return ret;
}

static int ti_csi2rx_runtime_suspend(struct device *dev)
{
	struct ti_csi2rx_dev *csi = dev_get_drvdata(dev);

	if (csi->enable_count != 0)
		return -EBUSY;

	for (unsigned int i = 0; i < csi->num_ctx; i++)
		dma_release_channel(csi->ctx[i].dma.chan);

	return 0;
}

static int ti_csi2rx_runtime_resume(struct device *dev)
{
	struct ti_csi2rx_dev *csi = dev_get_drvdata(dev);
	int ret;

	for (unsigned int i = 0; i < csi->num_ctx; i++) {
		ret = ti_csi2rx_init_dma(&csi->ctx[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int ti_csi2rx_suspend(struct device *dev)
{
	struct ti_csi2rx_dev *csi = dev_get_drvdata(dev);
	enum ti_csi2rx_dma_state state;
	struct ti_csi2rx_ctx *ctx;
	struct ti_csi2rx_dma *dma;
	unsigned long flags = 0;
	int ret = 0;

	/* If device was not in use we can simply suspend */
	if (pm_runtime_status_suspended(dev))
		return 0;

	/*
	 * If device is running, assert the pixel reset to cleanly stop any
	 * on-going streams before we suspend.
	 */
	writel(0, csi->shim + SHIM_CNTL);

	for (unsigned int i = 0; i < csi->num_ctx; i++) {
		ctx = &csi->ctx[i];
		dma = &ctx->dma;

		spin_lock_irqsave(&dma->lock, flags);
		state = dma->state;
		spin_unlock_irqrestore(&dma->lock, flags);

		if (state != TI_CSI2RX_DMA_STOPPED) {
			/* Disable source */
			ret = v4l2_subdev_disable_streams(&csi->subdev,
							  TI_CSI2RX_PAD_FIRST_SOURCE + ctx->idx,
							  BIT(0));
			if (ret)
				dev_err(csi->dev, "Failed to stop subdev stream\n");
		}

		/* Stop any on-going streams */
		writel(0, csi->shim + SHIM_DMACNTX(ctx->idx));

		/* Drain DMA */
		ti_csi2rx_drain_dma(ctx);

		/* Terminate DMA */
		ret = dmaengine_terminate_sync(ctx->dma.chan);
		if (ret)
			dev_err(csi->dev, "Failed to stop DMA\n");
	}

	return ret;
}

static int ti_csi2rx_resume(struct device *dev)
{
	struct ti_csi2rx_dev *csi = dev_get_drvdata(dev);
	struct ti_csi2rx_ctx *ctx;
	struct ti_csi2rx_dma *dma;
	struct ti_csi2rx_buffer *buf;
	unsigned long flags = 0;
	unsigned int reg;
	int ret = 0;

	/* If device was not in use, we can simply wakeup */
	if (pm_runtime_status_suspended(dev))
		return 0;

	/* If device was in use before, restore all the running streams */
	reg = SHIM_CNTL_PIX_RST;
	writel(reg, csi->shim + SHIM_CNTL);

	for (unsigned int i = 0; i < csi->num_ctx; i++) {
		ctx = &csi->ctx[i];
		dma = &ctx->dma;
		spin_lock_irqsave(&dma->lock, flags);
		if (dma->state != TI_CSI2RX_DMA_STOPPED) {
			/* Re-submit all previously submitted buffers to DMA */
			list_for_each_entry(buf, &ctx->dma.submitted, list) {
				ti_csi2rx_start_dma(ctx, buf);
			}
			spin_unlock_irqrestore(&dma->lock, flags);

			/* Restore stream config */
			ti_csi2rx_setup_shim(ctx);

			ret = v4l2_subdev_enable_streams(&csi->subdev,
							 TI_CSI2RX_PAD_FIRST_SOURCE + ctx->idx,
							 BIT(0));
			if (ret)
				dev_err(ctx->csi->dev, "Failed to start subdev\n");
		} else {
			spin_unlock_irqrestore(&dma->lock, flags);
		}
	}

	return ret;
}

static int ti_csi2rx_pm_notifier(struct notifier_block *nb,
				 unsigned long action, void *data)
{
	struct ti_csi2rx_dev *csi =
		container_of(nb, struct ti_csi2rx_dev, pm_notifier);

	switch (action) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
	case PM_RESTORE_PREPARE:
		ti_csi2rx_suspend(csi->dev);
		break;
	case PM_POST_SUSPEND:
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
		ti_csi2rx_resume(csi->dev);
		break;
	}

	return NOTIFY_DONE;
}

static const struct dev_pm_ops ti_csi2rx_pm_ops = {
	RUNTIME_PM_OPS(ti_csi2rx_runtime_suspend, ti_csi2rx_runtime_resume,
		       NULL)
};

static int ti_csi2rx_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct ti_csi2rx_dev *csi;
	int ret = 0, i, count;

	csi = devm_kzalloc(&pdev->dev, sizeof(*csi), GFP_KERNEL);
	if (!csi)
		return -ENOMEM;

	csi->dev = &pdev->dev;
	platform_set_drvdata(pdev, csi);

	csi->shim = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(csi->shim)) {
		ret = PTR_ERR(csi->shim);
		return ret;
	}

	csi->drain.len = DRAIN_BUFFER_SIZE;
	csi->drain.vaddr = dma_alloc_coherent(csi->dev, csi->drain.len,
					      &csi->drain.paddr,
					      GFP_KERNEL);
	if (!csi->drain.vaddr)
		return -ENOMEM;

	/* Only use as many contexts as the number of DMA channels allocated. */
	count = of_property_count_strings(np, "dma-names");
	if (count < 0) {
		dev_err(csi->dev, "Failed to get DMA channel count: %d\n", count);
		ret = count;
		goto err_dma_chan;
	}

	csi->num_ctx = count;
	if (csi->num_ctx > TI_CSI2RX_MAX_CTX) {
		dev_err(csi->dev,
			"%u DMA channels passed. Maximum is %u.\n",
			csi->num_ctx, TI_CSI2RX_MAX_CTX);
		ret = -EINVAL;
		goto err_dma_chan;
	}

	ret = ti_csi2rx_v4l2_init(csi);
	if (ret)
		goto err_dma_chan;

	for (i = 0; i < csi->num_ctx; i++) {
		csi->ctx[i].idx = i;
		csi->ctx[i].csi = csi;
		ret = ti_csi2rx_init_ctx(&csi->ctx[i]);
		if (ret)
			goto err_ctx;
	}

	pm_runtime_set_active(csi->dev);
	pm_runtime_enable(csi->dev);

	ret = ti_csi2rx_notifier_register(csi);
	if (ret)
		goto err_ctx;

	ret = devm_of_platform_populate(csi->dev);
	if (ret) {
		dev_err(csi->dev, "Failed to create children: %d\n", ret);
		goto err_notifier;
	}

	/*
	 * Use PM notifier instead of .suspend/.resume callbacks because the
	 * ordering of callbacks among camera pipeline devices (sensor, serdes,
	 * CSI bridge) cannot be enforced even with device links. The notifier
	 * is called when the system is fully functional, ensuring all
	 * dependencies are available when stopping/starting streams.
	 */
	csi->pm_notifier.notifier_call = ti_csi2rx_pm_notifier;
	ret = register_pm_notifier(&csi->pm_notifier);
	if (ret) {
		dev_err(csi->dev, "Failed to create PM notifier: %d\n", ret);
		goto err_notifier;
	}

	return 0;

err_notifier:
	ti_csi2rx_cleanup_notifier(csi);
err_ctx:
	while (i--)
		ti_csi2rx_cleanup_ctx(&csi->ctx[i]);
	ti_csi2rx_cleanup_v4l2(csi);
err_dma_chan:
	dma_free_coherent(csi->dev, csi->drain.len, csi->drain.vaddr,
			  csi->drain.paddr);
	return ret;
}

static void ti_csi2rx_remove(struct platform_device *pdev)
{
	struct ti_csi2rx_dev *csi = platform_get_drvdata(pdev);

	if (!pm_runtime_status_suspended(&pdev->dev))
		pm_runtime_set_suspended(&pdev->dev);

	for (unsigned int i = 0; i < csi->num_ctx; i++)
		ti_csi2rx_cleanup_ctx(&csi->ctx[i]);

	ti_csi2rx_cleanup_notifier(csi);
	unregister_pm_notifier(&csi->pm_notifier);

	ti_csi2rx_cleanup_v4l2(csi);
	dma_free_coherent(csi->dev, csi->drain.len, csi->drain.vaddr,
			  csi->drain.paddr);
	pm_runtime_disable(&pdev->dev);
}

static const struct of_device_id ti_csi2rx_of_match[] = {
	{ .compatible = "ti,j721e-csi2rx-shim", },
	{ },
};
MODULE_DEVICE_TABLE(of, ti_csi2rx_of_match);

static struct platform_driver ti_csi2rx_pdrv = {
	.probe = ti_csi2rx_probe,
	.remove = ti_csi2rx_remove,
	.driver = {
		.name = TI_CSI2RX_MODULE_NAME,
		.of_match_table = ti_csi2rx_of_match,
		.pm		= &ti_csi2rx_pm_ops,
	},
};

module_platform_driver(ti_csi2rx_pdrv);

MODULE_DESCRIPTION("TI J721E CSI2 RX Driver");
MODULE_AUTHOR("Jai Luthra <j-luthra@ti.com>");
MODULE_LICENSE("GPL");
