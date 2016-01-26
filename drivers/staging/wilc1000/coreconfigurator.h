
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

#define NUM_BASIC_SWITCHES      45
#define NUM_FHSS_SWITCHES       0

#define NUM_RSSI                5

#ifdef MAC_802_11N
#define NUM_11N_BASIC_SWITCHES  25
#define NUM_11N_HUT_SWITCHES    47
#else
#define NUM_11N_BASIC_SWITCHES  0
#define NUM_11N_HUT_SWITCHES    0
#endif

#define MAC_HDR_LEN             24
#define MAX_SSID_LEN            33
#define FCS_LEN                 4
#define TIME_STAMP_LEN          8
#define BEACON_INTERVAL_LEN     2
#define CAP_INFO_LEN            2
#define STATUS_CODE_LEN         2
#define AID_LEN                 2
#define IE_HDR_LEN              2

#define SET_CFG              0
#define GET_CFG              1

#define MAX_STRING_LEN               256
#define MAX_SURVEY_RESULT_FRAG_SIZE  MAX_STRING_LEN
#define SURVEY_RESULT_LENGTH         44
#define MAX_ASSOC_RESP_FRAME_SIZE    MAX_STRING_LEN

#define MAC_CONNECTED                1
#define MAC_DISCONNECTED             0

#define MAKE_WORD16(lsb, msb) ((((u16)(msb) << 8) & 0xFF00) | (lsb))
#define MAKE_WORD32(lsw, msw) ((((u32)(msw) << 16) & 0xFFFF0000) | (lsw))

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

struct wid {
	u16 id;
	enum wid_type type;
	s32 size;
	s8 *val;
};

typedef struct {
	u8 u8Full;
	u8 u8Index;
	s8 as8RSSI[NUM_RSSI];
} tstrRSSI;

typedef struct {
	s8 s8rssi;
	u16 u16CapInfo;
	u8 au8ssid[MAX_SSID_LEN];
	u8 u8SsidLen;
	u8 au8bssid[6];
	u16 u16BeaconPeriod;
	u8 u8DtimPeriod;
	u8 u8channel;
	unsigned long u32TimeRcvdInScanCached;
	unsigned long u32TimeRcvdInScan;
	bool bNewNetwork;
	u8 u8Found;
	u32 u32Tsf;
	u8 *pu8IEs;
	u16 u16IEsLen;
	void *pJoinParams;
	tstrRSSI strRssi;
	u64 u64Tsf;
} tstrNetworkInfo;

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

s32 wilc_send_config_pkt(struct wilc *wilc, u8 mode, struct wid *wids,
			 u32 count, u32 drv);
s32 wilc_parse_network_info(u8 *pu8MsgBuffer, tstrNetworkInfo **ppstrNetworkInfo);
s32 wilc_dealloc_network_info(tstrNetworkInfo *pstrNetworkInfo);

s32 wilc_parse_assoc_resp_info(u8 *pu8Buffer, u32 u32BufferLen,
		       tstrConnectRespInfo **ppstrConnectRespInfo);
s32 wilc_dealloc_assoc_resp_info(tstrConnectRespInfo *pstrConnectRespInfo);
void wilc_scan_complete_received(struct wilc *wilc, u8 *pu8Buffer,
				 u32 u32Length);
void wilc_network_info_received(struct wilc *wilc, u8 *pu8Buffer,
				u32 u32Length);
void wilc_gnrl_async_info_received(struct wilc *wilc, u8 *pu8Buffer,
				   u32 u32Length);
#endif
