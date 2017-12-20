/*
** Id: include/tdls_extr.h#1
*/

/*! \file   "tdls_extr.h"
    \brief This file contains the external used in other modules
	 for MediaTek Inc. 802.11 Wireless LAN Adapters.
*/

/*
** Log: tdls_extr.h
 *
 * 11 18 2013 vend_samp.lin
 * NULL
 * Initial version.
 *
 **
 */

#ifndef _TDLS_EXTR_H
#define _TDLS_EXTR_H

#if (CFG_SUPPORT_TDLS == 1)

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#define TDLS_TX_QUOTA_EMPTY_TIMEOUT			10

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
/* protocol */
#define TDLS_FRM_PROT_TYPE					0x890d

/*	TDLS uses Ethertype 89-0d frames. The UP shall be AC_VI, unless otherwise specified. */
#define USER_PRIORITY_TDLS					5

/* Status code */
#define TDLS_STATUS							WLAN_STATUS

#define TDLS_STATUS_SUCCESS					WLAN_STATUS_SUCCESS
#define TDLS_STATUS_FAILURE					WLAN_STATUS_FAILURE
#define TDLS_STATUS_INVALID_LENGTH			WLAN_STATUS_INVALID_LENGTH
#define TDLS_STATUS_RESOURCES				WLAN_STATUS_RESOURCES

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
#define TDLS_U32							UINT32
#define TDLS_U16							UINT16
#define TDLS_U8								UINT8

typedef enum _TDLS_REASON_CODE {
	TDLS_REASON_CODE_UNREACHABLE = 25,
	TDLS_REASON_CODE_UNSPECIFIED = 26,

	TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_UNKNOWN = 0x80,	/* 128 */
	TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_WIFI_OFF = 0x81,	/* 129 */
	TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_ROAMING = 0x82,	/* 130 */
	TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_PTI_TIMEOUT = 0x83,	/* 131 */
	TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_AGE_TIMEOUT = 0x84,	/* 132 */
	TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_REKEY = 0x85,	/* 133 */
	TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_PTI_SEND_FAIL = 0x86,	/* 134 */
	TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_PTI_SEND_MAX_FAIL = 0x87,	/* 135 */
	TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_WRONG_NETWORK_IDX = 0x88,	/* 136 */
	TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_NON_STATE3 = 0x89,	/* 137 */
	TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_TX_QUOTA_EMPTY = 0x8a,	/* 138 */
	TDLS_REASON_CODE_MTK_DIS_BY_US_DUE_TO_LOST_TEAR_DOWN = 0x8b	/* 139 */
} TDLS_REASON_CODE;

/* TDLS FSM */
typedef struct _TDLS_CMD_PEER_ADD_T {

	TDLS_U8 aucPeerMac[6];

#if 0
	ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex;
	UINT_16 u2CapInfo;

	UINT_16 u2OperationalRateSet;
	UINT_16 u2BSSBasicRateSet;
	BOOLEAN fgIsUnknownBssBasicRate;

	UINT_8 ucPhyTypeSet;
#endif
} TDLS_CMD_PEER_ADD_T;

typedef struct _TDLS_CMD_LINK_T {

	TDLS_U8 aucPeerMac[6];
	BOOLEAN fgIsEnabled;
} TDLS_CMD_LINK_T;

typedef struct _TDLS_CMD_PEER_UPDATE_HT_CAP_MCS_INFO_T {
	TDLS_U8 arRxMask[10];
	TDLS_U16 u2RxHighest;
	TDLS_U8 ucTxParams;
	TDLS_U8 Reserved[3];
} TDLS_CMD_PEER_UPDATE_HT_CAP_MCS_INFO_T;

typedef struct _TDLS_CMD_PEER_UPDATE_HT_CAP_T {
	TDLS_U16 u2CapInfo;
	TDLS_U8 ucAmpduParamsInfo;

	/* 16 bytes MCS information */
	TDLS_CMD_PEER_UPDATE_HT_CAP_MCS_INFO_T rMCS;

	TDLS_U16 u2ExtHtCapInfo;
	TDLS_U32 u4TxBfCapInfo;
	TDLS_U8 ucAntennaSelInfo;
} TDLS_CMD_PEER_UPDATE_HT_CAP_T;

typedef struct _TDLS_CMD_PEER_UPDATE_T {

	TDLS_U8 aucPeerMac[6];

#define TDLS_CMD_PEER_UPDATE_SUP_CHAN_MAX			50
	TDLS_U8 aucSupChan[TDLS_CMD_PEER_UPDATE_SUP_CHAN_MAX];

	TDLS_U16 u2StatusCode;

#define TDLS_CMD_PEER_UPDATE_SUP_RATE_MAX			50
	TDLS_U8 aucSupRate[TDLS_CMD_PEER_UPDATE_SUP_RATE_MAX];
	TDLS_U16 u2SupRateLen;

	TDLS_U8 UapsdBitmap;
	TDLS_U8 UapsdMaxSp;	/* MAX_SP */

	TDLS_U16 u2Capability;
#define TDLS_CMD_PEER_UPDATE_EXT_CAP_MAXLEN			5
	TDLS_U8 aucExtCap[TDLS_CMD_PEER_UPDATE_EXT_CAP_MAXLEN];
	TDLS_U16 u2ExtCapLen;

	TDLS_CMD_PEER_UPDATE_HT_CAP_T rHtCap;
	BOOLEAN fgIsSupHt;

} TDLS_CMD_PEER_UPDATE_T;

/* Command to TDLS core module */
typedef enum _TDLS_CMD_CORE_ID {
	TDLS_CORE_CMD_TEST_NULL_RCV = 0x00,
	TDLS_CORE_CMD_TEST_PTI_RSP = 0x01,
	TDLS_CORE_CMD_MIB_UPDATE = 0x02,
	TDLS_CORE_CMD_TEST_TX_FAIL_SKIP = 0x03,
	TDLS_CORE_CMD_UAPSD_CONF = 0x04,
	TDLS_CORE_CMD_TEST_DATA_RCV = 0x05,
	TDLS_CORE_CMD_TEST_PTI_REQ = 0x06,
	TDLS_CORE_CMD_TEST_CHSW_REQ = 0x07,
	TDLS_CORE_CMD_CHSW_CONF = 0x08,
	TDLS_CORE_CMD_TEST_KEEP_ALIVE_SKIP = 0x09,
	TDLS_CORE_CMD_TEST_CHSW_TIMEOUT_SKIP = 0x0a,
	TDLS_CORE_CMD_TEST_CHSW_RSP = 0x0b,
	TDLS_CORE_CMD_TEST_SCAN_SKIP = 0x0c,
	TDLS_CORE_CMD_SETUP_CONF = 0x0d,
	TDLS_CORE_CMD_TEST_TEAR_DOWN = 0x0e,
	TDLS_CORE_CMD_KEY_INFO = 0x0f,
	TDLS_CORE_CMD_TEST_PTI_TX_FAIL = 0x10
} TDLS_CMD_CORE_ID;

typedef struct _TDLS_CMD_CORE_TEST_NULL_RCV_T {

	TDLS_U32 u4PM;
} TDLS_CMD_CORE_TEST_NULL_RCV_T;

typedef struct _TDLS_CMD_CORE_TEST_PTI_REQ_RCV_T {

	TDLS_U32 u4DialogToken;
} TDLS_CMD_CORE_TEST_PTI_REQ_RCV_T;

typedef struct _TDLS_CMD_CORE_TEST_PTI_RSP_RCV_T {

	TDLS_U32 u4DialogToken;
	TDLS_U32 u4PM;
} TDLS_CMD_CORE_TEST_PTI_RSP_RCV_T;

typedef struct _TDLS_CMD_CORE_TEST_TEAR_DOWN_RCV_T {

	TDLS_U32 u4ReasonCode;
} TDLS_CMD_CORE_TEST_TEAR_DOWN_RCV_T;

typedef struct _TDLS_CMD_CORE_TEST_CHST_REQ_RCV_T {

	TDLS_U32 u4Chan;
	TDLS_U32 u4RegClass;
	TDLS_U32 u4SecChanOff;
	TDLS_U32 u4SwitchTime;
	TDLS_U32 u4SwitchTimeout;
} TDLS_CMD_CORE_TEST_CHST_REQ_RCV_T;

typedef struct _TDLS_CMD_CORE_TEST_CHST_RSP_RCV_T {

	TDLS_U32 u4Chan;
	TDLS_U32 u4SwitchTime;
	TDLS_U32 u4SwitchTimeout;
	TDLS_U32 u4StatusCode;
} TDLS_CMD_CORE_TEST_CHST_RSP_RCV_T;

typedef struct _TDLS_CMD_CORE_TEST_DATA_RCV_T {

	TDLS_U32 u4PM;
	TDLS_U32 u4UP;
	TDLS_U32 u4EOSP;
	TDLS_U32 u4IsNull;
} TDLS_CMD_CORE_TEST_DATA_RCV_T;

typedef struct _TDLS_CMD_CORE_MIB_PARAM_UPDATE_T {

	BOOLEAN Tdlsdot11TunneledDirectLinkSetupImplemented;
	BOOLEAN Tdlsdot11TDLSPeerUAPSDBufferSTAActivated;
	BOOLEAN Tdlsdot11TDLSPeerPSMActivated;
	TDLS_U16 Tdlsdot11TDLSPeerUAPSDIndicationWindow;
	BOOLEAN Tdlsdot11TDLSChannelSwitchingActivated;
	TDLS_U8 Tdlsdot11TDLSPeerSTAMissingAckRetryLimit;
	TDLS_U8 Tdlsdot11TDLSResponseTimeout;
	TDLS_U16 Tdlsdot11TDLSProbeDelay;
	TDLS_U8 Tdlsdot11TDLSDiscoveryRequestWindow;
	TDLS_U8 Tdlsdot11TDLSACDeterminationInterval;
} TDLS_CMD_CORE_MIB_PARAM_UPDATE_T;

typedef struct _TDLS_CMD_CORE_TEST_TX_FAIL_SKIP_T {

	BOOLEAN fgIsEnable;
} TDLS_CMD_CORE_TEST_TX_FAIL_SKIP_T;

typedef struct _TDLS_CMD_CORE_UAPSD_CONFIG_T {

	BOOLEAN fgIsSpTimeoutSkip;
	BOOLEAN fgIsPtiTimeoutSkip;
} TDLS_CMD_CORE_UAPSD_CONFIG_T;

typedef struct _TDLS_CMD_CORE_SETUP_CONFIG_T {

	BOOLEAN fgIs2040Supported;
} TDLS_CMD_CORE_SETUP_CONFIG_T;

typedef struct _TDLS_CMD_CORE_CHSW_CONFIG_T {

	TDLS_U8 ucNetTypeIndex;
	BOOLEAN fgIsChSwEnabled;
	BOOLEAN fgIsChSwStarted;
	TDLS_U8 ucRegClass;
	TDLS_U8 ucTargetChan;
	TDLS_U8 ucSecChanOff;
	BOOLEAN fgIsChSwRegular;
} TDLS_CMD_CORE_CHSW_CONFIG_T;

typedef struct _TDLS_CMD_CORE_TEST_KEEP_ALIVE_SKIP_T {

	BOOLEAN fgIsEnable;
} TDLS_CMD_CORE_TEST_KEEP_ALIVE_SKIP_T;

typedef struct _TDLS_CMD_CORE_TEST_CHSW_TIMEOUT_SKIP_T {

	BOOLEAN fgIsEnable;
} TDLS_CMD_CORE_TEST_CHSW_TIMEOUT_SKIP_T;

typedef struct _TDLS_CMD_CORE_TEST_PROHIBIT_T {

	BOOLEAN fgIsEnable;
	BOOLEAN fgIsSet;
} TDLS_CMD_CORE_TEST_PROHIBIT_T;

typedef struct _TDLS_CMD_CORE_TEST_SCAN_SKIP_T {

	BOOLEAN fgIsEnable;
} TDLS_CMD_CORE_TEST_SCAN_SKIP_T;

typedef struct _TDLS_CMD_CORE_INFO_DISPLAY_T {

	BOOLEAN fgIsToClearAllHistory;
} TDLS_CMD_CORE_INFO_DISPLAY_T;

typedef struct _TDLS_CMD_CORE_TEST_PTI_TX_FAIL_T {

	BOOLEAN fgIsEnable;
} TDLS_CMD_CORE_TEST_PTI_TX_FAIL_T;

typedef struct _TDLS_CMD_CORE_T {

	TDLS_U32 u4Command;	/* TDLS_CMD_CORE_ID */

	TDLS_U8 aucPeerMac[6];
	TDLS_U8 ucNetTypeIndex;

#define TDLS_CMD_CORE_RESERVED_SIZE					50
	union {
		TDLS_CMD_CORE_TEST_NULL_RCV_T rCmdNullRcv;
		TDLS_CMD_CORE_TEST_PTI_REQ_RCV_T rCmdPtiReqRcv;
		TDLS_CMD_CORE_TEST_PTI_RSP_RCV_T rCmdPtiRspRcv;
		TDLS_CMD_CORE_TEST_TEAR_DOWN_RCV_T rCmdTearDownRcv;
		TDLS_CMD_CORE_TEST_CHST_REQ_RCV_T rCmdChStReqRcv;
		TDLS_CMD_CORE_TEST_CHST_RSP_RCV_T rCmdChStRspRcv;
		TDLS_CMD_CORE_TEST_DATA_RCV_T rCmdDatRcv;
		TDLS_CMD_CORE_TEST_TX_FAIL_SKIP_T rCmdTxFailSkip;
		TDLS_CMD_CORE_TEST_KEEP_ALIVE_SKIP_T rCmdKeepAliveSkip;
		TDLS_CMD_CORE_TEST_CHSW_TIMEOUT_SKIP_T rCmdChSwTimeoutSkip;
		TDLS_CMD_CORE_TEST_PROHIBIT_T rCmdProhibit;
		TDLS_CMD_CORE_TEST_SCAN_SKIP_T rCmdScanSkip;
		TDLS_CMD_CORE_TEST_PTI_TX_FAIL_T rCmdPtiTxFail;

		TDLS_CMD_CORE_MIB_PARAM_UPDATE_T rCmdMibUpdate;
		TDLS_CMD_CORE_UAPSD_CONFIG_T rCmdUapsdConf;
		TDLS_CMD_CORE_CHSW_CONFIG_T rCmdChSwConf;
		TDLS_CMD_CORE_SETUP_CONFIG_T rCmdSetupConf;
		TDLS_CMD_CORE_INFO_DISPLAY_T rCmdInfoDisplay;
		TDLS_U8 Reserved[TDLS_CMD_CORE_RESERVED_SIZE];
	} Content;
} TDLS_CMD_CORE_T;

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
/*
	assign station record idx for the packet only when STA_STATE_3
	Or we will try to send data frame when the TDLS peer's state is STA_STATE_1
	EX:
		1. mtk_cfg80211_add_station: First create the STA_RECORD_T;
		2. TdlsexCfg80211TdlsMgmt: Send a TDLS request frame.
		3. mtk_cfg80211_add_station: Change state to STA_STATE_1.
		4. TdlsexCfg80211TdlsMgmt: Send a TDLS request frame.
*/
#define TDLSEX_STA_REC_IDX_GET(__prAdapter__, __MsduInfo__) \
{ \
	STA_RECORD_T *__StaRec__; \
	__MsduInfo__->ucStaRecIndex = STA_REC_INDEX_NOT_FOUND; \
	__StaRec__ = cnmGetStaRecByAddress(__prAdapter__, \
						(UINT_8) NETWORK_TYPE_AIS_INDEX, \
						__MsduInfo__->aucEthDestAddr); \
	if ((__StaRec__ != NULL) && \
		((__StaRec__)->ucStaState == STA_STATE_3) && \
		(IS_TDLS_STA(__StaRec__))) { \
		__MsduInfo__->ucStaRecIndex = __StaRec__->ucIndex; \
	} \
}

/* fill wiphy flag */
#define TDLSEX_WIPHY_FLAGS_INIT(__fgFlag__)\
{ \
	__fgFlag__ |= (WIPHY_FLAG_SUPPORTS_TDLS | WIPHY_FLAG_TDLS_EXTERNAL_SETUP);\
}

/* assign user priority of a TDLS action frame */
/*
	According to 802.11z: Setup req/resp are sent in AC_BK, otherwise we should default
	to AC_VI.
*/
#define TDLSEX_UP_ASSIGN(__UserPriority__) \
{ \
	__UserPriority__ = USER_PRIORITY_TDLS; \
}

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

int
TdlsexCfg80211TdlsMgmt(struct wiphy *wiphy, struct net_device *dev,
		       const u8 *peer, u8 action_code, u8 dialog_token,
		       u16 status_code, u32 peer_capability,
		       bool initiator, const u8 *buf, size_t len);

int TdlsexCfg80211TdlsOper(struct wiphy *wiphy, struct net_device *dev,
			const u8 *peer, enum nl80211_tdls_operation oper);

VOID TdlsexCmd(P_GLUE_INFO_T prGlueInfo, UINT_8 *prInBuf, UINT_32 u4InBufLen);

VOID TdlsexBssExtCapParse(STA_RECORD_T *prStaRec, UINT_8 *pucIE);

VOID TdlsexEventHandle(P_GLUE_INFO_T prGlueInfo, UINT8 *prInBuf, UINT32 u4InBufLen);

TDLS_STATUS TdlsexKeyHandle(ADAPTER_T *prAdapter, PARAM_KEY_T *prNewKey);

VOID TdlsexInit(ADAPTER_T *prAdapter);

BOOLEAN TdlsexIsAnyPeerInPowerSave(ADAPTER_T *prAdapter);

TDLS_STATUS TdlsexLinkCtrl(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

VOID
TdlsexLinkHistoryRecord(GLUE_INFO_T *prGlueInfo,
			BOOLEAN fgIsTearDown, UINT8 *pucPeerMac, BOOLEAN fgIsFromUs, UINT16 u2ReasonCode);

TDLS_STATUS TdlsexMgmtCtrl(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

TDLS_STATUS TdlsexPeerAdd(P_ADAPTER_T prAdapter, PVOID pvSetBuffer, UINT_32 u4SetBufferLen, PUINT_32 pu4SetInfoLen);

TDLS_STATUS TdlsexPeerUpdate(P_ADAPTER_T prAdapter, PVOID pvSetBuffer, UINT_32 u4SetBufferLen, PUINT_32 pu4SetInfoLen);

BOOLEAN TdlsexRxFrameDrop(GLUE_INFO_T *prGlueInfo, UINT_8 *pPkt);

VOID TdlsexRxFrameHandle(GLUE_INFO_T *prGlueInfo, UINT8 *pPkt, UINT16 u2PktLen);

TDLS_STATUS TdlsexStaRecIdxGet(ADAPTER_T *prAdapter, MSDU_INFO_T *prMsduInfo);

VOID TdlsexTxQuotaCheck(GLUE_INFO_T *prGlueInfo, STA_RECORD_T *prStaRec, UINT8 FreeQuota);

VOID TdlsexUninit(ADAPTER_T *prAdapter);

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* CFG_SUPPORT_TDLS */

#endif /* _TDLS_EXTR_H */
