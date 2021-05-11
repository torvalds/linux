// SPDX-License-Identifier: GPL-2.0
/*
 * V4L2 Capture CSI Subdev for Freescale i.MX6UL/L / i.MX7 SOC
 *
 * Copyright (c) 2019 Linaro Ltd
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gcd.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>

#include <media/imx.h>
#include "imx-media.h"

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

struct imx7_csi {
	struct device *dev;
	struct v4l2_subdev sd;
	struct v4l2_async_notifier notifier;
	struct imx_media_video_dev *vdev;
	struct imx_media_dev *imxmd;
	struct media_pad pad[IMX7_CSI_PADS_NUM];

	/* lock to protect members below */
	struct mutex lock;
	/* lock to protect irq handler when stop streaming */
	spinlock_t irqlock;

	struct v4l2_subdev *src_sd;

	struct v4l2_mbus_framefmt format_mbus[IMX7_CSI_PADS_NUM];
	const struct imx_media_pixfmt *cc[IMX7_CSI_PADS_NUM];
	struct v4l2_fract frame_interval[IMX7_CSI_PADS_NUM];

	void __iomem *regbase;
	int irq;
	struct clk *mclk;

	/* active vb2 buffers to send to video dev sink */
	struct imx_media_buffer *active_vb2_buf[2];
	struct imx_media_dma_buf underrun_buf;

	int buf_num;
	u32 frame_sequence;

	bool last_eof;
	bool is_streaming;
	bool is_csi2;

	struct completion last_eof_completion;
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

	imx7_csi_reg_write(csi, BIT_IMAGE_WIDTH(800) | BIT_IMAGE_HEIGHT(600),
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

static void imx7_csi_update_buf(struct imx7_csi *csi, dma_addr_t phys,
				int buf_num)
{
	if (buf_num == 1)
		imx7_csi_reg_write(csi, phys, CSI_CSIDMASA_FB2);
	else
		imx7_csi_reg_write(csi, phys, CSI_CSIDMASA_FB1);
}

static void imx7_csi_setup_vb2_buf(struct imx7_csi *csi)
{
	struct imx_media_video_dev *vdev = csi->vdev;
	struct imx_media_buffer *buf;
	struct vb2_buffer *vb2_buf;
	dma_addr_t phys[2];
	int i;

	for (i = 0; i < 2; i++) {
		buf = imx_media_capture_device_next_buf(vdev);
		if (buf) {
			csi->active_vb2_buf[i] = buf;
			vb2_buf = &buf->vbuf.vb2_buf;
			phys[i] = vb2_dma_contig_plane_dma_addr(vb2_buf, 0);
		} else {
			csi->active_vb2_buf[i] = NULL;
			phys[i] = csi->underrun_buf.phys;
		}

		imx7_csi_update_buf(csi, phys[i], i);
	}
}

static void imx7_csi_dma_unsetup_vb2_buf(struct imx7_csi *csi,
					 enum vb2_buffer_state return_status)
{
	struct imx_media_buffer *buf;
	int i;

	/* return any remaining active frames with return_status */
	for (i = 0; i < 2; i++) {
		buf = csi->active_vb2_buf[i];
		if (buf) {
			struct vb2_buffer *vb = &buf->vbuf.vb2_buf;

			vb->timestamp = ktime_get_ns();
			vb2_buffer_done(vb, return_status);
		}
	}
}

static int imx7_csi_dma_setup(struct imx7_csi *csi)
{
	struct imx_media_video_dev *vdev = csi->vdev;
	int ret;

	ret = imx_media_alloc_dma_buf(csi->dev, &csi->underrun_buf,
				      vdev->fmt.sizeimage);
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

static void imx7_csi_dma_cleanup(struct imx7_csi *csi)
{
	imx7_csi_dma_unsetup_vb2_buf(csi, VB2_BUF_STATE_ERROR);
	imx_media_free_dma_buf(csi->dev, &csi->underrun_buf);
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
	timeout_jiffies = msecs_to_jiffies(IMX_MEDIA_EOF_TIMEOUT);
	ret = wait_for_completion_timeout(&csi->last_eof_completion,
					  timeout_jiffies);
	if (ret == 0)
		v4l2_warn(&csi->sd, "wait last EOF timeout\n");

	imx7_csi_hw_disable_irq(csi);
}

static void imx7_csi_configure(struct imx7_csi *csi)
{
	struct imx_media_video_dev *vdev = csi->vdev;
	struct v4l2_pix_format *out_pix = &vdev->fmt;
	int width = out_pix->width;
	u32 stride = 0;
	u32 cr1, cr18;

	cr18 = imx7_csi_reg_read(csi, CSI_CSICR18);

	cr18 &= ~(BIT_CSI_HW_ENABLE | BIT_MIPI_DATA_FORMAT_MASK |
		  BIT_DATA_FROM_MIPI | BIT_BASEADDR_CHG_ERR_EN |
		  BIT_BASEADDR_SWITCH_EN | BIT_BASEADDR_SWITCH_SEL |
		  BIT_DEINTERLACE_EN);

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
		cr1 = BIT_SOF_POL | BIT_REDGE | BIT_HSYNC_POL | BIT_FCC
		    | BIT_MCLKDIV(1) | BIT_MCLKEN;

		cr18 |= BIT_DATA_FROM_MIPI;

		switch (csi->format_mbus[IMX7_CSI_PAD_SINK].code) {
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
			cr18 |= BIT_MIPI_DATA_FORMAT_RAW10;
			break;
		case MEDIA_BUS_FMT_Y12_1X12:
		case MEDIA_BUS_FMT_SBGGR12_1X12:
		case MEDIA_BUS_FMT_SGBRG12_1X12:
		case MEDIA_BUS_FMT_SGRBG12_1X12:
		case MEDIA_BUS_FMT_SRGGB12_1X12:
			cr18 |= BIT_MIPI_DATA_FORMAT_RAW12;
			break;
		case MEDIA_BUS_FMT_Y14_1X14:
		case MEDIA_BUS_FMT_SBGGR14_1X14:
		case MEDIA_BUS_FMT_SGBRG14_1X14:
		case MEDIA_BUS_FMT_SGRBG14_1X14:
		case MEDIA_BUS_FMT_SRGGB14_1X14:
			cr18 |= BIT_MIPI_DATA_FORMAT_RAW14;
			break;
		/*
		 * CSI-2 sources are supposed to use the 1X16 formats, but not
		 * all of them comply. Support both variants.
		 */
		case MEDIA_BUS_FMT_UYVY8_2X8:
		case MEDIA_BUS_FMT_UYVY8_1X16:
		case MEDIA_BUS_FMT_YUYV8_2X8:
		case MEDIA_BUS_FMT_YUYV8_1X16:
			cr18 |= BIT_MIPI_DATA_FORMAT_YUV422_8B;
			break;
		}

		switch (out_pix->pixelformat) {
		case V4L2_PIX_FMT_Y10:
		case V4L2_PIX_FMT_Y12:
		case V4L2_PIX_FMT_SBGGR8:
		case V4L2_PIX_FMT_SGBRG8:
		case V4L2_PIX_FMT_SGRBG8:
		case V4L2_PIX_FMT_SRGGB8:
		case V4L2_PIX_FMT_SBGGR16:
		case V4L2_PIX_FMT_SGBRG16:
		case V4L2_PIX_FMT_SGRBG16:
		case V4L2_PIX_FMT_SRGGB16:
			cr1 |= BIT_PIXEL_BIT;
			break;
		}
	}

	imx7_csi_reg_write(csi, cr1, CSI_CSICR1);
	imx7_csi_reg_write(csi, BIT_DMA_BURST_TYPE_RFF_INCR16, CSI_CSICR2);
	imx7_csi_reg_write(csi, BIT_FRMCNT_RST, CSI_CSICR3);
	imx7_csi_reg_write(csi, cr18, CSI_CSICR18);

	imx7_csi_reg_write(csi, (width * out_pix->height) >> 2, CSI_CSIRXCNT);
	imx7_csi_reg_write(csi, BIT_IMAGE_WIDTH(width) |
			   BIT_IMAGE_HEIGHT(out_pix->height),
			   CSI_CSIIMAG_PARA);
	imx7_csi_reg_write(csi, stride, CSI_CSIFBUF_PARA);
}

static int imx7_csi_init(struct imx7_csi *csi)
{
	int ret;

	ret = clk_prepare_enable(csi->mclk);
	if (ret < 0)
		return ret;

	imx7_csi_configure(csi);

	ret = imx7_csi_dma_setup(csi);
	if (ret < 0)
		return ret;

	return 0;
}

static void imx7_csi_deinit(struct imx7_csi *csi)
{
	imx7_csi_dma_cleanup(csi);
	imx7_csi_init_default(csi);
	imx7_csi_dmareq_rff_disable(csi);
	clk_disable_unprepare(csi->mclk);
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
	struct imx_media_video_dev *vdev = csi->vdev;
	struct imx_media_buffer *done, *next;
	struct vb2_buffer *vb;
	dma_addr_t phys;

	done = csi->active_vb2_buf[csi->buf_num];
	if (done) {
		done->vbuf.field = vdev->fmt.field;
		done->vbuf.sequence = csi->frame_sequence;
		vb = &done->vbuf.vb2_buf;
		vb->timestamp = ktime_get_ns();
		vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
	}
	csi->frame_sequence++;

	/* get next queued buffer */
	next = imx_media_capture_device_next_buf(vdev);
	if (next) {
		phys = vb2_dma_contig_plane_dma_addr(&next->vbuf.vb2_buf, 0);
		csi->active_vb2_buf[csi->buf_num] = next;
	} else {
		phys = csi->underrun_buf.phys;
		csi->active_vb2_buf[csi->buf_num] = NULL;
	}

	imx7_csi_update_buf(csi, phys, csi->buf_num);
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
 * V4L2 Subdev Operations
 */

static int imx7_csi_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&csi->lock);

	if (!csi->src_sd) {
		ret = -EPIPE;
		goto out_unlock;
	}

	if (csi->is_streaming == !!enable)
		goto out_unlock;

	if (enable) {
		ret = imx7_csi_init(csi);
		if (ret < 0)
			goto out_unlock;

		ret = v4l2_subdev_call(csi->src_sd, video, s_stream, 1);
		if (ret < 0) {
			imx7_csi_deinit(csi);
			goto out_unlock;
		}

		imx7_csi_enable(csi);
	} else {
		imx7_csi_disable(csi);

		v4l2_subdev_call(csi->src_sd, video, s_stream, 0);

		imx7_csi_deinit(csi);
	}

	csi->is_streaming = !!enable;

out_unlock:
	mutex_unlock(&csi->lock);

	return ret;
}

static int imx7_csi_init_cfg(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg)
{
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf;
	int ret;
	int i;

	for (i = 0; i < IMX7_CSI_PADS_NUM; i++) {
		mf = v4l2_subdev_get_try_format(sd, cfg, i);

		ret = imx_media_init_mbus_fmt(mf, 800, 600, 0, V4L2_FIELD_NONE,
					      &csi->cc[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static struct v4l2_mbus_framefmt *
imx7_csi_get_format(struct imx7_csi *csi,
		    struct v4l2_subdev_pad_config *cfg,
		    unsigned int pad,
		    enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&csi->sd, cfg, pad);

	return &csi->format_mbus[pad];
}

static int imx7_csi_enum_mbus_code(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *in_fmt;
	int ret = 0;

	mutex_lock(&csi->lock);

	in_fmt = imx7_csi_get_format(csi, cfg, IMX7_CSI_PAD_SINK, code->which);

	switch (code->pad) {
	case IMX7_CSI_PAD_SINK:
		ret = imx_media_enum_mbus_formats(&code->code, code->index,
						  PIXFMT_SEL_ANY);
		break;
	case IMX7_CSI_PAD_SRC:
		if (code->index != 0) {
			ret = -EINVAL;
			goto out_unlock;
		}

		code->code = in_fmt->code;
		break;
	default:
		ret = -EINVAL;
	}

out_unlock:
	mutex_unlock(&csi->lock);

	return ret;
}

static int imx7_csi_get_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_format *sdformat)
{
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *fmt;
	int ret = 0;

	mutex_lock(&csi->lock);

	fmt = imx7_csi_get_format(csi, cfg, sdformat->pad, sdformat->which);
	if (!fmt) {
		ret = -EINVAL;
		goto out_unlock;
	}

	sdformat->format = *fmt;

out_unlock:
	mutex_unlock(&csi->lock);

	return ret;
}

static int imx7_csi_try_fmt(struct imx7_csi *csi,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_format *sdformat,
			    const struct imx_media_pixfmt **cc)
{
	const struct imx_media_pixfmt *in_cc;
	struct v4l2_mbus_framefmt *in_fmt;
	u32 code;

	in_fmt = imx7_csi_get_format(csi, cfg, IMX7_CSI_PAD_SINK,
				     sdformat->which);
	if (!in_fmt)
		return -EINVAL;

	switch (sdformat->pad) {
	case IMX7_CSI_PAD_SRC:
		in_cc = imx_media_find_mbus_format(in_fmt->code,
						   PIXFMT_SEL_ANY);

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
		*cc = imx_media_find_mbus_format(sdformat->format.code,
						 PIXFMT_SEL_ANY);
		if (!*cc) {
			imx_media_enum_mbus_formats(&code, 0,
						    PIXFMT_SEL_YUV_RGB);
			*cc = imx_media_find_mbus_format(code,
							 PIXFMT_SEL_YUV_RGB);
			sdformat->format.code = (*cc)->codes[0];
		}

		if (sdformat->format.field != V4L2_FIELD_INTERLACED)
			sdformat->format.field = V4L2_FIELD_NONE;
		break;
	default:
		return -EINVAL;
	}

	imx_media_try_colorimetry(&sdformat->format, false);

	return 0;
}

static int imx7_csi_set_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_format *sdformat)
{
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);
	const struct imx_media_pixfmt *outcc;
	struct v4l2_mbus_framefmt *outfmt;
	const struct imx_media_pixfmt *cc;
	struct v4l2_mbus_framefmt *fmt;
	struct v4l2_subdev_format format;
	int ret = 0;

	if (sdformat->pad >= IMX7_CSI_PADS_NUM)
		return -EINVAL;

	mutex_lock(&csi->lock);

	if (csi->is_streaming) {
		ret = -EBUSY;
		goto out_unlock;
	}

	ret = imx7_csi_try_fmt(csi, cfg, sdformat, &cc);
	if (ret < 0)
		goto out_unlock;

	fmt = imx7_csi_get_format(csi, cfg, sdformat->pad, sdformat->which);
	if (!fmt) {
		ret = -EINVAL;
		goto out_unlock;
	}

	*fmt = sdformat->format;

	if (sdformat->pad == IMX7_CSI_PAD_SINK) {
		/* propagate format to source pads */
		format.pad = IMX7_CSI_PAD_SRC;
		format.which = sdformat->which;
		format.format = sdformat->format;
		if (imx7_csi_try_fmt(csi, cfg, &format, &outcc)) {
			ret = -EINVAL;
			goto out_unlock;
		}
		outfmt = imx7_csi_get_format(csi, cfg, IMX7_CSI_PAD_SRC,
					     sdformat->which);
		*outfmt = format.format;

		if (sdformat->which == V4L2_SUBDEV_FORMAT_ACTIVE)
			csi->cc[IMX7_CSI_PAD_SRC] = outcc;
	}

	if (sdformat->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		csi->cc[sdformat->pad] = cc;

out_unlock:
	mutex_unlock(&csi->lock);

	return ret;
}

static int imx7_csi_pad_link_validate(struct v4l2_subdev *sd,
				      struct media_link *link,
				      struct v4l2_subdev_format *source_fmt,
				      struct v4l2_subdev_format *sink_fmt)
{
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);
	struct imx_media_video_dev *vdev = csi->vdev;
	const struct v4l2_pix_format *out_pix = &vdev->fmt;
	struct media_pad *pad;
	int ret;

	if (!csi->src_sd)
		return -EPIPE;

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
		pad = imx_media_pipeline_pad(&csi->src_sd->entity, 0, 0, true);
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

	/* Validate the sink link, ensure the pixel format is supported. */
	switch (out_pix->pixelformat) {
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_GREY:
	case V4L2_PIX_FMT_Y10:
	case V4L2_PIX_FMT_Y12:
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SRGGB8:
	case V4L2_PIX_FMT_SBGGR16:
	case V4L2_PIX_FMT_SGBRG16:
	case V4L2_PIX_FMT_SGRBG16:
	case V4L2_PIX_FMT_SRGGB16:
		break;

	default:
		dev_dbg(csi->dev, "Invalid capture pixel format 0x%08x\n",
			out_pix->pixelformat);
		return -EINVAL;
	}

	return 0;
}

static int imx7_csi_registered(struct v4l2_subdev *sd)
{
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);
	int ret;
	int i;

	for (i = 0; i < IMX7_CSI_PADS_NUM; i++) {
		/* set a default mbus format  */
		ret = imx_media_init_mbus_fmt(&csi->format_mbus[i],
					      800, 600, 0, V4L2_FIELD_NONE,
					      &csi->cc[i]);
		if (ret < 0)
			return ret;

		/* init default frame interval */
		csi->frame_interval[i].numerator = 1;
		csi->frame_interval[i].denominator = 30;
	}

	csi->vdev = imx_media_capture_device_init(csi->sd.dev, &csi->sd,
						  IMX7_CSI_PAD_SRC, false);
	if (IS_ERR(csi->vdev))
		return PTR_ERR(csi->vdev);

	ret = imx_media_capture_device_register(csi->vdev,
						MEDIA_LNK_FL_IMMUTABLE);
	if (ret)
		imx_media_capture_device_remove(csi->vdev);

	return ret;
}

static void imx7_csi_unregistered(struct v4l2_subdev *sd)
{
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);

	imx_media_capture_device_unregister(csi->vdev);
	imx_media_capture_device_remove(csi->vdev);
}

static const struct v4l2_subdev_video_ops imx7_csi_video_ops = {
	.s_stream	= imx7_csi_s_stream,
};

static const struct v4l2_subdev_pad_ops imx7_csi_pad_ops = {
	.init_cfg	= imx7_csi_init_cfg,
	.enum_mbus_code	= imx7_csi_enum_mbus_code,
	.get_fmt	= imx7_csi_get_fmt,
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

	/*
	 * If the subdev is a video mux, it must be one of the CSI
	 * muxes. Mark it as such via its group id.
	 */
	if (sd->entity.function == MEDIA_ENT_F_VID_MUX)
		sd->grp_id = IMX_MEDIA_GRP_ID_CSI_MUX;

	csi->src_sd = sd;

	return v4l2_create_fwnode_links_to_pad(sd, sink, MEDIA_LNK_FL_ENABLED |
					       MEDIA_LNK_FL_IMMUTABLE);
}

static const struct v4l2_async_notifier_operations imx7_csi_notify_ops = {
	.bound = imx7_csi_notify_bound,
};

static int imx7_csi_async_register(struct imx7_csi *csi)
{
	struct v4l2_async_subdev *asd;
	struct fwnode_handle *ep;
	int ret;

	v4l2_async_notifier_init(&csi->notifier);

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(csi->dev), 0, 0,
					     FWNODE_GRAPH_ENDPOINT_NEXT);
	if (ep) {
		asd = v4l2_async_notifier_add_fwnode_remote_subdev(
			&csi->notifier, ep, struct v4l2_async_subdev);

		fwnode_handle_put(ep);

		if (IS_ERR(asd)) {
			ret = PTR_ERR(asd);
			/* OK if asd already exists */
			if (ret != -EEXIST)
				return ret;
		}
	}

	csi->notifier.ops = &imx7_csi_notify_ops;

	ret = v4l2_async_subdev_notifier_register(&csi->sd, &csi->notifier);
	if (ret)
		return ret;

	return v4l2_async_register_subdev(&csi->sd);
}

static int imx7_csi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct imx_media_dev *imxmd;
	struct imx7_csi *csi;
	int i, ret;

	csi = devm_kzalloc(&pdev->dev, sizeof(*csi), GFP_KERNEL);
	if (!csi)
		return -ENOMEM;

	csi->dev = dev;

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

	spin_lock_init(&csi->irqlock);
	mutex_init(&csi->lock);

	/* install interrupt handler */
	ret = devm_request_irq(dev, csi->irq, imx7_csi_irq_handler, 0, "csi",
			       (void *)csi);
	if (ret < 0) {
		dev_err(dev, "Request CSI IRQ failed.\n");
		goto destroy_mutex;
	}

	/* add media device */
	imxmd = imx_media_dev_init(dev, NULL);
	if (IS_ERR(imxmd)) {
		ret = PTR_ERR(imxmd);
		goto destroy_mutex;
	}
	platform_set_drvdata(pdev, &csi->sd);

	ret = imx_media_of_add_csi(imxmd, node);
	if (ret < 0 && ret != -ENODEV && ret != -EEXIST)
		goto cleanup;

	ret = imx_media_dev_notifier_register(imxmd, NULL);
	if (ret < 0)
		goto cleanup;

	csi->imxmd = imxmd;
	v4l2_subdev_init(&csi->sd, &imx7_csi_subdev_ops);
	v4l2_set_subdevdata(&csi->sd, csi);
	csi->sd.internal_ops = &imx7_csi_internal_ops;
	csi->sd.entity.ops = &imx7_csi_entity_ops;
	csi->sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	csi->sd.dev = &pdev->dev;
	csi->sd.owner = THIS_MODULE;
	csi->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;
	csi->sd.grp_id = IMX_MEDIA_GRP_ID_CSI;
	snprintf(csi->sd.name, sizeof(csi->sd.name), "csi");

	for (i = 0; i < IMX7_CSI_PADS_NUM; i++)
		csi->pad[i].flags = (i == IMX7_CSI_PAD_SINK) ?
			MEDIA_PAD_FL_SINK : MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&csi->sd.entity, IMX7_CSI_PADS_NUM,
				     csi->pad);
	if (ret < 0)
		goto cleanup;

	ret = imx7_csi_async_register(csi);
	if (ret)
		goto subdev_notifier_cleanup;

	return 0;

subdev_notifier_cleanup:
	v4l2_async_notifier_unregister(&csi->notifier);
	v4l2_async_notifier_cleanup(&csi->notifier);

cleanup:
	v4l2_async_notifier_unregister(&imxmd->notifier);
	v4l2_async_notifier_cleanup(&imxmd->notifier);
	v4l2_device_unregister(&imxmd->v4l2_dev);
	media_device_unregister(&imxmd->md);
	media_device_cleanup(&imxmd->md);

destroy_mutex:
	mutex_destroy(&csi->lock);

	return ret;
}

static int imx7_csi_remove(struct platform_device *pdev)
{
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);
	struct imx_media_dev *imxmd = csi->imxmd;

	v4l2_async_notifier_unregister(&imxmd->notifier);
	v4l2_async_notifier_cleanup(&imxmd->notifier);

	media_device_unregister(&imxmd->md);
	v4l2_device_unregister(&imxmd->v4l2_dev);
	media_device_cleanup(&imxmd->md);

	v4l2_async_notifier_unregister(&csi->notifier);
	v4l2_async_notifier_cleanup(&csi->notifier);
	v4l2_async_unregister_subdev(sd);

	mutex_destroy(&csi->lock);

	return 0;
}

static const struct of_device_id imx7_csi_of_match[] = {
	{ .compatible = "fsl,imx7-csi" },
	{ .compatible = "fsl,imx6ul-csi" },
	{ },
};
MODULE_DEVICE_TABLE(of, imx7_csi_of_match);

static struct platform_driver imx7_csi_driver = {
	.probe = imx7_csi_probe,
	.remove = imx7_csi_remove,
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
