
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

#include "odm_precomp.h"

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
		ODM_RT_TRACE(dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			     ("BbSwingIdxOfdm = %d BbSwingFlagOfdm=%d\n",
			     dm_odm->BbSwingIdxOfdm, dm_odm->BbSwingFlagOfdm));

		if (dm_odm->BbSwingIdxOfdm <= dm_odm->BbSwingIdxOfdmBase) {
			*pDirection	= 1;
			pwr_value		= (dm_odm->BbSwingIdxOfdmBase - dm_odm->BbSwingIdxOfdm);
		} else {
			*pDirection	= 2;
			pwr_value		= (dm_odm->BbSwingIdxOfdm - dm_odm->BbSwingIdxOfdmBase);
		}

		ODM_RT_TRACE(dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			     ("BbSwingIdxOfdm = %d BbSwingFlagOfdm=%d\n",
			     dm_odm->BbSwingIdxOfdm, dm_odm->BbSwingFlagOfdm));
	} else if (Type == 1) {	/*  For CCK adjust. */
		ODM_RT_TRACE(dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			     ("dm_odm->BbSwingIdxCck = %d dm_odm->BbSwingIdxCckBase = %d\n",
			     dm_odm->BbSwingIdxCck, dm_odm->BbSwingIdxCckBase));

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

	*pOutWriteVal = pwr_value | (pwr_value<<8) | (pwr_value<<16) | (pwr_value<<24);
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
		ODM_RT_TRACE(dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("odm_TxPwrTrackSetPwr88E CH=%d\n", *(dm_odm->pChannel)));
		PHY_SetTxPowerLevel8188E(dm_odm->Adapter, *(dm_odm->pChannel));
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
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);
	u8 ThermalValue = 0, delta, delta_LCK, delta_IQK, offset;
	u8 ThermalValue_AVG_count = 0;
	u32 ThermalValue_AVG = 0;
	s32 ele_A = 0, ele_D, TempCCk, X, value32;
	s32 Y, ele_C = 0;
	s8 OFDM_index[2], CCK_index = 0;
	s8 OFDM_index_old[2] = {0, 0}, CCK_index_old = 0;
	u32 i = 0, j = 0;
	bool is2t = false;

	u8 OFDM_min_index = 6, rf; /* OFDM BB Swing should be less than +3.0dB, which is required by Arthur */
	u8 Indexforchannel = 0/*GetRightChnlPlaceforIQK(pHalData->CurrentChannel)*/;
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

	dm_odm->RFCalibrateInfo.TXPowerTrackingCallbackCnt++; /* cosa add for debug */
	dm_odm->RFCalibrateInfo.bTXPowerTrackingInit = true;

	/*  <Kordan> RFCalibrateInfo.RegA24 will be initialized when ODM HW configuring, but MP configures with para files. */
	dm_odm->RFCalibrateInfo.RegA24 = 0x090e1317;

	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
		     ("===>dm_TXPowerTrackingCallback_ThermalMeter_8188E txpowercontrol %d\n",
		     dm_odm->RFCalibrateInfo.TxPowerTrackControl));

	ThermalValue = (u8)PHY_QueryRFReg(Adapter, RF_PATH_A, RF_T_METER_88E, 0xfc00);	/* 0x42: RF Reg[15:10] 88E */

	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
		     ("Readback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n",
		     ThermalValue, dm_odm->RFCalibrateInfo.ThermalValue, pHalData->EEPROMThermalMeter));

	if (is2t)
		rf = 2;
	else
		rf = 1;

	if (ThermalValue) {
		/* Query OFDM path A default setting */
		ele_D = PHY_QueryBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord)&bMaskOFDM_D;
		for (i = 0; i < OFDM_TABLE_SIZE_92D; i++) {	/* find the index */
			if (ele_D == (OFDMSwingTable[i]&bMaskOFDM_D)) {
				OFDM_index_old[0] = (u8)i;
				dm_odm->BbSwingIdxOfdmBase = (u8)i;
				ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
					     ("Initial pathA ele_D reg0x%x = 0x%x, OFDM_index=0x%x\n",
					     rOFDM0_XATxIQImbalance, ele_D, OFDM_index_old[0]));
				break;
			}
		}

		/* Query OFDM path B default setting */
		if (is2t) {
			ele_D = PHY_QueryBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord)&bMaskOFDM_D;
			for (i = 0; i < OFDM_TABLE_SIZE_92D; i++) {	/* find the index */
				if (ele_D == (OFDMSwingTable[i]&bMaskOFDM_D)) {
					OFDM_index_old[1] = (u8)i;
					ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
						     ("Initial pathB ele_D reg0x%x = 0x%x, OFDM_index=0x%x\n",
						rOFDM0_XBTxIQImbalance, ele_D, OFDM_index_old[1]));
					break;
				}
			}
		}

		/* Query CCK default setting From 0xa24 */
		TempCCk = dm_odm->RFCalibrateInfo.RegA24;

		for (i = 0; i < CCK_TABLE_SIZE; i++) {
			if (dm_odm->RFCalibrateInfo.bCCKinCH14) {
				if (memcmp(&TempCCk, &CCKSwingTable_Ch14[i][2], 4)) {
					CCK_index_old = (u8)i;
					dm_odm->BbSwingIdxCckBase = (u8)i;
					ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
						     ("Initial reg0x%x = 0x%x, CCK_index=0x%x, ch 14 %d\n",
						     rCCK0_TxFilter2, TempCCk, CCK_index_old, dm_odm->RFCalibrateInfo.bCCKinCH14));
					break;
				}
			} else {
				ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
					     ("RegA24: 0x%X, CCKSwingTable_Ch1_Ch13[%d][2]: CCKSwingTable_Ch1_Ch13[i][2]: 0x%X\n",
					     TempCCk, i, CCKSwingTable_Ch1_Ch13[i][2]));
				if (memcmp(&TempCCk, &CCKSwingTable_Ch1_Ch13[i][2], 4)) {
					CCK_index_old = (u8)i;
					dm_odm->BbSwingIdxCckBase = (u8)i;
					ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
						     ("Initial reg0x%x = 0x%x, CCK_index=0x%x, ch14 %d\n",
						     rCCK0_TxFilter2, TempCCk, CCK_index_old, dm_odm->RFCalibrateInfo.bCCKinCH14));
					break;
				}
			}
		}

		if (!dm_odm->RFCalibrateInfo.ThermalValue) {
			dm_odm->RFCalibrateInfo.ThermalValue = pHalData->EEPROMThermalMeter;
			dm_odm->RFCalibrateInfo.ThermalValue_LCK = ThermalValue;
			dm_odm->RFCalibrateInfo.ThermalValue_IQK = ThermalValue;

			for (i = 0; i < rf; i++)
				dm_odm->RFCalibrateInfo.OFDM_index[i] = OFDM_index_old[i];
			dm_odm->RFCalibrateInfo.CCK_index = CCK_index_old;
		}

		if (dm_odm->RFCalibrateInfo.bReloadtxpowerindex)
			ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
				     ("reload ofdm index for band switch\n"));

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

		if (ThermalValue_AVG_count) {
			ThermalValue = (u8)(ThermalValue_AVG / ThermalValue_AVG_count);
			ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
				     ("AVG Thermal Meter = 0x%x\n", ThermalValue));
		}

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
		ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
			     ("Readback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x delta 0x%x delta_LCK 0x%x delta_IQK 0x%x\n",
			     ThermalValue, dm_odm->RFCalibrateInfo.ThermalValue,
			     pHalData->EEPROMThermalMeter, delta, delta_LCK, delta_IQK));
		ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
			     ("pre thermal meter LCK 0x%x pre thermal meter IQK 0x%x delta_LCK_bound 0x%x delta_IQK_bound 0x%x\n",
			     dm_odm->RFCalibrateInfo.ThermalValue_LCK,
			     dm_odm->RFCalibrateInfo.ThermalValue_IQK,
			     dm_odm->RFCalibrateInfo.Delta_LCK,
			     dm_odm->RFCalibrateInfo.Delta_IQK));

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
				offset = index_mapping_NUM_88E-1;
			for (i = 0; i < rf; i++)
				OFDM_index[i] = dm_odm->RFCalibrateInfo.OFDM_index[i] + OFDM_index_mapping[j][offset];
			CCK_index = dm_odm->RFCalibrateInfo.CCK_index + OFDM_index_mapping[j][offset];

			if (is2t) {
				ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
					     ("temp OFDM_A_index=0x%x, OFDM_B_index=0x%x, CCK_index=0x%x\n",
					     dm_odm->RFCalibrateInfo.OFDM_index[0],
					     dm_odm->RFCalibrateInfo.OFDM_index[1],
					     dm_odm->RFCalibrateInfo.CCK_index));
			} else {
				ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
					     ("temp OFDM_A_index=0x%x, CCK_index=0x%x\n",
					     dm_odm->RFCalibrateInfo.OFDM_index[0],
					     dm_odm->RFCalibrateInfo.CCK_index));
			}

			for (i = 0; i < rf; i++) {
				if (OFDM_index[i] > OFDM_TABLE_SIZE_92D-1)
					OFDM_index[i] = OFDM_TABLE_SIZE_92D-1;
				else if (OFDM_index[i] < OFDM_min_index)
					OFDM_index[i] = OFDM_min_index;
			}

			if (CCK_index > CCK_TABLE_SIZE-1)
				CCK_index = CCK_TABLE_SIZE-1;
			else if (CCK_index < 0)
				CCK_index = 0;

			if (is2t) {
				ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
					     ("new OFDM_A_index=0x%x, OFDM_B_index=0x%x, CCK_index=0x%x\n",
					     OFDM_index[0], OFDM_index[1], CCK_index));
			} else {
				ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
					     ("new OFDM_A_index=0x%x, CCK_index=0x%x\n",
					     OFDM_index[0], CCK_index));
			}

			/* 2 temporarily remove bNOPG */
			/* Config by SwingTable */
			if (dm_odm->RFCalibrateInfo.TxPowerTrackControl) {
				dm_odm->RFCalibrateInfo.bDoneTxpower = true;

				/* Adujst OFDM Ant_A according to IQK result */
				ele_D = (OFDMSwingTable[(u8)OFDM_index[0]] & 0xFFC00000)>>22;
				X = dm_odm->RFCalibrateInfo.IQKMatrixRegSetting[Indexforchannel].Value[0][0];
				Y = dm_odm->RFCalibrateInfo.IQKMatrixRegSetting[Indexforchannel].Value[0][1];

				/*  Revse TX power table. */
				dm_odm->BbSwingIdxOfdm		= (u8)OFDM_index[0];
				dm_odm->BbSwingIdxCck		= (u8)CCK_index;

				if (dm_odm->BbSwingIdxOfdmCurrent != dm_odm->BbSwingIdxOfdm) {
					dm_odm->BbSwingIdxOfdmCurrent = dm_odm->BbSwingIdxOfdm;
					dm_odm->BbSwingFlagOfdm = true;
				}

				if (dm_odm->BbSwingIdxCckCurrent != dm_odm->BbSwingIdxCck) {
					dm_odm->BbSwingIdxCckCurrent = dm_odm->BbSwingIdxCck;
					dm_odm->BbSwingFlagCck = true;
				}

				if (X != 0) {
					if ((X & 0x00000200) != 0)
						X = X | 0xFFFFFC00;
					ele_A = ((X * ele_D)>>8)&0x000003FF;

					/* new element C = element D x Y */
					if ((Y & 0x00000200) != 0)
						Y = Y | 0xFFFFFC00;
					ele_C = ((Y * ele_D)>>8)&0x000003FF;

					/*  2012/04/23 MH According to Luke's suggestion, we can not write BB digital */
					/*  to increase TX power. Otherwise, EVM will be bad. */
				}

				ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
					     ("TxPwrTracking for path A: X=0x%x, Y=0x%x ele_A=0x%x ele_C=0x%x ele_D=0x%x 0xe94=0x%x 0xe9c=0x%x\n",
					     (u32)X, (u32)Y, (u32)ele_A, (u32)ele_C, (u32)ele_D, (u32)X, (u32)Y));

				if (is2t) {
					ele_D = (OFDMSwingTable[(u8)OFDM_index[1]] & 0xFFC00000)>>22;

					/* new element A = element D x X */
					X = dm_odm->RFCalibrateInfo.IQKMatrixRegSetting[Indexforchannel].Value[0][4];
					Y = dm_odm->RFCalibrateInfo.IQKMatrixRegSetting[Indexforchannel].Value[0][5];

					if ((X != 0) && (*(dm_odm->pBandType) == ODM_BAND_2_4G)) {
						if ((X & 0x00000200) != 0)	/* consider minus */
							X = X | 0xFFFFFC00;
						ele_A = ((X * ele_D)>>8)&0x000003FF;

						/* new element C = element D x Y */
						if ((Y & 0x00000200) != 0)
							Y = Y | 0xFFFFFC00;
						ele_C = ((Y * ele_D)>>8)&0x00003FF;

						/* wtite new elements A, C, D to regC88 and regC9C, element B is always 0 */
						value32 = (ele_D<<22) | ((ele_C&0x3F)<<16) | ele_A;
						PHY_SetBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, value32);

						value32 = (ele_C&0x000003C0)>>6;
						PHY_SetBBReg(Adapter, rOFDM0_XDTxAFE, bMaskH4Bits, value32);

						value32 = ((X * ele_D)>>7)&0x01;
						PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT28, value32);
					} else {
						PHY_SetBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, OFDMSwingTable[(u8)OFDM_index[1]]);
						PHY_SetBBReg(Adapter, rOFDM0_XDTxAFE, bMaskH4Bits, 0x00);
						PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT28, 0x00);
					}

					ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
						     ("TxPwrTracking path B: X=0x%x, Y=0x%x ele_A=0x%x ele_C=0x%x ele_D=0x%x 0xeb4=0x%x 0xebc=0x%x\n",
						     (u32)X, (u32)Y, (u32)ele_A,
						     (u32)ele_C, (u32)ele_D, (u32)X, (u32)Y));
				}

				ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
					     ("TxPwrTracking 0xc80 = 0x%x, 0xc94 = 0x%x RF 0x24 = 0x%x\n",
					     PHY_QueryBBReg(Adapter, 0xc80, bMaskDWord), PHY_QueryBBReg(Adapter,
					     0xc94, bMaskDWord), PHY_QueryRFReg(Adapter, RF_PATH_A, 0x24, bRFRegOffsetMask)));
			}
		}

		if (delta_IQK >= 8) { /*  Delta temperature is equal to or larger than 20 centigrade. */
			ODM_ResetIQKResult(dm_odm);

			dm_odm->RFCalibrateInfo.ThermalValue_IQK = ThermalValue;
			PHY_IQCalibrate_8188E(Adapter, false);
		}
		/* update thermal meter value */
		if (dm_odm->RFCalibrateInfo.TxPowerTrackControl)
			dm_odm->RFCalibrateInfo.ThermalValue = ThermalValue;
	}
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
		     ("<===dm_TXPowerTrackingCallback_ThermalMeter_8188E\n"));
	dm_odm->RFCalibrateInfo.TXPowercount = 0;
}

/* 1 7.	IQK */
#define MAX_TOLERANCE		5
#define IQK_DELAY_TIME		1		/* ms */

static u8 /* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
phy_PathA_IQK_8188E(struct adapter *adapt, bool configPathB)
{
	u32 regeac, regE94, regE9C, regEA4;
	u8 result = 0x00;
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path A IQK!\n"));

	/* 1 Tx IQK */
	/* path-A IQK setting */
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path-A IQK setting!\n"));
	PHY_SetBBReg(adapt, rTx_IQK_Tone_A, bMaskDWord, 0x10008c1c);
	PHY_SetBBReg(adapt, rRx_IQK_Tone_A, bMaskDWord, 0x30008c1c);
	PHY_SetBBReg(adapt, rTx_IQK_PI_A, bMaskDWord, 0x8214032a);
	PHY_SetBBReg(adapt, rRx_IQK_PI_A, bMaskDWord, 0x28160000);

	/* LO calibration setting */
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("LO calibration setting!\n"));
	PHY_SetBBReg(adapt, rIQK_AGC_Rsp, bMaskDWord, 0x00462911);

	/* One shot, path A LOK & IQK */
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("One shot, path A LOK & IQK!\n"));
	PHY_SetBBReg(adapt, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	PHY_SetBBReg(adapt, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);

	/*  delay x ms */
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Delay %d ms for One shot, path A LOK & IQK.\n", IQK_DELAY_TIME_88E));
	/* PlatformStallExecution(IQK_DELAY_TIME_88E*1000); */
	mdelay(IQK_DELAY_TIME_88E);

	/*  Check failed */
	regeac = PHY_QueryBBReg(adapt, rRx_Power_After_IQK_A_2, bMaskDWord);
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0xeac = 0x%x\n", regeac));
	regE94 = PHY_QueryBBReg(adapt, rTx_Power_Before_IQK_A, bMaskDWord);
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0xe94 = 0x%x\n", regE94));
	regE9C = PHY_QueryBBReg(adapt, rTx_Power_After_IQK_A, bMaskDWord);
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("0xe9c = 0x%x\n", regE9C));
	regEA4 = PHY_QueryBBReg(adapt, rRx_Power_Before_IQK_A_2, bMaskDWord);
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0xea4 = 0x%x\n", regEA4));

	if (!(regeac & BIT28) &&
	    (((regE94 & 0x03FF0000)>>16) != 0x142) &&
	    (((regE9C & 0x03FF0000)>>16) != 0x42))
		result |= 0x01;
	return result;
}

static u8 /* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
phy_PathA_RxIQK(struct adapter *adapt, bool configPathB)
{
	u32 regeac, regE94, regE9C, regEA4, u4tmp;
	u8 result = 0x00;
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path A Rx IQK!\n"));

	/* 1 Get TXIMR setting */
	/* modify RXIQK mode table */
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path-A Rx IQK modify RXIQK mode table!\n"));
	PHY_SetBBReg(adapt, rFPGA0_IQK, bMaskDWord, 0x00000000);
	PHY_SetRFReg(adapt, RF_PATH_A, RF_WE_LUT, bRFRegOffsetMask, 0x800a0);
	PHY_SetRFReg(adapt, RF_PATH_A, RF_RCK_OS, bRFRegOffsetMask, 0x30000);
	PHY_SetRFReg(adapt, RF_PATH_A, RF_TXPA_G1, bRFRegOffsetMask, 0x0000f);
	PHY_SetRFReg(adapt, RF_PATH_A, RF_TXPA_G2, bRFRegOffsetMask, 0xf117B);

	/* PA,PAD off */
	PHY_SetRFReg(adapt, RF_PATH_A, 0xdf, bRFRegOffsetMask, 0x980);
	PHY_SetRFReg(adapt, RF_PATH_A, 0x56, bRFRegOffsetMask, 0x51000);

	PHY_SetBBReg(adapt, rFPGA0_IQK, bMaskDWord, 0x80800000);

	/* IQK setting */
	PHY_SetBBReg(adapt, rTx_IQK, bMaskDWord, 0x01007c00);
	PHY_SetBBReg(adapt, rRx_IQK, bMaskDWord, 0x81004800);

	/* path-A IQK setting */
	PHY_SetBBReg(adapt, rTx_IQK_Tone_A, bMaskDWord, 0x10008c1c);
	PHY_SetBBReg(adapt, rRx_IQK_Tone_A, bMaskDWord, 0x30008c1c);
	PHY_SetBBReg(adapt, rTx_IQK_PI_A, bMaskDWord, 0x82160c1f);
	PHY_SetBBReg(adapt, rRx_IQK_PI_A, bMaskDWord, 0x28160000);

	/* LO calibration setting */
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("LO calibration setting!\n"));
	PHY_SetBBReg(adapt, rIQK_AGC_Rsp, bMaskDWord, 0x0046a911);

	/* One shot, path A LOK & IQK */
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("One shot, path A LOK & IQK!\n"));
	PHY_SetBBReg(adapt, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	PHY_SetBBReg(adapt, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);

	/*  delay x ms */
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
		     ("Delay %d ms for One shot, path A LOK & IQK.\n",
		     IQK_DELAY_TIME_88E));
	mdelay(IQK_DELAY_TIME_88E);

	/*  Check failed */
	regeac = PHY_QueryBBReg(adapt, rRx_Power_After_IQK_A_2, bMaskDWord);
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
		     ("0xeac = 0x%x\n", regeac));
	regE94 = PHY_QueryBBReg(adapt, rTx_Power_Before_IQK_A, bMaskDWord);
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
		     ("0xe94 = 0x%x\n", regE94));
	regE9C = PHY_QueryBBReg(adapt, rTx_Power_After_IQK_A, bMaskDWord);
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
		     ("0xe9c = 0x%x\n", regE9C));

	if (!(regeac & BIT28) &&
	    (((regE94 & 0x03FF0000)>>16) != 0x142) &&
	    (((regE9C & 0x03FF0000)>>16) != 0x42))
		result |= 0x01;
	else							/* if Tx not OK, ignore Rx */
		return result;

	u4tmp = 0x80007C00 | (regE94&0x3FF0000)  | ((regE9C&0x3FF0000) >> 16);
	PHY_SetBBReg(adapt, rTx_IQK, bMaskDWord, u4tmp);
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0xe40 = 0x%x u4tmp = 0x%x\n", PHY_QueryBBReg(adapt, rTx_IQK, bMaskDWord), u4tmp));

	/* 1 RX IQK */
	/* modify RXIQK mode table */
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path-A Rx IQK modify RXIQK mode table 2!\n"));
	PHY_SetBBReg(adapt, rFPGA0_IQK, bMaskDWord, 0x00000000);
	PHY_SetRFReg(adapt, RF_PATH_A, RF_WE_LUT, bRFRegOffsetMask, 0x800a0);
	PHY_SetRFReg(adapt, RF_PATH_A, RF_RCK_OS, bRFRegOffsetMask, 0x30000);
	PHY_SetRFReg(adapt, RF_PATH_A, RF_TXPA_G1, bRFRegOffsetMask, 0x0000f);
	PHY_SetRFReg(adapt, RF_PATH_A, RF_TXPA_G2, bRFRegOffsetMask, 0xf7ffa);
	PHY_SetBBReg(adapt, rFPGA0_IQK, bMaskDWord, 0x80800000);

	/* IQK setting */
	PHY_SetBBReg(adapt, rRx_IQK, bMaskDWord, 0x01004800);

	/* path-A IQK setting */
	PHY_SetBBReg(adapt, rTx_IQK_Tone_A, bMaskDWord, 0x38008c1c);
	PHY_SetBBReg(adapt, rRx_IQK_Tone_A, bMaskDWord, 0x18008c1c);
	PHY_SetBBReg(adapt, rTx_IQK_PI_A, bMaskDWord, 0x82160c05);
	PHY_SetBBReg(adapt, rRx_IQK_PI_A, bMaskDWord, 0x28160c1f);

	/* LO calibration setting */
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("LO calibration setting!\n"));
	PHY_SetBBReg(adapt, rIQK_AGC_Rsp, bMaskDWord, 0x0046a911);

	/* One shot, path A LOK & IQK */
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("One shot, path A LOK & IQK!\n"));
	PHY_SetBBReg(adapt, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	PHY_SetBBReg(adapt, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);

	/*  delay x ms */
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Delay %d ms for One shot, path A LOK & IQK.\n", IQK_DELAY_TIME_88E));
	/* PlatformStallExecution(IQK_DELAY_TIME_88E*1000); */
	mdelay(IQK_DELAY_TIME_88E);

	/*  Check failed */
	regeac = PHY_QueryBBReg(adapt, rRx_Power_After_IQK_A_2, bMaskDWord);
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("0xeac = 0x%x\n", regeac));
	regE94 = PHY_QueryBBReg(adapt, rTx_Power_Before_IQK_A, bMaskDWord);
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("0xe94 = 0x%x\n", regE94));
	regE9C = PHY_QueryBBReg(adapt, rTx_Power_After_IQK_A, bMaskDWord);
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("0xe9c = 0x%x\n", regE9C));
	regEA4 = PHY_QueryBBReg(adapt, rRx_Power_Before_IQK_A_2, bMaskDWord);
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("0xea4 = 0x%x\n", regEA4));

	/* reload RF 0xdf */
	PHY_SetBBReg(adapt, rFPGA0_IQK, bMaskDWord, 0x00000000);
	PHY_SetRFReg(adapt, RF_PATH_A, 0xdf, bRFRegOffsetMask, 0x180);

	if (!(regeac & BIT27) &&		/* if Tx is OK, check whether Rx is OK */
	    (((regEA4 & 0x03FF0000)>>16) != 0x132) &&
	    (((regeac & 0x03FF0000)>>16) != 0x36))
		result |= 0x02;
	else
		ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Path A Rx IQK fail!!\n"));

	return result;
}

static u8 /* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
phy_PathB_IQK_8188E(struct adapter *adapt)
{
	u32 regeac, regeb4, regebc, regec4, regecc;
	u8 result = 0x00;
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Path B IQK!\n"));

	/* One shot, path B LOK & IQK */
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("One shot, path A LOK & IQK!\n"));
	PHY_SetBBReg(adapt, rIQK_AGC_Cont, bMaskDWord, 0x00000002);
	PHY_SetBBReg(adapt, rIQK_AGC_Cont, bMaskDWord, 0x00000000);

	/*  delay x ms */
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
		     ("Delay %d ms for One shot, path B LOK & IQK.\n",
		     IQK_DELAY_TIME_88E));
	mdelay(IQK_DELAY_TIME_88E);

	/*  Check failed */
	regeac = PHY_QueryBBReg(adapt, rRx_Power_After_IQK_A_2, bMaskDWord);
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
		     ("0xeac = 0x%x\n", regeac));
	regeb4 = PHY_QueryBBReg(adapt, rTx_Power_Before_IQK_B, bMaskDWord);
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
		     ("0xeb4 = 0x%x\n", regeb4));
	regebc = PHY_QueryBBReg(adapt, rTx_Power_After_IQK_B, bMaskDWord);
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
		     ("0xebc = 0x%x\n", regebc));
	regec4 = PHY_QueryBBReg(adapt, rRx_Power_Before_IQK_B_2, bMaskDWord);
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
		     ("0xec4 = 0x%x\n", regec4));
	regecc = PHY_QueryBBReg(adapt, rRx_Power_After_IQK_B_2, bMaskDWord);
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
		     ("0xecc = 0x%x\n", regecc));

	if (!(regeac & BIT31) &&
	    (((regeb4 & 0x03FF0000)>>16) != 0x142) &&
	    (((regebc & 0x03FF0000)>>16) != 0x42))
		result |= 0x01;
	else
		return result;

	if (!(regeac & BIT30) &&
	    (((regec4 & 0x03FF0000)>>16) != 0x132) &&
	    (((regecc & 0x03FF0000)>>16) != 0x36))
		result |= 0x02;
	else
		ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Path B Rx IQK fail!!\n"));
	return result;
}

static void patha_fill_iqk(struct adapter *adapt, bool iqkok, s32 result[][8], u8 final_candidate, bool txonly)
{
	u32 Oldval_0, X, TX0_A, reg;
	s32 Y, TX0_C;
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
		     ("Path A IQ Calibration %s !\n",
		     (iqkok) ? "Success" : "Failed"));

	if (final_candidate == 0xFF) {
		return;
	} else if (iqkok) {
		Oldval_0 = (PHY_QueryBBReg(adapt, rOFDM0_XATxIQImbalance, bMaskDWord) >> 22) & 0x3FF;

		X = result[final_candidate][0];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;
		TX0_A = (X * Oldval_0) >> 8;
		ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
			     ("X = 0x%x, TX0_A = 0x%x, Oldval_0 0x%x\n",
			     X, TX0_A, Oldval_0));
		PHY_SetBBReg(adapt, rOFDM0_XATxIQImbalance, 0x3FF, TX0_A);

		PHY_SetBBReg(adapt, rOFDM0_ECCAThreshold, BIT(31), ((X * Oldval_0>>7) & 0x1));

		Y = result[final_candidate][1];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;

		TX0_C = (Y * Oldval_0) >> 8;
		ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Y = 0x%x, TX = 0x%x\n", Y, TX0_C));
		PHY_SetBBReg(adapt, rOFDM0_XCTxAFE, 0xF0000000, ((TX0_C&0x3C0)>>6));
		PHY_SetBBReg(adapt, rOFDM0_XATxIQImbalance, 0x003F0000, (TX0_C&0x3F));

		PHY_SetBBReg(adapt, rOFDM0_ECCAThreshold, BIT(29), ((Y * Oldval_0>>7) & 0x1));

		if (txonly) {
			ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("patha_fill_iqk only Tx OK\n"));
			return;
		}

		reg = result[final_candidate][2];
		PHY_SetBBReg(adapt, rOFDM0_XARxIQImbalance, 0x3FF, reg);

		reg = result[final_candidate][3] & 0x3F;
		PHY_SetBBReg(adapt, rOFDM0_XARxIQImbalance, 0xFC00, reg);

		reg = (result[final_candidate][3] >> 6) & 0xF;
		PHY_SetBBReg(adapt, rOFDM0_RxIQExtAnta, 0xF0000000, reg);
	}
}

static void pathb_fill_iqk(struct adapter *adapt, bool iqkok, s32 result[][8], u8 final_candidate, bool txonly)
{
	u32 Oldval_1, X, TX1_A, reg;
	s32 Y, TX1_C;
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
		     ("Path B IQ Calibration %s !\n",
		     (iqkok) ? "Success" : "Failed"));

	if (final_candidate == 0xFF) {
		return;
	} else if (iqkok) {
		Oldval_1 = (PHY_QueryBBReg(adapt, rOFDM0_XBTxIQImbalance, bMaskDWord) >> 22) & 0x3FF;

		X = result[final_candidate][4];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;
		TX1_A = (X * Oldval_1) >> 8;
		ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("X = 0x%x, TX1_A = 0x%x\n", X, TX1_A));
		PHY_SetBBReg(adapt, rOFDM0_XBTxIQImbalance, 0x3FF, TX1_A);

		PHY_SetBBReg(adapt, rOFDM0_ECCAThreshold, BIT(27), ((X * Oldval_1>>7) & 0x1));

		Y = result[final_candidate][5];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;

		TX1_C = (Y * Oldval_1) >> 8;
		ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Y = 0x%x, TX1_C = 0x%x\n", Y, TX1_C));
		PHY_SetBBReg(adapt, rOFDM0_XDTxAFE, 0xF0000000, ((TX1_C&0x3C0)>>6));
		PHY_SetBBReg(adapt, rOFDM0_XBTxIQImbalance, 0x003F0000, (TX1_C&0x3F));

		PHY_SetBBReg(adapt, rOFDM0_ECCAThreshold, BIT(25), ((Y * Oldval_1>>7) & 0x1));

		if (txonly)
			return;

		reg = result[final_candidate][6];
		PHY_SetBBReg(adapt, rOFDM0_XBRxIQImbalance, 0x3FF, reg);

		reg = result[final_candidate][7] & 0x3F;
		PHY_SetBBReg(adapt, rOFDM0_XBRxIQImbalance, 0xFC00, reg);

		reg = (result[final_candidate][7] >> 6) & 0xF;
		PHY_SetBBReg(adapt, rOFDM0_AGCRSSITable, 0x0000F000, reg);
	}
}

void _PHY_SaveADDARegisters(struct adapter *adapt, u32 *ADDAReg, u32 *ADDABackup, u32 RegisterNum)
{
	u32 i;
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;

	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Save ADDA parameters.\n"));
	for (i = 0; i < RegisterNum; i++) {
		ADDABackup[i] = PHY_QueryBBReg(adapt, ADDAReg[i], bMaskDWord);
	}
}

static void _PHY_SaveMACRegisters(
		struct adapter *adapt,
		u32 *MACReg,
		u32 *MACBackup
	)
{
	u32 i;
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Save MAC parameters.\n"));
	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++) {
		MACBackup[i] = rtw_read8(adapt, MACReg[i]);
	}
	MACBackup[i] = rtw_read32(adapt, MACReg[i]);
}

static void reload_adda_reg(struct adapter *adapt, u32 *ADDAReg, u32 *ADDABackup, u32 RegiesterNum)
{
	u32 i;
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;

	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Reload ADDA power saving parameters !\n"));
	for (i = 0; i < RegiesterNum; i++)
		PHY_SetBBReg(adapt, ADDAReg[i], bMaskDWord, ADDABackup[i]);
}

static void
_PHY_ReloadMACRegisters(
		struct adapter *adapt,
		u32 *MACReg,
		u32 *MACBackup
	)
{
	u32 i;
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;

	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Reload MAC parameters !\n"));
	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++) {
		rtw_write8(adapt, MACReg[i], (u8)MACBackup[i]);
	}
	rtw_write32(adapt, MACReg[i], MACBackup[i]);
}

void
_PHY_PathADDAOn(
		struct adapter *adapt,
		u32 *ADDAReg,
		bool isPathAOn,
		bool is2t
	)
{
	u32 pathOn;
	u32 i;
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("ADDA ON.\n"));

	pathOn = isPathAOn ? 0x04db25a4 : 0x0b1b25a4;
	if (!is2t) {
		pathOn = 0x0bdb25a0;
		PHY_SetBBReg(adapt, ADDAReg[0], bMaskDWord, 0x0b1b25a0);
	} else {
		PHY_SetBBReg(adapt, ADDAReg[0], bMaskDWord, pathOn);
	}

	for (i = 1; i < IQK_ADDA_REG_NUM; i++)
		PHY_SetBBReg(adapt, ADDAReg[i], bMaskDWord, pathOn);
}

void
_PHY_MACSettingCalibration(
		struct adapter *adapt,
		u32 *MACReg,
		u32 *MACBackup
	)
{
	u32 i = 0;
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;

	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("MAC settings for Calibration.\n"));

	rtw_write8(adapt, MACReg[i], 0x3F);

	for (i = 1; i < (IQK_MAC_REG_NUM - 1); i++) {
		rtw_write8(adapt, MACReg[i], (u8)(MACBackup[i]&(~BIT3)));
	}
	rtw_write8(adapt, MACReg[i], (u8)(MACBackup[i]&(~BIT5)));
}

void
_PHY_PathAStandBy(
	struct adapter *adapt
	)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;

	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Path-A standby mode!\n"));

	PHY_SetBBReg(adapt, rFPGA0_IQK, bMaskDWord, 0x0);
	PHY_SetBBReg(adapt, 0x840, bMaskDWord, 0x00010000);
	PHY_SetBBReg(adapt, rFPGA0_IQK, bMaskDWord, 0x80800000);
}

static void _PHY_PIModeSwitch(
		struct adapter *adapt,
		bool PIMode
	)
{
	u32 mode;
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;

	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("BB Switch to %s mode!\n", (PIMode ? "PI" : "SI")));

	mode = PIMode ? 0x01000100 : 0x01000000;
	PHY_SetBBReg(adapt, rFPGA0_XA_HSSIParameter1, bMaskDWord, mode);
	PHY_SetBBReg(adapt, rFPGA0_XB_HSSIParameter1, bMaskDWord, mode);
}

static bool phy_SimularityCompare_8188E(
		struct adapter *adapt,
		s32 resulta[][8],
		u8  c1,
		u8  c2
	)
{
	u32 i, j, diff, sim_bitmap, bound = 0;
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;
	u8 final_candidate[2] = {0xFF, 0xFF};	/* for path A and path B */
	bool result = true;
	bool is2t;
	s32 tmp1 = 0, tmp2 = 0;

	if ((dm_odm->RFType == ODM_2T2R) || (dm_odm->RFType == ODM_2T3R) || (dm_odm->RFType == ODM_2T4R))
		is2t = true;
	else
		is2t = false;

	if (is2t)
		bound = 8;
	else
		bound = 4;

	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("===> IQK:phy_SimularityCompare_8188E c1 %d c2 %d!!!\n", c1, c2));

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
			ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
				     ("IQK:phy_SimularityCompare_8188E differnece overflow index %d compare1 0x%x compare2 0x%x!!!\n",
				     i, resulta[c1][i], resulta[c2][i]));

			if ((i == 2 || i == 6) && !sim_bitmap) {
				if (resulta[c1][i] + resulta[c1][i+1] == 0)
					final_candidate[(i/4)] = c2;
				else if (resulta[c2][i] + resulta[c2][i+1] == 0)
					final_candidate[(i/4)] = c1;
				else
					sim_bitmap = sim_bitmap | (1<<i);
			} else {
				sim_bitmap = sim_bitmap | (1<<i);
			}
		}
	}

	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK:phy_SimularityCompare_8188E sim_bitmap   %d !!!\n", sim_bitmap));

	if (sim_bitmap == 0) {
		for (i = 0; i < (bound/4); i++) {
			if (final_candidate[i] != 0xFF) {
				for (j = i*4; j < (i+1)*4-2; j++)
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

static void phy_IQCalibrate_8188E(struct adapter *adapt, s32 result[][8], u8 t, bool is2t)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;
	u32 i;
	u8 PathAOK, PathBOK;
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

	u32 retryCount = 9;
	if (*(dm_odm->mp_mode) == 1)
		retryCount = 9;
	else
		retryCount = 2;
	/*  Note: IQ calibration must be performed after loading */
	/* 		PHY_REG.txt , and radio_a, radio_b.txt */

	if (t == 0) {
		ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQ Calibration for %s for %d times\n", (is2t ? "2T2R" : "1T1R"), t));

		/*  Save ADDA parameters, turn Path A ADDA on */
		_PHY_SaveADDARegisters(adapt, ADDA_REG, dm_odm->RFCalibrateInfo.ADDA_backup, IQK_ADDA_REG_NUM);
		_PHY_SaveMACRegisters(adapt, IQK_MAC_REG, dm_odm->RFCalibrateInfo.IQK_MAC_backup);
		_PHY_SaveADDARegisters(adapt, IQK_BB_REG_92C, dm_odm->RFCalibrateInfo.IQK_BB_backup, IQK_BB_REG_NUM);
	}
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQ Calibration for %s for %d times\n", (is2t ? "2T2R" : "1T1R"), t));

	_PHY_PathADDAOn(adapt, ADDA_REG, true, is2t);
	if (t == 0)
		dm_odm->RFCalibrateInfo.bRfPiEnable = (u8)PHY_QueryBBReg(adapt, rFPGA0_XA_HSSIParameter1, BIT(8));

	if (!dm_odm->RFCalibrateInfo.bRfPiEnable) {
		/*  Switch BB to PI mode to do IQ Calibration. */
		_PHY_PIModeSwitch(adapt, true);
	}

	/* BB setting */
	PHY_SetBBReg(adapt, rFPGA0_RFMOD, BIT24, 0x00);
	PHY_SetBBReg(adapt, rOFDM0_TRxPathEnable, bMaskDWord, 0x03a05600);
	PHY_SetBBReg(adapt, rOFDM0_TRMuxPar, bMaskDWord, 0x000800e4);
	PHY_SetBBReg(adapt, rFPGA0_XCD_RFInterfaceSW, bMaskDWord, 0x22204000);

	PHY_SetBBReg(adapt, rFPGA0_XAB_RFInterfaceSW, BIT10, 0x01);
	PHY_SetBBReg(adapt, rFPGA0_XAB_RFInterfaceSW, BIT26, 0x01);
	PHY_SetBBReg(adapt, rFPGA0_XA_RFInterfaceOE, BIT10, 0x00);
	PHY_SetBBReg(adapt, rFPGA0_XB_RFInterfaceOE, BIT10, 0x00);

	if (is2t) {
		PHY_SetBBReg(adapt, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x00010000);
		PHY_SetBBReg(adapt, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x00010000);
	}

	/* MAC settings */
	_PHY_MACSettingCalibration(adapt, IQK_MAC_REG, dm_odm->RFCalibrateInfo.IQK_MAC_backup);

	/* Page B init */
	/* AP or IQK */
	PHY_SetBBReg(adapt, rConfig_AntA, bMaskDWord, 0x0f600000);

	if (is2t)
		PHY_SetBBReg(adapt, rConfig_AntB, bMaskDWord, 0x0f600000);

	/*  IQ calibration setting */
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK setting!\n"));
	PHY_SetBBReg(adapt, rFPGA0_IQK, bMaskDWord, 0x80800000);
	PHY_SetBBReg(adapt, rTx_IQK, bMaskDWord, 0x01007c00);
	PHY_SetBBReg(adapt, rRx_IQK, bMaskDWord, 0x81004800);

	for (i = 0; i < retryCount; i++) {
		PathAOK = phy_PathA_IQK_8188E(adapt, is2t);
		if (PathAOK == 0x01) {
			ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path A Tx IQK Success!!\n"));
				result[t][0] = (PHY_QueryBBReg(adapt, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
				result[t][1] = (PHY_QueryBBReg(adapt, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;
			break;
		}
	}

	for (i = 0; i < retryCount; i++) {
		PathAOK = phy_PathA_RxIQK(adapt, is2t);
		if (PathAOK == 0x03) {
			ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Path A Rx IQK Success!!\n"));
				result[t][2] = (PHY_QueryBBReg(adapt, rRx_Power_Before_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
				result[t][3] = (PHY_QueryBBReg(adapt, rRx_Power_After_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
			break;
		} else {
			ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path A Rx IQK Fail!!\n"));
		}
	}

	if (0x00 == PathAOK) {
		ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path A IQK failed!!\n"));
	}

	if (is2t) {
		_PHY_PathAStandBy(adapt);

		/*  Turn Path B ADDA on */
		_PHY_PathADDAOn(adapt, ADDA_REG, false, is2t);

		for (i = 0; i < retryCount; i++) {
			PathBOK = phy_PathB_IQK_8188E(adapt);
			if (PathBOK == 0x03) {
				ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path B IQK Success!!\n"));
				result[t][4] = (PHY_QueryBBReg(adapt, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][5] = (PHY_QueryBBReg(adapt, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][6] = (PHY_QueryBBReg(adapt, rRx_Power_Before_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
				result[t][7] = (PHY_QueryBBReg(adapt, rRx_Power_After_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
				break;
			} else if (i == (retryCount - 1) && PathBOK == 0x01) {	/* Tx IQK OK */
				ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path B Only Tx IQK Success!!\n"));
				result[t][4] = (PHY_QueryBBReg(adapt, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][5] = (PHY_QueryBBReg(adapt, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;
			}
		}

		if (0x00 == PathBOK) {
			ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path B IQK failed!!\n"));
		}
	}

	/* Back to BB mode, load original value */
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK:Back to BB mode, load original value!\n"));
	PHY_SetBBReg(adapt, rFPGA0_IQK, bMaskDWord, 0);

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
		PHY_SetBBReg(adapt, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x00032ed3);
		if (is2t)
			PHY_SetBBReg(adapt, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x00032ed3);

		/* load 0xe30 IQC default value */
		PHY_SetBBReg(adapt, rTx_IQK_Tone_A, bMaskDWord, 0x01008c00);
		PHY_SetBBReg(adapt, rRx_IQK_Tone_A, bMaskDWord, 0x01008c00);
	}
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_IQCalibrate_8188E() <==\n"));
}

static void phy_LCCalibrate_8188E(struct adapter *adapt, bool is2t)
{
	u8 tmpreg;
	u32 RF_Amode = 0, RF_Bmode = 0, LC_Cal;

	/* Check continuous TX and Packet TX */
	tmpreg = rtw_read8(adapt, 0xd03);

	if ((tmpreg&0x70) != 0)			/* Deal with contisuous TX case */
		rtw_write8(adapt, 0xd03, tmpreg&0x8F);	/* disable all continuous TX */
	else							/*  Deal with Packet TX case */
		rtw_write8(adapt, REG_TXPAUSE, 0xFF);			/*  block all queues */

	if ((tmpreg&0x70) != 0) {
		/* 1. Read original RF mode */
		/* Path-A */
		RF_Amode = PHY_QueryRFReg(adapt, RF_PATH_A, RF_AC, bMask12Bits);

		/* Path-B */
		if (is2t)
			RF_Bmode = PHY_QueryRFReg(adapt, RF_PATH_B, RF_AC, bMask12Bits);

		/* 2. Set RF mode = standby mode */
		/* Path-A */
		PHY_SetRFReg(adapt, RF_PATH_A, RF_AC, bMask12Bits, (RF_Amode&0x8FFFF)|0x10000);

		/* Path-B */
		if (is2t)
			PHY_SetRFReg(adapt, RF_PATH_B, RF_AC, bMask12Bits, (RF_Bmode&0x8FFFF)|0x10000);
	}

	/* 3. Read RF reg18 */
	LC_Cal = PHY_QueryRFReg(adapt, RF_PATH_A, RF_CHNLBW, bMask12Bits);

	/* 4. Set LC calibration begin	bit15 */
	PHY_SetRFReg(adapt, RF_PATH_A, RF_CHNLBW, bMask12Bits, LC_Cal|0x08000);

	msleep(100);

	/* Restore original situation */
	if ((tmpreg&0x70) != 0) {
		/* Deal with continuous TX case */
		/* Path-A */
		rtw_write8(adapt, 0xd03, tmpreg);
		PHY_SetRFReg(adapt, RF_PATH_A, RF_AC, bMask12Bits, RF_Amode);

		/* Path-B */
		if (is2t)
			PHY_SetRFReg(adapt, RF_PATH_B, RF_AC, bMask12Bits, RF_Bmode);
	} else {
		/*  Deal with Packet TX case */
		rtw_write8(adapt, REG_TXPAUSE, 0x00);
	}
}

void PHY_IQCalibrate_8188E(struct adapter *adapt, bool recovery)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;
	struct mpt_context *pMptCtx = &(adapt->mppriv.MptCtx);
	s32 result[4][8];	/* last is final result */
	u8 i, final_candidate, Indexforchannel;
	bool pathaok, pathbok;
	s32 RegE94, RegE9C, RegEA4, RegEAC, RegEB4, RegEBC, RegEC4, RegECC;
	bool is12simular, is13simular, is23simular;
	bool singletone = false, carrier_sup = false;
	u32 IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
		rOFDM0_XARxIQImbalance, rOFDM0_XBRxIQImbalance,
		rOFDM0_ECCAThreshold, rOFDM0_AGCRSSITable,
		rOFDM0_XATxIQImbalance, rOFDM0_XBTxIQImbalance,
		rOFDM0_XCTxAFE, rOFDM0_XDTxAFE,
		rOFDM0_RxIQExtAnta};
	bool is2t;

	is2t = (dm_odm->RFType == ODM_2T2R) ? true : false;

	if (!(dm_odm->SupportAbility & ODM_RF_CALIBRATION))
		return;

	if (*(dm_odm->mp_mode) == 1) {
		singletone = pMptCtx->bSingleTone;
		carrier_sup = pMptCtx->bCarrierSuppression;
	}

	/*  20120213<Kordan> Turn on when continuous Tx to pass lab testing. (required by Edlu) */
	if (singletone || carrier_sup)
		return;

	if (recovery) {
		ODM_RT_TRACE(dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("PHY_IQCalibrate_8188E: Return due to recovery!\n"));
		reload_adda_reg(adapt, IQK_BB_REG_92C, dm_odm->RFCalibrateInfo.IQK_BB_backup_recover, 9);
		return;
	}
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("IQK:Start!!!\n"));

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
	pathbok = false;
	is12simular = false;
	is23simular = false;
	is13simular = false;

	for (i = 0; i < 3; i++) {
		phy_IQCalibrate_8188E(adapt, result, i, is2t);

		if (i == 1) {
			is12simular = phy_SimularityCompare_8188E(adapt, result, 0, 1);
			if (is12simular) {
				final_candidate = 0;
				ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK: is12simular final_candidate is %x\n", final_candidate));
				break;
			}
		}

		if (i == 2) {
			is13simular = phy_SimularityCompare_8188E(adapt, result, 0, 2);
			if (is13simular) {
				final_candidate = 0;
				ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK: is13simular final_candidate is %x\n", final_candidate));

				break;
			}
			is23simular = phy_SimularityCompare_8188E(adapt, result, 1, 2);
			if (is23simular) {
				final_candidate = 1;
				ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK: is23simular final_candidate is %x\n", final_candidate));
			} else {
				final_candidate = 3;
			}
		}
	}

	for (i = 0; i < 4; i++) {
		RegE94 = result[i][0];
		RegE9C = result[i][1];
		RegEA4 = result[i][2];
		RegEAC = result[i][3];
		RegEB4 = result[i][4];
		RegEBC = result[i][5];
		RegEC4 = result[i][6];
		RegECC = result[i][7];
		ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
			     ("IQK: RegE94=%x RegE9C=%x RegEA4=%x RegEAC=%x RegEB4=%x RegEBC=%x RegEC4=%x RegECC=%x\n",
			     RegE94, RegE9C, RegEA4, RegEAC, RegEB4, RegEBC, RegEC4, RegECC));
	}

	if (final_candidate != 0xff) {
		RegE94 = result[final_candidate][0];
		RegE9C = result[final_candidate][1];
		RegEA4 = result[final_candidate][2];
		RegEAC = result[final_candidate][3];
		RegEB4 = result[final_candidate][4];
		RegEBC = result[final_candidate][5];
		dm_odm->RFCalibrateInfo.RegE94 = RegE94;
		dm_odm->RFCalibrateInfo.RegE9C = RegE9C;
		dm_odm->RFCalibrateInfo.RegEB4 = RegEB4;
		dm_odm->RFCalibrateInfo.RegEBC = RegEBC;
		RegEC4 = result[final_candidate][6];
		RegECC = result[final_candidate][7];
		ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
			     ("IQK: final_candidate is %x\n", final_candidate));
		ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
			     ("IQK: RegE94=%x RegE9C=%x RegEA4=%x RegEAC=%x RegEB4=%x RegEBC=%x RegEC4=%x RegECC=%x\n",
			     RegE94, RegE9C, RegEA4, RegEAC, RegEB4, RegEBC, RegEC4, RegECC));
		pathaok = true;
		pathbok = true;
	} else {
		ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("IQK: FAIL use default value\n"));
		dm_odm->RFCalibrateInfo.RegE94 = 0x100;
		dm_odm->RFCalibrateInfo.RegEB4 = 0x100;	/* X default value */
		dm_odm->RFCalibrateInfo.RegE9C = 0x0;
		dm_odm->RFCalibrateInfo.RegEBC = 0x0;	/* Y default value */
	}
	if (RegE94 != 0)
		patha_fill_iqk(adapt, pathaok, result, final_candidate, (RegEA4 == 0));
	if (is2t) {
		if (RegEB4 != 0)
			pathb_fill_iqk(adapt, pathbok, result, final_candidate, (RegEC4 == 0));
	}

	Indexforchannel = ODM_GetRightChnlPlaceforIQK(pHalData->CurrentChannel);

/* To Fix BSOD when final_candidate is 0xff */
/* by sherry 20120321 */
	if (final_candidate < 4) {
		for (i = 0; i < IQK_Matrix_REG_NUM; i++)
			dm_odm->RFCalibrateInfo.IQKMatrixRegSetting[Indexforchannel].Value[0][i] = result[final_candidate][i];
		dm_odm->RFCalibrateInfo.IQKMatrixRegSetting[Indexforchannel].bIQKDone = true;
	}
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("\nIQK OK Indexforchannel %d.\n", Indexforchannel));

	_PHY_SaveADDARegisters(adapt, IQK_BB_REG_92C, dm_odm->RFCalibrateInfo.IQK_BB_backup_recover, 9);

	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("IQK finished\n"));
}

void PHY_LCCalibrate_8188E(struct adapter *adapt)
{
	bool singletone = false, carrier_sup = false;
	u32 timeout = 2000, timecount = 0;
	struct hal_data_8188e *pHalData = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;
	struct mpt_context *pMptCtx = &(adapt->mppriv.MptCtx);

	if (*(dm_odm->mp_mode) == 1) {
		singletone = pMptCtx->bSingleTone;
		carrier_sup = pMptCtx->bCarrierSuppression;
	}
	if (!(dm_odm->SupportAbility & ODM_RF_CALIBRATION))
		return;
	/*  20120213<Kordan> Turn on when continuous Tx to pass lab testing. (required by Edlu) */
	if (singletone || carrier_sup)
		return;

	while (*(dm_odm->pbScanInProcess) && timecount < timeout) {
		mdelay(50);
		timecount += 50;
	}

	dm_odm->RFCalibrateInfo.bLCKInProgress = true;

	if (dm_odm->RFType == ODM_2T2R) {
		phy_LCCalibrate_8188E(adapt, true);
	} else {
		/*  For 88C 1T1R */
		phy_LCCalibrate_8188E(adapt, false);
	}

	dm_odm->RFCalibrateInfo.bLCKInProgress = false;

	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
		     ("LCK:Finish!!!interface %d\n", dm_odm->InterfaceIndex));
}

static void phy_setrfpathswitch_8188e(struct adapter *adapt, bool main, bool is2t)
{
	if (!adapt->hw_init_completed) {
		u8 u1btmp;
		u1btmp = rtw_read8(adapt, REG_LEDCFG2) | BIT7;
		rtw_write8(adapt, REG_LEDCFG2, u1btmp);
		PHY_SetBBReg(adapt, rFPGA0_XAB_RFParameter, BIT13, 0x01);
	}

	if (is2t) {	/* 92C */
		if (main)
			PHY_SetBBReg(adapt, rFPGA0_XB_RFInterfaceOE, BIT5|BIT6, 0x1);	/* 92C_Path_A */
		else
			PHY_SetBBReg(adapt, rFPGA0_XB_RFInterfaceOE, BIT5|BIT6, 0x2);	/* BT */
	} else {			/* 88C */
		if (main)
			PHY_SetBBReg(adapt, rFPGA0_XA_RFInterfaceOE, BIT8|BIT9, 0x2);	/* Main */
		else
			PHY_SetBBReg(adapt, rFPGA0_XA_RFInterfaceOE, BIT8|BIT9, 0x1);	/* Aux */
	}
}

void PHY_SetRFPathSwitch_8188E(struct adapter *adapt, bool main)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;

	if (dm_odm->RFType == ODM_2T2R) {
		phy_setrfpathswitch_8188e(adapt, main, true);
	} else {
		/*  For 88C 1T1R */
		phy_setrfpathswitch_8188e(adapt, main, false);
	}
}
