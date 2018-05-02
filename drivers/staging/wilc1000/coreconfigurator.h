/* SPDX-License-Identifier: GPL-2.0 */
#ifndef CORECONFIGURATOR_H
#define CORECONFIGURATOR_H

#include "wilc_wlan_if.h"

#define NUM_RSSI                5

#define MAC_HDR_LEN             24
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
#define MAX_ASSOC_RESP_FRAME_SIZE    MAX_STRING_LEN

#define MAKE_WORD16(lsb, msb) ((((u16)(msb) << 8) & 0xFF00) | (lsb))
#define MAKE_WORD32(lsw, msw) ((((u32)(msw) << 16) & 0xFFFF0000) | (lsw))

enum connect_status {
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
};

struct rssi_history_buffer {
	bool full;
	u8 index;
	s8 samples[NUM_RSSI];
};

struct network_info {
	s8 rssi;
	u16 cap_info;
	u8 ssid[MAX_SSID_LEN];
	u8 ssid_len;
	u8 bssid[6];
	u16 beacon_period;
	u8 dtim_period;
	u8 ch;
	unsigned long time_scan_cached;
	unsigned long time_scan;
	bool new_network;
	u8 found;
	u32 tsf_lo;
	u8 *ies;
	u16 ies_len;
	void *join_params;
	struct rssi_history_buffer rssi_history;
	u64 tsf_hi;
};

struct connect_resp_info {
	u16 capability;
	u16 status;
	u16 assoc_id;
	u8 *ies;
	u16 ies_len;
};

struct connect_info {
	u8 bssid[6];
	u8 *req_ies;
	size_t req_ies_len;
	u8 *resp_ies;
	u16 resp_ies_len;
	u16 status;
};

struct disconnect_info {
	u16 reason;
	u8 *ie;
	size_t ie_len;
};

s32 wilc_parse_network_info(u8 *msg_buffer,
			    struct network_info **ret_network_info);
s32 wilc_parse_assoc_resp_info(u8 *buffer, u32 buffer_len,
			       struct connect_resp_info **ret_connect_resp_info);
void wilc_scan_complete_received(struct wilc *wilc, u8 *buffer, u32 length);
void wilc_network_info_received(struct wilc *wilc, u8 *buffer, u32 length);
void wilc_gnrl_async_info_received(struct wilc *wilc, u8 *buffer, u32 length);
#endif
