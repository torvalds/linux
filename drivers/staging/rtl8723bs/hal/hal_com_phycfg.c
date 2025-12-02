// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include <drv_types.h>
#include <hal_data.h>
#include <linux/kernel.h>

u8 PHY_GetTxPowerByRateBase(struct adapter *Adapter, u8 RfPath,
			    enum rate_section RateSection)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);
	u8	value = 0;

	if (RfPath >= RF_PATH_MAX)
		return 0;

	switch (RateSection) {
	case CCK:
		value = pHalData->TxPwrByRateBase2_4G[RfPath][0];
		break;
	case OFDM:
		value = pHalData->TxPwrByRateBase2_4G[RfPath][1];
		break;
	case HT_MCS0_MCS7:
		value = pHalData->TxPwrByRateBase2_4G[RfPath][2];
		break;
	default:
		break;
	}

	return value;
}

static void
phy_SetTxPowerByRateBase(struct adapter *Adapter, u8 RfPath,
			 enum rate_section RateSection, u8 Value)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);

	if (RfPath >= RF_PATH_MAX)
		return;

	switch (RateSection) {
	case CCK:
		pHalData->TxPwrByRateBase2_4G[RfPath][0] = Value;
		break;
	case OFDM:
		pHalData->TxPwrByRateBase2_4G[RfPath][1] = Value;
		break;
	case HT_MCS0_MCS7:
		pHalData->TxPwrByRateBase2_4G[RfPath][2] = Value;
		break;
	default:
		break;
	}
}

static void phy_StoreTxPowerByRateBase(struct adapter *padapter)
{
	u8 path, base;

	for (path = RF_PATH_A; path <= RF_PATH_B; ++path) {
		base = PHY_GetTxPowerByRate(padapter, path, MGN_11M);
		phy_SetTxPowerByRateBase(padapter, path, CCK, base);

		base = PHY_GetTxPowerByRate(padapter, path, MGN_54M);
		phy_SetTxPowerByRateBase(padapter, path, OFDM, base);

		base = PHY_GetTxPowerByRate(padapter, path, MGN_MCS7);
		phy_SetTxPowerByRateBase(padapter, path, HT_MCS0_MCS7, base);
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

	default:
		break;
	}
}

static void PHY_StoreTxPowerByRateNew(struct adapter *padapter,	u32 RfPath,
				      u32 RegAddr, u32 BitMask, u32 Data)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	u8 i = 0, rateIndex[4] = {0}, rateNum = 0;
	s8	PwrByRateVal[4] = {0};

	PHY_GetRateValuesOfTxPowerByRate(padapter, RegAddr, BitMask, Data, rateIndex, PwrByRateVal, &rateNum);

	if (RfPath >= RF_PATH_MAX)
		return;

	for (i = 0; i < rateNum; ++i) {
		pHalData->TxPwrByRateOffset[RfPath][rateIndex[i]] = PwrByRateVal[i];
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
	u8 rfPath, rate;

	for (rfPath = RF_PATH_A; rfPath < MAX_RF_PATH_NUM; ++rfPath)
		for (rate = 0; rate < TX_PWR_BY_RATE_NUM_RATE; ++rate)
			pHalData->TxPwrByRateOffset[rfPath][rate] = 0;
}

void PHY_StoreTxPowerByRate(
	struct adapter *padapter,
	u32	RfPath,
	u32	RegAddr,
	u32	BitMask,
	u32	Data
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	struct dm_odm_t *pDM_Odm = &pHalData->odmpriv;

	if (pDM_Odm->PhyRegPgVersion > 0)
		PHY_StoreTxPowerByRateNew(padapter, RfPath, RegAddr, BitMask, Data);
	else if (pDM_Odm->PhyRegPgVersion == 0) {
		PHY_StoreTxPowerByRateOld(padapter, RegAddr, BitMask, Data);
	}
}

static void
phy_ConvertTxPowerByRateInDbmToRelativeValues(
struct adapter *padapter
	)
{
	u8	base = 0, i = 0, value = 0, path = 0;
	u8	cckRates[4] = {
		MGN_1M, MGN_2M, MGN_5_5M, MGN_11M
	};
	u8	ofdmRates[8] = {
		MGN_6M, MGN_9M, MGN_12M, MGN_18M, MGN_24M, MGN_36M, MGN_48M, MGN_54M
	};
	u8 mcs0_7Rates[8] = {
		MGN_MCS0, MGN_MCS1, MGN_MCS2, MGN_MCS3, MGN_MCS4, MGN_MCS5, MGN_MCS6, MGN_MCS7
	};
	for (path = RF_PATH_A; path < RF_PATH_MAX; ++path) {
		/*  CCK */
		base = PHY_GetTxPowerByRate(padapter, path, MGN_11M);
		for (i = 0; i < ARRAY_SIZE(cckRates); ++i) {
			value = PHY_GetTxPowerByRate(padapter, path, cckRates[i]);
			PHY_SetTxPowerByRate(padapter, path, cckRates[i], value - base);
		}

		/*  OFDM */
		base = PHY_GetTxPowerByRate(padapter, path, MGN_54M);
		for (i = 0; i < sizeof(ofdmRates); ++i) {
			value = PHY_GetTxPowerByRate(padapter, path, ofdmRates[i]);
			PHY_SetTxPowerByRate(padapter, path, ofdmRates[i], value - base);
		}

		/*  HT MCS0~7 */
		base = PHY_GetTxPowerByRate(padapter, path, MGN_MCS7);
		for (i = 0; i < sizeof(mcs0_7Rates); ++i) {
			value = PHY_GetTxPowerByRate(padapter, path, mcs0_7Rates[i]);
			PHY_SetTxPowerByRate(padapter, path, mcs0_7Rates[i], value - base);
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
		if (MGN_MCS0 <= Rate && Rate <= MGN_MCS7)
			txPower += pHalData->BW20_24G_Diff[RFPath][TX_1S];
	} else if (BandWidth == CHANNEL_WIDTH_40) { /*  BW40-1S, BW40-2S */
		if (MGN_MCS0 <= Rate && Rate <= MGN_MCS7)
			txPower += pHalData->BW40_24G_Diff[RFPath][TX_1S];
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
	default:
		break;
	}
	return index;
}

s8 PHY_GetTxPowerByRate(struct adapter *padapter, u8 RFPath, u8 Rate)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	s8 value = 0;
	u8 rateIndex = PHY_GetRateIndexOfTxPowerByRate(Rate);

	if ((padapter->registrypriv.RegEnableTxPowerByRate == 2 && pHalData->EEPROMRegulatory == 2) ||
		   padapter->registrypriv.RegEnableTxPowerByRate == 0)
		return 0;

	if (RFPath >= RF_PATH_MAX)
		return value;

	if (rateIndex >= TX_PWR_BY_RATE_NUM_RATE)
		return value;

	return pHalData->TxPwrByRateOffset[RFPath][rateIndex];

}

void PHY_SetTxPowerByRate(
	struct adapter *padapter,
	u8 RFPath,
	u8 Rate,
	s8 Value
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	u8 rateIndex = PHY_GetRateIndexOfTxPowerByRate(Rate);

	if (RFPath >= RF_PATH_MAX)
		return;

	if (rateIndex >= TX_PWR_BY_RATE_NUM_RATE)
		return;

	pHalData->TxPwrByRateOffset[RFPath][rateIndex] = Value;
}

void PHY_SetTxPowerLevelByPath(struct adapter *Adapter, u8 channel, u8 path)
{
	PHY_SetTxPowerIndexByRateSection(Adapter, path, channel, CCK);

	PHY_SetTxPowerIndexByRateSection(Adapter, path, channel, OFDM);
	PHY_SetTxPowerIndexByRateSection(Adapter, path, channel, HT_MCS0_MCS7);
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
					tempPwrLmt = pHalData->TxPwrLimit_2_4G[regulation][bw][rateSection][channel][RF_PATH_A];

					for (rfPath = RF_PATH_A; rfPath < MAX_RF_PATH_NUM; ++rfPath) {
						if (pHalData->odmpriv.PhyRegPgValueType == PHY_REG_PG_EXACT_VALUE) {
							if (rateSection == 2) /*  HT 1T */
								BW40PwrBasedBm2_4G = PHY_GetTxPowerByRateBase(Adapter, rfPath, HT_MCS0_MCS7);
							else if (rateSection == 1) /*  OFDM */
								BW40PwrBasedBm2_4G = PHY_GetTxPowerByRateBase(Adapter, rfPath, OFDM);
							else if (rateSection == 0) /*  CCK */
								BW40PwrBasedBm2_4G = PHY_GetTxPowerByRateBase(Adapter, rfPath, CCK);
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
	else
		return;

	if (eqNByte(Bandwidth, (u8 *)("20M"), 3))
		bandwidth = 0;
	else if (eqNByte(Bandwidth, (u8 *)("40M"), 3))
		bandwidth = 1;

	channelIndex = phy_GetChannelIndexOfTxPowerLimit(channel);

	if (channelIndex == -1)
		return;

	prevPowerLimit = pHalData->TxPwrLimit_2_4G[regulation][bandwidth][rateSection][channelIndex][RF_PATH_A];

	if (powerLimit < prevPowerLimit)
		pHalData->TxPwrLimit_2_4G[regulation][bandwidth][rateSection][channelIndex][RF_PATH_A] = powerLimit;
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
