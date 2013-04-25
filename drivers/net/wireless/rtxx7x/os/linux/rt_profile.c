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

#if defined (CONFIG_RA_HW_NAT)  || defined (CONFIG_RA_HW_NAT_MODULE)
#include "../../../../../../net/nat/hw_nat/ra_nat.h"
#include "../../../../../../net/nat/hw_nat/frame_engine.h"
#endif


#ifdef SYSTEM_LOG_SUPPORT
/* for wireless system event message */
char const *pWirelessSysEventText[IW_SYS_EVENT_TYPE_NUM] = {    
	/* system status event */
    "had associated successfully",							/* IW_ASSOC_EVENT_FLAG */
    "had disassociated",									/* IW_DISASSOC_EVENT_FLAG */
    "had deauthenticated",									/* IW_DEAUTH_EVENT_FLAG */
    "had been aged-out and disassociated",					/* IW_AGEOUT_EVENT_FLAG */
    "occurred CounterMeasures attack",						/* IW_COUNTER_MEASURES_EVENT_FLAG */	
    "occurred replay counter different in Key Handshaking",	/* IW_REPLAY_COUNTER_DIFF_EVENT_FLAG */
    "occurred RSNIE different in Key Handshaking",			/* IW_RSNIE_DIFF_EVENT_FLAG */
    "occurred MIC different in Key Handshaking",			/* IW_MIC_DIFF_EVENT_FLAG */
    "occurred ICV error in RX",								/* IW_ICV_ERROR_EVENT_FLAG */
    "occurred MIC error in RX",								/* IW_MIC_ERROR_EVENT_FLAG */
	"Group Key Handshaking timeout",						/* IW_GROUP_HS_TIMEOUT_EVENT_FLAG */ 
	"Pairwise Key Handshaking timeout",						/* IW_PAIRWISE_HS_TIMEOUT_EVENT_FLAG */ 
	"RSN IE sanity check failure",							/* IW_RSNIE_SANITY_FAIL_EVENT_FLAG */ 
	"set key done in WPA/WPAPSK",							/* IW_SET_KEY_DONE_WPA1_EVENT_FLAG */ 
	"set key done in WPA2/WPA2PSK",                         /* IW_SET_KEY_DONE_WPA2_EVENT_FLAG */ 
	"connects with our wireless client",                    /* IW_STA_LINKUP_EVENT_FLAG */ 
	"disconnects with our wireless client",                 /* IW_STA_LINKDOWN_EVENT_FLAG */
	"scan completed",										/* IW_SCAN_COMPLETED_EVENT_FLAG */
	"scan terminate!! Busy!! Enqueue fail!!",				/* IW_SCAN_ENQUEUE_FAIL_EVENT_FLAG */
	"channel switch to ",									/* IW_CHANNEL_CHANGE_EVENT_FLAG */
	"wireless mode is not support",							/* IW_STA_MODE_EVENT_FLAG */
	"blacklisted in MAC filter list",						/* IW_MAC_FILTER_LIST_EVENT_FLAG */
	"Authentication rejected because of challenge failure",	/* IW_AUTH_REJECT_CHALLENGE_FAILURE */
	"Scanning",												/* IW_SCANNING_EVENT_FLAG */
	"Start a new IBSS",										/* IW_START_IBSS_FLAG */
	"Join the IBSS",										/* IW_JOIN_IBSS_FLAG */
	"Shared WEP fail",										/* IW_SHARED_WEP_FAIL*/
	};

#ifdef IDS_SUPPORT
/* for wireless IDS_spoof_attack event message */
char const *pWirelessSpoofEventText[IW_SPOOF_EVENT_TYPE_NUM] = {   	
    "detected conflict SSID",								/* IW_CONFLICT_SSID_EVENT_FLAG */
    "detected spoofed association response",				/* IW_SPOOF_ASSOC_RESP_EVENT_FLAG */
    "detected spoofed reassociation responses",				/* IW_SPOOF_REASSOC_RESP_EVENT_FLAG */
    "detected spoofed probe response",						/* IW_SPOOF_PROBE_RESP_EVENT_FLAG */
    "detected spoofed beacon",								/* IW_SPOOF_BEACON_EVENT_FLAG */
    "detected spoofed disassociation",						/* IW_SPOOF_DISASSOC_EVENT_FLAG */
    "detected spoofed authentication",						/* IW_SPOOF_AUTH_EVENT_FLAG */
    "detected spoofed deauthentication",					/* IW_SPOOF_DEAUTH_EVENT_FLAG */
    "detected spoofed unknown management frame",			/* IW_SPOOF_UNKNOWN_MGMT_EVENT_FLAG */
	"detected replay attack"								/* IW_REPLAY_ATTACK_EVENT_FLAG */	
	};

/* for wireless IDS_flooding_attack event message */
char const *pWirelessFloodEventText[IW_FLOOD_EVENT_TYPE_NUM] = {   	
	"detected authentication flooding",						/* IW_FLOOD_AUTH_EVENT_FLAG */
    "detected association request flooding",				/* IW_FLOOD_ASSOC_REQ_EVENT_FLAG */
    "detected reassociation request flooding",				/* IW_FLOOD_REASSOC_REQ_EVENT_FLAG */
    "detected probe request flooding",						/* IW_FLOOD_PROBE_REQ_EVENT_FLAG */
    "detected disassociation flooding",						/* IW_FLOOD_DISASSOC_EVENT_FLAG */
    "detected deauthentication flooding",					/* IW_FLOOD_DEAUTH_EVENT_FLAG */
    "detected 802.1x eap-request flooding"					/* IW_FLOOD_EAP_REQ_EVENT_FLAG */	
	};
#endif /* IDS_SUPPORT */

#endif /* SYSTEM_LOG_SUPPORT */


NDIS_STATUS	RTMPReadParametersHook(
	IN	PRTMP_ADAPTER pAd)
{
	PSTRING					src = NULL;
	RTMP_OS_FD				srcf;
	RTMP_OS_FS_INFO			osFSInfo;
	INT 						retval = NDIS_STATUS_FAILURE;
	PSTRING					buffer;

#ifdef HOSTAPD_SUPPORT
	int i;
#endif /*HOSTAPD_SUPPORT */

/*	buffer = kmalloc(MAX_INI_BUFFER_SIZE, MEM_ALLOC_FLAG); */
	os_alloc_mem(pAd, (UCHAR **)&buffer, MAX_INI_BUFFER_SIZE);
	if(buffer == NULL)
		return NDIS_STATUS_FAILURE;
	memset(buffer, 0x00, MAX_INI_BUFFER_SIZE);
			
	{	

#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			src = STA_PROFILE_PATH;
		}
#endif /* CONFIG_STA_SUPPORT */
#ifdef MULTIPLE_CARD_SUPPORT
		src = (PSTRING)pAd->MC_FileName;
#endif /* MULTIPLE_CARD_SUPPORT */
	}

	if (src && *src)
	{
		RtmpOSFSInfoChange(&osFSInfo, TRUE);
		srcf = RtmpOSFileOpen(src, O_RDONLY, 0);
		if (IS_FILE_OPEN_ERR(srcf)) 
		{
			DBGPRINT(RT_DEBUG_ERROR, ("Open file \"%s\" failed!\n", src));
		}
		else 
		{
			retval =RtmpOSFileRead(srcf, buffer, MAX_INI_BUFFER_SIZE);
			if (retval > 0)
			{
				RTMPSetProfileParameters(pAd, buffer);
				retval = NDIS_STATUS_SUCCESS;
			}
			else
				DBGPRINT(RT_DEBUG_ERROR, ("Read file \"%s\" failed(errCode=%d)!\n", src, retval));

			retval = RtmpOSFileClose(srcf);
			if ( retval != 0)
			{
				retval = NDIS_STATUS_FAILURE;
				DBGPRINT(RT_DEBUG_ERROR, ("Close file \"%s\" failed(errCode=%d)!\n", src, retval));
			}
		}
		
		RtmpOSFSInfoChange(&osFSInfo, FALSE);
	}

#ifdef HOSTAPD_SUPPORT
		for (i = 0; i < pAd->ApCfg.BssidNum; i++)
		{
			pAd->ApCfg.MBSSID[i].Hostapd=FALSE;
			DBGPRINT(RT_DEBUG_TRACE, ("Reset ra%d hostapd support=FLASE", i));
			
		}
#endif /*HOSTAPD_SUPPORT */

/*	kfree(buffer); */
	os_free_mem(NULL, buffer);
	
	return (retval);

}


#ifdef SYSTEM_LOG_SUPPORT
/*
	========================================================================
	
	Routine Description:
		Send log message through wireless event

		Support standard iw_event with IWEVCUSTOM. It is used below.

		iwreq_data.data.flags is used to store event_flag that is defined by user. 
		iwreq_data.data.length is the length of the event log.

		The format of the event log is composed of the entry's MAC address and
		the desired log message (refer to pWirelessEventText).

			ex: 11:22:33:44:55:66 has associated successfully

		p.s. The requirement of Wireless Extension is v15 or newer. 

	========================================================================
*/
VOID RtmpDrvSendWirelessEvent(
	IN	VOID					*pAdSrc,
	IN	USHORT					Event_flag,
	IN	PUCHAR 					pAddr,
	IN  UCHAR					BssIdx,
	IN	CHAR					Rssi)
{
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pAdSrc;
	PSTRING	pBuf = NULL, pBufPtr = NULL;
	USHORT	event, type, BufLen;	
	UCHAR	event_table_len = 0;

	if (pAd->CommonCfg.bWirelessEvent == FALSE)
		return;

	type = Event_flag & 0xFF00;	
	event = Event_flag & 0x00FF;

	switch (type)
	{
		case IW_SYS_EVENT_FLAG_START:
			event_table_len = IW_SYS_EVENT_TYPE_NUM;
			break;
#ifdef IDS_SUPPORT
		case IW_SPOOF_EVENT_FLAG_START:
			event_table_len = IW_SPOOF_EVENT_TYPE_NUM;
			break;

		case IW_FLOOD_EVENT_FLAG_START:
			event_table_len = IW_FLOOD_EVENT_TYPE_NUM;
			break;
#endif /* IDS_SUPPORT */ 			
	}
	
	if (event_table_len == 0)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s : The type(%0x02x) is not valid.\n", __FUNCTION__, type));			       		       		
		return;
	}
	
	if (event >= event_table_len)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s : The event(%0x02x) is not valid.\n", __FUNCTION__, event));			       		       		
		return;
	}	
 
	/*Allocate memory and copy the msg. */
/*	if((pBuf = kmalloc(IW_CUSTOM_MAX_LEN, GFP_ATOMIC)) != NULL) */
	os_alloc_mem(NULL, (UCHAR **)&pBuf, IW_CUSTOM_MAX_LEN);
	if(pBuf != NULL)
	{
		/*Prepare the payload */
		memset(pBuf, 0, IW_CUSTOM_MAX_LEN);

		pBufPtr = pBuf;		

		if (pAddr)
			pBufPtr += sprintf(pBufPtr, "(RT2860) STA(%02x:%02x:%02x:%02x:%02x:%02x) ", PRINT_MAC(pAddr));				
		else if (BssIdx < MAX_MBSSID_NUM(pAd))
			pBufPtr += sprintf(pBufPtr, "(RT2860) BSS(ra%d) ", BssIdx);
		else
			pBufPtr += sprintf(pBufPtr, "(RT2860) ");

		if (type == IW_SYS_EVENT_FLAG_START)
        {
			pBufPtr += sprintf(pBufPtr, "%s", pWirelessSysEventText[event]);
		    
            if (Event_flag == IW_CHANNEL_CHANGE_EVENT_FLAG)
		  	{
			 	pBufPtr += sprintf(pBufPtr, "%3d", Rssi);
			}			
		}
#ifdef IDS_SUPPORT		
		else if (type == IW_SPOOF_EVENT_FLAG_START)
			pBufPtr += sprintf(pBufPtr, "%s (RSSI=%d)", pWirelessSpoofEventText[event], Rssi);
		else if (type == IW_FLOOD_EVENT_FLAG_START)
			pBufPtr += sprintf(pBufPtr, "%s", pWirelessFloodEventText[event]);
#endif /* IDS_SUPPORT */		
		else
			pBufPtr += sprintf(pBufPtr, "%s", "unknown event");
		
		pBufPtr[pBufPtr - pBuf] = '\0';
		BufLen = pBufPtr - pBuf;
		
		RtmpOSWrielessEventSend(pAd->net_dev, RT_WLAN_EVENT_CUSTOM, Event_flag, NULL, (PUCHAR)pBuf, BufLen);
		/*DBGPRINT(RT_DEBUG_TRACE, ("%s : %s\n", __FUNCTION__, pBuf)); */
	
/*		kfree(pBuf); */
		os_free_mem(NULL, pBuf);
	}
	else
		DBGPRINT(RT_DEBUG_ERROR, ("%s : Can't allocate memory for wireless event.\n", __FUNCTION__));			       		       				
}
#endif /* SYSTEM_LOG_SUPPORT */


void RTMP_IndicateMediaState(
	IN	PRTMP_ADAPTER		pAd,
	IN  NDIS_MEDIA_STATE	media_state)
{	
	pAd->IndicateMediaState = media_state;

#ifdef SYSTEM_LOG_SUPPORT
		if (pAd->IndicateMediaState == NdisMediaStateConnected)
		{
			RTMPSendWirelessEvent(pAd, IW_STA_LINKUP_EVENT_FLAG, pAd->MacTab.Content[BSSID_WCID].Addr, BSS0, 0);
		}
		else
		{							
			RTMPSendWirelessEvent(pAd, IW_STA_LINKDOWN_EVENT_FLAG, pAd->MacTab.Content[BSSID_WCID].Addr, BSS0, 0); 		
		}	
#endif /* SYSTEM_LOG_SUPPORT */
}


#ifdef WORKQUEUE_BH
void tbtt_workq(struct work_struct *work)
#else
void tbtt_tasklet(unsigned long data)
#endif /* WORKQUEUE_BH */
{
/*#define MAX_TX_IN_TBTT		(16) */

}


void announce_802_3_packet(
	IN	VOID			*pAdSrc,
	IN	PNDIS_PACKET	pPacket,
	IN	UCHAR			OpMode)
{
	PRTMP_ADAPTER	pAd = (PRTMP_ADAPTER)pAdSrc;
/*	struct sk_buff	*pRxPkt; */
	PNDIS_PACKET pRxPkt;
#ifdef INF_PPA_SUPPORT
        int             ret = 0;
        unsigned int ppa_flags = 0; /* reserved for now */
#endif /* INF_PPA_SUPPORT */

	pAd = pAd; /* avoid compile warning */

	MEM_DBG_PKT_FREE_INC(pPacket);


	ASSERT(pPacket);

/*	pRxPkt = RTPKT_TO_OSPKT(pPacket); */
	pRxPkt = pPacket;

#ifdef CONFIG_STA_SUPPORT
#endif /* CONFIG_STA_SUPPORT */

    /* Push up the protocol stack */

#ifdef IKANOS_VX_1X0
	IKANOS_DataFrameRx(pAd, pRxPkt);
#else

/* mark for bridge fast path, 2009/06/22 */
/*	pRxPkt->protocol = eth_type_trans(pRxPkt, pRxPkt->dev); */

#ifdef INF_PPA_SUPPORT
	if (ppa_hook_directpath_send_fn && pAd->PPAEnable==TRUE ) 
	{
		RtmpOsPktInfPpaSend(pRxPkt);

		pRxPkt=NULL;
		return;

	}	  	
#endif /* INF_PPA_SUPPORT */

/*#ifdef CONFIG_5VT_ENHANCE */
/*	*(int*)(pRxPkt->cb) = BRIDGE_TAG; */
/*#endif */

	{

/*		pRxPkt->protocol = eth_type_trans(pRxPkt, pRxPkt->dev); */
		RtmpOsPktProtocolAssign(pRxPkt);
		RtmpOsPktRcvHandle(pRxPkt);
	}

#endif /* IKANOS_VX_1X0 */
}


#ifdef CONFIG_STA_SUPPORT
void STA_MonPktSend(
	IN	PRTMP_ADAPTER	pAd, 
	IN	RX_BLK			*pRxBlk)
{
	PNET_DEV pNetDev;
	PNDIS_PACKET pRxPacket;
	PHEADER_802_11 pHeader;
	USHORT DataSize;
	UINT32 MaxRssi;
	UCHAR L2PAD, PHYMODE, BW, ShortGI, MCS, AMPDU, STBC, RSSI1;
	UCHAR BssMonitorFlag11n, Channel, CentralChannel;
	UCHAR *pData, *pDevName;


	/* sanity check */
    ASSERT(pRxBlk->pRxPacket);
    if (pRxBlk->DataSize < 10)
    {
        DBGPRINT(RT_DEBUG_ERROR, ("%s : Size is too small! (%d)\n", __FUNCTION__, pRxBlk->DataSize));
		goto err_free_sk_buff;
    }

    if (pRxBlk->DataSize + sizeof(wlan_ng_prism2_header) > RX_BUFFER_AGGRESIZE)
    {
        DBGPRINT(RT_DEBUG_ERROR, ("%s : Size is too large! (%d)\n", __FUNCTION__, pRxBlk->DataSize + sizeof(wlan_ng_prism2_header)));
		goto err_free_sk_buff;
    }

	/* init */
	MaxRssi = RTMPMaxRssi(pAd,
						ConvertToRssi(pAd, pRxBlk->pRxWI->RSSI0, RSSI_0),
						ConvertToRssi(pAd, pRxBlk->pRxWI->RSSI1, RSSI_1),
						ConvertToRssi(pAd, pRxBlk->pRxWI->RSSI2, RSSI_2));

	pNetDev = get_netdev_from_bssid(pAd, BSS0); 
	pRxPacket = pRxBlk->pRxPacket;
	pHeader = pRxBlk->pHeader;
	pData = pRxBlk->pData;
	DataSize = pRxBlk->DataSize;
	L2PAD = pRxBlk->RxD.L2PAD;
	PHYMODE = pRxBlk->pRxWI->PHYMODE;
	BW = pRxBlk->pRxWI->BW;
	ShortGI = pRxBlk->pRxWI->ShortGI;
	MCS = pRxBlk->pRxWI->MCS;
	AMPDU = pRxBlk->RxD.AMPDU;
	STBC = pRxBlk->pRxWI->STBC;
	RSSI1 = pRxBlk->pRxWI->RSSI1;
	BssMonitorFlag11n = 0;
#ifdef MONITOR_FLAG_11N_SNIFFER_SUPPORT
	BssMonitorFlag11n = (pAd->StaCfg.BssMonitorFlag & MONITOR_FLAG_11N_SNIFFER);
#endif /* MONITOR_FLAG_11N_SNIFFER_SUPPORT */
	pDevName = (UCHAR *)RtmpOsGetNetDevName(pAd->net_dev);
	Channel = pAd->CommonCfg.Channel;
	CentralChannel = pAd->CommonCfg.CentralChannel;

	/* pass the packet */
	send_monitor_packets(pNetDev, pRxPacket, pHeader, pData, DataSize,
						L2PAD, PHYMODE, BW, ShortGI, MCS, AMPDU, STBC, RSSI1,
						BssMonitorFlag11n, pDevName, Channel, CentralChannel,
						MaxRssi);
	return;

err_free_sk_buff:
	RELEASE_NDIS_PACKET(pAd, pRxBlk->pRxPacket, NDIS_STATUS_FAILURE);	
	return;
}
#endif /* CONFIG_STA_SUPPORT */


extern NDIS_SPIN_LOCK TimerSemLock;

VOID	RTMPFreeAdapter(
	IN	VOID		*pAdSrc)
{
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)pAdSrc;
	POS_COOKIE os_cookie;
	int index;	

	os_cookie=(POS_COOKIE)pAd->OS_Cookie;

	if (pAd->BeaconBuf)
		os_free_mem(NULL, pAd->BeaconBuf);


	NdisFreeSpinLock(&pAd->MgmtRingLock);
	

	for (index =0 ; index < NUM_OF_TX_RING; index++)
	{
		NdisFreeSpinLock(&pAd->TxSwQueueLock[index]);
		NdisFreeSpinLock(&pAd->DeQueueLock[index]);
		pAd->DeQueueRunning[index] = FALSE;
	}
	
	NdisFreeSpinLock(&pAd->irq_lock);


#ifdef DOT11_N_SUPPORT
	NdisFreeSpinLock(&pAd->mpdu_blk_pool.lock);
#endif /* DOT11_N_SUPPORT */

	if (pAd->iw_stats)
	{
		os_free_mem(NULL, pAd->iw_stats);
		pAd->iw_stats = NULL;
	}
	if (pAd->stats)
	{
		os_free_mem(NULL, pAd->stats);
		pAd->stats = NULL;
	}

	NdisFreeSpinLock(&TimerSemLock);
	RTMP_OS_FREE_TIMER(pAd);
	RTMP_OS_FREE_LOCK(pAd);
	RTMP_OS_FREE_TASKLET(pAd);
	RTMP_OS_FREE_TASK(pAd);
	RTMP_OS_FREE_SEM(pAd);
	RTMP_OS_FREE_ATOMIC(pAd);

	RtmpOsVfree(pAd); /* pci_free_consistent(os_cookie->pci_dev,sizeof(RTMP_ADAPTER),pAd,os_cookie->pAd_pa); */
	if (os_cookie)
		os_free_mem(NULL, os_cookie);

#ifdef VENDOR_FEATURE4_SUPPORT
{
	extern ULONG OS_NumOfMemAlloc, OS_NumOfMemFree;
	printk("OS_NumOfMemAlloc = %ld, OS_NumOfMemFree = %ld\n",
			OS_NumOfMemAlloc, OS_NumOfMemFree);
}
#endif /* VENDOR_FEATURE4_SUPPORT */
}


int	RTMPSendPackets(
	IN	NDIS_HANDLE		MiniportAdapterContext,
	IN	PPNDIS_PACKET	ppPacketArray,
	IN	UINT			NumberOfPackets,
	IN	UINT32			PktTotalLen,
	IN	RTMP_NET_ETH_CONVERT_DEV_SEARCH	Func)
{
	PRTMP_ADAPTER pAd = (PRTMP_ADAPTER)MiniportAdapterContext;
	PNDIS_PACKET pPacket = ppPacketArray[0];


	if (pPacket == NULL)
		goto done;

	/* RT2870STA does this in RTMPSendPackets() */
#ifdef RALINK_ATE
	if (ATE_ON(pAd))
	{
		RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_RESOURCES);
		return 0;
	}
#endif /* RALINK_ATE */

#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		/* Drop send request since we are in monitor mode */
		if (MONITOR_ON(pAd))
		{
			RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_FAILURE);
			return 0;
		}
	}
#endif /* CONFIG_STA_SUPPORT */

        /* EapolStart size is 18 */
	if (PktTotalLen < 14)
	{
		/*printk("bad packet size: %d\n", pkt->len); */
		hex_dump("bad packet", GET_OS_PKT_DATAPTR(pPacket), PktTotalLen);
		RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_FAILURE);
		return 0;
	}



	RTMP_SET_PACKET_5VT(pPacket, 0);
/*	MiniportMMRequest(pAd, pkt->data, pkt->len); */
#ifdef CONFIG_5VT_ENHANCE
    if (*(int*)(GET_OS_PKT_CB(pPacket)) == BRIDGE_TAG) {
		RTMP_SET_PACKET_5VT(pPacket, 1);
    }
#endif


#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{

		STASendPackets((NDIS_HANDLE)pAd, (PPNDIS_PACKET) &pPacket, 1);
	}

#endif /* CONFIG_STA_SUPPORT */

done:
	return 0;
}


PNET_DEV get_netdev_from_bssid(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			FromWhichBSSID)
{
	PNET_DEV dev_p = NULL;

	do
	{
#ifdef CONFIG_STA_SUPPORT
#endif /* CONFIG_STA_SUPPORT */
		{
			dev_p = pAd->net_dev;
		}

	} while (FALSE);

	ASSERT(dev_p);
	return dev_p; /* return one of MBSS */
}






