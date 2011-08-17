//------------------------------------------------------------------------------
// <copyright file="wmi_host.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// This file contains local definitios for the wmi host module.
//
// Author(s): ="Atheros"
//==============================================================================
#ifndef _WMI_HOST_H_
#define _WMI_HOST_H_

#include "roaming.h"
#ifdef __cplusplus
extern "C" {
#endif

struct wmi_stats {
    u32 cmd_len_err;
    u32 cmd_id_err;
};

#define SSID_IE_LEN_INDEX 13

/* Host side link management data structures */
#define SIGNAL_QUALITY_THRESHOLD_LEVELS 6
#define SIGNAL_QUALITY_UPPER_THRESHOLD_LEVELS SIGNAL_QUALITY_THRESHOLD_LEVELS
#define SIGNAL_QUALITY_LOWER_THRESHOLD_LEVELS SIGNAL_QUALITY_THRESHOLD_LEVELS
typedef struct sq_threshold_params_s {
    s16 upper_threshold[SIGNAL_QUALITY_UPPER_THRESHOLD_LEVELS];
    s16 lower_threshold[SIGNAL_QUALITY_LOWER_THRESHOLD_LEVELS];
    u32 upper_threshold_valid_count;
    u32 lower_threshold_valid_count;
    u32 polling_interval;
    u8 weight;
    u8 last_rssi; //normally you would expect this to be bss specific but we keep only one instance because its only valid when the device is in a connected state. Not sure if it belongs to host or target.
    u8 last_rssi_poll_event; //Not sure if it belongs to host or target
} SQ_THRESHOLD_PARAMS;

/*
 * These constants are used with A_WLAN_BAND_SET.
 */ 
#define A_BAND_24GHZ           0
#define A_BAND_5GHZ            1
#define A_NUM_BANDS            2

struct wmi_t {
    bool                          wmi_ready;
    bool                          wmi_numQoSStream;
    u16 wmi_streamExistsForAC[WMM_NUM_AC];
    u8 wmi_fatPipeExists;
    void                           *wmi_devt;
    struct wmi_stats                wmi_stats;
    struct ieee80211_node_table     wmi_scan_table;
    u8 wmi_bssid[ATH_MAC_LEN];
    u8 wmi_powerMode;
    u8 wmi_phyMode;
    u8 wmi_keepaliveInterval;
#ifdef THREAD_X
    A_CSECT_T                       wmi_lock;
#else 
    A_MUTEX_T                       wmi_lock;
#endif
    HTC_ENDPOINT_ID                 wmi_endpoint_id;
    SQ_THRESHOLD_PARAMS             wmi_SqThresholdParams[SIGNAL_QUALITY_METRICS_NUM_MAX];
    CRYPTO_TYPE                     wmi_pair_crypto_type;
    CRYPTO_TYPE                     wmi_grp_crypto_type;
    bool                          wmi_is_wmm_enabled;
    u8 wmi_ht_allowed[A_NUM_BANDS];
    u8 wmi_traffic_class;
};

#ifdef THREAD_X
#define INIT_WMI_LOCK(w)    A_CSECT_INIT(&(w)->wmi_lock)
#define LOCK_WMI(w)         A_CSECT_ENTER(&(w)->wmi_lock);
#define UNLOCK_WMI(w)       A_CSECT_LEAVE(&(w)->wmi_lock);
#define DELETE_WMI_LOCK(w)  A_CSECT_DELETE(&(w)->wmi_lock);
#else
#define LOCK_WMI(w)     A_MUTEX_LOCK(&(w)->wmi_lock);
#define UNLOCK_WMI(w)   A_MUTEX_UNLOCK(&(w)->wmi_lock);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _WMI_HOST_H_ */
