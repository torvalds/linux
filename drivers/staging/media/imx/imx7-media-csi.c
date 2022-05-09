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
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ioctl.h>
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

enum imx_csi_model {
	IMX7_CSI_IMX7 = 0,
	IMX7_CSI_IMX8MQ,
};

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

static struct imx_media_buffer *
imx7_media_capture_device_next_buf(struct imx_media_video_dev *vdev);

static void imx7_csi_setup_vb2_buf(struct imx7_csi *csi)
{
	struct imx_media_video_dev *vdev = csi->vdev;
	struct imx_media_buffer *buf;
	struct vb2_buffer *vb2_buf;
	dma_addr_t phys[2];
	int i;

	for (i = 0; i < 2; i++) {
		buf = imx7_media_capture_device_next_buf(vdev);
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
			csi->active_vb2_buf[i] = NULL;
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

static void imx7_csi_dma_cleanup(struct imx7_csi *csi,
				 enum vb2_buffer_state return_status)
{
	imx7_csi_dma_unsetup_vb2_buf(csi, return_status);
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
	u32 cr3 = BIT_FRMCNT_RST;
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
	next = imx7_media_capture_device_next_buf(vdev);
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
 * Video Capture Device
 */

#define IMX_CAPTURE_NAME "imx-capture"

struct capture_priv {
	struct imx_media_dev *md;		/* Media device */
	struct device *dev;			/* Physical device */

	struct imx_media_video_dev vdev;	/* Video device */
	struct media_pad vdev_pad;		/* Video device pad */

	struct v4l2_subdev *src_sd;		/* Source subdev */
	int src_sd_pad;				/* Source subdev pad */

	struct mutex mutex;			/* Protect vdev operations */

	struct vb2_queue q;			/* The videobuf2 queue */
	struct list_head ready_q;		/* List of queued buffers */
	spinlock_t q_lock;			/* Protect ready_q */

	struct v4l2_ctrl_handler ctrl_hdlr;	/* Controls inherited from subdevs */

	bool legacy_api;			/* Use the legacy (pre-MC) API */
};

#define to_capture_priv(v) container_of(v, struct capture_priv, vdev)

/* In bytes, per queue */
#define VID_MEM_LIMIT	SZ_64M

/* -----------------------------------------------------------------------------
 * Video Capture Device - IOCTLs
 */

static const struct imx_media_pixfmt *capture_find_format(u32 code, u32 fourcc)
{
	const struct imx_media_pixfmt *cc;

	cc = imx_media_find_ipu_format(code, PIXFMT_SEL_YUV_RGB);
	if (cc) {
		enum imx_pixfmt_sel fmt_sel = cc->cs == IPUV3_COLORSPACE_YUV
					    ? PIXFMT_SEL_YUV : PIXFMT_SEL_RGB;

		cc = imx_media_find_pixel_format(fourcc, fmt_sel);
		if (!cc) {
			imx_media_enum_pixel_formats(&fourcc, 0, fmt_sel, 0);
			cc = imx_media_find_pixel_format(fourcc, fmt_sel);
		}

		return cc;
	}

	return imx_media_find_mbus_format(code, PIXFMT_SEL_ANY);
}

static int capture_querycap(struct file *file, void *fh,
			    struct v4l2_capability *cap)
{
	struct capture_priv *priv = video_drvdata(file);

	strscpy(cap->driver, IMX_CAPTURE_NAME, sizeof(cap->driver));
	strscpy(cap->card, IMX_CAPTURE_NAME, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", dev_name(priv->dev));

	return 0;
}

static int capture_enum_fmt_vid_cap(struct file *file, void *fh,
				    struct v4l2_fmtdesc *f)
{
	return imx_media_enum_pixel_formats(&f->pixelformat, f->index,
					    PIXFMT_SEL_ANY, f->mbus_code);
}

static int capture_enum_framesizes(struct file *file, void *fh,
				   struct v4l2_frmsizeenum *fsize)
{
	const struct imx_media_pixfmt *cc;

	if (fsize->index > 0)
		return -EINVAL;

	cc = imx_media_find_pixel_format(fsize->pixel_format, PIXFMT_SEL_ANY);
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

static int capture_g_fmt_vid_cap(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct capture_priv *priv = video_drvdata(file);

	f->fmt.pix = priv->vdev.fmt;

	return 0;
}

static const struct imx_media_pixfmt *
__capture_try_fmt(struct v4l2_pix_format *pixfmt, struct v4l2_rect *compose)
{
	struct v4l2_mbus_framefmt fmt_src;
	const struct imx_media_pixfmt *cc;

	/*
	 * Find the pixel format, default to the first supported format if not
	 * found.
	 */
	cc = imx_media_find_pixel_format(pixfmt->pixelformat, PIXFMT_SEL_ANY);
	if (!cc) {
		imx_media_enum_pixel_formats(&pixfmt->pixelformat, 0,
					     PIXFMT_SEL_ANY, 0);
		cc = imx_media_find_pixel_format(pixfmt->pixelformat,
						 PIXFMT_SEL_ANY);
	}

	/* Allow IDMAC interweave but enforce field order from source. */
	if (V4L2_FIELD_IS_INTERLACED(pixfmt->field)) {
		switch (pixfmt->field) {
		case V4L2_FIELD_SEQ_TB:
			pixfmt->field = V4L2_FIELD_INTERLACED_TB;
			break;
		case V4L2_FIELD_SEQ_BT:
			pixfmt->field = V4L2_FIELD_INTERLACED_BT;
			break;
		default:
			break;
		}
	}

	v4l2_fill_mbus_format(&fmt_src, pixfmt, 0);
	imx_media_mbus_fmt_to_pix_fmt(pixfmt, &fmt_src, cc);

	if (compose) {
		compose->width = fmt_src.width;
		compose->height = fmt_src.height;
	}

	return cc;
}

static int capture_try_fmt_vid_cap(struct file *file, void *fh,
				   struct v4l2_format *f)
{
	__capture_try_fmt(&f->fmt.pix, NULL);
	return 0;
}

static int capture_s_fmt_vid_cap(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct capture_priv *priv = video_drvdata(file);
	const struct imx_media_pixfmt *cc;

	if (vb2_is_busy(&priv->q)) {
		dev_err(priv->dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	cc = __capture_try_fmt(&f->fmt.pix, &priv->vdev.compose);

	priv->vdev.cc = cc;
	priv->vdev.fmt = f->fmt.pix;

	return 0;
}

static int capture_g_selection(struct file *file, void *fh,
			       struct v4l2_selection *s)
{
	struct capture_priv *priv = video_drvdata(file);

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		/* The compose rectangle is fixed to the source format. */
		s->r = priv->vdev.compose;
		break;
	case V4L2_SEL_TGT_COMPOSE_PADDED:
		/*
		 * The hardware writes with a configurable but fixed DMA burst
		 * size. If the source format width is not burst size aligned,
		 * the written frame contains padding to the right.
		 */
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = priv->vdev.fmt.width;
		s->r.height = priv->vdev.fmt.height;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int capture_subscribe_event(struct v4l2_fh *fh,
				   const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_IMX_FRAME_INTERVAL_ERROR:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	default:
		return -EINVAL;
	}
}

static const struct v4l2_ioctl_ops capture_ioctl_ops = {
	.vidioc_querycap		= capture_querycap,

	.vidioc_enum_fmt_vid_cap	= capture_enum_fmt_vid_cap,
	.vidioc_enum_framesizes		= capture_enum_framesizes,

	.vidioc_g_fmt_vid_cap		= capture_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= capture_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= capture_s_fmt_vid_cap,

	.vidioc_g_selection		= capture_g_selection,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,

	.vidioc_subscribe_event		= capture_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

/* -----------------------------------------------------------------------------
 * Video Capture Device - Queue Operations
 */

static int capture_queue_setup(struct vb2_queue *vq,
			       unsigned int *nbuffers,
			       unsigned int *nplanes,
			       unsigned int sizes[],
			       struct device *alloc_devs[])
{
	struct capture_priv *priv = vb2_get_drv_priv(vq);
	struct v4l2_pix_format *pix = &priv->vdev.fmt;
	unsigned int count = *nbuffers;

	if (vq->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (*nplanes) {
		if (*nplanes != 1 || sizes[0] < pix->sizeimage)
			return -EINVAL;
		count += vq->num_buffers;
	}

	count = min_t(__u32, VID_MEM_LIMIT / pix->sizeimage, count);

	if (*nplanes)
		*nbuffers = (count < vq->num_buffers) ? 0 :
			count - vq->num_buffers;
	else
		*nbuffers = count;

	*nplanes = 1;
	sizes[0] = pix->sizeimage;

	return 0;
}

static int capture_buf_init(struct vb2_buffer *vb)
{
	struct imx_media_buffer *buf = to_imx_media_vb(vb);

	INIT_LIST_HEAD(&buf->list);

	return 0;
}

static int capture_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct capture_priv *priv = vb2_get_drv_priv(vq);
	struct v4l2_pix_format *pix = &priv->vdev.fmt;

	if (vb2_plane_size(vb, 0) < pix->sizeimage) {
		dev_err(priv->dev,
			"data will not fit into plane (%lu < %lu)\n",
			vb2_plane_size(vb, 0), (long)pix->sizeimage);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, pix->sizeimage);

	return 0;
}

static void capture_buf_queue(struct vb2_buffer *vb)
{
	struct capture_priv *priv = vb2_get_drv_priv(vb->vb2_queue);
	struct imx_media_buffer *buf = to_imx_media_vb(vb);
	unsigned long flags;

	spin_lock_irqsave(&priv->q_lock, flags);

	list_add_tail(&buf->list, &priv->ready_q);

	spin_unlock_irqrestore(&priv->q_lock, flags);
}

static int capture_validate_fmt(struct capture_priv *priv)
{
	struct v4l2_subdev_format fmt_src;
	const struct imx_media_pixfmt *cc;
	int ret;

	/* Retrieve the media bus format on the source subdev. */
	fmt_src.pad = priv->src_sd_pad;
	fmt_src.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	ret = v4l2_subdev_call(priv->src_sd, pad, get_fmt, NULL, &fmt_src);
	if (ret)
		return ret;

	/*
	 * Verify that the media bus size matches the size set on the video
	 * node. It is sufficient to check the compose rectangle size without
	 * checking the rounded size from vdev.fmt, as the rounded size is
	 * derived directly from the compose rectangle size, and will thus
	 * always match if the compose rectangle matches.
	 */
	if (priv->vdev.compose.width != fmt_src.format.width ||
	    priv->vdev.compose.height != fmt_src.format.height)
		return -EPIPE;

	/*
	 * Verify that the media bus code is compatible with the pixel format
	 * set on the video node.
	 */
	cc = capture_find_format(fmt_src.format.code, 0);
	if (!cc || priv->vdev.cc->cs != cc->cs)
		return -EPIPE;

	return 0;
}

static int capture_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct capture_priv *priv = vb2_get_drv_priv(vq);
	struct imx_media_buffer *buf, *tmp;
	unsigned long flags;
	int ret;

	ret = capture_validate_fmt(priv);
	if (ret) {
		dev_err(priv->dev, "capture format not valid\n");
		goto return_bufs;
	}

	ret = imx_media_pipeline_set_stream(priv->md, &priv->src_sd->entity,
					    true);
	if (ret) {
		dev_err(priv->dev, "pipeline start failed with %d\n", ret);
		goto return_bufs;
	}

	return 0;

return_bufs:
	spin_lock_irqsave(&priv->q_lock, flags);
	list_for_each_entry_safe(buf, tmp, &priv->ready_q, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vbuf.vb2_buf, VB2_BUF_STATE_QUEUED);
	}
	spin_unlock_irqrestore(&priv->q_lock, flags);
	return ret;
}

static void capture_stop_streaming(struct vb2_queue *vq)
{
	struct capture_priv *priv = vb2_get_drv_priv(vq);
	struct imx_media_buffer *frame;
	struct imx_media_buffer *tmp;
	unsigned long flags;
	int ret;

	ret = imx_media_pipeline_set_stream(priv->md, &priv->src_sd->entity,
					    false);
	if (ret)
		dev_warn(priv->dev, "pipeline stop failed with %d\n", ret);

	/* release all active buffers */
	spin_lock_irqsave(&priv->q_lock, flags);
	list_for_each_entry_safe(frame, tmp, &priv->ready_q, list) {
		list_del(&frame->list);
		vb2_buffer_done(&frame->vbuf.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&priv->q_lock, flags);
}

static const struct vb2_ops capture_qops = {
	.queue_setup	 = capture_queue_setup,
	.buf_init        = capture_buf_init,
	.buf_prepare	 = capture_buf_prepare,
	.buf_queue	 = capture_buf_queue,
	.wait_prepare	 = vb2_ops_wait_prepare,
	.wait_finish	 = vb2_ops_wait_finish,
	.start_streaming = capture_start_streaming,
	.stop_streaming  = capture_stop_streaming,
};

/* -----------------------------------------------------------------------------
 * Video Capture Device - File Operations
 */

static int capture_open(struct file *file)
{
	struct capture_priv *priv = video_drvdata(file);
	struct video_device *vfd = priv->vdev.vfd;
	int ret;

	if (mutex_lock_interruptible(&priv->mutex))
		return -ERESTARTSYS;

	ret = v4l2_fh_open(file);
	if (ret) {
		dev_err(priv->dev, "v4l2_fh_open failed\n");
		goto out;
	}

	ret = v4l2_pipeline_pm_get(&vfd->entity);
	if (ret)
		v4l2_fh_release(file);

out:
	mutex_unlock(&priv->mutex);
	return ret;
}

static int capture_release(struct file *file)
{
	struct capture_priv *priv = video_drvdata(file);
	struct video_device *vfd = priv->vdev.vfd;
	struct vb2_queue *vq = &priv->q;

	mutex_lock(&priv->mutex);

	if (file->private_data == vq->owner) {
		vb2_queue_release(vq);
		vq->owner = NULL;
	}

	v4l2_pipeline_pm_put(&vfd->entity);

	v4l2_fh_release(file);
	mutex_unlock(&priv->mutex);
	return 0;
}

static const struct v4l2_file_operations capture_fops = {
	.owner		= THIS_MODULE,
	.open		= capture_open,
	.release	= capture_release,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= vb2_fop_mmap,
};

/* -----------------------------------------------------------------------------
 * Video Capture Device - Init & Cleanup
 */

static struct imx_media_buffer *
imx7_media_capture_device_next_buf(struct imx_media_video_dev *vdev)
{
	struct capture_priv *priv = to_capture_priv(vdev);
	struct imx_media_buffer *buf = NULL;
	unsigned long flags;

	spin_lock_irqsave(&priv->q_lock, flags);

	/* get next queued buffer */
	if (!list_empty(&priv->ready_q)) {
		buf = list_entry(priv->ready_q.next, struct imx_media_buffer,
				 list);
		list_del(&buf->list);
	}

	spin_unlock_irqrestore(&priv->q_lock, flags);

	return buf;
}

static int capture_init_format(struct capture_priv *priv)
{
	struct v4l2_subdev_format fmt_src = {
		.pad = priv->src_sd_pad,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	struct imx_media_video_dev *vdev = &priv->vdev;
	int ret;

	if (priv->legacy_api) {
		ret = v4l2_subdev_call(priv->src_sd, pad, get_fmt, NULL,
				       &fmt_src);
		if (ret) {
			dev_err(priv->dev, "failed to get source format\n");
			return ret;
		}
	} else {
		fmt_src.format.code = MEDIA_BUS_FMT_UYVY8_2X8;
		fmt_src.format.width = IMX_MEDIA_DEF_PIX_WIDTH;
		fmt_src.format.height = IMX_MEDIA_DEF_PIX_HEIGHT;
	}

	imx_media_mbus_fmt_to_pix_fmt(&vdev->fmt, &fmt_src.format, NULL);
	vdev->compose.width = fmt_src.format.width;
	vdev->compose.height = fmt_src.format.height;

	vdev->cc = imx_media_find_pixel_format(vdev->fmt.pixelformat,
					       PIXFMT_SEL_ANY);

	return 0;
}

static int imx7_media_capture_device_register(struct imx_media_video_dev *vdev,
					      u32 link_flags)
{
	struct capture_priv *priv = to_capture_priv(vdev);
	struct v4l2_subdev *sd = priv->src_sd;
	struct v4l2_device *v4l2_dev = sd->v4l2_dev;
	struct video_device *vfd = vdev->vfd;
	int ret;

	/* get media device */
	priv->md = container_of(v4l2_dev->mdev, struct imx_media_dev, md);

	vfd->v4l2_dev = v4l2_dev;

	/* Initialize the default format and compose rectangle. */
	ret = capture_init_format(priv);
	if (ret < 0)
		return ret;

	/* Register the video device. */
	ret = video_register_device(vfd, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(priv->dev, "Failed to register video device\n");
		return ret;
	}

	dev_info(priv->dev, "Registered %s as /dev/%s\n", vfd->name,
		 video_device_node_name(vfd));

	/* Create the link from the src_sd devnode pad to device node. */
	if (link_flags & MEDIA_LNK_FL_IMMUTABLE)
		link_flags |= MEDIA_LNK_FL_ENABLED;
	ret = media_create_pad_link(&sd->entity, priv->src_sd_pad,
				    &vfd->entity, 0, link_flags);
	if (ret) {
		dev_err(priv->dev, "failed to create link to device node\n");
		video_unregister_device(vfd);
		return ret;
	}

	/* Add vdev to the video devices list. */
	imx_media_add_video_device(priv->md, vdev);

	return 0;
}

static void imx7_media_capture_device_unregister(struct imx_media_video_dev *vdev)
{
	struct capture_priv *priv = to_capture_priv(vdev);
	struct video_device *vfd = priv->vdev.vfd;

	media_entity_cleanup(&vfd->entity);
	video_unregister_device(vfd);
}

static struct imx_media_video_dev *
imx7_media_capture_device_init(struct device *dev, struct v4l2_subdev *src_sd,
			       int pad, bool legacy_api)
{
	struct capture_priv *priv;
	struct video_device *vfd;
	struct vb2_queue *vq;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	priv->src_sd = src_sd;
	priv->src_sd_pad = pad;
	priv->dev = dev;
	priv->legacy_api = legacy_api;

	mutex_init(&priv->mutex);
	INIT_LIST_HEAD(&priv->ready_q);
	spin_lock_init(&priv->q_lock);

	/* Allocate and initialize the video device. */
	vfd = video_device_alloc();
	if (!vfd)
		return ERR_PTR(-ENOMEM);

	vfd->fops = &capture_fops;
	vfd->ioctl_ops = &capture_ioctl_ops;
	vfd->minor = -1;
	vfd->release = video_device_release;
	vfd->vfl_dir = VFL_DIR_RX;
	vfd->tvnorms = V4L2_STD_NTSC | V4L2_STD_PAL | V4L2_STD_SECAM;
	vfd->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING
			 | (!legacy_api ? V4L2_CAP_IO_MC : 0);
	vfd->lock = &priv->mutex;
	vfd->queue = &priv->q;

	snprintf(vfd->name, sizeof(vfd->name), "%s capture", src_sd->name);

	video_set_drvdata(vfd, priv);
	priv->vdev.vfd = vfd;
	INIT_LIST_HEAD(&priv->vdev.list);

	/* Initialize the video device pad. */
	priv->vdev_pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vfd->entity, 1, &priv->vdev_pad);
	if (ret) {
		video_device_release(vfd);
		return ERR_PTR(ret);
	}

	/* Initialize the vb2 queue. */
	vq = &priv->q;
	vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vq->io_modes = VB2_MMAP | VB2_DMABUF;
	vq->drv_priv = priv;
	vq->buf_struct_size = sizeof(struct imx_media_buffer);
	vq->ops = &capture_qops;
	vq->mem_ops = &vb2_dma_contig_memops;
	vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	vq->lock = &priv->mutex;
	vq->min_buffers_needed = 2;
	vq->dev = priv->dev;

	ret = vb2_queue_init(vq);
	if (ret) {
		dev_err(priv->dev, "vb2_queue_init failed\n");
		video_device_release(vfd);
		return ERR_PTR(ret);
	}

	if (legacy_api) {
		/* Initialize the control handler. */
		v4l2_ctrl_handler_init(&priv->ctrl_hdlr, 0);
		vfd->ctrl_handler = &priv->ctrl_hdlr;
	}

	return &priv->vdev;
}

static void imx7_media_capture_device_remove(struct imx_media_video_dev *vdev)
{
	struct capture_priv *priv = to_capture_priv(vdev);

	v4l2_ctrl_handler_free(&priv->ctrl_hdlr);
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
	mutex_unlock(&csi->lock);

	return ret;
}

static struct v4l2_mbus_framefmt *
imx7_csi_get_format(struct imx7_csi *csi,
		    struct v4l2_subdev_state *sd_state,
		    unsigned int pad,
		    enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&csi->sd, sd_state, pad);

	return &csi->format_mbus[pad];
}

static int imx7_csi_init_cfg(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state)
{
	const enum v4l2_subdev_format_whence which =
		sd_state ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);
	int ret;
	int i;

	for (i = 0; i < IMX7_CSI_PADS_NUM; i++) {
		struct v4l2_mbus_framefmt *mf =
			imx7_csi_get_format(csi, sd_state, i, which);

		ret = imx_media_init_mbus_fmt(mf, 800, 600, 0, V4L2_FIELD_NONE,
					      &csi->cc[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int imx7_csi_enum_mbus_code(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *in_fmt;
	int ret = 0;

	mutex_lock(&csi->lock);

	in_fmt = imx7_csi_get_format(csi, sd_state, IMX7_CSI_PAD_SINK,
				     code->which);

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
			    struct v4l2_subdev_state *sd_state,
			    struct v4l2_subdev_format *sdformat)
{
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *fmt;
	int ret = 0;

	mutex_lock(&csi->lock);

	fmt = imx7_csi_get_format(csi, sd_state, sdformat->pad,
				  sdformat->which);
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
			    struct v4l2_subdev_state *sd_state,
			    struct v4l2_subdev_format *sdformat,
			    const struct imx_media_pixfmt **cc)
{
	const struct imx_media_pixfmt *in_cc;
	struct v4l2_mbus_framefmt *in_fmt;
	u32 code;

	in_fmt = imx7_csi_get_format(csi, sd_state, IMX7_CSI_PAD_SINK,
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
			    struct v4l2_subdev_state *sd_state,
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

	ret = imx7_csi_try_fmt(csi, sd_state, sdformat, &cc);
	if (ret < 0)
		goto out_unlock;

	fmt = imx7_csi_get_format(csi, sd_state, sdformat->pad,
				  sdformat->which);
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
		if (imx7_csi_try_fmt(csi, sd_state, &format, &outcc)) {
			ret = -EINVAL;
			goto out_unlock;
		}
		outfmt = imx7_csi_get_format(csi, sd_state, IMX7_CSI_PAD_SRC,
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

	csi->vdev = imx7_media_capture_device_init(csi->sd.dev, &csi->sd,
						   IMX7_CSI_PAD_SRC, false);
	if (IS_ERR(csi->vdev))
		return PTR_ERR(csi->vdev);

	ret = imx7_media_capture_device_register(csi->vdev,
						 MEDIA_LNK_FL_IMMUTABLE);
	if (ret)
		goto err_remove;

	ret = v4l2_device_register_subdev_nodes(&csi->imxmd->v4l2_dev);
	if (ret)
		goto err_unreg;

	ret = media_device_register(&csi->imxmd->md);
	if (ret)
		goto err_unreg;

	return 0;

err_unreg:
	imx7_media_capture_device_unregister(csi->vdev);
err_remove:
	imx7_media_capture_device_remove(csi->vdev);
	return ret;
}

static void imx7_csi_unregistered(struct v4l2_subdev *sd)
{
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);

	imx7_media_capture_device_unregister(csi->vdev);
	imx7_media_capture_device_remove(csi->vdev);
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

static int imx7_csi_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct imx7_csi *csi = imx7_csi_notifier_to_dev(notifier);

	return v4l2_device_register_subdev_nodes(&csi->imxmd->v4l2_dev);
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
	if (ep) {
		asd = v4l2_async_nf_add_fwnode_remote(&csi->notifier, ep,
						      struct v4l2_async_subdev);

		fwnode_handle_put(ep);

		if (IS_ERR(asd)) {
			ret = PTR_ERR(asd);
			/* OK if asd already exists */
			if (ret != -EEXIST)
				return ret;
		}
	}

	csi->notifier.ops = &imx7_csi_notify_ops;

	ret = v4l2_async_nf_register(&csi->imxmd->v4l2_dev, &csi->notifier);
	if (ret)
		return ret;

	return 0;
}

static void imx7_csi_media_cleanup(struct imx7_csi *csi)
{
	struct imx_media_dev *imxmd = csi->imxmd;

	v4l2_device_unregister(&imxmd->v4l2_dev);
	media_device_unregister(&imxmd->md);
	media_device_cleanup(&imxmd->md);
}

static int imx7_csi_media_init(struct imx7_csi *csi)
{
	struct imx_media_dev *imxmd;
	unsigned int i;
	int ret;

	/* add media device */
	imxmd = imx_media_dev_init(csi->dev, NULL);
	if (IS_ERR(imxmd))
		return PTR_ERR(imxmd);

	csi->imxmd = imxmd;

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

	ret = v4l2_device_register_subdev(&csi->imxmd->v4l2_dev, &csi->sd);
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
	platform_set_drvdata(pdev, &csi->sd);

	spin_lock_init(&csi->irqlock);
	mutex_init(&csi->lock);

	/* Acquire resources and install interrupt handler. */
	csi->mclk = devm_clk_get(&pdev->dev, "mclk");
	if (IS_ERR(csi->mclk)) {
		ret = PTR_ERR(csi->mclk);
		dev_err(dev, "Failed to get mclk: %d", ret);
		goto destroy_mutex;
	}

	csi->irq = platform_get_irq(pdev, 0);
	if (csi->irq < 0) {
		ret = csi->irq;
		goto destroy_mutex;
	}

	csi->regbase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(csi->regbase)) {
		ret = PTR_ERR(csi->regbase);
		goto destroy_mutex;
	}

	csi->model = (enum imx_csi_model)(uintptr_t)of_device_get_match_data(&pdev->dev);

	ret = devm_request_irq(dev, csi->irq, imx7_csi_irq_handler, 0, "csi",
			       (void *)csi);
	if (ret < 0) {
		dev_err(dev, "Request CSI IRQ failed.\n");
		goto destroy_mutex;
	}

	/* Initialize all the media device infrastructure. */
	ret = imx7_csi_media_init(csi);
	if (ret)
		goto destroy_mutex;

	/* Set the default mbus formats. */
	ret = imx7_csi_init_cfg(&csi->sd, NULL);
	if (ret)
		goto media_cleanup;

	ret = imx7_csi_async_register(csi);
	if (ret)
		goto subdev_notifier_cleanup;

	return 0;

subdev_notifier_cleanup:
	v4l2_async_nf_unregister(&csi->notifier);
	v4l2_async_nf_cleanup(&csi->notifier);
media_cleanup:
	imx7_csi_media_cleanup(csi);

destroy_mutex:
	mutex_destroy(&csi->lock);

	return ret;
}

static int imx7_csi_remove(struct platform_device *pdev)
{
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);

	imx7_csi_media_cleanup(csi);

	v4l2_async_nf_unregister(&csi->notifier);
	v4l2_async_nf_cleanup(&csi->notifier);
	v4l2_async_unregister_subdev(sd);

	mutex_destroy(&csi->lock);

	return 0;
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
