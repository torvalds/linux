// SPDX-License-Identifier: GPL-2.0
/*
 * V4L2 Capture CSI Subdev for Freescale i.MX6UL/L / i.MX7 SOC
 *
 * Copyright (c) 2019 Linaro Ltd
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>

#define IMX7_CSI_PAD_SINK		0
#define IMX7_CSI_PAD_SRC		1
#define IMX7_CSI_PADS_NUM		2

/* csi control reg 1 */
#define BIT_SWAP16_EN			BIT(31)
#define BIT_EXT_VSYNC			BIT(30)
#define BIT_EOF_INT_EN			BIT(29)
#define BIT_PRP_IF_EN			BIT(28)
#define BIT_CCIR_MODE			BIT(27)
#define BIT_COF_INT_EN			BIT(26)
#define BIT_SF_OR_INTEN			BIT(25)
#define BIT_RF_OR_INTEN			BIT(24)
#define BIT_SFF_DMA_DONE_INTEN		BIT(22)
#define BIT_STATFF_INTEN		BIT(21)
#define BIT_FB2_DMA_DONE_INTEN		BIT(20)
#define BIT_FB1_DMA_DONE_INTEN		BIT(19)
#define BIT_RXFF_INTEN			BIT(18)
#define BIT_SOF_POL			BIT(17)
#define BIT_SOF_INTEN			BIT(16)
#define BIT_MCLKDIV(n)			((n) << 12)
#define BIT_MCLKDIV_MASK		(0xf << 12)
#define BIT_HSYNC_POL			BIT(11)
#define BIT_CCIR_EN			BIT(10)
#define BIT_MCLKEN			BIT(9)
#define BIT_FCC				BIT(8)
#define BIT_PACK_DIR			BIT(7)
#define BIT_CLR_STATFIFO		BIT(6)
#define BIT_CLR_RXFIFO			BIT(5)
#define BIT_GCLK_MODE			BIT(4)
#define BIT_INV_DATA			BIT(3)
#define BIT_INV_PCLK			BIT(2)
#define BIT_REDGE			BIT(1)
#define BIT_PIXEL_BIT			BIT(0)

/* control reg 2 */
#define BIT_DMA_BURST_TYPE_RFF_INCR4	(1 << 30)
#define BIT_DMA_BURST_TYPE_RFF_INCR8	(2 << 30)
#define BIT_DMA_BURST_TYPE_RFF_INCR16	(3 << 30)
#define BIT_DMA_BURST_TYPE_RFF_MASK	(3 << 30)

/* control reg 3 */
#define BIT_FRMCNT(n)			((n) << 16)
#define BIT_FRMCNT_MASK			(0xffff << 16)
#define BIT_FRMCNT_RST			BIT(15)
#define BIT_DMA_REFLASH_RFF		BIT(14)
#define BIT_DMA_REFLASH_SFF		BIT(13)
#define BIT_DMA_REQ_EN_RFF		BIT(12)
#define BIT_DMA_REQ_EN_SFF		BIT(11)
#define BIT_STATFF_LEVEL(n)		((n) << 8)
#define BIT_STATFF_LEVEL_MASK		(0x7 << 8)
#define BIT_HRESP_ERR_EN		BIT(7)
#define BIT_RXFF_LEVEL(n)		((n) << 4)
#define BIT_RXFF_LEVEL_MASK		(0x7 << 4)
#define BIT_TWO_8BIT_SENSOR		BIT(3)
#define BIT_ZERO_PACK_EN		BIT(2)
#define BIT_ECC_INT_EN			BIT(1)
#define BIT_ECC_AUTO_EN			BIT(0)

/* csi status reg */
#define BIT_ADDR_CH_ERR_INT		BIT(28)
#define BIT_FIELD0_INT			BIT(27)
#define BIT_FIELD1_INT			BIT(26)
#define BIT_SFF_OR_INT			BIT(25)
#define BIT_RFF_OR_INT			BIT(24)
#define BIT_DMA_TSF_DONE_SFF		BIT(22)
#define BIT_STATFF_INT			BIT(21)
#define BIT_DMA_TSF_DONE_FB2		BIT(20)
#define BIT_DMA_TSF_DONE_FB1		BIT(19)
#define BIT_RXFF_INT			BIT(18)
#define BIT_EOF_INT			BIT(17)
#define BIT_SOF_INT			BIT(16)
#define BIT_F2_INT			BIT(15)
#define BIT_F1_INT			BIT(14)
#define BIT_COF_INT			BIT(13)
#define BIT_HRESP_ERR_INT		BIT(7)
#define BIT_ECC_INT			BIT(1)
#define BIT_DRDY			BIT(0)

/* csi image parameter reg */
#define BIT_IMAGE_WIDTH(n)		((n) << 16)
#define BIT_IMAGE_HEIGHT(n)		(n)

/* csi control reg 18 */
#define BIT_CSI_HW_ENABLE		BIT(31)
#define BIT_MIPI_DATA_FORMAT_RAW8	(0x2a << 25)
#define BIT_MIPI_DATA_FORMAT_RAW10	(0x2b << 25)
#define BIT_MIPI_DATA_FORMAT_RAW12	(0x2c << 25)
#define BIT_MIPI_DATA_FORMAT_RAW14	(0x2d << 25)
#define BIT_MIPI_DATA_FORMAT_YUV422_8B	(0x1e << 25)
#define BIT_MIPI_DATA_FORMAT_MASK	(0x3f << 25)
#define BIT_DATA_FROM_MIPI		BIT(22)
#define BIT_MIPI_YU_SWAP		BIT(21)
#define BIT_MIPI_DOUBLE_CMPNT		BIT(20)
#define BIT_MASK_OPTION_FIRST_FRAME	(0 << 18)
#define BIT_MASK_OPTION_CSI_EN		(1 << 18)
#define BIT_MASK_OPTION_SECOND_FRAME	(2 << 18)
#define BIT_MASK_OPTION_ON_DATA		(3 << 18)
#define BIT_BASEADDR_CHG_ERR_EN		BIT(9)
#define BIT_BASEADDR_SWITCH_SEL		BIT(5)
#define BIT_BASEADDR_SWITCH_EN		BIT(4)
#define BIT_PARALLEL24_EN		BIT(3)
#define BIT_DEINTERLACE_EN		BIT(2)
#define BIT_TVDECODER_IN_EN		BIT(1)
#define BIT_NTSC_EN			BIT(0)

#define CSI_MCLK_VF			1
#define CSI_MCLK_ENC			2
#define CSI_MCLK_RAW			4
#define CSI_MCLK_I2C			8

#define CSI_CSICR1			0x00
#define CSI_CSICR2			0x04
#define CSI_CSICR3			0x08
#define CSI_STATFIFO			0x0c
#define CSI_CSIRXFIFO			0x10
#define CSI_CSIRXCNT			0x14
#define CSI_CSISR			0x18

#define CSI_CSIDBG			0x1c
#define CSI_CSIDMASA_STATFIFO		0x20
#define CSI_CSIDMATS_STATFIFO		0x24
#define CSI_CSIDMASA_FB1		0x28
#define CSI_CSIDMASA_FB2		0x2c
#define CSI_CSIFBUF_PARA		0x30
#define CSI_CSIIMAG_PARA		0x34

#define CSI_CSICR18			0x48
#define CSI_CSICR19			0x4c

#define IMX7_CSI_VIDEO_NAME		"imx-capture"
/* In bytes, per queue */
#define IMX7_CSI_VIDEO_MEM_LIMIT	SZ_512M
#define IMX7_CSI_VIDEO_EOF_TIMEOUT	2000

#define IMX7_CSI_DEF_MBUS_CODE		MEDIA_BUS_FMT_UYVY8_2X8
#define IMX7_CSI_DEF_PIX_FORMAT		V4L2_PIX_FMT_UYVY
#define IMX7_CSI_DEF_PIX_WIDTH		640
#define IMX7_CSI_DEF_PIX_HEIGHT		480

enum imx_csi_model {
	IMX7_CSI_IMX7 = 0,
	IMX7_CSI_IMX8MQ,
};

struct imx7_csi_pixfmt {
	/* the in-memory FourCC pixel format */
	u32     fourcc;
	/*
	 * the set of equivalent media bus codes for the fourcc.
	 * NOTE! codes pointer is NULL for in-memory-only formats.
	 */
	const u32 *codes;
	int     bpp;     /* total bpp */
	bool	yuv;
};

struct imx7_csi_vb2_buffer {
	struct vb2_v4l2_buffer vbuf;
	struct list_head list;
};

static inline struct imx7_csi_vb2_buffer *
to_imx7_csi_vb2_buffer(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	return container_of(vbuf, struct imx7_csi_vb2_buffer, vbuf);
}

struct imx7_csi_dma_buf {
	void *virt;
	dma_addr_t dma_addr;
	unsigned long len;
};

struct imx7_csi {
	struct device *dev;

	/* Resources and locks */
	void __iomem *regbase;
	int irq;
	struct clk *mclk;

	spinlock_t irqlock; /* Protects last_eof */

	/* Media and V4L2 device */
	struct media_device mdev;
	struct v4l2_device v4l2_dev;
	struct v4l2_async_notifier notifier;
	struct media_pipeline pipe;

	struct v4l2_subdev *src_sd;
	bool is_csi2;

	/* V4L2 subdev */
	struct v4l2_subdev sd;
	struct media_pad pad[IMX7_CSI_PADS_NUM];

	/* Video device */
	struct video_device *vdev;		/* Video device */
	struct media_pad vdev_pad;		/* Video device pad */

	struct v4l2_pix_format vdev_fmt;	/* The user format */
	const struct imx7_csi_pixfmt *vdev_cc;
	struct v4l2_rect vdev_compose;		/* The compose rectangle */

	struct mutex vdev_mutex;		/* Protect vdev operations */

	struct vb2_queue q;			/* The videobuf2 queue */
	struct list_head ready_q;		/* List of queued buffers */
	spinlock_t q_lock;			/* Protect ready_q */

	/* Buffers and streaming state */
	struct imx7_csi_vb2_buffer *active_vb2_buf[2];
	struct imx7_csi_dma_buf underrun_buf;

	bool is_streaming;
	int buf_num;
	u32 frame_sequence;

	bool last_eof;
	struct completion last_eof_completion;

	enum imx_csi_model model;
};

static struct imx7_csi *
imx7_csi_notifier_to_dev(struct v4l2_async_notifier *n)
{
	return container_of(n, struct imx7_csi, notifier);
}

/* -----------------------------------------------------------------------------
 * Hardware Configuration
 */

static u32 imx7_csi_reg_read(struct imx7_csi *csi, unsigned int offset)
{
	return readl(csi->regbase + offset);
}

static void imx7_csi_reg_write(struct imx7_csi *csi, unsigned int value,
			       unsigned int offset)
{
	writel(value, csi->regbase + offset);
}

static u32 imx7_csi_irq_clear(struct imx7_csi *csi)
{
	u32 isr;

	isr = imx7_csi_reg_read(csi, CSI_CSISR);
	imx7_csi_reg_write(csi, isr, CSI_CSISR);

	return isr;
}

static void imx7_csi_init_default(struct imx7_csi *csi)
{
	imx7_csi_reg_write(csi, BIT_SOF_POL | BIT_REDGE | BIT_GCLK_MODE |
			   BIT_HSYNC_POL | BIT_FCC | BIT_MCLKDIV(1) |
			   BIT_MCLKEN, CSI_CSICR1);
	imx7_csi_reg_write(csi, 0, CSI_CSICR2);
	imx7_csi_reg_write(csi, BIT_FRMCNT_RST, CSI_CSICR3);

	imx7_csi_reg_write(csi, BIT_IMAGE_WIDTH(IMX7_CSI_DEF_PIX_WIDTH) |
			   BIT_IMAGE_HEIGHT(IMX7_CSI_DEF_PIX_HEIGHT),
			   CSI_CSIIMAG_PARA);

	imx7_csi_reg_write(csi, BIT_DMA_REFLASH_RFF, CSI_CSICR3);
}

static void imx7_csi_hw_enable_irq(struct imx7_csi *csi)
{
	u32 cr1 = imx7_csi_reg_read(csi, CSI_CSICR1);

	cr1 |= BIT_RFF_OR_INT;
	cr1 |= BIT_FB1_DMA_DONE_INTEN;
	cr1 |= BIT_FB2_DMA_DONE_INTEN;

	imx7_csi_reg_write(csi, cr1, CSI_CSICR1);
}

static void imx7_csi_hw_disable_irq(struct imx7_csi *csi)
{
	u32 cr1 = imx7_csi_reg_read(csi, CSI_CSICR1);

	cr1 &= ~BIT_RFF_OR_INT;
	cr1 &= ~BIT_FB1_DMA_DONE_INTEN;
	cr1 &= ~BIT_FB2_DMA_DONE_INTEN;

	imx7_csi_reg_write(csi, cr1, CSI_CSICR1);
}

static void imx7_csi_hw_enable(struct imx7_csi *csi)
{
	u32 cr = imx7_csi_reg_read(csi, CSI_CSICR18);

	cr |= BIT_CSI_HW_ENABLE;

	imx7_csi_reg_write(csi, cr, CSI_CSICR18);
}

static void imx7_csi_hw_disable(struct imx7_csi *csi)
{
	u32 cr = imx7_csi_reg_read(csi, CSI_CSICR18);

	cr &= ~BIT_CSI_HW_ENABLE;

	imx7_csi_reg_write(csi, cr, CSI_CSICR18);
}

static void imx7_csi_dma_reflash(struct imx7_csi *csi)
{
	u32 cr3;

	cr3 = imx7_csi_reg_read(csi, CSI_CSICR3);
	cr3 |= BIT_DMA_REFLASH_RFF;
	imx7_csi_reg_write(csi, cr3, CSI_CSICR3);
}

static void imx7_csi_rx_fifo_clear(struct imx7_csi *csi)
{
	u32 cr1 = imx7_csi_reg_read(csi, CSI_CSICR1) & ~BIT_FCC;

	imx7_csi_reg_write(csi, cr1, CSI_CSICR1);
	imx7_csi_reg_write(csi, cr1 | BIT_CLR_RXFIFO, CSI_CSICR1);
	imx7_csi_reg_write(csi, cr1 | BIT_FCC, CSI_CSICR1);
}

static void imx7_csi_dmareq_rff_enable(struct imx7_csi *csi)
{
	u32 cr3 = imx7_csi_reg_read(csi, CSI_CSICR3);

	cr3 |= BIT_DMA_REQ_EN_RFF;
	cr3 |= BIT_HRESP_ERR_EN;
	cr3 &= ~BIT_RXFF_LEVEL_MASK;
	cr3 |= BIT_RXFF_LEVEL(2);

	imx7_csi_reg_write(csi, cr3, CSI_CSICR3);
}

static void imx7_csi_dmareq_rff_disable(struct imx7_csi *csi)
{
	u32 cr3 = imx7_csi_reg_read(csi, CSI_CSICR3);

	cr3 &= ~BIT_DMA_REQ_EN_RFF;
	cr3 &= ~BIT_HRESP_ERR_EN;
	imx7_csi_reg_write(csi, cr3, CSI_CSICR3);
}

static void imx7_csi_update_buf(struct imx7_csi *csi, dma_addr_t dma_addr,
				int buf_num)
{
	if (buf_num == 1)
		imx7_csi_reg_write(csi, dma_addr, CSI_CSIDMASA_FB2);
	else
		imx7_csi_reg_write(csi, dma_addr, CSI_CSIDMASA_FB1);
}

static struct imx7_csi_vb2_buffer *imx7_csi_video_next_buf(struct imx7_csi *csi);

static void imx7_csi_setup_vb2_buf(struct imx7_csi *csi)
{
	struct imx7_csi_vb2_buffer *buf;
	struct vb2_buffer *vb2_buf;
	int i;

	for (i = 0; i < 2; i++) {
		dma_addr_t dma_addr;

		buf = imx7_csi_video_next_buf(csi);
		if (buf) {
			csi->active_vb2_buf[i] = buf;
			vb2_buf = &buf->vbuf.vb2_buf;
			dma_addr = vb2_dma_contig_plane_dma_addr(vb2_buf, 0);
		} else {
			csi->active_vb2_buf[i] = NULL;
			dma_addr = csi->underrun_buf.dma_addr;
		}

		imx7_csi_update_buf(csi, dma_addr, i);
	}
}

static void imx7_csi_dma_unsetup_vb2_buf(struct imx7_csi *csi,
					 enum vb2_buffer_state return_status)
{
	struct imx7_csi_vb2_buffer *buf;
	int i;

	/* return any remaining active frames with return_status */
	for (i = 0; i < 2; i++) {
		buf = csi->active_vb2_buf[i];
		if (buf) {
			struct vb2_buffer *vb = &buf->vbuf.vb2_buf;

			vb->timestamp = ktime_get_ns();
			vb2_buffer_done(vb, return_status);
			csi->active_vb2_buf[i] = NULL;
		}
	}
}

static void imx7_csi_free_dma_buf(struct imx7_csi *csi,
				  struct imx7_csi_dma_buf *buf)
{
	if (buf->virt)
		dma_free_coherent(csi->dev, buf->len, buf->virt, buf->dma_addr);

	buf->virt = NULL;
	buf->dma_addr = 0;
}

static int imx7_csi_alloc_dma_buf(struct imx7_csi *csi,
				  struct imx7_csi_dma_buf *buf, int size)
{
	imx7_csi_free_dma_buf(csi, buf);

	buf->len = PAGE_ALIGN(size);
	buf->virt = dma_alloc_coherent(csi->dev, buf->len, &buf->dma_addr,
				       GFP_DMA | GFP_KERNEL);
	if (!buf->virt)
		return -ENOMEM;

	return 0;
}

static int imx7_csi_dma_setup(struct imx7_csi *csi)
{
	int ret;

	ret = imx7_csi_alloc_dma_buf(csi, &csi->underrun_buf,
				     csi->vdev_fmt.sizeimage);
	if (ret < 0) {
		v4l2_warn(&csi->sd, "consider increasing the CMA area\n");
		return ret;
	}

	csi->frame_sequence = 0;
	csi->last_eof = false;
	init_completion(&csi->last_eof_completion);

	imx7_csi_setup_vb2_buf(csi);

	return 0;
}

static void imx7_csi_dma_cleanup(struct imx7_csi *csi,
				 enum vb2_buffer_state return_status)
{
	imx7_csi_dma_unsetup_vb2_buf(csi, return_status);
	imx7_csi_free_dma_buf(csi, &csi->underrun_buf);
}

static void imx7_csi_dma_stop(struct imx7_csi *csi)
{
	unsigned long timeout_jiffies;
	unsigned long flags;
	int ret;

	/* mark next EOF interrupt as the last before stream off */
	spin_lock_irqsave(&csi->irqlock, flags);
	csi->last_eof = true;
	spin_unlock_irqrestore(&csi->irqlock, flags);

	/*
	 * and then wait for interrupt handler to mark completion.
	 */
	timeout_jiffies = msecs_to_jiffies(IMX7_CSI_VIDEO_EOF_TIMEOUT);
	ret = wait_for_completion_timeout(&csi->last_eof_completion,
					  timeout_jiffies);
	if (ret == 0)
		v4l2_warn(&csi->sd, "wait last EOF timeout\n");

	imx7_csi_hw_disable_irq(csi);
}

static void imx7_csi_configure(struct imx7_csi *csi,
			       struct v4l2_subdev_state *sd_state)
{
	struct v4l2_pix_format *out_pix = &csi->vdev_fmt;
	int width = out_pix->width;
	u32 stride = 0;
	u32 cr3 = BIT_FRMCNT_RST;
	u32 cr1, cr18;

	cr18 = imx7_csi_reg_read(csi, CSI_CSICR18);

	cr18 &= ~(BIT_CSI_HW_ENABLE | BIT_MIPI_DATA_FORMAT_MASK |
		  BIT_DATA_FROM_MIPI | BIT_MIPI_DOUBLE_CMPNT |
		  BIT_BASEADDR_CHG_ERR_EN | BIT_BASEADDR_SWITCH_SEL |
		  BIT_BASEADDR_SWITCH_EN | BIT_DEINTERLACE_EN);

	if (out_pix->field == V4L2_FIELD_INTERLACED) {
		cr18 |= BIT_DEINTERLACE_EN;
		stride = out_pix->width;
	}

	if (!csi->is_csi2) {
		cr1 = BIT_SOF_POL | BIT_REDGE | BIT_GCLK_MODE | BIT_HSYNC_POL
		    | BIT_FCC | BIT_MCLKDIV(1) | BIT_MCLKEN;

		cr18 |= BIT_BASEADDR_SWITCH_EN | BIT_BASEADDR_SWITCH_SEL |
			BIT_BASEADDR_CHG_ERR_EN;

		if (out_pix->pixelformat == V4L2_PIX_FMT_UYVY ||
		    out_pix->pixelformat == V4L2_PIX_FMT_YUYV)
			width *= 2;
	} else {
		const struct v4l2_mbus_framefmt *sink_fmt;

		sink_fmt = v4l2_subdev_get_pad_format(&csi->sd, sd_state,
						      IMX7_CSI_PAD_SINK);

		cr1 = BIT_SOF_POL | BIT_REDGE | BIT_HSYNC_POL | BIT_FCC
		    | BIT_MCLKDIV(1) | BIT_MCLKEN;

		cr18 |= BIT_DATA_FROM_MIPI;

		switch (sink_fmt->code) {
		case MEDIA_BUS_FMT_Y8_1X8:
		case MEDIA_BUS_FMT_SBGGR8_1X8:
		case MEDIA_BUS_FMT_SGBRG8_1X8:
		case MEDIA_BUS_FMT_SGRBG8_1X8:
		case MEDIA_BUS_FMT_SRGGB8_1X8:
			cr18 |= BIT_MIPI_DATA_FORMAT_RAW8;
			break;
		case MEDIA_BUS_FMT_Y10_1X10:
		case MEDIA_BUS_FMT_SBGGR10_1X10:
		case MEDIA_BUS_FMT_SGBRG10_1X10:
		case MEDIA_BUS_FMT_SGRBG10_1X10:
		case MEDIA_BUS_FMT_SRGGB10_1X10:
			cr3 |= BIT_TWO_8BIT_SENSOR;
			cr18 |= BIT_MIPI_DATA_FORMAT_RAW10;
			break;
		case MEDIA_BUS_FMT_Y12_1X12:
		case MEDIA_BUS_FMT_SBGGR12_1X12:
		case MEDIA_BUS_FMT_SGBRG12_1X12:
		case MEDIA_BUS_FMT_SGRBG12_1X12:
		case MEDIA_BUS_FMT_SRGGB12_1X12:
			cr3 |= BIT_TWO_8BIT_SENSOR;
			cr18 |= BIT_MIPI_DATA_FORMAT_RAW12;
			break;
		case MEDIA_BUS_FMT_Y14_1X14:
		case MEDIA_BUS_FMT_SBGGR14_1X14:
		case MEDIA_BUS_FMT_SGBRG14_1X14:
		case MEDIA_BUS_FMT_SGRBG14_1X14:
		case MEDIA_BUS_FMT_SRGGB14_1X14:
			cr3 |= BIT_TWO_8BIT_SENSOR;
			cr18 |= BIT_MIPI_DATA_FORMAT_RAW14;
			break;

		/*
		 * The CSI bridge has a 16-bit input bus. Depending on the
		 * connected source, data may be transmitted with 8 or 10 bits
		 * per clock sample (in bits [9:2] or [9:0] respectively) or
		 * with 16 bits per clock sample (in bits [15:0]). The data is
		 * then packed into a 32-bit FIFO (as shown in figure 13-11 of
		 * the i.MX8MM reference manual rev. 3).
		 *
		 * The data packing in a 32-bit FIFO input word is controlled by
		 * the CR3 TWO_8BIT_SENSOR field (also known as SENSOR_16BITS in
		 * the i.MX8MM reference manual). When set to 0, data packing
		 * groups four 8-bit input samples (bits [9:2]). When set to 1,
		 * data packing groups two 16-bit input samples (bits [15:0]).
		 *
		 * The register field CR18 MIPI_DOUBLE_CMPNT also needs to be
		 * configured according to the input format for YUV 4:2:2 data.
		 * The field controls the gasket between the CSI-2 receiver and
		 * the CSI bridge. On i.MX7 and i.MX8MM, the field must be set
		 * to 1 when the CSIS outputs 16-bit samples. On i.MX8MQ, the
		 * gasket ignores the MIPI_DOUBLE_CMPNT bit and YUV 4:2:2 always
		 * uses 16-bit samples. Setting MIPI_DOUBLE_CMPNT in that case
		 * has no effect, but doesn't cause any issue.
		 */
		case MEDIA_BUS_FMT_UYVY8_2X8:
		case MEDIA_BUS_FMT_YUYV8_2X8:
			cr18 |= BIT_MIPI_DATA_FORMAT_YUV422_8B;
			break;
		case MEDIA_BUS_FMT_UYVY8_1X16:
		case MEDIA_BUS_FMT_YUYV8_1X16:
			cr3 |= BIT_TWO_8BIT_SENSOR;
			cr18 |= BIT_MIPI_DATA_FORMAT_YUV422_8B |
				BIT_MIPI_DOUBLE_CMPNT;
			break;
		}
	}

	imx7_csi_reg_write(csi, cr1, CSI_CSICR1);
	imx7_csi_reg_write(csi, BIT_DMA_BURST_TYPE_RFF_INCR16, CSI_CSICR2);
	imx7_csi_reg_write(csi, cr3, CSI_CSICR3);
	imx7_csi_reg_write(csi, cr18, CSI_CSICR18);

	imx7_csi_reg_write(csi, (width * out_pix->height) >> 2, CSI_CSIRXCNT);
	imx7_csi_reg_write(csi, BIT_IMAGE_WIDTH(width) |
			   BIT_IMAGE_HEIGHT(out_pix->height),
			   CSI_CSIIMAG_PARA);
	imx7_csi_reg_write(csi, stride, CSI_CSIFBUF_PARA);
}

static int imx7_csi_init(struct imx7_csi *csi,
			 struct v4l2_subdev_state *sd_state)
{
	int ret;

	ret = clk_prepare_enable(csi->mclk);
	if (ret < 0)
		return ret;

	imx7_csi_configure(csi, sd_state);

	ret = imx7_csi_dma_setup(csi);
	if (ret < 0) {
		clk_disable_unprepare(csi->mclk);
		return ret;
	}

	return 0;
}

static void imx7_csi_deinit(struct imx7_csi *csi,
			    enum vb2_buffer_state return_status)
{
	imx7_csi_dma_cleanup(csi, return_status);
	imx7_csi_init_default(csi);
	imx7_csi_dmareq_rff_disable(csi);
	clk_disable_unprepare(csi->mclk);
}

static void imx7_csi_baseaddr_switch_on_second_frame(struct imx7_csi *csi)
{
	u32 cr18 = imx7_csi_reg_read(csi, CSI_CSICR18);

	cr18 |= BIT_BASEADDR_SWITCH_EN | BIT_BASEADDR_SWITCH_SEL |
		BIT_BASEADDR_CHG_ERR_EN;
	cr18 |= BIT_MASK_OPTION_SECOND_FRAME;
	imx7_csi_reg_write(csi, cr18, CSI_CSICR18);
}

static void imx7_csi_enable(struct imx7_csi *csi)
{
	/* Clear the Rx FIFO and reflash the DMA controller. */
	imx7_csi_rx_fifo_clear(csi);
	imx7_csi_dma_reflash(csi);

	usleep_range(2000, 3000);

	/* Clear and enable the interrupts. */
	imx7_csi_irq_clear(csi);
	imx7_csi_hw_enable_irq(csi);

	/* Enable the RxFIFO DMA and the CSI. */
	imx7_csi_dmareq_rff_enable(csi);
	imx7_csi_hw_enable(csi);

	if (csi->model == IMX7_CSI_IMX8MQ)
		imx7_csi_baseaddr_switch_on_second_frame(csi);
}

static void imx7_csi_disable(struct imx7_csi *csi)
{
	imx7_csi_dma_stop(csi);

	imx7_csi_dmareq_rff_disable(csi);

	imx7_csi_hw_disable_irq(csi);

	imx7_csi_hw_disable(csi);
}

/* -----------------------------------------------------------------------------
 * Interrupt Handling
 */

static void imx7_csi_error_recovery(struct imx7_csi *csi)
{
	imx7_csi_hw_disable(csi);

	imx7_csi_rx_fifo_clear(csi);

	imx7_csi_dma_reflash(csi);

	imx7_csi_hw_enable(csi);
}

static void imx7_csi_vb2_buf_done(struct imx7_csi *csi)
{
	struct imx7_csi_vb2_buffer *done, *next;
	struct vb2_buffer *vb;
	dma_addr_t dma_addr;

	done = csi->active_vb2_buf[csi->buf_num];
	if (done) {
		done->vbuf.field = csi->vdev_fmt.field;
		done->vbuf.sequence = csi->frame_sequence;
		vb = &done->vbuf.vb2_buf;
		vb->timestamp = ktime_get_ns();
		vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
	}
	csi->frame_sequence++;

	/* get next queued buffer */
	next = imx7_csi_video_next_buf(csi);
	if (next) {
		dma_addr = vb2_dma_contig_plane_dma_addr(&next->vbuf.vb2_buf, 0);
		csi->active_vb2_buf[csi->buf_num] = next;
	} else {
		dma_addr = csi->underrun_buf.dma_addr;
		csi->active_vb2_buf[csi->buf_num] = NULL;
	}

	imx7_csi_update_buf(csi, dma_addr, csi->buf_num);
}

static irqreturn_t imx7_csi_irq_handler(int irq, void *data)
{
	struct imx7_csi *csi =  data;
	u32 status;

	spin_lock(&csi->irqlock);

	status = imx7_csi_irq_clear(csi);

	if (status & BIT_RFF_OR_INT) {
		dev_warn(csi->dev, "Rx fifo overflow\n");
		imx7_csi_error_recovery(csi);
	}

	if (status & BIT_HRESP_ERR_INT) {
		dev_warn(csi->dev, "Hresponse error detected\n");
		imx7_csi_error_recovery(csi);
	}

	if (status & BIT_ADDR_CH_ERR_INT) {
		imx7_csi_hw_disable(csi);

		imx7_csi_dma_reflash(csi);

		imx7_csi_hw_enable(csi);
	}

	if ((status & BIT_DMA_TSF_DONE_FB1) &&
	    (status & BIT_DMA_TSF_DONE_FB2)) {
		/*
		 * For both FB1 and FB2 interrupter bits set case,
		 * CSI DMA is work in one of FB1 and FB2 buffer,
		 * but software can not know the state.
		 * Skip it to avoid base address updated
		 * when csi work in field0 and field1 will write to
		 * new base address.
		 */
	} else if (status & BIT_DMA_TSF_DONE_FB1) {
		csi->buf_num = 0;
	} else if (status & BIT_DMA_TSF_DONE_FB2) {
		csi->buf_num = 1;
	}

	if ((status & BIT_DMA_TSF_DONE_FB1) ||
	    (status & BIT_DMA_TSF_DONE_FB2)) {
		imx7_csi_vb2_buf_done(csi);

		if (csi->last_eof) {
			complete(&csi->last_eof_completion);
			csi->last_eof = false;
		}
	}

	spin_unlock(&csi->irqlock);

	return IRQ_HANDLED;
}

/* -----------------------------------------------------------------------------
 * Format Helpers
 */

#define IMX_BUS_FMTS(fmt...) (const u32[]) {fmt, 0}

/*
 * List of supported pixel formats for the subdevs. Keep V4L2_PIX_FMT_UYVY and
 * MEDIA_BUS_FMT_UYVY8_2X8 first to match IMX7_CSI_DEF_PIX_FORMAT and
 * IMX7_CSI_DEF_MBUS_CODE.
 *
 * TODO: Restrict the supported formats list based on the SoC integration.
 *
 * The CSI bridge can be configured to sample pixel components from the Rx queue
 * in single (8bpp) or double (16bpp) component modes. Image format variants
 * with different sample sizes (ie YUYV_2X8 vs YUYV_1X16) determine the pixel
 * components sampling size per each clock cycle and their packing mode (see
 * imx7_csi_configure() for details).
 *
 * As the CSI bridge can be interfaced with different IP blocks depending on the
 * SoC model it is integrated on, the Rx queue sampling size should match the
 * size of the samples transferred by the transmitting IP block. To avoid
 * misconfigurations of the capture pipeline, the enumeration of the supported
 * formats should be restricted to match the pixel source transmitting mode.
 *
 * Example: i.MX8MM SoC integrates the CSI bridge with the Samsung CSIS CSI-2
 * receiver which operates in dual pixel sampling mode. The CSI bridge should
 * only expose the 1X16 formats variant which instructs it to operate in dual
 * pixel sampling mode. When the CSI bridge is instead integrated on an i.MX7,
 * which supports both serial and parallel input, it should expose both
 * variants.
 *
 * This currently only applies to YUYV formats, but other formats might need to
 * be handled in the same way.
 */
static const struct imx7_csi_pixfmt pixel_formats[] = {
	/*** YUV formats start here ***/
	{
		.fourcc	= V4L2_PIX_FMT_UYVY,
		.codes  = IMX_BUS_FMTS(
			MEDIA_BUS_FMT_UYVY8_2X8,
			MEDIA_BUS_FMT_UYVY8_1X16
		),
		.yuv	= true,
		.bpp    = 16,
	}, {
		.fourcc	= V4L2_PIX_FMT_YUYV,
		.codes  = IMX_BUS_FMTS(
			MEDIA_BUS_FMT_YUYV8_2X8,
			MEDIA_BUS_FMT_YUYV8_1X16
		),
		.yuv	= true,
		.bpp    = 16,
	},
	/*** raw bayer and grayscale formats start here ***/
	{
		.fourcc = V4L2_PIX_FMT_SBGGR8,
		.codes  = IMX_BUS_FMTS(MEDIA_BUS_FMT_SBGGR8_1X8),
		.bpp    = 8,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.codes  = IMX_BUS_FMTS(MEDIA_BUS_FMT_SGBRG8_1X8),
		.bpp    = 8,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG8,
		.codes  = IMX_BUS_FMTS(MEDIA_BUS_FMT_SGRBG8_1X8),
		.bpp    = 8,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB8,
		.codes  = IMX_BUS_FMTS(MEDIA_BUS_FMT_SRGGB8_1X8),
		.bpp    = 8,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR10,
		.codes  = IMX_BUS_FMTS(MEDIA_BUS_FMT_SBGGR10_1X10),
		.bpp    = 16,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG10,
		.codes  = IMX_BUS_FMTS(MEDIA_BUS_FMT_SGBRG10_1X10),
		.bpp    = 16,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG10,
		.codes  = IMX_BUS_FMTS(MEDIA_BUS_FMT_SGRBG10_1X10),
		.bpp    = 16,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB10,
		.codes  = IMX_BUS_FMTS(MEDIA_BUS_FMT_SRGGB10_1X10),
		.bpp    = 16,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR12,
		.codes  = IMX_BUS_FMTS(MEDIA_BUS_FMT_SBGGR12_1X12),
		.bpp    = 16,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG12,
		.codes  = IMX_BUS_FMTS(MEDIA_BUS_FMT_SGBRG12_1X12),
		.bpp    = 16,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG12,
		.codes  = IMX_BUS_FMTS(MEDIA_BUS_FMT_SGRBG12_1X12),
		.bpp    = 16,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB12,
		.codes  = IMX_BUS_FMTS(MEDIA_BUS_FMT_SRGGB12_1X12),
		.bpp    = 16,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR14,
		.codes  = IMX_BUS_FMTS(MEDIA_BUS_FMT_SBGGR14_1X14),
		.bpp    = 16,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG14,
		.codes  = IMX_BUS_FMTS(MEDIA_BUS_FMT_SGBRG14_1X14),
		.bpp    = 16,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG14,
		.codes  = IMX_BUS_FMTS(MEDIA_BUS_FMT_SGRBG14_1X14),
		.bpp    = 16,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB14,
		.codes  = IMX_BUS_FMTS(MEDIA_BUS_FMT_SRGGB14_1X14),
		.bpp    = 16,
	}, {
		.fourcc = V4L2_PIX_FMT_GREY,
		.codes  = IMX_BUS_FMTS(MEDIA_BUS_FMT_Y8_1X8),
		.bpp    = 8,
	}, {
		.fourcc = V4L2_PIX_FMT_Y10,
		.codes  = IMX_BUS_FMTS(MEDIA_BUS_FMT_Y10_1X10),
		.bpp    = 16,
	}, {
		.fourcc = V4L2_PIX_FMT_Y12,
		.codes  = IMX_BUS_FMTS(MEDIA_BUS_FMT_Y12_1X12),
		.bpp    = 16,
	}, {
		.fourcc = V4L2_PIX_FMT_Y14,
		.codes  = IMX_BUS_FMTS(MEDIA_BUS_FMT_Y14_1X14),
		.bpp    = 16,
	},
};

/*
 * Search in the pixel_formats[] array for an entry with the given fourcc
 * return it.
 */
static const struct imx7_csi_pixfmt *imx7_csi_find_pixel_format(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pixel_formats); i++) {
		const struct imx7_csi_pixfmt *fmt = &pixel_formats[i];

		if (fmt->fourcc == fourcc)
			return fmt;
	}

	return NULL;
}

/*
 * Search in the pixel_formats[] array for an entry with the given media
 * bus code and return it.
 */
static const struct imx7_csi_pixfmt *imx7_csi_find_mbus_format(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pixel_formats); i++) {
		const struct imx7_csi_pixfmt *fmt = &pixel_formats[i];
		unsigned int j;

		if (!fmt->codes)
			continue;

		for (j = 0; fmt->codes[j]; j++) {
			if (code == fmt->codes[j])
				return fmt;
		}
	}

	return NULL;
}

/*
 * Enumerate entries in the pixel_formats[] array that match the
 * requested search criteria. Return the media-bus code that matches
 * the search criteria at the requested match index.
 *
 * @code: The returned media-bus code that matches the search criteria at
 *        the requested match index.
 * @index: The requested match index.
 */
static int imx7_csi_enum_mbus_formats(u32 *code, u32 index)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pixel_formats); i++) {
		const struct imx7_csi_pixfmt *fmt = &pixel_formats[i];
		unsigned int j;

		if (!fmt->codes)
			continue;

		for (j = 0; fmt->codes[j]; j++) {
			if (index == 0) {
				*code = fmt->codes[j];
				return 0;
			}

			index--;
		}
	}

	return -EINVAL;
}

/* -----------------------------------------------------------------------------
 * Video Capture Device - IOCTLs
 */

static int imx7_csi_video_querycap(struct file *file, void *fh,
				   struct v4l2_capability *cap)
{
	struct imx7_csi *csi = video_drvdata(file);

	strscpy(cap->driver, IMX7_CSI_VIDEO_NAME, sizeof(cap->driver));
	strscpy(cap->card, IMX7_CSI_VIDEO_NAME, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", dev_name(csi->dev));

	return 0;
}

static int imx7_csi_video_enum_fmt_vid_cap(struct file *file, void *fh,
					   struct v4l2_fmtdesc *f)
{
	unsigned int index = f->index;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pixel_formats); i++) {
		const struct imx7_csi_pixfmt *fmt = &pixel_formats[i];

		/*
		 * If a media bus code is specified, only consider formats that
		 * match it.
		 */
		if (f->mbus_code) {
			unsigned int j;

			if (!fmt->codes)
				continue;

			for (j = 0; fmt->codes[j]; j++) {
				if (f->mbus_code == fmt->codes[j])
					break;
			}

			if (!fmt->codes[j])
				continue;
		}

		if (index == 0) {
			f->pixelformat = fmt->fourcc;
			return 0;
		}

		index--;
	}

	return -EINVAL;
}

static int imx7_csi_video_enum_framesizes(struct file *file, void *fh,
					  struct v4l2_frmsizeenum *fsize)
{
	const struct imx7_csi_pixfmt *cc;

	if (fsize->index > 0)
		return -EINVAL;

	cc = imx7_csi_find_pixel_format(fsize->pixel_format);
	if (!cc)
		return -EINVAL;

	/*
	 * TODO: The constraints are hardware-specific and may depend on the
	 * pixel format. This should come from the driver using
	 * imx_media_capture.
	 */
	fsize->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;
	fsize->stepwise.min_width = 1;
	fsize->stepwise.max_width = 65535;
	fsize->stepwise.min_height = 1;
	fsize->stepwise.max_height = 65535;
	fsize->stepwise.step_width = 1;
	fsize->stepwise.step_height = 1;

	return 0;
}

static int imx7_csi_video_g_fmt_vid_cap(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct imx7_csi *csi = video_drvdata(file);

	f->fmt.pix = csi->vdev_fmt;

	return 0;
}

static const struct imx7_csi_pixfmt *
__imx7_csi_video_try_fmt(struct v4l2_pix_format *pixfmt,
			 struct v4l2_rect *compose)
{
	const struct imx7_csi_pixfmt *cc;
	u32 walign;

	if (compose) {
		compose->width = pixfmt->width;
		compose->height = pixfmt->height;
	}

	/*
	 * Find the pixel format, default to the first supported format if not
	 * found.
	 */
	cc = imx7_csi_find_pixel_format(pixfmt->pixelformat);
	if (!cc) {
		pixfmt->pixelformat = IMX7_CSI_DEF_PIX_FORMAT;
		cc = imx7_csi_find_pixel_format(pixfmt->pixelformat);
	}

	/*
	 * The width alignment is 8 bytes as indicated by the
	 * CSI_IMAG_PARA.IMAGE_WIDTH documentation. Convert it to pixels.
	 *
	 * TODO: Implement configurable stride support.
	 */
	walign = 8 * 8 / cc->bpp;
	v4l_bound_align_image(&pixfmt->width, 1, 0xffff, walign,
			      &pixfmt->height, 1, 0xffff, 1, 0);

	pixfmt->bytesperline = pixfmt->width * cc->bpp / 8;
	pixfmt->sizeimage = pixfmt->bytesperline * pixfmt->height;
	pixfmt->field = V4L2_FIELD_NONE;

	return cc;
}

static int imx7_csi_video_try_fmt_vid_cap(struct file *file, void *fh,
					  struct v4l2_format *f)
{
	__imx7_csi_video_try_fmt(&f->fmt.pix, NULL);
	return 0;
}

static int imx7_csi_video_s_fmt_vid_cap(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct imx7_csi *csi = video_drvdata(file);
	const struct imx7_csi_pixfmt *cc;

	if (vb2_is_busy(&csi->q)) {
		dev_err(csi->dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	cc = __imx7_csi_video_try_fmt(&f->fmt.pix, &csi->vdev_compose);

	csi->vdev_cc = cc;
	csi->vdev_fmt = f->fmt.pix;

	return 0;
}

static int imx7_csi_video_g_selection(struct file *file, void *fh,
				      struct v4l2_selection *s)
{
	struct imx7_csi *csi = video_drvdata(file);

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		/* The compose rectangle is fixed to the source format. */
		s->r = csi->vdev_compose;
		break;
	case V4L2_SEL_TGT_COMPOSE_PADDED:
		/*
		 * The hardware writes with a configurable but fixed DMA burst
		 * size. If the source format width is not burst size aligned,
		 * the written frame contains padding to the right.
		 */
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = csi->vdev_fmt.width;
		s->r.height = csi->vdev_fmt.height;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ioctl_ops imx7_csi_video_ioctl_ops = {
	.vidioc_querycap		= imx7_csi_video_querycap,

	.vidioc_enum_fmt_vid_cap	= imx7_csi_video_enum_fmt_vid_cap,
	.vidioc_enum_framesizes		= imx7_csi_video_enum_framesizes,

	.vidioc_g_fmt_vid_cap		= imx7_csi_video_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= imx7_csi_video_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= imx7_csi_video_s_fmt_vid_cap,

	.vidioc_g_selection		= imx7_csi_video_g_selection,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
};

/* -----------------------------------------------------------------------------
 * Video Capture Device - Queue Operations
 */

static int imx7_csi_video_queue_setup(struct vb2_queue *vq,
				      unsigned int *nbuffers,
				      unsigned int *nplanes,
				      unsigned int sizes[],
				      struct device *alloc_devs[])
{
	struct imx7_csi *csi = vb2_get_drv_priv(vq);
	struct v4l2_pix_format *pix = &csi->vdev_fmt;
	unsigned int count = *nbuffers;

	if (vq->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (*nplanes) {
		if (*nplanes != 1 || sizes[0] < pix->sizeimage)
			return -EINVAL;
		count += vq->num_buffers;
	}

	count = min_t(__u32, IMX7_CSI_VIDEO_MEM_LIMIT / pix->sizeimage, count);

	if (*nplanes)
		*nbuffers = (count < vq->num_buffers) ? 0 :
			count - vq->num_buffers;
	else
		*nbuffers = count;

	*nplanes = 1;
	sizes[0] = pix->sizeimage;

	return 0;
}

static int imx7_csi_video_buf_init(struct vb2_buffer *vb)
{
	struct imx7_csi_vb2_buffer *buf = to_imx7_csi_vb2_buffer(vb);

	INIT_LIST_HEAD(&buf->list);

	return 0;
}

static int imx7_csi_video_buf_prepare(struct vb2_buffer *vb)
{
	struct imx7_csi *csi = vb2_get_drv_priv(vb->vb2_queue);
	struct v4l2_pix_format *pix = &csi->vdev_fmt;

	if (vb2_plane_size(vb, 0) < pix->sizeimage) {
		dev_err(csi->dev,
			"data will not fit into plane (%lu < %lu)\n",
			vb2_plane_size(vb, 0), (long)pix->sizeimage);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, pix->sizeimage);

	return 0;
}

static bool imx7_csi_fast_track_buffer(struct imx7_csi *csi,
				       struct imx7_csi_vb2_buffer *buf)
{
	unsigned long flags;
	dma_addr_t dma_addr;
	int buf_num;
	u32 isr;

	if (!csi->is_streaming)
		return false;

	dma_addr = vb2_dma_contig_plane_dma_addr(&buf->vbuf.vb2_buf, 0);

	/*
	 * buf_num holds the framebuffer ID of the most recently (*not* the
	 * next anticipated) triggered interrupt. Without loss of generality,
	 * if buf_num is 0, the hardware is capturing to FB2. If FB1 has been
	 * programmed with a dummy buffer (as indicated by active_vb2_buf[0]
	 * being NULL), then we can fast-track the new buffer by programming
	 * its address in FB1 before the hardware completes FB2, instead of
	 * adding it to the buffer queue and incurring a delay of one
	 * additional frame.
	 *
	 * The irqlock prevents races with the interrupt handler that updates
	 * buf_num when it programs the next buffer, but we can still race with
	 * the hardware if we program the buffer in FB1 just after the hardware
	 * completes FB2 and switches to FB1 and before buf_num can be updated
	 * by the interrupt handler for FB2.  The fast-tracked buffer would
	 * then be ignored by the hardware while the driver would think it has
	 * successfully been processed.
	 *
	 * To avoid this problem, if we can't avoid the race, we can detect
	 * that we have lost it by checking, after programming the buffer in
	 * FB1, if the interrupt flag indicating completion of FB2 has been
	 * raised. If that is not the case, fast-tracking succeeded, and we can
	 * update active_vb2_buf[0]. Otherwise, we may or may not have lost the
	 * race (as the interrupt flag may have been raised just after
	 * programming FB1 and before we read the interrupt status register),
	 * and we need to assume the worst case of a race loss and queue the
	 * buffer through the slow path.
	 */

	spin_lock_irqsave(&csi->irqlock, flags);

	buf_num = csi->buf_num;
	if (csi->active_vb2_buf[buf_num]) {
		spin_unlock_irqrestore(&csi->irqlock, flags);
		return false;
	}

	imx7_csi_update_buf(csi, dma_addr, buf_num);

	isr = imx7_csi_reg_read(csi, CSI_CSISR);
	if (isr & (buf_num ? BIT_DMA_TSF_DONE_FB1 : BIT_DMA_TSF_DONE_FB2)) {
		/*
		 * The interrupt for the /other/ FB just came (the isr hasn't
		 * run yet though, because we have the lock here); we can't be
		 * sure we've programmed buf_num FB in time, so queue the buffer
		 * to the buffer queue normally. No need to undo writing the FB
		 * register, since we won't return it as active_vb2_buf is NULL,
		 * so it's okay to potentially write it to both FB1 and FB2;
		 * only the one where it was queued normally will be returned.
		 */
		spin_unlock_irqrestore(&csi->irqlock, flags);
		return false;
	}

	csi->active_vb2_buf[buf_num] = buf;

	spin_unlock_irqrestore(&csi->irqlock, flags);
	return true;
}

static void imx7_csi_video_buf_queue(struct vb2_buffer *vb)
{
	struct imx7_csi *csi = vb2_get_drv_priv(vb->vb2_queue);
	struct imx7_csi_vb2_buffer *buf = to_imx7_csi_vb2_buffer(vb);
	unsigned long flags;

	if (imx7_csi_fast_track_buffer(csi, buf))
		return;

	spin_lock_irqsave(&csi->q_lock, flags);

	list_add_tail(&buf->list, &csi->ready_q);

	spin_unlock_irqrestore(&csi->q_lock, flags);
}

static int imx7_csi_video_validate_fmt(struct imx7_csi *csi)
{
	struct v4l2_subdev_format fmt_src = {
		.pad = IMX7_CSI_PAD_SRC,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	const struct imx7_csi_pixfmt *cc;
	int ret;

	/* Retrieve the media bus format on the source subdev. */
	ret = v4l2_subdev_call_state_active(&csi->sd, pad, get_fmt, &fmt_src);
	if (ret)
		return ret;

	/*
	 * Verify that the media bus size matches the size set on the video
	 * node. It is sufficient to check the compose rectangle size without
	 * checking the rounded size from pix_fmt, as the rounded size is
	 * derived directly from the compose rectangle size, and will thus
	 * always match if the compose rectangle matches.
	 */
	if (csi->vdev_compose.width != fmt_src.format.width ||
	    csi->vdev_compose.height != fmt_src.format.height)
		return -EPIPE;

	/*
	 * Verify that the media bus code is compatible with the pixel format
	 * set on the video node.
	 */
	cc = imx7_csi_find_mbus_format(fmt_src.format.code);
	if (!cc || csi->vdev_cc->yuv != cc->yuv)
		return -EPIPE;

	return 0;
}

static int imx7_csi_video_start_streaming(struct vb2_queue *vq,
					  unsigned int count)
{
	struct imx7_csi *csi = vb2_get_drv_priv(vq);
	struct imx7_csi_vb2_buffer *buf, *tmp;
	unsigned long flags;
	int ret;

	ret = imx7_csi_video_validate_fmt(csi);
	if (ret) {
		dev_err(csi->dev, "capture format not valid\n");
		goto err_buffers;
	}

	mutex_lock(&csi->mdev.graph_mutex);

	ret = __video_device_pipeline_start(csi->vdev, &csi->pipe);
	if (ret)
		goto err_unlock;

	ret = v4l2_subdev_call(&csi->sd, video, s_stream, 1);
	if (ret)
		goto err_stop;

	mutex_unlock(&csi->mdev.graph_mutex);

	return 0;

err_stop:
	__video_device_pipeline_stop(csi->vdev);
err_unlock:
	mutex_unlock(&csi->mdev.graph_mutex);
	dev_err(csi->dev, "pipeline start failed with %d\n", ret);
err_buffers:
	spin_lock_irqsave(&csi->q_lock, flags);
	list_for_each_entry_safe(buf, tmp, &csi->ready_q, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vbuf.vb2_buf, VB2_BUF_STATE_QUEUED);
	}
	spin_unlock_irqrestore(&csi->q_lock, flags);
	return ret;
}

static void imx7_csi_video_stop_streaming(struct vb2_queue *vq)
{
	struct imx7_csi *csi = vb2_get_drv_priv(vq);
	struct imx7_csi_vb2_buffer *frame;
	struct imx7_csi_vb2_buffer *tmp;
	unsigned long flags;

	mutex_lock(&csi->mdev.graph_mutex);
	v4l2_subdev_call(&csi->sd, video, s_stream, 0);
	__video_device_pipeline_stop(csi->vdev);
	mutex_unlock(&csi->mdev.graph_mutex);

	/* release all active buffers */
	spin_lock_irqsave(&csi->q_lock, flags);
	list_for_each_entry_safe(frame, tmp, &csi->ready_q, list) {
		list_del(&frame->list);
		vb2_buffer_done(&frame->vbuf.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&csi->q_lock, flags);
}

static const struct vb2_ops imx7_csi_video_qops = {
	.queue_setup	 = imx7_csi_video_queue_setup,
	.buf_init        = imx7_csi_video_buf_init,
	.buf_prepare	 = imx7_csi_video_buf_prepare,
	.buf_queue	 = imx7_csi_video_buf_queue,
	.wait_prepare	 = vb2_ops_wait_prepare,
	.wait_finish	 = vb2_ops_wait_finish,
	.start_streaming = imx7_csi_video_start_streaming,
	.stop_streaming  = imx7_csi_video_stop_streaming,
};

/* -----------------------------------------------------------------------------
 * Video Capture Device - File Operations
 */

static int imx7_csi_video_open(struct file *file)
{
	struct imx7_csi *csi = video_drvdata(file);
	int ret;

	if (mutex_lock_interruptible(&csi->vdev_mutex))
		return -ERESTARTSYS;

	ret = v4l2_fh_open(file);
	if (ret) {
		dev_err(csi->dev, "v4l2_fh_open failed\n");
		goto out;
	}

	ret = v4l2_pipeline_pm_get(&csi->vdev->entity);
	if (ret)
		v4l2_fh_release(file);

out:
	mutex_unlock(&csi->vdev_mutex);
	return ret;
}

static int imx7_csi_video_release(struct file *file)
{
	struct imx7_csi *csi = video_drvdata(file);
	struct vb2_queue *vq = &csi->q;

	mutex_lock(&csi->vdev_mutex);

	if (file->private_data == vq->owner) {
		vb2_queue_release(vq);
		vq->owner = NULL;
	}

	v4l2_pipeline_pm_put(&csi->vdev->entity);

	v4l2_fh_release(file);
	mutex_unlock(&csi->vdev_mutex);
	return 0;
}

static const struct v4l2_file_operations imx7_csi_video_fops = {
	.owner		= THIS_MODULE,
	.open		= imx7_csi_video_open,
	.release	= imx7_csi_video_release,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= vb2_fop_mmap,
};

/* -----------------------------------------------------------------------------
 * Video Capture Device - Init & Cleanup
 */

static struct imx7_csi_vb2_buffer *imx7_csi_video_next_buf(struct imx7_csi *csi)
{
	struct imx7_csi_vb2_buffer *buf = NULL;
	unsigned long flags;

	spin_lock_irqsave(&csi->q_lock, flags);

	/* get next queued buffer */
	if (!list_empty(&csi->ready_q)) {
		buf = list_entry(csi->ready_q.next, struct imx7_csi_vb2_buffer,
				 list);
		list_del(&buf->list);
	}

	spin_unlock_irqrestore(&csi->q_lock, flags);

	return buf;
}

static void imx7_csi_video_init_format(struct imx7_csi *csi)
{
	struct v4l2_pix_format *pixfmt = &csi->vdev_fmt;

	pixfmt->width = IMX7_CSI_DEF_PIX_WIDTH;
	pixfmt->height = IMX7_CSI_DEF_PIX_HEIGHT;

	csi->vdev_cc = __imx7_csi_video_try_fmt(pixfmt, &csi->vdev_compose);
}

static int imx7_csi_video_register(struct imx7_csi *csi)
{
	struct v4l2_subdev *sd = &csi->sd;
	struct v4l2_device *v4l2_dev = sd->v4l2_dev;
	struct video_device *vdev = csi->vdev;
	int ret;

	vdev->v4l2_dev = v4l2_dev;

	/* Initialize the default format and compose rectangle. */
	imx7_csi_video_init_format(csi);

	/* Register the video device. */
	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(csi->dev, "Failed to register video device\n");
		return ret;
	}

	dev_info(csi->dev, "Registered %s as /dev/%s\n", vdev->name,
		 video_device_node_name(vdev));

	/* Create the link from the CSI subdev to the video device. */
	ret = media_create_pad_link(&sd->entity, IMX7_CSI_PAD_SRC,
				    &vdev->entity, 0, MEDIA_LNK_FL_IMMUTABLE |
				    MEDIA_LNK_FL_ENABLED);
	if (ret) {
		dev_err(csi->dev, "failed to create link to device node\n");
		video_unregister_device(vdev);
		return ret;
	}

	return 0;
}

static void imx7_csi_video_unregister(struct imx7_csi *csi)
{
	media_entity_cleanup(&csi->vdev->entity);
	video_unregister_device(csi->vdev);
}

static int imx7_csi_video_init(struct imx7_csi *csi)
{
	struct video_device *vdev;
	struct vb2_queue *vq;
	int ret;

	mutex_init(&csi->vdev_mutex);
	INIT_LIST_HEAD(&csi->ready_q);
	spin_lock_init(&csi->q_lock);

	/* Allocate and initialize the video device. */
	vdev = video_device_alloc();
	if (!vdev)
		return -ENOMEM;

	vdev->fops = &imx7_csi_video_fops;
	vdev->ioctl_ops = &imx7_csi_video_ioctl_ops;
	vdev->minor = -1;
	vdev->release = video_device_release;
	vdev->vfl_dir = VFL_DIR_RX;
	vdev->tvnorms = V4L2_STD_NTSC | V4L2_STD_PAL | V4L2_STD_SECAM;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING
			 | V4L2_CAP_IO_MC;
	vdev->lock = &csi->vdev_mutex;
	vdev->queue = &csi->q;

	snprintf(vdev->name, sizeof(vdev->name), "%s capture", csi->sd.name);

	video_set_drvdata(vdev, csi);
	csi->vdev = vdev;

	/* Initialize the video device pad. */
	csi->vdev_pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vdev->entity, 1, &csi->vdev_pad);
	if (ret) {
		video_device_release(vdev);
		return ret;
	}

	/* Initialize the vb2 queue. */
	vq = &csi->q;
	vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vq->io_modes = VB2_MMAP | VB2_DMABUF;
	vq->drv_priv = csi;
	vq->buf_struct_size = sizeof(struct imx7_csi_vb2_buffer);
	vq->ops = &imx7_csi_video_qops;
	vq->mem_ops = &vb2_dma_contig_memops;
	vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	vq->lock = &csi->vdev_mutex;
	vq->min_buffers_needed = 2;
	vq->dev = csi->dev;

	ret = vb2_queue_init(vq);
	if (ret) {
		dev_err(csi->dev, "vb2_queue_init failed\n");
		video_device_release(vdev);
		return ret;
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdev Operations
 */

static int imx7_csi_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);
	struct v4l2_subdev_state *sd_state;
	int ret = 0;

	sd_state = v4l2_subdev_lock_and_get_active_state(sd);

	if (enable) {
		ret = imx7_csi_init(csi, sd_state);
		if (ret < 0)
			goto out_unlock;

		ret = v4l2_subdev_call(csi->src_sd, video, s_stream, 1);
		if (ret < 0) {
			imx7_csi_deinit(csi, VB2_BUF_STATE_QUEUED);
			goto out_unlock;
		}

		imx7_csi_enable(csi);
	} else {
		imx7_csi_disable(csi);

		v4l2_subdev_call(csi->src_sd, video, s_stream, 0);

		imx7_csi_deinit(csi, VB2_BUF_STATE_ERROR);
	}

	csi->is_streaming = !!enable;

out_unlock:
	v4l2_subdev_unlock_state(sd_state);

	return ret;
}

static int imx7_csi_init_cfg(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state)
{
	const struct imx7_csi_pixfmt *cc;
	int i;

	cc = imx7_csi_find_mbus_format(IMX7_CSI_DEF_MBUS_CODE);

	for (i = 0; i < IMX7_CSI_PADS_NUM; i++) {
		struct v4l2_mbus_framefmt *mf =
			v4l2_subdev_get_pad_format(sd, sd_state, i);

		mf->code = IMX7_CSI_DEF_MBUS_CODE;
		mf->width = IMX7_CSI_DEF_PIX_WIDTH;
		mf->height = IMX7_CSI_DEF_PIX_HEIGHT;
		mf->field = V4L2_FIELD_NONE;

		mf->colorspace = V4L2_COLORSPACE_SRGB;
		mf->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(mf->colorspace);
		mf->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(mf->colorspace);
		mf->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(!cc->yuv,
					mf->colorspace, mf->ycbcr_enc);
	}

	return 0;
}

static int imx7_csi_enum_mbus_code(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_mbus_code_enum *code)
{
	struct v4l2_mbus_framefmt *in_fmt;
	int ret = 0;

	in_fmt = v4l2_subdev_get_pad_format(sd, sd_state, IMX7_CSI_PAD_SINK);

	switch (code->pad) {
	case IMX7_CSI_PAD_SINK:
		ret = imx7_csi_enum_mbus_formats(&code->code, code->index);
		break;

	case IMX7_CSI_PAD_SRC:
		if (code->index != 0) {
			ret = -EINVAL;
			break;
		}

		code->code = in_fmt->code;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/*
 * Default the colorspace in tryfmt to SRGB if set to an unsupported
 * colorspace or not initialized. Then set the remaining colorimetry
 * parameters based on the colorspace if they are uninitialized.
 *
 * tryfmt->code must be set on entry.
 */
static void imx7_csi_try_colorimetry(struct v4l2_mbus_framefmt *tryfmt)
{
	const struct imx7_csi_pixfmt *cc;
	bool is_rgb = false;

	cc = imx7_csi_find_mbus_format(tryfmt->code);
	if (cc && !cc->yuv)
		is_rgb = true;

	switch (tryfmt->colorspace) {
	case V4L2_COLORSPACE_SMPTE170M:
	case V4L2_COLORSPACE_REC709:
	case V4L2_COLORSPACE_JPEG:
	case V4L2_COLORSPACE_SRGB:
	case V4L2_COLORSPACE_BT2020:
	case V4L2_COLORSPACE_OPRGB:
	case V4L2_COLORSPACE_DCI_P3:
	case V4L2_COLORSPACE_RAW:
		break;
	default:
		tryfmt->colorspace = V4L2_COLORSPACE_SRGB;
		break;
	}

	if (tryfmt->xfer_func == V4L2_XFER_FUNC_DEFAULT)
		tryfmt->xfer_func =
			V4L2_MAP_XFER_FUNC_DEFAULT(tryfmt->colorspace);

	if (tryfmt->ycbcr_enc == V4L2_YCBCR_ENC_DEFAULT)
		tryfmt->ycbcr_enc =
			V4L2_MAP_YCBCR_ENC_DEFAULT(tryfmt->colorspace);

	if (tryfmt->quantization == V4L2_QUANTIZATION_DEFAULT)
		tryfmt->quantization =
			V4L2_MAP_QUANTIZATION_DEFAULT(is_rgb,
						      tryfmt->colorspace,
						      tryfmt->ycbcr_enc);
}

static void imx7_csi_try_fmt(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_format *sdformat,
			     const struct imx7_csi_pixfmt **cc)
{
	const struct imx7_csi_pixfmt *in_cc;
	struct v4l2_mbus_framefmt *in_fmt;
	u32 code;

	in_fmt = v4l2_subdev_get_pad_format(sd, sd_state, IMX7_CSI_PAD_SINK);

	switch (sdformat->pad) {
	case IMX7_CSI_PAD_SRC:
		in_cc = imx7_csi_find_mbus_format(in_fmt->code);

		sdformat->format.width = in_fmt->width;
		sdformat->format.height = in_fmt->height;
		sdformat->format.code = in_fmt->code;
		sdformat->format.field = in_fmt->field;
		*cc = in_cc;

		sdformat->format.colorspace = in_fmt->colorspace;
		sdformat->format.xfer_func = in_fmt->xfer_func;
		sdformat->format.quantization = in_fmt->quantization;
		sdformat->format.ycbcr_enc = in_fmt->ycbcr_enc;
		break;

	case IMX7_CSI_PAD_SINK:
		*cc = imx7_csi_find_mbus_format(sdformat->format.code);
		if (!*cc) {
			code = IMX7_CSI_DEF_MBUS_CODE;
			*cc = imx7_csi_find_mbus_format(code);
			sdformat->format.code = code;
		}

		if (sdformat->format.field != V4L2_FIELD_INTERLACED)
			sdformat->format.field = V4L2_FIELD_NONE;
		break;
	}

	imx7_csi_try_colorimetry(&sdformat->format);
}

static int imx7_csi_set_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *sd_state,
			    struct v4l2_subdev_format *sdformat)
{
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);
	const struct imx7_csi_pixfmt *outcc;
	struct v4l2_mbus_framefmt *outfmt;
	const struct imx7_csi_pixfmt *cc;
	struct v4l2_mbus_framefmt *fmt;
	struct v4l2_subdev_format format;

	if (csi->is_streaming)
		return -EBUSY;

	imx7_csi_try_fmt(sd, sd_state, sdformat, &cc);

	fmt = v4l2_subdev_get_pad_format(sd, sd_state, sdformat->pad);

	*fmt = sdformat->format;

	if (sdformat->pad == IMX7_CSI_PAD_SINK) {
		/* propagate format to source pads */
		format.pad = IMX7_CSI_PAD_SRC;
		format.which = sdformat->which;
		format.format = sdformat->format;
		imx7_csi_try_fmt(sd, sd_state, &format, &outcc);

		outfmt = v4l2_subdev_get_pad_format(sd, sd_state,
						    IMX7_CSI_PAD_SRC);
		*outfmt = format.format;
	}

	return 0;
}

static int imx7_csi_pad_link_validate(struct v4l2_subdev *sd,
				      struct media_link *link,
				      struct v4l2_subdev_format *source_fmt,
				      struct v4l2_subdev_format *sink_fmt)
{
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);
	struct media_pad *pad = NULL;
	unsigned int i;
	int ret;

	/*
	 * Validate the source link, and record whether the source uses the
	 * parallel input or the CSI-2 receiver.
	 */
	ret = v4l2_subdev_link_validate_default(sd, link, source_fmt, sink_fmt);
	if (ret)
		return ret;

	switch (csi->src_sd->entity.function) {
	case MEDIA_ENT_F_VID_IF_BRIDGE:
		/* The input is the CSI-2 receiver. */
		csi->is_csi2 = true;
		break;

	case MEDIA_ENT_F_VID_MUX:
		/* The input is the mux, check its input. */
		for (i = 0; i < csi->src_sd->entity.num_pads; i++) {
			struct media_pad *spad = &csi->src_sd->entity.pads[i];

			if (!(spad->flags & MEDIA_PAD_FL_SINK))
				continue;

			pad = media_pad_remote_pad_first(spad);
			if (pad)
				break;
		}

		if (!pad)
			return -ENODEV;

		csi->is_csi2 = pad->entity->function == MEDIA_ENT_F_VID_IF_BRIDGE;
		break;

	default:
		/*
		 * The input is an external entity, it must use the parallel
		 * bus.
		 */
		csi->is_csi2 = false;
		break;
	}

	return 0;
}

static int imx7_csi_registered(struct v4l2_subdev *sd)
{
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);
	int ret;

	ret = imx7_csi_video_init(csi);
	if (ret)
		return ret;

	ret = imx7_csi_video_register(csi);
	if (ret)
		return ret;

	ret = v4l2_device_register_subdev_nodes(&csi->v4l2_dev);
	if (ret)
		goto err_unreg;

	ret = media_device_register(&csi->mdev);
	if (ret)
		goto err_unreg;

	return 0;

err_unreg:
	imx7_csi_video_unregister(csi);
	return ret;
}

static void imx7_csi_unregistered(struct v4l2_subdev *sd)
{
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);

	imx7_csi_video_unregister(csi);
}

static const struct v4l2_subdev_video_ops imx7_csi_video_ops = {
	.s_stream	= imx7_csi_s_stream,
};

static const struct v4l2_subdev_pad_ops imx7_csi_pad_ops = {
	.init_cfg	= imx7_csi_init_cfg,
	.enum_mbus_code	= imx7_csi_enum_mbus_code,
	.get_fmt	= v4l2_subdev_get_fmt,
	.set_fmt	= imx7_csi_set_fmt,
	.link_validate	= imx7_csi_pad_link_validate,
};

static const struct v4l2_subdev_ops imx7_csi_subdev_ops = {
	.video		= &imx7_csi_video_ops,
	.pad		= &imx7_csi_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx7_csi_internal_ops = {
	.registered	= imx7_csi_registered,
	.unregistered	= imx7_csi_unregistered,
};

/* -----------------------------------------------------------------------------
 * Media Entity Operations
 */

static const struct media_entity_operations imx7_csi_entity_ops = {
	.link_validate	= v4l2_subdev_link_validate,
	.get_fwnode_pad = v4l2_subdev_get_fwnode_pad_1_to_1,
};

/* -----------------------------------------------------------------------------
 * Probe & Remove
 */

static int imx7_csi_notify_bound(struct v4l2_async_notifier *notifier,
				 struct v4l2_subdev *sd,
				 struct v4l2_async_subdev *asd)
{
	struct imx7_csi *csi = imx7_csi_notifier_to_dev(notifier);
	struct media_pad *sink = &csi->sd.entity.pads[IMX7_CSI_PAD_SINK];

	csi->src_sd = sd;

	return v4l2_create_fwnode_links_to_pad(sd, sink, MEDIA_LNK_FL_ENABLED |
					       MEDIA_LNK_FL_IMMUTABLE);
}

static int imx7_csi_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct imx7_csi *csi = imx7_csi_notifier_to_dev(notifier);

	return v4l2_device_register_subdev_nodes(&csi->v4l2_dev);
}

static const struct v4l2_async_notifier_operations imx7_csi_notify_ops = {
	.bound = imx7_csi_notify_bound,
	.complete = imx7_csi_notify_complete,
};

static int imx7_csi_async_register(struct imx7_csi *csi)
{
	struct v4l2_async_subdev *asd;
	struct fwnode_handle *ep;
	int ret;

	v4l2_async_nf_init(&csi->notifier);

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(csi->dev), 0, 0,
					     FWNODE_GRAPH_ENDPOINT_NEXT);
	if (!ep) {
		ret = dev_err_probe(csi->dev, -ENOTCONN,
				    "Failed to get remote endpoint\n");
		goto error;
	}

	asd = v4l2_async_nf_add_fwnode_remote(&csi->notifier, ep,
					      struct v4l2_async_subdev);

	fwnode_handle_put(ep);

	if (IS_ERR(asd)) {
		ret = dev_err_probe(csi->dev, PTR_ERR(asd),
				    "Failed to add remote subdev to notifier\n");
		goto error;
	}

	csi->notifier.ops = &imx7_csi_notify_ops;

	ret = v4l2_async_nf_register(&csi->v4l2_dev, &csi->notifier);
	if (ret)
		goto error;

	return 0;

error:
	v4l2_async_nf_cleanup(&csi->notifier);
	return ret;
}

static void imx7_csi_media_cleanup(struct imx7_csi *csi)
{
	v4l2_device_unregister(&csi->v4l2_dev);
	media_device_unregister(&csi->mdev);
	v4l2_subdev_cleanup(&csi->sd);
	media_device_cleanup(&csi->mdev);
}

static const struct media_device_ops imx7_csi_media_ops = {
	.link_notify = v4l2_pipeline_link_notify,
};

static int imx7_csi_media_dev_init(struct imx7_csi *csi)
{
	int ret;

	strscpy(csi->mdev.model, "imx-media", sizeof(csi->mdev.model));
	csi->mdev.ops = &imx7_csi_media_ops;
	csi->mdev.dev = csi->dev;

	csi->v4l2_dev.mdev = &csi->mdev;
	strscpy(csi->v4l2_dev.name, "imx-media",
		sizeof(csi->v4l2_dev.name));
	snprintf(csi->mdev.bus_info, sizeof(csi->mdev.bus_info),
		 "platform:%s", dev_name(csi->mdev.dev));

	media_device_init(&csi->mdev);

	ret = v4l2_device_register(csi->dev, &csi->v4l2_dev);
	if (ret < 0) {
		v4l2_err(&csi->v4l2_dev,
			 "Failed to register v4l2_device: %d\n", ret);
		goto cleanup;
	}

	return 0;

cleanup:
	media_device_cleanup(&csi->mdev);

	return ret;
}

static int imx7_csi_media_init(struct imx7_csi *csi)
{
	unsigned int i;
	int ret;

	/* add media device */
	ret = imx7_csi_media_dev_init(csi);
	if (ret)
		return ret;

	v4l2_subdev_init(&csi->sd, &imx7_csi_subdev_ops);
	v4l2_set_subdevdata(&csi->sd, csi);
	csi->sd.internal_ops = &imx7_csi_internal_ops;
	csi->sd.entity.ops = &imx7_csi_entity_ops;
	csi->sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	csi->sd.dev = csi->dev;
	csi->sd.owner = THIS_MODULE;
	csi->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(csi->sd.name, sizeof(csi->sd.name), "csi");

	for (i = 0; i < IMX7_CSI_PADS_NUM; i++)
		csi->pad[i].flags = (i == IMX7_CSI_PAD_SINK) ?
			MEDIA_PAD_FL_SINK : MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&csi->sd.entity, IMX7_CSI_PADS_NUM,
				     csi->pad);
	if (ret)
		goto error;

	ret = v4l2_subdev_init_finalize(&csi->sd);
	if (ret)
		goto error;

	ret = v4l2_device_register_subdev(&csi->v4l2_dev, &csi->sd);
	if (ret)
		goto error;

	return 0;

error:
	imx7_csi_media_cleanup(csi);
	return ret;
}

static int imx7_csi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct imx7_csi *csi;
	int ret;

	csi = devm_kzalloc(&pdev->dev, sizeof(*csi), GFP_KERNEL);
	if (!csi)
		return -ENOMEM;

	csi->dev = dev;
	platform_set_drvdata(pdev, csi);

	spin_lock_init(&csi->irqlock);

	/* Acquire resources and install interrupt handler. */
	csi->mclk = devm_clk_get(&pdev->dev, "mclk");
	if (IS_ERR(csi->mclk)) {
		ret = PTR_ERR(csi->mclk);
		dev_err(dev, "Failed to get mclk: %d", ret);
		return ret;
	}

	csi->irq = platform_get_irq(pdev, 0);
	if (csi->irq < 0)
		return csi->irq;

	csi->regbase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(csi->regbase))
		return PTR_ERR(csi->regbase);

	csi->model = (enum imx_csi_model)(uintptr_t)of_device_get_match_data(&pdev->dev);

	ret = devm_request_irq(dev, csi->irq, imx7_csi_irq_handler, 0, "csi",
			       (void *)csi);
	if (ret < 0) {
		dev_err(dev, "Request CSI IRQ failed.\n");
		return ret;
	}

	/* Initialize all the media device infrastructure. */
	ret = imx7_csi_media_init(csi);
	if (ret)
		return ret;

	ret = imx7_csi_async_register(csi);
	if (ret)
		goto err_media_cleanup;

	return 0;

err_media_cleanup:
	imx7_csi_media_cleanup(csi);

	return ret;
}

static void imx7_csi_remove(struct platform_device *pdev)
{
	struct imx7_csi *csi = platform_get_drvdata(pdev);

	imx7_csi_media_cleanup(csi);

	v4l2_async_nf_unregister(&csi->notifier);
	v4l2_async_nf_cleanup(&csi->notifier);
	v4l2_async_unregister_subdev(&csi->sd);
}

static const struct of_device_id imx7_csi_of_match[] = {
	{ .compatible = "fsl,imx8mq-csi", .data = (void *)IMX7_CSI_IMX8MQ },
	{ .compatible = "fsl,imx7-csi", .data = (void *)IMX7_CSI_IMX7 },
	{ .compatible = "fsl,imx6ul-csi", .data = (void *)IMX7_CSI_IMX7 },
	{ },
};
MODULE_DEVICE_TABLE(of, imx7_csi_of_match);

static struct platform_driver imx7_csi_driver = {
	.probe = imx7_csi_probe,
	.remove_new = imx7_csi_remove,
	.driver = {
		.of_match_table = imx7_csi_of_match,
		.name = "imx7-csi",
	},
};
module_platform_driver(imx7_csi_driver);

MODULE_DESCRIPTION("i.MX7 CSI subdev driver");
MODULE_AUTHOR("Rui Miguel Silva <rui.silva@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:imx7-csi");
