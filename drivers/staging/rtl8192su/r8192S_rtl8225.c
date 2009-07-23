
#include "r8192U.h"
#include "r8192S_hw.h"
#include "r8192S_phyreg.h"
#include "r8192S_phy.h"
#include "r8192S_rtl8225.h"

/*---------------------Define local function prototype-----------------------*/
void phy_RF8225_Config_HardCode(struct net_device* dev );
bool phy_RF8225_Config_ParaFile(struct net_device* dev );
/*---------------------Define local function prototype-----------------------*/
void PHY_SetRF8225OfdmTxPower(struct net_device* dev ,u8	powerlevel)
{

}



void PHY_SetRF8225CckTxPower(	struct net_device* dev ,	u8 powerlevel)
{

}


// TODO: The following RF 022D related function should be removed to HalPhy0222D.c.
void PHY_SetRF0222DOfdmTxPower(struct net_device* dev ,u8 powerlevel)
{
	//TODO: We should set RF TxPower for RF 0222D here!!
}



void PHY_SetRF0222DCckTxPower(struct net_device* dev ,u8	powerlevel)
{
	//TODO: We should set RF TxPower for RF 0222D here!!
}


/*-----------------------------------------------------------------------------
 * Function:    PHY_SetRF0222DBandwidth()
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
 //just in phy
void PHY_SetRF0222DBandwidth(struct net_device* dev , HT_CHANNEL_WIDTH	 Bandwidth)	//20M or 40M
{
	u8			eRFPath;
	struct r8192_priv *priv = ieee80211_priv(dev);


	//if (IS_HARDWARE_TYPE_8192S(dev))
	if (1)
	{
#ifndef RTL92SE_FPGA_VERIFY
		switch(Bandwidth)
		{
			case HT_CHANNEL_WIDTH_20:
#ifdef FIB_MODIFICATION
				write_nic_byte(dev, rFPGA0_AnalogParameter2, 0x58);
#endif
				rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E)RF90_PATH_A, RF_CHNLBW, BIT10|BIT11, 0x01);
				break;
			case HT_CHANNEL_WIDTH_20_40:
#ifdef FIB_MODIFICATION
				write_nic_byte(dev, rFPGA0_AnalogParameter2, 0x18);
#endif
				rtl8192_phy_SetRFReg(dev, (RF90_RADIO_PATH_E)RF90_PATH_A, RF_CHNLBW, BIT10|BIT11, 0x00);
				break;
			default:
				;//RT_TRACE(COMP_DBG, DBG_LOUD, ("PHY_SetRF8225Bandwidth(): unknown Bandwidth: %#X\n",Bandwidth ));
				break;
		}
#endif
	}
	else
	{
	for(eRFPath = 0; eRFPath <priv->NumTotalRFPath; eRFPath++)
	{
		switch(Bandwidth)
		{
			case HT_CHANNEL_WIDTH_20:
					//rtl8192_phy_SetRFReg(Adapter, (RF90_RADIO_PATH_E)RF90_PATH_A, RF_CHNLBW, (BIT10|BIT11), 0x01);
				break;
			case HT_CHANNEL_WIDTH_20_40:
					//rtl8192_phy_SetRFReg(Adapter, (RF90_RADIO_PATH_E)RF90_PATH_A, RF_CHNLBW, (BIT10|BIT11), 0x00);
				break;
			default:
				;//RT_TRACE(COMP_DBG, DBG_LOUD, ("PHY_SetRF8225Bandwidth(): unknown Bandwidth: %#X\n",Bandwidth ));
				break;

		}
	}
	}

}

// TODO: Aabove RF 022D related function should be removed to HalPhy0222D.c.

/*-----------------------------------------------------------------------------
 * Function:    PHY_SetRF8225Bandwidth()
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
 * Note:		8225(zebra1) support 20M only
 *---------------------------------------------------------------------------*/
 //just in phy
void PHY_SetRF8225Bandwidth(struct net_device* dev ,HT_CHANNEL_WIDTH Bandwidth)	//20M or 40M
{
	u8			eRFPath;
	struct r8192_priv *priv = ieee80211_priv(dev);

	//for(eRFPath = RF90_PATH_A; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
	for(eRFPath = 0; eRFPath <priv->NumTotalRFPath; eRFPath++)
	{
		switch(Bandwidth)
		{
			case HT_CHANNEL_WIDTH_20:
				// TODO: Update the parameters here
				break;
			case HT_CHANNEL_WIDTH_20_40:
				RT_TRACE(COMP_DBG, "SetChannelBandwidth8190Pci():8225 does not support 40M mode\n");
				break;
			default:
				RT_TRACE(COMP_DBG, "PHY_SetRF8225Bandwidth(): unknown Bandwidth: %#X\n",Bandwidth );
				break;

		}
	}

}

//just in phy
bool PHY_RF8225_Config(struct net_device* dev )
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	bool	rtStatus = true;
	//RF90_RADIO_PATH_E			eRFPath;
	//BB_REGISTER_DEFINITION_T	*pPhyReg;
	//u32						OrgStoreRFIntSW[RF90_PATH_D+1];

	//
	// Initialize general global value
	//
	// TODO: Extend RF_PATH_C and RF_PATH_D in the future
	priv->NumTotalRFPath = 2;

	//
	// Config BB and RF
	//
	//switch( Adapter->MgntInfo.bRegHwParaFile )
	//{
	//	case 0:
	//		phy_RF8225_Config_HardCode(dev);
	//		break;

	//	case 1:
	//		rtStatus = phy_RF8225_Config_ParaFile(dev);
	//		break;

	//	case 2:
			// Partial Modify.
			phy_RF8225_Config_HardCode(dev);
			phy_RF8225_Config_ParaFile(dev);
	//		break;

	//	default:
	//		phy_RF8225_Config_HardCode(dev);
	//		break;
	//}
	return rtStatus;

}

//just in 8225
void phy_RF8225_Config_HardCode(struct net_device* dev)
{

	// Set Default Bandwidth to 20M
	//Adapter->HalFunc	.SetBWModeHandler(Adapter, HT_CHANNEL_WIDTH_20);

	// TODO: Set Default Channel to channel one for RTL8225

}

//just in 8225
bool phy_RF8225_Config_ParaFile(struct net_device* dev)
{
	u32					u4RegValue = 0;
	//static char				szRadioAFile[] = RTL819X_PHY_RADIO_A;
	//static char				szRadioBFile[] = RTL819X_PHY_RADIO_B;
	u8					eRFPath;
	bool				rtStatus = true;
	struct r8192_priv *priv = ieee80211_priv(dev);
	BB_REGISTER_DEFINITION_T	*pPhyReg;
	//u8						eCheckItem;

#if	1
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
			//rtStatus = PHY_ConfigRFWithParaFile(dev, (char* )&szRadioAFile, (RF90_RADIO_PATH_E)eRFPath);
			rtStatus = rtl8192_phy_ConfigRFWithHeaderFile(dev,(RF90_RADIO_PATH_E)eRFPath);
			break;
		case RF90_PATH_B:
			//rtStatus = PHY_ConfigRFWithParaFile(dev, (char* )&szRadioBFile, (RF90_RADIO_PATH_E)eRFPath);
			rtStatus = rtl8192_phy_ConfigRFWithHeaderFile(dev,(RF90_RADIO_PATH_E)eRFPath);
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

		if(rtStatus == false){
			//RT_TRACE(COMP_FPGA, DBG_LOUD, ("phy_RF8225_Config_ParaFile():Radio[%d] Fail!!", eRFPath));
			goto phy_RF8225_Config_ParaFile_Fail;
		}

	}

	//RT_TRACE(COMP_INIT, DBG_LOUD, ("<---phy_RF8225_Config_ParaFile()\n"));
	return rtStatus;

phy_RF8225_Config_ParaFile_Fail:
#endif
	return rtStatus;
}


