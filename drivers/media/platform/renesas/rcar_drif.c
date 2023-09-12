// SPDX-License-Identifier: GPL-2.0+
/*
 * R-Car Gen3 Digital Radio Interface (DRIF) driver
 *
 * Copyright (C) 2017 Renesas Electronics Corporation
 */

/*
 * The R-Car DRIF is a receive only MSIOF like controller with an
 * external master device driving the SCK. It receives data into a FIFO,
 * then this driver uses the SYS-DMAC engine to move the data from
 * the device to memory.
 *
 * Each DRIF channel DRIFx (as per datasheet) contains two internal
 * channels DRIFx0 & DRIFx1 within itself with each having its own resources
 * like module clk, register set, irq and dma. These internal channels share
 * common CLK & SYNC from master. The two data pins D0 & D1 shall be
 * considered to represent the two internal channels. This internal split
 * is not visible to the master device.
 *
 * Depending on the master device, a DRIF channel can use
 *  (1) both internal channels (D0 & D1) to receive data in parallel (or)
 *  (2) one internal channel (D0 or D1) to receive data
 *
 * The primary design goal of this controller is to act as a Digital Radio
 * Interface that receives digital samples from a tuner device. Hence the
 * driver exposes the device as a V4L2 SDR device. In order to qualify as
 * a V4L2 SDR device, it should possess a tuner interface as mandated by the
 * framework. This driver expects a tuner driver (sub-device) to bind
 * asynchronously with this device and the combined drivers shall expose
 * a V4L2 compliant SDR device. The DRIF driver is independent of the
 * tuner vendor.
 *
 * The DRIF h/w can support I2S mode and Frame start synchronization pulse mode.
 * This driver is tested for I2S mode only because of the availability of
 * suitable master devices. Hence, not all configurable options of DRIF h/w
 * like lsb/msb first, syncdl, dtdl etc. are exposed via DT and I2S defaults
 * are used. These can be exposed later if needed after testing.
 */
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/ioctl.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>

/* DRIF register offsets */
#define RCAR_DRIF_SITMDR1			0x00
#define RCAR_DRIF_SITMDR2			0x04
#define RCAR_DRIF_SITMDR3			0x08
#define RCAR_DRIF_SIRMDR1			0x10
#define RCAR_DRIF_SIRMDR2			0x14
#define RCAR_DRIF_SIRMDR3			0x18
#define RCAR_DRIF_SICTR				0x28
#define RCAR_DRIF_SIFCTR			0x30
#define RCAR_DRIF_SISTR				0x40
#define RCAR_DRIF_SIIER				0x44
#define RCAR_DRIF_SIRFDR			0x60

#define RCAR_DRIF_RFOVF			BIT(3)	/* Receive FIFO overflow */
#define RCAR_DRIF_RFUDF			BIT(4)	/* Receive FIFO underflow */
#define RCAR_DRIF_RFSERR		BIT(5)	/* Receive frame sync error */
#define RCAR_DRIF_REOF			BIT(7)	/* Frame reception end */
#define RCAR_DRIF_RDREQ			BIT(12) /* Receive data xfer req */
#define RCAR_DRIF_RFFUL			BIT(13)	/* Receive FIFO full */

/* SIRMDR1 */
#define RCAR_DRIF_SIRMDR1_SYNCMD_FRAME		(0 << 28)
#define RCAR_DRIF_SIRMDR1_SYNCMD_LR		(3 << 28)

#define RCAR_DRIF_SIRMDR1_SYNCAC_POL_HIGH	(0 << 25)
#define RCAR_DRIF_SIRMDR1_SYNCAC_POL_LOW	(1 << 25)

#define RCAR_DRIF_SIRMDR1_MSB_FIRST		(0 << 24)
#define RCAR_DRIF_SIRMDR1_LSB_FIRST		(1 << 24)

#define RCAR_DRIF_SIRMDR1_DTDL_0		(0 << 20)
#define RCAR_DRIF_SIRMDR1_DTDL_1		(1 << 20)
#define RCAR_DRIF_SIRMDR1_DTDL_2		(2 << 20)
#define RCAR_DRIF_SIRMDR1_DTDL_0PT5		(5 << 20)
#define RCAR_DRIF_SIRMDR1_DTDL_1PT5		(6 << 20)

#define RCAR_DRIF_SIRMDR1_SYNCDL_0		(0 << 20)
#define RCAR_DRIF_SIRMDR1_SYNCDL_1		(1 << 20)
#define RCAR_DRIF_SIRMDR1_SYNCDL_2		(2 << 20)
#define RCAR_DRIF_SIRMDR1_SYNCDL_3		(3 << 20)
#define RCAR_DRIF_SIRMDR1_SYNCDL_0PT5		(5 << 20)
#define RCAR_DRIF_SIRMDR1_SYNCDL_1PT5		(6 << 20)

#define RCAR_DRIF_MDR_GRPCNT(n)			(((n) - 1) << 30)
#define RCAR_DRIF_MDR_BITLEN(n)			(((n) - 1) << 24)
#define RCAR_DRIF_MDR_WDCNT(n)			(((n) - 1) << 16)

/* Hidden Transmit register that controls CLK & SYNC */
#define RCAR_DRIF_SITMDR1_PCON			BIT(30)

#define RCAR_DRIF_SICTR_RX_RISING_EDGE		BIT(26)
#define RCAR_DRIF_SICTR_RX_EN			BIT(8)
#define RCAR_DRIF_SICTR_RESET			BIT(0)

/* Constants */
#define RCAR_DRIF_NUM_HWBUFS			32
#define RCAR_DRIF_MAX_DEVS			4
#define RCAR_DRIF_DEFAULT_NUM_HWBUFS		16
#define RCAR_DRIF_DEFAULT_HWBUF_SIZE		(4 * PAGE_SIZE)
#define RCAR_DRIF_MAX_CHANNEL			2
#define RCAR_SDR_BUFFER_SIZE			SZ_64K

/* Internal buffer status flags */
#define RCAR_DRIF_BUF_DONE			BIT(0)	/* DMA completed */
#define RCAR_DRIF_BUF_OVERFLOW			BIT(1)	/* Overflow detected */

#define to_rcar_drif_buf_pair(sdr, ch_num, idx)			\
	(&((sdr)->ch[!(ch_num)]->buf[(idx)]))

#define for_each_rcar_drif_channel(ch, ch_mask)			\
	for_each_set_bit(ch, ch_mask, RCAR_DRIF_MAX_CHANNEL)

/* Debug */
#define rdrif_dbg(sdr, fmt, arg...)				\
	dev_dbg(sdr->v4l2_dev.dev, fmt, ## arg)

#define rdrif_err(sdr, fmt, arg...)				\
	dev_err(sdr->v4l2_dev.dev, fmt, ## arg)

/* Stream formats */
struct rcar_drif_format {
	u32	pixelformat;
	u32	buffersize;
	u32	bitlen;
	u32	wdcnt;
	u32	num_ch;
};

/* Format descriptions for capture */
static const struct rcar_drif_format formats[] = {
	{
		.pixelformat	= V4L2_SDR_FMT_PCU16BE,
		.buffersize	= RCAR_SDR_BUFFER_SIZE,
		.bitlen		= 16,
		.wdcnt		= 1,
		.num_ch		= 2,
	},
	{
		.pixelformat	= V4L2_SDR_FMT_PCU18BE,
		.buffersize	= RCAR_SDR_BUFFER_SIZE,
		.bitlen		= 18,
		.wdcnt		= 1,
		.num_ch		= 2,
	},
	{
		.pixelformat	= V4L2_SDR_FMT_PCU20BE,
		.buffersize	= RCAR_SDR_BUFFER_SIZE,
		.bitlen		= 20,
		.wdcnt		= 1,
		.num_ch		= 2,
	},
};

/* Buffer for a received frame from one or both internal channels */
struct rcar_drif_frame_buf {
	/* Common v4l buffer stuff -- must be first */
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

/* OF graph endpoint's V4L2 async data */
struct rcar_drif_graph_ep {
	struct v4l2_subdev *subdev;	/* Async matched subdev */
};

/* DMA buffer */
struct rcar_drif_hwbuf {
	void *addr;			/* CPU-side address */
	unsigned int status;		/* Buffer status flags */
};

/* Internal channel */
struct rcar_drif {
	struct rcar_drif_sdr *sdr;	/* Group device */
	struct platform_device *pdev;	/* Channel's pdev */
	void __iomem *base;		/* Base register address */
	resource_size_t start;		/* I/O resource offset */
	struct dma_chan *dmach;		/* Reserved DMA channel */
	struct clk *clk;		/* Module clock */
	struct rcar_drif_hwbuf buf[RCAR_DRIF_NUM_HWBUFS]; /* H/W bufs */
	dma_addr_t dma_handle;		/* Handle for all bufs */
	unsigned int num;		/* Channel number */
	bool acting_sdr;		/* Channel acting as SDR device */
};

/* DRIF V4L2 SDR */
struct rcar_drif_sdr {
	struct device *dev;		/* Platform device */
	struct video_device *vdev;	/* V4L2 SDR device */
	struct v4l2_device v4l2_dev;	/* V4L2 device */

	/* Videobuf2 queue and queued buffers list */
	struct vb2_queue vb_queue;
	struct list_head queued_bufs;
	spinlock_t queued_bufs_lock;	/* Protects queued_bufs */
	spinlock_t dma_lock;		/* To serialize DMA cb of channels */

	struct mutex v4l2_mutex;	/* To serialize ioctls */
	struct mutex vb_queue_mutex;	/* To serialize streaming ioctls */
	struct v4l2_ctrl_handler ctrl_hdl;	/* SDR control handler */
	struct v4l2_async_notifier notifier;	/* For subdev (tuner) */
	struct rcar_drif_graph_ep ep;	/* Endpoint V4L2 async data */

	/* Current V4L2 SDR format ptr */
	const struct rcar_drif_format *fmt;

	/* Device tree SYNC properties */
	u32 mdr1;

	/* Internals */
	struct rcar_drif *ch[RCAR_DRIF_MAX_CHANNEL]; /* DRIFx0,1 */
	unsigned long hw_ch_mask;	/* Enabled channels per DT */
	unsigned long cur_ch_mask;	/* Used channels for an SDR FMT */
	u32 num_hw_ch;			/* Num of DT enabled channels */
	u32 num_cur_ch;			/* Num of used channels */
	u32 hwbuf_size;			/* Each DMA buffer size */
	u32 produced;			/* Buffers produced by sdr dev */
};

/* Register access functions */
static void rcar_drif_write(struct rcar_drif *ch, u32 offset, u32 data)
{
	writel(data, ch->base + offset);
}

static u32 rcar_drif_read(struct rcar_drif *ch, u32 offset)
{
	return readl(ch->base + offset);
}

/* Release DMA channels */
static void rcar_drif_release_dmachannels(struct rcar_drif_sdr *sdr)
{
	unsigned int i;

	for_each_rcar_drif_channel(i, &sdr->cur_ch_mask)
		if (sdr->ch[i]->dmach) {
			dma_release_channel(sdr->ch[i]->dmach);
			sdr->ch[i]->dmach = NULL;
		}
}

/* Allocate DMA channels */
static int rcar_drif_alloc_dmachannels(struct rcar_drif_sdr *sdr)
{
	struct dma_slave_config dma_cfg;
	unsigned int i;
	int ret;

	for_each_rcar_drif_channel(i, &sdr->cur_ch_mask) {
		struct rcar_drif *ch = sdr->ch[i];

		ch->dmach = dma_request_chan(&ch->pdev->dev, "rx");
		if (IS_ERR(ch->dmach)) {
			ret = PTR_ERR(ch->dmach);
			if (ret != -EPROBE_DEFER)
				rdrif_err(sdr,
					  "ch%u: dma channel req failed: %pe\n",
					  i, ch->dmach);
			ch->dmach = NULL;
			goto dmach_error;
		}

		/* Configure slave */
		memset(&dma_cfg, 0, sizeof(dma_cfg));
		dma_cfg.src_addr = (phys_addr_t)(ch->start + RCAR_DRIF_SIRFDR);
		dma_cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		ret = dmaengine_slave_config(ch->dmach, &dma_cfg);
		if (ret) {
			rdrif_err(sdr, "ch%u: dma slave config failed\n", i);
			goto dmach_error;
		}
	}
	return 0;

dmach_error:
	rcar_drif_release_dmachannels(sdr);
	return ret;
}

/* Release queued vb2 buffers */
static void rcar_drif_release_queued_bufs(struct rcar_drif_sdr *sdr,
					  enum vb2_buffer_state state)
{
	struct rcar_drif_frame_buf *fbuf, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&sdr->queued_bufs_lock, flags);
	list_for_each_entry_safe(fbuf, tmp, &sdr->queued_bufs, list) {
		list_del(&fbuf->list);
		vb2_buffer_done(&fbuf->vb.vb2_buf, state);
	}
	spin_unlock_irqrestore(&sdr->queued_bufs_lock, flags);
}

/* Set MDR defaults */
static inline void rcar_drif_set_mdr1(struct rcar_drif_sdr *sdr)
{
	unsigned int i;

	/* Set defaults for enabled internal channels */
	for_each_rcar_drif_channel(i, &sdr->cur_ch_mask) {
		/* Refer MSIOF section in manual for this register setting */
		rcar_drif_write(sdr->ch[i], RCAR_DRIF_SITMDR1,
				RCAR_DRIF_SITMDR1_PCON);

		/* Setup MDR1 value */
		rcar_drif_write(sdr->ch[i], RCAR_DRIF_SIRMDR1, sdr->mdr1);

		rdrif_dbg(sdr, "ch%u: mdr1 = 0x%08x",
			  i, rcar_drif_read(sdr->ch[i], RCAR_DRIF_SIRMDR1));
	}
}

/* Set DRIF receive format */
static int rcar_drif_set_format(struct rcar_drif_sdr *sdr)
{
	unsigned int i;

	rdrif_dbg(sdr, "setfmt: bitlen %u wdcnt %u num_ch %u\n",
		  sdr->fmt->bitlen, sdr->fmt->wdcnt, sdr->fmt->num_ch);

	/* Sanity check */
	if (sdr->fmt->num_ch > sdr->num_cur_ch) {
		rdrif_err(sdr, "fmt num_ch %u cur_ch %u mismatch\n",
			  sdr->fmt->num_ch, sdr->num_cur_ch);
		return -EINVAL;
	}

	/* Setup group, bitlen & wdcnt */
	for_each_rcar_drif_channel(i, &sdr->cur_ch_mask) {
		u32 mdr;

		/* Two groups */
		mdr = RCAR_DRIF_MDR_GRPCNT(2) |
			RCAR_DRIF_MDR_BITLEN(sdr->fmt->bitlen) |
			RCAR_DRIF_MDR_WDCNT(sdr->fmt->wdcnt);
		rcar_drif_write(sdr->ch[i], RCAR_DRIF_SIRMDR2, mdr);

		mdr = RCAR_DRIF_MDR_BITLEN(sdr->fmt->bitlen) |
			RCAR_DRIF_MDR_WDCNT(sdr->fmt->wdcnt);
		rcar_drif_write(sdr->ch[i], RCAR_DRIF_SIRMDR3, mdr);

		rdrif_dbg(sdr, "ch%u: new mdr[2,3] = 0x%08x, 0x%08x\n",
			  i, rcar_drif_read(sdr->ch[i], RCAR_DRIF_SIRMDR2),
			  rcar_drif_read(sdr->ch[i], RCAR_DRIF_SIRMDR3));
	}
	return 0;
}

/* Release DMA buffers */
static void rcar_drif_release_buf(struct rcar_drif_sdr *sdr)
{
	unsigned int i;

	for_each_rcar_drif_channel(i, &sdr->cur_ch_mask) {
		struct rcar_drif *ch = sdr->ch[i];

		/* First entry contains the dma buf ptr */
		if (ch->buf[0].addr) {
			dma_free_coherent(&ch->pdev->dev,
				sdr->hwbuf_size * RCAR_DRIF_NUM_HWBUFS,
				ch->buf[0].addr, ch->dma_handle);
			ch->buf[0].addr = NULL;
		}
	}
}

/* Request DMA buffers */
static int rcar_drif_request_buf(struct rcar_drif_sdr *sdr)
{
	int ret = -ENOMEM;
	unsigned int i, j;
	void *addr;

	for_each_rcar_drif_channel(i, &sdr->cur_ch_mask) {
		struct rcar_drif *ch = sdr->ch[i];

		/* Allocate DMA buffers */
		addr = dma_alloc_coherent(&ch->pdev->dev,
				sdr->hwbuf_size * RCAR_DRIF_NUM_HWBUFS,
				&ch->dma_handle, GFP_KERNEL);
		if (!addr) {
			rdrif_err(sdr,
			"ch%u: dma alloc failed. num hwbufs %u size %u\n",
			i, RCAR_DRIF_NUM_HWBUFS, sdr->hwbuf_size);
			goto error;
		}

		/* Split the chunk and populate bufctxt */
		for (j = 0; j < RCAR_DRIF_NUM_HWBUFS; j++) {
			ch->buf[j].addr = addr + (j * sdr->hwbuf_size);
			ch->buf[j].status = 0;
		}
	}
	return 0;
error:
	return ret;
}

/* Setup vb_queue minimum buffer requirements */
static int rcar_drif_queue_setup(struct vb2_queue *vq,
			unsigned int *num_buffers, unsigned int *num_planes,
			unsigned int sizes[], struct device *alloc_devs[])
{
	struct rcar_drif_sdr *sdr = vb2_get_drv_priv(vq);

	/* Need at least 16 buffers */
	if (vq->num_buffers + *num_buffers < 16)
		*num_buffers = 16 - vq->num_buffers;

	*num_planes = 1;
	sizes[0] = PAGE_ALIGN(sdr->fmt->buffersize);
	rdrif_dbg(sdr, "num_bufs %d sizes[0] %d\n", *num_buffers, sizes[0]);

	return 0;
}

/* Enqueue buffer */
static void rcar_drif_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rcar_drif_sdr *sdr = vb2_get_drv_priv(vb->vb2_queue);
	struct rcar_drif_frame_buf *fbuf =
			container_of(vbuf, struct rcar_drif_frame_buf, vb);
	unsigned long flags;

	rdrif_dbg(sdr, "buf_queue idx %u\n", vb->index);
	spin_lock_irqsave(&sdr->queued_bufs_lock, flags);
	list_add_tail(&fbuf->list, &sdr->queued_bufs);
	spin_unlock_irqrestore(&sdr->queued_bufs_lock, flags);
}

/* Get a frame buf from list */
static struct rcar_drif_frame_buf *
rcar_drif_get_fbuf(struct rcar_drif_sdr *sdr)
{
	struct rcar_drif_frame_buf *fbuf;
	unsigned long flags;

	spin_lock_irqsave(&sdr->queued_bufs_lock, flags);
	fbuf = list_first_entry_or_null(&sdr->queued_bufs, struct
					rcar_drif_frame_buf, list);
	if (!fbuf) {
		/*
		 * App is late in enqueing buffers. Samples lost & there will
		 * be a gap in sequence number when app recovers
		 */
		rdrif_dbg(sdr, "\napp late: prod %u\n", sdr->produced);
		spin_unlock_irqrestore(&sdr->queued_bufs_lock, flags);
		return NULL;
	}
	list_del(&fbuf->list);
	spin_unlock_irqrestore(&sdr->queued_bufs_lock, flags);

	return fbuf;
}

/* Helpers to set/clear buf pair status */
static inline bool rcar_drif_bufs_done(struct rcar_drif_hwbuf **buf)
{
	return (buf[0]->status & buf[1]->status & RCAR_DRIF_BUF_DONE);
}

static inline bool rcar_drif_bufs_overflow(struct rcar_drif_hwbuf **buf)
{
	return ((buf[0]->status | buf[1]->status) & RCAR_DRIF_BUF_OVERFLOW);
}

static inline void rcar_drif_bufs_clear(struct rcar_drif_hwbuf **buf,
					unsigned int bit)
{
	unsigned int i;

	for (i = 0; i < RCAR_DRIF_MAX_CHANNEL; i++)
		buf[i]->status &= ~bit;
}

/* Channel DMA complete */
static void rcar_drif_channel_complete(struct rcar_drif *ch, u32 idx)
{
	u32 str;

	ch->buf[idx].status |= RCAR_DRIF_BUF_DONE;

	/* Check for DRIF errors */
	str = rcar_drif_read(ch, RCAR_DRIF_SISTR);
	if (unlikely(str & RCAR_DRIF_RFOVF)) {
		/* Writing the same clears it */
		rcar_drif_write(ch, RCAR_DRIF_SISTR, str);

		/* Overflow: some samples are lost */
		ch->buf[idx].status |= RCAR_DRIF_BUF_OVERFLOW;
	}
}

/* DMA callback for each stage */
static void rcar_drif_dma_complete(void *dma_async_param)
{
	struct rcar_drif *ch = dma_async_param;
	struct rcar_drif_sdr *sdr = ch->sdr;
	struct rcar_drif_hwbuf *buf[RCAR_DRIF_MAX_CHANNEL];
	struct rcar_drif_frame_buf *fbuf;
	bool overflow = false;
	u32 idx, produced;
	unsigned int i;

	spin_lock(&sdr->dma_lock);

	/* DMA can be terminated while the callback was waiting on lock */
	if (!vb2_is_streaming(&sdr->vb_queue)) {
		spin_unlock(&sdr->dma_lock);
		return;
	}

	idx = sdr->produced % RCAR_DRIF_NUM_HWBUFS;
	rcar_drif_channel_complete(ch, idx);

	if (sdr->num_cur_ch == RCAR_DRIF_MAX_CHANNEL) {
		buf[0] = ch->num ? to_rcar_drif_buf_pair(sdr, ch->num, idx) :
				&ch->buf[idx];
		buf[1] = ch->num ? &ch->buf[idx] :
				to_rcar_drif_buf_pair(sdr, ch->num, idx);

		/* Check if both DMA buffers are done */
		if (!rcar_drif_bufs_done(buf)) {
			spin_unlock(&sdr->dma_lock);
			return;
		}

		/* Clear buf done status */
		rcar_drif_bufs_clear(buf, RCAR_DRIF_BUF_DONE);

		if (rcar_drif_bufs_overflow(buf)) {
			overflow = true;
			/* Clear the flag in status */
			rcar_drif_bufs_clear(buf, RCAR_DRIF_BUF_OVERFLOW);
		}
	} else {
		buf[0] = &ch->buf[idx];
		if (buf[0]->status & RCAR_DRIF_BUF_OVERFLOW) {
			overflow = true;
			/* Clear the flag in status */
			buf[0]->status &= ~RCAR_DRIF_BUF_OVERFLOW;
		}
	}

	/* Buffer produced for consumption */
	produced = sdr->produced++;
	spin_unlock(&sdr->dma_lock);

	rdrif_dbg(sdr, "ch%u: prod %u\n", ch->num, produced);

	/* Get fbuf */
	fbuf = rcar_drif_get_fbuf(sdr);
	if (!fbuf)
		return;

	for (i = 0; i < RCAR_DRIF_MAX_CHANNEL; i++)
		memcpy(vb2_plane_vaddr(&fbuf->vb.vb2_buf, 0) +
		       i * sdr->hwbuf_size, buf[i]->addr, sdr->hwbuf_size);

	fbuf->vb.field = V4L2_FIELD_NONE;
	fbuf->vb.sequence = produced;
	fbuf->vb.vb2_buf.timestamp = ktime_get_ns();
	vb2_set_plane_payload(&fbuf->vb.vb2_buf, 0, sdr->fmt->buffersize);

	/* Set error state on overflow */
	vb2_buffer_done(&fbuf->vb.vb2_buf,
			overflow ? VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
}

static int rcar_drif_qbuf(struct rcar_drif *ch)
{
	struct rcar_drif_sdr *sdr = ch->sdr;
	dma_addr_t addr = ch->dma_handle;
	struct dma_async_tx_descriptor *rxd;
	dma_cookie_t cookie;
	int ret = -EIO;

	/* Setup cyclic DMA with given buffers */
	rxd = dmaengine_prep_dma_cyclic(ch->dmach, addr,
					sdr->hwbuf_size * RCAR_DRIF_NUM_HWBUFS,
					sdr->hwbuf_size, DMA_DEV_TO_MEM,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!rxd) {
		rdrif_err(sdr, "ch%u: prep dma cyclic failed\n", ch->num);
		return ret;
	}

	/* Submit descriptor */
	rxd->callback = rcar_drif_dma_complete;
	rxd->callback_param = ch;
	cookie = dmaengine_submit(rxd);
	if (dma_submit_error(cookie)) {
		rdrif_err(sdr, "ch%u: dma submit failed\n", ch->num);
		return ret;
	}

	dma_async_issue_pending(ch->dmach);
	return 0;
}

/* Enable reception */
static int rcar_drif_enable_rx(struct rcar_drif_sdr *sdr)
{
	unsigned int i;
	u32 ctr;
	int ret = -EINVAL;

	/*
	 * When both internal channels are enabled, they can be synchronized
	 * only by the master
	 */

	/* Enable receive */
	for_each_rcar_drif_channel(i, &sdr->cur_ch_mask) {
		ctr = rcar_drif_read(sdr->ch[i], RCAR_DRIF_SICTR);
		ctr |= (RCAR_DRIF_SICTR_RX_RISING_EDGE |
			 RCAR_DRIF_SICTR_RX_EN);
		rcar_drif_write(sdr->ch[i], RCAR_DRIF_SICTR, ctr);
	}

	/* Check receive enabled */
	for_each_rcar_drif_channel(i, &sdr->cur_ch_mask) {
		ret = readl_poll_timeout(sdr->ch[i]->base + RCAR_DRIF_SICTR,
				ctr, ctr & RCAR_DRIF_SICTR_RX_EN, 7, 100000);
		if (ret) {
			rdrif_err(sdr, "ch%u: rx en failed. ctr 0x%08x\n", i,
				  rcar_drif_read(sdr->ch[i], RCAR_DRIF_SICTR));
			break;
		}
	}
	return ret;
}

/* Disable reception */
static void rcar_drif_disable_rx(struct rcar_drif_sdr *sdr)
{
	unsigned int i;
	u32 ctr;
	int ret;

	/* Disable receive */
	for_each_rcar_drif_channel(i, &sdr->cur_ch_mask) {
		ctr = rcar_drif_read(sdr->ch[i], RCAR_DRIF_SICTR);
		ctr &= ~RCAR_DRIF_SICTR_RX_EN;
		rcar_drif_write(sdr->ch[i], RCAR_DRIF_SICTR, ctr);
	}

	/* Check receive disabled */
	for_each_rcar_drif_channel(i, &sdr->cur_ch_mask) {
		ret = readl_poll_timeout(sdr->ch[i]->base + RCAR_DRIF_SICTR,
				ctr, !(ctr & RCAR_DRIF_SICTR_RX_EN), 7, 100000);
		if (ret)
			dev_warn(&sdr->vdev->dev,
			"ch%u: failed to disable rx. ctr 0x%08x\n",
			i, rcar_drif_read(sdr->ch[i], RCAR_DRIF_SICTR));
	}
}

/* Stop channel */
static void rcar_drif_stop_channel(struct rcar_drif *ch)
{
	/* Disable DMA receive interrupt */
	rcar_drif_write(ch, RCAR_DRIF_SIIER, 0x00000000);

	/* Terminate all DMA transfers */
	dmaengine_terminate_sync(ch->dmach);
}

/* Stop receive operation */
static void rcar_drif_stop(struct rcar_drif_sdr *sdr)
{
	unsigned int i;

	/* Disable Rx */
	rcar_drif_disable_rx(sdr);

	for_each_rcar_drif_channel(i, &sdr->cur_ch_mask)
		rcar_drif_stop_channel(sdr->ch[i]);
}

/* Start channel */
static int rcar_drif_start_channel(struct rcar_drif *ch)
{
	struct rcar_drif_sdr *sdr = ch->sdr;
	u32 ctr, str;
	int ret;

	/* Reset receive */
	rcar_drif_write(ch, RCAR_DRIF_SICTR, RCAR_DRIF_SICTR_RESET);
	ret = readl_poll_timeout(ch->base + RCAR_DRIF_SICTR, ctr,
				 !(ctr & RCAR_DRIF_SICTR_RESET), 7, 100000);
	if (ret) {
		rdrif_err(sdr, "ch%u: failed to reset rx. ctr 0x%08x\n",
			  ch->num, rcar_drif_read(ch, RCAR_DRIF_SICTR));
		return ret;
	}

	/* Queue buffers for DMA */
	ret = rcar_drif_qbuf(ch);
	if (ret)
		return ret;

	/* Clear status register flags */
	str = RCAR_DRIF_RFFUL | RCAR_DRIF_REOF | RCAR_DRIF_RFSERR |
		RCAR_DRIF_RFUDF | RCAR_DRIF_RFOVF;
	rcar_drif_write(ch, RCAR_DRIF_SISTR, str);

	/* Enable DMA receive interrupt */
	rcar_drif_write(ch, RCAR_DRIF_SIIER, 0x00009000);

	return ret;
}

/* Start receive operation */
static int rcar_drif_start(struct rcar_drif_sdr *sdr)
{
	unsigned long enabled = 0;
	unsigned int i;
	int ret;

	for_each_rcar_drif_channel(i, &sdr->cur_ch_mask) {
		ret = rcar_drif_start_channel(sdr->ch[i]);
		if (ret)
			goto start_error;
		enabled |= BIT(i);
	}

	ret = rcar_drif_enable_rx(sdr);
	if (ret)
		goto enable_error;

	sdr->produced = 0;
	return ret;

enable_error:
	rcar_drif_disable_rx(sdr);
start_error:
	for_each_rcar_drif_channel(i, &enabled)
		rcar_drif_stop_channel(sdr->ch[i]);

	return ret;
}

/* Start streaming */
static int rcar_drif_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct rcar_drif_sdr *sdr = vb2_get_drv_priv(vq);
	unsigned long enabled = 0;
	unsigned int i;
	int ret;

	mutex_lock(&sdr->v4l2_mutex);

	for_each_rcar_drif_channel(i, &sdr->cur_ch_mask) {
		ret = clk_prepare_enable(sdr->ch[i]->clk);
		if (ret)
			goto error;
		enabled |= BIT(i);
	}

	/* Set default MDRx settings */
	rcar_drif_set_mdr1(sdr);

	/* Set new format */
	ret = rcar_drif_set_format(sdr);
	if (ret)
		goto error;

	if (sdr->num_cur_ch == RCAR_DRIF_MAX_CHANNEL)
		sdr->hwbuf_size = sdr->fmt->buffersize / RCAR_DRIF_MAX_CHANNEL;
	else
		sdr->hwbuf_size = sdr->fmt->buffersize;

	rdrif_dbg(sdr, "num hwbufs %u, hwbuf_size %u\n",
		RCAR_DRIF_NUM_HWBUFS, sdr->hwbuf_size);

	/* Alloc DMA channel */
	ret = rcar_drif_alloc_dmachannels(sdr);
	if (ret)
		goto error;

	/* Request buffers */
	ret = rcar_drif_request_buf(sdr);
	if (ret)
		goto error;

	/* Start Rx */
	ret = rcar_drif_start(sdr);
	if (ret)
		goto error;

	mutex_unlock(&sdr->v4l2_mutex);

	return ret;

error:
	rcar_drif_release_queued_bufs(sdr, VB2_BUF_STATE_QUEUED);
	rcar_drif_release_buf(sdr);
	rcar_drif_release_dmachannels(sdr);
	for_each_rcar_drif_channel(i, &enabled)
		clk_disable_unprepare(sdr->ch[i]->clk);

	mutex_unlock(&sdr->v4l2_mutex);

	return ret;
}

/* Stop streaming */
static void rcar_drif_stop_streaming(struct vb2_queue *vq)
{
	struct rcar_drif_sdr *sdr = vb2_get_drv_priv(vq);
	unsigned int i;

	mutex_lock(&sdr->v4l2_mutex);

	/* Stop hardware streaming */
	rcar_drif_stop(sdr);

	/* Return all queued buffers to vb2 */
	rcar_drif_release_queued_bufs(sdr, VB2_BUF_STATE_ERROR);

	/* Release buf */
	rcar_drif_release_buf(sdr);

	/* Release DMA channel resources */
	rcar_drif_release_dmachannels(sdr);

	for_each_rcar_drif_channel(i, &sdr->cur_ch_mask)
		clk_disable_unprepare(sdr->ch[i]->clk);

	mutex_unlock(&sdr->v4l2_mutex);
}

/* Vb2 ops */
static const struct vb2_ops rcar_drif_vb2_ops = {
	.queue_setup            = rcar_drif_queue_setup,
	.buf_queue              = rcar_drif_buf_queue,
	.start_streaming        = rcar_drif_start_streaming,
	.stop_streaming         = rcar_drif_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

static int rcar_drif_querycap(struct file *file, void *fh,
			      struct v4l2_capability *cap)
{
	struct rcar_drif_sdr *sdr = video_drvdata(file);

	strscpy(cap->driver, KBUILD_MODNAME, sizeof(cap->driver));
	strscpy(cap->card, sdr->vdev->name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 sdr->vdev->name);

	return 0;
}

static int rcar_drif_set_default_format(struct rcar_drif_sdr *sdr)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		/* Matching fmt based on required channels is set as default */
		if (sdr->num_hw_ch == formats[i].num_ch) {
			sdr->fmt = &formats[i];
			sdr->cur_ch_mask = sdr->hw_ch_mask;
			sdr->num_cur_ch = sdr->num_hw_ch;
			dev_dbg(sdr->dev, "default fmt[%u]: mask %lu num %u\n",
				i, sdr->cur_ch_mask, sdr->num_cur_ch);
			return 0;
		}
	}
	return -EINVAL;
}

static int rcar_drif_enum_fmt_sdr_cap(struct file *file, void *priv,
				      struct v4l2_fmtdesc *f)
{
	if (f->index >= ARRAY_SIZE(formats))
		return -EINVAL;

	f->pixelformat = formats[f->index].pixelformat;

	return 0;
}

static int rcar_drif_g_fmt_sdr_cap(struct file *file, void *priv,
				   struct v4l2_format *f)
{
	struct rcar_drif_sdr *sdr = video_drvdata(file);

	f->fmt.sdr.pixelformat = sdr->fmt->pixelformat;
	f->fmt.sdr.buffersize = sdr->fmt->buffersize;

	return 0;
}

static int rcar_drif_s_fmt_sdr_cap(struct file *file, void *priv,
				   struct v4l2_format *f)
{
	struct rcar_drif_sdr *sdr = video_drvdata(file);
	struct vb2_queue *q = &sdr->vb_queue;
	unsigned int i;

	if (vb2_is_busy(q))
		return -EBUSY;

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		if (formats[i].pixelformat == f->fmt.sdr.pixelformat)
			break;
	}

	if (i == ARRAY_SIZE(formats))
		i = 0;		/* Set the 1st format as default on no match */

	sdr->fmt = &formats[i];
	f->fmt.sdr.pixelformat = sdr->fmt->pixelformat;
	f->fmt.sdr.buffersize = formats[i].buffersize;
	memset(f->fmt.sdr.reserved, 0, sizeof(f->fmt.sdr.reserved));

	/*
	 * If a format demands one channel only out of two
	 * enabled channels, pick the 0th channel.
	 */
	if (formats[i].num_ch < sdr->num_hw_ch) {
		sdr->cur_ch_mask = BIT(0);
		sdr->num_cur_ch = formats[i].num_ch;
	} else {
		sdr->cur_ch_mask = sdr->hw_ch_mask;
		sdr->num_cur_ch = sdr->num_hw_ch;
	}

	rdrif_dbg(sdr, "cur: idx %u mask %lu num %u\n",
		  i, sdr->cur_ch_mask, sdr->num_cur_ch);

	return 0;
}

static int rcar_drif_try_fmt_sdr_cap(struct file *file, void *priv,
				     struct v4l2_format *f)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		if (formats[i].pixelformat == f->fmt.sdr.pixelformat) {
			f->fmt.sdr.buffersize = formats[i].buffersize;
			return 0;
		}
	}

	f->fmt.sdr.pixelformat = formats[0].pixelformat;
	f->fmt.sdr.buffersize = formats[0].buffersize;
	memset(f->fmt.sdr.reserved, 0, sizeof(f->fmt.sdr.reserved));

	return 0;
}

/* Tuner subdev ioctls */
static int rcar_drif_enum_freq_bands(struct file *file, void *priv,
				     struct v4l2_frequency_band *band)
{
	struct rcar_drif_sdr *sdr = video_drvdata(file);

	return v4l2_subdev_call(sdr->ep.subdev, tuner, enum_freq_bands, band);
}

static int rcar_drif_g_frequency(struct file *file, void *priv,
				 struct v4l2_frequency *f)
{
	struct rcar_drif_sdr *sdr = video_drvdata(file);

	return v4l2_subdev_call(sdr->ep.subdev, tuner, g_frequency, f);
}

static int rcar_drif_s_frequency(struct file *file, void *priv,
				 const struct v4l2_frequency *f)
{
	struct rcar_drif_sdr *sdr = video_drvdata(file);

	return v4l2_subdev_call(sdr->ep.subdev, tuner, s_frequency, f);
}

static int rcar_drif_g_tuner(struct file *file, void *priv,
			     struct v4l2_tuner *vt)
{
	struct rcar_drif_sdr *sdr = video_drvdata(file);

	return v4l2_subdev_call(sdr->ep.subdev, tuner, g_tuner, vt);
}

static int rcar_drif_s_tuner(struct file *file, void *priv,
			     const struct v4l2_tuner *vt)
{
	struct rcar_drif_sdr *sdr = video_drvdata(file);

	return v4l2_subdev_call(sdr->ep.subdev, tuner, s_tuner, vt);
}

static const struct v4l2_ioctl_ops rcar_drif_ioctl_ops = {
	.vidioc_querycap          = rcar_drif_querycap,

	.vidioc_enum_fmt_sdr_cap  = rcar_drif_enum_fmt_sdr_cap,
	.vidioc_g_fmt_sdr_cap     = rcar_drif_g_fmt_sdr_cap,
	.vidioc_s_fmt_sdr_cap     = rcar_drif_s_fmt_sdr_cap,
	.vidioc_try_fmt_sdr_cap   = rcar_drif_try_fmt_sdr_cap,

	.vidioc_reqbufs           = vb2_ioctl_reqbufs,
	.vidioc_create_bufs       = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf       = vb2_ioctl_prepare_buf,
	.vidioc_querybuf          = vb2_ioctl_querybuf,
	.vidioc_qbuf              = vb2_ioctl_qbuf,
	.vidioc_dqbuf             = vb2_ioctl_dqbuf,

	.vidioc_streamon          = vb2_ioctl_streamon,
	.vidioc_streamoff         = vb2_ioctl_streamoff,

	.vidioc_s_frequency       = rcar_drif_s_frequency,
	.vidioc_g_frequency       = rcar_drif_g_frequency,
	.vidioc_s_tuner		  = rcar_drif_s_tuner,
	.vidioc_g_tuner		  = rcar_drif_g_tuner,
	.vidioc_enum_freq_bands   = rcar_drif_enum_freq_bands,
	.vidioc_subscribe_event   = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
	.vidioc_log_status        = v4l2_ctrl_log_status,
};

static const struct v4l2_file_operations rcar_drif_fops = {
	.owner                    = THIS_MODULE,
	.open                     = v4l2_fh_open,
	.release                  = vb2_fop_release,
	.read                     = vb2_fop_read,
	.poll                     = vb2_fop_poll,
	.mmap                     = vb2_fop_mmap,
	.unlocked_ioctl           = video_ioctl2,
};

static int rcar_drif_sdr_register(struct rcar_drif_sdr *sdr)
{
	int ret;

	/* Init video_device structure */
	sdr->vdev = video_device_alloc();
	if (!sdr->vdev)
		return -ENOMEM;

	snprintf(sdr->vdev->name, sizeof(sdr->vdev->name), "R-Car DRIF");
	sdr->vdev->fops = &rcar_drif_fops;
	sdr->vdev->ioctl_ops = &rcar_drif_ioctl_ops;
	sdr->vdev->release = video_device_release;
	sdr->vdev->lock = &sdr->v4l2_mutex;
	sdr->vdev->queue = &sdr->vb_queue;
	sdr->vdev->queue->lock = &sdr->vb_queue_mutex;
	sdr->vdev->ctrl_handler = &sdr->ctrl_hdl;
	sdr->vdev->v4l2_dev = &sdr->v4l2_dev;
	sdr->vdev->device_caps = V4L2_CAP_SDR_CAPTURE | V4L2_CAP_TUNER |
		V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
	video_set_drvdata(sdr->vdev, sdr);

	/* Register V4L2 SDR device */
	ret = video_register_device(sdr->vdev, VFL_TYPE_SDR, -1);
	if (ret) {
		video_device_release(sdr->vdev);
		sdr->vdev = NULL;
		dev_err(sdr->dev, "failed video_register_device (%d)\n", ret);
	}

	return ret;
}

static void rcar_drif_sdr_unregister(struct rcar_drif_sdr *sdr)
{
	video_unregister_device(sdr->vdev);
	sdr->vdev = NULL;
}

/* Sub-device bound callback */
static int rcar_drif_notify_bound(struct v4l2_async_notifier *notifier,
				   struct v4l2_subdev *subdev,
				   struct v4l2_async_connection *asd)
{
	struct rcar_drif_sdr *sdr =
		container_of(notifier, struct rcar_drif_sdr, notifier);

	v4l2_set_subdev_hostdata(subdev, sdr);
	sdr->ep.subdev = subdev;
	rdrif_dbg(sdr, "bound asd %s\n", subdev->name);

	return 0;
}

/* Sub-device unbind callback */
static void rcar_drif_notify_unbind(struct v4l2_async_notifier *notifier,
				   struct v4l2_subdev *subdev,
				   struct v4l2_async_connection *asd)
{
	struct rcar_drif_sdr *sdr =
		container_of(notifier, struct rcar_drif_sdr, notifier);

	if (sdr->ep.subdev != subdev) {
		rdrif_err(sdr, "subdev %s is not bound\n", subdev->name);
		return;
	}

	/* Free ctrl handler if initialized */
	v4l2_ctrl_handler_free(&sdr->ctrl_hdl);
	sdr->v4l2_dev.ctrl_handler = NULL;
	sdr->ep.subdev = NULL;

	rcar_drif_sdr_unregister(sdr);
	rdrif_dbg(sdr, "unbind asd %s\n", subdev->name);
}

/* Sub-device registered notification callback */
static int rcar_drif_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct rcar_drif_sdr *sdr =
		container_of(notifier, struct rcar_drif_sdr, notifier);
	int ret;

	/*
	 * The subdev tested at this point uses 4 controls. Using 10 as a worst
	 * case scenario hint. When less controls are needed there will be some
	 * unused memory and when more controls are needed the framework uses
	 * hash to manage controls within this number.
	 */
	ret = v4l2_ctrl_handler_init(&sdr->ctrl_hdl, 10);
	if (ret)
		return -ENOMEM;

	sdr->v4l2_dev.ctrl_handler = &sdr->ctrl_hdl;
	ret = v4l2_device_register_subdev_nodes(&sdr->v4l2_dev);
	if (ret) {
		rdrif_err(sdr, "failed: register subdev nodes ret %d\n", ret);
		goto error;
	}

	ret = v4l2_ctrl_add_handler(&sdr->ctrl_hdl,
				    sdr->ep.subdev->ctrl_handler, NULL, true);
	if (ret) {
		rdrif_err(sdr, "failed: ctrl add hdlr ret %d\n", ret);
		goto error;
	}

	ret = rcar_drif_sdr_register(sdr);
	if (ret)
		goto error;

	return ret;

error:
	v4l2_ctrl_handler_free(&sdr->ctrl_hdl);

	return ret;
}

static const struct v4l2_async_notifier_operations rcar_drif_notify_ops = {
	.bound = rcar_drif_notify_bound,
	.unbind = rcar_drif_notify_unbind,
	.complete = rcar_drif_notify_complete,
};

/* Read endpoint properties */
static void rcar_drif_get_ep_properties(struct rcar_drif_sdr *sdr,
					struct fwnode_handle *fwnode)
{
	u32 val;

	/* Set the I2S defaults for SIRMDR1*/
	sdr->mdr1 = RCAR_DRIF_SIRMDR1_SYNCMD_LR | RCAR_DRIF_SIRMDR1_MSB_FIRST |
		RCAR_DRIF_SIRMDR1_DTDL_1 | RCAR_DRIF_SIRMDR1_SYNCDL_0;

	/* Parse sync polarity from endpoint */
	if (!fwnode_property_read_u32(fwnode, "sync-active", &val))
		sdr->mdr1 |= val ? RCAR_DRIF_SIRMDR1_SYNCAC_POL_HIGH :
			RCAR_DRIF_SIRMDR1_SYNCAC_POL_LOW;
	else
		sdr->mdr1 |= RCAR_DRIF_SIRMDR1_SYNCAC_POL_HIGH; /* default */

	dev_dbg(sdr->dev, "mdr1 0x%08x\n", sdr->mdr1);
}

/* Parse sub-devs (tuner) to find a matching device */
static int rcar_drif_parse_subdevs(struct rcar_drif_sdr *sdr)
{
	struct v4l2_async_notifier *notifier = &sdr->notifier;
	struct fwnode_handle *fwnode, *ep;
	struct v4l2_async_connection *asd;

	v4l2_async_nf_init(&sdr->notifier, &sdr->v4l2_dev);

	ep = fwnode_graph_get_next_endpoint(of_fwnode_handle(sdr->dev->of_node),
					    NULL);
	if (!ep)
		return 0;

	/* Get the endpoint properties */
	rcar_drif_get_ep_properties(sdr, ep);

	fwnode = fwnode_graph_get_remote_port_parent(ep);
	fwnode_handle_put(ep);
	if (!fwnode) {
		dev_warn(sdr->dev, "bad remote port parent\n");
		return -EINVAL;
	}

	asd = v4l2_async_nf_add_fwnode(notifier, fwnode,
				       struct v4l2_async_connection);
	fwnode_handle_put(fwnode);
	if (IS_ERR(asd))
		return PTR_ERR(asd);

	return 0;
}

/* Check if the given device is the primary bond */
static bool rcar_drif_primary_bond(struct platform_device *pdev)
{
	return of_property_read_bool(pdev->dev.of_node, "renesas,primary-bond");
}

/* Check if both devices of the bond are enabled */
static struct device_node *rcar_drif_bond_enabled(struct platform_device *p)
{
	struct device_node *np;

	np = of_parse_phandle(p->dev.of_node, "renesas,bonding", 0);
	if (np && of_device_is_available(np))
		return np;

	return NULL;
}

/* Check if the bonded device is probed */
static int rcar_drif_bond_available(struct rcar_drif_sdr *sdr,
				    struct device_node *np)
{
	struct platform_device *pdev;
	struct rcar_drif *ch;
	int ret = 0;

	pdev = of_find_device_by_node(np);
	if (!pdev) {
		dev_err(sdr->dev, "failed to get bonded device from node\n");
		return -ENODEV;
	}

	device_lock(&pdev->dev);
	ch = platform_get_drvdata(pdev);
	if (ch) {
		/* Update sdr data in the bonded device */
		ch->sdr = sdr;

		/* Update sdr with bonded device data */
		sdr->ch[ch->num] = ch;
		sdr->hw_ch_mask |= BIT(ch->num);
	} else {
		/* Defer */
		dev_info(sdr->dev, "defer probe\n");
		ret = -EPROBE_DEFER;
	}
	device_unlock(&pdev->dev);

	put_device(&pdev->dev);

	return ret;
}

/* V4L2 SDR device probe */
static int rcar_drif_sdr_probe(struct rcar_drif_sdr *sdr)
{
	int ret;

	/* Validate any supported format for enabled channels */
	ret = rcar_drif_set_default_format(sdr);
	if (ret) {
		dev_err(sdr->dev, "failed to set default format\n");
		return ret;
	}

	/* Set defaults */
	sdr->hwbuf_size = RCAR_DRIF_DEFAULT_HWBUF_SIZE;

	mutex_init(&sdr->v4l2_mutex);
	mutex_init(&sdr->vb_queue_mutex);
	spin_lock_init(&sdr->queued_bufs_lock);
	spin_lock_init(&sdr->dma_lock);
	INIT_LIST_HEAD(&sdr->queued_bufs);

	/* Init videobuf2 queue structure */
	sdr->vb_queue.type = V4L2_BUF_TYPE_SDR_CAPTURE;
	sdr->vb_queue.io_modes = VB2_READ | VB2_MMAP | VB2_DMABUF;
	sdr->vb_queue.drv_priv = sdr;
	sdr->vb_queue.buf_struct_size = sizeof(struct rcar_drif_frame_buf);
	sdr->vb_queue.ops = &rcar_drif_vb2_ops;
	sdr->vb_queue.mem_ops = &vb2_vmalloc_memops;
	sdr->vb_queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;

	/* Init videobuf2 queue */
	ret = vb2_queue_init(&sdr->vb_queue);
	if (ret) {
		dev_err(sdr->dev, "failed: vb2_queue_init ret %d\n", ret);
		return ret;
	}

	/* Register the v4l2_device */
	ret = v4l2_device_register(sdr->dev, &sdr->v4l2_dev);
	if (ret) {
		dev_err(sdr->dev, "failed: v4l2_device_register ret %d\n", ret);
		return ret;
	}

	/*
	 * Parse subdevs after v4l2_device_register because if the subdev
	 * is already probed, bound and complete will be called immediately
	 */
	ret = rcar_drif_parse_subdevs(sdr);
	if (ret)
		goto error;

	sdr->notifier.ops = &rcar_drif_notify_ops;

	/* Register notifier */
	ret = v4l2_async_nf_register(&sdr->notifier);
	if (ret < 0) {
		dev_err(sdr->dev, "failed: notifier register ret %d\n", ret);
		goto cleanup;
	}

	return ret;

cleanup:
	v4l2_async_nf_cleanup(&sdr->notifier);
error:
	v4l2_device_unregister(&sdr->v4l2_dev);

	return ret;
}

/* V4L2 SDR device remove */
static void rcar_drif_sdr_remove(struct rcar_drif_sdr *sdr)
{
	v4l2_async_nf_unregister(&sdr->notifier);
	v4l2_async_nf_cleanup(&sdr->notifier);
	v4l2_device_unregister(&sdr->v4l2_dev);
}

/* DRIF channel probe */
static int rcar_drif_probe(struct platform_device *pdev)
{
	struct rcar_drif_sdr *sdr;
	struct device_node *np;
	struct rcar_drif *ch;
	struct resource	*res;
	int ret;

	/* Reserve memory for enabled channel */
	ch = devm_kzalloc(&pdev->dev, sizeof(*ch), GFP_KERNEL);
	if (!ch)
		return -ENOMEM;

	ch->pdev = pdev;

	/* Module clock */
	ch->clk = devm_clk_get(&pdev->dev, "fck");
	if (IS_ERR(ch->clk)) {
		ret = PTR_ERR(ch->clk);
		dev_err(&pdev->dev, "clk get failed (%d)\n", ret);
		return ret;
	}

	/* Register map */
	ch->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(ch->base))
		return PTR_ERR(ch->base);

	ch->start = res->start;
	platform_set_drvdata(pdev, ch);

	/* Check if both channels of the bond are enabled */
	np = rcar_drif_bond_enabled(pdev);
	if (np) {
		/* Check if current channel acting as primary-bond */
		if (!rcar_drif_primary_bond(pdev)) {
			ch->num = 1;	/* Primary bond is channel 0 always */
			of_node_put(np);
			return 0;
		}
	}

	/* Reserve memory for SDR structure */
	sdr = devm_kzalloc(&pdev->dev, sizeof(*sdr), GFP_KERNEL);
	if (!sdr) {
		of_node_put(np);
		return -ENOMEM;
	}
	ch->sdr = sdr;
	sdr->dev = &pdev->dev;

	/* Establish links between SDR and channel(s) */
	sdr->ch[ch->num] = ch;
	sdr->hw_ch_mask = BIT(ch->num);
	if (np) {
		/* Check if bonded device is ready */
		ret = rcar_drif_bond_available(sdr, np);
		of_node_put(np);
		if (ret)
			return ret;
	}
	sdr->num_hw_ch = hweight_long(sdr->hw_ch_mask);

	return rcar_drif_sdr_probe(sdr);
}

/* DRIF channel remove */
static void rcar_drif_remove(struct platform_device *pdev)
{
	struct rcar_drif *ch = platform_get_drvdata(pdev);
	struct rcar_drif_sdr *sdr = ch->sdr;

	/* Channel 0 will be the SDR instance */
	if (ch->num)
		return;

	/* SDR instance */
	rcar_drif_sdr_remove(sdr);
}

/* FIXME: Implement suspend/resume support */
static int __maybe_unused rcar_drif_suspend(struct device *dev)
{
	return 0;
}

static int __maybe_unused rcar_drif_resume(struct device *dev)
{
	return 0;
}

static SIMPLE_DEV_PM_OPS(rcar_drif_pm_ops, rcar_drif_suspend,
			 rcar_drif_resume);

static const struct of_device_id rcar_drif_of_table[] = {
	{ .compatible = "renesas,rcar-gen3-drif" },
	{ }
};
MODULE_DEVICE_TABLE(of, rcar_drif_of_table);

#define RCAR_DRIF_DRV_NAME "rcar_drif"
static struct platform_driver rcar_drif_driver = {
	.driver = {
		.name = RCAR_DRIF_DRV_NAME,
		.of_match_table = rcar_drif_of_table,
		.pm = &rcar_drif_pm_ops,
		},
	.probe = rcar_drif_probe,
	.remove_new = rcar_drif_remove,
};

module_platform_driver(rcar_drif_driver);

MODULE_DESCRIPTION("Renesas R-Car Gen3 DRIF driver");
MODULE_ALIAS("platform:" RCAR_DRIF_DRV_NAME);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ramesh Shanmugasundaram <ramesh.shanmugasundaram@bp.renesas.com>");
