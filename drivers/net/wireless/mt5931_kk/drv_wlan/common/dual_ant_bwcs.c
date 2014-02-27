/****************************************************************************
 * Ralink Tech Inc.
 * 4F, No. 2 Technology 5th Rd.
 * Science-based Industrial Park
 * Hsin-chu, Taiwan, R.O.C.
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * All rights reserved. Ralink's source code is an unpublished work and the
 * use of a copyright notice does not imply otherwise. This source code
 * contains confidential trade secret material of Ralink Tech. Any attemp
 * or participation in deciphering, decoding, reverse engineering or in any
 * way altering the source code is stricitly prohibited, unless the prior
 * written consent of Ralink Technology, Inc. is obtained.
 ****************************************************************************

    Module Name:
	rt_bwcs.c
 
    Abstract:
 
    Revision History:
    Who          When          What
    jinchuan    20130827    initialize rt_bwcs.c
    ---------    ----------    ----------------------------------------------
 */
#include "precomp.h"
#include "mtk_porting.h"
#include "dual_ant_bwcs.h"

#ifdef CFG_DUAL_ANTENNA

/*
	========================================================================
	
	Routine Description:
		Send channel event to upper layer 

	Arguments:
		IN pNetDev,		Net Device
		IN eventType,		Channel Event type
		IN flags,			flags
		IN pSrcMac,		Source Mac
		IN pData,			Event Data
		IN dataLen		Event Data Length

	Return Value:
		  0     	OK
		-1		Failure

	IRQL = PASSIVE_LEVEL
	
	Note:
		This function only can be used if the driver supports: 	P2P and STA are 
		coexisting with the same channel
		
		If the driver supports P2P and STA are coexisting with difference channels ,  
		we need another method to management the channel events which will be 
		sent to upper layer

	========================================================================
*/

static UINT32 RtmpOSWirelessEventTranslate(IN UINT32 eventType)
	{
		switch (eventType) {
		case RT_WLAN_EVENT_CUSTOM:
			eventType = IWEVCUSTOM;
			break;
	
		case RT_WLAN_EVENT_CGIWAP:
			eventType = SIOCGIWAP;
			break;
	
#if WIRELESS_EXT > 17
		case RT_WLAN_EVENT_ASSOC_REQ_IE:
			eventType = IWEVASSOCREQIE;
			break;
#endif /* WIRELESS_EXT */
	
#if WIRELESS_EXT >= 14
		case RT_WLAN_EVENT_SCAN:
			eventType = SIOCGIWSCAN;
			break;
#endif /* WIRELESS_EXT */
	
		case RT_WLAN_EVENT_EXPIRED:
			eventType = IWEVEXPIRED;
			break;
#ifdef P2P_SUPPORT
		case RT_WLAN_EVENT_SHOWPIN:
			eventType = 0x8C05; /* IWEVP2PKEYSHOWPIN; */
			break;
		case RT_WLAN_EVENT_PIN:
			eventType = 0x8C06; /* IWEVP2PKEYPIN; */
			break;
#endif /* P2P_SUPPORT */
	
		default:
			printk("Unknown event: 0x%x\n", eventType);
			break;
		}
	
		return eventType;
}


int RtmpOSWrielessEventSend(
	IN PNET_DEV pNetDev,
	IN UINT32 eventType,
	IN INT flags,
	IN PUCHAR pSrcMac,
	IN PUCHAR pData,
	IN UINT32 dataLen)
{
	union iwreq_data wrqu;

	/* translate event type */
	eventType = RtmpOSWirelessEventTranslate(eventType);

	memset(&wrqu, 0, sizeof (wrqu));

	if (flags > -1)
		wrqu.data.flags = flags;

	if (pSrcMac)
		memcpy(wrqu.ap_addr.sa_data, pSrcMac, MAC_ADDR_LEN);

	if ((pData != NULL) && (dataLen > 0))
		wrqu.data.length = dataLen;
	else
		wrqu.data.length = 0;

	wireless_send_event(pNetDev, eventType, &wrqu, (char *)pData);
	return 0;
}


int RtmpWirelessChannelNotify(
	IN PNET_DEV pNetDev,
	IN UINT32 eventType,
	IN INT flags,
	IN PUCHAR pSrcMac,
	IN PUCHAR pData,
	IN UINT32 dataLen)
{
	BOOLEAN 	send 		= FALSE;
	USHORT		type_idx 	= WIFI_CONTYPE_MAX;
	USHORT		i 			= 0;
	BWCS_WIFI	*pBwcsEvent = NULL;

	static UCHAR ch_sent[WIFI_CONTYPE_MAX] = {0};

	DBGPRINT(RT_DEBUG_TRACE, ("[RT_BWCS] ==> %s,%d\n", __FUNCTION__,__LINE__));
	
	if (pData == NULL) {
		DBGPRINT(RT_DEBUG_ERROR, ("Error: pData is NULL Pointer in %s\n", 
					__FUNCTION__));
		return -1;
	}

	pBwcsEvent = (BWCS_WIFI *)pData;

	/*Transfer the event*/
	switch(pBwcsEvent->event) {
	case WIFI_EVENT_STA_CONN_NEW:
	case WIFI_EVENT_STA_CONN_DEL:
		type_idx = WIFI_CONTYPE_STA;
		break;
	case WIFI_EVENT_P2P_GO_CONN_NEW:
	case WIFI_EVENT_P2P_GO_CONN_DEL:
	case WIFI_EVENT_P2P_GC_CONN_NEW:
	case WIFI_EVENT_P2P_GC_CONN_DEL:
		type_idx = WIFI_CONTYPE_P2P;
		break;
	case WIFI_EVENT_SOFTAP_CONN_NEW:
	case WIFI_EVENT_SOFTAP_CONN_DEL:
		type_idx = WIFI_CONTYPE_SOFTAP;
		break;
	default:
		type_idx = WIFI_CONTYPE_MAX;
		break;
	}
	
	DBGPRINT(RT_DEBUG_TRACE, ("[RT_BWCS] %s,%d, typeid=%d \n", __FUNCTION__,__LINE__, type_idx));

	/*do nothing*/
	if (type_idx == WIFI_CONTYPE_MAX)
		return 0;

	/*Transfer the event*/
	switch (pBwcsEvent->event) {
	case WIFI_EVENT_STA_CONN_NEW:
	case WIFI_EVENT_P2P_GO_CONN_NEW:
	case WIFI_EVENT_P2P_GC_CONN_NEW:
	case WIFI_EVENT_SOFTAP_CONN_NEW:
		{
			for (i=0; i<WIFI_CONTYPE_MAX; ) {
				if ((ch_sent[i] > 0) &&
					(ch_sent[i] == pBwcsEvent->para[WIFI_EVENT_PARAM_CH]))
					break;
				i++;
			}
			ch_sent[type_idx] = pBwcsEvent->para[WIFI_EVENT_PARAM_CH];

			/*if the channel */
			if (i == WIFI_CONTYPE_MAX) {
				pBwcsEvent->event = WIFI_EVENT_CONN_NEW;
				send = TRUE;
			}
		}
		break;
	case WIFI_EVENT_STA_CONN_DEL:
	case WIFI_EVENT_P2P_GO_CONN_DEL:
	case WIFI_EVENT_P2P_GC_CONN_DEL:
	case WIFI_EVENT_SOFTAP_CONN_DEL:
		{
			ch_sent[type_idx] = 0;
			for (i=0; i<WIFI_CONTYPE_MAX; ) {
				if ((ch_sent[i] > 0) &&
					(ch_sent[i] == pBwcsEvent->para[WIFI_EVENT_PARAM_CH]))
					break;
				i++;
			}
			if (i == WIFI_CONTYPE_MAX) {
				pBwcsEvent->event = WIFI_EVENT_CONN_DEL;
				send = TRUE;
			}
		}
		break;
	default:
		break;
	}
	
	DBGPRINT(RT_DEBUG_TRACE, ("[RT_BWCS] %s,%d, send=%d \n", __FUNCTION__,__LINE__, send));

	if (send) {
		RtmpOSWrielessEventSend(pNetDev, 
								eventType, 
								flags, 
								pSrcMac, 
								pData, 
								dataLen);						
	}
	return 0;
}

void wifi2bwcs_connection_event_ind_handler(P_GLUE_INFO_T prGlueInfo, USHORT event)
{

	BWCS_WIFI BwcsEvent;
	UINT_8  ucNetTypeIndex;
	struct net_device *gPrDev = prGlueInfo->prDevHandler;
	UINT_8	ucChannelNum; 
	BOOLEAN fg40mBwAllowed;
	UINT_8 ucBandWidth;
	ENUM_CHNL_EXT_T eRfSco;

	if(event == WIFI_EVENT_STA_CONN_NEW || event == WIFI_EVENT_STA_CONN_DEL)
		{
		ucNetTypeIndex = NETWORK_TYPE_AIS_INDEX;
		ucChannelNum = prGlueInfo->prAdapter->rWifiVar.arBssInfo[ucNetTypeIndex].ucPrimaryChannel;
		fg40mBwAllowed = prGlueInfo->prAdapter->rWifiVar.arBssInfo[ucNetTypeIndex].fgAssoc40mBwAllowed;
		ucBandWidth = (fg40mBwAllowed) ? WIFI_BW_40:WIFI_BW_20;
		}
	else if(event == WIFI_EVENT_P2P_GC_CONN_NEW || event == WIFI_EVENT_P2P_GC_CONN_DEL)
		{
		ucNetTypeIndex = NETWORK_TYPE_P2P_INDEX;
		ucChannelNum = prGlueInfo->prAdapter->rWifiVar.arBssInfo[ucNetTypeIndex].ucPrimaryChannel;
		fg40mBwAllowed = prGlueInfo->prAdapter->rWifiVar.arBssInfo[ucNetTypeIndex].fgAssoc40mBwAllowed;	
		ucBandWidth = (fg40mBwAllowed) ? WIFI_BW_40:WIFI_BW_20;
		}
	else if(event == WIFI_EVENT_P2P_GO_CONN_NEW || event == WIFI_EVENT_P2P_GO_CONN_DEL)
		{
		ucNetTypeIndex = NETWORK_TYPE_P2P_INDEX;
		ucChannelNum = prGlueInfo->prAdapter->rWifiVar.prP2pSpecificBssInfo->ucPreferredChannel;
		eRfSco = prGlueInfo->prAdapter->rWifiVar.prP2pSpecificBssInfo->eRfSco;
		if(eRfSco == CHNL_EXT_SCN)
			ucBandWidth = WIFI_BW_20;
		else if(eRfSco == CHNL_EXT_SCA || eRfSco == CHNL_EXT_SCB)
			ucBandWidth = WIFI_BW_40;
		}
		
	else if(event == WIFI_EVENT_SOFTAP_CONN_NEW || event == WIFI_EVENT_SOFTAP_CONN_DEL)
		{
		ucNetTypeIndex = NETWORK_TYPE_P2P_INDEX;
		ucChannelNum = prGlueInfo->prAdapter->rWifiVar.prP2pSpecificBssInfo->ucPreferredChannel;
		eRfSco = prGlueInfo->prAdapter->rWifiVar.prP2pSpecificBssInfo->eRfSco;
		if(eRfSco == CHNL_EXT_SCN)
			ucBandWidth = WIFI_BW_20;
		else if(eRfSco == CHNL_EXT_SCA || eRfSco == CHNL_EXT_SCB)
			ucBandWidth = WIFI_BW_40;
		}

	printk("wifi2bwcs_connection_event_ind_handler event,ucChannelNum,ucBandWidth: %d,%d,%d\n", event,ucChannelNum,ucBandWidth);
	memset(&BwcsEvent, 0, sizeof(BWCS_WIFI));

	/*send a bwcs event  to upper layer */
	snprintf(BwcsEvent.name, sizeof(BwcsEvent.name), BWCS_NAME);
	BwcsEvent.event = event;
	BwcsEvent.para[WIFI_EVENT_PARAM_CH] = ucChannelNum;
	BwcsEvent.para[WIFI_EVENT_PARAM_BW] = ucBandWidth;
	RtmpWirelessChannelNotify(gPrDev,
							RT_WLAN_EVENT_CUSTOM, 
							-1, 
							NULL, 
							(PUCHAR)&BwcsEvent, 
							sizeof(BWCS_WIFI));

}


#endif /* CFG_DUAL_ANTENNA */
