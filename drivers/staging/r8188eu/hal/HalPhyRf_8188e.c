// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#include "../include/drv_types.h"

/*---------------------------Define Local Constant---------------------------*/
/*  2010/04/25 MH Define the max tx power tracking tx agc power. */
#define		ODM_TXPWRTRACK_MAX_IDX_88E		6

/*---------------------------Define Local Constant---------------------------*/

/* 3============================================================ */
/* 3 Tx Power Tracking */
/* 3============================================================ */
/*-----------------------------------------------------------------------------
 * Function:	ODM_TxPwrTrackAdjust88E()
 *
 * Overview:	88E we can not write 0xc80/c94/c4c/ 0xa2x. Instead of write TX agc.
 *				No matter OFDM & CCK use the same method.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	04/23/2012	MHC		Create Version 0.
 *	04/23/2012	MHC		Adjust TX agc directly not throughput BB digital.
 *
 *---------------------------------------------------------------------------*/
void ODM_TxPwrTrackAdjust88E(struct odm_dm_struct *dm_odm, u8 Type,/*  0 = OFDM, 1 = CCK */
	u8 *pDirection, 		/*  1 = +(increase) 2 = -(decrease) */
	u32 *pOutWriteVal		/*  Tx tracking CCK/OFDM BB swing index adjust */
	)
{
	u8 pwr_value = 0;
	/*  Tx power tracking BB swing table. */
	/*  The base index = 12. +((12-n)/2)dB 13~?? = decrease tx pwr by -((n-12)/2)dB */
	if (Type == 0) {		/*  For OFDM afjust */
		if (dm_odm->BbSwingIdxOfdm <= dm_odm->BbSwingIdxOfdmBase) {
			*pDirection	= 1;
			pwr_value		= (dm_odm->BbSwingIdxOfdmBase - dm_odm->BbSwingIdxOfdm);
		} else {
			*pDirection	= 2;
			pwr_value		= (dm_odm->BbSwingIdxOfdm - dm_odm->BbSwingIdxOfdmBase);
		}
	} else if (Type == 1) {	/*  For CCK adjust. */
		if (dm_odm->BbSwingIdxCck <= dm_odm->BbSwingIdxCckBase) {
			*pDirection	= 1;
			pwr_value		= (dm_odm->BbSwingIdxCckBase - dm_odm->BbSwingIdxCck);
		} else {
			*pDirection	= 2;
			pwr_value		= (dm_odm->BbSwingIdxCck - dm_odm->BbSwingIdxCckBase);
		}
	}

	/*  */
	/*  2012/04/25 MH According to Ed/Luke.Lees estimate for EVM the max tx power tracking */
	/*  need to be less than 6 power index for 88E. */
	/*  */
	if (pwr_value >= ODM_TXPWRTRACK_MAX_IDX_88E && *pDirection == 1)
		pwr_value = ODM_TXPWRTRACK_MAX_IDX_88E;

	*pOutWriteVal = pwr_value | (pwr_value << 8) | (pwr_value << 16) | (pwr_value << 24);
}	/*  ODM_TxPwrTrackAdjust88E */

/*-----------------------------------------------------------------------------
 * Function:	odm_TxPwrTrackSetPwr88E()
 *
 * Overview:	88E change all channel tx power accordign to flag.
 *				OFDM & CCK are all different.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	04/23/2012	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
static void odm_TxPwrTrackSetPwr88E(struct odm_dm_struct *dm_odm)
{
	if (dm_odm->BbSwingFlagOfdm || dm_odm->BbSwingFlagCck) {
		PHY_SetTxPowerLevel8188E(dm_odm->Adapter, *dm_odm->pChannel);
		dm_odm->BbSwingFlagOfdm = false;
		dm_odm->BbSwingFlagCck	= false;
	}
}	/*  odm_TxPwrTrackSetPwr88E */

/* 091212 chiyokolin */
void
odm_TXPowerTrackingCallback_ThermalMeter_8188E(
	struct adapter *Adapter
	)
{
	struct hal_data_8188e *pHalData = &Adapter->haldata;
	u8 ThermalValue = 0, delta, delta_LCK, delta_IQK, offset;
	u8 ThermalValue_AVG_count = 0;
	u32 ThermalValue_AVG = 0;
	s32 ele_D, TempCCk;
	s8 OFDM_index, CCK_index = 0;
	s8 OFDM_index_old = 0, CCK_index_old = 0;
	u32 i = 0, j = 0;

	u8 OFDM_min_index = 6; /* OFDM BB Swing should be less than +3.0dB, which is required by Arthur */
	s8 OFDM_index_mapping[2][index_mapping_NUM_88E] = {
		{0, 0, 2, 3, 4, 4, 		/* 2.4G, decrease power */
		5, 6, 7, 7, 8, 9,
		10, 10, 11}, /*  For lower temperature, 20120220 updated on 20120220. */
		{0, 0, -1, -2, -3, -4, 		/* 2.4G, increase power */
		-4, -4, -4, -5, -7, -8,
		-9, -9, -10},
	};
	u8 Thermal_mapping[2][index_mapping_NUM_88E] = {
		{0, 2, 4, 6, 8, 10, 		/* 2.4G, decrease power */
		12, 14, 16, 18, 20, 22,
		24, 26, 27},
		{0, 2, 4, 6, 8, 10, 		/* 2.4G,, increase power */
		12, 14, 16, 18, 20, 22,
		25, 25, 25},
	};
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;

	/*  2012/04/25 MH Add for tx power tracking to set tx power in tx agc for 88E. */
	odm_TxPwrTrackSetPwr88E(dm_odm);

	/*  <Kordan> RFCalibrateInfo.RegA24 will be initialized when ODM HW configuring, but MP configures with para files. */
	dm_odm->RFCalibrateInfo.RegA24 = 0x090e1317;

	ThermalValue = (u8)rtl8188e_PHY_QueryRFReg(Adapter, RF_T_METER_88E, 0xfc00); /* 0x42: RF Reg[15:10] 88E */

	if (ThermalValue) {
		/* Query OFDM path A default setting */
		ele_D = rtl8188e_PHY_QueryBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord) & bMaskOFDM_D;
		for (i = 0; i < OFDM_TABLE_SIZE_92D; i++) {	/* find the index */
			if (ele_D == (OFDMSwingTable[i] & bMaskOFDM_D)) {
				OFDM_index_old = (u8)i;
				dm_odm->BbSwingIdxOfdmBase = (u8)i;
				break;
			}
		}

		/* Query CCK default setting From 0xa24 */
		TempCCk = dm_odm->RFCalibrateInfo.RegA24;

		for (i = 0; i < CCK_TABLE_SIZE; i++) {
			if (memcmp((void *)&TempCCk, (void *)&CCKSwingTable_Ch1_Ch13[i][2], 4)) {
				CCK_index_old = (u8)i;
				dm_odm->BbSwingIdxCckBase = (u8)i;
				break;
			}
		}

		if (!dm_odm->RFCalibrateInfo.ThermalValue) {
			dm_odm->RFCalibrateInfo.ThermalValue = pHalData->EEPROMThermalMeter;
			dm_odm->RFCalibrateInfo.ThermalValue_LCK = ThermalValue;
			dm_odm->RFCalibrateInfo.ThermalValue_IQK = ThermalValue;

			dm_odm->RFCalibrateInfo.OFDM_index = OFDM_index_old;
			dm_odm->RFCalibrateInfo.CCK_index = CCK_index_old;
		}

		/* calculate average thermal meter */
		dm_odm->RFCalibrateInfo.ThermalValue_AVG[dm_odm->RFCalibrateInfo.ThermalValue_AVG_index] = ThermalValue;
		dm_odm->RFCalibrateInfo.ThermalValue_AVG_index++;
		if (dm_odm->RFCalibrateInfo.ThermalValue_AVG_index == AVG_THERMAL_NUM_88E)
			dm_odm->RFCalibrateInfo.ThermalValue_AVG_index = 0;

		for (i = 0; i < AVG_THERMAL_NUM_88E; i++) {
			if (dm_odm->RFCalibrateInfo.ThermalValue_AVG[i]) {
				ThermalValue_AVG += dm_odm->RFCalibrateInfo.ThermalValue_AVG[i];
				ThermalValue_AVG_count++;
			}
		}

		if (ThermalValue_AVG_count)
			ThermalValue = (u8)(ThermalValue_AVG / ThermalValue_AVG_count);

		if (dm_odm->RFCalibrateInfo.bReloadtxpowerindex) {
			delta = ThermalValue > pHalData->EEPROMThermalMeter ?
				(ThermalValue - pHalData->EEPROMThermalMeter) :
				(pHalData->EEPROMThermalMeter - ThermalValue);
			dm_odm->RFCalibrateInfo.bReloadtxpowerindex = false;
			dm_odm->RFCalibrateInfo.bDoneTxpower = false;
		} else if (dm_odm->RFCalibrateInfo.bDoneTxpower) {
			delta = (ThermalValue > dm_odm->RFCalibrateInfo.ThermalValue) ?
				(ThermalValue - dm_odm->RFCalibrateInfo.ThermalValue) :
				(dm_odm->RFCalibrateInfo.ThermalValue - ThermalValue);
		} else {
			delta = ThermalValue > pHalData->EEPROMThermalMeter ?
				(ThermalValue - pHalData->EEPROMThermalMeter) :
				(pHalData->EEPROMThermalMeter - ThermalValue);
		}
		delta_LCK = (ThermalValue > dm_odm->RFCalibrateInfo.ThermalValue_LCK) ?
			    (ThermalValue - dm_odm->RFCalibrateInfo.ThermalValue_LCK) :
			    (dm_odm->RFCalibrateInfo.ThermalValue_LCK - ThermalValue);
		delta_IQK = (ThermalValue > dm_odm->RFCalibrateInfo.ThermalValue_IQK) ?
			    (ThermalValue - dm_odm->RFCalibrateInfo.ThermalValue_IQK) :
			    (dm_odm->RFCalibrateInfo.ThermalValue_IQK - ThermalValue);

		if ((delta_LCK >= 8)) { /*  Delta temperature is equal to or larger than 20 centigrade. */
			dm_odm->RFCalibrateInfo.ThermalValue_LCK = ThermalValue;
			PHY_LCCalibrate_8188E(Adapter);
		}

		if (delta > 0 && dm_odm->RFCalibrateInfo.TxPowerTrackControl) {
			delta = ThermalValue > pHalData->EEPROMThermalMeter ?
				(ThermalValue - pHalData->EEPROMThermalMeter) :
				(pHalData->EEPROMThermalMeter - ThermalValue);
			/* calculate new OFDM / CCK offset */
			if (ThermalValue > pHalData->EEPROMThermalMeter)
				j = 1;
			else
				j = 0;
			for (offset = 0; offset < index_mapping_NUM_88E; offset++) {
				if (delta < Thermal_mapping[j][offset]) {
					if (offset != 0)
						offset--;
					break;
				}
			}
			if (offset >= index_mapping_NUM_88E)
				offset = index_mapping_NUM_88E - 1;
			OFDM_index = dm_odm->RFCalibrateInfo.OFDM_index + OFDM_index_mapping[j][offset];
			CCK_index = dm_odm->RFCalibrateInfo.CCK_index + OFDM_index_mapping[j][offset];

			if (OFDM_index > OFDM_TABLE_SIZE_92D - 1)
				OFDM_index = OFDM_TABLE_SIZE_92D - 1;
			else if (OFDM_index < OFDM_min_index)
				OFDM_index = OFDM_min_index;

			if (CCK_index > CCK_TABLE_SIZE - 1)
				CCK_index = CCK_TABLE_SIZE - 1;
			else if (CCK_index < 0)
				CCK_index = 0;

			/* 2 temporarily remove bNOPG */
			/* Config by SwingTable */
			if (dm_odm->RFCalibrateInfo.TxPowerTrackControl) {
				dm_odm->RFCalibrateInfo.bDoneTxpower = true;

				/*  Revse TX power table. */
				dm_odm->BbSwingIdxOfdm		= (u8)OFDM_index;
				dm_odm->BbSwingIdxCck		= (u8)CCK_index;

				if (dm_odm->BbSwingIdxOfdmCurrent != dm_odm->BbSwingIdxOfdm) {
					dm_odm->BbSwingIdxOfdmCurrent = dm_odm->BbSwingIdxOfdm;
					dm_odm->BbSwingFlagOfdm = true;
				}

				if (dm_odm->BbSwingIdxCckCurrent != dm_odm->BbSwingIdxCck) {
					dm_odm->BbSwingIdxCckCurrent = dm_odm->BbSwingIdxCck;
					dm_odm->BbSwingFlagCck = true;
				}
			}
		}

		if (delta_IQK >= 8) { /*  Delta temperature is equal to or larger than 20 centigrade. */
			dm_odm->RFCalibrateInfo.ThermalValue_IQK = ThermalValue;
			PHY_IQCalibrate_8188E(Adapter, false);
		}
		/* update thermal meter value */
		if (dm_odm->RFCalibrateInfo.TxPowerTrackControl)
			dm_odm->RFCalibrateInfo.ThermalValue = ThermalValue;
	}
}

/* 1 7.	IQK */
#define MAX_TOLERANCE		5
#define IQK_DELAY_TIME		1		/* ms */

static u8 /* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
phy_PathA_IQK_8188E(struct adapter *adapt)
{
	u32 regeac, regE94, regE9C;
	u8 result = 0x00;

	/* 1 Tx IQK */
	/* path-A IQK setting */
	rtl8188e_PHY_SetBBReg(adapt, rTx_IQK_Tone_A, bMaskDWord, 0x10008c1c);
	rtl8188e_PHY_SetBBReg(adapt, rRx_IQK_Tone_A, bMaskDWord, 0x30008c1c);
	rtl8188e_PHY_SetBBReg(adapt, rTx_IQK_PI_A, bMaskDWord, 0x8214032a);
	rtl8188e_PHY_SetBBReg(adapt, rRx_IQK_PI_A, bMaskDWord, 0x28160000);

	/* LO calibration setting */
	rtl8188e_PHY_SetBBReg(adapt, rIQK_AGC_Rsp, bMaskDWord, 0x00462911);

	/* One shot, path A LOK & IQK */
	rtl8188e_PHY_SetBBReg(adapt, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	rtl8188e_PHY_SetBBReg(adapt, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);

	/*  delay x ms */
	/* PlatformStallExecution(IQK_DELAY_TIME_88E*1000); */
	mdelay(IQK_DELAY_TIME_88E);

	/*  Check failed */
	regeac = rtl8188e_PHY_QueryBBReg(adapt, rRx_Power_After_IQK_A_2, bMaskDWord);
	regE94 = rtl8188e_PHY_QueryBBReg(adapt, rTx_Power_Before_IQK_A, bMaskDWord);
	regE9C = rtl8188e_PHY_QueryBBReg(adapt, rTx_Power_After_IQK_A, bMaskDWord);

	if (!(regeac & BIT(28)) &&
	    (((regE94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((regE9C & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	return result;
}

static u8 /* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
phy_PathA_RxIQK(struct adapter *adapt)
{
	u32 regeac, regE94, regE9C, regEA4, u4tmp;
	u8 result = 0x00;

	/* 1 Get TXIMR setting */
	/* modify RXIQK mode table */
	rtl8188e_PHY_SetBBReg(adapt, rFPGA0_IQK, bMaskDWord, 0x00000000);
	rtl8188e_PHY_SetRFReg(adapt, RF_WE_LUT, bRFRegOffsetMask, 0x800a0);
	rtl8188e_PHY_SetRFReg(adapt, RF_RCK_OS, bRFRegOffsetMask, 0x30000);
	rtl8188e_PHY_SetRFReg(adapt, RF_TXPA_G1, bRFRegOffsetMask, 0x0000f);
	rtl8188e_PHY_SetRFReg(adapt, RF_TXPA_G2, bRFRegOffsetMask, 0xf117B);

	/* PA,PAD off */
	rtl8188e_PHY_SetRFReg(adapt, 0xdf, bRFRegOffsetMask, 0x980);
	rtl8188e_PHY_SetRFReg(adapt, 0x56, bRFRegOffsetMask, 0x51000);

	rtl8188e_PHY_SetBBReg(adapt, rFPGA0_IQK, bMaskDWord, 0x80800000);

	/* IQK setting */
	rtl8188e_PHY_SetBBReg(adapt, rTx_IQK, bMaskDWord, 0x01007c00);
	rtl8188e_PHY_SetBBReg(adapt, rRx_IQK, bMaskDWord, 0x81004800);

	/* path-A IQK setting */
	rtl8188e_PHY_SetBBReg(adapt, rTx_IQK_Tone_A, bMaskDWord, 0x10008c1c);
	rtl8188e_PHY_SetBBReg(adapt, rRx_IQK_Tone_A, bMaskDWord, 0x30008c1c);
	rtl8188e_PHY_SetBBReg(adapt, rTx_IQK_PI_A, bMaskDWord, 0x82160c1f);
	rtl8188e_PHY_SetBBReg(adapt, rRx_IQK_PI_A, bMaskDWord, 0x28160000);

	/* LO calibration setting */
	rtl8188e_PHY_SetBBReg(adapt, rIQK_AGC_Rsp, bMaskDWord, 0x0046a911);

	/* One shot, path A LOK & IQK */
	rtl8188e_PHY_SetBBReg(adapt, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	rtl8188e_PHY_SetBBReg(adapt, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);

	/*  delay x ms */
	mdelay(IQK_DELAY_TIME_88E);

	/*  Check failed */
	regeac = rtl8188e_PHY_QueryBBReg(adapt, rRx_Power_After_IQK_A_2, bMaskDWord);
	regE94 = rtl8188e_PHY_QueryBBReg(adapt, rTx_Power_Before_IQK_A, bMaskDWord);
	regE9C = rtl8188e_PHY_QueryBBReg(adapt, rTx_Power_After_IQK_A, bMaskDWord);

	if (!(regeac & BIT(28)) &&
	    (((regE94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((regE9C & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else							/* if Tx not OK, ignore Rx */
		return result;

	u4tmp = 0x80007C00 | (regE94 & 0x3FF0000)  | ((regE9C & 0x3FF0000) >> 16);
	rtl8188e_PHY_SetBBReg(adapt, rTx_IQK, bMaskDWord, u4tmp);

	/* 1 RX IQK */
	/* modify RXIQK mode table */
	rtl8188e_PHY_SetBBReg(adapt, rFPGA0_IQK, bMaskDWord, 0x00000000);
	rtl8188e_PHY_SetRFReg(adapt, RF_WE_LUT, bRFRegOffsetMask, 0x800a0);
	rtl8188e_PHY_SetRFReg(adapt, RF_RCK_OS, bRFRegOffsetMask, 0x30000);
	rtl8188e_PHY_SetRFReg(adapt, RF_TXPA_G1, bRFRegOffsetMask, 0x0000f);
	rtl8188e_PHY_SetRFReg(adapt, RF_TXPA_G2, bRFRegOffsetMask, 0xf7ffa);
	rtl8188e_PHY_SetBBReg(adapt, rFPGA0_IQK, bMaskDWord, 0x80800000);

	/* IQK setting */
	rtl8188e_PHY_SetBBReg(adapt, rRx_IQK, bMaskDWord, 0x01004800);

	/* path-A IQK setting */
	rtl8188e_PHY_SetBBReg(adapt, rTx_IQK_Tone_A, bMaskDWord, 0x38008c1c);
	rtl8188e_PHY_SetBBReg(adapt, rRx_IQK_Tone_A, bMaskDWord, 0x18008c1c);
	rtl8188e_PHY_SetBBReg(adapt, rTx_IQK_PI_A, bMaskDWord, 0x82160c05);
	rtl8188e_PHY_SetBBReg(adapt, rRx_IQK_PI_A, bMaskDWord, 0x28160c1f);

	/* LO calibration setting */
	rtl8188e_PHY_SetBBReg(adapt, rIQK_AGC_Rsp, bMaskDWord, 0x0046a911);

	/* One shot, path A LOK & IQK */
	rtl8188e_PHY_SetBBReg(adapt, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	rtl8188e_PHY_SetBBReg(adapt, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);

	/*  delay x ms */
	/* PlatformStallExecution(IQK_DELAY_TIME_88E*1000); */
	mdelay(IQK_DELAY_TIME_88E);

	/*  Check failed */
	regeac = rtl8188e_PHY_QueryBBReg(adapt, rRx_Power_After_IQK_A_2, bMaskDWord);
	regE94 = rtl8188e_PHY_QueryBBReg(adapt, rTx_Power_Before_IQK_A, bMaskDWord);
	regE9C = rtl8188e_PHY_QueryBBReg(adapt, rTx_Power_After_IQK_A, bMaskDWord);
	regEA4 = rtl8188e_PHY_QueryBBReg(adapt, rRx_Power_Before_IQK_A_2, bMaskDWord);

	/* reload RF 0xdf */
	rtl8188e_PHY_SetBBReg(adapt, rFPGA0_IQK, bMaskDWord, 0x00000000);
	rtl8188e_PHY_SetRFReg(adapt, 0xdf, bRFRegOffsetMask, 0x180);

	if (!(regeac & BIT(27)) &&		/* if Tx is OK, check whether Rx is OK */
	    (((regEA4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((regeac & 0x03FF0000) >> 16) != 0x36))
		result |= 0x02;

	return result;
}

static void patha_fill_iqk(struct adapter *adapt, bool iqkok, s32 result[][8], u8 final_candidate, bool txonly)
{
	u32 Oldval_0, X, TX0_A, reg;
	s32 Y, TX0_C;

	if (final_candidate == 0xFF) {
		return;
	} else if (iqkok) {
		Oldval_0 = (rtl8188e_PHY_QueryBBReg(adapt, rOFDM0_XATxIQImbalance, bMaskDWord) >> 22) & 0x3FF;

		X = result[final_candidate][0];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;
		TX0_A = (X * Oldval_0) >> 8;
		rtl8188e_PHY_SetBBReg(adapt, rOFDM0_XATxIQImbalance, 0x3FF, TX0_A);

		rtl8188e_PHY_SetBBReg(adapt, rOFDM0_ECCAThreshold, BIT(31), ((X * Oldval_0 >> 7) & 0x1));

		Y = result[final_candidate][1];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;

		TX0_C = (Y * Oldval_0) >> 8;
		rtl8188e_PHY_SetBBReg(adapt, rOFDM0_XCTxAFE, 0xF0000000, ((TX0_C & 0x3C0) >> 6));
		rtl8188e_PHY_SetBBReg(adapt, rOFDM0_XATxIQImbalance, 0x003F0000, (TX0_C & 0x3F));

		rtl8188e_PHY_SetBBReg(adapt, rOFDM0_ECCAThreshold, BIT(29), ((Y * Oldval_0 >> 7) & 0x1));

		if (txonly)
			return;

		reg = result[final_candidate][2];
		rtl8188e_PHY_SetBBReg(adapt, rOFDM0_XARxIQImbalance, 0x3FF, reg);

		reg = result[final_candidate][3] & 0x3F;
		rtl8188e_PHY_SetBBReg(adapt, rOFDM0_XARxIQImbalance, 0xFC00, reg);

		reg = (result[final_candidate][3] >> 6) & 0xF;
		rtl8188e_PHY_SetBBReg(adapt, rOFDM0_RxIQExtAnta, 0xF0000000, reg);
	}
}

void _PHY_SaveADDARegisters(struct adapter *adapt, u32 *ADDAReg, u32 *ADDABackup, u32 RegisterNum)
{
	u32 i;

	for (i = 0; i < RegisterNum; i++) {
		ADDABackup[i] = rtl8188e_PHY_QueryBBReg(adapt, ADDAReg[i], bMaskDWord);
	}
}

static void _PHY_SaveMACRegisters(
		struct adapter *adapt,
		u32 *MACReg,
		u32 *MACBackup
	)
{
	u32 i;

	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++)
		MACBackup[i] = rtw_read8(adapt, MACReg[i]);

	MACBackup[i] = rtw_read32(adapt, MACReg[i]);
}

static void reload_adda_reg(struct adapter *adapt, u32 *ADDAReg, u32 *ADDABackup, u32 RegiesterNum)
{
	u32 i;

	for (i = 0; i < RegiesterNum; i++)
		rtl8188e_PHY_SetBBReg(adapt, ADDAReg[i], bMaskDWord, ADDABackup[i]);
}

static void
_PHY_ReloadMACRegisters(
		struct adapter *adapt,
		u32 *MACReg,
		u32 *MACBackup
	)
{
	u32 i;

	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++)
		rtw_write8(adapt, MACReg[i], (u8)MACBackup[i]);

	rtw_write32(adapt, MACReg[i], MACBackup[i]);
}

static void
_PHY_PathADDAOn(
		struct adapter *adapt,
		u32 *ADDAReg)
{
	u32 i;

	rtl8188e_PHY_SetBBReg(adapt, ADDAReg[0], bMaskDWord, 0x0b1b25a0);

	for (i = 1; i < IQK_ADDA_REG_NUM; i++)
		rtl8188e_PHY_SetBBReg(adapt, ADDAReg[i], bMaskDWord, 0x0bdb25a0);
}

void
_PHY_MACSettingCalibration(
		struct adapter *adapt,
		u32 *MACReg,
		u32 *MACBackup
	)
{
	u32 i = 0;

	rtw_write8(adapt, MACReg[i], 0x3F);

	for (i = 1; i < (IQK_MAC_REG_NUM - 1); i++)
		rtw_write8(adapt, MACReg[i], (u8)(MACBackup[i] & (~BIT(3))));

	rtw_write8(adapt, MACReg[i], (u8)(MACBackup[i] & (~BIT(5))));
}

static void _PHY_PIModeSwitch(
		struct adapter *adapt,
		bool PIMode
	)
{
	u32 mode;

	mode = PIMode ? 0x01000100 : 0x01000000;
	rtl8188e_PHY_SetBBReg(adapt, rFPGA0_XA_HSSIParameter1, bMaskDWord, mode);
	rtl8188e_PHY_SetBBReg(adapt, rFPGA0_XB_HSSIParameter1, bMaskDWord, mode);
}

static bool phy_SimularityCompare_8188E(
		struct adapter *adapt,
		s32 resulta[][8],
		u8  c1,
		u8  c2
	)
{
	u32 i, j, diff, sim_bitmap, bound = 0;
	u8 final_candidate[2] = {0xFF, 0xFF};	/* for path A and path B */
	bool result = true;
	s32 tmp1 = 0, tmp2 = 0;

	bound = 4;
	sim_bitmap = 0;

	for (i = 0; i < bound; i++) {
		if ((i == 1) || (i == 3) || (i == 5) || (i == 7)) {
			if ((resulta[c1][i] & 0x00000200) != 0)
				tmp1 = resulta[c1][i] | 0xFFFFFC00;
			else
				tmp1 = resulta[c1][i];

			if ((resulta[c2][i] & 0x00000200) != 0)
				tmp2 = resulta[c2][i] | 0xFFFFFC00;
			else
				tmp2 = resulta[c2][i];
		} else {
			tmp1 = resulta[c1][i];
			tmp2 = resulta[c2][i];
		}

		diff = (tmp1 > tmp2) ? (tmp1 - tmp2) : (tmp2 - tmp1);

		if (diff > MAX_TOLERANCE) {
			if ((i == 2 || i == 6) && !sim_bitmap) {
				if (resulta[c1][i] + resulta[c1][i + 1] == 0)
					final_candidate[(i / 4)] = c2;
				else if (resulta[c2][i] + resulta[c2][i + 1] == 0)
					final_candidate[(i / 4)] = c1;
				else
					sim_bitmap = sim_bitmap | (1 << i);
			} else {
				sim_bitmap = sim_bitmap | (1 << i);
			}
		}
	}

	if (sim_bitmap == 0) {
		for (i = 0; i < (bound / 4); i++) {
			if (final_candidate[i] != 0xFF) {
				for (j = i * 4; j < (i + 1) * 4 - 2; j++)
					resulta[3][j] = resulta[final_candidate[i]][j];
				result = false;
			}
		}
		return result;
	} else {
		if (!(sim_bitmap & 0x03)) {		   /* path A TX OK */
			for (i = 0; i < 2; i++)
				resulta[3][i] = resulta[c1][i];
		}
		if (!(sim_bitmap & 0x0c)) {		   /* path A RX OK */
			for (i = 2; i < 4; i++)
				resulta[3][i] = resulta[c1][i];
		}

		if (!(sim_bitmap & 0x30)) { /* path B TX OK */
			for (i = 4; i < 6; i++)
				resulta[3][i] = resulta[c1][i];
		}

		if (!(sim_bitmap & 0xc0)) { /* path B RX OK */
			for (i = 6; i < 8; i++)
				resulta[3][i] = resulta[c1][i];
		}
		return false;
	}
}

static void phy_IQCalibrate_8188E(struct adapter *adapt, s32 result[][8], u8 t)
{
	struct hal_data_8188e *pHalData = &adapt->haldata;
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;
	u32 i;
	u8 PathAOK;
	u32 ADDA_REG[IQK_ADDA_REG_NUM] = {
						rFPGA0_XCD_SwitchControl, rBlue_Tooth,
						rRx_Wait_CCA, 	rTx_CCK_RFON,
						rTx_CCK_BBON, rTx_OFDM_RFON,
						rTx_OFDM_BBON, rTx_To_Rx,
						rTx_To_Tx, 	rRx_CCK,
						rRx_OFDM, 	rRx_Wait_RIFS,
						rRx_TO_Rx, 	rStandby,
						rSleep, 			rPMPD_ANAEN };
	u32 IQK_MAC_REG[IQK_MAC_REG_NUM] = {
						REG_TXPAUSE, 	REG_BCN_CTRL,
						REG_BCN_CTRL_1, REG_GPIO_MUXCFG};

	/* since 92C & 92D have the different define in IQK_BB_REG */
	u32 IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
							rOFDM0_TRxPathEnable, 	rOFDM0_TRMuxPar,
							rFPGA0_XCD_RFInterfaceSW, rConfig_AntA, rConfig_AntB,
							rFPGA0_XAB_RFInterfaceSW, rFPGA0_XA_RFInterfaceOE,
							rFPGA0_XB_RFInterfaceOE, rFPGA0_RFMOD
							};
	u32 retryCount = 2;
	/*  Note: IQ calibration must be performed after loading */
	/* 		PHY_REG.txt , and radio_a, radio_b.txt */

	if (t == 0) {
		/*  Save ADDA parameters, turn Path A ADDA on */
		_PHY_SaveADDARegisters(adapt, ADDA_REG, dm_odm->RFCalibrateInfo.ADDA_backup, IQK_ADDA_REG_NUM);
		_PHY_SaveMACRegisters(adapt, IQK_MAC_REG, dm_odm->RFCalibrateInfo.IQK_MAC_backup);
		_PHY_SaveADDARegisters(adapt, IQK_BB_REG_92C, dm_odm->RFCalibrateInfo.IQK_BB_backup, IQK_BB_REG_NUM);
	}

	_PHY_PathADDAOn(adapt, ADDA_REG);
	if (t == 0)
		dm_odm->RFCalibrateInfo.bRfPiEnable = (u8)rtl8188e_PHY_QueryBBReg(adapt, rFPGA0_XA_HSSIParameter1, BIT(8));

	if (!dm_odm->RFCalibrateInfo.bRfPiEnable) {
		/*  Switch BB to PI mode to do IQ Calibration. */
		_PHY_PIModeSwitch(adapt, true);
	}

	/* BB setting */
	rtl8188e_PHY_SetBBReg(adapt, rFPGA0_RFMOD, BIT(24), 0x00);
	rtl8188e_PHY_SetBBReg(adapt, rOFDM0_TRxPathEnable, bMaskDWord, 0x03a05600);
	rtl8188e_PHY_SetBBReg(adapt, rOFDM0_TRMuxPar, bMaskDWord, 0x000800e4);
	rtl8188e_PHY_SetBBReg(adapt, rFPGA0_XCD_RFInterfaceSW, bMaskDWord, 0x22204000);

	rtl8188e_PHY_SetBBReg(adapt, rFPGA0_XAB_RFInterfaceSW, BIT(10), 0x01);
	rtl8188e_PHY_SetBBReg(adapt, rFPGA0_XAB_RFInterfaceSW, BIT(26), 0x01);
	rtl8188e_PHY_SetBBReg(adapt, rFPGA0_XA_RFInterfaceOE, BIT(10), 0x00);
	rtl8188e_PHY_SetBBReg(adapt, rFPGA0_XB_RFInterfaceOE, BIT(10), 0x00);

	/* MAC settings */
	_PHY_MACSettingCalibration(adapt, IQK_MAC_REG, dm_odm->RFCalibrateInfo.IQK_MAC_backup);

	/* Page B init */
	/* AP or IQK */
	rtl8188e_PHY_SetBBReg(adapt, rConfig_AntA, bMaskDWord, 0x0f600000);


	/*  IQ calibration setting */
	rtl8188e_PHY_SetBBReg(adapt, rFPGA0_IQK, bMaskDWord, 0x80800000);
	rtl8188e_PHY_SetBBReg(adapt, rTx_IQK, bMaskDWord, 0x01007c00);
	rtl8188e_PHY_SetBBReg(adapt, rRx_IQK, bMaskDWord, 0x81004800);

	for (i = 0; i < retryCount; i++) {
		PathAOK = phy_PathA_IQK_8188E(adapt);
		if (PathAOK == 0x01) {
			result[t][0] = (rtl8188e_PHY_QueryBBReg(adapt, rTx_Power_Before_IQK_A, bMaskDWord) & 0x3FF0000) >> 16;
			result[t][1] = (rtl8188e_PHY_QueryBBReg(adapt, rTx_Power_After_IQK_A, bMaskDWord) & 0x3FF0000) >> 16;
			break;
		}
	}

	for (i = 0; i < retryCount; i++) {
		PathAOK = phy_PathA_RxIQK(adapt);
		if (PathAOK == 0x03) {
			result[t][2] = (rtl8188e_PHY_QueryBBReg(adapt, rRx_Power_Before_IQK_A_2, bMaskDWord) & 0x3FF0000) >> 16;
			result[t][3] = (rtl8188e_PHY_QueryBBReg(adapt, rRx_Power_After_IQK_A_2, bMaskDWord) & 0x3FF0000) >> 16;
			break;
		}
	}

	/* Back to BB mode, load original value */
	rtl8188e_PHY_SetBBReg(adapt, rFPGA0_IQK, bMaskDWord, 0);

	if (t != 0) {
		if (!dm_odm->RFCalibrateInfo.bRfPiEnable) {
			/*  Switch back BB to SI mode after finish IQ Calibration. */
			_PHY_PIModeSwitch(adapt, false);
		}

		/*  Reload ADDA power saving parameters */
		reload_adda_reg(adapt, ADDA_REG, dm_odm->RFCalibrateInfo.ADDA_backup, IQK_ADDA_REG_NUM);

		/*  Reload MAC parameters */
		_PHY_ReloadMACRegisters(adapt, IQK_MAC_REG, dm_odm->RFCalibrateInfo.IQK_MAC_backup);

		reload_adda_reg(adapt, IQK_BB_REG_92C, dm_odm->RFCalibrateInfo.IQK_BB_backup, IQK_BB_REG_NUM);

		/*  Restore RX initial gain */
		rtl8188e_PHY_SetBBReg(adapt, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x00032ed3);

		/* load 0xe30 IQC default value */
		rtl8188e_PHY_SetBBReg(adapt, rTx_IQK_Tone_A, bMaskDWord, 0x01008c00);
		rtl8188e_PHY_SetBBReg(adapt, rRx_IQK_Tone_A, bMaskDWord, 0x01008c00);
	}
}

static void phy_LCCalibrate_8188E(struct adapter *adapt)
{
	u8 tmpreg;
	u32 RF_Amode = 0, LC_Cal;

	/* Check continuous TX and Packet TX */
	tmpreg = rtw_read8(adapt, 0xd03);

	if ((tmpreg & 0x70) != 0)			/* Deal with contisuous TX case */
		rtw_write8(adapt, 0xd03, tmpreg & 0x8F);	/* disable all continuous TX */
	else							/*  Deal with Packet TX case */
		rtw_write8(adapt, REG_TXPAUSE, 0xFF);		/*  block all queues */

	if ((tmpreg & 0x70) != 0) {
		/* 1. Read original RF mode */
		/* Path-A */
		RF_Amode = rtl8188e_PHY_QueryRFReg(adapt, RF_AC, bMask12Bits);

		/* 2. Set RF mode = standby mode */
		/* Path-A */
		rtl8188e_PHY_SetRFReg(adapt, RF_AC, bMask12Bits, (RF_Amode & 0x8FFFF) | 0x10000);
	}

	/* 3. Read RF reg18 */
	LC_Cal = rtl8188e_PHY_QueryRFReg(adapt, RF_CHNLBW, bMask12Bits);

	/* 4. Set LC calibration begin	bit15 */
	rtl8188e_PHY_SetRFReg(adapt, RF_CHNLBW, bMask12Bits, LC_Cal | 0x08000);

	msleep(100);

	/* Restore original situation */
	if ((tmpreg & 0x70) != 0) {
		/* Deal with continuous TX case */
		/* Path-A */
		rtw_write8(adapt, 0xd03, tmpreg);
		rtl8188e_PHY_SetRFReg(adapt, RF_AC, bMask12Bits, RF_Amode);
	} else {
		/*  Deal with Packet TX case */
		rtw_write8(adapt, REG_TXPAUSE, 0x00);
	}
}

void PHY_IQCalibrate_8188E(struct adapter *adapt, bool recovery)
{
	struct hal_data_8188e *pHalData = &adapt->haldata;
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;
	s32 result[4][8];	/* last is final result */
	u8 i, final_candidate;
	bool pathaok;
	s32 RegE94, RegE9C, RegEA4, RegEB4, RegEBC;
	bool is12simular, is13simular, is23simular;
	u32 IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
		rOFDM0_XARxIQImbalance, rOFDM0_XBRxIQImbalance,
		rOFDM0_ECCAThreshold, rOFDM0_AGCRSSITable,
		rOFDM0_XATxIQImbalance, rOFDM0_XBTxIQImbalance,
		rOFDM0_XCTxAFE, rOFDM0_XDTxAFE,
		rOFDM0_RxIQExtAnta};

	if (recovery) {
		reload_adda_reg(adapt, IQK_BB_REG_92C, dm_odm->RFCalibrateInfo.IQK_BB_backup_recover, 9);
		return;
	}

	for (i = 0; i < 8; i++) {
		result[0][i] = 0;
		result[1][i] = 0;
		result[2][i] = 0;
		if ((i == 0) || (i == 2) || (i == 4)  || (i == 6))
			result[3][i] = 0x100;
		else
			result[3][i] = 0;
	}
	final_candidate = 0xff;
	pathaok = false;
	is12simular = false;
	is23simular = false;
	is13simular = false;

	for (i = 0; i < 3; i++) {
		phy_IQCalibrate_8188E(adapt, result, i);

		if (i == 1) {
			is12simular = phy_SimularityCompare_8188E(adapt, result, 0, 1);
			if (is12simular) {
				final_candidate = 0;
				break;
			}
		}

		if (i == 2) {
			is13simular = phy_SimularityCompare_8188E(adapt, result, 0, 2);
			if (is13simular) {
				final_candidate = 0;

				break;
			}
			is23simular = phy_SimularityCompare_8188E(adapt, result, 1, 2);
			if (is23simular) {
				final_candidate = 1;
			} else {
				final_candidate = 3;
			}
		}
	}

	for (i = 0; i < 4; i++) {
		RegE94 = result[i][0];
		RegE9C = result[i][1];
		RegEA4 = result[i][2];
		RegEB4 = result[i][4];
		RegEBC = result[i][5];
	}

	if (final_candidate != 0xff) {
		RegE94 = result[final_candidate][0];
		RegE9C = result[final_candidate][1];
		RegEA4 = result[final_candidate][2];
		RegEB4 = result[final_candidate][4];
		RegEBC = result[final_candidate][5];
		dm_odm->RFCalibrateInfo.RegE94 = RegE94;
		dm_odm->RFCalibrateInfo.RegE9C = RegE9C;
		dm_odm->RFCalibrateInfo.RegEB4 = RegEB4;
		dm_odm->RFCalibrateInfo.RegEBC = RegEBC;
		pathaok = true;
	} else {
		dm_odm->RFCalibrateInfo.RegE94 = 0x100;
		dm_odm->RFCalibrateInfo.RegEB4 = 0x100;	/* X default value */
		dm_odm->RFCalibrateInfo.RegE9C = 0x0;
		dm_odm->RFCalibrateInfo.RegEBC = 0x0;	/* Y default value */
	}
	if (RegE94 != 0)
		patha_fill_iqk(adapt, pathaok, result, final_candidate, (RegEA4 == 0));

/* To Fix BSOD when final_candidate is 0xff */
/* by sherry 20120321 */
	if (final_candidate < 4) {
		for (i = 0; i < IQK_Matrix_REG_NUM; i++)
			dm_odm->RFCalibrateInfo.IQKMatrixRegSetting.Value[0][i] = result[final_candidate][i];
		dm_odm->RFCalibrateInfo.IQKMatrixRegSetting.bIQKDone = true;
	}

	_PHY_SaveADDARegisters(adapt, IQK_BB_REG_92C, dm_odm->RFCalibrateInfo.IQK_BB_backup_recover, 9);
}

void PHY_LCCalibrate_8188E(struct adapter *adapt)
{
	u32 timeout = 2000, timecount = 0;
	struct hal_data_8188e *pHalData = &adapt->haldata;
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;

	while (*dm_odm->pbScanInProcess && timecount < timeout) {
		mdelay(50);
		timecount += 50;
	}

	phy_LCCalibrate_8188E(adapt);
}
