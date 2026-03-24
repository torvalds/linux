// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2026  Realtek Corporation
 */

#include "phy.h"
#include "reg.h"
#include "rtw8922d.h"
#include "rtw8922d_rfk.h"

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
