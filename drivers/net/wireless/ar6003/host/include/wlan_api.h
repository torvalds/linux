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
    A_UINT16    ie_chan;
    A_UINT8     *ie_tstamp;
    A_UINT8     *ie_ssid;
    A_UINT8     *ie_rates;
    A_UINT8     *ie_xrates;
    A_UINT8     *ie_country;
    A_UINT8     *ie_wpa;
    A_UINT8     *ie_rsn;
    A_UINT8     *ie_wmm;
    A_UINT8     *ie_ath;
    A_UINT16    ie_capInfo;
    A_UINT16    ie_beaconInt;
    A_UINT8     *ie_tim;
    A_UINT8     *ie_chswitch;
    A_UINT8     ie_erp;
    A_UINT8     *ie_wsc;
    A_UINT8     *ie_htcap;
    A_UINT8     *ie_htop;
#ifdef WAPI_ENABLE
    A_UINT8     *ie_wapi;
#endif


};

typedef struct bss {
    A_UINT8                      ni_macaddr[6];
    A_UINT8                      ni_snr;
    A_INT16                      ni_rssi;
    struct bss                   *ni_list_next;
    struct bss                   *ni_list_prev;
    struct bss                   *ni_hash_next;
    struct bss                   *ni_hash_prev;
    struct ieee80211_common_ie   ni_cie;
#ifdef P2P
    void                         *p2p_dev;
#endif /* P2P */
    A_UINT8                     *ni_buf;
    A_UINT16                     ni_framelen;
    A_UINT8                      ni_frametype; /* frame type in ni_buf */
    struct ieee80211_node_table *ni_table;
    A_UINT32                     ni_refcnt;
    int                          ni_scangen;

    A_UINT32                     ni_tstamp;
    A_UINT32                     ni_actcnt;
#ifdef OS_ROAM_MANAGEMENT
    A_UINT32                     ni_si_gen;
#endif
} bss_t;

typedef void wlan_node_iter_func(void *arg, bss_t *);

bss_t *wlan_node_alloc(struct ieee80211_node_table *nt, int wh_size);
void wlan_node_free(bss_t *ni);
void wlan_setup_node(struct ieee80211_node_table *nt, bss_t *ni,
                const A_UINT8 *macaddr);
bss_t *wlan_find_node(struct ieee80211_node_table *nt, const A_UINT8 *macaddr);
void wlan_node_reclaim(struct ieee80211_node_table *nt, bss_t *ni);
A_STATUS wlan_node_buf_update(struct ieee80211_node_table *nt, bss_t *ni, A_UINT32 len);
void wlan_free_allnodes(struct ieee80211_node_table *nt);
void wlan_iterate_nodes(struct ieee80211_node_table *nt, wlan_node_iter_func *f,
                        void *arg);

void wlan_node_table_init(void *wmip, struct ieee80211_node_table *nt);
void wlan_node_table_reset(struct ieee80211_node_table *nt);
void wlan_node_table_cleanup(struct ieee80211_node_table *nt);

A_STATUS wlan_parse_beacon(A_UINT8 *buf, int framelen,
                           struct ieee80211_common_ie *cie);

A_UINT16 wlan_ieee2freq(int chan);
A_UINT32 wlan_freq2ieee(A_UINT16 freq);

void wlan_set_nodeage(struct ieee80211_node_table *nt, A_UINT32 nodeAge);

void
wlan_refresh_inactive_nodes (struct ieee80211_node_table *nt);

bss_t *
wlan_find_Ssidnode (struct ieee80211_node_table *nt, A_UCHAR *pSsid,
                    A_UINT32 ssidLength, A_BOOL bIsWPA2, A_BOOL bMatchSSID);

void
wlan_node_return (struct ieee80211_node_table *nt, bss_t *ni);

bss_t *wlan_node_remove(struct ieee80211_node_table *nt, A_UINT8 *bssid);

bss_t *
wlan_find_matching_Ssidnode (struct ieee80211_node_table *nt, A_UCHAR *pSsid,
                    A_UINT32 ssidLength, A_UINT32 dot11AuthMode, A_UINT32 authMode,
                   A_UINT32 pairwiseCryptoType, A_UINT32 grpwiseCryptoTyp);

void wlan_node_update_timestamp(struct ieee80211_node_table *nt, bss_t *ni);

#ifdef __cplusplus
}
#endif

#endif /* _HOST_WLAN_API_H_ */
