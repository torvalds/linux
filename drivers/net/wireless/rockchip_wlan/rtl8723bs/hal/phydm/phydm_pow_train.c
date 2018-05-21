/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/

/* ************************************************************
 * include files
 * ************************************************************ */

#include "mp_precomp.h"
#include "phydm_precomp.h"

#ifdef PHYDM_POWER_TRAINING_SUPPORT
void
phydm_reset_pt_para(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_pow_train_stuc	*p_pow_train_t = &(p_dm->pow_train_table);

	p_pow_train_t->pow_train_score = 0;
	p_dm->phy_dbg_info.num_qry_phy_status_ofdm = 0;
	p_dm->phy_dbg_info.num_qry_phy_status_cck = 0;
}

void
phydm_update_power_training_state(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_pow_train_stuc	*p_pow_train_t = &(p_dm->pow_train_table);
	struct phydm_fa_struct			*p_fa_cnt = &(p_dm->false_alm_cnt);
	struct phydm_dig_struct		*p_dig_t = &p_dm->dm_dig_table;
	u32	pt_score_tmp = 0;
	u32 crc_ok_cnt;
	u32 cca_all_cnt;


	/*is_disable_power_training is the key to H2C to disable/enable power training*/
	/*if is_disable_power_training == 1, it will use largest power*/
	if (!(p_dm->support_ability & ODM_BB_PWR_TRAIN)) {
		p_dm->is_disable_power_training = true;
		phydm_reset_pt_para(p_dm);
		return;
	}

	PHYDM_DBG(p_dm, DBG_PWR_TRAIN, ("%s ======>\n", __FUNCTION__));

	if (p_pow_train_t->force_power_training_state == DISABLE_POW_TRAIN) {
		
		p_dm->is_disable_power_training = true;
		phydm_reset_pt_para(p_dm);
		PHYDM_DBG(p_dm, DBG_PWR_TRAIN, ("Disable PT\n"));
		return;

	} else if (p_pow_train_t->force_power_training_state == ENABLE_POW_TRAIN) {
	
		p_dm->is_disable_power_training = false;
		phydm_reset_pt_para(p_dm);
		PHYDM_DBG(p_dm, DBG_PWR_TRAIN, ("Enable PT\n"));
		return;

	} else if (p_pow_train_t->force_power_training_state == DYNAMIC_POW_TRAIN) {

		PHYDM_DBG(p_dm, DBG_PWR_TRAIN, ("Dynamic PT\n"));

		if (!p_dm->is_linked) {
			p_dm->is_disable_power_training = true;
			p_pow_train_t->pow_train_score = 0;
			p_dm->phy_dbg_info.num_qry_phy_status_ofdm = 0;
			p_dm->phy_dbg_info.num_qry_phy_status_cck = 0;

			PHYDM_DBG(p_dm, DBG_PWR_TRAIN, ("PT is disabled due to no link.\n"));
			return;
		}

		/* First connect */
		if ((p_dm->is_linked) && (p_dig_t->is_media_connect == false)) {
			p_pow_train_t->pow_train_score = 0;
			p_dm->phy_dbg_info.num_qry_phy_status_ofdm = 0;
			p_dm->phy_dbg_info.num_qry_phy_status_cck = 0;
			PHYDM_DBG(p_dm, DBG_PWR_TRAIN, ("(PT)First Connect\n"));
			return;
		}

		/* Compute score */
		crc_ok_cnt = p_dm->phy_dbg_info.num_qry_phy_status_ofdm + p_dm->phy_dbg_info.num_qry_phy_status_cck;
		cca_all_cnt = p_fa_cnt->cnt_cca_all;

		if (crc_ok_cnt < cca_all_cnt) {
			/* crc_ok <= (2/3)*cca */
			if ((crc_ok_cnt + (crc_ok_cnt >> 1)) <= cca_all_cnt)
				pt_score_tmp = DISABLE_PT_SCORE;

			/* crc_ok <= (4/5)*cca */
			else if ((crc_ok_cnt + (crc_ok_cnt >> 2)) <= cca_all_cnt)
				pt_score_tmp = KEEP_PRE_PT_SCORE;

			/* crc_ok > (4/5)*cca */
			else
				pt_score_tmp = ENABLE_PT_SCORE;
		} else {
			pt_score_tmp = ENABLE_PT_SCORE;
		}

		PHYDM_DBG(p_dm, DBG_PWR_TRAIN, ("crc_ok_cnt = %d, cnt_cca_all = %d\n",
				crc_ok_cnt, cca_all_cnt));

		PHYDM_DBG(p_dm, DBG_PWR_TRAIN, ("num_qry_phy_status_ofdm = %d, num_qry_phy_status_cck = %d\n",
			p_dm->phy_dbg_info.num_qry_phy_status_ofdm, p_dm->phy_dbg_info.num_qry_phy_status_cck));
		
		PHYDM_DBG(p_dm, DBG_PWR_TRAIN, ("pt_score_tmp = %d\n", pt_score_tmp));
		PHYDM_DBG(p_dm, DBG_PWR_TRAIN, ("pt_score_tmp = 0(DISABLE), 1(KEEP), 2(ENABLE)\n"));

		/* smoothing */
		p_pow_train_t->pow_train_score = (pt_score_tmp << 4) + (p_pow_train_t->pow_train_score >> 1) + (p_pow_train_t->pow_train_score >> 2);
		pt_score_tmp = (p_pow_train_t->pow_train_score + 32) >> 6;
		PHYDM_DBG(p_dm, DBG_PWR_TRAIN, ("pow_train_score = %d, score after smoothing = %d\n",
				p_pow_train_t->pow_train_score, pt_score_tmp));

		/* mode decision */
		if (pt_score_tmp == ENABLE_PT_SCORE) {
			
			p_dm->is_disable_power_training = false;
			PHYDM_DBG(p_dm, DBG_PWR_TRAIN, ("Enable power training under dynamic.\n"));
			
		} else if (pt_score_tmp == DISABLE_PT_SCORE) {
		
			p_dm->is_disable_power_training = true;
			PHYDM_DBG(p_dm, DBG_PWR_TRAIN, ("Disable PT due to noisy.\n"));
		}

		PHYDM_DBG(p_dm, DBG_PWR_TRAIN, ("Final, score = %d, is_disable_power_training = %d\n",
			pt_score_tmp, p_dm->is_disable_power_training));

		p_dm->phy_dbg_info.num_qry_phy_status_ofdm = 0;
		p_dm->phy_dbg_info.num_qry_phy_status_cck = 0;
	} else {
	
		p_dm->is_disable_power_training = true;
		phydm_reset_pt_para(p_dm);

		PHYDM_DBG(p_dm, DBG_PWR_TRAIN, ("PT is disabled due to unknown pt state.\n"));
		return;
	}
}

void
phydm_pow_train_debug(
	void		*p_dm_void,
	char		input[][16],
	u32		*_used,
	char		*output,
	u32		*_out_len,
	u32		input_num
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_pow_train_stuc	*p_pow_train_t = &(p_dm->pow_train_table);
	char		help[] = "-h";
	u32		var1[10] = {0};
	u32		used = *_used;
	u32		out_len = *_out_len;
	u32		i;

	if ((strcmp(input[1], help) == 0)) {
		PHYDM_SNPRINTF((output + used, out_len - used, "0: Dynamic state\n"));
		PHYDM_SNPRINTF((output + used, out_len - used, "1: Enable PT\n"));
		PHYDM_SNPRINTF((output + used, out_len - used, "2: Disable PT\n"));
		
	} else {

		for (i = 0; i < 10; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);
			}
		}

		if (var1[0] == 0) {
			p_pow_train_t->force_power_training_state = DYNAMIC_POW_TRAIN;
			PHYDM_SNPRINTF((output + used, out_len - used, "Dynamic state\n"));
		} else if (var1[0] == 1) {
			p_pow_train_t->force_power_training_state = ENABLE_POW_TRAIN;
			PHYDM_SNPRINTF((output + used, out_len - used, "Enable PT\n"));
		} else if (var1[0] == 2) {
			p_pow_train_t->force_power_training_state = DISABLE_POW_TRAIN;
			PHYDM_SNPRINTF((output + used, out_len - used, "Disable PT\n"));
		} else {
			PHYDM_SNPRINTF((output + used, out_len - used, "Set Error\n"));
		}		
	}

	*_used = used;
	*_out_len = out_len;
}


#endif



