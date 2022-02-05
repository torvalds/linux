// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

/******************************************************************************
 *
 *
 * Module:	rtl8192c_rf6052.c	( Source C File)
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

#define _RTL8188E_RF6052_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/rtl8188e_hal.h"

/*-----------------------------------------------------------------------------
 * Function:    PHY_RF6052SetBandwidth()
 *
 * Overview:    This function is called by SetBWModeCallback8190Pci() only
 *
 * Input:       struct adapter *Adapter
 *			WIRELESS_BANDWIDTH_E	Bandwidth	20M or 40M
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Note:		For RF type 0222D
 *---------------------------------------------------------------------------*/
void rtl8188e_PHY_RF6052SetBandwidth(struct adapter *Adapter,
				     enum ht_channel_width Bandwidth)
{
	struct hal_data_8188e *pHalData = &Adapter->haldata;

	switch (Bandwidth) {
	case HT_CHANNEL_WIDTH_20:
		pHalData->RfRegChnlVal = ((pHalData->RfRegChnlVal & 0xfffff3ff) | BIT(10) | BIT(11));
		rtl8188e_PHY_SetRFReg(Adapter, RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, pHalData->RfRegChnlVal);
		break;
	case HT_CHANNEL_WIDTH_40:
		pHalData->RfRegChnlVal = ((pHalData->RfRegChnlVal & 0xfffff3ff) | BIT(10));
		rtl8188e_PHY_SetRFReg(Adapter, RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, pHalData->RfRegChnlVal);
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

void
rtl8188e_PHY_RF6052SetCckTxPower(
		struct adapter *Adapter,
		u8 *pPowerlevel)
{
	struct hal_data_8188e *pHalData = &Adapter->haldata;
	struct mlme_ext_priv *pmlmeext = &Adapter->mlmeextpriv;
	u32 TxAGC[2] = {0, 0}, tmpval = 0, pwrtrac_value;
	u8 idx1, idx2;
	u8 *ptr;
	u8 direction;

	if (pmlmeext->sitesurvey_res.state == SCAN_PROCESS) {
		TxAGC[RF_PATH_A] = 0x3f3f3f3f;
		TxAGC[RF_PATH_B] = 0x3f3f3f3f;

		for (idx1 = RF_PATH_A; idx1 <= RF_PATH_B; idx1++) {
			TxAGC[idx1] =
				pPowerlevel[idx1] | (pPowerlevel[idx1] << 8) |
				(pPowerlevel[idx1] << 16) | (pPowerlevel[idx1] << 24);
		}
	} else {
		for (idx1 = RF_PATH_A; idx1 <= RF_PATH_B; idx1++) {
			TxAGC[idx1] =
				pPowerlevel[idx1] | (pPowerlevel[idx1] << 8) |
				(pPowerlevel[idx1] << 16) | (pPowerlevel[idx1] << 24);
		}
		if (pHalData->EEPROMRegulatory == 0) {
			tmpval = (pHalData->MCSTxPowerLevelOriginalOffset[0][6]) +
					(pHalData->MCSTxPowerLevelOriginalOffset[0][7] << 8);
			TxAGC[RF_PATH_A] += tmpval;

			tmpval = (pHalData->MCSTxPowerLevelOriginalOffset[0][14]) +
					(pHalData->MCSTxPowerLevelOriginalOffset[0][15] << 24);
			TxAGC[RF_PATH_B] += tmpval;
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
	ODM_TxPwrTrackAdjust88E(&pHalData->odmpriv, 1, &direction, &pwrtrac_value);

	if (direction == 1) {
		/*  Increase TX power */
		TxAGC[0] += pwrtrac_value;
		TxAGC[1] += pwrtrac_value;
	} else if (direction == 2) {
		/*  Decrease TX power */
		TxAGC[0] -=  pwrtrac_value;
		TxAGC[1] -=  pwrtrac_value;
	}

	/*  rf-A cck tx power */
	tmpval = TxAGC[RF_PATH_A] & 0xff;
	rtl8188e_PHY_SetBBReg(Adapter, rTxAGC_A_CCK1_Mcs32, bMaskByte1, tmpval);
	tmpval = TxAGC[RF_PATH_A] >> 8;
	rtl8188e_PHY_SetBBReg(Adapter, rTxAGC_B_CCK11_A_CCK2_11, 0xffffff00, tmpval);

	/*  rf-B cck tx power */
	tmpval = TxAGC[RF_PATH_B] >> 24;
	rtl8188e_PHY_SetBBReg(Adapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte0, tmpval);
	tmpval = TxAGC[RF_PATH_B] & 0x00ffffff;
	rtl8188e_PHY_SetBBReg(Adapter, rTxAGC_B_CCK1_55_Mcs32, 0xffffff00, tmpval);
}	/* PHY_RF6052SetCckTxPower */

/*  */
/*  powerbase0 for OFDM rates */
/*  powerbase1 for HT MCS rates */
/*  */
static void getpowerbase88e(struct adapter *Adapter, u8 *pPowerLevelOFDM,
			    u8 *pPowerLevelBW20, u8 *pPowerLevelBW40, u8 Channel, u32 *OfdmBase, u32 *MCSBase)
{
	struct hal_data_8188e *pHalData = &Adapter->haldata;
	u32 powerBase0, powerBase1;
	u8 i;

	for (i = 0; i < 2; i++) {
		powerBase0 = pPowerLevelOFDM[i];

		powerBase0 = (powerBase0 << 24) | (powerBase0 << 16) | (powerBase0 << 8) | powerBase0;
		*(OfdmBase + i) = powerBase0;
	}

	/* Check HT20 to HT40 diff */
	if (pHalData->CurrentChannelBW == HT_CHANNEL_WIDTH_20)
		powerBase1 = pPowerLevelBW20[0];
	else
		powerBase1 = pPowerLevelBW40[0];
	powerBase1 = (powerBase1 << 24) | (powerBase1 << 16) | (powerBase1 << 8) | powerBase1;
	*MCSBase = powerBase1;
}

static void get_rx_power_val_by_reg(struct adapter *Adapter, u8 Channel,
				    u8 index, u32 *powerBase0, u32 *powerBase1,
				    u32 *pOutWriteVal)
{
	struct hal_data_8188e *pHalData = &Adapter->haldata;
	u8	i, chnlGroup = 0, pwr_diff_limit[4], customer_pwr_limit;
	s8	pwr_diff = 0;
	u32	writeVal, customer_limit, rf;
	u8	Regulatory = pHalData->EEPROMRegulatory;

	/*  Index 0 & 1= legacy OFDM, 2-5=HT_MCS rate */

	for (rf = 0; rf < 2; rf++) {
		switch (Regulatory) {
		case 0:	/*  Realtek better performance */
				/*  increase power diff defined by Realtek for large power */
			chnlGroup = 0;
			writeVal = pHalData->MCSTxPowerLevelOriginalOffset[chnlGroup][index + (rf ? 8 : 0)] +
				((index < 2) ? powerBase0[rf] : powerBase1[rf]);
			break;
		case 1:	/*  Realtek regulatory */
			/*  increase power diff defined by Realtek for regulatory */
			if (pHalData->pwrGroupCnt == 1)
				chnlGroup = 0;
			if (pHalData->pwrGroupCnt >= MAX_PG_GROUP) {
				if (Channel < 3)			/*  Channel 1-2 */
					chnlGroup = 0;
				else if (Channel < 6)		/*  Channel 3-5 */
					chnlGroup = 1;
				else	 if (Channel < 9)		/*  Channel 6-8 */
					chnlGroup = 2;
				else if (Channel < 12)		/*  Channel 9-11 */
					chnlGroup = 3;
				else if (Channel < 14)		/*  Channel 12-13 */
					chnlGroup = 4;
				else if (Channel == 14)		/*  Channel 14 */
					chnlGroup = 5;
			}
			writeVal = pHalData->MCSTxPowerLevelOriginalOffset[chnlGroup][index + (rf ? 8 : 0)] +
					((index < 2) ? powerBase0[rf] : powerBase1[rf]);
			break;
		case 2:	/*  Better regulatory */
				/*  don't increase any power diff */
			writeVal = ((index < 2) ? powerBase0[rf] : powerBase1[rf]);
			break;
		case 3:	/*  Customer defined power diff. */
				/*  increase power diff defined by customer. */
			chnlGroup = 0;

			if (index < 2)
				pwr_diff = pHalData->TxPwrLegacyHtDiff[rf][Channel - 1];
			else if (pHalData->CurrentChannelBW == HT_CHANNEL_WIDTH_20)
				pwr_diff = pHalData->TxPwrHt20Diff[rf][Channel - 1];

			if (pHalData->CurrentChannelBW == HT_CHANNEL_WIDTH_40)
				customer_pwr_limit = pHalData->PwrGroupHT40[rf][Channel - 1];
			else
				customer_pwr_limit = pHalData->PwrGroupHT20[rf][Channel - 1];

			if (pwr_diff >= customer_pwr_limit)
				pwr_diff = 0;
			else
				pwr_diff = customer_pwr_limit - pwr_diff;

			for (i = 0; i < 4; i++) {
				pwr_diff_limit[i] = (u8)((pHalData->MCSTxPowerLevelOriginalOffset[chnlGroup][index + (rf ? 8 : 0)] & (0x7f << (i * 8))) >> (i * 8));

				if (pwr_diff_limit[i] > pwr_diff)
					pwr_diff_limit[i] = pwr_diff;
			}
			customer_limit = (pwr_diff_limit[3] << 24) | (pwr_diff_limit[2] << 16) |
					 (pwr_diff_limit[1] << 8) | (pwr_diff_limit[0]);
			writeVal = customer_limit + ((index < 2) ? powerBase0[rf] : powerBase1[rf]);
			break;
		default:
			chnlGroup = 0;
			writeVal = pHalData->MCSTxPowerLevelOriginalOffset[chnlGroup][index + (rf ? 8 : 0)] +
					((index < 2) ? powerBase0[rf] : powerBase1[rf]);
			break;
		}

		*(pOutWriteVal + rf) = writeVal;
	}
}
static void writeOFDMPowerReg88E(struct adapter *Adapter, u8 index, u32 *pValue)
{
	u16 regoffset_a[6] = {
		rTxAGC_A_Rate18_06, rTxAGC_A_Rate54_24,
		rTxAGC_A_Mcs03_Mcs00, rTxAGC_A_Mcs07_Mcs04,
		rTxAGC_A_Mcs11_Mcs08, rTxAGC_A_Mcs15_Mcs12};
	u16 regoffset_b[6] = {
		rTxAGC_B_Rate18_06, rTxAGC_B_Rate54_24,
		rTxAGC_B_Mcs03_Mcs00, rTxAGC_B_Mcs07_Mcs04,
		rTxAGC_B_Mcs11_Mcs08, rTxAGC_B_Mcs15_Mcs12};
	u8 i, rf, pwr_val[4];
	u32 writeVal;
	u16 regoffset;

	for (rf = 0; rf < 2; rf++) {
		writeVal = pValue[rf];
		for (i = 0; i < 4; i++) {
			pwr_val[i] = (u8)((writeVal & (0x7f << (i * 8))) >> (i * 8));
			if (pwr_val[i]  > RF6052_MAX_TX_PWR)
				pwr_val[i]  = RF6052_MAX_TX_PWR;
		}
		writeVal = (pwr_val[3] << 24) | (pwr_val[2] << 16) | (pwr_val[1] << 8) | pwr_val[0];

		if (rf == 0)
			regoffset = regoffset_a[index];
		else
			regoffset = regoffset_b[index];

		rtl8188e_PHY_SetBBReg(Adapter, regoffset, bMaskDWord, writeVal);

		/*  201005115 Joseph: Set Tx Power diff for Tx power training mechanism. */
		if (regoffset == rTxAGC_A_Mcs07_Mcs04 || regoffset == rTxAGC_B_Mcs07_Mcs04) {
			writeVal = pwr_val[3];
			if (regoffset == rTxAGC_A_Mcs15_Mcs12 || regoffset == rTxAGC_A_Mcs07_Mcs04)
				regoffset = 0xc90;
			if (regoffset == rTxAGC_B_Mcs15_Mcs12 || regoffset == rTxAGC_B_Mcs07_Mcs04)
				regoffset = 0xc98;
			for (i = 0; i < 3; i++) {
				if (i != 2)
					writeVal = (writeVal > 8) ? (writeVal - 8) : 0;
				else
					writeVal = (writeVal > 6) ? (writeVal - 6) : 0;
				rtw_write8(Adapter, (u32)(regoffset + i), (u8)writeVal);
			}
		}
	}
}

/*-----------------------------------------------------------------------------
 * Function:	PHY_RF6052SetOFDMTxPower
 *
 * Overview:	For legacy and HY OFDM, we must read EEPROM TX power index for
 *			different channel and read original value in TX power register area from
 *			0xe00. We increase offset and original value to be correct tx pwr.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/05/2008	MHC		Simulate 8192 series method.
 * 01/06/2009	MHC		1. Prevent Path B tx power overflow or underflow dure to
 *						A/B pwr difference or legacy/HT pwr diff.
 *						2. We concern with path B legacy/HT OFDM difference.
 * 01/22/2009	MHC		Support new EPRO format from SD3.
 *
 *---------------------------------------------------------------------------*/

void
rtl8188e_PHY_RF6052SetOFDMTxPower(
		struct adapter *Adapter,
		u8 *pPowerLevelOFDM,
		u8 *pPowerLevelBW20,
		u8 *pPowerLevelBW40,
		u8 Channel)
{
	struct hal_data_8188e *pHalData = &Adapter->haldata;
	u32 writeVal[2], powerBase0[2], powerBase1[2], pwrtrac_value;
	u8 direction;
	u8 index = 0;

	getpowerbase88e(Adapter, pPowerLevelOFDM, pPowerLevelBW20, pPowerLevelBW40, Channel, &powerBase0[0], &powerBase1[0]);

	/*  2012/04/23 MH According to power tracking value, we need to revise OFDM tx power. */
	/*  This is ued to fix unstable power tracking mode. */
	ODM_TxPwrTrackAdjust88E(&pHalData->odmpriv, 0, &direction, &pwrtrac_value);

	for (index = 0; index < 6; index++) {
		get_rx_power_val_by_reg(Adapter, Channel, index,
					&powerBase0[0], &powerBase1[0],
					&writeVal[0]);

		if (direction == 1) {
			writeVal[0] += pwrtrac_value;
			writeVal[1] += pwrtrac_value;
		} else if (direction == 2) {
			writeVal[0] -= pwrtrac_value;
			writeVal[1] -= pwrtrac_value;
		}
		writeOFDMPowerReg88E(Adapter, index, &writeVal[0]);
	}
}

static int phy_RF6052_Config_ParaFile(struct adapter *Adapter)
{
	struct bb_reg_def *pPhyReg;
	struct hal_data_8188e *pHalData = &Adapter->haldata;
	u32 u4RegValue = 0;
	int rtStatus = _SUCCESS;

	/* Initialize RF */

	pPhyReg = &pHalData->PHYRegDef[0];

	/*----Store original RFENV control type----*/
	u4RegValue = rtl8188e_PHY_QueryBBReg(Adapter, pPhyReg->rfintfs, bRFSI_RFENV);

	/*----Set RF_ENV enable----*/
	rtl8188e_PHY_SetBBReg(Adapter, pPhyReg->rfintfe, bRFSI_RFENV << 16, 0x1);
	udelay(1);/* PlatformStallExecution(1); */

	/*----Set RF_ENV output high----*/
	rtl8188e_PHY_SetBBReg(Adapter, pPhyReg->rfintfo, bRFSI_RFENV, 0x1);
	udelay(1);/* PlatformStallExecution(1); */

	/* Set bit number of Address and Data for RF register */
	rtl8188e_PHY_SetBBReg(Adapter, pPhyReg->rfHSSIPara2, b3WireAddressLength, 0x0);	/*  Set 1 to 4 bits for 8255 */
	udelay(1);/* PlatformStallExecution(1); */

	rtl8188e_PHY_SetBBReg(Adapter, pPhyReg->rfHSSIPara2, b3WireDataLength, 0x0);	/*  Set 0 to 12  bits for 8255 */
	udelay(1);/* PlatformStallExecution(1); */

	/*----Initialize RF fom connfiguration file----*/
	if (HAL_STATUS_FAILURE == ODM_ConfigRFWithHeaderFile(&pHalData->odmpriv))
		rtStatus = _FAIL;

	/*----Restore RFENV control type----*/;
	rtl8188e_PHY_SetBBReg(Adapter, pPhyReg->rfintfs, bRFSI_RFENV, u4RegValue);

	return rtStatus;
}

int PHY_RF6052_Config8188E(struct adapter *Adapter)
{
	int rtStatus = _SUCCESS;

	/*  */
	/*  Config BB and RF */
	/*  */
	rtStatus = phy_RF6052_Config_ParaFile(Adapter);
	return rtStatus;
}
