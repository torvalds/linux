#include "mp_precomp.h"
#include "phydm_precomp.h"

/*Set NHM period, threshold, disable ignore cca or not, disable ignore txon or not*/
VOID
phydm_NHMsetting(
	IN		PVOID		pDM_VOID,
	u1Byte	NHMsetting
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PCCX_INFO	CCX_INFO = &pDM_Odm->DM_CCX_INFO;

	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES) {

		if (NHMsetting == SET_NHM_SETTING){
			
			/*Set inexclude_cca, inexclude_txon*/
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC, BIT9, CCX_INFO->NHM_inexclude_cca);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC, BIT10, CCX_INFO->NHM_inexclude_txon);

			/*Set NHM period*/
			ODM_SetBBReg(pDM_Odm, ODM_REG_CCX_PERIOD_11AC, bMaskHWord, CCX_INFO->NHM_period);

			/*Set NHM threshold*/
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11AC, bMaskByte0, CCX_INFO->NHM_th[0]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11AC, bMaskByte1, CCX_INFO->NHM_th[1]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11AC, bMaskByte2, CCX_INFO->NHM_th[2]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11AC, bMaskByte3, CCX_INFO->NHM_th[3]);		
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11AC, bMaskByte0, CCX_INFO->NHM_th[4]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11AC, bMaskByte1, CCX_INFO->NHM_th[5]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11AC, bMaskByte2, CCX_INFO->NHM_th[6]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11AC, bMaskByte3, CCX_INFO->NHM_th[7]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH8_11AC, bMaskByte0, CCX_INFO->NHM_th[8]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC, bMaskByte2, CCX_INFO->NHM_th[9]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC, bMaskByte3, CCX_INFO->NHM_th[10]);

			/*CCX EN*/
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC, BIT8, CCX_EN);
			
		}
		else if (NHMsetting == STORE_NHM_SETTING) {

			/*Store pervious disable_ignore_cca, disable_ignore_txon*/
			CCX_INFO->NHM_inexclude_cca_restore = (BOOLEAN)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC, BIT9);
			CCX_INFO->NHM_inexclude_txon_restore = (BOOLEAN)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC, BIT10);

			/*Store pervious NHM period*/
			CCX_INFO->NHM_period_restore = (u2Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_CCX_PERIOD_11AC, bMaskHWord);

			/*Store NHM threshold*/
			CCX_INFO->NHM_th_restore[0] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11AC, bMaskByte0);
			CCX_INFO->NHM_th_restore[1] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11AC, bMaskByte1);
			CCX_INFO->NHM_th_restore[2] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11AC, bMaskByte2);
			CCX_INFO->NHM_th_restore[3] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11AC, bMaskByte3);		
			CCX_INFO->NHM_th_restore[4] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11AC, bMaskByte0);
			CCX_INFO->NHM_th_restore[5] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11AC, bMaskByte1);
			CCX_INFO->NHM_th_restore[6] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11AC, bMaskByte2);
			CCX_INFO->NHM_th_restore[7] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11AC, bMaskByte3);
			CCX_INFO->NHM_th_restore[8] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH8_11AC, bMaskByte0);
			CCX_INFO->NHM_th_restore[9] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC, bMaskByte2);
			CCX_INFO->NHM_th_restore[10] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC, bMaskByte3);
		}
		else if (NHMsetting == RESTORE_NHM_SETTING) {

			/*Set disable_ignore_cca, disable_ignore_txon*/
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC, BIT9, CCX_INFO->NHM_inexclude_cca_restore);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC, BIT10, CCX_INFO->NHM_inexclude_txon_restore);

			/*Set NHM period*/
			ODM_SetBBReg(pDM_Odm, ODM_REG_CCX_PERIOD_11AC, bMaskHWord, CCX_INFO->NHM_period);

			/*Set NHM threshold*/
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11AC, bMaskByte0, CCX_INFO->NHM_th_restore[0]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11AC, bMaskByte1, CCX_INFO->NHM_th_restore[1]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11AC, bMaskByte2, CCX_INFO->NHM_th_restore[2]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11AC, bMaskByte3, CCX_INFO->NHM_th_restore[3]);		
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11AC, bMaskByte0, CCX_INFO->NHM_th_restore[4]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11AC, bMaskByte1, CCX_INFO->NHM_th_restore[5]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11AC, bMaskByte2, CCX_INFO->NHM_th_restore[6]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11AC, bMaskByte3, CCX_INFO->NHM_th_restore[7]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH8_11AC, bMaskByte0, CCX_INFO->NHM_th_restore[8]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC, bMaskByte2, CCX_INFO->NHM_th_restore[9]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC, bMaskByte3, CCX_INFO->NHM_th_restore[10]);
		}
		else
			return;
	}

	else if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES) {

		if (NHMsetting == SET_NHM_SETTING){
		
			/*Set disable_ignore_cca, disable_ignore_txon*/
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N, BIT9, CCX_INFO->NHM_inexclude_cca);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N, BIT10, CCX_INFO->NHM_inexclude_txon);

			/*Set NHM period*/	
			ODM_SetBBReg(pDM_Odm, ODM_REG_CCX_PERIOD_11N, bMaskHWord, CCX_INFO->NHM_period);

			/*Set NHM threshold*/
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11N, bMaskByte0, CCX_INFO->NHM_th[0]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11N, bMaskByte1, CCX_INFO->NHM_th[1]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11N, bMaskByte2, CCX_INFO->NHM_th[2]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11N, bMaskByte3, CCX_INFO->NHM_th[3]);		
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11N, bMaskByte0, CCX_INFO->NHM_th[4]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11N, bMaskByte1, CCX_INFO->NHM_th[5]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11N, bMaskByte2, CCX_INFO->NHM_th[6]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11N, bMaskByte3, CCX_INFO->NHM_th[7]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH8_11N, bMaskByte0, CCX_INFO->NHM_th[8]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N, bMaskByte2, CCX_INFO->NHM_th[9]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N, bMaskByte3, CCX_INFO->NHM_th[10]);

			/*CCX EN*/
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N, BIT8, CCX_EN);
		}
		else if (NHMsetting == STORE_NHM_SETTING) {

			/*Store pervious disable_ignore_cca, disable_ignore_txon*/
			CCX_INFO->NHM_inexclude_cca_restore = (BOOLEAN)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N, BIT9);
			CCX_INFO->NHM_inexclude_txon_restore= (BOOLEAN)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N, BIT10);

			/*Store pervious NHM period*/
			CCX_INFO->NHM_period_restore= (u2Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_CCX_PERIOD_11N, bMaskHWord);

			/*Store NHM threshold*/
			CCX_INFO->NHM_th_restore[0] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11N, bMaskByte0);
			CCX_INFO->NHM_th_restore[1] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11N, bMaskByte1);
			CCX_INFO->NHM_th_restore[2] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11N, bMaskByte2);
			CCX_INFO->NHM_th_restore[3] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11N, bMaskByte3);		
			CCX_INFO->NHM_th_restore[4] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11N, bMaskByte0);
			CCX_INFO->NHM_th_restore[5] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11N, bMaskByte1);
			CCX_INFO->NHM_th_restore[6] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11N, bMaskByte2);
			CCX_INFO->NHM_th_restore[7] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11N, bMaskByte3);
			CCX_INFO->NHM_th_restore[8] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH8_11N, bMaskByte0);
			CCX_INFO->NHM_th_restore[9] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N, bMaskByte2);
			CCX_INFO->NHM_th_restore[10] = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N, bMaskByte3);			

		}
		else if (NHMsetting == RESTORE_NHM_SETTING) {

			/*Set disable_ignore_cca, disable_ignore_txon*/
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N, BIT9, CCX_INFO->NHM_inexclude_cca_restore);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N, BIT10, CCX_INFO->NHM_inexclude_txon_restore);

			/*Set NHM period*/
			ODM_SetBBReg(pDM_Odm, ODM_REG_CCX_PERIOD_11N, bMaskHWord, CCX_INFO->NHM_period_restore);

			/*Set NHM threshold*/
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11N, bMaskByte0, CCX_INFO->NHM_th_restore[0]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11N, bMaskByte1, CCX_INFO->NHM_th_restore[1]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11N, bMaskByte2, CCX_INFO->NHM_th_restore[2]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11N, bMaskByte3, CCX_INFO->NHM_th_restore[3]);		
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11N, bMaskByte0, CCX_INFO->NHM_th_restore[4]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11N, bMaskByte1, CCX_INFO->NHM_th_restore[5]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11N, bMaskByte2, CCX_INFO->NHM_th_restore[6]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11N, bMaskByte3, CCX_INFO->NHM_th_restore[7]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH8_11N, bMaskByte0, CCX_INFO->NHM_th_restore[8]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N, bMaskByte2, CCX_INFO->NHM_th_restore[9]);
			ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N, bMaskByte3, CCX_INFO->NHM_th_restore[10]);
		}
		else
			return;
		
	}
}

VOID
phydm_NHMtrigger(
	IN		PVOID		pDM_VOID
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PCCX_INFO		CCX_INFO = &pDM_Odm->DM_CCX_INFO;

	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES) {

		/*Trigger NHM*/
		ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC, BIT1, 0);
		ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC, BIT1, 1);
	}
	else if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES) {

		/*Trigger NHM*/
		ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N, BIT1, 0);
		ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N, BIT1, 1);		
	}
}

VOID
phydm_getNHMresult(
	IN		PVOID		pDM_VOID
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte			value32;
	PCCX_INFO		CCX_INFO = &pDM_Odm->DM_CCX_INFO;
	
	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES) {

			value32 = ODM_Read4Byte(pDM_Odm, ODM_REG_NHM_CNT_11AC);
			CCX_INFO->NHM_result[0] = (u1Byte)(value32 & bMaskByte0);
			CCX_INFO->NHM_result[1] = (u1Byte)((value32 & bMaskByte1) >> 8);
			CCX_INFO->NHM_result[2] = (u1Byte)((value32 & bMaskByte2) >> 16);
			CCX_INFO->NHM_result[3] = (u1Byte)((value32 & bMaskByte3) >> 24);

			value32 = ODM_Read4Byte(pDM_Odm, ODM_REG_NHM_CNT7_TO_CNT4_11AC);
			CCX_INFO->NHM_result[4] = (u1Byte)(value32 & bMaskByte0);
			CCX_INFO->NHM_result[5] = (u1Byte)((value32 & bMaskByte1) >> 8);
			CCX_INFO->NHM_result[6] = (u1Byte)((value32 & bMaskByte2) >> 16);
			CCX_INFO->NHM_result[7] = (u1Byte)((value32 & bMaskByte3) >> 24);

			value32 = ODM_Read4Byte(pDM_Odm, ODM_REG_NHM_CNT11_TO_CNT8_11AC);
			CCX_INFO->NHM_result[8] = (u1Byte)(value32 & bMaskByte0);
			CCX_INFO->NHM_result[9] = (u1Byte)((value32 & bMaskByte1) >> 8);
			CCX_INFO->NHM_result[10] = (u1Byte)((value32 & bMaskByte2) >> 16);
			CCX_INFO->NHM_result[11] = (u1Byte)((value32 & bMaskByte3) >> 24);

			/*Get NHM duration*/
			value32 = ODM_Read4Byte(pDM_Odm, ODM_REG_NHM_DUR_READY_11AC);
			CCX_INFO->NHM_duration = (u2Byte)(value32 & bMaskLWord);
			
	}

	else if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES) {

			value32 = ODM_Read4Byte(pDM_Odm, ODM_REG_NHM_CNT_11N);
			CCX_INFO->NHM_result[0] = (u1Byte)(value32 & bMaskByte0);
			CCX_INFO->NHM_result[1] = (u1Byte)((value32 & bMaskByte1) >> 8);
			CCX_INFO->NHM_result[2] = (u1Byte)((value32 & bMaskByte2) >> 16);
			CCX_INFO->NHM_result[3] = (u1Byte)((value32 & bMaskByte3) >> 24);

			value32 = ODM_Read4Byte(pDM_Odm, ODM_REG_NHM_CNT7_TO_CNT4_11N);
			CCX_INFO->NHM_result[4] = (u1Byte)(value32 & bMaskByte0);
			CCX_INFO->NHM_result[5] = (u1Byte)((value32 & bMaskByte1) >> 8);
			CCX_INFO->NHM_result[6] = (u1Byte)((value32 & bMaskByte2) >> 16);
			CCX_INFO->NHM_result[7] = (u1Byte)((value32 & bMaskByte3) >> 24);

			value32 = ODM_Read4Byte(pDM_Odm, ODM_REG_NHM_CNT9_TO_CNT8_11N);
			CCX_INFO->NHM_result[8] = (u1Byte)((value32 & bMaskByte2) >> 16);
			CCX_INFO->NHM_result[9] = (u1Byte)((value32 & bMaskByte3) >> 24);

			value32 = ODM_Read4Byte(pDM_Odm, ODM_REG_NHM_CNT10_TO_CNT11_11N);
			CCX_INFO->NHM_result[10] = (u1Byte)((value32 & bMaskByte2) >> 16);
			CCX_INFO->NHM_result[11] = (u1Byte)((value32 & bMaskByte3) >> 24);

			/*Get NHM duration*/
			value32 = ODM_Read4Byte(pDM_Odm, ODM_REG_NHM_CNT10_TO_CNT11_11N);
			CCX_INFO->NHM_duration = (u2Byte)(value32 & bMaskLWord);

	}

}

BOOLEAN
phydm_checkNHMready(
	IN		PVOID		pDM_VOID
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte			value32 = 0;
	u1Byte			i;
	BOOLEAN			ret = FALSE;
	
	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES) {

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_CLM_RESULT_11AC, bMaskDWord);
		
		for (i = 0; i < 200; i ++) {

			ODM_delay_ms(1);
			if (ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_DUR_READY_11AC, BIT17)) {
				ret = 1;
				break;
			}
		}
	}
	
	else if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES) {

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_CLM_READY_11N, bMaskDWord);
		
		for (i = 0; i < 200; i ++) {

			ODM_delay_ms(1);
			if (ODM_GetBBReg(pDM_Odm, ODM_REG_NHM_DUR_READY_11AC, BIT17) ) {
				ret = 1;
				break;
			}
		}		
	}
	return ret;
}

VOID
phydm_storeNHMsetting(
	IN		PVOID		pDM_VOID
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PCCX_INFO	CCX_INFO = &pDM_Odm->DM_CCX_INFO;

	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES) {


	}
	else if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES) {
		

		
	}
}

VOID
phydm_CLMsetting(
	IN		PVOID			pDM_VOID
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;		
	PCCX_INFO	CCX_INFO = &pDM_Odm->DM_CCX_INFO;


	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES) {

		ODM_SetBBReg(pDM_Odm, ODM_REG_CCX_PERIOD_11AC, bMaskLWord, CCX_INFO->CLM_period);	/*4us sample 1 time*/
		ODM_SetBBReg(pDM_Odm, ODM_REG_CLM_11AC, BIT8, 0x1);										/*Enable CCX for CLM*/
		
	} else if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES) {

		ODM_SetBBReg(pDM_Odm, ODM_REG_CCX_PERIOD_11N, bMaskLWord, CCX_INFO->CLM_period);	/*4us sample 1 time*/
		ODM_SetBBReg(pDM_Odm, ODM_REG_CLM_11N, BIT8, 0x1);								/*Enable CCX for CLM*/	
	}

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CCX, ODM_DBG_LOUD, ("[%s] : CLM period = %dus\n", __func__,  CCX_INFO->CLM_period*4));
		
}

VOID
phydm_CLMtrigger(
	IN		PVOID			pDM_VOID
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;

	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES) {
		ODM_SetBBReg(pDM_Odm, ODM_REG_CLM_11AC, BIT0, 0x0);	/*Trigger CLM*/
		ODM_SetBBReg(pDM_Odm, ODM_REG_CLM_11AC, BIT0, 0x1);
	} else if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES) {
		ODM_SetBBReg(pDM_Odm, ODM_REG_CLM_11N, BIT0, 0x0);	/*Trigger CLM*/
		ODM_SetBBReg(pDM_Odm, ODM_REG_CLM_11N, BIT0, 0x1);
	}
}

BOOLEAN
phydm_checkCLMready(
	IN		PVOID			pDM_VOID
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte			value32 = 0;
	BOOLEAN			ret = FALSE;
	
	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_CLM_RESULT_11AC, bMaskDWord);				/*make sure CLM calc is ready*/
	else if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_CLM_READY_11N, bMaskDWord);				/*make sure CLM calc is ready*/

	if ((pDM_Odm->SupportICType & ODM_IC_11AC_SERIES) && (value32 & BIT16))
		ret = TRUE;
	else if ((pDM_Odm->SupportICType & ODM_IC_11N_SERIES) && (value32 & BIT17))
		ret = TRUE;
	else
		ret = FALSE;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CCX, ODM_DBG_LOUD, ("[%s] : CLM ready = %d\n", __func__, ret));

	return ret;
}

void
phydm_getCLMresult(
	IN		PVOID			pDM_VOID
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PCCX_INFO	CCX_INFO = &pDM_Odm->DM_CCX_INFO;

	u4Byte			value32 = 0;
	u2Byte			results = 0;
	
	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_CLM_RESULT_11AC, bMaskDWord);				/*read CLM calc result*/
	else if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_CLM_RESULT_11N, bMaskDWord);				/*read CLM calc result*/

	CCX_INFO->CLM_result = (u2Byte)(value32 & bMaskLWord);

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CCX, ODM_DBG_LOUD, ("[%s] : CLM result = %dus\n", __func__, CCX_INFO->CLM_result*4));
	
}
