/*
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
 */

#include "odm_precomp.h"
#include "phy.h"

/*  2010/04/25 MH Define the max tx power tracking tx agc power. */
#define		ODM_TXPWRTRACK_MAX_IDX_88E		6


static u8 get_right_chnl_for_iqk(u8 chnl)
{
	u8 channel_all[ODM_TARGET_CHNL_NUM_2G_5G] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
		36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64,
		100, 102, 104, 106, 108, 110, 112, 114, 116, 118, 120, 122,
		124, 126, 128, 130, 132, 134, 136, 138, 140, 149, 151, 153,
		155, 157, 159, 161, 163, 165
	};
	u8 place = chnl;

	if (chnl > 14) {
		for (place = 14; place < sizeof(channel_all); place++) {
			if (channel_all[place] == chnl)
				return place-13;
		}
	}
	return 0;
}

void rtl88eu_dm_txpower_track_adjust(struct odm_dm_struct *dm_odm, u8 type,
				     u8 *direction, u32 *out_write_val)
{
	u8 pwr_value = 0;
	/*  Tx power tracking BB swing table. */
	if (type == 0) { /* For OFDM adjust */
		ODM_RT_TRACE(dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			     ("BbSwingIdxOfdm = %d BbSwingFlagOfdm=%d\n",
			     dm_odm->BbSwingIdxOfdm, dm_odm->BbSwingFlagOfdm));

		if (dm_odm->BbSwingIdxOfdm <= dm_odm->BbSwingIdxOfdmBase) {
			*direction = 1;
			pwr_value = (dm_odm->BbSwingIdxOfdmBase -
				     dm_odm->BbSwingIdxOfdm);
		} else {
			*direction = 2;
			pwr_value = (dm_odm->BbSwingIdxOfdm -
				     dm_odm->BbSwingIdxOfdmBase);
		}

	} else if (type == 1) { /* For CCK adjust. */
		ODM_RT_TRACE(dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			     ("dm_odm->BbSwingIdxCck = %d dm_odm->BbSwingIdxCckBase = %d\n",
			     dm_odm->BbSwingIdxCck, dm_odm->BbSwingIdxCckBase));

		if (dm_odm->BbSwingIdxCck <= dm_odm->BbSwingIdxCckBase) {
			*direction = 1;
			pwr_value = (dm_odm->BbSwingIdxCckBase -
				     dm_odm->BbSwingIdxCck);
		} else {
			*direction = 2;
			pwr_value = (dm_odm->BbSwingIdxCck -
				     dm_odm->BbSwingIdxCckBase);
		}

	}

	if (pwr_value >= ODM_TXPWRTRACK_MAX_IDX_88E && *direction == 1)
		pwr_value = ODM_TXPWRTRACK_MAX_IDX_88E;

	*out_write_val = pwr_value | (pwr_value<<8) | (pwr_value<<16) |
			 (pwr_value<<24);
}

static void dm_txpwr_track_setpwr(struct odm_dm_struct *dm_odm)
{
	if (dm_odm->BbSwingFlagOfdm || dm_odm->BbSwingFlagCck) {
		ODM_RT_TRACE(dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			     ("dm_txpwr_track_setpwr CH=%d\n", *(dm_odm->pChannel)));
		phy_set_tx_power_level(dm_odm->Adapter, *(dm_odm->pChannel));
		dm_odm->BbSwingFlagOfdm = false;
		dm_odm->BbSwingFlagCck = false;
	}
}

void rtl88eu_dm_txpower_tracking_callback_thermalmeter(struct adapter *adapt)
{
	struct hal_data_8188e *hal_data = GET_HAL_DATA(adapt);
	u8 thermal_val = 0, delta, delta_lck, delta_iqk, offset;
	u8 thermal_avg_count = 0;
	u32 thermal_avg = 0;
	s32 ele_a = 0, ele_d, temp_cck, x, value32;
	s32 y, ele_c = 0;
	s8 ofdm_index[2], cck_index = 0;
	s8 ofdm_index_old[2] = {0, 0}, cck_index_old = 0;
	u32 i = 0, j = 0;
	bool is2t = false;

	u8 ofdm_min_index = 6, rf; /* OFDM BB Swing should be less than +3.0dB */
	u8 indexforchannel = 0;
	s8 ofdm_index_mapping[2][index_mapping_NUM_88E] = {
		/* 2.4G, decrease power */
		{0, 0, 2, 3, 4, 4, 5, 6, 7, 7, 8, 9, 10, 10, 11},
		/* 2.4G, increase power */
		{0, 0, -1, -2, -3, -4,-4, -4, -4, -5, -7, -8,-9, -9, -10},
	};
	u8 thermal_mapping[2][index_mapping_NUM_88E] = {
		/* 2.4G, decrease power */
		{0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 27},
		/* 2.4G, increase power */
		{0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 25, 25, 25},
	};
	struct odm_dm_struct *dm_odm = &hal_data->odmpriv;

	dm_txpwr_track_setpwr(dm_odm);

	dm_odm->RFCalibrateInfo.TXPowerTrackingCallbackCnt++;
	dm_odm->RFCalibrateInfo.bTXPowerTrackingInit = true;

	dm_odm->RFCalibrateInfo.RegA24 = 0x090e1317;

	thermal_val = (u8)phy_query_rf_reg(adapt, RF_PATH_A,
					   RF_T_METER_88E, 0xfc00);

	if (is2t)
		rf = 2;
	else
		rf = 1;

	if (thermal_val) {
		/* Query OFDM path A default setting */
		ele_d = phy_query_bb_reg(adapt, rOFDM0_XATxIQImbalance, bMaskDWord)&bMaskOFDM_D;
		for (i = 0; i < OFDM_TABLE_SIZE_92D; i++) {
			if (ele_d == (OFDMSwingTable[i]&bMaskOFDM_D)) {
				ofdm_index_old[0] = (u8)i;
				dm_odm->BbSwingIdxOfdmBase = (u8)i;
				break;
			}
		}

		/* Query OFDM path B default setting */
		if (is2t) {
			ele_d = phy_query_bb_reg(adapt, rOFDM0_XBTxIQImbalance, bMaskDWord)&bMaskOFDM_D;
			for (i = 0; i < OFDM_TABLE_SIZE_92D; i++) {
				if (ele_d == (OFDMSwingTable[i]&bMaskOFDM_D)) {
					ofdm_index_old[1] = (u8)i;
					break;
				}
			}
		}

		/* Query CCK default setting From 0xa24 */
		temp_cck = dm_odm->RFCalibrateInfo.RegA24;

		for (i = 0; i < CCK_TABLE_SIZE; i++) {
			if (dm_odm->RFCalibrateInfo.bCCKinCH14) {
				if (memcmp(&temp_cck, &CCKSwingTable_Ch14[i][2], 4)) {
					cck_index_old = (u8)i;
					dm_odm->BbSwingIdxCckBase = (u8)i;
					break;
				}
			} else {
				if (memcmp(&temp_cck, &CCKSwingTable_Ch1_Ch13[i][2], 4)) {
					cck_index_old = (u8)i;
					dm_odm->BbSwingIdxCckBase = (u8)i;
					break;
				}
			}
		}

		if (!dm_odm->RFCalibrateInfo.ThermalValue) {
			dm_odm->RFCalibrateInfo.ThermalValue = hal_data->EEPROMThermalMeter;
			dm_odm->RFCalibrateInfo.ThermalValue_LCK = thermal_val;
			dm_odm->RFCalibrateInfo.ThermalValue_IQK = thermal_val;

			for (i = 0; i < rf; i++)
				dm_odm->RFCalibrateInfo.OFDM_index[i] = ofdm_index_old[i];
			dm_odm->RFCalibrateInfo.CCK_index = cck_index_old;
		}

		/* calculate average thermal meter */
		dm_odm->RFCalibrateInfo.ThermalValue_AVG[dm_odm->RFCalibrateInfo.ThermalValue_AVG_index] = thermal_val;
		dm_odm->RFCalibrateInfo.ThermalValue_AVG_index++;
		if (dm_odm->RFCalibrateInfo.ThermalValue_AVG_index == AVG_THERMAL_NUM_88E)
			dm_odm->RFCalibrateInfo.ThermalValue_AVG_index = 0;

		for (i = 0; i < AVG_THERMAL_NUM_88E; i++) {
			if (dm_odm->RFCalibrateInfo.ThermalValue_AVG[i]) {
				thermal_avg += dm_odm->RFCalibrateInfo.ThermalValue_AVG[i];
				thermal_avg_count++;
			}
		}

		if (thermal_avg_count)
			thermal_val = (u8)(thermal_avg / thermal_avg_count);

		if (dm_odm->RFCalibrateInfo.bReloadtxpowerindex) {
			delta = thermal_val > hal_data->EEPROMThermalMeter ?
				(thermal_val - hal_data->EEPROMThermalMeter) :
				(hal_data->EEPROMThermalMeter - thermal_val);
			dm_odm->RFCalibrateInfo.bReloadtxpowerindex = false;
			dm_odm->RFCalibrateInfo.bDoneTxpower = false;
		} else if (dm_odm->RFCalibrateInfo.bDoneTxpower) {
			delta = (thermal_val > dm_odm->RFCalibrateInfo.ThermalValue) ?
				(thermal_val - dm_odm->RFCalibrateInfo.ThermalValue) :
				(dm_odm->RFCalibrateInfo.ThermalValue - thermal_val);
		} else {
			delta = thermal_val > hal_data->EEPROMThermalMeter ?
				(thermal_val - hal_data->EEPROMThermalMeter) :
				(hal_data->EEPROMThermalMeter - thermal_val);
		}
		delta_lck = (thermal_val > dm_odm->RFCalibrateInfo.ThermalValue_LCK) ?
			    (thermal_val - dm_odm->RFCalibrateInfo.ThermalValue_LCK) :
			    (dm_odm->RFCalibrateInfo.ThermalValue_LCK - thermal_val);
		delta_iqk = (thermal_val > dm_odm->RFCalibrateInfo.ThermalValue_IQK) ?
			    (thermal_val - dm_odm->RFCalibrateInfo.ThermalValue_IQK) :
			    (dm_odm->RFCalibrateInfo.ThermalValue_IQK - thermal_val);

		/* Delta temperature is equal to or larger than 20 centigrade.*/
		if ((delta_lck >= 8)) {
			dm_odm->RFCalibrateInfo.ThermalValue_LCK = thermal_val;
			PHY_LCCalibrate_8188E(adapt);
		}

		if (delta > 0 && dm_odm->RFCalibrateInfo.TxPowerTrackControl) {
			delta = thermal_val > hal_data->EEPROMThermalMeter ?
				(thermal_val - hal_data->EEPROMThermalMeter) :
				(hal_data->EEPROMThermalMeter - thermal_val);
			/* calculate new OFDM / CCK offset */
			if (thermal_val > hal_data->EEPROMThermalMeter)
				j = 1;
			else
				j = 0;
			for (offset = 0; offset < index_mapping_NUM_88E; offset++) {
				if (delta < thermal_mapping[j][offset]) {
					if (offset != 0)
						offset--;
					break;
				}
			}
			if (offset >= index_mapping_NUM_88E)
				offset = index_mapping_NUM_88E-1;
			for (i = 0; i < rf; i++)
				ofdm_index[i] = dm_odm->RFCalibrateInfo.OFDM_index[i] + ofdm_index_mapping[j][offset];
			cck_index = dm_odm->RFCalibrateInfo.CCK_index + ofdm_index_mapping[j][offset];

			for (i = 0; i < rf; i++) {
				if (ofdm_index[i] > OFDM_TABLE_SIZE_92D-1)
					ofdm_index[i] = OFDM_TABLE_SIZE_92D-1;
				else if (ofdm_index[i] < ofdm_min_index)
					ofdm_index[i] = ofdm_min_index;
			}

			if (cck_index > CCK_TABLE_SIZE-1)
				cck_index = CCK_TABLE_SIZE-1;
			else if (cck_index < 0)
				cck_index = 0;

			/* 2 temporarily remove bNOPG */
			/* Config by SwingTable */
			if (dm_odm->RFCalibrateInfo.TxPowerTrackControl) {
				dm_odm->RFCalibrateInfo.bDoneTxpower = true;

				/* Adujst OFDM Ant_A according to IQK result */
				ele_d = (OFDMSwingTable[(u8)ofdm_index[0]] & 0xFFC00000)>>22;
				x = dm_odm->RFCalibrateInfo.IQKMatrixRegSetting[indexforchannel].Value[0][0];
				y = dm_odm->RFCalibrateInfo.IQKMatrixRegSetting[indexforchannel].Value[0][1];

				/*  Revse TX power table. */
				dm_odm->BbSwingIdxOfdm = (u8)ofdm_index[0];
				dm_odm->BbSwingIdxCck = (u8)cck_index;

				if (dm_odm->BbSwingIdxOfdmCurrent != dm_odm->BbSwingIdxOfdm) {
					dm_odm->BbSwingIdxOfdmCurrent = dm_odm->BbSwingIdxOfdm;
					dm_odm->BbSwingFlagOfdm = true;
				}

				if (dm_odm->BbSwingIdxCckCurrent != dm_odm->BbSwingIdxCck) {
					dm_odm->BbSwingIdxCckCurrent = dm_odm->BbSwingIdxCck;
					dm_odm->BbSwingFlagCck = true;
				}

				if (x != 0) {
					if ((x & 0x00000200) != 0)
						x = x | 0xFFFFFC00;
					ele_a = ((x * ele_d)>>8)&0x000003FF;

					/* new element C = element D x Y */
					if ((y & 0x00000200) != 0)
						y = y | 0xFFFFFC00;
					ele_c = ((y * ele_d)>>8)&0x000003FF;

				}

				if (is2t) {
					ele_d = (OFDMSwingTable[(u8)ofdm_index[1]] & 0xFFC00000)>>22;

					/* new element A = element D x X */
					x = dm_odm->RFCalibrateInfo.IQKMatrixRegSetting[indexforchannel].Value[0][4];
					y = dm_odm->RFCalibrateInfo.IQKMatrixRegSetting[indexforchannel].Value[0][5];

					if ((x != 0) && (*(dm_odm->pBandType) == ODM_BAND_2_4G)) {
						if ((x & 0x00000200) != 0)	/* consider minus */
							x = x | 0xFFFFFC00;
						ele_a = ((x * ele_d)>>8)&0x000003FF;

						/* new element C = element D x Y */
						if ((y & 0x00000200) != 0)
							y = y | 0xFFFFFC00;
						ele_c = ((y * ele_d)>>8)&0x00003FF;

						/* wtite new elements A, C, D to regC88 and regC9C, element B is always 0 */
						value32 = (ele_d<<22) | ((ele_c&0x3F)<<16) | ele_a;
						phy_set_bb_reg(adapt, rOFDM0_XBTxIQImbalance, bMaskDWord, value32);

						value32 = (ele_c&0x000003C0)>>6;
						phy_set_bb_reg(adapt, rOFDM0_XDTxAFE, bMaskH4Bits, value32);

						value32 = ((x * ele_d)>>7)&0x01;
						phy_set_bb_reg(adapt, rOFDM0_ECCAThreshold, BIT28, value32);
					} else {
						phy_set_bb_reg(adapt, rOFDM0_XBTxIQImbalance, bMaskDWord, OFDMSwingTable[(u8)ofdm_index[1]]);
						phy_set_bb_reg(adapt, rOFDM0_XDTxAFE, bMaskH4Bits, 0x00);
						phy_set_bb_reg(adapt, rOFDM0_ECCAThreshold, BIT28, 0x00);
					}

				}

			}
		}

		/* Delta temperature is equal to or larger than 20 centigrade.*/
		if (delta_iqk >= 8) {
			dm_odm->RFCalibrateInfo.ThermalValue_IQK = thermal_val;
			PHY_IQCalibrate_8188E(adapt, false);
		}
		/* update thermal meter value */
		if (dm_odm->RFCalibrateInfo.TxPowerTrackControl)
			dm_odm->RFCalibrateInfo.ThermalValue = thermal_val;
	}
	dm_odm->RFCalibrateInfo.TXPowercount = 0;
}

#define MAX_TOLERANCE 5

static u8 phy_path_a_iqk(struct adapter *adapt, bool config_pathb)
{
	u32 reg_eac, reg_e94, reg_e9c, reg_ea4;
	u8 result = 0x00;

	/* 1 Tx IQK */
	/* path-A IQK setting */
	phy_set_bb_reg(adapt, rTx_IQK_Tone_A, bMaskDWord, 0x10008c1c);
	phy_set_bb_reg(adapt, rRx_IQK_Tone_A, bMaskDWord, 0x30008c1c);
	phy_set_bb_reg(adapt, rTx_IQK_PI_A, bMaskDWord, 0x8214032a);
	phy_set_bb_reg(adapt, rRx_IQK_PI_A, bMaskDWord, 0x28160000);

	/* LO calibration setting */
	phy_set_bb_reg(adapt, rIQK_AGC_Rsp, bMaskDWord, 0x00462911);

	/* One shot, path A LOK & IQK */
	phy_set_bb_reg(adapt, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	phy_set_bb_reg(adapt, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);

	mdelay(IQK_DELAY_TIME_88E);

	reg_eac = phy_query_bb_reg(adapt, rRx_Power_After_IQK_A_2, bMaskDWord);
	reg_e94 = phy_query_bb_reg(adapt, rTx_Power_Before_IQK_A, bMaskDWord);
	reg_e9c = phy_query_bb_reg(adapt, rTx_Power_After_IQK_A, bMaskDWord);
	reg_ea4 = phy_query_bb_reg(adapt, rRx_Power_Before_IQK_A_2, bMaskDWord);

	if (!(reg_eac & BIT28) &&
	    (((reg_e94 & 0x03FF0000)>>16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000)>>16) != 0x42))
		result |= 0x01;
	return result;
}

static u8 phy_path_a_rx_iqk(struct adapter *adapt, bool configPathB)
{
	u32 reg_eac, reg_e94, reg_e9c, reg_ea4, u4tmp;
	u8 result = 0x00;
	struct hal_data_8188e *hal_data = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &hal_data->odmpriv;

	/* 1 Get TXIMR setting */
	/* modify RXIQK mode table */
	phy_set_bb_reg(adapt, rFPGA0_IQK, bMaskDWord, 0x00000000);
	phy_set_rf_reg(adapt, RF_PATH_A, RF_WE_LUT, bRFRegOffsetMask, 0x800a0);
	phy_set_rf_reg(adapt, RF_PATH_A, RF_RCK_OS, bRFRegOffsetMask, 0x30000);
	phy_set_rf_reg(adapt, RF_PATH_A, RF_TXPA_G1, bRFRegOffsetMask, 0x0000f);
	phy_set_rf_reg(adapt, RF_PATH_A, RF_TXPA_G2, bRFRegOffsetMask, 0xf117B);

	/* PA,PAD off */
	phy_set_rf_reg(adapt, RF_PATH_A, 0xdf, bRFRegOffsetMask, 0x980);
	phy_set_rf_reg(adapt, RF_PATH_A, 0x56, bRFRegOffsetMask, 0x51000);

	phy_set_bb_reg(adapt, rFPGA0_IQK, bMaskDWord, 0x80800000);

	/* IQK setting */
	phy_set_bb_reg(adapt, rTx_IQK, bMaskDWord, 0x01007c00);
	phy_set_bb_reg(adapt, rRx_IQK, bMaskDWord, 0x81004800);

	/* path-A IQK setting */
	phy_set_bb_reg(adapt, rTx_IQK_Tone_A, bMaskDWord, 0x10008c1c);
	phy_set_bb_reg(adapt, rRx_IQK_Tone_A, bMaskDWord, 0x30008c1c);
	phy_set_bb_reg(adapt, rTx_IQK_PI_A, bMaskDWord, 0x82160c1f);
	phy_set_bb_reg(adapt, rRx_IQK_PI_A, bMaskDWord, 0x28160000);

	/* LO calibration setting */
	phy_set_bb_reg(adapt, rIQK_AGC_Rsp, bMaskDWord, 0x0046a911);

	/* One shot, path A LOK & IQK */
	phy_set_bb_reg(adapt, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	phy_set_bb_reg(adapt, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);

	/* delay x ms */
	mdelay(IQK_DELAY_TIME_88E);

	/* Check failed */
	reg_eac = phy_query_bb_reg(adapt, rRx_Power_After_IQK_A_2, bMaskDWord);
	reg_e94 = phy_query_bb_reg(adapt, rTx_Power_Before_IQK_A, bMaskDWord);
	reg_e9c = phy_query_bb_reg(adapt, rTx_Power_After_IQK_A, bMaskDWord);

	if (!(reg_eac & BIT28) &&
	    (((reg_e94 & 0x03FF0000)>>16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000)>>16) != 0x42))
		result |= 0x01;
	else					/* if Tx not OK, ignore Rx */
		return result;

	u4tmp = 0x80007C00 | (reg_e94&0x3FF0000)  | ((reg_e9c&0x3FF0000) >> 16);
	phy_set_bb_reg(adapt, rTx_IQK, bMaskDWord, u4tmp);

	/* 1 RX IQK */
	/* modify RXIQK mode table */
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
		     ("Path-A Rx IQK modify RXIQK mode table 2!\n"));
	phy_set_bb_reg(adapt, rFPGA0_IQK, bMaskDWord, 0x00000000);
	phy_set_rf_reg(adapt, RF_PATH_A, RF_WE_LUT, bRFRegOffsetMask, 0x800a0);
	phy_set_rf_reg(adapt, RF_PATH_A, RF_RCK_OS, bRFRegOffsetMask, 0x30000);
	phy_set_rf_reg(adapt, RF_PATH_A, RF_TXPA_G1, bRFRegOffsetMask, 0x0000f);
	phy_set_rf_reg(adapt, RF_PATH_A, RF_TXPA_G2, bRFRegOffsetMask, 0xf7ffa);
	phy_set_bb_reg(adapt, rFPGA0_IQK, bMaskDWord, 0x80800000);

	/* IQK setting */
	phy_set_bb_reg(adapt, rRx_IQK, bMaskDWord, 0x01004800);

	/* path-A IQK setting */
	phy_set_bb_reg(adapt, rTx_IQK_Tone_A, bMaskDWord, 0x38008c1c);
	phy_set_bb_reg(adapt, rRx_IQK_Tone_A, bMaskDWord, 0x18008c1c);
	phy_set_bb_reg(adapt, rTx_IQK_PI_A, bMaskDWord, 0x82160c05);
	phy_set_bb_reg(adapt, rRx_IQK_PI_A, bMaskDWord, 0x28160c1f);

	/* LO calibration setting */
	phy_set_bb_reg(adapt, rIQK_AGC_Rsp, bMaskDWord, 0x0046a911);

	phy_set_bb_reg(adapt, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	phy_set_bb_reg(adapt, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);

	mdelay(IQK_DELAY_TIME_88E);

	/*  Check failed */
	reg_eac = phy_query_bb_reg(adapt, rRx_Power_After_IQK_A_2, bMaskDWord);
	reg_e94 = phy_query_bb_reg(adapt, rTx_Power_Before_IQK_A, bMaskDWord);
	reg_e9c = phy_query_bb_reg(adapt, rTx_Power_After_IQK_A, bMaskDWord);
	reg_ea4 = phy_query_bb_reg(adapt, rRx_Power_Before_IQK_A_2, bMaskDWord);

	/* reload RF 0xdf */
	phy_set_bb_reg(adapt, rFPGA0_IQK, bMaskDWord, 0x00000000);
	phy_set_rf_reg(adapt, RF_PATH_A, 0xdf, bRFRegOffsetMask, 0x180);

	if (!(reg_eac & BIT27) && /* if Tx is OK, check whether Rx is OK */
	    (((reg_ea4 & 0x03FF0000)>>16) != 0x132) &&
	    (((reg_eac & 0x03FF0000)>>16) != 0x36))
		result |= 0x02;
	else
		ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
			     ("Path A Rx IQK fail!!\n"));

	return result;
}

static u8 phy_path_b_iqk(struct adapter *adapt)
{
	u32 regeac, regeb4, regebc, regec4, regecc;
	u8 result = 0x00;
	struct hal_data_8188e *hal_data = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &hal_data->odmpriv;

	/* One shot, path B LOK & IQK */
	phy_set_bb_reg(adapt, rIQK_AGC_Cont, bMaskDWord, 0x00000002);
	phy_set_bb_reg(adapt, rIQK_AGC_Cont, bMaskDWord, 0x00000000);

	mdelay(IQK_DELAY_TIME_88E);

	regeac = phy_query_bb_reg(adapt, rRx_Power_After_IQK_A_2, bMaskDWord);
	regeb4 = phy_query_bb_reg(adapt, rTx_Power_Before_IQK_B, bMaskDWord);
	regebc = phy_query_bb_reg(adapt, rTx_Power_After_IQK_B, bMaskDWord);
	regec4 = phy_query_bb_reg(adapt, rRx_Power_Before_IQK_B_2, bMaskDWord);
	regecc = phy_query_bb_reg(adapt, rRx_Power_After_IQK_B_2, bMaskDWord);

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
		ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION,
			     ODM_DBG_LOUD,  ("Path B Rx IQK fail!!\n"));
	return result;
}

static void patha_fill_iqk(struct adapter *adapt, bool iqkok, s32 result[][8],
			   u8 final_candidate, bool txonly)
{
	u32 oldval_0, x, tx0_a, reg;
	s32 y, tx0_c;

	if (final_candidate == 0xFF) {
		return;
	} else if (iqkok) {
		oldval_0 = (phy_query_bb_reg(adapt, rOFDM0_XATxIQImbalance, bMaskDWord) >> 22) & 0x3FF;

		x = result[final_candidate][0];
		if ((x & 0x00000200) != 0)
			x = x | 0xFFFFFC00;

		tx0_a = (x * oldval_0) >> 8;
		phy_set_bb_reg(adapt, rOFDM0_XATxIQImbalance, 0x3FF, tx0_a);
		phy_set_bb_reg(adapt, rOFDM0_ECCAThreshold, BIT(31),
			       ((x * oldval_0>>7) & 0x1));

		y = result[final_candidate][1];
		if ((y & 0x00000200) != 0)
			y = y | 0xFFFFFC00;

		tx0_c = (y * oldval_0) >> 8;
		phy_set_bb_reg(adapt, rOFDM0_XCTxAFE, 0xF0000000,
			       ((tx0_c&0x3C0)>>6));
		phy_set_bb_reg(adapt, rOFDM0_XATxIQImbalance, 0x003F0000,
			       (tx0_c&0x3F));
		phy_set_bb_reg(adapt, rOFDM0_ECCAThreshold, BIT(29),
			       ((y * oldval_0>>7) & 0x1));

		if (txonly)
			return;

		reg = result[final_candidate][2];
		phy_set_bb_reg(adapt, rOFDM0_XARxIQImbalance, 0x3FF, reg);

		reg = result[final_candidate][3] & 0x3F;
		phy_set_bb_reg(adapt, rOFDM0_XARxIQImbalance, 0xFC00, reg);

		reg = (result[final_candidate][3] >> 6) & 0xF;
		phy_set_bb_reg(adapt, rOFDM0_RxIQExtAnta, 0xF0000000, reg);
	}
}

static void pathb_fill_iqk(struct adapter *adapt, bool iqkok, s32 result[][8],
			   u8 final_candidate, bool txonly)
{
	u32 oldval_1, x, tx1_a, reg;
	s32 y, tx1_c;

	if (final_candidate == 0xFF) {
		return;
	} else if (iqkok) {
		oldval_1 = (phy_query_bb_reg(adapt, rOFDM0_XBTxIQImbalance, bMaskDWord) >> 22) & 0x3FF;

		x = result[final_candidate][4];
		if ((x & 0x00000200) != 0)
			x = x | 0xFFFFFC00;
		tx1_a = (x * oldval_1) >> 8;
		phy_set_bb_reg(adapt, rOFDM0_XBTxIQImbalance, 0x3FF, tx1_a);

		phy_set_bb_reg(adapt, rOFDM0_ECCAThreshold, BIT(27),
			       ((x * oldval_1>>7) & 0x1));

		y = result[final_candidate][5];
		if ((y & 0x00000200) != 0)
			y = y | 0xFFFFFC00;

		tx1_c = (y * oldval_1) >> 8;

		phy_set_bb_reg(adapt, rOFDM0_XDTxAFE, 0xF0000000,
			       ((tx1_c&0x3C0)>>6));
		phy_set_bb_reg(adapt, rOFDM0_XBTxIQImbalance, 0x003F0000,
			       (tx1_c&0x3F));
		phy_set_bb_reg(adapt, rOFDM0_ECCAThreshold, BIT(25),
			       ((y * oldval_1>>7) & 0x1));

		if (txonly)
			return;

		reg = result[final_candidate][6];
		phy_set_bb_reg(adapt, rOFDM0_XBRxIQImbalance, 0x3FF, reg);

		reg = result[final_candidate][7] & 0x3F;
		phy_set_bb_reg(adapt, rOFDM0_XBRxIQImbalance, 0xFC00, reg);

		reg = (result[final_candidate][7] >> 6) & 0xF;
		phy_set_bb_reg(adapt, rOFDM0_AGCRSSITable, 0x0000F000, reg);
	}
}

static void save_adda_registers(struct adapter *adapt, u32 *addareg,
				u32 *backup, u32 register_num)
{
	u32 i;

	for (i = 0; i < register_num; i++) {
		backup[i] = phy_query_bb_reg(adapt, addareg[i], bMaskDWord);
	}
}

static void save_mac_registers(struct adapter *adapt, u32 *mac_reg,
			       u32 *backup)
{
	u32 i;

	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++) {
		backup[i] = usb_read8(adapt, mac_reg[i]);
	}
	backup[i] = usb_read32(adapt, mac_reg[i]);
}

static void reload_adda_reg(struct adapter *adapt, u32 *adda_reg,
			    u32 *backup, u32 regiester_num)
{
	u32 i;

	for (i = 0; i < regiester_num; i++)
		phy_set_bb_reg(adapt, adda_reg[i], bMaskDWord, backup[i]);
}

static void reload_mac_registers(struct adapter *adapt,
				 u32 *mac_reg, u32 *backup)
{
	u32 i;

	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++) {
		usb_write8(adapt, mac_reg[i], (u8)backup[i]);
	}
	usb_write32(adapt, mac_reg[i], backup[i]);
}

static void path_adda_on(struct adapter *adapt, u32 *adda_reg,
			 bool is_path_a_on, bool is2t)
{
	u32 path_on;
	u32 i;

	path_on = is_path_a_on ? 0x04db25a4 : 0x0b1b25a4;
	if (!is2t) {
		path_on = 0x0bdb25a0;
		phy_set_bb_reg(adapt, adda_reg[0], bMaskDWord, 0x0b1b25a0);
	} else {
		phy_set_bb_reg(adapt, adda_reg[0], bMaskDWord, path_on);
	}

	for (i = 1; i < IQK_ADDA_REG_NUM; i++)
		phy_set_bb_reg(adapt, adda_reg[i], bMaskDWord, path_on);
}

static void mac_setting_calibration(struct adapter *adapt, u32 *mac_reg, u32 *backup)
{
	u32 i = 0;

	usb_write8(adapt, mac_reg[i], 0x3F);

	for (i = 1; i < (IQK_MAC_REG_NUM - 1); i++) {
		usb_write8(adapt, mac_reg[i], (u8)(backup[i]&(~BIT3)));
	}
	usb_write8(adapt, mac_reg[i], (u8)(backup[i]&(~BIT5)));
}

static void path_a_standby(struct adapter *adapt)
{

	phy_set_bb_reg(adapt, rFPGA0_IQK, bMaskDWord, 0x0);
	phy_set_bb_reg(adapt, 0x840, bMaskDWord, 0x00010000);
	phy_set_bb_reg(adapt, rFPGA0_IQK, bMaskDWord, 0x80800000);
}

static void pi_mode_switch(struct adapter *adapt, bool pi_mode)
{
	u32 mode;

	mode = pi_mode ? 0x01000100 : 0x01000000;
	phy_set_bb_reg(adapt, rFPGA0_XA_HSSIParameter1, bMaskDWord, mode);
	phy_set_bb_reg(adapt, rFPGA0_XB_HSSIParameter1, bMaskDWord, mode);
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
		save_adda_registers(adapt, ADDA_REG, dm_odm->RFCalibrateInfo.ADDA_backup, IQK_ADDA_REG_NUM);
		save_mac_registers(adapt, IQK_MAC_REG, dm_odm->RFCalibrateInfo.IQK_MAC_backup);
		save_adda_registers(adapt, IQK_BB_REG_92C, dm_odm->RFCalibrateInfo.IQK_BB_backup, IQK_BB_REG_NUM);
	}
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQ Calibration for %s for %d times\n", (is2t ? "2T2R" : "1T1R"), t));

	path_adda_on(adapt, ADDA_REG, true, is2t);
	if (t == 0)
		dm_odm->RFCalibrateInfo.bRfPiEnable = (u8)phy_query_bb_reg(adapt, rFPGA0_XA_HSSIParameter1, BIT(8));

	if (!dm_odm->RFCalibrateInfo.bRfPiEnable) {
		/*  Switch BB to PI mode to do IQ Calibration. */
		pi_mode_switch(adapt, true);
	}

	/* BB setting */
	phy_set_bb_reg(adapt, rFPGA0_RFMOD, BIT24, 0x00);
	phy_set_bb_reg(adapt, rOFDM0_TRxPathEnable, bMaskDWord, 0x03a05600);
	phy_set_bb_reg(adapt, rOFDM0_TRMuxPar, bMaskDWord, 0x000800e4);
	phy_set_bb_reg(adapt, rFPGA0_XCD_RFInterfaceSW, bMaskDWord, 0x22204000);

	phy_set_bb_reg(adapt, rFPGA0_XAB_RFInterfaceSW, BIT10, 0x01);
	phy_set_bb_reg(adapt, rFPGA0_XAB_RFInterfaceSW, BIT26, 0x01);
	phy_set_bb_reg(adapt, rFPGA0_XA_RFInterfaceOE, BIT10, 0x00);
	phy_set_bb_reg(adapt, rFPGA0_XB_RFInterfaceOE, BIT10, 0x00);

	if (is2t) {
		phy_set_bb_reg(adapt, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x00010000);
		phy_set_bb_reg(adapt, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x00010000);
	}

	/* MAC settings */
	mac_setting_calibration(adapt, IQK_MAC_REG, dm_odm->RFCalibrateInfo.IQK_MAC_backup);

	/* Page B init */
	/* AP or IQK */
	phy_set_bb_reg(adapt, rConfig_AntA, bMaskDWord, 0x0f600000);

	if (is2t)
		phy_set_bb_reg(adapt, rConfig_AntB, bMaskDWord, 0x0f600000);

	/*  IQ calibration setting */
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK setting!\n"));
	phy_set_bb_reg(adapt, rFPGA0_IQK, bMaskDWord, 0x80800000);
	phy_set_bb_reg(adapt, rTx_IQK, bMaskDWord, 0x01007c00);
	phy_set_bb_reg(adapt, rRx_IQK, bMaskDWord, 0x81004800);

	for (i = 0; i < retryCount; i++) {
		PathAOK = phy_path_a_iqk(adapt, is2t);
		if (PathAOK == 0x01) {
			ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path A Tx IQK Success!!\n"));
				result[t][0] = (phy_query_bb_reg(adapt, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
				result[t][1] = (phy_query_bb_reg(adapt, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;
			break;
		}
	}

	for (i = 0; i < retryCount; i++) {
		PathAOK = phy_path_a_rx_iqk(adapt, is2t);
		if (PathAOK == 0x03) {
			ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Path A Rx IQK Success!!\n"));
				result[t][2] = (phy_query_bb_reg(adapt, rRx_Power_Before_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
				result[t][3] = (phy_query_bb_reg(adapt, rRx_Power_After_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
			break;
		} else {
			ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path A Rx IQK Fail!!\n"));
		}
	}

	if (0x00 == PathAOK) {
		ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path A IQK failed!!\n"));
	}

	if (is2t) {
		path_a_standby(adapt);

		/*  Turn Path B ADDA on */
		path_adda_on(adapt, ADDA_REG, false, is2t);

		for (i = 0; i < retryCount; i++) {
			PathBOK = phy_path_b_iqk(adapt);
			if (PathBOK == 0x03) {
				ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path B IQK Success!!\n"));
				result[t][4] = (phy_query_bb_reg(adapt, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][5] = (phy_query_bb_reg(adapt, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][6] = (phy_query_bb_reg(adapt, rRx_Power_Before_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
				result[t][7] = (phy_query_bb_reg(adapt, rRx_Power_After_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
				break;
			} else if (i == (retryCount - 1) && PathBOK == 0x01) {	/* Tx IQK OK */
				ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path B Only Tx IQK Success!!\n"));
				result[t][4] = (phy_query_bb_reg(adapt, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][5] = (phy_query_bb_reg(adapt, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;
			}
		}

		if (0x00 == PathBOK) {
			ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Path B IQK failed!!\n"));
		}
	}

	/* Back to BB mode, load original value */
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK:Back to BB mode, load original value!\n"));
	phy_set_bb_reg(adapt, rFPGA0_IQK, bMaskDWord, 0);

	if (t != 0) {
		if (!dm_odm->RFCalibrateInfo.bRfPiEnable) {
			/*  Switch back BB to SI mode after finish IQ Calibration. */
			pi_mode_switch(adapt, false);
		}

		/*  Reload ADDA power saving parameters */
		reload_adda_reg(adapt, ADDA_REG, dm_odm->RFCalibrateInfo.ADDA_backup, IQK_ADDA_REG_NUM);

		/*  Reload MAC parameters */
		reload_mac_registers(adapt, IQK_MAC_REG, dm_odm->RFCalibrateInfo.IQK_MAC_backup);

		reload_adda_reg(adapt, IQK_BB_REG_92C, dm_odm->RFCalibrateInfo.IQK_BB_backup, IQK_BB_REG_NUM);

		/*  Restore RX initial gain */
		phy_set_bb_reg(adapt, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x00032ed3);
		if (is2t)
			phy_set_bb_reg(adapt, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x00032ed3);

		/* load 0xe30 IQC default value */
		phy_set_bb_reg(adapt, rTx_IQK_Tone_A, bMaskDWord, 0x01008c00);
		phy_set_bb_reg(adapt, rRx_IQK_Tone_A, bMaskDWord, 0x01008c00);
	}
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("phy_IQCalibrate_8188E() <==\n"));
}

static void phy_LCCalibrate_8188E(struct adapter *adapt, bool is2t)
{
	u8 tmpreg;
	u32 RF_Amode = 0, RF_Bmode = 0, LC_Cal;

	/* Check continuous TX and Packet TX */
	tmpreg = usb_read8(adapt, 0xd03);

	if ((tmpreg&0x70) != 0)			/* Deal with contisuous TX case */
		usb_write8(adapt, 0xd03, tmpreg&0x8F);	/* disable all continuous TX */
	else							/*  Deal with Packet TX case */
		usb_write8(adapt, REG_TXPAUSE, 0xFF);			/*  block all queues */

	if ((tmpreg&0x70) != 0) {
		/* 1. Read original RF mode */
		/* Path-A */
		RF_Amode = phy_query_rf_reg(adapt, RF_PATH_A, RF_AC, bMask12Bits);

		/* Path-B */
		if (is2t)
			RF_Bmode = phy_query_rf_reg(adapt, RF_PATH_B, RF_AC, bMask12Bits);

		/* 2. Set RF mode = standby mode */
		/* Path-A */
		phy_set_rf_reg(adapt, RF_PATH_A, RF_AC, bMask12Bits, (RF_Amode&0x8FFFF)|0x10000);

		/* Path-B */
		if (is2t)
			phy_set_rf_reg(adapt, RF_PATH_B, RF_AC, bMask12Bits, (RF_Bmode&0x8FFFF)|0x10000);
	}

	/* 3. Read RF reg18 */
	LC_Cal = phy_query_rf_reg(adapt, RF_PATH_A, RF_CHNLBW, bMask12Bits);

	/* 4. Set LC calibration begin	bit15 */
	phy_set_rf_reg(adapt, RF_PATH_A, RF_CHNLBW, bMask12Bits, LC_Cal|0x08000);

	msleep(100);

	/* Restore original situation */
	if ((tmpreg&0x70) != 0) {
		/* Deal with continuous TX case */
		/* Path-A */
		usb_write8(adapt, 0xd03, tmpreg);
		phy_set_rf_reg(adapt, RF_PATH_A, RF_AC, bMask12Bits, RF_Amode);

		/* Path-B */
		if (is2t)
			phy_set_rf_reg(adapt, RF_PATH_B, RF_AC, bMask12Bits, RF_Bmode);
	} else {
		/*  Deal with Packet TX case */
		usb_write8(adapt, REG_TXPAUSE, 0x00);
	}
}

void PHY_IQCalibrate_8188E(struct adapter *adapt, bool recovery)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;
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

	Indexforchannel = get_right_chnl_for_iqk(pHalData->CurrentChannel);

/* To Fix BSOD when final_candidate is 0xff */
/* by sherry 20120321 */
	if (final_candidate < 4) {
		for (i = 0; i < IQK_Matrix_REG_NUM; i++)
			dm_odm->RFCalibrateInfo.IQKMatrixRegSetting[Indexforchannel].Value[0][i] = result[final_candidate][i];
		dm_odm->RFCalibrateInfo.IQKMatrixRegSetting[Indexforchannel].bIQKDone = true;
	}
	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("\nIQK OK Indexforchannel %d.\n", Indexforchannel));

	save_adda_registers(adapt, IQK_BB_REG_92C, dm_odm->RFCalibrateInfo.IQK_BB_backup_recover, 9);

	ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("IQK finished\n"));
}

void PHY_LCCalibrate_8188E(struct adapter *adapt)
{
	bool singletone = false, carrier_sup = false;
	u32 timeout = 2000, timecount = 0;
	struct hal_data_8188e *pHalData = GET_HAL_DATA(adapt);
	struct odm_dm_struct *dm_odm = &pHalData->odmpriv;

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
		u1btmp = usb_read8(adapt, REG_LEDCFG2) | BIT7;
		usb_write8(adapt, REG_LEDCFG2, u1btmp);
		phy_set_bb_reg(adapt, rFPGA0_XAB_RFParameter, BIT13, 0x01);
	}

	if (is2t) {	/* 92C */
		if (main)
			phy_set_bb_reg(adapt, rFPGA0_XB_RFInterfaceOE, BIT5|BIT6, 0x1);	/* 92C_Path_A */
		else
			phy_set_bb_reg(adapt, rFPGA0_XB_RFInterfaceOE, BIT5|BIT6, 0x2);	/* BT */
	} else {			/* 88C */
		if (main)
			phy_set_bb_reg(adapt, rFPGA0_XA_RFInterfaceOE, BIT8|BIT9, 0x2);	/* Main */
		else
			phy_set_bb_reg(adapt, rFPGA0_XA_RFInterfaceOE, BIT8|BIT9, 0x1);	/* Aux */
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
