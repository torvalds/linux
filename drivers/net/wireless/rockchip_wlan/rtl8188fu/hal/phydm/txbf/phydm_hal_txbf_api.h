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
#ifndef __PHYDM_HAL_TXBF_API_H__
#define __PHYDM_HAL_TXBF_API_H__

#if (defined(CONFIG_BB_TXBF_API))

#if (DM_ODM_SUPPORT_TYPE != ODM_AP)
#if defined(DM_ODM_CE_MAC80211)
#define tx_bf_nr(a, b) ({	\
	u8 __tx_bf_nr_a = (a);	\
	u8 __tx_bf_nr_b = (b);	\
	((__tx_bf_nr_a > __tx_bf_nr_b) ? (__tx_bf_nr_b) : (__tx_bf_nr_a)); })
#else
#define tx_bf_nr(a, b) ((a > b) ? (b) : (a))
#endif

u8 beamforming_get_htndp_tx_rate(void *dm_void, u8 bfer_str_num);

u8 beamforming_get_vht_ndp_tx_rate(void *dm_void, u8 bfer_str_num);

#endif

#if (RTL8822B_SUPPORT == 1 || RTL8822C_SUPPORT == 1 || RTL8192F_SUPPORT == 1 ||\
	RTL8814B_SUPPORT == 1 || RTL8198F_SUPPORT == 1)

u8 phydm_get_beamforming_sounding_info(void *dm_void, u16 *throughput,
				       u8 total_bfee_num, u8 *tx_rate);
u8 phydm_get_ndpa_rate(void *dm_void);

u8 phydm_get_mu_bfee_snding_decision(void *dm_void, u16 throughput);

#else
#define phydm_get_beamforming_sounding_info(dm, tp, bfee_num, rate) 0
#define phydm_get_ndpa_rate(dm)
#define phydm_get_mu_bfee_snding_decision(dm, tp)

#endif

#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
struct phydm_bf_rate_info_jgr3 {
	u8			enable;
	u8			mu_ratio_th;
	u32			pre_mu_ratio;
	u16			num_mu_vht_pkt[VHT_RATE_NUM];
	u16			num_qry_vht_pkt[VHT_RATE_NUM];
};

/*this function is only used for BFer*/
void phydm_txbf_rfmode(void *dm_void, u8 su_bfee_cnt, u8 mu_bfee_cnt);
void phydm_txbf_avoid_hang(void *dm_void);
void phydm_mu_rsoml_init(void *dm_void);
void phydm_mu_rsoml_decision(void *dm_void);

#if (RTL8814B_SUPPORT == 1)
void phydm_txbf_80p80_rfmode(void *dm_void, u8 su_bfee_cnt, u8 mu_bfee_cnt);
#endif

#endif /*#PHYDM_IC_JGR3_SERIES_SUPPORT*/
void phydm_bf_debug(void *dm_void, char input[][16], u32 *_used, char *output,
		    u32 *_out_len);
#endif
#endif
