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

/*************************************************************
 * include files
 ************************************************************/

#include "mp_precomp.h"
#include "phydm_precomp.h"

#ifdef PHYDM_POWER_TRAINING_SUPPORT
void phydm_reset_pt_para(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_pow_train_stuc *pt_t = &dm->pow_train_table;

	pt_t->pow_train_score = 0;
}

void phydm_update_power_training_state(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_pow_train_stuc *pt_t = &dm->pow_train_table;
	struct phydm_fa_struct *fa_cnt = &dm->false_alm_cnt;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u32 pt_score_tmp = ENABLE_PT_SCORE;
	u32 crc_ok_cnt = 0;
	u32 cca_cnt = 0;

	/*@is_disable_power_training is the key to H2C to disable/enable PT*/
	/*@if is_disable_power_training == 1, it will use largest power*/
	if (!(dm->support_ability & ODM_BB_PWR_TRAIN) || !dm->is_linked) {
		dm->is_disable_power_training = true;
		phydm_reset_pt_para(dm);
		return;
	}

	PHYDM_DBG(dm, DBG_PWR_TRAIN, "%s ======>\n", __func__);

	if (pt_t->pt_state == DISABLE_POW_TRAIN) {
		dm->is_disable_power_training = true;
		phydm_reset_pt_para(dm);
		PHYDM_DBG(dm, DBG_PWR_TRAIN, "Disable PT\n");
		return;

	} else if (pt_t->pt_state == ENABLE_POW_TRAIN) {
		dm->is_disable_power_training = false;
		phydm_reset_pt_para(dm);
		PHYDM_DBG(dm, DBG_PWR_TRAIN, "Enable PT\n");
		return;

	} else if (pt_t->pt_state == DYNAMIC_POW_TRAIN) {
		PHYDM_DBG(dm, DBG_PWR_TRAIN, "Dynamic PT\n");

		/* @Compute score */
		crc_ok_cnt = dm->phy_dbg_info.num_qry_phy_status_ofdm +
			     dm->phy_dbg_info.num_qry_phy_status_cck;
		cca_cnt = fa_cnt->cnt_cca_all;
#if 0
		if (crc_ok_cnt > cca_cnt) { /*invalid situation*/
			pt_score_tmp = KEEP_PRE_PT_SCORE;
			return;
		} else if ((crc_ok_cnt + (crc_ok_cnt >> 1)) <= cca_cnt) {
		/* @???crc_ok <= (2/3)*cca */
			pt_score_tmp = DISABLE_PT_SCORE;
			dm->is_disable_power_training = true;
		} else if ((crc_ok_cnt + (crc_ok_cnt >> 2)) <= cca_cnt) {
		/* @???crc_ok <= (4/5)*cca */
			pt_score_tmp = KEEP_PRE_PT_SCORE;
		} else {
		/* @???crc_ok > (4/5)*cca */
			pt_score_tmp = ENABLE_PT_SCORE;
			dm->is_disable_power_training = false;
		}
#endif
		if (ccx->nhm_ratio > 10) {
			pt_score_tmp = DISABLE_PT_SCORE;
			dm->is_disable_power_training = true;
		} else if (ccx->nhm_ratio < 5) {
			pt_score_tmp = ENABLE_PT_SCORE;
			dm->is_disable_power_training = false;
		} else {
			pt_score_tmp = KEEP_PRE_PT_SCORE;
		}

		PHYDM_DBG(dm, DBG_PWR_TRAIN,
			  "pkt_cnt{ofdm,cck,all} = {%d, %d, %d}, cnt_cca_all=%d\n",
			  dm->phy_dbg_info.num_qry_phy_status_ofdm,
			  dm->phy_dbg_info.num_qry_phy_status_cck,
			  crc_ok_cnt, cca_cnt);

		PHYDM_DBG(dm, DBG_PWR_TRAIN, "pt_score_tmp=%d\n", pt_score_tmp);

		/* smoothing */
		pt_t->pow_train_score = (pt_score_tmp << 4) +
					(pt_t->pow_train_score >> 1) +
					(pt_t->pow_train_score >> 2);

		pt_score_tmp = (pt_t->pow_train_score + 32) >> 6;

		PHYDM_DBG(dm, DBG_PWR_TRAIN,
			  "pow_train_score = %d, score after smoothing = %d, is_disable_PT = %d\n",
			  pt_t->pow_train_score, pt_score_tmp,
			  dm->is_disable_power_training);
	} else {
		PHYDM_DBG(dm, DBG_PWR_TRAIN, "[%s]warning\n", __func__);
	}
}

void phydm_pow_train_debug(
	void *dm_void,
	char input[][16],
	u32 *_used,
	char *output,
	u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_pow_train_stuc *pt_t = &dm->pow_train_table;
	char help[] = "-h";
	u32 var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 i;

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{0: Auto PT, 1:enable, 2: disable}\n");
	} else {
		for (i = 0; i < 10; i++) {
			if (input[i + 1])
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);
		}

		if (var1[0] == 0)
			pt_t->pt_state = DYNAMIC_POW_TRAIN;
		else if (var1[0] == 1)
			pt_t->pt_state = ENABLE_POW_TRAIN;
		else if (var1[0] == 2)
			pt_t->pt_state = DISABLE_POW_TRAIN;

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "PT state = %d\n", pt_t->pt_state);
	}

	*_used = used;
	*_out_len = out_len;
}

#endif
