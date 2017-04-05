#include "mp_precomp.h"
#include "phydm_precomp.h"

/*Set NHM period, threshold, disable ignore cca or not, disable ignore txon or not*/
void
phydm_nhm_setting(
	void		*p_dm_void,
	u8	nhm_setting
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _CCX_INFO	*ccx_info = &p_dm_odm->dm_ccx_info;

	if (p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES) {

		if (nhm_setting == SET_NHM_SETTING) {

			/*Set inexclude_cca, inexclude_txon*/
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11AC, BIT(9), ccx_info->nhm_inexclude_cca);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11AC, BIT(10), ccx_info->nhm_inexclude_txon);

			/*Set NHM period*/
			odm_set_bb_reg(p_dm_odm, ODM_REG_CCX_PERIOD_11AC, MASKHWORD, ccx_info->NHM_period);

			/*Set NHM threshold*/
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE0, ccx_info->NHM_th[0]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE1, ccx_info->NHM_th[1]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE2, ccx_info->NHM_th[2]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE3, ccx_info->NHM_th[3]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE0, ccx_info->NHM_th[4]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE1, ccx_info->NHM_th[5]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE2, ccx_info->NHM_th[6]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE3, ccx_info->NHM_th[7]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH8_11AC, MASKBYTE0, ccx_info->NHM_th[8]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11AC, MASKBYTE2, ccx_info->NHM_th[9]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11AC, MASKBYTE3, ccx_info->NHM_th[10]);

			/*CCX EN*/
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11AC, BIT(8), CCX_EN);

		} else if (nhm_setting == STORE_NHM_SETTING) {

			/*Store pervious disable_ignore_cca, disable_ignore_txon*/
			ccx_info->NHM_inexclude_cca_restore = (enum nhm_inexclude_cca)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11AC, BIT(9));
			ccx_info->NHM_inexclude_txon_restore = (enum nhm_inexclude_txon)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11AC, BIT(10));

			/*Store pervious NHM period*/
			ccx_info->NHM_period_restore = (u16)odm_get_bb_reg(p_dm_odm, ODM_REG_CCX_PERIOD_11AC, MASKHWORD);

			/*Store NHM threshold*/
			ccx_info->NHM_th_restore[0] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE0);
			ccx_info->NHM_th_restore[1] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE1);
			ccx_info->NHM_th_restore[2] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE2);
			ccx_info->NHM_th_restore[3] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE3);
			ccx_info->NHM_th_restore[4] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE0);
			ccx_info->NHM_th_restore[5] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE1);
			ccx_info->NHM_th_restore[6] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE2);
			ccx_info->NHM_th_restore[7] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE3);
			ccx_info->NHM_th_restore[8] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH8_11AC, MASKBYTE0);
			ccx_info->NHM_th_restore[9] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11AC, MASKBYTE2);
			ccx_info->NHM_th_restore[10] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11AC, MASKBYTE3);
		} else if (nhm_setting == RESTORE_NHM_SETTING) {

			/*Set disable_ignore_cca, disable_ignore_txon*/
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11AC, BIT(9), ccx_info->NHM_inexclude_cca_restore);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11AC, BIT(10), ccx_info->NHM_inexclude_txon_restore);

			/*Set NHM period*/
			odm_set_bb_reg(p_dm_odm, ODM_REG_CCX_PERIOD_11AC, MASKHWORD, ccx_info->NHM_period);

			/*Set NHM threshold*/
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE0, ccx_info->NHM_th_restore[0]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE1, ccx_info->NHM_th_restore[1]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE2, ccx_info->NHM_th_restore[2]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11AC, MASKBYTE3, ccx_info->NHM_th_restore[3]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE0, ccx_info->NHM_th_restore[4]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE1, ccx_info->NHM_th_restore[5]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE2, ccx_info->NHM_th_restore[6]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11AC, MASKBYTE3, ccx_info->NHM_th_restore[7]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH8_11AC, MASKBYTE0, ccx_info->NHM_th_restore[8]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11AC, MASKBYTE2, ccx_info->NHM_th_restore[9]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11AC, MASKBYTE3, ccx_info->NHM_th_restore[10]);
		} else
			return;
	}

	else if (p_dm_odm->support_ic_type & ODM_IC_11N_SERIES) {

		if (nhm_setting == SET_NHM_SETTING) {

			/*Set disable_ignore_cca, disable_ignore_txon*/
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11N, BIT(9), ccx_info->nhm_inexclude_cca);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11N, BIT(10), ccx_info->nhm_inexclude_txon);

			/*Set NHM period*/
			odm_set_bb_reg(p_dm_odm, ODM_REG_CCX_PERIOD_11N, MASKHWORD, ccx_info->NHM_period);

			/*Set NHM threshold*/
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE0, ccx_info->NHM_th[0]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE1, ccx_info->NHM_th[1]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE2, ccx_info->NHM_th[2]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE3, ccx_info->NHM_th[3]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE0, ccx_info->NHM_th[4]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE1, ccx_info->NHM_th[5]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE2, ccx_info->NHM_th[6]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE3, ccx_info->NHM_th[7]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH8_11N, MASKBYTE0, ccx_info->NHM_th[8]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11N, MASKBYTE2, ccx_info->NHM_th[9]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11N, MASKBYTE3, ccx_info->NHM_th[10]);

			/*CCX EN*/
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11N, BIT(8), CCX_EN);
		} else if (nhm_setting == STORE_NHM_SETTING) {

			/*Store pervious disable_ignore_cca, disable_ignore_txon*/
			ccx_info->NHM_inexclude_cca_restore = (enum nhm_inexclude_cca)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11N, BIT(9));
			ccx_info->NHM_inexclude_txon_restore = (enum nhm_inexclude_txon)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11N, BIT(10));

			/*Store pervious NHM period*/
			ccx_info->NHM_period_restore = (u16)odm_get_bb_reg(p_dm_odm, ODM_REG_CCX_PERIOD_11N, MASKHWORD);

			/*Store NHM threshold*/
			ccx_info->NHM_th_restore[0] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE0);
			ccx_info->NHM_th_restore[1] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE1);
			ccx_info->NHM_th_restore[2] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE2);
			ccx_info->NHM_th_restore[3] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE3);
			ccx_info->NHM_th_restore[4] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE0);
			ccx_info->NHM_th_restore[5] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE1);
			ccx_info->NHM_th_restore[6] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE2);
			ccx_info->NHM_th_restore[7] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE3);
			ccx_info->NHM_th_restore[8] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH8_11N, MASKBYTE0);
			ccx_info->NHM_th_restore[9] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11N, MASKBYTE2);
			ccx_info->NHM_th_restore[10] = (u8)odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11N, MASKBYTE3);

		} else if (nhm_setting == RESTORE_NHM_SETTING) {

			/*Set disable_ignore_cca, disable_ignore_txon*/
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11N, BIT(9), ccx_info->NHM_inexclude_cca_restore);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11N, BIT(10), ccx_info->NHM_inexclude_txon_restore);

			/*Set NHM period*/
			odm_set_bb_reg(p_dm_odm, ODM_REG_CCX_PERIOD_11N, MASKHWORD, ccx_info->NHM_period_restore);

			/*Set NHM threshold*/
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE0, ccx_info->NHM_th_restore[0]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE1, ccx_info->NHM_th_restore[1]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE2, ccx_info->NHM_th_restore[2]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH3_TO_TH0_11N, MASKBYTE3, ccx_info->NHM_th_restore[3]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE0, ccx_info->NHM_th_restore[4]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE1, ccx_info->NHM_th_restore[5]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE2, ccx_info->NHM_th_restore[6]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH7_TO_TH4_11N, MASKBYTE3, ccx_info->NHM_th_restore[7]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH8_11N, MASKBYTE0, ccx_info->NHM_th_restore[8]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11N, MASKBYTE2, ccx_info->NHM_th_restore[9]);
			odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11N, MASKBYTE3, ccx_info->NHM_th_restore[10]);
		} else
			return;

	}
}

void
phydm_nhm_trigger(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _CCX_INFO		*ccx_info = &p_dm_odm->dm_ccx_info;

	if (p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES) {

		/*Trigger NHM*/
		odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11AC, BIT(1), 0);
		odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11AC, BIT(1), 1);
	} else if (p_dm_odm->support_ic_type & ODM_IC_11N_SERIES) {

		/*Trigger NHM*/
		odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11N, BIT(1), 0);
		odm_set_bb_reg(p_dm_odm, ODM_REG_NHM_TH9_TH10_11N, BIT(1), 1);
	}
}

void
phydm_get_nhm_result(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32			value32;
	struct _CCX_INFO		*ccx_info = &p_dm_odm->dm_ccx_info;

	if (p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES) {

		value32 = odm_read_4byte(p_dm_odm, ODM_REG_NHM_CNT_11AC);
		ccx_info->NHM_result[0] = (u8)(value32 & MASKBYTE0);
		ccx_info->NHM_result[1] = (u8)((value32 & MASKBYTE1) >> 8);
		ccx_info->NHM_result[2] = (u8)((value32 & MASKBYTE2) >> 16);
		ccx_info->NHM_result[3] = (u8)((value32 & MASKBYTE3) >> 24);

		value32 = odm_read_4byte(p_dm_odm, ODM_REG_NHM_CNT7_TO_CNT4_11AC);
		ccx_info->NHM_result[4] = (u8)(value32 & MASKBYTE0);
		ccx_info->NHM_result[5] = (u8)((value32 & MASKBYTE1) >> 8);
		ccx_info->NHM_result[6] = (u8)((value32 & MASKBYTE2) >> 16);
		ccx_info->NHM_result[7] = (u8)((value32 & MASKBYTE3) >> 24);

		value32 = odm_read_4byte(p_dm_odm, ODM_REG_NHM_CNT11_TO_CNT8_11AC);
		ccx_info->NHM_result[8] = (u8)(value32 & MASKBYTE0);
		ccx_info->NHM_result[9] = (u8)((value32 & MASKBYTE1) >> 8);
		ccx_info->NHM_result[10] = (u8)((value32 & MASKBYTE2) >> 16);
		ccx_info->NHM_result[11] = (u8)((value32 & MASKBYTE3) >> 24);

		/*Get NHM duration*/
		value32 = odm_read_4byte(p_dm_odm, ODM_REG_NHM_DUR_READY_11AC);
		ccx_info->NHM_duration = (u16)(value32 & MASKLWORD);

	}

	else if (p_dm_odm->support_ic_type & ODM_IC_11N_SERIES) {

		value32 = odm_read_4byte(p_dm_odm, ODM_REG_NHM_CNT_11N);
		ccx_info->NHM_result[0] = (u8)(value32 & MASKBYTE0);
		ccx_info->NHM_result[1] = (u8)((value32 & MASKBYTE1) >> 8);
		ccx_info->NHM_result[2] = (u8)((value32 & MASKBYTE2) >> 16);
		ccx_info->NHM_result[3] = (u8)((value32 & MASKBYTE3) >> 24);

		value32 = odm_read_4byte(p_dm_odm, ODM_REG_NHM_CNT7_TO_CNT4_11N);
		ccx_info->NHM_result[4] = (u8)(value32 & MASKBYTE0);
		ccx_info->NHM_result[5] = (u8)((value32 & MASKBYTE1) >> 8);
		ccx_info->NHM_result[6] = (u8)((value32 & MASKBYTE2) >> 16);
		ccx_info->NHM_result[7] = (u8)((value32 & MASKBYTE3) >> 24);

		value32 = odm_read_4byte(p_dm_odm, ODM_REG_NHM_CNT9_TO_CNT8_11N);
		ccx_info->NHM_result[8] = (u8)((value32 & MASKBYTE2) >> 16);
		ccx_info->NHM_result[9] = (u8)((value32 & MASKBYTE3) >> 24);

		value32 = odm_read_4byte(p_dm_odm, ODM_REG_NHM_CNT10_TO_CNT11_11N);
		ccx_info->NHM_result[10] = (u8)((value32 & MASKBYTE2) >> 16);
		ccx_info->NHM_result[11] = (u8)((value32 & MASKBYTE3) >> 24);

		/*Get NHM duration*/
		value32 = odm_read_4byte(p_dm_odm, ODM_REG_NHM_CNT10_TO_CNT11_11N);
		ccx_info->NHM_duration = (u16)(value32 & MASKLWORD);

	}

}

boolean
phydm_check_nhm_ready(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32			value32 = 0;
	u8			i;
	boolean			ret = false;

	if (p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES) {

		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_CLM_RESULT_11AC, MASKDWORD);

		for (i = 0; i < 200; i++) {

			ODM_delay_ms(1);
			if (odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_DUR_READY_11AC, BIT(17))) {
				ret = 1;
				break;
			}
		}
	}

	else if (p_dm_odm->support_ic_type & ODM_IC_11N_SERIES) {

		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_CLM_READY_11N, MASKDWORD);

		for (i = 0; i < 200; i++) {

			ODM_delay_ms(1);
			if (odm_get_bb_reg(p_dm_odm, ODM_REG_NHM_DUR_READY_11AC, BIT(17))) {
				ret = 1;
				break;
			}
		}
	}
	return ret;
}

void
phydm_store_nhm_setting(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _CCX_INFO	*ccx_info = &p_dm_odm->dm_ccx_info;

	if (p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES) {


	} else if (p_dm_odm->support_ic_type & ODM_IC_11N_SERIES) {



	}
}

void
phydm_clm_setting(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _CCX_INFO	*ccx_info = &p_dm_odm->dm_ccx_info;


	if (p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES) {

		odm_set_bb_reg(p_dm_odm, ODM_REG_CCX_PERIOD_11AC, MASKLWORD, ccx_info->CLM_period);	/*4us sample 1 time*/
		odm_set_bb_reg(p_dm_odm, ODM_REG_CLM_11AC, BIT(8), 0x1);										/*Enable CCX for CLM*/

	} else if (p_dm_odm->support_ic_type & ODM_IC_11N_SERIES) {

		odm_set_bb_reg(p_dm_odm, ODM_REG_CCX_PERIOD_11N, MASKLWORD, ccx_info->CLM_period);	/*4us sample 1 time*/
		odm_set_bb_reg(p_dm_odm, ODM_REG_CLM_11N, BIT(8), 0x1);								/*Enable CCX for CLM*/
	}

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CCX, ODM_DBG_LOUD, ("[%s] : CLM period = %dus\n", __func__,  ccx_info->CLM_period * 4));

}

void
phydm_clm_trigger(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES) {
		odm_set_bb_reg(p_dm_odm, ODM_REG_CLM_11AC, BIT(0), 0x0);	/*Trigger CLM*/
		odm_set_bb_reg(p_dm_odm, ODM_REG_CLM_11AC, BIT(0), 0x1);
	} else if (p_dm_odm->support_ic_type & ODM_IC_11N_SERIES) {
		odm_set_bb_reg(p_dm_odm, ODM_REG_CLM_11N, BIT(0), 0x0);	/*Trigger CLM*/
		odm_set_bb_reg(p_dm_odm, ODM_REG_CLM_11N, BIT(0), 0x1);
	}
}

boolean
phydm_check_cl_mready(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32			value32 = 0;
	boolean			ret = false;

	if (p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES)
		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_CLM_RESULT_11AC, MASKDWORD);				/*make sure CLM calc is ready*/
	else if (p_dm_odm->support_ic_type & ODM_IC_11N_SERIES)
		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_CLM_READY_11N, MASKDWORD);				/*make sure CLM calc is ready*/

	if ((p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES) && (value32 & BIT(16)))
		ret = true;
	else if ((p_dm_odm->support_ic_type & ODM_IC_11N_SERIES) && (value32 & BIT(16)))
		ret = true;
	else
		ret = false;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CCX, ODM_DBG_LOUD, ("[%s] : CLM ready = %d\n", __func__, ret));

	return ret;
}

void
phydm_get_cl_mresult(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _CCX_INFO	*ccx_info = &p_dm_odm->dm_ccx_info;

	u32			value32 = 0;
	u16			results = 0;

	if (p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES)
		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_CLM_RESULT_11AC, MASKDWORD);				/*read CLM calc result*/
	else if (p_dm_odm->support_ic_type & ODM_IC_11N_SERIES)
		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_CLM_RESULT_11N, MASKDWORD);				/*read CLM calc result*/

	ccx_info->CLM_result = (u16)(value32 & MASKLWORD);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CCX, ODM_DBG_LOUD, ("[%s] : CLM result = %dus\n", __func__, ccx_info->CLM_result * 4));

}
