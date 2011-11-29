//------------------------------------------------------------------------------
// <copyright file="wlan_node.c" company="Atheros">
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
// IEEE 802.11 node handling support.
//
// Author(s): ="Atheros"
//==============================================================================
#include <a_config.h>
#include <athdefs.h>
#include <a_types.h>
#include <a_osapi.h>
#define ATH_MODULE_NAME wlan
#include <a_debug.h>
#include "htc.h"
#include "htc_api.h"
#include <wmi.h>
#include <ieee80211.h>
#include <wlan_api.h>
#include <wmi_api.h>
#include <ieee80211_node.h>
#ifdef P2P
#include <p2p_api.h>
#endif /* P2P */

#define ATH_DEBUG_WLAN ATH_DEBUG_MAKE_MODULE_MASK(0)

#ifdef DEBUG

static ATH_DEBUG_MASK_DESCRIPTION wlan_debug_desc[] = {
    { ATH_DEBUG_WLAN , "General WLAN Node Tracing"},
};

ATH_DEBUG_INSTANTIATE_MODULE_VAR(wlan,
                                 "wlan",
                                 "WLAN Node Management",
                                 ATH_DEBUG_MASK_DEFAULTS,
                                 ATH_DEBUG_DESCRIPTION_COUNT(wlan_debug_desc),
                                 wlan_debug_desc);
                                 
#endif

static bss_t * _ieee80211_find_node (struct ieee80211_node_table *nt,
                                     const A_UINT8 *macaddr);

bss_t *
wlan_node_alloc(struct ieee80211_node_table *nt, int wh_size)
{
    bss_t *ni;

    ni = A_MALLOC_NOWAIT(sizeof(bss_t));

    if (ni != NULL) {
        A_MEMZERO(ni, sizeof(bss_t));
        if (wh_size)
        {
            ni->ni_buf = A_MALLOC_NOWAIT(wh_size);
            if (ni->ni_buf == NULL) {
                A_FREE(ni);
                ni = NULL;
                return ni;
            } else {
                A_MEMZERO(ni->ni_buf, wh_size);
            }
        }
    } else {
        return ni;
    }

    /* Make sure our lists are clean */
    ni->ni_list_next = NULL;
    ni->ni_list_prev = NULL;
    ni->ni_hash_next = NULL;
    ni->ni_hash_prev = NULL;

    //
    // ni_scangen never initialized before and during suspend/resume of winmobile,
    // that some junk has been stored in this, due to this scan list didn't properly updated
    //
    ni->ni_scangen   = 0;

#ifdef OS_ROAM_MANAGEMENT
    ni->ni_si_gen    = 0;
#endif

    return ni;
}

void
wlan_node_free(bss_t *ni)
{
    if (ni->ni_buf != NULL) {
        A_FREE(ni->ni_buf);
        ni->ni_buf = NULL;
    }
#ifdef P2P
    if (ni->p2p_dev) {
        p2p_device_free(ni->p2p_dev);
    }
#endif /* P2P */
    A_FREE(ni);
    ni = NULL;
}

void
wlan_node_update_timestamp(struct ieee80211_node_table *nt, bss_t *ni)
{
    ni->ni_tstamp = A_GET_MS(0);
    ni->ni_actcnt = WLAN_NODE_INACT_CNT;
    return;
}

void
wlan_setup_node(struct ieee80211_node_table *nt, bss_t *ni,
                const A_UINT8 *macaddr)
{
    int hash;
    A_UINT32 timeoutValue = 0;

    A_MEMCPY(ni->ni_macaddr, macaddr, IEEE80211_ADDR_LEN);
    hash = IEEE80211_NODE_HASH (macaddr);
    ieee80211_node_initref (ni);     /* mark referenced */

    timeoutValue = nt->nt_nodeAge;

    wlan_node_update_timestamp(nt, ni);

    IEEE80211_NODE_LOCK_BH(nt);

    /* Insert at the end of the node list */
    ni->ni_list_next = NULL;
    ni->ni_list_prev = nt->nt_node_last;
    if(nt->nt_node_last != NULL)
    {
        nt->nt_node_last->ni_list_next = ni;
    }
    nt->nt_node_last = ni;
    if(nt->nt_node_first == NULL)
    {
        nt->nt_node_first = ni;
    }

    /* Insert into the hash list i.e. the bucket */
    if((ni->ni_hash_next = nt->nt_hash[hash]) != NULL)
    {
        nt->nt_hash[hash]->ni_hash_prev = ni;
    }
    ni->ni_hash_prev = NULL;
    nt->nt_hash[hash] = ni;

    IEEE80211_NODE_UNLOCK_BH(nt);
}

static bss_t *
_ieee80211_find_node(struct ieee80211_node_table *nt,
    const A_UINT8 *macaddr)
{
    bss_t *ni = NULL;
    int hash = 0;

    IEEE80211_NODE_LOCK_ASSERT(nt);

    hash = IEEE80211_NODE_HASH(macaddr);

    if (hash >= IEEE80211_NODE_HASHSIZE)
    {
        // overflow????
        return NULL;
    }

    if (hash < 0)
    {
        // underflow????
        return NULL;
    }

    if (NULL == nt)
    {
        return NULL;
    }

    if (NULL == nt->nt_hash)
    {
        return NULL;
    }

    if (NULL == nt->nt_hash[hash])
    {
        return NULL;
    }  

    for(ni = nt->nt_hash[hash]; ni; ni = ni->ni_hash_next) {
        if (IEEE80211_ADDR_EQ(ni->ni_macaddr, macaddr)) {
            ieee80211_node_incref(ni);  /* mark referenced */
            return ni;
        }
    }
    return NULL;
}

bss_t *
wlan_find_node(struct ieee80211_node_table *nt, const A_UINT8 *macaddr)
{
    bss_t *ni = NULL;

    if (NULL == nt || NULL == macaddr)
    {
        return ni; // NULL
    }

    IEEE80211_NODE_LOCK(nt);
    ni = _ieee80211_find_node(nt, macaddr);
    IEEE80211_NODE_UNLOCK(nt);
    return ni;
}


/* Update the buffer alone, not the entire node.
 * This involves,
 * 1) Freeing the buffer alone.
 * 2) Allocating a new buffer
 * 3) Update the node timestamp with the current time.
 */
A_STATUS 
wlan_node_buf_update(struct ieee80211_node_table *nt, bss_t *ni, A_UINT32 len)
{
    IEEE80211_NODE_LOCK(nt);

    /* Free the ni_buf alone.
     */
    if (ni->ni_buf != NULL) {
        A_FREE(ni->ni_buf);
        ni->ni_buf = NULL;
    }

    /* Allocate ni_buf for the new length.
     */
    if (len) {
        ni->ni_buf = A_MALLOC_NOWAIT(len);
        if (ni->ni_buf == NULL) {
            return A_ERROR;
        } else {
            A_MEMZERO(ni->ni_buf, len);
        }
    }

    /* Update the Node's timestamp.
     */
    wlan_node_update_timestamp(nt, ni);

    IEEE80211_NODE_UNLOCK(nt);

    return A_OK;
}

/*
 * Reclaim a node.  If this is the last reference count then
 * do the normal free work.  Otherwise remove it from the node
 * table and mark it gone by clearing the back-reference.
 */
void
wlan_node_reclaim(struct ieee80211_node_table *nt, bss_t *ni)
{
    IEEE80211_NODE_LOCK(nt);

    if(ni->ni_list_prev == NULL)
    {
        /* First in list so fix the list head */
        nt->nt_node_first = ni->ni_list_next;
    }
    else
    {
        ni->ni_list_prev->ni_list_next = ni->ni_list_next;
    }

    if(ni->ni_list_next == NULL)
    {
        /* Last in list so fix list tail */
        nt->nt_node_last = ni->ni_list_prev;
    }
    else
    {
        ni->ni_list_next->ni_list_prev = ni->ni_list_prev;
    }

    if(ni->ni_hash_prev == NULL)
    {
        /* First in list so fix the list head */
        int hash;
        hash = IEEE80211_NODE_HASH(ni->ni_macaddr);
        nt->nt_hash[hash] = ni->ni_hash_next;
    }
    else
    {
        ni->ni_hash_prev->ni_hash_next = ni->ni_hash_next;
    }

    if(ni->ni_hash_next != NULL)
    {
        ni->ni_hash_next->ni_hash_prev = ni->ni_hash_prev;
    }
    wlan_node_free(ni);

    IEEE80211_NODE_UNLOCK(nt);
}

static void
wlan_node_dec_free(bss_t *ni)
{
    if (ieee80211_node_dectestref(ni)) {
        wlan_node_free(ni);
    }
}

void
wlan_free_allnodes(struct ieee80211_node_table *nt)
{
    bss_t *ni;

    while ((ni = nt->nt_node_first) != NULL) {
        wlan_node_reclaim(nt, ni);
    }
}

void
wlan_iterate_nodes(struct ieee80211_node_table *nt, wlan_node_iter_func *f,
                   void *arg)
{
    bss_t *ni;
    A_UINT32 gen;

    gen = ++nt->nt_scangen;

    IEEE80211_NODE_LOCK(nt);
    for (ni = nt->nt_node_first; ni; ni = ni->ni_list_next) {
        if (ni->ni_scangen != gen) {
            ni->ni_scangen = gen;
            (void) ieee80211_node_incref(ni);
            (*f)(arg, ni);
            wlan_node_dec_free(ni);
        }
    }
    IEEE80211_NODE_UNLOCK(nt);
}

/*
 * Node table support.
 */
void
wlan_node_table_init(void *wmip, struct ieee80211_node_table *nt)
{
    int i;

    AR_DEBUG_PRINTF(ATH_DEBUG_WLAN, ("node table = 0x%lx\n", (unsigned long)nt));
    IEEE80211_NODE_LOCK_INIT(nt);

    A_REGISTER_MODULE_DEBUG_INFO(wlan);
    
    nt->nt_node_first = nt->nt_node_last = NULL;
    for(i = 0; i < IEEE80211_NODE_HASHSIZE; i++)
    {
        nt->nt_hash[i] = NULL;
    }

    nt->nt_wmip = wmip;
    nt->nt_nodeAge = WLAN_NODE_INACT_TIMEOUT_MSEC;

    //
    // nt_scangen never initialized before and during suspend/resume of winmobile, 
    // that some junk has been stored in this, due to this scan list didn't properly updated
    //
    nt->nt_scangen   = 0;

#ifdef OS_ROAM_MANAGEMENT
    nt->nt_si_gen    = 0;
#endif
}

void
wlan_set_nodeage(struct ieee80211_node_table *nt, A_UINT32 nodeAge)
{
    nt->nt_nodeAge = nodeAge;
    return;
}
void
wlan_refresh_inactive_nodes (struct ieee80211_node_table *nt)
{
    bss_t *bss, *nextBss;
    A_UINT8 myBssid[IEEE80211_ADDR_LEN];
    A_UINT32 timeoutValue = 0;
    A_UINT32 now = A_GET_MS(0);
    timeoutValue = nt->nt_nodeAge;

    wmi_get_current_bssid(nt->nt_wmip, myBssid);

    bss = nt->nt_node_first;
    while (bss != NULL)
    {
        nextBss = bss->ni_list_next;
        if (A_MEMCMP(myBssid, bss->ni_macaddr, sizeof(myBssid)) != 0)
        {
#ifdef P2P
            if (((now - bss->ni_tstamp) > timeoutValue))
#else
            if (((now - bss->ni_tstamp) > timeoutValue)  || --bss->ni_actcnt == 0)
#endif /* P2P */
            {
               /*
                * free up all but the current bss - if set
                */
                wlan_node_reclaim(nt, bss);
            }
        }
        bss = nextBss;
    }
}

void
wlan_node_table_cleanup(struct ieee80211_node_table *nt)
{
    wlan_free_allnodes(nt);
    IEEE80211_NODE_LOCK_DESTROY(nt);
}

bss_t *
wlan_find_Ssidnode (struct ieee80211_node_table *nt, A_UCHAR *pSsid,
                    A_UINT32 ssidLength, A_BOOL bIsWPA2, A_BOOL bMatchSSID)
{
    bss_t   *ni = NULL;
    A_UCHAR *pIESsid = NULL;

    IEEE80211_NODE_LOCK (nt);

    for (ni = nt->nt_node_first; ni; ni = ni->ni_list_next) {
        pIESsid = ni->ni_cie.ie_ssid;
        if ((pIESsid) && pIESsid[1] <= 32) {

            // Step 1 : Check SSID
            if (0x00 == memcmp (pSsid, &pIESsid[2], ssidLength)) {

                //
                // Step 2.1 : Check MatchSSID is TRUE, if so, return Matched SSID
                // Profile, otherwise check whether WPA2 or WPA
                //
                if (TRUE == bMatchSSID) {
                    ieee80211_node_incref (ni);  /* mark referenced */
                    IEEE80211_NODE_UNLOCK (nt);
                    return ni;
                }

                // Step 2 : if SSID matches, check WPA or WPA2
                if (TRUE == bIsWPA2 && NULL != ni->ni_cie.ie_rsn) {
                    ieee80211_node_incref (ni);  /* mark referenced */
                    IEEE80211_NODE_UNLOCK (nt);
                    return ni;
                }
                if (FALSE == bIsWPA2 && NULL != ni->ni_cie.ie_wpa) {
                    ieee80211_node_incref(ni);  /* mark referenced */
                    IEEE80211_NODE_UNLOCK (nt);
                    return ni;
                }
            }
        }
    }

    IEEE80211_NODE_UNLOCK (nt);

    return NULL;
}

void
wlan_node_return (struct ieee80211_node_table *nt, bss_t *ni)
{
    IEEE80211_NODE_LOCK (nt);
    wlan_node_dec_free (ni);
    IEEE80211_NODE_UNLOCK (nt);
}

void
wlan_node_remove_core (struct ieee80211_node_table *nt, bss_t *ni)
{
    if(ni->ni_list_prev == NULL)
    {
        /* First in list so fix the list head */
        nt->nt_node_first = ni->ni_list_next;
    }
    else
    {
        ni->ni_list_prev->ni_list_next = ni->ni_list_next;
    }

    if(ni->ni_list_next == NULL)
    {
        /* Last in list so fix list tail */
        nt->nt_node_last = ni->ni_list_prev;
    }
    else
    {
        ni->ni_list_next->ni_list_prev = ni->ni_list_prev;
    }

    if(ni->ni_hash_prev == NULL)
    {
        /* First in list so fix the list head */
        int hash;
        hash = IEEE80211_NODE_HASH(ni->ni_macaddr);
        nt->nt_hash[hash] = ni->ni_hash_next;
    }
    else
    {
        ni->ni_hash_prev->ni_hash_next = ni->ni_hash_next;
    }

    if(ni->ni_hash_next != NULL)
    {
        ni->ni_hash_next->ni_hash_prev = ni->ni_hash_prev;
    }
}

bss_t *
wlan_node_remove(struct ieee80211_node_table *nt, A_UINT8 *bssid)
{
    bss_t *bss, *nextBss;

    IEEE80211_NODE_LOCK(nt);

    bss = nt->nt_node_first;

    while (bss != NULL)
    {
        nextBss = bss->ni_list_next;

        if (A_MEMCMP(bssid, bss->ni_macaddr, 6) == 0)
        {
            wlan_node_remove_core (nt, bss);
            IEEE80211_NODE_UNLOCK(nt);
            return bss;
        }

        bss = nextBss;
    }

    IEEE80211_NODE_UNLOCK(nt);
    return NULL;
}

bss_t *
wlan_find_matching_Ssidnode (struct ieee80211_node_table *nt, A_UCHAR *pSsid,
                    A_UINT32 ssidLength, A_UINT32 dot11AuthMode, A_UINT32 authMode,
                   A_UINT32 pairwiseCryptoType, A_UINT32 grpwiseCryptoTyp)
{
    bss_t   *ni = NULL;
    bss_t   *best_ni = NULL;
    A_UCHAR *pIESsid = NULL;

    IEEE80211_NODE_LOCK (nt);

    for (ni = nt->nt_node_first; ni; ni = ni->ni_list_next) {
        pIESsid = ni->ni_cie.ie_ssid;
        if (pIESsid[1] <= 32) {

            // Step 1 : Check SSID
            if (0x00 == memcmp (pSsid, &pIESsid[2], ssidLength)) {

                if (ni->ni_cie.ie_capInfo & 0x10)
                {

                    if ((NULL != ni->ni_cie.ie_rsn) && (WMI_WPA2_PSK_AUTH == authMode))
                    {
                        /* WPA2 */
                        if (NULL == best_ni)
                        {
                            best_ni = ni;
                        }
                        else if (ni->ni_rssi > best_ni->ni_rssi)
                        {
                            best_ni = ni;
                        }
                    }
                    else if ((NULL != ni->ni_cie.ie_wpa) && (WMI_WPA_PSK_AUTH == authMode))
                    {
                        /* WPA */
                        if (NULL == best_ni)
                        {
                            best_ni = ni;
                        }
                        else if (ni->ni_rssi > best_ni->ni_rssi)
                        {
                            best_ni = ni;
                        }
                    }
                    else if (WEP_CRYPT == pairwiseCryptoType)
                    {
                        /* WEP */
                        if (NULL == best_ni)
                        {
                            best_ni = ni;
                        }
                        else if (ni->ni_rssi > best_ni->ni_rssi)
                        {
                            best_ni = ni;
                        }
                    }
                }
                else
                {
                    /* open AP */
                    if ((OPEN_AUTH == authMode) && (NONE_CRYPT == pairwiseCryptoType))
                    {
                        if (NULL == best_ni)
                        {
                            best_ni = ni;
                        }
                        else if (ni->ni_rssi > best_ni->ni_rssi)
                        {
                            best_ni = ni;
                        }
                    }
                }
            }
        }
    }

    IEEE80211_NODE_UNLOCK (nt);

    return best_ni;
}

