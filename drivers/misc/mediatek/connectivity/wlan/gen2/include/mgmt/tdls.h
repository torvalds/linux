/*
** Id: include/tdls.h#1
*/

/*! \file   "tdls.h"
    \brief This file contains the internal used in TDLS modules
	 for MediaTek Inc. 802.11 Wireless LAN Adapters.
*/

/*
** Log: tdls.h
 *
 * 11 18 2013 vend_samp.lin
 * NULL
 * Initial version.
 *
 **
 */

#ifndef _TDLS_H
#define _TDLS_H

#if (CFG_SUPPORT_TDLS == 1)

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/
#define TDLS_CFG_CMD_TEST			1
#define TDLS_CFG_HT_SUP				1

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
extern int wlanHardStartXmit(struct sk_buff *prSkb, struct net_device *prDev);
extern BOOLEAN flgTdlsTestExtCapElm;
extern UINT8 aucTdlsTestExtCapElm[];
/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
typedef struct _TDLS_LINK_HIS_OTHERS_T {
	BOOLEAN fgIsHt;		/* TRUE: HT device */

} TDLS_LINK_HIS_OTHERS_T;

/* command */
typedef enum _TDLS_CMD_ID {
	TDLS_CMD_TEST_TX_FRAME = 0x00,
	TDLS_CMD_TEST_RCV_FRAME = 0x01,
	TDLS_CMD_TEST_PEER_ADD = 0x02,
	TDLS_CMD_TEST_PEER_UPDATE = 0x03,
	TDLS_CMD_TEST_DATA_FRAME = 0x04,
	TDLS_CMD_TEST_RCV_NULL = 0x05,
	TDLS_CMD_MIB_UPDATE = 0x06,
	TDLS_CMD_TEST_SKIP_TX_FAIL = 0x07,
	TDLS_CMD_UAPSD_CONF = 0x08,
	TDLS_CMD_CH_SW_CONF = 0x09,
	TDLS_CMD_TEST_SKIP_KEEP_ALIVE = 0x0a,
	TDLS_CMD_TEST_SKIP_CHSW_TIMEOUT = 0x0b,
	TDLS_CMD_TEST_TX_TDLS_FRAME = 0x0c,
	TDLS_CMD_TEST_PROHIBIT_SET_IN_AP = 0x0d,
	TDLS_CMD_TEST_SCAN_DISABLE = 0x0e,
	TDLS_CMD_TEST_DATA_FRAME_CONT = 0x0f,
	TDLS_CMD_TEST_CH_SW_PROHIBIT_SET_IN_AP = 0x10,
	TDLS_CMD_SETUP_CONF = 0x11,
	TDLS_CMD_INFO = 0x12,
	TDLS_CMD_TEST_DELAY = 0x13,
	TDLS_CMD_KEY_INFO = 0x14,
	TDLS_CMD_TEST_PTI_TX_FAIL = 0x15
} TDLS_CMD_ID;

typedef enum _TDLS_EVENT_HOST_ID {
	TDLS_HOST_EVENT_TEAR_DOWN = 0x00,	/* TDLS_EVENT_HOST_SUBID_TEAR_DOWN */
	TDLS_HOST_EVENT_TX_DONE,
	TDLS_HOST_EVENT_FME_STATUS,	/* TDLS_EVENT_HOST_SUBID_SPECIFIC_FRAME */
	TDLS_HOST_EVENT_STATISTICS
} TDLS_EVENT_HOST_ID;

typedef enum _TDLS_EVENT_HOST_SUBID_TEAR_DOWN {
	TDLS_HOST_EVENT_TD_PTI_TIMEOUT = 0x00,
	TDLS_HOST_EVENT_TD_AGE_TIMEOUT,
	TDLS_HOST_EVENT_TD_PTI_SEND_FAIL,
	TDLS_HOST_EVENT_TD_PTI_SEND_MAX_FAIL,
	TDLS_HOST_EVENT_TD_WRONG_NETWORK_IDX,
	TDLS_HOST_EVENT_TD_NON_STATE3,
	TDLS_HOST_EVENT_TD_LOST_TEAR_DOWN
} TDLS_EVENT_HOST_SUBID_TEAR_DOWN;

typedef enum _TDLS_EVENT_HOST_SUBID_SPECIFIC_FRAME {
	TDLS_HOST_EVENT_SF_BA,
	TDLS_HOST_EVENT_SF_BA_OK,
	TDLS_HOST_EVENT_SF_BA_DECLINE,
	TDLS_HOST_EVENT_SF_BA_PEER,
	TDLS_HOST_EVENT_SF_BA_RSP_OK,
	TDLS_HOST_EVENT_SF_BA_RSP_DECLINE
} TDLS_EVENT_HOST_SUBID_SPECIFIC_FRAME;

/* payload specific type in the LLC/SNAP header */
#define TDLS_FRM_PAYLOAD_TYPE						2

#define TDLS_FRM_CATEGORY							12

typedef enum _TDLS_FRM_ACTION_ID {
	TDLS_FRM_ACTION_SETUP_REQ = 0x00,
	TDLS_FRM_ACTION_SETUP_RSP,
	TDLS_FRM_ACTION_CONFIRM,
	TDLS_FRM_ACTION_TEARDOWN,
	TDLS_FRM_ACTION_PTI,
	TDLS_FRM_ACTION_CHAN_SWITCH_REQ,
	TDLS_FRM_ACTION_CHAN_SWITCH_RSP,
	TDLS_FRM_ACTION_PEER_PSM_REQ,
	TDLS_FRM_ACTION_PEER_PSM_RSP,
	TDLS_FRM_ACTION_PTI_RSP,	/* 0x09 */
	TDLS_FRM_ACTION_DISCOVERY_REQ,

	TDLS_FRM_ACTION_EVENT_TEAR_DOWN_TO_SUPPLICANT = 0x30,

	TDLS_FRM_DATA_TEST_DATA = 0x80
} TDLS_FRM_ACTION_ID;

#define TDLS_FRM_ACTION_DISCOVERY_RESPONSE			14

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
/* 7.3.2.62 Link Identifier element */
#define ELEM_ID_LINK_IDENTIFIER						101
#define ELEM_LEN_LINK_IDENTIFIER					18

typedef struct _IE_LINK_IDENTIFIER_T {
	UINT_8 ucId;
	UINT_8 ucLength;
	UINT_8 aBSSID[6];
	UINT_8 aInitiator[6];
	UINT_8 aResponder[6];
} __KAL_ATTRIB_PACKED__ IE_LINK_IDENTIFIER_T;

#define TDLS_LINK_IDENTIFIER_IE(__ie__)	((IE_LINK_IDENTIFIER_T *)(__ie__))

/* test command use */
typedef struct _PARAM_CUSTOM_TDLS_CMD_STRUCT_T {

	UINT_8 ucFmeType;	/* TDLS_FRM_ACTION_ID */

	UINT_8 ucToken;
	UINT_16 u2Cap;

	/* bit0: TDLS, bit1: Peer U-APSD Buffer, bit2: Channel Switching */
#define TDLS_EX_CAP_PEER_UAPSD				BIT(0)
#define TDLS_EX_CAP_CHAN_SWITCH				BIT(1)
#define TDLS_EX_CAP_TDLS					BIT(2)
	UINT_8 ucExCap;

	UINT_8 arSupRate[4];
	UINT_8 arSupChan[4];

	UINT_32 u4Timeout;

#define TDLS_FME_MAC_ADDR_LEN				6
	UINT_8 arRspAddr[TDLS_FME_MAC_ADDR_LEN];
	UINT_8 arBssid[TDLS_FME_MAC_ADDR_LEN];

/*
	Linux Kernel-3.10
	struct station_parameters {
		const u8 *supported_rates;
		struct net_device *vlan;
		u32 sta_flags_mask, sta_flags_set;
		u32 sta_modify_mask;
		int listen_interval;
		u16 aid;
		u8 supported_rates_len;
		u8 plink_action;
		u8 plink_state;
		const struct ieee80211_ht_cap *ht_capa;
		const struct ieee80211_vht_cap *vht_capa;
		u8 uapsd_queues;
		u8 max_sp;
		enum nl80211_mesh_power_mode local_pm;
		u16 capability;
		const u8 *ext_capab;
		u8 ext_capab_len;
	};
*/
	struct ieee80211_ht_cap rHtCapa;
	struct ieee80211_vht_cap rVhtCapa; /* LINUX_KERNEL_VERSION >= 3.10.0 */
	struct station_parameters rPeerInfo;

} PARAM_CUSTOM_TDLS_CMD_STRUCT_T;

typedef struct _TDLS_MGMT_TX_INFO {
	UINT8 aucPeer[6];
	UINT8 ucActionCode;
	UINT8 ucDialogToken;
	UINT16 u2StatusCode;
	UINT32 u4SecBufLen;
	UINT8 aucSecBuf[1000];
} TDLS_MGMT_TX_INFO;

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

/* check any TDLS link */
#define TDLS_IS_NO_LINK_GOING(__GlueInfo__)									\
	((__GlueInfo__)->rTdlsLink.cLinkCnt == 0)

/* increase TDLS link count */
#define TDLS_LINK_INCREASE(__GlueInfo__)									\
	((__GlueInfo__)->rTdlsLink.cLinkCnt++)

/* decrease TDLS link count */
#define TDLS_LINK_DECREASE(__GlueInfo__) \
do { \
	if ((__GlueInfo__)->rTdlsLink.cLinkCnt > 0) \
		(__GlueInfo__)->rTdlsLink.cLinkCnt--; \
} while (0)

/* get TDLS link count */
#define TDLS_LINK_COUNT(__GlueInfo__)										\
	((__GlueInfo__)->rTdlsLink.cLinkCnt)

/* reset TDLS link count */
#define TDLS_LINK_COUNT_RESET(__GlueInfo__)									\
	((__GlueInfo__)->rTdlsLink.cLinkCnt = 0)

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
/* Note: these functions are used only in tdls module, not other modules */
UINT_32 TdlsFrameGeneralIeAppend(ADAPTER_T *prAdapter, STA_RECORD_T *prStaRec, UINT_16 u2StatusCode, UINT_8 *pPkt);

TDLS_STATUS
TdlsDataFrameSend(ADAPTER_T *prAdapter,
		  STA_RECORD_T *prStaRec,
		  UINT_8 *pPeerMac,
		  UINT_8 ucActionCode,
		  UINT_8 ucDialogToken, UINT_16 u2StatusCode, UINT_8 *pAppendIe, UINT_32 AppendIeLen);

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* CFG_SUPPORT_TDLS */

#endif /* _TDLS_H */
