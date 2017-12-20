/*
** Id: tdls_com.c#1
*/

/*! \file tdls_com.c
    \brief This file includes IEEE802.11z TDLS main support.
*/

/*
** Log: tdls_com.c
 *
 * 11 13 2013 vend_samp.lin
 * NULL
 * Initial version.
 */

/*******************************************************************************
 *						C O M P I L E R	 F L A G S
 ********************************************************************************
 */

/*******************************************************************************
 *						E X T E R N A L	R E F E R E N C E S
 ********************************************************************************
 */

#include "precomp.h"

#if (CFG_SUPPORT_TDLS == 1)
#include "tdls.h"

 /*******************************************************************************
 *						 C O N S T A N T S
 ********************************************************************************
 */

 /*******************************************************************************
 *						 F U N C T I O N   D E C L A R A T I O N S
 ********************************************************************************
 */

 /*******************************************************************************
 *						 P R I V A T E	 D A T A
 ********************************************************************************
 */

 /*******************************************************************************
 *						 P R I V A T E	F U N C T I O N S
 ********************************************************************************
 */

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to append general IEs.
*
* \param[in] pvAdapter		Pointer to the Adapter structure.
* \param[in] prStaRec		Pointer to the STA_RECORD_T structure.
* \param[in] u2StatusCode	Status code.
* \param[in] pPkt			Pointer to the frame body
*
* \retval append length
*/
/*----------------------------------------------------------------------------*/
UINT_32 TdlsFrameGeneralIeAppend(ADAPTER_T *prAdapter, STA_RECORD_T *prStaRec, UINT_16 u2StatusCode, UINT_8 *pPkt)
{
	GLUE_INFO_T *prGlueInfo;
	BSS_INFO_T *prBssInfo;
	PM_PROFILE_SETUP_INFO_T *prPmProfSetupInfo;
	UINT_32 u4NonHTPhyType;
	UINT_16 u2SupportedRateSet;
	UINT_8 aucAllSupportedRates[RATE_NUM] = { 0 };
	UINT_8 ucAllSupportedRatesLen;
	UINT_8 ucSupRatesLen;
	UINT_8 ucExtSupRatesLen;
	UINT_32 u4PktLen, u4IeLen;
	BOOLEAN fg40mAllowed;

	/* reference to assocBuildReAssocReqFrameCommonIEs() */

	/* init */
	prGlueInfo = (GLUE_INFO_T *) prAdapter->prGlueInfo;
	prBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX]);
	prPmProfSetupInfo = &prBssInfo->rPmProfSetupInfo;
	u4PktLen = 0;

	/* 3. Frame Formation - (5) Supported Rates element */
	/* use all sup rate we can support */
	if (prStaRec != NULL)
		u4NonHTPhyType = prStaRec->ucNonHTBasicPhyType;
	else
		u4NonHTPhyType = PHY_TYPE_ERP_INDEX;	/* default */

	u2SupportedRateSet = rNonHTPhyAttributes[u4NonHTPhyType].u2SupportedRateSet;

	if (prStaRec != NULL) {
		u2SupportedRateSet &= prStaRec->u2OperationalRateSet;

		if (u2SupportedRateSet == 0)
			u2SupportedRateSet = rNonHTPhyAttributes[u4NonHTPhyType].u2SupportedRateSet;
	}

	rateGetDataRatesFromRateSet(u2SupportedRateSet,
				    prBssInfo->u2BSSBasicRateSet, aucAllSupportedRates, &ucAllSupportedRatesLen);

	ucSupRatesLen = ((ucAllSupportedRatesLen > ELEM_MAX_LEN_SUP_RATES) ?
			 ELEM_MAX_LEN_SUP_RATES : ucAllSupportedRatesLen);

	ucExtSupRatesLen = ucAllSupportedRatesLen - ucSupRatesLen;

	if (ucSupRatesLen) {
		SUP_RATES_IE(pPkt)->ucId = ELEM_ID_SUP_RATES;
		SUP_RATES_IE(pPkt)->ucLength = ucSupRatesLen;
		kalMemCopy(SUP_RATES_IE(pPkt)->aucSupportedRates, aucAllSupportedRates, ucSupRatesLen);

		u4IeLen = IE_SIZE(pPkt);
		pPkt += u4IeLen;
		u4PktLen += u4IeLen;
	}

	/* 3. Frame Formation - (7) Extended sup rates element */
	if (ucExtSupRatesLen) {

		EXT_SUP_RATES_IE(pPkt)->ucId = ELEM_ID_EXTENDED_SUP_RATES;
		EXT_SUP_RATES_IE(pPkt)->ucLength = ucExtSupRatesLen;

		kalMemCopy(EXT_SUP_RATES_IE(pPkt)->aucExtSupportedRates,
			   &aucAllSupportedRates[ucSupRatesLen], ucExtSupRatesLen);

		u4IeLen = IE_SIZE(pPkt);
		pPkt += u4IeLen;
		u4PktLen += u4IeLen;
	}

	/* 3. Frame Formation - (8) Supported channels element */
	/*
	   The Supported channels element is included in Request frame and also in Response
	   frame if Status Code 0 (successful).
	 */
	if (u2StatusCode == 0) {
		SUPPORTED_CHANNELS_IE(pPkt)->ucId = ELEM_ID_SUP_CHS;
		SUPPORTED_CHANNELS_IE(pPkt)->ucLength = 2;
		SUPPORTED_CHANNELS_IE(pPkt)->ucChannelNum[0] = 1;
		SUPPORTED_CHANNELS_IE(pPkt)->ucChannelNum[1] = 11;

#if CFG_SUPPORT_DFS
		if (prAdapter->fgEnable5GBand == TRUE) {
			/* 5G support */
			SUPPORTED_CHANNELS_IE(pPkt)->ucLength = 10;
			SUPPORTED_CHANNELS_IE(pPkt)->ucChannelNum[2] = 36;
			SUPPORTED_CHANNELS_IE(pPkt)->ucChannelNum[3] = 4;
			SUPPORTED_CHANNELS_IE(pPkt)->ucChannelNum[4] = 52;
			SUPPORTED_CHANNELS_IE(pPkt)->ucChannelNum[5] = 4;
			SUPPORTED_CHANNELS_IE(pPkt)->ucChannelNum[6] = 149;
			SUPPORTED_CHANNELS_IE(pPkt)->ucChannelNum[7] = 4;
			SUPPORTED_CHANNELS_IE(pPkt)->ucChannelNum[8] = 165;
			SUPPORTED_CHANNELS_IE(pPkt)->ucChannelNum[9] = 4;
		}
#endif /* CFG_SUPPORT_DFS */

		u4IeLen = IE_SIZE(pPkt);
		pPkt += u4IeLen;
		u4PktLen += u4IeLen;
	}

	/* 3. Frame Formation - (14) HT capabilities element */

	/* no need to check AP capability */
/* if ((prAdapter->rWifiVar.ucAvailablePhyTypeSet & PHY_TYPE_SET_802_11N) && */

	/*
	   after we set ucPhyTypeSet to PHY_TYPE_SET_802_11N in TdlsexRxFrameHandle(),
	   supplicant will disable link if exists and we will clear prStaRec.

	   finally, prStaRec->ucPhyTypeSet will also be 0

	   so we have a fix in TdlsexPeerAdd().
	 */
	if (!prStaRec || (prStaRec->ucPhyTypeSet & PHY_TYPE_SET_802_11N)) {
		/* TODO: prAdapter->rWifiVar.rConnSettings.uc5GBandwidthMode */
#if 0				/* always support */
		if (prAdapter->rWifiVar.rConnSettings.uc2G4BandwidthMode == CONFIG_BW_20M)
			fg40mAllowed = FALSE;
		else
#endif
			fg40mAllowed = TRUE;

		u4IeLen = rlmFillHtCapIEByParams(fg40mAllowed,
						 prAdapter->rWifiVar.rConnSettings.fgRxShortGIDisabled,
						 prAdapter->rWifiVar.u8SupportRxSgi20,
						 prAdapter->rWifiVar.u8SupportRxSgi40,
						 prAdapter->rWifiVar.u8SupportRxGf,
						 prAdapter->rWifiVar.u8SupportRxSTBC, prBssInfo->eCurrentOPMode, pPkt);

		pPkt += u4IeLen;
		u4PktLen += u4IeLen;
	}

	/* 3. Frame Formation - (17) WMM Information element */

	/* always support */
/* if (prAdapter->rWifiVar.fgSupportQoS) */

	{
		/* force to support all UAPSD in TDLS link */
		u4IeLen = mqmGenerateWmmInfoIEByParam(TRUE /*prAdapter->rWifiVar.fgSupportUAPSD */ ,
						      0xf /*prPmProfSetupInfo->ucBmpDeliveryAC */ ,
						      0xf /*prPmProfSetupInfo->ucBmpTriggerAC */ ,
						      WMM_MAX_SP_LENGTH_ALL /*prPmProfSetupInfo->ucUapsdSp */ ,
						      pPkt);

		pPkt += u4IeLen;
		u4PktLen += u4IeLen;
	}

	return u4PktLen;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to transmit a TDLS data frame (setup req/rsp/confirm and tear down).
*
* \param[in] pvAdapter		Pointer to the Adapter structure.
* \param[in] prStaRec		Pointer to the STA_RECORD_T structure.
* \param[in] pPeerMac		Pointer to the MAC of the TDLS peer
* \param[in] ucActionCode	TDLS Action
* \param[in] ucDialogToken	Dialog token
* \param[in] u2StatusCode	Status code
* \param[in] pAppendIe		Others IEs (here are security IEs from supplicant)
* \param[in] AppendIeLen	IE length of others IEs
*
* \retval TDLS_STATUS_xx
*/
/*----------------------------------------------------------------------------*/
TDLS_STATUS
TdlsDataFrameSend(ADAPTER_T *prAdapter,
		  STA_RECORD_T *prStaRec,
		  UINT_8 *pPeerMac,
		  UINT_8 ucActionCode,
		  UINT_8 ucDialogToken, UINT_16 u2StatusCode, UINT_8 *pAppendIe, UINT_32 AppendIeLen)
{
#define LR_TDLS_FME_FIELD_FILL(__Len) \
do { \
	pPkt += __Len; \
	u4PktLen += __Len; \
} while (0)

	GLUE_INFO_T *prGlueInfo;
	BSS_INFO_T *prBssInfo;
	PM_PROFILE_SETUP_INFO_T *prPmProfSetupInfo;
	struct sk_buff *prMsduInfo;
	MSDU_INFO_T *prMsduInfoMgmt;
	UINT8 *pPkt, *pucInitiator, *pucResponder;
	UINT32 u4PktLen, u4IeLen;
	UINT16 u2CapInfo;
/* UINT8 *pPktTemp; */

	prGlueInfo = (GLUE_INFO_T *) prAdapter->prGlueInfo;

	DBGLOG(TDLS, INFO, "<tdls_fme> %s: 2040=%d\n", __func__, prGlueInfo->rTdlsLink.fgIs2040Sup);

	/* sanity check */
	if (prStaRec != NULL) {
		if (prStaRec->ucNetTypeIndex >= NETWORK_TYPE_INDEX_NUM) {
			DBGLOG(TDLS, ERROR,
			       "<tdls_cmd> %s: net index %d fail\n", __func__, prStaRec->ucNetTypeIndex);
			return TDLS_STATUS_FAILURE;
		}

		prBssInfo = &(prAdapter->rWifiVar.arBssInfo[prStaRec->ucNetTypeIndex]);
	} else {
		/* prStaRec maybe NULL in setup request */
		prBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX]);
	}

	/* allocate/init packet */
	prPmProfSetupInfo = &prBssInfo->rPmProfSetupInfo;
	u4PktLen = 0;
	prMsduInfo = NULL;
	prMsduInfoMgmt = NULL;

	/* make up frame content */
	if (ucActionCode != TDLS_FRM_ACTION_DISCOVERY_RESPONSE) {
		/*
		   The STAUT will not respond to a TDLS Discovery Request Frame with different BSSID.
		   Supplicant will check this in wpa_tdls_process_discovery_request().
		 */

		/* TODO: reduce 1600 to correct size */
		prMsduInfo = kalPacketAlloc(prGlueInfo, 1600, &pPkt);
		if (prMsduInfo == NULL) {
			DBGLOG(TDLS, ERROR, "<tdls_cmd> %s: allocate pkt fail\n", __func__);
			return TDLS_STATUS_RESOURCES;
		}

		prMsduInfo->dev = prGlueInfo->prDevHandler;
		if (prMsduInfo->dev == NULL) {
			DBGLOG(TDLS, ERROR, "<tdls_cmd> %s: MsduInfo->dev == NULL\n", __func__);
			kalPacketFree(prGlueInfo, prMsduInfo);
			return TDLS_STATUS_FAILURE;
		}

		/* 1. 802.3 header */
/* pPktTemp = pPkt; */
		kalMemCopy(pPkt, pPeerMac, TDLS_FME_MAC_ADDR_LEN);
		LR_TDLS_FME_FIELD_FILL(TDLS_FME_MAC_ADDR_LEN);
		kalMemCopy(pPkt, prBssInfo->aucOwnMacAddr, TDLS_FME_MAC_ADDR_LEN);
		LR_TDLS_FME_FIELD_FILL(TDLS_FME_MAC_ADDR_LEN);
		*(UINT_16 *) pPkt = htons(TDLS_FRM_PROT_TYPE);
		LR_TDLS_FME_FIELD_FILL(2);

		/* 2. payload type */
		*pPkt = TDLS_FRM_PAYLOAD_TYPE;
		LR_TDLS_FME_FIELD_FILL(1);

		/* 3. Frame Formation - (1) Category */
		*pPkt = TDLS_FRM_CATEGORY;
		LR_TDLS_FME_FIELD_FILL(1);
	} else {
		/* discovery response */
		WLAN_MAC_HEADER_T *prHdr;

		prMsduInfoMgmt = (MSDU_INFO_T *)
		    cnmMgtPktAlloc(prAdapter, PUBLIC_ACTION_MAX_LEN);
		if (prMsduInfoMgmt == NULL) {
			DBGLOG(TDLS, ERROR, "<tdls_cmd> %s: allocate mgmt pkt fail\n", __func__);
			return TDLS_STATUS_RESOURCES;
		}

		pPkt = (UINT8 *) prMsduInfoMgmt->prPacket;
		prHdr = (WLAN_MAC_HEADER_T *) pPkt;

		/* 1. 802.11 header */
		prHdr->u2FrameCtrl = MAC_FRAME_ACTION;
		prHdr->u2DurationID = 0;
		kalMemCopy(prHdr->aucAddr1, pPeerMac, TDLS_FME_MAC_ADDR_LEN);
		kalMemCopy(prHdr->aucAddr2, prBssInfo->aucOwnMacAddr, TDLS_FME_MAC_ADDR_LEN);
		kalMemCopy(prHdr->aucAddr3, prBssInfo->aucBSSID, TDLS_FME_MAC_ADDR_LEN);
		prHdr->u2SeqCtrl = 0;
		LR_TDLS_FME_FIELD_FILL(sizeof(WLAN_MAC_HEADER_T));

		/* Frame Formation - (1) Category */
		*pPkt = CATEGORY_PUBLIC_ACTION;
		LR_TDLS_FME_FIELD_FILL(1);
	}

	/* 3. Frame Formation - (2) Action */
	*pPkt = ucActionCode;
	LR_TDLS_FME_FIELD_FILL(1);

	/* 3. Frame Formation - Status Code */
	switch (ucActionCode) {
	case TDLS_FRM_ACTION_SETUP_RSP:
	case TDLS_FRM_ACTION_CONFIRM:
	case TDLS_FRM_ACTION_TEARDOWN:
		WLAN_SET_FIELD_16(pPkt, u2StatusCode);
		LR_TDLS_FME_FIELD_FILL(2);
		break;
	}

	/* 3. Frame Formation - (3) Dialog token */
	if (ucActionCode != TDLS_FRM_ACTION_TEARDOWN) {
		*pPkt = ucDialogToken;
		LR_TDLS_FME_FIELD_FILL(1);
	}

	/* Fill elements */
	if (ucActionCode != TDLS_FRM_ACTION_TEARDOWN) {
		/*
		   Capability

		   Support Rates
		   Extended Support Rates
		   Supported Channels
		   HT Capabilities
		   WMM Information Element

		   Extended Capabilities
		   Link Identifier

		   RSNIE
		   FTIE
		   Timeout Interval
		 */
		if (ucActionCode != TDLS_FRM_ACTION_CONFIRM) {
			/* 3. Frame Formation - (4) Capability: 0x31 0x04, privacy bit will be set */
			u2CapInfo = assocBuildCapabilityInfo(prAdapter, prStaRec);
			WLAN_SET_FIELD_16(pPkt, u2CapInfo);
			LR_TDLS_FME_FIELD_FILL(2);

			/* 4. Append general IEs */
			/*
			   TODO check HT: prAdapter->rWifiVar.rConnSettings.uc2G4BandwidthMode
			   must be CONFIG_BW_20_40M.

			   TODO check HT: HT_CAP_INFO_40M_INTOLERANT must be clear if
			   Tdls 20/40 is enabled.
			 */
			u4IeLen = TdlsFrameGeneralIeAppend(prAdapter, prStaRec, u2StatusCode, pPkt);
			LR_TDLS_FME_FIELD_FILL(u4IeLen);

			/* 5. Frame Formation - Extended capabilities element */
			EXT_CAP_IE(pPkt)->ucId = ELEM_ID_EXTENDED_CAP;
			EXT_CAP_IE(pPkt)->ucLength = 5;

			EXT_CAP_IE(pPkt)->aucCapabilities[0] = 0x00;	/* bit0 ~ bit7 */
			EXT_CAP_IE(pPkt)->aucCapabilities[1] = 0x00;	/* bit8 ~ bit15 */
			EXT_CAP_IE(pPkt)->aucCapabilities[2] = 0x00;	/* bit16 ~ bit23 */
			EXT_CAP_IE(pPkt)->aucCapabilities[3] = 0x00;	/* bit24 ~ bit31 */
			EXT_CAP_IE(pPkt)->aucCapabilities[4] = 0x00;	/* bit32 ~ bit39 */

			/* if (prCmd->ucExCap & TDLS_EX_CAP_PEER_UAPSD) */
			EXT_CAP_IE(pPkt)->aucCapabilities[3] |= BIT((28 - 24));
			/* if (prCmd->ucExCap & TDLS_EX_CAP_CHAN_SWITCH) */
			EXT_CAP_IE(pPkt)->aucCapabilities[3] |= BIT((30 - 24));
			/* if (prCmd->ucExCap & TDLS_EX_CAP_TDLS) */
			EXT_CAP_IE(pPkt)->aucCapabilities[4] |= BIT((37 - 32));

			u4IeLen = IE_SIZE(pPkt);
			LR_TDLS_FME_FIELD_FILL(u4IeLen);
		} else {
			/* 5. Frame Formation - WMM Parameter element */
			if (prAdapter->rWifiVar.fgSupportQoS) {
				u4IeLen = mqmGenerateWmmParamIEByParam(prAdapter,
								   prBssInfo, pPkt, OP_MODE_INFRASTRUCTURE);

				LR_TDLS_FME_FIELD_FILL(u4IeLen);
			}
		}
	}

	/* 6. Frame Formation - 20/40 BSS Coexistence */
	/*
	   Follow WiFi test plan, add 20/40 element to request/response/confirm.
	 */
/* if (prGlueInfo->rTdlsLink.fgIs2040Sup == TRUE) */ /* force to enable */
	if (ucActionCode != TDLS_FRM_ACTION_TEARDOWN) {
		/*
		   bit0 = 1: The Information Request field is used to indicate that a
		   transmitting STA is requesting the recipient to transmit a 20/40 BSS
		   Coexistence Management frame with the transmitting STA as the
		   recipient.

		   bit1 = 0: The Forty MHz Intolerant field is set to 1 to prohibit an AP
		   that receives this information or reports of this information from
		   operating a 20/40 MHz BSS.

		   bit2 = 0: The 20 MHz BSS Width Request field is set to 1 to prohibit
		   a receiving AP from operating its BSS as a 20/40 MHz BSS.
		 */
		BSS_20_40_COEXIST_IE(pPkt)->ucId = ELEM_ID_20_40_BSS_COEXISTENCE;
		BSS_20_40_COEXIST_IE(pPkt)->ucLength = 1;
		BSS_20_40_COEXIST_IE(pPkt)->ucData = 0x01;
		LR_TDLS_FME_FIELD_FILL(3);
	}

	/* 6. Frame Formation - HT Operation element */
/* u4IeLen = rlmFillHtOpIeBody(prBssInfo, pPkt); */
/* LR_TDLS_FME_FIELD_FILL(u4IeLen); */

	/* 7. Frame Formation - Link identifier element */
	/* Note1: Link ID sequence must be correct; Or the calculated MIC will be error */
	/*
	   Note2: When we receive a setup request with link ID, Marvell will send setup response
	   to the peer in link ID, not the SA in the WLAN header.
	 */
	TDLS_LINK_IDENTIFIER_IE(pPkt)->ucId = ELEM_ID_LINK_IDENTIFIER;
	TDLS_LINK_IDENTIFIER_IE(pPkt)->ucLength = ELEM_LEN_LINK_IDENTIFIER;

	kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aBSSID, prBssInfo->aucBSSID, 6);

	switch (ucActionCode) {
	case TDLS_FRM_ACTION_SETUP_REQ:
	case TDLS_FRM_ACTION_CONFIRM:
	default:
		/* we are initiator */
		pucInitiator = prBssInfo->aucOwnMacAddr;
		pucResponder = pPeerMac;

		if (prStaRec != NULL)
			prStaRec->flgTdlsIsInitiator = TRUE;
		break;

	case TDLS_FRM_ACTION_SETUP_RSP:
	case TDLS_FRM_ACTION_DISCOVERY_RESPONSE:
		/* peer is initiator */
		pucInitiator = pPeerMac;
		pucResponder = prBssInfo->aucOwnMacAddr;

		if (prStaRec != NULL)
			prStaRec->flgTdlsIsInitiator = FALSE;
		break;

	case TDLS_FRM_ACTION_TEARDOWN:
		if (prStaRec != NULL) {
			if (prStaRec->flgTdlsIsInitiator == TRUE) {
				/* we are initiator */
				pucInitiator = prBssInfo->aucOwnMacAddr;
				pucResponder = pPeerMac;
			} else {
				/* peer is initiator */
				pucInitiator = pPeerMac;
				pucResponder = prBssInfo->aucOwnMacAddr;
			}
		} else {
			/* peer is initiator */
			pucInitiator = pPeerMac;
			pucResponder = prBssInfo->aucOwnMacAddr;
		}
		break;
	}

	kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aInitiator, pucInitiator, 6);
	kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aResponder, pucResponder, 6);

	u4IeLen = IE_SIZE(pPkt);
	LR_TDLS_FME_FIELD_FILL(u4IeLen);

	/* 8. Append security IEs */
	/*
	   11.21.5 TDLS Direct Link Teardown
	   If the STA has security enabled on the link 37 with the AP, then the FTIE shall be
	   included in the TDLS Teardown frame.

	   For ralink station, it can accept our tear down without FTIE but marvell station.
	 */
/* if ((ucActionCode != TDLS_FRM_ACTION_TEARDOWN) && (pAppendIe != NULL)) */
	if (pAppendIe != NULL) {
		if ((ucActionCode != TDLS_FRM_ACTION_TEARDOWN) ||
		    ((ucActionCode == TDLS_FRM_ACTION_TEARDOWN) &&
		     (prStaRec != NULL) && (prStaRec->fgTdlsInSecurityMode == TRUE))) {
			kalMemCopy(pPkt, pAppendIe, AppendIeLen);
			LR_TDLS_FME_FIELD_FILL(AppendIeLen);
		}
	}

	/* 7. Append Supported Operating Classes IE */
	if (ucActionCode != TDLS_FRM_ACTION_TEARDOWN) {
		/* Note: if we do not put the IE, Marvell STA will decline our TDLS setup request */
		u4IeLen = rlmDomainSupOperatingClassIeFill(pPkt);
		LR_TDLS_FME_FIELD_FILL(u4IeLen);
	}

	/* 11. send the data or management frame */
	if (ucActionCode != TDLS_FRM_ACTION_DISCOVERY_RESPONSE) {
#if 0
		/*
		   Note1: remember to modify our MAC & AP MAC & peer MAC in LINK ID
		   Note2: dialog token in rsp & confirm must be same as sender.
		 */

#if 1
		/* example for Ralink's and Broadcom's TDLS setup request frame in open/none */
		if (ucActionCode == TDLS_FRM_ACTION_SETUP_REQ) {
#if 0
			/* mediatek */
			char buffer[] = { 0x31, 0x04,
				0x01, 0x08, 0x02, 0x04, 0x0b, 0x16, 0xc, 0x12, 0x18, 0x24,
				0x32, 0x04, 0x30, 0x48, 0x60, 0x6c,
				0x24, 0x0a, 0x01, 0x0b, 0x24, 0x04, 0x34, 0x04, 0x95, 0x04, 0xa5, 0x01,
				0x2d, 0x1a, 0x72, 0x11, 0x03, 0xff, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00,
				0xdd, 0x07, 0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x0f,
				0x7f, 0x05, 0x00, 0x00, 0x00, 0x50, 0x20,
				0x48, 0x01, 0x01,
				0x65, 0x12, 0x00, 0x0c, 0x43, 0x31, 0x35, 0x97, 0x00, 0x11, 0x22, 0x33,
				0x44, 0x05, 0x00, 0x22, 0x58, 0x00, 0xcc, 0x0f,
				0x3b, 0x0d, 0x0c, 0x01, 0x02, 0x03, 0x05, 0x16, 0x17, 0x19,
				0x1b, 0x1c, 0x1e, 0x20, 0x21,
				0x07, 0x06, 0x55, 0x53, 0x20, 0x01, 0x0b, 0x1e
			};
#endif

#if 1
			/* ralink *//* from capability */
			char buffer[] = { 0x21, 0x04,
				0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x12, 0x24, 0x48, 0x6c,
				0x07, 0x06, 0x55, 0x53, 0x20, 0xdd, 0x20, 0x00,
				0x32, 0x04, 0x0c, 0x18, 0x30, 0x60,
				0x24, 0x06, 0x01, 0x0b, 0x24, 0x08, 0x95, 0x04,
				0x7f, 0x05, 0x01, 0x00, 0x00, 0x50, 0x20,
				0x3b, 0x10, 0x20, 0x01, 0x02, 0x03, 0x04, 0x0c, 0x16, 0x17, 0x18, 0x19,
				0x1b, 0x1c, 0x1d, 0x1e, 0x20, 0x21,
				0x2d, 0x1a, 0x6e, 0x00, 0x17, 0xff, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00,
				0x00, 0x00, 0x00, 0x00,
				0x48, 0x01, 0x01,
				0x65, 0x12, 0x00, 0x0c, 0x43, 0x44, 0x0b, 0x1a, 0x00, 0x11, 0x22, 0x33,
				0x44, 0x05, 0x00, 0x22, 0x58, 0x00, 0xcc, 0x0f,
				0xdd, 0x07, 0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x0f
			};
#endif
#if 0
			/* 6630 */
			char buffer[] = { 0x01, 0x01,
				0x01, 0x04, 0x02, 0x04, 0x0b, 0x16,
				0x24, 0x02, 0x01, 0x0d,
				0x7f, 0x05, 0x00, 0x00, 0x00, 0x50, 0xff,
				0x2d, 0x1a, 0x61, 0x01, 0x03, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00,
				0x38, 0x05, 0x02, 0xc0, 0xa8, 0x00, 0x00,
				0x48, 0x01, 0x01,
				0x3b, 0x0d, 0x0c, 0x01, 0x02, 0x03, 0x05, 0x16, 0x17, 0x19,
				0x1b, 0x1c, 0x1d, 0x1e, 0x20, 0x21,
				0x07, 0x06, 0x55, 0x53, 0x20, 0x01, 0x0b, 0x1e,
				0x65, 0x12, 0x00, 0x0c, 0x43, 0x44, 0x0b, 0x1a, 0x00, 0x11, 0x22, 0x33,
				0x44, 0x05, 0x00, 0x22, 0x58, 0x00, 0xcc, 0x0f,
				0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01, 0x80, 0x01, 0x00, 0x00,
				0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x60,
				0x00, 0x00, 0x00,
				0xbf, 0x0c, 0x30, 0x01, 0x80, 0x03, 0xfe, 0xff, 0x00, 0x00, 0xfe, 0xff,
				0x00, 0x00
			};
#endif

			pPktTemp += 18;
			memcpy(pPktTemp, buffer, sizeof(buffer));
			u4PktLen = 18 + sizeof(buffer);
		}
#endif

#if 1
		if (ucActionCode == TDLS_FRM_ACTION_CONFIRM) {
			/* Note: dialog token must be same as request */
#if 1
			/* ralink */
			char buffer[] = { 0x00,
				0x01, 0x2d, 0x1a, 0x6e, 0x00, 0x17, 0xff, 0x00, 0x00, 0x00,
				0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x65, 0x12, 0x00, 0x0c, 0x43, 0x44, 0x0b, 0x1a, 0x00, 0x11, 0x22, 0x33,
				0x44, 0x05, 0x00, 0x22, 0x58, 0x00, 0xcc, 0x0f,
				0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01, 0x0f, 0x00, 0x03,
				0xa4, 0x00, 0x00, 0x27, 0xa4, 0x00, 0x00, 0x42, 0x43, 0x5e, 0x00,
				0x62, 0x32, 0x2f, 0x00
			};
#endif

#if 0
			/* 6630 */
			char buffer[] = { 0x00,
				0x01,
				0x38, 0x05, 0x02, 0xc0, 0xa8, 0x00, 0x00,
				0x48, 0x01, 0x01,
				0x3b, 0x0d, 0x0c, 0x01, 0x02, 0x03, 0x05, 0x16, 0x17, 0x19,
				0x1b, 0x1c, 0x1d, 0x1e, 0x20, 0x21,
				0x07, 0x06, 0x55, 0x53, 0x20, 0x01, 0x0b, 0x1e,
				0x65, 0x12, 0x00, 0x0c, 0x43, 0x44, 0x0b, 0x1a, 0x00, 0x11, 0x22, 0x33,
				0x44, 0x05, 0x00, 0x22, 0x58, 0x00, 0xcc, 0x0f,
				0xdd, 0x07, 0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00,
				0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01, 0x80, 0x3f, 0x00, 0x00,
				0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x60,
				0x00, 0x00, 0x00
			};
#endif

#if 0
			/* A/D die */
			char buffer[] = { 0x00,
				0x01,
				0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01, 0x0f, 0x6b, 0x00, 0x00,
				0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x60,
				0x00, 0x00, 0x00 0x65, 0x12, 0x00, 0x0c, 0x43, 0x31, 0x35, 0x97, 0x00, 0x11, 0x22, 0x33,
				0x44, 0x05, 0x00, 0x22, 0x58, 0x00, 0xcc, 0x0f,
				0x38, 0x0d, 0x0c, 0x01, 0x02, 0x03, 0x05, 0x16, 0x17, 0x19, 0x1b,
				0x1c, 0x1e, 0x20, 0x21,
				0x07, 0x06, 0x55, 0x53, 0x20, 0x01, 0x0b, 0x1e
			};
#endif

			pPktTemp += 18;
			memcpy(pPktTemp, buffer, sizeof(buffer));
			u4PktLen = 18 + sizeof(buffer);
		}
#endif

#else

#if 0
		/* for test in open/none */
		if (ucActionCode == TDLS_FRM_ACTION_SETUP_REQ) {
			char buffer[] = { 0x01, 0x04,
				0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x12, 0x24, 0x48, 0x6c,
				0x07, 0x06, 0x55, 0x53, 0x20, 0xdd, 0x20, 0x00,
				0x32, 0x04, 0x30, 0x48, 0x60, 0x6c,
				0x24, 0x0a, 0x01, 0x0b, 0x24, 0x04, 0x34, 0x04, 0x95, 0x04, 0xa5, 0x01,
				0x2d, 0x1a, 0x72, 0x11, 0x03, 0xff, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00,
				0xdd, 0x07, 0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x0f,
				0x7f, 0x05, 0x00, 0x00, 0x00, 0x50, 0x20,
				0x48, 0x01, 0x01,
				0x65, 0x12, 0x00, 0x0c, 0x43, 0x44, 0x0b, 0x1a, 0x00, 0x11, 0x22, 0x33,
				0x44, 0x05, 0x00, 0x22, 0x58, 0x00, 0xcc, 0x0f,
				0x3b, 0x0d, 0x0c, 0x01, 0x02, 0x03, 0x05, 0x16, 0x17, 0x19,
				0x1b, 0x1c, 0x1e, 0x20, 0x21
			};

			pPktTemp += 18;
			memcpy(pPktTemp, buffer, sizeof(buffer));
			u4PktLen = 18 + sizeof(buffer);
		}
#endif
#endif /* 0 */

		/* 9. Update packet length */
		prMsduInfo->len = u4PktLen;
		dumpMemory8(prMsduInfo->data, u4PktLen);

		wlanHardStartXmit(prMsduInfo, prMsduInfo->dev);
	} else {
		/*
		   A TDLS capable STA that receives a TDLS Discovery Request frame is required to
		   send the response "to the requesting STA, via the direct path."
		   However, prior to establishment of the direct link, the responding STA may not
		   know the rate capabilities of the requesting STA. In this case, the responding
		   STA shall send the TDLS Discovery Response frame using a rate from the
		   BSSBasicRateSet of the BSS to which the STA is currently associated.
		 */
		prMsduInfoMgmt->ucPacketType = HIF_TX_PACKET_TYPE_MGMT;
		prMsduInfoMgmt->ucStaRecIndex = prBssInfo->prStaRecOfAP->ucIndex;
		prMsduInfoMgmt->ucNetworkType = prBssInfo->ucNetTypeIndex;
		prMsduInfoMgmt->ucMacHeaderLength = WLAN_MAC_MGMT_HEADER_LEN;
		prMsduInfoMgmt->fgIs802_1x = FALSE;
		prMsduInfoMgmt->fgIs802_11 = TRUE;
		prMsduInfoMgmt->u2FrameLength = u4PktLen;
		prMsduInfoMgmt->ucTxSeqNum = nicIncreaseTxSeqNum(prAdapter);
		prMsduInfoMgmt->pfTxDoneHandler = NULL;
		prMsduInfoMgmt->fgIsBasicRate = TRUE;	/* use basic rate */

		/* Send them to HW queue */
		nicTxEnqueueMsdu(prAdapter, prMsduInfoMgmt);
	}

	return TDLS_STATUS_SUCCESS;
}

#endif /* CFG_SUPPORT_TDLS */

 /* End of tdls_com.c */
