/*
** Id: tdls.c#1
*/

/*! \file tdls.c
    \brief This file includes IEEE802.11z TDLS support.
*/

/*
** Log: tdls.c
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
#include "gl_wext.h"
#include "tdls.h"
#include "gl_cfg80211.h"
#include <uapi/linux/nl80211.h>
/*******************************************************************************
*						C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*						F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
static VOID TdlsCmdTestRxIndicatePkts(GLUE_INFO_T *prGlueInfo, struct sk_buff *prSkb);

#if TDLS_CFG_CMD_TEST
static void TdlsCmdTestAddPeer(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestChSwProhibitedBitSet(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestChSwReqRecv(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestChSwRspRecv(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestChSwTimeoutSkip(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestDataContSend(GLUE_INFO_T *prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestDataRecv(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestDataSend(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestDelay(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestDiscoveryReqRecv(GLUE_INFO_T *prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestKeepAliveSkip(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestProhibitedBitSet(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestPtiReqRecv(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestPtiRspRecv(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestPtiTxDoneFail(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestRvFrame(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestSetupConfirmRecv(GLUE_INFO_T *prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestSetupReqRecv(GLUE_INFO_T *prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestSetupRspRecv(GLUE_INFO_T *prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestScanCtrl(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestTearDownRecv(GLUE_INFO_T *prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestTxFailSkip(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestTxTdlsFrame(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestTxFrame(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static TDLS_STATUS
TdlsCmdTestTxFmeSetupReqBufTranslate(UINT_8 *pCmdBuf, UINT_32 u4BufLen, PARAM_CUSTOM_TDLS_CMD_STRUCT_T *prCmd);

static void TdlsCmdTestUpdatePeer(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdTestNullRecv(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static VOID TdlsTimerTestDataContSend(ADAPTER_T *prAdapter, UINT_32 u4Param);

static TDLS_STATUS
TdlsTestChStReqRecv(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

static TDLS_STATUS
TdlsTestChStRspRecv(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

static TDLS_STATUS
TdlsTestFrameSend(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

static TDLS_STATUS
TdlsTestNullRecv(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

static TDLS_STATUS
TdlsTestPtiReqRecv(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

static TDLS_STATUS
TdlsTestPtiRspRecv(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

static TDLS_STATUS
TdlsTestTearDownRecv(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

static TDLS_STATUS
TdlsTestDataRecv(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

static TDLS_STATUS
TdlsTestPtiTxFail(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

static TDLS_STATUS
TdlsTestTdlsFrameSend(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

static TDLS_STATUS
TdlsTestTxFailSkip(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

static TDLS_STATUS
TdlsTestKeepAliveSkip(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

static TDLS_STATUS
TdlsTestChSwTimeoutSkip(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

static TDLS_STATUS
TdlsTestScanSkip(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

static TDLS_STATUS
TdlsChSwConf(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

static void TdlsCmdChSwConf(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdInfoDisplay(GLUE_INFO_T *prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdKeyInfoDisplay(GLUE_INFO_T *prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdMibParamUpdate(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdSetupConf(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static void TdlsCmdUapsdConf(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

static TDLS_STATUS
TdlsInfoDisplay(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

static TDLS_STATUS
TdlsKeyInfoDisplay(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

static VOID
TdlsLinkHistoryRecord(GLUE_INFO_T *prGlueInfo,
		      BOOLEAN fgIsTearDown,
		      UINT8 *pucPeerMac, BOOLEAN fgIsFromUs, UINT16 u2ReasonCode, VOID *prOthers);

static VOID
TdlsLinkHistoryRecordUpdate(GLUE_INFO_T *prGlueInfo,
			    UINT8 *pucPeerMac, TDLS_EVENT_HOST_SUBID_SPECIFIC_FRAME eFmeStatus, VOID *pInfo);

static TDLS_STATUS
TdlsMibParamUpdate(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

static TDLS_STATUS
TdlsSetupConf(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

static TDLS_STATUS
TdlsUapsdConf(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

static void TdlsEventStatistics(GLUE_INFO_T *prGlueInfo, UINT8 *prInBuf, UINT32 u4InBufLen);

static void TdlsEventTearDown(GLUE_INFO_T *prGlueInfo, UINT8 *prInBuf, UINT32 u4InBufLen);

#endif /* TDLS_CFG_CMD_TEST */

/*******************************************************************************
*						P R I V A T E   D A T A
********************************************************************************
*/
static BOOLEAN fgIsPtiTimeoutSkip = FALSE;

/*******************************************************************************
*						P R I V A T E  F U N C T I O N S
********************************************************************************
*/

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to indicate packets to upper layer.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prSkb			A pointer to the received packet
*
* \retval None
*
*/
/*----------------------------------------------------------------------------*/
static VOID TdlsCmdTestRxIndicatePkts(GLUE_INFO_T *prGlueInfo, struct sk_buff *prSkb)
{
	struct net_device *prNetDev;

	/* init */
	prNetDev = prGlueInfo->prDevHandler;
	prGlueInfo->rNetDevStats.rx_bytes += prSkb->len;
	prGlueInfo->rNetDevStats.rx_packets++;

	/* pass to upper layer */
	//prNetDev->last_rx = jiffies;
	prSkb->protocol = eth_type_trans(prSkb, prNetDev);
	prSkb->dev = prNetDev;

	if (!in_interrupt())
		netif_rx_ni(prSkb);	/* only in non-interrupt context */
	else
		netif_rx(prSkb);
}

#if TDLS_CFG_CMD_TEST

#define LR_TDLS_FME_FIELD_FILL(__Len) \
do { \
	pPkt += __Len; \
	u4PktLen += __Len; \
} while (0)

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to add a TDLS peer.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_2_[Responder MAC]

		iwpriv wlan0 set_str_cmd 0_2_00:11:22:33:44:01
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestAddPeer(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	PARAM_CUSTOM_TDLS_CMD_STRUCT_T rCmd;
	struct wireless_dev *prWdev;

	/* reset */
	kalMemZero(&rCmd, sizeof(rCmd));

	/* parse arguments */
	CmdStringMacParse(prInBuf, &prInBuf, &u4InBufLen, rCmd.arRspAddr);

	/* init */
	rCmd.rPeerInfo.supported_rates = NULL;
	rCmd.rPeerInfo.ht_capa = &rCmd.rHtCapa;
	rCmd.rPeerInfo.vht_capa = &rCmd.rVhtCapa; /* LINUX_KERNEL_VERSION >= 3.10.0 */
	rCmd.rPeerInfo.sta_flags_set = BIT(NL80211_STA_FLAG_TDLS_PEER);

	/* send command to wifi task to handle */
	prWdev = prGlueInfo->prDevHandler->ieee80211_ptr;
	mtk_cfg80211_add_station(prWdev->wiphy, (void *)0x1, rCmd.arRspAddr, &rCmd.rPeerInfo);
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to simulate to set the TDLS Prohibited bit.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_16_[Enable/Disable]_[Set/Clear]

		iwpriv wlan0 set_str_cmd 0_16_1_1
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestChSwProhibitedBitSet(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	TDLS_CMD_CORE_T rCmd;

	/* parse arguments */
	kalMemZero(rCmd.aucPeerMac, sizeof(rCmd.aucPeerMac));
	rCmd.Content.rCmdProhibit.fgIsEnable = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdProhibit.fgIsSet = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);

	DBGLOG(TDLS, INFO, "%s: fgIsEnable = %d\n", __func__, rCmd.Content.rCmdProhibit.fgIsEnable);

	/* command to do this */
	flgTdlsTestExtCapElm = rCmd.Content.rCmdProhibit.fgIsEnable;

	aucTdlsTestExtCapElm[0] = ELEM_ID_EXTENDED_CAP;
	aucTdlsTestExtCapElm[1] = 5;
	aucTdlsTestExtCapElm[2] = 0;
	aucTdlsTestExtCapElm[3] = 0;
	aucTdlsTestExtCapElm[4] = 0;
	aucTdlsTestExtCapElm[5] = 0;
	aucTdlsTestExtCapElm[6] = (rCmd.Content.rCmdProhibit.fgIsSet << 7);	/* bit39 */
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to receive a channel switch request from the peer.
*
* \param[in] prGlueInfo Pointer to the Adapter structure
* \param[in] prInBuf A pointer to the command string buffer
* \param[in] u4InBufLen The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_1_5_[TDLS Peer MAC]_[Chan]_[RegulatoryClass]_
			[SecondaryChannelOffset]_[SwitchTime]_[SwitchTimeout]

		iwpriv wlan0 set_str_cmd 0_1_5_00:11:22:33:44:01_1_255_0_15000_30000

		RegulatoryClass: TODO (reference to Annex I of 802.11n spec.)
		Secondary Channel Offset:	0 (SCN - no secondary channel)
								1 (SCA - secondary channel above)
								2 (SCB - secondary channel below)
		SwitchTime: units of microseconds

*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestChSwReqRecv(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	WLAN_STATUS rStatus;
	TDLS_CMD_CORE_T rCmd;
	UINT_32 u4BufLen;

	/* parse arguments */
	CmdStringMacParse(prInBuf, &prInBuf, &u4InBufLen, rCmd.aucPeerMac);

	rCmd.Content.rCmdChStReqRcv.u4Chan = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdChStReqRcv.u4RegClass = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdChStReqRcv.u4SecChanOff = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdChStReqRcv.u4SwitchTime = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdChStReqRcv.u4SwitchTimeout = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);

	DBGLOG(TDLS, INFO, "%s:[%pM]u4Chan=%u u4RegClass=%u u4SecChanOff=%u u4SwitchTime=%u u4SwitchTimeout=%u\n",
			    __func__, rCmd.aucPeerMac,
			    (UINT32) rCmd.Content.rCmdChStReqRcv.u4Chan,
			    (UINT32) rCmd.Content.rCmdChStReqRcv.u4RegClass,
			    (UINT32) rCmd.Content.rCmdChStReqRcv.u4SecChanOff,
			    (UINT32) rCmd.Content.rCmdChStReqRcv.u4SwitchTime,
			    (UINT32) rCmd.Content.rCmdChStReqRcv.u4SwitchTimeout);

	/* command to do this */
	rStatus = kalIoctl(prGlueInfo, TdlsTestChStReqRecv, &rCmd, sizeof(rCmd), FALSE, FALSE, FALSE, FALSE, &u4BufLen);

	if (rStatus != WLAN_STATUS_SUCCESS) {
		DBGLOG(TDLS, ERROR, "%s kalIoctl fail:%x\n", __func__, rStatus);
		return;
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to receive a channel switch response from the peer.
*
* \param[in] prGlueInfo Pointer to the Adapter structure
* \param[in] prInBuf A pointer to the command string buffer
* \param[in] u4InBufLen The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_1_6_[TDLS Peer MAC]_[Chan]_
			[SwitchTime]_[SwitchTimeout]_[StatusCode]

		iwpriv wlan0 set_str_cmd 0_1_6_00:11:22:33:44:01_11_15000_30000_0

		RegulatoryClass: TODO (reference to Annex I of 802.11n spec.)
		Secondary Channel Offset:	0 (SCN - no secondary channel)
								1 (SCA - secondary channel above)
								2 (SCB - secondary channel below)
		SwitchTime: units of microseconds

*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestChSwRspRecv(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	WLAN_STATUS rStatus;
	TDLS_CMD_CORE_T rCmd;
	UINT_32 u4BufLen;

	/* parse arguments */
	CmdStringMacParse(prInBuf, &prInBuf, &u4InBufLen, rCmd.aucPeerMac);

	rCmd.Content.rCmdChStRspRcv.u4Chan = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdChStRspRcv.u4SwitchTime = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdChStRspRcv.u4SwitchTimeout = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdChStRspRcv.u4StatusCode = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);

	DBGLOG(TDLS, INFO, "%s: [ %pM ] u4Chan=%u u4SwitchTime=%u u4SwitchTimeout=%u u4StatusCode=%u\n",
			    __func__, rCmd.aucPeerMac,
			    (UINT32) rCmd.Content.rCmdChStRspRcv.u4Chan,
			    (UINT32) rCmd.Content.rCmdChStRspRcv.u4SwitchTime,
			    (UINT32) rCmd.Content.rCmdChStRspRcv.u4SwitchTimeout,
			    (UINT32) rCmd.Content.rCmdChStRspRcv.u4StatusCode);

	/* command to do this */
	rStatus = kalIoctl(prGlueInfo, TdlsTestChStRspRecv, &rCmd, sizeof(rCmd), FALSE, FALSE, FALSE, FALSE, &u4BufLen);

	if (rStatus != WLAN_STATUS_SUCCESS) {
		DBGLOG(TDLS, ERROR, "%s kalIoctl fail:%x\n", __func__, rStatus);
		return;
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to inform firmware to skip channel switch timeout function.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_11_[Enable/Disable]

		iwpriv wlan0 set_str_cmd 0_11_1
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestChSwTimeoutSkip(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	WLAN_STATUS rStatus;
	TDLS_CMD_CORE_T rCmd;
	UINT_32 u4BufLen;

	/* parse arguments */
	kalMemZero(rCmd.aucPeerMac, sizeof(rCmd.aucPeerMac));
	rCmd.Content.rCmdKeepAliveSkip.fgIsEnable = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);

	DBGLOG(TDLS, INFO, "%s: fgIsEnable = %d\n", __func__, rCmd.Content.rCmdKeepAliveSkip.fgIsEnable);

	/* command to do this */
	rStatus = kalIoctl(prGlueInfo,
			   TdlsTestChSwTimeoutSkip, &rCmd, sizeof(rCmd), FALSE, FALSE, FALSE, FALSE, &u4BufLen);

	if (rStatus != WLAN_STATUS_SUCCESS) {
		DBGLOG(TDLS, ERROR, "%s kalIoctl fail:%x\n", __func__, rStatus);
		return;
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to send a data frame to the peer periodically.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*/
/*----------------------------------------------------------------------------*/
static TIMER_T rTdlsTimerTestDataSend;
static UINT_8 aucTdlsTestDataSPeerMac[6];
static UINT_16 u2TdlsTestDataSInterval;

static void TdlsCmdTestDataContSend(GLUE_INFO_T *prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	ADAPTER_T *prAdapter;
	BOOLEAN fgIsEnabled;

	/* init */
	prAdapter = prGlueInfo->prAdapter;

	/* parse arguments */
	CmdStringMacParse(prInBuf, &prInBuf, &u4InBufLen, aucTdlsTestDataSPeerMac);
	u2TdlsTestDataSInterval = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	fgIsEnabled = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);

	cnmTimerStopTimer(prAdapter, &rTdlsTimerTestDataSend);

	if (fgIsEnabled == FALSE) {
		/* stop test timer */
		return;
	}

	/* re-init test timer */
	cnmTimerInitTimer(prAdapter,
			  &rTdlsTimerTestDataSend, (PFN_MGMT_TIMEOUT_FUNC) TdlsTimerTestDataContSend, (ULONG) NULL);

	cnmTimerStartTimer(prAdapter, &rTdlsTimerTestDataSend, u2TdlsTestDataSInterval);
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to receive a data frame from the peer.
*
* \param[in] prGlueInfo Pointer to the Adapter structure
* \param[in] prInBuf A pointer to the command string buffer
* \param[in] u4InBufLen The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_0_x80_[TDLS Peer MAC]_[PM]_[UP]_[EOSP]_[IsNull]

		iwpriv wlan0 set_str_cmd 0_1_x80_00:11:22:33:44:01_0_0_0_0
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestDataRecv(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	WLAN_STATUS rStatus;
	TDLS_CMD_CORE_T rCmd;
	UINT_32 u4BufLen;

	/* parse arguments */
	CmdStringMacParse(prInBuf, &prInBuf, &u4InBufLen, rCmd.aucPeerMac);
	rCmd.Content.rCmdDatRcv.u4PM = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdDatRcv.u4UP = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdDatRcv.u4EOSP = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdDatRcv.u4IsNull = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);

	DBGLOG(TDLS, INFO,
	       "<tdls_cmd> %s: [%pM] PM(%u) UP(%u) EOSP(%u) NULL(%u)\n",
		__func__, rCmd.aucPeerMac,
		(UINT32) rCmd.Content.rCmdDatRcv.u4PM,
		(UINT32) rCmd.Content.rCmdDatRcv.u4UP,
		(UINT32) rCmd.Content.rCmdDatRcv.u4EOSP, (UINT32) rCmd.Content.rCmdDatRcv.u4IsNull);

	/* command to do this */
	rStatus = kalIoctl(prGlueInfo, TdlsTestDataRecv, &rCmd, sizeof(rCmd), FALSE, FALSE, FALSE, FALSE, &u4BufLen);

	if (rStatus != WLAN_STATUS_SUCCESS) {
		DBGLOG(TDLS, ERROR, "%s kalIoctl fail:%x\n", __func__, rStatus);
		return;
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to send a data frame to the peer.
*
* \param[in] prGlueInfo Pointer to the Adapter structure
* \param[in] prInBuf A pointer to the command string buffer
* \param[in] u4InBufLen The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_4_[Responder MAC]_[tx status]

		iwpriv wlan0 set_str_cmd 0_4_00:11:22:33:44:01_0
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestDataSend(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	P_ADAPTER_T prAdapter;
	struct sk_buff *prMsduInfo;
	UINT_8 *prPkt;
	UINT_8 MAC[6];
	UINT_8 ucTxStatus;

	/* init */
	prAdapter = prGlueInfo->prAdapter;

	/* parse arguments */
	CmdStringMacParse(prInBuf, &prInBuf, &u4InBufLen, MAC);
	ucTxStatus = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);

	/* allocate a data frame */
	prMsduInfo = kalPacketAlloc(prGlueInfo, 1000, &prPkt);
	if (prMsduInfo == NULL) {
		DBGLOG(TDLS, ERROR, "<tdls_cmd> %s allocate pkt fail!\n", __func__);
		return;
	}

	/* init dev */
	prMsduInfo->dev = prGlueInfo->prDevHandler;
	if (prMsduInfo->dev == NULL) {
		DBGLOG(TDLS, ERROR, "<tdls_cmd> %s prMsduInfo->dev == NULL!\n", __func__);
		kalPacketFree(prGlueInfo, prMsduInfo);
		return;
	}

	/* init packet */
	prMsduInfo->len = 1000;
	kalMemZero(prMsduInfo->data, 100);	/* for QoS field */
	kalMemCopy(prMsduInfo->data, MAC, 6);
	kalMemCopy(prMsduInfo->data + 6, prAdapter->rMyMacAddr, 6);
	*(UINT_16 *) (prMsduInfo->data + 12) = 0x0800;

	/* simulate OS to send the packet */
	wlanHardStartXmit(prMsduInfo, prMsduInfo->dev);
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to simulate to set the TDLS Prohibited bit.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_16_[mili seconds]

		iwpriv wlan0 set_str_cmd 0_19_1000
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestDelay(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	UINT32 u4Delay;

	u4Delay = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	DBGLOG(TDLS, INFO, "%s: Delay = %d\n", __func__, u4Delay);

	kalMdelay(u4Delay);
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to receive a test discovery request frame command.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_1_10_[DialogToken]_[Peer MAC]_[BSSID]

		iwpriv wlan0 set_str_cmd 0_1_10_1_00:11:22:33:44:01
		iwpriv wlan0 set_str_cmd 0_1_10_1_00:11:22:33:44:01_00:22:33:44:11:22
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestDiscoveryReqRecv(GLUE_INFO_T *prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	ADAPTER_T *prAdapter;
	P_BSS_INFO_T prBssInfo;
	struct sk_buff *prMsduInfo;
	UINT_8 *pPkt;
	UINT_32 u4PktLen, u4IeLen;
	UINT_8 ucDialogToken, aucPeerMac[6], aucBSSID[6], aucZeroMac[6];

	/* parse arguments */
	ucDialogToken = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	CmdStringMacParse(prInBuf, &prInBuf, &u4InBufLen, aucPeerMac);

	kalMemZero(aucZeroMac, sizeof(aucZeroMac));
	kalMemZero(aucBSSID, sizeof(aucBSSID));
	CmdStringMacParse(prInBuf, &prInBuf, &u4InBufLen, aucBSSID);

	DBGLOG(TDLS, INFO,
	       "<tdls_fme> %s: DialogToken=%d from %pM\n", __func__, ucDialogToken, aucPeerMac);

	/* allocate/init packet */
	prAdapter = prGlueInfo->prAdapter;
	prBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX]);
	u4PktLen = 0;

	prMsduInfo = kalPacketAlloc(prGlueInfo, 1600, &pPkt);
	if (prMsduInfo == NULL) {
		DBGLOG(TDLS, ERROR, "<tdls_cmd> %s: allocate pkt fail\n", __func__);
		return;
	}

	prMsduInfo->dev = prGlueInfo->prDevHandler;
	if (prMsduInfo->dev == NULL) {
		DBGLOG(TDLS, ERROR, "<tdls_cmd> %s: MsduInfo->dev == NULL\n", __func__);
		kalPacketFree(prGlueInfo, prMsduInfo);
		return;
	}

	/* make up frame content */
	/* 1. 802.3 header */
	kalMemCopy(pPkt, prAdapter->rMyMacAddr, TDLS_FME_MAC_ADDR_LEN);
	LR_TDLS_FME_FIELD_FILL(TDLS_FME_MAC_ADDR_LEN);
	kalMemCopy(pPkt, aucPeerMac, TDLS_FME_MAC_ADDR_LEN);
	LR_TDLS_FME_FIELD_FILL(TDLS_FME_MAC_ADDR_LEN);
	*(UINT_16 *) pPkt = htons(TDLS_FRM_PROT_TYPE);
	LR_TDLS_FME_FIELD_FILL(2);

	/* 2. payload type */
	*pPkt = TDLS_FRM_PAYLOAD_TYPE;
	LR_TDLS_FME_FIELD_FILL(1);

	/* 3. Frame Formation - (1) Category */
	*pPkt = TDLS_FRM_CATEGORY;
	LR_TDLS_FME_FIELD_FILL(1);

	/* 3. Frame Formation - (2) Action */
	*pPkt = TDLS_FRM_ACTION_DISCOVERY_REQ;
	LR_TDLS_FME_FIELD_FILL(1);

	/* 3. Frame Formation - (3) Dialog token */
	*pPkt = ucDialogToken;
	LR_TDLS_FME_FIELD_FILL(1);

	/* 3. Frame Formation - (16) Link identifier element */
	TDLS_LINK_IDENTIFIER_IE(pPkt)->ucId = ELEM_ID_LINK_IDENTIFIER;
	TDLS_LINK_IDENTIFIER_IE(pPkt)->ucLength = 18;

	if (kalMemCmp(aucBSSID, aucZeroMac, 6) == 0)
		kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aBSSID, prBssInfo->aucBSSID, 6);
	else
		kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aBSSID, aucBSSID, 6);

	kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aInitiator, aucPeerMac, 6);
	kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aResponder, prAdapter->rMyMacAddr, 6);

	u4IeLen = IE_SIZE(pPkt);
	LR_TDLS_FME_FIELD_FILL(u4IeLen);

	/* 4. Update packet length */
	prMsduInfo->len = u4PktLen;
	dumpMemory8(prMsduInfo->data, u4PktLen);

	/* pass to OS */
	TdlsCmdTestRxIndicatePkts(prGlueInfo, prMsduInfo);
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to inform firmware to skip keep alive function.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_10_[Enable/Disable]

		iwpriv wlan0 set_str_cmd 0_10_1
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestKeepAliveSkip(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	WLAN_STATUS rStatus;
	TDLS_CMD_CORE_T rCmd;
	UINT_32 u4BufLen;

	/* parse arguments */
	kalMemZero(rCmd.aucPeerMac, sizeof(rCmd.aucPeerMac));
	rCmd.Content.rCmdKeepAliveSkip.fgIsEnable = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);

	DBGLOG(TDLS, INFO, "%s: fgIsEnable = %d\n", __func__, rCmd.Content.rCmdKeepAliveSkip.fgIsEnable);

	/* command to do this */
	rStatus = kalIoctl(prGlueInfo,
			   TdlsTestKeepAliveSkip, &rCmd, sizeof(rCmd), FALSE, FALSE, FALSE, FALSE, &u4BufLen);

	if (rStatus != WLAN_STATUS_SUCCESS) {
		DBGLOG(TDLS, ERROR, "%s kalIoctl fail:%x\n", __func__, rStatus);
		return;
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to simulate to set the TDLS Prohibited bit.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_11_[Enable/Disable]_[Set/Clear]

		iwpriv wlan0 set_str_cmd 0_13_1_1
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestProhibitedBitSet(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	TDLS_CMD_CORE_T rCmd;

	/* parse arguments */
	kalMemZero(rCmd.aucPeerMac, sizeof(rCmd.aucPeerMac));
	rCmd.Content.rCmdProhibit.fgIsEnable = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdProhibit.fgIsSet = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);

	DBGLOG(TDLS, INFO, "%s: fgIsEnable = %d\n", __func__, rCmd.Content.rCmdProhibit.fgIsEnable);

	/* command to do this */
	flgTdlsTestExtCapElm = rCmd.Content.rCmdProhibit.fgIsEnable;

	aucTdlsTestExtCapElm[0] = ELEM_ID_EXTENDED_CAP;
	aucTdlsTestExtCapElm[1] = 5;
	aucTdlsTestExtCapElm[2] = 0;
	aucTdlsTestExtCapElm[3] = 0;
	aucTdlsTestExtCapElm[4] = 0;
	aucTdlsTestExtCapElm[5] = 0;
	aucTdlsTestExtCapElm[6] = (rCmd.Content.rCmdProhibit.fgIsSet << 6);	/* bit38 */
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to receive a PTI request from the AP.
*
* \param[in] prGlueInfo Pointer to the Adapter structure
* \param[in] prInBuf A pointer to the command string buffer
* \param[in] u4InBufLen The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_1_4_[TDLS Peer MAC]_[Dialog Token]

		iwpriv wlan0 set_str_cmd 0_1_4_00:11:22:33:44:01_0
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestPtiReqRecv(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	WLAN_STATUS rStatus;
	TDLS_CMD_CORE_T rCmd;
	UINT_32 u4BufLen;

	/* parse arguments */
	CmdStringMacParse(prInBuf, &prInBuf, &u4InBufLen, rCmd.aucPeerMac);

	rCmd.Content.rCmdPtiRspRcv.u4DialogToken = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);

	DBGLOG(TDLS, INFO, "%s: [ %pM ] u4DialogToken = %u\n",
			    __func__, rCmd.aucPeerMac, (UINT32) rCmd.Content.rCmdPtiRspRcv.u4DialogToken);

	/* command to do this */
	rStatus = kalIoctl(prGlueInfo, TdlsTestPtiReqRecv, &rCmd, sizeof(rCmd), FALSE, FALSE, FALSE, FALSE, &u4BufLen);

	if (rStatus != WLAN_STATUS_SUCCESS) {
		DBGLOG(TDLS, ERROR, "%s kalIoctl fail:%x\n", __func__, rStatus);
		return;
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to receive a PTI response from the peer.
*
* \param[in] prGlueInfo Pointer to the Adapter structure
* \param[in] prInBuf A pointer to the command string buffer
* \param[in] u4InBufLen The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_1_9_[TDLS Peer MAC]_[Dialog Token]_[PM]

		iwpriv wlan0 set_str_cmd 0_1_9_00:11:22:33:44:01_0_1
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestPtiRspRecv(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	WLAN_STATUS rStatus;
	TDLS_CMD_CORE_T rCmd;
	UINT_32 u4BufLen;

	/* parse arguments */
	CmdStringMacParse(prInBuf, &prInBuf, &u4InBufLen, rCmd.aucPeerMac);

	rCmd.Content.rCmdPtiRspRcv.u4DialogToken = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdPtiRspRcv.u4PM = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);

	DBGLOG(TDLS, INFO, "%s: [%pM] u4DialogToken = %u %u\n",
			    __func__, rCmd.aucPeerMac,
			    (UINT32) rCmd.Content.rCmdPtiRspRcv.u4DialogToken,
			    (UINT32) rCmd.Content.rCmdPtiRspRcv.u4PM);

	/* command to do this */
	rStatus = kalIoctl(prGlueInfo, TdlsTestPtiRspRecv, &rCmd, sizeof(rCmd), FALSE, FALSE, FALSE, FALSE, &u4BufLen);

	if (rStatus != WLAN_STATUS_SUCCESS) {
		DBGLOG(TDLS, ERROR, "%s kalIoctl fail:%x\n", __func__, rStatus);
		return;
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to inform firmware to simulate PTI tx done fail case.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_21_[Enable/Disable]

		iwpriv wlan0 set_str_cmd 0_21_1
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestPtiTxDoneFail(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	WLAN_STATUS rStatus;
	TDLS_CMD_CORE_T rCmd;
	UINT_32 u4BufLen;

	/* parse arguments */
	kalMemZero(rCmd.aucPeerMac, sizeof(rCmd.aucPeerMac));
	rCmd.Content.rCmdPtiTxFail.fgIsEnable = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);

	DBGLOG(TDLS, INFO, "%s: fgIsEnable = %d\n", __func__, rCmd.Content.rCmdPtiTxFail.fgIsEnable);

	/* command to do this */
	rStatus = kalIoctl(prGlueInfo, TdlsTestPtiTxFail, &rCmd, sizeof(rCmd), FALSE, FALSE, FALSE, FALSE, &u4BufLen);

	if (rStatus != WLAN_STATUS_SUCCESS) {
		DBGLOG(TDLS, ERROR, "%s kalIoctl fail:%x\n", __func__, rStatus);
		return;
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to receive a test frame.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestRvFrame(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
/* PARAM_CUSTOM_TDLS_CMD_STRUCT_T rCmd; */
/* TDLS_STATUS u4Status; */
	UINT_32 u4Subcmd;
/* UINT_32 u4BufLen; */

	/* parse sub-command */
	u4Subcmd = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	DBGLOG(TDLS, INFO, "<tdls_cmd> test rv frame sub command = %u\n", (UINT32) u4Subcmd);

	/* parse command arguments */
	switch (u4Subcmd) {
	case TDLS_FRM_ACTION_SETUP_REQ:
		/* simulate to receive a setup request frame */
		TdlsCmdTestSetupReqRecv(prGlueInfo, prInBuf, u4InBufLen);
		break;

	case TDLS_FRM_ACTION_SETUP_RSP:
		/* simulate to receive a setup response frame */
		TdlsCmdTestSetupRspRecv(prGlueInfo, prInBuf, u4InBufLen);
		break;

	case TDLS_FRM_ACTION_CONFIRM:
		/* simulate to receive a setup confirm frame */
		TdlsCmdTestSetupConfirmRecv(prGlueInfo, prInBuf, u4InBufLen);
		break;

	case TDLS_FRM_ACTION_TEARDOWN:
		/* simulate to receive a tear down frame */
		TdlsCmdTestTearDownRecv(prGlueInfo, prInBuf, u4InBufLen);
		break;

	case TDLS_FRM_ACTION_PTI:
		/* simulate to receive a PTI request frame */
		TdlsCmdTestPtiReqRecv(prGlueInfo, prInBuf, u4InBufLen);
		break;

	case TDLS_FRM_ACTION_PTI_RSP:
		/* simulate to receive a PTI response frame */
		TdlsCmdTestPtiRspRecv(prGlueInfo, prInBuf, u4InBufLen);
		break;

	case TDLS_FRM_DATA_TEST_DATA:
		/* simulate to receive a DATA frame */
		TdlsCmdTestDataRecv(prGlueInfo, prInBuf, u4InBufLen);
		break;

	case TDLS_FRM_ACTION_CHAN_SWITCH_REQ:
		/* simulate to receive a channel switch request frame */
		TdlsCmdTestChSwReqRecv(prGlueInfo, prInBuf, u4InBufLen);
		break;

	case TDLS_FRM_ACTION_CHAN_SWITCH_RSP:
		/* simulate to receive a channel switch response frame */
		TdlsCmdTestChSwRspRecv(prGlueInfo, prInBuf, u4InBufLen);
		break;

	case TDLS_FRM_ACTION_DISCOVERY_REQ:
		/* simulate to receive a discovery request frame */
		TdlsCmdTestDiscoveryReqRecv(prGlueInfo, prInBuf, u4InBufLen);
		break;

	default:
		DBGLOG(TDLS, ERROR, "<tdls_cmd> wrong test rv frame sub command\n");
		return;
	}

/* if (u4Status != TDLS_STATUS_SUCCESS) */
	{
/* DBGLOG(TDLS, ERROR, ("<tdls_cmd> command parse fail\n")); */
/* return; */
	}

	/* send command to wifi task to handle */
#if 0
	kalIoctl(prGlueInfo,
		 TdlsTestFrameSend,
		 (PVOID)&rCmd, sizeof(PARAM_CUSTOM_TDLS_CMD_STRUCT_T), FALSE, FALSE, TRUE, FALSE, &u4BufLen);
#endif
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to receive a test setup confirm frame command.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_1_2_[DialogToken]_[StatusCode]_[Peer MAC]

		iwpriv wlan0 set_str_cmd 0_1_2_1_0_00:11:22:33:44:01
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestSetupConfirmRecv(GLUE_INFO_T *prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	ADAPTER_T *prAdapter;
	P_BSS_INFO_T prBssInfo;
	PM_PROFILE_SETUP_INFO_T *prPmProfSetupInfo;
	struct sk_buff *prMsduInfo;
	UINT_8 *pPkt;
	UINT_32 u4PktLen, u4IeLen;
	UINT_8 ucDialogToken, ucStatusCode, aucPeerMac[6];

	/* parse arguments */
	ucDialogToken = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	ucStatusCode = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	CmdStringMacParse(prInBuf, &prInBuf, &u4InBufLen, aucPeerMac);

	DBGLOG(TDLS, INFO,
	       "<tdls_fme> %s: DialogToken=%d StatusCode=%d from %pM\n",
		__func__, ucDialogToken, ucStatusCode, aucPeerMac);

	/* allocate/init packet */
	prAdapter = prGlueInfo->prAdapter;
	prBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX]);
	prPmProfSetupInfo = &prBssInfo->rPmProfSetupInfo;
	u4PktLen = 0;

	prMsduInfo = kalPacketAlloc(prGlueInfo, 1600, &pPkt);
	if (prMsduInfo == NULL) {
		DBGLOG(TDLS, ERROR, "<tdls_cmd> %s: allocate pkt fail\n", __func__);
		return;
	}

	prMsduInfo->dev = prGlueInfo->prDevHandler;
	if (prMsduInfo->dev == NULL) {
		DBGLOG(TDLS, ERROR, "<tdls_cmd> %s: MsduInfo->dev == NULL\n", __func__);
		kalPacketFree(prGlueInfo, prMsduInfo);
		return;
	}

	/* make up frame content */
	/* 1. 802.3 header */
	kalMemCopy(pPkt, prAdapter->rMyMacAddr, TDLS_FME_MAC_ADDR_LEN);
	LR_TDLS_FME_FIELD_FILL(TDLS_FME_MAC_ADDR_LEN);
	kalMemCopy(pPkt, aucPeerMac, TDLS_FME_MAC_ADDR_LEN);
	LR_TDLS_FME_FIELD_FILL(TDLS_FME_MAC_ADDR_LEN);
	*(UINT_16 *) pPkt = htons(TDLS_FRM_PROT_TYPE);
	LR_TDLS_FME_FIELD_FILL(2);

	/* 2. payload type */
	*pPkt = TDLS_FRM_PAYLOAD_TYPE;
	LR_TDLS_FME_FIELD_FILL(1);

	/* 3. Frame Formation - (1) Category */
	*pPkt = TDLS_FRM_CATEGORY;
	LR_TDLS_FME_FIELD_FILL(1);

	/* 3. Frame Formation - (2) Action */
	*pPkt = TDLS_FRM_ACTION_CONFIRM;
	LR_TDLS_FME_FIELD_FILL(1);

	/* 3. Frame Formation - (3) Status Code */
	*pPkt = ucStatusCode;
	*(pPkt + 1) = 0x00;
	LR_TDLS_FME_FIELD_FILL(2);

	/* 3. Frame Formation - (4) Dialog token */
	*pPkt = ucDialogToken;
	LR_TDLS_FME_FIELD_FILL(1);

	/* 3. Frame Formation - (17) WMM Information element */
	if (prAdapter->rWifiVar.fgSupportQoS) {
		u4IeLen = mqmGenerateWmmParamIEByParam(prAdapter, prBssInfo, pPkt, OP_MODE_INFRASTRUCTURE);
		LR_TDLS_FME_FIELD_FILL(u4IeLen);
	}

	/* 3. Frame Formation - (16) Link identifier element */
	TDLS_LINK_IDENTIFIER_IE(pPkt)->ucId = ELEM_ID_LINK_IDENTIFIER;
	TDLS_LINK_IDENTIFIER_IE(pPkt)->ucLength = ELEM_LEN_LINK_IDENTIFIER;

	kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aBSSID, prBssInfo->aucBSSID, 6);
	kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aInitiator, aucPeerMac, 6);
	kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aResponder, prAdapter->rMyMacAddr, 6);

	u4IeLen = IE_SIZE(pPkt);
	LR_TDLS_FME_FIELD_FILL(u4IeLen);

	/* 4. Update packet length */
	prMsduInfo->len = u4PktLen;
	dumpMemory8(prMsduInfo->data, u4PktLen);

	/* pass to OS */
	TdlsCmdTestRxIndicatePkts(prGlueInfo, prMsduInfo);
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to receive a test setup request frame command.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_1_0_[DialogToken]_[Peer MAC]_[BSSID]

		iwpriv wlan0 set_str_cmd 0_1_0_1_00:11:22:33:44:01
		iwpriv wlan0 set_str_cmd 0_1_0_1_00:11:22:33:44:01_00:22:33:44:11:22
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestSetupReqRecv(GLUE_INFO_T *prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	ADAPTER_T *prAdapter;
	P_BSS_INFO_T prBssInfo;
	struct sk_buff *prMsduInfo;
	UINT_8 *pPkt;
	UINT_32 u4PktLen, u4IeLen;
	UINT_8 ucDialogToken, aucPeerMac[6], aucBSSID[6], aucZeroMac[6];
	UINT_16 u2CapInfo;

	/* parse arguments */
	ucDialogToken = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	CmdStringMacParse(prInBuf, &prInBuf, &u4InBufLen, aucPeerMac);

	kalMemZero(aucZeroMac, sizeof(aucZeroMac));
	kalMemZero(aucBSSID, sizeof(aucBSSID));
	CmdStringMacParse(prInBuf, &prInBuf, &u4InBufLen, aucBSSID);

	DBGLOG(TDLS, INFO,
	       "<tdls_fme> %s: DialogToken=%d from %pM\n", __func__, ucDialogToken, aucPeerMac);

	/* allocate/init packet */
	prAdapter = prGlueInfo->prAdapter;
	prBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX]);
	u4PktLen = 0;

	prMsduInfo = kalPacketAlloc(prGlueInfo, 1600, &pPkt);
	if (prMsduInfo == NULL) {
		DBGLOG(TDLS, ERROR, "<tdls_cmd> %s: allocate pkt fail\n", __func__);
		return;
	}

	prMsduInfo->dev = prGlueInfo->prDevHandler;
	if (prMsduInfo->dev == NULL) {
		DBGLOG(TDLS, ERROR, "<tdls_cmd> %s: MsduInfo->dev == NULL\n", __func__);
		kalPacketFree(prGlueInfo, prMsduInfo);
		return;
	}

	/* make up frame content */
	/* 1. 802.3 header */
	kalMemCopy(pPkt, prAdapter->rMyMacAddr, TDLS_FME_MAC_ADDR_LEN);
	LR_TDLS_FME_FIELD_FILL(TDLS_FME_MAC_ADDR_LEN);
	kalMemCopy(pPkt, aucPeerMac, TDLS_FME_MAC_ADDR_LEN);
	LR_TDLS_FME_FIELD_FILL(TDLS_FME_MAC_ADDR_LEN);
	*(UINT_16 *) pPkt = htons(TDLS_FRM_PROT_TYPE);
	LR_TDLS_FME_FIELD_FILL(2);

	/* 2. payload type */
	*pPkt = TDLS_FRM_PAYLOAD_TYPE;
	LR_TDLS_FME_FIELD_FILL(1);

	/* 3. Frame Formation - (1) Category */
	*pPkt = TDLS_FRM_CATEGORY;
	LR_TDLS_FME_FIELD_FILL(1);

	/* 3. Frame Formation - (2) Action */
	*pPkt = TDLS_FRM_ACTION_SETUP_REQ;
	LR_TDLS_FME_FIELD_FILL(1);

	/* 3. Frame Formation - (3) Dialog token */
	*pPkt = ucDialogToken;
	LR_TDLS_FME_FIELD_FILL(1);

	/* 3. Frame Formation - (4) Capability */
	u2CapInfo = assocBuildCapabilityInfo(prAdapter, NULL);
	WLAN_SET_FIELD_16(pPkt, u2CapInfo);
	LR_TDLS_FME_FIELD_FILL(2);

	/* 4. Append general IEs */
	u4IeLen = TdlsFrameGeneralIeAppend(prAdapter, NULL, 0, pPkt);
	LR_TDLS_FME_FIELD_FILL(u4IeLen);

	/* 3. Frame Formation - (10) Extended capabilities element */
	EXT_CAP_IE(pPkt)->ucId = ELEM_ID_EXTENDED_CAP;
	EXT_CAP_IE(pPkt)->ucLength = 5;

	EXT_CAP_IE(pPkt)->aucCapabilities[0] = 0x00;	/* bit0 ~ bit7 */
	EXT_CAP_IE(pPkt)->aucCapabilities[1] = 0x00;	/* bit8 ~ bit15 */
	EXT_CAP_IE(pPkt)->aucCapabilities[2] = 0x00;	/* bit16 ~ bit23 */
	EXT_CAP_IE(pPkt)->aucCapabilities[3] = 0x00;	/* bit24 ~ bit31 */
	EXT_CAP_IE(pPkt)->aucCapabilities[4] = 0x00;	/* bit32 ~ bit39 */

	/* TDLS_EX_CAP_PEER_UAPSD */
	EXT_CAP_IE(pPkt)->aucCapabilities[3] |= BIT((28 - 24));
	/* TDLS_EX_CAP_CHAN_SWITCH */
	EXT_CAP_IE(pPkt)->aucCapabilities[3] |= BIT((30 - 24));
	/* TDLS_EX_CAP_TDLS */
	EXT_CAP_IE(pPkt)->aucCapabilities[4] |= BIT((37 - 32));

	u4IeLen = IE_SIZE(pPkt);
	LR_TDLS_FME_FIELD_FILL(u4IeLen);

	/* 3. Frame Formation - (16) Link identifier element */
	TDLS_LINK_IDENTIFIER_IE(pPkt)->ucId = ELEM_ID_LINK_IDENTIFIER;
	TDLS_LINK_IDENTIFIER_IE(pPkt)->ucLength = 18;

	if (kalMemCmp(aucBSSID, aucZeroMac, 6) == 0)
		kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aBSSID, prBssInfo->aucBSSID, 6);
	else
		kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aBSSID, aucBSSID, 6);

	kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aInitiator, aucPeerMac, 6);
	kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aResponder, prAdapter->rMyMacAddr, 6);

	u4IeLen = IE_SIZE(pPkt);
	LR_TDLS_FME_FIELD_FILL(u4IeLen);

	/* 4. Update packet length */
	prMsduInfo->len = u4PktLen;
	dumpMemory8(prMsduInfo->data, u4PktLen);

	/* pass to OS */
	TdlsCmdTestRxIndicatePkts(prGlueInfo, prMsduInfo);
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to receive a test setup response frame command.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_1_1_[DialogToken]_[StatusCode]_[Peer MAC]

		iwpriv wlan0 set_str_cmd 0_1_1_1_0_00:11:22:33:44:01
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestSetupRspRecv(GLUE_INFO_T *prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	ADAPTER_T *prAdapter;
	P_BSS_INFO_T prBssInfo;
	struct sk_buff *prMsduInfo;
	UINT_8 *pPkt;
	UINT_32 u4PktLen, u4IeLen;
	UINT_8 ucDialogToken, ucStatusCode, aucPeerMac[6];
	UINT_16 u2CapInfo;

	/* parse arguments */
	ucDialogToken = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	ucStatusCode = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	CmdStringMacParse(prInBuf, &prInBuf, &u4InBufLen, aucPeerMac);

	DBGLOG(TDLS, INFO,
	       "<tdls_fme> %s: DialogToken=%d StatusCode=%d from %pM\n",
		__func__, ucDialogToken, ucStatusCode, aucPeerMac);

	/* allocate/init packet */
	prAdapter = prGlueInfo->prAdapter;
	prBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX]);
	u4PktLen = 0;

	prMsduInfo = kalPacketAlloc(prGlueInfo, 1600, &pPkt);
	if (prMsduInfo == NULL) {
		DBGLOG(TDLS, ERROR, "<tdls_cmd> %s: allocate pkt fail\n", __func__);
		return;
	}

	prMsduInfo->dev = prGlueInfo->prDevHandler;
	if (prMsduInfo->dev == NULL) {
		DBGLOG(TDLS, ERROR, "<tdls_cmd> %s: MsduInfo->dev == NULL\n", __func__);
		kalPacketFree(prGlueInfo, prMsduInfo);
		return;
	}

	/* make up frame content */
	/* 1. 802.3 header */
	kalMemCopy(pPkt, prAdapter->rMyMacAddr, TDLS_FME_MAC_ADDR_LEN);
	LR_TDLS_FME_FIELD_FILL(TDLS_FME_MAC_ADDR_LEN);
	kalMemCopy(pPkt, aucPeerMac, TDLS_FME_MAC_ADDR_LEN);
	LR_TDLS_FME_FIELD_FILL(TDLS_FME_MAC_ADDR_LEN);
	*(UINT_16 *) pPkt = htons(TDLS_FRM_PROT_TYPE);
	LR_TDLS_FME_FIELD_FILL(2);

	/* 2. payload type */
	*pPkt = TDLS_FRM_PAYLOAD_TYPE;
	LR_TDLS_FME_FIELD_FILL(1);

	/* 3. Frame Formation - (1) Category */
	*pPkt = TDLS_FRM_CATEGORY;
	LR_TDLS_FME_FIELD_FILL(1);

	/* 3. Frame Formation - (2) Action */
	*pPkt = TDLS_FRM_ACTION_SETUP_RSP;
	LR_TDLS_FME_FIELD_FILL(1);

	/* 3. Frame Formation - (3) Status Code */
	*pPkt = ucStatusCode;
	*(pPkt + 1) = 0x00;
	LR_TDLS_FME_FIELD_FILL(2);

	/* 3. Frame Formation - (4) Dialog token */
	*pPkt = ucDialogToken;
	LR_TDLS_FME_FIELD_FILL(1);

	/* 3. Frame Formation - (5) Capability */
	u2CapInfo = assocBuildCapabilityInfo(prAdapter, NULL);
	WLAN_SET_FIELD_16(pPkt, u2CapInfo);
	LR_TDLS_FME_FIELD_FILL(2);

	/* 4. Append general IEs */
	u4IeLen = TdlsFrameGeneralIeAppend(prAdapter, NULL, 0, pPkt);
	LR_TDLS_FME_FIELD_FILL(u4IeLen);

	/* 3. Frame Formation - (10) Extended capabilities element */
	EXT_CAP_IE(pPkt)->ucId = ELEM_ID_EXTENDED_CAP;
	EXT_CAP_IE(pPkt)->ucLength = 5;

	EXT_CAP_IE(pPkt)->aucCapabilities[0] = 0x00;	/* bit0 ~ bit7 */
	EXT_CAP_IE(pPkt)->aucCapabilities[1] = 0x00;	/* bit8 ~ bit15 */
	EXT_CAP_IE(pPkt)->aucCapabilities[2] = 0x00;	/* bit16 ~ bit23 */
	EXT_CAP_IE(pPkt)->aucCapabilities[3] = 0x00;	/* bit24 ~ bit31 */
	EXT_CAP_IE(pPkt)->aucCapabilities[4] = 0x00;	/* bit32 ~ bit39 */

	/* TDLS_EX_CAP_PEER_UAPSD */
	EXT_CAP_IE(pPkt)->aucCapabilities[3] |= BIT((28 - 24));
	/* TDLS_EX_CAP_CHAN_SWITCH */
	EXT_CAP_IE(pPkt)->aucCapabilities[3] |= BIT((30 - 24));
	/* TDLS_EX_CAP_TDLS */
	EXT_CAP_IE(pPkt)->aucCapabilities[4] |= BIT((37 - 32));

	u4IeLen = IE_SIZE(pPkt);
	LR_TDLS_FME_FIELD_FILL(u4IeLen);

	/* 3. Frame Formation - (16) Link identifier element */
	TDLS_LINK_IDENTIFIER_IE(pPkt)->ucId = ELEM_ID_LINK_IDENTIFIER;
	TDLS_LINK_IDENTIFIER_IE(pPkt)->ucLength = ELEM_LEN_LINK_IDENTIFIER;

	kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aBSSID, prBssInfo->aucBSSID, 6);
	kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aInitiator, prAdapter->rMyMacAddr, 6);
	kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aResponder, aucPeerMac, 6);

	u4IeLen = IE_SIZE(pPkt);
	LR_TDLS_FME_FIELD_FILL(u4IeLen);

	/* 4. Update packet length */
	prMsduInfo->len = u4PktLen;
	dumpMemory8(prMsduInfo->data, u4PktLen);

	/* pass to OS */
	TdlsCmdTestRxIndicatePkts(prGlueInfo, prMsduInfo);
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to inform firmware to skip channel switch timeout function.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_14_[Enable/Disable]

		iwpriv wlan0 set_str_cmd 0_14_0
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestScanCtrl(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	WLAN_STATUS rStatus;
	TDLS_CMD_CORE_T rCmd;
	UINT_32 u4BufLen;

	/* parse arguments */
	kalMemZero(rCmd.aucPeerMac, sizeof(rCmd.aucPeerMac));
	rCmd.Content.rCmdScanSkip.fgIsEnable = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);

	DBGLOG(TDLS, INFO, "%s: fgIsEnable = %d\n", __func__, rCmd.Content.rCmdScanSkip.fgIsEnable);

	/* command to do this */
	rStatus = kalIoctl(prGlueInfo, TdlsTestScanSkip, &rCmd, sizeof(rCmd), FALSE, FALSE, FALSE, FALSE, &u4BufLen);

	if (rStatus != WLAN_STATUS_SUCCESS) {
		DBGLOG(TDLS, ERROR, "%s kalIoctl fail:%x\n", __func__, rStatus);
		return;
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to receive a test tear down frame command.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_1_3_[IsInitiator]_[ReasonCode]_[Peer MAC]_[Where]

		Where 0 (From driver) or 1 (From FW)

		iwpriv wlan0 set_str_cmd 0_1_3_1_26_00:11:22:33:44:01_0
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestTearDownRecv(GLUE_INFO_T *prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	ADAPTER_T *prAdapter;
	P_BSS_INFO_T prBssInfo;
	struct sk_buff *prMsduInfo;
	UINT_8 *pPkt;
	UINT_32 u4PktLen, u4IeLen;
	BOOLEAN fgIsInitiator;
	UINT_8 ucReasonCode, aucPeerMac[6];
	BOOLEAN fgIsFromWhich;
	WLAN_STATUS rStatus;
	TDLS_CMD_CORE_T rCmd;
	UINT_32 u4BufLen;

	/* parse arguments */
	fgIsInitiator = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	ucReasonCode = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	CmdStringMacParse(prInBuf, &prInBuf, &u4InBufLen, aucPeerMac);
	fgIsFromWhich = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);

	DBGLOG(TDLS, INFO,
	       "<tdls_fme> %s: ReasonCode=%d from %pM %d\n",
		__func__, ucReasonCode, aucPeerMac, fgIsFromWhich);

	if (fgIsFromWhich == 0) {
		/* allocate/init packet */
		prAdapter = prGlueInfo->prAdapter;
		prBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX]);
		u4PktLen = 0;

		prMsduInfo = kalPacketAlloc(prGlueInfo, 1600, &pPkt);
		if (prMsduInfo == NULL) {
			DBGLOG(TDLS, ERROR, "<tdls_cmd> %s: allocate pkt fail\n", __func__);
			return;
		}

		prMsduInfo->dev = prGlueInfo->prDevHandler;
		if (prMsduInfo->dev == NULL) {
			DBGLOG(TDLS, ERROR, "<tdls_cmd> %s: MsduInfo->dev == NULL\n", __func__);
			kalPacketFree(prGlueInfo, prMsduInfo);
			return;
		}

		/* make up frame content */
		/* 1. 802.3 header */
		kalMemCopy(pPkt, prAdapter->rMyMacAddr, TDLS_FME_MAC_ADDR_LEN);
		LR_TDLS_FME_FIELD_FILL(TDLS_FME_MAC_ADDR_LEN);
		kalMemCopy(pPkt, aucPeerMac, TDLS_FME_MAC_ADDR_LEN);
		LR_TDLS_FME_FIELD_FILL(TDLS_FME_MAC_ADDR_LEN);
		*(UINT_16 *) pPkt = htons(TDLS_FRM_PROT_TYPE);
		LR_TDLS_FME_FIELD_FILL(2);

		/* 2. payload type */
		*pPkt = TDLS_FRM_PAYLOAD_TYPE;
		LR_TDLS_FME_FIELD_FILL(1);

		/* 3. Frame Formation - (1) Category */
		*pPkt = TDLS_FRM_CATEGORY;
		LR_TDLS_FME_FIELD_FILL(1);

		/* 3. Frame Formation - (2) Action */
		*pPkt = TDLS_FRM_ACTION_TEARDOWN;
		LR_TDLS_FME_FIELD_FILL(1);

		/* 3. Frame Formation - (3) Reason Code */
		*pPkt = ucReasonCode;
		*(pPkt + 1) = 0x00;
		LR_TDLS_FME_FIELD_FILL(2);

		/* 3. Frame Formation - (16) Link identifier element */
		TDLS_LINK_IDENTIFIER_IE(pPkt)->ucId = ELEM_ID_LINK_IDENTIFIER;
		TDLS_LINK_IDENTIFIER_IE(pPkt)->ucLength = ELEM_LEN_LINK_IDENTIFIER;

		kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aBSSID, prBssInfo->aucBSSID, 6);
		if (fgIsInitiator == 1) {
			kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aInitiator, aucPeerMac, 6);
			kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aResponder, prAdapter->rMyMacAddr, 6);
		} else {
			kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aInitiator, prAdapter->rMyMacAddr, 6);
			kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aResponder, aucPeerMac, 6);
		}

		u4IeLen = IE_SIZE(pPkt);
		LR_TDLS_FME_FIELD_FILL(u4IeLen);

		/* 4. Update packet length */
		prMsduInfo->len = u4PktLen;
		dumpMemory8(prMsduInfo->data, u4PktLen);

		/* pass to OS */
		TdlsCmdTestRxIndicatePkts(prGlueInfo, prMsduInfo);
	} else {
		kalMemZero(&rCmd, sizeof(rCmd));
		kalMemCopy(rCmd.aucPeerMac, aucPeerMac, 6);
		rCmd.Content.rCmdTearDownRcv.u4ReasonCode = (UINT32) ucReasonCode;

		/* command to do this */
		rStatus = kalIoctl(prGlueInfo,
				   TdlsTestTearDownRecv, &rCmd, sizeof(rCmd), FALSE, FALSE, FALSE, FALSE, &u4BufLen);

		if (rStatus != WLAN_STATUS_SUCCESS) {
			DBGLOG(TDLS, ERROR, "%s kalIoctl fail:%x\n", __func__, rStatus);
			return;
		}
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to inform firmware to skip tx fail case.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_7_[Enable/Disable]

		iwpriv wlan0 set_str_cmd 0_7_1
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestTxFailSkip(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	WLAN_STATUS rStatus;
	TDLS_CMD_CORE_T rCmd;
	UINT_32 u4BufLen;

	/* parse arguments */
	kalMemZero(rCmd.aucPeerMac, sizeof(rCmd.aucPeerMac));
	rCmd.Content.rCmdTxFailSkip.fgIsEnable = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);

	DBGLOG(TDLS, INFO, "%s: fgIsEnable = %d\n", __func__, rCmd.Content.rCmdTxFailSkip.fgIsEnable);

	/* command to do this */
	rStatus = kalIoctl(prGlueInfo, TdlsTestTxFailSkip, &rCmd, sizeof(rCmd), FALSE, FALSE, FALSE, FALSE, &u4BufLen);

	if (rStatus != WLAN_STATUS_SUCCESS) {
		DBGLOG(TDLS, ERROR, "%s kalIoctl fail:%x\n", __func__, rStatus);
		return;
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to send a test frame command to wifi task.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_12_0_[FrameType]_[DialogToken]_[Peer MAC]

		iwpriv wlan0 set_str_cmd 0_12_0_0_1_00:11:22:33:44:01
*
*		EX: iwpriv wlan0 set_str_cmd 0_12_2_[FrameType]_[DialogToken]_[Peer MAC]

		iwpriv wlan0 set_str_cmd 0_12_2_0_1_00:11:22:33:44:01
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestTxTdlsFrame(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	PARAM_CUSTOM_TDLS_CMD_STRUCT_T rCmd;
	UINT32 u4Subcmd;
	UINT_32 u4BufLen;

	/* parse sub-command */
	u4Subcmd = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	DBGLOG(TDLS, INFO, "<tdls_cmd> test tx tdls frame sub command = %u\n", u4Subcmd);

	/* parse command arguments */
	rCmd.ucFmeType = CmdStringDecParse(prInBuf, &prInBuf, &u4BufLen);

	switch (u4Subcmd) {
	case TDLS_FRM_ACTION_SETUP_REQ:
	case TDLS_FRM_ACTION_SETUP_RSP:
	case TDLS_FRM_ACTION_CONFIRM:
		rCmd.ucToken = CmdStringDecParse(prInBuf, &prInBuf, &u4BufLen);
		CmdStringMacParse(prInBuf, &prInBuf, &u4InBufLen, rCmd.arRspAddr);

		DBGLOG(TDLS, INFO, "<tdls_cmd> setup FmeType=%d Token=%d to [%pM]\n",
				   rCmd.ucFmeType, rCmd.ucToken, rCmd.arRspAddr);
		break;

	default:
		DBGLOG(TDLS, ERROR, "<tdls_cmd> wrong test tx frame sub command\n");
		return;
	}

	/* send command to wifi task to handle */
	kalIoctl(prGlueInfo,
		 TdlsTestTdlsFrameSend,
		 (PVOID)&rCmd, sizeof(PARAM_CUSTOM_TDLS_CMD_STRUCT_T), FALSE, FALSE, TRUE, FALSE, &u4BufLen);
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to send a test frame command to wifi task.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_0_0_[FrameType]_[DialogToken]_[Cap]_[ExCap]_
		[SupRate0]_[SupRate1]_[SupRate2]_[SupRate3]_
		[SupChan0]_[SupChan1]_[SupChan2]_[SupChan3]_
		[Timeout]_[Peer MAC]

		iwpriv wlan0 set_str_cmd 0_0_0_0_1_1_7_0_0_0_0_0_0_0_0_300_00:11:22:33:44:01
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestTxFrame(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	PARAM_CUSTOM_TDLS_CMD_STRUCT_T rCmd;
	TDLS_STATUS u4Status;
	UINT_32 u4Subcmd;
	UINT_32 u4BufLen;

	/* parse sub-command */
	u4Subcmd = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	DBGLOG(TDLS, INFO, "<tdls_cmd> test tx frame sub command = %u\n", (UINT32) u4Subcmd);

	/* parse command arguments */
	switch (u4Subcmd) {
	case TDLS_FRM_ACTION_SETUP_REQ:
		u4Status = TdlsCmdTestTxFmeSetupReqBufTranslate(prInBuf, u4InBufLen, &rCmd);
		break;

	default:
		DBGLOG(TDLS, ERROR, "<tdls_cmd> wrong test tx frame sub command\n");
		return;
	}

	if (u4Status != TDLS_STATUS_SUCCESS) {
		DBGLOG(TDLS, ERROR, "<tdls_cmd> command parse fail\n");
		return;
	}

	/* send command to wifi task to handle */
	kalIoctl(prGlueInfo,
		 TdlsTestFrameSend,
		 (PVOID)&rCmd, sizeof(PARAM_CUSTOM_TDLS_CMD_STRUCT_T), FALSE, FALSE, TRUE, FALSE, &u4BufLen);
}

/*----------------------------------------------------------------------------*/
/*!
* @brief Parse the TDLS test frame command, setup request
*
* @param CmdBuf		Pointer to the buffer.
* @param BufLen		Record buffer length.
* @param CmdTspec	Pointer to the structure.
*
* @retval WLAN_STATUS_SUCCESS: Translate OK.
* @retval WLAN_STATUS_FAILURE: Translate fail.
* @usage iwpriv wlan0 set_str_cmd [tdls]_[command]
*
*		EX: iwpriv wlan0 set_str_cmd 0_0_0_[FrameType]_[DialogToken]_[Cap]_[ExCap]_
		[SupRate0]_[SupRate1]_[SupRate2]_[SupRate3]_
		[SupChan0]_[SupChan1]_[SupChan2]_[SupChan3]_
		[Timeout]_[Peer MAC]

		iwpriv wlan0 set_str_cmd 0_0_0_0_1_1_7_0_0_0_0_0_0_0_0_300_00:11:22:33:44:01
*/
/*----------------------------------------------------------------------------*/
static TDLS_STATUS
TdlsCmdTestTxFmeSetupReqBufTranslate(UINT_8 *pCmdBuf, UINT_32 u4BufLen, PARAM_CUSTOM_TDLS_CMD_STRUCT_T *prCmd)
{
/* dumpMemory8(ANDROID_LOG_INFO, pCmdBuf, u4BufLen); */

	prCmd->ucFmeType = CmdStringDecParse(pCmdBuf, &pCmdBuf, &u4BufLen);
	prCmd->ucToken = CmdStringDecParse(pCmdBuf, &pCmdBuf, &u4BufLen);
	prCmd->u2Cap = CmdStringDecParse(pCmdBuf, &pCmdBuf, &u4BufLen);
	prCmd->ucExCap = CmdStringDecParse(pCmdBuf, &pCmdBuf, &u4BufLen);
	prCmd->arSupRate[0] = CmdStringDecParse(pCmdBuf, &pCmdBuf, &u4BufLen);
	prCmd->arSupRate[1] = CmdStringDecParse(pCmdBuf, &pCmdBuf, &u4BufLen);
	prCmd->arSupRate[2] = CmdStringDecParse(pCmdBuf, &pCmdBuf, &u4BufLen);
	prCmd->arSupRate[3] = CmdStringDecParse(pCmdBuf, &pCmdBuf, &u4BufLen);
	prCmd->arSupChan[0] = CmdStringDecParse(pCmdBuf, &pCmdBuf, &u4BufLen);
	prCmd->arSupChan[1] = CmdStringDecParse(pCmdBuf, &pCmdBuf, &u4BufLen);
	prCmd->arSupChan[2] = CmdStringDecParse(pCmdBuf, &pCmdBuf, &u4BufLen);
	prCmd->arSupChan[3] = CmdStringDecParse(pCmdBuf, &pCmdBuf, &u4BufLen);
	prCmd->u4Timeout = CmdStringDecParse(pCmdBuf, &pCmdBuf, &u4BufLen);
	CmdStringMacParse(pCmdBuf, &pCmdBuf, &u4BufLen, prCmd->arRspAddr);

	DBGLOG(TDLS, INFO, "<tdls_cmd> command content =\n");
	DBGLOG(TDLS, INFO, "\tPeer MAC = %pM\n", (prCmd->arRspAddr));
	DBGLOG(TDLS, INFO, "\tToken = %u, Cap = 0x%x, ExCap = 0x%x, Timeout = %us FrameType = %u\n",
			    (UINT32) prCmd->ucToken, prCmd->u2Cap, prCmd->ucExCap,
			    (UINT32) prCmd->u4Timeout, (UINT32) prCmd->ucFmeType);
	DBGLOG(TDLS, INFO, "\tSupRate = 0x%x %x %x %x\n",
			    prCmd->arSupRate[0], prCmd->arSupRate[1], prCmd->arSupRate[2], prCmd->arSupRate[3]);
	DBGLOG(TDLS, INFO, "\tSupChan = %d %d %d %d\n",
			    prCmd->arSupChan[0], prCmd->arSupChan[1], prCmd->arSupChan[2], prCmd->arSupChan[3]);

	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to update a TDLS peer.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_3_[Responder MAC]

		iwpriv wlan0 set_str_cmd 0_3_00:11:22:33:44:01
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestUpdatePeer(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	PARAM_CUSTOM_TDLS_CMD_STRUCT_T rCmd;
	struct wireless_dev *prWdev;

	/* reset */
	kalMemZero(&rCmd, sizeof(rCmd));

	/* parse arguments */
	CmdStringMacParse(prInBuf, &prInBuf, &u4InBufLen, rCmd.arRspAddr);

	/* init */
	rCmd.rPeerInfo.supported_rates = rCmd.arSupRate;
	rCmd.rPeerInfo.ht_capa = &rCmd.rHtCapa;
	rCmd.rPeerInfo.vht_capa = &rCmd.rVhtCapa; /* LINUX_KERNEL_VERSION >= 3.10.0 */
	rCmd.rPeerInfo.sta_flags_set = BIT(NL80211_STA_FLAG_TDLS_PEER);
	rCmd.rPeerInfo.uapsd_queues = 0xf;	/* all AC */
	rCmd.rPeerInfo.max_sp = 0;	/* delivery all packets */

	/* send command to wifi task to handle */
	prWdev = prGlueInfo->prDevHandler->ieee80211_ptr;
	mtk_cfg80211_add_station(prWdev->wiphy, (void *)0x1, rCmd.arRspAddr, &rCmd.rPeerInfo);

	/* update */
	TdlsexCfg80211TdlsOper(prWdev->wiphy, (void *)0x1, rCmd.arRspAddr, NL80211_TDLS_ENABLE_LINK);
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to receive a Null frame from the peer.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*		EX: iwpriv wlan0 set_str_cmd 0_5_[Responder MAC]_[PM bit]

		iwpriv wlan0 set_str_cmd 0_5_00:11:22:33:44:01_1
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdTestNullRecv(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	WLAN_STATUS rStatus;
	TDLS_CMD_CORE_T rCmd;
	UINT_32 u4BufLen;

	/* parse arguments */
	CmdStringMacParse(prInBuf, &prInBuf, &u4InBufLen, rCmd.aucPeerMac);
	rCmd.Content.rCmdNullRcv.u4PM = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);

	DBGLOG(TDLS, INFO, "%s: [%pM] u4PM = %u\n",
			    __func__, (rCmd.aucPeerMac), (UINT32) rCmd.Content.rCmdNullRcv.u4PM);

	/* command to do this */
	rStatus = kalIoctl(prGlueInfo, TdlsTestNullRecv, &rCmd, sizeof(rCmd), FALSE, FALSE, FALSE, FALSE, &u4BufLen);

	if (rStatus != WLAN_STATUS_SUCCESS) {
		DBGLOG(TDLS, ERROR, "%s kalIoctl fail:%x\n", __func__, rStatus);
		return;
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to send a data frame to the peer periodically.
*
* \param[in] prAdapter		Pointer to the Adapter structure
* \param[in] u4Param		no use
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_15_[Responder MAC]_[Interval: ms]_[Enable/Disable]

		iwpriv wlan0 set_str_cmd 0_15_00:11:22:33:44:01_5000_1
*/
/*----------------------------------------------------------------------------*/
static VOID TdlsTimerTestDataContSend(ADAPTER_T *prAdapter, UINT_32 u4Param)
{
	GLUE_INFO_T *prGlueInfo;
	struct sk_buff *prMsduInfo;
	UINT_8 *prPkt;

	/* init */
	prGlueInfo = prAdapter->prGlueInfo;

	/* allocate a data frame */
	prMsduInfo = kalPacketAlloc(prGlueInfo, 1000, &prPkt);
	if (prMsduInfo == NULL) {
		DBGLOG(TDLS, ERROR, "<tdls_cmd> %s allocate pkt fail!\n", __func__);
		return;
	}

	/* init dev */
	prMsduInfo->dev = prGlueInfo->prDevHandler;
	if (prMsduInfo->dev == NULL) {
		DBGLOG(TDLS, ERROR, "<tdls_cmd> %s prMsduInfo->dev == NULL!\n", __func__);
		kalPacketFree(prGlueInfo, prMsduInfo);
		return;
	}

	/* init packet */
	prMsduInfo->len = 1000;
	kalMemCopy(prMsduInfo->data, aucTdlsTestDataSPeerMac, 6);
	kalMemCopy(prMsduInfo->data + 6, prAdapter->rMyMacAddr, 6);
	*(UINT_16 *) (prMsduInfo->data + 12) = 0x0800;

	DBGLOG(TDLS, INFO, "<tdls_cmd> %s try to send a data frame to %pM\n",
			    __func__, aucTdlsTestDataSPeerMac);

	/* simulate OS to send the packet */
	wlanHardStartXmit(prMsduInfo, prMsduInfo->dev);

	/* restart test timer */
	cnmTimerStartTimer(prAdapter, &rTdlsTimerTestDataSend, u2TdlsTestDataSInterval);
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to receive a Channel Switch Request frame.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*/
/*----------------------------------------------------------------------------*/
static TDLS_STATUS
TdlsTestChStReqRecv(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	TDLS_CMD_CORE_T *prCmdContent;
	WLAN_STATUS rStatus;

	/* init command buffer */
	prCmdContent = (TDLS_CMD_CORE_T *) pvSetBuffer;
	prCmdContent->u4Command = TDLS_CORE_CMD_TEST_CHSW_REQ;

	/* send the command */
	rStatus = wlanSendSetQueryCmd(prAdapter,	/* prAdapter */
				      CMD_ID_TDLS_CORE,	/* ucCID */
				      TRUE,	/* fgSetQuery */
				      FALSE,	/* fgNeedResp */
				      FALSE,	/* fgIsOid */
				      NULL, NULL,	/* pfCmdTimeoutHandler */
				      sizeof(TDLS_CMD_CORE_T),	/* u4SetQueryInfoLen */
				      (PUINT_8) prCmdContent,	/* pucInfoBuffer */
				      NULL,	/* pvSetQueryBuffer */
				      0	/* u4SetQueryBufferLen */
	    );

	if (rStatus != WLAN_STATUS_PENDING) {
		DBGLOG(TDLS, ERROR, "%s wlanSendSetQueryCmd allocation fail!\n", __func__);
		return TDLS_STATUS_RESOURCES;
	}

	DBGLOG(TDLS, INFO, "%s cmd ok.\n", __func__);
	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to receive a Channel Switch Response frame.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*/
/*----------------------------------------------------------------------------*/
static TDLS_STATUS
TdlsTestChStRspRecv(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	TDLS_CMD_CORE_T *prCmdContent;
	WLAN_STATUS rStatus;

	/* init command buffer */
	prCmdContent = (TDLS_CMD_CORE_T *) pvSetBuffer;
	prCmdContent->u4Command = TDLS_CORE_CMD_TEST_CHSW_RSP;

	/* send the command */
	rStatus = wlanSendSetQueryCmd(prAdapter,	/* prAdapter */
				      CMD_ID_TDLS_CORE,	/* ucCID */
				      TRUE,	/* fgSetQuery */
				      FALSE,	/* fgNeedResp */
				      FALSE,	/* fgIsOid */
				      NULL, NULL,	/* pfCmdTimeoutHandler */
				      sizeof(TDLS_CMD_CORE_T),	/* u4SetQueryInfoLen */
				      (PUINT_8) prCmdContent,	/* pucInfoBuffer */
				      NULL,	/* pvSetQueryBuffer */
				      0	/* u4SetQueryBufferLen */
	    );

	if (rStatus != WLAN_STATUS_PENDING) {
		DBGLOG(TDLS, ERROR, "%s wlanSendSetQueryCmd allocation fail!\n", __func__);
		return TDLS_STATUS_RESOURCES;
	}

	DBGLOG(TDLS, INFO, "%s cmd ok.\n", __func__);
	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to send a test frame.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*/
/*----------------------------------------------------------------------------*/
static TDLS_STATUS
TdlsTestFrameSend(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	GLUE_INFO_T *prGlueInfo;
	PARAM_CUSTOM_TDLS_CMD_STRUCT_T *prCmd;
	P_BSS_INFO_T prBssInfo;
	struct sk_buff *prMsduInfo;
	UINT_8 *pPkt;
	UINT_32 u4PktLen, u4IeLen;

	/* sanity check */
	ASSERT(prAdapter);
	ASSERT(pvSetBuffer);
	ASSERT(pu4SetInfoLen);

	DBGLOG(TDLS, INFO, "<tdls_fme> %s\n", __func__);

	if (u4SetBufferLen == 0)
		return TDLS_STATUS_INVALID_LENGTH;

	/* allocate/init packet */
	prGlueInfo = (GLUE_INFO_T *) prAdapter->prGlueInfo;
	prCmd = (PARAM_CUSTOM_TDLS_CMD_STRUCT_T *) pvSetBuffer;
	prBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX]);
	*pu4SetInfoLen = u4SetBufferLen;
	u4PktLen = 0;

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

	/* make up frame content */
	/* 1. 802.3 header */
	kalMemCopy(pPkt, prCmd->arRspAddr, TDLS_FME_MAC_ADDR_LEN);
	LR_TDLS_FME_FIELD_FILL(TDLS_FME_MAC_ADDR_LEN);
	kalMemCopy(pPkt, prAdapter->rMyMacAddr, TDLS_FME_MAC_ADDR_LEN);
	LR_TDLS_FME_FIELD_FILL(TDLS_FME_MAC_ADDR_LEN);
	*(UINT_16 *) pPkt = htons(TDLS_FRM_PROT_TYPE);
	LR_TDLS_FME_FIELD_FILL(2);

	/* 2. payload type */
	*pPkt = TDLS_FRM_PAYLOAD_TYPE;
	LR_TDLS_FME_FIELD_FILL(1);

	/* 3. Frame Formation - (1) Category */
	*pPkt = TDLS_FRM_CATEGORY;
	LR_TDLS_FME_FIELD_FILL(1);

	/* 3. Frame Formation - (2) Action */
	*pPkt = prCmd->ucFmeType;
	LR_TDLS_FME_FIELD_FILL(1);

	/* 3. Frame Formation - (3) Dialog token */
	*pPkt = prCmd->ucToken;
	LR_TDLS_FME_FIELD_FILL(1);

	/* 3. Frame Formation - (4) Capability */
	WLAN_SET_FIELD_16(pPkt, prCmd->u2Cap);
	LR_TDLS_FME_FIELD_FILL(2);

	/* 4. Append general IEs */
	u4IeLen = TdlsFrameGeneralIeAppend(prAdapter, NULL, 0, pPkt);
	LR_TDLS_FME_FIELD_FILL(u4IeLen);

	/* 3. Frame Formation - (10) Extended capabilities element */
	EXT_CAP_IE(pPkt)->ucId = ELEM_ID_EXTENDED_CAP;
	EXT_CAP_IE(pPkt)->ucLength = 5;

	EXT_CAP_IE(pPkt)->aucCapabilities[0] = 0x00;	/* bit0 ~ bit7 */
	EXT_CAP_IE(pPkt)->aucCapabilities[1] = 0x00;	/* bit8 ~ bit15 */
	EXT_CAP_IE(pPkt)->aucCapabilities[2] = 0x00;	/* bit16 ~ bit23 */
	EXT_CAP_IE(pPkt)->aucCapabilities[3] = 0x00;	/* bit24 ~ bit31 */
	EXT_CAP_IE(pPkt)->aucCapabilities[4] = 0x00;	/* bit32 ~ bit39 */

	if (prCmd->ucExCap & TDLS_EX_CAP_PEER_UAPSD)
		EXT_CAP_IE(pPkt)->aucCapabilities[3] |= BIT((28 - 24));
	if (prCmd->ucExCap & TDLS_EX_CAP_CHAN_SWITCH)
		EXT_CAP_IE(pPkt)->aucCapabilities[3] |= BIT((30 - 24));
	if (prCmd->ucExCap & TDLS_EX_CAP_TDLS)
		EXT_CAP_IE(pPkt)->aucCapabilities[4] |= BIT((37 - 32));

	u4IeLen = IE_SIZE(pPkt);
	LR_TDLS_FME_FIELD_FILL(u4IeLen);

	/* 3. Frame Formation - (12) Timeout interval element (TPK Key Lifetime) */
	TIMEOUT_INTERVAL_IE(pPkt)->ucId = ELEM_ID_TIMEOUT_INTERVAL;
	TIMEOUT_INTERVAL_IE(pPkt)->ucLength = 5;

	TIMEOUT_INTERVAL_IE(pPkt)->ucType = IE_TIMEOUT_INTERVAL_TYPE_KEY_LIFETIME;
	TIMEOUT_INTERVAL_IE(pPkt)->u4Value = htonl(prCmd->u4Timeout);

	u4IeLen = IE_SIZE(pPkt);
	LR_TDLS_FME_FIELD_FILL(u4IeLen);

	/* 3. Frame Formation - (16) Link identifier element */
	TDLS_LINK_IDENTIFIER_IE(pPkt)->ucId = ELEM_ID_LINK_IDENTIFIER;
	TDLS_LINK_IDENTIFIER_IE(pPkt)->ucLength = ELEM_LEN_LINK_IDENTIFIER;

	kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aBSSID, prBssInfo->aucBSSID, 6);
	kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aInitiator, prAdapter->rMyMacAddr, 6);
	kalMemCopy(TDLS_LINK_IDENTIFIER_IE(pPkt)->aResponder, prCmd->arRspAddr, 6);

	u4IeLen = IE_SIZE(pPkt);
	LR_TDLS_FME_FIELD_FILL(u4IeLen);

	/* 4. Update packet length */
	prMsduInfo->len = u4PktLen;
	dumpMemory8(prMsduInfo->data, u4PktLen);

	/* 5. send the data frame */
	wlanHardStartXmit(prMsduInfo, prMsduInfo->dev);
	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to receive a NULL frame.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*/
/*----------------------------------------------------------------------------*/
static TDLS_STATUS
TdlsTestNullRecv(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	TDLS_CMD_CORE_T *prCmdContent;
	WLAN_STATUS rStatus;

	/* init command buffer */
	prCmdContent = (TDLS_CMD_CORE_T *) pvSetBuffer;
	prCmdContent->u4Command = TDLS_CORE_CMD_TEST_NULL_RCV;

	/* send the command */
	rStatus = wlanSendSetQueryCmd(prAdapter,	/* prAdapter */
				      CMD_ID_TDLS_CORE,	/* ucCID */
				      TRUE,	/* fgSetQuery */
				      FALSE,	/* fgNeedResp */
				      FALSE,	/* fgIsOid */
				      NULL, NULL,	/* pfCmdTimeoutHandler */
				      sizeof(TDLS_CMD_CORE_T),	/* u4SetQueryInfoLen */
				      (PUINT_8) prCmdContent,	/* pucInfoBuffer */
				      NULL,	/* pvSetQueryBuffer */
				      0	/* u4SetQueryBufferLen */
	    );

	if (rStatus != WLAN_STATUS_PENDING) {
		DBGLOG(TDLS, ERROR, "%s wlanSendSetQueryCmd allocation fail!\n", __func__);
		return TDLS_STATUS_RESOURCES;
	}

	DBGLOG(TDLS, INFO, "%s cmd ok.\n", __func__);
	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to receive a PTI frame.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*/
/*----------------------------------------------------------------------------*/
static TDLS_STATUS
TdlsTestPtiReqRecv(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	TDLS_CMD_CORE_T *prCmdContent;
	WLAN_STATUS rStatus;

	/* init command buffer */
	prCmdContent = (TDLS_CMD_CORE_T *) pvSetBuffer;
	prCmdContent->u4Command = TDLS_CORE_CMD_TEST_PTI_REQ;

	/* send the command */
	rStatus = wlanSendSetQueryCmd(prAdapter,	/* prAdapter */
				      CMD_ID_TDLS_CORE,	/* ucCID */
				      TRUE,	/* fgSetQuery */
				      FALSE,	/* fgNeedResp */
				      FALSE,	/* fgIsOid */
				      NULL, NULL,	/* pfCmdTimeoutHandler */
				      sizeof(TDLS_CMD_CORE_T),	/* u4SetQueryInfoLen */
				      (PUINT_8) prCmdContent,	/* pucInfoBuffer */
				      NULL,	/* pvSetQueryBuffer */
				      0	/* u4SetQueryBufferLen */
	    );

	if (rStatus != WLAN_STATUS_PENDING) {
		DBGLOG(TDLS, ERROR, "%s wlanSendSetQueryCmd allocation fail!\n", __func__);
		return TDLS_STATUS_RESOURCES;
	}

	DBGLOG(TDLS, INFO, "%s cmd ok.\n", __func__);
	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to receive a PTI response frame.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*/
/*----------------------------------------------------------------------------*/
static TDLS_STATUS
TdlsTestPtiRspRecv(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	TDLS_CMD_CORE_T *prCmdContent;
	WLAN_STATUS rStatus;

	/* init command buffer */
	prCmdContent = (TDLS_CMD_CORE_T *) pvSetBuffer;
	prCmdContent->u4Command = TDLS_CORE_CMD_TEST_PTI_RSP;

	/* send the command */
	rStatus = wlanSendSetQueryCmd(prAdapter,	/* prAdapter */
				      CMD_ID_TDLS_CORE,	/* ucCID */
				      TRUE,	/* fgSetQuery */
				      FALSE,	/* fgNeedResp */
				      FALSE,	/* fgIsOid */
				      NULL, NULL,	/* pfCmdTimeoutHandler */
				      sizeof(TDLS_CMD_CORE_T),	/* u4SetQueryInfoLen */
				      (PUINT_8) prCmdContent,	/* pucInfoBuffer */
				      NULL,	/* pvSetQueryBuffer */
				      0	/* u4SetQueryBufferLen */
	    );

	if (rStatus != WLAN_STATUS_PENDING) {
		DBGLOG(TDLS, ERROR, "%s wlanSendSetQueryCmd allocation fail!\n", __func__);
		return TDLS_STATUS_RESOURCES;
	}

	DBGLOG(TDLS, INFO, "%s cmd ok.\n", __func__);
	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to receive a Tear Down frame.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*/
/*----------------------------------------------------------------------------*/
static TDLS_STATUS
TdlsTestTearDownRecv(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	TDLS_CMD_CORE_T *prCmdContent;
	WLAN_STATUS rStatus;

	/* init command buffer */
	prCmdContent = (TDLS_CMD_CORE_T *) pvSetBuffer;
	prCmdContent->u4Command = TDLS_CORE_CMD_TEST_TEAR_DOWN;

	/* send the command */
	rStatus = wlanSendSetQueryCmd(prAdapter,	/* prAdapter */
				      CMD_ID_TDLS_CORE,	/* ucCID */
				      TRUE,	/* fgSetQuery */
				      FALSE,	/* fgNeedResp */
				      FALSE,	/* fgIsOid */
				      NULL, NULL,	/* pfCmdTimeoutHandler */
				      sizeof(TDLS_CMD_CORE_T),	/* u4SetQueryInfoLen */
				      (PUINT_8) prCmdContent,	/* pucInfoBuffer */
				      NULL,	/* pvSetQueryBuffer */
				      0	/* u4SetQueryBufferLen */
	    );

	if (rStatus != WLAN_STATUS_PENDING) {
		DBGLOG(TDLS, ERROR, "%s wlanSendSetQueryCmd allocation fail!\n", __func__);
		return TDLS_STATUS_RESOURCES;
	}

	DBGLOG(TDLS, INFO, "%s cmd ok.\n", __func__);
	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to receive a data frame.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*/
/*----------------------------------------------------------------------------*/
static TDLS_STATUS
TdlsTestDataRecv(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	TDLS_CMD_CORE_T *prCmdContent;
	WLAN_STATUS rStatus;

	/* init command buffer */
	prCmdContent = (TDLS_CMD_CORE_T *) pvSetBuffer;
	prCmdContent->u4Command = TDLS_CORE_CMD_TEST_DATA_RCV;

	/* send the command */
	rStatus = wlanSendSetQueryCmd(prAdapter,	/* prAdapter */
				      CMD_ID_TDLS_CORE,	/* ucCID */
				      TRUE,	/* fgSetQuery */
				      FALSE,	/* fgNeedResp */
				      FALSE,	/* fgIsOid */
				      NULL, NULL,	/* pfCmdTimeoutHandler */
				      sizeof(TDLS_CMD_CORE_T),	/* u4SetQueryInfoLen */
				      (PUINT_8) prCmdContent,	/* pucInfoBuffer */
				      NULL,	/* pvSetQueryBuffer */
				      0	/* u4SetQueryBufferLen */
	    );

	if (rStatus != WLAN_STATUS_PENDING) {
		DBGLOG(TDLS, ERROR, "%s wlanSendSetQueryCmd allocation fail!\n", __func__);
		return TDLS_STATUS_RESOURCES;
	}

	DBGLOG(TDLS, INFO, "%s cmd ok.\n", __func__);
	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to skip PTI tx fail status.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*/
/*----------------------------------------------------------------------------*/
static TDLS_STATUS
TdlsTestPtiTxFail(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	TDLS_CMD_CORE_T *prCmdContent;
	WLAN_STATUS rStatus;

	/* init command buffer */
	prCmdContent = (TDLS_CMD_CORE_T *) pvSetBuffer;
	prCmdContent->u4Command = TDLS_CORE_CMD_TEST_PTI_TX_FAIL;

	/* send the command */
	rStatus = wlanSendSetQueryCmd(prAdapter,	/* prAdapter */
				      CMD_ID_TDLS_CORE,	/* ucCID */
				      TRUE,	/* fgSetQuery */
				      FALSE,	/* fgNeedResp */
				      FALSE,	/* fgIsOid */
				      NULL, NULL,	/* pfCmdTimeoutHandler */
				      sizeof(TDLS_CMD_CORE_T),	/* u4SetQueryInfoLen */
				      (PUINT_8) prCmdContent,	/* pucInfoBuffer */
				      NULL,	/* pvSetQueryBuffer */
				      0	/* u4SetQueryBufferLen */
	    );

	if (rStatus != WLAN_STATUS_PENDING) {
		DBGLOG(TDLS, ERROR, "%s wlanSendSetQueryCmd allocation fail!\n", __func__);
		return TDLS_STATUS_RESOURCES;
	}

	DBGLOG(TDLS, INFO, "%s cmd ok.\n", __func__);
	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to send a TDLS action frame.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*		EX: iwpriv wlan0 set_str_cmd 0_12_0_[FrameType]_[DialogToken]_[Peer MAC]

		iwpriv wlan0 set_str_cmd 0_12_0_0_1_00:11:22:33:44:01
*/
/*----------------------------------------------------------------------------*/
static TDLS_STATUS
TdlsTestTdlsFrameSend(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	GLUE_INFO_T *prGlueInfo;
	PARAM_CUSTOM_TDLS_CMD_STRUCT_T *prCmd;
	struct wireless_dev *prWdev;

	/* sanity check */
	ASSERT(prAdapter);
	ASSERT(pvSetBuffer);
	ASSERT(pu4SetInfoLen);

	DBGLOG(TDLS, INFO, "<tdls_fme> %s\n", __func__);

	if (u4SetBufferLen == 0)
		return TDLS_STATUS_INVALID_LENGTH;

	/* allocate/init packet */
	prGlueInfo = (GLUE_INFO_T *) prAdapter->prGlueInfo;
	prCmd = (PARAM_CUSTOM_TDLS_CMD_STRUCT_T *) pvSetBuffer;
	prWdev = (struct wireless_dev *)prGlueInfo->prDevHandler->ieee80211_ptr;

	TdlsexCfg80211TdlsMgmt(prWdev->wiphy, NULL,
			prCmd->arRspAddr, prCmd->ucFmeType, 1,
			0, 0, /* open/none */
			FALSE, NULL, 0);

	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to skip tx fail status. So always success in tx done in firmware.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*/
/*----------------------------------------------------------------------------*/
static TDLS_STATUS
TdlsTestTxFailSkip(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	TDLS_CMD_CORE_T *prCmdContent;
	WLAN_STATUS rStatus;

	/* init command buffer */
	prCmdContent = (TDLS_CMD_CORE_T *) pvSetBuffer;
	prCmdContent->u4Command = TDLS_CORE_CMD_TEST_TX_FAIL_SKIP;

	/* send the command */
	rStatus = wlanSendSetQueryCmd(prAdapter,	/* prAdapter */
				      CMD_ID_TDLS_CORE,	/* ucCID */
				      TRUE,	/* fgSetQuery */
				      FALSE,	/* fgNeedResp */
				      FALSE,	/* fgIsOid */
				      NULL, NULL,	/* pfCmdTimeoutHandler */
				      sizeof(TDLS_CMD_CORE_T),	/* u4SetQueryInfoLen */
				      (PUINT_8) prCmdContent,	/* pucInfoBuffer */
				      NULL,	/* pvSetQueryBuffer */
				      0	/* u4SetQueryBufferLen */
	    );

	if (rStatus != WLAN_STATUS_PENDING) {
		DBGLOG(TDLS, ERROR, "%s wlanSendSetQueryCmd allocation fail!\n", __func__);
		return TDLS_STATUS_RESOURCES;
	}

	DBGLOG(TDLS, INFO, "%s cmd ok.\n", __func__);
	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to skip to do keep alive function in firmware.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*/
/*----------------------------------------------------------------------------*/
static TDLS_STATUS
TdlsTestKeepAliveSkip(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	TDLS_CMD_CORE_T *prCmdContent;
	WLAN_STATUS rStatus;

	/* init command buffer */
	prCmdContent = (TDLS_CMD_CORE_T *) pvSetBuffer;
	prCmdContent->u4Command = TDLS_CORE_CMD_TEST_KEEP_ALIVE_SKIP;

	/* send the command */
	rStatus = wlanSendSetQueryCmd(prAdapter,	/* prAdapter */
				      CMD_ID_TDLS_CORE,	/* ucCID */
				      TRUE,	/* fgSetQuery */
				      FALSE,	/* fgNeedResp */
				      FALSE,	/* fgIsOid */
				      NULL, NULL,	/* pfCmdTimeoutHandler */
				      sizeof(TDLS_CMD_CORE_T),	/* u4SetQueryInfoLen */
				      (PUINT_8) prCmdContent,	/* pucInfoBuffer */
				      NULL,	/* pvSetQueryBuffer */
				      0	/* u4SetQueryBufferLen */
	    );

	if (rStatus != WLAN_STATUS_PENDING) {
		DBGLOG(TDLS, ERROR, "%s wlanSendSetQueryCmd allocation fail!\n", __func__);
		return TDLS_STATUS_RESOURCES;
	}

	DBGLOG(TDLS, INFO, "%s cmd ok.\n", __func__);
	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to skip channel switch timeout.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*/
/*----------------------------------------------------------------------------*/
static TDLS_STATUS
TdlsTestChSwTimeoutSkip(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	TDLS_CMD_CORE_T *prCmdContent;
	WLAN_STATUS rStatus;

	/* init command buffer */
	prCmdContent = (TDLS_CMD_CORE_T *) pvSetBuffer;
	prCmdContent->u4Command = TDLS_CORE_CMD_TEST_CHSW_TIMEOUT_SKIP;

	/* send the command */
	rStatus = wlanSendSetQueryCmd(prAdapter,	/* prAdapter */
				      CMD_ID_TDLS_CORE,	/* ucCID */
				      TRUE,	/* fgSetQuery */
				      FALSE,	/* fgNeedResp */
				      FALSE,	/* fgIsOid */
				      NULL, NULL,	/* pfCmdTimeoutHandler */
				      sizeof(TDLS_CMD_CORE_T),	/* u4SetQueryInfoLen */
				      (PUINT_8) prCmdContent,	/* pucInfoBuffer */
				      NULL,	/* pvSetQueryBuffer */
				      0	/* u4SetQueryBufferLen */
	    );

	if (rStatus != WLAN_STATUS_PENDING) {
		DBGLOG(TDLS, ERROR, "%s wlanSendSetQueryCmd allocation fail!\n", __func__);
		return TDLS_STATUS_RESOURCES;
	}

	DBGLOG(TDLS, INFO, "%s cmd ok.\n", __func__);
	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to skip scan request.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*/
/*----------------------------------------------------------------------------*/
static TDLS_STATUS
TdlsTestScanSkip(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	TDLS_CMD_CORE_T *prCmdContent;
	WLAN_STATUS rStatus;

	/* init command buffer */
	prCmdContent = (TDLS_CMD_CORE_T *) pvSetBuffer;
	prCmdContent->u4Command = TDLS_CORE_CMD_TEST_SCAN_SKIP;

	/* send the command */
	rStatus = wlanSendSetQueryCmd(prAdapter,	/* prAdapter */
				      CMD_ID_TDLS_CORE,	/* ucCID */
				      TRUE,	/* fgSetQuery */
				      FALSE,	/* fgNeedResp */
				      FALSE,	/* fgIsOid */
				      NULL, NULL,	/* pfCmdTimeoutHandler */
				      sizeof(TDLS_CMD_CORE_T),	/* u4SetQueryInfoLen */
				      (PUINT_8) prCmdContent,	/* pucInfoBuffer */
				      NULL,	/* pvSetQueryBuffer */
				      0	/* u4SetQueryBufferLen */
	    );

	if (rStatus != WLAN_STATUS_PENDING) {
		DBGLOG(TDLS, ERROR, "%s wlanSendSetQueryCmd allocation fail!\n", __func__);
		return TDLS_STATUS_RESOURCES;
	}

	DBGLOG(TDLS, INFO, "%s cmd ok.\n", __func__);
	return TDLS_STATUS_SUCCESS;
}

#endif /* TDLS_CFG_CMD_TEST */

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to configure channel switch parameters.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*/
/*----------------------------------------------------------------------------*/
static TDLS_STATUS
TdlsChSwConf(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	TDLS_CMD_CORE_T *prCmdContent;
	WLAN_STATUS rStatus;

	/* init command buffer */
	prCmdContent = (TDLS_CMD_CORE_T *) pvSetBuffer;
	prCmdContent->u4Command = TDLS_CORE_CMD_CHSW_CONF;

	/* send the command */
	rStatus = wlanSendSetQueryCmd(prAdapter,	/* prAdapter */
				      CMD_ID_TDLS_CORE,	/* ucCID */
				      TRUE,	/* fgSetQuery */
				      FALSE,	/* fgNeedResp */
				      FALSE,	/* fgIsOid */
				      NULL, NULL,	/* pfCmdTimeoutHandler */
				      sizeof(TDLS_CMD_CORE_T),	/* u4SetQueryInfoLen */
				      (PUINT_8) prCmdContent,	/* pucInfoBuffer */
				      NULL,	/* pvSetQueryBuffer */
				      0	/* u4SetQueryBufferLen */
	    );

	if (rStatus != WLAN_STATUS_PENDING) {
		DBGLOG(TDLS, ERROR, "%s wlanSendSetQueryCmd allocation fail!\n", __func__);
		return TDLS_STATUS_RESOURCES;
	}

	DBGLOG(TDLS, INFO, "%s cmd ok.\n", __func__);
	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to update channel switch parameters.
*
* \param[in] prGlueInfo Pointer to the Adapter structure
* \param[in] prInBuf A pointer to the command string buffer
* \param[in] u4InBufLen The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_9_[TDLS Peer MAC]_
			[NetworkTypeIndex]_[1 (Enable) or (0) Disable]_[1 (Start) or 0 (Stop)]_
			[RegClass]_[Chan]_[SecChanOff]_[1 (Reqular) or (0) One Shot]

		RegulatoryClass: TODO (reference to Annex I of 802.11n spec.)
		Secondary Channel Offset:	0 (SCN - no secondary channel)
								1 (SCA - secondary channel above)
								2 (SCB - secondary channel below)
		SwitchTime: units of microseconds

		iwpriv wlan0 set_str_cmd 0_9_00:11:22:33:44:01_0_1_0_0_1_0_0
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdChSwConf(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	WLAN_STATUS rStatus;
	TDLS_CMD_CORE_T rCmd;
	UINT_32 u4BufLen;

	/* parse arguments */
	CmdStringMacParse(prInBuf, &prInBuf, &u4InBufLen, rCmd.aucPeerMac);

	rCmd.Content.rCmdChSwConf.ucNetTypeIndex = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdChSwConf.fgIsChSwEnabled = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdChSwConf.fgIsChSwStarted = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdChSwConf.ucRegClass = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdChSwConf.ucTargetChan = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdChSwConf.ucSecChanOff = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdChSwConf.fgIsChSwRegular = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);

	DBGLOG(TDLS, INFO, "%s: %pM ucNetTypeIndex=%d, fgIsChSwEnabled=%d, fgIsChSwStarted=%d",
		__func__, (rCmd.aucPeerMac),
		rCmd.Content.rCmdChSwConf.ucNetTypeIndex,
		rCmd.Content.rCmdChSwConf.fgIsChSwEnabled,
		rCmd.Content.rCmdChSwConf.fgIsChSwStarted);
	DBGLOG(TDLS, INFO, " RegClass=%d, TargetChan=%d, SecChanOff=%d, Regular=%d\n",
		rCmd.Content.rCmdChSwConf.ucRegClass,
		rCmd.Content.rCmdChSwConf.ucTargetChan,
		rCmd.Content.rCmdChSwConf.ucSecChanOff, rCmd.Content.rCmdChSwConf.fgIsChSwRegular);

	/* command to do this */
	rStatus = kalIoctl(prGlueInfo, TdlsChSwConf, &rCmd, sizeof(rCmd), FALSE, FALSE, FALSE, FALSE, &u4BufLen);

	if (rStatus != WLAN_STATUS_SUCCESS) {
		DBGLOG(TDLS, ERROR, "%s kalIoctl fail:%x\n", __func__, rStatus);
		return;
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to display TDLS related information.
*
* \param[in] prGlueInfo Pointer to the Adapter structure
* \param[in] prInBuf A pointer to the command string buffer
* \param[in] u4InBufLen The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_18_[Peer MAC]_[Network Interface ID]_[IsClear]

		Network Interface ID: reference to ENUM_NETWORK_TYPE_INDEX_T

		typedef enum _ENUM_NETWORK_TYPE_INDEX_T {
			NETWORK_TYPE_AIS_INDEX = 0,
			NETWORK_TYPE_P2P_INDEX,
			NETWORK_TYPE_BOW_INDEX,
			NETWORK_TYPE_INDEX_NUM
		} ENUM_NETWORK_TYPE_INDEX_T;

		iwpriv wlan0 set_str_cmd 0_18_00:00:00:00:00:00_0_0
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdInfoDisplay(GLUE_INFO_T *prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	WLAN_STATUS rStatus;
	TDLS_CMD_CORE_T rCmd;
	UINT_32 u4BufLen;

	/* parse arguments */
	kalMemZero(&rCmd, sizeof(rCmd));

	CmdStringMacParse(prInBuf, &prInBuf, &u4InBufLen, rCmd.aucPeerMac);
	rCmd.ucNetTypeIndex = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdInfoDisplay.fgIsToClearAllHistory = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);

	DBGLOG(TDLS, INFO, "<tdls_cmd> %s: Command PeerMac=%pM in BSS%u\n",
			    __func__, (rCmd.aucPeerMac), rCmd.ucNetTypeIndex);

	/* command to do this */
	rStatus = kalIoctl(prGlueInfo, TdlsInfoDisplay, &rCmd, sizeof(rCmd), FALSE, FALSE, FALSE, FALSE, &u4BufLen);

	if (rStatus != WLAN_STATUS_SUCCESS) {
		DBGLOG(TDLS, ERROR, "%s kalIoctl fail:%x\n", __func__, rStatus);
		return;
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to display key related information.
*
* \param[in] prGlueInfo Pointer to the Adapter structure
* \param[in] prInBuf A pointer to the command string buffer
* \param[in] u4InBufLen The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_20

		iwpriv wlan0 set_str_cmd 0_20
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdKeyInfoDisplay(GLUE_INFO_T *prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	WLAN_STATUS rStatus;
	TDLS_CMD_CORE_T rCmd;
	UINT_32 u4BufLen;

	/* parse arguments */
	kalMemZero(&rCmd, sizeof(rCmd));

	DBGLOG(TDLS, INFO, "<tdls_cmd> %s\n", __func__);

	/* command to do this */
	rStatus = kalIoctl(prGlueInfo, TdlsKeyInfoDisplay, &rCmd, sizeof(rCmd), FALSE, FALSE, FALSE, FALSE, &u4BufLen);

	if (rStatus != WLAN_STATUS_SUCCESS) {
		DBGLOG(TDLS, ERROR, "%s kalIoctl fail:%x\n", __func__, rStatus);
		return;
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to update MIB parameters.
*
* \param[in] prGlueInfo Pointer to the Adapter structure
* \param[in] prInBuf A pointer to the command string buffer
* \param[in] u4InBufLen The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_6_[TdlsEn]_[UapsdEn]_[PsmEn]_[PtiWin]_[CWCap]_
			[AckMisRetry]_[RspTimeout]_[CWPbDelay]_[DRWin]_[LowestAcInt]

		iwpriv wlan0 set_str_cmd 0_6_1_1_0_1_1_3_5_1000_2_1

		reference to TDLS_CMD_CORE_MIB_PARAM_UPDATE_T
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdMibParamUpdate(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	WLAN_STATUS rStatus;
	TDLS_CMD_CORE_T rCmd;
	UINT_32 u4BufLen;

	/* reset */
	kalMemZero(&rCmd, sizeof(rCmd));

	/* parse arguments */
	rCmd.Content.rCmdMibUpdate.Tdlsdot11TunneledDirectLinkSetupImplemented =
	    CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdMibUpdate.Tdlsdot11TDLSPeerUAPSDBufferSTAActivated =
	    CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdMibUpdate.Tdlsdot11TDLSPeerPSMActivated = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdMibUpdate.Tdlsdot11TDLSPeerUAPSDIndicationWindow =
	    CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdMibUpdate.Tdlsdot11TDLSChannelSwitchingActivated =
	    CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdMibUpdate.Tdlsdot11TDLSPeerSTAMissingAckRetryLimit =
	    CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdMibUpdate.Tdlsdot11TDLSResponseTimeout = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdMibUpdate.Tdlsdot11TDLSProbeDelay = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdMibUpdate.Tdlsdot11TDLSDiscoveryRequestWindow =
	    CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdMibUpdate.Tdlsdot11TDLSACDeterminationInterval =
	    CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);

	DBGLOG(TDLS, INFO, "<tdls_cmd> MIB param = %d %d %d %d %d %d %d %d %d %d\n",
			    rCmd.Content.rCmdMibUpdate.Tdlsdot11TunneledDirectLinkSetupImplemented,
			    rCmd.Content.rCmdMibUpdate.Tdlsdot11TDLSPeerUAPSDBufferSTAActivated,
			    rCmd.Content.rCmdMibUpdate.Tdlsdot11TDLSPeerPSMActivated,
			    rCmd.Content.rCmdMibUpdate.Tdlsdot11TDLSPeerUAPSDIndicationWindow,
			    rCmd.Content.rCmdMibUpdate.Tdlsdot11TDLSChannelSwitchingActivated,
			    rCmd.Content.rCmdMibUpdate.Tdlsdot11TDLSPeerSTAMissingAckRetryLimit,
			    rCmd.Content.rCmdMibUpdate.Tdlsdot11TDLSResponseTimeout,
			    rCmd.Content.rCmdMibUpdate.Tdlsdot11TDLSProbeDelay,
			    rCmd.Content.rCmdMibUpdate.Tdlsdot11TDLSDiscoveryRequestWindow,
			    rCmd.Content.rCmdMibUpdate.Tdlsdot11TDLSACDeterminationInterval);

	/* command to do this */
	rStatus = kalIoctl(prGlueInfo, TdlsMibParamUpdate, &rCmd, sizeof(rCmd), FALSE, FALSE, FALSE, FALSE, &u4BufLen);

	if (rStatus != WLAN_STATUS_SUCCESS) {
		DBGLOG(TDLS, ERROR, "%s kalIoctl fail:%x\n", __func__, rStatus);
		return;
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to update setup parameters.
*
* \param[in] prGlueInfo Pointer to the Adapter structure
* \param[in] prInBuf A pointer to the command string buffer
* \param[in] u4InBufLen The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_17_[20/40 Support]

		iwpriv wlan0 set_str_cmd 0_17_1
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdSetupConf(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	WLAN_STATUS rStatus;
	TDLS_CMD_CORE_T rCmd;
	UINT_32 u4BufLen;

	/* parse arguments */
	kalMemZero(rCmd.aucPeerMac, sizeof(rCmd.aucPeerMac));

	rCmd.Content.rCmdSetupConf.fgIs2040Supported = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);

	DBGLOG(TDLS, INFO, "%s: rCmdSetupConf=%d\n", __func__, rCmd.Content.rCmdSetupConf.fgIs2040Supported);

	/* command to do this */
	prGlueInfo->rTdlsLink.fgIs2040Sup = rCmd.Content.rCmdSetupConf.fgIs2040Supported;

	rStatus = kalIoctl(prGlueInfo, TdlsSetupConf, &rCmd, sizeof(rCmd), FALSE, FALSE, FALSE, FALSE, &u4BufLen);

	if (rStatus != WLAN_STATUS_SUCCESS) {
		DBGLOG(TDLS, ERROR, "%s kalIoctl fail:%x\n", __func__, rStatus);
		return;
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to update UAPSD parameters.
*
* \param[in] prGlueInfo Pointer to the Adapter structure
* \param[in] prInBuf A pointer to the command string buffer
* \param[in] u4InBufLen The length of the buffer
* \param[out] None
*
* \retval None
*
*		EX: iwpriv wlan0 set_str_cmd 0_8_[SP timeout skip]_[PTI timeout skip]

		iwpriv wlan0 set_str_cmd 0_8_1_1
*/
/*----------------------------------------------------------------------------*/
static void TdlsCmdUapsdConf(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	WLAN_STATUS rStatus;
	TDLS_CMD_CORE_T rCmd;
	UINT_32 u4BufLen;

	/* parse arguments */
	kalMemZero(rCmd.aucPeerMac, sizeof(rCmd.aucPeerMac));

	/* UAPSD Service Period */
	rCmd.Content.rCmdUapsdConf.fgIsSpTimeoutSkip = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	rCmd.Content.rCmdUapsdConf.fgIsPtiTimeoutSkip = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	/* PTI Service Period */
	fgIsPtiTimeoutSkip = rCmd.Content.rCmdUapsdConf.fgIsPtiTimeoutSkip;

	DBGLOG(TDLS, INFO, "%s: fgIsSpTimeoutSkip=%d, fgIsPtiTimeoutSkip=%d\n",
			    __func__, rCmd.Content.rCmdUapsdConf.fgIsSpTimeoutSkip, fgIsPtiTimeoutSkip);

	/* command to do this */
	rStatus = kalIoctl(prGlueInfo, TdlsUapsdConf, &rCmd, sizeof(rCmd), FALSE, FALSE, FALSE, FALSE, &u4BufLen);

	if (rStatus != WLAN_STATUS_SUCCESS) {
		DBGLOG(TDLS, ERROR, "%s kalIoctl fail:%x\n", __func__, rStatus);
		return;
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to display TDLS all information.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*	iwpriv wlan0 set_str_cmd 0_18_00:00:00:00:00:00_0_0
*
*/
/*----------------------------------------------------------------------------*/
static TDLS_STATUS
TdlsInfoDisplay(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	GLUE_INFO_T *prGlueInfo;
	TDLS_CMD_CORE_T *prCmdContent;
	STA_RECORD_T *prStaRec;
	TDLS_INFO_LINK_T *prLink;
	UINT32 u4StartIdx;
	UINT32 u4PeerNum;
	BOOLEAN fgIsListAll;
	UINT8 ucMacZero[6];
	UINT32 u4HisIdx;
	UINT8 ucNetTypeIndex;

	/* init */
	prGlueInfo = prAdapter->prGlueInfo;
	prCmdContent = (TDLS_CMD_CORE_T *) pvSetBuffer;
	u4StartIdx = 0;
	u4PeerNum = 1;
	fgIsListAll = TRUE;
	kalMemZero(ucMacZero, sizeof(ucMacZero));
	ucNetTypeIndex = NETWORK_TYPE_AIS_INDEX;

	/* display common information */
	DBGLOG(TDLS, TRACE, "TDLS common:\n");
	DBGLOG(TDLS, TRACE, "\t\trFreeSwRfbList=%u\n", (UINT32) prAdapter->rRxCtrl.rFreeSwRfbList.u4NumElem);
	DBGLOG(TDLS, TRACE, "\t\tjiffies=%u %ums (HZ=%d)\n", (UINT32) jiffies, (UINT32) kalGetTimeTick(), HZ);

	/* display disconnection history information */
	DBGLOG(TDLS, TRACE, "TDLS link history: %d\n", prGlueInfo->rTdlsLink.u4LinkIdx);

	for (u4HisIdx = prGlueInfo->rTdlsLink.u4LinkIdx + 1; u4HisIdx < TDLS_LINK_HISTORY_MAX; u4HisIdx++) {
		prLink = &prGlueInfo->rTdlsLink.rLinkHistory[u4HisIdx];

		if (kalMemCmp(prLink->aucPeerMac, ucMacZero, 6) == 0)
			continue;	/* skip all zero */

		DBGLOG(TDLS, TRACE,
			"\t\t%d. %pM jiffies start(%lu %ums)end(%lu %ums)Reason(%u)fromUs(%u)Dup(%u)HT(%u)\n",
			u4HisIdx, prLink->aucPeerMac,
			prLink->jiffies_start, jiffies_to_msecs(prLink->jiffies_start),
			prLink->jiffies_end, jiffies_to_msecs(prLink->jiffies_end),
			prLink->ucReasonCode,
			prLink->fgIsFromUs, prLink->ucDupCount, (prLink->ucHtCap & TDLS_INFO_LINK_HT_CAP_SUP));

		if (prLink->ucHtCap & TDLS_INFO_LINK_HT_CAP_SUP) {
			DBGLOG(TDLS, TRACE,
			       "\t\t\tBA (0x%x %x %x %x %x %x %x %x)\n",
				prLink->ucHtBa[0], prLink->ucHtBa[1],
				prLink->ucHtBa[2], prLink->ucHtBa[3],
				prLink->ucHtBa[4], prLink->ucHtBa[5], prLink->ucHtBa[6], prLink->ucHtBa[7]);
		}
	}
	for (u4HisIdx = 0; u4HisIdx <= prGlueInfo->rTdlsLink.u4LinkIdx; u4HisIdx++) {
		prLink = &prGlueInfo->rTdlsLink.rLinkHistory[u4HisIdx];

		if (kalMemCmp(prLink->aucPeerMac, ucMacZero, 6) == 0)
			continue;	/* skip all zero, use continue, not break */

		DBGLOG(TDLS, TRACE,
		       "\t\t%d. %pM jiffies start(%lu %ums)end(%lu %ums)Reason(%u)fromUs(%u)Dup(%u)HT(%u)\n",
			u4HisIdx, (prLink->aucPeerMac),
			prLink->jiffies_start, jiffies_to_msecs(prLink->jiffies_start),
			prLink->jiffies_end, jiffies_to_msecs(prLink->jiffies_end),
			prLink->ucReasonCode,
			prLink->fgIsFromUs, prLink->ucDupCount, (prLink->ucHtCap & TDLS_INFO_LINK_HT_CAP_SUP));

		if (prLink->ucHtCap & TDLS_INFO_LINK_HT_CAP_SUP) {
			DBGLOG(TDLS, TRACE,
			       "\t\t\tBA (0x%x %x %x %x %x %x %x %x)\n",
				prLink->ucHtBa[0], prLink->ucHtBa[1],
				prLink->ucHtBa[2], prLink->ucHtBa[3],
				prLink->ucHtBa[4], prLink->ucHtBa[5], prLink->ucHtBa[6], prLink->ucHtBa[7]);
		}
	}
	DBGLOG(TDLS, TRACE, "\n");

	/* display link information */
	if (prCmdContent != NULL) {
		if (kalMemCmp(prCmdContent->aucPeerMac, ucMacZero, 6) != 0) {
			prStaRec = cnmGetStaRecByAddress(prAdapter,
							 prCmdContent->ucNetTypeIndex, prCmdContent->aucPeerMac);
			if (prStaRec == NULL)
				fgIsListAll = TRUE;
		}

		ucNetTypeIndex = prCmdContent->ucNetTypeIndex;
	}

	while (1) {
		if (fgIsListAll == TRUE) {
			/* list all TDLS peers */
			prStaRec = cnmStaTheTypeGet(prAdapter, ucNetTypeIndex, STA_TYPE_TDLS_PEER, &u4StartIdx);
			if (prStaRec == NULL)
				break;
		}

		DBGLOG(TDLS, TRACE, "-------- TDLS %d: 0x %pM\n", u4PeerNum, (prStaRec->aucMacAddr));
		DBGLOG(TDLS, TRACE, "\t\t\t State %d, PM %d, Cap 0x%x\n",
				     prStaRec->ucStaState, prStaRec->fgIsInPS, prStaRec->u2CapInfo);
		DBGLOG(TDLS, TRACE, "\t\t\t SetupDisable %d, ChSwDisable %d\n",
				     prStaRec->fgTdlsIsProhibited, prStaRec->fgTdlsIsChSwProhibited);

		if (fgIsListAll == FALSE)
			break;	/* only list one */
	}

	/* check if we need to clear all histories */
	if ((prCmdContent != NULL) && (prCmdContent->Content.rCmdInfoDisplay.fgIsToClearAllHistory == TRUE)) {
		kalMemZero(&prGlueInfo->rTdlsLink, sizeof(prGlueInfo->rTdlsLink));
		prGlueInfo->rTdlsLink.u4LinkIdx = TDLS_LINK_HISTORY_MAX - 1;
	}

	DBGLOG(TDLS, INFO, "%s cmd ok.\n", __func__);
	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to display key information.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*/
/*----------------------------------------------------------------------------*/
static TDLS_STATUS
TdlsKeyInfoDisplay(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	TDLS_CMD_CORE_T *prCmdContent;
	WLAN_STATUS rStatus;

	/* init command buffer */
	prCmdContent = (TDLS_CMD_CORE_T *) pvSetBuffer;
	prCmdContent->u4Command = TDLS_CORE_CMD_KEY_INFO;

	/* send the command */
	rStatus = wlanSendSetQueryCmd(prAdapter,	/* prAdapter */
				      CMD_ID_TDLS_CORE,	/* ucCID */
				      TRUE,	/* fgSetQuery */
				      FALSE,	/* fgNeedResp */
				      FALSE,	/* fgIsOid */
				      NULL, NULL,	/* pfCmdTimeoutHandler */
				      sizeof(TDLS_CMD_CORE_T),	/* u4SetQueryInfoLen */
				      (PUINT_8) prCmdContent,	/* pucInfoBuffer */
				      NULL,	/* pvSetQueryBuffer */
				      0	/* u4SetQueryBufferLen */
	    );

	if (rStatus != WLAN_STATUS_PENDING) {
		DBGLOG(TDLS, ERROR, "%s wlanSendSetQueryCmd allocation fail!\n", __func__);
		return TDLS_STATUS_RESOURCES;
	}

	DBGLOG(TDLS, INFO, "%s cmd ok.\n", __func__);
	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to record a disconnection event.
*
* \param[in] prGlueInfo			Pointer to the GLUE_INFO_T structure
* \param[in] fgIsTearDown		TRUE: the link is torn down
* \param[in] pucPeerMac		Pointer to the MAC of the TDLS peer
* \param[in] fgIsFromUs		TRUE: tear down is from us
* \param[in] u2ReasonCode		Disconnection reason (TDLS_REASON_CODE)
*
* \retval none
*/
/*----------------------------------------------------------------------------*/
static VOID
TdlsLinkHistoryRecord(GLUE_INFO_T *prGlueInfo,
		      BOOLEAN fgIsTearDown,
		      UINT8 *pucPeerMac, BOOLEAN fgIsFromUs, UINT16 u2ReasonCode, VOID *prOthers)
{
	TDLS_INFO_LINK_T *prLink;

	DBGLOG(TDLS, INFO,
	       "<tdls_evt> %s: record history for %pM %d %d %d %d\n",
		__func__, pucPeerMac, prGlueInfo->rTdlsLink.u4LinkIdx,
		fgIsTearDown, fgIsFromUs, u2ReasonCode);

	/* check duplicate one */
	if (prGlueInfo->rTdlsLink.u4LinkIdx >= TDLS_LINK_HISTORY_MAX) {
		DBGLOG(TDLS, ERROR, "<tdls_evt> %s: u4LinkIdx >= TDLS_LINK_HISTORY_MAX\n", __func__);

		/* reset to 0 */
		prGlueInfo->rTdlsLink.u4LinkIdx = 0;
	}

	prLink = &prGlueInfo->rTdlsLink.rLinkHistory[prGlueInfo->rTdlsLink.u4LinkIdx];

	if (kalMemCmp(&prLink->aucPeerMac, pucPeerMac, 6) == 0) {
		if ((prLink->ucReasonCode == u2ReasonCode) && (prLink->fgIsFromUs == fgIsFromUs)) {
			/* same Peer MAC, Reason Code, Trigger source */
			if (fgIsTearDown == TRUE) {
				if (prLink->jiffies_end != 0) {
					/* already torn down */
					prLink->ucDupCount++;
					return;
				}
			} else {
				/* already built */
				prLink->ucDupCount++;
				return;
			}
		}
	}

	/* search old entry */
	if (fgIsTearDown == TRUE) {
		/* TODO: need to search all entries to find it if we support multiple TDLS link design */
		if (kalMemCmp(&prLink->aucPeerMac, pucPeerMac, 6) != 0) {
			/* error! can not find the link entry */
			DBGLOG(TDLS, INFO, "<tdls_evt> %s: cannot find the same entry!!!\n", __func__);
			return;
		}

		prLink->jiffies_end = jiffies;
		prLink->ucReasonCode = (UINT8) u2ReasonCode;
		prLink->fgIsFromUs = fgIsFromUs;
	} else {
		/* record new one */
		prGlueInfo->rTdlsLink.u4LinkIdx++;
		if (prGlueInfo->rTdlsLink.u4LinkIdx >= TDLS_LINK_HISTORY_MAX)
			prGlueInfo->rTdlsLink.u4LinkIdx = 0;

		prLink = &prGlueInfo->rTdlsLink.rLinkHistory[prGlueInfo->rTdlsLink.u4LinkIdx];

		prLink->jiffies_start = jiffies;
		prLink->jiffies_end = 0;
		kalMemCopy(&prLink->aucPeerMac, pucPeerMac, 6);
		prLink->ucReasonCode = 0;
		prLink->fgIsFromUs = (UINT8) fgIsFromUs;
		prLink->ucDupCount = 0;

		if (prOthers != NULL) {
			/* record other parameters */
			TDLS_LINK_HIS_OTHERS_T *prHisOthers;

			prHisOthers = (TDLS_LINK_HIS_OTHERS_T *) prOthers;
			if (prHisOthers->fgIsHt == TRUE)
				prLink->ucHtCap |= TDLS_INFO_LINK_HT_CAP_SUP;
		}
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to update a disconnection event.
*
* \param[in] prGlueInfo			Pointer to the GLUE_INFO_T structure
* \param[in] pucPeerMac		Pointer to the MAC of the TDLS peer
* \param[in] eFmeStatus		TDLS_EVENT_HOST_SUBID_SPECIFIC_FRAME
* \param[in] pInfo				other information
*
* \retval none
*/
/*----------------------------------------------------------------------------*/
static VOID
TdlsLinkHistoryRecordUpdate(GLUE_INFO_T *prGlueInfo,
			    UINT8 *pucPeerMac, TDLS_EVENT_HOST_SUBID_SPECIFIC_FRAME eFmeStatus, VOID *pInfo)
{
	TDLS_INFO_LINK_T *prLink;
	UINT32 u4LinkIdx;
	UINT32 u4Tid;

	/* sanity check */
	if ((eFmeStatus < TDLS_HOST_EVENT_SF_BA) || (eFmeStatus > TDLS_HOST_EVENT_SF_BA_RSP_DECLINE)) {
		/* do not care these frames */
		return;
	}

	DBGLOG(TDLS, INFO,
	       "<tdls_evt> %s: update history for %pM %d %d\n",
		__func__, (pucPeerMac), prGlueInfo->rTdlsLink.u4LinkIdx, eFmeStatus);

	/* init */
	u4LinkIdx = prGlueInfo->rTdlsLink.u4LinkIdx;
	prLink = &prGlueInfo->rTdlsLink.rLinkHistory[u4LinkIdx];

	/* TODO: need to search all entries to find it if we support multiple TDLS link design */
	if (kalMemCmp(&prLink->aucPeerMac, pucPeerMac, 6) != 0) {
		/* error! can not find the link entry */
		DBGLOG(TDLS, INFO, "<tdls_evt> %s: cannot find the same entry!!!\n", __func__);
		return;
	}

	/* update */
	u4Tid = *(UINT32 *) pInfo;
	switch (eFmeStatus) {
	case TDLS_HOST_EVENT_SF_BA:
		prLink->ucHtBa[u4Tid] |= TDLS_INFO_LINK_HT_BA_SETUP;
		break;

	case TDLS_HOST_EVENT_SF_BA_OK:
		prLink->ucHtBa[u4Tid] |= TDLS_INFO_LINK_HT_BA_SETUP_OK;
		break;

	case TDLS_HOST_EVENT_SF_BA_DECLINE:
		prLink->ucHtBa[u4Tid] |= TDLS_INFO_LINK_HT_BA_SETUP_DECLINE;
		break;

	case TDLS_HOST_EVENT_SF_BA_PEER:
		prLink->ucHtBa[u4Tid] |= TDLS_INFO_LINK_HT_BA_PEER;
		break;

	case TDLS_HOST_EVENT_SF_BA_RSP_OK:
		prLink->ucHtBa[u4Tid] |= TDLS_INFO_LINK_HT_BA_RSP_OK;
		break;

	case TDLS_HOST_EVENT_SF_BA_RSP_DECLINE:
		prLink->ucHtBa[u4Tid] |= TDLS_INFO_LINK_HT_BA_RSP_DECLINE;
		break;
	}

	/* display TDLS link history */
	TdlsInfoDisplay(prGlueInfo->prAdapter, NULL, 0, NULL);
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to configure TDLS MIB parameters.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*/
/*----------------------------------------------------------------------------*/
static TDLS_STATUS
TdlsMibParamUpdate(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	TDLS_CMD_CORE_T *prCmdContent;
	WLAN_STATUS rStatus;

	/* init command buffer */
	prCmdContent = (TDLS_CMD_CORE_T *) pvSetBuffer;
	prCmdContent->u4Command = TDLS_CORE_CMD_MIB_UPDATE;

	/* send the command */
	rStatus = wlanSendSetQueryCmd(prAdapter,	/* prAdapter */
				      CMD_ID_TDLS_CORE,	/* ucCID */
				      TRUE,	/* fgSetQuery */
				      FALSE,	/* fgNeedResp */
				      FALSE,	/* fgIsOid */
				      NULL, NULL,	/* pfCmdTimeoutHandler */
				      sizeof(TDLS_CMD_CORE_T),	/* u4SetQueryInfoLen */
				      (PUINT_8) prCmdContent,	/* pucInfoBuffer */
				      NULL,	/* pvSetQueryBuffer */
				      0	/* u4SetQueryBufferLen */
	    );

	if (rStatus != WLAN_STATUS_PENDING) {
		DBGLOG(TDLS, ERROR, "%s wlanSendSetQueryCmd allocation fail!\n", __func__);
		return TDLS_STATUS_RESOURCES;
	}

	DBGLOG(TDLS, INFO, "%s cmd ok.\n", __func__);
	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to configure TDLS SETUP parameters.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*/
/*----------------------------------------------------------------------------*/
static TDLS_STATUS
TdlsSetupConf(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	TDLS_CMD_CORE_T *prCmdContent;
	WLAN_STATUS rStatus;

	/* init command buffer */
	prCmdContent = (TDLS_CMD_CORE_T *) pvSetBuffer;
	prCmdContent->u4Command = TDLS_CORE_CMD_SETUP_CONF;

	/* send the command */
	rStatus = wlanSendSetQueryCmd(prAdapter,	/* prAdapter */
				      CMD_ID_TDLS_CORE,	/* ucCID */
				      TRUE,	/* fgSetQuery */
				      FALSE,	/* fgNeedResp */
				      FALSE,	/* fgIsOid */
				      NULL, NULL,	/* pfCmdTimeoutHandler */
				      sizeof(TDLS_CMD_CORE_T),	/* u4SetQueryInfoLen */
				      (PUINT_8) prCmdContent,	/* pucInfoBuffer */
				      NULL,	/* pvSetQueryBuffer */
				      0	/* u4SetQueryBufferLen */
	    );

	if (rStatus != WLAN_STATUS_PENDING) {
		DBGLOG(TDLS, ERROR, "%s wlanSendSetQueryCmd allocation fail!\n", __func__);
		return TDLS_STATUS_RESOURCES;
	}

	DBGLOG(TDLS, INFO, "%s cmd ok.\n", __func__);
	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to configure UAPSD parameters.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*/
/*----------------------------------------------------------------------------*/
static TDLS_STATUS
TdlsUapsdConf(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	TDLS_CMD_CORE_T *prCmdContent;
	WLAN_STATUS rStatus;

	/* init command buffer */
	prCmdContent = (TDLS_CMD_CORE_T *) pvSetBuffer;
	prCmdContent->u4Command = TDLS_CORE_CMD_UAPSD_CONF;

	/* send the command */
	rStatus = wlanSendSetQueryCmd(prAdapter,	/* prAdapter */
				      CMD_ID_TDLS_CORE,	/* ucCID */
				      TRUE,	/* fgSetQuery */
				      FALSE,	/* fgNeedResp */
				      FALSE,	/* fgIsOid */
				      NULL, NULL,	/* pfCmdTimeoutHandler */
				      sizeof(TDLS_CMD_CORE_T),	/* u4SetQueryInfoLen */
				      (PUINT_8) prCmdContent,	/* pucInfoBuffer */
				      NULL,	/* pvSetQueryBuffer */
				      0	/* u4SetQueryBufferLen */
	    );

	if (rStatus != WLAN_STATUS_PENDING) {
		DBGLOG(TDLS, ERROR, "%s wlanSendSetQueryCmd allocation fail!\n", __func__);
		return TDLS_STATUS_RESOURCES;
	}

	DBGLOG(TDLS, INFO, "%s cmd ok.\n", __func__);
	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to update frame status.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer, from u4EventSubId
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*/
/*----------------------------------------------------------------------------*/
static void TdlsEventFmeStatus(GLUE_INFO_T *prGlueInfo, UINT8 *prInBuf, UINT32 u4InBufLen)
{
	TDLS_EVENT_HOST_SUBID_SPECIFIC_FRAME eFmeStatus;
	STA_RECORD_T *prStaRec;
	UINT32 u4Tid;

	/* init */
	u4Tid = *(UINT32 *) prInBuf;
	prInBuf += 4;		/* skip u4EventSubId */

	/* sanity check */
	prStaRec = cnmGetStaRecByIndex(prGlueInfo->prAdapter, *prInBuf);
	if ((prStaRec == NULL) || (!IS_TDLS_STA(prStaRec)))
		return;
	prInBuf++;

	/* update status */
	eFmeStatus = *prInBuf;
	TdlsLinkHistoryRecordUpdate(prGlueInfo, prStaRec->aucMacAddr, eFmeStatus, &u4Tid);
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to collect TDLS statistics from firmware.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer, from u4EventSubId
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*/
/*----------------------------------------------------------------------------*/
static void TdlsEventStatistics(GLUE_INFO_T *prGlueInfo, UINT8 *prInBuf, UINT32 u4InBufLen)
{
	STA_RECORD_T *prStaRec;
	STAT_CNT_INFO_FW_T *prStat;
	UINT32 u4RateId;

	/* init */
	prStaRec = cnmGetStaRecByIndex(prGlueInfo->prAdapter, *prInBuf);
	if ((prStaRec == NULL) || (!IS_TDLS_STA(prStaRec)))
		return;

	prInBuf += 4;		/* skip prStaRec->ucIndex */

	/* update statistics */
	kalMemCopy(&prStaRec->rTdlsStatistics.rFw, prInBuf, sizeof(prStaRec->rTdlsStatistics.rFw));

	/* display statistics */
	prStat = &prStaRec->rTdlsStatistics.rFw;

	DBGLOG(TDLS, TRACE, "<tdls_evt> peer [%pM] statistics:\n", (prStaRec->aucMacAddr));
	DBGLOG(TDLS, TRACE, "\t\tT%d %d %d (P%d %d) (%dus) - E%d 0x%x - R%d (P%d)\n",
			     prStat->u4NumOfTx, prStat->u4NumOfTxOK, prStat->u4NumOfTxRetry,
			     prStat->u4NumOfPtiRspTxOk, prStat->u4NumOfPtiRspTxErr,
			     prStat->u4TxDoneAirTimeMax,
			     prStat->u4NumOfTxErr, prStat->u4TxErrBitmap, prStat->u4NumOfRx, prStat->u4NumOfPtiRspRx);

	DBGLOG(TDLS, TRACE, "\t\t");

	for (u4RateId = prStat->u4TxRateOkHisId; u4RateId < STAT_CNT_INFO_MAX_TX_RATE_OK_HIS_NUM; u4RateId++)
		DBGLOG(TDLS, TRACE,
			"%d(%d) ", prStat->aucTxRateOkHis[u4RateId][0], prStat->aucTxRateOkHis[u4RateId][1]);
	for (u4RateId = 0; u4RateId < prStat->u4TxRateOkHisId; u4RateId++)
		DBGLOG(TDLS, TRACE,
			"%d(%d) ", prStat->aucTxRateOkHis[u4RateId][0], prStat->aucTxRateOkHis[u4RateId][1]);

	DBGLOG(TDLS, TRACE, "\n\n");
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to do tear down.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer, from u4EventSubId
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*/
/*----------------------------------------------------------------------------*/
static void TdlsEventTearDown(GLUE_INFO_T *prGlueInfo, UINT8 *prInBuf, UINT32 u4InBufLen)
{
	STA_RECORD_T *prStaRec;
	UINT16 u2ReasonCode;
	UINT32 u4TearDownSubId;
	UINT8 *pMac, aucZeroMac[6];

	/* init */
	u4TearDownSubId = *(UINT32 *) prInBuf;
	kalMemZero(aucZeroMac, sizeof(aucZeroMac));
	pMac = aucZeroMac;

	prStaRec = cnmGetStaRecByIndex(prGlueInfo->prAdapter, *(prInBuf + 4));
	if (prStaRec != NULL)
		pMac = prStaRec->aucMacAddr;

	/* handle */
	if (u4TearDownSubId == TDLS_HOST_EVENT_TD_PTI_TIMEOUT) {
		DBGLOG(TDLS, WARN, "<tdls_evt> %s: peer [%pM] Reason=PTI timeout\n",
				    __func__, pMac);
	} else if (u4TearDownSubId == TDLS_HOST_EVENT_TD_AGE_TIMEOUT) {
		DBGLOG(TDLS, WARN, "<tdls_evt> %s: peer [%pM] Reason=AGE timeout\n",
				    __func__, pMac);
	} else {
		DBGLOG(TDLS, WARN, "<tdls_evt> %s: peer [%pM] Reason=%d\n",
				    __func__, pMac, u4TearDownSubId);
	}

	/* sanity check */
	if (prStaRec == NULL)
		return;

	if (fgIsPtiTimeoutSkip == TRUE) {
		/* skip PTI timeout event */
		if (u4TearDownSubId == TDLS_HOST_EVENT_TD_PTI_TIMEOUT) {
			DBGLOG(TDLS, WARN, "<tdls_evt> %s: skip PTI timeout\n", __func__);
			return;
		}
	}

	/* record history */
	if (u4TearDownSubId == TDLS_HOST_EVENT_TD_AGE_TIMEOUT)
		u2ReasonCode = TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_AGE_TIMEOUT;
	else if (u4TearDownSubId == TDLS_HOST_EVENT_TD_PTI_TIMEOUT)
		u2ReasonCode = TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_PTI_TIMEOUT;
	else if (u4TearDownSubId == TDLS_HOST_EVENT_TD_PTI_SEND_FAIL)
		u2ReasonCode = TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_PTI_SEND_FAIL;
	else if (u4TearDownSubId == TDLS_HOST_EVENT_TD_PTI_SEND_MAX_FAIL)
		u2ReasonCode = TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_PTI_SEND_MAX_FAIL;
	else if (u4TearDownSubId == TDLS_HOST_EVENT_TD_WRONG_NETWORK_IDX)
		u2ReasonCode = TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_WRONG_NETWORK_IDX;
	else if (u4TearDownSubId == TDLS_HOST_EVENT_TD_NON_STATE3)
		u2ReasonCode = TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_NON_STATE3;
	else if (u4TearDownSubId == TDLS_HOST_EVENT_TD_LOST_TEAR_DOWN)
		u2ReasonCode = TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_LOST_TEAR_DOWN;
	else {
		/* shall not be here */
		u2ReasonCode = TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_UNKNOWN;
	}

	TdlsLinkHistoryRecord(prGlueInfo, TRUE, prStaRec->aucMacAddr, TRUE, u2ReasonCode, NULL);

	/* correct correct reason code for PTI or AGE timeout to supplicant */
	if ((u2ReasonCode == TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_AGE_TIMEOUT) ||
	    (u2ReasonCode == TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_PTI_TIMEOUT)) {
		u2ReasonCode = TDLS_REASON_CODE_UNREACHABLE;
	}

	/* 16 Nov 21:49 2012 http://permalink.gmane.org/gmane.linux.kernel.wireless.general/99712 */
	cfg80211_tdls_oper_request(prGlueInfo->prDevHandler,
				   prStaRec->aucMacAddr, NL80211_TDLS_TEARDOWN, u2ReasonCode, GFP_ATOMIC);
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to do tx down.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer, from u4EventSubId
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*/
/*----------------------------------------------------------------------------*/
static void TdlsEventTxDone(GLUE_INFO_T *prGlueInfo, UINT8 *prInBuf, UINT32 u4InBufLen)
{
	/* UINT32 u4FmeIdx; */
	UINT8 *pucFmeHdr;
	UINT8 ucErrStatus;

	ucErrStatus = *(UINT32 *) prInBuf;

	pucFmeHdr = prInBuf + 4;	/* skip ucErrStatus */

	if (ucErrStatus == 0)
		DBGLOG(TDLS, TRACE, "<tdls_evt> %s: OK to tx a TDLS action:", __func__);
	else
		DBGLOG(TDLS, TRACE, "<tdls_evt> %s: fail to tx a TDLS action (err=0x%x):", __func__, ucErrStatus);
	#if 0
	/* dump TX packet content from wlan header */
	for (u4FmeIdx = 0; u4FmeIdx < (u4InBufLen - 4); u4FmeIdx++) {
		if ((u4FmeIdx % 16) == 0)
			DBGLOG(TDLS, TRACE, "\n");

		DBGLOG(TDLS, TRACE, "%02x ", *pucFmeHdr++);
	}
	DBGLOG(TDLS, TRACE, "\n\n");
	#endif
}

/*******************************************************************************
*						P U B L I C  F U N C T I O N S
********************************************************************************
*/

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to parse TDLS Extended Capabilities element.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*/
/*----------------------------------------------------------------------------*/
VOID TdlsexBssExtCapParse(STA_RECORD_T *prStaRec, UINT_8 *pucIE)
{
	UINT_8 *pucIeExtCap;

	/* sanity check */
	if ((prStaRec == NULL) || (pucIE == NULL))
		return;

	if (IE_ID(pucIE) != ELEM_ID_EXTENDED_CAP)
		return;

	/*
	   from bit0 ~

	   bit 38: TDLS Prohibited
	   The TDLS Prohibited subfield indicates whether the use of TDLS is prohibited. The
	   field is set to 1 to indicate that TDLS is prohibited and to 0 to indicate that TDLS is
	   allowed.
	 */
	if (IE_LEN(pucIE) < 5)
		return;		/* we need 39/8 = 5 bytes */

	/* init */
	prStaRec->fgTdlsIsProhibited = FALSE;
	prStaRec->fgTdlsIsChSwProhibited = FALSE;

	/* parse */
	pucIeExtCap = pucIE + 2;
	pucIeExtCap += 4;	/* shift to the byte we care about */

	if ((*pucIeExtCap) & BIT(38 - 32))
		prStaRec->fgTdlsIsProhibited = TRUE;
	if ((*pucIeExtCap) & BIT(39 - 32))
		prStaRec->fgTdlsIsChSwProhibited = TRUE;

	DBGLOG(TDLS, TRACE,
	       "<tdls> %s: AP [%pM] tdls prohibit bit=%d %d\n",
		__func__,
		prStaRec->aucMacAddr, prStaRec->fgTdlsIsProhibited, prStaRec->fgTdlsIsChSwProhibited);
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to transmit a TDLS data frame from nl80211.
*
* \param[in] pvAdapter		Pointer to the Adapter structure.
* \param[in]
* \param[in]
* \param[in] buf			includes RSN IE + FT IE + Lifetimeout IE
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
int
TdlsexCfg80211TdlsMgmt(struct wiphy *wiphy, struct net_device *dev,
		       const u8 *peer, u8 action_code, u8 dialog_token,
		       u16 status_code, u32 peer_capability,
		       bool initiator, const u8 *buf, size_t len)
{
	ADAPTER_T *prAdapter;
	GLUE_INFO_T *prGlueInfo;
	BSS_INFO_T *prAisBssInfo;
	WLAN_STATUS rStatus;
	UINT_32 u4BufLen;
	TDLS_MGMT_TX_INFO *prMgmtTxInfo;

	/*
	   Have correct behavior for STAUT receiving TDLS Setup Request after sending TDLS
	   Set Request and before receiving TDLS Setup Response:
	   -- Source Address of received Request is higher than own MAC address
	   -- Source Address of received Request is lower than own MAC address

	   ==> STA with larger MAC address will send the response frame.

	   Supplicant will do this in wpa_tdls_process_tpk_m1().
	 */

	/* sanity check */
	if ((wiphy == NULL) || (peer == NULL)) {
		DBGLOG(TDLS, ERROR, "<tdls_cfg> %s: wrong 0x%p 0x%p!\n", __func__, wiphy, peer);
		return -EINVAL;
	}

	DBGLOG(TDLS, INFO, "<tdls_cfg> %s: [%pM] %d %d %d 0x%p %u\n",
			    __func__, peer, action_code, dialog_token, status_code, buf, (UINT32) len);

	/* init */
	prGlueInfo = (GLUE_INFO_T *) wiphy_priv(wiphy);
	if (prGlueInfo == NULL) {
		DBGLOG(TDLS, ERROR, "<tdls_cfg> %s: wrong prGlueInfo 0x%p!\n", __func__, prGlueInfo);
		return -EINVAL;
	}

	prAdapter = prGlueInfo->prAdapter;
	if (prAdapter->fgTdlsIsSup == FALSE) {
		DBGLOG(TDLS, ERROR, "<tdls_cfg> %s: firmware TDLS is not supported!\n", __func__);
		return -EBUSY;
	}

	prAisBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX]);
	if (prAisBssInfo->fgTdlsIsProhibited == TRUE) {
		/* do not send anything if TDLS is prohibited in the BSS */
		return 0;
	}

	prMgmtTxInfo = kalMemAlloc(sizeof(TDLS_MGMT_TX_INFO), VIR_MEM_TYPE);
	if (prMgmtTxInfo == NULL) {
		DBGLOG(TDLS, ERROR, "<tdls_cfg> %s: allocate fail!\n", __func__);
		return -ENOMEM;
	}

	kalMemZero(prMgmtTxInfo, sizeof(TDLS_MGMT_TX_INFO));

	if (peer != NULL)
		kalMemCopy(prMgmtTxInfo->aucPeer, peer, 6);
	prMgmtTxInfo->ucActionCode = action_code;
	prMgmtTxInfo->ucDialogToken = dialog_token;
	prMgmtTxInfo->u2StatusCode = status_code;

	if (buf != NULL) {
		if (len > sizeof(prMgmtTxInfo->aucSecBuf)) {
			kalMemFree(prMgmtTxInfo, VIR_MEM_TYPE, sizeof(TDLS_MGMT_TX_INFO));
			return -EINVAL;
		}
		prMgmtTxInfo->u4SecBufLen = len;
		kalMemCopy(prMgmtTxInfo->aucSecBuf, buf, len);
	}

	/* send the TDLS action data frame */
	rStatus = kalIoctl(prGlueInfo,
			   TdlsexMgmtCtrl,
			   prMgmtTxInfo, sizeof(TDLS_MGMT_TX_INFO), FALSE, FALSE, FALSE, FALSE, &u4BufLen);

	/*
	   clear all content to avoid any bug if we dont yet execute TdlsexMgmtCtrl()
	   then kalIoctl finishes
	 */
	kalMemZero(prMgmtTxInfo, sizeof(TDLS_MGMT_TX_INFO));

	if (rStatus != WLAN_STATUS_SUCCESS) {
		DBGLOG(TDLS, ERROR, "%s enable or disable link fail:%x\n", __func__, rStatus);
		kalMemFree(prMgmtTxInfo, VIR_MEM_TYPE, sizeof(TDLS_MGMT_TX_INFO));
		return -EINVAL;
	}

	kalMemFree(prMgmtTxInfo, VIR_MEM_TYPE, sizeof(TDLS_MGMT_TX_INFO));
	return 0;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to enable or disable TDLS link from upper layer.
*
* \param[in] pvAdapter		Pointer to the Adapter structure.
* \param[in]
* \param[in]
* \param[in] buf			includes RSN IE + FT IE + Lifetimeout IE
*
* \retval WLAN_STATUS_SUCCESS
* \retval WLAN_STATUS_INVALID_LENGTH
*/
/*----------------------------------------------------------------------------*/
int TdlsexCfg80211TdlsOper(struct wiphy *wiphy, struct net_device *dev,
				const u8 *peer, enum nl80211_tdls_operation oper)
{
	ADAPTER_T *prAdapter;
	GLUE_INFO_T *prGlueInfo;
	WLAN_STATUS rStatus;
	UINT_32 u4BufLen;
	TDLS_CMD_LINK_T rCmdLink;

	/* sanity check */
	if (peer == NULL) {
		DBGLOG(TDLS, ERROR, "<tdls_cfg> %s: peer == NULL!\n", __func__);
		return -EINVAL;
	}

	DBGLOG(TDLS, INFO, "<tdls_cfg> %s: [%pM] %d %d\n",
			    __func__, peer, oper, (wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS));

	if (!(wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
		return -ENOTSUPP;

	/* init */
	prGlueInfo = (GLUE_INFO_T *) wiphy_priv(wiphy);
	if (prGlueInfo == NULL) {
		DBGLOG(TDLS, ERROR, "<tdls_cfg> %s: wrong prGlueInfo 0x%p!\n", __func__, prGlueInfo);
		return -EINVAL;
	}
	prAdapter = prGlueInfo->prAdapter;
	kalMemCopy(rCmdLink.aucPeerMac, peer, sizeof(rCmdLink.aucPeerMac));
	rCmdLink.fgIsEnabled = FALSE;

	/*
	   enum nl80211_tdls_operation {
	   NL80211_TDLS_DISCOVERY_REQ,
	   NL80211_TDLS_SETUP,
	   NL80211_TDLS_TEARDOWN,
	   NL80211_TDLS_ENABLE_LINK,
	   NL80211_TDLS_DISABLE_LINK,
	   };
	 */

	switch (oper) {
	case NL80211_TDLS_ENABLE_LINK:
		rCmdLink.fgIsEnabled = TRUE;
		break;

	case NL80211_TDLS_DISABLE_LINK:
		rCmdLink.fgIsEnabled = FALSE;
		break;

	case NL80211_TDLS_TEARDOWN:
	case NL80211_TDLS_SETUP:
	case NL80211_TDLS_DISCOVERY_REQ:
		/* we do not support setup/teardown/discovery from driver */
		return -ENOTSUPP;

	default:
		return -ENOTSUPP;
	}

	/* enable or disable TDLS link */
	rStatus = kalIoctl(prGlueInfo,
			   TdlsexLinkCtrl, &rCmdLink, sizeof(TDLS_CMD_LINK_T), FALSE, FALSE, FALSE, FALSE, &u4BufLen);

	if (rStatus != WLAN_STATUS_SUCCESS) {
		DBGLOG(TDLS, ERROR, "%s enable or disable link fail:%x\n", __func__, rStatus);
		return -EINVAL;
	}

	return 0;
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to send a command to TDLS module.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*/
/*----------------------------------------------------------------------------*/
VOID TdlsexCmd(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen)
{
	UINT_32 u4Subcmd;
	static void (*TdlsCmdTestFunc)(P_GLUE_INFO_T, UINT_8 *, UINT_32);

	/* parse TDLS sub-command */
	u4Subcmd = CmdStringDecParse(prInBuf, &prInBuf, &u4InBufLen);
	DBGLOG(TDLS, INFO, "<tdls_cmd> sub command = %u\n", (UINT32) u4Subcmd);
	TdlsCmdTestFunc = NULL;

	/* handle different sub-command */
	switch (u4Subcmd) {
#if TDLS_CFG_CMD_TEST		/* only for unit test */
	case TDLS_CMD_TEST_TX_FRAME:
		/* simulate to send a TDLS frame */
		/* TdlsCmdTestTxFrame(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdTestTxFrame;
		break;

	case TDLS_CMD_TEST_TX_TDLS_FRAME:
		/* simulate to send a TDLS frame from supplicant */
		/* TdlsCmdTestTxTdlsFrame(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdTestTxTdlsFrame;
		break;

	case TDLS_CMD_TEST_RCV_FRAME:
		/* simulate to receive a TDLS frame */
		/* TdlsCmdTestRvFrame(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdTestRvFrame;
		break;

	case TDLS_CMD_TEST_PEER_ADD:
		/* simulate to add a TDLS peer */
		/* TdlsCmdTestAddPeer(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdTestAddPeer;
		break;

	case TDLS_CMD_TEST_PEER_UPDATE:
		/* simulate to update a TDLS peer */
		/* TdlsCmdTestUpdatePeer(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdTestUpdatePeer;
		break;

	case TDLS_CMD_TEST_DATA_FRAME:
		/* simulate to send a data frame to the peer */
		/* TdlsCmdTestDataSend(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdTestDataSend;
		break;

	case TDLS_CMD_TEST_RCV_NULL:
		/* simulate to receive a QoS null frame from the peer */
		/* TdlsCmdTestNullRecv(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdTestNullRecv;
		break;

	case TDLS_CMD_TEST_SKIP_TX_FAIL:
		/* command firmware to skip tx fail case */
		/* TdlsCmdTestTxFailSkip(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdTestTxFailSkip;
		break;

	case TDLS_CMD_TEST_SKIP_KEEP_ALIVE:
		/* command firmware to skip keep alive function */
		/* TdlsCmdTestKeepAliveSkip(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdTestKeepAliveSkip;
		break;

	case TDLS_CMD_TEST_SKIP_CHSW_TIMEOUT:
		/* command firmware to skip channel switch timeout function */
		/* TdlsCmdTestChSwTimeoutSkip(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdTestChSwTimeoutSkip;
		break;

	case TDLS_CMD_TEST_PROHIBIT_SET_IN_AP:
		/* simulate to set Prohibited Bit in AP */
		/* TdlsCmdTestProhibitedBitSet(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdTestProhibitedBitSet;
		break;

	case TDLS_CMD_TEST_SCAN_DISABLE:
		/* command to disable scan request to do channel switch */
		/* TdlsCmdTestScanCtrl(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdTestScanCtrl;
		break;

	case TDLS_CMD_TEST_DATA_FRAME_CONT:
		/* simulate to send a data frame to the peer periodically */
		/* TdlsCmdTestDataContSend(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdTestDataContSend;
		break;

	case TDLS_CMD_TEST_CH_SW_PROHIBIT_SET_IN_AP:
		/* simulate to set channel switch Prohibited Bit in AP */
		/* TdlsCmdTestChSwProhibitedBitSet(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdTestChSwProhibitedBitSet;
		break;

	case TDLS_CMD_TEST_DELAY:
		/* delay a where */
		/* TdlsCmdTestDelay(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdTestDelay;
		break;

	case TDLS_CMD_TEST_PTI_TX_FAIL:
		/* simulate the tx done fail for PTI */
		/* TdlsCmdTestPtiTxDoneFail(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdTestPtiTxDoneFail;
		break;
#endif /* TDLS_CFG_CMD_TEST */

	case TDLS_CMD_MIB_UPDATE:
		/* update MIB parameters */
		/* TdlsCmdMibParamUpdate(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdMibParamUpdate;
		break;

	case TDLS_CMD_UAPSD_CONF:
		/* config UAPSD parameters */
		/* TdlsCmdUapsdConf(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdUapsdConf;
		break;

	case TDLS_CMD_CH_SW_CONF:
		/* enable or disable or start or stop channel switch function */
		/* TdlsCmdChSwConf(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdChSwConf;
		break;

	case TDLS_CMD_SETUP_CONF:
		/* config setup parameters */
		/* TdlsCmdSetupConf(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdSetupConf;
		break;

	case TDLS_CMD_INFO:
		/* display all TDLS information */
		/* TdlsCmdInfoDisplay(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdInfoDisplay;
		break;

	case TDLS_CMD_KEY_INFO:
		/* display key information */
		/* TdlsCmdKeyInfoDisplay(prGlueInfo, prInBuf, u4InBufLen); */
		TdlsCmdTestFunc = TdlsCmdKeyInfoDisplay;
		break;

	default:
		break;
	}

	if (TdlsCmdTestFunc != NULL)
		TdlsCmdTestFunc(prGlueInfo, prInBuf, u4InBufLen);

}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to record a disconnection event.
*
* \param[in] prGlueInfo			Pointer to the GLUE_INFO_T structure
* \param[in] fgIsTearDown		TRUE: tear down
* \param[in] pucPeerMac		Pointer to the MAC of the TDLS peer
* \param[in] fgIsFromUs		TRUE: tear down is from us
* \param[in] u2ReasonCode		Disconnection reason (TDLS_REASON_CODE)
*
* \retval none
*
*/
/*----------------------------------------------------------------------------*/
VOID
TdlsexLinkHistoryRecord(GLUE_INFO_T *prGlueInfo,
			BOOLEAN fgIsTearDown, UINT8 *pucPeerMac, BOOLEAN fgIsFromUs, UINT16 u2ReasonCode)
{
	/* sanity check */
	if ((prGlueInfo == NULL) || (pucPeerMac == NULL))
		return;

	DBGLOG(TDLS, INFO,
	       "<tdls_evt> %s: Rcv a inform from %pM %d %d\n",
		__func__, pucPeerMac, fgIsFromUs, u2ReasonCode);

	/* record */
	TdlsLinkHistoryRecord(prGlueInfo, fgIsTearDown, pucPeerMac, fgIsFromUs, u2ReasonCode, NULL);
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to send a command to TDLS module.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*/
/*----------------------------------------------------------------------------*/
VOID TdlsexEventHandle(GLUE_INFO_T *prGlueInfo, UINT8 *prInBuf, UINT32 u4InBufLen)
{
	UINT32 u4EventId;

	/* sanity check */
	if ((prGlueInfo == NULL) || (prInBuf == NULL))
		return;		/* shall not be here */

	/* handle */
	u4EventId = *(UINT32 *) prInBuf;
	u4InBufLen -= 4;

	DBGLOG(TDLS, INFO, "<tdls> %s: Rcv a event: %d\n", __func__, u4EventId);

	switch (u4EventId) {
	case TDLS_HOST_EVENT_TEAR_DOWN:
		TdlsEventTearDown(prGlueInfo, prInBuf + 4, u4InBufLen);
		break;

	case TDLS_HOST_EVENT_TX_DONE:
		TdlsEventTxDone(prGlueInfo, prInBuf + 4, u4InBufLen);
		break;

	case TDLS_HOST_EVENT_FME_STATUS:
		TdlsEventFmeStatus(prGlueInfo, prInBuf + 4, u4InBufLen);
		break;

	case TDLS_HOST_EVENT_STATISTICS:
		TdlsEventStatistics(prGlueInfo, prInBuf + 4, u4InBufLen);
		break;
	}
}

/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to initialize variables in TDLS.
*
* \param[in] prAdapter			Pointer to the Adapter structure
*
* @return	TDLS_STATUS_SUCCESS: do not set key and key infor. is queued
			TDLS_STATUS_FAILURE: set key
*/
/*----------------------------------------------------------------------------*/
TDLS_STATUS TdlsexKeyHandle(ADAPTER_T *prAdapter, PARAM_KEY_T *prNewKey)
{
	STA_RECORD_T *prStaRec;

	/* sanity check */
	if ((prAdapter == NULL) || (prNewKey == NULL))
		return TDLS_STATUS_FAILURE;

	/*
	   supplicant will set key before updating station & enabling the link so we need to
	   backup the key information and set key when link is enabled
	 */
	prStaRec = cnmGetStaRecByAddress(prAdapter, NETWORK_TYPE_AIS_INDEX, prNewKey->arBSSID);
	if ((prStaRec != NULL) && IS_TDLS_STA(prStaRec)) {
		DBGLOG(TDLS, TRACE, "<tdls> %s: [%pM] queue key (len=%d) until link is enabled\n",
				     __func__, prNewKey->arBSSID, (UINT32) prNewKey->u4KeyLength);

		if (prStaRec->ucStaState == STA_STATE_3) {
			DBGLOG(TDLS, TRACE, "<tdls> %s: [%pM] tear down the link due to STA_STATE_3\n",
					     __func__, prNewKey->arBSSID);

			/* re-key */
			TdlsLinkHistoryRecord(prAdapter->prGlueInfo, TRUE,
					      prStaRec->aucMacAddr, TRUE,
					      TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_REKEY, NULL);

			/* 16 Nov 21:49 2012 http://permalink.gmane.org/gmane.linux.kernel.wireless.general/99712 */
			cfg80211_tdls_oper_request(prAdapter->prGlueInfo->prDevHandler,
						   prStaRec->aucMacAddr, TDLS_FRM_ACTION_TEARDOWN,
						   TDLS_REASON_CODE_UNSPECIFIED, GFP_ATOMIC);
			return TDLS_STATUS_SUCCESS;
		}

		/* backup the key */
		kalMemCopy(&prStaRec->rTdlsKeyTemp, prNewKey, sizeof(prStaRec->rTdlsKeyTemp));
		return TDLS_STATUS_SUCCESS;
	}

	return TDLS_STATUS_FAILURE;
}

/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to initialize variables in TDLS.
*
* \param[in] prAdapter			Pointer to the Adapter structure
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID TdlsexInit(ADAPTER_T *prAdapter)
{
	GLUE_INFO_T *prGlueInfo;

	/* init */
	prGlueInfo = (GLUE_INFO_T *) prAdapter->prGlueInfo;

	/* reset */
	kalMemZero(&prGlueInfo->rTdlsLink, sizeof(prGlueInfo->rTdlsLink));
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to get any peer is in power save.
*
* \param[in] prAdapter			Pointer to the Adapter structure
*
* \retval TRUE (at least one peer is in power save)
*/
/*----------------------------------------------------------------------------*/
BOOLEAN TdlsexIsAnyPeerInPowerSave(ADAPTER_T *prAdapter)
{
	STA_RECORD_T *prStaRec;
	UINT32 u4StaId, u4StartIdx;

	for (u4StaId = 0, u4StartIdx = 0; u4StaId < CFG_STA_REC_NUM; u4StaId++) {
		/* list all TDLS peers */
		prStaRec = cnmStaTheTypeGet(prAdapter, NETWORK_TYPE_AIS_INDEX, STA_TYPE_TDLS_PEER, &u4StartIdx);
		if (prStaRec == NULL)
			break;

		if (prStaRec->fgIsInPS == TRUE) {
			DBGLOG(TDLS, TRACE, "<tx> yes, at least one peer is in ps\n");
			return TRUE;
		}
	}

	return FALSE;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to enable or disable a TDLS link.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*/
/*----------------------------------------------------------------------------*/
TDLS_STATUS TdlsexLinkCtrl(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	GLUE_INFO_T *prGlueInfo;
	TDLS_CMD_LINK_T *prCmd;
	BSS_INFO_T *prBssInfo;
	STA_RECORD_T *prStaRec;
	TDLS_LINK_HIS_OTHERS_T rHisOthers;

	/* sanity check */
	if ((prAdapter == NULL) || (pvSetBuffer == NULL) || (pu4SetInfoLen == NULL)) {
		DBGLOG(TDLS, ERROR, "<tdls_cmd> %s: sanity fail!\n", __func__);
		return TDLS_STATUS_FAILURE;
	}

	/* init */
	prGlueInfo = (GLUE_INFO_T *) prAdapter->prGlueInfo;
	*pu4SetInfoLen = sizeof(TDLS_CMD_LINK_T);
	prCmd = (TDLS_CMD_LINK_T *) pvSetBuffer;

	/* search old entry */
	prStaRec = cnmGetStaRecByAddress(prAdapter, (UINT_8) NETWORK_TYPE_AIS_INDEX, prCmd->aucPeerMac);
	if (prStaRec == NULL) {
		DBGLOG(TDLS, ERROR, "<tdls_cfg> %s: cannot find the peer! %pM\n",
				     __func__, prCmd->aucPeerMac);
		return TDLS_STATUS_FAILURE;
	}

	if (prCmd->fgIsEnabled == TRUE) {
		cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_3);
		DBGLOG(TDLS, TRACE, "<tdls_cfg> %s: NL80211_TDLS_ENABLE_LINK\n", __func__);

		/* update key information after cnmStaRecChangeState(STA_STATE_3) */
		prStaRec->fgTdlsInSecurityMode = FALSE;

		if (prStaRec->rTdlsKeyTemp.u4Length > 0) {
			UINT_32 u4BufLen;	/* no use */

			DBGLOG(TDLS, INFO, "<tdls_cfg> %s: key len=%d\n",
					    __func__, (UINT32) prStaRec->rTdlsKeyTemp.u4Length);

			/*
			   reminder the function that we are CIPHER_SUITE_CCMP,
			   do not change cipher type to CIPHER_SUITE_WEP128
			 */
			_wlanoidSetAddKey(prAdapter, &prStaRec->rTdlsKeyTemp,
					  prStaRec->rTdlsKeyTemp.u4Length, FALSE, CIPHER_SUITE_CCMP, &u4BufLen);

			/* clear the temp key */
			prStaRec->fgTdlsInSecurityMode = TRUE;
			kalMemZero(&prStaRec->rTdlsKeyTemp, sizeof(prStaRec->rTdlsKeyTemp));
		}

		/* check if we need to disable channel switch function */
		prBssInfo = &(prAdapter->rWifiVar.arBssInfo[prStaRec->ucNetTypeIndex]);
		if (prBssInfo->fgTdlsIsChSwProhibited == TRUE) {
			TDLS_CMD_CORE_T rCmd;

			kalMemZero(&rCmd, sizeof(TDLS_CMD_CORE_T));
			rCmd.Content.rCmdChSwConf.ucNetTypeIndex = prStaRec->ucNetTypeIndex;
			rCmd.Content.rCmdChSwConf.fgIsChSwEnabled = FALSE;
			kalMemCopy(rCmd.aucPeerMac, prStaRec->aucMacAddr, 6);
			TdlsChSwConf(prAdapter, &rCmd, 0, 0);

			DBGLOG(TDLS, INFO, "<tdls_cfg> %s: disable channel switch\n", __func__);
		}

		TDLS_LINK_INCREASE(prGlueInfo);

		/* record link */
		if (prStaRec->ucDesiredPhyTypeSet & PHY_TYPE_SET_802_11N)
			rHisOthers.fgIsHt = TRUE;
		else
			rHisOthers.fgIsHt = FALSE;

		TdlsLinkHistoryRecord(prAdapter->prGlueInfo, FALSE,
				      prStaRec->aucMacAddr, !prStaRec->flgTdlsIsInitiator, 0, &rHisOthers);
	} else {
		cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_1);
		cnmStaRecFree(prAdapter, prStaRec, TRUE);	/* release to other TDLS peers */
		DBGLOG(TDLS, TRACE, "<tdls_cfg> %s: NL80211_TDLS_DISABLE_LINK\n", __func__);

		TDLS_LINK_DECREASE(prGlueInfo);
/* while(1); //sample debug */
	}

	/* work-around link count */
	if ((TDLS_LINK_COUNT(prGlueInfo) < 0) || (TDLS_LINK_COUNT(prGlueInfo) > 1)) {
		/* ERROR case: work-around to recount by searching all station records */
		UINT32 u4Idx;

		TDLS_LINK_COUNT_RESET(prGlueInfo);

		for (u4Idx = 0; u4Idx < CFG_STA_REC_NUM; u4Idx++) {
			prStaRec = &prAdapter->arStaRec[u4Idx];

			if (prStaRec->fgIsInUse && IS_TDLS_STA(prStaRec))
				TDLS_LINK_INCREASE(prGlueInfo);
		}

		if (TDLS_LINK_COUNT(prGlueInfo) > 1) {
			/* number of links is still > 1 */
			DBGLOG(TDLS, INFO, "<tdls_cfg> %s: cTdlsLinkCnt %d > 1?\n",
					    __func__, TDLS_LINK_COUNT(prGlueInfo));

			TDLS_LINK_COUNT_RESET(prGlueInfo);

			/* free all TDLS links */
			for (u4Idx = 0; u4Idx < CFG_STA_REC_NUM; u4Idx++) {
				prStaRec = &prAdapter->arStaRec[u4Idx];

				if (prStaRec->fgIsInUse && IS_TDLS_STA(prStaRec))
					cnmStaRecFree(prAdapter, prStaRec, TRUE);
			}

			/* maybe inform supplicant ? */
		}
	}

	/* display TDLS link history */
	TdlsInfoDisplay(prAdapter, NULL, 0, NULL);

	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to send a TDLS action data frame.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*/
/*----------------------------------------------------------------------------*/
TDLS_STATUS TdlsexMgmtCtrl(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	GLUE_INFO_T *prGlueInfo;
	TDLS_MGMT_TX_INFO *prMgmtTxInfo;
	STA_RECORD_T *prStaRec;

	/* sanity check */
	if ((prAdapter == NULL) || (pvSetBuffer == NULL) || (pu4SetInfoLen == NULL)) {
		DBGLOG(TDLS, ERROR, "<tdls_cmd> %s: sanity fail!\n", __func__);
		return TDLS_STATUS_FAILURE;
	}

	/* init */
	prGlueInfo = (GLUE_INFO_T *) prAdapter->prGlueInfo;
	*pu4SetInfoLen = sizeof(TDLS_MGMT_TX_INFO);
	prMgmtTxInfo = (TDLS_MGMT_TX_INFO *) pvSetBuffer;

	switch (prMgmtTxInfo->ucActionCode) {
	case TDLS_FRM_ACTION_DISCOVERY_RESPONSE:
		prStaRec = NULL;
		break;

	case TDLS_FRM_ACTION_SETUP_REQ:
		prStaRec = cnmGetStaRecByAddress(prAdapter, (UINT_8) NETWORK_TYPE_AIS_INDEX, prMgmtTxInfo->aucPeer);
		if ((prStaRec != NULL) && (prStaRec->ucStaState == STA_STATE_3)) {
			/* rekey? we reject re-setup link currently */
			/* TODO: Still can setup link during rekey */

			/*
			   return success to avoid supplicant clear TDLS entry;
			   Or we cannot send out any TDLS tear down frame to the peer
			 */
			DBGLOG(TDLS, TRACE, "<tdls_cmd> %s: skip new setup on the exist link!\n", __func__);
			return TDLS_STATUS_SUCCESS;
		}

		prStaRec = NULL;
		break;

	case TDLS_FRM_ACTION_SETUP_RSP:
	case TDLS_FRM_ACTION_CONFIRM:
	case TDLS_FRM_ACTION_TEARDOWN:
		prStaRec = cnmGetStaRecByAddress(prAdapter, (UINT_8) NETWORK_TYPE_AIS_INDEX, prMgmtTxInfo->aucPeer);
#if 0				/* in some cases, the prStaRec is still NULL */
		/*
		   EX: if a peer sends us a TDLS setup request with wrong BSSID,
		   supplicant will not call TdlsexPeerAdd() to create prStaRec and
		   supplicant will send a TDLS setup response with status code 7.

		   So in the case, prStaRec will be NULL.
		 */
		if (prStaRec == NULL) {
			DBGLOG(TDLS, ERROR, "<tdls_cfg> %s: cannot find the peer!\n", __func__);
			return -EINVAL;
		}
#endif
		break;

		/*
		   TODO: Discovery response frame
		   Note that the TDLS Discovery Response frame is not a TDLS frame but a 11
		   Public Action frame.
		   In WiFi TDLS Tech Minutes June 8 2010.doc,
		   a public action frame (i.e. it is no longer an encapsulated data frame)
		 */

	default:
		DBGLOG(TDLS, ERROR,
		       "<tdls_cfg> %s: wrong action_code %d!\n", __func__, prMgmtTxInfo->ucActionCode);
		return TDLS_STATUS_FAILURE;
	}

	/* send the TDLS data frame */
	if (prStaRec != NULL) {
		DBGLOG(TDLS, INFO, "<tdls_cfg> %s: [%pM] ps=%d status=%d\n",
				    __func__, prStaRec->aucMacAddr,
				    prStaRec->fgIsInPS, prMgmtTxInfo->u2StatusCode);

		if (prMgmtTxInfo->ucActionCode == TDLS_FRM_ACTION_TEARDOWN) {
			/* record disconnect history */
			TdlsLinkHistoryRecord(prGlueInfo, TRUE, prMgmtTxInfo->aucPeer,
					      TRUE, prMgmtTxInfo->u2StatusCode, NULL);
		}
	}

	return TdlsDataFrameSend(prAdapter,
				 prStaRec,
				 prMgmtTxInfo->aucPeer,
				 prMgmtTxInfo->ucActionCode,
				 prMgmtTxInfo->ucDialogToken,
				 prMgmtTxInfo->u2StatusCode,
				 (UINT_8 *) prMgmtTxInfo->aucSecBuf, prMgmtTxInfo->u4SecBufLen);
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to add a peer record.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*/
/*----------------------------------------------------------------------------*/
TDLS_STATUS TdlsexPeerAdd(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	GLUE_INFO_T *prGlueInfo;
	TDLS_CMD_PEER_ADD_T *prCmd;
	BSS_INFO_T *prAisBssInfo;
	STA_RECORD_T *prStaRec;
	UINT_8 ucNonHTPhyTypeSet;
	UINT32 u4StartIdx;
	OS_SYSTIME rCurTime;

	/* sanity check */
	DBGLOG(TDLS, INFO, "<tdls_cmd> %s\n", __func__);

	if ((prAdapter == NULL) || (pvSetBuffer == NULL) || (pu4SetInfoLen == NULL)) {
		DBGLOG(TDLS, ERROR, "<tdls_cmd> %s: sanity fail!\n", __func__);
		return TDLS_STATUS_FAILURE;
	}

	/* init */
	prGlueInfo = (GLUE_INFO_T *) prAdapter->prGlueInfo;
	*pu4SetInfoLen = sizeof(TDLS_CMD_PEER_ADD_T);
	prCmd = (TDLS_CMD_PEER_ADD_T *) pvSetBuffer;
	prAisBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX]);
	u4StartIdx = 0;

	/* search old entry */
	prStaRec = cnmGetStaRecByAddress(prAdapter, (UINT_8) NETWORK_TYPE_AIS_INDEX, prCmd->aucPeerMac);

	/* check if any TDLS link exists because we only support one TDLS link currently */
	if (prStaRec == NULL) {
		/* the MAC is new peer */
		prStaRec = cnmStaTheTypeGet(prAdapter, NETWORK_TYPE_AIS_INDEX, STA_TYPE_TDLS_PEER, &u4StartIdx);

		if (prStaRec != NULL) {
			/* a building TDLS link exists */
			DBGLOG(TDLS, ERROR,
			       "<tdls_cmd> %s: one TDLS link setup [%pM] is going...\n",
				__func__, prStaRec->aucMacAddr);

			if (prStaRec->ucStaState != STA_STATE_3) {
				/* check timeout */
				GET_CURRENT_SYSTIME(&rCurTime);

				if (CHECK_FOR_TIMEOUT(rCurTime, prStaRec->rTdlsSetupStartTime,
						      SEC_TO_SYSTIME(TDLS_SETUP_TIMEOUT_SEC))) {
					/* free the StaRec */
					cnmStaRecFree(prAdapter, prStaRec, TRUE);

					DBGLOG(TDLS, ERROR,
					       "<tdls_cmd> %s: free going TDLS link setup [%pM]\n",
						__func__, (prStaRec->aucMacAddr));

					/* handle new setup */
					prStaRec = NULL;
				} else
					return TDLS_STATUS_FAILURE;
			} else {
				/* the TDLS is built and works fine, reject new one */
				return TDLS_STATUS_FAILURE;
			}
		}
	} else {
		if (prStaRec->ucStaState == STA_STATE_3) {
			/* the peer exists, maybe TPK lifetime expired, supplicant wants to renew key */
			TdlsLinkHistoryRecord(prAdapter->prGlueInfo, TRUE,
					      prStaRec->aucMacAddr, TRUE,
					      TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_REKEY, NULL);

			/* 16 Nov 21:49 2012 http://permalink.gmane.org/gmane.linux.kernel.wireless.general/99712 */
			cfg80211_tdls_oper_request(prAdapter->prGlueInfo->prDevHandler,
						   prStaRec->aucMacAddr, TDLS_FRM_ACTION_TEARDOWN,
						   TDLS_REASON_CODE_UNSPECIFIED, GFP_ATOMIC);

			DBGLOG(TDLS, TRACE,
			       "<tdls_cmd> %s: re-setup link for [%pM] maybe re-key?\n",
				__func__, (prStaRec->aucMacAddr));
			return TDLS_STATUS_FAILURE;
		}
	}

	/*
	   create new entry if not exist

	   1. we are initiator
	   (1) send TDLS setup request
	   wpa_sm_tdls_peer_addset(sm, peer->addr, 1, 0, 0, NULL, 0, NULL, NULL, 0, NULL, 0);
	   create a station record with STA_STATE_1.
	   (2) got TDLS setup response and send TDLS setup confirm
	   wpa_tdls_enable_link()
	   update a station record with STA_STATE_3.

	   2. we are responder
	   (1) got TDLS setup request
	   wpa_sm_tdls_peer_addset(sm, peer->addr, 1, 0, 0, NULL, 0, NULL, NULL, 0, NULL, 0);
	   create a station record with STA_STATE_1.
	   (2) send TDLS setup response
	   (3) got TDLS setup confirm
	   wpa_tdls_enable_link()
	   update a station record with STA_STATE_3.
	 */
	if (prStaRec == NULL) {
		prStaRec = cnmStaRecAlloc(prAdapter, (UINT_8) NETWORK_TYPE_AIS_INDEX);

		if (prStaRec == NULL) {
			/* shall not be here */
			DBGLOG(TDLS, ERROR, "<tdls_cmd> %s: alloc prStaRec fail!\n", __func__);
			return TDLS_STATUS_RESOURCES;
		}

		/* init the prStaRec */
		/* prStaRec will be zero first in cnmStaRecAlloc() */
		COPY_MAC_ADDR(prStaRec->aucMacAddr, prCmd->aucPeerMac);

/* cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_1); */
	} else {
#if 0
		if ((prStaRec->ucStaState > STA_STATE_1) && (IS_TDLS_STA(prStaRec))) {
			/*
			   test plan: The STAUT should locally tear down existing TDLS direct link and
			   respond with Set up Response frame.
			 */
			cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_1);
		}
#endif
	}

	/* reference to bssCreateStaRecFromBssDesc() and use our best capability */
	/* reference to assocBuildReAssocReqFrameCommonIEs() to fill elements */

	/* prStaRec->u2CapInfo */
	/* TODO: Need to parse elements from setup request frame */
	prStaRec->u2OperationalRateSet = prAisBssInfo->u2OperationalRateSet;
	prStaRec->u2BSSBasicRateSet = prAisBssInfo->u2BSSBasicRateSet;
	prStaRec->u2DesiredNonHTRateSet = prAdapter->rWifiVar.ucAvailablePhyTypeSet;
	prStaRec->ucPhyTypeSet = prAisBssInfo->ucPhyTypeSet;
	prStaRec->eStaType = STA_TYPE_TDLS_PEER;

	prStaRec->ucDesiredPhyTypeSet =	/*prStaRec->ucPhyTypeSet & */
	    prAdapter->rWifiVar.ucAvailablePhyTypeSet;
	ucNonHTPhyTypeSet = prStaRec->ucDesiredPhyTypeSet & PHY_TYPE_SET_802_11ABG;

	/* check for Target BSS's non HT Phy Types */
	if (ucNonHTPhyTypeSet) {
		if (ucNonHTPhyTypeSet & PHY_TYPE_BIT_ERP) {
			prStaRec->ucNonHTBasicPhyType = PHY_TYPE_ERP_INDEX;
		} else if (ucNonHTPhyTypeSet & PHY_TYPE_BIT_OFDM) {
			prStaRec->ucNonHTBasicPhyType = PHY_TYPE_OFDM_INDEX;
		} else {	/* if (ucNonHTPhyTypeSet & PHY_TYPE_HR_DSSS_INDEX) */

			prStaRec->ucNonHTBasicPhyType = PHY_TYPE_HR_DSSS_INDEX;
		}

		prStaRec->fgHasBasicPhyType = TRUE;
	} else {
		/* use mandatory for 11N only BSS */
/* ASSERT(prStaRec->ucPhyTypeSet & PHY_TYPE_SET_802_11N); */

		prStaRec->ucNonHTBasicPhyType = PHY_TYPE_HR_DSSS_INDEX;
		prStaRec->fgHasBasicPhyType = FALSE;
	}

	/* update non HT Desired Rate Set */
	{
		P_CONNECTION_SETTINGS_T prConnSettings;

		prConnSettings = &(prAdapter->rWifiVar.rConnSettings);
		prStaRec->u2DesiredNonHTRateSet =
		    (prStaRec->u2OperationalRateSet & prConnSettings->u2DesiredNonHTRateSet);
	}

#if 0		/* TdlsexPeerAdd() will be called before we receive setup rsp in TdlsexRxFrameHandle() */
	/* check if the add is from the same peer in the 1st unhandled setup request frame */
	DBGLOG(TDLS, INFO, "<tdls_cmd> %s: [%pM] [%pM]\n",
			    __func__, prGlueInfo->aucTdlsHtPeerMac, prCmd->aucPeerMac);

	if (kalMemCmp(prGlueInfo->aucTdlsHtPeerMac, prCmd->aucPeerMac, 6) == 0) {
		/* copy the HT capability from its setup request */
		kalMemCopy(&prStaRec->rTdlsHtCap, &prGlueInfo->rTdlsHtCap, sizeof(IE_HT_CAP_T));

		prStaRec->ucPhyTypeSet |= PHY_TYPE_SET_802_11N;
		prStaRec->u2DesiredNonHTRateSet |= BIT(RATE_HT_PHY_INDEX);

		/* reset backup */
		kalMemZero(&prGlueInfo->rTdlsHtCap, sizeof(prStaRec->rTdlsHtCap));
		kalMemZero(prGlueInfo->aucTdlsHtPeerMac, sizeof(prGlueInfo->aucTdlsHtPeerMac));

		DBGLOG(TDLS, INFO, "<tdls_cmd> %s: peer is a HT device\n", __func__);
	}
#endif

	/* update WMM: must support due to UAPSD in TDLS link */
	prStaRec->fgIsWmmSupported = TRUE;
	prStaRec->fgIsUapsdSupported = TRUE;

	/* update station record to firmware */
	cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_1);

	/* update time */
	GET_CURRENT_SYSTIME(&prStaRec->rTdlsSetupStartTime);

	DBGLOG(TDLS, INFO, "<tdls_cmd> %s: create a peer [%pM]\n",
			    __func__, (prStaRec->aucMacAddr));

	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to update a peer record.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*/
/*----------------------------------------------------------------------------*/
TDLS_STATUS TdlsexPeerUpdate(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	GLUE_INFO_T *prGlueInfo;
	TDLS_CMD_PEER_UPDATE_T *prCmd;
	BSS_INFO_T *prAisBssInfo;
	STA_RECORD_T *prStaRec;
	IE_HT_CAP_T *prHtCap;

	/* sanity check */
	DBGLOG(TDLS, INFO, "<tdls_cmd> %s\n", __func__);

	if ((prAdapter == NULL) || (pvSetBuffer == NULL) || (pu4SetInfoLen == NULL)) {
		DBGLOG(TDLS, ERROR, "<tdls_cmd> %s: sanity fail!\n", __func__);
		return TDLS_STATUS_FAILURE;
	}

	/* init */
	prGlueInfo = (GLUE_INFO_T *) prAdapter->prGlueInfo;
	*pu4SetInfoLen = sizeof(TDLS_CMD_PEER_ADD_T);
	prCmd = (TDLS_CMD_PEER_UPDATE_T *) pvSetBuffer;
	prAisBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX]);

	/* search old entry */
	prStaRec = cnmGetStaRecByAddress(prAdapter, (UINT_8) NETWORK_TYPE_AIS_INDEX, prCmd->aucPeerMac);

	/*
	   create new entry if not exist

	   1. we are initiator
	   (1) send TDLS setup request
	   wpa_sm_tdls_peer_addset(sm, peer->addr, 1, 0, 0, NULL, 0, NULL, NULL, 0, NULL, 0);
	   create a station record with STA_STATE_1.
	   (2) got TDLS setup response and send TDLS setup confirm
	   wpa_tdls_enable_link()
	   update a station record with STA_STATE_3.

	   2. we are responder
	   (1) got TDLS setup request
	   wpa_sm_tdls_peer_addset(sm, peer->addr, 1, 0, 0, NULL, 0, NULL, NULL, 0, NULL, 0);
	   create a station record with STA_STATE_1.
	   (2) send TDLS setup response
	   (3) got TDLS setup confirm
	   wpa_tdls_enable_link()
	   update a station record with STA_STATE_3.
	 */
	if ((prStaRec == NULL) || (prStaRec->fgIsInUse == 0)) {
		DBGLOG(TDLS, ERROR, "<tdls_cmd> %s: cannot find the peer!\n", __func__);
		return TDLS_STATUS_FAILURE;
	}

	DBGLOG(TDLS, INFO, "<tdls_cmd> %s: update a peer [%pM] %d -> %d, 0x%x\n",
			    __func__, (prStaRec->aucMacAddr),
			    prStaRec->ucStaState, STA_STATE_3, prStaRec->eStaType);

	if (!IS_TDLS_STA(prStaRec)) {
		DBGLOG(TDLS, ERROR, "<tdls_cmd> %s: peer is not TDLS one!\n", __func__);
		return TDLS_STATUS_FAILURE;
	}

	/* check if the add is from the same peer in the 1st unhandled setup request frame */
	DBGLOG(TDLS, INFO, "<tdls_cmd> %s: [%pM] [%pM]\n",
			    __func__, (prGlueInfo->aucTdlsHtPeerMac), (prCmd->aucPeerMac));

	if (kalMemCmp(prGlueInfo->aucTdlsHtPeerMac, prCmd->aucPeerMac, 6) == 0) {
		/* copy the HT capability from its setup request */
		kalMemCopy(&prStaRec->rTdlsHtCap, &prGlueInfo->rTdlsHtCap, sizeof(IE_HT_CAP_T));

		prStaRec->ucPhyTypeSet |= PHY_TYPE_SET_802_11N;
		prStaRec->u2DesiredNonHTRateSet |= BIT(RATE_HT_PHY_INDEX);

		/* reset backup */
		kalMemZero(&prGlueInfo->rTdlsHtCap, sizeof(prStaRec->rTdlsHtCap));
		kalMemZero(prGlueInfo->aucTdlsHtPeerMac, sizeof(prGlueInfo->aucTdlsHtPeerMac));

		DBGLOG(TDLS, INFO, "<tdls_cmd> %s: peer is a HT device\n", __func__);
	}

	/* update the record join time. */
	GET_CURRENT_SYSTIME(&prStaRec->rUpdateTime);

	/* update Station Record - Status/Reason Code */
	prStaRec->u2StatusCode = prCmd->u2StatusCode;

	/* prStaRec->ucStaState shall be STA_STATE_1 */

	prStaRec->u2CapInfo = prCmd->u2Capability;
/*	prStaRec->u2OperationalRateSet */
	prStaRec->u2AssocId = 0;	/* no use */
	prStaRec->u2ListenInterval = 0;	/* unknown */
/*	prStaRec->ucDesiredPhyTypeSet */
/*	prStaRec->u2DesiredNonHTRateSet */
/*	prStaRec->u2BSSBasicRateSet */
/*	prStaRec->ucMcsSet */
/*	prStaRec->fgSupMcs32 */
/*	prStaRec->u2HtCapInfo */
	prStaRec->fgIsQoS = TRUE;
	prStaRec->fgIsUapsdSupported = (prCmd->UapsdBitmap == 0) ? FALSE : TRUE;
/*	prStaRec->ucAmpduParam */
/*	prStaRec->u2HtExtendedCap */
	prStaRec->u4TxBeamformingCap = 0;	/* no use */
	prStaRec->ucAselCap = 0;	/* no use */
	prStaRec->ucRCPI = 0;
	prStaRec->ucBmpTriggerAC = prCmd->UapsdBitmap;
	prStaRec->ucBmpDeliveryAC = prCmd->UapsdBitmap;
	prStaRec->ucUapsdSp = prCmd->UapsdMaxSp;

	/* update HT */
#if (TDLS_CFG_HT_SUP == 1)
	if (prCmd->fgIsSupHt == FALSE) {
		/* no HT IE is from supplicant so we use the backup */
		prHtCap = (IE_HT_CAP_T *) &prStaRec->rTdlsHtCap;

		DBGLOG(TDLS, INFO, "<tdls_cmd> %s: [%pM] update ht ie 0x%x\n",
				    __func__, (prStaRec->aucMacAddr), prHtCap->ucId);

		if (prHtCap->ucId == ELEM_ID_HT_CAP) {
			prStaRec->ucMcsSet = prHtCap->rSupMcsSet.aucRxMcsBitmask[0];
			prStaRec->fgSupMcs32 = (prHtCap->rSupMcsSet.aucRxMcsBitmask[32 / 8] & BIT(0)) ? TRUE : FALSE;

			prStaRec->u2HtCapInfo = prHtCap->u2HtCapInfo;
			prStaRec->ucAmpduParam = prHtCap->ucAmpduParam;
			prStaRec->u2HtExtendedCap = prHtCap->u2HtExtendedCap;
			prStaRec->u4TxBeamformingCap = prHtCap->u4TxBeamformingCap;
			prStaRec->ucAselCap = prHtCap->ucAselCap;
			prStaRec->ucDesiredPhyTypeSet |= PHY_TYPE_SET_802_11N;
		}
	} else {
		/* TODO: use the HT IE from supplicant */
	}
#endif /* TDLS_CFG_HT_SUP */

	DBGLOG(TDLS, INFO, "<tdls_cmd> %s: UAPSD 0x%x %d MCS=0x%x\n",
			    __func__, prCmd->UapsdBitmap, prCmd->UapsdMaxSp, prStaRec->ucMcsSet);

/* cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_3); */

	DBGLOG(TDLS, INFO, "<tdls_cmd> %s: update a peer [%pM]\n",
			    __func__, (prStaRec->aucMacAddr));

	return TDLS_STATUS_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to check if we need to drop a TDLS action frame.
*
* \param[in] *pPkt		Pointer to the struct sk_buff->data.
* \param[in]
* \param[in]
*
* \retval None
*/
/*----------------------------------------------------------------------------*/
BOOLEAN TdlsexRxFrameDrop(GLUE_INFO_T *prGlueInfo, UINT_8 *pPkt)
{
	ADAPTER_T *prAdapter;
	UINT8 ucActionId;

	/* sanity check */
	if ((pPkt == NULL) || (*(pPkt + 12) != 0x89) || (*(pPkt + 13) != 0x0d))
		return FALSE;	/* not TDLS data frame htons(0x890d) */

#if 0				/* supplicant handles this check */
	if (prStaRec == NULL)
		return FALSE;	/* shall not be here */

	DBGLOG(TDLS, INFO,
	       "<tdls_fme> %s: Rcv a TDLS action frame (id=%d) %d %d\n",
		__func__, *(pPkt + 13 + 4), prStaRec->fgTdlsIsProhibited, fgIsPtiTimeoutSkip);

	/* check if TDLS Prohibited bit is set in AP's beacon */
	if (prStaRec->fgTdlsIsProhibited == TRUE)
		return TRUE;
#endif

	ucActionId = *(pPkt + 12 + 2 + 2);	/* skip dst, src MAC, type, payload type, category */

	if (fgIsPtiTimeoutSkip == TRUE) {
		/* also skip any tear down frame from the peer */
		if (ucActionId == TDLS_FRM_ACTION_TEARDOWN)
			return TRUE;
	}

	prAdapter = prGlueInfo->prAdapter;
	DBGLOG(TDLS, INFO,
	       "<tdls_fme> %s: Rcv a TDLS action frame %d (%u)\n",
		__func__, ucActionId, (UINT32) prAdapter->rRxCtrl.rFreeSwRfbList.u4NumElem);

	if (ucActionId == TDLS_FRM_ACTION_TEARDOWN) {
		DBGLOG(TDLS, WARN, "<tdls_fme> %s: Rcv a TDLS tear down frame %d, will DISABLE link\n",
				__func__, *(pPkt + 13 + 4));	/* reason code */

		/* record disconnect history */
		TdlsLinkHistoryRecord(prGlueInfo, TRUE, pPkt + 6, FALSE, *(pPkt + 13 + 4), NULL);

		/* inform tear down to supplicant only in OPEN/NONE mode */
		/*
		   we need to tear down the link manually; or supplicant will display
		   "No FTIE in TDLS Teardown" and it will not tear down the link
		 */
		cfg80211_tdls_oper_request(prGlueInfo->prDevHandler,
					   pPkt + 6, TDLS_FRM_ACTION_TEARDOWN, *(pPkt + 13 + 4), GFP_ATOMIC);
	}
#if 0				/* pass all to supplicant except same thing is handled in supplicant */
	if (((*(pPkt + 13 + 3)) == TDLS_FRM_ACTION_PTI) ||
	    ((*(pPkt + 13 + 3)) == TDLS_FRM_ACTION_CHAN_SWITCH_REQ) ||
	    ((*(pPkt + 13 + 3)) == TDLS_FRM_ACTION_CHAN_SWITCH_RSP) ||
	    ((*(pPkt + 13 + 3)) == TDLS_FRM_ACTION_PTI_RSP)) {
		return TRUE;
	}
#endif

	return FALSE;
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to parse some IEs in the setup frame from the peer.
*
* \param[in] prGlueInfo			Pointer to the Adapter structure
* \param[in] pPkt				Pointer to the ethernet packet
*
* \retval None
*
*/
/*----------------------------------------------------------------------------*/
VOID TdlsexRxFrameHandle(GLUE_INFO_T *prGlueInfo, UINT8 *pPkt, UINT16 u2PktLen)
{
	ADAPTER_T *prAdapter;
	STA_RECORD_T *prStaRec;
	UINT8 ucActionId;
	UINT8 *pucPeerMac, ucElmId, ucElmLen;
	INT16 s2FmeLen;

	/* sanity check */
	if ((prGlueInfo == NULL) || (pPkt == NULL) || (*(pPkt + 12) != 0x89) || (*(pPkt + 13) != 0x0d))
		return;

	ucActionId = *(pPkt + 12 + 2 + 2);	/* skip dst, src MAC, type, payload type, category */

	if ((ucActionId != TDLS_FRM_ACTION_SETUP_REQ) && (ucActionId != TDLS_FRM_ACTION_SETUP_RSP))
		return;

	/* init */
	prAdapter = prGlueInfo->prAdapter;
	pucPeerMac = pPkt + 6;
	s2FmeLen = (INT16) u2PktLen;

	DBGLOG(TDLS, TRACE,
	       "<tdls_fme> %s: get a setup frame %d from %pM\n",
		__func__, ucActionId, (pucPeerMac));

	if (ucActionId == TDLS_FRM_ACTION_SETUP_REQ)
		pPkt += 12 + 2 + 2 + 1 + 1 + 2;	/* skip action, dialog token, capability */
	else
		pPkt += 12 + 2 + 2 + 1 + 2 + 1 + 2;	/* skip action, status code, dialog token, capability */

	/* check station record */
	prStaRec = cnmGetStaRecByAddress(prGlueInfo->prAdapter, (UINT_8) NETWORK_TYPE_AIS_INDEX, pucPeerMac);

	if (prStaRec == NULL) {
		prStaRec = cnmStaRecAlloc(prAdapter, (UINT_8) NETWORK_TYPE_AIS_INDEX);

		if (prStaRec == NULL) {
			/* TODO: only one TDLS entry, need to free old one if timeout */
			DBGLOG(TDLS, ERROR, "<tdls_cmd> %s: alloc prStaRec fail!\n", __func__);
			return;
		}

		/* init the prStaRec */
		/* prStaRec will be zero first in cnmStaRecAlloc() */
		COPY_MAC_ADDR(prStaRec->aucMacAddr, pucPeerMac);

		cnmStaRecChangeState(prAdapter, prStaRec, STA_STATE_1);
	}

	/* backup HT IE to station record */
	/* TODO: Maybe our TDLS only supports non-11n */
	while (s2FmeLen > 0) {
		ucElmId = *pPkt++;
		ucElmLen = *pPkt++;

		switch (ucElmId) {
		case ELEM_ID_HT_CAP:	/* 0x2d */
			/* backup the HT IE of 1st unhandled setup request frame */
			if (prGlueInfo->rTdlsHtCap.ucId == 0x00) {
				kalMemCopy(prGlueInfo->aucTdlsHtPeerMac, pucPeerMac, 6);
				kalMemCopy(&prGlueInfo->rTdlsHtCap, pPkt - 2, ucElmLen + 2);

				/*
				   cannot backup in prStaRec; or

				   1. we build a TDLS link
				   2. peer re-sends setup req
				   3. we backup HT cap element
				   4. supplicant disables the link
				   5. we clear the prStaRec
				 */

				DBGLOG(TDLS, TRACE,
				       "<tdls_fme> %s: %pM: find a HT IE\n",
					__func__, (pucPeerMac));
			}
			return;

		case ELEM_ID_EXTENDED_CAP:
			/* TODO: backup the extended capability IE */
			break;
		}

		pPkt += ucElmLen;
		s2FmeLen -= (2 + ucElmLen);
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to get the TDLS station record.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval	TDLS_STATUS_SUCCESS: this is TDLS packet
*		TDLS_STATUS_FAILURE: this is not TDLS packet
*/
/*----------------------------------------------------------------------------*/
TDLS_STATUS TdlsexStaRecIdxGet(ADAPTER_T *prAdapter, MSDU_INFO_T *prMsduInfo)
{
	BSS_INFO_T *prBssInfo;
	STA_RECORD_T *prStaRec;
	TDLS_STATUS Status;

	/* sanity check */
	if ((prAdapter == NULL) || (prMsduInfo == NULL))
		return TDLS_STATUS_FAILURE;

	if (prAdapter->prGlueInfo == NULL)
		return TDLS_STATUS_FAILURE;
	if (TDLS_IS_NO_LINK_GOING(prAdapter->prGlueInfo))
		return TDLS_STATUS_FAILURE;

	/* init */
	prMsduInfo->ucStaRecIndex = STA_REC_INDEX_NOT_FOUND;
	Status = TDLS_STATUS_SUCCESS;

	/* get record by ether dest */
	prStaRec = cnmGetStaRecByAddress(prAdapter, (UINT_8) NETWORK_TYPE_AIS_INDEX, prMsduInfo->aucEthDestAddr);

	/*
	   TDLS Setup Request frames, TDLS Setup Response frames and TDLS Setup Confirm
	   frames shall be transmitted through the AP and shall not be transmitted to a group
	   address.

	   1. In first time, prStaRec == NULL or prStaRec->ucStaState != STA_STATE_3,
	   we will send them to AP;
	   2. When link is still on, if you command to send TDLS setup from supplicant,
	   supplicant will DISABLE LINK first, prStaRec will be NULL then send TDLS
	   setup frame to the peer.
	 */

	do {
		if ((prStaRec != NULL) && (prStaRec->ucStaState == STA_STATE_3) && (IS_TDLS_STA(prStaRec))) {
			/*
			   TDLS Test Case 5.3 Tear Down
			   Automatically sends TDLS Teardown frame to STA 2 via AP

			   11.21.5 TDLS Direct Link Teardown
			   The TDLS Teardown frame shall be sent over the direct path and the reason
			   code shall be set to "TDLS 40 direct link teardown for unspecified reason",
			   except when the TDLS peer STA is unreachable via the TDLS direct link,
			   in which case, the TDLS Teardown frame shall be sent through the AP and
			   the reason code shall be set to "TDLS direct link teardown due to TDLS peer
			   STA unreachable via the TDLS direct link".
			 */
			/* if (prStaRec->fgIsInPS == TRUE) */
			/*
			   check if the packet is tear down:
			   we do not want to use PTI to indicate the tear down and
			   we want to send the tear down to AP then AP help us to send it
			 */
			struct sk_buff *prSkb;
			UINT8 *pEth;
			UINT_16 u2EtherTypeLen;

			prSkb = (struct sk_buff *)prMsduInfo->prPacket;
			if (prSkb != NULL) {
				UINT8 ucActionCode, ucReasonCode;

				/* init */
				pEth = prSkb->data;
				u2EtherTypeLen = (pEth[ETH_TYPE_LEN_OFFSET] << 8) |
				    (pEth[ETH_TYPE_LEN_OFFSET + 1]);
				ucActionCode = pEth[ETH_TYPE_LEN_OFFSET + 1 + 3];
				ucReasonCode = pEth[ETH_TYPE_LEN_OFFSET + 1 + 4] |
				    (pEth[ETH_TYPE_LEN_OFFSET + 1 + 5] << 8);

				/* TDLS_REASON_CODE_UNREACHABLE: keep alive fail or PTI timeout */
				if ((u2EtherTypeLen == TDLS_FRM_PROT_TYPE) &&
					(ucActionCode == TDLS_FRM_ACTION_TEARDOWN) &&
					(ucReasonCode == TDLS_REASON_CODE_UNREACHABLE)) {
					/*
					   when we cannot reach the peer,
					   we need AP's help to send the tear down frame
					 */
					prBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX]);
					prStaRec = prBssInfo->prStaRecOfAP;
					if (prStaRec == NULL) {
						Status = TDLS_STATUS_FAILURE;
						break;
					}
#if 0
					/* change status code */
					pEth[ETH_TYPE_LEN_OFFSET + 1 + 4] = TDLS_REASON_CODE_UNREACHABLE;
#endif
				}
			}
			prMsduInfo->ucStaRecIndex = prStaRec->ucIndex;
		}
	} while (FALSE);

	DBGLOG(TDLS, INFO, "<tdls> %s: (Status=%x) [%pM] ucStaRecIndex = %d!\n",
			    __func__, (INT32) Status, (prMsduInfo->aucEthDestAddr),
			    prMsduInfo->ucStaRecIndex);
	return Status;
}

/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to check if we suffer timeout for TX quota empty case.
*
* \param[in] prAdapter			Pointer to the Adapter structure
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID TdlsexTxQuotaCheck(GLUE_INFO_T *prGlueInfo, STA_RECORD_T *prStaRec, UINT8 FreeQuota)
{
	OS_SYSTIME rCurTime;

	/* sanity check */
	if (!IS_TDLS_STA(prStaRec))
		return;

	if (FreeQuota != 0) {
		/* reset timeout */
		prStaRec->rTdlsTxQuotaEmptyTime = 0;
		return;
	}

	/* work-around: check if the no free quota case is too long */
	GET_CURRENT_SYSTIME(&rCurTime);

	if (prStaRec->rTdlsTxQuotaEmptyTime == 0) {
		prStaRec->rTdlsTxQuotaEmptyTime = rCurTime;
	} else {
		if (CHECK_FOR_TIMEOUT(rCurTime, prStaRec->rTdlsTxQuotaEmptyTime,
				      SEC_TO_SYSTIME(TDLS_TX_QUOTA_EMPTY_TIMEOUT))) {
			/* tear down the link */
			DBGLOG(TDLS, WARN,
			       "<tdls> %s: [%pM] TX quota empty timeout!\n",
				__func__, (prStaRec->aucMacAddr));

			/* record disconnect history */
			TdlsLinkHistoryRecord(prGlueInfo, TRUE, prStaRec->aucMacAddr,
					      TRUE, TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_TX_QUOTA_EMPTY, NULL);

			/* inform tear down to supplicant only in OPEN/NONE mode */
			/*
			   we need to tear down the link manually; or supplicant will display
			   "No FTIE in TDLS Teardown" and it will not tear down the link
			 */
			cfg80211_tdls_oper_request(prGlueInfo->prDevHandler,
						   prStaRec->aucMacAddr, TDLS_FRM_ACTION_TEARDOWN,
						   TDLS_REASON_CODE_UNREACHABLE, GFP_ATOMIC);
		}
	}
}

/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to un-initialize variables in TDLS.
*
* \param[in] prAdapter			Pointer to the Adapter structure
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID TdlsexUninit(ADAPTER_T *prAdapter)
{
#if TDLS_CFG_CMD_TEST
	cnmTimerStopTimer(prAdapter, &rTdlsTimerTestDataSend);
#endif /* TDLS_CFG_CMD_TEST */
}

#endif /* CFG_SUPPORT_TDLS */

/* End of tdls.c */
