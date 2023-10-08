// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Rockchip Electronics Co., Ltd. */

#include <linux/kfifo.h>
#include <linux/rk-isp32-config.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>	/* for ISP statistics */
#include "dev.h"
#include "regs.h"
#include "common.h"
#include "isp_stats.h"
#include "isp_stats_v32.h"
#include "isp_params_v32.h"

#define ISP32_3A_MEAS_DONE		BIT(31)

static void isp3_module_done(struct rkisp_isp_stats_vdev *stats_vdev,
			     u32 reg, u32 value)
{
	void __iomem *base = stats_vdev->dev->hw_dev->base_addr;

	writel(value, base + reg);
}

static u32 isp3_stats_read(struct rkisp_isp_stats_vdev *stats_vdev, u32 addr)
{
	return rkisp_read(stats_vdev->dev, addr, true);
}

static void isp3_stats_write(struct rkisp_isp_stats_vdev *stats_vdev,
			     u32 addr, u32 value)
{
	rkisp_write(stats_vdev->dev, addr, value, true);
}

static int
rkisp_stats_get_vsm_stats(struct rkisp_isp_stats_vdev *stats_vdev,
			  struct rkisp32_isp_stat_buffer *pbuf)
{
	struct isp32_vsm_stat *vsm;
	u32 value;

	if (!pbuf)
		return 0;

	vsm = &pbuf->params.vsm;
	if (isp3_stats_read(stats_vdev, ISP32_VSM_MODE)) {
		value = isp3_stats_read(stats_vdev, ISP32_VSM_DELTA_H);
		vsm->delta_h = value;
		value = isp3_stats_read(stats_vdev, ISP32_VSM_DELTA_V);
		vsm->delta_v = value;
		pbuf->meas_type |= ISP32_STAT_VSM;
	}
	return 0;
}

static int
rkisp_stats_get_bls_stats(struct rkisp_isp_stats_vdev *stats_vdev, void *pbuf)
{
	struct rkisp_device *dev = stats_vdev->dev;
	struct ispsd_in_fmt in_fmt = dev->isp_sdev.in_fmt;
	enum rkisp_fmt_raw_pat_type raw_type = in_fmt.bayer_pat;
	struct isp2x_bls_stat *bls;
	u32 value;

	if (!pbuf)
		return 0;

	value = isp3_stats_read(stats_vdev, ISP3X_BLS_CTRL);
	if (value & (ISP_BLS_ENA | ISP_BLS_MODE_MEASURED)) {
		if (dev->isp_ver == ISP_V32) {
			struct rkisp32_isp_stat_buffer *p = pbuf;

			bls = &p->params.bls;
			p->meas_type |= ISP32_STAT_BLS;
		} else {
			struct rkisp32_lite_stat_buffer *p = pbuf;

			bls = &p->params.bls;
			p->meas_type |= ISP32_STAT_BLS;
		}

		switch (raw_type) {
		case RAW_BGGR:
			bls->meas_r = isp3_stats_read(stats_vdev, ISP3X_BLS_D_MEASURED);
			bls->meas_gr = isp3_stats_read(stats_vdev, ISP3X_BLS_C_MEASURED);
			bls->meas_gb = isp3_stats_read(stats_vdev, ISP3X_BLS_B_MEASURED);
			bls->meas_b = isp3_stats_read(stats_vdev, ISP3X_BLS_A_MEASURED);
			break;
		case RAW_GBRG:
			bls->meas_r = isp3_stats_read(stats_vdev, ISP3X_BLS_C_MEASURED);
			bls->meas_gr = isp3_stats_read(stats_vdev, ISP3X_BLS_D_MEASURED);
			bls->meas_gb = isp3_stats_read(stats_vdev, ISP3X_BLS_A_MEASURED);
			bls->meas_b = isp3_stats_read(stats_vdev, ISP3X_BLS_B_MEASURED);
			break;
		case RAW_GRBG:
			bls->meas_r = isp3_stats_read(stats_vdev, ISP3X_BLS_B_MEASURED);
			bls->meas_gr = isp3_stats_read(stats_vdev, ISP3X_BLS_A_MEASURED);
			bls->meas_gb = isp3_stats_read(stats_vdev, ISP3X_BLS_D_MEASURED);
			bls->meas_b = isp3_stats_read(stats_vdev, ISP3X_BLS_C_MEASURED);
			break;
		case RAW_RGGB:
			bls->meas_r = isp3_stats_read(stats_vdev, ISP3X_BLS_A_MEASURED);
			bls->meas_gr = isp3_stats_read(stats_vdev, ISP3X_BLS_B_MEASURED);
			bls->meas_gb = isp3_stats_read(stats_vdev, ISP3X_BLS_C_MEASURED);
			bls->meas_b = isp3_stats_read(stats_vdev, ISP3X_BLS_D_MEASURED);
			break;
		default:
			break;
		}
	}
	return 0;
}

static int
rkisp_stats_get_dhaz_stats(struct rkisp_isp_stats_vdev *stats_vdev, void *pbuf)
{
	struct rkisp_device *dev = stats_vdev->dev;
	struct isp3x_dhaz_stat *dhaz;
	u32 value, i;

	if (!pbuf)
		return 0;

	value = isp3_stats_read(stats_vdev, ISP3X_DHAZ_CTRL);
	if (value & ISP_DHAZ_ENMUX) {
		if (dev->isp_ver == ISP_V32) {
			struct rkisp32_isp_stat_buffer *p = pbuf;

			dhaz = &p->params.dhaz;
			p->meas_type |= ISP32_STAT_DHAZ;
		} else {
			struct rkisp32_lite_stat_buffer *p = pbuf;

			dhaz = &p->params.dhaz;
			p->meas_type |= ISP32_STAT_DHAZ;
		}

		value = isp3_stats_read(stats_vdev, ISP3X_DHAZ_SUMH_RD);
		dhaz->dhaz_pic_sumh = value;

		value = isp3_stats_read(stats_vdev, ISP3X_DHAZ_ADP_RD0);
		dhaz->dhaz_adp_air_base = value >> 16;
		dhaz->dhaz_adp_wt = value & 0xFFFF;

		value = isp3_stats_read(stats_vdev, ISP3X_DHAZ_ADP_RD1);
		dhaz->dhaz_adp_gratio = value >> 16;
		dhaz->dhaz_adp_tmax = value & 0xFFFF;

		for (i = 0; i < ISP3X_DHAZ_HIST_IIR_NUM / 2; i++) {
			value = isp3_stats_read(stats_vdev, ISP3X_DHAZ_HIST_REG0 + 4 * i);
			dhaz->h_rgb_iir[2 * i] = value & 0xFFFF;
			dhaz->h_rgb_iir[2 * i + 1] = value >> 16;
		}
	}
	return 0;
}

static int
rkisp_stats_get_rawawb_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp32_isp_stat_buffer *pbuf)
{
	u32 ctrl = isp3_stats_read(stats_vdev, ISP3X_RAWAWB_CTRL);

	if (!(ctrl & ISP32_3A_MEAS_DONE)) {
		v4l2_dbg(1, rkisp_debug, &stats_vdev->dev->v4l2_dev,
			 "%s fail, ctrl:0x%x\n", __func__, ctrl);
		return -ENODATA;
	}

	if (!pbuf)
		goto out;

	pbuf->meas_type |= ISP32_STAT_RAWAWB;
out:
	isp3_module_done(stats_vdev, ISP3X_RAWAWB_CTRL, ctrl);
	return 0;
}

static int
rkisp_stats_get_rawaf_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
			       struct rkisp32_isp_stat_buffer *pbuf)
{
	struct isp32_rawaf_stat *af;
	u32 ctrl;

	ctrl = isp3_stats_read(stats_vdev, ISP3X_RAWAF_CTRL);
	if (!(ctrl & ISP32_3A_MEAS_DONE)) {
		v4l2_dbg(1, rkisp_debug, &stats_vdev->dev->v4l2_dev,
			 "%s fail, ctrl:0x%x\n", __func__, ctrl);
		return -ENODATA;
	}

	if (!pbuf || stats_vdev->af_meas_done_next)
		goto out;

	af = &pbuf->params.rawaf;
	pbuf->meas_type |= ISP32_STAT_RAWAF;

	af->afm_sum_b = isp3_stats_read(stats_vdev, ISP3X_RAWAF_SUM_B);
	af->afm_lum_b = isp3_stats_read(stats_vdev, ISP3X_RAWAF_LUM_B);
	af->int_state = isp3_stats_read(stats_vdev, ISP3X_RAWAF_INT_STATE);
	af->highlit_cnt_winb = isp3_stats_read(stats_vdev, ISP3X_RAWAF_HIGHLIT_CNT_WINB);

out:
	/* af should not clean mease done during isp working for af_ae_mode */
	stats_vdev->af_meas_done_next = false;
	if ((ctrl & ISP3X_RAWAF_AE_MODE) &&
	    (isp3_stats_read(stats_vdev, ISP3X_DPCC0_BASE) & ISP3X_DPCC_WORKING))
		stats_vdev->af_meas_done_next = true;
	else
		isp3_module_done(stats_vdev, ISP3X_RAWAF_CTRL, ctrl);
	return 0;
}

static int
rkisp_stats_get_rawaebig_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				  struct rkisp32_isp_stat_buffer *pbuf,
				  u32 blk_no)
{
	struct isp32_rawaebig_stat1 *ae = NULL;
	u32 i, base, addr, ctrl, meas_type;

	switch (blk_no) {
	case 1:
		base = RAWAE_BIG2_BASE;
		meas_type = ISP32_STAT_RAWAE1;
		if (pbuf)
			ae = &pbuf->params.rawae1_1;
		break;
	case 2:
		base = RAWAE_BIG3_BASE;
		meas_type = ISP32_STAT_RAWAE2;
		if (pbuf)
			ae = &pbuf->params.rawae2_1;
		break;
	default:
		base = RAWAE_BIG1_BASE;
		meas_type = ISP32_STAT_RAWAE3;
		if (pbuf)
			ae = &pbuf->params.rawae3_1;
		break;
	}

	ctrl = isp3_stats_read(stats_vdev, base + ISP3X_RAWAE_BIG_CTRL);
	if (!(ctrl & ISP32_3A_MEAS_DONE)) {
		v4l2_dbg(1, rkisp_debug, &stats_vdev->dev->v4l2_dev,
			 "%s fail, addr:0x%x ctrl:0x%x\n",
			 __func__, base, ctrl);
		return -ENODATA;
	}

	if (!ae || stats_vdev->ae_meas_done_next)
		goto out;

	for (i = 0; i < ISP32_RAWAEBIG_SUBWIN_NUM; i++) {
		addr = base + ISP3X_RAWAE_BIG_WND1_SUMR + i * 4;
		ae->sumr[i] = isp3_stats_read(stats_vdev, addr);
		addr = base + ISP3X_RAWAE_BIG_WND1_SUMG + i * 4;
		ae->sumg[i] = isp3_stats_read(stats_vdev, addr);
		addr = base + ISP3X_RAWAE_BIG_WND1_SUMB + i * 4;
		ae->sumb[i] = isp3_stats_read(stats_vdev, addr);
	}

	pbuf->meas_type |= meas_type;

out:
	/* ae should not clean mease done during isp working for af_ae_mode */
	if (blk_no == 0 && stats_vdev->af_meas_done_next) {
		stats_vdev->ae_meas_done_next = true;
	} else {
		isp3_module_done(stats_vdev, base + ISP3X_RAWAE_BIG_CTRL, ctrl);
		if (blk_no == 0)
			stats_vdev->ae_meas_done_next = false;
	}
	return 0;
}

static int
rkisp_stats_get_rawhstbig_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				   struct rkisp32_isp_stat_buffer *pbuf,
				   u32 blk_no)
{
	u32 addr, ctrl, meas_type;

	switch (blk_no) {
	case 1:
		addr = ISP3X_RAWHIST_BIG2_BASE;
		meas_type = ISP32_STAT_RAWHST1;
		break;
	case 2:
		addr = ISP3X_RAWHIST_BIG3_BASE;
		meas_type = ISP32_STAT_RAWHST2;
		break;
	case 0:
	default:
		addr = ISP3X_RAWHIST_BIG1_BASE;
		meas_type = ISP32_STAT_RAWHST3;
		break;
	}

	ctrl = isp3_stats_read(stats_vdev, addr + ISP3X_RAWHIST_BIG_CTRL);
	if (!(ctrl & ISP32_3A_MEAS_DONE)) {
		v4l2_dbg(1, rkisp_debug, &stats_vdev->dev->v4l2_dev,
			 "%s fail, addr:0x%x ctrl:0x%x\n",
			 __func__, addr, ctrl);
		return -ENODATA;
	}

	if (!pbuf)
		goto out;

	pbuf->meas_type |= meas_type;
out:
	isp3_module_done(stats_vdev, addr + ISP3X_RAWHIST_BIG_CTRL, ctrl);
	return 0;
}

static int
rkisp_stats_get_rawae1_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp32_isp_stat_buffer *pbuf)
{
	return rkisp_stats_get_rawaebig_meas_ddr(stats_vdev, pbuf, 1);
}

static int
rkisp_stats_get_rawhst1_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				 struct rkisp32_isp_stat_buffer *pbuf)
{
	return rkisp_stats_get_rawhstbig_meas_ddr(stats_vdev, pbuf, 1);
}

static int
rkisp_stats_get_rawae2_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp32_isp_stat_buffer *pbuf)
{
	return rkisp_stats_get_rawaebig_meas_ddr(stats_vdev, pbuf, 2);
}

static int
rkisp_stats_get_rawhst2_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				 struct rkisp32_isp_stat_buffer *pbuf)
{
	return rkisp_stats_get_rawhstbig_meas_ddr(stats_vdev, pbuf, 2);
}

static int
rkisp_stats_get_rawae3_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp32_isp_stat_buffer *pbuf)
{
	return rkisp_stats_get_rawaebig_meas_ddr(stats_vdev, pbuf, 0);
}

static int
rkisp_stats_get_rawhst3_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				 struct rkisp32_isp_stat_buffer *pbuf)
{
	return rkisp_stats_get_rawhstbig_meas_ddr(stats_vdev, pbuf, 0);
}

static int
rkisp_stats_get_rawaelite_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				   struct rkisp32_isp_stat_buffer *pbuf)
{
	u32 ctrl;

	ctrl = isp3_stats_read(stats_vdev, ISP3X_RAWAE_LITE_CTRL);
	if ((ctrl & ISP32_3A_MEAS_DONE) == 0) {
		v4l2_dbg(1, rkisp_debug, &stats_vdev->dev->v4l2_dev,
			 "%s fail, ctrl:0x%x\n", __func__, ctrl);
		return -ENODATA;
	}

	if (!pbuf)
		goto out;

	pbuf->meas_type |= ISP32_STAT_RAWAE0;

out:
	isp3_module_done(stats_vdev, ISP3X_RAWAE_LITE_CTRL, ctrl);
	return 0;
}

static int
rkisp_stats_get_rawhstlite_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				    struct rkisp32_isp_stat_buffer *pbuf)
{
	u32 ctrl;

	ctrl = isp3_stats_read(stats_vdev, ISP3X_RAWHIST_LITE_CTRL);
	if ((ctrl & ISP32_3A_MEAS_DONE) == 0) {
		v4l2_dbg(1, rkisp_debug, &stats_vdev->dev->v4l2_dev,
			 "%s fail, ctrl:0x%x\n", __func__, ctrl);
		return -ENODATA;
	}

	if (!pbuf)
		goto out;

	pbuf->meas_type |= ISP32_STAT_RAWHST0;

out:
	isp3_module_done(stats_vdev, ISP3X_RAWHIST_LITE_CTRL, ctrl);
	return 0;
}

static struct rkisp_stats_ops_v32 __maybe_unused stats_ddr_ops_v32 = {
	.get_rawawb_meas = rkisp_stats_get_rawawb_meas_ddr,
	.get_rawaf_meas = rkisp_stats_get_rawaf_meas_ddr,
	.get_rawae0_meas = rkisp_stats_get_rawaelite_meas_ddr,
	.get_rawhst0_meas = rkisp_stats_get_rawhstlite_meas_ddr,
	.get_rawae1_meas = rkisp_stats_get_rawae1_meas_ddr,
	.get_rawhst1_meas = rkisp_stats_get_rawhst1_meas_ddr,
	.get_rawae2_meas = rkisp_stats_get_rawae2_meas_ddr,
	.get_rawhst2_meas = rkisp_stats_get_rawhst2_meas_ddr,
	.get_rawae3_meas = rkisp_stats_get_rawae3_meas_ddr,
	.get_rawhst3_meas = rkisp_stats_get_rawhst3_meas_ddr,
	.get_bls_stats = rkisp_stats_get_bls_stats,
	.get_dhaz_stats = rkisp_stats_get_dhaz_stats,
	.get_vsm_stats = rkisp_stats_get_vsm_stats,
};

static void
rkisp_stats_update_buf(struct rkisp_isp_stats_vdev *stats_vdev)
{
	struct rkisp_device *dev = stats_vdev->dev;
	struct rkisp_buffer *buf;
	unsigned long flags;
	u32 size = stats_vdev->vdev_fmt.fmt.meta.buffersize;
	u32 val = 0;

	spin_lock_irqsave(&stats_vdev->rd_lock, flags);
	if (!stats_vdev->nxt_buf && !list_empty(&stats_vdev->stat)) {
		buf = list_first_entry(&stats_vdev->stat,
				       struct rkisp_buffer, queue);
		list_del(&buf->queue);
		stats_vdev->nxt_buf = buf;
	}
	spin_unlock_irqrestore(&stats_vdev->rd_lock, flags);

	if (stats_vdev->nxt_buf) {
		val = stats_vdev->nxt_buf->buff_addr[0];
		v4l2_dbg(2, rkisp_debug, &dev->v4l2_dev,
			 "%s BASE:0x%x SHD:0x%x\n",
			 __func__, val,
			 isp3_stats_read(stats_vdev, ISP3X_MI_3A_WR_BASE));
		if (!dev->hw_dev->is_single) {
			stats_vdev->cur_buf = stats_vdev->nxt_buf;
			stats_vdev->nxt_buf = NULL;
		}
	} else if (stats_vdev->stats_buf[0].mem_priv) {
		val = stats_vdev->stats_buf[0].dma_addr;
	}

	if (val) {
		rkisp_write(dev, ISP3X_MI_3A_WR_BASE, val, false);
		if (dev->hw_dev->unite)
			rkisp_next_write(dev, ISP3X_MI_3A_WR_BASE, val + size / 2, false);
	}
}

static void
rkisp_stats_info2ddr(struct rkisp_isp_stats_vdev *stats_vdev, void *pbuf)
{
	struct rkisp_device *dev = stats_vdev->dev;
	struct rkisp_isp_params_val_v32 *priv_val;
	struct rkisp_dummy_buffer *buf;
	int idx, buf_fd = -1;
	u32 reg = 0, ctrl;

	priv_val = (struct rkisp_isp_params_val_v32 *)dev->params_vdev.priv_val;
	if (!priv_val->buf_info_owner && priv_val->buf_info_idx >= 0) {
		priv_val->buf_info_idx = -1;
		rkisp_clear_bits(dev, ISP3X_GAIN_CTRL, ISP3X_GAIN_2DDR_EN, false);
		rkisp_clear_bits(dev, ISP3X_RAWAWB_CTRL, ISP32_RAWAWB_2DDR_PATH_EN, false);
		return;
	}

	if (priv_val->buf_info_owner == RKISP_INFO2DRR_OWNER_GAIN) {
		reg = ISP3X_GAIN_CTRL;
		ctrl = ISP3X_GAIN_2DDR_EN;
	} else {
		reg = ISP3X_RAWAWB_CTRL;
		ctrl = ISP32_RAWAWB_2DDR_PATH_EN;
	}

	idx = priv_val->buf_info_idx;
	if (idx >= 0) {
		buf = &priv_val->buf_info[idx];
		rkisp_finish_buffer(dev, buf);
		if (*(u32 *)buf->vaddr != RKISP_INFO2DDR_BUF_INIT && pbuf &&
		    (reg != ISP3X_RAWAWB_CTRL ||
		     !(rkisp_read(dev, reg, true) & ISP32_RAWAWB_2DDR_PATH_ERR))) {
			if (dev->isp_ver == ISP_V32) {
				struct rkisp32_isp_stat_buffer *p = pbuf;

				p->params.info2ddr.buf_fd = buf->dma_fd;
				p->params.info2ddr.owner = priv_val->buf_info_owner;
				p->meas_type |= ISP32_STAT_INFO2DDR;
			} else {
				struct rkisp32_lite_stat_buffer *p = pbuf;

				p->params.info2ddr.buf_fd = buf->dma_fd;
				p->params.info2ddr.owner = priv_val->buf_info_owner;
				p->meas_type |= ISP32_STAT_INFO2DDR;
			}
			buf_fd = buf->dma_fd;
		} else if (reg == ISP3X_RAWAWB_CTRL &&
			   rkisp_read(dev, reg, true) & ISP32_RAWAWB_2DDR_PATH_ERR) {
			v4l2_warn(&dev->v4l2_dev, "rawawb2ddr path error idx:%d\n", idx);
		}

		if (buf_fd == -1)
			return;
	}
	/* get next unused buf to hw */
	for (idx = 0; idx < priv_val->buf_info_cnt; idx++) {
		buf = &priv_val->buf_info[idx];
		if (*(u32 *)buf->vaddr == RKISP_INFO2DDR_BUF_INIT)
			break;
	}

	if (idx == priv_val->buf_info_cnt) {
		rkisp_clear_bits(dev, reg, ctrl, false);
		priv_val->buf_info_idx = -1;
	} else {
		buf = &priv_val->buf_info[idx];
		rkisp_write(dev, ISP3X_MI_GAIN_WR_BASE, buf->dma_addr, false);
		if (dev->hw_dev->is_single)
			rkisp_write(dev, ISP3X_MI_WR_CTRL2, ISP3X_GAINSELF_UPD, true);
		if (priv_val->buf_info_idx < 0)
			rkisp_set_bits(dev, reg, 0, ctrl, false);
		priv_val->buf_info_idx = idx;
	}
}

static void
rkisp_stats_send_meas(struct rkisp_isp_stats_vdev *stats_vdev,
		      struct rkisp_isp_readout_work *meas_work)
{
	struct rkisp_device *dev = stats_vdev->dev;
	struct rkisp_hw_dev *hw = dev->hw_dev;
	struct rkisp_isp_params_vdev *params_vdev = &dev->params_vdev;
	struct rkisp_buffer *cur_buf = stats_vdev->cur_buf;
	struct rkisp32_isp_stat_buffer *cur_stat_buf = NULL;
	struct rkisp_stats_ops_v32 *ops =
		(struct rkisp_stats_ops_v32 *)stats_vdev->priv_ops;
	u32 size = stats_vdev->vdev_fmt.fmt.meta.buffersize;
	u32 cur_frame_id = meas_work->frame_id;
	bool is_dummy = false;
	unsigned long flags;

	if (!stats_vdev->rdbk_drop) {
		if (!cur_buf && stats_vdev->stats_buf[0].mem_priv) {
			rkisp_finish_buffer(stats_vdev->dev, &stats_vdev->stats_buf[0]);
			cur_stat_buf = stats_vdev->stats_buf[0].vaddr;
			cur_stat_buf->frame_id = cur_frame_id;
			cur_stat_buf->params_id = params_vdev->cur_frame_id;
			is_dummy = true;
		} else if (cur_buf) {
			cur_stat_buf = cur_buf->vaddr[0];
			cur_stat_buf->frame_id = cur_frame_id;
			cur_stat_buf->params_id = params_vdev->cur_frame_id;
		}

		/* buffer done when frame of right handle */
		if (hw->unite == ISP_UNITE_ONE) {
			if (dev->unite_index == ISP_UNITE_LEFT) {
				cur_buf = NULL;
				is_dummy = false;
			} else if (cur_stat_buf) {
				cur_stat_buf = (void *)cur_stat_buf + size / 2;
				cur_stat_buf->frame_id = cur_frame_id;
				cur_stat_buf->params_id = params_vdev->cur_frame_id;
			}
		}

		if (hw->unite != ISP_UNITE_ONE || dev->unite_index == ISP_UNITE_RIGHT) {
			/* config buf for next frame */
			stats_vdev->cur_buf = NULL;
			if (stats_vdev->nxt_buf) {
				stats_vdev->cur_buf = stats_vdev->nxt_buf;
				stats_vdev->nxt_buf = NULL;
			}
			rkisp_stats_update_buf(stats_vdev);
		}
	} else {
		cur_buf = NULL;
	}

	if (meas_work->isp3a_ris & ISP3X_3A_RAWAWB)
		ops->get_rawawb_meas(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWAF ||
	    stats_vdev->af_meas_done_next)
		ops->get_rawaf_meas(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWAE_BIG ||
	    stats_vdev->ae_meas_done_next)
		ops->get_rawae3_meas(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWHIST_BIG)
		ops->get_rawhst3_meas(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWAE_CH0)
		ops->get_rawae0_meas(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWAE_CH1)
		ops->get_rawae1_meas(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWAE_CH2)
		ops->get_rawae2_meas(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWHIST_CH0)
		ops->get_rawhst0_meas(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWHIST_CH1)
		ops->get_rawhst1_meas(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWHIST_CH2)
		ops->get_rawhst2_meas(stats_vdev, cur_stat_buf);

	if (meas_work->isp_ris & ISP3X_FRAME) {
		ops->get_bls_stats(stats_vdev, cur_stat_buf);
		ops->get_dhaz_stats(stats_vdev, cur_stat_buf);
		ops->get_vsm_stats(stats_vdev, cur_stat_buf);
	}

	if (cur_stat_buf && stats_vdev->dev->is_first_double)
		cur_stat_buf->meas_type |= ISP32_STAT_RTT_FST;

	if (is_dummy) {
		spin_lock_irqsave(&stats_vdev->rd_lock, flags);
		if (!list_empty(&stats_vdev->stat)) {
			cur_buf = list_first_entry(&stats_vdev->stat, struct rkisp_buffer, queue);
			list_del(&cur_buf->queue);
		}
		spin_unlock_irqrestore(&stats_vdev->rd_lock, flags);
		if (cur_buf) {
			memcpy(cur_buf->vaddr[0], stats_vdev->stats_buf[0].vaddr, size);
			cur_stat_buf = cur_buf->vaddr[0];
		}
	}
	if (cur_buf && cur_stat_buf) {
		cur_stat_buf->frame_id = cur_frame_id;
		cur_stat_buf->params_id = params_vdev->cur_frame_id;
		cur_stat_buf->params.info2ddr.buf_fd = -1;
		cur_stat_buf->params.info2ddr.owner = 0;
		rkisp_stats_info2ddr(stats_vdev, cur_stat_buf);

		vb2_set_plane_payload(&cur_buf->vb.vb2_buf, 0, size);
		cur_buf->vb.sequence = cur_frame_id;
		cur_buf->vb.vb2_buf.timestamp = meas_work->timestamp;
		vb2_buffer_done(&cur_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}
	v4l2_dbg(4, rkisp_debug, &dev->v4l2_dev,
		 "%s id:%d seq:%d params_id:%d ris:0x%x buf:%p meas_type:0x%x\n",
		 __func__, dev->unite_index,
		 cur_frame_id, params_vdev->cur_frame_id, meas_work->isp3a_ris,
		 cur_buf, !cur_stat_buf ? 0 : cur_stat_buf->meas_type);
}

static int
rkisp_stats_get_rawawb_meas_lite(struct rkisp_isp_stats_vdev *stats_vdev,
				 struct rkisp32_lite_stat_buffer *pbuf)
{
	struct isp32_lite_rawawb_meas_stat *awb;
	u32 i, val, ctrl = isp3_stats_read(stats_vdev, ISP3X_RAWAWB_CTRL);

	if (!(ctrl & ISP32_3A_MEAS_DONE)) {
		v4l2_dbg(1, rkisp_debug, &stats_vdev->dev->v4l2_dev,
			 "%s fail, ctrl:0x%x\n", __func__, ctrl);
		return -ENODATA;
	}

	if (!pbuf)
		goto out;
	awb = &pbuf->params.rawawb;
	for (i = 0; i < ISP32_RAWAWB_SUM_NUM; i++) {
		val = isp3_stats_read(stats_vdev, ISP3X_RAWAWB_SUM_RGAIN_NOR_0 + 0x30 * i);
		awb->sum[i].rgain_nor = val;
		val = isp3_stats_read(stats_vdev, ISP3X_RAWAWB_SUM_BGAIN_NOR_0 + 0x30 * i);
		awb->sum[i].bgain_nor = val;
		val = isp3_stats_read(stats_vdev, ISP3X_RAWAWB_WP_NUM_NOR_0 + 0x30 * i);
		awb->sum[i].wp_num_nor = val;

		val = isp3_stats_read(stats_vdev, ISP3X_RAWAWB_SUM_RGAIN_BIG_0 + 0x30 * i);
		awb->sum[i].rgain_big = val;
		val = isp3_stats_read(stats_vdev, ISP3X_RAWAWB_SUM_BGAIN_BIG_0 + 0x30 * i);
		awb->sum[i].bgain_big = val;
		val = isp3_stats_read(stats_vdev, ISP3X_RAWAWB_WP_NUM_BIG_0 + 0x30 * i);
		awb->sum[i].wp_num_big = val;

		val = isp3_stats_read(stats_vdev, ISP3X_RAWAWB_WPNUM2_0 + 4 * i);
		awb->sum[i].wp_num2 = val;
	}

	for (i = 0; i < ISP32_RAWAWB_EXCL_STAT_NUM; i++) {
		val = isp3_stats_read(stats_vdev, ISP3X_RAWAWB_SUM_R_EXC0 + 0x10 * i);
		awb->sum_exc[i].rgain_exc = val;
		val = isp3_stats_read(stats_vdev, ISP3X_RAWAWB_SUM_B_EXC0 + 0x10 * i);
		awb->sum_exc[i].bgain_exc = val;
		val = isp3_stats_read(stats_vdev, ISP3X_RAWAWB_WP_NM_EXC0 + 0x10 * i);
		awb->sum_exc[i].wp_num_exc = val;
	}

	for (i = 0; i < ISP32_RAWAWB_HSTBIN_NUM / 2; i++) {
		val = isp3_stats_read(stats_vdev, ISP3X_RAWAWB_Y_HIST01 + 4 * i);
		awb->yhist_bin[2 * i] = val & 0xffff;
		awb->yhist_bin[2 * i + 1] = (val >> 16) & 0xffff;
	}

	/* RAMDATA R/G/B/WP */
	for (i = 0; i < ISP32L_RAWAWB_RAMDATA_RGB_NUM; i++) {
		val = isp3_stats_read(stats_vdev, ISP3X_RAWAWB_RAM_DATA_BASE);
		awb->ramdata_r[i] = val & 0x1fffff;
		awb->ramdata_g[i] = (val >> 21) & 0x7ff;
		val = isp3_stats_read(stats_vdev, ISP3X_RAWAWB_RAM_DATA_BASE);
		awb->ramdata_g[i] |= ((val & 0x3ff) << 11);
		awb->ramdata_b[i] = (val >> 10) & 0x1fffff;
	}
	for (i = 0; i < ISP32L_RAWAWB_RAMDATA_WP_NUM; i++) {
		val = isp3_stats_read(stats_vdev, ISP3X_RAWAWB_RAM_DATA_BASE);
		awb->ramdata_wpnum0[i] = val & 0x3fff;
		awb->ramdata_wpnum1[i] = (val >> 16) & 0x3fff;
	}

	pbuf->meas_type |= ISP32_STAT_RAWAWB;
out:
	isp3_module_done(stats_vdev, ISP3X_RAWAWB_CTRL, ctrl);
	return 0;
}

static int
rkisp_stats_get_rawaf_meas_lite(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp32_lite_stat_buffer *pbuf)
{
	struct isp32_lite_rawaf_stat *af;
	u32 i, val, ctrl;

	ctrl = isp3_stats_read(stats_vdev, ISP3X_RAWAF_CTRL);
	if (!(ctrl & ISP32_3A_MEAS_DONE)) {
		v4l2_dbg(1, rkisp_debug, &stats_vdev->dev->v4l2_dev,
			 "%s fail, ctrl:0x%x\n", __func__, ctrl);
		return -ENODATA;
	}

	if (!pbuf || stats_vdev->af_meas_done_next)
		goto out;

	af = &pbuf->params.rawaf;
	af->afm_sum_b = isp3_stats_read(stats_vdev, ISP3X_RAWAF_SUM_B);
	af->afm_lum_b = isp3_stats_read(stats_vdev, ISP3X_RAWAF_LUM_B);
	af->int_state = isp3_stats_read(stats_vdev, ISP3X_RAWAF_INT_STATE);
	af->highlit_cnt_winb = isp3_stats_read(stats_vdev, ISP3X_RAWAF_HIGHLIT_CNT_WINB);
	/* hiir: first 25 word, viir: remaining 25 word */
	for (i = 0; i < ISP32L_RAWAF_WND_DATA; i++) {
		val = isp3_stats_read(stats_vdev, ISP3X_RAWAF_RAM_DATA);
		af->ramdata.hiir_wnd_data[i] = val;
	}
	for (i = 0; i < ISP32L_RAWAF_WND_DATA; i++) {
		val = isp3_stats_read(stats_vdev, ISP3X_RAWAF_RAM_DATA);
		af->ramdata.viir_wnd_data[i] = val;
	}

	pbuf->meas_type |= ISP32_STAT_RAWAF;
out:
	/* af should not clean mease done during isp working for af_ae_mode */
	stats_vdev->af_meas_done_next = false;
	if ((ctrl & ISP3X_RAWAF_AE_MODE) &&
	    (isp3_stats_read(stats_vdev, ISP3X_DPCC0_BASE) & ISP3X_DPCC_WORKING))
		stats_vdev->af_meas_done_next = true;
	else
		isp3_module_done(stats_vdev, ISP3X_RAWAF_CTRL, ctrl);
	return 0;
}

static int
rkisp_stats_get_rawae3_meas_lite(struct rkisp_isp_stats_vdev *stats_vdev,
				 struct rkisp32_lite_stat_buffer *pbuf)
{
	struct isp32_lite_rawaebig_stat *ae = NULL;
	u32 i, val, addr, ctrl, base = RAWAE_BIG1_BASE;

	ctrl = isp3_stats_read(stats_vdev, base + ISP3X_RAWAE_BIG_CTRL);
	if (!(ctrl & ISP32_3A_MEAS_DONE)) {
		v4l2_dbg(1, rkisp_debug, &stats_vdev->dev->v4l2_dev,
			 "%s fail, addr:0x%x ctrl:0x%x\n",
			 __func__, base, ctrl);
		return -ENODATA;
	}

	if (!pbuf || stats_vdev->ae_meas_done_next)
		goto out;

	ae = &pbuf->params.rawae3;
	addr = base + ISP3X_RAWAE_BIG_WND1_SUMR;
	ae->sumr = isp3_stats_read(stats_vdev, addr);
	addr = base + ISP3X_RAWAE_BIG_WND1_SUMG;
	ae->sumg = isp3_stats_read(stats_vdev, addr);
	addr = base + ISP3X_RAWAE_BIG_WND1_SUMB;
	ae->sumb = isp3_stats_read(stats_vdev, addr);

	addr = base + ISP3X_RAWAE_BIG_RO_MEAN_BASE_ADDR;
	for (i = 0; i < ISP32_RAWAEBIG_MEAN_NUM; i++) {
		val = isp3_stats_read(stats_vdev, addr);
		ae->data[i].channelg_xy = val & 0xfff;
		ae->data[i].channelb_xy = (val >> 12) & 0x3ff;
		ae->data[i].channelr_xy = (val >> 22) & 0x3ff;
	}

	pbuf->meas_type |= ISP32_STAT_RAWAE3;
out:
	/* ae should not clean mease done during isp working for af_ae_mode */
	if (stats_vdev->af_meas_done_next) {
		stats_vdev->ae_meas_done_next = true;
	} else {
		isp3_module_done(stats_vdev, base + ISP3X_RAWAE_BIG_CTRL, ctrl);
		stats_vdev->ae_meas_done_next = false;
	}
	return 0;
}

static int
rkisp_stats_get_rawhst3_meas_lite(struct rkisp_isp_stats_vdev *stats_vdev,
				  struct rkisp32_lite_stat_buffer *pbuf)
{
	struct isp2x_rawhistbig_stat *hst;
	u32 i, ctrl, addr, base = ISP3X_RAWHIST_BIG1_BASE;

	ctrl = isp3_stats_read(stats_vdev, base + ISP3X_RAWHIST_BIG_CTRL);
	if (!(ctrl & ISP32_3A_MEAS_DONE)) {
		v4l2_dbg(1, rkisp_debug, &stats_vdev->dev->v4l2_dev,
			 "%s fail, addr:0x%x ctrl:0x%x\n",
			 __func__, base, ctrl);
		return -ENODATA;
	}

	if (!pbuf)
		goto out;
	hst = &pbuf->params.rawhist3;
	addr = base + ISP3X_RAWHIST_BIG_RO_BASE_BIN;
	for (i = 0; i < ISP3X_HIST_BIN_N_MAX; i++)
		hst->hist_bin[i] = isp3_stats_read(stats_vdev, addr);

	pbuf->meas_type |= ISP32_STAT_RAWHST3;
out:
	isp3_module_done(stats_vdev, base + ISP3X_RAWHIST_BIG_CTRL, ctrl);
	return 0;
}

static int
rkisp_stats_get_rawaelite_meas_lite(struct rkisp_isp_stats_vdev *stats_vdev,
				    struct rkisp32_lite_stat_buffer *pbuf)
{
	struct isp2x_rawaelite_stat *ae;
	u32 i, val, ctrl;

	ctrl = isp3_stats_read(stats_vdev, ISP3X_RAWAE_LITE_CTRL);
	if ((ctrl & ISP32_3A_MEAS_DONE) == 0) {
		v4l2_dbg(1, rkisp_debug, &stats_vdev->dev->v4l2_dev,
			 "%s fail, ctrl:0x%x\n", __func__, ctrl);
		return -ENODATA;
	}

	if (!pbuf)
		goto out;
	ae = &pbuf->params.rawae0;
	for (i = 0; i < ISP32_RAWAELITE_MEAN_NUM; i++) {
		val = isp3_stats_read(stats_vdev, ISP3X_RAWAE_LITE_RO_MEAN + 4 * i);
		ae->data[i].channelg_xy = val & 0xfff;
		ae->data[i].channelb_xy = (val >> 12) & 0x3ff;
		ae->data[i].channelr_xy = (val >> 22) & 0x3ff;
	}

	pbuf->meas_type |= ISP32_STAT_RAWAE0;
out:
	isp3_module_done(stats_vdev, ISP3X_RAWAE_LITE_CTRL, ctrl);
	return 0;
}

static int
rkisp_stats_get_rawhstlite_meas_lite(struct rkisp_isp_stats_vdev *stats_vdev,
				     struct rkisp32_lite_stat_buffer *pbuf)
{
	struct isp32_lite_rawhistlite_stat *hst;
	u32 i, ctrl;

	ctrl = isp3_stats_read(stats_vdev, ISP3X_RAWHIST_LITE_CTRL);
	if ((ctrl & ISP32_3A_MEAS_DONE) == 0) {
		v4l2_dbg(1, rkisp_debug, &stats_vdev->dev->v4l2_dev,
			 "%s fail, ctrl:0x%x\n", __func__, ctrl);
		return -ENODATA;
	}

	if (!pbuf)
		goto out;
	hst = &pbuf->params.rawhist0;
	for (i = 0; i < ISP32L_HIST_LITE_BIN_N_MAX; i++)
		hst->hist_bin[i] = isp3_stats_read(stats_vdev, ISP3X_RAWHIST_LITE_RO_BASE_BIN);

	pbuf->meas_type |= ISP32_STAT_RAWHST0;
out:
	isp3_module_done(stats_vdev, ISP3X_RAWHIST_LITE_CTRL, ctrl);
	return 0;
}

static void
rkisp_stats_send_meas_lite(struct rkisp_isp_stats_vdev *stats_vdev,
			   struct rkisp_isp_readout_work *meas_work)
{
	struct rkisp_device *dev = stats_vdev->dev;
	struct rkisp_isp_params_vdev *params_vdev = &dev->params_vdev;
	unsigned int cur_frame_id = meas_work->frame_id;
	struct rkisp_buffer *cur_buf = NULL;
	struct rkisp32_lite_stat_buffer *cur_stat_buf = NULL;
	u32 size = sizeof(struct rkisp32_lite_stat_buffer);

	spin_lock(&stats_vdev->rd_lock);
	if (!list_empty(&stats_vdev->stat)) {
		cur_buf = list_first_entry(&stats_vdev->stat, struct rkisp_buffer, queue);
		list_del(&cur_buf->queue);
	}
	spin_unlock(&stats_vdev->rd_lock);

	if (cur_buf) {
		cur_stat_buf = (struct rkisp32_lite_stat_buffer *)(cur_buf->vaddr[0]);
		cur_stat_buf->frame_id = cur_frame_id;
		cur_stat_buf->params_id = params_vdev->cur_frame_id;
		cur_stat_buf->params.info2ddr.buf_fd = -1;
		cur_stat_buf->params.info2ddr.owner = 0;
	}

	if (meas_work->isp3a_ris & ISP3X_3A_RAWAWB)
		rkisp_stats_get_rawawb_meas_lite(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWAF ||
	    stats_vdev->af_meas_done_next)
		rkisp_stats_get_rawaf_meas_lite(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWAE_BIG ||
	    stats_vdev->ae_meas_done_next)
		rkisp_stats_get_rawae3_meas_lite(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWHIST_BIG)
		rkisp_stats_get_rawhst3_meas_lite(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWAE_CH0)
		rkisp_stats_get_rawaelite_meas_lite(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWHIST_CH0)
		rkisp_stats_get_rawhstlite_meas_lite(stats_vdev, cur_stat_buf);

	if (meas_work->isp_ris & ISP3X_FRAME) {
		rkisp_stats_get_bls_stats(stats_vdev, cur_stat_buf);
		rkisp_stats_get_dhaz_stats(stats_vdev, cur_stat_buf);
	}

	if (cur_buf) {
		rkisp_stats_info2ddr(stats_vdev, cur_stat_buf);
		vb2_set_plane_payload(&cur_buf->vb.vb2_buf, 0, size);
		cur_buf->vb.sequence = cur_frame_id;
		cur_buf->vb.vb2_buf.timestamp = meas_work->timestamp;
		vb2_buffer_done(&cur_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}
	v4l2_dbg(4, rkisp_debug, &dev->v4l2_dev,
		 "%s seq:%d params_id:%d ris:0x%x buf:%p meas_type:0x%x\n",
		 __func__,
		 cur_frame_id, params_vdev->cur_frame_id, meas_work->isp_ris,
		 cur_buf, !cur_stat_buf ? 0 : cur_stat_buf->meas_type);
}

static void
rkisp_stats_send_meas_v32(struct rkisp_isp_stats_vdev *stats_vdev,
			  struct rkisp_isp_readout_work *meas_work)
{
	if (meas_work->isp_ris & ISP3X_AFM_SUM_OF)
		v4l2_warn(stats_vdev->vnode.vdev.v4l2_dev,
			  "ISP3X_AFM_SUM_OF\n");

	if (meas_work->isp_ris & ISP3X_AFM_LUM_OF)
		v4l2_warn(stats_vdev->vnode.vdev.v4l2_dev,
			  "ISP3X_AFM_LUM_OF\n");

	if (meas_work->isp3a_ris & ISP3X_3A_RAWAF_SUM)
		v4l2_warn(stats_vdev->vnode.vdev.v4l2_dev,
			  "ISP3X_3A_RAWAF_SUM\n");

	if (stats_vdev->dev->isp_ver == ISP_V32)
		rkisp_stats_send_meas(stats_vdev, meas_work);
	else
		rkisp_stats_send_meas_lite(stats_vdev, meas_work);
}

static void
rkisp_stats_isr_v32(struct rkisp_isp_stats_vdev *stats_vdev,
		    u32 isp_ris, u32 isp3a_ris)
{
	struct rkisp_isp_readout_work work;
	u32 iq_isr_mask = ISP3X_SIAWB_DONE | ISP3X_SIAF_FIN |
		ISP3X_EXP_END | ISP3X_SIHST_RDY | ISP3X_AFM_SUM_OF | ISP3X_AFM_LUM_OF;
	u32 cur_frame_id, isp_mis_tmp = 0;
	u32 temp_isp_ris, temp_isp3a_ris;

	rkisp_dmarx_get_frame(stats_vdev->dev, &cur_frame_id, NULL, NULL, true);

	spin_lock(&stats_vdev->irq_lock);

	temp_isp_ris = isp3_stats_read(stats_vdev, ISP3X_ISP_RIS);
	temp_isp3a_ris = isp3_stats_read(stats_vdev, ISP3X_ISP_3A_RIS);

	isp_mis_tmp = isp_ris & iq_isr_mask;
	if (isp_mis_tmp) {
		isp3_stats_write(stats_vdev, ISP3X_ISP_ICR, isp_mis_tmp);

		isp_mis_tmp &= isp3_stats_read(stats_vdev, ISP3X_ISP_MIS);
		if (isp_mis_tmp)
			v4l2_err(stats_vdev->vnode.vdev.v4l2_dev,
				 "isp icr 3A info err: 0x%x 0x%x\n",
				 isp_mis_tmp, isp_ris);
	}

	isp_mis_tmp = temp_isp3a_ris;
	if (isp_mis_tmp) {
		isp3_stats_write(stats_vdev, ISP3X_ISP_3A_ICR, isp_mis_tmp);

		isp_mis_tmp &= isp3_stats_read(stats_vdev, ISP3X_ISP_3A_MIS);
		if (isp_mis_tmp)
			v4l2_err(stats_vdev->vnode.vdev.v4l2_dev,
				 "isp3A icr 3A info err: 0x%x 0x%x\n",
				 isp_mis_tmp, isp3a_ris);
	}

	if (isp_ris & ISP3X_FRAME) {
		work.readout = RKISP_ISP_READOUT_MEAS;
		work.frame_id = cur_frame_id;
		work.isp_ris = temp_isp_ris | isp_ris;
		work.isp3a_ris = temp_isp3a_ris;
		work.timestamp = ktime_get_ns();
		rkisp_stats_send_meas_v32(stats_vdev, &work);
	}

	spin_unlock(&stats_vdev->irq_lock);
}

static void
rkisp_stats_rdbk_enable_v32(struct rkisp_isp_stats_vdev *stats_vdev, bool en)
{
	if (!en) {
		stats_vdev->isp_rdbk = 0;
		stats_vdev->isp3a_rdbk = 0;
	}

	stats_vdev->rdbk_mode = en;
}

static struct rkisp_isp_stats_ops rkisp_isp_stats_ops_tbl = {
	.isr_hdl = rkisp_stats_isr_v32,
	.send_meas = rkisp_stats_send_meas_v32,
	.rdbk_enable = rkisp_stats_rdbk_enable_v32,
};

void rkisp_stats_first_ddr_config_v32(struct rkisp_isp_stats_vdev *stats_vdev)
{
	struct rkisp_device *dev = stats_vdev->dev;
	u32 size = stats_vdev->vdev_fmt.fmt.meta.buffersize;
	u32 div = dev->hw_dev->unite ? 2 : 1;

	if (dev->isp_sdev.in_fmt.fmt_type == FMT_YUV)
		return;

	stats_vdev->stats_buf[0].is_need_vaddr = true;
	stats_vdev->stats_buf[0].size = size;
	if (rkisp_alloc_buffer(dev, &stats_vdev->stats_buf[0]))
		v4l2_warn(&dev->v4l2_dev, "stats alloc buf fail\n");
	else
		memset(stats_vdev->stats_buf[0].vaddr, 0, size);
	rkisp_stats_update_buf(stats_vdev);
	rkisp_unite_write(dev, ISP3X_MI_DBR_WR_SIZE, size / div, false);
	rkisp_unite_set_bits(dev, ISP3X_SWS_CFG, 0, ISP3X_3A_DDR_WRITE_EN, false);
	if (stats_vdev->nxt_buf) {
		stats_vdev->cur_buf = stats_vdev->nxt_buf;
		stats_vdev->nxt_buf = NULL;
	}
}

void rkisp_stats_next_ddr_config_v32(struct rkisp_isp_stats_vdev *stats_vdev)
{
	struct rkisp_hw_dev *hw = stats_vdev->dev->hw_dev;

	if (!stats_vdev->streamon)
		return;
	/* pingpong buf */
	if (hw->is_single)
		rkisp_stats_update_buf(stats_vdev);
}

void rkisp_init_stats_vdev_v32(struct rkisp_isp_stats_vdev *stats_vdev)
{
	int mult = stats_vdev->dev->hw_dev->unite ? 2 : 1;
	u32 size;

	stats_vdev->vdev_fmt.fmt.meta.dataformat =
		V4L2_META_FMT_RK_ISP1_STAT_3A;
	if (stats_vdev->dev->isp_ver == ISP_V32) {
		stats_vdev->priv_ops = &stats_ddr_ops_v32;
		stats_vdev->rd_stats_from_ddr = true;
		size = ALIGN(sizeof(struct rkisp32_isp_stat_buffer), 16);
	} else {
		stats_vdev->priv_ops = NULL;
		stats_vdev->rd_stats_from_ddr = false;
		size = sizeof(struct rkisp32_lite_stat_buffer);
	}
	stats_vdev->vdev_fmt.fmt.meta.buffersize = size * mult;
	stats_vdev->ops = &rkisp_isp_stats_ops_tbl;
}

void rkisp_uninit_stats_vdev_v32(struct rkisp_isp_stats_vdev *stats_vdev)
{

}
