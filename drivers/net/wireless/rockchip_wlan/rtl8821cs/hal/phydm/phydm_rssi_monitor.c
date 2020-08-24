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

#ifdef PHYDM_SUPPORT_RSSI_MONITOR

void phydm_rssi_monitor_h2c(void *dm_void, u8 macid)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ra_table *ra_t = &dm->dm_ra_table;
	struct cmn_sta_info *sta = dm->phydm_sta_info[macid];
	struct ra_sta_info *ra = NULL;
	#ifdef CONFIG_BEAMFORMING
	struct bf_cmn_info *bf = NULL;
	#endif
	u8 h2c[H2C_MAX_LENGTH] = {0};
	u8 stbc_en, ldpc_en;
	u8 bf_en = 0;
	u8 is_rx, is_tx;

	if (is_sta_active(sta)) {
		ra = &sta->ra_info;
	} else {
		PHYDM_DBG(dm, DBG_RSSI_MNTR, "[Warning] %s\n", __func__);
		return;
	}

	PHYDM_DBG(dm, DBG_RSSI_MNTR, "%s ======>\n", __func__);
	PHYDM_DBG(dm, DBG_RSSI_MNTR, "MACID=%d\n", sta->mac_id);

	is_rx = (ra->txrx_state == RX_STATE) ? 1 : 0;
	is_tx = (ra->txrx_state == TX_STATE) ? 1 : 0;
	stbc_en = (sta->stbc_en) ? 1 : 0;
	ldpc_en = (sta->ldpc_en) ? 1 : 0;

	#ifdef CONFIG_BEAMFORMING
	bf = &sta->bf_info;

	if ((bf->ht_beamform_cap & BEAMFORMING_HT_BEAMFORMEE_ENABLE) ||
	    (bf->vht_beamform_cap & BEAMFORMING_VHT_BEAMFORMEE_ENABLE))
		bf_en = 1;
	#endif

	PHYDM_DBG(dm, DBG_RSSI_MNTR, "RA_th_ofst=(( %s%d ))\n",
		  ((ra_t->ra_ofst_direc) ? "+" : "-"), ra_t->ra_th_ofst);

	h2c[0] = sta->mac_id;
	h2c[1] = 0;
	h2c[2] = sta->rssi_stat.rssi;
	h2c[3] = is_rx | (stbc_en << 1) |
		     ((dm->noisy_decision & 0x1) << 2) | (bf_en << 6);
	h2c[4] = (ra_t->ra_th_ofst & 0x7f) |
		     ((ra_t->ra_ofst_direc & 0x1) << 7);
	h2c[5] = 0;
	h2c[6] = 0;

	PHYDM_DBG(dm, DBG_RSSI_MNTR, "PHYDM h2c[0x42]=0x%x %x %x %x %x %x %x\n",
		  h2c[6], h2c[5], h2c[4], h2c[3], h2c[2], h2c[1], h2c[0]);

	#if (RTL8188E_SUPPORT)
	if (dm->support_ic_type == ODM_RTL8188E)
		odm_ra_set_rssi_8188e(dm, sta->mac_id, sta->rssi_stat.rssi);
	else
	#endif
	{
		odm_fill_h2c_cmd(dm, ODM_H2C_RSSI_REPORT, H2C_MAX_LENGTH, h2c);
	}
}

void phydm_calculate_rssi_min_max(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct cmn_sta_info *sta;
	s8 rssi_max_tmp = 0, rssi_min_tmp = 100;
	u8 i;
	u8 sta_cnt = 0;

	if (!dm->is_linked)
		return;

	PHYDM_DBG(dm, DBG_RSSI_MNTR, "%s ======>\n", __func__);

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		sta = dm->phydm_sta_info[i];
		if (is_sta_active(sta)) {
			sta_cnt++;

			if (sta->rssi_stat.rssi < rssi_min_tmp) {
				rssi_min_tmp = sta->rssi_stat.rssi;
				dm->rssi_min_macid = i;
			}

			if (sta->rssi_stat.rssi > rssi_max_tmp) {
				rssi_max_tmp = sta->rssi_stat.rssi;
				dm->rssi_max_macid = i;
			}

			/*@[Send RSSI to FW]*/
			if (!sta->ra_info.disable_ra)
				phydm_rssi_monitor_h2c(dm, i);

			if (sta_cnt == dm->number_linked_client)
				break;
		}
	}
	dm->pre_rssi_min = dm->rssi_min;

	dm->rssi_max = (u8)rssi_max_tmp;
	dm->rssi_min = (u8)rssi_min_tmp;
}

void phydm_rssi_monitor_check(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (!(dm->support_ability & ODM_BB_RSSI_MONITOR))
		return;

	/*@for AP watchdog period = 1 sec*/
	if ((dm->phydm_sys_up_time % 2) == 1)
		return;

	PHYDM_DBG(dm, DBG_RSSI_MNTR, "%s ======>\n", __func__);

	phydm_calculate_rssi_min_max(dm);

	PHYDM_DBG(dm, DBG_RSSI_MNTR, "RSSI {max, min} = {%d, %d}\n",
		  dm->rssi_max, dm->rssi_min);
}

void phydm_rssi_monitor_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ra_table *ra_tab = &dm->dm_ra_table;

	dm->pre_rssi_min = 0;
	dm->rssi_max = 0;
	dm->rssi_min = 0;
}

#endif
