/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#ifdef RT_CFG80211_SUPPORT

#include "rt_config.h"

#define RT_CFG80211_DEBUG /* debug use */
#define CFG80211CB			(pAd->pCfg80211_CB)

#ifdef RT_CFG80211_DEBUG
#define CFG80211DBG(__Flg, __pMsg)		DBGPRINT(__Flg, __pMsg)
#else
#define CFG80211DBG(__Flg, __pMsg)
#endif /* RT_CFG80211_DEBUG */




BOOLEAN CFG80211DRV_OpsSetChannel(
	VOID						*pAdOrg,
	VOID						*pData)
{
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pAdOrg;
	CMD_RTPRIV_IOCTL_80211_CHAN *pChan;
	UINT8 ChanId;
	UINT8 IfType;
	UINT8 ChannelType;
	STRING ChStr[5] = { 0 };
#ifdef DOT11_N_SUPPORT
	UCHAR BW_Old;
	BOOLEAN FlgIsChanged;
#endif /* DOT11_N_SUPPORT */


	/* init */
	pChan = (CMD_RTPRIV_IOCTL_80211_CHAN *)pData;
	ChanId = pChan->ChanId;
	IfType = pChan->IfType;
	ChannelType = pChan->ChanType;

#ifdef DOT11_N_SUPPORT
	if (IfType != RT_CMD_80211_IFTYPE_MONITOR)
	{
		/* get channel BW */
		FlgIsChanged = FALSE;
		BW_Old = pAd->CommonCfg.RegTransmitSetting.field.BW;
	
		/* set to new channel BW */
		if (ChannelType == RT_CMD_80211_CHANTYPE_HT20)
		{
			pAd->CommonCfg.RegTransmitSetting.field.BW = BW_20;
			FlgIsChanged = TRUE;
		}
		else if ((ChannelType == RT_CMD_80211_CHANTYPE_HT40MINUS) ||
				(ChannelType == RT_CMD_80211_CHANTYPE_HT40PLUS))
		{
			/* not support NL80211_CHAN_HT40MINUS or NL80211_CHAN_HT40PLUS */
			/* i.e. primary channel = 36, secondary channel must be 40 */
			pAd->CommonCfg.RegTransmitSetting.field.BW = BW_40;
			FlgIsChanged = TRUE;
		} /* End of if */
	
		CFG80211DBG(RT_DEBUG_ERROR, ("80211> New BW = %d\n",
					pAd->CommonCfg.RegTransmitSetting.field.BW));
	
		/* change HT/non-HT mode (do NOT change wireless mode here) */
		if (((ChannelType == RT_CMD_80211_CHANTYPE_NOHT) &&
			(pAd->CommonCfg.HT_Disable == 0)) ||
			((ChannelType != RT_CMD_80211_CHANTYPE_NOHT) &&
			(pAd->CommonCfg.HT_Disable == 1)))
		{
			if (ChannelType == RT_CMD_80211_CHANTYPE_NOHT)
				pAd->CommonCfg.HT_Disable = 1;
			else
				pAd->CommonCfg.HT_Disable = 0;
			/* End of if */
	
			FlgIsChanged = TRUE;
			CFG80211DBG(RT_DEBUG_ERROR, ("80211> HT Disable = %d\n",
						pAd->CommonCfg.HT_Disable));
		} /* End of if */
	}
	else
	{
		/* for monitor mode */
		FlgIsChanged = TRUE;
		pAd->CommonCfg.HT_Disable = 0;
		pAd->CommonCfg.RegTransmitSetting.field.BW = BW_40;
	} /* End of if */

	if (FlgIsChanged == TRUE)
		SetCommonHT(pAd);
	/* End of if */
#endif /* DOT11_N_SUPPORT */

	/* switch to the channel */
	sprintf(ChStr, "%d", ChanId);
	if (Set_Channel_Proc(pAd, ChStr) == FALSE)
	{
		CFG80211DBG(RT_DEBUG_ERROR, ("80211> Change channel fail!\n"));
	} /* End of if */

#ifdef CONFIG_STA_SUPPORT
#ifdef DOT11_N_SUPPORT
	if ((IfType == RT_CMD_80211_IFTYPE_STATION) && (FlgIsChanged == TRUE))
	{
		/*
			1. Station mode;
			2. New BW settings is 20MHz but current BW is not 20MHz;
			3. New BW settings is 40MHz but current BW is 20MHz;

			Re-connect to the AP due to BW 20/40 or HT/non-HT change.
		*/
		Set_SSID_Proc(pAd, (PSTRING)pAd->CommonCfg.Ssid);
	} /* End of if */
#endif /* DOT11_N_SUPPORT */

	if (IfType == RT_CMD_80211_IFTYPE_ADHOC)
	{
		/* update IBSS beacon */
		MlmeUpdateTxRates(pAd, FALSE, 0);
		MakeIbssBeacon(pAd);
		AsicEnableIbssSync(pAd);

		Set_SSID_Proc(pAd, (PSTRING)pAd->CommonCfg.Ssid);
	} /* End of if */

	if (IfType == RT_CMD_80211_IFTYPE_MONITOR)
	{
		/* reset monitor mode in the new channel */
		Set_NetworkType_Proc(pAd, "Monitor");
		RTMP_IO_WRITE32(pAd, RX_FILTR_CFG, pChan->MonFilterFlag);
	} /* End of if */
#endif /* CONFIG_STA_SUPPORT */

	return TRUE;
}


BOOLEAN CFG80211DRV_OpsChgVirtualInf(
	VOID						*pAdOrg,
	VOID						*pFlgFilter,
	UINT8						IfType)
{
#ifdef CONFIG_STA_SUPPORT
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pAdOrg;
	UINT32 FlgFilter = *(UINT32 *)pFlgFilter;


	/* change type */
	if (IfType == RT_CMD_80211_IFTYPE_ADHOC)
		Set_NetworkType_Proc(pAd, "Adhoc");
	else if (IfType == RT_CMD_80211_IFTYPE_STATION)
		Set_NetworkType_Proc(pAd, "Infra");
	else if (IfType == RT_CMD_80211_IFTYPE_MONITOR)
	{
		/* set packet filter */
		Set_NetworkType_Proc(pAd, "Monitor");

		if (FlgFilter != 0)
		{
			UINT32 Filter;


			RTMP_IO_READ32(pAd, RX_FILTR_CFG, &Filter);

			if ((FlgFilter & RT_CMD_80211_FILTER_FCSFAIL) == \
												RT_CMD_80211_FILTER_FCSFAIL)
			{
				Filter = Filter & (~0x01);
			}
			else
				Filter = Filter | 0x01;
			/* End of if */
	
			if ((FlgFilter & RT_CMD_80211_FILTER_PLCPFAIL) == \
												RT_CMD_80211_FILTER_PLCPFAIL)
			{
				Filter = Filter & (~0x02);
			}
			else
				Filter = Filter | 0x02;
			/* End of if */
	
			if ((FlgFilter & RT_CMD_80211_FILTER_CONTROL) == \
												RT_CMD_80211_FILTER_CONTROL)
			{
				Filter = Filter & (~0xFF00);
			}
			else
				Filter = Filter | 0xFF00;
			/* End of if */
	
			if ((FlgFilter & RT_CMD_80211_FILTER_OTHER_BSS) == \
												RT_CMD_80211_FILTER_OTHER_BSS)
			{
				Filter = Filter & (~0x08);
			}
			else
				Filter = Filter | 0x08;
			/* End of if */

			RTMP_IO_WRITE32(pAd, RX_FILTR_CFG, Filter);
			*(UINT32 *)pFlgFilter = Filter;
		} /* End of if */

		return TRUE; /* not need to set SSID */
	} /* End of if */

	pAd->StaCfg.bAutoReconnect = TRUE;

	CFG80211DBG(RT_DEBUG_ERROR, ("80211> SSID = %s\n", pAd->CommonCfg.Ssid));
	Set_SSID_Proc(pAd, (PSTRING)pAd->CommonCfg.Ssid);
#endif /* CONFIG_STA_SUPPORT */

	return TRUE;
}


BOOLEAN CFG80211DRV_OpsScan(
	VOID						*pAdOrg)
{
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pAdOrg;


	if (pAd->FlgCfg80211Scanning == TRUE)
		return FALSE; /* scanning */
	/* End of if */

	/* do scan */
	pAd->FlgCfg80211Scanning = TRUE;
	return TRUE;
}


BOOLEAN CFG80211DRV_OpsJoinIbss(
	VOID						*pAdOrg,
	VOID						*pData)
{
#ifdef CONFIG_STA_SUPPORT
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pAdOrg;
	CMD_RTPRIV_IOCTL_80211_IBSS *pIbssInfo;


	pIbssInfo = (CMD_RTPRIV_IOCTL_80211_IBSS *)pData;
	pAd->StaCfg.bAutoReconnect = TRUE;

	pAd->CommonCfg.BeaconPeriod = pIbssInfo->BeaconInterval;
	Set_SSID_Proc(pAd, (PSTRING)pIbssInfo->pSsid);
#endif /* CONFIG_STA_SUPPORT */
	return TRUE;
}


BOOLEAN CFG80211DRV_OpsLeave(
	VOID						*pAdOrg)
{
#ifdef CONFIG_STA_SUPPORT
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pAdOrg;


	pAd->StaCfg.bAutoReconnect = FALSE;
	pAd->FlgCfg80211Connecting = FALSE;
	LinkDown(pAd, FALSE);
#endif /* CONFIG_STA_SUPPORT */
	return TRUE;
}


BOOLEAN CFG80211DRV_StaGet(
	VOID						*pAdOrg,
	VOID						*pData)
{
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pAdOrg;
	CMD_RTPRIV_IOCTL_80211_STA *pIbssInfo;


	pIbssInfo = (CMD_RTPRIV_IOCTL_80211_STA *)pData;


#ifdef CONFIG_STA_SUPPORT
{
	HTTRANSMIT_SETTING PhyInfo;
	ULONG DataRate = 0;
	UINT32 RSSI;


	/* fill tx rate */
    if ((pAd->CommonCfg.PhyMode <= PHY_11G) ||
		(pAd->MacTab.Content[BSSID_WCID].HTPhyMode.field.MODE <= MODE_OFDM))
	{
		PhyInfo.word = pAd->StaCfg.HTPhyMode.word;
	}
    else
		PhyInfo.word = pAd->MacTab.Content[BSSID_WCID].HTPhyMode.word;
	/* End of if */

	getRate(PhyInfo, &DataRate);

	if ((PhyInfo.field.MODE == MODE_HTMIX) ||
		(PhyInfo.field.MODE == MODE_HTGREENFIELD))
	{
		if (PhyInfo.field.BW)
			pIbssInfo->TxRateFlags |= RT_CMD_80211_TXRATE_BW_40;
		/* End of if */
		if (PhyInfo.field.ShortGI)
			pIbssInfo->TxRateFlags |= RT_CMD_80211_TXRATE_SHORT_GI;
		/* End of if */

		pIbssInfo->TxRateMCS = PhyInfo.field.MCS;
	}
	else
	{
		pIbssInfo->TxRateFlags = RT_CMD_80211_TXRATE_LEGACY;
		pIbssInfo->TxRateMCS = DataRate*10; /* unit: 100kbps */
	} /* End of if */

	/* fill signal */
	RSSI = (pAd->StaCfg.RssiSample.AvgRssi0 +
			pAd->StaCfg.RssiSample.AvgRssi1 +
			pAd->StaCfg.RssiSample.AvgRssi2) / 3;
	pIbssInfo->Signal = RSSI;
}
#endif /* CONFIG_STA_SUPPORT */

	return TRUE;
}


BOOLEAN CFG80211DRV_KeyAdd(
	VOID						*pAdOrg,
	VOID						*pData)
{
#ifdef CONFIG_STA_SUPPORT
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pAdOrg;
	CMD_RTPRIV_IOCTL_80211_KEY *pKeyInfo;


	pKeyInfo = (CMD_RTPRIV_IOCTL_80211_KEY *)pData;

	if (pKeyInfo->KeyType == RT_CMD_80211_KEY_WEP)
	{
		switch(pKeyInfo->KeyId)
		{
			case 1:
			default:
				Set_Key1_Proc(pAd, (PSTRING)pKeyInfo->KeyBuf);
				break;

			case 2:
				Set_Key2_Proc(pAd, (PSTRING)pKeyInfo->KeyBuf);
				break;

			case 3:
				Set_Key3_Proc(pAd, (PSTRING)pKeyInfo->KeyBuf);
				break;

			case 4:
				Set_Key4_Proc(pAd, (PSTRING)pKeyInfo->KeyBuf);
				break;
		} /* End of switch */
	}
	else
		Set_WPAPSK_Proc(pAd, (PSTRING)pKeyInfo->KeyBuf);
#endif /* CONFIG_STA_SUPPORT */

	return TRUE;
}


BOOLEAN CFG80211DRV_Connect(
	VOID						*pAdOrg,
	VOID						*pData)
{
#ifdef CONFIG_STA_SUPPORT
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pAdOrg;
	CMD_RTPRIV_IOCTL_80211_CONNECT *pConnInfo;
	UCHAR SSID[NDIS_802_11_LENGTH_SSID];
	UINT32 SSIDLen;


	pConnInfo = (CMD_RTPRIV_IOCTL_80211_CONNECT *)pData;
	/* change to infrastructure mode if we are in ADHOC mode */
	Set_NetworkType_Proc(pAd, "Infra");

	/* set authentication mode */
	if (pConnInfo->WpaVer == 2)
	{
		if (pConnInfo->FlgIs8021x == TRUE)
			Set_AuthMode_Proc(pAd, "WPA2");
		else
			Set_AuthMode_Proc(pAd, "WPA2PSK");
		/* End of if */
	}
	else if (pConnInfo->WpaVer == 1)
	{
		if (pConnInfo->FlgIs8021x == TRUE)
			Set_AuthMode_Proc(pAd, "WPA");
		else
			Set_AuthMode_Proc(pAd, "WPAPSK");
		/* End of if */
	}
	else if (pConnInfo->FlgIsAuthOpen == FALSE)
		Set_AuthMode_Proc(pAd, "SHARED");
	else
		Set_AuthMode_Proc(pAd, "OPEN");
	/* End of if */

	CFG80211DBG(RT_DEBUG_ERROR,
				("80211> AuthMode = %d\n", pAd->StaCfg.AuthMode));

	/* set encryption mode */
	if (pConnInfo->PairwiseEncrypType & RT_CMD_80211_CONN_ENCRYPT_CCMP)
		Set_EncrypType_Proc(pAd, "AES");
	else if (pConnInfo->PairwiseEncrypType & RT_CMD_80211_CONN_ENCRYPT_TKIP)
		Set_EncrypType_Proc(pAd, "TKIP");
	else if (pConnInfo->PairwiseEncrypType & RT_CMD_80211_CONN_ENCRYPT_WEP)
	{
		Set_EncrypType_Proc(pAd, "WEP");
	}
	else if (pConnInfo->GroupwiseEncrypType & RT_CMD_80211_CONN_ENCRYPT_CCMP)
		Set_EncrypType_Proc(pAd, "AES");
	else if (pConnInfo->GroupwiseEncrypType & RT_CMD_80211_CONN_ENCRYPT_TKIP)
		Set_EncrypType_Proc(pAd, "TKIP");
	else
		Set_EncrypType_Proc(pAd, "NONE");
	/* End of if */

	CFG80211DBG(RT_DEBUG_ERROR,
				("80211> EncrypType = %d\n", pAd->StaCfg.WepStatus));

	/* set channel: STATION will auto-scan */

	/* set WEP key */
	if (pConnInfo->pKey &&
		((pConnInfo->GroupwiseEncrypType | pConnInfo->PairwiseEncrypType) &
												RT_CMD_80211_CONN_ENCRYPT_WEP))
	{
		UCHAR KeyBuf[50];

		/* reset AuthMode and EncrypType */
		Set_AuthMode_Proc(pAd, "SHARED");
		Set_EncrypType_Proc(pAd, "WEP");

		/* reset key */
#ifdef RT_CFG80211_DEBUG
		hex_dump("KeyBuf=", (UINT8 *)pConnInfo->pKey, pConnInfo->KeyLen);
#endif /* RT_CFG80211_DEBUG */

		pAd->StaCfg.DefaultKeyId = pConnInfo->KeyIdx; /* base 0 */
		if (pConnInfo->KeyLen >= sizeof(KeyBuf))
			return FALSE;
		/* End of if */
		memcpy(KeyBuf, pConnInfo->pKey, pConnInfo->KeyLen);
		KeyBuf[pConnInfo->KeyLen] = 0x00;

		CFG80211DBG(RT_DEBUG_ERROR,
					("80211> pAd->StaCfg.DefaultKeyId = %d\n",
					pAd->StaCfg.DefaultKeyId));

		switch(pConnInfo->KeyIdx)
		{
			case 1:
			default:
				Set_Key1_Proc(pAd, (PSTRING)KeyBuf);
				break;

			case 2:
				Set_Key2_Proc(pAd, (PSTRING)KeyBuf);
				break;

			case 3:
				Set_Key3_Proc(pAd, (PSTRING)KeyBuf);
				break;

			case 4:
				Set_Key4_Proc(pAd, (PSTRING)KeyBuf);
				break;
		} /* End of switch */
	} /* End of if */

	/* TODO: We need to provide a command to set BSSID to associate a AP */

	/* re-set SSID */
	pAd->StaCfg.bAutoReconnect = TRUE;
	pAd->FlgCfg80211Connecting = TRUE;

	SSIDLen = pConnInfo->SsidLen;
	if (SSIDLen > NDIS_802_11_LENGTH_SSID)
		SSIDLen = NDIS_802_11_LENGTH_SSID;
	/* End of if */

	memset(&SSID, 0, sizeof(SSID));
	memcpy(SSID, pConnInfo->pSsid, SSIDLen);
	Set_SSID_Proc(pAd, (PSTRING)SSID);
	CFG80211DBG(RT_DEBUG_ERROR, ("80211> SSID = %s\n", SSID));
#endif /* CONFIG_STA_SUPPORT */

	return TRUE;
}


VOID CFG80211DRV_RegNotify(
	VOID						*pAdOrg,
	VOID						*pData)
{
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pAdOrg;
	CMD_RTPRIV_IOCTL_80211_REG_NOTIFY *pRegInfo;


	pRegInfo = (CMD_RTPRIV_IOCTL_80211_REG_NOTIFY *)pData;

	/* keep Alpha2 and we can re-call the function when interface is up */
	pAd->Cfg80211_Alpha2[0] = pRegInfo->Alpha2[0];
	pAd->Cfg80211_Alpha2[1] = pRegInfo->Alpha2[1];

	/* apply the new regulatory rule */
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_START_UP))
	{
		/* interface is up */
		CFG80211_RegRuleApply(pAd, pRegInfo->pWiphy, (UCHAR *)pRegInfo->Alpha2);
	}
	else
	{
		CFG80211DBG(RT_DEBUG_ERROR, ("crda> interface is down!\n"));
	} /* End of if */
}


VOID CFG80211_UnRegister(
	IN PRTMP_ADAPTER		pAd,
	IN VOID					*pNetDev)
{
#ifdef CONFIG_STA_SUPPORT
	UINT32 BssId;

	/* free channel information for scan table */
	for(BssId=0; BssId<MAX_LEN_OF_BSS_TABLE; BssId++)
	{
		if (pAd->ScanTab.BssEntry[BssId].pCfg80211_Chan != NULL)
			os_free_mem(NULL, pAd->ScanTab.BssEntry[BssId].pCfg80211_Chan);
		/* End of if */

		pAd->ScanTab.BssEntry[BssId].pCfg80211_Chan = NULL;
	} /* End of for */
#endif /* CONFIG_STA_SUPPORT */

	/* sanity check */
	if (pAd->pCfg80211_CB == NULL)
		return;
	/* End of if */

	CFG80211OS_UnRegister(pAd->pCfg80211_CB, pNetDev);
	pAd->pCfg80211_CB = NULL;
	pAd->CommonCfg.HT_Disable = 0;
}


/*
========================================================================
Routine Description:
	Parse and handle country region in beacon from associated AP.

Arguments:
	pAdCB			- WLAN control block pointer
	pVIE			- Beacon elements
	LenVIE			- Total length of Beacon elements

Return Value:
	NONE

Note:
========================================================================
*/
VOID CFG80211_BeaconCountryRegionParse(
	IN VOID						*pAdCB,
	IN NDIS_802_11_VARIABLE_IEs	*pVIE,
	IN UINT16					LenVIE)
{
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pAdCB;
	UCHAR *pElement = (UCHAR *)pVIE;
	UINT32 LenEmt;


	while(LenVIE > 0)
	{
		pVIE = (NDIS_802_11_VARIABLE_IEs *)pElement;

		if (pVIE->ElementID == IE_COUNTRY)
		{
			/* send command to do regulation hint only when associated */
			RTEnqueueInternalCmd(pAd, CMDTHREAD_REG_HINT_11D,
								pVIE->data, pVIE->Length);
			break;
		} /* End of if */

		LenEmt = pVIE->Length + 2;

		if (LenVIE <= LenEmt)
			break; /* length is not enough */
		/* End of if */

		pElement += LenEmt;
		LenVIE -= LenEmt;
	} /* End of while */
} /* End of CFG80211_BeaconCountryRegionParse */


/*
========================================================================
Routine Description:
	Hint to the wireless core a regulatory domain from driver.

Arguments:
	pAd				- WLAN control block pointer
	pCountryIe		- pointer to the country IE
	CountryIeLen	- length of the country IE

Return Value:
	NONE

Note:
	Must call the function in kernel thread.
========================================================================
*/
VOID CFG80211_RegHint(
	IN VOID						*pAdCB,
	IN UCHAR					*pCountryIe,
	IN ULONG					CountryIeLen)
{
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pAdCB;


	CFG80211OS_RegHint(CFG80211CB, pCountryIe, CountryIeLen);
} /* End of CFG80211_RegHint */


/*
========================================================================
Routine Description:
	Hint to the wireless core a regulatory domain from country element.

Arguments:
	pAdCB			- WLAN control block pointer
	pCountryIe		- pointer to the country IE
	CountryIeLen	- length of the country IE

Return Value:
	NONE

Note:
	Must call the function in kernel thread.
========================================================================
*/
VOID CFG80211_RegHint11D(
	IN VOID						*pAdCB,
	IN UCHAR					*pCountryIe,
	IN ULONG					CountryIeLen)
{
	/* no regulatory_hint_11d() in 2.6.32 */
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pAdCB;


	CFG80211OS_RegHint11D(CFG80211CB, pCountryIe, CountryIeLen);
} /* End of CFG80211_RegHint11D */


/*
========================================================================
Routine Description:
	Apply new regulatory rule.

Arguments:
	pAdCB			- WLAN control block pointer
	pWiphy			- Wireless hardware description
	pAlpha2			- Regulation domain (2B)

Return Value:
	NONE

Note:
	Can only be called when interface is up.

	For general mac80211 device, it will be set to new power by Ops->config()
	In rt2x00/, the settings is done in rt2x00lib_config().
========================================================================
*/
VOID CFG80211_RegRuleApply(
	IN VOID						*pAdCB,
	IN VOID						*pWiphy,
	IN UCHAR					*pAlpha2)
{
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pAdCB;
	VOID *pBand24G, *pBand5G;
	RADAR_DETECT_STRUCT	*pRadarDetect;
	UINT32 IdBand, IdChan, IdPwr;
	UINT32 ChanNum, ChanId, Power, RecId, DfsType;
	BOOLEAN FlgIsRadar;
	ULONG IrqFlags;


	CFG80211DBG(RT_DEBUG_ERROR, ("crda> CFG80211_RegRuleApply ==>\n"));

	/* init */
	pBand24G = NULL;
	pBand5G = NULL;

	if (pAd == NULL)
		return;

	RTMP_IRQ_LOCK(&pAd->irq_lock, IrqFlags);

	/* zero first */
	NdisZeroMemory(pAd->ChannelList,
					MAX_NUM_OF_CHANNELS * sizeof(CHANNEL_TX_POWER));

	/* 2.4GHZ & 5GHz */
	RecId = 0;
	pRadarDetect = &pAd->CommonCfg.RadarDetect;

	/* find the DfsType */
	DfsType = CE;

	pBand24G = NULL;
	pBand5G = NULL;

	if (CFG80211OS_BandInfoGet(CFG80211CB, pWiphy, &pBand24G, &pBand5G) == FALSE)
		return;

#ifdef AUTO_CH_SELECT_ENHANCE
#ifdef EXT_BUILD_CHANNEL_LIST
	if ((pAlpha2[0] != '0') && (pAlpha2[1] != '0'))
	{
		UINT32 IdReg;

		if (pBand5G != NULL)
		{
			for(IdReg=0; ; IdReg++)
			{
				if (ChRegion[IdReg].CountReg[0] == 0x00)
					break;
				/* End of if */
	
				if ((pAlpha2[0] == ChRegion[IdReg].CountReg[0]) &&
					(pAlpha2[1] == ChRegion[IdReg].CountReg[1]))
				{
					DfsType = ChRegion[IdReg].DfsType;
	
					CFG80211DBG(RT_DEBUG_ERROR,
								("crda> find region %c%c, DFS Type %d\n",
								pAlpha2[0], pAlpha2[1], DfsType));
					break;
				} /* End of if */
			} /* End of for */
		} /* End of if */
	} /* End of if */
#endif /* EXT_BUILD_CHANNEL_LIST */
#endif /* AUTO_CH_SELECT_ENHANCE */

	for(IdBand=0; IdBand<2; IdBand++)
	{
		if (((IdBand == 0) && (pBand24G == NULL)) ||
			((IdBand == 1) && (pBand5G == NULL)))
		{
			continue;
		} /* End of if */

		if (IdBand == 0)
		{
			CFG80211DBG(RT_DEBUG_ERROR, ("crda> reset chan/power for 2.4GHz\n"));
		}
		else
		{
			CFG80211DBG(RT_DEBUG_ERROR, ("crda> reset chan/power for 5GHz\n"));
		} /* End of if */

		ChanNum = CFG80211OS_ChanNumGet(CFG80211CB, pWiphy, IdBand);

		for(IdChan=0; IdChan<ChanNum; IdChan++)
		{
			if (CFG80211OS_ChanInfoGet(CFG80211CB, pWiphy, IdBand, IdChan,
									&ChanId, &Power, &FlgIsRadar) == FALSE)
			{
				/* the channel is not allowed in the regulatory domain */
				/* get next channel information */
				continue;
			} /* End of if */

			if ((pAd->CommonCfg.PhyMode == PHY_11A) ||
				(pAd->CommonCfg.PhyMode == PHY_11AN_MIXED))
			{
				/* 5G-only mode */
				if (ChanId <= CFG80211_NUM_OF_CHAN_2GHZ)
					continue; /* check next */
				/* End of if */
			} /* End of if */

			if ((pAd->CommonCfg.PhyMode != PHY_11A) &&
				(pAd->CommonCfg.PhyMode != PHY_11ABG_MIXED) &&
				(pAd->CommonCfg.PhyMode != PHY_11AN_MIXED) &&
				(pAd->CommonCfg.PhyMode != PHY_11ABGN_MIXED) &&
				(pAd->CommonCfg.PhyMode != PHY_11AGN_MIXED))
			{
				/* 2.5G-only mode */
				if (ChanId > CFG80211_NUM_OF_CHAN_2GHZ)
					continue; /* check next */
				/* End of if */
			} /* End of if */

			for(IdPwr=0; IdPwr<MAX_NUM_OF_CHANNELS; IdPwr++)
			{
				if (ChanId == pAd->TxPower[IdPwr].Channel)
				{
					/* init the channel info. */
					NdisMoveMemory(&pAd->ChannelList[RecId],
									&pAd->TxPower[IdPwr],
									sizeof(CHANNEL_TX_POWER));

					/* keep channel number */
					pAd->ChannelList[RecId].Channel = ChanId;

					/* keep maximum tranmission power */
					pAd->ChannelList[RecId].MaxTxPwr = Power;

					/* keep DFS flag */
					if (FlgIsRadar == TRUE)
						pAd->ChannelList[RecId].DfsReq = TRUE;
					else
						pAd->ChannelList[RecId].DfsReq = FALSE;
					/* End of if */

					/* keep DFS type */
					pAd->ChannelList[RecId].RegulatoryDomain = DfsType;

					/* re-set DFS info. */
					pRadarDetect->RDDurRegion = DfsType;

					if (DfsType == JAP_W53)
						pRadarDetect->DfsSessionTime = 15;
					else if (DfsType == JAP_W56)
						pRadarDetect->DfsSessionTime = 13;
					else if (DfsType == JAP)
						pRadarDetect->DfsSessionTime = 5;
					else if (DfsType == FCC)
					{
						pRadarDetect->DfsSessionTime = 5;
					}
					else if (DfsType == CE)
						pRadarDetect->DfsSessionTime = 13;
					else
						pRadarDetect->DfsSessionTime = 13;
					/* End of if */

					CFG80211DBG(RT_DEBUG_ERROR,
								("Chan %03d:\tpower %d dBm, "
								"DFS %d, DFS Type %d\n",
								ChanId, Power,
								((FlgIsRadar == TRUE)?1:0),
								DfsType));

					/* change to record next channel info. */
					RecId ++;
					break;
				} /* End of if */
			} /* End of for */
		} /* End of for */
	} /* End of for */

	pAd->ChannelListNum = RecId;
	RTMP_IRQ_UNLOCK(&pAd->irq_lock, IrqFlags);

	CFG80211DBG(RT_DEBUG_ERROR, ("crda> Number of channels = %d\n", RecId));
} /* End of CFG80211_RegRuleApply */


/*
========================================================================
Routine Description:
	Inform us that a scan is got.

Arguments:
	pAdCB				- WLAN control block pointer

Return Value:
	NONE

Note:
	Call RT_CFG80211_SCANNING_INFORM, not CFG80211_Scaning
========================================================================
*/
VOID CFG80211_Scaning(
	IN VOID							*pAdCB,
	IN UINT32						BssIdx,
	IN UINT32						ChanId,
	IN UCHAR						*pFrame,
	IN UINT32						FrameLen,
	IN INT32						RSSI)
{
#ifdef CONFIG_STA_SUPPORT
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pAdCB;
	VOID *pCfg80211_CB = pAd->pCfg80211_CB;
	BOOLEAN FlgIsNMode;
	UINT8 BW;


	CFG80211DBG(RT_DEBUG_ERROR, ("80211> CFG80211_Scaning ==>\n"));

	if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE))
	{
		DBGPRINT(RT_DEBUG_TRACE, ("80211> Network is down!\n"));
		return;
	} /* End of if */

	/*
		In connect function, we also need to report BSS information to cfg80211;
		Not only scan function.
	*/
	if ((pAd->FlgCfg80211Scanning == FALSE) &&
		(pAd->FlgCfg80211Connecting == FALSE))
	{
		return; /* no scan is running */
	} /* End of if */

	/* init */
	/* Note: Can not use local variable to do pChan */
	if (pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED)
		FlgIsNMode = TRUE;
	else
		FlgIsNMode = FALSE;

	if (pAd->CommonCfg.RegTransmitSetting.field.BW == BW_20)
		BW = 0;
	else
		BW = 1;

	CFG80211OS_Scaning(pCfg80211_CB,
						&pAd->ScanTab.BssEntry[BssIdx].pCfg80211_Chan,
						ChanId,
						pFrame,
						FrameLen,
						RSSI,
						FlgIsNMode,
						BW);
#endif /* CONFIG_STA_SUPPORT */
} /* End of CFG80211_Scaning */


/*
========================================================================
Routine Description:
	Inform us that scan ends.

Arguments:
	pAdCB			- WLAN control block pointer
	FlgIsAborted	- 1: scan is aborted

Return Value:
	NONE

Note:
========================================================================
*/
VOID CFG80211_ScanEnd(
	IN VOID						*pAdCB,
	IN BOOLEAN					FlgIsAborted)
{
#ifdef CONFIG_STA_SUPPORT
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pAdCB;


	if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE))
	{
		DBGPRINT(RT_DEBUG_TRACE, ("80211> Network is down!\n"));
		return;
	} /* End of if */

	if (pAd->FlgCfg80211Scanning == FALSE)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("80211> No scan is running!\n"));
		return; /* no scan is running */
	} /* End of if */

	if (FlgIsAborted == TRUE)
		FlgIsAborted = 1;
	else
		FlgIsAborted = 0;
	/* End of if */

	CFG80211OS_ScanEnd(CFG80211CB, FlgIsAborted);

	pAd->FlgCfg80211Scanning = FALSE;
#endif /* CONFIG_STA_SUPPORT */
} /* End of CFG80211_ScanEnd */


/*
========================================================================
Routine Description:
	Inform CFG80211 about association status.

Arguments:
	pAdCB			- WLAN control block pointer
	pBSSID			- the BSSID of the AP
	pReqIe			- the element list in the association request frame
	ReqIeLen		- the request element length
	pRspIe			- the element list in the association response frame
	RspIeLen		- the response element length
	FlgIsSuccess	- 1: success; otherwise: fail

Return Value:
	None

Note:
========================================================================
*/
VOID CFG80211_ConnectResultInform(
	IN VOID						*pAdCB,
	IN UCHAR					*pBSSID,
	IN UCHAR					*pReqIe,
	IN UINT32					ReqIeLen,
	IN UCHAR					*pRspIe,
	IN UINT32					RspIeLen,
	IN UCHAR					FlgIsSuccess)
{
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pAdCB;


	CFG80211DBG(RT_DEBUG_ERROR, ("80211> CFG80211_ConnectResultInform ==>\n"));

	CFG80211OS_ConnectResultInform(CFG80211CB,
								pBSSID,
								pReqIe,
								ReqIeLen,
								pRspIe,
								RspIeLen,
								FlgIsSuccess);

	pAd->FlgCfg80211Connecting = FALSE;
} /* End of CFG80211_ConnectResultInform */


/*
========================================================================
Routine Description:
	Re-Initialize wireless channel/PHY in 2.4GHZ and 5GHZ.

Arguments:
	pAdCB			- WLAN control block pointer

Return Value:
	TRUE			- re-init successfully
	FALSE			- re-init fail

Note:
	CFG80211_SupBandInit() is called in xx_probe().
	But we do not have complete chip information in xx_probe() so we
	need to re-init bands in xx_open().
========================================================================
*/
BOOLEAN CFG80211_SupBandReInit(
	IN VOID						*pAdCB)
{
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pAdCB;
	CFG80211_BAND BandInfo;


	CFG80211DBG(RT_DEBUG_ERROR, ("80211> re-init bands...\n"));

	/* re-init bands */
	NdisZeroMemory(&BandInfo, sizeof(BandInfo));
	CFG80211_BANDINFO_FILL(pAd, &BandInfo);

	return CFG80211OS_SupBandReInit(CFG80211CB, &BandInfo);
} /* End of CFG80211_SupBandReInit */


#endif /* RT_CFG80211_SUPPORT */

/* End of cfg80211drv.c */
