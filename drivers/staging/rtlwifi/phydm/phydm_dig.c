/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
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

/* ************************************************************
 * include files
 * *************************************************************/
#include "mp_precomp.h"
#include "phydm_precomp.h"

static int get_igi_for_diff(int);

static inline void phydm_check_ap_write_dig(struct phy_dm_struct *dm,
					    u8 current_igi)
{
	switch (*dm->one_path_cca) {
	case ODM_CCA_2R:
		odm_set_bb_reg(dm, ODM_REG(IGI_A, dm), ODM_BIT(IGI, dm),
			       current_igi);

		if (dm->rf_type > ODM_1T1R)
			odm_set_bb_reg(dm, ODM_REG(IGI_B, dm), ODM_BIT(IGI, dm),
				       current_igi);
		break;
	case ODM_CCA_1R_A:
		odm_set_bb_reg(dm, ODM_REG(IGI_A, dm), ODM_BIT(IGI, dm),
			       current_igi);
		if (dm->rf_type != ODM_1T1R)
			odm_set_bb_reg(dm, ODM_REG(IGI_B, dm), ODM_BIT(IGI, dm),
				       get_igi_for_diff(current_igi));
		break;
	case ODM_CCA_1R_B:
		odm_set_bb_reg(dm, ODM_REG(IGI_B, dm), ODM_BIT(IGI, dm),
			       get_igi_for_diff(current_igi));
		if (dm->rf_type != ODM_1T1R)
			odm_set_bb_reg(dm, ODM_REG(IGI_A, dm), ODM_BIT(IGI, dm),
				       current_igi);
		break;
	}
}

static inline u8 phydm_get_current_igi(u8 dig_max_of_min, u8 rssi_min,
				       u8 current_igi)
{
	if (rssi_min < dig_max_of_min) {
		if (current_igi < rssi_min)
			return rssi_min;
	} else {
		if (current_igi < dig_max_of_min)
			return dig_max_of_min;
	}
	return current_igi;
}

void odm_change_dynamic_init_gain_thresh(void *dm_void, u32 dm_type,
					 u32 dm_value)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dig_thres *dig_tab = &dm->dm_dig_table;

	if (dm_type == DIG_TYPE_THRESH_HIGH) {
		dig_tab->rssi_high_thresh = dm_value;
	} else if (dm_type == DIG_TYPE_THRESH_LOW) {
		dig_tab->rssi_low_thresh = dm_value;
	} else if (dm_type == DIG_TYPE_ENABLE) {
		dig_tab->dig_enable_flag = true;
	} else if (dm_type == DIG_TYPE_DISABLE) {
		dig_tab->dig_enable_flag = false;
	} else if (dm_type == DIG_TYPE_BACKOFF) {
		if (dm_value > 30)
			dm_value = 30;
		dig_tab->backoff_val = (u8)dm_value;
	} else if (dm_type == DIG_TYPE_RX_GAIN_MIN) {
		if (dm_value == 0)
			dm_value = 0x1;
		dig_tab->rx_gain_range_min = (u8)dm_value;
	} else if (dm_type == DIG_TYPE_RX_GAIN_MAX) {
		if (dm_value > 0x50)
			dm_value = 0x50;
		dig_tab->rx_gain_range_max = (u8)dm_value;
	}
} /* dm_change_dynamic_init_gain_thresh */

static int get_igi_for_diff(int value_IGI)
{
#define ONERCCA_LOW_TH 0x30
#define ONERCCA_LOW_DIFF 8

	if (value_IGI < ONERCCA_LOW_TH) {
		if ((ONERCCA_LOW_TH - value_IGI) < ONERCCA_LOW_DIFF)
			return ONERCCA_LOW_TH;
		else
			return value_IGI + ONERCCA_LOW_DIFF;
	}

	return value_IGI;
}

static void odm_fa_threshold_check(void *dm_void, bool is_dfs_band,
				   bool is_performance, u32 rx_tp, u32 tx_tp,
				   u32 *dm_FA_thres)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	if (dm->is_linked && (is_performance || is_dfs_band)) {
		/*For NIC*/
		dm_FA_thres[0] = DM_DIG_FA_TH0;
		dm_FA_thres[1] = DM_DIG_FA_TH1;
		dm_FA_thres[2] = DM_DIG_FA_TH2;
	} else {
		if (is_dfs_band) {
			/* For DFS band and no link */
			dm_FA_thres[0] = 250;
			dm_FA_thres[1] = 1000;
			dm_FA_thres[2] = 2000;
		} else {
			dm_FA_thres[0] = 2000;
			dm_FA_thres[1] = 4000;
			dm_FA_thres[2] = 5000;
		}
	}
}

static u8 odm_forbidden_igi_check(void *dm_void, u8 dig_dynamic_min,
				  u8 current_igi)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dig_thres *dig_tab = &dm->dm_dig_table;
	struct false_alarm_stat *fa_cnt =
		(struct false_alarm_stat *)phydm_get_structure(
			dm, PHYDM_FALSEALMCNT);
	u8 rx_gain_range_min = dig_tab->rx_gain_range_min;

	if (dig_tab->large_fa_timeout) {
		if (--dig_tab->large_fa_timeout == 0)
			dig_tab->large_fa_hit = 0;
	}

	if (fa_cnt->cnt_all > 10000) {
		ODM_RT_TRACE(dm, ODM_COMP_DIG,
			     "%s(): Abnormally false alarm case.\n", __func__);

		if (dig_tab->large_fa_hit != 3)
			dig_tab->large_fa_hit++;

		if (dig_tab->forbidden_igi < current_igi) {
			dig_tab->forbidden_igi = current_igi;
			dig_tab->large_fa_hit = 1;
			dig_tab->large_fa_timeout = LARGE_FA_TIMEOUT;
		}

		if (dig_tab->large_fa_hit >= 3) {
			if ((dig_tab->forbidden_igi + 2) >
			    dig_tab->rx_gain_range_max)
				rx_gain_range_min = dig_tab->rx_gain_range_max;
			else
				rx_gain_range_min =
					(dig_tab->forbidden_igi + 2);
			dig_tab->recover_cnt = 1800;
			ODM_RT_TRACE(
				dm, ODM_COMP_DIG,
				"%s(): Abnormally false alarm case: recover_cnt = %d\n",
				__func__, dig_tab->recover_cnt);
		}
	}

	else if (fa_cnt->cnt_all > 2000) {
		ODM_RT_TRACE(dm, ODM_COMP_DIG,
			     "Abnormally false alarm case.\n");
		ODM_RT_TRACE(
			dm, ODM_COMP_DIG,
			"cnt_all=%d, cnt_all_pre=%d, current_igi=0x%x, pre_ig_value=0x%x\n",
			fa_cnt->cnt_all, fa_cnt->cnt_all_pre, current_igi,
			dig_tab->pre_ig_value);

		/* fa_cnt->cnt_all = 1.1875*fa_cnt->cnt_all_pre */
		if ((fa_cnt->cnt_all >
		     (fa_cnt->cnt_all_pre + (fa_cnt->cnt_all_pre >> 3) +
		      (fa_cnt->cnt_all_pre >> 4))) &&
		    (current_igi < dig_tab->pre_ig_value)) {
			if (dig_tab->large_fa_hit != 3)
				dig_tab->large_fa_hit++;

			if (dig_tab->forbidden_igi < current_igi) {
				ODM_RT_TRACE(
					dm, ODM_COMP_DIG,
					"Updating forbidden_igi by current_igi, forbidden_igi=0x%x, current_igi=0x%x\n",
					dig_tab->forbidden_igi, current_igi);

				dig_tab->forbidden_igi = current_igi;
				dig_tab->large_fa_hit = 1;
				dig_tab->large_fa_timeout = LARGE_FA_TIMEOUT;
			}
		}

		if (dig_tab->large_fa_hit >= 3) {
			ODM_RT_TRACE(
				dm, ODM_COMP_DIG,
				"FaHit is greater than 3, rx_gain_range_max=0x%x, rx_gain_range_min=0x%x, forbidden_igi=0x%x\n",
				dig_tab->rx_gain_range_max, rx_gain_range_min,
				dig_tab->forbidden_igi);

			if ((dig_tab->forbidden_igi + 1) >
			    dig_tab->rx_gain_range_max)
				rx_gain_range_min = dig_tab->rx_gain_range_max;
			else
				rx_gain_range_min =
					(dig_tab->forbidden_igi + 1);

			dig_tab->recover_cnt = 1200;
			ODM_RT_TRACE(
				dm, ODM_COMP_DIG,
				"Abnormally false alarm case: recover_cnt = %d,  rx_gain_range_min = 0x%x\n",
				dig_tab->recover_cnt, rx_gain_range_min);
		}
	} else {
		if (dig_tab->recover_cnt != 0) {
			dig_tab->recover_cnt--;
			ODM_RT_TRACE(dm, ODM_COMP_DIG,
				     "%s(): Normal Case: recover_cnt = %d\n",
				     __func__, dig_tab->recover_cnt);
			return rx_gain_range_min;
		}

		if (dig_tab->large_fa_hit >= 3) {
			dig_tab->large_fa_hit = 0;
			return rx_gain_range_min;
		}

		if ((dig_tab->forbidden_igi - 2) <
		    dig_dynamic_min) { /* DM_DIG_MIN) */
			dig_tab->forbidden_igi =
				dig_dynamic_min; /* DM_DIG_MIN; */
			rx_gain_range_min = dig_dynamic_min; /* DM_DIG_MIN; */
			ODM_RT_TRACE(dm, ODM_COMP_DIG,
				     "%s(): Normal Case: At Lower Bound\n",
				     __func__);
		} else {
			if (dig_tab->large_fa_hit == 0) {
				dig_tab->forbidden_igi -= 2;
				rx_gain_range_min =
					(dig_tab->forbidden_igi + 2);
				ODM_RT_TRACE(
					dm, ODM_COMP_DIG,
					"%s(): Normal Case: Approach Lower Bound\n",
					__func__);
			}
		}
	}

	return rx_gain_range_min;
}

static void phydm_set_big_jump_step(void *dm_void, u8 current_igi)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dig_thres *dig_tab = &dm->dm_dig_table;
	u8 step1[8] = {24, 30, 40, 50, 60, 70, 80, 90};
	u8 i;

	if (dig_tab->enable_adjust_big_jump == 0)
		return;

	for (i = 0; i <= dig_tab->big_jump_step1; i++) {
		if ((current_igi + step1[i]) >
		    dig_tab->big_jump_lmt[dig_tab->agc_table_idx]) {
			if (i != 0)
				i = i - 1;
			break;
		} else if (i == dig_tab->big_jump_step1) {
			break;
		}
	}
	if (dm->support_ic_type & ODM_RTL8822B)
		odm_set_bb_reg(dm, 0x8c8, 0xe, i);
	else if (dm->support_ic_type & ODM_RTL8197F)
		odm_set_bb_reg(dm, ODM_REG_BB_AGC_SET_2_11N, 0xe, i);

	ODM_RT_TRACE(dm, ODM_COMP_DIG,
		     "%s(): bigjump = %d (ori = 0x%x), LMT=0x%x\n", __func__, i,
		     dig_tab->big_jump_step1,
		     dig_tab->big_jump_lmt[dig_tab->agc_table_idx]);
}

void odm_write_dig(void *dm_void, u8 current_igi)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dig_thres *dig_tab = &dm->dm_dig_table;

	if (dig_tab->is_stop_dig) {
		ODM_RT_TRACE(dm, ODM_COMP_DIG, "%s(): Stop Writing IGI\n",
			     __func__);
		return;
	}

	ODM_RT_TRACE(dm, ODM_COMP_DIG,
		     "%s(): ODM_REG(IGI_A,dm)=0x%x, ODM_BIT(IGI,dm)=0x%x\n",
		     __func__, ODM_REG(IGI_A, dm), ODM_BIT(IGI, dm));

	/* 1 Check initial gain by upper bound */
	if ((!dig_tab->is_psd_in_progress) && dm->is_linked) {
		if (current_igi > dig_tab->rx_gain_range_max) {
			ODM_RT_TRACE(
				dm, ODM_COMP_DIG,
				"%s(): current_igi(0x%02x) is larger than upper bound !!\n",
				__func__, current_igi);
			current_igi = dig_tab->rx_gain_range_max;
		}
		if (dm->support_ability & ODM_BB_ADAPTIVITY &&
		    dm->adaptivity_flag) {
			if (current_igi > dm->adaptivity_igi_upper)
				current_igi = dm->adaptivity_igi_upper;

			ODM_RT_TRACE(
				dm, ODM_COMP_DIG,
				"%s(): adaptivity case: Force upper bound to 0x%x !!!!!!\n",
				__func__, current_igi);
		}
	}

	if (dig_tab->cur_ig_value != current_igi) {
		/* Modify big jump step for 8822B and 8197F */
		if (dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8197F))
			phydm_set_big_jump_step(dm, current_igi);

		/* Set IGI value of CCK for new CCK AGC */
		if (dm->cck_new_agc) {
			if (dm->support_ic_type & ODM_IC_PHY_STATUE_NEW_TYPE)
				odm_set_bb_reg(dm, 0xa0c, 0x00003f00,
					       (current_igi >> 1));
		}

		/*Add by YuChen for USB IO too slow issue*/
		if ((dm->support_ability & ODM_BB_ADAPTIVITY) &&
		    (current_igi > dig_tab->cur_ig_value)) {
			dig_tab->cur_ig_value = current_igi;
			phydm_adaptivity(dm);
		}

		/* 1 Set IGI value */
		if (dm->support_platform & (ODM_WIN | ODM_CE)) {
			odm_set_bb_reg(dm, ODM_REG(IGI_A, dm), ODM_BIT(IGI, dm),
				       current_igi);

			if (dm->rf_type > ODM_1T1R)
				odm_set_bb_reg(dm, ODM_REG(IGI_B, dm),
					       ODM_BIT(IGI, dm), current_igi);

		} else if (dm->support_platform & (ODM_AP)) {
			phydm_check_ap_write_dig(dm, current_igi);
		}

		dig_tab->cur_ig_value = current_igi;
	}

	ODM_RT_TRACE(dm, ODM_COMP_DIG, "%s(): current_igi(0x%02x).\n", __func__,
		     current_igi);
}

void odm_pause_dig(void *dm_void, enum phydm_pause_type pause_type,
		   enum phydm_pause_level pause_level, u8 igi_value)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dig_thres *dig_tab = &dm->dm_dig_table;
	s8 max_level;

	ODM_RT_TRACE(dm, ODM_COMP_DIG, "%s()=========> level = %d\n", __func__,
		     pause_level);

	if ((dig_tab->pause_dig_level == 0) &&
	    (!(dm->support_ability & ODM_BB_DIG) ||
	     !(dm->support_ability & ODM_BB_FA_CNT))) {
		ODM_RT_TRACE(
			dm, ODM_COMP_DIG,
			"%s(): Return: support_ability DIG or FA is disabled !!\n",
			__func__);
		return;
	}

	if (pause_level > DM_DIG_MAX_PAUSE_TYPE) {
		ODM_RT_TRACE(dm, ODM_COMP_DIG,
			     "%s(): Return: Wrong pause level !!\n", __func__);
		return;
	}

	ODM_RT_TRACE(dm, ODM_COMP_DIG,
		     "%s(): pause level = 0x%x, Current value = 0x%x\n",
		     __func__, dig_tab->pause_dig_level, igi_value);
	ODM_RT_TRACE(
		dm, ODM_COMP_DIG,
		"%s(): pause value = 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		__func__, dig_tab->pause_dig_value[7],
		dig_tab->pause_dig_value[6], dig_tab->pause_dig_value[5],
		dig_tab->pause_dig_value[4], dig_tab->pause_dig_value[3],
		dig_tab->pause_dig_value[2], dig_tab->pause_dig_value[1],
		dig_tab->pause_dig_value[0]);

	switch (pause_type) {
	/* Pause DIG */
	case PHYDM_PAUSE: {
		/* Disable DIG */
		odm_cmn_info_update(dm, ODM_CMNINFO_ABILITY,
				    dm->support_ability & (~ODM_BB_DIG));
		ODM_RT_TRACE(dm, ODM_COMP_DIG, "%s(): Pause DIG !!\n",
			     __func__);

		/* Backup IGI value */
		if (dig_tab->pause_dig_level == 0) {
			dig_tab->igi_backup = dig_tab->cur_ig_value;
			ODM_RT_TRACE(
				dm, ODM_COMP_DIG,
				"%s(): Backup IGI  = 0x%x, new IGI = 0x%x\n",
				__func__, dig_tab->igi_backup, igi_value);
		}

		/* Record IGI value */
		dig_tab->pause_dig_value[pause_level] = igi_value;

		/* Update pause level */
		dig_tab->pause_dig_level =
			(dig_tab->pause_dig_level | BIT(pause_level));

		/* Write new IGI value */
		if (BIT(pause_level + 1) > dig_tab->pause_dig_level) {
			odm_write_dig(dm, igi_value);
			ODM_RT_TRACE(dm, ODM_COMP_DIG,
				     "%s(): IGI of higher level = 0x%x\n",
				     __func__, igi_value);
		}
		break;
	}
	/* Resume DIG */
	case PHYDM_RESUME: {
		/* check if the level is illegal or not */
		if ((dig_tab->pause_dig_level & (BIT(pause_level))) != 0) {
			dig_tab->pause_dig_level = dig_tab->pause_dig_level &
						   (~(BIT(pause_level)));
			dig_tab->pause_dig_value[pause_level] = 0;
			ODM_RT_TRACE(dm, ODM_COMP_DIG, "%s(): Resume DIG !!\n",
				     __func__);
		} else {
			ODM_RT_TRACE(dm, ODM_COMP_DIG,
				     "%s(): Wrong resume level !!\n", __func__);
			break;
		}

		/* Resume DIG */
		if (dig_tab->pause_dig_level == 0) {
			/* Write backup IGI value */
			odm_write_dig(dm, dig_tab->igi_backup);
			dig_tab->is_ignore_dig = true;
			ODM_RT_TRACE(dm, ODM_COMP_DIG,
				     "%s(): Write original IGI = 0x%x\n",
				     __func__, dig_tab->igi_backup);

			/* Enable DIG */
			odm_cmn_info_update(dm, ODM_CMNINFO_ABILITY,
					    dm->support_ability | ODM_BB_DIG);
			break;
		}

		if (BIT(pause_level) <= dig_tab->pause_dig_level)
			break;

		/* Calculate the maximum level now */
		for (max_level = (pause_level - 1); max_level >= 0;
		     max_level--) {
			if ((dig_tab->pause_dig_level & BIT(max_level)) > 0)
				break;
		}

		/* write IGI of lower level */
		odm_write_dig(dm, dig_tab->pause_dig_value[max_level]);
		ODM_RT_TRACE(dm, ODM_COMP_DIG,
			     "%s(): Write IGI (0x%x) of level (%d)\n", __func__,
			     dig_tab->pause_dig_value[max_level], max_level);
		break;
	}
	default:
		ODM_RT_TRACE(dm, ODM_COMP_DIG, "%s(): Wrong  type !!\n",
			     __func__);
		break;
	}

	ODM_RT_TRACE(dm, ODM_COMP_DIG,
		     "%s(): pause level = 0x%x, Current value = 0x%x\n",
		     __func__, dig_tab->pause_dig_level, igi_value);
	ODM_RT_TRACE(
		dm, ODM_COMP_DIG,
		"%s(): pause value = 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		__func__, dig_tab->pause_dig_value[7],
		dig_tab->pause_dig_value[6], dig_tab->pause_dig_value[5],
		dig_tab->pause_dig_value[4], dig_tab->pause_dig_value[3],
		dig_tab->pause_dig_value[2], dig_tab->pause_dig_value[1],
		dig_tab->pause_dig_value[0]);
}

static bool odm_dig_abort(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dig_thres *dig_tab = &dm->dm_dig_table;

	/* support_ability */
	if (!(dm->support_ability & ODM_BB_FA_CNT)) {
		ODM_RT_TRACE(
			dm, ODM_COMP_DIG,
			"%s(): Return: support_ability ODM_BB_FA_CNT is disabled\n",
			__func__);
		return true;
	}

	/* support_ability */
	if (!(dm->support_ability & ODM_BB_DIG)) {
		ODM_RT_TRACE(
			dm, ODM_COMP_DIG,
			"%s(): Return: support_ability ODM_BB_DIG is disabled\n",
			__func__);
		return true;
	}

	/* ScanInProcess */
	if (*dm->is_scan_in_process) {
		ODM_RT_TRACE(dm, ODM_COMP_DIG,
			     "%s(): Return: In Scan Progress\n", __func__);
		return true;
	}

	if (dig_tab->is_ignore_dig) {
		dig_tab->is_ignore_dig = false;
		ODM_RT_TRACE(dm, ODM_COMP_DIG, "%s(): Return: Ignore DIG\n",
			     __func__);
		return true;
	}

	/* add by Neil Chen to avoid PSD is processing */
	if (!dm->is_dm_initial_gain_enable) {
		ODM_RT_TRACE(dm, ODM_COMP_DIG,
			     "%s(): Return: PSD is Processing\n", __func__);
		return true;
	}

	return false;
}

void odm_dig_init(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dig_thres *dig_tab = &dm->dm_dig_table;
	u32 ret_value;
	u8 i;

	dig_tab->is_stop_dig = false;
	dig_tab->is_ignore_dig = false;
	dig_tab->is_psd_in_progress = false;
	dig_tab->cur_ig_value =
		(u8)odm_get_bb_reg(dm, ODM_REG(IGI_A, dm), ODM_BIT(IGI, dm));
	dig_tab->pre_ig_value = 0;
	dig_tab->rssi_low_thresh = DM_DIG_THRESH_LOW;
	dig_tab->rssi_high_thresh = DM_DIG_THRESH_HIGH;
	dig_tab->fa_low_thresh = DM_FALSEALARM_THRESH_LOW;
	dig_tab->fa_high_thresh = DM_FALSEALARM_THRESH_HIGH;
	dig_tab->backoff_val = DM_DIG_BACKOFF_DEFAULT;
	dig_tab->backoff_val_range_max = DM_DIG_BACKOFF_MAX;
	dig_tab->backoff_val_range_min = DM_DIG_BACKOFF_MIN;
	dig_tab->pre_cck_cca_thres = 0xFF;
	dig_tab->cur_cck_cca_thres = 0x83;
	dig_tab->forbidden_igi = DM_DIG_MIN_NIC;
	dig_tab->large_fa_hit = 0;
	dig_tab->large_fa_timeout = 0;
	dig_tab->recover_cnt = 0;
	dig_tab->is_media_connect_0 = false;
	dig_tab->is_media_connect_1 = false;

	/*To initialize dm->is_dm_initial_gain_enable==false to avoid DIG err*/
	dm->is_dm_initial_gain_enable = true;

	dig_tab->dig_dynamic_min_0 = DM_DIG_MIN_NIC;
	dig_tab->dig_dynamic_min_1 = DM_DIG_MIN_NIC;

	/* To Initi BT30 IGI */
	dig_tab->bt30_cur_igi = 0x32;

	odm_memory_set(dm, dig_tab->pause_dig_value, 0,
		       (DM_DIG_MAX_PAUSE_TYPE + 1));
	dig_tab->pause_dig_level = 0;
	odm_memory_set(dm, dig_tab->pause_cckpd_value, 0,
		       (DM_DIG_MAX_PAUSE_TYPE + 1));
	dig_tab->pause_cckpd_level = 0;

	if (dm->board_type & (ODM_BOARD_EXT_PA | ODM_BOARD_EXT_LNA)) {
		dig_tab->rx_gain_range_max = DM_DIG_MAX_NIC;
		dig_tab->rx_gain_range_min = DM_DIG_MIN_NIC;
	} else {
		dig_tab->rx_gain_range_max = DM_DIG_MAX_NIC;
		dig_tab->rx_gain_range_min = DM_DIG_MIN_NIC;
	}

	dig_tab->enable_adjust_big_jump = 1;
	if (dm->support_ic_type & ODM_RTL8822B) {
		ret_value = odm_get_bb_reg(dm, 0x8c8, MASKLWORD);
		dig_tab->big_jump_step1 = (u8)(ret_value & 0xe) >> 1;
		dig_tab->big_jump_step2 = (u8)(ret_value & 0x30) >> 4;
		dig_tab->big_jump_step3 = (u8)(ret_value & 0xc0) >> 6;

	} else if (dm->support_ic_type & ODM_RTL8197F) {
		ret_value =
			odm_get_bb_reg(dm, ODM_REG_BB_AGC_SET_2_11N, MASKLWORD);
		dig_tab->big_jump_step1 = (u8)(ret_value & 0xe) >> 1;
		dig_tab->big_jump_step2 = (u8)(ret_value & 0x30) >> 4;
		dig_tab->big_jump_step3 = (u8)(ret_value & 0xc0) >> 6;
	}
	if (dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8197F)) {
		for (i = 0; i < sizeof(dig_tab->big_jump_lmt); i++) {
			if (dig_tab->big_jump_lmt[i] == 0)
				dig_tab->big_jump_lmt[i] =
					0x64; /* Set -10dBm as default value */
		}
	}
}

void odm_DIG(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	/* Common parameters */
	struct dig_thres *dig_tab = &dm->dm_dig_table;
	struct false_alarm_stat *fa_cnt =
		(struct false_alarm_stat *)phydm_get_structure(
			dm, PHYDM_FALSEALMCNT);
	bool first_connect, first_dis_connect;
	u8 dig_max_of_min, dig_dynamic_min;
	u8 dm_dig_max, dm_dig_min;
	u8 current_igi = dig_tab->cur_ig_value;
	u8 offset;
	u32 dm_FA_thres[3];
	u32 tx_tp = 0, rx_tp = 0;
	bool is_dfs_band = false;
	bool is_performance = true, is_first_tp_target = false,
	     is_first_coverage = false;

	if (odm_dig_abort(dm))
		return;

	ODM_RT_TRACE(dm, ODM_COMP_DIG, "DIG Start===>\n");

	/* 1 Update status */
	{
		dig_dynamic_min = dig_tab->dig_dynamic_min_0;
		first_connect = (dm->is_linked) && !dig_tab->is_media_connect_0;
		first_dis_connect =
			(!dm->is_linked) && dig_tab->is_media_connect_0;
	}

	/* 1 Boundary Decision */
	{
		/* 2 For WIN\CE */
		if (dm->support_ic_type >= ODM_RTL8188E)
			dm_dig_max = 0x5A;
		else
			dm_dig_max = DM_DIG_MAX_NIC;

		if (dm->support_ic_type != ODM_RTL8821)
			dm_dig_min = DM_DIG_MIN_NIC;
		else
			dm_dig_min = 0x1C;

		dig_max_of_min = DM_DIG_MAX_AP;

		/* Modify lower bound for DFS band */
		if ((((*dm->channel >= 52) && (*dm->channel <= 64)) ||
		     ((*dm->channel >= 100) && (*dm->channel <= 140))) &&
		    phydm_dfs_master_enabled(dm)) {
			is_dfs_band = true;
			if (*dm->band_width == ODM_BW20M)
				dm_dig_min = DM_DIG_MIN_AP_DFS + 2;
			else
				dm_dig_min = DM_DIG_MIN_AP_DFS;
			ODM_RT_TRACE(dm, ODM_COMP_DIG,
				     "DIG: ====== In DFS band ======\n");
		}
	}
	ODM_RT_TRACE(dm, ODM_COMP_DIG,
		     "DIG: Absolutly upper bound = 0x%x, lower bound = 0x%x\n",
		     dm_dig_max, dm_dig_min);

	if (dm->pu1_forced_igi_lb && (*dm->pu1_forced_igi_lb > 0)) {
		ODM_RT_TRACE(dm, ODM_COMP_DIG, "DIG: Force IGI lb to: 0x%02x\n",
			     *dm->pu1_forced_igi_lb);
		dm_dig_min = *dm->pu1_forced_igi_lb;
		dm_dig_max = (dm_dig_min <= dm_dig_max) ? (dm_dig_max) :
							  (dm_dig_min + 1);
	}

	/* 1 Adjust boundary by RSSI */
	if (dm->is_linked && is_performance) {
		/* 2 Modify DIG upper bound */
		/* 4 Modify DIG upper bound for 92E, 8723A\B, 8821 & 8812 BT */
		if ((dm->support_ic_type & (ODM_RTL8192E | ODM_RTL8723B |
					    ODM_RTL8812 | ODM_RTL8821)) &&
		    (dm->is_bt_limited_dig == 1)) {
			offset = 10;
			ODM_RT_TRACE(
				dm, ODM_COMP_DIG,
				"DIG: Coex. case: Force upper bound to RSSI + %d\n",
				offset);
		} else {
			offset = 15;
		}

		if ((dm->rssi_min + offset) > dm_dig_max)
			dig_tab->rx_gain_range_max = dm_dig_max;
		else if ((dm->rssi_min + offset) < dm_dig_min)
			dig_tab->rx_gain_range_max = dm_dig_min;
		else
			dig_tab->rx_gain_range_max = dm->rssi_min + offset;

		/* 2 Modify DIG lower bound */
		/* if(dm->is_one_entry_only) */
		{
			if (dm->rssi_min < dm_dig_min)
				dig_dynamic_min = dm_dig_min;
			else if (dm->rssi_min > dig_max_of_min)
				dig_dynamic_min = dig_max_of_min;
			else
				dig_dynamic_min = dm->rssi_min;

			if (is_dfs_band) {
				dig_dynamic_min = dm_dig_min;
				ODM_RT_TRACE(
					dm, ODM_COMP_DIG,
					"DIG: DFS band: Force lower bound to 0x%x after link\n",
					dm_dig_min);
			}
		}
	} else {
		if (is_performance && is_dfs_band) {
			dig_tab->rx_gain_range_max = 0x28;
			ODM_RT_TRACE(
				dm, ODM_COMP_DIG,
				"DIG: DFS band: Force upper bound to 0x%x before link\n",
				dig_tab->rx_gain_range_max);
		} else {
			if (is_performance)
				dig_tab->rx_gain_range_max = DM_DIG_MAX_OF_MIN;
			else
				dig_tab->rx_gain_range_max = dm_dig_max;
		}
		dig_dynamic_min = dm_dig_min;
	}

	/* 1 Force Lower Bound for AntDiv */
	if (dm->is_linked && !dm->is_one_entry_only &&
	    (dm->support_ic_type & ODM_ANTDIV_SUPPORT) &&
	    (dm->support_ability & ODM_BB_ANT_DIV)) {
		if (dm->ant_div_type == CG_TRX_HW_ANTDIV ||
		    dm->ant_div_type == CG_TRX_SMART_ANTDIV) {
			if (dig_tab->ant_div_rssi_max > dig_max_of_min)
				dig_dynamic_min = dig_max_of_min;
			else
				dig_dynamic_min = (u8)dig_tab->ant_div_rssi_max;
			ODM_RT_TRACE(
				dm, ODM_COMP_DIG,
				"DIG: AntDiv case: Force lower bound to 0x%x\n",
				dig_dynamic_min);
			ODM_RT_TRACE(dm, ODM_COMP_DIG,
				     "DIG: AntDiv case: rssi_max = 0x%x\n",
				     dig_tab->ant_div_rssi_max);
		}
	}
	ODM_RT_TRACE(
		dm, ODM_COMP_DIG,
		"DIG: Adjust boundary by RSSI Upper bound = 0x%x, Lower bound = 0x%x\n",
		dig_tab->rx_gain_range_max, dig_dynamic_min);
	ODM_RT_TRACE(
		dm, ODM_COMP_DIG,
		"DIG: Link status: is_linked = %d, RSSI = %d, bFirstConnect = %d, bFirsrDisConnect = %d\n",
		dm->is_linked, dm->rssi_min, first_connect, first_dis_connect);

	/* 1 Modify DIG lower bound, deal with abnormal case */
	/* 2 Abnormal false alarm case */
	if (is_dfs_band) {
		dig_tab->rx_gain_range_min = dig_dynamic_min;
	} else {
		if (!dm->is_linked) {
			dig_tab->rx_gain_range_min = dig_dynamic_min;

			if (first_dis_connect)
				dig_tab->forbidden_igi = dig_dynamic_min;
		} else {
			dig_tab->rx_gain_range_min = odm_forbidden_igi_check(
				dm, dig_dynamic_min, current_igi);
		}
	}

	/* 2 Abnormal # beacon case */
	if (dm->is_linked && !first_connect) {
		ODM_RT_TRACE(dm, ODM_COMP_DIG, "Beacon Num (%d)\n",
			     dm->phy_dbg_info.num_qry_beacon_pkt);
		if ((dm->phy_dbg_info.num_qry_beacon_pkt < 5) &&
		    (dm->bsta_state)) {
			dig_tab->rx_gain_range_min = 0x1c;
			ODM_RT_TRACE(
				dm, ODM_COMP_DIG,
				"DIG: Abnrormal #beacon (%d) case in STA mode: Force lower bound to 0x%x\n",
				dm->phy_dbg_info.num_qry_beacon_pkt,
				dig_tab->rx_gain_range_min);
		}
	}

	/* 2 Abnormal lower bound case */
	if (dig_tab->rx_gain_range_min > dig_tab->rx_gain_range_max) {
		dig_tab->rx_gain_range_min = dig_tab->rx_gain_range_max;
		ODM_RT_TRACE(
			dm, ODM_COMP_DIG,
			"DIG: Abnrormal lower bound case: Force lower bound to 0x%x\n",
			dig_tab->rx_gain_range_min);
	}

	/* 1 False alarm threshold decision */
	odm_fa_threshold_check(dm, is_dfs_band, is_performance, rx_tp, tx_tp,
			       dm_FA_thres);
	ODM_RT_TRACE(dm, ODM_COMP_DIG,
		     "DIG: False alarm threshold = %d, %d, %d\n",
		     dm_FA_thres[0], dm_FA_thres[1], dm_FA_thres[2]);

	/* 1 Adjust initial gain by false alarm */
	if (dm->is_linked && is_performance) {
		/* 2 After link */
		ODM_RT_TRACE(dm, ODM_COMP_DIG, "DIG: Adjust IGI after link\n");

		if (is_first_tp_target || (first_connect && is_performance)) {
			dig_tab->large_fa_hit = 0;

			if (is_dfs_band) {
				u8 rssi = dm->rssi_min;

				current_igi =
					(dm->rssi_min > 0x28) ? 0x28 : rssi;
				ODM_RT_TRACE(
					dm, ODM_COMP_DIG,
					"DIG: DFS band: One-shot to 0x28 upmost\n");
			} else {
				current_igi = phydm_get_current_igi(
					dig_max_of_min, dm->rssi_min,
					current_igi);
			}

			ODM_RT_TRACE(
				dm, ODM_COMP_DIG,
				"DIG: First connect case: IGI does on-shot to 0x%x\n",
				current_igi);

		} else {
			if (fa_cnt->cnt_all > dm_FA_thres[2])
				current_igi = current_igi + 4;
			else if (fa_cnt->cnt_all > dm_FA_thres[1])
				current_igi = current_igi + 2;
			else if (fa_cnt->cnt_all < dm_FA_thres[0])
				current_igi = current_igi - 2;

			/* 4 Abnormal # beacon case */
			if ((dm->phy_dbg_info.num_qry_beacon_pkt < 5) &&
			    (fa_cnt->cnt_all < DM_DIG_FA_TH1) &&
			    (dm->bsta_state)) {
				current_igi = dig_tab->rx_gain_range_min;
				ODM_RT_TRACE(
					dm, ODM_COMP_DIG,
					"DIG: Abnormal #beacon (%d) case: IGI does one-shot to 0x%x\n",
					dm->phy_dbg_info.num_qry_beacon_pkt,
					current_igi);
			}
		}
	} else {
		/* 2 Before link */
		ODM_RT_TRACE(dm, ODM_COMP_DIG, "DIG: Adjust IGI before link\n");

		if (first_dis_connect || is_first_coverage) {
			current_igi = dm_dig_min;
			ODM_RT_TRACE(
				dm, ODM_COMP_DIG,
				"DIG: First disconnect case: IGI does on-shot to lower bound\n");
		} else {
			if (fa_cnt->cnt_all > dm_FA_thres[2])
				current_igi = current_igi + 4;
			else if (fa_cnt->cnt_all > dm_FA_thres[1])
				current_igi = current_igi + 2;
			else if (fa_cnt->cnt_all < dm_FA_thres[0])
				current_igi = current_igi - 2;
		}
	}

	/* 1 Check initial gain by upper/lower bound */
	if (current_igi < dig_tab->rx_gain_range_min)
		current_igi = dig_tab->rx_gain_range_min;

	if (current_igi > dig_tab->rx_gain_range_max)
		current_igi = dig_tab->rx_gain_range_max;

	ODM_RT_TRACE(dm, ODM_COMP_DIG, "DIG: cur_ig_value=0x%x, TotalFA = %d\n",
		     current_igi, fa_cnt->cnt_all);

	/* 1 Update status */
	if (dm->is_bt_hs_operation) {
		if (dm->is_linked) {
			if (dig_tab->bt30_cur_igi > (current_igi))
				odm_write_dig(dm, current_igi);
			else
				odm_write_dig(dm, dig_tab->bt30_cur_igi);

			dig_tab->is_media_connect_0 = dm->is_linked;
			dig_tab->dig_dynamic_min_0 = dig_dynamic_min;
		} else {
			if (dm->is_link_in_process)
				odm_write_dig(dm, 0x1c);
			else if (dm->is_bt_connect_process)
				odm_write_dig(dm, 0x28);
			else
				odm_write_dig(dm, dig_tab->bt30_cur_igi);
		}
	} else { /* BT is not using */
		odm_write_dig(dm, current_igi);
		dig_tab->is_media_connect_0 = dm->is_linked;
		dig_tab->dig_dynamic_min_0 = dig_dynamic_min;
	}
	ODM_RT_TRACE(dm, ODM_COMP_DIG, "DIG end\n");
}

void odm_dig_by_rssi_lps(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct false_alarm_stat *fa_cnt =
		(struct false_alarm_stat *)phydm_get_structure(
			dm, PHYDM_FALSEALMCNT);

	u8 rssi_lower = DM_DIG_MIN_NIC; /* 0x1E or 0x1C */
	u8 current_igi = dm->rssi_min;

	if (odm_dig_abort(dm))
		return;

	current_igi = current_igi + RSSI_OFFSET_DIG;

	ODM_RT_TRACE(dm, ODM_COMP_DIG, "%s()==>\n", __func__);

	/* Using FW PS mode to make IGI */
	/* Adjust by  FA in LPS MODE */
	if (fa_cnt->cnt_all > DM_DIG_FA_TH2_LPS)
		current_igi = current_igi + 4;
	else if (fa_cnt->cnt_all > DM_DIG_FA_TH1_LPS)
		current_igi = current_igi + 2;
	else if (fa_cnt->cnt_all < DM_DIG_FA_TH0_LPS)
		current_igi = current_igi - 2;

	/* Lower bound checking */

	/* RSSI Lower bound check */
	if ((dm->rssi_min - 10) > DM_DIG_MIN_NIC)
		rssi_lower = (dm->rssi_min - 10);
	else
		rssi_lower = DM_DIG_MIN_NIC;

	/* Upper and Lower Bound checking */
	if (current_igi > DM_DIG_MAX_NIC)
		current_igi = DM_DIG_MAX_NIC;
	else if (current_igi < rssi_lower)
		current_igi = rssi_lower;

	ODM_RT_TRACE(dm, ODM_COMP_DIG, "%s(): fa_cnt->cnt_all = %d\n", __func__,
		     fa_cnt->cnt_all);
	ODM_RT_TRACE(dm, ODM_COMP_DIG, "%s(): dm->rssi_min = %d\n", __func__,
		     dm->rssi_min);
	ODM_RT_TRACE(dm, ODM_COMP_DIG, "%s(): current_igi = 0x%x\n", __func__,
		     current_igi);

	odm_write_dig(
		dm,
		current_igi); /* odm_write_dig(dm, dig_tab->cur_ig_value); */
}

/* 3============================================================
 * 3 FASLE ALARM CHECK
 * 3============================================================
 */

void odm_false_alarm_counter_statistics(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct false_alarm_stat *false_alm_cnt =
		(struct false_alarm_stat *)phydm_get_structure(
			dm, PHYDM_FALSEALMCNT);
	struct rt_adcsmp *adc_smp = &dm->adcsmp;
	u32 ret_value;

	if (!(dm->support_ability & ODM_BB_FA_CNT))
		return;

	ODM_RT_TRACE(dm, ODM_COMP_FA_CNT, "%s()======>\n", __func__);

	if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		/* hold ofdm counter */
		odm_set_bb_reg(dm, ODM_REG_OFDM_FA_HOLDC_11N, BIT(31),
			       1); /* hold page C counter */
		odm_set_bb_reg(dm, ODM_REG_OFDM_FA_RSTD_11N, BIT(31),
			       1); /* hold page D counter */

		ret_value = odm_get_bb_reg(dm, ODM_REG_OFDM_FA_TYPE1_11N,
					   MASKDWORD);
		false_alm_cnt->cnt_fast_fsync = (ret_value & 0xffff);
		false_alm_cnt->cnt_sb_search_fail =
			((ret_value & 0xffff0000) >> 16);

		ret_value = odm_get_bb_reg(dm, ODM_REG_OFDM_FA_TYPE2_11N,
					   MASKDWORD);
		false_alm_cnt->cnt_ofdm_cca = (ret_value & 0xffff);
		false_alm_cnt->cnt_parity_fail =
			((ret_value & 0xffff0000) >> 16);

		ret_value = odm_get_bb_reg(dm, ODM_REG_OFDM_FA_TYPE3_11N,
					   MASKDWORD);
		false_alm_cnt->cnt_rate_illegal = (ret_value & 0xffff);
		false_alm_cnt->cnt_crc8_fail = ((ret_value & 0xffff0000) >> 16);

		ret_value = odm_get_bb_reg(dm, ODM_REG_OFDM_FA_TYPE4_11N,
					   MASKDWORD);
		false_alm_cnt->cnt_mcs_fail = (ret_value & 0xffff);

		false_alm_cnt->cnt_ofdm_fail =
			false_alm_cnt->cnt_parity_fail +
			false_alm_cnt->cnt_rate_illegal +
			false_alm_cnt->cnt_crc8_fail +
			false_alm_cnt->cnt_mcs_fail +
			false_alm_cnt->cnt_fast_fsync +
			false_alm_cnt->cnt_sb_search_fail;

		/* read CCK CRC32 counter */
		false_alm_cnt->cnt_cck_crc32_error = odm_get_bb_reg(
			dm, ODM_REG_CCK_CRC32_ERROR_CNT_11N, MASKDWORD);
		false_alm_cnt->cnt_cck_crc32_ok = odm_get_bb_reg(
			dm, ODM_REG_CCK_CRC32_OK_CNT_11N, MASKDWORD);

		/* read OFDM CRC32 counter */
		ret_value = odm_get_bb_reg(dm, ODM_REG_OFDM_CRC32_CNT_11N,
					   MASKDWORD);
		false_alm_cnt->cnt_ofdm_crc32_error =
			(ret_value & 0xffff0000) >> 16;
		false_alm_cnt->cnt_ofdm_crc32_ok = ret_value & 0xffff;

		/* read HT CRC32 counter */
		ret_value =
			odm_get_bb_reg(dm, ODM_REG_HT_CRC32_CNT_11N, MASKDWORD);
		false_alm_cnt->cnt_ht_crc32_error =
			(ret_value & 0xffff0000) >> 16;
		false_alm_cnt->cnt_ht_crc32_ok = ret_value & 0xffff;

		/* read VHT CRC32 counter */
		false_alm_cnt->cnt_vht_crc32_error = 0;
		false_alm_cnt->cnt_vht_crc32_ok = 0;

		{
			/* hold cck counter */
			odm_set_bb_reg(dm, ODM_REG_CCK_FA_RST_11N, BIT(12), 1);
			odm_set_bb_reg(dm, ODM_REG_CCK_FA_RST_11N, BIT(14), 1);

			ret_value = odm_get_bb_reg(dm, ODM_REG_CCK_FA_LSB_11N,
						   MASKBYTE0);
			false_alm_cnt->cnt_cck_fail = ret_value;

			ret_value = odm_get_bb_reg(dm, ODM_REG_CCK_FA_MSB_11N,
						   MASKBYTE3);
			false_alm_cnt->cnt_cck_fail += (ret_value & 0xff) << 8;

			ret_value = odm_get_bb_reg(dm, ODM_REG_CCK_CCA_CNT_11N,
						   MASKDWORD);
			false_alm_cnt->cnt_cck_cca =
				((ret_value & 0xFF) << 8) |
				((ret_value & 0xFF00) >> 8);
		}

		false_alm_cnt->cnt_all_pre = false_alm_cnt->cnt_all;

		false_alm_cnt->cnt_all = (false_alm_cnt->cnt_fast_fsync +
					  false_alm_cnt->cnt_sb_search_fail +
					  false_alm_cnt->cnt_parity_fail +
					  false_alm_cnt->cnt_rate_illegal +
					  false_alm_cnt->cnt_crc8_fail +
					  false_alm_cnt->cnt_mcs_fail +
					  false_alm_cnt->cnt_cck_fail);

		false_alm_cnt->cnt_cca_all = false_alm_cnt->cnt_ofdm_cca +
					     false_alm_cnt->cnt_cck_cca;

		if (dm->support_ic_type >= ODM_RTL8188E) {
			/*reset false alarm counter registers*/
			odm_set_bb_reg(dm, ODM_REG_OFDM_FA_RSTC_11N, BIT(31),
				       1);
			odm_set_bb_reg(dm, ODM_REG_OFDM_FA_RSTC_11N, BIT(31),
				       0);
			odm_set_bb_reg(dm, ODM_REG_OFDM_FA_RSTD_11N, BIT(27),
				       1);
			odm_set_bb_reg(dm, ODM_REG_OFDM_FA_RSTD_11N, BIT(27),
				       0);

			/*update ofdm counter*/
			odm_set_bb_reg(dm, ODM_REG_OFDM_FA_HOLDC_11N, BIT(31),
				       0); /*update page C counter*/
			odm_set_bb_reg(dm, ODM_REG_OFDM_FA_RSTD_11N, BIT(31),
				       0); /*update page D counter*/

			/*reset CCK CCA counter*/
			odm_set_bb_reg(dm, ODM_REG_CCK_FA_RST_11N,
				       BIT(13) | BIT(12), 0);
			odm_set_bb_reg(dm, ODM_REG_CCK_FA_RST_11N,
				       BIT(13) | BIT(12), 2);

			/*reset CCK FA counter*/
			odm_set_bb_reg(dm, ODM_REG_CCK_FA_RST_11N,
				       BIT(15) | BIT(14), 0);
			odm_set_bb_reg(dm, ODM_REG_CCK_FA_RST_11N,
				       BIT(15) | BIT(14), 2);

			/*reset CRC32 counter*/
			odm_set_bb_reg(dm, ODM_REG_PAGE_F_RST_11N, BIT(16), 1);
			odm_set_bb_reg(dm, ODM_REG_PAGE_F_RST_11N, BIT(16), 0);
		}

		/* Get debug port 0 */
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11N, MASKDWORD, 0x0);
		false_alm_cnt->dbg_port0 =
			odm_get_bb_reg(dm, ODM_REG_RPT_11N, MASKDWORD);

		/* Get EDCCA flag */
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11N, MASKDWORD, 0x208);
		false_alm_cnt->edcca_flag =
			(bool)odm_get_bb_reg(dm, ODM_REG_RPT_11N, BIT(30));

		ODM_RT_TRACE(
			dm, ODM_COMP_FA_CNT,
			"[OFDM FA Detail] Parity_Fail = (( %d )), Rate_Illegal = (( %d )), CRC8_fail = (( %d )), Mcs_fail = (( %d )), Fast_Fsync = (( %d )), SB_Search_fail = (( %d ))\n",
			false_alm_cnt->cnt_parity_fail,
			false_alm_cnt->cnt_rate_illegal,
			false_alm_cnt->cnt_crc8_fail,
			false_alm_cnt->cnt_mcs_fail,
			false_alm_cnt->cnt_fast_fsync,
			false_alm_cnt->cnt_sb_search_fail);
	}

	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		u32 cck_enable;

		/* read OFDM FA counter */
		false_alm_cnt->cnt_ofdm_fail =
			odm_get_bb_reg(dm, ODM_REG_OFDM_FA_11AC, MASKLWORD);

		/* Read CCK FA counter */
		false_alm_cnt->cnt_cck_fail =
			odm_get_bb_reg(dm, ODM_REG_CCK_FA_11AC, MASKLWORD);

		/* read CCK/OFDM CCA counter */
		ret_value =
			odm_get_bb_reg(dm, ODM_REG_CCK_CCA_CNT_11AC, MASKDWORD);
		false_alm_cnt->cnt_ofdm_cca = (ret_value & 0xffff0000) >> 16;
		false_alm_cnt->cnt_cck_cca = ret_value & 0xffff;

		/* read CCK CRC32 counter */
		ret_value = odm_get_bb_reg(dm, ODM_REG_CCK_CRC32_CNT_11AC,
					   MASKDWORD);
		false_alm_cnt->cnt_cck_crc32_error =
			(ret_value & 0xffff0000) >> 16;
		false_alm_cnt->cnt_cck_crc32_ok = ret_value & 0xffff;

		/* read OFDM CRC32 counter */
		ret_value = odm_get_bb_reg(dm, ODM_REG_OFDM_CRC32_CNT_11AC,
					   MASKDWORD);
		false_alm_cnt->cnt_ofdm_crc32_error =
			(ret_value & 0xffff0000) >> 16;
		false_alm_cnt->cnt_ofdm_crc32_ok = ret_value & 0xffff;

		/* read HT CRC32 counter */
		ret_value = odm_get_bb_reg(dm, ODM_REG_HT_CRC32_CNT_11AC,
					   MASKDWORD);
		false_alm_cnt->cnt_ht_crc32_error =
			(ret_value & 0xffff0000) >> 16;
		false_alm_cnt->cnt_ht_crc32_ok = ret_value & 0xffff;

		/* read VHT CRC32 counter */
		ret_value = odm_get_bb_reg(dm, ODM_REG_VHT_CRC32_CNT_11AC,
					   MASKDWORD);
		false_alm_cnt->cnt_vht_crc32_error =
			(ret_value & 0xffff0000) >> 16;
		false_alm_cnt->cnt_vht_crc32_ok = ret_value & 0xffff;

		/* reset OFDM FA counter */
		odm_set_bb_reg(dm, ODM_REG_OFDM_FA_RST_11AC, BIT(17), 1);
		odm_set_bb_reg(dm, ODM_REG_OFDM_FA_RST_11AC, BIT(17), 0);

		/* reset CCK FA counter */
		odm_set_bb_reg(dm, ODM_REG_CCK_FA_RST_11AC, BIT(15), 0);
		odm_set_bb_reg(dm, ODM_REG_CCK_FA_RST_11AC, BIT(15), 1);

		/* reset CCA counter */
		odm_set_bb_reg(dm, ODM_REG_RST_RPT_11AC, BIT(0), 1);
		odm_set_bb_reg(dm, ODM_REG_RST_RPT_11AC, BIT(0), 0);

		cck_enable =
			odm_get_bb_reg(dm, ODM_REG_BB_RX_PATH_11AC, BIT(28));
		if (cck_enable) { /* if(*dm->band_type == ODM_BAND_2_4G) */
			false_alm_cnt->cnt_all = false_alm_cnt->cnt_ofdm_fail +
						 false_alm_cnt->cnt_cck_fail;
			false_alm_cnt->cnt_cca_all =
				false_alm_cnt->cnt_cck_cca +
				false_alm_cnt->cnt_ofdm_cca;
		} else {
			false_alm_cnt->cnt_all = false_alm_cnt->cnt_ofdm_fail;
			false_alm_cnt->cnt_cca_all =
				false_alm_cnt->cnt_ofdm_cca;
		}

		if (adc_smp->adc_smp_state == ADCSMP_STATE_IDLE) {
			if (phydm_set_bb_dbg_port(
				    dm, BB_DBGPORT_PRIORITY_1,
				    0x0)) { /*set debug port to 0x0*/
				false_alm_cnt->dbg_port0 =
					phydm_get_bb_dbg_port_value(dm);
				phydm_release_bb_dbg_port(dm);
			}

			if (phydm_set_bb_dbg_port(
				    dm, BB_DBGPORT_PRIORITY_1,
				    0x209)) { /*set debug port to 0x0*/
				false_alm_cnt->edcca_flag =
					(bool)((phydm_get_bb_dbg_port_value(
							dm) &
						BIT(30)) >>
					       30);
				phydm_release_bb_dbg_port(dm);
			}
		}
	}

	false_alm_cnt->cnt_crc32_error_all =
		false_alm_cnt->cnt_vht_crc32_error +
		false_alm_cnt->cnt_ht_crc32_error +
		false_alm_cnt->cnt_ofdm_crc32_error +
		false_alm_cnt->cnt_cck_crc32_error;
	false_alm_cnt->cnt_crc32_ok_all = false_alm_cnt->cnt_vht_crc32_ok +
					  false_alm_cnt->cnt_ht_crc32_ok +
					  false_alm_cnt->cnt_ofdm_crc32_ok +
					  false_alm_cnt->cnt_cck_crc32_ok;

	ODM_RT_TRACE(dm, ODM_COMP_FA_CNT,
		     "[CCA Cnt] {CCK, OFDM, Total} = {%d, %d, %d}\n",
		     false_alm_cnt->cnt_cck_cca, false_alm_cnt->cnt_ofdm_cca,
		     false_alm_cnt->cnt_cca_all);

	ODM_RT_TRACE(dm, ODM_COMP_FA_CNT,
		     "[FA Cnt] {CCK, OFDM, Total} = {%d, %d, %d}\n",
		     false_alm_cnt->cnt_cck_fail, false_alm_cnt->cnt_ofdm_fail,
		     false_alm_cnt->cnt_all);

	ODM_RT_TRACE(dm, ODM_COMP_FA_CNT,
		     "[CCK]  CRC32 {error, ok}= {%d, %d}\n",
		     false_alm_cnt->cnt_cck_crc32_error,
		     false_alm_cnt->cnt_cck_crc32_ok);
	ODM_RT_TRACE(dm, ODM_COMP_FA_CNT, "[OFDM]CRC32 {error, ok}= {%d, %d}\n",
		     false_alm_cnt->cnt_ofdm_crc32_error,
		     false_alm_cnt->cnt_ofdm_crc32_ok);
	ODM_RT_TRACE(dm, ODM_COMP_FA_CNT,
		     "[ HT ]  CRC32 {error, ok}= {%d, %d}\n",
		     false_alm_cnt->cnt_ht_crc32_error,
		     false_alm_cnt->cnt_ht_crc32_ok);
	ODM_RT_TRACE(dm, ODM_COMP_FA_CNT,
		     "[VHT]  CRC32 {error, ok}= {%d, %d}\n",
		     false_alm_cnt->cnt_vht_crc32_error,
		     false_alm_cnt->cnt_vht_crc32_ok);
	ODM_RT_TRACE(dm, ODM_COMP_FA_CNT,
		     "[VHT]  CRC32 {error, ok}= {%d, %d}\n",
		     false_alm_cnt->cnt_crc32_error_all,
		     false_alm_cnt->cnt_crc32_ok_all);
	ODM_RT_TRACE(dm, ODM_COMP_FA_CNT,
		     "FA_Cnt: Dbg port 0x0 = 0x%x, EDCCA = %d\n\n",
		     false_alm_cnt->dbg_port0, false_alm_cnt->edcca_flag);
}

/* 3============================================================
 * 3 CCK Packet Detect threshold
 * 3============================================================
 */

void odm_pause_cck_packet_detection(void *dm_void,
				    enum phydm_pause_type pause_type,
				    enum phydm_pause_level pause_level,
				    u8 cck_pd_threshold)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dig_thres *dig_tab = &dm->dm_dig_table;
	s8 max_level;

	ODM_RT_TRACE(dm, ODM_COMP_DIG, "%s()=========> level = %d\n", __func__,
		     pause_level);

	if ((dig_tab->pause_cckpd_level == 0) &&
	    (!(dm->support_ability & ODM_BB_CCK_PD) ||
	     !(dm->support_ability & ODM_BB_FA_CNT))) {
		ODM_RT_TRACE(
			dm, ODM_COMP_DIG,
			"Return: support_ability ODM_BB_CCK_PD or ODM_BB_FA_CNT is disabled\n");
		return;
	}

	if (pause_level > DM_DIG_MAX_PAUSE_TYPE) {
		ODM_RT_TRACE(dm, ODM_COMP_DIG,
			     "%s(): Return: Wrong pause level !!\n", __func__);
		return;
	}

	ODM_RT_TRACE(dm, ODM_COMP_DIG,
		     "%s(): pause level = 0x%x, Current value = 0x%x\n",
		     __func__, dig_tab->pause_cckpd_level, cck_pd_threshold);
	ODM_RT_TRACE(
		dm, ODM_COMP_DIG,
		"%s(): pause value = 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		__func__, dig_tab->pause_cckpd_value[7],
		dig_tab->pause_cckpd_value[6], dig_tab->pause_cckpd_value[5],
		dig_tab->pause_cckpd_value[4], dig_tab->pause_cckpd_value[3],
		dig_tab->pause_cckpd_value[2], dig_tab->pause_cckpd_value[1],
		dig_tab->pause_cckpd_value[0]);

	switch (pause_type) {
	/* Pause CCK Packet Detection threshold */
	case PHYDM_PAUSE: {
		/* Disable CCK PD */
		odm_cmn_info_update(dm, ODM_CMNINFO_ABILITY,
				    dm->support_ability & (~ODM_BB_CCK_PD));
		ODM_RT_TRACE(dm, ODM_COMP_DIG,
			     "%s(): Pause CCK packet detection threshold !!\n",
			     __func__);

		/*Backup original CCK PD threshold decided by CCK PD mechanism*/
		if (dig_tab->pause_cckpd_level == 0) {
			dig_tab->cck_pd_backup = dig_tab->cur_cck_cca_thres;
			ODM_RT_TRACE(
				dm, ODM_COMP_DIG,
				"%s(): Backup CCKPD  = 0x%x, new CCKPD = 0x%x\n",
				__func__, dig_tab->cck_pd_backup,
				cck_pd_threshold);
		}

		/* Update pause level */
		dig_tab->pause_cckpd_level =
			(dig_tab->pause_cckpd_level | BIT(pause_level));

		/* Record CCK PD threshold */
		dig_tab->pause_cckpd_value[pause_level] = cck_pd_threshold;

		/* Write new CCK PD threshold */
		if (BIT(pause_level + 1) > dig_tab->pause_cckpd_level) {
			odm_write_cck_cca_thres(dm, cck_pd_threshold);
			ODM_RT_TRACE(dm, ODM_COMP_DIG,
				     "%s(): CCKPD of higher level = 0x%x\n",
				     __func__, cck_pd_threshold);
		}
		break;
	}
	/* Resume CCK Packet Detection threshold */
	case PHYDM_RESUME: {
		/* check if the level is illegal or not */
		if ((dig_tab->pause_cckpd_level & (BIT(pause_level))) != 0) {
			dig_tab->pause_cckpd_level =
				dig_tab->pause_cckpd_level &
				(~(BIT(pause_level)));
			dig_tab->pause_cckpd_value[pause_level] = 0;
			ODM_RT_TRACE(dm, ODM_COMP_DIG,
				     "%s(): Resume CCK PD !!\n", __func__);
		} else {
			ODM_RT_TRACE(dm, ODM_COMP_DIG,
				     "%s(): Wrong resume level !!\n", __func__);
			break;
		}

		/* Resume DIG */
		if (dig_tab->pause_cckpd_level == 0) {
			/* Write backup IGI value */
			odm_write_cck_cca_thres(dm, dig_tab->cck_pd_backup);
			/* dig_tab->is_ignore_dig = true; */
			ODM_RT_TRACE(dm, ODM_COMP_DIG,
				     "%s(): Write original CCKPD = 0x%x\n",
				     __func__, dig_tab->cck_pd_backup);

			/* Enable DIG */
			odm_cmn_info_update(dm, ODM_CMNINFO_ABILITY,
					    dm->support_ability |
						    ODM_BB_CCK_PD);
			break;
		}

		if (BIT(pause_level) <= dig_tab->pause_cckpd_level)
			break;

		/* Calculate the maximum level now */
		for (max_level = (pause_level - 1); max_level >= 0;
		     max_level--) {
			if ((dig_tab->pause_cckpd_level & BIT(max_level)) > 0)
				break;
		}

		/* write CCKPD of lower level */
		odm_write_cck_cca_thres(dm,
					dig_tab->pause_cckpd_value[max_level]);
		ODM_RT_TRACE(dm, ODM_COMP_DIG,
			     "%s(): Write CCKPD (0x%x) of level (%d)\n",
			     __func__, dig_tab->pause_cckpd_value[max_level],
			     max_level);
		break;
	}
	default:
		ODM_RT_TRACE(dm, ODM_COMP_DIG, "%s(): Wrong  type !!\n",
			     __func__);
		break;
	}

	ODM_RT_TRACE(dm, ODM_COMP_DIG,
		     "%s(): pause level = 0x%x, Current value = 0x%x\n",
		     __func__, dig_tab->pause_cckpd_level, cck_pd_threshold);
	ODM_RT_TRACE(
		dm, ODM_COMP_DIG,
		"%s(): pause value = 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		__func__, dig_tab->pause_cckpd_value[7],
		dig_tab->pause_cckpd_value[6], dig_tab->pause_cckpd_value[5],
		dig_tab->pause_cckpd_value[4], dig_tab->pause_cckpd_value[3],
		dig_tab->pause_cckpd_value[2], dig_tab->pause_cckpd_value[1],
		dig_tab->pause_cckpd_value[0]);
}

void odm_cck_packet_detection_thresh(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dig_thres *dig_tab = &dm->dm_dig_table;
	struct false_alarm_stat *false_alm_cnt =
		(struct false_alarm_stat *)phydm_get_structure(
			dm, PHYDM_FALSEALMCNT);
	u8 cur_cck_cca_thres = dig_tab->cur_cck_cca_thres, rssi_thd = 35;

	if ((!(dm->support_ability & ODM_BB_CCK_PD)) ||
	    (!(dm->support_ability & ODM_BB_FA_CNT))) {
		ODM_RT_TRACE(dm, ODM_COMP_DIG, "CCK_PD: return==========\n");
		return;
	}

	if (dm->ext_lna)
		return;

	ODM_RT_TRACE(dm, ODM_COMP_DIG, "CCK_PD: ==========>\n");

	if (dig_tab->cck_fa_ma == 0xffffffff)
		dig_tab->cck_fa_ma = false_alm_cnt->cnt_cck_fail;
	else
		dig_tab->cck_fa_ma =
			((dig_tab->cck_fa_ma << 1) + dig_tab->cck_fa_ma +
			 false_alm_cnt->cnt_cck_fail) >>
			2;

	ODM_RT_TRACE(dm, ODM_COMP_DIG, "CCK_PD: CCK FA moving average = %d\n",
		     dig_tab->cck_fa_ma);

	if (dm->is_linked) {
		if (dm->rssi_min > rssi_thd) {
			cur_cck_cca_thres = 0xcd;
		} else if (dm->rssi_min > 20) {
			if (dig_tab->cck_fa_ma >
			    ((DM_DIG_FA_TH1 >> 1) + (DM_DIG_FA_TH1 >> 3)))
				cur_cck_cca_thres = 0xcd;
			else if (dig_tab->cck_fa_ma < (DM_DIG_FA_TH0 >> 1))
				cur_cck_cca_thres = 0x83;
		} else if (dm->rssi_min > 7) {
			cur_cck_cca_thres = 0x83;
		} else {
			cur_cck_cca_thres = 0x40;
		}

	} else {
		if (dig_tab->cck_fa_ma > 0x400)
			cur_cck_cca_thres = 0x83;
		else if (dig_tab->cck_fa_ma < 0x200)
			cur_cck_cca_thres = 0x40;
	}

	{
		odm_write_cck_cca_thres(dm, cur_cck_cca_thres);
	}

	ODM_RT_TRACE(dm, ODM_COMP_DIG, "CCK_PD: cck_cca_th=((0x%x))\n\n",
		     cur_cck_cca_thres);
}

void odm_write_cck_cca_thres(void *dm_void, u8 cur_cck_cca_thres)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dig_thres *dig_tab = &dm->dm_dig_table;

	if (dig_tab->cur_cck_cca_thres !=
	    cur_cck_cca_thres) { /* modify by Guo.Mingzhi 2012-01-03 */
		odm_write_1byte(dm, ODM_REG(CCK_CCA, dm), cur_cck_cca_thres);
		dig_tab->cck_fa_ma = 0xffffffff;
	}
	dig_tab->pre_cck_cca_thres = dig_tab->cur_cck_cca_thres;
	dig_tab->cur_cck_cca_thres = cur_cck_cca_thres;
}

bool phydm_dig_go_up_check(void *dm_void)
{
	bool ret = true;

	return ret;
}
