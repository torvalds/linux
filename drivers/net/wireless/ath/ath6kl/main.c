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

#include "core.h"
#include "hif-ops.h"
#include "cfg80211.h"
#include "target.h"
#include "debug.h"

struct ath6kl_sta *ath6kl_find_sta(struct ath6kl *ar, u8 *node_addr)
{
	struct ath6kl_sta *conn = NULL;
	u8 i, max_conn;

	max_conn = (ar->nw_type == AP_NETWORK) ? AP_MAX_NUM_STA : 0;

	for (i = 0; i < max_conn; i++) {
		if (memcmp(node_addr, ar->sta_list[i].mac, ETH_ALEN) == 0) {
			conn = &ar->sta_list[i];
			break;
		}
	}

	return conn;
}

struct ath6kl_sta *ath6kl_find_sta_by_aid(struct ath6kl *ar, u8 aid)
{
	struct ath6kl_sta *conn = NULL;
	u8 ctr;

	for (ctr = 0; ctr < AP_MAX_NUM_STA; ctr++) {
		if (ar->sta_list[ctr].aid == aid) {
			conn = &ar->sta_list[ctr];
			break;
		}
	}
	return conn;
}

static void ath6kl_add_new_sta(struct ath6kl *ar, u8 *mac, u16 aid, u8 *wpaie,
			u8 ielen, u8 keymgmt, u8 ucipher, u8 auth)
{
	struct ath6kl_sta *sta;
	u8 free_slot;

	free_slot = aid - 1;

	sta = &ar->sta_list[free_slot];
	memcpy(sta->mac, mac, ETH_ALEN);
	memcpy(sta->wpa_ie, wpaie, ielen);
	sta->aid = aid;
	sta->keymgmt = keymgmt;
	sta->ucipher = ucipher;
	sta->auth = auth;

	ar->sta_list_index = ar->sta_list_index | (1 << free_slot);
	ar->ap_stats.sta[free_slot].aid = cpu_to_le32(aid);
}

static void ath6kl_sta_cleanup(struct ath6kl *ar, u8 i)
{
	struct ath6kl_sta *sta = &ar->sta_list[i];

	/* empty the queued pkts in the PS queue if any */
	spin_lock_bh(&sta->psq_lock);
	skb_queue_purge(&sta->psq);
	spin_unlock_bh(&sta->psq_lock);

	memset(&ar->ap_stats.sta[sta->aid - 1], 0,
	       sizeof(struct wmi_per_sta_stat));
	memset(sta->mac, 0, ETH_ALEN);
	memset(sta->wpa_ie, 0, ATH6KL_MAX_IE);
	sta->aid = 0;
	sta->sta_flags = 0;

	ar->sta_list_index = ar->sta_list_index & ~(1 << i);

}

static u8 ath6kl_remove_sta(struct ath6kl *ar, u8 *mac, u16 reason)
{
	u8 i, removed = 0;

	if (is_zero_ether_addr(mac))
		return removed;

	if (is_broadcast_ether_addr(mac)) {
		ath6kl_dbg(ATH6KL_DBG_TRC, "deleting all station\n");

		for (i = 0; i < AP_MAX_NUM_STA; i++) {
			if (!is_zero_ether_addr(ar->sta_list[i].mac)) {
				ath6kl_sta_cleanup(ar, i);
				removed = 1;
			}
		}
	} else {
		for (i = 0; i < AP_MAX_NUM_STA; i++) {
			if (memcmp(ar->sta_list[i].mac, mac, ETH_ALEN) == 0) {
				ath6kl_dbg(ATH6KL_DBG_TRC,
					   "deleting station %pM aid=%d reason=%d\n",
					   mac, ar->sta_list[i].aid, reason);
				ath6kl_sta_cleanup(ar, i);
				removed = 1;
				break;
			}
		}
	}

	return removed;
}

enum htc_endpoint_id ath6kl_ac2_endpoint_id(void *devt, u8 ac)
{
	struct ath6kl *ar = devt;
	return ar->ac2ep_map[ac];
}

struct ath6kl_cookie *ath6kl_alloc_cookie(struct ath6kl *ar)
{
	struct ath6kl_cookie *cookie;

	cookie = ar->cookie_list;
	if (cookie != NULL) {
		ar->cookie_list = cookie->arc_list_next;
		ar->cookie_count--;
	}

	return cookie;
}

void ath6kl_cookie_init(struct ath6kl *ar)
{
	u32 i;

	ar->cookie_list = NULL;
	ar->cookie_count = 0;

	memset(ar->cookie_mem, 0, sizeof(ar->cookie_mem));

	for (i = 0; i < MAX_COOKIE_NUM; i++)
		ath6kl_free_cookie(ar, &ar->cookie_mem[i]);
}

void ath6kl_cookie_cleanup(struct ath6kl *ar)
{
	ar->cookie_list = NULL;
	ar->cookie_count = 0;
}

void ath6kl_free_cookie(struct ath6kl *ar, struct ath6kl_cookie *cookie)
{
	/* Insert first */

	if (!ar || !cookie)
		return;

	cookie->arc_list_next = ar->cookie_list;
	ar->cookie_list = cookie;
	ar->cookie_count++;
}

/* set the window address register (using 4-byte register access ). */
static int ath6kl_set_addrwin_reg(struct ath6kl *ar, u32 reg_addr, u32 addr)
{
	int status;
	u8 addr_val[4];
	s32 i;

	/*
	 * Write bytes 1,2,3 of the register to set the upper address bytes,
	 * the LSB is written last to initiate the access cycle
	 */

	for (i = 1; i <= 3; i++) {
		/*
		 * Fill the buffer with the address byte value we want to
		 * hit 4 times.
		 */
		memset(addr_val, ((u8 *)&addr)[i], 4);

		/*
		 * Hit each byte of the register address with a 4-byte
		 * write operation to the same address, this is a harmless
		 * operation.
		 */
		status = hif_read_write_sync(ar, reg_addr + i, addr_val,
					     4, HIF_WR_SYNC_BYTE_FIX);
		if (status)
			break;
	}

	if (status) {
		ath6kl_err("failed to write initial bytes of 0x%x to window reg: 0x%X\n",
			   addr, reg_addr);
		return status;
	}

	/*
	 * Write the address register again, this time write the whole
	 * 4-byte value. The effect here is that the LSB write causes the
	 * cycle to start, the extra 3 byte write to bytes 1,2,3 has no
	 * effect since we are writing the same values again
	 */
	status = hif_read_write_sync(ar, reg_addr, (u8 *)(&addr),
				     4, HIF_WR_SYNC_BYTE_INC);

	if (status) {
		ath6kl_err("failed to write 0x%x to window reg: 0x%X\n",
			   addr, reg_addr);
		return status;
	}

	return 0;
}

/*
 * Read from the ATH6KL through its diagnostic window. No cooperation from
 * the Target is required for this.
 */
int ath6kl_read_reg_diag(struct ath6kl *ar, u32 *address, u32 *data)
{
	int status;

	/* set window register to start read cycle */
	status = ath6kl_set_addrwin_reg(ar, WINDOW_READ_ADDR_ADDRESS,
					*address);

	if (status)
		return status;

	/* read the data */
	status = hif_read_write_sync(ar, WINDOW_DATA_ADDRESS, (u8 *)data,
				     sizeof(u32), HIF_RD_SYNC_BYTE_INC);
	if (status) {
		ath6kl_err("failed to read from window data addr\n");
		return status;
	}

	return status;
}


/*
 * Write to the ATH6KL through its diagnostic window. No cooperation from
 * the Target is required for this.
 */
static int ath6kl_write_reg_diag(struct ath6kl *ar, u32 *address, u32 *data)
{
	int status;

	/* set write data */
	status = hif_read_write_sync(ar, WINDOW_DATA_ADDRESS, (u8 *)data,
				     sizeof(u32), HIF_WR_SYNC_BYTE_INC);
	if (status) {
		ath6kl_err("failed to write 0x%x to window data addr\n", *data);
		return status;
	}

	/* set window register, which starts the write cycle */
	return ath6kl_set_addrwin_reg(ar, WINDOW_WRITE_ADDR_ADDRESS,
				      *address);
}

int ath6kl_access_datadiag(struct ath6kl *ar, u32 address,
			   u8 *data, u32 length, bool read)
{
	u32 count;
	int status = 0;

	for (count = 0; count < length; count += 4, address += 4) {
		if (read) {
			status = ath6kl_read_reg_diag(ar, &address,
						      (u32 *) &data[count]);
			if (status)
				break;
		} else {
			status = ath6kl_write_reg_diag(ar, &address,
						       (u32 *) &data[count]);
			if (status)
				break;
		}
	}

	return status;
}

static void ath6kl_reset_device(struct ath6kl *ar, u32 target_type,
				bool wait_fot_compltn, bool cold_reset)
{
	int status = 0;
	u32 address;
	u32 data;

	if (target_type != TARGET_TYPE_AR6003)
		return;

	data = cold_reset ? RESET_CONTROL_COLD_RST : RESET_CONTROL_MBOX_RST;

	address = RTC_BASE_ADDRESS;
	status = ath6kl_write_reg_diag(ar, &address, &data);

	if (status)
		ath6kl_err("failed to reset target\n");
}

void ath6kl_stop_endpoint(struct net_device *dev, bool keep_profile,
			  bool get_dbglogs)
{
	struct ath6kl *ar = ath6kl_priv(dev);
	static u8 bcast_mac[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	bool discon_issued;

	netif_stop_queue(dev);

	/* disable the target and the interrupts associated with it */
	if (test_bit(WMI_READY, &ar->flag)) {
		discon_issued = (test_bit(CONNECTED, &ar->flag) ||
				 test_bit(CONNECT_PEND, &ar->flag));
		ath6kl_disconnect(ar);
		if (!keep_profile)
			ath6kl_init_profile_info(ar);

		del_timer(&ar->disconnect_timer);

		clear_bit(WMI_READY, &ar->flag);
		ath6kl_wmi_shutdown(ar->wmi);
		clear_bit(WMI_ENABLED, &ar->flag);
		ar->wmi = NULL;

		/*
		 * After wmi_shudown all WMI events will be dropped. We
		 * need to cleanup the buffers allocated in AP mode and
		 * give disconnect notification to stack, which usually
		 * happens in the disconnect_event. Simulate the disconnect
		 * event by calling the function directly. Sometimes
		 * disconnect_event will be received when the debug logs
		 * are collected.
		 */
		if (discon_issued)
			ath6kl_disconnect_event(ar, DISCONNECT_CMD,
						(ar->nw_type & AP_NETWORK) ?
						bcast_mac : ar->bssid,
						0, NULL, 0);

		ar->user_key_ctrl = 0;

	} else {
		ath6kl_dbg(ATH6KL_DBG_TRC,
			   "%s: wmi is not ready 0x%p 0x%p\n",
			   __func__, ar, ar->wmi);

		/* Shut down WMI if we have started it */
		if (test_bit(WMI_ENABLED, &ar->flag)) {
			ath6kl_dbg(ATH6KL_DBG_TRC,
				   "%s: shut down wmi\n", __func__);
			ath6kl_wmi_shutdown(ar->wmi);
			clear_bit(WMI_ENABLED, &ar->flag);
			ar->wmi = NULL;
		}
	}

	if (ar->htc_target) {
		ath6kl_dbg(ATH6KL_DBG_TRC, "%s: shut down htc\n", __func__);
		htc_stop(ar->htc_target);
	}

	/*
	 * Try to reset the device if we can. The driver may have been
	 * configure NOT to reset the target during a debug session.
	 */
	ath6kl_dbg(ATH6KL_DBG_TRC,
		   "attempting to reset target on instance destroy\n");
	ath6kl_reset_device(ar, ar->target_type, true, true);
}

static void ath6kl_install_static_wep_keys(struct ath6kl *ar)
{
	u8 index;
	u8 keyusage;

	for (index = WMI_MIN_KEY_INDEX; index <= WMI_MAX_KEY_INDEX; index++) {
		if (ar->wep_key_list[index].key_len) {
			keyusage = GROUP_USAGE;
			if (index == ar->def_txkey_index)
				keyusage |= TX_USAGE;

			ath6kl_wmi_addkey_cmd(ar->wmi,
					      index,
					      WEP_CRYPT,
					      keyusage,
					      ar->wep_key_list[index].key_len,
					      NULL,
					      ar->wep_key_list[index].key,
					      KEY_OP_INIT_VAL, NULL,
					      NO_SYNC_WMIFLAG);
		}
	}
}

static void ath6kl_connect_ap_mode(struct ath6kl *ar, u16 channel, u8 *bssid,
				   u16 listen_int, u16 beacon_int,
				   u8 assoc_resp_len, u8 *assoc_info)
{
	struct net_device *dev = ar->net_dev;
	struct station_info sinfo;
	struct ath6kl_req_key *ik;
	enum crypto_type keyType = NONE_CRYPT;

	if (memcmp(dev->dev_addr, bssid, ETH_ALEN) == 0) {
		ik = &ar->ap_mode_bkey;

		switch (ar->auth_mode) {
		case NONE_AUTH:
			if (ar->prwise_crypto == WEP_CRYPT)
				ath6kl_install_static_wep_keys(ar);
			break;
		case WPA_PSK_AUTH:
		case WPA2_PSK_AUTH:
		case (WPA_PSK_AUTH|WPA2_PSK_AUTH):
			switch (ik->ik_type) {
			case ATH6KL_CIPHER_TKIP:
				keyType = TKIP_CRYPT;
				break;
			case ATH6KL_CIPHER_AES_CCM:
				keyType = AES_CRYPT;
				break;
			default:
				goto skip_key;
			}
			ath6kl_wmi_addkey_cmd(ar->wmi, ik->ik_keyix, keyType,
					      GROUP_USAGE, ik->ik_keylen,
					      (u8 *)&ik->ik_keyrsc,
					      ik->ik_keydata,
					      KEY_OP_INIT_VAL, ik->ik_macaddr,
					      SYNC_BOTH_WMIFLAG);
			break;
		}
skip_key:
		set_bit(CONNECTED, &ar->flag);
		return;
	}

	ath6kl_dbg(ATH6KL_DBG_TRC, "new station %pM aid=%d\n",
		   bssid, channel);

	ath6kl_add_new_sta(ar, bssid, channel, assoc_info, assoc_resp_len,
			   listen_int & 0xFF, beacon_int,
			   (listen_int >> 8) & 0xFF);

	/* send event to application */
	memset(&sinfo, 0, sizeof(sinfo));

	/* TODO: sinfo.generation */
	/* TODO: need to deliver (Re)AssocReq IEs somehow.. change in
	 * cfg80211 needed, e.g., by adding those into sinfo
	 */
	cfg80211_new_sta(ar->net_dev, bssid, &sinfo, GFP_KERNEL);

	netif_wake_queue(ar->net_dev);

	return;
}

/* Functions for Tx credit handling */
void ath6k_credit_init(struct htc_credit_state_info *cred_info,
		       struct list_head *ep_list,
		       int tot_credits)
{
	struct htc_endpoint_credit_dist *cur_ep_dist;
	int count;

	cred_info->cur_free_credits = tot_credits;
	cred_info->total_avail_credits = tot_credits;

	list_for_each_entry(cur_ep_dist, ep_list, list) {
		if (cur_ep_dist->endpoint == ENDPOINT_0)
			continue;

		cur_ep_dist->cred_min = cur_ep_dist->cred_per_msg;

		if (tot_credits > 4)
			if ((cur_ep_dist->svc_id == WMI_DATA_BK_SVC) ||
			    (cur_ep_dist->svc_id == WMI_DATA_BE_SVC)) {
				ath6kl_deposit_credit_to_ep(cred_info,
						cur_ep_dist,
						cur_ep_dist->cred_min);
				cur_ep_dist->dist_flags |= HTC_EP_ACTIVE;
			}

		if (cur_ep_dist->svc_id == WMI_CONTROL_SVC) {
			ath6kl_deposit_credit_to_ep(cred_info, cur_ep_dist,
						    cur_ep_dist->cred_min);
			/*
			 * Control service is always marked active, it
			 * never goes inactive EVER.
			 */
			cur_ep_dist->dist_flags |= HTC_EP_ACTIVE;
		} else if (cur_ep_dist->svc_id == WMI_DATA_BK_SVC)
			/* this is the lowest priority data endpoint */
			cred_info->lowestpri_ep_dist = cur_ep_dist->list;

		/*
		 * Streams have to be created (explicit | implicit) for all
		 * kinds of traffic. BE endpoints are also inactive in the
		 * beginning. When BE traffic starts it creates implicit
		 * streams that redistributes credits.
		 *
		 * Note: all other endpoints have minimums set but are
		 * initially given NO credits. credits will be distributed
		 * as traffic activity demands
		 */
	}

	WARN_ON(cred_info->cur_free_credits <= 0);

	list_for_each_entry(cur_ep_dist, ep_list, list) {
		if (cur_ep_dist->endpoint == ENDPOINT_0)
			continue;

		if (cur_ep_dist->svc_id == WMI_CONTROL_SVC)
			cur_ep_dist->cred_norm = cur_ep_dist->cred_per_msg;
		else {
			/*
			 * For the remaining data endpoints, we assume that
			 * each cred_per_msg are the same. We use a simple
			 * calculation here, we take the remaining credits
			 * and determine how many max messages this can
			 * cover and then set each endpoint's normal value
			 * equal to 3/4 this amount.
			 */
			count = (cred_info->cur_free_credits /
				 cur_ep_dist->cred_per_msg)
				* cur_ep_dist->cred_per_msg;
			count = (count * 3) >> 2;
			count = max(count, cur_ep_dist->cred_per_msg);
			cur_ep_dist->cred_norm = count;

		}
	}
}

/* initialize and setup credit distribution */
int ath6k_setup_credit_dist(void *htc_handle,
			    struct htc_credit_state_info *cred_info)
{
	u16 servicepriority[5];

	memset(cred_info, 0, sizeof(struct htc_credit_state_info));

	servicepriority[0] = WMI_CONTROL_SVC;  /* highest */
	servicepriority[1] = WMI_DATA_VO_SVC;
	servicepriority[2] = WMI_DATA_VI_SVC;
	servicepriority[3] = WMI_DATA_BE_SVC;
	servicepriority[4] = WMI_DATA_BK_SVC; /* lowest */

	/* set priority list */
	htc_set_credit_dist(htc_handle, cred_info, servicepriority, 5);

	return 0;
}

/* reduce an ep's credits back to a set limit */
static void ath6k_reduce_credits(struct htc_credit_state_info *cred_info,
				 struct htc_endpoint_credit_dist  *ep_dist,
				 int limit)
{
	int credits;

	ep_dist->cred_assngd = limit;

	if (ep_dist->credits <= limit)
		return;

	credits = ep_dist->credits - limit;
	ep_dist->credits -= credits;
	cred_info->cur_free_credits += credits;
}

static void ath6k_credit_update(struct htc_credit_state_info *cred_info,
				struct list_head *epdist_list)
{
	struct htc_endpoint_credit_dist *cur_dist_list;

	list_for_each_entry(cur_dist_list, epdist_list, list) {
		if (cur_dist_list->endpoint == ENDPOINT_0)
			continue;

		if (cur_dist_list->cred_to_dist > 0) {
			cur_dist_list->credits +=
					cur_dist_list->cred_to_dist;
			cur_dist_list->cred_to_dist = 0;
			if (cur_dist_list->credits >
			    cur_dist_list->cred_assngd)
				ath6k_reduce_credits(cred_info,
						cur_dist_list,
						cur_dist_list->cred_assngd);

			if (cur_dist_list->credits >
			    cur_dist_list->cred_norm)
				ath6k_reduce_credits(cred_info, cur_dist_list,
						     cur_dist_list->cred_norm);

			if (!(cur_dist_list->dist_flags & HTC_EP_ACTIVE)) {
				if (cur_dist_list->txq_depth == 0)
					ath6k_reduce_credits(cred_info,
							     cur_dist_list, 0);
			}
		}
	}
}

/*
 * HTC has an endpoint that needs credits, ep_dist is the endpoint in
 * question.
 */
void ath6k_seek_credits(struct htc_credit_state_info *cred_info,
			struct htc_endpoint_credit_dist *ep_dist)
{
	struct htc_endpoint_credit_dist *curdist_list;
	int credits = 0;
	int need;

	if (ep_dist->svc_id == WMI_CONTROL_SVC)
		goto out;

	if ((ep_dist->svc_id == WMI_DATA_VI_SVC) ||
	    (ep_dist->svc_id == WMI_DATA_VO_SVC))
		if ((ep_dist->cred_assngd >= ep_dist->cred_norm))
			goto out;

	/*
	 * For all other services, we follow a simple algorithm of:
	 *
	 * 1. checking the free pool for credits
	 * 2. checking lower priority endpoints for credits to take
	 */

	credits = min(cred_info->cur_free_credits, ep_dist->seek_cred);

	if (credits >= ep_dist->seek_cred)
		goto out;

	/*
	 * We don't have enough in the free pool, try taking away from
	 * lower priority services The rule for taking away credits:
	 *
	 *   1. Only take from lower priority endpoints
	 *   2. Only take what is allocated above the minimum (never
	 *      starve an endpoint completely)
	 *   3. Only take what you need.
	 */

	list_for_each_entry_reverse(curdist_list,
				    &cred_info->lowestpri_ep_dist,
				    list) {
		if (curdist_list == ep_dist)
			break;

		need = ep_dist->seek_cred - cred_info->cur_free_credits;

		if ((curdist_list->cred_assngd - need) >=
		     curdist_list->cred_min) {
			/*
			 * The current one has been allocated more than
			 * it's minimum and it has enough credits assigned
			 * above it's minimum to fulfill our need try to
			 * take away just enough to fulfill our need.
			 */
			ath6k_reduce_credits(cred_info, curdist_list,
					curdist_list->cred_assngd - need);

			if (cred_info->cur_free_credits >=
			    ep_dist->seek_cred)
				break;
		}

		if (curdist_list->endpoint == ENDPOINT_0)
			break;
	}

	credits = min(cred_info->cur_free_credits, ep_dist->seek_cred);

out:
	/* did we find some credits? */
	if (credits)
		ath6kl_deposit_credit_to_ep(cred_info, ep_dist, credits);

	ep_dist->seek_cred = 0;
}

/* redistribute credits based on activity change */
static void ath6k_redistribute_credits(struct htc_credit_state_info *info,
				       struct list_head *ep_dist_list)
{
	struct htc_endpoint_credit_dist *curdist_list;

	list_for_each_entry(curdist_list, ep_dist_list, list) {
		if (curdist_list->endpoint == ENDPOINT_0)
			continue;

		if ((curdist_list->svc_id == WMI_DATA_BK_SVC)  ||
		    (curdist_list->svc_id == WMI_DATA_BE_SVC))
			curdist_list->dist_flags |= HTC_EP_ACTIVE;

		if ((curdist_list->svc_id != WMI_CONTROL_SVC) &&
		    !(curdist_list->dist_flags & HTC_EP_ACTIVE)) {
			if (curdist_list->txq_depth == 0)
				ath6k_reduce_credits(info,
						curdist_list, 0);
			else
				ath6k_reduce_credits(info,
						curdist_list,
						curdist_list->cred_min);
		}
	}
}

/*
 *
 * This function is invoked whenever endpoints require credit
 * distributions. A lock is held while this function is invoked, this
 * function shall NOT block. The ep_dist_list is a list of distribution
 * structures in prioritized order as defined by the call to the
 * htc_set_credit_dist() api.
 */
void ath6k_credit_distribute(struct htc_credit_state_info *cred_info,
			     struct list_head *ep_dist_list,
			     enum htc_credit_dist_reason reason)
{
	switch (reason) {
	case HTC_CREDIT_DIST_SEND_COMPLETE:
		ath6k_credit_update(cred_info, ep_dist_list);
		break;
	case HTC_CREDIT_DIST_ACTIVITY_CHANGE:
		ath6k_redistribute_credits(cred_info, ep_dist_list);
		break;
	default:
		break;
	}

	WARN_ON(cred_info->cur_free_credits > cred_info->total_avail_credits);
	WARN_ON(cred_info->cur_free_credits < 0);
}

void disconnect_timer_handler(unsigned long ptr)
{
	struct net_device *dev = (struct net_device *)ptr;
	struct ath6kl *ar = ath6kl_priv(dev);

	ath6kl_init_profile_info(ar);
	ath6kl_disconnect(ar);
}

void ath6kl_disconnect(struct ath6kl *ar)
{
	if (test_bit(CONNECTED, &ar->flag) ||
	    test_bit(CONNECT_PEND, &ar->flag)) {
		ath6kl_wmi_disconnect_cmd(ar->wmi);
		/*
		 * Disconnect command is issued, clear the connect pending
		 * flag. The connected flag will be cleared in
		 * disconnect event notification.
		 */
		clear_bit(CONNECT_PEND, &ar->flag);
	}
}

/* WMI Event handlers */

static const char *get_hw_id_string(u32 id)
{
	switch (id) {
	case AR6003_REV1_VERSION:
		return "1.0";
	case AR6003_REV2_VERSION:
		return "2.0";
	case AR6003_REV3_VERSION:
		return "2.1.1";
	default:
		return "unknown";
	}
}

void ath6kl_ready_event(void *devt, u8 *datap, u32 sw_ver, u32 abi_ver)
{
	struct ath6kl *ar = devt;
	struct net_device *dev = ar->net_dev;

	memcpy(dev->dev_addr, datap, ETH_ALEN);
	ath6kl_dbg(ATH6KL_DBG_TRC, "%s: mac addr = %pM\n",
		   __func__, dev->dev_addr);

	ar->version.wlan_ver = sw_ver;
	ar->version.abi_ver = abi_ver;

	snprintf(ar->wdev->wiphy->fw_version,
		 sizeof(ar->wdev->wiphy->fw_version),
		 "%u.%u.%u.%u",
		 (ar->version.wlan_ver & 0xf0000000) >> 28,
		 (ar->version.wlan_ver & 0x0f000000) >> 24,
		 (ar->version.wlan_ver & 0x00ff0000) >> 16,
		 (ar->version.wlan_ver & 0x0000ffff));

	/* indicate to the waiting thread that the ready event was received */
	set_bit(WMI_READY, &ar->flag);
	wake_up(&ar->event_wq);

	ath6kl_info("hw %s fw %s\n",
		    get_hw_id_string(ar->wdev->wiphy->hw_version),
		    ar->wdev->wiphy->fw_version);
}

void ath6kl_scan_complete_evt(struct ath6kl *ar, int status)
{
	ath6kl_cfg80211_scan_complete_event(ar, status);

	if (!ar->usr_bss_filter)
		ath6kl_wmi_bssfilter_cmd(ar->wmi, NONE_BSS_FILTER, 0);

	ath6kl_dbg(ATH6KL_DBG_WLAN_SCAN, "scan complete: %d\n", status);
}

void ath6kl_connect_event(struct ath6kl *ar, u16 channel, u8 *bssid,
			  u16 listen_int, u16 beacon_int,
			  enum network_type net_type, u8 beacon_ie_len,
			  u8 assoc_req_len, u8 assoc_resp_len,
			  u8 *assoc_info)
{
	unsigned long flags;

	if (ar->nw_type == AP_NETWORK) {
		ath6kl_connect_ap_mode(ar, channel, bssid, listen_int,
				       beacon_int, assoc_resp_len,
				       assoc_info);
		return;
	}

	ath6kl_cfg80211_connect_event(ar, channel, bssid,
				      listen_int, beacon_int,
				      net_type, beacon_ie_len,
				      assoc_req_len, assoc_resp_len,
				      assoc_info);

	memcpy(ar->bssid, bssid, sizeof(ar->bssid));
	ar->bss_ch = channel;

	if ((ar->nw_type == INFRA_NETWORK))
		ath6kl_wmi_listeninterval_cmd(ar->wmi, ar->listen_intvl_t,
					      ar->listen_intvl_b);

	netif_wake_queue(ar->net_dev);

	/* Update connect & link status atomically */
	spin_lock_irqsave(&ar->lock, flags);
	set_bit(CONNECTED, &ar->flag);
	clear_bit(CONNECT_PEND, &ar->flag);
	netif_carrier_on(ar->net_dev);
	spin_unlock_irqrestore(&ar->lock, flags);

	aggr_reset_state(ar->aggr_cntxt);
	ar->reconnect_flag = 0;

	if ((ar->nw_type == ADHOC_NETWORK) && ar->ibss_ps_enable) {
		memset(ar->node_map, 0, sizeof(ar->node_map));
		ar->node_num = 0;
		ar->next_ep_id = ENDPOINT_2;
	}

	if (!ar->usr_bss_filter)
		ath6kl_wmi_bssfilter_cmd(ar->wmi, NONE_BSS_FILTER, 0);
}

void ath6kl_tkip_micerr_event(struct ath6kl *ar, u8 keyid, bool ismcast)
{
	struct ath6kl_sta *sta;
	u8 tsc[6];
	/*
	 * For AP case, keyid will have aid of STA which sent pkt with
	 * MIC error. Use this aid to get MAC & send it to hostapd.
	 */
	if (ar->nw_type == AP_NETWORK) {
		sta = ath6kl_find_sta_by_aid(ar, (keyid >> 2));
		if (!sta)
			return;

		ath6kl_dbg(ATH6KL_DBG_TRC,
			   "ap tkip mic error received from aid=%d\n", keyid);

		memset(tsc, 0, sizeof(tsc)); /* FIX: get correct TSC */
		cfg80211_michael_mic_failure(ar->net_dev, sta->mac,
					     NL80211_KEYTYPE_PAIRWISE, keyid,
					     tsc, GFP_KERNEL);
	} else
		ath6kl_cfg80211_tkip_micerr_event(ar, keyid, ismcast);

}

static void ath6kl_update_target_stats(struct ath6kl *ar, u8 *ptr, u32 len)
{
	struct wmi_target_stats *tgt_stats =
		(struct wmi_target_stats *) ptr;
	struct target_stats *stats = &ar->target_stats;
	struct tkip_ccmp_stats *ccmp_stats;
	struct bss *conn_bss = NULL;
	struct cserv_stats *c_stats;
	u8 ac;

	if (len < sizeof(*tgt_stats))
		return;

	/* update the RSSI of the connected bss */
	if (test_bit(CONNECTED, &ar->flag)) {
		conn_bss = ath6kl_wmi_find_node(ar->wmi, ar->bssid);
		if (conn_bss) {
			c_stats = &tgt_stats->cserv_stats;
			conn_bss->ni_rssi =
				a_sle16_to_cpu(c_stats->cs_ave_beacon_rssi);
			conn_bss->ni_snr =
				tgt_stats->cserv_stats.cs_ave_beacon_snr;
			ath6kl_wmi_node_return(ar->wmi, conn_bss);
		}
	}

	ath6kl_dbg(ATH6KL_DBG_TRC, "updating target stats\n");

	stats->tx_pkt += le32_to_cpu(tgt_stats->stats.tx.pkt);
	stats->tx_byte += le32_to_cpu(tgt_stats->stats.tx.byte);
	stats->tx_ucast_pkt += le32_to_cpu(tgt_stats->stats.tx.ucast_pkt);
	stats->tx_ucast_byte += le32_to_cpu(tgt_stats->stats.tx.ucast_byte);
	stats->tx_mcast_pkt += le32_to_cpu(tgt_stats->stats.tx.mcast_pkt);
	stats->tx_mcast_byte += le32_to_cpu(tgt_stats->stats.tx.mcast_byte);
	stats->tx_bcast_pkt  += le32_to_cpu(tgt_stats->stats.tx.bcast_pkt);
	stats->tx_bcast_byte += le32_to_cpu(tgt_stats->stats.tx.bcast_byte);
	stats->tx_rts_success_cnt +=
		le32_to_cpu(tgt_stats->stats.tx.rts_success_cnt);

	for (ac = 0; ac < WMM_NUM_AC; ac++)
		stats->tx_pkt_per_ac[ac] +=
			le32_to_cpu(tgt_stats->stats.tx.pkt_per_ac[ac]);

	stats->tx_err += le32_to_cpu(tgt_stats->stats.tx.err);
	stats->tx_fail_cnt += le32_to_cpu(tgt_stats->stats.tx.fail_cnt);
	stats->tx_retry_cnt += le32_to_cpu(tgt_stats->stats.tx.retry_cnt);
	stats->tx_mult_retry_cnt +=
		le32_to_cpu(tgt_stats->stats.tx.mult_retry_cnt);
	stats->tx_rts_fail_cnt +=
		le32_to_cpu(tgt_stats->stats.tx.rts_fail_cnt);
	stats->tx_ucast_rate =
	    ath6kl_wmi_get_rate(a_sle32_to_cpu(tgt_stats->stats.tx.ucast_rate));

	stats->rx_pkt += le32_to_cpu(tgt_stats->stats.rx.pkt);
	stats->rx_byte += le32_to_cpu(tgt_stats->stats.rx.byte);
	stats->rx_ucast_pkt += le32_to_cpu(tgt_stats->stats.rx.ucast_pkt);
	stats->rx_ucast_byte += le32_to_cpu(tgt_stats->stats.rx.ucast_byte);
	stats->rx_mcast_pkt += le32_to_cpu(tgt_stats->stats.rx.mcast_pkt);
	stats->rx_mcast_byte += le32_to_cpu(tgt_stats->stats.rx.mcast_byte);
	stats->rx_bcast_pkt += le32_to_cpu(tgt_stats->stats.rx.bcast_pkt);
	stats->rx_bcast_byte += le32_to_cpu(tgt_stats->stats.rx.bcast_byte);
	stats->rx_frgment_pkt += le32_to_cpu(tgt_stats->stats.rx.frgment_pkt);
	stats->rx_err += le32_to_cpu(tgt_stats->stats.rx.err);
	stats->rx_crc_err += le32_to_cpu(tgt_stats->stats.rx.crc_err);
	stats->rx_key_cache_miss +=
		le32_to_cpu(tgt_stats->stats.rx.key_cache_miss);
	stats->rx_decrypt_err += le32_to_cpu(tgt_stats->stats.rx.decrypt_err);
	stats->rx_dupl_frame += le32_to_cpu(tgt_stats->stats.rx.dupl_frame);
	stats->rx_ucast_rate =
	    ath6kl_wmi_get_rate(a_sle32_to_cpu(tgt_stats->stats.rx.ucast_rate));

	ccmp_stats = &tgt_stats->stats.tkip_ccmp_stats;

	stats->tkip_local_mic_fail +=
		le32_to_cpu(ccmp_stats->tkip_local_mic_fail);
	stats->tkip_cnter_measures_invoked +=
		le32_to_cpu(ccmp_stats->tkip_cnter_measures_invoked);
	stats->tkip_fmt_err += le32_to_cpu(ccmp_stats->tkip_fmt_err);

	stats->ccmp_fmt_err += le32_to_cpu(ccmp_stats->ccmp_fmt_err);
	stats->ccmp_replays += le32_to_cpu(ccmp_stats->ccmp_replays);

	stats->pwr_save_fail_cnt +=
		le32_to_cpu(tgt_stats->pm_stats.pwr_save_failure_cnt);
	stats->noise_floor_calib =
		a_sle32_to_cpu(tgt_stats->noise_floor_calib);

	stats->cs_bmiss_cnt +=
		le32_to_cpu(tgt_stats->cserv_stats.cs_bmiss_cnt);
	stats->cs_low_rssi_cnt +=
		le32_to_cpu(tgt_stats->cserv_stats.cs_low_rssi_cnt);
	stats->cs_connect_cnt +=
		le16_to_cpu(tgt_stats->cserv_stats.cs_connect_cnt);
	stats->cs_discon_cnt +=
		le16_to_cpu(tgt_stats->cserv_stats.cs_discon_cnt);

	stats->cs_ave_beacon_rssi =
		a_sle16_to_cpu(tgt_stats->cserv_stats.cs_ave_beacon_rssi);

	stats->cs_last_roam_msec =
		tgt_stats->cserv_stats.cs_last_roam_msec;
	stats->cs_snr = tgt_stats->cserv_stats.cs_snr;
	stats->cs_rssi = a_sle16_to_cpu(tgt_stats->cserv_stats.cs_rssi);

	stats->lq_val = le32_to_cpu(tgt_stats->lq_val);

	stats->wow_pkt_dropped +=
		le32_to_cpu(tgt_stats->wow_stats.wow_pkt_dropped);
	stats->wow_host_pkt_wakeups +=
		tgt_stats->wow_stats.wow_host_pkt_wakeups;
	stats->wow_host_evt_wakeups +=
		tgt_stats->wow_stats.wow_host_evt_wakeups;
	stats->wow_evt_discarded +=
		le16_to_cpu(tgt_stats->wow_stats.wow_evt_discarded);

	if (test_bit(STATS_UPDATE_PEND, &ar->flag)) {
		clear_bit(STATS_UPDATE_PEND, &ar->flag);
		wake_up(&ar->event_wq);
	}
}

static void ath6kl_add_le32(__le32 *var, __le32 val)
{
	*var = cpu_to_le32(le32_to_cpu(*var) + le32_to_cpu(val));
}

void ath6kl_tgt_stats_event(struct ath6kl *ar, u8 *ptr, u32 len)
{
	struct wmi_ap_mode_stat *p = (struct wmi_ap_mode_stat *) ptr;
	struct wmi_ap_mode_stat *ap = &ar->ap_stats;
	struct wmi_per_sta_stat *st_ap, *st_p;
	u8 ac;

	if (ar->nw_type == AP_NETWORK) {
		if (len < sizeof(*p))
			return;

		for (ac = 0; ac < AP_MAX_NUM_STA; ac++) {
			st_ap = &ap->sta[ac];
			st_p = &p->sta[ac];

			ath6kl_add_le32(&st_ap->tx_bytes, st_p->tx_bytes);
			ath6kl_add_le32(&st_ap->tx_pkts, st_p->tx_pkts);
			ath6kl_add_le32(&st_ap->tx_error, st_p->tx_error);
			ath6kl_add_le32(&st_ap->tx_discard, st_p->tx_discard);
			ath6kl_add_le32(&st_ap->rx_bytes, st_p->rx_bytes);
			ath6kl_add_le32(&st_ap->rx_pkts, st_p->rx_pkts);
			ath6kl_add_le32(&st_ap->rx_error, st_p->rx_error);
			ath6kl_add_le32(&st_ap->rx_discard, st_p->rx_discard);
		}

	} else {
		ath6kl_update_target_stats(ar, ptr, len);
	}
}

void ath6kl_wakeup_event(void *dev)
{
	struct ath6kl *ar = (struct ath6kl *) dev;

	wake_up(&ar->event_wq);
}

void ath6kl_txpwr_rx_evt(void *devt, u8 tx_pwr)
{
	struct ath6kl *ar = (struct ath6kl *) devt;

	ar->tx_pwr = tx_pwr;
	wake_up(&ar->event_wq);
}

void ath6kl_pspoll_event(struct ath6kl *ar, u8 aid)
{
	struct ath6kl_sta *conn;
	struct sk_buff *skb;
	bool psq_empty = false;

	conn = ath6kl_find_sta_by_aid(ar, aid);

	if (!conn)
		return;
	/*
	 * Send out a packet queued on ps queue. When the ps queue
	 * becomes empty update the PVB for this station.
	 */
	spin_lock_bh(&conn->psq_lock);
	psq_empty  = skb_queue_empty(&conn->psq);
	spin_unlock_bh(&conn->psq_lock);

	if (psq_empty)
		/* TODO: Send out a NULL data frame */
		return;

	spin_lock_bh(&conn->psq_lock);
	skb = skb_dequeue(&conn->psq);
	spin_unlock_bh(&conn->psq_lock);

	conn->sta_flags |= STA_PS_POLLED;
	ath6kl_data_tx(skb, ar->net_dev);
	conn->sta_flags &= ~STA_PS_POLLED;

	spin_lock_bh(&conn->psq_lock);
	psq_empty  = skb_queue_empty(&conn->psq);
	spin_unlock_bh(&conn->psq_lock);

	if (psq_empty)
		ath6kl_wmi_set_pvb_cmd(ar->wmi, conn->aid, 0);
}

void ath6kl_dtimexpiry_event(struct ath6kl *ar)
{
	bool mcastq_empty = false;
	struct sk_buff *skb;

	/*
	 * If there are no associated STAs, ignore the DTIM expiry event.
	 * There can be potential race conditions where the last associated
	 * STA may disconnect & before the host could clear the 'Indicate
	 * DTIM' request to the firmware, the firmware would have just
	 * indicated a DTIM expiry event. The race is between 'clear DTIM
	 * expiry cmd' going from the host to the firmware & the DTIM
	 * expiry event happening from the firmware to the host.
	 */
	if (!ar->sta_list_index)
		return;

	spin_lock_bh(&ar->mcastpsq_lock);
	mcastq_empty = skb_queue_empty(&ar->mcastpsq);
	spin_unlock_bh(&ar->mcastpsq_lock);

	if (mcastq_empty)
		return;

	/* set the STA flag to dtim_expired for the frame to go out */
	set_bit(DTIM_EXPIRED, &ar->flag);

	spin_lock_bh(&ar->mcastpsq_lock);
	while ((skb = skb_dequeue(&ar->mcastpsq)) != NULL) {
		spin_unlock_bh(&ar->mcastpsq_lock);

		ath6kl_data_tx(skb, ar->net_dev);

		spin_lock_bh(&ar->mcastpsq_lock);
	}
	spin_unlock_bh(&ar->mcastpsq_lock);

	clear_bit(DTIM_EXPIRED, &ar->flag);

	/* clear the LSB of the BitMapCtl field of the TIM IE */
	ath6kl_wmi_set_pvb_cmd(ar->wmi, MCAST_AID, 0);
}

void ath6kl_disconnect_event(struct ath6kl *ar, u8 reason, u8 *bssid,
			     u8 assoc_resp_len, u8 *assoc_info,
			     u16 prot_reason_status)
{
	struct bss *wmi_ssid_node = NULL;
	unsigned long flags;

	if (ar->nw_type == AP_NETWORK) {
		if (!ath6kl_remove_sta(ar, bssid, prot_reason_status))
			return;

		/* if no more associated STAs, empty the mcast PS q */
		if (ar->sta_list_index == 0) {
			spin_lock_bh(&ar->mcastpsq_lock);
			skb_queue_purge(&ar->mcastpsq);
			spin_unlock_bh(&ar->mcastpsq_lock);

			/* clear the LSB of the TIM IE's BitMapCtl field */
			if (test_bit(WMI_READY, &ar->flag))
				ath6kl_wmi_set_pvb_cmd(ar->wmi, MCAST_AID, 0);
		}

		if (!is_broadcast_ether_addr(bssid)) {
			/* send event to application */
			cfg80211_del_sta(ar->net_dev, bssid, GFP_KERNEL);
		}

		clear_bit(CONNECTED, &ar->flag);
		return;
	}

	ath6kl_cfg80211_disconnect_event(ar, reason, bssid,
				       assoc_resp_len, assoc_info,
				       prot_reason_status);

	aggr_reset_state(ar->aggr_cntxt);

	del_timer(&ar->disconnect_timer);

	ath6kl_dbg(ATH6KL_DBG_WLAN_CONNECT,
		   "disconnect reason is %d\n", reason);

	/*
	 * If the event is due to disconnect cmd from the host, only they
	 * the target would stop trying to connect. Under any other
	 * condition, target would keep trying to connect.
	 */
	if (reason == DISCONNECT_CMD) {
		if (!ar->usr_bss_filter && test_bit(WMI_READY, &ar->flag))
			ath6kl_wmi_bssfilter_cmd(ar->wmi, NONE_BSS_FILTER, 0);
	} else {
		set_bit(CONNECT_PEND, &ar->flag);
		if (((reason == ASSOC_FAILED) &&
		    (prot_reason_status == 0x11)) ||
		    ((reason == ASSOC_FAILED) && (prot_reason_status == 0x0)
		     && (ar->reconnect_flag == 1))) {
			set_bit(CONNECTED, &ar->flag);
			return;
		}
	}

	if ((reason == NO_NETWORK_AVAIL) && test_bit(WMI_READY, &ar->flag))  {
		ath6kl_wmi_node_free(ar->wmi, bssid);

		/*
		 * In case any other same SSID nodes are present remove it,
		 * since those nodes also not available now.
		 */
		do {
			/*
			 * Find the nodes based on SSID and remove it
			 *
			 * Note: This case will not work out for
			 * Hidden-SSID
			 */
			wmi_ssid_node = ath6kl_wmi_find_ssid_node(ar->wmi,
								  ar->ssid,
								  ar->ssid_len,
								  false,
								  true);

			if (wmi_ssid_node)
				ath6kl_wmi_node_free(ar->wmi,
						     wmi_ssid_node->ni_macaddr);

		} while (wmi_ssid_node);
	}

	/* update connect & link status atomically */
	spin_lock_irqsave(&ar->lock, flags);
	clear_bit(CONNECTED, &ar->flag);
	netif_carrier_off(ar->net_dev);
	spin_unlock_irqrestore(&ar->lock, flags);

	if ((reason != CSERV_DISCONNECT) || (ar->reconnect_flag != 1))
		ar->reconnect_flag = 0;

	if (reason != CSERV_DISCONNECT)
		ar->user_key_ctrl = 0;

	netif_stop_queue(ar->net_dev);
	memset(ar->bssid, 0, sizeof(ar->bssid));
	ar->bss_ch = 0;

	ath6kl_tx_data_cleanup(ar);
}

static int ath6kl_open(struct net_device *dev)
{
	struct ath6kl *ar = ath6kl_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&ar->lock, flags);

	ar->wlan_state = WLAN_ENABLED;

	if (test_bit(CONNECTED, &ar->flag)) {
		netif_carrier_on(dev);
		netif_wake_queue(dev);
	} else
		netif_carrier_off(dev);

	spin_unlock_irqrestore(&ar->lock, flags);

	return 0;
}

static int ath6kl_close(struct net_device *dev)
{
	struct ath6kl *ar = ath6kl_priv(dev);

	netif_stop_queue(dev);

	ath6kl_disconnect(ar);

	if (test_bit(WMI_READY, &ar->flag)) {
		if (ath6kl_wmi_scanparams_cmd(ar->wmi, 0xFFFF, 0, 0, 0, 0, 0, 0,
					      0, 0, 0))
			return -EIO;

		ar->wlan_state = WLAN_DISABLED;
	}

	ath6kl_cfg80211_scan_complete_event(ar, -ECANCELED);

	return 0;
}

static struct net_device_stats *ath6kl_get_stats(struct net_device *dev)
{
	struct ath6kl *ar = ath6kl_priv(dev);

	return &ar->net_stats;
}

static struct net_device_ops ath6kl_netdev_ops = {
	.ndo_open               = ath6kl_open,
	.ndo_stop               = ath6kl_close,
	.ndo_start_xmit         = ath6kl_data_tx,
	.ndo_get_stats          = ath6kl_get_stats,
};

void init_netdev(struct net_device *dev)
{
	dev->netdev_ops = &ath6kl_netdev_ops;
	dev->watchdog_timeo = ATH6KL_TX_TIMEOUT;

	dev->needed_headroom = ETH_HLEN;
	dev->needed_headroom += sizeof(struct ath6kl_llc_snap_hdr) +
				sizeof(struct wmi_data_hdr) + HTC_HDR_LENGTH
				+ WMI_MAX_TX_META_SZ;

	return;
}
