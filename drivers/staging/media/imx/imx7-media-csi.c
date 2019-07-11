// SPDX-License-Identifier: GPL-2.0
/*
 * V4L2 Capture CSI Subdev for Freescale i.MX7 SOC
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

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>

#include <media/imx.h>
#include "imx-media.h"

#define IMX7_CSI_PAD_SINK	0
#define IMX7_CSI_PAD_SRC	1
#define IMX7_CSI_PADS_NUM	2

/* reset values */
#define CSICR1_RESET_VAL	0x40000800
#define CSICR2_RESET_VAL	0x0
#define CSICR3_RESET_VAL	0x0

/* csi control reg 1 */
#define BIT_SWAP16_EN		BIT(31)
#define BIT_EXT_VSYNC		BIT(30)
#define BIT_EOF_INT_EN		BIT(29)
#define BIT_PRP_IF_EN		BIT(28)
#define BIT_CCIR_MODE		BIT(27)
#define BIT_COF_INT_EN		BIT(26)
#define BIT_SF_OR_INTEN		BIT(25)
#define BIT_RF_OR_INTEN		BIT(24)
#define BIT_SFF_DMA_DONE_INTEN  BIT(22)
#define BIT_STATFF_INTEN	BIT(21)
#define BIT_FB2_DMA_DONE_INTEN  BIT(20)
#define BIT_FB1_DMA_DONE_INTEN  BIT(19)
#define BIT_RXFF_INTEN		BIT(18)
#define BIT_SOF_POL		BIT(17)
#define BIT_SOF_INTEN		BIT(16)
#define BIT_MCLKDIV		(0xF << 12)
#define BIT_HSYNC_POL		BIT(11)
#define BIT_CCIR_EN		BIT(10)
#define BIT_MCLKEN		BIT(9)
#define BIT_FCC			BIT(8)
#define BIT_PACK_DIR		BIT(7)
#define BIT_CLR_STATFIFO	BIT(6)
#define BIT_CLR_RXFIFO		BIT(5)
#define BIT_GCLK_MODE		BIT(4)
#define BIT_INV_DATA		BIT(3)
#define BIT_INV_PCLK		BIT(2)
#define BIT_REDGE		BIT(1)
#define BIT_PIXEL_BIT		BIT(0)

#define SHIFT_MCLKDIV		12

/* control reg 3 */
#define BIT_FRMCNT		(0xFFFF << 16)
#define BIT_FRMCNT_RST		BIT(15)
#define BIT_DMA_REFLASH_RFF	BIT(14)
#define BIT_DMA_REFLASH_SFF	BIT(13)
#define BIT_DMA_REQ_EN_RFF	BIT(12)
#define BIT_DMA_REQ_EN_SFF	BIT(11)
#define BIT_STATFF_LEVEL	(0x7 << 8)
#define BIT_HRESP_ERR_EN	BIT(7)
#define BIT_RXFF_LEVEL		(0x7 << 4)
#define BIT_TWO_8BIT_SENSOR	BIT(3)
#define BIT_ZERO_PACK_EN	BIT(2)
#define BIT_ECC_INT_EN		BIT(1)
#define BIT_ECC_AUTO_EN		BIT(0)

#define SHIFT_FRMCNT		16
#define SHIFT_RXFIFO_LEVEL	4

/* csi status reg */
#define BIT_ADDR_CH_ERR_INT	BIT(28)
#define BIT_FIELD0_INT		BIT(27)
#define BIT_FIELD1_INT		BIT(26)
#define BIT_SFF_OR_INT		BIT(25)
#define BIT_RFF_OR_INT		BIT(24)
#define BIT_DMA_TSF_DONE_SFF	BIT(22)
#define BIT_STATFF_INT		BIT(21)
#define BIT_DMA_TSF_DONE_FB2	BIT(20)
#define BIT_DMA_TSF_DONE_FB1	BIT(19)
#define BIT_RXFF_INT		BIT(18)
#define BIT_EOF_INT		BIT(17)
#define BIT_SOF_INT		BIT(16)
#define BIT_F2_INT		BIT(15)
#define BIT_F1_INT		BIT(14)
#define BIT_COF_INT		BIT(13)
#define BIT_HRESP_ERR_INT	BIT(7)
#define BIT_ECC_INT		BIT(1)
#define BIT_DRDY		BIT(0)

/* csi control reg 18 */
#define BIT_CSI_HW_ENABLE		BIT(31)
#define BIT_MIPI_DATA_FORMAT_RAW8	(0x2a << 25)
#define BIT_MIPI_DATA_FORMAT_RAW10	(0x2b << 25)
#define BIT_MIPI_DATA_FORMAT_RAW12	(0x2c << 25)
#define BIT_MIPI_DATA_FORMAT_RAW14	(0x2d << 25)
#define BIT_MIPI_DATA_FORMAT_YUV422_8B	(0x1e << 25)
#define BIT_MIPI_DATA_FORMAT_MASK	(0x3F << 25)
#define BIT_MIPI_DATA_FORMAT_OFFSET	25
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

#define CSI_MCLK_VF		1
#define CSI_MCLK_ENC		2
#define CSI_MCLK_RAW		4
#define CSI_MCLK_I2C		8

#define CSI_CSICR1		0x0
#define CSI_CSICR2		0x4
#define CSI_CSICR3		0x8
#define CSI_STATFIFO		0xC
#define CSI_CSIRXFIFO		0x10
#define CSI_CSIRXCNT		0x14
#define CSI_CSISR		0x18

#define CSI_CSIDBG		0x1C
#define CSI_CSIDMASA_STATFIFO	0x20
#define CSI_CSIDMATS_STATFIFO	0x24
#define CSI_CSIDMASA_FB1	0x28
#define CSI_CSIDMASA_FB2	0x2C
#define CSI_CSIFBUF_PARA	0x30
#define CSI_CSIIMAG_PARA	0x34

#define CSI_CSICR18		0x48
#define CSI_CSICR19		0x4c

static const char * const imx7_csi_clk_id[] = {"axi", "dcic", "mclk"};

struct imx7_csi {
	struct device *dev;
	struct v4l2_subdev sd;
	struct imx_media_video_dev *vdev;
	struct imx_media_dev *imxmd;
	struct media_pad pad[IMX7_CSI_PADS_NUM];

	/* lock to protect members below */
	struct mutex lock;
	/* lock to protect irq handler when stop streaming */
	spinlock_t irqlock;

	struct v4l2_subdev *src_sd;

	struct media_entity *sink;

	struct v4l2_fwnode_endpoint upstream_ep;

	struct v4l2_mbus_framefmt format_mbus[IMX7_CSI_PADS_NUM];
	const struct imx_media_pixfmt *cc[IMX7_CSI_PADS_NUM];
	struct v4l2_fract frame_interval[IMX7_CSI_PADS_NUM];

	struct v4l2_ctrl_handler ctrl_hdlr;

	void __iomem *regbase;
	int irq;

	int num_clks;
	struct clk_bulk_data *clks;

	/* active vb2 buffers to send to video dev sink */
	struct imx_media_buffer *active_vb2_buf[2];
	struct imx_media_dma_buf underrun_buf;

	int buf_num;
	u32 frame_sequence;

	bool last_eof;
	bool is_init;
	bool is_streaming;
	bool is_csi2;

	struct completion last_eof_completion;
};

#define imx7_csi_reg_read(_csi, _offset) \
	__raw_readl((_csi)->regbase + (_offset))
#define imx7_csi_reg_write(_csi, _val, _offset) \
	__raw_writel(_val, (_csi)->regbase + (_offset))

static void imx7_csi_clk_enable(struct imx7_csi *csi)
{
	int ret;

	ret = clk_bulk_prepare_enable(csi->num_clks, csi->clks);
	if (ret < 0)
		dev_err(csi->dev, "failed to enable clocks\n");
}

static void imx7_csi_clk_disable(struct imx7_csi *csi)
{
	clk_bulk_disable_unprepare(csi->num_clks, csi->clks);
}

static void imx7_csi_hw_reset(struct imx7_csi *csi)
{
	imx7_csi_reg_write(csi,
			   imx7_csi_reg_read(csi, CSI_CSICR3) | BIT_FRMCNT_RST,
			   CSI_CSICR3);

	imx7_csi_reg_write(csi, CSICR1_RESET_VAL, CSI_CSICR1);
	imx7_csi_reg_write(csi, CSICR2_RESET_VAL, CSI_CSICR2);
	imx7_csi_reg_write(csi, CSICR3_RESET_VAL, CSI_CSICR3);
}

static unsigned long imx7_csi_irq_clear(struct imx7_csi *csi)
{
	unsigned long isr;

	isr = imx7_csi_reg_read(csi, CSI_CSISR);
	imx7_csi_reg_write(csi, isr, CSI_CSISR);

	return isr;
}

static void imx7_csi_init_interface(struct imx7_csi *csi)
{
	unsigned int val = 0;
	unsigned int imag_para;

	val = BIT_SOF_POL | BIT_REDGE | BIT_GCLK_MODE | BIT_HSYNC_POL |
		BIT_FCC | 1 << SHIFT_MCLKDIV | BIT_MCLKEN;
	imx7_csi_reg_write(csi, val, CSI_CSICR1);

	imag_para = (800 << 16) | 600;
	imx7_csi_reg_write(csi, imag_para, CSI_CSIIMAG_PARA);

	val = BIT_DMA_REFLASH_RFF;
	imx7_csi_reg_write(csi, val, CSI_CSICR3);
}

static void imx7_csi_hw_enable_irq(struct imx7_csi *csi)
{
	unsigned long cr1 = imx7_csi_reg_read(csi, CSI_CSICR1);

	cr1 |= BIT_SOF_INTEN;
	cr1 |= BIT_RFF_OR_INT;

	/* still capture needs DMA interrupt */
	cr1 |= BIT_FB1_DMA_DONE_INTEN;
	cr1 |= BIT_FB2_DMA_DONE_INTEN;

	cr1 |= BIT_EOF_INT_EN;

	imx7_csi_reg_write(csi, cr1, CSI_CSICR1);
}

static void imx7_csi_hw_disable_irq(struct imx7_csi *csi)
{
	unsigned long cr1 = imx7_csi_reg_read(csi, CSI_CSICR1);

	cr1 &= ~BIT_SOF_INTEN;
	cr1 &= ~BIT_RFF_OR_INT;
	cr1 &= ~BIT_FB1_DMA_DONE_INTEN;
	cr1 &= ~BIT_FB2_DMA_DONE_INTEN;
	cr1 &= ~BIT_EOF_INT_EN;

	imx7_csi_reg_write(csi, cr1, CSI_CSICR1);
}

static void imx7_csi_hw_enable(struct imx7_csi *csi)
{
	unsigned long cr = imx7_csi_reg_read(csi, CSI_CSICR18);

	cr |= BIT_CSI_HW_ENABLE;

	imx7_csi_reg_write(csi, cr, CSI_CSICR18);
}

static void imx7_csi_hw_disable(struct imx7_csi *csi)
{
	unsigned long cr = imx7_csi_reg_read(csi, CSI_CSICR18);

	cr &= ~BIT_CSI_HW_ENABLE;

	imx7_csi_reg_write(csi, cr, CSI_CSICR18);
}

static void imx7_csi_dma_reflash(struct imx7_csi *csi)
{
	unsigned long cr3 = imx7_csi_reg_read(csi, CSI_CSICR18);

	cr3 = imx7_csi_reg_read(csi, CSI_CSICR3);
	cr3 |= BIT_DMA_REFLASH_RFF;
	imx7_csi_reg_write(csi, cr3, CSI_CSICR3);
}

static void imx7_csi_rx_fifo_clear(struct imx7_csi *csi)
{
	unsigned long cr1;

	cr1 = imx7_csi_reg_read(csi, CSI_CSICR1);
	imx7_csi_reg_write(csi, cr1 & ~BIT_FCC, CSI_CSICR1);
	cr1 = imx7_csi_reg_read(csi, CSI_CSICR1);
	imx7_csi_reg_write(csi, cr1 | BIT_CLR_RXFIFO, CSI_CSICR1);

	cr1 = imx7_csi_reg_read(csi, CSI_CSICR1);
	imx7_csi_reg_write(csi, cr1 | BIT_FCC, CSI_CSICR1);
}

static void imx7_csi_buf_stride_set(struct imx7_csi *csi, u32 stride)
{
	imx7_csi_reg_write(csi, stride, CSI_CSIFBUF_PARA);
}

static void imx7_csi_deinterlace_enable(struct imx7_csi *csi, bool enable)
{
	unsigned long cr18 = imx7_csi_reg_read(csi, CSI_CSICR18);

	if (enable)
		cr18 |= BIT_DEINTERLACE_EN;
	else
		cr18 &= ~BIT_DEINTERLACE_EN;

	imx7_csi_reg_write(csi, cr18, CSI_CSICR18);
}

static void imx7_csi_dmareq_rff_enable(struct imx7_csi *csi)
{
	unsigned long cr3 = imx7_csi_reg_read(csi, CSI_CSICR3);
	unsigned long cr2 = imx7_csi_reg_read(csi, CSI_CSICR2);

	/* Burst Type of DMA Transfer from RxFIFO. INCR16 */
	cr2 |= 0xC0000000;

	cr3 |= BIT_DMA_REQ_EN_RFF;
	cr3 |= BIT_HRESP_ERR_EN;
	cr3 &= ~BIT_RXFF_LEVEL;
	cr3 |= 0x2 << 4;

	imx7_csi_reg_write(csi, cr3, CSI_CSICR3);
	imx7_csi_reg_write(csi, cr2, CSI_CSICR2);
}

static void imx7_csi_dmareq_rff_disable(struct imx7_csi *csi)
{
	unsigned long cr3 = imx7_csi_reg_read(csi, CSI_CSICR3);

	cr3 &= ~BIT_DMA_REQ_EN_RFF;
	cr3 &= ~BIT_HRESP_ERR_EN;
	imx7_csi_reg_write(csi, cr3, CSI_CSICR3);
}

static void imx7_csi_set_imagpara(struct imx7_csi *csi, int width, int height)
{
	int imag_para;
	int rx_count;

	rx_count = (width * height) >> 2;
	imx7_csi_reg_write(csi, rx_count, CSI_CSIRXCNT);

	imag_para = (width << 16) | height;
	imx7_csi_reg_write(csi, imag_para, CSI_CSIIMAG_PARA);

	/* reflash the embedded DMA controller */
	imx7_csi_dma_reflash(csi);
}

static void imx7_csi_sw_reset(struct imx7_csi *csi)
{
	imx7_csi_hw_disable(csi);

	imx7_csi_rx_fifo_clear(csi);

	imx7_csi_dma_reflash(csi);

	usleep_range(2000, 3000);

	imx7_csi_irq_clear(csi);

	imx7_csi_hw_enable(csi);
}

static void imx7_csi_error_recovery(struct imx7_csi *csi)
{
	imx7_csi_hw_disable(csi);

	imx7_csi_rx_fifo_clear(csi);

	imx7_csi_dma_reflash(csi);

	imx7_csi_hw_enable(csi);
}

static void imx7_csi_init(struct imx7_csi *csi)
{
	if (csi->is_init)
		return;

	imx7_csi_clk_enable(csi);
	imx7_csi_hw_reset(csi);
	imx7_csi_init_interface(csi);
	imx7_csi_dmareq_rff_enable(csi);

	csi->is_init = true;
}

static void imx7_csi_deinit(struct imx7_csi *csi)
{
	if (!csi->is_init)
		return;

	imx7_csi_hw_reset(csi);
	imx7_csi_init_interface(csi);
	imx7_csi_dmareq_rff_disable(csi);
	imx7_csi_clk_disable(csi);

	csi->is_init = false;
}

static int imx7_csi_get_upstream_endpoint(struct imx7_csi *csi,
					  struct v4l2_fwnode_endpoint *ep,
					  bool skip_mux)
{
	struct device_node *endpoint, *port;
	struct media_entity *src;
	struct v4l2_subdev *sd;
	struct media_pad *pad;

	if (!csi->src_sd)
		return -EPIPE;

	src = &csi->src_sd->entity;

skip_video_mux:
	/* get source pad of entity directly upstream from src */
	pad = imx_media_find_upstream_pad(csi->imxmd, src, 0);
	if (IS_ERR(pad))
		return PTR_ERR(pad);

	sd = media_entity_to_v4l2_subdev(pad->entity);

	/* To get bus type we may need to skip video mux */
	if (skip_mux && src->function == MEDIA_ENT_F_VID_MUX) {
		src = &sd->entity;
		goto skip_video_mux;
	}

	/*
	 * NOTE: this assumes an OF-graph port id is the same as a
	 * media pad index.
	 */
	port = of_graph_get_port_by_id(sd->dev->of_node, pad->index);
	if (!port)
		return -ENODEV;

	endpoint = of_get_next_child(port, NULL);
	of_node_put(port);
	if (!endpoint)
		return -ENODEV;

	v4l2_fwnode_endpoint_parse(of_fwnode_handle(endpoint), ep);
	of_node_put(endpoint);

	return 0;
}

static int imx7_csi_link_setup(struct media_entity *entity,
			       const struct media_pad *local,
			       const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);
	struct v4l2_subdev *remote_sd;
	int ret = 0;

	dev_dbg(csi->dev, "link setup %s -> %s\n", remote->entity->name,
		local->entity->name);

	mutex_lock(&csi->lock);

	if (local->flags & MEDIA_PAD_FL_SINK) {
		if (!is_media_entity_v4l2_subdev(remote->entity)) {
			ret = -EINVAL;
			goto unlock;
		}

		remote_sd = media_entity_to_v4l2_subdev(remote->entity);

		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (csi->src_sd) {
				ret = -EBUSY;
				goto unlock;
			}
			csi->src_sd = remote_sd;
		} else {
			csi->src_sd = NULL;
		}

		goto init;
	}

	/* source pad */
	if (flags & MEDIA_LNK_FL_ENABLED) {
		if (csi->sink) {
			ret = -EBUSY;
			goto unlock;
		}
		csi->sink = remote->entity;
	} else {
		v4l2_ctrl_handler_free(&csi->ctrl_hdlr);
		v4l2_ctrl_handler_init(&csi->ctrl_hdlr, 0);
		csi->sink = NULL;
	}

init:
	if (csi->sink || csi->src_sd)
		imx7_csi_init(csi);
	else
		imx7_csi_deinit(csi);

unlock:
	mutex_unlock(&csi->lock);

	return ret;
}

static int imx7_csi_pad_link_validate(struct v4l2_subdev *sd,
				      struct media_link *link,
				      struct v4l2_subdev_format *source_fmt,
				      struct v4l2_subdev_format *sink_fmt)
{
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);
	struct v4l2_fwnode_endpoint upstream_ep = {};
	int ret;

	ret = v4l2_subdev_link_validate_default(sd, link, source_fmt, sink_fmt);
	if (ret)
		return ret;

	ret = imx7_csi_get_upstream_endpoint(csi, &upstream_ep, true);
	if (ret) {
		v4l2_err(&csi->sd, "failed to find upstream endpoint\n");
		return ret;
	}

	mutex_lock(&csi->lock);

	csi->upstream_ep = upstream_ep;
	csi->is_csi2 = (upstream_ep.bus_type == V4L2_MBUS_CSI2_DPHY);

	mutex_unlock(&csi->lock);

	return 0;
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

static void imx7_csi_vb2_buf_done(struct imx7_csi *csi)
{
	struct imx_media_video_dev *vdev = csi->vdev;
	struct imx_media_buffer *done, *next;
	struct vb2_buffer *vb;
	dma_addr_t phys;

	done = csi->active_vb2_buf[csi->buf_num];
	if (done) {
		done->vbuf.field = vdev->fmt.fmt.pix.field;
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
	unsigned long status;

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

static int imx7_csi_dma_start(struct imx7_csi *csi)
{
	struct imx_media_video_dev *vdev = csi->vdev;
	struct v4l2_pix_format *out_pix = &vdev->fmt.fmt.pix;
	int ret;

	ret = imx_media_alloc_dma_buf(csi->imxmd, &csi->underrun_buf,
				      out_pix->sizeimage);
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

	imx7_csi_dma_unsetup_vb2_buf(csi, VB2_BUF_STATE_ERROR);

	imx_media_free_dma_buf(csi->imxmd, &csi->underrun_buf);
}

static int imx7_csi_configure(struct imx7_csi *csi)
{
	struct imx_media_video_dev *vdev = csi->vdev;
	struct v4l2_pix_format *out_pix = &vdev->fmt.fmt.pix;
	__u32 in_code = csi->format_mbus[IMX7_CSI_PAD_SINK].code;
	u32 cr1, cr18;

	if (out_pix->field == V4L2_FIELD_INTERLACED) {
		imx7_csi_deinterlace_enable(csi, true);
		imx7_csi_buf_stride_set(csi, out_pix->width);
	} else {
		imx7_csi_deinterlace_enable(csi, false);
		imx7_csi_buf_stride_set(csi, 0);
	}

	imx7_csi_set_imagpara(csi, out_pix->width, out_pix->height);

	if (!csi->is_csi2)
		return 0;

	cr1 = imx7_csi_reg_read(csi, CSI_CSICR1);
	cr1 &= ~BIT_GCLK_MODE;

	cr18 = imx7_csi_reg_read(csi, CSI_CSICR18);
	cr18 &= BIT_MIPI_DATA_FORMAT_MASK;
	cr18 |= BIT_DATA_FROM_MIPI;

	switch (out_pix->pixelformat) {
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_YUYV:
		cr18 |= BIT_MIPI_DATA_FORMAT_YUV422_8B;
		break;
	case V4L2_PIX_FMT_SBGGR8:
		cr18 |= BIT_MIPI_DATA_FORMAT_RAW8;
		break;
	case V4L2_PIX_FMT_SBGGR16:
		if (in_code == MEDIA_BUS_FMT_SBGGR10_1X10)
			cr18 |= BIT_MIPI_DATA_FORMAT_RAW10;
		else if (in_code == MEDIA_BUS_FMT_SBGGR12_1X12)
			cr18 |= BIT_MIPI_DATA_FORMAT_RAW12;
		else if (in_code == MEDIA_BUS_FMT_SBGGR14_1X14)
			cr18 |= BIT_MIPI_DATA_FORMAT_RAW14;
		cr1 |= BIT_PIXEL_BIT;
		break;
	default:
		return -EINVAL;
	}

	imx7_csi_reg_write(csi, cr1, CSI_CSICR1);
	imx7_csi_reg_write(csi, cr18, CSI_CSICR18);

	return 0;
}

static int imx7_csi_enable(struct imx7_csi *csi)
{
	imx7_csi_sw_reset(csi);

	if (csi->is_csi2) {
		imx7_csi_dmareq_rff_enable(csi);
		imx7_csi_hw_enable_irq(csi);
		imx7_csi_hw_enable(csi);
		return 0;
	}

	return 0;
}

static void imx7_csi_disable(struct imx7_csi *csi)
{
	imx7_csi_dmareq_rff_disable(csi);

	imx7_csi_hw_disable_irq(csi);

	imx7_csi_buf_stride_set(csi, 0);

	imx7_csi_hw_disable(csi);
}

static int imx7_csi_streaming_start(struct imx7_csi *csi)
{
	int ret;

	ret = imx7_csi_dma_start(csi);
	if (ret < 0)
		return ret;

	ret = imx7_csi_configure(csi);
	if (ret < 0)
		goto dma_stop;

	imx7_csi_enable(csi);

	return 0;

dma_stop:
	imx7_csi_dma_stop(csi);

	return ret;
}

static int imx7_csi_streaming_stop(struct imx7_csi *csi)
{
	imx7_csi_dma_stop(csi);

	imx7_csi_disable(csi);

	return 0;
}

static int imx7_csi_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&csi->lock);

	if (!csi->src_sd || !csi->sink) {
		ret = -EPIPE;
		goto out_unlock;
	}

	if (csi->is_streaming == !!enable)
		goto out_unlock;

	if (enable) {
		ret = v4l2_subdev_call(csi->src_sd, video, s_stream, 1);
		if (ret < 0)
			goto out_unlock;

		ret = imx7_csi_streaming_start(csi);
		if (ret < 0) {
			v4l2_subdev_call(csi->src_sd, video, s_stream, 0);
			goto out_unlock;
		}
	} else {
		imx7_csi_streaming_stop(csi);

		v4l2_subdev_call(csi->src_sd, video, s_stream, 0);
	}

	csi->is_streaming = !!enable;

out_unlock:
	mutex_unlock(&csi->lock);

	return ret;
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
		ret = imx_media_enum_mbus_format(&code->code, code->index,
						 CS_SEL_ANY, true);
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
		in_cc = imx_media_find_mbus_format(in_fmt->code, CS_SEL_ANY,
						   true);

		sdformat->format.width = in_fmt->width;
		sdformat->format.height = in_fmt->height;
		sdformat->format.code = in_fmt->code;
		*cc = in_cc;

		sdformat->format.colorspace = in_fmt->colorspace;
		sdformat->format.xfer_func = in_fmt->xfer_func;
		break;
	case IMX7_CSI_PAD_SINK:
		*cc = imx_media_find_mbus_format(sdformat->format.code,
						 CS_SEL_ANY, true);
		if (!*cc) {
			imx_media_enum_mbus_format(&code, 0, CS_SEL_ANY, false);
			*cc = imx_media_find_mbus_format(code, CS_SEL_ANY,
							 false);
			sdformat->format.code = (*cc)->codes[0];
		}
		break;
	default:
		return -EINVAL;
		break;
	}

	imx_media_try_colorimetry(&sdformat->format, false);

	return 0;
}

static int imx7_csi_set_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_format *sdformat)
{
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);
	struct imx_media_video_dev *vdev = csi->vdev;
	const struct imx_media_pixfmt *outcc;
	struct v4l2_mbus_framefmt *outfmt;
	struct v4l2_pix_format vdev_fmt;
	struct v4l2_rect vdev_compose;
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

	if (sdformat->which == V4L2_SUBDEV_FORMAT_TRY)
		goto out_unlock;

	csi->cc[sdformat->pad] = cc;

	/* propagate output pad format to capture device */
	imx_media_mbus_fmt_to_pix_fmt(&vdev_fmt, &vdev_compose,
				      &csi->format_mbus[IMX7_CSI_PAD_SRC],
				      csi->cc[IMX7_CSI_PAD_SRC]);
	mutex_unlock(&csi->lock);
	imx_media_capture_device_set_format(vdev, &vdev_fmt, &vdev_compose);

	return 0;

out_unlock:
	mutex_unlock(&csi->lock);

	return ret;
}

static int imx7_csi_registered(struct v4l2_subdev *sd)
{
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);
	int ret;
	int i;

	for (i = 0; i < IMX7_CSI_PADS_NUM; i++) {
		csi->pad[i].flags = (i == IMX7_CSI_PAD_SINK) ?
			MEDIA_PAD_FL_SINK : MEDIA_PAD_FL_SOURCE;

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

	ret = media_entity_pads_init(&sd->entity, IMX7_CSI_PADS_NUM, csi->pad);
	if (ret < 0)
		return ret;

	ret = imx_media_capture_device_register(csi->imxmd, csi->vdev);
	if (ret < 0)
		return ret;

	ret = imx_media_add_video_device(csi->imxmd, csi->vdev);
	if (ret < 0) {
		imx_media_capture_device_unregister(csi->vdev);
		return ret;
	}

	return 0;
}

static void imx7_csi_unregistered(struct v4l2_subdev *sd)
{
	struct imx7_csi *csi = v4l2_get_subdevdata(sd);

	imx_media_capture_device_unregister(csi->vdev);
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

static const struct media_entity_operations imx7_csi_entity_ops = {
	.link_setup	= imx7_csi_link_setup,
	.link_validate	= v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_video_ops imx7_csi_video_ops = {
	.s_stream		= imx7_csi_s_stream,
};

static const struct v4l2_subdev_pad_ops imx7_csi_pad_ops = {
	.init_cfg =		imx7_csi_init_cfg,
	.enum_mbus_code =	imx7_csi_enum_mbus_code,
	.get_fmt =		imx7_csi_get_fmt,
	.set_fmt =		imx7_csi_set_fmt,
	.link_validate =	imx7_csi_pad_link_validate,
};

static const struct v4l2_subdev_ops imx7_csi_subdev_ops = {
	.video =	&imx7_csi_video_ops,
	.pad =		&imx7_csi_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx7_csi_internal_ops = {
	.registered	= imx7_csi_registered,
	.unregistered	= imx7_csi_unregistered,
};

static int imx7_csi_parse_endpoint(struct device *dev,
				   struct v4l2_fwnode_endpoint *vep,
				   struct v4l2_async_subdev *asd)
{
	return fwnode_device_is_available(asd->match.fwnode) ? 0 : -EINVAL;
}

static int imx7_csi_clocks_get(struct imx7_csi *csi)
{
	struct device *dev = csi->dev;
	int i;

	csi->num_clks = ARRAY_SIZE(imx7_csi_clk_id);
	csi->clks = devm_kcalloc(dev, csi->num_clks, sizeof(*csi->clks),
				 GFP_KERNEL);

	if (!csi->clks)
		return -ENOMEM;

	for (i = 0; i < csi->num_clks; i++)
		csi->clks[i].id = imx7_csi_clk_id[i];

	return devm_clk_bulk_get(dev, csi->num_clks, csi->clks);
}

static int imx7_csi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct imx_media_dev *imxmd;
	struct imx7_csi *csi;
	struct resource *res;
	int ret;

	csi = devm_kzalloc(&pdev->dev, sizeof(*csi), GFP_KERNEL);
	if (!csi)
		return -ENOMEM;

	csi->dev = dev;

	ret = imx7_csi_clocks_get(csi);
	if (ret < 0) {
		dev_err(dev, "Failed to get clocks");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	csi->irq = platform_get_irq(pdev, 0);
	if (!res || csi->irq < 0) {
		dev_err(dev, "Missing platform resources data\n");
		return -ENODEV;
	}

	csi->regbase = devm_ioremap_resource(dev, res);
	if (IS_ERR(csi->regbase)) {
		dev_err(dev, "Failed platform resources map\n");
		return -ENODEV;
	}

	spin_lock_init(&csi->irqlock);
	mutex_init(&csi->lock);

	/* install interrupt handler */
	ret = devm_request_irq(dev, csi->irq, imx7_csi_irq_handler, 0, "csi",
			       (void *)csi);
	if (ret < 0) {
		dev_err(dev, "Request CSI IRQ failed.\n");
		ret = -ENODEV;
		goto destroy_mutex;
	}

	/* add media device */
	imxmd = imx_media_dev_init(dev);
	if (IS_ERR(imxmd)) {
		ret = PTR_ERR(imxmd);
		goto destroy_mutex;
	}
	platform_set_drvdata(pdev, &csi->sd);

	ret = imx_media_of_add_csi(imxmd, node);
	if (ret < 0 && ret != -ENODEV && ret != -EEXIST)
		goto cleanup;

	ret = imx_media_dev_notifier_register(imxmd);
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

	csi->vdev = imx_media_capture_device_init(&csi->sd, IMX7_CSI_PAD_SRC);
	if (IS_ERR(csi->vdev))
		return PTR_ERR(csi->vdev);

	v4l2_ctrl_handler_init(&csi->ctrl_hdlr, 0);
	csi->sd.ctrl_handler = &csi->ctrl_hdlr;

	ret = v4l2_async_register_fwnode_subdev(&csi->sd,
						sizeof(struct v4l2_async_subdev),
						NULL, 0,
						imx7_csi_parse_endpoint);
	if (ret)
		goto free;

	return 0;

free:
	imx_media_capture_device_unregister(csi->vdev);
	imx_media_capture_device_remove(csi->vdev);
	v4l2_ctrl_handler_free(&csi->ctrl_hdlr);

cleanup:
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

	imx_media_capture_device_unregister(csi->vdev);
	imx_media_capture_device_remove(csi->vdev);

	v4l2_async_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&csi->ctrl_hdlr);

	mutex_destroy(&csi->lock);

	return 0;
}

static const struct of_device_id imx7_csi_of_match[] = {
	{ .compatible = "fsl,imx7-csi" },
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
