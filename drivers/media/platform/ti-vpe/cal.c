// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI Camera Access Layer (CAL) - Driver
 *
 * Copyright (c) 2015-2020 Texas Instruments Inc.
 *
 * Authors:
 *	Benoit Parrot <bparrot@ti.com>
 *	Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/media-device.h>
#include <media/v4l2-async.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "cal.h"
#include "cal_regs.h"

MODULE_DESCRIPTION("TI CAL driver");
MODULE_AUTHOR("Benoit Parrot, <bparrot@ti.com>");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("0.1.0");

int cal_video_nr = -1;
module_param_named(video_nr, cal_video_nr, uint, 0644);
MODULE_PARM_DESC(video_nr, "videoX start number, -1 is autodetect");

unsigned int cal_debug;
module_param_named(debug, cal_debug, uint, 0644);
MODULE_PARM_DESC(debug, "activates debug info");

/* ------------------------------------------------------------------
 *	Format Handling
 * ------------------------------------------------------------------
 */

const struct cal_format_info cal_formats[] = {
	{
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.code		= MEDIA_BUS_FMT_YUYV8_2X8,
		.bpp		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_UYVY,
		.code		= MEDIA_BUS_FMT_UYVY8_2X8,
		.bpp		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_YVYU,
		.code		= MEDIA_BUS_FMT_YVYU8_2X8,
		.bpp		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_VYUY,
		.code		= MEDIA_BUS_FMT_VYUY8_2X8,
		.bpp		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_RGB565, /* gggbbbbb rrrrrggg */
		.code		= MEDIA_BUS_FMT_RGB565_2X8_LE,
		.bpp		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_RGB565X, /* rrrrrggg gggbbbbb */
		.code		= MEDIA_BUS_FMT_RGB565_2X8_BE,
		.bpp		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_RGB555, /* gggbbbbb arrrrrgg */
		.code		= MEDIA_BUS_FMT_RGB555_2X8_PADHI_LE,
		.bpp		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_RGB555X, /* arrrrrgg gggbbbbb */
		.code		= MEDIA_BUS_FMT_RGB555_2X8_PADHI_BE,
		.bpp		= 16,
	}, {
		.fourcc		= V4L2_PIX_FMT_RGB24, /* rgb */
		.code		= MEDIA_BUS_FMT_RGB888_2X12_LE,
		.bpp		= 24,
	}, {
		.fourcc		= V4L2_PIX_FMT_BGR24, /* bgr */
		.code		= MEDIA_BUS_FMT_RGB888_2X12_BE,
		.bpp		= 24,
	}, {
		.fourcc		= V4L2_PIX_FMT_RGB32, /* argb */
		.code		= MEDIA_BUS_FMT_ARGB8888_1X32,
		.bpp		= 32,
	}, {
		.fourcc		= V4L2_PIX_FMT_SBGGR8,
		.code		= MEDIA_BUS_FMT_SBGGR8_1X8,
		.bpp		= 8,
	}, {
		.fourcc		= V4L2_PIX_FMT_SGBRG8,
		.code		= MEDIA_BUS_FMT_SGBRG8_1X8,
		.bpp		= 8,
	}, {
		.fourcc		= V4L2_PIX_FMT_SGRBG8,
		.code		= MEDIA_BUS_FMT_SGRBG8_1X8,
		.bpp		= 8,
	}, {
		.fourcc		= V4L2_PIX_FMT_SRGGB8,
		.code		= MEDIA_BUS_FMT_SRGGB8_1X8,
		.bpp		= 8,
	}, {
		.fourcc		= V4L2_PIX_FMT_SBGGR10,
		.code		= MEDIA_BUS_FMT_SBGGR10_1X10,
		.bpp		= 10,
	}, {
		.fourcc		= V4L2_PIX_FMT_SGBRG10,
		.code		= MEDIA_BUS_FMT_SGBRG10_1X10,
		.bpp		= 10,
	}, {
		.fourcc		= V4L2_PIX_FMT_SGRBG10,
		.code		= MEDIA_BUS_FMT_SGRBG10_1X10,
		.bpp		= 10,
	}, {
		.fourcc		= V4L2_PIX_FMT_SRGGB10,
		.code		= MEDIA_BUS_FMT_SRGGB10_1X10,
		.bpp		= 10,
	}, {
		.fourcc		= V4L2_PIX_FMT_SBGGR12,
		.code		= MEDIA_BUS_FMT_SBGGR12_1X12,
		.bpp		= 12,
	}, {
		.fourcc		= V4L2_PIX_FMT_SGBRG12,
		.code		= MEDIA_BUS_FMT_SGBRG12_1X12,
		.bpp		= 12,
	}, {
		.fourcc		= V4L2_PIX_FMT_SGRBG12,
		.code		= MEDIA_BUS_FMT_SGRBG12_1X12,
		.bpp		= 12,
	}, {
		.fourcc		= V4L2_PIX_FMT_SRGGB12,
		.code		= MEDIA_BUS_FMT_SRGGB12_1X12,
		.bpp		= 12,
	},
};

const unsigned int cal_num_formats = ARRAY_SIZE(cal_formats);

const struct cal_format_info *cal_format_by_fourcc(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cal_formats); ++i) {
		if (cal_formats[i].fourcc == fourcc)
			return &cal_formats[i];
	}

	return NULL;
}

const struct cal_format_info *cal_format_by_code(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cal_formats); ++i) {
		if (cal_formats[i].code == code)
			return &cal_formats[i];
	}

	return NULL;
}

/* ------------------------------------------------------------------
 *	Platform Data
 * ------------------------------------------------------------------
 */

static const struct cal_camerarx_data dra72x_cal_camerarx[] = {
	{
		.fields = {
			[F_CTRLCLKEN] = { 10, 10 },
			[F_CAMMODE] = { 11, 12 },
			[F_LANEENABLE] = { 13, 16 },
			[F_CSI_MODE] = { 17, 17 },
		},
		.num_lanes = 4,
	},
	{
		.fields = {
			[F_CTRLCLKEN] = { 0, 0 },
			[F_CAMMODE] = { 1, 2 },
			[F_LANEENABLE] = { 3, 4 },
			[F_CSI_MODE] = { 5, 5 },
		},
		.num_lanes = 2,
	},
};

static const struct cal_data dra72x_cal_data = {
	.camerarx = dra72x_cal_camerarx,
	.num_csi2_phy = ARRAY_SIZE(dra72x_cal_camerarx),
};

static const struct cal_data dra72x_es1_cal_data = {
	.camerarx = dra72x_cal_camerarx,
	.num_csi2_phy = ARRAY_SIZE(dra72x_cal_camerarx),
	.flags = DRA72_CAL_PRE_ES2_LDO_DISABLE,
};

static const struct cal_camerarx_data dra76x_cal_csi_phy[] = {
	{
		.fields = {
			[F_CTRLCLKEN] = { 8, 8 },
			[F_CAMMODE] = { 9, 10 },
			[F_CSI_MODE] = { 11, 11 },
			[F_LANEENABLE] = { 27, 31 },
		},
		.num_lanes = 5,
	},
	{
		.fields = {
			[F_CTRLCLKEN] = { 0, 0 },
			[F_CAMMODE] = { 1, 2 },
			[F_CSI_MODE] = { 3, 3 },
			[F_LANEENABLE] = { 24, 26 },
		},
		.num_lanes = 3,
	},
};

static const struct cal_data dra76x_cal_data = {
	.camerarx = dra76x_cal_csi_phy,
	.num_csi2_phy = ARRAY_SIZE(dra76x_cal_csi_phy),
};

static const struct cal_camerarx_data am654_cal_csi_phy[] = {
	{
		.fields = {
			[F_CTRLCLKEN] = { 15, 15 },
			[F_CAMMODE] = { 24, 25 },
			[F_LANEENABLE] = { 0, 4 },
		},
		.num_lanes = 5,
	},
};

static const struct cal_data am654_cal_data = {
	.camerarx = am654_cal_csi_phy,
	.num_csi2_phy = ARRAY_SIZE(am654_cal_csi_phy),
};

/* ------------------------------------------------------------------
 *	I/O Register Accessors
 * ------------------------------------------------------------------
 */

void cal_quickdump_regs(struct cal_dev *cal)
{
	unsigned int i;

	cal_info(cal, "CAL Registers @ 0x%pa:\n", &cal->res->start);
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 16, 4,
		       (__force const void *)cal->base,
		       resource_size(cal->res), false);

	for (i = 0; i < cal->data->num_csi2_phy; ++i) {
		struct cal_camerarx *phy = cal->phy[i];

		cal_info(cal, "CSI2 Core %u Registers @ %pa:\n", i,
			 &phy->res->start);
		print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 16, 4,
			       (__force const void *)phy->base,
			       resource_size(phy->res),
			       false);
	}
}

/* ------------------------------------------------------------------
 *	Context Management
 * ------------------------------------------------------------------
 */

static void cal_ctx_csi2_config(struct cal_ctx *ctx)
{
	u32 val;

	val = cal_read(ctx->cal, CAL_CSI2_CTX0(ctx->index));
	cal_set_field(&val, ctx->cport, CAL_CSI2_CTX_CPORT_MASK);
	/*
	 * DT type: MIPI CSI-2 Specs
	 *   0x1: All - DT filter is disabled
	 *  0x24: RGB888 1 pixel  = 3 bytes
	 *  0x2B: RAW10  4 pixels = 5 bytes
	 *  0x2A: RAW8   1 pixel  = 1 byte
	 *  0x1E: YUV422 2 pixels = 4 bytes
	 */
	cal_set_field(&val, 0x1, CAL_CSI2_CTX_DT_MASK);
	cal_set_field(&val, 0, CAL_CSI2_CTX_VC_MASK);
	cal_set_field(&val, ctx->v_fmt.fmt.pix.height, CAL_CSI2_CTX_LINES_MASK);
	cal_set_field(&val, CAL_CSI2_CTX_ATT_PIX, CAL_CSI2_CTX_ATT_MASK);
	cal_set_field(&val, CAL_CSI2_CTX_PACK_MODE_LINE,
		      CAL_CSI2_CTX_PACK_MODE_MASK);
	cal_write(ctx->cal, CAL_CSI2_CTX0(ctx->index), val);
	ctx_dbg(3, ctx, "CAL_CSI2_CTX0(%d) = 0x%08x\n", ctx->index,
		cal_read(ctx->cal, CAL_CSI2_CTX0(ctx->index)));
}

static void cal_ctx_pix_proc_config(struct cal_ctx *ctx)
{
	u32 val, extract, pack;

	switch (ctx->fmtinfo->bpp) {
	case 8:
		extract = CAL_PIX_PROC_EXTRACT_B8;
		pack = CAL_PIX_PROC_PACK_B8;
		break;
	case 10:
		extract = CAL_PIX_PROC_EXTRACT_B10_MIPI;
		pack = CAL_PIX_PROC_PACK_B16;
		break;
	case 12:
		extract = CAL_PIX_PROC_EXTRACT_B12_MIPI;
		pack = CAL_PIX_PROC_PACK_B16;
		break;
	case 16:
		extract = CAL_PIX_PROC_EXTRACT_B16_LE;
		pack = CAL_PIX_PROC_PACK_B16;
		break;
	default:
		/*
		 * If you see this warning then it means that you added
		 * some new entry in the cal_formats[] array with a different
		 * bit per pixel values then the one supported below.
		 * Either add support for the new bpp value below or adjust
		 * the new entry to use one of the value below.
		 *
		 * Instead of failing here just use 8 bpp as a default.
		 */
		dev_warn_once(ctx->cal->dev,
			      "%s:%d:%s: bpp:%d unsupported! Overwritten with 8.\n",
			      __FILE__, __LINE__, __func__, ctx->fmtinfo->bpp);
		extract = CAL_PIX_PROC_EXTRACT_B8;
		pack = CAL_PIX_PROC_PACK_B8;
		break;
	}

	val = cal_read(ctx->cal, CAL_PIX_PROC(ctx->index));
	cal_set_field(&val, extract, CAL_PIX_PROC_EXTRACT_MASK);
	cal_set_field(&val, CAL_PIX_PROC_DPCMD_BYPASS, CAL_PIX_PROC_DPCMD_MASK);
	cal_set_field(&val, CAL_PIX_PROC_DPCME_BYPASS, CAL_PIX_PROC_DPCME_MASK);
	cal_set_field(&val, pack, CAL_PIX_PROC_PACK_MASK);
	cal_set_field(&val, ctx->cport, CAL_PIX_PROC_CPORT_MASK);
	cal_set_field(&val, 1, CAL_PIX_PROC_EN_MASK);
	cal_write(ctx->cal, CAL_PIX_PROC(ctx->index), val);
	ctx_dbg(3, ctx, "CAL_PIX_PROC(%d) = 0x%08x\n", ctx->index,
		cal_read(ctx->cal, CAL_PIX_PROC(ctx->index)));
}

static void cal_ctx_wr_dma_config(struct cal_ctx *ctx)
{
	unsigned int stride = ctx->v_fmt.fmt.pix.bytesperline;
	u32 val;

	val = cal_read(ctx->cal, CAL_WR_DMA_CTRL(ctx->index));
	cal_set_field(&val, ctx->cport, CAL_WR_DMA_CTRL_CPORT_MASK);
	cal_set_field(&val, ctx->v_fmt.fmt.pix.height,
		      CAL_WR_DMA_CTRL_YSIZE_MASK);
	cal_set_field(&val, CAL_WR_DMA_CTRL_DTAG_PIX_DAT,
		      CAL_WR_DMA_CTRL_DTAG_MASK);
	cal_set_field(&val, CAL_WR_DMA_CTRL_MODE_CONST,
		      CAL_WR_DMA_CTRL_MODE_MASK);
	cal_set_field(&val, CAL_WR_DMA_CTRL_PATTERN_LINEAR,
		      CAL_WR_DMA_CTRL_PATTERN_MASK);
	cal_set_field(&val, 1, CAL_WR_DMA_CTRL_STALL_RD_MASK);
	cal_write(ctx->cal, CAL_WR_DMA_CTRL(ctx->index), val);
	ctx_dbg(3, ctx, "CAL_WR_DMA_CTRL(%d) = 0x%08x\n", ctx->index,
		cal_read(ctx->cal, CAL_WR_DMA_CTRL(ctx->index)));

	cal_write_field(ctx->cal, CAL_WR_DMA_OFST(ctx->index),
			stride / 16, CAL_WR_DMA_OFST_MASK);
	ctx_dbg(3, ctx, "CAL_WR_DMA_OFST(%d) = 0x%08x\n", ctx->index,
		cal_read(ctx->cal, CAL_WR_DMA_OFST(ctx->index)));

	val = cal_read(ctx->cal, CAL_WR_DMA_XSIZE(ctx->index));
	/* 64 bit word means no skipping */
	cal_set_field(&val, 0, CAL_WR_DMA_XSIZE_XSKIP_MASK);
	/*
	 * The XSIZE field is expressed in 64-bit units and prevents overflows
	 * in case of synchronization issues by limiting the number of bytes
	 * written per line.
	 */
	cal_set_field(&val, stride / 8, CAL_WR_DMA_XSIZE_MASK);
	cal_write(ctx->cal, CAL_WR_DMA_XSIZE(ctx->index), val);
	ctx_dbg(3, ctx, "CAL_WR_DMA_XSIZE(%d) = 0x%08x\n", ctx->index,
		cal_read(ctx->cal, CAL_WR_DMA_XSIZE(ctx->index)));

	val = cal_read(ctx->cal, CAL_CTRL);
	cal_set_field(&val, CAL_CTRL_BURSTSIZE_BURST128,
		      CAL_CTRL_BURSTSIZE_MASK);
	cal_set_field(&val, 0xF, CAL_CTRL_TAGCNT_MASK);
	cal_set_field(&val, CAL_CTRL_POSTED_WRITES_NONPOSTED,
		      CAL_CTRL_POSTED_WRITES_MASK);
	cal_set_field(&val, 0xFF, CAL_CTRL_MFLAGL_MASK);
	cal_set_field(&val, 0xFF, CAL_CTRL_MFLAGH_MASK);
	cal_write(ctx->cal, CAL_CTRL, val);
	ctx_dbg(3, ctx, "CAL_CTRL = 0x%08x\n", cal_read(ctx->cal, CAL_CTRL));
}

void cal_ctx_set_dma_addr(struct cal_ctx *ctx, dma_addr_t addr)
{
	cal_write(ctx->cal, CAL_WR_DMA_ADDR(ctx->index), addr);
}

static void cal_ctx_wr_dma_disable(struct cal_ctx *ctx)
{
	u32 val = cal_read(ctx->cal, CAL_WR_DMA_CTRL(ctx->index));

	cal_set_field(&val, CAL_WR_DMA_CTRL_MODE_DIS,
		      CAL_WR_DMA_CTRL_MODE_MASK);
	cal_write(ctx->cal, CAL_WR_DMA_CTRL(ctx->index), val);
}

static bool cal_ctx_wr_dma_stopped(struct cal_ctx *ctx)
{
	bool stopped;

	spin_lock_irq(&ctx->dma.lock);
	stopped = ctx->dma.state == CAL_DMA_STOPPED;
	spin_unlock_irq(&ctx->dma.lock);

	return stopped;
}

void cal_ctx_start(struct cal_ctx *ctx)
{
	ctx->sequence = 0;
	ctx->dma.state = CAL_DMA_RUNNING;

	/* Configure the CSI-2, pixel processing and write DMA contexts. */
	cal_ctx_csi2_config(ctx);
	cal_ctx_pix_proc_config(ctx);
	cal_ctx_wr_dma_config(ctx);

	/* Enable IRQ_WDMA_END and IRQ_WDMA_START. */
	cal_write(ctx->cal, CAL_HL_IRQENABLE_SET(1),
		  CAL_HL_IRQ_MASK(ctx->index));
	cal_write(ctx->cal, CAL_HL_IRQENABLE_SET(2),
		  CAL_HL_IRQ_MASK(ctx->index));
}

void cal_ctx_stop(struct cal_ctx *ctx)
{
	long timeout;

	/*
	 * Request DMA stop and wait until it completes. If completion times
	 * out, forcefully disable the DMA.
	 */
	spin_lock_irq(&ctx->dma.lock);
	ctx->dma.state = CAL_DMA_STOP_REQUESTED;
	spin_unlock_irq(&ctx->dma.lock);

	timeout = wait_event_timeout(ctx->dma.wait, cal_ctx_wr_dma_stopped(ctx),
				     msecs_to_jiffies(500));
	if (!timeout) {
		ctx_err(ctx, "failed to disable dma cleanly\n");
		cal_ctx_wr_dma_disable(ctx);
	}

	/* Disable IRQ_WDMA_END and IRQ_WDMA_START. */
	cal_write(ctx->cal, CAL_HL_IRQENABLE_CLR(1),
		  CAL_HL_IRQ_MASK(ctx->index));
	cal_write(ctx->cal, CAL_HL_IRQENABLE_CLR(2),
		  CAL_HL_IRQ_MASK(ctx->index));

	ctx->dma.state = CAL_DMA_STOPPED;
}

/* ------------------------------------------------------------------
 *	IRQ Handling
 * ------------------------------------------------------------------
 */

static inline void cal_irq_wdma_start(struct cal_ctx *ctx)
{
	spin_lock(&ctx->dma.lock);

	if (ctx->dma.state == CAL_DMA_STOP_REQUESTED) {
		/*
		 * If a stop is requested, disable the write DMA context
		 * immediately. The CAL_WR_DMA_CTRL_j.MODE field is shadowed,
		 * the current frame will complete and the DMA will then stop.
		 */
		cal_ctx_wr_dma_disable(ctx);
		ctx->dma.state = CAL_DMA_STOP_PENDING;
	} else if (!list_empty(&ctx->dma.queue) && !ctx->dma.pending) {
		/*
		 * Otherwise, if a new buffer is available, queue it to the
		 * hardware.
		 */
		struct cal_buffer *buf;
		dma_addr_t addr;

		buf = list_first_entry(&ctx->dma.queue, struct cal_buffer,
				       list);
		addr = vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 0);
		cal_ctx_set_dma_addr(ctx, addr);

		ctx->dma.pending = buf;
		list_del(&buf->list);
	}

	spin_unlock(&ctx->dma.lock);
}

static inline void cal_irq_wdma_end(struct cal_ctx *ctx)
{
	struct cal_buffer *buf = NULL;

	spin_lock(&ctx->dma.lock);

	/* If the DMA context was stopping, it is now stopped. */
	if (ctx->dma.state == CAL_DMA_STOP_PENDING) {
		ctx->dma.state = CAL_DMA_STOPPED;
		wake_up(&ctx->dma.wait);
	}

	/* If a new buffer was queued, complete the current buffer. */
	if (ctx->dma.pending) {
		buf = ctx->dma.active;
		ctx->dma.active = ctx->dma.pending;
		ctx->dma.pending = NULL;
	}

	spin_unlock(&ctx->dma.lock);

	if (buf) {
		buf->vb.vb2_buf.timestamp = ktime_get_ns();
		buf->vb.field = ctx->v_fmt.fmt.pix.field;
		buf->vb.sequence = ctx->sequence++;
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}
}

static irqreturn_t cal_irq(int irq_cal, void *data)
{
	struct cal_dev *cal = data;
	u32 status;

	status = cal_read(cal, CAL_HL_IRQSTATUS(0));
	if (status) {
		unsigned int i;

		cal_write(cal, CAL_HL_IRQSTATUS(0), status);

		if (status & CAL_HL_IRQ_OCPO_ERR_MASK)
			dev_err_ratelimited(cal->dev, "OCPO ERROR\n");

		for (i = 0; i < cal->data->num_csi2_phy; ++i) {
			if (status & CAL_HL_IRQ_CIO_MASK(i)) {
				u32 cio_stat = cal_read(cal,
							CAL_CSI2_COMPLEXIO_IRQSTATUS(i));

				dev_err_ratelimited(cal->dev,
						    "CIO%u error: %#08x\n", i, cio_stat);

				cal_write(cal, CAL_CSI2_COMPLEXIO_IRQSTATUS(i),
					  cio_stat);
			}
		}
	}

	/* Check which DMA just finished */
	status = cal_read(cal, CAL_HL_IRQSTATUS(1));
	if (status) {
		unsigned int i;

		/* Clear Interrupt status */
		cal_write(cal, CAL_HL_IRQSTATUS(1), status);

		for (i = 0; i < ARRAY_SIZE(cal->ctx); ++i) {
			if (status & CAL_HL_IRQ_MASK(i))
				cal_irq_wdma_end(cal->ctx[i]);
		}
	}

	/* Check which DMA just started */
	status = cal_read(cal, CAL_HL_IRQSTATUS(2));
	if (status) {
		unsigned int i;

		/* Clear Interrupt status */
		cal_write(cal, CAL_HL_IRQSTATUS(2), status);

		for (i = 0; i < ARRAY_SIZE(cal->ctx); ++i) {
			if (status & CAL_HL_IRQ_MASK(i))
				cal_irq_wdma_start(cal->ctx[i]);
		}
	}

	return IRQ_HANDLED;
}

/* ------------------------------------------------------------------
 *	Asynchronous V4L2 subdev binding
 * ------------------------------------------------------------------
 */

struct cal_v4l2_async_subdev {
	struct v4l2_async_subdev asd; /* Must be first */
	struct cal_camerarx *phy;
};

static inline struct cal_v4l2_async_subdev *
to_cal_asd(struct v4l2_async_subdev *asd)
{
	return container_of(asd, struct cal_v4l2_async_subdev, asd);
}

static int cal_async_notifier_bound(struct v4l2_async_notifier *notifier,
				    struct v4l2_subdev *subdev,
				    struct v4l2_async_subdev *asd)
{
	struct cal_camerarx *phy = to_cal_asd(asd)->phy;
	int pad;
	int ret;

	if (phy->sensor) {
		phy_info(phy, "Rejecting subdev %s (Already set!!)",
			 subdev->name);
		return 0;
	}

	phy->sensor = subdev;
	phy_dbg(1, phy, "Using sensor %s for capture\n", subdev->name);

	pad = media_entity_get_fwnode_pad(&subdev->entity,
					  of_fwnode_handle(phy->sensor_ep_node),
					  MEDIA_PAD_FL_SOURCE);
	if (pad < 0) {
		phy_err(phy, "Sensor %s has no connected source pad\n",
			subdev->name);
		return pad;
	}

	ret = media_create_pad_link(&subdev->entity, pad,
				    &phy->subdev.entity, CAL_CAMERARX_PAD_SINK,
				    MEDIA_LNK_FL_IMMUTABLE |
				    MEDIA_LNK_FL_ENABLED);
	if (ret) {
		phy_err(phy, "Failed to create media link for sensor %s\n",
			subdev->name);
		return ret;
	}

	return 0;
}

static int cal_async_notifier_complete(struct v4l2_async_notifier *notifier)
{
	struct cal_dev *cal = container_of(notifier, struct cal_dev, notifier);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cal->ctx); ++i) {
		if (cal->ctx[i])
			cal_ctx_v4l2_register(cal->ctx[i]);
	}

	return 0;
}

static const struct v4l2_async_notifier_operations cal_async_notifier_ops = {
	.bound = cal_async_notifier_bound,
	.complete = cal_async_notifier_complete,
};

static int cal_async_notifier_register(struct cal_dev *cal)
{
	unsigned int i;
	int ret;

	v4l2_async_notifier_init(&cal->notifier);
	cal->notifier.ops = &cal_async_notifier_ops;

	for (i = 0; i < cal->data->num_csi2_phy; ++i) {
		struct cal_camerarx *phy = cal->phy[i];
		struct cal_v4l2_async_subdev *casd;
		struct fwnode_handle *fwnode;

		if (!phy->sensor_node)
			continue;

		fwnode = of_fwnode_handle(phy->sensor_node);
		casd = v4l2_async_notifier_add_fwnode_subdev(&cal->notifier,
							     fwnode,
							     struct cal_v4l2_async_subdev);
		if (IS_ERR(casd)) {
			phy_err(phy, "Failed to add subdev to notifier\n");
			ret = PTR_ERR(casd);
			goto error;
		}

		casd->phy = phy;
	}

	ret = v4l2_async_notifier_register(&cal->v4l2_dev, &cal->notifier);
	if (ret) {
		cal_err(cal, "Error registering async notifier\n");
		goto error;
	}

	return 0;

error:
	v4l2_async_notifier_cleanup(&cal->notifier);
	return ret;
}

static void cal_async_notifier_unregister(struct cal_dev *cal)
{
	v4l2_async_notifier_unregister(&cal->notifier);
	v4l2_async_notifier_cleanup(&cal->notifier);
}

/* ------------------------------------------------------------------
 *	Media and V4L2 device handling
 * ------------------------------------------------------------------
 */

/*
 * Register user-facing devices. To be called at the end of the probe function
 * when all resources are initialized and ready.
 */
static int cal_media_register(struct cal_dev *cal)
{
	int ret;

	ret = media_device_register(&cal->mdev);
	if (ret) {
		cal_err(cal, "Failed to register media device\n");
		return ret;
	}

	/*
	 * Register the async notifier. This may trigger registration of the
	 * V4L2 video devices if all subdevs are ready.
	 */
	ret = cal_async_notifier_register(cal);
	if (ret) {
		media_device_unregister(&cal->mdev);
		return ret;
	}

	return 0;
}

/*
 * Unregister the user-facing devices, but don't free memory yet. To be called
 * at the beginning of the remove function, to disallow access from userspace.
 */
static void cal_media_unregister(struct cal_dev *cal)
{
	unsigned int i;

	/* Unregister all the V4L2 video devices. */
	for (i = 0; i < ARRAY_SIZE(cal->ctx); i++) {
		if (cal->ctx[i])
			cal_ctx_v4l2_unregister(cal->ctx[i]);
	}

	cal_async_notifier_unregister(cal);
	media_device_unregister(&cal->mdev);
}

/*
 * Initialize the in-kernel objects. To be called at the beginning of the probe
 * function, before the V4L2 device is used by the driver.
 */
static int cal_media_init(struct cal_dev *cal)
{
	struct media_device *mdev = &cal->mdev;
	int ret;

	mdev->dev = cal->dev;
	mdev->hw_revision = cal->revision;
	strscpy(mdev->model, "CAL", sizeof(mdev->model));
	snprintf(mdev->bus_info, sizeof(mdev->bus_info), "platform:%s",
		 dev_name(mdev->dev));
	media_device_init(mdev);

	/*
	 * Initialize the V4L2 device (despite the function name, this performs
	 * initialization, not registration).
	 */
	cal->v4l2_dev.mdev = mdev;
	ret = v4l2_device_register(cal->dev, &cal->v4l2_dev);
	if (ret) {
		cal_err(cal, "Failed to register V4L2 device\n");
		return ret;
	}

	vb2_dma_contig_set_max_seg_size(cal->dev, DMA_BIT_MASK(32));

	return 0;
}

/*
 * Cleanup the in-kernel objects, freeing memory. To be called at the very end
 * of the remove sequence, when nothing (including userspace) can access the
 * objects anymore.
 */
static void cal_media_cleanup(struct cal_dev *cal)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cal->ctx); i++) {
		if (cal->ctx[i])
			cal_ctx_v4l2_cleanup(cal->ctx[i]);
	}

	v4l2_device_unregister(&cal->v4l2_dev);
	media_device_cleanup(&cal->mdev);

	vb2_dma_contig_clear_max_seg_size(cal->dev);
}

/* ------------------------------------------------------------------
 *	Initialization and module stuff
 * ------------------------------------------------------------------
 */

static struct cal_ctx *cal_ctx_create(struct cal_dev *cal, int inst)
{
	struct cal_ctx *ctx;
	int ret;

	ctx = devm_kzalloc(cal->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->cal = cal;
	ctx->phy = cal->phy[inst];
	ctx->index = inst;
	ctx->cport = inst;

	ret = cal_ctx_v4l2_init(ctx);
	if (ret)
		return NULL;

	return ctx;
}

static const struct of_device_id cal_of_match[] = {
	{
		.compatible = "ti,dra72-cal",
		.data = (void *)&dra72x_cal_data,
	},
	{
		.compatible = "ti,dra72-pre-es2-cal",
		.data = (void *)&dra72x_es1_cal_data,
	},
	{
		.compatible = "ti,dra76-cal",
		.data = (void *)&dra76x_cal_data,
	},
	{
		.compatible = "ti,am654-cal",
		.data = (void *)&am654_cal_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, cal_of_match);

/* Get hardware revision and info. */

#define CAL_HL_HWINFO_VALUE		0xa3c90469

static void cal_get_hwinfo(struct cal_dev *cal)
{
	u32 hwinfo;

	cal->revision = cal_read(cal, CAL_HL_REVISION);
	switch (FIELD_GET(CAL_HL_REVISION_SCHEME_MASK, cal->revision)) {
	case CAL_HL_REVISION_SCHEME_H08:
		cal_dbg(3, cal, "CAL HW revision %lu.%lu.%lu (0x%08x)\n",
			FIELD_GET(CAL_HL_REVISION_MAJOR_MASK, cal->revision),
			FIELD_GET(CAL_HL_REVISION_MINOR_MASK, cal->revision),
			FIELD_GET(CAL_HL_REVISION_RTL_MASK, cal->revision),
			cal->revision);
		break;

	case CAL_HL_REVISION_SCHEME_LEGACY:
	default:
		cal_info(cal, "Unexpected CAL HW revision 0x%08x\n",
			 cal->revision);
		break;
	}

	hwinfo = cal_read(cal, CAL_HL_HWINFO);
	if (hwinfo != CAL_HL_HWINFO_VALUE)
		cal_info(cal, "CAL_HL_HWINFO = 0x%08x, expected 0x%08x\n",
			 hwinfo, CAL_HL_HWINFO_VALUE);
}

static int cal_init_camerarx_regmap(struct cal_dev *cal)
{
	struct platform_device *pdev = to_platform_device(cal->dev);
	struct device_node *np = cal->dev->of_node;
	struct regmap_config config = { };
	struct regmap *syscon;
	struct resource *res;
	unsigned int offset;
	void __iomem *base;

	syscon = syscon_regmap_lookup_by_phandle_args(np, "ti,camerrx-control",
						      1, &offset);
	if (!IS_ERR(syscon)) {
		cal->syscon_camerrx = syscon;
		cal->syscon_camerrx_offset = offset;
		return 0;
	}

	dev_warn(cal->dev, "failed to get ti,camerrx-control: %ld\n",
		 PTR_ERR(syscon));

	/*
	 * Backward DTS compatibility. If syscon entry is not present then
	 * check if the camerrx_control resource is present.
	 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "camerrx_control");
	base = devm_ioremap_resource(cal->dev, res);
	if (IS_ERR(base)) {
		cal_err(cal, "failed to ioremap camerrx_control\n");
		return PTR_ERR(base);
	}

	cal_dbg(1, cal, "ioresource %s at %pa - %pa\n",
		res->name, &res->start, &res->end);

	config.reg_bits = 32;
	config.reg_stride = 4;
	config.val_bits = 32;
	config.max_register = resource_size(res) - 4;

	syscon = regmap_init_mmio(NULL, base, &config);
	if (IS_ERR(syscon)) {
		pr_err("regmap init failed\n");
		return PTR_ERR(syscon);
	}

	/*
	 * In this case the base already point to the direct CM register so no
	 * need for an offset.
	 */
	cal->syscon_camerrx = syscon;
	cal->syscon_camerrx_offset = 0;

	return 0;
}

static int cal_probe(struct platform_device *pdev)
{
	struct cal_dev *cal;
	struct cal_ctx *ctx;
	bool connected = false;
	unsigned int i;
	int ret;
	int irq;

	cal = devm_kzalloc(&pdev->dev, sizeof(*cal), GFP_KERNEL);
	if (!cal)
		return -ENOMEM;

	cal->data = of_device_get_match_data(&pdev->dev);
	if (!cal->data) {
		dev_err(&pdev->dev, "Could not get feature data based on compatible version\n");
		return -ENODEV;
	}

	cal->dev = &pdev->dev;
	platform_set_drvdata(pdev, cal);

	/* Acquire resources: clocks, CAMERARX regmap, I/O memory and IRQ. */
	cal->fclk = devm_clk_get(&pdev->dev, "fck");
	if (IS_ERR(cal->fclk)) {
		dev_err(&pdev->dev, "cannot get CAL fclk\n");
		return PTR_ERR(cal->fclk);
	}

	ret = cal_init_camerarx_regmap(cal);
	if (ret < 0)
		return ret;

	cal->res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"cal_top");
	cal->base = devm_ioremap_resource(&pdev->dev, cal->res);
	if (IS_ERR(cal->base))
		return PTR_ERR(cal->base);

	cal_dbg(1, cal, "ioresource %s at %pa - %pa\n",
		cal->res->name, &cal->res->start, &cal->res->end);

	irq = platform_get_irq(pdev, 0);
	cal_dbg(1, cal, "got irq# %d\n", irq);
	ret = devm_request_irq(&pdev->dev, irq, cal_irq, 0, CAL_MODULE_NAME,
			       cal);
	if (ret)
		return ret;

	/* Read the revision and hardware info to verify hardware access. */
	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret)
		goto error_pm_runtime;

	cal_get_hwinfo(cal);
	pm_runtime_put_sync(&pdev->dev);

	/* Initialize the media device. */
	ret = cal_media_init(cal);
	if (ret < 0)
		goto error_pm_runtime;

	/* Create CAMERARX PHYs. */
	for (i = 0; i < cal->data->num_csi2_phy; ++i) {
		cal->phy[i] = cal_camerarx_create(cal, i);
		if (IS_ERR(cal->phy[i])) {
			ret = PTR_ERR(cal->phy[i]);
			cal->phy[i] = NULL;
			goto error_camerarx;
		}

		if (cal->phy[i]->sensor_node)
			connected = true;
	}

	if (!connected) {
		cal_err(cal, "Neither port is configured, no point in staying up\n");
		ret = -ENODEV;
		goto error_camerarx;
	}

	/* Create contexts. */
	for (i = 0; i < cal->data->num_csi2_phy; ++i) {
		if (!cal->phy[i]->sensor_node)
			continue;

		cal->ctx[i] = cal_ctx_create(cal, i);
		if (!cal->ctx[i]) {
			cal_err(cal, "Failed to create context %u\n", i);
			ret = -ENODEV;
			goto error_context;
		}
	}

	/* Register the media device. */
	ret = cal_media_register(cal);
	if (ret)
		goto error_context;

	return 0;

error_context:
	for (i = 0; i < ARRAY_SIZE(cal->ctx); i++) {
		ctx = cal->ctx[i];
		if (ctx)
			cal_ctx_v4l2_cleanup(ctx);
	}

error_camerarx:
	for (i = 0; i < cal->data->num_csi2_phy; i++)
		cal_camerarx_destroy(cal->phy[i]);

	cal_media_cleanup(cal);

error_pm_runtime:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int cal_remove(struct platform_device *pdev)
{
	struct cal_dev *cal = platform_get_drvdata(pdev);
	unsigned int i;

	cal_dbg(1, cal, "Removing %s\n", CAL_MODULE_NAME);

	pm_runtime_get_sync(&pdev->dev);

	cal_media_unregister(cal);

	for (i = 0; i < ARRAY_SIZE(cal->phy); i++) {
		if (cal->phy[i])
			cal_camerarx_disable(cal->phy[i]);
	}

	cal_media_cleanup(cal);

	for (i = 0; i < cal->data->num_csi2_phy; i++)
		cal_camerarx_destroy(cal->phy[i]);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static int cal_runtime_resume(struct device *dev)
{
	struct cal_dev *cal = dev_get_drvdata(dev);
	unsigned int i;

	if (cal->data->flags & DRA72_CAL_PRE_ES2_LDO_DISABLE) {
		/*
		 * Apply errata on both port everytime we (re-)enable
		 * the clock
		 */
		for (i = 0; i < cal->data->num_csi2_phy; i++)
			cal_camerarx_i913_errata(cal->phy[i]);
	}

	/*
	 * Enable global interrupts that are not related to a particular
	 * CAMERARAX or context.
	 */
	cal_write(cal, CAL_HL_IRQENABLE_SET(0), CAL_HL_IRQ_OCPO_ERR_MASK);

	return 0;
}

static const struct dev_pm_ops cal_pm_ops = {
	.runtime_resume = cal_runtime_resume,
};

static struct platform_driver cal_pdrv = {
	.probe		= cal_probe,
	.remove		= cal_remove,
	.driver		= {
		.name	= CAL_MODULE_NAME,
		.pm	= &cal_pm_ops,
		.of_match_table = cal_of_match,
	},
};

module_platform_driver(cal_pdrv);
