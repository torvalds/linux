// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#include <linux/kfifo.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>	/* for ISP statistics */
#include "dev.h"
#include "regs.h"
#include "isp_stats.h"
#include "isp_stats_v1x.h"

static void
rkisp1_stats_get_awb_meas_v10(struct rkisp_isp_stats_vdev *stats_vdev,
			      struct rkisp1_stat_buffer *pbuf)
{
	/* Protect against concurrent access from ISR? */
	u32 reg_val;

	pbuf->meas_type |= CIFISP_STAT_AWB;
	reg_val = readl(stats_vdev->dev->base_addr + CIF_ISP_AWB_WHITE_CNT_V10);
	pbuf->params.awb.awb_mean[0].cnt = CIF_ISP_AWB_GET_PIXEL_CNT(reg_val);
	reg_val = readl(stats_vdev->dev->base_addr + CIF_ISP_AWB_MEAN_V10);

	pbuf->params.awb.awb_mean[0].mean_cr_or_r =
		CIF_ISP_AWB_GET_MEAN_CR_R(reg_val);
	pbuf->params.awb.awb_mean[0].mean_cb_or_b =
		CIF_ISP_AWB_GET_MEAN_CB_B(reg_val);
	pbuf->params.awb.awb_mean[0].mean_y_or_g =
		CIF_ISP_AWB_GET_MEAN_Y_G(reg_val);
}

static void
rkisp1_stats_get_awb_meas_v12(struct rkisp_isp_stats_vdev *stats_vdev,
			      struct rkisp1_stat_buffer *pbuf)
{
	/* Protect against concurrent access from ISR? */
	u32 reg_val;

	pbuf->meas_type |= CIFISP_STAT_AWB;
	reg_val = readl(stats_vdev->dev->base_addr + CIF_ISP_AWB_WHITE_CNT_V12);
	pbuf->params.awb.awb_mean[0].cnt = CIF_ISP_AWB_GET_PIXEL_CNT(reg_val);
	reg_val = readl(stats_vdev->dev->base_addr + CIF_ISP_AWB_MEAN_V12);

	pbuf->params.awb.awb_mean[0].mean_cr_or_r =
		CIF_ISP_AWB_GET_MEAN_CR_R(reg_val);
	pbuf->params.awb.awb_mean[0].mean_cb_or_b =
		CIF_ISP_AWB_GET_MEAN_CB_B(reg_val);
	pbuf->params.awb.awb_mean[0].mean_y_or_g =
		CIF_ISP_AWB_GET_MEAN_Y_G(reg_val);
}

static void
rkisp1_stats_get_aec_meas_v10(struct rkisp_isp_stats_vdev *stats_vdev,
			      struct rkisp1_stat_buffer *pbuf)
{
	int i;
	void __iomem *addr = stats_vdev->dev->base_addr + CIF_ISP_EXP_MEAN_00_V10;
	struct rkisp_stats_v1x_config *config =
		(struct rkisp_stats_v1x_config *)stats_vdev->priv_cfg;

	pbuf->meas_type |= CIFISP_STAT_AUTOEXP;
	for (i = 0; i < config->ae_mean_max; i++)
		pbuf->params.ae.exp_mean[i] = (u8)readl(addr + i * 4);
}

static void
rkisp1_stats_get_aec_meas_v12(struct rkisp_isp_stats_vdev *stats_vdev,
			      struct rkisp1_stat_buffer *pbuf)
{
	int i;
	void __iomem *addr = stats_vdev->dev->base_addr + CIF_ISP_EXP_MEAN_V12;
	struct rkisp_stats_v1x_config *config =
		(struct rkisp_stats_v1x_config *)stats_vdev->priv_cfg;
	u32 value;

	pbuf->meas_type |= CIFISP_STAT_AUTOEXP;
	for (i = 0; i < config->ae_mean_max / 4; i++) {
		value = readl(addr + i * 4);
		pbuf->params.ae.exp_mean[4 * i + 0] = CIF_ISP_EXP_GET_MEAN_xy0_V12(value);
		pbuf->params.ae.exp_mean[4 * i + 1] = CIF_ISP_EXP_GET_MEAN_xy1_V12(value);
		pbuf->params.ae.exp_mean[4 * i + 2] = CIF_ISP_EXP_GET_MEAN_xy2_V12(value);
		pbuf->params.ae.exp_mean[4 * i + 3] = CIF_ISP_EXP_GET_MEAN_xy3_V12(value);
	}
	value = readl(addr + i * 4);
	pbuf->params.ae.exp_mean[4 * i + 0] = CIF_ISP_EXP_GET_MEAN_xy0_V12(value);
}

static void rkisp1_stats_get_afc_meas(struct rkisp_isp_stats_vdev *stats_vdev,
				      struct rkisp1_stat_buffer *pbuf)
{
	void __iomem *base_addr;
	struct cifisp_af_stat *af;

	pbuf->meas_type |= CIFISP_STAT_AFM_FIN;

	af = &pbuf->params.af;
	base_addr = stats_vdev->dev->base_addr;
	af->window[0].sum = readl(base_addr + CIF_ISP_AFM_SUM_A);
	af->window[0].lum = readl(base_addr + CIF_ISP_AFM_LUM_A);
	af->window[1].sum = readl(base_addr + CIF_ISP_AFM_SUM_B);
	af->window[1].lum = readl(base_addr + CIF_ISP_AFM_LUM_B);
	af->window[2].sum = readl(base_addr + CIF_ISP_AFM_SUM_C);
	af->window[2].lum = readl(base_addr + CIF_ISP_AFM_LUM_C);
}

static void
rkisp1_stats_get_hst_meas_v10(struct rkisp_isp_stats_vdev *stats_vdev,
			      struct rkisp1_stat_buffer *pbuf)
{
	int i;
	void __iomem *addr = stats_vdev->dev->base_addr + CIF_ISP_HIST_BIN_0_V10;
	struct rkisp_stats_v1x_config *config =
		(struct rkisp_stats_v1x_config *)stats_vdev->priv_cfg;

	pbuf->meas_type |= CIFISP_STAT_HIST;
	for (i = 0; i < config->hist_bin_n_max; i++)
		pbuf->params.hist.hist_bins[i] = readl(addr + (i * 4));
}

static void
rkisp1_stats_get_hst_meas_v12(struct rkisp_isp_stats_vdev *stats_vdev,
					  struct rkisp1_stat_buffer *pbuf)
{
	int i;
	void __iomem *addr = stats_vdev->dev->base_addr + CIF_ISP_HIST_BIN_V12;
	struct rkisp_stats_v1x_config *config =
		(struct rkisp_stats_v1x_config *)stats_vdev->priv_cfg;
	u32 value;

	pbuf->meas_type |= CIFISP_STAT_HIST;
	for (i = 0; i < config->hist_bin_n_max / 2; i++) {
		value = readl(addr + (i * 4));
		pbuf->params.hist.hist_bins[2 * i] = CIF_ISP_HIST_GET_BIN0_V12(value);
		pbuf->params.hist.hist_bins[2 * i + 1] = CIF_ISP_HIST_GET_BIN1_V12(value);
	}
}

static void rkisp1_stats_get_bls_meas(struct rkisp_isp_stats_vdev *stats_vdev,
				      struct rkisp1_stat_buffer *pbuf)
{
	struct rkisp_device *dev = stats_vdev->dev;
	const struct ispsd_in_fmt *in_fmt =
			rkisp_get_ispsd_in_fmt(&dev->isp_sdev);
	void __iomem *base = stats_vdev->dev->base_addr;
	struct cifisp_bls_meas_val *bls_val;

	bls_val = &pbuf->params.ae.bls_val;
	if (in_fmt->bayer_pat == RAW_BGGR) {
		bls_val->meas_b = readl(base + CIF_ISP_BLS_A_MEASURED);
		bls_val->meas_gb = readl(base + CIF_ISP_BLS_B_MEASURED);
		bls_val->meas_gr = readl(base + CIF_ISP_BLS_C_MEASURED);
		bls_val->meas_r = readl(base + CIF_ISP_BLS_D_MEASURED);
	} else if (in_fmt->bayer_pat == RAW_GBRG) {
		bls_val->meas_gb = readl(base + CIF_ISP_BLS_A_MEASURED);
		bls_val->meas_b = readl(base + CIF_ISP_BLS_B_MEASURED);
		bls_val->meas_r = readl(base + CIF_ISP_BLS_C_MEASURED);
		bls_val->meas_gr = readl(base + CIF_ISP_BLS_D_MEASURED);
	} else if (in_fmt->bayer_pat == RAW_GRBG) {
		bls_val->meas_gr = readl(base + CIF_ISP_BLS_A_MEASURED);
		bls_val->meas_r = readl(base + CIF_ISP_BLS_B_MEASURED);
		bls_val->meas_b = readl(base + CIF_ISP_BLS_C_MEASURED);
		bls_val->meas_gb = readl(base + CIF_ISP_BLS_D_MEASURED);
	} else if (in_fmt->bayer_pat == RAW_RGGB) {
		bls_val->meas_r = readl(base + CIF_ISP_BLS_A_MEASURED);
		bls_val->meas_gr = readl(base + CIF_ISP_BLS_B_MEASURED);
		bls_val->meas_gb = readl(base + CIF_ISP_BLS_C_MEASURED);
		bls_val->meas_b = readl(base + CIF_ISP_BLS_D_MEASURED);
	}
}

static void rkisp1_stats_get_emb_data(struct rkisp_isp_stats_vdev *stats_vdev,
				      struct rkisp1_stat_buffer *pbuf)
{
	unsigned int i;
	struct rkisp_device *dev = stats_vdev->dev;
	unsigned int ph = 0, out = 0, packet_len = 0, playload_len = 0;
	unsigned int mipi_kfifo_len;
	unsigned int idx;
	unsigned char *fifo_data;

	idx = RKISP_EMDDATA_FIFO_MAX;
	for (i = 0; i < RKISP_EMDDATA_FIFO_MAX; i++) {
		if (dev->emd_data_fifo[i].frame_id == pbuf->frame_id) {
			idx = i;
			break;
		}
	}

	if (idx == RKISP_EMDDATA_FIFO_MAX)
		return;

	if (kfifo_is_empty(&dev->emd_data_fifo[idx].mipi_kfifo))
		return;

	mipi_kfifo_len = dev->emd_data_fifo[idx].data_len;
	fifo_data = &pbuf->params.emd.data[0];
	for (i = 0; i < mipi_kfifo_len;) {
		/* handle the package header */
		out = kfifo_out(&dev->emd_data_fifo[idx].mipi_kfifo,
				&ph, sizeof(ph));
		if (!out)
			break;
		packet_len = (ph >> 8) & 0xfff;
		i += sizeof(ph);

		/* handle the package data */
		out = kfifo_out(&dev->emd_data_fifo[idx].mipi_kfifo,
				fifo_data, packet_len);
		if (!out)
			break;

		i += packet_len;
		playload_len += packet_len;
		fifo_data += packet_len;

		v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
			 "packet_len: 0x%x, ph: 0x%x\n",
			 packet_len, ph);
	}

	pbuf->meas_type |= CIFISP_STAT_EMB_DATA;

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "playload_len: %d, pbuf->frame_id %d\n",
		 playload_len, pbuf->frame_id);
}

static struct rkisp_stats_v1x_ops rkisp1_v10_stats_ops = {
	.get_awb_meas = rkisp1_stats_get_awb_meas_v10,
	.get_aec_meas = rkisp1_stats_get_aec_meas_v10,
	.get_afc_meas = rkisp1_stats_get_afc_meas,
	.get_hst_meas = rkisp1_stats_get_hst_meas_v10,
	.get_bls_meas = rkisp1_stats_get_bls_meas,
	.get_emb_data = rkisp1_stats_get_emb_data,
};

static struct rkisp_stats_v1x_ops rkisp1_v12_stats_ops = {
	.get_awb_meas = rkisp1_stats_get_awb_meas_v12,
	.get_aec_meas = rkisp1_stats_get_aec_meas_v12,
	.get_afc_meas = rkisp1_stats_get_afc_meas,
	.get_hst_meas = rkisp1_stats_get_hst_meas_v12,
	.get_bls_meas = rkisp1_stats_get_bls_meas,
};

static struct rkisp_stats_v1x_config rkisp1_v10_stats_config = {
	.ae_mean_max = 25,
	.hist_bin_n_max = 16,
};

static struct rkisp_stats_v1x_config rkisp1_v12_stats_config = {
	.ae_mean_max = 81,
	.hist_bin_n_max = 32,
};

static void
rkisp1_stats_send_meas_v1x(struct rkisp_isp_stats_vdev *stats_vdev,
			      struct rkisp_isp_readout_work *meas_work)
{
	unsigned int cur_frame_id = -1;
	struct rkisp1_stat_buffer *cur_stat_buf;
	struct rkisp_buffer *cur_buf = NULL;
	struct rkisp_stats_v1x_ops *ops =
		(struct rkisp_stats_v1x_ops *)stats_vdev->priv_ops;

	cur_frame_id = atomic_read(&stats_vdev->dev->isp_sdev.frm_sync_seq) - 1;
	if (cur_frame_id != meas_work->frame_id) {
		v4l2_warn(stats_vdev->vnode.vdev.v4l2_dev,
			  "Measurement late(%d, %d)\n",
			  cur_frame_id, meas_work->frame_id);
		cur_frame_id = meas_work->frame_id;
	}

	spin_lock(&stats_vdev->rd_lock);
	/* get one empty buffer */
	if (!list_empty(&stats_vdev->stat)) {
		cur_buf = list_first_entry(&stats_vdev->stat,
					   struct rkisp_buffer, queue);
		list_del(&cur_buf->queue);
	}
	spin_unlock(&stats_vdev->rd_lock);

	if (!cur_buf)
		return;

	cur_stat_buf =
		(struct rkisp1_stat_buffer *)(cur_buf->vaddr[0]);
	memset(cur_stat_buf, 0, sizeof(*cur_stat_buf));
	cur_stat_buf->frame_id = cur_frame_id;
	if (meas_work->isp_ris & CIF_ISP_AWB_DONE) {
		ops->get_awb_meas(stats_vdev, cur_stat_buf);
		cur_stat_buf->meas_type |= CIFISP_STAT_AWB;
	}

	if (meas_work->isp_ris & CIF_ISP_AFM_FIN) {
		ops->get_afc_meas(stats_vdev, cur_stat_buf);
		cur_stat_buf->meas_type |= CIFISP_STAT_AFM_FIN;
	}

	if (meas_work->isp_ris & CIF_ISP_EXP_END) {
		ops->get_aec_meas(stats_vdev, cur_stat_buf);
		ops->get_bls_meas(stats_vdev, cur_stat_buf);
		cur_stat_buf->meas_type |= CIFISP_STAT_AUTOEXP;
	}

	if (meas_work->isp_ris & CIF_ISP_HIST_MEASURE_RDY) {
		ops->get_hst_meas(stats_vdev, cur_stat_buf);
		cur_stat_buf->meas_type |= CIFISP_STAT_HIST;
	}

	if ((meas_work->isp_ris & CIF_ISP_FRAME) &&
		ops->get_emb_data)
		ops->get_emb_data(stats_vdev, cur_stat_buf);

	vb2_set_plane_payload(&cur_buf->vb.vb2_buf, 0,
			      sizeof(struct rkisp1_stat_buffer));
	cur_buf->vb.sequence = cur_frame_id;
	cur_buf->vb.vb2_buf.timestamp = meas_work->timestamp;
	vb2_buffer_done(&cur_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
}

static void
rkisp1_stats_isr_v1x(struct rkisp_isp_stats_vdev *stats_vdev,
		     u32 isp_ris, u32 isp3a_ris)
{
	unsigned int isp_mis_tmp = 0;
	struct rkisp_isp_readout_work work;
	unsigned int cur_frame_id =
		atomic_read(&stats_vdev->dev->isp_sdev.frm_sync_seq) - 1;
#ifdef LOG_ISR_EXE_TIME
	ktime_t in_t = ktime_get();
#endif

	spin_lock(&stats_vdev->irq_lock);

	isp_mis_tmp = isp_ris & (CIF_ISP_AWB_DONE | CIF_ISP_AFM_FIN |
			CIF_ISP_EXP_END | CIF_ISP_HIST_MEASURE_RDY);
	if (isp_mis_tmp) {
		writel(isp_mis_tmp,
			stats_vdev->dev->base_addr + CIF_ISP_ICR);

		isp_mis_tmp &= readl(stats_vdev->dev->base_addr + CIF_ISP_MIS);
		if (isp_mis_tmp)
			v4l2_err(stats_vdev->vnode.vdev.v4l2_dev,
				 "isp icr 3A info err: 0x%x 0x%x\n",
				 isp_mis_tmp, isp_ris);
	}

	if (!stats_vdev->streamon)
		goto unlock;

	if (isp_ris & (CIF_ISP_FRAME | CIF_ISP_AWB_DONE |
		CIF_ISP_AFM_FIN | CIF_ISP_EXP_END |
		CIF_ISP_HIST_MEASURE_RDY)) {
		work.readout = RKISP_ISP_READOUT_MEAS;
		work.frame_id = cur_frame_id;
		work.isp_ris = isp_ris;
		work.timestamp = ktime_get_ns();
		if (!kfifo_is_full(&stats_vdev->rd_kfifo))
			kfifo_in(&stats_vdev->rd_kfifo,
				 &work, sizeof(work));
		else
			v4l2_err(stats_vdev->vnode.vdev.v4l2_dev,
				 "stats kfifo is full\n");

		tasklet_schedule(&stats_vdev->rd_tasklet);
	}

#ifdef LOG_ISR_EXE_TIME
	if (isp_ris & (CIF_ISP_EXP_END | CIF_ISP_AWB_DONE |
		       CIF_ISP_FRAME | CIF_ISP_HIST_MEASURE_RDY)) {
		unsigned int diff_us =
		    ktime_to_us(ktime_sub(ktime_get(), in_t));

		if (diff_us > g_longest_isr_time)
			g_longest_isr_time = diff_us;

		v4l2_info(stats_vdev->vnode.vdev.v4l2_dev,
			  "isp_isr time %d %d\n", diff_us, g_longest_isr_time);
	}
#endif

unlock:
	spin_unlock(&stats_vdev->irq_lock);
}

static void
rkisp_stats_rdbk_enable_v1x(struct rkisp_isp_stats_vdev *stats_vdev, bool en)
{
}

static struct rkisp_isp_stats_ops rkisp_isp_stats_ops_tbl = {
	.isr_hdl = rkisp1_stats_isr_v1x,
	.send_meas = rkisp1_stats_send_meas_v1x,
	.rdbk_enable = rkisp_stats_rdbk_enable_v1x,
};

void rkisp_init_stats_vdev_v1x(struct rkisp_isp_stats_vdev *stats_vdev)
{
	stats_vdev->vdev_fmt.fmt.meta.dataformat =
		V4L2_META_FMT_RK_ISP1_STAT_3A;
	stats_vdev->vdev_fmt.fmt.meta.buffersize =
		sizeof(struct rkisp1_stat_buffer);

	stats_vdev->ops = &rkisp_isp_stats_ops_tbl;
	if (stats_vdev->dev->isp_ver == ISP_V12 ||
	    stats_vdev->dev->isp_ver == ISP_V13) {
		stats_vdev->priv_ops = &rkisp1_v12_stats_ops;
		stats_vdev->priv_cfg = &rkisp1_v12_stats_config;
	} else {
		stats_vdev->priv_ops = &rkisp1_v10_stats_ops;
		stats_vdev->priv_cfg = &rkisp1_v10_stats_config;
	}
}

void rkisp_uninit_stats_vdev_v1x(struct rkisp_isp_stats_vdev *stats_vdev)
{
}

