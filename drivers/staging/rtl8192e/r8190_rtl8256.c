/*
  This is part of the rtl8192 driver
  released under the GPL (See file COPYING for details).

  This files contains programming code for the rtl8256
  radio frontend.

  *Many* thanks to Realtek Corp. for their great support!

*/

#include "r8192E.h"
#include "r8192E_hw.h"
#include "r819xE_phyreg.h"
#include "r819xE_phy.h"
#include "r8190_rtl8256.h"

/*--------------------------------------------------------------------------
 * Overview:   	set RF band width (20M or 40M)
 * Input:       struct net_device*	dev
 * 		WIRELESS_BANDWIDTH_E	Bandwidth	//20M or 40M
 * Output:      NONE
 * Return:      NONE
 * Note:	8226 support both 20M  and 40 MHz
 *---------------------------------------------------------------------------*/
void PHY_SetRF8256Bandwidth(struct net_device* dev , HT_CHANNEL_WIDTH Bandwidth)	//20M or 40M
{
	u8	eRFPath;
	struct r8192_priv *priv = ieee80211_priv(dev);

	//for(eRFPath = RF90_PATH_A; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
	for(eRFPath = 0; eRFPath <priv->NumTotalRFPath; eRFPath++)
	{
		if (!rtl8192_phy_CheckIsLegalRFPath(dev, eRFPath))
				continue;

		switch(Bandwidth)
		{
			case HT_CHANNEL_WIDTH_20:
				if(priv->card_8192_version == VERSION_8190_BD || priv->card_8192_version == VERSION_8190_BE)// 8256 D-cut, E-cut, xiong: consider it later!
				{
					rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, 0x0b, bMask12Bits, 0x100); //phy para:1ba
					rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, 0x2c, bMask12Bits, 0x3d7);
					rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, 0x0e, bMask12Bits, 0x021);

					//cosa add for sd3's request 01/23/2008
					//rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, 0x14, bMask12Bits, 0x5ab);
				}
				else
				{
					RT_TRACE(COMP_ERR, "PHY_SetRF8256Bandwidth(): unknown hardware version\n");
				}

				break;
			case HT_CHANNEL_WIDTH_20_40:
				if(priv->card_8192_version == VERSION_8190_BD ||priv->card_8192_version == VERSION_8190_BE)// 8256 D-cut, E-cut, xiong: consider it later!
				{
					rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, 0x0b, bMask12Bits, 0x300); //phy para:3ba
					rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, 0x2c, bMask12Bits, 0x3ff);
					rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, 0x0e, bMask12Bits, 0x0e1);

					//cosa add for sd3's request 01/23/2008
					#if 0
					if(priv->chan == 3 || priv->chan == 9) //I need to set priv->chan whenever current channel changes
						rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, 0x14, bMask12Bits, 0x59b);
					else
						rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, 0x14, bMask12Bits, 0x5ab);
					#endif
				}
				else
				{
					RT_TRACE(COMP_ERR, "PHY_SetRF8256Bandwidth(): unknown hardware version\n");
				}


				break;
			default:
				RT_TRACE(COMP_ERR, "PHY_SetRF8256Bandwidth(): unknown Bandwidth: %#X\n",Bandwidth );
				break;

		}
	}
	return;
}
/*--------------------------------------------------------------------------
 * Overview:    Interface to config 8256
 * Input:       struct net_device*	dev
 * Output:      NONE
 * Return:      NONE
 *---------------------------------------------------------------------------*/
RT_STATUS PHY_RF8256_Config(struct net_device* dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	// Initialize general global value
	//
	RT_STATUS rtStatus = RT_STATUS_SUCCESS;
	// TODO: Extend RF_PATH_C and RF_PATH_D in the future
	priv->NumTotalRFPath = RTL819X_TOTAL_RF_PATH;
	// Config BB and RF
	rtStatus = phy_RF8256_Config_ParaFile(dev);

	return rtStatus;
}
/*--------------------------------------------------------------------------
 * Overview:    Interface to config 8256
 * Input:       struct net_device*	dev
 * Output:      NONE
 * Return:      NONE
 *---------------------------------------------------------------------------*/
RT_STATUS phy_RF8256_Config_ParaFile(struct net_device* dev)
{
	u32 	u4RegValue = 0;
	u8 	eRFPath;
	RT_STATUS				rtStatus = RT_STATUS_SUCCESS;
	BB_REGISTER_DEFINITION_T	*pPhyReg;
	struct r8192_priv *priv = ieee80211_priv(dev);
	u32	RegOffSetToBeCheck = 0x3;
	u32 	RegValueToBeCheck = 0x7f1;
	u32	RF3_Final_Value = 0;
	u8	ConstRetryTimes = 5, RetryTimes = 5;
	u8 ret = 0;
	//3//-----------------------------------------------------------------
	//3// <2> Initialize RF
	//3//-----------------------------------------------------------------
	for(eRFPath = (RF90_RADIO_PATH_E)RF90_PATH_A; eRFPath <priv->NumTotalRFPath; eRFPath++)
	{
		if (!rtl8192_phy_CheckIsLegalRFPath(dev, eRFPath))
				continue;

		pPhyReg = &priv->PHYRegDef[eRFPath];

		// Joseph test for shorten RF config
	//	pHalData->RfReg0Value[eRFPath] =  rtl8192_phy_QueryRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, rGlobalCtrl, bMaskDWord);

		/*----Store original RFENV control type----*/
		switch(eRFPath)
		{
		case RF90_PATH_A:
		case RF90_PATH_C:
			u4RegValue = rtl8192_QueryBBReg(dev, pPhyReg->rfintfs, bRFSI_RFENV);
			break;
		case RF90_PATH_B :
		case RF90_PATH_D:
			u4RegValue = rtl8192_QueryBBReg(dev, pPhyReg->rfintfs, bRFSI_RFENV<<16);
			break;
		}

		/*----Set RF_ENV enable----*/
		rtl8192_setBBreg(dev, pPhyReg->rfintfe, bRFSI_RFENV<<16, 0x1);

		/*----Set RF_ENV output high----*/
		rtl8192_setBBreg(dev, pPhyReg->rfintfo, bRFSI_RFENV, 0x1);

		/* Set bit number of Address and Data for RF register */
		rtl8192_setBBreg(dev, pPhyReg->rfHSSIPara2, b3WireAddressLength, 0x0); 	// Set 0 to 4 bits for Z-serial and set 1 to 6 bits for 8258
		rtl8192_setBBreg(dev, pPhyReg->rfHSSIPara2, b3WireDataLength, 0x0);	// Set 0 to 12 bits for Z-serial and 8258, and set 1 to 14 bits for ???

		rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E) eRFPath, 0x0, bMask12Bits, 0xbf);

		/*----Check RF block (for FPGA platform only)----*/
		// TODO: this function should be removed on ASIC , Emily 2007.2.2
		rtStatus = rtl8192_phy_checkBBAndRF(dev, HW90_BLOCK_RF, (RF90_RADIO_PATH_E)eRFPath);
		if(rtStatus!= RT_STATUS_SUCCESS)
		{
			RT_TRACE(COMP_ERR, "PHY_RF8256_Config():Check Radio[%d] Fail!!\n", eRFPath);
			goto phy_RF8256_Config_ParaFile_Fail;
		}

		RetryTimes = ConstRetryTimes;
		RF3_Final_Value = 0;
		/*----Initialize RF fom connfiguration file----*/
		switch(eRFPath)
		{
		case RF90_PATH_A:
			while(RF3_Final_Value!=RegValueToBeCheck && RetryTimes!=0)
			{
				ret = rtl8192_phy_ConfigRFWithHeaderFile(dev,(RF90_RADIO_PATH_E)eRFPath);
				RF3_Final_Value = rtl8192_phy_QueryRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, RegOffSetToBeCheck, bMask12Bits);
				RT_TRACE(COMP_RF, "RF %d %d register final value: %x\n", eRFPath, RegOffSetToBeCheck, RF3_Final_Value);
				RetryTimes--;
			}
			break;
		case RF90_PATH_B:
			while(RF3_Final_Value!=RegValueToBeCheck && RetryTimes!=0)
			{
				ret = rtl8192_phy_ConfigRFWithHeaderFile(dev,(RF90_RADIO_PATH_E)eRFPath);
				RF3_Final_Value = rtl8192_phy_QueryRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, RegOffSetToBeCheck, bMask12Bits);
				RT_TRACE(COMP_RF, "RF %d %d register final value: %x\n", eRFPath, RegOffSetToBeCheck, RF3_Final_Value);
				RetryTimes--;
			}
			break;
		case RF90_PATH_C:
			while(RF3_Final_Value!=RegValueToBeCheck && RetryTimes!=0)
			{
				ret = rtl8192_phy_ConfigRFWithHeaderFile(dev,(RF90_RADIO_PATH_E)eRFPath);
				RF3_Final_Value = rtl8192_phy_QueryRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, RegOffSetToBeCheck, bMask12Bits);
				RT_TRACE(COMP_RF, "RF %d %d register final value: %x\n", eRFPath, RegOffSetToBeCheck, RF3_Final_Value);
				RetryTimes--;
			}
			break;
		case RF90_PATH_D:
			while(RF3_Final_Value!=RegValueToBeCheck && RetryTimes!=0)
			{
				ret = rtl8192_phy_ConfigRFWithHeaderFile(dev,(RF90_RADIO_PATH_E)eRFPath);
				RF3_Final_Value = rtl8192_phy_QueryRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, RegOffSetToBeCheck, bMask12Bits);
				RT_TRACE(COMP_RF, "RF %d %d register final value: %x\n", eRFPath, RegOffSetToBeCheck, RF3_Final_Value);
				RetryTimes--;
			}
			break;
		}

		/*----Restore RFENV control type----*/;
		switch(eRFPath)
		{
		case RF90_PATH_A:
		case RF90_PATH_C:
			rtl8192_setBBreg(dev, pPhyReg->rfintfs, bRFSI_RFENV, u4RegValue);
			break;
		case RF90_PATH_B :
		case RF90_PATH_D:
			rtl8192_setBBreg(dev, pPhyReg->rfintfs, bRFSI_RFENV<<16, u4RegValue);
			break;
		}

		if(ret){
			RT_TRACE(COMP_ERR, "phy_RF8256_Config_ParaFile():Radio[%d] Fail!!", eRFPath);
			goto phy_RF8256_Config_ParaFile_Fail;
		}

	}

	RT_TRACE(COMP_PHY, "PHY Initialization Success\n") ;
	return RT_STATUS_SUCCESS;

phy_RF8256_Config_ParaFile_Fail:
	RT_TRACE(COMP_ERR, "PHY Initialization failed\n") ;
	return RT_STATUS_FAILURE;
}


void PHY_SetRF8256CCKTxPower(struct net_device*	dev, u8	powerlevel)
{
	u32	TxAGC=0;
	struct r8192_priv *priv = ieee80211_priv(dev);
#ifdef RTL8190P
	u8				byte0, byte1;

	TxAGC |= ((powerlevel<<8)|powerlevel);
	TxAGC += priv->CCKTxPowerLevelOriginalOffset;

	if(priv->bDynamicTxLowPower == true  //cosa 04282008 for cck long range
		/*pMgntInfo->bScanInProgress == TRUE*/ ) //cosa 05/22/2008 for scan
	{
		if(priv->CustomerID == RT_CID_819x_Netcore)
			TxAGC = 0x2222;
		else
		TxAGC += ((priv->CckPwEnl<<8)|priv->CckPwEnl);
	}

	byte0 = (u8)(TxAGC & 0xff);
	byte1 = (u8)((TxAGC & 0xff00)>>8);
	if(byte0 > 0x24)
		byte0 = 0x24;
	if(byte1 > 0x24)
		byte1 = 0x24;
	if(priv->rf_type == RF_2T4R)	//Only 2T4R you have to care the Antenna Tx Power offset
	{	// check antenna C over the max index 0x24
			if(priv->RF_C_TxPwDiff > 0)
			{
				if( (byte0 + (u8)priv->RF_C_TxPwDiff) > 0x24)
					byte0 = 0x24 - priv->RF_C_TxPwDiff;
				if( (byte1 + (u8)priv->RF_C_TxPwDiff) > 0x24)
					byte1 = 0x24 - priv->RF_C_TxPwDiff;
			}
		}
	TxAGC = (byte1<<8) |byte0;
	write_nic_dword(dev, CCK_TXAGC, TxAGC);
#else
	#ifdef RTL8192E

	TxAGC = powerlevel;
	if(priv->bDynamicTxLowPower == true)//cosa 04282008 for cck long range
	{
		if(priv->CustomerID == RT_CID_819x_Netcore)
		TxAGC = 0x22;
	else
		TxAGC += priv->CckPwEnl;
	}
	if(TxAGC > 0x24)
		TxAGC = 0x24;
	rtl8192_setBBreg(dev, rTxAGC_CCK_Mcs32, bTxAGCRateCCK, TxAGC);
	#endif
#endif
}


void PHY_SetRF8256OFDMTxPower(struct net_device* dev, u8 powerlevel)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	//Joseph TxPower for 8192 testing
#ifdef RTL8190P
	u32				TxAGC1=0, TxAGC2=0, TxAGC2_tmp = 0;
	u8				i, byteVal1[4], byteVal2[4], byteVal3[4];

	if(priv->bDynamicTxHighPower == true)     //Add by Jacken 2008/03/06
	{
		TxAGC1 |= ((powerlevel<<24)|(powerlevel<<16)|(powerlevel<<8)|powerlevel);
		//for tx power track
		TxAGC2_tmp = TxAGC1;

		TxAGC1 += priv->MCSTxPowerLevelOriginalOffset[0];
		TxAGC2 =0x03030303;

		//for tx power track
		TxAGC2_tmp += priv->MCSTxPowerLevelOriginalOffset[1];
	}
	else
	{
		TxAGC1 |= ((powerlevel<<24)|(powerlevel<<16)|(powerlevel<<8)|powerlevel);
		TxAGC2 = TxAGC1;

		TxAGC1 += priv->MCSTxPowerLevelOriginalOffset[0];
		TxAGC2 += priv->MCSTxPowerLevelOriginalOffset[1];

		TxAGC2_tmp = TxAGC2;

	}
	for(i=0; i<4; i++)
	{
		byteVal1[i] = (u8)(  (TxAGC1 & (0xff<<(i*8))) >>(i*8) );
		if(byteVal1[i] > 0x24)
			byteVal1[i] = 0x24;
		byteVal2[i] = (u8)(  (TxAGC2 & (0xff<<(i*8))) >>(i*8) );
		if(byteVal2[i] > 0x24)
			byteVal2[i] = 0x24;

		//for tx power track
		byteVal3[i] = (u8)(  (TxAGC2_tmp & (0xff<<(i*8))) >>(i*8) );
		if(byteVal3[i] > 0x24)
			byteVal3[i] = 0x24;
	}

	if(priv->rf_type == RF_2T4R)	//Only 2T4R you have to care the Antenna Tx Power offset
	{	// check antenna C over the max index 0x24
		if(priv->RF_C_TxPwDiff > 0)
		{
			for(i=0; i<4; i++)
			{
				if( (byteVal1[i] + (u8)priv->RF_C_TxPwDiff) > 0x24)
					byteVal1[i] = 0x24 - priv->RF_C_TxPwDiff;
				if( (byteVal2[i] + (u8)priv->RF_C_TxPwDiff) > 0x24)
					byteVal2[i] = 0x24 - priv->RF_C_TxPwDiff;
				if( (byteVal3[i] + (u8)priv->RF_C_TxPwDiff) > 0x24)
					byteVal3[i] = 0x24 - priv->RF_C_TxPwDiff;
			}
		}
	}

	TxAGC1 = (byteVal1[3]<<24) | (byteVal1[2]<<16) |(byteVal1[1]<<8) |byteVal1[0];
	TxAGC2 = (byteVal2[3]<<24) | (byteVal2[2]<<16) |(byteVal2[1]<<8) |byteVal2[0];

	//for tx power track
	TxAGC2_tmp = (byteVal3[3]<<24) | (byteVal3[2]<<16) |(byteVal3[1]<<8) |byteVal3[0];
	priv->Pwr_Track = TxAGC2_tmp;
	//DbgPrint("TxAGC2_tmp = 0x%x\n", TxAGC2_tmp);

	//DbgPrint("TxAGC1/TxAGC2 = 0x%x/0x%x\n", TxAGC1, TxAGC2);
	write_nic_dword(dev, MCS_TXAGC, TxAGC1);
	write_nic_dword(dev, MCS_TXAGC+4, TxAGC2);
#else
#ifdef RTL8192E
	u32 writeVal, powerBase0, powerBase1, writeVal_tmp;
	u8 index = 0;
	u16 RegOffset[6] = {0xe00, 0xe04, 0xe10, 0xe14, 0xe18, 0xe1c};
	u8 byte0, byte1, byte2, byte3;

	powerBase0 = powerlevel + priv->LegacyHTTxPowerDiff;	//OFDM rates
	powerBase0 = (powerBase0<<24) | (powerBase0<<16) |(powerBase0<<8) |powerBase0;
	powerBase1 = powerlevel;							//MCS rates
	powerBase1 = (powerBase1<<24) | (powerBase1<<16) |(powerBase1<<8) |powerBase1;

	for(index=0; index<6; index++)
	{
		writeVal = priv->MCSTxPowerLevelOriginalOffset[index] + ((index<2)?powerBase0:powerBase1);
		byte0 = (u8)(writeVal & 0x7f);
		byte1 = (u8)((writeVal & 0x7f00)>>8);
		byte2 = (u8)((writeVal & 0x7f0000)>>16);
		byte3 = (u8)((writeVal & 0x7f000000)>>24);
		if(byte0 > 0x24)	// Max power index = 0x24
			byte0 = 0x24;
		if(byte1 > 0x24)
			byte1 = 0x24;
		if(byte2 > 0x24)
			byte2 = 0x24;
		if(byte3 > 0x24)
			byte3 = 0x24;

		if(index == 3)
		{
			writeVal_tmp = (byte3<<24) | (byte2<<16) |(byte1<<8) |byte0;
			priv->Pwr_Track = writeVal_tmp;
		}

		if(priv->bDynamicTxHighPower == true)     //Add by Jacken 2008/03/06  //when DM implement, add this
		{
			writeVal = 0x03030303;
		}
		else
		{
			writeVal = (byte3<<24) | (byte2<<16) |(byte1<<8) |byte0;
		}
		rtl8192_setBBreg(dev, RegOffset[index], 0x7f7f7f7f, writeVal);
	}

#endif
#endif
	return;
}

#define MAX_DOZE_WAITING_TIMES_9x 64
static bool
SetRFPowerState8190(
	struct net_device* dev,
	RT_RF_POWER_STATE	eRFPowerState
	)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	PRT_POWER_SAVE_CONTROL	pPSC = (PRT_POWER_SAVE_CONTROL)(&(priv->ieee80211->PowerSaveControl));
	bool bResult = true;
	//u8 eRFPath;
	u8	i = 0, QueueID = 0;
	//ptx_ring	head=NULL,tail=NULL;
	struct rtl8192_tx_ring  *ring = NULL;

	if(priv->SetRFPowerStateInProgress == true)
		return false;
	//RT_TRACE(COMP_PS, "===========> SetRFPowerState8190()!\n");
	priv->SetRFPowerStateInProgress = true;

	switch(priv->rf_chip)
	{
		case RF_8256:
		switch( eRFPowerState )
		{
			case eRfOn:
				//RT_TRACE(COMP_PS, "SetRFPowerState8190() eRfOn !\n");
						//RXTX enable control: On
					//for(eRFPath = 0; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
					//	PHY_SetRFReg(dev, (RF90_RADIO_PATH_E)eRFPath, 0x4, 0xC00, 0x2);
#ifdef RTL8190P
				if(priv->rf_type == RF_2T4R)
				{
					//enable RF-Chip A/B
					rtl8192_setBBreg(dev, rFPGA0_XA_RFInterfaceOE, BIT4, 0x1); // 0x860[4]
					//enable RF-Chip C/D
					rtl8192_setBBreg(dev, rFPGA0_XC_RFInterfaceOE, BIT4, 0x1); // 0x868[4]
					//analog to digital on
					rtl8192_setBBreg(dev, rFPGA0_AnalogParameter4, 0xf00, 0xf);// 0x88c[11:8]
					//digital to analog on
					rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x1e0, 0xf); // 0x880[8:5]
					//rx antenna on
					rtl8192_setBBreg(dev, rOFDM0_TRxPathEnable, 0xf, 0xf);// 0xc04[3:0]
					//rx antenna on
					rtl8192_setBBreg(dev, rOFDM1_TRxPathEnable, 0xf, 0xf);// 0xd04[3:0]
					//analog to digital part2 on
					rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x1e00, 0xf); // 0x880[12:9]
				}
				else if(priv->rf_type == RF_1T2R)	//RF-C, RF-D
				{
					//enable RF-Chip C/D
					rtl8192_setBBreg(dev, rFPGA0_XC_RFInterfaceOE, BIT4, 0x1); // 0x868[4]
					//analog to digital on
					rtl8192_setBBreg(dev, rFPGA0_AnalogParameter4, 0xc00, 0x3);// 0x88c[11:10]
					//digital to analog on
					rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x180, 0x3); // 0x880[8:7]
					//rx antenna on
					rtl8192_setBBreg(dev, rOFDM0_TRxPathEnable, 0xc, 0x3);// 0xc04[3:2]
					//rx antenna on
					rtl8192_setBBreg(dev, rOFDM1_TRxPathEnable, 0xc, 0x3);// 0xd04[3:2]
					//analog to digital part2 on
					rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x1800, 0x3); // 0x880[12:11]
				}
				else if(priv->rf_type == RF_1T1R)	//RF-C
				{
					//enable RF-Chip C/D
					rtl8192_setBBreg(dev, rFPGA0_XC_RFInterfaceOE, BIT4, 0x1); // 0x868[4]
					//analog to digital on
					rtl8192_setBBreg(dev, rFPGA0_AnalogParameter4, 0x400, 0x1);// 0x88c[10]
					//digital to analog on
					rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x80, 0x1); // 0x880[7]
					//rx antenna on
					rtl8192_setBBreg(dev, rOFDM0_TRxPathEnable, 0x4, 0x1);// 0xc04[2]
					//rx antenna on
					rtl8192_setBBreg(dev, rOFDM1_TRxPathEnable, 0x4, 0x1);// 0xd04[2]
					//analog to digital part2 on
					rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x800, 0x1); // 0x880[11]
				}

#elif defined RTL8192E
				// turn on RF
				if((priv->ieee80211->eRFPowerState == eRfOff) && RT_IN_PS_LEVEL(pPSC, RT_RF_OFF_LEVL_HALT_NIC))
				{ // The current RF state is OFF and the RF OFF level is halting the NIC, re-initialize the NIC.
					bool rtstatus = true;
					u32 InitilizeCount = 3;
					do
					{
						InitilizeCount--;
						priv->RegRfOff = false;
						rtstatus = NicIFEnableNIC(dev);
					}while( (rtstatus != true) &&(InitilizeCount >0) );

					if(rtstatus != true)
					{
						RT_TRACE(COMP_ERR,"%s():Initialize Adapter fail,return\n",__FUNCTION__);
						priv->SetRFPowerStateInProgress = false;
						return false;
					}

					RT_CLEAR_PS_LEVEL(pPSC, RT_RF_OFF_LEVL_HALT_NIC);
				} else {
					write_nic_byte(dev, ANAPAR, 0x37);//160MHz
					//write_nic_byte(dev, MacBlkCtrl, 0x17); // 0x403
					mdelay(1);
					//enable clock 80/88 MHz
					rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x4, 0x1); // 0x880[2]
					priv->bHwRfOffAction = 0;
					//}

					//RF-A, RF-B
					//enable RF-Chip A/B
					rtl8192_setBBreg(dev, rFPGA0_XA_RFInterfaceOE, BIT4, 0x1);		// 0x860[4]
					//analog to digital on
					rtl8192_setBBreg(dev, rFPGA0_AnalogParameter4, 0x300, 0x3);// 0x88c[9:8]
					//digital to analog on
					rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x18, 0x3); // 0x880[4:3]
					//rx antenna on
					rtl8192_setBBreg(dev, rOFDM0_TRxPathEnable, 0x3, 0x3);// 0xc04[1:0]
					//rx antenna on
					rtl8192_setBBreg(dev, rOFDM1_TRxPathEnable, 0x3, 0x3);// 0xd04[1:0]
					//analog to digital part2 on
					rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x60, 0x3); 	// 0x880[6:5]

					// Baseband reset 2008.09.30 add
					//write_nic_byte(dev, BB_RESET, (read_nic_byte(dev, BB_RESET)|BIT0));

				//2 	AFE
					// 2008.09.30 add
					//rtl8192_setBBreg(dev, rFPGA0_AnalogParameter2, 0x20000000, 0x1); // 0x884
					//analog to digital part2 on
					//rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x60, 0x3);		// 0x880[6:5]


					//digital to analog on
					//rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x98, 0x13); // 0x880[4:3]
					//analog to digital on
					//rtl8192_setBBreg(dev, rFPGA0_AnalogParameter4, 0xf03, 0xf03);// 0x88c[9:8]
					//rx antenna on
					//PHY_SetBBReg(dev, rOFDM0_TRxPathEnable, 0x3, 0x3);// 0xc04[1:0]
					//rx antenna on 2008.09.30 mark
					//PHY_SetBBReg(dev, rOFDM1_TRxPathEnable, 0x3, 0x3);// 0xd04[1:0]

				//2 	RF
					//enable RF-Chip A/B
					//rtl8192_setBBreg(dev, rFPGA0_XA_RFInterfaceOE, BIT4, 0x1);		// 0x860[4]
					//rtl8192_setBBreg(dev, rFPGA0_XB_RFInterfaceOE, BIT4, 0x1);		// 0x864[4]

				}

				#endif
						break;

				//
				// In current solution, RFSleep=RFOff in order to save power under 802.11 power save.
				// By Bruce, 2008-01-16.
				//
			case eRfSleep:
			{
				// HW setting had been configured with deeper mode.
				if(priv->ieee80211->eRFPowerState == eRfOff)
					break;

				// Update current RF state variable.
				//priv->ieee80211->eRFPowerState = eRFPowerState;

				//if (pPSC->bLeisurePs)
				{
					for(QueueID = 0, i = 0; QueueID < MAX_TX_QUEUE; )
					{
							ring = &priv->tx_ring[QueueID];

							if(skb_queue_len(&ring->queue) == 0)
							{
								QueueID++;
								continue;
							}
							else
							{
								RT_TRACE((COMP_POWER|COMP_RF), "eRf Off/Sleep: %d times TcbBusyQueue[%d] !=0 before doze!\n", (i+1), QueueID);
								udelay(10);
								i++;
							}

							if(i >= MAX_DOZE_WAITING_TIMES_9x)
							{
								RT_TRACE(COMP_POWER, "\n\n\n TimeOut!! SetRFPowerState8190(): eRfOff: %d times TcbBusyQueue[%d] != 0 !!!\n\n\n", MAX_DOZE_WAITING_TIMES_9x, QueueID);
								break;
							}
						}
				}

				//if(Adapter->HardwareType == HARDWARE_TYPE_RTL8190P)
#ifdef RTL8190P
				{
					PHY_SetRtl8190pRfOff(dev);
				}
				//else if(Adapter->HardwareType == HARDWARE_TYPE_RTL8192E)
#elif defined RTL8192E
				{
					PHY_SetRtl8192eRfOff(dev);
				}
#endif
			}
								break;

			case eRfOff:
				//RT_TRACE(COMP_PS, "SetRFPowerState8190() eRfOff/Sleep !\n");

				// Update current RF state variable.
				//priv->ieee80211->eRFPowerState = eRFPowerState;

				//
				// Disconnect with Any AP or STA.
				//
				for(QueueID = 0, i = 0; QueueID < MAX_TX_QUEUE; )
				{
					ring = &priv->tx_ring[QueueID];

					if(skb_queue_len(&ring->queue) == 0)
						{
							QueueID++;
							continue;
						}
						else
						{
							RT_TRACE(COMP_POWER,
							"eRf Off/Sleep: %d times TcbBusyQueue[%d] !=0 before doze!\n", (i+1), QueueID);
							udelay(10);
							i++;
						}

						if(i >= MAX_DOZE_WAITING_TIMES_9x)
						{
							RT_TRACE(COMP_POWER, "\n\n\n SetZebraRFPowerState8185B(): eRfOff: %d times TcbBusyQueue[%d] != 0 !!!\n\n\n", MAX_DOZE_WAITING_TIMES_9x, QueueID);
							break;
						}
					}

				//if(Adapter->HardwareType == HARDWARE_TYPE_RTL8190P)
#if defined RTL8190P
				{
					PHY_SetRtl8190pRfOff(dev);
				}
				//else if(Adapter->HardwareType == HARDWARE_TYPE_RTL8192E)
#elif defined RTL8192E
				{
					//if(pPSC->RegRfPsLevel & RT_RF_OFF_LEVL_HALT_NIC && !RT_IN_PS_LEVEL(pPSC, RT_RF_OFF_LEVL_HALT_NIC) && priv->ieee80211->RfOffReason > RF_CHANGE_BY_PS)
					if (pPSC->RegRfPsLevel & RT_RF_OFF_LEVL_HALT_NIC && !RT_IN_PS_LEVEL(pPSC, RT_RF_OFF_LEVL_HALT_NIC))
					{ // Disable all components.
						//
						// Note:
						//	NicIFSetLinkStatus is a big problem when we indicate the status to OS,
						//	the OS(XP) will reset. But now, we cnnot find why the NIC is hard to receive
						//	packets after RF ON. Just keep this function here and still work to find out the root couse.
						//	By Bruce, 2009-05-01.
						//
						//NicIFSetLinkStatus( Adapter, RT_MEDIA_DISCONNECT );
						//if HW radio of , need to indicate scan complete first for not be reset.
						//if(MgntScanInProgress(pMgntInfo))
						//	MgntResetScanProcess( Adapter );

						// <1> Disable Interrupt
						//rtl8192_irq_disable(dev);
						// <2> Stop all timer
						//MgntCancelAllTimer(Adapter);
						// <3> Disable Adapter
						//NicIFHaltAdapter(Adapter, false);
						NicIFDisableNIC(dev);
						RT_SET_PS_LEVEL(pPSC, RT_RF_OFF_LEVL_HALT_NIC);
					}
					else if (!(pPSC->RegRfPsLevel & RT_RF_OFF_LEVL_HALT_NIC))
					{ // Normal case.
				  		// IPS should go to this.
						PHY_SetRtl8192eRfOff(dev);
					}
				}
#else
				else
				{
					RT_TRACE(COMP_DBG,DBG_TRACE,("It is not 8190Pci and 8192PciE \n"));
				}
				#endif

					break;

			default:
					bResult = false;
					RT_TRACE(COMP_ERR, "SetRFPowerState8190(): unknow state to set: 0x%X!!!\n", eRFPowerState);
					break;
		}

		break;

		default:
			RT_TRACE(COMP_ERR, "SetRFPowerState8190(): Unknown RF type\n");
			break;
	}

	if(bResult)
	{
		// Update current RF state variable.
		priv->ieee80211->eRFPowerState = eRFPowerState;
	}

	//printk("%s()priv->ieee80211->eRFPowerState:%s\n" ,__func__,priv->ieee80211->eRFPowerState == eRfOn ? "On" : "Off");
	priv->SetRFPowerStateInProgress = false;
	//RT_TRACE(COMP_PS, "<=========== SetRFPowerState8190() bResult = %d!\n", bResult);
	return bResult;
}



//
//	Description:
//		Change RF power state.
//
//	Assumption:
//		This function must be executed in re-schdulable context,
//		ie. PASSIVE_LEVEL.
//
//	050823, by rcnjko.
//
static bool
SetRFPowerState(
	struct net_device* dev,
	RT_RF_POWER_STATE	eRFPowerState
	)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	bool bResult = false;

	RT_TRACE(COMP_RF,"---------> SetRFPowerState(): eRFPowerState(%d)\n", eRFPowerState);
#ifdef RTL8192E
	if(eRFPowerState == priv->ieee80211->eRFPowerState && priv->bHwRfOffAction == 0)
#else
	if(eRFPowerState == priv->ieee80211->eRFPowerState)
#endif
	{
		RT_TRACE(COMP_POWER, "<--------- SetRFPowerState(): discard the request for eRFPowerState(%d) is the same.\n", eRFPowerState);
		return bResult;
	}

	bResult = SetRFPowerState8190(dev, eRFPowerState);

	RT_TRACE(COMP_POWER, "<--------- SetRFPowerState(): bResult(%d)\n", bResult);

	return bResult;
}

static void
MgntDisconnectIBSS(
	struct net_device* dev
)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	//RT_OP_MODE	OpMode;
	u8			i;
	bool	bFilterOutNonAssociatedBSSID = false;

	//IEEE80211_DEBUG(IEEE80211_DL_TRACE, "XXXXXXXXXX MgntDisconnect IBSS\n");

	priv->ieee80211->state = IEEE80211_NOLINK;

//	PlatformZeroMemory( pMgntInfo->Bssid, 6 );
	for(i=0;i<6;i++)  priv->ieee80211->current_network.bssid[i]= 0x55;
	priv->OpMode = RT_OP_MODE_NO_LINK;
	write_nic_word(dev, BSSIDR, ((u16*)priv->ieee80211->current_network.bssid)[0]);
	write_nic_dword(dev, BSSIDR+2, ((u32*)(priv->ieee80211->current_network.bssid+2))[0]);
	{
			RT_OP_MODE	OpMode = priv->OpMode;
			//LED_CTL_MODE	LedAction = LED_CTL_NO_LINK;
			u8	btMsr = read_nic_byte(dev, MSR);

			btMsr &= 0xfc;

			switch(OpMode)
			{
			case RT_OP_MODE_INFRASTRUCTURE:
				btMsr |= MSR_LINK_MANAGED;
				//LedAction = LED_CTL_LINK;
				break;

			case RT_OP_MODE_IBSS:
				btMsr |= MSR_LINK_ADHOC;
				// led link set seperate
				break;

			case RT_OP_MODE_AP:
				btMsr |= MSR_LINK_MASTER;
				//LedAction = LED_CTL_LINK;
				break;

			default:
				btMsr |= MSR_LINK_NONE;
				break;
			}

			write_nic_byte(dev, MSR, btMsr);

			// LED control
			//Adapter->HalFunc.LedControlHandler(Adapter, LedAction);
	}
	ieee80211_stop_send_beacons(priv->ieee80211);

	// If disconnect, clear RCR CBSSID bit
	bFilterOutNonAssociatedBSSID = false;
	{
			u32 RegRCR, Type;
			Type = bFilterOutNonAssociatedBSSID;
			RegRCR = read_nic_dword(dev,RCR);
			priv->ReceiveConfig = RegRCR;
			if (Type == true)
				RegRCR |= (RCR_CBSSID);
			else if (Type == false)
				RegRCR &= (~RCR_CBSSID);

			{
				write_nic_dword(dev, RCR,RegRCR);
				priv->ReceiveConfig = RegRCR;
			}

		}
	//MgntIndicateMediaStatus( Adapter, RT_MEDIA_DISCONNECT, GENERAL_INDICATE );
	notify_wx_assoc_event(priv->ieee80211);

}

static void
MlmeDisassociateRequest(
	struct net_device* dev,
	u8* 		asSta,
	u8			asRsn
	)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8 i;

	RemovePeerTS(priv->ieee80211, asSta);

	SendDisassociation( priv->ieee80211, asSta, asRsn );

	if(memcpy(priv->ieee80211->current_network.bssid,asSta,6) == NULL)
	{
		//ShuChen TODO: change media status.
		//ShuChen TODO: What to do when disassociate.
		priv->ieee80211->state = IEEE80211_NOLINK;
		//pMgntInfo->AsocTimestamp = 0;
		for(i=0;i<6;i++)  priv->ieee80211->current_network.bssid[i] = 0x22;
//		pMgntInfo->mBrates.Length = 0;
//		Adapter->HalFunc.SetHwRegHandler( Adapter, HW_VAR_BASIC_RATE, (pu1Byte)(&pMgntInfo->mBrates) );
		priv->OpMode = RT_OP_MODE_NO_LINK;
		{
			RT_OP_MODE	OpMode = priv->OpMode;
			//LED_CTL_MODE	LedAction = LED_CTL_NO_LINK;
			u8 btMsr = read_nic_byte(dev, MSR);

			btMsr &= 0xfc;

			switch(OpMode)
			{
			case RT_OP_MODE_INFRASTRUCTURE:
				btMsr |= MSR_LINK_MANAGED;
				//LedAction = LED_CTL_LINK;
				break;

			case RT_OP_MODE_IBSS:
				btMsr |= MSR_LINK_ADHOC;
				// led link set seperate
				break;

			case RT_OP_MODE_AP:
				btMsr |= MSR_LINK_MASTER;
				//LedAction = LED_CTL_LINK;
				break;

			default:
				btMsr |= MSR_LINK_NONE;
				break;
			}

			write_nic_byte(dev, MSR, btMsr);

			// LED control
			//Adapter->HalFunc.LedControlHandler(Adapter, LedAction);
		}
		ieee80211_disassociate(priv->ieee80211);

		write_nic_word(dev, BSSIDR, ((u16*)priv->ieee80211->current_network.bssid)[0]);
		write_nic_dword(dev, BSSIDR+2, ((u32*)(priv->ieee80211->current_network.bssid+2))[0]);

	}

}


static void
MgntDisconnectAP(
	struct net_device* dev,
	u8 asRsn
)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	bool bFilterOutNonAssociatedBSSID = false;

//
// Commented out by rcnjko, 2005.01.27:
// I move SecClearAllKeys() to MgntActSet_802_11_DISASSOCIATE().
//
//	//2004/09/15, kcwu, the key should be cleared, or the new handshaking will not success
//	SecClearAllKeys(Adapter);

	// In WPA WPA2 need to Clear all key ... because new key will set after new handshaking.
#ifdef TO_DO
	if(   pMgntInfo->SecurityInfo.AuthMode > RT_802_11AuthModeAutoSwitch ||
		(pMgntInfo->bAPSuportCCKM && pMgntInfo->bCCX8021xenable) )	// In CCKM mode will Clear key
	{
		SecClearAllKeys(Adapter);
		RT_TRACE(COMP_SEC, DBG_LOUD,("======>CCKM clear key..."))
	}
#endif
	// If disconnect, clear RCR CBSSID bit
	bFilterOutNonAssociatedBSSID = false;
	{
			u32 RegRCR, Type;

			Type = bFilterOutNonAssociatedBSSID;
			//Adapter->HalFunc.GetHwRegHandler(Adapter, HW_VAR_RCR, (pu1Byte)(&RegRCR));
			RegRCR = read_nic_dword(dev,RCR);
			priv->ReceiveConfig = RegRCR;

			if (Type == true)
				RegRCR |= (RCR_CBSSID);
			else if (Type == false)
				RegRCR &= (~RCR_CBSSID);

			write_nic_dword(dev, RCR,RegRCR);
			priv->ReceiveConfig = RegRCR;


	}
	// 2004.10.11, by rcnjko.
	//MlmeDisassociateRequest( Adapter, pMgntInfo->Bssid, disas_lv_ss );
	MlmeDisassociateRequest( dev, priv->ieee80211->current_network.bssid, asRsn );

	priv->ieee80211->state = IEEE80211_NOLINK;
	//pMgntInfo->AsocTimestamp = 0;
}


static bool
MgntDisconnect(
	struct net_device* dev,
	u8 asRsn
)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	//
	// Schedule an workitem to wake up for ps mode, 070109, by rcnjko.
	//
#ifdef TO_DO
	if(pMgntInfo->mPss != eAwake)
	{
		//
		// Using AwkaeTimer to prevent mismatch ps state.
		// In the timer the state will be changed according to the RF is being awoke or not. By Bruce, 2007-10-31.
		//
		// PlatformScheduleWorkItem( &(pMgntInfo->AwakeWorkItem) );
		PlatformSetTimer( Adapter, &(pMgntInfo->AwakeTimer), 0 );
	}
#endif
	// Follow 8180 AP mode, 2005.05.30, by rcnjko.
#ifdef TO_DO
	if(pMgntInfo->mActingAsAp)
	{
		RT_TRACE(COMP_MLME, DBG_LOUD, ("MgntDisconnect() ===> AP_DisassociateAllStation\n"));
		AP_DisassociateAllStation(Adapter, unspec_reason);
		return TRUE;
	}
#endif
	// Indication of disassociation event.
	//DrvIFIndicateDisassociation(Adapter, asRsn);

	// In adhoc mode, update beacon frame.
	if( priv->ieee80211->state == IEEE80211_LINKED )
	{
		if( priv->ieee80211->iw_mode == IW_MODE_ADHOC )
		{
			//RT_TRACE(COMP_MLME, "MgntDisconnect() ===> MgntDisconnectIBSS\n");
			MgntDisconnectIBSS(dev);
		}
		if( priv->ieee80211->iw_mode == IW_MODE_INFRA )
		{
			// We clear key here instead of MgntDisconnectAP() because that
			// MgntActSet_802_11_DISASSOCIATE() is an interface called by OS,
			// e.g. OID_802_11_DISASSOCIATE in Windows while as MgntDisconnectAP() is
			// used to handle disassociation related things to AP, e.g. send Disassoc
			// frame to AP.  2005.01.27, by rcnjko.
			//IEEE80211_DEBUG(IEEE80211_DL_TRACE,"MgntDisconnect() ===> MgntDisconnectAP\n");
			MgntDisconnectAP(dev, asRsn);
		}

		// Inidicate Disconnect, 2005.02.23, by rcnjko.
		//MgntIndicateMediaStatus( Adapter, RT_MEDIA_DISCONNECT, GENERAL_INDICATE);
	}

	return true;
}

//
//	Description:
//		Chang RF Power State.
//		Note that, only MgntActSet_RF_State() is allowed to set HW_VAR_RF_STATE.
//
//	Assumption:
//		PASSIVE LEVEL.
//
bool
MgntActSet_RF_State(
	struct net_device* dev,
	RT_RF_POWER_STATE	StateToSet,
	RT_RF_CHANGE_SOURCE ChangeSource
	)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	bool 			bActionAllowed = false;
	bool 			bConnectBySSID = false;
	RT_RF_POWER_STATE	rtState;
	u16					RFWaitCounter = 0;
	unsigned long flag;
	RT_TRACE(COMP_POWER, "===>MgntActSet_RF_State(): StateToSet(%d)\n",StateToSet);

	//1//
	//1//<1>Prevent the race condition of RF state change.
	//1//
	// Only one thread can change the RF state at one time, and others should wait to be executed. By Bruce, 2007-11-28.

	while(true)
	{
		spin_lock_irqsave(&priv->rf_ps_lock,flag);
		if(priv->RFChangeInProgress)
		{
			spin_unlock_irqrestore(&priv->rf_ps_lock,flag);
			RT_TRACE(COMP_POWER, "MgntActSet_RF_State(): RF Change in progress! Wait to set..StateToSet(%d).\n", StateToSet);

			// Set RF after the previous action is done.
			while(priv->RFChangeInProgress)
			{
				RFWaitCounter ++;
				RT_TRACE(COMP_POWER, "MgntActSet_RF_State(): Wait 1 ms (%d times)...\n", RFWaitCounter);
				udelay(1000); // 1 ms

				// Wait too long, return FALSE to avoid to be stuck here.
				if(RFWaitCounter > 100)
				{
					RT_TRACE(COMP_ERR, "MgntActSet_RF_State(): Wait too logn to set RF\n");
					// TODO: Reset RF state?
					return false;
				}
			}
		}
		else
		{
			priv->RFChangeInProgress = true;
			spin_unlock_irqrestore(&priv->rf_ps_lock,flag);
			break;
		}
	}

	rtState = priv->ieee80211->eRFPowerState;

	switch(StateToSet)
	{
	case eRfOn:
		//
		// Turn On RF no matter the IPS setting because we need to update the RF state to Ndis under Vista, or
		// the Windows does not allow the driver to perform site survey any more. By Bruce, 2007-10-02.
		//

		priv->ieee80211->RfOffReason &= (~ChangeSource);

		if(! priv->ieee80211->RfOffReason)
		{
			priv->ieee80211->RfOffReason = 0;
			bActionAllowed = true;


			if(rtState == eRfOff && ChangeSource >=RF_CHANGE_BY_HW )
			{
				bConnectBySSID = true;
			}
		}
		else
			RT_TRACE(COMP_POWER, "MgntActSet_RF_State - eRfon reject pMgntInfo->RfOffReason= 0x%x, ChangeSource=0x%X\n", priv->ieee80211->RfOffReason, ChangeSource);

		break;

	case eRfOff:

			if (priv->ieee80211->RfOffReason > RF_CHANGE_BY_IPS)
			{
				//
				// 060808, Annie:
				// Disconnect to current BSS when radio off. Asked by QuanTa.
				//
				// Set all link status falg, by Bruce, 2007-06-26.
				//MgntActSet_802_11_DISASSOCIATE( Adapter, disas_lv_ss );
				MgntDisconnect(dev, disas_lv_ss);

				// Clear content of bssDesc[] and bssDesc4Query[] to avoid reporting old bss to UI.
				// 2007.05.28, by shien chang.

			}


		priv->ieee80211->RfOffReason |= ChangeSource;
		bActionAllowed = true;
		break;

	case eRfSleep:
		priv->ieee80211->RfOffReason |= ChangeSource;
		bActionAllowed = true;
		break;

	default:
		break;
	}

	if(bActionAllowed)
	{
		RT_TRACE(COMP_POWER, "MgntActSet_RF_State(): Action is allowed.... StateToSet(%d), RfOffReason(%#X)\n", StateToSet, priv->ieee80211->RfOffReason);
				// Config HW to the specified mode.
		SetRFPowerState(dev, StateToSet);
		// Turn on RF.
		if(StateToSet == eRfOn)
		{
			//Adapter->HalFunc.HalEnableRxHandler(Adapter);
			if(bConnectBySSID)
			{
				//MgntActSet_802_11_SSID(Adapter, Adapter->MgntInfo.Ssid.Octet, Adapter->MgntInfo.Ssid.Length, TRUE );
			}
		}
		// Turn off RF.
		else if(StateToSet == eRfOff)
		{
			//Adapter->HalFunc.HalDisableRxHandler(Adapter);
		}
	}
	else
	{
		RT_TRACE(COMP_POWER, "MgntActSet_RF_State(): Action is rejected.... StateToSet(%d), ChangeSource(%#X), RfOffReason(%#X)\n", StateToSet, ChangeSource, priv->ieee80211->RfOffReason);
	}

	// Release RF spinlock
	spin_lock_irqsave(&priv->rf_ps_lock,flag);
	priv->RFChangeInProgress = false;
	spin_unlock_irqrestore(&priv->rf_ps_lock,flag);

	RT_TRACE(COMP_POWER, "<===MgntActSet_RF_State()\n");
	return bActionAllowed;
}


