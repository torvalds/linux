// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#include <linux/kfifo.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>	/* for ISP statistics */
#include "dev.h"
#include "regs.h"
#include "common.h"
#include "isp_stats.h"
#include "isp_stats_v2x.h"

#define ISP2X_SIAWB_GET_MEAN_CR_R(x)	((x) & 0xFF)
#define ISP2X_SIAWB_GET_MEAN_CB_B(x)	(((x) >> 8) & 0xFF)
#define ISP2X_SIAWB_GET_MEAN_Y_G(x)	(((x) >> 16) & 0xFF)
#define ISP2X_SIAWB_GET_PIXEL_CNT(x)	((x) & 0x3FFFFFF)

#define ISP2X_HIST_GET_BIN0(x)		((x) & 0xFFFF)
#define ISP2X_HIST_GET_BIN1(x)		(((x) >> 16) & 0xFFFF)

#define ISP2X_3A_MEAS_DONE		BIT(31)

#define ISP2X_EXP_GET_MEAN_xy0(x)	((x) & 0xFF)
#define ISP2X_EXP_GET_MEAN_xy1(x)	(((x) >> 8) & 0xFF)
#define ISP2X_EXP_GET_MEAN_xy2(x)	(((x) >> 16) & 0xFF)
#define ISP2X_EXP_GET_MEAN_xy3(x)	(((x) >> 24) & 0xFF)

#define ISP2X_RAWAEBIG_GET_MEAN_G(x)	((x) & 0xFFF)
#define ISP2X_RAWAEBIG_GET_MEAN_B(x)	(((x) >> 12) & 0x3FF)
#define ISP2X_RAWAEBIG_GET_MEAN_R(x)	(((x) >> 22) & 0x3FF)

#define ISP2X_RAWAF_INT_LINE0_EN	BIT(27)

static void
rkisp_stats_get_siawb_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
			       struct rkisp_isp2x_stat_buffer *pbuf)
{
	u32 reg_val;

	if (!pbuf)
		return;

	pbuf->meas_type |= ISP2X_STAT_SIAWB;
	reg_val = rkisp_read(stats_vdev->dev, CIF_ISP_AWB_WHITE_CNT_V10, true);
	pbuf->params.siawb.awb_mean[0].cnt = ISP2X_SIAWB_GET_PIXEL_CNT(reg_val);
	reg_val = rkisp_read(stats_vdev->dev, CIF_ISP_AWB_MEAN_V10, true);

	pbuf->params.siawb.awb_mean[0].mean_cr_or_r =
		ISP2X_SIAWB_GET_MEAN_CR_R(reg_val);
	pbuf->params.siawb.awb_mean[0].mean_cb_or_b =
		ISP2X_SIAWB_GET_MEAN_CB_B(reg_val);
	pbuf->params.siawb.awb_mean[0].mean_y_or_g =
		ISP2X_SIAWB_GET_MEAN_Y_G(reg_val);
}

static void
rkisp_stats_get_rawawb_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp_isp2x_stat_buffer *pbuf)
{
	u64 msb, lsb;
	u32 value;
	int i;

	if (!pbuf)
		goto out;

	pbuf->meas_type |= ISP2X_STAT_RAWAWB;

	for (i = 0; i < ISP2X_RAWAWB_SUM_NUM; i++) {
		pbuf->params.rawawb.ro_rawawb_sum_r_nor[i] =
			rkisp_read(stats_vdev->dev, ISP_RAWAWB_SUM_R_NOR_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_rawawb_sum_g_nor[i] =
			rkisp_read(stats_vdev->dev, ISP_RAWAWB_SUM_G_NOR_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_rawawb_sum_b_nor[i] =
			rkisp_read(stats_vdev->dev, ISP_RAWAWB_SUM_B_NOR_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_rawawb_wp_num_nor[i] =
			rkisp_read(stats_vdev->dev, ISP_RAWAWB_WP_NUM_NOR_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_rawawb_sum_r_big[i] =
			rkisp_read(stats_vdev->dev, ISP_RAWAWB_SUM_R_BIG_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_rawawb_sum_g_big[i] =
			rkisp_read(stats_vdev->dev, ISP_RAWAWB_SUM_G_BIG_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_rawawb_sum_b_big[i] =
			rkisp_read(stats_vdev->dev, ISP_RAWAWB_SUM_B_BIG_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_rawawb_wp_num_big[i] =
			rkisp_read(stats_vdev->dev, ISP_RAWAWB_WP_NUM_BIG_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_rawawb_sum_r_sma[i] =
			rkisp_read(stats_vdev->dev, ISP_RAWAWB_SUM_R_SMA_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_rawawb_sum_g_sma[i] =
			rkisp_read(stats_vdev->dev, ISP_RAWAWB_SUM_G_SMA_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_rawawb_sum_b_sma[i] =
			rkisp_read(stats_vdev->dev, ISP_RAWAWB_SUM_B_SMA_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_rawawb_wp_num_sma[i] =
			rkisp_read(stats_vdev->dev, ISP_RAWAWB_WP_NUM_SMA_0 + 0x30 * i, true);
	}

	for (i = 0; i < ISP2X_RAWAWB_MULWD_NUM; i++) {
		pbuf->params.rawawb.ro_sum_r_nor_multiwindow[i] =
			rkisp_read(stats_vdev->dev,
				   ISP_RAWAWB_SUM_R_NOR_MULTIWINDOW_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_sum_g_nor_multiwindow[i] =
			rkisp_read(stats_vdev->dev,
				   ISP_RAWAWB_SUM_G_NOR_MULTIWINDOW_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_sum_b_nor_multiwindow[i] =
			rkisp_read(stats_vdev->dev,
				   ISP_RAWAWB_SUM_B_NOR_MULTIWINDOW_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_wp_nm_nor_multiwindow[i] =
			rkisp_read(stats_vdev->dev,
				   ISP_RAWAWB_WP_NM_NOR_MULTIWINDOW_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_sum_r_big_multiwindow[i] =
			rkisp_read(stats_vdev->dev,
				   ISP_RAWAWB_SUM_R_BIG_MULTIWINDOW_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_sum_g_big_multiwindow[i] =
			rkisp_read(stats_vdev->dev,
				   ISP_RAWAWB_SUM_G_BIG_MULTIWINDOW_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_sum_b_big_multiwindow[i] =
			rkisp_read(stats_vdev->dev,
				   ISP_RAWAWB_SUM_B_BIG_MULTIWINDOW_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_wp_nm_big_multiwindow[i] =
			rkisp_read(stats_vdev->dev,
				   ISP_RAWAWB_WP_NM_BIG_MULTIWINDOW_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_sum_r_sma_multiwindow[i] =
			rkisp_read(stats_vdev->dev,
				   ISP_RAWAWB_SUM_R_SMA_MULTIWINDOW_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_sum_g_sma_multiwindow[i] =
			rkisp_read(stats_vdev->dev,
				   ISP_RAWAWB_SUM_G_SMA_MULTIWINDOW_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_sum_b_sma_multiwindow[i] =
			rkisp_read(stats_vdev->dev,
				   ISP_RAWAWB_SUM_B_SMA_MULTIWINDOW_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_wp_nm_sma_multiwindow[i] =
			rkisp_read(stats_vdev->dev,
				   ISP_RAWAWB_WP_NM_SMA_MULTIWINDOW_0 + 0x30 * i, true);
	}

	for (i = 0; i < ISP2X_RAWAWB_SUM_NUM; i++) {
		pbuf->params.rawawb.ro_sum_r_exc[i] =
			rkisp_read(stats_vdev->dev, ISP_RAWAWB_SUM_R_EXC_0 + 0x10 * i, true);
		pbuf->params.rawawb.ro_sum_g_exc[i] =
			rkisp_read(stats_vdev->dev, ISP_RAWAWB_SUM_G_EXC_0 + 0x10 * i, true);
		pbuf->params.rawawb.ro_sum_b_exc[i] =
			rkisp_read(stats_vdev->dev, ISP_RAWAWB_SUM_B_EXC_0 + 0x10 * i, true);
		pbuf->params.rawawb.ro_wp_nm_exc[i] =
			rkisp_read(stats_vdev->dev, ISP_RAWAWB_WP_NM_EXC_0 + 0x10 * i, true);
	}

	for (i = 0; i < ISP2X_RAWAWB_RAMDATA_NUM; i++) {
		lsb = rkisp_read(stats_vdev->dev, ISP_RAWAWB_RAM_DATA, true);
		msb = rkisp_read(stats_vdev->dev, ISP_RAWAWB_RAM_DATA, true);
		pbuf->params.rawawb.ramdata[i].b = lsb & 0x3FFFF;
		pbuf->params.rawawb.ramdata[i].g = ((lsb & 0xFFFC0000) >> 18) | (msb & 0xF) << 14;
		pbuf->params.rawawb.ramdata[i].r = (msb & 0x3FFFF0) >> 4;
		pbuf->params.rawawb.ramdata[i].wp = (msb & 0xE0000000) >> 29;
	}

out:
	value = rkisp_read(stats_vdev->dev, ISP_RAWAWB_CTRL, true);
	value |= ISP2X_3A_MEAS_DONE;
	rkisp_write(stats_vdev->dev, ISP_RAWAWB_CTRL, value, true);
}

static void
rkisp_stats_get_siaf_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
			      struct rkisp_isp2x_stat_buffer *pbuf)
{
	struct isp2x_siaf_stat *af;

	if (!pbuf)
		return;

	pbuf->meas_type |= ISP2X_STAT_SIAF;

	af = &pbuf->params.siaf;
	af->win[0].sum = rkisp_read(stats_vdev->dev, ISP_AFM_SUM_A, true);
	af->win[0].lum = rkisp_read(stats_vdev->dev, ISP_AFM_LUM_A, true);
	af->win[1].sum = rkisp_read(stats_vdev->dev, ISP_AFM_SUM_B, true);
	af->win[1].lum = rkisp_read(stats_vdev->dev, ISP_AFM_LUM_B, true);
	af->win[2].sum = rkisp_read(stats_vdev->dev, ISP_AFM_SUM_C, true);
	af->win[2].lum = rkisp_read(stats_vdev->dev, ISP_AFM_LUM_C, true);
}

static void
rkisp_stats_get_rawaf_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
			       struct rkisp_isp2x_stat_buffer *pbuf)
{
	struct isp2x_rawaf_stat *af;
	u32 value, read_line;
	u32 line_num[ISP2X_RAWAF_LINE_NUM + 1];
	int i;

	if (!pbuf)
		goto out;

	af = &pbuf->params.rawaf;
	pbuf->meas_type |= ISP2X_STAT_RAWAF;

	af->afm_sum[0] = rkisp_read(stats_vdev->dev, ISP_RAWAF_SUM_A, true);
	af->afm_sum[1] = rkisp_read(stats_vdev->dev, ISP_RAWAF_SUM_B, true);
	af->afm_lum[0] = rkisp_read(stats_vdev->dev, ISP_RAWAF_LUM_A, true);
	af->afm_lum[1] = rkisp_read(stats_vdev->dev, ISP_RAWAF_LUM_B, true);
	af->int_state = rkisp_read(stats_vdev->dev, ISP_RAWAF_INT_STATE, true);

	memset(line_num, 0, sizeof(line_num));
	line_num[ISP2X_RAWAF_LINE_NUM] = ISP2X_RAWAF_SUMDATA_ROW;
	value = rkisp_read(stats_vdev->dev, ISP_RAWAF_INT_LINE, true);
	for (i = 0; i < ISP2X_RAWAF_LINE_NUM; i++) {
		if (value & (ISP2X_RAWAF_INT_LINE0_EN << i)) {
			line_num[i] = (value >> (4 * i)) & 0xF;
			line_num[ISP2X_RAWAF_LINE_NUM] -= line_num[i];
		}
	}

	read_line = 0;
	for (i = 0; i < ISP2X_RAWAF_LINE_NUM; i++) {
		if (af->int_state & (1 << i))
			read_line += line_num[i];
	}

	if (!read_line)
		read_line = line_num[ISP2X_RAWAF_LINE_NUM];

	for (i = 0; i < read_line * ISP2X_RAWAF_SUMDATA_COLUMN; i++)
		af->ramdata[i] = rkisp_read(stats_vdev->dev, ISP_RAWAF_RAM_DATA, true);

out:
	value = rkisp_read(stats_vdev->dev, ISP_RAWAF_CTRL, true);
	value |= ISP2X_3A_MEAS_DONE;
	rkisp_write(stats_vdev->dev, ISP_RAWAF_CTRL, value, true);
	rkisp_write(stats_vdev->dev, ISP_RAWAF_INT_STATE, 0, true);
}

static void
rkisp_stats_get_yuvae_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
			       struct rkisp_isp2x_stat_buffer *pbuf)
{
	u32 value;
	int i;

	if (!pbuf)
		goto out;

	pbuf->meas_type |= ISP2X_STAT_YUVAE;
	for (i = 0; i < ISP2X_YUVAE_MEAN_NUM / 4; i++) {
		value = rkisp_read(stats_vdev->dev, ISP_YUVAE_RO_MEAN_BASE_ADDR, true);
		pbuf->params.yuvae.mean[4 * i + 0] = ISP2X_EXP_GET_MEAN_xy0(value);
		pbuf->params.yuvae.mean[4 * i + 1] = ISP2X_EXP_GET_MEAN_xy1(value);
		pbuf->params.yuvae.mean[4 * i + 2] = ISP2X_EXP_GET_MEAN_xy2(value);
		pbuf->params.yuvae.mean[4 * i + 3] = ISP2X_EXP_GET_MEAN_xy3(value);
	}
	value = rkisp_read(stats_vdev->dev, ISP_YUVAE_RO_MEAN_BASE_ADDR, true);
	pbuf->params.yuvae.mean[4 * i + 0] = ISP2X_EXP_GET_MEAN_xy0(value);

	for (i = 0; i < ISP2X_YUVAE_SUBWIN_NUM; i++)
		pbuf->params.yuvae.ro_yuvae_sumy[i] =
			rkisp_read(stats_vdev->dev, ISP_YUVAE_WND1_SUMY + 4 * i, true);

out:
	value = rkisp_read(stats_vdev->dev, ISP_YUVAE_CTRL, true);
	value |= ISP2X_3A_MEAS_DONE;
	rkisp_write(stats_vdev->dev, ISP_YUVAE_CTRL, value, true);
}

static void
rkisp_stats_get_sihst_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
			       struct rkisp_isp2x_stat_buffer *pbuf)
{
	u32 value;
	int i, j;

	if (!pbuf)
		return;

	pbuf->meas_type |= ISP2X_STAT_SIHST;
	for (i = 0; i < ISP2X_SIHIST_WIN_NUM; i++) {
		for (j = 0; j < ISP2X_SIHIST_BIN_N_MAX / 2; j++) {
			value = rkisp_read(stats_vdev->dev,
					   ISP_HIST_HIST_BIN + i * 0x40 + j * 4, true);
			pbuf->params.sihst.win_stat[i].hist_bins[2 * j] =
				ISP2X_HIST_GET_BIN0(value);
			pbuf->params.sihst.win_stat[i].hist_bins[2 * j + 1] =
				ISP2X_HIST_GET_BIN1(value);
		}
	}
}

static void
rkisp_stats_get_rawaebig_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				  struct isp2x_rawaebig_stat *ae, u32 blk_no)
{
	u32 addr, value;
	int i;

	switch (blk_no) {
	case 0:
		addr = RAWAE_BIG1_BASE;
		break;
	case 1:
		addr = RAWAE_BIG2_BASE;
		break;
	case 2:
		addr = RAWAE_BIG3_BASE;
		break;
	default:
		addr = RAWAE_BIG1_BASE;
		break;
	}

	if (!ae)
		goto out;

	for (i = 0; i < ISP2X_RAWAEBIG_SUBWIN_NUM; i++)
		ae->sumr[i] = rkisp_read(stats_vdev->dev, addr + RAWAE_BIG_WND1_SUMR + i * 4, true);

	for (i = 0; i < ISP2X_RAWAEBIG_SUBWIN_NUM; i++)
		ae->sumg[i] = rkisp_read(stats_vdev->dev, addr + RAWAE_BIG_WND1_SUMG + i * 4, true);

	for (i = 0; i < ISP2X_RAWAEBIG_SUBWIN_NUM; i++)
		ae->sumb[i] = rkisp_read(stats_vdev->dev, addr + RAWAE_BIG_WND1_SUMB + i * 4, true);

	for (i = 0; i < ISP2X_RAWAEBIG_MEAN_NUM; i++) {
		value = rkisp_read(stats_vdev->dev, addr + RAWAE_BIG_RO_MEAN_BASE_ADDR, true);
		ae->data[i].channelg_xy = ISP2X_RAWAEBIG_GET_MEAN_G(value);
		ae->data[i].channelb_xy = ISP2X_RAWAEBIG_GET_MEAN_B(value);
		ae->data[i].channelr_xy = ISP2X_RAWAEBIG_GET_MEAN_R(value);
	}

out:
	value = rkisp_read(stats_vdev->dev, addr + RAWAE_BIG_CTRL, true);
	value |= ISP2X_3A_MEAS_DONE;
	rkisp_write(stats_vdev->dev, addr + RAWAE_BIG_CTRL, value, true);
}

static void
rkisp_stats_get_rawhstbig_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				   struct isp2x_rawhistbig_stat *hst, u32 blk_no)
{
	u32 addr, value;
	int i;

	switch (blk_no) {
	case 0:
		addr = ISP_RAWHIST_BIG1_BASE;
		break;
	case 1:
		addr = ISP_RAWHIST_BIG2_BASE;
		break;
	case 2:
		addr = ISP_RAWHIST_BIG3_BASE;
		break;
	default:
		addr = ISP_RAWHIST_BIG1_BASE;
		break;
	}

	if (!hst)
		goto out;

	for (i = 0; i < ISP2X_HIST_BIN_N_MAX; i++)
		hst->hist_bin[i] = rkisp_read(stats_vdev->dev,
					      addr + ISP_RAWHIST_BIG_RO_BASE_BIN, true);

out:
	value = rkisp_read(stats_vdev->dev, addr + ISP_RAWHIST_BIG_CTRL, true);
	value |= ISP2X_3A_MEAS_DONE;
	rkisp_write(stats_vdev->dev, addr + ISP_RAWHIST_BIG_CTRL, value, true);
}

static void
rkisp_stats_get_rawae1_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp_isp2x_stat_buffer *pbuf)
{
	if (!pbuf)
		rkisp_stats_get_rawaebig_meas_reg(stats_vdev, NULL, 1);
	else
		rkisp_stats_get_rawaebig_meas_reg(stats_vdev, &pbuf->params.rawae1, 1);
}

static void
rkisp_stats_get_rawhst1_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				 struct rkisp_isp2x_stat_buffer *pbuf)
{
	if (!pbuf)
		rkisp_stats_get_rawhstbig_meas_reg(stats_vdev, NULL, 1);
	else
		rkisp_stats_get_rawhstbig_meas_reg(stats_vdev, &pbuf->params.rawhist1, 1);
}

static void
rkisp_stats_get_rawae2_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp_isp2x_stat_buffer *pbuf)
{
	if (!pbuf)
		rkisp_stats_get_rawaebig_meas_reg(stats_vdev, NULL, 2);
	else
		rkisp_stats_get_rawaebig_meas_reg(stats_vdev, &pbuf->params.rawae2, 2);
}

static void
rkisp_stats_get_rawhst2_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				 struct rkisp_isp2x_stat_buffer *pbuf)
{
	if (!pbuf)
		rkisp_stats_get_rawhstbig_meas_reg(stats_vdev, NULL, 2);
	else
		rkisp_stats_get_rawhstbig_meas_reg(stats_vdev, &pbuf->params.rawhist2, 2);
}

static void
rkisp_stats_get_rawae3_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp_isp2x_stat_buffer *pbuf)
{
	if (!pbuf)
		rkisp_stats_get_rawaebig_meas_reg(stats_vdev, NULL, 0);
	else
		rkisp_stats_get_rawaebig_meas_reg(stats_vdev, &pbuf->params.rawae3, 0);
}

static void
rkisp_stats_get_rawhst3_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				 struct rkisp_isp2x_stat_buffer *pbuf)
{
	if (!pbuf)
		rkisp_stats_get_rawhstbig_meas_reg(stats_vdev, NULL, 0);
	else
		rkisp_stats_get_rawhstbig_meas_reg(stats_vdev, &pbuf->params.rawhist3, 0);
}

static void
rkisp_stats_get_rawaelite_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				   struct rkisp_isp2x_stat_buffer *pbuf)
{
	struct isp2x_rawaelite_stat *ae;
	u32 value;
	int i;

	if (!pbuf)
		goto out;

	ae = &pbuf->params.rawae0;
	value = rkisp_read(stats_vdev->dev, ISP_RAWAE_LITE_CTRL, true);

	if ((value & ISP2X_3A_MEAS_DONE) == 0)
		return;

	for (i = 0; i < ISP2X_RAWAELITE_MEAN_NUM; i++) {
		value = rkisp_read(stats_vdev->dev, ISP_RAWAE_LITE_RO_MEAN + 4 * i, true);
		ae->data[i].channelg_xy = ISP2X_RAWAEBIG_GET_MEAN_G(value);
		ae->data[i].channelb_xy = ISP2X_RAWAEBIG_GET_MEAN_B(value);
		ae->data[i].channelr_xy = ISP2X_RAWAEBIG_GET_MEAN_R(value);
	}

out:
	value = rkisp_read(stats_vdev->dev, ISP_RAWAE_LITE_CTRL, true);
	value |= ISP2X_3A_MEAS_DONE;
	rkisp_write(stats_vdev->dev, ISP_RAWAE_LITE_CTRL, value, true);
}

static void
rkisp_stats_get_rawhstlite_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				    struct rkisp_isp2x_stat_buffer *pbuf)
{
	struct isp2x_rawhistlite_stat *hst;
	u32 value;
	int i;

	if (!pbuf)
		goto out;

	hst = &pbuf->params.rawhist0;
	for (i = 0; i < ISP2X_HIST_BIN_N_MAX; i++)
		hst->hist_bin[i] = rkisp_read(stats_vdev->dev, ISP_RAWHIST_LITE_RO_BASE_BIN, true);

out:
	value = rkisp_read(stats_vdev->dev, ISP_RAWHIST_LITE_CTRL, true);
	value |= ISP2X_3A_MEAS_DONE;
	rkisp_write(stats_vdev->dev, ISP_RAWHIST_LITE_CTRL, value, true);
}

static void
rkisp_stats_get_bls_stats(struct rkisp_isp_stats_vdev *stats_vdev,
			  struct rkisp_isp2x_stat_buffer *pbuf)
{
	struct ispsd_in_fmt in_fmt = stats_vdev->dev->isp_sdev.in_fmt;
	enum rkisp_fmt_raw_pat_type raw_type = in_fmt.bayer_pat;
	struct isp2x_bls_stat *bls;
	u32 value;

	if (!pbuf)
		return;

	bls = &pbuf->params.bls;
	value = rkisp_read(stats_vdev->dev, ISP_BLS_CTRL, true);
	if (value & (ISP_BLS_ENA | ISP_BLS_MODE_MEASURED)) {
		pbuf->meas_type |= ISP2X_STAT_BLS;

		switch (raw_type) {
		case RAW_BGGR:
			bls->meas_r = rkisp_read(stats_vdev->dev, ISP_BLS_D_MEASURED, true);
			bls->meas_gr = rkisp_read(stats_vdev->dev, ISP_BLS_C_MEASURED, true);
			bls->meas_gb = rkisp_read(stats_vdev->dev, ISP_BLS_B_MEASURED, true);
			bls->meas_b = rkisp_read(stats_vdev->dev, ISP_BLS_A_MEASURED, true);
			break;
		case RAW_GBRG:
			bls->meas_r = rkisp_read(stats_vdev->dev, ISP_BLS_C_MEASURED, true);
			bls->meas_gr = rkisp_read(stats_vdev->dev, ISP_BLS_D_MEASURED, true);
			bls->meas_gb = rkisp_read(stats_vdev->dev, ISP_BLS_A_MEASURED, true);
			bls->meas_b = rkisp_read(stats_vdev->dev, ISP_BLS_B_MEASURED, true);
			break;
		case RAW_GRBG:
			bls->meas_r = rkisp_read(stats_vdev->dev, ISP_BLS_B_MEASURED, true);
			bls->meas_gr = rkisp_read(stats_vdev->dev, ISP_BLS_A_MEASURED, true);
			bls->meas_gb = rkisp_read(stats_vdev->dev, ISP_BLS_D_MEASURED, true);
			bls->meas_b = rkisp_read(stats_vdev->dev, ISP_BLS_C_MEASURED, true);
			break;
		case RAW_RGGB:
			bls->meas_r = rkisp_read(stats_vdev->dev, ISP_BLS_A_MEASURED, true);
			bls->meas_gr = rkisp_read(stats_vdev->dev, ISP_BLS_B_MEASURED, true);
			bls->meas_gb = rkisp_read(stats_vdev->dev, ISP_BLS_C_MEASURED, true);
			bls->meas_b = rkisp_read(stats_vdev->dev, ISP_BLS_D_MEASURED, true);
			break;
		default:
			break;
		}
	}
}

static void
rkisp_stats_get_tmo_stats(struct rkisp_isp_stats_vdev *stats_vdev,
			  struct rkisp_isp2x_stat_buffer *pbuf)
{
	struct rkisp_device *dev = stats_vdev->dev;
	struct rkisp_isp_params_vdev *params_vdev = &dev->params_vdev;
	struct isp2x_hdrtmo_stat *tmo;
	u32 value, i;

	if (!pbuf || !params_vdev->hdrtmo_en)
		return;

	tmo = &pbuf->params.hdrtmo;
	value = rkisp_read(stats_vdev->dev, ISP_HDRTMO_CTRL, true);
	if (value & ISP_HDRTMO_EN) {
		pbuf->meas_type |= ISP2X_STAT_HDRTMO;

		value = rkisp_read(stats_vdev->dev, ISP_HDRTMO_LG_RO0, true);
		tmo->lglow = value >> 16;
		tmo->lgmin = value & 0xFFFF;

		value = rkisp_read(stats_vdev->dev, ISP_HDRTMO_LG_RO1, true);
		tmo->lghigh = value >> 16;
		tmo->lgmax = value & 0xFFFF;

		value = rkisp_read(stats_vdev->dev, ISP_HDRTMO_LG_RO2, true);
		tmo->weightkey = (value >> 16) & 0xFF;
		tmo->lgmean = value & 0xFFFF;

		value = rkisp_read(stats_vdev->dev, ISP_HDRTMO_LG_RO3, true);
		tmo->lgrange1 = value >> 16;
		tmo->lgrange0 = value & 0xFFFF;

		value = rkisp_read(stats_vdev->dev, ISP_HDRTMO_LG_RO4, true);
		tmo->palpha = (value >> 16) & 0x3FF;
		tmo->lgavgmax = value & 0xFFFF;

		value = rkisp_read(stats_vdev->dev, ISP_HDRTMO_LG_RO5, true);
		tmo->linecnt = value & 0x1FFF;

		for (i = 0; i < ISP2X_HDRTMO_MINMAX_NUM; i++)
			tmo->min_max[i] = rkisp_read(stats_vdev->dev,
						     ISP_HDRTMO_HIST_RO0 + 4 * i, true);
	}
}

static void
rkisp_stats_get_dhaz_stats(struct rkisp_isp_stats_vdev *stats_vdev,
			   struct rkisp_isp2x_stat_buffer *pbuf)
{
	struct isp2x_dhaz_stat *dhaz;
	u32 value, i;

	if (!pbuf)
		return;

	dhaz = &pbuf->params.dhaz;
	value = rkisp_read(stats_vdev->dev, ISP_DHAZ_CTRL, true);
	if (value & ISP_DHAZ_ENMUX) {
		pbuf->meas_type |= ISP2X_STAT_DHAZ;

		value = rkisp_read(stats_vdev->dev, ISP_DHAZ_ADP_RD0, true);
		dhaz->dhaz_adp_air_base = value >> 16;
		dhaz->dhaz_adp_wt = value & 0xFFFF;

		value = rkisp_read(stats_vdev->dev, ISP_DHAZ_ADP_RD1, true);
		dhaz->dhaz_adp_gratio = value >> 16;
		dhaz->dhaz_adp_tmax = value & 0xFFFF;

		for (i = 0; i < ISP2X_DHAZ_HIST_IIR_NUM / 2; i++) {
			value = rkisp_read(stats_vdev->dev, ISP_DHAZ_HIST_REG0 + 4 * i, true);
			dhaz->h_r_iir[2 * i] = value & 0xFFFF;
			dhaz->h_r_iir[2 * i + 1] = value >> 16;
		}

		for (i = 0; i < ISP2X_DHAZ_HIST_IIR_NUM / 2; i++) {
			value = rkisp_read(stats_vdev->dev, ISP_DHAZ_HIST_REG32 + 4 * i, true);
			dhaz->h_g_iir[2 * i] = value & 0xFFFF;
			dhaz->h_g_iir[2 * i + 1] = value >> 16;
		}

		for (i = 0; i < ISP2X_DHAZ_HIST_IIR_NUM / 2; i++) {
			value = rkisp_read(stats_vdev->dev, ISP_DHAZ_HIST_REG64 + 4 * i, true);
			dhaz->h_b_iir[2 * i] = value & 0xFFFF;
			dhaz->h_b_iir[2 * i + 1] = value >> 16;
		}
	}
}

static struct rkisp_stats_v2x_ops __maybe_unused rkisp_stats_reg_ops_v2x = {
	.get_siawb_meas = rkisp_stats_get_siawb_meas_reg,
	.get_rawawb_meas = rkisp_stats_get_rawawb_meas_reg,
	.get_siaf_meas = rkisp_stats_get_siaf_meas_reg,
	.get_rawaf_meas = rkisp_stats_get_rawaf_meas_reg,
	.get_yuvae_meas = rkisp_stats_get_yuvae_meas_reg,
	.get_sihst_meas = rkisp_stats_get_sihst_meas_reg,
	.get_rawae0_meas = rkisp_stats_get_rawaelite_meas_reg,
	.get_rawhst0_meas = rkisp_stats_get_rawhstlite_meas_reg,
	.get_rawae1_meas = rkisp_stats_get_rawae1_meas_reg,
	.get_rawhst1_meas = rkisp_stats_get_rawhst1_meas_reg,
	.get_rawae2_meas = rkisp_stats_get_rawae2_meas_reg,
	.get_rawhst2_meas = rkisp_stats_get_rawhst2_meas_reg,
	.get_rawae3_meas = rkisp_stats_get_rawae3_meas_reg,
	.get_rawhst3_meas = rkisp_stats_get_rawhst3_meas_reg,
	.get_bls_stats = rkisp_stats_get_bls_stats,
	.get_tmo_stats = rkisp_stats_get_tmo_stats,
	.get_dhaz_stats = rkisp_stats_get_dhaz_stats,
};

static void
rkisp_stats_get_siawb_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
			       struct rkisp_isp2x_stat_buffer *pbuf)
{
	u32 reg_val;
	u32 *ddr_addr;
	u32 rd_buf_idx;

	if (!pbuf)
		return;

	pbuf->meas_type |= ISP2X_STAT_SIAWB;

	rd_buf_idx = stats_vdev->rd_buf_idx;
	ddr_addr = stats_vdev->stats_buf[rd_buf_idx].vaddr + 0x2C60;

	reg_val = ddr_addr[0];
	pbuf->params.siawb.awb_mean[0].cnt = ISP2X_SIAWB_GET_PIXEL_CNT(reg_val);

	reg_val = ddr_addr[1];
	pbuf->params.siawb.awb_mean[0].mean_cr_or_r =
		ISP2X_SIAWB_GET_MEAN_CR_R(reg_val);
	pbuf->params.siawb.awb_mean[0].mean_cb_or_b =
		ISP2X_SIAWB_GET_MEAN_CB_B(reg_val);
	pbuf->params.siawb.awb_mean[0].mean_y_or_g =
		ISP2X_SIAWB_GET_MEAN_Y_G(reg_val);
}

static void
rkisp_stats_get_rawawb_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp_isp2x_stat_buffer *pbuf)
{
	u32 value, rd_buf_idx, tmp_oft;
	u32 *reg_addr, *raw_addr;
	u64 msb, lsb;
	u32 i;

	if (!pbuf)
		goto OUT;

	pbuf->meas_type |= ISP2X_STAT_RAWAWB;

	tmp_oft = ISP_RAWAWB_SUM_R_NOR_0;
	rd_buf_idx = stats_vdev->rd_buf_idx;
	raw_addr = stats_vdev->stats_buf[rd_buf_idx].vaddr + 0x2000;
	reg_addr = stats_vdev->stats_buf[rd_buf_idx].vaddr + 0x2710;

	for (i = 0; i < ISP2X_RAWAWB_SUM_NUM; i++) {
		pbuf->params.rawawb.ro_rawawb_sum_r_nor[i] =
			reg_addr[(ISP_RAWAWB_SUM_R_NOR_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_rawawb_sum_g_nor[i] =
			reg_addr[(ISP_RAWAWB_SUM_G_NOR_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_rawawb_sum_b_nor[i] =
			reg_addr[(ISP_RAWAWB_SUM_B_NOR_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_rawawb_wp_num_nor[i] =
			reg_addr[(ISP_RAWAWB_WP_NUM_NOR_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_rawawb_sum_r_big[i] =
			reg_addr[(ISP_RAWAWB_SUM_R_BIG_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_rawawb_sum_g_big[i] =
			reg_addr[(ISP_RAWAWB_SUM_G_BIG_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_rawawb_sum_b_big[i] =
			reg_addr[(ISP_RAWAWB_SUM_B_BIG_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_rawawb_wp_num_big[i] =
			reg_addr[(ISP_RAWAWB_WP_NUM_BIG_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_rawawb_sum_r_sma[i] =
			reg_addr[(ISP_RAWAWB_SUM_R_SMA_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_rawawb_sum_g_sma[i] =
			reg_addr[(ISP_RAWAWB_SUM_G_SMA_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_rawawb_sum_b_sma[i] =
			reg_addr[(ISP_RAWAWB_SUM_B_SMA_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_rawawb_wp_num_sma[i] =
			reg_addr[(ISP_RAWAWB_WP_NUM_SMA_0 - tmp_oft + 0x30 * i) / 4];
	}

	for (i = 0; i < ISP2X_RAWAWB_MULWD_NUM; i++) {
		pbuf->params.rawawb.ro_sum_r_nor_multiwindow[i] =
			reg_addr[(ISP_RAWAWB_SUM_R_NOR_MULTIWINDOW_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_sum_g_nor_multiwindow[i] =
			reg_addr[(ISP_RAWAWB_SUM_G_NOR_MULTIWINDOW_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_sum_b_nor_multiwindow[i] =
			reg_addr[(ISP_RAWAWB_SUM_B_NOR_MULTIWINDOW_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_wp_nm_nor_multiwindow[i] =
			reg_addr[(ISP_RAWAWB_WP_NM_NOR_MULTIWINDOW_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_sum_r_big_multiwindow[i] =
			reg_addr[(ISP_RAWAWB_SUM_R_BIG_MULTIWINDOW_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_sum_g_big_multiwindow[i] =
			reg_addr[(ISP_RAWAWB_SUM_G_BIG_MULTIWINDOW_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_sum_b_big_multiwindow[i] =
			reg_addr[(ISP_RAWAWB_SUM_B_BIG_MULTIWINDOW_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_wp_nm_big_multiwindow[i] =
			reg_addr[(ISP_RAWAWB_WP_NM_BIG_MULTIWINDOW_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_sum_r_sma_multiwindow[i] =
			reg_addr[(ISP_RAWAWB_SUM_R_SMA_MULTIWINDOW_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_sum_g_sma_multiwindow[i] =
			reg_addr[(ISP_RAWAWB_SUM_G_SMA_MULTIWINDOW_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_sum_b_sma_multiwindow[i] =
			reg_addr[(ISP_RAWAWB_SUM_B_SMA_MULTIWINDOW_0 - tmp_oft + 0x30 * i) / 4];
		pbuf->params.rawawb.ro_wp_nm_sma_multiwindow[i] =
			reg_addr[(ISP_RAWAWB_WP_NM_SMA_MULTIWINDOW_0 - tmp_oft + 0x30 * i) / 4];
	}

	for (i = 0; i < ISP2X_RAWAWB_SUM_NUM; i++) {
		pbuf->params.rawawb.ro_sum_r_exc[i] =
			reg_addr[(ISP_RAWAWB_SUM_R_EXC_0 - tmp_oft + 0x10 * i) / 4];
		pbuf->params.rawawb.ro_sum_g_exc[i] =
			reg_addr[(ISP_RAWAWB_SUM_G_EXC_0 - tmp_oft + 0x10 * i) / 4];
		pbuf->params.rawawb.ro_sum_b_exc[i] =
			reg_addr[(ISP_RAWAWB_SUM_B_EXC_0 - tmp_oft + 0x10 * i) / 4];
		pbuf->params.rawawb.ro_wp_nm_exc[i] =
			reg_addr[(ISP_RAWAWB_WP_NM_EXC_0 - tmp_oft + 0x10 * i) / 4];
	}

	for (i = 0; i < ISP2X_RAWAWB_RAMDATA_NUM / 2; i++) {
		lsb = raw_addr[4 * i];
		msb = raw_addr[4 * i + 1];
		pbuf->params.rawawb.ramdata[2 * i].b = lsb & 0x3FFFF;
		pbuf->params.rawawb.ramdata[2 * i].g = ((lsb & 0xFFFC0000) >> 18) | (msb & 0xF) << 14;
		pbuf->params.rawawb.ramdata[2 * i].r = (msb & 0x3FFFF0) >> 4;
		pbuf->params.rawawb.ramdata[2 * i].wp = (msb & 0x01C00000) >> 22;

		lsb = ((raw_addr[4 * i + 2] & 0x3FFFFFF) << 6) | ((raw_addr[4 * i + 1] & 0xFC000000) >> 26);
		msb = ((raw_addr[4 * i + 3] & 0x3FFFFFF) << 6) | ((raw_addr[4 * i + 2] & 0xFC000000) >> 26);
		pbuf->params.rawawb.ramdata[2 * i + 1].b = lsb & 0x3FFFF;
		pbuf->params.rawawb.ramdata[2 * i + 1].g = ((lsb & 0xFFFC0000) >> 18) | (msb & 0xF) << 14;
		pbuf->params.rawawb.ramdata[2 * i + 1].r = (msb & 0x3FFFF0) >> 4;
		pbuf->params.rawawb.ramdata[2 * i + 1].wp = (msb & 0x01C00000) >> 22;
	}

	lsb = raw_addr[4 * i];
	msb = raw_addr[4 * i + 1];
	pbuf->params.rawawb.ramdata[2 * i].b = lsb & 0x3FFFF;
	pbuf->params.rawawb.ramdata[2 * i].g = ((lsb & 0xFFFC0000) >> 18) | (msb & 0xF) << 14;
	pbuf->params.rawawb.ramdata[2 * i].r = (msb & 0x3FFFF0) >> 4;
	pbuf->params.rawawb.ramdata[2 * i].wp = (msb & 0x01C00000) >> 22;

OUT:
	value = rkisp_read(stats_vdev->dev, ISP_RAWAWB_CTRL, true);
	value |= ISP2X_3A_MEAS_DONE;
	rkisp_write(stats_vdev->dev, ISP_RAWAWB_CTRL, value, true);
}

static void
rkisp_stats_get_siaf_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
			      struct rkisp_isp2x_stat_buffer *pbuf)
{
	struct isp2x_siaf_stat *af;
	u32 *ddr_addr;
	u32 rd_buf_idx;

	if (!pbuf)
		return;

	pbuf->meas_type |= ISP2X_STAT_SIAF;

	af = &pbuf->params.siaf;
	rd_buf_idx = stats_vdev->rd_buf_idx;
	ddr_addr = stats_vdev->stats_buf[rd_buf_idx].vaddr + 0x2C40;
	af->win[0].sum = ddr_addr[0];
	af->win[0].lum = ddr_addr[4];
	af->win[1].sum = ddr_addr[1];
	af->win[1].lum = ddr_addr[5];
	af->win[2].sum = ddr_addr[2];
	af->win[2].lum = ddr_addr[6];
}

static void
rkisp_stats_get_rawaf_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
			       struct rkisp_isp2x_stat_buffer *pbuf)
{
	u32 *ddr_addr;
	struct isp2x_rawaf_stat *af;
	u32 value, rd_buf_idx;
	int i;

	if (!pbuf)
		goto OUT;

	af = &pbuf->params.rawaf;
	pbuf->meas_type |= ISP2X_STAT_RAWAF;

	rd_buf_idx = stats_vdev->rd_buf_idx;
	ddr_addr = stats_vdev->stats_buf[rd_buf_idx].vaddr + 0x1C00;

	af->afm_sum[0] = rkisp_read(stats_vdev->dev, ISP_RAWAF_SUM_A, true);
	af->afm_sum[1] = rkisp_read(stats_vdev->dev, ISP_RAWAF_SUM_B, true);
	af->afm_lum[0] = rkisp_read(stats_vdev->dev, ISP_RAWAF_LUM_A, true);
	af->afm_lum[1] = rkisp_read(stats_vdev->dev, ISP_RAWAF_LUM_B, true);
	af->int_state = rkisp_read(stats_vdev->dev, ISP_RAWAF_INT_STATE, true);

	for (i = 0; i < ISP2X_RAWAF_SUMDATA_ROW * ISP2X_RAWAF_SUMDATA_COLUMN; i++)
		af->ramdata[i] = ddr_addr[i];

OUT:
	value = rkisp_read(stats_vdev->dev, ISP_RAWAF_CTRL, true);
	value |= ISP2X_3A_MEAS_DONE;
	rkisp_write(stats_vdev->dev, ISP_RAWAF_CTRL, value, true);
	rkisp_write(stats_vdev->dev, ISP_RAWAF_INT_STATE, 0, true);
}

static void
rkisp_stats_get_yuvae_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
			       struct rkisp_isp2x_stat_buffer *pbuf)
{
	u32 *ddr_addr;
	u32 value, rd_buf_idx;
	int i;

	if (!pbuf)
		goto OUT;

	pbuf->meas_type |= ISP2X_STAT_YUVAE;

	rd_buf_idx = stats_vdev->rd_buf_idx;
	ddr_addr = stats_vdev->stats_buf[rd_buf_idx].vaddr + 0x2B00;
	for (i = 0; i < ISP2X_YUVAE_MEAN_NUM / 4; i++) {
		value = ddr_addr[i];
		pbuf->params.yuvae.mean[4 * i + 0] = ISP2X_EXP_GET_MEAN_xy0(value);
		pbuf->params.yuvae.mean[4 * i + 1] = ISP2X_EXP_GET_MEAN_xy1(value);
		pbuf->params.yuvae.mean[4 * i + 2] = ISP2X_EXP_GET_MEAN_xy2(value);
		pbuf->params.yuvae.mean[4 * i + 3] = ISP2X_EXP_GET_MEAN_xy3(value);
	}
	value = ddr_addr[i];
	pbuf->params.yuvae.mean[4 * i + 0] = ISP2X_EXP_GET_MEAN_xy0(value);

	for (i = 0; i < ISP2X_YUVAE_SUBWIN_NUM; i++)
		pbuf->params.yuvae.ro_yuvae_sumy[i] =
			rkisp_read(stats_vdev->dev, ISP_YUVAE_WND1_SUMY + 4 * i, true);

OUT:
	value = rkisp_read(stats_vdev->dev, ISP_YUVAE_CTRL, true);
	value |= ISP2X_3A_MEAS_DONE;
	rkisp_write(stats_vdev->dev, ISP_YUVAE_CTRL, value, true);
}

static void
rkisp_stats_get_sihst_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
			       struct rkisp_isp2x_stat_buffer *pbuf)
{
	u32 *ddr_addr;
	u32 rd_buf_idx;
	u32 value;
	int i, j;

	if (!pbuf)
		return;

	pbuf->meas_type |= ISP2X_STAT_SIHST;

	rd_buf_idx = stats_vdev->rd_buf_idx;
	ddr_addr = stats_vdev->stats_buf[rd_buf_idx].vaddr + 0x2C00;
	for (i = 0; i < ISP2X_SIHIST_WIN_NUM; i++) {
		ddr_addr += i * 0x40;
		for (j = 0; j < ISP2X_SIHIST_BIN_N_MAX / 2; j++) {
			value = ddr_addr[j];
			pbuf->params.sihst.win_stat[i].hist_bins[2 * j] =
				ISP2X_HIST_GET_BIN0(value);
			pbuf->params.sihst.win_stat[i].hist_bins[2 * j + 1] =
				ISP2X_HIST_GET_BIN1(value);
		}
	}
}

static void
rkisp_stats_get_rawaebig_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				  struct isp2x_rawaebig_stat *ae, u32 blk_no)
{
	u32 *ddr_addr;
	u32 value, rd_buf_idx;
	u32 addr;
	int i;

	rd_buf_idx = stats_vdev->rd_buf_idx;
	ddr_addr = stats_vdev->stats_buf[rd_buf_idx].vaddr;

	switch (blk_no) {
	case 1:
		addr = RAWAE_BIG2_BASE;
		ddr_addr += 0x0390 >> 2;
		break;
	case 2:
		addr = RAWAE_BIG3_BASE;
		ddr_addr += 0x0720 >> 2;
		break;
	default:
		addr = RAWAE_BIG1_BASE;
		break;
	}

	if (!ae)
		goto OUT;

	for (i = 0; i < ISP2X_RAWAEBIG_SUBWIN_NUM; i++)
		ae->sumr[i] = rkisp_read(stats_vdev->dev, addr + RAWAE_BIG_WND1_SUMR + i * 4, true);

	for (i = 0; i < ISP2X_RAWAEBIG_SUBWIN_NUM; i++)
		ae->sumg[i] = rkisp_read(stats_vdev->dev, addr + RAWAE_BIG_WND1_SUMG + i * 4, true);

	for (i = 0; i < ISP2X_RAWAEBIG_SUBWIN_NUM; i++)
		ae->sumb[i] = rkisp_read(stats_vdev->dev, addr + RAWAE_BIG_WND1_SUMB + i * 4, true);

	for (i = 0; i < ISP2X_RAWAEBIG_MEAN_NUM; i++) {
		value = ddr_addr[i];
		ae->data[i].channelg_xy = ISP2X_RAWAEBIG_GET_MEAN_G(value);
		ae->data[i].channelb_xy = ISP2X_RAWAEBIG_GET_MEAN_B(value);
		ae->data[i].channelr_xy = ISP2X_RAWAEBIG_GET_MEAN_R(value);
	}

OUT:
	value = rkisp_read(stats_vdev->dev, addr + RAWAE_BIG_CTRL, true);
	value |= ISP2X_3A_MEAS_DONE;
	rkisp_write(stats_vdev->dev, addr + RAWAE_BIG_CTRL, value, true);
}

static void
rkisp_stats_get_rawhstbig_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				   struct isp2x_rawhistbig_stat *hst, u32 blk_no)
{
	u32 *ddr_addr;
	u32 value, rd_buf_idx;
	u32 addr;
	int i;

	rd_buf_idx = stats_vdev->rd_buf_idx;
	ddr_addr = stats_vdev->stats_buf[rd_buf_idx].vaddr + 0x0C00;

	switch (blk_no) {
	case 1:
		addr = ISP_RAWHIST_BIG2_BASE;
		ddr_addr += 0x0800 >> 2;
		break;
	case 2:
		addr = ISP_RAWHIST_BIG3_BASE;
		ddr_addr += 0x0C00 >> 2;
		break;
	default:
		addr = ISP_RAWHIST_BIG1_BASE;
		break;
	}

	if (!hst)
		goto OUT;

	for (i = 0; i < ISP2X_HIST_BIN_N_MAX; i++)
		hst->hist_bin[i] = ddr_addr[i];

OUT:
	value = rkisp_read(stats_vdev->dev, addr + ISP_RAWHIST_BIG_CTRL, true);
	value |= ISP2X_3A_MEAS_DONE;
	rkisp_write(stats_vdev->dev, addr + ISP_RAWHIST_BIG_CTRL, value, true);
}

static void
rkisp_stats_get_rawae1_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp_isp2x_stat_buffer *pbuf)
{
	if (!pbuf)
		rkisp_stats_get_rawaebig_meas_ddr(stats_vdev, NULL, 1);
	else
		rkisp_stats_get_rawaebig_meas_ddr(stats_vdev, &pbuf->params.rawae1, 1);
}

static void
rkisp_stats_get_rawhst1_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				 struct rkisp_isp2x_stat_buffer *pbuf)
{
	if (!pbuf)
		rkisp_stats_get_rawhstbig_meas_ddr(stats_vdev, NULL, 1);
	else
		rkisp_stats_get_rawhstbig_meas_ddr(stats_vdev, &pbuf->params.rawhist1, 1);
}

static void
rkisp_stats_get_rawae2_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp_isp2x_stat_buffer *pbuf)
{
	if (!pbuf)
		rkisp_stats_get_rawaebig_meas_ddr(stats_vdev, NULL, 2);
	else
		rkisp_stats_get_rawaebig_meas_ddr(stats_vdev, &pbuf->params.rawae2, 2);
}

static void
rkisp_stats_get_rawhst2_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				 struct rkisp_isp2x_stat_buffer *pbuf)
{
	if (!pbuf)
		rkisp_stats_get_rawhstbig_meas_ddr(stats_vdev, NULL, 2);
	else
		rkisp_stats_get_rawhstbig_meas_ddr(stats_vdev, &pbuf->params.rawhist2, 2);
}

static void
rkisp_stats_get_rawae3_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp_isp2x_stat_buffer *pbuf)
{
	if (!pbuf)
		rkisp_stats_get_rawaebig_meas_ddr(stats_vdev, NULL, 0);
	else
		rkisp_stats_get_rawaebig_meas_ddr(stats_vdev, &pbuf->params.rawae3, 0);
}

static void
rkisp_stats_get_rawhst3_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				 struct rkisp_isp2x_stat_buffer *pbuf)
{
	if (!pbuf)
		rkisp_stats_get_rawhstbig_meas_ddr(stats_vdev, NULL, 0);
	else
		rkisp_stats_get_rawhstbig_meas_ddr(stats_vdev, &pbuf->params.rawhist3, 0);
}

static void
rkisp_stats_get_rawaelite_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				   struct rkisp_isp2x_stat_buffer *pbuf)
{
	struct isp2x_rawaelite_stat *ae;
	u32 *ddr_addr;
	u32 value, rd_buf_idx;
	int i;

	if (!pbuf)
		goto OUT;

	ae = &pbuf->params.rawae0;
	value = rkisp_read(stats_vdev->dev, ISP_RAWAE_LITE_CTRL, true);
	if ((value & ISP2X_3A_MEAS_DONE) == 0)
		return;

	rd_buf_idx = stats_vdev->rd_buf_idx;
	ddr_addr = stats_vdev->stats_buf[rd_buf_idx].vaddr + 0x0AB0;
	for (i = 0; i < ISP2X_RAWAELITE_MEAN_NUM; i++) {
		value = ddr_addr[i];
		ae->data[i].channelg_xy = ISP2X_RAWAEBIG_GET_MEAN_G(value);
		ae->data[i].channelb_xy = ISP2X_RAWAEBIG_GET_MEAN_B(value);
		ae->data[i].channelr_xy = ISP2X_RAWAEBIG_GET_MEAN_R(value);
	}

OUT:
	value = rkisp_read(stats_vdev->dev, ISP_RAWAE_LITE_CTRL, true);
	value |= ISP2X_3A_MEAS_DONE;
	rkisp_write(stats_vdev->dev, ISP_RAWAE_LITE_CTRL, value, true);
}

static void
rkisp_stats_get_rawhstlite_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				    struct rkisp_isp2x_stat_buffer *pbuf)
{
	struct isp2x_rawhistlite_stat *hst;
	u32 *ddr_addr;
	u32 value, rd_buf_idx;
	int i;

	if (!pbuf)
		goto OUT;

	hst = &pbuf->params.rawhist0;

	rd_buf_idx = stats_vdev->rd_buf_idx;
	ddr_addr = stats_vdev->stats_buf[rd_buf_idx].vaddr + 0x0C00 + 0x0400;

	for (i = 0; i < ISP2X_HIST_BIN_N_MAX; i++)
		hst->hist_bin[i] = ddr_addr[i];

OUT:
	value = rkisp_read(stats_vdev->dev, ISP_RAWHIST_LITE_CTRL, true);
	value |= ISP2X_3A_MEAS_DONE;
	rkisp_write(stats_vdev->dev, ISP_RAWHIST_LITE_CTRL, value, true);
}

static struct rkisp_stats_v2x_ops __maybe_unused rkisp_stats_ddr_ops_v2x = {
	.get_siawb_meas = rkisp_stats_get_siawb_meas_ddr,
	.get_rawawb_meas = rkisp_stats_get_rawawb_meas_ddr,
	.get_siaf_meas = rkisp_stats_get_siaf_meas_ddr,
	.get_rawaf_meas = rkisp_stats_get_rawaf_meas_ddr,
	.get_yuvae_meas = rkisp_stats_get_yuvae_meas_ddr,
	.get_sihst_meas = rkisp_stats_get_sihst_meas_ddr,
	.get_rawae0_meas = rkisp_stats_get_rawaelite_meas_ddr,
	.get_rawhst0_meas = rkisp_stats_get_rawhstlite_meas_ddr,
	.get_rawae1_meas = rkisp_stats_get_rawae1_meas_ddr,
	.get_rawhst1_meas = rkisp_stats_get_rawhst1_meas_ddr,
	.get_rawae2_meas = rkisp_stats_get_rawae2_meas_ddr,
	.get_rawhst2_meas = rkisp_stats_get_rawhst2_meas_ddr,
	.get_rawae3_meas = rkisp_stats_get_rawae3_meas_ddr,
	.get_rawhst3_meas = rkisp_stats_get_rawhst3_meas_ddr,
	.get_bls_stats = rkisp_stats_get_bls_stats,
	.get_tmo_stats = rkisp_stats_get_tmo_stats,
	.get_dhaz_stats = rkisp_stats_get_dhaz_stats,
};

static void
rkisp_stats_send_meas_v2x(struct rkisp_isp_stats_vdev *stats_vdev,
			  struct rkisp_isp_readout_work *meas_work)
{
	unsigned int cur_frame_id = -1;
	struct rkisp_isp2x_stat_buffer *cur_stat_buf = NULL;
	struct rkisp_buffer *cur_buf = NULL;
	struct rkisp_stats_v2x_ops *ops =
		(struct rkisp_stats_v2x_ops *)stats_vdev->priv_ops;

	cur_frame_id = meas_work->frame_id;
	spin_lock(&stats_vdev->rd_lock);
	/* get one empty buffer */
	if (!list_empty(&stats_vdev->stat)) {
		cur_buf = list_first_entry(&stats_vdev->stat,
					   struct rkisp_buffer, queue);
		list_del(&cur_buf->queue);
	}
	spin_unlock(&stats_vdev->rd_lock);

	if (cur_buf) {
		cur_stat_buf =
			(struct rkisp_isp2x_stat_buffer *)(cur_buf->vaddr[0]);
		cur_stat_buf->frame_id = cur_frame_id;
	}

	if (meas_work->isp_ris & ISP2X_SIAWB_DONE) {
		ops->get_siawb_meas(stats_vdev, cur_stat_buf);

		if (cur_stat_buf)
			cur_stat_buf->meas_type |= ISP2X_STAT_SIAWB;
	}

	if (meas_work->isp_ris & ISP2X_SIAF_FIN) {
		ops->get_siaf_meas(stats_vdev, cur_stat_buf);
		if (cur_stat_buf)
			cur_stat_buf->meas_type |= ISP2X_STAT_SIAF;
	}

	if (meas_work->isp_ris & ISP2X_AFM_SUM_OF)
		v4l2_warn(stats_vdev->vnode.vdev.v4l2_dev,
			  "ISP2X_AFM_SUM_OF\n");

	if (meas_work->isp_ris & ISP2X_AFM_LUM_OF)
		v4l2_warn(stats_vdev->vnode.vdev.v4l2_dev,
			  "ISP2X_AFM_LUM_OF\n");

	if (meas_work->isp_ris & ISP2X_YUVAE_END) {
		ops->get_yuvae_meas(stats_vdev, cur_stat_buf);
		if (cur_stat_buf)
			cur_stat_buf->meas_type |= ISP2X_STAT_YUVAE;
	}

	if (meas_work->isp_ris & ISP2X_SIHST_RDY) {
		ops->get_sihst_meas(stats_vdev, cur_stat_buf);
		if (cur_stat_buf)
			cur_stat_buf->meas_type |= ISP2X_STAT_SIHST;
	}

	if (meas_work->isp3a_ris & ISP2X_3A_RAWAWB) {
		ops->get_rawawb_meas(stats_vdev, cur_stat_buf);
		if (cur_stat_buf)
			cur_stat_buf->meas_type |= ISP2X_STAT_RAWAWB;
	}

	if (meas_work->isp3a_ris & ISP2X_3A_RAWAF) {
		ops->get_rawaf_meas(stats_vdev, cur_stat_buf);
		if (cur_stat_buf)
			cur_stat_buf->meas_type |= ISP2X_STAT_RAWAF;
	}

	if (meas_work->isp3a_ris & ISP2X_3A_RAWAF_SUM)
		v4l2_warn(stats_vdev->vnode.vdev.v4l2_dev,
			  "ISP2X_3A_RAWAF_SUM\n");

	if (meas_work->isp3a_ris & ISP2X_3A_RAWAF_LUM)
		v4l2_warn(stats_vdev->vnode.vdev.v4l2_dev,
			  "ISP2X_3A_RAWAF_LUM\n");

	if (meas_work->isp3a_ris & ISP2X_3A_RAWAE_BIG) {
		ops->get_rawae3_meas(stats_vdev, cur_stat_buf);
		if (cur_stat_buf)
			cur_stat_buf->meas_type |= ISP2X_STAT_RAWAE3;
	}

	if (meas_work->isp3a_ris & ISP2X_3A_RAWHIST_BIG) {
		ops->get_rawhst3_meas(stats_vdev, cur_stat_buf);
		if (cur_stat_buf)
			cur_stat_buf->meas_type |= ISP2X_STAT_RAWHST3;
	}

	if (meas_work->isp3a_ris & ISP2X_3A_RAWAE_CH0) {
		ops->get_rawae0_meas(stats_vdev, cur_stat_buf);
		if (cur_stat_buf)
			cur_stat_buf->meas_type |= ISP2X_STAT_RAWAE0;
	}

	if (meas_work->isp3a_ris & ISP2X_3A_RAWAE_CH1) {
		ops->get_rawae1_meas(stats_vdev, cur_stat_buf);
		if (cur_stat_buf)
			cur_stat_buf->meas_type |= ISP2X_STAT_RAWAE1;
	}

	if (meas_work->isp3a_ris & ISP2X_3A_RAWAE_CH2) {
		ops->get_rawae2_meas(stats_vdev, cur_stat_buf);
		if (cur_stat_buf)
			cur_stat_buf->meas_type |= ISP2X_STAT_RAWAE2;
	}

	if (meas_work->isp3a_ris & ISP2X_3A_RAWHIST_CH0) {
		ops->get_rawhst0_meas(stats_vdev, cur_stat_buf);
		if (cur_stat_buf)
			cur_stat_buf->meas_type |= ISP2X_STAT_RAWHST0;
	}

	if (meas_work->isp3a_ris & ISP2X_3A_RAWHIST_CH1) {
		ops->get_rawhst1_meas(stats_vdev, cur_stat_buf);
		if (cur_stat_buf)
			cur_stat_buf->meas_type |= ISP2X_STAT_RAWHST1;
	}

	if (meas_work->isp3a_ris & ISP2X_3A_RAWHIST_CH2) {
		ops->get_rawhst2_meas(stats_vdev, cur_stat_buf);
		if (cur_stat_buf)
			cur_stat_buf->meas_type |= ISP2X_STAT_RAWHST2;
	}

	if (meas_work->isp_ris & ISP2X_FRAME) {
		ops->get_bls_stats(stats_vdev, cur_stat_buf);
		ops->get_tmo_stats(stats_vdev, cur_stat_buf);
		ops->get_dhaz_stats(stats_vdev, cur_stat_buf);
	}

	if (cur_buf) {
		bool reg_withstream = false;
		struct v4l2_subdev *sd = v4l2_get_subdev_hostdata(&stats_vdev->dev->br_dev.sd);

		vb2_set_plane_payload(&cur_buf->vb.vb2_buf, 0,
				      sizeof(struct rkisp_isp2x_stat_buffer));
		cur_buf->vb.sequence = cur_frame_id;
		cur_buf->vb.vb2_buf.timestamp = meas_work->timestamp;
		v4l2_subdev_call(sd, core, ioctl, RKISP_ISPP_CMD_GET_REG_WITHSTREAM, &reg_withstream);
		if (reg_withstream) {
			struct rkisp_isp2x_stat_buffer *tmp_statsbuf;

			tmp_statsbuf = (struct rkisp_isp2x_stat_buffer *)stats_vdev->tmp_statsbuf.vaddr;
			if (tmp_statsbuf)
				memcpy(tmp_statsbuf, cur_stat_buf, sizeof(*cur_stat_buf));
		}
		vb2_buffer_done(&cur_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}
}

static void
rkisp_stats_clr_3a_isr(struct rkisp_isp_stats_vdev *stats_vdev,
		u32 isp_ris, u32 isp3a_ris)
{
	struct rkisp_stats_v2x_ops *ops =
		(struct rkisp_stats_v2x_ops *)stats_vdev->priv_ops;

	if (isp_ris & ISP2X_SIAWB_DONE)
		ops->get_siawb_meas(stats_vdev, NULL);

	if (isp_ris & ISP2X_SIAF_FIN)
		ops->get_siaf_meas(stats_vdev, NULL);

	if (isp_ris & ISP2X_YUVAE_END)
		ops->get_yuvae_meas(stats_vdev, NULL);

	if (isp_ris & ISP2X_SIHST_RDY)
		ops->get_sihst_meas(stats_vdev, NULL);

	if (isp3a_ris & ISP2X_3A_RAWAWB)
		ops->get_rawawb_meas(stats_vdev, NULL);

	if (isp3a_ris & ISP2X_3A_RAWAF)
		ops->get_rawaf_meas(stats_vdev, NULL);

	if (isp3a_ris & ISP2X_3A_RAWAE_BIG)
		ops->get_rawae3_meas(stats_vdev, NULL);

	if (isp3a_ris & ISP2X_3A_RAWHIST_BIG)
		ops->get_rawhst3_meas(stats_vdev, NULL);

	if (isp3a_ris & ISP2X_3A_RAWAE_CH0)
		ops->get_rawae0_meas(stats_vdev, NULL);

	if (isp3a_ris & ISP2X_3A_RAWAE_CH1)
		ops->get_rawae1_meas(stats_vdev, NULL);

	if (isp3a_ris & ISP2X_3A_RAWAE_CH2)
		ops->get_rawae2_meas(stats_vdev, NULL);

	if (isp3a_ris & ISP2X_3A_RAWHIST_CH0)
		ops->get_rawhst0_meas(stats_vdev, NULL);

	if (isp3a_ris & ISP2X_3A_RAWHIST_CH1)
		ops->get_rawhst1_meas(stats_vdev, NULL);

	if (isp3a_ris & ISP2X_3A_RAWHIST_CH2)
		ops->get_rawhst2_meas(stats_vdev, NULL);
}

static void
rkisp_stats_isr_v2x(struct rkisp_isp_stats_vdev *stats_vdev,
		    u32 isp_ris, u32 isp3a_ris)
{
	struct rkisp_device *dev = stats_vdev->dev;
	u32 isp_mis_tmp = 0;
	struct rkisp_isp_readout_work work;
	u32 cur_frame_id;
	u32 iq_isr_mask = ISP2X_SIAWB_DONE | ISP2X_SIAF_FIN |
		ISP2X_YUVAE_END | ISP2X_SIHST_RDY | ISP2X_AFM_SUM_OF | ISP2X_AFM_LUM_OF;
	u32 iq_3a_mask = 0;
	u32 hdl_ris, hdl_3aris, unhdl_ris, unhdl_3aris;
	u32 wr_buf_idx;
	u32 temp_isp_ris, temp_isp3a_ris;

	rkisp_dmarx_get_frame(stats_vdev->dev, &cur_frame_id, NULL, NULL, true);
#ifdef LOG_ISR_EXE_TIME
	ktime_t in_t = ktime_get();
#endif
	if (IS_HDR_RDBK(dev->hdr.op_mode))
		iq_3a_mask = ISP2X_3A_RAWAE_BIG;
	spin_lock(&stats_vdev->irq_lock);

	temp_isp_ris = rkisp_read(stats_vdev->dev, ISP_ISP_RIS, true);
	temp_isp3a_ris = rkisp_read(stats_vdev->dev, ISP_ISP3A_RIS, true);
	isp_mis_tmp = isp_ris & iq_isr_mask;
	if (isp_mis_tmp) {
		rkisp_write(stats_vdev->dev, ISP_ISP_ICR, isp_mis_tmp, true);

		isp_mis_tmp &= rkisp_read(stats_vdev->dev, ISP_ISP_MIS, true);
		if (isp_mis_tmp)
			v4l2_err(stats_vdev->vnode.vdev.v4l2_dev,
				 "isp icr 3A info err: 0x%x 0x%x\n",
				 isp_mis_tmp, isp_ris);
	}

	isp_mis_tmp = isp3a_ris & iq_3a_mask;
	if (isp_mis_tmp) {
		rkisp_write(stats_vdev->dev, ISP_ISP3A_ICR, isp_mis_tmp, true);

		isp_mis_tmp &= rkisp_read(stats_vdev->dev, ISP_ISP3A_MIS, true);
		if (isp_mis_tmp)
			v4l2_err(stats_vdev->vnode.vdev.v4l2_dev,
				 "isp3A icr 3A info err: 0x%x 0x%x\n",
				 isp_mis_tmp, isp_ris);
	}

	if (!stats_vdev->streamon)
		goto unlock;

	if ((isp_ris & ISP2X_FRAME) && stats_vdev->rd_stats_from_ddr) {
		wr_buf_idx = stats_vdev->wr_buf_idx;
		stats_vdev->rd_buf_idx = wr_buf_idx;

		wr_buf_idx = (wr_buf_idx + 1) % RKISP_STATS_DDR_BUF_NUM;
		stats_vdev->wr_buf_idx = wr_buf_idx;
		rkisp_write(stats_vdev->dev, MI_DBR_WR_SIZE,
			    RKISP_RD_STATS_BUF_SIZE, false);
		rkisp_write(stats_vdev->dev, MI_SWS_3A_WR_BASE,
			    stats_vdev->stats_buf[wr_buf_idx].dma_addr, false);
		rkisp_set_bits(stats_vdev->dev, CTRL_SWS_CFG, SW_3A_DDR_WRITE_EN,
			       SW_3A_DDR_WRITE_EN, false);
	}

	unhdl_ris = 0;
	unhdl_3aris = 0;
	if (stats_vdev->rdbk_mode) {
		hdl_ris = isp_ris & ~stats_vdev->isp_rdbk;
		hdl_3aris = isp3a_ris & ~stats_vdev->isp3a_rdbk;
		unhdl_ris = isp_ris & stats_vdev->isp_rdbk;
		unhdl_3aris = isp3a_ris & stats_vdev->isp3a_rdbk;
		stats_vdev->isp_rdbk |= hdl_ris;
		stats_vdev->isp3a_rdbk |= hdl_3aris;
	}

	if (isp_ris & CIF_ISP_FRAME) {
		work.readout = RKISP_ISP_READOUT_MEAS;
		work.frame_id = cur_frame_id;
		work.isp_ris = temp_isp_ris | isp_ris;
		work.isp3a_ris = temp_isp3a_ris | iq_3a_mask;
		work.timestamp = ktime_get_ns();

		if (!IS_HDR_RDBK(dev->hdr.op_mode)) {
			if (!kfifo_is_full(&stats_vdev->rd_kfifo))
				kfifo_in(&stats_vdev->rd_kfifo,
					 &work, sizeof(work));
			else
				v4l2_err(stats_vdev->vnode.vdev.v4l2_dev,
					 "stats kfifo is full\n");

			tasklet_schedule(&stats_vdev->rd_tasklet);
		} else {
			rkisp_stats_send_meas_v2x(stats_vdev, &work);
		}
	}

	/*
	 * The last time that rx perform 'back read' don't clear done flag
	 * in advance, otherwise the statistics will be abnormal.
	 */
	if (!(isp3a_ris & ISP2X_3A_RAWAE_BIG) ||
	    stats_vdev->dev->params_vdev.rdbk_times > 1)
		rkisp_stats_clr_3a_isr(stats_vdev, unhdl_ris, unhdl_3aris);

#ifdef LOG_ISR_EXE_TIME
	if (isp_ris & iq_isr_mask) {
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
rkisp_stats_rdbk_enable_v2x(struct rkisp_isp_stats_vdev *stats_vdev, bool en)
{
	if (!en) {
		stats_vdev->isp_rdbk = 0;
		stats_vdev->isp3a_rdbk = 0;
	}

	stats_vdev->rdbk_mode = en;
}

static struct rkisp_isp_stats_ops rkisp_isp_stats_ops_tbl = {
	.isr_hdl = rkisp_stats_isr_v2x,
	.send_meas = rkisp_stats_send_meas_v2x,
	.rdbk_enable = rkisp_stats_rdbk_enable_v2x,
};

void rkisp_stats_first_ddr_config_v2x(struct rkisp_isp_stats_vdev *stats_vdev)
{
	bool reg_withstream = false;
	struct v4l2_subdev *sd = v4l2_get_subdev_hostdata(&stats_vdev->dev->br_dev.sd);

	if (stats_vdev->rd_stats_from_ddr) {
		stats_vdev->wr_buf_idx = 0;
		stats_vdev->rd_buf_idx = 0;

		rkisp_write(stats_vdev->dev, MI_DBR_WR_SIZE,
			    RKISP_RD_STATS_BUF_SIZE, false);
		rkisp_write(stats_vdev->dev, MI_SWS_3A_WR_BASE,
			    stats_vdev->stats_buf[0].dma_addr, false);
		rkisp_set_bits(stats_vdev->dev, CTRL_SWS_CFG, SW_3A_DDR_WRITE_EN,
			       SW_3A_DDR_WRITE_EN, false);
	}

	v4l2_subdev_call(sd, core, ioctl, RKISP_ISPP_CMD_GET_REG_WITHSTREAM, &reg_withstream);
	if (reg_withstream) {
		stats_vdev->tmp_statsbuf.is_need_vaddr = true;
		stats_vdev->tmp_statsbuf.size = sizeof(struct rkisp_isp2x_stat_buffer);
		rkisp_alloc_buffer(stats_vdev->dev, &stats_vdev->tmp_statsbuf);
	}
}

void rkisp_init_stats_vdev_v2x(struct rkisp_isp_stats_vdev *stats_vdev)
{
	stats_vdev->vdev_fmt.fmt.meta.dataformat =
		V4L2_META_FMT_RK_ISP1_STAT_3A;
	stats_vdev->vdev_fmt.fmt.meta.buffersize =
		sizeof(struct rkisp_isp2x_stat_buffer);

	stats_vdev->ops = &rkisp_isp_stats_ops_tbl;
	stats_vdev->priv_ops = &rkisp_stats_reg_ops_v2x;

#ifdef RKISP_RD_STATS_FROM_DDR
	{
		int i;

		stats_vdev->priv_ops = &rkisp_stats_ddr_ops_v2x;
		stats_vdev->rd_stats_from_ddr = true;
		stats_vdev->rd_buf_idx = 0;
		stats_vdev->wr_buf_idx = 0;
		for (i = 0; i < RKISP_STATS_DDR_BUF_NUM; i++) {
			stats_vdev->stats_buf[i].is_need_vaddr = true;
			stats_vdev->stats_buf[i].size = RKISP_RD_STATS_BUF_SIZE;
			rkisp_alloc_buffer(stats_vdev->dev, &stats_vdev->stats_buf[i]);
		}
	}
#endif
}

void rkisp_uninit_stats_vdev_v2x(struct rkisp_isp_stats_vdev *stats_vdev)
{
	int i;

	for (i = 0; i < RKISP_STATS_DDR_BUF_NUM; i++)
		rkisp_free_buffer(stats_vdev->dev, &stats_vdev->stats_buf[i]);
}

