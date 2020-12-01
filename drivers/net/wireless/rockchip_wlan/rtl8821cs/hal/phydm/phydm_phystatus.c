/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

/*@************************************************************
 * include files
 ************************************************************/

#include "mp_precomp.h"
#include "phydm_precomp.h"

#ifdef PHYDM_COMPILE_MU
u8 phydm_get_gid(struct dm_struct *dm, u8 *phy_status_inf)
{
#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT)
	struct phy_sts_rpt_jgr2_type1 *rpt_jgr2 = NULL;
#endif
#ifdef PHYSTS_3RD_TYPE_SUPPORT
	struct phy_sts_rpt_jgr3_type1 *rpt_jgr3 = NULL;
#endif
	u8 gid = 0;

	if (dm->ic_phy_sts_type == PHYDM_PHYSTS_TYPE_1)
		return 0;

	if ((*phy_status_inf & 0xf) != 1)
		return 0;

	switch (dm->ic_phy_sts_type) {
	#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT)
	case PHYDM_PHYSTS_TYPE_2:
		rpt_jgr2 = (struct phy_sts_rpt_jgr2_type1 *)phy_status_inf;
		gid = rpt_jgr2->gid;
		break;
	#endif
	#ifdef PHYSTS_3RD_TYPE_SUPPORT
	case PHYDM_PHYSTS_TYPE_3:
		rpt_jgr3 = (struct phy_sts_rpt_jgr3_type1 *)phy_status_inf;
		gid = rpt_jgr3->gid;
		break;
	#endif
	default:
		break;
	}

	return gid;
}
#endif

void phydm_rx_statistic_cal(struct dm_struct *dm,
			    struct phydm_phyinfo_struct *phy_info,
			    u8 *phy_status_inf,
			    struct phydm_perpkt_info_struct *pktinfo)
{
	struct odm_phy_dbg_info *dbg_i = &dm->phy_dbg_info;

#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	struct phydm_bf_rate_info_jgr3 *bfrateinfo = &dm->bf_rate_info_jgr3;
#endif

	u8 rate = (pktinfo->data_rate & 0x7f);
	u8 bw_idx = phy_info->band_width;
	u8 offset = 0;
	u8 gid = 0;
#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT || defined(PHYSTS_3RD_TYPE_SUPPORT))
	u8 val = 0;
#endif
	#ifdef PHYDM_COMPILE_MU
	u8 is_mu_pkt = 0;
	#endif

	if (rate <= ODM_RATE54M) {
		dbg_i->num_qry_legacy_pkt[rate]++;
	} else if (rate <= ODM_RATEMCS31) {
		dbg_i->ht_pkt_not_zero = true;
		offset = rate - ODM_RATEMCS0;

		if (offset > (HT_RATE_NUM - 1))
			offset = HT_RATE_NUM - 1;

		if (dm->support_ic_type &
		    (PHYSTS_2ND_TYPE_IC | PHYSTS_3RD_TYPE_IC)) {
			if (bw_idx == *dm->band_width) {
				dbg_i->num_qry_ht_pkt[offset]++;

			} else if (bw_idx == CHANNEL_WIDTH_20) {
				dbg_i->num_qry_pkt_sc_20m[offset]++;
				dbg_i->low_bw_20_occur = true;
			}
		} else {
			dbg_i->num_qry_ht_pkt[offset]++;
		}
	}
#if (ODM_IC_11AC_SERIES_SUPPORT || defined(PHYSTS_3RD_TYPE_SUPPORT))
	else if (rate <= ODM_RATEVHTSS4MCS9) {
		offset = rate - ODM_RATEVHTSS1MCS0;

		if (offset > (VHT_RATE_NUM - 1))
			offset = VHT_RATE_NUM - 1;

		#ifdef PHYDM_COMPILE_MU
		gid = phydm_get_gid(dm, phy_status_inf);

		if (gid != 0 && gid != 63)
			is_mu_pkt = true;

		if (is_mu_pkt) {
		#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT ||\
		     (defined(PHYSTS_3RD_TYPE_SUPPORT)))
			dbg_i->num_mu_vht_pkt[offset]++;
		#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
			bfrateinfo->num_mu_vht_pkt[offset]++;
		#endif
		#else
			dbg_i->num_qry_vht_pkt[offset]++; /*@for debug*/
		#endif
		} else
		#endif
		{
			dbg_i->vht_pkt_not_zero = true;

			if (dm->support_ic_type &
			    (PHYSTS_2ND_TYPE_IC | PHYSTS_3RD_TYPE_IC)) {
				if (bw_idx == *dm->band_width) {
					dbg_i->num_qry_vht_pkt[offset]++;
				#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
					bfrateinfo->num_qry_vht_pkt[offset]++;
				#endif

				} else if (bw_idx == CHANNEL_WIDTH_20) {
					dbg_i->num_qry_pkt_sc_20m[offset]++;
					dbg_i->low_bw_20_occur = true;
				} else {/*@if (bw_idx == CHANNEL_WIDTH_40)*/
					dbg_i->num_qry_pkt_sc_40m[offset]++;
					dbg_i->low_bw_40_occur = true;
				}
			} else {
				dbg_i->num_qry_vht_pkt[offset]++;
			}
		}

		#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT ||\
		     (defined(PHYSTS_3RD_TYPE_SUPPORT)))
		if (pktinfo->ppdu_cnt < 4) {
			val = rate;

			#ifdef PHYDM_COMPILE_MU
			if (is_mu_pkt)
				val |= BIT(7);
			#endif

			dbg_i->num_of_ppdu[pktinfo->ppdu_cnt] = val;
			dbg_i->gid_num[pktinfo->ppdu_cnt] = gid;
		}
		#endif
	}
#endif
}

void phydm_reset_phystatus_avg(struct dm_struct *dm)
{
	struct phydm_phystatus_avg *dbg_avg = NULL;

	dbg_avg = &dm->phy_dbg_info.phystatus_statistic_avg;
	odm_memory_set(dm, &dbg_avg->rssi_cck_avg, 0,
		       sizeof(struct phydm_phystatus_avg));
}

void phydm_reset_phystatus_statistic(struct dm_struct *dm)
{
	struct phydm_phystatus_statistic *dbg_s = NULL;

	dbg_s = &dm->phy_dbg_info.physts_statistic_info;

	odm_memory_set(dm, &dbg_s->rssi_cck_sum, 0,
		       sizeof(struct phydm_phystatus_statistic));
}

void phydm_reset_phy_info(struct dm_struct *dm,
			  struct phydm_phyinfo_struct *phy_info)
{
	u8 i = 0;

	odm_memory_set(dm, &phy_info->physts_rpt_valid, 0,
		       sizeof(struct phydm_phyinfo_struct));

	phy_info->rx_power = -110;
	phy_info->recv_signal_power = -110;

	for (i = 0; i < dm->num_rf_path; i++)
		phy_info->rx_pwr[i] = -110;
}

void phydm_avg_rssi_evm_snr(void *dm_void,
			    struct phydm_phyinfo_struct *phy_info,
			    struct phydm_perpkt_info_struct *pktinfo)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_phy_dbg_info *dbg_i = &dm->phy_dbg_info;
	struct phydm_phystatus_statistic *dbg_s = &dbg_i->physts_statistic_info;
	u8 *rssi = phy_info->rx_mimo_signal_strength;
	u8 *evm = phy_info->rx_mimo_evm_dbm;
	s8 *snr = phy_info->rx_snr;
	u32 size = PHYSTS_PATH_NUM; /*size of path=4*/
	u16 size_th = PHY_HIST_SIZE - 1; /*size of threshold*/
	u16 val = 0, intvl = 0;
	u8 i = 0;

	if (pktinfo->is_packet_beacon) {
		for (i = 0; i < dm->num_rf_path; i++)
			dbg_s->rssi_beacon_sum[i] += rssi[i];

		dbg_s->rssi_beacon_cnt++;
	}

	if (pktinfo->data_rate <= ODM_RATE11M) {
		/*RSSI*/
		dbg_s->rssi_cck_sum += rssi[0];
		#if (defined(PHYSTS_3RD_TYPE_SUPPORT) && defined(PHYDM_COMPILE_ABOVE_2SS))
		if (dm->support_ic_type & PHYSTS_3RD_TYPE_IC) {
			for (i = 1; i < dm->num_rf_path; i++)
				dbg_s->rssi_cck_sum_abv_2ss[i - 1] += rssi[i];
		}
		#endif
		dbg_s->rssi_cck_cnt++;
	} else if (pktinfo->data_rate <= ODM_RATE54M) {
		for (i = 0; i < dm->num_rf_path; i++) {
			/*SNR & RSSI*/
			dbg_s->snr_ofdm_sum[i] += snr[i];
			dbg_s->rssi_ofdm_sum[i] += rssi[i];
		}
		/*@evm*/
		dbg_s->evm_ofdm_sum += evm[0];
		dbg_s->rssi_ofdm_cnt++;

		val = (u16)evm[0];
		intvl = phydm_find_intrvl(dm, val, dbg_i->evm_hist_th, size_th);
		dbg_s->evm_ofdm_hist[intvl]++;

		val = (u16)snr[0];
		intvl = phydm_find_intrvl(dm, val, dbg_i->snr_hist_th, size_th);
		dbg_s->snr_ofdm_hist[intvl]++;

	} else if (pktinfo->rate_ss == 1) {
/*@===[1-SS]==================================================================*/
		for (i = 0; i < dm->num_rf_path; i++) {
			/*SNR & RSSI*/
			dbg_s->snr_1ss_sum[i] += snr[i];
			dbg_s->rssi_1ss_sum[i] += rssi[i];
		}

		/*@evm*/
		dbg_s->evm_1ss_sum += evm[0];
		/*@EVM Histogram*/
		val = (u16)evm[0];
		intvl = phydm_find_intrvl(dm, val, dbg_i->evm_hist_th, size_th);
		dbg_s->evm_1ss_hist[intvl]++;

		/*SNR Histogram*/
		val = (u16)snr[0];
		intvl = phydm_find_intrvl(dm, val, dbg_i->snr_hist_th, size_th);
		dbg_s->snr_1ss_hist[intvl]++;

		dbg_s->rssi_1ss_cnt++;
	} else if (pktinfo->rate_ss == 2) {
/*@===[2-SS]==================================================================*/
		#if (defined(PHYDM_COMPILE_ABOVE_2SS))
		for (i = 0; i < dm->num_rf_path; i++) {
			/*SNR & RSSI*/
			dbg_s->snr_2ss_sum[i] += snr[i];
			dbg_s->rssi_2ss_sum[i] += rssi[i];
		}

		for (i = 0; i < pktinfo->rate_ss; i++) {
			/*@evm*/
			dbg_s->evm_2ss_sum[i] += evm[i];
			/*@EVM Histogram*/
			val = (u16)evm[i];
			intvl = phydm_find_intrvl(dm, val, dbg_i->evm_hist_th,
						  size_th);
			dbg_s->evm_2ss_hist[i][intvl]++;

			/*SNR Histogram*/
			val = (u16)snr[i];
			intvl = phydm_find_intrvl(dm, val, dbg_i->snr_hist_th,
						  size_th);
			dbg_s->snr_2ss_hist[i][intvl]++;
		}
		dbg_s->rssi_2ss_cnt++;
		#endif
	} else if (pktinfo->rate_ss == 3) {
/*@===[3-SS]==================================================================*/
		#if (defined(PHYDM_COMPILE_ABOVE_3SS))
		for (i = 0; i < dm->num_rf_path; i++) {
			/*SNR & RSSI*/
			dbg_s->snr_3ss_sum[i] += snr[i];
			dbg_s->rssi_3ss_sum[i] += rssi[i];
		}

		for (i = 0; i < pktinfo->rate_ss; i++) {
			/*@evm*/
			dbg_s->evm_3ss_sum[i] += evm[i];
			/*@EVM Histogram*/
			val = (u16)evm[i];
			intvl = phydm_find_intrvl(dm, val, dbg_i->evm_hist_th,
						  size_th);
			dbg_s->evm_3ss_hist[i][intvl]++;

			/*SNR Histogram*/
			val = (u16)snr[i];
			intvl = phydm_find_intrvl(dm, val, dbg_i->snr_hist_th,
						  size_th);
			dbg_s->snr_3ss_hist[i][intvl]++;
		}
		dbg_s->rssi_3ss_cnt++;
		#endif
	} else if (pktinfo->rate_ss == 4) {
/*@===[4-SS]==================================================================*/
		#if (defined(PHYDM_COMPILE_ABOVE_4SS))
		for (i = 0; i < dm->num_rf_path; i++) {
			/*SNR & RSSI*/
			dbg_s->snr_4ss_sum[i] += snr[i];
			dbg_s->rssi_4ss_sum[i] += rssi[i];
		}

		for (i = 0; i < pktinfo->rate_ss; i++) {
			/*@evm*/
			dbg_s->evm_4ss_sum[i] += evm[i];

			/*@EVM Histogram*/
			val = (u16)evm[i];
			intvl = phydm_find_intrvl(dm, val, dbg_i->evm_hist_th,
						  size_th);
			dbg_s->evm_4ss_hist[i][intvl]++;

			/*SNR Histogram*/
			val = (u16)snr[i];
			intvl = phydm_find_intrvl(dm, val, dbg_i->snr_hist_th,
						  size_th);
			dbg_s->snr_4ss_hist[i][intvl]++;
		}
		dbg_s->rssi_4ss_cnt++;
		#endif
	}
}

void phydm_avg_phystatus_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_phy_dbg_info *dbg_i = &dm->phy_dbg_info;
	u16 snr_hist_th[PHY_HIST_TH_SIZE] = {5, 8, 11, 14, 17, 20, 23, 26,
					      29, 32, 35};
	u16 evm_hist_th[PHY_HIST_TH_SIZE] = {5, 8, 11, 14, 17, 20, 23, 26,
					      29, 32, 35};
	#ifdef PHYDM_PHYSTAUS_AUTO_SWITCH
	u16 cn_hist_th[PHY_HIST_TH_SIZE] = {2, 3, 4, 5, 6, 8, 10,
					    12, 14, 16, 18};
	#endif
	u32 size = PHY_HIST_TH_SIZE * 2;
	u8 i = 0;

	odm_move_memory(dm, dbg_i->snr_hist_th, snr_hist_th, size);
	odm_move_memory(dm, dbg_i->evm_hist_th, evm_hist_th, size);
	#ifdef PHYDM_PHYSTAUS_AUTO_SWITCH
	dm->pkt_proc_struct.physts_auto_swch_en = false;
	for (i = 0; i < PHY_HIST_TH_SIZE; i++)
		dbg_i->cn_hist_th[i] = cn_hist_th[i] << 1;
	#endif
}

u8 phydm_get_signal_quality(struct phydm_phyinfo_struct *phy_info,
			    struct dm_struct *dm,
			    struct phy_status_rpt_8192cd *phy_sts)
{
	u8 sq_rpt;
	u8 result = 0;

	if (phy_info->rx_pwdb_all > 40 && !dm->is_in_hct_test) {
		result = 100;
	} else {
		sq_rpt = phy_sts->cck_sig_qual_ofdm_pwdb_all;

		if (sq_rpt > 64)
			result = 0;
		else if (sq_rpt < 20)
			result = 100;
		else
			result = ((64 - sq_rpt) * 100) / 44;
	}

	return result;
}

u8 phydm_pw_2_percent(s8 ant_power)
{
	if ((ant_power <= -100) || ant_power >= 20)
		return 0;
	else if (ant_power >= 0)
		return 100;
	else
		return 100 + ant_power;
}

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
void phydm_process_signal_strength(struct dm_struct *dm,
				   struct phydm_phyinfo_struct *phy_info,
				   struct phydm_perpkt_info_struct *pktinfo)
{
	boolean is_cck_rate = 0;
	u8 avg_rssi = 0, tmp_rssi = 0, best_rssi = 0, second_rssi = 0;
	u8 ss = 0; /*signal strenth after scale mapping*/
	u8 pwdb = phy_info->rx_pwdb_all;
	u8 i;

	is_cck_rate = (pktinfo->data_rate <= ODM_RATE11M) ? true : false;

	/*use the best two RSSI only*/
	for (i = RF_PATH_A; i < PHYDM_MAX_RF_PATH; i++) {
		tmp_rssi = phy_info->rx_mimo_signal_strength[i];

		/*@Get the best two RSSI*/
		if (tmp_rssi > best_rssi && tmp_rssi > second_rssi) {
			second_rssi = best_rssi;
			best_rssi = tmp_rssi;
		} else if (tmp_rssi > second_rssi && tmp_rssi <= best_rssi) {
			second_rssi = tmp_rssi;
		}
	}

	if (best_rssi == 0)
		return;

	if (pktinfo->rate_ss == 1)
		avg_rssi = best_rssi;
	else
		avg_rssi = (best_rssi + second_rssi) >> 1;

	/* Update signal strength to UI,
	 * and phy_info->rx_pwdb_all is the maximum RSSI of all path
	 */
	if (dm->support_ic_type & (PHYSTS_3RD_TYPE_IC | PHYSTS_2ND_TYPE_IC))
		ss = SignalScaleProc(dm->adapter, pwdb, false, false);
	else
		ss = SignalScaleProc(dm->adapter, pwdb, true, is_cck_rate);

	phy_info->signal_strength = ss;
}

static u8 phydm_sq_patch_lenovo(
	struct dm_struct *dm,
	u8 is_cck_rate,
	u8 pwdb_all,
	u8 path,
	u8 RSSI)
{
	u8 sq = 0;

	if (is_cck_rate) {
		if (dm->support_ic_type & ODM_RTL8192E) {
/*@
 * <Roger_Notes>
 * Expected signal strength and bars indication at Lenovo lab. 2013.04.11
 * 802.11n, 802.11b, 802.11g only at channel 6
 *
 *	Attenuation (dB)	OS Signal Bars	RSSI by Xirrus (dBm)
 *		50				5			-49
 *		55				5			-49
 *		60				5			-50
 *		65				5			-51
 *		70				5			-52
 *		75				5			-54
 *		80				5			-55
 *		85				4			-60
 *		90				3			-63
 *		95				3			-65
 *		100				2			-67
 *		102				2			-67
 *		104				1			-70
 */
			if (pwdb_all >= 50)
				sq = 100;
			else if (pwdb_all >= 35 && pwdb_all < 50)
				sq = 80;
			else if (pwdb_all >= 31 && pwdb_all < 35)
				sq = 60;
			else if (pwdb_all >= 22 && pwdb_all < 31)
				sq = 40;
			else if (pwdb_all >= 18 && pwdb_all < 22)
				sq = 20;
			else
				sq = 10;
		} else {
			if (pwdb_all >= 50)
				sq = 100;
			else if (pwdb_all >= 35 && pwdb_all < 50)
				sq = 80;
			else if (pwdb_all >= 22 && pwdb_all < 35)
				sq = 60;
			else if (pwdb_all >= 18 && pwdb_all < 22)
				sq = 40;
			else
				sq = 10;
		}

	} else {
		/* OFDM rate */

		if (dm->support_ic_type & ODM_RTL8192E) {
			if (RSSI >= 45)
				sq = 100;
			else if (RSSI >= 22 && RSSI < 45)
				sq = 80;
			else if (RSSI >= 18 && RSSI < 22)
				sq = 40;
			else
				sq = 20;
		} else {
			if (RSSI >= 45)
				sq = 100;
			else if (RSSI >= 22 && RSSI < 45)
				sq = 80;
			else if (RSSI >= 18 && RSSI < 22)
				sq = 40;
			else
				sq = 20;
		}
	}
	return sq;
}

static u8 phydm_sq_patch_rt_cid_819x_acer(
	struct dm_struct *dm,
	u8 is_cck_rate,
	u8 pwdb_all,
	u8 path,
	u8 RSSI)
{
	u8 sq = 0;

	if (is_cck_rate) {
#if OS_WIN_FROM_WIN8(OS_VERSION)
		if (pwdb_all >= 50)
			sq = 100;
		else if (pwdb_all >= 35 && pwdb_all < 50)
			sq = 80;
		else if (pwdb_all >= 30 && pwdb_all < 35)
			sq = 60;
		else if (pwdb_all >= 25 && pwdb_all < 30)
			sq = 40;
		else if (pwdb_all >= 20 && pwdb_all < 25)
			sq = 20;
		else
			sq = 10;
#else
		if (pwdb_all >= 50)
			sq = 100;
		else if (pwdb_all >= 35 && pwdb_all < 50)
			sq = 80;
		else if (pwdb_all >= 30 && pwdb_all < 35)
			sq = 60;
		else if (pwdb_all >= 25 && pwdb_all < 30)
			sq = 40;
		else if (pwdb_all >= 20 && pwdb_all < 25)
			sq = 20;
		else
			sq = 10;

		/* @Abnormal case, do not indicate the value above 20 on Win7 */
		if (pwdb_all == 0)
			sq = 20;
#endif

	} else {
		/* OFDM rate */
		if (dm->support_ic_type & ODM_RTL8192E) {
			if (RSSI >= 45)
				sq = 100;
			else if (RSSI >= 22 && RSSI < 45)
				sq = 80;
			else if (RSSI >= 18 && RSSI < 22)
				sq = 40;
			else
				sq = 20;
		} else {
			if (RSSI >= 35)
				sq = 100;
			else if (RSSI >= 30 && RSSI < 35)
				sq = 80;
			else if (RSSI >= 25 && RSSI < 30)
				sq = 40;
			else
				sq = 20;
		}
	}
	return sq;
}
#endif

static u8
phydm_evm_2_percent(s8 value)
{
	/* @-33dB~0dB to 0%~99% */
	s8 ret_val;

	ret_val = value;
	ret_val /= 2;

/*@dbg_print("value=%d\n", value);*/
#ifdef ODM_EVM_ENHANCE_ANTDIV
	if (ret_val >= 0)
		ret_val = 0;

	if (ret_val <= -40)
		ret_val = -40;

	ret_val = 0 - ret_val;
	ret_val *= 3;
#else
	if (ret_val >= 0)
		ret_val = 0;

	if (ret_val <= -33)
		ret_val = -33;

	ret_val = 0 - ret_val;
	ret_val *= 3;

	if (ret_val == 99)
		ret_val = 100;
#endif

	return (u8)ret_val;
}

s8 phydm_cck_rssi_convert(struct dm_struct *dm, u16 lna_idx, u8 vga_idx)
{
	/*@phydm_get_cck_rssi_table_from_reg*/
	return (dm->cck_lna_gain_table[lna_idx] - (vga_idx << 1));
}

void phydm_get_cck_rssi_table_from_reg(struct dm_struct *dm)
{
	u8 used_lna_idx_tmp;
	u32 reg_0xa80 = 0x7431, reg_0xabc = 0xcbe5edfd;
	u32 val = 0;
	u8 i;

	/*@example: {-53, -43, -33, -27, -19, -13, -3, 1}*/
	/*@{0xCB, 0xD5, 0xDF, 0xE5, 0xED, 0xF3, 0xFD, 0x2}*/

	PHYDM_DBG(dm, ODM_COMP_INIT, "CCK LNA Gain table init\n");

	if (!(dm->support_ic_type & ODM_RTL8197F))
		return;

	reg_0xa80 = odm_get_bb_reg(dm, R_0xa80, 0xFFFF);
	reg_0xabc = odm_get_bb_reg(dm, R_0xabc, MASKDWORD);

	PHYDM_DBG(dm, ODM_COMP_INIT, "reg_0xa80 = 0x%x\n", reg_0xa80);
	PHYDM_DBG(dm, ODM_COMP_INIT, "reg_0xabc = 0x%x\n", reg_0xabc);

	for (i = 0; i <= 3; i++) {
		used_lna_idx_tmp = (u8)((reg_0xa80 >> (4 * i)) & 0x7);
		val = (reg_0xabc >> (8 * i)) & 0xff;
		dm->cck_lna_gain_table[used_lna_idx_tmp] = (s8)val;
	}

	PHYDM_DBG(dm, ODM_COMP_INIT,
		  "cck_lna_gain_table = {%d,%d,%d,%d,%d,%d,%d,%d}\n",
		  dm->cck_lna_gain_table[0], dm->cck_lna_gain_table[1],
		  dm->cck_lna_gain_table[2], dm->cck_lna_gain_table[3],
		  dm->cck_lna_gain_table[4], dm->cck_lna_gain_table[5],
		  dm->cck_lna_gain_table[6], dm->cck_lna_gain_table[7]);
}

s8 phydm_get_cck_rssi(void *dm_void, u8 lna_idx, u8 vga_idx)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	s8 rx_pow = 0;

	switch (dm->support_ic_type) {
	#if (RTL8197F_SUPPORT)
	case ODM_RTL8197F:
		rx_pow = phydm_cck_rssi_convert(dm, lna_idx, vga_idx);
		break;
	#endif

	#if (RTL8723D_SUPPORT)
	case ODM_RTL8723D:
		rx_pow = phydm_cckrssi_8723d(dm, lna_idx, vga_idx);
		break;
	#endif

	#if (RTL8710B_SUPPORT)
	case ODM_RTL8710B:
		rx_pow = phydm_cckrssi_8710b(dm, lna_idx, vga_idx);
		break;
	#endif

	#if (RTL8721D_SUPPORT)
	case ODM_RTL8721D:
		rx_pow = phydm_cckrssi_8721d(dm, lna_idx, vga_idx);
		break;
	#endif

	#if (RTL8710C_SUPPORT)
	case ODM_RTL8710C:
		rx_pow = phydm_cckrssi_8710c(dm, lna_idx, vga_idx);
		break;
	#endif

	#if (RTL8192F_SUPPORT)
	case ODM_RTL8192F:
		rx_pow = phydm_cckrssi_8192f(dm, lna_idx, vga_idx);
		break;
	#endif

	#if (RTL8821C_SUPPORT)
	case ODM_RTL8821C:
		rx_pow = phydm_cck_rssi_8821c(dm, lna_idx, vga_idx);
		break;
	#endif

	#if (RTL8195B_SUPPORT)
	case ODM_RTL8195B:
		rx_pow = phydm_cck_rssi_8195B(dm, lna_idx, vga_idx);
		break;
	#endif

	#if (RTL8188E_SUPPORT)
	case ODM_RTL8188E:
		rx_pow = phydm_cck_rssi_8188e(dm, lna_idx, vga_idx);
		break;
	#endif

	#if (RTL8192E_SUPPORT)
	case ODM_RTL8192E:
		rx_pow = phydm_cck_rssi_8192e(dm, lna_idx, vga_idx);
		break;
	#endif

	#if (RTL8723B_SUPPORT)
	case ODM_RTL8723B:
		rx_pow = phydm_cck_rssi_8723b(dm, lna_idx, vga_idx);
		break;
	#endif

	#if (RTL8703B_SUPPORT)
	case ODM_RTL8703B:
		rx_pow = phydm_cck_rssi_8703b(dm, lna_idx, vga_idx);
		break;
	#endif

	#if (RTL8188F_SUPPORT)
	case ODM_RTL8188F:
		rx_pow = phydm_cck_rssi_8188f(dm, lna_idx, vga_idx);
		break;
	#endif

	#if (RTL8195A_SUPPORT)
	case ODM_RTL8195A:
		rx_pow = phydm_cck_rssi_8195a(dm, lna_idx, vga_idx);
		break;
	#endif

	#if (RTL8812A_SUPPORT)
	case ODM_RTL8812:
		rx_pow = phydm_cck_rssi_8812a(dm, lna_idx, vga_idx);
		break;
	#endif

	#if (RTL8821A_SUPPORT || RTL8881A_SUPPORT)
	case ODM_RTL8821:
	case ODM_RTL8881A:
		rx_pow = phydm_cck_rssi_8821a(dm, lna_idx, vga_idx);
		break;
	#endif

	#if (RTL8814A_SUPPORT)
	case ODM_RTL8814A:
		rx_pow = phydm_cck_rssi_8814a(dm, lna_idx, vga_idx);
		break;
	#endif

	default:
		break;
	}

	return rx_pow;
}

#if (ODM_IC_11N_SERIES_SUPPORT)
void phydm_phy_sts_n_parsing(struct dm_struct *dm,
			     struct phydm_phyinfo_struct *phy_info,
			     u8 *phy_status_inf,
			     struct phydm_perpkt_info_struct *pktinfo)
{
	u8 i = 0;
	s8 rx_pwr[4], rx_pwr_all = 0;
	u8 EVM, pwdb_all = 0, pwdb_all_bt = 0;
	u8 RSSI, total_rssi = 0;
	u8 rf_rx_num = 0;
	u8 lna_idx = 0;
	u8 vga_idx = 0;
	u8 cck_agc_rpt;
	s8 evm_tmp = 0;
	u8 sq = 0;
	u8 val_tmp = 0;
	s8 val_s8 = 0;
	struct phy_status_rpt_8192cd *phy_sts = NULL;

	phy_sts = (struct phy_status_rpt_8192cd *)phy_status_inf;

	if (pktinfo->is_cck_rate) {
		cck_agc_rpt = phy_sts->cck_agc_rpt_ofdm_cfosho_a;

		/*@3 bit LNA*/
		lna_idx = ((cck_agc_rpt & 0xE0) >> 5);
		vga_idx = (cck_agc_rpt & 0x1F);

		#if (RTL8703B_SUPPORT)
		if (dm->support_ic_type & (ODM_RTL8703B) &&
		    dm->cck_agc_report_type == 1) {
			/*@4 bit LNA*/
			if (phy_sts->cck_rpt_b_ofdm_cfosho_b & BIT(7))
				val_tmp = 1;
			else
				val_tmp = 0;
			lna_idx = (val_tmp << 3) | lna_idx;
		}
		#endif

		rx_pwr_all = phydm_get_cck_rssi(dm, lna_idx, vga_idx);

		PHYDM_DBG(dm, DBG_RSSI_MNTR,
			  "ext_lna_gain (( %d )), lna_idx: (( 0x%x )), vga_idx: (( 0x%x )), rx_pwr_all: (( %d ))\n",
			  dm->ext_lna_gain, lna_idx, vga_idx, rx_pwr_all);

		if (dm->board_type & ODM_BOARD_EXT_LNA)
			rx_pwr_all -= dm->ext_lna_gain;

		pwdb_all = phydm_pw_2_percent(rx_pwr_all);

		if (pktinfo->is_to_self) {
			dm->cck_lna_idx = lna_idx;
			dm->cck_vga_idx = vga_idx;
		}

		phy_info->rx_pwdb_all = pwdb_all;
		phy_info->bt_rx_rssi_percentage = pwdb_all;
		phy_info->recv_signal_power = rx_pwr_all;

		/* @(3) Get Signal Quality (EVM) */
		#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		if (dm->iot_table.win_patch_id == RT_CID_819X_LENOVO)
			sq = phydm_sq_patch_lenovo(dm, pktinfo->is_cck_rate, pwdb_all, 0, 0);
		else if (dm->iot_table.win_patch_id == RT_CID_819X_ACER)
			sq = phydm_sq_patch_rt_cid_819x_acer(dm, pktinfo->is_cck_rate, pwdb_all, 0, 0);
		else
		#endif
			sq = phydm_get_signal_quality(phy_info, dm, phy_sts);

		/* @dbg_print("cck sq = %d\n", sq); */

		phy_info->signal_quality = sq;
		phy_info->rx_mimo_signal_quality[RF_PATH_A] = sq;
		phy_info->rx_mimo_signal_quality[RF_PATH_B] = -1;

		for (i = RF_PATH_A; i < PHYDM_MAX_RF_PATH; i++) {
			if (i == 0)
				phy_info->rx_mimo_signal_strength[0] = pwdb_all;
			else
				phy_info->rx_mimo_signal_strength[i] = 0;
		}
	} else { /* @2 is OFDM rate */

		/* @(1)Get RSSI for HT rate */

		for (i = RF_PATH_A; i < dm->num_rf_path; i++) {
			if (dm->rf_path_rx_enable & BIT(i))
				rf_rx_num++;

			val_s8 = phy_sts->path_agc[i].gain & 0x3F;
			rx_pwr[i] = (val_s8 * 2) - 110;

			if (pktinfo->is_to_self)
				dm->ofdm_agc_idx[i] = val_s8;

			phy_info->rx_pwr[i] = rx_pwr[i];
			RSSI = phydm_pw_2_percent(rx_pwr[i]);
			total_rssi += RSSI;

			phy_info->rx_mimo_signal_strength[i] = (u8)RSSI;

			/* @Get Rx snr value in DB */
			val_s8 = (s8)(phy_sts->path_rxsnr[i] / 2);
			phy_info->rx_snr[i] = val_s8;

			/* Record Signal Strength for next packet */

			#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
			if (i == RF_PATH_A) {
				if (dm->iot_table.win_patch_id == RT_CID_819X_LENOVO) {
					phy_info->signal_quality = phydm_sq_patch_lenovo(dm, pktinfo->is_cck_rate, pwdb_all, i, RSSI);
				} else if (dm->iot_table.win_patch_id == RT_CID_819X_ACER)
					phy_info->signal_quality = phydm_sq_patch_rt_cid_819x_acer(dm, pktinfo->is_cck_rate, pwdb_all, 0, RSSI);
			}
			#endif
		}

		/* @(2)PWDB, Average PWDB calculated by hardware (for RA) */
		val_s8 = phy_sts->cck_sig_qual_ofdm_pwdb_all >> 1;
		rx_pwr_all = (val_s8  & 0x7f) - 110;

		pwdb_all = phydm_pw_2_percent(rx_pwr_all);
		pwdb_all_bt = pwdb_all;

		phy_info->rx_pwdb_all = pwdb_all;
		phy_info->bt_rx_rssi_percentage = pwdb_all_bt;
		phy_info->rx_power = rx_pwr_all;
		phy_info->recv_signal_power = rx_pwr_all;

		/* @(3)EVM of HT rate */
		for (i = 0; i < pktinfo->rate_ss; i++) {
		/* @Do not use shift operation like "rx_evmX >>= 1"
		 * because the compilor of free build environment
		 * fill most significant bit to "zero" when doing shifting
		 * operation which may change a negative
		 * value to positive one, then the dbm value
		 * (which is supposed to be negative) is not correct anymore.
		 */
			if (i >= PHYDM_MAX_RF_PATH_N)
				break;

			EVM = phydm_evm_2_percent(phy_sts->stream_rxevm[i]);

			/*@Fill value in RFD, Get the 1st spatial stream only*/
			if (i == RF_PATH_A)
				phy_info->signal_quality = (u8)(EVM & 0xff);

			phy_info->rx_mimo_signal_quality[i] = (u8)(EVM & 0xff);

			if (phy_sts->stream_rxevm[i] < 0)
				evm_tmp = 0 - phy_sts->stream_rxevm[i];

			if (evm_tmp == 64)
				evm_tmp = 0;

			phy_info->rx_mimo_evm_dbm[i] = (u8)evm_tmp;
		}
		phydm_parsing_cfo(dm, pktinfo,
				  phy_sts->path_cfotail, pktinfo->rate_ss);
	}

	#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
	dm->dm_fat_table.antsel_rx_keep_0 = phy_sts->ant_sel;
	dm->dm_fat_table.antsel_rx_keep_1 = phy_sts->ant_sel_b;
	dm->dm_fat_table.antsel_rx_keep_2 = phy_sts->antsel_rx_keep_2;
	#endif
}
#endif

#if ODM_IC_11AC_SERIES_SUPPORT
static s16
phydm_cfo(s8 value)
{
	s16 ret_val;

	if (value < 0) {
		ret_val = 0 - value;
		ret_val = (ret_val << 1) + (ret_val >> 1); /*@2.5~=312.5/2^7 */
		ret_val = ret_val | BIT(12); /*set bit12 as 1 for negative cfo*/
	} else {
		ret_val = value;
		ret_val = (ret_val << 1) + (ret_val >> 1); /* @*2.5~=312.5/2^7*/
	}
	return ret_val;
}

static u8
phydm_evm_dbm(s8 value)
{
	s8 ret_val = value;

	/* @-33dB~0dB to 33dB ~ 0dB */
	if (ret_val == -128)
		ret_val = 127;
	else if (ret_val < 0)
		ret_val = 0 - ret_val;

	ret_val = ret_val >> 1;
	return (u8)ret_val;
}

void phydm_rx_physts_bw_parsing(struct phydm_phyinfo_struct *phy_info,
				struct phydm_perpkt_info_struct *
				pktinfo,
				struct phy_status_rpt_8812 *
				phy_sts)
{
	if (pktinfo->data_rate > ODM_RATE54M) {
		switch (phy_sts->r_RFMOD) {
		case 1:
			if (phy_sts->sub_chnl == 0)
				phy_info->band_width = 1;
			else
				phy_info->band_width = 0;
			break;

		case 2:
			if (phy_sts->sub_chnl == 0)
				phy_info->band_width = 2;
			else if (phy_sts->sub_chnl == 9 ||
				 phy_sts->sub_chnl == 10)
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

void phydm_get_sq(struct dm_struct *dm, struct phydm_phyinfo_struct *phy_info,
		  u8 is_cck_rate)
{
	u8 sq = 0;
	u8 pwdb_all = phy_info->rx_pwdb_all; /*precentage*/
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	u8 rssi = phy_info->rx_mimo_signal_strength[0];
	#endif

	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if (dm->iot_table.win_patch_id == RT_CID_819X_LENOVO) {
		if (is_cck_rate)
			sq = phydm_sq_patch_lenovo(dm, 1, pwdb_all, 0, 0);
		else
			sq = phydm_sq_patch_lenovo(dm, 0, pwdb_all, 0, rssi);
	} else
	#endif
	{
		if (is_cck_rate) {
			if (pwdb_all > 40 && !dm->is_in_hct_test) {
				sq = 100;
			} else {
				if (pwdb_all > 64)
					sq = 0;
				else if (pwdb_all < 20)
					sq = 100;
				else
					sq = ((64 - pwdb_all) * 100) / 44;
			}
		} else {
			sq = phy_info->rx_mimo_signal_quality[0];
		}
	}

#if 0
	/* @dbg_print("cck sq = %d\n", sq); */
#endif
	phy_info->signal_quality = sq;
}

void phydm_rx_physts_1st_type(struct dm_struct *dm,
			      struct phydm_phyinfo_struct *phy_info,
			      u8 *phy_status_inf,
			      struct phydm_perpkt_info_struct *pktinfo)
{
	u8 i = 0;
	s8 rx_pwr_db = 0;
	u8 val = 0; /*tmp value*/
	s8 val_s8 = 0; /*tmp value*/
	u8 rssi = 0; /*pre path RSSI*/
	u8 rf_rx_num = 0;
	u8 lna_idx = 0, vga_idx = 0;
	u8 cck_agc_rpt = 0;
	struct phy_status_rpt_8812 *phy_sts = NULL;
	#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	#endif

	phy_sts = (struct phy_status_rpt_8812 *)phy_status_inf;
	phydm_rx_physts_bw_parsing(phy_info, pktinfo, phy_sts);

	/* @== [CCK rate] ====================================================*/
	if (pktinfo->is_cck_rate) {
		cck_agc_rpt = phy_sts->cfosho[0];
		lna_idx = (cck_agc_rpt & 0xE0) >> 5;
		vga_idx = cck_agc_rpt & 0x1F;

		rx_pwr_db = phydm_get_cck_rssi(dm, lna_idx, vga_idx);
		rssi = phydm_pw_2_percent(rx_pwr_db);

		if (dm->support_ic_type == ODM_RTL8812 &&
		    !dm->is_cck_high_power) {
			if (rssi >= 80) {
				rssi = ((rssi - 80) << 1) +
					   ((rssi - 80) >> 1) + 80;
			} else if ((rssi <= 78) && (rssi >= 20)) {
				rssi += 3;
			}
		}
		dm->cck_lna_idx = lna_idx;
		dm->cck_vga_idx = vga_idx;

		phy_info->rx_pwdb_all = rssi;
		phy_info->rx_mimo_signal_strength[0] = rssi;
	} else {
	/* @== [OFDM rate] ===================================================*/
		for (i = RF_PATH_A; i < dm->num_rf_path; i++) {
			/*@[RSSI]*/
			if (dm->rf_path_rx_enable & BIT(i))
				rf_rx_num++;

			if (i < RF_PATH_C)
				val = phy_sts->gain_trsw[i];
			else
				val = phy_sts->gain_trsw_cd[i - 2];

			phy_info->rx_pwr[i] = (val & 0x7F) - 110;
			rssi = phydm_pw_2_percent(phy_info->rx_pwr[i]);
			phy_info->rx_mimo_signal_strength[i] = rssi;

			/*@[SNR]*/
			if (i < RF_PATH_C)
				val_s8 = phy_sts->rxsnr[i];
			else if (dm->support_ic_type & (ODM_RTL8814A))
				val_s8 = (s8)phy_sts->csi_current[i - 2];

			phy_info->rx_snr[i] = val_s8 >> 1;

			/*@[CFO_short  & CFO_tail]*/
			if (i < RF_PATH_C) {
				val_s8 = phy_sts->cfosho[i];
				phy_info->cfo_short[i] = phydm_cfo(val_s8);
				val_s8 = phy_sts->cfotail[i];
				phy_info->cfo_tail[i] = phydm_cfo(val_s8);
			}

			if (i < RF_PATH_C && pktinfo->is_to_self)
				dm->ofdm_agc_idx[i] = phy_sts->gain_trsw[i];
		}

	/* @== [PWDB] ========================================================*/

		/*@(Avg PWDB calculated by hardware*/
		if (!dm->is_mp_chip) /*@8812, 8821*/
			val = phy_sts->pwdb_all;
		else
			val = phy_sts->pwdb_all >> 1; /*old fomula*/

		rx_pwr_db = (val & 0x7f) - 110;
		phy_info->rx_pwdb_all = phydm_pw_2_percent(rx_pwr_db);

		/*@(4)EVM of OFDM rate*/
		for (i = 0; i < pktinfo->rate_ss; i++) {
			if (!pktinfo->is_cck_rate &&
			    pktinfo->data_rate <= ODM_RATE54M) {
				val_s8 = phy_sts->sigevm;
			} else if (i < RF_PATH_C) {
				if (phy_sts->rxevm[i] == -128)
					phy_sts->rxevm[i] = -25;

				val_s8 = phy_sts->rxevm[i];
			} else {
				if (phy_sts->rxevm_cd[i - 2] == -128)
					phy_sts->rxevm_cd[i - 2] = -25;

				val_s8 = phy_sts->rxevm_cd[i - 2];
			}
			/*@[EVM to 0~100%]*/
			val = phydm_evm_2_percent(val_s8);
			phy_info->rx_mimo_signal_quality[i] = val;
			/*@[EVM dBm]*/
			phy_info->rx_mimo_evm_dbm[i] = phydm_evm_dbm(val_s8);
		}
		phydm_parsing_cfo(dm, pktinfo,
				  phy_sts->cfotail, pktinfo->rate_ss);
	}

	/* @== [General Info] ================================================*/

	phy_info->rx_power = rx_pwr_db;
	phy_info->bt_rx_rssi_percentage = phy_info->rx_pwdb_all;
	phy_info->recv_signal_power = phy_info->rx_power;
	phydm_get_sq(dm, phy_info, pktinfo->is_cck_rate);

	dm->rx_pwdb_ave = dm->rx_pwdb_ave + phy_info->rx_pwdb_all;

	#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
	fat_tab->hw_antsw_occur = phy_sts->hw_antsw_occur;
	dm->dm_fat_table.antsel_rx_keep_0 = phy_sts->antidx_anta;
	dm->dm_fat_table.antsel_rx_keep_1 = phy_sts->antidx_antb;
	dm->dm_fat_table.antsel_rx_keep_2 = phy_sts->antidx_antc;
	dm->dm_fat_table.antsel_rx_keep_3 = phy_sts->antidx_antd;
	#endif
}

#endif

void phydm_reset_rssi_for_dm(struct dm_struct *dm, u8 station_id)
{
	struct cmn_sta_info *sta;

	sta = dm->phydm_sta_info[station_id];

	if (!is_sta_active(sta))
		return;
	PHYDM_DBG(dm, DBG_RSSI_MNTR, "Reset RSSI for macid = (( %d ))\n",
		  station_id);

	sta->rssi_stat.rssi_cck = -1;
	sta->rssi_stat.rssi_ofdm = -1;
	sta->rssi_stat.rssi = -1;
	sta->rssi_stat.ofdm_pkt_cnt = 0;
	sta->rssi_stat.cck_pkt_cnt = 0;
	sta->rssi_stat.cck_sum_power = 0;
	sta->rssi_stat.is_send_rssi = RA_RSSI_STATE_INIT;
	sta->rssi_stat.packet_map = 0;
	sta->rssi_stat.valid_bit = 0;
}

#if (ODM_IC_11N_SERIES_SUPPORT || ODM_IC_11AC_SERIES_SUPPORT)

s32 phydm_get_rssi_8814_ofdm(struct dm_struct *dm, u8 *rssi_in)
{
	s32 rssi_avg;
	u8 rx_count = 0;
	u64 rssi_linear = 0;

	if (dm->rx_ant_status & BB_PATH_A) {
		rx_count++;
		rssi_linear += phydm_db_2_linear(rssi_in[RF_PATH_A]);
	}

	if (dm->rx_ant_status & BB_PATH_B) {
		rx_count++;
		rssi_linear += phydm_db_2_linear(rssi_in[RF_PATH_B]);
	}

	if (dm->rx_ant_status & BB_PATH_C) {
		rx_count++;
		rssi_linear += phydm_db_2_linear(rssi_in[RF_PATH_C]);
	}

	if (dm->rx_ant_status & BB_PATH_D) {
		rx_count++;
		rssi_linear += phydm_db_2_linear(rssi_in[RF_PATH_D]);
	}

	/* @Rounding and removing fractional bits */
	rssi_linear = (rssi_linear + (1 << (FRAC_BITS - 1))) >> FRAC_BITS;

	/* @Calculate average RSSI */
	switch (rx_count) {
	case 2:
		rssi_linear = DIVIDED_2(rssi_linear);
		break;
	case 3:
		rssi_linear = DIVIDED_3(rssi_linear);
		break;
	case 4:
		rssi_linear = DIVIDED_4(rssi_linear);
		break;
	}
	rssi_avg = odm_convert_to_db(rssi_linear);

	return rssi_avg;
}

void phydm_process_rssi_for_dm(struct dm_struct *dm,
			       struct phydm_phyinfo_struct *phy_info,
			       struct phydm_perpkt_info_struct *pktinfo)
{
	s32 rssi_ave = 0; /*@average among all paths*/
	s8 rssi_all = 0; /*@average value of CCK & OFDM*/
	s8 rssi_cck_tmp = 0, rssi_ofdm_tmp = 0;
	u8 i = 0;
	u8 rssi_max = 0, rssi_min = 0;
	u32 w1 = 0, w2 = 0; /*weighting*/
	u8 send_rssi_2_fw = 0;
	u8 *rssi_tmp = NULL;
	struct cmn_sta_info *sta = NULL;
	struct rssi_info *rssi_t = NULL;
	#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
	#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	struct phydm_fat_struct *fat_tab = &dm->dm_fat_table;
	#endif
	#endif

	if (pktinfo->station_id >= ODM_ASSOCIATE_ENTRY_NUM)
		return;

	#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
	odm_s0s1_sw_ant_div_by_ctrl_frame_process_rssi(dm, phy_info, pktinfo);
	#endif

	sta = dm->phydm_sta_info[pktinfo->station_id];

	if (!is_sta_active(sta))
		return;

	rssi_t = &sta->rssi_stat;

	#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
	#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	if ((dm->support_ability & ODM_BB_ANT_DIV) &&
	    fat_tab->enable_ctrl_frame_antdiv) {
		if (pktinfo->is_packet_match_bssid)
			dm->data_frame_num++;

		if (fat_tab->use_ctrl_frame_antdiv) {
			if (!pktinfo->is_to_self) /*@data frame + CTRL frame*/
				return;
		} else {
			/*@data frame only*/
			if (!pktinfo->is_packet_match_bssid)
				return;
		}
	} else
	#endif
	#endif
	{
		if (!pktinfo->is_packet_match_bssid) /*@data frame only*/
			return;
	}

	if (pktinfo->is_packet_beacon) {
		dm->phy_dbg_info.num_qry_beacon_pkt++;
		dm->phy_dbg_info.beacon_phy_rate = pktinfo->data_rate;
	}

	/* @--------------Statistic for antenna/path diversity--------------- */
	#ifdef ODM_EVM_ENHANCE_ANTDIV
	if (dm->antdiv_evm_en)
		phydm_rx_rate_for_antdiv(dm, pktinfo);
	#endif

	#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	if (dm->support_ability & ODM_BB_ANT_DIV)
		odm_process_rssi_for_ant_div(dm, phy_info, pktinfo);
	#endif

	#if (defined(CONFIG_PATH_DIVERSITY))
	if (dm->support_ability & ODM_BB_PATH_DIV)
		phydm_process_rssi_for_path_div(dm, phy_info, pktinfo);
	#endif
	/* @----------------------------------------------------------------- */

	rssi_cck_tmp = rssi_t->rssi_cck;
	rssi_ofdm_tmp = rssi_t->rssi_ofdm;
	rssi_all = rssi_t->rssi;

	if (!(pktinfo->is_packet_to_self || pktinfo->is_packet_beacon))
		return;

	if (!pktinfo->is_cck_rate) {
/* @=== [ofdm RSSI] ======================================================== */
		rssi_tmp = phy_info->rx_mimo_signal_strength;

		#if (RTL8814A_SUPPORT == 1)
		if (dm->support_ic_type & (ODM_RTL8814A)) {
			rssi_ave = phydm_get_rssi_8814_ofdm(dm, rssi_tmp);
		} else
		#endif
		{
			if (rssi_tmp[RF_PATH_B] == 0) {
				rssi_ave = rssi_tmp[RF_PATH_A];
			} else {
				if (rssi_tmp[RF_PATH_A] > rssi_tmp[RF_PATH_B]) {
					rssi_max = rssi_tmp[RF_PATH_A];
					rssi_min = rssi_tmp[RF_PATH_B];
				} else {
					rssi_max = rssi_tmp[RF_PATH_B];
					rssi_min = rssi_tmp[RF_PATH_A];
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

		/* OFDM MA RSSI */
		if (rssi_ofdm_tmp <= 0) { /* @initialize */
			rssi_ofdm_tmp = (s8)phy_info->rx_pwdb_all;
		} else {
			rssi_ofdm_tmp = (s8)WEIGHTING_AVG(rssi_ofdm_tmp,
							  (1 << RSSI_MA) - 1,
							  rssi_ave, 1);
			if (phy_info->rx_pwdb_all > (u32)rssi_ofdm_tmp)
				rssi_ofdm_tmp++;
		}

		PHYDM_DBG(dm, DBG_RSSI_MNTR, "rssi_ofdm=%d\n", rssi_ofdm_tmp);
	} else {
/* @=== [cck RSSI] ========================================================= */
		rssi_ave = phy_info->rx_pwdb_all;

		if (rssi_t->cck_pkt_cnt <= 63)
			rssi_t->cck_pkt_cnt++;

		/* @1 Process CCK RSSI */
		if (rssi_cck_tmp <= 0) { /* @initialize */
			rssi_cck_tmp = (s8)phy_info->rx_pwdb_all;
			rssi_t->cck_sum_power = (u16)phy_info->rx_pwdb_all;
			rssi_t->cck_pkt_cnt = 1; /*reset*/
			PHYDM_DBG(dm, DBG_RSSI_MNTR, "[1]CCK_INIT\n");
		} else if (rssi_t->cck_pkt_cnt <= CCK_RSSI_INIT_COUNT) {
			rssi_t->cck_sum_power = rssi_t->cck_sum_power +
						(u16)phy_info->rx_pwdb_all;

			rssi_cck_tmp = rssi_t->cck_sum_power /
				       rssi_t->cck_pkt_cnt;

			PHYDM_DBG(dm, DBG_RSSI_MNTR,
				  "[2]SumPow=%d, cck_pkt=%d\n",
				  rssi_t->cck_sum_power, rssi_t->cck_pkt_cnt);
		} else {
			rssi_cck_tmp = (s8)WEIGHTING_AVG(rssi_cck_tmp,
							 (1 << RSSI_MA) - 1,
							 phy_info->rx_pwdb_all,
							 1);
			if (phy_info->rx_pwdb_all > (u32)rssi_cck_tmp)
				rssi_cck_tmp++;
		}
		PHYDM_DBG(dm, DBG_RSSI_MNTR, "rssi_cck=%d\n", rssi_cck_tmp);
	}

/* @=== [ofdm + cck weighting RSSI] ========================================= */
	if (!pktinfo->is_cck_rate) {
		if (rssi_t->ofdm_pkt_cnt < 8 && !(rssi_t->packet_map & BIT(7)))
			rssi_t->ofdm_pkt_cnt++; /*OFDM packet cnt in bitmap*/

		rssi_t->packet_map = (rssi_t->packet_map << 1) | BIT(0);
	} else {
		if (rssi_t->ofdm_pkt_cnt > 0 && rssi_t->packet_map & BIT(7))
			rssi_t->ofdm_pkt_cnt--;

		rssi_t->packet_map = rssi_t->packet_map << 1;
	}

	if (rssi_t->ofdm_pkt_cnt == 8) {
		rssi_all = rssi_ofdm_tmp;
	} else {
		if (rssi_t->valid_bit < 8)
			rssi_t->valid_bit++;

		if (rssi_t->valid_bit == 8) {
			if (rssi_t->ofdm_pkt_cnt > 4)
				w1 = 64;
			else
				w1 = (u32)(rssi_t->ofdm_pkt_cnt << 4);

			w2 = 64 - w1;

			rssi_all = (s8)((w1 * (u32)rssi_ofdm_tmp +
					 w2 * (u32)rssi_cck_tmp) >> 6);
		} else if (rssi_t->valid_bit != 0) { /*@(valid_bit > 8)*/
			w1 = (u32)rssi_t->ofdm_pkt_cnt;
			w2 = (u32)(rssi_t->valid_bit - rssi_t->ofdm_pkt_cnt);
			rssi_all = (s8)WEIGHTING_AVG((u32)rssi_ofdm_tmp, w1,
						     (u32)rssi_cck_tmp, w2);
		} else {
			rssi_all = 0;
		}
	}
	PHYDM_DBG(dm, DBG_RSSI_MNTR, "rssi=%d,w1=%d,w2=%d\n", rssi_all, w1, w2);

	if ((rssi_t->ofdm_pkt_cnt >= 1 || rssi_t->cck_pkt_cnt >= 5) &&
	    rssi_t->is_send_rssi == RA_RSSI_STATE_INIT) {
		send_rssi_2_fw = 1;
		rssi_t->is_send_rssi = RA_RSSI_STATE_SEND;
	}

	rssi_t->rssi_cck = rssi_cck_tmp;
	rssi_t->rssi_ofdm = rssi_ofdm_tmp;
	rssi_t->rssi = rssi_all;

	if (send_rssi_2_fw) { /* Trigger init rate by RSSI */
		if (rssi_t->ofdm_pkt_cnt != 0)
			rssi_t->rssi = rssi_ofdm_tmp;

		PHYDM_DBG(dm, DBG_RSSI_MNTR,
			  "[Send to FW] PWDB=%d, ofdm_pkt=%d, cck_pkt=%d\n",
			  rssi_all, rssi_t->ofdm_pkt_cnt, rssi_t->cck_pkt_cnt);
	}

#if 0
	/* @dbg_print("ofdm_pkt=%d, weighting=%d\n", ofdm_pkt_cnt, weighting);*/
	/* @dbg_print("rssi_ofdm_tmp=%d, rssi_all=%d, rssi_cck_tmp=%d\n", */
	/*	rssi_ofdm_tmp, rssi_all, rssi_cck_tmp); */
#endif
}
#endif

#ifdef PHYSTS_3RD_TYPE_SUPPORT
#ifdef PHYDM_PHYSTAUS_AUTO_SWITCH
void phydm_physts_auto_switch_jgr3_reset(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct pkt_process_info *pkt_proc = &dm->pkt_proc_struct;

	pkt_proc->phy_ppdu_cnt = 0xff;
	pkt_proc->mac_ppdu_cnt = 0xff;
	pkt_proc->page_bitmap_record = 0;
}

boolean phydm_physts_auto_switch_jgr3(void *dm_void, u8 *phy_sts,
				      struct phydm_perpkt_info_struct *pktinfo)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct pkt_process_info *pkt_proc = &dm->pkt_proc_struct;
	boolean is_skip_physts_parsing = false;
	u8 phy_sts_byte0 = (*phy_sts & 0xff);
	u8 phy_ppdu_cnt_pre = 0, mac_ppdu_cnt_pre = 0;
	u8 ppdu_phy_rate_pre = 0, ppdu_macid_pre = 0;
	u8 page = phy_sts_byte0 & 0xf;

	if (!pkt_proc->physts_auto_swch_en)
		return is_skip_physts_parsing;

	phy_ppdu_cnt_pre = pkt_proc->phy_ppdu_cnt;
	mac_ppdu_cnt_pre = pkt_proc->mac_ppdu_cnt;
	ppdu_phy_rate_pre = pkt_proc->ppdu_phy_rate;
	ppdu_macid_pre = pkt_proc->ppdu_macid;

	pkt_proc->phy_ppdu_cnt = (phy_sts_byte0 & 0x30) >> 4;
	pkt_proc->mac_ppdu_cnt = pktinfo->ppdu_cnt;
	pkt_proc->ppdu_phy_rate = pktinfo->data_rate;
	pkt_proc->ppdu_macid = pktinfo->station_id;

	PHYDM_DBG(dm, DBG_PHY_STATUS,
		  "[rate:0x%x] PPDU mac{pre, curr}= {%d, %d}, phy{pre, curr}= {%d, %d}\n",
		  pktinfo->data_rate, mac_ppdu_cnt_pre, pkt_proc->mac_ppdu_cnt,
		  phy_ppdu_cnt_pre, pkt_proc->phy_ppdu_cnt);

	if (pktinfo->data_rate < ODM_RATEMCS0) {
		pkt_proc->page_bitmap_record = 0;
		return is_skip_physts_parsing;
	}

	if (ppdu_macid_pre == pkt_proc->ppdu_macid &&
	    ppdu_phy_rate_pre == pkt_proc->ppdu_phy_rate &&
	    phy_ppdu_cnt_pre == pkt_proc->phy_ppdu_cnt &&
	    mac_ppdu_cnt_pre == pkt_proc->mac_ppdu_cnt) {
		if (pkt_proc->page_bitmap_record & BIT(page)) {
			/*@PHYDM_DBG(dm, DBG_PHY_STATUS, "collect page-%d enough\n", page);*/
			is_skip_physts_parsing = true;
		} else if (pkt_proc->page_bitmap_record ==
			   pkt_proc->page_bitmap_target) {
			/*@PHYDM_DBG(dm, DBG_PHY_STATUS, "collect all enough\n");*/
			is_skip_physts_parsing = true;
		} else {
			/*@PHYDM_DBG(dm, DBG_PHY_STATUS, "update page-%d\n", page);*/
			pkt_proc->page_bitmap_record |= BIT(page);
		}
		pkt_proc->is_1st_mpdu = false;
	} else {
		/*@PHYDM_DBG(dm, DBG_PHY_STATUS, "[New Pkt] update page-%d\n", page);*/
		pkt_proc->page_bitmap_record = BIT(page);
		pkt_proc->is_1st_mpdu = true;
	}

	PHYDM_DBG(dm, DBG_PHY_STATUS,
		  "bitmap{record, target}= {0x%x, 0x%x}\n",
		  pkt_proc->page_bitmap_record,
		  pkt_proc->page_bitmap_target);

	return is_skip_physts_parsing;
}

void phydm_physts_auto_switch_jgr3_set(void *dm_void, boolean enable,
				       u8 bitmap_en)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct pkt_process_info *pkt_proc = &dm->pkt_proc_struct;
	u16 en_page_num = 1;

	if (!(dm->support_ic_type & PHYSTS_AUTO_SWITCH_IC))
		return;
#if 0
	if (!(dm->support_ic_type & PHYSTS_3RD_TYPE_IC))
		return;
#endif
	pkt_proc->physts_auto_swch_en = enable;
	pkt_proc->page_bitmap_target = bitmap_en;
	phydm_physts_auto_switch_jgr3_reset(dm);
	en_page_num = phydm_ones_num_in_bitmap((u64)bitmap_en, 8);

	PHYDM_DBG(dm, DBG_CMN, "[%s]en=%d, bitmap_en=%d, en_page_num=%d\n",
		  __func__, enable, bitmap_en, en_page_num);

	if (enable) {
		/*@per MPDU latch & update phy-staatus*/
		odm_set_mac_reg(dm, R_0x60c, BIT(31), 1);
		/*@Update Period (OFDM Symbol)*/
		odm_set_bb_reg(dm, R_0x8c0, 0xfc000, 3);
		/*@switchin bitmap*/
		odm_set_bb_reg(dm, R_0x8c4, 0x7f80000, bitmap_en);
		/*@mode 3*/
		odm_set_bb_reg(dm, R_0x8c4, (BIT(28) | BIT(27)), 3);
	} else {
		odm_set_mac_reg(dm, R_0x60c, BIT(31), 0);
		odm_set_bb_reg(dm, R_0x8c0, 0xfc000, 0x1);
		odm_set_bb_reg(dm, R_0x8c4, 0x7f80000, 0x2);
		odm_set_bb_reg(dm, R_0x8c4, (BIT(28) | BIT(27)), 0);
	}
}

void phydm_avg_condi_num(void *dm_void,
			 struct phydm_phyinfo_struct *phy_info,
			 struct phydm_perpkt_info_struct *pktinfo)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_phy_dbg_info *dbg_i = &dm->phy_dbg_info;
	struct phydm_phystatus_statistic *dbg_s = &dbg_i->physts_statistic_info;
	u16 size_th = PHY_HIST_SIZE - 1; /*size of threshold*/
	u16 val = 0, intvl = 0;
	u8 arry_idx = 0;

	if (pktinfo->rate_ss == 1)
		return;

	arry_idx = pktinfo->rate_ss - 1;

	dbg_s->p4_cnt[arry_idx]++;
	dbg_s->cn_sum[arry_idx] += dbg_i->condition_num_seg0;

	/*CN Histogram*/
	val = (u16)dbg_i->condition_num_seg0;
	intvl = phydm_find_intrvl(dm, val, dbg_i->cn_hist_th, size_th);
	dbg_s->cn_hist[arry_idx][intvl]++;

	dbg_i->condi_num = (u32)dbg_i->condition_num_seg0; /*will remove*/
}
#endif

void phydm_print_phystat_jgr3(struct dm_struct *dm, u8 *phy_sts,
			      struct phydm_perpkt_info_struct *pktinfo,
			      struct phydm_phyinfo_struct *phy_info)
{
	struct phy_sts_rpt_jgr3_type0 *rpt0 = NULL;
	struct phy_sts_rpt_jgr3_type1 *rpt1 = NULL;
	struct phy_sts_rpt_jgr3_type2_3 *rpt2 = NULL;
	struct phy_sts_rpt_jgr3_type4 *rpt3 = NULL;
	struct phy_sts_rpt_jgr3_type5 *rpt4 = NULL;
	struct phy_sts_rpt_jgr3_type6 *rpt5 = NULL;
	
	struct odm_phy_dbg_info *dbg = &dm->phy_dbg_info;
	u8 phy_status_page_num = (*phy_sts & 0xf);
	u32 *phy_status_tmp = NULL;
	u8 i = 0;
	/*u32 size = PHY_STATUS_JRGUAR3_DW_LEN << 2;*/

	if (!(dm->debug_components & DBG_PHY_STATUS))
		return;

	rpt0 = (struct phy_sts_rpt_jgr3_type0 *)phy_sts;
	rpt1 = (struct phy_sts_rpt_jgr3_type1 *)phy_sts;
	rpt2 = (struct phy_sts_rpt_jgr3_type2_3 *)phy_sts;
	rpt3 = (struct phy_sts_rpt_jgr3_type4 *)phy_sts;
	rpt4 = (struct phy_sts_rpt_jgr3_type5 *)phy_sts;

	if (dm->support_ic_type & ODM_RTL8723F) {
		rpt5 = (struct phy_sts_rpt_jgr3_type6 *)phy_sts;
		
		if (pktinfo->is_cck_rate)
			phy_status_page_num  = 0;
	}

	phy_status_tmp = (u32 *)phy_sts;

	if (dbg->show_phy_sts_all_pkt == 0) {
		if (!pktinfo->is_packet_match_bssid)
			return;
	}

	dbg->show_phy_sts_cnt++;

	if (dbg->show_phy_sts_max_cnt != SHOW_PHY_STATUS_UNLIMITED) {
		if (dbg->show_phy_sts_cnt > dbg->show_phy_sts_max_cnt)
			return;
	}

	if (phy_status_page_num == 0)
		pr_debug("Phy Status Rpt: CCK\n");
	else
		pr_debug("Phy Status Rpt: OFDM_%d\n", phy_status_page_num);

	pr_debug("StaID=%d, RxRate = 0x%x match_bssid=%d, ppdu_cnt=%d\n",
		 pktinfo->station_id, pktinfo->data_rate,
		 pktinfo->is_packet_match_bssid, pktinfo->ppdu_cnt);

	for (i = 0; i < PHY_STATUS_JRGUAR3_DW_LEN; i++)
		pr_debug("Offset[%d:%d] = 0x%x\n",
			 ((4 * i) + 3), (4 * i), phy_status_tmp[i]);

	if (phy_status_page_num == 0) { /* @CCK(default) */
		if (dm->support_ic_type & ODM_RTL8723F) {
			#if (RTL8723F_SUPPORT)
			pr_debug("[0] Pop_idx=%d, Pkt_cnt=%d, Channel_msb=%d, AGC_table_path0=%d, TRSW_mux_keep=%d, HW_AntSW_occur_keep_cck=%d, Gnt_BT_keep_cnt=%d,Rssi_msb=%d\n",
				 rpt5->pop_idx, rpt5->pkt_cnt,
				 rpt5->channel_msb, rpt5->agc_table_a,
				 rpt5->trsw, rpt5->hw_antsw_occur_keep_cck,
				 rpt5->gnt_bt_keep_cck, rpt5->rssi_msb);
			pr_debug("[4] Channel=%d, Antidx_CCK_keep=%d, Cck_mp_gain_idx_keep=%d\n",
				 rpt5->channel, rpt5->antidx_a,
				 rpt5->mp_gain_idx_a);
			pr_debug("[8] Rssi=%d\n",rpt5->rssi);
			pr_debug("[12] Avg_cfo=%d\n",rpt5->avg_cfo);
			pr_debug("[16] Coarse_cfo=%d, Coarse_cfo_msb=%d, Avg_cfo_msb=%d, Evm_hdr=%d\n",
				 rpt5->coarse_cfo, rpt5->coarse_cfo_msb,
				 rpt5->avg_cfo_msb, rpt5->evm_hdr);
			pr_debug("[20] Evm_pld=%d\n",rpt5->evm_pld);
			#endif
		} else {
			pr_debug("[0] Pkt_cnt=%d, Channel_msb=%d, Pwdb_a=%d, Gain_a=%d, TRSW=%d, AGC_table_b=%d, AGC_table_c=%d,\n",
				 rpt0->pkt_cnt, rpt0->channel_msb, rpt0->pwdb_a,
				 rpt0->gain_a, rpt0->trsw, rpt0->agc_table_b,
				 rpt0->agc_table_c);
			pr_debug("[4] Path_Sel_o=%d, Gnt_BT_keep_cnt=%d, HW_AntSW_occur_keep_cck=%d,\n Band=%d, Channel=%d, AGC_table_a=%d, l_RXSC=%d, AGC_table_d=%d\n",
				 rpt0->path_sel_o, rpt0->gnt_bt_keep_cck,
				 rpt0->hw_antsw_occur_keep_cck, rpt0->band,
				 rpt0->channel, rpt0->agc_table_a, rpt0->l_rxsc,
				 rpt0->agc_table_d);
			pr_debug("[8] AntIdx={%d, %d, %d, %d}, Length=%d\n",
				 rpt0->antidx_d, rpt0->antidx_c, rpt0->antidx_b,
				 rpt0->antidx_a, rpt0->length);
			pr_debug("[12] MF_off=%d, SQloss=%d, lockbit=%d, raterr=%d, rxrate=%d, lna_h_a=%d, CCK_BB_power_a=%d, lna_l_a=%d, vga_a=%d, sq=%d\n",
				 rpt0->mf_off, rpt0->sqloss, rpt0->lockbit,
				 rpt0->raterr, rpt0->rxrate, rpt0->lna_h_a,
				 rpt0->bb_power_a, rpt0->lna_l_a, rpt0->vga_a,
				 rpt0->signal_quality);
			pr_debug("[16] Gain_b=%d, lna_h_b=%d, CCK_BB_power_b=%d, lna_l_b=%d, vga_b=%d, Pwdb_b=%d\n",
				 rpt0->gain_b, rpt0->lna_h_b, rpt0->bb_power_b,
				 rpt0->lna_l_b, rpt0->vga_b, rpt0->pwdb_b);
			pr_debug("[20] Gain_c=%d, lna_h_c=%d, CCK_BB_power_c=%d, lna_l_c=%d, vga_c=%d, Pwdb_c=%d\n",
				 rpt0->gain_c, rpt0->lna_h_c, rpt0->bb_power_c,
				 rpt0->lna_l_c, rpt0->vga_c, rpt0->pwdb_c);
			pr_debug("[24] Gain_d=%d, lna_h_d=%d, CCK_BB_power_d=%d, lna_l_d=%d, vga_d=%d, Pwdb_d=%d\n",
				 rpt0->gain_c, rpt0->lna_h_c, rpt0->bb_power_c,
				 rpt0->lna_l_c, rpt0->vga_c, rpt0->pwdb_c);
		}
	} else if (phy_status_page_num == 1) {
		pr_debug("[0] pwdb[C:A]={%d, %d, %d}, Channel_pri_msb=%d, Pkt_cnt=%d,\n",
			 rpt1->pwdb_c, rpt1->pwdb_b, rpt1->pwdb_a,
			 rpt1->channel_pri_msb, rpt1->pkt_cnt);
		pr_debug("[4] BF: %d, stbc=%d, ldpc=%d, gnt_bt=%d, band=%d, Ch_pri_lsb=%d, rxsc[ht, l]={%d, %d}, pwdb[D]=%d\n",
			 rpt1->beamformed, rpt1->stbc, rpt1->ldpc, rpt1->gnt_bt,
			 rpt1->band, rpt1->channel_pri_lsb, rpt1->ht_rxsc,
			 rpt1->l_rxsc, rpt1->pwdb_d);
		pr_debug("[8] AntIdx[D:A]={%d, %d, %d, %d}, HW_AntSW_occur[D:A]={%d, %d, %d, %d}, Channel_sec[msb,lsb]={%d, %d}\n",
			 rpt1->antidx_d, rpt1->antidx_c,
			 rpt1->antidx_b, rpt1->antidx_a,
			 rpt1->hw_antsw_occur_d, rpt1->hw_antsw_occur_c,
			 rpt1->hw_antsw_occur_b, rpt1->hw_antsw_occur_a,
			 rpt1->channel_sec_msb, rpt1->channel_sec_lsb);
		pr_debug("[12] GID=%d, PAID[msb,lsb]={%d,%d}\n",
			 rpt1->gid, rpt1->paid_msb, rpt1->paid);
		pr_debug("[16] RX_EVM[D:A]={%d, %d, %d, %d}\n",
			 rpt1->rxevm[3], rpt1->rxevm[2],
			 rpt1->rxevm[1], rpt1->rxevm[0]);
		pr_debug("[20] CFO_tail[D:A]={%d, %d, %d, %d}\n",
			 rpt1->cfo_tail[3], rpt1->cfo_tail[2],
			 rpt1->cfo_tail[1], rpt1->cfo_tail[0]);
		pr_debug("[24] RX_SNR[D:A]={%d, %d, %d, %d}\n\n",
			 rpt1->rxsnr[3], rpt1->rxsnr[2],
			 rpt1->rxsnr[1], rpt1->rxsnr[0]);
	} else if (phy_status_page_num == 2 || phy_status_page_num == 3) {
		pr_debug("[0] pwdb[C:A]={%d, %d, %d}, Channel_mdb=%d, Pkt_cnt=%d\n",
			 rpt2->pwdb[2], rpt2->pwdb[1], rpt2->pwdb[0],
			 rpt2->channel_msb, rpt2->pkt_cnt);
		pr_debug("[4] BF=%d, STBC=%d, LDPC=%d, Gnt_BT=%d, band=%d, CH_lsb=%d, rxsc[ht, l]={%d, %d}, pwdb_D=%d\n",
			 rpt2->beamformed, rpt2->stbc, rpt2->ldpc, rpt2->gnt_bt,
			 rpt2->band, rpt2->channel_lsb,
			 rpt2->ht_rxsc, rpt2->l_rxsc, rpt2->pwdb[3]);
		pr_debug("[8] AgcTab[D:A]={%d, %d, %d, %d}, pwed_th=%d, shift_l_map=%d\n",
			 rpt2->agc_table_d, rpt2->agc_table_c,
			 rpt2->agc_table_b, rpt2->agc_table_a,
			 rpt2->pwed_th, rpt2->shift_l_map);
		pr_debug("[12] AvgNoisePowerdB=%d, mp_gain_c[msb, lsb]={%d, %d}, mp_gain_b[msb, lsb]={%d, %d}, mp_gain_a=%d, cnt_cca2agc_rdy=%d\n",
			 rpt2->avg_noise_pwr_lsb, rpt2->mp_gain_c_msb,
			 rpt2->mp_gain_c_lsb, rpt2->mp_gain_b_msb,
			 rpt2->mp_gain_b_lsb, rpt2->mp_gain_a,
			 rpt2->cnt_cca2agc_rdy);
		pr_debug("[16] HT AAGC gain[B:A]={%d, %d}, AAGC step[D:A]={%d, %d, %d, %d}, IsFreqSelectFadimg=%d, mp_gain_d=%d\n",
			 rpt2->ht_aagc_gain[1], rpt2->ht_aagc_gain[0],
			 rpt2->aagc_step_d, rpt2->aagc_step_c,
			 rpt2->aagc_step_b, rpt2->aagc_step_a,
			 rpt2->is_freq_select_fading, rpt2->mp_gain_d);
		pr_debug("[20] DAGC gain ant[B:A]={%d, %d}, HT AAGC gain[D:C]={%d, %d}\n",
			 rpt2->dagc_gain[1], rpt2->dagc_gain[0],
			 rpt2->ht_aagc_gain[3], rpt2->ht_aagc_gain[2]);
		pr_debug("[24] AvgNoisePwerdB=%d, syn_count[msb, lsb]={%d, %d}, counter=%d, DAGC gain ant[D:C]={%d, %d}\n",
			 rpt2->avg_noise_pwr_msb, rpt2->syn_count_msb,
			 rpt2->syn_count_lsb, rpt2->counter,
			 rpt2->dagc_gain[3], rpt2->dagc_gain[2]);
	} else if (phy_status_page_num == 4) { /*type 4*/
		pr_debug("[0] pwdb[C:A]={%d, %d, %d}, Channel_mdb=%d, Pkt_cnt=%d\n",
			 rpt3->pwdb[2], rpt3->pwdb[1], rpt3->pwdb[0],
			 rpt3->channel_msb, rpt3->pkt_cnt);
		pr_debug("[4] BF=%d, STBC=%d, LDPC=%d, GNT_BT=%d, band=%d, CH_pri=%d, rxsc[ht, l]={%d, %d}, pwdb_D=%d\n",
			 rpt3->beamformed, rpt3->stbc, rpt3->ldpc, rpt3->gnt_bt,
			 rpt3->band, rpt3->channel_lsb, rpt3->ht_rxsc,
			 rpt3->l_rxsc, rpt3->pwdb[3]);
		pr_debug("[8] AntIdx[D:A]={%d, %d, %d, %d}, HW_AntSW_occur[D:A]={%d, %d, %d, %d}, Training_done[D:A]={%d, %d, %d, %d},\n    BadToneCnt_CN_excess_0=%d, BadToneCnt_min_eign_0=%d\n",
			 rpt3->antidx_d, rpt3->antidx_c,
			 rpt3->antidx_b, rpt3->antidx_a,
			 rpt3->hw_antsw_occur_d, rpt3->hw_antsw_occur_c,
			 rpt3->hw_antsw_occur_b, rpt3->hw_antsw_occur_a,
			 rpt3->training_done_d, rpt3->training_done_c,
			 rpt3->training_done_b, rpt3->training_done_a,
			 rpt3->bad_tone_cnt_cn_excess_0,
			 rpt3->bad_tone_cnt_min_eign_0);
		pr_debug("[12] avg_cond_num_1=%d, avg_cond_num_0=%d, bad_tone_cnt_cn_excess_1=%d,\n     bad_tone_cnt_min_eign_1=%d, Tx_pkt_cnt=%d\n",
			 ((rpt3->avg_cond_num_1_msb << 1) |
			 rpt3->avg_cond_num_1_lsb),
			 rpt3->avg_cond_num_0, rpt3->bad_tone_cnt_cn_excess_1,
			 rpt3->bad_tone_cnt_min_eign_1, rpt3->tx_pkt_cnt);
		pr_debug("[16] Stream RXEVM[D:A]={%d, %d, %d, %d}\n",
			 rpt3->rxevm[3], rpt3->rxevm[2],
			 rpt3->rxevm[1], rpt3->rxevm[0]);
		pr_debug("[20] Eigenvalue[D:A]={%d, %d, %d, %d}\n",
			 rpt3->eigenvalue[3], rpt3->eigenvalue[2],
			 rpt3->eigenvalue[1], rpt3->eigenvalue[0]);
		pr_debug("[24] RX SNR[D:A]={%d, %d, %d, %d}\n",
			 rpt3->rxsnr[3], rpt3->rxsnr[2],
			 rpt3->rxsnr[1], rpt3->rxsnr[0]);
	} else if (phy_status_page_num == 5) { /*type 5*/
		pr_debug("[0] pwdb[C:A]={%d, %d, %d}, Channel_mdb=%d, Pkt_cnt=%d\n",
			 rpt4->pwdb[2], rpt4->pwdb[1], rpt4->pwdb[0],
			 rpt4->channel_msb, rpt4->pkt_cnt);
		pr_debug("[4] BF=%d, STBC=%d, LDPC=%d, GNT_BT=%d, band=%d, CH_pri=%d, rxsc[ht, l]={%d, %d}, pwdb_D=%d\n",
			 rpt4->beamformed, rpt4->stbc, rpt4->ldpc, rpt4->gnt_bt,
			 rpt4->band, rpt4->channel_lsb, rpt4->ht_rxsc,
			 rpt4->l_rxsc, rpt4->pwdb[3]);
		pr_debug("[8] AntIdx[D:A]={%d, %d, %d, %d}, HW_AntSW_occur[D:A]={%d, %d, %d, %d}\n",
			 rpt4->antidx_d, rpt4->antidx_c,
			 rpt4->antidx_b, rpt4->antidx_a,
			 rpt4->hw_antsw_occur_d, rpt4->hw_antsw_occur_c,
			 rpt4->hw_antsw_occur_b, rpt4->hw_antsw_occur_a);
		pr_debug("[12] Inf_posD[1,0]={%d, %d}, Inf_posC[1,0]={%d, %d}, Inf_posB[1,0]={%d, %d}, Inf_posA[1,0]={%d, %d}, Tx_pkt_cnt=%d\n",
			 rpt4->inf_pos_1_D_flg, rpt4->inf_pos_0_D_flg,
			 rpt4->inf_pos_1_C_flg, rpt4->inf_pos_0_C_flg,
			 rpt4->inf_pos_1_B_flg, rpt4->inf_pos_0_B_flg,
			 rpt4->inf_pos_1_A_flg, rpt4->inf_pos_0_A_flg,
			 rpt4->tx_pkt_cnt);
		pr_debug("[16] Inf_pos_B[1,0]={%d, %d}, Inf_pos_A[1,0]={%d, %d}\n",
			 rpt4->inf_pos_1_b, rpt4->inf_pos_0_b,
			 rpt4->inf_pos_1_a, rpt4->inf_pos_0_a);
		pr_debug("[20] Inf_pos_D[1,0]={%d, %d}, Inf_pos_C[1,0]={%d, %d}\n",
			 rpt4->inf_pos_1_d, rpt4->inf_pos_0_d,
			 rpt4->inf_pos_1_c, rpt4->inf_pos_0_c);
	}
}

void phydm_reset_phy_info_jgr3(struct dm_struct *phydm,
			       struct phydm_phyinfo_struct *phy_info)
{
	u8 i;

	phy_info->rx_pwdb_all = 0;
	phy_info->signal_quality = 0;
	phy_info->band_width = 0;
	phy_info->rx_count = 0;
	phy_info->rx_power = -110;
	phy_info->recv_signal_power = -110;
	phy_info->bt_rx_rssi_percentage = 0;
	phy_info->signal_strength = 0;
	phy_info->channel = 0;
	phy_info->is_mu_packet = 0;
	phy_info->is_beamformed = 0;
	phy_info->rxsc = 0;

	for (i = 0; i < 4; i++) {
		phy_info->rx_mimo_signal_strength[i] = 0;
		phy_info->rx_mimo_signal_quality[i] = 0;
		phy_info->rx_mimo_evm_dbm[i] = 0;
		phy_info->cfo_short[i] = 0;
		phy_info->cfo_tail[i] = 0;
		phy_info->rx_pwr[i] = -110;
		phy_info->rx_snr[i] = 0;
	}
}

#if 0
void phydm_per_path_info_3rd(u8 rx_path, s8 pwr, s8 rx_evm, s8 cfo_tail,
			     s8 rx_snr, struct phydm_phyinfo_struct *phy_info)
{
	u8 evm_dbm = 0;
	u8 evm_percentage = 0;

	/* SNR is S(8,1), EVM is S(8,1), CFO is S(8,7) */

	evm_dbm = (rx_evm == -128) ? 0 : ((u8)(0 - rx_evm) >> 1);
	evm_percentage = (evm_dbm >= 34) ? 100 : evm_dbm * 3;

	phy_info->rx_pwr[rx_path] = pwr;

	/*@CFO(kHz) = CFO_tail * 312.5(kHz) / 2^7 ~= CFO tail * 5/2 (kHz)*/
	phy_info->cfo_tail[rx_path] = (cfo_tail * 5) >> 1;
	phy_info->rx_mimo_evm_dbm[rx_path] = evm_dbm;
	phy_info->rx_mimo_signal_strength[rx_path] = phydm_pw_2_percent(pwr);
	phy_info->rx_mimo_signal_quality[rx_path] = evm_percentage;
	phy_info->rx_snr[rx_path] = rx_snr >> 1;
}

void phydm_common_phy_info_jgr3(s8 rx_power, u8 channel, boolean is_beamformed,
				boolean is_mu_packet, u8 bandwidth,
				u8 signal_quality, u8 rxsc,
				struct phydm_phyinfo_struct *phy_info)
{
	phy_info->rx_power = rx_power; /* RSSI in dB */
	phy_info->recv_signal_power = rx_power; /* RSSI in dB */
	phy_info->channel = channel; /* @channel number */
	phy_info->is_beamformed = is_beamformed; /* @apply BF */
	phy_info->is_mu_packet = is_mu_packet; /* @MU packet */
	phy_info->rxsc = rxsc;

	phy_info->rx_pwdb_all = phydm_pw_2_percent(rx_power); /*percentage */
	phy_info->signal_quality = signal_quality; /* signal quality */
	phy_info->band_width = bandwidth; /* @bandwidth */
}
#endif

void phydm_get_physts_0_jgr3(struct dm_struct *dm, u8 *phy_status_inf,
			     struct phydm_perpkt_info_struct *pktinfo,
			     struct phydm_phyinfo_struct *phy_info)
{
	/* type 0 is used for cck packet */
	struct phy_sts_rpt_jgr3_type0 *phy_sts = NULL;
	struct odm_phy_dbg_info *dbg_i = &dm->phy_dbg_info;
	u8 sq = 0, i, rx_cnt = 0;
	s8 rx_power[4], pwdb;
	s8 rx_pwr_db_max = -120;

	phy_sts = (struct phy_sts_rpt_jgr3_type0 *)phy_status_inf;

	#if (RTL8197G_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8197G) {
		if (dm->rx_ant_status == BB_PATH_B) {
			phy_sts->pwdb_b = phy_sts->pwdb_a;
			phy_sts->gain_b = phy_sts->gain_a;
			phy_sts->pwdb_a = 0;
			phy_sts->gain_a = 0;
		}
	}
	#endif

	rx_power[0] = phy_sts->pwdb_a;
	rx_power[1] = phy_sts->pwdb_b;
	rx_power[2] = phy_sts->pwdb_c;
	rx_power[3] = phy_sts->pwdb_d;

	#if (RTL8822C_SUPPORT || RTL8197G_SUPPORT)
	if (dm->support_ic_type & (ODM_RTL8822C | ODM_RTL8197G)) {
		struct phydm_physts *physts_table = &dm->dm_physts_table;
		if (phy_sts->gain_a < physts_table->cck_gi_l_bnd)
			rx_power[0] += ((physts_table->cck_gi_l_bnd -
					phy_sts->gain_a) << 1);
		else if (phy_sts->gain_a > physts_table->cck_gi_u_bnd)
			rx_power[0] -= ((phy_sts->gain_a -
					physts_table->cck_gi_u_bnd) << 1);

		if (phy_sts->gain_b < physts_table->cck_gi_l_bnd)
			rx_power[1] += ((physts_table->cck_gi_l_bnd -
					phy_sts->gain_b) << 1);
		else if (phy_sts->gain_b > physts_table->cck_gi_u_bnd)
			rx_power[1] -= ((phy_sts->gain_b -
					physts_table->cck_gi_u_bnd) << 1);
	}
	#endif

	/* @Update per-path information */
	for (i = RF_PATH_A; i < dm->num_rf_path; i++) {
		if ((dm->rx_ant_status & BIT(i)) == 0)
			continue;

		rx_cnt++; /* @check the number of the ant */

		if (rx_cnt > dm->num_rf_path)
			break;

		if (pktinfo->is_to_self)
			dm->ofdm_agc_idx[i] = rx_power[i];

		/* @Setting the RX power: agc_idx -110 dBm*/
		pwdb = rx_power[i] - 110;

		phy_info->rx_pwr[i] = pwdb;
		phy_info->rx_mimo_signal_strength[i] = phydm_pw_2_percent(pwdb);

		/* search maximum pwdb */
		if (pwdb > rx_pwr_db_max)
			rx_pwr_db_max = pwdb;
	}

	/* @Calculate Signal Quality*/
	if (phy_sts->signal_quality >= 64) {
		sq = 0;
	} else if (phy_sts->signal_quality <= 20) {
		sq = 100;
	} else {
		/* @mapping to 2~99% */
		sq = 64 - phy_sts->signal_quality;
		sq = ((sq << 3) + sq) >> 2;
	}

	/* @Modify CCK PWDB if old AGC */
	if (!dm->cck_new_agc) {
		u8 lna_idx[4], vga_idx[4];

		lna_idx[0] = ((phy_sts->lna_h_a << 3) | phy_sts->lna_l_a);
		vga_idx[0] = phy_sts->vga_a;
		lna_idx[1] = ((phy_sts->lna_h_b << 3) | phy_sts->lna_l_b);
		vga_idx[1] = phy_sts->vga_b;
		lna_idx[2] = ((phy_sts->lna_h_c << 3) | phy_sts->lna_l_c);
		vga_idx[2] = phy_sts->vga_c;
		lna_idx[3] = ((phy_sts->lna_h_d << 3) | phy_sts->lna_l_d);
		vga_idx[3] = phy_sts->vga_d;
	}

	/*@CCK no STBC and LDPC*/
	dbg_i->is_ldpc_pkt = false;
	dbg_i->is_stbc_pkt = false;

	/*cck channel has hw bug, [WLANBB-1429]*/
	phy_info->channel = 0;
	phy_info->rx_power = rx_pwr_db_max;
	phy_info->recv_signal_power = rx_pwr_db_max;
	phy_info->is_beamformed = false;
	phy_info->is_mu_packet = false;
	phy_info->rxsc = phy_sts->l_rxsc;
	phy_info->rx_pwdb_all = phydm_pw_2_percent(rx_pwr_db_max);
	phy_info->signal_quality = sq;
	phy_info->band_width = CHANNEL_WIDTH_20;

	#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
	dm->dm_fat_table.antsel_rx_keep_0 = phy_sts->antidx_a;
	dm->dm_fat_table.antsel_rx_keep_1 = phy_sts->antidx_b;
	dm->dm_fat_table.antsel_rx_keep_2 = phy_sts->antidx_c;
	dm->dm_fat_table.antsel_rx_keep_3 = phy_sts->antidx_d;
	#endif
}

void phydm_get_physts_1_others_jgr3(struct dm_struct *dm, u8 *phy_status_inf,
				    struct phydm_perpkt_info_struct *pktinfo,
				    struct phydm_phyinfo_struct *phy_info)
{
	struct phy_sts_rpt_jgr3_type1 *phy_sts = NULL;
	struct odm_phy_dbg_info *dbg_i = &dm->phy_dbg_info;
	s8 evm = 0;
	u8 i;
	s8 sq = 0;

	phy_sts = (struct phy_sts_rpt_jgr3_type1 *)phy_status_inf;

	/* SNR: S(8,1), EVM: S(8,1), CFO: S(8,7) */

	for (i = RF_PATH_A; i < dm->num_rf_path; i++) {
		if ((dm->rx_ant_status & BIT(i)) == 0)
			continue;

		evm = phy_sts->rxevm[i];
		evm = (evm == -128) ? 0 : ((0 - evm) >> 1);
		sq = (evm >= 34) ? 100 : evm * 3; /* @Convert EVM to 0~100%*/

		phy_info->rx_mimo_evm_dbm[i] = (u8)evm;
		phy_info->rx_mimo_signal_quality[i] = sq;
		phy_info->rx_snr[i] = phy_sts->rxsnr[i] >> 1;
		/*@CFO(kHz) = CFO_tail*312.5(kHz)/2^7 ~= CFO tail * 5/2 (kHz)*/
		phy_info->cfo_tail[i] = (phy_sts->cfo_tail[i] * 5) >> 1;
		dbg_i->cfo_tail[i] = (phy_sts->cfo_tail[i] * 5) >> 1;
	}
	phy_info->signal_quality = phy_info->rx_mimo_signal_quality[0];

	if (phy_sts->gid != 0 && phy_sts->gid != 63) {
		phy_info->is_mu_packet = true;
		dbg_i->num_qry_mu_pkt++;
	} else {
		phy_info->is_mu_packet = false;
	}

	phydm_parsing_cfo(dm, pktinfo, phy_sts->cfo_tail, pktinfo->rate_ss);

#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	dm->dm_fat_table.antsel_rx_keep_0 = phy_sts->antidx_a;
	dm->dm_fat_table.antsel_rx_keep_1 = phy_sts->antidx_b;
	dm->dm_fat_table.antsel_rx_keep_2 = phy_sts->antidx_c;
	dm->dm_fat_table.antsel_rx_keep_3 = phy_sts->antidx_d;
#endif
}

void phydm_get_physts_2_others_jgr3(struct dm_struct *dm, u8 *phy_status_inf,
				    struct phydm_perpkt_info_struct *pktinfo,
				    struct phydm_phyinfo_struct *phy_info)
{
	/* type 2 & 3 is used for ofdm packet */
	struct phy_sts_rpt_jgr3_type2_3 *phy_sts = NULL;
}

void phydm_get_physts_4_others_jgr3(struct dm_struct *dm, u8 *phy_status_inf,
				    struct phydm_perpkt_info_struct *pktinfo,
				    struct phydm_phyinfo_struct *phy_info)
{
	struct phy_sts_rpt_jgr3_type4 *phy_sts = NULL;
	struct odm_phy_dbg_info *dbg_i = &dm->phy_dbg_info;
	s8 evm = 0;
	u8 i;
	s8 sq = 0;

	phy_sts = (struct phy_sts_rpt_jgr3_type4 *)phy_status_inf;

	/* SNR: S(8,1), EVM: S(8,1), CFO: S(8,7) */

	for (i = RF_PATH_A; i < dm->num_rf_path; i++) {
		if ((dm->rx_ant_status & BIT(i)) == 0)
			continue;

		evm = phy_sts->rxevm[i];
		evm = (evm == -128) ? 0 : ((0 - evm) >> 1);
		sq = (evm >= 34) ? 100 : evm * 3; /* @Convert EVM to 0~100%*/

		phy_info->rx_mimo_evm_dbm[i] = (u8)evm;
		phy_info->rx_mimo_signal_quality[i] = sq;
		phy_info->rx_snr[i] = phy_sts->rxsnr[i] >> 1;
	}
	phy_info->signal_quality = phy_info->rx_mimo_signal_quality[0];
#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	dm->dm_fat_table.antsel_rx_keep_0 = phy_sts->antidx_a;
	dm->dm_fat_table.antsel_rx_keep_1 = phy_sts->antidx_b;
	dm->dm_fat_table.antsel_rx_keep_2 = phy_sts->antidx_c;
	dm->dm_fat_table.antsel_rx_keep_3 = phy_sts->antidx_d;
#endif
	odm_move_memory(dm, dbg_i->eigen_val, phy_sts->eigenvalue, 4);
	dbg_i->condition_num_seg0 = phy_sts->avg_cond_num_0;
}

void phydm_get_physts_5_others_jgr3(struct dm_struct *dm, u8 *phy_status_inf,
				    struct phydm_perpkt_info_struct *pktinfo,
				    struct phydm_phyinfo_struct *phy_info)
{
	struct phy_sts_rpt_jgr3_type5 *phy_sts = NULL;

}
#if (RTL8723F_SUPPORT)
void phydm_get_physts_6_jgr3(struct dm_struct *dm, u8 *phy_status_inf,
			     struct phydm_perpkt_info_struct *pktinfo,
			     struct phydm_phyinfo_struct *phy_info)
{
	/* type 0 is used for cck packet */
	struct phy_sts_rpt_jgr3_type6 *phy_sts = NULL;
	struct odm_phy_dbg_info *dbg_i = &dm->phy_dbg_info;
	u8 sq = 0, i, rx_cnt = 0;
	s8 rx_power[4], pwdb;
	s8 rx_pwr_db_max = -120;
	u8 evm = 0;
	phy_sts = (struct phy_sts_rpt_jgr3_type6 *)phy_status_inf;
	/* judy_add_8723F_0512 */
	/* rssi S(11,3) */
	rx_power[0] = (s8)((phy_sts->rssi_msb << 5) + (phy_sts->rssi >> 3));
	/* @Update per-path information */
	for (i = RF_PATH_A; i < dm->num_rf_path; i++) {
		if ((dm->rx_ant_status & BIT(i)) == 0)
			continue;

		rx_cnt++; /* @check the number of the ant */

		if (rx_cnt > dm->num_rf_path)
			break;

		if (pktinfo->is_to_self)
			dm->ofdm_agc_idx[i] = rx_power[i]+110;

		/* @Setting the RX power: agc_idx dBm*/
		pwdb = rx_power[i];

		phy_info->rx_pwr[i] = pwdb;
		phy_info->rx_mimo_signal_strength[i] = phydm_pw_2_percent(pwdb);

		/* search maximum pwdb */
		if (pwdb > rx_pwr_db_max)
			rx_pwr_db_max = pwdb;
	}
	
	/* @Calculate EVM U(8,2)*/
	evm = phy_sts->evm_pld >> 2;
	if (pktinfo->data_rate > ODM_RATE2M)
		phy_info->rx_cck_evm = (u8)(evm - 10);/* @5_5M/11M*/
	else
		phy_info->rx_cck_evm = (u8)(evm - 12);/* @1M/2M*/

	sq = (phy_info->rx_cck_evm >= 34) ? 100 : phy_info->rx_cck_evm * 3;
	phy_info->signal_quality = sq;
	/*@CCK no STBC and LDPC*/
	dbg_i->is_ldpc_pkt = false;
	dbg_i->is_stbc_pkt = false;

	/*cck channel has hw bug, [WLANBB-1429]*/
	phy_info->channel = 0;
	phy_info->rx_power = rx_pwr_db_max;
	phy_info->recv_signal_power = rx_pwr_db_max;
	phy_info->is_beamformed = false;
	phy_info->is_mu_packet = false;
	phy_info->rx_pwdb_all = phydm_pw_2_percent(rx_pwr_db_max);
	phy_info->band_width = CHANNEL_WIDTH_20;
	
	//phydm_parsing_cfo(dm, pktinfo, phy_sts->avg_cfo, pktinfo->rate_ss);
	
	#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
	dm->dm_fat_table.antsel_rx_keep_0 = phy_sts->antidx_a;
	dm->dm_fat_table.antsel_rx_keep_1 = 0;
	dm->dm_fat_table.antsel_rx_keep_2 = 0;
	dm->dm_fat_table.antsel_rx_keep_3 = 0;
	#endif
}
#endif
void phydm_get_physts_ofdm_cmn_jgr3(struct dm_struct *dm, u8 *phy_status_inf,
				    struct phydm_perpkt_info_struct *pktinfo,
				    struct phydm_phyinfo_struct *phy_info)
{
	struct phy_sts_rpt_jgr3_ofdm_cmn *phy_sts = NULL;
	struct odm_phy_dbg_info *dbg_i = &dm->phy_dbg_info;
	s8 rx_pwr_db_max = -120;
	s8 pwdb = 0;
	u8 i, rx_cnt = 0;

	phy_sts = (struct phy_sts_rpt_jgr3_ofdm_cmn *)phy_status_inf;

	/* Parsing Offset0 & 4*/
	for (i = RF_PATH_A; i < dm->num_rf_path; i++) {
		if ((dm->rx_ant_status & BIT(i)) == 0)
			continue;

		rx_cnt++; /* @check the number of the ant */

		pwdb = (s8)phy_sts->pwdb[i] - 110; /*@dB*/

		if (pktinfo->is_to_self)
			dm->ofdm_agc_idx[i] = phy_sts->pwdb[i];

		/* search maximum pwdb */
		if (pwdb > rx_pwr_db_max)
			rx_pwr_db_max = pwdb;

		phy_info->rx_pwr[i] = pwdb;
		phy_info->rx_mimo_signal_strength[i] = phydm_pw_2_percent(pwdb);
	}

	phy_info->rx_count = (rx_cnt > 0) ? rx_cnt - 1 : 0; /*from 1~4 to 0~3 */
	phy_info->rx_power = rx_pwr_db_max;
	phy_info->rx_pwdb_all = phydm_pw_2_percent(rx_pwr_db_max);
	phy_info->recv_signal_power = rx_pwr_db_max;
	phy_info->channel = phy_sts->channel_lsb;
	phy_info->is_beamformed = (boolean)phy_sts->beamformed;
	phy_info->rxsc = (PHYDM_IS_LEGACY_RATE(pktinfo->data_rate)) ?
			  phy_sts->l_rxsc : phy_sts->ht_rxsc;
	phy_info->band_width = phydm_rxsc_2_bw(dm, phy_info->rxsc);

	dbg_i->is_ldpc_pkt = phy_sts->ldpc;
	dbg_i->is_stbc_pkt = phy_sts->stbc;
	dbg_i->num_qry_bf_pkt += phy_sts->beamformed;
}

void phydm_process_dm_rssi_jgr3(struct dm_struct *dm,
				struct phydm_phyinfo_struct *phy_info,
				struct phydm_perpkt_info_struct *pktinfo)
{
	struct cmn_sta_info *sta = NULL;
	struct rssi_info *rssi_t = NULL;
	u8 rssi_tmp = 0;
	u64 rssi_linear = 0;
	s16 rssi_db = 0;
	u8 i = 0;
	u8 rx_count = 0;

	#if (defined(PHYDM_CCK_RX_PATHDIV_SUPPORT))
	struct phydm_cck_rx_pathdiv *cckrx_t = &dm->dm_cck_rx_pathdiv_table;
	#endif

	/*@[Step4]*/
	if (pktinfo->station_id >= ODM_ASSOCIATE_ENTRY_NUM)
		return;

	sta = dm->phydm_sta_info[pktinfo->station_id];

	if (!is_sta_active(sta))
		return;

	if (!pktinfo->is_packet_match_bssid) /*@data frame only*/
		return;

	if (!(pktinfo->is_packet_to_self) && !(pktinfo->is_packet_beacon))
		return;

	if (pktinfo->is_packet_beacon) {
		dm->phy_dbg_info.num_qry_beacon_pkt++;
		dm->phy_dbg_info.beacon_phy_rate = pktinfo->data_rate;
	}

	#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	if (dm->support_ability & ODM_BB_ANT_DIV)
		odm_process_rssi_for_ant_div(dm, phy_info, pktinfo);
	#endif

	#ifdef ODM_EVM_ENHANCE_ANTDIV
	phydm_rx_rate_for_antdiv(dm, pktinfo);
	#endif

	#if (defined(CONFIG_PATH_DIVERSITY))
	if (dm->support_ability & ODM_BB_PATH_DIV)
		phydm_process_rssi_for_path_div(dm, phy_info, pktinfo);
	#endif

	#if (defined(PHYDM_CCK_RX_PATHDIV_SUPPORT))
	if (cckrx_t->en_cck_rx_pathdiv)
		phydm_process_rssi_for_cck_rx_pathdiv(dm, phy_info, pktinfo);
	#endif

	rssi_t = &sta->rssi_stat;

	for (i = 0; i < dm->num_rf_path; i++) {
		rssi_tmp = phy_info->rx_mimo_signal_strength[i];
		if (rssi_tmp != 0) {
			rx_count++;
			rssi_linear += phydm_db_2_linear(rssi_tmp);
		}
	}
	/* @Rounding and removing fractional bits */
	rssi_linear = (rssi_linear + (1 << (FRAC_BITS - 1))) >> FRAC_BITS;

	switch (rx_count) {
	case 2:
		rssi_linear = DIVIDED_2(rssi_linear);
		break;
	case 3:
		rssi_linear = DIVIDED_3(rssi_linear);
		break;
	case 4:
		rssi_linear = DIVIDED_4(rssi_linear);
		break;
	}

	rssi_db = (s16)odm_convert_to_db(rssi_linear);

	if (rssi_t->rssi_acc == 0) {
		rssi_t->rssi_acc = (s16)(rssi_db << RSSI_MA);
		rssi_t->rssi = (s8)(rssi_db);
	} else {
		rssi_t->rssi_acc = MA_ACC(rssi_t->rssi_acc, rssi_db, RSSI_MA);
		rssi_t->rssi = (s8)GET_MA_VAL(rssi_t->rssi_acc, RSSI_MA);
	}

	if (pktinfo->is_cck_rate)
		rssi_t->rssi_cck = (s8)rssi_db;
	else
		rssi_t->rssi_ofdm = (s8)rssi_db;
}

void phydm_rx_physts_jgr3(void *dm_void, u8 *phy_sts,
			  struct phydm_perpkt_info_struct *pktinfo,
			  struct phydm_phyinfo_struct *phy_info)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 phy_status_type = (*phy_sts & 0xf);
	if (dm->support_ic_type & ODM_RTL8723F) {
		if (pktinfo->data_rate <= ODM_RATE11M)
			phy_status_type = 6;
	}
	/*@[Step 2]*/
	/*phydm_reset_phy_info_jgr3(dm, phy_info);*/ /* @Memory reset */

	/* Phy status parsing */
	switch (phy_status_type) {
	case 0: /*@CCK*/
		phydm_get_physts_0_jgr3(dm, phy_sts, pktinfo, phy_info);
		break;
	case 1:
		phydm_get_physts_ofdm_cmn_jgr3(dm, phy_sts, pktinfo, phy_info);
		phydm_get_physts_1_others_jgr3(dm, phy_sts, pktinfo, phy_info);
		break;
	case 2:
	case 3:
		phydm_get_physts_ofdm_cmn_jgr3(dm, phy_sts, pktinfo, phy_info);
		phydm_get_physts_2_others_jgr3(dm, phy_sts, pktinfo, phy_info);
		break;
	case 4:
		phydm_get_physts_ofdm_cmn_jgr3(dm, phy_sts, pktinfo, phy_info);
		phydm_get_physts_4_others_jgr3(dm, phy_sts, pktinfo, phy_info);
		break;
	case 5:
		phydm_get_physts_ofdm_cmn_jgr3(dm, phy_sts, pktinfo, phy_info);
		phydm_get_physts_5_others_jgr3(dm, phy_sts, pktinfo, phy_info);
		break;
#if (RTL8723F_SUPPORT)
	case 6:
		phydm_get_physts_6_jgr3(dm, phy_sts, pktinfo, phy_info);
		break;
#endif
	default:
		break;
	}

#if 0
	PHYDM_DBG(dm, DBG_PHY_STATUS, "RSSI: {%d, %d}\n",
		  phy_info->rx_mimo_signal_strength[0],
		  phy_info->rx_mimo_signal_strength[1]);
	PHYDM_DBG(dm, DBG_PHY_STATUS, "rxdb: {%d, %d}\n",
		  phy_info->rx_pwr[0], phy_info->rx_pwr[1]);
	PHYDM_DBG(dm, DBG_PHY_STATUS, "EVM: {%d, %d}\n",
		  phy_info->rx_mimo_evm_dbm[0], phy_info->rx_mimo_evm_dbm[1]);
	PHYDM_DBG(dm, DBG_PHY_STATUS, "SQ: {%d, %d}\n",
		  phy_info->rx_mimo_signal_quality[0],
		  phy_info->rx_mimo_signal_quality[1]);
	PHYDM_DBG(dm, DBG_PHY_STATUS, "SNR: {%d, %d}\n",
		  phy_info->rx_snr[0], phy_info->rx_snr[1]);
	PHYDM_DBG(dm, DBG_PHY_STATUS, "CFO: {%d, %d}\n",
		  phy_info->cfo_tail[0], phy_info->cfo_tail[1]);
	PHYDM_DBG(dm, DBG_PHY_STATUS,
		  "rx_pwdb_all = %d, rx_power = %d, recv_signal_power = %d\n",
		  phy_info->rx_pwdb_all, phy_info->rx_power,
		  phy_info->recv_signal_power);
	PHYDM_DBG(dm, DBG_PHY_STATUS, "signal_quality = %d\n",
		  phy_info->signal_quality);
	PHYDM_DBG(dm, DBG_PHY_STATUS,
		  "is_beamformed = %d, is_mu_packet = %d, rx_count = %d\n",
		  phy_info->is_beamformed, phy_info->is_mu_packet,
		  phy_info->rx_count);
	PHYDM_DBG(dm, DBG_PHY_STATUS,
		  "channel = %d, rxsc = %d, band_width = %d\n",
		  phy_info->channel, phy_info->rxsc, phy_info->band_width);
#endif

	/*@[Step 1]*/
	phydm_print_phystat_jgr3(dm, phy_sts, pktinfo, phy_info);
}

#endif

#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT)
/* @For 8822B only!! need to move to FW finally */
/*@==============================================*/

boolean
phydm_query_is_mu_api(struct dm_struct *phydm, u8 ppdu_idx, u8 *p_data_rate,
		      u8 *p_gid)
{
	u8 data_rate = 0, gid = 0;
	boolean is_mu = false;

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

void phydm_print_phy_sts_jgr2(struct dm_struct *dm, u8 *phy_status_inf,
			      struct phydm_perpkt_info_struct *pktinfo,
			      struct phydm_phyinfo_struct *phy_info)
{
	struct phy_sts_rpt_jgr2_type0 *rpt0 = NULL;
	struct phy_sts_rpt_jgr2_type1 *rpt = NULL;
	struct phy_sts_rpt_jgr2_type2 *rpt2 = NULL;
	struct odm_phy_dbg_info *dbg = &dm->phy_dbg_info;
	u8 phy_status_page_num = (*phy_status_inf & 0xf);
	u32 phy_status[PHY_STATUS_JRGUAR2_DW_LEN] = {0};
	u8 i;
	u32 size = PHY_STATUS_JRGUAR2_DW_LEN << 2;

	rpt0 = (struct phy_sts_rpt_jgr2_type0 *)phy_status_inf;
	rpt = (struct phy_sts_rpt_jgr2_type1 *)phy_status_inf;
	rpt2 = (struct phy_sts_rpt_jgr2_type2 *)phy_status_inf;

	odm_move_memory(dm, phy_status, phy_status_inf, size);

	if (!(dm->debug_components & DBG_PHY_STATUS))
		return;

	if (dbg->show_phy_sts_all_pkt == 0) {
		if (!pktinfo->is_packet_match_bssid)
			return;
	}

	dbg->show_phy_sts_cnt++;
	#if 0
	dbg_print("cnt=%d, max=%d\n",
		  dbg->show_phy_sts_cnt, dbg->show_phy_sts_max_cnt);
	#endif

	if (dbg->show_phy_sts_max_cnt != SHOW_PHY_STATUS_UNLIMITED) {
		if (dbg->show_phy_sts_cnt > dbg->show_phy_sts_max_cnt)
			return;
	}

	pr_debug("Phy Status Rpt: OFDM_%d\n", phy_status_page_num);
	pr_debug("StaID=%d, RxRate = 0x%x match_bssid=%d\n",
		 pktinfo->station_id, pktinfo->data_rate,
		 pktinfo->is_packet_match_bssid);

	for (i = 0; i < PHY_STATUS_JRGUAR2_DW_LEN; i++)
		pr_debug("Offset[%d:%d] = 0x%x\n",
			 ((4 * i) + 3), (4 * i), phy_status[i]);

	if (phy_status_page_num == 0) {
		pr_debug("[0] TRSW=%d, MP_gain_idx=%d, pwdb=%d\n",
			 rpt0->trsw, rpt0->gain, rpt0->pwdb);
		pr_debug("[4] band=%d, CH=%d, agc_table = %d, rxsc = %d\n",
			 rpt0->band, rpt0->channel,
			 rpt0->agc_table, rpt0->rxsc);
		pr_debug("[8] AntIdx[D:A]={%d, %d, %d, %d}, LSIG_len=%d\n",
			 rpt0->antidx_d, rpt0->antidx_c, rpt0->antidx_b,
			 rpt0->antidx_a, rpt0->length);
		pr_debug("[12] lna_h=%d, bb_pwr=%d, lna_l=%d, vga=%d, sq=%d\n",
			 rpt0->lna_h, rpt0->bb_power, rpt0->lna_l,
			 rpt0->vga, rpt0->signal_quality);

	} else if (phy_status_page_num == 1) {
		pr_debug("[0] pwdb[D:A]={%d, %d, %d, %d}\n",
			 rpt->pwdb[3], rpt->pwdb[2],
			 rpt->pwdb[1], rpt->pwdb[0]);
		pr_debug("[4] BF: %d, ldpc=%d, stbc=%d, g_bt=%d, antsw=%d, band=%d, CH=%d, rxsc[ht, l]={%d, %d}\n",
			 rpt->beamformed, rpt->ldpc, rpt->stbc, rpt->gnt_bt,
			 rpt->hw_antsw_occu, rpt->band, rpt->channel,
			 rpt->ht_rxsc, rpt->l_rxsc);
		pr_debug("[8] AntIdx[D:A]={%d, %d, %d, %d}, LSIG_len=%d\n",
			 rpt->antidx_d, rpt->antidx_c, rpt->antidx_b,
			 rpt->antidx_a, rpt->lsig_length);
		pr_debug("[12] rf_mode=%d, NBI=%d, Intf_pos=%d, GID=%d, PAID=%d\n",
			 rpt->rf_mode, rpt->nb_intf_flag,
			 (rpt->intf_pos + (rpt->intf_pos_msb << 8)), rpt->gid,
			 (rpt->paid + (rpt->paid_msb << 8)));
		pr_debug("[16] EVM[D:A]={%d, %d, %d, %d}\n",
			 rpt->rxevm[3], rpt->rxevm[2],
			 rpt->rxevm[1], rpt->rxevm[0]);
		pr_debug("[20] CFO[D:A]={%d, %d, %d, %d}\n",
			 rpt->cfo_tail[3], rpt->cfo_tail[2], rpt->cfo_tail[1],
			 rpt->cfo_tail[0]);
		pr_debug("[24] SNR[D:A]={%d, %d, %d, %d}\n\n",
			 rpt->rxsnr[3], rpt->rxsnr[2], rpt->rxsnr[1],
			 rpt->rxsnr[0]);

	} else if (phy_status_page_num == 2) {
		pr_debug("[0] pwdb[D:A]={%d, %d, %d, %d}\n",
			 rpt2->pwdb[3], rpt2->pwdb[2], rpt2->pwdb[1],
			 rpt2->pwdb[0]);
		pr_debug("[4] BF: %d, ldpc=%d, stbc=%d, g_bt=%d, antsw=%d, band=%d, CH=%d, rxsc[ht,l]={%d, %d}\n",
			 rpt2->beamformed, rpt2->ldpc, rpt2->stbc, rpt2->gnt_bt,
			 rpt2->hw_antsw_occu, rpt2->band, rpt2->channel,
			 rpt2->ht_rxsc, rpt2->l_rxsc);
		pr_debug("[8] AgcTab[D:A]={%d, %d, %d, %d}, cnt_pw2cca=%d, shift_l_map=%d\n",
			 rpt2->agc_table_d, rpt2->agc_table_c,
			 rpt2->agc_table_b, rpt2->agc_table_a,
			 rpt2->cnt_pw2cca, rpt2->shift_l_map);
		pr_debug("[12] (TRSW|Gain)[D:A]={%d %d, %d %d, %d %d, %d %d}, cnt_cca2agc_rdy=%d\n",
			 rpt2->trsw_d, rpt2->gain_d, rpt2->trsw_c, rpt2->gain_c,
			 rpt2->trsw_b, rpt2->gain_b, rpt2->trsw_a,
			 rpt2->gain_a, rpt2->cnt_cca2agc_rdy);
		pr_debug("[16] AAGC step[D:A]={%d, %d, %d, %d} HT AAGC gain[D:A]={%d, %d, %d, %d}\n",
			 rpt2->aagc_step_d, rpt2->aagc_step_c,
			 rpt2->aagc_step_b, rpt2->aagc_step_a,
			 rpt2->ht_aagc_gain[3], rpt2->ht_aagc_gain[2],
			 rpt2->ht_aagc_gain[1], rpt2->ht_aagc_gain[0]);
		pr_debug("[20] DAGC gain[D:A]={%d, %d, %d, %d}\n",
			 rpt2->dagc_gain[3],
			 rpt2->dagc_gain[2], rpt2->dagc_gain[1],
			 rpt2->dagc_gain[0]);
		pr_debug("[24] syn_cnt: %d, Cnt=%d\n\n",
			 rpt2->syn_count, rpt2->counter);
	}
}

void phydm_set_per_path_phy_info(u8 rx_path, s8 pwr, s8 rx_evm, s8 cfo_tail,
				 s8 rx_snr, u8 ant_idx,
				 struct phydm_phyinfo_struct *phy_info)
{
	u8 evm_dbm = 0;
	u8 evm_percentage = 0;

	/* SNR is S(8,1), EVM is S(8,1), CFO is S(8,7) */

	if (rx_evm < 0) {
		/* @Calculate EVM in dBm */
		evm_dbm = ((u8)(0 - rx_evm) >> 1);

		if (evm_dbm == 64)
			evm_dbm = 0; /*@if 1SS rate, evm_dbm [2nd stream] =64*/

		if (evm_dbm != 0) {
			/* @Convert EVM to 0%~100% percentage */
			if (evm_dbm >= 34)
				evm_percentage = 100;
			else
				evm_percentage = (evm_dbm << 1) + (evm_dbm);
		}
	}

	phy_info->rx_pwr[rx_path] = pwr;

	/*@CFO(kHz) = CFO_tail * 312.5(kHz) / 2^7 ~= CFO tail * 5/2 (kHz)*/
	phy_info->cfo_tail[rx_path] = (cfo_tail * 5) >> 1;
	phy_info->rx_mimo_evm_dbm[rx_path] = evm_dbm;
	phy_info->rx_mimo_signal_strength[rx_path] = phydm_pw_2_percent(pwr);
	phy_info->rx_mimo_signal_quality[rx_path] = evm_percentage;
	phy_info->rx_snr[rx_path] = rx_snr >> 1;
	phy_info->ant_idx[rx_path] = ant_idx;

#if 0
	if (!pktinfo->is_packet_match_bssid)
		return;

	dbg_print("path (%d)--------\n", rx_path);
	dbg_print("rx_pwr = %d, Signal strength = %d\n",
		  phy_info->rx_pwr[rx_path],
		  phy_info->rx_mimo_signal_strength[rx_path]);
	dbg_print("evm_dbm = %d, Signal quality = %d\n",
		  phy_info->rx_mimo_evm_dbm[rx_path],
		  phy_info->rx_mimo_signal_quality[rx_path]);
	dbg_print("CFO = %d, SNR = %d\n",
		  phy_info->cfo_tail[rx_path], phy_info->rx_snr[rx_path]);

#endif
}

void phydm_set_common_phy_info(s8 rx_power, u8 channel, boolean is_beamformed,
			       boolean is_mu_packet, u8 bandwidth,
			       u8 signal_quality, u8 rxsc,
			       struct phydm_phyinfo_struct *phy_info)
{
	phy_info->rx_power = rx_power; /* RSSI in dB */
	phy_info->recv_signal_power = rx_power; /* RSSI in dB */
	phy_info->channel = channel; /* @channel number */
	phy_info->is_beamformed = is_beamformed; /* @apply BF */
	phy_info->is_mu_packet = is_mu_packet; /* @MU packet */
	phy_info->rxsc = rxsc;

	/* RSSI in percentage */
	phy_info->rx_pwdb_all = phydm_pw_2_percent(rx_power);
	phy_info->signal_quality = signal_quality; /* signal quality */
	phy_info->band_width = bandwidth; /* @bandwidth */

#if 0
	if (!pktinfo->is_packet_match_bssid)
		return;

	dbg_print("rx_pwdb_all = %d, rx_power = %d, recv_signal_power = %d\n",
		  phy_info->rx_pwdb_all, phy_info->rx_power,
		  phy_info->recv_signal_power);
	dbg_print("signal_quality = %d\n", phy_info->signal_quality);
	dbg_print("is_beamformed = %d, is_mu_packet = %d, rx_count = %d\n",
		  phy_info->is_beamformed, phy_info->is_mu_packet,
		  phy_info->rx_count + 1);
	dbg_print("channel = %d, rxsc = %d, band_width = %d\n", channel,
		  rxsc, bandwidth);

#endif
}

void phydm_get_phy_sts_type0(struct dm_struct *dm, u8 *phy_status_inf,
			     struct phydm_perpkt_info_struct *pktinfo,
			     struct phydm_phyinfo_struct *phy_info)
{
	/* type 0 is used for cck packet */
	struct phy_sts_rpt_jgr2_type0 *phy_sts = NULL;
	u8 sq = 0;
	s8 rx_pow = 0;
	u8 lna_idx = 0, vga_idx = 0;
	u8 ant_idx;

	phy_sts = (struct phy_sts_rpt_jgr2_type0 *)phy_status_inf;
	rx_pow = phy_sts->pwdb - 110;

	/* Fill in per-path antenna index */
	ant_idx = phy_sts->antidx_a;

	if (dm->support_ic_type & ODM_RTL8723D) {
		#if (RTL8723D_SUPPORT)
		rx_pow = phy_sts->pwdb - 97;
		#endif
	}
	#if (RTL8821C_SUPPORT)
	else if (dm->support_ic_type & ODM_RTL8821C) {
		if (phy_sts->pwdb >= -57)
			rx_pow = phy_sts->pwdb - 100;
		else
			rx_pow = phy_sts->pwdb - 102;
	}
	#endif

	if (pktinfo->is_to_self) {
		dm->ofdm_agc_idx[0] = phy_sts->pwdb;
		dm->ofdm_agc_idx[1] = 0;
		dm->ofdm_agc_idx[2] = 0;
		dm->ofdm_agc_idx[3] = 0;
	}

	/* @Calculate Signal Quality*/
	if (phy_sts->signal_quality >= 64) {
		sq = 0;
	} else if (phy_sts->signal_quality <= 20) {
		sq = 100;
	} else {
		/* @mapping to 2~99% */
		sq = 64 - phy_sts->signal_quality;
		sq = ((sq << 3) + sq) >> 2;
	}

	/* @Get RSSI for old CCK AGC */
	if (!dm->cck_new_agc) {
		vga_idx = phy_sts->vga;

		if (dm->support_ic_type & ODM_RTL8197F) {
			/*@3bit LNA*/
			lna_idx = phy_sts->lna_l;
		} else {
			/*@4bit LNA*/
			lna_idx = (phy_sts->lna_h << 3) | phy_sts->lna_l;
		}
		rx_pow = phydm_get_cck_rssi(dm, lna_idx, vga_idx);
	}

	/* @Confirm CCK RSSI */
	#if (RTL8197F_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8197F) {
		u8 bb_pwr_th_l = 5; /* round( 31*0.15 ) */
		u8 bb_pwr_th_h = 27; /* round( 31*0.85 ) */

		if (phy_sts->bb_power < bb_pwr_th_l ||
		    phy_sts->bb_power > bb_pwr_th_h)
			rx_pow = 0; /* @Error RSSI for CCK ; set 100*/
	}
	#endif

	/*@CCK no STBC and LDPC*/
	dm->phy_dbg_info.is_ldpc_pkt = false;
	dm->phy_dbg_info.is_stbc_pkt = false;

	/* Update Common information */
	phydm_set_common_phy_info(rx_pow, phy_sts->channel, false,
				  false, CHANNEL_WIDTH_20, sq,
				  phy_sts->rxsc, phy_info);
	/* Update CCK pwdb */
	phydm_set_per_path_phy_info(RF_PATH_A, rx_pow, 0, 0, 0, ant_idx,
				    phy_info);

	#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
	dm->dm_fat_table.antsel_rx_keep_0 = phy_sts->antidx_a;
	dm->dm_fat_table.antsel_rx_keep_1 = phy_sts->antidx_b;
	dm->dm_fat_table.antsel_rx_keep_2 = phy_sts->antidx_c;
	dm->dm_fat_table.antsel_rx_keep_3 = phy_sts->antidx_d;
	#endif
}

void phydm_get_phy_sts_type1(struct dm_struct *dm, u8 *phy_status_inf,
			     struct phydm_perpkt_info_struct *pktinfo,
			     struct phydm_phyinfo_struct *phy_info)
{
	/* type 1 is used for ofdm packet */
	struct phy_sts_rpt_jgr2_type1 *phy_sts = NULL;
	struct odm_phy_dbg_info *dbg_i = &dm->phy_dbg_info;
	s8 rx_pwr_db = -120;
	s8 rx_pwr = 0;
	u8 i, rxsc, bw = CHANNEL_WIDTH_20, rx_count = 0;
	boolean is_mu;
	u8 ant_idx[4];

	phy_sts = (struct phy_sts_rpt_jgr2_type1 *)phy_status_inf;

	/* Fill in per-path antenna index */
	ant_idx[0] = phy_sts->antidx_a;
	ant_idx[1] = phy_sts->antidx_b;
	ant_idx[2] = phy_sts->antidx_c;
	ant_idx[3] = phy_sts->antidx_d;

	/* Update per-path information */
	for (i = RF_PATH_A; i < PHYDM_MAX_RF_PATH; i++) {
		if (!(dm->rx_ant_status & BIT(i)))
			continue;
		rx_count++;

		if (rx_count > dm->num_rf_path)
			break;

		/* Update per-path information
		 * (RSSI_dB RSSI_percentage EVM SNR CFO sq)
		 */
		/* @EVM report is reported by stream, not path */
		rx_pwr = phy_sts->pwdb[i] - 110; /* per-path pwdb(dB)*/

		if (pktinfo->is_to_self)
			dm->ofdm_agc_idx[i] = phy_sts->pwdb[i];

		phydm_set_per_path_phy_info(i, rx_pwr,
					    phy_sts->rxevm[rx_count - 1],
					    phy_sts->cfo_tail[i],
					    phy_sts->rxsnr[i],
					    ant_idx[i], phy_info);
		/* search maximum pwdb */
		if (rx_pwr > rx_pwr_db)
			rx_pwr_db = rx_pwr;
	}

	/* @mapping RX counter from 1~4 to 0~3 */
	if (rx_count > 0)
		phy_info->rx_count = rx_count - 1;

	/* @Check if MU packet or not */
	if (phy_sts->gid != 0 && phy_sts->gid != 63) {
		is_mu = true;
		dbg_i->num_qry_mu_pkt++;
	} else {
		is_mu = false;
	}

	/* @count BF packet */
	dbg_i->num_qry_bf_pkt = dbg_i->num_qry_bf_pkt + phy_sts->beamformed;

	/*STBC or LDPC pkt*/
	dbg_i->is_ldpc_pkt = phy_sts->ldpc;
	dbg_i->is_stbc_pkt = phy_sts->stbc;

	/* @Check sub-channel */
	if (pktinfo->data_rate > ODM_RATE11M &&
	    pktinfo->data_rate < ODM_RATEMCS0)
		rxsc = phy_sts->l_rxsc;
	else
		rxsc = phy_sts->ht_rxsc;

	/* @Check RX bandwidth */
	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		if (rxsc >= 1 && rxsc <= 8)
			bw = CHANNEL_WIDTH_20;
		else if ((rxsc >= 9) && (rxsc <= 12))
			bw = CHANNEL_WIDTH_40;
		else if (rxsc >= 13)
			bw = CHANNEL_WIDTH_80;
		else
			bw = phy_sts->rf_mode;

	} else if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		if (phy_sts->rf_mode == 0)
			bw = CHANNEL_WIDTH_20;
		else if ((rxsc == 1) || (rxsc == 2))
			bw = CHANNEL_WIDTH_20;
		else
			bw = CHANNEL_WIDTH_40;
	}

	/* Update packet information */
	phydm_set_common_phy_info(rx_pwr_db, phy_sts->channel,
				  (boolean)phy_sts->beamformed, is_mu, bw,
				  phy_info->rx_mimo_signal_quality[0],
				  rxsc, phy_info);

	phydm_parsing_cfo(dm, pktinfo, phy_sts->cfo_tail, pktinfo->rate_ss);
	#ifdef PHYDM_LNA_SAT_CHK_TYPE2
	phydm_parsing_snr(dm, pktinfo, phy_sts->rxsnr);
	#endif

	#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	dm->dm_fat_table.antsel_rx_keep_0 = phy_sts->antidx_a;
	dm->dm_fat_table.antsel_rx_keep_1 = phy_sts->antidx_b;
	dm->dm_fat_table.antsel_rx_keep_2 = phy_sts->antidx_c;
	dm->dm_fat_table.antsel_rx_keep_3 = phy_sts->antidx_d;
	#endif
}

void phydm_get_phy_sts_type2(struct dm_struct *dm, u8 *phy_status_inf,
			     struct phydm_perpkt_info_struct *pktinfo,
			     struct phydm_phyinfo_struct *phy_info)
{
	struct phy_sts_rpt_jgr2_type2 *phy_sts = NULL;
	s8 rx_pwr_db_max = -120;
	s8 rx_pwr = 0;
	u8 i, rxsc, bw = CHANNEL_WIDTH_20, rx_count = 0;

	phy_sts = (struct phy_sts_rpt_jgr2_type2 *)phy_status_inf;

	for (i = RF_PATH_A; i < PHYDM_MAX_RF_PATH; i++) {
		if (!(dm->rx_ant_status & BIT(i)))
			continue;
		rx_count++;

		if (rx_count > dm->num_rf_path)
			break;

		/* Update per-path information*/
		/* RSSI_dB, RSSI_percentage, EVM, SNR, CFO, sq */
		#if (RTL8197F_SUPPORT)
		if ((dm->support_ic_type & ODM_RTL8197F) &&
		    phy_sts->pwdb[i] == 0x7f) { /*@97f workaround*/

			if (i == RF_PATH_A) {
				rx_pwr = (phy_sts->gain_a) << 1;
				rx_pwr = rx_pwr - 110;
			} else if (i == RF_PATH_B) {
				rx_pwr = (phy_sts->gain_b) << 1;
				rx_pwr = rx_pwr - 110;
			} else {
				rx_pwr = 0;
			}
		} else
		#endif
			rx_pwr = phy_sts->pwdb[i] - 110; /*@dBm*/

		phydm_set_per_path_phy_info(i, rx_pwr, 0, 0, 0, 0, phy_info);

		if (rx_pwr > rx_pwr_db_max) /* search max pwdb */
			rx_pwr_db_max = rx_pwr;
	}

	/* @mapping RX counter from 1~4 to 0~3 */
	if (rx_count > 0)
		phy_info->rx_count = rx_count - 1;

	/* @Check RX sub-channel */
	if (pktinfo->data_rate > ODM_RATE11M &&
	    pktinfo->data_rate < ODM_RATEMCS0)
		rxsc = phy_sts->l_rxsc;
	else
		rxsc = phy_sts->ht_rxsc;

	/*STBC or LDPC pkt*/
	dm->phy_dbg_info.is_ldpc_pkt = phy_sts->ldpc;
	dm->phy_dbg_info.is_stbc_pkt = phy_sts->stbc;

	/* @Check RX bandwidth */
	/* @BW information of sc=0 is useless,
	 *because there is no information of RF mode
	 */
	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		if (rxsc >= 1 && rxsc <= 8)
			bw = CHANNEL_WIDTH_20;
		else if ((rxsc >= 9) && (rxsc <= 12))
			bw = CHANNEL_WIDTH_40;
		else if (rxsc >= 13)
			bw = CHANNEL_WIDTH_80;

	} else if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		if (rxsc == 3)
			bw = CHANNEL_WIDTH_40;
		else if ((rxsc == 1) || (rxsc == 2))
			bw = CHANNEL_WIDTH_20;
	}

	/* Update packet information */
	phydm_set_common_phy_info(rx_pwr_db_max, phy_sts->channel,
				  (boolean)phy_sts->beamformed,
				  false, bw, 0, rxsc, phy_info);
}

void phydm_process_rssi_for_dm_2nd_type(struct dm_struct *dm,
					struct phydm_phyinfo_struct *phy_info,
					struct phydm_perpkt_info_struct *pktinfo
					)
{
	struct cmn_sta_info *sta = NULL;
	struct rssi_info *rssi_t = NULL;
	u8 rssi_tmp = 0;
	u64 rssi_linear = 0;
	s16 rssi_db = 0;
	u8 i = 0;

	if (pktinfo->station_id >= ODM_ASSOCIATE_ENTRY_NUM)
		return;

	sta = dm->phydm_sta_info[pktinfo->station_id];

	if (!is_sta_active(sta))
		return;

	if (!pktinfo->is_packet_match_bssid) /*@data frame only*/
		return;

#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	if (dm->support_ability & ODM_BB_ANT_DIV)
		odm_process_rssi_for_ant_div(dm, phy_info, pktinfo);
#endif

#if (defined(CONFIG_PATH_DIVERSITY))
	if (dm->support_ability & ODM_BB_PATH_DIV)
		phydm_process_rssi_for_path_div(dm, phy_info, pktinfo);
#endif

#ifdef CONFIG_ADAPTIVE_SOML
	phydm_rx_qam_for_soml(dm, pktinfo);
	phydm_rx_rate_for_soml(dm, pktinfo);
#endif

	if (!(pktinfo->is_packet_to_self) && !(pktinfo->is_packet_beacon))
		return;

	if (pktinfo->is_packet_beacon) {
		dm->phy_dbg_info.num_qry_beacon_pkt++;
		dm->phy_dbg_info.beacon_phy_rate = pktinfo->data_rate;
	}

	rssi_t = &sta->rssi_stat;

	for (i = RF_PATH_A; i < PHYDM_MAX_RF_PATH; i++) {
		rssi_tmp = phy_info->rx_mimo_signal_strength[i];
		if (rssi_tmp != 0)
			rssi_linear += phydm_db_2_linear(rssi_tmp);
	}
	/* @Rounding and removing fractional bits */
	rssi_linear = (rssi_linear + (1 << (FRAC_BITS - 1))) >> FRAC_BITS;

	switch (phy_info->rx_count + 1) {
	case 2:
		rssi_linear = DIVIDED_2(rssi_linear);
		break;
	case 3:
		rssi_linear = DIVIDED_3(rssi_linear);
		break;
	case 4:
		rssi_linear = DIVIDED_4(rssi_linear);
		break;
	}

	rssi_db = (s16)odm_convert_to_db(rssi_linear);

	if (rssi_t->rssi_acc == 0) {
		rssi_t->rssi_acc = (s16)(rssi_db << RSSI_MA);
		rssi_t->rssi = (s8)(rssi_db);
	} else {
		rssi_t->rssi_acc = MA_ACC(rssi_t->rssi_acc, rssi_db, RSSI_MA);
		rssi_t->rssi = (s8)GET_MA_VAL(rssi_t->rssi_acc, RSSI_MA);
	}

	if (pktinfo->is_cck_rate)
		rssi_t->rssi_cck = (s8)rssi_db;
	else
		rssi_t->rssi_ofdm = (s8)rssi_db;
}

void phydm_rx_physts_2nd_type(void *dm_void, u8 *phy_sts,
			      struct phydm_perpkt_info_struct *pktinfo,
			      struct phydm_phyinfo_struct *phy_info)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 page = (*phy_sts & 0xf);

	/* Phy status parsing */
	switch (page) {
	case 0: /*@CCK*/
		phydm_get_phy_sts_type0(dm, phy_sts, pktinfo, phy_info);
		break;
	case 1:
		phydm_get_phy_sts_type1(dm, phy_sts, pktinfo, phy_info);
		break;
	case 2:
		phydm_get_phy_sts_type2(dm, phy_sts, pktinfo, phy_info);
		break;
	default:
		break;
	}

#if (RTL8822B_SUPPORT || RTL8821C_SUPPORT || RTL8195B_SUPPORT)
	if (dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8821C | ODM_RTL8195B))
		phydm_print_phy_sts_jgr2(dm, phy_sts, pktinfo, phy_info);
#endif
}

/*@==============================================*/
#endif

boolean odm_phy_status_query(struct dm_struct *dm,
			     struct phydm_phyinfo_struct *phy_info,
			     u8 *phy_sts,
			     struct phydm_perpkt_info_struct *pktinfo)
{
#ifdef PHYDM_PHYSTAUS_AUTO_SWITCH
	struct pkt_process_info *pkt_proc = &dm->pkt_proc_struct;
	boolean auto_swch_en = dm->pkt_proc_struct.physts_auto_swch_en;
#endif
	u8 rate = pktinfo->data_rate;
	u8 page = (*phy_sts & 0xf);

	pktinfo->is_cck_rate = PHYDM_IS_CCK_RATE(rate);
	pktinfo->rate_ss = phydm_rate_to_num_ss(dm, rate);
	dm->rate_ss = pktinfo->rate_ss; /*@For AP EVM SW antenna diversity use*/

	if (pktinfo->is_cck_rate)
		dm->phy_dbg_info.num_qry_phy_status_cck++;
	else
		dm->phy_dbg_info.num_qry_phy_status_ofdm++;

	/*Reset phy_info*/
	phydm_reset_phy_info(dm, phy_info);

	if (dm->support_ic_type & PHYSTS_3RD_TYPE_IC) {
		#ifdef PHYSTS_3RD_TYPE_SUPPORT
		#ifdef PHYDM_PHYSTAUS_AUTO_SWITCH
		if (phydm_physts_auto_switch_jgr3(dm, phy_sts, pktinfo)) {
			PHYDM_DBG(dm, DBG_PHY_STATUS, "SKIP parsing\n");
			phy_info->physts_rpt_valid = false;
			return false;
		}
		#endif
		phydm_rx_physts_jgr3(dm, phy_sts, pktinfo, phy_info);
		phydm_process_dm_rssi_jgr3(dm, phy_info, pktinfo);
		#endif
	} else if (dm->support_ic_type & PHYSTS_2ND_TYPE_IC) {
		#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT)
		phydm_rx_physts_2nd_type(dm, phy_sts, pktinfo, phy_info);
		phydm_process_rssi_for_dm_2nd_type(dm, phy_info, pktinfo);
		#endif
	} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		#if ODM_IC_11AC_SERIES_SUPPORT
		phydm_rx_physts_1st_type(dm, phy_info, phy_sts, pktinfo);
		phydm_process_rssi_for_dm(dm, phy_info, pktinfo);
		#endif
	} else if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		#if ODM_IC_11N_SERIES_SUPPORT
		phydm_phy_sts_n_parsing(dm, phy_info, phy_sts, pktinfo);
		phydm_process_rssi_for_dm(dm, phy_info, pktinfo);
		#endif
	}
	phy_info->signal_strength = phy_info->rx_pwdb_all;
	#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	phydm_process_signal_strength(dm, phy_info, pktinfo);
	#endif

	/*For basic debug message*/
	if (pktinfo->is_packet_match_bssid || *dm->mp_mode) {
		dm->curr_station_id = pktinfo->station_id;
		dm->rx_rate = rate;
		dm->rssi_a = phy_info->rx_mimo_signal_strength[RF_PATH_A];
		dm->rssi_b = phy_info->rx_mimo_signal_strength[RF_PATH_B];
		dm->rssi_c = phy_info->rx_mimo_signal_strength[RF_PATH_C];
		dm->rssi_d = phy_info->rx_mimo_signal_strength[RF_PATH_D];

		if (rate >= ODM_RATE6M && rate <= ODM_RATE54M)
			dm->rxsc_l = (s8)phy_info->rxsc;
		else if (phy_info->band_width == CHANNEL_WIDTH_20)
			dm->rxsc_20 = (s8)phy_info->rxsc;
		else if (phy_info->band_width == CHANNEL_WIDTH_40)
			dm->rxsc_40 = (s8)phy_info->rxsc;
		else if (phy_info->band_width == CHANNEL_WIDTH_80)
			dm->rxsc_80 = (s8)phy_info->rxsc;

		#ifdef PHYDM_PHYSTAUS_AUTO_SWITCH
		if (auto_swch_en && page == 4 && pktinfo->rate_ss > 1)
			phydm_avg_condi_num(dm, phy_info, pktinfo);

		if (!auto_swch_en ||
		    (pkt_proc->is_1st_mpdu || PHYDM_IS_LEGACY_RATE(rate)))
		#endif
		{
			phydm_avg_rssi_evm_snr(dm, phy_info, pktinfo);
			phydm_rx_statistic_cal(dm, phy_info, phy_sts, pktinfo);
		}
	}

	phy_info->physts_rpt_valid = true;
	return true;
}

void phydm_rx_phy_status_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_phy_dbg_info *dbg = &dm->phy_dbg_info;

	dbg->show_phy_sts_all_pkt = 0;
	dbg->show_phy_sts_max_cnt = 1;
	dbg->show_phy_sts_cnt = 0;

	phydm_avg_phystatus_init(dm);

	#ifdef PHYDM_PHYSTAUS_AUTO_SWITCH
	dm->pkt_proc_struct.physts_auto_swch_en = false;
	#endif
}

void phydm_physts_dbg(void *dm_void, char input[][16], u32 *_used,
		      char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	char help[] = "-h";
	boolean enable;
	u32 var[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;
	u8 i = 0;

	for (i = 0; i < 3; i++) {
		PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var[i]);
	}

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Page Auto Switching: swh {en} {bitmap(hex)}\n");
	} else if ((strcmp(input[1], "swh") == 0)) {
		#ifdef PHYDM_PHYSTAUS_AUTO_SWITCH
		PHYDM_SSCANF(input[3], DCMD_HEX, &var[2]);
		enable = (boolean)var[1];
		phydm_physts_auto_switch_jgr3_set(dm, enable, (u8)var[2]);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Page Auto Switching: en=%d, bitmap=0x%x\n",
			 enable, var[2]);
		#endif
	}
	*_used = used;
	*_out_len = out_len;
}
