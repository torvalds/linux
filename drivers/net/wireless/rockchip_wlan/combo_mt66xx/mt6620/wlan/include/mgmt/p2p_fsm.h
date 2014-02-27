/*
** $Id: //Department/DaVinci/TRUNK/WiFi_P2P_Driver/include/mgmt/p2p_fsm.h#23 $
*/

/*! \file   p2p_fsm.h
    \brief  Declaration of functions and finite state machine for P2P Module.

    Declaration of functions and finite state machine for P2P Module.
*/



/*
** $Log: p2p_fsm.h $
** 
** 09 12 2012 wcpadmin
** [ALPS00276400] Remove MTK copyright and legal header on GPL/LGPL related packages
** .
** 
** 08 14 2012 yuche.tsai
** NULL
** Fix compile error.
** 
** 07 26 2012 yuche.tsai
** [ALPS00324337] [ALPS.JB][Hot-Spot] Driver update for Hot-Spot
** Update driver code of ALPS.JB for hot-spot.
** 
** 07 19 2012 yuche.tsai
** NULL
** Code update for JB.
 *
 * 07 18 2012 yuche.tsai
 * NULL
 * add one file.
 *
 * 12 02 2011 yuche.tsai
 * NULL
 * Resolve class 3 error issue under AP mode.
 *
 * data frame may TX before Assoc Response TX.
 *
 * 11 11 2011 yuche.tsai
 * NULL
 * Fix work thread cancel issue.
 *
 * 11 11 2011 yuche.tsai
 * NULL
 * Fix default device name issue.
 *
 * 11 09 2011 yuche.tsai
 * [WCXRP00001093] [Need Patch][Volunteer Patch] Service Discovery 2.0 state transition issue.
 * Fix SD2.0 issue which may cause KE. (Monkey test)
 *
 * 11 08 2011 yuche.tsai
 * [WCXRP00001094] [Volunteer Patch][Driver] Driver version & supplicant version query & set support for service discovery version check.
 * Add support for driver version query & p2p supplicant verseion set.
 * For new service discovery mechanism sync.
 *
 * 10 18 2011 yuche.tsai
 * [WCXRP00001045] [WiFi Direct][Driver] Check 2.1 branch.
 * Support Channle Query.
 *
 * 10 18 2011 yuche.tsai
 * [WCXRP00001045] [WiFi Direct][Driver] Check 2.1 branch.
 * New 2.1 branch

 *
 * 09 01 2011 yuche.tsai
 * NULL
 * Fix channel stay interval.
 * Sync channel stay interval & channel request interval under AP mode..
 *
 * 08 30 2011 yuche.tsai
 * [WCXRP00000953] [Volunteer Patch][Driver] Hot Spot Channel ASSERT issue.
 * Fix hot spot FW assert issue when under concurrent case. (DBG enable only)
 *
 * 08 16 2011 cp.wu
 * [WCXRP00000934] [MT6620 Wi-Fi][Driver][P2P] Wi-Fi hot spot with auto sparse channel residence
 * auto channel decision for 2.4GHz hot spot mode
 *
 * 08 16 2011 yuche.tsai
 * NULL
 * Fix scan policy for Active LISTEN scan.
 *
 * 08 09 2011 yuche.tsai
 * [WCXRP00000919] [Volunteer Patch][WiFi Direct][Driver] Invitation New Feature.
 * Invitation Feature add on.
 *
 * 08 02 2011 yuche.tsai
 * [WCXRP00000896] [Volunteer Patch][WiFi Direct][Driver] GO with multiple client, TX deauth to a disconnecting device issue.
 * Support TX Deauth Issue.
 *
 * 07 26 2011 yuche.tsai
 * [WCXRP00000875] [Volunteer Patch][WiFi Direct][Driver] MT6620 IOT issue with realtek test bed solution.
 * Turn off persistent group support for V2.0 release.
 *
 * 07 18 2011 yuche.tsai
 * [WCXRP00000856] [Volunteer Patch][WiFi Direct][Driver] MT6620 WiFi Direct IOT Issue with BCM solution.
 * Fix compile error.
 *
 * 07 18 2011 yuche.tsai
 * [WCXRP00000856] [Volunteer Patch][WiFi Direct][Driver] MT6620 WiFi Direct IOT Issue with BCM solution.
 * Fix MT6620 WiFi Direct IOT Issue with BCM solution.
 *
 * 07 11 2011 yuche.tsai
 * [WCXRP00000845] [Volunteer Patch][WiFi Direct] WiFi Direct Device Connection Robustness
 * Enhance Connection Robustness.
 *
 * 07 08 2011 yuche.tsai
 * [WCXRP00000841] [Volunteer Patch][WiFi Direct] Group Owner Setting.
 * Update GO configure parameter.
 *
 * 07 05 2011 yuche.tsai
 * [WCXRP00000821] [Volunteer Patch][WiFi Direct][Driver] WiFi Direct Connection Speed Issue
 * Disable enhancement II for debugging.
 *
 * 07 05 2011 yuche.tsai
 * [WCXRP00000821] [Volunteer Patch][WiFi Direct][Driver] WiFi Direct Connection Speed Issue
 * Refine compile flag.
 *
 * 07 05 2011 yuche.tsai
 * [WCXRP00000821] [Volunteer Patch][WiFi Direct][Driver] WiFi Direct Connection Speed Issue
 * Add wifi direct connection enhancement method I, II & VI.
 *
 * 06 20 2011 yuche.tsai
 * [WCXRP00000799] [Volunteer Patch][MT6620][Driver] Connection Indication Twice Issue.
 * Fix connection indication twice issue.
 *
 * 06 07 2011 yuche.tsai
 * [WCXRP00000763] [Volunteer Patch][MT6620][Driver] RX Service Discovery Frame under AP mode Issue
 * Fix RX SD request under AP mode issue.
 *
 * 05 04 2011 yuche.tsai
 * NULL
 * Support partial persistent group function.
 *
 * 04 20 2011 terry.wu
 * [WCXRP00000660] [MT6620 Wi-Fi][Driver] Remove flag CFG_WIFI_DIRECT_MOVED
 * Remove CFG_WIFI_DIRECT_MOVED.
 *
 * 04 08 2011 yuche.tsai
 * [WCXRP00000624] [Volunteer Patch][MT6620][Driver] Add device discoverability support for GO.
 * Add device discoverability support.
 *
 * 03 25 2011 yuche.tsai
 * NULL
 * Improve some error handleing.
 *
 * 03 22 2011 george.huang
 * [WCXRP00000504] [MT6620 Wi-Fi][FW] Support Sigma CAPI for power saving related command
 * link with supplicant commands
 *
 * 03 22 2011 yuche.tsai
 * NULL
 * 1.Shorten the LISTEN interval.
 * 2. Fix IF address issue when we are GO
 * 3. Fix LISTEN channel issue.
 *
 * 03 21 2011 yuche.tsai
 * NULL
 * Change P2P Connection Request Flow.
 *
 * 03 19 2011 yuche.tsai
 * [WCXRP00000584] [Volunteer Patch][MT6620][Driver] Add beacon timeout support for WiFi Direct.
 * Add beacon timeout support.
 *
 * 03 19 2011 yuche.tsai
 * [WCXRP00000581] [Volunteer Patch][MT6620][Driver] P2P IE in Assoc Req Issue
 * Append P2P IE in Assoc Req, so that GC can be discovered in probe response of GO.
 *
 * 03 18 2011 yuche.tsai
 * [WCXRP00000574] [Volunteer Patch][MT6620][Driver] Modify P2P FSM Connection Flow
 * Modify connection flow after Group Formation Complete, or device connect to a GO.
 * Instead of request channel & connect directly, we use scan to allocate channel bandwidth & connect after RX BCN.
 *
 * 03 15 2011 yuche.tsai
 * [WCXRP00000560] [Volunteer Patch][MT6620][Driver] P2P Connection from UI using KEY/DISPLAY issue
 * Fix some configure method issue.
 *
 * 03 10 2011 yuche.tsai
 * NULL
 * Add P2P API.
 *
 * 03 07 2011 yuche.tsai
 * [WCXRP00000502] [Volunteer Patch][MT6620][Driver] Fix group ID issue when doing Group Formation.
 * .
 *
 * 03 07 2011 wh.su
 * [WCXRP00000506] [MT6620 Wi-Fi][Driver][FW] Add Security check related code
 * rename the define to anti_pviracy.
 *
 * 03 05 2011 wh.su
 * [WCXRP00000506] [MT6620 Wi-Fi][Driver][FW] Add Security check related code
 * add the code to get the check rsponse and indicate to app.
 *
 * 03 01 2011 yuche.tsai
 * [WCXRP00000501] [Volunteer Patch][MT6620][Driver] No common channel issue when doing GO formation
 * Update channel issue when doing GO formation..
 *
 * 03 01 2011 yuche.tsai
 * [WCXRP00000245] 1. Invitation Request/Response.
2. Provision Discovery Request/Response

 * Update Service Discovery Related wlanoid function.
 *
 * 02 18 2011 wh.su
 * [WCXRP00000471] [MT6620 Wi-Fi][Driver] Add P2P Provison discovery append Config Method attribute at WSC IE
 * fixed the ioctl setting that index not map to spec defined config method.
 *
 * 02 18 2011 yuche.tsai
 * [WCXRP00000480] [Volunteer Patch][MT6620][Driver] WCS IE format issue
 * Fix WSC IE BE format issue.
 *
 * 02 17 2011 wh.su
 * [WCXRP00000471] [MT6620 Wi-Fi][Driver] Add P2P Provison discovery append Config Method attribute at WSC IE
 * append the WSC IE config method attribute at provision discovery request.
 *
 * 02 11 2011 yuche.tsai
 * [WCXRP00000431] [Volunteer Patch][MT6620][Driver] Add MLME support for deauthentication under AP(Hot-Spot) mode.
 * Add two function prototype.
 *
 * 02 10 2011 yuche.tsai
 * [WCXRP00000431] [Volunteer Patch][MT6620][Driver] Add MLME support for deauthentication under AP(Hot-Spot) mode.
 * Support Disassoc & Deauthentication for Hot-Spot.
 *
 * 02 09 2011 yuche.tsai
 * [WCXRP00000245] 1. Invitation Request/Response.

2. Provision Discovery Request/Response

 * Add Service Discovery Indication Related code.
 *
 * 02 09 2011 yuche.tsai
 * [WCXRP00000431] [Volunteer Patch][MT6620][Driver] Add MLME support for deauthentication under AP(Hot-Spot) mode.
 * Add Support for MLME deauthentication for Hot-Spot.
 *
 * 02 09 2011 yuche.tsai
 * [WCXRP00000429] [Volunteer Patch][MT6620][Driver] Hot Spot Client Limit Issue
 * Fix Client Limit Issue.
 *
 * 01 26 2011 yuche.tsai
 * [WCXRP00000245] 1. Invitation Request/Response.

2. Provision Discovery Request/Response

 * Add Service Discovery Function.
 *
 * 01 25 2011 terry.wu
 * [WCXRP00000393] [MT6620 Wi-Fi][Driver] Add new module insert parameter
 * Add a new module parameter to indicate current runnig mode, P2P or AP.
 *
 * 01 19 2011 george.huang
 * [WCXRP00000355] [MT6620 Wi-Fi] Set WMM-PS related setting with qualifying AP capability
 * Null NOA attribute setting when no related parameters.
 *
 * 01 12 2011 yuche.tsai
 * [WCXRP00000352] [Volunteer Patch][MT6620][Driver] P2P Statsion Record Client List Issue
 * Modify some behavior of AP mode.
 *
 * 12 22 2010 yuche.tsai
 * [WCXRP00000245] 1. Invitation Request/Response.
2. Provision Discovery Request/Response

 * Fix Compile Error.
 *
 * 12 15 2010 yuche.tsai
 * [WCXRP00000245] 1. Invitation Request/Response.
2. Provision Discovery Request/Response

 * Refine Connection Flow.
 *
 * 12 08 2010 yuche.tsai
 * [WCXRP00000244] [MT6620][Driver] Add station record type for each client when in AP mode.
 * Change STA Type under AP mode. We would tell if client is a P2P device or a legacy client by checking the P2P IE in assoc req frame.
 *
 * 12 02 2010 yuche.tsai
 * NULL
 * Update P2P Connection Policy for Invitation.
 *
 * 12 02 2010 yuche.tsai
 * NULL
 * Update P2P Connection Policy for Invitation & Provision Discovery.
 *
 * 11 30 2010 yuche.tsai
 * NULL
 * Invitation & Provision Discovery Indication.
 *
 * 11 30 2010 yuche.tsai
 * NULL
 * Update Configure Method indication & selection for Provision Discovery & GO_NEGO_REQ
 *
 * 11 29 2010 yuche.tsai
 * NULL
 * Update P2P related function for INVITATION & PROVISION DISCOVERY.
 *
 * 11 26 2010 george.huang
 * [WCXRP00000152] [MT6620 Wi-Fi] AP mode power saving function
 * Update P2P PS for NOA function.
 *
 * 11 25 2010 yuche.tsai
 * NULL
 * Update Code for Invitation Related Function.
 *
 * 11 17 2010 wh.su
 * [WCXRP00000164] [MT6620 Wi-Fi][Driver] Support the p2p random SSID[WCXRP00000179] [MT6620 Wi-Fi][FW] Set the Tx lowest rate at wlan table for normal operation
 * fixed some ASSERT check.
 *
 * 11 04 2010 wh.su
 * [WCXRP00000164] [MT6620 Wi-Fi][Driver] Support the p2p random SSID
 * adding the p2p random ssid support.
 *
 * 10 20 2010 wh.su
 * [WCXRP00000124] [MT6620 Wi-Fi] [Driver] Support the dissolve P2P Group
 * Add the code to support disconnect p2p group
 *
 * 10 08 2010 wh.su
 * [WCXRP00000085] [MT6620 Wif-Fi] [Driver] update the modified p2p state machine
 * update the frog's new p2p state machine.
 *
 * 10 04 2010 wh.su
 * [WCXRP00000081] [MT6620][Driver] Fix the compiling error at WinXP while enable P2P
 * fixed compiling error while enable p2p.
 *
 * 09 21 2010 kevin.huang
 * [WCXRP00000054] [MT6620 Wi-Fi][Driver] Restructure driver for second Interface
 * Isolate P2P related function for Hardware Software Bundle
 *
 * 09 10 2010 wh.su
 * NULL
 * fixed the compiling error at WinXP.
 *
 * 09 07 2010 wh.su
 * NULL
 * adding the code for beacon/probe req/ probe rsp wsc ie at p2p.
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 08 26 2010 yuche.tsai
 * NULL
 * Add connection abort message event prototype.
 *
 * 08 20 2010 kevin.huang
 * NULL
 * Modify AAA Module for changing STA STATE 3 at p2p/bowRunEventAAAComplete()
 *
 * 08 16 2010 yuche.tsai
 * NULL
 * Fix P2P Intended Interface Address Bug.
 * Extend GO Nego Timeout Time.
 *
 * 08 16 2010 yuche.tsai
 * NULL
 * Extend Listen Interval default value & remove deprecated variable.
 *
 * 08 16 2010 kevin.huang
 * NULL
 * Refine AAA functions
 *
 * 08 12 2010 kevin.huang
 * NULL
 * Refine bssProcessProbeRequest() and bssSendBeaconProbeResponse()
 *
 * 08 12 2010 yuche.tsai
 * NULL
 * Add function prototype for join complete.
 *
 * 08 11 2010 yuche.tsai
 * NULL
 * Add some function proto type for P2P FSM under provisioning phase..
 *
 * 08 11 2010 yuche.tsai
 * NULL
 * Change P2P data structure for supporting
 * 1. P2P Device discovery.
 * 2. P2P Group Negotiation.
 * 3. P2P JOIN
 *
 * 08 05 2010 yuche.tsai
 * NULL
 * Check-in P2P Device Discovery Feature.
 *
 * 08 03 2010 george.huang
 * NULL
 * handle event for updating NOA parameters indicated from FW
 *
 * 08 02 2010 yuche.tsai
 * NULL
 * P2P Group Negotiation Code Check in.
 *
 * 07 26 2010 yuche.tsai
 *
 * Update P2P FSM header file.
 *
 * 07 23 2010 cp.wu
 *
 * P2P/RSN/WAPI IEs need to be declared with compact structure.
 *
 * 07 21 2010 yuche.tsai
 *
 * Add for P2P Scan Result Parsing & Saving.
 *
 * 07 19 2010 yuche.tsai
 *
 * Update P2P FSM header file.
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 21 2010 yuche.tsai
 * [WPD00003839][MT6620 5931][P2P] Feature migration
 * Fix some P2P function prototype.
 *
 * 06 17 2010 yuche.tsai
 * [WPD00003839][MT6620 5931][P2P] Feature migration
 * First draft for migration P2P FSM from FW to Driver.
 *
 * 03 18 2010 kevin.huang
 * [BORA00000663][WIFISYS][New Feature] AdHoc Mode Support
 * Rename CFG flag for P2P
 *
 * 02 26 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Modify parameter of p2pStartGO
 *
 * 02 23 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Add Wi-Fi Direct SSID and P2P GO Test Mode
 *
 * 02 04 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Add AAA Module Support, Revise Net Type to Net Type Index for array lookup
*/

#ifndef _P2P_FSM_H
#define _P2P_FSM_H


/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/
#define CID52_53_54         0


/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/





/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
typedef enum _ENUM_P2P_STATE_T {
    P2P_STATE_IDLE = 0,
    P2P_STATE_SCAN,
    P2P_STATE_AP_CHANNEL_DETECT,
    P2P_STATE_REQING_CHANNEL,
    P2P_STATE_CHNL_ON_HAND, /* Requesting Channel to Send Specific Frame. */
    P2P_STATE_GC_JOIN, /* Sending Specific Frame. May extending channel by other event. */
    P2P_STATE_NUM
} ENUM_P2P_STATE_T, *P_ENUM_P2P_STATE_T;


typedef enum _ENUM_CHANNEL_REQ_TYPE_T {
    CHANNEL_REQ_TYPE_REMAIN_ON_CHANNEL,
    CHANNEL_REQ_TYPE_GC_JOIN_REQ,
    CHANNEL_REQ_TYPE_GO_START_BSS
}
ENUM_CHANNEL_REQ_TYPE_T, *P_ENUM_CHANNEL_REQ_TYPE_T;


typedef enum _ENUM_BUFFER_TYPE_T {
    ENUM_FRAME_TYPE_EXTRA_IE_BEACON,
    ENUM_FRAME_TYPE_EXTRA_IE_ASSOC_RSP,
    ENUM_FRAME_TYPE_EXTRA_IE_PROBE_RSP,
    ENUM_FRAME_TYPE_PROBE_RSP_TEMPLATE,
    ENUM_FRAME_TYPE_BEACON_TEMPLATE,
    ENUM_FRAME_IE_NUM
} ENUM_BUFFER_TYPE_T, *P_ENUM_BUFFER_TYPE_T;

typedef enum _ENUM_HIDDEN_SSID_TYPE_T {
    ENUM_HIDDEN_SSID_NONE,
    ENUM_HIDDEN_SSID_LEN,
    ENUM_HIDDEN_SSID_ZERO_CONTENT,
    ENUM_HIDDEN_SSID_NUM
}
ENUM_HIDDEN_SSID_TYPE_T, *P_ENUM_HIDDEN_SSID_TYPE_T;

typedef struct _P2P_SSID_STRUCT_T {
    UINT_8 aucSsid[32];
    UINT_8 ucSsidLen;
} P2P_SSID_STRUCT_T, *P_P2P_SSID_STRUCT_T;

typedef struct _P2P_STATION_INFO_T {
    UINT_32 u4InactiveTime;
    UINT_32 u4RxBytes;          // TODO:
    UINT_32 u4TxBytes;          // TODO:
    UINT_32 u4RxPackets;            // TODO:
    UINT_32 u4TxPackets;            // TODO:
    // TODO: Add more for requirement.
}
P2P_STATION_INFO_T, *P_P2P_STATION_INFO_T;


typedef struct _AP_CRYPTO_SETTINGS_T {
    UINT_32 u4WpaVersion;
    UINT_32 u4CipherGroup;
    INT_32 i4NumOfCiphers;
    UINT_32 aucCiphersPairwise[5];
    INT_32 i4NumOfAkmSuites;
    UINT_32 aucAkmSuites[2];
    BOOLEAN fgIsControlPort;
    UINT_16 u2ControlPortBE;
    BOOLEAN fgIsControlPortEncrypt;
} AP_CRYPTO_SETTINGS_T, *P_AP_CRYPTO_SETTINGS_T;
typedef struct _MSG_P2P_MGMT_TX_REQUEST_T {
    MSG_HDR_T rMsgHdr;
    P_MSDU_INFO_T prMgmtMsduInfo;
    UINT_64 u8Cookie;                   /* For indication. */
    BOOLEAN fgNoneCckRate;
    BOOLEAN fgIsWaitRsp;
} MSG_P2P_MGMT_TX_REQUEST_T, *P_MSG_P2P_MGMT_TX_REQUEST_T;


/*-------------------- P2P FSM ACTION STRUCT ---------------------*/
typedef struct _P2P_CHNL_REQ_INFO_T {
    BOOLEAN fgIsChannelRequested;
    UINT_8 ucSeqNumOfChReq;
    UINT_64 u8Cookie;
    UINT_8 ucReqChnlNum;
    ENUM_BAND_T eBand;
    ENUM_CHNL_EXT_T eChnlSco;
    UINT_32 u4MaxInterval;
    ENUM_CHANNEL_REQ_TYPE_T eChannelReqType;

    UINT_8 ucOriChnlNum;
    ENUM_BAND_T eOriBand;
    ENUM_CHNL_EXT_T eOriChnlSco;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
	P_MSG_P2P_MGMT_TX_REQUEST_T prMsgTxReq;
	BOOLEAN fgNeedIndSupp;
#endif
} P2P_CHNL_REQ_INFO_T, *P_P2P_CHNL_REQ_INFO_T;

typedef struct _P2P_SCAN_REQ_INFO_T {
    ENUM_SCAN_TYPE_T eScanType;
    ENUM_SCAN_CHANNEL eChannelSet;
    UINT_16  u2PassiveDewellTime;
    UINT_8 ucSeqNumOfScnMsg;
    BOOLEAN fgIsAbort;
    BOOLEAN fgIsScanRequest;
    UINT_8 ucNumChannelList;
    RF_CHANNEL_INFO_T arScanChannelList[MAXIMUM_OPERATION_CHANNEL_LIST];
    UINT_32 u4BufLength;
    UINT_8 aucIEBuf[MAX_IE_LENGTH];
    P2P_SSID_STRUCT_T rSsidStruct;    // Currently we can only take one SSID scan request
}
P2P_SCAN_REQ_INFO_T, *P_P2P_SCAN_REQ_INFO_T;

typedef struct _P2P_CONNECTION_REQ_INFO_T {

    BOOLEAN fgIsConnRequest;
    P2P_SSID_STRUCT_T rSsidStruct;
    UINT_8 aucBssid[MAC_ADDR_LEN];
    /* For ASSOC Req. */
    UINT_32 u4BufLength;
    UINT_8 aucIEBuf[MAX_IE_LENGTH];
} P2P_CONNECTION_REQ_INFO_T, *P_P2P_CONNECTION_REQ_INFO_T;

typedef struct _P2P_MGMT_TX_REQ_INFO_T {
    BOOLEAN fgIsMgmtTxRequested;
    P_MSDU_INFO_T prMgmtTxMsdu;
    UINT_64 u8Cookie;
} P2P_MGMT_TX_REQ_INFO_T, *P_P2P_MGMT_TX_REQ_INFO_T;

typedef struct _P2P_BEACON_UPDATE_INFO_T {
    PUINT_8 pucBcnHdr;
    UINT_32 u4BcnHdrLen;
    PUINT_8 pucBcnBody;
    UINT_32 u4BcnBodyLen;
}
P2P_BEACON_UPDATE_INFO_T, *P_P2P_BEACON_UPDATE_INFO_T;

typedef struct _P2P_PROBE_RSP_UPDATE_INFO_T {
    P_MSDU_INFO_T prProbeRspMsduTemplate;
} P2P_PROBE_RSP_UPDATE_INFO_T, *P_P2P_PROBE_RSP_UPDATE_INFO_T;

typedef struct _P2P_ASSOC_RSP_UPDATE_INFO_T {
    PUINT_8 pucAssocRspExtIE;
    UINT_16 u2AssocIELen;
} P2P_ASSOC_RSP_UPDATE_INFO_T, *P_P2P_ASSOC_RSP_UPDATE_INFO_T;

typedef struct _P2P_JOIN_INFO_T {
    UINT_32 ucSeqNumOfReqMsg;
    UINT_8 ucAvailableAuthTypes;
    P_STA_RECORD_T prTargetStaRec;
    P2P_SSID_STRUCT_T rSsidStruct;
    BOOLEAN fgIsJoinComplete;
    /* For ASSOC Rsp. */
    UINT_32 u4BufLength;
    UINT_8 aucIEBuf[MAX_IE_LENGTH];
}
P2P_JOIN_INFO_T, *P_P2P_JOIN_INFO_T;

#if CFG_SUPPORT_WFD

#define WFD_FLAGS_DEV_INFO_VALID            BIT(0)     /* 1. WFD_DEV_INFO, 2. WFD_CTRL_PORT, 3. WFD_MAT_TP. */
#define WFD_FLAGS_SINK_INFO_VALID           BIT(1)     /* 1. WFD_SINK_STATUS, 2. WFD_SINK_MAC. */
#define WFD_FLAGS_ASSOC_MAC_VALID        BIT(2)     /* 1. WFD_ASSOC_MAC. */
#define WFD_FLAGS_EXT_CAPABILITY_VALID  BIT(3)     /* 1. WFD_EXTEND_CAPABILITY. */



struct _WFD_CFG_SETTINGS_T {
    UINT_32 u4WfdCmdType;
    UINT_8 ucWfdEnable;
    UINT_8 ucWfdCoupleSinkStatus;
    UINT_8  ucWfdSessionAvailable; /* 0: NA 1:Set 2:Clear */
    UINT_8  ucWfdSigmaMode;
    UINT_16 u2WfdDevInfo;
    UINT_16 u2WfdControlPort;
    UINT_16 u2WfdMaximumTp;
    UINT_16 u2WfdExtendCap;
    UINT_8 aucWfdCoupleSinkAddress[MAC_ADDR_LEN];
    UINT_8 aucWfdAssociatedBssid[MAC_ADDR_LEN];
    UINT_8 aucWfdVideolp[4];
    UINT_8 aucWfdAudiolp[4];
    UINT_16 u2WfdVideoPort;
    UINT_16 u2WfdAudioPort;
    UINT_32 u4WfdFlag;
    UINT_32 u4WfdPolicy;
    UINT_32 u4WfdState;
    UINT_8 aucWfdSessionInformationIE[24*8];
    UINT_16 u2WfdSessionInformationIELen;
    UINT_8 aucReserved1[2];
    UINT_8   aucWfdPrimarySinkMac[MAC_ADDR_LEN];
    UINT_8   aucWfdSecondarySinkMac[MAC_ADDR_LEN];
    UINT_32  u4WfdAdvancedFlag;
    /* Group 1 64 bytes */
    UINT_8   aucWfdLocalIp[4];
    UINT_16  u2WfdLifetimeAc2; /* Unit is 2 TU */
    UINT_16  u2WfdLifetimeAc3; /* Unit is 2 TU */
    UINT_8   aucReverved2[56];
    /* Group 2 64 bytes */
    UINT_8   aucReverved3[64];
    /* Group 3 64 bytes */
    UINT_8   aucReverved4[64];

};

#endif



struct _P2P_FSM_INFO_T {
    /* State related. */
    ENUM_P2P_STATE_T    ePreviousState;
    ENUM_P2P_STATE_T    eCurrentState;

    /* Channel related. */
    P2P_CHNL_REQ_INFO_T rChnlReqInfo;

    /* Scan related. */
    P2P_SCAN_REQ_INFO_T rScanReqInfo;

    /* Connection related. */
    P2P_CONNECTION_REQ_INFO_T rConnReqInfo;

    /* Mgmt tx related. */
    P2P_MGMT_TX_REQ_INFO_T rMgmtTxInfo;

    /* Beacon related. */
    P2P_BEACON_UPDATE_INFO_T rBcnContentInfo;

    /* Probe Response related. */
    P2P_PROBE_RSP_UPDATE_INFO_T rProbeRspContentInfo;

    /* Assoc Rsp related. */
    P2P_ASSOC_RSP_UPDATE_INFO_T rAssocRspContentInfo;

    /* GC Join related. */
    P2P_JOIN_INFO_T rJoinInfo;

    /* FSM Timer */
    TIMER_T rP2pFsmTimeoutTimer;


    /* GC Target BSS. */
    P_BSS_DESC_T prTargetBss;

    /* GC Connection Request. */
    BOOLEAN fgIsConnectionRequested;

    BOOLEAN fgIsApMode;

    /* Channel grant interval. */
    UINT_32 u4GrantInterval;

    /* Packet filter for P2P module. */
    UINT_32 u4P2pPacketFilter;

    //vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv Prepare for use vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
    /* Msg event queue. */
    LINK_T rMsgEventQueue;

#if CFG_SUPPORT_WFD
    WFD_CFG_SETTINGS_T rWfdConfigureSettings;
#endif

    BOOLEAN fgIsWPSMode;
};


/*---------------- Messages -------------------*/
typedef struct _MSG_P2P_SCAN_REQUEST_T {
    MSG_HDR_T       rMsgHdr;    /* Must be the first member */
    P_P2P_SSID_STRUCT_T prSSID;
    INT_32 i4SsidNum;
    UINT_32 u4NumChannel;
    PUINT_8 pucIEBuf;
    UINT_32 u4IELen;
    BOOLEAN fgIsAbort;
    RF_CHANNEL_INFO_T arChannelListInfo[1];
} MSG_P2P_SCAN_REQUEST_T, *P_MSG_P2P_SCAN_REQUEST_T;

typedef struct _MSG_P2P_CHNL_REQUEST_T {
    MSG_HDR_T       rMsgHdr;    /* Must be the first member */
    UINT_64 u8Cookie;
    UINT_32 u4Duration;
    ENUM_CHNL_EXT_T eChnlSco;
    RF_CHANNEL_INFO_T rChannelInfo;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
	BOOLEAN fgNeedIndSupp;
#endif
} MSG_P2P_CHNL_REQUEST_T, *P_MSG_P2P_CHNL_REQUEST_T;

typedef struct _MSG_P2P_CHNL_ABORT_T {
    MSG_HDR_T       rMsgHdr;    /* Must be the first member */
    UINT_64 u8Cookie;
} MSG_P2P_CHNL_ABORT_T, *P_MSG_P2P_CHNL_ABORT_T;


typedef struct _MSG_P2P_CONNECTION_REQUEST_T {
    MSG_HDR_T       rMsgHdr;    /* Must be the first member */
    P2P_SSID_STRUCT_T rSsid;
    UINT_8 aucBssid[MAC_ADDR_LEN];
    ENUM_CHNL_EXT_T eChnlSco;
    RF_CHANNEL_INFO_T rChannelInfo;
    UINT_32 u4IELen;
    UINT_8 aucIEBuf[1];
    // TODO: Auth Type, OPEN, SHARED, FT, EAP...
} MSG_P2P_CONNECTION_REQUEST_T, *P_MSG_P2P_CONNECTION_REQUEST_T;


typedef struct _MSG_P2P_CONNECTION_ABORT_T {
    MSG_HDR_T rMsgHdr; /* Must be the first member. */
    UINT_8 aucTargetID[MAC_ADDR_LEN];
    UINT_16 u2ReasonCode;
    BOOLEAN fgSendDeauth;
} MSG_P2P_CONNECTION_ABORT_T, *P_MSG_P2P_CONNECTION_ABORT_T;
#if 0
typedef struct _MSG_P2P_MGMT_TX_REQUEST_T {
    MSG_HDR_T rMsgHdr;
    P_MSDU_INFO_T prMgmtMsduInfo;
    UINT_64 u8Cookie;                   /* For indication. */
    BOOLEAN fgNoneCckRate;
    BOOLEAN fgIsWaitRsp;
} MSG_P2P_MGMT_TX_REQUEST_T, *P_MSG_P2P_MGMT_TX_REQUEST_T;
#endif
typedef struct _MSG_P2P_START_AP_T {
    MSG_HDR_T rMsgHdr;
    UINT_32 u4DtimPeriod;
    UINT_32 u4BcnInterval;
    UINT_8 aucSsid[32];
    UINT_16 u2SsidLen;
    UINT_8 ucHiddenSsidType;
    BOOLEAN fgIsPrivacy;
    AP_CRYPTO_SETTINGS_T rEncryptionSettings;
    INT_32 i4InactiveTimeout;
}
MSG_P2P_START_AP_T, *P_MSG_P2P_START_AP_T;


typedef struct _MSG_P2P_BEACON_UPDATE_T {
    MSG_HDR_T rMsgHdr;
    UINT_32 u4BcnHdrLen;
    UINT_32 u4BcnBodyLen;
    PUINT_8 pucBcnHdr;
    PUINT_8 pucBcnBody;
    UINT_8 aucBuffer[1];       /* Header & Body are put here. */
}
MSG_P2P_BEACON_UPDATE_T, *P_MSG_P2P_BEACON_UPDATE_T;

typedef struct _MSG_P2P_MGMT_FRAME_UPDATE_T {
    MSG_HDR_T rMsgHdr;
    ENUM_BUFFER_TYPE_T eBufferType;
    UINT_32 u4BufferLen;
    UINT_8 aucBuffer[1];
} MSG_P2P_MGMT_FRAME_UPDATE_T, *P_MSG_P2P_MGMT_FRAME_UPDATE_T;


typedef struct _MSG_P2P_SWITCH_OP_MODE_T {
    MSG_HDR_T       rMsgHdr;    /* Must be the first member */
    ENUM_OP_MODE_T eOpMode;
} MSG_P2P_SWITCH_OP_MODE_T, *P_MSG_P2P_SWITCH_OP_MODE_T;

typedef struct _MSG_P2P_MGMT_FRAME_REGISTER_T {
    MSG_HDR_T       rMsgHdr;    /* Must be the first member */
    UINT_16             u2FrameType;
    BOOLEAN           fgIsRegister;
}
MSG_P2P_MGMT_FRAME_REGISTER_T, *P_MSG_P2P_MGMT_FRAME_REGISTER_T;

typedef struct _MSG_P2P_NETDEV_REGISTER_T {
    MSG_HDR_T       rMsgHdr;    /* Must be the first member */
    BOOLEAN         fgIsEnable;
    UINT_8          ucMode;
} MSG_P2P_NETDEV_REGISTER_T, *P_MSG_P2P_NETDEV_REGISTER_T;

#if CFG_SUPPORT_WFD
typedef struct _MSG_WFD_CONFIG_SETTINGS_CHANGED_T {
    MSG_HDR_T       rMsgHdr;    /* Must be the first member */
    P_WFD_CFG_SETTINGS_T prWfdCfgSettings;
} MSG_WFD_CONFIG_SETTINGS_CHANGED_T, *P_MSG_WFD_CONFIG_SETTINGS_CHANGED_T;
#endif

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
VOID
p2pFsmStateTransition(
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo,
    IN ENUM_P2P_STATE_T eNextState
    );



VOID
p2pFsmRunEventAbort(
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo
    );


VOID
p2pFsmRunEventScanRequest(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
p2pFsmRunEventMgmtFrameTx(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
p2pFsmRunEventStartAP(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
p2pFsmRunEventNetDeviceRegister(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
p2pFsmRunEventUpdateMgmtFrame(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
p2pFsmRunEventBeaconUpdate(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
p2pFsmRunEventStopAP(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
p2pFsmRunEventChannelRequest(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );


VOID
p2pFsmRunEventChannelAbort(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );


VOID
p2pFsmRunEventDissolve(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );


VOID
p2pFsmRunEventSwitchOPMode(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );


WLAN_STATUS
p2pFsmRunEventMgmtFrameTxDone(
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo,
    IN ENUM_TX_RESULT_CODE_T rTxDoneStatus
    );


VOID
p2pFsmRunEventMgmtFrameRegister(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

#if CFG_SUPPORT_WFD
VOID
p2pFsmRunEventWfdSettingUpdate(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );
#endif


#if 0
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#endif

//3 /* --------------- WFA P2P DEFAULT PARAMETERS --------------- */
#define P2P_WILDCARD_SSID                           "DIRECT-"
#define P2P_WILDCARD_SSID_LEN                       7
#define P2P_GROUP_ID_LEN                            9

#define P2P_DRIVER_VERSION                          2        /* Update when needed. */

#define P2P_DEFAULT_DEV_NAME                        "Wireless Client"
#define P2P_DEFAULT_DEV_NAME_LEN                    15
#define P2P_DEFAULT_PRIMARY_CATEGORY_ID             10
#define P2P_DEFAULT_PRIMARY_SUB_CATEGORY_ID         5
#define P2P_DEFAULT_CONFIG_METHOD                   (WPS_ATTRI_CFG_METHOD_PUSH_BUTTON | WPS_ATTRI_CFG_METHOD_KEYPAD | WPS_ATTRI_CFG_METHOD_DISPLAY)
#define P2P_DEFAULT_LISTEN_CHANNEL                   1

#define P2P_MAX_SUPPORTED_SEC_DEV_TYPE_COUNT        0 /* NOTE(Kevin): Shall <= 16 */
#define P2P_MAX_SUPPORTED_CHANNEL_LIST_COUNT         13

#define P2P_MAX_SUPPORTED_CHANNEL_LIST_SIZE            51  /* Contains 6 sub-band. */

#define P2P_GC_MAX_CACHED_SEC_DEV_TYPE_COUNT        8 /* NOTE(Kevin): Shall <= 16 */

#define P2P_MAXIMUM_CLIENT_COUNT                    8
#define P2P_MAXIMUM_NOA_COUNT                       8


#define P2P_MAXIMUM_ATTRIBUTE_LEN                   251

#define P2P_CTWINDOW_DEFAULT                        25 /* in TU=(1024usec) */

#define P2P_MAXIMUM_ATTRIBUTES_CACHE_SIZE           768

/* P2P 3.1.2.1.3 - Find Phase */
#define P2P_MAX_DISCOVERABLE_INTERVAL    8   //3//3
#define P2P_MIN_DISCOVERABLE_INTERVAL    5   // 1

#define P2P_LISTEN_SCAN_UNIT                     100    // MS

/* FSM Time Related constrain. */
#define P2P_SERACH_STATE_PERIOD_MS              1000   // Deprecated.

#define P2P_GO_CHANNEL_STAY_INTERVAL             1000

#define P2P_GO_NEGO_TIMEOUT_MS                          500
#define P2P_CONNECTION_TIMEOUT_SEC                   120

#define P2P_INVITAION_TIMEOUT_MS                         500   /* Timeout Wait Invitation Resonse. */
#define P2P_PROVISION_DISCOVERY_TIMEOUT_MS     500   /* Timeout Wait Provision Discovery Resonse. */

//3 /* --------------- WFA P2P IE --------------- */
/* P2P 4.1.1 - P2P IE format */
#define P2P_OUI_TYPE_LEN                            4
#define P2P_IE_OUI_HDR                              (ELEM_HDR_LEN + P2P_OUI_TYPE_LEN) /* == OFFSET_OF(IE_P2P_T, aucP2PAttributes[0]) */

/* P2P 4.1.1 - General P2P Attribute */
#define P2P_ATTRI_HDR_LEN                           3 /* ID(1 octet) + Length(2 octets) */

/* P2P 4.1.1 - P2P Attribute ID definitions */
#define P2P_ATTRI_ID_STATUS                                 0
#define P2P_ATTRI_ID_REASON_CODE                            1
#define P2P_ATTRI_ID_P2P_CAPABILITY                         2
#define P2P_ATTRI_ID_P2P_DEV_ID                             3
#define P2P_ATTRI_ID_GO_INTENT                              4
#define P2P_ATTRI_ID_CFG_TIMEOUT                            5
#define P2P_ATTRI_ID_LISTEN_CHANNEL                         6
#define P2P_ATTRI_ID_P2P_GROUP_BSSID                        7
#define P2P_ATTRI_ID_EXT_LISTEN_TIMING                      8
#define P2P_ATTRI_ID_INTENDED_P2P_IF_ADDR                   9
#define P2P_ATTRI_ID_P2P_MANAGEABILITY                      10
#define P2P_ATTRI_ID_CHANNEL_LIST                           11
#define P2P_ATTRI_ID_NOTICE_OF_ABSENCE                      12
#define P2P_ATTRI_ID_P2P_DEV_INFO                           13
#define P2P_ATTRI_ID_P2P_GROUP_INFO                         14
#define P2P_ATTRI_ID_P2P_GROUP_ID                           15
#define P2P_ATTRI_ID_P2P_INTERFACE                          16
#define P2P_ATTRI_ID_OPERATING_CHANNEL                      17
#define P2P_ATTRI_ID_INVITATION_FLAG                        18
#define P2P_ATTRI_ID_VENDOR_SPECIFIC                        221

/* Maximum Length of P2P Attributes */
#define P2P_ATTRI_MAX_LEN_STATUS                            1 /* 0 */
#define P2P_ATTRI_MAX_LEN_REASON_CODE                       1 /* 1 */
#define P2P_ATTRI_MAX_LEN_P2P_CAPABILITY                    2 /* 2 */
#define P2P_ATTRI_MAX_LEN_P2P_DEV_ID                        6 /* 3 */
#define P2P_ATTRI_MAX_LEN_GO_INTENT                         1 /* 4 */
#define P2P_ATTRI_MAX_LEN_CFG_TIMEOUT                       2 /* 5 */
#if CID52_53_54
    #define P2P_ATTRI_MAX_LEN_LISTEN_CHANNEL                    5 /* 6 */
#else
    #define P2P_ATTRI_MAX_LEN_LISTEN_CHANNEL                    5 /* 6 */
#endif
#define P2P_ATTRI_MAX_LEN_P2P_GROUP_BSSID                   6 /* 7 */
#define P2P_ATTRI_MAX_LEN_EXT_LISTEN_TIMING                 4 /* 8 */
#define P2P_ATTRI_MAX_LEN_INTENDED_P2P_IF_ADDR              6 /* 9 */
#define P2P_ATTRI_MAX_LEN_P2P_MANAGEABILITY                 1 /* 10 */
//#define P2P_ATTRI_MAX_LEN_CHANNEL_LIST                      3 + (n* (2 + num_of_ch)) /* 11 */
#define P2P_ATTRI_LEN_CHANNEL_LIST                                  3 /* 11 */
#define P2P_ATTRI_LEN_CHANNEL_ENTRY                                  2 /* 11 */


//#define P2P_ATTRI_MAX_LEN_NOTICE_OF_ABSENCE                 2 + (n* (13)) /* 12 */
#define P2P_ATTRI_MAX_LEN_NOTICE_OF_ABSENCE                 (2 + (P2P_MAXIMUM_NOA_COUNT*(13))) /* 12 */

#define P2P_ATTRI_MAX_LEN_P2P_DEV_INFO                      17 + (8 * (8)) + 36 /* 13 */
//#define P2P_ATTRI_MAX_LEN_P2P_GROUP_INFO                    n* (25 + (m* (8)) + 32) /* 14 */
#define P2P_ATTRI_MAX_LEN_P2P_GROUP_ID                      38 /* 15 */
#define P2P_ATTRI_MAX_LEN_P2P_INTERFACE                     253 // 7 + 6* [0~41] /* 16 */
#if CID52_53_54
    #define P2P_ATTRI_MAX_LEN_OPERATING_CHANNEL                 5 /* 17 */
#else
    #define P2P_ATTRI_MAX_LEN_OPERATING_CHANNEL                 5 /* 17 */
#endif
#define P2P_ATTRI_MAX_LEN_INVITATION_FLAGS                          1 /* 18 */

/* P2P 4.1.2 - P2P Status definitions */
#define P2P_STATUS_SUCCESS                                  0
#define P2P_STATUS_FAIL_INFO_IS_CURRENTLY_UNAVAILABLE   1
#define P2P_STATUS_FAIL_INCOMPATIBLE_PARAM                  2
#define P2P_STATUS_FAIL_LIMIT_REACHED                       3
#define P2P_STATUS_FAIL_INVALID_PARAM                       4
#define P2P_STATUS_FAIL_UNABLE_ACCOMMODATE_REQ              5
#define P2P_STATUS_FAIL_PREVIOUS_PROTOCOL_ERR               6
#define P2P_STATUS_FAIL_NO_COMMON_CHANNELS                  7
#define P2P_STATUS_FAIL_UNKNOWN_P2P_GROUP                   8
#define P2P_STATUS_FAIL_SAME_INTENT_VALUE_15                9
#define P2P_STATUS_FAIL_INCOMPATIBLE_PROVISION_METHOD       10
#define P2P_STATUS_FAIL_REJECTED_BY_USER                    11


/* P2P 4.1.3 - P2P Minor Reason Code definitions */
#define P2P_REASON_SUCCESS                                  0
#define P2P_REASON_DISASSOCIATED_DUE_CROSS_CONNECTION       1
#define P2P_REASON_DISASSOCIATED_DUE_UNMANAGEABLE           2
#define P2P_REASON_DISASSOCIATED_DUE_NO_P2P_COEXIST_PARAM   3
#define P2P_REASON_DISASSOCIATED_DUE_MANAGEABLE             4


/* P2P 4.1.4 - Device Capability Bitmap definitions */
#define P2P_DEV_CAPABILITY_SERVICE_DISCOVERY                BIT(0)
#define P2P_DEV_CAPABILITY_CLIENT_DISCOVERABILITY           BIT(1)
#define P2P_DEV_CAPABILITY_CONCURRENT_OPERATION             BIT(2)
#define P2P_DEV_CAPABILITY_P2P_INFRA_MANAGED                BIT(3)
#define P2P_DEV_CAPABILITY_P2P_DEVICE_LIMIT                 BIT(4)
#define P2P_DEV_CAPABILITY_P2P_INVITATION_PROCEDURE         BIT(5)


/* P2P 4.1.4 - Group Capability Bitmap definitions */
#define P2P_GROUP_CAPABILITY_P2P_GROUP_OWNER                BIT(0)
#define P2P_GROUP_CAPABILITY_PERSISTENT_P2P_GROUP           BIT(1)
#define P2P_GROUP_CAPABILITY_P2P_GROUP_LIMIT                BIT(2)
#define P2P_GROUP_CAPABILITY_INTRA_BSS_DISTRIBUTION         BIT(3)
#define P2P_GROUP_CAPABILITY_CROSS_CONNECTION               BIT(4)
#define P2P_GROUP_CAPABILITY_PERSISTENT_RECONNECT           BIT(5)
#define P2P_GROUP_CAPABILITY_GROUP_FORMATION                BIT(6)

/* P2P 4.1.6 - GO Intent field definitions */
#define P2P_GO_INTENT_TIE_BREAKER_FIELD                     BIT(0)
#define P2P_GO_INTENT_VALUE_MASK                            BITS(1,7)
#define P2P_GO_INTENT_VALUE_OFFSET                          1

/* P2P 4.1.12 - Manageability Bitmap definitions */
#define P2P_DEVICE_MANAGEMENT                               BIT(0)

/* P2P 4.1.14 - CTWindow and OppPS Parameters definitions */
#define P2P_CTW_OPPPS_PARAM_OPPPS_FIELD                     BIT(7)
#define P2P_CTW_OPPPS_PARAM_CTWINDOW_MASK                   BITS(0,6)


#define ELEM_MAX_LEN_P2P_FOR_PROBE_REQ                      \
            (P2P_OUI_TYPE_LEN + \
             (P2P_ATTRI_HDR_LEN + P2P_ATTRI_MAX_LEN_P2P_CAPABILITY) + \
             (P2P_ATTRI_HDR_LEN + P2P_ATTRI_MAX_LEN_P2P_DEV_ID) + \
             (P2P_ATTRI_HDR_LEN + P2P_ATTRI_MAX_LEN_LISTEN_CHANNEL) + \
             (P2P_ATTRI_HDR_LEN + P2P_ATTRI_MAX_LEN_OPERATING_CHANNEL))

#define ELEM_MAX_LEN_P2P_FOR_ASSOC_REQ                      \
            (P2P_OUI_TYPE_LEN + \
             (P2P_ATTRI_HDR_LEN + P2P_ATTRI_MAX_LEN_P2P_CAPABILITY) + \
             (P2P_ATTRI_HDR_LEN + P2P_ATTRI_MAX_LEN_EXT_LISTEN_TIMING) + \
             (P2P_ATTRI_HDR_LEN + P2P_ATTRI_MAX_LEN_P2P_DEV_INFO))


/* P2P 4.1.16 - P2P Client Infor Descriptor */
#define P2P_CLIENT_INFO_DESC_HDR_LEN                        1 /* Length(1 octets) */

/* P2P 4.1.20 - P2P Invitation Flags Attribute*/
#define P2P_INVITATION_FLAGS_INVITATION_TYPE       BIT(0)
#define P2P_INVITATION_TYPE_INVITATION                      0
#define P2P_INVITATION_TYPE_REINVOKE                          1
//3 /* --------------- WPS Data Element Definitions --------------- */
/* P2P 4.2.2 - General WSC Attribute */
#define WSC_ATTRI_HDR_LEN                                   4 /* ID(2 octet) + Length(2 octets) */
#define WSC_ATTRI_MAX_LEN_VERSION                           1
#define WSC_ATTRI_MAX_LEN_DEVICE_PASSWORD_ID                2
#define WSC_ATTRI_LEN_CONFIG_METHOD                         2

/* WPS 11 - Data Element Definitions */
#define WPS_ATTRI_ID_VERSION            0x104A
#define WPS_ATTRI_ID_CONFIGURATION_METHODS   0x1008
#define WPS_ATTRI_ID_DEVICE_PASSWORD    0x1012
#define WPS_ATTRI_ID_DEVICE_NAME        0x1011
#define WPS_ATTRI_ID_PRI_DEVICE_TYPE    0x1054
#define WPS_ATTRI_ID_SEC_DEVICE_TYPE    0x1055

#define WPS_MAXIMUM_ATTRIBUTES_CACHE_SIZE           300

#define WPS_ATTRI_MAX_LEN_DEVICE_NAME   32  /* 0x1011 */

#define WPS_ATTRI_CFG_METHOD_USBA           BIT(0)
#define WPS_ATTRI_CFG_METHOD_ETHERNET       BIT(1)
#define WPS_ATTRI_CFG_METHOD_LABEL          BIT(2)
#define WPS_ATTRI_CFG_METHOD_DISPLAY        BIT(3)
#define WPS_ATTRI_CFG_METHOD_EXT_NFC        BIT(4)
#define WPS_ATTRI_CFG_METHOD_INT_NFC        BIT(5)
#define WPS_ATTRI_CFG_METHOD_NFC_IF         BIT(6)
#define WPS_ATTRI_CFG_METHOD_PUSH_BUTTON    BIT(7)
#define WPS_ATTRI_CFG_METHOD_KEYPAD         BIT(8)


#define P2P_FLAGS_PROVISION_COMPLETE                            0x00000001
#define P2P_FLAGS_PROVISION_DISCOVERY_COMPLETE        0x00000002
#define P2P_FLAGS_PROVISION_DISCOVERY_WAIT_RESPONSE 0x00000004
#define P2P_FLAGS_PROVISION_DISCOVERY_RESPONSE_WAIT  0x00000008
#define P2P_FLAGS_MASK_PROVISION                                    0x00000017
#define P2P_FLAGS_MASK_PROVISION_COMPLETE                   0x00000015
#define P2P_FLAGS_PROVISION_DISCOVERY_INDICATED        0x00000010
#define P2P_FLAGS_INVITATION_TOBE_GO                            0x00000100
#define P2P_FLAGS_INVITATION_TOBE_GC                            0x00000200
#define P2P_FLAGS_INVITATION_SUCCESS                            0x00000400
#define P2P_FLAGS_INVITATION_WAITING_TARGET                            0x00000800
#define P2P_FLAGS_MASK_INVITATION                                  0x00000F00
#define P2P_FLAGS_FORMATION_ON_GOING                          0x00010000
#define P2P_FLAGS_FORMATION_LOCAL_PWID_RDY              0x00020000
#define P2P_FLAGS_FORMATION_TARGET_PWID_RDY           0x00040000
#define P2P_FLAGS_FORMATION_COMPLETE                            0x00080000
#define P2P_FLAGS_MASK_FORMATION                                  0x000F0000
#define P2P_FLAGS_DEVICE_DISCOVER_REQ                        0x00100000
#define P2P_FLAGS_DEVICE_DISCOVER_DONE                       0x00200000
#define P2P_FLAGS_DEVICE_INVITATION_WAIT                      0x00400000
#define P2P_FLAGS_DEVICE_SERVICE_DISCOVER_WAIT         0x00800000
#define P2P_FLAGS_MASK_DEVICE_DISCOVER                        0x00F00000

#define P2P_FLAGS_DEVICE_FORMATION_REQUEST                 0x01000000


/* MACRO for flag operation */
#define SET_FLAGS(_FlagsVar, _BitsToSet) \
        (_FlagsVar) = ((_FlagsVar) | (_BitsToSet))

#define TEST_FLAGS(_FlagsVar, _BitsToCheck) \
        (((_FlagsVar) & (_BitsToCheck)) == (_BitsToCheck))

#define CLEAR_FLAGS(_FlagsVar, _BitsToClear) \
        (_FlagsVar) &= ~(_BitsToClear)



#define CFG_DISABLE_WIFI_DIRECT_ENHANCEMENT_I     0

#define CFG_DISABLE_WIFI_DIRECT_ENHANCEMENT_II     0

#define CFG_DISABLE_WIFI_DIRECT_ENHANCEMENT_III     0

#define CFG_DISABLE_WIFI_DIRECT_ENHANCEMENT_IV     0

#define CFG_DISABLE_DELAY_PROVISION_DISCOVERY      0

#define CFG_CONNECTION_POLICY_2_0                            0

/* Device Password ID */
enum wps_dev_password_id {
    DEV_PW_DEFAULT = 0x0000,
    DEV_PW_USER_SPECIFIED = 0x0001,
    DEV_PW_MACHINE_SPECIFIED = 0x0002,
    DEV_PW_REKEY = 0x0003,
    DEV_PW_PUSHBUTTON = 0x0004,
    DEV_PW_REGISTRAR_SPECIFIED = 0x0005
};

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
#if defined(WINDOWS_DDK) || defined(WINDOWS_CE)
#pragma pack(1)
#endif

//3 /* --------------- WFA P2P IE and Attributes --------------- */

/* P2P 4.1.1 - P2P Information Element */
typedef struct _IE_P2P_T {
    UINT_8      ucId;                   /* Element ID */
    UINT_8      ucLength;               /* Length */
    UINT_8      aucOui[3];              /* OUI */
    UINT_8      ucOuiType;              /* OUI Type */
    UINT_8      aucP2PAttributes[1];    /* P2P Attributes */
} __KAL_ATTRIB_PACKED__ IE_P2P_T, *P_IE_P2P_T;

/* P2P 4.1.1 - General P2P Attribute */
typedef struct _P2P_ATTRIBUTE_T {
    UINT_8      ucId;                   /* Attribute ID */
    UINT_16     u2Length;               /* Length */
    UINT_8      aucBody[1];             /* Body field */
} __KAL_ATTRIB_PACKED__ P2P_ATTRIBUTE_T, ATTRIBUTE_HDR_T, *P_P2P_ATTRIBUTE_T, *P_ATTRIBUTE_HDR_T;


/* P2P 4.1.2 - P2P Status Attribute */
typedef struct _P2P_ATTRI_STATUS_T {
    UINT_8      ucId;                   /* Attribute ID */
    UINT_16     u2Length;               /* Length */
    UINT_8      ucStatusCode;           /* Status Code */
} __KAL_ATTRIB_PACKED__ P2P_ATTRI_STATUS_T, *P_P2P_ATTRI_STATUS_T;


/* P2P 4.1.3 - P2P Minor Reason Code Attribute */
typedef struct _P2P_ATTRI_REASON_T {
    UINT_8      ucId;                   /* Attribute ID */
    UINT_16     u2Length;               /* Length */
    UINT_8      ucMinorReasonCode;      /* Minor Reason Code */
} __KAL_ATTRIB_PACKED__ P2P_ATTRI_REASON_T, *P_P2P_ATTRI_REASON_T;


/* P2P 4.1.4 - P2P Capability Attribute */
typedef struct _P2P_ATTRI_CAPABILITY_T {
    UINT_8      ucId;                   /* Attribute ID */
    UINT_16     u2Length;               /* Length */
    UINT_8      ucDeviceCap;            /* Device Capability Bitmap */
    UINT_8      ucGroupCap;             /* Group Capability Bitmap */
} __KAL_ATTRIB_PACKED__ P2P_ATTRI_CAPABILITY_T, *P_P2P_ATTRI_CAPABILITY_T;


/* P2P 4.1.5 - P2P Device ID Attribute */
typedef struct _P2P_ATTRI_DEV_ID_T {
    UINT_8      ucId;                       /* Attribute ID */
    UINT_16     u2Length;                   /* Length */
    UINT_8      aucDevAddr[MAC_ADDR_LEN];   /* P2P Device Address */
} __KAL_ATTRIB_PACKED__ P2P_ATTRI_DEV_ID_T, *P_P2P_ATTRI_DEV_ID_T;


/* P2P 4.1.6 - Group Owner Intent Attribute */
typedef struct _P2P_ATTRI_GO_INTENT_T {
    UINT_8      ucId;                   /* Attribute ID */
    UINT_16     u2Length;               /* Length */
    UINT_8      ucGOIntent;             /* Group Owner Intent */
} __KAL_ATTRIB_PACKED__ P2P_ATTRI_GO_INTENT_T, *P_P2P_ATTRI_GO_INTENT_T;


/* P2P 4.1.7 - Configuration Timeout Attribute */
typedef struct _P2P_ATTRI_CFG_TIMEOUT_T {
    UINT_8      ucId;                   /* Attribute ID */
    UINT_16     u2Length;               /* Length */
    UINT_8      ucGOCfgTimeout;         /* GO Configuration Timeout */
    UINT_8      ucClientCfgTimeout;     /* Client Configuration Timeout */
} __KAL_ATTRIB_PACKED__ P2P_ATTRI_CFG_TIMEOUT_T, *P_P2P_ATTRI_CFG_TIMEOUT_T;


/* P2P 4.1.8 - Listen Channel Attribute */
typedef struct _P2P_ATTRI_LISTEN_CHANNEL_T {
    UINT_8      ucId;                   /* Attribute ID */
    UINT_16     u2Length;               /* Length */
    UINT_8      aucCountryString[3];    /* Country String */
    UINT_8      ucOperatingClass;       /* Operating Class from 802.11 Annex J/P802.11 REVmb 3.0 */
    UINT_8      ucChannelNumber;        /* Channel Number */
} __KAL_ATTRIB_PACKED__ P2P_ATTRI_LISTEN_CHANNEL_T, *P_P2P_ATTRI_LISTEN_CHANNEL_T;


/* P2P 4.1.9 - P2P Group BSSID Attribute */
typedef struct _P2P_ATTRI_GROUP_BSSID_T {
    UINT_8      ucId;                   /* Attribute ID */
    UINT_16     u2Length;               /* Length */
    UINT_8      aucBssid[MAC_ADDR_LEN]; /* P2P Group BSSID */
} __KAL_ATTRIB_PACKED__ P2P_ATTRI_GROUP_BSSID_T, *P_P2P_ATTRI_GROUP_BSSID_T;


/* P2P 4.1.10 - Extended Listen Timing Attribute */
typedef struct _P2P_ATTRI_EXT_LISTEN_TIMING_T {
    UINT_8      ucId;                   /* Attribute ID */
    UINT_16     u2Length;               /* Length */
    UINT_16     u2AvailPeriod;          /* Availability Period */
    UINT_16     u2AvailInterval;        /* Availability Interval */
} __KAL_ATTRIB_PACKED__ P2P_ATTRI_EXT_LISTEN_TIMING_T, *P_P2P_ATTRI_EXT_LISTEN_TIMING_T;


/* P2P 4.1.11 - Intended P2P Interface Address Attribute */
typedef struct _P2P_ATTRI_INTENDED_IF_ADDR_T {
    UINT_8      ucId;                   /* Attribute ID */
    UINT_16     u2Length;               /* Length */
    UINT_8      aucIfAddr[MAC_ADDR_LEN];/* P2P Interface Address */
} __KAL_ATTRIB_PACKED__ P2P_ATTRI_INTENDED_IF_ADDR_T, *P_P2P_ATTRI_INTENDED_IF_ADDR_T;


/* P2P 4.1.12 - P2P Manageability Attribute */
typedef struct _P2P_ATTRI_MANAGEABILITY_T {
    UINT_8      ucId;                   /* Attribute ID */
    UINT_16     u2Length;               /* Length */
    UINT_8      ucManageability;        /* P2P Manageability Bitmap */
} __KAL_ATTRIB_PACKED__ P2P_ATTRI_MANAGEABILITY_T, *P_P2P_ATTRI_MANAGEABILITY_T;


/* P2P 4.1.13 - Channel List Attribute */
typedef struct _P2P_ATTRI_CHANNEL_LIST_T {
    UINT_8      ucId;                   /* Attribute ID */
    UINT_16     u2Length;               /* Length */
    UINT_8      aucCountryString[3];    /* Country String */
    UINT_8      aucChannelEntry[1];     /* Channel Entry List */
} __KAL_ATTRIB_PACKED__ P2P_ATTRI_CHANNEL_T, *P_P2P_ATTRI_CHANNEL_T;

typedef struct _CHANNEL_ENTRY_FIELD_T {
    UINT_8      ucRegulatoryClass;      /* Regulatory Class */
    UINT_8      ucNumberOfChannels;     /* Number Of Channels */
    UINT_8      aucChannelList[1];      /* Channel List */
} __KAL_ATTRIB_PACKED__ CHANNEL_ENTRY_FIELD_T, *P_CHANNEL_ENTRY_FIELD_T;


/* P2P 4.1.14 - Notice of Absence Attribute */
typedef struct _P2P_ATTRI_NOA_T {
    UINT_8      ucId;                   /* Attribute ID */
    UINT_16     u2Length;               /* Length */
    UINT_8      ucIndex;                /* Index */
    UINT_8      ucCTWOppPSParam;        /* CTWindow and OppPS Parameters */
    UINT_8      aucNoADesc[1];          /* NoA Descriptor */
} __KAL_ATTRIB_PACKED__ P2P_ATTRI_NOA_T, *P_P2P_ATTRI_NOA_T;

typedef struct _NOA_DESCRIPTOR_T {
    UINT_8      ucCountType;            /* Count/Type */
    UINT_32     u4Duration;             /* Duration */
    UINT_32     u4Interval;             /* Interval */
    UINT_32     u4StartTime;            /* Start Time */
} __KAL_ATTRIB_PACKED__ NOA_DESCRIPTOR_T, *P_NOA_DESCRIPTOR_T;

typedef struct _P2P_ATTRI_DEV_INFO_T {
    UINT_8      ucId;                           /* Attribute ID */
    UINT_16     u2Length;                       /* Length */
    UINT_8      aucDevAddr[MAC_ADDR_LEN];       /* P2P Device Address */
    UINT_16     u2ConfigMethodsBE;               /* Config Method */
    DEVICE_TYPE_T rPrimaryDevTypeBE;            /* Primary Device Type */
    UINT_8      ucNumOfSecondaryDevType;        /* Number of Secondary Device Types */
    DEVICE_TYPE_T arSecondaryDevTypeListBE[1];  /* Secondary Device Type List */
} __KAL_ATTRIB_PACKED__ P2P_ATTRI_DEV_INFO_T, *P_P2P_ATTRI_DEV_INFO_T;

/* WPS 7.1 & 11 WPS TLV Data Format - Device Name */
typedef struct _DEVICE_NAME_TLV_T {
    UINT_16     u2Id;                       /* WPS Attribute Type */
    UINT_16     u2Length;                   /* Data Length */
    UINT_8      aucName[32];                /* Device Name */    // TODO: Fixme
} __KAL_ATTRIB_PACKED__ DEVICE_NAME_TLV_T, *P_DEVICE_NAME_TLV_T;


/* P2P 4.1.16 - P2P Group Info Attribute */
typedef struct _P2P_CLIENT_INFO_DESC_T {
    UINT_8      ucLength;                       /* Length */
    UINT_8      aucDevAddr[MAC_ADDR_LEN];       /* P2P Device Address */
    UINT_8      aucIfAddr[MAC_ADDR_LEN];        /* P2P Interface Address */
    UINT_8      ucDeviceCap;                    /* Device Capability Bitmap */
    UINT_16     u2ConfigMethodsBE;               /* Config Method */
    DEVICE_TYPE_T rPrimaryDevTypeBE;            /* Primary Device Type */
    UINT_8      ucNumOfSecondaryDevType;        /* Number of Secondary Device Types */
    DEVICE_TYPE_T arSecondaryDevTypeListBE[1];  /* Secondary Device Type List */
} __KAL_ATTRIB_PACKED__ P2P_CLIENT_INFO_DESC_T, *P_P2P_CLIENT_INFO_DESC_T;

typedef struct _P2P_ATTRI_GROUP_INFO_T {
    UINT_8      ucId;                       /* Attribute ID */
    UINT_16     u2Length;                   /* Length */
    P2P_CLIENT_INFO_DESC_T arClientDesc[1]; /* P2P Client Info Descriptors */
} __KAL_ATTRIB_PACKED__ P2P_ATTRI_GROUP_INFO_T, *P_P2P_ATTRI_GROUP_INFO_T;


/* P2P 4.1.17 - P2P Group ID Attribute */
typedef struct _P2P_ATTRI_GROUP_ID_T {
    UINT_8      ucId;                       /* Attribute ID */
    UINT_16     u2Length;                   /* Length */
    UINT_8      aucDevAddr[MAC_ADDR_LEN];   /* P2P Device Address */
    UINT_8      aucSSID[ELEM_MAX_LEN_SSID]; /* SSID */
} __KAL_ATTRIB_PACKED__ P2P_ATTRI_GROUP_ID_T, *P_P2P_ATTRI_GROUP_ID_T;


/* P2P 4.1.18 - P2P Interface Attribute */
typedef struct _P2P_ATTRI_INTERFACE_T {
    UINT_8      ucId;                       /* Attribute ID */
    UINT_16     u2Length;                   /* Length */
    UINT_8      aucDevAddr[MAC_ADDR_LEN];   /* P2P Device Address */
    UINT_8      ucIfAddrCount;              /* P2P Interface Address Count */
    UINT_8      aucIfAddrList[MAC_ADDR_LEN];/* P2P Interface Address List */
} __KAL_ATTRIB_PACKED__ P2P_ATTRI_INTERFACE_T, *P_P2P_ATTRI_INTERFACE_T;


/* P2P 4.1.19 - Operating Channel Attribute */
typedef struct _P2P_ATTRI_OPERATING_CHANNEL_T {
    UINT_8      ucId;                   /* Attribute ID */
    UINT_16     u2Length;               /* Length */
    UINT_8      aucCountryString[3];    /* Country String */
    UINT_8      ucOperatingClass;       /* Operating Class from 802.11 Annex J/P802.11 REVmb 3.0 */
    UINT_8      ucChannelNumber;        /* Channel Number */
} __KAL_ATTRIB_PACKED__ P2P_ATTRI_OPERATING_CHANNEL_T, *P_P2P_ATTRI_OPERATING_CHANNEL_T;

/* P2P 4.1.20 - Invitation Flags Attribute */
typedef struct _P2P_ATTRI_INVITATION_FLAG_T {
    UINT_8 ucId;                        /* Attribute ID */
    UINT_16 u2Length;               /* Length */
    UINT_8 ucInviteFlagsBitmap;    /* Invitation Flags Bitmap */
} __KAL_ATTRIB_PACKED__ P2P_ATTRI_INVITATION_FLAG_T, *P_P2P_ATTRI_INVITATION_FLAG_T;



/* P2P 4.1.1 - General WSC Attribute */
typedef struct _WSC_ATTRIBUTE_T {
    UINT_16     u2Id;                   /* Attribute ID */
    UINT_16     u2Length;               /* Length */
    UINT_8      aucBody[1];             /* Body field */
} __KAL_ATTRIB_PACKED__ WSC_ATTRIBUTE_T, *P_WSC_ATTRIBUTE_T;

/* WSC 1.0 Table 28 */
typedef struct _WSC_ATTRI_VERSION_T {
    UINT_16     u2Id;                   /* Attribute ID */
    UINT_16     u2Length;               /* Length */
    UINT_8      ucVersion;              /* Version 1.0 or 1.1 */
} __KAL_ATTRIB_PACKED__ WSC_ATTRI_VERSION_T, *P_WSC_ATTRI_VERSION_T;

typedef struct _WSC_ATTRI_DEVICE_PASSWORD_ID_T {
    UINT_16     u2Id;                   /* Attribute ID */
    UINT_16     u2Length;               /* Length */
    UINT_16     u2DevPasswordId;        /* Device Password ID */
} __KAL_ATTRIB_PACKED__ WSC_ATTRI_DEVICE_PASSWORD_ID_T, *P_WSC_ATTRI_DEVICE_PASSWORD_ID_T;


typedef struct _WSC_ATTRI_CONFIGURATION_METHOD_T {
    UINT_16 u2Id;                       /* Attribute ID */
    UINT_16 u2Length;               /* Length */
    UINT_16 u2ConfigMethods;     /* Configure Methods */
} __KAL_ATTRIB_PACKED__ WSC_ATTRI_CONFIGURATION_METHOD_T, *P_WSC_ATTRI_CONFIGURATION_METHOD_T;


#if defined(WINDOWS_DDK) || defined(WINDOWS_CE)
#pragma pack()
#endif


//3 /* --------------- WFA P2P Attributes Handler prototype --------------- */
typedef UINT_32 (*PFN_APPEND_ATTRI_FUNC)(P_ADAPTER_T, BOOLEAN, PUINT_16, PUINT_8, UINT_16);

typedef VOID (*PFN_HANDLE_ATTRI_FUNC)(P_SW_RFB_T, P_P2P_ATTRIBUTE_T);

typedef VOID (*PFN_VERIFY_ATTRI_FUNC)(P_SW_RFB_T, P_P2P_ATTRIBUTE_T, PUINT_16);

typedef UINT_32 (*PFN_CALCULATE_VAR_ATTRI_LEN_FUNC)(P_ADAPTER_T, P_STA_RECORD_T);


typedef struct _APPEND_VAR_ATTRI_ENTRY_T {
    UINT_16                             u2EstimatedFixedAttriLen; /* For fixed length */
    PFN_CALCULATE_VAR_ATTRI_LEN_FUNC    pfnCalculateVariableAttriLen;
    PFN_APPEND_ATTRI_FUNC               pfnAppendAttri;
} APPEND_VAR_ATTRI_ENTRY_T, *P_APPEND_VAR_ATTRI_ENTRY_T;

typedef enum _ENUM_CONFIG_METHOD_SEL {
    ENUM_CONFIG_METHOD_SEL_AUTO,
    ENUM_CONFIG_METHOD_SEL_USER,
    ENUM_CONFIG_METHOD_SEL_NUM
} ENUM_CONFIG_METHOD_SEL, *P_ENUM_CONFIG_METHOD_SEL;

typedef enum _ENUM_P2P_FORMATION_POLICY {
    ENUM_P2P_FORMATION_POLICY_AUTO = 0,
    ENUM_P2P_FORMATION_POLICY_PASSIVE,   /* Device would wait GO NEGO REQ instead of sending it actively. */
    ENUM_P2P_FORMATION_POLICY_NUM
} ENUM_P2P_FORMATION_POLICY, P_ENUM_P2P_FORMATION_POLICY;

typedef enum _ENUM_P2P_INVITATION_POLICY {
    ENUM_P2P_INVITATION_POLICY_USER = 0,
    ENUM_P2P_INVITATION_POLICY_ACCEPT_FIRST,
    ENUM_P2P_INVITATION_POLICY_DENY_ALL,
    ENUM_P2P_INVITATION_POLICY_NUM
} ENUM_P2P_INVITATION_POLICY, P_ENUM_P2P_INVITATION_POLICY;

//3 /* --------------- Data Structure for P2P Operation --------------- */
//3 /* Session for CONNECTION SETTINGS of P2P */
struct _P2P_CONNECTION_SETTINGS_T {
    UINT_8 ucDevNameLen;
    UINT_8 aucDevName[WPS_ATTRI_MAX_LEN_DEVICE_NAME];

    DEVICE_TYPE_T rPrimaryDevTypeBE;

    ENUM_P2P_FORMATION_POLICY eFormationPolicy;            /* Formation Policy. */

    /*------------WSC Related Param---------------*/
    UINT_16 u2ConfigMethodsSupport;              /* Prefered configure method.
                                                                        * Some device may not have keypad.
                                                                        */
    ENUM_CONFIG_METHOD_SEL eConfigMethodSelType;
    UINT_16 u2TargetConfigMethod;                        /* Configure method selected by user or auto. */
    UINT_16 u2LocalConfigMethod;                        /* Configure method of target. */
    BOOLEAN fgIsPasswordIDRdy;
    /*------------WSC Related Param---------------*/

    UINT_8 ucClientConfigTimeout;
    UINT_8 ucGoConfigTimeout;

    UINT_8 ucSecondaryDevTypeCount;
#if P2P_MAX_SUPPORTED_SEC_DEV_TYPE_COUNT
    DEVICE_TYPE_T                   arSecondaryDevTypeBE[P2P_MAX_SUPPORTED_SEC_DEV_TYPE_COUNT];
#endif


#if 0
    UINT_8 ucRfChannelListCount;
#if P2P_MAX_SUPPORTED_CHANNEL_LIST_COUNT
    UINT_8            aucChannelList[P2P_MAX_SUPPORTED_CHANNEL_LIST_COUNT];      /* Channel Numbering depends on 802.11mb Annex J. */

#endif
#else
    UINT_8 ucRfChannelListSize;
#if P2P_MAX_SUPPORTED_CHANNEL_LIST_SIZE
    UINT_8 aucChannelEntriesField[P2P_MAX_SUPPORTED_CHANNEL_LIST_SIZE];
#endif
#endif

    /* Go Intent */
    UINT_8 ucTieBreaker;
    UINT_8 ucGoIntent;

    /* For Device Capability */
    BOOLEAN fgSupportServiceDiscovery;
    BOOLEAN fgSupportClientDiscoverability;
    BOOLEAN fgSupportConcurrentOperation;
    BOOLEAN fgSupportInfraManaged;
    BOOLEAN fgSupportInvitationProcedure;

    /* For Group Capability */
    BOOLEAN fgSupportPersistentP2PGroup;
    BOOLEAN fgSupportIntraBSSDistribution;
    BOOLEAN fgSupportCrossConnection;
    BOOLEAN fgSupportPersistentReconnect;

    BOOLEAN fgP2pGroupLimit;

    BOOLEAN fgSupportOppPS;
    UINT_16 u2CTWindow;

    BOOLEAN fgIsScanReqIssued;
    BOOLEAN fgIsServiceDiscoverIssued;


    /*============ Target Device Connection Settings ============*/

    /* Discover Target Device Info. */
    BOOLEAN fgIsDevId;
    BOOLEAN fgIsDevType;

    /* Encryption mode of Target Device */
    ENUM_PARAM_AUTH_MODE_T          eAuthMode;

    /* SSID
      *  1. AP Mode, this is the desired SSID user specified.
      *  2. Client Mode, this is the target SSID to be connected to.
      */
    UINT_8 aucSSID[ELEM_MAX_LEN_SSID];
    UINT_8 ucSSIDLen;

    /* Operating channel requested. */
    UINT_8 ucOperatingChnl;
    ENUM_BAND_T eBand;

    /* Linten channel requested. */
    UINT_8 ucListenChnl;

    /* For device discover address/type. */
    UINT_8 aucTargetDevAddr[MAC_ADDR_LEN];   /* P2P Device Address, for P2P Device Discovery & P2P Connection. */

#if CFG_ENABLE_WIFI_DIRECT
    P_P2P_DEVICE_DESC_T prTargetP2pDesc;
#endif

    UINT_8 ucLastStatus;  /* P2P FSM would append status attribute according to this field. */


#if !CFG_DISABLE_DELAY_PROVISION_DISCOVERY
    UINT_8 ucLastDialogToken;
    UINT_8 aucIndicateDevAddr[MAC_ADDR_LEN];
#endif

#if 0
    UINT_8 ucTargetRfChannelListCount;
#if P2P_MAX_SUPPORTED_CHANNEL_LIST_COUNT
    UINT_8            aucTargetChannelList[P2P_MAX_SUPPORTED_CHANNEL_LIST_COUNT];      /* Channel Numbering depends on 802.11mb Annex J. */
#endif
#endif

};


typedef struct _NOA_TIMING_T {
    BOOLEAN     fgIsInUse;              /* Indicate if this entry is in use or not */
    UINT_8      ucCount;                /* Count */

    UINT_8      aucReserved[2];

    UINT_32     u4Duration;             /* Duration */
    UINT_32     u4Interval;             /* Interval */
    UINT_32     u4StartTime;            /* Start Time */
} NOA_TIMING_T, *P_NOA_TIMING_T;

typedef enum _ENUM_P2P_IOCTL_T {
    P2P_IOCTL_IDLE = 0,
    P2P_IOCTL_DEV_DISCOVER,
    P2P_IOCTL_INVITATION_REQ,
    P2P_IOCTL_SERV_DISCOVER,
    P2P_IOCTL_WAITING,
    P2P_IOCTL_NUM
} ENUM_P2P_IOCTL_T;



/*---------------- Service Discovery Related -------------------*/
typedef enum _ENUM_SERVICE_TX_TYPE_T {
    ENUM_SERVICE_TX_TYPE_BY_DA,
    ENUM_SERVICE_TX_TYPE_BY_CHNL,
    ENUM_SERVICE_TX_TYPE_NUM
} ENUM_SERVICE_TX_TYPE_T;


typedef struct _SERVICE_DISCOVERY_FRAME_DATA_T {
    QUE_ENTRY_T rQueueEntry;
    P_MSDU_INFO_T prSDFrame;
    ENUM_SERVICE_TX_TYPE_T eServiceType;
    UINT_8 ucSeqNum;
    union {

        UINT_8 ucChannelNum;
        UINT_8 aucPeerAddr[MAC_ADDR_LEN];
    } uTypeData;
    BOOLEAN fgIsTxDoneIndicate;
} SERVICE_DISCOVERY_FRAME_DATA_T, *P_SERVICE_DISCOVERY_FRAME_DATA_T;




struct _P2P_FSM_INFO_T_DEPRECATED {
    /* P2P FSM State */
    ENUM_P2P_STATE_T    eCurrentState;

    /* Channel */
    BOOLEAN fgIsChannelRequested;










    ENUM_P2P_STATE_T    ePreviousState;

    ENUM_P2P_STATE_T    eReturnState;     /* Return state after current activity finished or abort. */

    UINT_8 aucTargetIfAddr[PARAM_MAC_ADDR_LEN];
    P_BSS_DESC_T prTargetBss; /* BSS of target P2P Device. For Connection/Service Discovery */

    P_STA_RECORD_T prTargetStaRec;

    BOOLEAN fgIsRsponseProbe; /* Indicate if P2P FSM can response probe request frame. */

    /* Sequence number of requested message. */
    UINT_8 ucSeqNumOfReqMsg;        /* Used for SAA FSM request message. */

    /* Channel Privilege */
    UINT_8 ucSeqNumOfChReq;         /* Used for Channel Request message. */


    UINT_8 ucSeqNumOfScnMsg;        /* Used for SCAN FSM request message. */
    UINT_8 ucSeqNumOfCancelMsg;

    UINT_8 ucDialogToken;
    UINT_8 ucRxDialogToken;

    /* Timer */
    TIMER_T rDeviceDiscoverTimer;     /* For device discovery time of each discovery request from user.*/
    TIMER_T rOperationListenTimer;     /* For Find phase under operational state. */
    TIMER_T rFSMTimer;                      /* A timer used for Action frame timeout usage. */

    TIMER_T rRejoinTimer;                      /* A timer used for Action frame timeout usage. */


    /* Flag to indicate Provisioning */
    BOOLEAN fgIsConnectionRequested;

    /* Current IOCTL. */
    ENUM_P2P_IOCTL_T eP2pIOCTL;

    UINT_8              ucAvailableAuthTypes;       /* Used for AUTH_MODE_AUTO_SWITCH */

    /*--------SERVICE DISCOVERY--------*/
    QUE_T rQueueGASRx;    /* Input Request/Response. */
    QUE_T rQueueGASTx;    /* Output Response. */
    P_SERVICE_DISCOVERY_FRAME_DATA_T prSDRequest;
    UINT_8 ucVersionNum;   /* GAS packet sequence number for...Action Frame? */
    UINT_8 ucGlobalSeqNum; /* Sequence Number of RX SD packet. */
    /*--------Service DISCOVERY--------*/

    /*--------DEVICE DISCOVERY---------*/
    UINT_8 aucTargetGroupID[PARAM_MAC_ADDR_LEN];
    UINT_16 u2TargetGroupSsidLen;
    UINT_8 aucTargetSsid[32];
    UINT_8 aucSearchingP2pDevice[PARAM_MAC_ADDR_LEN];
    UINT_8 ucDLToken;
    /*----------------------------------*/

    /* Indicating Peer Status. */
    UINT_32 u4Flags;

    /*Indicating current running mode.*/
    BOOLEAN fgIsApMode;


    /*------------INVITATION------------*/
    ENUM_P2P_INVITATION_POLICY eInvitationRspPolicy;
    /*----------------------------------*/

};



struct _P2P_SPECIFIC_BSS_INFO_T {
    /* For GO(AP) Mode - Compose TIM IE */
    UINT_16                 u2SmallestAID;
    UINT_16                 u2LargestAID;
    UINT_8                  ucBitmapCtrl;
    //UINT_8                  aucPartialVirtualBitmap[MAX_LEN_TIM_PARTIAL_BMP];

    /* For GC/GO OppPS */
    BOOLEAN                 fgEnableOppPS;
    UINT_16                 u2CTWindow;

    /* For GC/GO NOA */
    UINT_8                  ucNoAIndex;
    UINT_8                  ucNoATimingCount; /* Number of NoA Timing */
    NOA_TIMING_T            arNoATiming[P2P_MAXIMUM_NOA_COUNT];

    BOOLEAN                 fgIsNoaAttrExisted;

    /* For P2P Device */
    UINT_8                  ucRegClass;       /* Regulatory Class for channel. */
    UINT_8                  ucListenChannel; /* Linten Channel only on channels 1, 6 and 11 in the 2.4 GHz. */

    UINT_8 ucPreferredChannel; /* Operating Channel, should be one of channel list in p2p connection settings. */
    ENUM_CHNL_EXT_T eRfSco;
    ENUM_BAND_T eRfBand;

    /* Extened Listen Timing. */
    UINT_16 u2AvailabilityPeriod;
    UINT_16 u2AvailabilityInterval;


#if 0 //LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
    UINT_16                 u2IELenForBCN;
    UINT_8                  aucBeaconIECache[P2P_MAXIMUM_ATTRIBUTES_CACHE_SIZE + WPS_MAXIMUM_ATTRIBUTES_CACHE_SIZE];

//    UINT_16                 u2IELenForProbeRsp;
//    UINT_8                  aucProbeRspIECache[P2P_MAXIMUM_ATTRIBUTES_CACHE_SIZE + WPS_MAXIMUM_ATTRIBUTES_CACHE_SIZE];

    UINT_16                 u2IELenForAssocRsp;
    UINT_8                  aucAssocRspIECache[P2P_MAXIMUM_ATTRIBUTES_CACHE_SIZE + WPS_MAXIMUM_ATTRIBUTES_CACHE_SIZE];
    
#else
    UINT_16                 u2AttributeLen;
    UINT_8                  aucAttributesCache[P2P_MAXIMUM_ATTRIBUTES_CACHE_SIZE];

    UINT_16                 u2WscAttributeLen;
    UINT_8                  aucWscAttributesCache[WPS_MAXIMUM_ATTRIBUTES_CACHE_SIZE];
#endif
    UINT_8 aucGroupID[MAC_ADDR_LEN];
    UINT_16 u2GroupSsidLen;
    UINT_8 aucGroupSsid[ELEM_MAX_LEN_SSID];

    PARAM_CUSTOM_NOA_PARAM_STRUC_T rNoaParam;
    PARAM_CUSTOM_OPPPS_PARAM_STRUC_T rOppPsParam;

	UINT_16                 u2WpaIeLen;
	UINT_8                  aucWpaIeBuffer[ELEM_HDR_LEN + ELEM_MAX_LEN_WPA];

};







typedef struct _MSG_P2P_DEVICE_DISCOVER_T {
    MSG_HDR_T       rMsgHdr;    /* Must be the first member */
    UINT_32 u4DevDiscoverTime;    /* 0: Infinite, 1~X: in unit of MS. */
    BOOLEAN fgIsSpecificType;
#if CFG_ENABLE_WIFI_DIRECT
    P2P_DEVICE_TYPE_T rTargetDeviceType;
#endif
    UINT_8 aucTargetDeviceID[MAC_ADDR_LEN];
} MSG_P2P_DEVICE_DISCOVER_T, *P_MSG_P2P_DEVICE_DISCOVER_T;



typedef struct _MSG_P2P_INVITATION_REQUEST_T {
    MSG_HDR_T       rMsgHdr;    /* Must be the first member */
    UINT_8 aucDeviceID[MAC_ADDR_LEN];    /* Target Device ID to be invited. */
} MSG_P2P_INVITATION_REQUEST_T, *P_MSG_P2P_INVITATION_REQUEST_T;

typedef struct _MSG_P2P_FUNCTION_SWITCH_T {
    MSG_HDR_T       rMsgHdr;    /* Must be the first member */
    BOOLEAN fgIsFuncOn;
} MSG_P2P_FUNCTION_SWITCH_T, *P_MSG_P2P_FUNCTION_SWITCH_T;

typedef struct _MSG_P2P_SERVICE_DISCOVERY_REQUEST_T {
    MSG_HDR_T       rMsgHdr;    /* Must be the first member */
    UINT_8 aucDeviceID[MAC_ADDR_LEN];
    BOOLEAN fgNeedTxDoneIndicate;
    UINT_8 ucSeqNum;
} MSG_P2P_SERVICE_DISCOVERY_REQUEST_T, *P_MSG_P2P_SERVICE_DISCOVERY_REQUEST_T;



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
#define p2pChangeMediaState(_prAdapter, _eNewMediaState) \
            (_prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_P2P_INDEX].eConnectionState = (_eNewMediaState));

#define ATTRI_ID(_fp)       (((P_P2P_ATTRIBUTE_T) _fp)->ucId)
#define ATTRI_LEN(_fp)      \
            (((UINT_16) ((PUINT_8)&((P_P2P_ATTRIBUTE_T) _fp)->u2Length)[0]) | \
             ((UINT_16) ((PUINT_8)&((P_P2P_ATTRIBUTE_T) _fp)->u2Length)[1] << 8))


#define ATTRI_SIZE(_fp)     (P2P_ATTRI_HDR_LEN + ATTRI_LEN(_fp))

#define P2P_ATTRI_FOR_EACH(_pucAttriBuf, _u2AttriBufLen, _u2Offset) \
    for ((_u2Offset) = 0; ((_u2Offset) < (_u2AttriBufLen)); \
        (_u2Offset) += ATTRI_SIZE(_pucAttriBuf), ((_pucAttriBuf) += ATTRI_SIZE(_pucAttriBuf)) )


#define P2P_IE(_fp)          ((P_IE_P2P_T) _fp)


#define WSC_ATTRI_ID(_fp)     \
             (((UINT_16) ((PUINT_8)&((P_WSC_ATTRIBUTE_T) _fp)->u2Id)[0] << 8) | \
             ((UINT_16) ((PUINT_8)&((P_WSC_ATTRIBUTE_T) _fp)->u2Id)[1]))

#define WSC_ATTRI_LEN(_fp)      \
            (((UINT_16) ((PUINT_8)&((P_WSC_ATTRIBUTE_T) _fp)->u2Length)[0] << 8) | \
             ((UINT_16) ((PUINT_8)&((P_WSC_ATTRIBUTE_T) _fp)->u2Length)[1]))


#define WSC_ATTRI_SIZE(_fp)     (WSC_ATTRI_HDR_LEN + WSC_ATTRI_LEN(_fp))

#define WSC_ATTRI_FOR_EACH(_pucAttriBuf, _u2AttriBufLen, _u2Offset) \
    for ((_u2Offset) = 0; ((_u2Offset) < (_u2AttriBufLen)); \
        (_u2Offset) += WSC_ATTRI_SIZE(_pucAttriBuf), ((_pucAttriBuf) += WSC_ATTRI_SIZE(_pucAttriBuf)) )

#define WSC_IE(_fp)          ((P_IE_P2P_T) _fp)


#define WFD_ATTRI_ID(_fp)       (((P_WFD_ATTRIBUTE_T) _fp)->ucElemID)

#define WFD_ATTRI_LEN(_fp)      \
            (((UINT_16) ((PUINT_8)&((P_WFD_ATTRIBUTE_T) _fp)->u2Length)[0] << 8) | \
             ((UINT_16) ((PUINT_8)&((P_WFD_ATTRIBUTE_T) _fp)->u2Length)[1]))

#define WFD_ATTRI_SIZE(_fp)     (WFD_ATTRI_HDR_LEN + WFD_ATTRI_LEN(_fp))

#define WFD_ATTRI_FOR_EACH(_pucAttriBuf, _u2AttriBufLen, _u2Offset) \
    for ((_u2Offset) = 0; ((_u2Offset) < (_u2AttriBufLen)); \
        (_u2Offset) += WFD_ATTRI_SIZE(_pucAttriBuf), ((_pucAttriBuf) += WFD_ATTRI_SIZE(_pucAttriBuf)) )




#if DBG
    #define ASSERT_BREAK(_exp) \
        { \
            if (!(_exp)) { \
                ASSERT(FALSE); \
                break; \
            } \
        }

#else
    #define ASSERT_BREAK(_exp)
#endif


/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*======P2P State======*/
VOID
p2pStateInit_LISTEN(
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo,
    IN P_P2P_SPECIFIC_BSS_INFO_T prSP2pBssInfo,
    IN UINT_8 ucListenChannel
    );

VOID
p2pStateAbort_LISTEN(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsChannelExtenstion
    );

VOID
p2pStateAbort_SEARCH_SCAN(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsChannelExtenstion
    );

VOID
p2pStateAbort_GO_OPERATION(
    IN P_ADAPTER_T prAdapter
    );

VOID
p2pStateAbort_GC_OPERATION(
    IN P_ADAPTER_T prAdapter
    );

VOID
p2pStateInit_CONFIGURATION(
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo,
    IN P_BSS_INFO_T prP2pBssInfo,
    IN P_P2P_SPECIFIC_BSS_INFO_T prP2pSpecBssInfo
    );

VOID
p2pStateAbort_CONFIGURATION(
    IN P_ADAPTER_T prAdapter
    );

VOID
p2pStateInit_JOIN(
    IN P_ADAPTER_T prAdapter
    );

VOID
p2pStateAbort_JOIN(
    IN P_ADAPTER_T prAdapter
    );

/*====== P2P Functions ======*/


VOID
p2pFuncInitGO(
    IN P_ADAPTER_T prAdapter
    );





VOID
p2pFuncDisconnect(
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec,
    IN BOOLEAN fgSendDeauth,
    IN UINT_16 u2ReasonCode
    );

VOID
p2pFuncSwitchOPMode(
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_INFO_T prP2pBssInfo,
    IN ENUM_OP_MODE_T eOpMode,
    IN BOOLEAN fgSyncToFW
    );

VOID
p2pFuncRunEventProvisioningComplete(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

WLAN_STATUS
p2pFuncSetGroupID(
    IN P_ADAPTER_T prAdapter,
    IN PUINT_8 pucGroupID,
    IN PUINT_8 pucSsid,
    IN UINT_8 ucSsidLen
    );


WLAN_STATUS
p2pFuncSendDeviceDiscoverabilityReqFrame(
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 aucDestAddr[],
    IN UINT_8 ucDialogToken
    );

WLAN_STATUS
p2pFuncSendDeviceDiscoverabilityRspFrame(
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 aucDestAddr[],
    IN UINT_8 ucDialogToken
    );


UINT_8
p2pFuncGetVersionNumOfSD(
    IN P_ADAPTER_T prAdapter
    );

/*====== P2P FSM ======*/
VOID
p2pFsmRunEventConnectionRequest(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
p2pFsmRunEventDeviceDiscoveryRequest(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
p2pFsmRunEventDeviceDiscoveryAbort(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
p2pFsmRunEventRxGroupNegotiationReqFrame(
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb
    );

WLAN_STATUS
p2pFsmRunEventGroupNegotiationRequestTxDone(
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo,
    IN ENUM_TX_RESULT_CODE_T rTxDoneStatus
    );

WLAN_STATUS
p2pFsmRunEventGroupNegotiationResponseTxDone(
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo,
    IN ENUM_TX_RESULT_CODE_T rTxDoneStatus
    );

WLAN_STATUS
p2pFsmRunEventGroupNegotiationConfirmTxDone(
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo,
    IN ENUM_TX_RESULT_CODE_T rTxDoneStatus
    );

WLAN_STATUS
p2pFsmRunEventProvisionDiscoveryRequestTxDone(
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo,
    IN ENUM_TX_RESULT_CODE_T rTxDoneStatus
    );

WLAN_STATUS
p2pFsmRunEventProvisionDiscoveryResponseTxDone(
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo,
    IN ENUM_TX_RESULT_CODE_T rTxDoneStatus
    );


WLAN_STATUS
p2pFsmRunEventInvitationRequestTxDone (
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo,
    IN ENUM_TX_RESULT_CODE_T rTxDoneStatus
    );



VOID
p2pFsmRunEventRxDeauthentication(
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec,
    IN P_SW_RFB_T prSwRfb
    );

VOID
p2pFsmRunEventRxDisassociation(
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec,
    IN P_SW_RFB_T prSwRfb
    );

VOID
p2pFsmRunEventBeaconTimeout(
    IN P_ADAPTER_T prAdapter
    );



WLAN_STATUS
p2pFsmRunEventDeauthTxDone(
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo,
    IN ENUM_TX_RESULT_CODE_T rTxDoneStatus
    );


#if 1
#endif


//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
/*======Mail Box Event Message=====*/


VOID
p2pFsmRunEventConnectionAbort(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
p2pFsmRunEventConnectionTrigger(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );


VOID
p2pFsmRunEventP2PFunctionSwitch(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
p2pFsmRunEventChGrant(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
p2pFsmRunEventJoinComplete(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
p2pFsmRunEventConnectionPause(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
p2pIndicationOfMediaStateToHost(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_PARAM_MEDIA_STATE_T eConnectionState,
    IN UINT_8 aucTargetAddr[]
    );

VOID
p2pUpdateBssInfoForJOIN(
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec,
    IN P_SW_RFB_T prAssocRspSwRfb
    );

/*======Mail Box Event Message=====*/


VOID
p2pFsmInit(
    IN P_ADAPTER_T prAdapter
    );

VOID
p2pFsmUninit(
    IN P_ADAPTER_T prAdapter
    );

VOID
p2pFsmSteps(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_P2P_STATE_T eNextState
    );

VOID
p2pStartGO(
    IN P_ADAPTER_T prAdapter
    );

VOID
p2pAssignSsid(
    IN PUINT_8 pucSsid,
    IN PUINT_8 pucSsidLen
    );

VOID
p2pFsmRunEventScanDone(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
p2pFsmRunEventIOReqTimeout(
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4Param
    );

VOID
p2pFsmRunEventSearchPeriodTimeout(
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4Param
    );

VOID
p2pFsmRunEventFsmTimeout(
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4Param
    );

VOID
p2pFsmRunEventRejoinTimeout(
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4Parm
    );




/*=============== P2P Function Related ================*/

/*=============== P2P Function Related ================*/


#if CFG_TEST_WIFI_DIRECT_GO
VOID
p2pTest(
    IN P_ADAPTER_T prAdapter
    );
#endif /* CFG_TEST_WIFI_DIRECT_GO */




VOID
p2pGenerateP2P_IEForBeacon(
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo
    );

VOID
p2pGenerateP2P_IEForAssocReq(
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo
    );

VOID
p2pGenerateP2P_IEForAssocRsp(
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo
    );


VOID
p2pGenerateP2P_IEForProbeReq(
    IN P_ADAPTER_T prAdapter,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );




UINT_32
p2pCalculateP2P_IELenForBeacon(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    );

UINT_32
p2pCalculateP2P_IELenForAssocRsp(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    );


UINT_32
p2pCalculateP2P_IELenForProbeReq(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    );



VOID
p2pGenerateWSC_IEForProbeResp(
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo
    );

VOID
p2pGenerateWSC_IEForProbeReq(
    IN P_ADAPTER_T prAdapter,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );



UINT_16
p2pCalculateWSC_IELenForProbeReq(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex
    );

UINT_32
p2pCalculateWSC_IELenForProbeResp(
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    );

UINT_32
p2pAppendAttriStatus(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );



UINT_32
p2pAppendAttriCapability(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

UINT_32
p2pAppendAttriGoIntent(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

UINT_32
p2pAppendAttriCfgTimeout(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

UINT_32
p2pAppendAttriGroupBssid(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );


UINT_32
p2pAppendAttriDeviceIDForBeacon(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

UINT_32
p2pAppendAttriDeviceIDForProbeReq(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

UINT_32
p2pAppendAttriDeviceIDForDeviceDiscoveryReq(
        IN P_ADAPTER_T prAdapter,
        IN BOOLEAN fgIsAssocFrame,
        IN PUINT_16 pu2Offset,
        IN PUINT_8 pucBuf,
        IN UINT_16 u2BufSize
        );

UINT_32
p2pAppendAttriListenChannel(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

UINT_32
p2pAppendAttriIntendP2pIfAddr(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );


UINT_32
p2pAppendAttriChannelList(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

UINT_32
p2pCalculateAttriLenChannelList(
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec
    );

UINT_32
p2pAppendAttriNoA(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

UINT_32
p2pAppendAttriDeviceInfo(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

UINT_32
p2pCalculateAttriLenDeviceInfo(
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec
    );

UINT_32
p2pAppendAttriGroupInfo(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

UINT_32
p2pCalculateAttriLenGroupInfo(
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec
    );


UINT_32
p2pAppendAttriP2pGroupID(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

UINT_32
p2pAppendAttriOperatingChannel(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

UINT_32
p2pAppendAttriInvitationFlag(
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );


VOID
p2pGenerateWscIE (
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 ucOuiType,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize,
    IN APPEND_VAR_ATTRI_ENTRY_T arAppendAttriTable[],
    IN UINT_32 u4AttriTableSize
    );

UINT_32
p2pAppendAttriWSCConfigMethod (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

UINT_32
p2pAppendAttriWSCVersion (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

UINT_32
p2pAppendAttriWSCGONegReqDevPasswordId (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

UINT_32
p2pAppendAttriWSCGONegRspDevPasswordId (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    );

WLAN_STATUS
p2pGetWscAttriList(
    IN P_ADAPTER_T prAdapter,
    IN UINT_8       ucOuiType,
    IN PUINT_8      pucIE,
    IN UINT_16      u2IELength,
    OUT PPUINT_8    ppucAttriList,
    OUT PUINT_16    pu2AttriListLen
    );

WLAN_STATUS
p2pGetAttriList (
    IN P_ADAPTER_T  prAdapter,
    IN UINT_8       ucOuiType,
    IN PUINT_8      pucIE,
    IN UINT_16      u2IELength,
    OUT PPUINT_8    ppucAttriList,
    OUT PUINT_16    pu2AttriListLen
    );

VOID
p2pRunEventAAATxFail (
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec
    );

WLAN_STATUS
p2pRunEventAAASuccess (
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec
    );


WLAN_STATUS
p2pRunEventAAAComplete (
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec
    );

WLAN_STATUS
p2pSendProbeResponseFrame(
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb
    );

BOOLEAN
p2pFsmRunEventRxProbeRequestFrame(
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb
    );

VOID
p2pFsmRunEventRxProbeResponseFrame(
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb,
    IN P_BSS_DESC_T prBssDesc
    );

WLAN_STATUS
p2pRxPublicActionFrame(
    IN P_ADAPTER_T     prAdapter,
    IN P_SW_RFB_T      prSwRfb
    );

WLAN_STATUS
p2pRxActionFrame(
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb
    );

VOID
p2pFsmRunEventRxGroupNegotiationRspFrame(
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb
    );

VOID
p2pFsmRunEventRxGroupNegotiationCfmFrame(
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb
    );


#if 0 // frog
BOOLEAN
scanMatchFilterOfP2P (
    IN P_SW_RFB_T prSWRfb,
    IN PP_BSS_DESC_T pprBssDesc
    );
#endif // frog

VOID
p2pProcessEvent_UpdateNOAParam (
    IN P_ADAPTER_T    prAdapter,
    UINT_8  ucNetTypeIndex,
    P_EVENT_UPDATE_NOA_PARAMS_T prEventUpdateNoaParam
    );

VOID
p2pFuncCompleteIOCTL(
    IN P_ADAPTER_T prAdapter,
    IN WLAN_STATUS rWlanStatus
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
#ifndef _lint
/* Kevin: we don't have to call following function to inspect the data structure.
 * It will check automatically while at compile time.
 * We'll need this for porting driver to different RTOS.
 */
__KAL_INLINE__ VOID
p2pDataTypeCheck (
    VOID
    )
{
    DATA_STRUC_INSPECTING_ASSERT(sizeof(IE_P2P_T) == (2+4+1)); // all UINT_8
    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_ATTRIBUTE_T) == (3+1));
    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_ATTRI_STATUS_T) == (3+1));
    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_ATTRI_REASON_T) == (3+1));
    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_ATTRI_CAPABILITY_T) == (3+2));
    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_ATTRI_DEV_ID_T) == (3+6));
    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_ATTRI_GO_INTENT_T) == (3+1));
    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_ATTRI_CFG_TIMEOUT_T) == (3+2));
#if CID52_53_54
    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_ATTRI_LISTEN_CHANNEL_T) == (3+5));
#else
    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_ATTRI_LISTEN_CHANNEL_T) == (3+5));
#endif
    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_ATTRI_GROUP_BSSID_T) == (3+6));
    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_ATTRI_EXT_LISTEN_TIMING_T) == (3+4));
    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_ATTRI_INTENDED_IF_ADDR_T) == (3+6));
    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_ATTRI_MANAGEABILITY_T) == (3+1));

    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_ATTRI_CHANNEL_T) == (3+4));
    DATA_STRUC_INSPECTING_ASSERT(sizeof(CHANNEL_ENTRY_FIELD_T) == 3);
    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_ATTRI_NOA_T) == (3+3));
    DATA_STRUC_INSPECTING_ASSERT(sizeof(NOA_DESCRIPTOR_T) == 13);
    DATA_STRUC_INSPECTING_ASSERT(sizeof(DEVICE_TYPE_T) == 8);
    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_ATTRI_DEV_INFO_T) == (3+6+2+8+1+8));
    DATA_STRUC_INSPECTING_ASSERT(sizeof(DEVICE_NAME_TLV_T) == (4+32));
    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_CLIENT_INFO_DESC_T) == (1+6+6+1+2+8+1+8));
    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_ATTRI_GROUP_INFO_T) == (3+(1+6+6+1+2+8+1+8)));
    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_ATTRI_GROUP_ID_T) == (3+38));
    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_ATTRI_INTERFACE_T) == (3+13));
#if CID52_53_54
    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_ATTRI_OPERATING_CHANNEL_T) == (3+5));
#else
    DATA_STRUC_INSPECTING_ASSERT(sizeof(P2P_ATTRI_OPERATING_CHANNEL_T) == (3+5));
#endif


    return;
}
#endif /* _lint */

#endif /* _P2P_FSM_H */




