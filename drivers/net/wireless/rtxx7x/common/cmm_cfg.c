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


#include "rt_config.h"



INT ComputeChecksum(
	IN UINT PIN)
{
	INT digit_s;
    UINT accum = 0;

	PIN *= 10;
	accum += 3 * ((PIN / 10000000) % 10);
	accum += 1 * ((PIN / 1000000) % 10);
	accum += 3 * ((PIN / 100000) % 10);
	accum += 1 * ((PIN / 10000) % 10);
	accum += 3 * ((PIN / 1000) % 10);
	accum += 1 * ((PIN / 100) % 10);
	accum += 3 * ((PIN / 10) % 10);

	digit_s = (accum % 10);
	return ((10 - digit_s) % 10);
} /* ComputeChecksum*/

UINT GenerateWpsPinCode(
	IN	PRTMP_ADAPTER	pAd,
    IN  BOOLEAN         bFromApcli,
	IN	UCHAR			apidx)
{
	UCHAR	macAddr[MAC_ADDR_LEN];
	UINT 	iPin;
	UINT	checksum;

#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		NdisMoveMemory(&macAddr[0], pAd->CurrentAddress, MAC_ADDR_LEN);
#endif /* CONFIG_STA_SUPPORT */

	iPin = macAddr[3] * 256 * 256 + macAddr[4] * 256 + macAddr[5];

	iPin = iPin % 10000000;
	checksum = ComputeChecksum( iPin );
	iPin = iPin*10 + checksum;

	return iPin;
}

char* GetPhyMode(
	int Mode)
{
	switch(Mode)
	{
		case MODE_CCK:
			return "CCK";

		case MODE_OFDM:
			return "OFDM";
#ifdef DOT11_N_SUPPORT
		case MODE_HTMIX:
			return "HTMIX";

		case MODE_HTGREENFIELD:
			return "GREEN";
#endif /* DOT11_N_SUPPORT */
		default:
			return "N/A";
	}
}


char* GetBW(
	int BW)
{
	switch(BW)
	{
		case BW_10:
			return "10M";

		case BW_20:
			return "20M";
#ifdef DOT11_N_SUPPORT
		case BW_40:
			return "40M";
#endif /* DOT11_N_SUPPORT */
		default:
			return "N/A";
	}
}


/*
    ==========================================================================
    Description:
        Set Country Region to pAd->CommonCfg.CountryRegion.
        This command will not work, if the field of CountryRegion in eeprom is programmed.

    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT RT_CfgSetCountryRegion(
	IN PRTMP_ADAPTER	pAd,
	IN PSTRING			arg,
	IN INT				band)
{
	LONG region;
	UCHAR *pCountryRegion;

	region = simple_strtol(arg, 0, 10);

	if (band == BAND_24G)
		pCountryRegion = &pAd->CommonCfg.CountryRegion;
	else
		pCountryRegion = &pAd->CommonCfg.CountryRegionForABand;

	/* TODO: Is it neccesay for following check???*/
	/* Country can be set only when EEPROM not programmed*/
	if (*pCountryRegion & 0x80)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("CfgSetCountryRegion():CountryRegion in eeprom was programmed\n"));
		return FALSE;
	}

	if((region >= 0) &&
	   (((band == BAND_24G) &&((region <= REGION_MAXIMUM_BG_BAND) ||
	   (region == REGION_31_BG_BAND) || (region == REGION_32_BG_BAND) || (region == REGION_33_BG_BAND) )) ||
	    ((band == BAND_5G) && (region <= REGION_MAXIMUM_A_BAND) ))
	  )
	{
		*pCountryRegion= (UCHAR) region;
	}
	else
	{
		DBGPRINT(RT_DEBUG_ERROR, ("CfgSetCountryRegion():region(%ld) out of range!\n", region));
		return FALSE;
	}

	return TRUE;

}


/*
    ==========================================================================
    Description:
        Set Wireless Mode
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT RT_CfgSetWirelessMode(
	IN	PRTMP_ADAPTER	pAd,
	IN	PSTRING			arg)
{
	INT		MaxPhyMode = PHY_11G;
	LONG	WirelessMode;

#ifdef DOT11_N_SUPPORT
	if (!RTMP_TEST_MORE_FLAG(pAd, fRTMP_ADAPTER_DISABLE_DOT_11N))
		MaxPhyMode = PHY_11N_5G;
#endif /* DOT11_N_SUPPORT */

	WirelessMode = simple_strtol(arg, 0, 10);

	/* check if chip support 5G band when WirelessMode is 5G band */
	if (PHY_MODE_IS_5G_BAND(WirelessMode))
	{
		if (!RFIC_IS_5G_BAND(pAd))
		{
			DBGPRINT(RT_DEBUG_ERROR,
					("phy mode> Error! The chip does not support 5G band %d!\n",
					pAd->RfIcType));
			return FALSE;
		}
	}

	if (WirelessMode <= MaxPhyMode)
	{
		pAd->CommonCfg.PhyMode = WirelessMode;
		pAd->CommonCfg.DesiredPhyMode = WirelessMode;
		return TRUE;
	}

	return FALSE;

}


/* maybe can be moved to GPL code, ap_mbss.c, but the code will be open */


INT RT_CfgSetShortSlot(
	IN	PRTMP_ADAPTER	pAd,
	IN	PSTRING			arg)
{
	LONG ShortSlot;

	ShortSlot = simple_strtol(arg, 0, 10);

	if (ShortSlot == 1)
		pAd->CommonCfg.bUseShortSlotTime = TRUE;
	else if (ShortSlot == 0)
		pAd->CommonCfg.bUseShortSlotTime = FALSE;
	else
		return FALSE;  /*Invalid argument */

	return TRUE;
}


/*
    ==========================================================================
    Description:
        Set WEP KEY base on KeyIdx
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT	RT_CfgSetWepKey(
	IN	PRTMP_ADAPTER	pAd,
	IN	PSTRING			keyString,
	IN	CIPHER_KEY		*pSharedKey,
	IN	INT				keyIdx)
{
	INT				KeyLen;
	INT				i;
	/*UCHAR			CipherAlg = CIPHER_NONE;*/
	BOOLEAN			bKeyIsHex = FALSE;

	/* TODO: Shall we do memset for the original key info??*/
	memset(pSharedKey, 0, sizeof(CIPHER_KEY));
	KeyLen = strlen(keyString);
	switch (KeyLen)
	{
		case 5: /*wep 40 Ascii type*/
		case 13: /*wep 104 Ascii type*/
			bKeyIsHex = FALSE;
			pSharedKey->KeyLen = KeyLen;
			NdisMoveMemory(pSharedKey->Key, keyString, KeyLen);
			break;

		case 10: /*wep 40 Hex type*/
		case 26: /*wep 104 Hex type*/
			for(i=0; i < KeyLen; i++)
			{
				if( !isxdigit(*(keyString+i)) )
					return FALSE;  /*Not Hex value;*/
			}
			bKeyIsHex = TRUE;
			pSharedKey->KeyLen = KeyLen/2 ;
			AtoH(keyString, pSharedKey->Key, pSharedKey->KeyLen);
			break;

		default: /*Invalid argument */
			DBGPRINT(RT_DEBUG_TRACE, ("RT_CfgSetWepKey(keyIdx=%d):Invalid argument (arg=%s)\n", keyIdx, keyString));
			return FALSE;
	}

	pSharedKey->CipherAlg = ((KeyLen % 5) ? CIPHER_WEP128 : CIPHER_WEP64);
	DBGPRINT(RT_DEBUG_TRACE, ("RT_CfgSetWepKey:(KeyIdx=%d,type=%s, Alg=%s)\n",
						keyIdx, (bKeyIsHex == FALSE ? "Ascii" : "Hex"), CipherName[pSharedKey->CipherAlg]));

	return TRUE;
}


/*
    ==========================================================================
    Description:
        Set WPA PSK key

    Arguments:
        pAdapter	Pointer to our adapter
        keyString	WPA pre-shared key string
        pHashStr	String used for password hash function
        hashStrLen	Lenght of the hash string
        pPMKBuf		Output buffer of WPAPSK key

    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
INT RT_CfgSetWPAPSKKey(
	IN RTMP_ADAPTER	*pAd,
	IN PSTRING		keyString,
	IN UCHAR		*pHashStr,
	IN INT			hashStrLen,
	OUT PUCHAR		pPMKBuf)
{
	int keyLen;
	UCHAR keyMaterial[40];

	keyLen = strlen(keyString);
	if ((keyLen < 8) || (keyLen > 64))
	{
		DBGPRINT(RT_DEBUG_TRACE, ("WPAPSK Key length(%d) error, required 8 ~ 64 characters!(keyStr=%s)\n",
									keyLen, keyString));
		return FALSE;
	}

	memset(pPMKBuf, 0, 32);
	if (keyLen == 64)
	{
	    AtoH(keyString, pPMKBuf, 32);
	}
	else
	{
	    RtmpPasswordHash(keyString, pHashStr, hashStrLen, keyMaterial);
	    NdisMoveMemory(pPMKBuf, keyMaterial, 32);
	}

	return TRUE;
}

INT	RT_CfgSetFixedTxPhyMode(
	IN	PSTRING			arg)
{
	INT		fix_tx_mode = FIXED_TXMODE_HT;
	UINT32	value;

	if (strcmp(arg, "OFDM") == 0 || strcmp(arg, "ofdm") == 0)
	{
		fix_tx_mode = FIXED_TXMODE_OFDM;
	}
	else if (strcmp(arg, "CCK") == 0 || strcmp(arg, "cck") == 0)
	{
	    fix_tx_mode = FIXED_TXMODE_CCK;
	}
	else if (strcmp(arg, "HT") == 0 || strcmp(arg, "ht") == 0)
	{
	    fix_tx_mode = FIXED_TXMODE_HT;
	}
	else
	{
		value = simple_strtol(arg, 0, 10);
		/* 1 : CCK*/
		/* 2 : OFDM*/
		/* otherwise : HT*/
		if (value == FIXED_TXMODE_CCK || value == FIXED_TXMODE_OFDM)
			fix_tx_mode = value;
		else
			fix_tx_mode = FIXED_TXMODE_HT;
	}

	return fix_tx_mode;

}

INT	RT_CfgSetMacAddress(
	IN 	PRTMP_ADAPTER 	pAd,
	IN	PSTRING			arg)
{
	INT	i, mac_len;

	/* Mac address acceptable format 01:02:03:04:05:06 length 17 */
	mac_len = strlen(arg);
	if(mac_len != 17)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s : invalid length (%d)\n", __FUNCTION__, mac_len));
		return FALSE;
	}

	if(strcmp(arg, "00:00:00:00:00:00") == 0)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s : invalid mac setting \n", __FUNCTION__));
		return FALSE;
	}

	for (i = 0; i < MAC_ADDR_LEN; i++)
	{
		AtoH(arg, &pAd->CurrentAddress[i], 1);
		arg = arg + 3;
	}

	pAd->bLocalAdminMAC = TRUE;
	return TRUE;
}

INT	RT_CfgSetTxMCSProc(
	IN	PSTRING			arg,
	OUT	BOOLEAN			*pAutoRate)
{
	INT	Value = simple_strtol(arg, 0, 10);
	INT	TxMcs;

	if ((Value >= 0 && Value <= 23) || (Value == 32)) /* 3*3*/
	{
		TxMcs = Value;
		*pAutoRate = FALSE;
	}
	else
	{
		TxMcs = MCS_AUTO;
		*pAutoRate = TRUE;
	}

	return TxMcs;

}

INT	RT_CfgSetAutoFallBack(
	IN 	PRTMP_ADAPTER 	pAd,
	IN	PSTRING			arg)
{
	TX_RTY_CFG_STRUC	tx_rty_cfg;
	UCHAR				AutoFallBack = (UCHAR)simple_strtol(arg, 0, 10);

	RTMP_IO_READ32(pAd, TX_RTY_CFG, &tx_rty_cfg.word);
	tx_rty_cfg.field.TxautoFBEnable = (AutoFallBack) ? 1 : 0;
	RTMP_IO_WRITE32(pAd, TX_RTY_CFG, tx_rty_cfg.word);
	DBGPRINT(RT_DEBUG_TRACE, ("RT_CfgSetAutoFallBack::(tx_rty_cfg=0x%x)\n", tx_rty_cfg.word));
	return TRUE;
}


/*
========================================================================
Routine Description:
	Handler for CMD_RTPRIV_IOCTL_STA_SIOCGIWNAME.

Arguments:
	pAd				- WLAN control block pointer
	*pData			- the communication data pointer
	Data			- the communication data

Return Value:
	NDIS_STATUS_SUCCESS or NDIS_STATUS_FAILURE

Note:
========================================================================
*/
INT RtmpIoctl_rt_ioctl_giwname(
	IN	RTMP_ADAPTER			*pAd,
	IN	VOID					*pData,
	IN	ULONG					Data)
{
	UCHAR CurOpMode = OPMODE_AP;

	if (CurOpMode == OPMODE_AP)
	{
		strcpy(pData, "RTWIFI SoftAP");
	}

	return NDIS_STATUS_SUCCESS;
}


INT RTMP_COM_IoctlHandle(
	IN	VOID					*pAdSrc,
	IN	RTMP_IOCTL_INPUT_STRUCT	*wrq,
	IN	INT						cmd,
	IN	USHORT					subcmd,
	IN	VOID					*pData,
	IN	ULONG					Data)
{
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pAdSrc;
	POS_COOKIE pObj = (POS_COOKIE)pAd->OS_Cookie;
	INT Status = NDIS_STATUS_SUCCESS, i;
	UCHAR PermanentAddress[MAC_ADDR_LEN];
	USHORT Addr01, Addr23, Addr45;


	pObj = pObj; /* avoid compile warning */

	switch(cmd)
	{
		case CMD_RTPRIV_IOCTL_NETDEV_GET:
		/* get main net_dev */
		{
			VOID **ppNetDev = (VOID **)pData;
			*ppNetDev = (VOID *)(pAd->net_dev);
		}
			break;

		case CMD_RTPRIV_IOCTL_NETDEV_SET:
		/* set main net_dev */
			pAd->net_dev = pData;

			break;

		case CMD_RTPRIV_IOCTL_OPMODE_GET:
		/* get Operation Mode */
			*(ULONG *)pData = pAd->OpMode;
			break;


		case CMD_RTPRIV_IOCTL_TASK_LIST_GET:
		/* get all Tasks */
		{
			RT_CMD_WAIT_QUEUE_LIST *pList = (RT_CMD_WAIT_QUEUE_LIST *)pData;

			pList->pMlmeTask = &pAd->mlmeTask;
#ifdef RTMP_TIMER_TASK_SUPPORT
			pList->pTimerTask = &pAd->timerTask;
#endif /* RTMP_TIMER_TASK_SUPPORT */
			pList->pCmdQTask = &pAd->cmdQTask;
		}
			break;

		case CMD_RTPRIV_IOCTL_IRQ_INIT:
		/* init IRQ */
			RTMP_IRQ_INIT(pAd);
			break;

		case CMD_RTPRIV_IOCTL_IRQ_RELEASE:
		/* release IRQ */
			RTMP_OS_IRQ_RELEASE(pAd, pAd->net_dev);
			break;


		case CMD_RTPRIV_IOCTL_NIC_NOT_EXIST:
		/* set driver state to fRTMP_ADAPTER_NIC_NOT_EXIST */
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST);
			break;

#ifdef CONFIG_APSTA_MIXED_SUPPORT
		case CMD_RTPRIV_IOCTL_MAX_IN_BIT:
			/* set MAX_IN_BIT for WMM */
			CW_MAX_IN_BITS = Data;
			break;
#endif /* CONFIG_APSTA_MIXED_SUPPORT */
#ifdef CONFIG_STA_SUPPORT
		case CMD_RTPRIV_IOCTL_ADAPTER_SEND_DISSASSOCIATE:
		/* clear driver state to fRTMP_ADAPTER_SUSPEND */
			RtmpOSWrielessEventSend(pAd->net_dev, RT_WLAN_EVENT_CGIWAP, -1, NULL, NULL, 0);
			break;

#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND
                case CMD_RTPRIV_IOCTL_USB_DEV_GET:
                /* get USB DEV */
                {
                        VOID **ppUsb_Dev = (VOID **)pData;
                        *ppUsb_Dev = (VOID *)(pObj->pUsb_Dev);
                }
                        break;

                case CMD_RTPRIV_IOCTL_USB_INTF_GET:
                /* get USB INTF */
                {
                        VOID **ppINTF = (VOID **)pData;
                        *ppINTF = (VOID *)(pObj->intf);
                }
                        break;


		case CMD_RTPRIV_IOCTL_ADAPTER_SUSPEND_SET:
		/* set driver state to fRTMP_ADAPTER_SUSPEND */
			RTMP_SET_FLAG(pAd,fRTMP_ADAPTER_SUSPEND);
			break;

		case CMD_RTPRIV_IOCTL_ADAPTER_SUSPEND_CLEAR:
		/* clear driver state to fRTMP_ADAPTER_SUSPEND */
			RTMP_CLEAR_FLAG(pAd,fRTMP_ADAPTER_SUSPEND);
			break;

		case CMD_RTPRIV_IOCTL_ADAPTER_SUSPEND_TEST:
		/* test driver state to fRTMP_ADAPTER_SUSPEND */
			*(UCHAR *)pData = RTMP_TEST_FLAG(pAd,fRTMP_ADAPTER_SUSPEND);
			break;

		case CMD_RTPRIV_IOCTL_ADAPTER_CPU_SUSPEND_SET:
		/* set driver state to fRTMP_ADAPTER_CPU_SUSPEND */
			DBGPRINT(RT_DEBUG_TRACE, ("CMD_RTPRIV_IOCTL_ADAPTER_CPU_SUSPEND_SET\n"));
			RTMP_SET_FLAG(pAd,fRTMP_ADAPTER_CPU_SUSPEND);
			break;

		case CMD_RTPRIV_IOCTL_ADAPTER_CPU_SUSPEND_CLEAR:
		/* clear driver state to fRTMP_ADAPTER_CPU_SUSPEND */
			DBGPRINT(RT_DEBUG_TRACE, ("CMD_RTPRIV_IOCTL_ADAPTER_CPU_SUSPEND_CLEAR\n"));
			RTMP_CLEAR_FLAG(pAd,fRTMP_ADAPTER_CPU_SUSPEND);
			break;

		case CMD_RTPRIV_IOCTL_ADAPTER_CPU_SUSPEND_TEST:
		/* test driver state to fRTMP_ADAPTER_CPU_SUSPEND */
			*(UCHAR *)pData = RTMP_TEST_FLAG(pAd,fRTMP_ADAPTER_CPU_SUSPEND);
			break;

		case CMD_RTPRIV_IOCTL_ADAPTER_IDLE_RADIO_OFF_TEST:
		/* test driver state to fRTMP_ADAPTER_IDLE_RADIO_OFF */
			*(UCHAR *)pData = RTMP_TEST_FLAG(pAd,fRTMP_ADAPTER_IDLE_RADIO_OFF);
			break;

		case CMD_RTPRIV_IOCTL_ADAPTER_RT28XX_USB_ASICRADIO_OFF:
		/* RT28xxUsbAsicRadioOff */
			RT28xxUsbAsicRadioOff(pAd);
			break;

		case CMD_RTPRIV_IOCTL_ADAPTER_RT28XX_USB_ASICRADIO_ON:
		/* RT28xxUsbAsicRadioOn */
			RT28xxUsbAsicRadioOn(pAd);
			break;

#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */

		case CMD_RTPRIV_IOCTL_AP_BSSID_GET:
			if (pAd->StaCfg.PortSecured == WPA_802_1X_PORT_NOT_SECURED)
				NdisCopyMemory(pData, pAd->MlmeAux.Bssid, 6);
			else
				return NDIS_STATUS_FAILURE;
			break;
#endif /* CONFIG_STA_SUPPORT */

		case CMD_RTPRIV_IOCTL_SANITY_CHECK:
		/* sanity check before IOCTL */
			if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE))
				return NDIS_STATUS_FAILURE;
			break;

		case CMD_RTPRIV_IOCTL_SIOCGIWFREQ:
		/* get channel number */
			*(ULONG *)pData = pAd->CommonCfg.Channel;
			break;



		case CMD_RTPRIV_IOCTL_BEACON_UPDATE:
		/* update all beacon contents */
			break;

		case CMD_RTPRIV_IOCTL_RXPATH_GET:
		/* get the number of rx path */
			*(ULONG *)pData = pAd->Antenna.field.RxPath;
			break;

		case CMD_RTPRIV_IOCTL_CHAN_LIST_NUM_GET:
			*(ULONG *)pData = pAd->ChannelListNum;
			break;

		case CMD_RTPRIV_IOCTL_CHAN_LIST_GET:
		{
			UINT32 i;
			UCHAR *pChannel = (UCHAR *)pData;

			for (i = 1; i <= pAd->ChannelListNum; i++)
			{
				*pChannel = pAd->ChannelList[i-1].Channel;
				pChannel ++;
			}
		}
			break;

		case CMD_RTPRIV_IOCTL_FREQ_LIST_GET:
		{
			UINT32 i;
			UINT32 *pFreq = (UINT32 *)pData;
			UINT32 m;

			for (i = 1; i <= pAd->ChannelListNum; i++)
			{
				m = 2412000;
				MAP_CHANNEL_ID_TO_KHZ(pAd->ChannelList[i-1].Channel, m);
				(*pFreq) = m;
				pFreq ++;
			}
		}
			break;

#ifdef RTMP_USB_SUPPORT
		case CMD_RTPRIV_IOCTL_USB_MORE_FLAG_SET:
		{
			RT_CMD_USB_MORE_FLAG_CONFIG *pConfig;
			UINT32 VendorID, ProductID;


			pConfig = (RT_CMD_USB_MORE_FLAG_CONFIG *)pData;
			VendorID = pConfig->VendorID;
			ProductID = pConfig->ProductID;

			if (VendorID == 0x0DB0)
			{
				if ((ProductID == 0x871C) || (ProductID == 0x822C))
				{
					RTMP_SET_MORE_FLAG(pAd, (fRTMP_ADAPTER_DISABLE_DOT_11N | fRTMP_ADAPTER_WSC_PBC_PIN0));
				}
				if ((ProductID == 0x871A) || (ProductID == 0x822A))
				{
					RTMP_SET_MORE_FLAG(pAd, fRTMP_ADAPTER_DISABLE_DOT_11N);
				}
				if ((ProductID == 0x871B) || (ProductID == 0x822B))
				{
					RTMP_SET_MORE_FLAG(pAd, fRTMP_ADAPTER_WSC_PBC_PIN0);
				}
			}

	    	if (VendorID == 0x07D1)
	    	{
				if (ProductID == 0x3C0F)
					RTMP_SET_MORE_FLAG(pAd, fRTMP_ADAPTER_DISABLE_DOT_11N);
	    	}
		}
			break;

		case CMD_RTPRIV_IOCTL_USB_CONFIG_INIT:
		{
			RT_CMD_USB_DEV_CONFIG *pConfig;
			UINT32 i;


			pConfig = (RT_CMD_USB_DEV_CONFIG *)pData;
			pAd->NumberOfPipes = pConfig->NumberOfPipes;
			pAd->BulkInEpAddr = pConfig->BulkInEpAddr;
			pAd->BulkInMaxPacketSize = pConfig->BulkInMaxPacketSize;
			for(i=0; i<6; i++)
			{
				pAd->BulkOutEpAddr[i] = pConfig->BulkOutEpAddr[i];
				pAd->BulkOutMaxPacketSize = pConfig->BulkOutMaxPacketSize;
			}
			pAd->config = pConfig->pConfig;
		}
			break;

		case CMD_RTPRIV_IOCTL_USB_SUSPEND:
			pAd->PM_FlgSuspend = 1;
			if (Data)
			{
				RTUSBCancelPendingBulkInIRP(pAd);
				RTUSBCancelPendingBulkOutIRP(pAd);
			}
			break;

		case CMD_RTPRIV_IOCTL_USB_RESUME:
			pAd->PM_FlgSuspend = 0;
			break;
#endif /* RTMP_USB_SUPPORT */


#ifdef RT_CFG80211_SUPPORT
		case CMD_RTPRIV_IOCTL_CFG80211_CFG_START:
			RT_CFG80211_REINIT(pAd);
			RT_CFG80211_CRDA_REG_RULE_APPLY(pAd);
			break;
#endif /* RT_CFG80211_SUPPORT */

#ifdef INF_PPA_SUPPORT
		case CMD_RTPRIV_IOCTL_INF_PPA_INIT:
			os_alloc_mem(NULL, (UCHAR **)&(pAd->pDirectpathCb), sizeof(PPA_DIRECTPATH_CB));
			break;

		case CMD_RTPRIV_IOCTL_INF_PPA_EXIT:
			if (ppa_hook_directpath_register_dev_fn && pAd->PPAEnable==TRUE)
			{
				UINT status;
				status=ppa_hook_directpath_register_dev_fn(&pAd->g_if_id, pAd->net_dev, NULL, 0);
				DBGPRINT(RT_DEBUG_TRACE, ("unregister PPA:g_if_id=%d status=%d\n",pAd->g_if_id,status));
			}
			os_free_mem(NULL, pAd->pDirectpathCb);
			break;
#endif /* INF_PPA_SUPPORT*/

		case CMD_RTPRIV_IOCTL_VIRTUAL_INF_UP:
		/* interface up */
		{
			RT_CMD_INF_UP_DOWN *pInfConf = (RT_CMD_INF_UP_DOWN *)pData;

			if (VIRTUAL_IF_NUM(pAd) == 0)
			{
				if (pInfConf->rt28xx_open(pAd->net_dev) != 0)
				{
					DBGPRINT(RT_DEBUG_TRACE, ("rt28xx_open return fail!\n"));
					return NDIS_STATUS_FAILURE;
				}
				else
					VIRTUAL_IF_INC(pAd);
			}
			else
			{
			}
			
		}
			break;

		case CMD_RTPRIV_IOCTL_VIRTUAL_INF_DOWN:
		/* interface down */
		{
			RT_CMD_INF_UP_DOWN *pInfConf = (RT_CMD_INF_UP_DOWN *)pData;

		
			if (VIRTUAL_IF_NUM(pAd) > 0)
				{
				pInfConf->rt28xx_close(pAd->net_dev);
				VIRTUAL_IF_DEC(pAd);				
		  }   
		}
			break;

		case CMD_RTPRIV_IOCTL_VIRTUAL_INF_GET:
		/* get virtual interface number */
			*(ULONG *)pData = VIRTUAL_IF_NUM(pAd);
			break;

		case CMD_RTPRIV_IOCTL_INF_TYPE_GET:
		/* get current interface type */
			*(ULONG *)pData = pAd->infType;
			break;

		case CMD_RTPRIV_IOCTL_INF_STATS_GET:
		/* get statistics */
		{
			RT_CMD_STATS *pStats = (RT_CMD_STATS *)pData;

			pStats->pStats = pAd->stats;
			pStats->rx_packets = pAd->WlanCounters.ReceivedFragmentCount.QuadPart;
			pStats->tx_packets = pAd->WlanCounters.TransmittedFragmentCount.QuadPart;
			pStats->rx_bytes = pAd->RalinkCounters.ReceivedByteCount;
			pStats->tx_bytes = pAd->RalinkCounters.TransmittedByteCount;
			pStats->rx_errors = pAd->Counters8023.RxErrors;
			pStats->tx_errors = pAd->Counters8023.TxErrors;
		    pStats->multicast = pAd->WlanCounters.MulticastReceivedFrameCount.QuadPart;   /* multicast packets received*/
		    pStats->collisions = pAd->Counters8023.OneCollision + pAd->Counters8023.MoreCollisions;  /* Collision packets*/
		    pStats->rx_over_errors = pAd->Counters8023.RxNoBuffer;                   /* receiver ring buff overflow*/
		    pStats->rx_crc_errors = 0;/*pAd->WlanCounters.FCSErrorCount;      recved pkt with crc error*/
		    pStats->rx_frame_errors = pAd->Counters8023.RcvAlignmentErrors;          /* recv'd frame alignment error*/
		    pStats->rx_fifo_errors = pAd->Counters8023.RxNoBuffer;                   /* recv'r fifo overrun*/
		}
			break;

		case CMD_RTPRIV_IOCTL_INF_IW_STATUS_GET:
		/* get wireless statistics */
		{
			UCHAR CurOpMode = OPMODE_AP;
			RT_CMD_IW_STATS *pStats = (RT_CMD_IW_STATS *)pData;

			pStats->qual = 0;
			pStats->level = 0;
			pStats->noise = 0;
			pStats->pStats = pAd->iw_stats;

#ifdef CONFIG_STA_SUPPORT
			if (pAd->OpMode == OPMODE_STA)
			{
				CurOpMode = OPMODE_STA;
			}
#endif /* CONFIG_STA_SUPPORT */

			/*check if the interface is down*/
			if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE))
				return NDIS_STATUS_FAILURE;


#ifdef CONFIG_STA_SUPPORT
			if (CurOpMode == OPMODE_STA)
				pStats->qual = ((pAd->Mlme.ChannelQuality * 12)/10 + 10);
#endif /* CONFIG_STA_SUPPORT */

			if (pStats->qual > 100)
				pStats->qual = 100;

#ifdef CONFIG_STA_SUPPORT
			if (CurOpMode == OPMODE_STA)
			{
				pStats->level =
					RTMPMaxRssi(pAd, pAd->StaCfg.RssiSample.AvgRssi0,
									pAd->StaCfg.RssiSample.AvgRssi1,
									pAd->StaCfg.RssiSample.AvgRssi2);
			}
#endif /* CONFIG_STA_SUPPORT */

#ifdef CONFIG_STA_SUPPORT
			pStats->noise = RTMPMaxRssi(pAd, pAd->StaCfg.RssiSample.AvgRssi0,
										pAd->StaCfg.RssiSample.AvgRssi1,
										pAd->StaCfg.RssiSample.AvgRssi2) -
										RTMPMinSnr(pAd, pAd->StaCfg.RssiSample.AvgSnr0,
										pAd->StaCfg.RssiSample.AvgSnr1);
#endif /* CONFIG_STA_SUPPORT */
		}
			break;

		case CMD_RTPRIV_IOCTL_INF_MAIN_CREATE:
			*(VOID **)pData = RtmpPhyNetDevMainCreate(pAd);
			break;

		case CMD_RTPRIV_IOCTL_INF_MAIN_ID_GET:
			*(ULONG *)pData = INT_MAIN;
			break;

		case CMD_RTPRIV_IOCTL_INF_MAIN_CHECK:
			if (Data != INT_MAIN)
				return NDIS_STATUS_FAILURE;
			break;

		case CMD_RTPRIV_IOCTL_INF_P2P_CHECK:
			if (Data != INT_P2P)
				return NDIS_STATUS_FAILURE;
			break;


#ifdef RALINK_ATE
#ifdef RALINK_QA
		case CMD_RTPRIV_IOCTL_ATE:
			RtmpDoAte(pAd, wrq, pData);
			break;
#endif /* RALINK_QA */
#endif /* RALINK_ATE */

		case CMD_RTPRIV_IOCTL_MAC_ADDR_GET:

			RT28xx_EEPROM_READ16(pAd, 0x04, Addr01);
			RT28xx_EEPROM_READ16(pAd, 0x06, Addr23);
			RT28xx_EEPROM_READ16(pAd, 0x08, Addr45);

			PermanentAddress[0] = (UCHAR)(Addr01 & 0xff);
			PermanentAddress[1] = (UCHAR)(Addr01 >> 8);
			PermanentAddress[2] = (UCHAR)(Addr23 & 0xff);
			PermanentAddress[3] = (UCHAR)(Addr23 >> 8);
			PermanentAddress[4] = (UCHAR)(Addr45 & 0xff);
			PermanentAddress[5] = (UCHAR)(Addr45 >> 8);

			for(i=0; i<6; i++)
				*(UCHAR *)(pData+i) = PermanentAddress[i];
			break;

		case CMD_RTPRIV_IOCTL_SIOCGIWNAME:
			RtmpIoctl_rt_ioctl_giwname(pAd, pData, 0);
			break;

#ifdef RT_CFG80211_SUPPORT
		case CMD_RTPRIV_IOCTL_80211_CB_GET:
			*(VOID **)pData = (VOID *)(pAd->pCfg80211_CB);
			break;

		case CMD_RTPRIV_IOCTL_80211_CB_SET:
			pAd->pCfg80211_CB = pData;
			break;

		case CMD_RTPRIV_IOCTL_80211_CHAN_SET:
			if (CFG80211DRV_OpsSetChannel(pAd, pData) != TRUE)
				return NDIS_STATUS_FAILURE;
			break;

		case CMD_RTPRIV_IOCTL_80211_VIF_CHG:
			if (CFG80211DRV_OpsChgVirtualInf(pAd, pData, Data) != TRUE)
				return NDIS_STATUS_FAILURE;
			break;

		case CMD_RTPRIV_IOCTL_80211_SCAN:
			CFG80211DRV_OpsScan(pAd);
			break;

		case CMD_RTPRIV_IOCTL_80211_IBSS_JOIN:
			CFG80211DRV_OpsJoinIbss(pAd, pData);
			break;

		case CMD_RTPRIV_IOCTL_80211_STA_LEAVE:
			CFG80211DRV_OpsLeave(pAd);
			break;

		case CMD_RTPRIV_IOCTL_80211_STA_GET:
			if (CFG80211DRV_StaGet(pAd, pData) != TRUE)
				return NDIS_STATUS_FAILURE;
			break;

		case CMD_RTPRIV_IOCTL_80211_KEY_ADD:
			CFG80211DRV_KeyAdd(pAd, pData);
			break;

		case CMD_RTPRIV_IOCTL_80211_KEY_DEFAULT_SET:
#ifdef CONFIG_STA_SUPPORT
			pAd->StaCfg.DefaultKeyId = Data; /* base 0 */
#endif /* CONFIG_STA_SUPPORT */
			break;

		case CMD_RTPRIV_IOCTL_80211_CONNECT_TO:
			CFG80211DRV_Connect(pAd, pData);
			break;

#ifdef RFKILL_HW_SUPPORT
		case CMD_RTPRIV_IOCTL_80211_RFKILL:
		{
			UINT32 data = 0;
			BOOLEAN active;

			/* Read GPIO pin2 as Hardware controlled radio state */
			RTMP_IO_READ32(pAd, GPIO_CTRL_CFG, &data);
			active = !!(data & 0x04);

			if (!active)
			{
				RTMPSetLED(pAd, LED_RADIO_OFF);
				*(UINT8 *)pData = 0;
			}
			else
				*(UINT8 *)pData = 1;
		}
			break;
#endif /* RFKILL_HW_SUPPORT */

		case CMD_RTPRIV_IOCTL_80211_REG_NOTIFY_TO:
			CFG80211DRV_RegNotify(pAd, pData);
			break;

		case CMD_RTPRIV_IOCTL_80211_BANDINFO_GET:
		{
			CFG80211_BAND *pBandInfo = (CFG80211_BAND *)pData;
			CFG80211_BANDINFO_FILL(pAd, pBandInfo);
		}
			break;
#endif /* RT_CFG80211_SUPPORT */

		default:
			return NDIS_STATUS_FAILURE;
	}

	return Status;
}

/*
    ==========================================================================
    Description:
        Issue a site survey command to driver
	Arguments:
	    pAdapter                    Pointer to our adapter
	    wrq                         Pointer to the ioctl argument

    Return Value:
        None

    Note:
        Usage:
               1.) iwpriv ra0 set site_survey
    ==========================================================================
*/
INT Set_SiteSurvey_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PSTRING			arg)
{
	NDIS_802_11_SSID Ssid;
	POS_COOKIE pObj;

	pObj = (POS_COOKIE) pAd->OS_Cookie;

	//check if the interface is down
	if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE))
	{
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		return -ENETDOWN;
	}

#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		if (MONITOR_ON(pAd))
    	{
        	DBGPRINT(RT_DEBUG_TRACE, ("!!! Driver is in Monitor Mode now !!!\n"));
        	return -EINVAL;
    	}
	}
#endif // CONFIG_STA_SUPPORT //

    NdisZeroMemory(&Ssid, sizeof(NDIS_802_11_SSID));


#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		Ssid.SsidLength = 0;
		if ((arg != NULL) &&
			(strlen(arg) <= MAX_LEN_OF_SSID))
		{
			RTMPMoveMemory(Ssid.Ssid, arg, strlen(arg));
			Ssid.SsidLength = strlen(arg);
		}

		pAd->StaCfg.bScanReqIsFromWebUI = TRUE;
		StaSiteSurvey(pAd, &Ssid, SCAN_ACTIVE);
	}
#endif // CONFIG_STA_SUPPORT //

	DBGPRINT(RT_DEBUG_TRACE, ("Set_SiteSurvey_Proc\n"));

    return TRUE;
}
