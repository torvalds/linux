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
 ******************************************************************************/
/******************************************************************************
 *
 *
 * Module:	rtl8192c_rf6052.c	(Source C File)
 *
 * Note:	Provide RF 6052 series relative API.
 *
 * Function:
 *
 * Export:
 *
 * Abbrev:
 *
 * History:
 * Data			Who		Remark
 *
 * 09/25/2008	MHC		Create initial version.
 * 11/05/2008	MHC		Add API for tw power setting.
 *
 *
******************************************************************************/

#define _RTL8723A_RF6052_C_

#include <osdep_service.h>
#include <drv_types.h>

#include <rtl8723a_hal.h>
#include <usb_ops_linux.h>

/*-----------------------------------------------------------------------------
 * Function:    PHY_RF6052SetBandwidth()
 *
 * Overview:    This function is called by SetBWMode23aCallback8190Pci() only
 *
 * Input:       struct rtw_adapter *				Adapter
 *			WIRELESS_BANDWIDTH_E	Bandwidth	20M or 40M
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Note:		For RF type 0222D
 *---------------------------------------------------------------------------*/
void rtl8723a_phy_rf6052set_bw(struct rtw_adapter *Adapter,
			       enum ht_channel_width Bandwidth)	/* 20M or 40M */
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);

	switch (Bandwidth) {
	case HT_CHANNEL_WIDTH_20:
		pHalData->RfRegChnlVal[0] =
			(pHalData->RfRegChnlVal[0] & 0xfffff3ff) | 0x0400;
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask,
			     pHalData->RfRegChnlVal[0]);
		break;
	case HT_CHANNEL_WIDTH_40:
		pHalData->RfRegChnlVal[0] =
			(pHalData->RfRegChnlVal[0] & 0xfffff3ff);
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask,
			     pHalData->RfRegChnlVal[0]);
		break;
	default:
		break;
	}
}

/*-----------------------------------------------------------------------------
 * Function:	PHY_RF6052SetCckTxPower
 *
 * Overview:
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/05/2008	MHC		Simulate 8192series..
 *
 *---------------------------------------------------------------------------*/

void rtl823a_phy_rf6052setccktxpower(struct rtw_adapter *Adapter,
				     u8 *pPowerlevel)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv *pdmpriv = &pHalData->dmpriv;
	struct mlme_ext_priv *pmlmeext = &Adapter->mlmeextpriv;
	u32 TxAGC[2] = {0, 0}, tmpval = 0;
	bool TurboScanOff = false;
	u8 idx1, idx2;
	u8 *ptr;

	/*  According to SD3 eechou's suggestion, we need to disable
	    turbo scan for RU. */
	/*  Otherwise, external PA will be broken if power index > 0x20. */
	if (pHalData->EEPROMRegulatory != 0 || pHalData->ExternalPA)
		TurboScanOff = true;

	if (pmlmeext->sitesurvey_res.state == SCAN_PROCESS) {
		TxAGC[RF_PATH_A] = 0x3f3f3f3f;
		TxAGC[RF_PATH_B] = 0x3f3f3f3f;

		TurboScanOff = true;/* disable turbo scan */

		if (TurboScanOff) {
			for (idx1 = RF_PATH_A; idx1 <= RF_PATH_B; idx1++) {
				TxAGC[idx1] = pPowerlevel[idx1] |
					(pPowerlevel[idx1] << 8) |
					(pPowerlevel[idx1] << 16) |
					(pPowerlevel[idx1] << 24);
				/*  2010/10/18 MH For external PA module.
				    We need to limit power index to be less
				    than 0x20. */
				if (TxAGC[idx1] > 0x20 && pHalData->ExternalPA)
					TxAGC[idx1] = 0x20;
			}
		}
	} else {
/*  20100427 Joseph: Driver dynamic Tx power shall not affect Tx
 *  power. It shall be determined by power training mechanism. */
/*  Currently, we cannot fully disable driver dynamic tx power
 *  mechanism because it is referenced by BT coexist mechanism. */
/*  In the future, two mechanism shall be separated from each other
 *  and maintained independantly. Thanks for Lanhsin's reminder. */
		if (pdmpriv->DynamicTxHighPowerLvl == TxHighPwrLevel_Level1) {
			TxAGC[RF_PATH_A] = 0x10101010;
			TxAGC[RF_PATH_B] = 0x10101010;
		} else if (pdmpriv->DynamicTxHighPowerLvl ==
			   TxHighPwrLevel_Level2) {
			TxAGC[RF_PATH_A] = 0x00000000;
			TxAGC[RF_PATH_B] = 0x00000000;
		} else {
			for (idx1 = RF_PATH_A; idx1 <= RF_PATH_B; idx1++) {
				TxAGC[idx1] = pPowerlevel[idx1] |
					(pPowerlevel[idx1] << 8) |
					(pPowerlevel[idx1] << 16) |
					(pPowerlevel[idx1] << 24);
			}

			if (pHalData->EEPROMRegulatory == 0) {
				tmpval = (pHalData->MCSTxPowerLevelOriginalOffset[0][6]) +
						(pHalData->MCSTxPowerLevelOriginalOffset[0][7]<<8);
				TxAGC[RF_PATH_A] += tmpval;

				tmpval = (pHalData->MCSTxPowerLevelOriginalOffset[0][14]) +
						(pHalData->MCSTxPowerLevelOriginalOffset[0][15]<<24);
				TxAGC[RF_PATH_B] += tmpval;
			}
		}
	}

	for (idx1 = RF_PATH_A; idx1 <= RF_PATH_B; idx1++) {
		ptr = (u8 *)(&TxAGC[idx1]);
		for (idx2 = 0; idx2 < 4; idx2++) {
			if (*ptr > RF6052_MAX_TX_PWR)
				*ptr = RF6052_MAX_TX_PWR;
			ptr++;
		}
	}

	/*  rf-A cck tx power */
	tmpval = TxAGC[RF_PATH_A] & 0xff;
	PHY_SetBBReg(Adapter, rTxAGC_A_CCK1_Mcs32, bMaskByte1, tmpval);
	tmpval = TxAGC[RF_PATH_A] >> 8;
	PHY_SetBBReg(Adapter, rTxAGC_B_CCK11_A_CCK2_11, 0xffffff00, tmpval);

	/*  rf-B cck tx power */
	tmpval = TxAGC[RF_PATH_B] >> 24;
	PHY_SetBBReg(Adapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte0, tmpval);
	tmpval = TxAGC[RF_PATH_B] & 0x00ffffff;
	PHY_SetBBReg(Adapter, rTxAGC_B_CCK1_55_Mcs32, 0xffffff00, tmpval);
}	/* PHY_RF6052SetCckTxPower */

/*  powerbase0 for OFDM rates */
/*  powerbase1 for HT MCS rates */
static void getPowerBase(struct rtw_adapter *Adapter, u8 *pPowerLevel,
			 u8 Channel, u32 *OfdmBase, u32 *MCSBase)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	u32 powerBase0, powerBase1;
	u8 Legacy_pwrdiff = 0;
	s8 HT20_pwrdiff = 0;
	u8 i, powerlevel[2];

	for (i = 0; i < 2; i++) {
		powerlevel[i] = pPowerLevel[i];
		Legacy_pwrdiff = pHalData->TxPwrLegacyHtDiff[i][Channel-1];
		powerBase0 = powerlevel[i] + Legacy_pwrdiff;

		powerBase0 = powerBase0 << 24 | powerBase0 << 16 |
			powerBase0 << 8 | powerBase0;
		*(OfdmBase + i) = powerBase0;
	}

	for (i = 0; i < 2; i++) {
		/* Check HT20 to HT40 diff */
		if (pHalData->CurrentChannelBW == HT_CHANNEL_WIDTH_20) {
			HT20_pwrdiff = pHalData->TxPwrHt20Diff[i][Channel-1];
			powerlevel[i] += HT20_pwrdiff;
		}
		powerBase1 = powerlevel[i];
		powerBase1 = powerBase1 << 24 | powerBase1 << 16 |
			powerBase1 << 8 | powerBase1;
		*(MCSBase + i) = powerBase1;
	}
}

static void
getTxPowerWriteValByRegulatory(struct rtw_adapter *Adapter, u8 Channel,
			       u8 index, u32 *powerBase0, u32 *powerBase1,
			       u32 *pOutWriteVal)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u8 i, chnlGroup = 0, pwr_diff_limit[4];
	u32 writeVal, customer_limit, rf;

	/*  Index 0 & 1 = legacy OFDM, 2-5 = HT_MCS rate */
	for (rf = 0; rf < 2; rf++) {
		switch (pHalData->EEPROMRegulatory) {
		case 0:	/*  Realtek better performance */
			/*  increase power diff defined by Realtek for
			 *  large power */
			chnlGroup = 0;
			writeVal = pHalData->MCSTxPowerLevelOriginalOffset[chnlGroup][index+(rf?8:0)] +
				((index < 2) ? powerBase0[rf] : powerBase1[rf]);
			break;
		case 1:	/*  Realtek regulatory */
			/*  increase power diff defined by Realtek for
			 *  regulatory */
			if (pHalData->pwrGroupCnt == 1)
				chnlGroup = 0;
			if (pHalData->pwrGroupCnt >= 3) {
				if (Channel <= 3)
					chnlGroup = 0;
				else if (Channel >= 4 && Channel <= 9)
					chnlGroup = 1;
				else if (Channel > 9)
					chnlGroup = 2;

				if (pHalData->CurrentChannelBW ==
				    HT_CHANNEL_WIDTH_20)
					chnlGroup++;
				else
					chnlGroup += 4;
			}
			writeVal = pHalData->MCSTxPowerLevelOriginalOffset[chnlGroup][index+(rf?8:0)] +
				   ((index < 2) ? powerBase0[rf] :
				    powerBase1[rf]);
			break;
		case 2:	/*  Better regulatory */
			/*  don't increase any power diff */
			writeVal = (index < 2) ? powerBase0[rf] :
				    powerBase1[rf];
			break;
		case 3:	/*  Customer defined power diff. */
			chnlGroup = 0;

			for (i = 0; i < 4; i++) {
				pwr_diff_limit[i] = (u8)((pHalData->MCSTxPowerLevelOriginalOffset[chnlGroup][index +
						    (rf ? 8 : 0)]&(0x7f << (i*8))) >> (i*8));
				if (pHalData->CurrentChannelBW == HT_CHANNEL_WIDTH_40) {
					if (pwr_diff_limit[i] > pHalData->PwrGroupHT40[rf][Channel-1])
						pwr_diff_limit[i] = pHalData->PwrGroupHT40[rf][Channel-1];
				} else {
					if (pwr_diff_limit[i] > pHalData->PwrGroupHT20[rf][Channel-1])
						pwr_diff_limit[i] = pHalData->PwrGroupHT20[rf][Channel-1];
				}
			}
			customer_limit = (pwr_diff_limit[3]<<24) | (pwr_diff_limit[2]<<16) |
							(pwr_diff_limit[1]<<8) | (pwr_diff_limit[0]);
			writeVal = customer_limit + ((index<2)?powerBase0[rf]:powerBase1[rf]);
			break;
		default:
			chnlGroup = 0;
			writeVal = pHalData->MCSTxPowerLevelOriginalOffset[chnlGroup][index+(rf?8:0)] +
					((index < 2) ? powerBase0[rf] : powerBase1[rf]);
			break;
		}

/*  20100427 Joseph: Driver dynamic Tx power shall not affect Tx power.
    It shall be determined by power training mechanism. */
/*  Currently, we cannot fully disable driver dynamic tx power mechanism
    because it is referenced by BT coexist mechanism. */
/*  In the future, two mechanism shall be separated from each other and
    maintained independantly. Thanks for Lanhsin's reminder. */

		if (pdmpriv->DynamicTxHighPowerLvl == TxHighPwrLevel_Level1)
			writeVal = 0x14141414;
		else if (pdmpriv->DynamicTxHighPowerLvl ==
			 TxHighPwrLevel_Level2)
			writeVal = 0x00000000;

		/* 20100628 Joseph: High power mode for BT-Coexist mechanism. */
		/* This mechanism is only applied when
		   Driver-Highpower-Mechanism is OFF. */
		if (pdmpriv->DynamicTxHighPowerLvl == TxHighPwrLevel_BT1)
			writeVal = writeVal - 0x06060606;
		else if (pdmpriv->DynamicTxHighPowerLvl == TxHighPwrLevel_BT2)
			writeVal = writeVal;
		*(pOutWriteVal + rf) = writeVal;
	}
}

static void writeOFDMPowerReg(struct rtw_adapter *Adapter, u8 index,
			      u32 *pValue)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	u16 RegOffset_A[6] = {
		rTxAGC_A_Rate18_06, rTxAGC_A_Rate54_24,
		rTxAGC_A_Mcs03_Mcs00, rTxAGC_A_Mcs07_Mcs04,
		rTxAGC_A_Mcs11_Mcs08, rTxAGC_A_Mcs15_Mcs12
	};
	u16 RegOffset_B[6] = {
		rTxAGC_B_Rate18_06, rTxAGC_B_Rate54_24,
		rTxAGC_B_Mcs03_Mcs00, rTxAGC_B_Mcs07_Mcs04,
		rTxAGC_B_Mcs11_Mcs08, rTxAGC_B_Mcs15_Mcs12
	};
	u8 i, rf, pwr_val[4];
	u32 writeVal;
	u16 RegOffset;

	for (rf = 0; rf < 2; rf++) {
		writeVal = pValue[rf];
		for (i = 0; i < 4; i++) {
			pwr_val[i] = (u8)((writeVal &
					   (0x7f << (i * 8))) >> (i * 8));
			if (pwr_val[i] > RF6052_MAX_TX_PWR)
				pwr_val[i]  = RF6052_MAX_TX_PWR;
		}
		writeVal = pwr_val[3] << 24 | pwr_val[2] << 16 |
			pwr_val[1] << 8 | pwr_val[0];

		if (rf == 0)
			RegOffset = RegOffset_A[index];
		else
			RegOffset = RegOffset_B[index];

		rtl8723au_write32(Adapter, RegOffset, writeVal);

		/*  201005115 Joseph: Set Tx Power diff for Tx power
		    training mechanism. */
		if (((pHalData->rf_type == RF_2T2R) &&
		    (RegOffset == rTxAGC_A_Mcs15_Mcs12 ||
		     RegOffset == rTxAGC_B_Mcs15_Mcs12)) ||
		    ((pHalData->rf_type != RF_2T2R) &&
		     (RegOffset == rTxAGC_A_Mcs07_Mcs04 ||
		      RegOffset == rTxAGC_B_Mcs07_Mcs04))) {
			writeVal = pwr_val[3];
			if (RegOffset == rTxAGC_A_Mcs15_Mcs12 ||
			    RegOffset == rTxAGC_A_Mcs07_Mcs04)
				RegOffset = 0xc90;
			if (RegOffset == rTxAGC_B_Mcs15_Mcs12 ||
			    RegOffset == rTxAGC_B_Mcs07_Mcs04)
				RegOffset = 0xc98;
			for (i = 0; i < 3; i++) {
				if (i != 2)
					writeVal = (writeVal > 8) ?
						(writeVal - 8) : 0;
				else
					writeVal = (writeVal > 6) ?
						(writeVal - 6) : 0;
				rtl8723au_write8(Adapter, RegOffset + i,
						 (u8)writeVal);
			}
		}
	}
}
/*-----------------------------------------------------------------------------
 * Function:	PHY_RF6052SetOFDMTxPower
 *
 * Overview:	For legacy and HY OFDM, we must read EEPROM TX power index for
 *		different channel and read original value in TX power
 *		register area from 0xe00. We increase offset and
 *		original value to be correct tx pwr.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Remark
 * 11/05/2008	MHC	Simulate 8192 series method.
 * 01/06/2009	MHC	1. Prevent Path B tx power overflow or
 *			underflow dure to A/B pwr difference or
 *			legacy/HT pwr diff.
 *			2. We concern with path B legacy/HT OFDM difference.
 * 01/22/2009	MHC	Support new EPRO format from SD3.
 *
 *---------------------------------------------------------------------------*/
void rtl8723a_PHY_RF6052SetOFDMTxPower(struct rtw_adapter *Adapter,
				       u8 *pPowerLevel, u8 Channel)
{
	u32 writeVal[2], powerBase0[2], powerBase1[2];
	u8 index = 0;

	getPowerBase(Adapter, pPowerLevel, Channel,
		     &powerBase0[0], &powerBase1[0]);

	for (index = 0; index < 6; index++) {
		getTxPowerWriteValByRegulatory(Adapter, Channel, index,
			&powerBase0[0], &powerBase1[0], &writeVal[0]);

		writeOFDMPowerReg(Adapter, index, &writeVal[0]);
	}
}

static int phy_RF6052_Config_ParaFile(struct rtw_adapter *Adapter)
{
	u32 u4RegValue = 0;
	u8 eRFPath;
	struct bb_reg_define *pPhyReg;
	int rtStatus = _SUCCESS;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);

	/* 3----------------------------------------------------------------- */
	/* 3 <2> Initialize RF */
	/* 3----------------------------------------------------------------- */
	for (eRFPath = 0; eRFPath < pHalData->NumTotalRFPath; eRFPath++) {

		pPhyReg = &pHalData->PHYRegDef[eRFPath];

		/*----Store original RFENV control type----*/
		switch (eRFPath) {
		case RF_PATH_A:
			u4RegValue = PHY_QueryBBReg(Adapter, pPhyReg->rfintfs,
						    bRFSI_RFENV);
			break;
		case RF_PATH_B:
			u4RegValue = PHY_QueryBBReg(Adapter, pPhyReg->rfintfs,
						    bRFSI_RFENV << 16);
			break;
		}

		/*----Set RF_ENV enable----*/
		PHY_SetBBReg(Adapter, pPhyReg->rfintfe, bRFSI_RFENV << 16, 0x1);
		udelay(1);/* PlatformStallExecution(1); */

		/*----Set RF_ENV output high----*/
		PHY_SetBBReg(Adapter, pPhyReg->rfintfo, bRFSI_RFENV, 0x1);
		udelay(1);/* PlatformStallExecution(1); */

		/* Set bit number of Address and Data for RF register */
		PHY_SetBBReg(Adapter, pPhyReg->rfHSSIPara2, b3WireAddressLength,
			     0x0);	/*  Set 1 to 4 bits for 8255 */
		udelay(1);/* PlatformStallExecution(1); */

		PHY_SetBBReg(Adapter, pPhyReg->rfHSSIPara2, b3WireDataLength,
			     0x0);	/*  Set 0 to 12  bits for 8255 */
		udelay(1);/* PlatformStallExecution(1); */

		/*----Initialize RF fom connfiguration file----*/
		switch (eRFPath) {
		case RF_PATH_A:
			ODM_ReadAndConfig_RadioA_1T_8723A(&pHalData->odmpriv);
			break;
		case RF_PATH_B:
			break;
		}

		/*----Restore RFENV control type----*/;
		switch (eRFPath) {
		case RF_PATH_A:
			PHY_SetBBReg(Adapter, pPhyReg->rfintfs,
				     bRFSI_RFENV, u4RegValue);
			break;
		case RF_PATH_B:
			PHY_SetBBReg(Adapter, pPhyReg->rfintfs,
				     bRFSI_RFENV << 16, u4RegValue);
			break;
		}

		if (rtStatus != _SUCCESS) {
			goto phy_RF6052_Config_ParaFile_Fail;
		}
	}
phy_RF6052_Config_ParaFile_Fail:
	return rtStatus;
}

int PHY_RF6052_Config8723A(struct rtw_adapter *Adapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	int rtStatus = _SUCCESS;

	/*  Initialize general global value */
	/*  TODO: Extend RF_PATH_C and RF_PATH_D in the future */
	if (pHalData->rf_type == RF_1T1R)
		pHalData->NumTotalRFPath = 1;
	else
		pHalData->NumTotalRFPath = 2;

	/*  Config BB and RF */
	rtStatus = phy_RF6052_Config_ParaFile(Adapter);
	return rtStatus;
}

/* End of HalRf6052.c */
