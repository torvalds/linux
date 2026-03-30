// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2026  Realtek Corporation
 */

#include "chan.h"
#include "debug.h"
#include "phy.h"
#include "reg.h"
#include "rtw8922d.h"
#include "rtw8922d_rfk.h"

static void rtw8922d_tssi_cont_en(struct rtw89_dev *rtwdev, bool en,
				  enum rtw89_rf_path path, u8 phy_idx)
{
	static const u32 tssi_trk_man[2] = {R_TSSI_EN_P0_BE4,
					    R_TSSI_EN_P0_BE4 + 0x100};

	if (en)
		rtw89_phy_write32_idx(rtwdev, tssi_trk_man[path],
				      B_TSSI_CONT_EN, 0, phy_idx);
	else
		rtw89_phy_write32_idx(rtwdev, tssi_trk_man[path],
				      B_TSSI_CONT_EN, 1, phy_idx);
}

void rtw8922d_tssi_cont_en_phyidx(struct rtw89_dev *rtwdev, bool en, u8 phy_idx)
{
	if (rtwdev->mlo_dbcc_mode == MLO_1_PLUS_1_1RF) {
		if (phy_idx == RTW89_PHY_0)
			rtw8922d_tssi_cont_en(rtwdev, en, RF_PATH_A, phy_idx);
		else
			rtw8922d_tssi_cont_en(rtwdev, en, RF_PATH_B, phy_idx);
	} else {
		rtw8922d_tssi_cont_en(rtwdev, en, RF_PATH_A, phy_idx);
		rtw8922d_tssi_cont_en(rtwdev, en, RF_PATH_B, phy_idx);
	}
}

static
void rtw8922d_ctl_band_ch_bw(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			     const struct rtw89_chan *chan)
{
	u8 synpath;
	u32 rf18;

	synpath = rtw89_phy_get_syn_sel(rtwdev, phy);
	rf18 = rtw89_chip_chan_to_rf18_val(rtwdev, chan);

	rtw89_write_rf(rtwdev, synpath, RR_RSV1, RFREG_MASK, 0x0);
	rtw89_write_rf(rtwdev, synpath, RR_MOD, RFREG_MASK, 0x30000);
	rtw89_write_rf(rtwdev, synpath, RR_CFGCH, RFREG_MASK, rf18);
	fsleep(400);
	rtw89_write_rf(rtwdev, synpath, RR_RSV1, RFREG_MASK, 0x1);
	rtw89_write_rf(rtwdev, synpath, RR_CFGCH_V1, RFREG_MASK, rf18);
}

void rtw8922d_set_channel_rf(struct rtw89_dev *rtwdev,
			     const struct rtw89_chan *chan,
			     enum rtw89_phy_idx phy_idx)
{
	rtw8922d_ctl_band_ch_bw(rtwdev, phy_idx, chan);
}

enum _rf_syn_pow {
	RF_SYN_ON_OFF,
	RF_SYN_OFF_ON,
	RF_SYN_ALLON,
	RF_SYN_ALLOFF,
};

static void rtw8922d_set_syn01(struct rtw89_dev *rtwdev, enum _rf_syn_pow syn)
{
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "SYN config=%d\n", syn);

	if (syn == RF_SYN_ALLON) {
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_MOD, BIT(1), 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_MOD, BIT(1), 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_RSV1, MASKDWORD, 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_RSV1, MASKDWORD, 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RR_POW_SYN_V1, 0xf);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_POW, RR_POW_SYN_V1, 0xf);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_RSV1, MASKDWORD, 0x1);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_RSV1, MASKDWORD, 0x1);
	} else if (syn == RF_SYN_ON_OFF) {
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_MOD, BIT(1), 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_RSV1, MASKDWORD, 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RR_POW_SYN_V1, 0xf);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_POW, RR_POW_SYN_V1, 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_RSV1, MASKDWORD, 0x1);
	} else if (syn == RF_SYN_OFF_ON) {
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_MOD, BIT(1), 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_RSV1, MASKDWORD, 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RR_POW_SYN_V1, 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_POW, RR_POW_SYN_V1, 0xf);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_RSV1, MASKDWORD, 0x1);
	} else if (syn == RF_SYN_ALLOFF) {
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RR_POW_SYN_V1, 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_POW, RR_POW_SYN_V1, 0x0);
	}
}

static void rtw8922d_chlk_ktbl_sel(struct rtw89_dev *rtwdev, u8 kpath, u8 idx)
{
	bool mlo_linking = false;

	if (idx > 2) {
		rtw89_warn(rtwdev, "[DBCC][ERROR]indx is out of limit!! index(%d)", idx);
		return;
	}

	if (mlo_linking) {
		if (kpath & RF_A) {
			rtw89_write_rf(rtwdev, RF_PATH_A, RR_MODOPT, RR_SW_SEL, 0x0);
			rtw89_write_rf(rtwdev, RF_PATH_A, RR_MODOPT_V1, RR_SW_SEL, 0x0);
		}

		if (kpath & RF_B) {
			rtw89_write_rf(rtwdev, RF_PATH_B, RR_MODOPT, RR_SW_SEL, 0x0);
			rtw89_write_rf(rtwdev, RF_PATH_B, RR_MODOPT_V1, RR_SW_SEL, 0x0);
		}

		return;
	}

	if (kpath & RF_A) {
		rtw89_phy_write32_mask(rtwdev, R_KTBL0A_BE4, B_KTBL0_RST, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_KTBL0A_BE4, B_KTBL0_IDX0, idx);
		rtw89_phy_write32_mask(rtwdev, R_KTBL0A_BE4, B_KTBL0_IDX1, idx);

		rtw89_write_rf(rtwdev, RF_PATH_A, RR_MODOPT, RR_TXG_SEL, 0x4 | idx);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_MODOPT_V1, RR_TXG_SEL, 0x4 | idx);

		rtw89_phy_write32_mask(rtwdev, R_KTBL1A_BE4, B_KTBL1_TBL0, idx & BIT(0));
		rtw89_phy_write32_mask(rtwdev, R_KTBL1A_BE4, B_KTBL1_TBL1, (idx & BIT(1)) >> 1);
	}

	if (kpath & RF_B) {
		rtw89_phy_write32_mask(rtwdev, R_KTBL0B_BE4, B_KTBL0_RST, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_KTBL0B_BE4, B_KTBL0_IDX0, idx);
		rtw89_phy_write32_mask(rtwdev, R_KTBL0B_BE4, B_KTBL0_IDX1, idx);

		rtw89_write_rf(rtwdev, RF_PATH_B, RR_MODOPT, RR_TXG_SEL, 0x4 | idx);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_MODOPT_V1, RR_TXG_SEL, 0x4 | idx);

		rtw89_phy_write32_mask(rtwdev, R_KTBL1B_BE4, B_KTBL1_TBL0, idx & BIT(0));
		rtw89_phy_write32_mask(rtwdev, R_KTBL1B_BE4, B_KTBL1_TBL1, (idx & BIT(1)) >> 1);
	}
}

static u8 rtw8922d_chlk_reload_sel_tbl(struct rtw89_dev *rtwdev,
				       const struct rtw89_chan *chan, u8 path)
{
	struct rtw89_rfk_mcc_info_data *rfk_mcc = rtwdev->rfk_mcc.data;
	struct rtw89_rfk_chan_desc desc[__RTW89_RFK_CHS_NR_V1] = {};
	u8 tbl_sel;

	for (tbl_sel = 0; tbl_sel < ARRAY_SIZE(desc); tbl_sel++) {
		struct rtw89_rfk_chan_desc *p = &desc[tbl_sel];

		p->ch = rfk_mcc->ch[tbl_sel];

		p->has_band = true;
		p->band = rfk_mcc->band[tbl_sel];

		p->has_bw = true;
		p->bw = rfk_mcc->bw[tbl_sel];
	}

	tbl_sel = rtw89_rfk_chan_lookup(rtwdev, desc, ARRAY_SIZE(desc), chan);

	rfk_mcc->ch[tbl_sel] = chan->channel;
	rfk_mcc->band[tbl_sel] = chan->band_type;
	rfk_mcc->bw[tbl_sel] = chan->band_width;
	rfk_mcc->rf18[tbl_sel] = rtw89_chip_chan_to_rf18_val(rtwdev, chan);

	/* shared table array, but tbl_sel can be independent by path */
	rfk_mcc[path].table_idx = tbl_sel;

	return tbl_sel;
}

static void rtw8922d_chlk_reload(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chan *chan0, *chan1;
	u8 s0_tbl, s1_tbl;

	switch (rtwdev->mlo_dbcc_mode) {
	default:
	case MLO_2_PLUS_0_1RF:
		chan0 = rtw89_mgnt_chan_get(rtwdev, 0);
		chan1 = chan0;
		break;
	case MLO_0_PLUS_2_1RF:
		chan1 = rtw89_mgnt_chan_get(rtwdev, 1);
		chan0 = chan1;
		break;
	case MLO_1_PLUS_1_1RF:
		chan0 = rtw89_mgnt_chan_get(rtwdev, 0);
		chan1 = rtw89_mgnt_chan_get(rtwdev, 1);
		break;
	}

	s0_tbl = rtw8922d_chlk_reload_sel_tbl(rtwdev, chan0, 0);
	s1_tbl = rtw8922d_chlk_reload_sel_tbl(rtwdev, chan1, 1);

	rtw8922d_chlk_ktbl_sel(rtwdev, RF_A, s0_tbl);
	rtw8922d_chlk_ktbl_sel(rtwdev, RF_B, s1_tbl);
}

static enum _rf_syn_pow rtw8922d_get_syn_pow(struct rtw89_dev *rtwdev)
{
	switch (rtwdev->mlo_dbcc_mode) {
	case MLO_0_PLUS_2_1RF:
		return RF_SYN_OFF_ON;
	case MLO_0_PLUS_2_2RF:
	case MLO_1_PLUS_1_2RF:
	case MLO_2_PLUS_0_1RF:
	case MLO_2_PLUS_0_2RF:
	case MLO_2_PLUS_2_2RF:
	case MLO_DBCC_NOT_SUPPORT:
	default:
		return RF_SYN_ON_OFF;
	case MLO_1_PLUS_1_1RF:
	case DBCC_LEGACY:
		return RF_SYN_ALLON;
	}
}

void rtw8922d_rfk_mlo_ctrl(struct rtw89_dev *rtwdev)
{
	enum _rf_syn_pow syn_pow = rtw8922d_get_syn_pow(rtwdev);

	if (!rtwdev->dbcc_en)
		goto set_rfk_reload;

	rtw8922d_set_syn01(rtwdev, syn_pow);

set_rfk_reload:
	rtw8922d_chlk_reload(rtwdev);
}

void rtw8922d_pre_set_channel_rf(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	bool mlo_1_1;

	if (!rtwdev->dbcc_en)
		return;

	mlo_1_1 = rtw89_is_mlo_1_1(rtwdev);
	if (mlo_1_1)
		rtw8922d_set_syn01(rtwdev, RF_SYN_ALLON);
	else if (phy_idx == RTW89_PHY_0)
		rtw8922d_set_syn01(rtwdev, RF_SYN_ON_OFF);
	else
		rtw8922d_set_syn01(rtwdev, RF_SYN_OFF_ON);

	fsleep(1000);
}

void rtw8922d_post_set_channel_rf(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	rtw8922d_rfk_mlo_ctrl(rtwdev);
}

static u8 _get_thermal(struct rtw89_dev *rtwdev, enum rtw89_rf_path path)
{
	rtw89_write_rf(rtwdev, path, RR_TM, RR_TM_TRI, 0x1);
	rtw89_write_rf(rtwdev, path, RR_TM, RR_TM_TRI, 0x0);
	rtw89_write_rf(rtwdev, path, RR_TM, RR_TM_TRI, 0x1);

	fsleep(200);

	return rtw89_read_rf(rtwdev, path, RR_TM, RR_TM_VAL_V1);
}

static void _lck_keep_thermal(struct rtw89_dev *rtwdev)
{
	struct rtw89_lck_info *lck = &rtwdev->lck;
	int path;

	for (path = 0; path < rtwdev->chip->rf_path_num; path++) {
		lck->thermal[path] = _get_thermal(rtwdev, path);
		rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
			    "[LCK] path=%d thermal=0x%x", path, lck->thermal[path]);
	}
}

static void _lck(struct rtw89_dev *rtwdev)
{
	enum _rf_syn_pow syn_pow = rtw8922d_get_syn_pow(rtwdev);
	u8 path_mask = 0;
	u32 tmp18, tmp5;
	int path;

	rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK, "[LCK] DO LCK\n");

	if (syn_pow == RF_SYN_ALLON)
		path_mask = BIT(RF_PATH_A) | BIT(RF_PATH_B);
	else if (syn_pow == RF_SYN_ON_OFF)
		path_mask = BIT(RF_PATH_A);
	else if (syn_pow == RF_SYN_OFF_ON)
		path_mask = BIT(RF_PATH_B);
	else
		return;

	for (path = 0; path < rtwdev->chip->rf_path_num; path++) {
		if (!(path_mask & BIT(path)))
			continue;

		tmp18 = rtw89_read_rf(rtwdev, path, RR_CFGCH, MASKDWORD);
		tmp5 = rtw89_read_rf(rtwdev, path, RR_RSV1, MASKDWORD);

		rtw89_write_rf(rtwdev, path, RR_MOD, MASKDWORD, 0x10000);
		rtw89_write_rf(rtwdev, path, RR_RSV1, MASKDWORD, 0x0);
		rtw89_write_rf(rtwdev, path, RR_LCK_TRG, RR_LCK_TRGSEL, 0x1);
		rtw89_write_rf(rtwdev, path, RR_CFGCH, MASKDWORD, tmp18);
		rtw89_write_rf(rtwdev, path, RR_LCK_TRG, RR_LCK_TRGSEL, 0x0);

		fsleep(400);

		rtw89_write_rf(rtwdev, path, RR_RSV1, MASKDWORD, tmp5);
	}

	_lck_keep_thermal(rtwdev);
}

#define RTW8922D_LCK_TH 16
void rtw8922d_lck_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_lck_info *lck = &rtwdev->lck;
	u8 cur_thermal;
	int delta;
	int path;

	for (path = 0; path < rtwdev->chip->rf_path_num; path++) {
		cur_thermal = _get_thermal(rtwdev, path);
		delta = abs((int)cur_thermal - lck->thermal[path]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
			    "[LCK] path=%d current thermal=0x%x delta=0x%x\n",
			    path, cur_thermal, delta);

		if (delta >= RTW8922D_LCK_TH) {
			_lck(rtwdev);
			return;
		}
	}
}
