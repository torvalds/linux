/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _BRCM_PHY_LCN_H_
#define _BRCM_PHY_LCN_H_

#include <types.h>

struct brcms_phy_lcnphy {
	int lcnphy_txrf_sp_9_override;
	u8 lcnphy_full_cal_channel;
	u8 lcnphy_cal_counter;
	u16 lcnphy_cal_temper;
	bool lcnphy_recal;

	u8 lcnphy_rc_cap;
	u32 lcnphy_mcs20_po;

	u8 lcnphy_tr_isolation_mid;
	u8 lcnphy_tr_isolation_low;
	u8 lcnphy_tr_isolation_hi;

	u8 lcnphy_bx_arch;
	u8 lcnphy_rx_power_offset;
	u8 lcnphy_rssi_vf;
	u8 lcnphy_rssi_vc;
	u8 lcnphy_rssi_gs;
	u8 lcnphy_tssi_val;
	u8 lcnphy_rssi_vf_lowtemp;
	u8 lcnphy_rssi_vc_lowtemp;
	u8 lcnphy_rssi_gs_lowtemp;

	u8 lcnphy_rssi_vf_hightemp;
	u8 lcnphy_rssi_vc_hightemp;
	u8 lcnphy_rssi_gs_hightemp;

	s16 lcnphy_pa0b0;
	s16 lcnphy_pa0b1;
	s16 lcnphy_pa0b2;

	u16 lcnphy_rawtempsense;
	u8 lcnphy_measPower;
	u8 lcnphy_tempsense_slope;
	u8 lcnphy_freqoffset_corr;
	u8 lcnphy_tempsense_option;
	u8 lcnphy_tempcorrx;
	bool lcnphy_iqcal_swp_dis;
	bool lcnphy_hw_iqcal_en;
	uint lcnphy_bandedge_corr;
	bool lcnphy_spurmod;
	u16 lcnphy_tssi_tx_cnt;
	u16 lcnphy_tssi_idx;
	u16 lcnphy_tssi_npt;

	u16 lcnphy_target_tx_freq;
	s8 lcnphy_tx_power_idx_override;
	u16 lcnphy_noise_samples;

	u32 lcnphy_papdRxGnIdx;
	u32 lcnphy_papd_rxGnCtrl_init;

	u32 lcnphy_gain_idx_14_lowword;
	u32 lcnphy_gain_idx_14_hiword;
	u32 lcnphy_gain_idx_27_lowword;
	u32 lcnphy_gain_idx_27_hiword;
	s16 lcnphy_ofdmgainidxtableoffset;
	s16 lcnphy_dsssgainidxtableoffset;
	u32 lcnphy_tr_R_gain_val;
	u32 lcnphy_tr_T_gain_val;
	s8 lcnphy_input_pwr_offset_db;
	u16 lcnphy_Med_Low_Gain_db;
	u16 lcnphy_Very_Low_Gain_db;
	s8 lcnphy_lastsensed_temperature;
	s8 lcnphy_pkteng_rssi_slope;
	u8 lcnphy_saved_tx_user_target[TXP_NUM_RATES];
	u8 lcnphy_volt_winner;
	u8 lcnphy_volt_low;
	u8 lcnphy_54_48_36_24mbps_backoff;
	u8 lcnphy_11n_backoff;
	u8 lcnphy_lowerofdm;
	u8 lcnphy_cck;
	u8 lcnphy_psat_2pt3_detected;
	s32 lcnphy_lowest_Re_div_Im;
	s8 lcnphy_final_papd_cal_idx;
	u16 lcnphy_extstxctrl4;
	u16 lcnphy_extstxctrl0;
	u16 lcnphy_extstxctrl1;
	s16 lcnphy_cck_dig_filt_type;
	s16 lcnphy_ofdm_dig_filt_type;
	struct lcnphy_cal_results lcnphy_cal_results;

	u8 lcnphy_psat_pwr;
	u8 lcnphy_psat_indx;
	s32 lcnphy_min_phase;
	u8 lcnphy_final_idx;
	u8 lcnphy_start_idx;
	u8 lcnphy_current_index;
	u16 lcnphy_logen_buf_1;
	u16 lcnphy_local_ovr_2;
	u16 lcnphy_local_oval_6;
	u16 lcnphy_local_oval_5;
	u16 lcnphy_logen_mixer_1;

	u8 lcnphy_aci_stat;
	uint lcnphy_aci_start_time;
	s8 lcnphy_tx_power_offset[TXP_NUM_RATES];
};
#endif				/* _BRCM_PHY_LCN_H_ */
