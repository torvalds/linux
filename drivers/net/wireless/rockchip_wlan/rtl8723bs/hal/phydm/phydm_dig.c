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


boolean
phydm_dig_go_up_check(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _CCX_INFO			*ccx_info = &p_dm->dm_ccx_info;
	struct phydm_dig_struct		*p_dig_t = &p_dm->dm_dig_table;
	u8		cur_ig_value = p_dig_t->cur_ig_value;
	u8		max_cover_bond;
	u8		rx_gain_range_max = p_dig_t->rx_gain_range_max;
	u8		i = 0, j = 0;
	u8		total_nhm_cnt = ccx_info->nhm_result_total;
	u32		dig_cover_cnt = 0;
	u32		over_dig_cover_cnt = 0;
	boolean		ret = true;

	if (*p_dm->p_bb_op_mode == PHYDM_PERFORMANCE_MODE)
		return ret;

	max_cover_bond = DIG_MAX_BALANCE_MODE - p_dig_t->dig_upcheck_initial_value;

	if (cur_ig_value < max_cover_bond - 6)
		p_dig_t->dig_go_up_check_level = DIG_GOUPCHECK_LEVEL_0;
	else if (cur_ig_value <= DIG_MAX_BALANCE_MODE)
		p_dig_t->dig_go_up_check_level = DIG_GOUPCHECK_LEVEL_1;
	else	/* cur_ig_value > DM_DIG_MAX_AP, foolproof */
		p_dig_t->dig_go_up_check_level = DIG_GOUPCHECK_LEVEL_2;
	

	PHYDM_DBG(p_dm, DBG_DIG, ("check_lv = %d, max_cover_bond = 0x%x\n",
			p_dig_t->dig_go_up_check_level,
			max_cover_bond));

	if (total_nhm_cnt != 0) {
		if (p_dig_t->dig_go_up_check_level == DIG_GOUPCHECK_LEVEL_0) {
			for (i = 3; i<=11; i++)
				dig_cover_cnt += ccx_info->nhm_result[i];
			ret = ((p_dig_t->dig_level0_ratio_reciprocal * dig_cover_cnt) >= total_nhm_cnt) ? true : false;
		} else if (p_dig_t->dig_go_up_check_level == DIG_GOUPCHECK_LEVEL_1) {
			
			/* search index */
			for (i = 0; i<=10; i++) {
				if ((max_cover_bond * 2) == ccx_info->nhm_th[i]) {
					for(j =(i+1); j <= 11; j++)
						over_dig_cover_cnt += ccx_info->nhm_result[j];
					break;
				}
			}
			ret = (p_dig_t->dig_level1_ratio_reciprocal * over_dig_cover_cnt < total_nhm_cnt) ? true : false;

			if (!ret) {
				/* update p_dig_t->rx_gain_range_max */
				p_dig_t->rx_gain_range_max = (rx_gain_range_max >= max_cover_bond - 6) ? (max_cover_bond - 6) : rx_gain_range_max;

				PHYDM_DBG(p_dm, DBG_DIG,
					("Noise pwr over DIG can filter, lock rx_gain_range_max to 0x%x\n",
					p_dig_t->rx_gain_range_max));
			}
		} else if (p_dig_t->dig_go_up_check_level == DIG_GOUPCHECK_LEVEL_2) {
			/* cur_ig_value > DM_DIG_MAX_AP, foolproof */
			ret = true;
		}
	} else
		ret = true;

	return ret;
}

void
odm_fa_threshold_check(
	void			*p_dm_void,
	boolean			is_dfs_band,
	boolean			is_performance
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_dig_struct	*p_dig_t = &p_dm->dm_dig_table;

	if (p_dig_t->is_dbg_fa_th == true)
		return;
	
	if (p_dm->is_linked && (is_performance || is_dfs_band)) {
		if ((p_dm->rx_tp >> 2) > p_dm->tx_tp && p_dm->rx_tp < 10 && p_dm->rx_tp > 1) {			/*10Mbps & 1Mbps*/
			p_dig_t->fa_th[0] = 125;
			p_dig_t->fa_th[1] = 250;
			p_dig_t->fa_th[2] = 500;
		} else {
			p_dig_t->fa_th[0] = 250;
			p_dig_t->fa_th[1] = 500;
			p_dig_t->fa_th[2] = 750;
		}
	} else {

		if (is_dfs_band) {	/* For DFS band and no link */
			
			p_dig_t->fa_th[0] = 250;
			p_dig_t->fa_th[1] = 1000;
			p_dig_t->fa_th[2] = 2000;
		} else {
			p_dig_t->fa_th[0] = 2000;
			p_dig_t->fa_th[1] = 4000;
			p_dig_t->fa_th[2] = 5000;
		}
	}

	PHYDM_DBG(p_dm, DBG_DIG,
		("FA_th={%d,%d,%d}\n",
		p_dig_t->fa_th[0],
		p_dig_t->fa_th[1],
		p_dig_t->fa_th[2]));

}

void
phydm_set_big_jump_step(
	void			*p_dm_void,
	u8			current_igi
)
{
#if (RTL8822B_SUPPORT == 1 || RTL8197F_SUPPORT == 1)
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_dig_struct	*p_dig_t = &p_dm->dm_dig_table;
	u8		step1[8] = {24, 30, 40, 50, 60, 70, 80, 90};
	u8		i;

	if (p_dig_t->enable_adjust_big_jump == 0)
		return;

	for (i = 0; i <= p_dig_t->big_jump_step1; i++) {
		if ((current_igi + step1[i]) > p_dig_t->big_jump_lmt[p_dig_t->agc_table_idx]) {
			if (i != 0)
				i = i - 1;
			break;
		} else if (i == p_dig_t->big_jump_step1)
			break;
	}
	if (p_dm->support_ic_type & ODM_RTL8822B)
		odm_set_bb_reg(p_dm, 0x8c8, 0xe, i);
	else if (p_dm->support_ic_type & ODM_RTL8197F)
		odm_set_bb_reg(p_dm, ODM_REG_BB_AGC_SET_2_11N, 0xe, i);

	PHYDM_DBG(p_dm, DBG_DIG,
		("phydm_set_big_jump_step(): bigjump = %d (ori = 0x%x), LMT=0x%x\n",
		i, p_dig_t->big_jump_step1, p_dig_t->big_jump_lmt[p_dig_t->agc_table_idx]));
#endif
}

void
odm_write_dig(
	void			*p_dm_void,
	u8			current_igi
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_dig_struct	*p_dig_t = &p_dm->dm_dig_table;
	struct phydm_adaptivity_struct	*p_adaptivity = (struct phydm_adaptivity_struct *)phydm_get_structure(p_dm, PHYDM_ADAPTIVITY);

	PHYDM_DBG(p_dm, DBG_DIG, ("odm_write_dig===>\n"));

	/* 1 Check IGI by upper bound */
	if (p_adaptivity->igi_lmt_en && 
		(current_igi > p_adaptivity->adapt_igi_up) && p_dm->is_linked) {
		
		current_igi = p_adaptivity->adapt_igi_up;

		PHYDM_DBG(p_dm, DBG_DIG,
			("Force to Adaptivity Upper bound=((0x%x))\n", current_igi));
	}

	if (p_dig_t->cur_ig_value != current_igi) {

		#if (RTL8822B_SUPPORT == 1 || RTL8197F_SUPPORT == 1)
		/* Modify big jump step for 8822B and 8197F */
		if (p_dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8197F))
			phydm_set_big_jump_step(p_dm, current_igi);
		#endif

		#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT == 1)
		/* Set IGI value of CCK for new CCK AGC */
		if (p_dm->cck_new_agc && (p_dm->support_ic_type & ODM_IC_PHY_STATUE_NEW_TYPE))
			odm_set_bb_reg(p_dm, 0xa0c, 0x3f00, (current_igi >> 1));
		#endif

		/*Add by YuChen for USB IO too slow issue*/
		if ((p_dm->support_ability & ODM_BB_ADAPTIVITY) && (current_igi > p_dig_t->cur_ig_value)) {
			p_dig_t->cur_ig_value = current_igi;
			phydm_adaptivity(p_dm);
		}

		/* Set IGI value */
		odm_set_bb_reg(p_dm, ODM_REG(IGI_A, p_dm), ODM_BIT(IGI, p_dm), current_igi);

		#if (defined(PHYDM_COMPILE_ABOVE_2SS))
		if (p_dm->support_ic_type & PHYDM_IC_ABOVE_2SS)
			odm_set_bb_reg(p_dm, ODM_REG(IGI_B, p_dm), ODM_BIT(IGI, p_dm), current_igi);
		#endif

		#if (defined(PHYDM_COMPILE_ABOVE_4SS))
		if (p_dm->support_ic_type & PHYDM_IC_ABOVE_4SS) {
			odm_set_bb_reg(p_dm, ODM_REG(IGI_C, p_dm), ODM_BIT(IGI, p_dm), current_igi);
			odm_set_bb_reg(p_dm, ODM_REG(IGI_D, p_dm), ODM_BIT(IGI, p_dm), current_igi);
		}
		#endif
		
		p_dig_t->cur_ig_value = current_igi;
	}

	PHYDM_DBG(p_dm, DBG_DIG, ("New_igi=((0x%x))\n\n", current_igi));
}

void
phydm_set_dig_val(
	void			*p_dm_void,
	u32			*val_buf,
	u8			val_len
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (val_len != 1) {
		PHYDM_DBG(p_dm, ODM_COMP_API, ("[Error][DIG]Need val_len=1\n"));
		return;
	}
	
	odm_write_dig(p_dm, (u8)(*val_buf));
}

void
odm_pause_dig(
	void					*p_dm_void,
	enum phydm_pause_type		pause_type,
	enum phydm_pause_level		pause_level,
	u8					igi_value
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_dig_struct	*p_dig_t = &p_dm->dm_dig_table;
	u8	i = 0;
	s8	max_level;
	
	PHYDM_DBG(p_dm, DBG_DIG, ("%s ======>\n", __func__));

	if ((p_dig_t->pause_lv_bitmap == 0) && 
		(!(p_dm->support_ability & (ODM_BB_DIG | ODM_BB_FA_CNT)))) {
		
		PHYDM_DBG(p_dm, DBG_DIG, ("Return: DIG or FA is disabled\n"));
		return;
	}

	if (pause_level >= PHYDM_PAUSE_MAX_NUM) {
		PHYDM_DBG(p_dm, DBG_DIG, ("Return: Wrong Lv\n"));
		return;
	}

	PHYDM_DBG(p_dm, DBG_DIG, ("Set pause {type, lv, IGI}={%d, %d, 0x%x}\n",
		pause_type, pause_level, igi_value));
	
	for (i = 0; i < PHYDM_PAUSE_MAX_NUM; i ++) {
		PHYDM_DBG(p_dm, DBG_DIG, ("pause val[%d]=0x%x\n", 
										i, p_dig_t->pause_dig_value[i]));
	}

	switch (pause_type) {
	
	case PHYDM_PAUSE:
	{
		PHYDM_DBG(p_dm, DBG_DIG, ("Pause DIG\n"));
		
		p_dm->support_ability &= ~ODM_BB_DIG;
		
		if (p_dig_t->pause_lv_bitmap == 0) {
			p_dig_t->igi_backup = p_dig_t->cur_ig_value; /* Backup IGI value */
			PHYDM_DBG(p_dm, DBG_DIG, ("Backup IGI=0x%x\n", 
														p_dig_t->igi_backup));
		}

		p_dig_t->pause_dig_value[pause_level] = igi_value; /* Record IGI value */
		p_dig_t->pause_lv_bitmap |= BIT(pause_level); /* Update pause level bit-map*/

		if (BIT(pause_level + 1) > p_dig_t->pause_lv_bitmap) {
			PHYDM_DBG(p_dm, DBG_DIG, ("[SUCCESS ]Pause DIG\n"));
			odm_write_dig(p_dm, igi_value);
		} else {
			PHYDM_DBG(p_dm, DBG_DIG, 
				("[FAIL] Pause DIG, pause_bitmap=0x%x\n", p_dig_t->pause_lv_bitmap));
		}
		break;
	}
	
	case PHYDM_RESUME:
	{
		PHYDM_DBG(p_dm, DBG_DIG, ("Resume DIG\n"));
		
		/* check if the level is illegal or not */
		if ((p_dig_t->pause_lv_bitmap & (BIT(pause_level))) != 0) {
			
			p_dig_t->pause_lv_bitmap &= ~(BIT(pause_level));
			p_dig_t->pause_dig_value[pause_level] = 0;
		} else {
		
			PHYDM_DBG(p_dm, DBG_DIG, ("Wrong resume Lv\n"));
			break;
		}

		PHYDM_DBG(p_dm, DBG_DIG, ("Pause_bitmap =0x%x\n", 
													p_dig_t->pause_lv_bitmap));

		/* Resume DIG */
		if (p_dig_t->pause_lv_bitmap == 0) {

			PHYDM_DBG(p_dm, DBG_DIG, ("Revert ori_IGI\n"));
			odm_write_dig(p_dm, p_dig_t->igi_backup); /* Revert IGI*/
			p_dig_t->is_ignore_dig = true;
			p_dm->support_ability |= ODM_BB_DIG;/* Enable DIG */
			break;
		}

		if (BIT(pause_level) > p_dig_t->pause_lv_bitmap) {

			/* Calculate the maximum level now */
			for (max_level = (pause_level - 1); max_level >= 0; max_level--) {
				if (p_dig_t->pause_lv_bitmap & BIT(max_level))
					break;
			}

			/* write IGI to lower level */
			PHYDM_DBG(p_dm, DBG_DIG, ("Pause @ IGI{Lv=0x%x}=%d)\n",
				max_level, p_dig_t->pause_dig_value[max_level]));
			
			odm_write_dig(p_dm, p_dig_t->pause_dig_value[max_level]);

			break;
		}
		break;
	}
	default:
		PHYDM_DBG(p_dm, DBG_DIG, ("Wrong pause  type\n"));
		break;
	}

	PHYDM_DBG(p_dm, DBG_DIG, ("New pause bitmap = 0x%x\n",
		p_dig_t->pause_lv_bitmap));
	
	for (i = 0; i < PHYDM_PAUSE_MAX_NUM; i ++) {
		PHYDM_DBG(p_dm, DBG_DIG, ("pause val[%d]=0x%x\n", 
										i, p_dig_t->pause_dig_value[i]));
	}

}

boolean
odm_dig_abort(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_dig_struct	*p_dig_t = &p_dm->dm_dig_table;
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*p_adapter	= p_dm->adapter;
#endif

	/* support_ability */
	if ((!(p_dm->support_ability & ODM_BB_FA_CNT)) ||
		(!(p_dm->support_ability & ODM_BB_DIG)) ||
		(*(p_dm->p_is_scan_in_process))) {
		PHYDM_DBG(p_dm, DBG_DIG, ("Not Support\n"));
		return true;
	}

	if (p_dm->pause_ability & ODM_BB_DIG) {
		
		PHYDM_DBG(p_dm, DBG_DIG, ("Return: Pause DIG in LV=%d\n", p_dm->pause_lv_table.lv_dig));
		return true;
	}
	
	if (p_dig_t->is_ignore_dig) {
		p_dig_t->is_ignore_dig = false;
		PHYDM_DBG(p_dm, DBG_DIG, ("Return: Ignore DIG\n"));
		return true;
	}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#if OS_WIN_FROM_WIN7(OS_VERSION)
	if (IsAPModeExist(p_adapter) && p_adapter->bInHctTest) {
		PHYDM_DBG(p_dm, DBG_DIG, (" Return: Is AP mode or In HCT Test\n"));
		return true;
	}
#endif
#endif

	return false;
}

void
phydm_dig_init(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_dig_struct		*p_dig_t = &p_dm->dm_dig_table;
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct phydm_fa_struct	*false_alm_cnt = (struct phydm_fa_struct *)phydm_get_structure(p_dm, PHYDM_FALSEALMCNT);
#endif
	u32			ret_value = 0;
	u8			i;

	p_dig_t->dm_dig_max = DIG_MAX_BALANCE_MODE;
	p_dig_t->dm_dig_min = DIG_MIN_PERFORMANCE;
	p_dig_t->dig_max_of_min = DIG_MAX_OF_MIN_BALANCE_MODE;

	p_dig_t->is_ignore_dig = false;
	p_dig_t->cur_ig_value = (u8) odm_get_bb_reg(p_dm, ODM_REG(IGI_A, p_dm), ODM_BIT(IGI, p_dm));
	p_dig_t->is_media_connect = false;

	p_dig_t->fa_th[0] = 250;
	p_dig_t->fa_th[1] = 500;
	p_dig_t->fa_th[2] = 750;
	p_dig_t->is_dbg_fa_th = false;
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	/* For RTL8881A */
	false_alm_cnt->cnt_ofdm_fail_pre = 0;
#endif

	odm_memory_set(p_dm, p_dig_t->pause_dig_value, 0, PHYDM_PAUSE_MAX_NUM);
	p_dig_t->pause_lv_bitmap = 0;

	p_dig_t->rx_gain_range_max = DIG_MAX_BALANCE_MODE;
	p_dig_t->rx_gain_range_min = p_dig_t->cur_ig_value;

#if (RTL8822B_SUPPORT == 1 || RTL8197F_SUPPORT == 1)
	p_dig_t->enable_adjust_big_jump = 1;
	if (p_dm->support_ic_type & ODM_RTL8822B)
		ret_value = odm_get_bb_reg(p_dm, 0x8c8, MASKLWORD);
	else if (p_dm->support_ic_type & ODM_RTL8197F)
		ret_value = odm_get_bb_reg(p_dm, 0xc74, MASKLWORD);

	p_dig_t->big_jump_step1 = (u8)(ret_value & 0xe) >> 1;
	p_dig_t->big_jump_step2 = (u8)(ret_value & 0x30) >> 4;
	p_dig_t->big_jump_step3 = (u8)(ret_value & 0xc0) >> 6;

	if (p_dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8197F)) {
		for (i = 0; i < sizeof(p_dig_t->big_jump_lmt); i++) {
			if (p_dig_t->big_jump_lmt[i] == 0)
				p_dig_t->big_jump_lmt[i] = 0x64;		/* Set -10dBm as default value */
		}
	}
#endif

	p_dm->pre_rssi_min = 0;

#ifdef PHYDM_TDMA_DIG_SUPPORT
	p_dm->original_dig_restore = 1;
#endif
}

boolean
phydm_dig_performance_mode_decision(
	struct PHY_DM_STRUCT		*p_dm
)
{
	boolean	is_performance = true;

#ifdef PHYDM_DIG_MODE_DECISION_SUPPORT
	struct phydm_dig_struct	*p_dig_t = &p_dm->dm_dig_table;

	switch (p_dig_t->dig_mode_decision) {
	case PHYDM_DIG_PERFORAMNCE_MODE:
		is_performance = true;
		break;
	case PHYDM_DIG_COVERAGE_MODE:
		is_performance = false;
		break;
	default:
		is_performance = true;
		break;
	}
#endif

	return is_performance;
}

void
phydm_dig_abs_boundary_decision(
	struct PHY_DM_STRUCT		*p_dm,
	boolean	is_performance,
	boolean	is_dfs_band
)
{
	struct phydm_dig_struct	*p_dig_t = &p_dm->dm_dig_table;

	if (!p_dm->is_linked) {
		p_dig_t->dm_dig_max = DIG_MAX_COVERAGR;
		p_dig_t->dm_dig_min = DIG_MIN_COVERAGE;
	} else if (is_dfs_band == true) {
		if (*p_dm->p_band_width == CHANNEL_WIDTH_20)
			p_dig_t->dm_dig_min = DIG_MIN_DFS + 2;
		else
			p_dig_t->dm_dig_min = DIG_MIN_DFS;

		p_dig_t->dig_max_of_min = DIG_MAX_OF_MIN_BALANCE_MODE;
		p_dig_t->dm_dig_max = DIG_MAX_BALANCE_MODE;

	} else if (!is_performance) {
		p_dig_t->dm_dig_max = DIG_MAX_COVERAGR;
		p_dig_t->dm_dig_min = DIG_MIN_COVERAGE;
		#if (DIG_HW == 1)
		p_dig_t->dig_max_of_min = DIG_MIN_COVERAGE;
		#else
		p_dig_t->dig_max_of_min = DIG_MAX_OF_MIN_COVERAGE;
		#endif
	} else {
		if (*p_dm->p_bb_op_mode == PHYDM_BALANCE_MODE) {	/*service > 2 devices*/
			p_dig_t->dm_dig_max = DIG_MAX_BALANCE_MODE;
			#if (DIG_HW == 1)
			p_dig_t->dig_max_of_min = DIG_MIN_COVERAGE;
			#else
			p_dig_t->dig_max_of_min = DIG_MAX_OF_MIN_BALANCE_MODE;
			#endif
		} else if (*p_dm->p_bb_op_mode == PHYDM_PERFORMANCE_MODE) {	/*service 1 devices*/
			p_dig_t->dm_dig_max = DIG_MAX_PERFORMANCE_MODE;
			p_dig_t->dig_max_of_min = DIG_MAX_OF_MIN_PERFORMANCE_MODE;
		}

		if (p_dm->support_ic_type &
			(ODM_RTL8814A | ODM_RTL8812 | ODM_RTL8821 | ODM_RTL8822B))
			p_dig_t->dm_dig_min = 0x1c;
		else if (p_dm->support_ic_type & ODM_RTL8197F)
			p_dig_t->dm_dig_min = 0x1e;		/*For HW setting*/
		else
			p_dig_t->dm_dig_min = DIG_MIN_PERFORMANCE;
	}

	PHYDM_DBG(p_dm, DBG_DIG,
		("Abs-bound{Max, Min}={0x%x, 0x%x}, Max_of_min =  0x%x\n",
		p_dig_t->dm_dig_max,
		p_dig_t->dm_dig_min,
		p_dig_t->dig_max_of_min));

}

void
phydm_dig_dym_boundary_decision(
	struct PHY_DM_STRUCT		*p_dm,
	boolean	is_performance
)
{
	struct phydm_dig_struct	*p_dig_t = &p_dm->dm_dig_table;
	u8 offset = 15, tmp_max = 0;

	PHYDM_DBG(p_dm, DBG_DIG,
			("Offset=((%d))\n", offset));

	/* DIG lower bound */
	if (p_dm->rssi_min > p_dig_t->dig_max_of_min)
		p_dig_t->rx_gain_range_min = p_dig_t->dig_max_of_min;
	else if (p_dm->rssi_min < p_dig_t->dm_dig_min)
		p_dig_t->rx_gain_range_min = p_dig_t->dm_dig_min;
	else
		p_dig_t->rx_gain_range_min = p_dm->rssi_min;

	/* DIG upper bound */
	tmp_max = p_dig_t->rx_gain_range_min + offset;

	if (tmp_max > p_dig_t->dm_dig_max)
		p_dig_t->rx_gain_range_max = p_dig_t->dm_dig_max;
	else
		p_dig_t->rx_gain_range_max = tmp_max;

	/* 1 Force Lower Bound for AntDiv */
	if (!p_dm->is_one_entry_only) {
		if ((p_dm->support_ic_type & ODM_ANTDIV_SUPPORT) && (p_dm->support_ability & ODM_BB_ANT_DIV)) {
			if (p_dm->ant_div_type == CG_TRX_HW_ANTDIV || p_dm->ant_div_type == CG_TRX_SMART_ANTDIV) {
				if (p_dig_t->ant_div_rssi_max > p_dig_t->dig_max_of_min)
					p_dig_t->rx_gain_range_min = p_dig_t->dig_max_of_min;
				else
					p_dig_t->rx_gain_range_min = (u8)p_dig_t->ant_div_rssi_max;
				
				PHYDM_DBG(p_dm, DBG_DIG,
					("AntDiv: Force Dyn-Min = 0x%x, RSSI_max = 0x%x\n",
					p_dig_t->rx_gain_range_min, p_dig_t->ant_div_rssi_max));
			}
		}
	}
	PHYDM_DBG(p_dm, DBG_DIG,
		("Dym-bound{Max, Min}={0x%x, 0x%x}\n",
		p_dig_t->rx_gain_range_max, p_dig_t->rx_gain_range_min));
}

void
phydm_dig_abnormal_case(
	struct PHY_DM_STRUCT		*p_dm,
	u8	current_igi,
	boolean	is_performance,
	boolean	is_dfs_band
)
{
	struct phydm_dig_struct	*p_dig_t = &p_dm->dm_dig_table;
	boolean	first_connect = false, first_dis_connect = false;

	first_connect = (p_dm->is_linked) && (p_dig_t->is_media_connect == false);
	first_dis_connect = (!p_dm->is_linked) && (p_dig_t->is_media_connect == true);

	/* Modify DIG lower bound, deal with abnormal case */
	if (!p_dm->is_linked && is_dfs_band && is_performance) {
		p_dig_t->rx_gain_range_max = DIG_MAX_DFS;
		PHYDM_DBG(p_dm, DBG_DIG,
			("DFS band: Force max to 0x%x before link\n", p_dig_t->rx_gain_range_max));
	}

	if (is_dfs_band)
		p_dig_t->rx_gain_range_min = p_dig_t->dm_dig_min;

	/* Abnormal lower bound case */
	if (p_dig_t->rx_gain_range_min > p_dig_t->rx_gain_range_max)
		p_dig_t->rx_gain_range_min = p_dig_t->rx_gain_range_max;

	PHYDM_DBG(p_dm, DBG_DIG,
		("Abnoraml checked {Max, Min}={0x%x, 0x%x}\n",
		p_dig_t->rx_gain_range_max, p_dig_t->rx_gain_range_min));

}

u8
phydm_dig_current_igi_by_fa_th(
	struct PHY_DM_STRUCT		*p_dm,
	u8			current_igi,
	u32			false_alm_cnt,
	u8			*step_size
)
{
	boolean	dig_go_up_check = true;
	struct phydm_dig_struct	*p_dig_t = &p_dm->dm_dig_table;
	
	dig_go_up_check = phydm_dig_go_up_check(p_dm);

	if ((false_alm_cnt > p_dig_t->fa_th[2]) && dig_go_up_check)
		current_igi = current_igi + step_size[0];
	else if ((false_alm_cnt > p_dig_t->fa_th[1]) && dig_go_up_check)
		current_igi = current_igi + step_size[1];
	else if (false_alm_cnt < p_dig_t->fa_th[0])
		current_igi = current_igi - step_size[2];

	return current_igi;

}

u8
phydm_dig_igi_start_value(
	struct PHY_DM_STRUCT		*p_dm,
	boolean	is_performance,
	u8		current_igi,
	u32		false_alm_cnt,
	boolean	is_dfs_band
)
{
	struct phydm_dig_struct	*p_dig_t = &p_dm->dm_dig_table;
	u8		step_size[3] = {0};
	boolean	first_connect = false, first_dis_connect = false;

	first_connect = (p_dm->is_linked) && (p_dig_t->is_media_connect == false);
	first_dis_connect = (!p_dm->is_linked) && (p_dig_t->is_media_connect == true);

	if (p_dm->is_linked) {
		if (p_dm->pre_rssi_min <= p_dm->rssi_min) {
			step_size[0] = 2;
			step_size[1] = 1;
			step_size[2] = 2;
		} else {
			step_size[0] = 4;
			step_size[1] = 2;
			step_size[2] = 2;
		}
		p_dm->pre_rssi_min = p_dm->rssi_min;
	} else {
		step_size[0] = 2;
		step_size[1] = 1;
		step_size[2] = 2;
	}
	
	PHYDM_DBG(p_dm, DBG_DIG,
		("step_size = {-%d,  +%d, +%d}\n", step_size[2], step_size[1], step_size[0]));

	PHYDM_DBG(p_dm, DBG_DIG,
		("rssi_min = %d, pre_rssi_min = %d\n", p_dm->rssi_min, p_dm->pre_rssi_min));

	if (p_dm->is_linked && is_performance) {
		/* 2 After link */
		PHYDM_DBG(p_dm, DBG_DIG, ("Adjust IGI after link\n"));

		if (first_connect && is_performance) {

			if (is_dfs_band) {
				if (p_dm->rssi_min > DIG_MAX_DFS)
					current_igi = DIG_MAX_DFS;
				else
					current_igi = p_dm->rssi_min;
				PHYDM_DBG(p_dm, DBG_DIG,
					("DFS band: one shot IGI to 0x%x most\n", p_dig_t->rx_gain_range_max));
			} else
				current_igi = p_dig_t->rx_gain_range_min;
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (RTL8812A_SUPPORT == 1)
			if (p_dm->support_ic_type == ODM_RTL8812)
				odm_config_bb_with_header_file(p_dm, CONFIG_BB_AGC_TAB_DIFF);
#endif
#endif
			PHYDM_DBG(p_dm, DBG_DIG,
				("First connect case: IGI does on-shot to 0x%x\n", current_igi));
		} else {

			/* 4 Abnormal # beacon case */
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))		
			if ((p_dm->phy_dbg_info.num_qry_beacon_pkt < 5) &&
				(false_alm_cnt < DM_DIG_FA_TH1) && (p_dm->bsta_state)) {
				if (p_dm->support_ic_type != ODM_RTL8723D) {
					p_dig_t->rx_gain_range_min = 0x1c;
					current_igi = p_dig_t->rx_gain_range_min;
					PHYDM_DBG(p_dm, DBG_DIG,
						("Abnormal #beacon (%d) case: IGI does one-shot to 0x%x\n",
						p_dm->phy_dbg_info.num_qry_beacon_pkt, current_igi));
				}
			} else
#endif
				current_igi = phydm_dig_current_igi_by_fa_th(p_dm,
						current_igi, false_alm_cnt, step_size);
		}
	} else {
		/* 2 Before link */
		PHYDM_DBG(p_dm, DBG_DIG, ("Adjust IGI before link\n"));

		if (first_dis_connect) {
			current_igi = p_dig_t->dm_dig_min;
			PHYDM_DBG(p_dm, DBG_DIG, ("First disconnect case: IGI does on-shot to lower bound\n"));
		} else {
			PHYDM_DBG(p_dm, DBG_DIG,
				("Pre_IGI=((0x%x)), FA=((%d))\n", current_igi, false_alm_cnt));

			current_igi = phydm_dig_current_igi_by_fa_th(p_dm,
						current_igi, false_alm_cnt, step_size);
		}
	}

	return current_igi;

}

void
phydm_dig(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_dig_struct	*p_dig_t = &p_dm->dm_dig_table;
	struct phydm_fa_struct		*p_falm_cnt = &p_dm->false_alm_cnt;
#ifdef PHYDM_TDMA_DIG_SUPPORT
	struct phydm_fa_acc_struct *p_falm_cnt_acc = &p_dm->false_alm_cnt_acc;
#endif
	boolean		first_connect, first_dis_connect;
	u8			current_igi = p_dig_t->cur_ig_value;
	u32			false_alm_cnt= p_falm_cnt->cnt_all;
	boolean		is_dfs_band = false, is_performance = true;

#ifdef PHYDM_TDMA_DIG_SUPPORT
	if (p_dm->original_dig_restore == 0) {
		if (p_dig_t->cur_ig_value_tdma == 0)
			p_dig_t->cur_ig_value_tdma = p_dig_t->cur_ig_value;
		
		current_igi = p_dig_t->cur_ig_value_tdma;
		false_alm_cnt = p_falm_cnt_acc->cnt_all_1sec;
	}
#endif

	if (odm_dig_abort(p_dm) == true)
		return;

	PHYDM_DBG(p_dm, DBG_DIG, ("%s Start===>\n", __func__));

	/* 1 Update status */
	first_connect = (p_dm->is_linked) && (p_dig_t->is_media_connect == false);
	first_dis_connect = (!p_dm->is_linked) && (p_dig_t->is_media_connect == true);

	PHYDM_DBG(p_dm, DBG_DIG,
		("is_linked = %d, RSSI = %d, 1stConnect = %d, 1stDisconnect = %d\n",
		p_dm->is_linked, p_dm->rssi_min, first_connect, first_dis_connect));

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP | ODM_CE))
	/* Modify lower bound for DFS band */
	if (p_dm->is_dfs_band) {
		#if (DM_ODM_SUPPORT_TYPE & (ODM_CE))
		if (phydm_dfs_master_enabled(p_dm))
		#endif
			is_dfs_band = true;
		
		PHYDM_DBG(p_dm, DBG_DIG, ("In DFS band\n"));
	}
#endif

	is_performance = phydm_dig_performance_mode_decision(p_dm);
	PHYDM_DBG(p_dm, DBG_DIG,
		("DIG ((%s)) mode\n", (is_performance ? "Performance" : "Coverage")));

	/* Boundary Decision */
	phydm_dig_abs_boundary_decision(p_dm, is_performance, is_dfs_band);

	/*init dym boundary*/
	p_dig_t->rx_gain_range_max = p_dig_t->dig_max_of_min;	/*if no link, always stay at lower bound*/
	p_dig_t->rx_gain_range_min = p_dig_t->dm_dig_min;

	/* Adjust boundary by RSSI */
	if (p_dm->is_linked)
		phydm_dig_dym_boundary_decision(p_dm, is_performance);

	/*Abnormal case check*/
	phydm_dig_abnormal_case(p_dm, current_igi, is_performance, is_dfs_band);

	/* False alarm threshold decision */
	odm_fa_threshold_check(p_dm, is_dfs_band, is_performance);

	/* 1 Adjust initial gain by false alarm */
	current_igi = phydm_dig_igi_start_value(p_dm,
		is_performance, current_igi, false_alm_cnt, is_dfs_band);

	/* 1 Check initial gain by upper/lower bound */
	if (current_igi < p_dig_t->rx_gain_range_min)
		current_igi = p_dig_t->rx_gain_range_min;

	if (current_igi > p_dig_t->rx_gain_range_max)
		current_igi = p_dig_t->rx_gain_range_max;

	PHYDM_DBG(p_dm, DBG_DIG, ("New_IGI=((0x%x))\n", current_igi));

	/* 1 Update status */
#ifdef PHYDM_TDMA_DIG_SUPPORT
	if (p_dm->original_dig_restore == 0) {

		p_dig_t->cur_ig_value_tdma = current_igi;
		/*It is possible fa_acc_1sec_tsf >= */
		/*1sec while tdma_dig_state == 0*/
		if (p_dig_t->tdma_dig_state != 0)
			odm_write_dig(p_dm, p_dig_t->cur_ig_value_tdma);
	} else
#endif 
		odm_write_dig(p_dm, current_igi);

	p_dig_t->is_media_connect = p_dm->is_linked;
	
	PHYDM_DBG(p_dm, DBG_DIG, ("DIG end\n"));
}

void
phydm_dig_lps_32k(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8	current_igi = p_dm->rssi_min;


	odm_write_dig(p_dm, current_igi);
}

void
phydm_dig_by_rssi_lps(
	void		*p_dm_void
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_fa_struct	*p_falm_cnt;

	u8	rssi_lower = DIG_MIN_LPS; /* 0x1E or 0x1C */
	u8	current_igi = p_dm->rssi_min;

	p_falm_cnt = &p_dm->false_alm_cnt;
	if (odm_dig_abort(p_dm) == true)
		return;

	current_igi = current_igi + RSSI_OFFSET_DIG_LPS;
	PHYDM_DBG(p_dm, DBG_DIG, ("%s==>\n", __func__));

	/* Using FW PS mode to make IGI */
	/* Adjust by  FA in LPS MODE */
	if (p_falm_cnt->cnt_all > DM_DIG_FA_TH2_LPS)
		current_igi = current_igi + 4;
	else if (p_falm_cnt->cnt_all > DM_DIG_FA_TH1_LPS)
		current_igi = current_igi + 2;
	else if (p_falm_cnt->cnt_all < DM_DIG_FA_TH0_LPS)
		current_igi = current_igi - 2;


	/* Lower bound checking */

	/* RSSI Lower bound check */
	if ((p_dm->rssi_min - 10) > DIG_MIN_LPS)
		rssi_lower = (p_dm->rssi_min - 10);
	else
		rssi_lower = DIG_MIN_LPS;

	/* Upper and Lower Bound checking */
	if (current_igi > DIG_MAX_LPS)
		current_igi = DIG_MAX_LPS;
	else if (current_igi < rssi_lower)
		current_igi = rssi_lower;

	PHYDM_DBG(p_dm, DBG_DIG,
		("%s p_falm_cnt->cnt_all = %d\n", __func__,
		p_falm_cnt->cnt_all));
	PHYDM_DBG(p_dm, DBG_DIG,
		("%s p_dm->rssi_min = %d\n", __func__,
		p_dm->rssi_min));
	PHYDM_DBG(p_dm, DBG_DIG,
		("%s current_igi = 0x%x\n", __func__,
		current_igi));

	/* odm_write_dig(p_dm, p_dig_t->cur_ig_value); */
	odm_write_dig(p_dm, current_igi);
#endif
}

/* 3============================================================
 * 3 FASLE ALARM CHECK
 * 3============================================================ */
void
phydm_false_alarm_counter_reg_reset(
	void					*p_dm_void
)
{
	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_dig_struct	*p_dig_t = &p_dm->dm_dig_table;
	struct phydm_fa_struct *p_falm_cnt = &p_dm->false_alm_cnt;
#ifdef PHYDM_TDMA_DIG_SUPPORT
	struct phydm_fa_acc_struct *p_falm_cnt_acc = &p_dm->false_alm_cnt_acc;
#endif
	u32	false_alm_cnt;

#ifdef PHYDM_TDMA_DIG_SUPPORT
	if (p_dm->original_dig_restore == 0) {

		if (p_dig_t->cur_ig_value_tdma == 0)
			p_dig_t->cur_ig_value_tdma = p_dig_t->cur_ig_value;

		false_alm_cnt = p_falm_cnt_acc->cnt_all_1sec;
	} else 
#endif
	{
		false_alm_cnt = p_falm_cnt->cnt_all;
	}

#if (ODM_IC_11N_SERIES_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {
		/*reset false alarm counter registers*/
		odm_set_bb_reg(p_dm, 0xC0C, BIT(31), 1);
		odm_set_bb_reg(p_dm, 0xC0C, BIT(31), 0);
		odm_set_bb_reg(p_dm, 0xD00, BIT(27), 1);
		odm_set_bb_reg(p_dm, 0xD00, BIT(27), 0);

		/*update ofdm counter*/
		/*update page C counter*/
		odm_set_bb_reg(p_dm, 0xD00, BIT(31), 0);
		/*update page D counter*/
		odm_set_bb_reg(p_dm, 0xD00, BIT(31), 0);

		/*reset CCK CCA counter*/
		odm_set_bb_reg(p_dm, 0xA2C, BIT(13) | BIT(12), 0);
		odm_set_bb_reg(p_dm, 0xA2C, BIT(13) | BIT(12), 2);

		/*reset CCK FA counter*/
		odm_set_bb_reg(p_dm, 0xA2C, BIT(15) | BIT(14), 0);
		odm_set_bb_reg(p_dm, 0xA2C, BIT(15) | BIT(14), 2);

		/*reset CRC32 counter*/
		odm_set_bb_reg(p_dm, 0xF14, BIT(16), 1);
		odm_set_bb_reg(p_dm, 0xF14, BIT(16), 0);
	}
#endif	/* #if (ODM_IC_11N_SERIES_SUPPORT == 1) */

#if (ODM_IC_11AC_SERIES_SUPPORT == 1)
		if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
	#if (RTL8881A_SUPPORT == 1)
			/* Reset FA counter by enable/disable OFDM */
			if (false_alm_cnt->cnt_ofdm_fail_pre >= 0x7fff) {
				/* reset OFDM */
				odm_set_bb_reg(p_dm, 0x808, BIT(29), 0);
				odm_set_bb_reg(p_dm, 0x808, BIT(29), 1);
				false_alm_cnt->cnt_ofdm_fail_pre = 0;
				PHYDM_DBG(p_dm, DBG_FA_CNT, ("Reset FA_cnt\n"));
			}
	#endif	/* #if (RTL8881A_SUPPORT == 1) */
			/* reset OFDM FA countner */
			odm_set_bb_reg(p_dm, 0x9A4, BIT(17), 1);
			odm_set_bb_reg(p_dm, 0x9A4, BIT(17), 0);

			/* reset CCK FA counter */
			odm_set_bb_reg(p_dm, 0xA2C, BIT(15), 0);
			odm_set_bb_reg(p_dm, 0xA2C, BIT(15), 1);

			/* reset CCA counter */
			odm_set_bb_reg(p_dm, 0xB58, BIT(0), 1);
			odm_set_bb_reg(p_dm, 0xB58, BIT(0), 0);
		}
#endif	/* #if (ODM_IC_11AC_SERIES_SUPPORT == 1) */
}

void
phydm_false_alarm_counter_reg_hold(
	void					*p_dm_void
)
{
	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {
		/*hold ofdm counter*/
		/*hold page C counter*/
		odm_set_bb_reg(p_dm, 0xC00, BIT(31), 1);
		/*hold page D counter*/
		odm_set_bb_reg(p_dm, 0xD00, BIT(31), 1);

		//hold cck counter
		odm_set_bb_reg(p_dm, 0xA2C, BIT(12), 1);
		odm_set_bb_reg(p_dm, 0xA2C, BIT(14), 1);
	}
}

void
odm_false_alarm_counter_statistics(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT					*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_fa_struct	*false_alm_cnt = (struct phydm_fa_struct *)phydm_get_structure(p_dm, PHYDM_FALSEALMCNT);
	struct phydm_adaptivity_struct	*adaptivity = (struct phydm_adaptivity_struct *)phydm_get_structure(p_dm, PHYDM_ADAPTIVITY);
	u32						ret_value;

	if (!(p_dm->support_ability & ODM_BB_FA_CNT))
		return;

	PHYDM_DBG(p_dm, DBG_FA_CNT, ("FA_Counter()======>\n"));

#if (ODM_IC_11N_SERIES_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {

		/* hold ofdm & cck counter */
		phydm_false_alarm_counter_reg_hold(p_dm);

		ret_value = odm_get_bb_reg(p_dm, ODM_REG_OFDM_FA_TYPE1_11N, MASKDWORD);
		false_alm_cnt->cnt_fast_fsync = (ret_value & 0xffff);
		false_alm_cnt->cnt_sb_search_fail = ((ret_value & 0xffff0000) >> 16);

		ret_value = odm_get_bb_reg(p_dm, ODM_REG_OFDM_FA_TYPE2_11N, MASKDWORD);
		false_alm_cnt->cnt_ofdm_cca = (ret_value & 0xffff);
		false_alm_cnt->cnt_parity_fail = ((ret_value & 0xffff0000) >> 16);

		ret_value = odm_get_bb_reg(p_dm, ODM_REG_OFDM_FA_TYPE3_11N, MASKDWORD);
		false_alm_cnt->cnt_rate_illegal = (ret_value & 0xffff);
		false_alm_cnt->cnt_crc8_fail = ((ret_value & 0xffff0000) >> 16);

		ret_value = odm_get_bb_reg(p_dm, ODM_REG_OFDM_FA_TYPE4_11N, MASKDWORD);
		false_alm_cnt->cnt_mcs_fail = (ret_value & 0xffff);

		false_alm_cnt->cnt_ofdm_fail =
			false_alm_cnt->cnt_parity_fail + false_alm_cnt->cnt_rate_illegal +
			false_alm_cnt->cnt_crc8_fail + false_alm_cnt->cnt_mcs_fail +
			false_alm_cnt->cnt_fast_fsync + false_alm_cnt->cnt_sb_search_fail;

		/* read CCK CRC32 counter */
		false_alm_cnt->cnt_cck_crc32_error = odm_get_bb_reg(p_dm, ODM_REG_CCK_CRC32_ERROR_CNT_11N, MASKDWORD);
		false_alm_cnt->cnt_cck_crc32_ok = odm_get_bb_reg(p_dm, ODM_REG_CCK_CRC32_OK_CNT_11N, MASKDWORD);

		/* read OFDM CRC32 counter */
		ret_value = odm_get_bb_reg(p_dm, ODM_REG_OFDM_CRC32_CNT_11N, MASKDWORD);
		false_alm_cnt->cnt_ofdm_crc32_error = (ret_value & 0xffff0000) >> 16;
		false_alm_cnt->cnt_ofdm_crc32_ok = ret_value & 0xffff;

		/* read HT CRC32 counter */
		ret_value = odm_get_bb_reg(p_dm, ODM_REG_HT_CRC32_CNT_11N, MASKDWORD);
		false_alm_cnt->cnt_ht_crc32_error = (ret_value & 0xffff0000) >> 16;
		false_alm_cnt->cnt_ht_crc32_ok = ret_value & 0xffff;

		/* read VHT CRC32 counter */
		false_alm_cnt->cnt_vht_crc32_error = 0;
		false_alm_cnt->cnt_vht_crc32_ok = 0;
		
#if (RTL8723D_SUPPORT == 1)
		if (p_dm->support_ic_type == ODM_RTL8723D) {
			/* read HT CRC32 agg counter */
			ret_value = odm_get_bb_reg(p_dm, ODM_REG_HT_CRC32_CNT_11N_AGG, MASKDWORD);
			false_alm_cnt->cnt_ht_crc32_error_agg = (ret_value & 0xffff0000) >> 16;
			false_alm_cnt->cnt_ht_crc32_ok_agg= ret_value & 0xffff;
		}
#endif
		
#if (RTL8188E_SUPPORT == 1)
		if (p_dm->support_ic_type == ODM_RTL8188E) {
			ret_value = odm_get_bb_reg(p_dm, ODM_REG_SC_CNT_11N, MASKDWORD);
			false_alm_cnt->cnt_bw_lsc = (ret_value & 0xffff);
			false_alm_cnt->cnt_bw_usc = ((ret_value & 0xffff0000) >> 16);
		}
#endif

		{
			ret_value = odm_get_bb_reg(p_dm, ODM_REG_CCK_FA_LSB_11N, MASKBYTE0);
			false_alm_cnt->cnt_cck_fail = ret_value;

			ret_value = odm_get_bb_reg(p_dm, ODM_REG_CCK_FA_MSB_11N, MASKBYTE3);
			false_alm_cnt->cnt_cck_fail += (ret_value & 0xff) << 8;

			ret_value = odm_get_bb_reg(p_dm, ODM_REG_CCK_CCA_CNT_11N, MASKDWORD);
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

		PHYDM_DBG(p_dm, DBG_FA_CNT,
			("[OFDM FA Detail] Parity_Fail = (( %d )), Rate_Illegal = (( %d )), CRC8_fail = (( %d )), Mcs_fail = (( %d )), Fast_Fsync = (( %d )), SB_Search_fail = (( %d ))\n",
			false_alm_cnt->cnt_parity_fail, false_alm_cnt->cnt_rate_illegal, false_alm_cnt->cnt_crc8_fail, false_alm_cnt->cnt_mcs_fail, false_alm_cnt->cnt_fast_fsync, false_alm_cnt->cnt_sb_search_fail));
		
	}
#endif

#if (ODM_IC_11AC_SERIES_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
		u32 cck_enable;

		/* read OFDM FA counter */
		false_alm_cnt->cnt_ofdm_fail = odm_get_bb_reg(p_dm, ODM_REG_OFDM_FA_11AC, MASKLWORD);

		/* Read CCK FA counter */
		false_alm_cnt->cnt_cck_fail = odm_get_bb_reg(p_dm, ODM_REG_CCK_FA_11AC, MASKLWORD);

		/* read CCK/OFDM CCA counter */
		ret_value = odm_get_bb_reg(p_dm, ODM_REG_CCK_CCA_CNT_11AC, MASKDWORD);
		false_alm_cnt->cnt_ofdm_cca = (ret_value & 0xffff0000) >> 16;
		false_alm_cnt->cnt_cck_cca = ret_value & 0xffff;

		/* read CCK CRC32 counter */
		ret_value = odm_get_bb_reg(p_dm, ODM_REG_CCK_CRC32_CNT_11AC, MASKDWORD);
		false_alm_cnt->cnt_cck_crc32_error = (ret_value & 0xffff0000) >> 16;
		false_alm_cnt->cnt_cck_crc32_ok = ret_value & 0xffff;

		/* read OFDM CRC32 counter */
		ret_value = odm_get_bb_reg(p_dm, ODM_REG_OFDM_CRC32_CNT_11AC, MASKDWORD);
		false_alm_cnt->cnt_ofdm_crc32_error = (ret_value & 0xffff0000) >> 16;
		false_alm_cnt->cnt_ofdm_crc32_ok = ret_value & 0xffff;

		/* read HT CRC32 counter */
		ret_value = odm_get_bb_reg(p_dm, ODM_REG_HT_CRC32_CNT_11AC, MASKDWORD);
		false_alm_cnt->cnt_ht_crc32_error = (ret_value & 0xffff0000) >> 16;
		false_alm_cnt->cnt_ht_crc32_ok = ret_value & 0xffff;

		/* read VHT CRC32 counter */
		ret_value = odm_get_bb_reg(p_dm, ODM_REG_VHT_CRC32_CNT_11AC, MASKDWORD);
		false_alm_cnt->cnt_vht_crc32_error = (ret_value & 0xffff0000) >> 16;
		false_alm_cnt->cnt_vht_crc32_ok = ret_value & 0xffff;

#if (RTL8881A_SUPPORT == 1)
		/* For 8881A */
		if (p_dm->support_ic_type == ODM_RTL8881A) {
			u32 cnt_ofdm_fail_temp = 0;

			if (false_alm_cnt->cnt_ofdm_fail >= false_alm_cnt->cnt_ofdm_fail_pre) {
				cnt_ofdm_fail_temp = false_alm_cnt->cnt_ofdm_fail_pre;
				false_alm_cnt->cnt_ofdm_fail_pre = false_alm_cnt->cnt_ofdm_fail;
				false_alm_cnt->cnt_ofdm_fail = false_alm_cnt->cnt_ofdm_fail - cnt_ofdm_fail_temp;
			} else
				false_alm_cnt->cnt_ofdm_fail_pre = false_alm_cnt->cnt_ofdm_fail;
			PHYDM_DBG(p_dm, DBG_FA_CNT, ("odm_false_alarm_counter_statistics(): cnt_ofdm_fail=%d\n",	false_alm_cnt->cnt_ofdm_fail_pre));
			PHYDM_DBG(p_dm, DBG_FA_CNT, ("odm_false_alarm_counter_statistics(): cnt_ofdm_fail_pre=%d\n",	cnt_ofdm_fail_temp));
		}
#endif
		cck_enable =  odm_get_bb_reg(p_dm, ODM_REG_BB_RX_PATH_11AC, BIT(28));
		if (cck_enable) { /* if(*p_dm->p_band_type == ODM_BAND_2_4G) */
			false_alm_cnt->cnt_all = false_alm_cnt->cnt_ofdm_fail + false_alm_cnt->cnt_cck_fail;
			false_alm_cnt->cnt_cca_all = false_alm_cnt->cnt_cck_cca + false_alm_cnt->cnt_ofdm_cca;
		} else {
			false_alm_cnt->cnt_all = false_alm_cnt->cnt_ofdm_fail;
			false_alm_cnt->cnt_cca_all = false_alm_cnt->cnt_ofdm_cca;
		}
	}
#endif

#if (RTL8723D_SUPPORT == 1)
	if (p_dm->support_ic_type != ODM_RTL8723D) {
		if (phydm_set_bb_dbg_port(p_dm, BB_DBGPORT_PRIORITY_1, 0x0)) {/*set debug port to 0x0*/
			false_alm_cnt->dbg_port0 = phydm_get_bb_dbg_port_value(p_dm);
			phydm_release_bb_dbg_port(p_dm);
		}

		if (phydm_set_bb_dbg_port(p_dm, BB_DBGPORT_PRIORITY_1, adaptivity->adaptivity_dbg_port)) {
			if (p_dm->support_ic_type & (ODM_RTL8723B | ODM_RTL8188E))
				false_alm_cnt->edcca_flag = (boolean)((phydm_get_bb_dbg_port_value(p_dm) & BIT(30)) >> 30);
			else
				false_alm_cnt->edcca_flag = (boolean)((phydm_get_bb_dbg_port_value(p_dm) & BIT(29)) >> 29);
			phydm_release_bb_dbg_port(p_dm);
		}
	}
#endif

	phydm_false_alarm_counter_reg_reset(p_dm_void);

	false_alm_cnt->cnt_crc32_error_all = false_alm_cnt->cnt_vht_crc32_error + false_alm_cnt->cnt_ht_crc32_error + false_alm_cnt->cnt_ofdm_crc32_error + false_alm_cnt->cnt_cck_crc32_error;
	false_alm_cnt->cnt_crc32_ok_all = false_alm_cnt->cnt_vht_crc32_ok + false_alm_cnt->cnt_ht_crc32_ok + false_alm_cnt->cnt_ofdm_crc32_ok + false_alm_cnt->cnt_cck_crc32_ok;

	PHYDM_DBG(p_dm, DBG_FA_CNT, ("[CCA Cnt] {CCK, OFDM, Total} = {%d, %d, %d}\n",
		false_alm_cnt->cnt_cck_cca, false_alm_cnt->cnt_ofdm_cca, false_alm_cnt->cnt_cca_all));

	PHYDM_DBG(p_dm, DBG_FA_CNT, ("[FA Cnt] {CCK, OFDM, Total} = {%d, %d, %d}\n",
		false_alm_cnt->cnt_cck_fail, false_alm_cnt->cnt_ofdm_fail, false_alm_cnt->cnt_all));

	PHYDM_DBG(p_dm, DBG_FA_CNT, ("[CCK]  CRC32 {error, ok}= {%d, %d}\n", false_alm_cnt->cnt_cck_crc32_error, false_alm_cnt->cnt_cck_crc32_ok));
	PHYDM_DBG(p_dm, DBG_FA_CNT, ("[OFDM]CRC32 {error, ok}= {%d, %d}\n", false_alm_cnt->cnt_ofdm_crc32_error, false_alm_cnt->cnt_ofdm_crc32_ok));
	PHYDM_DBG(p_dm, DBG_FA_CNT, ("[ HT ]  CRC32 {error, ok}= {%d, %d}\n", false_alm_cnt->cnt_ht_crc32_error, false_alm_cnt->cnt_ht_crc32_ok));
	PHYDM_DBG(p_dm, DBG_FA_CNT, ("[VHT]  CRC32 {error, ok}= {%d, %d}\n", false_alm_cnt->cnt_vht_crc32_error, false_alm_cnt->cnt_vht_crc32_ok));
	PHYDM_DBG(p_dm, DBG_FA_CNT, ("[VHT]  CRC32 {error, ok}= {%d, %d}\n", false_alm_cnt->cnt_crc32_error_all, false_alm_cnt->cnt_crc32_ok_all));
	PHYDM_DBG(p_dm, DBG_FA_CNT, ("FA_Cnt: Dbg port 0x0 = 0x%x, EDCCA = %d\n\n", false_alm_cnt->dbg_port0, false_alm_cnt->edcca_flag));
}

#ifdef PHYDM_TDMA_DIG_SUPPORT
void
phydm_set_tdma_dig_timer(
	void		*p_dm_void
	)
{
	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32	delta_time_us = p_dm->tdma_dig_timer_ms * 1000;
	struct phydm_dig_struct	*p_dig_t;
	u32	timeout;
	u32	current_time_stamp, diff_time_stamp, regb0;
	
	p_dig_t = &p_dm->dm_dig_table;
	/*some IC has no FREERUN_CUNT register, like 92E*/
	if (p_dm->support_ic_type & ODM_RTL8197F)
		current_time_stamp = odm_get_bb_reg(p_dm, 0x568, bMaskDWord);
	else
		return;

	timeout = current_time_stamp + delta_time_us;

	diff_time_stamp = current_time_stamp - p_dig_t->cur_timestamp;
	p_dig_t->pre_timestamp = p_dig_t->cur_timestamp;
	p_dig_t->cur_timestamp = current_time_stamp;

	/*HIMR0, it shows HW interrupt mask*/
	regb0 = odm_get_bb_reg(p_dm, 0xb0, bMaskDWord);

	PHYDM_DBG(p_dm, DBG_DIG,
		("Set next tdma_dig_timer\n"));
	PHYDM_DBG(p_dm, DBG_DIG,
		("current_time_stamp=%d, delta_time_us=%d, timeout=%d, diff_time_stamp=%d, Reg0xb0 = 0x%x\n",
		current_time_stamp,
		delta_time_us,
		timeout,
		diff_time_stamp,
		regb0));

	if (p_dm->support_ic_type & ODM_RTL8197F)		/*REG_PS_TIMER2*/
		odm_set_bb_reg(p_dm, 0x588, bMaskDWord, timeout);
	else {
		PHYDM_DBG(p_dm, DBG_DIG,
					("NOT 97F, TDMA-DIG timer does NOT start!\n"));
		return;
	}
}

void
phydm_tdma_dig_timer_check(
	void		*p_dm_void
	)
{
	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_dig_struct	*p_dig_t;

	p_dig_t = &p_dm->dm_dig_table;
	
	PHYDM_DBG(p_dm, DBG_DIG,
				("tdma_dig_cnt=%d, pre_tdma_dig_cnt=%d\n",
				p_dig_t->tdma_dig_cnt,
				p_dig_t->pre_tdma_dig_cnt));

	if ((p_dig_t->tdma_dig_cnt == 0) ||
		(p_dig_t->tdma_dig_cnt == p_dig_t->pre_tdma_dig_cnt)) {

		if (p_dm->support_ability & ODM_BB_DIG) {
			/*if interrupt mask info is got.*/
			/*Reg0xb0 is no longer needed*/
			/*regb0 = odm_get_bb_reg(p_dm, 0xb0, bMaskDWord);*/
			PHYDM_DBG(p_dm, DBG_DIG,
						("Check fail, IntMask[0]=0x%x, restart tdma_dig_timer !!!\n",
						*p_dm->p_interrupt_mask));

			phydm_tdma_dig_add_interrupt_mask_handler(p_dm);
			phydm_enable_rx_related_interrupt_handler(p_dm);
			phydm_set_tdma_dig_timer(p_dm);
		}
	} else
		PHYDM_DBG(p_dm, DBG_DIG,
					("Check pass, update pre_tdma_dig_cnt\n"));

	p_dig_t->pre_tdma_dig_cnt = p_dig_t->tdma_dig_cnt;
}

/*different IC/team may use different timer for tdma-dig*/
void
phydm_tdma_dig_add_interrupt_mask_handler(
	void		*p_dm_void
	)
{
	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

#if (DM_ODM_SUPPORT_TYPE == (ODM_AP))
	if (p_dm->support_ic_type & ODM_RTL8197F)
		phydm_add_interrupt_mask_handler(p_dm, HAL_INT_TYPE_PSTIMEOUT2);	/*HAL_INT_TYPE_PSTIMEOUT2*/
#elif (DM_ODM_SUPPORT_TYPE == (ODM_WIN))
#elif (DM_ODM_SUPPORT_TYPE == (ODM_CE))
#endif
}

void
phydm_tdma_dig(
	void		*p_dm_void
	)
{
	struct PHY_DM_STRUCT *p_dm;
	struct phydm_dig_struct	*p_dig_t;
	struct phydm_fa_struct *p_falm_cnt;
	u32	reg_c50;
	
	p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	p_dig_t = &p_dm->dm_dig_table;
	p_falm_cnt = &p_dm->false_alm_cnt;
	reg_c50 = odm_get_bb_reg(p_dm, 0xc50, MASKBYTE0);

	p_dig_t->tdma_dig_state =
		p_dig_t->tdma_dig_cnt % p_dm->tdma_dig_state_number;

	PHYDM_DBG(p_dm, DBG_DIG,
				("tdma_dig_state=%d, regc50=0x%x\n",
				p_dig_t->tdma_dig_state,
				reg_c50));

	p_dig_t->tdma_dig_cnt++;

	if (p_dig_t->tdma_dig_state == 1) {
		// update IGI from tdma_dig_state == 0
		if (p_dig_t->cur_ig_value_tdma == 0)
			p_dig_t->cur_ig_value_tdma = p_dig_t->cur_ig_value;

		odm_write_dig(p_dm, p_dig_t->cur_ig_value_tdma);
		phydm_tdma_false_alarm_counter_check(p_dm);
		PHYDM_DBG(p_dm, DBG_DIG,
			("tdma_dig_state=%d, reset FA counter !!!\n",
			p_dig_t->tdma_dig_state));

	} else if (p_dig_t->tdma_dig_state == 0) {
		/* update p_dig_t->CurIGValue,*/
		/* it may different from p_dig_t->cur_ig_value_tdma */
		/* TDMA IGI upperbond @ L-state = */
		/* rf_ft_var.tdma_dig_low_upper_bond = 0x26 */

		if (p_dig_t->cur_ig_value >= p_dm->tdma_dig_low_upper_bond)
			p_dig_t->low_ig_value = p_dm->tdma_dig_low_upper_bond;
		else
			p_dig_t->low_ig_value = p_dig_t->cur_ig_value;

		odm_write_dig(p_dm, p_dig_t->low_ig_value);
		phydm_tdma_false_alarm_counter_check(p_dm);
	} else
		phydm_tdma_false_alarm_counter_check(p_dm);
}

/*============================================================*/
/*FASLE ALARM CHECK*/
/*============================================================*/

void
phydm_tdma_false_alarm_counter_check(
	void		*p_dm_void
	)
{
	struct PHY_DM_STRUCT	*p_dm;
	struct phydm_fa_struct	*p_falm_cnt;
	struct phydm_fa_acc_struct	*p_falm_cnt_acc;
	struct phydm_dig_struct	*p_dig_t;
	boolean	rssi_dump_en = 0;
	u32 timestamp;
	u8 tdma_dig_state_number;

	p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	p_falm_cnt = &p_dm->false_alm_cnt;
	p_falm_cnt_acc = &p_dm->false_alm_cnt_acc;
	p_dig_t = &p_dm->dm_dig_table;

	if (p_dig_t->tdma_dig_state == 1)
		phydm_false_alarm_counter_reset(p_dm);
		/* Reset FalseAlarmCounterStatistics */
		/* fa_acc_1sec_tsf = fa_acc_1sec_tsf, keep */
		/* fa_end_tsf = fa_start_tsf = TSF */
	else {
		odm_false_alarm_counter_statistics(p_dm);
		if (p_dm->support_ic_type & ODM_RTL8197F)		/*REG_FREERUN_CNT*/
			timestamp = odm_get_bb_reg(p_dm, 0x568, bMaskDWord);
		else {
			PHYDM_DBG(p_dm, DBG_DIG,
						("Caution! NOT 97F! TDMA-DIG timer does NOT start!!!\n"));
			return;
		}
		p_dig_t->fa_end_timestamp = timestamp;
		p_dig_t->fa_acc_1sec_timestamp +=
			(p_dig_t->fa_end_timestamp - p_dig_t->fa_start_timestamp);

		/*prevent dumb*/
		if (p_dm->tdma_dig_state_number == 1)
			p_dm->tdma_dig_state_number = 2;

		tdma_dig_state_number = p_dm->tdma_dig_state_number;
		p_dig_t->sec_factor =
			tdma_dig_state_number / (tdma_dig_state_number - 1);

		/*1sec = 1000000us*/
		if (p_dig_t->fa_acc_1sec_timestamp >= (u32)(1000000 / p_dig_t->sec_factor)) {
			rssi_dump_en = 1;
			phydm_false_alarm_counter_acc(p_dm, rssi_dump_en);
			PHYDM_DBG(p_dm, DBG_DIG,
						("sec_factor = %u, total FA = %u, is_linked=%u\n",
						p_dig_t->sec_factor,
						p_falm_cnt_acc->cnt_all,
						p_dm->is_linked));

			phydm_noisy_detection(p_dm);
			phydm_cck_pd_th(p_dm);
			phydm_dig(p_dm);
			phydm_false_alarm_counter_acc_reset(p_dm);

			/* Reset FalseAlarmCounterStatistics */
			/* fa_end_tsf = fa_start_tsf = TSF, keep */
			/* fa_acc_1sec_tsf = 0 */
			phydm_false_alarm_counter_reset(p_dm);
		} else
			phydm_false_alarm_counter_acc(p_dm, rssi_dump_en);
	}
}

void
phydm_false_alarm_counter_acc(
	void		*p_dm_void,
	boolean		rssi_dump_en
	)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_fa_struct			*p_falm_cnt;
	struct phydm_fa_acc_struct		*p_falm_cnt_acc;
	struct phydm_dig_struct	*p_dig_t;
	
	p_falm_cnt = &p_dm->false_alm_cnt;
	p_falm_cnt_acc = &p_dm->false_alm_cnt_acc;
	p_dig_t = &p_dm->dm_dig_table;

	p_falm_cnt_acc->cnt_parity_fail += p_falm_cnt->cnt_parity_fail;
	p_falm_cnt_acc->cnt_rate_illegal += p_falm_cnt->cnt_rate_illegal;
	p_falm_cnt_acc->cnt_crc8_fail += p_falm_cnt->cnt_crc8_fail;
	p_falm_cnt_acc->cnt_mcs_fail += p_falm_cnt->cnt_mcs_fail;
	p_falm_cnt_acc->cnt_ofdm_fail += p_falm_cnt->cnt_ofdm_fail;
	p_falm_cnt_acc->cnt_cck_fail += p_falm_cnt->cnt_cck_fail;
	p_falm_cnt_acc->cnt_all += p_falm_cnt->cnt_all;
	p_falm_cnt_acc->cnt_fast_fsync += p_falm_cnt->cnt_fast_fsync;
	p_falm_cnt_acc->cnt_sb_search_fail += p_falm_cnt->cnt_sb_search_fail;
	p_falm_cnt_acc->cnt_ofdm_cca += p_falm_cnt->cnt_ofdm_cca;
	p_falm_cnt_acc->cnt_cck_cca += p_falm_cnt->cnt_cck_cca;
	p_falm_cnt_acc->cnt_cca_all += p_falm_cnt->cnt_cca_all;
	p_falm_cnt_acc->cnt_cck_crc32_error += p_falm_cnt->cnt_cck_crc32_error;
	p_falm_cnt_acc->cnt_cck_crc32_ok += p_falm_cnt->cnt_cck_crc32_ok;
	p_falm_cnt_acc->cnt_ofdm_crc32_error += p_falm_cnt->cnt_ofdm_crc32_error;
	p_falm_cnt_acc->cnt_ofdm_crc32_ok += p_falm_cnt->cnt_ofdm_crc32_ok;
	p_falm_cnt_acc->cnt_ht_crc32_error += p_falm_cnt->cnt_ht_crc32_error;
	p_falm_cnt_acc->cnt_ht_crc32_ok += p_falm_cnt->cnt_ht_crc32_ok;
	p_falm_cnt_acc->cnt_vht_crc32_error += p_falm_cnt->cnt_vht_crc32_error;
	p_falm_cnt_acc->cnt_vht_crc32_ok += p_falm_cnt->cnt_vht_crc32_ok;
	p_falm_cnt_acc->cnt_crc32_error_all += p_falm_cnt->cnt_crc32_error_all;
	p_falm_cnt_acc->cnt_crc32_ok_all += p_falm_cnt->cnt_crc32_ok_all;

	if (rssi_dump_en == 1) {
		p_falm_cnt_acc->cnt_all_1sec =
			p_falm_cnt_acc->cnt_all * p_dig_t->sec_factor;
		p_falm_cnt_acc->cnt_cca_all_1sec =
			p_falm_cnt_acc->cnt_cca_all * p_dig_t->sec_factor;
		p_falm_cnt_acc->cnt_cck_fail_1sec =
			p_falm_cnt_acc->cnt_cck_fail * p_dig_t->sec_factor;
	}
}

void
phydm_false_alarm_counter_acc_reset(
	void		*p_dm_void
	)
{
	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_fa_acc_struct *p_falm_cnt_acc;

	p_falm_cnt_acc = &p_dm->false_alm_cnt_acc;

	/* Cnt_all_for_rssi_dump & Cnt_CCA_all_for_rssi_dump */
	/* do NOT need to be reset */
	odm_memory_set(p_dm, p_falm_cnt_acc, 0, sizeof(p_falm_cnt_acc));
}

void
phydm_false_alarm_counter_reset(
	void		*p_dm_void
	)
{
	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_fa_struct *p_falm_cnt;
	struct phydm_dig_struct	*p_dig_t;
	u32	timestamp;

	p_falm_cnt = &p_dm->false_alm_cnt;
	p_dig_t = &p_dm->dm_dig_table;

	memset(p_falm_cnt, 0, sizeof(p_dm->false_alm_cnt));
	phydm_false_alarm_counter_reg_reset(p_dm);

	if (p_dig_t->tdma_dig_state != 1)
		p_dig_t->fa_acc_1sec_timestamp = 0;
	else
		p_dig_t->fa_acc_1sec_timestamp = p_dig_t->fa_acc_1sec_timestamp;

	/*REG_FREERUN_CNT*/
	timestamp = odm_get_bb_reg(p_dm, 0x568, bMaskDWord);
	p_dig_t->fa_start_timestamp = timestamp;
	p_dig_t->fa_end_timestamp = timestamp;
}

#endif	/*#ifdef PHYDM_TDMA_DIG_SUPPORT*/

#ifdef PHYDM_LNA_SAT_CHK_SUPPORT
void
phydm_lna_sat_chk_init(
	void		*p_dm_void
	)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	struct phydm_lna_sat_info_struct *p_lna_info = &p_dm->dm_lna_sat_info;

	PHYDM_DBG(p_dm, DBG_LNA_SAT_CHK, ("%s ==>\n", __FUNCTION__));

	p_lna_info->check_time = 0;
	p_lna_info->sat_cnt_acc_patha = 0;
	p_lna_info->sat_cnt_acc_pathb = 0;
	p_lna_info->cur_sat_status = 0;
	p_lna_info->pre_sat_status = 0;
	p_lna_info->cur_timer_check_cnt = 0;
	p_lna_info->pre_timer_check_cnt = 0;
}

void
phydm_set_ofdm_agc_tab(
	void	*p_dm_void,
	u8		tab_sel
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	/* table sel:0/2, 1 is used for CCK */
	if (tab_sel == OFDM_AGC_TAB_0)
		odm_set_bb_reg(p_dm, 0xc70, 0x1e00, OFDM_AGC_TAB_0);
	else if (tab_sel == OFDM_AGC_TAB_2)
		odm_set_bb_reg(p_dm, 0xc70, 0x1e00, OFDM_AGC_TAB_2);
	else
		odm_set_bb_reg(p_dm, 0xc70, 0x1e00, OFDM_AGC_TAB_0);
}

u8
phydm_get_ofdm_agc_tab(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	return (u1Byte)odm_get_bb_reg(p_dm, 0xc70, 0x1e00);
}

void
phydm_lna_sat_chk(
	void		*p_dm_void
	)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_dig_struct	*p_dig_t = &p_dm->dm_dig_table;
	struct phydm_lna_sat_info_struct *p_lna_info = &p_dm->dm_lna_sat_info;

	u1Byte			igi_rssi_min, rssi_min = p_dm->rssi_min;
	u4Byte			sat_status_patha, sat_status_pathb;
	u1Byte			igi_restore = p_dig_t->cur_ig_value;
	u1Byte			i, lna_sat_chk_cnt = p_dm->lna_sat_chk_cnt;
	u4Byte			lna_sat_cnt_thd = 0;
	u1Byte			agc_tab;
	u4Byte			max_check_time = 0;

	PHYDM_DBG(p_dm, DBG_LNA_SAT_CHK, ("\n%s ==>\n", __FUNCTION__));

	if (!(p_dm->support_ability & ODM_BB_LNA_SAT_CHK)) {
		PHYDM_DBG(p_dm, DBG_LNA_SAT_CHK,
			("support ability is disabled, return.\n"));
		return;
	}

	if (p_dm->is_disable_lna_sat_chk) {
		phydm_lna_sat_chk_init(p_dm);
		PHYDM_DBG(p_dm, DBG_LNA_SAT_CHK,
			("is_disable_lna_sat_chk=%d, return.\n", p_dm->is_disable_lna_sat_chk));
		return;
	}

	//func_start = ODM_GetBBReg(pDM_Odm, 0x560, bMaskDWord);

	// move igi to target pin of rssi_min
	if ((rssi_min == 0) || (rssi_min == 0xff)) {
		// adapt agc table 0
		phydm_set_ofdm_agc_tab(p_dm, OFDM_AGC_TAB_0);
		phydm_lna_sat_chk_init(p_dm);
		return;
	} else if (rssi_min % 2 != 0)
		igi_rssi_min = rssi_min + DIFF_RSSI_TO_IGI - 1;
	else
		igi_rssi_min = rssi_min + DIFF_RSSI_TO_IGI;

	if ((p_dm->lna_sat_chk_period_ms > 0) && (p_dm->lna_sat_chk_period_ms <= ONE_SEC_MS))
		max_check_time = lna_sat_chk_cnt*(ONE_SEC_MS/(p_dm->lna_sat_chk_period_ms))*5;
	else
		max_check_time = lna_sat_chk_cnt * 5;

	lna_sat_cnt_thd = (max_check_time * p_dm->lna_sat_chk_duty_cycle)/100;

	PHYDM_DBG(p_dm, DBG_LNA_SAT_CHK,
		("check_time=%d, rssi_min=%d, igi_rssi_min=0x%x\nlna_sat_chk_cnt=%d, lna_sat_chk_period_ms=%d, max_check_time=%d, lna_sat_cnt_thd=%d\n",
		p_lna_info->check_time,
		rssi_min,
		igi_rssi_min,
		lna_sat_chk_cnt,
		p_dm->lna_sat_chk_period_ms,
		max_check_time,
		lna_sat_cnt_thd));

	odm_write_dig(p_dm, igi_rssi_min);

	// adapt agc table 0 check saturation status
	phydm_set_ofdm_agc_tab(p_dm, OFDM_AGC_TAB_0);
	// open rf power detection ckt & set detection range
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x86, 0x1f, 0x10);
	odm_set_rf_reg(p_dm, RF_PATH_B, 0x86, 0x1f, 0x10);

	// check saturation status
	for (i = 0; i < lna_sat_chk_cnt; i++) {
		sat_status_patha = odm_get_rf_reg(p_dm, RF_PATH_A, 0xae, 0xc0000);
		sat_status_pathb = odm_get_rf_reg(p_dm, RF_PATH_B, 0xae, 0xc0000);
		if (sat_status_patha != 0)
			p_lna_info->sat_cnt_acc_patha++;
		if (sat_status_pathb != 0)
			p_lna_info->sat_cnt_acc_pathb++;

		if ((p_lna_info->sat_cnt_acc_patha >= lna_sat_cnt_thd) ||
			(p_lna_info->sat_cnt_acc_pathb >= lna_sat_cnt_thd)) {
			p_lna_info->cur_sat_status = 1;
			PHYDM_DBG(p_dm, DBG_LNA_SAT_CHK,
			("cur_sat_status=%d, check_time=%d\n",
			p_lna_info->cur_sat_status,
			p_lna_info->check_time));
			break;
		} else
			p_lna_info->cur_sat_status = 0;
	}

	PHYDM_DBG(p_dm, DBG_LNA_SAT_CHK,
		("cur_sat_status=%d, pre_sat_status=%d, sat_cnt_acc_patha=%d, sat_cnt_acc_pathb=%d\n",
		p_lna_info->cur_sat_status,
		p_lna_info->pre_sat_status,
		p_lna_info->sat_cnt_acc_patha,
		p_lna_info->sat_cnt_acc_pathb));

	// agc table decision
	if (p_lna_info->cur_sat_status) {
		if (!p_dm->is_disable_gain_table_switch)
			phydm_set_ofdm_agc_tab(p_dm, OFDM_AGC_TAB_2);
		p_lna_info->check_time = 0;
		p_lna_info->sat_cnt_acc_patha = 0;
		p_lna_info->sat_cnt_acc_pathb = 0;
		p_lna_info->pre_sat_status = p_lna_info->cur_sat_status;

	} else if (p_lna_info->check_time <= (max_check_time - 1)) {
		if (p_lna_info->pre_sat_status && (!p_dm->is_disable_gain_table_switch))
			phydm_set_ofdm_agc_tab(p_dm, OFDM_AGC_TAB_2);
		p_lna_info->check_time++;

	} else if (p_lna_info->check_time == max_check_time) {
		if (!p_dm->is_disable_gain_table_switch && (p_lna_info->pre_sat_status == 1))
			phydm_set_ofdm_agc_tab(p_dm, OFDM_AGC_TAB_0);
		p_lna_info->check_time = 0;
		p_lna_info->sat_cnt_acc_patha = 0;
		p_lna_info->sat_cnt_acc_pathb = 0;
		p_lna_info->pre_sat_status = p_lna_info->cur_sat_status;
	}

	agc_tab = phydm_get_ofdm_agc_tab(p_dm);

	PHYDM_DBG(p_dm, DBG_LNA_SAT_CHK, ("use AGC tab %d\n", agc_tab));
	//func_end = ODM_GetBBReg(pDM_Odm, 0x560, bMaskDWord);

	//PHYDM_DBG(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("function process time=%d\n",
	//	func_end - func_start));

	// restore previous igi
	odm_write_dig(p_dm, igi_restore);
	p_lna_info->cur_timer_check_cnt++;
	odm_set_timer(p_dm, &p_lna_info->phydm_lna_sat_chk_timer, p_dm->lna_sat_chk_period_ms);
}

void
phydm_lna_sat_chk_callback(
	void		*p_dm_void

	)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	PHYDM_DBG(p_dm, DBG_LNA_SAT_CHK, ("\n%s ==>\n", __FUNCTION__));
	phydm_lna_sat_chk(p_dm);
}

void
phydm_lna_sat_chk_timers(
	void		*p_dm_void,
	u8			state
	)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_lna_sat_info_struct *p_lna_info = &p_dm->dm_lna_sat_info;

	if (state == INIT_LNA_SAT_CHK_TIMMER) {
		odm_initialize_timer(p_dm, &(p_lna_info->phydm_lna_sat_chk_timer),
			(void *)phydm_lna_sat_chk_callback, NULL, "phydm_lna_sat_chk_timer");
	} else if (state == CANCEL_LNA_SAT_CHK_TIMMER) {
		odm_cancel_timer(p_dm, &(p_lna_info->phydm_lna_sat_chk_timer));
	} else if (state == RELEASE_LNA_SAT_CHK_TIMMER) {
		odm_release_timer(p_dm, &(p_lna_info->phydm_lna_sat_chk_timer));
	}
}

void
phydm_lna_sat_chk_watchdog(
	void		*p_dm_void
	)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_lna_sat_info_struct *p_lna_info = &p_dm->dm_lna_sat_info;

	u1Byte rssi_min = p_dm->rssi_min;

	PHYDM_DBG(p_dm, DBG_LNA_SAT_CHK, ("\n%s ==>\n", __FUNCTION__));

	if (!(p_dm->support_ability & ODM_BB_LNA_SAT_CHK)) {
		PHYDM_DBG(p_dm, DBG_LNA_SAT_CHK,
			("support ability is disabled, return.\n"));
		return;
	}

	PHYDM_DBG(p_dm, DBG_LNA_SAT_CHK, ("pre_timer_check_cnt=%d, cur_timer_check_cnt=%d\n",
		p_lna_info->pre_timer_check_cnt,
		p_lna_info->cur_timer_check_cnt));

	if (p_dm->is_disable_lna_sat_chk) {
		phydm_lna_sat_chk_init(p_dm);
		PHYDM_DBG(p_dm, DBG_LNA_SAT_CHK,
			("is_disable_lna_sat_chk=%d, return.\n", p_dm->is_disable_lna_sat_chk));
		return;
	}

	if ((p_dm->support_ic_type & ODM_RTL8197F) == 0) {
		PHYDM_DBG(p_dm, DBG_LNA_SAT_CHK,
			("SupportICType != ODM_RTL8197F, return.\n"));
		return;
	}

	if ((rssi_min == 0) || (rssi_min == 0xff)) {
		// adapt agc table 0
		phydm_set_ofdm_agc_tab(p_dm, OFDM_AGC_TAB_0);
		phydm_lna_sat_chk_init(p_dm);
		PHYDM_DBG(p_dm, DBG_LNA_SAT_CHK,
			("rssi_min=%d, return.\n", rssi_min));
		return;
	}

	if (p_lna_info->cur_timer_check_cnt == p_lna_info->pre_timer_check_cnt) {
		PHYDM_DBG(p_dm, DBG_LNA_SAT_CHK, ("Timer check fail, restart timer.\n"));
		phydm_lna_sat_chk(p_dm);
	} else {
		PHYDM_DBG(p_dm, DBG_LNA_SAT_CHK, ("Timer check pass.\n"));
	}
	p_lna_info->pre_timer_check_cnt = p_lna_info->cur_timer_check_cnt;
}
#endif	/*#if (PHYDM_LNA_SAT_CHK_SUPPORT == 1)*/

void
phydm_dig_debug(
	void		*p_dm_void,
	char		input[][16],
	u32		*_used,
	char		*output,
	u32		*_out_len,
	u32		input_num
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_dig_struct	*p_dig_t = &p_dm->dm_dig_table;
	char		help[] = "-h";
	char		monitor[] = "-m";
	u32		var1[10] = {0};
	u32		used = *_used;
	u32		out_len = *_out_len;
	u8		i;

	if ((strcmp(input[1], help) == 0))
		PHYDM_SNPRINTF((output + used, out_len - used, "{0} fa[0] fa[1] fa[2]\n"));
	else if ((strcmp(input[1], monitor) == 0)) {
		PHYDM_SNPRINTF((output + used, out_len - used,
			"Read DIG fa_th[0:2]= {%d, %d, %d}\n", 
			p_dig_t->fa_th[0], p_dig_t->fa_th[1], p_dig_t->fa_th[2]));

	} else {

		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

		for (i = 1; i < 10; i++) {
			if (input[i + 1])
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
		}

		if (var1[0] == 0) {
			p_dig_t->is_dbg_fa_th = true;
			p_dig_t->fa_th[0] =  (u16)var1[1];
			p_dig_t->fa_th[1] =  (u16)var1[2];
			p_dig_t->fa_th[2] =  (u16)var1[3];

			PHYDM_SNPRINTF((output + used, out_len - used,
				"Set DIG fa_th[0:2]= {%d, %d, %d}\n", 
				p_dig_t->fa_th[0], p_dig_t->fa_th[1], p_dig_t->fa_th[2]));
		} else
			p_dig_t->is_dbg_fa_th = false;
	}
	*_used = used;
	*_out_len = out_len;
}

