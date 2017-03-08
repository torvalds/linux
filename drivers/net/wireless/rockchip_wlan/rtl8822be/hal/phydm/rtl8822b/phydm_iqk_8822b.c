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

#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (RTL8822B_SUPPORT == 1)


/*---------------------------Define Local Constant---------------------------*/


/*---------------------------Define Local Constant---------------------------*/


#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
void DoIQK_8822B(
	PVOID		pDM_VOID,
	u1Byte 		DeltaThermalIndex,
	u1Byte		ThermalValue,	
	u1Byte 		Threshold
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;

	PADAPTER 		Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	ODM_ResetIQKResult(pDM_Odm);		

	pDM_Odm->RFCalibrateInfo.ThermalValue_IQK= ThermalValue;
    
	PHY_IQCalibrate_8822B(pDM_Odm, TRUE);
	
}
#else
/*Originally pConfig->DoIQK is hooked PHY_IQCalibrate_8822B, but DoIQK_8822B and PHY_IQCalibrate_8822B have different arguments*/
void DoIQK_8822B(
	PVOID		pDM_VOID,
	u1Byte	DeltaThermalIndex,
	u1Byte	ThermalValue,	
	u1Byte	Threshold
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	BOOLEAN		bReCovery = (BOOLEAN) DeltaThermalIndex;

	PHY_IQCalibrate_8822B(pDM_Odm, TRUE);
}
#endif

VOID
_IQK_Fill_IQK_Report_8822B(
	IN	PVOID		pDM_VOID,
	u1Byte			channel	
)
{	
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PIQK_INFO	pIQK_info = &pDM_Odm->IQK_info;
	u4Byte		tmp1 = 0x0, tmp2 = 0x0, tmp3 = 0x0;
	u1Byte		i;
	
	for (i = 0; i < SS_8822B; i++) {
		tmp1 = tmp1 + ((pIQK_info->IQK_fail_report[channel][i][TX_IQK] & 0x1) << i);
		tmp2 = tmp2 + ((pIQK_info->IQK_fail_report[channel][i][RX_IQK] & 0x1) << (i+4));
		tmp3 = tmp3 + ((pIQK_info->RXIQK_fail_code[channel][i] & 0x3) << (i*2+8));
	}
	ODM_Write4Byte(pDM_Odm, 0x1b00, 0xf8000008);
	ODM_SetBBReg(pDM_Odm, 0x1bf0, 0x0000ffff, tmp1 | tmp2 | tmp3);
	
	for (i = 0; i < 2; i++)
		ODM_Write4Byte(pDM_Odm, 0x1be8+(i*4), (pIQK_info->RXIQK_AGC[channel][(i*2)+1] << 16) | pIQK_info->RXIQK_AGC[channel][i*2]);
}


VOID
_IQK_IQK_failReport_8822B(
	IN PDM_ODM_T	pDM_Odm
)
{	
	u4Byte		tmp1bf0 = 0x0;
	u1Byte		i;
	
	tmp1bf0 = ODM_Read4Byte(pDM_Odm, 0x1bf0);

	for (i = 0; i < 4; i++) {
		if (tmp1bf0 & (0x1 << i))
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK] please check S%d TXIQK\n", i));
#else
			panic_printk("[IQK] please check S%d TXIQK\n", i);
#endif
		if (tmp1bf0 & (0x1 << (i + 12)))
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK] please check S%d RXIQK\n", i));
#else
			panic_printk("[IQK] please check S%d RXIQK\n", i);
#endif

	}
}


VOID 
_IQK_BackupMacBB_8822B(
	IN PDM_ODM_T	pDM_Odm,
	IN pu4Byte		MAC_backup,
	IN pu4Byte		BB_backup,
	IN pu4Byte		Backup_MAC_REG,
	IN pu4Byte		Backup_BB_REG
	)
{
	u4Byte i;
	for (i = 0; i < MAC_REG_NUM_8822B; i++)
		MAC_backup[i] = ODM_Read4Byte(pDM_Odm, Backup_MAC_REG[i]);

	for (i = 0; i < BB_REG_NUM_8822B; i++)
		BB_backup[i] = ODM_Read4Byte(pDM_Odm, Backup_BB_REG[i]);
	
/*	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]BackupMacBB Success!!!!\n")); */
}


VOID
_IQK_BackupRF_8822B(
	IN PDM_ODM_T	pDM_Odm,
	IN u4Byte		RF_backup[][2],
	IN pu4Byte		Backup_RF_REG
	)	
{
	u4Byte i;

	for (i = 0; i < RF_REG_NUM_8822B; i++) {
        	RF_backup[i][ODM_RF_PATH_A] = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, Backup_RF_REG[i], bRFRegOffsetMask);
		RF_backup[i][ODM_RF_PATH_B] = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_B, Backup_RF_REG[i], bRFRegOffsetMask);
    	}
/*	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]BackupRF Success!!!!\n")); */
}


VOID
_IQK_AGCbnd_int_8822B(
	IN PDM_ODM_T	pDM_Odm
	)
{
	/*initialize RX AGC bnd, it must do after bbreset*/
	ODM_Write4Byte(pDM_Odm, 0x1b00, 0xf8000008);
	ODM_Write4Byte(pDM_Odm, 0x1b00, 0xf80a7008);
	ODM_Write4Byte(pDM_Odm, 0x1b00, 0xf8015008);
	ODM_Write4Byte(pDM_Odm, 0x1b00, 0xf8000008);
	/*ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, ("[IQK]init. rx agc bnd\n"));*/
}


VOID
_IQK_BB_Reset_8822B(
	IN PDM_ODM_T	pDM_Odm
	)
{
	BOOLEAN		CCAing = FALSE;
	u4Byte		count = 0;		

	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x0, bRFRegOffsetMask, 0x10000);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x0, bRFRegOffsetMask, 0x10000);

	while (1) {
		ODM_Write4Byte(pDM_Odm, 0x8fc, 0x0);
		ODM_SetBBReg(pDM_Odm, 0x198c, 0x7, 0x7); 
		CCAing = (BOOLEAN) ODM_GetBBReg(pDM_Odm, 0xfa0, BIT3);		

		if (count > 30)
			CCAing = FALSE;

		if (CCAing) {
			ODM_delay_ms(1);
			count++;
		}
		else {
			ODM_Write1Byte(pDM_Odm, 0x808, 0x0);	/*RX ant off*/
			ODM_SetBBReg(pDM_Odm, 0xa04, BIT27|BIT26|BIT25|BIT24, 0x0);		/*CCK RX Path off*/

			/*BBreset*/
			ODM_SetBBReg(pDM_Odm, 0x0, BIT16, 0x0);
			ODM_SetBBReg(pDM_Odm, 0x0, BIT16, 0x1);

			if (ODM_GetBBReg(pDM_Odm, 0x660, BIT16))
				ODM_Write4Byte(pDM_Odm, 0x6b4, 0x89000006);
			/*ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]BBreset!!!!\n"));*/
			break;
		}
	}
}

VOID
_IQK_AFESetting_8822B(
	IN PDM_ODM_T	pDM_Odm,
	IN BOOLEAN		Do_IQK
	)
{
	if (Do_IQK) {
		ODM_Write4Byte(pDM_Odm, 0xc60, 0x50000000); 
		ODM_Write4Byte(pDM_Odm, 0xc60, 0x70070040);
		ODM_Write4Byte(pDM_Odm, 0xe60, 0x50000000); 
		ODM_Write4Byte(pDM_Odm, 0xe60, 0x70070040);
		 
		ODM_Write4Byte(pDM_Odm, 0xc58, 0xd8000402);
		ODM_Write4Byte(pDM_Odm, 0xc5c, 0xd1000120);
		ODM_Write4Byte(pDM_Odm, 0xc6c, 0x00000a15);
		ODM_Write4Byte(pDM_Odm, 0xe58, 0xd8000402);
		ODM_Write4Byte(pDM_Odm, 0xe5c, 0xd1000120);
		ODM_Write4Byte(pDM_Odm, 0xe6c, 0x00000a15);
		_IQK_BB_Reset_8822B(pDM_Odm);
/*		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]AFE setting for IQK mode!!!!\n")); */
	} else {
		ODM_Write4Byte(pDM_Odm, 0xc60, 0x50000000); 
		ODM_Write4Byte(pDM_Odm, 0xc60, 0x70038040);
		ODM_Write4Byte(pDM_Odm, 0xe60, 0x50000000); 
		ODM_Write4Byte(pDM_Odm, 0xe60, 0x70038040);

		ODM_Write4Byte(pDM_Odm, 0xc58, 0xd8020402);
		ODM_Write4Byte(pDM_Odm, 0xc5c, 0xde000120);
		ODM_Write4Byte(pDM_Odm, 0xc6c, 0x0000122a);
		ODM_Write4Byte(pDM_Odm, 0xe58, 0xd8020402);
		ODM_Write4Byte(pDM_Odm, 0xe5c, 0xde000120);
		ODM_Write4Byte(pDM_Odm, 0xe6c, 0x0000122a);
/*		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]AFE setting for Normal mode!!!!\n")); */
	}
}

VOID
_IQK_RestoreMacBB_8822B(
	IN PDM_ODM_T		pDM_Odm,
	IN pu4Byte		MAC_backup,
	IN pu4Byte		BB_backup,
	IN pu4Byte		Backup_MAC_REG, 
	IN pu4Byte		Backup_BB_REG
	)	
{
	u4Byte i;

   	for (i = 0; i < MAC_REG_NUM_8822B; i++)
        	ODM_Write4Byte(pDM_Odm, Backup_MAC_REG[i], MAC_backup[i]);
	for (i = 0; i < BB_REG_NUM_8822B; i++)
        	ODM_Write4Byte(pDM_Odm, Backup_BB_REG[i], BB_backup[i]);
/*	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]RestoreMacBB Success!!!!\n")); */
}

VOID
_IQK_RestoreRF_8822B(
	IN PDM_ODM_T			pDM_Odm,
	IN pu4Byte			Backup_RF_REG,
	IN u4Byte 			RF_backup[][2]
	)
{	
	u4Byte i;

	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xef, bRFRegOffsetMask, 0x0);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0xef, bRFRegOffsetMask, 0x0);
	/*0xdf[4]=0*/
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0xdf, bRFRegOffsetMask, RF_backup[0][ODM_RF_PATH_A]&(~BIT4));
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0xdf, bRFRegOffsetMask, RF_backup[0][ODM_RF_PATH_B]&(~BIT4));

	for (i = 1; i < RF_REG_NUM_8822B; i++) {
        	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, Backup_RF_REG[i], bRFRegOffsetMask, RF_backup[i][ODM_RF_PATH_A]);
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, Backup_RF_REG[i], bRFRegOffsetMask, RF_backup[i][ODM_RF_PATH_B]);
    	}
/*	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]RestoreRF Success!!!!\n")); */
	
}


void
_IQK_backupIQK_8822B(
	IN PDM_ODM_T			pDM_Odm,
	IN u1Byte				step
	)
{
	PIQK_INFO	pIQK_info = &pDM_Odm->IQK_info;
	u1Byte		i, j, k, path, idx;
	u4Byte		tmp;
	u2Byte		IQK_Apply[2] = {0xc94, 0xe94};

	if (step == 0x0) {
		pIQK_info->IQK_Channel[1] = pIQK_info->IQK_Channel[0];
		for (i = 0; i < 2; i++) {
			pIQK_info->LOK_IDAC[1][i] = pIQK_info->LOK_IDAC[0][i];
			pIQK_info->RXIQK_AGC[1][i] = pIQK_info->RXIQK_AGC[0][i];
			pIQK_info->bypassIQK[1][i] = pIQK_info->bypassIQK[0][i];
			pIQK_info->RXIQK_fail_code[1][i] = pIQK_info->RXIQK_fail_code[0][i];
			for (j = 0; j < 2; j++) {				
				pIQK_info->IQK_fail_report[1][i][j] = pIQK_info->IQK_fail_report[0][i][j]; 
				for (k = 0; k < 8; k++) {
					pIQK_info->IQK_CFIR_real[1][i][j][k] = pIQK_info->IQK_CFIR_real[0][i][j][k];
					pIQK_info->IQK_CFIR_imag[1][i][j][k] = pIQK_info->IQK_CFIR_imag[0][i][j][k];
				}
			}
		}
		
		for (i = 0; i < 4; i++) {
			pIQK_info->RXIQK_fail_code[0][i] = 0x0;
			pIQK_info->RXIQK_AGC[0][i] = 0x0;
			for (j = 0; j < 2; j++) {
				pIQK_info->IQK_fail_report[0][i][j] = TRUE;
				pIQK_info->gs_retry_count[0][i][j] = 0x0;
			}
			for (j = 0; j < 3; j++)		
				pIQK_info->retry_count[0][i][j] = 0x0;
		}
	} else {
		pIQK_info->IQK_Channel[0] = pIQK_info->RFReg18;
		for (path = 0; path < 2; path++) {
			pIQK_info->LOK_IDAC[0][path] = ODM_GetRFReg(pDM_Odm, path, 0x58, bRFRegOffsetMask);
			pIQK_info->bypassIQK[0][path] = ODM_GetBBReg(pDM_Odm, IQK_Apply[path], bMaskDWord);

			for (idx = 0; idx < 2; idx++) {
				ODM_SetBBReg(pDM_Odm, 0x1b00, bMaskDWord, 0xf8000008 | path << 1);
			
				if (idx == 0)
					ODM_SetBBReg(pDM_Odm, 0x1b0c, BIT13|BIT12, 0x3);
				else
					ODM_SetBBReg(pDM_Odm, 0x1b0c, BIT13|BIT12, 0x1);

				ODM_SetBBReg(pDM_Odm, 0x1bd4, BIT20|BIT19|BIT18|BIT17|BIT16, 0x10); 
			
				for (i = 0; i < 8; i++) {
					ODM_SetBBReg(pDM_Odm, 0x1bd8, bMaskDWord, 0xe0000001+(i*4));
					tmp = ODM_GetBBReg(pDM_Odm, 0x1bfc, bMaskDWord);
					pIQK_info->IQK_CFIR_real[0][path][idx][i] = (tmp & 0x0fff0000)>>16;
					pIQK_info->IQK_CFIR_imag[0][path][idx][i] = tmp & 0xfff;
				}
			}
			ODM_SetBBReg(pDM_Odm, 0x1bd8, bMaskDWord, 0x0);
			ODM_SetBBReg(pDM_Odm, 0x1b0c, BIT13|BIT12, 0x0);
		}
	}
}

VOID
_IQK_ReloadIQKsetting_8822B(
	IN PDM_ODM_T			pDM_Odm,
	IN u1Byte				channel,
	IN u1Byte				reload_idx  /*1: reload TX, 2: reload LO, TX, RX*/
	)
{	
	PIQK_INFO	pIQK_info = &pDM_Odm->IQK_info;
	u1Byte i, path, idx;
	u2Byte		IQK_Apply[2] = {0xc94, 0xe94};

	for (path = 0; path < 2; path++) {
		if (reload_idx == 2) {
		ODM_SetRFReg(pDM_Odm, path, 0xdf, BIT4, 0x1);
		ODM_SetRFReg(pDM_Odm, path, 0x58, bRFRegOffsetMask, pIQK_info->LOK_IDAC[channel][path]);
		}
		
		for (idx = 0; idx < reload_idx; idx++) {
			ODM_SetBBReg(pDM_Odm, 0x1b00, bMaskDWord, 0xf8000008 | path << 1);
			ODM_SetBBReg(pDM_Odm, 0x1b2c, bMaskDWord, 0x7);
			ODM_SetBBReg(pDM_Odm, 0x1b38, bMaskDWord, 0x20000000);
			ODM_SetBBReg(pDM_Odm, 0x1b3c, bMaskDWord, 0x20000000);
			ODM_SetBBReg(pDM_Odm, 0x1bcc, bMaskDWord, 0x00000000);
			
			if (idx == 0)
				ODM_SetBBReg(pDM_Odm, 0x1b0c, BIT13|BIT12, 0x3);
			else
				ODM_SetBBReg(pDM_Odm, 0x1b0c, BIT13|BIT12, 0x1);
			
			ODM_SetBBReg(pDM_Odm, 0x1bd4, BIT20|BIT19|BIT18|BIT17|BIT16, 0x10); 

			for (i = 0; i < 8; i++) {
				ODM_Write4Byte(pDM_Odm, 0x1bd8,	((0xc0000000 >> idx) + 0x3)+(i*4)+(pIQK_info->IQK_CFIR_real[channel][path][idx][i]<<9));
				ODM_Write4Byte(pDM_Odm, 0x1bd8, ((0xc0000000 >> idx) + 0x1)+(i*4)+(pIQK_info->IQK_CFIR_imag[channel][path][idx][i]<<9));
			}
		}
		ODM_SetBBReg(pDM_Odm, IQK_Apply[path], bMaskDWord, pIQK_info->bypassIQK[channel][path]);

		ODM_SetBBReg(pDM_Odm, 0x1bd8, bMaskDWord, 0x0);
		ODM_SetBBReg(pDM_Odm, 0x1b0c, BIT13|BIT12, 0x0);
	}
}


BOOLEAN
_IQK_ReloadIQK_8822B(
	IN	PDM_ODM_T			pDM_Odm,
	IN	BOOLEAN			reset
	)
{	
	PIQK_INFO	pIQK_info = &pDM_Odm->IQK_info;
	u1Byte i;
	BOOLEAN reload = FALSE;

	if (reset) {
		for (i = 0; i < 2; i++)
			pIQK_info->IQK_Channel[i] = 0x0;
	} else {
		pIQK_info->RFReg18 = ODM_GetRFReg(pDM_Odm, 0, 0x18, bRFRegOffsetMask);

		for (i = 0; i < 2; i++) {
			if (pIQK_info->RFReg18 == pIQK_info->IQK_Channel[i]) {
				_IQK_ReloadIQKsetting_8822B(pDM_Odm, i, 2);
				_IQK_Fill_IQK_Report_8822B(pDM_Odm, i);
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]reload IQK result before!!!!\n"));
				reload = TRUE;
			}
		}
	}
	return reload;
}


VOID 
_IQK_RFESetting_8822B(
	IN PDM_ODM_T	pDM_Odm,
	IN BOOLEAN		extPAon
	)
{
	if (extPAon) {
		/*RFE setting*/
		ODM_Write4Byte(pDM_Odm, 0xcb0, 0x77777777);
		ODM_Write4Byte(pDM_Odm, 0xcb4, 0x00007777);
		ODM_Write4Byte(pDM_Odm, 0xcbc, 0x0000083B);
		ODM_Write4Byte(pDM_Odm, 0xeb0, 0x77777777);
		ODM_Write4Byte(pDM_Odm, 0xeb4, 0x00007777);
		ODM_Write4Byte(pDM_Odm, 0xebc, 0x0000083B);
		/*ODM_Write4Byte(pDM_Odm, 0x1990, 0x00000c30);*/
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]external PA on!!!!\n"));
	} else {
		/*RFE setting*/
		ODM_Write4Byte(pDM_Odm, 0xcb0, 0x77777777);
		ODM_Write4Byte(pDM_Odm, 0xcb4, 0x00007777);
		ODM_Write4Byte(pDM_Odm, 0xcbc, 0x00000100);
		ODM_Write4Byte(pDM_Odm, 0xeb0, 0x77777777);
		ODM_Write4Byte(pDM_Odm, 0xeb4, 0x00007777);
		ODM_Write4Byte(pDM_Odm, 0xebc, 0x00000100);
		/*ODM_Write4Byte(pDM_Odm, 0x1990, 0x00000c30);*/
/*		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]external PA off!!!!\n"));*/
	}
}


VOID 
_IQK_RFSetting_8822B(
	IN PDM_ODM_T	pDM_Odm
	)
{
	u1Byte path;
	u4Byte tmp;

	ODM_Write4Byte(pDM_Odm, 0x1b00, 0xf8000008);
	ODM_Write4Byte(pDM_Odm, 0x1bb8, 0x00000000);

	for (path = 0; path < 2; path++) {
		/*0xdf:B11 = 1,B4 = 0, B1 = 1*/
		tmp = ODM_GetRFReg(pDM_Odm, path, 0xdf, bRFRegOffsetMask);
		tmp = (tmp&(~BIT4))|BIT1|BIT11;
		ODM_SetRFReg(pDM_Odm, path, 0xdf, bRFRegOffsetMask, tmp);	
			
		/*release 0x56 TXBB*/
		ODM_SetRFReg(pDM_Odm, path, 0x65, bRFRegOffsetMask, 0x09000);	
		
		if (*pDM_Odm->pBandType == ODM_BAND_5G) {
			ODM_SetRFReg(pDM_Odm, path, 0xef, BIT19, 0x1);
			ODM_SetRFReg(pDM_Odm, path, 0x33, bRFRegOffsetMask, 0x00026);
			ODM_SetRFReg(pDM_Odm, path, 0x3e, bRFRegOffsetMask, 0x00037);
			ODM_SetRFReg(pDM_Odm, path, 0x3f, bRFRegOffsetMask, 0xdefce);
			ODM_SetRFReg(pDM_Odm, path, 0xef, BIT19, 0x0);
		} else {
			ODM_SetRFReg(pDM_Odm, path, 0xef, BIT19, 0x1);
			ODM_SetRFReg(pDM_Odm, path, 0x33, bRFRegOffsetMask, 0x00026);
			ODM_SetRFReg(pDM_Odm, path, 0x3e, bRFRegOffsetMask, 0x00037);
			ODM_SetRFReg(pDM_Odm, path, 0x3f, bRFRegOffsetMask, 0x5efce);
			ODM_SetRFReg(pDM_Odm, path, 0xef, BIT19, 0x0);
		}
	}
}



VOID 
_IQK_ConfigureMACBB_8822B(
	IN PDM_ODM_T		pDM_Odm
	)
{
	/*MACBB register setting*/
	ODM_Write1Byte(pDM_Odm, 0x522, 0x7f);
	ODM_SetBBReg(pDM_Odm, 0x550, BIT11|BIT3, 0x0);
	ODM_SetBBReg(pDM_Odm, 0x90c, BIT15, 0x1);			/*0x90c[15]=1: dac_buf reset selection*/
	ODM_SetBBReg(pDM_Odm, 0x9a4, BIT31, 0x0);         /*0x9a4[31]=0: Select da clock*/
	/*0xc94[0]=1, 0xe94[0]=1: 讓tx從iqk打出來*/
	ODM_SetBBReg(pDM_Odm, 0xc94, BIT0, 0x1);
	ODM_SetBBReg(pDM_Odm, 0xe94, BIT0, 0x1); 
	/* 3-wire off*/
	ODM_Write4Byte(pDM_Odm, 0xc00, 0x00000004);
	ODM_Write4Byte(pDM_Odm, 0xe00, 0x00000004);
/*	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]Set MACBB setting for IQK!!!!\n"));*/

}

VOID
_IQK_LOKSetting_8822B(
	IN PDM_ODM_T	pDM_Odm,
	
	IN u1Byte Path
	)
{
	ODM_Write4Byte(pDM_Odm, 0x1b00, 0xf8000008 | Path << 1);
	ODM_Write4Byte(pDM_Odm, 0x1bcc, 0x9);
	ODM_Write1Byte(pDM_Odm, 0x1b23, 0x00);

	switch (*pDM_Odm->pBandType) {
	case ODM_BAND_2_4G:
		ODM_Write1Byte(pDM_Odm, 0x1b2b, 0x00);
		ODM_SetRFReg(pDM_Odm, Path, 0x56, bRFRegOffsetMask, 0x50df2);
		ODM_SetRFReg(pDM_Odm, Path, 0x8f, bRFRegOffsetMask, 0xadc00);
		/* WE_LUT_TX_LOK*/
		ODM_SetRFReg(pDM_Odm, Path, 0xef, BIT4, 0x1);
		ODM_SetRFReg(pDM_Odm, Path, 0x33, BIT1|BIT0, 0x0);		
		break;
	case ODM_BAND_5G:
		ODM_Write1Byte(pDM_Odm, 0x1b2b, 0x80);
		ODM_SetRFReg(pDM_Odm, Path, 0x56, bRFRegOffsetMask, 0x5086c);
		ODM_SetRFReg(pDM_Odm, Path, 0x8f, bRFRegOffsetMask, 0xa9c00);
		/* WE_LUT_TX_LOK*/
		ODM_SetRFReg(pDM_Odm, Path, 0xef, BIT4, 0x1);
		ODM_SetRFReg(pDM_Odm, Path, 0x33, BIT1|BIT0, 0x1);
		break;
}
/*	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]Set LOK setting!!!!\n"));*/
}


VOID
_IQK_TXKSetting_8822B(
	IN PDM_ODM_T	pDM_Odm,
	IN u1Byte Path
	)
{
	ODM_Write4Byte(pDM_Odm, 0x1b00, 0xf8000008 | Path << 1);
	ODM_Write4Byte(pDM_Odm, 0x1bcc, 0x9);
	ODM_Write4Byte(pDM_Odm, 0x1b20, 0x01440008);

	if (Path == 0x0) 
		ODM_Write4Byte(pDM_Odm, 0x1b00, 0xf800000a);
	else
		ODM_Write4Byte(pDM_Odm, 0x1b00, 0xf8000008);
	ODM_Write4Byte(pDM_Odm, 0x1bcc, 0x3f);

	switch (*pDM_Odm->pBandType) {
	case ODM_BAND_2_4G:
		ODM_SetRFReg(pDM_Odm, Path, 0x56, bRFRegOffsetMask, 0x50df2);
		ODM_SetRFReg(pDM_Odm, Path, 0x8f, bRFRegOffsetMask, 0xadc00);
		ODM_Write1Byte(pDM_Odm, 0x1b2b, 0x00);
		break;
	case ODM_BAND_5G:
		ODM_SetRFReg(pDM_Odm, Path, 0x56, bRFRegOffsetMask, 0x500ef);
		ODM_SetRFReg(pDM_Odm, Path, 0x8f, bRFRegOffsetMask, 0xa9c00);
		ODM_Write1Byte(pDM_Odm, 0x1b2b, 0x80);
		break;
	}
/*	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]Set TXK setting!!!!\n"));*/

}


VOID
_IQK_RXK1Setting_8822B(
	IN PDM_ODM_T	pDM_Odm,
	IN u1Byte Path
	)
{
	PIQK_INFO	pIQK_info = &pDM_Odm->IQK_info;

	ODM_Write4Byte(pDM_Odm, 0x1b00, 0xf8000008 | Path << 1);

	switch (*pDM_Odm->pBandType) {
	case ODM_BAND_2_4G:
		pIQK_info->tmp1bcc = 0x09;
		ODM_Write1Byte(pDM_Odm, 0x1bcc, pIQK_info->tmp1bcc);
		ODM_Write1Byte(pDM_Odm, 0x1b2b, 0x00);
		ODM_Write4Byte(pDM_Odm, 0x1b20, 0x01450008);
		ODM_Write4Byte(pDM_Odm, 0x1b24, 0x01460c88);	
		ODM_SetRFReg(pDM_Odm, Path, 0x56, bRFRegOffsetMask, 0x510e0);
		ODM_SetRFReg(pDM_Odm, Path, 0x8f, bRFRegOffsetMask, 0xacc00);
			break;
	case ODM_BAND_5G:
		pIQK_info->tmp1bcc = 0x09;
		ODM_Write1Byte(pDM_Odm, 0x1bcc, 0x09);
		ODM_Write1Byte(pDM_Odm, 0x1b2b, 0x80);
		ODM_Write4Byte(pDM_Odm, 0x1b20, 0x00850008);
		ODM_Write4Byte(pDM_Odm, 0x1b24, 0x00460048);
		ODM_SetRFReg(pDM_Odm, Path, 0x56, bRFRegOffsetMask, 0x510e0);
		ODM_SetRFReg(pDM_Odm, Path, 0x8f, bRFRegOffsetMask, 0xadc00);
		break;
	}
/*ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]Set RXK setting!!!!\n"));*/

}


VOID
_IQK_RXK2Setting_8822B(
	IN PDM_ODM_T	pDM_Odm,
	IN u1Byte Path
	)
{
	PIQK_INFO	pIQK_info = &pDM_Odm->IQK_info;

	ODM_Write4Byte(pDM_Odm, 0x1b00, 0xf8000008 | Path << 1);

	switch (*pDM_Odm->pBandType) {
	case ODM_BAND_2_4G:
		pIQK_info->tmp1bcc = 0x12;
		ODM_Write1Byte(pDM_Odm, 0x1bcc, pIQK_info->tmp1bcc);
		ODM_Write1Byte(pDM_Odm, 0x1b2b, 0x00);
		ODM_Write4Byte(pDM_Odm, 0x1b20, 0x01450008);
		ODM_Write4Byte(pDM_Odm, 0x1b24, 0x01460848);	
		ODM_SetRFReg(pDM_Odm, Path, 0x56, bRFRegOffsetMask, 0x510e0);
		ODM_SetRFReg(pDM_Odm, Path, 0x8f, bRFRegOffsetMask, 0xa9c00);
		break;
	case ODM_BAND_5G:
		if (Path == ODM_RF_PATH_A) {
			pIQK_info->tmp1bcc = 0x12;
			ODM_Write1Byte(pDM_Odm, 0x1bcc, pIQK_info->tmp1bcc);
		} else {
			pIQK_info->tmp1bcc = 0x09;
			ODM_Write1Byte(pDM_Odm, 0x1bcc, pIQK_info->tmp1bcc);
		}		
		ODM_Write1Byte(pDM_Odm, 0x1b2b, 0x80);
		ODM_Write4Byte(pDM_Odm, 0x1b20, 0x00850008);
		ODM_Write4Byte(pDM_Odm, 0x1b24, 0x00460848);
		ODM_SetRFReg(pDM_Odm, Path, 0x56, bRFRegOffsetMask, 0x51060);
		ODM_SetRFReg(pDM_Odm, Path, 0x8f, bRFRegOffsetMask, 0xa9c00);
		break;
	}
/*	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]Set RXK setting!!!!\n"));*/

}


BOOLEAN
_IQK_CheckCal_8822B(
	IN PDM_ODM_T			pDM_Odm,
	IN u4Byte				IQK_CMD
	)
{	
	BOOLEAN		notready = TRUE, fail = TRUE;	
	u4Byte		delay_count = 0x0;

	while (notready) {
		if (ODM_Read4Byte(pDM_Odm, 0x1b00) == (IQK_CMD & 0xffffff0f)) {
			fail = (BOOLEAN) ODM_GetBBReg(pDM_Odm, 0x1b08, BIT26);
			notready = FALSE;
		} else {
			ODM_delay_ms(1);
			delay_count++;
		}
		
		if (delay_count >= 50) {
			fail = TRUE;
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, 
				("[IQK]IQK timeout!!!\n"));
			break;
		}
	}
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, 
		("[IQK]delay count = 0x%x!!!\n", delay_count));
	return fail;
}


BOOLEAN
_IQK_RXIQK_GainSearchFail_8822B(
	IN PDM_ODM_T			pDM_Odm,
	IN u1Byte		Path,
	IN u1Byte		step
	)
{	

	PIQK_INFO	pIQK_info = &pDM_Odm->IQK_info;
	BOOLEAN	fail = TRUE;		
	u4Byte	IQK_CMD = 0x0, RFReg0, tmp, bb_idx;

	ODM_Write4Byte(pDM_Odm, 0x1b00, 0xf8000008 | Path << 1);

	if (step == RXIQK1)
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]============ S%d RXIQK GainSearch ============\n", Path));

	if (step == RXIQK1)
		IQK_CMD = 0xf8000208 | (1 << (Path + 4));
	else
		IQK_CMD = 0xf8000308 | (1 << (Path + 4));		

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, ("[IQK]S%d GS%d_Trigger = 0x%x\n", Path, step, IQK_CMD));

	ODM_Write4Byte(pDM_Odm, 0x1b00, IQK_CMD);
	ODM_Write4Byte(pDM_Odm, 0x1b00, IQK_CMD+0x1);
	ODM_delay_ms(GS_delay_8822B);
	fail = _IQK_CheckCal_8822B(pDM_Odm, IQK_CMD);

	ODM_Write4Byte(pDM_Odm, 0x1b00, 0xf8000008 | Path << 1);			
	ODM_Write4Byte(pDM_Odm, 0x1bcc, pIQK_info->tmp1bcc);

	if (step == RXIQK2) {
		RFReg0 = ODM_GetRFReg(pDM_Odm, Path, 0x0, bRFRegOffsetMask);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, 
		("[IQK]S%d ==> RF0x0 = 0x%x\n", Path, RFReg0));
		tmp = (RFReg0 & 0x1fe0) >> 5;
		pIQK_info->lna_idx = tmp >> 5;
		bb_idx = tmp & 0x1f;

		if (bb_idx == 0x1) {
			if (pIQK_info->lna_idx != 0x0)
				pIQK_info->lna_idx--;
			fail = TRUE;
		} else if (bb_idx == 0xa) {
			if (pIQK_info->lna_idx != 0x7)
				pIQK_info->lna_idx++;
			fail = TRUE;
		} else
			fail = FALSE;

		if (fail) {
			ODM_Write4Byte(pDM_Odm, 0x1b00, 0xf8000008 | Path << 1);			
			ODM_Write4Byte(pDM_Odm, 0x1b24, (ODM_Read4Byte(pDM_Odm, 0x1b24) & 0xffffe3ff) | (pIQK_info->lna_idx << 10));
		}
	}

	return fail;
}

BOOLEAN
_LOK_One_Shot_8822B(
	IN	PVOID		pDM_VOID,
	u1Byte			Path
)
{	
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PIQK_INFO	pIQK_info = &pDM_Odm->IQK_info;
	u1Byte		delay_count = 0, ii;
	BOOLEAN		LOK_notready = FALSE;
	u4Byte		LOK_temp = 0;
	u4Byte		IQK_CMD = 0x0;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, 
		("[IQK]==========S%d LOK ==========\n", Path));
		
	IQK_CMD = 0xf8000008|(1<<(4+Path));
			
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE,("[IQK]LOK_Trigger = 0x%x\n", IQK_CMD));

	ODM_Write4Byte(pDM_Odm, 0x1b00, IQK_CMD);
	ODM_Write4Byte(pDM_Odm, 0x1b00, IQK_CMD+1);
	/*LOK: CMD ID = 0	{0xf8000018, 0xf8000028}*/
	/*LOK: CMD ID = 0	{0xf8000019, 0xf8000029}*/
	ODM_delay_ms(LOK_delay_8822B);

	delay_count = 0;
	LOK_notready = TRUE;
		
	while (LOK_notready) {
		if (ODM_Read4Byte(pDM_Odm, 0x1b00) == (IQK_CMD & 0xffffff0f))
			LOK_notready = FALSE;
		else
			LOK_notready = TRUE;
		
		if (LOK_notready) {
			ODM_delay_ms(1);
			delay_count++;
		}

		if (delay_count >= 50) {
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, 
				("[IQK]S%d LOK timeout!!!\n", Path));
			break;
		}
	}

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, 
		("[IQK]S%d ==> delay_count = 0x%d\n", Path, delay_count));
	if (ODM_COMP_CALIBRATION) {	
	if (!LOK_notready) {
			LOK_temp = ODM_GetRFReg(pDM_Odm, (ODM_RF_RADIO_PATH_E)Path, 0x58, bRFRegOffsetMask);
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, ("[IQK]0x58 = 0x%x\n", LOK_temp));
	} else {
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, ("[IQK]==>S%d LOK Fail!!!\n", Path));
		}
	}
	pIQK_info->LOK_fail[Path] = LOK_notready;
	return LOK_notready;
}




BOOLEAN
_IQK_One_Shot_8822B(
	IN	PVOID		pDM_VOID,
	u1Byte		Path,
	u1Byte 		idx
)
{	
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PIQK_INFO	pIQK_info = &pDM_Odm->IQK_info;
	u1Byte		delay_count = 0;
	BOOLEAN		notready = TRUE, fail = TRUE, search_fail = TRUE;
	u4Byte		IQK_CMD = 0x0, tmp;
	u2Byte		IQK_Apply[2]	= {0xc94, 0xe94};

	if (idx == TXIQK)
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]============ S%d WBTXIQK ============\n", Path));
	else if (idx == RXIQK1)
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]============ S%d WBRXIQK STEP1============\n", Path));
	else
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]============ S%d WBRXIQK STEP2============\n", Path));
	
	if (idx == TXIQK) {
		IQK_CMD = 0xf8000008 | ((*pDM_Odm->pBandWidth + 4) << 8) | (1 << (Path + 4));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, ("[IQK]TXK_Trigger = 0x%x\n", IQK_CMD));
						/*{0xf8000418, 0xf800042a} ==> 20 WBTXK (CMD = 4)*/
						/*{0xf8000518, 0xf800052a} ==> 40 WBTXK (CMD = 5)*/
						/*{0xf8000618, 0xf800062a} ==> 80 WBTXK (CMD = 6)*/
	} else if (idx == RXIQK1) {
		if (*pDM_Odm->pBandWidth == 2)
			IQK_CMD = 0xf8000808 | (1 << (Path + 4));
		else
			IQK_CMD = 0xf8000708 | (1 << (Path + 4));			
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, ("[IQK]RXK1_Trigger = 0x%x\n", IQK_CMD));
						/*{0xf8000718, 0xf800072a} ==> 20 WBTXK (CMD = 7)*/
						/*{0xf8000718, 0xf800072a} ==> 40 WBTXK (CMD = 7)*/
						/*{0xf8000818, 0xf800082a} ==> 80 WBTXK (CMD = 8)*/
	} else if (idx == RXIQK2) {
		IQK_CMD = 0xf8000008 | ((*pDM_Odm->pBandWidth + 9) << 8) | (1 << (Path+4));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, ("[IQK]RXK2_Trigger = 0x%x\n", IQK_CMD));
						/*{0xf8000918, 0xf800092a} ==> 20 WBRXK (CMD = 9)*/
						/*{0xf8000a18, 0xf8000a2a} ==> 40 WBRXK (CMD = 10)*/
						/*{0xf8000b18, 0xf8000b2a} ==> 80 WBRXK (CMD = 11)*/
		ODM_Write4Byte(pDM_Odm, 0x1b00, 0xf8000008 | Path << 1);			
		ODM_Write4Byte(pDM_Odm, 0x1b24, (ODM_Read4Byte(pDM_Odm, 0x1b24) & 0xffffe3ff) | ((pIQK_info->lna_idx & 0x7) << 10));
	}

	ODM_Write4Byte(pDM_Odm, 0x1b00, IQK_CMD);
	ODM_Write4Byte(pDM_Odm, 0x1b00, IQK_CMD+0x1);
	ODM_delay_ms(WBIQK_delay_8822B);
			
	while (notready) {
		if (ODM_Read4Byte(pDM_Odm, 0x1b00) == (IQK_CMD & 0xffffff0f))
			notready = FALSE;
		else
			notready = TRUE;
		
		if (notready) {
			ODM_delay_ms(1);
			delay_count++;
		} else {
				fail = (BOOLEAN) ODM_GetBBReg(pDM_Odm, 0x1b08, BIT26);
			break;
		}

		if (delay_count >= 50) {
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, 
				("[IQK]S%d IQK timeout!!!\n", Path));
			break;
		}
	}

	if (pDM_Odm->DebugComponents && ODM_COMP_CALIBRATION) {
		ODM_Write4Byte(pDM_Odm, 0x1b00, 0xf8000008 | Path << 1);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, 
			("[IQK]S%d ==> 0x1b00 = 0x%x, 0x1b08 = 0x%x\n", Path, ODM_Read4Byte(pDM_Odm, 0x1b00), ODM_Read4Byte(pDM_Odm, 0x1b08)));
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, 
				("[IQK]S%d ==> delay_count = 0x%d\n", Path, delay_count));
		if (idx != TXIQK) 
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, 
				("[IQK]S%d ==> RF0x0 = 0x%x, RF0x56 = 0x%x\n", Path, ODM_GetRFReg(pDM_Odm, Path, 0x0, bRFRegOffsetMask), ODM_GetRFReg(pDM_Odm, Path, 0x56, bRFRegOffsetMask)));
	}

	ODM_Write4Byte(pDM_Odm, 0x1b00, 0xf8000008 | Path << 1);

	if (idx == RXIQK2) {
		pIQK_info->RXIQK_AGC[0][Path] = ODM_GetRFReg(pDM_Odm, Path, 0x0, bRFRegOffsetMask) >> 4;
		ODM_Write4Byte(pDM_Odm, 0x1b38, 0x20000000);

		if (!fail)												/*RXIQK success*/
			ODM_SetBBReg(pDM_Odm, IQK_Apply[Path], (BIT11|BIT10), 0x1);
		else
			ODM_SetBBReg(pDM_Odm, IQK_Apply[Path], (BIT11|BIT10), 0x0);
	}
	
	if (idx == TXIQK)
		pIQK_info->IQK_fail_report[0][Path][TXIQK] = fail;
	else
		pIQK_info->IQK_fail_report[0][Path][RXIQK] = fail;

	return fail;
}


BOOLEAN
_IQK_RXIQKbyPath_8822B(
	IN	PVOID		pDM_VOID,
	u1Byte		Path
)
{	
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PIQK_INFO	pIQK_info = &pDM_Odm->IQK_info;
	BOOLEAN		KFAIL = TRUE, gonext;
	u1Byte		i;

#if 1
/*while (1) {*/
		switch (pIQK_info->rxiqk_step) {
		case 1:		/*gain search_RXK1*/
			_IQK_RXK1Setting_8822B(pDM_Odm, Path);
			gonext = FALSE;
			while (1) {
				KFAIL = _IQK_RXIQK_GainSearchFail_8822B(pDM_Odm, Path, RXIQK1);
				if (KFAIL && (pIQK_info->gs_retry_count[0][Path][RXIQK1] < 2))
					pIQK_info->gs_retry_count[0][Path][RXIQK1]++;
				else if (KFAIL) {
					pIQK_info->RXIQK_fail_code[0][Path] = 0;
					pIQK_info->rxiqk_step = 5;
					gonext = TRUE;
				} else {
					pIQK_info->rxiqk_step++;
					gonext = TRUE;
				}

				if (gonext)
					break;
			}
			break;
		case 2:		/*gain search_RXK2*/
			_IQK_RXK2Setting_8822B(pDM_Odm, Path);
			while (1) {
				KFAIL = _IQK_RXIQK_GainSearchFail_8822B(pDM_Odm, Path, RXIQK2);
				if (KFAIL && (pIQK_info->gs_retry_count[0][Path][RXIQK2] < 2))
					pIQK_info->gs_retry_count[0][Path][RXIQK2]++;
				else {
					pIQK_info->rxiqk_step++;
					break;
				}
			}
			break;		
		case 3:		/*RXK1*/
			_IQK_RXK1Setting_8822B(pDM_Odm, Path);
			gonext = FALSE;
			while (1) {
				KFAIL = _IQK_One_Shot_8822B(pDM_Odm, Path, RXIQK1);
				if (KFAIL && (pIQK_info->retry_count[0][Path][RXIQK1] < 2))
					pIQK_info->retry_count[0][Path][RXIQK1]++;
				else if (KFAIL) {
					pIQK_info->RXIQK_fail_code[0][Path] = 1;
					pIQK_info->rxiqk_step = 5;
					gonext = TRUE;
				} else {
					pIQK_info->rxiqk_step++;
					gonext = TRUE;
				}
				if (gonext)
					break;
			}
			break;	
		case 4:		/*RXK2*/
			_IQK_RXK2Setting_8822B(pDM_Odm, Path);
			gonext = FALSE;
			while (1) {
				KFAIL = _IQK_One_Shot_8822B(pDM_Odm, Path,	RXIQK2);
				if (KFAIL && (pIQK_info->retry_count[0][Path][RXIQK2] < 2))
					pIQK_info->retry_count[0][Path][RXIQK2]++;
				else if (KFAIL) {
					pIQK_info->RXIQK_fail_code[0][Path] = 2;
					pIQK_info->rxiqk_step = 5;
					gonext = TRUE;
				} else {
					pIQK_info->rxiqk_step++;
					gonext = TRUE;
				}
				if (gonext)
					break;
			}
			break;
		}

	return KFAIL;

#endif
}


VOID
_IQK_IQKbyPath_8822B(
	IN	PVOID		pDM_VOID
)
{	
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PIQK_INFO	pIQK_info = &pDM_Odm->IQK_info;
	BOOLEAN		KFAIL = TRUE;
	u1Byte		i;

/*	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, ("[IQK]IQKstep = 0x%x\n", pDM_Odm->RFCalibrateInfo.IQKstep)); */
#if 1
	switch (pDM_Odm->RFCalibrateInfo.IQKstep) {
	case 1:		/*S0 LOK*/
#if 1
		_IQK_LOKSetting_8822B(pDM_Odm, ODM_RF_PATH_A);
		_LOK_One_Shot_8822B(pDM_Odm, ODM_RF_PATH_A);
#endif
		pDM_Odm->RFCalibrateInfo.IQKstep++;
		break;
	case 2:		/*S1 LOK*/
#if 1
		_IQK_LOKSetting_8822B(pDM_Odm, ODM_RF_PATH_B);
		_LOK_One_Shot_8822B(pDM_Odm, ODM_RF_PATH_B);
#endif
		pDM_Odm->RFCalibrateInfo.IQKstep++;
		break;		
	case 3:		/*S0 TXIQK*/
#if 1
		_IQK_TXKSetting_8822B(pDM_Odm, ODM_RF_PATH_A);
		KFAIL = _IQK_One_Shot_8822B(pDM_Odm, ODM_RF_PATH_A, TXIQK);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, ("[IQK]S0TXK KFail = 0x%x\n", KFAIL));
		
		if (KFAIL && (pIQK_info->retry_count[0][ODM_RF_PATH_A][TXIQK] < 3))
			pIQK_info->retry_count[0][ODM_RF_PATH_A][TXIQK]++;
		else
#endif	
			pDM_Odm->RFCalibrateInfo.IQKstep++;
		break;	
	case 4:		/*S1 TXIQK*/
#if 1
		_IQK_TXKSetting_8822B(pDM_Odm, ODM_RF_PATH_B);
		KFAIL = _IQK_One_Shot_8822B(pDM_Odm, ODM_RF_PATH_B,	TXIQK);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, ("[IQK]S1TXK KFail = 0x%x\n", KFAIL));

		if (KFAIL && pIQK_info->retry_count[0][ODM_RF_PATH_B][TXIQK] < 3)
			pIQK_info->retry_count[0][ODM_RF_PATH_B][TXIQK]++;
		else
#endif
			pDM_Odm->RFCalibrateInfo.IQKstep++;
		break;	
	case 5:		/*S0 RXIQK*/
		KFAIL = _IQK_RXIQKbyPath_8822B(pDM_Odm, ODM_RF_PATH_A);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, ("[IQK]S0RXK KFail = 0x%x\n", KFAIL));
		if (pIQK_info->rxiqk_step == 5) {
			pDM_Odm->RFCalibrateInfo.IQKstep++;
			pIQK_info->rxiqk_step = 1;
			if (KFAIL)
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, 
					("[IQK]S0RXK fail code: %d!!!\n", pIQK_info->RXIQK_fail_code[0][ODM_RF_PATH_A]));
		}
		break;	
	case 6:		/*S1 RXIQK*/
		KFAIL = _IQK_RXIQKbyPath_8822B(pDM_Odm, ODM_RF_PATH_B);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, ("[IQK]S1RXK KFail = 0x%x\n", KFAIL));
		if (pIQK_info->rxiqk_step == 5) {
			pDM_Odm->RFCalibrateInfo.IQKstep++;
			pIQK_info->rxiqk_step = 1;
			if (KFAIL)
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, 
					("[IQK]S1RXK fail code: %d!!!\n", pIQK_info->RXIQK_fail_code[0][ODM_RF_PATH_B]));
			ODM_Write4Byte(pDM_Odm, 0x1bcc, 0x0);
		}
		break;
	}

	if (pDM_Odm->RFCalibrateInfo.IQKstep == 7) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE,
			("[IQK]==========LOK summary ==========\n"));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
			("[IQK]PathA_LOK_notready = %d, PathB_LOK1_notready = %d\n", 
			pIQK_info->LOK_fail[ODM_RF_PATH_A], pIQK_info->LOK_fail[ODM_RF_PATH_B]));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE,
			("[IQK]==========IQK summary ==========\n"));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
			("[IQK]PathA_TXIQK_fail = %d, PathB_TXIQK_fail = %d\n", 
			pIQK_info->IQK_fail_report[0][ODM_RF_PATH_A][TXIQK], pIQK_info->IQK_fail_report[0][ODM_RF_PATH_B][TXIQK]));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
			("[IQK]PathA_RXIQK_fail = %d, PathB_RXIQK_fail = %d\n", 
			pIQK_info->IQK_fail_report[0][ODM_RF_PATH_A][RXIQK], pIQK_info->IQK_fail_report[0][ODM_RF_PATH_B][RXIQK]));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
			("[IQK]PathA_TXIQK_retry = %d, PathB_TXIQK_retry = %d\n", 
			pIQK_info->retry_count[0][ODM_RF_PATH_A][TXIQK], pIQK_info->retry_count[0][ODM_RF_PATH_B][TXIQK]));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
			("[IQK]PathA_RXK1_retry = %d, PathA_RXK2_retry = %d, PathB_RXK1_retry = %d, PathB_RXK2_retry = %d\n", 
			pIQK_info->retry_count[0][ODM_RF_PATH_A][RXIQK1], pIQK_info->retry_count[0][ODM_RF_PATH_A][RXIQK2],
			pIQK_info->retry_count[0][ODM_RF_PATH_B][RXIQK1], pIQK_info->retry_count[0][ODM_RF_PATH_B][RXIQK2]));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
			("[IQK]PathA_GS1_retry = %d, PathA_GS2_retry = %d, PathB_GS1_retry = %d, PathB_GS2_retry = %d\n", 
			pIQK_info->gs_retry_count[0][ODM_RF_PATH_A][RXIQK1], pIQK_info->gs_retry_count[0][ODM_RF_PATH_A][RXIQK2],
			pIQK_info->gs_retry_count[0][ODM_RF_PATH_B][RXIQK1], pIQK_info->gs_retry_count[0][ODM_RF_PATH_B][RXIQK2]));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE,
			("[IQK]================================\n"));
		
			for (i = 0; i < 2; i++) {
				ODM_Write4Byte(pDM_Odm, 0x1b00, 0xf8000008 | i << 1);		
				ODM_Write4Byte(pDM_Odm, 0x1b2c, 0x7);	
				ODM_Write4Byte(pDM_Odm, 0x1bcc, 0x0);
			}
	}
#endif
}

VOID
_IQK_StartIQK_8822B(
	IN PDM_ODM_T		pDM_Odm
	)
{	
	u4Byte tmp;
	
	/*GNT_WL = 1*/
	tmp = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x1, bRFRegOffsetMask);
	tmp = tmp|BIT5|BIT0;
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x1, bRFRegOffsetMask, tmp);	

	tmp = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x1, bRFRegOffsetMask);
	tmp = tmp|BIT5|BIT0;
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x1, bRFRegOffsetMask, tmp);	

	_IQK_IQKbyPath_8822B(pDM_Odm);


}

VOID
_IQCalibrate_8822B_Init(
	IN	PVOID		pDM_VOID
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PIQK_INFO	pIQK_info = &pDM_Odm->IQK_info;
	u1Byte	i, j, k, m;

	if (pIQK_info->IQKtimes == 0) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]=====>PHY_IQCalibrate_8822B_Init\n"));

		for (i = 0; i < SS_8822B; i++) {
			for (j = 0; j < 2; j++) {
				pIQK_info->LOK_fail[i] = TRUE;
				pIQK_info->IQK_fail[j][i] = TRUE;
				pIQK_info->IQC_Matrix[j][i] = 0x20000000;
			}
		}

		for (i = 0; i < 2; i++) {
			pIQK_info->IQK_Channel[i] = 0x0;

			for (j = 0; j < SS_8822B; j++) {
				pIQK_info->LOK_IDAC[i][j] = 0x0;
				pIQK_info->RXIQK_AGC[i][j] = 0x0;
				pIQK_info->bypassIQK[i][j] = 0x0;
				
				for (k = 0; k < 2; k++) {
					pIQK_info->IQK_fail_report[i][j][k] = TRUE;
					for (m = 0; m < 8; m++) {
						pIQK_info->IQK_CFIR_real[i][j][k][m] = 0x0;
						pIQK_info->IQK_CFIR_imag[i][j][k][m] = 0x0;
					}
				}

				for (k = 0; k < 3; k++)
					pIQK_info->retry_count[i][j][k] = 0x0;
				
			}
		}
	}
}


VOID	
phy_IQCalibrate_8822B(
	IN PDM_ODM_T		pDM_Odm,
	IN	BOOLEAN			reset
	)
{

	u4Byte	MAC_backup[MAC_REG_NUM_8822B], BB_backup[BB_REG_NUM_8822B], RF_backup[RF_REG_NUM_8822B][SS_8822B];
	u4Byte 	Backup_MAC_REG[MAC_REG_NUM_8822B] = {0x520, 0x550}; 
	u4Byte	Backup_BB_REG[BB_REG_NUM_8822B] = {0x808, 0x90c, 0xc00, 0xcb0, 0xcb4, 0xcbc, 0xe00, 0xeb0, 0xeb4, 0xebc, 0x1990, 0x9a4, 0xa04}; 
	u4Byte	Backup_RF_REG[RF_REG_NUM_8822B] = {0xdf, 0x8f, 0x65, 0x0, 0x1}; 
	u1Byte	i, j;

	PIQK_INFO	pIQK_info = &pDM_Odm->IQK_info;

	if (!pDM_Odm->mp_mode)
		if (_IQK_ReloadIQK_8822B(pDM_Odm, reset))
			return;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, 
		("[IQK]==========IQK strat!!!!!==========\n"));

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, 
		("[IQK]pBandType = %s, BandWidth = %d, ExtPA2G = %d, ExtPA5G = %d\n", (*pDM_Odm->pBandType == ODM_BAND_5G) ? "5G" : "2G", *pDM_Odm->pBandWidth, pDM_Odm->ExtPA, pDM_Odm->ExtPA5G));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, 
		("[IQK]Interface = %d, CutVersion = %x\n", pDM_Odm->SupportInterface, pDM_Odm->CutVersion));
	
	pIQK_info->IQKtimes++;
	
	pDM_Odm->RFCalibrateInfo.IQK_TotalProgressingTime = 0;
	pDM_Odm->RFCalibrateInfo.IQKstep = 1;
	pIQK_info->rxiqk_step = 1;

	_IQK_backupIQK_8822B(pDM_Odm, 0);
	_IQK_BackupMacBB_8822B(pDM_Odm, MAC_backup, BB_backup, Backup_MAC_REG, Backup_BB_REG);
	_IQK_BackupRF_8822B(pDM_Odm, RF_backup, Backup_RF_REG);

	_IQK_ConfigureMACBB_8822B(pDM_Odm);
	_IQK_AFESetting_8822B(pDM_Odm,TRUE);
	_IQK_RFESetting_8822B(pDM_Odm, FALSE);
	_IQK_AGCbnd_int_8822B(pDM_Odm);
	_IQK_RFSetting_8822B(pDM_Odm);

	while (1) {
		if (!pDM_Odm->mp_mode)	
			pDM_Odm->RFCalibrateInfo.IQK_StartTime = ODM_GetCurrentTime(pDM_Odm);

		_IQK_StartIQK_8822B(pDM_Odm);

		if (!pDM_Odm->mp_mode) {
			pDM_Odm->RFCalibrateInfo.IQK_ProgressingTime = ODM_GetProgressingTime(pDM_Odm, pDM_Odm->RFCalibrateInfo.IQK_StartTime);
			pDM_Odm->RFCalibrateInfo.IQK_TotalProgressingTime += ODM_GetProgressingTime(pDM_Odm, pDM_Odm->RFCalibrateInfo.IQK_StartTime);
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  
				("[IQK]IQK ProgressingTime = %lld ms\n", pDM_Odm->RFCalibrateInfo.IQK_ProgressingTime));
		}
	
		if (pDM_Odm->RFCalibrateInfo.IQKstep == 7)
			break;
	};

	_IQK_backupIQK_8822B(pDM_Odm, 1);
	_IQK_AFESetting_8822B(pDM_Odm,FALSE);
	_IQK_RestoreMacBB_8822B(pDM_Odm, MAC_backup, BB_backup, Backup_MAC_REG, Backup_BB_REG);
	_IQK_RestoreRF_8822B(pDM_Odm, Backup_RF_REG, RF_backup);
	_IQK_Fill_IQK_Report_8822B(pDM_Odm, 0);

	pDM_Odm->RFCalibrateInfo.IQK_TotalProgressingTime += ODM_GetProgressingTime(pDM_Odm, pDM_Odm->RFCalibrateInfo.IQK_StartTime);	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  
		("[IQK]Total IQK ProgressingTime = %lld ms\n", pDM_Odm->RFCalibrateInfo.IQK_TotalProgressingTime));

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, 
	("[IQK]==========IQK end!!!!!==========\n"));
}


VOID
phy_IQCalibrate_By_FW_8822B(
	IN	PVOID		pDM_VOID,
	IN	u1Byte		clear
	)
{
}


/*IQK version:v3.2 , NCTL v0.6*/
/*1.add LNA boundary of RX gain search*/
VOID
PHY_IQCalibrate_8822B(
	IN	PVOID		pDM_VOID,
	IN	BOOLEAN		clear
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;

	u4Byte counter = 0x0;
	
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	PADAPTER 		pAdapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);	
	
	#if (MP_DRIVER == 1)
		#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)	
			PMPT_CONTEXT	pMptCtx = &(pAdapter->MptCtx);	
		#else
			PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.MptCtx);		
		#endif	
	#endif
	#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
		if (ODM_CheckPowerStatus(pAdapter) == FALSE)
			return;
	#endif

	#if MP_DRIVER == 1	
		if( pMptCtx->bSingleTone || pMptCtx->bCarrierSuppression )
			return;
	#endif
	
#endif

	pDM_Odm->IQKFWOffload = 0;

/*FW IQK*/
	if (pDM_Odm->IQKFWOffload) {
	if ( ! pDM_Odm->RFCalibrateInfo.bIQKInProgress) {
		ODM_AcquireSpinLock(pDM_Odm, RT_IQK_SPINLOCK);
		pDM_Odm->RFCalibrateInfo.bIQKInProgress = TRUE;
		ODM_ReleaseSpinLock(pDM_Odm, RT_IQK_SPINLOCK);
	
			pDM_Odm->RFCalibrateInfo.IQK_StartTime = ODM_GetCurrentTime(pDM_Odm);

			ODM_Write4Byte(pDM_Odm, 0x1b00, 0xf8000008);			
			ODM_SetBBReg(pDM_Odm, 0x1bf0, 0xff000000, 0xff);			
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, 
				("[IQK]0x1bf0 = 0x%x\n", ODM_Read4Byte(pDM_Odm, 0x1bf0)));

			phy_IQCalibrate_By_FW_8822B(pDM_Odm, clear);
			
			while (1) {
				if (((ODM_Read4Byte(pDM_Odm, 0x1bf0) >> 24) == 0x7f) || (counter > 300))
					break;
			
				counter++;				
				ODM_delay_ms(1);
			};

			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE, 
				("[IQK]counter = %d\n", counter));		
			
			pDM_Odm->RFCalibrateInfo.IQK_ProgressingTime = ODM_GetProgressingTime(pDM_Odm, pDM_Odm->RFCalibrateInfo.IQK_StartTime);

			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("[IQK]IQK ProgressingTime = %lld ms\n", pDM_Odm->RFCalibrateInfo.IQK_ProgressingTime));
			
				ODM_AcquireSpinLock(pDM_Odm, RT_IQK_SPINLOCK);
				pDM_Odm->RFCalibrateInfo.bIQKInProgress = FALSE;
				ODM_ReleaseSpinLock(pDM_Odm, RT_IQK_SPINLOCK);
		}	else
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("== Return the IQK CMD, because the IQK in Progress ==\n"));

	}
	else {	

	_IQCalibrate_8822B_Init(pDM_VOID);

	if (!pDM_Odm->RFCalibrateInfo.bIQKInProgress) {
		
		ODM_AcquireSpinLock(pDM_Odm, RT_IQK_SPINLOCK);
		pDM_Odm->RFCalibrateInfo.bIQKInProgress = TRUE;
		ODM_ReleaseSpinLock(pDM_Odm, RT_IQK_SPINLOCK);
		if (pDM_Odm->mp_mode)	
			pDM_Odm->RFCalibrateInfo.IQK_StartTime = ODM_GetCurrentTime(pDM_Odm);
		
	#if (DM_ODM_SUPPORT_TYPE & (ODM_CE))
		phy_IQCalibrate_8822B(pDM_Odm, clear);
		/*DBG_871X("%s,%d, do IQK %u ms\n", __func__, __LINE__, rtw_get_passing_time_ms(time_iqk));*/
	#else
		phy_IQCalibrate_8822B(pDM_Odm, clear);
	#endif
		if (pDM_Odm->mp_mode) {
			pDM_Odm->RFCalibrateInfo.IQK_ProgressingTime = ODM_GetProgressingTime(pDM_Odm, pDM_Odm->RFCalibrateInfo.IQK_StartTime);
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("[IQK]IQK ProgressingTime = %lld ms\n", pDM_Odm->RFCalibrateInfo.IQK_ProgressingTime));
		}
		ODM_AcquireSpinLock(pDM_Odm, RT_IQK_SPINLOCK);
		pDM_Odm->RFCalibrateInfo.bIQKInProgress = FALSE;
		ODM_ReleaseSpinLock(pDM_Odm, RT_IQK_SPINLOCK);
	}
	else{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]== Return the IQK CMD, because the IQK in Progress ==\n"));
	}

}

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	_IQK_IQK_failReport_8822B(pDM_Odm);
#endif

}

#endif


