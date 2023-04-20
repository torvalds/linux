// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd. */

#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>
#include "dev.h"
#include "regs.h"
#include "rkisp_tb_helper.h"

#define STREAM_MAX_MP_RSZ_OUTPUT_WIDTH		4416
#define STREAM_MAX_MP_RSZ_OUTPUT_HEIGHT		3312
#define STREAM_MAX_SP_RSZ_OUTPUT_WIDTH		1920
#define STREAM_MAX_SP_RSZ_OUTPUT_HEIGHT		1080
#define STREAM_MIN_RSZ_OUTPUT_WIDTH		32
#define STREAM_MIN_RSZ_OUTPUT_HEIGHT		32
#define STREAM_OUTPUT_STEP_WISE			8

#define STREAM_MIN_MP_SP_INPUT_WIDTH		STREAM_MIN_RSZ_OUTPUT_WIDTH
#define STREAM_MIN_MP_SP_INPUT_HEIGHT		STREAM_MIN_RSZ_OUTPUT_HEIGHT

static int hdr_dma_frame(struct rkisp_device *dev)
{
	int max_dma;

	switch (dev->hdr.op_mode) {
	case HDR_FRAMEX2_DDR:
	case HDR_LINEX2_DDR:
	case HDR_RDBK_FRAME1:
		max_dma = 1;
		break;
	case HDR_FRAMEX3_DDR:
	case HDR_LINEX3_DDR:
	case HDR_RDBK_FRAME2:
		max_dma = 2;
		break;
	case HDR_RDBK_FRAME3:
		max_dma = HDR_DMA_MAX;
		break;
	case HDR_LINEX2_NO_DDR:
	case HDR_NORMAL:
	default:
		max_dma = 0;
	}
	return max_dma;
}

static int rkisp_create_hdr_buf(struct rkisp_device *dev)
{
	int i, j, max_dma, max_buf = 1;
	struct rkisp_dummy_buffer *buf;
	struct rkisp_stream *stream;
	u32 size;

	stream = &dev->cap_dev.stream[RKISP_STREAM_DMATX0];
	size = stream->out_fmt.plane_fmt[0].sizeimage;
	max_dma = hdr_dma_frame(dev);
	/* hdr read back mode using base and shd address
	 * this support multi-buffer
	 */
	if (IS_HDR_RDBK(dev->hdr.op_mode)) {
		if (!dev->dmarx_dev.trigger)
			max_buf = HDR_MAX_DUMMY_BUF;
		else
			max_buf = 0;
	}
	for (i = 0; i < max_dma; i++) {
		for (j = 0; j < max_buf; j++) {
			buf = &dev->hdr.dummy_buf[i][j];
			buf->size = size;
			if (rkisp_alloc_buffer(dev, buf) < 0) {
				v4l2_err(&dev->v4l2_dev,
					"Failed to allocate the memory for hdr buffer\n");
				return -ENOMEM;
			}
			hdr_qbuf(&dev->hdr.q_tx[i], buf);
			v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
				 "hdr buf[%d][%d]:0x%x\n",
				 i, j, (u32)buf->dma_addr);
		}
		dev->hdr.index[i] = i;
	}
	/*
	 * normal: q_tx[0] to dma0
	 *	   q_tx[1] to dma1
	 * rdbk1: using dma2
		   q_tx[0] to dma2
	 * rdbk2: using dma0 (as M), dma2 (as S)
	 *	   q_tx[0] to dma0
	 *	   q_tx[1] to dma2
	 * rdbk3: using dma0 (as M), dam1 (as L), dma2 (as S)
	 *	   q_tx[0] to dma0
	 *	   q_tx[1] to dma1
	 *	   q_tx[2] to dma2
	 */
	if (dev->hdr.op_mode == HDR_RDBK_FRAME1) {
		dev->hdr.index[HDR_DMA2] = 0;
		dev->hdr.index[HDR_DMA0] = 1;
		dev->hdr.index[HDR_DMA1] = 2;
	} else if (dev->hdr.op_mode == HDR_RDBK_FRAME2) {
		dev->hdr.index[HDR_DMA0] = 0;
		dev->hdr.index[HDR_DMA2] = 1;
		dev->hdr.index[HDR_DMA1] = 2;
	}

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "hdr:%d buf index dma0:%d dma1:%d dma2:%d\n",
		 max_dma,
		 dev->hdr.index[HDR_DMA0],
		 dev->hdr.index[HDR_DMA1],
		 dev->hdr.index[HDR_DMA2]);
	return 0;
}

void hdr_destroy_buf(struct rkisp_device *dev)
{
	int i, j;
	struct rkisp_dummy_buffer *buf;

	if (atomic_read(&dev->cap_dev.refcnt) > 1 ||
	    !dev->active_sensor ||
	    (dev->active_sensor &&
	     dev->active_sensor->mbus.type != V4L2_MBUS_CSI2_DPHY) ||
	    (dev->isp_inp & INP_CIF) ||
	    (dev->isp_ver != ISP_V20 && dev->isp_ver != ISP_V21))
		return;

	atomic_set(&dev->hdr.refcnt, 0);
	for (i = 0; i < HDR_DMA_MAX; i++) {
		buf = dev->hdr.rx_cur_buf[i];
		if (buf) {
			rkisp_free_buffer(dev, buf);
			dev->hdr.rx_cur_buf[i] = NULL;
		}

		for (j = 0; j < HDR_MAX_DUMMY_BUF; j++) {
			buf = hdr_dqbuf(&dev->hdr.q_tx[i]);
			if (buf)
				rkisp_free_buffer(dev, buf);
			buf = hdr_dqbuf(&dev->hdr.q_rx[i]);
			if (buf)
				rkisp_free_buffer(dev, buf);
		}
	}
}

int hdr_update_dmatx_buf(struct rkisp_device *dev)
{
	void __iomem *base = dev->base_addr;
	struct rkisp_stream *dmatx;
	struct rkisp_dummy_buffer *buf;
	u8 i, index;

	if (!dev->active_sensor ||
	    (dev->active_sensor &&
	     dev->active_sensor->mbus.type != V4L2_MBUS_CSI2_DPHY) ||
	    (dev->isp_inp & INP_CIF) ||
	    (dev->isp_ver != ISP_V20 && dev->isp_ver != ISP_V21))
		return 0;

	for (i = RKISP_STREAM_DMATX0; i <= RKISP_STREAM_DMATX2; i++) {
		dmatx = &dev->cap_dev.stream[i];
		if (dmatx->ops && dmatx->ops->frame_end)
			dmatx->ops->frame_end(dmatx, FRAME_INIT);
	}

	if (dev->dmarx_dev.trigger)
		goto end;

	/* for rawrd auto trigger mode, config first buf */
	index = dev->hdr.index[HDR_DMA0];
	buf = hdr_dqbuf(&dev->hdr.q_rx[index]);
	if (buf) {
		mi_raw0_rd_set_addr(base, buf->dma_addr);
		dev->hdr.rx_cur_buf[index] = buf;
	} else {
		mi_raw0_rd_set_addr(base,
			readl(base + MI_RAW0_WR_BASE_SHD));
	}

	index = dev->hdr.index[HDR_DMA1];
	buf = hdr_dqbuf(&dev->hdr.q_rx[index]);
	if (buf) {
		mi_raw1_rd_set_addr(base, buf->dma_addr);
		dev->hdr.rx_cur_buf[index] = buf;
	} else {
		mi_raw1_rd_set_addr(base,
			readl(base + MI_RAW1_WR_BASE_SHD));
	}

	index = dev->hdr.index[HDR_DMA2];
	buf = hdr_dqbuf(&dev->hdr.q_rx[index]);
	if (buf) {
		mi_raw2_rd_set_addr(base, buf->dma_addr);
		dev->hdr.rx_cur_buf[index] = buf;
	} else {
		mi_raw2_rd_set_addr(base,
			readl(base + MI_RAW2_WR_BASE_SHD));
	}

end:
	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "CSI2RX CTRL0:0x%x CTRL1:0x%x\n"
		 "WR CTRL RAW0:0x%x RAW1:0x%x RAW2:0x%x\n"
		 "RD CTRL:0x%x\n",
		 readl(base + CSI2RX_CTRL0),
		 readl(base + CSI2RX_CTRL1),
		 readl(base + CSI2RX_RAW0_WR_CTRL),
		 readl(base + CSI2RX_RAW1_WR_CTRL),
		 readl(base + CSI2RX_RAW2_WR_CTRL),
		 readl(base + CSI2RX_RAW_RD_CTRL));
	return 0;
}

int hdr_config_dmatx(struct rkisp_device *dev)
{
	struct rkisp_stream *stream;
	struct v4l2_pix_format_mplane pixm;
	u32 memory = 0;

	if (atomic_inc_return(&dev->hdr.refcnt) > 1 ||
	    !dev->active_sensor ||
	    (dev->active_sensor &&
	     dev->active_sensor->mbus.type != V4L2_MBUS_CSI2_DPHY) ||
	    (dev->isp_inp & INP_CIF) ||
	    (dev->isp_ver != ISP_V20 && dev->isp_ver != ISP_V21))
		return 0;

	rkisp_create_hdr_buf(dev);
	memset(&pixm, 0, sizeof(pixm));
	if (dev->hdr.op_mode == HDR_FRAMEX2_DDR ||
	    dev->hdr.op_mode == HDR_LINEX2_DDR ||
	    dev->hdr.op_mode == HDR_FRAMEX3_DDR ||
	    dev->hdr.op_mode == HDR_LINEX3_DDR ||
	    dev->hdr.op_mode == HDR_RDBK_FRAME2 ||
	    dev->hdr.op_mode == HDR_RDBK_FRAME3) {
		stream = &dev->cap_dev.stream[RKISP_STREAM_DMATX0];
		if (stream->ops && stream->ops->config_mi)
			stream->ops->config_mi(stream);

		if (!dev->dmarx_dev.trigger) {
			pixm = stream->out_fmt;
			stream = &dev->dmarx_dev.stream[RKISP_STREAM_RAWRD0];
			rkisp_dmarx_set_fmt(stream, pixm);
			mi_raw_length(stream);
		}
	}
	if (dev->hdr.op_mode == HDR_FRAMEX3_DDR ||
	    dev->hdr.op_mode == HDR_LINEX3_DDR ||
	    dev->hdr.op_mode == HDR_RDBK_FRAME3) {
		stream = &dev->cap_dev.stream[RKISP_STREAM_DMATX1];
		if (stream->ops && stream->ops->config_mi)
			stream->ops->config_mi(stream);

		if (!dev->dmarx_dev.trigger) {
			pixm = stream->out_fmt;
			stream = &dev->dmarx_dev.stream[RKISP_STREAM_RAWRD1];
			rkisp_dmarx_set_fmt(stream, pixm);
			mi_raw_length(stream);
		}
	}
	if (dev->hdr.op_mode == HDR_RDBK_FRAME1 ||
	    dev->hdr.op_mode == HDR_RDBK_FRAME2 ||
	    dev->hdr.op_mode == HDR_RDBK_FRAME3) {
		stream = &dev->cap_dev.stream[RKISP_STREAM_DMATX2];
		if (stream->ops && stream->ops->config_mi)
			stream->ops->config_mi(stream);

		if (!dev->dmarx_dev.trigger) {
			memory = stream->memory;
			pixm = stream->out_fmt;
			stream = &dev->dmarx_dev.stream[RKISP_STREAM_RAWRD2];
			rkisp_dmarx_set_fmt(stream, pixm);
			stream->ops->config_mi(stream);
		}
	}

	if (dev->hdr.op_mode != HDR_NORMAL && !dev->dmarx_dev.trigger) {
		raw_rd_ctrl(dev->base_addr, memory << 2);
		if (pixm.width && pixm.height)
			rkisp_rawrd_set_pic_size(dev, pixm.width, pixm.height);
	}
	return 0;
}

void hdr_stop_dmatx(struct rkisp_device *dev)
{
	struct rkisp_stream *stream;

	if (atomic_dec_return(&dev->hdr.refcnt) ||
	    !dev->active_sensor ||
	    (dev->active_sensor &&
	     dev->active_sensor->mbus.type != V4L2_MBUS_CSI2_DPHY) ||
	    (dev->isp_inp & INP_CIF) ||
	    (dev->isp_ver != ISP_V20 && dev->isp_ver != ISP_V21))
		return;

	if (dev->hdr.op_mode == HDR_FRAMEX2_DDR ||
	    dev->hdr.op_mode == HDR_LINEX2_DDR ||
	    dev->hdr.op_mode == HDR_FRAMEX3_DDR ||
	    dev->hdr.op_mode == HDR_LINEX3_DDR ||
	    dev->hdr.op_mode == HDR_RDBK_FRAME2 ||
	    dev->hdr.op_mode == HDR_RDBK_FRAME3) {
		stream = &dev->cap_dev.stream[RKISP_STREAM_DMATX0];
		stream->ops->stop_mi(stream);
	}
	if (dev->hdr.op_mode == HDR_FRAMEX3_DDR ||
	    dev->hdr.op_mode == HDR_LINEX3_DDR ||
	    dev->hdr.op_mode == HDR_RDBK_FRAME3) {
		stream = &dev->cap_dev.stream[RKISP_STREAM_DMATX1];
		stream->ops->stop_mi(stream);
	}
	if (dev->hdr.op_mode == HDR_RDBK_FRAME1 ||
	    dev->hdr.op_mode == HDR_RDBK_FRAME2 ||
	    dev->hdr.op_mode == HDR_RDBK_FRAME3) {
		stream = &dev->cap_dev.stream[RKISP_STREAM_DMATX2];
		stream->ops->stop_mi(stream);
	}
}

struct rkisp_dummy_buffer *hdr_dqbuf(struct list_head *q)
{
	struct rkisp_dummy_buffer *buf = NULL;

	if (!list_empty(q)) {
		buf = list_first_entry(q,
			struct rkisp_dummy_buffer, queue);
		list_del(&buf->queue);
	}
	return buf;
}

void hdr_qbuf(struct list_head *q,
	      struct rkisp_dummy_buffer *buf)
{
	if (buf)
		list_add_tail(&buf->queue, q);
}

void rkisp_config_dmatx_valid_buf(struct rkisp_device *dev)
{
	struct rkisp_hw_dev *hw = dev->hw_dev;
	struct rkisp_stream *stream;
	struct rkisp_device *isp;
	u32 i, j;

	if (!hw->dummy_buf.mem_priv ||
	    !dev->active_sensor ||
	    (dev->active_sensor &&
	     dev->active_sensor->mbus.type != V4L2_MBUS_CSI2_DPHY) ||
	    (dev->isp_inp & INP_CIF) ||
	    (dev->isp_ver != ISP_V20 && dev->isp_ver != ISP_V21))
		return;
	/* dmatx buf update by mi force or oneself frame end,
	 * for async dmatx enable need to update to valid buf first.
	 */
	for (i = 0; i < hw->dev_num; i++) {
		isp = hw->isp[i];
		if (!isp ||
		    (isp && !(isp->isp_inp & INP_CSI)))
			continue;
		for (j = RKISP_STREAM_DMATX0; j < RKISP_MAX_STREAM; j++) {
			stream = &isp->cap_dev.stream[j];
			if (!stream->linked || stream->curr_buf || stream->next_buf)
				continue;
			mi_set_y_addr(stream, hw->dummy_buf.dma_addr);
		}
	}
}

/* Get xsubs and ysubs for fourcc formats
 *
 * @xsubs: horizontal color samples in a 4*4 matrix, for yuv
 * @ysubs: vertical color samples in a 4*4 matrix, for yuv
 */
int rkisp_fcc_xysubs(u32 fcc, u32 *xsubs, u32 *ysubs)
{
	switch (fcc) {
	case V4L2_PIX_FMT_GREY:
	case V4L2_PIX_FMT_YUV444M:
		*xsubs = 1;
		*ysubs = 1;
		break;
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_YUV422P:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
	case V4L2_PIX_FMT_YVU422M:
	case V4L2_PIX_FMT_FBC2:
		*xsubs = 2;
		*ysubs = 1;
		break;
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21M:
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_FBCG:
	case V4L2_PIX_FMT_FBC0:
		*xsubs = 2;
		*ysubs = 2;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int rkisp_mbus_code_xysubs(u32 code, u32 *xsubs, u32 *ysubs)
{
	switch (code) {
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YVYU8_1X16:
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_VYUY8_1X16:
		*xsubs = 2;
		*ysubs = 1;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int rkisp_stream_frame_start(struct rkisp_device *dev, u32 isp_mis)
{
	struct rkisp_stream *stream;
	int i;

	if (isp_mis)
		rkisp_dvbm_event(dev, CIF_ISP_V_START);
	rkisp_bridge_update_mi(dev, isp_mis);

	for (i = 0; i < RKISP_MAX_STREAM; i++) {
		if (i == RKISP_STREAM_VIR || i == RKISP_STREAM_LUMA)
			continue;
		stream = &dev->cap_dev.stream[i];
		if (stream->streaming &&
		    stream->ops && stream->ops->frame_start)
			stream->ops->frame_start(stream, isp_mis);
	}

	return 0;
}

void rkisp_stream_buf_done_early(struct rkisp_device *dev)
{
	struct rkisp_stream *stream;
	int i;

	if (!dev->cap_dev.is_done_early)
		return;

	for (i = 0; i < RKISP_MAX_STREAM; i++) {
		if (i == RKISP_STREAM_VIR || i == RKISP_STREAM_LUMA ||
		    i == RKISP_STREAM_DMATX0 || i == RKISP_STREAM_DMATX1 ||
		    i == RKISP_STREAM_DMATX2 || i == RKISP_STREAM_DMATX3)
			continue;
		stream = &dev->cap_dev.stream[i];
		if (stream->streaming && !stream->stopping &&
		    stream->ops && stream->ops->frame_end)
			stream->ops->frame_end(stream, FRAME_WORK);
	}
}

struct stream_config rkisp_mp_stream_config = {
	/* constraints */
	.max_rsz_width = STREAM_MAX_MP_RSZ_OUTPUT_WIDTH,
	.max_rsz_height = STREAM_MAX_MP_RSZ_OUTPUT_HEIGHT,
	.min_rsz_width = STREAM_MIN_RSZ_OUTPUT_WIDTH,
	.min_rsz_height = STREAM_MIN_RSZ_OUTPUT_HEIGHT,
	.frame_end_id = CIF_MI_MP_FRAME,
	/* registers */
	.rsz = {
		.ctrl = CIF_MRSZ_CTRL,
		.scale_hy = CIF_MRSZ_SCALE_HY,
		.scale_hcr = CIF_MRSZ_SCALE_HCR,
		.scale_hcb = CIF_MRSZ_SCALE_HCB,
		.scale_vy = CIF_MRSZ_SCALE_VY,
		.scale_vc = CIF_MRSZ_SCALE_VC,
		.scale_lut = CIF_MRSZ_SCALE_LUT,
		.scale_lut_addr = CIF_MRSZ_SCALE_LUT_ADDR,
		.scale_hy_shd = CIF_MRSZ_SCALE_HY_SHD,
		.scale_hcr_shd = CIF_MRSZ_SCALE_HCR_SHD,
		.scale_hcb_shd = CIF_MRSZ_SCALE_HCB_SHD,
		.scale_vy_shd = CIF_MRSZ_SCALE_VY_SHD,
		.scale_vc_shd = CIF_MRSZ_SCALE_VC_SHD,
		.phase_hy = CIF_MRSZ_PHASE_HY,
		.phase_hc = CIF_MRSZ_PHASE_HC,
		.phase_vy = CIF_MRSZ_PHASE_VY,
		.phase_vc = CIF_MRSZ_PHASE_VC,
		.ctrl_shd = CIF_MRSZ_CTRL_SHD,
		.phase_hy_shd = CIF_MRSZ_PHASE_HY_SHD,
		.phase_hc_shd = CIF_MRSZ_PHASE_HC_SHD,
		.phase_vy_shd = CIF_MRSZ_PHASE_VY_SHD,
		.phase_vc_shd = CIF_MRSZ_PHASE_VC_SHD,
	},
	.dual_crop = {
		.ctrl = CIF_DUAL_CROP_CTRL,
		.yuvmode_mask = CIF_DUAL_CROP_MP_MODE_YUV,
		.rawmode_mask = CIF_DUAL_CROP_MP_MODE_RAW,
		.h_offset = CIF_DUAL_CROP_M_H_OFFS,
		.v_offset = CIF_DUAL_CROP_M_V_OFFS,
		.h_size = CIF_DUAL_CROP_M_H_SIZE,
		.v_size = CIF_DUAL_CROP_M_V_SIZE,
	},
	.mi = {
		.y_size_init = CIF_MI_MP_Y_SIZE_INIT,
		.cb_size_init = CIF_MI_MP_CB_SIZE_INIT,
		.cr_size_init = CIF_MI_MP_CR_SIZE_INIT,
		.y_base_ad_init = CIF_MI_MP_Y_BASE_AD_INIT,
		.cb_base_ad_init = CIF_MI_MP_CB_BASE_AD_INIT,
		.cr_base_ad_init = CIF_MI_MP_CR_BASE_AD_INIT,
		.y_offs_cnt_init = CIF_MI_MP_Y_OFFS_CNT_INIT,
		.cb_offs_cnt_init = CIF_MI_MP_CB_OFFS_CNT_INIT,
		.cr_offs_cnt_init = CIF_MI_MP_CR_OFFS_CNT_INIT,
		.y_base_ad_shd = CIF_MI_MP_Y_BASE_AD_SHD,
		.y_pic_size = ISP3X_MI_MP_WR_Y_PIC_SIZE,
	},
};

struct stream_config rkisp_sp_stream_config = {
	/* constraints */
	.max_rsz_width = STREAM_MAX_SP_RSZ_OUTPUT_WIDTH,
	.max_rsz_height = STREAM_MAX_SP_RSZ_OUTPUT_HEIGHT,
	.min_rsz_width = STREAM_MIN_RSZ_OUTPUT_WIDTH,
	.min_rsz_height = STREAM_MIN_RSZ_OUTPUT_HEIGHT,
	.frame_end_id = CIF_MI_SP_FRAME,
	/* registers */
	.rsz = {
		.ctrl = CIF_SRSZ_CTRL,
		.scale_hy = CIF_SRSZ_SCALE_HY,
		.scale_hcr = CIF_SRSZ_SCALE_HCR,
		.scale_hcb = CIF_SRSZ_SCALE_HCB,
		.scale_vy = CIF_SRSZ_SCALE_VY,
		.scale_vc = CIF_SRSZ_SCALE_VC,
		.scale_lut = CIF_SRSZ_SCALE_LUT,
		.scale_lut_addr = CIF_SRSZ_SCALE_LUT_ADDR,
		.scale_hy_shd = CIF_SRSZ_SCALE_HY_SHD,
		.scale_hcr_shd = CIF_SRSZ_SCALE_HCR_SHD,
		.scale_hcb_shd = CIF_SRSZ_SCALE_HCB_SHD,
		.scale_vy_shd = CIF_SRSZ_SCALE_VY_SHD,
		.scale_vc_shd = CIF_SRSZ_SCALE_VC_SHD,
		.phase_hy = CIF_SRSZ_PHASE_HY,
		.phase_hc = CIF_SRSZ_PHASE_HC,
		.phase_vy = CIF_SRSZ_PHASE_VY,
		.phase_vc = CIF_SRSZ_PHASE_VC,
		.ctrl_shd = CIF_SRSZ_CTRL_SHD,
		.phase_hy_shd = CIF_SRSZ_PHASE_HY_SHD,
		.phase_hc_shd = CIF_SRSZ_PHASE_HC_SHD,
		.phase_vy_shd = CIF_SRSZ_PHASE_VY_SHD,
		.phase_vc_shd = CIF_SRSZ_PHASE_VC_SHD,
	},
	.dual_crop = {
		.ctrl = CIF_DUAL_CROP_CTRL,
		.yuvmode_mask = CIF_DUAL_CROP_SP_MODE_YUV,
		.rawmode_mask = CIF_DUAL_CROP_SP_MODE_RAW,
		.h_offset = CIF_DUAL_CROP_S_H_OFFS,
		.v_offset = CIF_DUAL_CROP_S_V_OFFS,
		.h_size = CIF_DUAL_CROP_S_H_SIZE,
		.v_size = CIF_DUAL_CROP_S_V_SIZE,
	},
	.mi = {
		.y_size_init = CIF_MI_SP_Y_SIZE_INIT,
		.cb_size_init = CIF_MI_SP_CB_SIZE_INIT,
		.cr_size_init = CIF_MI_SP_CR_SIZE_INIT,
		.y_base_ad_init = CIF_MI_SP_Y_BASE_AD_INIT,
		.cb_base_ad_init = CIF_MI_SP_CB_BASE_AD_INIT,
		.cr_base_ad_init = CIF_MI_SP_CR_BASE_AD_INIT,
		.y_offs_cnt_init = CIF_MI_SP_Y_OFFS_CNT_INIT,
		.cb_offs_cnt_init = CIF_MI_SP_CB_OFFS_CNT_INIT,
		.cr_offs_cnt_init = CIF_MI_SP_CR_OFFS_CNT_INIT,
		.y_base_ad_shd = CIF_MI_SP_Y_BASE_AD_SHD,
		.y_pic_size = ISP3X_MI_SP_WR_Y_PIC_SIZE,
	},
};

static const
struct capture_fmt *find_fmt(struct rkisp_stream *stream, const u32 pixelfmt)
{
	const struct capture_fmt *fmt;
	int i;

	for (i = 0; i < stream->config->fmt_size; i++) {
		fmt = &stream->config->fmts[i];
		if (fmt->fourcc == pixelfmt)
			return fmt;
	}
	return NULL;
}

static void restrict_rsz_resolution(struct rkisp_stream *stream,
				    const struct stream_config *cfg,
				    struct v4l2_rect *max_rsz)
{
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_rect *input_win = rkisp_get_isp_sd_win(&dev->isp_sdev);

	if (stream->id == RKISP_STREAM_VIR ||
	    (dev->isp_ver == ISP_V30 && stream->id == RKISP_STREAM_BP)) {
		max_rsz->width = input_win->width;
		max_rsz->height = input_win->height;
	} else if (stream->id == RKISP_STREAM_FBC) {
		max_rsz->width = stream->dcrop.width;
		max_rsz->height = stream->dcrop.height;
	} else if (stream->id == RKISP_STREAM_MPDS ||
		   stream->id == RKISP_STREAM_BPDS) {
		struct rkisp_stream *t = &dev->cap_dev.stream[stream->conn_id];

		max_rsz->width = t->out_fmt.width / 4;
		max_rsz->height = t->out_fmt.height / 4;
	} else if (stream->id == RKISP_STREAM_LUMA) {
		u32 div = dev->is_bigmode ? 32 : 16;

		max_rsz->width = ALIGN(DIV_ROUND_UP(input_win->width, div), 4);
		max_rsz->height = DIV_ROUND_UP(input_win->height, div);
	} else if (dev->hw_dev->unite) {
		/* scale down only for unite mode */
		max_rsz->width = min_t(int, input_win->width, cfg->max_rsz_width);
		max_rsz->height = min_t(int, input_win->height, cfg->max_rsz_height);
	} else {
		/* scale up/down */
		max_rsz->width = cfg->max_rsz_width;
		max_rsz->height = cfg->max_rsz_height;
	}
}

static int rkisp_set_fmt(struct rkisp_stream *stream,
			   struct v4l2_pix_format_mplane *pixm,
			   bool try)
{
	const struct capture_fmt *fmt;
	struct rkisp_vdev_node *node = &stream->vnode;
	const struct stream_config *config = stream->config;
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_rect max_rsz;
	u32 i, planes, imagsize = 0, xsubs = 1, ysubs = 1;

	fmt = find_fmt(stream, pixm->pixelformat);
	if (!fmt) {
		v4l2_err(&dev->v4l2_dev,
			 "%s nonsupport pixelformat:%c%c%c%c\n",
			 node->vdev.name,
			 pixm->pixelformat,
			 pixm->pixelformat >> 8,
			 pixm->pixelformat >> 16,
			 pixm->pixelformat >> 24);
		return -EINVAL;
	}

	/* do checks on resolution */
	restrict_rsz_resolution(stream, config, &max_rsz);
	if (stream->id == RKISP_STREAM_MP ||
	    stream->id == RKISP_STREAM_SP ||
	    (stream->id == RKISP_STREAM_BP && dev->isp_ver != ISP_V30)) {
		pixm->width = clamp_t(u32, pixm->width, config->min_rsz_width, max_rsz.width);
	} else if (pixm->width != max_rsz.width &&
		   pixm->height != max_rsz.height &&
		   (stream->id == RKISP_STREAM_LUMA ||
		    (dev->isp_ver == ISP_V30 &&
		     (stream->id == RKISP_STREAM_BP || stream->id == RKISP_STREAM_FBC)))) {
		v4l2_warn(&dev->v4l2_dev,
			  "%s no scale %dx%d should equal to %dx%d\n",
			  node->vdev.name,
			  pixm->width, pixm->height,
			  max_rsz.width, max_rsz.height);
		pixm->width = max_rsz.width;
		pixm->height = max_rsz.height;
	} else if (stream->id == RKISP_STREAM_MPDS || stream->id == RKISP_STREAM_BPDS) {
		struct rkisp_stream *t = &dev->cap_dev.stream[stream->conn_id];

		if (pixm->pixelformat != t->out_fmt.pixelformat ||
		    pixm->width != max_rsz.width || pixm->height != max_rsz.height) {
			v4l2_warn(&dev->v4l2_dev,
				  "%s from %s, force to %dx%d %c%c%c%c\n",
				  node->vdev.name, t->vnode.vdev.name,
				  max_rsz.width, max_rsz.height,
				  t->out_fmt.pixelformat,
				  t->out_fmt.pixelformat >> 8,
				  t->out_fmt.pixelformat >> 16,
				  t->out_fmt.pixelformat >> 24);
			pixm->pixelformat = t->out_fmt.pixelformat;
			pixm->width = max_rsz.width;
			pixm->height = max_rsz.height;
		}
	} else if (stream->id == RKISP_STREAM_VIR) {
		struct rkisp_stream *t;

		if (stream->conn_id != -1) {
			t = &dev->cap_dev.stream[stream->conn_id];
			*pixm = t->out_fmt;
		} else {
			for (i = RKISP_STREAM_MP; i < RKISP_STREAM_VIR; i++) {
				t = &dev->cap_dev.stream[i];
				if (t->out_isp_fmt.fmt_type != FMT_YUV || !t->streaming)
					continue;
				if (t->out_fmt.plane_fmt[0].sizeimage > imagsize) {
					imagsize = t->out_fmt.plane_fmt[0].sizeimage;
					*pixm = t->out_fmt;
					stream->conn_id = t->id;
				}
			}
		}
		if (stream->conn_id == -1) {
			v4l2_err(&dev->v4l2_dev, "no output stream for iqtool\n");
			return -EINVAL;
		}
		imagsize = 0;
	}

	pixm->num_planes = fmt->mplanes;
	pixm->field = V4L2_FIELD_NONE;
	/* get quantization from ispsd */
	pixm->quantization = stream->ispdev->isp_sdev.quantization;

	/* calculate size */
	rkisp_fcc_xysubs(fmt->fourcc, &xsubs, &ysubs);
	planes = fmt->cplanes ? fmt->cplanes : fmt->mplanes;
	for (i = 0; i < planes; i++) {
		struct v4l2_plane_pix_format *plane_fmt;
		unsigned int width, height, bytesperline, w, h;

		plane_fmt = pixm->plane_fmt + i;

		w = (fmt->fmt_type == FMT_FBC) ?
			ALIGN(pixm->width, 16) : pixm->width;
		h = (fmt->fmt_type == FMT_FBC) ?
			ALIGN(pixm->height, 16) : pixm->height;
		/* mainpath for warp default */
		if (dev->cap_dev.wrap_line && stream->id == RKISP_STREAM_MP)
			h = dev->cap_dev.wrap_line;
		width = i ? w / xsubs : w;
		height = i ? h / ysubs : h;

		if (dev->isp_ver == ISP_V20 &&
		    fmt->fmt_type == FMT_BAYER &&
		    stream->id == RKISP_STREAM_DMATX2)
			height += RKMODULE_EXTEND_LINE;

		if ((dev->isp_ver == ISP_V20 ||
		     dev->isp_ver == ISP_V21) &&
		    !stream->memory &&
		    fmt->fmt_type == FMT_BAYER &&
		    stream->id != RKISP_STREAM_MP &&
		    stream->id != RKISP_STREAM_SP)
			/* compact mode need bytesperline 4byte align */
			bytesperline = ALIGN(width * fmt->bpp[i] / 8, 256);
		else
			bytesperline = width * DIV_ROUND_UP(fmt->bpp[i], 8);

		if (i != 0 || plane_fmt->bytesperline < bytesperline)
			plane_fmt->bytesperline = bytesperline;

		/* 128bit AXI, 16byte align for bytesperline */
		if ((dev->isp_ver == ISP_V20 && stream->id == RKISP_STREAM_SP) ||
		    dev->isp_ver >= ISP_V30)
			plane_fmt->bytesperline = ALIGN(plane_fmt->bytesperline, 16);

		plane_fmt->sizeimage = plane_fmt->bytesperline * height;

		/* FMT_FBCGAIN: uv address is y size offset need 64 align
		 * FMT_FBC: width and height need 16 align
		 *          header: width * height / 16, and 4096 align for mpp
		 *          payload: yuv420 or yuv422 size
		 */
		if (fmt->fmt_type == FMT_FBCGAIN && i == 0)
			plane_fmt->sizeimage = ALIGN(plane_fmt->sizeimage, 64);
		else if (fmt->fmt_type == FMT_FBC && i == 0)
			plane_fmt->sizeimage = ALIGN(plane_fmt->sizeimage >> 4, RK_MPP_ALIGN);
		else if (fmt->fmt_type == FMT_FBC)
			plane_fmt->sizeimage += w * h;
		imagsize += plane_fmt->sizeimage;
	}

	/* convert to non-MPLANE format.
	 * it's important since we want to unify none-MPLANE
	 * and MPLANE.
	 */
	if (fmt->mplanes == 1 || fmt->fmt_type == FMT_FBCGAIN)
		pixm->plane_fmt[0].sizeimage = imagsize;

	if (!try && !stream->start_stream && !stream->streaming) {
		stream->out_isp_fmt = *fmt;
		stream->out_fmt = *pixm;

		if (stream->id == RKISP_STREAM_SP) {
			stream->u.sp.y_stride =
				pixm->plane_fmt[0].bytesperline /
				DIV_ROUND_UP(fmt->bpp[0], 8);
		} else if (stream->id == RKISP_STREAM_MP) {
			stream->u.mp.raw_enable = (fmt->fmt_type == FMT_BAYER);
		}

		v4l2_dbg(1, rkisp_debug, &stream->ispdev->v4l2_dev,
			 "%s: %s req(%d, %d) out(%d, %d)\n", __func__,
			 node->vdev.name, pixm->width, pixm->height,
			 stream->out_fmt.width, stream->out_fmt.height);
	}

	return 0;
}

struct rockit_isp_ops rockit_isp_ops = {
	.rkisp_set_fmt = rkisp_set_fmt,
};

int rkisp_fh_open(struct file *filp)
{
	struct rkisp_stream *stream = video_drvdata(filp);
	int ret;

	if (!stream->ispdev->is_probe_end)
		return -EINVAL;

	ret = v4l2_fh_open(filp);
	if (!ret) {
		ret = v4l2_pipeline_pm_get(&stream->vnode.vdev.entity);
		if (ret < 0)
			vb2_fop_release(filp);
	}

	return ret;
}

int rkisp_fop_release(struct file *file)
{
	struct rkisp_stream *stream = video_drvdata(file);
	int ret;

	ret = vb2_fop_release(file);
	if (!ret)
		v4l2_pipeline_pm_put(&stream->vnode.vdev.entity);
	return ret;
}

void rkisp_set_stream_def_fmt(struct rkisp_device *dev, u32 id,
	u32 width, u32 height, u32 pixelformat)
{
	struct rkisp_stream *stream = &dev->cap_dev.stream[id];
	struct v4l2_pix_format_mplane pixm;

	memset(&pixm, 0, sizeof(pixm));
	if (pixelformat)
		pixm.pixelformat = pixelformat;
	else
		pixm.pixelformat = stream->out_isp_fmt.fourcc;
	if (!pixm.pixelformat)
		return;

	stream->dcrop.left = 0;
	stream->dcrop.top = 0;
	stream->dcrop.width = width;
	stream->dcrop.height = height;

	pixm.width = width;
	pixm.height = height;
	rkisp_set_fmt(stream, &pixm, false);
}

/************************* v4l2_file_operations***************************/
static const struct v4l2_file_operations rkisp_fops = {
	.open = rkisp_fh_open,
	.release = rkisp_fop_release,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = video_ioctl2,
#endif
};

/*
 * mp and sp v4l2_ioctl_ops
 */

static int rkisp_enum_input(struct file *file, void *priv,
			     struct v4l2_input *input)
{
	if (input->index > 0)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;
	strlcpy(input->name, "Camera", sizeof(input->name));

	return 0;
}

static int rkisp_try_fmt_vid_cap_mplane(struct file *file, void *fh,
					 struct v4l2_format *f)
{
	struct rkisp_stream *stream = video_drvdata(file);

	return rkisp_set_fmt(stream, &f->fmt.pix_mp, true);
}

static int rkisp_enum_framesizes(struct file *file, void *prov,
				 struct v4l2_frmsizeenum *fsize)
{
	struct rkisp_stream *stream = video_drvdata(file);
	const struct stream_config *config = stream->config;
	struct v4l2_frmsize_stepwise *s = &fsize->stepwise;
	struct v4l2_frmsize_discrete *d = &fsize->discrete;
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_rect max_rsz;
	struct v4l2_rect *input_win = rkisp_get_isp_sd_win(&dev->isp_sdev);

	if (fsize->index != 0)
		return -EINVAL;

	if (!find_fmt(stream, fsize->pixel_format))
		return -EINVAL;

	restrict_rsz_resolution(stream, config, &max_rsz);

	if (stream->out_isp_fmt.fmt_type == FMT_BAYER ||
	    stream->id == RKISP_STREAM_FBC ||
	    stream->id == RKISP_STREAM_BPDS ||
	    stream->id == RKISP_STREAM_MPDS ||
	    stream->id == RKISP_STREAM_LUMA ||
	    stream->id == RKISP_STREAM_VIR ||
	    (stream->id == RKISP_STREAM_BP && dev->hw_dev->isp_ver == ISP_V30)) {
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		d->width = max_rsz.width;
		d->height = max_rsz.height;
	} else {
		fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
		s->min_width = STREAM_MIN_RSZ_OUTPUT_WIDTH;
		s->min_height = STREAM_MIN_RSZ_OUTPUT_HEIGHT;
		s->max_width = min_t(u32, max_rsz.width, input_win->width);
		s->max_height = input_win->height;
		s->step_width = STREAM_OUTPUT_STEP_WISE;
		s->step_height = STREAM_OUTPUT_STEP_WISE;
	}

	return 0;
}

static int rkisp_get_cmsk(struct rkisp_stream *stream, struct rkisp_cmsk_cfg *cfg)
{
	struct rkisp_device *dev = stream->ispdev;
	unsigned long lock_flags = 0;
	u32 i, win_en, mode;

	if ((dev->isp_ver != ISP_V30 && dev->isp_ver != ISP_V32) ||
	    stream->id == RKISP_STREAM_FBC ||
	    stream->id == RKISP_STREAM_MPDS ||
	    stream->id == RKISP_STREAM_BPDS) {
		v4l2_err(&dev->v4l2_dev, "%s not support\n", __func__);
		return -EINVAL;
	}

	spin_lock_irqsave(&dev->cmsk_lock, lock_flags);
	*cfg = dev->cmsk_cfg;
	spin_unlock_irqrestore(&dev->cmsk_lock, lock_flags);

	switch (stream->id) {
	case RKISP_STREAM_MP:
		win_en = cfg->win[0].win_en;
		mode = cfg->win[0].mode;
		break;
	case RKISP_STREAM_SP:
		win_en = cfg->win[1].win_en;
		mode = cfg->win[1].mode;
		break;
	case RKISP_STREAM_BP:
	default:
		win_en = cfg->win[2].win_en;
		mode = cfg->win[2].mode;
		break;
	}

	cfg->width_ro = dev->isp_sdev.out_crop.width;
	cfg->height_ro = dev->isp_sdev.out_crop.height;
	for (i = 0; i < RKISP_CMSK_WIN_MAX; i++) {
		cfg->win[i].win_en = !!(win_en & BIT(i));
		cfg->win[i].mode = !!(mode & BIT(i));
	}

	return 0;
}

static int rkisp_set_cmsk(struct rkisp_stream *stream, struct rkisp_cmsk_cfg *cfg)
{
	struct rkisp_device *dev = stream->ispdev;
	unsigned long lock_flags = 0;
	u16 i, win_en = 0, mode = 0;
	u16 h_offs, v_offs, h_size, v_size;
	u32 width = dev->isp_sdev.out_crop.width;
	u32 height = dev->isp_sdev.out_crop.height;
	u32 align = (dev->isp_ver == ISP_V30) ? 8 : 2;
	bool warn = false;

	if ((dev->isp_ver != ISP_V30 && dev->isp_ver != ISP_V32) ||
	    stream->id == RKISP_STREAM_FBC ||
	    stream->id == RKISP_STREAM_MPDS ||
	    stream->id == RKISP_STREAM_BPDS) {
		v4l2_err(&dev->v4l2_dev, "%s not support\n", __func__);
		return -EINVAL;
	}

	spin_lock_irqsave(&dev->cmsk_lock, lock_flags);
	dev->is_cmsk_upd = true;
	for (i = 0; i < RKISP_CMSK_WIN_MAX; i++) {
		win_en |= cfg->win[i].win_en ? BIT(i) : 0;
		mode |= cfg->win[i].mode ? BIT(i) : 0;

		if (cfg->win[i].win_en) {
			if (cfg->win[i].mode) {
				dev->cmsk_cfg.win[i].cover_color_y = cfg->win[i].cover_color_y;
				dev->cmsk_cfg.win[i].cover_color_u = cfg->win[i].cover_color_u;
				dev->cmsk_cfg.win[i].cover_color_v = cfg->win[i].cover_color_v;
			}
			h_offs = cfg->win[i].h_offs & ~0x1;
			v_offs = cfg->win[i].v_offs & ~0x1;
			h_size = ALIGN_DOWN(cfg->win[i].h_size, align);
			v_size = ALIGN_DOWN(cfg->win[i].v_size, align);
			if (h_offs != cfg->win[i].h_offs ||
			    v_offs != cfg->win[i].v_offs ||
			    h_size != cfg->win[i].h_size ||
			    v_size != cfg->win[i].v_size)
				warn = true;
			if (h_offs + h_size > width) {
				h_size = ALIGN_DOWN(width - h_offs, align);
				warn = true;
			}
			if (v_offs + v_size > height) {
				v_size = ALIGN_DOWN(height - v_offs, align);
				warn = true;
			}
			if (warn) {
				warn = false;
				v4l2_warn(&dev->v4l2_dev,
					  "%s cmsk offs 2 align, size %d align and offs + size < resolution\n"
					  "\t cmsk win%d result to offs:%d %d, size:%d %d\n",
					  stream->vnode.vdev.name, i, align, h_offs, v_offs, h_size, v_size);
			}
			dev->cmsk_cfg.win[i].h_offs = h_offs;
			dev->cmsk_cfg.win[i].v_offs = v_offs;
			dev->cmsk_cfg.win[i].h_size = h_size;
			dev->cmsk_cfg.win[i].v_size = v_size;
		}
	}

	switch (stream->id) {
	case RKISP_STREAM_MP:
		dev->cmsk_cfg.win[0].win_en = win_en;
		dev->cmsk_cfg.win[0].mode = mode;
		break;
	case RKISP_STREAM_SP:
		dev->cmsk_cfg.win[1].win_en = win_en;
		dev->cmsk_cfg.win[1].mode = mode;
		break;
	case RKISP_STREAM_BP:
	default:
		dev->cmsk_cfg.win[2].win_en = win_en;
		dev->cmsk_cfg.win[2].mode = mode;
		break;
	}
	dev->cmsk_cfg.mosaic_block = cfg->mosaic_block;
	spin_unlock_irqrestore(&dev->cmsk_lock, lock_flags);
	return 0;

}

static int rkisp_get_stream_info(struct rkisp_stream *stream,
				 struct rkisp_stream_info *info)
{
	struct rkisp_device *dev = stream->ispdev;
	u32 id = 0;

	rkisp_dmarx_get_frame(stream->ispdev, &id, NULL, NULL, true);
	info->cur_frame_id = stream->dbg.id;
	info->input_frame_loss = dev->isp_sdev.dbg.frameloss;
	info->output_frame_loss = stream->dbg.frameloss;
	info->stream_on = stream->streaming;
	info->stream_id = stream->id;
	return 0;
}

static int rkisp_get_mirror_flip(struct rkisp_stream *stream,
				 struct rkisp_mirror_flip *cfg)
{
	struct rkisp_device *dev = stream->ispdev;

	if (dev->isp_ver != ISP_V32)
		return -EINVAL;

	cfg->mirror = dev->cap_dev.is_mirror;
	cfg->flip = stream->is_flip;
	return 0;
}

static int rkisp_set_mirror_flip(struct rkisp_stream *stream,
				 struct rkisp_mirror_flip *cfg)
{
	struct rkisp_device *dev = stream->ispdev;

	if (dev->isp_ver != ISP_V32)
		return -EINVAL;

	if (dev->cap_dev.wrap_line) {
		v4l2_warn(&dev->v4l2_dev, "wrap_line mode can not set the mirror");
		dev->cap_dev.is_mirror = 0;
	} else {
		dev->cap_dev.is_mirror = cfg->mirror;
	}

	stream->is_flip = cfg->flip;
	stream->is_mf_upd = true;
	return 0;
}

static int rkisp_get_wrap_line(struct rkisp_stream *stream, struct rkisp_wrap_info *arg)
{
	struct rkisp_device *dev = stream->ispdev;

	if (dev->isp_ver != ISP_V32 && stream->id != RKISP_STREAM_MP)
		return -EINVAL;

	arg->width = dev->cap_dev.wrap_width;
	arg->height = dev->cap_dev.wrap_line;
	return 0;
}

static int rkisp_set_wrap_line(struct rkisp_stream *stream, struct rkisp_wrap_info *arg)
{
	struct rkisp_device *dev = stream->ispdev;

	if (dev->isp_ver != ISP_V32 ||
	    dev->hw_dev->dev_link_num > 1 ||
	    !stream->ops->set_wrap ||
	    dev->hw_dev->unite) {
		v4l2_err(&dev->v4l2_dev,
			 "wrap only support for single sensor and mainpath\n");
		return -EINVAL;
	}
	dev->cap_dev.wrap_width = arg->width;
	return stream->ops->set_wrap(stream, arg->height);
}

static int rkisp_set_fps(struct rkisp_stream *stream, int *fps)
{
	struct rkisp_device *dev = stream->ispdev;

	if (dev->isp_ver != ISP_V32)
		return -EINVAL;

	return rkisp_rockit_fps_set(fps, stream);
}

static int rkisp_get_fps(struct rkisp_stream *stream, int *fps)
{
	struct rkisp_device *dev = stream->ispdev;

	if (dev->isp_ver != ISP_V32)
		return -EINVAL;

	return rkisp_rockit_fps_get(fps, stream);
}

int rkisp_get_tb_stream_info(struct rkisp_stream *stream,
			     struct rkisp_tb_stream_info *info)
{
	struct rkisp_device *dev = stream->ispdev;

	if (stream->id != RKISP_STREAM_MP) {
		v4l2_err(&dev->v4l2_dev, "fast only support for MP\n");
		return -EINVAL;
	}

	if (!dev->tb_stream_info.buf_max) {
		v4l2_err(&dev->v4l2_dev, "thunderboot no enough memory for image\n");
		return -EINVAL;
	}

	memcpy(info, &dev->tb_stream_info, sizeof(*info));
	return 0;
}

int rkisp_free_tb_stream_buf(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_isp_subdev *sdev = &dev->isp_sdev;
	struct v4l2_subdev *sd = &sdev->sd;

	return sd->ops->core->ioctl(sd, RKISP_CMD_FREE_SHARED_BUF, NULL);
}

static int rkisp_set_iqtool_connect_id(struct rkisp_stream *stream, int stream_id)
{
	struct rkisp_device *dev = stream->ispdev;

	if (stream->id != RKISP_STREAM_VIR) {
		v4l2_err(&dev->v4l2_dev, "only support for iqtool video\n");
		goto err;
	}

	if (stream_id != RKISP_STREAM_MP &&
	    stream_id != RKISP_STREAM_SP &&
	    stream_id != RKISP_STREAM_BP) {
		v4l2_err(&dev->v4l2_dev, "invalid connect stream id\n");
		goto err;
	}

	stream->conn_id = stream_id;
	return 0;
err:
	return -EINVAL;
}

static long rkisp_ioctl_default(struct file *file, void *fh,
				bool valid_prio, unsigned int cmd, void *arg)
{
	struct rkisp_stream *stream = video_drvdata(file);
	long ret = 0;

	if (!arg && cmd != RKISP_CMD_FREE_TB_STREAM_BUF)
		return -EINVAL;

	switch (cmd) {
	case RKISP_CMD_GET_CSI_MEMORY_MODE:
		if (stream->id != RKISP_STREAM_DMATX0 &&
		    stream->id != RKISP_STREAM_DMATX1 &&
		    stream->id != RKISP_STREAM_DMATX2 &&
		    stream->id != RKISP_STREAM_DMATX3)
			ret = -EINVAL;
		else if (stream->memory == 0)
			*(int *)arg = CSI_MEM_COMPACT;
		else if (stream->memory == SW_CSI_RAW_WR_SIMG_MODE)
			*(int *)arg = CSI_MEM_WORD_BIG_ALIGN;
		else
			*(int *)arg = CSI_MEM_WORD_LITTLE_ALIGN;
		break;
	case RKISP_CMD_SET_CSI_MEMORY_MODE:
		if (stream->id != RKISP_STREAM_DMATX0 &&
		    stream->id != RKISP_STREAM_DMATX1 &&
		    stream->id != RKISP_STREAM_DMATX2 &&
		    stream->id != RKISP_STREAM_DMATX3)
			ret = -EINVAL;
		else if (*(int *)arg == CSI_MEM_COMPACT)
			stream->memory = 0;
		else if (*(int *)arg == CSI_MEM_WORD_BIG_ALIGN)
			stream->memory = SW_CSI_RAW_WR_SIMG_MODE;
		else
			stream->memory =
				SW_CSI_RWA_WR_SIMG_SWP | SW_CSI_RAW_WR_SIMG_MODE;
		break;
	case RKISP_CMD_GET_CMSK:
		ret = rkisp_get_cmsk(stream, arg);
		break;
	case RKISP_CMD_SET_CMSK:
		ret = rkisp_set_cmsk(stream, arg);
		break;
	case RKISP_CMD_GET_STREAM_INFO:
		ret = rkisp_get_stream_info(stream, arg);
		break;
	case RKISP_CMD_GET_MIRROR_FLIP:
		ret = rkisp_get_mirror_flip(stream, arg);
		break;
	case RKISP_CMD_SET_MIRROR_FLIP:
		ret = rkisp_set_mirror_flip(stream, arg);
		break;
	case RKISP_CMD_GET_WRAP_LINE:
		ret = rkisp_get_wrap_line(stream, arg);
		break;
	case RKISP_CMD_SET_WRAP_LINE:
		ret = rkisp_set_wrap_line(stream, arg);
		break;
	case RKISP_CMD_SET_FPS:
		ret = rkisp_set_fps(stream, arg);
		break;
	case RKISP_CMD_GET_FPS:
		ret = rkisp_get_fps(stream, arg);
		break;
	case RKISP_CMD_GET_TB_STREAM_INFO:
		ret = rkisp_get_tb_stream_info(stream, arg);
		break;
	case RKISP_CMD_FREE_TB_STREAM_BUF:
		ret = rkisp_free_tb_stream_buf(stream);
		break;
	case RKISP_CMD_SET_IQTOOL_CONN_ID:
		ret = rkisp_set_iqtool_connect_id(stream, *(int *)arg);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int rkisp_enum_frameintervals(struct file *file, void *fh,
				     struct v4l2_frmivalenum *fival)
{
	const struct rkisp_stream *stream = video_drvdata(file);
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_sensor_info *sensor = dev->active_sensor;
	struct v4l2_subdev_frame_interval fi;
	int ret;

	if (fival->index != 0)
		return -EINVAL;

	if (!sensor) {
		/* TODO: active_sensor is NULL if using DMARX path */
		v4l2_err(&dev->v4l2_dev, "%s Not active sensor\n", __func__);
		return -ENODEV;
	}

	ret = v4l2_subdev_call(sensor->sd, video, g_frame_interval, &fi);
	if (ret && ret != -ENOIOCTLCMD) {
		return ret;
	} else if (ret == -ENOIOCTLCMD) {
		/* Set a default value for sensors not implements ioctl */
		fi.interval.numerator = 1;
		fi.interval.denominator = 30;
	}

	fival->type = V4L2_FRMIVAL_TYPE_CONTINUOUS;
	fival->stepwise.step.numerator = 1;
	fival->stepwise.step.denominator = 1;
	fival->stepwise.max.numerator = 1;
	fival->stepwise.max.denominator = 1;
	fival->stepwise.min.numerator = fi.interval.numerator;
	fival->stepwise.min.denominator = fi.interval.denominator;

	return 0;
}

static int rkisp_enum_fmt_vid_cap_mplane(struct file *file, void *priv,
					  struct v4l2_fmtdesc *f)
{
	struct rkisp_stream *stream = video_drvdata(file);
	const struct capture_fmt *fmt = NULL;
	struct rkisp_device *dev = stream->ispdev;
	struct ispsd_in_fmt *isp_in_fmt = &dev->isp_sdev.in_fmt;
	struct ispsd_out_fmt *isp_out_fmt = &dev->isp_sdev.out_fmt;
	int ret = -EINVAL;

	/* only one output format for raw */
	if (isp_out_fmt->fmt_type == FMT_BAYER ||
	    stream->id == RKISP_STREAM_DMATX0 ||
	    stream->id == RKISP_STREAM_DMATX1 ||
	    stream->id == RKISP_STREAM_DMATX2 ||
	    stream->id == RKISP_STREAM_DMATX3) {
		u32 pixelformat = rkisp_mbus_pixelcode_to_v4l2(isp_in_fmt->mbus_code);

		if (f->index == 0) {
			fmt = find_fmt(stream, pixelformat);
			if (fmt) {
				f->pixelformat = pixelformat;
				ret = 0;
			}
		}
		return ret;
	}

	if (f->index >= stream->config->fmt_size)
		return -EINVAL;

	fmt = &stream->config->fmts[f->index];
	/* only output yuv format */
	if (isp_out_fmt->fmt_type == FMT_YUV && fmt->fmt_type == FMT_BAYER)
		return -EINVAL;

	f->pixelformat = fmt->fourcc;
	switch (f->pixelformat) {
	case V4L2_PIX_FMT_FBC2:
		strscpy(f->description,
			"Rockchip yuv422sp fbc encoder",
			sizeof(f->description));
		break;
	case V4L2_PIX_FMT_FBC0:
		strscpy(f->description,
			"Rockchip yuv420sp fbc encoder",
			sizeof(f->description));
		break;
	case V4L2_PIX_FMT_FBCG:
		strscpy(f->description,
			"Rockchip fbc gain",
			sizeof(f->description));
		break;
	case V4l2_PIX_FMT_EBD8:
		strscpy(f->description,
			"Embedded data 8-bit",
			sizeof(f->description));
		break;
	case V4l2_PIX_FMT_SPD16:
		strscpy(f->description,
			"Shield pix data 16-bit",
			sizeof(f->description));
		break;
	default:
		break;
	}

	return 0;
}

static int rkisp_s_fmt_vid_cap_mplane(struct file *file,
				       void *priv, struct v4l2_format *f)
{
	struct rkisp_stream *stream = video_drvdata(file);
	struct video_device *vdev = &stream->vnode.vdev;
	struct rkisp_vdev_node *node = vdev_to_node(vdev);
	struct rkisp_device *dev = stream->ispdev;

	if (vb2_is_streaming(&node->buf_queue)) {
		v4l2_err(&dev->v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	return rkisp_set_fmt(stream, &f->fmt.pix_mp, false);
}

static int rkisp_g_fmt_vid_cap_mplane(struct file *file, void *fh,
				       struct v4l2_format *f)
{
	struct rkisp_stream *stream = video_drvdata(file);

	f->fmt.pix_mp = stream->out_fmt;

	return 0;
}

static int rkisp_g_selection(struct file *file, void *prv,
			      struct v4l2_selection *sel)
{
	struct rkisp_stream *stream = video_drvdata(file);
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_rect *dcrop = &stream->dcrop;
	struct v4l2_rect *input_win;

	input_win = rkisp_get_isp_sd_win(&dev->isp_sdev);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.width = input_win->width;
		sel->r.height = input_win->height;
		sel->r.left = 0;
		sel->r.top = 0;
		break;
	case V4L2_SEL_TGT_CROP:
		sel->r = *dcrop;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static struct v4l2_rect *rkisp_update_crop(struct rkisp_stream *stream,
					    struct v4l2_rect *sel,
					    const struct v4l2_rect *in)
{
	struct rkisp_device *dev = stream->ispdev;
	bool is_unite = !!dev->hw_dev->unite;
	u32 align = is_unite ? 4 : 2;

	/* Not crop for MP bayer raw data and dmatx path */
	if ((stream->id == RKISP_STREAM_MP &&
	     stream->out_isp_fmt.fmt_type == FMT_BAYER) ||
	    stream->id == RKISP_STREAM_DMATX0 ||
	    stream->id == RKISP_STREAM_DMATX1 ||
	    stream->id == RKISP_STREAM_DMATX2 ||
	    stream->id == RKISP_STREAM_DMATX3 ||
	    stream->id == RKISP_STREAM_MPDS ||
	    stream->id == RKISP_STREAM_BPDS) {
		sel->left = 0;
		sel->top = 0;
		sel->width = in->width;
		sel->height = in->height;
		return sel;
	}

	sel->left = ALIGN(sel->left, 2);
	sel->width = ALIGN(sel->width, align);
	sel->left = clamp_t(u32, sel->left, 0,
			    in->width - STREAM_MIN_MP_SP_INPUT_WIDTH);
	sel->top = clamp_t(u32, sel->top, 0,
			   in->height - STREAM_MIN_MP_SP_INPUT_HEIGHT);
	sel->width = clamp_t(u32, sel->width, STREAM_MIN_MP_SP_INPUT_WIDTH,
			     in->width - sel->left);
	sel->height = clamp_t(u32, sel->height, STREAM_MIN_MP_SP_INPUT_HEIGHT,
			      in->height - sel->top);
	if (is_unite && (sel->width + 2 * sel->left) != in->width) {
		sel->left = ALIGN_DOWN((in->width - sel->width) / 2, 2);
		v4l2_warn(&dev->v4l2_dev,
			  "try horizontal center crop(%d,%d)/%dx%d for dual isp\n",
			  sel->left, sel->top, sel->width, sel->height);
	}
	stream->is_crop_upd = true;
	return sel;
}

static int rkisp_s_selection(struct file *file, void *prv,
			      struct v4l2_selection *sel)
{
	struct rkisp_stream *stream = video_drvdata(file);
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_rect *dcrop = &stream->dcrop;
	const struct v4l2_rect *input_win;

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	if (sel->flags != 0)
		return -EINVAL;

	input_win = rkisp_get_isp_sd_win(&dev->isp_sdev);
	*dcrop = *rkisp_update_crop(stream, &sel->r, input_win);
	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "stream %d crop(%d,%d)/%dx%d\n", stream->id,
		 dcrop->left, dcrop->top, dcrop->width, dcrop->height);

	return 0;
}

static int rkisp_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct rkisp_stream *stream = video_drvdata(file);
	struct device *dev = stream->ispdev->dev;
	struct video_device *vdev = video_devdata(file);

	strlcpy(cap->card, vdev->name, sizeof(cap->card));
	snprintf(cap->driver, sizeof(cap->driver),
		 "%s_v%d", dev->driver->name,
		 stream->ispdev->isp_ver >> 4);
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", dev_name(dev));
	cap->version = RKISP_DRIVER_VERSION;
	return 0;
}

static const struct v4l2_ioctl_ops rkisp_v4l2_ioctl_ops = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_enum_input = rkisp_enum_input,
	.vidioc_try_fmt_vid_cap_mplane = rkisp_try_fmt_vid_cap_mplane,
	.vidioc_enum_fmt_vid_cap = rkisp_enum_fmt_vid_cap_mplane,
	.vidioc_s_fmt_vid_cap_mplane = rkisp_s_fmt_vid_cap_mplane,
	.vidioc_g_fmt_vid_cap_mplane = rkisp_g_fmt_vid_cap_mplane,
	.vidioc_s_selection = rkisp_s_selection,
	.vidioc_g_selection = rkisp_g_selection,
	.vidioc_querycap = rkisp_querycap,
	.vidioc_enum_frameintervals = rkisp_enum_frameintervals,
	.vidioc_enum_framesizes = rkisp_enum_framesizes,
	.vidioc_default = rkisp_ioctl_default,
};

static void rkisp_buf_done_task(unsigned long arg)
{
	struct rkisp_stream *stream = (struct rkisp_stream *)arg;
	struct rkisp_buffer *buf = NULL;
	unsigned long lock_flags = 0;
	LIST_HEAD(local_list);

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	list_replace_init(&stream->buf_done_list, &local_list);
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);

	while (!list_empty(&local_list)) {
		buf = list_first_entry(&local_list,
				       struct rkisp_buffer, queue);
		list_del(&buf->queue);

		v4l2_dbg(2, rkisp_debug, &stream->ispdev->v4l2_dev,
			 "stream:%d seq:%d buf:0x%x done\n",
			 stream->id, buf->vb.sequence, buf->buff_addr[0]);
		vb2_buffer_done(&buf->vb.vb2_buf,
				stream->streaming ? VB2_BUF_STATE_DONE : VB2_BUF_STATE_ERROR);
	}
}

void rkisp_stream_buf_done(struct rkisp_stream *stream,
			   struct rkisp_buffer *buf)
{
	unsigned long lock_flags = 0;

	if (!stream || !buf)
		return;
	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	list_add_tail(&buf->queue, &stream->buf_done_list);
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);
	tasklet_schedule(&stream->buf_done_tasklet);
}

static void rkisp_stream_fast(struct work_struct *work)
{
	struct rkisp_capture_device *cap_dev =
		container_of(work, struct rkisp_capture_device, fast_work);
	struct rkisp_stream *stream = &cap_dev->stream[0];
	struct rkisp_device *ispdev = cap_dev->ispdev;
	struct v4l2_subdev *sd = ispdev->active_sensor->sd;
	int ret;

	if (ispdev->isp_ver != ISP_V32)
		return;

	mutex_lock(&ispdev->hw_dev->dev_lock);
	rkisp_chk_tb_over(ispdev);
	mutex_unlock(&ispdev->hw_dev->dev_lock);
	if (ispdev->tb_head.complete != RKISP_TB_OK)
		return;
	ret = v4l2_pipeline_pm_get(&stream->vnode.vdev.entity);
	if (ret < 0) {
		dev_err(ispdev->dev, "%s PM get fail:%d\n", __func__, ret);
		ispdev->is_thunderboot = false;
		return;
	}

	if (ispdev->hw_dev->dev_num > 1)
		ispdev->hw_dev->is_single = false;
	ispdev->is_pre_on = true;
	ispdev->is_rdbk_auto = true;
	ispdev->pipe.open(&ispdev->pipe, &stream->vnode.vdev.entity, true);
	v4l2_subdev_call(sd, video, s_stream, true);
}

void rkisp_unregister_stream_vdev(struct rkisp_stream *stream)
{
	tasklet_kill(&stream->buf_done_tasklet);
	media_entity_cleanup(&stream->vnode.vdev.entity);
	video_unregister_device(&stream->vnode.vdev);
}

int rkisp_register_stream_vdev(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct video_device *vdev = &stream->vnode.vdev;
	struct rkisp_vdev_node *node;
	struct media_entity *source, *sink;
	int ret = 0, pad;

	mutex_init(&stream->apilock);
	node = vdev_to_node(vdev);

	vdev->ioctl_ops = &rkisp_v4l2_ioctl_ops;
	vdev->release = video_device_release_empty;
	vdev->fops = &rkisp_fops;
	vdev->minor = -1;
	vdev->v4l2_dev = v4l2_dev;
	vdev->lock = &stream->apilock;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE |
				V4L2_CAP_STREAMING;
	video_set_drvdata(vdev, stream);
	vdev->vfl_dir = VFL_DIR_RX;
	node->pad.flags = MEDIA_PAD_FL_SINK;
	vdev->queue = &node->buf_queue;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		v4l2_err(v4l2_dev,
			 "video_register_device failed with error %d\n", ret);
		return ret;
	}

	ret = media_entity_pads_init(&vdev->entity, 1, &node->pad);
	if (ret < 0)
		goto unreg;

	source = &dev->csi_dev.sd.entity;
	switch (stream->id) {
	case RKISP_STREAM_DMATX0://CSI_SRC_CH1
	case RKISP_STREAM_DMATX1://CSI_SRC_CH2
	case RKISP_STREAM_DMATX2://CSI_SRC_CH3
	case RKISP_STREAM_DMATX3://CSI_SRC_CH4
		pad = stream->id;
		dev->csi_dev.sink[pad - 1].linked = true;
		dev->csi_dev.sink[pad - 1].index = BIT(pad - 1);
		break;
	default:
		source = &dev->isp_sdev.sd.entity;
		pad = RKISP_ISP_PAD_SOURCE_PATH;
	}
	sink = &vdev->entity;
	ret = media_create_pad_link(source, pad,
		sink, 0, stream->linked);
	if (ret < 0)
		goto unreg;
	INIT_LIST_HEAD(&stream->buf_done_list);
	tasklet_init(&stream->buf_done_tasklet,
		     rkisp_buf_done_task,
		     (unsigned long)stream);
	tasklet_disable(&stream->buf_done_tasklet);
	return 0;
unreg:
	video_unregister_device(vdev);
	return ret;
}

int rkisp_register_stream_vdevs(struct rkisp_device *dev)
{
	struct rkisp_capture_device *cap_dev = &dev->cap_dev;
	struct stream_config *st_cfg = &rkisp_mp_stream_config;
	int ret = 0;

	memset(cap_dev, 0, sizeof(*cap_dev));
	cap_dev->ispdev = dev;
	atomic_set(&cap_dev->refcnt, 0);

	if (dev->isp_ver <= ISP_V13) {
		if (dev->isp_ver == ISP_V12) {
			st_cfg->max_rsz_width = CIF_ISP_INPUT_W_MAX_V12;
			st_cfg->max_rsz_height = CIF_ISP_INPUT_H_MAX_V12;
		} else if (dev->isp_ver == ISP_V13) {
			st_cfg->max_rsz_width = CIF_ISP_INPUT_W_MAX_V13;
			st_cfg->max_rsz_height = CIF_ISP_INPUT_H_MAX_V13;
		}
		ret = rkisp_register_stream_v1x(dev);
	} else if (dev->isp_ver == ISP_V20) {
		ret = rkisp_register_stream_v20(dev);
	} else if (dev->isp_ver == ISP_V21) {
		st_cfg->max_rsz_width = CIF_ISP_INPUT_W_MAX_V21;
		st_cfg->max_rsz_height = CIF_ISP_INPUT_H_MAX_V21;
		ret = rkisp_register_stream_v21(dev);
	} else if (dev->isp_ver == ISP_V30) {
		st_cfg->max_rsz_width = dev->hw_dev->unite ?
					CIF_ISP_INPUT_W_MAX_V30_UNITE : CIF_ISP_INPUT_W_MAX_V30;
		st_cfg->max_rsz_height = dev->hw_dev->unite ?
					 CIF_ISP_INPUT_H_MAX_V30_UNITE : CIF_ISP_INPUT_H_MAX_V30;
		ret = rkisp_register_stream_v30(dev);
	} else if (dev->isp_ver == ISP_V32) {
		st_cfg->max_rsz_width = dev->hw_dev->unite ?
					CIF_ISP_INPUT_W_MAX_V32_UNITE : CIF_ISP_INPUT_W_MAX_V32;
		st_cfg->max_rsz_height = dev->hw_dev->unite ?
					CIF_ISP_INPUT_H_MAX_V32_UNITE : CIF_ISP_INPUT_H_MAX_V32;
		st_cfg = &rkisp_sp_stream_config;
		st_cfg->max_rsz_width = dev->hw_dev->unite ?
					CIF_ISP_INPUT_W_MAX_V32_UNITE : CIF_ISP_INPUT_W_MAX_V32;
		st_cfg->max_rsz_height = dev->hw_dev->unite ?
					 CIF_ISP_INPUT_H_MAX_V32_UNITE : CIF_ISP_INPUT_H_MAX_V32;
		ret = rkisp_register_stream_v32(dev);
	} else if (dev->isp_ver == ISP_V32_L) {
		st_cfg->max_rsz_width = CIF_ISP_INPUT_W_MAX_V32_L;
		st_cfg->max_rsz_height = CIF_ISP_INPUT_H_MAX_V32_L;
		st_cfg = &rkisp_sp_stream_config;
		st_cfg->max_rsz_width = CIF_ISP_INPUT_W_MAX_V32_L;
		st_cfg->max_rsz_height = CIF_ISP_INPUT_H_MAX_V32_L;
		ret = rkisp_register_stream_v32(dev);
	}

	INIT_WORK(&cap_dev->fast_work, rkisp_stream_fast);
	return ret;
}

void rkisp_unregister_stream_vdevs(struct rkisp_device *dev)
{
	if (dev->isp_ver <= ISP_V13)
		rkisp_unregister_stream_v1x(dev);
	else if (dev->isp_ver == ISP_V20)
		rkisp_unregister_stream_v20(dev);
	else if (dev->isp_ver == ISP_V21)
		rkisp_unregister_stream_v21(dev);
	else if (dev->isp_ver == ISP_V30)
		rkisp_unregister_stream_v30(dev);
	else if (dev->isp_ver == ISP_V32 || dev->isp_ver == ISP_V32_L)
		rkisp_unregister_stream_v32(dev);
}

void rkisp_mi_isr(u32 mis_val, struct rkisp_device *dev)
{
	if (dev->isp_ver <= ISP_V13)
		rkisp_mi_v1x_isr(mis_val, dev);
	else if (dev->isp_ver == ISP_V20)
		rkisp_mi_v20_isr(mis_val, dev);
	else if (dev->isp_ver == ISP_V21)
		rkisp_mi_v21_isr(mis_val, dev);
	else if (dev->isp_ver == ISP_V30)
		rkisp_mi_v30_isr(mis_val, dev);
	else if (dev->isp_ver == ISP_V32 || dev->isp_ver == ISP_V32_L)
		rkisp_mi_v32_isr(mis_val, dev);
}
