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
}


void PHY_SetRF8256OFDMTxPower(struct net_device* dev, u8 powerlevel)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

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
}

#define MAX_DOZE_WAITING_TIMES_9x 64
static void r8192e_drain_tx_queues(struct r8192_priv *priv)
{
	u8 i, QueueID;

	for (QueueID = 0, i = 0; QueueID < MAX_TX_QUEUE; )
	{
		struct rtl8192_tx_ring *ring = &priv->tx_ring[QueueID];

		if(skb_queue_len(&ring->queue) == 0)
		{
			QueueID++;
			continue;
		}

		udelay(10);
		i++;

		if (i >= MAX_DOZE_WAITING_TIMES_9x)
		{
			RT_TRACE(COMP_POWER, "r8192e_drain_tx_queues() timeout queue %d\n", QueueID);
			break;
		}
	}
}

static bool
SetRFPowerState8190(struct net_device *dev, RT_RF_POWER_STATE eRFPowerState)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	PRT_POWER_SAVE_CONTROL	pPSC = (PRT_POWER_SAVE_CONTROL)(&(priv->ieee80211->PowerSaveControl));
	bool bResult = true;

	spin_lock(&priv->ps_lock);
	if (priv->SetRFPowerStateInProgress) {
		bResult = false;
		goto out;
	}
	priv->SetRFPowerStateInProgress = true;

	switch( eRFPowerState )
	{
	case eRfOn:

		// turn on RF
		if ((priv->ieee80211->eRFPowerState == eRfOff) &&
		    RT_IN_PS_LEVEL(pPSC, RT_RF_OFF_LEVL_HALT_NIC))
		{
			/*
			 * The current RF state is OFF and the RF OFF level
			 * is halting the NIC, re-initialize the NIC.
			 */
			if (!NicIFEnableNIC(dev)) {
				RT_TRACE(COMP_ERR, "%s(): NicIFEnableNIC failed\n",__FUNCTION__);
				bResult = false;
				goto out;
			}

			RT_CLEAR_PS_LEVEL(pPSC, RT_RF_OFF_LEVL_HALT_NIC);
		} else {
			write_nic_byte(priv, ANAPAR, 0x37);//160MHz
			mdelay(1);
			//enable clock 80/88 MHz
			rtl8192_setBBreg(dev, rFPGA0_AnalogParameter1, 0x4, 0x1); // 0x880[2]
			priv->bHwRfOffAction = 0;

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

		}

		break;

	//
	// In current solution, RFSleep=RFOff in order to save power under 802.11 power save.
	// By Bruce, 2008-01-16.
	//
	case eRfSleep:

		// HW setting had been configured with deeper mode.
		if(priv->ieee80211->eRFPowerState == eRfOff)
			break;

		r8192e_drain_tx_queues(priv);

		PHY_SetRtl8192eRfOff(dev);

		break;

	case eRfOff:

		//
		// Disconnect with Any AP or STA.
		//
		r8192e_drain_tx_queues(priv);


		if (pPSC->RegRfPsLevel & RT_RF_OFF_LEVL_HALT_NIC && !RT_IN_PS_LEVEL(pPSC, RT_RF_OFF_LEVL_HALT_NIC))
		{
			/* Disable all components. */
			NicIFDisableNIC(dev);
			RT_SET_PS_LEVEL(pPSC, RT_RF_OFF_LEVL_HALT_NIC);
		}
		else if (!(pPSC->RegRfPsLevel & RT_RF_OFF_LEVL_HALT_NIC))
		{
			/* Normal case - IPS should go to this. */
			PHY_SetRtl8192eRfOff(dev);
		}
		break;

	default:
		bResult = false;
		RT_TRACE(COMP_ERR, "SetRFPowerState8190(): unknow state to set: 0x%X!!!\n", eRFPowerState);
		break;
	}

	if(bResult)
	{
		// Update current RF state variable.
		priv->ieee80211->eRFPowerState = eRFPowerState;
	}

out:
	priv->SetRFPowerStateInProgress = false;
	spin_unlock(&priv->ps_lock);
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
	if(eRFPowerState == priv->ieee80211->eRFPowerState && priv->bHwRfOffAction == 0)
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
	u8			i;
	bool	bFilterOutNonAssociatedBSSID = false;

	priv->ieee80211->state = IEEE80211_NOLINK;

	for(i=0;i<6;i++)  priv->ieee80211->current_network.bssid[i]= 0x55;
	priv->OpMode = RT_OP_MODE_NO_LINK;
	write_nic_word(priv, BSSIDR, ((u16*)priv->ieee80211->current_network.bssid)[0]);
	write_nic_dword(priv, BSSIDR+2, ((u32*)(priv->ieee80211->current_network.bssid+2))[0]);
	{
			RT_OP_MODE	OpMode = priv->OpMode;
			u8	btMsr = read_nic_byte(priv, MSR);

			btMsr &= 0xfc;

			switch(OpMode)
			{
			case RT_OP_MODE_INFRASTRUCTURE:
				btMsr |= MSR_LINK_MANAGED;
				break;

			case RT_OP_MODE_IBSS:
				btMsr |= MSR_LINK_ADHOC;
				// led link set separate
				break;

			case RT_OP_MODE_AP:
				btMsr |= MSR_LINK_MASTER;
				break;

			default:
				btMsr |= MSR_LINK_NONE;
				break;
			}

			write_nic_byte(priv, MSR, btMsr);
	}
	ieee80211_stop_send_beacons(priv->ieee80211);

	// If disconnect, clear RCR CBSSID bit
	bFilterOutNonAssociatedBSSID = false;
	{
			u32 RegRCR, Type;
			Type = bFilterOutNonAssociatedBSSID;
			RegRCR = read_nic_dword(priv, RCR);
			priv->ReceiveConfig = RegRCR;
			if (Type == true)
				RegRCR |= (RCR_CBSSID);
			else if (Type == false)
				RegRCR &= (~RCR_CBSSID);

			{
				write_nic_dword(priv, RCR, RegRCR);
				priv->ReceiveConfig = RegRCR;
			}

		}
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
		for(i=0;i<6;i++)  priv->ieee80211->current_network.bssid[i] = 0x22;
		priv->OpMode = RT_OP_MODE_NO_LINK;
		{
			RT_OP_MODE	OpMode = priv->OpMode;
			u8 btMsr = read_nic_byte(priv, MSR);

			btMsr &= 0xfc;

			switch(OpMode)
			{
			case RT_OP_MODE_INFRASTRUCTURE:
				btMsr |= MSR_LINK_MANAGED;
				break;

			case RT_OP_MODE_IBSS:
				btMsr |= MSR_LINK_ADHOC;
				// led link set separate
				break;

			case RT_OP_MODE_AP:
				btMsr |= MSR_LINK_MASTER;
				break;

			default:
				btMsr |= MSR_LINK_NONE;
				break;
			}

			write_nic_byte(priv, MSR, btMsr);
		}
		ieee80211_disassociate(priv->ieee80211);

		write_nic_word(priv, BSSIDR, ((u16*)priv->ieee80211->current_network.bssid)[0]);
		write_nic_dword(priv, BSSIDR+2, ((u32*)(priv->ieee80211->current_network.bssid+2))[0]);

	}

}


static void MgntDisconnectAP(struct net_device *dev, u8 asRsn)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	bool bFilterOutNonAssociatedBSSID = false;
	u32 RegRCR, Type;

	/* If disconnect, clear RCR CBSSID bit */
	bFilterOutNonAssociatedBSSID = false;

	Type = bFilterOutNonAssociatedBSSID;
	RegRCR = read_nic_dword(priv, RCR);
	priv->ReceiveConfig = RegRCR;

	if (Type == true)
		RegRCR |= (RCR_CBSSID);
	else if (Type == false)
		RegRCR &= (~RCR_CBSSID);

	write_nic_dword(priv, RCR, RegRCR);
	priv->ReceiveConfig = RegRCR;

	MlmeDisassociateRequest(dev, priv->ieee80211->current_network.bssid, asRsn);

	priv->ieee80211->state = IEEE80211_NOLINK;
}


static bool
MgntDisconnect(
	struct net_device* dev,
	u8 asRsn
)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	// In adhoc mode, update beacon frame.
	if( priv->ieee80211->state == IEEE80211_LINKED )
	{
		if( priv->ieee80211->iw_mode == IW_MODE_ADHOC )
		{
			MgntDisconnectIBSS(dev);
		}
		if( priv->ieee80211->iw_mode == IW_MODE_INFRA )
		{
			// We clear key here instead of MgntDisconnectAP() because that
			// MgntActSet_802_11_DISASSOCIATE() is an interface called by OS,
			// e.g. OID_802_11_DISASSOCIATE in Windows while as MgntDisconnectAP() is
			// used to handle disassociation related things to AP, e.g. send Disassoc
			// frame to AP.  2005.01.27, by rcnjko.
			MgntDisconnectAP(dev, asRsn);
		}
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
	RT_TRACE(COMP_POWER, "===>MgntActSet_RF_State(): StateToSet(%d)\n",StateToSet);

	//1//
	//1//<1>Prevent the race condition of RF state change.
	//1//
	// Only one thread can change the RF state at one time, and others should wait to be executed. By Bruce, 2007-11-28.

	while(true)
	{
		spin_lock(&priv->rf_ps_lock);
		if(priv->RFChangeInProgress)
		{
			spin_unlock(&priv->rf_ps_lock);
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
			spin_unlock(&priv->rf_ps_lock);
			break;
		}
	}

	rtState = priv->ieee80211->eRFPowerState;

	switch(StateToSet)
	{
	case eRfOn:
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
			// Disconnect to current BSS when radio off. Asked by QuanTa.
			MgntDisconnect(dev, disas_lv_ss);
		}

		priv->ieee80211->RfOffReason |= ChangeSource;
		bActionAllowed = true;
		break;

	case eRfSleep:
		priv->ieee80211->RfOffReason |= ChangeSource;
		bActionAllowed = true;
		break;
	}

	if (bActionAllowed)
	{
		RT_TRACE(COMP_POWER, "MgntActSet_RF_State(): Action is allowed.... StateToSet(%d), RfOffReason(%#X)\n", StateToSet, priv->ieee80211->RfOffReason);
		// Config HW to the specified mode.
		SetRFPowerState(dev, StateToSet);
	}
	else
	{
		RT_TRACE(COMP_POWER, "MgntActSet_RF_State(): Action is rejected.... StateToSet(%d), ChangeSource(%#X), RfOffReason(%#X)\n", StateToSet, ChangeSource, priv->ieee80211->RfOffReason);
	}

	// Release RF spinlock
	spin_lock(&priv->rf_ps_lock);
	priv->RFChangeInProgress = false;
	spin_unlock(&priv->rf_ps_lock);

	RT_TRACE(COMP_POWER, "<===MgntActSet_RF_State()\n");
	return bActionAllowed;
}


