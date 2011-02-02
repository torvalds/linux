//------------------------------------------------------------------------------
// <copyright file="ieee80211_node.h" company="Atheros">
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
// Author(s): ="Atheros"
//==============================================================================
#ifndef _IEEE80211_NODE_H_
#define _IEEE80211_NODE_H_

/*
 * Node locking definitions.
 */
#define IEEE80211_NODE_LOCK_INIT(_nt)   A_MUTEX_INIT(&(_nt)->nt_nodelock)
#define IEEE80211_NODE_LOCK_DESTROY(_nt) if (A_IS_MUTEX_VALID(&(_nt)->nt_nodelock)) { \
                                               A_MUTEX_DELETE(&(_nt)->nt_nodelock); }
       
#define IEEE80211_NODE_LOCK(_nt)        A_MUTEX_LOCK(&(_nt)->nt_nodelock)
#define IEEE80211_NODE_UNLOCK(_nt)      A_MUTEX_UNLOCK(&(_nt)->nt_nodelock)
#define IEEE80211_NODE_LOCK_BH(_nt)     A_MUTEX_LOCK(&(_nt)->nt_nodelock)
#define IEEE80211_NODE_UNLOCK_BH(_nt)   A_MUTEX_UNLOCK(&(_nt)->nt_nodelock)
#define IEEE80211_NODE_LOCK_ASSERT(_nt)

/*
 * Node reference counting definitions.
 *
 * ieee80211_node_initref   initialize the reference count to 1
 * ieee80211_node_incref    add a reference
 * ieee80211_node_decref    remove a reference
 * ieee80211_node_dectestref    remove a reference and return 1 if this
 *              is the last reference, otherwise 0
 * ieee80211_node_refcnt    reference count for printing (only)
 */
#define ieee80211_node_initref(_ni)     ((_ni)->ni_refcnt = 1)
#define ieee80211_node_incref(_ni)      ((_ni)->ni_refcnt++)
#define ieee80211_node_decref(_ni)      ((_ni)->ni_refcnt--)
#define ieee80211_node_dectestref(_ni)  (((_ni)->ni_refcnt--) == 1)
#define ieee80211_node_refcnt(_ni)      ((_ni)->ni_refcnt)

#define IEEE80211_NODE_HASHSIZE 32
/* simple hash is enough for variation of macaddr */
#define IEEE80211_NODE_HASH(addr)   \
    (((const u8 *)(addr))[IEEE80211_ADDR_LEN - 1] % \
        IEEE80211_NODE_HASHSIZE)

/*
 * Table of ieee80211_node instances.  Each ieee80211com
 * has at least one for holding the scan candidates.
 * When operating as an access point or in ibss mode there
 * is a second table for associated stations or neighbors.
 */
struct ieee80211_node_table {
    void                   *nt_wmip;       /* back reference */
    A_MUTEX_T               nt_nodelock;    /* on node table */
    struct bss              *nt_node_first; /* information of all nodes */
    struct bss              *nt_node_last;  /* information of all nodes */
    struct bss              *nt_hash[IEEE80211_NODE_HASHSIZE];
    const char              *nt_name;   /* for debugging */
    u32 nt_scangen; /* gen# for timeout scan */
#ifdef THREAD_X
    A_TIMER                 nt_inact_timer;
    u8 isTimerArmed;   /* is the node timer armed */
#endif
    u32 nt_nodeAge; /* node aging time */
#ifdef OS_ROAM_MANAGEMENT
    u32 nt_si_gen; /* gen# for scan indication*/
#endif
};

#ifdef THREAD_X
#define WLAN_NODE_INACT_TIMEOUT_MSEC            20000
#else
#define WLAN_NODE_INACT_TIMEOUT_MSEC            120000
#endif
   
#define WLAN_NODE_INACT_CNT            4

#endif /* _IEEE80211_NODE_H_ */
