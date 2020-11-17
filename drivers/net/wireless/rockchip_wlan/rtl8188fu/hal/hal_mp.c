/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#define _HAL_MP_C_

#include <drv_types.h>

#ifdef CONFIG_MP_INCLUDED

#ifdef RTW_HALMAC
	#include <hal_data.h>		/* struct HAL_DATA_TYPE, RF register definition and etc. */
#else /* !RTW_HALMAC */
	#ifdef CONFIG_RTL8188E
		#include <rtl8188e_hal.h>
	#endif
	#ifdef CONFIG_RTL8723B
		#include <rtl8723b_hal.h>
	#endif
	#ifdef CONFIG_RTL8192E
		#include <rtl8192e_hal.h>
	#endif
	#ifdef CONFIG_RTL8814A
		#include <rtl8814a_hal.h>
	#endif
	#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A)
		#include <rtl8812a_hal.h>
	#endif
	#ifdef CONFIG_RTL8703B
		#include <rtl8703b_hal.h>
	#endif
	#ifdef CONFIG_RTL8723D
		#include <rtl8723d_hal.h>
	#endif
	#ifdef CONFIG_RTL8710B
		#include <rtl8710b_hal.h>
	#endif
	#ifdef CONFIG_RTL8188F
		#include <rtl8188f_hal.h>
	#endif
	#ifdef CONFIG_RTL8188GTV
		#include <rtl8188gtv_hal.h>
	#endif
	#ifdef CONFIG_RTL8192F
		#include <rtl8192f_hal.h>
	#endif
#endif /* !RTW_HALMAC */


u8 MgntQuery_NssTxRate(u16 Rate)
{
	u8	NssNum = RF_TX_NUM_NONIMPLEMENT;

	if ((Rate >= MGN_MCS8 && Rate <= MGN_MCS15) ||
	    (Rate >= MGN_VHT2SS_MCS0 && Rate <= MGN_VHT2SS_MCS9))
		NssNum = RF_2TX;
	else if ((Rate >= MGN_MCS16 && Rate <= MGN_MCS23) ||
		 (Rate >= MGN_VHT3SS_MCS0 && Rate <= MGN_VHT3SS_MCS9))
		NssNum = RF_3TX;
	else if ((Rate >= MGN_MCS24 && Rate <= MGN_MCS31) ||
		 (Rate >= MGN_VHT4SS_MCS0 && Rate <= MGN_VHT4SS_MCS9))
		NssNum = RF_4TX;
	else
		NssNum = RF_1TX;

	return NssNum;
}

void hal_mpt_SwitchRfSetting(PADAPTER	pAdapter)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT		pMptCtx = &(pAdapter->mppriv.mpt_ctx);
	u8				ChannelToSw = pMptCtx->MptChannelToSw;
	u32				ulRateIdx = pMptCtx->mpt_rate_index;
	u32				ulbandwidth = pMptCtx->MptBandWidth;

	/* <20120525, Kordan> Dynamic mechanism for APK, asked by Dennis.*/
	if (IS_HARDWARE_TYPE_8188ES(pAdapter) && (1 <= ChannelToSw && ChannelToSw <= 11) &&
	    (ulRateIdx == MPT_RATE_MCS0 || ulRateIdx == MPT_RATE_1M || ulRateIdx == MPT_RATE_6M)) {
		pMptCtx->backup0x52_RF_A = (u8)phy_query_rf_reg(pAdapter, RF_PATH_A, RF_0x52, 0x000F0);
		pMptCtx->backup0x52_RF_B = (u8)phy_query_rf_reg(pAdapter, RF_PATH_B, RF_0x52, 0x000F0);

		if ((PlatformEFIORead4Byte(pAdapter, 0xF4) & BIT29) == BIT29) {
			phy_set_rf_reg(pAdapter, RF_PATH_A, RF_0x52, 0x000F0, 0xB);
			phy_set_rf_reg(pAdapter, RF_PATH_B, RF_0x52, 0x000F0, 0xB);
		} else {
			phy_set_rf_reg(pAdapter, RF_PATH_A, RF_0x52, 0x000F0, 0xD);
			phy_set_rf_reg(pAdapter, RF_PATH_B, RF_0x52, 0x000F0, 0xD);
		}
	} else if (IS_HARDWARE_TYPE_8188EE(pAdapter)) { /* <20140903, VincentL> Asked by RF Eason and Edlu*/
		if (ChannelToSw == 3 && ulbandwidth == MPT_BW_40MHZ) {
			phy_set_rf_reg(pAdapter, RF_PATH_A, RF_0x52, 0x000F0, 0xB); /*RF 0x52 = 0x0007E4BD*/
			phy_set_rf_reg(pAdapter, RF_PATH_B, RF_0x52, 0x000F0, 0xB); /*RF 0x52 = 0x0007E4BD*/
		} else {
			phy_set_rf_reg(pAdapter, RF_PATH_A, RF_0x52, 0x000F0, 0x9); /*RF 0x52 = 0x0007E49D*/
			phy_set_rf_reg(pAdapter, RF_PATH_B, RF_0x52, 0x000F0, 0x9); /*RF 0x52 = 0x0007E49D*/
		}
	} else if (IS_HARDWARE_TYPE_8188E(pAdapter)) {
		phy_set_rf_reg(pAdapter, RF_PATH_A, RF_0x52, 0x000F0, pMptCtx->backup0x52_RF_A);
		phy_set_rf_reg(pAdapter, RF_PATH_B, RF_0x52, 0x000F0, pMptCtx->backup0x52_RF_B);
	}
}

s32 hal_mpt_SetPowerTracking(PADAPTER padapter, u8 enable)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct dm_struct		*pDM_Odm = &(pHalData->odmpriv);


	if (!netif_running(padapter->pnetdev)) {
		return _FAIL;
	}

	if (check_fwstate(&padapter->mlmepriv, WIFI_MP_STATE) == _FALSE) {
		return _FAIL;
	}
	if (enable)
		pDM_Odm->rf_calibrate_info.txpowertrack_control = _TRUE;
	else
		pDM_Odm->rf_calibrate_info.txpowertrack_control = _FALSE;

	return _SUCCESS;
}

void hal_mpt_GetPowerTracking(PADAPTER padapter, u8 *enable)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct dm_struct		*pDM_Odm = &(pHalData->odmpriv);


	*enable = pDM_Odm->rf_calibrate_info.txpowertrack_control;
}


void hal_mpt_CCKTxPowerAdjust(PADAPTER Adapter, BOOLEAN bInCH14)
{
	u32		TempVal = 0, TempVal2 = 0, TempVal3 = 0;
	u32		CurrCCKSwingVal = 0, CCKSwingIndex = 12;
	u8		i;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PMPT_CONTEXT		pMptCtx = &(Adapter->mppriv.mpt_ctx);
	u8				u1Channel = pHalData->current_channel;
	u32				ulRateIdx = pMptCtx->mpt_rate_index;
	u8				DataRate = 0xFF;

	/* Do not modify CCK TX filter parameters for 8822B*/
	if(IS_HARDWARE_TYPE_8822B(Adapter) || IS_HARDWARE_TYPE_8821C(Adapter) ||
		IS_HARDWARE_TYPE_8723D(Adapter) || IS_HARDWARE_TYPE_8192F(Adapter) || IS_HARDWARE_TYPE_8822C(Adapter))
		return;

	DataRate = mpt_to_mgnt_rate(ulRateIdx);

	if (u1Channel == 14 && IS_CCK_RATE(DataRate))
		pHalData->bCCKinCH14 = TRUE;
	else
		pHalData->bCCKinCH14 = FALSE;

	if (IS_HARDWARE_TYPE_8703B(Adapter)) {
		if ((u1Channel == 14) && IS_CCK_RATE(DataRate)) {
			/* Channel 14 in CCK, need to set 0xA26~0xA29 to 0 for 8703B */
			phy_set_bb_reg(Adapter, rCCK0_TxFilter2, bMaskHWord, 0);
			phy_set_bb_reg(Adapter, rCCK0_DebugPort, bMaskLWord, 0);

		} else {
			/* Normal setting for 8703B, just recover to the default setting. */
			/* This hardcore values reference from the parameter which BB team gave. */
			for (i = 0 ; i < 2 ; ++i)
				phy_set_bb_reg(Adapter, pHalData->RegForRecover[i].offset, bMaskDWord, pHalData->RegForRecover[i].value);

		}
	} else if (IS_HARDWARE_TYPE_8723D(Adapter)) {
		/* 2.4G CCK TX DFIR */
		/* 2016.01.20 Suggest from RS BB mingzhi*/
		if ((u1Channel == 14)) {
			phy_set_bb_reg(Adapter, rCCK0_TxFilter2, bMaskDWord, 0x0000B81C);
			phy_set_bb_reg(Adapter, rCCK0_DebugPort, bMaskDWord, 0x00000000);
			phy_set_bb_reg(Adapter, 0xAAC, bMaskDWord, 0x00003667);
		} else {
			for (i = 0 ; i < 3 ; ++i) {
				phy_set_bb_reg(Adapter,
					     pHalData->RegForRecover[i].offset,
					     bMaskDWord,
					     pHalData->RegForRecover[i].value);
			}
		}
	} else if (IS_HARDWARE_TYPE_8188F(Adapter) || IS_HARDWARE_TYPE_8188GTV(Adapter)) {
		/* get current cck swing value and check 0xa22 & 0xa23 later to match the table.*/
		CurrCCKSwingVal = read_bbreg(Adapter, rCCK0_TxFilter1, bMaskHWord);
		CCKSwingIndex = 20; /* default index */

		if (!pHalData->bCCKinCH14) {
			/* Readback the current bb cck swing value and compare with the table to */
			/* get the current swing index */
			for (i = 0; i < CCK_TABLE_SIZE_88F; i++) {
				if (((CurrCCKSwingVal & 0xff) == (u32)cck_swing_table_ch1_ch13_88f[i][0]) &&
				    (((CurrCCKSwingVal & 0xff00) >> 8) == (u32)cck_swing_table_ch1_ch13_88f[i][1])) {
					CCKSwingIndex = i;
					break;
				}
			}
			write_bbreg(Adapter, 0xa22, bMaskByte0, cck_swing_table_ch1_ch13_88f[CCKSwingIndex][0]);
			write_bbreg(Adapter, 0xa23, bMaskByte0, cck_swing_table_ch1_ch13_88f[CCKSwingIndex][1]);
			write_bbreg(Adapter, 0xa24, bMaskByte0, cck_swing_table_ch1_ch13_88f[CCKSwingIndex][2]);
			write_bbreg(Adapter, 0xa25, bMaskByte0, cck_swing_table_ch1_ch13_88f[CCKSwingIndex][3]);
			write_bbreg(Adapter, 0xa26, bMaskByte0, cck_swing_table_ch1_ch13_88f[CCKSwingIndex][4]);
			write_bbreg(Adapter, 0xa27, bMaskByte0, cck_swing_table_ch1_ch13_88f[CCKSwingIndex][5]);
			write_bbreg(Adapter, 0xa28, bMaskByte0, cck_swing_table_ch1_ch13_88f[CCKSwingIndex][6]);
			write_bbreg(Adapter, 0xa29, bMaskByte0, cck_swing_table_ch1_ch13_88f[CCKSwingIndex][7]);
			write_bbreg(Adapter, 0xa9a, bMaskByte0, cck_swing_table_ch1_ch13_88f[CCKSwingIndex][8]);
			write_bbreg(Adapter, 0xa9b, bMaskByte0, cck_swing_table_ch1_ch13_88f[CCKSwingIndex][9]);
			write_bbreg(Adapter, 0xa9c, bMaskByte0, cck_swing_table_ch1_ch13_88f[CCKSwingIndex][10]);
			write_bbreg(Adapter, 0xa9d, bMaskByte0, cck_swing_table_ch1_ch13_88f[CCKSwingIndex][11]);
			write_bbreg(Adapter, 0xaa0, bMaskByte0, cck_swing_table_ch1_ch13_88f[CCKSwingIndex][12]);
			write_bbreg(Adapter, 0xaa1, bMaskByte0, cck_swing_table_ch1_ch13_88f[CCKSwingIndex][13]);
			write_bbreg(Adapter, 0xaa2, bMaskByte0, cck_swing_table_ch1_ch13_88f[CCKSwingIndex][14]);
			write_bbreg(Adapter, 0xaa3, bMaskByte0, cck_swing_table_ch1_ch13_88f[CCKSwingIndex][15]);
			RTW_INFO("%s , cck_swing_table_ch1_ch13_88f[%d]\n", __func__, CCKSwingIndex);
		}  else {
			for (i = 0; i < CCK_TABLE_SIZE_88F; i++) {
				if (((CurrCCKSwingVal & 0xff) == (u32)cck_swing_table_ch14_88f[i][0]) &&
				    (((CurrCCKSwingVal & 0xff00) >> 8) == (u32)cck_swing_table_ch14_88f[i][1])) {
					CCKSwingIndex = i;
					break;
				}
			}
			write_bbreg(Adapter, 0xa22, bMaskByte0, cck_swing_table_ch14_88f[CCKSwingIndex][0]);
			write_bbreg(Adapter, 0xa23, bMaskByte0, cck_swing_table_ch14_88f[CCKSwingIndex][1]);
			write_bbreg(Adapter, 0xa24, bMaskByte0, cck_swing_table_ch14_88f[CCKSwingIndex][2]);
			write_bbreg(Adapter, 0xa25, bMaskByte0, cck_swing_table_ch14_88f[CCKSwingIndex][3]);
			write_bbreg(Adapter, 0xa26, bMaskByte0, cck_swing_table_ch14_88f[CCKSwingIndex][4]);
			write_bbreg(Adapter, 0xa27, bMaskByte0, cck_swing_table_ch14_88f[CCKSwingIndex][5]);
			write_bbreg(Adapter, 0xa28, bMaskByte0, cck_swing_table_ch14_88f[CCKSwingIndex][6]);
			write_bbreg(Adapter, 0xa29, bMaskByte0, cck_swing_table_ch14_88f[CCKSwingIndex][7]);
			write_bbreg(Adapter, 0xa9a, bMaskByte0, cck_swing_table_ch14_88f[CCKSwingIndex][8]);
			write_bbreg(Adapter, 0xa9b, bMaskByte0, cck_swing_table_ch14_88f[CCKSwingIndex][9]);
			write_bbreg(Adapter, 0xa9c, bMaskByte0, cck_swing_table_ch14_88f[CCKSwingIndex][10]);
			write_bbreg(Adapter, 0xa9d, bMaskByte0, cck_swing_table_ch14_88f[CCKSwingIndex][11]);
			write_bbreg(Adapter, 0xaa0, bMaskByte0, cck_swing_table_ch14_88f[CCKSwingIndex][12]);
			write_bbreg(Adapter, 0xaa1, bMaskByte0, cck_swing_table_ch14_88f[CCKSwingIndex][13]);
			write_bbreg(Adapter, 0xaa2, bMaskByte0, cck_swing_table_ch14_88f[CCKSwingIndex][14]);
			write_bbreg(Adapter, 0xaa3, bMaskByte0, cck_swing_table_ch14_88f[CCKSwingIndex][15]);
			RTW_INFO("%s , cck_swing_table_ch14_88f[%d]\n", __func__, CCKSwingIndex);
		}
	} else {

		/* get current cck swing value and check 0xa22 & 0xa23 later to match the table.*/
		CurrCCKSwingVal = read_bbreg(Adapter, rCCK0_TxFilter1, bMaskHWord);

		if (!pHalData->bCCKinCH14) {
			/* Readback the current bb cck swing value and compare with the table to */
			/* get the current swing index */
			for (i = 0; i < CCK_TABLE_SIZE; i++) {
				if (((CurrCCKSwingVal & 0xff) == (u32)cck_swing_table_ch1_ch13[i][0]) &&
				    (((CurrCCKSwingVal & 0xff00) >> 8) == (u32)cck_swing_table_ch1_ch13[i][1])) {
					CCKSwingIndex = i;
					break;
				}
			}

			/*Write 0xa22 0xa23*/
			TempVal = cck_swing_table_ch1_ch13[CCKSwingIndex][0] +
				(cck_swing_table_ch1_ch13[CCKSwingIndex][1] << 8);


			/*Write 0xa24 ~ 0xa27*/
			TempVal2 = 0;
			TempVal2 = cck_swing_table_ch1_ch13[CCKSwingIndex][2] +
				(cck_swing_table_ch1_ch13[CCKSwingIndex][3] << 8) +
				(cck_swing_table_ch1_ch13[CCKSwingIndex][4] << 16) +
				(cck_swing_table_ch1_ch13[CCKSwingIndex][5] << 24);

			/*Write 0xa28  0xa29*/
			TempVal3 = 0;
			TempVal3 = cck_swing_table_ch1_ch13[CCKSwingIndex][6] +
				(cck_swing_table_ch1_ch13[CCKSwingIndex][7] << 8);
		}  else {
			for (i = 0; i < CCK_TABLE_SIZE; i++) {
				if (((CurrCCKSwingVal & 0xff) == (u32)cck_swing_table_ch14[i][0]) &&
				    (((CurrCCKSwingVal & 0xff00) >> 8) == (u32)cck_swing_table_ch14[i][1])) {
					CCKSwingIndex = i;
					break;
				}
			}

			/*Write 0xa22 0xa23*/
			TempVal = cck_swing_table_ch14[CCKSwingIndex][0] +
				  (cck_swing_table_ch14[CCKSwingIndex][1] << 8);

			/*Write 0xa24 ~ 0xa27*/
			TempVal2 = 0;
			TempVal2 = cck_swing_table_ch14[CCKSwingIndex][2] +
				   (cck_swing_table_ch14[CCKSwingIndex][3] << 8) +
				(cck_swing_table_ch14[CCKSwingIndex][4] << 16) +
				   (cck_swing_table_ch14[CCKSwingIndex][5] << 24);

			/*Write 0xa28  0xa29*/
			TempVal3 = 0;
			TempVal3 = cck_swing_table_ch14[CCKSwingIndex][6] +
				   (cck_swing_table_ch14[CCKSwingIndex][7] << 8);
		}

		write_bbreg(Adapter, rCCK0_TxFilter1, bMaskHWord, TempVal);
		write_bbreg(Adapter, rCCK0_TxFilter2, bMaskDWord, TempVal2);
		write_bbreg(Adapter, rCCK0_DebugPort, bMaskLWord, TempVal3);
	}

}

void hal_mpt_SetChannel(PADAPTER pAdapter)
{
	enum rf_path eRFPath;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_struct		*pDM_Odm = &(pHalData->odmpriv);
	struct mp_priv	*pmp = &pAdapter->mppriv;
	u8		channel = pmp->channel;
	u8		bandwidth = pmp->bandwidth;

	hal_mpt_SwitchRfSetting(pAdapter);

	pHalData->bSwChnl = _TRUE;
	pHalData->bSetChnlBW = _TRUE;

	if (bandwidth == 2) {
		rtw_hal_set_chnl_bw(pAdapter, channel, bandwidth, HAL_PRIME_CHNL_OFFSET_LOWER, HAL_PRIME_CHNL_OFFSET_UPPER);
	} else if (bandwidth == 1) {
		rtw_hal_set_chnl_bw(pAdapter, channel, bandwidth, HAL_PRIME_CHNL_OFFSET_UPPER, 0);
	} else
		rtw_hal_set_chnl_bw(pAdapter, channel, bandwidth, pmp->prime_channel_offset, 0);

	hal_mpt_CCKTxPowerAdjust(pAdapter, pHalData->bCCKinCH14);
	rtw_btcoex_wifionly_scan_notify(pAdapter);

}

/*
 * Notice
 *	Switch bandwitdth may change center frequency(channel)
 */
void hal_mpt_SetBandwidth(PADAPTER pAdapter)
{
	struct mp_priv *pmp = &pAdapter->mppriv;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	u8		channel = pmp->channel;
	u8		bandwidth = pmp->bandwidth;

	pHalData->bSwChnl = _TRUE;
	pHalData->bSetChnlBW = _TRUE;

	if (bandwidth == 2) {
		rtw_hal_set_chnl_bw(pAdapter, channel, bandwidth, HAL_PRIME_CHNL_OFFSET_LOWER, HAL_PRIME_CHNL_OFFSET_UPPER);
	} else if (bandwidth == 1) {
		rtw_hal_set_chnl_bw(pAdapter, channel, bandwidth, HAL_PRIME_CHNL_OFFSET_UPPER, 0);
	} else
		rtw_hal_set_chnl_bw(pAdapter, channel, bandwidth, pmp->prime_channel_offset, 0);

	hal_mpt_SwitchRfSetting(pAdapter);
	rtw_btcoex_wifionly_scan_notify(pAdapter);

}

void mpt_SetTxPower_Old(PADAPTER pAdapter, MPT_TXPWR_DEF Rate, u8 *pTxPower)
{
	switch (Rate) {
	case MPT_CCK: {
		u32	TxAGC = 0, pwr = 0;
		u8	rf;

		pwr = pTxPower[RF_PATH_A];
		if (pwr < 0x3f) {
			TxAGC = (pwr << 16) | (pwr << 8) | (pwr);
			phy_set_bb_reg(pAdapter, rTxAGC_A_CCK1_Mcs32, bMaskByte1, pTxPower[RF_PATH_A]);
			phy_set_bb_reg(pAdapter, rTxAGC_B_CCK11_A_CCK2_11, 0xffffff00, TxAGC);
		}
		pwr = pTxPower[RF_PATH_B];
		if (pwr < 0x3f) {
			TxAGC = (pwr << 16) | (pwr << 8) | (pwr);
			phy_set_bb_reg(pAdapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte0, pTxPower[RF_PATH_B]);
			phy_set_bb_reg(pAdapter, rTxAGC_B_CCK1_55_Mcs32, 0xffffff00, TxAGC);
		}
	}
	break;

	case MPT_OFDM_AND_HT: {
		u32	TxAGC = 0;
		u8	pwr = 0, rf;

		pwr = pTxPower[0];
		if (pwr < 0x3f) {
			TxAGC |= ((pwr << 24) | (pwr << 16) | (pwr << 8) | pwr);
			RTW_INFO("HT Tx-rf(A) Power = 0x%x\n", TxAGC);
			phy_set_bb_reg(pAdapter, rTxAGC_A_Rate18_06, bMaskDWord, TxAGC);
			phy_set_bb_reg(pAdapter, rTxAGC_A_Rate54_24, bMaskDWord, TxAGC);
			phy_set_bb_reg(pAdapter, rTxAGC_A_Mcs03_Mcs00, bMaskDWord, TxAGC);
			phy_set_bb_reg(pAdapter, rTxAGC_A_Mcs07_Mcs04, bMaskDWord, TxAGC);
			phy_set_bb_reg(pAdapter, rTxAGC_A_Mcs11_Mcs08, bMaskDWord, TxAGC);
			phy_set_bb_reg(pAdapter, rTxAGC_A_Mcs15_Mcs12, bMaskDWord, TxAGC);
		}
		TxAGC = 0;
		pwr = pTxPower[1];
		if (pwr < 0x3f) {
			TxAGC |= ((pwr << 24) | (pwr << 16) | (pwr << 8) | pwr);
			RTW_INFO("HT Tx-rf(B) Power = 0x%x\n", TxAGC);
			phy_set_bb_reg(pAdapter, rTxAGC_B_Rate18_06, bMaskDWord, TxAGC);
			phy_set_bb_reg(pAdapter, rTxAGC_B_Rate54_24, bMaskDWord, TxAGC);
			phy_set_bb_reg(pAdapter, rTxAGC_B_Mcs03_Mcs00, bMaskDWord, TxAGC);
			phy_set_bb_reg(pAdapter, rTxAGC_B_Mcs07_Mcs04, bMaskDWord, TxAGC);
			phy_set_bb_reg(pAdapter, rTxAGC_B_Mcs11_Mcs08, bMaskDWord, TxAGC);
			phy_set_bb_reg(pAdapter, rTxAGC_B_Mcs15_Mcs12, bMaskDWord, TxAGC);
		}
	}
	break;

	default:
		break;
	}
	RTW_INFO("<===mpt_SetTxPower_Old()\n");
}

void
mpt_SetTxPower(
	PADAPTER		pAdapter,
	MPT_TXPWR_DEF	Rate,
	u8 *pTxPower
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	u8 path = 0 , i = 0, MaxRate = MGN_6M;
	u8 StartPath = RF_PATH_A, EndPath = RF_PATH_B;

	if (IS_HARDWARE_TYPE_8814A(pAdapter))
		EndPath = RF_PATH_D;
	else if (IS_HARDWARE_TYPE_8188F(pAdapter) || IS_HARDWARE_TYPE_8188GTV(pAdapter)
		|| IS_HARDWARE_TYPE_8723D(pAdapter) || IS_HARDWARE_TYPE_8821C(pAdapter))
		EndPath = RF_PATH_A;

	switch (Rate) {
	case MPT_CCK: {
		u8 rate[] = {MGN_1M, MGN_2M, MGN_5_5M, MGN_11M};

		for (path = StartPath; path <= EndPath; path++)
			for (i = 0; i < sizeof(rate); ++i)
				PHY_SetTxPowerIndex(pAdapter, pTxPower[path], path, rate[i]);
	}
	break;
	case MPT_OFDM: {
		u8 rate[] = {
			MGN_6M, MGN_9M, MGN_12M, MGN_18M,
			MGN_24M, MGN_36M, MGN_48M, MGN_54M,
		};

		for (path = StartPath; path <= EndPath; path++)
			for (i = 0; i < sizeof(rate); ++i)
				PHY_SetTxPowerIndex(pAdapter, pTxPower[path], path, rate[i]);
	}
	break;
	case MPT_HT: {
		u8 rate[] = {
			MGN_MCS0, MGN_MCS1, MGN_MCS2, MGN_MCS3, MGN_MCS4,
			MGN_MCS5, MGN_MCS6, MGN_MCS7, MGN_MCS8, MGN_MCS9,
			MGN_MCS10, MGN_MCS11, MGN_MCS12, MGN_MCS13, MGN_MCS14,
			MGN_MCS15, MGN_MCS16, MGN_MCS17, MGN_MCS18, MGN_MCS19,
			MGN_MCS20, MGN_MCS21, MGN_MCS22, MGN_MCS23, MGN_MCS24,
			MGN_MCS25, MGN_MCS26, MGN_MCS27, MGN_MCS28, MGN_MCS29,
			MGN_MCS30, MGN_MCS31,
		};
		if (pHalData->rf_type == RF_3T3R)
			MaxRate = MGN_MCS23;
		else if (pHalData->rf_type == RF_2T2R)
			MaxRate = MGN_MCS15;
		else
			MaxRate = MGN_MCS7;
		for (path = StartPath; path <= EndPath; path++) {
			for (i = 0; i < sizeof(rate); ++i) {
				if (rate[i] > MaxRate)
					break;
				PHY_SetTxPowerIndex(pAdapter, pTxPower[path], path, rate[i]);
			}
		}
	}
	break;
	case MPT_VHT: {
		u8 rate[] = {
			MGN_VHT1SS_MCS0, MGN_VHT1SS_MCS1, MGN_VHT1SS_MCS2, MGN_VHT1SS_MCS3, MGN_VHT1SS_MCS4,
			MGN_VHT1SS_MCS5, MGN_VHT1SS_MCS6, MGN_VHT1SS_MCS7, MGN_VHT1SS_MCS8, MGN_VHT1SS_MCS9,
			MGN_VHT2SS_MCS0, MGN_VHT2SS_MCS1, MGN_VHT2SS_MCS2, MGN_VHT2SS_MCS3, MGN_VHT2SS_MCS4,
			MGN_VHT2SS_MCS5, MGN_VHT2SS_MCS6, MGN_VHT2SS_MCS7, MGN_VHT2SS_MCS8, MGN_VHT2SS_MCS9,
			MGN_VHT3SS_MCS0, MGN_VHT3SS_MCS1, MGN_VHT3SS_MCS2, MGN_VHT3SS_MCS3, MGN_VHT3SS_MCS4,
			MGN_VHT3SS_MCS5, MGN_VHT3SS_MCS6, MGN_VHT3SS_MCS7, MGN_VHT3SS_MCS8, MGN_VHT3SS_MCS9,
			MGN_VHT4SS_MCS0, MGN_VHT4SS_MCS1, MGN_VHT4SS_MCS2, MGN_VHT4SS_MCS3, MGN_VHT4SS_MCS4,
			MGN_VHT4SS_MCS5, MGN_VHT4SS_MCS6, MGN_VHT4SS_MCS7, MGN_VHT4SS_MCS8, MGN_VHT4SS_MCS9,
		};
		if (pHalData->rf_type == RF_3T3R)
			MaxRate = MGN_VHT3SS_MCS9;
		else if (pHalData->rf_type == RF_2T2R || pHalData->rf_type == RF_2T4R)
			MaxRate = MGN_VHT2SS_MCS9;
		else
			MaxRate = MGN_VHT1SS_MCS9;

		for (path = StartPath; path <= EndPath; path++) {
			for (i = 0; i < sizeof(rate); ++i) {
				if (rate[i] > MaxRate)
					break;
				PHY_SetTxPowerIndex(pAdapter, pTxPower[path], path, rate[i]);
			}
		}
	}
	break;
	default:
		RTW_INFO("<===mpt_SetTxPower: Illegal channel!!\n");
		break;
	}
}

void hal_mpt_SetTxPower(PADAPTER pAdapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT		pMptCtx = &(pAdapter->mppriv.mpt_ctx);
	struct dm_struct		*pDM_Odm = &pHalData->odmpriv;

	if (pHalData->rf_chip < RF_CHIP_MAX) {
		if (IS_HARDWARE_TYPE_8188E(pAdapter) ||
		    IS_HARDWARE_TYPE_8723B(pAdapter) ||
		    IS_HARDWARE_TYPE_8192E(pAdapter) ||
		    IS_HARDWARE_TYPE_8703B(pAdapter) ||
		    IS_HARDWARE_TYPE_8188F(pAdapter) ||
		    IS_HARDWARE_TYPE_8188GTV(pAdapter)
		) {
			u8 path = (pHalData->antenna_tx_path == ANTENNA_A) ? (RF_PATH_A) : (RF_PATH_B);

			RTW_INFO("===> MPT_ProSetTxPower: Old\n");

			mpt_SetTxPower_Old(pAdapter, MPT_CCK, pMptCtx->TxPwrLevel);
			mpt_SetTxPower_Old(pAdapter, MPT_OFDM_AND_HT, pMptCtx->TxPwrLevel);

		} else {

			mpt_SetTxPower(pAdapter, MPT_CCK, pMptCtx->TxPwrLevel);
			mpt_SetTxPower(pAdapter, MPT_OFDM, pMptCtx->TxPwrLevel);
			mpt_SetTxPower(pAdapter, MPT_HT, pMptCtx->TxPwrLevel);
			if(IS_HARDWARE_TYPE_JAGUAR_ALL(pAdapter)) {
				RTW_INFO("===> MPT_ProSetTxPower: Jaguar/Jaguar2\n");
				mpt_SetTxPower(pAdapter, MPT_VHT, pMptCtx->TxPwrLevel);
			}
		}

		rtw_hal_set_txpwr_done(pAdapter);
	} else
		RTW_INFO("RFChipID < RF_CHIP_MAX, the RF chip is not supported - %d\n", pHalData->rf_chip);

	odm_clear_txpowertracking_state(pDM_Odm);
}

void hal_mpt_SetDataRate(PADAPTER pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT		pMptCtx = &(pAdapter->mppriv.mpt_ctx);
	u32 DataRate;

	DataRate = mpt_to_mgnt_rate(pMptCtx->mpt_rate_index);

	hal_mpt_SwitchRfSetting(pAdapter);

	hal_mpt_CCKTxPowerAdjust(pAdapter, pHalData->bCCKinCH14);
#ifdef CONFIG_RTL8723B
	if (IS_HARDWARE_TYPE_8723B(pAdapter)) {
		if (IS_CCK_RATE(DataRate)) {
			if (pMptCtx->mpt_rf_path == RF_PATH_A)
				phy_set_rf_reg(pAdapter, RF_PATH_A, 0x51, 0xF, 0x6);
			else
				phy_set_rf_reg(pAdapter, RF_PATH_A, 0x71, 0xF, 0x6);
		} else {
			if (pMptCtx->mpt_rf_path == RF_PATH_A)
				phy_set_rf_reg(pAdapter, RF_PATH_A, 0x51, 0xF, 0xE);
			else
				phy_set_rf_reg(pAdapter, RF_PATH_A, 0x71, 0xF, 0xE);
		}
	}

	if ((IS_HARDWARE_TYPE_8723BS(pAdapter) &&
	     ((pHalData->PackageType == PACKAGE_TFBGA79) || (pHalData->PackageType == PACKAGE_TFBGA90)))) {
		if (pMptCtx->mpt_rf_path == RF_PATH_A)
			phy_set_rf_reg(pAdapter, RF_PATH_A, 0x51, 0xF, 0xE);
		else
			phy_set_rf_reg(pAdapter, RF_PATH_A, 0x71, 0xF, 0xE);
	}
#endif
}

#define RF_PATH_AB	22

#ifdef CONFIG_RTL8814A
void mpt_ToggleIG_8814A(PADAPTER	pAdapter)
{
	u8 Path;
	u32 IGReg = rA_IGI_Jaguar, IGvalue = 0;

	for (Path = 0; Path <= RF_PATH_D; Path++) {
		switch (Path) {
		case RF_PATH_B:
			IGReg = rB_IGI_Jaguar;
			break;
		case RF_PATH_C:
			IGReg = rC_IGI_Jaguar2;
			break;
		case RF_PATH_D:
			IGReg = rD_IGI_Jaguar2;
			break;
		default:
			IGReg = rA_IGI_Jaguar;
			break;
		}

		IGvalue = phy_query_bb_reg(pAdapter, IGReg, bMaskByte0);
		phy_set_bb_reg(pAdapter, IGReg, bMaskByte0, IGvalue + 2);
		phy_set_bb_reg(pAdapter, IGReg, bMaskByte0, IGvalue);
	}
}

void mpt_SetRFPath_8814A(PADAPTER	pAdapter)
{

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT	pMptCtx = &pAdapter->mppriv.mpt_ctx;
	R_ANTENNA_SELECT_OFDM	*p_ofdm_tx;	/* OFDM Tx register */
	R_ANTENNA_SELECT_CCK	*p_cck_txrx;
	u8	ForcedDataRate = mpt_to_mgnt_rate(pMptCtx->mpt_rate_index);
	/*/PRT_HIGH_THROUGHPUT		pHTInfo = GET_HT_INFO(pMgntInfo);*/
	/*/PRT_VERY_HIGH_THROUGHPUT	pVHTInfo = GET_VHT_INFO(pMgntInfo);*/

	u32	ulAntennaTx = pHalData->antenna_tx_path;
	u32	ulAntennaRx = pHalData->AntennaRxPath;
	u8	NssforRate = MgntQuery_NssTxRate(ForcedDataRate);

	if (NssforRate == RF_3TX) {
		RTW_INFO("===> SetAntenna 3T Rate ForcedDataRate %d NssforRate %d AntennaTx %d\n", ForcedDataRate, NssforRate, ulAntennaTx);

		switch (ulAntennaTx) {
		case ANTENNA_BCD:
			pMptCtx->mpt_rf_path = RF_PATH_BCD;
			/*pHalData->ValidTxPath = 0x0e;*/
			phy_set_bb_reg(pAdapter, rTxAnt_23Nsts_Jaguar2, 0x0fff0000, 0x90e);	/*/ 0x940[27:16]=12'b0010_0100_0111*/
			break;

		case ANTENNA_ABC:
		default:
			pMptCtx->mpt_rf_path = RF_PATH_ABC;
			/*pHalData->ValidTxPath = 0x0d;*/
			phy_set_bb_reg(pAdapter, rTxAnt_23Nsts_Jaguar2, 0x0fff0000, 0x247);	/*/ 0x940[27:16]=12'b0010_0100_0111*/
			break;
		}

	} else { /*/if(NssforRate == RF_1TX)*/
		RTW_INFO("===> SetAntenna for 1T/2T Rate, ForcedDataRate %d NssforRate %d AntennaTx %d\n", ForcedDataRate, NssforRate, ulAntennaTx);
		switch (ulAntennaTx) {
		case ANTENNA_BCD:
			pMptCtx->mpt_rf_path = RF_PATH_BCD;
			/*pHalData->ValidTxPath = 0x0e;*/
			phy_set_bb_reg(pAdapter, rCCK_RX_Jaguar, 0xf0000000, 0x7);
			phy_set_bb_reg(pAdapter, rTxAnt_1Nsts_Jaguar2, 0x000f00000, 0xe);
			phy_set_bb_reg(pAdapter, rTxPath_Jaguar, 0xf0, 0xe);
			break;

		case ANTENNA_BC:
			pMptCtx->mpt_rf_path = RF_PATH_BC;
			/*pHalData->ValidTxPath = 0x06;*/
			phy_set_bb_reg(pAdapter, rCCK_RX_Jaguar, 0xf0000000, 0x6);
			phy_set_bb_reg(pAdapter, rTxAnt_1Nsts_Jaguar2, 0x000f00000, 0x6);
			phy_set_bb_reg(pAdapter, rTxPath_Jaguar, 0xf0, 0x6);
			break;
		case ANTENNA_B:
			pMptCtx->mpt_rf_path = RF_PATH_B;
			/*pHalData->ValidTxPath = 0x02;*/
			phy_set_bb_reg(pAdapter, rCCK_RX_Jaguar, 0xf0000000, 0x4);			/*/ 0xa07[7:4] = 4'b0100*/
			phy_set_bb_reg(pAdapter, rTxAnt_1Nsts_Jaguar2, 0xfff00000, 0x002);	/*/ 0x93C[31:20]=12'b0000_0000_0010*/
			phy_set_bb_reg(pAdapter, rTxPath_Jaguar, 0xf0, 0x2);					/* 0x80C[7:4] = 4'b0010*/
			break;

		case ANTENNA_C:
			pMptCtx->mpt_rf_path = RF_PATH_C;
			/*pHalData->ValidTxPath = 0x04;*/
			phy_set_bb_reg(pAdapter, rCCK_RX_Jaguar, 0xf0000000, 0x2);			/*/ 0xa07[7:4] = 4'b0010*/
			phy_set_bb_reg(pAdapter, rTxAnt_1Nsts_Jaguar2, 0xfff00000, 0x004);	/*/ 0x93C[31:20]=12'b0000_0000_0100*/
			phy_set_bb_reg(pAdapter, rTxPath_Jaguar, 0xf0, 0x4);					/*/ 0x80C[7:4] = 4'b0100*/
			break;

		case ANTENNA_D:
			pMptCtx->mpt_rf_path = RF_PATH_D;
			/*pHalData->ValidTxPath = 0x08;*/
			phy_set_bb_reg(pAdapter, rCCK_RX_Jaguar, 0xf0000000, 0x1);			/*/ 0xa07[7:4] = 4'b0001*/
			phy_set_bb_reg(pAdapter, rTxAnt_1Nsts_Jaguar2, 0xfff00000, 0x008);	/*/ 0x93C[31:20]=12'b0000_0000_1000*/
			phy_set_bb_reg(pAdapter, rTxPath_Jaguar, 0xf0, 0x8);					/*/ 0x80C[7:4] = 4'b1000*/
			break;

		case ANTENNA_A:
		default:
			pMptCtx->mpt_rf_path = RF_PATH_A;
			/*pHalData->ValidTxPath = 0x01;*/
			phy_set_bb_reg(pAdapter, rCCK_RX_Jaguar, 0xf0000000, 0x8);			/*/ 0xa07[7:4] = 4'b1000*/
			phy_set_bb_reg(pAdapter, rTxAnt_1Nsts_Jaguar2, 0xfff00000, 0x001);	/*/ 0x93C[31:20]=12'b0000_0000_0001*/
			phy_set_bb_reg(pAdapter, rTxPath_Jaguar, 0xf0, 0x1);					/*/ 0x80C[7:4] = 4'b0001*/
			break;
		}
	}

	switch (ulAntennaRx) {
	case ANTENNA_A:
		/*pHalData->ValidRxPath = 0x01;*/
		phy_set_bb_reg(pAdapter, rCCAonSec_Jaguar, BIT1, 0x1);
		phy_set_bb_reg(pAdapter, 0x1000, bMaskByte2, 0x2);
		phy_set_bb_reg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0x11);
		phy_set_bb_reg(pAdapter, 0x1000, bMaskByte2, 0x3);
		phy_set_bb_reg(pAdapter, rCCAonSec_Jaguar, BIT1, 0x0);
		phy_set_bb_reg(pAdapter, rCCK_RX_Jaguar, 0x0C000000, 0x0);
		phy_set_rf_reg(pAdapter, RF_PATH_A, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_A_0x0[19:16] = 3, RX mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_B, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_B_0x0[19:16] = 1, Standby mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_C, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_C_0x0[19:16] = 1, Standby mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_D, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_D_0x0[19:16] = 1, Standby mode*/
		/*/ CCA related PD_delay_th*/
		phy_set_bb_reg(pAdapter, rAGC_table_Jaguar, 0x0F000000, 0x5);
		phy_set_bb_reg(pAdapter, rPwed_TH_Jaguar, 0x0000000F, 0xA);
		break;

	case ANTENNA_B:
		/*pHalData->ValidRxPath = 0x02;*/
		phy_set_bb_reg(pAdapter, rCCAonSec_Jaguar, BIT1, 0x1);
		phy_set_bb_reg(pAdapter, 0x1000, bMaskByte2, 0x2);
		phy_set_bb_reg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0x22);
		phy_set_bb_reg(pAdapter, 0x1000, bMaskByte2, 0x3);
		phy_set_bb_reg(pAdapter, rCCAonSec_Jaguar, BIT1, 0x0);
		phy_set_bb_reg(pAdapter, rCCK_RX_Jaguar, 0x0C000000, 0x1);
		phy_set_rf_reg(pAdapter, RF_PATH_A, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_A_0x0[19:16] = 1, Standby mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_B, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_B_0x0[19:16] = 3, RX mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_C, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_C_0x0[19:16] = 1, Standby mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_D, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_D_0x0[19:16] = 1, Standby mode*/
		/*/ CCA related PD_delay_th*/
		phy_set_bb_reg(pAdapter, rAGC_table_Jaguar, 0x0F000000, 0x5);
		phy_set_bb_reg(pAdapter, rPwed_TH_Jaguar, 0x0000000F, 0xA);
		break;

	case ANTENNA_C:
		/*pHalData->ValidRxPath = 0x04;*/
		phy_set_bb_reg(pAdapter, rCCAonSec_Jaguar, BIT1, 0x1);
		phy_set_bb_reg(pAdapter, 0x1000, bMaskByte2, 0x2);
		phy_set_bb_reg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0x44);
		phy_set_bb_reg(pAdapter, 0x1000, bMaskByte2, 0x3);
		phy_set_bb_reg(pAdapter, rCCAonSec_Jaguar, BIT1, 0x0);
		phy_set_bb_reg(pAdapter, rCCK_RX_Jaguar, 0x0C000000, 0x2);
		phy_set_rf_reg(pAdapter, RF_PATH_A, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_A_0x0[19:16] = 1, Standby mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_B, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_B_0x0[19:16] = 1, Standby mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_C, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_C_0x0[19:16] = 3, RX mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_D, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_D_0x0[19:16] = 1, Standby mode*/
		/*/ CCA related PD_delay_th*/
		phy_set_bb_reg(pAdapter, rAGC_table_Jaguar, 0x0F000000, 0x5);
		phy_set_bb_reg(pAdapter, rPwed_TH_Jaguar, 0x0000000F, 0xA);
		break;

	case ANTENNA_D:
		/*pHalData->ValidRxPath = 0x08;*/
		phy_set_bb_reg(pAdapter, rCCAonSec_Jaguar, BIT1, 0x1);
		phy_set_bb_reg(pAdapter, 0x1000, bMaskByte2, 0x2);
		phy_set_bb_reg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0x88);
		phy_set_bb_reg(pAdapter, 0x1000, bMaskByte2, 0x3);
		phy_set_bb_reg(pAdapter, rCCAonSec_Jaguar, BIT1, 0x0);
		phy_set_bb_reg(pAdapter, rCCK_RX_Jaguar, 0x0C000000, 0x3);
		phy_set_rf_reg(pAdapter, RF_PATH_A, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_A_0x0[19:16] = 1, Standby mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_B, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_B_0x0[19:16] = 1, Standby mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_C, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_C_0x0[19:16] = 1, Standby mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_D, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_D_0x0[19:16] = 3, RX mode*/
		/*/ CCA related PD_delay_th*/
		phy_set_bb_reg(pAdapter, rAGC_table_Jaguar, 0x0F000000, 0x5);
		phy_set_bb_reg(pAdapter, rPwed_TH_Jaguar, 0x0000000F, 0xA);
		break;

	case ANTENNA_BC:
		/*pHalData->ValidRxPath = 0x06;*/
		phy_set_bb_reg(pAdapter, rCCAonSec_Jaguar, BIT1, 0x1);
		phy_set_bb_reg(pAdapter, 0x1000, bMaskByte2, 0x2);
		phy_set_bb_reg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0x66);
		phy_set_bb_reg(pAdapter, 0x1000, bMaskByte2, 0x3);
		phy_set_bb_reg(pAdapter, rCCAonSec_Jaguar, BIT1, 0x0);
		phy_set_bb_reg(pAdapter, rCCK_RX_Jaguar, 0x0f000000, 0x6);
		phy_set_rf_reg(pAdapter, RF_PATH_A, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_A_0x0[19:16] = 1, Standby mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_B, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_B_0x0[19:16] = 3, RX mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_C, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_C_0x0[19:16] = 3, Rx mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_D, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_D_0x0[19:16] = 1, Standby mode*/
		/*/ CCA related PD_delay_th*/
		phy_set_bb_reg(pAdapter, rAGC_table_Jaguar, 0x0F000000, 0x5);
		phy_set_bb_reg(pAdapter, rPwed_TH_Jaguar, 0x0000000F, 0xA);
		break;

	case ANTENNA_CD:
		/*pHalData->ValidRxPath = 0x0C;*/
		phy_set_bb_reg(pAdapter, rCCAonSec_Jaguar, BIT1, 0x1);
		phy_set_bb_reg(pAdapter, 0x1000, bMaskByte2, 0x2);
		phy_set_bb_reg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0xcc);
		phy_set_bb_reg(pAdapter, 0x1000, bMaskByte2, 0x3);
		phy_set_bb_reg(pAdapter, rCCAonSec_Jaguar, BIT1, 0x0);
		phy_set_bb_reg(pAdapter, rCCK_RX_Jaguar, 0x0f000000, 0xB);
		phy_set_rf_reg(pAdapter, RF_PATH_A, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_A_0x0[19:16] = 1, Standby mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_B, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_B_0x0[19:16] = 1, Standby mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_C, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_C_0x0[19:16] = 3, Rx mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_D, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_D_0x0[19:16] = 3, RX mode*/
		/*/ CCA related PD_delay_th*/
		phy_set_bb_reg(pAdapter, rAGC_table_Jaguar, 0x0F000000, 0x5);
		phy_set_bb_reg(pAdapter, rPwed_TH_Jaguar, 0x0000000F, 0xA);
		break;

	case ANTENNA_BCD:
		/*pHalData->ValidRxPath = 0x0e;*/
		phy_set_bb_reg(pAdapter, rCCAonSec_Jaguar, BIT1, 0x1);
		phy_set_bb_reg(pAdapter, 0x1000, bMaskByte2, 0x2);
		phy_set_bb_reg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0xee);
		phy_set_bb_reg(pAdapter, 0x1000, bMaskByte2, 0x3);
		phy_set_bb_reg(pAdapter, rCCAonSec_Jaguar, BIT1, 0x0);
		phy_set_bb_reg(pAdapter, rCCK_RX_Jaguar, 0x0f000000, 0x6);
		phy_set_rf_reg(pAdapter, RF_PATH_A, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_A_0x0[19:16] = 1, Standby mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_B, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_B_0x0[19:16] = 3, RX mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_C, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_C_0x0[19:16] = 3, RX mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_D, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_D_0x0[19:16] = 3, Rx mode*/
		/*/ CCA related PD_delay_th*/
		phy_set_bb_reg(pAdapter, rAGC_table_Jaguar, 0x0F000000, 0x3);
		phy_set_bb_reg(pAdapter, rPwed_TH_Jaguar, 0x0000000F, 0x8);
		break;

	case ANTENNA_ABCD:
		/*pHalData->ValidRxPath = 0x0f;*/
		phy_set_bb_reg(pAdapter, rCCAonSec_Jaguar, BIT1, 0x1);
		phy_set_bb_reg(pAdapter, 0x1000, bMaskByte2, 0x2);
		phy_set_bb_reg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0xff);
		phy_set_bb_reg(pAdapter, 0x1000, bMaskByte2, 0x3);
		phy_set_bb_reg(pAdapter, rCCAonSec_Jaguar, BIT1, 0x0);
		phy_set_bb_reg(pAdapter, rCCK_RX_Jaguar, 0x0f000000, 0x1);
		phy_set_rf_reg(pAdapter, RF_PATH_A, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_A_0x0[19:16] = 3, RX mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_B, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_B_0x0[19:16] = 3, RX mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_C, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_C_0x0[19:16] = 3, RX mode*/
		phy_set_rf_reg(pAdapter, RF_PATH_D, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_D_0x0[19:16] = 3, RX mode*/
		/*/ CCA related PD_delay_th*/
		phy_set_bb_reg(pAdapter, rAGC_table_Jaguar, 0x0F000000, 0x3);
		phy_set_bb_reg(pAdapter, rPwed_TH_Jaguar, 0x0000000F, 0x8);
		break;

	default:
		break;
	}

	PHY_Set_SecCCATH_by_RXANT_8814A(pAdapter, ulAntennaRx);

	mpt_ToggleIG_8814A(pAdapter);
}
#endif /* CONFIG_RTL8814A */
#if defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C)  || defined(CONFIG_RTL8822C)
void
mpt_SetSingleTone_8814A(
		PADAPTER	pAdapter,
		BOOLEAN	bSingleTone,
		BOOLEAN	bEnPMacTx)
{

	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.mpt_ctx);
	u8 StartPath = RF_PATH_A,  EndPath = RF_PATH_A, path;
	static u32		regIG0 = 0, regIG1 = 0, regIG2 = 0, regIG3 = 0;

	if (bSingleTone) {
		regIG0 = phy_query_bb_reg(pAdapter, rA_TxScale_Jaguar, bMaskDWord);		/*/ 0xC1C[31:21]*/
		regIG1 = phy_query_bb_reg(pAdapter, rB_TxScale_Jaguar, bMaskDWord);		/*/ 0xE1C[31:21]*/
		regIG2 = phy_query_bb_reg(pAdapter, rC_TxScale_Jaguar2, bMaskDWord);	/*/ 0x181C[31:21]*/
		regIG3 = phy_query_bb_reg(pAdapter, rD_TxScale_Jaguar2, bMaskDWord);	/*/ 0x1A1C[31:21]*/

		switch (pMptCtx->mpt_rf_path) {
		case RF_PATH_A:
		case RF_PATH_B:
		case RF_PATH_C:
		case RF_PATH_D:
			StartPath = pMptCtx->mpt_rf_path;
			EndPath = pMptCtx->mpt_rf_path;
			break;
		case RF_PATH_AB:
			EndPath = RF_PATH_B;
			break;
		case RF_PATH_BC:
			StartPath = RF_PATH_B;
			EndPath = RF_PATH_C;
			break;
		case RF_PATH_ABC:
			EndPath = RF_PATH_C;
			break;
		case RF_PATH_BCD:
			StartPath = RF_PATH_B;
			EndPath = RF_PATH_D;
			break;
		case RF_PATH_ABCD:
			EndPath = RF_PATH_D;
			break;
		}

		if (bEnPMacTx == FALSE) {
			hal_mpt_SetContinuousTx(pAdapter, _TRUE);
			issue_nulldata(pAdapter, NULL, 1, 3, 500);
		}

		phy_set_bb_reg(pAdapter, rCCAonSec_Jaguar, BIT1, 0x1); /*/ Disable CCA*/

		for (path = StartPath; path <= EndPath; path++) {
			phy_set_rf_reg(pAdapter, path, RF_AC_Jaguar, 0xF0000, 0x2); /*/ Tx mode: RF0x00[19:16]=4'b0010 */
			phy_set_rf_reg(pAdapter, path, RF_AC_Jaguar, 0x1F, 0x0); /*/ Lowest RF gain index: RF_0x0[4:0] = 0*/

			phy_set_rf_reg(pAdapter, path, lna_low_gain_3, BIT1, 0x1); /*/ RF LO enabled*/
		}

		phy_set_bb_reg(pAdapter, rA_TxScale_Jaguar, 0xFFE00000, 0); /*/ 0xC1C[31:21]*/
		phy_set_bb_reg(pAdapter, rB_TxScale_Jaguar, 0xFFE00000, 0); /*/ 0xE1C[31:21]*/
		phy_set_bb_reg(pAdapter, rC_TxScale_Jaguar2, 0xFFE00000, 0); /*/ 0x181C[31:21]*/
		phy_set_bb_reg(pAdapter, rD_TxScale_Jaguar2, 0xFFE00000, 0); /*/ 0x1A1C[31:21]*/
	} else {
		switch (pMptCtx->mpt_rf_path) {
		case RF_PATH_A:
		case RF_PATH_B:
		case RF_PATH_C:
		case RF_PATH_D:
			StartPath = pMptCtx->mpt_rf_path;
			EndPath = pMptCtx->mpt_rf_path;
			break;
		case RF_PATH_AB:
			EndPath = RF_PATH_B;
			break;
		case RF_PATH_BC:
			StartPath = RF_PATH_B;
			EndPath = RF_PATH_C;
			break;
		case RF_PATH_ABC:
			EndPath = RF_PATH_C;
			break;
		case RF_PATH_BCD:
			StartPath = RF_PATH_B;
			EndPath = RF_PATH_D;
			break;
		case RF_PATH_ABCD:
			EndPath = RF_PATH_D;
			break;
		}
		for (path = StartPath; path <= EndPath; path++)
			phy_set_rf_reg(pAdapter, path, lna_low_gain_3, BIT1, 0x0); /* RF LO disabled */

		phy_set_bb_reg(pAdapter, rCCAonSec_Jaguar, BIT1, 0x0); /* Enable CCA*/

		if (bEnPMacTx == FALSE)
			hal_mpt_SetContinuousTx(pAdapter, _FALSE);

		phy_set_bb_reg(pAdapter, rA_TxScale_Jaguar, bMaskDWord, regIG0); /* 0xC1C[31:21]*/
		phy_set_bb_reg(pAdapter, rB_TxScale_Jaguar, bMaskDWord, regIG1); /* 0xE1C[31:21]*/
		phy_set_bb_reg(pAdapter, rC_TxScale_Jaguar2, bMaskDWord, regIG2); /* 0x181C[31:21]*/
		phy_set_bb_reg(pAdapter, rD_TxScale_Jaguar2, bMaskDWord, regIG3); /* 0x1A1C[31:21]*/
	}
}

#endif

#if	defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A)
void mpt_SetRFPath_8812A(PADAPTER pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT	pMptCtx = &pAdapter->mppriv.mpt_ctx;
	struct mp_priv *pmp = &pAdapter->mppriv;
	u8		channel = pmp->channel;
	u8		bandwidth = pmp->bandwidth;
	u8		eLNA_2g = pHalData->ExternalLNA_2G;
	u32		ulAntennaTx, ulAntennaRx;

	ulAntennaTx = pHalData->antenna_tx_path;
	ulAntennaRx = pHalData->AntennaRxPath;

	switch (ulAntennaTx) {
	case ANTENNA_A:
		pMptCtx->mpt_rf_path = RF_PATH_A;
		phy_set_bb_reg(pAdapter, rTxPath_Jaguar, bMaskLWord, 0x1111);
		if (pHalData->rfe_type == 3 && IS_HARDWARE_TYPE_8812(pAdapter))
			phy_set_bb_reg(pAdapter, r_ANTSEL_SW_Jaguar, bMask_AntselPathFollow_Jaguar, 0x0);
		break;
	case ANTENNA_B:
		pMptCtx->mpt_rf_path = RF_PATH_B;
		phy_set_bb_reg(pAdapter, rTxPath_Jaguar, bMaskLWord, 0x2222);
		if (pHalData->rfe_type == 3 && IS_HARDWARE_TYPE_8812(pAdapter))
			phy_set_bb_reg(pAdapter,	r_ANTSEL_SW_Jaguar, bMask_AntselPathFollow_Jaguar, 0x1);
		break;
	case ANTENNA_AB:
		pMptCtx->mpt_rf_path = RF_PATH_AB;
		phy_set_bb_reg(pAdapter, rTxPath_Jaguar, bMaskLWord, 0x3333);
		if (pHalData->rfe_type == 3 && IS_HARDWARE_TYPE_8812(pAdapter))
			phy_set_bb_reg(pAdapter, r_ANTSEL_SW_Jaguar, bMask_AntselPathFollow_Jaguar, 0x0);
		break;
	default:
		pMptCtx->mpt_rf_path = RF_PATH_AB;
		RTW_INFO("Unknown Tx antenna.\n");
		break;
	}

	switch (ulAntennaRx) {
		u32 reg0xC50 = 0;
	case ANTENNA_A:
		phy_set_bb_reg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0x11);
		phy_set_rf_reg(pAdapter, RF_PATH_B, RF_AC_Jaguar, 0xF0000, 0x1); /*/ RF_B_0x0[19:16] = 1, Standby mode*/
		phy_set_bb_reg(pAdapter, rCCK_RX_Jaguar, bCCK_RX_Jaguar, 0x0);
		phy_set_rf_reg(pAdapter, RF_PATH_A, RF_AC_Jaguar, BIT19 | BIT18 | BIT17 | BIT16, 0x3);

		/*/ <20121101, Kordan> To prevent gain table from not switched, asked by Ynlin.*/
		reg0xC50 = phy_query_bb_reg(pAdapter, rA_IGI_Jaguar, bMaskByte0);
		phy_set_bb_reg(pAdapter, rA_IGI_Jaguar, bMaskByte0, reg0xC50 + 2);
		phy_set_bb_reg(pAdapter, rA_IGI_Jaguar, bMaskByte0, reg0xC50);

		/* set PWED_TH for BB Yn user guide R29 */
		if (IS_HARDWARE_TYPE_8812(pAdapter)) {
			if (channel <= 14) { /* 2.4G */
				if (bandwidth == CHANNEL_WIDTH_20
				    && eLNA_2g == 0) {
					/* 0x830[3:1]=3'b010 */
					phy_set_bb_reg(pAdapter, rPwed_TH_Jaguar, BIT1 | BIT2 | BIT3, 0x02);
				} else
					/* 0x830[3:1]=3'b100 */
					phy_set_bb_reg(pAdapter, rPwed_TH_Jaguar, BIT1 | BIT2 | BIT3, 0x04);
			} else
				/* 0x830[3:1]=3'b100 for 5G */
				phy_set_bb_reg(pAdapter, rPwed_TH_Jaguar, BIT1 | BIT2 | BIT3, 0x04);
		}
		break;
	case ANTENNA_B:
		phy_set_bb_reg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0x22);
		phy_set_rf_reg(pAdapter, RF_PATH_A, RF_AC_Jaguar, 0xF0000, 0x1);/*/ RF_A_0x0[19:16] = 1, Standby mode */
		phy_set_bb_reg(pAdapter, rCCK_RX_Jaguar, bCCK_RX_Jaguar, 0x1);
		phy_set_rf_reg(pAdapter, RF_PATH_B, RF_AC_Jaguar, BIT19 | BIT18 | BIT17 | BIT16, 0x3);

		/*/ <20121101, Kordan> To prevent gain table from not switched, asked by Ynlin.*/
		reg0xC50 = phy_query_bb_reg(pAdapter, rB_IGI_Jaguar, bMaskByte0);
		phy_set_bb_reg(pAdapter, rB_IGI_Jaguar, bMaskByte0, reg0xC50 + 2);
		phy_set_bb_reg(pAdapter, rB_IGI_Jaguar, bMaskByte0, reg0xC50);

		/* set PWED_TH for BB Yn user guide R29 */
		if (IS_HARDWARE_TYPE_8812(pAdapter)) {
			if (channel <= 14) {
				if (bandwidth == CHANNEL_WIDTH_20
				    && eLNA_2g == 0) {
					/* 0x830[3:1]=3'b010 */
					phy_set_bb_reg(pAdapter, rPwed_TH_Jaguar, BIT1 | BIT2 | BIT3, 0x02);
				} else
					/* 0x830[3:1]=3'b100 */
					phy_set_bb_reg(pAdapter, rPwed_TH_Jaguar, BIT1 | BIT2 | BIT3, 0x04);
			} else
				/* 0x830[3:1]=3'b100 for 5G */
				phy_set_bb_reg(pAdapter, rPwed_TH_Jaguar, BIT1 | BIT2 | BIT3, 0x04);
		}
		break;
	case ANTENNA_AB:
		phy_set_bb_reg(pAdapter, rRxPath_Jaguar, bMaskByte0, 0x33);
		phy_set_rf_reg(pAdapter, RF_PATH_B, RF_AC_Jaguar, 0xF0000, 0x3); /*/ RF_B_0x0[19:16] = 3, Rx mode*/
		phy_set_bb_reg(pAdapter, rCCK_RX_Jaguar, bCCK_RX_Jaguar, 0x0);
		/* set PWED_TH for BB Yn user guide R29 */
		phy_set_bb_reg(pAdapter, rPwed_TH_Jaguar, BIT1 | BIT2 | BIT3, 0x04);
		break;
	default:
		RTW_INFO("Unknown Rx antenna.\n");
		break;
	}

	if (pHalData->rfe_type == 5 || pHalData->rfe_type == 1) {
		if (ulAntennaTx == ANTENNA_A || ulAntennaTx == ANTENNA_AB) {
			/* WiFi */
			phy_set_bb_reg(pAdapter, r_ANTSEL_SW_Jaguar, BIT(1) | BIT(0), 0x2);
			phy_set_bb_reg(pAdapter, r_ANTSEL_SW_Jaguar, BIT(9) | BIT(8), 0x3);
		} else {
			/* BT */
			phy_set_bb_reg(pAdapter, r_ANTSEL_SW_Jaguar, BIT(1) | BIT(0), 0x1);
			phy_set_bb_reg(pAdapter, r_ANTSEL_SW_Jaguar, BIT(9) | BIT(8), 0x3);
		}
	}
}
#endif

#ifdef CONFIG_RTL8723B
void mpt_SetRFPath_8723B(PADAPTER pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u32		ulAntennaTx, ulAntennaRx;
	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.mpt_ctx);
	struct dm_struct	*pDM_Odm = &pHalData->odmpriv;
	struct dm_rf_calibration_struct	*pRFCalibrateInfo = &(pDM_Odm->rf_calibrate_info);

	ulAntennaTx = pHalData->antenna_tx_path;
	ulAntennaRx = pHalData->AntennaRxPath;

	if (pHalData->rf_chip >= RF_CHIP_MAX) {
		RTW_INFO("This RF chip ID is not supported\n");
		return;
	}

	switch (pAdapter->mppriv.antenna_tx) {
		u8 p = 0, i = 0;
	case ANTENNA_A: { /*/ Actually path S1  (Wi-Fi)*/
		pMptCtx->mpt_rf_path = RF_PATH_A;
		phy_set_bb_reg(pAdapter, rS0S1_PathSwitch, BIT9 | BIT8 | BIT7, 0x0);
		phy_set_bb_reg(pAdapter, 0xB2C, BIT31, 0x0); /* AGC Table Sel*/

		for (i = 0; i < 3; ++i) {
			u32 offset = pRFCalibrateInfo->tx_iqc_8723b[RF_PATH_A][i][0];
			u32 data = pRFCalibrateInfo->tx_iqc_8723b[RF_PATH_A][i][1];

			if (offset != 0) {
				phy_set_bb_reg(pAdapter, offset, bMaskDWord, data);
				RTW_INFO("Switch to S1 TxIQC(offset, data) = (0x%X, 0x%X)\n", offset, data);
			}
		}
		for (i = 0; i < 2; ++i) {
			u32 offset = pRFCalibrateInfo->rx_iqc_8723b[RF_PATH_A][i][0];
			u32 data = pRFCalibrateInfo->rx_iqc_8723b[RF_PATH_A][i][1];

			if (offset != 0) {
				phy_set_bb_reg(pAdapter, offset, bMaskDWord, data);
				RTW_INFO("Switch to S1 RxIQC (offset, data) = (0x%X, 0x%X)\n", offset, data);
			}
		}
	}
	break;
	case ANTENNA_B: { /*/ Actually path S0 (BT)*/
		u32 offset;
		u32 data;

		pMptCtx->mpt_rf_path = RF_PATH_B;
		phy_set_bb_reg(pAdapter, rS0S1_PathSwitch, BIT9 | BIT8 | BIT7, 0x5);
		phy_set_bb_reg(pAdapter, 0xB2C, BIT31, 0x1); /*/ AGC Table Sel.*/

		for (i = 0; i < 3; ++i) {
			/*/ <20130603, Kordan> Because BB suppors only 1T1R, we restore IQC  to S1 instead of S0.*/
			offset = pRFCalibrateInfo->tx_iqc_8723b[RF_PATH_A][i][0];
			data = pRFCalibrateInfo->tx_iqc_8723b[RF_PATH_B][i][1];
			if (pRFCalibrateInfo->tx_iqc_8723b[RF_PATH_B][i][0] != 0) {
				phy_set_bb_reg(pAdapter, offset, bMaskDWord, data);
				RTW_INFO("Switch to S0 TxIQC (offset, data) = (0x%X, 0x%X)\n", offset, data);
			}
		}
		/*/ <20130603, Kordan> Because BB suppors only 1T1R, we restore IQC to S1 instead of S0.*/
		for (i = 0; i < 2; ++i) {
			offset = pRFCalibrateInfo->rx_iqc_8723b[RF_PATH_A][i][0];
			data = pRFCalibrateInfo->rx_iqc_8723b[RF_PATH_B][i][1];
			if (pRFCalibrateInfo->rx_iqc_8723b[RF_PATH_B][i][0] != 0) {
				phy_set_bb_reg(pAdapter, offset, bMaskDWord, data);
				RTW_INFO("Switch to S0 RxIQC (offset, data) = (0x%X, 0x%X)\n", offset, data);
			}
		}
	}
	break;
	default:
		pMptCtx->mpt_rf_path = RF_PATH_AB;
		break;
	}
}
#endif

#ifdef CONFIG_RTL8703B
void mpt_SetRFPath_8703B(PADAPTER pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u32					ulAntennaTx, ulAntennaRx;
	PMPT_CONTEXT		pMptCtx = &(pAdapter->mppriv.mpt_ctx);
	struct dm_struct		*pDM_Odm = &pHalData->odmpriv;
	struct dm_rf_calibration_struct			*pRFCalibrateInfo = &(pDM_Odm->rf_calibrate_info);

	ulAntennaTx = pHalData->antenna_tx_path;
	ulAntennaRx = pHalData->AntennaRxPath;

	if (pHalData->rf_chip >= RF_CHIP_MAX) {
		RTW_INFO("This RF chip ID is not supported\n");
		return;
	}

	switch (pAdapter->mppriv.antenna_tx) {
		u8 p = 0, i = 0;

	case ANTENNA_A: { /* Actually path S1  (Wi-Fi) */
		pMptCtx->mpt_rf_path = RF_PATH_A;
		phy_set_bb_reg(pAdapter, rS0S1_PathSwitch, BIT9 | BIT8 | BIT7, 0x0);
		phy_set_bb_reg(pAdapter, 0xB2C, BIT31, 0x0); /* AGC Table Sel*/

		for (i = 0; i < 3; ++i) {
			u32 offset = pRFCalibrateInfo->tx_iqc_8703b[i][0];
			u32 data = pRFCalibrateInfo->tx_iqc_8703b[i][1];

			if (offset != 0) {
				phy_set_bb_reg(pAdapter, offset, bMaskDWord, data);
				RTW_INFO("Switch to S1 TxIQC(offset, data) = (0x%X, 0x%X)\n", offset, data);
			}

		}
		for (i = 0; i < 2; ++i) {
			u32 offset = pRFCalibrateInfo->rx_iqc_8703b[i][0];
			u32 data = pRFCalibrateInfo->rx_iqc_8703b[i][1];

			if (offset != 0) {
				phy_set_bb_reg(pAdapter, offset, bMaskDWord, data);
				RTW_INFO("Switch to S1 RxIQC (offset, data) = (0x%X, 0x%X)\n", offset, data);
			}
		}
	}
	break;
	case ANTENNA_B: { /* Actually path S0 (BT)*/
		pMptCtx->mpt_rf_path = RF_PATH_B;
		phy_set_bb_reg(pAdapter, rS0S1_PathSwitch, BIT9 | BIT8 | BIT7, 0x5);
		phy_set_bb_reg(pAdapter, 0xB2C, BIT31, 0x1); /* AGC Table Sel */

		for (i = 0; i < 3; ++i) {
			u32 offset = pRFCalibrateInfo->tx_iqc_8703b[i][0];
			u32 data = pRFCalibrateInfo->tx_iqc_8703b[i][1];

			if (pRFCalibrateInfo->tx_iqc_8703b[i][0] != 0) {
				phy_set_bb_reg(pAdapter, offset, bMaskDWord, data);
				RTW_INFO("Switch to S0 TxIQC (offset, data) = (0x%X, 0x%X)\n", offset, data);
			}
		}
		for (i = 0; i < 2; ++i) {
			u32 offset = pRFCalibrateInfo->rx_iqc_8703b[i][0];
			u32 data = pRFCalibrateInfo->rx_iqc_8703b[i][1];

			if (pRFCalibrateInfo->rx_iqc_8703b[i][0] != 0) {
				phy_set_bb_reg(pAdapter, offset, bMaskDWord, data);
				RTW_INFO("Switch to S0 RxIQC (offset, data) = (0x%X, 0x%X)\n", offset, data);
			}
		}
	}
	break;
	default:
		pMptCtx->mpt_rf_path = RF_PATH_AB;
		break;
	}

}
#endif

#ifdef CONFIG_RTL8723D
void mpt_SetRFPath_8723D(PADAPTER pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u8	p = 0, i = 0;
	u32	ulAntennaTx, ulAntennaRx, offset = 0, data = 0, val32 = 0;
	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.mpt_ctx);
	struct dm_struct	*pDM_Odm = &pHalData->odmpriv;
	struct dm_rf_calibration_struct	*pRFCalibrateInfo = &(pDM_Odm->rf_calibrate_info);

	ulAntennaTx = pHalData->antenna_tx_path;
	ulAntennaRx = pHalData->AntennaRxPath;

	if (pHalData->rf_chip >= RF_CHIP_MAX) {
		RTW_INFO("This RF chip ID is not supported\n");
		return;
	}

	switch (pAdapter->mppriv.antenna_tx) {
	/* Actually path S1  (Wi-Fi) */
	case ANTENNA_A: {
		pMptCtx->mpt_rf_path = RF_PATH_A;
		phy_set_bb_reg(pAdapter, rS0S1_PathSwitch, BIT9|BIT8|BIT7|BIT6, 0);
	}
	break;
	/* Actually path S0 (BT) */
	case ANTENNA_B: {
		pMptCtx->mpt_rf_path = RF_PATH_B;
		phy_set_bb_reg(pAdapter, rS0S1_PathSwitch, BIT9|BIT8|BIT7|BIT6, 0xA);

	}
	break;
	default:
		pMptCtx->mpt_rf_path = RF_PATH_AB;
		break;
	}
}
#endif

void mpt_SetRFPath_819X(PADAPTER	pAdapter)
{
	HAL_DATA_TYPE			*pHalData	= GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT		pMptCtx = &(pAdapter->mppriv.mpt_ctx);
	u32			ulAntennaTx, ulAntennaRx;
	R_ANTENNA_SELECT_OFDM	*p_ofdm_tx;	/* OFDM Tx register */
	R_ANTENNA_SELECT_CCK	*p_cck_txrx;
	u8		r_rx_antenna_ofdm = 0, r_ant_select_cck_val = 0;
	u8		chgTx = 0, chgRx = 0;
	u32		r_ant_sel_cck_val = 0, r_ant_select_ofdm_val = 0, r_ofdm_tx_en_val = 0;

	ulAntennaTx = pHalData->antenna_tx_path;
	ulAntennaRx = pHalData->AntennaRxPath;

	p_ofdm_tx = (R_ANTENNA_SELECT_OFDM *)&r_ant_select_ofdm_val;
	p_cck_txrx = (R_ANTENNA_SELECT_CCK *)&r_ant_select_cck_val;

	p_ofdm_tx->r_ant_ht1			= 0x1;
	p_ofdm_tx->r_ant_ht2			= 0x2;/*Second TX RF path is A*/
	p_ofdm_tx->r_ant_non_ht			= 0x3;/*/ 0x1+0x2=0x3 */

	switch (ulAntennaTx) {
	case ANTENNA_A:
		p_ofdm_tx->r_tx_antenna		= 0x1;
		r_ofdm_tx_en_val		= 0x1;
		p_ofdm_tx->r_ant_l		= 0x1;
		p_ofdm_tx->r_ant_ht_s1		= 0x1;
		p_ofdm_tx->r_ant_non_ht_s1	= 0x1;
		p_cck_txrx->r_ccktx_enable	= 0x8;
		chgTx = 1;
		/*/ From SD3 Willis suggestion !!! Set RF A=TX and B as standby*/
		/*/if (IS_HARDWARE_TYPE_8192S(pAdapter))*/
		{
			phy_set_bb_reg(pAdapter, rFPGA0_XA_HSSIParameter2, 0xe, 2);
			phy_set_bb_reg(pAdapter, rFPGA0_XB_HSSIParameter2, 0xe, 1);
			r_ofdm_tx_en_val			= 0x3;
			/*/ Power save*/
			/*/cosa r_ant_select_ofdm_val = 0x11111111;*/
			/*/ We need to close RFB by SW control*/
			if (pHalData->rf_type == RF_2T2R) {
				phy_set_bb_reg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT10, 0);
				phy_set_bb_reg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT26, 1);
				phy_set_bb_reg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT10, 0);
				phy_set_bb_reg(pAdapter, rFPGA0_XAB_RFParameter, BIT1, 1);
				phy_set_bb_reg(pAdapter, rFPGA0_XAB_RFParameter, BIT17, 0);
			}
		}
		pMptCtx->mpt_rf_path = RF_PATH_A;
		break;
	case ANTENNA_B:
		p_ofdm_tx->r_tx_antenna		= 0x2;
		r_ofdm_tx_en_val		= 0x2;
		p_ofdm_tx->r_ant_l		= 0x2;
		p_ofdm_tx->r_ant_ht_s1		= 0x2;
		p_ofdm_tx->r_ant_non_ht_s1	= 0x2;
		p_cck_txrx->r_ccktx_enable	= 0x4;
		chgTx = 1;
		/*/ From SD3 Willis suggestion !!! Set RF A as standby*/
		/*/if (IS_HARDWARE_TYPE_8192S(pAdapter))*/
		{
			phy_set_bb_reg(pAdapter, rFPGA0_XA_HSSIParameter2, 0xe, 1);
			phy_set_bb_reg(pAdapter, rFPGA0_XB_HSSIParameter2, 0xe, 2);

			/*/ 2008/10/31 MH From SD3 Willi's suggestion. We must read RF 1T table.*/
			/*/ 2009/01/08 MH From Sd3 Willis. We need to close RFA by SW control*/
			if (pHalData->rf_type == RF_2T2R || pHalData->rf_type == RF_1T2R) {
				phy_set_bb_reg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT10, 1);
				phy_set_bb_reg(pAdapter, rFPGA0_XA_RFInterfaceOE, BIT10, 0);
				phy_set_bb_reg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT26, 0);
				/*/phy_set_bb_reg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT10, 0);*/
				phy_set_bb_reg(pAdapter, rFPGA0_XAB_RFParameter, BIT1, 0);
				phy_set_bb_reg(pAdapter, rFPGA0_XAB_RFParameter, BIT17, 1);
			}
		}
		pMptCtx->mpt_rf_path = RF_PATH_B;
		break;
	case ANTENNA_AB:/*/ For 8192S*/
		p_ofdm_tx->r_tx_antenna		= 0x3;
		r_ofdm_tx_en_val		= 0x3;
		p_ofdm_tx->r_ant_l		= 0x3;
		p_ofdm_tx->r_ant_ht_s1		= 0x3;
		p_ofdm_tx->r_ant_non_ht_s1	= 0x3;
		p_cck_txrx->r_ccktx_enable	= 0xC;
		chgTx = 1;
		/*/ From SD3Willis suggestion !!! Set RF B as standby*/
		/*/if (IS_HARDWARE_TYPE_8192S(pAdapter))*/
		{
			phy_set_bb_reg(pAdapter, rFPGA0_XA_HSSIParameter2, 0xe, 2);
			phy_set_bb_reg(pAdapter, rFPGA0_XB_HSSIParameter2, 0xe, 2);
			/* Disable Power save*/
			/*cosa r_ant_select_ofdm_val = 0x3321333;*/
			/* 2009/01/08 MH From Sd3 Willis. We need to enable RFA/B by SW control*/
			if (pHalData->rf_type == RF_2T2R) {
				phy_set_bb_reg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT10, 0);

				phy_set_bb_reg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT26, 0);
				/*/phy_set_bb_reg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT10, 0);*/
				phy_set_bb_reg(pAdapter, rFPGA0_XAB_RFParameter, BIT1, 1);
				phy_set_bb_reg(pAdapter, rFPGA0_XAB_RFParameter, BIT17, 1);
			}
		}
		pMptCtx->mpt_rf_path = RF_PATH_AB;
		break;
	default:
		break;
	}
#if 0
	/*  r_rx_antenna_ofdm, bit0=A, bit1=B, bit2=C, bit3=D */
	/*  r_cckrx_enable : CCK default, 0=A, 1=B, 2=C, 3=D */
	/* r_cckrx_enable_2 : CCK option, 0=A, 1=B, 2=C, 3=D	 */
#endif
	switch (ulAntennaRx) {
	case ANTENNA_A:
		r_rx_antenna_ofdm		= 0x1;	/* A*/
		p_cck_txrx->r_cckrx_enable	= 0x0;	/* default: A*/
		p_cck_txrx->r_cckrx_enable_2	= 0x0;	/* option: A*/
		chgRx = 1;
		break;
	case ANTENNA_B:
		r_rx_antenna_ofdm			= 0x2;	/*/ B*/
		p_cck_txrx->r_cckrx_enable	= 0x1;	/*/ default: B*/
		p_cck_txrx->r_cckrx_enable_2	= 0x1;	/*/ option: B*/
		chgRx = 1;
		break;
	case ANTENNA_AB:/*/ For 8192S and 8192E/U...*/
		r_rx_antenna_ofdm		= 0x3;/*/ AB*/
		p_cck_txrx->r_cckrx_enable	= 0x0;/*/ default:A*/
		p_cck_txrx->r_cckrx_enable_2	= 0x1;/*/ option:B*/
		chgRx = 1;
		break;
	default:
		break;
	}


	if (chgTx && chgRx) {
		switch (pHalData->rf_chip) {
		case RF_8225:
		case RF_8256:
		case RF_6052:
			/*/r_ant_sel_cck_val = r_ant_select_cck_val;*/
			phy_set_bb_reg(pAdapter, rFPGA1_TxInfo, 0x7fffffff, r_ant_select_ofdm_val);		/*/OFDM Tx*/
			phy_set_bb_reg(pAdapter, rFPGA0_TxInfo, 0x0000000f, r_ofdm_tx_en_val);		/*/OFDM Tx*/
			phy_set_bb_reg(pAdapter, rOFDM0_TRxPathEnable, 0x0000000f, r_rx_antenna_ofdm);	/*/OFDM Rx*/
			phy_set_bb_reg(pAdapter, rOFDM1_TRxPathEnable, 0x0000000f, r_rx_antenna_ofdm);	/*/OFDM Rx*/
			if (IS_HARDWARE_TYPE_8192E(pAdapter)) {
				phy_set_bb_reg(pAdapter, rOFDM0_TRxPathEnable, 0x000000F0, r_rx_antenna_ofdm);	/*/OFDM Rx*/
				phy_set_bb_reg(pAdapter, rOFDM1_TRxPathEnable, 0x000000F0, r_rx_antenna_ofdm);	/*/OFDM Rx*/
			}
			phy_set_bb_reg(pAdapter, rCCK0_AFESetting, bMaskByte3, r_ant_select_cck_val);/*/r_ant_sel_cck_val); /CCK TxRx*/
			break;

		default:
			RTW_INFO("Unsupported RFChipID for switching antenna.\n");
			break;
		}
	}
}	/* MPT_ProSetRFPath */

#ifdef CONFIG_RTL8192F

void mpt_set_rfpath_8192f(PADAPTER	pAdapter)
{
	HAL_DATA_TYPE			*pHalData	= GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT		pMptCtx = &(pAdapter->mppriv.mpt_ctx);

	u16		ForcedDataRate = mpt_to_mgnt_rate(pMptCtx->mpt_rate_index);
	u8				NssforRate, odmNssforRate;
	u32				ulAntennaTx, ulAntennaRx;
	u8				RxAntToPhyDm;
	u8				TxAntToPhyDm;

	ulAntennaTx = pHalData->antenna_tx_path;
	ulAntennaRx = pHalData->AntennaRxPath;
	NssforRate = MgntQuery_NssTxRate(ForcedDataRate);

	if (pHalData->rf_chip >= RF_TYPE_MAX)
		RTW_INFO("This RF chip ID is not supported\n");

	switch (ulAntennaTx) {
	case ANTENNA_A:
			pMptCtx->mpt_rf_path = RF_PATH_A;
			TxAntToPhyDm = BB_PATH_A;
			break;
	case ANTENNA_B:
			pMptCtx->mpt_rf_path = RF_PATH_B;
			TxAntToPhyDm = BB_PATH_B;
			break;
	case ANTENNA_AB:
			pMptCtx->mpt_rf_path = RF_PATH_AB;
			TxAntToPhyDm = (BB_PATH_A|BB_PATH_B);
			break;
	default:
			pMptCtx->mpt_rf_path = RF_PATH_AB;
			TxAntToPhyDm = (BB_PATH_A|BB_PATH_B);
			break;
	}

	switch (ulAntennaRx) {
	case ANTENNA_A:
			RxAntToPhyDm = BB_PATH_A;
			break;
	case ANTENNA_B:
			RxAntToPhyDm = BB_PATH_B;
			break;
	case ANTENNA_AB:
			RxAntToPhyDm = (BB_PATH_A|BB_PATH_B);
			break;
	default:
			RxAntToPhyDm = (BB_PATH_A|BB_PATH_B);
			break;
	}

	config_phydm_trx_mode_8192f(GET_PDM_ODM(pAdapter), TxAntToPhyDm, RxAntToPhyDm, FALSE);

}

#endif

void hal_mpt_SetAntenna(PADAPTER	pAdapter)

{
	RTW_INFO("Do %s\n", __func__);
#ifdef CONFIG_RTL8822C
	if (IS_HARDWARE_TYPE_8822C(pAdapter)) {
		rtl8822c_mp_config_rfpath(pAdapter);
		return;
	}
#endif
#ifdef CONFIG_RTL8814A
	if (IS_HARDWARE_TYPE_8814A(pAdapter)) {
		mpt_SetRFPath_8814A(pAdapter);
		return;
	}
#endif
#ifdef CONFIG_RTL8822B
	if (IS_HARDWARE_TYPE_8822B(pAdapter)) {
		rtl8822b_mp_config_rfpath(pAdapter);
		return;
	}
#endif
#ifdef CONFIG_RTL8821C
	if (IS_HARDWARE_TYPE_8821C(pAdapter)) {
		rtl8821c_mp_config_rfpath(pAdapter);
		return;
	}
#endif
#if	defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A)
	if (IS_HARDWARE_TYPE_JAGUAR(pAdapter)) {
		mpt_SetRFPath_8812A(pAdapter);
		return;
	}
#endif
#ifdef CONFIG_RTL8723B
	if (IS_HARDWARE_TYPE_8723B(pAdapter)) {
		mpt_SetRFPath_8723B(pAdapter);
		return;
	}
#endif

#ifdef CONFIG_RTL8703B
	if (IS_HARDWARE_TYPE_8703B(pAdapter)) {
		mpt_SetRFPath_8703B(pAdapter);
		return;
	}
#endif

#ifdef CONFIG_RTL8723D
	if (IS_HARDWARE_TYPE_8723D(pAdapter)) {
		mpt_SetRFPath_8723D(pAdapter);
		return;
	}
#endif

#ifdef CONFIG_RTL8192F
		if (IS_HARDWARE_TYPE_8192F(pAdapter)) {
			mpt_set_rfpath_8192f(pAdapter);
			return;
		}
#endif

	/*	else if (IS_HARDWARE_TYPE_8821B(pAdapter))
			mpt_SetRFPath_8821B(pAdapter);
		Prepare for 8822B
		else if (IS_HARDWARE_TYPE_8822B(Context))
			mpt_SetRFPath_8822B(Context);
	*/
	mpt_SetRFPath_819X(pAdapter);
	RTW_INFO("mpt_SetRFPath_819X Do %s\n", __func__);
}

s32 hal_mpt_SetThermalMeter(PADAPTER pAdapter, u8 target_ther)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(pAdapter);

	if (!netif_running(pAdapter->pnetdev)) {
		return _FAIL;
	}


	if (check_fwstate(&pAdapter->mlmepriv, WIFI_MP_STATE) == _FALSE) {
		return _FAIL;
	}


	target_ther &= 0xff;
	if (target_ther < 0x07)
		target_ther = 0x07;
	else if (target_ther > 0x1d)
		target_ther = 0x1d;

	pHalData->eeprom_thermal_meter = target_ther;

	return _SUCCESS;
}


void hal_mpt_TriggerRFThermalMeter(PADAPTER pAdapter)
{
	if (IS_HARDWARE_TYPE_JAGUAR3(pAdapter)) {
		phy_set_rf_reg(pAdapter, RF_PATH_A, 0x42, BIT19, 0x1);
		phy_set_rf_reg(pAdapter, RF_PATH_A, 0x42, BIT19, 0x0);
		phy_set_rf_reg(pAdapter, RF_PATH_A, 0x42, BIT19, 0x1);
	} else
		phy_set_rf_reg(pAdapter, RF_PATH_A, 0x42, BIT17 | BIT16, 0x03);

}


u8 hal_mpt_ReadRFThermalMeter(PADAPTER pAdapter, u8 rf_path)

{
	struct dm_struct *p_dm_odm = adapter_to_phydm(pAdapter);

	u32 ThermalValue = 0;
	s32 thermal_value_temp = 0;
	s8 thermal_offset = 0;
	u32 thermal_reg_mask = 0;

	if (IS_8822C_SERIES(GET_HAL_DATA(pAdapter)->version_id))
			thermal_reg_mask = 0x007e; 	/*0x42: RF Reg[6:1], 35332(themal K  & bias k & power trim) & 35325(tssi )*/
	else
			thermal_reg_mask = 0xfc00;	/*0x42: RF Reg[15:10]*/

	ThermalValue = (u8)phy_query_rf_reg(pAdapter, rf_path, 0x42, thermal_reg_mask);

	thermal_offset = phydm_get_thermal_offset(p_dm_odm);

	thermal_value_temp = ThermalValue + thermal_offset;

	if (thermal_value_temp > 63)
		ThermalValue = 63;
	else if (thermal_value_temp < 0)
		ThermalValue = 0;
	else
		ThermalValue = thermal_value_temp;

	return (u8)ThermalValue;
}


void hal_mpt_GetThermalMeter(PADAPTER pAdapter, u8 rfpath, u8 *value)
{
#if 0
	fw_cmd(pAdapter, IOCMD_GET_THERMAL_METER);
	rtw_msleep_os(1000);
	fw_cmd_data(pAdapter, value, 1);
	*value &= 0xFF;
#else
	hal_mpt_TriggerRFThermalMeter(pAdapter);
	rtw_msleep_os(1000);
	*value = hal_mpt_ReadRFThermalMeter(pAdapter, rfpath);
#endif

}


void hal_mpt_SetSingleCarrierTx(PADAPTER pAdapter, u8 bStart)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(pAdapter);

	pAdapter->mppriv.mpt_ctx.bSingleCarrier = bStart;

	if (bStart) {/*/ Start Single Carrier.*/
		/*/ Start Single Carrier.*/
		/*/ 1. if OFDM block on?*/
		if (!phy_query_bb_reg(pAdapter, rFPGA0_RFMOD, bOFDMEn))
			phy_set_bb_reg(pAdapter, rFPGA0_RFMOD, bOFDMEn, 1); /*set OFDM block on*/

		/*/ 2. set CCK test mode off, set to CCK normal mode*/
		phy_set_bb_reg(pAdapter, rCCK0_System, bCCKBBMode, 0);

		/*/ 3. turn on scramble setting*/
		phy_set_bb_reg(pAdapter, rCCK0_System, bCCKScramble, 1);

		/*/ 4. Turn On Continue Tx and turn off the other test modes.*/
#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A) || defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C)
		if (IS_HARDWARE_TYPE_JAGUAR_ALL(pAdapter))
			phy_set_bb_reg(pAdapter, rSingleTone_ContTx_Jaguar, BIT18 | BIT17 | BIT16, OFDM_SingleCarrier);
		else
#endif /* CONFIG_RTL8812A || CONFIG_RTL8821A || CONFIG_RTL8814A || CONFIG_RTL8822B || CONFIG_RTL8821C */
			phy_set_bb_reg(pAdapter, rOFDM1_LSTF, BIT30 | BIT29 | BIT28, OFDM_SingleCarrier);

	} else {
		/*/ Stop Single Carrier.*/
		/*/ Stop Single Carrier.*/
		/*/ Turn off all test modes.*/
#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A) || defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C)
		if (IS_HARDWARE_TYPE_JAGUAR_ALL(pAdapter))
			phy_set_bb_reg(pAdapter, rSingleTone_ContTx_Jaguar, BIT18 | BIT17 | BIT16, OFDM_ALL_OFF);
		else
#endif /* CONFIG_RTL8812A || CONFIG_RTL8821A || CONFIG_RTL8814A || CONFIG_RTL8822B || CONFIG_RTL8821C */
			phy_set_bb_reg(pAdapter, rOFDM1_LSTF, BIT30 | BIT29 | BIT28, OFDM_ALL_OFF);

		rtw_msleep_os(10);
		/*/BB Reset*/
		phy_set_bb_reg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
		phy_set_bb_reg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);
	}
}


void hal_mpt_SetSingleToneTx(PADAPTER pAdapter, u8 bStart)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT		pMptCtx = &(pAdapter->mppriv.mpt_ctx);
	struct dm_struct		*pDM_Odm = &pHalData->odmpriv;
	u32			ulAntennaTx = pHalData->antenna_tx_path;
	static u32		regRF = 0, regBB0 = 0, regBB1 = 0, regBB2 = 0, regBB3 = 0;
	u8 rfPath;

	if (IS_HARDWARE_TYPE_JAGUAR3(pAdapter)) {
#ifdef	PHYDM_MP_SUPPORT
		phydm_mp_set_single_tone(pDM_Odm, bStart, pMptCtx->mpt_rf_path);
#endif
		return;
	}

	switch (ulAntennaTx) {
	case ANTENNA_B:
		rfPath = RF_PATH_B;
		break;
	case ANTENNA_C:
		rfPath = RF_PATH_C;
		break;
	case ANTENNA_D:
		rfPath = RF_PATH_D;
		break;
	case ANTENNA_A:
	default:
		rfPath = RF_PATH_A;
		break;
	}

	pAdapter->mppriv.mpt_ctx.is_single_tone = bStart;
	if (bStart) {
		/*/ Start Single Tone.*/
		/*/ <20120326, Kordan> To amplify the power of tone for Xtal calibration. (asked by Edlu)*/
		if (IS_HARDWARE_TYPE_8188E(pAdapter)) {
			regRF = phy_query_rf_reg(pAdapter, rfPath, lna_low_gain_3, bRFRegOffsetMask);
			phy_set_rf_reg(pAdapter, RF_PATH_A, lna_low_gain_3, BIT1, 0x1); /*/ RF LO enabled*/
			phy_set_bb_reg(pAdapter, rFPGA0_RFMOD, bCCKEn, 0x0);
			phy_set_bb_reg(pAdapter, rFPGA0_RFMOD, bOFDMEn, 0x0);
		} else if (IS_HARDWARE_TYPE_8192E(pAdapter)) { /*/ USB need to do RF LO disable first, PCIE isn't required to follow this order.*/
			/*/Set MAC REG 88C: Prevent SingleTone Fail*/
			phy_set_mac_reg(pAdapter, 0x88C, 0xF00000, 0xF);
			phy_set_rf_reg(pAdapter, pMptCtx->mpt_rf_path, lna_low_gain_3, BIT1, 0x1); /*/ RF LO disabled*/
			phy_set_rf_reg(pAdapter, pMptCtx->mpt_rf_path, RF_AC, 0xF0000, 0x2); /*/ Tx mode*/
		}	else if (IS_HARDWARE_TYPE_8192F(pAdapter)) { /* USB need to do RF LO disable first, PCIE isn't required to follow this order.*/
 #ifdef CONFIG_RTL8192F
			phy_set_mac_reg(pAdapter, REG_LEDCFG0_8192F, BIT23, 0x1);
			phy_set_mac_reg(pAdapter, REG_LEDCFG0_8192F, BIT26, 0x1);
			phy_set_mac_reg(pAdapter, REG_PAD_CTRL1_8192F, BIT7, 0x1);
			phy_set_mac_reg(pAdapter, REG_PAD_CTRL1_8192F, BIT1, 0x1);
			phy_set_mac_reg(pAdapter, REG_PAD_CTRL1_8192F, BIT0, 0x1);
			phy_set_mac_reg(pAdapter, REG_AFE_CTRL_4_8192F, BIT16, 0x1);
			phy_set_bb_reg(pAdapter, 0x88C, 0xF00000, 0xF);
			phy_set_rf_reg(pAdapter, pMptCtx->mpt_rf_path, 0x57, BIT1, 0x1); /* RF LO disabled*/
			phy_set_rf_reg(pAdapter, pMptCtx->mpt_rf_path, RF_AC, 0xF0000, 0x2); /* Tx mode*/
#endif
		} else if (IS_HARDWARE_TYPE_8723B(pAdapter)) {
			if (pMptCtx->mpt_rf_path == RF_PATH_A) {
				phy_set_rf_reg(pAdapter, RF_PATH_A, RF_AC, 0xF0000, 0x2); /*/ Tx mode*/
				phy_set_rf_reg(pAdapter, RF_PATH_A, 0x56, 0xF, 0x1); /*/ RF LO enabled*/
			} else {
				/*/ S0/S1 both use PATH A to configure*/
				phy_set_rf_reg(pAdapter, RF_PATH_A, RF_AC, 0xF0000, 0x2); /*/ Tx mode*/
				phy_set_rf_reg(pAdapter, RF_PATH_A, 0x76, 0xF, 0x1); /*/ RF LO enabled*/
			}
		} else if (IS_HARDWARE_TYPE_8703B(pAdapter)) {
			if (pMptCtx->mpt_rf_path == RF_PATH_A) {
				phy_set_rf_reg(pAdapter, RF_PATH_A, RF_AC, 0xF0000, 0x2); /* Tx mode */
				phy_set_rf_reg(pAdapter, RF_PATH_A, 0x53, 0xF000, 0x1); /* RF LO enabled */
			}
		} else if (IS_HARDWARE_TYPE_8188F(pAdapter) || IS_HARDWARE_TYPE_8188GTV(pAdapter)) {
			/*Set BB REG 88C: Prevent SingleTone Fail*/
			phy_set_bb_reg(pAdapter, rFPGA0_AnalogParameter4, 0xF00000, 0xF);
			phy_set_rf_reg(pAdapter, pMptCtx->mpt_rf_path, lna_low_gain_3, BIT1, 0x1);
			phy_set_rf_reg(pAdapter, pMptCtx->mpt_rf_path, RF_AC, 0xF0000, 0x2);

		} else if (IS_HARDWARE_TYPE_8723D(pAdapter)) {
			if (pMptCtx->mpt_rf_path == RF_PATH_A) {
				phy_set_bb_reg(pAdapter, rFPGA0_RFMOD, bCCKEn|bOFDMEn, 0);
				phy_set_rf_reg(pAdapter, RF_PATH_A, RF_AC, BIT16, 0x0);
				phy_set_rf_reg(pAdapter, RF_PATH_A, 0x53, BIT0, 0x1);
			} else {/* S0/S1 both use PATH A to configure */
				phy_set_bb_reg(pAdapter, rFPGA0_RFMOD, bCCKEn|bOFDMEn, 0);
				phy_set_rf_reg(pAdapter, RF_PATH_A, RF_AC, BIT16, 0x0);
				phy_set_rf_reg(pAdapter, RF_PATH_A, 0x63, BIT0, 0x1);
			}
		} else if (IS_HARDWARE_TYPE_JAGUAR(pAdapter) || IS_HARDWARE_TYPE_8822B(pAdapter) || IS_HARDWARE_TYPE_8821C(pAdapter)) {
#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A) || defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C)
			u8 p = RF_PATH_A;

			regRF = phy_query_rf_reg(pAdapter, RF_PATH_A, RF_AC_Jaguar, bRFRegOffsetMask);
			regBB0 = phy_query_bb_reg(pAdapter, rA_RFE_Pinmux_Jaguar, bMaskDWord);
			regBB1 = phy_query_bb_reg(pAdapter, rB_RFE_Pinmux_Jaguar, bMaskDWord);
			regBB2 = phy_query_bb_reg(pAdapter, rA_RFE_Pinmux_Jaguar + 4, bMaskDWord);
			regBB3 = phy_query_bb_reg(pAdapter, rB_RFE_Pinmux_Jaguar + 4, bMaskDWord);

			phy_set_bb_reg(pAdapter, rOFDMCCKEN_Jaguar, BIT29 | BIT28, 0x0); /*/ Disable CCK and OFDM*/

			if (pMptCtx->mpt_rf_path == RF_PATH_AB) {
				for (p = RF_PATH_A; p <= RF_PATH_B; ++p) {
					phy_set_rf_reg(pAdapter, p, RF_AC_Jaguar, 0xF0000, 0x2); /*/ Tx mode: RF0x00[19:16]=4'b0010 */
					phy_set_rf_reg(pAdapter, p, RF_AC_Jaguar, 0x1F, 0x0); /*/ Lowest RF gain index: RF_0x0[4:0] = 0*/
					phy_set_rf_reg(pAdapter, p, lna_low_gain_3, BIT1, 0x1); /*/ RF LO enabled*/
				}
			} else {
				phy_set_rf_reg(pAdapter, pMptCtx->mpt_rf_path, RF_AC_Jaguar, 0xF0000, 0x2); /*/ Tx mode: RF0x00[19:16]=4'b0010 */
				phy_set_rf_reg(pAdapter, pMptCtx->mpt_rf_path, RF_AC_Jaguar, 0x1F, 0x0); /*/ Lowest RF gain index: RF_0x0[4:0] = 0*/
#ifdef CONFIG_RTL8821C
				if (IS_HARDWARE_TYPE_8821C(pAdapter) && pDM_Odm->current_rf_set_8821c == SWITCH_TO_BTG)
					phy_set_rf_reg(pAdapter, pMptCtx->mpt_rf_path, 0x75, BIT16, 0x1); /* RF LO (for BTG) enabled */
				else
#endif
					phy_set_rf_reg(pAdapter, pMptCtx->mpt_rf_path, lna_low_gain_3, BIT1, 0x1); /*/ RF LO enabled*/
			}
			if (IS_HARDWARE_TYPE_8822B(pAdapter)) {
					phy_set_bb_reg(pAdapter, rA_RFE_Pinmux_Jaguar, bMaskDWord, 0x77777777);  /* 0xCB0=0x77777777*/
					phy_set_bb_reg(pAdapter, rB_RFE_Pinmux_Jaguar, bMaskDWord, 0x77777777);  /* 0xEB0=0x77777777*/
					phy_set_bb_reg(pAdapter, rA_RFE_Pinmux_Jaguar + 4, bMaskLWord, 0x7777);  /* 0xCB4[15:0] = 0x7777*/
					phy_set_bb_reg(pAdapter, rB_RFE_Pinmux_Jaguar + 4, bMaskLWord, 0x7777);  /* 0xEB4[15:0] = 0x7777*/
					phy_set_bb_reg(pAdapter, rA_RFE_Inverse_Jaguar, 0xFFF, 0xb); /* 0xCBC[23:16] = 0x12*/
					phy_set_bb_reg(pAdapter, rB_RFE_Inverse_Jaguar, 0xFFF, 0x830); /* 0xEBC[23:16] = 0x12*/
			} else if (IS_HARDWARE_TYPE_8821C(pAdapter)) {
				phy_set_bb_reg(pAdapter, rA_RFE_Pinmux_Jaguar, 0xF0F0, 0x707);  /* 0xCB0[[15:12, 7:4] = 0x707*/

				if (pHalData->external_pa_5g)
				{
					phy_set_bb_reg(pAdapter, rA_RFE_Pinmux_Jaguar + 4, 0xA00000, 0x1); /* 0xCB4[23, 21] = 0x1*/
				}
				else if (pHalData->ExternalPA_2G)
				{
					phy_set_bb_reg(pAdapter, rA_RFE_Pinmux_Jaguar + 4, 0xA00000, 0x1); /* 0xCB4[23, 21] = 0x1*/
				}
			} else {
				phy_set_bb_reg(pAdapter, rA_RFE_Pinmux_Jaguar, 0xFF00F0, 0x77007);  /*/ 0xCB0[[23:16, 7:4] = 0x77007*/
				phy_set_bb_reg(pAdapter, rB_RFE_Pinmux_Jaguar, 0xFF00F0, 0x77007);  /*/ 0xCB0[[23:16, 7:4] = 0x77007*/

				if (pHalData->external_pa_5g) {
					phy_set_bb_reg(pAdapter, rA_RFE_Pinmux_Jaguar + 4, 0xFF00000, 0x12); /*/ 0xCB4[23:16] = 0x12*/
					phy_set_bb_reg(pAdapter, rB_RFE_Pinmux_Jaguar + 4, 0xFF00000, 0x12); /*/ 0xEB4[23:16] = 0x12*/
				} else if (pHalData->ExternalPA_2G) {
					phy_set_bb_reg(pAdapter, rA_RFE_Pinmux_Jaguar + 4, 0xFF00000, 0x11); /*/ 0xCB4[23:16] = 0x11*/
					phy_set_bb_reg(pAdapter, rB_RFE_Pinmux_Jaguar + 4, 0xFF00000, 0x11); /*/ 0xEB4[23:16] = 0x11*/
				}
			}
#endif
		}
#if defined(CONFIG_RTL8814A)
				else if (IS_HARDWARE_TYPE_8814A(pAdapter))
						mpt_SetSingleTone_8814A(pAdapter, TRUE, FALSE);
#endif
		else	/*/ Turn On SingleTone and turn off the other test modes.*/
			phy_set_bb_reg(pAdapter, rOFDM1_LSTF, BIT30 | BIT29 | BIT28, OFDM_SingleTone);

		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000500);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000500);

	} else {/*/ Stop Single Ton e.*/

		if (IS_HARDWARE_TYPE_8188E(pAdapter)) {
			phy_set_rf_reg(pAdapter, RF_PATH_A, lna_low_gain_3, bRFRegOffsetMask, regRF);
			phy_set_bb_reg(pAdapter, rFPGA0_RFMOD, bCCKEn, 0x1);
			phy_set_bb_reg(pAdapter, rFPGA0_RFMOD, bOFDMEn, 0x1);
		} else if (IS_HARDWARE_TYPE_8192E(pAdapter)) {
			phy_set_rf_reg(pAdapter, pMptCtx->mpt_rf_path, RF_AC, 0xF0000, 0x3);/*/ Tx mode*/
			phy_set_rf_reg(pAdapter, pMptCtx->mpt_rf_path, lna_low_gain_3, BIT1, 0x0);/*/ RF LO disabled */
			/*/ RESTORE MAC REG 88C: Enable RF Functions*/
			phy_set_mac_reg(pAdapter, 0x88C, 0xF00000, 0x0);
		} else if (IS_HARDWARE_TYPE_8192F(pAdapter)){
#ifdef CONFIG_RTL8192F
			phy_set_mac_reg(pAdapter, REG_LEDCFG0_8192F, BIT23, 0x0);
			phy_set_mac_reg(pAdapter, REG_LEDCFG0_8192F, BIT26, 0x0);
			phy_set_mac_reg(pAdapter, REG_PAD_CTRL1_8192F, BIT7, 0x0);
			phy_set_mac_reg(pAdapter, REG_PAD_CTRL1_8192F, BIT1, 0x0);
			phy_set_mac_reg(pAdapter, REG_PAD_CTRL1_8192F, BIT0, 0x0);
			phy_set_mac_reg(pAdapter, REG_AFE_CTRL_4_8192F, BIT16, 0x0);
			phy_set_bb_reg(pAdapter, 0x88C, 0xF00000, 0x0);
			phy_set_rf_reg(pAdapter, pMptCtx->mpt_rf_path, 0x57, BIT1, 0x0); /* RF LO disabled*/
			phy_set_rf_reg(pAdapter, pMptCtx->mpt_rf_path, RF_AC, 0xF0000, 0x3); /* Rx mode*/
#endif
		} else if (IS_HARDWARE_TYPE_8723B(pAdapter)) {
			if (pMptCtx->mpt_rf_path == RF_PATH_A) {
				phy_set_rf_reg(pAdapter, RF_PATH_A, RF_AC, 0xF0000, 0x3); /*/ Rx mode*/
				phy_set_rf_reg(pAdapter, RF_PATH_A, 0x56, 0xF, 0x0); /*/ RF LO disabled*/
			} else {
				/*/ S0/S1 both use PATH A to configure*/
				phy_set_rf_reg(pAdapter, RF_PATH_A, RF_AC, 0xF0000, 0x3); /*/ Rx mode*/
				phy_set_rf_reg(pAdapter, RF_PATH_A, 0x76, 0xF, 0x0); /*/ RF LO disabled*/
			}
		} else if (IS_HARDWARE_TYPE_8703B(pAdapter)) {
			if (pMptCtx->mpt_rf_path == RF_PATH_A) {
				phy_set_rf_reg(pAdapter, RF_PATH_A, RF_AC, 0xF0000, 0x3); /* Rx mode */
				phy_set_rf_reg(pAdapter, RF_PATH_A, 0x53, 0xF000, 0x0); /* RF LO disabled */
			}
		} else if (IS_HARDWARE_TYPE_8188F(pAdapter) || IS_HARDWARE_TYPE_8188GTV(pAdapter)) {
			phy_set_rf_reg(pAdapter, pMptCtx->mpt_rf_path, RF_AC, 0xF0000, 0x3); /*Tx mode*/
			phy_set_rf_reg(pAdapter, pMptCtx->mpt_rf_path, lna_low_gain_3, BIT1, 0x0); /*RF LO disabled*/
			/*Set BB REG 88C: Prevent SingleTone Fail*/
			phy_set_bb_reg(pAdapter, rFPGA0_AnalogParameter4, 0xF00000, 0xc);
		} else if (IS_HARDWARE_TYPE_8723D(pAdapter)) {
			if (pMptCtx->mpt_rf_path == RF_PATH_A) {
				phy_set_bb_reg(pAdapter, rFPGA0_RFMOD, bCCKEn|bOFDMEn, 0x3);
				phy_set_rf_reg(pAdapter, RF_PATH_A, RF_AC, BIT16, 0x1);
				phy_set_rf_reg(pAdapter, RF_PATH_A, 0x53, BIT0, 0x0);
			} else {	/* S0/S1 both use PATH A to configure */
				phy_set_bb_reg(pAdapter, rFPGA0_RFMOD, bCCKEn|bOFDMEn, 0x3);
				phy_set_rf_reg(pAdapter, RF_PATH_A, RF_AC, BIT16, 0x1);
				phy_set_rf_reg(pAdapter, RF_PATH_A, 0x63, BIT0, 0x0);
			}
		} else if (IS_HARDWARE_TYPE_JAGUAR(pAdapter) || IS_HARDWARE_TYPE_8822B(pAdapter) || IS_HARDWARE_TYPE_8821C(pAdapter)) {
#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A) || defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C)
			u8 p = RF_PATH_A;

			phy_set_bb_reg(pAdapter, rOFDMCCKEN_Jaguar, BIT29 | BIT28, 0x3); /*/ Disable CCK and OFDM*/

			if (pMptCtx->mpt_rf_path == RF_PATH_AB) {
				for (p = RF_PATH_A; p <= RF_PATH_B; ++p) {
					phy_set_rf_reg(pAdapter, p, RF_AC_Jaguar, bRFRegOffsetMask, regRF);
					phy_set_rf_reg(pAdapter, p, lna_low_gain_3, BIT1, 0x0); /*/ RF LO disabled*/
				}
			} else {
				p = pMptCtx->mpt_rf_path;
				phy_set_rf_reg(pAdapter, p, RF_AC_Jaguar, bRFRegOffsetMask, regRF);

				if (IS_HARDWARE_TYPE_8821C(pAdapter))
					phy_set_rf_reg(pAdapter, p, 0x75, BIT16, 0x0); /* RF LO (for BTG) disabled */

				phy_set_rf_reg(pAdapter, p, lna_low_gain_3, BIT1, 0x0); /*/ RF LO disabled*/
			}

			phy_set_bb_reg(pAdapter, rA_RFE_Pinmux_Jaguar, bMaskDWord, regBB0);
			phy_set_bb_reg(pAdapter, rB_RFE_Pinmux_Jaguar, bMaskDWord, regBB1);
			phy_set_bb_reg(pAdapter, rA_RFE_Pinmux_Jaguar + 4, bMaskDWord, regBB2);
			phy_set_bb_reg(pAdapter, rB_RFE_Pinmux_Jaguar + 4, bMaskDWord, regBB3);

			if (IS_HARDWARE_TYPE_8822B(pAdapter)) {
				RTW_INFO("Restore RFE control Pin cbc\n");
				phy_set_bb_reg(pAdapter, rA_RFE_Inverse_Jaguar, 0xfff, 0x0);
				phy_set_bb_reg(pAdapter, rB_RFE_Inverse_Jaguar, 0xfff, 0x0);
			}
#endif
		}
#if defined(CONFIG_RTL8814A)
		else if (IS_HARDWARE_TYPE_8814A(pAdapter))
			mpt_SetSingleTone_8814A(pAdapter, FALSE, FALSE);

		else/*/ Turn off all test modes.*/
			phy_set_bb_reg(pAdapter, rSingleTone_ContTx_Jaguar, BIT18 | BIT17 | BIT16, OFDM_ALL_OFF);
#endif
		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000100);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000100);

	}
}

void hal_mpt_SetCarrierSuppressionTx(PADAPTER pAdapter, u8 bStart)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_struct		*pdm_odm = &pHalData->odmpriv;
	u8 Rate;

	pAdapter->mppriv.mpt_ctx.is_carrier_suppression = bStart;

	if (IS_HARDWARE_TYPE_JAGUAR3(pAdapter)) {
#ifdef PHYDM_MP_SUPPORT
		phydm_mp_set_carrier_supp(pdm_odm, bStart, pAdapter->mppriv.rateidx);
#endif
		return;
	}

	Rate = HwRateToMPTRate(pAdapter->mppriv.rateidx);
	if (bStart) {/* Start Carrier Suppression.*/
		if (Rate <= MPT_RATE_11M) {
			/*/ 1. if CCK block on?*/
			if (!read_bbreg(pAdapter, rFPGA0_RFMOD, bCCKEn))
				write_bbreg(pAdapter, rFPGA0_RFMOD, bCCKEn, bEnable);/*set CCK block on*/

			/*/Turn Off All Test Mode*/
			if (IS_HARDWARE_TYPE_JAGUAR_ALL(pAdapter))
				phy_set_bb_reg(pAdapter, 0x914, BIT18 | BIT17 | BIT16, OFDM_ALL_OFF); /* rSingleTone_ContTx_Jaguar*/
			else
				phy_set_bb_reg(pAdapter, rOFDM1_LSTF, BIT30 | BIT29 | BIT28, OFDM_ALL_OFF);

			write_bbreg(pAdapter, rCCK0_System, bCCKBBMode, 0x2);    /*/transmit mode*/
			write_bbreg(pAdapter, rCCK0_System, bCCKScramble, 0x0);  /*/turn off scramble setting*/

			/*/Set CCK Tx Test Rate*/
			write_bbreg(pAdapter, rCCK0_System, bCCKTxRate, 0x0);    /*/Set FTxRate to 1Mbps*/
		}

		/*Set for dynamic set Power index*/
		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000500);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000500);

	} else {/* Stop Carrier Suppression.*/

		if (Rate <= MPT_RATE_11M) {
			write_bbreg(pAdapter, rCCK0_System, bCCKBBMode, 0x0);    /*normal mode*/
			write_bbreg(pAdapter, rCCK0_System, bCCKScramble, 0x1);  /*turn on scramble setting*/

			/*BB Reset*/
			write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
			write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);
		}
		/*Stop for dynamic set Power index*/
		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000100);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000100);
	}
	RTW_INFO("\n MPT_ProSetCarrierSupp() is finished.\n");
}

u32 hal_mpt_query_phytxok(PADAPTER	pAdapter)
{
	PMPT_CONTEXT pMptCtx = &(pAdapter->mppriv.mpt_ctx);
	RT_PMAC_TX_INFO PMacTxInfo = pMptCtx->PMacTxInfo;
	HAL_DATA_TYPE			*pHalData	= GET_HAL_DATA(pAdapter);
	u16 count = 0;

#ifdef PHYDM_MP_SUPPORT
	struct dm_struct *dm = (struct dm_struct *)&pHalData->odmpriv;
	struct phydm_mp *mp = &dm->dm_mp_table;

	if (IS_HARDWARE_TYPE_JAGUAR3(pAdapter)) {
		phydm_mp_get_tx_ok(&pHalData->odmpriv, pAdapter->mppriv.rateidx);
		count = mp->tx_phy_ok_cnt;

	} else
#endif
	{

	if (IS_MPT_CCK_RATE(PMacTxInfo.TX_RATE))
		count = phy_query_bb_reg(pAdapter, 0xF50, bMaskLWord); /* [15:0]*/
	else
		count = phy_query_bb_reg(pAdapter, 0xF50, bMaskHWord); /* [31:16]*/
	}

	if (count > 50000) {
		rtw_reset_phy_trx_ok_counters(pAdapter);
		pAdapter->mppriv.tx.sended += count;
		count = 0;
	}

	return pAdapter->mppriv.tx.sended + count;

}

static	void mpt_StopCckContTx(
	PADAPTER	pAdapter
)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.mpt_ctx);
	u8			u1bReg;

	pMptCtx->bCckContTx = FALSE;
	pMptCtx->bOfdmContTx = FALSE;

	phy_set_bb_reg(pAdapter, rCCK0_System, bCCKBBMode, 0x0);	/*normal mode*/
	phy_set_bb_reg(pAdapter, rCCK0_System, bCCKScramble, 0x1);	/*turn on scramble setting*/

	if (!IS_HARDWARE_TYPE_JAGUAR_ALL(pAdapter)) {
		phy_set_bb_reg(pAdapter, 0xa14, 0x300, 0x0);			/* 0xa15[1:0] = 2b00*/
		phy_set_bb_reg(pAdapter, rOFDM0_TRMuxPar, 0x10000, 0x0);		/* 0xc08[16] = 0*/

		phy_set_bb_reg(pAdapter, rFPGA0_XA_HSSIParameter2, BIT14, 0);
		phy_set_bb_reg(pAdapter, rFPGA0_XB_HSSIParameter2, BIT14, 0);
		phy_set_bb_reg(pAdapter, 0x0B34, BIT14, 0);
	}

	/*BB Reset*/
	phy_set_bb_reg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
	phy_set_bb_reg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);

	if (!IS_HARDWARE_TYPE_JAGUAR_ALL(pAdapter)) {
	phy_set_bb_reg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000100);
	phy_set_bb_reg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000100);
	}

	if (IS_HARDWARE_TYPE_8188E(pAdapter) || IS_HARDWARE_TYPE_8723B(pAdapter) || 
		IS_HARDWARE_TYPE_8703B(pAdapter) || IS_HARDWARE_TYPE_8188F(pAdapter) || 
		IS_HARDWARE_TYPE_8723D(pAdapter) || IS_HARDWARE_TYPE_8192F(pAdapter) || 
		IS_HARDWARE_TYPE_8821C(pAdapter) || IS_HARDWARE_TYPE_8188GTV(pAdapter)) {
		phy_set_bb_reg(pAdapter, 0xA70, BIT(14), bDisable);/* patch Count CCK adjust Rate*/
	}

}	/* mpt_StopCckContTx */


static	void mpt_StopOfdmContTx(
	PADAPTER	pAdapter
)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.mpt_ctx);
	u8			u1bReg;
	u32			data;

	pMptCtx->bCckContTx = FALSE;
	pMptCtx->bOfdmContTx = FALSE;

	if (IS_HARDWARE_TYPE_JAGUAR_ALL(pAdapter))
		phy_set_bb_reg(pAdapter, 0x914, BIT18 | BIT17 | BIT16, OFDM_ALL_OFF);
	else
		phy_set_bb_reg(pAdapter, rOFDM1_LSTF, BIT30 | BIT29 | BIT28, OFDM_ALL_OFF);

	rtw_mdelay_os(10);

	if (!IS_HARDWARE_TYPE_JAGUAR_ALL(pAdapter)){
		phy_set_bb_reg(pAdapter, 0xa14, 0x300, 0x0);			/* 0xa15[1:0] = 0*/
		phy_set_bb_reg(pAdapter, rOFDM0_TRMuxPar, 0x10000, 0x0);		/* 0xc08[16] = 0*/
	}

	/*BB Reset*/
	phy_set_bb_reg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
	phy_set_bb_reg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);

	if (!IS_HARDWARE_TYPE_JAGUAR_ALL(pAdapter)) {
	phy_set_bb_reg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000100);
	phy_set_bb_reg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000100);
	}
}	/* mpt_StopOfdmContTx */


static	void mpt_StartCckContTx(
	PADAPTER		pAdapter
)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.mpt_ctx);
	u32			cckrate;

	/* 1. if CCK block on */
	if (!phy_query_bb_reg(pAdapter, rFPGA0_RFMOD, bCCKEn))
		phy_set_bb_reg(pAdapter, rFPGA0_RFMOD, bCCKEn, 1);/*set CCK block on*/

	/*Turn Off All Test Mode*/
	if (IS_HARDWARE_TYPE_JAGUAR_ALL(pAdapter))
		phy_set_bb_reg(pAdapter, 0x914, BIT18 | BIT17 | BIT16, OFDM_ALL_OFF);
	else
		phy_set_bb_reg(pAdapter, rOFDM1_LSTF, BIT30 | BIT29 | BIT28, OFDM_ALL_OFF);

	cckrate  = pAdapter->mppriv.rateidx;

	phy_set_bb_reg(pAdapter, rCCK0_System, bCCKTxRate, cckrate);

	phy_set_bb_reg(pAdapter, rCCK0_System, bCCKBBMode, 0x2);	/*transmit mode*/
	phy_set_bb_reg(pAdapter, rCCK0_System, bCCKScramble, 0x1);	/*turn on scramble setting*/

	if (!IS_HARDWARE_TYPE_JAGUAR_ALL(pAdapter)) {
		phy_set_bb_reg(pAdapter, 0xa14, 0x300, 0x3);			/* 0xa15[1:0] = 11 force cck rxiq = 0*/
		phy_set_bb_reg(pAdapter, rOFDM0_TRMuxPar, 0x10000, 0x1);		/* 0xc08[16] = 1 force ofdm rxiq = ofdm txiq*/
		phy_set_bb_reg(pAdapter, rFPGA0_XA_HSSIParameter2, BIT14, 1);
		phy_set_bb_reg(pAdapter, rFPGA0_XB_HSSIParameter2, BIT14, 1);
		phy_set_bb_reg(pAdapter, 0x0B34, BIT14, 1);
	}

	if (!IS_HARDWARE_TYPE_JAGUAR_ALL(pAdapter)) {
		phy_set_bb_reg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000500);
		phy_set_bb_reg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000500);
	}

	if (IS_HARDWARE_TYPE_8188E(pAdapter) || IS_HARDWARE_TYPE_8723B(pAdapter) || 
		IS_HARDWARE_TYPE_8703B(pAdapter) || IS_HARDWARE_TYPE_8188F(pAdapter) || 
		IS_HARDWARE_TYPE_8723D(pAdapter) || IS_HARDWARE_TYPE_8192F(pAdapter) || 
		IS_HARDWARE_TYPE_8821C(pAdapter) || IS_HARDWARE_TYPE_8188GTV(pAdapter)) {
		if (pAdapter->mppriv.rateidx == MPT_RATE_1M) /* patch Count CCK adjust Rate*/
			phy_set_bb_reg(pAdapter, 0xA70, BIT(14), bDisable);
		else
			phy_set_bb_reg(pAdapter, 0xA70, BIT(14), bEnable);
	}

	pMptCtx->bCckContTx = TRUE;
	pMptCtx->bOfdmContTx = FALSE;

}	/* mpt_StartCckContTx */


static	void mpt_StartOfdmContTx(
	PADAPTER		pAdapter
)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.mpt_ctx);

	/* 1. if OFDM block on?*/
	if (!phy_query_bb_reg(pAdapter, rFPGA0_RFMOD, bOFDMEn))
		phy_set_bb_reg(pAdapter, rFPGA0_RFMOD, bOFDMEn, 1);/*set OFDM block on*/

	/* 2. set CCK test mode off, set to CCK normal mode*/
	phy_set_bb_reg(pAdapter, rCCK0_System, bCCKBBMode, 0);

	/* 3. turn on scramble setting*/
	phy_set_bb_reg(pAdapter, rCCK0_System, bCCKScramble, 1);

	if (!IS_HARDWARE_TYPE_JAGUAR_ALL(pAdapter)) {
		phy_set_bb_reg(pAdapter, 0xa14, 0x300, 0x3);			/* 0xa15[1:0] = 2b'11*/
		phy_set_bb_reg(pAdapter, rOFDM0_TRMuxPar, 0x10000, 0x1);		/* 0xc08[16] = 1*/
	}

	/* 4. Turn On Continue Tx and turn off the other test modes.*/
	if (IS_HARDWARE_TYPE_JAGUAR_ALL(pAdapter))
		phy_set_bb_reg(pAdapter, 0x914, BIT18 | BIT17 | BIT16, OFDM_ContinuousTx);
	else
		phy_set_bb_reg(pAdapter, rOFDM1_LSTF, BIT30 | BIT29 | BIT28, OFDM_ContinuousTx);

	if (!IS_HARDWARE_TYPE_JAGUAR_ALL(pAdapter)) {
	phy_set_bb_reg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000500);
	phy_set_bb_reg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000500);
	}

	pMptCtx->bCckContTx = FALSE;
	pMptCtx->bOfdmContTx = TRUE;
}	/* mpt_StartOfdmContTx */

#if defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8821B) || defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C)  || defined(CONFIG_RTL8822C)
#ifdef PHYDM_PMAC_TX_SETTING_SUPPORT
static void mpt_convert_phydm_txinfo_for_jaguar3(
	RT_PMAC_TX_INFO	pMacTxInfo, struct phydm_pmac_info *phydmtxinfo)
{
	phydmtxinfo->en_pmac_tx = pMacTxInfo.bEnPMacTx;
	phydmtxinfo->mode = pMacTxInfo.Mode;
	phydmtxinfo->tx_rate = MRateToHwRate(mpt_to_mgnt_rate(pMacTxInfo.TX_RATE));
	phydmtxinfo->tx_sc = pMacTxInfo.TX_SC;
	phydmtxinfo->is_short_preamble = pMacTxInfo.bSPreamble;
	phydmtxinfo->ndp_sound = pMacTxInfo.NDP_sound;
	phydmtxinfo->bw = pMacTxInfo.BandWidth;
	phydmtxinfo->m_stbc = pMacTxInfo.m_STBC;
	phydmtxinfo->packet_period = pMacTxInfo.PacketPeriod;
	phydmtxinfo->packet_count = pMacTxInfo.PacketCount;
	phydmtxinfo->packet_pattern = pMacTxInfo.PacketPattern;
	phydmtxinfo->sfd = pMacTxInfo.SFD;
	phydmtxinfo->signal_field = pMacTxInfo.SignalField;
	phydmtxinfo->service_field = pMacTxInfo.ServiceField;
	phydmtxinfo->length = pMacTxInfo.LENGTH;
	_rtw_memcpy(&phydmtxinfo->crc16,pMacTxInfo.CRC16, 2);
	_rtw_memcpy(&phydmtxinfo->lsig , pMacTxInfo.LSIG,3);
	_rtw_memcpy(&phydmtxinfo->ht_sig , pMacTxInfo.HT_SIG,6);
	_rtw_memcpy(&phydmtxinfo->vht_sig_a , pMacTxInfo.VHT_SIG_A,6);
	_rtw_memcpy(&phydmtxinfo->vht_sig_b , pMacTxInfo.VHT_SIG_B,4);
	phydmtxinfo->vht_sig_b_crc = pMacTxInfo.VHT_SIG_B_CRC;
	_rtw_memcpy(&phydmtxinfo->vht_delimiter,pMacTxInfo.VHT_Delimiter,4);
}
#endif

/* for HW TX mode */
void mpt_ProSetPMacTx(PADAPTER	Adapter)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);
	PMPT_CONTEXT	pMptCtx		=	&(Adapter->mppriv.mpt_ctx);
	struct mp_priv *pmppriv = &Adapter->mppriv;
	RT_PMAC_TX_INFO	PMacTxInfo	=	pMptCtx->PMacTxInfo;
	u32			u4bTmp;
	struct dm_struct *p_dm_odm;

	p_dm_odm = &pHalData->odmpriv;

#if 0
	PRINT_DATA("LSIG ", PMacTxInfo.LSIG, 3);
	PRINT_DATA("HT_SIG", PMacTxInfo.HT_SIG, 6);
	PRINT_DATA("VHT_SIG_A", PMacTxInfo.VHT_SIG_A, 6);
	PRINT_DATA("VHT_SIG_B", PMacTxInfo.VHT_SIG_B, 4);
	dbg_print("VHT_SIG_B_CRC %x\n", PMacTxInfo.VHT_SIG_B_CRC);
	PRINT_DATA("VHT_Delimiter", PMacTxInfo.VHT_Delimiter, 4);

	PRINT_DATA("Src Address", Adapter->mac_addr, ETH_ALEN);
	PRINT_DATA("Dest Address", PMacTxInfo.MacAddress, ETH_ALEN);
#endif
	if (pmppriv->pktInterval != 0)
		PMacTxInfo.PacketPeriod = pmppriv->pktInterval;

    	if (pmppriv->tx.count != 0)
        	PMacTxInfo.PacketCount = pmppriv->tx.count;

	RTW_INFO("SGI %d bSPreamble %d bSTBC %d bLDPC %d NDP_sound %d\n", PMacTxInfo.bSGI, PMacTxInfo.bSPreamble, PMacTxInfo.bSTBC, PMacTxInfo.bLDPC, PMacTxInfo.NDP_sound);
	RTW_INFO("TXSC %d BandWidth %d PacketPeriod %d PacketCount %d PacketLength %d PacketPattern %d\n", PMacTxInfo.TX_SC, PMacTxInfo.BandWidth, PMacTxInfo.PacketPeriod, PMacTxInfo.PacketCount,
		 PMacTxInfo.PacketLength, PMacTxInfo.PacketPattern);

	if (IS_HARDWARE_TYPE_JAGUAR3(Adapter)) {
#ifdef PHYDM_PMAC_TX_SETTING_SUPPORT
		struct phydm_pmac_info phydm_mactxinfo;

		mpt_convert_phydm_txinfo_for_jaguar3(PMacTxInfo, &phydm_mactxinfo);
		phydm_set_pmac_tx(p_dm_odm, &phydm_mactxinfo, pMptCtx->mpt_rf_path);
#endif
		return;
	}

	if (PMacTxInfo.bEnPMacTx == FALSE) {
		if (pMptCtx->HWTxmode == CONTINUOUS_TX) {
			phy_set_bb_reg(Adapter, 0xb04, 0xf, 2);			/*	TX Stop*/
			if (IS_MPT_CCK_RATE(pMptCtx->mpt_rate_index))
				mpt_StopCckContTx(Adapter);
			else
				mpt_StopOfdmContTx(Adapter);
		} else if (IS_MPT_CCK_RATE(pMptCtx->mpt_rate_index)) {
			u4bTmp = phy_query_bb_reg(Adapter, 0xf50, bMaskLWord);
			phy_set_bb_reg(Adapter, 0xb1c, bMaskLWord, u4bTmp + 50);
			phy_set_bb_reg(Adapter, 0xb04, 0xf, 2);		/*TX Stop*/
		} else
			phy_set_bb_reg(Adapter, 0xb04, 0xf, 2);		/*	TX Stop*/

		if (pMptCtx->HWTxmode == OFDM_Single_Tone_TX) {
			/* Stop HW TX -> Stop Continuous TX -> Stop RF Setting*/
			if (IS_MPT_CCK_RATE(pMptCtx->mpt_rate_index))
				mpt_StopCckContTx(Adapter);
			else
				mpt_StopOfdmContTx(Adapter);

			mpt_SetSingleTone_8814A(Adapter, FALSE, TRUE);
		}
		pMptCtx->HWTxmode = TEST_NONE;
		return;
	}

    	pMptCtx->mpt_rate_index = PMacTxInfo.TX_RATE;

	if (PMacTxInfo.Mode == CONTINUOUS_TX) {
		pMptCtx->HWTxmode = CONTINUOUS_TX;
		PMacTxInfo.PacketCount = 1;

        	hal_mpt_SetTxPower(Adapter);

		if (IS_MPT_CCK_RATE(PMacTxInfo.TX_RATE))
			mpt_StartCckContTx(Adapter);
		else
			mpt_StartOfdmContTx(Adapter);
	} else if (PMacTxInfo.Mode == OFDM_Single_Tone_TX) {
		/* Continuous TX -> HW TX -> RF Setting */
		pMptCtx->HWTxmode = OFDM_Single_Tone_TX;
		PMacTxInfo.PacketCount = 1;

		if (IS_MPT_CCK_RATE(PMacTxInfo.TX_RATE))
			mpt_StartCckContTx(Adapter);
		else
			mpt_StartOfdmContTx(Adapter);
	} else if (PMacTxInfo.Mode == PACKETS_TX) {
		pMptCtx->HWTxmode = PACKETS_TX;
		if (IS_MPT_CCK_RATE(PMacTxInfo.TX_RATE) && PMacTxInfo.PacketCount == 0)
			PMacTxInfo.PacketCount = 0xffff;
	}

	if (IS_MPT_CCK_RATE(PMacTxInfo.TX_RATE)) {
		/* 0xb1c[0:15] TX packet count 0xb1C[31:16]	SFD*/
		u4bTmp = PMacTxInfo.PacketCount | (PMacTxInfo.SFD << 16);
		phy_set_bb_reg(Adapter, 0xb1c, bMaskDWord, u4bTmp);
		/* 0xb40 7:0 SIGNAL	15:8 SERVICE	31:16 LENGTH*/
		u4bTmp = PMacTxInfo.SignalField | (PMacTxInfo.ServiceField << 8) | (PMacTxInfo.LENGTH << 16);
		phy_set_bb_reg(Adapter, 0xb40, bMaskDWord, u4bTmp);
		u4bTmp = PMacTxInfo.CRC16[0] | (PMacTxInfo.CRC16[1] << 8);
		phy_set_bb_reg(Adapter, 0xb44, bMaskLWord, u4bTmp);

		if (PMacTxInfo.bSPreamble)
			phy_set_bb_reg(Adapter, 0xb0c, BIT27, 0);
		else
			phy_set_bb_reg(Adapter, 0xb0c, BIT27, 1);
	} else {
		phy_set_bb_reg(Adapter, 0xb18, 0xfffff, PMacTxInfo.PacketCount);

		u4bTmp = PMacTxInfo.LSIG[0] | ((PMacTxInfo.LSIG[1]) << 8) | ((PMacTxInfo.LSIG[2]) << 16) | ((PMacTxInfo.PacketPattern) << 24);
		phy_set_bb_reg(Adapter, 0xb08, bMaskDWord, u4bTmp);	/*	Set 0xb08[23:0] = LSIG, 0xb08[31:24] =  Data init octet*/

		if (PMacTxInfo.PacketPattern == 0x12)
			u4bTmp = 0x3000000;
		else
			u4bTmp = 0;
	}

	if (IS_MPT_HT_RATE(PMacTxInfo.TX_RATE)) {
		u4bTmp |= PMacTxInfo.HT_SIG[0] | ((PMacTxInfo.HT_SIG[1]) << 8) | ((PMacTxInfo.HT_SIG[2]) << 16);
		phy_set_bb_reg(Adapter, 0xb0c, bMaskDWord, u4bTmp);
		u4bTmp = PMacTxInfo.HT_SIG[3] | ((PMacTxInfo.HT_SIG[4]) << 8) | ((PMacTxInfo.HT_SIG[5]) << 16);
		phy_set_bb_reg(Adapter, 0xb10, 0xffffff, u4bTmp);
	} else if (IS_MPT_VHT_RATE(PMacTxInfo.TX_RATE)) {
		u4bTmp |= PMacTxInfo.VHT_SIG_A[0] | ((PMacTxInfo.VHT_SIG_A[1]) << 8) | ((PMacTxInfo.VHT_SIG_A[2]) << 16);
		phy_set_bb_reg(Adapter, 0xb0c, bMaskDWord, u4bTmp);
		u4bTmp = PMacTxInfo.VHT_SIG_A[3] | ((PMacTxInfo.VHT_SIG_A[4]) << 8) | ((PMacTxInfo.VHT_SIG_A[5]) << 16);
		phy_set_bb_reg(Adapter, 0xb10, 0xffffff, u4bTmp);

		_rtw_memcpy(&u4bTmp, PMacTxInfo.VHT_SIG_B, 4);
		phy_set_bb_reg(Adapter, 0xb14, bMaskDWord, u4bTmp);
	}

	if (IS_MPT_VHT_RATE(PMacTxInfo.TX_RATE)) {
		u4bTmp = (PMacTxInfo.VHT_SIG_B_CRC << 24) | PMacTxInfo.PacketPeriod;	/* for TX interval */
		phy_set_bb_reg(Adapter, 0xb20, bMaskDWord, u4bTmp);

		_rtw_memcpy(&u4bTmp, PMacTxInfo.VHT_Delimiter, 4);
		phy_set_bb_reg(Adapter, 0xb24, bMaskDWord, u4bTmp);

		/* 0xb28 - 0xb34 24 byte Probe Request MAC Header*/
		/*& Duration & Frame control*/
		phy_set_bb_reg(Adapter, 0xb28, bMaskDWord, 0x00000040);

		/* Address1 [0:3]*/
		u4bTmp = PMacTxInfo.MacAddress[0] | (PMacTxInfo.MacAddress[1] << 8) | (PMacTxInfo.MacAddress[2] << 16) | (PMacTxInfo.MacAddress[3] << 24);
		phy_set_bb_reg(Adapter, 0xb2C, bMaskDWord, u4bTmp);

		/* Address3 [3:0]*/
		phy_set_bb_reg(Adapter, 0xb38, bMaskDWord, u4bTmp);

		/* Address2[0:1] & Address1 [5:4]*/
		u4bTmp = PMacTxInfo.MacAddress[4] | (PMacTxInfo.MacAddress[5] << 8) | (Adapter->mac_addr[0] << 16) | (Adapter->mac_addr[1] << 24);
		phy_set_bb_reg(Adapter, 0xb30, bMaskDWord, u4bTmp);

		/* Address2 [5:2]*/
		u4bTmp = Adapter->mac_addr[2] | (Adapter->mac_addr[3] << 8) | (Adapter->mac_addr[4] << 16) | (Adapter->mac_addr[5] << 24);
		phy_set_bb_reg(Adapter, 0xb34, bMaskDWord, u4bTmp);

		/* Sequence Control & Address3 [5:4]*/
		/*u4bTmp = PMacTxInfo.MacAddress[4]|(PMacTxInfo.MacAddress[5] << 8) ;*/
		/*phy_set_bb_reg(Adapter, 0xb38, bMaskDWord, u4bTmp);*/
	} else {
		phy_set_bb_reg(Adapter, 0xb20, bMaskDWord, PMacTxInfo.PacketPeriod);	/* for TX interval*/
		/* & Duration & Frame control */
		phy_set_bb_reg(Adapter, 0xb24, bMaskDWord, 0x00000040);

		/* 0xb24 - 0xb38 24 byte Probe Request MAC Header*/
		/* Address1 [0:3]*/
		u4bTmp = PMacTxInfo.MacAddress[0] | (PMacTxInfo.MacAddress[1] << 8) | (PMacTxInfo.MacAddress[2] << 16) | (PMacTxInfo.MacAddress[3] << 24);
		phy_set_bb_reg(Adapter, 0xb28, bMaskDWord, u4bTmp);

		/* Address3 [3:0]*/
		phy_set_bb_reg(Adapter, 0xb34, bMaskDWord, u4bTmp);

		/* Address2[0:1] & Address1 [5:4]*/
		u4bTmp = PMacTxInfo.MacAddress[4] | (PMacTxInfo.MacAddress[5] << 8) | (Adapter->mac_addr[0] << 16) | (Adapter->mac_addr[1] << 24);
		phy_set_bb_reg(Adapter, 0xb2c, bMaskDWord, u4bTmp);

		/* Address2 [5:2] */
		u4bTmp = Adapter->mac_addr[2] | (Adapter->mac_addr[3] << 8) | (Adapter->mac_addr[4] << 16) | (Adapter->mac_addr[5] << 24);
		phy_set_bb_reg(Adapter, 0xb30, bMaskDWord, u4bTmp);

		/* Sequence Control & Address3 [5:4]*/
		u4bTmp = PMacTxInfo.MacAddress[4] | (PMacTxInfo.MacAddress[5] << 8);
		phy_set_bb_reg(Adapter, 0xb38, bMaskDWord, u4bTmp);
	}

	phy_set_bb_reg(Adapter, 0xb48, bMaskByte3, PMacTxInfo.TX_RATE_HEX);

	/* 0xb4c 3:0 TXSC	5:4	BW	7:6 m_STBC	8 NDP_Sound*/
	u4bTmp = (PMacTxInfo.TX_SC) | ((PMacTxInfo.BandWidth) << 4) | ((PMacTxInfo.m_STBC - 1) << 6) | ((PMacTxInfo.NDP_sound) << 8);
	phy_set_bb_reg(Adapter, 0xb4c, 0x1ff, u4bTmp);

	if (IS_HARDWARE_TYPE_JAGUAR2(Adapter)) {
		u32 offset = 0xb44;

		if (IS_MPT_OFDM_RATE(PMacTxInfo.TX_RATE))
			phy_set_bb_reg(Adapter, offset, 0xc0000000, 0);
		else if (IS_MPT_HT_RATE(PMacTxInfo.TX_RATE))
			phy_set_bb_reg(Adapter, offset, 0xc0000000, 1);
		else if (IS_MPT_VHT_RATE(PMacTxInfo.TX_RATE))
			phy_set_bb_reg(Adapter, offset, 0xc0000000, 2);

	} else if(IS_HARDWARE_TYPE_JAGUAR(Adapter)) {
		u32 offset = 0xb4c;

		if(IS_MPT_OFDM_RATE(PMacTxInfo.TX_RATE))
			phy_set_bb_reg(Adapter, offset, 0xc0000000, 0);
		else if(IS_MPT_HT_RATE(PMacTxInfo.TX_RATE))
			phy_set_bb_reg(Adapter, offset, 0xc0000000, 1);
		else if(IS_MPT_VHT_RATE(PMacTxInfo.TX_RATE))
			phy_set_bb_reg(Adapter, offset, 0xc0000000, 2);
	}

	phy_set_bb_reg(Adapter, 0xb00, BIT8, 1);		/*	Turn on PMAC*/
	/* phy_set_bb_reg(Adapter, 0xb04, 0xf, 2);				 */ /* TX Stop */
	if (IS_MPT_CCK_RATE(PMacTxInfo.TX_RATE)) {
		phy_set_bb_reg(Adapter, 0xb04, 0xf, 8);		/*TX CCK ON*/
		phy_set_bb_reg(Adapter, 0xA84, BIT31, 0);
	} else
		phy_set_bb_reg(Adapter, 0xb04, 0xf, 4);		/*	TX Ofdm ON	*/

	if (PMacTxInfo.Mode == OFDM_Single_Tone_TX)
		mpt_SetSingleTone_8814A(Adapter, TRUE, TRUE);

}

#endif

void hal_mpt_SetContinuousTx(PADAPTER pAdapter, u8 bStart)
{
	u8 Rate;

	RTW_INFO("SetContinuousTx: rate:%d\n", pAdapter->mppriv.rateidx);
	Rate = HwRateToMPTRate(pAdapter->mppriv.rateidx);
	pAdapter->mppriv.mpt_ctx.is_start_cont_tx = bStart;

	if (Rate <= MPT_RATE_11M) {
		if (bStart)
			mpt_StartCckContTx(pAdapter);
		else
			mpt_StopCckContTx(pAdapter);

	} else if (Rate >= MPT_RATE_6M) {
		if (bStart)
			mpt_StartOfdmContTx(pAdapter);
		else
			mpt_StopOfdmContTx(pAdapter);
	}
}

#endif /* CONFIG_MP_INCLUDE*/
