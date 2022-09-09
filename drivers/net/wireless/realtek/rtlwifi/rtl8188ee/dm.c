// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2013  Realtek Corporation.*/

#include "../wifi.h"
#include "../base.h"
#include "../pci.h"
#include "../core.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "dm.h"
#include "fw.h"
#include "trx.h"

static const u32 ofdmswing_table[OFDM_TABLE_SIZE] = {
	0x7f8001fe,		/* 0, +6.0dB */
	0x788001e2,		/* 1, +5.5dB */
	0x71c001c7,		/* 2, +5.0dB */
	0x6b8001ae,		/* 3, +4.5dB */
	0x65400195,		/* 4, +4.0dB */
	0x5fc0017f,		/* 5, +3.5dB */
	0x5a400169,		/* 6, +3.0dB */
	0x55400155,		/* 7, +2.5dB */
	0x50800142,		/* 8, +2.0dB */
	0x4c000130,		/* 9, +1.5dB */
	0x47c0011f,		/* 10, +1.0dB */
	0x43c0010f,		/* 11, +0.5dB */
	0x40000100,		/* 12, +0dB */
	0x3c8000f2,		/* 13, -0.5dB */
	0x390000e4,		/* 14, -1.0dB */
	0x35c000d7,		/* 15, -1.5dB */
	0x32c000cb,		/* 16, -2.0dB */
	0x300000c0,		/* 17, -2.5dB */
	0x2d4000b5,		/* 18, -3.0dB */
	0x2ac000ab,		/* 19, -3.5dB */
	0x288000a2,		/* 20, -4.0dB */
	0x26000098,		/* 21, -4.5dB */
	0x24000090,		/* 22, -5.0dB */
	0x22000088,		/* 23, -5.5dB */
	0x20000080,		/* 24, -6.0dB */
	0x1e400079,		/* 25, -6.5dB */
	0x1c800072,		/* 26, -7.0dB */
	0x1b00006c,		/* 27. -7.5dB */
	0x19800066,		/* 28, -8.0dB */
	0x18000060,		/* 29, -8.5dB */
	0x16c0005b,		/* 30, -9.0dB */
	0x15800056,		/* 31, -9.5dB */
	0x14400051,		/* 32, -10.0dB */
	0x1300004c,		/* 33, -10.5dB */
	0x12000048,		/* 34, -11.0dB */
	0x11000044,		/* 35, -11.5dB */
	0x10000040,		/* 36, -12.0dB */
	0x0f00003c,		/* 37, -12.5dB */
	0x0e400039,		/* 38, -13.0dB */
	0x0d800036,		/* 39, -13.5dB */
	0x0cc00033,		/* 40, -14.0dB */
	0x0c000030,		/* 41, -14.5dB */
	0x0b40002d,		/* 42, -15.0dB */
};

static const u8 cck_tbl_ch1_13[CCK_TABLE_SIZE][8] = {
	{0x36, 0x35, 0x2e, 0x25, 0x1c, 0x12, 0x09, 0x04},	/* 0, +0dB */
	{0x33, 0x32, 0x2b, 0x23, 0x1a, 0x11, 0x08, 0x04},	/* 1, -0.5dB */
	{0x30, 0x2f, 0x29, 0x21, 0x19, 0x10, 0x08, 0x03},	/* 2, -1.0dB */
	{0x2d, 0x2d, 0x27, 0x1f, 0x18, 0x0f, 0x08, 0x03},	/* 3, -1.5dB */
	{0x2b, 0x2a, 0x25, 0x1e, 0x16, 0x0e, 0x07, 0x03},	/* 4, -2.0dB */
	{0x28, 0x28, 0x22, 0x1c, 0x15, 0x0d, 0x07, 0x03},	/* 5, -2.5dB */
	{0x26, 0x25, 0x21, 0x1b, 0x14, 0x0d, 0x06, 0x03},	/* 6, -3.0dB */
	{0x24, 0x23, 0x1f, 0x19, 0x13, 0x0c, 0x06, 0x03},	/* 7, -3.5dB */
	{0x22, 0x21, 0x1d, 0x18, 0x11, 0x0b, 0x06, 0x02},	/* 8, -4.0dB */
	{0x20, 0x20, 0x1b, 0x16, 0x11, 0x08, 0x05, 0x02},	/* 9, -4.5dB */
	{0x1f, 0x1e, 0x1a, 0x15, 0x10, 0x0a, 0x05, 0x02},	/* 10, -5.0dB */
	{0x1d, 0x1c, 0x18, 0x14, 0x0f, 0x0a, 0x05, 0x02},	/* 11, -5.5dB */
	{0x1b, 0x1a, 0x17, 0x13, 0x0e, 0x09, 0x04, 0x02},	/* 12, -6.0dB */
	{0x1a, 0x19, 0x16, 0x12, 0x0d, 0x09, 0x04, 0x02},	/* 13, -6.5dB */
	{0x18, 0x17, 0x15, 0x11, 0x0c, 0x08, 0x04, 0x02},	/* 14, -7.0dB */
	{0x17, 0x16, 0x13, 0x10, 0x0c, 0x08, 0x04, 0x02},	/* 15, -7.5dB */
	{0x16, 0x15, 0x12, 0x0f, 0x0b, 0x07, 0x04, 0x01},	/* 16, -8.0dB */
	{0x14, 0x14, 0x11, 0x0e, 0x0b, 0x07, 0x03, 0x02},	/* 17, -8.5dB */
	{0x13, 0x13, 0x10, 0x0d, 0x0a, 0x06, 0x03, 0x01},	/* 18, -9.0dB */
	{0x12, 0x12, 0x0f, 0x0c, 0x09, 0x06, 0x03, 0x01},	/* 19, -9.5dB */
	{0x11, 0x11, 0x0f, 0x0c, 0x09, 0x06, 0x03, 0x01},	/* 20, -10.0dB*/
	{0x10, 0x10, 0x0e, 0x0b, 0x08, 0x05, 0x03, 0x01},	/* 21, -10.5dB*/
	{0x0f, 0x0f, 0x0d, 0x0b, 0x08, 0x05, 0x03, 0x01},	/* 22, -11.0dB*/
	{0x0e, 0x0e, 0x0c, 0x0a, 0x08, 0x05, 0x02, 0x01},	/* 23, -11.5dB*/
	{0x0d, 0x0d, 0x0c, 0x0a, 0x07, 0x05, 0x02, 0x01},	/* 24, -12.0dB*/
	{0x0d, 0x0c, 0x0b, 0x09, 0x07, 0x04, 0x02, 0x01},	/* 25, -12.5dB*/
	{0x0c, 0x0c, 0x0a, 0x09, 0x06, 0x04, 0x02, 0x01},	/* 26, -13.0dB*/
	{0x0b, 0x0b, 0x0a, 0x08, 0x06, 0x04, 0x02, 0x01},	/* 27, -13.5dB*/
	{0x0b, 0x0a, 0x09, 0x08, 0x06, 0x04, 0x02, 0x01},	/* 28, -14.0dB*/
	{0x0a, 0x0a, 0x09, 0x07, 0x05, 0x03, 0x02, 0x01},	/* 29, -14.5dB*/
	{0x0a, 0x09, 0x08, 0x07, 0x05, 0x03, 0x02, 0x01},	/* 30, -15.0dB*/
	{0x09, 0x09, 0x08, 0x06, 0x05, 0x03, 0x01, 0x01},	/* 31, -15.5dB*/
	{0x09, 0x08, 0x07, 0x06, 0x04, 0x03, 0x01, 0x01}	/* 32, -16.0dB*/
};

static const u8 cck_tbl_ch14[CCK_TABLE_SIZE][8] = {
	{0x36, 0x35, 0x2e, 0x1b, 0x00, 0x00, 0x00, 0x00},	/* 0, +0dB */
	{0x33, 0x32, 0x2b, 0x19, 0x00, 0x00, 0x00, 0x00},	/* 1, -0.5dB */
	{0x30, 0x2f, 0x29, 0x18, 0x00, 0x00, 0x00, 0x00},	/* 2, -1.0dB */
	{0x2d, 0x2d, 0x17, 0x17, 0x00, 0x00, 0x00, 0x00},	/* 3, -1.5dB */
	{0x2b, 0x2a, 0x25, 0x15, 0x00, 0x00, 0x00, 0x00},	/* 4, -2.0dB */
	{0x28, 0x28, 0x24, 0x14, 0x00, 0x00, 0x00, 0x00},	/* 5, -2.5dB */
	{0x26, 0x25, 0x21, 0x13, 0x00, 0x00, 0x00, 0x00},	/* 6, -3.0dB */
	{0x24, 0x23, 0x1f, 0x12, 0x00, 0x00, 0x00, 0x00},	/* 7, -3.5dB */
	{0x22, 0x21, 0x1d, 0x11, 0x00, 0x00, 0x00, 0x00},	/* 8, -4.0dB */
	{0x20, 0x20, 0x1b, 0x10, 0x00, 0x00, 0x00, 0x00},	/* 9, -4.5dB */
	{0x1f, 0x1e, 0x1a, 0x0f, 0x00, 0x00, 0x00, 0x00},	/* 10, -5.0dB */
	{0x1d, 0x1c, 0x18, 0x0e, 0x00, 0x00, 0x00, 0x00},	/* 11, -5.5dB */
	{0x1b, 0x1a, 0x17, 0x0e, 0x00, 0x00, 0x00, 0x00},	/* 12, -6.0dB */
	{0x1a, 0x19, 0x16, 0x0d, 0x00, 0x00, 0x00, 0x00},	/* 13, -6.5dB */
	{0x18, 0x17, 0x15, 0x0c, 0x00, 0x00, 0x00, 0x00},	/* 14, -7.0dB */
	{0x17, 0x16, 0x13, 0x0b, 0x00, 0x00, 0x00, 0x00},	/* 15, -7.5dB */
	{0x16, 0x15, 0x12, 0x0b, 0x00, 0x00, 0x00, 0x00},	/* 16, -8.0dB */
	{0x14, 0x14, 0x11, 0x0a, 0x00, 0x00, 0x00, 0x00},	/* 17, -8.5dB */
	{0x13, 0x13, 0x10, 0x0a, 0x00, 0x00, 0x00, 0x00},	/* 18, -9.0dB */
	{0x12, 0x12, 0x0f, 0x09, 0x00, 0x00, 0x00, 0x00},	/* 19, -9.5dB */
	{0x11, 0x11, 0x0f, 0x09, 0x00, 0x00, 0x00, 0x00},	/* 20, -10.0dB*/
	{0x10, 0x10, 0x0e, 0x08, 0x00, 0x00, 0x00, 0x00},	/* 21, -10.5dB*/
	{0x0f, 0x0f, 0x0d, 0x08, 0x00, 0x00, 0x00, 0x00},	/* 22, -11.0dB*/
	{0x0e, 0x0e, 0x0c, 0x07, 0x00, 0x00, 0x00, 0x00},	/* 23, -11.5dB*/
	{0x0d, 0x0d, 0x0c, 0x07, 0x00, 0x00, 0x00, 0x00},	/* 24, -12.0dB*/
	{0x0d, 0x0c, 0x0b, 0x06, 0x00, 0x00, 0x00, 0x00},	/* 25, -12.5dB*/
	{0x0c, 0x0c, 0x0a, 0x06, 0x00, 0x00, 0x00, 0x00},	/* 26, -13.0dB*/
	{0x0b, 0x0b, 0x0a, 0x06, 0x00, 0x00, 0x00, 0x00},	/* 27, -13.5dB*/
	{0x0b, 0x0a, 0x09, 0x05, 0x00, 0x00, 0x00, 0x00},	/* 28, -14.0dB*/
	{0x0a, 0x0a, 0x09, 0x05, 0x00, 0x00, 0x00, 0x00},	/* 29, -14.5dB*/
	{0x0a, 0x09, 0x08, 0x05, 0x00, 0x00, 0x00, 0x00},	/* 30, -15.0dB*/
	{0x09, 0x09, 0x08, 0x05, 0x00, 0x00, 0x00, 0x00},	/* 31, -15.5dB*/
	{0x09, 0x08, 0x07, 0x04, 0x00, 0x00, 0x00, 0x00}	/* 32, -16.0dB*/
};

#define	CAL_SWING_OFF(_off, _dir, _size, _del)				\
	do {								\
		for (_off = 0; _off < _size; _off++) {			\
			if (_del < thermal_threshold[_dir][_off]) {	\
				if (_off != 0)				\
					_off--;				\
				break;					\
			}						\
		}							\
		if (_off >= _size)					\
			_off = _size - 1;				\
	} while (0)

static void rtl88e_set_iqk_matrix(struct ieee80211_hw *hw,
				  u8 ofdm_index, u8 rfpath,
				  long iqk_result_x, long iqk_result_y)
{
	long ele_a = 0, ele_d, ele_c = 0, value32;

	ele_d = (ofdmswing_table[ofdm_index] & 0xFFC00000)>>22;

	if (iqk_result_x != 0) {
		if ((iqk_result_x & 0x00000200) != 0)
			iqk_result_x = iqk_result_x | 0xFFFFFC00;
		ele_a = ((iqk_result_x * ele_d)>>8)&0x000003FF;

		if ((iqk_result_y & 0x00000200) != 0)
			iqk_result_y = iqk_result_y | 0xFFFFFC00;
		ele_c = ((iqk_result_y * ele_d)>>8)&0x000003FF;

		switch (rfpath) {
		case RF90_PATH_A:
			value32 = (ele_d << 22)|((ele_c & 0x3F)<<16) | ele_a;
			rtl_set_bbreg(hw, ROFDM0_XATXIQIMBALANCE,
				      MASKDWORD, value32);
			value32 = (ele_c & 0x000003C0) >> 6;
			rtl_set_bbreg(hw, ROFDM0_XCTXAFE, MASKH4BITS,
				      value32);
			value32 = ((iqk_result_x * ele_d) >> 7) & 0x01;
			rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(24),
				      value32);
			break;
		case RF90_PATH_B:
			value32 = (ele_d << 22)|((ele_c & 0x3F)<<16) | ele_a;
			rtl_set_bbreg(hw, ROFDM0_XBTXIQIMBALANCE, MASKDWORD,
				      value32);
			value32 = (ele_c & 0x000003C0) >> 6;
			rtl_set_bbreg(hw, ROFDM0_XDTXAFE, MASKH4BITS, value32);
			value32 = ((iqk_result_x * ele_d) >> 7) & 0x01;
			rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(28),
				      value32);
			break;
		default:
			break;
		}
	} else {
		switch (rfpath) {
		case RF90_PATH_A:
			rtl_set_bbreg(hw, ROFDM0_XATXIQIMBALANCE,
				      MASKDWORD, ofdmswing_table[ofdm_index]);
			rtl_set_bbreg(hw, ROFDM0_XCTXAFE,
				      MASKH4BITS, 0x00);
			rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD,
				      BIT(24), 0x00);
			break;
		case RF90_PATH_B:
			rtl_set_bbreg(hw, ROFDM0_XBTXIQIMBALANCE,
				      MASKDWORD, ofdmswing_table[ofdm_index]);
			rtl_set_bbreg(hw, ROFDM0_XDTXAFE,
				      MASKH4BITS, 0x00);
			rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD,
				      BIT(28), 0x00);
			break;
		default:
			break;
		}
	}
}

void rtl88e_dm_txpower_track_adjust(struct ieee80211_hw *hw,
	u8 type, u8 *pdirection, u32 *poutwrite_val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));
	u8 pwr_val = 0;
	u8 cck_base = rtldm->swing_idx_cck_base;
	u8 cck_val = rtldm->swing_idx_cck;
	u8 ofdm_base = rtldm->swing_idx_ofdm_base[0];
	u8 ofdm_val = rtlpriv->dm.swing_idx_ofdm[RF90_PATH_A];

	if (type == 0) {
		if (ofdm_val <= ofdm_base) {
			*pdirection = 1;
			pwr_val = ofdm_base - ofdm_val;
		} else {
			*pdirection = 2;
			pwr_val = ofdm_base - ofdm_val;
		}
	} else if (type == 1) {
		if (cck_val <= cck_base) {
			*pdirection = 1;
			pwr_val = cck_base - cck_val;
		} else {
			*pdirection = 2;
			pwr_val = cck_val - cck_base;
		}
	}

	if (pwr_val >= TXPWRTRACK_MAX_IDX && (*pdirection == 1))
		pwr_val = TXPWRTRACK_MAX_IDX;

	*poutwrite_val = pwr_val | (pwr_val << 8) | (pwr_val << 16) |
			 (pwr_val << 24);
}

static void dm_tx_pwr_track_set_pwr(struct ieee80211_hw *hw,
				    enum pwr_track_control_method method,
				    u8 rfpath, u8 channel_mapped_index)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));

	if (method == TXAGC) {
		if (rtldm->swing_flag_ofdm ||
		    rtldm->swing_flag_cck) {
			rtl88e_phy_set_txpower_level(hw,
						     rtlphy->current_channel);
			rtldm->swing_flag_ofdm = false;
			rtldm->swing_flag_cck = false;
		}
	} else if (method == BBSWING) {
		if (!rtldm->cck_inch14) {
			rtl_write_byte(rtlpriv, 0xa22,
				       cck_tbl_ch1_13[rtldm->swing_idx_cck][0]);
			rtl_write_byte(rtlpriv, 0xa23,
				       cck_tbl_ch1_13[rtldm->swing_idx_cck][1]);
			rtl_write_byte(rtlpriv, 0xa24,
				       cck_tbl_ch1_13[rtldm->swing_idx_cck][2]);
			rtl_write_byte(rtlpriv, 0xa25,
				       cck_tbl_ch1_13[rtldm->swing_idx_cck][3]);
			rtl_write_byte(rtlpriv, 0xa26,
				       cck_tbl_ch1_13[rtldm->swing_idx_cck][4]);
			rtl_write_byte(rtlpriv, 0xa27,
				       cck_tbl_ch1_13[rtldm->swing_idx_cck][5]);
			rtl_write_byte(rtlpriv, 0xa28,
				       cck_tbl_ch1_13[rtldm->swing_idx_cck][6]);
			rtl_write_byte(rtlpriv, 0xa29,
				       cck_tbl_ch1_13[rtldm->swing_idx_cck][7]);
		} else {
			rtl_write_byte(rtlpriv, 0xa22,
				       cck_tbl_ch14[rtldm->swing_idx_cck][0]);
			rtl_write_byte(rtlpriv, 0xa23,
				       cck_tbl_ch14[rtldm->swing_idx_cck][1]);
			rtl_write_byte(rtlpriv, 0xa24,
				       cck_tbl_ch14[rtldm->swing_idx_cck][2]);
			rtl_write_byte(rtlpriv, 0xa25,
				       cck_tbl_ch14[rtldm->swing_idx_cck][3]);
			rtl_write_byte(rtlpriv, 0xa26,
				       cck_tbl_ch14[rtldm->swing_idx_cck][4]);
			rtl_write_byte(rtlpriv, 0xa27,
				       cck_tbl_ch14[rtldm->swing_idx_cck][5]);
			rtl_write_byte(rtlpriv, 0xa28,
				       cck_tbl_ch14[rtldm->swing_idx_cck][6]);
			rtl_write_byte(rtlpriv, 0xa29,
				       cck_tbl_ch14[rtldm->swing_idx_cck][7]);
		}

		if (rfpath == RF90_PATH_A) {
			rtl88e_set_iqk_matrix(hw, rtldm->swing_idx_ofdm[rfpath],
					      rfpath, rtlphy->iqk_matrix
					      [channel_mapped_index].
					      value[0][0],
					      rtlphy->iqk_matrix
					      [channel_mapped_index].
					      value[0][1]);
		} else if (rfpath == RF90_PATH_B) {
			rtl88e_set_iqk_matrix(hw, rtldm->swing_idx_ofdm[rfpath],
					      rfpath, rtlphy->iqk_matrix
					      [channel_mapped_index].
					      value[0][4],
					      rtlphy->iqk_matrix
					      [channel_mapped_index].
					      value[0][5]);
		}
	} else {
		return;
	}
}

static u8 rtl88e_dm_initial_gain_min_pwdb(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_dig = &rtlpriv->dm_digtable;
	long rssi_val_min = 0;

	if ((dm_dig->curmultista_cstate == DIG_MULTISTA_CONNECT) &&
	    (dm_dig->cur_sta_cstate == DIG_STA_CONNECT)) {
		if (rtlpriv->dm.entry_min_undec_sm_pwdb != 0)
			rssi_val_min =
			    (rtlpriv->dm.entry_min_undec_sm_pwdb >
			     rtlpriv->dm.undec_sm_pwdb) ?
			    rtlpriv->dm.undec_sm_pwdb :
			    rtlpriv->dm.entry_min_undec_sm_pwdb;
		else
			rssi_val_min = rtlpriv->dm.undec_sm_pwdb;
	} else if (dm_dig->cur_sta_cstate == DIG_STA_CONNECT ||
		   dm_dig->cur_sta_cstate == DIG_STA_BEFORE_CONNECT) {
		rssi_val_min = rtlpriv->dm.undec_sm_pwdb;
	} else if (dm_dig->curmultista_cstate ==
		DIG_MULTISTA_CONNECT) {
		rssi_val_min = rtlpriv->dm.entry_min_undec_sm_pwdb;
	}

	return (u8)rssi_val_min;
}

static void rtl88e_dm_false_alarm_counter_statistics(struct ieee80211_hw *hw)
{
	u32 ret_value;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct false_alarm_statistics *falsealm_cnt = &rtlpriv->falsealm_cnt;

	rtl_set_bbreg(hw, ROFDM0_LSTF, BIT(31), 1);
	rtl_set_bbreg(hw, ROFDM1_LSTF, BIT(31), 1);

	ret_value = rtl_get_bbreg(hw, ROFDM0_FRAMESYNC, MASKDWORD);
	falsealm_cnt->cnt_fast_fsync_fail = (ret_value&0xffff);
	falsealm_cnt->cnt_sb_search_fail = ((ret_value&0xffff0000)>>16);

	ret_value = rtl_get_bbreg(hw, ROFDM_PHYCOUNTER1, MASKDWORD);
	falsealm_cnt->cnt_ofdm_cca = (ret_value&0xffff);
	falsealm_cnt->cnt_parity_fail = ((ret_value & 0xffff0000) >> 16);

	ret_value = rtl_get_bbreg(hw, ROFDM_PHYCOUNTER2, MASKDWORD);
	falsealm_cnt->cnt_rate_illegal = (ret_value & 0xffff);
	falsealm_cnt->cnt_crc8_fail = ((ret_value & 0xffff0000) >> 16);

	ret_value = rtl_get_bbreg(hw, ROFDM_PHYCOUNTER3, MASKDWORD);
	falsealm_cnt->cnt_mcs_fail = (ret_value & 0xffff);
	falsealm_cnt->cnt_ofdm_fail = falsealm_cnt->cnt_parity_fail +
		falsealm_cnt->cnt_rate_illegal +
		falsealm_cnt->cnt_crc8_fail +
		falsealm_cnt->cnt_mcs_fail +
		falsealm_cnt->cnt_fast_fsync_fail +
		falsealm_cnt->cnt_sb_search_fail;

	ret_value = rtl_get_bbreg(hw, REG_SC_CNT, MASKDWORD);
	falsealm_cnt->cnt_bw_lsc = (ret_value & 0xffff);
	falsealm_cnt->cnt_bw_usc = ((ret_value & 0xffff0000) >> 16);

	rtl_set_bbreg(hw, RCCK0_FALSEALARMREPORT, BIT(12), 1);
	rtl_set_bbreg(hw, RCCK0_FALSEALARMREPORT, BIT(14), 1);

	ret_value = rtl_get_bbreg(hw, RCCK0_FACOUNTERLOWER, MASKBYTE0);
	falsealm_cnt->cnt_cck_fail = ret_value;

	ret_value = rtl_get_bbreg(hw, RCCK0_FACOUNTERUPPER, MASKBYTE3);
	falsealm_cnt->cnt_cck_fail += (ret_value & 0xff) << 8;

	ret_value = rtl_get_bbreg(hw, RCCK0_CCA_CNT, MASKDWORD);
	falsealm_cnt->cnt_cck_cca = ((ret_value & 0xff) << 8) |
		((ret_value&0xFF00)>>8);

	falsealm_cnt->cnt_all = (falsealm_cnt->cnt_fast_fsync_fail +
				falsealm_cnt->cnt_sb_search_fail +
				falsealm_cnt->cnt_parity_fail +
				falsealm_cnt->cnt_rate_illegal +
				falsealm_cnt->cnt_crc8_fail +
				falsealm_cnt->cnt_mcs_fail +
				falsealm_cnt->cnt_cck_fail);
	falsealm_cnt->cnt_cca_all = falsealm_cnt->cnt_ofdm_cca +
		falsealm_cnt->cnt_cck_cca;

	rtl_set_bbreg(hw, ROFDM0_TRSWISOLATION, BIT(31), 1);
	rtl_set_bbreg(hw, ROFDM0_TRSWISOLATION, BIT(31), 0);
	rtl_set_bbreg(hw, ROFDM1_LSTF, BIT(27), 1);
	rtl_set_bbreg(hw, ROFDM1_LSTF, BIT(27), 0);
	rtl_set_bbreg(hw, ROFDM0_LSTF, BIT(31), 0);
	rtl_set_bbreg(hw, ROFDM1_LSTF, BIT(31), 0);
	rtl_set_bbreg(hw, RCCK0_FALSEALARMREPORT, BIT(13)|BIT(12), 0);
	rtl_set_bbreg(hw, RCCK0_FALSEALARMREPORT, BIT(13)|BIT(12), 2);
	rtl_set_bbreg(hw, RCCK0_FALSEALARMREPORT, BIT(15)|BIT(14), 0);
	rtl_set_bbreg(hw, RCCK0_FALSEALARMREPORT, BIT(15)|BIT(14), 2);

	rtl_dbg(rtlpriv, COMP_DIG, DBG_TRACE,
		"cnt_parity_fail = %d, cnt_rate_illegal = %d, cnt_crc8_fail = %d, cnt_mcs_fail = %d\n",
		falsealm_cnt->cnt_parity_fail,
		falsealm_cnt->cnt_rate_illegal,
		falsealm_cnt->cnt_crc8_fail, falsealm_cnt->cnt_mcs_fail);

	rtl_dbg(rtlpriv, COMP_DIG, DBG_TRACE,
		"cnt_ofdm_fail = %x, cnt_cck_fail = %x, cnt_all = %x\n",
		falsealm_cnt->cnt_ofdm_fail,
		falsealm_cnt->cnt_cck_fail, falsealm_cnt->cnt_all);
}

static void rtl88e_dm_cck_packet_detection_thresh(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_dig = &rtlpriv->dm_digtable;
	u8 cur_cck_cca_thresh;

	if (dm_dig->cur_sta_cstate == DIG_STA_CONNECT) {
		dm_dig->rssi_val_min = rtl88e_dm_initial_gain_min_pwdb(hw);
		if (dm_dig->rssi_val_min > 25) {
			cur_cck_cca_thresh = 0xcd;
		} else if ((dm_dig->rssi_val_min <= 25) &&
			   (dm_dig->rssi_val_min > 10)) {
			cur_cck_cca_thresh = 0x83;
		} else {
			if (rtlpriv->falsealm_cnt.cnt_cck_fail > 1000)
				cur_cck_cca_thresh = 0x83;
			else
				cur_cck_cca_thresh = 0x40;
		}

	} else {
		if (rtlpriv->falsealm_cnt.cnt_cck_fail > 1000)
			cur_cck_cca_thresh = 0x83;
		else
			cur_cck_cca_thresh = 0x40;
	}

	if (dm_dig->cur_cck_cca_thres != cur_cck_cca_thresh)
		rtl_set_bbreg(hw, RCCK0_CCA, MASKBYTE2, cur_cck_cca_thresh);

	dm_dig->cur_cck_cca_thres = cur_cck_cca_thresh;
	dm_dig->pre_cck_cca_thres = dm_dig->cur_cck_cca_thres;
	rtl_dbg(rtlpriv, COMP_DIG, DBG_TRACE,
		"CCK cca thresh hold =%x\n", dm_dig->cur_cck_cca_thres);
}

static void rtl88e_dm_dig(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct dig_t *dm_dig = &rtlpriv->dm_digtable;
	u8 dig_dynamic_min, dig_maxofmin;
	bool bfirstconnect;
	u8 dm_dig_max, dm_dig_min;
	u8 current_igi = dm_dig->cur_igvalue;

	if (!rtlpriv->dm.dm_initialgain_enable)
		return;
	if (!dm_dig->dig_enable_flag)
		return;
	if (mac->act_scanning)
		return;

	if (mac->link_state >= MAC80211_LINKED)
		dm_dig->cur_sta_cstate = DIG_STA_CONNECT;
	else
		dm_dig->cur_sta_cstate = DIG_STA_DISCONNECT;
	if (rtlpriv->mac80211.opmode == NL80211_IFTYPE_AP ||
	    rtlpriv->mac80211.opmode == NL80211_IFTYPE_ADHOC)
		dm_dig->cur_sta_cstate = DIG_STA_DISCONNECT;

	dm_dig_max = DM_DIG_MAX;
	dm_dig_min = DM_DIG_MIN;
	dig_maxofmin = DM_DIG_MAX_AP;
	dig_dynamic_min = dm_dig->dig_min_0;
	bfirstconnect = ((mac->link_state >= MAC80211_LINKED) ? true : false) &&
			 !dm_dig->media_connect_0;

	dm_dig->rssi_val_min =
		rtl88e_dm_initial_gain_min_pwdb(hw);

	if (mac->link_state >= MAC80211_LINKED) {
		if ((dm_dig->rssi_val_min + 20) > dm_dig_max)
			dm_dig->rx_gain_max = dm_dig_max;
		else if ((dm_dig->rssi_val_min + 20) < dm_dig_min)
			dm_dig->rx_gain_max = dm_dig_min;
		else
			dm_dig->rx_gain_max = dm_dig->rssi_val_min + 20;

		if (rtlefuse->antenna_div_type == CG_TRX_HW_ANTDIV) {
			dig_dynamic_min  = dm_dig->antdiv_rssi_max;
		} else {
			if (dm_dig->rssi_val_min < dm_dig_min)
				dig_dynamic_min = dm_dig_min;
			else if (dm_dig->rssi_val_min < dig_maxofmin)
				dig_dynamic_min = dig_maxofmin;
			else
				dig_dynamic_min = dm_dig->rssi_val_min;
		}
	} else {
		dm_dig->rx_gain_max = dm_dig_max;
		dig_dynamic_min = dm_dig_min;
		rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD, "no link\n");
	}

	if (rtlpriv->falsealm_cnt.cnt_all > 10000) {
		dm_dig->large_fa_hit++;
		if (dm_dig->forbidden_igi < current_igi) {
			dm_dig->forbidden_igi = current_igi;
			dm_dig->large_fa_hit = 1;
		}

		if (dm_dig->large_fa_hit >= 3) {
			if ((dm_dig->forbidden_igi + 1) >
				dm_dig->rx_gain_max)
				dm_dig->rx_gain_min =
					dm_dig->rx_gain_max;
			else
				dm_dig->rx_gain_min =
					dm_dig->forbidden_igi + 1;
			dm_dig->recover_cnt = 3600;
		}
	} else {
		if (dm_dig->recover_cnt != 0) {
			dm_dig->recover_cnt--;
		} else {
			if (dm_dig->large_fa_hit == 0) {
				if ((dm_dig->forbidden_igi - 1) <
				    dig_dynamic_min) {
					dm_dig->forbidden_igi = dig_dynamic_min;
					dm_dig->rx_gain_min = dig_dynamic_min;
				} else {
					dm_dig->forbidden_igi--;
					dm_dig->rx_gain_min =
						dm_dig->forbidden_igi + 1;
				}
			} else if (dm_dig->large_fa_hit == 3) {
				dm_dig->large_fa_hit = 0;
			}
		}
	}

	if (dm_dig->cur_sta_cstate == DIG_STA_CONNECT) {
		if (bfirstconnect) {
			current_igi = dm_dig->rssi_val_min;
		} else {
			if (rtlpriv->falsealm_cnt.cnt_all > DM_DIG_FA_TH2)
				current_igi += 2;
			else if (rtlpriv->falsealm_cnt.cnt_all > DM_DIG_FA_TH1)
				current_igi++;
			else if (rtlpriv->falsealm_cnt.cnt_all < DM_DIG_FA_TH0)
				current_igi--;
		}
	} else {
		if (rtlpriv->falsealm_cnt.cnt_all > 10000)
			current_igi += 2;
		else if (rtlpriv->falsealm_cnt.cnt_all > 8000)
			current_igi++;
		else if (rtlpriv->falsealm_cnt.cnt_all < 500)
			current_igi--;
	}

	if (current_igi > DM_DIG_FA_UPPER)
		current_igi = DM_DIG_FA_UPPER;
	else if (current_igi < DM_DIG_FA_LOWER)
		current_igi = DM_DIG_FA_LOWER;

	if (rtlpriv->falsealm_cnt.cnt_all > 10000)
		current_igi = DM_DIG_FA_UPPER;

	dm_dig->cur_igvalue = current_igi;
	rtl88e_dm_write_dig(hw);
	dm_dig->media_connect_0 =
		((mac->link_state >= MAC80211_LINKED) ? true : false);
	dm_dig->dig_min_0 = dig_dynamic_min;

	rtl88e_dm_cck_packet_detection_thresh(hw);
}

static void rtl88e_dm_init_dynamic_txpower(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->dm.dynamic_txpower_enable = false;

	rtlpriv->dm.last_dtp_lvl = TXHIGHPWRLEVEL_NORMAL;
	rtlpriv->dm.dynamic_txhighpower_lvl = TXHIGHPWRLEVEL_NORMAL;
}

static void rtl92c_dm_dynamic_txpower(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	long undec_sm_pwdb;

	if (!rtlpriv->dm.dynamic_txpower_enable)
		return;

	if (rtlpriv->dm.dm_flag & HAL_DM_HIPWR_DISABLE) {
		rtlpriv->dm.dynamic_txhighpower_lvl = TXHIGHPWRLEVEL_NORMAL;
		return;
	}

	if ((mac->link_state < MAC80211_LINKED) &&
	    (rtlpriv->dm.entry_min_undec_sm_pwdb == 0)) {
		rtl_dbg(rtlpriv, COMP_POWER, DBG_TRACE,
			"Not connected to any\n");

		rtlpriv->dm.dynamic_txhighpower_lvl = TXHIGHPWRLEVEL_NORMAL;

		rtlpriv->dm.last_dtp_lvl = TXHIGHPWRLEVEL_NORMAL;
		return;
	}

	if (mac->link_state >= MAC80211_LINKED) {
		if (mac->opmode == NL80211_IFTYPE_ADHOC) {
			undec_sm_pwdb =
			    rtlpriv->dm.entry_min_undec_sm_pwdb;
			rtl_dbg(rtlpriv, COMP_POWER, DBG_LOUD,
				"AP Client PWDB = 0x%lx\n",
				undec_sm_pwdb);
		} else {
			undec_sm_pwdb =
			    rtlpriv->dm.undec_sm_pwdb;
			rtl_dbg(rtlpriv, COMP_POWER, DBG_LOUD,
				"STA Default Port PWDB = 0x%lx\n",
				undec_sm_pwdb);
		}
	} else {
		undec_sm_pwdb =
		    rtlpriv->dm.entry_min_undec_sm_pwdb;

		rtl_dbg(rtlpriv, COMP_POWER, DBG_LOUD,
			"AP Ext Port PWDB = 0x%lx\n",
			undec_sm_pwdb);
	}

	if (undec_sm_pwdb >= TX_POWER_NEAR_FIELD_THRESH_LVL2) {
		rtlpriv->dm.dynamic_txhighpower_lvl = TXHIGHPWRLEVEL_LEVEL1;
		rtl_dbg(rtlpriv, COMP_POWER, DBG_LOUD,
			"TXHIGHPWRLEVEL_LEVEL1 (TxPwr = 0x0)\n");
	} else if ((undec_sm_pwdb <
		    (TX_POWER_NEAR_FIELD_THRESH_LVL2 - 3)) &&
		   (undec_sm_pwdb >=
		    TX_POWER_NEAR_FIELD_THRESH_LVL1)) {
		rtlpriv->dm.dynamic_txhighpower_lvl = TXHIGHPWRLEVEL_LEVEL1;
		rtl_dbg(rtlpriv, COMP_POWER, DBG_LOUD,
			"TXHIGHPWRLEVEL_LEVEL1 (TxPwr = 0x10)\n");
	} else if (undec_sm_pwdb <
		   (TX_POWER_NEAR_FIELD_THRESH_LVL1 - 5)) {
		rtlpriv->dm.dynamic_txhighpower_lvl = TXHIGHPWRLEVEL_NORMAL;
		rtl_dbg(rtlpriv, COMP_POWER, DBG_LOUD,
			"TXHIGHPWRLEVEL_NORMAL\n");
	}

	if ((rtlpriv->dm.dynamic_txhighpower_lvl !=
		rtlpriv->dm.last_dtp_lvl)) {
		rtl_dbg(rtlpriv, COMP_POWER, DBG_LOUD,
			"PHY_SetTxPowerLevel8192S() Channel = %d\n",
			  rtlphy->current_channel);
		rtl88e_phy_set_txpower_level(hw, rtlphy->current_channel);
	}

	rtlpriv->dm.last_dtp_lvl = rtlpriv->dm.dynamic_txhighpower_lvl;
}

void rtl88e_dm_write_dig(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_dig = &rtlpriv->dm_digtable;

	rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
		"cur_igvalue = 0x%x, pre_igvalue = 0x%x, backoff_val = %d\n",
		 dm_dig->cur_igvalue, dm_dig->pre_igvalue,
		 dm_dig->back_val);

	if (dm_dig->cur_igvalue > 0x3f)
		dm_dig->cur_igvalue = 0x3f;
	if (dm_dig->pre_igvalue != dm_dig->cur_igvalue) {
		rtl_set_bbreg(hw, ROFDM0_XAAGCCORE1, 0x7f,
			      dm_dig->cur_igvalue);

		dm_dig->pre_igvalue = dm_dig->cur_igvalue;
	}
}

static void rtl88e_dm_pwdb_monitor(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_sta_info *drv_priv;
	static u64 last_record_txok_cnt;
	static u64 last_record_rxok_cnt;
	long tmp_entry_max_pwdb = 0, tmp_entry_min_pwdb = 0xff;

	if (rtlhal->oem_id == RT_CID_819X_HP) {
		u64 cur_txok_cnt = 0;
		u64 cur_rxok_cnt = 0;
		cur_txok_cnt = rtlpriv->stats.txbytesunicast -
			last_record_txok_cnt;
		cur_rxok_cnt = rtlpriv->stats.rxbytesunicast -
			last_record_rxok_cnt;
		last_record_txok_cnt = cur_txok_cnt;
		last_record_rxok_cnt = cur_rxok_cnt;

		if (cur_rxok_cnt > (cur_txok_cnt * 6))
			rtl_write_dword(rtlpriv, REG_ARFR0, 0x8f015);
		else
			rtl_write_dword(rtlpriv, REG_ARFR0, 0xff015);
	}

	/* AP & ADHOC & MESH */
	spin_lock_bh(&rtlpriv->locks.entry_list_lock);
	list_for_each_entry(drv_priv, &rtlpriv->entry_list, list) {
		if (drv_priv->rssi_stat.undec_sm_pwdb <
			tmp_entry_min_pwdb)
			tmp_entry_min_pwdb = drv_priv->rssi_stat.undec_sm_pwdb;
		if (drv_priv->rssi_stat.undec_sm_pwdb >
			tmp_entry_max_pwdb)
			tmp_entry_max_pwdb = drv_priv->rssi_stat.undec_sm_pwdb;
	}
	spin_unlock_bh(&rtlpriv->locks.entry_list_lock);

	/* If associated entry is found */
	if (tmp_entry_max_pwdb != 0) {
		rtlpriv->dm.entry_max_undec_sm_pwdb = tmp_entry_max_pwdb;
		RTPRINT(rtlpriv, FDM, DM_PWDB, "EntryMaxPWDB = 0x%lx(%ld)\n",
			tmp_entry_max_pwdb, tmp_entry_max_pwdb);
	} else {
		rtlpriv->dm.entry_max_undec_sm_pwdb = 0;
	}
	/* If associated entry is found */
	if (tmp_entry_min_pwdb != 0xff) {
		rtlpriv->dm.entry_min_undec_sm_pwdb = tmp_entry_min_pwdb;
		RTPRINT(rtlpriv, FDM, DM_PWDB, "EntryMinPWDB = 0x%lx(%ld)\n",
					tmp_entry_min_pwdb, tmp_entry_min_pwdb);
	} else {
		rtlpriv->dm.entry_min_undec_sm_pwdb = 0;
	}
	/* Indicate Rx signal strength to FW. */
	if (!rtlpriv->dm.useramask)
		rtl_write_byte(rtlpriv, 0x4fe, rtlpriv->dm.undec_sm_pwdb);
}

void rtl88e_dm_init_edca_turbo(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->dm.current_turbo_edca = false;
	rtlpriv->dm.is_any_nonbepkts = false;
	rtlpriv->dm.is_cur_rdlstate = false;
}

static void rtl88e_dm_check_edca_turbo(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	static u64 last_txok_cnt;
	static u64 last_rxok_cnt;
	static u32 last_bt_edca_ul;
	static u32 last_bt_edca_dl;
	u64 cur_txok_cnt = 0;
	u64 cur_rxok_cnt = 0;
	u32 edca_be_ul = 0x5ea42b;
	u32 edca_be_dl = 0x5ea42b;
	bool bt_change_edca = false;

	if ((last_bt_edca_ul != rtlpriv->btcoexist.bt_edca_ul) ||
	    (last_bt_edca_dl != rtlpriv->btcoexist.bt_edca_dl)) {
		rtlpriv->dm.current_turbo_edca = false;
		last_bt_edca_ul = rtlpriv->btcoexist.bt_edca_ul;
		last_bt_edca_dl = rtlpriv->btcoexist.bt_edca_dl;
	}

	if (rtlpriv->btcoexist.bt_edca_ul != 0) {
		edca_be_ul = rtlpriv->btcoexist.bt_edca_ul;
		bt_change_edca = true;
	}

	if (rtlpriv->btcoexist.bt_edca_dl != 0) {
		edca_be_ul = rtlpriv->btcoexist.bt_edca_dl;
		bt_change_edca = true;
	}

	if (mac->link_state != MAC80211_LINKED) {
		rtlpriv->dm.current_turbo_edca = false;
		return;
	}
	if ((bt_change_edca) ||
	    ((!rtlpriv->dm.is_any_nonbepkts) &&
	     (!rtlpriv->dm.disable_framebursting))) {

		cur_txok_cnt = rtlpriv->stats.txbytesunicast - last_txok_cnt;
		cur_rxok_cnt = rtlpriv->stats.rxbytesunicast - last_rxok_cnt;

		if (cur_rxok_cnt > 4 * cur_txok_cnt) {
			if (!rtlpriv->dm.is_cur_rdlstate ||
			    !rtlpriv->dm.current_turbo_edca) {
				rtl_write_dword(rtlpriv,
						REG_EDCA_BE_PARAM,
						edca_be_dl);
				rtlpriv->dm.is_cur_rdlstate = true;
			}
		} else {
			if (rtlpriv->dm.is_cur_rdlstate ||
			    !rtlpriv->dm.current_turbo_edca) {
				rtl_write_dword(rtlpriv,
						REG_EDCA_BE_PARAM,
						edca_be_ul);
				rtlpriv->dm.is_cur_rdlstate = false;
			}
		}
		rtlpriv->dm.current_turbo_edca = true;
	} else {
		if (rtlpriv->dm.current_turbo_edca) {
			u8 tmp = AC0_BE;

			rtlpriv->cfg->ops->set_hw_reg(hw,
						      HW_VAR_AC_PARAM,
						      &tmp);
			rtlpriv->dm.current_turbo_edca = false;
		}
	}

	rtlpriv->dm.is_any_nonbepkts = false;
	last_txok_cnt = rtlpriv->stats.txbytesunicast;
	last_rxok_cnt = rtlpriv->stats.rxbytesunicast;
}

static void dm_txpower_track_cb_therm(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_dm	*rtldm = rtl_dm(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 thermalvalue = 0, delta, delta_lck, delta_iqk, offset;
	u8 thermalvalue_avg_count = 0;
	u32 thermalvalue_avg = 0;
	long  ele_d, temp_cck;
	s8 ofdm_index[2], cck_index = 0,
		ofdm_index_old[2] = {0, 0}, cck_index_old = 0;
	int i = 0;
	/*bool is2t = false;*/

	u8 ofdm_min_index = 6, rf = 1;
	/*u8 index_for_channel;*/
	enum _power_dec_inc {power_dec, power_inc};

	/*0.1 the following TWO tables decide the
	 *final index of OFDM/CCK swing table
	 */
	static const s8 delta_swing_table_idx[2][15]  = {
		{0, 0, 2, 3, 4, 4, 5, 6, 7, 7, 8, 9, 10, 10, 11},
		{0, 0, -1, -2, -3, -4, -4, -4, -4, -5, -7, -8, -9, -9, -10}
	};
	static const u8 thermal_threshold[2][15] = {
		{0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 27},
		{0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 25, 25, 25}
	};

	/*Initilization (7 steps in total) */
	rtlpriv->dm.txpower_trackinginit = true;
	rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		"%s\n", __func__);

	thermalvalue = (u8)rtl_get_rfreg(hw, RF90_PATH_A, RF_T_METER,
					 0xfc00);
	if (!thermalvalue)
		return;
	rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		"Readback Thermal Meter = 0x%x pre thermal meter 0x%x eeprom_thermalmeter 0x%x\n",
		thermalvalue, rtlpriv->dm.thermalvalue,
		rtlefuse->eeprom_thermalmeter);

	/*1. Query OFDM Default Setting: Path A*/
	ele_d = rtl_get_bbreg(hw, ROFDM0_XATXIQIMBALANCE, MASKDWORD) &
			      MASKOFDM_D;
	for (i = 0; i < OFDM_TABLE_LENGTH; i++) {
		if (ele_d == (ofdmswing_table[i] & MASKOFDM_D)) {
			ofdm_index_old[0] = (u8)i;
			rtldm->swing_idx_ofdm_base[RF90_PATH_A] = (u8)i;
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"Initial pathA ele_d reg0x%x = 0x%lx, ofdm_index = 0x%x\n",
				 ROFDM0_XATXIQIMBALANCE,
				 ele_d, ofdm_index_old[0]);
			break;
		}
	}

	/*2.Query CCK default setting From 0xa24*/
	temp_cck = rtl_get_bbreg(hw, RCCK0_TXFILTER2, MASKDWORD) & MASKCCK;
	for (i = 0; i < CCK_TABLE_LENGTH; i++) {
		if (rtlpriv->dm.cck_inch14) {
			if (memcmp(&temp_cck, &cck_tbl_ch14[i][2], 4) == 0) {
				cck_index_old = (u8)i;
				rtldm->swing_idx_cck_base = (u8)i;
				rtl_dbg(rtlpriv, COMP_POWER_TRACKING,
					DBG_LOUD,
					"Initial reg0x%x = 0x%lx, cck_index = 0x%x, ch 14 %d\n",
					RCCK0_TXFILTER2, temp_cck,
					cck_index_old,
					rtlpriv->dm.cck_inch14);
				break;
			}
		} else {
			if (memcmp(&temp_cck, &cck_tbl_ch1_13[i][2], 4) == 0) {
				cck_index_old = (u8)i;
				rtldm->swing_idx_cck_base = (u8)i;
				rtl_dbg(rtlpriv, COMP_POWER_TRACKING,
					DBG_LOUD,
					"Initial reg0x%x = 0x%lx, cck_index = 0x%x, ch14 %d\n",
					RCCK0_TXFILTER2, temp_cck,
					cck_index_old,
					rtlpriv->dm.cck_inch14);
				break;
			}
		}
	}

	/*3 Initialize ThermalValues of RFCalibrateInfo*/
	if (!rtldm->thermalvalue) {
		rtlpriv->dm.thermalvalue = rtlefuse->eeprom_thermalmeter;
		rtlpriv->dm.thermalvalue_lck = thermalvalue;
		rtlpriv->dm.thermalvalue_iqk = thermalvalue;
		for (i = 0; i < rf; i++)
			rtlpriv->dm.ofdm_index[i] = ofdm_index_old[i];
		rtlpriv->dm.cck_index = cck_index_old;
	}

	/*4 Calculate average thermal meter*/
	rtldm->thermalvalue_avg[rtldm->thermalvalue_avg_index] = thermalvalue;
	rtldm->thermalvalue_avg_index++;
	if (rtldm->thermalvalue_avg_index == AVG_THERMAL_NUM_88E)
		rtldm->thermalvalue_avg_index = 0;

	for (i = 0; i < AVG_THERMAL_NUM_88E; i++) {
		if (rtldm->thermalvalue_avg[i]) {
			thermalvalue_avg += rtldm->thermalvalue_avg[i];
			thermalvalue_avg_count++;
		}
	}

	if (thermalvalue_avg_count)
		thermalvalue = (u8)(thermalvalue_avg / thermalvalue_avg_count);

	/* 5 Calculate delta, delta_LCK, delta_IQK.*/
	if (rtlhal->reloadtxpowerindex) {
		delta = (thermalvalue > rtlefuse->eeprom_thermalmeter) ?
		    (thermalvalue - rtlefuse->eeprom_thermalmeter) :
		    (rtlefuse->eeprom_thermalmeter - thermalvalue);
		rtlhal->reloadtxpowerindex = false;
		rtlpriv->dm.done_txpower = false;
	} else if (rtlpriv->dm.done_txpower) {
		delta = (thermalvalue > rtlpriv->dm.thermalvalue) ?
		    (thermalvalue - rtlpriv->dm.thermalvalue) :
		    (rtlpriv->dm.thermalvalue - thermalvalue);
	} else {
		delta = (thermalvalue > rtlefuse->eeprom_thermalmeter) ?
		    (thermalvalue - rtlefuse->eeprom_thermalmeter) :
		    (rtlefuse->eeprom_thermalmeter - thermalvalue);
	}
	delta_lck = (thermalvalue > rtlpriv->dm.thermalvalue_lck) ?
	    (thermalvalue - rtlpriv->dm.thermalvalue_lck) :
	    (rtlpriv->dm.thermalvalue_lck - thermalvalue);
	delta_iqk = (thermalvalue > rtlpriv->dm.thermalvalue_iqk) ?
	    (thermalvalue - rtlpriv->dm.thermalvalue_iqk) :
	    (rtlpriv->dm.thermalvalue_iqk - thermalvalue);

	rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		"Readback Thermal Meter = 0x%x pre thermal meter 0x%x eeprom_thermalmeter 0x%x delta 0x%x delta_lck 0x%x delta_iqk 0x%x\n",
		thermalvalue, rtlpriv->dm.thermalvalue,
		rtlefuse->eeprom_thermalmeter, delta, delta_lck,
		delta_iqk);
	/* 6 If necessary, do LCK.*/
	if (delta_lck >= 8) {
		rtlpriv->dm.thermalvalue_lck = thermalvalue;
		rtl88e_phy_lc_calibrate(hw);
	}

	/* 7 If necessary, move the index of
	 * swing table to adjust Tx power.
	 */
	if (delta > 0 && rtlpriv->dm.txpower_track_control) {
		delta = (thermalvalue > rtlefuse->eeprom_thermalmeter) ?
		    (thermalvalue - rtlefuse->eeprom_thermalmeter) :
		    (rtlefuse->eeprom_thermalmeter - thermalvalue);

		/* 7.1 Get the final CCK_index and OFDM_index for each
		 * swing table.
		 */
		if (thermalvalue > rtlefuse->eeprom_thermalmeter) {
			CAL_SWING_OFF(offset, power_inc, INDEX_MAPPING_NUM,
				      delta);
			for (i = 0; i < rf; i++)
				ofdm_index[i] =
				  rtldm->ofdm_index[i] +
				  delta_swing_table_idx[power_inc][offset];
			cck_index = rtldm->cck_index +
				delta_swing_table_idx[power_inc][offset];
		} else {
			CAL_SWING_OFF(offset, power_dec, INDEX_MAPPING_NUM,
				      delta);
			for (i = 0; i < rf; i++)
				ofdm_index[i] =
				  rtldm->ofdm_index[i] +
				  delta_swing_table_idx[power_dec][offset];
			cck_index = rtldm->cck_index +
				delta_swing_table_idx[power_dec][offset];
		}

		/* 7.2 Handle boundary conditions of index.*/
		for (i = 0; i < rf; i++) {
			if (ofdm_index[i] > OFDM_TABLE_SIZE-1)
				ofdm_index[i] = OFDM_TABLE_SIZE-1;
			else if (rtldm->ofdm_index[i] < ofdm_min_index)
				ofdm_index[i] = ofdm_min_index;
		}

		if (cck_index > CCK_TABLE_SIZE-1)
			cck_index = CCK_TABLE_SIZE-1;
		else if (cck_index < 0)
			cck_index = 0;

		/*7.3Configure the Swing Table to adjust Tx Power.*/
		if (rtlpriv->dm.txpower_track_control) {
			rtldm->done_txpower = true;
			rtldm->swing_idx_ofdm[RF90_PATH_A] =
				(u8)ofdm_index[RF90_PATH_A];
			rtldm->swing_idx_cck = cck_index;
			if (rtldm->swing_idx_ofdm_cur !=
			    rtldm->swing_idx_ofdm[0]) {
				rtldm->swing_idx_ofdm_cur =
					 rtldm->swing_idx_ofdm[0];
				rtldm->swing_flag_ofdm = true;
			}

			if (rtldm->swing_idx_cck_cur != rtldm->swing_idx_cck) {
				rtldm->swing_idx_cck_cur = rtldm->swing_idx_cck;
				rtldm->swing_flag_cck = true;
			}

			dm_tx_pwr_track_set_pwr(hw, TXAGC, 0, 0);
		}
	}

	if (delta_iqk >= 8) {
		rtlpriv->dm.thermalvalue_iqk = thermalvalue;
		rtl88e_phy_iq_calibrate(hw, false);
	}

	if (rtldm->txpower_track_control)
		rtldm->thermalvalue = thermalvalue;
	rtldm->txpowercount = 0;
	rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD, "end\n");
}

static void rtl88e_dm_init_txpower_tracking(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->dm.txpower_tracking = true;
	rtlpriv->dm.txpower_trackinginit = false;
	rtlpriv->dm.txpowercount = 0;
	rtlpriv->dm.txpower_track_control = true;

	rtlpriv->dm.swing_idx_ofdm[RF90_PATH_A] = 12;
	rtlpriv->dm.swing_idx_ofdm_cur = 12;
	rtlpriv->dm.swing_flag_ofdm = false;
	rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		"rtlpriv->dm.txpower_tracking = %d\n",
		rtlpriv->dm.txpower_tracking);
}

void rtl88e_dm_check_txpower_tracking(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (!rtlpriv->dm.txpower_tracking)
		return;

	if (!rtlpriv->dm.tm_trigger) {
		rtl_set_rfreg(hw, RF90_PATH_A, RF_T_METER, BIT(17)|BIT(16),
			      0x03);
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"Trigger 88E Thermal Meter!!\n");
		rtlpriv->dm.tm_trigger = 1;
		return;
	} else {
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"Schedule TxPowerTracking !!\n");
		dm_txpower_track_cb_therm(hw);
		rtlpriv->dm.tm_trigger = 0;
	}
}

void rtl88e_dm_init_rate_adaptive_mask(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rate_adaptive *p_ra = &rtlpriv->ra;

	p_ra->ratr_state = DM_RATR_STA_INIT;
	p_ra->pre_ratr_state = DM_RATR_STA_INIT;

	if (rtlpriv->dm.dm_type == DM_TYPE_BYDRIVER)
		rtlpriv->dm.useramask = true;
	else
		rtlpriv->dm.useramask = false;
}

static void rtl88e_dm_refresh_rate_adaptive_mask(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rate_adaptive *p_ra = &rtlpriv->ra;
	u32 low_rssithresh_for_ra, high_rssithresh_for_ra;
	struct ieee80211_sta *sta = NULL;

	if (is_hal_stop(rtlhal)) {
		rtl_dbg(rtlpriv, COMP_RATE, DBG_LOUD,
			"driver is going to unload\n");
		return;
	}

	if (!rtlpriv->dm.useramask) {
		rtl_dbg(rtlpriv, COMP_RATE, DBG_LOUD,
			"driver does not control rate adaptive mask\n");
		return;
	}

	if (mac->link_state == MAC80211_LINKED &&
	    mac->opmode == NL80211_IFTYPE_STATION) {
		switch (p_ra->pre_ratr_state) {
		case DM_RATR_STA_HIGH:
			high_rssithresh_for_ra = 50;
			low_rssithresh_for_ra = 20;
			break;
		case DM_RATR_STA_MIDDLE:
			high_rssithresh_for_ra = 55;
			low_rssithresh_for_ra = 20;
			break;
		case DM_RATR_STA_LOW:
			high_rssithresh_for_ra = 50;
			low_rssithresh_for_ra = 25;
			break;
		default:
			high_rssithresh_for_ra = 50;
			low_rssithresh_for_ra = 20;
			break;
		}

		if (rtlpriv->dm.undec_sm_pwdb >
		    (long)high_rssithresh_for_ra)
			p_ra->ratr_state = DM_RATR_STA_HIGH;
		else if (rtlpriv->dm.undec_sm_pwdb >
			 (long)low_rssithresh_for_ra)
			p_ra->ratr_state = DM_RATR_STA_MIDDLE;
		else
			p_ra->ratr_state = DM_RATR_STA_LOW;

		if (p_ra->pre_ratr_state != p_ra->ratr_state) {
			rtl_dbg(rtlpriv, COMP_RATE, DBG_LOUD,
				"RSSI = %ld\n",
				rtlpriv->dm.undec_sm_pwdb);
			rtl_dbg(rtlpriv, COMP_RATE, DBG_LOUD,
				"RSSI_LEVEL = %d\n", p_ra->ratr_state);
			rtl_dbg(rtlpriv, COMP_RATE, DBG_LOUD,
				"PreState = %d, CurState = %d\n",
				p_ra->pre_ratr_state, p_ra->ratr_state);

			rcu_read_lock();
			sta = rtl_find_sta(hw, mac->bssid);
			if (sta)
				rtlpriv->cfg->ops->update_rate_tbl(hw, sta,
							p_ra->ratr_state,
								   true);
			rcu_read_unlock();

			p_ra->pre_ratr_state = p_ra->ratr_state;
		}
	}
}

static void rtl92c_dm_init_dynamic_bb_powersaving(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct ps_t *dm_pstable = &rtlpriv->dm_pstable;

	dm_pstable->pre_ccastate = CCA_MAX;
	dm_pstable->cur_ccasate = CCA_MAX;
	dm_pstable->pre_rfstate = RF_MAX;
	dm_pstable->cur_rfstate = RF_MAX;
	dm_pstable->rssi_val_min = 0;
}

static void rtl88e_dm_update_rx_idle_ant(struct ieee80211_hw *hw,
					 u8 ant)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));
	struct fast_ant_training *pfat_table = &rtldm->fat_table;
	u32 default_ant, optional_ant;

	if (pfat_table->rx_idle_ant != ant) {
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
			"need to update rx idle ant\n");
		if (ant == MAIN_ANT) {
			default_ant =
			  (pfat_table->rx_idle_ant == CG_TRX_HW_ANTDIV) ?
			  MAIN_ANT_CG_TRX : MAIN_ANT_CGCS_RX;
			optional_ant =
			  (pfat_table->rx_idle_ant == CG_TRX_HW_ANTDIV) ?
			  AUX_ANT_CG_TRX : AUX_ANT_CGCS_RX;
		} else {
			default_ant =
			   (pfat_table->rx_idle_ant == CG_TRX_HW_ANTDIV) ?
			   AUX_ANT_CG_TRX : AUX_ANT_CGCS_RX;
			optional_ant =
			   (pfat_table->rx_idle_ant == CG_TRX_HW_ANTDIV) ?
			   MAIN_ANT_CG_TRX : MAIN_ANT_CGCS_RX;
		}

		if (rtlefuse->antenna_div_type == CG_TRX_HW_ANTDIV) {
			rtl_set_bbreg(hw, DM_REG_RX_ANT_CTRL_11N,
				      BIT(5) | BIT(4) | BIT(3), default_ant);
			rtl_set_bbreg(hw, DM_REG_RX_ANT_CTRL_11N,
				      BIT(8) | BIT(7) | BIT(6), optional_ant);
			rtl_set_bbreg(hw, DM_REG_ANTSEL_CTRL_11N,
				      BIT(14) | BIT(13) | BIT(12),
				      default_ant);
			rtl_set_bbreg(hw, DM_REG_RESP_TX_11N,
				      BIT(6) | BIT(7), default_ant);
		} else if (rtlefuse->antenna_div_type == CGCS_RX_HW_ANTDIV) {
			rtl_set_bbreg(hw, DM_REG_RX_ANT_CTRL_11N,
				      BIT(5) | BIT(4) | BIT(3), default_ant);
			rtl_set_bbreg(hw, DM_REG_RX_ANT_CTRL_11N,
				      BIT(8) | BIT(7) | BIT(6), optional_ant);
		}
	}
	pfat_table->rx_idle_ant = ant;
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "RxIdleAnt %s\n",
		(ant == MAIN_ANT) ? ("MAIN_ANT") : ("AUX_ANT"));
}

static void rtl88e_dm_update_tx_ant(struct ieee80211_hw *hw,
				    u8 ant, u32 mac_id)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));
	struct fast_ant_training *pfat_table = &rtldm->fat_table;
	u8 target_ant;

	if (ant == MAIN_ANT)
		target_ant = MAIN_ANT_CG_TRX;
	else
		target_ant = AUX_ANT_CG_TRX;

	pfat_table->antsel_a[mac_id] = target_ant & BIT(0);
	pfat_table->antsel_b[mac_id] = (target_ant & BIT(1)) >> 1;
	pfat_table->antsel_c[mac_id] = (target_ant & BIT(2)) >> 2;
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "txfrominfo target ant %s\n",
		(ant == MAIN_ANT) ? ("MAIN_ANT") : ("AUX_ANT"));
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "antsel_tr_mux = 3'b%d%d%d\n",
		pfat_table->antsel_c[mac_id],
		pfat_table->antsel_b[mac_id],
		pfat_table->antsel_a[mac_id]);
}

static void rtl88e_dm_rx_hw_antena_div_init(struct ieee80211_hw *hw)
{
	u32  value32;

	/*MAC Setting*/
	value32 = rtl_get_bbreg(hw, DM_REG_ANTSEL_PIN_11N, MASKDWORD);
	rtl_set_bbreg(hw, DM_REG_ANTSEL_PIN_11N,
		      MASKDWORD, value32 | (BIT(23) | BIT(25)));
	/*Pin Setting*/
	rtl_set_bbreg(hw, DM_REG_PIN_CTRL_11N, BIT(9) | BIT(8), 0);
	rtl_set_bbreg(hw, DM_REG_RX_ANT_CTRL_11N, BIT(10), 0);
	rtl_set_bbreg(hw, DM_REG_LNA_SWITCH_11N, BIT(22), 1);
	rtl_set_bbreg(hw, DM_REG_LNA_SWITCH_11N, BIT(31), 1);
	/*OFDM Setting*/
	rtl_set_bbreg(hw, DM_REG_ANTDIV_PARA1_11N, MASKDWORD, 0x000000a0);
	/*CCK Setting*/
	rtl_set_bbreg(hw, DM_REG_BB_PWR_SAV4_11N, BIT(7), 1);
	rtl_set_bbreg(hw, DM_REG_CCK_ANTDIV_PARA2_11N, BIT(4), 1);
	rtl88e_dm_update_rx_idle_ant(hw, MAIN_ANT);
	rtl_set_bbreg(hw, DM_REG_ANT_MAPPING1_11N, MASKLWORD, 0x0201);
}

static void rtl88e_dm_trx_hw_antenna_div_init(struct ieee80211_hw *hw)
{
	u32  value32;

	/*MAC Setting*/
	value32 = rtl_get_bbreg(hw, DM_REG_ANTSEL_PIN_11N, MASKDWORD);
	rtl_set_bbreg(hw, DM_REG_ANTSEL_PIN_11N, MASKDWORD,
		      value32 | (BIT(23) | BIT(25)));
	/*Pin Setting*/
	rtl_set_bbreg(hw, DM_REG_PIN_CTRL_11N, BIT(9) | BIT(8), 0);
	rtl_set_bbreg(hw, DM_REG_RX_ANT_CTRL_11N, BIT(10), 0);
	rtl_set_bbreg(hw, DM_REG_LNA_SWITCH_11N, BIT(22), 0);
	rtl_set_bbreg(hw, DM_REG_LNA_SWITCH_11N, BIT(31), 1);
	/*OFDM Setting*/
	rtl_set_bbreg(hw, DM_REG_ANTDIV_PARA1_11N, MASKDWORD, 0x000000a0);
	/*CCK Setting*/
	rtl_set_bbreg(hw, DM_REG_BB_PWR_SAV4_11N, BIT(7), 1);
	rtl_set_bbreg(hw, DM_REG_CCK_ANTDIV_PARA2_11N, BIT(4), 1);
	/*TX Setting*/
	rtl_set_bbreg(hw, DM_REG_TX_ANT_CTRL_11N, BIT(21), 0);
	rtl88e_dm_update_rx_idle_ant(hw, MAIN_ANT);
	rtl_set_bbreg(hw, DM_REG_ANT_MAPPING1_11N, MASKLWORD, 0x0201);
}

static void rtl88e_dm_fast_training_init(struct ieee80211_hw *hw)
{
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));
	struct fast_ant_training *pfat_table = &rtldm->fat_table;
	u32 ant_combination = 2;
	u32 value32, i;

	for (i = 0; i < 6; i++) {
		pfat_table->bssid[i] = 0;
		pfat_table->ant_sum[i] = 0;
		pfat_table->ant_cnt[i] = 0;
		pfat_table->ant_ave[i] = 0;
	}
	pfat_table->train_idx = 0;
	pfat_table->fat_state = FAT_NORMAL_STATE;

	/*MAC Setting*/
	value32 = rtl_get_bbreg(hw, DM_REG_ANTSEL_PIN_11N, MASKDWORD);
	rtl_set_bbreg(hw, DM_REG_ANTSEL_PIN_11N,
		      MASKDWORD, value32 | (BIT(23) | BIT(25)));
	value32 = rtl_get_bbreg(hw, DM_REG_ANT_TRAIN_PARA2_11N, MASKDWORD);
	rtl_set_bbreg(hw, DM_REG_ANT_TRAIN_PARA2_11N,
		      MASKDWORD, value32 | (BIT(16) | BIT(17)));
	rtl_set_bbreg(hw, DM_REG_ANT_TRAIN_PARA2_11N,
		      MASKLWORD, 0);
	rtl_set_bbreg(hw, DM_REG_ANT_TRAIN_PARA1_11N,
		      MASKDWORD, 0);

	/*Pin Setting*/
	rtl_set_bbreg(hw, DM_REG_PIN_CTRL_11N, BIT(9) | BIT(8), 0);
	rtl_set_bbreg(hw, DM_REG_RX_ANT_CTRL_11N, BIT(10), 0);
	rtl_set_bbreg(hw, DM_REG_LNA_SWITCH_11N, BIT(22), 0);
	rtl_set_bbreg(hw, DM_REG_LNA_SWITCH_11N, BIT(31), 1);

	/*OFDM Setting*/
	rtl_set_bbreg(hw, DM_REG_ANTDIV_PARA1_11N, MASKDWORD, 0x000000a0);
	/*antenna mapping table*/
	rtl_set_bbreg(hw, DM_REG_ANT_MAPPING1_11N, MASKBYTE0, 1);
	rtl_set_bbreg(hw, DM_REG_ANT_MAPPING1_11N, MASKBYTE1, 2);

	/*TX Setting*/
	rtl_set_bbreg(hw, DM_REG_TX_ANT_CTRL_11N, BIT(21), 1);
	rtl_set_bbreg(hw, DM_REG_RX_ANT_CTRL_11N,
		      BIT(5) | BIT(4) | BIT(3), 0);
	rtl_set_bbreg(hw, DM_REG_RX_ANT_CTRL_11N,
		      BIT(8) | BIT(7) | BIT(6), 1);
	rtl_set_bbreg(hw, DM_REG_RX_ANT_CTRL_11N,
		      BIT(2) | BIT(1) | BIT(0), (ant_combination - 1));

	rtl_set_bbreg(hw, DM_REG_IGI_A_11N, BIT(7), 1);
}

static void rtl88e_dm_antenna_div_init(struct ieee80211_hw *hw)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));

	if (rtlefuse->antenna_div_type == CGCS_RX_HW_ANTDIV)
		rtl88e_dm_rx_hw_antena_div_init(hw);
	else if (rtlefuse->antenna_div_type == CG_TRX_HW_ANTDIV)
		rtl88e_dm_trx_hw_antenna_div_init(hw);
	else if (rtlefuse->antenna_div_type == CG_TRX_SMART_ANTDIV)
		rtl88e_dm_fast_training_init(hw);

}

void rtl88e_dm_set_tx_ant_by_tx_info(struct ieee80211_hw *hw,
				     u8 *pdesc, u32 mac_id)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));
	struct fast_ant_training *pfat_table = &rtldm->fat_table;
	__le32 *pdesc32 = (__le32 *)pdesc;

	if ((rtlefuse->antenna_div_type == CG_TRX_HW_ANTDIV) ||
	    (rtlefuse->antenna_div_type == CG_TRX_SMART_ANTDIV)) {
		set_tx_desc_antsel_a(pdesc32, pfat_table->antsel_a[mac_id]);
		set_tx_desc_antsel_b(pdesc32, pfat_table->antsel_b[mac_id]);
		set_tx_desc_antsel_c(pdesc32, pfat_table->antsel_c[mac_id]);
	}
}

void rtl88e_dm_ant_sel_statistics(struct ieee80211_hw *hw,
				  u8 antsel_tr_mux, u32 mac_id,
				  u32 rx_pwdb_all)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));
	struct fast_ant_training *pfat_table = &rtldm->fat_table;

	if (rtlefuse->antenna_div_type == CG_TRX_HW_ANTDIV) {
		if (antsel_tr_mux == MAIN_ANT_CG_TRX) {
			pfat_table->main_ant_sum[mac_id] += rx_pwdb_all;
			pfat_table->main_ant_cnt[mac_id]++;
		} else {
			pfat_table->aux_ant_sum[mac_id] += rx_pwdb_all;
			pfat_table->aux_ant_cnt[mac_id]++;
		}
	} else if (rtlefuse->antenna_div_type == CGCS_RX_HW_ANTDIV) {
		if (antsel_tr_mux == MAIN_ANT_CGCS_RX) {
			pfat_table->main_ant_sum[mac_id] += rx_pwdb_all;
			pfat_table->main_ant_cnt[mac_id]++;
		} else {
			pfat_table->aux_ant_sum[mac_id] += rx_pwdb_all;
			pfat_table->aux_ant_cnt[mac_id]++;
		}
	}
}

static void rtl88e_dm_hw_ant_div(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));
	struct rtl_sta_info *drv_priv;
	struct fast_ant_training *pfat_table = &rtldm->fat_table;
	struct dig_t *dm_dig = &rtlpriv->dm_digtable;
	u32 i, min_rssi = 0xff, ant_div_max_rssi = 0;
	u32 max_rssi = 0, local_min_rssi, local_max_rssi;
	u32 main_rssi, aux_rssi;
	u8 rx_idle_ant = 0, target_ant = 7;

	/*for sta its self*/
	i = 0;
	main_rssi = (pfat_table->main_ant_cnt[i] != 0) ?
		(pfat_table->main_ant_sum[i] / pfat_table->main_ant_cnt[i]) : 0;
	aux_rssi = (pfat_table->aux_ant_cnt[i] != 0) ?
		(pfat_table->aux_ant_sum[i] / pfat_table->aux_ant_cnt[i]) : 0;
	target_ant = (main_rssi == aux_rssi) ?
		pfat_table->rx_idle_ant : ((main_rssi >= aux_rssi) ?
		MAIN_ANT : AUX_ANT);
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"main_ant_sum %d main_ant_cnt %d\n",
		pfat_table->main_ant_sum[i],
		pfat_table->main_ant_cnt[i]);
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"aux_ant_sum %d aux_ant_cnt %d\n",
		pfat_table->aux_ant_sum[i], pfat_table->aux_ant_cnt[i]);
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "main_rssi %d aux_rssi%d\n",
		main_rssi, aux_rssi);
	local_max_rssi = (main_rssi > aux_rssi) ? main_rssi : aux_rssi;
	if ((local_max_rssi > ant_div_max_rssi) && (local_max_rssi < 40))
		ant_div_max_rssi = local_max_rssi;
	if (local_max_rssi > max_rssi)
		max_rssi = local_max_rssi;

	if ((pfat_table->rx_idle_ant == MAIN_ANT) && (main_rssi == 0))
		main_rssi = aux_rssi;
	else if ((pfat_table->rx_idle_ant == AUX_ANT) && (aux_rssi == 0))
		aux_rssi = main_rssi;

	local_min_rssi = (main_rssi > aux_rssi) ? aux_rssi : main_rssi;
	if (local_min_rssi < min_rssi) {
		min_rssi = local_min_rssi;
		rx_idle_ant = target_ant;
	}
	if (rtlefuse->antenna_div_type == CG_TRX_HW_ANTDIV)
		rtl88e_dm_update_tx_ant(hw, target_ant, i);

	if (rtlpriv->mac80211.opmode == NL80211_IFTYPE_AP ||
	    rtlpriv->mac80211.opmode == NL80211_IFTYPE_ADHOC) {
		spin_lock_bh(&rtlpriv->locks.entry_list_lock);
		list_for_each_entry(drv_priv, &rtlpriv->entry_list, list) {
			i++;
			main_rssi = (pfat_table->main_ant_cnt[i] != 0) ?
				(pfat_table->main_ant_sum[i] /
				pfat_table->main_ant_cnt[i]) : 0;
			aux_rssi = (pfat_table->aux_ant_cnt[i] != 0) ?
				(pfat_table->aux_ant_sum[i] /
				pfat_table->aux_ant_cnt[i]) : 0;
			target_ant = (main_rssi == aux_rssi) ?
				pfat_table->rx_idle_ant : ((main_rssi >=
				aux_rssi) ? MAIN_ANT : AUX_ANT);

			local_max_rssi = (main_rssi > aux_rssi) ?
					 main_rssi : aux_rssi;
			if ((local_max_rssi > ant_div_max_rssi) &&
			    (local_max_rssi < 40))
				ant_div_max_rssi = local_max_rssi;
			if (local_max_rssi > max_rssi)
				max_rssi = local_max_rssi;

			if ((pfat_table->rx_idle_ant == MAIN_ANT) &&
			    (main_rssi == 0))
				main_rssi = aux_rssi;
			else if ((pfat_table->rx_idle_ant == AUX_ANT) &&
				 (aux_rssi == 0))
				aux_rssi = main_rssi;

			local_min_rssi = (main_rssi > aux_rssi) ?
				aux_rssi : main_rssi;
			if (local_min_rssi < min_rssi) {
				min_rssi = local_min_rssi;
				rx_idle_ant = target_ant;
			}
			if (rtlefuse->antenna_div_type == CG_TRX_HW_ANTDIV)
				rtl88e_dm_update_tx_ant(hw, target_ant, i);
		}
		spin_unlock_bh(&rtlpriv->locks.entry_list_lock);
	}

	for (i = 0; i < ASSOCIATE_ENTRY_NUM; i++) {
		pfat_table->main_ant_sum[i] = 0;
		pfat_table->aux_ant_sum[i] = 0;
		pfat_table->main_ant_cnt[i] = 0;
		pfat_table->aux_ant_cnt[i] = 0;
	}

	rtl88e_dm_update_rx_idle_ant(hw, rx_idle_ant);

	dm_dig->antdiv_rssi_max = ant_div_max_rssi;
	dm_dig->rssi_max = max_rssi;
}

static void rtl88e_set_next_mac_address_target(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));
	struct rtl_sta_info *drv_priv;
	struct fast_ant_training *pfat_table = &rtldm->fat_table;
	u32 value32, i, j = 0;

	if (mac->link_state >= MAC80211_LINKED) {
		for (i = 0; i < ASSOCIATE_ENTRY_NUM; i++) {
			if ((pfat_table->train_idx + 1) == ASSOCIATE_ENTRY_NUM)
				pfat_table->train_idx = 0;
			else
				pfat_table->train_idx++;

			if (pfat_table->train_idx == 0) {
				value32 = (mac->mac_addr[5] << 8) |
					  mac->mac_addr[4];
				rtl_set_bbreg(hw, DM_REG_ANT_TRAIN_PARA2_11N,
					      MASKLWORD, value32);

				value32 = (mac->mac_addr[3] << 24) |
					  (mac->mac_addr[2] << 16) |
					  (mac->mac_addr[1] << 8) |
					  mac->mac_addr[0];
				rtl_set_bbreg(hw, DM_REG_ANT_TRAIN_PARA1_11N,
					      MASKDWORD, value32);
				break;
			}

			if (rtlpriv->mac80211.opmode !=
			    NL80211_IFTYPE_STATION) {
				spin_lock_bh(&rtlpriv->locks.entry_list_lock);
				list_for_each_entry(drv_priv,
						    &rtlpriv->entry_list, list) {
					j++;
					if (j != pfat_table->train_idx)
						continue;

					value32 = (drv_priv->mac_addr[5] << 8) |
						  drv_priv->mac_addr[4];
					rtl_set_bbreg(hw,
						      DM_REG_ANT_TRAIN_PARA2_11N,
						      MASKLWORD, value32);

					value32 = (drv_priv->mac_addr[3] << 24) |
						  (drv_priv->mac_addr[2] << 16) |
						  (drv_priv->mac_addr[1] << 8) |
						  drv_priv->mac_addr[0];
					rtl_set_bbreg(hw,
						      DM_REG_ANT_TRAIN_PARA1_11N,
						      MASKDWORD, value32);
					break;
				}
				spin_unlock_bh(&rtlpriv->locks.entry_list_lock);
				/*find entry, break*/
				if (j == pfat_table->train_idx)
					break;
			}
		}
	}
}

static void rtl88e_dm_fast_ant_training(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));
	struct fast_ant_training *pfat_table = &rtldm->fat_table;
	u32 i, max_rssi = 0;
	u8 target_ant = 2;
	bool bpkt_filter_match = false;

	if (pfat_table->fat_state == FAT_TRAINING_STATE) {
		for (i = 0; i < 7; i++) {
			if (pfat_table->ant_cnt[i] == 0) {
				pfat_table->ant_ave[i] = 0;
			} else {
				pfat_table->ant_ave[i] =
					pfat_table->ant_sum[i] /
					pfat_table->ant_cnt[i];
				bpkt_filter_match = true;
			}

			if (pfat_table->ant_ave[i] > max_rssi) {
				max_rssi = pfat_table->ant_ave[i];
				target_ant = (u8) i;
			}
		}

		if (!bpkt_filter_match) {
			rtl_set_bbreg(hw, DM_REG_TXAGC_A_1_MCS32_11N,
				      BIT(16), 0);
			rtl_set_bbreg(hw, DM_REG_IGI_A_11N, BIT(7), 0);
		} else {
			rtl_set_bbreg(hw, DM_REG_TXAGC_A_1_MCS32_11N,
				      BIT(16), 0);
			rtl_set_bbreg(hw, DM_REG_RX_ANT_CTRL_11N, BIT(8) |
				      BIT(7) | BIT(6), target_ant);
			rtl_set_bbreg(hw, DM_REG_TX_ANT_CTRL_11N,
				      BIT(21), 1);

			pfat_table->antsel_a[pfat_table->train_idx] =
				target_ant & BIT(0);
			pfat_table->antsel_b[pfat_table->train_idx] =
				(target_ant & BIT(1)) >> 1;
			pfat_table->antsel_c[pfat_table->train_idx] =
				(target_ant & BIT(2)) >> 2;

			if (target_ant == 0)
				rtl_set_bbreg(hw, DM_REG_IGI_A_11N, BIT(7), 0);
		}

		for (i = 0; i < 7; i++) {
			pfat_table->ant_sum[i] = 0;
			pfat_table->ant_cnt[i] = 0;
		}

		pfat_table->fat_state = FAT_NORMAL_STATE;
		return;
	}

	if (pfat_table->fat_state == FAT_NORMAL_STATE) {
		rtl88e_set_next_mac_address_target(hw);

		pfat_table->fat_state = FAT_TRAINING_STATE;
		rtl_set_bbreg(hw, DM_REG_TXAGC_A_1_MCS32_11N, BIT(16), 1);
		rtl_set_bbreg(hw, DM_REG_IGI_A_11N, BIT(7), 1);

		mod_timer(&rtlpriv->works.fast_antenna_training_timer,
			  jiffies + MSECS(RTL_WATCH_DOG_TIME));
	}
}

void rtl88e_dm_fast_antenna_training_callback(struct timer_list *t)
{
	struct rtl_priv *rtlpriv =
		from_timer(rtlpriv, t, works.fast_antenna_training_timer);
	struct ieee80211_hw *hw = rtlpriv->hw;

	rtl88e_dm_fast_ant_training(hw);
}

static void rtl88e_dm_antenna_diversity(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));
	struct fast_ant_training *pfat_table = &rtldm->fat_table;

	if (mac->link_state < MAC80211_LINKED) {
		rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD, "No Link\n");
		if (pfat_table->becomelinked) {
			rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
				"need to turn off HW AntDiv\n");
			rtl_set_bbreg(hw, DM_REG_IGI_A_11N, BIT(7), 0);
			rtl_set_bbreg(hw, DM_REG_CCK_ANTDIV_PARA1_11N,
				      BIT(15), 0);
			if (rtlefuse->antenna_div_type == CG_TRX_HW_ANTDIV)
				rtl_set_bbreg(hw, DM_REG_TX_ANT_CTRL_11N,
					      BIT(21), 0);
			pfat_table->becomelinked =
				(mac->link_state == MAC80211_LINKED) ?
				true : false;
		}
		return;
	} else {
		if (!pfat_table->becomelinked) {
			rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
				"Need to turn on HW AntDiv\n");
			rtl_set_bbreg(hw, DM_REG_IGI_A_11N, BIT(7), 1);
			rtl_set_bbreg(hw, DM_REG_CCK_ANTDIV_PARA1_11N,
				      BIT(15), 1);
			if (rtlefuse->antenna_div_type == CG_TRX_HW_ANTDIV)
				rtl_set_bbreg(hw, DM_REG_TX_ANT_CTRL_11N,
					      BIT(21), 1);
			pfat_table->becomelinked =
				(mac->link_state >= MAC80211_LINKED) ?
				true : false;
		}
	}

	if ((rtlefuse->antenna_div_type == CG_TRX_HW_ANTDIV) ||
	    (rtlefuse->antenna_div_type == CGCS_RX_HW_ANTDIV))
		rtl88e_dm_hw_ant_div(hw);
	else if (rtlefuse->antenna_div_type == CG_TRX_SMART_ANTDIV)
		rtl88e_dm_fast_ant_training(hw);
}

void rtl88e_dm_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 cur_igvalue = rtl_get_bbreg(hw, ROFDM0_XAAGCCORE1, 0x7f);

	rtlpriv->dm.dm_type = DM_TYPE_BYDRIVER;
	rtl_dm_diginit(hw, cur_igvalue);
	rtl88e_dm_init_dynamic_txpower(hw);
	rtl88e_dm_init_edca_turbo(hw);
	rtl88e_dm_init_rate_adaptive_mask(hw);
	rtl88e_dm_init_txpower_tracking(hw);
	rtl92c_dm_init_dynamic_bb_powersaving(hw);
	rtl88e_dm_antenna_div_init(hw);
}

void rtl88e_dm_watchdog(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	bool fw_current_inpsmode = false;
	bool fw_ps_awake = true;

	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
				      (u8 *)(&fw_current_inpsmode));
	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_FWLPS_RF_ON,
				      (u8 *)(&fw_ps_awake));
	if (ppsc->p2p_ps_info.p2p_ps_mode)
		fw_ps_awake = false;

	spin_lock(&rtlpriv->locks.rf_ps_lock);
	if ((ppsc->rfpwr_state == ERFON) &&
	    ((!fw_current_inpsmode) && fw_ps_awake) &&
	    (!ppsc->rfchange_inprogress)) {
		rtl88e_dm_pwdb_monitor(hw);
		rtl88e_dm_dig(hw);
		rtl88e_dm_false_alarm_counter_statistics(hw);
		rtl92c_dm_dynamic_txpower(hw);
		rtl88e_dm_check_txpower_tracking(hw);
		rtl88e_dm_refresh_rate_adaptive_mask(hw);
		rtl88e_dm_check_edca_turbo(hw);
		rtl88e_dm_antenna_diversity(hw);
	}
	spin_unlock(&rtlpriv->locks.rf_ps_lock);
}
