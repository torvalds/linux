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

u8 PHY_GetTxPowerByRateBase(struct adapter *Adapter, u8 RfPath,
			    u8 TxNum, enum rate_section RateSection)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);
	u8	value = 0;

	if (RfPath > ODM_RF_PATH_D)
		return 0;

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
	default:
		break;
	}

	return value;
}

static void
phy_SetTxPowerByRateBase(
	struct adapter *Adapter,
	u8 RfPath,
	enum rate_section	RateSection,
	u8 TxNum,
	u8 Value
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);

	if (RfPath > ODM_RF_PATH_D)
		return;

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
	default:
		break;
	}
}

static void
phy_StoreTxPowerByRateBase(
struct adapter *padapter
	)
{
	u8 path, base;

	for (path = ODM_RF_PATH_A; path <= ODM_RF_PATH_B; ++path) {
		base = PHY_GetTxPowerByRate(padapter, path, RF_1TX, MGN_11M);
		phy_SetTxPowerByRateBase(padapter, path, CCK, RF_1TX, base);

		base = PHY_GetTxPowerByRate(padapter, path, RF_1TX, MGN_54M);
		phy_SetTxPowerByRateBase(padapter, path, OFDM, RF_1TX, base);

		base = PHY_GetTxPowerByRate(padapter, path, RF_1TX, MGN_MCS7);
		phy_SetTxPowerByRateBase(padapter, path, HT_MCS0_MCS7, RF_1TX, base);

		base = PHY_GetTxPowerByRate(padapter, path, RF_2TX, MGN_MCS15);
		phy_SetTxPowerByRateBase(padapter, path, HT_MCS8_MCS15, RF_2TX, base);

		base = PHY_GetTxPowerByRate(padapter, path, RF_3TX, MGN_MCS23);
		phy_SetTxPowerByRateBase(padapter, path, HT_MCS16_MCS23, RF_3TX, base);

	}
}

u8 PHY_GetRateSectionIndexOfTxPowerByRate(
	struct adapter *padapter, u32 RegAddr, u32 BitMask
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	struct dm_odm_t *pDM_Odm = &pHalData->odmpriv;
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

	default:
		break;
	}
}

static void PHY_StoreTxPowerByRateNew(
	struct adapter *padapter,
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

	if (RfPath > ODM_RF_PATH_D)
		return;

	if (TxNum > ODM_RF_PATH_D)
		return;

	for (i = 0; i < rateNum; ++i) {
		pHalData->TxPwrByRateOffset[RfPath][TxNum][rateIndex[i]] = PwrByRateVal[i];
	}
}

static void PHY_StoreTxPowerByRateOld(
	struct adapter *padapter, u32	RegAddr, u32 BitMask, u32 Data
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	u8	index = PHY_GetRateSectionIndexOfTxPowerByRate(padapter, RegAddr, BitMask);

	pHalData->MCSTxPowerLevelOriginalOffset[pHalData->pwrGroupCnt][index] = Data;
}

void PHY_InitTxPowerByRate(struct adapter *padapter)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	u8 rfPath, TxNum, rate;

	for (rfPath = 0; rfPath < TX_PWR_BY_RATE_NUM_RF; ++rfPath)
		for (TxNum = 0; TxNum < TX_PWR_BY_RATE_NUM_RF; ++TxNum)
			for (rate = 0; rate < TX_PWR_BY_RATE_NUM_RATE; ++rate)
				pHalData->TxPwrByRateOffset[rfPath][TxNum][rate] = 0;
}

void PHY_StoreTxPowerByRate(
	struct adapter *padapter,
	u32	RfPath,
	u32	TxNum,
	u32	RegAddr,
	u32	BitMask,
	u32	Data
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	struct dm_odm_t *pDM_Odm = &pHalData->odmpriv;

	if (pDM_Odm->PhyRegPgVersion > 0)
		PHY_StoreTxPowerByRateNew(padapter, RfPath, TxNum, RegAddr, BitMask, Data);
	else if (pDM_Odm->PhyRegPgVersion == 0) {
		PHY_StoreTxPowerByRateOld(padapter, RegAddr, BitMask, Data);

		if (RegAddr == rTxAGC_A_Mcs15_Mcs12 && pHalData->rf_type == RF_1T1R)
			pHalData->pwrGroupCnt++;
		else if (RegAddr == rTxAGC_B_Mcs15_Mcs12 && pHalData->rf_type != RF_1T1R)
			pHalData->pwrGroupCnt++;
	}
}

static void
phy_ConvertTxPowerByRateInDbmToRelativeValues(
struct adapter *padapter
	)
{
	u8	base = 0, i = 0, value = 0, path = 0, txNum = 0;
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

	for (path = ODM_RF_PATH_A; path <= ODM_RF_PATH_D; ++path) {
		for (txNum = RF_1TX; txNum < RF_MAX_TX_NUM; ++txNum) {
			/*  CCK */
			base = PHY_GetTxPowerByRate(padapter, path, txNum, MGN_11M);
			for (i = 0; i < ARRAY_SIZE(cckRates); ++i) {
				value = PHY_GetTxPowerByRate(padapter, path, txNum, cckRates[i]);
				PHY_SetTxPowerByRate(padapter, path, txNum, cckRates[i], value - base);
			}

			/*  OFDM */
			base = PHY_GetTxPowerByRate(padapter, path, txNum, MGN_54M);
			for (i = 0; i < sizeof(ofdmRates); ++i) {
				value = PHY_GetTxPowerByRate(padapter, path, txNum, ofdmRates[i]);
				PHY_SetTxPowerByRate(padapter, path, txNum, ofdmRates[i], value - base);
			}

			/*  HT MCS0~7 */
			base = PHY_GetTxPowerByRate(padapter, path, txNum, MGN_MCS7);
			for (i = 0; i < sizeof(mcs0_7Rates); ++i) {
				value = PHY_GetTxPowerByRate(padapter, path, txNum, mcs0_7Rates[i]);
				PHY_SetTxPowerByRate(padapter, path, txNum, mcs0_7Rates[i], value - base);
			}

			/*  HT MCS8~15 */
			base = PHY_GetTxPowerByRate(padapter, path, txNum, MGN_MCS15);
			for (i = 0; i < sizeof(mcs8_15Rates); ++i) {
				value = PHY_GetTxPowerByRate(padapter, path, txNum, mcs8_15Rates[i]);
				PHY_SetTxPowerByRate(padapter, path, txNum, mcs8_15Rates[i], value - base);
			}

			/*  HT MCS16~23 */
			base = PHY_GetTxPowerByRate(padapter, path, txNum, MGN_MCS23);
			for (i = 0; i < sizeof(mcs16_23Rates); ++i) {
				value = PHY_GetTxPowerByRate(padapter, path, txNum, mcs16_23Rates[i]);
				PHY_SetTxPowerByRate(padapter, path, txNum, mcs16_23Rates[i], value - base);
			}
		}
	}
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

	}
}

u8 PHY_GetTxPowerIndexBase(
	struct adapter *padapter,
	u8 RFPath,
	u8 Rate,
	enum channel_width	BandWidth,
	u8 Channel
)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	u8 txPower = 0;
	u8 chnlIdx = (Channel-1);

	if (HAL_IsLegalChannel(padapter, Channel) == false)
		chnlIdx = 0;

	if (IS_CCK_RATE(Rate))
		txPower = pHalData->Index24G_CCK_Base[RFPath][chnlIdx];
	else if (MGN_6M <= Rate)
		txPower = pHalData->Index24G_BW40_Base[RFPath][chnlIdx];

	/*  OFDM-1T */
	if ((MGN_6M <= Rate && Rate <= MGN_54M) && !IS_CCK_RATE(Rate))
		txPower += pHalData->OFDM_24G_Diff[RFPath][TX_1S];

	if (BandWidth == CHANNEL_WIDTH_20) { /*  BW20-1S, BW20-2S */
		if (MGN_MCS0 <= Rate && Rate <= MGN_MCS31)
			txPower += pHalData->BW20_24G_Diff[RFPath][TX_1S];
		if (MGN_MCS8 <= Rate && Rate <= MGN_MCS31)
			txPower += pHalData->BW20_24G_Diff[RFPath][TX_2S];
		if (MGN_MCS16 <= Rate && Rate <= MGN_MCS31)
			txPower += pHalData->BW20_24G_Diff[RFPath][TX_3S];
		if (MGN_MCS24 <= Rate && Rate <= MGN_MCS31)
			txPower += pHalData->BW20_24G_Diff[RFPath][TX_4S];

	} else if (BandWidth == CHANNEL_WIDTH_40) { /*  BW40-1S, BW40-2S */
		if (MGN_MCS0 <= Rate && Rate <= MGN_MCS31)
			txPower += pHalData->BW40_24G_Diff[RFPath][TX_1S];
		if (MGN_MCS8 <= Rate && Rate <= MGN_MCS31)
			txPower += pHalData->BW40_24G_Diff[RFPath][TX_2S];
		if (MGN_MCS16 <= Rate && Rate <= MGN_MCS31)
			txPower += pHalData->BW40_24G_Diff[RFPath][TX_3S];
		if (MGN_MCS24 <= Rate && Rate <= MGN_MCS31)
			txPower += pHalData->BW40_24G_Diff[RFPath][TX_4S];

	}

	return txPower;
}

s8 PHY_GetTxPowerTrackingOffset(struct adapter *padapter, u8 RFPath, u8 Rate)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	struct dm_odm_t *pDM_Odm = &pHalData->odmpriv;
	s8 offset = 0;

	if (pDM_Odm->RFCalibrateInfo.TxPowerTrackControl  == false)
		return offset;

	if ((Rate == MGN_1M) || (Rate == MGN_2M) || (Rate == MGN_5_5M) || (Rate == MGN_11M))
		offset = pDM_Odm->Remnant_CCKSwingIdx;
	else
		offset = pDM_Odm->Remnant_OFDMSwingIdx[RFPath];

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
	default:
		break;
	}
	return index;
}

s8 PHY_GetTxPowerByRate(
	struct adapter *padapter, u8 RFPath, u8 TxNum, u8 Rate
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	s8 value = 0;
	u8 rateIndex = PHY_GetRateIndexOfTxPowerByRate(Rate);

	if ((padapter->registrypriv.RegEnableTxPowerByRate == 2 && pHalData->EEPROMRegulatory == 2) ||
		   padapter->registrypriv.RegEnableTxPowerByRate == 0)
		return 0;

	if (RFPath > ODM_RF_PATH_D)
		return value;

	if (TxNum >= RF_MAX_TX_NUM)
		return value;

	if (rateIndex >= TX_PWR_BY_RATE_NUM_RATE)
		return value;

	return pHalData->TxPwrByRateOffset[RFPath][TxNum][rateIndex];

}

void PHY_SetTxPowerByRate(
	struct adapter *padapter,
	u8 RFPath,
	u8 TxNum,
	u8 Rate,
	s8 Value
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	u8 rateIndex = PHY_GetRateIndexOfTxPowerByRate(Rate);

	if (RFPath > ODM_RF_PATH_D)
		return;

	if (TxNum >= RF_MAX_TX_NUM)
		return;

	if (rateIndex >= TX_PWR_BY_RATE_NUM_RATE)
		return;

	pHalData->TxPwrByRateOffset[RFPath][TxNum][rateIndex] = Value;
}

void PHY_SetTxPowerLevelByPath(struct adapter *Adapter, u8 channel, u8 path)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);

	/* if (pMgntInfo->RegNByteAccess == 0) */
	{
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
	enum channel_width BandWidth,
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

static s8 phy_GetChannelIndexOfTxPowerLimit(u8 Channel)
{
	return Channel - 1;
}

static s16 get_bandwidth_idx(const enum channel_width bandwidth)
{
	switch (bandwidth) {
	case CHANNEL_WIDTH_20:
		return 0;
	case CHANNEL_WIDTH_40:
		return 1;
	default:
		return -1;
	}
}

static s16 get_rate_sctn_idx(const u8 rate)
{
	switch (rate) {
	case MGN_1M: case MGN_2M: case MGN_5_5M: case MGN_11M:
		return 0;
	case MGN_6M: case MGN_9M: case MGN_12M: case MGN_18M:
	case MGN_24M: case MGN_36M: case MGN_48M: case MGN_54M:
		return 1;
	case MGN_MCS0: case MGN_MCS1: case MGN_MCS2: case MGN_MCS3:
	case MGN_MCS4: case MGN_MCS5: case MGN_MCS6: case MGN_MCS7:
		return 2;
	case MGN_MCS8: case MGN_MCS9: case MGN_MCS10: case MGN_MCS11:
	case MGN_MCS12: case MGN_MCS13: case MGN_MCS14: case MGN_MCS15:
		return 3;
	case MGN_MCS16: case MGN_MCS17: case MGN_MCS18: case MGN_MCS19:
	case MGN_MCS20: case MGN_MCS21: case MGN_MCS22: case MGN_MCS23:
		return 4;
	case MGN_MCS24: case MGN_MCS25: case MGN_MCS26: case MGN_MCS27:
	case MGN_MCS28: case MGN_MCS29: case MGN_MCS30: case MGN_MCS31:
		return 5;
	default:
		return -1;
	}
}

s8 phy_get_tx_pwr_lmt(struct adapter *adapter, u32 reg_pwr_tbl_sel,
		      enum channel_width bandwidth,
		      u8 rf_path, u8 data_rate, u8 channel)
{
	s16 idx_regulation = -1;
	s16 idx_bandwidth  = -1;
	s16 idx_rate_sctn  = -1;
	s16 idx_channel    = -1;
	s8 pwr_lmt = MAX_POWER_INDEX;
	struct hal_com_data *hal_data = GET_HAL_DATA(adapter);
	s8 limits[10] = {0}; u8 i = 0;

	if (((adapter->registrypriv.RegEnableTxPowerLimit == 2) &&
	     (hal_data->EEPROMRegulatory != 1)) ||
	    (adapter->registrypriv.RegEnableTxPowerLimit == 0))
		return MAX_POWER_INDEX;

	switch (adapter->registrypriv.RegPwrTblSel) {
	case 1:
		idx_regulation = TXPWR_LMT_ETSI;
		break;
	case 2:
		idx_regulation = TXPWR_LMT_MKK;
		break;
	case 3:
		idx_regulation = TXPWR_LMT_FCC;
		break;
	case 4:
		idx_regulation = TXPWR_LMT_WW;
		break;
	default:
		idx_regulation = hal_data->Regulation2_4G;
		break;
	}

	idx_bandwidth = get_bandwidth_idx(bandwidth);
	idx_rate_sctn = get_rate_sctn_idx(data_rate);

	/*  workaround for wrong index combination to obtain tx power limit, */
	/*  OFDM only exists in BW 20M */
	/*  CCK table will only be given in BW 20M */
	/*  HT on 80M will reference to HT on 40M */
	if (idx_rate_sctn == 0 || idx_rate_sctn == 1)
		idx_bandwidth = 0;

	channel = phy_GetChannelIndexOfTxPowerLimit(channel);

	if (idx_regulation == -1 || idx_bandwidth == -1 ||
	    idx_rate_sctn == -1 || idx_channel == -1)
		return MAX_POWER_INDEX;


	for (i = 0; i < MAX_REGULATION_NUM; i++)
		limits[i] = hal_data->TxPwrLimit_2_4G[i]
						     [idx_bandwidth]
						     [idx_rate_sctn]
						     [idx_channel]
						     [rf_path];

	pwr_lmt = (idx_regulation == TXPWR_LMT_WW) ?
		phy_GetWorldWideLimit(limits) :
		hal_data->TxPwrLimit_2_4G[idx_regulation]
					 [idx_bandwidth]
					 [idx_rate_sctn]
					 [idx_channel]
					 [rf_path];

	return pwr_lmt;
}

void PHY_ConvertTxPowerLimitToPowerIndex(struct adapter *Adapter)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);
	u8 BW40PwrBasedBm2_4G = 0x2E;
	u8 regulation, bw, channel, rateSection;
	s8 tempValue = 0, tempPwrLmt = 0;
	u8 rfPath = 0;

	for (regulation = 0; regulation < MAX_REGULATION_NUM; ++regulation) {
		for (bw = 0; bw < MAX_2_4G_BANDWIDTH_NUM; ++bw) {
			for (channel = 0; channel < CHANNEL_MAX_NUMBER_2G; ++channel) {
				for (rateSection = 0; rateSection < MAX_RATE_SECTION_NUM; ++rateSection) {
					tempPwrLmt = pHalData->TxPwrLimit_2_4G[regulation][bw][rateSection][channel][ODM_RF_PATH_A];

					for (rfPath = ODM_RF_PATH_A; rfPath < MAX_RF_PATH_NUM; ++rfPath) {
						if (pHalData->odmpriv.PhyRegPgValueType == PHY_REG_PG_EXACT_VALUE) {
							if (rateSection == 5) /*  HT 4T */
								BW40PwrBasedBm2_4G = PHY_GetTxPowerByRateBase(Adapter, rfPath, RF_4TX, HT_MCS24_MCS31);
							else if (rateSection == 4) /*  HT 3T */
								BW40PwrBasedBm2_4G = PHY_GetTxPowerByRateBase(Adapter, rfPath, RF_3TX, HT_MCS16_MCS23);
							else if (rateSection == 3) /*  HT 2T */
								BW40PwrBasedBm2_4G = PHY_GetTxPowerByRateBase(Adapter, rfPath, RF_2TX, HT_MCS8_MCS15);
							else if (rateSection == 2) /*  HT 1T */
								BW40PwrBasedBm2_4G = PHY_GetTxPowerByRateBase(Adapter, rfPath, RF_1TX, HT_MCS0_MCS7);
							else if (rateSection == 1) /*  OFDM */
								BW40PwrBasedBm2_4G = PHY_GetTxPowerByRateBase(Adapter, rfPath, RF_1TX, OFDM);
							else if (rateSection == 0) /*  CCK */
								BW40PwrBasedBm2_4G = PHY_GetTxPowerByRateBase(Adapter, rfPath, RF_1TX, CCK);
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
}

void PHY_InitTxPowerLimit(struct adapter *Adapter)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);
	u8 i, j, k, l, m;

	for (i = 0; i < MAX_REGULATION_NUM; ++i) {
		for (j = 0; j < MAX_2_4G_BANDWIDTH_NUM; ++j)
			for (k = 0; k < MAX_RATE_SECTION_NUM; ++k)
				for (m = 0; m < CHANNEL_MAX_NUMBER_2G; ++m)
					for (l = 0; l < MAX_RF_PATH_NUM; ++l)
						pHalData->TxPwrLimit_2_4G[i][j][k][m][l] = MAX_POWER_INDEX;
	}
}

void PHY_SetTxPowerLimit(
	struct adapter *Adapter,
	u8 *Regulation,
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

	GetU1ByteIntegerFromStringInDecimal((s8 *)Channel, &channel);
	GetU1ByteIntegerFromStringInDecimal((s8 *)PowerLimit, &powerLimit);

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
	else
		return;

	if (eqNByte(Bandwidth, (u8 *)("20M"), 3))
		bandwidth = 0;
	else if (eqNByte(Bandwidth, (u8 *)("40M"), 3))
		bandwidth = 1;
	else if (eqNByte(Bandwidth, (u8 *)("80M"), 3))
		bandwidth = 2;
	else if (eqNByte(Bandwidth, (u8 *)("160M"), 4))
		bandwidth = 3;

	channelIndex = phy_GetChannelIndexOfTxPowerLimit(channel);

	if (channelIndex == -1)
		return;

	prevPowerLimit = pHalData->TxPwrLimit_2_4G[regulation][bandwidth][rateSection][channelIndex][ODM_RF_PATH_A];

	if (powerLimit < prevPowerLimit)
		pHalData->TxPwrLimit_2_4G[regulation][bandwidth][rateSection][channelIndex][ODM_RF_PATH_A] = powerLimit;
}

void Hal_ChannelPlanToRegulation(struct adapter *Adapter, u16 ChannelPlan)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);
	pHalData->Regulation2_4G = TXPWR_LMT_WW;

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
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI1:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_MKK1_MKK1:
		pHalData->Regulation2_4G = TXPWR_LMT_MKK;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_KCC1:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_FCC2:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_FCC3:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_FCC4:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_FCC5:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_FCC6:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_FCC1_FCC7:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI2:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI3:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_MKK1_MKK2:
		pHalData->Regulation2_4G = TXPWR_LMT_MKK;
		break;
	case RT_CHANNEL_DOMAIN_MKK1_MKK3:
		pHalData->Regulation2_4G = TXPWR_LMT_MKK;
		break;
	case RT_CHANNEL_DOMAIN_FCC1_NCC1:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_FCC1_NCC2:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_GLOBAL_NULL:
		pHalData->Regulation2_4G = TXPWR_LMT_WW;
		break;
	case RT_CHANNEL_DOMAIN_ETSI1_ETSI4:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_FCC1_FCC2:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_FCC1_NCC3:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI5:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_FCC1_FCC8:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI6:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI7:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI8:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI9:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI10:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI11:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_FCC1_NCC4:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI12:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_FCC1_FCC9:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_WORLD_ETSI13:
		pHalData->Regulation2_4G = TXPWR_LMT_ETSI;
		break;
	case RT_CHANNEL_DOMAIN_FCC1_FCC10:
		pHalData->Regulation2_4G = TXPWR_LMT_FCC;
		break;
	case RT_CHANNEL_DOMAIN_REALTEK_DEFINE: /* Realtek Reserve */
		pHalData->Regulation2_4G = TXPWR_LMT_WW;
		break;
	default:
		break;
	}
}
