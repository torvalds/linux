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
#define _RTL8723B_MP_C_
#ifdef CONFIG_MP_INCLUDED

#include <rtl8723b_hal.h>


/*-----------------------------------------------------------------------------
 * Function:	mpt_SwitchRfSetting
 *
 * Overview:	Change RF Setting when we siwthc channel/rate/BW for MP.
 *
 * Input:		IN	PADAPTER				pAdapter
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 * When 		Who 	Remark
 * 01/08/2009	MHC 	Suggestion from SD3 Willis for 92S series.
 * 01/09/2009	MHC 	Add CCK modification for 40MHZ. Suggestion from SD3.
 *
 *---------------------------------------------------------------------------*/
 static void phy_SwitchRfSetting(PADAPTER	 pAdapter,u8 channel )
{
/*
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(pAdapter);
    u32					u4RF_IPA[3], u4RF_TXBIAS, u4RF_SYN_G2;

	//default value
	{
		u4RF_IPA[0] = 0x4F424;			//CCK
		u4RF_IPA[1] = 0xCF424;			//OFDM
		u4RF_IPA[2] = 0x8F424;			//MCS
		u4RF_TXBIAS = 0xC0356;
		u4RF_SYN_G2 = 0x4F200;
	}

	switch(channel)
	{
		case 1:
			u4RF_IPA[0] = 0x4F40C;
			u4RF_IPA[1] = 0xCF466;
			u4RF_TXBIAS = 0xC0350;
			break;

		case 2:
			u4RF_IPA[0] =  0x4F407;
			u4RF_TXBIAS =  0xC0350;
			break;

		case 3:
			u4RF_IPA[0] =  0x4F407;
			u4RF_IPA[2] =  0x8F466;
			u4RF_TXBIAS =  0xC0350;
			break;

		case 5:
		case 8:
			u4RF_SYN_G2 =  0x0F400;
			break;

		case 6:
		case 13:
			u4RF_IPA[0] =  0x4F40C;
			break;

		case 7:
			u4RF_IPA[0] =  0x4F40C;
			u4RF_SYN_G2 =  0x0F400;
			break;

		case 9:
			u4RF_IPA[2] =  0x8F454;
			u4RF_SYN_G2 =  0x0F400;
			break;

		case 11:
			u4RF_IPA[0] =  0x4F40C;
			u4RF_IPA[1] =  0xCF454;
			u4RF_SYN_G2 =  0x0F400;
			break;

		default:
			u4RF_IPA[0] =  0x4F424;
			u4RF_IPA[1] =  0x8F424;
			u4RF_IPA[2] =  0xCF424;
			u4RF_TXBIAS =  0xC0356;
			u4RF_SYN_G2 =  0x4F200;
			break;
	}

	PHY_SetRFReg(pAdapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, u4RF_IPA[0]);
	PHY_SetRFReg(pAdapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, u4RF_IPA[1]);
	PHY_SetRFReg(pAdapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, u4RF_IPA[2]);
	PHY_SetRFReg(pAdapter, RF_PATH_A, RF_TXBIAS, bRFRegOffsetMask, u4RF_TXBIAS);
	PHY_SetRFReg(pAdapter, RF_PATH_A, RF_SYN_G2, bRFRegOffsetMask, u4RF_SYN_G2);
*/
}

void Hal_mpt_SwitchRfSetting(PADAPTER pAdapter)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT		pMptCtx = &(pAdapter->mppriv.MptCtx);
	u8				ChannelToSw = pMptCtx->MptChannelToSw;

	phy_SwitchRfSetting(pAdapter, ChannelToSw);
}

s32 Hal_SetPowerTracking(PADAPTER padapter, u8 enable)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;


	if (!netif_running(padapter->pnetdev)) {
		RT_TRACE(_module_mp_, _drv_warning_, ("SetPowerTracking! Fail: interface not opened!\n"));
		return _FAIL;
	}

	if (check_fwstate(&padapter->mlmepriv, WIFI_MP_STATE) == _FALSE) {
		RT_TRACE(_module_mp_, _drv_warning_, ("SetPowerTracking! Fail: not in MP mode!\n"));
		return _FAIL;
	}

	if (enable)
		pdmpriv->TxPowerTrackControl = _TRUE;
	else
		pdmpriv->TxPowerTrackControl = _FALSE;

	return _SUCCESS;
}

void Hal_GetPowerTracking(PADAPTER padapter, u8 *enable)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;


	*enable = pdmpriv->TxPowerTrackControl;
}

static void Hal_disable_dm(PADAPTER padapter)
{
	u8 v8;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;


	//3 1. disable firmware dynamic mechanism
	// disable Power Training, Rate Adaptive
	v8 = rtw_read8(padapter, REG_BCN_CTRL);
	v8 &= ~EN_BCN_FUNCTION;
	rtw_write8(padapter, REG_BCN_CTRL, v8);

	//3 2. disable driver dynamic mechanism
	// disable Dynamic Initial Gain
	// disable High Power
	// disable Power Tracking
	Switch_DM_Func(padapter, DYNAMIC_FUNC_DISABLE, _FALSE);

	// enable APK, LCK and IQK but disable power tracking
	pdmpriv->TxPowerTrackControl = _FALSE;
	Switch_DM_Func(padapter, DYNAMIC_RF_TX_PWR_TRACK , _TRUE);
}

void Hal_MPT_CCKTxPowerAdjust(PADAPTER Adapter, BOOLEAN bInCH14)
{
	u32		TempVal = 0, TempVal2 = 0, TempVal3 = 0;
	u32		CurrCCKSwingVal = 0, CCKSwingIndex = 12;
	u8		i;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);


	// get current cck swing value and check 0xa22 & 0xa23 later to match the table.
	CurrCCKSwingVal = read_bbreg(Adapter, rCCK0_TxFilter1, bMaskHWord);

	if (!bInCH14)
	{
		// Readback the current bb cck swing value and compare with the table to
		// get the current swing index
		for (i = 0; i < CCK_TABLE_SIZE; i++)
		{
			if (((CurrCCKSwingVal&0xff) == (u32)CCKSwingTable_Ch1_Ch13[i][0]) &&
				(((CurrCCKSwingVal&0xff00)>>8) == (u32)CCKSwingTable_Ch1_Ch13[i][1]))
			{
				CCKSwingIndex = i;
//				RT_TRACE(COMP_INIT, DBG_LOUD,("Ch1~13, Current reg0x%x = 0x%lx, CCKSwingIndex=0x%x\n",
//					(rCCK0_TxFilter1+2), CurrCCKSwingVal, CCKSwingIndex));
				break;
			}
		}

		//Write 0xa22 0xa23
		TempVal = CCKSwingTable_Ch1_Ch13[CCKSwingIndex][0] +
				(CCKSwingTable_Ch1_Ch13[CCKSwingIndex][1]<<8) ;


		//Write 0xa24 ~ 0xa27
		TempVal2 = 0;
		TempVal2 = CCKSwingTable_Ch1_Ch13[CCKSwingIndex][2] +
				(CCKSwingTable_Ch1_Ch13[CCKSwingIndex][3]<<8) +
				(CCKSwingTable_Ch1_Ch13[CCKSwingIndex][4]<<16 )+
				(CCKSwingTable_Ch1_Ch13[CCKSwingIndex][5]<<24);

		//Write 0xa28  0xa29
		TempVal3 = 0;
		TempVal3 = CCKSwingTable_Ch1_Ch13[CCKSwingIndex][6] +
				(CCKSwingTable_Ch1_Ch13[CCKSwingIndex][7]<<8) ;
	}
	else
	{
		for (i = 0; i < CCK_TABLE_SIZE; i++)
		{
			if (((CurrCCKSwingVal&0xff) == (u32)CCKSwingTable_Ch14[i][0]) &&
				(((CurrCCKSwingVal&0xff00)>>8) == (u32)CCKSwingTable_Ch14[i][1]))
			{
				CCKSwingIndex = i;
//				RT_TRACE(COMP_INIT, DBG_LOUD,("Ch14, Current reg0x%x = 0x%lx, CCKSwingIndex=0x%x\n",
//					(rCCK0_TxFilter1+2), CurrCCKSwingVal, CCKSwingIndex));
				break;
			}
		}

		//Write 0xa22 0xa23
		TempVal = CCKSwingTable_Ch14[CCKSwingIndex][0] +
				(CCKSwingTable_Ch14[CCKSwingIndex][1]<<8) ;

		//Write 0xa24 ~ 0xa27
		TempVal2 = 0;
		TempVal2 = CCKSwingTable_Ch14[CCKSwingIndex][2] +
				(CCKSwingTable_Ch14[CCKSwingIndex][3]<<8) +
				(CCKSwingTable_Ch14[CCKSwingIndex][4]<<16 )+
				(CCKSwingTable_Ch14[CCKSwingIndex][5]<<24);

		//Write 0xa28  0xa29
		TempVal3 = 0;
		TempVal3 = CCKSwingTable_Ch14[CCKSwingIndex][6] +
				(CCKSwingTable_Ch14[CCKSwingIndex][7]<<8) ;
	}

	write_bbreg(Adapter, rCCK0_TxFilter1, bMaskHWord, TempVal);
	write_bbreg(Adapter, rCCK0_TxFilter2, bMaskDWord, TempVal2);
	write_bbreg(Adapter, rCCK0_DebugPort, bMaskLWord, TempVal3);
}

void Hal_MPT_CCKTxPowerAdjustbyIndex(PADAPTER pAdapter, BOOLEAN beven)
{
	s32		TempCCk;
	u8		CCK_index, CCK_index_old=0;
	u8		Action = 0;	//0: no action, 1: even->odd, 2:odd->even
	u8		TimeOut = 100;
	s32		i = 0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT	pMptCtx = &pAdapter->mppriv.MptCtx;


	if (!IS_92C_SERIAL(pHalData->VersionID))
		return;
#if 0
	while(PlatformAtomicExchange(&Adapter->IntrCCKRefCount, TRUE) == TRUE)
	{
		PlatformSleepUs(100);
		TimeOut--;
		if(TimeOut <= 0)
		{
			RTPRINT(FINIT, INIT_TxPower,
			 ("!!!MPT_CCKTxPowerAdjustbyIndex Wait for check CCK gain index too long!!!\n" ));
			break;
		}
	}
#endif
	if (beven && !pMptCtx->bMptIndexEven)	//odd->even
	{
		Action = 2;
		pMptCtx->bMptIndexEven = _TRUE;
	}
	else if (!beven && pMptCtx->bMptIndexEven)	//even->odd
	{
		Action = 1;
		pMptCtx->bMptIndexEven = _FALSE;
	}

	if (Action != 0)
	{
		//Query CCK default setting From 0xa24
		TempCCk = read_bbreg(pAdapter, rCCK0_TxFilter2, bMaskDWord) & bMaskCCK;
		for (i = 0; i < CCK_TABLE_SIZE; i++)
		{
			if (pHalData->dmpriv.bCCKinCH14)
			{
				if (_rtw_memcmp((void*)&TempCCk, (void*)&CCKSwingTable_Ch14[i][2], 4) == _TRUE)
				{
					CCK_index_old = (u8) i;
//					RTPRINT(FINIT, INIT_TxPower,("MPT_CCKTxPowerAdjustbyIndex: Initial reg0x%x = 0x%lx, CCK_index=0x%x, ch 14 %d\n",
//						rCCK0_TxFilter2, TempCCk, CCK_index_old, pHalData->bCCKinCH14));
					break;
				}
			}
			else
			{
				if (_rtw_memcmp((void*)&TempCCk, (void*)&CCKSwingTable_Ch1_Ch13[i][2], 4) == _TRUE)
				{
					CCK_index_old = (u8) i;
//					RTPRINT(FINIT, INIT_TxPower,("MPT_CCKTxPowerAdjustbyIndex: Initial reg0x%x = 0x%lx, CCK_index=0x%x, ch14 %d\n",
//						rCCK0_TxFilter2, TempCCk, CCK_index_old, pHalData->bCCKinCH14));
					break;
				}
			}
		}

		if (Action == 1) {
			if (CCK_index_old == 0)
				CCK_index_old = 1;
			CCK_index = CCK_index_old - 1;
		} else {
			CCK_index = CCK_index_old + 1;
		}

		if (CCK_index == CCK_TABLE_SIZE) {
			CCK_index = CCK_TABLE_SIZE -1;
			RT_TRACE(_module_mp_, _drv_info_, ("CCK_index == CCK_TABLE_SIZE\n"));
		}

//		RTPRINT(FINIT, INIT_TxPower,("MPT_CCKTxPowerAdjustbyIndex: new CCK_index=0x%x\n",
//			 CCK_index));

		//Adjust CCK according to gain index
		if (!pHalData->dmpriv.bCCKinCH14) {
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
#if 0
	RTPRINT(FINIT, INIT_TxPower,
	("MPT_CCKTxPowerAdjustbyIndex 0xa20=%x\n", PlatformEFIORead4Byte(Adapter, 0xa20)));

	PlatformAtomicExchange(&Adapter->IntrCCKRefCount, FALSE);
#endif
}
/*---------------------------hal\rtl8192c\MPT_HelperFunc.c---------------------------*/

/*
 * SetChannel
 * Description
 *	Use H2C command to change channel,
 *	not only modify rf register, but also other setting need to be done.
 */
void Hal_SetChannel(PADAPTER pAdapter)
{
#if 0
	struct mp_priv *pmp = &pAdapter->mppriv;

//	SelectChannel(pAdapter, pmp->channel);
	set_channel_bwmode(pAdapter, pmp->channel, pmp->channel_offset, pmp->bandwidth);
#else
	u8 		eRFPath;

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct mp_priv	*pmp = &pAdapter->mppriv;
	u8		channel = pmp->channel;
	u8		bandwidth = pmp->bandwidth;
	u8		rate = pmp->rateidx;


	// set RF channel register
	for (eRFPath = 0; eRFPath < pHalData->NumTotalRFPath; eRFPath++)
	{
      if(IS_HARDWARE_TYPE_8192D(pAdapter))
			_write_rfreg(pAdapter, (RF_PATH)eRFPath, rRfChannel, 0xFF, channel);
		else
		_write_rfreg(pAdapter, eRFPath, rRfChannel, 0x3FF, channel);
	}
	Hal_mpt_SwitchRfSetting(pAdapter);

	SelectChannel(pAdapter, channel);

	if (pHalData->CurrentChannel == 14 && !pHalData->dmpriv.bCCKinCH14) {
		pHalData->dmpriv.bCCKinCH14 = _TRUE;
		Hal_MPT_CCKTxPowerAdjust(pAdapter, pHalData->dmpriv.bCCKinCH14);
	}
	else if (pHalData->CurrentChannel != 14 && pHalData->dmpriv.bCCKinCH14) {
		pHalData->dmpriv.bCCKinCH14 = _FALSE;
		Hal_MPT_CCKTxPowerAdjust(pAdapter, pHalData->dmpriv.bCCKinCH14);
	}

#endif
}

/*
 * Notice
 *	Switch bandwitdth may change center frequency(channel)
 */
void Hal_SetBandwidth(PADAPTER pAdapter)
{
	struct mp_priv *pmp = &pAdapter->mppriv;


	SetBWMode(pAdapter, pmp->bandwidth, pmp->prime_channel_offset);
	Hal_mpt_SwitchRfSetting(pAdapter);
}

void Hal_SetCCKTxPower(PADAPTER pAdapter, u8 *TxPower)
{
	u32 tmpval = 0;


	// rf-A cck tx power
	write_bbreg(pAdapter, rTxAGC_A_CCK1_Mcs32, bMaskByte1, TxPower[RF_PATH_A]);
	tmpval = (TxPower[RF_PATH_A]<<16) | (TxPower[RF_PATH_A]<<8) | TxPower[RF_PATH_A];
	write_bbreg(pAdapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskH3Bytes, tmpval);

	// rf-B cck tx power
	write_bbreg(pAdapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte0, TxPower[RF_PATH_B]);
	tmpval = (TxPower[RF_PATH_B]<<16) | (TxPower[RF_PATH_B]<<8) | TxPower[RF_PATH_B];
	write_bbreg(pAdapter, rTxAGC_B_CCK1_55_Mcs32, bMaskH3Bytes, tmpval);

	RT_TRACE(_module_mp_, _drv_notice_,
		 ("-SetCCKTxPower: A[0x%02x] B[0x%02x]\n",
		  TxPower[RF_PATH_A], TxPower[RF_PATH_B]));
}

void Hal_SetOFDMTxPower(PADAPTER pAdapter, u8 *TxPower)
{
	u32 TxAGC = 0;
	u8 tmpval = 0;
	PMPT_CONTEXT	pMptCtx = &pAdapter->mppriv.MptCtx;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);


	// HT Tx-rf(A)
	tmpval = TxPower[RF_PATH_A];
	TxAGC = (tmpval<<24) | (tmpval<<16) | (tmpval<<8) | tmpval;

	write_bbreg(pAdapter, rTxAGC_A_Rate18_06, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_A_Rate54_24, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_A_Mcs03_Mcs00, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_A_Mcs07_Mcs04, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_A_Mcs11_Mcs08, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_A_Mcs15_Mcs12, bMaskDWord, TxAGC);

	// HT Tx-rf(B)
	tmpval = TxPower[RF_PATH_B];
	TxAGC = (tmpval<<24) | (tmpval<<16) | (tmpval<<8) | tmpval;

	write_bbreg(pAdapter, rTxAGC_B_Rate18_06, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_B_Rate54_24, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_B_Mcs03_Mcs00, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_B_Mcs07_Mcs04, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_B_Mcs11_Mcs08, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_B_Mcs15_Mcs12, bMaskDWord, TxAGC);

	RT_TRACE(_module_mp_, _drv_notice_,
		 ("-SetOFDMTxPower: A[0x%02x] B[0x%02x]\n",
		  TxPower[RF_PATH_A], TxPower[RF_PATH_B]));
}

void Hal_SetAntennaPathPower(PADAPTER pAdapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(pAdapter);
	u8 TxPowerLevel[MAX_RF_PATH_NUMS];
	u8 rfPath;

	TxPowerLevel[RF_PATH_A] = pAdapter->mppriv.txpoweridx;
	TxPowerLevel[RF_PATH_B] = pAdapter->mppriv.txpoweridx_b;

	switch (pAdapter->mppriv.antenna_tx)
	{
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

	switch (pHalData->rf_chip)
	{
		case RF_8225:
		case RF_8256:
		case RF_6052:
			Hal_SetCCKTxPower(pAdapter, TxPowerLevel);
			if (pAdapter->mppriv.rateidx < MPT_RATE_6M)	// CCK rate
				Hal_MPT_CCKTxPowerAdjustbyIndex(pAdapter, TxPowerLevel[rfPath]%2 == 0);
			Hal_SetOFDMTxPower(pAdapter, TxPowerLevel);
			break;

		default:
			break;
	}
}



void 
mpt_SetTxPower(
	IN	PADAPTER		pAdapter,
	IN	MPT_TXPWR_DEF	Rate,
	IN	pu1Byte 		pTxPower
	)
{
	if (IS_HARDWARE_TYPE_JAGUAR(pAdapter))
	{
		//mpt_SetTxPower_8812(pAdapter, Rate, pTxPower);
		return;
	}
	
	
	switch (Rate)
	{
		case MPT_CCK:
		{
			u4Byte	TxAGC = 0, pwr=0;
			u1Byte	rf;

			pwr = pTxPower[ODM_RF_PATH_A];
			TxAGC = (pwr<<16)|(pwr<<8)|(pwr);
			PHY_SetBBReg(pAdapter, rTxAGC_A_CCK1_Mcs32, bMaskByte1, pTxPower[ODM_RF_PATH_A]);
			PHY_SetBBReg(pAdapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskH3Bytes, TxAGC);

			pwr = pTxPower[ODM_RF_PATH_B];
			TxAGC = (pwr<<16)|(pwr<<8)|(pwr);
			PHY_SetBBReg(pAdapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte0, pTxPower[ODM_RF_PATH_B]);
			PHY_SetBBReg(pAdapter, rTxAGC_B_CCK1_55_Mcs32, bMaskH3Bytes, TxAGC);
			
		} break;
		
		case MPT_OFDM:
{
			u4Byte	TxAGC=0;
			u1Byte	pwr=0, rf;
			PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.MptCtx);		
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(pAdapter);

			pwr = pTxPower[0];
			TxAGC |= ((pwr<<24)|(pwr<<16)|(pwr<<8)|pwr);
			DBG_8192C("HT Tx-rf(A) Power = 0x%x\n", TxAGC);
			
			PHY_SetBBReg(pAdapter, rTxAGC_A_Rate18_06, bMaskDWord, TxAGC);
			PHY_SetBBReg(pAdapter, rTxAGC_A_Rate54_24, bMaskDWord, TxAGC);
			PHY_SetBBReg(pAdapter, rTxAGC_A_Mcs03_Mcs00, bMaskDWord, TxAGC);
			PHY_SetBBReg(pAdapter, rTxAGC_A_Mcs07_Mcs04, bMaskDWord, TxAGC);
			PHY_SetBBReg(pAdapter, rTxAGC_A_Mcs11_Mcs08, bMaskDWord, TxAGC);
			PHY_SetBBReg(pAdapter, rTxAGC_A_Mcs15_Mcs12, bMaskDWord, TxAGC);
			
			TxAGC=0;
			pwr = pTxPower[1];
			TxAGC |= ((pwr<<24)|(pwr<<16)|(pwr<<8)|pwr);
			DBG_8192C("HT Tx-rf(B) Power = 0x%x\n", TxAGC);
			
			PHY_SetBBReg(pAdapter, rTxAGC_B_Rate18_06, bMaskDWord, TxAGC);
			PHY_SetBBReg(pAdapter, rTxAGC_B_Rate54_24, bMaskDWord, TxAGC);
			PHY_SetBBReg(pAdapter, rTxAGC_B_Mcs03_Mcs00, bMaskDWord, TxAGC);
			PHY_SetBBReg(pAdapter, rTxAGC_B_Mcs07_Mcs04, bMaskDWord, TxAGC);
			PHY_SetBBReg(pAdapter, rTxAGC_B_Mcs11_Mcs08, bMaskDWord, TxAGC);
			PHY_SetBBReg(pAdapter, rTxAGC_B_Mcs15_Mcs12, bMaskDWord, TxAGC);
			
		} break;

		default:
			break;
			
	}
	
	}

void Hal_SetTxPower(PADAPTER pAdapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT		pMptCtx = &(pAdapter->mppriv.MptCtx);
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	u8 TxPowerLevel[MAX_RF_PATH];
	u1Byte				path;
	
	//#if DEV_BUS_TYPE == RT_USB_INTERFACE || DEV_BUS_TYPE == RT_SDIO_INTERFACE
		//RT_ASSERT((KeGetCurrentIrql() == PASSIVE_LEVEL), ("MPT_ProSetTxPower(): not in PASSIVE_LEVEL!\n"));
	//#endif
	
	path = (pHalData->AntennaTxPath == ANTENNA_A) ? (ODM_RF_PATH_A) : (ODM_RF_PATH_B);
		
		if (pHalData->rf_chip < RF_TYPE_MAX)
		{
			if (IS_HARDWARE_TYPE_JAGUAR(pAdapter))
	{

				DBG_8192C("===> MPT_ProSetTxPower: Jaguar\n");
				/*
					mpt_SetTxPower_8812(pAdapter, MPT_CCK, pMptCtx->TxPwrLevel);
					mpt_SetTxPower_8812(pAdapter, MPT_OFDM, pMptCtx->TxPwrLevel);
					mpt_SetTxPower_8812(pAdapter, MPT_VHT_OFDM, pMptCtx->TxPwrLevel);
					*/
			}
			else 
			{
				DBG_8192C("===> MPT_ProSetTxPower: Others\n");
				mpt_SetTxPower(pAdapter, MPT_CCK, pMptCtx->TxPwrLevel);
				Hal_MPT_CCKTxPowerAdjustbyIndex(pAdapter, pMptCtx->TxPwrLevel[path]%2 == 0);		
				mpt_SetTxPower(pAdapter, MPT_OFDM, pMptCtx->TxPwrLevel);
			}
		}
		else
		{
		   DBG_8192C("RFChipID < RF_TYPE_MAX, the RF chip is not supported - %d\n", pHalData->rf_chip);
	}

		ODM_ClearTxPowerTrackingState(pDM_Odm);

}

void Hal_SetTxAGCOffset(PADAPTER pAdapter, u32 ulTxAGCOffset)
{
#if 0
	u32 TxAGCOffset_B, TxAGCOffset_C, TxAGCOffset_D,tmpAGC;


	TxAGCOffset_B = (ulTxAGCOffset&0x000000ff);
	TxAGCOffset_C = ((ulTxAGCOffset&0x0000ff00)>>8);
	TxAGCOffset_D = ((ulTxAGCOffset&0x00ff0000)>>16);

	tmpAGC = (TxAGCOffset_D<<8 | TxAGCOffset_C<<4 | TxAGCOffset_B);
	write_bbreg(pAdapter, rFPGA0_TxGainStage,
			(bXBTxAGC|bXCTxAGC|bXDTxAGC), tmpAGC);
#endif
}

void Hal_SetDataRate(PADAPTER pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT		pMptCtx = &(pAdapter->mppriv.MptCtx);
	u32 DataRate;
	
	DataRate=MptToMgntRate(pAdapter->mppriv.rateidx);
	
		if(!IS_HARDWARE_TYPE_8723A(pAdapter))
	        Hal_mpt_SwitchRfSetting(pAdapter);
		if (IS_CCK_RATE(DataRate))
		{
			if (pMptCtx->MptRfPath == ODM_RF_PATH_A) // S1
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x51, 0xF, 0x6);	
			else // S0
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x71, 0xF, 0x6);
		}
		else
		{
			if (pMptCtx->MptRfPath == ODM_RF_PATH_A) // S1
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x51, 0xF, 0xE);	
			else // S0
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x71, 0xF, 0xE);		
		}

	// <20130913, Kordan> 8723BS TFBGA uses the default setting.
	if ((IS_HARDWARE_TYPE_8723BS(pAdapter) && 
		  ((pHalData->PackageType == PACKAGE_TFBGA79) || (pHalData->PackageType == PACKAGE_TFBGA90))))
	{
		if (pMptCtx->MptRfPath == ODM_RF_PATH_A) // S1
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x51, 0xF, 0xE);	
		else // S0
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x71, 0xF, 0xE);			
	}
}

#define RF_PATH_AB	22

void Hal_SetAntenna(PADAPTER pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u4Byte					ulAntennaTx, ulAntennaRx;
	PMPT_CONTEXT		pMptCtx = &(pAdapter->mppriv.MptCtx);
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	PODM_RF_CAL_T			pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);

	ulAntennaTx = pHalData->AntennaTxPath;
	ulAntennaRx = pHalData->AntennaRxPath;

	if (pHalData->rf_chip>= RF_TYPE_MAX)
	{
		DBG_8192C("This RF chip ID is not supported\n");
		return ;
	}

	switch (pAdapter->mppriv.antenna_tx)
	{
		u1Byte p = 0, i = 0;

	    case ANTENNA_A: // Actually path S1  (Wi-Fi)
			{
				pMptCtx->MptRfPath = ODM_RF_PATH_A;			
				PHY_SetBBReg(pAdapter, rS0S1_PathSwitch, BIT9|BIT8|BIT7, 0x0);
				PHY_SetBBReg(pAdapter, 0xB2C, BIT31, 0x0); // AGC Table Sel

				//<20130522, Kordan> 0x51 and 0x71 should be set immediately after path switched, or they might be overwritten.
				if ((pHalData->PackageType == PACKAGE_TFBGA79) || (pHalData->PackageType == PACKAGE_TFBGA90))
					PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x51, bRFRegOffsetMask, 0x6B10E);
				else
					PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x51, bRFRegOffsetMask, 0x6B04E);


				for (i = 0; i < 3; ++i)
				{
					u4Byte offset = pRFCalibrateInfo->TxIQC_8723B[ODM_RF_PATH_A][i][0];
					 u4Byte data = pRFCalibrateInfo->TxIQC_8723B[ODM_RF_PATH_A][i][1];
					if (offset != 0) {
						PHY_SetBBReg(pAdapter, offset, bMaskDWord, data);
						DBG_8192C("Switch to S1 TxIQC(offset, data) = (0x%X, 0x%X)\n", offset, data);
					}

				}
				 for (i = 0; i < 2; ++i)
				{
					u4Byte offset = pRFCalibrateInfo->RxIQC_8723B[ODM_RF_PATH_A][i][0];
					u4Byte data = pRFCalibrateInfo->RxIQC_8723B[ODM_RF_PATH_A][i][1];
					if (offset != 0) {
						PHY_SetBBReg(pAdapter, offset, bMaskDWord, data);					
						DBG_8192C("Switch to S1 RxIQC (offset, data) = (0x%X, 0x%X)\n", offset, data);
					}
				}
			}
		break;
		case ANTENNA_B: // Actually path S0 (BT)
			{
				pMptCtx->MptRfPath = ODM_RF_PATH_B;
				PHY_SetBBReg(pAdapter, rS0S1_PathSwitch, BIT9|BIT8|BIT7, 0x5);
				PHY_SetBBReg(pAdapter, 0xB2C, BIT31, 0x1); // AGC Table Sel
				
				//<20130522, Kordan> 0x51 and 0x71 should be set immediately after path switched, or they might be overwritten.
				if ((pHalData->PackageType == PACKAGE_TFBGA79) || (pHalData->PackageType == PACKAGE_TFBGA90))
						PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x51, bRFRegOffsetMask, 0x6B10E);
				else
						PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x51, bRFRegOffsetMask, 0x6B04E);


				for (i = 0; i < 3; ++i)
				{
					 // <20130603, Kordan> Because BB suppors only 1T1R, we restore IQC  to S1 instead of S0.
					 u4Byte offset = pRFCalibrateInfo->TxIQC_8723B[ODM_RF_PATH_A][i][0];
					 u4Byte data = pRFCalibrateInfo->TxIQC_8723B[ODM_RF_PATH_B][i][1];
					if (pRFCalibrateInfo->TxIQC_8723B[ODM_RF_PATH_B][i][0] != 0) {
					 PHY_SetBBReg(pAdapter, offset, bMaskDWord, data);
					 DBG_8192C("Switch to S0 TxIQC (offset, data) = (0x%X, 0x%X)\n", offset, data);
					}
				}
				for (i = 0; i < 2; ++i)
				{
					 // <20130603, Kordan> Because BB suppors only 1T1R, we restore IQC to S1 instead of S0.
					 u4Byte offset = pRFCalibrateInfo->RxIQC_8723B[ODM_RF_PATH_A][i][0];
					 u4Byte data = pRFCalibrateInfo->RxIQC_8723B[ODM_RF_PATH_B][i][1];
					if (pRFCalibrateInfo->RxIQC_8723B[ODM_RF_PATH_B][i][0] != 0) {
						PHY_SetBBReg(pAdapter, offset, bMaskDWord, data);
						DBG_8192C("Switch to S0 RxIQC (offset, data) = (0x%X, 0x%X)\n", offset, data);
					}
				}

			}
			break;
		default:
			pMptCtx->MptRfPath = RF_PATH_AB; 
           RT_TRACE(_module_mp_, _drv_notice_, ("Unknown Tx antenna.\n"));
			break;
	}

	RT_TRACE(_module_mp_, _drv_notice_, ("-SwitchAntenna: finished\n"));
}

s32 Hal_SetThermalMeter(PADAPTER pAdapter, u8 target_ther)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(pAdapter);


	if (!netif_running(pAdapter->pnetdev)) {
		RT_TRACE(_module_mp_, _drv_warning_, ("SetThermalMeter! Fail: interface not opened!\n"));
		return _FAIL;
	}

	if (check_fwstate(&pAdapter->mlmepriv, WIFI_MP_STATE) == _FALSE) {
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

void Hal_TriggerRFThermalMeter(PADAPTER pAdapter)
{
	PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_T_METER_8723B, BIT17 | BIT16, 0x03);
//	RT_TRACE(_module_mp_,_drv_alert_, ("TriggerRFThermalMeter() finished.\n" ));
}

u8 Hal_ReadRFThermalMeter(PADAPTER pAdapter)
{
	u32 ThermalValue = 0;

	ThermalValue = (u1Byte)PHY_QueryRFReg(pAdapter, ODM_RF_PATH_A, RF_T_METER_8723B, 0xfc00);	// 0x42: RF Reg[15:10]					

	return (u8)ThermalValue;
}

void Hal_GetThermalMeter(PADAPTER pAdapter, u8 *value)
{
#if 0
	fw_cmd(pAdapter, IOCMD_GET_THERMAL_METER);
	rtw_msleep_os(1000);
	fw_cmd_data(pAdapter, value, 1);
	*value &= 0xFF;
#else

	Hal_TriggerRFThermalMeter(pAdapter);
	rtw_msleep_os(1000);
	*value = Hal_ReadRFThermalMeter(pAdapter);
#endif
}

void Hal_SetSingleCarrierTx(PADAPTER pAdapter, u8 bStart)
{
    HAL_DATA_TYPE *pHalData = GET_HAL_DATA(pAdapter);
	pAdapter->mppriv.MptCtx.bSingleCarrier = bStart;
	if (bStart)// Start Single Carrier.
	{
		RT_TRACE(_module_mp_,_drv_alert_, ("SetSingleCarrierTx: test start\n"));
		// Start Single Carrier.
		// 1. if OFDM block on?
		if(!PHY_QueryBBReg(pAdapter, rFPGA0_RFMOD, bOFDMEn))
			PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, bOFDMEn, 1);//set OFDM block on

		// 2. set CCK test mode off, set to CCK normal mode
		PHY_SetBBReg(pAdapter, rCCK0_System, bCCKBBMode, 0);

		// 3. turn on scramble setting
		PHY_SetBBReg(pAdapter, rCCK0_System, bCCKScramble, 1);

		// 4. Turn On Continue Tx and turn off the other test modes.
		//if (IS_HARDWARE_TYPE_JAGUAR(pAdapter))
			//PHY_SetBBReg(pAdapter, rSingleTone_ContTx_Jaguar, BIT18|BIT17|BIT16, OFDM_SingleCarrier);
		//else
		PHY_SetBBReg(pAdapter, rOFDM1_LSTF, BIT30|BIT29|BIT28, OFDM_SingleCarrier);
	}
	else// Stop Single Carrier.
	{
		// Stop Single Carrier.
		// Turn off all test modes.
		//if (IS_HARDWARE_TYPE_JAGUAR(pAdapter))
		//	PHY_SetBBReg(pAdapter, rSingleTone_ContTx_Jaguar, BIT18|BIT17|BIT16, OFDM_ALL_OFF);
		//else
		PHY_SetBBReg(pAdapter, rOFDM1_LSTF, BIT30|BIT29|BIT28, OFDM_ALL_OFF);
	    //Delay 10 ms
		rtw_msleep_os(10);
		//BB Reset
	    PHY_SetBBReg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
	    PHY_SetBBReg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);
	}
}


void Hal_SetSingleToneTx(PADAPTER pAdapter, u8 bStart)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT		pMptCtx = &(pAdapter->mppriv.MptCtx);
	BOOLEAN		is92C = IS_92C_SERIAL(pHalData->VersionID);
	static u4Byte       reg58 = 0x0;
	static u4Byte       regRF0x0 = 0x0;
    static u4Byte       reg0xCB0 = 0x0;
    static u4Byte       reg0xEB0 = 0x0;
    static u4Byte       reg0xCB4 = 0x0;
    static u4Byte       reg0xEB4 = 0x0;
	u8 rfPath;

	switch (pAdapter->mppriv.antenna_tx)
	{
		case ANTENNA_A:
		default:
			pMptCtx->MptRfPath = rfPath = RF_PATH_A;
			break;
		case ANTENNA_B:
			pMptCtx->MptRfPath = rfPath = RF_PATH_B;
			break;
		case ANTENNA_C:
			pMptCtx->MptRfPath = rfPath = RF_PATH_C;
			break;
	}

	pAdapter->mppriv.MptCtx.bSingleTone = bStart;
	if (bStart)// Start Single Tone.
	{

		// <20120326, Kordan> To amplify the power of tone for Xtal calibration. (asked by Edlu)
		if (IS_HARDWARE_TYPE_8188E(pAdapter))
       		 {
			reg58 = PHY_QueryRFReg(pAdapter, rfPath, LNA_Low_Gain_3, bRFRegOffsetMask);
			if (rfPath == ODM_RF_PATH_A) 
				pMptCtx->backup0x58_RF_A = reg58; 
			else
				pMptCtx->backup0x58_RF_B = reg58;
			
			PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, LNA_Low_Gain_3, BIT1, 0x1); // RF LO enabled		
			PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, bCCKEn, 0x0);
			PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, bOFDMEn, 0x0);
		
		
		}
		else if (IS_HARDWARE_TYPE_8192E(pAdapter))
		{ // USB need to do RF LO disable first, PCIE isn't required to follow this order.
			PHY_SetRFReg(pAdapter, pMptCtx->MptRfPath, LNA_Low_Gain_3, BIT1, 0x1); // RF LO disabled
			PHY_SetRFReg(pAdapter, pMptCtx->MptRfPath, RF_AC, 0xF0000, 0x2); // Tx mode
		}			
		else if (IS_HARDWARE_TYPE_8723B(pAdapter))
		{
			if (pMptCtx->MptRfPath == ODM_RF_PATH_A) {
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_AC, 0xF0000, 0x2); // Tx mode
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x56, 0xF, 0x1); // RF LO enabled
			} else { 
				// S0/S1 both use PATH A to configure
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_AC, 0xF0000, 0x2); // Tx mode
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x76, 0xF, 0x1); // RF LO enabled
			}
		}
		else if (IS_HARDWARE_TYPE_JAGUAR(pAdapter)) 
		{
			/*
			u1Byte p = ODM_RF_PATH_A;
		
			regRF0x0 = PHY_QueryRFReg(pAdapter, ODM_RF_PATH_A, RF_AC_Jaguar, bRFRegOffsetMask);
			reg0xCB0 = PHY_QueryBBReg(pAdapter, rA_RFE_Pinmux_Jaguar, bMaskDWord);
			reg0xEB0 = PHY_QueryBBReg(pAdapter, rB_RFE_Pinmux_Jaguar, bMaskDWord);
			reg0xCB4 = PHY_QueryBBReg(pAdapter, rA_RFE_Pinmux_Jaguar+4, bMaskDWord);
			reg0xEB4 = PHY_QueryBBReg(pAdapter, rB_RFE_Pinmux_Jaguar+4, bMaskDWord);
			
			PHY_SetBBReg(pAdapter, rOFDMCCKEN_Jaguar, BIT29|BIT28, 0x0); // Disable CCK and OFDM
		
			if (pMptCtx->MptRfPath == RF_PATH_AB) {
				for (p = ODM_RF_PATH_A; p <= ODM_RF_PATH_B; ++p) {					
					PHY_SetRFReg(pAdapter, p, RF_AC_Jaguar, 0xF0000, 0x2); // Tx mode: RF0x00[19:16]=4'b0010 
					PHY_SetRFReg(pAdapter, p, RF_AC_Jaguar, 0x1F, 0x0); // Lowest RF gain index: RF_0x0[4:0] = 0
					PHY_SetRFReg(pAdapter, p, LNA_Low_Gain_3, BIT1, 0x1); // RF LO enabled
				}
			} else {
				PHY_SetRFReg(pAdapter, pMptCtx->MptRfPath, RF_AC_Jaguar, 0xF0000, 0x2); // Tx mode: RF0x00[19:16]=4'b0010 
				PHY_SetRFReg(pAdapter, pMptCtx->MptRfPath, RF_AC_Jaguar, 0x1F, 0x0); // Lowest RF gain index: RF_0x0[4:0] = 0
				PHY_SetRFReg(pAdapter, pMptCtx->MptRfPath, LNA_Low_Gain_3, BIT1, 0x1); // RF LO enabled
			}
			
		
			PHY_SetBBReg(pAdapter, rA_RFE_Pinmux_Jaguar, 0xFF00F0, 0x77007);  // 0xCB0[[23:16, 7:4] = 0x77007
			PHY_SetBBReg(pAdapter, rB_RFE_Pinmux_Jaguar, 0xFF00F0, 0x77007);  // 0xCB0[[23:16, 7:4] = 0x77007
		
			if (pHalData->ExternalPA_5G) {
		
				PHY_SetBBReg(pAdapter, rA_RFE_Pinmux_Jaguar+4, 0xFF00000, 0x12); // 0xCB4[23:16] = 0x12
				PHY_SetBBReg(pAdapter, rB_RFE_Pinmux_Jaguar+4, 0xFF00000, 0x12); // 0xEB4[23:16] = 0x12
			} else if (pHalData->ExternalPA_2G) {
				PHY_SetBBReg(pAdapter, rA_RFE_Pinmux_Jaguar+4, 0xFF00000, 0x11); // 0xCB4[23:16] = 0x11
				PHY_SetBBReg(pAdapter, rB_RFE_Pinmux_Jaguar+4, 0xFF00000, 0x11); // 0xEB4[23:16] = 0x11
			}
			*/
		}
		else
		{
			// Turn On SingleTone and turn off the other test modes.
			PHY_SetBBReg(pAdapter, rOFDM1_LSTF, BIT30|BIT29|BIT28, OFDM_SingleTone);			
		}


		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000500);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000500);

	}
	else// Stop Single Tone.
	{
	// Stop Single Tone.
		if (IS_HARDWARE_TYPE_8188E(pAdapter))
		{
            PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, LNA_Low_Gain_3, bRFRegOffsetMask, pMptCtx->backup0x58_RF_A);
                	
    		PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, bCCKEn, 0x1);
    		PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, bOFDMEn, 0x1);
		}		
		else if (IS_HARDWARE_TYPE_8192E(pAdapter))
		{
			PHY_SetRFReg(pAdapter, pMptCtx->MptRfPath, RF_AC, 0xF0000, 0x3); // Tx mode
			PHY_SetRFReg(pAdapter, pMptCtx->MptRfPath, LNA_Low_Gain_3, BIT1, 0x0); // RF LO disabled    	
		}						
		else if (IS_HARDWARE_TYPE_8723B(pAdapter))
		{
			if (pMptCtx->MptRfPath == ODM_RF_PATH_A) {
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_AC, 0xF0000, 0x3); // Rx mode
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x56, 0xF, 0x0); // RF LO disabled
			} else {
				// S0/S1 both use PATH A to configure
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, RF_AC, 0xF0000, 0x3); // Rx mode
				PHY_SetRFReg(pAdapter, ODM_RF_PATH_A, 0x76, 0xF, 0x0); // RF LO disabled
			}    	
		}		
        else if (IS_HARDWARE_TYPE_JAGUAR(pAdapter)) 
		{
			/*
			u1Byte p = ODM_RF_PATH_A;

			PHY_SetBBReg(pAdapter, rOFDMCCKEN_Jaguar, BIT29|BIT28, 0x3); // Disable CCK and OFDM

			if (pMptCtx->MptRfPath == RF_PATH_AB) {
				for (p = ODM_RF_PATH_A; p <= ODM_RF_PATH_B; ++p) {					
					PHY_SetRFReg(pAdapter, p, RF_AC_Jaguar, bRFRegOffsetMask, regRF0x0);
					PHY_SetRFReg(pAdapter, p, LNA_Low_Gain_3, BIT1, 0x0); // RF LO disabled
				}
		} else {
				PHY_SetRFReg(pAdapter, p, RF_AC_Jaguar, bRFRegOffsetMask, regRF0x0);
				PHY_SetRFReg(pAdapter, p, LNA_Low_Gain_3, BIT1, 0x0); // RF LO disabled
			}
			PHY_SetBBReg(pAdapter, rA_RFE_Pinmux_Jaguar, bMaskDWord, reg0xCB0); 
			PHY_SetBBReg(pAdapter, rB_RFE_Pinmux_Jaguar, bMaskDWord, reg0xEB0); 
			PHY_SetBBReg(pAdapter, rA_RFE_Pinmux_Jaguar+4, bMaskDWord, reg0xCB4);
			PHY_SetBBReg(pAdapter, rB_RFE_Pinmux_Jaguar+4, bMaskDWord, reg0xEB4); 
			*/
	    }	
		else
		{
			// Turn off all test modes.	
			/*
			PHY_SetBBReg(pAdapter, rSingleTone_ContTx_Jaguar, BIT18|BIT17|BIT16, OFDM_ALL_OFF);
			*/
		}

		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000100);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000100);

	}

}


void Hal_SetCarrierSuppressionTx(PADAPTER pAdapter, u8 bStart)
{
	pAdapter->mppriv.MptCtx.bCarrierSuppression = bStart;
	if (bStart) // Start Carrier Suppression.
	{
		RT_TRACE(_module_mp_,_drv_alert_, ("SetCarrierSuppressionTx: test start\n"));
		//if(pMgntInfo->dot11CurrentWirelessMode == WIRELESS_MODE_B)
		if (pAdapter->mppriv.rateidx <= MPT_RATE_11M)
		  {
			// 1. if CCK block on?
			if(!read_bbreg(pAdapter, rFPGA0_RFMOD, bCCKEn))
				write_bbreg(pAdapter, rFPGA0_RFMOD, bCCKEn, bEnable);//set CCK block on

			//Turn Off All Test Mode
			write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bDisable);
			write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bDisable);
			write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);

			write_bbreg(pAdapter, rCCK0_System, bCCKBBMode, 0x2);    //transmit mode
			write_bbreg(pAdapter, rCCK0_System, bCCKScramble, 0x0);  //turn off scramble setting

			//Set CCK Tx Test Rate
			//PHY_SetBBReg(pAdapter, rCCK0_System, bCCKTxRate, pMgntInfo->ForcedDataRate);
			write_bbreg(pAdapter, rCCK0_System, bCCKTxRate, 0x0);    //Set FTxRate to 1Mbps
		}

		 //Set for dynamic set Power index
		 write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000500);
		 write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000500);

	}
	else// Stop Carrier Suppression.
	{
		RT_TRACE(_module_mp_,_drv_alert_, ("SetCarrierSuppressionTx: test stop\n"));
		//if(pMgntInfo->dot11CurrentWirelessMode == WIRELESS_MODE_B)
		if (pAdapter->mppriv.rateidx <= MPT_RATE_11M ) {
			write_bbreg(pAdapter, rCCK0_System, bCCKBBMode, 0x0);    //normal mode
			write_bbreg(pAdapter, rCCK0_System, bCCKScramble, 0x1);  //turn on scramble setting

			//BB Reset
			write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
			write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);
		}
		//Stop for dynamic set Power index
		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000100);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000100);
	}
	//DbgPrint("\n MPT_ProSetCarrierSupp() is finished. \n");
}

void Hal_SetCCKContinuousTx(PADAPTER pAdapter, u8 bStart)
{
	u32 cckrate;

	if (bStart)
	{
		RT_TRACE(_module_mp_, _drv_alert_,
			 ("SetCCKContinuousTx: test start\n"));

		// 1. if CCK block on?
		if(!read_bbreg(pAdapter, rFPGA0_RFMOD, bCCKEn))
			write_bbreg(pAdapter, rFPGA0_RFMOD, bCCKEn, bEnable);//set CCK block on

		//Turn Off All Test Mode
		//if (IS_HARDWARE_TYPE_JAGUAR(pAdapter))
			//PHY_SetBBReg(pAdapter, rSingleTone_ContTx_Jaguar, BIT18|BIT17|BIT16, OFDM_ALL_OFF);
		//else
			PHY_SetBBReg(pAdapter, rOFDM1_LSTF, BIT30|BIT29|BIT28, OFDM_ALL_OFF);
		//Set CCK Tx Test Rate
		#if 0
		switch(pAdapter->mppriv.rateidx)
		{
			case 2:
				cckrate = 0;
				break;
			case 4:
				cckrate = 1;
				break;
			case 11:
				cckrate = 2;
				break;
			case 22:
				cckrate = 3;
				break;
			default:
				cckrate = 0;
				break;
		}
		#else
		cckrate  = pAdapter->mppriv.rateidx;
		#endif
		write_bbreg(pAdapter, rCCK0_System, bCCKTxRate, cckrate);
		write_bbreg(pAdapter, rCCK0_System, bCCKBBMode, 0x2);	//transmit mode
		write_bbreg(pAdapter, rCCK0_System, bCCKScramble, bEnable);	//turn on scramble setting

		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000500);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000500);
#ifdef CONFIG_RTL8192C
		// Patch for CCK 11M waveform
		if (cckrate == MPT_RATE_1M)
			write_bbreg(pAdapter, 0xA71, BIT(6), bDisable);
		else
			write_bbreg(pAdapter, 0xA71, BIT(6), bEnable);
#endif

	}
	else {
		RT_TRACE(_module_mp_, _drv_info_,
			 ("SetCCKContinuousTx: test stop\n"));

		write_bbreg(pAdapter, rCCK0_System, bCCKBBMode, 0x0);	//normal mode
		write_bbreg(pAdapter, rCCK0_System, bCCKScramble, bEnable);	//turn on scramble setting

		//BB Reset
		write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
		write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);

		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000100);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000100);
	}

	pAdapter->mppriv.MptCtx.bCckContTx = bStart;
	pAdapter->mppriv.MptCtx.bOfdmContTx = _FALSE;
}/* mpt_StartCckContTx */

void Hal_SetOFDMContinuousTx(PADAPTER pAdapter, u8 bStart)
{
    HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	if (bStart) {
		RT_TRACE(_module_mp_, _drv_info_, ("SetOFDMContinuousTx: test start\n"));
		// 1. if OFDM block on?
		if(!read_bbreg(pAdapter, rFPGA0_RFMOD, bOFDMEn))
			write_bbreg(pAdapter, rFPGA0_RFMOD, bOFDMEn, bEnable);//set OFDM block on
        {

		// 2. set CCK test mode off, set to CCK normal mode
		write_bbreg(pAdapter, rCCK0_System, bCCKBBMode, bDisable);

		// 3. turn on scramble setting
		write_bbreg(pAdapter, rCCK0_System, bCCKScramble, bEnable);
        }
		// 4. Turn On Continue Tx and turn off the other test modes.
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bEnable);
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bDisable);
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);

		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000500);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000500);

	} else {
		RT_TRACE(_module_mp_,_drv_info_, ("SetOFDMContinuousTx: test stop\n"));
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bDisable);
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bDisable);
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);
		//Delay 10 ms
		rtw_msleep_os(10);
		//BB Reset
		write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
		write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);

		write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter1, bMaskDWord, 0x01000100);
		write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter1, bMaskDWord, 0x01000100);
	}

	pAdapter->mppriv.MptCtx.bCckContTx = _FALSE;
	pAdapter->mppriv.MptCtx.bOfdmContTx = bStart;
}/* mpt_StartOfdmContTx */

void Hal_SetContinuousTx(PADAPTER pAdapter, u8 bStart)
{
#if 0
	// ADC turn off [bit24-21] adc port0 ~ port1
	if (bStart) {
		write_bbreg(pAdapter, rRx_Wait_CCCA, read_bbreg(pAdapter, rRx_Wait_CCCA) & 0xFE1FFFFF);
		rtw_usleep_os(100);
	}
#endif
	RT_TRACE(_module_mp_, _drv_info_,
		 ("SetContinuousTx: rate:%d\n", pAdapter->mppriv.rateidx));

	pAdapter->mppriv.MptCtx.bStartContTx = bStart;

	//write_bbreg(pAdapter, rFixContTxRate, bFixContTxRate, bStart);

	if (pAdapter->mppriv.rateidx <= MPT_RATE_11M)
	{
		Hal_SetCCKContinuousTx(pAdapter, bStart);
	}
	else if ((pAdapter->mppriv.rateidx >= MPT_RATE_6M) &&
		 (pAdapter->mppriv.rateidx <= MPT_RATE_MCS15))
	{
		Hal_SetOFDMContinuousTx(pAdapter, bStart);
	}
#if 0
	// ADC turn on [bit24-21] adc port0 ~ port1
	if (!bStart) {
		write_bbreg(pAdapter, rRx_Wait_CCCA, read_bbreg(pAdapter, rRx_Wait_CCCA) | 0x01E00000);
	}
#endif
}



u8 MRateToHwRate8723B( u8 rate)
{
	u8	ret = DESC8723B_RATE1M;
		
	switch(rate)
	{
		// CCK and OFDM non-HT rates
	case MPT_RATE_1M:		ret = DESC8723B_RATE1M; break;
	case MPT_RATE_2M:		ret = DESC8723B_RATE2M; break;
	case MPT_RATE_55M:		ret = DESC8723B_RATE5_5M;	break;
	case MPT_RATE_11M:		ret = DESC8723B_RATE11M;	break;
	case MPT_RATE_6M:		ret = DESC8723B_RATE6M; break;
	case MPT_RATE_9M:		ret = DESC8723B_RATE9M; break;
	case MPT_RATE_12M:		ret = DESC8723B_RATE12M;	break;
	case MPT_RATE_18M:		ret = DESC8723B_RATE18M;	break;
	case MPT_RATE_24M:		ret = DESC8723B_RATE24M;	break;
	case MPT_RATE_36M:		ret = DESC8723B_RATE36M;	break;
	case MPT_RATE_48M:		ret = DESC8723B_RATE48M;	break;
	case MPT_RATE_54M:		ret = DESC8723B_RATE54M;	break;

		// HT rates since here
	case MPT_RATE_MCS0:		ret = DESC8723B_RATEMCS0;	break;
	case MPT_RATE_MCS1:		ret = DESC8723B_RATEMCS1;	break;
	case MPT_RATE_MCS2:		ret = DESC8723B_RATEMCS2;	break;
	case MPT_RATE_MCS3:		ret = DESC8723B_RATEMCS3;	break;
	case MPT_RATE_MCS4:		ret = DESC8723B_RATEMCS4;	break;
	case MPT_RATE_MCS5:		ret = DESC8723B_RATEMCS5;	break;
	case MPT_RATE_MCS6:		ret = DESC8723B_RATEMCS6;	break;
	case MPT_RATE_MCS7:		ret = DESC8723B_RATEMCS7;	break;
	case MPT_RATE_MCS8:		ret = DESC8723B_RATEMCS8;	break;
	case MPT_RATE_MCS9:		ret = DESC8723B_RATEMCS9;	break;
	case MPT_RATE_MCS10: ret = DESC8723B_RATEMCS10;	break;
	case MPT_RATE_MCS11: ret = DESC8723B_RATEMCS11;	break;
	case MPT_RATE_MCS12: ret = DESC8723B_RATEMCS12;	break;
	case MPT_RATE_MCS13: ret = DESC8723B_RATEMCS13;	break;
	case MPT_RATE_MCS14: ret = DESC8723B_RATEMCS14;	break;
	case MPT_RATE_MCS15: ret = DESC8723B_RATEMCS15;	break;
	case MPT_RATE_VHT1SS_MCS0:		ret = DESC8723B_RATEVHTSS1MCS0; break;
	case MPT_RATE_VHT1SS_MCS1:		ret = DESC8723B_RATEVHTSS1MCS1; break;
	case MPT_RATE_VHT1SS_MCS2:		ret = DESC8723B_RATEVHTSS1MCS2; break;
	case MPT_RATE_VHT1SS_MCS3:		ret = DESC8723B_RATEVHTSS1MCS3; break;
	case MPT_RATE_VHT1SS_MCS4:		ret = DESC8723B_RATEVHTSS1MCS4; break;
	case MPT_RATE_VHT1SS_MCS5:		ret = DESC8723B_RATEVHTSS1MCS5; break;
	case MPT_RATE_VHT1SS_MCS6:		ret = DESC8723B_RATEVHTSS1MCS6; break;
	case MPT_RATE_VHT1SS_MCS7:		ret = DESC8723B_RATEVHTSS1MCS7; break;
	case MPT_RATE_VHT1SS_MCS8:		ret = DESC8723B_RATEVHTSS1MCS8; break;
	case MPT_RATE_VHT1SS_MCS9:		ret = DESC8723B_RATEVHTSS1MCS9; break;	
	case MPT_RATE_VHT2SS_MCS0:		ret = DESC8723B_RATEVHTSS2MCS0; break;
	case MPT_RATE_VHT2SS_MCS1:		ret = DESC8723B_RATEVHTSS2MCS1; break;
	case MPT_RATE_VHT2SS_MCS2:		ret = DESC8723B_RATEVHTSS2MCS2; break;
	case MPT_RATE_VHT2SS_MCS3:		ret = DESC8723B_RATEVHTSS2MCS3; break;
	case MPT_RATE_VHT2SS_MCS4:		ret = DESC8723B_RATEVHTSS2MCS4; break;
	case MPT_RATE_VHT2SS_MCS5:		ret = DESC8723B_RATEVHTSS2MCS5; break;
	case MPT_RATE_VHT2SS_MCS6:		ret = DESC8723B_RATEVHTSS2MCS6; break;
	case MPT_RATE_VHT2SS_MCS7:		ret = DESC8723B_RATEVHTSS2MCS7; break;
	case MPT_RATE_VHT2SS_MCS8:		ret = DESC8723B_RATEVHTSS2MCS8; break;
	case MPT_RATE_VHT2SS_MCS9:		ret = DESC8723B_RATEVHTSS2MCS9; break;	
	default:		break;
	}

	return ret;
}


u8 HwRateToMRate8723B(u8	 rate)
{

	u8	ret_rate = MGN_1M;


	switch(rate)
	{
	
		case DESC8723B_RATE1M:		ret_rate = MGN_1M;		break;
		case DESC8723B_RATE2M:		ret_rate = MGN_2M;		break;
		case DESC8723B_RATE5_5M:		ret_rate = MGN_5_5M;		break;
		case DESC8723B_RATE11M: 	ret_rate = MGN_11M; 	break;
		case DESC8723B_RATE6M:		ret_rate = MGN_6M;		break;
		case DESC8723B_RATE9M:		ret_rate = MGN_9M;		break;
		case DESC8723B_RATE12M: 	ret_rate = MGN_12M; 	break;
		case DESC8723B_RATE18M: 	ret_rate = MGN_18M; 	break;
		case DESC8723B_RATE24M: 	ret_rate = MGN_24M; 	break;
		case DESC8723B_RATE36M: 	ret_rate = MGN_36M; 	break;
		case DESC8723B_RATE48M: 	ret_rate = MGN_48M; 	break;
		case DESC8723B_RATE54M: 	ret_rate = MGN_54M; 	break;			
		case DESC8723B_RATEMCS0:	ret_rate = MGN_MCS0;		break;
		case DESC8723B_RATEMCS1:	ret_rate = MGN_MCS1;		break;
		case DESC8723B_RATEMCS2:	ret_rate = MGN_MCS2;		break;
		case DESC8723B_RATEMCS3:	ret_rate = MGN_MCS3;		break;
		case DESC8723B_RATEMCS4:	ret_rate = MGN_MCS4;		break;
		case DESC8723B_RATEMCS5:	ret_rate = MGN_MCS5;		break;
		case DESC8723B_RATEMCS6:	ret_rate = MGN_MCS6;		break;
		case DESC8723B_RATEMCS7:	ret_rate = MGN_MCS7;		break;
		case DESC8723B_RATEMCS8:	ret_rate = MGN_MCS8;		break;
		case DESC8723B_RATEMCS9:	ret_rate = MGN_MCS9;		break;
		case DESC8723B_RATEMCS10:	ret_rate = MGN_MCS10;	break;
		case DESC8723B_RATEMCS11:	ret_rate = MGN_MCS11;	break;
		case DESC8723B_RATEMCS12:	ret_rate = MGN_MCS12;	break;
		case DESC8723B_RATEMCS13:	ret_rate = MGN_MCS13;	break;
		case DESC8723B_RATEMCS14:	ret_rate = MGN_MCS14;	break;
		case DESC8723B_RATEMCS15:	ret_rate = MGN_MCS15;	break;
		case DESC8723B_RATEVHTSS1MCS0:	ret_rate = MGN_VHT1SS_MCS0; 	break;
		case DESC8723B_RATEVHTSS1MCS1:	ret_rate = MGN_VHT1SS_MCS1; 	break;
		case DESC8723B_RATEVHTSS1MCS2:	ret_rate = MGN_VHT1SS_MCS2; 	break;
		case DESC8723B_RATEVHTSS1MCS3:	ret_rate = MGN_VHT1SS_MCS3; 	break;
		case DESC8723B_RATEVHTSS1MCS4:	ret_rate = MGN_VHT1SS_MCS4; 	break;
		case DESC8723B_RATEVHTSS1MCS5:	ret_rate = MGN_VHT1SS_MCS5; 	break;
		case DESC8723B_RATEVHTSS1MCS6:	ret_rate = MGN_VHT1SS_MCS6; 	break;
		case DESC8723B_RATEVHTSS1MCS7:	ret_rate = MGN_VHT1SS_MCS7; 	break;
		case DESC8723B_RATEVHTSS1MCS8:	ret_rate = MGN_VHT1SS_MCS8; 	break;
		case DESC8723B_RATEVHTSS1MCS9:	ret_rate = MGN_VHT1SS_MCS9; 	break;
		case DESC8723B_RATEVHTSS2MCS0:	ret_rate = MGN_VHT2SS_MCS0; 	break;
		case DESC8723B_RATEVHTSS2MCS1:	ret_rate = MGN_VHT2SS_MCS1; 	break;
		case DESC8723B_RATEVHTSS2MCS2:	ret_rate = MGN_VHT2SS_MCS2; 	break;
		case DESC8723B_RATEVHTSS2MCS3:	ret_rate = MGN_VHT2SS_MCS3; 	break;
		case DESC8723B_RATEVHTSS2MCS4:	ret_rate = MGN_VHT2SS_MCS4; 	break;
		case DESC8723B_RATEVHTSS2MCS5:	ret_rate = MGN_VHT2SS_MCS5; 	break;
		case DESC8723B_RATEVHTSS2MCS6:	ret_rate = MGN_VHT2SS_MCS6; 	break;
		case DESC8723B_RATEVHTSS2MCS7:	ret_rate = MGN_VHT2SS_MCS7; 	break;
		case DESC8723B_RATEVHTSS2MCS8:	ret_rate = MGN_VHT2SS_MCS8; 	break;
		case DESC8723B_RATEVHTSS2MCS9:	ret_rate = MGN_VHT2SS_MCS9; 	break;				
		
		default:							
			DBG_8192C("HwRateToMRate8723B(): Non supported Rate [%x]!!!\n",rate );
			break;
	}	
	return ret_rate;
}

#endif // CONFIG_MP_INCLUDE

