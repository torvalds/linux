// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _RTL8188E_PHYCFG_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <rtl8188e_hal.h>
#include <rf.h>
#include <phy.h>

#define MAX_PRECMD_CNT 16
#define MAX_RFDEPENDCMD_CNT 16
#define MAX_POSTCMD_CNT 16

#define MAX_DOZE_WAITING_TIMES_9x 64

static u32 cal_bit_shift(u32 bitmask)
{
	u32 i;

	for (i = 0; i <= 31; i++) {
		if (((bitmask >> i) & 0x1) == 1)
			break;
	}
	return i;
}

u32 phy_query_bb_reg(struct adapter *adapt, u32 regaddr, u32 bitmask)
{
	u32 original_value, bit_shift;

	original_value = usb_read32(adapt, regaddr);
	bit_shift = cal_bit_shift(bitmask);
	return (original_value & bitmask) >> bit_shift;
}

void phy_set_bb_reg(struct adapter *adapt, u32 regaddr, u32 bitmask, u32 data)
{
	u32 original_value, bit_shift;

	if (bitmask != bMaskDWord) { /* if not "double word" write */
		original_value = usb_read32(adapt, regaddr);
		bit_shift = cal_bit_shift(bitmask);
		data = (original_value & (~bitmask)) | (data << bit_shift);
	}

	usb_write32(adapt, regaddr, data);
}

static u32 rf_serial_read(struct adapter *adapt, enum rf_radio_path rfpath, u32 offset)
{
	u32 ret = 0;
	struct bb_reg_def *phyreg = &adapt->HalData->PHYRegDef[rfpath];
	u32 tmplong, tmplong2;
	u8 rfpi_enable = 0;

	offset &= 0xff;

	tmplong = phy_query_bb_reg(adapt, rFPGA0_XA_HSSIParameter2, bMaskDWord);
	if (rfpath == RF_PATH_A)
		tmplong2 = tmplong;
	else
		tmplong2 = phy_query_bb_reg(adapt, phyreg->rfHSSIPara2,
					    bMaskDWord);

	tmplong2 = (tmplong2 & (~bLSSIReadAddress)) |
		   (offset << 23) | bLSSIReadEdge;

	phy_set_bb_reg(adapt, rFPGA0_XA_HSSIParameter2, bMaskDWord,
		       tmplong & (~bLSSIReadEdge));
	udelay(10);

	phy_set_bb_reg(adapt, phyreg->rfHSSIPara2, bMaskDWord, tmplong2);
	udelay(100);

	udelay(10);

	if (rfpath == RF_PATH_A)
		rfpi_enable = (u8)phy_query_bb_reg(adapt, rFPGA0_XA_HSSIParameter1, BIT(8));
	else if (rfpath == RF_PATH_B)
		rfpi_enable = (u8)phy_query_bb_reg(adapt, rFPGA0_XB_HSSIParameter1, BIT(8));

	if (rfpi_enable)
		ret = phy_query_bb_reg(adapt, phyreg->rfLSSIReadBackPi,
				       bLSSIReadBackData);
	else
		ret = phy_query_bb_reg(adapt, phyreg->rfLSSIReadBack,
				       bLSSIReadBackData);
	return ret;
}

static void rf_serial_write(struct adapter *adapt,
			    enum rf_radio_path rfpath, u32 offset,
			    u32 data)
{
	u32 data_and_addr = 0;
	struct bb_reg_def *phyreg = &adapt->HalData->PHYRegDef[rfpath];

	offset &= 0xff;
	data_and_addr = ((offset << 20) | (data & 0x000fffff)) & 0x0fffffff;
	phy_set_bb_reg(adapt, phyreg->rf3wireOffset, bMaskDWord, data_and_addr);
}

u32 rtw_hal_read_rfreg(struct adapter *adapt, enum rf_radio_path rf_path,
		       u32 reg_addr, u32 bit_mask)
{
	u32 original_value, bit_shift;

	original_value = rf_serial_read(adapt, rf_path, reg_addr);
	bit_shift =  cal_bit_shift(bit_mask);
	return (original_value & bit_mask) >> bit_shift;
}

void phy_set_rf_reg(struct adapter *adapt, enum rf_radio_path rf_path,
		    u32 reg_addr, u32 bit_mask, u32 data)
{
	u32 original_value, bit_shift;

	/*  RF data is 12 bits only */
	if (bit_mask != bRFRegOffsetMask) {
		original_value = rf_serial_read(adapt, rf_path, reg_addr);
		bit_shift =  cal_bit_shift(bit_mask);
		data = (original_value & (~bit_mask)) | (data << bit_shift);
	}

	rf_serial_write(adapt, rf_path, reg_addr, data);
}

static void get_tx_power_index(struct adapter *adapt, u8 channel, u8 *cck_pwr,
			       u8 *ofdm_pwr, u8 *bw20_pwr, u8 *bw40_pwr)
{
	struct hal_data_8188e *hal_data = adapt->HalData;
	u8 index = (channel - 1);
	u8 TxCount = 0, path_nums;

	path_nums = 1;

	for (TxCount = 0; TxCount < path_nums; TxCount++) {
		if (TxCount == RF_PATH_A) {
			cck_pwr[TxCount] = hal_data->Index24G_CCK_Base[TxCount][index];
			ofdm_pwr[TxCount] = hal_data->Index24G_BW40_Base[RF_PATH_A][index] +
					    hal_data->OFDM_24G_Diff[TxCount][RF_PATH_A];

			bw20_pwr[TxCount] = hal_data->Index24G_BW40_Base[RF_PATH_A][index] +
					    hal_data->BW20_24G_Diff[TxCount][RF_PATH_A];
			bw40_pwr[TxCount] = hal_data->Index24G_BW40_Base[TxCount][index];
		} else if (TxCount == RF_PATH_B) {
			cck_pwr[TxCount] = hal_data->Index24G_CCK_Base[TxCount][index];
			ofdm_pwr[TxCount] = hal_data->Index24G_BW40_Base[RF_PATH_A][index] +
			hal_data->BW20_24G_Diff[RF_PATH_A][index] +
			hal_data->BW20_24G_Diff[TxCount][index];

			bw20_pwr[TxCount] = hal_data->Index24G_BW40_Base[RF_PATH_A][index] +
			hal_data->BW20_24G_Diff[TxCount][RF_PATH_A] +
			hal_data->BW20_24G_Diff[TxCount][index];
			bw40_pwr[TxCount] = hal_data->Index24G_BW40_Base[TxCount][index];
		}
	}
}

static void phy_power_index_check(struct adapter *adapt, u8 channel,
				  u8 *cck_pwr, u8 *ofdm_pwr, u8 *bw20_pwr,
				  u8 *bw40_pwr)
{
	struct hal_data_8188e *hal_data = adapt->HalData;

	hal_data->CurrentCckTxPwrIdx = cck_pwr[0];
	hal_data->CurrentOfdm24GTxPwrIdx = ofdm_pwr[0];
	hal_data->CurrentBW2024GTxPwrIdx = bw20_pwr[0];
	hal_data->CurrentBW4024GTxPwrIdx = bw40_pwr[0];
}

void phy_set_tx_power_level(struct adapter *adapt, u8 channel)
{
	u8 cck_pwr[MAX_TX_COUNT] = {0};
	u8 ofdm_pwr[MAX_TX_COUNT] = {0};/*  [0]:RF-A, [1]:RF-B */
	u8 bw20_pwr[MAX_TX_COUNT] = {0};
	u8 bw40_pwr[MAX_TX_COUNT] = {0};

	get_tx_power_index(adapt, channel, &cck_pwr[0], &ofdm_pwr[0],
			   &bw20_pwr[0], &bw40_pwr[0]);

	phy_power_index_check(adapt, channel, &cck_pwr[0], &ofdm_pwr[0],
			      &bw20_pwr[0], &bw40_pwr[0]);

	rtl88eu_phy_rf6052_set_cck_txpower(adapt, &cck_pwr[0]);
	rtl88eu_phy_rf6052_set_ofdm_txpower(adapt, &ofdm_pwr[0], &bw20_pwr[0],
					    &bw40_pwr[0], channel);
}

static void phy_set_bw_mode_callback(struct adapter *adapt)
{
	struct hal_data_8188e *hal_data = adapt->HalData;
	u8 reg_bw_opmode;
	u8 reg_prsr_rsc;

	if (adapt->bDriverStopped)
		return;

	/* Set MAC register */

	reg_bw_opmode = usb_read8(adapt, REG_BWOPMODE);
	reg_prsr_rsc = usb_read8(adapt, REG_RRSR + 2);

	switch (hal_data->CurrentChannelBW) {
	case HT_CHANNEL_WIDTH_20:
		reg_bw_opmode |= BW_OPMODE_20MHZ;
		usb_write8(adapt, REG_BWOPMODE, reg_bw_opmode);
		break;
	case HT_CHANNEL_WIDTH_40:
		reg_bw_opmode &= ~BW_OPMODE_20MHZ;
		usb_write8(adapt, REG_BWOPMODE, reg_bw_opmode);
		reg_prsr_rsc = (reg_prsr_rsc & 0x90) |
			       (hal_data->nCur40MhzPrimeSC << 5);
		usb_write8(adapt, REG_RRSR + 2, reg_prsr_rsc);
		break;
	default:
		break;
	}

	/* Set PHY related register */
	switch (hal_data->CurrentChannelBW) {
	case HT_CHANNEL_WIDTH_20:
		phy_set_bb_reg(adapt, rFPGA0_RFMOD, bRFMOD, 0x0);
		phy_set_bb_reg(adapt, rFPGA1_RFMOD, bRFMOD, 0x0);
		break;
	case HT_CHANNEL_WIDTH_40:
		phy_set_bb_reg(adapt, rFPGA0_RFMOD, bRFMOD, 0x1);
		phy_set_bb_reg(adapt, rFPGA1_RFMOD, bRFMOD, 0x1);
		/* Set Control channel to upper or lower.
		 * These settings are required only for 40MHz
		 */
		phy_set_bb_reg(adapt, rCCK0_System, bCCKSideBand,
			       (hal_data->nCur40MhzPrimeSC >> 1));
		phy_set_bb_reg(adapt, rOFDM1_LSTF, 0xC00,
			       hal_data->nCur40MhzPrimeSC);
		phy_set_bb_reg(adapt, 0x818, (BIT(26) | BIT(27)),
			       (hal_data->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) ? 2 : 1);
		break;
	default:
		break;
	}

	/* Set RF related register */
	rtl88eu_phy_rf6052_set_bandwidth(adapt, hal_data->CurrentChannelBW);
}

void rtw_hal_set_bwmode(struct adapter *adapt, enum ht_channel_width bandwidth,
			unsigned char offset)
{
	struct hal_data_8188e *hal_data = adapt->HalData;
	enum ht_channel_width tmp_bw = hal_data->CurrentChannelBW;

	hal_data->CurrentChannelBW = bandwidth;
	hal_data->nCur40MhzPrimeSC = offset;

	if ((!adapt->bDriverStopped) && (!adapt->bSurpriseRemoved))
		phy_set_bw_mode_callback(adapt);
	else
		hal_data->CurrentChannelBW = tmp_bw;
}

static void phy_sw_chnl_callback(struct adapter *adapt, u8 channel)
{
	u32 param1, param2;
	struct hal_data_8188e *hal_data = adapt->HalData;

	phy_set_tx_power_level(adapt, channel);

	param1 = RF_CHNLBW;
	param2 = channel;
	hal_data->RfRegChnlVal[0] = (hal_data->RfRegChnlVal[0] &
					  0xfffffc00) | param2;
	phy_set_rf_reg(adapt, 0, param1,
		       bRFRegOffsetMask, hal_data->RfRegChnlVal[0]);
}

void rtw_hal_set_chan(struct adapter *adapt, u8 channel)
{
	struct hal_data_8188e *hal_data = adapt->HalData;
	u8 tmpchannel = hal_data->CurrentChannel;

	if (channel == 0)
		channel = 1;

	hal_data->CurrentChannel = channel;

	if ((!adapt->bDriverStopped) && (!adapt->bSurpriseRemoved))
		phy_sw_chnl_callback(adapt, channel);
	else
		hal_data->CurrentChannel = tmpchannel;
}

#define ODM_TXPWRTRACK_MAX_IDX_88E  6

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
			pwr_value = dm_odm->BbSwingIdxOfdmBase -
				     dm_odm->BbSwingIdxOfdm;
		} else {
			*direction = 2;
			pwr_value = dm_odm->BbSwingIdxOfdm -
				     dm_odm->BbSwingIdxOfdmBase;
		}

	} else if (type == 1) { /* For CCK adjust. */
		ODM_RT_TRACE(dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			     ("dm_odm->BbSwingIdxCck = %d dm_odm->BbSwingIdxCckBase = %d\n",
			     dm_odm->BbSwingIdxCck, dm_odm->BbSwingIdxCckBase));

		if (dm_odm->BbSwingIdxCck <= dm_odm->BbSwingIdxCckBase) {
			*direction = 1;
			pwr_value = dm_odm->BbSwingIdxCckBase -
				     dm_odm->BbSwingIdxCck;
		} else {
			*direction = 2;
			pwr_value = dm_odm->BbSwingIdxCck -
				     dm_odm->BbSwingIdxCckBase;
		}
	}

	if (pwr_value >= ODM_TXPWRTRACK_MAX_IDX_88E && *direction == 1)
		pwr_value = ODM_TXPWRTRACK_MAX_IDX_88E;

	*out_write_val = pwr_value | (pwr_value << 8) | (pwr_value << 16) |
			 (pwr_value << 24);
}

static void dm_txpwr_track_setpwr(struct odm_dm_struct *dm_odm)
{
	if (dm_odm->BbSwingFlagOfdm || dm_odm->BbSwingFlagCck) {
		ODM_RT_TRACE(dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			     ("%s CH=%d\n", __func__, *dm_odm->pChannel));
		phy_set_tx_power_level(dm_odm->Adapter, *dm_odm->pChannel);
		dm_odm->BbSwingFlagOfdm = false;
		dm_odm->BbSwingFlagCck = false;
	}
}

void rtl88eu_dm_txpower_tracking_callback_thermalmeter(struct adapter *adapt)
{
	struct hal_data_8188e *hal_data = adapt->HalData;
	u8 thermal_val = 0, delta, delta_lck, delta_iqk, offset;
	u8 thermal_avg_count = 0;
	u32 thermal_avg = 0;
	s32 ele_d, temp_cck;
	s8 ofdm_index[2], cck_index = 0;
	s8 ofdm_index_old[2] = {0, 0}, cck_index_old = 0;
	u32 i = 0, j = 0;

	u8 ofdm_min_index = 6; /* OFDM BB Swing should be less than +3.0dB */
	s8 ofdm_index_mapping[2][index_mapping_NUM_88E] = {
		/* 2.4G, decrease power */
		{0, 0, 2, 3, 4, 4, 5, 6, 7, 7, 8, 9, 10, 10, 11},
		/* 2.4G, increase power */
		{0, 0, -1, -2, -3, -4, -4, -4, -4, -5, -7, -8, -9, -9, -10},
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

	dm_odm->RFCalibrateInfo.RegA24 = 0x090e1317;

	thermal_val = (u8)rtw_hal_read_rfreg(adapt, RF_PATH_A,
					   RF_T_METER_88E, 0xfc00);

	if (thermal_val) {
		/* Query OFDM path A default setting */
		ele_d = phy_query_bb_reg(adapt, rOFDM0_XATxIQImbalance, bMaskDWord) & bMaskOFDM_D;
		for (i = 0; i < OFDM_TABLE_SIZE_92D; i++) {
			if (ele_d == (OFDMSwingTable[i] & bMaskOFDM_D)) {
				ofdm_index_old[0] = (u8)i;
				dm_odm->BbSwingIdxOfdmBase = (u8)i;
				break;
			}
		}

		/* Query CCK default setting From 0xa24 */
		temp_cck = dm_odm->RFCalibrateInfo.RegA24;

		for (i = 0; i < CCK_TABLE_SIZE; i++) {
			if ((dm_odm->RFCalibrateInfo.bCCKinCH14 &&
			     memcmp(&temp_cck, &CCKSwingTable_Ch14[i][2], 4)) ||
			    memcmp(&temp_cck, &CCKSwingTable_Ch1_Ch13[i][2], 4)) {
				cck_index_old = (u8)i;
				dm_odm->BbSwingIdxCckBase = (u8)i;
				break;
			}
		}

		if (!dm_odm->RFCalibrateInfo.ThermalValue) {
			dm_odm->RFCalibrateInfo.ThermalValue = hal_data->EEPROMThermalMeter;
			dm_odm->RFCalibrateInfo.ThermalValue_LCK = thermal_val;
			dm_odm->RFCalibrateInfo.ThermalValue_IQK = thermal_val;

			dm_odm->RFCalibrateInfo.OFDM_index[0] = ofdm_index_old[0];
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

		if (dm_odm->RFCalibrateInfo.bDoneTxpower &&
		    !dm_odm->RFCalibrateInfo.bReloadtxpowerindex) {
			delta = abs(thermal_val - dm_odm->RFCalibrateInfo.ThermalValue);
		} else {
			delta = abs(thermal_val - hal_data->EEPROMThermalMeter);
			if (dm_odm->RFCalibrateInfo.bReloadtxpowerindex) {
				dm_odm->RFCalibrateInfo.bReloadtxpowerindex = false;
				dm_odm->RFCalibrateInfo.bDoneTxpower = false;
			}
		}

		delta_lck = abs(dm_odm->RFCalibrateInfo.ThermalValue_LCK - thermal_val);
		delta_iqk = abs(dm_odm->RFCalibrateInfo.ThermalValue_IQK - thermal_val);

		/* Delta temperature is equal to or larger than 20 centigrade.*/
		if ((delta_lck >= 8)) {
			dm_odm->RFCalibrateInfo.ThermalValue_LCK = thermal_val;
			rtl88eu_phy_lc_calibrate(adapt);
		}

		if (delta > 0 && dm_odm->RFCalibrateInfo.TxPowerTrackControl) {
			delta = abs(hal_data->EEPROMThermalMeter - thermal_val);

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
				offset = index_mapping_NUM_88E - 1;

			/* Updating ofdm_index values with new OFDM / CCK offset */
			ofdm_index[0] = dm_odm->RFCalibrateInfo.OFDM_index[0] + ofdm_index_mapping[j][offset];
			if (ofdm_index[0] > OFDM_TABLE_SIZE_92D - 1)
				ofdm_index[0] = OFDM_TABLE_SIZE_92D - 1;
			else if (ofdm_index[0] < ofdm_min_index)
				ofdm_index[0] = ofdm_min_index;

			cck_index = dm_odm->RFCalibrateInfo.CCK_index + ofdm_index_mapping[j][offset];
			if (cck_index > CCK_TABLE_SIZE - 1)
				cck_index = CCK_TABLE_SIZE - 1;
			else if (cck_index < 0)
				cck_index = 0;

			/* 2 temporarily remove bNOPG */
			/* Config by SwingTable */
			if (dm_odm->RFCalibrateInfo.TxPowerTrackControl) {
				dm_odm->RFCalibrateInfo.bDoneTxpower = true;

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
			}
		}

		/* Delta temperature is equal to or larger than 20 centigrade.*/
		if (delta_iqk >= 8) {
			dm_odm->RFCalibrateInfo.ThermalValue_IQK = thermal_val;
			rtl88eu_phy_iq_calibrate(adapt, false);
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
	u32 reg_eac, reg_e94, reg_e9c;
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

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	return result;
}

static u8 phy_path_a_rx_iqk(struct adapter *adapt, bool configPathB)
{
	u32 reg_eac, reg_e94, reg_e9c, reg_ea4, u4tmp;
	u8 result = 0x00;
	struct odm_dm_struct *dm_odm = &adapt->HalData->odmpriv;

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

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else					/* if Tx not OK, ignore Rx */
		return result;

	u4tmp = 0x80007C00 | (reg_e94 & 0x3FF0000)  | ((reg_e9c & 0x3FF0000) >> 16);
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

	if (!(reg_eac & BIT(27)) && /* if Tx is OK, check whether Rx is OK */
	    (((reg_ea4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((reg_eac & 0x03FF0000) >> 16) != 0x36))
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
	struct odm_dm_struct *dm_odm = &adapt->HalData->odmpriv;

	/* One shot, path B LOK & IQK */
	phy_set_bb_reg(adapt, rIQK_AGC_Cont, bMaskDWord, 0x00000002);
	phy_set_bb_reg(adapt, rIQK_AGC_Cont, bMaskDWord, 0x00000000);

	mdelay(IQK_DELAY_TIME_88E);

	regeac = phy_query_bb_reg(adapt, rRx_Power_After_IQK_A_2, bMaskDWord);
	regeb4 = phy_query_bb_reg(adapt, rTx_Power_Before_IQK_B, bMaskDWord);
	regebc = phy_query_bb_reg(adapt, rTx_Power_After_IQK_B, bMaskDWord);
	regec4 = phy_query_bb_reg(adapt, rRx_Power_Before_IQK_B_2, bMaskDWord);
	regecc = phy_query_bb_reg(adapt, rRx_Power_After_IQK_B_2, bMaskDWord);

	if (!(regeac & BIT(31)) &&
	    (((regeb4 & 0x03FF0000) >> 16) != 0x142) &&
	    (((regebc & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else
		return result;

	if (!(regeac & BIT(30)) &&
	    (((regec4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((regecc & 0x03FF0000) >> 16) != 0x36))
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
			       ((x * oldval_0 >> 7) & 0x1));

		y = result[final_candidate][1];
		if ((y & 0x00000200) != 0)
			y = y | 0xFFFFFC00;

		tx0_c = (y * oldval_0) >> 8;
		phy_set_bb_reg(adapt, rOFDM0_XCTxAFE, 0xF0000000,
			       ((tx0_c & 0x3C0) >> 6));
		phy_set_bb_reg(adapt, rOFDM0_XATxIQImbalance, 0x003F0000,
			       (tx0_c & 0x3F));
		phy_set_bb_reg(adapt, rOFDM0_ECCAThreshold, BIT(29),
			       ((y * oldval_0 >> 7) & 0x1));

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
			       ((x * oldval_1 >> 7) & 0x1));

		y = result[final_candidate][5];
		if ((y & 0x00000200) != 0)
			y = y | 0xFFFFFC00;

		tx1_c = (y * oldval_1) >> 8;

		phy_set_bb_reg(adapt, rOFDM0_XDTxAFE, 0xF0000000,
			       ((tx1_c & 0x3C0) >> 6));
		phy_set_bb_reg(adapt, rOFDM0_XBTxIQImbalance, 0x003F0000,
			       (tx1_c & 0x3F));
		phy_set_bb_reg(adapt, rOFDM0_ECCAThreshold, BIT(25),
			       ((y * oldval_1 >> 7) & 0x1));

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

static void save_adda_registers(struct adapter *adapt, const u32 *addareg,
				u32 *backup, u32 register_num)
{
	u32 i;

	for (i = 0; i < register_num; i++)
		backup[i] = phy_query_bb_reg(adapt, addareg[i], bMaskDWord);
}

static void save_mac_registers(struct adapter *adapt, const u32 *mac_reg,
			       u32 *backup)
{
	u32 i;

	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++)
		backup[i] = usb_read8(adapt, mac_reg[i]);

	backup[i] = usb_read32(adapt, mac_reg[i]);
}

static void reload_adda_reg(struct adapter *adapt, const u32 *adda_reg,
			    u32 *backup, u32 regiester_num)
{
	u32 i;

	for (i = 0; i < regiester_num; i++)
		phy_set_bb_reg(adapt, adda_reg[i], bMaskDWord, backup[i]);
}

static void reload_mac_registers(struct adapter *adapt, const u32 *mac_reg,
				 u32 *backup)
{
	u32 i;

	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++)
		usb_write8(adapt, mac_reg[i], (u8)backup[i]);

	usb_write32(adapt, mac_reg[i], backup[i]);
}

static void path_adda_on(struct adapter *adapt, const u32 *adda_reg,
			 bool is_path_a_on, bool is2t)
{
	u32 path_on;
	u32 i;

	if (!is2t) {
		path_on = 0x0bdb25a0;
		phy_set_bb_reg(adapt, adda_reg[0], bMaskDWord, 0x0b1b25a0);
	} else {
		path_on = is_path_a_on ? 0x04db25a4 : 0x0b1b25a4;
		phy_set_bb_reg(adapt, adda_reg[0], bMaskDWord, path_on);
	}

	for (i = 1; i < IQK_ADDA_REG_NUM; i++)
		phy_set_bb_reg(adapt, adda_reg[i], bMaskDWord, path_on);
}

static void mac_setting_calibration(struct adapter *adapt, const u32 *mac_reg,
				    u32 *backup)
{
	u32 i = 0;

	usb_write8(adapt, mac_reg[i], 0x3F);

	for (i = 1; i < (IQK_MAC_REG_NUM - 1); i++)
		usb_write8(adapt, mac_reg[i], (u8)(backup[i] & (~BIT(3))));

	usb_write8(adapt, mac_reg[i], (u8)(backup[i] & (~BIT(5))));
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

static bool simularity_compare(struct adapter *adapt, s32 resulta[][8],
			       u8 c1, u8 c2)
{
	u32 i, j, diff, sim_bitmap = 0, bound;
	u8 final_candidate[2] = {0xFF, 0xFF};	/* for path A and path B */
	bool result = true;
	s32 tmp1 = 0, tmp2 = 0;

	bound = 4;

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

		diff = abs(tmp1 - tmp2);

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
	}

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

static void phy_iq_calibrate(struct adapter *adapt, s32 result[][8],
			     u8 t, bool is2t)
{
	struct odm_dm_struct *dm_odm = &adapt->HalData->odmpriv;
	u32 i;
	u8 path_a_ok, path_b_ok;
	static const u32 adda_reg[IQK_ADDA_REG_NUM] = {
		rFPGA0_XCD_SwitchControl, rBlue_Tooth,
		rRx_Wait_CCA, rTx_CCK_RFON,
		rTx_CCK_BBON, rTx_OFDM_RFON,
		rTx_OFDM_BBON, rTx_To_Rx,
		rTx_To_Tx, rRx_CCK,
		rRx_OFDM, rRx_Wait_RIFS,
		rRx_TO_Rx, rStandby,
		rSleep, rPMPD_ANAEN
	};
	static const u32 iqk_mac_reg[IQK_MAC_REG_NUM] = {
		REG_TXPAUSE, REG_BCN_CTRL,
		REG_BCN_CTRL_1, REG_GPIO_MUXCFG
	};
	/* since 92C & 92D have the different define in IQK_BB_REG */
	static const u32 iqk_bb_reg_92c[IQK_BB_REG_NUM] = {
		rOFDM0_TRxPathEnable, rOFDM0_TRMuxPar,
		rFPGA0_XCD_RFInterfaceSW, rConfig_AntA, rConfig_AntB,
		rFPGA0_XAB_RFInterfaceSW, rFPGA0_XA_RFInterfaceOE,
		rFPGA0_XB_RFInterfaceOE, rFPGA0_RFMOD
	};

	u32 retry_count = 9;

	if (*dm_odm->mp_mode == 1)
		retry_count = 9;
	else
		retry_count = 2;

	if (t == 0) {
		/*  Save ADDA parameters, turn Path A ADDA on */
		save_adda_registers(adapt, adda_reg, dm_odm->RFCalibrateInfo.ADDA_backup,
				    IQK_ADDA_REG_NUM);
		save_mac_registers(adapt, iqk_mac_reg,
				   dm_odm->RFCalibrateInfo.IQK_MAC_backup);
		save_adda_registers(adapt, iqk_bb_reg_92c,
				    dm_odm->RFCalibrateInfo.IQK_BB_backup, IQK_BB_REG_NUM);
	}

	path_adda_on(adapt, adda_reg, true, is2t);
	if (t == 0)
		dm_odm->RFCalibrateInfo.bRfPiEnable = (u8)phy_query_bb_reg(adapt, rFPGA0_XA_HSSIParameter1,
									   BIT(8));

	if (!dm_odm->RFCalibrateInfo.bRfPiEnable) {
		/*  Switch BB to PI mode to do IQ Calibration. */
		pi_mode_switch(adapt, true);
	}

	/* BB setting */
	phy_set_bb_reg(adapt, rFPGA0_RFMOD, BIT(24), 0x00);
	phy_set_bb_reg(adapt, rOFDM0_TRxPathEnable, bMaskDWord, 0x03a05600);
	phy_set_bb_reg(adapt, rOFDM0_TRMuxPar, bMaskDWord, 0x000800e4);
	phy_set_bb_reg(adapt, rFPGA0_XCD_RFInterfaceSW, bMaskDWord, 0x22204000);

	phy_set_bb_reg(adapt, rFPGA0_XAB_RFInterfaceSW, BIT(10), 0x01);
	phy_set_bb_reg(adapt, rFPGA0_XAB_RFInterfaceSW, BIT(26), 0x01);
	phy_set_bb_reg(adapt, rFPGA0_XA_RFInterfaceOE, BIT(10), 0x00);
	phy_set_bb_reg(adapt, rFPGA0_XB_RFInterfaceOE, BIT(10), 0x00);

	if (is2t) {
		phy_set_bb_reg(adapt, rFPGA0_XA_LSSIParameter, bMaskDWord,
			       0x00010000);
		phy_set_bb_reg(adapt, rFPGA0_XB_LSSIParameter, bMaskDWord,
			       0x00010000);
	}

	/* MAC settings */
	mac_setting_calibration(adapt, iqk_mac_reg,
				dm_odm->RFCalibrateInfo.IQK_MAC_backup);

	/* Page B init */
	/* AP or IQK */
	phy_set_bb_reg(adapt, rConfig_AntA, bMaskDWord, 0x0f600000);

	if (is2t)
		phy_set_bb_reg(adapt, rConfig_AntB, bMaskDWord, 0x0f600000);

	/*  IQ calibration setting */
	phy_set_bb_reg(adapt, rFPGA0_IQK, bMaskDWord, 0x80800000);
	phy_set_bb_reg(adapt, rTx_IQK, bMaskDWord, 0x01007c00);
	phy_set_bb_reg(adapt, rRx_IQK, bMaskDWord, 0x81004800);

	for (i = 0; i < retry_count; i++) {
		path_a_ok = phy_path_a_iqk(adapt, is2t);
		if (path_a_ok == 0x01) {
			result[t][0] = (phy_query_bb_reg(adapt, rTx_Power_Before_IQK_A,
							 bMaskDWord) & 0x3FF0000) >> 16;
			result[t][1] = (phy_query_bb_reg(adapt, rTx_Power_After_IQK_A,
							 bMaskDWord) & 0x3FF0000) >> 16;
			break;
		}
	}

	for (i = 0; i < retry_count; i++) {
		path_a_ok = phy_path_a_rx_iqk(adapt, is2t);
		if (path_a_ok == 0x03) {
			result[t][2] = (phy_query_bb_reg(adapt, rRx_Power_Before_IQK_A_2,
							 bMaskDWord) & 0x3FF0000) >> 16;
			result[t][3] = (phy_query_bb_reg(adapt, rRx_Power_After_IQK_A_2,
							 bMaskDWord) & 0x3FF0000) >> 16;
			break;
		}
		ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
			     ("Path A Rx IQK Fail!!\n"));
	}

	if (path_a_ok == 0x00) {
		ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
			     ("Path A IQK failed!!\n"));
	}

	if (is2t) {
		path_a_standby(adapt);

		/*  Turn Path B ADDA on */
		path_adda_on(adapt, adda_reg, false, is2t);

		for (i = 0; i < retry_count; i++) {
			path_b_ok = phy_path_b_iqk(adapt);
			if (path_b_ok == 0x03) {
				result[t][4] = (phy_query_bb_reg(adapt, rTx_Power_Before_IQK_B,
								 bMaskDWord) & 0x3FF0000) >> 16;
				result[t][5] = (phy_query_bb_reg(adapt, rTx_Power_After_IQK_B,
								 bMaskDWord) & 0x3FF0000) >> 16;
				result[t][6] = (phy_query_bb_reg(adapt, rRx_Power_Before_IQK_B_2,
								 bMaskDWord) & 0x3FF0000) >> 16;
				result[t][7] = (phy_query_bb_reg(adapt, rRx_Power_After_IQK_B_2,
								 bMaskDWord) & 0x3FF0000) >> 16;
				break;
			} else if (i == (retry_count - 1) && path_b_ok == 0x01) {	/* Tx IQK OK */
				result[t][4] = (phy_query_bb_reg(adapt, rTx_Power_Before_IQK_B,
								 bMaskDWord) & 0x3FF0000) >> 16;
				result[t][5] = (phy_query_bb_reg(adapt, rTx_Power_After_IQK_B,
								 bMaskDWord) & 0x3FF0000) >> 16;
			}
		}

		if (path_b_ok == 0x00) {
			ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
				     ("Path B IQK failed!!\n"));
		}
	}

	/* Back to BB mode, load original value */
	phy_set_bb_reg(adapt, rFPGA0_IQK, bMaskDWord, 0);

	if (t != 0) {
		if (!dm_odm->RFCalibrateInfo.bRfPiEnable) {
			/* Switch back BB to SI mode after
			 * finish IQ Calibration.
			 */
			pi_mode_switch(adapt, false);
		}

		/*  Reload ADDA power saving parameters */
		reload_adda_reg(adapt, adda_reg, dm_odm->RFCalibrateInfo.ADDA_backup,
				IQK_ADDA_REG_NUM);

		/*  Reload MAC parameters */
		reload_mac_registers(adapt, iqk_mac_reg,
				     dm_odm->RFCalibrateInfo.IQK_MAC_backup);

		reload_adda_reg(adapt, iqk_bb_reg_92c, dm_odm->RFCalibrateInfo.IQK_BB_backup,
				IQK_BB_REG_NUM);

		/*  Restore RX initial gain */
		phy_set_bb_reg(adapt, rFPGA0_XA_LSSIParameter,
			       bMaskDWord, 0x00032ed3);
		if (is2t)
			phy_set_bb_reg(adapt, rFPGA0_XB_LSSIParameter,
				       bMaskDWord, 0x00032ed3);

		/* load 0xe30 IQC default value */
		phy_set_bb_reg(adapt, rTx_IQK_Tone_A, bMaskDWord, 0x01008c00);
		phy_set_bb_reg(adapt, rRx_IQK_Tone_A, bMaskDWord, 0x01008c00);
	}
}

static void phy_lc_calibrate(struct adapter *adapt, bool is2t)
{
	u8 tmpreg;
	u32 rf_a_mode = 0, rf_b_mode = 0, lc_cal;

	/* Check continuous TX and Packet TX */
	tmpreg = usb_read8(adapt, 0xd03);

	if ((tmpreg & 0x70) != 0)
		usb_write8(adapt, 0xd03, tmpreg & 0x8F);
	else
		usb_write8(adapt, REG_TXPAUSE, 0xFF);

	if ((tmpreg & 0x70) != 0) {
		/* 1. Read original RF mode */
		/* Path-A */
		rf_a_mode = rtw_hal_read_rfreg(adapt, RF_PATH_A, RF_AC,
					       bMask12Bits);

		/* Path-B */
		if (is2t)
			rf_b_mode = rtw_hal_read_rfreg(adapt, RF_PATH_B, RF_AC,
						       bMask12Bits);

		/* 2. Set RF mode = standby mode */
		/* Path-A */
		phy_set_rf_reg(adapt, RF_PATH_A, RF_AC, bMask12Bits,
			       (rf_a_mode & 0x8FFFF) | 0x10000);

		/* Path-B */
		if (is2t)
			phy_set_rf_reg(adapt, RF_PATH_B, RF_AC, bMask12Bits,
				       (rf_b_mode & 0x8FFFF) | 0x10000);
	}

	/* 3. Read RF reg18 */
	lc_cal = rtw_hal_read_rfreg(adapt, RF_PATH_A, RF_CHNLBW, bMask12Bits);

	/* 4. Set LC calibration begin bit15 */
	phy_set_rf_reg(adapt, RF_PATH_A, RF_CHNLBW, bMask12Bits,
		       lc_cal | 0x08000);

	msleep(100);

	/* Restore original situation */
	if ((tmpreg & 0x70) != 0) {
		/* Deal with continuous TX case */
		/* Path-A */
		usb_write8(adapt, 0xd03, tmpreg);
		phy_set_rf_reg(adapt, RF_PATH_A, RF_AC, bMask12Bits, rf_a_mode);

		/* Path-B */
		if (is2t)
			phy_set_rf_reg(adapt, RF_PATH_B, RF_AC, bMask12Bits,
				       rf_b_mode);
	} else {
		/* Deal with Packet TX case */
		usb_write8(adapt, REG_TXPAUSE, 0x00);
	}
}

void rtl88eu_phy_iq_calibrate(struct adapter *adapt, bool recovery)
{
	struct odm_dm_struct *dm_odm = &adapt->HalData->odmpriv;
	s32 result[4][8];
	u8 i, final;
	bool pathaok, pathbok;
	s32 reg_e94, reg_e9c, reg_ea4, reg_eb4, reg_ebc, reg_ec4;
	bool is12simular, is13simular, is23simular;
	bool singletone = false, carrier_sup = false;
	u32 iqk_bb_reg_92c[IQK_BB_REG_NUM] = {
		rOFDM0_XARxIQImbalance, rOFDM0_XBRxIQImbalance,
		rOFDM0_ECCAThreshold, rOFDM0_AGCRSSITable,
		rOFDM0_XATxIQImbalance, rOFDM0_XBTxIQImbalance,
		rOFDM0_XCTxAFE, rOFDM0_XDTxAFE,
		rOFDM0_RxIQExtAnta};
	bool is2t;

	is2t = false;

	if (!(dm_odm->SupportAbility & ODM_RF_CALIBRATION))
		return;

	if (singletone || carrier_sup)
		return;

	if (recovery) {
		ODM_RT_TRACE(dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD,
			     ("phy_iq_calibrate: Return due to recovery!\n"));
		reload_adda_reg(adapt, iqk_bb_reg_92c,
				dm_odm->RFCalibrateInfo.IQK_BB_backup_recover, 9);
		return;
	}

	memset(result, 0, sizeof(result));
	for (i = 0; i < 8; i += 2)
		result[3][i] = 0x100;

	final = 0xff;
	pathaok = false;
	pathbok = false;
	is12simular = false;
	is23simular = false;
	is13simular = false;

	for (i = 0; i < 3; i++) {
		phy_iq_calibrate(adapt, result, i, is2t);

		if (i == 1) {
			is12simular = simularity_compare(adapt, result, 0, 1);
			if (is12simular) {
				final = 0;
				break;
			}
		}

		if (i == 2) {
			is13simular = simularity_compare(adapt, result, 0, 2);
			if (is13simular) {
				final = 0;
				break;
			}
			is23simular = simularity_compare(adapt, result, 1, 2);
			if (is23simular)
				final = 1;
			else
				final = 3;
		}
	}

	for (i = 0; i < 4; i++) {
		reg_e94 = result[i][0];
		reg_e9c = result[i][1];
		reg_ea4 = result[i][2];
		reg_eb4 = result[i][4];
		reg_ebc = result[i][5];
		reg_ec4 = result[i][6];
	}

	if (final != 0xff) {
		reg_e94 = result[final][0];
		reg_e9c = result[final][1];
		reg_ea4 = result[final][2];
		reg_eb4 = result[final][4];
		reg_ebc = result[final][5];
		dm_odm->RFCalibrateInfo.RegE94 = reg_e94;
		dm_odm->RFCalibrateInfo.RegE9C = reg_e9c;
		dm_odm->RFCalibrateInfo.RegEB4 = reg_eb4;
		dm_odm->RFCalibrateInfo.RegEBC = reg_ebc;
		reg_ec4 = result[final][6];
		pathaok = true;
		pathbok = true;
	} else {
		ODM_RT_TRACE(dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD,
			     ("IQK: FAIL use default value\n"));
		dm_odm->RFCalibrateInfo.RegE94 = 0x100;
		dm_odm->RFCalibrateInfo.RegEB4 = 0x100;
		dm_odm->RFCalibrateInfo.RegE9C = 0x0;
		dm_odm->RFCalibrateInfo.RegEBC = 0x0;
	}
	if (reg_e94 != 0)
		patha_fill_iqk(adapt, pathaok, result, final,
			       (reg_ea4 == 0));
	if (is2t) {
		if (reg_eb4 != 0)
			pathb_fill_iqk(adapt, pathbok, result, final,
				       (reg_ec4 == 0));
	}

	if (final < 4) {
		for (i = 0; i < IQK_Matrix_REG_NUM; i++)
			dm_odm->RFCalibrateInfo.IQKMatrixRegSetting[0].Value[0][i] = result[final][i];
		dm_odm->RFCalibrateInfo.IQKMatrixRegSetting[0].bIQKDone = true;
	}

	save_adda_registers(adapt, iqk_bb_reg_92c,
			    dm_odm->RFCalibrateInfo.IQK_BB_backup_recover, 9);
}

void rtl88eu_phy_lc_calibrate(struct adapter *adapt)
{
	bool singletone = false, carrier_sup = false;
	u32 timeout = 2000, timecount = 0;
	struct odm_dm_struct *dm_odm = &adapt->HalData->odmpriv;

	if (!(dm_odm->SupportAbility & ODM_RF_CALIBRATION))
		return;
	if (singletone || carrier_sup)
		return;

	while (*dm_odm->pbScanInProcess && timecount < timeout) {
		mdelay(50);
		timecount += 50;
	}

	dm_odm->RFCalibrateInfo.bLCKInProgress = true;

	phy_lc_calibrate(adapt, false);

	dm_odm->RFCalibrateInfo.bLCKInProgress = false;
}
