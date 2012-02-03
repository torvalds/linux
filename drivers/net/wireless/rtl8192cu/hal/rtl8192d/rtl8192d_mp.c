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
#define _RTL8192D_MP_C_
#ifdef CONFIG_MP_INCLUDED

#include <drv_types.h>
#include <rtw_mp.h>

#ifdef CONFIG_RTL8192D
#include <rtl8192d_hal.h>
#endif

#define IQK_DELAY_TIME		1 	//ms

#define PHY_IQCalibrate(a)	rtl8192d_PHY_IQCalibrate(a)
#define PHY_LCCalibrate(a)	rtl8192d_PHY_LCCalibrate(a)
#define dm_CheckTXPowerTracking(a)	rtl8192d_dm_CheckTXPowerTracking(a)
#define PHY_SetRFPathSwitch(a,b)	rtl8192d_PHY_SetRFPathSwitch(a,b)


VOID Hal_MptSet8256CCKTxPower( PADAPTER pAdapter,u8 *pTxPower)
{
	u8				TxAGC[2]={0, 0};
	u32 			 tmpval=0;
	u8				rf;
	for(rf=0; rf<2; rf++)
		TxAGC[rf] = pTxPower[rf];

	// rf-A cck tx power
	PHY_SetBBReg(pAdapter, rTxAGC_A_CCK1_Mcs32, bMaskByte1, TxAGC[RF_PATH_A]);

	tmpval = (TxAGC[RF_PATH_A]<<16)|(TxAGC[RF_PATH_A]<<8)|(TxAGC[RF_PATH_A]);
	PHY_SetBBReg(pAdapter, rTxAGC_B_CCK11_A_CCK2_11, 0xffffff00, tmpval);

	// rf-B cck tx power
	PHY_SetBBReg(pAdapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte0, TxAGC[RF_PATH_B]);

	tmpval = (TxAGC[RF_PATH_B]<<16)|(TxAGC[RF_PATH_B]<<8)|(TxAGC[RF_PATH_B]);
	PHY_SetBBReg(pAdapter, rTxAGC_B_CCK1_55_Mcs32, 0xffffff00, tmpval);

}


VOID Hal_MptSet8256OFDMTxPower(PADAPTER pAdapter,u8 *pTxPower)
{
	u32 			 TxAGC=0;
	u8 tmpval=0;
	u8 rf;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	PMPT_CONTEXT		pMptCtx = &(pAdapter->mppriv.MptCtx);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	tmpval = pTxPower[0];
	TxAGC |= ((tmpval<<24)|(tmpval<<16)|(tmpval<<8)|tmpval);

	PHY_SetBBReg(pAdapter, rTxAGC_A_Rate18_06, bMaskDWord, TxAGC);
	PHY_SetBBReg(pAdapter, rTxAGC_A_Rate54_24, bMaskDWord, TxAGC);
	PHY_SetBBReg(pAdapter, rTxAGC_A_Mcs03_Mcs00, bMaskDWord, TxAGC);
	PHY_SetBBReg(pAdapter, rTxAGC_A_Mcs07_Mcs04, bMaskDWord, TxAGC);
	PHY_SetBBReg(pAdapter, rTxAGC_A_Mcs11_Mcs08, bMaskDWord, TxAGC);
	PHY_SetBBReg(pAdapter, rTxAGC_A_Mcs15_Mcs12, bMaskDWord, TxAGC);

	TxAGC=0;
	tmpval = pTxPower[1];
	TxAGC |= ((tmpval<<24)|(tmpval<<16)|(tmpval<<8)|tmpval);

	PHY_SetBBReg(pAdapter, rTxAGC_B_Rate18_06, bMaskDWord, TxAGC);
	PHY_SetBBReg(pAdapter, rTxAGC_B_Rate54_24, bMaskDWord, TxAGC);
	PHY_SetBBReg(pAdapter, rTxAGC_B_Mcs03_Mcs00, bMaskDWord, TxAGC);
	PHY_SetBBReg(pAdapter, rTxAGC_B_Mcs07_Mcs04, bMaskDWord, TxAGC);
	PHY_SetBBReg(pAdapter, rTxAGC_B_Mcs11_Mcs08, bMaskDWord, TxAGC);
	PHY_SetBBReg(pAdapter, rTxAGC_B_Mcs15_Mcs12, bMaskDWord, TxAGC);



}


void Hal_SetAntenna(PADAPTER pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	R_ANTENNA_SELECT_OFDM *p_ofdm_tx;	/* OFDM Tx register */
	R_ANTENNA_SELECT_CCK *p_cck_txrx;

	u8	r_rx_antenna_ofdm = 0, r_ant_select_cck_val = 0;
	u8	chgTx = 0, chgRx = 0;
	u32 r_ant_sel_cck_val = 0, r_ant_select_ofdm_val = 0, r_ofdm_tx_en_val = 0;


	p_ofdm_tx = (R_ANTENNA_SELECT_OFDM *)&r_ant_select_ofdm_val;
	p_cck_txrx = (R_ANTENNA_SELECT_CCK *)&r_ant_select_cck_val;

	p_ofdm_tx->r_ant_ht1	= 0x1;
	p_ofdm_tx->r_ant_ht2	= 0x2;	// Second TX RF path is A
	p_ofdm_tx->r_ant_non_ht = 0x3;	// 0x1+0x2=0x3

	switch (pAdapter->mppriv.antenna_tx)
	{
		case ANTENNA_A:
			p_ofdm_tx->r_tx_antenna 	= 0x1;
			r_ofdm_tx_en_val		= 0x1;
			p_ofdm_tx->r_ant_l		= 0x1;
			p_ofdm_tx->r_ant_ht_s1		= 0x1;
			p_ofdm_tx->r_ant_non_ht_s1	= 0x1;
			p_cck_txrx->r_ccktx_enable	= 0x8;
			chgTx = 1;

			// From SD3 Willis suggestion !!! Set RF A=TX and B as standby
//			if (IS_HARDWARE_TYPE_8192S(pAdapter))
			{
			write_bbreg(pAdapter, rFPGA0_XA_HSSIParameter2, 0xe, 2);
			write_bbreg(pAdapter, rFPGA0_XB_HSSIParameter2, 0xe, 1);
			r_ofdm_tx_en_val		= 0x3;

			// Power save
			//cosa r_ant_select_ofdm_val = 0x11111111;

			// We need to close RFB by SW control
			if (pHalData->rf_type == RF_2T2R)
			{
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT10, 0);
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT26, 1);
				PHY_SetBBReg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT10, 0);
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT1, 1);
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT17, 0);
			}
			}
			break;

		case ANTENNA_B:
			p_ofdm_tx->r_tx_antenna 	= 0x2;
			r_ofdm_tx_en_val		= 0x2;
			p_ofdm_tx->r_ant_l		= 0x2;
			p_ofdm_tx->r_ant_ht_s1		= 0x2;
			p_ofdm_tx->r_ant_non_ht_s1	= 0x2;
			p_cck_txrx->r_ccktx_enable	= 0x4;
			chgTx = 1;

			// From SD3 Willis suggestion !!! Set RF A as standby
			//if (IS_HARDWARE_TYPE_8192S(pAdapter))
			{
			PHY_SetBBReg(pAdapter, rFPGA0_XA_HSSIParameter2, 0xe, 1);
			PHY_SetBBReg(pAdapter, rFPGA0_XB_HSSIParameter2, 0xe, 2);
//			r_ofdm_tx_en_val		= 0x3;

			// Power save
			//cosa r_ant_select_ofdm_val = 0x22222222;

			// 2008/10/31 MH From SD3 Willi's suggestion. We must read RF 1T table.
			// 2009/01/08 MH From Sd3 Willis. We need to close RFA by SW control
			if (pHalData->rf_type == RF_2T2R || pHalData->rf_type == RF_1T2R)
			{
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT10, 1);
				PHY_SetBBReg(pAdapter, rFPGA0_XA_RFInterfaceOE, BIT10, 0);
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT26, 0);
//				PHY_SetBBReg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT10, 0);
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT1, 0);
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT17, 1);
			}
			}
		break;

		case ANTENNA_AB:	// For 8192S
			p_ofdm_tx->r_tx_antenna 	= 0x3;
			r_ofdm_tx_en_val		= 0x3;
			p_ofdm_tx->r_ant_l		= 0x3;
			p_ofdm_tx->r_ant_ht_s1		= 0x3;
			p_ofdm_tx->r_ant_non_ht_s1	= 0x3;
			p_cck_txrx->r_ccktx_enable	= 0xC;
			chgTx = 1;

			// From SD3 Willis suggestion !!! Set RF B as standby
			//if (IS_HARDWARE_TYPE_8192S(pAdapter))
			{
			PHY_SetBBReg(pAdapter, rFPGA0_XA_HSSIParameter2, 0xe, 2);
			PHY_SetBBReg(pAdapter, rFPGA0_XB_HSSIParameter2, 0xe, 2);

			// Disable Power save
			//cosa r_ant_select_ofdm_val = 0x3321333;
#if 0
			// 2008/10/31 MH From SD3 Willi's suggestion. We must read RFA 2T table.
			if ((pHalData->VersionID == VERSION_8192S_ACUT)) // For RTL8192SU A-Cut only, by Roger, 2008.11.07.
			{
				mpt_RFConfigFromPreParaArrary(pAdapter, 1, RF_PATH_A);
			}
#endif
			// 2009/01/08 MH From Sd3 Willis. We need to enable RFA/B by SW control
			if (pHalData->rf_type == RF_2T2R)
			{
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT10, 0);
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT26, 0);
//				PHY_SetBBReg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT10, 0);
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT1, 1);
				PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFParameter, BIT17, 1);
			}
			}
			break;

		default:
			break;
	}

	//
	// r_rx_antenna_ofdm, bit0=A, bit1=B, bit2=C, bit3=D
	// r_cckrx_enable : CCK default, 0=A, 1=B, 2=C, 3=D
	// r_cckrx_enable_2 : CCK option, 0=A, 1=B, 2=C, 3=D
	//
	switch (pAdapter->mppriv.antenna_rx)
	{
		case ANTENNA_A:
			r_rx_antenna_ofdm		= 0x1;	// A
			p_cck_txrx->r_cckrx_enable	= 0x0;	// default: A
			p_cck_txrx->r_cckrx_enable_2	= 0x0;	// option: A
			chgRx = 1;
			break;

		case ANTENNA_B:
			r_rx_antenna_ofdm		= 0x2;	// B
			p_cck_txrx->r_cckrx_enable	= 0x1;	// default: B
			p_cck_txrx->r_cckrx_enable_2	= 0x1;	// option: B
			chgRx = 1;
			break;

		case ANTENNA_AB:
			r_rx_antenna_ofdm		= 0x3;	// AB
			p_cck_txrx->r_cckrx_enable	= 0x0;	// default:A
			p_cck_txrx->r_cckrx_enable_2	= 0x1;	// option:B
			chgRx = 1;
			break;

		default:
			break;
	}

	if (chgTx && chgRx)
	{
		switch(pHalData->rf_chip)
		{
			case RF_8225:
			case RF_8256:
			case RF_6052:
				//r_ant_sel_cck_val = r_ant_select_cck_val;
				PHY_SetBBReg(pAdapter, rFPGA1_TxInfo, 0x7fffffff, r_ant_select_ofdm_val);	//OFDM Tx
				PHY_SetBBReg(pAdapter, rFPGA0_TxInfo, 0x0000000f, r_ofdm_tx_en_val);		//OFDM Tx
				PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable, 0x0000000f, r_rx_antenna_ofdm);	//OFDM Rx
				PHY_SetBBReg(pAdapter, rOFDM1_TRxPathEnable, 0x0000000f, r_rx_antenna_ofdm);	//OFDM Rx
				PHY_SetBBReg(pAdapter, rCCK0_AFESetting, bMaskByte3, r_ant_select_cck_val);//r_ant_sel_cck_val);		//CCK TxRx
#ifdef CONFIG_RTL8192D
				if(pHalData->CurrentBandType92D == BAND_ON_2_4G || IS_92D_SINGLEPHY(pHalData->VersionID))
						rtw_write8(pAdapter, rCCK0_AFESetting+3, r_ant_select_cck_val);
#endif
				break;

			default:
				break;
		}
	}

	RT_TRACE(_module_mp_, _drv_notice_, ("-SwitchAntenna: finished\n"));
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



void Hal_SetCarrierSuppressionTx(PADAPTER pAdapter, u8 bStart)
{
   // PMGNT_INFO          pMgntInfo = &(pAdapter->MgntInfo);
    HAL_DATA_TYPE       *pHalData   = GET_HAL_DATA(pAdapter);
    PMPT_CONTEXT	pMptCtx = &pAdapter->mppriv.MptCtx;


    if(bStart)
    { // Start Carrier Suppression.
        //if(pMgntInfo->dot11CurrentWirelessMode == WIRELESS_MODE_B)
        if( pMptCtx->MptRateIndex >= MPT_RATE_1M &&
            pMptCtx->MptRateIndex <= MPT_RATE_11M )
        { // Start CCK Carrier Suppression
            // 1. if CCK block on?
            if(!PHY_QueryBBReg(pAdapter, rFPGA0_RFMOD, bCCKEn))
                PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, bCCKEn, bEnable);//set CCK block on

            //Turn Off All Test Mode
            PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bDisable);
            PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bDisable);
            PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);

            PHY_SetBBReg(pAdapter, rCCK0_System, bCCKBBMode, 0x2);    //transmit mode
            PHY_SetBBReg(pAdapter, rCCK0_System, bCCKScramble, 0x0);  //turn off scramble setting
                //Set CCK Tx Test Rate
            //PHY_SetBBReg(pAdapter, rCCK0_System, bCCKTxRate, pMgntInfo->ForcedDataRate);
            PHY_SetBBReg(pAdapter, rCCK0_System, bCCKTxRate, 0x0);    //Set FTxRate to 1Mbps
        }
    }
    else
    { // Stop Carrier Suppression.
        //if(pMgntInfo->dot11CurrentWirelessMode == WIRELESS_MODE_B)
        if( pMptCtx->MptRateIndex >= MPT_RATE_1M &&
            pMptCtx->MptRateIndex <= MPT_RATE_11M )
        { // Stop Carrier Suppression
            PHY_SetBBReg(pAdapter, rCCK0_System, bCCKBBMode, 0x0);    //normal mode
            PHY_SetBBReg(pAdapter, rCCK0_System, bCCKScramble, 0x1);  //turn on scramble setting

            //BB Reset
            PHY_SetBBReg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
            PHY_SetBBReg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);
        }
    }

}


void Hal_SetSingleToneTx ( PADAPTER pAdapter , u8 bStart )
{
   // PMGNT_INFO          pMgntInfo = &(pAdapter->MgntInfo);
    HAL_DATA_TYPE       *pHalData   = GET_HAL_DATA(pAdapter);
    PMPT_CONTEXT	pMptCtx = &pAdapter->mppriv.MptCtx;
    u8              CurrChannel = pAdapter->mppriv.channel;
    u32              ulAntennaTx = pAdapter->mppriv.antenna_tx;
    RF_RADIO_PATH_E   rfPath;

    switch(ulAntennaTx)
    {
        case ANTENNA_A:         rfPath = RF_PATH_A;       break;
        case ANTENNA_B:         rfPath = RF_PATH_B;       break;
        case ANTENNA_C:         rfPath = RF_PATH_C;       break;
        default:
            rfPath = RF_PATH_C;
            break;
    }

    if(bStart)
    {   // Start Single Tone.
    #if 1// use RF debug mode out by 'LO'.
        //DbgPrint(" Started from Normal ->Single tone\n");
        //Normal ->Single tone
        PHY_SetRFReg(pAdapter, rfPath, 0x00, bRFRegOffsetMask, 0xb0);
        rtw_udelay_os(IQK_DELAY_TIME*100);
        PHY_SetRFReg(pAdapter, rfPath, 0x01, bRFRegOffsetMask, 0x41f);
        rtw_udelay_os(IQK_DELAY_TIME*100);
        PHY_SetRFReg(pAdapter, rfPath, 0x0c, bRFRegOffsetMask, 0xc40);
        rtw_udelay_os(IQK_DELAY_TIME*100);
        PHY_SetRFReg(pAdapter, rfPath, 0x08, bRFRegOffsetMask, 0xe01);
        rtw_udelay_os(IQK_DELAY_TIME*100);
        PHY_SetRFReg(pAdapter, rfPath, 0x09, bRFRegOffsetMask, 0x5f0);
        rtw_udelay_os(IQK_DELAY_TIME*100);
    #else// the old one, trigger by BB register..
        // 1. if OFDM block on?
        if(!PHY_QueryBBReg(pAdapter, rFPGA0_RFMOD, bOFDMEn))
            PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, bOFDMEn, bEnable);//set OFDM block on

        // 2. set CCK test mode off, set to CCK normal mode
        PHY_SetBBReg(pAdapter, rCCK0_System, bCCKBBMode, bDisable);

        // 3. turn on scramble setting
        PHY_SetBBReg(pAdapter, rCCK0_System, bCCKScramble, bEnable);

        // 4. Turn On Continue Tx and turn off the other test modes.
        PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bDisable);
        PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bDisable);
        PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bEnable);
    #endif
    }
    else
    {   // Stop Single Tone.
    #if 1// set RF mode from debug mode to normal mode.
        //DbgPrint(" end from Single tone -> Normal \n");
        PHY_SetRFReg(pAdapter, rfPath, 0x01, bRFRegOffsetMask, 0xee0);
        rtw_udelay_os(IQK_DELAY_TIME*100);
        PHY_SetRFReg(pAdapter, rfPath, 0x0c, bRFRegOffsetMask, 0x240);
        rtw_udelay_os(IQK_DELAY_TIME*100);
        PHY_SetRFReg(pAdapter, rfPath, 0x08, bRFRegOffsetMask, 0xe1c);
        rtw_udelay_os(IQK_DELAY_TIME*100);
        PHY_SetRFReg(pAdapter, rfPath, 0x09, bRFRegOffsetMask, 0x7f0);
        rtw_udelay_os(IQK_DELAY_TIME*100);
        PHY_SetRFReg(pAdapter, rfPath, 0x00, bRFRegOffsetMask, 0xbf);
        rtw_udelay_os(IQK_DELAY_TIME*100);
    #else//old code, stop single tone by BB registers.
        //Turn off all test modes.
        PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bDisable);
        PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bDisable);
        PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);
        //Delay 10 ms
        delay_ms(10);
        //BB Reset
        PHY_SetBBReg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
        PHY_SetBBReg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);
    #endif
    }

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


VOID Hal_TriggerRFThermalMeter( PADAPTER pAdapter )
{
   // PADAPTER			  pAdapter = (PADAPTER)Context;
	PHY_SetRFReg(pAdapter, RF_PATH_A, RF_T_METER, BIT17 | BIT16, 0x03);
}


u8 Hal_ReadRFThermalMeter(PADAPTER pAdapter)
{
	u32 ThermalValue = 0;
	
	ThermalValue = _read_rfreg(pAdapter, RF_PATH_A, RF_T_METER, 0xf800);	//0x42: RF Reg[15:11]
//	RT_TRACE(_module_mp_, _drv_alert_, ("ThermalValue = 0x%x\n", ThermalValue));
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

void Hal_SetTxPower (PADAPTER pAdapter)
{
	HAL_DATA_TYPE		*pHalData	= GET_HAL_DATA(pAdapter);
	//u1Byte				CurrChannel;
	u8                  TxPowerLevel_CCK[2], TxPowerLevel_HTOFDM[2];
    PMPT_CONTEXT        pMptCtx = &(pAdapter->mppriv.MptCtx);
	u8  				rf;

	printk("%s",__func__);

    TxPowerLevel_CCK[RF_PATH_A] = pAdapter->mppriv.txpoweridx;
	TxPowerLevel_CCK[RF_PATH_B] = pAdapter->mppriv.txpoweridx_b;
    TxPowerLevel_HTOFDM[RF_PATH_A] = pAdapter->mppriv.txpoweridx;
	TxPowerLevel_HTOFDM[RF_PATH_B] = pAdapter->mppriv.txpoweridx_b;

	for(rf=0; rf<2; rf++)
	{
		if(IS_HARDWARE_TYPE_8192D(pAdapter))
		{
			//RT_TRACE(COMP_MP, DBG_LOUD, ("antenna settings txpath 0x%x\n", pHalData->AntennaTxPath));
			switch(pHalData->AntennaTxPath)
			{
				case ANTENNA_B:
					TxPowerLevel_CCK[rf] = pAdapter->mppriv.txpoweridx_b;
					break;

				case ANTENNA_A:
				case ANTENNA_AB:
				default:
					TxPowerLevel_CCK[rf] =pAdapter->mppriv.txpoweridx;
					break;
			}
		}
	}
	switch(pHalData->rf_chip)
	{
		// 2008/09/12 MH Test only !! We enable the TX power tracking for MP!!!!!
		// We should call normal driver API later!!
		case RF_8225:
		case RF_8256:
		case RF_6052:
			Hal_MptSet8256CCKTxPower(pAdapter, &TxPowerLevel_CCK[0]);
			Hal_MptSet8256OFDMTxPower(pAdapter, &TxPowerLevel_HTOFDM[0]);
			break;

		default:
			break;
	}
	//DbgPrint("\n MPT_ProSetTxPower() is finished \n");
}


void Hal_SetSingleCarrierTx (PADAPTER pAdapter, u8 bStart)
{
    HAL_DATA_TYPE       *pHalData   = GET_HAL_DATA(pAdapter);
    u8              CurrChannel = pAdapter->mppriv.channel;
     PMPT_CONTEXT        pMptCtx = &(pAdapter->mppriv.MptCtx);

    if ( bStart )
    {// Start Single Carrier.
        // 1. if OFDM block on?
        if(!PHY_QueryBBReg(pAdapter, rFPGA0_RFMOD, bOFDMEn))
            PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, bOFDMEn, bEnable);//set OFDM block on

        if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
        {
            // 2. set CCK test mode off, set to CCK normal mode
            PHY_SetBBReg(pAdapter, rCCK0_System, bCCKBBMode, bDisable);

            // 3. turn on scramble setting
            PHY_SetBBReg(pAdapter, rCCK0_System, bCCKScramble, bEnable);
        }

        // 4. Turn On Continue Tx and turn off the other test modes.
        PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bDisable);
        PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bEnable);
        PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);
    }
    else
    { // Stop Single Carrier.
        //Turn off all test modes.
        PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bDisable);
        PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bDisable);
        PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);
        //Delay 10 ms
        rtw_mdelay_os(10);
        //BB Reset
        PHY_SetBBReg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
        PHY_SetBBReg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);
    }
}

static  VOID Hal_mpt_StartCckContTx(PADAPTER pAdapter,BOOLEAN bScrambleOn)
{

    HAL_DATA_TYPE   *pHalData   = GET_HAL_DATA(pAdapter);
    PMPT_CONTEXT        pMptCtx = &pAdapter->mppriv.MptCtx;
    //u1Byte            u1bReg;
    //u4Byte            data;
    //u1Byte            CckTxAGC;
    u32          cckrate;

    // 1. if CCK block on?
    if(!PHY_QueryBBReg(pAdapter, rFPGA0_RFMOD, bCCKEn))
        PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, bCCKEn, bEnable);//set CCK block on

    //Turn Off All Test Mode
    PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bDisable);
    PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bDisable);
    PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);
    //Set CCK Tx Test Rate
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
    PHY_SetBBReg(pAdapter, rCCK0_System, bCCKTxRate, cckrate);

    PHY_SetBBReg(pAdapter, rCCK0_System, bCCKBBMode, 0x2);    //transmit mode
    PHY_SetBBReg(pAdapter, rCCK0_System, bCCKScramble, 0x1);  //turn on scramble setting

    pMptCtx->bCckContTx = _TRUE;
    pMptCtx->bOfdmContTx = _FALSE;

}   /* mpt_StartCckContTx */


static  VOID Hal_mpt_StopCckCoNtTx(PADAPTER pAdapter)
{
    HAL_DATA_TYPE   *pHalData   = GET_HAL_DATA(pAdapter);
    PMPT_CONTEXT        pMptCtx = &(pAdapter->mppriv.MptCtx);

    u8          u1bReg;

    pMptCtx->bCckContTx = _FALSE;
    pMptCtx->bOfdmContTx = _FALSE;

    PHY_SetBBReg(pAdapter, rCCK0_System, bCCKBBMode, 0x0);    //normal mode
    PHY_SetBBReg(pAdapter, rCCK0_System, bCCKScramble, 0x1);  //turn on scramble setting

    //BB Reset
    PHY_SetBBReg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
    PHY_SetBBReg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);

}   /* mpt_StopCckCoNtTx */


static  VOID Hal_mpt_StartOfdmContTx( PADAPTER pAdapter )
{

    HAL_DATA_TYPE   *pHalData   = GET_HAL_DATA(pAdapter);
    PMPT_CONTEXT        pMptCtx = &(pAdapter->mppriv.MptCtx);
    //u1Byte            u1bReg;
    //u4Byte            data;
    //u1Byte            OfdmTxAGC;

    // 1. if OFDM block on?
    if(!PHY_QueryBBReg(pAdapter, rFPGA0_RFMOD, bOFDMEn))
        PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, bOFDMEn, bEnable);//set OFDM block on

    if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
    {
        // 2. set CCK test mode off, set to CCK normal mode
        PHY_SetBBReg(pAdapter, rCCK0_System, bCCKBBMode, bDisable);

        // 3. turn on scramble setting
        PHY_SetBBReg(pAdapter, rCCK0_System, bCCKScramble, bEnable);
    }

    // 4. Turn On Continue Tx and turn off the other test modes.
       PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bEnable);
       PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bDisable);
       PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);

    pMptCtx->bCckContTx = _FALSE;
    pMptCtx->bOfdmContTx = _TRUE;
    //pMptCtx->bCtxTriggerPktSent = _FALSE;

}   /* mpt_StartOfdmContTx */


static  VOID Hal_mpt_StopOfdmContTx( PADAPTER pAdapter)
{
    HAL_DATA_TYPE   *pHalData   = GET_HAL_DATA(pAdapter);
    PMPT_CONTEXT        pMptCtx = &(pAdapter->mppriv.MptCtx);
    u8          u1bReg;
    u32          data;

    pMptCtx->bCckContTx = _FALSE;
    pMptCtx->bOfdmContTx = _FALSE;

    PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bDisable);
    PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bDisable);
    PHY_SetBBReg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);
    //Delay 10 ms
    rtw_msleep_os(10);
    //BB Reset
    PHY_SetBBReg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
    PHY_SetBBReg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);

}   /* mpt_StopOfdmContTx */


void Hal_SetContinuousTx (PADAPTER pAdapter, u8 bStart)
{
    HAL_DATA_TYPE       *pHalData   = GET_HAL_DATA(pAdapter);
    PMPT_CONTEXT        pMptCtx = &(pAdapter->mppriv.MptCtx);

    if(bStart)
    { // Start Continuous Tx.
        if( pAdapter->mppriv.rateidx >= MPT_RATE_1M &&
            pAdapter->mppriv.rateidx <= MPT_RATE_11M )
            Hal_mpt_StartCckContTx(pAdapter, _TRUE);
        else if(pAdapter->mppriv.rateidx >= MPT_RATE_6M &&
                pAdapter->mppriv.rateidx <= MPT_RATE_MCS15 )
            Hal_mpt_StartOfdmContTx(pAdapter);
        else
        {
            //RT_ASSERT(_FALSE, ("MPT_ProSetUpContTx(): Unknown wireless rate index: %d\n", pMptCtx->MptRateIndex));
            pMptCtx->bStartContTx = _FALSE;
            pMptCtx->bCckContTx = _FALSE;
            pMptCtx->bOfdmContTx = _FALSE;
        }

    }
    else
    { // Stop Continuous Tx.
        BOOLEAN bCckContTx = pMptCtx->bCckContTx;
        BOOLEAN bOfdmContTx = pMptCtx->bOfdmContTx;

        if(bCckContTx == _TRUE && bOfdmContTx == _FALSE)
        { // Stop CCK Continuous Tx.
            Hal_mpt_StopCckCoNtTx(pAdapter);
        }
        else if(bCckContTx == _FALSE && bOfdmContTx == _TRUE)
        { // Stop OFDM Continuous Tx.
            Hal_mpt_StopOfdmContTx(pAdapter);
        }
        else if(bCckContTx == _FALSE && bOfdmContTx == _FALSE)
        { // We've already stopped Continuous Tx.
        }
        else
        { // Unexpected case.

        }
    }

}


void Hal_mpt_SwitchRfSetting(PADAPTER pAdapter)
{
	
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(pAdapter);
	struct mp_priv *pmp = &pAdapter->mppriv;
	u8 ChannelToSw = pmp->channel, eRFPath = RF_PATH_A;
	u8 ulRateIdx = pmp->rateidx;
	u8 ulbandwidth = pmp->bandwidth;
	PMPT_CONTEXT	pMptCtx = &(pAdapter->mppriv.MptCtx);
    BOOLEAN             bInteralPA = _FALSE;
    u32				value = 0;
    
    
	if (((ulRateIdx == MPT_RATE_1M || ulRateIdx == MPT_RATE_6M || ulRateIdx == MPT_RATE_MCS0 ||
        ulRateIdx == MPT_RATE_MCS8) && ulbandwidth == HT_CHANNEL_WIDTH_20 &&
        (ChannelToSw == 1 || ChannelToSw == 11)) ||
        ((ulRateIdx == MPT_RATE_MCS0 ||ulRateIdx == MPT_RATE_MCS8) &&
        ulbandwidth == HT_CHANNEL_WIDTH_40 &&
        (ChannelToSw == 3 || ChannelToSw == 9)))

    {
        value = 0x294a5;
    }
    else
    {
        value = 0x18c63;
    }


    for(eRFPath = 0; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
    {
        if(pHalData->MacPhyMode92D == DUALMAC_DUALPHY &&
            pHalData->interfaceIndex == 1)      //MAC 1 5G
            bInteralPA = pHalData->InternalPA5G[1];
        else
            bInteralPA = pHalData->InternalPA5G[eRFPath];

        if(!bInteralPA ||  pHalData->CurrentBandType92D==BAND_ON_2_4G)
        	_write_rfreg(pAdapter, (RF_RADIO_PATH_E)eRFPath, 0x03, bRFRegOffsetMask, value);
    }
}

void Hal_SetBandwidth(PADAPTER pAdapter)
{
	struct mp_priv *pmp = &pAdapter->mppriv;

	SetBWMode(pAdapter, pmp->bandwidth, pmp->prime_channel_offset);
	Hal_mpt_SwitchRfSetting(pAdapter);
}


void MPT_CCKTxPowerAdjust(PADAPTER Adapter,BOOLEAN	bInCH14)
{
	u4Byte				TempVal = 0, TempVal2 = 0, TempVal3 = 0;
	u4Byte				CurrCCKSwingVal=0, CCKSwingIndex=12;
	HAL_DATA_TYPE		*pHalData	= GET_HAL_DATA(Adapter);
	u1Byte				i;


	// get current cck swing value and check 0xa22 & 0xa23 later to match the table.
	
	CurrCCKSwingVal = PHY_QueryBBReg(Adapter, rCCK0_TxFilter1, bMaskHWord);
	
	if(!bInCH14)
	{
		// Readback the current bb cck swing value and compare with the table to 
		// get the current swing index
		for(i=0 ; i<CCK_TABLE_SIZE ; i++)
		{
			if( ((CurrCCKSwingVal&0xff) == (u4Byte)CCKSwingTable_Ch1_Ch13[i][0]) &&
				( ((CurrCCKSwingVal&0xff00)>>8) == (u4Byte)CCKSwingTable_Ch1_Ch13[i][1]) )
			{
				CCKSwingIndex = i;
				//RT_TRACE(COMP_INIT, DBG_LOUD,("Ch1~13, Current reg0x%x = 0x%lx, CCKSwingIndex=0x%x\n", (rCCK0_TxFilter1+2), CurrCCKSwingVal, CCKSwingIndex));
				break;
			}
		}
		
		//Write 0xa22 0xa23
		TempVal =	CCKSwingTable_Ch1_Ch13[CCKSwingIndex][0] +
					(CCKSwingTable_Ch1_Ch13[CCKSwingIndex][1]<<8) ;
	
		
		//Write 0xa24 ~ 0xa27
		TempVal2 = 0;
		TempVal2 =	CCKSwingTable_Ch1_Ch13[CCKSwingIndex][2] +
					(CCKSwingTable_Ch1_Ch13[CCKSwingIndex][3]<<8) +
					(CCKSwingTable_Ch1_Ch13[CCKSwingIndex][4]<<16 )+
					(CCKSwingTable_Ch1_Ch13[CCKSwingIndex][5]<<24);
		
		//Write 0xa28  0xa29
		TempVal3 = 0;
		TempVal3 =	CCKSwingTable_Ch1_Ch13[CCKSwingIndex][6] +
					(CCKSwingTable_Ch1_Ch13[CCKSwingIndex][7]<<8) ;
		
		
	}
	else
	{
		for(i=0 ; i<CCK_TABLE_SIZE ; i++)
		{
			if( ((CurrCCKSwingVal&0xff) == (u4Byte)CCKSwingTable_Ch14[i][0]) &&
				( ((CurrCCKSwingVal&0xff00)>>8) == (u4Byte)CCKSwingTable_Ch14[i][1]) )
			{
				CCKSwingIndex = i;
				//RT_TRACE(COMP_INIT, DBG_LOUD,("Ch14, Current reg0x%x = 0x%lx, CCKSwingIndex=0x%x\n", (rCCK0_TxFilter1+2), CurrCCKSwingVal, CCKSwingIndex));
				break;
			}
		}
		
		//Write 0xa22 0xa23
		TempVal =	CCKSwingTable_Ch14[CCKSwingIndex][0] +
					(CCKSwingTable_Ch14[CCKSwingIndex][1]<<8) ;
		
		//Write 0xa24 ~ 0xa27
		TempVal2= 0;
		TempVal2 =	CCKSwingTable_Ch14[CCKSwingIndex][2] +
					(CCKSwingTable_Ch14[CCKSwingIndex][3]<<8) +
					(CCKSwingTable_Ch14[CCKSwingIndex][4]<<16 )+
					(CCKSwingTable_Ch14[CCKSwingIndex][5]<<24);
		
		//Write 0xa28  0xa29
		TempVal3 = 0;
		TempVal3 =	CCKSwingTable_Ch14[CCKSwingIndex][6] +
					(CCKSwingTable_Ch14[CCKSwingIndex][7]<<8) ;
	}

	PHY_SetBBReg(Adapter, rCCK0_TxFilter1,bMaskHWord, TempVal);
	//RTPRINT(FMP, MP_SWICH_CH, ("0xA20=0x%x\n", TempVal));
	PHY_SetBBReg(Adapter, rCCK0_TxFilter2,bMaskDWord, TempVal2);
	//RTPRINT(FMP, MP_SWICH_CH, ("0xA24=0x%x\n", TempVal2));
	PHY_SetBBReg(Adapter, rCCK0_DebugPort,bMaskLWord, TempVal3);
	//RTPRINT(FMP, MP_SWICH_CH, ("0xA28=0x%x\n", TempVal3));

}


void Hal_SetChannel(PADAPTER pAdapter)
{
#if 0
	struct mp_priv *pmp = &pAdapter->mppriv;

//	SelectChannel(pAdapter, pmp->channel);
	set_channel_bwmode(pAdapter, pmp->channel, pmp->channel_offset, pmp->bandwidth);
#else
	u8		eRFPath;

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct mp_priv	*pmp = &pAdapter->mppriv;
	u8		channel = pmp->channel;
	u8		bandwidth = pmp->bandwidth;
	u8		rate = pmp->rateidx;


	// set RF channel register
	for (eRFPath = 0; eRFPath < pHalData->NumTotalRFPath; eRFPath++)
	{
	  if(IS_HARDWARE_TYPE_8192D(pAdapter))
			_write_rfreg(pAdapter, (RF_RADIO_PATH_E)eRFPath, rRfChannel, 0xFF, channel);
		else
		_write_rfreg(pAdapter, eRFPath, rRfChannel, 0x3FF, channel);
	}
	Hal_mpt_SwitchRfSetting(pAdapter);

	SelectChannel(pAdapter, channel);

	if (pHalData->CurrentChannel == 14 && !pHalData->dmpriv.bCCKinCH14) {
		pHalData->dmpriv.bCCKinCH14 = _TRUE;
		MPT_CCKTxPowerAdjust(pAdapter, pHalData->dmpriv.bCCKinCH14);
	}
	else if (pHalData->CurrentChannel != 14 && pHalData->dmpriv.bCCKinCH14) {
		pHalData->dmpriv.bCCKinCH14 = _FALSE;
		MPT_CCKTxPowerAdjust(pAdapter, pHalData->dmpriv.bCCKinCH14);
	}
#if 0
//#ifdef CONFIG_USB_HCI
	// Georgia add 2009-11-17, suggested by Edlu , for 8188CU ,46 PIN
	if (!IS_92C_SERIAL(pHalData->VersionID) && !IS_NORMAL_CHIP(pHalData->VersionID)) {
		mpt_AdjustRFRegByRateByChan92CU(pAdapter, rate, pHalData->CurrentChannel, bandwidth);
	}
#endif

#endif
}


void Hal_SetCCKTxPower(PADAPTER pAdapter, u8 *TxPower)
{
	u32 tmpval = 0;


	// rf-A cck tx power
	write_bbreg(pAdapter, rTxAGC_A_CCK1_Mcs32, bMaskByte1, TxPower[RF_PATH_A]);
	tmpval = (TxPower[RF_PATH_A]<<16) | (TxPower[RF_PATH_A]<<8) | TxPower[RF_PATH_A];
	write_bbreg(pAdapter, rTxAGC_B_CCK11_A_CCK2_11, 0xffffff00, tmpval);

	// rf-B cck tx power
	write_bbreg(pAdapter, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte0, TxPower[RF_PATH_B]);
	tmpval = (TxPower[RF_PATH_B]<<16) | (TxPower[RF_PATH_B]<<8) | TxPower[RF_PATH_B];
	write_bbreg(pAdapter, rTxAGC_B_CCK1_55_Mcs32, 0xffffff00, tmpval);

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

	if (pHalData->dmpriv.bAPKdone && !IS_NORMAL_CHIP(pHalData->VersionID))
	{
		if (tmpval > pMptCtx->APK_bound[RF_PATH_A])
			write_rfreg(pAdapter, RF_PATH_A, 0xe, pHalData->dmpriv.APKoutput[0][0]);
		else
			write_rfreg(pAdapter, RF_PATH_A, 0xe, pHalData->dmpriv.APKoutput[0][1]);
	}

	// HT Tx-rf(B)
	tmpval = TxPower[RF_PATH_B];
	TxAGC = (tmpval<<24) | (tmpval<<16) | (tmpval<<8) | tmpval;

	write_bbreg(pAdapter, rTxAGC_B_Rate18_06, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_B_Rate54_24, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_B_Mcs03_Mcs00, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_B_Mcs07_Mcs04, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_B_Mcs11_Mcs08, bMaskDWord, TxAGC);
	write_bbreg(pAdapter, rTxAGC_B_Mcs15_Mcs12, bMaskDWord, TxAGC);

	if (pHalData->dmpriv.bAPKdone && !IS_NORMAL_CHIP(pHalData->VersionID))
	{
		if (tmpval > pMptCtx->APK_bound[RF_PATH_B])
			write_rfreg(pAdapter, RF_PATH_B, 0xe, pHalData->dmpriv.APKoutput[1][0]);
		else
			write_rfreg(pAdapter, RF_PATH_B, 0xe, pHalData->dmpriv.APKoutput[1][1]);
	}

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
			Hal_SetOFDMTxPower(pAdapter, TxPowerLevel);
			break;

		default:
			break;
	}
}

void Hal_SetDataRate(PADAPTER pAdapter)
{
	Hal_mpt_SwitchRfSetting(pAdapter);
}

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
	}

	pAdapter->mppriv.MptCtx.bCckContTx = _FALSE;
	pAdapter->mppriv.MptCtx.bOfdmContTx = bStart;
}/* mpt_StartOfdmContTx */


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
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMContinueTx, bDisable);
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMSingleCarrier, bDisable);
		write_bbreg(pAdapter, rOFDM1_LSTF, bOFDMSingleTone, bDisable);
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
		write_bbreg(pAdapter, rCCK0_System, bCCKScramble, bEnable); //turn on scramble setting

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
		write_bbreg(pAdapter, rCCK0_System, bCCKScramble, bEnable); //turn on scramble setting

		//BB Reset
		write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x0);
		write_bbreg(pAdapter, rPMAC_Reset, bBBResetB, 0x1);
	}

	pAdapter->mppriv.MptCtx.bCckContTx = bStart;
	pAdapter->mppriv.MptCtx.bOfdmContTx = _FALSE;
}/* mpt_StartCckContTx */


void Hal_ProSetCrystalCap (PADAPTER pAdapter , u32 CrystalCapVal)
{
	HAL_DATA_TYPE		*pHalData	= GET_HAL_DATA(pAdapter);

	if(!IS_HARDWARE_TYPE_8192D(pAdapter))
		return;

	//CrystalCap = pHalData->CrystalCap;

	PHY_SetBBReg(pAdapter, 0x24, 0xF0, CrystalCapVal & 0x0F);
	PHY_SetBBReg(pAdapter, 0x28, 0xF0000000, (CrystalCapVal & 0xF0) >> 4);

}


#endif // CONFIG_MP_INCLUDED

