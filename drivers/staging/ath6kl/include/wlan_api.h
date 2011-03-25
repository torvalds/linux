//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
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
// This file contains the API for the host wlan module
//
// Author(s): ="Atheros"
//==============================================================================
#ifndef _HOST_WLAN_API_H_
#define _HOST_WLAN_API_H_


#ifdef __cplusplus
extern "C" {
#endif

#include <a_osapi.h>

struct ieee80211_node_table;
struct ieee80211_frame;

struct ieee80211_common_ie {
    u16 ie_chan;
    u8 *ie_tstamp;
    u8 *ie_ssid;
    u8 *ie_rates;
    u8 *ie_xrates;
    u8 *ie_country;
    u8 *ie_wpa;
    u8 *ie_rsn;
    u8 *ie_wmm;
    u8 *ie_ath;
    u16 ie_capInfo;
    u16 ie_beaconInt;
    u8 *ie_tim;
    u8 *ie_chswitch;
    u8 ie_erp;
    u8 *ie_wsc;
    u8 *ie_htcap;
    u8 *ie_htop;
#ifdef WAPI_ENABLE
    u8 *ie_wapi;
#endif
};

typedef struct bss {
    u8 ni_macaddr[6];
    u8 ni_snr;
    s16 ni_rssi;
    struct bss                   *ni_list_next;
    struct bss                   *ni_list_prev;
    struct bss                   *ni_hash_next;
    struct bss                   *ni_hash_prev;
    struct ieee80211_common_ie   ni_cie;
    u8 *ni_buf;
    u16 ni_framelen;
    struct ieee80211_node_table *ni_table;
    u32 ni_refcnt;
    int                          ni_scangen;

    u32 ni_tstamp;
    u32 ni_actcnt;
#ifdef OS_ROAM_MANAGEMENT
    u32 ni_si_gen;
#endif
} bss_t;

typedef void wlan_node_iter_func(void *arg, bss_t *);

bss_t *wlan_node_alloc(struct ieee80211_node_table *nt, int wh_size);
void wlan_node_free(bss_t *ni);
void wlan_setup_node(struct ieee80211_node_table *nt, bss_t *ni,
                const u8 *macaddr);
bss_t *wlan_find_node(struct ieee80211_node_table *nt, const u8 *macaddr);
void wlan_node_reclaim(struct ieee80211_node_table *nt, bss_t *ni);
void wlan_free_allnodes(struct ieee80211_node_table *nt);
void wlan_iterate_nodes(struct ieee80211_node_table *nt, wlan_node_iter_func *f,
                        void *arg);

void wlan_node_table_init(void *wmip, struct ieee80211_node_table *nt);
void wlan_node_table_reset(struct ieee80211_node_table *nt);
void wlan_node_table_cleanup(struct ieee80211_node_table *nt);

int wlan_parse_beacon(u8 *buf, int framelen,
                           struct ieee80211_common_ie *cie);

u16 wlan_ieee2freq(int chan);
u32 wlan_freq2ieee(u16 freq);

void wlan_set_nodeage(struct ieee80211_node_table *nt, u32 nodeAge);

void
wlan_refresh_inactive_nodes (struct ieee80211_node_table *nt);

bss_t *
wlan_find_Ssidnode (struct ieee80211_node_table *nt, u8 *pSsid,
                    u32 ssidLength, bool bIsWPA2, bool bMatchSSID);

void
wlan_node_return (struct ieee80211_node_table *nt, bss_t *ni);

bss_t *wlan_node_remove(struct ieee80211_node_table *nt, u8 *bssid);

bss_t *
wlan_find_matching_Ssidnode (struct ieee80211_node_table *nt, u8 *pSsid,
                    u32 ssidLength, u32 dot11AuthMode, u32 authMode,
                   u32 pairwiseCryptoType, u32 grpwiseCryptoTyp);

#ifdef __cplusplus
}
#endif

#endif /* _HOST_WLAN_API_H_ */
