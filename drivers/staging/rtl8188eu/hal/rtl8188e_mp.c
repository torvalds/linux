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
#define _RTL8188E_MP_C_

#include <drv_types.h>
#include <rtw_mp.h>
#include <rtl8188e_hal.h>
#include <rtl8188e_dm.h>

s32 Hal_SetPowerTracking(struct adapter *padapter, u8 enable)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(padapter);
	struct odm_dm_struct *pDM_Odm = &(pHalData->odmpriv);

	if (!netif_running(padapter->pnetdev)) {
		RT_TRACE(_module_mp_, _drv_warning_,
			 ("SetPowerTracking! Fail: interface not opened!\n"));
		return _FAIL;
	}

	if (!check_fwstate(&padapter->mlmepriv, WIFI_MP_STATE)) {
		RT_TRACE(_module_mp_, _drv_warning_,
			 ("SetPowerTracking! Fail: not in MP mode!\n"));
		return _FAIL;
	}

	if (enable)
		pDM_Odm->RFCalibrateInfo.bTXPowerTracking = true;
	else
		pDM_Odm->RFCalibrateInfo.bTXPowerTrackingInit = false;

	return _SUCCESS;
}

void Hal_GetPowerTracking(struct adapter *padapter, u8 *enable)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(padapter);
	struct odm_dm_struct *pDM_Odm = &(pHalData->odmpriv);

	*enable = pDM_Odm->RFCalibrateInfo.TxPowerTrackControl;
}

/*-----------------------------------------------------------------------------
 * Function:	mpt_SwitchRfSetting
 *
 * Overview:	Change RF Setting when we siwthc channel/rate/BW for MP.
 *
 * Input:	struct adapter *				pAdapter
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 01/08/2009	MHC		Suggestion from SD3 Willis for 92S series.
 * 01/09/2009	MHC		Add CCK modification for 40MHZ. Suggestion from SD3.
 *
 *---------------------------------------------------------------------------*/
void Hal_mpt_SwitchRfSetting(struct adapter *pAdapter)
{
	struct mp_priv	*pmp = &pAdapter->mppriv;

	/*  <20120525, Kordan> Dynamic mechanism for APK, asked by Dennis. */
		pmp->MptCtx.backup0x52_RF_A = (u8)PHY_QueryRFReg(pAdapter, RF_PATH_A, RF_0x52, 0x000F0);
		pmp->MptCtx.backup0x52_RF_B = (u8)PHY_QueryRFReg(pAdapter, RF_PATH_B, RF_0x52, 0x000F0);
		PHY_SetRFReg(pAdapter, RF_PATH_A, RF_0x52, 0x000F0, 0xD);
		PHY_SetRFReg(pAdapter, RF_PATH_B, RF_0x52, 0x000F0, 0xD);

	return;
}
/*---------------------------hal\rtl8192c\MPT_Phy.c---------------------------*/

/*---------------------------hal\rtl8192c\MPT_HelperFunc.c---------------------------*/
void Hal_MPT_CCKTxPowerAdjust(struct adapter *Adapter, bool bInCH14)
{
	u32		TempVal = 0, TempVal2 = 0, TempVal3 = 0;
	u32		CurrCCKSwingVal = 0, CCKSwingIndex = 12;
	u8		i;

	/*  get current cck swing value and check 0xa22 & 0xa23 later to match the table. */
	CurrCCKSwingVal = read_bbreg(Adapter, rCCK0_TxFilter1, bMaskHWord);

	if (!bInCH14) {
		/*  Readback the current bb cck swing value and compare with the table to */
		/*  get the current swing index */
		for (i = 0; i < CCK_TABLE_SIZE; i++) {
			if (((CurrCCKSwingVal&0xff) == (u32)CCKSwingTable_Ch1_Ch13[i][0]) &&
			    (((CurrCCKSwingVal&0xff00)>>8) == (u32)CCKSwingTable_Ch1_Ch13[i][1])) {
				CCKSwingIndex = i;
				break;
			}
		}

		/* Write 0xa22 0xa23 */
		TempVal = CCKSwingTable_Ch1_Ch13[CCKSwingIndex][0] +
				(CCKSwingTable_Ch1_Ch13[CCKSwingIndex][1]<<8);


		/* Write 0xa24 ~ 0xa27 */
		TempVal2 = 0;
		TempVal2 = CCKSwingTable_Ch1_Ch13[CCKSwingIndex][2] +
				(CCKSwingTable_Ch1_Ch13[CCKSwingIndex][3]<<8) +
				(CCKSwingTable_Ch1_Ch13[CCKSwingIndex][4]<<16)+
				(CCKSwingTable_Ch1_Ch13[CCKSwingIndex][5]<<24);

		/* Write 0xa28  0xa29 */
		TempVal3 = 0;
		TempVal3 = CCKSwingTable_Ch1_Ch13[CCKSwingIndex][6] +
				(CCKSwingTable_Ch1_Ch13[CCKSwingIndex][7]<<8);
	} else {
		for (i = 0; i < CCK_TABLE_SIZE; i++) {
			if (((CurrCCKSwingVal&0xff) == (u32)CCKSwingTable_Ch14[i][0]) &&
			    (((CurrCCKSwingVal&0xff00)>>8) == (u32)CCKSwingTable_Ch14[i][1])) {
				CCKSwingIndex = i;
				break;
			}
		}

		/* Write 0xa22 0xa23 */
		TempVal = CCKSwingTable_Ch14[CCKSwingIndex][0] +
				(CCKSwingTable_Ch14[CCKSwingIndex][1]<<8);

		/* Write 0xa24 ~ 0xa27 */
		TempVal2 = 0;
		TempVal2 = CCKSwingTable_Ch14[CCKSwingIndex][2] +
				(CCKSwingTable_Ch14[CCKSwingIndex][3]<<8) +
				(CCKSwingTable_Ch14[CCKSwingIndex][4]<<16)+
				(CCKSwingTable_Ch14[CCKSwingIndex][5]<<24);

		/* Write 0xa28  0xa29 */
		TempVal3 = 0;
		TempVal3 = CCKSwingTable_Ch14[CCKSwingIndex][6] +
				(CCKSwingTable_Ch14[CCKSwingIndex][7]<<8);
	}

	write_bbreg(Adapter, rCCK0_TxFilter1, bMaskHWord, TempVal);
	write_bbreg(Adapter, rCCK0_TxFilter2, bMaskDWord, TempVal2);
	write_bbreg(Adapter, rCCK0_DebugPort, bMaskLWord, TempVal3);
}

void Hal_MPT_CCKTxPowerAdjustbyIndex(struct adapter *pAdapter, bool beven)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(pAdapter);
	struct mpt_context *pMptCtx = &pAdapter->mppriv.MptCtx;
	struct odm_dm_struct *pDM_Odm = &(pHalData->odmpriv);
	s32		TempCCk;
	u8		CCK_index, CCK_index_old = 0;
	u8		Action = 0;	/* 0: no action, 1: even->odd, 2:odd->even */
	s32		i = 0;


	if (!IS_92C_SERIAL(pHalData->VersionID))
		return;
	if (beven && !pMptCtx->bMptIndexEven) {
		/* odd->even */
		Action = 2;
		pMptCtx->bMptIndexEven = true;
	} else if (!beven && pMptCtx->bMptIndexEven) {
		/* even->odd */
		Action = 1;
		pMptCtx->bMptIndexEven = false;
	}

	if (Action != 0) {
		/* Query CCK default setting From 0xa24 */
		TempCCk = read_bbreg(pAdapter, rCCK0_TxFilter2, bMaskDWord) & bMaskCCK;
		for (i = 0; i < CCK_TABLE_SIZE; i++) {
			if (pDM_Odm->RFCalibrateInfo.bCCKinCH14) {
				if (_rtw_memcmp((void *)&TempCCk, (void *)&CCKSwingTable_Ch14[i][2], 4)) {
					CCK_index_old = (u8)i;
					break;
				}
			} else {
				if (_rtw_memcmp((void *)&TempCCk, (void *)&CCKSwingTable_Ch1_Ch13[i][2], 4)) {
					CCK_index_old = (u8)i;
					break;
				}
			}
		}

		if (Action == 1)
			CCK_index = CCK_index_old - 1;
		else
			CCK_index = CCK_index_old + 1;

		/* Adjust CCK according to gain index */
		if (!pDM_Odm->RFCalibrateInfo.bCCKinCH14) {
			rtw_write8(pAdapter, 0xa22, CCKSwingTable_Ch1_Ch13[CCK_index][0]);
			rtw_write8(pAdapter, 0xa23, CCKSwingTable_Ch1_Ch13[CCK_index][1]);
			rtw_write8(pAdapter, 0xa24, CCKSwingTable_Ch1_Ch13[CCK_index][2]);
			rtw_write8(pAdapter, 0xa25, CCKSwingTable_Ch1_Ch13[CCK_index][3]);
			rtw_write8(pAdapter, 0xa26, CCKSwingTable_Ch1_Ch13[CCK_index][4]);
			rtw_write8(pAdapter, 0xa27, CCKSwingTable_Ch1_Ch13[CCK_index][5]);
			rtw_write8(pAdapter, 0xa28, CCKSwingTable_Ch1_Ch13[CCK_index][6]);
			rtw_write8(pAdapter, 0xa29, CCKSwingTable_Ch1_Ch13[CCK_index][7]);
		} else {
			rtw_write8(pAdapter, 0xa22, CCKSwingTable_Ch14[CCK_index][0]);
			rtw_write8(pAdapter, 0xa23, CCKSwingTable_Ch14[CCK_index][1]);
			rtw_write8(pAdapter, 0xa24, CCKSwingTable_Ch14[CCK_index][2]);
			rtw_write8(pAdapter, 0xa25, CCKSwingTable_Ch14[CCK_index][3]);
			rtw_write8(pAdapter, 0xa26, CCKSwingTable_Ch14[CCK_index][4]);
			rtw_write8(pAdapter, 0xa27, CCKSwingTable_Ch14[CCK_index][5]);
			rtw_write8(pAdapter, 0xa28, CCKSwingTable_Ch14[CCK_index][6]);
			rtw_write8(pAdapter, 0xa29, CCKSwingTable_Ch14[CCK_index][7]);
		}
	}
}
/*---------------------------hal\rtl8192c\MPT_HelperFunc.c---------------------------*/

/*
 * SetChannel
 * Description
 *	Use H2C command to change channel,
 *	not only modify rf register, but also other setting need to be done.
 */
void Hal_SetChannel(struct adapter *pAdapter)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(pAdapter);
	struct mp_priv	*pmp = &pAdapter->mppriv;
	struct odm_dm_struct *pDM_Odm = &(pHalData->odmpriv);
	u8		eRFPath;
	u8		channel = pmp->channel;

	/*  set RF channel register */
	for (eRFPath = 0; eRFPath < pHalData->NumTotalRFPath; eRFPath++)
		_write_rfreg(pAdapter, eRFPath, ODM_CHANNEL, 0x3FF, channel);
	Hal_mpt_SwitchRfSetting(pAdapter);

	SelectChannel(pAdapter, channel);

	if (pHalData->CurrentChannel == 14 && !pDM_Odm->RFCalibrateInfo.bCCKinCH14) {
		pDM_Odm->RFCalibrateInfo.bCCKinCH14 = true;
		Hal_MPT_CCKTxPowerAdjust(pAdapter, pDM_Odm->RFCalibrateInfo.bCCKinCH14);
	} else if (pHalData->CurrentChannel != 14 && pDM_Odm->RFCalibrateInfo.bCCKinCH14) {
		pDM_Odm->RFCalibrateInfo.bCCKinCH14 = false;
		Hal_MPT_CCKTxPowerAdjust(pAdapter, pDM_Odm->RFCalibrateInfo.bCCKinCH14);
	}
}

/*
 * Notice
 *	Switch bandwitdth may change center frequency(channel)
 */
void Hal_SetBandwidth(struct adapter *pAdapter)
{
	struct mp_priv *pmp = &pAdapter->mppriv;


	SetBWMode(pAdapter, pmp->bandwidth, pmp->prime_channel_offset);
	Hal_mpt_SwitchRfSetting(pAdapter);
}

void Hal_SetCCKTxPower(struct adapter *pAdapter, u8 *TxPower)
{
	u32 tmpval = 0;


	/*  rf-A cck tx power */
	write_bbreg(pAdapter, rTxAGC_A_CCK1_Mcs32, bMaskByte1, TxPower[RF_PATH_A]);
	tmpval = (TxPower[RF_PATH_A]<<16) | (TxPower[RF_PATH_A]<<8) | TxPower[RF_PATH_A];
	write_bbreg(pAdapter, rTxAGC_B_CCK11_A_CCK2_11, 0xffffff00, tmpval);

	/*  rf-B cck tx power */
	write_bbreg(pAdapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte0, TxPower[RF_PATH_B]);
	tmpval = (TxPower[RF_PATH_B]<<16) | (TxPower[RF_PATH_B]<<8) | TxPower[RF_PATH_B];
	write_bbreg(pAdapter, rTxAGC_B_CCK1_55_Mcs32, 0xffffff00, tmpval);

	RT_TRACE(_module_mp_, _drv_notice_,
		 ("-SetCCKTxPower: A[0x%02x] B[0x%02x]\n",
		  TxPower[RF_PATH_A], TxPower[RF_PATH_B]));
}

void Hal_SetOFDMTxPower(struct adapter *pAdapter, u8 *TxPower)
{
	u32 TxAGC = 0;
	u8 tmpval = 0;

	/*  HT Tx-rf(A) */
	tmpval = TxPower[RF_PATH_A];
	TxAGC = (tmpval<<24) | (tmpval<<16) | (tmpval<<8) | tmpval;

	write_bbreg(pAdapter, rTxAGC_A_Rate18_06, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_A_Rate54_24, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_A_Mcs03_Mcs00, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_A_Mcs07_Mcs04, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_A_Mcs11_Mcs08, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_A_Mcs15_Mcs12, bMaskDWord, TxAGC);

	/*  HT Tx-rf(B) */
	tmpval = TxPower[RF_PATH_B];
	TxAGC = (tmpval<<24) | (tmpval<<16) | (tmpval<<8) | tmpval;

	write_bbreg(pAdapter, rTxAGC_B_Rate18_06, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_B_Rate54_24, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_B_Mcs03_Mcs00, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_B_Mcs07_Mcs04, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_B_Mcs11_Mcs08, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_B_Mcs15_Mcs12, bMaskDWord, TxAGC);
}

void Hal_SetAntennaPathPower(struct adapter *pAdapter)
{
	struct hal_data_8188e *pHalData = GET_HAL_DATA(pAdapter);
	u8 TxPowerLevel[MAX_RF_PATH_NUMS];
	u8 rfPath;

	TxPowerLevel[RF_PATH_A] = pAdapter->mppriv.txpoweridx;
	TxPowerLevel[RF_PATH_B] = pAdapter->mppriv.txpoweridx_b;

	switch (pAdapter->mppriv.antenna_tx) {
	case ANTENNA_A:
	default:
		rfPath = RF_PATH_A;
		break;
	case ANTENNA_B:
		rfPath = RF_PATH_B;
		break;
	case ANTENNA_C:
		rfPath = RF_PATH_C;
		break;
	}

	switch (pHalData->rf_chip) {
	case RF_8225:
	case RF_8256:
	case RF_6052:
		Hal_SetCCKTxPower(pAdapter, TxPowerLevel);
		if (pAdapter->mppriv.rateidx < MPT_RATE_6M)	/*  CCK rate */
			Hal_MPT_CCKTxPowerAdjustbyIndex(pAdapter, TxPowerLevel[rfPath]%2 == 0);
		Hal_SetOFDMTxPower(pAdapter, TxPowerLevel);
		break;
	default:
		break;
	}
}

void Hal_SetTxPower(struct adapter *pAdapter)
{
	struct hal_data_8188e *pHalData = GET_HAL_DATA(pAdapter);
	u8 TxPower = pAdapter->mppriv.txpoweridx;
	u8 TxPowerLevel[MAX_RF_PATH_NUMS];
	u8 rf, rfPath;

	for (rf = 0; rf < MAX_RF_PATH_NUMS; rf++)
		TxPowerLevel[rf] = TxPower;

	switch (pAdapter->mppriv.antenna_tx) {
	case ANTENNA_A:
	default:
		rfPath = RF_PATH_A;
		break;
	case ANTENNA_B:
		rfPath = RF_PATH_B;
		break;
	case ANTENNA_C:
		rfPath = RF_PATH_C;
		break;
	}

	switch (pHalData->rf_chip) {
	/*  2008/09/12 MH Test only !! We enable the TX power tracking for MP!!!!! */
	/*  We should call normal driver API later!! */
	case RF_8225:
	case RF_8256:
	case RF_6052:
		Hal_SetCCKTxPower(pAdapter, TxPowerLevel);
		if (pAdapter->mppriv.rateidx < MPT_RATE_6M)	/*  CCK rate */
			Hal_MPT_CCKTxPowerAdjustbyIndex(pAdapter, TxPowerLevel[rfPath]%2 == 0);
		Hal_SetOFDMTxPower(pAdapter, TxPowerLevel);
		break;
	default:
		break;
	}
}

void Hal_SetDataRate(struct adapter *pAdapter)
{
	Hal_mpt_SwitchRfSetting(pAdapter);
}

void Hal_SetAntenna(struct adapter *pAdapter)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(pAdapter);

	struct ant_sel_ofdm *p_ofdm_tx;	/* OFDM Tx register */
	struct ant_sel_cck *p_cck_txrx;
	u8	r_rx_antenna_ofdm = 0, r_ant_select_cck_val = 0;
	u8	chgTx = 0, chgRx = 0;
	u32	r_ant_select_ofdm_val = 0, r_ofdm_tx_en_val = 0;


	p_ofdm_tx = (struct ant_sel_ofdm *)&r_ant_select_ofdm_val;
	p_cck_txrx = (struct ant_sel_cck *)&r_ant_select_cck_val;

	p_ofdm_tx->r_ant_ht1	= 0x1;
	p_ofdm_tx->r_ant_ht2	= 0x2;	/*  Second TX RF path is A */
	p_ofdm_tx->r_ant_non_ht = 0x3;	/*  0x1+0x2=0x3 */

	switch (pAdapter->mppriv.antenna_tx) {
	case ANTENNA_A:
		p_ofdm_tx->r_tx_antenna		= 0x1;
		r_ofdm_tx_en_val		= 0x1;
		p_ofdm_tx->r_ant_l		= 0x1;
		p_ofdm_tx->r_ant_ht_s1		= 0x1;
		p_ofdm_tx->r_ant_non_ht_s1	= 0x1;
		p_cck_txrx->r_ccktx_enable	= 0x8;
		chgTx = 1;

		/*  From SD3 Willis suggestion !!! Set RF A=TX and B as standby */
		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter2, 0xe, 2);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter2, 0xe, 1);
		r_ofdm_tx_en_val		= 0x3;

		/*  Power save */

		/*  We need to close RFB by SW control */
		if (pHalData->rf_type == RF_2T2R) {
			PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT10, 0);
			PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT26, 1);
			PHY_SetBBReg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT10, 0);
			PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT1, 1);
			PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT17, 0);
		}
		break;
	case ANTENNA_B:
		p_ofdm_tx->r_tx_antenna		= 0x2;
		r_ofdm_tx_en_val		= 0x2;
		p_ofdm_tx->r_ant_l		= 0x2;
		p_ofdm_tx->r_ant_ht_s1		= 0x2;
		p_ofdm_tx->r_ant_non_ht_s1	= 0x2;
		p_cck_txrx->r_ccktx_enable	= 0x4;
		chgTx = 1;
		/*  From SD3 Willis suggestion !!! Set RF A as standby */
		PHY_SetBBReg(pAdapter, rFPGA0_XA_HSSIParameter2, 0xe, 1);
		PHY_SetBBReg(pAdapter, rFPGA0_XB_HSSIParameter2, 0xe, 2);

		/*  Power save */
		/* cosa r_ant_select_ofdm_val = 0x22222222; */

		/*  2008/10/31 MH From SD3 Willi's suggestion. We must read RF 1T table. */
		/*  2009/01/08 MH From Sd3 Willis. We need to close RFA by SW control */
		if (pHalData->rf_type == RF_2T2R || pHalData->rf_type == RF_1T2R) {
			PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT10, 1);
			PHY_SetBBReg(pAdapter, rFPGA0_XA_RFInterfaceOE, BIT10, 0);
			PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT26, 0);
			PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT1, 0);
			PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT17, 1);
		}
		break;
	case ANTENNA_AB:	/*  For 8192S */
		p_ofdm_tx->r_tx_antenna		= 0x3;
		r_ofdm_tx_en_val		= 0x3;
		p_ofdm_tx->r_ant_l		= 0x3;
		p_ofdm_tx->r_ant_ht_s1		= 0x3;
		p_ofdm_tx->r_ant_non_ht_s1	= 0x3;
		p_cck_txrx->r_ccktx_enable	= 0xC;
		chgTx = 1;

		/*  From SD3 Willis suggestion !!! Set RF B as standby */
		PHY_SetBBReg(pAdapter, rFPGA0_XA_HSSIParameter2, 0xe, 2);
		PHY_SetBBReg(pAdapter, rFPGA0_XB_HSSIParameter2, 0xe, 2);

		/*  Disable Power save */
		/* cosa r_ant_select_ofdm_val = 0x3321333; */
		/*  2009/01/08 MH From Sd3 Willis. We need to enable RFA/B by SW control */
		if (pHalData->rf_type == RF_2T2R) {
			PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT10, 0);
			PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT26, 0);
			PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT1, 1);
			PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT17, 1);
		}
		break;
	default:
		break;
	}

	/*  r_rx_antenna_ofdm, bit0=A, bit1=B, bit2=C, bit3=D */
	/*  r_cckrx_enable : CCK default, 0=A, 1=B, 2=C, 3=D */
	/*  r_cckrx_enable_2 : CCK option, 0=A, 1=B, 2=C, 3=D */
	switch (pAdapter->mppriv.antenna_rx) {
	case ANTENNA_A:
		r_rx_antenna_ofdm		= 0x1;	/*  A */
		p_cck_txrx->r_cckrx_enable	= 0x0;	/*  default: A */
		p_cck_txrx->r_cckrx_enable_2	= 0x0;	/*  option: A */
		chgRx = 1;
		break;
	case ANTENNA_B:
		r_rx_antenna_ofdm		= 0x2;	/*  B */
		p_cck_txrx->r_cckrx_enable	= 0x1;	/*  default: B */
		p_cck_txrx->r_cckrx_enable_2	= 0x1;	/*  option: B */
		chgRx = 1;
		break;
	case ANTENNA_AB:
		r_rx_antenna_ofdm		= 0x3;	/*  AB */
		p_cck_txrx->r_cckrx_enable	= 0x0;	/*  default:A */
		p_cck_txrx->r_cckrx_enable_2	= 0x1;	/*  option:B */
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
			/* r_ant_sel_cck_val = r_ant_select_cck_val; */
			PHY_SetBBReg(pAdapter, rFPGA1_TxInfo, 0x7fffffff, r_ant_select_ofdm_val);	/* OFDM Tx */
			PHY_SetBBReg(pAdapter, rFPGA0_TxInfo, 0x0000000f, r_ofdm_tx_en_val);		/* OFDM Tx */
			PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable, 0x0000000f, r_rx_antenna_ofdm);	/* OFDM Rx */
			PHY_SetBBReg(pAdapter, rOFDM1_TRxPathEnable, 0x0000000f, r_rx_antenna_ofdm);	/* OFDM Rx */
			PHY_SetBBReg(pAdapter, rCCK0_AFESetting, bMaskByte3, r_ant_select_cck_val);	/* CCK TxRx */

			break;
		default:
			break;
		}
	}

	RT_TRACE(_module_mp_, _drv_notice_, ("-SwitchAntenna: finished\n"));
}

s32 Hal_SetThermalMeter(struct adapter *pAdapter, u8 target_ther)
{
	struct hal_data_8188e *pHalData = GET_HAL_DATA(pAdapter);


	if (!netif_running(pAdapter->pnetdev)) {
		RT_TRACE(_module_mp_, _drv_warning_, ("SetThermalMeter! Fail: interface not opened!\n"));
		return _FAIL;
	}

	if (check_fwstate(&pAdapter->mlmepriv, WIFI_MP_STATE) == false) {
		RT_TRACE(_module_mp_, _drv_warning_, ("SetThermalMeter: Fail! not in MP mode!\n"));
		return _FAIL;
	}

	target_ther &= 0xff;
	if (target_ther < 0x07)
		target_ther = 0x07;
	else if (target_ther > 0x1d)
		target_ther = 0x1d;

	pHalData->EEPROMThermalMeter = target_ther;

	return _SUCCESS;
}

void Hal_TriggerRFThermalMeter(struct adapter *pAdapter)
{
	_write_rfreg(pAdapter, RF_PATH_A , RF_T_METER_88E , BIT17 | BIT16 , 0x03);
}

u8 Hal_ReadRFThermalMeter(struct adapter *pAdapter)
{
	u32 ThermalValue = 0;

	ThermalValue = _read_rfreg(pAdapter, RF_PATH_A, RF_T_METER_88E, 0xfc00);
	return (u8)ThermalValue;
}

void Hal_GetThermalMeter(struct adapter *pAdapter, u8 *value)
{
	Hal_TriggerRFThermalMeter(pAdapter);
	msleep(1000);
	*value = Hal_ReadRFThermalMeter(pAdapter);
}

void Hal_SetSingleCarrierTx(struct adapter *pAdapter, u8 bStart)
{
	pAdapter->mppriv.MptCtx.bSingleCarrier = bStart;
	if (bStart) {
		/*  Start Single Carrier. */
		RT_TRACE(_module_mp_, _drv_alert_, ("SetSingleCarrierTx: test start\n"));
		/*  1. if OFDM block on? */
		if (!read_bbreg(pAdapter, rFPGA0_RFMOD, bOFDMEn))
			write_bbreg(pAdapter, rFPGA0_RFMOD, bOFDMEn, bEnable);/* set OFDM block on */

		/*  2. set CCK test mode off, set to CCK normal mode */
		write_bbreg(pAdapter, rCCK0_System, bCCKBBMode, bDisable);
		/*  3. turn on scramble setting */
		write_bbreg(pAdapter, rCCK0_System, bCCKScramble, bEnable);
		/*  4. Turn On Single Carrier Tx and turn off the other test modes. */
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bDisable);
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bEnable);
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);
		/* for dynamic set Power index. */
		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000500);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000500);
	} else {
		/*  Stop Single Carrier. */
		RT_TRACE(_module_mp_, _drv_alert_, ("SetSingleCarrierTx: test stop\n"));

		/*  Turn off all test modes. */
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bDisable);
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bDisable);
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);
		msleep(10);

		/* BB Reset */
		write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
		write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);

		/* Stop for dynamic set Power index. */
		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000100);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000100);
	}
}


void Hal_SetSingleToneTx(struct adapter *pAdapter, u8 bStart)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(pAdapter);
	bool		is92C = IS_92C_SERIAL(pHalData->VersionID);

	u8 rfPath;
	u32              reg58 = 0x0;
	switch (pAdapter->mppriv.antenna_tx) {
	case ANTENNA_A:
	default:
		rfPath = RF_PATH_A;
		break;
	case ANTENNA_B:
		rfPath = RF_PATH_B;
		break;
	case ANTENNA_C:
		rfPath = RF_PATH_C;
		break;
	}

	pAdapter->mppriv.MptCtx.bSingleTone = bStart;
	if (bStart) {
		/*  Start Single Tone. */
		RT_TRACE(_module_mp_, _drv_alert_, ("SetSingleToneTx: test start\n"));
		/*  <20120326, Kordan> To amplify the power of tone for Xtal calibration. (asked by Edlu) */
		reg58 = PHY_QueryRFReg(pAdapter, RF_PATH_A, LNA_Low_Gain_3, bRFRegOffsetMask);
		reg58 &= 0xFFFFFFF0;
		reg58 += 2;
		PHY_SetRFReg(pAdapter, RF_PATH_A, LNA_Low_Gain_3, bRFRegOffsetMask, reg58);
		PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, bCCKEn, 0x0);
		PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, bOFDMEn, 0x0);

		if (is92C) {
			_write_rfreg(pAdapter, RF_PATH_A, 0x21, BIT19, 0x01);
			msleep(1);
			if (rfPath == RF_PATH_A)
				write_rfreg(pAdapter, RF_PATH_B, 0x00, 0x10000); /*  PAD all on. */
			else if (rfPath == RF_PATH_B)
				write_rfreg(pAdapter, RF_PATH_A, 0x00, 0x10000); /*  PAD all on. */
			write_rfreg(pAdapter, rfPath, 0x00, 0x2001f); /*  PAD all on. */
			msleep(1);
		} else {
			write_rfreg(pAdapter, rfPath, 0x21, 0xd4000);
			msleep(1);
			write_rfreg(pAdapter, rfPath, 0x00, 0x2001f); /*  PAD all on. */
			msleep(1);
		}

		/* for dynamic set Power index. */
		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000500);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000500);

	} else {
		/*  Stop Single Tone. */
		RT_TRACE(_module_mp_, _drv_alert_, ("SetSingleToneTx: test stop\n"));

		/*  <20120326, Kordan> To amplify the power of tone for Xtal calibration. (asked by Edlu) */
		/*  <20120326, Kordan> Only in single tone mode. (asked by Edlu) */
		reg58 = PHY_QueryRFReg(pAdapter, RF_PATH_A, LNA_Low_Gain_3, bRFRegOffsetMask);
		reg58 &= 0xFFFFFFF0;
		PHY_SetRFReg(pAdapter, RF_PATH_A, LNA_Low_Gain_3, bRFRegOffsetMask, reg58);
		write_bbreg(pAdapter, rFPGA0_RFMOD, bCCKEn, 0x1);
		write_bbreg(pAdapter, rFPGA0_RFMOD, bOFDMEn, 0x1);
		if (is92C) {
			_write_rfreg(pAdapter, RF_PATH_A, 0x21, BIT19, 0x00);
			msleep(1);
			write_rfreg(pAdapter, RF_PATH_A, 0x00, 0x32d75); /*  PAD all on. */
			write_rfreg(pAdapter, RF_PATH_B, 0x00, 0x32d75); /*  PAD all on. */
			msleep(1);
		} else {
			write_rfreg(pAdapter, rfPath, 0x21, 0x54000);
			msleep(1);
			write_rfreg(pAdapter, rfPath, 0x00, 0x30000); /*  PAD all on. */
			msleep(1);
		}

		/* Stop for dynamic set Power index. */
		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000100);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000100);
	}
}



void Hal_SetCarrierSuppressionTx(struct adapter *pAdapter, u8 bStart)
{
	pAdapter->mppriv.MptCtx.bCarrierSuppression = bStart;
	if (bStart) {
		/*  Start Carrier Suppression. */
		RT_TRACE(_module_mp_, _drv_alert_, ("SetCarrierSuppressionTx: test start\n"));
		if (pAdapter->mppriv.rateidx <= MPT_RATE_11M) {
			/*  1. if CCK block on? */
			if (!read_bbreg(pAdapter, rFPGA0_RFMOD, bCCKEn))
				write_bbreg(pAdapter, rFPGA0_RFMOD, bCCKEn, bEnable);/* set CCK block on */

			/* Turn Off All Test Mode */
			write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bDisable);
			write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bDisable);
			write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);

			write_bbreg(pAdapter, rCCK0_System, bCCKBBMode, 0x2);    /* transmit mode */
			write_bbreg(pAdapter, rCCK0_System, bCCKScramble, 0x0);  /* turn off scramble setting */

			/* Set CCK Tx Test Rate */
			write_bbreg(pAdapter, rCCK0_System, bCCKTxRate, 0x0);    /* Set FTxRate to 1Mbps */
		}

		/* for dynamic set Power index. */
		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000500);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000500);
	} else {
		/*  Stop Carrier Suppression. */
		RT_TRACE(_module_mp_, _drv_alert_, ("SetCarrierSuppressionTx: test stop\n"));
		if (pAdapter->mppriv.rateidx <= MPT_RATE_11M) {
			write_bbreg(pAdapter, rCCK0_System, bCCKBBMode, 0x0);    /* normal mode */
			write_bbreg(pAdapter, rCCK0_System, bCCKScramble, 0x1);  /* turn on scramble setting */

			/* BB Reset */
			write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
			write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);
		}

		/* Stop for dynamic set Power index. */
		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000100);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000100);
	}
}

void Hal_SetCCKContinuousTx(struct adapter *pAdapter, u8 bStart)
{
	u32 cckrate;

	if (bStart) {
		RT_TRACE(_module_mp_, _drv_alert_,
			 ("SetCCKContinuousTx: test start\n"));

		/*  1. if CCK block on? */
		if (!read_bbreg(pAdapter, rFPGA0_RFMOD, bCCKEn))
			write_bbreg(pAdapter, rFPGA0_RFMOD, bCCKEn, bEnable);/* set CCK block on */

		/* Turn Off All Test Mode */
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bDisable);
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bDisable);
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);
		/* Set CCK Tx Test Rate */
		cckrate  = pAdapter->mppriv.rateidx;
		write_bbreg(pAdapter, rCCK0_System, bCCKTxRate, cckrate);
		write_bbreg(pAdapter, rCCK0_System, bCCKBBMode, 0x2);	/* transmit mode */
		write_bbreg(pAdapter, rCCK0_System, bCCKScramble, bEnable);	/* turn on scramble setting */

		/* for dynamic set Power index. */
		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000500);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000500);
	} else {
		RT_TRACE(_module_mp_, _drv_info_,
			 ("SetCCKContinuousTx: test stop\n"));

		write_bbreg(pAdapter, rCCK0_System, bCCKBBMode, 0x0);	/* normal mode */
		write_bbreg(pAdapter, rCCK0_System, bCCKScramble, bEnable);	/* turn on scramble setting */

		/* BB Reset */
		write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
		write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);

		/* Stop for dynamic set Power index. */
		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000100);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000100);
	}

	pAdapter->mppriv.MptCtx.bCckContTx = bStart;
	pAdapter->mppriv.MptCtx.bOfdmContTx = false;
} /* mpt_StartCckContTx */

void Hal_SetOFDMContinuousTx(struct adapter *pAdapter, u8 bStart)
{
	if (bStart) {
		RT_TRACE(_module_mp_, _drv_info_, ("SetOFDMContinuousTx: test start\n"));
		/*  1. if OFDM block on? */
		if (!read_bbreg(pAdapter, rFPGA0_RFMOD, bOFDMEn))
			write_bbreg(pAdapter, rFPGA0_RFMOD, bOFDMEn, bEnable);/* set OFDM block on */

		/*  2. set CCK test mode off, set to CCK normal mode */
		write_bbreg(pAdapter, rCCK0_System, bCCKBBMode, bDisable);

		/*  3. turn on scramble setting */
		write_bbreg(pAdapter, rCCK0_System, bCCKScramble, bEnable);
		/*  4. Turn On Continue Tx and turn off the other test modes. */
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bEnable);
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bDisable);
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);

		/* for dynamic set Power index. */
		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000500);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000500);

	} else {
		RT_TRACE(_module_mp_, _drv_info_, ("SetOFDMContinuousTx: test stop\n"));
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bDisable);
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bDisable);
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);
		/* Delay 10 ms */
		msleep(10);
		/* BB Reset */
		write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
		write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);

		/* Stop for dynamic set Power index. */
		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000100);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000100);
	}

	pAdapter->mppriv.MptCtx.bCckContTx = false;
	pAdapter->mppriv.MptCtx.bOfdmContTx = bStart;
} /* mpt_StartOfdmContTx */

void Hal_SetContinuousTx(struct adapter *pAdapter, u8 bStart)
{
	RT_TRACE(_module_mp_, _drv_info_,
		 ("SetContinuousTx: rate:%d\n", pAdapter->mppriv.rateidx));

	pAdapter->mppriv.MptCtx.bStartContTx = bStart;
	if (pAdapter->mppriv.rateidx <= MPT_RATE_11M)
		Hal_SetCCKContinuousTx(pAdapter, bStart);
	else if ((pAdapter->mppriv.rateidx >= MPT_RATE_6M) &&
		 (pAdapter->mppriv.rateidx <= MPT_RATE_MCS15))
		Hal_SetOFDMContinuousTx(pAdapter, bStart);
}
