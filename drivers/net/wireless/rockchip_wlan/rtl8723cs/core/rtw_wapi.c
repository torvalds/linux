/* SPDX-License-Identifier: GPL-2.0 */
#ifdef CONFIG_WAPI_SUPPORT

#include <linux/unistd.h>
#include <linux/etherdevice.h>
#include <drv_types.h>
#include <rtw_wapi.h>


u32 wapi_debug_component =
	/*				WAPI_INIT	|
	 *				WAPI_API	|
	 *				WAPI_TX	|
	 *				WAPI_RX	| */
	WAPI_ERR ; /* always open err flags on */

void WapiFreeAllStaInfo(_adapter *padapter)
{
	PRT_WAPI_T				pWapiInfo;
	PRT_WAPI_STA_INFO		pWapiStaInfo;
	PRT_WAPI_BKID			pWapiBkid;

	WAPI_TRACE(WAPI_INIT, "===========> %s\n", __FUNCTION__);
	pWapiInfo = &padapter->wapiInfo;

	/* Pust to Idle List */
	rtw_wapi_return_all_sta_info(padapter);

	/* Sta Info List */
	while (!list_empty(&(pWapiInfo->wapiSTAIdleList))) {
		pWapiStaInfo = (PRT_WAPI_STA_INFO)list_entry(pWapiInfo->wapiSTAIdleList.next, RT_WAPI_STA_INFO, list);
		list_del_init(&pWapiStaInfo->list);
	}

	/* BKID List */
	while (!list_empty(&(pWapiInfo->wapiBKIDIdleList))) {
		pWapiBkid = (PRT_WAPI_BKID)list_entry(pWapiInfo->wapiBKIDIdleList.next, RT_WAPI_BKID, list);
		list_del_init(&pWapiBkid->list);
	}
	WAPI_TRACE(WAPI_INIT, "<=========== %s\n", __FUNCTION__);
	return;
}

void WapiSetIE(_adapter *padapter)
{
	PRT_WAPI_T		pWapiInfo = &(padapter->wapiInfo);
	/* PRT_WAPI_BKID	pWapiBkid; */
	u16		protocolVer = 1;
	u16		akmCnt = 1;
	u16		suiteCnt = 1;
	u16		capability = 0;
	u8		OUI[3];

	OUI[0] = 0x00;
	OUI[1] = 0x14;
	OUI[2] = 0x72;

	pWapiInfo->wapiIELength = 0;
	/* protocol version */
	memcpy(pWapiInfo->wapiIE + pWapiInfo->wapiIELength, &protocolVer, 2);
	pWapiInfo->wapiIELength += 2;
	/* akm */
	memcpy(pWapiInfo->wapiIE + pWapiInfo->wapiIELength, &akmCnt, 2);
	pWapiInfo->wapiIELength += 2;

	if (pWapiInfo->bWapiPSK) {
		memcpy(pWapiInfo->wapiIE + pWapiInfo->wapiIELength, OUI, 3);
		pWapiInfo->wapiIELength += 3;
		pWapiInfo->wapiIE[pWapiInfo->wapiIELength] = 0x2;
		pWapiInfo->wapiIELength += 1;
	} else {
		memcpy(pWapiInfo->wapiIE + pWapiInfo->wapiIELength, OUI, 3);
		pWapiInfo->wapiIELength += 3;
		pWapiInfo->wapiIE[pWapiInfo->wapiIELength] = 0x1;
		pWapiInfo->wapiIELength += 1;
	}

	/* usk */
	memcpy(pWapiInfo->wapiIE + pWapiInfo->wapiIELength, &suiteCnt, 2);
	pWapiInfo->wapiIELength += 2;
	memcpy(pWapiInfo->wapiIE + pWapiInfo->wapiIELength, OUI, 3);
	pWapiInfo->wapiIELength += 3;
	pWapiInfo->wapiIE[pWapiInfo->wapiIELength] = 0x1;
	pWapiInfo->wapiIELength += 1;

	/* msk */
	memcpy(pWapiInfo->wapiIE + pWapiInfo->wapiIELength, OUI, 3);
	pWapiInfo->wapiIELength += 3;
	pWapiInfo->wapiIE[pWapiInfo->wapiIELength] = 0x1;
	pWapiInfo->wapiIELength += 1;

	/* Capbility */
	memcpy(pWapiInfo->wapiIE + pWapiInfo->wapiIELength, &capability, 2);
	pWapiInfo->wapiIELength += 2;
}


/*  PN1 > PN2, return 1,
 *  else return 0.
 */
u32 WapiComparePN(u8 *PN1, u8 *PN2)
{
	char i;

	if ((NULL == PN1) || (NULL == PN2))
		return 1;

	/* overflow case */
	if ((PN2[15] - PN1[15]) & 0x80)
		return 1;

	for (i = 16; i > 0; i--) {
		if (PN1[i - 1] == PN2[i - 1])
			continue;
		else if (PN1[i - 1] > PN2[i - 1])
			return 1;
		else
			return 0;
	}

	return 0;
}

u8
WapiGetEntryForCamWrite(_adapter *padapter, u8 *pMacAddr, u8 KID, BOOLEAN IsMsk)
{
	PRT_WAPI_T		pWapiInfo = NULL;
	/* PRT_WAPI_CAM_ENTRY	pEntry=NULL; */
	u8 i = 0;
	u8 ret = 0xff;

	WAPI_TRACE(WAPI_API, "===========> %s\n", __FUNCTION__);

	pWapiInfo =  &padapter->wapiInfo;

	/* exist? */
	for (i = 0; i < WAPI_CAM_ENTRY_NUM; i++) {
		if (pWapiInfo->wapiCamEntry[i].IsUsed
		    && (_rtw_memcmp(pMacAddr, pWapiInfo->wapiCamEntry[i].PeerMacAddr, ETH_ALEN) == _TRUE)
		    && pWapiInfo->wapiCamEntry[i].keyidx == KID
		    && pWapiInfo->wapiCamEntry[i].type == IsMsk) {
			ret = pWapiInfo->wapiCamEntry[i].entry_idx; /* cover it */
			break;
		}
	}

	if (i == WAPI_CAM_ENTRY_NUM) { /* not found */
		for (i = 0; i < WAPI_CAM_ENTRY_NUM; i++) {
			if (pWapiInfo->wapiCamEntry[i].IsUsed == 0) {
				pWapiInfo->wapiCamEntry[i].IsUsed = 1;
				pWapiInfo->wapiCamEntry[i].type = IsMsk;
				pWapiInfo->wapiCamEntry[i].keyidx = KID;
				_rtw_memcpy(pWapiInfo->wapiCamEntry[i].PeerMacAddr, pMacAddr, ETH_ALEN);
				ret = pWapiInfo->wapiCamEntry[i].entry_idx;
				break;
			}
		}
	}

	WAPI_TRACE(WAPI_API, "<========== %s\n", __FUNCTION__);
	return ret;

	/*
		if(RTIsListEmpty(&pWapiInfo->wapiCamIdleList)) {
			return 0;
		}

		pEntry = (PRT_WAPI_CAM_ENTRY)RTRemoveHeadList(&pWapiInfo->wapiCamIdleList);
		RTInsertTailList(&pWapiInfo->wapiCamUsedList, &pEntry->list);


		return pEntry->entry_idx;*/
}

u8 WapiGetEntryForCamClear(_adapter *padapter, u8 *pPeerMac, u8 keyid, u8 IsMsk)
{
	PRT_WAPI_T		pWapiInfo = NULL;
	u8		i = 0;

	WAPI_TRACE(WAPI_API, "===========> %s\n", __FUNCTION__);

	pWapiInfo =  &padapter->wapiInfo;

	for (i = 0; i < WAPI_CAM_ENTRY_NUM; i++) {
		if (pWapiInfo->wapiCamEntry[i].IsUsed
		    && (_rtw_memcmp(pPeerMac, pWapiInfo->wapiCamEntry[i].PeerMacAddr, ETH_ALEN) == _TRUE)
		    && pWapiInfo->wapiCamEntry[i].keyidx == keyid
		    && pWapiInfo->wapiCamEntry[i].type == IsMsk) {
			pWapiInfo->wapiCamEntry[i].IsUsed = 0;
			pWapiInfo->wapiCamEntry[i].keyidx = 2;
			_rtw_memset(pWapiInfo->wapiCamEntry[i].PeerMacAddr, 0, ETH_ALEN);

			WAPI_TRACE(WAPI_API, "<========== %s\n", __FUNCTION__);
			return pWapiInfo->wapiCamEntry[i].entry_idx;
		}
	}

	WAPI_TRACE(WAPI_API, "<====WapiGetReturnCamEntry(), No this cam entry.\n");
	return 0xff;
	/*
		if(RTIsListEmpty(&pWapiInfo->wapiCamUsedList)) {
			return FALSE;
		}

		pList = &pWapiInfo->wapiCamUsedList;
		while(pList->Flink != &pWapiInfo->wapiCamUsedList)
		{
			pEntry = (PRT_WAPI_CAM_ENTRY)pList->Flink;
			if(PlatformCompareMemory(pPeerMac,pEntry->PeerMacAddr, ETHER_ADDRLEN)== 0
				&& keyid == pEntry->keyidx)
			{
				RTRemoveEntryList(pList);
				RTInsertHeadList(&pWapiInfo->wapiCamIdleList, pList);
				return pEntry->entry_idx;
			}
			pList = pList->Flink;
		}

		return 0;
	*/
}

void
WapiResetAllCamEntry(_adapter *padapter)
{
	PRT_WAPI_T		pWapiInfo;
	int				i;

	WAPI_TRACE(WAPI_API, "===========> %s\n", __FUNCTION__);

	pWapiInfo =  &padapter->wapiInfo;

	for (i = 0; i < WAPI_CAM_ENTRY_NUM; i++) {
		_rtw_memset(pWapiInfo->wapiCamEntry[i].PeerMacAddr, 0, ETH_ALEN);
		pWapiInfo->wapiCamEntry[i].IsUsed = 0;
		pWapiInfo->wapiCamEntry[i].keyidx = 2; /* invalid */
		pWapiInfo->wapiCamEntry[i].entry_idx = 4 + i * 2;
	}

	WAPI_TRACE(WAPI_API, "<========== %s\n", __FUNCTION__);

	return;
}

u8 WapiWriteOneCamEntry(
	_adapter	*padapter,
	u8			*pMacAddr,
	u8			KeyId,
	u8			EntryId,
	u8			EncAlg,
	u8			bGroupKey,
	u8			*pKey
)
{
	u8 retVal = 0;
	u16 usConfig = 0;

	WAPI_TRACE(WAPI_API, "===========> %s\n", __FUNCTION__);

	if (EntryId >= 32) {
		WAPI_TRACE(WAPI_ERR, "<=== CamAddOneEntry(): ulKeyId exceed!\n");
		return retVal;
	}

	usConfig = usConfig | (0x01 << 15) | ((u16)(EncAlg) << 2) | (KeyId);

	if (EncAlg == _SMS4_) {
		if (bGroupKey == 1)
			usConfig |= (0x01 << 6);
		if ((EntryId % 2) == 1) /* ==0 sec key; == 1mic key */
			usConfig |= (0x01 << 5);
	}

	write_cam(padapter, EntryId, usConfig, pMacAddr, pKey);

	WAPI_TRACE(WAPI_API, "===========> %s\n", __FUNCTION__);
	return 1;
}

void rtw_wapi_init(_adapter *padapter)
{
	PRT_WAPI_T		pWapiInfo;
	int				i;

	WAPI_TRACE(WAPI_INIT, "===========> %s\n", __FUNCTION__);
	RT_ASSERT_RET(padapter);

	if (!padapter->WapiSupport) {
		WAPI_TRACE(WAPI_INIT, "<========== %s, WAPI not supported!\n", __FUNCTION__);
		return;
	}

	pWapiInfo =  &padapter->wapiInfo;
	pWapiInfo->bWapiEnable = false;

	/* Init BKID List */
	INIT_LIST_HEAD(&pWapiInfo->wapiBKIDIdleList);
	INIT_LIST_HEAD(&pWapiInfo->wapiBKIDStoreList);
	for (i = 0; i < WAPI_MAX_BKID_NUM; i++)
		list_add_tail(&pWapiInfo->wapiBKID[i].list, &pWapiInfo->wapiBKIDIdleList);

	/* Init STA List */
	INIT_LIST_HEAD(&pWapiInfo->wapiSTAIdleList);
	INIT_LIST_HEAD(&pWapiInfo->wapiSTAUsedList);
	for (i = 0; i < WAPI_MAX_STAINFO_NUM; i++)
		list_add_tail(&pWapiInfo->wapiSta[i].list, &pWapiInfo->wapiSTAIdleList);

	for (i = 0; i < WAPI_CAM_ENTRY_NUM; i++) {
		pWapiInfo->wapiCamEntry[i].IsUsed = 0;
		pWapiInfo->wapiCamEntry[i].keyidx = 2; /* invalid */
		pWapiInfo->wapiCamEntry[i].entry_idx = 4 + i * 2;
	}

	WAPI_TRACE(WAPI_INIT, "<========== %s\n", __FUNCTION__);
}

void rtw_wapi_free(_adapter *padapter)
{
	WAPI_TRACE(WAPI_INIT, "===========> %s\n", __FUNCTION__);
	RT_ASSERT_RET(padapter);

	if (!padapter->WapiSupport) {
		WAPI_TRACE(WAPI_INIT, "<========== %s, WAPI not supported!\n", __FUNCTION__);
		return;
	}

	WapiFreeAllStaInfo(padapter);

	WAPI_TRACE(WAPI_INIT, "<========== %s\n", __FUNCTION__);
}

void rtw_wapi_disable_tx(_adapter *padapter)
{
	WAPI_TRACE(WAPI_INIT, "===========> %s\n", __FUNCTION__);
	RT_ASSERT_RET(padapter);

	if (!padapter->WapiSupport) {
		WAPI_TRACE(WAPI_INIT, "<========== %s, WAPI not supported!\n", __FUNCTION__);
		return;
	}

	padapter->wapiInfo.wapiTxMsk.bTxEnable = false;
	padapter->wapiInfo.wapiTxMsk.bSet = false;

	WAPI_TRACE(WAPI_INIT, "<========== %s\n", __FUNCTION__);
}

u8 rtw_wapi_is_wai_packet(_adapter *padapter, u8 *pkt_data)
{
	PRT_WAPI_T pWapiInfo = &(padapter->wapiInfo);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct security_priv   *psecuritypriv = &padapter->securitypriv;
	PRT_WAPI_STA_INFO pWapiSta = NULL;
	u8 WaiPkt = 0, *pTaddr, bFind = false;
	u8 Offset_TypeWAI = 0 ;	/* (mac header len + llc length) */

	WAPI_TRACE(WAPI_TX | WAPI_RX, "===========> %s\n", __FUNCTION__);

	if ((!padapter->WapiSupport) || (!pWapiInfo->bWapiEnable)) {
		WAPI_TRACE(WAPI_MLME, "<========== %s, WAPI not supported or not enabled!\n", __FUNCTION__);
		return 0;
	}

	Offset_TypeWAI = 24 + 6 ;

	/* YJ,add,091103. Data frame may also have skb->data[30]=0x88 and skb->data[31]=0xb4. */
	if ((pkt_data[1] & 0x40) != 0) {
		/* RTW_INFO("data is privacy\n"); */
		return 0;
	}

	pTaddr = get_addr2_ptr(pkt_data);
	if (list_empty(&pWapiInfo->wapiSTAUsedList))
		bFind = false;
	else {
		list_for_each_entry(pWapiSta, &pWapiInfo->wapiSTAUsedList, list) {
			if (_rtw_memcmp(pTaddr, pWapiSta->PeerMacAddr, 6) == _TRUE) {
				bFind = true;
				break;
			}
		}
	}

	WAPI_TRACE(WAPI_TX | WAPI_RX, "%s: bFind=%d pTaddr="MAC_FMT"\n", __FUNCTION__, bFind, MAC_ARG(pTaddr));

	if (pkt_data[0] == WIFI_QOS_DATA_TYPE)
		Offset_TypeWAI += 2;

	/* 88b4? */
	if ((pkt_data[Offset_TypeWAI] == 0x88) && (pkt_data[Offset_TypeWAI + 1] == 0xb4)) {
		WaiPkt = pkt_data[Offset_TypeWAI + 5];

		psecuritypriv->hw_decrypted = _TRUE;
	} else
		WAPI_TRACE(WAPI_TX | WAPI_RX, "%s(): non wai packet\n", __FUNCTION__);

	WAPI_TRACE(WAPI_TX | WAPI_RX, "%s(): Recvd WAI frame. IsWAIPkt(%d)\n", __FUNCTION__, WaiPkt);

	return	WaiPkt;
}


void rtw_wapi_update_info(_adapter *padapter, union recv_frame *precv_frame)
{
	PRT_WAPI_T     pWapiInfo = &(padapter->wapiInfo);
	struct recv_frame_hdr *precv_hdr;
	u8	*ptr;
	u8	*pTA;
	u8	*pRecvPN;


	WAPI_TRACE(WAPI_RX, "===========> %s\n", __FUNCTION__);

	if ((!padapter->WapiSupport) || (!pWapiInfo->bWapiEnable)) {
		WAPI_TRACE(WAPI_RX, "<========== %s, WAPI not supported or not enabled!\n", __FUNCTION__);
		return;
	}

	precv_hdr = &precv_frame->u.hdr;
	ptr = precv_hdr->rx_data;

	if (precv_hdr->attrib.qos == 1)
		precv_hdr->UserPriority = GetTid(ptr);
	else
		precv_hdr->UserPriority = 0;

	pTA = get_addr2_ptr(ptr);
	_rtw_memcpy((u8 *)precv_hdr->WapiSrcAddr, pTA, 6);
	pRecvPN = ptr + precv_hdr->attrib.hdrlen + 2;
	_rtw_memcpy((u8 *)precv_hdr->WapiTempPN, pRecvPN, 16);

	WAPI_TRACE(WAPI_RX, "<========== %s\n", __FUNCTION__);
}

/****************************************************************************
TRUE-----------------Drop
FALSE---------------- handle
add to support WAPI to N-mode
*****************************************************************************/
u8 rtw_wapi_check_for_drop(
	_adapter *padapter,
	union recv_frame *precv_frame
)
{
	PRT_WAPI_T     pWapiInfo = &(padapter->wapiInfo);
	u8			*pLastRecvPN = NULL;
	u8			bFind = false;
	PRT_WAPI_STA_INFO	pWapiSta = NULL;
	u8			bDrop = false;
	struct recv_frame_hdr *precv_hdr = &precv_frame->u.hdr;
	u8					WapiAEPNInitialValueSrc[16] = {0x37, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C} ;
	u8					WapiAEMultiCastPNInitialValueSrc[16] = {0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C} ;
	u8					*ptr = precv_frame->u.hdr.rx_data;
	int					i;

	WAPI_TRACE(WAPI_RX, "===========> %s\n", __FUNCTION__);

	if ((!padapter->WapiSupport) || (!pWapiInfo->bWapiEnable)) {
		WAPI_TRACE(WAPI_RX, "<========== %s, WAPI not supported or not enabled!\n", __FUNCTION__);
		return false;
	}

	if (precv_hdr->bIsWaiPacket != 0) {
		if (precv_hdr->bIsWaiPacket == 0x8) {

			RTW_INFO("rtw_wapi_check_for_drop: dump packet\n");
			for (i = 0; i < 50; i++) {
				RTW_INFO("%02X  ", ptr[i]);
				if ((i + 1) % 8 == 0)
					RTW_INFO("\n");
			}
			RTW_INFO("\n rtw_wapi_check_for_drop: dump packet\n");

			for (i = 0; i < 16; i++) {
				if (ptr[i + 27] != 0)
					break;
			}

			if (i == 16) {
				WAPI_TRACE(WAPI_RX, "rtw_wapi_check_for_drop: drop with zero BKID\n");
				return true;
			} else
				return false;
		} else
			return false;
	}

	if (list_empty(&pWapiInfo->wapiSTAUsedList))
		bFind = false;
	else {
		list_for_each_entry(pWapiSta, &pWapiInfo->wapiSTAUsedList, list) {
			if (_rtw_memcmp(precv_hdr->WapiSrcAddr, pWapiSta->PeerMacAddr, ETH_ALEN) == _TRUE) {
				bFind = true;
				break;
			}
		}
	}
	WAPI_TRACE(WAPI_RX, "%s: bFind=%d prxb->WapiSrcAddr="MAC_FMT"\n", __FUNCTION__, bFind, MAC_ARG(precv_hdr->WapiSrcAddr));

	if (bFind) {
		if (IS_MCAST(precv_hdr->attrib.ra)) {
			WAPI_TRACE(WAPI_RX, "rtw_wapi_check_for_drop: multicast case\n");
			pLastRecvPN = pWapiSta->lastRxMulticastPN;
		} else {
			WAPI_TRACE(WAPI_RX, "rtw_wapi_check_for_drop: unicast case\n");
			switch (precv_hdr->UserPriority) {
			case 0:
			case 3:
				pLastRecvPN = pWapiSta->lastRxUnicastPNBEQueue;
				break;
			case 1:
			case 2:
				pLastRecvPN = pWapiSta->lastRxUnicastPNBKQueue;
				break;
			case 4:
			case 5:
				pLastRecvPN = pWapiSta->lastRxUnicastPNVIQueue;
				break;
			case 6:
			case 7:
				pLastRecvPN = pWapiSta->lastRxUnicastPNVOQueue;
				break;
			default:
				WAPI_TRACE(WAPI_ERR, "%s: Unknown TID\n", __FUNCTION__);
				break;
			}
		}

		if (!WapiComparePN(precv_hdr->WapiTempPN, pLastRecvPN)) {
			WAPI_TRACE(WAPI_RX, "%s: Equal PN!!\n", __FUNCTION__);
			if (IS_MCAST(precv_hdr->attrib.ra))
				_rtw_memcpy(pLastRecvPN, WapiAEMultiCastPNInitialValueSrc, 16);
			else
				_rtw_memcpy(pLastRecvPN, WapiAEPNInitialValueSrc, 16);
			bDrop = true;
		} else
			_rtw_memcpy(pLastRecvPN, precv_hdr->WapiTempPN, 16);
	}

	WAPI_TRACE(WAPI_RX, "<========== %s\n", __FUNCTION__);
	return bDrop;
}

void rtw_build_probe_resp_wapi_ie(_adapter *padapter, unsigned char *pframe, struct pkt_attrib *pattrib)
{
	PRT_WAPI_T pWapiInfo = &(padapter->wapiInfo);
	u8 WapiIELength = 0;

	WAPI_TRACE(WAPI_MLME, "===========> %s\n", __FUNCTION__);

	if ((!padapter->WapiSupport)  || (!pWapiInfo->bWapiEnable)) {
		WAPI_TRACE(WAPI_MLME, "<========== %s, WAPI not supported!\n", __FUNCTION__);
		return;
	}

	WapiSetIE(padapter);
	WapiIELength = pWapiInfo->wapiIELength;
	pframe[0] = _WAPI_IE_;
	pframe[1] = WapiIELength;
	_rtw_memcpy(pframe + 2, pWapiInfo->wapiIE, WapiIELength);
	pframe += WapiIELength + 2;
	pattrib->pktlen += WapiIELength + 2;

	WAPI_TRACE(WAPI_MLME, "<========== %s\n", __FUNCTION__);
}

void rtw_build_beacon_wapi_ie(_adapter *padapter, unsigned char *pframe, struct pkt_attrib *pattrib)
{
	PRT_WAPI_T pWapiInfo = &(padapter->wapiInfo);
	u8 WapiIELength = 0;
	WAPI_TRACE(WAPI_MLME, "===========> %s\n", __FUNCTION__);

	if ((!padapter->WapiSupport)  || (!pWapiInfo->bWapiEnable)) {
		WAPI_TRACE(WAPI_MLME, "<========== %s, WAPI not supported!\n", __FUNCTION__);
		return;
	}

	WapiSetIE(padapter);
	WapiIELength = pWapiInfo->wapiIELength;
	pframe[0] = _WAPI_IE_;
	pframe[1] = WapiIELength;
	_rtw_memcpy(pframe + 2, pWapiInfo->wapiIE, WapiIELength);
	pframe += WapiIELength + 2;
	pattrib->pktlen += WapiIELength + 2;

	WAPI_TRACE(WAPI_MLME, "<========== %s\n", __FUNCTION__);
}

void rtw_build_assoc_req_wapi_ie(_adapter *padapter, unsigned char *pframe, struct pkt_attrib *pattrib)
{
	PRT_WAPI_BKID		pWapiBKID;
	u16					bkidNum;
	PRT_WAPI_T			pWapiInfo = &(padapter->wapiInfo);
	u8					WapiIELength = 0;

	WAPI_TRACE(WAPI_MLME, "===========> %s\n", __FUNCTION__);

	if ((!padapter->WapiSupport) || (!pWapiInfo->bWapiEnable)) {
		WAPI_TRACE(WAPI_MLME, "<========== %s, WAPI not supported!\n", __FUNCTION__);
		return;
	}

	WapiSetIE(padapter);
	WapiIELength = pWapiInfo->wapiIELength;
	bkidNum = 0;
	if (!list_empty(&(pWapiInfo->wapiBKIDStoreList))) {
		list_for_each_entry(pWapiBKID, &pWapiInfo->wapiBKIDStoreList, list) {
			bkidNum++;
			_rtw_memcpy(pWapiInfo->wapiIE + WapiIELength + 2, pWapiBKID->bkid, 16);
			WapiIELength += 16;
		}
	}
	_rtw_memcpy(pWapiInfo->wapiIE + WapiIELength, &bkidNum, 2);
	WapiIELength += 2;

	pframe[0] = _WAPI_IE_;
	pframe[1] = WapiIELength;
	_rtw_memcpy(pframe + 2, pWapiInfo->wapiIE, WapiIELength);
	pframe += WapiIELength + 2;
	pattrib->pktlen += WapiIELength + 2;
	WAPI_TRACE(WAPI_MLME, "<========== %s\n", __FUNCTION__);
}

void rtw_wapi_on_assoc_ok(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE)
{
	PRT_WAPI_T pWapiInfo = &(padapter->wapiInfo);
	PRT_WAPI_STA_INFO pWapiSta;
	u8 WapiAEPNInitialValueSrc[16] = {0x37, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C} ;
	/* u8 WapiASUEPNInitialValueSrc[16] = {0x36,0x5C,0x36,0x5C,0x36,0x5C,0x36,0x5C,0x36,0x5C,0x36,0x5C,0x36,0x5C,0x36,0x5C} ; */
	u8 WapiAEMultiCastPNInitialValueSrc[16] = {0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C} ;

	WAPI_TRACE(WAPI_MLME, "===========> %s\n", __FUNCTION__);

	if ((!padapter->WapiSupport) || (!pWapiInfo->bWapiEnable)) {
		WAPI_TRACE(WAPI_MLME, "<========== %s, WAPI not supported or not enabled!\n", __FUNCTION__);
		return;
	}

	pWapiSta = (PRT_WAPI_STA_INFO)list_entry(pWapiInfo->wapiSTAIdleList.next, RT_WAPI_STA_INFO, list);
	list_del_init(&pWapiSta->list);
	list_add_tail(&pWapiSta->list, &pWapiInfo->wapiSTAUsedList);
	_rtw_memcpy(pWapiSta->PeerMacAddr, padapter->mlmeextpriv.mlmext_info.network.MacAddress, 6);
	_rtw_memcpy(pWapiSta->lastRxMulticastPN, WapiAEMultiCastPNInitialValueSrc, 16);
	_rtw_memcpy(pWapiSta->lastRxUnicastPN, WapiAEPNInitialValueSrc, 16);

	/* For chenk PN error with Qos Data after s3: add by ylb 20111114 */
	_rtw_memcpy(pWapiSta->lastRxUnicastPNBEQueue, WapiAEPNInitialValueSrc, 16);
	_rtw_memcpy(pWapiSta->lastRxUnicastPNBKQueue, WapiAEPNInitialValueSrc, 16);
	_rtw_memcpy(pWapiSta->lastRxUnicastPNVIQueue, WapiAEPNInitialValueSrc, 16);
	_rtw_memcpy(pWapiSta->lastRxUnicastPNVOQueue, WapiAEPNInitialValueSrc, 16);

	WAPI_TRACE(WAPI_MLME, "<========== %s\n", __FUNCTION__);
}


void rtw_wapi_return_one_sta_info(_adapter *padapter, u8 *MacAddr)
{
	PRT_WAPI_T				pWapiInfo;
	PRT_WAPI_STA_INFO		pWapiStaInfo = NULL;
	PRT_WAPI_BKID			pWapiBkid = NULL;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;

	pWapiInfo = &padapter->wapiInfo;

	WAPI_TRACE(WAPI_API, "==========> %s\n", __FUNCTION__);

	if ((!padapter->WapiSupport) || (!pWapiInfo->bWapiEnable)) {
		WAPI_TRACE(WAPI_MLME, "<========== %s, WAPI not supported or not enabled!\n", __FUNCTION__);
		return;
	}

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		while (!list_empty(&(pWapiInfo->wapiBKIDStoreList))) {
			pWapiBkid = (PRT_WAPI_BKID)list_entry(pWapiInfo->wapiBKIDStoreList.next, RT_WAPI_BKID, list);
			list_del_init(&pWapiBkid->list);
			_rtw_memset(pWapiBkid->bkid, 0, 16);
			list_add_tail(&pWapiBkid->list, &pWapiInfo->wapiBKIDIdleList);
		}
	}


	WAPI_TRACE(WAPI_API, " %s: after clear bkid\n", __FUNCTION__);


	/* Remove STA info */
	if (list_empty(&(pWapiInfo->wapiSTAUsedList))) {
		WAPI_TRACE(WAPI_API, " %s: wapiSTAUsedList is null\n", __FUNCTION__);
		return;
	} else {

		WAPI_TRACE(WAPI_API, " %s: wapiSTAUsedList is not null\n", __FUNCTION__);
#if 0
		pWapiStaInfo = (PRT_WAPI_STA_INFO)list_entry((pWapiInfo->wapiSTAUsedList.next), RT_WAPI_STA_INFO, list);

		list_for_each_entry(pWapiStaInfo, &(pWapiInfo->wapiSTAUsedList), list) {

			RTW_INFO("MAC Addr %02x-%02x-%02x-%02x-%02x-%02x\n", MacAddr[0], MacAddr[1], MacAddr[2], MacAddr[3], MacAddr[4], MacAddr[5]);


			RTW_INFO("peer Addr %02x-%02x-%02x-%02x-%02x-%02x\n", pWapiStaInfo->PeerMacAddr[0], pWapiStaInfo->PeerMacAddr[1], pWapiStaInfo->PeerMacAddr[2], pWapiStaInfo->PeerMacAddr[3],
				pWapiStaInfo->PeerMacAddr[4], pWapiStaInfo->PeerMacAddr[5]);

			if (pWapiStaInfo == NULL) {
				WAPI_TRACE(WAPI_API, " %s: pWapiStaInfo == NULL Case\n", __FUNCTION__);
				return;
			}

			if (pWapiStaInfo->PeerMacAddr == NULL) {
				WAPI_TRACE(WAPI_API, " %s: pWapiStaInfo->PeerMacAddr == NULL Case\n", __FUNCTION__);
				return;
			}

			if (MacAddr == NULL) {
				WAPI_TRACE(WAPI_API, " %s: MacAddr == NULL Case\n", __FUNCTION__);
				return;
			}

			if (_rtw_memcmp(pWapiStaInfo->PeerMacAddr, MacAddr, ETH_ALEN) == _TRUE) {
				pWapiStaInfo->bAuthenticateInProgress = false;
				pWapiStaInfo->bSetkeyOk = false;
				_rtw_memset(pWapiStaInfo->PeerMacAddr, 0, ETH_ALEN);
				list_del_init(&pWapiStaInfo->list);
				list_add_tail(&pWapiStaInfo->list, &pWapiInfo->wapiSTAIdleList);
				break;
			}

		}
#endif

		while (!list_empty(&(pWapiInfo->wapiSTAUsedList))) {
			pWapiStaInfo = (PRT_WAPI_STA_INFO)list_entry(pWapiInfo->wapiSTAUsedList.next, RT_WAPI_STA_INFO, list);

			RTW_INFO("peer Addr %02x-%02x-%02x-%02x-%02x-%02x\n", pWapiStaInfo->PeerMacAddr[0], pWapiStaInfo->PeerMacAddr[1], pWapiStaInfo->PeerMacAddr[2], pWapiStaInfo->PeerMacAddr[3],
				pWapiStaInfo->PeerMacAddr[4], pWapiStaInfo->PeerMacAddr[5]);

			list_del_init(&pWapiStaInfo->list);
			memset(pWapiStaInfo->PeerMacAddr, 0, ETH_ALEN);
			pWapiStaInfo->bSetkeyOk = 0;
			list_add_tail(&pWapiStaInfo->list, &pWapiInfo->wapiSTAIdleList);
		}

	}

	WAPI_TRACE(WAPI_API, "<========== %s\n", __FUNCTION__);
	return;
}

void rtw_wapi_return_all_sta_info(_adapter *padapter)
{
	PRT_WAPI_T				pWapiInfo;
	PRT_WAPI_STA_INFO		pWapiStaInfo;
	PRT_WAPI_BKID			pWapiBkid;
	WAPI_TRACE(WAPI_API, "===========> %s\n", __FUNCTION__);

	pWapiInfo = &padapter->wapiInfo;

	if ((!padapter->WapiSupport) || (!pWapiInfo->bWapiEnable)) {
		WAPI_TRACE(WAPI_MLME, "<========== %s, WAPI not supported or not enabled!\n", __FUNCTION__);
		return;
	}

	/* Sta Info List */
	while (!list_empty(&(pWapiInfo->wapiSTAUsedList))) {
		pWapiStaInfo = (PRT_WAPI_STA_INFO)list_entry(pWapiInfo->wapiSTAUsedList.next, RT_WAPI_STA_INFO, list);
		list_del_init(&pWapiStaInfo->list);
		memset(pWapiStaInfo->PeerMacAddr, 0, ETH_ALEN);
		pWapiStaInfo->bSetkeyOk = 0;
		list_add_tail(&pWapiStaInfo->list, &pWapiInfo->wapiSTAIdleList);
	}

	/* BKID List */
	while (!list_empty(&(pWapiInfo->wapiBKIDStoreList))) {
		pWapiBkid = (PRT_WAPI_BKID)list_entry(pWapiInfo->wapiBKIDStoreList.next, RT_WAPI_BKID, list);
		list_del_init(&pWapiBkid->list);
		memset(pWapiBkid->bkid, 0, 16);
		list_add_tail(&pWapiBkid->list, &pWapiInfo->wapiBKIDIdleList);
	}
	WAPI_TRACE(WAPI_API, "<========== %s\n", __FUNCTION__);
}

void rtw_wapi_clear_cam_entry(_adapter *padapter, u8 *pMacAddr)
{
	u8 UcIndex = 0;

	WAPI_TRACE(WAPI_API, "===========> %s\n", __FUNCTION__);

	if ((!padapter->WapiSupport) || (!padapter->wapiInfo.bWapiEnable)) {
		WAPI_TRACE(WAPI_MLME, "<========== %s, WAPI not supported or not enabled!\n", __FUNCTION__);
		return;
	}

	UcIndex = WapiGetEntryForCamClear(padapter, pMacAddr, 0, 0);
	if (UcIndex != 0xff) {
		/* CAM_mark_invalid(Adapter, UcIndex); */
		CAM_empty_entry(padapter, UcIndex);
	}

	UcIndex = WapiGetEntryForCamClear(padapter, pMacAddr, 1, 0);
	if (UcIndex != 0xff) {
		/* CAM_mark_invalid(Adapter, UcIndex); */
		CAM_empty_entry(padapter, UcIndex);
	}

	UcIndex = WapiGetEntryForCamClear(padapter, pMacAddr, 0, 1);
	if (UcIndex != 0xff) {
		/* CAM_mark_invalid(Adapter, UcIndex); */
		CAM_empty_entry(padapter, UcIndex);
	}

	UcIndex = WapiGetEntryForCamClear(padapter, pMacAddr, 1, 1);
	if (UcIndex != 0xff) {
		/* CAM_mark_invalid(padapter, UcIndex); */
		CAM_empty_entry(padapter, UcIndex);
	}

	WAPI_TRACE(WAPI_API, "<========== %s\n", __FUNCTION__);
}

void rtw_wapi_clear_all_cam_entry(_adapter *padapter)
{
	WAPI_TRACE(WAPI_API, "===========> %s\n", __FUNCTION__);

	if ((!padapter->WapiSupport) || (!padapter->wapiInfo.bWapiEnable)) {
		WAPI_TRACE(WAPI_MLME, "<========== %s, WAPI not supported or not enabled!\n", __FUNCTION__);
		return;
	}

	invalidate_cam_all(padapter); /* is this ok? */
	WapiResetAllCamEntry(padapter);

	WAPI_TRACE(WAPI_API, "===========> %s\n", __FUNCTION__);
}

void rtw_wapi_set_key(_adapter *padapter, RT_WAPI_KEY *pWapiKey, RT_WAPI_STA_INFO *pWapiSta, u8 bGroupKey, u8 bUseDefaultKey)
{
	PRT_WAPI_T		pWapiInfo =  &padapter->wapiInfo;
	u8				*pMacAddr = pWapiSta->PeerMacAddr;
	u32 EntryId = 0;
	BOOLEAN IsPairWise = false ;
	u8 EncAlgo;

	WAPI_TRACE(WAPI_API, "===========> %s\n", __FUNCTION__);

	if ((!padapter->WapiSupport) || (!padapter->wapiInfo.bWapiEnable)) {
		WAPI_TRACE(WAPI_API, "<========== %s, WAPI not supported or not enabled!\n", __FUNCTION__);
		return;
	}

	EncAlgo = _SMS4_;

	/* For Tx bc/mc pkt,use defualt key entry */
	if (bUseDefaultKey) {
		/* when WAPI update key, keyid will be 0 or 1 by turns. */
		if (pWapiKey->keyId == 0)
			EntryId = 0;
		else
			EntryId = 2;
	} else {
		/* tx/rx unicast pkt, or rx broadcast, find the key entry by peer's MacAddr */
		EntryId = WapiGetEntryForCamWrite(padapter, pMacAddr, pWapiKey->keyId, bGroupKey);
	}

	if (EntryId == 0xff) {
		WAPI_TRACE(WAPI_API, "===>No entry for WAPI setkey! !!\n");
		return;
	}

	/* EntryId is also used to diff Sec key and Mic key */
	/* Sec Key */
	WapiWriteOneCamEntry(padapter,
			     pMacAddr,
			     pWapiKey->keyId, /* keyid */
			     EntryId,	/* entry */
			     EncAlgo, /* type */
			     bGroupKey, /* pairwise or group key */
			     pWapiKey->dataKey);
	/* MIC key */
	WapiWriteOneCamEntry(padapter,
			     pMacAddr,
			     pWapiKey->keyId, /* keyid */
			     EntryId + 1,	/* entry */
			     EncAlgo, /* type */
			     bGroupKey, /* pairwise or group key */
			     pWapiKey->micKey);

	WAPI_TRACE(WAPI_API, "Set Wapi Key :KeyId:%d,EntryId:%d,PairwiseKey:%d.\n", pWapiKey->keyId, EntryId, !bGroupKey);
	WAPI_TRACE(WAPI_API, "===========> %s\n", __FUNCTION__);

}

#if 0
/* YJ,test,091013 */
void wapi_test_set_key(struct _adapter *padapter, u8 *buf)
{
	/*Data: keyType(1) + bTxEnable(1) + bAuthenticator(1) + bUpdate(1) + PeerAddr(6) + DataKey(16) + MicKey(16) + KeyId(1)*/
	PRT_WAPI_T			pWapiInfo = &padapter->wapiInfo;
	PRT_WAPI_BKID		pWapiBkid;
	PRT_WAPI_STA_INFO	pWapiSta;
	u8					data[43];
	bool					bTxEnable;
	bool					bUpdate;
	bool					bAuthenticator;
	u8					PeerAddr[6];
	u8					WapiAEPNInitialValueSrc[16] = {0x37, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C} ;
	u8					WapiASUEPNInitialValueSrc[16] = {0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C} ;
	u8					WapiAEMultiCastPNInitialValueSrc[16] = {0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C, 0x36, 0x5C} ;

	WAPI_TRACE(WAPI_INIT, "===========>%s\n", __FUNCTION__);

	if (!padapter->WapiSupport)
		return;

	copy_from_user(data, buf, 43);
	bTxEnable = data[1];
	bAuthenticator = data[2];
	bUpdate = data[3];
	memcpy(PeerAddr, data + 4, 6);

	if (data[0] == 0x3) {
		if (!list_empty(&(pWapiInfo->wapiBKIDIdleList))) {
			pWapiBkid = (PRT_WAPI_BKID)list_entry(pWapiInfo->wapiBKIDIdleList.next, RT_WAPI_BKID, list);
			list_del_init(&pWapiBkid->list);
			memcpy(pWapiBkid->bkid, data + 10, 16);
			WAPI_DATA(WAPI_INIT, "SetKey - BKID", pWapiBkid->bkid, 16);
			list_add_tail(&pWapiBkid->list, &pWapiInfo->wapiBKIDStoreList);
		}
	} else {
		list_for_each_entry(pWapiSta, &pWapiInfo->wapiSTAUsedList, list) {
			if (!memcmp(pWapiSta->PeerMacAddr, PeerAddr, 6)) {
				pWapiSta->bAuthenticatorInUpdata = false;
				switch (data[0]) {
				case 1:              /* usk */
					if (bAuthenticator) {       /* authenticator */
						memcpy(pWapiSta->lastTxUnicastPN, WapiAEPNInitialValueSrc, 16);
						if (!bUpdate) {    /* first */
							WAPI_TRACE(WAPI_INIT, "AE fisrt set usk\n");
							pWapiSta->wapiUsk.bSet = true;
							memcpy(pWapiSta->wapiUsk.dataKey, data + 10, 16);
							memcpy(pWapiSta->wapiUsk.micKey, data + 26, 16);
							pWapiSta->wapiUsk.keyId = *(data + 42);
							pWapiSta->wapiUsk.bTxEnable = true;
							WAPI_DATA(WAPI_INIT, "SetKey - AE USK Data Key", pWapiSta->wapiUsk.dataKey, 16);
							WAPI_DATA(WAPI_INIT, "SetKey - AE USK Mic Key", pWapiSta->wapiUsk.micKey, 16);
						} else {           /* update */
							WAPI_TRACE(WAPI_INIT, "AE update usk\n");
							pWapiSta->wapiUskUpdate.bSet = true;
							pWapiSta->bAuthenticatorInUpdata = true;
							memcpy(pWapiSta->wapiUskUpdate.dataKey, data + 10, 16);
							memcpy(pWapiSta->wapiUskUpdate.micKey, data + 26, 16);
							memcpy(pWapiSta->lastRxUnicastPNBEQueue, WapiASUEPNInitialValueSrc, 16);
							memcpy(pWapiSta->lastRxUnicastPNBKQueue, WapiASUEPNInitialValueSrc, 16);
							memcpy(pWapiSta->lastRxUnicastPNVIQueue, WapiASUEPNInitialValueSrc, 16);
							memcpy(pWapiSta->lastRxUnicastPNVOQueue, WapiASUEPNInitialValueSrc, 16);
							memcpy(pWapiSta->lastRxUnicastPN, WapiASUEPNInitialValueSrc, 16);
							pWapiSta->wapiUskUpdate.keyId = *(data + 42);
							pWapiSta->wapiUskUpdate.bTxEnable = true;
						}
					} else {
						if (!bUpdate) {
							WAPI_TRACE(WAPI_INIT, "ASUE fisrt set usk\n");
							if (bTxEnable) {
								pWapiSta->wapiUsk.bTxEnable = true;
								memcpy(pWapiSta->lastTxUnicastPN, WapiASUEPNInitialValueSrc, 16);
							} else {
								pWapiSta->wapiUsk.bSet = true;
								memcpy(pWapiSta->wapiUsk.dataKey, data + 10, 16);
								memcpy(pWapiSta->wapiUsk.micKey, data + 26, 16);
								pWapiSta->wapiUsk.keyId = *(data + 42);
								pWapiSta->wapiUsk.bTxEnable = false;
							}
						} else {
							WAPI_TRACE(WAPI_INIT, "ASUE update usk\n");
							if (bTxEnable) {
								pWapiSta->wapiUskUpdate.bTxEnable = true;
								if (pWapiSta->wapiUskUpdate.bSet) {
									memcpy(pWapiSta->wapiUsk.dataKey, pWapiSta->wapiUskUpdate.dataKey, 16);
									memcpy(pWapiSta->wapiUsk.micKey, pWapiSta->wapiUskUpdate.micKey, 16);
									pWapiSta->wapiUsk.keyId = pWapiSta->wapiUskUpdate.keyId;
									memcpy(pWapiSta->lastRxUnicastPNBEQueue, WapiASUEPNInitialValueSrc, 16);
									memcpy(pWapiSta->lastRxUnicastPNBKQueue, WapiASUEPNInitialValueSrc, 16);
									memcpy(pWapiSta->lastRxUnicastPNVIQueue, WapiASUEPNInitialValueSrc, 16);
									memcpy(pWapiSta->lastRxUnicastPNVOQueue, WapiASUEPNInitialValueSrc, 16);
									memcpy(pWapiSta->lastRxUnicastPN, WapiASUEPNInitialValueSrc, 16);
									pWapiSta->wapiUskUpdate.bTxEnable = false;
									pWapiSta->wapiUskUpdate.bSet = false;
								}
								memcpy(pWapiSta->lastTxUnicastPN, WapiASUEPNInitialValueSrc, 16);
							} else {
								pWapiSta->wapiUskUpdate.bSet = true;
								memcpy(pWapiSta->wapiUskUpdate.dataKey, data + 10, 16);
								memcpy(pWapiSta->wapiUskUpdate.micKey, data + 26, 16);
								pWapiSta->wapiUskUpdate.keyId = *(data + 42);
								pWapiSta->wapiUskUpdate.bTxEnable = false;
							}
						}
					}
					break;
				case 2:		/* msk */
					if (bAuthenticator) {        /* authenticator */
						pWapiInfo->wapiTxMsk.bSet = true;
						memcpy(pWapiInfo->wapiTxMsk.dataKey, data + 10, 16);
						memcpy(pWapiInfo->wapiTxMsk.micKey, data + 26, 16);
						pWapiInfo->wapiTxMsk.keyId = *(data + 42);
						pWapiInfo->wapiTxMsk.bTxEnable = true;
						memcpy(pWapiInfo->lastTxMulticastPN, WapiAEMultiCastPNInitialValueSrc, 16);

						if (!bUpdate) {    /* first */
							WAPI_TRACE(WAPI_INIT, "AE fisrt set msk\n");
							if (!pWapiSta->bSetkeyOk)
								pWapiSta->bSetkeyOk = true;
							pWapiInfo->bFirstAuthentiateInProgress = false;
						} else                /* update */
							WAPI_TRACE(WAPI_INIT, "AE update msk\n");

						WAPI_DATA(WAPI_INIT, "SetKey - AE MSK Data Key", pWapiInfo->wapiTxMsk.dataKey, 16);
						WAPI_DATA(WAPI_INIT, "SetKey - AE MSK Mic Key", pWapiInfo->wapiTxMsk.micKey, 16);
					} else {
						if (!bUpdate) {
							WAPI_TRACE(WAPI_INIT, "ASUE fisrt set msk\n");
							pWapiSta->wapiMsk.bSet = true;
							memcpy(pWapiSta->wapiMsk.dataKey, data + 10, 16);
							memcpy(pWapiSta->wapiMsk.micKey, data + 26, 16);
							pWapiSta->wapiMsk.keyId = *(data + 42);
							pWapiSta->wapiMsk.bTxEnable = false;
							if (!pWapiSta->bSetkeyOk)
								pWapiSta->bSetkeyOk = true;
							pWapiInfo->bFirstAuthentiateInProgress = false;
							WAPI_DATA(WAPI_INIT, "SetKey - ASUE MSK Data Key", pWapiSta->wapiMsk.dataKey, 16);
							WAPI_DATA(WAPI_INIT, "SetKey - ASUE MSK Mic Key", pWapiSta->wapiMsk.micKey, 16);
						} else {
							WAPI_TRACE(WAPI_INIT, "ASUE update msk\n");
							pWapiSta->wapiMskUpdate.bSet = true;
							memcpy(pWapiSta->wapiMskUpdate.dataKey, data + 10, 16);
							memcpy(pWapiSta->wapiMskUpdate.micKey, data + 26, 16);
							pWapiSta->wapiMskUpdate.keyId = *(data + 42);
							pWapiSta->wapiMskUpdate.bTxEnable = false;
						}
					}
					break;
				default:
					WAPI_TRACE(WAPI_ERR, "Unknown Flag\n");
					break;
				}
			}
		}
	}
	WAPI_TRACE(WAPI_INIT, "<===========%s\n", __FUNCTION__);
}


void wapi_test_init(struct _adapter *padapter)
{
	u8 keybuf[100];
	u8 mac_addr[6] = {0x00, 0xe0, 0x4c, 0x72, 0x04, 0x70};
	u8 UskDataKey[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
	u8 UskMicKey[16] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
	u8 UskId = 0;
	u8 MskDataKey[16] = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f};
	u8 MskMicKey[16] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f};
	u8 MskId = 0;

	WAPI_TRACE(WAPI_INIT, "===========>%s\n", __FUNCTION__);

	/* Enable Wapi */
	WAPI_TRACE(WAPI_INIT, "%s: Enable wapi!!!!\n", __FUNCTION__);
	padapter->wapiInfo.bWapiEnable = true;
	padapter->pairwise_key_type = KEY_TYPE_SMS4;
	ieee->group_key_type = KEY_TYPE_SMS4;
	padapter->wapiInfo.extra_prefix_len = WAPI_EXT_LEN;
	padapter->wapiInfo.extra_postfix_len = SMS4_MIC_LEN;

	/* set usk */
	WAPI_TRACE(WAPI_INIT, "%s: Set USK!!!!\n", __FUNCTION__);
	memset(keybuf, 0, 100);
	keybuf[0] = 1;                           /* set usk */
	keybuf[1] = 1; 				/* enable tx */
	keybuf[2] = 1; 				/* AE */
	keybuf[3] = 0; 				/* not update */

	memcpy(keybuf + 4, mac_addr, 6);
	memcpy(keybuf + 10, UskDataKey, 16);
	memcpy(keybuf + 26, UskMicKey, 16);
	keybuf[42] = UskId;
	wapi_test_set_key(padapter, keybuf);

	memset(keybuf, 0, 100);
	keybuf[0] = 1;                           /* set usk */
	keybuf[1] = 1; 				/* enable tx */
	keybuf[2] = 0; 				/* AE */
	keybuf[3] = 0; 				/* not update */

	memcpy(keybuf + 4, mac_addr, 6);
	memcpy(keybuf + 10, UskDataKey, 16);
	memcpy(keybuf + 26, UskMicKey, 16);
	keybuf[42] = UskId;
	wapi_test_set_key(padapter, keybuf);

	/* set msk */
	WAPI_TRACE(WAPI_INIT, "%s: Set MSK!!!!\n", __FUNCTION__);
	memset(keybuf, 0, 100);
	keybuf[0] = 2;                                /* set msk */
	keybuf[1] = 1;                               /* Enable TX */
	keybuf[2] = 1; 				/* AE */
	keybuf[3] = 0;                              /* not update */
	memcpy(keybuf + 4, mac_addr, 6);
	memcpy(keybuf + 10, MskDataKey, 16);
	memcpy(keybuf + 26, MskMicKey, 16);
	keybuf[42] = MskId;
	wapi_test_set_key(padapter, keybuf);

	memset(keybuf, 0, 100);
	keybuf[0] = 2;                                /* set msk */
	keybuf[1] = 1;                               /* Enable TX */
	keybuf[2] = 0; 				/* AE */
	keybuf[3] = 0;                              /* not update */
	memcpy(keybuf + 4, mac_addr, 6);
	memcpy(keybuf + 10, MskDataKey, 16);
	memcpy(keybuf + 26, MskMicKey, 16);
	keybuf[42] = MskId;
	wapi_test_set_key(padapter, keybuf);
	WAPI_TRACE(WAPI_INIT, "<===========%s\n", __FUNCTION__);
}
#endif

void rtw_wapi_get_iv(_adapter *padapter, u8 *pRA, u8 *IV)
{
	PWLAN_HEADER_WAPI_EXTENSION pWapiExt = NULL;
	PRT_WAPI_T         pWapiInfo = &padapter->wapiInfo;
	bool	bPNOverflow = false;
	bool	bFindMatchPeer = false;
	PRT_WAPI_STA_INFO  pWapiSta = NULL;

	pWapiExt = (PWLAN_HEADER_WAPI_EXTENSION)IV;

	WAPI_DATA(WAPI_RX, "wapi_get_iv: pra", pRA, 6);

	if (IS_MCAST(pRA)) {
		if (!pWapiInfo->wapiTxMsk.bTxEnable) {
			WAPI_TRACE(WAPI_ERR, "%s: bTxEnable = 0!!\n", __FUNCTION__);
			return;
		}

		if (pWapiInfo->wapiTxMsk.keyId <= 1) {
			pWapiExt->KeyIdx = pWapiInfo->wapiTxMsk.keyId;
			pWapiExt->Reserved = 0;
			bPNOverflow = WapiIncreasePN(pWapiInfo->lastTxMulticastPN, 1);
			memcpy(pWapiExt->PN, pWapiInfo->lastTxMulticastPN, 16);
		}
	} else {
		if (list_empty(&pWapiInfo->wapiSTAUsedList)) {
			WAPI_TRACE(WAPI_RX, "rtw_wapi_get_iv: list is empty\n");
			_rtw_memset(IV, 10, 18);
			return;
		} else {
			list_for_each_entry(pWapiSta, &pWapiInfo->wapiSTAUsedList, list) {
				WAPI_DATA(WAPI_RX, "rtw_wapi_get_iv: peermacaddr ", pWapiSta->PeerMacAddr, 6);
				if (_rtw_memcmp((u8 *)pWapiSta->PeerMacAddr, pRA, 6) == _TRUE) {
					bFindMatchPeer = true;
					break;
				}
			}

			WAPI_TRACE(WAPI_RX, "bFindMatchPeer: %d\n", bFindMatchPeer);
			WAPI_DATA(WAPI_RX, "Addr", pRA, 6);

			if (bFindMatchPeer) {
				if ((!pWapiSta->wapiUskUpdate.bTxEnable) && (!pWapiSta->wapiUsk.bTxEnable))
					return;

				if (pWapiSta->wapiUsk.keyId <= 1) {
					if (pWapiSta->wapiUskUpdate.bTxEnable)
						pWapiExt->KeyIdx = pWapiSta->wapiUskUpdate.keyId;
					else
						pWapiExt->KeyIdx = pWapiSta->wapiUsk.keyId;

					pWapiExt->Reserved = 0;
					bPNOverflow = WapiIncreasePN(pWapiSta->lastTxUnicastPN, 2);
					_rtw_memcpy(pWapiExt->PN, pWapiSta->lastTxUnicastPN, 16);

				}
			}
		}

	}

}

bool rtw_wapi_drop_for_key_absent(_adapter *padapter, u8 *pRA)
{
	PRT_WAPI_T         pWapiInfo = &padapter->wapiInfo;
	bool				bFindMatchPeer = false;
	bool				bDrop = false;
	PRT_WAPI_STA_INFO  pWapiSta = NULL;
	struct security_priv		*psecuritypriv = &padapter->securitypriv;

	WAPI_DATA(WAPI_RX, "rtw_wapi_drop_for_key_absent: ra ", pRA, 6);

	if (psecuritypriv->dot11PrivacyAlgrthm == _SMS4_) {
		if ((!padapter->WapiSupport) || (!pWapiInfo->bWapiEnable))
			return true;

		if (IS_MCAST(pRA)) {
			if (!pWapiInfo->wapiTxMsk.bTxEnable) {
				bDrop = true;
				WAPI_TRACE(WAPI_RX, "rtw_wapi_drop_for_key_absent: multicast key is absent\n");
				return bDrop;
			}
		} else {
			if (!list_empty(&pWapiInfo->wapiSTAUsedList)) {
				list_for_each_entry(pWapiSta, &pWapiInfo->wapiSTAUsedList, list) {
					WAPI_DATA(WAPI_RX, "rtw_wapi_drop_for_key_absent: pWapiSta->PeerMacAddr ", pWapiSta->PeerMacAddr, 6);
					if (_rtw_memcmp(pRA, pWapiSta->PeerMacAddr, 6) == _TRUE) {
						bFindMatchPeer = true;
						break;
					}
				}
				if (bFindMatchPeer)	{
					if (!pWapiSta->wapiUsk.bTxEnable) {
						bDrop = true;
						WAPI_TRACE(WAPI_RX, "rtw_wapi_drop_for_key_absent: unicast key is absent\n");
						return bDrop;
					}
				} else {
					bDrop = true;
					WAPI_TRACE(WAPI_RX, "rtw_wapi_drop_for_key_absent: no peer find\n");
					return bDrop;
				}

			} else {
				bDrop = true;
				WAPI_TRACE(WAPI_RX, "rtw_wapi_drop_for_key_absent: no sta  exist\n");
				return bDrop;
			}
		}
	} else
		return bDrop;

	return bDrop;
}

#endif
