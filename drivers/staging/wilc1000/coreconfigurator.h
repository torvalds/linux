
/*!
 *  @file	coreconfigurator.h
 *  @brief
 *  @author
 *  @sa		coreconfigurator.c
 *  @date	1 Mar 2012
 *  @version	1.0
 */


#ifndef CORECONFIGURATOR_H
#define CORECONFIGURATOR_H

#include "wilc_wlan_if.h"
/*****************************************************************************/
/* Constants                                                                 */
/*****************************************************************************/
/* Number of WID Options Supported */
#define NUM_BASIC_SWITCHES      45
#define NUM_FHSS_SWITCHES       0

#define NUM_RSSI                5

#ifdef MAC_802_11N
#define NUM_11N_BASIC_SWITCHES  25
#define NUM_11N_HUT_SWITCHES    47
#else /* MAC_802_11N */
#define NUM_11N_BASIC_SWITCHES  0
#define NUM_11N_HUT_SWITCHES    0
#endif /* MAC_802_11N */

extern u16 g_num_total_switches;

#define MAC_HDR_LEN             24          /* No Address4 - non-ESS         */
#define MAX_SSID_LEN            33
#define FCS_LEN                 4
#define TIME_STAMP_LEN          8
#define BEACON_INTERVAL_LEN     2
#define CAP_INFO_LEN            2
#define STATUS_CODE_LEN         2
#define AID_LEN                 2
#define IE_HDR_LEN              2


/* Operating Mode: SET */
#define SET_CFG              0
/* Operating Mode: GET */
#define GET_CFG              1

#define MAX_PACKET_BUFF_SIZE 1596

#define MAX_STRING_LEN               256
#define MAX_SURVEY_RESULT_FRAG_SIZE  MAX_STRING_LEN
#define SURVEY_RESULT_LENGTH         44
#define MAX_ASSOC_RESP_FRAME_SIZE    MAX_STRING_LEN

#define STATUS_MSG_LEN               12
#define MAC_CONNECTED                1
#define MAC_DISCONNECTED             0



/*****************************************************************************/
/* Function Macros                                                           */
/*****************************************************************************/
#define MAKE_WORD16(lsb, msb) ((((u16)(msb) << 8) & 0xFF00) | (lsb))
#define MAKE_WORD32(lsw, msw) ((((u32)(msw) << 16) & 0xFFFF0000) | (lsw))


/*****************************************************************************/
/* Type Definitions                                                                                                                       */
/*****************************************************************************/
/* Status Codes for Authentication and Association Frames */
typedef enum {
	SUCCESSFUL_STATUSCODE    = 0,
	UNSPEC_FAIL              = 1,
	UNSUP_CAP                = 10,
	REASOC_NO_ASOC           = 11,
	FAIL_OTHER               = 12,
	UNSUPT_ALG               = 13,
	AUTH_SEQ_FAIL            = 14,
	CHLNG_FAIL               = 15,
	AUTH_TIMEOUT             = 16,
	AP_FULL                  = 17,
	UNSUP_RATE               = 18,
	SHORT_PREAMBLE_UNSUP     = 19,
	PBCC_UNSUP               = 20,
	CHANNEL_AGIL_UNSUP       = 21,
	SHORT_SLOT_UNSUP         = 25,
	OFDM_DSSS_UNSUP          = 26,
	CONNECT_STS_FORCE_16_BIT = 0xFFFF
} tenuConnectSts;

typedef struct {
	u16 u16WIDid;
	tenuWIDtype enuWIDtype;
	s32 s32ValueSize;
	s8      *ps8WidVal;

} tstrWID;

typedef struct {
	u8 u8Full;
	u8 u8Index;
	s8 as8RSSI[NUM_RSSI];
} tstrRSSI;
/* This structure is used to support parsing of the received 'N' message */
typedef struct {
	s8 s8rssi;
	u16 u16CapInfo;
	u8 au8ssid[MAX_SSID_LEN];
	u8 u8SsidLen;
	u8 au8bssid[6];
	u16 u16BeaconPeriod;
	u8 u8DtimPeriod;
	u8 u8channel;
	unsigned long u32TimeRcvdInScanCached; /* of type unsigned long to be accepted by the linux kernel macro time_after() */
	unsigned long u32TimeRcvdInScan;
	bool bNewNetwork;
#ifdef AGING_ALG
	u8 u8Found;
#endif
#ifdef WILC_P2P
	u32 u32Tsf; /* time-stamp [Low only 32 bit] */
#endif
	u8 *pu8IEs;
	u16 u16IEsLen;
	void *pJoinParams;
	tstrRSSI strRssi;
	u64 u64Tsf; /* time-stamp [Low and High 64 bit] */
} tstrNetworkInfo;

/* This structure is used to support parsing of the received Association Response frame */
typedef struct {
	u16 u16capability;
	u16 u16ConnectStatus;
	u16 u16AssocID;
	u8 *pu8RespIEs;
	u16 u16RespIEsLen;
} tstrConnectRespInfo;


typedef struct {
	u8 au8bssid[6];
	u8 *pu8ReqIEs;
	size_t ReqIEsLen;
	u8 *pu8RespIEs;
	u16 u16RespIEsLen;
	u16 u16ConnectStatus;
} tstrConnectInfo;



typedef struct {
	u16 u16reason;
	u8 *ie;
	size_t ie_len;
} tstrDisconnectNotifInfo;

#ifndef CONNECT_DIRECT
typedef struct wid_site_survey_reslts {
	char SSID[MAX_SSID_LEN];
	u8 BssType;
	u8 Channel;
	u8 SecurityStatus;
	u8 BSSID[6];
	char RxPower;
	u8 Reserved;

} wid_site_survey_reslts_s;
#endif

extern s32 CoreConfiguratorInit(void);
extern s32 CoreConfiguratorDeInit(void);

extern s32 SendConfigPkt(u8 u8Mode, tstrWID *pstrWIDs,
				 u32 u32WIDsCount, bool bRespRequired, u32 drvHandler);
extern s32 ParseNetworkInfo(u8 *pu8MsgBuffer, tstrNetworkInfo **ppstrNetworkInfo);
extern s32 DeallocateNetworkInfo(tstrNetworkInfo *pstrNetworkInfo);

extern s32 ParseAssocRespInfo(u8 *pu8Buffer, u32 u32BufferLen,
				      tstrConnectRespInfo **ppstrConnectRespInfo);
extern s32 DeallocateAssocRespInfo(tstrConnectRespInfo *pstrConnectRespInfo);

#ifndef CONNECT_DIRECT
extern s32 ParseSurveyResults(u8 ppu8RcvdSiteSurveyResults[][MAX_SURVEY_RESULT_FRAG_SIZE],
				      wid_site_survey_reslts_s **ppstrSurveyResults, u32 *pu32SurveyResultsCount);
extern s32 DeallocateSurveyResults(wid_site_survey_reslts_s *pstrSurveyResults);
#endif

extern s32 SendRawPacket(s8 *pspacket, s32 s32PacketLen);
extern void NetworkInfoReceived(u8 *pu8Buffer, u32 u32Length);
void GnrlAsyncInfoReceived(u8 *pu8Buffer, u32 u32Length);
void host_int_ScanCompleteReceived(u8 *pu8Buffer, u32 u32Length);

#endif
