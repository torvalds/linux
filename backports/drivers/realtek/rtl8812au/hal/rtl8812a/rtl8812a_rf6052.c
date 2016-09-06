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
#define _RTL8812A_RF6052_C_

//#include <drv_types.h>
#include <rtl8812a_hal.h>


/*-----------------------------------------------------------------------------
 * Function:    PHY_RF6052SetBandwidth()
 *
 * Overview:    This function is called by SetBWModeCallback8190Pci() only
 *
 * Input:       PADAPTER				Adapter
 *			WIRELESS_BANDWIDTH_E	Bandwidth	//20M or 40M
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Note:		For RF type 0222D
 *---------------------------------------------------------------------------*/
VOID
PHY_RF6052SetBandwidth8812(
	IN	PADAPTER				Adapter,
	IN	CHANNEL_WIDTH		Bandwidth)	//20M or 40M
{	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	
	switch(Bandwidth)
	{
		case CHANNEL_WIDTH_20:
			//DBG_871X("PHY_RF6052SetBandwidth8812(), set 20MHz, pHalData->RfRegChnlVal[0] = 0x%x \n", pHalData->RfRegChnlVal[0]);
			PHY_SetRFReg(Adapter, RF_PATH_A, RF_CHNLBW_Jaguar, BIT11|BIT10, 3);
			PHY_SetRFReg(Adapter, RF_PATH_B, RF_CHNLBW_Jaguar, BIT11|BIT10, 3);
		break;
			
		case CHANNEL_WIDTH_40:
			//DBG_871X("PHY_RF6052SetBandwidth8812(), set 40MHz, pHalData->RfRegChnlVal[0] = 0x%x \n", pHalData->RfRegChnlVal[0]);
			PHY_SetRFReg(Adapter, RF_PATH_A, RF_CHNLBW_Jaguar, BIT11|BIT10, 1);	
			PHY_SetRFReg(Adapter, RF_PATH_B, RF_CHNLBW_Jaguar, BIT11|BIT10, 1);	
		break;
		
		case CHANNEL_WIDTH_80:
			//DBG_871X("PHY_RF6052SetBandwidth8812(), set 80MHz, pHalData->RfRegChnlVal[0] = 0x%x \n", pHalData->RfRegChnlVal[0]);
			PHY_SetRFReg(Adapter, RF_PATH_A, RF_CHNLBW_Jaguar, BIT11|BIT10, 0);	
			PHY_SetRFReg(Adapter, RF_PATH_B, RF_CHNLBW_Jaguar, BIT11|BIT10, 0);	
		break;
			
		default:
			DBG_871X("PHY_RF6052SetBandwidth8812(): unknown Bandwidth: %#X\n",Bandwidth );
			break;			
	}
}

//
// powerbase0 for OFDM rates
// powerbase1 for HT MCS rates
//
void getPowerBase8812(
	IN	PADAPTER	Adapter,
	IN	u8*			pPowerLevelOFDM,
	IN	u8*			pPowerLevelBW20,
	IN	u8*			pPowerLevelBW40,
	IN	u8			Channel,
	IN OUT u32*		OfdmBase,
	IN OUT u32*		MCSBase
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32			powerBase0, powerBase1;
	u8			i, powerlevel[2];

	for(i=0; i<2; i++)
	{
		powerBase0 = pPowerLevelOFDM[i];

		powerBase0 = (powerBase0<<24) | (powerBase0<<16) |(powerBase0<<8) |powerBase0;
		*(OfdmBase+i) = powerBase0;
		//DBG_871X(" [OFDM power base index rf(%c) = 0x%x]\n", ((i==0)?'A':'B'), *(OfdmBase+i));
	}

	for(i=0; i<pHalData->NumTotalRFPath; i++)
	{
		//Check HT20 to HT40 diff
		if(pHalData->CurrentChannelBW == CHANNEL_WIDTH_20)
		{
			powerlevel[i] = pPowerLevelBW20[i];
		}
		else
		{
			powerlevel[i] = pPowerLevelBW40[i];
		}	
		powerBase1 = powerlevel[i];
		powerBase1 = (powerBase1<<24) | (powerBase1<<16) |(powerBase1<<8) |powerBase1;
		*(MCSBase+i) = powerBase1;
		//DBG_871X(" [MCS power base index rf(%c) = 0x%x]\n", ((i==0)?'A':'B'), *(MCSBase+i));
	}
}

void getTxPowerWriteValByRegulatory8812(
	IN		PADAPTER	Adapter,
	IN		u8			Channel,
	IN		u8			index,
	IN		u32*		powerBase0,
	IN		u32*		powerBase1,
	OUT		u32*		pOutWriteVal
	)
{
	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u8			i, chnlGroup=0, pwr_diff_limit[4], customer_pwr_limit;
	s8			pwr_diff=0;
	u32 			writeVal, customer_limit, rf;
	u8			Regulatory = pHalData->EEPROMRegulatory;

	//
	// Index 0 & 1= legacy OFDM, 2-5=HT_MCS rate
	//
#if 0 // (INTEL_PROXIMITY_SUPPORT == 1)
	if(pMgntInfo->IntelProximityModeInfo.PowerOutput > 0)
		Regulatory = 2;
#endif

	for(rf=0; rf<2; rf++)
	{
		switch(Regulatory)
		{
			case 0:	// Realtek better performance
					// increase power diff defined by Realtek for large power
				chnlGroup = 0;
				//RTPRINT(FPHY, PHY_TXPWR, ("MCSTxPowerLevelOriginalOffset[%d][%d] = 0x%x\n", 
				//	chnlGroup, index, pHalData->MCSTxPowerLevelOriginalOffset[chnlGroup][index+(rf?8:0)]));
				writeVal = pHalData->MCSTxPowerLevelOriginalOffset[chnlGroup][index+(rf?8:0)] + 
					((index<2)?powerBase0[rf]:powerBase1[rf]);
				//RTPRINT(FPHY, PHY_TXPWR, ("RTK better performance, writeVal(%c) = 0x%x\n", ((rf==0)?'A':'B'), writeVal));
				break;
			case 1:	// Realtek regulatory
					// increase power diff defined by Realtek for regulatory
				{
					if(pHalData->pwrGroupCnt == 1)
						chnlGroup = 0;
					//if(pHalData->pwrGroupCnt >= pHalData->PGMaxGroup)
					{
						if (Channel < 3)			// Chanel 1-2
							chnlGroup = 0;
						else if (Channel < 6)		// Channel 3-5
							chnlGroup = 1;
						else	 if(Channel <9)		// Channel 6-8
							chnlGroup = 2;
						else if(Channel <12)		// Channel 9-11
							chnlGroup = 3;
						else if(Channel <14)		// Channel 12-13
							chnlGroup = 4;
						else if(Channel ==14)		// Channel 14
							chnlGroup = 5;	
				
/*
						if(Channel <= 3)
							chnlGroup = 0;
						else if(Channel >= 4 && Channel <= 9)
							chnlGroup = 1;
						else if(Channel > 9)
							chnlGroup = 2;
						
						
						if(pHalData->CurrentChannelBW == CHANNEL_WIDTH_20)
							chnlGroup++;
						else
							chnlGroup+=4;
*/							
					}
					//RTPRINT(FPHY, PHY_TXPWR, ("MCSTxPowerLevelOriginalOffset[%d][%d] = 0x%x\n", 
					//chnlGroup, index, pHalData->MCSTxPowerLevelOriginalOffset[chnlGroup][index+(rf?8:0)]));
					writeVal = pHalData->MCSTxPowerLevelOriginalOffset[chnlGroup][index+(rf?8:0)] + 
							((index<2)?powerBase0[rf]:powerBase1[rf]);
					//RTPRINT(FPHY, PHY_TXPWR, ("Realtek regulatory, 20MHz, writeVal(%c) = 0x%x\n", ((rf==0)?'A':'B'), writeVal));
				}
				break;
			case 2:	// Better regulatory
					// don't increase any power diff
				writeVal = ((index<2)?powerBase0[rf]:powerBase1[rf]);
				//RTPRINT(FPHY, PHY_TXPWR, ("Better regulatory, writeVal(%c) = 0x%x\n", ((rf==0)?'A':'B'), writeVal));
				break;
			case 3:	// Customer defined power diff.
					// increase power diff defined by customer.
				chnlGroup = 0;
				//RTPRINT(FPHY, PHY_TXPWR, ("MCSTxPowerLevelOriginalOffset[%d][%d] = 0x%x\n", 
				//	chnlGroup, index, pHalData->MCSTxPowerLevelOriginalOffset[chnlGroup][index+(rf?8:0)]));

				/*
				if (pHalData->CurrentChannelBW == CHANNEL_WIDTH_40)
				{
					RTPRINT(FPHY, PHY_TXPWR, ("customer's limit, 40MHz rf(%c) = 0x%x\n", 
						((rf==0)?'A':'B'), pHalData->PwrGroupHT40[rf][Channel-1]));
				}
				else
				{
					RTPRINT(FPHY, PHY_TXPWR, ("customer's limit, 20MHz rf(%c) = 0x%x\n", 
						((rf==0)?'A':'B'), pHalData->PwrGroupHT20[rf][Channel-1]));
				}*/

				if(index < 2)
					pwr_diff = pHalData->TxPwrLegacyHtDiff[rf][Channel-1];
				else if (pHalData->CurrentChannelBW == CHANNEL_WIDTH_20)
					pwr_diff = pHalData->TxPwrHt20Diff[rf][Channel-1];

				//RTPRINT(FPHY, PHY_TXPWR, ("power diff rf(%c) = 0x%x\n", ((rf==0)?'A':'B'), pwr_diff));

				if (pHalData->CurrentChannelBW == CHANNEL_WIDTH_40)
					customer_pwr_limit = pHalData->PwrGroupHT40[rf][Channel-1];
				else
					customer_pwr_limit = pHalData->PwrGroupHT20[rf][Channel-1];

				//RTPRINT(FPHY, PHY_TXPWR, ("customer pwr limit  rf(%c) = 0x%x\n", ((rf==0)?'A':'B'), customer_pwr_limit));

				if(pwr_diff >= customer_pwr_limit)
					pwr_diff = 0;
				else
					pwr_diff = customer_pwr_limit - pwr_diff;
				
				for (i=0; i<4; i++)
				{
					pwr_diff_limit[i] = (u1Byte)((pHalData->MCSTxPowerLevelOriginalOffset[chnlGroup][index+(rf?8:0)]&(0x7f<<(i*8)))>>(i*8));
					
					if(pwr_diff_limit[i] > pwr_diff)
						pwr_diff_limit[i] = pwr_diff;
				}
				customer_limit = (pwr_diff_limit[3]<<24) | (pwr_diff_limit[2]<<16) |
								(pwr_diff_limit[1]<<8) | (pwr_diff_limit[0]);
				//RTPRINT(FPHY, PHY_TXPWR, ("Customer's limit rf(%c) = 0x%x\n", ((rf==0)?'A':'B'), customer_limit));
				writeVal = customer_limit + ((index<2)?powerBase0[rf]:powerBase1[rf]);
				//RTPRINT(FPHY, PHY_TXPWR, ("Customer, writeVal rf(%c)= 0x%x\n", ((rf==0)?'A':'B'), writeVal));
				break;
			default:
				chnlGroup = 0;
				writeVal = pHalData->MCSTxPowerLevelOriginalOffset[chnlGroup][index+(rf?8:0)] + 
						((index<2)?powerBase0[rf]:powerBase1[rf]);
				//RTPRINT(FPHY, PHY_TXPWR, ("RTK better performance, writeVal rf(%c) = 0x%x\n", ((rf==0)?'A':'B'), writeVal));
				break;
		}

// 20100427 Joseph: Driver dynamic Tx power shall not affect Tx power. It shall be determined by power training mechanism.
// Currently, we cannot fully disable driver dynamic tx power mechanism because it is referenced by BT coexist mechanism.
// In the future, two mechanism shall be separated from each other and maintained independantly. Thanks for Lanhsin's reminder.
		//92d do not need this
		if(pdmpriv->DynamicTxHighPowerLvl == TxHighPwrLevel_Level1)
			writeVal = 0x14141414;
		else if(pdmpriv->DynamicTxHighPowerLvl == TxHighPwrLevel_Level2)
			writeVal = 0x00000000;

		// 20100628 Joseph: High power mode for BT-Coexist mechanism.
		// This mechanism is only applied when Driver-Highpower-Mechanism is OFF.
		if(pdmpriv->DynamicTxHighPowerLvl == TxHighPwrLevel_BT1)
		{
			//RTPRINT(FBT, BT_TRACE, ("Tx Power (-6)\n"));
			writeVal = writeVal - 0x06060606;
		}
		else if(pdmpriv->DynamicTxHighPowerLvl == TxHighPwrLevel_BT2)
		{
			//RTPRINT(FBT, BT_TRACE, ("Tx Power (-0)\n"));
			writeVal = writeVal ;		
		}
		/*
		if(pMgntInfo->bDisableTXPowerByRate)
		{
		// add for  OID_RT_11N_TX_POWER_BY_RATE ,disable tx powre change by rate                                  
			writeVal = 0x2c2c2c2c;
		}
		*/
		*(pOutWriteVal+rf) = writeVal;
	}
}

static void writeOFDMPowerReg8812(
	IN		PADAPTER	Adapter,
	IN		u8		index,
	IN 		u32*		pValue
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	u2Byte RegOffset_A[6] = {	
    	rTxAGC_A_Ofdm18_Ofdm6_JAguar,
        rTxAGC_A_Ofdm54_Ofdm24_JAguar,
        rTxAGC_A_MCS3_MCS0_JAguar,
        rTxAGC_A_MCS7_MCS4_JAguar,
        rTxAGC_A_MCS11_MCS8_JAguar,
        rTxAGC_A_MCS15_MCS12_JAguar
    };
	u2Byte RegOffset_B[6] = {
        rTxAGC_B_Ofdm18_Ofdm6_JAguar,
        rTxAGC_B_Ofdm54_Ofdm24_JAguar,
        rTxAGC_B_MCS3_MCS0_JAguar,
        rTxAGC_B_MCS7_MCS4_JAguar,
        rTxAGC_B_MCS11_MCS8_JAguar,
        rTxAGC_B_MCS15_MCS12_JAguar 
    };

	u8	i, rf, pwr_val[4];
	u32	writeVal;
	u16	RegOffset;

	for(rf=0; rf<2; rf++)
	{
		writeVal = pValue[rf];
		for(i=0; i<RF_PATH_MAX_92C_88E; i++)
		{
			pwr_val[i] = (u8)((writeVal & (0x7f<<(i*8)))>>(i*8));
			if (pwr_val[i]  > RF6052_MAX_TX_PWR)
				pwr_val[i]  = RF6052_MAX_TX_PWR;
		}
		writeVal = (pwr_val[3]<<24) | (pwr_val[2]<<16) |(pwr_val[1]<<8) |pwr_val[0];

		if(rf == 0)
			RegOffset = RegOffset_A[index];
		else
			RegOffset = RegOffset_B[index];
		PHY_SetBBReg(Adapter, RegOffset, bMaskDWord, writeVal);
		//RTPRINT(FPHY, PHY_TXPWR, ("Set 0x%x = %08x\n", RegOffset, writeVal));
	}
}

VOID
PHY_RF6052SetCckTxPower8812(
	IN	PADAPTER	Adapter,
	IN	u8*			pPowerlevel)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv		*pdmpriv = &pHalData->dmpriv;
	struct mlme_ext_priv 	*pmlmeext = &Adapter->mlmeextpriv;
	u32			TxAGC[2]={0, 0}, tmpval=0;
	BOOLEAN		TurboScanOff = _FALSE;
	u8			idx1, idx2;
	u8*			ptr;

	//FOR CE ,must disable turbo scan
	TurboScanOff = _TRUE;	

	if(pmlmeext->sitesurvey_res.state == SCAN_PROCESS)
	{
		TxAGC[RF_PATH_A] = 0x3f3f3f3f;
		TxAGC[RF_PATH_B] = 0x3f3f3f3f;

		TurboScanOff = _TRUE;//disable turbo scan

		if(TurboScanOff)
		{
			for(idx1=RF_PATH_A; idx1<=RF_PATH_B; idx1++)
			{
				TxAGC[idx1] =
					pPowerlevel[idx1] | (pPowerlevel[idx1]<<8) |
					(pPowerlevel[idx1]<<16) | (pPowerlevel[idx1]<<24);
#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI)
				// 2010/10/18 MH For external PA module. We need to limit power index to be less than 0x20.
				if (TxAGC[idx1] > 0x20 && pHalData->ExternalPA_5G)
					TxAGC[idx1] = 0x20;
#endif
			}
		}
	}
	else
	{
// 20100427 Joseph: Driver dynamic Tx power shall not affect Tx power. It shall be determined by power training mechanism.
// Currently, we cannot fully disable driver dynamic tx power mechanism because it is referenced by BT coexist mechanism.
// In the future, two mechanism shall be separated from each other and maintained independantly. Thanks for Lanhsin's reminder.
		if(pdmpriv->DynamicTxHighPowerLvl == TxHighPwrLevel_Level1)
		{
			TxAGC[RF_PATH_A] = 0x10101010;
			TxAGC[RF_PATH_B] = 0x10101010;
		}
		else if(pdmpriv->DynamicTxHighPowerLvl == TxHighPwrLevel_Level2)
		{
			TxAGC[RF_PATH_A] = 0x00000000;
			TxAGC[RF_PATH_B] = 0x00000000;
		}
		else
		{
			for(idx1=RF_PATH_A; idx1<=RF_PATH_B; idx1++)
			{
				TxAGC[idx1] =
					pPowerlevel[idx1] | (pPowerlevel[idx1]<<8) |
					(pPowerlevel[idx1]<<16) | (pPowerlevel[idx1]<<24);
			}

			if(pHalData->EEPROMRegulatory==0)
			{
				tmpval = (pHalData->MCSTxPowerLevelOriginalOffset[0][6]) +
						(pHalData->MCSTxPowerLevelOriginalOffset[0][7]<<8);
				TxAGC[RF_PATH_A] += tmpval;

				tmpval = (pHalData->MCSTxPowerLevelOriginalOffset[0][14]) +
						(pHalData->MCSTxPowerLevelOriginalOffset[0][15]<<24);
				TxAGC[RF_PATH_B] += tmpval;
			}
		}
	}

	for(idx1=RF_PATH_A; idx1<=RF_PATH_B; idx1++)
	{
		ptr = (u8*)(&(TxAGC[idx1]));
		for(idx2=0; idx2<4; idx2++)
		{
			if(*ptr > RF6052_MAX_TX_PWR)
				*ptr = RF6052_MAX_TX_PWR;
			ptr++;
		}
	}

	// rf-A cck tx power
	tmpval = TxAGC[RF_PATH_A]&0xff;
	PHY_SetBBReg(Adapter, rTxAGC_A_CCK11_CCK1_JAguar, bMaskByte1, tmpval);
	//RT_DISP(FPHY, PHY_TXPWR, ("CCK PWR 1M (rf-A) = 0x%x (reg 0x%x)\n", tmpval, rTxAGC_A_CCK1_Mcs32));
	tmpval = TxAGC[RF_PATH_A]>>8;
	PHY_SetBBReg(Adapter, rTxAGC_A_CCK11_CCK1_JAguar, 0xffffff00, tmpval);
	//RT_DISP(FPHY, PHY_TXPWR, ("CCK PWR 2~11M (rf-A) = 0x%x (reg 0x%x)\n", tmpval, rTxAGC_B_CCK11_A_CCK2_11));

	// rf-B cck tx power
	tmpval = TxAGC[RF_PATH_B]>>24;
	PHY_SetBBReg(Adapter, rTxAGC_B_CCK11_CCK1_JAguar, bMaskByte0, tmpval);
	//RT_DISP(FPHY, PHY_TXPWR, ("CCK PWR 11M (rf-B) = 0x%x (reg 0x%x)\n", tmpval, rTxAGC_B_CCK11_A_CCK2_11));
	tmpval = TxAGC[RF_PATH_B]&0x00ffffff;
	PHY_SetBBReg(Adapter, rTxAGC_B_CCK11_CCK1_JAguar, 0xffffff00, tmpval);
	//RT_DISP(FPHY, PHY_TXPWR, ("CCK PWR 1~5.5M (rf-B) = 0x%x (reg 0x%x)\n", tmpval, rTxAGC_B_CCK1_55_Mcs32));

}	/* PHY_RF6052SetCckTxPower */

VOID 
PHY_RF6052SetOFDMTxPower8812(
	IN	PADAPTER	Adapter,
	IN	u8*		pPowerLevelOFDM,
	IN	u8*		pPowerLevelBW20,
	IN	u8*		pPowerLevelBW40,	
	IN	u8		Channel)
{
	u32 writeVal[2], powerBase0[2], powerBase1[2], pwrtrac_value;
	u8 index = 0;	
	

	//DBG_871X("PHY_RF6052SetOFDMTxPower, channel(%d) \n", Channel);

	getPowerBase8812(Adapter, pPowerLevelOFDM,pPowerLevelBW20,pPowerLevelBW40, Channel, &powerBase0[0], &powerBase1[0]);
	
	for(index=0; index<6; index++)
	{
		getTxPowerWriteValByRegulatory8812(Adapter, Channel, index, 
			&powerBase0[0], &powerBase1[0], &writeVal[0]);

		writeOFDMPowerReg8812(Adapter, index, &writeVal[0]);
	}
}

static int
phy_RF6052_Config_ParaFile_8812(
	IN	PADAPTER		Adapter
	)
{
	u8					eRFPath;
	int					rtStatus = _SUCCESS;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	static char			sz8812RadioAFile[] = RTL8812_PHY_RADIO_A;	
	static char			sz8812RadioBFile[] = RTL8812_PHY_RADIO_B;
	static char 			sz8812TxPwrTrack[] = RTL8812_TXPWR_TRACK;
	static char			sz8821RadioAFile[] = RTL8821_PHY_RADIO_A;
	static char			sz8821RadioBFile[] = RTL8821_PHY_RADIO_B;
	static char 			sz8821TxPwrTrack[] = RTL8821_TXPWR_TRACK;	
	char					*pszRadioAFile = NULL, *pszRadioBFile = NULL, *pszTxPwrTrack = NULL;


	if(IS_HARDWARE_TYPE_8812(Adapter))
	{
		pszRadioAFile = sz8812RadioAFile;
		pszRadioBFile = sz8812RadioBFile;
		pszTxPwrTrack = sz8812TxPwrTrack;
	}
	else
	{
		pszRadioAFile = sz8821RadioAFile;
		pszRadioBFile = sz8821RadioBFile;
		pszTxPwrTrack = sz8821TxPwrTrack;
	}


	//3//-----------------------------------------------------------------
	//3// <2> Initialize RF
	//3//-----------------------------------------------------------------
	//for(eRFPath = RF_PATH_A; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
	for(eRFPath = 0; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
	{
		/*----Initialize RF fom connfiguration file----*/
		switch(eRFPath)
		{
		case RF_PATH_A:
#ifdef CONFIG_EMBEDDED_FWIMG
			if(HAL_STATUS_FAILURE ==ODM_ConfigRFWithHeaderFile(&pHalData->odmpriv,CONFIG_RF_RADIO, (ODM_RF_RADIO_PATH_E)eRFPath))
				rtStatus= _FAIL;
#else
			rtStatus = PHY_ConfigRFWithParaFile(Adapter, pszRadioAFile, eRFPath);
#endif//#ifdef CONFIG_EMBEDDED_FWIMG
			break;
		case RF_PATH_B:
#ifdef CONFIG_EMBEDDED_FWIMG
			if(HAL_STATUS_FAILURE ==ODM_ConfigRFWithHeaderFile(&pHalData->odmpriv,CONFIG_RF_RADIO, (ODM_RF_RADIO_PATH_E)eRFPath))
				rtStatus= _FAIL;
#else
			rtStatus =PHY_ConfigRFWithParaFile(Adapter, pszRadioBFile, eRFPath);
#endif
			break;
		default:
			break;
		}

		if(rtStatus != _SUCCESS){
			DBG_871X("%s():Radio[%d] Fail!!", __FUNCTION__, eRFPath);
			goto phy_RF6052_Config_ParaFile_Fail;
		}

	}

	//3 -----------------------------------------------------------------
	//3 Configuration of Tx Power Tracking 
	//3 -----------------------------------------------------------------

#ifdef CONFIG_EMBEDDED_FWIMG
	ODM_ConfigRFWithTxPwrTrackHeaderFile(&pHalData->odmpriv);
#else
	PHY_ConfigRFWithTxPwrTrackParaFile(Adapter, pszTxPwrTrack);
#endif

	//RT_TRACE(COMP_INIT, DBG_LOUD, ("<---phy_RF6052_Config_ParaFile_8812()\n"));

phy_RF6052_Config_ParaFile_Fail:
	return rtStatus;
}


int
PHY_RF6052_Config_8812(
	IN	PADAPTER		Adapter)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	int					rtStatus = _SUCCESS;

	// Initialize general global value
	if(pHalData->rf_type == RF_1T1R)
		pHalData->NumTotalRFPath = 1;
	else
		pHalData->NumTotalRFPath = 2;

	//
	// Config BB and RF
	//
	rtStatus = phy_RF6052_Config_ParaFile_8812(Adapter);

	return rtStatus;

}


/* End of HalRf6052.c */

