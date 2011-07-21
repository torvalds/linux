/*
 * Copyright (c) 2004-2011 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "htc.h"
#include "wmi.h"
#include "debug.h"

struct bss *wlan_node_alloc(int wh_size)
{
	struct bss *ni;

	ni = kzalloc(sizeof(struct bss), GFP_ATOMIC);

	if ((ni != NULL) && wh_size) {
		ni->ni_buf = kmalloc(wh_size, GFP_ATOMIC);
		if (ni->ni_buf == NULL) {
			kfree(ni);
			return NULL;
		}
	}

	return ni;
}

void wlan_node_free(struct bss *ni)
{
	kfree(ni->ni_buf);
	kfree(ni);
}

void wlan_setup_node(struct ath6kl_node_table *nt, struct bss *ni,
		     const u8 *mac_addr)
{
	int hash;

	memcpy(ni->ni_macaddr, mac_addr, ETH_ALEN);
	hash = ATH6KL_NODE_HASH(mac_addr);
	ni->ni_refcnt = 1;

	ni->ni_tstamp = jiffies_to_msecs(jiffies);
	ni->ni_actcnt = WLAN_NODE_INACT_CNT;

	spin_lock_bh(&nt->nt_nodelock);

	/* insert at the end of the node list */
	ni->ni_list_next = NULL;
	ni->ni_list_prev = nt->nt_node_last;
	if (nt->nt_node_last != NULL)
		nt->nt_node_last->ni_list_next = ni;

	nt->nt_node_last = ni;
	if (nt->nt_node_first == NULL)
		nt->nt_node_first = ni;

	/* insert into the hash list */
	ni->ni_hash_next = nt->nt_hash[hash];
	if (ni->ni_hash_next != NULL)
		nt->nt_hash[hash]->ni_hash_prev = ni;

	ni->ni_hash_prev = NULL;
	nt->nt_hash[hash] = ni;

	spin_unlock_bh(&nt->nt_nodelock);
}

struct bss *wlan_find_node(struct ath6kl_node_table *nt,
			   const u8 *mac_addr)
{
	struct bss *ni, *found_ni = NULL;
	int hash;

	spin_lock_bh(&nt->nt_nodelock);

	hash = ATH6KL_NODE_HASH(mac_addr);
	for (ni = nt->nt_hash[hash]; ni; ni = ni->ni_hash_next) {
		if (memcmp(ni->ni_macaddr, mac_addr, ETH_ALEN) == 0) {
			ni->ni_refcnt++;
			found_ni = ni;
			break;
		}
	}

	spin_unlock_bh(&nt->nt_nodelock);

	return found_ni;
}

void wlan_node_reclaim(struct ath6kl_node_table *nt, struct bss *ni)
{
	int hash;

	spin_lock_bh(&nt->nt_nodelock);

	if (ni->ni_list_prev == NULL)
		/* fix list head */
		nt->nt_node_first = ni->ni_list_next;
	else
		ni->ni_list_prev->ni_list_next = ni->ni_list_next;

	if (ni->ni_list_next == NULL)
		/* fix list tail */
		nt->nt_node_last = ni->ni_list_prev;
	else
		ni->ni_list_next->ni_list_prev = ni->ni_list_prev;

	if (ni->ni_hash_prev == NULL) {
		/* first in list so fix the list head */
		hash = ATH6KL_NODE_HASH(ni->ni_macaddr);
		nt->nt_hash[hash] = ni->ni_hash_next;
	} else {
		ni->ni_hash_prev->ni_hash_next = ni->ni_hash_next;
	}

	if (ni->ni_hash_next != NULL)
		ni->ni_hash_next->ni_hash_prev = ni->ni_hash_prev;

	wlan_node_free(ni);

	spin_unlock_bh(&nt->nt_nodelock);
}

static void wlan_node_dec_free(struct bss *ni)
{
	if ((ni->ni_refcnt--) == 1)
		wlan_node_free(ni);
}

void wlan_free_allnodes(struct ath6kl_node_table *nt)
{
	struct bss *ni;

	while ((ni = nt->nt_node_first) != NULL)
		wlan_node_reclaim(nt, ni);
}

void wlan_iterate_nodes(struct ath6kl_node_table *nt, void *arg)
{
	struct bss *ni;

	spin_lock_bh(&nt->nt_nodelock);
	for (ni = nt->nt_node_first; ni; ni = ni->ni_list_next) {
			ni->ni_refcnt++;
			ath6kl_cfg80211_scan_node(arg, ni);
			wlan_node_dec_free(ni);
	}
	spin_unlock_bh(&nt->nt_nodelock);
}

void wlan_node_table_init(struct ath6kl_node_table *nt)
{
	ath6kl_dbg(ATH6KL_DBG_WLAN_NODE, "node table = 0x%lx\n",
		   (unsigned long)nt);

	memset(nt, 0, sizeof(struct ath6kl_node_table));

	spin_lock_init(&nt->nt_nodelock);

	nt->nt_node_age = WLAN_NODE_INACT_TIMEOUT_MSEC;
}

void wlan_refresh_inactive_nodes(struct ath6kl *ar)
{
	struct ath6kl_node_table *nt = &ar->scan_table;
	struct bss *bss;
	u32 now;

	now = jiffies_to_msecs(jiffies);
	bss = nt->nt_node_first;
	while (bss != NULL) {
		/* refresh all nodes except the current bss */
		if (memcmp(ar->bssid, bss->ni_macaddr, ETH_ALEN) != 0) {
			if (((now - bss->ni_tstamp) > nt->nt_node_age)
			    || --bss->ni_actcnt == 0) {
				wlan_node_reclaim(nt, bss);
			}
		}
		bss = bss->ni_list_next;
	}
}

void wlan_node_table_cleanup(struct ath6kl_node_table *nt)
{
	wlan_free_allnodes(nt);
}

struct bss *wlan_find_ssid_node(struct ath6kl_node_table *nt, u8 * ssid,
				u32 ssid_len, bool is_wpa2, bool match_ssid)
{
	struct bss *ni, *found_ni = NULL;
	u8 *ie_ssid;

	spin_lock_bh(&nt->nt_nodelock);

	for (ni = nt->nt_node_first; ni; ni = ni->ni_list_next) {

		ie_ssid = ni->ni_cie.ie_ssid;

		if ((ie_ssid[1] <= IEEE80211_MAX_SSID_LEN) &&
		    (memcmp(ssid, &ie_ssid[2], ssid_len) == 0)) {

				if (match_ssid ||
				    (is_wpa2 && ni->ni_cie.ie_rsn != NULL) ||
				    (!is_wpa2 && ni->ni_cie.ie_wpa != NULL)) {
					ni->ni_refcnt++;
					found_ni = ni;
					break;
				}
		}
	}

	spin_unlock_bh(&nt->nt_nodelock);

	return found_ni;
}

void wlan_node_return(struct ath6kl_node_table *nt, struct bss *ni)
{
	spin_lock_bh(&nt->nt_nodelock);
	wlan_node_dec_free(ni);
	spin_unlock_bh(&nt->nt_nodelock);
}
