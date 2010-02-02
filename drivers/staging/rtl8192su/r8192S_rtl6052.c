/******************************************************************************
 *
 *     (c) Copyright  2008, RealTEK Technologies Inc. All Rights Reserved.
 *
 * Module:	HalRf6052.c	( Source C File)
 *
 * Note:	Provide RF 6052 series relative API.
 *
 * Function:
 *
 * Export:
 *
 * Abbrev:
 *
 * History:
 * Data			Who		Remark
 *
 * 09/25/2008	MHC		Create initial version.
 * 11/05/2008 	MHC		Add API for tw power setting.
 *
 *
******************************************************************************/
#include "r8192U.h"
#include "r8192S_rtl6052.h"

#include "r8192S_hw.h"
#include "r8192S_phyreg.h"
#include "r8192S_phy.h"


/*---------------------------Define Local Constant---------------------------*/
// Define local structure for debug!!!!!
typedef struct RF_Shadow_Compare_Map {
	// Shadow register value
	u32		Value;
	// Compare or not flag
	u8		Compare;
	// Record If it had ever modified unpredicted
	u8		ErrorOrNot;
	// Recorver Flag
	u8		Recorver;
	//
	u8		Driver_Write;
}RF_SHADOW_T;
/*---------------------------Define Local Constant---------------------------*/


/*------------------------Define global variable-----------------------------*/
/*------------------------Define global variable-----------------------------*/




/*---------------------Define local function prototype-----------------------*/
void phy_RF6052_Config_HardCode(struct net_device* dev);

RT_STATUS phy_RF6052_Config_ParaFile(struct net_device* dev);
/*---------------------Define local function prototype-----------------------*/

/*------------------------Define function prototype--------------------------*/
extern void RF_ChangeTxPath(struct net_device* dev,  u16 DataRate);

/*------------------------Define function prototype--------------------------*/

/*------------------------Define local variable------------------------------*/
// 2008/11/20 MH For Debug only, RF
static	RF_SHADOW_T	RF_Shadow[RF6052_MAX_PATH][RF6052_MAX_REG];// = {{0}};//FIXLZM
/*------------------------Define local variable------------------------------*/

/*------------------------Define function prototype--------------------------*/
/*-----------------------------------------------------------------------------
 * Function:	RF_ChangeTxPath
 *
 * Overview:	For RL6052, we must change some RF settign for 1T or 2T.
 *
 * Input:		u16 DataRate		// 0x80-8f, 0x90-9f
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 09/25/2008 	MHC		Create Version 0.
 *						Firmwaer support the utility later.
 *
 *---------------------------------------------------------------------------*/
extern void RF_ChangeTxPath(struct net_device* dev,  u16 DataRate)
{
}	/* RF_ChangeTxPath */


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
void PHY_RF6052SetBandwidth(struct net_device* dev, HT_CHANNEL_WIDTH Bandwidth)	//20M or 40M
{
	//u8				eRFPath;
	//struct r8192_priv 	*priv = ieee80211_priv(dev);


	//if (priv->card_8192 == NIC_8192SE)
	{
		switch(Bandwidth)
		{
			case HT_CHANNEL_WIDTH_20:
				//if (priv->card_8192_version >= VERSION_8192S_BCUT)
				//	rtl8192_setBBreg(dev, rFPGA0_AnalogParameter2, 0xff, 0x58);

				rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E)RF90_PATH_A, RF_CHNLBW, BIT10|BIT11, 0x01);
				break;
			case HT_CHANNEL_WIDTH_20_40:
				//if (priv->card_8192_version >= VERSION_8192S_BCUT)
				//	rtl8192_setBBreg(dev, rFPGA0_AnalogParameter2, 0xff, 0x18);

				rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E)RF90_PATH_A, RF_CHNLBW, BIT10|BIT11, 0x00);
				break;
			default:
				RT_TRACE(COMP_DBG, "PHY_SetRF6052Bandwidth(): unknown Bandwidth: %#X\n",Bandwidth);
				break;
		}
	}
//	else
}


/*-----------------------------------------------------------------------------
 * Function:	PHY_RF6052SetCckTxPower
 *
 * Overview:
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/05/2008 	MHC		Simulate 8192series..
 *
 *---------------------------------------------------------------------------*/
extern void PHY_RF6052SetCckTxPower(struct net_device* dev, u8	powerlevel)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u32				TxAGC=0;

	if(priv->ieee80211->scanning == 1)
		TxAGC = 0x3f;
	else if(priv->bDynamicTxLowPower == true)//cosa 04282008 for cck long range
		TxAGC = 0x22;
	else
		TxAGC = powerlevel;

	//cosa add for lenovo, to pass the safety spec, don't increase power index for different rates.
	if(priv->bIgnoreDiffRateTxPowerOffset)
		TxAGC = powerlevel;

	if(TxAGC > RF6052_MAX_TX_PWR)
		TxAGC = RF6052_MAX_TX_PWR;

	//printk("CCK PWR= %x\n", TxAGC);
	rtl8192_setBBreg(dev, rTxAGC_CCK_Mcs32, bTxAGCRateCCK, TxAGC);

}	/* PHY_RF6052SetCckTxPower */



/*-----------------------------------------------------------------------------
 * Function:	PHY_RF6052SetOFDMTxPower
 *
 * Overview:	For legacy and HY OFDM, we must read EEPROM TX power index for
 *			different channel and read original value in TX power register area from
 *			0xe00. We increase offset and original value to be correct tx pwr.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/05/2008 	MHC		Simulate 8192 series method.
* 01/06/2009	MHC		1. Prevent Path B tx power overflow or underflow dure to
 *						A/B pwr difference or legacy/HT pwr diff.
 *						2. We concern with path B legacy/HT OFDM difference.
 * 01/22/2009	MHC		Support new EPRO format from SD3.
 *---------------------------------------------------------------------------*/
 #if 1
extern void PHY_RF6052SetOFDMTxPower(struct net_device* dev, u8 powerlevel)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u32 	writeVal, powerBase0, powerBase1;
	u8 	index = 0;
	u16 	RegOffset[6] = {0xe00, 0xe04, 0xe10, 0xe14, 0xe18, 0xe1c};
	//u8 	byte0, byte1, byte2, byte3;
	u8    Channel = priv->ieee80211->current_network.channel;
	u8	rfa_pwr[4];
	u8	rfa_lower_bound = 0, rfa_upper_bound = 0 /*, rfa_htpwr, rfa_legacypwr*/;
	u8	i;
	u8	rf_pwr_diff = 0;
	u8	Legacy_pwrdiff=0, HT20_pwrdiff=0, BandEdge_Pwrdiff=0;
	u8	ofdm_bandedge_chnl_low=0, ofdm_bandedge_chnl_high=0;


	// We only care about the path A for legacy.
	if (priv->EEPROMVersion != 2)
	powerBase0 = powerlevel + (priv->LegacyHTTxPowerDiff & 0xf);
	else if (priv->EEPROMVersion == 2)	// Defined by SD1 Jong
	{
		//
		// 2009/01/21 MH Support new EEPROM format from SD3 requirement
		//
		Legacy_pwrdiff = priv->TxPwrLegacyHtDiff[RF90_PATH_A][Channel-1];
		// For legacy OFDM, tx pwr always > HT OFDM pwr. We do not care Path B
		// legacy OFDM pwr diff. NO BB register to notify HW.
		powerBase0 = powerlevel + Legacy_pwrdiff;
		//RTPRINT(FPHY, PHY_TXPWR, (" [LagacyToHT40 pwr diff = %d]\n", Legacy_pwrdiff));

		// Band Edge scheme is enabled for FCC mode
		if (priv->TxPwrbandEdgeFlag == 1/* && pHalData->ChannelPlan == 0*/)
		{
			ofdm_bandedge_chnl_low = 1;
			ofdm_bandedge_chnl_high = 11;
			BandEdge_Pwrdiff = 0;
			if (Channel <= ofdm_bandedge_chnl_low)
				BandEdge_Pwrdiff = priv->TxPwrbandEdgeLegacyOfdm[RF90_PATH_A][0];
			else if (Channel >= ofdm_bandedge_chnl_high)
			{
				BandEdge_Pwrdiff = priv->TxPwrbandEdgeLegacyOfdm[RF90_PATH_A][1];
			}
			powerBase0 -= BandEdge_Pwrdiff;
			if (Channel <= ofdm_bandedge_chnl_low || Channel >= ofdm_bandedge_chnl_high)
			{
				//RTPRINT(FPHY, PHY_TXPWR, (" [OFDM band-edge channel = %d, pwr diff = %d]\n",
				//Channel, BandEdge_Pwrdiff));
			}
		}
		//RTPRINT(FPHY, PHY_TXPWR, (" [OFDM power base index = 0x%x]\n", powerBase0));
	}
	powerBase0 = (powerBase0<<24) | (powerBase0<<16) |(powerBase0<<8) |powerBase0;

	//MCS rates
	if(priv->EEPROMVersion == 2)
	{
		//Cosa add for new EEPROM content. 02102009

		//Check HT20 to HT40 diff
		if (priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20)
		{
			// HT 20<->40 pwr diff
			HT20_pwrdiff = priv->TxPwrHt20Diff[RF90_PATH_A][Channel-1];

			// Calculate Antenna pwr diff
			if (HT20_pwrdiff < 8)	// 0~+7
				powerlevel += HT20_pwrdiff;
			else				// index8-15=-8~-1
				powerlevel -= (16-HT20_pwrdiff);

			//RTPRINT(FPHY, PHY_TXPWR, (" [HT20 to HT40 pwrdiff = %d]\n", HT20_pwrdiff));
			//RTPRINT(FPHY, PHY_TXPWR, (" [MCS power base index = 0x%x]\n", powerlevel));
		}

		// Band Edge scheme is enabled for FCC mode
		if (priv->TxPwrbandEdgeFlag == 1/* && pHalData->ChannelPlan == 0*/)
		{
			BandEdge_Pwrdiff = 0;
			if (priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20_40)
			{
				if (Channel <= 3)
					BandEdge_Pwrdiff = priv->TxPwrbandEdgeHt40[RF90_PATH_A][0];
				else if (Channel >= 9)
					BandEdge_Pwrdiff = priv->TxPwrbandEdgeHt40[RF90_PATH_A][1];
				if (Channel <= 3 || Channel >= 9)
				{
					//RTPRINT(FPHY, PHY_TXPWR, (" [HT40 band-edge channel = %d, pwr diff = %d]\n",
					//Channel, BandEdge_Pwrdiff));
				}
			}
			else if (priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20)
			{
				if (Channel <= 1)
					BandEdge_Pwrdiff = priv->TxPwrbandEdgeHt20[RF90_PATH_A][0];
				else if (Channel >= 11)
					BandEdge_Pwrdiff = priv->TxPwrbandEdgeHt20[RF90_PATH_A][1];
				if (Channel <= 1 || Channel >= 11)
				{
					//RTPRINT(FPHY, PHY_TXPWR, (" [HT20 band-edge channel = %d, pwr diff = %d]\n",
					//Channel, BandEdge_Pwrdiff));
				}
			}
			powerlevel -= BandEdge_Pwrdiff;
			//RTPRINT(FPHY, PHY_TXPWR, (" [MCS power base index = 0x%x]\n", powerlevel));
		}
	}
	powerBase1 = powerlevel;
	powerBase1 = (powerBase1<<24) | (powerBase1<<16) |(powerBase1<<8) |powerBase1;

	//RTPRINT(FPHY, PHY_TXPWR, (" [Legacy/HT power index= %x/%x]\n", powerBase0, powerBase1));

	for(index=0; index<6; index++)
	{
		//
		// Index 0 & 1= legacy OFDM, 2-5=HT_MCS rate
		//
		//cosa add for lenovo, to pass the safety spec, don't increase power index for different rates.
		if(priv->bIgnoreDiffRateTxPowerOffset)
			writeVal = ((index<2)?powerBase0:powerBase1);
		else
		writeVal = priv->MCSTxPowerLevelOriginalOffset[index] + ((index<2)?powerBase0:powerBase1);

		//RTPRINT(FPHY, PHY_TXPWR, ("Reg 0x%x, Original=%x writeVal=%x\n",
		//RegOffset[index], priv->MCSTxPowerLevelOriginalOffset[index], writeVal));

		//
		// If path A and Path B coexist, we must limit Path A tx power.
		// Protect Path B pwr over or underflow. We need to calculate upper and
		// lower bound of path A tx power.
		//
		if (priv->rf_type == RF_2T2R)
		{
			rf_pwr_diff = priv->AntennaTxPwDiff[0];
			//RTPRINT(FPHY, PHY_TXPWR, ("2T2R RF-B to RF-A PWR DIFF=%d\n", rf_pwr_diff));

			if (rf_pwr_diff >= 8)		// Diff=-8~-1
			{	// Prevent underflow!!
				rfa_lower_bound = 0x10-rf_pwr_diff;
				//RTPRINT(FPHY, PHY_TXPWR, ("rfa_lower_bound= %d\n", rfa_lower_bound));
			}
			else if (rf_pwr_diff >= 0)	// Diff = 0-7
			{
				rfa_upper_bound = RF6052_MAX_TX_PWR-rf_pwr_diff;
				//RTPRINT(FPHY, PHY_TXPWR, ("rfa_upper_bound= %d\n", rfa_upper_bound));
			}
		}

		for (i=  0; i <4; i++)
		{
			rfa_pwr[i] = (u8)((writeVal & (0x7f<<(i*8)))>>(i*8));
			if (rfa_pwr[i]  > RF6052_MAX_TX_PWR)
				rfa_pwr[i]  = RF6052_MAX_TX_PWR;

			//
			// If path A and Path B coexist, we must limit Path A tx power.
			// Protect Path B pwr under/over flow. We need to calculate upper and
			// lower bound of path A tx power.
			//
			if (priv->rf_type == RF_2T2R)
			{
				if (rf_pwr_diff >= 8)		// Diff=-8~-1
				{	// Prevent underflow!!
					if (rfa_pwr[i] <rfa_lower_bound)
					{
						//RTPRINT(FPHY, PHY_TXPWR, ("Underflow"));
						rfa_pwr[i] = rfa_lower_bound;
					}
				}
				else if (rf_pwr_diff >= 1)	// Diff = 0-7
				{	// Prevent overflow
					if (rfa_pwr[i] > rfa_upper_bound)
					{
						//RTPRINT(FPHY, PHY_TXPWR, ("Overflow"));
						rfa_pwr[i] = rfa_upper_bound;
					}
				}
				//RTPRINT(FPHY, PHY_TXPWR, ("rfa_pwr[%d]=%x\n", i, rfa_pwr[i]));
			}

		}

		//
		// Add description: PWDB > threshold!!!High power issue!!
		// We must decrease tx power !! Why is the value ???
		//
		if(priv->bDynamicTxHighPower == TRUE)
		{
			// For MCS rate
			if(index > 1)
			{
				writeVal = 0x03030303;
			}
			// For Legacy rate
			else
			{
				writeVal = (rfa_pwr[3]<<24) | (rfa_pwr[2]<<16) |(rfa_pwr[1]<<8) |rfa_pwr[0];
			}
			//RTPRINT(FPHY, PHY_TXPWR, ("HighPower=%08x\n", writeVal));
		}
		else
		{
			writeVal = (rfa_pwr[3]<<24) | (rfa_pwr[2]<<16) |(rfa_pwr[1]<<8) |rfa_pwr[0];
			//RTPRINT(FPHY, PHY_TXPWR, ("NormalPower=%08x\n", writeVal));
		}

		//
		// Write different rate set tx power index.
		//
		//if (DCMD_Test_Flag == 0)
		rtl8192_setBBreg(dev, RegOffset[index], 0x7f7f7f7f, writeVal);
	}

}	/* PHY_RF6052SetOFDMTxPower */
#else
extern void PHY_RF6052SetOFDMTxPower(struct net_device* dev, u8 powerlevel)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u32 	writeVal, powerBase0, powerBase1;
	u8 	index = 0;
	u16 	RegOffset[6] = {0xe00, 0xe04, 0xe10, 0xe14, 0xe18, 0xe1c};
	u8 	byte0, byte1, byte2, byte3;
	u8    channel = priv->ieee80211->current_network.channel;

	//Legacy OFDM rates
	powerBase0 = powerlevel + (priv->LegacyHTTxPowerDiff & 0xf);
	powerBase0 = (powerBase0<<24) | (powerBase0<<16) |(powerBase0<<8) |powerBase0;

	//MCS rates HT OFDM
	powerBase1 = powerlevel;
	powerBase1 = (powerBase1<<24) | (powerBase1<<16) |(powerBase1<<8) |powerBase1;

	//printk("Legacy/HT PWR= %x/%x\n", powerBase0, powerBase1);

	for(index=0; index<6; index++)
	{
		//
		// Index 0 & 1= legacy OFDM, 2-5=HT_MCS rate
		//
		writeVal = priv->MCSTxPowerLevelOriginalOffset[index] +  ((index<2)?powerBase0:powerBase1);

		//printk("Index = %d Original=%x writeVal=%x\n", index, priv->MCSTxPowerLevelOriginalOffset[index], writeVal);

		byte0 = (u8)(writeVal & 0x7f);
		byte1 = (u8)((writeVal & 0x7f00)>>8);
		byte2 = (u8)((writeVal & 0x7f0000)>>16);
		byte3 = (u8)((writeVal & 0x7f000000)>>24);

		// Max power index = 0x3F Range = 0-0x3F
		if(byte0 > RF6052_MAX_TX_PWR)
			byte0 = RF6052_MAX_TX_PWR;
		if(byte1 > RF6052_MAX_TX_PWR)
			byte1 = RF6052_MAX_TX_PWR;
		if(byte2 > RF6052_MAX_TX_PWR)
			byte2 = RF6052_MAX_TX_PWR;
		if(byte3 > RF6052_MAX_TX_PWR)
			byte3 = RF6052_MAX_TX_PWR;

		//
		// Add description: PWDB > threshold!!!High power issue!!
		// We must decrease tx power !! Why is the value ???
		//
		if(priv->bDynamicTxHighPower == true)
		{
			// For MCS rate
			if(index > 1)
			{
				writeVal = 0x03030303;
			}
			// For Legacy rate
			else
			{
				writeVal = (byte3<<24) | (byte2<<16) |(byte1<<8) |byte0;
			}
		}
		else
		{
			writeVal = (byte3<<24) | (byte2<<16) |(byte1<<8) |byte0;
		}

		//
		// Write different rate set tx power index.
		//
		rtl8192_setBBreg(dev, RegOffset[index], 0x7f7f7f7f, writeVal);
	}

}	/* PHY_RF6052SetOFDMTxPower */
#endif

RT_STATUS PHY_RF6052_Config(struct net_device* dev)
{
	struct r8192_priv 			*priv = ieee80211_priv(dev);
	RT_STATUS				rtStatus = RT_STATUS_SUCCESS;
	//RF90_RADIO_PATH_E		eRFPath;
	//BB_REGISTER_DEFINITION_T	*pPhyReg;
	//u32						OrgStoreRFIntSW[RF90_PATH_D+1];

	//
	// Initialize general global value
	//
	// TODO: Extend RF_PATH_C and RF_PATH_D in the future
	if(priv->rf_type == RF_1T1R)
		priv->NumTotalRFPath = 1;
	else
		priv->NumTotalRFPath = 2;

	//
	// Config BB and RF
	//
//	switch( priv->bRegHwParaFile )
//	{
//		case 0:
//			phy_RF6052_Config_HardCode(dev);
//			break;

//		case 1:
			rtStatus = phy_RF6052_Config_ParaFile(dev);
//			break;

//		case 2:
			// Partial Modify.
//			phy_RF6052_Config_HardCode(dev);
//			phy_RF6052_Config_ParaFile(dev);
//			break;

//		default:
//			phy_RF6052_Config_HardCode(dev);
//			break;
//	}
	return rtStatus;

}

void phy_RF6052_Config_HardCode(struct net_device* dev)
{

	// Set Default Bandwidth to 20M
	//Adapter->HalFunc	.SetBWModeHandler(Adapter, HT_CHANNEL_WIDTH_20);

	// TODO: Set Default Channel to channel one for RTL8225

}

RT_STATUS phy_RF6052_Config_ParaFile(struct net_device* dev)
{
	u32			u4RegValue = 0;
	//static s1Byte		szRadioAFile[] = RTL819X_PHY_RADIO_A;
	//static s1Byte		szRadioBFile[] = RTL819X_PHY_RADIO_B;
	//static s1Byte		szRadioBGMFile[] = RTL819X_PHY_RADIO_B_GM;
	u8			eRFPath;
	RT_STATUS		rtStatus = RT_STATUS_SUCCESS;
	struct r8192_priv 	*priv = ieee80211_priv(dev);
	BB_REGISTER_DEFINITION_T	*pPhyReg;
	//u8			eCheckItem;


	//3//-----------------------------------------------------------------
	//3// <2> Initialize RF
	//3//-----------------------------------------------------------------
	//for(eRFPath = RF90_PATH_A; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
	for(eRFPath = 0; eRFPath <priv->NumTotalRFPath; eRFPath++)
	{

		pPhyReg = &priv->PHYRegDef[eRFPath];

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
		rtl8192_setBBreg(dev, pPhyReg->rfHSSIPara2, b3WireAddressLength, 0x0); 	// Set 1 to 4 bits for 8255
		rtl8192_setBBreg(dev, pPhyReg->rfHSSIPara2, b3WireDataLength, 0x0);	// Set 0 to 12  bits for 8255


		/*----Initialize RF fom connfiguration file----*/
		switch(eRFPath)
		{
		case RF90_PATH_A:
			rtStatus= rtl8192_phy_ConfigRFWithHeaderFile(dev,(RF90_RADIO_PATH_E)eRFPath);
			break;
		case RF90_PATH_B:
			rtStatus= rtl8192_phy_ConfigRFWithHeaderFile(dev,(RF90_RADIO_PATH_E)eRFPath);
			break;
		case RF90_PATH_C:
			break;
		case RF90_PATH_D:
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

		if(rtStatus != RT_STATUS_SUCCESS){
			printk("phy_RF6052_Config_ParaFile():Radio[%d] Fail!!", eRFPath);
			goto phy_RF6052_Config_ParaFile_Fail;
		}

	}

	RT_TRACE(COMP_INIT, "<---phy_RF6052_Config_ParaFile()\n");
	return rtStatus;

phy_RF6052_Config_ParaFile_Fail:
	return rtStatus;
}


//
// ==> RF shadow Operation API Code Section!!!
//
/*-----------------------------------------------------------------------------
 * Function:	PHY_RFShadowRead
 *				PHY_RFShadowWrite
 *				PHY_RFShadowCompare
 *				PHY_RFShadowRecorver
 *				PHY_RFShadowCompareAll
 *				PHY_RFShadowRecorverAll
 *				PHY_RFShadowCompareFlagSet
 *				PHY_RFShadowRecorverFlagSet
 *
 * Overview:	When we set RF register, we must write shadow at first.
 *			When we are running, we must compare shadow abd locate error addr.
 *			Decide to recorver or not.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/20/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
extern u32 PHY_RFShadowRead(
	struct net_device		* dev,
	RF90_RADIO_PATH_E	eRFPath,
	u32					Offset)
{
	return	RF_Shadow[eRFPath][Offset].Value;

}	/* PHY_RFShadowRead */


extern void PHY_RFShadowWrite(
	struct net_device		* dev,
	u32	eRFPath,
	u32					Offset,
	u32					Data)
{
	//RF_Shadow[eRFPath][Offset].Value = (Data & bMask20Bits);
	RF_Shadow[eRFPath][Offset].Value = (Data & bRFRegOffsetMask);
	RF_Shadow[eRFPath][Offset].Driver_Write = true;

}	/* PHY_RFShadowWrite */


extern void PHY_RFShadowCompare(
	struct net_device		* dev,
	RF90_RADIO_PATH_E	eRFPath,
	u32					Offset)
{
	u32	reg;

	// Check if we need to check the register
	if (RF_Shadow[eRFPath][Offset].Compare == true)
	{
		reg = rtl8192_phy_QueryRFReg(dev, eRFPath, Offset, bRFRegOffsetMask);
		// Compare shadow and real rf register for 20bits!!
		if (RF_Shadow[eRFPath][Offset].Value != reg)
		{
			// Locate error position.
			RF_Shadow[eRFPath][Offset].ErrorOrNot = true;
			RT_TRACE(COMP_INIT, "PHY_RFShadowCompare RF-%d Addr%02xErr = %05x", eRFPath, Offset, reg);
		}
	}

}	/* PHY_RFShadowCompare */

extern void PHY_RFShadowRecorver(
	struct net_device		* dev,
	RF90_RADIO_PATH_E	eRFPath,
	u32					Offset)
{
	// Check if the address is error
	if (RF_Shadow[eRFPath][Offset].ErrorOrNot == true)
	{
		// Check if we need to recorver the register.
		if (RF_Shadow[eRFPath][Offset].Recorver == true)
		{
			rtl8192_phy_SetRFReg(dev, eRFPath, Offset, bRFRegOffsetMask, RF_Shadow[eRFPath][Offset].Value);
			RT_TRACE(COMP_INIT, "PHY_RFShadowRecorver RF-%d Addr%02x=%05x",
			eRFPath, Offset, RF_Shadow[eRFPath][Offset].Value);
		}
	}

}	/* PHY_RFShadowRecorver */


extern void PHY_RFShadowCompareAll(struct net_device * dev)
{
	u32		eRFPath;
	u32		Offset;

	for (eRFPath = 0; eRFPath < RF6052_MAX_PATH; eRFPath++)
	{
		for (Offset = 0; Offset <= RF6052_MAX_REG; Offset++)
		{
			PHY_RFShadowCompare(dev, (RF90_RADIO_PATH_E)eRFPath, Offset);
		}
	}

}	/* PHY_RFShadowCompareAll */


extern void PHY_RFShadowRecorverAll(struct net_device * dev)
{
	u32		eRFPath;
	u32		Offset;

	for (eRFPath = 0; eRFPath < RF6052_MAX_PATH; eRFPath++)
	{
		for (Offset = 0; Offset <= RF6052_MAX_REG; Offset++)
		{
			PHY_RFShadowRecorver(dev, (RF90_RADIO_PATH_E)eRFPath, Offset);
		}
	}

}	/* PHY_RFShadowRecorverAll */


extern void PHY_RFShadowCompareFlagSet(
	struct net_device 		* dev,
	RF90_RADIO_PATH_E	eRFPath,
	u32					Offset,
	u8					Type)
{
	// Set True or False!!!
	RF_Shadow[eRFPath][Offset].Compare = Type;

}	/* PHY_RFShadowCompareFlagSet */


extern void PHY_RFShadowRecorverFlagSet(
	struct net_device 		* dev,
	RF90_RADIO_PATH_E	eRFPath,
	u32					Offset,
	u8					Type)
{
	// Set True or False!!!
	RF_Shadow[eRFPath][Offset].Recorver= Type;

}	/* PHY_RFShadowRecorverFlagSet */


extern void PHY_RFShadowCompareFlagSetAll(struct net_device  * dev)
{
	u32		eRFPath;
	u32		Offset;

	for (eRFPath = 0; eRFPath < RF6052_MAX_PATH; eRFPath++)
	{
		for (Offset = 0; Offset <= RF6052_MAX_REG; Offset++)
		{
			// 2008/11/20 MH For S3S4 test, we only check reg 26/27 now!!!!
			if (Offset != 0x26 && Offset != 0x27)
				PHY_RFShadowCompareFlagSet(dev, (RF90_RADIO_PATH_E)eRFPath, Offset, FALSE);
			else
				PHY_RFShadowCompareFlagSet(dev, (RF90_RADIO_PATH_E)eRFPath, Offset, TRUE);
		}
	}

}	/* PHY_RFShadowCompareFlagSetAll */


extern void PHY_RFShadowRecorverFlagSetAll(struct net_device  * dev)
{
	u32		eRFPath;
	u32		Offset;

	for (eRFPath = 0; eRFPath < RF6052_MAX_PATH; eRFPath++)
	{
		for (Offset = 0; Offset <= RF6052_MAX_REG; Offset++)
		{
			// 2008/11/20 MH For S3S4 test, we only check reg 26/27 now!!!!
			if (Offset != 0x26 && Offset != 0x27)
				PHY_RFShadowRecorverFlagSet(dev, (RF90_RADIO_PATH_E)eRFPath, Offset, FALSE);
			else
				PHY_RFShadowRecorverFlagSet(dev, (RF90_RADIO_PATH_E)eRFPath, Offset, TRUE);
		}
	}

}	/* PHY_RFShadowCompareFlagSetAll */



extern void PHY_RFShadowRefresh(struct net_device  * dev)
{
	u32		eRFPath;
	u32		Offset;

	for (eRFPath = 0; eRFPath < RF6052_MAX_PATH; eRFPath++)
	{
		for (Offset = 0; Offset <= RF6052_MAX_REG; Offset++)
		{
			RF_Shadow[eRFPath][Offset].Value = 0;
			RF_Shadow[eRFPath][Offset].Compare = false;
			RF_Shadow[eRFPath][Offset].Recorver  = false;
			RF_Shadow[eRFPath][Offset].ErrorOrNot = false;
			RF_Shadow[eRFPath][Offset].Driver_Write = false;
		}
	}

}	/* PHY_RFShadowRead */

/* End of HalRf6052.c */
