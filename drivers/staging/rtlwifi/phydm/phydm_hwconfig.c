// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

/* ************************************************************
 * include files
 * *************************************************************/

#include "mp_precomp.h"
#include "phydm_precomp.h"

#define READ_AND_CONFIG_MP(ic, txt) (odm_read_and_config_mp_##ic##txt(dm))
#define READ_AND_CONFIG_TC(ic, txt) (odm_read_and_config_tc_##ic##txt(dm))

#define READ_AND_CONFIG READ_AND_CONFIG_MP

#define READ_FIRMWARE_MP(ic, txt)                                              \
	(odm_read_firmware_mp_##ic##txt(dm, p_firmware, size))
#define READ_FIRMWARE_TC(ic, txt)                                              \
	(odm_read_firmware_tc_##ic##txt(dm, p_firmware, size))

#define READ_FIRMWARE READ_FIRMWARE_MP

#define GET_VERSION_MP(ic, txt) (odm_get_version_mp_##ic##txt())
#define GET_VERSION_TC(ic, txt) (odm_get_version_tc_##ic##txt())

#define GET_VERSION(ic, txt) GET_VERSION_MP(ic, txt)

static u32 phydm_process_rssi_pwdb(struct phy_dm_struct *dm,
				   struct rtl_sta_info *entry,
				   struct dm_per_pkt_info *pktinfo,
				   u32 undecorated_smoothed_ofdm,
				   u32 undecorated_smoothed_cck)
{
	u32 weighting = 0, undecorated_smoothed_pwdb;
	/* 2011.07.28 LukeLee: modified to prevent unstable CCK RSSI */

	if (entry->rssi_stat.ofdm_pkt == 64) {
		/* speed up when all packets are OFDM */
		undecorated_smoothed_pwdb = undecorated_smoothed_ofdm;
		ODM_RT_TRACE(dm, ODM_COMP_RSSI_MONITOR,
			     "PWDB_0[%d] = (( %d ))\n", pktinfo->station_id,
			     undecorated_smoothed_cck);
	} else {
		if (entry->rssi_stat.valid_bit < 64)
			entry->rssi_stat.valid_bit++;

		if (entry->rssi_stat.valid_bit == 64) {
			weighting = ((entry->rssi_stat.ofdm_pkt) > 4) ?
					    64 :
					    (entry->rssi_stat.ofdm_pkt << 4);
			undecorated_smoothed_pwdb =
				(weighting * undecorated_smoothed_ofdm +
				 (64 - weighting) * undecorated_smoothed_cck) >>
				6;
			ODM_RT_TRACE(dm, ODM_COMP_RSSI_MONITOR,
				     "PWDB_1[%d] = (( %d )), W = (( %d ))\n",
				     pktinfo->station_id,
				     undecorated_smoothed_cck, weighting);
		} else {
			if (entry->rssi_stat.valid_bit != 0)
				undecorated_smoothed_pwdb =
					(entry->rssi_stat.ofdm_pkt *
						 undecorated_smoothed_ofdm +
					 (entry->rssi_stat.valid_bit -
					  entry->rssi_stat.ofdm_pkt) *
						 undecorated_smoothed_cck) /
					entry->rssi_stat.valid_bit;
			else
				undecorated_smoothed_pwdb = 0;

			ODM_RT_TRACE(
				dm, ODM_COMP_RSSI_MONITOR,
				"PWDB_2[%d] = (( %d )), ofdm_pkt = (( %d )), Valid_Bit = (( %d ))\n",
				pktinfo->station_id, undecorated_smoothed_cck,
				entry->rssi_stat.ofdm_pkt,
				entry->rssi_stat.valid_bit);
		}
	}

	return undecorated_smoothed_pwdb;
}

static u32 phydm_process_rssi_cck(struct phy_dm_struct *dm,
				  struct dm_phy_status_info *phy_info,
				  struct rtl_sta_info *entry,
				  u32 undecorated_smoothed_cck)
{
	u32 rssi_ave;
	u8 i;

	rssi_ave = phy_info->rx_pwdb_all;
	dm->rssi_a = (u8)phy_info->rx_pwdb_all;
	dm->rssi_b = 0xFF;
	dm->rssi_c = 0xFF;
	dm->rssi_d = 0xFF;

	if (entry->rssi_stat.cck_pkt <= 63)
		entry->rssi_stat.cck_pkt++;

	/* 1 Process CCK RSSI */
	if (undecorated_smoothed_cck <= 0) { /* initialize */
		undecorated_smoothed_cck = phy_info->rx_pwdb_all;
		entry->rssi_stat.cck_sum_power =
			(u16)phy_info->rx_pwdb_all; /*reset*/
		entry->rssi_stat.cck_pkt = 1; /*reset*/
		ODM_RT_TRACE(dm, ODM_COMP_RSSI_MONITOR, "CCK_INIT: (( %d ))\n",
			     undecorated_smoothed_cck);
	} else if (entry->rssi_stat.cck_pkt <= CCK_RSSI_INIT_COUNT) {
		entry->rssi_stat.cck_sum_power =
			entry->rssi_stat.cck_sum_power +
			(u16)phy_info->rx_pwdb_all;
		undecorated_smoothed_cck = entry->rssi_stat.cck_sum_power /
					   entry->rssi_stat.cck_pkt;

		ODM_RT_TRACE(
			dm, ODM_COMP_RSSI_MONITOR,
			"CCK_0: (( %d )), SumPow = (( %d )), cck_pkt = (( %d ))\n",
			undecorated_smoothed_cck,
			entry->rssi_stat.cck_sum_power,
			entry->rssi_stat.cck_pkt);
	} else {
		if (phy_info->rx_pwdb_all > (u32)undecorated_smoothed_cck) {
			undecorated_smoothed_cck =
				(((undecorated_smoothed_cck) *
				  (RX_SMOOTH_FACTOR - 1)) +
				 (phy_info->rx_pwdb_all)) /
				(RX_SMOOTH_FACTOR);
			undecorated_smoothed_cck = undecorated_smoothed_cck + 1;
			ODM_RT_TRACE(dm, ODM_COMP_RSSI_MONITOR,
				     "CCK_1: (( %d ))\n",
				     undecorated_smoothed_cck);
		} else {
			undecorated_smoothed_cck =
				(((undecorated_smoothed_cck) *
				  (RX_SMOOTH_FACTOR - 1)) +
				 (phy_info->rx_pwdb_all)) /
				(RX_SMOOTH_FACTOR);
			ODM_RT_TRACE(dm, ODM_COMP_RSSI_MONITOR,
				     "CCK_2: (( %d ))\n",
				     undecorated_smoothed_cck);
		}
	}

	i = 63;
	entry->rssi_stat.ofdm_pkt -=
		(u8)((entry->rssi_stat.packet_map >> i) & BIT(0));
	entry->rssi_stat.packet_map = entry->rssi_stat.packet_map << 1;
	return undecorated_smoothed_cck;
}

static u32 phydm_process_rssi_ofdm(struct phy_dm_struct *dm,
				   struct dm_phy_status_info *phy_info,
				   struct rtl_sta_info *entry,
				   u32 undecorated_smoothed_ofdm)
{
	u32 rssi_ave;
	u8 rssi_max, rssi_min, i;

	if (dm->support_ic_type & (ODM_RTL8814A | ODM_RTL8822B)) {
		u8 rx_count = 0;
		u32 rssi_linear = 0;

		if (dm->rx_ant_status & ODM_RF_A) {
			dm->rssi_a = phy_info->rx_mimo_signal_strength
					     [ODM_RF_PATH_A];
			rx_count++;
			rssi_linear += odm_convert_to_linear(
				phy_info->rx_mimo_signal_strength
					[ODM_RF_PATH_A]);
		} else {
			dm->rssi_a = 0;
		}

		if (dm->rx_ant_status & ODM_RF_B) {
			dm->rssi_b = phy_info->rx_mimo_signal_strength
					     [ODM_RF_PATH_B];
			rx_count++;
			rssi_linear += odm_convert_to_linear(
				phy_info->rx_mimo_signal_strength
					[ODM_RF_PATH_B]);
		} else {
			dm->rssi_b = 0;
		}

		if (dm->rx_ant_status & ODM_RF_C) {
			dm->rssi_c = phy_info->rx_mimo_signal_strength
					     [ODM_RF_PATH_C];
			rx_count++;
			rssi_linear += odm_convert_to_linear(
				phy_info->rx_mimo_signal_strength
					[ODM_RF_PATH_C]);
		} else {
			dm->rssi_c = 0;
		}

		if (dm->rx_ant_status & ODM_RF_D) {
			dm->rssi_d = phy_info->rx_mimo_signal_strength
					     [ODM_RF_PATH_D];
			rx_count++;
			rssi_linear += odm_convert_to_linear(
				phy_info->rx_mimo_signal_strength
					[ODM_RF_PATH_D]);
		} else {
			dm->rssi_d = 0;
		}

		/* Calculate average RSSI */
		switch (rx_count) {
		case 2:
			rssi_linear = (rssi_linear >> 1);
			break;
		case 3:
			/* rssi_linear/3 ~ rssi_linear*11/32 */
			rssi_linear = ((rssi_linear) + (rssi_linear << 1) +
				       (rssi_linear << 3)) >>
				      5;
			break;
		case 4:
			rssi_linear = (rssi_linear >> 2);
			break;
		}

		rssi_ave = odm_convert_to_db(rssi_linear);
	} else {
		if (phy_info->rx_mimo_signal_strength[ODM_RF_PATH_B] == 0) {
			rssi_ave = phy_info->rx_mimo_signal_strength
					   [ODM_RF_PATH_A];
			dm->rssi_a = phy_info->rx_mimo_signal_strength
					     [ODM_RF_PATH_A];
			dm->rssi_b = 0;
		} else {
			dm->rssi_a = phy_info->rx_mimo_signal_strength
					     [ODM_RF_PATH_A];
			dm->rssi_b = phy_info->rx_mimo_signal_strength
					     [ODM_RF_PATH_B];

			if (phy_info->rx_mimo_signal_strength[ODM_RF_PATH_A] >
			    phy_info->rx_mimo_signal_strength[ODM_RF_PATH_B]) {
				rssi_max = phy_info->rx_mimo_signal_strength
						   [ODM_RF_PATH_A];
				rssi_min = phy_info->rx_mimo_signal_strength
						   [ODM_RF_PATH_B];
			} else {
				rssi_max = phy_info->rx_mimo_signal_strength
						   [ODM_RF_PATH_B];
				rssi_min = phy_info->rx_mimo_signal_strength
						   [ODM_RF_PATH_A];
			}
			if ((rssi_max - rssi_min) < 3)
				rssi_ave = rssi_max;
			else if ((rssi_max - rssi_min) < 6)
				rssi_ave = rssi_max - 1;
			else if ((rssi_max - rssi_min) < 10)
				rssi_ave = rssi_max - 2;
			else
				rssi_ave = rssi_max - 3;
		}
	}

	/* 1 Process OFDM RSSI */
	if (undecorated_smoothed_ofdm <= 0) { /* initialize */
		undecorated_smoothed_ofdm = phy_info->rx_pwdb_all;
		ODM_RT_TRACE(dm, ODM_COMP_RSSI_MONITOR, "OFDM_INIT: (( %d ))\n",
			     undecorated_smoothed_ofdm);
	} else {
		if (phy_info->rx_pwdb_all > (u32)undecorated_smoothed_ofdm) {
			undecorated_smoothed_ofdm =
				(((undecorated_smoothed_ofdm) *
				  (RX_SMOOTH_FACTOR - 1)) +
				 (rssi_ave)) /
				(RX_SMOOTH_FACTOR);
			undecorated_smoothed_ofdm =
				undecorated_smoothed_ofdm + 1;
			ODM_RT_TRACE(dm, ODM_COMP_RSSI_MONITOR,
				     "OFDM_1: (( %d ))\n",
				     undecorated_smoothed_ofdm);
		} else {
			undecorated_smoothed_ofdm =
				(((undecorated_smoothed_ofdm) *
				  (RX_SMOOTH_FACTOR - 1)) +
				 (rssi_ave)) /
				(RX_SMOOTH_FACTOR);
			ODM_RT_TRACE(dm, ODM_COMP_RSSI_MONITOR,
				     "OFDM_2: (( %d ))\n",
				     undecorated_smoothed_ofdm);
		}
	}

	if (entry->rssi_stat.ofdm_pkt != 64) {
		i = 63;
		entry->rssi_stat.ofdm_pkt -=
			(u8)(((entry->rssi_stat.packet_map >> i) & BIT(0)) - 1);
	}

	entry->rssi_stat.packet_map =
		(entry->rssi_stat.packet_map << 1) | BIT(0);
	return undecorated_smoothed_ofdm;
}

static u8 odm_evm_db_to_percentage(s8);
static u8 odm_evm_dbm_jaguar_series(s8);

static inline u32 phydm_get_rssi_average(struct phy_dm_struct *dm,
					 struct dm_phy_status_info *phy_info)
{
	u8 rssi_max = 0, rssi_min = 0;

	dm->rssi_a = phy_info->rx_mimo_signal_strength[ODM_RF_PATH_A];
	dm->rssi_b = phy_info->rx_mimo_signal_strength[ODM_RF_PATH_B];

	if (phy_info->rx_mimo_signal_strength[ODM_RF_PATH_A] >
	    phy_info->rx_mimo_signal_strength[ODM_RF_PATH_B]) {
		rssi_max = phy_info->rx_mimo_signal_strength[ODM_RF_PATH_A];
		rssi_min = phy_info->rx_mimo_signal_strength[ODM_RF_PATH_B];
	} else {
		rssi_max = phy_info->rx_mimo_signal_strength[ODM_RF_PATH_B];
		rssi_min = phy_info->rx_mimo_signal_strength[ODM_RF_PATH_A];
	}
	if ((rssi_max - rssi_min) < 3)
		return rssi_max;
	else if ((rssi_max - rssi_min) < 6)
		return rssi_max - 1;
	else if ((rssi_max - rssi_min) < 10)
		return rssi_max - 2;
	else
		return rssi_max - 3;
}

static inline u8 phydm_get_evm_dbm(u8 i, u8 EVM,
				   struct phy_status_rpt_8812 *phy_sta_rpt,
				   struct dm_phy_status_info *phy_info)
{
	if (i < ODM_RF_PATH_C)
		return odm_evm_dbm_jaguar_series(phy_sta_rpt->rxevm[i]);
	else
		return odm_evm_dbm_jaguar_series(phy_sta_rpt->rxevm_cd[i - 2]);
	/*RT_DISP(FRX, RX_PHY_SQ, ("RXRATE=%x RXEVM=%x EVM=%s%d\n",*/
	/*pktinfo->data_rate, phy_sta_rpt->rxevm[i], "%", EVM));*/
}

static inline u8 phydm_get_odm_evm(u8 i, struct dm_per_pkt_info *pktinfo,
				   struct phy_status_rpt_8812 *phy_sta_rpt)
{
	u8 evm = 0;

	if (pktinfo->data_rate >= ODM_RATE6M &&
	    pktinfo->data_rate <= ODM_RATE54M) {
		if (i == ODM_RF_PATH_A) {
			evm = odm_evm_db_to_percentage(
				(phy_sta_rpt->sigevm)); /*dbm*/
			evm += 20;
			if (evm > 100)
				evm = 100;
		}
	} else {
		if (i < ODM_RF_PATH_C) {
			if (phy_sta_rpt->rxevm[i] == -128)
				phy_sta_rpt->rxevm[i] = -25;
			evm = odm_evm_db_to_percentage(
				(phy_sta_rpt->rxevm[i])); /*dbm*/
		} else {
			if (phy_sta_rpt->rxevm_cd[i - 2] == -128)
				phy_sta_rpt->rxevm_cd[i - 2] = -25;
			evm = odm_evm_db_to_percentage(
				(phy_sta_rpt->rxevm_cd[i - 2])); /*dbm*/
		}
	}

	return evm;
}

static inline s8 phydm_get_rx_pwr(u8 LNA_idx, u8 VGA_idx, u8 cck_highpwr)
{
	switch (LNA_idx) {
	case 7:
		if (VGA_idx <= 27)
			return -100 + 2 * (27 - VGA_idx); /*VGA_idx = 27~2*/
		else
			return -100;
		break;
	case 6:
		return -48 + 2 * (2 - VGA_idx); /*VGA_idx = 2~0*/
	case 5:
		return -42 + 2 * (7 - VGA_idx); /*VGA_idx = 7~5*/
	case 4:
		return -36 + 2 * (7 - VGA_idx); /*VGA_idx = 7~4*/
	case 3:
		return -24 + 2 * (7 - VGA_idx); /*VGA_idx = 7~0*/
	case 2:
		if (cck_highpwr)
			return -12 + 2 * (5 - VGA_idx); /*VGA_idx = 5~0*/
		else
			return -6 + 2 * (5 - VGA_idx);
		break;
	case 1:
		return 8 - 2 * VGA_idx;
	case 0:
		return 14 - 2 * VGA_idx;
	default:
		break;
	}
	return 0;
}

static inline u8 phydm_adjust_pwdb(u8 cck_highpwr, u8 pwdb_all)
{
	if (!cck_highpwr) {
		if (pwdb_all >= 80)
			return ((pwdb_all - 80) << 1) + ((pwdb_all - 80) >> 1) +
			       80;
		else if ((pwdb_all <= 78) && (pwdb_all >= 20))
			return pwdb_all + 3;
		if (pwdb_all > 100)
			return 100;
	}
	return pwdb_all;
}

static inline u8
phydm_get_signal_quality_8812(struct dm_phy_status_info *phy_info,
			      struct phy_dm_struct *dm,
			      struct phy_status_rpt_8812 *phy_sta_rpt)
{
	u8 sq_rpt;

	if (phy_info->rx_pwdb_all > 40 && !dm->is_in_hct_test)
		return 100;

	sq_rpt = phy_sta_rpt->pwdb_all;

	if (sq_rpt > 64)
		return 0;
	else if (sq_rpt < 20)
		return 100;
	else
		return ((64 - sq_rpt) * 100) / 44;
}

static inline u8
phydm_get_signal_quality_8192(struct dm_phy_status_info *phy_info,
			      struct phy_dm_struct *dm,
			      struct phy_status_rpt_8192cd *phy_sta_rpt)
{
	u8 sq_rpt;

	if (phy_info->rx_pwdb_all > 40 && !dm->is_in_hct_test)
		return 100;

	sq_rpt = phy_sta_rpt->cck_sig_qual_ofdm_pwdb_all;

	if (sq_rpt > 64)
		return 0;
	else if (sq_rpt < 20)
		return 100;
	else
		return ((64 - sq_rpt) * 100) / 44;
}

static u8 odm_query_rx_pwr_percentage(s8 ant_power)
{
	if ((ant_power <= -100) || (ant_power >= 20))
		return 0;
	else if (ant_power >= 0)
		return 100;
	else
		return 100 + ant_power;
}

static u8 odm_evm_db_to_percentage(s8 value)
{
	/* -33dB~0dB to 0%~99% */
	s8 ret_val;

	ret_val = value;
	ret_val /= 2;

	if (ret_val >= 0)
		ret_val = 0;

	if (ret_val <= -33)
		ret_val = -33;

	ret_val = 0 - ret_val;
	ret_val *= 3;

	if (ret_val == 99)
		ret_val = 100;

	return (u8)ret_val;
}

static u8 odm_evm_dbm_jaguar_series(s8 value)
{
	s8 ret_val = value;

	/* -33dB~0dB to 33dB ~ 0dB */
	if (ret_val == -128)
		ret_val = 127;
	else if (ret_val < 0)
		ret_val = 0 - ret_val;

	ret_val = ret_val >> 1;
	return (u8)ret_val;
}

static s16 odm_cfo(s8 value)
{
	s16 ret_val;

	if (value < 0) {
		ret_val = 0 - value;
		ret_val = (ret_val << 1) + (ret_val >> 1); /* *2.5~=312.5/2^7 */
		ret_val =
			ret_val | BIT(12); /* set bit12 as 1 for negative cfo */
	} else {
		ret_val = value;
		ret_val = (ret_val << 1) + (ret_val >> 1); /* *2.5~=312.5/2^7 */
	}
	return ret_val;
}

static u8 phydm_rate_to_num_ss(struct phy_dm_struct *dm, u8 data_rate)
{
	u8 num_ss = 1;

	if (data_rate <= ODM_RATE54M)
		num_ss = 1;
	else if (data_rate <= ODM_RATEMCS31)
		num_ss = ((data_rate - ODM_RATEMCS0) >> 3) + 1;
	else if (data_rate <= ODM_RATEVHTSS1MCS9)
		num_ss = 1;
	else if (data_rate <= ODM_RATEVHTSS2MCS9)
		num_ss = 2;
	else if (data_rate <= ODM_RATEVHTSS3MCS9)
		num_ss = 3;
	else if (data_rate <= ODM_RATEVHTSS4MCS9)
		num_ss = 4;

	return num_ss;
}

static void odm_rx_phy_status92c_series_parsing(
	struct phy_dm_struct *dm, struct dm_phy_status_info *phy_info,
	u8 *phy_status, struct dm_per_pkt_info *pktinfo)
{
	u8 i, max_spatial_stream;
	s8 rx_pwr[4], rx_pwr_all = 0;
	u8 EVM, pwdb_all = 0, pwdb_all_bt;
	u8 RSSI, total_rssi = 0;
	bool is_cck_rate = false;
	u8 rf_rx_num = 0;
	u8 LNA_idx = 0;
	u8 VGA_idx = 0;
	u8 cck_agc_rpt;
	u8 num_ss;
	struct phy_status_rpt_8192cd *phy_sta_rpt =
		(struct phy_status_rpt_8192cd *)phy_status;

	is_cck_rate = (pktinfo->data_rate <= ODM_RATE11M) ? true : false;

	if (pktinfo->is_to_self)
		dm->curr_station_id = pktinfo->station_id;

	phy_info->rx_mimo_signal_quality[ODM_RF_PATH_A] = -1;
	phy_info->rx_mimo_signal_quality[ODM_RF_PATH_B] = -1;

	if (is_cck_rate) {
		dm->phy_dbg_info.num_qry_phy_status_cck++;
		cck_agc_rpt = phy_sta_rpt->cck_agc_rpt_ofdm_cfosho_a;

		if (dm->support_ic_type & (ODM_RTL8703B)) {
		} else { /*3 bit LNA*/

			LNA_idx = ((cck_agc_rpt & 0xE0) >> 5);
			VGA_idx = (cck_agc_rpt & 0x1F);
		}

		ODM_RT_TRACE(
			dm, ODM_COMP_RSSI_MONITOR,
			"ext_lna_gain (( %d )), LNA_idx: (( 0x%x )), VGA_idx: (( 0x%x )), rx_pwr_all: (( %d ))\n",
			dm->ext_lna_gain, LNA_idx, VGA_idx, rx_pwr_all);

		if (dm->board_type & ODM_BOARD_EXT_LNA)
			rx_pwr_all -= dm->ext_lna_gain;

		pwdb_all = odm_query_rx_pwr_percentage(rx_pwr_all);

		if (pktinfo->is_to_self) {
			dm->cck_lna_idx = LNA_idx;
			dm->cck_vga_idx = VGA_idx;
		}
		phy_info->rx_pwdb_all = pwdb_all;

		phy_info->bt_rx_rssi_percentage = pwdb_all;
		phy_info->recv_signal_power = rx_pwr_all;
		/* (3) Get Signal Quality (EVM) */
		{
			u8 sq;

			sq = phydm_get_signal_quality_8192(phy_info, dm,
							   phy_sta_rpt);
			phy_info->signal_quality = sq;
			phy_info->rx_mimo_signal_quality[ODM_RF_PATH_A] = sq;
			phy_info->rx_mimo_signal_quality[ODM_RF_PATH_B] = -1;
		}

		for (i = ODM_RF_PATH_A; i < ODM_RF_PATH_MAX; i++) {
			if (i == 0)
				phy_info->rx_mimo_signal_strength[0] = pwdb_all;
			else
				phy_info->rx_mimo_signal_strength[1] = 0;
		}
	} else { /* 2 is OFDM rate */
		dm->phy_dbg_info.num_qry_phy_status_ofdm++;

		/*  */
		/* (1)Get RSSI for HT rate */
		/*  */

		for (i = ODM_RF_PATH_A; i < ODM_RF_PATH_MAX; i++) {
			/* 2008/01/30 MH we will judge RF RX path now. */
			if (dm->rf_path_rx_enable & BIT(i))
				rf_rx_num++;
			/* else */
			/* continue; */

			rx_pwr[i] =
				((phy_sta_rpt->path_agc[i].gain & 0x3F) * 2) -
				110;

			if (pktinfo->is_to_self) {
				dm->ofdm_agc_idx[i] =
					(phy_sta_rpt->path_agc[i].gain & 0x3F);
				/**/
			}

			phy_info->rx_pwr[i] = rx_pwr[i];

			/* Translate DBM to percentage. */
			RSSI = odm_query_rx_pwr_percentage(rx_pwr[i]);
			total_rssi += RSSI;

			phy_info->rx_mimo_signal_strength[i] = (u8)RSSI;

			/* Get Rx snr value in DB */
			dm->phy_dbg_info.rx_snr_db[i] =
				(s32)(phy_sta_rpt->path_rxsnr[i] / 2);
			phy_info->rx_snr[i] = dm->phy_dbg_info.rx_snr_db[i];

			/* Record Signal Strength for next packet */
			/* if(pktinfo->is_packet_match_bssid) */
			{
			}
		}

		/*  */
		/* (2)PWDB, Average PWDB calcuated by hardware (for RA) */
		/*  */
		rx_pwr_all = (((phy_sta_rpt->cck_sig_qual_ofdm_pwdb_all) >> 1) &
			      0x7f) -
			     110;

		pwdb_all = odm_query_rx_pwr_percentage(rx_pwr_all);
		pwdb_all_bt = pwdb_all;

		phy_info->rx_pwdb_all = pwdb_all;
		phy_info->bt_rx_rssi_percentage = pwdb_all_bt;
		phy_info->rx_power = rx_pwr_all;
		phy_info->recv_signal_power = rx_pwr_all;

		if ((dm->support_platform == ODM_WIN) && (dm->patch_id == 19)) {
			/* do nothing */
		} else if ((dm->support_platform == ODM_WIN) &&
			   (dm->patch_id == 25)) {
			/* do nothing */
		} else { /* mgnt_info->customer_id != RT_CID_819X_LENOVO */
			/*  */
			/* (3)EVM of HT rate */
			/*  */
			if (pktinfo->data_rate >= ODM_RATEMCS8 &&
			    pktinfo->data_rate <= ODM_RATEMCS15) {
				/* both spatial stream make sense */
				max_spatial_stream = 2;
			} else {
				/* only spatial stream 1 makes sense */
				max_spatial_stream = 1;
			}

			for (i = 0; i < max_spatial_stream; i++) {
				/*Don't use shift operation like "rx_evmX >>= 1"
				 *because the compilor of free build environment
				 *fill most significant bit to "zero" when doing
				 *shifting operation which may change a negative
				 *value to positive one, then the dbm value
				 *(which is supposed to be negative)  is not
				 *correct anymore.
				 */
				EVM = odm_evm_db_to_percentage(
					(phy_sta_rpt
						 ->stream_rxevm[i])); /* dbm */

				/* Fill value in RFD, Get the first spatial
				 * stream only
				 */
				if (i == ODM_RF_PATH_A)
					phy_info->signal_quality =
						(u8)(EVM & 0xff);
				phy_info->rx_mimo_signal_quality[i] =
					(u8)(EVM & 0xff);
			}
		}

		num_ss = phydm_rate_to_num_ss(dm, pktinfo->data_rate);
		odm_parsing_cfo(dm, pktinfo, phy_sta_rpt->path_cfotail, num_ss);
	}
	/* UI BSS List signal strength(in percentage), make it good looking,
	 * from 0~100.
	 */
	/* It is assigned to the BSS List in GetValueFromBeaconOrProbeRsp(). */
	if (is_cck_rate)
		phy_info->signal_strength = pwdb_all;
	else if (rf_rx_num != 0)
		phy_info->signal_strength = (total_rssi /= rf_rx_num);

	/* For 92C/92D HW (Hybrid) Antenna Diversity */
}

static void
odm_rx_phy_bw_jaguar_series_parsing(struct dm_phy_status_info *phy_info,
				    struct dm_per_pkt_info *pktinfo,
				    struct phy_status_rpt_8812 *phy_sta_rpt)
{
	if (pktinfo->data_rate <= ODM_RATE54M) {
		switch (phy_sta_rpt->r_RFMOD) {
		case 1:
			if (phy_sta_rpt->sub_chnl == 0)
				phy_info->band_width = 1;
			else
				phy_info->band_width = 0;
			break;

		case 2:
			if (phy_sta_rpt->sub_chnl == 0)
				phy_info->band_width = 2;
			else if (phy_sta_rpt->sub_chnl == 9 ||
				 phy_sta_rpt->sub_chnl == 10)
				phy_info->band_width = 1;
			else
				phy_info->band_width = 0;
			break;

		default:
		case 0:
			phy_info->band_width = 0;
			break;
		}
	}
}

static void odm_rx_phy_status_jaguar_series_parsing(
	struct phy_dm_struct *dm, struct dm_phy_status_info *phy_info,
	u8 *phy_status, struct dm_per_pkt_info *pktinfo)
{
	u8 i, max_spatial_stream;
	s8 rx_pwr[4], rx_pwr_all = 0;
	u8 EVM = 0, evm_dbm, pwdb_all = 0, pwdb_all_bt;
	u8 RSSI, avg_rssi = 0, best_rssi = 0, second_rssi = 0;
	u8 is_cck_rate = 0;
	u8 rf_rx_num = 0;
	u8 cck_highpwr = 0;
	u8 LNA_idx, VGA_idx;
	struct phy_status_rpt_8812 *phy_sta_rpt =
		(struct phy_status_rpt_8812 *)phy_status;
	struct fast_antenna_training *fat_tab = &dm->dm_fat_table;
	u8 num_ss;

	odm_rx_phy_bw_jaguar_series_parsing(phy_info, pktinfo, phy_sta_rpt);

	if (pktinfo->data_rate <= ODM_RATE11M)
		is_cck_rate = true;
	else
		is_cck_rate = false;

	if (pktinfo->is_to_self)
		dm->curr_station_id = pktinfo->station_id;
	else
		dm->curr_station_id = 0xff;

	phy_info->rx_mimo_signal_quality[ODM_RF_PATH_A] = -1;
	phy_info->rx_mimo_signal_quality[ODM_RF_PATH_B] = -1;
	phy_info->rx_mimo_signal_quality[ODM_RF_PATH_C] = -1;
	phy_info->rx_mimo_signal_quality[ODM_RF_PATH_D] = -1;

	if (is_cck_rate) {
		u8 cck_agc_rpt;

		dm->phy_dbg_info.num_qry_phy_status_cck++;

		/*(1)Hardware does not provide RSSI for CCK*/
		/*(2)PWDB, Average PWDB calculated by hardware (for RA)*/

		cck_highpwr = dm->is_cck_high_power;

		cck_agc_rpt = phy_sta_rpt->cfosho[0];
		LNA_idx = ((cck_agc_rpt & 0xE0) >> 5);
		VGA_idx = (cck_agc_rpt & 0x1F);

		if (dm->support_ic_type == ODM_RTL8812) {
			rx_pwr_all =
				phydm_get_rx_pwr(LNA_idx, VGA_idx, cck_highpwr);
			rx_pwr_all += 6;
			pwdb_all = odm_query_rx_pwr_percentage(rx_pwr_all);
			pwdb_all = phydm_adjust_pwdb(cck_highpwr, pwdb_all);

		} else if (dm->support_ic_type & (ODM_RTL8821 | ODM_RTL8881A)) {
			s8 pout = -6;

			switch (LNA_idx) {
			case 5:
				rx_pwr_all = pout - 32 - (2 * VGA_idx);
				break;
			case 4:
				rx_pwr_all = pout - 24 - (2 * VGA_idx);
				break;
			case 2:
				rx_pwr_all = pout - 11 - (2 * VGA_idx);
				break;
			case 1:
				rx_pwr_all = pout + 5 - (2 * VGA_idx);
				break;
			case 0:
				rx_pwr_all = pout + 21 - (2 * VGA_idx);
				break;
			}
			pwdb_all = odm_query_rx_pwr_percentage(rx_pwr_all);
		} else if (dm->support_ic_type == ODM_RTL8814A ||
			   dm->support_ic_type == ODM_RTL8822B) {
			s8 pout = -6;

			switch (LNA_idx) {
			/*CCK only use LNA: 2, 3, 5, 7*/
			case 7:
				rx_pwr_all = pout - 32 - (2 * VGA_idx);
				break;
			case 5:
				rx_pwr_all = pout - 22 - (2 * VGA_idx);
				break;
			case 3:
				rx_pwr_all = pout - 2 - (2 * VGA_idx);
				break;
			case 2:
				rx_pwr_all = pout + 5 - (2 * VGA_idx);
				break;
			default:
				break;
			}
			pwdb_all = odm_query_rx_pwr_percentage(rx_pwr_all);
		}

		dm->cck_lna_idx = LNA_idx;
		dm->cck_vga_idx = VGA_idx;
		phy_info->rx_pwdb_all = pwdb_all;
		phy_info->bt_rx_rssi_percentage = pwdb_all;
		phy_info->recv_signal_power = rx_pwr_all;
		/*(3) Get Signal Quality (EVM)*/
		{
			u8 sq = 0;

			if (!(dm->support_platform == ODM_WIN &&
			      dm->patch_id == RT_CID_819X_LENOVO))
				sq = phydm_get_signal_quality_8812(phy_info, dm,
								   phy_sta_rpt);

			phy_info->signal_quality = sq;
			phy_info->rx_mimo_signal_quality[ODM_RF_PATH_A] = sq;
		}

		for (i = ODM_RF_PATH_A; i < ODM_RF_PATH_MAX_JAGUAR; i++) {
			if (i == 0)
				phy_info->rx_mimo_signal_strength[0] = pwdb_all;
			else
				phy_info->rx_mimo_signal_strength[i] = 0;
		}
	} else {
		/*is OFDM rate*/
		fat_tab->hw_antsw_occur = phy_sta_rpt->hw_antsw_occur;

		dm->phy_dbg_info.num_qry_phy_status_ofdm++;

		/*(1)Get RSSI for OFDM rate*/

		for (i = ODM_RF_PATH_A; i < ODM_RF_PATH_MAX_JAGUAR; i++) {
			/*2008/01/30 MH we will judge RF RX path now.*/
			if (dm->rf_path_rx_enable & BIT(i))
				rf_rx_num++;
			/*2012.05.25 LukeLee: Testchip AGC report is wrong,
			 *it should be restored back to old formula in MP chip
			 */
			if (i < ODM_RF_PATH_C)
				rx_pwr[i] = (phy_sta_rpt->gain_trsw[i] & 0x7F) -
					    110;
			else
				rx_pwr[i] = (phy_sta_rpt->gain_trsw_cd[i - 2] &
					     0x7F) -
					    110;

			phy_info->rx_pwr[i] = rx_pwr[i];

			/* Translate DBM to percentage. */
			RSSI = odm_query_rx_pwr_percentage(rx_pwr[i]);

			/*total_rssi += RSSI;*/
			/*Get the best two RSSI*/
			if (RSSI > best_rssi && RSSI > second_rssi) {
				second_rssi = best_rssi;
				best_rssi = RSSI;
			} else if (RSSI > second_rssi && RSSI <= best_rssi) {
				second_rssi = RSSI;
			}

			phy_info->rx_mimo_signal_strength[i] = (u8)RSSI;

			/*Get Rx snr value in DB*/
			if (i < ODM_RF_PATH_C)
				phy_info->rx_snr[i] =
					dm->phy_dbg_info.rx_snr_db[i] =
						phy_sta_rpt->rxsnr[i] / 2;
			else if (dm->support_ic_type &
				 (ODM_RTL8814A | ODM_RTL8822B))
				phy_info->rx_snr[i] = dm->phy_dbg_info
							      .rx_snr_db[i] =
					phy_sta_rpt->csi_current[i - 2] / 2;

			/*(2) CFO_short  & CFO_tail*/
			if (i < ODM_RF_PATH_C) {
				phy_info->cfo_short[i] =
					odm_cfo((phy_sta_rpt->cfosho[i]));
				phy_info->cfo_tail[i] =
					odm_cfo((phy_sta_rpt->cfotail[i]));
			}
		}

		/*(3)PWDB, Average PWDB calculated by hardware (for RA)*/

		/*2012.05.25 LukeLee: Testchip AGC report is wrong, it should be
		 *restored back to old formula in MP chip
		 */
		if ((dm->support_ic_type &
		     (ODM_RTL8812 | ODM_RTL8821 | ODM_RTL8881A)) &&
		    (!dm->is_mp_chip))
			rx_pwr_all = (phy_sta_rpt->pwdb_all & 0x7f) - 110;
		else
			rx_pwr_all = (((phy_sta_rpt->pwdb_all) >> 1) & 0x7f) -
				     110; /*OLD FORMULA*/

		pwdb_all = odm_query_rx_pwr_percentage(rx_pwr_all);
		pwdb_all_bt = pwdb_all;

		phy_info->rx_pwdb_all = pwdb_all;
		phy_info->bt_rx_rssi_percentage = pwdb_all_bt;
		phy_info->rx_power = rx_pwr_all;
		phy_info->recv_signal_power = rx_pwr_all;

		if ((dm->support_platform == ODM_WIN) && (dm->patch_id == 19)) {
			/*do nothing*/
		} else {
			/*mgnt_info->customer_id != RT_CID_819X_LENOVO*/

			/*(4)EVM of OFDM rate*/

			if ((pktinfo->data_rate >= ODM_RATEMCS8) &&
			    (pktinfo->data_rate <= ODM_RATEMCS15))
				max_spatial_stream = 2;
			else if ((pktinfo->data_rate >= ODM_RATEVHTSS2MCS0) &&
				 (pktinfo->data_rate <= ODM_RATEVHTSS2MCS9))
				max_spatial_stream = 2;
			else if ((pktinfo->data_rate >= ODM_RATEMCS16) &&
				 (pktinfo->data_rate <= ODM_RATEMCS23))
				max_spatial_stream = 3;
			else if ((pktinfo->data_rate >= ODM_RATEVHTSS3MCS0) &&
				 (pktinfo->data_rate <= ODM_RATEVHTSS3MCS9))
				max_spatial_stream = 3;
			else
				max_spatial_stream = 1;

			for (i = 0; i < max_spatial_stream; i++) {
				/*Don't use shift operation like "rx_evmX >>= 1"
				 *because the compilor of free build environment
				 *fill most significant bit to "zero" when doing
				 *shifting operation which may change a negative
				 *value to positive one, then the dbm value
				 *(which is supposed to be negative) is not
				 *correct anymore.
				 */

				EVM = phydm_get_odm_evm(i, pktinfo,
							phy_sta_rpt);
				evm_dbm = phydm_get_evm_dbm(i, EVM, phy_sta_rpt,
							    phy_info);
				phy_info->rx_mimo_signal_quality[i] = EVM;
				phy_info->rx_mimo_evm_dbm[i] = evm_dbm;
			}
		}

		num_ss = phydm_rate_to_num_ss(dm, pktinfo->data_rate);
		odm_parsing_cfo(dm, pktinfo, phy_sta_rpt->cfotail, num_ss);
	}

	/*UI BSS List signal strength(in percentage), make it good looking,
	 *from 0~100.
	 */
	/*It is assigned to the BSS List in GetValueFromBeaconOrProbeRsp().*/
	if (is_cck_rate) {
		phy_info->signal_strength = pwdb_all;
	} else if (rf_rx_num != 0) {
		/* 2015/01 Sean, use the best two RSSI only,
		 * suggested by Ynlin and ChenYu.
		 */
		if (rf_rx_num == 1)
			avg_rssi = best_rssi;
		else
			avg_rssi = (best_rssi + second_rssi) / 2;

		phy_info->signal_strength = avg_rssi;
	}

	dm->rx_pwdb_ave = dm->rx_pwdb_ave + phy_info->rx_pwdb_all;

	dm->dm_fat_table.antsel_rx_keep_0 = phy_sta_rpt->antidx_anta;
	dm->dm_fat_table.antsel_rx_keep_1 = phy_sta_rpt->antidx_antb;
	dm->dm_fat_table.antsel_rx_keep_2 = phy_sta_rpt->antidx_antc;
	dm->dm_fat_table.antsel_rx_keep_3 = phy_sta_rpt->antidx_antd;
}

void phydm_reset_rssi_for_dm(struct phy_dm_struct *dm, u8 station_id)
{
	struct rtl_sta_info *entry;

	entry = dm->odm_sta_info[station_id];

	if (!IS_STA_VALID(entry))
		return;

	ODM_RT_TRACE(dm, ODM_COMP_RSSI_MONITOR,
		     "Reset RSSI for macid = (( %d ))\n", station_id);

	entry->rssi_stat.undecorated_smoothed_cck = -1;
	entry->rssi_stat.undecorated_smoothed_ofdm = -1;
	entry->rssi_stat.undecorated_smoothed_pwdb = -1;
	entry->rssi_stat.ofdm_pkt = 0;
	entry->rssi_stat.cck_pkt = 0;
	entry->rssi_stat.cck_sum_power = 0;
	entry->rssi_stat.is_send_rssi = RA_RSSI_STATE_INIT;
	entry->rssi_stat.packet_map = 0;
	entry->rssi_stat.valid_bit = 0;
}

void odm_init_rssi_for_dm(struct phy_dm_struct *dm) {}

static void odm_process_rssi_for_dm(struct phy_dm_struct *dm,
				    struct dm_phy_status_info *phy_info,
				    struct dm_per_pkt_info *pktinfo)
{
	s32 undecorated_smoothed_pwdb, undecorated_smoothed_cck,
		undecorated_smoothed_ofdm;
	u8 is_cck_rate = 0;
	u8 send_rssi_2_fw = 0;
	struct rtl_sta_info *entry;

	if (pktinfo->station_id >= ODM_ASSOCIATE_ENTRY_NUM)
		return;

	/* 2012/05/30 MH/Luke.Lee Add some description */
	/* In windows driver: AP/IBSS mode STA */
	entry = dm->odm_sta_info[pktinfo->station_id];

	if (!IS_STA_VALID(entry))
		return;

	{
		if ((!pktinfo->is_packet_match_bssid)) /*data frame only*/
			return;
	}

	if (pktinfo->is_packet_beacon)
		dm->phy_dbg_info.num_qry_beacon_pkt++;

	is_cck_rate = (pktinfo->data_rate <= ODM_RATE11M) ? true : false;
	dm->rx_rate = pktinfo->data_rate;

	/* --------------Statistic for antenna/path diversity---------------- */

	/* -----------------Smart Antenna Debug Message------------------ */

	undecorated_smoothed_cck = entry->rssi_stat.undecorated_smoothed_cck;
	undecorated_smoothed_ofdm = entry->rssi_stat.undecorated_smoothed_ofdm;
	undecorated_smoothed_pwdb = entry->rssi_stat.undecorated_smoothed_pwdb;

	if (pktinfo->is_packet_to_self || pktinfo->is_packet_beacon) {
		if (!is_cck_rate) /* ofdm rate */
			undecorated_smoothed_ofdm = phydm_process_rssi_ofdm(
				dm, phy_info, entry, undecorated_smoothed_ofdm);
		else
			undecorated_smoothed_cck = phydm_process_rssi_cck(
				dm, phy_info, entry, undecorated_smoothed_cck);

		undecorated_smoothed_pwdb = phydm_process_rssi_pwdb(
			dm, entry, pktinfo, undecorated_smoothed_ofdm,
			undecorated_smoothed_cck);

		if ((entry->rssi_stat.ofdm_pkt >= 1 ||
		     entry->rssi_stat.cck_pkt >= 5) &&
		    (entry->rssi_stat.is_send_rssi == RA_RSSI_STATE_INIT)) {
			send_rssi_2_fw = 1;
			entry->rssi_stat.is_send_rssi = RA_RSSI_STATE_SEND;
		}

		entry->rssi_stat.undecorated_smoothed_cck =
			undecorated_smoothed_cck;
		entry->rssi_stat.undecorated_smoothed_ofdm =
			undecorated_smoothed_ofdm;
		entry->rssi_stat.undecorated_smoothed_pwdb =
			undecorated_smoothed_pwdb;

		if (send_rssi_2_fw) { /* Trigger init rate by RSSI */

			if (entry->rssi_stat.ofdm_pkt != 0)
				entry->rssi_stat.undecorated_smoothed_pwdb =
					undecorated_smoothed_ofdm;

			ODM_RT_TRACE(
				dm, ODM_COMP_RSSI_MONITOR,
				"[Send to FW] PWDB = (( %d )), ofdm_pkt = (( %d )), cck_pkt = (( %d ))\n",
				undecorated_smoothed_pwdb,
				entry->rssi_stat.ofdm_pkt,
				entry->rssi_stat.cck_pkt);
		}
	}
}

/*
 * Endianness before calling this API
 */
static void odm_phy_status_query_92c_series(struct phy_dm_struct *dm,
					    struct dm_phy_status_info *phy_info,
					    u8 *phy_status,
					    struct dm_per_pkt_info *pktinfo)
{
	odm_rx_phy_status92c_series_parsing(dm, phy_info, phy_status, pktinfo);
	odm_process_rssi_for_dm(dm, phy_info, pktinfo);
}

/*
 * Endianness before calling this API
 */

static void odm_phy_status_query_jaguar_series(
	struct phy_dm_struct *dm, struct dm_phy_status_info *phy_info,
	u8 *phy_status, struct dm_per_pkt_info *pktinfo)
{
	odm_rx_phy_status_jaguar_series_parsing(dm, phy_info, phy_status,
						pktinfo);
	odm_process_rssi_for_dm(dm, phy_info, pktinfo);
}

void odm_phy_status_query(struct phy_dm_struct *dm,
			  struct dm_phy_status_info *phy_info, u8 *phy_status,
			  struct dm_per_pkt_info *pktinfo)
{
	if (dm->support_ic_type & ODM_IC_PHY_STATUE_NEW_TYPE) {
		phydm_rx_phy_status_new_type(dm, phy_status, pktinfo, phy_info);
		return;
	}

	if (dm->support_ic_type & ODM_IC_11AC_SERIES)
		odm_phy_status_query_jaguar_series(dm, phy_info, phy_status,
						   pktinfo);

	if (dm->support_ic_type & ODM_IC_11N_SERIES)
		odm_phy_status_query_92c_series(dm, phy_info, phy_status,
						pktinfo);
}

/* For future use. */
void odm_mac_status_query(struct phy_dm_struct *dm, u8 *mac_status, u8 mac_id,
			  bool is_packet_match_bssid, bool is_packet_to_self,
			  bool is_packet_beacon)
{
	/* 2011/10/19 Driver team will handle in the future. */
}

/*
 * If you want to add a new IC, Please follow below template and generate
 * a new one.
 */

enum hal_status
odm_config_rf_with_header_file(struct phy_dm_struct *dm,
			       enum odm_rf_config_type config_type,
			       enum odm_rf_radio_path e_rf_path)
{
	ODM_RT_TRACE(dm, ODM_COMP_INIT,
		     "===>%s (%s)\n", __func__,
		     (dm->is_mp_chip) ? "MPChip" : "TestChip");
	ODM_RT_TRACE(
		dm, ODM_COMP_INIT,
		"dm->support_platform: 0x%X, dm->support_interface: 0x%X, dm->board_type: 0x%X\n",
		dm->support_platform, dm->support_interface, dm->board_type);

	/* 1 AP doesn't use PHYDM power tracking table in these ICs */
	/* JJ ADD 20161014 */

	/* 1 All platforms support */
	if (dm->support_ic_type == ODM_RTL8822B) {
		if (config_type == CONFIG_RF_RADIO) {
			if (e_rf_path == ODM_RF_PATH_A)
				READ_AND_CONFIG_MP(8822b, _radioa);
			else if (e_rf_path == ODM_RF_PATH_B)
				READ_AND_CONFIG_MP(8822b, _radiob);
		} else if (config_type == CONFIG_RF_TXPWR_LMT) {
			if (dm->rfe_type == 5)
				READ_AND_CONFIG_MP(8822b, _txpwr_lmt_type5);
			else
				READ_AND_CONFIG_MP(8822b, _txpwr_lmt);
		}
	}

	return HAL_STATUS_SUCCESS;
}

enum hal_status
odm_config_rf_with_tx_pwr_track_header_file(struct phy_dm_struct *dm)
{
	ODM_RT_TRACE(dm, ODM_COMP_INIT,
		     "===>%s (%s)\n", __func__,
		     (dm->is_mp_chip) ? "MPChip" : "TestChip");
	ODM_RT_TRACE(
		dm, ODM_COMP_INIT,
		"dm->support_platform: 0x%X, dm->support_interface: 0x%X, dm->board_type: 0x%X\n",
		dm->support_platform, dm->support_interface, dm->board_type);

	/* 1 AP doesn't use PHYDM power tracking table in these ICs */
	/* JJ ADD 20161014 */

	/* 1 All platforms support */

	if (dm->support_ic_type == ODM_RTL8822B) {
		if (dm->rfe_type == 0)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type0);
		else if (dm->rfe_type == 1)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type1);
		else if (dm->rfe_type == 2)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type2);
		else if ((dm->rfe_type == 3) || (dm->rfe_type == 5))
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type3_type5);
		else if (dm->rfe_type == 4)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type4);
		else if (dm->rfe_type == 6)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type6);
		else if (dm->rfe_type == 7)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type7);
		else if (dm->rfe_type == 8)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type8);
		else if (dm->rfe_type == 9)
			READ_AND_CONFIG_MP(8822b, _txpowertrack_type9);
		else
			READ_AND_CONFIG_MP(8822b, _txpowertrack);
	}

	return HAL_STATUS_SUCCESS;
}

enum hal_status
odm_config_bb_with_header_file(struct phy_dm_struct *dm,
			       enum odm_bb_config_type config_type)
{
	/* 1 AP doesn't use PHYDM initialization in these ICs */
	/* JJ ADD 20161014 */

	/* 1 All platforms support */
	if (dm->support_ic_type == ODM_RTL8822B) {
		if (config_type == CONFIG_BB_PHY_REG)
			READ_AND_CONFIG_MP(8822b, _phy_reg);
		else if (config_type == CONFIG_BB_AGC_TAB)
			READ_AND_CONFIG_MP(8822b, _agc_tab);
		else if (config_type == CONFIG_BB_PHY_REG_PG)
			READ_AND_CONFIG_MP(8822b, _phy_reg_pg);
		/*else if (config_type == CONFIG_BB_PHY_REG_MP)*/
		/*READ_AND_CONFIG_MP(8822b, _phy_reg_mp);*/
	}

	return HAL_STATUS_SUCCESS;
}

enum hal_status odm_config_mac_with_header_file(struct phy_dm_struct *dm)
{
	ODM_RT_TRACE(dm, ODM_COMP_INIT,
		     "===>%s (%s)\n", __func__,
		     (dm->is_mp_chip) ? "MPChip" : "TestChip");
	ODM_RT_TRACE(
		dm, ODM_COMP_INIT,
		"dm->support_platform: 0x%X, dm->support_interface: 0x%X, dm->board_type: 0x%X\n",
		dm->support_platform, dm->support_interface, dm->board_type);

	/* 1 AP doesn't use PHYDM initialization in these ICs */
	/* JJ ADD 20161014 */

	/* 1 All platforms support */
	if (dm->support_ic_type == ODM_RTL8822B)
		READ_AND_CONFIG_MP(8822b, _mac_reg);

	return HAL_STATUS_SUCCESS;
}

enum hal_status
odm_config_fw_with_header_file(struct phy_dm_struct *dm,
			       enum odm_fw_config_type config_type,
			       u8 *p_firmware, u32 *size)
{
	return HAL_STATUS_SUCCESS;
}

u32 odm_get_hw_img_version(struct phy_dm_struct *dm)
{
	u32 version = 0;

	/* 1 AP doesn't use PHYDM initialization in these ICs */
	/* JJ ADD 20161014 */

	/*1 All platforms support*/
	if (dm->support_ic_type == ODM_RTL8822B)
		version = GET_VERSION_MP(8822b, _mac_reg);

	return version;
}

/* For 8822B only!! need to move to FW finally */
/*==============================================*/

bool phydm_query_is_mu_api(struct phy_dm_struct *phydm, u8 ppdu_idx,
			   u8 *p_data_rate, u8 *p_gid)
{
	u8 data_rate = 0, gid = 0;
	bool is_mu = false;

	data_rate = phydm->phy_dbg_info.num_of_ppdu[ppdu_idx];
	gid = phydm->phy_dbg_info.gid_num[ppdu_idx];

	if (data_rate & BIT(7)) {
		is_mu = true;
		data_rate = data_rate & ~(BIT(7));
	} else {
		is_mu = false;
	}

	*p_data_rate = data_rate;
	*p_gid = gid;

	return is_mu;
}

static void phydm_rx_statistic_cal(struct phy_dm_struct *phydm, u8 *phy_status,
				   struct dm_per_pkt_info *pktinfo)
{
	struct phy_status_rpt_jaguar2_type1 *phy_sta_rpt =
		(struct phy_status_rpt_jaguar2_type1 *)phy_status;
	u8 date_rate = pktinfo->data_rate & ~(BIT(7));

	if ((phy_sta_rpt->gid != 0) && (phy_sta_rpt->gid != 63)) {
		if (date_rate >= ODM_RATEVHTSS1MCS0) {
			phydm->phy_dbg_info
				.num_qry_mu_vht_pkt[date_rate - 0x2C]++;
			phydm->phy_dbg_info.num_of_ppdu[pktinfo->ppdu_cnt] =
				date_rate | BIT(7);
			phydm->phy_dbg_info.gid_num[pktinfo->ppdu_cnt] =
				phy_sta_rpt->gid;
		}

	} else {
		if (date_rate >= ODM_RATEVHTSS1MCS0) {
			phydm->phy_dbg_info.num_qry_vht_pkt[date_rate - 0x2C]++;
			phydm->phy_dbg_info.num_of_ppdu[pktinfo->ppdu_cnt] =
				date_rate;
			phydm->phy_dbg_info.gid_num[pktinfo->ppdu_cnt] =
				phy_sta_rpt->gid;
		}
	}
}

static void phydm_reset_phy_info(struct phy_dm_struct *phydm,
				 struct dm_phy_status_info *phy_info)
{
	phy_info->rx_pwdb_all = 0;
	phy_info->signal_quality = 0;
	phy_info->band_width = 0;
	phy_info->rx_count = 0;
	odm_memory_set(phydm, phy_info->rx_mimo_signal_quality, 0, 4);
	odm_memory_set(phydm, phy_info->rx_mimo_signal_strength, 0, 4);
	odm_memory_set(phydm, phy_info->rx_snr, 0, 4);

	phy_info->rx_power = -110;
	phy_info->recv_signal_power = -110;
	phy_info->bt_rx_rssi_percentage = 0;
	phy_info->signal_strength = 0;
	phy_info->bt_coex_pwr_adjust = 0;
	phy_info->channel = 0;
	phy_info->is_mu_packet = 0;
	phy_info->is_beamformed = 0;
	phy_info->rxsc = 0;
	odm_memory_set(phydm, phy_info->rx_pwr, -110, 4);
	odm_memory_set(phydm, phy_info->rx_mimo_evm_dbm, 0, 4);
	odm_memory_set(phydm, phy_info->cfo_short, 0, 8);
	odm_memory_set(phydm, phy_info->cfo_tail, 0, 8);
}

static void phydm_set_per_path_phy_info(u8 rx_path, s8 rx_pwr, s8 rx_evm,
					s8 cfo_tail, s8 rx_snr,
					struct dm_phy_status_info *phy_info)
{
	u8 evm_dbm = 0;
	u8 evm_percentage = 0;

	/* SNR is S(8,1), EVM is S(8,1), CFO is S(8,7) */

	if (rx_evm < 0) {
		/* Calculate EVM in dBm */
		evm_dbm = ((u8)(0 - rx_evm) >> 1);

		/* Calculate EVM in percentage */
		if (evm_dbm >= 33)
			evm_percentage = 100;
		else
			evm_percentage = (evm_dbm << 1) + (evm_dbm);
	}

	phy_info->rx_pwr[rx_path] = rx_pwr;
	phy_info->rx_mimo_evm_dbm[rx_path] = evm_dbm;

	/* CFO = CFO_tail * 312.5 / 2^7 ~= CFO tail * 39/512 (kHz)*/
	phy_info->cfo_tail[rx_path] = cfo_tail;
	phy_info->cfo_tail[rx_path] = ((phy_info->cfo_tail[rx_path] << 5) +
				       (phy_info->cfo_tail[rx_path] << 2) +
				       (phy_info->cfo_tail[rx_path] << 1) +
				       (phy_info->cfo_tail[rx_path])) >>
				      9;

	phy_info->rx_mimo_signal_strength[rx_path] =
		odm_query_rx_pwr_percentage(rx_pwr);
	phy_info->rx_mimo_signal_quality[rx_path] = evm_percentage;
	phy_info->rx_snr[rx_path] = rx_snr >> 1;
}

static void phydm_set_common_phy_info(s8 rx_power, u8 channel,
				      bool is_beamformed, bool is_mu_packet,
				      u8 bandwidth, u8 signal_quality, u8 rxsc,
				      struct dm_phy_status_info *phy_info)
{
	phy_info->rx_power = rx_power; /* RSSI in dB */
	phy_info->recv_signal_power = rx_power; /* RSSI in dB */
	phy_info->channel = channel; /* channel number */
	phy_info->is_beamformed = is_beamformed; /* apply BF */
	phy_info->is_mu_packet = is_mu_packet; /* MU packet */
	phy_info->rxsc = rxsc;
	phy_info->rx_pwdb_all =
		odm_query_rx_pwr_percentage(rx_power); /* RSSI in percentage */
	phy_info->signal_quality = signal_quality; /* signal quality */
	phy_info->band_width = bandwidth; /* bandwidth */
}

static void phydm_get_rx_phy_status_type0(struct phy_dm_struct *dm,
					  u8 *phy_status,
					  struct dm_per_pkt_info *pktinfo,
					  struct dm_phy_status_info *phy_info)
{
	/* type 0 is used for cck packet */

	struct phy_status_rpt_jaguar2_type0 *phy_sta_rpt =
		(struct phy_status_rpt_jaguar2_type0 *)phy_status;
	u8 sq = 0;
	s8 rx_power = phy_sta_rpt->pwdb - 110;

	/* JJ ADD 20161014 */

	/* Calculate Signal Quality*/
	if (pktinfo->is_packet_match_bssid) {
		if (phy_sta_rpt->signal_quality >= 64) {
			sq = 0;
		} else if (phy_sta_rpt->signal_quality <= 20) {
			sq = 100;
		} else {
			/* mapping to 2~99% */
			sq = 64 - phy_sta_rpt->signal_quality;
			sq = ((sq << 3) + sq) >> 2;
		}
	}

	/* Modify CCK PWDB if old AGC */
	if (!dm->cck_new_agc) {
		u8 lna_idx, vga_idx;

		lna_idx = ((phy_sta_rpt->lna_h << 3) | phy_sta_rpt->lna_l);
		vga_idx = phy_sta_rpt->vga;

		/* JJ ADD 20161014 */

		/* Need to do !! */
		/*if (dm->support_ic_type & ODM_RTL8822B) */
		/*rx_power = odm_CCKRSSI_8822B(LNA_idx, VGA_idx);*/
	}

	/* Update CCK packet counter */
	dm->phy_dbg_info.num_qry_phy_status_cck++;

	/*CCK no STBC and LDPC*/
	dm->phy_dbg_info.is_ldpc_pkt = false;
	dm->phy_dbg_info.is_stbc_pkt = false;

	/* Update Common information */
	phydm_set_common_phy_info(rx_power, phy_sta_rpt->channel, false, false,
				  ODM_BW20M, sq, phy_sta_rpt->rxsc, phy_info);

	/* Update CCK pwdb */
	/* Update per-path information */
	phydm_set_per_path_phy_info(ODM_RF_PATH_A, rx_power, 0, 0, 0, phy_info);

	dm->dm_fat_table.antsel_rx_keep_0 = phy_sta_rpt->antidx_a;
	dm->dm_fat_table.antsel_rx_keep_1 = phy_sta_rpt->antidx_b;
	dm->dm_fat_table.antsel_rx_keep_2 = phy_sta_rpt->antidx_c;
	dm->dm_fat_table.antsel_rx_keep_3 = phy_sta_rpt->antidx_d;
}

static void phydm_get_rx_phy_status_type1(struct phy_dm_struct *dm,
					  u8 *phy_status,
					  struct dm_per_pkt_info *pktinfo,
					  struct dm_phy_status_info *phy_info)
{
	/* type 1 is used for ofdm packet */

	struct phy_status_rpt_jaguar2_type1 *phy_sta_rpt =
		(struct phy_status_rpt_jaguar2_type1 *)phy_status;
	s8 rx_pwr_db = -120;
	u8 i, rxsc, bw = ODM_BW20M, rx_count = 0;
	bool is_mu;
	u8 num_ss;

	/* Update OFDM packet counter */
	dm->phy_dbg_info.num_qry_phy_status_ofdm++;

	/* Update per-path information */
	for (i = ODM_RF_PATH_A; i < ODM_RF_PATH_MAX_JAGUAR; i++) {
		if (dm->rx_ant_status & BIT(i)) {
			s8 rx_path_pwr_db;

			/* RX path counter */
			rx_count++;

			/* Update per-path information
			 * (RSSI_dB RSSI_percentage EVM SNR CFO sq)
			 */
			/* EVM report is reported by stream, not path */
			rx_path_pwr_db = phy_sta_rpt->pwdb[i] -
					 110; /* per-path pwdb in dB domain */
			phydm_set_per_path_phy_info(
				i, rx_path_pwr_db,
				phy_sta_rpt->rxevm[rx_count - 1],
				phy_sta_rpt->cfo_tail[i], phy_sta_rpt->rxsnr[i],
				phy_info);

			/* search maximum pwdb */
			if (rx_path_pwr_db > rx_pwr_db)
				rx_pwr_db = rx_path_pwr_db;
		}
	}

	/* mapping RX counter from 1~4 to 0~3 */
	if (rx_count > 0)
		phy_info->rx_count = rx_count - 1;

	/* Check if MU packet or not */
	if ((phy_sta_rpt->gid != 0) && (phy_sta_rpt->gid != 63)) {
		is_mu = true;
		dm->phy_dbg_info.num_qry_mu_pkt++;
	} else {
		is_mu = false;
	}

	/* count BF packet */
	dm->phy_dbg_info.num_qry_bf_pkt =
		dm->phy_dbg_info.num_qry_bf_pkt + phy_sta_rpt->beamformed;

	/*STBC or LDPC pkt*/
	dm->phy_dbg_info.is_ldpc_pkt = phy_sta_rpt->ldpc;
	dm->phy_dbg_info.is_stbc_pkt = phy_sta_rpt->stbc;

	/* Check sub-channel */
	if ((pktinfo->data_rate > ODM_RATE11M) &&
	    (pktinfo->data_rate < ODM_RATEMCS0))
		rxsc = phy_sta_rpt->l_rxsc;
	else
		rxsc = phy_sta_rpt->ht_rxsc;

	/* Check RX bandwidth */
	if (dm->support_ic_type & ODM_RTL8822B) {
		if ((rxsc >= 1) && (rxsc <= 8))
			bw = ODM_BW20M;
		else if ((rxsc >= 9) && (rxsc <= 12))
			bw = ODM_BW40M;
		else if (rxsc >= 13)
			bw = ODM_BW80M;
		else
			bw = phy_sta_rpt->rf_mode;
	} else if (dm->support_ic_type & (ODM_RTL8197F | ODM_RTL8723D |
					  ODM_RTL8710B)) { /* JJ ADD 20161014 */
		if (phy_sta_rpt->rf_mode == 0)
			bw = ODM_BW20M;
		else if ((rxsc == 1) || (rxsc == 2))
			bw = ODM_BW20M;
		else
			bw = ODM_BW40M;
	}

	/* Update packet information */
	phydm_set_common_phy_info(
		rx_pwr_db, phy_sta_rpt->channel, (bool)phy_sta_rpt->beamformed,
		is_mu, bw, odm_evm_db_to_percentage(phy_sta_rpt->rxevm[0]),
		rxsc, phy_info);

	num_ss = phydm_rate_to_num_ss(dm, pktinfo->data_rate);

	odm_parsing_cfo(dm, pktinfo, phy_sta_rpt->cfo_tail, num_ss);
	dm->dm_fat_table.antsel_rx_keep_0 = phy_sta_rpt->antidx_a;
	dm->dm_fat_table.antsel_rx_keep_1 = phy_sta_rpt->antidx_b;
	dm->dm_fat_table.antsel_rx_keep_2 = phy_sta_rpt->antidx_c;
	dm->dm_fat_table.antsel_rx_keep_3 = phy_sta_rpt->antidx_d;

	if (pktinfo->is_packet_match_bssid) {
		/* */
		phydm_rx_statistic_cal(dm, phy_status, pktinfo);
	}
}

static void phydm_get_rx_phy_status_type2(struct phy_dm_struct *dm,
					  u8 *phy_status,
					  struct dm_per_pkt_info *pktinfo,
					  struct dm_phy_status_info *phy_info)
{
	struct phy_status_rpt_jaguar2_type2 *phy_sta_rpt =
		(struct phy_status_rpt_jaguar2_type2 *)phy_status;
	s8 rx_pwr_db = -120;
	u8 i, rxsc, bw = ODM_BW20M, rx_count = 0;

	/* Update OFDM packet counter */
	dm->phy_dbg_info.num_qry_phy_status_ofdm++;

	/* Update per-path information */
	for (i = ODM_RF_PATH_A; i < ODM_RF_PATH_MAX_JAGUAR; i++) {
		if (dm->rx_ant_status & BIT(i)) {
			s8 rx_path_pwr_db;

			/* RX path counter */
			rx_count++;

			/* Update per-path information
			 * (RSSI_dB RSSI_percentage EVM SNR CFO sq)
			 */
			rx_path_pwr_db = phy_sta_rpt->pwdb[i] -
					 110; /* per-path pwdb in dB domain */

			phydm_set_per_path_phy_info(i, rx_path_pwr_db, 0, 0, 0,
						    phy_info);

			/* search maximum pwdb */
			if (rx_path_pwr_db > rx_pwr_db)
				rx_pwr_db = rx_path_pwr_db;
		}
	}

	/* mapping RX counter from 1~4 to 0~3 */
	if (rx_count > 0)
		phy_info->rx_count = rx_count - 1;

	/* Check RX sub-channel */
	if ((pktinfo->data_rate > ODM_RATE11M) &&
	    (pktinfo->data_rate < ODM_RATEMCS0))
		rxsc = phy_sta_rpt->l_rxsc;
	else
		rxsc = phy_sta_rpt->ht_rxsc;

	/*STBC or LDPC pkt*/
	dm->phy_dbg_info.is_ldpc_pkt = phy_sta_rpt->ldpc;
	dm->phy_dbg_info.is_stbc_pkt = phy_sta_rpt->stbc;

	/* Check RX bandwidth */
	/* the BW information of sc=0 is useless, because there is
	 * no information of RF mode
	 */

	if (dm->support_ic_type & ODM_RTL8822B) {
		if ((rxsc >= 1) && (rxsc <= 8))
			bw = ODM_BW20M;
		else if ((rxsc >= 9) && (rxsc <= 12))
			bw = ODM_BW40M;
		else if (rxsc >= 13)
			bw = ODM_BW80M;
		else
			bw = ODM_BW20M;
	} else if (dm->support_ic_type & (ODM_RTL8197F | ODM_RTL8723D |
					  ODM_RTL8710B)) { /* JJ ADD 20161014 */
		if (rxsc == 3)
			bw = ODM_BW40M;
		else
			bw = ODM_BW20M;
	}

	/* Update packet information */
	phydm_set_common_phy_info(rx_pwr_db, phy_sta_rpt->channel,
				  (bool)phy_sta_rpt->beamformed, false, bw, 0,
				  rxsc, phy_info);
}

static void
phydm_process_rssi_for_dm_new_type(struct phy_dm_struct *dm,
				   struct dm_phy_status_info *phy_info,
				   struct dm_per_pkt_info *pktinfo)
{
	s32 undecorated_smoothed_pwdb, accumulate_pwdb;
	u32 rssi_ave;
	u8 i;
	struct rtl_sta_info *entry;
	u8 scaling_factor = 4;

	if (pktinfo->station_id >= ODM_ASSOCIATE_ENTRY_NUM)
		return;

	entry = dm->odm_sta_info[pktinfo->station_id];

	if (!IS_STA_VALID(entry))
		return;

	if ((!pktinfo->is_packet_match_bssid)) /*data frame only*/
		return;

	if (pktinfo->is_packet_beacon)
		dm->phy_dbg_info.num_qry_beacon_pkt++;

	if (pktinfo->is_packet_to_self || pktinfo->is_packet_beacon) {
		u32 rssi_linear = 0;

		dm->rx_rate = pktinfo->data_rate;
		undecorated_smoothed_pwdb =
			entry->rssi_stat.undecorated_smoothed_pwdb;
		accumulate_pwdb = dm->accumulate_pwdb[pktinfo->station_id];
		dm->rssi_a = phy_info->rx_mimo_signal_strength[ODM_RF_PATH_A];
		dm->rssi_b = phy_info->rx_mimo_signal_strength[ODM_RF_PATH_B];
		dm->rssi_c = phy_info->rx_mimo_signal_strength[ODM_RF_PATH_C];
		dm->rssi_d = phy_info->rx_mimo_signal_strength[ODM_RF_PATH_D];

		for (i = ODM_RF_PATH_A; i < ODM_RF_PATH_MAX_JAGUAR; i++) {
			if (phy_info->rx_mimo_signal_strength[i] != 0)
				rssi_linear += odm_convert_to_linear(
					phy_info->rx_mimo_signal_strength[i]);
		}

		switch (phy_info->rx_count + 1) {
		case 2:
			rssi_linear = (rssi_linear >> 1);
			break;
		case 3:
			/* rssi_linear/3 ~ rssi_linear*11/32 */
			rssi_linear = ((rssi_linear) + (rssi_linear << 1) +
				       (rssi_linear << 3)) >>
				      5;
			break;
		case 4:
			rssi_linear = (rssi_linear >> 2);
			break;
		}
		rssi_ave = odm_convert_to_db(rssi_linear);

		if (undecorated_smoothed_pwdb <= 0) {
			accumulate_pwdb =
				(phy_info->rx_pwdb_all << scaling_factor);
			undecorated_smoothed_pwdb = phy_info->rx_pwdb_all;
		} else {
			accumulate_pwdb = accumulate_pwdb -
					  (accumulate_pwdb >> scaling_factor) +
					  rssi_ave;
			undecorated_smoothed_pwdb =
				(accumulate_pwdb +
				 (1 << (scaling_factor - 1))) >>
				scaling_factor;
		}

		entry->rssi_stat.undecorated_smoothed_pwdb =
			undecorated_smoothed_pwdb;
		dm->accumulate_pwdb[pktinfo->station_id] = accumulate_pwdb;
	}
}

void phydm_rx_phy_status_new_type(struct phy_dm_struct *phydm, u8 *phy_status,
				  struct dm_per_pkt_info *pktinfo,
				  struct dm_phy_status_info *phy_info)
{
	u8 phy_status_type = (*phy_status & 0xf);

	/* Memory reset */
	phydm_reset_phy_info(phydm, phy_info);

	/* Phy status parsing */
	switch (phy_status_type) {
	case 0: {
		phydm_get_rx_phy_status_type0(phydm, phy_status, pktinfo,
					      phy_info);
		break;
	}
	case 1: {
		phydm_get_rx_phy_status_type1(phydm, phy_status, pktinfo,
					      phy_info);
		break;
	}
	case 2: {
		phydm_get_rx_phy_status_type2(phydm, phy_status, pktinfo,
					      phy_info);
		break;
	}
	default:
		return;
	}

	/* Update signal strength to UI, and phy_info->rx_pwdb_all is the
	 * maximum RSSI of all path
	 */
	phy_info->signal_strength = phy_info->rx_pwdb_all;

	/* Calculate average RSSI and smoothed RSSI */
	phydm_process_rssi_for_dm_new_type(phydm, phy_info, pktinfo);
}
