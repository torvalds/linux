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

#ifdef PHYDM_CCK_RX_PATHDIV_SUPPORT /* @PHYDM-342*/
void phydm_cck_rx_pathdiv_manaul(void *dm_void, boolean en_cck_rx_pathdiv)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	/* @Can not apply for 98F/14B/97G from DD YC*/
	if (en_cck_rx_pathdiv) {
		odm_set_bb_reg(dm, R_0x1a14, BIT(7), 0x0);
		odm_set_bb_reg(dm, R_0x1a74, BIT(8), 0x1);
	} else {
		odm_set_bb_reg(dm, R_0x1a14, BIT(7), 0x1);
		odm_set_bb_reg(dm, R_0x1a74, BIT(8), 0x0);
	}
}

void phydm_cck_rx_pathdiv_watchdog(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cck_rx_pathdiv *cckrx_t = &dm->dm_cck_rx_pathdiv_table;
	struct phydm_fa_struct *fa_t = &dm->false_alm_cnt;
	u8 rssi_th = 0;
	u32 rssi_a = 0, rssi_b = 0, rssi_avg = 0;

	if (!cckrx_t->en_cck_rx_pathdiv)
		return;

	rssi_a = PHYDM_DIV(cckrx_t->path_a_sum, cckrx_t->path_a_cnt);
	rssi_b = PHYDM_DIV(cckrx_t->path_b_sum, cckrx_t->path_b_cnt);
	rssi_avg = (rssi_a + rssi_b) >> 1;

	pr_debug("Rx-A:%d, Rx-B:%d, avg:%d\n", rssi_a, rssi_b, rssi_avg);

	cckrx_t->path_a_cnt = 0;
	cckrx_t->path_a_sum = 0;
	cckrx_t->path_b_cnt = 0;
	cckrx_t->path_b_sum = 0;

	if (fa_t->cnt_all >= 100)
		rssi_th = cckrx_t->rssi_fa_th;
	else
		rssi_th = cckrx_t->rssi_th;

	if (dm->phy_dbg_info.num_qry_beacon_pkt > 14 && rssi_avg <= rssi_th)
		phydm_cck_rx_pathdiv_manaul(dm, true);
	else
		phydm_cck_rx_pathdiv_manaul(dm, false);
}

void phydm_cck_rx_pathdiv_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cck_rx_pathdiv *cckrx_t = &dm->dm_cck_rx_pathdiv_table;

	cckrx_t->en_cck_rx_pathdiv = false;
	cckrx_t->path_a_cnt = 0;
	cckrx_t->path_a_sum = 0;
	cckrx_t->path_b_cnt = 0;
	cckrx_t->path_b_sum = 0;
	cckrx_t->rssi_fa_th = 45;
	cckrx_t->rssi_th = 25;
}

void phydm_process_rssi_for_cck_rx_pathdiv(void *dm_void, void *phy_info_void,
					   void *pkt_info_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_phyinfo_struct *phy_info = NULL;
	struct phydm_perpkt_info_struct *pktinfo = NULL;
	struct phydm_cck_rx_pathdiv *cckrx_t = &dm->dm_cck_rx_pathdiv_table;

	phy_info = (struct phydm_phyinfo_struct *)phy_info_void;
	pktinfo = (struct phydm_perpkt_info_struct *)pkt_info_void;

	if (!(pktinfo->is_packet_to_self || pktinfo->is_packet_match_bssid))
		return;

	if (pktinfo->is_cck_rate)
		return;

	cckrx_t->path_a_sum += phy_info->rx_mimo_signal_strength[0];
	cckrx_t->path_a_cnt++;

	cckrx_t->path_b_sum += phy_info->rx_mimo_signal_strength[1];
	cckrx_t->path_b_cnt++;
}

void phydm_cck_rx_pathdiv_dbg(void *dm_void, char input[][16], u32 *_used,
			      char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cck_rx_pathdiv *cckrx_t = &dm->dm_cck_rx_pathdiv_table;
	char help[] = "-h";
	u32 var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;
	u8 i = 0;

	if (!(dm->support_ic_type & ODM_RTL8822C))
		return;

	for (i = 0; i < 3; i++) {
		PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
	}

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "CCK rx pathdiv manual on: {1} {En}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "CCK rx pathdiv watchdog on: {2} {En}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "CCK rx pathdiv rssi_th : {3} {th} {fa_th}\n");
	} else if (var1[0] == 1) {
		if (var1[1] == 1)
			phydm_cck_rx_pathdiv_manaul(dm, true);
		else
			phydm_cck_rx_pathdiv_manaul(dm, false);
	} else if (var1[0] == 2) {
		if (var1[1] == 1) {
			cckrx_t->en_cck_rx_pathdiv = true;
		} else {
			cckrx_t->en_cck_rx_pathdiv = false;
			phydm_cck_rx_pathdiv_manaul(dm, false);
		}
	} else if (var1[0] == 3) {
		cckrx_t->rssi_th = (u8)var1[1];
		cckrx_t->rssi_fa_th = (u8)var1[2];
	}
	*_used = used;
	*_out_len = out_len;
}
#endif
