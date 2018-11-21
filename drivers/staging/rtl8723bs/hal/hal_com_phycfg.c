// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _HAL_COM_PHYCFG_C_

#include <drv_types.h>
#include <rtw_debug.h>
#include <hal_data.h>
#include <linux/kernel.h>

u8 PHY_GetTxPowerByRateBase(struct adapter *Adapter, u8 Band, u8 RfPath,
			    u8 TxNum, enum RATE_SECTION RateSection)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);
	u8	value = 0;

	if (RfPath > ODM_RF_PATH_D) {
		DBG_871X("Invalid Rf Path %d in PHY_GetTxPowerByRateBase()\n", RfPath);
		return 0;
	}

	if (Band == BAND_ON_2_4G) {
		switch (RateSection) {
		case CCK:
			value = pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][0];
			break;
		case OFDM:
			value = pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][1];
			break;
		case HT_MCS0_MCS7:
			value = pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][2];
			break;
		case HT_MCS8_MCS15:
			value = pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][3];
			break;
		case HT_MCS16_MCS23:
			value = pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][4];
			break;
		case HT_MCS24_MCS31:
			value = pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][5];
			break;
		case VHT_1SSMCS0_1SSMCS9:
			value = pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][6];
			break;
		case VHT_2SSMCS0_2SSMCS9:
			value = pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][7];
			break;
		case VHT_3SSMCS0_3SSMCS9:
			value = pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][8];
			break;
		case VHT_4SSMCS0_4SSMCS9:
			value = pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][9];
			break;
		default:
			DBG_871X("Invalid RateSection %d in Band 2.4G, Rf Path %d, %dTx in PHY_GetTxPowerByRateBase()\n",
					 RateSection, RfPath, TxNum);
			break;
		}
	} else if (Band == BAND_ON_5G) {
		switch (RateSection) {
		case OFDM:
			value = pHalData->TxPwrByRateBase5G[RfPath][TxNum][0];
			break;
		case HT_MCS0_MCS7:
			value = pHalData->TxPwrByRateBase5G[RfPath][TxNum][1];
			break;
		case HT_MCS8_MCS15:
			value = pHalData->TxPwrByRateBase5G[RfPath][TxNum][2];
			break;
		case HT_MCS16_MCS23:
			value = pHalData->TxPwrByRateBase5G[RfPath][TxNum][3];
			break;
		case HT_MCS24_MCS31:
			value = pHalData->TxPwrByRateBase5G[RfPath][TxNum][4];
			break;
		case VHT_1SSMCS0_1SSMCS9:
			value = pHalData->TxPwrByRateBase5G[RfPath][TxNum][5];
			break;
		case VHT_2SSMCS0_2SSMCS9:
			value = pHalData->TxPwrByRateBase5G[RfPath][TxNum][6];
			break;
		case VHT_3SSMCS0_3SSMCS9:
			value = pHalData->TxPwrByRateBase5G[RfPath][TxNum][7];
			break;
		case VHT_4SSMCS0_4SSMCS9:
			value = pHalData->TxPwrByRateBase5G[RfPath][TxNum][8];
			break;
		default:
			DBG_871X("Invalid RateSection %d in Band 5G, Rf Path %d, %dTx in PHY_GetTxPowerByRateBase()\n",
					 RateSection, RfPath, TxNum);
			break;
		}
	} else
		DBG_871X("Invalid Band %d in PHY_GetTxPowerByRateBase()\n", Band);

	return value;
}

static void
phy_SetTxPowerByRateBase(
	struct adapter *Adapter,
	u8 Band,
	u8 RfPath,
	enum RATE_SECTION	RateSection,
	u8 TxNum,
	u8 Value
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);

	if (RfPath > ODM_RF_PATH_D) {
		DBG_871X("Invalid Rf Path %d in phy_SetTxPowerByRatBase()\n", RfPath);
		return;
	}

	if (Band == BAND_ON_2_4G) {
		switch (RateSection) {
		case CCK:
			pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][0] = Value;
			break;
		case OFDM:
			pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][1] = Value;
			break;
		case HT_MCS0_MCS7:
			pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][2] = Value;
			break;
		case HT_MCS8_MCS15:
			pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][3] = Value;
			break;
		case HT_MCS16_MCS23:
			pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][4] = Value;
			break;
		case HT_MCS24_MCS31:
			pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][5] = Value;
			break;
		case VHT_1SSMCS0_1SSMCS9:
			pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][6] = Value;
			break;
		case VHT_2SSMCS0_2SSMCS9:
			pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][7] = Value;
			break;
		case VHT_3SSMCS0_3SSMCS9:
			pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][8] = Value;
			break;
		case VHT_4SSMCS0_4SSMCS9:
			pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][9] = Value;
			break;
		default:
			DBG_871X("Invalid RateSection %d in Band 2.4G, Rf Path %d, %dTx in phy_SetTxPowerByRateBase()\n",
					 RateSection, RfPath, TxNum);
			break;
		}
	} else if (Band == BAND_ON_5G) {
		switch (RateSection) {
		case OFDM:
			pHalData->TxPwrByRateBase5G[RfPath][TxNum][0] = Value;
			break;
		case HT_MCS0_MCS7:
			pHalData->TxPwrByRateBase5G[RfPath][TxNum][1] = Value;
			break;
		case HT_MCS8_MCS15:
			pHalData->TxPwrByRateBase5G[RfPath][TxNum][2] = Value;
			break;
		case HT_MCS16_MCS23:
			pHalData->TxPwrByRateBase5G[RfPath][TxNum][3] = Value;
			break;
		case HT_MCS24_MCS31:
			pHalData->TxPwrByRateBase5G[RfPath][TxNum][4] = Value;
			break;
		case VHT_1SSMCS0_1SSMCS9:
			pHalData->TxPwrByRateBase5G[RfPath][TxNum][5] = Value;
			break;
		case VHT_2SSMCS0_2SSMCS9:
			pHalData->TxPwrByRateBase5G[RfPath][TxNum][6] = Value;
			break;
		case VHT_3SSMCS0_3SSMCS9:
			pHalData->TxPwrByRateBase5G[RfPath][TxNum][7] = Value;
			break;
		case VHT_4SSMCS0_4SSMCS9:
			pHalData->TxPwrByRateBase5G[RfPath][TxNum][8] = Value;
			break;
		default:
			DBG_871X("Invalid RateSection %d in Band 5G, Rf Path %d, %dTx in phy_SetTxPowerByRateBase()\n",
					 RateSection, RfPath, TxNum);
			break;
		}
	} else
		DBG_871X("Invalid Band %d in phy_SetTxPowerByRateBase()\n", Band);
}

static void
phy_StoreTxPowerByRateBase(
struct adapter *padapter
	)
{
	u8 path, base;

	/* DBG_871X("===>%s\n", __func__); */

	for (path = ODM_RF_PATH_A; path <= ODM_RF_PATH_B; ++path) {
		base = PHY_GetTxPowerByRate(padapter, BAND_ON_2_4G, path, RF_1TX, MGN_11M);
		phy_SetTxPowerByRateBase(padapter, BAND_ON_2_4G, path, CCK, RF_1TX, base);
		/* DBG_871X("Power index base of 2.4G path %d 1Tx CCK = > 0x%x\n", path, base); */

		base = PHY_GetTxPowerByRate(padapter, BAND_ON_2_4G, path, RF_1TX, MGN_54M);
		phy_SetTxPowerByRateBase(padapter, BAND_ON_2_4G, path, OFDM, RF_1TX, base);
		/* DBG_871X("Power index base of 2.4G path %d 1Tx OFDM = > 0x%x\n", path, base); */

		base = PHY_GetTxPowerByRate(padapter, BAND_ON_2_4G, path, RF_1TX, MGN_MCS7);
		phy_SetTxPowerByRateBase(padapter, BAND_ON_2_4G, path, HT_MCS0_MCS7, RF_1TX, base);
		/* DBG_871X("Power index base of 2.4G path %d 1Tx MCS0-7 = > 0x%x\n", path, base); */

		base = PHY_GetTxPowerByRate(padapter, BAND_ON_2_4G, path, RF_2TX, MGN_MCS15);
		phy_SetTxPowerByRateBase(padapter, BAND_ON_2_4G, path, HT_MCS8_MCS15, RF_2TX, base);
		/* DBG_871X("Power index base of 2.4G path %d 2Tx MCS8-15 = > 0x%x\n", path, base); */

		base = PHY_GetTxPowerByRate(padapter, BAND_ON_2_4G, path, RF_3TX, MGN_MCS23);
		phy_SetTxPowerByRateBase(padapter, BAND_ON_2_4G, path, HT_MCS16_MCS23, RF_3TX, base);
		/* DBG_871X("Power index base of 2.4G path %d 3Tx MCS16-23 = > 0x%x\n", path, base); */

		base = PHY_GetTxPowerByRate(padapter, BAND_ON_2_4G, path, RF_1TX, MGN_VHT1SS_MCS7);
		phy_SetTxPowerByRateBase(padapter, BAND_ON_2_4G, path, VHT_1SSMCS0_1SSMCS9, RF_1TX, base);
		/* DBG_871X("Power index base of 2.4G path %d 1Tx VHT1SS = > 0x%x\n", path, base); */

		base = PHY_GetTxPowerByRate(padapter, BAND_ON_2_4G, path, RF_2TX, MGN_VHT2SS_MCS7);
		phy_SetTxPowerByRateBase(padapter, BAND_ON_2_4G, path, VHT_2SSMCS0_2SSMCS9, RF_2TX, base);
		/* DBG_871X("Power index base of 2.4G path %d 2Tx VHT2SS = > 0x%x\n", path, base); */

		base = PHY_GetTxPowerByRate(padapter, BAND_ON_2_4G, path, RF_3TX, MGN_VHT3SS_MCS7);
		phy_SetTxPowerByRateBase(padapter, BAND_ON_2_4G, path, VHT_3SSMCS0_3SSMCS9, RF_3TX, base);
		/* DBG_871X("Power index base of 2.4G path %d 3Tx VHT3SS = > 0x%x\n", path, base); */

		base = PHY_GetTxPowerByRate(padapter, BAND_ON_5G, path, RF_1TX, MGN_54M);
		phy_SetTxPowerByRateBase(padapter, BAND_ON_5G, path, OFDM, RF_1TX, base);
		/* DBG_871X("Power index base of 5G path %d 1Tx OFDM = > 0x%x\n", path, base); */

		base = PHY_GetTxPowerByRate(padapter, BAND_ON_5G, path, RF_1TX, MGN_MCS7);
		phy_SetTxPowerByRateBase(padapter, BAND_ON_5G, path, HT_MCS0_MCS7, RF_1TX, base);
		/* DBG_871X("Power index base of 5G path %d 1Tx MCS0~7 = > 0x%x\n", path, base); */

		base = PHY_GetTxPowerByRate(padapter, BAND_ON_5G, path, RF_2TX, MGN_MCS15);
		phy_SetTxPowerByRateBase(padapter, BAND_ON_5G, path, HT_MCS8_MCS15, RF_2TX, base);
		/* DBG_871X("Power index base of 5G path %d 2Tx MCS8~15 = > 0x%x\n", path, base); */

		base = PHY_GetTxPowerByRate(padapter, BAND_ON_5G, path, RF_3TX, MGN_MCS23);
		phy_SetTxPowerByRateBase(padapter, BAND_ON_5G, path, HT_MCS16_MCS23, RF_3TX, base);
		/* DBG_871X("Power index base of 5G path %d 3Tx MCS16~23 = > 0x%x\n", path, base); */

		base = PHY_GetTxPowerByRate(padapter, BAND_ON_5G, path, RF_1TX, MGN_VHT1SS_MCS7);
		phy_SetTxPowerByRateBase(padapter, BAND_ON_5G, path, VHT_1SSMCS0_1SSMCS9, RF_1TX, base);
		/* DBG_871X("Power index base of 5G path %d 1Tx VHT1SS = > 0x%x\n", path, base); */

		base = PHY_GetTxPowerByRate(padapter, BAND_ON_5G, path, RF_2TX, MGN_VHT2SS_MCS7);
		phy_SetTxPowerByRateBase(padapter, BAND_ON_5G, path, VHT_2SSMCS0_2SSMCS9, RF_2TX, base);
		/* DBG_871X("Power index base of 5G path %d 2Tx VHT2SS = > 0x%x\n", path, base); */

		base = PHY_GetTxPowerByRate(padapter, BAND_ON_5G, path, RF_3TX, MGN_VHT2SS_MCS7);
		phy_SetTxPowerByRateBase(padapter, BAND_ON_5G, path, VHT_3SSMCS0_3SSMCS9, RF_3TX, base);
		/* DBG_871X("Power index base of 5G path %d 3Tx VHT3SS = > 0x%x\n", path, base); */
	}

	/* DBG_871X("<===%s\n", __func__); */
}

u8 PHY_GetRateSectionIndexOfTxPowerByRate(
	struct adapter *padapter, u32 RegAddr, u32 BitMask
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	PDM_ODM_T pDM_Odm = &pHalData->odmpriv;
	u8	index = 0;

	if (pDM_Odm->PhyRegPgVersion == 0) {
		switch (RegAddr) {
		case rTxAGC_A_Rate18_06:
			index = 0;
			break;
		case rTxAGC_A_Rate54_24:
			index = 1;
			break;
		case rTxAGC_A_CCK1_Mcs32:
			index = 6;
			break;
		case rTxAGC_B_CCK11_A_CCK2_11:
			if (BitMask == bMaskH3Bytes)
				index = 7;
			else if (BitMask == 0x000000ff)
				index = 15;
			break;

		case rTxAGC_A_Mcs03_Mcs00:
			index = 2;
			break;
		case rTxAGC_A_Mcs07_Mcs04:
			index = 3;
			break;
		case rTxAGC_A_Mcs11_Mcs08:
			index = 4;
			break;
		case rTxAGC_A_Mcs15_Mcs12:
			index = 5;
			break;
		case rTxAGC_B_Rate18_06:
			index = 8;
			break;
		case rTxAGC_B_Rate54_24:
			index = 9;
			break;
		case rTxAGC_B_CCK1_55_Mcs32:
			index = 14;
			break;
		case rTxAGC_B_Mcs03_Mcs00:
			index = 10;
			break;
		case rTxAGC_B_Mcs07_Mcs04:
			index = 11;
			break;
		case rTxAGC_B_Mcs11_Mcs08:
			index = 12;
			break;
		case rTxAGC_B_Mcs15_Mcs12:
			index = 13;
			break;
		default:
			DBG_871X("Invalid RegAddr 0x3%x in PHY_GetRateSectionIndexOfTxPowerByRate()", RegAddr);
			break;
		}
	}

	return index;
}

void
PHY_GetRateValuesOfTxPowerByRate(
	struct adapter *padapter,
	u32	RegAddr,
	u32	BitMask,
	u32	Value,
	u8 *RateIndex,
	s8 *PwrByRateVal,
	u8 *RateNum
)
{
	u8 i = 0;

	switch (RegAddr) {
	case rTxAGC_A_Rate18_06:
	case rTxAGC_B_Rate18_06:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_6M);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_9M);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_12M);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_18M);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case rTxAGC_A_Rate54_24:
	case rTxAGC_B_Rate54_24:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_24M);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_36M);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_48M);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_54M);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case rTxAGC_A_CCK1_Mcs32:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_1M);
		PwrByRateVal[0] = (s8) ((((Value >> (8 + 4)) & 0xF)) * 10 +
										((Value >> 8) & 0xF));
		*RateNum = 1;
		break;

	case rTxAGC_B_CCK11_A_CCK2_11:
		if (BitMask == 0xffffff00) {
			RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_2M);
			RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_5_5M);
			RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_11M);
			for (i = 1; i < 4; ++i) {
				PwrByRateVal[i - 1] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
												((Value >> (i * 8)) & 0xF));
			}
			*RateNum = 3;
		} else if (BitMask == 0x000000ff) {
			RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_11M);
			PwrByRateVal[0] = (s8) ((((Value >> 4) & 0xF)) * 10 + (Value & 0xF));
			*RateNum = 1;
		}
		break;

	case rTxAGC_A_Mcs03_Mcs00:
	case rTxAGC_B_Mcs03_Mcs00:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS0);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS1);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS2);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS3);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case rTxAGC_A_Mcs07_Mcs04:
	case rTxAGC_B_Mcs07_Mcs04:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS4);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS5);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS6);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS7);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case rTxAGC_A_Mcs11_Mcs08:
	case rTxAGC_B_Mcs11_Mcs08:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS8);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS9);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS10);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS11);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case rTxAGC_A_Mcs15_Mcs12:
	case rTxAGC_B_Mcs15_Mcs12:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS12);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS13);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS14);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS15);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;

		break;

	case rTxAGC_B_CCK1_55_Mcs32:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_1M);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_2M);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_5_5M);
		for (i = 1; i < 4; ++i) {
			PwrByRateVal[i - 1] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 3;
		break;

	case 0xC20:
	case 0xE20:
	case 0x1820:
	case 0x1a20:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_1M);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_2M);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_5_5M);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_11M);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC24:
	case 0xE24:
	case 0x1824:
	case 0x1a24:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_6M);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_9M);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_12M);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_18M);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC28:
	case 0xE28:
	case 0x1828:
	case 0x1a28:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_24M);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_36M);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_48M);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_54M);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC2C:
	case 0xE2C:
	case 0x182C:
	case 0x1a2C:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS0);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS1);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS2);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS3);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC30:
	case 0xE30:
	case 0x1830:
	case 0x1a30:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS4);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS5);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS6);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS7);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC34:
	case 0xE34:
	case 0x1834:
	case 0x1a34:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS8);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS9);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS10);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS11);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC38:
	case 0xE38:
	case 0x1838:
	case 0x1a38:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS12);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS13);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS14);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS15);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC3C:
	case 0xE3C:
	case 0x183C:
	case 0x1a3C:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT1SS_MCS0);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT1SS_MCS1);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT1SS_MCS2);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT1SS_MCS3);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC40:
	case 0xE40:
	case 0x1840:
	case 0x1a40:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT1SS_MCS4);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT1SS_MCS5);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT1SS_MCS6);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT1SS_MCS7);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC44:
	case 0xE44:
	case 0x1844:
	case 0x1a44:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT1SS_MCS8);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT1SS_MCS9);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT2SS_MCS0);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT2SS_MCS1);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC48:
	case 0xE48:
	case 0x1848:
	case 0x1a48:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT2SS_MCS2);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT2SS_MCS3);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT2SS_MCS4);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT2SS_MCS5);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC4C:
	case 0xE4C:
	case 0x184C:
	case 0x1a4C:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT2SS_MCS6);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT2SS_MCS7);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT2SS_MCS8);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT2SS_MCS9);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xCD8:
	case 0xED8:
	case 0x18D8:
	case 0x1aD8:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS16);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS17);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS18);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS19);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xCDC:
	case 0xEDC:
	case 0x18DC:
	case 0x1aDC:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS20);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS21);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS22);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_MCS23);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xCE0:
	case 0xEE0:
	case 0x18E0:
	case 0x1aE0:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT3SS_MCS0);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT3SS_MCS1);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT3SS_MCS2);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT3SS_MCS3);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xCE4:
	case 0xEE4:
	case 0x18E4:
	case 0x1aE4:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT3SS_MCS4);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT3SS_MCS5);
		RateIndex[2] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT3SS_MCS6);
		RateIndex[3] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT3SS_MCS7);
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xCE8:
	case 0xEE8:
	case 0x18E8:
	case 0x1aE8:
		RateIndex[0] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT3SS_MCS8);
		RateIndex[1] = PHY_GetRateIndexOfTxPowerByRate(MGN_VHT3SS_MCS9);
		for (i = 0; i < 2; ++i) {
			PwrByRateVal[i] = (s8) ((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
											((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	default:
		DBG_871X("Invalid RegAddr 0x%x in %s()\n", RegAddr, __func__);
		break;
	}
}

static void PHY_StoreTxPowerByRateNew(
	struct adapter *padapter,
	u32	Band,
	u32	RfPath,
	u32	TxNum,
	u32	RegAddr,
	u32	BitMask,
	u32	Data
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	u8 i = 0, rateIndex[4] = {0}, rateNum = 0;
	s8	PwrByRateVal[4] = {0};

	PHY_GetRateValuesOfTxPowerByRate(padapter, RegAddr, BitMask, Data, rateIndex, PwrByRateVal, &rateNum);

	if (Band != BAND_ON_2_4G && Band != BAND_ON_5G) {
		DBG_871X("Invalid Band %d\n", Band);
		return;
	}

	if (RfPath > ODM_RF_PATH_D) {
		DBG_871X("Invalid RfPath %d\n", RfPath);
		return;
	}

	if (TxNum > ODM_RF_PATH_D) {
		DBG_871X("Invalid TxNum %d\n", TxNum);
		return;
	}

	for (i = 0; i < rateNum; ++i) {
		if (rateIndex[i] == PHY_GetRateIndexOfTxPowerByRate(MGN_VHT2SS_MCS0) ||
			 rateIndex[i] == PHY_GetRateIndexOfTxPowerByRate(MGN_VHT2SS_MCS1))
			TxNum = RF_2TX;

		pHalData->TxPwrByRateOffset[Band][RfPath][TxNum][rateIndex[i]] = PwrByRateVal[i];
	}
}

static void PHY_StoreTxPowerByRateOld(
	struct adapter *padapter, u32	RegAddr, u32 BitMask, u32 Data
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	u8	index = PHY_GetRateSectionIndexOfTxPowerByRate(padapter, RegAddr, BitMask);

	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][index] = Data;
	/* DBG_871X("MCSTxPowerLevelOriginalOffset[%d][0] = 0x%x\n", pHalData->pwrGroupCnt, */
	/*	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][0]); */
}

void PHY_InitTxPowerByRate(struct adapter *padapter)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	u8 band, rfPath, TxNum, rate;

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; ++band)
			for (rfPath = 0; rfPath < TX_PWR_BY_RATE_NUM_RF; ++rfPath)
				for (TxNum = 0; TxNum < TX_PWR_BY_RATE_NUM_RF; ++TxNum)
					for (rate = 0; rate < TX_PWR_BY_RATE_NUM_RATE; ++rate)
						pHalData->TxPwrByRateOffset[band][rfPath][TxNum][rate] = 0;
}

void PHY_StoreTxPowerByRate(
	struct adapter *padapter,
	u32	Band,
	u32	RfPath,
	u32	TxNum,
	u32	RegAddr,
	u32	BitMask,
	u32	Data
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;

	if (pDM_Odm->PhyRegPgVersion > 0)
		PHY_StoreTxPowerByRateNew(padapter, Band, RfPath, TxNum, RegAddr, BitMask, Data);
	else if (pDM_Odm->PhyRegPgVersion == 0) {
		PHY_StoreTxPowerByRateOld(padapter, RegAddr, BitMask, Data);

		if (RegAddr == rTxAGC_A_Mcs15_Mcs12 && pHalData->rf_type == RF_1T1R)
			pHalData->pwrGroupCnt++;
		else if (RegAddr == rTxAGC_B_Mcs15_Mcs12 && pHalData->rf_type != RF_1T1R)
			pHalData->pwrGroupCnt++;
	} else
		DBG_871X("Invalid PHY_REG_PG.txt version %d\n",  pDM_Odm->PhyRegPgVersion);

}

static void
phy_ConvertTxPowerByRateInDbmToRelativeValues(
struct adapter *padapter
	)
{
	u8	base = 0, i = 0, value = 0, band = 0, path = 0, txNum = 0;
	u8	cckRates[4] = {
		MGN_1M, MGN_2M, MGN_5_5M, MGN_11M
	};
	u8	ofdmRates[8] = {
		MGN_6M, MGN_9M, MGN_12M, MGN_18M, MGN_24M, MGN_36M, MGN_48M, MGN_54M
	};
	u8 mcs0_7Rates[8] = {
		MGN_MCS0, MGN_MCS1, MGN_MCS2, MGN_MCS3, MGN_MCS4, MGN_MCS5, MGN_MCS6, MGN_MCS7
	};
	u8 mcs8_15Rates[8] = {
		MGN_MCS8, MGN_MCS9, MGN_MCS10, MGN_MCS11, MGN_MCS12, MGN_MCS13, MGN_MCS14, MGN_MCS15
	};
	u8 mcs16_23Rates[8] = {
		MGN_MCS16, MGN_MCS17, MGN_MCS18, MGN_MCS19, MGN_MCS20, MGN_MCS21, MGN_MCS22, MGN_MCS23
	};
	u8 vht1ssRates[10] = {
		MGN_VHT1SS_MCS0, MGN_VHT1SS_MCS1, MGN_VHT1SS_MCS2, MGN_VHT1SS_MCS3, MGN_VHT1SS_MCS4,
		MGN_VHT1SS_MCS5, MGN_VHT1SS_MCS6, MGN_VHT1SS_MCS7, MGN_VHT1SS_MCS8, MGN_VHT1SS_MCS9
	};
	u8 vht2ssRates[10] = {
		MGN_VHT2SS_MCS0, MGN_VHT2SS_MCS1, MGN_VHT2SS_MCS2, MGN_VHT2SS_MCS3, MGN_VHT2SS_MCS4,
		MGN_VHT2SS_MCS5, MGN_VHT2SS_MCS6, MGN_VHT2SS_MCS7, MGN_VHT2SS_MCS8, MGN_VHT2SS_MCS9
	};
	u8 vht3ssRates[10] = {
		MGN_VHT3SS_MCS0, MGN_VHT3SS_MCS1, MGN_VHT3SS_MCS2, MGN_VHT3SS_MCS3, MGN_VHT3SS_MCS4,
		MGN_VHT3SS_MCS5, MGN_VHT3SS_MCS6, MGN_VHT3SS_MCS7, MGN_VHT3SS_MCS8, MGN_VHT3SS_MCS9
	};

	/* DBG_871X("===>PHY_ConvertTxPowerByRateInDbmToRelativeValues()\n"); */

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; ++band) {
		for (path = ODM_RF_PATH_A; path <= ODM_RF_PATH_D; ++path) {
			for (txNum = RF_1TX; txNum < RF_MAX_TX_NUM; ++txNum) {
				/*  CCK */
				base = PHY_GetTxPowerByRate(padapter, band, path, txNum, MGN_11M);
				for (i = 0; i < ARRAY_SIZE(cckRates); ++i) {
					value = PHY_GetTxPowerByRate(padapter, band, path, txNum, cckRates[i]);
					PHY_SetTxPowerByRate(padapter, band, path, txNum, cckRates[i], value - base);
				}

				/*  OFDM */
				base = PHY_GetTxPowerByRate(padapter, band, path, txNum, MGN_54M);
				for (i = 0; i < sizeof(ofdmRates); ++i) {
					value = PHY_GetTxPowerByRate(padapter, band, path, txNum, ofdmRates[i]);
					PHY_SetTxPowerByRate(padapter, band, path, txNum, ofdmRates[i], value - base);
				}

				/*  HT MCS0~7 */
				base = PHY_GetTxPowerByRate(padapter, band, path, txNum, MGN_MCS7);
				for (i = 0; i < sizeof(mcs0_7Rates); ++i) {
					value = PHY_GetTxPowerByRate(padapter, band, path, txNum, mcs0_7Rates[i]);
					PHY_SetTxPowerByRate(padapter, band, path, txNum, mcs0_7Rates[i], value - base);
				}

				/*  HT MCS8~15 */
				base = PHY_GetTxPowerByRate(padapter, band, path, txNum, MGN_MCS15);
				for (i = 0; i < sizeof(mcs8_15Rates); ++i) {
					value = PHY_GetTxPowerByRate(padapter, band, path, txNum, mcs8_15Rates[i]);
					PHY_SetTxPowerByRate(padapter, band, path, txNum, mcs8_15Rates[i], value - base);
				}

				/*  HT MCS16~23 */
				base = PHY_GetTxPowerByRate(padapter, band, path, txNum, MGN_MCS23);
				for (i = 0; i < sizeof(mcs16_23Rates); ++i) {
					value = PHY_GetTxPowerByRate(padapter, band, path, txNum, mcs16_23Rates[i]);
					PHY_SetTxPowerByRate(padapter, band, path, txNum, mcs16_23Rates[i], value - base);
				}

				/*  VHT 1SS */
				base = PHY_GetTxPowerByRate(padapter, band, path, txNum, MGN_VHT1SS_MCS7);
				for (i = 0; i < sizeof(vht1ssRates); ++i) {
					value = PHY_GetTxPowerByRate(padapter, band, path, txNum, vht1ssRates[i]);
					PHY_SetTxPowerByRate(padapter, band, path, txNum, vht1ssRates[i], value - base);
				}

				/*  VHT 2SS */
				base = PHY_GetTxPowerByRate(padapter, band, path, txNum, MGN_VHT2SS_MCS7);
				for (i = 0; i < sizeof(vht2ssRates); ++i) {
					value = PHY_GetTxPowerByRate(padapter, band, path, txNum, vht2ssRates[i]);
					PHY_SetTxPowerByRate(padapter, band, path, txNum, vht2ssRates[i], value - base);
				}

				/*  VHT 3SS */
				base = PHY_GetTxPowerByRate(padapter, band, path, txNum, MGN_VHT3SS_MCS7);
				for (i = 0; i < sizeof(vht3ssRates); ++i) {
					value = PHY_GetTxPowerByRate(padapter, band, path, txNum, vht3ssRates[i]);
					PHY_SetTxPowerByRate(padapter, band, path, txNum, vht3ssRates[i], value - base);
				}
			}
		}
	}

	/* DBG_871X("<===PHY_ConvertTxPowerByRateInDbmToRelativeValues()\n"); */
}

/*
  * This function must be called if the value in the PHY_REG_PG.txt(or header)
  * is exact dBm values
  */
void PHY_TxPowerByRateConfiguration(struct adapter *padapter)
{
	phy_StoreTxPowerByRateBase(padapter);
	phy_ConvertTxPowerByRateInDbmToRelativeValues(padapter);
}

void PHY_SetTxPowerIndexByRateSection(
	struct adapter *padapter, u8 RFPath, u8 Channel, u8 RateSection
)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);

	if (RateSection == CCK) {
		u8 cckRates[]   = {MGN_1M, MGN_2M, MGN_5_5M, MGN_11M};
		if (pHalData->CurrentBandType == BAND_ON_2_4G)
			PHY_SetTxPowerIndexByRateArray(padapter, RFPath,
						     pHalData->CurrentChannelBW,
						     Channel, cckRates,
						     ARRAY_SIZE(cckRates));

	} else if (RateSection == OFDM) {
		u8 ofdmRates[]  = {MGN_6M, MGN_9M, MGN_12M, MGN_18M, MGN_24M, MGN_36M, MGN_48M, MGN_54M};
		PHY_SetTxPowerIndexByRateArray(padapter, RFPath,
					       pHalData->CurrentChannelBW,
					       Channel, ofdmRates,
					       ARRAY_SIZE(ofdmRates));

	} else if (RateSection == HT_MCS0_MCS7) {
		u8 htRates1T[]  = {MGN_MCS0, MGN_MCS1, MGN_MCS2, MGN_MCS3, MGN_MCS4, MGN_MCS5, MGN_MCS6, MGN_MCS7};
		PHY_SetTxPowerIndexByRateArray(padapter, RFPath,
					       pHalData->CurrentChannelBW,
					       Channel, htRates1T,
					       ARRAY_SIZE(htRates1T));

	} else if (RateSection == HT_MCS8_MCS15) {
		u8 htRates2T[]  = {MGN_MCS8, MGN_MCS9, MGN_MCS10, MGN_MCS11, MGN_MCS12, MGN_MCS13, MGN_MCS14, MGN_MCS15};
		PHY_SetTxPowerIndexByRateArray(padapter, RFPath,
					       pHalData->CurrentChannelBW,
					       Channel, htRates2T,
					       ARRAY_SIZE(htRates2T));

	} else if (RateSection == HT_MCS16_MCS23) {
		u8 htRates3T[]  = {MGN_MCS16, MGN_MCS17, MGN_MCS18, MGN_MCS19, MGN_MCS20, MGN_MCS21, MGN_MCS22, MGN_MCS23};
		PHY_SetTxPowerIndexByRateArray(padapter, RFPath,
					       pHalData->CurrentChannelBW,
					       Channel, htRates3T,
					       ARRAY_SIZE(htRates3T));

	} else if (RateSection == HT_MCS24_MCS31) {
		u8 htRates4T[]  = {MGN_MCS24, MGN_MCS25, MGN_MCS26, MGN_MCS27, MGN_MCS28, MGN_MCS29, MGN_MCS30, MGN_MCS31};
		PHY_SetTxPowerIndexByRateArray(padapter, RFPath,
					       pHalData->CurrentChannelBW,
					       Channel, htRates4T,
					       ARRAY_SIZE(htRates4T));

	} else if (RateSection == VHT_1SSMCS0_1SSMCS9) {
		u8 vhtRates1T[] = {MGN_VHT1SS_MCS0, MGN_VHT1SS_MCS1, MGN_VHT1SS_MCS2, MGN_VHT1SS_MCS3, MGN_VHT1SS_MCS4,
				MGN_VHT1SS_MCS5, MGN_VHT1SS_MCS6, MGN_VHT1SS_MCS7, MGN_VHT1SS_MCS8, MGN_VHT1SS_MCS9};
		PHY_SetTxPowerIndexByRateArray(padapter, RFPath,
					       pHalData->CurrentChannelBW,
					       Channel, vhtRates1T,
					       ARRAY_SIZE(vhtRates1T));

	} else if (RateSection == VHT_2SSMCS0_2SSMCS9) {
		u8 vhtRates2T[] = {MGN_VHT2SS_MCS0, MGN_VHT2SS_MCS1, MGN_VHT2SS_MCS2, MGN_VHT2SS_MCS3, MGN_VHT2SS_MCS4,
				MGN_VHT2SS_MCS5, MGN_VHT2SS_MCS6, MGN_VHT2SS_MCS7, MGN_VHT2SS_MCS8, MGN_VHT2SS_MCS9};

		PHY_SetTxPowerIndexByRateArray(padapter, RFPath,
					       pHalData->CurrentChannelBW,
					       Channel, vhtRates2T,
					       ARRAY_SIZE(vhtRates2T));
	} else if (RateSection == VHT_3SSMCS0_3SSMCS9) {
		u8 vhtRates3T[] = {MGN_VHT3SS_MCS0, MGN_VHT3SS_MCS1, MGN_VHT3SS_MCS2, MGN_VHT3SS_MCS3, MGN_VHT3SS_MCS4,
				MGN_VHT3SS_MCS5, MGN_VHT3SS_MCS6, MGN_VHT3SS_MCS7, MGN_VHT3SS_MCS8, MGN_VHT3SS_MCS9};

		PHY_SetTxPowerIndexByRateArray(padapter, RFPath,
					       pHalData->CurrentChannelBW,
					       Channel, vhtRates3T,
					       ARRAY_SIZE(vhtRates3T));
	} else if (RateSection == VHT_4SSMCS0_4SSMCS9) {
		u8 vhtRates4T[] = {MGN_VHT4SS_MCS0, MGN_VHT4SS_MCS1, MGN_VHT4SS_MCS2, MGN_VHT4SS_MCS3, MGN_VHT4SS_MCS4,
				MGN_VHT4SS_MCS5, MGN_VHT4SS_MCS6, MGN_VHT4SS_MCS7, MGN_VHT4SS_MCS8, MGN_VHT4SS_MCS9};

		PHY_SetTxPowerIndexByRateArray(padapter, RFPath,
					       pHalData->CurrentChannelBW,
					       Channel, vhtRates4T,
					       ARRAY_SIZE(vhtRates4T));
	} else
		DBG_871X("Invalid RateSection %d in %s", RateSection, __func__);
}

static bool phy_GetChnlIndex(u8 Channel, u8 *ChannelIdx)
{
	u8 channel5G[CHANNEL_MAX_NUMBER_5G] = {
		36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 100, 102,
		104, 106, 108, 110, 112, 114, 116, 118, 120, 122, 124, 126, 128, 130,
		132, 134, 136, 138, 140, 142, 144, 149, 151, 153, 155, 157, 159, 161,
		163, 165, 167, 168, 169, 171, 173, 175, 177
	};
	u8  i = 0;
	bool bIn24G = true;

	if (Channel <= 14) {
		bIn24G = true;
		*ChannelIdx = Channel-1;
	} else {
		bIn24G = false;

		for (i = 0; i < ARRAY_SIZE(channel5G); ++i) {
			if (channel5G[i] == Channel) {
				*ChannelIdx = i;
				return bIn24G;
			}
		}
	}

	return bIn24G;
}

u8 PHY_GetTxPowerIndexBase(
	struct adapter *padapter,
	u8 RFPath,
	u8 Rate,
	enum CHANNEL_WIDTH	BandWidth,
	u8 Channel,
	bool *bIn24G
)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	u8 i = 0;	/* default set to 1S */
	u8 txPower = 0;
	u8 chnlIdx = (Channel-1);

	if (HAL_IsLegalChannel(padapter, Channel) == false) {
		chnlIdx = 0;
		DBG_871X("Illegal channel!!\n");
	}

	*bIn24G = phy_GetChnlIndex(Channel, &chnlIdx);

	/* DBG_871X("[%s] Channel Index: %d\n", (*bIn24G?"2.4G":"5G"), chnlIdx); */

	if (*bIn24G) { /* 3 ============================== 2.4 G ============================== */
		if (IS_CCK_RATE(Rate))
			txPower = pHalData->Index24G_CCK_Base[RFPath][chnlIdx];
		else if (MGN_6M <= Rate)
			txPower = pHalData->Index24G_BW40_Base[RFPath][chnlIdx];
		else
			DBG_871X("PHY_GetTxPowerIndexBase: INVALID Rate.\n");

		/* DBG_871X("Base Tx power(RF-%c, Rate #%d, Channel Index %d) = 0x%X\n", */
		/*		((RFPath == 0)?'A':'B'), Rate, chnlIdx, txPower); */

		/*  OFDM-1T */
		if ((MGN_6M <= Rate && Rate <= MGN_54M) && !IS_CCK_RATE(Rate)) {
			txPower += pHalData->OFDM_24G_Diff[RFPath][TX_1S];
			/* DBG_871X("+PowerDiff 2.4G (RF-%c): (OFDM-1T) = (%d)\n", ((RFPath == 0)?'A':'B'), pHalData->OFDM_24G_Diff[RFPath][TX_1S]); */
		}
		if (BandWidth == CHANNEL_WIDTH_20) { /*  BW20-1S, BW20-2S */
			if ((MGN_MCS0 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT1SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW20_24G_Diff[RFPath][TX_1S];
			if ((MGN_MCS8 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT2SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW20_24G_Diff[RFPath][TX_2S];
			if ((MGN_MCS16 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT3SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW20_24G_Diff[RFPath][TX_3S];
			if ((MGN_MCS24 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT4SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW20_24G_Diff[RFPath][TX_4S];

			/* DBG_871X("+PowerDiff 2.4G (RF-%c): (BW20-1S, BW20-2S, BW20-3S, BW20-4S) = (%d, %d, %d, %d)\n", ((RFPath == 0)?'A':(RFPath == 1)?'B':(RFPath ==2)?'C':'D'), */
			/*	pHalData->BW20_24G_Diff[RFPath][TX_1S], pHalData->BW20_24G_Diff[RFPath][TX_2S], */
			/*	pHalData->BW20_24G_Diff[RFPath][TX_3S], pHalData->BW20_24G_Diff[RFPath][TX_4S]); */
		} else if (BandWidth == CHANNEL_WIDTH_40) { /*  BW40-1S, BW40-2S */
			if ((MGN_MCS0 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT1SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_24G_Diff[RFPath][TX_1S];
			if ((MGN_MCS8 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT2SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_24G_Diff[RFPath][TX_2S];
			if ((MGN_MCS16 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT3SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_24G_Diff[RFPath][TX_3S];
			if ((MGN_MCS24 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT4SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_24G_Diff[RFPath][TX_4S];

			/* DBG_871X("+PowerDiff 2.4G (RF-%c): (BW40-1S, BW40-2S, BW40-3S, BW40-4S) = (%d, %d, %d, %d)\n", ((RFPath == 0)?'A':(RFPath == 1)?'B':(RFPath ==2)?'C':'D'), */
			/*	pHalData->BW40_24G_Diff[RFPath][TX_1S], pHalData->BW40_24G_Diff[RFPath][TX_2S], */
			/*	pHalData->BW40_24G_Diff[RFPath][TX_3S], pHalData->BW40_24G_Diff[RFPath][TX_4S]); */
		}
		/*  Willis suggest adopt BW 40M power index while in BW 80 mode */
		else if (BandWidth == CHANNEL_WIDTH_80) {
			if ((MGN_MCS0 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT1SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_24G_Diff[RFPath][TX_1S];
			if ((MGN_MCS8 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT2SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_24G_Diff[RFPath][TX_2S];
			if ((MGN_MCS16 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT3SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_24G_Diff[RFPath][TX_3S];
			if ((MGN_MCS24 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT4SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_24G_Diff[RFPath][TX_4S];

			/* DBG_871X("+PowerDiff 2.4G (RF-%c): (BW40-1S, BW40-2S, BW40-3S, BW40-4T) = (%d, %d, %d, %d) P.S. Current is in BW 80MHz\n", ((RFPath == 0)?'A':(RFPath == 1)?'B':(RFPath ==2)?'C':'D'), */
			/*	pHalData->BW40_24G_Diff[RFPath][TX_1S], pHalData->BW40_24G_Diff[RFPath][TX_2S], */
			/*	pHalData->BW40_24G_Diff[RFPath][TX_3S], pHalData->BW40_24G_Diff[RFPath][TX_4S]); */
		}
	} else {/* 3 ============================== 5 G ============================== */
		if (MGN_6M <= Rate)
			txPower = pHalData->Index5G_BW40_Base[RFPath][chnlIdx];
		else
			DBG_871X("===> mpt_ProQueryCalTxPower_Jaguar: INVALID Rate.\n");

		/* DBG_871X("Base Tx power(RF-%c, Rate #%d, Channel Index %d) = 0x%X\n", */
		/*	((RFPath == 0)?'A':'B'), Rate, chnlIdx, txPower); */

		/*  OFDM-1T */
		if ((MGN_6M <= Rate && Rate <= MGN_54M) && !IS_CCK_RATE(Rate)) {
			txPower += pHalData->OFDM_5G_Diff[RFPath][TX_1S];
			/* DBG_871X("+PowerDiff 5G (RF-%c): (OFDM-1T) = (%d)\n", ((RFPath == 0)?'A':'B'), pHalData->OFDM_5G_Diff[RFPath][TX_1S]); */
		}

		/*  BW20-1S, BW20-2S */
		if (BandWidth == CHANNEL_WIDTH_20) {
			if ((MGN_MCS0 <= Rate && Rate <= MGN_MCS31)  || (MGN_VHT1SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW20_5G_Diff[RFPath][TX_1S];
			if ((MGN_MCS8 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT2SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW20_5G_Diff[RFPath][TX_2S];
			if ((MGN_MCS16 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT3SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW20_5G_Diff[RFPath][TX_3S];
			if ((MGN_MCS24 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT4SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW20_5G_Diff[RFPath][TX_4S];

			/* DBG_871X("+PowerDiff 5G (RF-%c): (BW20-1S, BW20-2S, BW20-3S, BW20-4S) = (%d, %d, %d, %d)\n", ((RFPath == 0)?'A':(RFPath == 1)?'B':(RFPath ==2)?'C':'D'), */
			/*	pHalData->BW20_5G_Diff[RFPath][TX_1S], pHalData->BW20_5G_Diff[RFPath][TX_2S], */
			/*	pHalData->BW20_5G_Diff[RFPath][TX_3S], pHalData->BW20_5G_Diff[RFPath][TX_4S]); */
		} else if (BandWidth == CHANNEL_WIDTH_40) { /*  BW40-1S, BW40-2S */
			if ((MGN_MCS0 <= Rate && Rate <= MGN_MCS31)  || (MGN_VHT1SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_5G_Diff[RFPath][TX_1S];
			if ((MGN_MCS8 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT2SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_5G_Diff[RFPath][TX_2S];
			if ((MGN_MCS16 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT3SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_5G_Diff[RFPath][TX_3S];
			if ((MGN_MCS24 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT4SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_5G_Diff[RFPath][TX_4S];

			/* DBG_871X("+PowerDiff 5G(RF-%c): (BW40-1S, BW40-2S) = (%d, %d, %d, %d)\n", ((RFPath == 0)?'A':(RFPath == 1)?'B':(RFPath ==2)?'C':'D'), */
			/*	pHalData->BW40_5G_Diff[RFPath][TX_1S], pHalData->BW40_5G_Diff[RFPath][TX_2S], */
			/*	pHalData->BW40_5G_Diff[RFPath][TX_3S], pHalData->BW40_5G_Diff[RFPath][TX_4S]); */
		} else if (BandWidth == CHANNEL_WIDTH_80) { /*  BW80-1S, BW80-2S */
			/*  <20121220, Kordan> Get the index of array "Index5G_BW80_Base". */
			u8 channel5G_80M[CHANNEL_MAX_NUMBER_5G_80M] = {42, 58, 106, 122, 138, 155, 171};
			for (i = 0; i < ARRAY_SIZE(channel5G_80M); ++i)
				if (channel5G_80M[i] == Channel)
					chnlIdx = i;

			txPower = pHalData->Index5G_BW80_Base[RFPath][chnlIdx];

			if ((MGN_MCS0 <= Rate && Rate <= MGN_MCS31)  || (MGN_VHT1SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += + pHalData->BW80_5G_Diff[RFPath][TX_1S];
			if ((MGN_MCS8 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT2SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW80_5G_Diff[RFPath][TX_2S];
			if ((MGN_MCS16 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT3SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW80_5G_Diff[RFPath][TX_3S];
			if ((MGN_MCS23 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT4SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW80_5G_Diff[RFPath][TX_4S];

			/* DBG_871X("+PowerDiff 5G(RF-%c): (BW80-1S, BW80-2S, BW80-3S, BW80-4S) = (%d, %d, %d, %d)\n", ((RFPath == 0)?'A':(RFPath == 1)?'B':(RFPath ==2)?'C':'D'), */
			/*	pHalData->BW80_5G_Diff[RFPath][TX_1S], pHalData->BW80_5G_Diff[RFPath][TX_2S], */
			/*	pHalData->BW80_5G_Diff[RFPath][TX_3S], pHalData->BW80_5G_Diff[RFPath][TX_4S]); */
		}
	}

	return txPower;
}

s8 PHY_GetTxPowerTrackingOffset(struct adapter *padapter, u8 RFPath, u8 Rate)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	PDM_ODM_T pDM_Odm = &pHalData->odmpriv;
	s8 offset = 0;

	if (pDM_Odm->RFCalibrateInfo.TxPowerTrackControl  == false)
		return offset;

	if ((Rate == MGN_1M) || (Rate == MGN_2M) || (Rate == MGN_5_5M) || (Rate == MGN_11M)) {
		offset = pDM_Odm->Remnant_CCKSwingIdx;
		/* DBG_871X("+Remnant_CCKSwingIdx = 0x%x\n", RFPath, Rate, pDM_Odm->Remnant_CCKSwingIdx); */
	} else {
		offset = pDM_Odm->Remnant_OFDMSwingIdx[RFPath];
		/* DBG_871X("+Remanant_OFDMSwingIdx[RFPath %u][Rate 0x%x] = 0x%x\n", RFPath, Rate, pDM_Odm->Remnant_OFDMSwingIdx[RFPath]); */

	}

	return offset;
}

u8 PHY_GetRateIndexOfTxPowerByRate(u8 Rate)
{
	u8 index = 0;
	switch (Rate) {
	case MGN_1M:
		index = 0;
		break;
	case MGN_2M:
		index = 1;
		break;
	case MGN_5_5M:
		index = 2;
		break;
	case MGN_11M:
		index = 3;
		break;
	case MGN_6M:
		index = 4;
		break;
	case MGN_9M:
		index = 5;
		break;
	case MGN_12M:
		index = 6;
		break;
	case MGN_18M:
		index = 7;
		break;
	case MGN_24M:
		index = 8;
		break;
	case MGN_36M:
		index = 9;
		break;
	case MGN_48M:
		index = 10;
		break;
	case MGN_54M:
		index = 11;
		break;
	case MGN_MCS0:
		index = 12;
		break;
	case MGN_MCS1:
		index = 13;
		break;
	case MGN_MCS2:
		index = 14;
		break;
	case MGN_MCS3:
		index = 15;
		break;
	case MGN_MCS4:
		index = 16;
		break;
	case MGN_MCS5:
		index = 17;
		break;
	case MGN_MCS6:
		index = 18;
		break;
	case MGN_MCS7:
		index = 19;
		break;
	case MGN_MCS8:
		index = 20;
		break;
	case MGN_MCS9:
		index = 21;
		break;
	case MGN_MCS10:
		index = 22;
		break;
	case MGN_MCS11:
		index = 23;
		break;
	case MGN_MCS12:
		index = 24;
		break;
	case MGN_MCS13:
		index = 25;
		break;
	case MGN_MCS14:
		index = 26;
		break;
	case MGN_MCS15:
		index = 27;
		break;
	case MGN_MCS16:
		index = 28;
		break;
	case MGN_MCS17:
		index = 29;
		break;
	case MGN_MCS18:
		index = 30;
		break;
	case MGN_MCS19:
		index = 31;
		break;
	case MGN_MCS20:
		index = 32;
		break;
	case MGN_MCS21:
		index = 33;
		break;
	case MGN_MCS22:
		index = 34;
		break;
	case MGN_MCS23:
		index = 35;
		break;
	case MGN_MCS24:
		index = 36;
		break;
	case MGN_MCS25:
		index = 37;
		break;
	case MGN_MCS26:
		index = 38;
		break;
	case MGN_MCS27:
		index = 39;
		break;
	case MGN_MCS28:
		index = 40;
		break;
	case MGN_MCS29:
		index = 41;
		break;
	case MGN_MCS30:
		index = 42;
		break;
	case MGN_MCS31:
		index = 43;
		break;
	case MGN_VHT1SS_MCS0:
		index = 44;
		break;
	case MGN_VHT1SS_MCS1:
		index = 45;
		break;
	case MGN_VHT1SS_MCS2:
		index = 46;
		break;
	case MGN_VHT1SS_MCS3:
		index = 47;
		break;
	case MGN_VHT1SS_MCS4:
		index = 48;
		break;
	case MGN_VHT1SS_MCS5:
		index = 49;
		break;
	case MGN_VHT1SS_MCS6:
		index = 50;
		break;
	case MGN_VHT1SS_MCS7:
		index = 51;
		break;
	case MGN_VHT1SS_MCS8:
		index = 52;
		break;
	case MGN_VHT1SS_MCS9:
		index = 53;
		break;
	case MGN_VHT2SS_MCS0:
		index = 54;
		break;
	case MGN_VHT2SS_MCS1:
		index = 55;
		break;
	case MGN_VHT2SS_MCS2:
		index = 56;
		break;
	case MGN_VHT2SS_MCS3:
		index = 57;
		break;
	case MGN_VHT2SS_MCS4:
		index = 58;
		break;
	case MGN_VHT2SS_MCS5:
		index = 59;
		break;
	case MGN_VHT2SS_MCS6:
		index = 60;
		break;
	case MGN_VHT2SS_MCS7:
		index = 61;
		break;
	case MGN_VHT2SS_MCS8:
		index = 62;
		break;
	case MGN_VHT2SS_MCS9:
		index = 63;
		break;
	case MGN_VHT3SS_MCS0:
		index = 64;
		break;
	case MGN_VHT3SS_MCS1:
		index = 65;
		break;
	case MGN_VHT3SS_MCS2:
		index = 66;
		break;
	case MGN_VHT3SS_MCS3:
		index = 67;
		break;
	case MGN_VHT3SS_MCS4:
		index = 68;
		break;
	case MGN_VHT3SS_MCS5:
		index = 69;
		break;
	case MGN_VHT3SS_MCS6:
		index = 70;
		break;
	case MGN_VHT3SS_MCS7:
		index = 71;
		break;
	case MGN_VHT3SS_MCS8:
		index = 72;
		break;
	case MGN_VHT3SS_MCS9:
		index = 73;
		break;
	case MGN_VHT4SS_MCS0:
		index = 74;
		break;
	case MGN_VHT4SS_MCS1:
		index = 75;
		break;
	case MGN_VHT4SS_MCS2:
		index = 76;
		break;
	case MGN_VHT4SS_MCS3:
		index = 77;
		break;
	case MGN_VHT4SS_MCS4:
		index = 78;
		break;
	case MGN_VHT4SS_MCS5:
		index = 79;
		break;
	case MGN_VHT4SS_MCS6:
		index = 80;
		break;
	case MGN_VHT4SS_MCS7:
		index = 81;
		break;
	case MGN_VHT4SS_MCS8:
		index = 82;
		break;
	case MGN_VHT4SS_MCS9:
		index = 83;
		break;
	default:
		DBG_871X("Invalid rate 0x%x in %s\n", Rate, __func__);
		break;
	}
	return index;
}

s8 PHY_GetTxPowerByRate(
	struct adapter *padapter, u8 Band, u8 RFPath, u8 TxNum, u8 Rate
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	s8 value = 0;
	u8 rateIndex = PHY_GetRateIndexOfTxPowerByRate(Rate);

	if ((padapter->registrypriv.RegEnableTxPowerByRate == 2 && pHalData->EEPROMRegulatory == 2) ||
		   padapter->registrypriv.RegEnableTxPowerByRate == 0)
		return 0;

	if (Band != BAND_ON_2_4G && Band != BAND_ON_5G) {
		DBG_871X("Invalid band %d in %s\n", Band, __func__);
		return value;
	}
	if (RFPath > ODM_RF_PATH_D) {
		DBG_871X("Invalid RfPath %d in %s\n", RFPath, __func__);
		return value;
	}
	if (TxNum >= RF_MAX_TX_NUM) {
		DBG_871X("Invalid TxNum %d in %s\n", TxNum, __func__);
		return value;
	}
	if (rateIndex >= TX_PWR_BY_RATE_NUM_RATE) {
		DBG_871X("Invalid RateIndex %d in %s\n", rateIndex, __func__);
		return value;
	}

	value = pHalData->TxPwrByRateOffset[Band][RFPath][TxNum][rateIndex];

	return value;

}

void PHY_SetTxPowerByRate(
	struct adapter *padapter,
	u8 Band,
	u8 RFPath,
	u8 TxNum,
	u8 Rate,
	s8 Value
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	u8 rateIndex = PHY_GetRateIndexOfTxPowerByRate(Rate);

	if (Band != BAND_ON_2_4G && Band != BAND_ON_5G) {
		DBG_871X("Invalid band %d in %s\n", Band, __func__);
		return;
	}
	if (RFPath > ODM_RF_PATH_D) {
		DBG_871X("Invalid RfPath %d in %s\n", RFPath, __func__);
		return;
	}
	if (TxNum >= RF_MAX_TX_NUM) {
		DBG_871X("Invalid TxNum %d in %s\n", TxNum, __func__);
		return;
	}
	if (rateIndex >= TX_PWR_BY_RATE_NUM_RATE) {
		DBG_871X("Invalid RateIndex %d in %s\n", rateIndex, __func__);
		return;
	}

	pHalData->TxPwrByRateOffset[Band][RFPath][TxNum][rateIndex] = Value;
}

void PHY_SetTxPowerLevelByPath(struct adapter *Adapter, u8 channel, u8 path)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);
	bool bIsIn24G = (pHalData->CurrentBandType == BAND_ON_2_4G);

	/* if (pMgntInfo->RegNByteAccess == 0) */
	{
		if (bIsIn24G)
			PHY_SetTxPowerIndexByRateSection(Adapter, path, channel, CCK);

		PHY_SetTxPowerIndexByRateSection(Adapter, path, channel, OFDM);
		PHY_SetTxPowerIndexByRateSection(Adapter, path, channel, HT_MCS0_MCS7);

		if (pHalData->NumTotalRFPath >= 2)
			PHY_SetTxPowerIndexByRateSection(Adapter, path, channel, HT_MCS8_MCS15);

	}
}

void PHY_SetTxPowerIndexByRateArray(
	struct adapter *padapter,
	u8 RFPath,
	enum CHANNEL_WIDTH BandWidth,
	u8 Channel,
	u8 *Rates,
	u8 RateArraySize
)
{
	u32 powerIndex = 0;
	int	i = 0;

	for (i = 0; i < RateArraySize; ++i) {
		powerIndex = PHY_GetTxPowerIndex(padapter, RFPath, Rates[i], BandWidth, Channel);
		PHY_SetTxPowerIndex(padapter, powerIndex, RFPath, Rates[i]);
	}
}

static s8 phy_GetWorldWideLimit(s8 *LimitTable)
{
	s8	min = LimitTable[0];
	u8 i = 0;

	for (i = 0; i < MAX_REGULATION_NUM; ++i) {
		if (LimitTable[i] < min)
			min = LimitTable[i];
	}

	return min;
}

static s8 phy_GetChannelIndexOfTxPowerLimit(u8 Band, u8 Channel)
{
	s8	channelIndex = -1;
	u8 channel5G[CHANNEL_MAX_NUMBER_5G] = {
		36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 100, 102,
		104, 106, 108, 110, 112, 114, 116, 118, 120, 122, 124, 126, 128, 130,
		132, 134, 136, 138, 140, 142, 144, 149, 151, 153, 155, 157, 159, 161,
		163, 165, 167, 168, 169, 171, 173, 175, 177
	};
	u8 i = 0;
	if (Band == BAND_ON_2_4G)
		channelIndex = Channel - 1;
	else if (Band == BAND_ON_5G) {
		for (i = 0; i < ARRAY_SIZE(channel5G); ++i) {
			if (channel5G[i] == Channel)
				channelIndex = i;
		}
	} else
		DBG_871X("Invalid Band %d in %s", Band, __func__);

	if (channelIndex == -1)
		DBG_871X("Invalid Channel %d of Band %d in %s", Channel, Band, __func__);

	return channelIndex;
}

s8 PHY_GetTxPowerLimit(
	struct adapter *Adapter,
	u32 RegPwrTblSel,
	enum BAND_TYPE Band,
	enum CHANNEL_WIDTH Bandwidth,
	u8 RfPath,
	u8 DataRate,
	u8 Channel
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);
	s16	band = -1, regulation = -1, bandwidth = -1, rateSection = -1, channel = -1;
	s8 powerLimit = MAX_POWER_INDEX;

	if ((Adapter->registrypriv.RegEnableTxPowerLimit == 2 && pHalData->EEPROMRegulatory != 1) ||
		   Adapter->registrypriv.RegEnableTxPowerLimit == 0)
		return MAX_POWER_INDEX;

	switch (Adapter->registrypriv.RegPwrTblSel) {
	case 1:
			regulation = TXPWR_LMT_ETSI;
			break;
	case 2:
			regulation = TXPWR_LMT_MKK;
			break;
	case 3:
			regulation = TXPWR_LMT_FCC;
			break;

	case 4:
			regulation = TXPWR_LMT_WW;
			break;

	default:
			regulation = (Band == BAND_ON_2_4G) ? pHalData->Regulation2_4G : pHalData->Regulation5G;
			break;
	}

	/* DBG_871X("pMgntInfo->RegPwrTblSel %d, final regulation %d\n", Adapter->registrypriv.RegPwrTblSel, regulation); */


	if (Band == BAND_ON_2_4G)
		band = 0;
	else if (Band == BAND_ON_5G)
		band = 1;

	if (Bandwidth == CHANNEL_WIDTH_20)
		bandwidth = 0;
	else if (Bandwidth == CHANNEL_WIDTH_40)
		bandwidth = 1;
	else if (Bandwidth == CHANNEL_WIDTH_80)
		bandwidth = 2;
	else if (Bandwidth == CHANNEL_WIDTH_160)
		bandwidth = 3;

	switch (DataRate) {
	case MGN_1M: case MGN_2M: case MGN_5_5M: case MGN_11M:
		rateSection = 0;
		break;

	case MGN_6M: case MGN_9M: case MGN_12M: case MGN_18M:
	case MGN_24M: case MGN_36M: case MGN_48M: case MGN_54M:
		rateSection = 1;
		break;

	case MGN_MCS0: case MGN_MCS1: case MGN_MCS2: case MGN_MCS3:
	case MGN_MCS4: case MGN_MCS5: case MGN_MCS6: case MGN_MCS7:
		rateSection = 2;
		break;

	case MGN_MCS8: case MGN_MCS9: case MGN_MCS10: case MGN_MCS11:
	case MGN_MCS12: case MGN_MCS13: case MGN_MCS14: case MGN_MCS15:
		rateSection = 3;
		break;

	case MGN_MCS16: case MGN_MCS17: case MGN_MCS18: case MGN_MCS19:
	case MGN_MCS20: case MGN_MCS21: case MGN_MCS22: case MGN_MCS23:
		rateSection = 4;
		break;

	case MGN_MCS24: case MGN_MCS25: case MGN_MCS26: case MGN_MCS27:
	case MGN_MCS28: case MGN_MCS29: case MGN_MCS30: case MGN_MCS31:
		rateSection = 5;
		break;

	case MGN_VHT1SS_MCS0: case MGN_VHT1SS_MCS1: case MGN_VHT1SS_MCS2:
	case MGN_VHT1SS_MCS3: case MGN_VHT1SS_MCS4: case MGN_VHT1SS_MCS5:
	case MGN_VHT1SS_MCS6: case MGN_VHT1SS_MCS7: case MGN_VHT1SS_MCS8:
	case MGN_VHT1SS_MCS9:
		rateSection = 6;
		break;

	case MGN_VHT2SS_MCS0: case MGN_VHT2SS_MCS1: case MGN_VHT2SS_MCS2:
	case MGN_VHT2SS_MCS3: case MGN_VHT2SS_MCS4: case MGN_VHT2SS_MCS5:
	case MGN_VHT2SS_MCS6: case MGN_VHT2SS_MCS7: case MGN_VHT2SS_MCS8:
	case MGN_VHT2SS_MCS9:
		rateSection = 7;
		break;

	case MGN_VHT3SS_MCS0: case MGN_VHT3SS_MCS1: case MGN_VHT3SS_MCS2:
	case MGN_VHT3SS_MCS3: case MGN_VHT3SS_MCS4: case MGN_VHT3SS_MCS5:
	case MGN_VHT3SS_MCS6: case MGN_VHT3SS_MCS7: case MGN_VHT3SS_MCS8:
	case MGN_VHT3SS_MCS9:
		rateSection = 8;
		break;

	case MGN_VHT4SS_MCS0: case MGN_VHT4SS_MCS1: case MGN_VHT4SS_MCS2:
	case MGN_VHT4SS_MCS3: case MGN_VHT4SS_MCS4: case MGN_VHT4SS_MCS5:
	case MGN_VHT4SS_MCS6: case MGN_VHT4SS_MCS7: case MGN_VHT4SS_MCS8:
	case MGN_VHT4SS_MCS9:
		rateSection = 9;
		break;

	default:
		DBG_871X("Wrong rate 0x%x\n", DataRate);
		break;
	}

	if (Band == BAND_ON_5G  && rateSection == 0)
			DBG_871X("Wrong rate 0x%x: No CCK in 5G Band\n", DataRate);

	/*  workaround for wrong index combination to obtain tx power limit, */
	/*  OFDM only exists in BW 20M */
	if (rateSection == 1)
		bandwidth = 0;

	/*  workaround for wrong index combination to obtain tx power limit, */
	/*  CCK table will only be given in BW 20M */
	if (rateSection == 0)
		bandwidth = 0;

	/*  workaround for wrong indxe combination to obtain tx power limit, */
	/*  HT on 80M will reference to HT on 40M */
	if ((rateSection == 2 || rateSection == 3) && Band == BAND_ON_5G && bandwidth == 2) {
		bandwidth = 1;
	}

	if (Band == BAND_ON_2_4G)
		channel = phy_GetChannelIndexOfTxPowerLimit(BAND_ON_2_4G, Channel);
	else if (Band == BAND_ON_5G)
		channel = phy_GetChannelIndexOfTxPowerLimit(BAND_ON_5G, Channel);
	else if (Band == BAND_ON_BOTH) {
		/*  BAND_ON_BOTH don't care temporarily */
	}

	if (band == -1 || regulation == -1 || bandwidth == -1 ||
	     rateSection == -1 || channel == -1) {
		/* DBG_871X("Wrong index value to access power limit table [band %d][regulation %d][bandwidth %d][rf_path %d][rate_section %d][chnlGroup %d]\n", */
		/*	  band, regulation, bandwidth, RfPath, rateSection, channelGroup); */

		return MAX_POWER_INDEX;
	}

	if (Band == BAND_ON_2_4G) {
		s8 limits[10] = {0}; u8 i = 0;
		for (i = 0; i < MAX_REGULATION_NUM; i++)
			limits[i] = pHalData->TxPwrLimit_2_4G[i][bandwidth][rateSection][channel][RfPath];

		powerLimit = (regulation == TXPWR_LMT_WW) ? phy_GetWorldWideLimit(limits) :
			pHalData->TxPwrLimit_2_4G[regulation][bandwidth][rateSection][channel][RfPath];

	} else if (Band == BAND_ON_5G) {
		s8 limits[10] = {0}; u8 i = 0;
		for (i = 0; i < MAX_REGULATION_NUM; ++i)
			limits[i] = pHalData->TxPwrLimit_5G[i][bandwidth][rateSection][channel][RfPath];

		powerLimit = (regulation == TXPWR_LMT_WW) ? phy_GetWorldWideLimit(limits) :
			pHalData->TxPwrLimit_5G[regulation][bandwidth][rateSection][channel][RfPath];
	} else
		DBG_871X("No power limit table of the specified band\n");

	/*  combine 5G VHT & HT rate */
	/*  5G 20M and 40M HT and VHT can cross reference */
	/*
	if (Band == BAND_ON_5G && powerLimit == MAX_POWER_INDEX) {
		if (bandwidth == 0 || bandwidth == 1) {
			RT_TRACE(COMP_INIT, DBG_LOUD, ("No power limit table of the specified band %d, bandwidth %d, ratesection %d, rf path %d\n",
					  band, bandwidth, rateSection, RfPath));
			if (rateSection == 2)
				powerLimit = pHalData->TxPwrLimit_5G[regulation]
										[bandwidth][4][channelGroup][RfPath];
			else if (rateSection == 4)
				powerLimit = pHalData->TxPwrLimit_5G[regulation]
										[bandwidth][2][channelGroup][RfPath];
			else if (rateSection == 3)
				powerLimit = pHalData->TxPwrLimit_5G[regulation]
										[bandwidth][5][channelGroup][RfPath];
			else if (rateSection == 5)
				powerLimit = pHalData->TxPwrLimit_5G[regulation]
										[bandwidth][3][channelGroup][RfPath];
		}
	}
	*/
	/* DBG_871X("TxPwrLmt[Regulation %d][Band %d][BW %d][RFPath %d][Rate 0x%x][Chnl %d] = %d\n", */
	/*		regulation, pHalData->CurrentBandType, Bandwidth, RfPath, DataRate, Channel, powerLimit); */
	return powerLimit;
}

static void phy_CrossReferenceHTAndVHTTxPowerLimit(struct adapter *padapter)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	u8 regulation, bw, channel, rateSection;
	s8 tempPwrLmt = 0;

	for (regulation = 0; regulation < MAX_REGULATION_NUM; ++regulation) {
		for (bw = 0; bw < MAX_5G_BANDWITH_NUM; ++bw) {
			for (channel = 0; channel < CHANNEL_MAX_NUMBER_5G; ++channel) {
				for (rateSection = 0; rateSection < MAX_RATE_SECTION_NUM; ++rateSection) {
					tempPwrLmt = pHalData->TxPwrLimit_5G[regulation][bw][rateSection][channel][ODM_RF_PATH_A];
					if (tempPwrLmt == MAX_POWER_INDEX) {
						u8 baseSection = 2, refSection = 6;
						if (bw == 0 || bw == 1) { /*  5G 20M 40M VHT and HT can cross reference */
							/* DBG_871X("No power limit table of the specified band %d, bandwidth %d, ratesection %d, channel %d, rf path %d\n", */
							/*			1, bw, rateSection, channel, ODM_RF_PATH_A); */
							if (rateSection >= 2 && rateSection <= 9) {
								if (rateSection == 2) {
									baseSection = 2;
									refSection = 6;
								} else if (rateSection == 3) {
									baseSection = 3;
									refSection = 7;
								} else if (rateSection == 4) {
									baseSection = 4;
									refSection = 8;
								} else if (rateSection == 5) {
									baseSection = 5;
									refSection = 9;
								} else if (rateSection == 6) {
									baseSection = 6;
									refSection = 2;
								} else if (rateSection == 7) {
									baseSection = 7;
									refSection = 3;
								} else if (rateSection == 8) {
									baseSection = 8;
									refSection = 4;
								} else if (rateSection == 9) {
									baseSection = 9;
									refSection = 5;
								}
								pHalData->TxPwrLimit_5G[regulation][bw][baseSection][channel][ODM_RF_PATH_A] =
									pHalData->TxPwrLimit_5G[regulation][bw][refSection][channel][ODM_RF_PATH_A];
							}

							/* DBG_871X("use other value %d", tempPwrLmt); */
						}
					}
				}
			}
		}
	}
}

void PHY_ConvertTxPowerLimitToPowerIndex(struct adapter *Adapter)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);
	u8 BW40PwrBasedBm2_4G = 0x2E;
	u8 regulation, bw, channel, rateSection;
	s8 tempValue = 0, tempPwrLmt = 0;
	u8 rfPath = 0;

	/* DBG_871X("=====> PHY_ConvertTxPowerLimitToPowerIndex()\n"); */

	phy_CrossReferenceHTAndVHTTxPowerLimit(Adapter);

	for (regulation = 0; regulation < MAX_REGULATION_NUM; ++regulation) {
		for (bw = 0; bw < MAX_2_4G_BANDWITH_NUM; ++bw) {
			for (channel = 0; channel < CHANNEL_MAX_NUMBER_2G; ++channel) {
				for (rateSection = 0; rateSection < MAX_RATE_SECTION_NUM; ++rateSection) {
					tempPwrLmt = pHalData->TxPwrLimit_2_4G[regulation][bw][rateSection][channel][ODM_RF_PATH_A];

					for (rfPath = ODM_RF_PATH_A; rfPath < MAX_RF_PATH_NUM; ++rfPath) {
						if (pHalData->odmpriv.PhyRegPgValueType == PHY_REG_PG_EXACT_VALUE) {
							if (rateSection == 5) /*  HT 4T */
								BW40PwrBasedBm2_4G = PHY_GetTxPowerByRateBase(Adapter, BAND_ON_2_4G, rfPath, RF_4TX, HT_MCS24_MCS31);
							else if (rateSection == 4) /*  HT 3T */
								BW40PwrBasedBm2_4G = PHY_GetTxPowerByRateBase(Adapter, BAND_ON_2_4G, rfPath, RF_3TX, HT_MCS16_MCS23);
							else if (rateSection == 3) /*  HT 2T */
								BW40PwrBasedBm2_4G = PHY_GetTxPowerByRateBase(Adapter, BAND_ON_2_4G, rfPath, RF_2TX, HT_MCS8_MCS15);
							else if (rateSection == 2) /*  HT 1T */
								BW40PwrBasedBm2_4G = PHY_GetTxPowerByRateBase(Adapter, BAND_ON_2_4G, rfPath, RF_1TX, HT_MCS0_MCS7);
							else if (rateSection == 1) /*  OFDM */
								BW40PwrBasedBm2_4G = PHY_GetTxPowerByRateBase(Adapter, BAND_ON_2_4G, rfPath, RF_1TX, OFDM);
							else if (rateSection == 0) /*  CCK */
								BW40PwrBasedBm2_4G = PHY_GetTxPowerByRateBase(Adapter, BAND_ON_2_4G, rfPath, RF_1TX, CCK);
						} else
							BW40PwrBasedBm2_4G = Adapter->registrypriv.RegPowerBase * 2;

						if (tempPwrLmt != MAX_POWER_INDEX) {
							tempValue = tempPwrLmt - BW40PwrBasedBm2_4G;
							pHalData->TxPwrLimit_2_4G[regulation][bw][rateSection][channel][rfPath] = tempValue;
						}
					}
				}
			}
		}
	}

	/* DBG_871X("<===== PHY_ConvertTxPowerLimitToPowerIndex()\n"); */
}

void PHY_InitTxPowerLimit(struct adapter *Adapter)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);
	u8 i, j, k, l, m;

	/* DBG_871X("=====> PHY_InitTxPowerLimit()!\n"); */

	for (i = 0; i < MAX_REGULATION_NUM; ++i) {
		for (j = 0; j < MAX_2_4G_BANDWITH_NUM; ++j)
			for (k = 0; k < MAX_RATE_SECTION_NUM; ++k)
				for (m = 0; m < CHANNEL_MAX_NUMBER_2G; ++m)
					for (l = 0; l < MAX_RF_PATH_NUM; ++l)
						pHalData->TxPwrLimit_2_4G[i][j][k][m][l] = MAX_POWER_INDEX;
	}

	for (i = 0; i < MAX_REGULATION_NUM; ++i) {
		for (j = 0; j < MAX_5G_BANDWITH_NUM; ++j)
			for (k = 0; k < MAX_RATE_SECTION_NUM; ++k)
				for (m = 0; m < CHANNEL_MAX_NUMBER_5G; ++m)
					for (l = 0; l < MAX_RF_PATH_NUM; ++l)
						pHalData->TxPwrLimit_5G[i][j][k][m][l] = MAX_POWER_INDEX;
	}

	/* DBG_871X("<===== PHY_InitTxPowerLimit()!\n"); */
}

void PHY_SetTxPowerLimit(
	struct adapter *Adapter,
	u8 *Regulation,
	u8 *Band,
	u8 *Bandwidth,
	u8 *RateSection,
	u8 *RfPath,
	u8 *Channel,
	u8 *PowerLimit
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);
	u8 regulation = 0, bandwidth = 0, rateSection = 0, channel;
	s8 powerLimit = 0, prevPowerLimit, channelIndex;

	/* DBG_871X("Index of power limit table [band %s][regulation %s][bw %s][rate section %s][rf path %s][chnl %s][val %s]\n", */
	/*	  Band, Regulation, Bandwidth, RateSection, RfPath, Channel, PowerLimit); */

	if (!GetU1ByteIntegerFromStringInDecimal((s8 *)Channel, &channel) ||
		 !GetU1ByteIntegerFromStringInDecimal((s8 *)PowerLimit, &powerLimit))
		DBG_871X("Illegal index of power limit table [chnl %s][val %s]\n", Channel, PowerLimit);

	powerLimit = powerLimit > MAX_POWER_INDEX ? MAX_POWER_INDEX : powerLimit;

	if (eqNByte(Regulation, (u8 *)("FCC"), 3))
		regulation = 0;
	else if (eqNByte(Regulation, (u8 *)("MKK"), 3))
		regulation = 1;
	else if (eqNByte(Regulation, (u8 *)("ETSI"), 4))
		regulation = 2;
	else if (eqNByte(Regulation, (u8 *)("WW13"), 4))
		regulation = 3;

	if (eqNByte(RateSection, (u8 *)("CCK"), 3) && eqNByte(RfPath, (u8 *)("1T"), 2))
		rateSection = 0;
	else if (eqNByte(RateSection, (u8 *)("OFDM"), 4) && eqNByte(RfPath, (u8 *)("1T"), 2))
		rateSection = 1;
	else if (eqNByte(RateSection, (u8 *)("HT"), 2) && eqNByte(RfPath, (u8 *)("1T"), 2))
		rateSection = 2;
	else if (eqNByte(RateSection, (u8 *)("HT"), 2) && eqNByte(RfPath, (u8 *)("2T"), 2))
		rateSection = 3;
	else if (eqNByte(RateSection, (u8 *)("HT"), 2) && eqNByte(RfPath, (u8 *)("3T"), 2))
		rateSection = 4;
	else if (eqNByte(RateSection, (u8 *)("HT"), 2) && eqNByte(RfPath, (u8 *)("4T"), 2))
		rateSection = 5;
	else if (eqNByte(RateSection, (u8 *)("VHT"), 3) && eqNByte(RfPath, (u8 *)("1T"), 2))
		rateSection = 6;
	else if (eqNByte(RateSection, (u8 *)("VHT"), 3) && eqNByte(RfPath, (u8 *)("2T"), 2))
		rateSection = 7;
	else if (eqNByte(RateSection, (u8 *)("VHT"), 3) && eqNByte(RfPath, (u8 *)("3T"), 2))
		rateSection = 8;
	else if (eqNByte(RateSection, (u8 *)("VHT"), 3) && eqNByte(RfPath, (u8 *)("4T"), 2))
		rateSection = 9;
	else {
		DBG_871X("Wrong rate section!\n");
		return;
	}


	if (eqNByte(Bandwidth, (u8 *)("20M"), 3))
		bandwidth = 0;
	else if (eqNByte(Bandwidth, (u8 *)("40M"), 3))
		bandwidth = 1;
	else if (eqNByte(Bandwidth, (u8 *)("80M"), 3))
		bandwidth = 2;
	else if (eqNByte(Bandwidth, (u8 *)("160M"), 4))
		bandwidth = 3;

	if (eqNByte(Band, (u8 *)("2.4G"), 4)) {
		channelIndex = phy_GetChannelIndexOfTxPowerLimit(BAND_ON_2_4G, channel);

		if (channelIndex == -1)
			return;

		prevPowerLimit = pHalData->TxPwrLimit_2_4G[regulation][bandwidth][rateSection][channelIndex][ODM_RF_PATH_A];

		if (powerLimit < prevPowerLimit)
			pHalData->TxPwrLimit_2_4G[regulation][bandwidth][rateSection][channelIndex][ODM_RF_PATH_A] = powerLimit;

		/* DBG_871X("2.4G Band value : [regulation %d][bw %d][rate_section %d][chnl %d][val %d]\n", */
		/*	  regulation, bandwidth, rateSection, channelIndex, pHalData->TxPwrLimit_2_4G[regulation][bandwidth][rateSection][channelIndex][ODM_RF_PATH_A]); */
	} else if (eqNByte(Band, (u8 *)("5G"), 2)) {
		channelIndex = phy_GetChannelIndexOfTxPowerLimit(BAND_ON_5G, channel);

		if (channelIndex == -1)
			return;

		prevPowerLimit = pHalData->TxPwrLimit_5G[regulation][bandwidth][rateSection][channelIndex][ODM_RF_PATH_A];

		if (powerLimit < prevPowerLimit)
			pHalData->TxPwrLimit_5G[regulation][bandwidth][rateSection][channelIndex][ODM_RF_PATH_A] = powerLimit;

		/* DBG_871X("5G Band value : [regulation %d][bw %d][rate_section %d][chnl %d][val %d]\n", */
		/*	  regulation, bandwidth, rateSection, channel, pHalData->TxPwrLimit_5G[regulation][bandwidth][rateSection][channelIndex][ODM_RF_PATH_A]); */
	} else {
		DBG_871X("Cannot recognize the band info in %s\n", Band);
		return;
	}
}

u8 PHY_GetTxPowerIndex(
	struct adapter *padapter,
	u8 RFPath,
	u8 Rate,
	enum CHANNEL_WIDTH BandWidth,
	u8 Channel
)
{
	return PHY_GetTxPowerIndex_8723B(padapter, RFPath, Rate, BandWidth, Channel);
}

void PHY_SetTxPowerIndex(
	struct adapter *padapter, u32 PowerIndex, u8 RFPath, u8 Rate
)
{
	PHY_SetTxPowerIndex_8723B(padapter, PowerIndex, RFPath, Rate);
}

void Hal_ChannelPlanToRegulation(struct adapter *Adapter, u16 ChannelPlan)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);
	pHalData->Regulation2_4G = TXPWR_LMT_WW;
	pHalData->Regulation5G = TXPWR_LMT_WW;

	switch (ChannelPlan) {
	case RT_CHANNEL_DOMAIN_WORLD_NULL:
		pHalData->Regulation2_4G = TXPWR_LMT_WW;
		break;
	case RT_CHANNEL_DOMAIN_ETSI1_NULL:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_FCC1_NULL:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_MKK1_NULL:
		pHalData->Regulation2_4G = TXPWR_LMT_MKK;
		break;
	case RT_CHANNEL_DOMAIN_ETSI2_NULL:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_FCC1_FCC1:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		pHalData->Regulation5G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI1:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		pHalData->Regulation5G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_MKK1_MKK1:
		pHalData->Regulation2_4G = TXPWR_LMT_MKK;
		pHalData->Regulation5G = TXPWR_LMT_MKK;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_KCC1:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		pHalData->Regulation5G = TXPWR_LMT_MKK;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_FCC2:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		pHalData->Regulation5G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_FCC3:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		pHalData->Regulation5G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_FCC4:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		pHalData->Regulation5G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_FCC5:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		pHalData->Regulation5G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_FCC6:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		pHalData->Regulation5G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_FCC1_FCC7:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		pHalData->Regulation5G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI2:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		pHalData->Regulation5G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI3:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		pHalData->Regulation5G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_MKK1_MKK2:
		pHalData->Regulation2_4G = TXPWR_LMT_MKK;
		pHalData->Regulation5G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_MKK1_MKK3:
		pHalData->Regulation2_4G = TXPWR_LMT_MKK;
		pHalData->Regulation5G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_FCC1_NCC1:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		pHalData->Regulation5G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_FCC1_NCC2:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		pHalData->Regulation5G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_GLOBAL_NULL:
		pHalData->Regulation2_4G = TXPWR_LMT_WW;
		pHalData->Regulation5G = TXPWR_LMT_WW;
		break;
	case RT_CHANNEL_DOMAIN_ETSI1_ETSI4:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		pHalData->Regulation5G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_FCC1_FCC2:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		pHalData->Regulation5G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_FCC1_NCC3:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		pHalData->Regulation5G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI5:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		pHalData->Regulation5G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_FCC1_FCC8:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		pHalData->Regulation5G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI6:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		pHalData->Regulation5G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI7:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		pHalData->Regulation5G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI8:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		pHalData->Regulation5G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI9:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		pHalData->Regulation5G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI10:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		pHalData->Regulation5G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI11:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		pHalData->Regulation5G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_FCC1_NCC4:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		pHalData->Regulation5G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI12:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		pHalData->Regulation5G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_FCC1_FCC9:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		pHalData->Regulation5G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI13:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		pHalData->Regulation5G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_FCC1_FCC10:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		pHalData->Regulation5G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_REALTEK_DEFINE: /* Realtek Reserve */
		pHalData->Regulation2_4G = TXPWR_LMT_WW;
		pHalData->Regulation5G = TXPWR_LMT_WW;
		break;
	default:
		break;
	}
}


static char file_path_bs[PATH_MAX];

#define GetLineFromBuffer(buffer)	 strsep(&buffer, "\n")

int phy_ConfigMACWithParaFile(struct adapter *Adapter, char *pFileName)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);
	int	rlen = 0, rtStatus = _FAIL;
	char *szLine, *ptmp;
	u32 u4bRegOffset, u4bRegValue, u4bMove;

	if (!(Adapter->registrypriv.load_phy_file & LOAD_MAC_PARA_FILE))
		return rtStatus;

	memset(pHalData->para_file_buf, 0, MAX_PARA_FILE_BUF_LEN);

	if ((pHalData->mac_reg_len == 0) && (pHalData->mac_reg == NULL)) {
		rtw_merge_string(file_path_bs, PATH_MAX, rtw_phy_file_path, pFileName);

		if (rtw_is_file_readable(file_path_bs) == true) {
			rlen = rtw_retrive_from_file(file_path_bs, pHalData->para_file_buf, MAX_PARA_FILE_BUF_LEN);
			if (rlen > 0) {
				rtStatus = _SUCCESS;
				pHalData->mac_reg = vzalloc(rlen);
				if (pHalData->mac_reg) {
					memcpy(pHalData->mac_reg, pHalData->para_file_buf, rlen);
					pHalData->mac_reg_len = rlen;
				} else
					DBG_871X("%s mac_reg alloc fail !\n", __func__);
			}
		}
	} else {
		if ((pHalData->mac_reg_len != 0) && (pHalData->mac_reg != NULL)) {
			memcpy(pHalData->para_file_buf, pHalData->mac_reg, pHalData->mac_reg_len);
			rtStatus = _SUCCESS;
		} else
			DBG_871X("%s(): Critical Error !!!\n", __func__);
	}

	if (rtStatus == _SUCCESS) {
		ptmp = pHalData->para_file_buf;
		for (szLine = GetLineFromBuffer(ptmp); szLine != NULL; szLine = GetLineFromBuffer(ptmp)) {
			if (!IsCommentString(szLine)) {
				/*  Get 1st hex value as register offset */
				if (GetHexValueFromString(szLine, &u4bRegOffset, &u4bMove)) {
					if (u4bRegOffset == 0xffff) /*  Ending. */
						break;

					/*  Get 2nd hex value as register value. */
					szLine += u4bMove;
					if (GetHexValueFromString(szLine, &u4bRegValue, &u4bMove))
						rtw_write8(Adapter, u4bRegOffset, (u8)u4bRegValue);
				}
			}
		}
	} else
		DBG_871X("%s(): No File %s, Load from HWImg Array!\n", __func__, pFileName);

	return rtStatus;
}

int phy_ConfigBBWithParaFile(
	struct adapter *Adapter, char *pFileName, u32 ConfigType
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);
	int	rlen = 0, rtStatus = _FAIL;
	char *szLine, *ptmp;
	u32 u4bRegOffset, u4bRegValue, u4bMove;
	char *pBuf = NULL;
	u32 *pBufLen = NULL;

	if (!(Adapter->registrypriv.load_phy_file & LOAD_BB_PARA_FILE))
		return rtStatus;

	switch (ConfigType) {
	case CONFIG_BB_PHY_REG:
		pBuf = pHalData->bb_phy_reg;
		pBufLen = &pHalData->bb_phy_reg_len;
		break;
	case CONFIG_BB_AGC_TAB:
		pBuf = pHalData->bb_agc_tab;
		pBufLen = &pHalData->bb_agc_tab_len;
		break;
	default:
		DBG_871X("Unknown ConfigType!! %d\r\n", ConfigType);
		break;
	}

	memset(pHalData->para_file_buf, 0, MAX_PARA_FILE_BUF_LEN);

	if ((pBufLen != NULL) && (*pBufLen == 0) && (pBuf == NULL)) {
		rtw_merge_string(file_path_bs, PATH_MAX, rtw_phy_file_path, pFileName);

		if (rtw_is_file_readable(file_path_bs) == true) {
			rlen = rtw_retrive_from_file(file_path_bs, pHalData->para_file_buf, MAX_PARA_FILE_BUF_LEN);
			if (rlen > 0) {
				rtStatus = _SUCCESS;
				pBuf = vzalloc(rlen);
				if (pBuf) {
					memcpy(pBuf, pHalData->para_file_buf, rlen);
					*pBufLen = rlen;

					switch (ConfigType) {
					case CONFIG_BB_PHY_REG:
						pHalData->bb_phy_reg = pBuf;
						break;
					case CONFIG_BB_AGC_TAB:
						pHalData->bb_agc_tab = pBuf;
						break;
					}
				} else
					DBG_871X("%s(): ConfigType %d  alloc fail !\n", __func__, ConfigType);
			}
		}
	} else {
		if ((pBufLen != NULL) && (*pBufLen == 0) && (pBuf == NULL)) {
			memcpy(pHalData->para_file_buf, pBuf, *pBufLen);
			rtStatus = _SUCCESS;
		} else
			DBG_871X("%s(): Critical Error !!!\n", __func__);
	}

	if (rtStatus == _SUCCESS) {
		ptmp = pHalData->para_file_buf;
		for (szLine = GetLineFromBuffer(ptmp); szLine != NULL; szLine = GetLineFromBuffer(ptmp)) {
			if (!IsCommentString(szLine)) {
				/*  Get 1st hex value as register offset. */
				if (GetHexValueFromString(szLine, &u4bRegOffset, &u4bMove)) {
					if (u4bRegOffset == 0xffff) /*  Ending. */
						break;
					else if (u4bRegOffset == 0xfe || u4bRegOffset == 0xffe)
						msleep(50);
					else if (u4bRegOffset == 0xfd)
						mdelay(5);
					else if (u4bRegOffset == 0xfc)
						mdelay(1);
					else if (u4bRegOffset == 0xfb)
						udelay(50);
					else if (u4bRegOffset == 0xfa)
						udelay(5);
					else if (u4bRegOffset == 0xf9)
						udelay(1);

					/*  Get 2nd hex value as register value. */
					szLine += u4bMove;
					if (GetHexValueFromString(szLine, &u4bRegValue, &u4bMove)) {
						/* DBG_871X("[BB-ADDR]%03lX =%08lX\n", u4bRegOffset, u4bRegValue); */
						PHY_SetBBReg(Adapter, u4bRegOffset, bMaskDWord, u4bRegValue);

						if (u4bRegOffset == 0xa24)
							pHalData->odmpriv.RFCalibrateInfo.RegA24 = u4bRegValue;

						/*  Add 1us delay between BB/RF register setting. */
						udelay(1);
					}
				}
			}
		}
	} else
		DBG_871X("%s(): No File %s, Load from HWImg Array!\n", __func__, pFileName);

	return rtStatus;
}

static void phy_DecryptBBPgParaFile(struct adapter *Adapter, char *buffer)
{
	u32 i = 0, j = 0;
	u8 map[95] = {0};
	u8 currentChar;
	char *BufOfLines, *ptmp;

	/* DBG_871X("=====>phy_DecryptBBPgParaFile()\n"); */
	/*  32 the ascii code of the first visable char, 126 the last one */
	for (i = 0; i < 95; ++i)
		map[i] = (u8) (94 - i);

	ptmp = buffer;
	i = 0;
	for (BufOfLines = GetLineFromBuffer(ptmp); BufOfLines != NULL; BufOfLines = GetLineFromBuffer(ptmp)) {
		/* DBG_871X("Encrypted Line: %s\n", BufOfLines); */

		for (j = 0; j < strlen(BufOfLines); ++j) {
			currentChar = BufOfLines[j];

			if (currentChar == '\0')
				break;

			currentChar -=  (u8) ((((i + j) * 3) % 128));

			BufOfLines[j] = map[currentChar - 32] + 32;
		}
		/* DBG_871X("Decrypted Line: %s\n", BufOfLines); */
		if (strlen(BufOfLines) != 0)
			i++;
		BufOfLines[strlen(BufOfLines)] = '\n';
	}
}

static int phy_ParseBBPgParaFile(struct adapter *Adapter, char *buffer)
{
	int	rtStatus = _SUCCESS;
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);
	char *szLine, *ptmp;
	u32 u4bRegOffset, u4bRegMask, u4bRegValue;
	u32 u4bMove;
	bool firstLine = true;
	u8 tx_num = 0;
	u8 band = 0, rf_path = 0;

	/* DBG_871X("=====>phy_ParseBBPgParaFile()\n"); */

	if (Adapter->registrypriv.RegDecryptCustomFile == 1)
		phy_DecryptBBPgParaFile(Adapter, buffer);

	ptmp = buffer;
	for (szLine = GetLineFromBuffer(ptmp); szLine != NULL; szLine = GetLineFromBuffer(ptmp)) {
		if (!IsCommentString(szLine)) {
			if (isAllSpaceOrTab(szLine, sizeof(*szLine)))
				continue;

			/*  Get header info (relative value or exact value) */
			if (firstLine) {
				if (eqNByte(szLine, (u8 *)("#[v1]"), 5)) {

					pHalData->odmpriv.PhyRegPgVersion = szLine[3] - '0';
					/* DBG_871X("This is a new format PHY_REG_PG.txt\n"); */
				} else if (eqNByte(szLine, (u8 *)("#[v0]"), 5)) {
					pHalData->odmpriv.PhyRegPgVersion = szLine[3] - '0';
					/* DBG_871X("This is a old format PHY_REG_PG.txt ok\n"); */
				} else {
					DBG_871X("The format in PHY_REG_PG are invalid %s\n", szLine);
					return _FAIL;
				}

				if (eqNByte(szLine + 5, (u8 *)("[Exact]#"), 8)) {
					pHalData->odmpriv.PhyRegPgValueType = PHY_REG_PG_EXACT_VALUE;
					/* DBG_871X("The values in PHY_REG_PG are exact values ok\n"); */
					firstLine = false;
					continue;
				} else if (eqNByte(szLine + 5, (u8 *)("[Relative]#"), 11)) {
					pHalData->odmpriv.PhyRegPgValueType = PHY_REG_PG_RELATIVE_VALUE;
					/* DBG_871X("The values in PHY_REG_PG are relative values ok\n"); */
					firstLine = false;
					continue;
				} else {
					DBG_871X("The values in PHY_REG_PG are invalid %s\n", szLine);
					return _FAIL;
				}
			}

			if (pHalData->odmpriv.PhyRegPgVersion == 0) {
				/*  Get 1st hex value as register offset. */
				if (GetHexValueFromString(szLine, &u4bRegOffset, &u4bMove)) {
					szLine += u4bMove;
					if (u4bRegOffset == 0xffff) /*  Ending. */
						break;

					/*  Get 2nd hex value as register mask. */
					if (GetHexValueFromString(szLine, &u4bRegMask, &u4bMove))
						szLine += u4bMove;
					else
						return _FAIL;

					if (pHalData->odmpriv.PhyRegPgValueType == PHY_REG_PG_RELATIVE_VALUE) {
						/*  Get 3rd hex value as register value. */
						if (GetHexValueFromString(szLine, &u4bRegValue, &u4bMove)) {
							PHY_StoreTxPowerByRate(Adapter, 0, 0, 1, u4bRegOffset, u4bRegMask, u4bRegValue);
							/* DBG_871X("[ADDR] %03X =%08X Mask =%08x\n", u4bRegOffset, u4bRegValue, u4bRegMask); */
						} else
							return _FAIL;
					} else if (pHalData->odmpriv.PhyRegPgValueType == PHY_REG_PG_EXACT_VALUE) {
						u32 combineValue = 0;
						u8 integer = 0, fraction = 0;

						if (GetFractionValueFromString(szLine, &integer, &fraction, &u4bMove))
							szLine += u4bMove;
						else
							return _FAIL;

						integer *= 2;
						if (fraction == 5)
							integer += 1;
						combineValue |= (((integer / 10) << 4) + (integer % 10));
						/* DBG_871X(" %d", integer); */

						if (GetFractionValueFromString(szLine, &integer, &fraction, &u4bMove))
							szLine += u4bMove;
						else
							return _FAIL;

						integer *= 2;
						if (fraction == 5)
							integer += 1;
						combineValue <<= 8;
						combineValue |= (((integer / 10) << 4) + (integer % 10));
						/* DBG_871X(" %d", integer); */

						if (GetFractionValueFromString(szLine, &integer, &fraction, &u4bMove))
							szLine += u4bMove;
						else
							return _FAIL;

						integer *= 2;
						if (fraction == 5)
							integer += 1;
						combineValue <<= 8;
						combineValue |= (((integer / 10) << 4) + (integer % 10));
						/* DBG_871X(" %d", integer); */

						if (GetFractionValueFromString(szLine, &integer, &fraction, &u4bMove))
							szLine += u4bMove;
						else
							return _FAIL;

						integer *= 2;
						if (fraction == 5)
							integer += 1;
						combineValue <<= 8;
						combineValue |= (((integer / 10) << 4) + (integer % 10));
						/* DBG_871X(" %d", integer); */
						PHY_StoreTxPowerByRate(Adapter, 0, 0, 1, u4bRegOffset, u4bRegMask, combineValue);

						/* DBG_871X("[ADDR] 0x%3x = 0x%4x\n", u4bRegOffset, combineValue); */
					}
				}
			} else if (pHalData->odmpriv.PhyRegPgVersion > 0) {
				u32 index = 0;

				if (eqNByte(szLine, "0xffff", 6))
					break;

				if (!eqNByte("#[END]#", szLine, 7)) {
					/*  load the table label info */
					if (szLine[0] == '#') {
						index = 0;
						if (eqNByte(szLine, "#[2.4G]", 7)) {
							band = BAND_ON_2_4G;
							index += 8;
						} else if (eqNByte(szLine, "#[5G]", 5)) {
							band = BAND_ON_5G;
							index += 6;
						} else {
							DBG_871X("Invalid band %s in PHY_REG_PG.txt\n", szLine);
							return _FAIL;
						}

						rf_path = szLine[index] - 'A';
						/* DBG_871X(" Table label Band %d, RfPath %d\n", band, rf_path); */
					} else { /*  load rows of tables */
						if (szLine[1] == '1')
							tx_num = RF_1TX;
						else if (szLine[1] == '2')
							tx_num = RF_2TX;
						else if (szLine[1] == '3')
							tx_num = RF_3TX;
						else if (szLine[1] == '4')
							tx_num = RF_4TX;
						else {
							DBG_871X("Invalid row in PHY_REG_PG.txt %c\n", szLine[1]);
							return _FAIL;
						}

						while (szLine[index] != ']')
							++index;
						++index;/*  skip ] */

						/*  Get 2nd hex value as register offset. */
						szLine += index;
						if (GetHexValueFromString(szLine, &u4bRegOffset, &u4bMove))
							szLine += u4bMove;
						else
							return _FAIL;

						/*  Get 2nd hex value as register mask. */
						if (GetHexValueFromString(szLine, &u4bRegMask, &u4bMove))
							szLine += u4bMove;
						else
							return _FAIL;

						if (pHalData->odmpriv.PhyRegPgValueType == PHY_REG_PG_RELATIVE_VALUE) {
							/*  Get 3rd hex value as register value. */
							if (GetHexValueFromString(szLine, &u4bRegValue, &u4bMove)) {
								PHY_StoreTxPowerByRate(Adapter, band, rf_path, tx_num, u4bRegOffset, u4bRegMask, u4bRegValue);
								/* DBG_871X("[ADDR] %03X (tx_num %d) =%08X Mask =%08x\n", u4bRegOffset, tx_num, u4bRegValue, u4bRegMask); */
							} else
								return _FAIL;
						} else if (pHalData->odmpriv.PhyRegPgValueType == PHY_REG_PG_EXACT_VALUE) {
							u32 combineValue = 0;
							u8 integer = 0, fraction = 0;

							if (GetFractionValueFromString(szLine, &integer, &fraction, &u4bMove))
								szLine += u4bMove;
							else
								return _FAIL;

							integer *= 2;
							if (fraction == 5)
								integer += 1;
							combineValue |= (((integer / 10) << 4) + (integer % 10));
							/* DBG_871X(" %d", integer); */

							if (GetFractionValueFromString(szLine, &integer, &fraction, &u4bMove))
								szLine += u4bMove;
							else
								return _FAIL;

							integer *= 2;
							if (fraction == 5)
								integer += 1;
							combineValue <<= 8;
							combineValue |= (((integer / 10) << 4) + (integer % 10));
							/* DBG_871X(" %d", integer); */

							if (GetFractionValueFromString(szLine, &integer, &fraction, &u4bMove))
								szLine += u4bMove;
							else
								return _FAIL;

							integer *= 2;
							if (fraction == 5)
								integer += 1;
							combineValue <<= 8;
							combineValue |= (((integer / 10) << 4) + (integer % 10));
							/* DBG_871X(" %d", integer); */

							if (GetFractionValueFromString(szLine, &integer, &fraction, &u4bMove))
								szLine += u4bMove;
							else
								return _FAIL;

							integer *= 2;
							if (fraction == 5)
								integer += 1;
							combineValue <<= 8;
							combineValue |= (((integer / 10) << 4) + (integer % 10));
							/* DBG_871X(" %d", integer); */
							PHY_StoreTxPowerByRate(Adapter, band, rf_path, tx_num, u4bRegOffset, u4bRegMask, combineValue);

							/* DBG_871X("[ADDR] 0x%3x (tx_num %d) = 0x%4x\n", u4bRegOffset, tx_num, combineValue); */
						}
					}
				}
			}
		}
	}
	/* DBG_871X("<=====phy_ParseBBPgParaFile()\n"); */
	return rtStatus;
}

int phy_ConfigBBWithPgParaFile(struct adapter *Adapter, char *pFileName)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);
	int	rlen = 0, rtStatus = _FAIL;

	if (!(Adapter->registrypriv.load_phy_file & LOAD_BB_PG_PARA_FILE))
		return rtStatus;

	memset(pHalData->para_file_buf, 0, MAX_PARA_FILE_BUF_LEN);

	if ((pHalData->bb_phy_reg_pg_len == 0) && (pHalData->bb_phy_reg_pg == NULL)) {
		rtw_merge_string(file_path_bs, PATH_MAX, rtw_phy_file_path, pFileName);

		if (rtw_is_file_readable(file_path_bs) == true) {
			rlen = rtw_retrive_from_file(file_path_bs, pHalData->para_file_buf, MAX_PARA_FILE_BUF_LEN);
			if (rlen > 0) {
				rtStatus = _SUCCESS;
				pHalData->bb_phy_reg_pg = vzalloc(rlen);
				if (pHalData->bb_phy_reg_pg) {
					memcpy(pHalData->bb_phy_reg_pg, pHalData->para_file_buf, rlen);
					pHalData->bb_phy_reg_pg_len = rlen;
				} else
					DBG_871X("%s bb_phy_reg_pg alloc fail !\n", __func__);
			}
		}
	} else {
		if ((pHalData->bb_phy_reg_pg_len != 0) && (pHalData->bb_phy_reg_pg != NULL)) {
			memcpy(pHalData->para_file_buf, pHalData->bb_phy_reg_pg, pHalData->bb_phy_reg_pg_len);
			rtStatus = _SUCCESS;
		} else
			DBG_871X("%s(): Critical Error !!!\n", __func__);
	}

	if (rtStatus == _SUCCESS) {
		/* DBG_871X("phy_ConfigBBWithPgParaFile(): read %s ok\n", pFileName); */
		phy_ParseBBPgParaFile(Adapter, pHalData->para_file_buf);
	} else
		DBG_871X("%s(): No File %s, Load from HWImg Array!\n", __func__, pFileName);

	return rtStatus;
}

int PHY_ConfigRFWithParaFile(
	struct adapter *Adapter, char *pFileName, u8 eRFPath
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);
	int	rlen = 0, rtStatus = _FAIL;
	char *szLine, *ptmp;
	u32 u4bRegOffset, u4bRegValue, u4bMove;
	u16 i;
	char *pBuf = NULL;
	u32 *pBufLen = NULL;

	if (!(Adapter->registrypriv.load_phy_file & LOAD_RF_PARA_FILE))
		return rtStatus;

	switch (eRFPath) {
	case ODM_RF_PATH_A:
		pBuf = pHalData->rf_radio_a;
		pBufLen = &pHalData->rf_radio_a_len;
		break;
	case ODM_RF_PATH_B:
		pBuf = pHalData->rf_radio_b;
		pBufLen = &pHalData->rf_radio_b_len;
		break;
	default:
		DBG_871X("Unknown RF path!! %d\r\n", eRFPath);
		break;
	}

	memset(pHalData->para_file_buf, 0, MAX_PARA_FILE_BUF_LEN);

	if ((pBufLen != NULL) && (*pBufLen == 0) && (pBuf == NULL)) {
		rtw_merge_string(file_path_bs, PATH_MAX, rtw_phy_file_path, pFileName);

		if (rtw_is_file_readable(file_path_bs) == true) {
			rlen = rtw_retrive_from_file(file_path_bs, pHalData->para_file_buf, MAX_PARA_FILE_BUF_LEN);
			if (rlen > 0) {
				rtStatus = _SUCCESS;
				pBuf = vzalloc(rlen);
				if (pBuf) {
					memcpy(pBuf, pHalData->para_file_buf, rlen);
					*pBufLen = rlen;

					switch (eRFPath) {
					case ODM_RF_PATH_A:
						pHalData->rf_radio_a = pBuf;
						break;
					case ODM_RF_PATH_B:
						pHalData->rf_radio_b = pBuf;
						break;
					}
				} else
					DBG_871X("%s(): eRFPath =%d  alloc fail !\n", __func__, eRFPath);
			}
		}
	} else {
		if ((pBufLen != NULL) && (*pBufLen == 0) && (pBuf == NULL)) {
			memcpy(pHalData->para_file_buf, pBuf, *pBufLen);
			rtStatus = _SUCCESS;
		} else
			DBG_871X("%s(): Critical Error !!!\n", __func__);
	}

	if (rtStatus == _SUCCESS) {
		/* DBG_871X("%s(): read %s successfully\n", __func__, pFileName); */

		ptmp = pHalData->para_file_buf;
		for (szLine = GetLineFromBuffer(ptmp); szLine != NULL; szLine = GetLineFromBuffer(ptmp)) {
			if (!IsCommentString(szLine)) {
				/*  Get 1st hex value as register offset. */
				if (GetHexValueFromString(szLine, &u4bRegOffset, &u4bMove)) {
					if (u4bRegOffset == 0xfe || u4bRegOffset == 0xffe) /*  Deay specific ms. Only RF configuration require delay. */
						msleep(50);
					else if (u4bRegOffset == 0xfd) {
						/* mdelay(5); */
						for (i = 0; i < 100; i++)
							udelay(MAX_STALL_TIME);
					} else if (u4bRegOffset == 0xfc) {
						/* mdelay(1); */
						for (i = 0; i < 20; i++)
							udelay(MAX_STALL_TIME);
					} else if (u4bRegOffset == 0xfb)
						udelay(50);
					else if (u4bRegOffset == 0xfa)
						udelay(5);
					else if (u4bRegOffset == 0xf9)
						udelay(1);
					else if (u4bRegOffset == 0xffff)
						break;

					/*  Get 2nd hex value as register value. */
					szLine += u4bMove;
					if (GetHexValueFromString(szLine, &u4bRegValue, &u4bMove)) {
						PHY_SetRFReg(Adapter, eRFPath, u4bRegOffset, bRFRegOffsetMask, u4bRegValue);

						/*  Temp add, for frequency lock, if no delay, that may cause */
						/*  frequency shift, ex: 2412MHz => 2417MHz */
						/*  If frequency shift, the following action may works. */
						/*  Fractional-N table in radio_a.txt */
						/* 0x2a 0x00001		channel 1 */
						/* 0x2b 0x00808		frequency divider. */
						/* 0x2b 0x53333 */
						/* 0x2c 0x0000c */
						udelay(1);
					}
				}
			}
		}
	} else
		DBG_871X("%s(): No File %s, Load from HWImg Array!\n", __func__, pFileName);

	return rtStatus;
}

static void initDeltaSwingIndexTables(
	struct adapter *Adapter,
	char *Band,
	char *Path,
	char *Sign,
	char *Channel,
	char *Rate,
	char *Data
)
{
	#define STR_EQUAL_5G(_band, _path, _sign, _rate, _chnl) \
		((strcmp(Band, _band) == 0) && (strcmp(Path, _path) == 0) && (strcmp(Sign, _sign) == 0) &&\
		(strcmp(Rate, _rate) == 0) && (strcmp(Channel, _chnl) == 0)\
	)
	#define STR_EQUAL_2G(_band, _path, _sign, _rate) \
		((strcmp(Band, _band) == 0) && (strcmp(Path, _path) == 0) && (strcmp(Sign, _sign) == 0) &&\
		(strcmp(Rate, _rate) == 0)\
	)

	#define STORE_SWING_TABLE(_array, _iteratedIdx) \
		for (token = strsep(&Data, delim); token != NULL; token = strsep(&Data, delim)) {\
			sscanf(token, "%d", &idx);\
			_array[_iteratedIdx++] = (u8)idx;\
		} \

	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T pDM_Odm = &pHalData->odmpriv;
	PODM_RF_CAL_T pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);
	u32 j = 0;
	char *token;
	char delim[] = ",";
	u32 idx = 0;

	/* DBG_871X("===>initDeltaSwingIndexTables(): Band: %s;\nPath: %s;\nSign: %s;\nChannel: %s;\nRate: %s;\n, Data: %s;\n", */
	/*	Band, Path, Sign, Channel, Rate, Data); */

	if (STR_EQUAL_2G("2G", "A", "+", "CCK")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_P, j);
	} else if (STR_EQUAL_2G("2G", "A", "-", "CCK")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_N, j);
	} else if (STR_EQUAL_2G("2G", "B", "+", "CCK")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_P, j);
	} else if (STR_EQUAL_2G("2G", "B", "-", "CCK")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_N, j);
	} else if (STR_EQUAL_2G("2G", "A", "+", "ALL")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_2GA_P, j);
	} else if (STR_EQUAL_2G("2G", "A", "-", "ALL")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_2GA_N, j);
	} else if (STR_EQUAL_2G("2G", "B", "+", "ALL")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_2GB_P, j);
	} else if (STR_EQUAL_2G("2G", "B", "-", "ALL")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_2GB_N, j);
	} else if (STR_EQUAL_5G("5G", "A", "+", "ALL", "0")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_5GA_P[0], j);
	} else if (STR_EQUAL_5G("5G", "A", "-", "ALL", "0")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_5GA_N[0], j);
	} else if (STR_EQUAL_5G("5G", "B", "+", "ALL", "0")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_5GB_P[0], j);
	} else if (STR_EQUAL_5G("5G", "B", "-", "ALL", "0")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_5GB_N[0], j);
	} else if (STR_EQUAL_5G("5G", "A", "+", "ALL", "1")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_5GA_P[1], j);
	} else if (STR_EQUAL_5G("5G", "A", "-", "ALL", "1")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_5GA_N[1], j);
	} else if (STR_EQUAL_5G("5G", "B", "+", "ALL", "1")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_5GB_P[1], j);
	} else if (STR_EQUAL_5G("5G", "B", "-", "ALL", "1")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_5GB_N[1], j);
	} else if (STR_EQUAL_5G("5G", "A", "+", "ALL", "2")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_5GA_P[2], j);
	} else if (STR_EQUAL_5G("5G", "A", "-", "ALL", "2")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_5GA_N[2], j);
	} else if (STR_EQUAL_5G("5G", "B", "+", "ALL", "2")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_5GB_P[2], j);
	} else if (STR_EQUAL_5G("5G", "B", "-", "ALL", "2")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_5GB_N[2], j);
	} else if (STR_EQUAL_5G("5G", "A", "+", "ALL", "3")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_5GA_P[3], j);
	} else if (STR_EQUAL_5G("5G", "A", "-", "ALL", "3")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_5GA_N[3], j);
	} else if (STR_EQUAL_5G("5G", "B", "+", "ALL", "3")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_5GB_P[3], j);
	} else if (STR_EQUAL_5G("5G", "B", "-", "ALL", "3")) {
		STORE_SWING_TABLE(pRFCalibrateInfo->DeltaSwingTableIdx_5GB_N[3], j);
	} else
		DBG_871X("===>initDeltaSwingIndexTables(): The input is invalid!!\n");
}

int PHY_ConfigRFWithTxPwrTrackParaFile(struct adapter *Adapter, char *pFileName)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);
	int	rlen = 0, rtStatus = _FAIL;
	char *szLine, *ptmp;
	u32 i = 0;

	if (!(Adapter->registrypriv.load_phy_file & LOAD_RF_TXPWR_TRACK_PARA_FILE))
		return rtStatus;

	memset(pHalData->para_file_buf, 0, MAX_PARA_FILE_BUF_LEN);

	if ((pHalData->rf_tx_pwr_track_len == 0) && (pHalData->rf_tx_pwr_track == NULL)) {
		rtw_merge_string(file_path_bs, PATH_MAX, rtw_phy_file_path, pFileName);

		if (rtw_is_file_readable(file_path_bs) == true) {
			rlen = rtw_retrive_from_file(file_path_bs, pHalData->para_file_buf, MAX_PARA_FILE_BUF_LEN);
			if (rlen > 0) {
				rtStatus = _SUCCESS;
				pHalData->rf_tx_pwr_track = vzalloc(rlen);
				if (pHalData->rf_tx_pwr_track) {
					memcpy(pHalData->rf_tx_pwr_track, pHalData->para_file_buf, rlen);
					pHalData->rf_tx_pwr_track_len = rlen;
				} else
					DBG_871X("%s rf_tx_pwr_track alloc fail !\n", __func__);
			}
		}
	} else {
		if ((pHalData->rf_tx_pwr_track_len != 0) && (pHalData->rf_tx_pwr_track != NULL)) {
			memcpy(pHalData->para_file_buf, pHalData->rf_tx_pwr_track, pHalData->rf_tx_pwr_track_len);
			rtStatus = _SUCCESS;
		} else
			DBG_871X("%s(): Critical Error !!!\n", __func__);
	}

	if (rtStatus == _SUCCESS) {
		/* DBG_871X("%s(): read %s successfully\n", __func__, pFileName); */

		ptmp = pHalData->para_file_buf;
		for (szLine = GetLineFromBuffer(ptmp); szLine != NULL; szLine = GetLineFromBuffer(ptmp)) {
			if (!IsCommentString(szLine)) {
				char band[5] = "", path[5] = "", sign[5] = "";
				char chnl[5] = "", rate[10] = "";
				char data[300] = ""; /*  100 is too small */

				if (strlen(szLine) < 10 || szLine[0] != '[')
					continue;

				strncpy(band, szLine+1, 2);
				strncpy(path, szLine+5, 1);
				strncpy(sign, szLine+8, 1);

				i = 10; /*  szLine+10 */
				if (!ParseQualifiedString(szLine, &i, rate, '[', ']')) {
					/* DBG_871X("Fail to parse rate!\n"); */
				}
				if (!ParseQualifiedString(szLine, &i, chnl, '[', ']')) {
					/* DBG_871X("Fail to parse channel group!\n"); */
				}
				while (szLine[i] != '{' && i < strlen(szLine))
					i++;
				if (!ParseQualifiedString(szLine, &i, data, '{', '}')) {
					/* DBG_871X("Fail to parse data!\n"); */
				}

				initDeltaSwingIndexTables(Adapter, band, path, sign, chnl, rate, data);
			}
		}
	} else
		DBG_871X("%s(): No File %s, Load from HWImg Array!\n", __func__, pFileName);

	return rtStatus;
}

static int phy_ParsePowerLimitTableFile(struct adapter *Adapter, char *buffer)
{
	u32 i = 0, forCnt = 0;
	u8 loadingStage = 0, limitValue = 0, fraction = 0;
	char *szLine, *ptmp;
	int	rtStatus = _SUCCESS;
	char band[10], bandwidth[10], rateSection[10],
		regulation[TXPWR_LMT_MAX_REGULATION_NUM][10], rfPath[10], colNumBuf[10];
	u8 colNum = 0;

	DBG_871X("===>phy_ParsePowerLimitTableFile()\n");

	if (Adapter->registrypriv.RegDecryptCustomFile == 1)
		phy_DecryptBBPgParaFile(Adapter, buffer);

	ptmp = buffer;
	for (szLine = GetLineFromBuffer(ptmp); szLine != NULL; szLine = GetLineFromBuffer(ptmp)) {
		/*  skip comment */
		if (IsCommentString(szLine)) {
			continue;
		}

		if (loadingStage == 0) {
			for (forCnt = 0; forCnt < TXPWR_LMT_MAX_REGULATION_NUM; ++forCnt)
				memset((void *) regulation[forCnt], 0, 10);

			memset((void *) band, 0, 10);
			memset((void *) bandwidth, 0, 10);
			memset((void *) rateSection, 0, 10);
			memset((void *) rfPath, 0, 10);
			memset((void *) colNumBuf, 0, 10);

			if (szLine[0] != '#' || szLine[1] != '#')
				continue;

			/*  skip the space */
			i = 2;
			while (szLine[i] == ' ' || szLine[i] == '\t')
				++i;

			szLine[--i] = ' '; /*  return the space in front of the regulation info */

			/*  Parse the label of the table */
			if (!ParseQualifiedString(szLine, &i, band, ' ', ',')) {
				DBG_871X("Fail to parse band!\n");
				return _FAIL;
			}
			if (!ParseQualifiedString(szLine, &i, bandwidth, ' ', ',')) {
				DBG_871X("Fail to parse bandwidth!\n");
				return _FAIL;
			}
			if (!ParseQualifiedString(szLine, &i, rfPath, ' ', ',')) {
				DBG_871X("Fail to parse rf path!\n");
				return _FAIL;
			}
			if (!ParseQualifiedString(szLine, &i, rateSection, ' ', ',')) {
				DBG_871X("Fail to parse rate!\n");
				return _FAIL;
			}

			loadingStage = 1;
		} else if (loadingStage == 1) {
			if (szLine[0] != '#' || szLine[1] != '#')
				continue;

			/*  skip the space */
			i = 2;
			while (szLine[i] == ' ' || szLine[i] == '\t')
				++i;

			if (!eqNByte((u8 *)(szLine + i), (u8 *)("START"), 5)) {
				DBG_871X("Lost \"##   START\" label\n");
				return _FAIL;
			}

			loadingStage = 2;
		} else if (loadingStage == 2) {
			if (szLine[0] != '#' || szLine[1] != '#')
				continue;

			/*  skip the space */
			i = 2;
			while (szLine[i] == ' ' || szLine[i] == '\t')
				++i;

			if (!ParseQualifiedString(szLine, &i, colNumBuf, '#', '#')) {
				DBG_871X("Fail to parse column number!\n");
				return _FAIL;
			}

			if (!GetU1ByteIntegerFromStringInDecimal(colNumBuf, &colNum))
				return _FAIL;

			if (colNum > TXPWR_LMT_MAX_REGULATION_NUM) {
				DBG_871X(
					"unvalid col number %d (greater than max %d)\n",
					colNum, TXPWR_LMT_MAX_REGULATION_NUM
				);
				return _FAIL;
			}

			for (forCnt = 0; forCnt < colNum; ++forCnt) {
				u8 regulation_name_cnt = 0;

				/*  skip the space */
				while (szLine[i] == ' ' || szLine[i] == '\t')
					++i;

				while (szLine[i] != ' ' && szLine[i] != '\t' && szLine[i] != '\0')
					regulation[forCnt][regulation_name_cnt++] = szLine[i++];
				/* DBG_871X("regulation %s!\n", regulation[forCnt]); */

				if (regulation_name_cnt == 0) {
					DBG_871X("unvalid number of regulation!\n");
					return _FAIL;
				}
			}

			loadingStage = 3;
		} else if (loadingStage == 3) {
			char channel[10] = {0}, powerLimit[10] = {0};
			u8 cnt = 0;

			/*  the table ends */
			if (szLine[0] == '#' && szLine[1] == '#') {
				i = 2;
				while (szLine[i] == ' ' || szLine[i] == '\t')
					++i;

				if (eqNByte((u8 *)(szLine + i), (u8 *)("END"), 3)) {
					loadingStage = 0;
					continue;
				} else {
					DBG_871X("Wrong format\n");
					DBG_871X("<===== phy_ParsePowerLimitTableFile()\n");
					return _FAIL;
				}
			}

			if ((szLine[0] != 'c' && szLine[0] != 'C') ||
				 (szLine[1] != 'h' && szLine[1] != 'H')) {
				DBG_871X("Meet wrong channel => power limt pair\n");
				continue;
			}
			i = 2;/*  move to the  location behind 'h' */

			/*  load the channel number */
			cnt = 0;
			while (szLine[i] >= '0' && szLine[i] <= '9') {
				channel[cnt] = szLine[i];
				++cnt;
				++i;
			}
			/* DBG_871X("chnl %s!\n", channel); */

			for (forCnt = 0; forCnt < colNum; ++forCnt) {
				/*  skip the space between channel number and the power limit value */
				while (szLine[i] == ' ' || szLine[i] == '\t')
					++i;

				/*  load the power limit value */
				cnt = 0;
				fraction = 0;
				memset((void *) powerLimit, 0, 10);
				while ((szLine[i] >= '0' && szLine[i] <= '9') || szLine[i] == '.') {
					if (szLine[i] == '.') {
						if ((szLine[i+1] >= '0' && szLine[i+1] <= '9')) {
							fraction = szLine[i+1];
							i += 2;
						} else {
							DBG_871X("Wrong fraction in TXPWR_LMT.txt\n");
							return _FAIL;
						}

						break;
					}

					powerLimit[cnt] = szLine[i];
					++cnt;
					++i;
				}

				if (powerLimit[0] == '\0') {
					powerLimit[0] = '6';
					powerLimit[1] = '3';
					i += 2;
				} else {
					if (!GetU1ByteIntegerFromStringInDecimal(powerLimit, &limitValue))
						return _FAIL;

					limitValue *= 2;
					cnt = 0;
					if (fraction == '5')
						++limitValue;

					/*  the value is greater or equal to 100 */
					if (limitValue >= 100) {
						powerLimit[cnt++] = limitValue/100 + '0';
						limitValue %= 100;

						if (limitValue >= 10) {
							powerLimit[cnt++] = limitValue/10 + '0';
							limitValue %= 10;
						} else
							powerLimit[cnt++] = '0';

						powerLimit[cnt++] = limitValue + '0';
					} else if (limitValue >= 10) { /*  the value is greater or equal to 10 */
						powerLimit[cnt++] = limitValue/10 + '0';
						limitValue %= 10;
						powerLimit[cnt++] = limitValue + '0';
					}
					/*  the value is less than 10 */
					else
						powerLimit[cnt++] = limitValue + '0';

					powerLimit[cnt] = '\0';
				}

				/* DBG_871X("ch%s => %s\n", channel, powerLimit); */

				/*  store the power limit value */
				PHY_SetTxPowerLimit(Adapter, (u8 *)regulation[forCnt], (u8 *)band,
					(u8 *)bandwidth, (u8 *)rateSection, (u8 *)rfPath, (u8 *)channel, (u8 *)powerLimit);

			}
		} else {
			DBG_871X("Abnormal loading stage in phy_ParsePowerLimitTableFile()!\n");
			rtStatus = _FAIL;
			break;
		}
	}

	DBG_871X("<===phy_ParsePowerLimitTableFile()\n");
	return rtStatus;
}

int PHY_ConfigRFWithPowerLimitTableParaFile(
	struct adapter *Adapter, char *pFileName
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);
	int	rlen = 0, rtStatus = _FAIL;

	if (!(Adapter->registrypriv.load_phy_file & LOAD_RF_TXPWR_LMT_PARA_FILE))
		return rtStatus;

	memset(pHalData->para_file_buf, 0, MAX_PARA_FILE_BUF_LEN);

	if ((pHalData->rf_tx_pwr_lmt_len == 0) && (pHalData->rf_tx_pwr_lmt == NULL)) {
		rtw_merge_string(file_path_bs, PATH_MAX, rtw_phy_file_path, pFileName);

		if (rtw_is_file_readable(file_path_bs) == true) {
			rlen = rtw_retrive_from_file(file_path_bs, pHalData->para_file_buf, MAX_PARA_FILE_BUF_LEN);
			if (rlen > 0) {
				rtStatus = _SUCCESS;
				pHalData->rf_tx_pwr_lmt = vzalloc(rlen);
				if (pHalData->rf_tx_pwr_lmt) {
					memcpy(pHalData->rf_tx_pwr_lmt, pHalData->para_file_buf, rlen);
					pHalData->rf_tx_pwr_lmt_len = rlen;
				} else
					DBG_871X("%s rf_tx_pwr_lmt alloc fail !\n", __func__);
			}
		}
	} else {
		if ((pHalData->rf_tx_pwr_lmt_len != 0) && (pHalData->rf_tx_pwr_lmt != NULL)) {
			memcpy(pHalData->para_file_buf, pHalData->rf_tx_pwr_lmt, pHalData->rf_tx_pwr_lmt_len);
			rtStatus = _SUCCESS;
		} else
			DBG_871X("%s(): Critical Error !!!\n", __func__);
	}

	if (rtStatus == _SUCCESS) {
		/* DBG_871X("%s(): read %s ok\n", __func__, pFileName); */
		rtStatus = phy_ParsePowerLimitTableFile(Adapter, pHalData->para_file_buf);
	} else
		DBG_871X("%s(): No File %s, Load from HWImg Array!\n", __func__, pFileName);

	return rtStatus;
}

void phy_free_filebuf(struct adapter *padapter)
{
	struct hal_com_data		*pHalData = GET_HAL_DATA(padapter);

	if (pHalData->mac_reg)
		vfree(pHalData->mac_reg);
	if (pHalData->bb_phy_reg)
		vfree(pHalData->bb_phy_reg);
	if (pHalData->bb_agc_tab)
		vfree(pHalData->bb_agc_tab);
	if (pHalData->bb_phy_reg_pg)
		vfree(pHalData->bb_phy_reg_pg);
	if (pHalData->bb_phy_reg_mp)
		vfree(pHalData->bb_phy_reg_mp);
	if (pHalData->rf_radio_a)
		vfree(pHalData->rf_radio_a);
	if (pHalData->rf_radio_b)
		vfree(pHalData->rf_radio_b);
	if (pHalData->rf_tx_pwr_track)
		vfree(pHalData->rf_tx_pwr_track);
	if (pHalData->rf_tx_pwr_lmt)
		vfree(pHalData->rf_tx_pwr_lmt);

}

