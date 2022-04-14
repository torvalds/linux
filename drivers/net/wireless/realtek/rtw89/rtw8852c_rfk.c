// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2022  Realtek Corporation
 */

#include "debug.h"
#include "phy.h"
#include "reg.h"
#include "rtw8852c.h"
#include "rtw8852c_rfk.h"

static u8 _kpath(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK]dbcc_en: %x,  PHY%d\n",
		    rtwdev->dbcc_en, phy_idx);

	if (!rtwdev->dbcc_en)
		return RF_AB;

	if (phy_idx == RTW89_PHY_0)
		return RF_A;
	else
		return RF_B;
}

static void _bw_setting(struct rtw89_dev *rtwdev, enum rtw89_rf_path path,
			enum rtw89_bandwidth bw, bool is_dav)
{
	u32 rf_reg18;
	u32 reg_reg18_addr;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK]===>%s\n", __func__);
	if (is_dav)
		reg_reg18_addr = RR_CFGCH;
	else
		reg_reg18_addr = RR_CFGCH_V1;

	rf_reg18 = rtw89_read_rf(rtwdev, path, reg_reg18_addr, RFREG_MASK);
	rf_reg18 &= ~RR_CFGCH_BW;

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_5:
	case RTW89_CHANNEL_WIDTH_10:
	case RTW89_CHANNEL_WIDTH_20:
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BW, CFGCH_BW_20M);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW0 | (path << 8), B_P0_CFCH_BW0, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW1 | (path << 8), B_P0_CFCH_BW1, 0xf);
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BW, CFGCH_BW_40M);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW0 | (path << 8), B_P0_CFCH_BW0, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW1 | (path << 8), B_P0_CFCH_BW1, 0xf);
		break;
	case RTW89_CHANNEL_WIDTH_80:
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BW, CFGCH_BW_80M);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW0 | (path << 8), B_P0_CFCH_BW0, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW1 | (path << 8), B_P0_CFCH_BW1, 0xd);
		break;
	case RTW89_CHANNEL_WIDTH_160:
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BW, CFGCH_BW_160M);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW0 | (path << 8), B_P0_CFCH_BW0, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW1 | (path << 8), B_P0_CFCH_BW1, 0xb);
		break;
	default:
		break;
	}

	rtw89_write_rf(rtwdev, path, reg_reg18_addr, RFREG_MASK, rf_reg18);
}

static void _ctrl_bw(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
		     enum rtw89_bandwidth bw)
{
	bool is_dav;
	u8 kpath, path;
	u32 tmp = 0;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK]===>%s\n", __func__);
	kpath = _kpath(rtwdev, phy);

	for (path = 0; path < 2; path++) {
		if (!(kpath & BIT(path)))
			continue;

		is_dav = true;
		_bw_setting(rtwdev, path, bw, is_dav);
		is_dav = false;
		_bw_setting(rtwdev, path, bw, is_dav);
		if (rtwdev->dbcc_en)
			continue;

		if (path == RF_PATH_B && rtwdev->hal.cv == CHIP_CAV) {
			rtw89_write_rf(rtwdev, RF_PATH_B, RR_RSV1, RR_RSV1_RST, 0x0);
			tmp = rtw89_read_rf(rtwdev, RF_PATH_A, RR_CFGCH, RFREG_MASK);
			rtw89_write_rf(rtwdev, RF_PATH_B, RR_APK, RR_APK_MOD, 0x3);
			rtw89_write_rf(rtwdev, RF_PATH_B, RR_CFGCH, RFREG_MASK, tmp);
			fsleep(100);
			rtw89_write_rf(rtwdev, RF_PATH_B, RR_RSV1, RR_RSV1_RST, 0x1);
		}
	}
}

static void _ch_setting(struct rtw89_dev *rtwdev, enum rtw89_rf_path path,
			u8 central_ch, enum rtw89_band band, bool is_dav)
{
	u32 rf_reg18;
	u32 reg_reg18_addr;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK]===>%s\n", __func__);
	if (is_dav)
		reg_reg18_addr = 0x18;
	else
		reg_reg18_addr = 0x10018;

	rf_reg18 = rtw89_read_rf(rtwdev, path, reg_reg18_addr, RFREG_MASK);
	rf_reg18 &= ~(RR_CFGCH_BAND1 | RR_CFGCH_BAND0 | RR_CFGCH_CH);
	rf_reg18 |= FIELD_PREP(RR_CFGCH_CH, central_ch);

	switch (band) {
	case RTW89_BAND_2G:
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BAND1, CFGCH_BAND1_2G);
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BAND0, CFGCH_BAND0_2G);
		break;
	case RTW89_BAND_5G:
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BAND1, CFGCH_BAND1_5G);
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BAND0, CFGCH_BAND0_5G);
		break;
	case RTW89_BAND_6G:
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BAND1, CFGCH_BAND1_6G);
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BAND0, CFGCH_BAND0_6G);
		break;
	default:
		break;
	}
	rtw89_write_rf(rtwdev, path, reg_reg18_addr, RFREG_MASK, rf_reg18);
	fsleep(100);
}

static void _ctrl_ch(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
		     u8 central_ch, enum rtw89_band band)
{
	u8 kpath, path;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK]===>%s\n", __func__);
	if (band != RTW89_BAND_6G) {
		if ((central_ch > 14 && central_ch < 36) ||
		    (central_ch > 64 && central_ch < 100) ||
		    (central_ch > 144 && central_ch < 149) || central_ch > 177)
			return;
	} else {
		if (central_ch > 253 || central_ch  == 2)
			return;
	}

	kpath = _kpath(rtwdev, phy);

	for (path = 0; path < 2; path++) {
		if (kpath & BIT(path)) {
			_ch_setting(rtwdev, path, central_ch, band, true);
			_ch_setting(rtwdev, path, central_ch, band, false);
		}
	}
}

static void _rxbb_bw(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
		     enum rtw89_bandwidth bw)
{
	u8 kpath;
	u8 path;
	u32 val;

	kpath = _kpath(rtwdev, phy);
	for (path = 0; path < 2; path++) {
		if (!(kpath & BIT(path)))
			continue;

		rtw89_write_rf(rtwdev, path, RR_LUTWE2, RR_LUTWE2_RTXBW, 0x1);
		rtw89_write_rf(rtwdev, path, RR_LUTWA, RR_LUTWA_M2, 0xa);
		switch (bw) {
		case RTW89_CHANNEL_WIDTH_20:
			val = 0x1b;
			break;
		case RTW89_CHANNEL_WIDTH_40:
			val = 0x13;
			break;
		case RTW89_CHANNEL_WIDTH_80:
			val = 0xb;
			break;
		case RTW89_CHANNEL_WIDTH_160:
		default:
			val = 0x3;
			break;
		}
		rtw89_write_rf(rtwdev, path, RR_LUTWD0, RR_LUTWD0_LB, val);
		rtw89_write_rf(rtwdev, path, RR_LUTWE2, RR_LUTWE2_RTXBW, 0x0);
	}
}

static
void rtw8852c_ctrl_bw_ch(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			 u8 central_ch, enum rtw89_band band,
			 enum rtw89_bandwidth bw)
{
	_ctrl_ch(rtwdev, phy, central_ch, band);
	_ctrl_bw(rtwdev, phy, bw);
	_rxbb_bw(rtwdev, phy, bw);
}

void rtw8852c_set_channel_rf(struct rtw89_dev *rtwdev,
			     struct rtw89_channel_params *param,
			     enum rtw89_phy_idx phy_idx)
{
	rtw8852c_ctrl_bw_ch(rtwdev, phy_idx, param->center_chan, param->band_type,
			    param->bandwidth);
}
