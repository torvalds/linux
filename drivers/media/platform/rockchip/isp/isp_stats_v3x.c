// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd. */

#include <linux/kfifo.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>	/* for ISP statistics */
#include "dev.h"
#include "regs.h"
#include "common.h"
#include "isp_stats.h"
#include "isp_stats_v3x.h"

#define ISP2X_HIST_GET_BIN0(x)		((x) & 0xFFFF)
#define ISP2X_HIST_GET_BIN1(x)		(((x) >> 16) & 0xFFFF)

#define ISP3X_3A_MEAS_DONE		BIT(31)

#define ISP2X_EXP_GET_MEAN_xy0(x)	((x) & 0xFF)
#define ISP2X_EXP_GET_MEAN_xy1(x)	(((x) >> 8) & 0xFF)
#define ISP2X_EXP_GET_MEAN_xy2(x)	(((x) >> 16) & 0xFF)
#define ISP2X_EXP_GET_MEAN_xy3(x)	(((x) >> 24) & 0xFF)

#define ISP3X_RAWAEBIG_GET_MEAN_G(x)	((x) & 0xFFF)
#define ISP3X_RAWAEBIG_GET_MEAN_B(x)	(((x) >> 12) & 0x3FF)
#define ISP3X_RAWAEBIG_GET_MEAN_R(x)	(((x) >> 22) & 0x3FF)

#define ISP2X_RAWAF_INT_LINE0_EN	BIT(27)

static int
rkisp_stats_get_rawawb_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp3x_isp_stat_buffer *pbuf)
{
	u64 msb, lsb;
	u32 value;
	int i;

	value = rkisp_read(stats_vdev->dev, ISP3X_RAWAWB_CTRL, true);
	if (!(value & ISP3X_3A_MEAS_DONE))
		return -ENODATA;

	if (!pbuf)
		return 0;

	pbuf->meas_type |= ISP3X_STAT_RAWAWB;
	for (i = 0; i < ISP3X_RAWAWB_HSTBIN_NUM / 2; i++) {
		value = rkisp_read(stats_vdev->dev, ISP3X_RAWAWB_Y_HIST01 + 4 * i, true);
		pbuf->params.rawawb.ro_yhist_bin[2 * i] = value & 0xFFFF;
		pbuf->params.rawawb.ro_yhist_bin[2 * i + 1] = (value & 0xFFFF0000) >> 16;
	}

	for (i = 0; i < ISP3X_RAWAWB_SUM_NUM; i++) {
		pbuf->params.rawawb.ro_rawawb_sum_rgain_nor[i] =
			rkisp_read(stats_vdev->dev, ISP3X_RAWAWB_SUM_RGAIN_NOR_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_rawawb_sum_bgain_nor[i] =
			rkisp_read(stats_vdev->dev, ISP3X_RAWAWB_SUM_BGAIN_NOR_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_rawawb_wp_num_nor[i] =
			rkisp_read(stats_vdev->dev, ISP3X_RAWAWB_WP_NUM_NOR_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_rawawb_sum_rgain_big[i] =
			rkisp_read(stats_vdev->dev, ISP3X_RAWAWB_SUM_RGAIN_BIG_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_rawawb_sum_bgain_big[i] =
			rkisp_read(stats_vdev->dev, ISP3X_RAWAWB_SUM_BGAIN_BIG_0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_rawawb_wp_num_big[i] =
			rkisp_read(stats_vdev->dev, ISP3X_RAWAWB_WP_NUM_BIG_0 + 0x30 * i, true);

		pbuf->params.rawawb.ro_wp_num2[i] =
			rkisp_read(stats_vdev->dev, ISP3X_RAWAWB_WPNUM2_0 + 4 * i, true);
	}

	for (i = 0; i < ISP3X_RAWAWB_MULWD_NUM; i++) {
		pbuf->params.rawawb.ro_sum_r_nor_multiwindow[i] =
			rkisp_read(stats_vdev->dev,
				   ISP3X_RAWAWB_SUM_R_NOR_MULTIWINDOW0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_sum_b_nor_multiwindow[i] =
			rkisp_read(stats_vdev->dev,
				   ISP3X_RAWAWB_SUM_B_NOR_MULTIWINDOW0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_wp_nm_nor_multiwindow[i] =
			rkisp_read(stats_vdev->dev,
				   ISP3X_RAWAWB_WP_NM_NOR_MULTIWINDOW0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_sum_r_big_multiwindow[i] =
			rkisp_read(stats_vdev->dev,
				   ISP3X_RAWAWB_SUM_R_BIG_MULTIWINDOW0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_sum_b_big_multiwindow[i] =
			rkisp_read(stats_vdev->dev,
				   ISP3X_RAWAWB_SUM_B_BIG_MULTIWINDOW0 + 0x30 * i, true);
		pbuf->params.rawawb.ro_wp_nm_big_multiwindow[i] =
			rkisp_read(stats_vdev->dev,
				   ISP3X_RAWAWB_WP_NM_BIG_MULTIWINDOW0 + 0x30 * i, true);
	}

	for (i = 0; i < ISP3X_RAWAWB_EXCL_STAT_NUM; i++) {
		pbuf->params.rawawb.ro_sum_r_exc[i] =
			rkisp_read(stats_vdev->dev, ISP3X_RAWAWB_SUM_R_EXC0 + 0x10 * i, true);
		pbuf->params.rawawb.ro_sum_b_exc[i] =
			rkisp_read(stats_vdev->dev, ISP3X_RAWAWB_SUM_B_EXC0 + 0x10 * i, true);
		pbuf->params.rawawb.ro_wp_nm_exc[i] =
			rkisp_read(stats_vdev->dev, ISP3X_RAWAWB_WP_NM_EXC0 + 0x10 * i, true);
	}

	for (i = 0; i < ISP3X_RAWAWB_RAMDATA_NUM; i++) {
		lsb = rkisp_read(stats_vdev->dev, ISP3X_RAWAWB_RAM_DATA_BASE, true);
		msb = rkisp_read(stats_vdev->dev, ISP3X_RAWAWB_RAM_DATA_BASE, true);
		pbuf->params.rawawb.ramdata[i].b = lsb & 0x3FFFF;
		pbuf->params.rawawb.ramdata[i].g = ((lsb & 0xFFFC0000) >> 18) | (msb & 0xF) << 14;
		pbuf->params.rawawb.ramdata[i].r = (msb & 0x3FFFF0) >> 4;
		pbuf->params.rawawb.ramdata[i].wp = (msb & 0xFFC00000) >> 22;
	}

	return 0;
}

static int
rkisp_stats_get_rawaf_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
			       struct rkisp3x_isp_stat_buffer *pbuf)
{
	struct isp3x_rawaf_stat *af;
	u32 i, value;

	value = rkisp_read(stats_vdev->dev, ISP3X_RAWAF_CTRL, true);
	if (!(value & ISP3X_3A_MEAS_DONE))
		return -ENODATA;

	if (!pbuf)
		return 0;

	af = &pbuf->params.rawaf;
	pbuf->meas_type |= ISP3X_STAT_RAWAF;
	af->afm_sum_b = rkisp_read(stats_vdev->dev, ISP3X_RAWAF_SUM_B, true);
	af->afm_lum_b = rkisp_read(stats_vdev->dev, ISP3X_RAWAF_LUM_B, true);
	af->int_state = rkisp_read(stats_vdev->dev, ISP3X_RAWAF_INT_STATE, true);
	af->highlit_cnt_winb = rkisp_read(stats_vdev->dev, ISP3X_RAWAF_HIGHLIT_CNT_WINB, true);

	for (i = 0; i < ISP3X_RAWAF_SUMDATA_NUM; i++) {
		af->ramdata[i].v1 = rkisp_read(stats_vdev->dev, ISP3X_RAWAF_RAM_DATA, true);
		af->ramdata[i].v2 = rkisp_read(stats_vdev->dev, ISP3X_RAWAF_RAM_DATA, true);
		af->ramdata[i].h1 = rkisp_read(stats_vdev->dev, ISP3X_RAWAF_RAM_DATA, true);
		af->ramdata[i].h2 = rkisp_read(stats_vdev->dev, ISP3X_RAWAF_RAM_DATA, true);
	}
	return 0;
}

static int
rkisp_stats_get_rawaebig_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				  struct isp2x_rawaebig_stat *ae, u32 blk_no)
{
	u32 addr, value;
	int i;

	switch (blk_no) {
	case 1:
		addr = ISP3X_RAWAE_BIG2_BASE;
		break;
	case 2:
		addr = ISP3X_RAWAE_BIG3_BASE;
		break;
	case 0:
	default:
		addr = ISP3X_RAWAE_BIG1_BASE;
		break;
	}

	value = rkisp_read(stats_vdev->dev, addr + ISP3X_RAWAE_BIG_CTRL, true);
	if (!(value & ISP3X_3A_MEAS_DONE))
		return -ENODATA;

	if (!ae)
		return 0;

	for (i = 0; i < ISP3X_RAWAEBIG_SUBWIN_NUM; i++)
		ae->sumr[i] = rkisp_read(stats_vdev->dev,
					 addr + ISP3X_RAWAE_BIG_WND1_SUMR + i * 4, true);

	for (i = 0; i < ISP3X_RAWAEBIG_SUBWIN_NUM; i++)
		ae->sumg[i] = rkisp_read(stats_vdev->dev,
					 addr + ISP3X_RAWAE_BIG_WND1_SUMG + i * 4, true);

	for (i = 0; i < ISP3X_RAWAEBIG_SUBWIN_NUM; i++)
		ae->sumb[i] = rkisp_read(stats_vdev->dev,
					 addr + ISP3X_RAWAE_BIG_WND1_SUMB + i * 4, true);

	for (i = 0; i < ISP3X_RAWAEBIG_MEAN_NUM; i++) {
		value = rkisp_read(stats_vdev->dev, addr + ISP3X_RAWAE_BIG_RO_MEAN_BASE_ADDR, true);
		ae->data[i].channelg_xy = ISP3X_RAWAEBIG_GET_MEAN_G(value);
		ae->data[i].channelb_xy = ISP3X_RAWAEBIG_GET_MEAN_B(value);
		ae->data[i].channelr_xy = ISP3X_RAWAEBIG_GET_MEAN_R(value);
	}

	return 0;
}

static int
rkisp_stats_get_rawhstbig_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				   struct isp2x_rawhistbig_stat *hst, u32 blk_no)
{
	u32 addr, value;
	int i;

	switch (blk_no) {
	case 1:
		addr = ISP3X_RAWHIST_BIG2_BASE;
		break;
	case 2:
		addr = ISP3X_RAWHIST_BIG3_BASE;
		break;
	case 0:
	default:
		addr = ISP3X_RAWHIST_BIG1_BASE;
		break;
	}

	value = rkisp_read(stats_vdev->dev, addr + ISP3X_RAWHIST_BIG_CTRL, true);
	if (!(value & ISP3X_3A_MEAS_DONE))
		return -ENODATA;

	if (!hst)
		return 0;

	for (i = 0; i < ISP3X_HIST_BIN_N_MAX; i++) {
		hst->hist_bin[i] = rkisp_read(stats_vdev->dev,
					      addr + ISP3X_RAWHIST_BIG_RO_BASE_BIN, true);
	}

	return 0;
}

static int
rkisp_stats_get_rawae1_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp3x_isp_stat_buffer *pbuf)
{
	int ret = 0;

	if (!pbuf) {
		rkisp_stats_get_rawaebig_meas_reg(stats_vdev, NULL, 1);
	} else {
		ret = rkisp_stats_get_rawaebig_meas_reg(stats_vdev, &pbuf->params.rawae1, 1);
		if (!ret)
			pbuf->meas_type |= ISP3X_STAT_RAWAE1;
	}

	return ret;
}

static int
rkisp_stats_get_rawhst1_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				 struct rkisp3x_isp_stat_buffer *pbuf)
{
	int ret = 0;

	if (!pbuf) {
		rkisp_stats_get_rawhstbig_meas_reg(stats_vdev, NULL, 1);
	} else {
		ret = rkisp_stats_get_rawhstbig_meas_reg(stats_vdev, &pbuf->params.rawhist1, 1);
		if (!ret)
			pbuf->meas_type |= ISP3X_STAT_RAWHST1;
	}

	return ret;
}

static int
rkisp_stats_get_rawae2_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp3x_isp_stat_buffer *pbuf)
{
	int ret = 0;

	if (!pbuf) {
		rkisp_stats_get_rawaebig_meas_reg(stats_vdev, NULL, 2);
	} else {
		ret = rkisp_stats_get_rawaebig_meas_reg(stats_vdev, &pbuf->params.rawae2, 2);
		if (!ret)
			pbuf->meas_type |= ISP3X_STAT_RAWAE2;
	}

	return ret;
}

static int
rkisp_stats_get_rawhst2_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				 struct rkisp3x_isp_stat_buffer *pbuf)
{
	int ret = 0;

	if (!pbuf) {
		rkisp_stats_get_rawhstbig_meas_reg(stats_vdev, NULL, 2);
	} else {
		ret = rkisp_stats_get_rawhstbig_meas_reg(stats_vdev, &pbuf->params.rawhist2, 2);
		if (!ret)
			pbuf->meas_type |= ISP3X_STAT_RAWHST2;
	}

	return ret;
}

static int
rkisp_stats_get_rawae3_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp3x_isp_stat_buffer *pbuf)
{
	int ret = 0;

	if (!pbuf) {
		rkisp_stats_get_rawaebig_meas_reg(stats_vdev, NULL, 0);
	} else {
		ret = rkisp_stats_get_rawaebig_meas_reg(stats_vdev, &pbuf->params.rawae3, 0);
		if (!ret)
			pbuf->meas_type |= ISP3X_STAT_RAWAE3;
	}

	return ret;
}

static int
rkisp_stats_get_rawhst3_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				 struct rkisp3x_isp_stat_buffer *pbuf)
{
	int ret = 0;

	if (!pbuf) {
		rkisp_stats_get_rawhstbig_meas_reg(stats_vdev, NULL, 0);
	} else {
		ret = rkisp_stats_get_rawhstbig_meas_reg(stats_vdev, &pbuf->params.rawhist3, 0);
		if (!ret)
			pbuf->meas_type |= ISP3X_STAT_RAWHST3;
	}

	return ret;
}

static int
rkisp_stats_get_rawaelite_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				   struct rkisp3x_isp_stat_buffer *pbuf)
{
	struct isp2x_rawaelite_stat *ae;
	u32 value;
	int i;

	value = rkisp_read(stats_vdev->dev, ISP3X_RAWAE_LITE_CTRL, true);
	if ((value & ISP3X_3A_MEAS_DONE) == 0)
		return -ENODATA;

	if (!pbuf)
		return 0;

	pbuf->meas_type |= ISP3X_STAT_RAWAE0;
	ae = &pbuf->params.rawae0;
	for (i = 0; i < ISP3X_RAWAELITE_MEAN_NUM; i++) {
		value = rkisp_read(stats_vdev->dev, ISP3X_RAWAE_LITE_RO_MEAN + 4 * i, true);
		ae->data[i].channelg_xy = ISP3X_RAWAEBIG_GET_MEAN_G(value);
		ae->data[i].channelb_xy = ISP3X_RAWAEBIG_GET_MEAN_B(value);
		ae->data[i].channelr_xy = ISP3X_RAWAEBIG_GET_MEAN_R(value);
	}

	return 0;
}

static int
rkisp_stats_get_rawhstlite_meas_reg(struct rkisp_isp_stats_vdev *stats_vdev,
				    struct rkisp3x_isp_stat_buffer *pbuf)
{
	struct isp2x_rawhistlite_stat *hst;
	u32 value;
	int i;

	value = rkisp_read(stats_vdev->dev, ISP3X_RAWHIST_LITE_CTRL, true);
	if ((value & ISP3X_3A_MEAS_DONE) == 0)
		return -ENODATA;

	if (!pbuf)
		return 0;

	pbuf->meas_type |= ISP3X_STAT_RAWHST0;
	hst = &pbuf->params.rawhist0;
	for (i = 0; i < ISP3X_HIST_BIN_N_MAX; i++)
		hst->hist_bin[i] = rkisp_read(stats_vdev->dev,
					      ISP3X_RAWHIST_LITE_RO_BASE_BIN, true);

	return 0;
}

static int
rkisp_stats_get_bls_stats(struct rkisp_isp_stats_vdev *stats_vdev,
			  struct rkisp3x_isp_stat_buffer *pbuf)
{
	struct ispsd_in_fmt in_fmt = stats_vdev->dev->isp_sdev.in_fmt;
	enum rkisp_fmt_raw_pat_type raw_type = in_fmt.bayer_pat;
	struct isp2x_bls_stat *bls;
	u32 value;

	if (!pbuf)
		return 0;

	bls = &pbuf->params.bls;
	value = rkisp_read(stats_vdev->dev, ISP3X_BLS_CTRL, true);
	if (value & (ISP_BLS_ENA | ISP_BLS_MODE_MEASURED)) {
		pbuf->meas_type |= ISP3X_STAT_BLS;

		switch (raw_type) {
		case RAW_BGGR:
			bls->meas_r = rkisp_read(stats_vdev->dev, ISP3X_BLS_D_MEASURED, true);
			bls->meas_gr = rkisp_read(stats_vdev->dev, ISP3X_BLS_C_MEASURED, true);
			bls->meas_gb = rkisp_read(stats_vdev->dev, ISP3X_BLS_B_MEASURED, true);
			bls->meas_b = rkisp_read(stats_vdev->dev, ISP3X_BLS_A_MEASURED, true);
			break;
		case RAW_GBRG:
			bls->meas_r = rkisp_read(stats_vdev->dev, ISP3X_BLS_C_MEASURED, true);
			bls->meas_gr = rkisp_read(stats_vdev->dev, ISP3X_BLS_D_MEASURED, true);
			bls->meas_gb = rkisp_read(stats_vdev->dev, ISP3X_BLS_A_MEASURED, true);
			bls->meas_b = rkisp_read(stats_vdev->dev, ISP3X_BLS_B_MEASURED, true);
			break;
		case RAW_GRBG:
			bls->meas_r = rkisp_read(stats_vdev->dev, ISP3X_BLS_B_MEASURED, true);
			bls->meas_gr = rkisp_read(stats_vdev->dev, ISP3X_BLS_A_MEASURED, true);
			bls->meas_gb = rkisp_read(stats_vdev->dev, ISP3X_BLS_D_MEASURED, true);
			bls->meas_b = rkisp_read(stats_vdev->dev, ISP3X_BLS_C_MEASURED, true);
			break;
		case RAW_RGGB:
			bls->meas_r = rkisp_read(stats_vdev->dev, ISP3X_BLS_A_MEASURED, true);
			bls->meas_gr = rkisp_read(stats_vdev->dev, ISP3X_BLS_B_MEASURED, true);
			bls->meas_gb = rkisp_read(stats_vdev->dev, ISP3X_BLS_C_MEASURED, true);
			bls->meas_b = rkisp_read(stats_vdev->dev, ISP3X_BLS_D_MEASURED, true);
			break;
		default:
			break;
		}
	}
	return 0;
}

static int
rkisp_stats_get_dhaz_stats(struct rkisp_isp_stats_vdev *stats_vdev,
			   struct rkisp3x_isp_stat_buffer *pbuf)
{
	struct isp3x_dhaz_stat *dhaz;
	u32 value, i;

	if (!pbuf)
		return 0;

	dhaz = &pbuf->params.dhaz;
	value = rkisp_read(stats_vdev->dev, ISP3X_DHAZ_CTRL, true);
	if (value & ISP_DHAZ_ENMUX) {
		pbuf->meas_type |= ISP3X_STAT_DHAZ;

		value = rkisp_read(stats_vdev->dev, ISP3X_DHAZ_SUMH_RD, true);
		dhaz->dhaz_pic_sumh = value;

		value = rkisp_read(stats_vdev->dev, ISP3X_DHAZ_ADP_RD0, true);
		dhaz->dhaz_adp_air_base = value >> 16;
		dhaz->dhaz_adp_wt = value & 0xFFFF;

		value = rkisp_read(stats_vdev->dev, ISP3X_DHAZ_ADP_RD1, true);
		dhaz->dhaz_adp_gratio = value >> 16;
		dhaz->dhaz_adp_tmax = value & 0xFFFF;

		for (i = 0; i < ISP3X_DHAZ_HIST_IIR_NUM / 2; i++) {
			value = rkisp_read(stats_vdev->dev, ISP3X_DHAZ_HIST_REG0 + 4 * i, true);
			dhaz->h_rgb_iir[2 * i] = value & 0xFFFF;
			dhaz->h_rgb_iir[2 * i + 1] = value >> 16;
		}
	}
	return 0;
}

static struct rkisp_stats_ops_v3x __maybe_unused stats_reg_ops_v3x = {
	.get_rawawb_meas = rkisp_stats_get_rawawb_meas_reg,
	.get_rawaf_meas = rkisp_stats_get_rawaf_meas_reg,
	.get_rawae0_meas = rkisp_stats_get_rawaelite_meas_reg,
	.get_rawhst0_meas = rkisp_stats_get_rawhstlite_meas_reg,
	.get_rawae1_meas = rkisp_stats_get_rawae1_meas_reg,
	.get_rawhst1_meas = rkisp_stats_get_rawhst1_meas_reg,
	.get_rawae2_meas = rkisp_stats_get_rawae2_meas_reg,
	.get_rawhst2_meas = rkisp_stats_get_rawhst2_meas_reg,
	.get_rawae3_meas = rkisp_stats_get_rawae3_meas_reg,
	.get_rawhst3_meas = rkisp_stats_get_rawhst3_meas_reg,
	.get_bls_stats = rkisp_stats_get_bls_stats,
	.get_dhaz_stats = rkisp_stats_get_dhaz_stats,
};

static int
rkisp_stats_get_rawawb_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp3x_isp_stat_buffer *pbuf)
{
	struct isp3x_rawawb_meas_stat *rawawb;
	u32 value, rd_buf_idx;
	u32 *reg_addr, *raw_addr;
	u64 msb, lsb;
	u32 i;

	if (!pbuf)
		return 0;

	rawawb = &pbuf->params.rawawb;
	pbuf->meas_type |= ISP3X_STAT_RAWAWB;
	rd_buf_idx = stats_vdev->rd_buf_idx;
	raw_addr = stats_vdev->stats_buf[rd_buf_idx].vaddr + 0x2b00;
	reg_addr = stats_vdev->stats_buf[rd_buf_idx].vaddr + 0x2b00 + 0x710;
	for (i = 0; i < ISP3X_RAWAWB_SUM_NUM; i++) {
		rawawb->ro_rawawb_sum_rgain_nor[i] =
			reg_addr[(0x20 * i + 0x0) / 4];
		rawawb->ro_rawawb_sum_bgain_nor[i] =
			reg_addr[(0x20 * i + 0x4) / 4];
		rawawb->ro_rawawb_wp_num_nor[i] =
			reg_addr[(0x20 * i + 0x8) / 4];
		rawawb->ro_wp_num2[i] =
			reg_addr[(0x20 * i + 0xc) / 4];
		rawawb->ro_rawawb_sum_rgain_big[i] =
			reg_addr[(0x20 * i + 0x10) / 4];
		rawawb->ro_rawawb_sum_bgain_big[i] =
			reg_addr[(0x20 * i + 0x14) / 4];
		rawawb->ro_rawawb_wp_num_big[i] =
			reg_addr[(0x20 * i + 0x18) / 4];
	}

	for (i = 0; i < ISP3X_RAWAWB_HSTBIN_NUM / 2; i++) {
		value = reg_addr[(0x04 * i + 0xE0) / 4];
		rawawb->ro_yhist_bin[2 * i] = value & 0xFFFF;
		rawawb->ro_yhist_bin[2 * i + 1] = (value & 0xFFFF0000) >> 16;
	}

	for (i = 0; i < ISP3X_RAWAWB_MULWD_NUM; i++) {
		rawawb->ro_sum_r_nor_multiwindow[i] =
			reg_addr[(0x20 * i + 0xF0) / 4];
		rawawb->ro_sum_b_nor_multiwindow[i] =
			reg_addr[(0x20 * i + 0xF4) / 4];
		rawawb->ro_wp_nm_nor_multiwindow[i] =
			reg_addr[(0x20 * i + 0xF8) / 4];
		rawawb->ro_sum_r_big_multiwindow[i] =
			reg_addr[(0x20 * i + 0x100) / 4];
		rawawb->ro_sum_b_big_multiwindow[i] =
			reg_addr[(0x20 * i + 0x104) / 4];
		rawawb->ro_wp_nm_big_multiwindow[i] =
			reg_addr[(0x20 * i + 0x108) / 4];
	}

	for (i = 0; i < ISP3X_RAWAWB_EXCL_STAT_NUM; i++) {
		rawawb->ro_sum_r_exc[i] = reg_addr[(0x10 * i + 0x170) / 4];
		rawawb->ro_sum_b_exc[i] = reg_addr[(0x10 * i + 0x174) / 4];
		rawawb->ro_wp_nm_exc[i] = reg_addr[(0x10 * i + 0x178) / 4];
	}

	for (i = 0; i < ISP3X_RAWAWB_RAMDATA_NUM; i++) {
		lsb = raw_addr[2 * i];
		msb = raw_addr[2 * i + 1];
		rawawb->ramdata[i].b = lsb & 0x3FFFF;
		rawawb->ramdata[i].g = ((lsb & 0xFFFC0000) >> 18) | (msb & 0xF) << 14;
		rawawb->ramdata[i].r = (msb & 0x3FFFF0) >> 4;
		rawawb->ramdata[i].wp = (msb & 0xFFC00000) >> 22;
	}
	return 0;
}

static int
rkisp_stats_get_rawaf_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
			       struct rkisp3x_isp_stat_buffer *pbuf)
{
	struct isp3x_rawaf_stat *af;
	u32 i, rd_buf_idx, *ddr_addr;

	if (!pbuf)
		return 0;

	af = &pbuf->params.rawaf;
	pbuf->meas_type |= ISP3X_STAT_RAWAF;

	rd_buf_idx = stats_vdev->rd_buf_idx;
	ddr_addr = stats_vdev->stats_buf[rd_buf_idx].vaddr + 0x1C00;

	af->afm_sum_b = rkisp_read(stats_vdev->dev, ISP3X_RAWAF_SUM_B, true);
	af->afm_lum_b = rkisp_read(stats_vdev->dev, ISP3X_RAWAF_LUM_B, true);
	af->int_state = rkisp_read(stats_vdev->dev, ISP3X_RAWAF_INT_STATE, true);

	for (i = 0; i < ISP3X_RAWAF_SUMDATA_NUM; i++) {
		af->ramdata[i].v1 = ddr_addr[i * 4];
		af->ramdata[i].v2 = ddr_addr[i * 4 + 1];
		af->ramdata[i].h1 = ddr_addr[i * 4 + 2];
		af->ramdata[i].h2 = ddr_addr[i * 4 + 3];
	}

	return 0;
}

static int
rkisp_stats_get_rawaebig_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				  struct isp2x_rawaebig_stat *ae, u32 blk_no)
{
	u32 i, value, addr, rd_buf_idx;
	u32 *ddr_addr;

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
		return 0;

	for (i = 0; i < ISP3X_RAWAEBIG_SUBWIN_NUM; i++)
		ae->sumr[i] = rkisp_read(stats_vdev->dev,
					 addr + ISP3X_RAWAE_BIG_WND1_SUMR + i * 4, true);

	for (i = 0; i < ISP3X_RAWAEBIG_SUBWIN_NUM; i++)
		ae->sumg[i] = rkisp_read(stats_vdev->dev,
					 addr + ISP3X_RAWAE_BIG_WND1_SUMG + i * 4, true);

	for (i = 0; i < ISP3X_RAWAEBIG_SUBWIN_NUM; i++)
		ae->sumb[i] = rkisp_read(stats_vdev->dev,
					 addr + ISP3X_RAWAE_BIG_WND1_SUMB + i * 4, true);

	for (i = 0; i < ISP3X_RAWAEBIG_MEAN_NUM; i++) {
		value = ddr_addr[i];
		ae->data[i].channelg_xy = ISP3X_RAWAEBIG_GET_MEAN_G(value);
		ae->data[i].channelb_xy = ISP3X_RAWAEBIG_GET_MEAN_B(value);
		ae->data[i].channelr_xy = ISP3X_RAWAEBIG_GET_MEAN_R(value);
	}

	return 0;
}

static int
rkisp_stats_get_rawhstbig_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				   struct isp2x_rawhistbig_stat *hst, u32 blk_no)
{
	u32 i, rd_buf_idx, *ddr_addr;

	rd_buf_idx = stats_vdev->rd_buf_idx;
	ddr_addr = stats_vdev->stats_buf[rd_buf_idx].vaddr + 0x0C00;

	switch (blk_no) {
	case 1:
		ddr_addr += 0x0800 >> 2;
		break;
	case 2:
		ddr_addr += 0x0C00 >> 2;
		break;
	default:
		break;
	}

	if (!hst)
		return 0;

	for (i = 0; i < ISP3X_HIST_BIN_N_MAX; i++)
		hst->hist_bin[i] = ddr_addr[i];

	return 0;
}

static int
rkisp_stats_get_rawae1_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp3x_isp_stat_buffer *pbuf)
{
	int ret = 0;

	if (!pbuf) {
		rkisp_stats_get_rawaebig_meas_ddr(stats_vdev, NULL, 1);
	} else {
		ret = rkisp_stats_get_rawaebig_meas_ddr(stats_vdev, &pbuf->params.rawae1, 1);
		if (!ret)
			pbuf->meas_type |= ISP3X_STAT_RAWAE1;
	}

	return ret;
}

static int
rkisp_stats_get_rawhst1_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				 struct rkisp3x_isp_stat_buffer *pbuf)
{
	int ret = 0;

	if (!pbuf) {
		rkisp_stats_get_rawhstbig_meas_ddr(stats_vdev, NULL, 1);
	} else {
		ret = rkisp_stats_get_rawhstbig_meas_ddr(stats_vdev, &pbuf->params.rawhist1, 1);
		if (!ret)
			pbuf->meas_type |= ISP3X_STAT_RAWHST1;
	}

	return ret;
}

static int
rkisp_stats_get_rawae2_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp3x_isp_stat_buffer *pbuf)
{
	int ret = 0;

	if (!pbuf) {
		rkisp_stats_get_rawaebig_meas_ddr(stats_vdev, NULL, 2);
	} else {
		ret = rkisp_stats_get_rawaebig_meas_ddr(stats_vdev, &pbuf->params.rawae2, 2);
		if (!ret)
			pbuf->meas_type |= ISP3X_STAT_RAWAE2;
	}

	return ret;
}

static int
rkisp_stats_get_rawhst2_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				 struct rkisp3x_isp_stat_buffer *pbuf)
{
	int ret = 0;

	if (!pbuf) {
		rkisp_stats_get_rawhstbig_meas_ddr(stats_vdev, NULL, 2);
	} else {
		ret = rkisp_stats_get_rawhstbig_meas_ddr(stats_vdev, &pbuf->params.rawhist2, 2);
		if (!ret)
			pbuf->meas_type |= ISP3X_STAT_RAWHST2;
	}

	return ret;
}

static int
rkisp_stats_get_rawae3_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp3x_isp_stat_buffer *pbuf)
{
	int ret = 0;

	if (!pbuf) {
		rkisp_stats_get_rawaebig_meas_ddr(stats_vdev, NULL, 0);
	} else {
		ret = rkisp_stats_get_rawaebig_meas_ddr(stats_vdev, &pbuf->params.rawae3, 0);
		if (!ret)
			pbuf->meas_type |= ISP3X_STAT_RAWAE3;
	}

	return ret;
}

static int
rkisp_stats_get_rawhst3_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				 struct rkisp3x_isp_stat_buffer *pbuf)
{
	int ret = 0;

	if (!pbuf) {
		rkisp_stats_get_rawhstbig_meas_ddr(stats_vdev, NULL, 0);
	} else {
		ret = rkisp_stats_get_rawhstbig_meas_ddr(stats_vdev, &pbuf->params.rawhist3, 0);
		if (!ret)
			pbuf->meas_type |= ISP3X_STAT_RAWHST3;
	}

	return ret;
}

static int
rkisp_stats_get_rawaelite_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				   struct rkisp3x_isp_stat_buffer *pbuf)
{
	struct isp2x_rawaelite_stat *ae;
	u32 i, value, rd_buf_idx, *ddr_addr;

	if (!pbuf)
		return 0;

	pbuf->meas_type |= ISP3X_STAT_RAWAE0;
	ae = &pbuf->params.rawae0;
	rd_buf_idx = stats_vdev->rd_buf_idx;
	ddr_addr = stats_vdev->stats_buf[rd_buf_idx].vaddr + 0x0AB0;
	for (i = 0; i < ISP3X_RAWAELITE_MEAN_NUM; i++) {
		value = ddr_addr[i];
		ae->data[i].channelg_xy = ISP3X_RAWAEBIG_GET_MEAN_G(value);
		ae->data[i].channelb_xy = ISP3X_RAWAEBIG_GET_MEAN_B(value);
		ae->data[i].channelr_xy = ISP3X_RAWAEBIG_GET_MEAN_R(value);
	}

	return 0;
}

static int
rkisp_stats_get_rawhstlite_meas_ddr(struct rkisp_isp_stats_vdev *stats_vdev,
				    struct rkisp3x_isp_stat_buffer *pbuf)
{
	struct isp2x_rawhistlite_stat *hst;
	u32 *ddr_addr, rd_buf_idx, i;

	if (!pbuf)
		return 0;

	pbuf->meas_type |= ISP3X_STAT_RAWHST0;
	hst = &pbuf->params.rawhist0;
	rd_buf_idx = stats_vdev->rd_buf_idx;
	ddr_addr = stats_vdev->stats_buf[rd_buf_idx].vaddr + 0x0C00 + 0x0400;

	for (i = 0; i < ISP3X_HIST_BIN_N_MAX; i++)
		hst->hist_bin[i] = ddr_addr[i];

	return 0;
}

static struct rkisp_stats_ops_v3x __maybe_unused stats_ddr_ops_v3x = {
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
};

static int
rkisp_stats_restart_rawawb_meas(struct rkisp_isp_stats_vdev *stats_vdev)
{
	u32 value;

	value = rkisp_read(stats_vdev->dev, ISP3X_RAWAWB_CTRL, true);
	if (value & ISP3X_3A_MEAS_DONE)
		rkisp_write(stats_vdev->dev, ISP3X_RAWAWB_CTRL, value, true);
	return 0;
}

static int
rkisp_stats_restart_rawaf_meas(struct rkisp_isp_stats_vdev *stats_vdev)
{
	u32 value;

	value = rkisp_read(stats_vdev->dev, ISP3X_RAWAF_CTRL, true);
	if (value & ISP3X_3A_MEAS_DONE) {
		rkisp_write(stats_vdev->dev, ISP3X_RAWAF_CTRL, value, true);
		rkisp_write(stats_vdev->dev, ISP3X_RAWAF_INT_STATE, 0, true);
	}
	return 0;
}

static int
rkisp_stats_restart_rawaebig_meas(struct rkisp_isp_stats_vdev *stats_vdev, u32 blk_no)
{
	u32 addr, value;

	switch (blk_no) {
	case 1:
		addr = ISP3X_RAWAE_BIG2_BASE;
		break;
	case 2:
		addr = ISP3X_RAWAE_BIG3_BASE;
		break;
	case 0:
	default:
		addr = ISP3X_RAWAE_BIG1_BASE;
		break;
	}

	value = rkisp_read(stats_vdev->dev, addr + ISP3X_RAWAE_BIG_CTRL, true);
	if (value & ISP3X_3A_MEAS_DONE)
		rkisp_write(stats_vdev->dev, addr + ISP3X_RAWAE_BIG_CTRL, value, true);
	return 0;
}

static int
rkisp_stats_restart_rawhstbig_meas(struct rkisp_isp_stats_vdev *stats_vdev, u32 blk_no)
{
	u32 addr, value;

	switch (blk_no) {
	case 1:
		addr = ISP3X_RAWHIST_BIG2_BASE;
		break;
	case 2:
		addr = ISP3X_RAWHIST_BIG3_BASE;
		break;
	case 0:
	default:
		addr = ISP3X_RAWHIST_BIG1_BASE;
		break;
	}

	value = rkisp_read(stats_vdev->dev, addr + ISP3X_RAWHIST_BIG_CTRL, true);
	if (value & ISP3X_3A_MEAS_DONE)
		rkisp_write(stats_vdev->dev, addr + ISP3X_RAWHIST_BIG_CTRL, value, true);
	return 0;
}

static int
rkisp_stats_restart_rawae1_meas(struct rkisp_isp_stats_vdev *stats_vdev)
{
	return rkisp_stats_restart_rawaebig_meas(stats_vdev, 1);
}

static int
rkisp_stats_restart_rawhst1_meas(struct rkisp_isp_stats_vdev *stats_vdev)
{
	return rkisp_stats_restart_rawhstbig_meas(stats_vdev, 1);
}

static int
rkisp_stats_restart_rawae2_meas(struct rkisp_isp_stats_vdev *stats_vdev)
{
	return rkisp_stats_restart_rawaebig_meas(stats_vdev, 2);
}

static int
rkisp_stats_restart_rawhst2_meas(struct rkisp_isp_stats_vdev *stats_vdev)
{
	return rkisp_stats_restart_rawhstbig_meas(stats_vdev, 2);
}

static int
rkisp_stats_restart_rawae3_meas(struct rkisp_isp_stats_vdev *stats_vdev)
{
	return rkisp_stats_restart_rawaebig_meas(stats_vdev, 0);
}

static int
rkisp_stats_restart_rawhst3_meas(struct rkisp_isp_stats_vdev *stats_vdev)
{
	return rkisp_stats_restart_rawhstbig_meas(stats_vdev, 0);
}

static int
rkisp_stats_restart_rawae0_meas(struct rkisp_isp_stats_vdev *stats_vdev)
{
	u32 value;

	value = rkisp_read(stats_vdev->dev, ISP3X_RAWAE_LITE_CTRL, true);
	if (value & ISP3X_3A_MEAS_DONE)
		rkisp_write(stats_vdev->dev, ISP3X_RAWAE_LITE_CTRL, value, true);
	return 0;
}

static int
rkisp_stats_restart_rawhst0_meas(struct rkisp_isp_stats_vdev *stats_vdev)
{
	u32 value;

	value = rkisp_read(stats_vdev->dev, ISP3X_RAWHIST_LITE_CTRL, true);
	if (value & ISP3X_3A_MEAS_DONE)
		rkisp_write(stats_vdev->dev, ISP3X_RAWHIST_LITE_CTRL, value, true);
	return 0;
}

static void
rkisp_stats_restart_meas(struct rkisp_isp_stats_vdev *stats_vdev)
{
	rkisp_stats_restart_rawawb_meas(stats_vdev);
	rkisp_stats_restart_rawaf_meas(stats_vdev);
	rkisp_stats_restart_rawae1_meas(stats_vdev);
	rkisp_stats_restart_rawae2_meas(stats_vdev);
	rkisp_stats_restart_rawae3_meas(stats_vdev);
	rkisp_stats_restart_rawae0_meas(stats_vdev);
	rkisp_stats_restart_rawhst1_meas(stats_vdev);
	rkisp_stats_restart_rawhst2_meas(stats_vdev);
	rkisp_stats_restart_rawhst3_meas(stats_vdev);
	rkisp_stats_restart_rawhst0_meas(stats_vdev);
}

static void
rkisp_stats_send_meas_v3x(struct rkisp_isp_stats_vdev *stats_vdev,
			  struct rkisp_isp_readout_work *meas_work)
{
	unsigned int cur_frame_id = -1;
	struct rkisp_buffer *cur_buf = stats_vdev->cur_buf;
	struct rkisp3x_isp_stat_buffer *cur_stat_buf = NULL;
	struct rkisp_stats_ops_v3x *ops =
		(struct rkisp_stats_ops_v3x *)stats_vdev->priv_ops;
	int ret = 0;

	cur_frame_id = meas_work->frame_id;
	spin_lock(&stats_vdev->rd_lock);
	/* get one empty buffer */
	if (!cur_buf) {
		if (!list_empty(&stats_vdev->stat)) {
			cur_buf = list_first_entry(&stats_vdev->stat,
						   struct rkisp_buffer, queue);
			list_del(&cur_buf->queue);
		}
	}
	spin_unlock(&stats_vdev->rd_lock);

	if (cur_buf) {
		cur_stat_buf =
			(struct rkisp3x_isp_stat_buffer *)(cur_buf->vaddr[0]);
		cur_stat_buf->frame_id = cur_frame_id;
	}

	if (meas_work->isp_ris & ISP3X_AFM_SUM_OF)
		v4l2_warn(stats_vdev->vnode.vdev.v4l2_dev,
			  "ISP3X_AFM_SUM_OF\n");

	if (meas_work->isp_ris & ISP3X_AFM_LUM_OF)
		v4l2_warn(stats_vdev->vnode.vdev.v4l2_dev,
			  "ISP3X_AFM_LUM_OF\n");

	if (meas_work->isp3a_ris & ISP3X_3A_RAWAWB)
		ret |= ops->get_rawawb_meas(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWAF)
		ret |= ops->get_rawaf_meas(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWAF_SUM)
		v4l2_warn(stats_vdev->vnode.vdev.v4l2_dev,
			  "ISP3X_3A_RAWAF_SUM\n");

	if (meas_work->isp3a_ris & ISP3X_3A_RAWAF_LUM)
		v4l2_warn(stats_vdev->vnode.vdev.v4l2_dev,
			  "ISP3X_3A_RAWAF_LUM\n");

	if (meas_work->isp3a_ris & ISP3X_3A_RAWAE_BIG)
		ret |= ops->get_rawae3_meas(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWHIST_BIG)
		ret |= ops->get_rawhst3_meas(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWAE_CH0)
		ret |= ops->get_rawae0_meas(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWAE_CH1)
		ret |= ops->get_rawae1_meas(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWAE_CH2)
		ret |= ops->get_rawae2_meas(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWHIST_CH0)
		ret |= ops->get_rawhst0_meas(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWHIST_CH1)
		ret |= ops->get_rawhst1_meas(stats_vdev, cur_stat_buf);

	if (meas_work->isp3a_ris & ISP3X_3A_RAWHIST_CH2)
		ret |= ops->get_rawhst2_meas(stats_vdev, cur_stat_buf);

	if (meas_work->isp_ris & ISP3X_FRAME) {
		ret |= ops->get_bls_stats(stats_vdev, cur_stat_buf);
		ret |= ops->get_dhaz_stats(stats_vdev, cur_stat_buf);
	}

	rkisp_stats_restart_meas(stats_vdev);

	if (cur_buf && !ret) {
		vb2_set_plane_payload(&cur_buf->vb.vb2_buf, 0,
				      sizeof(struct rkisp3x_isp_stat_buffer));
		cur_buf->vb.sequence = cur_frame_id;
		cur_buf->vb.vb2_buf.timestamp = meas_work->timestamp;
		vb2_buffer_done(&cur_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		cur_buf = NULL;
	}

	stats_vdev->cur_buf = cur_buf;
}

static void
rkisp_stats_isr_v3x(struct rkisp_isp_stats_vdev *stats_vdev,
		    u32 isp_ris, u32 isp3a_ris)
{
	struct rkisp_device *dev = stats_vdev->dev;
	struct rkisp_isp_readout_work work;
	u32 iq_isr_mask = ISP3X_SIAWB_DONE | ISP3X_SIAF_FIN |
		ISP3X_EXP_END | ISP3X_SIHST_RDY | ISP3X_AFM_SUM_OF | ISP3X_AFM_LUM_OF;
	u32 cur_frame_id, isp_mis_tmp = 0, iq_3a_mask = 0;
	u32 wr_buf_idx, temp_isp_ris, temp_isp3a_ris;

	rkisp_dmarx_get_frame(stats_vdev->dev, &cur_frame_id, NULL, NULL, true);

	if (IS_HDR_RDBK(dev->hdr.op_mode))
		iq_3a_mask = ISP3X_3A_RAWAE_BIG;

	spin_lock(&stats_vdev->irq_lock);

	temp_isp_ris = rkisp_read(stats_vdev->dev, ISP3X_ISP_RIS, true);
	temp_isp3a_ris = rkisp_read(stats_vdev->dev, ISP3X_ISP_3A_RIS, true);
	isp_mis_tmp = isp_ris & iq_isr_mask;
	if (isp_mis_tmp) {
		rkisp_write(stats_vdev->dev, ISP3X_ISP_ICR, isp_mis_tmp, true);

		isp_mis_tmp &= rkisp_read(stats_vdev->dev, ISP3X_ISP_MIS, true);
		if (isp_mis_tmp)
			v4l2_err(stats_vdev->vnode.vdev.v4l2_dev,
				 "isp icr 3A info err: 0x%x 0x%x\n",
				 isp_mis_tmp, isp_ris);
	}

	isp_mis_tmp = isp3a_ris & iq_3a_mask;
	if (isp_mis_tmp) {
		rkisp_write(stats_vdev->dev, ISP3X_ISP_3A_ICR, isp_mis_tmp, true);

		isp_mis_tmp &= rkisp_read(stats_vdev->dev, ISP3X_ISP_3A_MIS, true);
		if (isp_mis_tmp)
			v4l2_err(stats_vdev->vnode.vdev.v4l2_dev,
				 "isp3A icr 3A info err: 0x%x 0x%x\n",
				 isp_mis_tmp, isp_ris);
	}

	if (!stats_vdev->streamon)
		goto unlock;

	if ((isp_ris & ISP3X_FRAME) && stats_vdev->rd_stats_from_ddr) {
		wr_buf_idx = stats_vdev->wr_buf_idx;
		stats_vdev->rd_buf_idx = wr_buf_idx;
		rkisp_finish_buffer(dev, &stats_vdev->stats_buf[wr_buf_idx]);
		wr_buf_idx = (wr_buf_idx + 1) % RKISP_STATS_DDR_BUF_NUM;
		stats_vdev->wr_buf_idx = wr_buf_idx;
		rkisp_finish_buffer(dev, &stats_vdev->stats_buf[wr_buf_idx]);
		rkisp_write(stats_vdev->dev, ISP3X_MI_DBR_WR_SIZE,
			    ISP3X_RD_STATS_BUF_SIZE, false);
		rkisp_write(stats_vdev->dev, ISP3X_MI_3A_WR_BASE,
			    stats_vdev->stats_buf[wr_buf_idx].dma_addr, false);
		rkisp_set_bits(stats_vdev->dev, ISP3X_SWS_CFG, 0,
			       ISP3X_3A_DDR_WRITE_EN, false);
	}


	if (isp_ris & ISP3X_FRAME) {
		work.readout = RKISP_ISP_READOUT_MEAS;
		work.frame_id = cur_frame_id;
		work.isp_ris = temp_isp_ris | isp_ris;
		work.isp3a_ris = temp_isp3a_ris | iq_3a_mask;
		work.timestamp = ktime_get_ns();

		rkisp_stats_send_meas_v3x(stats_vdev, &work);
	}

unlock:
	spin_unlock(&stats_vdev->irq_lock);
}

static void
rkisp_stats_rdbk_enable_v3x(struct rkisp_isp_stats_vdev *stats_vdev, bool en)
{
	if (!en) {
		stats_vdev->isp_rdbk = 0;
		stats_vdev->isp3a_rdbk = 0;
	}

	stats_vdev->rdbk_mode = en;
}

static struct rkisp_isp_stats_ops rkisp_isp_stats_ops_tbl = {
	.isr_hdl = rkisp_stats_isr_v3x,
	.send_meas = rkisp_stats_send_meas_v3x,
	.rdbk_enable = rkisp_stats_rdbk_enable_v3x,
};

void rkisp_stats_first_ddr_config_v3x(struct rkisp_isp_stats_vdev *stats_vdev)
{
	stats_vdev->rd_stats_from_ddr = false;
	stats_vdev->priv_ops = &stats_reg_ops_v3x;

	if (!IS_HDR_RDBK(stats_vdev->dev->hdr.op_mode) &&
	    stats_vdev->stats_buf[0].mem_priv) {
		stats_vdev->priv_ops = &stats_ddr_ops_v3x;
		stats_vdev->rd_stats_from_ddr = true;
		stats_vdev->rd_buf_idx = 0;
		stats_vdev->wr_buf_idx = 0;

		rkisp_write(stats_vdev->dev, ISP3X_MI_DBR_WR_SIZE,
			    ISP3X_RD_STATS_BUF_SIZE, false);
		rkisp_write(stats_vdev->dev, ISP3X_MI_3A_WR_BASE,
			    stats_vdev->stats_buf[0].dma_addr, false);
		rkisp_set_bits(stats_vdev->dev, ISP3X_SWS_CFG, SW_3A_DDR_WRITE_EN,
			       ISP3X_3A_DDR_WRITE_EN, false);
	}
}

void rkisp_init_stats_vdev_v3x(struct rkisp_isp_stats_vdev *stats_vdev)
{
	int i;

	stats_vdev->vdev_fmt.fmt.meta.dataformat =
		V4L2_META_FMT_RK_ISP1_STAT_3A;
	stats_vdev->vdev_fmt.fmt.meta.buffersize =
		sizeof(struct rkisp3x_isp_stat_buffer);

	stats_vdev->ops = &rkisp_isp_stats_ops_tbl;
	stats_vdev->priv_ops = &stats_reg_ops_v3x;
	stats_vdev->rd_stats_from_ddr = false;

	for (i = 0; i < RKISP_STATS_DDR_BUF_NUM; i++) {
		stats_vdev->stats_buf[i].is_need_vaddr = true;
		stats_vdev->stats_buf[i].size = ISP3X_RD_STATS_BUF_SIZE;
		rkisp_alloc_buffer(stats_vdev->dev, &stats_vdev->stats_buf[i]);
	}
}

void rkisp_uninit_stats_vdev_v3x(struct rkisp_isp_stats_vdev *stats_vdev)
{
	int i;

	for (i = 0; i < RKISP_STATS_DDR_BUF_NUM; i++)
		rkisp_free_buffer(stats_vdev->dev, &stats_vdev->stats_buf[i]);
}
