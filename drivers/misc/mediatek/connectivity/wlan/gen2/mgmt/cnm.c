/*
** Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/mgmt/cnm.c#2
*/

/*! \file   "cnm.c"
    \brief  Module of Concurrent Network Management

    Module of Concurrent Network Management
*/

/*
** Log: cnm.c
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Compile no error before trial run.
 *
 * 11 15 2011 cm.chang
 * NULL
 * Fix possible wrong message when P2P is unregistered
 *
 * 11 14 2011 yuche.tsai
 * [WCXRP00001107] [Volunteer Patch][Driver] Large Network Type index assert in FW issue.
 * Fix large network type index assert in FW issue.
 *
 * 11 10 2011 cm.chang
 * NULL
 * Modify debug message for XLOG
 *
 * 11 08 2011 cm.chang
 * NULL
 * Add RLM and CNM debug message for XLOG
 *
 * 11 01 2011 cm.chang
 * [WCXRP00001077] [All Wi-Fi][Driver] Fix wrong preferred channel for AP and BOW
 * Only check AIS channel for P2P and BOW
 *
 * 10 25 2011 cm.chang
 * [WCXRP00001058] [All Wi-Fi][Driver] Fix sta_rec's phyTypeSet and OBSS scan in AP mode
 * Extension channel of some 5G AP will not follow regulation requirement
 *
 * 09 30 2011 cm.chang
 * [WCXRP00001020] [MT6620 Wi-Fi][Driver] Handle secondary channel offset of AP in 5GHz band
 * .
 *
 * 09 01 2011 cm.chang
 * [WCXRP00000937] [MT6620 Wi-Fi][Driver][FW] cnm.c line #848 assert when doing monkey test
 * Print message only in Linux platform for monkey testing
 *
 * 06 23 2011 cp.wu
 * [WCXRP00000798] [MT6620 Wi-Fi][Firmware] Follow-ups for WAPI frequency offset workaround in firmware SCN module
 * change parameter name from PeerAddr to BSSID
 *
 * 06 20 2011 cp.wu
 * [WCXRP00000798] [MT6620 Wi-Fi][Firmware] Follow-ups for WAPI frequency offset workaround in firmware SCN module
 * 1. specify target's BSSID when requesting channel privilege.
 * 2. pass BSSID information to firmware domain
 *
 * 06 01 2011 cm.chang
 * [WCXRP00000756] [MT6620 Wi-Fi][Driver] 1. AIS follow channel of BOW 2. Provide legal channel function
 * Limit AIS to fixed channel same with BOW
 *
 * 04 12 2011 cm.chang
 * [WCXRP00000634] [MT6620 Wi-Fi][Driver][FW] 2nd BSS will not support 40MHz bandwidth for concurrency
 * .
 *
 * 03 10 2011 cm.chang
 * [WCXRP00000358] [MT6620 Wi-Fi][Driver] Provide concurrent information for each module
 * Check if P2P network index is Tethering AP
 *
 * 03 10 2011 cm.chang
 * [WCXRP00000358] [MT6620 Wi-Fi][Driver] Provide concurrent information for each module
 * Add some functions to let AIS/Tethering or AIS/BOW be the same channel
 *
 * 02 17 2011 cm.chang
 * [WCXRP00000358] [MT6620 Wi-Fi][Driver] Provide concurrent information for each module
 * When P2P registried, invoke BOW deactivate function
 *
 * 01 12 2011 cm.chang
 * [WCXRP00000358] [MT6620 Wi-Fi][Driver] Provide concurrent information for each module
 * Provide function to decide if BSS can be activated or not
 *
 * 12 07 2010 cm.chang
 * [WCXRP00000239] MT6620 Wi-Fi][Driver][FW] Merge concurrent branch back to maintrunk
 * 1. BSSINFO include RLM parameter
 * 2. free all sta records when network is disconnected
 *
 * 12 07 2010 cm.chang
 * [WCXRP00000238] MT6620 Wi-Fi][Driver][FW] Support regulation domain setting from NVRAM and supplicant
 * 1. Country code is from NVRAM or supplicant
 * 2. Change band definition in CMD/EVENT.
 *
 * 11 08 2010 cm.chang
 * [WCXRP00000169] [MT6620 Wi-Fi][Driver][FW] Remove unused CNM recover message ID
 * Remove CNM channel reover message ID
 *
 * 10 13 2010 cm.chang
 * [WCXRP00000094] [MT6620 Wi-Fi][Driver] Connect to 2.4GHz AP, Driver crash.
 * Add exception handle when cmd buffer is not available
 *
 * 08 24 2010 cm.chang
 * NULL
 * Support RLM initail channel of Ad-hoc, P2P and BOW
 *
 * 07 19 2010 wh.su
 *
 * update for security supporting.
 *
 * 07 19 2010 cm.chang
 *
 * Set RLM parameters and enable CNM channel manager
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 07 08 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Rename MID_MNY_CNM_CH_RELEASE to MID_MNY_CNM_CH_ABORT
 *
 * 07 01 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Fix wrong message ID for channel grant to requester
 *
 * 07 01 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Modify CNM message handler for new flow
 *
 * 06 07 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Set 20/40M bandwidth of AP HT OP before association process
 *
 * 05 31 2010 yarco.yang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Add RX TSF Log Feature and ADDBA Rsp with DECLINE handling
 *
 * 05 21 2010 yarco.yang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support TCP/UDP/IP Checksum offload feature
 *
 * 05 12 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Add Power Management - Legacy PS-POLL support.
 *
 * 05 05 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Add a new function to send abort message
 *
 * 04 27 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * BMC mac address shall be ignored in basic config command
 *
 * 04 24 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * g_aprBssInfo[] depends on CFG_SUPPORT_P2P and CFG_SUPPORT_BOW
 *
 * 04 22 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support change of MAC address by host command
 *
 * 04 16 2010 wh.su
 * [BORA00000680][MT6620] Support the statistic for Micxxsoft os query
 * adding the wpa-none for ibss beacon.
 *
 * 04 07 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Fix bug for OBSS scan
 *
 * 03 30 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support 2.4G OBSS scan
 *
 * 03 16 2010 kevin.huang
 * [BORA00000663][WIFISYS][New Feature] AdHoc Mode Support
 * Add AdHoc Mode
 *
 * 03 10 2010 kevin.huang
 * [BORA00000654][WIFISYS][New Feature] CNM Module - Ch Manager Support
 *
 *  *  *  *  *  *  *  *  *  * Add Channel Manager for arbitration of JOIN and SCAN Req
 *
 * 02 25 2010 wh.su
 * [BORA00000605][WIFISYS] Phase3 Integration
 * use the Rx0 dor event indicate.
 *
 * 02 08 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support partial part about cmd basic configuration
 *
 * Dec 10 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Remove conditional compiling FPGA_V5
 *
 * Nov 18 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add function cnmFsmEventInit()
 *
 * Nov 2 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 *
**
*/

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "precomp.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to initialize variables in CNM_INFO_T.
*
* @param (none)
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID cnmInit(P_ADAPTER_T prAdapter)
{

}				/* end of cnmInit() */

/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to initialize variables in CNM_INFO_T.
*
* @param (none)
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID cnmUninit(P_ADAPTER_T prAdapter)
{

}				/* end of cnmUninit() */

/*----------------------------------------------------------------------------*/
/*!
* @brief Before handle the message from other module, it need to obtain
*        the Channel privilege from Channel Manager
*
* @param[in] prMsgHdr   The message need to be handled.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID cnmChMngrRequestPrivilege(P_ADAPTER_T prAdapter, P_MSG_HDR_T prMsgHdr)
{
	P_MSG_CH_REQ_T prMsgChReq;
	P_CMD_CH_PRIVILEGE_T prCmdBody;
	WLAN_STATUS rStatus;

	ASSERT(prAdapter);
	ASSERT(prMsgHdr);

	prMsgChReq = (P_MSG_CH_REQ_T) prMsgHdr;

	prCmdBody = (P_CMD_CH_PRIVILEGE_T)
	    cnmMemAlloc(prAdapter, RAM_TYPE_BUF, sizeof(CMD_CH_PRIVILEGE_T));
	ASSERT(prCmdBody);

	/* To do: exception handle */
	if (!prCmdBody) {
		DBGLOG(CNM, ERROR, "ChReq: fail to get buf (net=%d, token=%d)\n",
				    prMsgChReq->ucNetTypeIndex, prMsgChReq->ucTokenID);

		cnmMemFree(prAdapter, prMsgHdr);
		return;
	}

	DBGLOG(CNM, INFO, "ChReq net=%d token=%d b=%d c=%d s=%d\n",
			   prMsgChReq->ucNetTypeIndex, prMsgChReq->ucTokenID,
			   prMsgChReq->eRfBand, prMsgChReq->ucPrimaryChannel, prMsgChReq->eRfSco);

	prCmdBody->ucNetTypeIndex = prMsgChReq->ucNetTypeIndex;
	prCmdBody->ucTokenID = prMsgChReq->ucTokenID;
	prCmdBody->ucAction = CMD_CH_ACTION_REQ;	/* Request */
	prCmdBody->ucPrimaryChannel = prMsgChReq->ucPrimaryChannel;
	prCmdBody->ucRfSco = (UINT_8) prMsgChReq->eRfSco;
	prCmdBody->ucRfBand = (UINT_8) prMsgChReq->eRfBand;
	prCmdBody->ucReqType = (UINT_8) prMsgChReq->eReqType;
	prCmdBody->ucReserved = 0;
	prCmdBody->u4MaxInterval = prMsgChReq->u4MaxInterval;
	COPY_MAC_ADDR(prCmdBody->aucBSSID, prMsgChReq->aucBSSID);

	ASSERT(prCmdBody->ucNetTypeIndex < NETWORK_TYPE_INDEX_NUM);

	/* For monkey testing 20110901 */
	if (prCmdBody->ucNetTypeIndex >= NETWORK_TYPE_INDEX_NUM)
		DBGLOG(CNM, ERROR, "CNM: ChReq with wrong netIdx=%d\n\n", prCmdBody->ucNetTypeIndex);

	rStatus = wlanSendSetQueryCmd(prAdapter,	/* prAdapter */
				      CMD_ID_CH_PRIVILEGE,	/* ucCID */
				      TRUE,	/* fgSetQuery */
				      FALSE,	/* fgNeedResp */
				      FALSE,	/* fgIsOid */
				      NULL,	/* pfCmdDoneHandler */
				      NULL,	/* pfCmdTimeoutHandler */
				      sizeof(CMD_CH_PRIVILEGE_T),	/* u4SetQueryInfoLen */
				      (PUINT_8) prCmdBody,	/* pucInfoBuffer */
				      NULL,	/* pvSetQueryBuffer */
				      0	/* u4SetQueryBufferLen */
	    );

	ASSERT(rStatus == WLAN_STATUS_PENDING);

	cnmMemFree(prAdapter, prCmdBody);
	cnmMemFree(prAdapter, prMsgHdr);

}				/* end of cnmChMngrRequestPrivilege() */

/*----------------------------------------------------------------------------*/
/*!
* @brief Before deliver the message to other module, it need to release
*        the Channel privilege to Channel Manager.
*
* @param[in] prMsgHdr   The message need to be delivered
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID cnmChMngrAbortPrivilege(P_ADAPTER_T prAdapter, P_MSG_HDR_T prMsgHdr)
{
	P_MSG_CH_ABORT_T prMsgChAbort;
	P_CMD_CH_PRIVILEGE_T prCmdBody;
	WLAN_STATUS rStatus;

	ASSERT(prAdapter);
	ASSERT(prMsgHdr);

	prMsgChAbort = (P_MSG_CH_ABORT_T) prMsgHdr;

	prCmdBody = (P_CMD_CH_PRIVILEGE_T)
	    cnmMemAlloc(prAdapter, RAM_TYPE_BUF, sizeof(CMD_CH_PRIVILEGE_T));
	ASSERT(prCmdBody);

	/* To do: exception handle */
	if (!prCmdBody) {
		DBGLOG(CNM, ERROR, "ChAbort: fail to get buf (net=%d, token=%d)\n",
				    prMsgChAbort->ucNetTypeIndex, prMsgChAbort->ucTokenID);

		cnmMemFree(prAdapter, prMsgHdr);
		return;
	}

	DBGLOG(CNM, INFO, "ChAbort net=%d token=%d\n", prMsgChAbort->ucNetTypeIndex, prMsgChAbort->ucTokenID);

	prCmdBody->ucNetTypeIndex = prMsgChAbort->ucNetTypeIndex;
	prCmdBody->ucTokenID = prMsgChAbort->ucTokenID;
	prCmdBody->ucAction = CMD_CH_ACTION_ABORT;	/* Abort */

	ASSERT(prCmdBody->ucNetTypeIndex < NETWORK_TYPE_INDEX_NUM);

	/* For monkey testing 20110901 */
	if (prCmdBody->ucNetTypeIndex >= NETWORK_TYPE_INDEX_NUM)
		DBGLOG(CNM, ERROR, "CNM: ChAbort with wrong netIdx=%d\n\n", prCmdBody->ucNetTypeIndex);

	rStatus = wlanSendSetQueryCmd(prAdapter,	/* prAdapter */
				      CMD_ID_CH_PRIVILEGE,	/* ucCID */
				      TRUE,	/* fgSetQuery */
				      FALSE,	/* fgNeedResp */
				      FALSE,	/* fgIsOid */
				      NULL,	/* pfCmdDoneHandler */
				      NULL,	/* pfCmdTimeoutHandler */
				      sizeof(CMD_CH_PRIVILEGE_T),	/* u4SetQueryInfoLen */
				      (PUINT_8) prCmdBody,	/* pucInfoBuffer */
				      NULL,	/* pvSetQueryBuffer */
				      0	/* u4SetQueryBufferLen */
	    );

	ASSERT(rStatus == WLAN_STATUS_PENDING);

	cnmMemFree(prAdapter, prCmdBody);
	cnmMemFree(prAdapter, prMsgHdr);

}				/* end of cnmChMngrAbortPrivilege() */

/*----------------------------------------------------------------------------*/
/*!
* @brief
*
* @param (none)
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID cnmChMngrHandleChEvent(P_ADAPTER_T prAdapter, P_WIFI_EVENT_T prEvent)
{
	P_EVENT_CH_PRIVILEGE_T prEventBody;
	P_MSG_CH_GRANT_T prChResp;

	ASSERT(prAdapter);
	ASSERT(prEvent);

	prEventBody = (P_EVENT_CH_PRIVILEGE_T) (prEvent->aucBuffer);
	prChResp = (P_MSG_CH_GRANT_T)
	    cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_CH_GRANT_T));
	ASSERT(prChResp);

	/* To do: exception handle */
	if (!prChResp) {
		DBGLOG(CNM, ERROR, "ChGrant: fail to get buf (net=%d, token=%d)\n",
				    prEventBody->ucNetTypeIndex, prEventBody->ucTokenID);

		return;
	}

	DBGLOG(CNM, INFO, "ChGrant net=%d token=%d ch=%d sco=%d\n",
			   prEventBody->ucNetTypeIndex, prEventBody->ucTokenID,
			   prEventBody->ucPrimaryChannel, prEventBody->ucRfSco);

	ASSERT(prEventBody->ucNetTypeIndex < NETWORK_TYPE_INDEX_NUM);
	ASSERT(prEventBody->ucStatus == EVENT_CH_STATUS_GRANT);

	/* Decide message ID based on network and response status */
	if (prEventBody->ucNetTypeIndex == NETWORK_TYPE_AIS_INDEX)
		prChResp->rMsgHdr.eMsgId = MID_CNM_AIS_CH_GRANT;
#if CFG_ENABLE_WIFI_DIRECT
	else if ((prAdapter->fgIsP2PRegistered) && (prEventBody->ucNetTypeIndex == NETWORK_TYPE_P2P_INDEX))
		prChResp->rMsgHdr.eMsgId = MID_CNM_P2P_CH_GRANT;
#endif
#if CFG_ENABLE_BT_OVER_WIFI
	else if (prEventBody->ucNetTypeIndex == NETWORK_TYPE_BOW_INDEX)
		prChResp->rMsgHdr.eMsgId = MID_CNM_BOW_CH_GRANT;
#endif
	else {
		cnmMemFree(prAdapter, prChResp);
		return;
	}

	prChResp->ucNetTypeIndex = prEventBody->ucNetTypeIndex;
	prChResp->ucTokenID = prEventBody->ucTokenID;
	prChResp->ucPrimaryChannel = prEventBody->ucPrimaryChannel;
	prChResp->eRfSco = (ENUM_CHNL_EXT_T) prEventBody->ucRfSco;
	prChResp->eRfBand = (ENUM_BAND_T) prEventBody->ucRfBand;
	prChResp->eReqType = (ENUM_CH_REQ_TYPE_T) prEventBody->ucReqType;
	prChResp->u4GrantInterval = prEventBody->u4GrantInterval;

	mboxSendMsg(prAdapter, MBOX_ID_0, (P_MSG_HDR_T) prChResp, MSG_SEND_METHOD_BUF);
}

/*----------------------------------------------------------------------------*/
/*!
* @brief This function is invoked for P2P or BOW networks
*
* @param (none)
*
* @return TRUE: suggest to adopt the returned preferred channel
*         FALSE: No suggestion. Caller should adopt its preference
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
cnmPreferredChannel(P_ADAPTER_T prAdapter, P_ENUM_BAND_T prBand, PUINT_8 pucPrimaryChannel, P_ENUM_CHNL_EXT_T prBssSCO)
{
	P_BSS_INFO_T prBssInfo;

	ASSERT(prAdapter);
	ASSERT(prBand);
	ASSERT(pucPrimaryChannel);
	ASSERT(prBssSCO);

	prBssInfo = &prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX];

	if (RLM_NET_PARAM_VALID(prBssInfo)) {
		*prBand = prBssInfo->eBand;
		*pucPrimaryChannel = prBssInfo->ucPrimaryChannel;
		*prBssSCO = prBssInfo->eBssSCO;

		return TRUE;
	}

	return FALSE;
}

/*----------------------------------------------------------------------------*/
/*!
* @brief
*
* @param (none)
*
* @return TRUE: available channel is limited to return value
*         FALSE: no limited
*/
/*----------------------------------------------------------------------------*/
BOOLEAN cnmAisInfraChannelFixed(P_ADAPTER_T prAdapter, P_ENUM_BAND_T prBand, PUINT_8 pucPrimaryChannel)
{
#if CFG_ENABLE_WIFI_DIRECT || (CFG_ENABLE_BT_OVER_WIFI && CFG_BOW_LIMIT_AIS_CHNL)
	P_BSS_INFO_T prBssInfo;
#endif

#if CFG_ENABLE_WIFI_DIRECT
	if (IS_NET_ACTIVE(prAdapter, NETWORK_TYPE_P2P_INDEX) && p2pFuncIsAPMode(prAdapter->rWifiVar.prP2pFsmInfo)) {

		ASSERT(prAdapter->fgIsP2PRegistered);

		prBssInfo = &prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_P2P_INDEX];

		*prBand = prBssInfo->eBand;
		*pucPrimaryChannel = prBssInfo->ucPrimaryChannel;

		return TRUE;
	}
#endif

#if CFG_ENABLE_BT_OVER_WIFI && CFG_BOW_LIMIT_AIS_CHNL
	if (IS_NET_ACTIVE(prAdapter, NETWORK_TYPE_BOW_INDEX)) {

		prBssInfo = &prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_BOW_INDEX];

		*prBand = prBssInfo->eBand;
		*pucPrimaryChannel = prBssInfo->ucPrimaryChannel;

		return TRUE;
	}
#endif

	return FALSE;
}

#if CFG_P2P_LEGACY_COEX_REVISE
BOOLEAN cnmAisDetectP2PChannel(P_ADAPTER_T prAdapter, P_ENUM_BAND_T prBand, PUINT_8 pucPrimaryChannel)
{
	P_WIFI_VAR_T prWifiVar = &prAdapter->rWifiVar;
	P_BSS_INFO_T prP2PBssInfo = &prWifiVar->arBssInfo[NETWORK_TYPE_P2P_INDEX];
#if CFG_ENABLE_WIFI_DIRECT
	if (IS_NET_ACTIVE(prAdapter, NETWORK_TYPE_P2P_INDEX) &&
	    (prP2PBssInfo->eConnectionState == PARAM_MEDIA_STATE_CONNECTED ||
	     (prP2PBssInfo->eCurrentOPMode == OP_MODE_ACCESS_POINT && prP2PBssInfo->eIntendOPMode == OP_MODE_NUM))) {
		*prBand = prP2PBssInfo->eBand;
		*pucPrimaryChannel = prP2PBssInfo->ucPrimaryChannel;
#if CFG_SUPPORT_MCC
		if (nicFreq2ChannelNum(prWifiVar->rConnSettings.u4FreqInKHz * 1000) != *pucPrimaryChannel) {
			DBGLOG(CNM, INFO, "p2p is running on Channel %d, but supplicant try to run as MCC\n",
					   *pucPrimaryChannel);
			return FALSE;
		}
#endif
		DBGLOG(CNM, INFO, "p2p is running on Channel %d, supplicant try to run as SCC\n",
				   *pucPrimaryChannel);
		return TRUE;
	}
#endif
	return FALSE;
}
#endif
/*----------------------------------------------------------------------------*/
/*!
* @brief
*
* @param (none)
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID cnmAisInfraConnectNotify(P_ADAPTER_T prAdapter)
{
#if CFG_ENABLE_BT_OVER_WIFI
	P_BSS_INFO_T prAisBssInfo, prBowBssInfo;

	prAisBssInfo = &prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX];
	prBowBssInfo = &prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_BOW_INDEX];

	if (RLM_NET_PARAM_VALID(prAisBssInfo) && RLM_NET_PARAM_VALID(prBowBssInfo)) {
		if (prAisBssInfo->eBand != prBowBssInfo->eBand ||
		    prAisBssInfo->ucPrimaryChannel != prBowBssInfo->ucPrimaryChannel) {

			/* Notify BOW to do deactivation */
			bowNotifyAllLinkDisconnected(prAdapter);
		}
	}
#endif
}

/*----------------------------------------------------------------------------*/
/*!
* @brief
*
* @param (none)
*
* @return TRUE: permitted
*         FALSE: Not permitted
*/
/*----------------------------------------------------------------------------*/
BOOLEAN cnmAisIbssIsPermitted(P_ADAPTER_T prAdapter)
{
#if CFG_ENABLE_WIFI_DIRECT
	if (IS_NET_ACTIVE(prAdapter, NETWORK_TYPE_P2P_INDEX))
		return FALSE;
#endif

#if CFG_ENABLE_BT_OVER_WIFI
	if (IS_NET_ACTIVE(prAdapter, NETWORK_TYPE_BOW_INDEX))
		return FALSE;
#endif

	return TRUE;
}

/*----------------------------------------------------------------------------*/
/*!
* @brief
*
* @param (none)
*
* @return TRUE: permitted
*         FALSE: Not permitted
*/
/*----------------------------------------------------------------------------*/
BOOLEAN cnmP2PIsPermitted(P_ADAPTER_T prAdapter)
{
	P_BSS_INFO_T prBssInfo;

	prBssInfo = &prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX];

	if (IS_BSS_ACTIVE(prBssInfo) && prBssInfo->eCurrentOPMode == OP_MODE_IBSS)
		return FALSE;
#if CFG_ENABLE_BT_OVER_WIFI
	if (IS_NET_ACTIVE(prAdapter, NETWORK_TYPE_BOW_INDEX)) {
		/* Notify BOW to do deactivation */
		bowNotifyAllLinkDisconnected(prAdapter);
	}
#endif

	return TRUE;
}

/*----------------------------------------------------------------------------*/
/*!
* @brief
*
* @param (none)
*
* @return TRUE: permitted
*         FALSE: Not permitted
*/
/*----------------------------------------------------------------------------*/
BOOLEAN cnmBowIsPermitted(P_ADAPTER_T prAdapter)
{
	P_BSS_INFO_T prBssInfo;

	prBssInfo = &prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX];

	if (IS_BSS_ACTIVE(prBssInfo) && prBssInfo->eCurrentOPMode == OP_MODE_IBSS)
		return FALSE;
#if CFG_ENABLE_WIFI_DIRECT
	if (IS_NET_ACTIVE(prAdapter, NETWORK_TYPE_P2P_INDEX))
		return FALSE;
#endif

	return TRUE;
}

/*----------------------------------------------------------------------------*/
/*!
* @brief
*
* @param (none)
*
* @return TRUE: permitted
*         FALSE: Not permitted
*/
/*----------------------------------------------------------------------------*/
BOOLEAN cnmBss40mBwPermitted(P_ADAPTER_T prAdapter, ENUM_NETWORK_TYPE_INDEX_T eNetTypeIdx)
{
	P_BSS_INFO_T prBssInfo;
	UINT_8 i;
	P_BSS_DESC_T    prBssDesc = NULL;

	/* Note: To support real-time decision instead of current activated-time,
	 *       the STA roaming case shall be considered about synchronization
	 *       problem. Another variable fgAssoc40mBwAllowed is added to
	 *       represent HT capability when association
	 */
	for (i = 0; i < NETWORK_TYPE_INDEX_NUM; i++) {
		if (i != (UINT_8) eNetTypeIdx) {
			prBssInfo = &prAdapter->rWifiVar.arBssInfo[i];

			if (IS_BSS_ACTIVE(prBssInfo) && (prBssInfo->fg40mBwAllowed || prBssInfo->fgAssoc40mBwAllowed))
				return FALSE;
		}
	}

	if (eNetTypeIdx == NETWORK_TYPE_AIS_INDEX)
		prBssDesc = prAdapter->rWifiVar.rAisFsmInfo.prTargetBssDesc;
	else if ((eNetTypeIdx == NETWORK_TYPE_P2P_INDEX) && (prAdapter->rWifiVar.prP2pFsmInfo))
		prBssDesc = prAdapter->rWifiVar.prP2pFsmInfo->prTargetBss;
	if (prBssDesc) {
#if (CFG_FORCE_USE_20BW == 1)
		if (prBssDesc->eBand == BAND_2G4)
			return FALSE;
#endif
		if (prBssDesc->eSco == CHNL_EXT_SCN)
			return FALSE;
	}

	return TRUE;
}
