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

#include "mp_precomp.h"
#include "phydm_precomp.h"

/*Set NHM period, threshold, disable ignore cca or not, disable ignore txon or not*/
void
phydm_nhm_init(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _CCX_INFO		*ccx_info = &p_dm->dm_ccx_info;

	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("[%s]===>\n", __FUNCTION__));
	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("cur_ig_value=0x%x\n", p_dm->dm_dig_table.cur_ig_value));

	phydm_set_nhm_th_by_igi(p_dm, p_dm->dm_dig_table.cur_ig_value);

	ccx_info->nhm_period = 0xC350;
	ccx_info->nhm_inexclude_cca = NHM_EXCLUDE_CCA;
	ccx_info->nhm_inexclude_txon = NHM_EXCLUDE_TXON;

	phydm_nhm_setting(p_dm, SET_NHM_SETTING);
}

boolean
phydm_cal_nhm_cnt(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _CCX_INFO		*ccx_info = &p_dm->dm_ccx_info;
	u8						noisy_nhm_th_index, low_pwr_cnt = 0, high_pwr_cnt = 0;
	u8						noisy_nhm_th = 0x52;
	u8						i;
	boolean					noisy = false, clean = true;

	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("[%s]===>\n", __FUNCTION__));

	if (!(p_dm->support_ability & ODM_BB_ENV_MONITOR))
		return noisy;

	/*nhm_th = 0x52 means 0x52/2-110 = -69dbm*/
	/* IGI < 0x14 */
	if (ccx_info->nhm_th[10] < noisy_nhm_th)
		return	clean;
	else if (ccx_info->nhm_th[0] > noisy_nhm_th)
		return	(p_dm->noisy_decision) ? noisy : clean;
	/* 0x14 <= IGI <= 0x37*/
	else {
		/* search index */
		noisy_nhm_th_index = (noisy_nhm_th - ccx_info->nhm_th[0]) << 2;

		for (i = 0; i <= 11; i++) {
			if (i <= noisy_nhm_th_index)
				low_pwr_cnt += ccx_info->nhm_result[i];
			else
				high_pwr_cnt += ccx_info->nhm_result[i];
		}

		if (low_pwr_cnt + high_pwr_cnt == 0)
			return noisy;		/* noisy environment */
		else if (low_pwr_cnt - high_pwr_cnt >= 100)
			return clean;		/* clean environment */
		else
			return noisy;		/* noisy environment */
	}
}

void
phydm_nhm_setting(
	void		*p_dm_void,
	u8	nhm_setting
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _CCX_INFO	*ccx_info = &p_dm->dm_ccx_info;

	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("[%s]===>\n", __FUNCTION__));

	if (nhm_setting == SET_NHM_SETTING) {
		PHYDM_DBG(p_dm, DBG_ENV_MNTR,
		("nhm_th=(H->L)[0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x]\n",
		ccx_info->nhm_th[10], ccx_info->nhm_th[9], ccx_info->nhm_th[8],
		ccx_info->nhm_th[7], ccx_info->nhm_th[6], ccx_info->nhm_th[5],
		ccx_info->nhm_th[4], ccx_info->nhm_th[3], ccx_info->nhm_th[2],
		ccx_info->nhm_th[1], ccx_info->nhm_th[0]));
	}

	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {

		if (nhm_setting == SET_NHM_SETTING) {

			/*Set inexclude_cca, inexclude_txon*/
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11AC, BIT(9), ccx_info->nhm_inexclude_cca);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11AC, BIT(10), ccx_info->nhm_inexclude_txon);

			/*Set NHM period*/
			odm_set_bb_reg(p_dm, ODM_REG_CCX_PERIOD_11AC, MASKHWORD, ccx_info->nhm_period);

			/*Set NHM threshold*/ /*Unit: PWdB U(8,1)*/
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE0, ccx_info->nhm_th[0]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE1, ccx_info->nhm_th[1]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE2, ccx_info->nhm_th[2]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE3, ccx_info->nhm_th[3]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE0, ccx_info->nhm_th[4]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE1, ccx_info->nhm_th[5]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE2, ccx_info->nhm_th[6]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE3, ccx_info->nhm_th[7]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH8_11AC, MASKBYTE0, ccx_info->nhm_th[8]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11AC, MASKBYTE2, ccx_info->nhm_th[9]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11AC, MASKBYTE3, ccx_info->nhm_th[10]);

			/*CCX EN*/
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11AC, BIT(8), CCX_EN);

		} else if (nhm_setting == STORE_NHM_SETTING) {

			/*Store pervious disable_ignore_cca, disable_ignore_txon*/
			ccx_info->nhm_inexclude_cca_restore = (enum nhm_inexclude_cca)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11AC, BIT(9));
			ccx_info->nhm_inexclude_txon_restore = (enum nhm_inexclude_txon)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11AC, BIT(10));

			/*Store pervious NHM period*/
			ccx_info->nhm_period_restore = (u16)odm_get_bb_reg(p_dm, ODM_REG_CCX_PERIOD_11AC, MASKHWORD);

			/*Store NHM threshold*/
			ccx_info->nhm_th_restore[0] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE0);
			ccx_info->nhm_th_restore[1] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE1);
			ccx_info->nhm_th_restore[2] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE2);
			ccx_info->nhm_th_restore[3] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE3);
			ccx_info->nhm_th_restore[4] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE0);
			ccx_info->nhm_th_restore[5] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE1);
			ccx_info->nhm_th_restore[6] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE2);
			ccx_info->nhm_th_restore[7] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE3);
			ccx_info->nhm_th_restore[8] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH8_11AC, MASKBYTE0);
			ccx_info->nhm_th_restore[9] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11AC, MASKBYTE2);
			ccx_info->nhm_th_restore[10] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11AC, MASKBYTE3);
		} else if (nhm_setting == RESTORE_NHM_SETTING) {

			/*Set disable_ignore_cca, disable_ignore_txon*/
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11AC, BIT(9), ccx_info->nhm_inexclude_cca_restore);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11AC, BIT(10), ccx_info->nhm_inexclude_txon_restore);

			/*Set NHM period*/
			odm_set_bb_reg(p_dm, ODM_REG_CCX_PERIOD_11AC, MASKHWORD, ccx_info->nhm_period);

			/*Set NHM threshold*/
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE0, ccx_info->nhm_th_restore[0]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE1, ccx_info->nhm_th_restore[1]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE2, ccx_info->nhm_th_restore[2]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE3, ccx_info->nhm_th_restore[3]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE0, ccx_info->nhm_th_restore[4]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE1, ccx_info->nhm_th_restore[5]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE2, ccx_info->nhm_th_restore[6]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE3, ccx_info->nhm_th_restore[7]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH8_11AC, MASKBYTE0, ccx_info->nhm_th_restore[8]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11AC, MASKBYTE2, ccx_info->nhm_th_restore[9]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11AC, MASKBYTE3, ccx_info->nhm_th_restore[10]);
		} else
			return;
	}

	else if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {

		if (nhm_setting == SET_NHM_SETTING) {

			/*Set disable_ignore_cca, disable_ignore_txon*/
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11N, BIT(9), ccx_info->nhm_inexclude_cca);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11N, BIT(10), ccx_info->nhm_inexclude_txon);

			/*Set NHM period*/
			odm_set_bb_reg(p_dm, ODM_REG_CCX_PERIOD_11N, MASKHWORD, ccx_info->nhm_period);

			/*Set NHM threshold*/
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE0, ccx_info->nhm_th[0]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE1, ccx_info->nhm_th[1]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE2, ccx_info->nhm_th[2]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE3, ccx_info->nhm_th[3]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE0, ccx_info->nhm_th[4]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE1, ccx_info->nhm_th[5]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE2, ccx_info->nhm_th[6]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE3, ccx_info->nhm_th[7]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH8_11N, MASKBYTE0, ccx_info->nhm_th[8]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11N, MASKBYTE2, ccx_info->nhm_th[9]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11N, MASKBYTE3, ccx_info->nhm_th[10]);

			/*CCX EN*/
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11N, BIT(8), CCX_EN);
		} else if (nhm_setting == STORE_NHM_SETTING) {

			/*Store pervious disable_ignore_cca, disable_ignore_txon*/
			ccx_info->nhm_inexclude_cca_restore = (enum nhm_inexclude_cca)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11N, BIT(9));
			ccx_info->nhm_inexclude_txon_restore = (enum nhm_inexclude_txon)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11N, BIT(10));

			/*Store pervious NHM period*/
			ccx_info->nhm_period_restore = (u16)odm_get_bb_reg(p_dm, ODM_REG_CCX_PERIOD_11N, MASKHWORD);

			/*Store NHM threshold*/
			ccx_info->nhm_th_restore[0] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE0);
			ccx_info->nhm_th_restore[1] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE1);
			ccx_info->nhm_th_restore[2] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE2);
			ccx_info->nhm_th_restore[3] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE3);
			ccx_info->nhm_th_restore[4] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE0);
			ccx_info->nhm_th_restore[5] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE1);
			ccx_info->nhm_th_restore[6] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE2);
			ccx_info->nhm_th_restore[7] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE3);
			ccx_info->nhm_th_restore[8] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH8_11N, MASKBYTE0);
			ccx_info->nhm_th_restore[9] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11N, MASKBYTE2);
			ccx_info->nhm_th_restore[10] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11N, MASKBYTE3);

		} else if (nhm_setting == RESTORE_NHM_SETTING) {

			/*Set disable_ignore_cca, disable_ignore_txon*/
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11N, BIT(9), ccx_info->nhm_inexclude_cca_restore);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11N, BIT(10), ccx_info->nhm_inexclude_txon_restore);

			/*Set NHM period*/
			odm_set_bb_reg(p_dm, ODM_REG_CCX_PERIOD_11N, MASKHWORD, ccx_info->nhm_period_restore);

			/*Set NHM threshold*/
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE0, ccx_info->nhm_th_restore[0]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE1, ccx_info->nhm_th_restore[1]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE2, ccx_info->nhm_th_restore[2]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE3, ccx_info->nhm_th_restore[3]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE0, ccx_info->nhm_th_restore[4]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE1, ccx_info->nhm_th_restore[5]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE2, ccx_info->nhm_th_restore[6]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE3, ccx_info->nhm_th_restore[7]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH8_11N, MASKBYTE0, ccx_info->nhm_th_restore[8]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11N, MASKBYTE2, ccx_info->nhm_th_restore[9]);
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11N, MASKBYTE3, ccx_info->nhm_th_restore[10]);
		} else
			return;

	}
}

void
phydm_nhm_trigger(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("[%s]===>\n", __FUNCTION__));

	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {

		/*Trigger NHM*/
		odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11AC, BIT(1), 0);
		odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11AC, BIT(1), 1);
	} else if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {

		/*Trigger NHM*/
		odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11N, BIT(1), 0);
		odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11N, BIT(1), 1);
	}
}

void
phydm_get_nhm_result(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _CCX_INFO		*ccx_info = &p_dm->dm_ccx_info;
	u32			value32;
	u8			i;

	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("[%s]===>\n", __FUNCTION__));

	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {

		value32 = odm_read_4byte(p_dm, ODM_REG_NHM_CNT_11AC);
		ccx_info->nhm_result[0] = (u8)(value32 & MASKBYTE0);
		ccx_info->nhm_result[1] = (u8)((value32 & MASKBYTE1) >> 8);
		ccx_info->nhm_result[2] = (u8)((value32 & MASKBYTE2) >> 16);
		ccx_info->nhm_result[3] = (u8)((value32 & MASKBYTE3) >> 24);

		value32 = odm_read_4byte(p_dm, ODM_REG_NHM_CNT7_TO_CNT4_11AC);
		ccx_info->nhm_result[4] = (u8)(value32 & MASKBYTE0);
		ccx_info->nhm_result[5] = (u8)((value32 & MASKBYTE1) >> 8);
		ccx_info->nhm_result[6] = (u8)((value32 & MASKBYTE2) >> 16);
		ccx_info->nhm_result[7] = (u8)((value32 & MASKBYTE3) >> 24);

		value32 = odm_read_4byte(p_dm, ODM_REG_NHM_CNT11_TO_CNT8_11AC);
		ccx_info->nhm_result[8] = (u8)(value32 & MASKBYTE0);
		ccx_info->nhm_result[9] = (u8)((value32 & MASKBYTE1) >> 8);
		ccx_info->nhm_result[10] = (u8)((value32 & MASKBYTE2) >> 16);
		ccx_info->nhm_result[11] = (u8)((value32 & MASKBYTE3) >> 24);

		/*Get NHM duration*/
		value32 = odm_read_4byte(p_dm, ODM_REG_NHM_DUR_READY_11AC);
		ccx_info->nhm_duration = (u16)(value32 & MASKLWORD);

	}

	else if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {

		value32 = odm_read_4byte(p_dm, ODM_REG_NHM_CNT_11N);
		ccx_info->nhm_result[0] = (u8)(value32 & MASKBYTE0);
		ccx_info->nhm_result[1] = (u8)((value32 & MASKBYTE1) >> 8);
		ccx_info->nhm_result[2] = (u8)((value32 & MASKBYTE2) >> 16);
		ccx_info->nhm_result[3] = (u8)((value32 & MASKBYTE3) >> 24);

		value32 = odm_read_4byte(p_dm, ODM_REG_NHM_CNT7_TO_CNT4_11N);
		ccx_info->nhm_result[4] = (u8)(value32 & MASKBYTE0);
		ccx_info->nhm_result[5] = (u8)((value32 & MASKBYTE1) >> 8);
		ccx_info->nhm_result[6] = (u8)((value32 & MASKBYTE2) >> 16);
		ccx_info->nhm_result[7] = (u8)((value32 & MASKBYTE3) >> 24);

		value32 = odm_read_4byte(p_dm, ODM_REG_NHM_CNT9_TO_CNT8_11N);
		ccx_info->nhm_result[8] = (u8)((value32 & MASKBYTE2) >> 16);
		ccx_info->nhm_result[9] = (u8)((value32 & MASKBYTE3) >> 24);

		value32 = odm_read_4byte(p_dm, ODM_REG_NHM_CNT10_TO_CNT11_11N);
		ccx_info->nhm_result[10] = (u8)((value32 & MASKBYTE2) >> 16);
		ccx_info->nhm_result[11] = (u8)((value32 & MASKBYTE3) >> 24);

		/*Get NHM duration*/
		value32 = odm_read_4byte(p_dm, ODM_REG_NHM_CNT10_TO_CNT11_11N);
		ccx_info->nhm_duration = (u16)(value32 & MASKLWORD);

	}

	/* sum all nhm_result */
	ccx_info->nhm_result_total = 0;
	for (i = 0; i <= 11; i++)
		ccx_info->nhm_result_total += ccx_info->nhm_result[i];

		PHYDM_DBG(p_dm, DBG_ENV_MNTR,
		("nhm_result=(H->L)[%d %d %d %d (igi) %d %d %d %d %d %d %d %d]\n",
			ccx_info->nhm_result[11], ccx_info->nhm_result[10], ccx_info->nhm_result[9],
			ccx_info->nhm_result[8], ccx_info->nhm_result[7], ccx_info->nhm_result[6],
			ccx_info->nhm_result[5], ccx_info->nhm_result[4], ccx_info->nhm_result[3],
			ccx_info->nhm_result[2], ccx_info->nhm_result[1], ccx_info->nhm_result[0]));
}

boolean
phydm_check_nhm_rdy(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			i;
	boolean			is_ready = false;

	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {

		for (i = 0; i < 200; i++) {
			ODM_delay_ms(1);
			if (odm_get_bb_reg(p_dm, ODM_REG_NHM_DUR_READY_11AC, BIT(16))) {
				is_ready = 1;
				break;
			}
		}
	}

	else if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {

		for (i = 0; i < 200; i++) {
			ODM_delay_ms(1);
			if (odm_get_bb_reg(p_dm, ODM_REG_NHM_DUR_READY_11AC, BIT(17))) {
				is_ready = 1;
				break;
			}
		}
	}
	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("[%s]NHM rdy=%d\n", __FUNCTION__, is_ready));
	return is_ready;
}

void
phydm_store_nhm_setting(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("[%s]===>\n", __FUNCTION__));

	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {


	} else if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {



	}
}

void
phydm_clm_setting(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _CCX_INFO	*ccx_info = &p_dm->dm_ccx_info;

	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {

		odm_set_bb_reg(p_dm, ODM_REG_CCX_PERIOD_11AC, MASKLWORD, ccx_info->clm_period);	/*4us sample 1 time*/
		odm_set_bb_reg(p_dm, ODM_REG_CLM_11AC, BIT(8), 0x1); /*Enable CCX for CLM*/

	} else if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {

		odm_set_bb_reg(p_dm, ODM_REG_CCX_PERIOD_11N, MASKLWORD, ccx_info->clm_period);
		odm_set_bb_reg(p_dm, ODM_REG_CLM_11N, BIT(8), 0x1);	/*Enable CCX for CLM*/
	}

	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("[%s]CLM period=%dus\n", __func__, ccx_info->clm_period * 4));

}

void
phydm_clm_trigger(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("[%s]===>\n", __FUNCTION__));

	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
		odm_set_bb_reg(p_dm, ODM_REG_CLM_11AC, BIT(0), 0x0);	/*Trigger CLM*/
		odm_set_bb_reg(p_dm, ODM_REG_CLM_11AC, BIT(0), 0x1);
	} else if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {
		odm_set_bb_reg(p_dm, ODM_REG_CLM_11N, BIT(0), 0x0);	/*Trigger CLM*/
		odm_set_bb_reg(p_dm, ODM_REG_CLM_11N, BIT(0), 0x1);
	}
}

boolean
phydm_check_clm_rdy(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	boolean			is_ready = false;
	u8				i;

	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
		for (i = 0; i < 200; i++) {
			ODM_delay_ms(1);
			if (odm_get_bb_reg(p_dm, ODM_REG_CLM_RESULT_11AC, BIT(16))) {
				is_ready = 1;
				break;
			}
		}
	} else if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {
		for (i = 0; i < 200; i++) {
			ODM_delay_ms(1);
			if (odm_get_bb_reg(p_dm, ODM_REG_CLM_READY_11N, BIT(16))) {
				is_ready = 1;
				break;
			}
		}
	}
	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("[%s]CLM rdy=%d\n", __FUNCTION__, is_ready));
	return is_ready;
}

void
phydm_get_clm_result(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _CCX_INFO	*ccx_info = &p_dm->dm_ccx_info;

	u32			value32 = 0;

	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES)
		value32 = odm_get_bb_reg(p_dm, ODM_REG_CLM_RESULT_11AC, MASKDWORD);
	else if (p_dm->support_ic_type & ODM_IC_11N_SERIES)
		value32 = odm_get_bb_reg(p_dm, ODM_REG_CLM_RESULT_11N, MASKDWORD);

	ccx_info->clm_result = (u16)(value32 & MASKLWORD);

	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("[%s]CLM result = %dus\n", __func__, ccx_info->clm_result * 4));

}

void
phydm_ccx_monitor(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (!(p_dm->support_ability & ODM_BB_ENV_MONITOR))
		return;

	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("[%s]===>\n", __FUNCTION__));

	phydm_ccx_monitor_result(p_dm);
	phydm_ccx_monitor_trigger(p_dm, (u16)0xC350);	/*monitor 200ms*/
}

void
phydm_ccx_monitor_trigger(
	void			*p_dm_void,
	u16				monitor_time		/*unit 4us*/
)
{
	u8			nhm_th[11], i, igi;
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _CCX_INFO		*ccx_info = &p_dm->dm_ccx_info;

	if (!(p_dm->support_ability & ODM_BB_ENV_MONITOR))
		return;

	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("[%s]===>\n", __FUNCTION__));

	/* check if NHM threshold is changed */
	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
		
		nhm_th[0] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE0);
		nhm_th[1] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE1);
		nhm_th[2] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE2);
		nhm_th[3] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE3);
		nhm_th[4] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE0);
		nhm_th[5] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE1);
		nhm_th[6] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE2);
		nhm_th[7] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE3);
		nhm_th[8] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH8_11AC, MASKBYTE0);
		nhm_th[9] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11AC, MASKBYTE2);
		nhm_th[10] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11AC, MASKBYTE3);
	} else if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {
		
		nhm_th[0] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE0);
		nhm_th[1] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE1);
		nhm_th[2] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE2);
		nhm_th[3] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE3);
		nhm_th[4] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE0);
		nhm_th[5] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE1);
		nhm_th[6] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE2);
		nhm_th[7] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE3);
		nhm_th[8] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH8_11N, MASKBYTE0);
		nhm_th[9] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11N, MASKBYTE2);
		nhm_th[10] = (u8)odm_get_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11N, MASKBYTE3);
	}

	for (i = 0; i <= 10; i++) {
		
		if (nhm_th[i] != ccx_info->nhm_th[i]) {	
			PHYDM_DBG(p_dm, DBG_ENV_MNTR,
				("nhm_th[%d] != ccx_info->nhm_th[%d]!!\n", i, i));
		}
	}

	igi = (u8)odm_get_bb_reg(p_dm, 0xC50, MASKBYTE0);
	phydm_set_nhm_th_by_igi(p_dm, igi);

	ccx_info->nhm_period = monitor_time;
	ccx_info->nhm_inexclude_cca = NHM_EXCLUDE_CCA;
	ccx_info->nhm_inexclude_txon = NHM_EXCLUDE_TXON;

	phydm_nhm_setting(p_dm, SET_NHM_SETTING);
	phydm_nhm_trigger(p_dm);
	
	ccx_info->echo_clm_en = true;
	ccx_info->clm_period = monitor_time;
	phydm_clm_setting(p_dm);
	phydm_clm_trigger(p_dm);

}

void
phydm_ccx_monitor_result(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _CCX_INFO		*ccx_info = &p_dm->dm_ccx_info;
	u32						nhm_ratio = 0, clm_ratio = 0;

	if (!(p_dm->support_ability & ODM_BB_ENV_MONITOR))
		return;

	if (phydm_check_nhm_rdy(p_dm))
		phydm_get_nhm_result(p_dm);

	if (phydm_check_clm_rdy(p_dm))
		phydm_get_clm_result(p_dm);

	if (ccx_info->clm_period != 0)
		clm_ratio = (ccx_info->clm_result*100) / ccx_info->clm_period;
	
	if (ccx_info->nhm_result_total != 0)
		nhm_ratio = ((ccx_info->nhm_result_total - ccx_info->nhm_result[0])*100) >> 8;

	ccx_info->clm_ratio = (u8)clm_ratio;
	ccx_info->nhm_ratio = (u8)nhm_ratio;

	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("[%s]echo_igi=0x%x, nhm_ratio=%d, clm_ratio=%d\n",
		__FUNCTION__, ccx_info->echo_igi, nhm_ratio, clm_ratio));
		
	}

void
phydm_set_nhm_th_by_igi(
	void			*p_dm_void,
	u8				igi
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _CCX_INFO		*ccx_info = &p_dm->dm_ccx_info;
	u8	i;

	ccx_info->echo_igi = igi;
	ccx_info->nhm_th[0] = (ccx_info->echo_igi - CCA_CAP) * IGI_TO_NHM_TH_MULTIPLIER;
	for (i = 1; i <= 10; i++)
		ccx_info->nhm_th[i] = ccx_info->nhm_th[0] + 2 * IGI_TO_NHM_TH_MULTIPLIER * i;

	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("[%s]echo_igi=0x%x\n", __FUNCTION__, ccx_info->echo_igi));
}

