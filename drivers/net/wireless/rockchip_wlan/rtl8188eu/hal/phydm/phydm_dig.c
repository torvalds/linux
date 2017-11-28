/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

/* ************************************************************
 * include files
 * ************************************************************ */
#include "mp_precomp.h"
#include "phydm_precomp.h"


void
odm_change_dynamic_init_gain_thresh(
	void		*p_dm_void,
	u32		dm_type,
	u32		dm_value
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _dynamic_initial_gain_threshold_			*p_dm_dig_table = &p_dm_odm->dm_dig_table;

	if (dm_type == DIG_TYPE_THRESH_HIGH)
		p_dm_dig_table->rssi_high_thresh = dm_value;
	else if (dm_type == DIG_TYPE_THRESH_LOW)
		p_dm_dig_table->rssi_low_thresh = dm_value;
	else if (dm_type == DIG_TYPE_ENABLE)
		p_dm_dig_table->dig_enable_flag	= true;
	else if (dm_type == DIG_TYPE_DISABLE)
		p_dm_dig_table->dig_enable_flag = false;
	else if (dm_type == DIG_TYPE_BACKOFF) {
		if (dm_value > 30)
			dm_value = 30;
		p_dm_dig_table->backoff_val = (u8)dm_value;
	} else if (dm_type == DIG_TYPE_RX_GAIN_MIN) {
		if (dm_value == 0)
			dm_value = 0x1;
		p_dm_dig_table->rx_gain_range_min = (u8)dm_value;
	} else if (dm_type == DIG_TYPE_RX_GAIN_MAX) {
		if (dm_value > 0x50)
			dm_value = 0x50;
		p_dm_dig_table->rx_gain_range_max = (u8)dm_value;
	}
}	/* dm_change_dynamic_init_gain_thresh */

int
get_igi_for_diff(int value_IGI)
{
#define ONERCCA_LOW_TH		0x30
#define ONERCCA_LOW_DIFF		8

	if (value_IGI < ONERCCA_LOW_TH) {
		if ((ONERCCA_LOW_TH - value_IGI) < ONERCCA_LOW_DIFF)
			return ONERCCA_LOW_TH;
		else
			return value_IGI + ONERCCA_LOW_DIFF;
	} else
		return value_IGI;
}

void
odm_fa_threshold_check(
	void			*p_dm_void,
	bool			is_dfs_band,
	bool			is_performance,
	u32			rx_tp,
	u32			tx_tp,
	u32			*dm_FA_thres
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (p_dm_odm->is_linked && (is_performance || is_dfs_band)) {
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
		/*For AP*/
		if ((rx_tp >> 2) > tx_tp && rx_tp < 10000 && rx_tp > 500) {			/*10Mbps & 0.5Mbps*/
			dm_FA_thres[0] = 0x080;
			dm_FA_thres[1] = 0x100;
			dm_FA_thres[2] = 0x200;
		} else {
			dm_FA_thres[0] = 0x100;
			dm_FA_thres[1] = 0x200;
			dm_FA_thres[2] = 0x300;
		}
#else
		/*For NIC*/
		dm_FA_thres[0] = DM_DIG_FA_TH0;
		dm_FA_thres[1] = DM_DIG_FA_TH1;
		dm_FA_thres[2] = DM_DIG_FA_TH2;
#endif
	} else {
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP | ODM_CE))
		if (is_dfs_band) {
			/* For DFS band and no link */
			dm_FA_thres[0] = 250;
			dm_FA_thres[1] = 1000;
			dm_FA_thres[2] = 2000;
		} else
#endif
		{
			dm_FA_thres[0] = 2000;
			dm_FA_thres[1] = 4000;
			dm_FA_thres[2] = 5000;
		}
	}
	return;
}

u8
odm_forbidden_igi_check(
	void			*p_dm_void,
	u8			dig_dynamic_min,
	u8			current_igi
)
{
	struct PHY_DM_STRUCT					*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _dynamic_initial_gain_threshold_						*p_dm_dig_table = &p_dm_odm->dm_dig_table;
	struct _FALSE_ALARM_STATISTICS	*p_false_alm_cnt = (struct _FALSE_ALARM_STATISTICS *)phydm_get_structure(p_dm_odm, PHYDM_FALSEALMCNT);
	u8						rx_gain_range_min = p_dm_dig_table->rx_gain_range_min;

	if (p_dm_dig_table->large_fa_timeout) {
		if (--p_dm_dig_table->large_fa_timeout == 0)
			p_dm_dig_table->large_fa_hit = 0;
	}

	if (p_false_alm_cnt->cnt_all > 10000) {

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Abnormally false alarm case.\n"));

		if (p_dm_dig_table->large_fa_hit != 3)
			p_dm_dig_table->large_fa_hit++;

		if (p_dm_dig_table->forbidden_igi < current_igi) { /* if(p_dm_dig_table->forbidden_igi < p_dm_dig_table->cur_ig_value) */
			p_dm_dig_table->forbidden_igi = current_igi;/* p_dm_dig_table->forbidden_igi = p_dm_dig_table->cur_ig_value; */
			p_dm_dig_table->large_fa_hit = 1;
			p_dm_dig_table->large_fa_timeout = LARGE_FA_TIMEOUT;
		}

		if (p_dm_dig_table->large_fa_hit >= 3) {
			if ((p_dm_dig_table->forbidden_igi + 2) > p_dm_dig_table->rx_gain_range_max)
				rx_gain_range_min = p_dm_dig_table->rx_gain_range_max;
			else
				rx_gain_range_min = (p_dm_dig_table->forbidden_igi + 2);
			p_dm_dig_table->recover_cnt = 1800;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Abnormally false alarm case: recover_cnt = %d\n", p_dm_dig_table->recover_cnt));
		}
	}

	else if (p_false_alm_cnt->cnt_all > 2000) {

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("Abnormally false alarm case.\n"));
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("cnt_all=%d, cnt_all_pre=%d, current_igi=0x%x, pre_ig_value=0x%x\n",
			p_false_alm_cnt->cnt_all, p_false_alm_cnt->cnt_all_pre, current_igi, p_dm_dig_table->pre_ig_value));

		/* p_false_alm_cnt->cnt_all = 1.1875*p_false_alm_cnt->cnt_all_pre */
		if ((p_false_alm_cnt->cnt_all > (p_false_alm_cnt->cnt_all_pre + (p_false_alm_cnt->cnt_all_pre >> 3) + (p_false_alm_cnt->cnt_all_pre >> 4))) && (current_igi < p_dm_dig_table->pre_ig_value)) {
			if (p_dm_dig_table->large_fa_hit != 3)
				p_dm_dig_table->large_fa_hit++;

			if (p_dm_dig_table->forbidden_igi < current_igi)	{	/*if(p_dm_dig_table->forbidden_igi < p_dm_dig_table->cur_ig_value)*/

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("Updating forbidden_igi by current_igi, forbidden_igi=0x%x, current_igi=0x%x\n",
					p_dm_dig_table->forbidden_igi, current_igi));

				p_dm_dig_table->forbidden_igi = current_igi;	/*p_dm_dig_table->forbidden_igi = p_dm_dig_table->cur_ig_value;*/
				p_dm_dig_table->large_fa_hit = 1;
				p_dm_dig_table->large_fa_timeout = LARGE_FA_TIMEOUT;
			}

		}

		if (p_dm_dig_table->large_fa_hit >= 3) {

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("FaHit is greater than 3, rx_gain_range_max=0x%x, rx_gain_range_min=0x%x, forbidden_igi=0x%x\n",
				p_dm_dig_table->rx_gain_range_max, rx_gain_range_min, p_dm_dig_table->forbidden_igi));

			if ((p_dm_dig_table->forbidden_igi + 1) > p_dm_dig_table->rx_gain_range_max)
				rx_gain_range_min = p_dm_dig_table->rx_gain_range_max;
			else
				rx_gain_range_min = (p_dm_dig_table->forbidden_igi + 1);

			p_dm_dig_table->recover_cnt = 1200;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("Abnormally false alarm case: recover_cnt = %d,  rx_gain_range_min = 0x%x\n", p_dm_dig_table->recover_cnt, rx_gain_range_min));
		}
	}

	else {
		if (p_dm_dig_table->recover_cnt != 0) {

			p_dm_dig_table->recover_cnt--;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Normal Case: recover_cnt = %d\n", p_dm_dig_table->recover_cnt));
		} else {
			if (p_dm_dig_table->large_fa_hit < 3) {
				if ((p_dm_dig_table->forbidden_igi - 2) < dig_dynamic_min) { /* DM_DIG_MIN) */
					p_dm_dig_table->forbidden_igi = dig_dynamic_min; /* DM_DIG_MIN; */
					rx_gain_range_min = dig_dynamic_min; /* DM_DIG_MIN; */
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Normal Case: At Lower Bound\n"));
				} else {
					if (p_dm_dig_table->large_fa_hit == 0) {
						p_dm_dig_table->forbidden_igi -= 2;
						rx_gain_range_min = (p_dm_dig_table->forbidden_igi + 2);
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Normal Case: Approach Lower Bound\n"));
					}
				}
			} else
				p_dm_dig_table->large_fa_hit = 0;
		}
	}

	return rx_gain_range_min;

}

void
odm_inband_noise_calculate(
	void		*p_dm_void
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct PHY_DM_STRUCT			*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _dynamic_initial_gain_threshold_				*p_dm_dig_table = &p_dm_odm->dm_dig_table;
	u8				igi_backup, time_cnt = 0, valid_cnt = 0;
	bool				is_timeout = true;
	s8				s_noise_a, s_noise_b;
	s32				noise_rpt_a = 0, noise_rpt_b = 0;
	u32				tmp = 0;
	static	u8		fail_cnt = 0;

	if (!(p_dm_odm->support_ic_type & (ODM_RTL8192E)))
		return;

	if (p_dm_odm->rf_type == ODM_1T1R || *(p_dm_odm->p_one_path_cca) != ODM_CCA_2R)
		return;

	if (!p_dm_dig_table->is_noise_est)
		return;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_InbandNoiseEstimate()========>\n"));

	/* 1 Set initial gain. */
	igi_backup = p_dm_dig_table->cur_ig_value;
	p_dm_dig_table->igi_offset_a = 0;
	p_dm_dig_table->igi_offset_b = 0;
	odm_write_dig(p_dm_odm, 0x24);

	/* 1 Update idle time power report */
	if (p_dm_odm->support_ic_type & ODM_IC_11N_SERIES)
		odm_set_bb_reg(p_dm_odm, ODM_REG_TX_ANT_CTRL_11N, BIT(25), 0x0);

	delay_ms(2);

	/* 1 Get noise power level */
	while (1) {
		/* 2 Read Noise Floor Report */
		if (p_dm_odm->support_ic_type & ODM_IC_11N_SERIES)
			tmp = odm_get_bb_reg(p_dm_odm, 0x8f8, MASKLWORD);

		s_noise_a = (s8)(tmp & 0xff);
		s_noise_b = (s8)((tmp & 0xff00) >> 8);

		/* ODM_RT_TRACE(p_dm_odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("s_noise_a = %d, s_noise_b = %d\n",s_noise_a, s_noise_b)); */

		if ((s_noise_a < 20 && s_noise_a >= -70) && (s_noise_b < 20 && s_noise_b >= -70)) {
			valid_cnt++;
			noise_rpt_a += s_noise_a;
			noise_rpt_b += s_noise_b;
			/* ODM_RT_TRACE(p_dm_odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("s_noise_a = %d, s_noise_b = %d\n",s_noise_a, s_noise_b)); */
		}

		time_cnt++;
		is_timeout = (time_cnt >= 150) ? true : false;

		if (valid_cnt == 20 || is_timeout)
			break;

		delay_ms(2);

	}

	/* 1 Keep idle time power report */
	if (p_dm_odm->support_ic_type & ODM_IC_11N_SERIES)
		odm_set_bb_reg(p_dm_odm, ODM_REG_TX_ANT_CTRL_11N, BIT(25), 0x1);

	/* 1 Recover IGI */
	odm_write_dig(p_dm_odm, igi_backup);

	/* 1 Calculate Noise Floor */
	if (valid_cnt != 0) {
		noise_rpt_a  /= (valid_cnt << 1);
		noise_rpt_b  /= (valid_cnt << 1);
	}

	if (is_timeout) {
		noise_rpt_a = 0;
		noise_rpt_b = 0;

		fail_cnt++;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("Noise estimate fail time = %d\n", fail_cnt));

		if (fail_cnt == 3) {
			fail_cnt = 0;
			p_dm_dig_table->is_noise_est = false;
		}
	} else {
		noise_rpt_a = -110 + 0x24 + noise_rpt_a - 6;
		noise_rpt_b = -110 + 0x24 + noise_rpt_b - 6;
		p_dm_dig_table->is_noise_est = false;
		fail_cnt = 0;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("noise_rpt_a = %d, noise_rpt_b = %d\n", noise_rpt_a, noise_rpt_b));
	}

	/* 1 Calculate IGI Offset */
	if (noise_rpt_a > noise_rpt_b) {
		p_dm_dig_table->igi_offset_a = noise_rpt_a - noise_rpt_b;
		p_dm_dig_table->igi_offset_b = 0;
	} else {
		p_dm_dig_table->igi_offset_a = 0;
		p_dm_dig_table->igi_offset_b = noise_rpt_b - noise_rpt_a;
	}

#endif
	return;
}

void
odm_dig_for_bt_hs_mode(
	void		*p_dm_void
)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT				*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _dynamic_initial_gain_threshold_					*p_dm_dig_table = &p_dm_odm->dm_dig_table;
	u8					dig_for_bt_hs = 0;
	u8					dig_up_bound = 0x5a;

	if (p_dm_odm->is_bt_connect_process)
		dig_for_bt_hs = 0x22;
	else {
		/*  */
		/* Decide DIG value by BT HS RSSI. */
		/*  */
		dig_for_bt_hs = p_dm_odm->bt_hs_rssi + 4;

		/* DIG Bound */
		if (dig_for_bt_hs > dig_up_bound)
			dig_for_bt_hs = dig_up_bound;
		if (dig_for_bt_hs < 0x1c)
			dig_for_bt_hs = 0x1c;

		/* update Current IGI */
		p_dm_dig_table->bt30_cur_igi = dig_for_bt_hs;
	}
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_dig_for_bt_hs_mode() : set DigValue=0x%x\n", dig_for_bt_hs));
#endif
}

void
phydm_set_big_jump_step(
	void			*p_dm_void,
	u8			current_igi
)
{
#if (RTL8822B_SUPPORT == 1 || RTL8197F_SUPPORT == 1)
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _dynamic_initial_gain_threshold_			*p_dm_dig_table = &p_dm_odm->dm_dig_table;
	u8			step1[8] = {24, 30, 40, 50, 60, 70, 80, 90};
	u8			i;

	if (p_dm_dig_table->enable_adjust_big_jump == 0)
		return;

	for (i = 0; i <= p_dm_dig_table->big_jump_step1; i++) {
		if ((current_igi + step1[i]) > p_dm_dig_table->big_jump_lmt[p_dm_dig_table->agc_table_idx]) {
			if (i != 0)
				i = i - 1;
			break;
		} else if (i == p_dm_dig_table->big_jump_step1)
			break;
	}
	if (p_dm_odm->support_ic_type & ODM_RTL8822B)
		odm_set_bb_reg(p_dm_odm, 0x8c8, 0xe, i);
	else if (p_dm_odm->support_ic_type & ODM_RTL8197F)
		odm_set_bb_reg(p_dm_odm, ODM_REG_BB_AGC_SET_2_11N, 0xe, i);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("phydm_set_big_jump_step(): bigjump = %d (ori = 0x%x), LMT=0x%x\n", i, p_dm_dig_table->big_jump_step1, p_dm_dig_table->big_jump_lmt[p_dm_dig_table->agc_table_idx]));
#endif
}

void
odm_write_dig(
	void			*p_dm_void,
	u8			current_igi
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _dynamic_initial_gain_threshold_			*p_dm_dig_table = &p_dm_odm->dm_dig_table;

	if (p_dm_dig_table->is_stop_dig) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_write_dig(): Stop Writing IGI\n"));
		return;
	}

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_TRACE, ("odm_write_dig(): ODM_REG(IGI_A,p_dm_odm)=0x%x, ODM_BIT(IGI,p_dm_odm)=0x%x\n",
			ODM_REG(IGI_A, p_dm_odm), ODM_BIT(IGI, p_dm_odm)));

	/* 1 Check initial gain by upper bound */
	if ((!p_dm_dig_table->is_psd_in_progress) && p_dm_odm->is_linked) {
		if (current_igi > p_dm_dig_table->rx_gain_range_max) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_TRACE, ("odm_write_dig(): current_igi(0x%02x) is larger than upper bound !!\n", current_igi));
			current_igi = p_dm_dig_table->rx_gain_range_max;
		}
		if (p_dm_odm->support_ability & ODM_BB_ADAPTIVITY && p_dm_odm->adaptivity_flag == true) {
			if (current_igi > p_dm_odm->adaptivity_igi_upper)
				current_igi = p_dm_odm->adaptivity_igi_upper;

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_write_dig(): adaptivity case: Force upper bound to 0x%x !!!!!!\n", current_igi));
		}
	}

	if (p_dm_dig_table->cur_ig_value != current_igi) {

#if (RTL8822B_SUPPORT == 1 || RTL8197F_SUPPORT == 1)
		/* Modify big jump step for 8822B and 8197F */
		if (p_dm_odm->support_ic_type & (ODM_RTL8822B | ODM_RTL8197F))
			phydm_set_big_jump_step(p_dm_odm, current_igi);
#endif

#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT == 1)
		/* Set IGI value of CCK for new CCK AGC */
		if (p_dm_odm->cck_new_agc) {
			if (p_dm_odm->support_ic_type & ODM_IC_PHY_STATUE_NEW_TYPE)
				odm_set_bb_reg(p_dm_odm, 0xa0c, 0x00003f00, (current_igi >> 1));
		}
#endif

		/*Add by YuChen for USB IO too slow issue*/
		if ((p_dm_odm->support_ability & ODM_BB_ADAPTIVITY) && (current_igi > p_dm_dig_table->cur_ig_value)) {
			p_dm_dig_table->cur_ig_value = current_igi;
			phydm_adaptivity(p_dm_odm);
		}

		/* 1 Set IGI value */
		if (p_dm_odm->support_platform & (ODM_WIN | ODM_CE)) {
			odm_set_bb_reg(p_dm_odm, ODM_REG(IGI_A, p_dm_odm), ODM_BIT(IGI, p_dm_odm), current_igi);

			if (p_dm_odm->rf_type > ODM_1T1R)
				odm_set_bb_reg(p_dm_odm, ODM_REG(IGI_B, p_dm_odm), ODM_BIT(IGI, p_dm_odm), current_igi);

#if (RTL8814A_SUPPORT == 1)
			if (p_dm_odm->support_ic_type & ODM_RTL8814A) {
				odm_set_bb_reg(p_dm_odm, ODM_REG(IGI_C, p_dm_odm), ODM_BIT(IGI, p_dm_odm), current_igi);
				odm_set_bb_reg(p_dm_odm, ODM_REG(IGI_D, p_dm_odm), ODM_BIT(IGI, p_dm_odm), current_igi);
			}
#endif
		} else if (p_dm_odm->support_platform & (ODM_AP)) {
			switch (*(p_dm_odm->p_one_path_cca)) {
			case ODM_CCA_2R:
				odm_set_bb_reg(p_dm_odm, ODM_REG(IGI_A, p_dm_odm), ODM_BIT(IGI, p_dm_odm), current_igi);

				if (p_dm_odm->rf_type > ODM_1T1R)
					odm_set_bb_reg(p_dm_odm, ODM_REG(IGI_B, p_dm_odm), ODM_BIT(IGI, p_dm_odm), current_igi);
#if (RTL8814A_SUPPORT == 1)
				if (p_dm_odm->support_ic_type & ODM_RTL8814A) {
					odm_set_bb_reg(p_dm_odm, ODM_REG(IGI_C, p_dm_odm), ODM_BIT(IGI, p_dm_odm), current_igi);
					odm_set_bb_reg(p_dm_odm, ODM_REG(IGI_D, p_dm_odm), ODM_BIT(IGI, p_dm_odm), current_igi);
				}
#endif
				break;
			case ODM_CCA_1R_A:
				odm_set_bb_reg(p_dm_odm, ODM_REG(IGI_A, p_dm_odm), ODM_BIT(IGI, p_dm_odm), current_igi);
				if (p_dm_odm->rf_type != ODM_1T1R)
					odm_set_bb_reg(p_dm_odm, ODM_REG(IGI_B, p_dm_odm), ODM_BIT(IGI, p_dm_odm), get_igi_for_diff(current_igi));
				break;
			case ODM_CCA_1R_B:
				odm_set_bb_reg(p_dm_odm, ODM_REG(IGI_B, p_dm_odm), ODM_BIT(IGI, p_dm_odm), get_igi_for_diff(current_igi));
				if (p_dm_odm->rf_type != ODM_1T1R)
					odm_set_bb_reg(p_dm_odm, ODM_REG(IGI_A, p_dm_odm), ODM_BIT(IGI, p_dm_odm), current_igi);
				break;
			}
		}

		p_dm_dig_table->cur_ig_value = current_igi;
	}

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_TRACE, ("odm_write_dig(): current_igi(0x%02x).\n", current_igi));

}

void
odm_pause_dig(
	void					*p_dm_void,
	enum phydm_pause_type		pause_type,
	enum phydm_pause_level		pause_level,
	u8					igi_value
)
{
	struct PHY_DM_STRUCT			*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _dynamic_initial_gain_threshold_				*p_dm_dig_table = &p_dm_odm->dm_dig_table;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_dig()=========> level = %d\n", pause_level));

	if ((p_dm_dig_table->pause_dig_level == 0) && (!(p_dm_odm->support_ability & ODM_BB_DIG) || !(p_dm_odm->support_ability & ODM_BB_FA_CNT))) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD,
			("odm_pause_dig(): Return: support_ability DIG or FA is disabled !!\n"));
		return;
	}

	if (pause_level > DM_DIG_MAX_PAUSE_TYPE) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD,
			("odm_pause_dig(): Return: Wrong pause level !!\n"));
		return;
	}

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_dig(): pause level = 0x%x, Current value = 0x%x\n", p_dm_dig_table->pause_dig_level, igi_value));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_dig(): pause value = 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		p_dm_dig_table->pause_dig_value[7], p_dm_dig_table->pause_dig_value[6], p_dm_dig_table->pause_dig_value[5], p_dm_dig_table->pause_dig_value[4],
		p_dm_dig_table->pause_dig_value[3], p_dm_dig_table->pause_dig_value[2], p_dm_dig_table->pause_dig_value[1], p_dm_dig_table->pause_dig_value[0]));

	switch (pause_type) {
	/* Pause DIG */
	case PHYDM_PAUSE:
	{
		/* Disable DIG */
		odm_cmn_info_update(p_dm_odm, ODM_CMNINFO_ABILITY, p_dm_odm->support_ability & (~ODM_BB_DIG));
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_dig(): Pause DIG !!\n"));

		/* Backup IGI value */
		if (p_dm_dig_table->pause_dig_level == 0) {
			p_dm_dig_table->igi_backup = p_dm_dig_table->cur_ig_value;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_dig(): Backup IGI  = 0x%x, new IGI = 0x%x\n", p_dm_dig_table->igi_backup, igi_value));
		}

		/* Record IGI value */
		p_dm_dig_table->pause_dig_value[pause_level] = igi_value;

		/* Update pause level */
		p_dm_dig_table->pause_dig_level = (p_dm_dig_table->pause_dig_level | BIT(pause_level));

		/* Write new IGI value */
		if (BIT(pause_level + 1) > p_dm_dig_table->pause_dig_level) {
			odm_write_dig(p_dm_odm, igi_value);
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_dig(): IGI of higher level = 0x%x\n",  igi_value));
		}
		break;
	}
	/* Resume DIG */
	case PHYDM_RESUME:
	{
		/* check if the level is illegal or not */
		if ((p_dm_dig_table->pause_dig_level & (BIT(pause_level))) != 0) {
			p_dm_dig_table->pause_dig_level = p_dm_dig_table->pause_dig_level & (~(BIT(pause_level)));
			p_dm_dig_table->pause_dig_value[pause_level] = 0;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_dig(): Resume DIG !!\n"));
		} else {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_dig(): Wrong resume level !!\n"));
			break;
		}

		/* Resume DIG */
		if (p_dm_dig_table->pause_dig_level == 0) {
			/* Write backup IGI value */
			odm_write_dig(p_dm_odm, p_dm_dig_table->igi_backup);
			p_dm_dig_table->is_ignore_dig = true;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_dig(): Write original IGI = 0x%x\n", p_dm_dig_table->igi_backup));

			/* Enable DIG */
			odm_cmn_info_update(p_dm_odm, ODM_CMNINFO_ABILITY, p_dm_odm->support_ability | ODM_BB_DIG);
			break;
		}

		if (BIT(pause_level) > p_dm_dig_table->pause_dig_level) {
			s8		max_level;

			/* Calculate the maximum level now */
			for (max_level = (pause_level - 1); max_level >= 0; max_level--) {
				if ((p_dm_dig_table->pause_dig_level & BIT(max_level)) > 0)
					break;
			}

			/* write IGI of lower level */
			odm_write_dig(p_dm_odm, p_dm_dig_table->pause_dig_value[max_level]);
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_dig(): Write IGI (0x%x) of level (%d)\n",
				p_dm_dig_table->pause_dig_value[max_level], max_level));
			break;
		}
		break;
	}
	default:
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_dig(): Wrong  type !!\n"));
		break;
	}

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_dig(): pause level = 0x%x, Current value = 0x%x\n", p_dm_dig_table->pause_dig_level, igi_value));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_dig(): pause value = 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		p_dm_dig_table->pause_dig_value[7], p_dm_dig_table->pause_dig_value[6], p_dm_dig_table->pause_dig_value[5], p_dm_dig_table->pause_dig_value[4],
		p_dm_dig_table->pause_dig_value[3], p_dm_dig_table->pause_dig_value[2], p_dm_dig_table->pause_dig_value[1], p_dm_dig_table->pause_dig_value[0]));

}

bool
odm_dig_abort(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _dynamic_initial_gain_threshold_			*p_dm_dig_table = &p_dm_odm->dm_dig_table;

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct rtl8192cd_priv	*priv = p_dm_odm->priv;
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*p_adapter	= p_dm_odm->adapter;
#endif

	/* support_ability */
	if (!(p_dm_odm->support_ability & ODM_BB_FA_CNT)) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Return: support_ability ODM_BB_FA_CNT is disabled\n"));
		return	true;
	}

	/* support_ability */
	if (!(p_dm_odm->support_ability & ODM_BB_DIG)) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Return: support_ability ODM_BB_DIG is disabled\n"));
		return	true;
	}

	/* ScanInProcess */
	if (*(p_dm_odm->p_is_scan_in_process)) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Return: In Scan Progress\n"));
		return	true;
	}

	if (p_dm_dig_table->is_ignore_dig) {
		p_dm_dig_table->is_ignore_dig = false;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Return: Ignore DIG\n"));
		return	true;
	}

	/* add by Neil Chen to avoid PSD is processing */
	if (p_dm_odm->is_dm_initial_gain_enable == false) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Return: PSD is Processing\n"));
		return	true;
	}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#if OS_WIN_FROM_WIN7(OS_VERSION)
	if (IsAPModeExist(p_adapter) && p_adapter->bInHctTest) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Return: Is AP mode or In HCT Test\n"));
		return	true;
	}
#endif

	if (p_dm_odm->is_bt_hs_operation)
		odm_dig_for_bt_hs_mode(p_dm_odm);

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
#ifdef CONFIG_SPECIAL_SETTING_FOR_FUNAI_TV
	if ((p_dm_odm->is_linked) && (p_dm_odm->adapter->registrypriv.force_igi != 0)) {
		printk("p_dm_odm->rssi_min=%d\n", p_dm_odm->rssi_min);
		odm_write_dig(p_dm_odm, p_dm_odm->adapter->registrypriv.force_igi);
		return	true;
	}
#endif
#else
	if (!(priv->up_time > 5)) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Return: Not In DIG operation period\n"));
		return	true;
	}
#endif

	return	false;
}

void
odm_dig_init(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT					*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _dynamic_initial_gain_threshold_						*p_dm_dig_table = &p_dm_odm->dm_dig_table;
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct _FALSE_ALARM_STATISTICS	*false_alm_cnt = (struct _FALSE_ALARM_STATISTICS *)phydm_get_structure(p_dm_odm, PHYDM_FALSEALMCNT);
#endif
	u32						ret_value;
	u8						i;

	p_dm_dig_table->is_stop_dig = false;
	p_dm_dig_table->is_ignore_dig = false;
	p_dm_dig_table->is_psd_in_progress = false;
	p_dm_dig_table->cur_ig_value = (u8) odm_get_bb_reg(p_dm_odm, ODM_REG(IGI_A, p_dm_odm), ODM_BIT(IGI, p_dm_odm));
	p_dm_dig_table->pre_ig_value = 0;
	p_dm_dig_table->rssi_low_thresh	= DM_DIG_THRESH_LOW;
	p_dm_dig_table->rssi_high_thresh	= DM_DIG_THRESH_HIGH;
	p_dm_dig_table->fa_low_thresh	= DM_FALSEALARM_THRESH_LOW;
	p_dm_dig_table->fa_high_thresh	= DM_FALSEALARM_THRESH_HIGH;
	p_dm_dig_table->backoff_val = DM_DIG_BACKOFF_DEFAULT;
	p_dm_dig_table->backoff_val_range_max = DM_DIG_BACKOFF_MAX;
	p_dm_dig_table->backoff_val_range_min = DM_DIG_BACKOFF_MIN;
	p_dm_dig_table->pre_cck_cca_thres = 0xFF;
	p_dm_dig_table->cur_cck_cca_thres = 0x83;
	p_dm_dig_table->forbidden_igi = DM_DIG_MIN_NIC;
	p_dm_dig_table->large_fa_hit = 0;
	p_dm_dig_table->large_fa_timeout = 0;
	p_dm_dig_table->recover_cnt = 0;
	p_dm_dig_table->is_media_connect_0 = false;
	p_dm_dig_table->is_media_connect_1 = false;

	/* To Initialize p_dm_odm->is_dm_initial_gain_enable == false to avoid DIG error */
	p_dm_odm->is_dm_initial_gain_enable = true;

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	p_dm_dig_table->dig_dynamic_min_0 = 0x25;
	p_dm_dig_table->dig_dynamic_min_1 = 0x25;

	/* For AP\ ADSL modified DIG */
	p_dm_dig_table->is_tp_target = false;
	p_dm_dig_table->is_noise_est = true;
	p_dm_dig_table->igi_offset_a = 0;
	p_dm_dig_table->igi_offset_b = 0;
	p_dm_dig_table->tp_train_th_min = 0;

	/* For RTL8881A */
	false_alm_cnt->cnt_ofdm_fail_pre = 0;

	/* Dyanmic EDCCA */
	if (p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES)
		odm_set_bb_reg(p_dm_odm, 0xC50, 0xFFFF0000, 0xfafd);
#else
	p_dm_dig_table->dig_dynamic_min_0 = DM_DIG_MIN_NIC;
	p_dm_dig_table->dig_dynamic_min_1 = DM_DIG_MIN_NIC;

	/* To Initi BT30 IGI */
	p_dm_dig_table->bt30_cur_igi = 0x32;

	odm_memory_set(p_dm_odm, p_dm_dig_table->pause_dig_value, 0, (DM_DIG_MAX_PAUSE_TYPE + 1));
	p_dm_dig_table->pause_dig_level = 0;
	odm_memory_set(p_dm_odm, p_dm_dig_table->pause_cckpd_value, 0, (DM_DIG_MAX_PAUSE_TYPE + 1));
	p_dm_dig_table->pause_cckpd_level = 0;
#endif

	if (p_dm_odm->board_type & (ODM_BOARD_EXT_PA | ODM_BOARD_EXT_LNA)) {
		p_dm_dig_table->rx_gain_range_max = DM_DIG_MAX_NIC;
		p_dm_dig_table->rx_gain_range_min = DM_DIG_MIN_NIC;
	} else {
		p_dm_dig_table->rx_gain_range_max = DM_DIG_MAX_NIC;
		p_dm_dig_table->rx_gain_range_min = DM_DIG_MIN_NIC;
	}

#if (RTL8822B_SUPPORT == 1 || RTL8197F_SUPPORT == 1)
	p_dm_dig_table->enable_adjust_big_jump = 1;
	if (p_dm_odm->support_ic_type & ODM_RTL8822B) {
		ret_value = odm_get_bb_reg(p_dm_odm, 0x8c8, MASKLWORD);
		p_dm_dig_table->big_jump_step1 = (u8)(ret_value & 0xe) >> 1;
		p_dm_dig_table->big_jump_step2 = (u8)(ret_value & 0x30) >> 4;
		p_dm_dig_table->big_jump_step3 = (u8)(ret_value & 0xc0) >> 6;

	} else if (p_dm_odm->support_ic_type & ODM_RTL8197F) {
		ret_value = odm_get_bb_reg(p_dm_odm, ODM_REG_BB_AGC_SET_2_11N, MASKLWORD);
		p_dm_dig_table->big_jump_step1 = (u8)(ret_value & 0xe) >> 1;
		p_dm_dig_table->big_jump_step2 = (u8)(ret_value & 0x30) >> 4;
		p_dm_dig_table->big_jump_step3 = (u8)(ret_value & 0xc0) >> 6;
	}
	if (p_dm_odm->support_ic_type & (ODM_RTL8822B | ODM_RTL8197F)) {
		for (i = 0; i < sizeof(p_dm_dig_table->big_jump_lmt); i++) {
			if (p_dm_dig_table->big_jump_lmt[i] == 0)
				p_dm_dig_table->big_jump_lmt[i] = 0x64;		/* Set -10dBm as default value */
		}
	}
#endif
}


void
odm_DIG(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT					*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER					*p_adapter	= p_dm_odm->adapter;
	HAL_DATA_TYPE				*p_hal_data = GET_HAL_DATA(p_dm_odm->adapter);
#elif (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct rtl8192cd_priv				*priv = p_dm_odm->priv;
	struct sta_info				*p_entry;
#endif

	/* Common parameters */
	struct _dynamic_initial_gain_threshold_						*p_dm_dig_table = &p_dm_odm->dm_dig_table;
	struct _FALSE_ALARM_STATISTICS		*p_false_alm_cnt = (struct _FALSE_ALARM_STATISTICS *)phydm_get_structure(p_dm_odm, PHYDM_FALSEALMCNT);
	bool						first_connect, first_dis_connect;
	u8						dig_max_of_min, dig_dynamic_min;
	u8						dm_dig_max, dm_dig_min;
	u8						current_igi = p_dm_dig_table->cur_ig_value;
	u8						offset;
	u32						dm_FA_thres[3];
	u32						tx_tp = 0, rx_tp = 0;
	bool						dig_go_up_check = true;
	bool						is_dfs_band = false;
	bool						is_performance = true, is_first_tp_target = false, is_first_coverage = false;
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	u32						tp_train_th_min = dm_dig_tp_target_th0;
	static		u8			time_cnt = 0;
	u8						i;
#endif

	if (odm_dig_abort(p_dm_odm) == true)
		return;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG()===========================>\n\n"));


	/* 1 Update status */
	{
		dig_dynamic_min = p_dm_dig_table->dig_dynamic_min_0;
		first_connect = (p_dm_odm->is_linked) && (p_dm_dig_table->is_media_connect_0 == false);
		first_dis_connect = (!p_dm_odm->is_linked) && (p_dm_dig_table->is_media_connect_0 == true);
	}

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	/* 1 Noise Floor Estimate */
	/* p_dm_dig_table->is_noise_est = (first_connect)?true:p_dm_dig_table->is_noise_est; */
	/* odm_inband_noise_calculate (p_dm_odm); */

	/* 1 mode decision */
	if (p_dm_odm->is_linked) {
		/* 2 Calculate total TP */
		for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
			p_entry = p_dm_odm->p_odm_sta_info[i];
			if (IS_STA_VALID(p_entry)) {
				rx_tp += (u32)(p_entry->rx_byte_cnt_low_maw >> 7);
				tx_tp += (u32)(p_entry->tx_byte_cnt_low_maw >> 7);			/* Kbps */
			}
		}
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): TX TP = %dkbps, RX TP = %dkbps\n", tx_tp, rx_tp));
	}

	switch (p_dm_odm->priv->pshare->rf_ft_var.dig_cov_enable) {
	case 0:
	{
		is_performance = true;
		break;
	}
	case 1:
	{
		is_performance = false;
		break;
	}
	case 2:
	{
		if (p_dm_odm->is_linked) {
			if (p_dm_dig_table->tp_train_th_min > dm_dig_tp_target_th0)
				tp_train_th_min = p_dm_dig_table->tp_train_th_min;

			if (p_dm_dig_table->tp_train_th_min > dm_dig_tp_target_th1)
				tp_train_th_min = dm_dig_tp_target_th1;

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): TP training mode lower bound = %dkbps\n", tp_train_th_min));

			/* 2 Decide DIG mode by total TP */
			if ((tx_tp + rx_tp) > dm_dig_tp_target_th1) {		/* change to performance mode */
				is_first_tp_target = (!p_dm_dig_table->is_tp_target) ? true : false;
				p_dm_dig_table->is_tp_target = true;
				is_performance = true;
			} else if ((tx_tp + rx_tp) < tp_train_th_min) {	/* change to coverage mode */
				is_first_coverage = (p_dm_dig_table->is_tp_target) ? true : false;

				if (time_cnt < dm_dig_tp_training_period) {
					p_dm_dig_table->is_tp_target = false;
					is_performance = false;
					time_cnt++;
				} else {
					p_dm_dig_table->is_tp_target = true;
					is_performance = true;
					is_first_tp_target = true;
					time_cnt = 0;
				}
			} else {									/* remain previous mode */
				is_performance = p_dm_dig_table->is_tp_target;

				if (!is_performance) {
					if (time_cnt < dm_dig_tp_training_period)
						time_cnt++;
					else {
						p_dm_dig_table->is_tp_target = true;
						is_performance = true;
						is_first_tp_target = true;
						time_cnt = 0;
					}
				}
			}

			if (!is_performance)
				p_dm_dig_table->tp_train_th_min = rx_tp + tx_tp;

		} else {
			is_performance = false;
			p_dm_dig_table->tp_train_th_min = 0;
		}
		break;
	}
	default:
		is_performance = true;
	}

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("====== DIG mode = %d  ======\n", p_dm_odm->priv->pshare->rf_ft_var.dig_cov_enable));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("====== is_performance = %d ======\n", is_performance));
#endif

	/* 1 Boundary Decision */
	{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
		/* 2 For AP\ADSL */
		if (!is_performance) {
			dm_dig_max = DM_DIG_MAX_AP_COVERAGR;
			dm_dig_min = DM_DIG_MIN_AP_COVERAGE;
			dig_max_of_min = DM_DIG_MAX_OF_MIN_COVERAGE;
		} else {
			if (p_dm_odm->rf_type == ODM_1T1R)
				dm_dig_max = DM_DIG_MAX_AP - 6;
			else
				dm_dig_max = DM_DIG_MAX_AP;

			if ((*p_dm_odm->p_band_type == ODM_BAND_2_4G) && (p_dm_odm->support_ic_type & ODM_RTL8814A)) /* for 2G 8814 */
				dm_dig_min = 0x1c;
			else if (p_dm_odm->support_ic_type & ODM_RTL8197F)
				dm_dig_min = 0x1e;
			else
				dm_dig_min = DM_DIG_MIN_AP;
			dig_max_of_min = DM_DIG_MAX_OF_MIN;
		}

		/* 4 TX2path */
		if (priv->pmib->dot11RFEntry.tx2path && !is_dfs_band && (*(p_dm_odm->p_wireless_mode) == ODM_WM_B))
			dm_dig_max = 0x2A;

#if RTL8192E_SUPPORT
#ifdef HIGH_POWER_EXT_LNA
		if ((p_dm_odm->support_ic_type & (ODM_RTL8192E)) && (p_dm_odm->ext_lna))
			dm_dig_max = 0x42;
#endif
#endif
		if (p_dm_odm->igi_lower_bound) {
			if (dm_dig_min < p_dm_odm->igi_lower_bound)
				dm_dig_min = p_dm_odm->igi_lower_bound;
			if (dig_max_of_min < p_dm_odm->igi_lower_bound)
				dig_max_of_min = p_dm_odm->igi_lower_bound;
		}
		if (p_dm_odm->igi_upper_bound) {
			if (dm_dig_max > p_dm_odm->igi_upper_bound)
				dm_dig_max = p_dm_odm->igi_upper_bound;
			if (dig_max_of_min > p_dm_odm->igi_upper_bound)
				dig_max_of_min = p_dm_odm->igi_upper_bound;
		}
#else
		/* 2 For WIN\CE */
		if (p_dm_odm->support_ic_type >= ODM_RTL8188E)
			dm_dig_max = 0x5A;
		else
			dm_dig_max = DM_DIG_MAX_NIC;

		if (p_dm_odm->support_ic_type != ODM_RTL8821)
			dm_dig_min = DM_DIG_MIN_NIC;
		else
			dm_dig_min = 0x1C;

		dig_max_of_min = DM_DIG_MAX_AP;
#endif


#if (DM_ODM_SUPPORT_TYPE & (ODM_AP | ODM_CE))
		/* Modify lower bound for DFS band */
		if ((((*p_dm_odm->p_channel >= 52) && (*p_dm_odm->p_channel <= 64)) ||
		     ((*p_dm_odm->p_channel >= 100) && (*p_dm_odm->p_channel <= 140)))
#if (DM_ODM_SUPPORT_TYPE & (ODM_CE))
		    && phydm_dfs_master_enabled(p_dm_odm) == true
#endif
		   ) {
			is_dfs_band = true;
			if (*p_dm_odm->p_band_width == ODM_BW20M)
				dm_dig_min = DM_DIG_MIN_AP_DFS + 2;
			else
				dm_dig_min = DM_DIG_MIN_AP_DFS;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): ====== In DFS band ======\n"));
		}
#endif
	}
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Absolutly upper bound = 0x%x, lower bound = 0x%x\n", dm_dig_max, dm_dig_min));

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	if (p_dm_odm->pu1_forced_igi_lb && (0 < *p_dm_odm->pu1_forced_igi_lb)) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Force IGI lb to: 0x%02x !!!!!!\n", *p_dm_odm->pu1_forced_igi_lb));
		dm_dig_min = *p_dm_odm->pu1_forced_igi_lb;
		dm_dig_max = (dm_dig_min <= dm_dig_max) ? (dm_dig_max) : (dm_dig_min + 1);
	}
#endif

	/* 1 Adjust boundary by RSSI */
	if (p_dm_odm->is_linked && is_performance) {
		/* 2 Modify DIG upper bound */
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
		offset = 15;
#else
		/* 4 Modify DIG upper bound for 92E, 8723A\B, 8821 & 8812 BT */
		if ((p_dm_odm->support_ic_type & (ODM_RTL8192E | ODM_RTL8723B | ODM_RTL8812 | ODM_RTL8821)) && (p_dm_odm->is_bt_limited_dig == 1)) {
			offset = 10;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Coex. case: Force upper bound to RSSI + %d !!!!!!\n", offset));
		} else
			offset = 15;
#endif

		if ((p_dm_odm->rssi_min + offset) > dm_dig_max)
			p_dm_dig_table->rx_gain_range_max = dm_dig_max;
		else if ((p_dm_odm->rssi_min + offset) < dm_dig_min)
			p_dm_dig_table->rx_gain_range_max = dm_dig_min;
		else
			p_dm_dig_table->rx_gain_range_max = p_dm_odm->rssi_min + offset;

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
		/* 2 Modify DIG lower bound */
		/* if(p_dm_odm->is_one_entry_only) */
		{
			if (p_dm_odm->rssi_min < dm_dig_min)
				dig_dynamic_min = dm_dig_min;
			else if (p_dm_odm->rssi_min > dig_max_of_min)
				dig_dynamic_min = dig_max_of_min;
			else
				dig_dynamic_min = p_dm_odm->rssi_min;

#if (DM_ODM_SUPPORT_TYPE & ODM_CE)
			if (is_dfs_band) {
				dig_dynamic_min = dm_dig_min;
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): DFS band: Force lower bound to 0x%x after link !!!!!!\n", dm_dig_min));
			}
#endif
		}
#else
		{
			/* 4 For AP */
#ifdef __ECOS
			HAL_REORDER_BARRIER();
#else
			rmb();
#endif
			if (is_dfs_band) {
				dig_dynamic_min = dm_dig_min;
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): DFS band: Force lower bound to 0x%x after link !!!!!!\n", dm_dig_min));
			} else {
				if (p_dm_odm->rssi_min < dm_dig_min)
					dig_dynamic_min = dm_dig_min;
				else if (p_dm_odm->rssi_min > dig_max_of_min)
					dig_dynamic_min = dig_max_of_min;
				else
					dig_dynamic_min = p_dm_odm->rssi_min;
			}
		}
#endif
	} else {
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP | ODM_CE))
		if (is_performance && is_dfs_band) {
			p_dm_dig_table->rx_gain_range_max = 0x28;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): DFS band: Force upper bound to 0x%x before link !!!!!!\n", p_dm_dig_table->rx_gain_range_max));
		} else
#endif
		{
			if (is_performance)
				p_dm_dig_table->rx_gain_range_max = DM_DIG_MAX_OF_MIN;
			else
				p_dm_dig_table->rx_gain_range_max = dm_dig_max;
		}
		dig_dynamic_min = dm_dig_min;
	}

	/* 1 Force Lower Bound for AntDiv */
	if (p_dm_odm->is_linked && !p_dm_odm->is_one_entry_only) {
		if ((p_dm_odm->support_ic_type & ODM_ANTDIV_SUPPORT) && (p_dm_odm->support_ability & ODM_BB_ANT_DIV)) {
			if (p_dm_odm->ant_div_type == CG_TRX_HW_ANTDIV || p_dm_odm->ant_div_type == CG_TRX_SMART_ANTDIV) {
				if (p_dm_dig_table->ant_div_rssi_max > dig_max_of_min)
					dig_dynamic_min = dig_max_of_min;
				else
					dig_dynamic_min = (u8) p_dm_dig_table->ant_div_rssi_max;
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Antenna diversity case: Force lower bound to 0x%x !!!!!!\n", dig_dynamic_min));
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Antenna diversity case: RSSI_max = 0x%x !!!!!!\n", p_dm_dig_table->ant_div_rssi_max));
			}
		}
	}
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Adjust boundary by RSSI Upper bound = 0x%x, Lower bound = 0x%x\n",
			p_dm_dig_table->rx_gain_range_max, dig_dynamic_min));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Link status: is_linked = %d, RSSI = %d, bFirstConnect = %d, bFirsrDisConnect = %d\n\n",
		p_dm_odm->is_linked, p_dm_odm->rssi_min, first_connect, first_dis_connect));

	/* 1 Modify DIG lower bound, deal with abnormal case */
	/* 2 Abnormal false alarm case */
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP | ODM_CE))
	if (is_dfs_band)
		p_dm_dig_table->rx_gain_range_min = dig_dynamic_min;
	else
#endif
	{
		if (!p_dm_odm->is_linked) {
			p_dm_dig_table->rx_gain_range_min = dig_dynamic_min;

			if (first_dis_connect)
				p_dm_dig_table->forbidden_igi = dig_dynamic_min;
		} else
			p_dm_dig_table->rx_gain_range_min = odm_forbidden_igi_check(p_dm_odm, dig_dynamic_min, current_igi);
	}

	/* 2 Abnormal # beacon case */
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	if (p_dm_odm->is_linked && !first_connect) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("Beacon Num (%d)\n", p_dm_odm->phy_dbg_info.num_qry_beacon_pkt));
		if ((p_dm_odm->phy_dbg_info.num_qry_beacon_pkt < 5) && (p_dm_odm->bsta_state)) {
			p_dm_dig_table->rx_gain_range_min = 0x1c;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Abnrormal #beacon (%d) case in STA mode: Force lower bound to 0x%x !!!!!!\n\n",
				p_dm_odm->phy_dbg_info.num_qry_beacon_pkt, p_dm_dig_table->rx_gain_range_min));
		}
	}
#endif

	/* 2 Abnormal lower bound case */
	if (p_dm_dig_table->rx_gain_range_min > p_dm_dig_table->rx_gain_range_max) {
		p_dm_dig_table->rx_gain_range_min = p_dm_dig_table->rx_gain_range_max;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Abnrormal lower bound case: Force lower bound to 0x%x !!!!!!\n\n", p_dm_dig_table->rx_gain_range_min));
	}


	/* 1 False alarm threshold decision */
	odm_fa_threshold_check(p_dm_odm, is_dfs_band, is_performance, rx_tp, tx_tp, dm_FA_thres);
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): False alarm threshold = %d, %d, %d \n\n", dm_FA_thres[0], dm_FA_thres[1], dm_FA_thres[2]));

	/* 1 Adjust initial gain by false alarm */
	if (p_dm_odm->is_linked && is_performance) {
		/* 2 After link */
		ODM_RT_TRACE(p_dm_odm,	ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Adjust IGI after link\n"));

		if (is_first_tp_target || (first_connect && is_performance)) {
			p_dm_dig_table->large_fa_hit = 0;

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP | ODM_CE))
			if (is_dfs_band) {
				if (p_dm_odm->rssi_min > 0x28)
					current_igi = 0x28;
				else
					current_igi = p_dm_odm->rssi_min;
				ODM_RT_TRACE(p_dm_odm,	ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): DFS band: One-shot to 0x28 upmost!!!!!!\n"));
			} else
#endif
			{
				if (p_dm_odm->rssi_min < dig_max_of_min) {
					if (current_igi < p_dm_odm->rssi_min)
						current_igi = p_dm_odm->rssi_min;
				} else {
					if (current_igi < dig_max_of_min)
						current_igi = dig_max_of_min;
				}

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (RTL8812A_SUPPORT == 1)
				if (p_dm_odm->support_ic_type == ODM_RTL8812)
					odm_config_bb_with_header_file(p_dm_odm, CONFIG_BB_AGC_TAB_DIFF);
#endif
#endif
			}

			ODM_RT_TRACE(p_dm_odm,	ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): First connect case: IGI does on-shot to 0x%x\n", current_igi));

		} else {

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))

			if (priv->pshare->rf_ft_var.dig_upcheck_enable)
				dig_go_up_check = phydm_dig_go_up_check(p_dm_odm);
#endif

			if ((p_false_alm_cnt->cnt_all > dm_FA_thres[2]) && dig_go_up_check)
				current_igi = current_igi + 4;
			else if ((p_false_alm_cnt->cnt_all > dm_FA_thres[1]) && dig_go_up_check)
				current_igi = current_igi + 2;
			else if (p_false_alm_cnt->cnt_all < dm_FA_thres[0])
				current_igi = current_igi - 2;

			/* 4 Abnormal # beacon case */
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
			if ((p_dm_odm->phy_dbg_info.num_qry_beacon_pkt < 5) && (p_false_alm_cnt->cnt_all < DM_DIG_FA_TH1) && (p_dm_odm->bsta_state)) {
				current_igi = p_dm_dig_table->rx_gain_range_min;
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Abnormal #beacon (%d) case: IGI does one-shot to 0x%x\n",
					p_dm_odm->phy_dbg_info.num_qry_beacon_pkt, current_igi));
			}
#endif
		}
	} else {
		/* 2 Before link */
		ODM_RT_TRACE(p_dm_odm,	ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): Adjust IGI before link\n"));

		if (first_dis_connect || is_first_coverage) {
			current_igi = dm_dig_min;
			ODM_RT_TRACE(p_dm_odm,	ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): First disconnect case: IGI does on-shot to lower bound\n"));
		} else {

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
			if (priv->pshare->rf_ft_var.dig_upcheck_enable)
				dig_go_up_check = phydm_dig_go_up_check(p_dm_odm);
#endif

			if ((p_false_alm_cnt->cnt_all > dm_FA_thres[2]) && dig_go_up_check)
				current_igi = current_igi + 4;
			else if ((p_false_alm_cnt->cnt_all > dm_FA_thres[1]) && dig_go_up_check)
				current_igi = current_igi + 2;
			else if (p_false_alm_cnt->cnt_all < dm_FA_thres[0])
				current_igi = current_igi - 2;
		}
	}

	/* 1 Check initial gain by upper/lower bound */
	if (current_igi < p_dm_dig_table->rx_gain_range_min)
		current_igi = p_dm_dig_table->rx_gain_range_min;

	if (current_igi > p_dm_dig_table->rx_gain_range_max)
		current_igi = p_dm_dig_table->rx_gain_range_max;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): cur_ig_value=0x%x, TotalFA = %d\n\n", current_igi, p_false_alm_cnt->cnt_all));

	/* 1 Update status */
	{
#if ((DM_ODM_SUPPORT_TYPE & ODM_WIN) || ((DM_ODM_SUPPORT_TYPE & ODM_CE) && (ODM_CONFIG_BT_COEXIST == 1)))
		if (p_dm_odm->is_bt_hs_operation) {
			if (p_dm_odm->is_linked) {
				if (p_dm_dig_table->bt30_cur_igi > (current_igi))
					odm_write_dig(p_dm_odm, current_igi);
				else
					odm_write_dig(p_dm_odm, p_dm_dig_table->bt30_cur_igi);

				p_dm_dig_table->is_media_connect_0 = p_dm_odm->is_linked;
				p_dm_dig_table->dig_dynamic_min_0 = dig_dynamic_min;
			} else {
				if (p_dm_odm->is_link_in_process)
					odm_write_dig(p_dm_odm, 0x1c);
				else if (p_dm_odm->is_bt_connect_process)
					odm_write_dig(p_dm_odm, 0x28);
				else
					odm_write_dig(p_dm_odm, p_dm_dig_table->bt30_cur_igi);/* odm_write_dig(p_dm_odm, p_dm_dig_table->cur_ig_value); */
			}
		} else		/* BT is not using */
#endif
		{
			odm_write_dig(p_dm_odm, current_igi);/* odm_write_dig(p_dm_odm, p_dm_dig_table->cur_ig_value); */
			p_dm_dig_table->is_media_connect_0 = p_dm_odm->is_linked;
			p_dm_dig_table->dig_dynamic_min_0 = dig_dynamic_min;
		}
	}
}

void
odm_dig_by_rssi_lps(
	void		*p_dm_void
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	struct PHY_DM_STRUCT					*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FALSE_ALARM_STATISTICS		*p_false_alm_cnt = (struct _FALSE_ALARM_STATISTICS *)phydm_get_structure(p_dm_odm, PHYDM_FALSEALMCNT);

	u8	rssi_lower = DM_DIG_MIN_NIC; /* 0x1E or 0x1C */
	u8	current_igi = p_dm_odm->rssi_min;

	if (odm_dig_abort(p_dm_odm) == true)
		return;

	current_igi = current_igi + RSSI_OFFSET_DIG;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_dig_by_rssi_lps()==>\n"));

	/* Using FW PS mode to make IGI */
	/* Adjust by  FA in LPS MODE */
	if (p_false_alm_cnt->cnt_all > DM_DIG_FA_TH2_LPS)
		current_igi = current_igi + 4;
	else if (p_false_alm_cnt->cnt_all > DM_DIG_FA_TH1_LPS)
		current_igi = current_igi + 2;
	else if (p_false_alm_cnt->cnt_all < DM_DIG_FA_TH0_LPS)
		current_igi = current_igi - 2;


	/* Lower bound checking */

	/* RSSI Lower bound check */
	if ((p_dm_odm->rssi_min - 10) > DM_DIG_MIN_NIC)
		rssi_lower = (p_dm_odm->rssi_min - 10);
	else
		rssi_lower = DM_DIG_MIN_NIC;

	/* Upper and Lower Bound checking */
	if (current_igi > DM_DIG_MAX_NIC)
		current_igi = DM_DIG_MAX_NIC;
	else if (current_igi < rssi_lower)
		current_igi = rssi_lower;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_dig_by_rssi_lps(): p_false_alm_cnt->cnt_all = %d\n", p_false_alm_cnt->cnt_all));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_dig_by_rssi_lps(): p_dm_odm->rssi_min = %d\n", p_dm_odm->rssi_min));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_dig_by_rssi_lps(): current_igi = 0x%x\n", current_igi));

	odm_write_dig(p_dm_odm, current_igi);/* odm_write_dig(p_dm_odm, p_dm_dig_table->cur_ig_value); */
#endif
}

/* 3============================================================
 * 3 FASLE ALARM CHECK
 * 3============================================================ */

void
odm_false_alarm_counter_statistics(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT					*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FALSE_ALARM_STATISTICS	*false_alm_cnt = (struct _FALSE_ALARM_STATISTICS *)phydm_get_structure(p_dm_odm, PHYDM_FALSEALMCNT);
#if (PHYDM_LA_MODE_SUPPORT == 1)
	struct _RT_ADCSMP					*adc_smp = &(p_dm_odm->adcsmp);
#endif
	u32						ret_value;

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	/* Mark there, and check this in odm_DMWatchDog */
#if 0 /* (DM_ODM_SUPPORT_TYPE == ODM_AP) */
	struct rtl8192cd_priv *priv		= p_dm_odm->priv;
	if ((priv->auto_channel != 0) && (priv->auto_channel != 2))
		return;
#endif
#endif

	if (!(p_dm_odm->support_ability & ODM_BB_FA_CNT))
		return;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("odm_false_alarm_counter_statistics()======>\n"));

#if (ODM_IC_11N_SERIES_SUPPORT == 1)
	if (p_dm_odm->support_ic_type & ODM_IC_11N_SERIES) {

		/* hold ofdm counter */
		odm_set_bb_reg(p_dm_odm, ODM_REG_OFDM_FA_HOLDC_11N, BIT(31), 1); /* hold page C counter */
		odm_set_bb_reg(p_dm_odm, ODM_REG_OFDM_FA_RSTD_11N, BIT(31), 1); /* hold page D counter */

		ret_value = odm_get_bb_reg(p_dm_odm, ODM_REG_OFDM_FA_TYPE1_11N, MASKDWORD);
		false_alm_cnt->cnt_fast_fsync = (ret_value & 0xffff);
		false_alm_cnt->cnt_sb_search_fail = ((ret_value & 0xffff0000) >> 16);

		ret_value = odm_get_bb_reg(p_dm_odm, ODM_REG_OFDM_FA_TYPE2_11N, MASKDWORD);
		false_alm_cnt->cnt_ofdm_cca = (ret_value & 0xffff);
		false_alm_cnt->cnt_parity_fail = ((ret_value & 0xffff0000) >> 16);

		ret_value = odm_get_bb_reg(p_dm_odm, ODM_REG_OFDM_FA_TYPE3_11N, MASKDWORD);
		false_alm_cnt->cnt_rate_illegal = (ret_value & 0xffff);
		false_alm_cnt->cnt_crc8_fail = ((ret_value & 0xffff0000) >> 16);

		ret_value = odm_get_bb_reg(p_dm_odm, ODM_REG_OFDM_FA_TYPE4_11N, MASKDWORD);
		false_alm_cnt->cnt_mcs_fail = (ret_value & 0xffff);

		false_alm_cnt->cnt_ofdm_fail =	false_alm_cnt->cnt_parity_fail + false_alm_cnt->cnt_rate_illegal +
			false_alm_cnt->cnt_crc8_fail + false_alm_cnt->cnt_mcs_fail +
			false_alm_cnt->cnt_fast_fsync + false_alm_cnt->cnt_sb_search_fail;

		/* read CCK CRC32 counter */
		false_alm_cnt->cnt_cck_crc32_error = odm_get_bb_reg(p_dm_odm, ODM_REG_CCK_CRC32_ERROR_CNT_11N, MASKDWORD);
		false_alm_cnt->cnt_cck_crc32_ok = odm_get_bb_reg(p_dm_odm, ODM_REG_CCK_CRC32_OK_CNT_11N, MASKDWORD);

		/* read OFDM CRC32 counter */
		ret_value = odm_get_bb_reg(p_dm_odm, ODM_REG_OFDM_CRC32_CNT_11N, MASKDWORD);
		false_alm_cnt->cnt_ofdm_crc32_error = (ret_value & 0xffff0000) >> 16;
		false_alm_cnt->cnt_ofdm_crc32_ok = ret_value & 0xffff;

		/* read HT CRC32 counter */
		ret_value = odm_get_bb_reg(p_dm_odm, ODM_REG_HT_CRC32_CNT_11N, MASKDWORD);
		false_alm_cnt->cnt_ht_crc32_error = (ret_value & 0xffff0000) >> 16;
		false_alm_cnt->cnt_ht_crc32_ok = ret_value & 0xffff;

		/* read VHT CRC32 counter */
		false_alm_cnt->cnt_vht_crc32_error = 0;
		false_alm_cnt->cnt_vht_crc32_ok = 0;

#if (RTL8188E_SUPPORT == 1)
		if (p_dm_odm->support_ic_type == ODM_RTL8188E) {
			ret_value = odm_get_bb_reg(p_dm_odm, ODM_REG_SC_CNT_11N, MASKDWORD);
			false_alm_cnt->cnt_bw_lsc = (ret_value & 0xffff);
			false_alm_cnt->cnt_bw_usc = ((ret_value & 0xffff0000) >> 16);
		}
#endif

		{
			/* hold cck counter */
			odm_set_bb_reg(p_dm_odm, ODM_REG_CCK_FA_RST_11N, BIT(12), 1);
			odm_set_bb_reg(p_dm_odm, ODM_REG_CCK_FA_RST_11N, BIT(14), 1);

			ret_value = odm_get_bb_reg(p_dm_odm, ODM_REG_CCK_FA_LSB_11N, MASKBYTE0);
			false_alm_cnt->cnt_cck_fail = ret_value;

			ret_value = odm_get_bb_reg(p_dm_odm, ODM_REG_CCK_FA_MSB_11N, MASKBYTE3);
			false_alm_cnt->cnt_cck_fail += (ret_value & 0xff) << 8;

			ret_value = odm_get_bb_reg(p_dm_odm, ODM_REG_CCK_CCA_CNT_11N, MASKDWORD);
			false_alm_cnt->cnt_cck_cca = ((ret_value & 0xFF) << 8) | ((ret_value & 0xFF00) >> 8);
		}

		false_alm_cnt->cnt_all_pre = false_alm_cnt->cnt_all;

		false_alm_cnt->cnt_all = (false_alm_cnt->cnt_fast_fsync +
					  false_alm_cnt->cnt_sb_search_fail +
					  false_alm_cnt->cnt_parity_fail +
					  false_alm_cnt->cnt_rate_illegal +
					  false_alm_cnt->cnt_crc8_fail +
					  false_alm_cnt->cnt_mcs_fail +
					  false_alm_cnt->cnt_cck_fail);

		false_alm_cnt->cnt_cca_all = false_alm_cnt->cnt_ofdm_cca + false_alm_cnt->cnt_cck_cca;

		if (p_dm_odm->support_ic_type >= ODM_RTL8188E) {
			/*reset false alarm counter registers*/
			odm_set_bb_reg(p_dm_odm, ODM_REG_OFDM_FA_RSTC_11N, BIT(31), 1);
			odm_set_bb_reg(p_dm_odm, ODM_REG_OFDM_FA_RSTC_11N, BIT(31), 0);
			odm_set_bb_reg(p_dm_odm, ODM_REG_OFDM_FA_RSTD_11N, BIT(27), 1);
			odm_set_bb_reg(p_dm_odm, ODM_REG_OFDM_FA_RSTD_11N, BIT(27), 0);

			/*update ofdm counter*/
			odm_set_bb_reg(p_dm_odm, ODM_REG_OFDM_FA_HOLDC_11N, BIT(31), 0);	/*update page C counter*/
			odm_set_bb_reg(p_dm_odm, ODM_REG_OFDM_FA_RSTD_11N, BIT(31), 0);	/*update page D counter*/

			/*reset CCK CCA counter*/
			odm_set_bb_reg(p_dm_odm, ODM_REG_CCK_FA_RST_11N, BIT(13) | BIT(12), 0);
			odm_set_bb_reg(p_dm_odm, ODM_REG_CCK_FA_RST_11N, BIT(13) | BIT(12), 2);

			/*reset CCK FA counter*/
			odm_set_bb_reg(p_dm_odm, ODM_REG_CCK_FA_RST_11N, BIT(15) | BIT(14), 0);
			odm_set_bb_reg(p_dm_odm, ODM_REG_CCK_FA_RST_11N, BIT(15) | BIT(14), 2);

			/*reset CRC32 counter*/
			odm_set_bb_reg(p_dm_odm, ODM_REG_PAGE_F_RST_11N, BIT(16), 1);
			odm_set_bb_reg(p_dm_odm, ODM_REG_PAGE_F_RST_11N, BIT(16), 0);
		}

		/* Get debug port 0 */
		odm_set_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11N, MASKDWORD, 0x0);
		false_alm_cnt->dbg_port0 = odm_get_bb_reg(p_dm_odm, ODM_REG_RPT_11N, MASKDWORD);

		/* Get EDCCA flag */
		odm_set_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11N, MASKDWORD, 0x208);
		false_alm_cnt->edcca_flag = (bool)odm_get_bb_reg(p_dm_odm, ODM_REG_RPT_11N, BIT(30));

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("odm_false_alarm_counter_statistics(): cnt_fast_fsync=%d, cnt_sb_search_fail=%d\n",
			false_alm_cnt->cnt_fast_fsync, false_alm_cnt->cnt_sb_search_fail));
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("odm_false_alarm_counter_statistics(): cnt_parity_fail=%d, cnt_rate_illegal=%d\n",
			false_alm_cnt->cnt_parity_fail, false_alm_cnt->cnt_rate_illegal));
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("odm_false_alarm_counter_statistics(): cnt_crc8_fail=%d, cnt_mcs_fail=%d\n",
			false_alm_cnt->cnt_crc8_fail, false_alm_cnt->cnt_mcs_fail));
	}
#endif

#if (ODM_IC_11AC_SERIES_SUPPORT == 1)
	if (p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES) {
		u32 cck_enable;

		/* read OFDM FA counter */
		false_alm_cnt->cnt_ofdm_fail = odm_get_bb_reg(p_dm_odm, ODM_REG_OFDM_FA_11AC, MASKLWORD);

		/* Read CCK FA counter */
		false_alm_cnt->cnt_cck_fail = odm_get_bb_reg(p_dm_odm, ODM_REG_CCK_FA_11AC, MASKLWORD);

		/* read CCK/OFDM CCA counter */
		ret_value = odm_get_bb_reg(p_dm_odm, ODM_REG_CCK_CCA_CNT_11AC, MASKDWORD);
		false_alm_cnt->cnt_ofdm_cca = (ret_value & 0xffff0000) >> 16;
		false_alm_cnt->cnt_cck_cca = ret_value & 0xffff;

		/* read CCK CRC32 counter */
		ret_value = odm_get_bb_reg(p_dm_odm, ODM_REG_CCK_CRC32_CNT_11AC, MASKDWORD);
		false_alm_cnt->cnt_cck_crc32_error = (ret_value & 0xffff0000) >> 16;
		false_alm_cnt->cnt_cck_crc32_ok = ret_value & 0xffff;

		/* read OFDM CRC32 counter */
		ret_value = odm_get_bb_reg(p_dm_odm, ODM_REG_OFDM_CRC32_CNT_11AC, MASKDWORD);
		false_alm_cnt->cnt_ofdm_crc32_error = (ret_value & 0xffff0000) >> 16;
		false_alm_cnt->cnt_ofdm_crc32_ok = ret_value & 0xffff;

		/* read HT CRC32 counter */
		ret_value = odm_get_bb_reg(p_dm_odm, ODM_REG_HT_CRC32_CNT_11AC, MASKDWORD);
		false_alm_cnt->cnt_ht_crc32_error = (ret_value & 0xffff0000) >> 16;
		false_alm_cnt->cnt_ht_crc32_ok = ret_value & 0xffff;

		/* read VHT CRC32 counter */
		ret_value = odm_get_bb_reg(p_dm_odm, ODM_REG_VHT_CRC32_CNT_11AC, MASKDWORD);
		false_alm_cnt->cnt_vht_crc32_error = (ret_value & 0xffff0000) >> 16;
		false_alm_cnt->cnt_vht_crc32_ok = ret_value & 0xffff;

#if (RTL8881A_SUPPORT == 1)
		/* For 8881A */
		if (p_dm_odm->support_ic_type == ODM_RTL8881A) {
			u32 cnt_ofdm_fail_temp = 0;

			if (false_alm_cnt->cnt_ofdm_fail >= false_alm_cnt->cnt_ofdm_fail_pre) {
				cnt_ofdm_fail_temp = false_alm_cnt->cnt_ofdm_fail_pre;
				false_alm_cnt->cnt_ofdm_fail_pre = false_alm_cnt->cnt_ofdm_fail;
				false_alm_cnt->cnt_ofdm_fail = false_alm_cnt->cnt_ofdm_fail - cnt_ofdm_fail_temp;
			} else
				false_alm_cnt->cnt_ofdm_fail_pre = false_alm_cnt->cnt_ofdm_fail;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("odm_false_alarm_counter_statistics(): cnt_ofdm_fail=%d\n",	false_alm_cnt->cnt_ofdm_fail_pre));
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("odm_false_alarm_counter_statistics(): cnt_ofdm_fail_pre=%d\n",	cnt_ofdm_fail_temp));

			/* Reset FA counter by enable/disable OFDM */
			if (false_alm_cnt->cnt_ofdm_fail_pre >= 0x7fff) {
				/* reset OFDM */
				odm_set_bb_reg(p_dm_odm, ODM_REG_BB_RX_PATH_11AC, BIT(29), 0);
				odm_set_bb_reg(p_dm_odm, ODM_REG_BB_RX_PATH_11AC, BIT(29), 1);
				false_alm_cnt->cnt_ofdm_fail_pre = 0;
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("odm_false_alarm_counter_statistics(): Reset false alarm counter\n"));
			}
		}
#endif

		/* reset OFDM FA coutner */
		odm_set_bb_reg(p_dm_odm, ODM_REG_OFDM_FA_RST_11AC, BIT(17), 1);
		odm_set_bb_reg(p_dm_odm, ODM_REG_OFDM_FA_RST_11AC, BIT(17), 0);

		/* reset CCK FA counter */
		odm_set_bb_reg(p_dm_odm, ODM_REG_CCK_FA_RST_11AC, BIT(15), 0);
		odm_set_bb_reg(p_dm_odm, ODM_REG_CCK_FA_RST_11AC, BIT(15), 1);

		/* reset CCA counter */
		odm_set_bb_reg(p_dm_odm, ODM_REG_RST_RPT_11AC, BIT(0), 1);
		odm_set_bb_reg(p_dm_odm, ODM_REG_RST_RPT_11AC, BIT(0), 0);

		cck_enable =  odm_get_bb_reg(p_dm_odm, ODM_REG_BB_RX_PATH_11AC, BIT(28));
		if (cck_enable) { /* if(*p_dm_odm->p_band_type == ODM_BAND_2_4G) */
			false_alm_cnt->cnt_all = false_alm_cnt->cnt_ofdm_fail + false_alm_cnt->cnt_cck_fail;
			false_alm_cnt->cnt_cca_all = false_alm_cnt->cnt_cck_cca + false_alm_cnt->cnt_ofdm_cca;
		} else {
			false_alm_cnt->cnt_all = false_alm_cnt->cnt_ofdm_fail;
			false_alm_cnt->cnt_cca_all = false_alm_cnt->cnt_ofdm_cca;
		}

#if (PHYDM_LA_MODE_SUPPORT == 1)
		if (adc_smp->adc_smp_state == ADCSMP_STATE_IDLE)
#endif
		{
			/* Get debug port 0 */
			odm_set_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x0);
			false_alm_cnt->dbg_port0 = odm_get_bb_reg(p_dm_odm, ODM_REG_RPT_11AC, MASKDWORD);

			/* Get EDCCA flag */
			odm_set_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x209);
			false_alm_cnt->edcca_flag = (bool)odm_get_bb_reg(p_dm_odm, ODM_REG_RPT_11AC, BIT(30));
		}

	}
#endif


	false_alm_cnt->cnt_crc32_error_all = false_alm_cnt->cnt_vht_crc32_error + false_alm_cnt->cnt_ht_crc32_error + false_alm_cnt->cnt_ofdm_crc32_error + false_alm_cnt->cnt_cck_crc32_error;
	false_alm_cnt->cnt_crc32_ok_all = false_alm_cnt->cnt_vht_crc32_ok + false_alm_cnt->cnt_ht_crc32_ok + false_alm_cnt->cnt_ofdm_crc32_ok + false_alm_cnt->cnt_cck_crc32_ok;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("odm_false_alarm_counter_statistics(): cnt_ofdm_cca=%d\n", false_alm_cnt->cnt_ofdm_cca));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("odm_false_alarm_counter_statistics(): cnt_cck_cca=%d\n", false_alm_cnt->cnt_cck_cca));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("odm_false_alarm_counter_statistics(): cnt_cca_all=%d\n", false_alm_cnt->cnt_cca_all));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("odm_false_alarm_counter_statistics(): cnt_ofdm_fail=%d\n", false_alm_cnt->cnt_ofdm_fail));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("odm_false_alarm_counter_statistics(): cnt_cck_fail=%d\n", false_alm_cnt->cnt_cck_fail));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("odm_false_alarm_counter_statistics(): cnt_ofdm_fail=%d\n", false_alm_cnt->cnt_ofdm_fail));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("odm_false_alarm_counter_statistics(): Total False Alarm=%d\n", false_alm_cnt->cnt_all));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("odm_false_alarm_counter_statistics(): CCK CRC32 fail: %d, ok: %d\n", false_alm_cnt->cnt_cck_crc32_error, false_alm_cnt->cnt_cck_crc32_ok));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("odm_false_alarm_counter_statistics(): OFDM CRC32 fail: %d, ok: %d\n", false_alm_cnt->cnt_ofdm_crc32_error, false_alm_cnt->cnt_ofdm_crc32_ok));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("odm_false_alarm_counter_statistics(): HT CRC32 fail: %d, ok: %d\n", false_alm_cnt->cnt_ht_crc32_error, false_alm_cnt->cnt_ht_crc32_ok));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("odm_false_alarm_counter_statistics(): VHT CRC32 fail: %d, ok: %d\n", false_alm_cnt->cnt_vht_crc32_error, false_alm_cnt->cnt_vht_crc32_ok));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("odm_false_alarm_counter_statistics(): Total CRC32 fail: %d, ok: %d\n", false_alm_cnt->cnt_crc32_error_all, false_alm_cnt->cnt_crc32_ok_all));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_FA_CNT, ODM_DBG_LOUD, ("odm_false_alarm_counter_statistics(): dbg port 0x0 = 0x%x, EDCCA = %d\n\n", false_alm_cnt->dbg_port0, false_alm_cnt->edcca_flag));
}

/* 3============================================================
 * 3 CCK Packet Detect threshold
 * 3============================================================ */

void
odm_pause_cck_packet_detection(
	void					*p_dm_void,
	enum phydm_pause_type		pause_type,
	enum phydm_pause_level		pause_level,
	u8					cck_pd_threshold
)
{
	struct PHY_DM_STRUCT			*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _dynamic_initial_gain_threshold_				*p_dm_dig_table = &p_dm_odm->dm_dig_table;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_cck_packet_detection()=========> level = %d\n", pause_level));

	if ((p_dm_dig_table->pause_cckpd_level == 0) && (!(p_dm_odm->support_ability & ODM_BB_CCK_PD) || !(p_dm_odm->support_ability & ODM_BB_FA_CNT))) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("Return: support_ability ODM_BB_CCK_PD or ODM_BB_FA_CNT is disabled\n"));
		return;
	}

	if (pause_level > DM_DIG_MAX_PAUSE_TYPE) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD,
			("odm_pause_cck_packet_detection(): Return: Wrong pause level !!\n"));
		return;
	}

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_cck_packet_detection(): pause level = 0x%x, Current value = 0x%x\n", p_dm_dig_table->pause_cckpd_level, cck_pd_threshold));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_cck_packet_detection(): pause value = 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		p_dm_dig_table->pause_cckpd_value[7], p_dm_dig_table->pause_cckpd_value[6], p_dm_dig_table->pause_cckpd_value[5], p_dm_dig_table->pause_cckpd_value[4],
		p_dm_dig_table->pause_cckpd_value[3], p_dm_dig_table->pause_cckpd_value[2], p_dm_dig_table->pause_cckpd_value[1], p_dm_dig_table->pause_cckpd_value[0]));

	switch (pause_type) {
	/* Pause CCK Packet Detection threshold */
	case PHYDM_PAUSE:
	{
		/* Disable CCK PD */
		odm_cmn_info_update(p_dm_odm, ODM_CMNINFO_ABILITY, p_dm_odm->support_ability & (~ODM_BB_CCK_PD));
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_cck_packet_detection(): Pause CCK packet detection threshold !!\n"));

		/* Backup original CCK PD threshold decided by CCK PD mechanism */
		if (p_dm_dig_table->pause_cckpd_level == 0) {
			p_dm_dig_table->cck_pd_backup = p_dm_dig_table->cur_cck_cca_thres;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD,
				("odm_pause_cck_packet_detection(): Backup CCKPD  = 0x%x, new CCKPD = 0x%x\n", p_dm_dig_table->cck_pd_backup, cck_pd_threshold));
		}

		/* Update pause level */
		p_dm_dig_table->pause_cckpd_level = (p_dm_dig_table->pause_cckpd_level | BIT(pause_level));

		/* Record CCK PD threshold */
		p_dm_dig_table->pause_cckpd_value[pause_level] = cck_pd_threshold;

		/* Write new CCK PD threshold */
		if (BIT(pause_level + 1) > p_dm_dig_table->pause_cckpd_level) {
			odm_write_cck_cca_thres(p_dm_odm, cck_pd_threshold);
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_cck_packet_detection(): CCKPD of higher level = 0x%x\n", cck_pd_threshold));
		}
		break;
	}
	/* Resume CCK Packet Detection threshold */
	case PHYDM_RESUME:
	{
		/* check if the level is illegal or not */
		if ((p_dm_dig_table->pause_cckpd_level & (BIT(pause_level))) != 0) {
			p_dm_dig_table->pause_cckpd_level = p_dm_dig_table->pause_cckpd_level & (~(BIT(pause_level)));
			p_dm_dig_table->pause_cckpd_value[pause_level] = 0;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_cck_packet_detection(): Resume CCK PD !!\n"));
		} else {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_cck_packet_detection(): Wrong resume level !!\n"));
			break;
		}

		/* Resume DIG */
		if (p_dm_dig_table->pause_cckpd_level == 0) {
			/* Write backup IGI value */
			odm_write_cck_cca_thres(p_dm_odm, p_dm_dig_table->cck_pd_backup);
			/* p_dm_dig_table->is_ignore_dig = true; */
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_cck_packet_detection(): Write original CCKPD = 0x%x\n", p_dm_dig_table->cck_pd_backup));

			/* Enable DIG */
			odm_cmn_info_update(p_dm_odm, ODM_CMNINFO_ABILITY, p_dm_odm->support_ability | ODM_BB_CCK_PD);
			break;
		}

		if (BIT(pause_level) > p_dm_dig_table->pause_cckpd_level) {
			s8	max_level;

			/* Calculate the maximum level now */
			for (max_level = (pause_level - 1); max_level >= 0; max_level--) {
				if ((p_dm_dig_table->pause_cckpd_level & BIT(max_level)) > 0)
					break;
			}

			/* write CCKPD of lower level */
			odm_write_cck_cca_thres(p_dm_odm, p_dm_dig_table->pause_cckpd_value[max_level]);
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_cck_packet_detection(): Write CCKPD (0x%x) of level (%d)\n",
				p_dm_dig_table->pause_cckpd_value[max_level], max_level));
			break;
		}
		break;
	}
	default:
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_cck_packet_detection(): Wrong  type !!\n"));
		break;
	}

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_cck_packet_detection(): pause level = 0x%x, Current value = 0x%x\n", p_dm_dig_table->pause_cckpd_level, cck_pd_threshold));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_pause_cck_packet_detection(): pause value = 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		p_dm_dig_table->pause_cckpd_value[7], p_dm_dig_table->pause_cckpd_value[6], p_dm_dig_table->pause_cckpd_value[5], p_dm_dig_table->pause_cckpd_value[4],
		p_dm_dig_table->pause_cckpd_value[3], p_dm_dig_table->pause_cckpd_value[2], p_dm_dig_table->pause_cckpd_value[1], p_dm_dig_table->pause_cckpd_value[0]));
}


void
odm_cck_packet_detection_thresh(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT				*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _dynamic_initial_gain_threshold_					*p_dm_dig_table = &p_dm_odm->dm_dig_table;
	struct _FALSE_ALARM_STATISTICS	*false_alm_cnt = (struct _FALSE_ALARM_STATISTICS *)phydm_get_structure(p_dm_odm, PHYDM_FALSEALMCNT);
	u8					cur_cck_cca_thres = p_dm_dig_table->cur_cck_cca_thres, RSSI_thd = 35;


#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	/* modify by Guo.Mingzhi 2011-12-29 */
	if (p_dm_odm->is_dual_mac_smart_concurrent == true)
		/*	if (p_dm_odm->is_dual_mac_smart_concurrent == false) */
		return;
	if (p_dm_odm->is_bt_hs_operation) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_cck_packet_detection_thresh() write 0xcd for BT HS mode!!\n"));
		odm_write_cck_cca_thres(p_dm_odm, 0xcd);
		return;
	}
#endif

	if ((!(p_dm_odm->support_ability & ODM_BB_CCK_PD)) || (!(p_dm_odm->support_ability & ODM_BB_FA_CNT))) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_cck_packet_detection_thresh()  return==========\n"));
#ifdef MCR_WIRELESS_EXTEND
		odm_write_cck_cca_thres(p_dm_odm, 0x43);
#endif
		return;
	}

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	if (p_dm_odm->ext_lna)
		return;
#endif

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_cck_packet_detection_thresh()  ==========>\n"));

	if (p_dm_dig_table->cck_fa_ma == 0xffffffff)
		p_dm_dig_table->cck_fa_ma = false_alm_cnt->cnt_cck_fail;
	else
		p_dm_dig_table->cck_fa_ma = ((p_dm_dig_table->cck_fa_ma << 1) + p_dm_dig_table->cck_fa_ma + false_alm_cnt->cnt_cck_fail) >> 2;
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_cck_packet_detection_thresh(): CCK FA moving average = %d\n", p_dm_dig_table->cck_fa_ma));

	if (p_dm_odm->is_linked) {
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if 0 /*for [PCIE-1596]*/
		if (p_dm_odm->rssi_min > (RSSI_thd + 14))
			cur_cck_cca_thres = 0xed;
		else if (p_dm_odm->rssi_min > (RSSI_thd + 6))
			cur_cck_cca_thres = 0xdd;
		else
#endif
			if (p_dm_odm->rssi_min > RSSI_thd)
				cur_cck_cca_thres = 0xcd;
			else if (p_dm_odm->rssi_min > 20) {
				if (p_dm_dig_table->cck_fa_ma > ((DM_DIG_FA_TH1 >> 1) + (DM_DIG_FA_TH1 >> 3)))
					cur_cck_cca_thres = 0xcd;
				else if (p_dm_dig_table->cck_fa_ma < (DM_DIG_FA_TH0 >> 1))
					cur_cck_cca_thres = 0x83;
			} else if (p_dm_odm->rssi_min > 7)
				cur_cck_cca_thres = 0x83;
			else
				cur_cck_cca_thres = 0x40;
#else
		if (p_dm_dig_table->cur_ig_value > (0x24 + 14))
			cur_cck_cca_thres = 0xed;
		else if (p_dm_dig_table->cur_ig_value > (0x24 + 6))
			cur_cck_cca_thres = 0xdd;
		else if (p_dm_dig_table->cur_ig_value > 0x24)
			cur_cck_cca_thres = 0xcd;
		else {
			if (p_dm_dig_table->cck_fa_ma > 0x400)
				cur_cck_cca_thres = 0x83;
			else if (p_dm_dig_table->cck_fa_ma < 0x200)
				cur_cck_cca_thres = 0x40;
		}

#endif
	} else {
		if (p_dm_dig_table->cck_fa_ma > 0x400)
			cur_cck_cca_thres = 0x83;
		else if (p_dm_dig_table->cck_fa_ma < 0x200)
			cur_cck_cca_thres = 0x40;
	}

	odm_write_cck_cca_thres(p_dm_odm, cur_cck_cca_thres);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_cck_packet_detection_thresh()  cur_cck_cca_thres = 0x%x\n", cur_cck_cca_thres));
}

void
odm_write_cck_cca_thres(
	void			*p_dm_void,
	u8			cur_cck_cca_thres
)
{
	struct PHY_DM_STRUCT			*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _dynamic_initial_gain_threshold_				*p_dm_dig_table = &p_dm_odm->dm_dig_table;

	if (p_dm_dig_table->cur_cck_cca_thres != cur_cck_cca_thres) {	/* modify by Guo.Mingzhi 2012-01-03 */
		odm_write_1byte(p_dm_odm, ODM_REG(CCK_CCA, p_dm_odm), cur_cck_cca_thres);
		p_dm_dig_table->cck_fa_ma = 0xffffffff;
	}
	p_dm_dig_table->pre_cck_cca_thres = p_dm_dig_table->cur_cck_cca_thres;
	p_dm_dig_table->cur_cck_cca_thres = cur_cck_cca_thres;
}

bool
phydm_dig_go_up_check(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT				*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _CCX_INFO				*ccx_info = &p_dm_odm->dm_ccx_info;
	struct _dynamic_initial_gain_threshold_					*p_dm_dig_table = &p_dm_odm->dm_dig_table;
	u8					cur_ig_value = p_dm_dig_table->cur_ig_value;
	u8					max_DIG_cover_bond;
	u8					current_igi_max_up_resolution;
	u8					rx_gain_range_max;
	u8					i = 0;

	u32					total_NHM_cnt;
	u32					DIG_cover_cnt;
	u32					over_DIG_cover_cnt;
	bool					ret = true;

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct rtl8192cd_priv				*priv = p_dm_odm->priv;

	max_DIG_cover_bond = DM_DIG_MAX_AP - priv->pshare->rf_ft_var.dig_upcheck_initial_value;
	current_igi_max_up_resolution = cur_ig_value + 6;
	rx_gain_range_max = p_dm_dig_table->rx_gain_range_max;

	phydm_get_nhm_result(p_dm_odm);

	total_NHM_cnt = ccx_info->NHM_result[0] + ccx_info->NHM_result[1];

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("phydm_dig_go_up_check(): *****Get NHM results*****\n"));

	if (total_NHM_cnt != 0) {

		/* cur_ig_value < max_DIG_cover_bond - 6 */
		if (p_dm_dig_table->dig_go_up_check_level == DIG_GOUPCHECK_LEVEL_0) {
			DIG_cover_cnt = ccx_info->NHM_result[1];
			ret = ((priv->pshare->rf_ft_var.dig_level0_ratio_reciprocal * DIG_cover_cnt) >= total_NHM_cnt) ? true : false;
		}

		/* (max_DIG_cover_bond - 6) <= cur_ig_value < DM_DIG_MAX_AP */
		else if (p_dm_dig_table->dig_go_up_check_level == DIG_GOUPCHECK_LEVEL_1) {
			over_DIG_cover_cnt = ccx_info->NHM_result[1];
			ret = (priv->pshare->rf_ft_var.dig_level1_ratio_reciprocal * over_DIG_cover_cnt < total_NHM_cnt) ? true : false;

			if (!ret) {
				/* update p_dm_dig_table->rx_gain_range_max */
				p_dm_dig_table->rx_gain_range_max = (rx_gain_range_max >= max_DIG_cover_bond - 6) ? (max_DIG_cover_bond - 6) : rx_gain_range_max;

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("phydm_dig_go_up_check(): Noise power is beyond DIG can filter, lock rx_gain_range_max to 0x%x\n",
					p_dm_dig_table->rx_gain_range_max));
			}
		}

		/* cur_ig_value > DM_DIG_MAX_AP, foolproof */
		else if (p_dm_dig_table->dig_go_up_check_level == DIG_GOUPCHECK_LEVEL_2)
			ret = true;

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("phydm_dig_go_up_check(): DIG_GoUpCheck_level = %d\n, current_igi_max_up_resolution = 0x%x\n, max_DIG_cover_bond = 0x%x\n, rx_gain_range_max = 0x%x, ret = %d\n",
				p_dm_dig_table->dig_go_up_check_level,
				current_igi_max_up_resolution,
				max_DIG_cover_bond,
				p_dm_dig_table->rx_gain_range_max,
				ret));

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("phydm_dig_go_up_check(): NHM_result = %d, %d, %d, %d\n",
			ccx_info->NHM_result[0], ccx_info->NHM_result[1], ccx_info->NHM_result[2], ccx_info->NHM_result[3]));

	} else
		ret = true;

	for (i = 0 ; i <= 10 ; i++)
		ccx_info->NHM_th[i] = 0xFF;

	if (cur_ig_value < max_DIG_cover_bond - 6) {
		ccx_info->NHM_th[0] = 2 * (cur_ig_value - priv->pshare->rf_ft_var.dig_upcheck_initial_value);
		p_dm_dig_table->dig_go_up_check_level = DIG_GOUPCHECK_LEVEL_0;
	} else if (cur_ig_value <= DM_DIG_MAX_AP) {
		ccx_info->NHM_th[0] = 2 * max_DIG_cover_bond;
		p_dm_dig_table->dig_go_up_check_level = DIG_GOUPCHECK_LEVEL_1;
	}
	/* cur_ig_value > DM_DIG_MAX_AP, foolproof */
	else {
		p_dm_dig_table->dig_go_up_check_level = DIG_GOUPCHECK_LEVEL_2;
		ret = true;
	}

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("phydm_dig_go_up_check(): *****Set NHM settings*****\n"));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("phydm_dig_go_up_check(): DIG_GoUpCheck_level = %d\n",
			p_dm_dig_table->dig_go_up_check_level));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("phydm_dig_go_up_check(): NHM_th = 0x%x, 0x%x, 0x%x\n",
		ccx_info->NHM_th[0], ccx_info->NHM_th[1], ccx_info->NHM_th[2]));

	ccx_info->nhm_inexclude_cca = NHM_EXCLUDE_CCA;
	ccx_info->nhm_inexclude_txon = NHM_EXCLUDE_TXON;
	ccx_info->NHM_period = 0xC350;

	phydm_nhm_setting(p_dm_odm, SET_NHM_SETTING);
	phydm_nhm_trigger(p_dm_odm);
#endif

	return ret;
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

/* <20130108, Kordan> E.g., With LNA used, we make the Rx power smaller to have a better EVM. (Asked by Willis) */
void
odm_rfe_control(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u64		rssi_val
)
{
	struct _ADAPTER		*adapter = (struct _ADAPTER *)p_dm_odm->adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
	static u8	trsw_high_pwr = 0;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("===> odm_rfe_control, RSSI = %d, trsw_high_pwr = 0x%X, p_dm_odm->rfe_type = %d\n",
			rssi_val, trsw_high_pwr, p_dm_odm->rfe_type));

	if (p_dm_odm->rfe_type == 3) {

		p_dm_odm->RSSI_TRSW = rssi_val;

		if (p_dm_odm->RSSI_TRSW >= p_dm_odm->RSSI_TRSW_H) {
			trsw_high_pwr = 1; /* Switch to */
			odm_set_bb_reg(p_dm_odm, REG_ANTSEL_SW_JAGUAR, BIT(1) | BIT0, 0x1); /* Set ANTSW=1/ANTSWB=0  for SW control */
			odm_set_bb_reg(p_dm_odm, REG_ANTSEL_SW_JAGUAR, BIT(9) | BIT8, 0x3); /* Set ANTSW=1/ANTSWB=0  for SW control */

		} else if (p_dm_odm->RSSI_TRSW <= p_dm_odm->RSSI_TRSW_L) {
			trsw_high_pwr = 0; /* Switched back */
			odm_set_bb_reg(p_dm_odm, REG_ANTSEL_SW_JAGUAR, BIT(1) | BIT0, 0x1); /* Set ANTSW=1/ANTSWB=0  for SW control */
			odm_set_bb_reg(p_dm_odm, REG_ANTSEL_SW_JAGUAR, BIT(9) | BIT8, 0x0); /* Set ANTSW=1/ANTSWB=0  for SW control */

		}
	}


	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("(p_dm_odm->RSSI_TRSW_H, p_dm_odm->RSSI_TRSW_L) = (%d, %d)\n", p_dm_odm->RSSI_TRSW_H, p_dm_odm->RSSI_TRSW_L));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("(rssi_val, rssi_val, p_dm_odm->RSSI_TRSW_iso) = (%d, %d, %d)\n",
		rssi_val, p_dm_odm->RSSI_TRSW_iso, p_dm_odm->RSSI_TRSW));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("<=== odm_rfe_control, RSSI = %d, trsw_high_pwr = 0x%X\n", rssi_val, trsw_high_pwr));
}

void
odm_mpt_dig_work_item_callback(
	void            *p_context
)
{
	struct _ADAPTER	*adapter = (struct _ADAPTER *)p_context;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;

	ODM_MPT_DIG(p_dm_odm);
}

void
odm_mpt_dig_callback(
	struct timer_list		*p_timer
)
{
	struct _ADAPTER		*adapter = (struct _ADAPTER *)p_timer->Adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;


#if DEV_BUS_TYPE == RT_PCI_INTERFACE
#if USE_WORKITEM
	odm_schedule_work_item(&p_dm_odm->mpt_dig_workitem);
#else
	ODM_MPT_DIG(p_dm_odm);
#endif
#else
	odm_schedule_work_item(&p_dm_odm->mpt_dig_workitem);
#endif

}

#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
void
odm_mpt_dig_callback(
	void					*p_dm_void
)
{
	struct PHY_DM_STRUCT			*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
#if USE_WORKITEM
	odm_schedule_work_item(&p_dm_odm->mpt_dig_workitem);
#else
	ODM_MPT_DIG(p_dm_odm);
#endif
}
#endif

#if (DM_ODM_SUPPORT_TYPE != ODM_CE)
void
odm_mpt_write_dig(
	void					*p_dm_void,
	u8					cur_ig_value
)
{
	struct PHY_DM_STRUCT					*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _dynamic_initial_gain_threshold_						*p_dm_dig_table = &p_dm_odm->dm_dig_table;

	odm_write_1byte(p_dm_odm, ODM_REG(IGI_A, p_dm_odm), cur_ig_value);

#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT == 1)
	/* Set IGI value of CCK for new CCK AGC */
	if (p_dm_odm->cck_new_agc) {
		if (p_dm_odm->support_ic_type & ODM_IC_PHY_STATUE_NEW_TYPE)
			odm_set_bb_reg(p_dm_odm, 0xa0c, 0x00003f00, (cur_ig_value >> 1));
	}

#endif

	if (p_dm_odm->rf_type > ODM_1T1R)
		odm_write_1byte(p_dm_odm, ODM_REG(IGI_B, p_dm_odm), cur_ig_value);

	if ((p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES) && (p_dm_odm->rf_type > ODM_2T2R)) {
		odm_write_1byte(p_dm_odm, ODM_REG(IGI_C, p_dm_odm), cur_ig_value);
		odm_write_1byte(p_dm_odm, ODM_REG(IGI_D, p_dm_odm), cur_ig_value);
	}

	p_dm_dig_table->cur_ig_value = cur_ig_value;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("cur_ig_value = 0x%x\n", cur_ig_value));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("p_dm_odm->rf_type = 0x%x\n", p_dm_odm->rf_type));
}

void
ODM_MPT_DIG(
	void					*p_dm_void
)
{
	struct PHY_DM_STRUCT					*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _dynamic_initial_gain_threshold_						*p_dm_dig_table = &p_dm_odm->dm_dig_table;
	struct _FALSE_ALARM_STATISTICS		*p_false_alm_cnt = (struct _FALSE_ALARM_STATISTICS *)phydm_get_structure(p_dm_odm, PHYDM_FALSEALMCNT);
	u8						current_igi = p_dm_dig_table->cur_ig_value;
	u8						dig_upper = 0x40, dig_lower = 0x20;
	u32						rx_ok_cal;
	u32						rx_pwdb_ave_final;
	u8						IGI_A = 0x20, IGI_B = 0x20;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

#if ODM_FIX_2G_DIG
	IGI_A = 0x22;
	IGI_B = 0x24;
#endif

#else
	if (!(p_dm_odm->priv->pshare->rf_ft_var.mp_specific && p_dm_odm->priv->pshare->mp_dig_on))
		return;

	if (*p_dm_odm->p_band_type == ODM_BAND_5G)
		dig_lower = 0x22;
#endif

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("===> ODM_MPT_DIG, p_band_type = %d\n", *p_dm_odm->p_band_type));

#if (ODM_FIX_2G_DIG || (DM_ODM_SUPPORT_TYPE & ODM_AP))
	if (*p_dm_odm->p_band_type == ODM_BAND_5G || (p_dm_odm->support_ic_type & (ODM_RTL8814A | ODM_RTL8822B))) /* for 5G or 8814 */
#else
	if (1) /* for both 2G/5G */
#endif
	{
		odm_false_alarm_counter_statistics(p_dm_odm);

		rx_ok_cal = p_dm_odm->phy_dbg_info.num_qry_phy_status_cck + p_dm_odm->phy_dbg_info.num_qry_phy_status_ofdm;
		rx_pwdb_ave_final = (rx_ok_cal != 0) ? p_dm_odm->rx_pwdb_ave / rx_ok_cal : 0;

		p_dm_odm->phy_dbg_info.num_qry_phy_status_cck = 0;
		p_dm_odm->phy_dbg_info.num_qry_phy_status_ofdm = 0;
		p_dm_odm->rx_pwdb_ave = 0;
		p_dm_odm->MPDIG_2G = false;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		p_dm_odm->times_2g = 0;
#endif

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("RX OK = %d\n", rx_ok_cal));
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("RSSI = %d\n", rx_pwdb_ave_final));

		if (rx_ok_cal >= 70 && rx_pwdb_ave_final <= 40) {
			if (current_igi > 0x24)
				odm_mpt_write_dig(p_dm_odm, 0x24);
		} else {
			if (p_false_alm_cnt->cnt_all > 1000)
				current_igi = current_igi + 8;
			else if (p_false_alm_cnt->cnt_all > 200)
				current_igi = current_igi + 4;
			else if (p_false_alm_cnt->cnt_all > 50)
				current_igi = current_igi + 2;
			else if (p_false_alm_cnt->cnt_all < 2)
				current_igi = current_igi - 2;

			if (current_igi < dig_lower)
				current_igi = dig_lower;

			if (current_igi > dig_upper)
				current_igi = dig_upper;

			odm_mpt_write_dig(p_dm_odm, current_igi);
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("DIG = 0x%x, cnt_all = %d, cnt_ofdm_fail = %d, cnt_cck_fail = %d\n",
				current_igi, p_false_alm_cnt->cnt_all, p_false_alm_cnt->cnt_ofdm_fail, p_false_alm_cnt->cnt_cck_fail));
		}
	} else {
		if (p_dm_odm->MPDIG_2G == false) {
			if ((p_dm_odm->support_platform & ODM_WIN) && !(p_dm_odm->support_ic_type & (ODM_RTL8814A | ODM_RTL8822B))) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("===> Fix IGI\n"));
				odm_write_1byte(p_dm_odm, ODM_REG(IGI_A, p_dm_odm), IGI_A);
				odm_write_1byte(p_dm_odm, ODM_REG(IGI_B, p_dm_odm), IGI_B);
				p_dm_dig_table->cur_ig_value = IGI_B;
			} else
				odm_mpt_write_dig(p_dm_odm, IGI_A);
		}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		p_dm_odm->times_2g++;

		if (p_dm_odm->times_2g == 3)
#endif
		{
			p_dm_odm->MPDIG_2G = true;
		}
	}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if (p_dm_odm->support_ic_type == ODM_RTL8812)
		odm_rfe_control(p_dm_odm, rx_pwdb_ave_final);
#endif

	odm_set_timer(p_dm_odm, &p_dm_odm->mpt_dig_timer, 700);
}
#endif
