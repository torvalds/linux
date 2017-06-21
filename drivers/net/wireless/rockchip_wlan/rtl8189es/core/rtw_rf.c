/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *                                        
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTW_RF_C_

#include <drv_types.h>
#include <hal_data.h>

int rtw_ch2freq(int chan)
{
	/* see 802.11 17.3.8.3.2 and Annex J
	* there are overlapping channel numbers in 5GHz and 2GHz bands */

	/*
	* RTK: don't consider the overlapping channel numbers: 5G channel <= 14,
	* because we don't support it. simply judge from channel number
	*/

	if (chan >= 1 && chan <= 14) {
		if (chan == 14)
			return 2484;
		else if (chan < 14)
			return 2407 + chan * 5;
	} else if (chan >= 36 && chan <= 177) {
		return 5000 + chan * 5;
	}

	return 0; /* not supported */
}

int rtw_freq2ch(int freq)
{
	/* see 802.11 17.3.8.3.2 and Annex J */
	if (freq == 2484)
		return 14;
	else if (freq < 2484)
		return (freq - 2407) / 5;
	else if (freq >= 4910 && freq <= 4980)
		return (freq - 4000) / 5;
	else if (freq <= 45000) /* DMG band lower limit */
		return (freq - 5000) / 5;
	else if (freq >= 58320 && freq <= 64800)
		return (freq - 56160) / 2160;
	else
		return 0;
}

bool rtw_chbw_to_freq_range(u8 ch, u8 bw, u8 offset, u32 *hi, u32 *lo)
{
	u8 c_ch;
	u32 freq;
	u32 hi_ret = 0, lo_ret = 0;
	int i;
	bool valid = _FALSE;

	if (hi)
		*hi = 0;
	if (lo)
		*lo = 0;

	c_ch = rtw_get_center_ch(ch, bw, offset);
	freq = rtw_ch2freq(c_ch);

	if (!freq) {
		rtw_warn_on(1);
		goto exit;
	}

	if (bw == CHANNEL_WIDTH_80) {
		hi_ret = freq + 40;
		lo_ret = freq - 40;
	} else if (bw == CHANNEL_WIDTH_40) {
		hi_ret = freq + 20;
		lo_ret = freq - 20;
	} else if (bw == CHANNEL_WIDTH_20) {
		hi_ret = freq + 10;
		lo_ret = freq - 10;
	} else {
		rtw_warn_on(1);
	}

	if (hi)
		*hi = hi_ret;
	if (lo)
		*lo = lo_ret;

	valid = _TRUE;

exit:
	return valid;
}

int rtw_ch_to_bb_gain_sel(int ch)
{
	int sel = -1;

	if (ch >= 1 && ch <= 14)
		sel = BB_GAIN_2G;
#ifdef CONFIG_IEEE80211_BAND_5GHZ
	else if (ch >= 36 && ch < 50)
		sel = BB_GAIN_5GLB1;
	else if (ch >= 50 && ch <= 64)
		sel = BB_GAIN_5GLB2;
	else if (ch >= 100 && ch <= 118)
		sel = BB_GAIN_5GMB1;
	else if (ch >= 120 && ch <= 140)
		sel = BB_GAIN_5GMB2;
	else if (ch >= 149 && ch <= 165)
		sel = BB_GAIN_5GHB;
#endif

	return sel;
}

s8 rtw_rf_get_kfree_tx_gain_offset(_adapter *padapter, u8 path, u8 ch)
{
	s8 kfree_offset = 0;

#ifdef CONFIG_RF_GAIN_OFFSET
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(padapter);
	struct kfree_data_t *kfree_data = GET_KFREE_DATA(padapter);
	s8 bb_gain_sel = rtw_ch_to_bb_gain_sel(ch);

	if (bb_gain_sel < BB_GAIN_2G || bb_gain_sel >= BB_GAIN_NUM) {
		rtw_warn_on(1);
		goto exit;
	}

	if (kfree_data->flag & KFREE_FLAG_ON) {
		kfree_offset = kfree_data->bb_gain[bb_gain_sel][path];
		if (1)
			DBG_871X("%s path:%u, ch:%u, bb_gain_sel:%d, kfree_offset:%d\n"
				, __func__, path, ch, bb_gain_sel, kfree_offset);
	}
exit:
#endif /* CONFIG_RF_GAIN_OFFSET */

	return kfree_offset;
}

void rtw_rf_set_tx_gain_offset(_adapter *adapter, u8 path, s8 offset)
{
	u8 write_value;

	switch (rtw_get_chip_type(adapter)) {
#ifdef CONFIG_RTL8188F
	case RTL8188F:
		write_value = RF_TX_GAIN_OFFSET_8188F(offset);
		rtw_hal_write_rfreg(adapter, path, 0x55, 0x0fc000, write_value);
		break;
#endif /* CONFIG_RTL8188F */
#ifdef CONFIG_RTL8821A
	case RTL8821:
		write_value = RF_TX_GAIN_OFFSET_8821A(offset);
		rtw_hal_write_rfreg(adapter, path, 0x55, 0x0f8000, write_value);
		break;
#endif /* CONFIG_RTL8821A */
	default:
		rtw_warn_on(1);
		break;
	}
}

void rtw_rf_apply_tx_gain_offset(_adapter *adapter, u8 ch)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	s8 kfree_offset = 0;
	s8 tx_pwr_track_offset = 0; /* TODO: 8814A should consider tx pwr track when setting tx gain offset */
	s8 total_offset;
	int i;

	for (i = 0; i < hal_data->NumTotalRFPath; i++) {
		kfree_offset = rtw_rf_get_kfree_tx_gain_offset(adapter, i, ch);
		total_offset = kfree_offset + tx_pwr_track_offset;
		rtw_rf_set_tx_gain_offset(adapter, i, total_offset);
	}
}

bool rtw_is_dfs_range(u32 hi, u32 lo)
{
	return rtw_is_range_overlap(hi, lo, 5720 + 10, 5260 - 10)?_TRUE:_FALSE;
}

bool rtw_is_dfs_ch(u8 ch, u8 bw, u8 offset)
{
	u32 hi, lo;

	if (rtw_chbw_to_freq_range(ch, bw, offset, &hi, &lo) == _FALSE)
		return _FALSE;

	return rtw_is_dfs_range(hi, lo)?_TRUE:_FALSE;
}

bool rtw_is_long_cac_range(u32 hi, u32 lo)
{
	return rtw_is_range_overlap(hi, lo, 5660 + 10, 5600 - 10)?_TRUE:_FALSE;
}

bool rtw_is_long_cac_ch(u8 ch, u8 bw, u8 offset)
{
	u32 hi, lo;

	if (rtw_chbw_to_freq_range(ch, bw, offset, &hi, &lo) == _FALSE)
		return _FALSE;

	return rtw_is_long_cac_range(hi, lo)?_TRUE:_FALSE;
}

