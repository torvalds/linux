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

 #include "mp_precomp.h"
 #include "phydm_precomp.h"

#ifndef index_mapping_NUM_88E
 #define	index_mapping_NUM_88E	15
#endif

//#if(DM_ODM_SUPPORT_TYPE & ODM_WIN)

#define 	CALCULATE_SWINGTALBE_OFFSET(_offset, _direction, _size, _deltaThermal) \
					do {\
						for(_offset = 0; _offset < _size; _offset++)\
						{\
							if(_deltaThermal < thermalThreshold[_direction][_offset])\
							{\
								if(_offset != 0)\
									_offset--;\
								break;\
							}\
						}			\
						if(_offset >= _size)\
							_offset = _size-1;\
					} while(0)


void ConfigureTxpowerTrack(
	IN	PVOID		pDM_VOID,
	OUT	PTXPWRTRACK_CFG	pConfig
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
#if RTL8812A_SUPPORT
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	//if (IS_HARDWARE_TYPE_8812(pDM_Odm->Adapter))
	if(pDM_Odm->SupportICType==ODM_RTL8812)
		ConfigureTxpowerTrack_8812A(pConfig);
	//else
#endif
#endif

#if RTL8814A_SUPPORT
	if(pDM_Odm->SupportICType== ODM_RTL8814A)
		ConfigureTxpowerTrack_8814A(pConfig);
#endif


#if RTL8188E_SUPPORT
	if(pDM_Odm->SupportICType==ODM_RTL8188E)
		ConfigureTxpowerTrack_8188E(pConfig);
#endif 
}

#if (RTL8192E_SUPPORT==1) 
VOID
ODM_TXPowerTrackingCallback_ThermalMeter_92E(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN	PVOID		pDM_VOID
#else
	IN PADAPTER	Adapter
#endif
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte	ThermalValue = 0, delta, delta_IQK, delta_LCK, channel, is_decrease, rf_mimo_mode;
	u1Byte	ThermalValue_AVG_count = 0;
    	u1Byte     OFDM_min_index = 10; //OFDM BB Swing should be less than +2.5dB, which is required by Arthur
	s1Byte	OFDM_index[2], index ;
    	u4Byte	ThermalValue_AVG = 0, Reg0x18;
	u4Byte	i = 0, j = 0, rf;
	s4Byte	value32, CCK_index = 0, ele_A, ele_D, ele_C, X, Y;
	prtl8192cd_priv 	priv = pDM_Odm->priv;

	rf_mimo_mode = pDM_Odm->RFType;
	//ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("%s:%d rf_mimo_mode:%d\n", __FUNCTION__, __LINE__, rf_mimo_mode));

#ifdef MP_TEST
	if ((OPMODE & WIFI_MP_STATE) || priv->pshare->rf_ft_var.mp_specific) {
		channel = priv->pshare->working_channel;
		if (priv->pshare->mp_txpwr_tracking == FALSE)
			return;
	} else
#endif
	{
		channel = (priv->pmib->dot11RFEntry.dot11channel);
	}

	ThermalValue = (unsigned char)ODM_GetRFReg(pDM_Odm, RF_PATH_A, ODM_RF_T_METER_92E, 0xfc00);	//0x42: RF Reg[15:10] 88E
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", ThermalValue, priv->pshare->ThermalValue, priv->pmib->dot11RFEntry.ther));


	switch (rf_mimo_mode) {
		case MIMO_1T1R:
			rf = 1;      
			break;
		case MIMO_2T2R:
			rf = 2;
			break;
		default:
			rf = 2;
			break;
	}

	//Query OFDM path A default setting 	Bit[31:21]
	ele_D = PHY_QueryBBReg(priv, rOFDM0_XATxIQImbalance, bMaskOFDM_D);
	for (i = 0; i < OFDM_TABLE_SIZE_92E; i++) {
		if (ele_D == (OFDMSwingTable_92E[i] >> 22)) {
			OFDM_index[0] = (unsigned char)i;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("PathA 0xC80[31:22] = 0x%x, OFDM_index=%d\n", ele_D, OFDM_index[0]));
			break;
		}
	}

	//Query OFDM path B default setting
	if (rf_mimo_mode == MIMO_2T2R) {
		ele_D = PHY_QueryBBReg(priv, rOFDM0_XBTxIQImbalance, bMaskOFDM_D);
		for (i = 0; i < OFDM_TABLE_SIZE_92E; i++) {
			if (ele_D == (OFDMSwingTable_92E[i] >> 22)) {
				OFDM_index[1] = (unsigned char)i;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("PathB 0xC88[31:22] = 0x%x, OFDM_index=%d\n", ele_D, OFDM_index[1]));
				break;
			}
		}
	}

	/* calculate average thermal meter */
	{
		priv->pshare->ThermalValue_AVG_88XX[priv->pshare->ThermalValue_AVG_index_88XX] = ThermalValue;
		priv->pshare->ThermalValue_AVG_index_88XX++;
		if (priv->pshare->ThermalValue_AVG_index_88XX == AVG_THERMAL_NUM_88XX)
			priv->pshare->ThermalValue_AVG_index_88XX = 0;

		for (i = 0; i < AVG_THERMAL_NUM_88XX; i++) {
			if (priv->pshare->ThermalValue_AVG_88XX[i]) {
				ThermalValue_AVG += priv->pshare->ThermalValue_AVG_88XX[i];
				ThermalValue_AVG_count++;
			}
		}

		if (ThermalValue_AVG_count) {
			ThermalValue = (unsigned char)(ThermalValue_AVG / ThermalValue_AVG_count);
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("AVG Thermal Meter = 0x%x \n", ThermalValue));
		}
	}

	/* Initialize */
	if (!priv->pshare->ThermalValue) {
		priv->pshare->ThermalValue = priv->pmib->dot11RFEntry.ther;
		priv->pshare->ThermalValue_IQK = ThermalValue;
		priv->pshare->ThermalValue_LCK = ThermalValue;
	}

	if (ThermalValue != priv->pshare->ThermalValue) {
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("\n******** START POWER TRACKING ********\n")); 					
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", ThermalValue, priv->pshare->ThermalValue, priv->pmib->dot11RFEntry.ther)); 			

		delta = RTL_ABS(ThermalValue, priv->pmib->dot11RFEntry.ther);
		delta_IQK = RTL_ABS(ThermalValue, priv->pshare->ThermalValue_IQK);
		delta_LCK = RTL_ABS(ThermalValue, priv->pshare->ThermalValue_LCK);
		is_decrease = ((ThermalValue < priv->pmib->dot11RFEntry.ther) ? 1 : 0);
		
#ifdef _TRACKING_TABLE_FILE
		if (priv->pshare->rf_ft_var.pwr_track_file) {				
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("Diff: (%s)%d ==> get index from table : %d)\n", (is_decrease?"-":"+"), delta, get_tx_tracking_index(priv, channel, i, delta, is_decrease, 0)));
        
            	if (is_decrease) {					
                	for (i = 0; i < rf; i++) {
				OFDM_index[i] = priv->pshare->OFDM_index0[i] + get_tx_tracking_index(priv, channel, i, delta, is_decrease, 0);
				OFDM_index[i] = ((OFDM_index[i] > (OFDM_TABLE_SIZE_92E- 1)) ? (OFDM_TABLE_SIZE_92E - 1) : OFDM_index[i]);
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,(">>> decrese power ---> new OFDM_INDEX:%d (%d + %d)\n", OFDM_index[i], priv->pshare->OFDM_index0[i], get_tx_tracking_index(priv, channel, i, delta, is_decrease, 0)));
	                        CCK_index = priv->pshare->CCK_index0 + get_tx_tracking_index(priv, channel, i, delta, is_decrease, 1);                        
				CCK_index = ((CCK_index > (CCK_TABLE_SIZE_92E - 1)) ? (CCK_TABLE_SIZE_92E - 1) : CCK_index);				
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,(">>> Decrese power ---> new CCK_INDEX:%d (%d + %d)\n",  CCK_index, priv->pshare->CCK_index0, get_tx_tracking_index(priv, channel, i, delta, is_decrease, 1)));
			}
		} else {
			for (i = 0; i < rf; i++) {
				OFDM_index[i] = priv->pshare->OFDM_index0[i] - get_tx_tracking_index(priv, channel, i, delta, is_decrease, 0);
				OFDM_index[i] = ((OFDM_index[i] < OFDM_min_index) ?  OFDM_min_index : OFDM_index[i]);
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,(">>> Increse power ---> new OFDM_INDEX:%d (%d - %d)\n", OFDM_index[i], priv->pshare->OFDM_index0[i], get_tx_tracking_index(priv, channel, i, delta, is_decrease, 0)));
				CCK_index = priv->pshare->CCK_index0 - get_tx_tracking_index(priv, channel, i, delta, is_decrease, 1);						  
	                        CCK_index = ((CCK_index < 0 )? 0 : CCK_index);  
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,(">>> Increse power ---> new CCK_INDEX:%d (%d - %d)\n", CCK_index, priv->pshare->CCK_index0, get_tx_tracking_index(priv, channel, i, delta, is_decrease, 1)));
			}
		}
		}
#endif //CFG_TRACKING_TABLE_FILE

		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("OFDMSwingTable_92E[(unsigned int)OFDM_index[0]] = %x \n",OFDMSwingTable_92E[(unsigned int)OFDM_index[0]]));
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("OFDMSwingTable_92E[(unsigned int)OFDM_index[1]] = %x \n",OFDMSwingTable_92E[(unsigned int)OFDM_index[1]]));

		//Adujst OFDM Ant_A according to IQK result
		ele_D = (OFDMSwingTable_92E[(unsigned int)OFDM_index[0]] & 0xFFC00000) >> 22;
		X = priv->pshare->RegE94;
		Y = priv->pshare->RegE9C;

		if (X != 0) {
			if ((X & 0x00000200) != 0)
				X = X | 0xFFFFFC00;
			ele_A = ((X * ele_D) >> 8) & 0x000003FF;

			//new element C = element D x Y
			if ((Y & 0x00000200) != 0)
				Y = Y | 0xFFFFFC00;
			ele_C = ((Y * ele_D) >> 8) & 0x000003FF;

			//wirte new elements A, C, D to regC80 and regC94, element B is always 0
			value32 = (ele_D << 22) | ((ele_C & 0x3F) << 16) | ele_A;
			PHY_SetBBReg(priv, rOFDM0_XATxIQImbalance, bMaskDWord, value32);

			value32 = (ele_C&0x000003C0)>>6;
			PHY_SetBBReg(priv, rOFDM0_XCTxAFE, bMaskH4Bits, value32);
			
			value32 = ((X * ele_D)>>7)&0x01;
			PHY_SetBBReg(priv, rOFDM0_ECCAThreshold, BIT(24), value32);
		} else {
			PHY_SetBBReg(priv, rOFDM0_XATxIQImbalance, bMaskDWord, OFDMSwingTable_92E[(unsigned int)OFDM_index[0]]);
			PHY_SetBBReg(priv, rOFDM0_XCTxAFE, bMaskH4Bits, 0x00);
			PHY_SetBBReg(priv, rOFDM0_ECCAThreshold, BIT(24), 0x00);
		}

		set_CCK_swing_index(priv, CCK_index);

		if (rf == 2) {
			ele_D = (OFDMSwingTable_92E[(unsigned int)OFDM_index[1]] & 0xFFC00000) >> 22;
			X = priv->pshare->RegEB4;
			Y = priv->pshare->RegEBC;

			if (X != 0) {
				if ((X & 0x00000200) != 0)	//consider minus
					X = X | 0xFFFFFC00;
				ele_A = ((X * ele_D) >> 8) & 0x000003FF;

				//new element C = element D x Y
				if ((Y & 0x00000200) != 0)
					Y = Y | 0xFFFFFC00;
				ele_C = ((Y * ele_D) >> 8) & 0x00003FF;

				//wirte new elements A, C, D to regC88 and regC9C, element B is always 0
				value32 = (ele_D << 22) | ((ele_C & 0x3F) << 16) | ele_A;
				PHY_SetBBReg(priv, rOFDM0_XBTxIQImbalance, bMaskDWord, value32);
				
				value32 = (ele_C & 0x000003C0) >> 6;
				PHY_SetBBReg(priv, rOFDM0_XDTxAFE, bMaskH4Bits, value32);

				value32 = ((X * ele_D) >> 7) & 0x01;
				PHY_SetBBReg(priv, rOFDM0_ECCAThreshold, BIT(28), value32);
			} else {
				PHY_SetBBReg(priv, rOFDM0_XBTxIQImbalance, bMaskDWord, OFDMSwingTable_92E[(unsigned int)OFDM_index[1]]);
				PHY_SetBBReg(priv, rOFDM0_XDTxAFE, bMaskH4Bits, 0x00);
				PHY_SetBBReg(priv, rOFDM0_ECCAThreshold, BIT(28), 0x00);
			}

		}

		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("0xc80 = 0x%x \n", PHY_QueryBBReg(priv, rOFDM0_XATxIQImbalance, bMaskDWord)));
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("0xc88 = 0x%x \n", PHY_QueryBBReg(priv, rOFDM0_XBTxIQImbalance, bMaskDWord)));

		if (delta_IQK > 3) {
			priv->pshare->ThermalValue_IQK = ThermalValue;
#ifdef MP_TEST
			if (!(priv->pshare->rf_ft_var.mp_specific && (OPMODE & (WIFI_MP_CTX_BACKGROUND | WIFI_MP_CTX_PACKET))))
#endif	
				PHY_IQCalibrate_8192E(pDM_Odm,false);
		}

		if (delta_LCK > 8) {
			RTL_W8(0x522, 0xff);
			Reg0x18 = PHY_QueryRFReg(priv, RF_PATH_A, 0x18, bMask20Bits, 1);
			PHY_SetRFReg(priv, RF_PATH_A, 0xB4, BIT(14), 1);			
			PHY_SetRFReg(priv, RF_PATH_A, 0x18, BIT(15), 1);
			delay_ms(1);
			PHY_SetRFReg(priv, RF_PATH_A, 0xB4, BIT(14), 0);
			PHY_SetRFReg(priv, RF_PATH_A, 0x18, bMask20Bits, Reg0x18);		
			RTL_W8(0x522, 0x0);
			priv->pshare->ThermalValue_LCK = ThermalValue;
		}	
	}

	//update thermal meter value
	priv->pshare->ThermalValue = ThermalValue;
	for (i = 0 ; i < rf ; i++)
		priv->pshare->OFDM_index[i] = OFDM_index[i];
	priv->pshare->CCK_index = CCK_index;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,  ("\n******** END:%s() ********\n", __FUNCTION__));	
}
#endif

#if (RTL8814A_SUPPORT ==1)					
		
VOID
ODM_TXPowerTrackingCallback_ThermalMeter_JaguarSeries2(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN	PVOID		pDM_VOID
#else
	IN PADAPTER	Adapter
#endif
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte			ThermalValue = 0, delta, delta_LCK, delta_IQK, channel, is_increase;
	u1Byte			ThermalValue_AVG_count = 0, p = 0, i = 0;
	u4Byte			ThermalValue_AVG = 0, Reg0x18;
	u4Byte 			BBSwingReg[4] = {rA_TxScale_Jaguar,rB_TxScale_Jaguar,rC_TxScale_Jaguar2,rD_TxScale_Jaguar2};
	s4Byte			ele_D;
	u4Byte			BBswingIdx;
	prtl8192cd_priv	priv = pDM_Odm->priv;
	TXPWRTRACK_CFG 	c;
	BOOLEAN			bTSSIenable = FALSE;
	PODM_RF_CAL_T	pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);

	//4 1. The following TWO tables decide the final index of OFDM/CCK swing table.
	pu1Byte			deltaSwingTableIdx_TUP_A = NULL, deltaSwingTableIdx_TDOWN_A = NULL;
	pu1Byte			deltaSwingTableIdx_TUP_B = NULL, deltaSwingTableIdx_TDOWN_B = NULL;
	//for 8814 add by Yu Chen
	pu1Byte			deltaSwingTableIdx_TUP_C = NULL, deltaSwingTableIdx_TDOWN_C = NULL;
	pu1Byte			deltaSwingTableIdx_TUP_D = NULL, deltaSwingTableIdx_TDOWN_D = NULL;

#ifdef MP_TEST
	if ((OPMODE & WIFI_MP_STATE) || priv->pshare->rf_ft_var.mp_specific) {
		channel = priv->pshare->working_channel;
		if (priv->pshare->mp_txpwr_tracking == FALSE)
			return;
	} else
#endif
	{
		channel = (priv->pmib->dot11RFEntry.dot11channel);
	}

	ConfigureTxpowerTrack(pDM_Odm, &c);
	pRFCalibrateInfo->DefaultOfdmIndex = priv->pshare->OFDM_index0[ODM_RF_PATH_A];

	(*c.GetDeltaSwingTable)(pDM_Odm, (pu1Byte*)&deltaSwingTableIdx_TUP_A, (pu1Byte*)&deltaSwingTableIdx_TDOWN_A,
									  (pu1Byte*)&deltaSwingTableIdx_TUP_B, (pu1Byte*)&deltaSwingTableIdx_TDOWN_B);

	if(pDM_Odm->SupportICType & ODM_RTL8814A)	// for 8814 path C & D
	(*c.GetDeltaSwingTable8814only)(pDM_Odm, (pu1Byte*)&deltaSwingTableIdx_TUP_C, (pu1Byte*)&deltaSwingTableIdx_TDOWN_C,
									  (pu1Byte*)&deltaSwingTableIdx_TUP_D, (pu1Byte*)&deltaSwingTableIdx_TDOWN_D);
	
	ThermalValue = (u1Byte)ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, c.ThermalRegAddr, 0xfc00); //0x42: RF Reg[15:10] 88E
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("\nReadback Thermal Meter = 0x%x, pre thermal meter 0x%x, EEPROMthermalmeter 0x%x\n", ThermalValue, pDM_Odm->RFCalibrateInfo.ThermalValue, priv->pmib->dot11RFEntry.ther));

	/* Initialize */
	if (!pDM_Odm->RFCalibrateInfo.ThermalValue) {
		pDM_Odm->RFCalibrateInfo.ThermalValue = priv->pmib->dot11RFEntry.ther;
	}
	
	if (!pDM_Odm->RFCalibrateInfo.ThermalValue_LCK) {
		pDM_Odm->RFCalibrateInfo.ThermalValue_LCK = priv->pmib->dot11RFEntry.ther;
	}

	if (!pDM_Odm->RFCalibrateInfo.ThermalValue_IQK) {
		pDM_Odm->RFCalibrateInfo.ThermalValue_IQK = priv->pmib->dot11RFEntry.ther;
	}
	
	bTSSIenable = (BOOLEAN)ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, rRF_TxGainOffset, BIT7);	// check TSSI enable
	
	//4 Query OFDM BB swing default setting 	Bit[31:21]	
	for(p = ODM_RF_PATH_A ; p < c.RfPathCount ; p++)
	{
		ele_D = ODM_GetBBReg(pDM_Odm, BBSwingReg[p], 0xffe00000);	
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("0x%x:0x%x ([31:21] = 0x%x)\n", BBSwingReg[p], ODM_GetBBReg(pDM_Odm, BBSwingReg[p], bMaskDWord), ele_D));
		
		for (BBswingIdx = 0; BBswingIdx < TXSCALE_TABLE_SIZE; BBswingIdx++) {//4 
			if (ele_D == TxScalingTable_Jaguar[BBswingIdx]) {
				pDM_Odm->RFCalibrateInfo.OFDM_index[p] = (u1Byte)BBswingIdx;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
					("OFDM_index[%d]=%d\n",p, pDM_Odm->RFCalibrateInfo.OFDM_index[p]));				
				break;
			}
		}
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("KfreeOffset[%d]=%d\n",p, pRFCalibrateInfo->KfreeOffset[p]));
		
	}

	/* calculate average thermal meter */
	pDM_Odm->RFCalibrateInfo.ThermalValue_AVG[pDM_Odm->RFCalibrateInfo.ThermalValue_AVG_index] = ThermalValue;
	pDM_Odm->RFCalibrateInfo.ThermalValue_AVG_index++;
	if(pDM_Odm->RFCalibrateInfo.ThermalValue_AVG_index == c.AverageThermalNum)   //Average times =  c.AverageThermalNum
		pDM_Odm->RFCalibrateInfo.ThermalValue_AVG_index = 0;

	for(i = 0; i < c.AverageThermalNum; i++)
	{
		if(pDM_Odm->RFCalibrateInfo.ThermalValue_AVG[i])
		{
			ThermalValue_AVG += pDM_Odm->RFCalibrateInfo.ThermalValue_AVG[i];
			ThermalValue_AVG_count++;
		}
	}

	if(ThermalValue_AVG_count)               //Calculate Average ThermalValue after average enough times
	{
		ThermalValue = (u1Byte)(ThermalValue_AVG / ThermalValue_AVG_count);
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("AVG Thermal Meter = 0x%X, EEPROMthermalmeter = 0x%X\n", ThermalValue, priv->pmib->dot11RFEntry.ther));					
	}

	//4 Calculate delta, delta_LCK, delta_IQK.
	delta = RTL_ABS(ThermalValue, priv->pmib->dot11RFEntry.ther);	
	delta_LCK = RTL_ABS(ThermalValue, pDM_Odm->RFCalibrateInfo.ThermalValue_LCK);
	delta_IQK = RTL_ABS(ThermalValue, pDM_Odm->RFCalibrateInfo.ThermalValue_IQK);
	is_increase = ((ThermalValue < priv->pmib->dot11RFEntry.ther) ? 0 : 1);

	//4 if necessary, do LCK.
	if (!(pDM_Odm->SupportICType & ODM_RTL8821)) {
		if (delta_LCK > c.Threshold_IQK) {
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("delta_LCK(%d) >= Threshold_IQK(%d)\n", delta_LCK, c.Threshold_IQK));
			pDM_Odm->RFCalibrateInfo.ThermalValue_LCK = ThermalValue;
			if (c.PHY_LCCalibrate)
				(*c.PHY_LCCalibrate)(pDM_Odm);
		}
	}

	if (delta_IQK > c.Threshold_IQK) 
	{
		panic_printk("%s(%d)\n", __FUNCTION__, __LINE__);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("delta_IQK(%d) >= Threshold_IQK(%d)\n", delta_IQK, c.Threshold_IQK));
		pDM_Odm->RFCalibrateInfo.ThermalValue_IQK = ThermalValue;
		if(c.DoIQK)
			(*c.DoIQK)(pDM_Odm, TRUE, 0, 0);
	} 

	if(!priv->pmib->dot11RFEntry.ther)	/*Don't do power tracking since no calibrated thermal value*/
		return;
	
	 //4 Do Power Tracking

	 if(bTSSIenable == TRUE)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("**********Enter PURE TSSI MODE**********\n"));
		for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++)
			(*c.ODM_TxPwrTrackSetPwr)(pDM_Odm, TSSI_MODE, p, 0);
	}
	else if (ThermalValue != pDM_Odm->RFCalibrateInfo.ThermalValue)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("\n******** START POWER TRACKING ********\n")); 					
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", ThermalValue, pDM_Odm->RFCalibrateInfo.ThermalValue, priv->pmib->dot11RFEntry.ther)); 			
				
#ifdef _TRACKING_TABLE_FILE
		if (priv->pshare->rf_ft_var.pwr_track_file)
		{				
			if (is_increase)			// thermal is higher than base
			{
				for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++) 
				{
					switch(p)
					{
					case ODM_RF_PATH_B:
						ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("deltaSwingTableIdx_TUP_B[%d] = %d\n", delta, deltaSwingTableIdx_TUP_B[delta])); 						
						pRFCalibrateInfo->Absolute_OFDMSwingIdx[p] = deltaSwingTableIdx_TUP_B[delta];       // Record delta swing for mix mode power tracking
						ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is higher and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_B] = %d\n", pRFCalibrateInfo->Absolute_OFDMSwingIdx[p]));  
					break;

					case ODM_RF_PATH_C:
						ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("deltaSwingTableIdx_TUP_C[%d] = %d\n", delta, deltaSwingTableIdx_TUP_C[delta]));								
						pRFCalibrateInfo->Absolute_OFDMSwingIdx[p] = deltaSwingTableIdx_TUP_C[delta];       // Record delta swing for mix mode power tracking
						ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is higher and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_C] = %d\n", pRFCalibrateInfo->Absolute_OFDMSwingIdx[p]));  
					break;

					case ODM_RF_PATH_D:
						ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("deltaSwingTableIdx_TUP_D[%d] = %d\n", delta, deltaSwingTableIdx_TUP_D[delta]));							
						pRFCalibrateInfo->Absolute_OFDMSwingIdx[p] = deltaSwingTableIdx_TUP_D[delta];       // Record delta swing for mix mode power tracking
						ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is higher and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_D] = %d\n", pRFCalibrateInfo->Absolute_OFDMSwingIdx[p]));  
					break;

					default:
						ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, 
							("deltaSwingTableIdx_TUP_A[%d] = %d\n", delta, deltaSwingTableIdx_TUP_A[delta]));						
						pRFCalibrateInfo->Absolute_OFDMSwingIdx[p] = deltaSwingTableIdx_TUP_A[delta];        // Record delta swing for mix mode power tracking
						ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is higher and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_A] = %d\n", pRFCalibrateInfo->Absolute_OFDMSwingIdx[p]));  
					break;
					}		
				}
			}
			else					// thermal is lower than base
			{
				for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++) 
				{
					switch(p)
					{
					case ODM_RF_PATH_B:
						ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("deltaSwingTableIdx_TDOWN_B[%d] = %d\n", delta, deltaSwingTableIdx_TDOWN_B[delta]));  
						pRFCalibrateInfo->Absolute_OFDMSwingIdx[p] = -1 * deltaSwingTableIdx_TDOWN_B[delta];        // Record delta swing for mix mode power tracking
						ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is lower and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_B] = %d\n", pRFCalibrateInfo->Absolute_OFDMSwingIdx[p])); 
					break;

					case ODM_RF_PATH_C:
						ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("deltaSwingTableIdx_TDOWN_C[%d] = %d\n", delta, deltaSwingTableIdx_TDOWN_C[delta]));  
						pRFCalibrateInfo->Absolute_OFDMSwingIdx[p] = -1 * deltaSwingTableIdx_TDOWN_C[delta];        // Record delta swing for mix mode power tracking
						ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is lower and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_C] = %d\n", pRFCalibrateInfo->Absolute_OFDMSwingIdx[p]));   
					break;

					case ODM_RF_PATH_D:
						ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("deltaSwingTableIdx_TDOWN_D[%d] = %d\n", delta, deltaSwingTableIdx_TDOWN_D[delta]));  
						pRFCalibrateInfo->Absolute_OFDMSwingIdx[p] = -1 * deltaSwingTableIdx_TDOWN_D[delta];        // Record delta swing for mix mode power tracking
						ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is lower and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_D] = %d\n", pRFCalibrateInfo->Absolute_OFDMSwingIdx[p]));  
					break;

					default:
						ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("deltaSwingTableIdx_TDOWN_A[%d] = %d\n", delta, deltaSwingTableIdx_TDOWN_A[delta]));  
						pRFCalibrateInfo->Absolute_OFDMSwingIdx[p] = -1 * deltaSwingTableIdx_TDOWN_A[delta];        // Record delta swing for mix mode power tracking
						ODM_RT_TRACE(pDM_Odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is lower and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_A] = %d\n", pRFCalibrateInfo->Absolute_OFDMSwingIdx[p]));  
					break;
					}		
				}
			}
				
			if (is_increase)
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,(">>> increse power ---> \n"));
				for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++) 
				(*c.ODM_TxPwrTrackSetPwr)(pDM_Odm, MIX_MODE, p, 0);
			} 
			else
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,(">>> decrese power --->\n"));
				for (p = ODM_RF_PATH_A; p < c.RfPathCount; p++) 
				(*c.ODM_TxPwrTrackSetPwr)(pDM_Odm, MIX_MODE, p, 0);
			}
		}
#endif		

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("\n******** END:%s() ********\n", __FUNCTION__));
	//update thermal meter value
	pDM_Odm->RFCalibrateInfo.ThermalValue =  ThermalValue;

	}
}

#elif(ODM_IC_11AC_SERIES_SUPPORT)
VOID
ODM_TXPowerTrackingCallback_ThermalMeter_JaguarSeries(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN	PVOID		pDM_VOID
#else
	IN PADAPTER	Adapter
#endif
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	unsigned char			ThermalValue = 0, delta, delta_LCK, channel, is_decrease;
	unsigned char			ThermalValue_AVG_count = 0;
	unsigned int			ThermalValue_AVG = 0, Reg0x18;
	unsigned int 			BBSwingReg[4]={0xc1c,0xe1c,0x181c,0x1a1c};
	int 					ele_D, value32;
	char					OFDM_index[2], index;
	unsigned int			i = 0, j = 0, rf_path, max_rf_path =2 ,rf;
	prtl8192cd_priv		priv = pDM_Odm->priv;
	unsigned char			OFDM_min_index = 7; //OFDM BB Swing should be less than +2.5dB, which is required by Arthur and Mimic

#ifdef MP_TEST
	if ((OPMODE & WIFI_MP_STATE) || priv->pshare->rf_ft_var.mp_specific) {
		channel = priv->pshare->working_channel;
		if (priv->pshare->mp_txpwr_tracking == FALSE)
			return;
	} else
#endif
	{
		channel = (priv->pmib->dot11RFEntry.dot11channel);
	}

#if RTL8881A_SUPPORT
	if (pDM_Odm->SupportICType == ODM_RTL8881A) {
		max_rf_path = 1;
		if ((get_bonding_type_8881A() == BOND_8881AM ||get_bonding_type_8881A() == BOND_8881AN) 			
			&& priv->pshare->rf_ft_var.use_intpa8881A && (priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_2G))			
			OFDM_min_index = 6;		// intPA - upper bond set to +3 dB (base: -2 dB)ot11RFEntry.phyBandSelect == PHY_BAND_2G))
		else
			OFDM_min_index = 10;		//OFDM BB Swing should be less than +1dB, which is required by Arthur and Mimic
	}
#endif


	ThermalValue = (unsigned char)PHY_QueryRFReg(priv, RF_PATH_A, 0x42, 0xfc00, 1); //0x42: RF Reg[15:10] 88E
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", ThermalValue, priv->pshare->ThermalValue, priv->pmib->dot11RFEntry.ther));


	//4 Query OFDM BB swing default setting 	Bit[31:21]
	for(rf_path = 0 ; rf_path < max_rf_path ; rf_path++){
		ele_D = PHY_QueryBBReg(priv, BBSwingReg[rf_path], 0xffe00000);	
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("0x%x:0x%x ([31:21] = 0x%x)\n",BBSwingReg[rf_path], PHY_QueryBBReg(priv, BBSwingReg[rf_path], bMaskDWord),ele_D)); 			
		for (i = 0; i < OFDM_TABLE_SIZE_8812; i++) {//4 
			if (ele_D == OFDMSwingTable_8812[i]) {
				OFDM_index[rf_path] = (unsigned char)i;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("OFDM_index[%d]=%d\n",rf_path, OFDM_index[rf_path]));				
				break;
			}
		}
	}
#if 0	
	//Query OFDM path A default setting 	Bit[31:21]
	ele_D = PHY_QueryBBReg(priv, 0xc1c, 0xffe00000);	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("0xc1c:0x%x ([31:21] = 0x%x)\n", PHY_QueryBBReg(priv, 0xc1c, bMaskDWord),ele_D)); 			
	for (i = 0; i < OFDM_TABLE_SIZE_8812; i++) {//4 
		if (ele_D == OFDMSwingTable_8812[i]) {
			OFDM_index[0] = (unsigned char)i;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("OFDM_index[0]=%d\n", OFDM_index[0]));				
			break;
		}
	}
	//Query OFDM path B default setting
	if (rf == 2) {
		ele_D = PHY_QueryBBReg(priv, 0xe1c, 0xffe00000);		
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("0xe1c:0x%x ([32:21] = 0x%x)\n", PHY_QueryBBReg(priv, 0xe1c, bMaskDWord),ele_D)); 			
		for (i = 0; i < OFDM_TABLE_SIZE_8812; i++) {
			if (ele_D == OFDMSwingTable_8812[i]) {
				OFDM_index[1] = (unsigned char)i;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("OFDM_index[1]=%d\n", OFDM_index[1])); 			
				break;
			}
		}
	}
#endif
	/* Initialize */
	if (!priv->pshare->ThermalValue) {
		priv->pshare->ThermalValue = priv->pmib->dot11RFEntry.ther;
		priv->pshare->ThermalValue_LCK = ThermalValue;
	}

	/* calculate average thermal meter */
	{
		priv->pshare->ThermalValue_AVG_8812[priv->pshare->ThermalValue_AVG_index_8812] = ThermalValue;
		priv->pshare->ThermalValue_AVG_index_8812++;
		if (priv->pshare->ThermalValue_AVG_index_8812 == AVG_THERMAL_NUM_8812)
			priv->pshare->ThermalValue_AVG_index_8812 = 0;

		for (i = 0; i < AVG_THERMAL_NUM_8812; i++) {
			if (priv->pshare->ThermalValue_AVG_8812[i]) {
				ThermalValue_AVG += priv->pshare->ThermalValue_AVG_8812[i];
				ThermalValue_AVG_count++;
			}
		}

		if (ThermalValue_AVG_count) {
			ThermalValue = (unsigned char)(ThermalValue_AVG / ThermalValue_AVG_count);
			//printk("AVG Thermal Meter = 0x%x \n", ThermalValue);
		}
	}
	

	//4 If necessary,  do power tracking

	if(!priv->pmib->dot11RFEntry.ther) /*Don't do power tracking since no calibrated thermal value*/
		return;  

	if (ThermalValue != priv->pshare->ThermalValue) {
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("\n******** START POWER TRACKING ********\n")); 					
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", ThermalValue, priv->pshare->ThermalValue, priv->pmib->dot11RFEntry.ther)); 			
		delta = RTL_ABS(ThermalValue, priv->pmib->dot11RFEntry.ther);
		delta_LCK = RTL_ABS(ThermalValue, priv->pshare->ThermalValue_LCK);
		is_decrease = ((ThermalValue < priv->pmib->dot11RFEntry.ther) ? 1 : 0);
		//if (priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_5G) 
		{
#ifdef _TRACKING_TABLE_FILE
			if (priv->pshare->rf_ft_var.pwr_track_file) {				
				for (rf_path = 0; rf_path < max_rf_path; rf_path++) {
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("Diff: (%s)%d ==> get index from table : %d)\n", (is_decrease?"-":"+"), delta, get_tx_tracking_index(priv, channel, rf_path, delta, is_decrease, 0)));
					if (is_decrease) {
						OFDM_index[rf_path] = priv->pshare->OFDM_index0[rf_path] + get_tx_tracking_index(priv, channel, rf_path, delta, is_decrease, 0);
						OFDM_index[rf_path] = ((OFDM_index[rf_path] > (OFDM_TABLE_SIZE_8812 - 1)) ? (OFDM_TABLE_SIZE_8812 - 1) : OFDM_index[rf_path]);
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,(">>> decrese power ---> new OFDM_INDEX:%d (%d + %d)\n", OFDM_index[rf_path], priv->pshare->OFDM_index0[rf_path], get_tx_tracking_index(priv, channel, rf_path, delta, is_decrease, 0)));
#if 0// RTL8881A_SUPPORT
						if (pDM_Odm->SupportICType == ODM_RTL8881A){
							if(priv->pshare->rf_ft_var.pwrtrk_TxAGC_enable){
								if(priv->pshare->AddTxAGC){//TxAGC has been added
									AddTxPower88XX_AC(priv,0); 
									priv->pshare->AddTxAGC = 0;
									priv->pshare->AddTxAGC_index = 0;
								}
							}
						}
#endif				
					} else {

						OFDM_index[rf_path] = priv->pshare->OFDM_index0[rf_path] - get_tx_tracking_index(priv, channel, rf_path, delta, is_decrease, 0);
#if 0// RTL8881A_SUPPORT
						if(pDM_Odm->SupportICType == ODM_RTL8881A){ 
							if(priv->pshare->rf_ft_var.pwrtrk_TxAGC_enable){
								if(OFDM_index[i] < OFDM_min_index){
									priv->pshare->AddTxAGC_index = (OFDM_min_index - OFDM_index[i])/2;  // Calculate Remnant TxAGC Value,  2 index for 1 TxAGC 
									AddTxPower88XX_AC(priv,priv->pshare->AddTxAGC_index);
									priv->pshare->AddTxAGC = 1;     //AddTxAGC Flag = 1
									OFDM_index[i] = OFDM_min_index;
								}
								else{
									if(priv->pshare->AddTxAGC){// TxAGC been added
										priv->pshare->AddTxAGC = 0;
										priv->pshare->AddTxAGC_index = 0;
										AddTxPower88XX_AC(priv,0); //minus the added TPI
									}
								}
							}
						}
#else
						OFDM_index[rf_path] = ((OFDM_index[rf_path] < OFDM_min_index) ?  OFDM_min_index : OFDM_index[rf_path]);
#endif
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,(">>> increse power ---> new OFDM_INDEX:%d (%d - %d)\n", OFDM_index[rf_path], priv->pshare->OFDM_index0[rf_path], get_tx_tracking_index(priv, channel, rf_path, delta, is_decrease, 0)));
					}
				}
			}
#endif
			//4 Set new BB swing index
			for (rf_path = 0; rf_path < max_rf_path; rf_path++) {
				PHY_SetBBReg(priv, BBSwingReg[rf_path], 0xffe00000, OFDMSwingTable_8812[(unsigned int)OFDM_index[rf_path]]);
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("Readback 0x%x[31:21] = 0x%x, OFDM_index:%d\n",BBSwingReg[rf_path], PHY_QueryBBReg(priv, BBSwingReg[rf_path], 0xffe00000), OFDM_index[rf_path]));				
			}

		}
		if (delta_LCK > 8) {
			RTL_W8(0x522, 0xff);
			Reg0x18 = PHY_QueryRFReg(priv, RF_PATH_A, 0x18, bMask20Bits, 1);
			PHY_SetRFReg(priv, RF_PATH_A, 0xB4, BIT(14), 1);			
			PHY_SetRFReg(priv, RF_PATH_A, 0x18, BIT(15), 1);
            delay_ms(200); // frequency deviation
			PHY_SetRFReg(priv, RF_PATH_A, 0xB4, BIT(14), 0);
			PHY_SetRFReg(priv, RF_PATH_A, 0x18, bMask20Bits, Reg0x18);
			#ifdef CONFIG_RTL_8812_SUPPORT
			if (GET_CHIP_VER(priv)== VERSION_8812E)			
				UpdateBBRFVal8812(priv, priv->pmib->dot11RFEntry.dot11channel);	
			#endif
			RTL_W8(0x522, 0x0);
			priv->pshare->ThermalValue_LCK = ThermalValue;
		}	
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("\n******** END:%s() ********\n", __FUNCTION__));

		//update thermal meter value
		priv->pshare->ThermalValue = ThermalValue;
		for (rf_path = 0; rf_path < max_rf_path; rf_path++)
			priv->pshare->OFDM_index[rf_path] = OFDM_index[rf_path];
	}
}

#endif


VOID
ODM_TXPowerTrackingCallback_ThermalMeter(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN	PVOID		pDM_VOID
#else
	IN PADAPTER	Adapter
#endif
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PODM_RF_CAL_T	pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);
#if (RTL8814A_SUPPORT == 1)		//use this function to do power tracking after 8814 by YuChen
	if (pDM_Odm->SupportICType & ODM_RTL8814A) {
		ODM_TXPowerTrackingCallback_ThermalMeter_JaguarSeries2(pDM_Odm);
		return;
		}
#elif ODM_IC_11AC_SERIES_SUPPORT
	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES) {
		ODM_TXPowerTrackingCallback_ThermalMeter_JaguarSeries(pDM_Odm);
		return;
	}
#endif

#if (RTL8192E_SUPPORT == 1)
	if (pDM_Odm->SupportICType==ODM_RTL8192E) {
		ODM_TXPowerTrackingCallback_ThermalMeter_92E(pDM_Odm);
		return;
	}
#endif

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	//PMGNT_INFO      		pMgntInfo = &Adapter->MgntInfo;
#endif
	
	u1Byte			ThermalValue = 0, delta, delta_LCK, delta_IQK, offset;
	u1Byte			ThermalValue_AVG_count = 0;
	u4Byte			ThermalValue_AVG = 0;	
//	s4Byte			ele_A=0, ele_D, TempCCk, X, value32;
//	s4Byte			Y, ele_C=0;
//	s1Byte			OFDM_index[2], CCK_index=0, OFDM_index_old[2]={0,0}, CCK_index_old=0, index;
//	s1Byte			deltaPowerIndex = 0;
	u4Byte			i = 0;//, j = 0;
	BOOLEAN 		is2T = FALSE;
//	BOOLEAN 		bInteralPA = FALSE;

	u1Byte			OFDM_max_index = 34, rf = (is2T) ? 2 : 1; //OFDM BB Swing should be less than +3.0dB, which is required by Arthur
	u1Byte			Indexforchannel = 0;/*GetRightChnlPlaceforIQK(pHalData->CurrentChannel)*/
    enum            _POWER_DEC_INC { POWER_DEC, POWER_INC };
	#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PDM_ODM_T		pDM_Odm = &pHalData->odmpriv;
	#endif
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	#endif

	TXPWRTRACK_CFG 	c;


	//4 1. The following TWO tables decide the final index of OFDM/CCK swing table.
	s1Byte			deltaSwingTableIdx[2][index_mapping_NUM_88E] = { 
                        // {{Power decreasing(lower temperature)}, {Power increasing(higher temperature)}}
                        {0,0,2,3,4,4,5,6,7,7,8,9,10,10,11}, {0,0,1,2,3,4,4,4,4,5,7,8,9,9,10}
                    };	
	u1Byte			thermalThreshold[2][index_mapping_NUM_88E]={
                        // {{Power decreasing(lower temperature)}, {Power increasing(higher temperature)}}
					    {0,2,4,6,8,10,12,14,16,18,20,22,24,26,27}, {0,2,4,6,8,10,12,14,16,18,20,22,25,25,25}
                    };		

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	prtl8192cd_priv	priv = pDM_Odm->priv;
#endif	

	//4 2. Initilization ( 7 steps in total )

	ConfigureTxpowerTrack(pDM_Odm, &c);
	
	pDM_Odm->RFCalibrateInfo.TXPowerTrackingCallbackCnt++; //cosa add for debug
	pDM_Odm->RFCalibrateInfo.bTXPowerTrackingInit = TRUE;
    
#if (MP_DRIVER == 1)      
    pDM_Odm->RFCalibrateInfo.TxPowerTrackControl = pHalData->TxPowerTrackControl; // <Kordan> We should keep updating the control variable according to HalData.
    // <Kordan> RFCalibrateInfo.RegA24 will be initialized when ODM HW configuring, but MP configures with para files.
    pDM_Odm->RFCalibrateInfo.RegA24 = 0x090e1317; 
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_AP) && defined(MP_TEST)
	if ((OPMODE & WIFI_MP_STATE) || pDM_Odm->priv->pshare->rf_ft_var.mp_specific) {
		if(pDM_Odm->priv->pshare->mp_txpwr_tracking == FALSE)
			return;
	}
#endif
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("===>odm_TXPowerTrackingCallback_ThermalMeter_8188E, pDM_Odm->BbSwingIdxCckBase: %d, pDM_Odm->BbSwingIdxOfdmBase: %d \n", pRFCalibrateInfo->BbSwingIdxCckBase, pRFCalibrateInfo->BbSwingIdxOfdmBase));
/*
	if (!pDM_Odm->RFCalibrateInfo.TM_Trigger) {
		ODM_SetRFReg(pDM_Odm, RF_PATH_A, c.ThermalRegAddr, BIT17 | BIT16, 0x3);
		pDM_Odm->RFCalibrateInfo.TM_Trigger = 1;
		return;
	}
*/	
	ThermalValue = (u1Byte)ODM_GetRFReg(pDM_Odm, RF_PATH_A, c.ThermalRegAddr, 0xfc00);	//0x42: RF Reg[15:10] 88E
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	if( ! ThermalValue || ! pDM_Odm->RFCalibrateInfo.TxPowerTrackControl)
#else
	if( ! pDM_Odm->RFCalibrateInfo.TxPowerTrackControl)
#endif		
        return;

	//4 3. Initialize ThermalValues of RFCalibrateInfo
	
	if( ! pDM_Odm->RFCalibrateInfo.ThermalValue)
	{
		pDM_Odm->RFCalibrateInfo.ThermalValue_LCK = ThermalValue;				
		pDM_Odm->RFCalibrateInfo.ThermalValue_IQK = ThermalValue;										
	}			

	if(pDM_Odm->RFCalibrateInfo.bReloadtxpowerindex)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("reload ofdm index for band switch\n"));				
	}

	//4 4. Calculate average thermal meter
	
	pDM_Odm->RFCalibrateInfo.ThermalValue_AVG[pDM_Odm->RFCalibrateInfo.ThermalValue_AVG_index] = ThermalValue;
	pDM_Odm->RFCalibrateInfo.ThermalValue_AVG_index++;
	if(pDM_Odm->RFCalibrateInfo.ThermalValue_AVG_index == c.AverageThermalNum)
		pDM_Odm->RFCalibrateInfo.ThermalValue_AVG_index = 0;

	for(i = 0; i < c.AverageThermalNum; i++)
	{
		if(pDM_Odm->RFCalibrateInfo.ThermalValue_AVG[i])
		{
			ThermalValue_AVG += pDM_Odm->RFCalibrateInfo.ThermalValue_AVG[i];
			ThermalValue_AVG_count++;
		}
	}

	if(ThermalValue_AVG_count)
	{
		// Give the new thermo value a weighting
		ThermalValue_AVG += (ThermalValue*4);
		
		ThermalValue = (u1Byte)(ThermalValue_AVG / (ThermalValue_AVG_count+4));
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("AVG Thermal Meter = 0x%x \n", ThermalValue));					
	}
			
	//4 5. Calculate delta, delta_LCK, delta_IQK.
	
	delta 	  = (ThermalValue > pDM_Odm->RFCalibrateInfo.ThermalValue)?(ThermalValue - pDM_Odm->RFCalibrateInfo.ThermalValue):(pDM_Odm->RFCalibrateInfo.ThermalValue - ThermalValue);
	delta_LCK = (ThermalValue > pDM_Odm->RFCalibrateInfo.ThermalValue_LCK)?(ThermalValue - pDM_Odm->RFCalibrateInfo.ThermalValue_LCK):(pDM_Odm->RFCalibrateInfo.ThermalValue_LCK - ThermalValue);
	delta_IQK = (ThermalValue > pDM_Odm->RFCalibrateInfo.ThermalValue_IQK)?(ThermalValue - pDM_Odm->RFCalibrateInfo.ThermalValue_IQK):(pDM_Odm->RFCalibrateInfo.ThermalValue_IQK - ThermalValue);
		
	//4 6. If necessary, do LCK.	
	if (!(pDM_Odm->SupportICType & ODM_RTL8821)) {
	/*if((delta_LCK > pHalData->Delta_LCK) && (pHalData->Delta_LCK != 0))*/
		if (delta_LCK >= c.Threshold_IQK) { 
			/*Delta temperature is equal to or larger than 20 centigrade.*/
			pDM_Odm->RFCalibrateInfo.ThermalValue_LCK = ThermalValue;
			(*c.PHY_LCCalibrate)(pDM_Odm);
		}
	}

	//3 7. If necessary, move the index of swing table to adjust Tx power.	
	
	if (delta > 0 && pDM_Odm->RFCalibrateInfo.TxPowerTrackControl)
	{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))			
	    delta = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);		
#else
	    delta = (ThermalValue > pDM_Odm->priv->pmib->dot11RFEntry.ther)?(ThermalValue - pDM_Odm->priv->pmib->dot11RFEntry.ther):(pDM_Odm->priv->pmib->dot11RFEntry.ther - ThermalValue);		
#endif


		//4 7.1 The Final Power Index = BaseIndex + PowerIndexOffset
		
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))				
		if(ThermalValue > pHalData->EEPROMThermalMeter) {
#else
		if(ThermalValue > pDM_Odm->priv->pmib->dot11RFEntry.ther) {
#endif
			CALCULATE_SWINGTALBE_OFFSET(offset, POWER_INC, index_mapping_NUM_88E, delta);
			pDM_Odm->RFCalibrateInfo.DeltaPowerIndexLast = pDM_Odm->RFCalibrateInfo.DeltaPowerIndex;
			pDM_Odm->RFCalibrateInfo.DeltaPowerIndex =  deltaSwingTableIdx[POWER_INC][offset];

        } else {
        
			CALCULATE_SWINGTALBE_OFFSET(offset, POWER_DEC, index_mapping_NUM_88E, delta);
			pDM_Odm->RFCalibrateInfo.DeltaPowerIndexLast = pDM_Odm->RFCalibrateInfo.DeltaPowerIndex;
			pDM_Odm->RFCalibrateInfo.DeltaPowerIndex = (-1)*deltaSwingTableIdx[POWER_DEC][offset];
        }
		
		if (pDM_Odm->RFCalibrateInfo.DeltaPowerIndex == pDM_Odm->RFCalibrateInfo.DeltaPowerIndexLast)
			pDM_Odm->RFCalibrateInfo.PowerIndexOffset = 0;
		else
			pDM_Odm->RFCalibrateInfo.PowerIndexOffset = pDM_Odm->RFCalibrateInfo.DeltaPowerIndex - pDM_Odm->RFCalibrateInfo.DeltaPowerIndexLast;
		
	    for(i = 0; i < rf; i++) 		
	    	pDM_Odm->RFCalibrateInfo.OFDM_index[i] = pRFCalibrateInfo->BbSwingIdxOfdmBase + pDM_Odm->RFCalibrateInfo.PowerIndexOffset;
		pDM_Odm->RFCalibrateInfo.CCK_index = pRFCalibrateInfo->BbSwingIdxCckBase + pDM_Odm->RFCalibrateInfo.PowerIndexOffset;

		pRFCalibrateInfo->BbSwingIdxCck = pDM_Odm->RFCalibrateInfo.CCK_index;	
		pRFCalibrateInfo->BbSwingIdxOfdm[RF_PATH_A] = pDM_Odm->RFCalibrateInfo.OFDM_index[RF_PATH_A];	

		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("The 'CCK' final index(%d) = BaseIndex(%d) + PowerIndexOffset(%d)\n", pRFCalibrateInfo->BbSwingIdxCck, pRFCalibrateInfo->BbSwingIdxCckBase, pDM_Odm->RFCalibrateInfo.PowerIndexOffset));
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("The 'OFDM' final index(%d) = BaseIndex(%d) + PowerIndexOffset(%d)\n", pRFCalibrateInfo->BbSwingIdxOfdm[RF_PATH_A], pRFCalibrateInfo->BbSwingIdxOfdmBase, pDM_Odm->RFCalibrateInfo.PowerIndexOffset));

		//4 7.1 Handle boundary conditions of index.
		
		
		for(i = 0; i < rf; i++)
		{
			if(pDM_Odm->RFCalibrateInfo.OFDM_index[i] > OFDM_max_index)
			{
				pDM_Odm->RFCalibrateInfo.OFDM_index[i] = OFDM_max_index;
			}
			else if (pDM_Odm->RFCalibrateInfo.OFDM_index[i] < 0)
			{
				pDM_Odm->RFCalibrateInfo.OFDM_index[i] = 0;
			}
		}

		if(pDM_Odm->RFCalibrateInfo.CCK_index > c.SwingTableSize_CCK-1)
			pDM_Odm->RFCalibrateInfo.CCK_index = c.SwingTableSize_CCK-1;
		else if (pDM_Odm->RFCalibrateInfo.CCK_index < 0)
			pDM_Odm->RFCalibrateInfo.CCK_index = 0;
	}
	else
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("The thermal meter is unchanged or TxPowerTracking OFF: ThermalValue: %d , pDM_Odm->RFCalibrateInfo.ThermalValue: %d)\n", ThermalValue, pDM_Odm->RFCalibrateInfo.ThermalValue));
		pDM_Odm->RFCalibrateInfo.PowerIndexOffset = 0;
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("TxPowerTracking: [CCK] Swing Current Index: %d, Swing Base Index: %d\n", pDM_Odm->RFCalibrateInfo.CCK_index, pRFCalibrateInfo->BbSwingIdxCckBase));
				
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("TxPowerTracking: [OFDM] Swing Current Index: %d, Swing Base Index: %d\n", pDM_Odm->RFCalibrateInfo.OFDM_index[RF_PATH_A], pRFCalibrateInfo->BbSwingIdxOfdmBase));
	
	if (pDM_Odm->RFCalibrateInfo.PowerIndexOffset != 0 && pDM_Odm->RFCalibrateInfo.TxPowerTrackControl)
	{
		//4 7.2 Configure the Swing Table to adjust Tx Power.
		
			pDM_Odm->RFCalibrateInfo.bTxPowerChanged = TRUE; // Always TRUE after Tx Power is adjusted by power tracking.			
			//
			// 2012/04/23 MH According to Luke's suggestion, we can not write BB digital
			// to increase TX power. Otherwise, EVM will be bad.
			//
			// 2012/04/25 MH Add for tx power tracking to set tx power in tx agc for 88E.
			if (ThermalValue > pDM_Odm->RFCalibrateInfo.ThermalValue)
			{
				//ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				//	("Temperature Increasing: delta_pi: %d , delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n", 
				//	pDM_Odm->RFCalibrateInfo.PowerIndexOffset, delta, ThermalValue, pHalData->EEPROMThermalMeter, pDM_Odm->RFCalibrateInfo.ThermalValue));	
			}
			else if (ThermalValue < pDM_Odm->RFCalibrateInfo.ThermalValue)// Low temperature
			{
				//ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				//	("Temperature Decreasing: delta_pi: %d , delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n",
				//		pDM_Odm->RFCalibrateInfo.PowerIndexOffset, delta, ThermalValue, pHalData->EEPROMThermalMeter, pDM_Odm->RFCalibrateInfo.ThermalValue));				
			}
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			if (ThermalValue > pHalData->EEPROMThermalMeter)
#else
			if (ThermalValue > pDM_Odm->priv->pmib->dot11RFEntry.ther)
#endif
			{
//				ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("Temperature(%d) hugher than PG value(%d), increases the power by TxAGC\n", ThermalValue, pHalData->EEPROMThermalMeter));
				(*c.ODM_TxPwrTrackSetPwr)(pDM_Odm, TXAGC, 0, 0);							
			}
			else
			{
	//			ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("Temperature(%d) lower than PG value(%d), increases the power by TxAGC\n", ThermalValue, pHalData->EEPROMThermalMeter));
				(*c.ODM_TxPwrTrackSetPwr)(pDM_Odm, BBSWING, RF_PATH_A, Indexforchannel);	
				if(is2T)
					(*c.ODM_TxPwrTrackSetPwr)(pDM_Odm, BBSWING, RF_PATH_B, Indexforchannel);				
			}
			
			pRFCalibrateInfo->BbSwingIdxCckBase = pRFCalibrateInfo->BbSwingIdxCck;
			pRFCalibrateInfo->BbSwingIdxOfdmBase = pRFCalibrateInfo->BbSwingIdxOfdm[RF_PATH_A];
			pDM_Odm->RFCalibrateInfo.ThermalValue = ThermalValue;

	}
		
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	// if((delta_IQK > pHalData->Delta_IQK) && (pHalData->Delta_IQK != 0))
	if ((delta_IQK >= 8)) // Delta temperature is equal to or larger than 20 centigrade.
		(*c.DoIQK)(pDM_Odm, delta_IQK, ThermalValue, 8);
#endif		
			
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("<===dm_TXPowerTrackingCallback_ThermalMeter_8188E\n"));
	
	pDM_Odm->RFCalibrateInfo.TXPowercount = 0;
}

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)


VOID
phy_PathAStandBy(
	IN	PADAPTER	pAdapter
	)
{
	RTPRINT(FINIT, INIT_IQK, ("Path-A standby mode!\n"));

	PHY_SetBBReg(pAdapter, rFPGA0_IQK, 0xffffff00, 0x0);
	PHY_SetBBReg(pAdapter, 0x840, bMaskDWord, 0x00010000);
	PHY_SetBBReg(pAdapter, rFPGA0_IQK, 0xffffff00, 0x808000);
}

//1 7.	IQK
//#define MAX_TOLERANCE		5
//#define IQK_DELAY_TIME		1		//ms

u1Byte			//bit0 = 1 => Tx OK, bit1 = 1 => Rx OK
phy_PathA_IQK_8192C(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		configPathB
	)
{

	u4Byte regEAC, regE94, regE9C, regEA4;
	u1Byte result = 0x00;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	RTPRINT(FINIT, INIT_IQK, ("Path A IQK!\n"));

	//path-A IQK setting
	RTPRINT(FINIT, INIT_IQK, ("Path-A IQK setting!\n"));
	if(pAdapter->interfaceIndex == 0)
	{
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x10008c1f);
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x10008c1f);
	}
	else
	{
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x10008c22);
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x10008c22);	
	}

	PHY_SetBBReg(pAdapter, rTx_IQK_PI_A, bMaskDWord, 0x82140102);

	PHY_SetBBReg(pAdapter, rRx_IQK_PI_A, bMaskDWord, configPathB ? 0x28160202 : 
		IS_81xxC_VENDOR_UMC_B_CUT(pHalData->VersionID)?0x28160202:0x28160502);

	//path-B IQK setting
	if(configPathB)
	{
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_B, bMaskDWord, 0x10008c22);
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_B, bMaskDWord, 0x10008c22);
		PHY_SetBBReg(pAdapter, rTx_IQK_PI_B, bMaskDWord, 0x82140102);
		PHY_SetBBReg(pAdapter, rRx_IQK_PI_B, bMaskDWord, 0x28160202);
	}

	//LO calibration setting
	RTPRINT(FINIT, INIT_IQK, ("LO calibration setting!\n"));
	PHY_SetBBReg(pAdapter, rIQK_AGC_Rsp, bMaskDWord, 0x001028d1);

	//One shot, path A LOK & IQK
	RTPRINT(FINIT, INIT_IQK, ("One shot, path A LOK & IQK!\n"));
	PHY_SetBBReg(pAdapter, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	PHY_SetBBReg(pAdapter, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);
	
	// delay x ms
	RTPRINT(FINIT, INIT_IQK, ("Delay %d ms for One shot, path A LOK & IQK.\n", IQK_DELAY_TIME));
	PlatformStallExecution(IQK_DELAY_TIME*1000);

	// Check failed
	regEAC = PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, bMaskDWord);
	RTPRINT(FINIT, INIT_IQK, ("0xeac = 0x%x\n", regEAC));
	regE94 = PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_A, bMaskDWord);
	RTPRINT(FINIT, INIT_IQK, ("0xe94 = 0x%x\n", regE94));
	regE9C= PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_A, bMaskDWord);
	RTPRINT(FINIT, INIT_IQK, ("0xe9c = 0x%x\n", regE9C));
	regEA4= PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_A_2, bMaskDWord);
	RTPRINT(FINIT, INIT_IQK, ("0xea4 = 0x%x\n", regEA4));

	if(!(regEAC & BIT28) &&		
		(((regE94 & 0x03FF0000)>>16) != 0x142) &&
		(((regE9C & 0x03FF0000)>>16) != 0x42) )
		result |= 0x01;
	else							//if Tx not OK, ignore Rx
		return result;

	if(!(regEAC & BIT27) &&		//if Tx is OK, check whether Rx is OK
		(((regEA4 & 0x03FF0000)>>16) != 0x132) &&
		(((regEAC & 0x03FF0000)>>16) != 0x36))
		result |= 0x02;
	else
		RTPRINT(FINIT, INIT_IQK, ("Path A Rx IQK fail!!\n"));
	
	return result;


}

u1Byte				//bit0 = 1 => Tx OK, bit1 = 1 => Rx OK
phy_PathB_IQK_8192C(
	IN	PADAPTER	pAdapter
	)
{
	u4Byte regEAC, regEB4, regEBC, regEC4, regECC;
	u1Byte	result = 0x00;
	RTPRINT(FINIT, INIT_IQK, ("Path B IQK!\n"));

	//One shot, path B LOK & IQK
	RTPRINT(FINIT, INIT_IQK, ("One shot, path A LOK & IQK!\n"));
	PHY_SetBBReg(pAdapter, rIQK_AGC_Cont, bMaskDWord, 0x00000002);
	PHY_SetBBReg(pAdapter, rIQK_AGC_Cont, bMaskDWord, 0x00000000);

	// delay x ms
	RTPRINT(FINIT, INIT_IQK, ("Delay %d ms for One shot, path B LOK & IQK.\n", IQK_DELAY_TIME));
	PlatformStallExecution(IQK_DELAY_TIME*1000);

	// Check failed
	regEAC = PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, bMaskDWord);
	RTPRINT(FINIT, INIT_IQK, ("0xeac = 0x%x\n", regEAC));
	regEB4 = PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_B, bMaskDWord);
	RTPRINT(FINIT, INIT_IQK, ("0xeb4 = 0x%x\n", regEB4));
	regEBC= PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_B, bMaskDWord);
	RTPRINT(FINIT, INIT_IQK, ("0xebc = 0x%x\n", regEBC));
	regEC4= PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_B_2, bMaskDWord);
	RTPRINT(FINIT, INIT_IQK, ("0xec4 = 0x%x\n", regEC4));
	regECC= PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_B_2, bMaskDWord);
	RTPRINT(FINIT, INIT_IQK, ("0xecc = 0x%x\n", regECC));

	if(!(regEAC & BIT31) &&
		(((regEB4 & 0x03FF0000)>>16) != 0x142) &&
		(((regEBC & 0x03FF0000)>>16) != 0x42))
		result |= 0x01;
	else
		return result;

	if(!(regEAC & BIT30) &&
		(((regEC4 & 0x03FF0000)>>16) != 0x132) &&
		(((regECC & 0x03FF0000)>>16) != 0x36))
		result |= 0x02;
	else
		RTPRINT(FINIT, INIT_IQK, ("Path B Rx IQK fail!!\n"));
	

	return result;

}

VOID
phy_PathAFillIQKMatrix(
	IN	PADAPTER	pAdapter,
	IN  BOOLEAN    	bIQKOK,
	IN	s4Byte		result[][8],
	IN	u1Byte		final_candidate,
	IN  BOOLEAN		bTxOnly
	)
{
	u4Byte	Oldval_0, X, TX0_A, reg;
	s4Byte	Y, TX0_C;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);	
	
	RTPRINT(FINIT, INIT_IQK, ("Path A IQ Calibration %s !\n",(bIQKOK)?"Success":"Failed"));

	if(final_candidate == 0xFF)
		return;

	else if(bIQKOK)
	{
		Oldval_0 = (PHY_QueryBBReg(pAdapter, rOFDM0_XATxIQImbalance, bMaskDWord) >> 22) & 0x3FF;

		X = result[final_candidate][0];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;				
		TX0_A = (X * Oldval_0) >> 8;
		RTPRINT(FINIT, INIT_IQK, ("X = 0x%x, TX0_A = 0x%x, Oldval_0 0x%x\n", X, TX0_A, Oldval_0));
		PHY_SetBBReg(pAdapter, rOFDM0_XATxIQImbalance, 0x3FF, TX0_A);
		PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT(31), ((X * Oldval_0>>7) & 0x1));
     
		Y = result[final_candidate][1];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;		

		//path B IQK result + 3
		if(pAdapter->interfaceIndex == 1 && pHalData->CurrentBandType == BAND_ON_5G)
			Y += 3;
		
		TX0_C = (Y * Oldval_0) >> 8;
		RTPRINT(FINIT, INIT_IQK, ("Y = 0x%x, TX = 0x%x\n", Y, TX0_C));
		PHY_SetBBReg(pAdapter, rOFDM0_XCTxAFE, 0xF0000000, ((TX0_C&0x3C0)>>6));
		PHY_SetBBReg(pAdapter, rOFDM0_XATxIQImbalance, 0x003F0000, (TX0_C&0x3F));
		PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT(29), ((Y * Oldval_0>>7) & 0x1));

		if(bTxOnly)
		{
			RTPRINT(FINIT, INIT_IQK, ("phy_PathAFillIQKMatrix only Tx OK\n"));		
			return;
		}

		reg = result[final_candidate][2];
		PHY_SetBBReg(pAdapter, rOFDM0_XARxIQImbalance, 0x3FF, reg);
	
		reg = result[final_candidate][3] & 0x3F;
		PHY_SetBBReg(pAdapter, rOFDM0_XARxIQImbalance, 0xFC00, reg);

		reg = (result[final_candidate][3] >> 6) & 0xF;
		PHY_SetBBReg(pAdapter, rOFDM0_RxIQExtAnta, 0xF0000000, reg);
	}
}

VOID
phy_PathBFillIQKMatrix(
	IN	PADAPTER	pAdapter,
	IN  BOOLEAN   	bIQKOK,
	IN	s4Byte		result[][8],
	IN	u1Byte		final_candidate,
	IN	BOOLEAN		bTxOnly			//do Tx only
	)
{
	u4Byte	Oldval_1, X, TX1_A, reg;
	s4Byte	Y, TX1_C;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);	
	
	RTPRINT(FINIT, INIT_IQK, ("Path B IQ Calibration %s !\n",(bIQKOK)?"Success":"Failed"));

	if(final_candidate == 0xFF)
		return;

	else if(bIQKOK)
	{
		Oldval_1 = (PHY_QueryBBReg(pAdapter, rOFDM0_XBTxIQImbalance, bMaskDWord) >> 22) & 0x3FF;

		X = result[final_candidate][4];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;		
		TX1_A = (X * Oldval_1) >> 8;
		RTPRINT(FINIT, INIT_IQK, ("X = 0x%x, TX1_A = 0x%x\n", X, TX1_A));
		PHY_SetBBReg(pAdapter, rOFDM0_XBTxIQImbalance, 0x3FF, TX1_A);
		PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT(27), ((X * Oldval_1>>7) & 0x1));

		Y = result[final_candidate][5];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;		
		if(pHalData->CurrentBandType == BAND_ON_5G)		
			Y += 3;		//temp modify for preformance
		TX1_C = (Y * Oldval_1) >> 8;
		RTPRINT(FINIT, INIT_IQK, ("Y = 0x%x, TX1_C = 0x%x\n", Y, TX1_C));
		PHY_SetBBReg(pAdapter, rOFDM0_XDTxAFE, 0xF0000000, ((TX1_C&0x3C0)>>6));
		PHY_SetBBReg(pAdapter, rOFDM0_XBTxIQImbalance, 0x003F0000, (TX1_C&0x3F));
		PHY_SetBBReg(pAdapter, rOFDM0_ECCAThreshold, BIT(25), ((Y * Oldval_1>>7) & 0x1));

		if(bTxOnly)
			return;

		reg = result[final_candidate][6];
		PHY_SetBBReg(pAdapter, rOFDM0_XBRxIQImbalance, 0x3FF, reg);
	
		reg = result[final_candidate][7] & 0x3F;
		PHY_SetBBReg(pAdapter, rOFDM0_XBRxIQImbalance, 0xFC00, reg);

		reg = (result[final_candidate][7] >> 6) & 0xF;
		PHY_SetBBReg(pAdapter, rOFDM0_AGCRSSITable, 0x0000F000, reg);
	}
}


BOOLEAN							
phy_SimularityCompare_92C(
	IN	PADAPTER	pAdapter,
	IN	s4Byte 		result[][8],
	IN	u1Byte		 c1,
	IN	u1Byte		 c2
	)
{
	u4Byte		i, j, diff, SimularityBitMap, bound = 0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);	
	u1Byte		final_candidate[2] = {0xFF, 0xFF};	//for path A and path B
	BOOLEAN		bResult = TRUE, is2T = IS_92C_SERIAL( pHalData->VersionID);
	
	if(is2T)
		bound = 8;
	else
		bound = 4;

	SimularityBitMap = 0;
	
	for( i = 0; i < bound; i++ )
	{
		diff = (result[c1][i] > result[c2][i]) ? (result[c1][i] - result[c2][i]) : (result[c2][i] - result[c1][i]);
		if (diff > MAX_TOLERANCE)
		{
			if((i == 2 || i == 6) && !SimularityBitMap)
			{
				if(result[c1][i]+result[c1][i+1] == 0)
					final_candidate[(i/4)] = c2;
				else if (result[c2][i]+result[c2][i+1] == 0)
					final_candidate[(i/4)] = c1;
				else
					SimularityBitMap = SimularityBitMap|(1<<i);					
			}
			else
				SimularityBitMap = SimularityBitMap|(1<<i);
		}
	}
	
	if ( SimularityBitMap == 0)
	{
		for( i = 0; i < (bound/4); i++ )
		{
			if(final_candidate[i] != 0xFF)
			{
				for( j = i*4; j < (i+1)*4-2; j++)
					result[3][j] = result[final_candidate[i]][j];
				bResult = FALSE;
			}
		}
		return bResult;
	}
	else if (!(SimularityBitMap & 0x0F))			//path A OK
	{
		for(i = 0; i < 4; i++)
			result[3][i] = result[c1][i];
		return FALSE;
	}
	else if (!(SimularityBitMap & 0xF0) && is2T)	//path B OK
	{
		for(i = 4; i < 8; i++)
			result[3][i] = result[c1][i];
		return FALSE;
	}	
	else		
		return FALSE;
	
}

/*
return FALSE => do IQK again
*/
BOOLEAN							
phy_SimularityCompare(
	IN	PADAPTER	pAdapter,
	IN	s4Byte 		result[][8],
	IN	u1Byte		 c1,
	IN	u1Byte		 c2
	)
{	
	return phy_SimularityCompare_92C(pAdapter, result, c1, c2);	

}

VOID	
phy_IQCalibrate_8192C(
	IN	PADAPTER	pAdapter,
	IN	s4Byte 		result[][8],
	IN	u1Byte		t,
	IN	BOOLEAN		is2T
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u4Byte			i;
	u1Byte			PathAOK, PathBOK;
	u4Byte			ADDA_REG[IQK_ADDA_REG_NUM] = {	
						rFPGA0_XCD_SwitchControl, 	rBlue_Tooth, 	
						rRx_Wait_CCA, 		rTx_CCK_RFON,
						rTx_CCK_BBON, 	rTx_OFDM_RFON, 	
						rTx_OFDM_BBON, 	rTx_To_Rx,
						rTx_To_Tx, 		rRx_CCK, 	
						rRx_OFDM, 		rRx_Wait_RIFS,
						rRx_TO_Rx, 		rStandby, 	
						rSleep, 			rPMPD_ANAEN };
	u4Byte			IQK_MAC_REG[IQK_MAC_REG_NUM] = {
						REG_TXPAUSE, 		REG_BCN_CTRL,	
						REG_BCN_CTRL_1,	REG_GPIO_MUXCFG};
					
	//since 92C & 92D have the different define in IQK_BB_REG	
	u4Byte	IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
							rOFDM0_TRxPathEnable, 		rOFDM0_TRMuxPar,	
							rFPGA0_XCD_RFInterfaceSW,	rConfig_AntA,	rConfig_AntB,
							rFPGA0_XAB_RFInterfaceSW,	rFPGA0_XA_RFInterfaceOE,	
							rFPGA0_XB_RFInterfaceOE,	/*rFPGA0_RFMOD*/ rCCK0_AFESetting	
							};	

	u4Byte	IQK_BB_REG_92D[IQK_BB_REG_NUM_92D] = {	//for normal
							rFPGA0_XAB_RFInterfaceSW,	rFPGA0_XA_RFInterfaceOE,	
							rFPGA0_XB_RFInterfaceOE,	rOFDM0_TRMuxPar,
							rFPGA0_XCD_RFInterfaceSW,	rOFDM0_TRxPathEnable,	
							/*rFPGA0_RFMOD*/ rCCK0_AFESetting,			rFPGA0_AnalogParameter4,
							rOFDM0_XAAGCCore1,		rOFDM0_XBAGCCore1						
						};		
#if MP_DRIVER
	const u4Byte	retryCount = 9;
#else
	const u4Byte	retryCount = 2;
#endif
	//Neil Chen--2011--05--19--
       //3 Path Div	
	u1Byte                 rfPathSwitch=0x0;

	// Note: IQ calibration must be performed after loading 
	// 		PHY_REG.txt , and radio_a, radio_b.txt	
	
	u4Byte bbvalue;

	if(t==0)
	{
	 	 //bbvalue = PHY_QueryBBReg(pAdapter, rFPGA0_RFMOD, bMaskDWord);
		//	RTPRINT(FINIT, INIT_IQK, ("phy_IQCalibrate_8192C()==>0x%08x\n",bbvalue));

			RTPRINT(FINIT, INIT_IQK, ("IQ Calibration for %s\n", (is2T ? "2T2R" : "1T1R")));
	
	 	// Save ADDA parameters, turn Path A ADDA on
	 	phy_SaveADDARegisters(pAdapter, ADDA_REG, pHalData->ADDA_backup, IQK_ADDA_REG_NUM);
		phy_SaveMACRegisters(pAdapter, IQK_MAC_REG, pHalData->IQK_MAC_backup);
		phy_SaveADDARegisters(pAdapter, IQK_BB_REG_92C, pHalData->IQK_BB_backup, IQK_BB_REG_NUM);
	}
	
 	phy_PathADDAOn(pAdapter, ADDA_REG, TRUE, is2T);
	
	if(t==0)
	{
		pHalData->bRfPiEnable = (u1Byte)PHY_QueryBBReg(pAdapter, rFPGA0_XA_HSSIParameter1, BIT(8));
	}
	
	if(!pHalData->bRfPiEnable){
		// Switch BB to PI mode to do IQ Calibration.
		phy_PIModeSwitch(pAdapter, TRUE);
	}
	
	//MAC settings
	phy_MACSettingCalibration(pAdapter, IQK_MAC_REG, pHalData->IQK_MAC_backup);
	
	//PHY_SetBBReg(pAdapter, rFPGA0_RFMOD, BIT24, 0x00);		
	PHY_SetBBReg(pAdapter, rCCK0_AFESetting, bMaskDWord, (0x0f000000 | (PHY_QueryBBReg(pAdapter, rCCK0_AFESetting, bMaskDWord))) );
	PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable, bMaskDWord, 0x03a05600);
	PHY_SetBBReg(pAdapter, rOFDM0_TRMuxPar, bMaskDWord, 0x000800e4);
	PHY_SetBBReg(pAdapter, rFPGA0_XCD_RFInterfaceSW, bMaskDWord, 0x22204000);
	{
		PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT10, 0x01);
		PHY_SetBBReg(pAdapter, rFPGA0_XAB_RFInterfaceSW, BIT26, 0x01);	
		PHY_SetBBReg(pAdapter, rFPGA0_XA_RFInterfaceOE, BIT10, 0x00);
		PHY_SetBBReg(pAdapter, rFPGA0_XB_RFInterfaceOE, BIT10, 0x00);	
	}

	if(is2T)
	{
		PHY_SetBBReg(pAdapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x00010000);
		PHY_SetBBReg(pAdapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x00010000);
	}

	{
		//Page B init
		PHY_SetBBReg(pAdapter, rConfig_AntA, bMaskDWord, 0x00080000);
		
		if(is2T)
		{
			PHY_SetBBReg(pAdapter, rConfig_AntB, bMaskDWord, 0x00080000);
		}
	}
	// IQ calibration setting
	RTPRINT(FINIT, INIT_IQK, ("IQK setting!\n"));		
	PHY_SetBBReg(pAdapter, rFPGA0_IQK, 0xffffff00, 0x808000);
	PHY_SetBBReg(pAdapter, rTx_IQK, bMaskDWord, 0x01007c00);
	PHY_SetBBReg(pAdapter, rRx_IQK, bMaskDWord, 0x01004800);

	for(i = 0 ; i < retryCount ; i++){
		PathAOK = phy_PathA_IQK_8192C(pAdapter, is2T);
		if(PathAOK == 0x03){
			RTPRINT(FINIT, INIT_IQK, ("Path A IQK Success!!\n"));
				result[t][0] = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
				result[t][1] = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;
				result[t][2] = (PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
				result[t][3] = (PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_A_2, bMaskDWord)&0x3FF0000)>>16;
			break;
		}
		else if (i == (retryCount-1) && PathAOK == 0x01)	//Tx IQK OK
		{
			RTPRINT(FINIT, INIT_IQK, ("Path A IQK Only  Tx Success!!\n"));
			
			result[t][0] = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_A, bMaskDWord)&0x3FF0000)>>16;
			result[t][1] = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_A, bMaskDWord)&0x3FF0000)>>16;			
		}
	}

	if(0x00 == PathAOK){		
		RTPRINT(FINIT, INIT_IQK, ("Path A IQK failed!!\n"));		
	}

	if(is2T){
		phy_PathAStandBy(pAdapter);

		// Turn Path B ADDA on
		phy_PathADDAOn(pAdapter, ADDA_REG, FALSE, is2T);

		for(i = 0 ; i < retryCount ; i++){
			PathBOK = phy_PathB_IQK_8192C(pAdapter);
			if(PathBOK == 0x03){
				RTPRINT(FINIT, INIT_IQK, ("Path B IQK Success!!\n"));
				result[t][4] = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][5] = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][6] = (PHY_QueryBBReg(pAdapter, rRx_Power_Before_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
				result[t][7] = (PHY_QueryBBReg(pAdapter, rRx_Power_After_IQK_B_2, bMaskDWord)&0x3FF0000)>>16;
				break;
			}
			else if (i == (retryCount - 1) && PathBOK == 0x01)	//Tx IQK OK
			{
				RTPRINT(FINIT, INIT_IQK, ("Path B Only Tx IQK Success!!\n"));
				result[t][4] = (PHY_QueryBBReg(pAdapter, rTx_Power_Before_IQK_B, bMaskDWord)&0x3FF0000)>>16;
				result[t][5] = (PHY_QueryBBReg(pAdapter, rTx_Power_After_IQK_B, bMaskDWord)&0x3FF0000)>>16;				
			}
		}

		if(0x00 == PathBOK){		
			RTPRINT(FINIT, INIT_IQK, ("Path B IQK failed!!\n"));		
		}
	}

	//Back to BB mode, load original value
	RTPRINT(FINIT, INIT_IQK, ("IQK:Back to BB mode, load original value!\n"));
	PHY_SetBBReg(pAdapter, rFPGA0_IQK, 0xffffff00, 0);

	if(t!=0)
	{
		if(!pHalData->bRfPiEnable){
			// Switch back BB to SI mode after finish IQ Calibration.
			phy_PIModeSwitch(pAdapter, FALSE);
		}

	 	// Reload ADDA power saving parameters
	 	phy_ReloadADDARegisters(pAdapter, ADDA_REG, pHalData->ADDA_backup, IQK_ADDA_REG_NUM);

		// Reload MAC parameters
		phy_ReloadMACRegisters(pAdapter, IQK_MAC_REG, pHalData->IQK_MAC_backup);
		
	 	// Reload BB parameters
		phy_ReloadADDARegisters(pAdapter, IQK_BB_REG_92C, pHalData->IQK_BB_backup, IQK_BB_REG_NUM);
		
		/*Restore RX initial gain*/
		PHY_SetBBReg(pAdapter, rFPGA0_XA_LSSIParameter, bMaskDWord, 0x00032ed3);
		if (is2T)
			PHY_SetBBReg(pAdapter, rFPGA0_XB_LSSIParameter, bMaskDWord, 0x00032ed3);
		//load 0xe30 IQC default value
		PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x01008c00);		
		PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x01008c00);				
		
	}
	RTPRINT(FINIT, INIT_IQK, ("phy_IQCalibrate_8192C() <==\n"));
	
}


VOID	
phy_LCCalibrate92C(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		is2T
	)
{
	u1Byte	tmpReg;
	u4Byte	RF_Amode=0, RF_Bmode=0, LC_Cal;
//	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	//Check continuous TX and Packet TX
	tmpReg = PlatformEFIORead1Byte(pAdapter, 0xd03);

	if((tmpReg&0x70) != 0)			//Deal with contisuous TX case
		PlatformEFIOWrite1Byte(pAdapter, 0xd03, tmpReg&0x8F);	//disable all continuous TX
	else							// Deal with Packet TX case
		PlatformEFIOWrite1Byte(pAdapter, REG_TXPAUSE, 0xFF);			// block all queues

	if((tmpReg&0x70) != 0)
	{
		//1. Read original RF mode
		//Path-A
		RF_Amode = PHY_QueryRFReg(pAdapter, RF_PATH_A, RF_AC, bMask12Bits);

		//Path-B
		if(is2T)
			RF_Bmode = PHY_QueryRFReg(pAdapter, RF_PATH_B, RF_AC, bMask12Bits);	

		//2. Set RF mode = standby mode
		//Path-A
		PHY_SetRFReg(pAdapter, RF_PATH_A, RF_AC, bMask12Bits, (RF_Amode&0x8FFFF)|0x10000);

		//Path-B
		if(is2T)
			PHY_SetRFReg(pAdapter, RF_PATH_B, RF_AC, bMask12Bits, (RF_Bmode&0x8FFFF)|0x10000);			
	}
	
	//3. Read RF reg18
	LC_Cal = PHY_QueryRFReg(pAdapter, RF_PATH_A, RF_CHNLBW, bMask12Bits);
	
	//4. Set LC calibration begin	bit15
	PHY_SetRFReg(pAdapter, RF_PATH_A, RF_CHNLBW, bMask12Bits, LC_Cal|0x08000);

	delay_ms(100);		


	//Restore original situation
	if((tmpReg&0x70) != 0)	//Deal with contisuous TX case 
	{  
		//Path-A
		PlatformEFIOWrite1Byte(pAdapter, 0xd03, tmpReg);
		PHY_SetRFReg(pAdapter, RF_PATH_A, RF_AC, bMask12Bits, RF_Amode);
		
		//Path-B
		if(is2T)
			PHY_SetRFReg(pAdapter, RF_PATH_B, RF_AC, bMask12Bits, RF_Bmode);
	}
	else // Deal with Packet TX case
	{
		PlatformEFIOWrite1Byte(pAdapter, REG_TXPAUSE, 0x00);	
	}
}


VOID	
phy_LCCalibrate(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		is2T
	)
{
	phy_LCCalibrate92C(pAdapter, is2T);
}



//Analog Pre-distortion calibration
#define		APK_BB_REG_NUM	8
#define		APK_CURVE_REG_NUM 4
#define		PATH_NUM		2

VOID	
phy_APCalibrate_8192C(
	IN	PADAPTER	pAdapter,
	IN	s1Byte 		delta,
	IN	BOOLEAN		is2T
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	u4Byte 			regD[PATH_NUM];
	u4Byte			tmpReg, index, offset, i, apkbound;
	u1Byte			path, pathbound = PATH_NUM;
	u4Byte			BB_backup[APK_BB_REG_NUM];
	u4Byte			BB_REG[APK_BB_REG_NUM] = {	
						rFPGA1_TxBlock, 	rOFDM0_TRxPathEnable, 
						rFPGA0_RFMOD, 	rOFDM0_TRMuxPar, 
						rFPGA0_XCD_RFInterfaceSW,	rFPGA0_XAB_RFInterfaceSW, 
						rFPGA0_XA_RFInterfaceOE, 	rFPGA0_XB_RFInterfaceOE	};
	u4Byte			BB_AP_MODE[APK_BB_REG_NUM] = {	
						0x00000020, 0x00a05430, 0x02040000, 
						0x000800e4, 0x00204000 };
	u4Byte			BB_normal_AP_MODE[APK_BB_REG_NUM] = {	
						0x00000020, 0x00a05430, 0x02040000, 
						0x000800e4, 0x22204000 };						

	u4Byte			AFE_backup[IQK_ADDA_REG_NUM];
	u4Byte			AFE_REG[IQK_ADDA_REG_NUM] = {	
						rFPGA0_XCD_SwitchControl, 	rBlue_Tooth, 	
						rRx_Wait_CCA, 		rTx_CCK_RFON,
						rTx_CCK_BBON, 	rTx_OFDM_RFON, 	
						rTx_OFDM_BBON, 	rTx_To_Rx,
						rTx_To_Tx, 		rRx_CCK, 	
						rRx_OFDM, 		rRx_Wait_RIFS,
						rRx_TO_Rx, 		rStandby, 	
						rSleep, 			rPMPD_ANAEN };

	u4Byte			MAC_backup[IQK_MAC_REG_NUM];
	u4Byte			MAC_REG[IQK_MAC_REG_NUM] = {
						REG_TXPAUSE, 		REG_BCN_CTRL,	
						REG_BCN_CTRL_1,	REG_GPIO_MUXCFG};

	u4Byte			APK_RF_init_value[PATH_NUM][APK_BB_REG_NUM] = {
					{0x0852c, 0x1852c, 0x5852c, 0x1852c, 0x5852c},
					{0x2852e, 0x0852e, 0x3852e, 0x0852e, 0x0852e}
					};	

	u4Byte			APK_normal_RF_init_value[PATH_NUM][APK_BB_REG_NUM] = {
					{0x0852c, 0x0a52c, 0x3a52c, 0x5a52c, 0x5a52c},	//path settings equal to path b settings
					{0x0852c, 0x0a52c, 0x5a52c, 0x5a52c, 0x5a52c}
					};
	
	u4Byte			APK_RF_value_0[PATH_NUM][APK_BB_REG_NUM] = {
					{0x52019, 0x52014, 0x52013, 0x5200f, 0x5208d},
					{0x5201a, 0x52019, 0x52016, 0x52033, 0x52050}
					};

	u4Byte			APK_normal_RF_value_0[PATH_NUM][APK_BB_REG_NUM] = {
					{0x52019, 0x52017, 0x52010, 0x5200d, 0x5206a},	//path settings equal to path b settings
					{0x52019, 0x52017, 0x52010, 0x5200d, 0x5206a}
					};
#if 0	
	u4Byte			APK_RF_value_A[PATH_NUM][APK_BB_REG_NUM] = {
					{0x1adb0, 0x1adb0, 0x1ada0, 0x1ad90, 0x1ad80},		
					{0x00fb0, 0x00fb0, 0x00fa0, 0x00f90, 0x00f80}						
					};
#endif
	u4Byte			AFE_on_off[PATH_NUM] = {
					0x04db25a4, 0x0b1b25a4};	//path A on path B off / path A off path B on

	u4Byte			APK_offset[PATH_NUM] = {
					rConfig_AntA, rConfig_AntB};

	u4Byte			APK_normal_offset[PATH_NUM] = {
					rConfig_Pmpd_AntA, rConfig_Pmpd_AntB};
					
	u4Byte			APK_value[PATH_NUM] = {
					0x92fc0000, 0x12fc0000};					

	u4Byte			APK_normal_value[PATH_NUM] = {
					0x92680000, 0x12680000};					

	s1Byte			APK_delta_mapping[APK_BB_REG_NUM][13] = {
					{-4, -3, -2, -2, -1, -1, 0, 1, 2, 3, 4, 5, 6},
					{-4, -3, -2, -2, -1, -1, 0, 1, 2, 3, 4, 5, 6},											
					{-6, -4, -2, -2, -1, -1, 0, 1, 2, 3, 4, 5, 6},
					{-1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6},
					{-11, -9, -7, -5, -3, -1, 0, 0, 0, 0, 0, 0, 0}
					};
	
	u4Byte			APK_normal_setting_value_1[13] = {
					0x01017018, 0xf7ed8f84, 0x1b1a1816, 0x2522201e, 0x322e2b28,
					0x433f3a36, 0x5b544e49, 0x7b726a62, 0xa69a8f84, 0xdfcfc0b3,
					0x12680000, 0x00880000, 0x00880000
					};

	u4Byte			APK_normal_setting_value_2[16] = {
					0x01c7021d, 0x01670183, 0x01000123, 0x00bf00e2, 0x008d00a3,
					0x0068007b, 0x004d0059, 0x003a0042, 0x002b0031, 0x001f0025,
					0x0017001b, 0x00110014, 0x000c000f, 0x0009000b, 0x00070008,
					0x00050006
					};
	
	u4Byte			APK_result[PATH_NUM][APK_BB_REG_NUM];	//val_1_1a, val_1_2a, val_2a, val_3a, val_4a
//	u4Byte			AP_curve[PATH_NUM][APK_CURVE_REG_NUM];

	s4Byte			BB_offset, delta_V, delta_offset;

#if MP_DRIVER == 1
	PMPT_CONTEXT	pMptCtx = &(pAdapter->MptCtx);	

	pMptCtx->APK_bound[0] = 45;
	pMptCtx->APK_bound[1] = 52;		
#endif

	RTPRINT(FINIT, INIT_IQK, ("==>phy_APCalibrate_8192C() delta %d\n", delta));
	RTPRINT(FINIT, INIT_IQK, ("AP Calibration for %s\n", (is2T ? "2T2R" : "1T1R")));
	if(!is2T)
		pathbound = 1;

	//2 FOR NORMAL CHIP SETTINGS

// Temporarily do not allow normal driver to do the following settings because these offset
// and value will cause RF internal PA to be unpredictably disabled by HW, such that RF Tx signal
// will disappear after disable/enable card many times on 88CU. RF SD and DD have not find the
// root cause, so we remove these actions temporarily. Added by tynli and SD3 Allen. 2010.05.31.
#if MP_DRIVER != 1
	return;
#endif
	//settings adjust for normal chip
	for(index = 0; index < PATH_NUM; index ++)
	{
		APK_offset[index] = APK_normal_offset[index];
		APK_value[index] = APK_normal_value[index];
		AFE_on_off[index] = 0x6fdb25a4;
	}

	for(index = 0; index < APK_BB_REG_NUM; index ++)
	{
		for(path = 0; path < pathbound; path++)
		{
			APK_RF_init_value[path][index] = APK_normal_RF_init_value[path][index];
			APK_RF_value_0[path][index] = APK_normal_RF_value_0[path][index];
		}
		BB_AP_MODE[index] = BB_normal_AP_MODE[index];
	}			

	apkbound = 6;
	
	//save BB default value
	for(index = 0; index < APK_BB_REG_NUM ; index++)
	{
		if(index == 0)		//skip 
			continue;				
		BB_backup[index] = PHY_QueryBBReg(pAdapter, BB_REG[index], bMaskDWord);
	}
	
	//save MAC default value													
	phy_SaveMACRegisters(pAdapter, MAC_REG, MAC_backup);
	
	//save AFE default value
	phy_SaveADDARegisters(pAdapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);

	for(path = 0; path < pathbound; path++)
	{


		if(path == RF_PATH_A)
		{
			//path A APK
			//load APK setting
			//path-A		
			offset = rPdp_AntA;
			for(index = 0; index < 11; index ++)			
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				RTPRINT(FINIT, INIT_IQK, ("phy_APCalibrate_8192C() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}
			
			PHY_SetBBReg(pAdapter, rConfig_Pmpd_AntB, bMaskDWord, 0x12680000);
			
			offset = rConfig_AntA;
			for(; index < 13; index ++) 		
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				RTPRINT(FINIT, INIT_IQK, ("phy_APCalibrate_8192C() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}	
			
			//page-B1
			PHY_SetBBReg(pAdapter, rFPGA0_IQK, 0xffffff00, 0x400000);
		
			//path A
			offset = rPdp_AntA;
			for(index = 0; index < 16; index++)
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_2[index]);		
				RTPRINT(FINIT, INIT_IQK, ("phy_APCalibrate_8192C() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}				
			PHY_SetBBReg(pAdapter, rFPGA0_IQK, 0xffffff00, 0);							
		}
		else if(path == RF_PATH_B)
		{
			//path B APK
			//load APK setting
			//path-B		
			offset = rPdp_AntB;
			for(index = 0; index < 10; index ++)			
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				RTPRINT(FINIT, INIT_IQK, ("phy_APCalibrate_8192C() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}
			PHY_SetBBReg(pAdapter, rConfig_Pmpd_AntA, bMaskDWord, 0x12680000);
			
			PHY_SetBBReg(pAdapter, rConfig_Pmpd_AntB, bMaskDWord, 0x12680000);
			
			offset = rConfig_AntA;
			index = 11;
			for(; index < 13; index ++) //offset 0xb68, 0xb6c		
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_1[index]);
				RTPRINT(FINIT, INIT_IQK, ("phy_APCalibrate_8192C() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}	
			
			//page-B1
			PHY_SetBBReg(pAdapter, rFPGA0_IQK, 0xffffff00, 0x400000);
			
			//path B
			offset = 0xb60;
			for(index = 0; index < 16; index++)
			{
				PHY_SetBBReg(pAdapter, offset, bMaskDWord, APK_normal_setting_value_2[index]);		
				RTPRINT(FINIT, INIT_IQK, ("phy_APCalibrate_8192C() offset 0x%x value 0x%x\n", offset, PHY_QueryBBReg(pAdapter, offset, bMaskDWord))); 	
				
				offset += 0x04;
			}				
			PHY_SetBBReg(pAdapter, rFPGA0_IQK, 0xffffff00, 0);							
		}
	
		//save RF default value
		regD[path] = PHY_QueryRFReg(pAdapter, path, RF_TXBIAS_A, bRFRegOffsetMask);
		
		//Path A AFE all on, path B AFE All off or vise versa
		for(index = 0; index < IQK_ADDA_REG_NUM ; index++)
			PHY_SetBBReg(pAdapter, AFE_REG[index], bMaskDWord, AFE_on_off[path]);
		RTPRINT(FINIT, INIT_IQK, ("phy_APCalibrate_8192C() offset 0xe70 %x\n", PHY_QueryBBReg(pAdapter, rRx_Wait_CCA, bMaskDWord)));		

		//BB to AP mode
		if(path == 0)
		{				
			for(index = 0; index < APK_BB_REG_NUM ; index++)
			{

				if(index == 0)		//skip 
					continue;			
				else if (index < 5)
				PHY_SetBBReg(pAdapter, BB_REG[index], bMaskDWord, BB_AP_MODE[index]);
				else if (BB_REG[index] == 0x870)
					PHY_SetBBReg(pAdapter, BB_REG[index], bMaskDWord, BB_backup[index]|BIT10|BIT26);
				else
					PHY_SetBBReg(pAdapter, BB_REG[index], BIT10, 0x0);					
			}

			PHY_SetBBReg(pAdapter, rTx_IQK_Tone_A, bMaskDWord, 0x01008c00);			
			PHY_SetBBReg(pAdapter, rRx_IQK_Tone_A, bMaskDWord, 0x01008c00);					
		}
		else		//path B
		{
			PHY_SetBBReg(pAdapter, rTx_IQK_Tone_B, bMaskDWord, 0x01008c00);			
			PHY_SetBBReg(pAdapter, rRx_IQK_Tone_B, bMaskDWord, 0x01008c00);					
		
		}

		RTPRINT(FINIT, INIT_IQK, ("phy_APCalibrate_8192C() offset 0x800 %x\n", PHY_QueryBBReg(pAdapter, 0x800, bMaskDWord)));				

		//MAC settings
		phy_MACSettingCalibration(pAdapter, MAC_REG, MAC_backup);
		
		if(path == RF_PATH_A)	//Path B to standby mode
		{
			PHY_SetRFReg(pAdapter, RF_PATH_B, RF_AC, bRFRegOffsetMask, 0x10000);			
		}
		else			//Path A to standby mode
		{
			PHY_SetRFReg(pAdapter, RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x10000);			
			PHY_SetRFReg(pAdapter, RF_PATH_A, RF_MODE1, bRFRegOffsetMask, 0x1000f);			
			PHY_SetRFReg(pAdapter, RF_PATH_A, RF_MODE2, bRFRegOffsetMask, 0x20103);						
		}

		delta_offset = ((delta+14)/2);
		if(delta_offset < 0)
			delta_offset = 0;
		else if (delta_offset > 12)
			delta_offset = 12;
			
		//AP calibration
		for(index = 0; index < APK_BB_REG_NUM; index++)
		{
			if(index != 1)	//only DO PA11+PAD01001, AP RF setting
				continue;
					
			tmpReg = APK_RF_init_value[path][index];
#if 1			
			if(!pHalData->bAPKThermalMeterIgnore)
			{
				BB_offset = (tmpReg & 0xF0000) >> 16;

				if(!(tmpReg & BIT15)) //sign bit 0
				{
					BB_offset = -BB_offset;
				}

				delta_V = APK_delta_mapping[index][delta_offset];
				
				BB_offset += delta_V;

				RTPRINT(FINIT, INIT_IQK, ("phy_APCalibrate_8192C() APK index %d tmpReg 0x%x delta_V %d delta_offset %d\n", index, tmpReg, delta_V, delta_offset));		
				
				if(BB_offset < 0)
				{
					tmpReg = tmpReg & (~BIT15);
					BB_offset = -BB_offset;
				}
				else
				{
					tmpReg = tmpReg | BIT15;
				}
				tmpReg = (tmpReg & 0xFFF0FFFF) | (BB_offset << 16);
			}
#endif

#if DEV_BUS_TYPE==RT_PCI_INTERFACE
			if(IS_81xxC_VENDOR_UMC_B_CUT(pHalData->VersionID))
				PHY_SetRFReg(pAdapter, path, RF_IPA_A, bRFRegOffsetMask, 0x894ae);
			else
#endif	
				PHY_SetRFReg(pAdapter, path, RF_IPA_A, bRFRegOffsetMask, 0x8992e);
			RTPRINT(FINIT, INIT_IQK, ("phy_APCalibrate_8192C() offset 0xc %x\n", PHY_QueryRFReg(pAdapter, path, RF_IPA_A, bRFRegOffsetMask)));		
			PHY_SetRFReg(pAdapter, path, RF_AC, bRFRegOffsetMask, APK_RF_value_0[path][index]);
			RTPRINT(FINIT, INIT_IQK, ("phy_APCalibrate_8192C() offset 0x0 %x\n", PHY_QueryRFReg(pAdapter, path, RF_AC, bRFRegOffsetMask)));		
			PHY_SetRFReg(pAdapter, path, RF_TXBIAS_A, bRFRegOffsetMask, tmpReg);
			RTPRINT(FINIT, INIT_IQK, ("phy_APCalibrate_8192C() offset 0xd %x\n", PHY_QueryRFReg(pAdapter, path, RF_TXBIAS_A, bRFRegOffsetMask)));					
			
			// PA11+PAD01111, one shot	
			i = 0;
			do
			{
				PHY_SetBBReg(pAdapter, rFPGA0_IQK, 0xffffff00, 0x800000);
				{
					PHY_SetBBReg(pAdapter, APK_offset[path], bMaskDWord, APK_value[0]);		
					RTPRINT(FINIT, INIT_IQK, ("phy_APCalibrate_8192C() offset 0x%x value 0x%x\n", APK_offset[path], PHY_QueryBBReg(pAdapter, APK_offset[path], bMaskDWord)));
					delay_ms(3);				
					PHY_SetBBReg(pAdapter, APK_offset[path], bMaskDWord, APK_value[1]);
					RTPRINT(FINIT, INIT_IQK, ("phy_APCalibrate_8192C() offset 0x%x value 0x%x\n", APK_offset[path], PHY_QueryBBReg(pAdapter, APK_offset[path], bMaskDWord)));

					delay_ms(20);
				}
				PHY_SetBBReg(pAdapter, rFPGA0_IQK, 0xffffff00, 0);

				if(path == RF_PATH_A)
					tmpReg = PHY_QueryBBReg(pAdapter, rAPK, 0x03E00000);
				else
					tmpReg = PHY_QueryBBReg(pAdapter, rAPK, 0xF8000000);
				RTPRINT(FINIT, INIT_IQK, ("phy_APCalibrate_8192C() offset 0xbd8[25:21] %x\n", tmpReg));		
				

				i++;
			}
			while(tmpReg > apkbound && i < 4);

			APK_result[path][index] = tmpReg;
		}
	}

	//reload MAC default value	
	phy_ReloadMACRegisters(pAdapter, MAC_REG, MAC_backup);
	
	//reload BB default value	
	for(index = 0; index < APK_BB_REG_NUM ; index++)
	{

		if(index == 0)		//skip 
			continue;					
		PHY_SetBBReg(pAdapter, BB_REG[index], bMaskDWord, BB_backup[index]);
	}

	//reload AFE default value
	phy_ReloadADDARegisters(pAdapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);

	//reload RF path default value
	for(path = 0; path < pathbound; path++)
	{
		PHY_SetRFReg(pAdapter, path, RF_TXBIAS_A, bRFRegOffsetMask, regD[path]);
		if(path == RF_PATH_B)
		{
			PHY_SetRFReg(pAdapter, RF_PATH_A, RF_MODE1, bRFRegOffsetMask, 0x1000f);			
			PHY_SetRFReg(pAdapter, RF_PATH_A, RF_MODE2, bRFRegOffsetMask, 0x20101);						
		}

		//note no index == 0
		if (APK_result[path][1] > 6)
			APK_result[path][1] = 6;
		RTPRINT(FINIT, INIT_IQK, ("apk path %d result %d 0x%x \t", path, 1, APK_result[path][1]));					
	}

	RTPRINT(FINIT, INIT_IQK, ("\n"));
	

	for(path = 0; path < pathbound; path++)
	{
		PHY_SetRFReg(pAdapter, path, RF_BS_PA_APSET_G1_G4, bRFRegOffsetMask, 
		((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (APK_result[path][1] << 5) | APK_result[path][1]));
		if(path == RF_PATH_A)
			PHY_SetRFReg(pAdapter, path, RF_BS_PA_APSET_G5_G8, bRFRegOffsetMask, 
			((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (0x00 << 5) | 0x05));		
		else
		PHY_SetRFReg(pAdapter, path, RF_BS_PA_APSET_G5_G8, bRFRegOffsetMask, 
			((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (0x02 << 5) | 0x05));						
			
		PHY_SetRFReg(pAdapter, path, RF_BS_PA_APSET_G9_G11, bRFRegOffsetMask, ((0x08 << 15) | (0x08 << 10) | (0x08 << 5) | 0x08));			
	}

	pHalData->bAPKdone = TRUE;

	RTPRINT(FINIT, INIT_IQK, ("<==phy_APCalibrate_8192C()\n"));
}


VOID
PHY_IQCalibrate_8192C(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN 	bReCovery
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	s4Byte			result[4][8];	//last is final result
	u1Byte			i, final_candidate, Indexforchannel;
	BOOLEAN			bPathAOK, bPathBOK;
	s4Byte			RegE94, RegE9C, RegEA4, RegEAC, RegEB4, RegEBC, RegEC4, RegECC, RegTmp = 0;
	BOOLEAN			is12simular, is13simular, is23simular;	
	BOOLEAN 		bStartContTx = FALSE, bSingleTone = FALSE, bCarrierSuppression = FALSE;
	u4Byte			IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
					rOFDM0_XARxIQImbalance, 	rOFDM0_XBRxIQImbalance, 
					rOFDM0_ECCAThreshold, 	rOFDM0_AGCRSSITable,
					rOFDM0_XATxIQImbalance, 	rOFDM0_XBTxIQImbalance, 
					rOFDM0_XCTxAFE, 			rOFDM0_XDTxAFE, 
					rOFDM0_RxIQExtAnta};

	if (ODM_CheckPowerStatus(pAdapter) == FALSE)
		return;
	
#if MP_DRIVER == 1	
	bStartContTx = pAdapter->MptCtx.bStartContTx;
	bSingleTone = pAdapter->MptCtx.bSingleTone;
	bCarrierSuppression = pAdapter->MptCtx.bCarrierSuppression;	
#endif
	
	//ignore IQK when continuous Tx
	if(bStartContTx || bSingleTone || bCarrierSuppression)
		return;

#ifdef DISABLE_BB_RF
	return;
#endif
	if(pAdapter->bSlaveOfDMSP)
		return;

	if (bReCovery)
		{
			phy_ReloadADDARegisters(pAdapter, IQK_BB_REG_92C, pHalData->IQK_BB_backup_recover, 9);
			return;		

		}

	RTPRINT(FINIT, INIT_IQK, ("IQK:Start!!!\n"));

	for(i = 0; i < 8; i++)
	{
		result[0][i] = 0;
		result[1][i] = 0;
		result[2][i] = 0;
		result[3][i] = 0;
	}
	final_candidate = 0xff;
	bPathAOK = FALSE;
	bPathBOK = FALSE;
	is12simular = FALSE;
	is23simular = FALSE;
	is13simular = FALSE;

	AcquireCCKAndRWPageAControl(pAdapter);
	/*RT_TRACE(COMP_INIT,DBG_LOUD,("Acquire Mutex in IQCalibrate\n"));*/
	for (i=0; i<3; i++)
	{
		/*For 88C 1T1R*/
		phy_IQCalibrate_8192C(pAdapter, result, i, FALSE);
		
		if(i == 1)
		{
			is12simular = phy_SimularityCompare(pAdapter, result, 0, 1);
			if(is12simular)
			{
				final_candidate = 0;
				break;
			}
		}
		
		if(i == 2)
		{
			is13simular = phy_SimularityCompare(pAdapter, result, 0, 2);
			if(is13simular)
			{
				final_candidate = 0;			
				break;
			}
			
			is23simular = phy_SimularityCompare(pAdapter, result, 1, 2);
			if(is23simular)
				final_candidate = 1;
			else
			{
				for(i = 0; i < 8; i++)
					RegTmp += result[3][i];

				if(RegTmp != 0)
					final_candidate = 3;			
				else
					final_candidate = 0xFF;
			}
		}
	}
//	RT_TRACE(COMP_INIT,DBG_LOUD,("Release Mutex in IQCalibrate \n"));
	ReleaseCCKAndRWPageAControl(pAdapter);

	for (i=0; i<4; i++)
	{
		RegE94 = result[i][0];
		RegE9C = result[i][1];
		RegEA4 = result[i][2];
		RegEAC = result[i][3];
		RegEB4 = result[i][4];
		RegEBC = result[i][5];
		RegEC4 = result[i][6];
		RegECC = result[i][7];
		RTPRINT(FINIT, INIT_IQK, ("IQK: RegE94=%x RegE9C=%x RegEA4=%x RegEAC=%x RegEB4=%x RegEBC=%x RegEC4=%x RegECC=%x\n ", RegE94, RegE9C, RegEA4, RegEAC, RegEB4, RegEBC, RegEC4, RegECC));
	}
	
	if(final_candidate != 0xff)
	{
		pHalData->RegE94 = RegE94 = result[final_candidate][0];
		pHalData->RegE9C = RegE9C = result[final_candidate][1];
		RegEA4 = result[final_candidate][2];
		RegEAC = result[final_candidate][3];
		pHalData->RegEB4 = RegEB4 = result[final_candidate][4];
		pHalData->RegEBC = RegEBC = result[final_candidate][5];
		RegEC4 = result[final_candidate][6];
		RegECC = result[final_candidate][7];
		RTPRINT(FINIT, INIT_IQK, ("IQK: final_candidate is %x\n",final_candidate));
		RTPRINT(FINIT, INIT_IQK, ("IQK: RegE94=%x RegE9C=%x RegEA4=%x RegEAC=%x RegEB4=%x RegEBC=%x RegEC4=%x RegECC=%x\n ", RegE94, RegE9C, RegEA4, RegEAC, RegEB4, RegEBC, RegEC4, RegECC));
		bPathAOK = bPathBOK = TRUE;
	}
	else
	{
		RegE94 = RegEB4 = pHalData->RegE94 = pHalData->RegEB4 = 0x100;	//X default value
		RegE9C = RegEBC = pHalData->RegE9C = pHalData->RegEBC = 0x0;		//Y default value
	}
	
	if((RegE94 != 0)/*&&(RegEA4 != 0)*/)
	{
		if(pHalData->CurrentBandType == BAND_ON_5G)
			phy_PathAFillIQKMatrix_5G_Normal(pAdapter, bPathAOK, result, final_candidate, (RegEA4 == 0));			
		else		
			phy_PathAFillIQKMatrix(pAdapter, bPathAOK, result, final_candidate, (RegEA4 == 0));

	}
	
	if (IS_92C_SERIAL(pHalData->VersionID) || IS_92D_SINGLEPHY(pHalData->VersionID))
	{
		if((RegEB4 != 0)/*&&(RegEC4 != 0)*/)
		{
			if(pHalData->CurrentBandType == BAND_ON_5G)		
				phy_PathBFillIQKMatrix_5G_Normal(pAdapter, bPathBOK, result, final_candidate, (RegEC4 == 0));
			else
				phy_PathBFillIQKMatrix(pAdapter, bPathBOK, result, final_candidate, (RegEC4 == 0));
		}
	}
	
	phy_SaveADDARegisters(pAdapter, IQK_BB_REG_92C, pHalData->IQK_BB_backup_recover, 9);

}


VOID
PHY_LCCalibrate_8192C(
	IN	PADAPTER	pAdapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	BOOLEAN 		bStartContTx = FALSE, bSingleTone = FALSE, bCarrierSuppression = FALSE;
	PMGNT_INFO		pMgntInfo=&pAdapter->MgntInfo;
	PMGNT_INFO		pMgntInfoBuddyAdapter;
	u4Byte			timeout = 2000, timecount = 0;
	PADAPTER	BuddyAdapter = pAdapter->BuddyAdapter;

#if MP_DRIVER == 1	
	bStartContTx = pAdapter->MptCtx.bStartContTx;
	bSingleTone = pAdapter->MptCtx.bSingleTone;
	bCarrierSuppression = pAdapter->MptCtx.bCarrierSuppression;		
#endif

#ifdef DISABLE_BB_RF
	return;
#endif

	//ignore LCK when continuous Tx
	if(bStartContTx || bSingleTone || bCarrierSuppression)
		return;

	if(BuddyAdapter != NULL &&
		((pAdapter->interfaceIndex == 0 && pHalData->CurrentBandType == BAND_ON_2_4G) ||
		(pAdapter->interfaceIndex == 1 && pHalData->CurrentBandType == BAND_ON_5G)))
	{
		pMgntInfoBuddyAdapter=&BuddyAdapter->MgntInfo;
		while(pMgntInfoBuddyAdapter->bScanInProgress && timecount < timeout)
		{
			delay_ms(50);
			timecount += 50;
		}
	}

	while(pMgntInfo->bScanInProgress && timecount < timeout)
	{
		delay_ms(50);
		timecount += 50;
	}	
	
	pHalData->bLCKInProgress = TRUE;

	RTPRINT(FINIT, INIT_IQK, ("LCK:Start!!!interface %d currentband %x delay %d ms\n", pAdapter->interfaceIndex, pHalData->CurrentBandType, timecount));
	
	//if(IS_92C_SERIAL(pHalData->VersionID) || IS_92D_SINGLEPHY(pHalData->VersionID))
	if(IS_2T2R(pHalData->VersionID))
	{
		phy_LCCalibrate(pAdapter, TRUE);
	}
	else{
		// For 88C 1T1R
		phy_LCCalibrate(pAdapter, FALSE);
	}

	pHalData->bLCKInProgress = FALSE;

	RTPRINT(FINIT, INIT_IQK, ("LCK:Finish!!!interface %d\n", pAdapter->interfaceIndex));
	

}

VOID
PHY_APCalibrate_8192C(
	IN	PADAPTER	pAdapter,
	IN	s1Byte 		delta	
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	//default disable APK, because Tx NG issue, suggest by Jenyu, 2011.11.25
	return;

#ifdef DISABLE_BB_RF
	return;
#endif

#if FOR_BRAZIL_PRETEST != 1
	if(pHalData->bAPKdone)
#endif		
		return;

	if(IS_92C_SERIAL( pHalData->VersionID)){
		phy_APCalibrate_8192C(pAdapter, delta, TRUE);
	}
	else{
		// For 88C 1T1R
		phy_APCalibrate_8192C(pAdapter, delta, FALSE);
	}
}


#endif


//3============================================================
//3 IQ Calibration
//3============================================================

VOID
ODM_ResetIQKResult(
	IN	PVOID		pDM_VOID 
)
{
	return;
}
#if 1//!(DM_ODM_SUPPORT_TYPE & ODM_AP)
u1Byte ODM_GetRightChnlPlaceforIQK(u1Byte chnl)
{
	u1Byte	channel_all[ODM_TARGET_CHNL_NUM_2G_5G] = 
	{1,2,3,4,5,6,7,8,9,10,11,12,13,14,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,100,102,104,106,108,110,112,114,116,118,120,122,124,126,128,130,132,134,136,138,140,149,151,153,155,157,159,161,163,165};
	u1Byte	place = chnl;

	
	if(chnl > 14)
	{
		for(place = 14; place<sizeof(channel_all); place++)
		{
			if(channel_all[place] == chnl)
			{
				return place-13;
			}
		}
	}	
	return 0;

}
#endif

VOID
odm_IQCalibrate(
		IN	PDM_ODM_T	pDM_Odm 
		)
{
	PADAPTER	Adapter = pDM_Odm->Adapter;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)	
	if (*pDM_Odm->pIsFcsModeEnable)
		return;
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))		
	if (!IS_HARDWARE_TYPE_JAGUAR(Adapter))
		return;
#if (DM_ODM_SUPPORT_TYPE & (ODM_CE))
	else if (IS_HARDWARE_TYPE_8812AU(Adapter))
		return;
#endif
#endif
	
#if (RTL8821A_SUPPORT == 1)
	if (pDM_Odm->bLinked) {
		if ((*pDM_Odm->pChannel != pDM_Odm->preChannel) && (!*pDM_Odm->pbScanInProcess)) {
			pDM_Odm->preChannel = *pDM_Odm->pChannel;
			pDM_Odm->LinkedInterval = 0;
		}

		if (pDM_Odm->LinkedInterval < 3)
			pDM_Odm->LinkedInterval++;
		
		if (pDM_Odm->LinkedInterval == 2) {
			/*Mark out IQK flow to prevent tx stuck. by Maddest 20130306*/
			/*Open it verified by James 20130715*/
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
			PHY_IQCalibrate_8821A(pDM_Odm, FALSE);
#elif (DM_ODM_SUPPORT_TYPE == ODM_WIN)
			PHY_IQCalibrate(Adapter, FALSE);
#else
			PHY_IQCalibrate_8821A(Adapter, FALSE);
#endif
		}
	} else
		pDM_Odm->LinkedInterval = 0;
#endif
}

void phydm_rf_init(IN	PVOID		pDM_VOID)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	odm_TXPowerTrackingInit(pDM_Odm);

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	ODM_ClearTxPowerTrackingState(pDM_Odm);	
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
#if (RTL8814A_SUPPORT == 1)		
	if (pDM_Odm->SupportICType & ODM_RTL8814A)
		PHY_IQCalibrate_8814A_Init(pDM_Odm);
#endif	
#endif

}

void phydm_rf_watchdog(IN	PVOID		pDM_VOID)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	ODM_TXPowerTrackingCheck(pDM_Odm);
	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
		odm_IQCalibrate(pDM_Odm);
#endif
}
