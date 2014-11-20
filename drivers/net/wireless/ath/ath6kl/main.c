/*
 * Copyright (c) 2004-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2012 Qualcomm Atheros, Inc.
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "core.h"
#include "hif-ops.h"
#include "cfg80211.h"
#include "target.h"
#include "debug.h"

struct ath6kl_sta *ath6kl_find_sta(struct ath6kl_vif *vif, u8 *node_addr)
{
	struct ath6kl *ar = vif->ar;
	struct ath6kl_sta *conn = NULL;
	u8 i, max_conn;

	if (is_zero_ether_addr(node_addr))
		return NULL;

	max_conn = (vif->nw_type == AP_NETWORK) ? AP_MAX_NUM_STA : 0;

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

static void ath6kl_add_new_sta(struct ath6kl_vif *vif, u8 *mac, u16 aid,
			       u8 *wpaie, size_t ielen, u8 keymgmt,
			       u8 ucipher, u8 auth, u8 apsd_info)
{
	struct ath6kl *ar = vif->ar;
	struct ath6kl_sta *sta;
	u8 free_slot;

	free_slot = aid - 1;

	sta = &ar->sta_list[free_slot];
	memcpy(sta->mac, mac, ETH_ALEN);
	if (ielen <= ATH6KL_MAX_IE)
		memcpy(sta->wpa_ie, wpaie, ielen);
	sta->aid = aid;
	sta->keymgmt = keymgmt;
	sta->ucipher = ucipher;
	sta->auth = auth;
	sta->apsd_info = apsd_info;

	ar->sta_list_index = ar->sta_list_index | (1 << free_slot);
	ar->ap_stats.sta[free_slot].aid = cpu_to_le32(aid);
	aggr_conn_init(vif, vif->aggr_cntxt, sta->aggr_conn);
}

static void ath6kl_sta_cleanup(struct ath6kl *ar, u8 i)
{
	struct ath6kl_sta *sta = &ar->sta_list[i];
	struct ath6kl_mgmt_buff *entry, *tmp;

	/* empty the queued pkts in the PS queue if any */
	spin_lock_bh(&sta->psq_lock);
	skb_queue_purge(&sta->psq);
	skb_queue_purge(&sta->apsdq);

	if (sta->mgmt_psq_len != 0) {
		list_for_each_entry_safe(entry, tmp, &sta->mgmt_psq, list) {
			kfree(entry);
		}
		INIT_LIST_HEAD(&sta->mgmt_psq);
		sta->mgmt_psq_len = 0;
	}

	spin_unlock_bh(&sta->psq_lock);

	memset(&ar->ap_stats.sta[sta->aid - 1], 0,
	       sizeof(struct wmi_per_sta_stat));
	memset(sta->mac, 0, ETH_ALEN);
	memset(sta->wpa_ie, 0, ATH6KL_MAX_IE);
	sta->aid = 0;
	sta->sta_flags = 0;

	ar->sta_list_index = ar->sta_list_index & ~(1 << i);
	aggr_reset_state(sta->aggr_conn);
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

/*
 * Read from the hardware through its diagnostic window. No cooperation
 * from the firmware is required for this.
 */
int ath6kl_diag_read32(struct ath6kl *ar, u32 address, u32 *value)
{
	int ret;

	ret = ath6kl_hif_diag_read32(ar, address, value);
	if (ret) {
		ath6kl_warn("failed to read32 through diagnose window: %d\n",
			    ret);
		return ret;
	}

	return 0;
}

/*
 * Write to the ATH6KL through its diagnostic window. No cooperation from
 * the Target is required for this.
 */
int ath6kl_diag_write32(struct ath6kl *ar, u32 address, __le32 value)
{
	int ret;

	ret = ath6kl_hif_diag_write32(ar, address, value);

	if (ret) {
		ath6kl_err("failed to write 0x%x during diagnose window to 0x%x\n",
			   address, value);
		return ret;
	}

	return 0;
}

int ath6kl_diag_read(struct ath6kl *ar, u32 address, void *data, u32 length)
{
	u32 count, *buf = data;
	int ret;

	if (WARN_ON(length % 4))
		return -EINVAL;

	for (count = 0; count < length / 4; count++, address += 4) {
		ret = ath6kl_diag_read32(ar, address, &buf[count]);
		if (ret)
			return ret;
	}

	return 0;
}

int ath6kl_diag_write(struct ath6kl *ar, u32 address, void *data, u32 length)
{
	u32 count;
	__le32 *buf = data;
	int ret;

	if (WARN_ON(length % 4))
		return -EINVAL;

	for (count = 0; count < length / 4; count++, address += 4) {
		ret = ath6kl_diag_write32(ar, address, buf[count]);
		if (ret)
			return ret;
	}

	return 0;
}

int ath6kl_read_fwlogs(struct ath6kl *ar)
{
	struct ath6kl_dbglog_hdr debug_hdr;
	struct ath6kl_dbglog_buf debug_buf;
	u32 address, length, dropped, firstbuf, debug_hdr_addr;
	int ret, loop;
	u8 *buf;

	buf = kmalloc(ATH6KL_FWLOG_PAYLOAD_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	address = TARG_VTOP(ar->target_type,
			    ath6kl_get_hi_item_addr(ar,
						    HI_ITEM(hi_dbglog_hdr)));

	ret = ath6kl_diag_read32(ar, address, &debug_hdr_addr);
	if (ret)
		goto out;

	/* Get the contents of the ring buffer */
	if (debug_hdr_addr == 0) {
		ath6kl_warn("Invalid address for debug_hdr_addr\n");
		ret = -EINVAL;
		goto out;
	}

	address = TARG_VTOP(ar->target_type, debug_hdr_addr);
	ret = ath6kl_diag_read(ar, address, &debug_hdr, sizeof(debug_hdr));
	if (ret)
		goto out;

	address = TARG_VTOP(ar->target_type,
			    le32_to_cpu(debug_hdr.dbuf_addr));
	firstbuf = address;
	dropped = le32_to_cpu(debug_hdr.dropped);
	ret = ath6kl_diag_read(ar, address, &debug_buf, sizeof(debug_buf));
	if (ret)
		goto out;

	loop = 100;

	do {
		address = TARG_VTOP(ar->target_type,
				    le32_to_cpu(debug_buf.buffer_addr));
		length = le32_to_cpu(debug_buf.length);

		if (length != 0 && (le32_to_cpu(debug_buf.length) <=
				    le32_to_cpu(debug_buf.bufsize))) {
			length = ALIGN(length, 4);

			ret = ath6kl_diag_read(ar, address,
					       buf, length);
			if (ret)
				goto out;

			ath6kl_debug_fwlog_event(ar, buf, length);
		}

		address = TARG_VTOP(ar->target_type,
				    le32_to_cpu(debug_buf.next));
		ret = ath6kl_diag_read(ar, address, &debug_buf,
				       sizeof(debug_buf));
		if (ret)
			goto out;

		loop--;

		if (WARN_ON(loop == 0)) {
			ret = -ETIMEDOUT;
			goto out;
		}
	} while (address != firstbuf);

out:
	kfree(buf);

	return ret;
}

static void ath6kl_install_static_wep_keys(struct ath6kl_vif *vif)
{
	u8 index;
	u8 keyusage;

	for (index = 0; index <= WMI_MAX_KEY_INDEX; index++) {
		if (vif->wep_key_list[index].key_len) {
			keyusage = GROUP_USAGE;
			if (index == vif->def_txkey_index)
				keyusage |= TX_USAGE;

			ath6kl_wmi_addkey_cmd(vif->ar->wmi, vif->fw_vif_idx,
					      index,
					      WEP_CRYPT,
					      keyusage,
					      vif->wep_key_list[index].key_len,
					      NULL, 0,
					      vif->wep_key_list[index].key,
					      KEY_OP_INIT_VAL, NULL,
					      NO_SYNC_WMIFLAG);
		}
	}
}

void ath6kl_connect_ap_mode_bss(struct ath6kl_vif *vif, u16 channel)
{
	struct ath6kl *ar = vif->ar;
	struct ath6kl_req_key *ik;
	int res;
	u8 key_rsc[ATH6KL_KEY_SEQ_LEN];

	ik = &ar->ap_mode_bkey;

	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG, "AP mode started on %u MHz\n", channel);

	switch (vif->auth_mode) {
	case NONE_AUTH:
		if (vif->prwise_crypto == WEP_CRYPT)
			ath6kl_install_static_wep_keys(vif);
		if (!ik->valid || ik->key_type != WAPI_CRYPT)
			break;
		/* for WAPI, we need to set the delayed group key, continue: */
	case WPA_PSK_AUTH:
	case WPA2_PSK_AUTH:
	case (WPA_PSK_AUTH | WPA2_PSK_AUTH):
		if (!ik->valid)
			break;

		ath6kl_dbg(ATH6KL_DBG_WLAN_CFG,
			   "Delayed addkey for the initial group key for AP mode\n");
		memset(key_rsc, 0, sizeof(key_rsc));
		res = ath6kl_wmi_addkey_cmd(
			ar->wmi, vif->fw_vif_idx, ik->key_index, ik->key_type,
			GROUP_USAGE, ik->key_len, key_rsc, ATH6KL_KEY_SEQ_LEN,
			ik->key,
			KEY_OP_INIT_VAL, NULL, SYNC_BOTH_WMIFLAG);
		if (res) {
			ath6kl_dbg(ATH6KL_DBG_WLAN_CFG,
				   "Delayed addkey failed: %d\n", res);
		}
		break;
	}

	if (ar->last_ch != channel)
		/* we actually don't know the phymode, default to HT20 */
		ath6kl_cfg80211_ch_switch_notify(vif, channel, WMI_11G_HT20);

	ath6kl_wmi_bssfilter_cmd(ar->wmi, vif->fw_vif_idx, NONE_BSS_FILTER, 0);
	set_bit(CONNECTED, &vif->flags);
	netif_carrier_on(vif->ndev);
}

void ath6kl_connect_ap_mode_sta(struct ath6kl_vif *vif, u16 aid, u8 *mac_addr,
				u8 keymgmt, u8 ucipher, u8 auth,
				u8 assoc_req_len, u8 *assoc_info, u8 apsd_info)
{
	u8 *ies = NULL, *wpa_ie = NULL, *pos;
	size_t ies_len = 0;
	struct station_info sinfo;

	ath6kl_dbg(ATH6KL_DBG_TRC, "new station %pM aid=%d\n", mac_addr, aid);

	if (assoc_req_len > sizeof(struct ieee80211_hdr_3addr)) {
		struct ieee80211_mgmt *mgmt =
			(struct ieee80211_mgmt *) assoc_info;
		if (ieee80211_is_assoc_req(mgmt->frame_control) &&
		    assoc_req_len >= sizeof(struct ieee80211_hdr_3addr) +
		    sizeof(mgmt->u.assoc_req)) {
			ies = mgmt->u.assoc_req.variable;
			ies_len = assoc_info + assoc_req_len - ies;
		} else if (ieee80211_is_reassoc_req(mgmt->frame_control) &&
			   assoc_req_len >= sizeof(struct ieee80211_hdr_3addr)
			   + sizeof(mgmt->u.reassoc_req)) {
			ies = mgmt->u.reassoc_req.variable;
			ies_len = assoc_info + assoc_req_len - ies;
		}
	}

	pos = ies;
	while (pos && pos + 1 < ies + ies_len) {
		if (pos + 2 + pos[1] > ies + ies_len)
			break;
		if (pos[0] == WLAN_EID_RSN)
			wpa_ie = pos; /* RSN IE */
		else if (pos[0] == WLAN_EID_VENDOR_SPECIFIC &&
			 pos[1] >= 4 &&
			 pos[2] == 0x00 && pos[3] == 0x50 && pos[4] == 0xf2) {
			if (pos[5] == 0x01)
				wpa_ie = pos; /* WPA IE */
			else if (pos[5] == 0x04) {
				wpa_ie = pos; /* WPS IE */
				break; /* overrides WPA/RSN IE */
			}
		} else if (pos[0] == 0x44 && wpa_ie == NULL) {
			/*
			 * Note: WAPI Parameter Set IE re-uses Element ID that
			 * was officially allocated for BSS AC Access Delay. As
			 * such, we need to be a bit more careful on when
			 * parsing the frame. However, BSS AC Access Delay
			 * element is not supposed to be included in
			 * (Re)Association Request frames, so this should not
			 * cause problems.
			 */
			wpa_ie = pos; /* WAPI IE */
			break;
		}
		pos += 2 + pos[1];
	}

	ath6kl_add_new_sta(vif, mac_addr, aid, wpa_ie,
			   wpa_ie ? 2 + wpa_ie[1] : 0,
			   keymgmt, ucipher, auth, apsd_info);

	/* send event to application */
	memset(&sinfo, 0, sizeof(sinfo));

	/* TODO: sinfo.generation */

	sinfo.assoc_req_ies = ies;
	sinfo.assoc_req_ies_len = ies_len;
	sinfo.filled |= STATION_INFO_ASSOC_REQ_IES;

	cfg80211_new_sta(vif->ndev, mac_addr, &sinfo, GFP_KERNEL);

	netif_wake_queue(vif->ndev);
}

void disconnect_timer_handler(unsigned long ptr)
{
	struct net_device *dev = (struct net_device *)ptr;
	struct ath6kl_vif *vif = netdev_priv(dev);

	ath6kl_init_profile_info(vif);
	ath6kl_disconnect(vif);
}

void ath6kl_disconnect(struct ath6kl_vif *vif)
{
	if (test_bit(CONNECTED, &vif->flags) ||
	    test_bit(CONNECT_PEND, &vif->flags)) {
		ath6kl_wmi_disconnect_cmd(vif->ar->wmi, vif->fw_vif_idx);
		/*
		 * Disconnect command is issued, clear the connect pending
		 * flag. The connected flag will be cleared in
		 * disconnect event notification.
		 */
		clear_bit(CONNECT_PEND, &vif->flags);
	}
}

/* WMI Event handlers */

void ath6kl_ready_event(void *devt, u8 *datap, u32 sw_ver, u32 abi_ver,
			enum wmi_phy_cap cap)
{
	struct ath6kl *ar = devt;

	memcpy(ar->mac_addr, datap, ETH_ALEN);

	ath6kl_dbg(ATH6KL_DBG_BOOT,
		   "ready event mac addr %pM sw_ver 0x%x abi_ver 0x%x cap 0x%x\n",
		   ar->mac_addr, sw_ver, abi_ver, cap);

	ar->version.wlan_ver = sw_ver;
	ar->version.abi_ver = abi_ver;
	ar->hw.cap = cap;

	if (strlen(ar->wiphy->fw_version) == 0) {
		snprintf(ar->wiphy->fw_version,
			 sizeof(ar->wiphy->fw_version),
			 "%u.%u.%u.%u",
			 (ar->version.wlan_ver & 0xf0000000) >> 28,
			 (ar->version.wlan_ver & 0x0f000000) >> 24,
			 (ar->version.wlan_ver & 0x00ff0000) >> 16,
			 (ar->version.wlan_ver & 0x0000ffff));
	}

	/* indicate to the waiting thread that the ready event was received */
	set_bit(WMI_READY, &ar->flag);
	wake_up(&ar->event_wq);
}

void ath6kl_scan_complete_evt(struct ath6kl_vif *vif, int status)
{
	struct ath6kl *ar = vif->ar;
	bool aborted = false;

	if (status != WMI_SCAN_STATUS_SUCCESS)
		aborted = true;

	ath6kl_cfg80211_scan_complete_event(vif, aborted);

	if (!ar->usr_bss_filter) {
		clear_bit(CLEAR_BSSFILTER_ON_BEACON, &vif->flags);
		ath6kl_wmi_bssfilter_cmd(ar->wmi, vif->fw_vif_idx,
					 NONE_BSS_FILTER, 0);
	}

	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG, "scan complete: %d\n", status);
}

static int ath6kl_commit_ch_switch(struct ath6kl_vif *vif, u16 channel)
{
	struct ath6kl *ar = vif->ar;

	vif->profile.ch = cpu_to_le16(channel);

	switch (vif->nw_type) {
	case AP_NETWORK:
		/*
		 * reconfigure any saved RSN IE capabilites in the beacon /
		 * probe response to stay in sync with the supplicant.
		 */
		if (vif->rsn_capab &&
		    test_bit(ATH6KL_FW_CAPABILITY_RSN_CAP_OVERRIDE,
			     ar->fw_capabilities))
			ath6kl_wmi_set_ie_cmd(ar->wmi, vif->fw_vif_idx,
					      WLAN_EID_RSN, WMI_RSN_IE_CAPB,
					      (const u8 *) &vif->rsn_capab,
					      sizeof(vif->rsn_capab));

		return ath6kl_wmi_ap_profile_commit(ar->wmi, vif->fw_vif_idx,
						    &vif->profile);
	default:
		ath6kl_err("won't switch channels nw_type=%d\n", vif->nw_type);
		return -ENOTSUPP;
	}
}

static void ath6kl_check_ch_switch(struct ath6kl *ar, u16 channel)
{
	struct ath6kl_vif *vif;
	int res = 0;

	if (!ar->want_ch_switch)
		return;

	spin_lock_bh(&ar->list_lock);
	list_for_each_entry(vif, &ar->vif_list, list) {
		if (ar->want_ch_switch & (1 << vif->fw_vif_idx))
			res = ath6kl_commit_ch_switch(vif, channel);

		/* if channel switch failed, oh well we tried */
		ar->want_ch_switch &= ~(1 << vif->fw_vif_idx);

		if (res)
			ath6kl_err("channel switch failed nw_type %d res %d\n",
				   vif->nw_type, res);
	}
	spin_unlock_bh(&ar->list_lock);
}

void ath6kl_connect_event(struct ath6kl_vif *vif, u16 channel, u8 *bssid,
			  u16 listen_int, u16 beacon_int,
			  enum network_type net_type, u8 beacon_ie_len,
			  u8 assoc_req_len, u8 assoc_resp_len,
			  u8 *assoc_info)
{
	struct ath6kl *ar = vif->ar;

	ath6kl_cfg80211_connect_event(vif, channel, bssid,
				      listen_int, beacon_int,
				      net_type, beacon_ie_len,
				      assoc_req_len, assoc_resp_len,
				      assoc_info);

	memcpy(vif->bssid, bssid, sizeof(vif->bssid));
	vif->bss_ch = channel;

	if ((vif->nw_type == INFRA_NETWORK)) {
		ath6kl_wmi_listeninterval_cmd(ar->wmi, vif->fw_vif_idx,
					      vif->listen_intvl_t, 0);
		ath6kl_check_ch_switch(ar, channel);
	}

	netif_wake_queue(vif->ndev);

	/* Update connect & link status atomically */
	spin_lock_bh(&vif->if_lock);
	set_bit(CONNECTED, &vif->flags);
	clear_bit(CONNECT_PEND, &vif->flags);
	netif_carrier_on(vif->ndev);
	spin_unlock_bh(&vif->if_lock);

	aggr_reset_state(vif->aggr_cntxt->aggr_conn);
	vif->reconnect_flag = 0;

	if ((vif->nw_type == ADHOC_NETWORK) && ar->ibss_ps_enable) {
		memset(ar->node_map, 0, sizeof(ar->node_map));
		ar->node_num = 0;
		ar->next_ep_id = ENDPOINT_2;
	}

	if (!ar->usr_bss_filter) {
		set_bit(CLEAR_BSSFILTER_ON_BEACON, &vif->flags);
		ath6kl_wmi_bssfilter_cmd(ar->wmi, vif->fw_vif_idx,
					 CURRENT_BSS_FILTER, 0);
	}
}

void ath6kl_tkip_micerr_event(struct ath6kl_vif *vif, u8 keyid, bool ismcast)
{
	struct ath6kl_sta *sta;
	struct ath6kl *ar = vif->ar;
	u8 tsc[6];

	/*
	 * For AP case, keyid will have aid of STA which sent pkt with
	 * MIC error. Use this aid to get MAC & send it to hostapd.
	 */
	if (vif->nw_type == AP_NETWORK) {
		sta = ath6kl_find_sta_by_aid(ar, (keyid >> 2));
		if (!sta)
			return;

		ath6kl_dbg(ATH6KL_DBG_TRC,
			   "ap tkip mic error received from aid=%d\n", keyid);

		memset(tsc, 0, sizeof(tsc)); /* FIX: get correct TSC */
		cfg80211_michael_mic_failure(vif->ndev, sta->mac,
					     NL80211_KEYTYPE_PAIRWISE, keyid,
					     tsc, GFP_KERNEL);
	} else {
		ath6kl_cfg80211_tkip_micerr_event(vif, keyid, ismcast);
	}
}

static void ath6kl_update_target_stats(struct ath6kl_vif *vif, u8 *ptr, u32 len)
{
	struct wmi_target_stats *tgt_stats =
		(struct wmi_target_stats *) ptr;
	struct ath6kl *ar = vif->ar;
	struct target_stats *stats = &vif->target_stats;
	struct tkip_ccmp_stats *ccmp_stats;
	s32 rate;
	u8 ac;

	if (len < sizeof(*tgt_stats))
		return;

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

	rate = a_sle32_to_cpu(tgt_stats->stats.tx.ucast_rate);
	stats->tx_ucast_rate = ath6kl_wmi_get_rate(ar->wmi, rate);

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

	rate = a_sle32_to_cpu(tgt_stats->stats.rx.ucast_rate);
	stats->rx_ucast_rate = ath6kl_wmi_get_rate(ar->wmi, rate);

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

	stats->arp_received = le32_to_cpu(tgt_stats->arp_stats.arp_received);
	stats->arp_replied = le32_to_cpu(tgt_stats->arp_stats.arp_replied);
	stats->arp_matched = le32_to_cpu(tgt_stats->arp_stats.arp_matched);

	if (test_bit(STATS_UPDATE_PEND, &vif->flags)) {
		clear_bit(STATS_UPDATE_PEND, &vif->flags);
		wake_up(&ar->event_wq);
	}
}

static void ath6kl_add_le32(__le32 *var, __le32 val)
{
	*var = cpu_to_le32(le32_to_cpu(*var) + le32_to_cpu(val));
}

void ath6kl_tgt_stats_event(struct ath6kl_vif *vif, u8 *ptr, u32 len)
{
	struct wmi_ap_mode_stat *p = (struct wmi_ap_mode_stat *) ptr;
	struct ath6kl *ar = vif->ar;
	struct wmi_ap_mode_stat *ap = &ar->ap_stats;
	struct wmi_per_sta_stat *st_ap, *st_p;
	u8 ac;

	if (vif->nw_type == AP_NETWORK) {
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
		ath6kl_update_target_stats(vif, ptr, len);
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

void ath6kl_pspoll_event(struct ath6kl_vif *vif, u8 aid)
{
	struct ath6kl_sta *conn;
	struct sk_buff *skb;
	bool psq_empty = false;
	struct ath6kl *ar = vif->ar;
	struct ath6kl_mgmt_buff *mgmt_buf;

	conn = ath6kl_find_sta_by_aid(ar, aid);

	if (!conn)
		return;
	/*
	 * Send out a packet queued on ps queue. When the ps queue
	 * becomes empty update the PVB for this station.
	 */
	spin_lock_bh(&conn->psq_lock);
	psq_empty  = skb_queue_empty(&conn->psq) && (conn->mgmt_psq_len == 0);
	spin_unlock_bh(&conn->psq_lock);

	if (psq_empty)
		/* TODO: Send out a NULL data frame */
		return;

	spin_lock_bh(&conn->psq_lock);
	if (conn->mgmt_psq_len > 0) {
		mgmt_buf = list_first_entry(&conn->mgmt_psq,
					struct ath6kl_mgmt_buff, list);
		list_del(&mgmt_buf->list);
		conn->mgmt_psq_len--;
		spin_unlock_bh(&conn->psq_lock);

		conn->sta_flags |= STA_PS_POLLED;
		ath6kl_wmi_send_mgmt_cmd(ar->wmi, vif->fw_vif_idx,
					 mgmt_buf->id, mgmt_buf->freq,
					 mgmt_buf->wait, mgmt_buf->buf,
					 mgmt_buf->len, mgmt_buf->no_cck);
		conn->sta_flags &= ~STA_PS_POLLED;
		kfree(mgmt_buf);
	} else {
		skb = skb_dequeue(&conn->psq);
		spin_unlock_bh(&conn->psq_lock);

		conn->sta_flags |= STA_PS_POLLED;
		ath6kl_data_tx(skb, vif->ndev);
		conn->sta_flags &= ~STA_PS_POLLED;
	}

	spin_lock_bh(&conn->psq_lock);
	psq_empty  = skb_queue_empty(&conn->psq) && (conn->mgmt_psq_len == 0);
	spin_unlock_bh(&conn->psq_lock);

	if (psq_empty)
		ath6kl_wmi_set_pvb_cmd(ar->wmi, vif->fw_vif_idx, conn->aid, 0);
}

void ath6kl_dtimexpiry_event(struct ath6kl_vif *vif)
{
	bool mcastq_empty = false;
	struct sk_buff *skb;
	struct ath6kl *ar = vif->ar;

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
	set_bit(DTIM_EXPIRED, &vif->flags);

	spin_lock_bh(&ar->mcastpsq_lock);
	while ((skb = skb_dequeue(&ar->mcastpsq)) != NULL) {
		spin_unlock_bh(&ar->mcastpsq_lock);

		ath6kl_data_tx(skb, vif->ndev);

		spin_lock_bh(&ar->mcastpsq_lock);
	}
	spin_unlock_bh(&ar->mcastpsq_lock);

	clear_bit(DTIM_EXPIRED, &vif->flags);

	/* clear the LSB of the BitMapCtl field of the TIM IE */
	ath6kl_wmi_set_pvb_cmd(ar->wmi, vif->fw_vif_idx, MCAST_AID, 0);
}

void ath6kl_disconnect_event(struct ath6kl_vif *vif, u8 reason, u8 *bssid,
			     u8 assoc_resp_len, u8 *assoc_info,
			     u16 prot_reason_status)
{
	struct ath6kl *ar = vif->ar;

	if (vif->nw_type == AP_NETWORK) {
		/* disconnect due to other STA vif switching channels */
		if (reason == BSS_DISCONNECTED &&
		    prot_reason_status == WMI_AP_REASON_STA_ROAM) {
			ar->want_ch_switch |= 1 << vif->fw_vif_idx;
			/* bail back to this channel if STA vif fails connect */
			ar->last_ch = le16_to_cpu(vif->profile.ch);
		}

		if (prot_reason_status == WMI_AP_REASON_MAX_STA) {
			/* send max client reached notification to user space */
			cfg80211_conn_failed(vif->ndev, bssid,
					     NL80211_CONN_FAIL_MAX_CLIENTS,
					     GFP_KERNEL);
		}

		if (prot_reason_status == WMI_AP_REASON_ACL) {
			/* send blocked client notification to user space */
			cfg80211_conn_failed(vif->ndev, bssid,
					     NL80211_CONN_FAIL_BLOCKED_CLIENT,
					     GFP_KERNEL);
		}

		if (!ath6kl_remove_sta(ar, bssid, prot_reason_status))
			return;

		/* if no more associated STAs, empty the mcast PS q */
		if (ar->sta_list_index == 0) {
			spin_lock_bh(&ar->mcastpsq_lock);
			skb_queue_purge(&ar->mcastpsq);
			spin_unlock_bh(&ar->mcastpsq_lock);

			/* clear the LSB of the TIM IE's BitMapCtl field */
			if (test_bit(WMI_READY, &ar->flag))
				ath6kl_wmi_set_pvb_cmd(ar->wmi, vif->fw_vif_idx,
						       MCAST_AID, 0);
		}

		if (!is_broadcast_ether_addr(bssid)) {
			/* send event to application */
			cfg80211_del_sta(vif->ndev, bssid, GFP_KERNEL);
		}

		if (memcmp(vif->ndev->dev_addr, bssid, ETH_ALEN) == 0) {
			memset(vif->wep_key_list, 0, sizeof(vif->wep_key_list));
			clear_bit(CONNECTED, &vif->flags);
		}
		return;
	}

	ath6kl_cfg80211_disconnect_event(vif, reason, bssid,
					 assoc_resp_len, assoc_info,
					 prot_reason_status);

	aggr_reset_state(vif->aggr_cntxt->aggr_conn);

	del_timer(&vif->disconnect_timer);

	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG, "disconnect reason is %d\n", reason);

	/*
	 * If the event is due to disconnect cmd from the host, only they
	 * the target would stop trying to connect. Under any other
	 * condition, target would keep trying to connect.
	 */
	if (reason == DISCONNECT_CMD) {
		if (!ar->usr_bss_filter && test_bit(WMI_READY, &ar->flag))
			ath6kl_wmi_bssfilter_cmd(ar->wmi, vif->fw_vif_idx,
						 NONE_BSS_FILTER, 0);
	} else {
		set_bit(CONNECT_PEND, &vif->flags);
		if (((reason == ASSOC_FAILED) &&
		     (prot_reason_status == 0x11)) ||
		    ((reason == ASSOC_FAILED) && (prot_reason_status == 0x0) &&
		     (vif->reconnect_flag == 1))) {
			set_bit(CONNECTED, &vif->flags);
			return;
		}
	}

	/* restart disconnected concurrent vifs waiting for new channel */
	ath6kl_check_ch_switch(ar, ar->last_ch);

	/* update connect & link status atomically */
	spin_lock_bh(&vif->if_lock);
	clear_bit(CONNECTED, &vif->flags);
	netif_carrier_off(vif->ndev);
	spin_unlock_bh(&vif->if_lock);

	if ((reason != CSERV_DISCONNECT) || (vif->reconnect_flag != 1))
		vif->reconnect_flag = 0;

	if (reason != CSERV_DISCONNECT)
		ar->user_key_ctrl = 0;

	netif_stop_queue(vif->ndev);
	memset(vif->bssid, 0, sizeof(vif->bssid));
	vif->bss_ch = 0;

	ath6kl_tx_data_cleanup(ar);
}

struct ath6kl_vif *ath6kl_vif_first(struct ath6kl *ar)
{
	struct ath6kl_vif *vif;

	spin_lock_bh(&ar->list_lock);
	if (list_empty(&ar->vif_list)) {
		spin_unlock_bh(&ar->list_lock);
		return NULL;
	}

	vif = list_first_entry(&ar->vif_list, struct ath6kl_vif, list);

	spin_unlock_bh(&ar->list_lock);

	return vif;
}

static int ath6kl_open(struct net_device *dev)
{
	struct ath6kl_vif *vif = netdev_priv(dev);

	set_bit(WLAN_ENABLED, &vif->flags);

	if (test_bit(CONNECTED, &vif->flags)) {
		netif_carrier_on(dev);
		netif_wake_queue(dev);
	} else {
		netif_carrier_off(dev);
	}

	return 0;
}

static int ath6kl_close(struct net_device *dev)
{
	struct ath6kl_vif *vif = netdev_priv(dev);

	netif_stop_queue(dev);

	ath6kl_cfg80211_stop(vif);

	clear_bit(WLAN_ENABLED, &vif->flags);

	return 0;
}

static struct net_device_stats *ath6kl_get_stats(struct net_device *dev)
{
	struct ath6kl_vif *vif = netdev_priv(dev);

	return &vif->net_stats;
}

static int ath6kl_set_features(struct net_device *dev,
			       netdev_features_t features)
{
	struct ath6kl_vif *vif = netdev_priv(dev);
	struct ath6kl *ar = vif->ar;
	int err = 0;

	if ((features & NETIF_F_RXCSUM) &&
	    (ar->rx_meta_ver != WMI_META_VERSION_2)) {
		ar->rx_meta_ver = WMI_META_VERSION_2;
		err = ath6kl_wmi_set_rx_frame_format_cmd(ar->wmi,
							 vif->fw_vif_idx,
							 ar->rx_meta_ver, 0, 0);
		if (err) {
			dev->features = features & ~NETIF_F_RXCSUM;
			return err;
		}
	} else if (!(features & NETIF_F_RXCSUM) &&
		   (ar->rx_meta_ver == WMI_META_VERSION_2)) {
		ar->rx_meta_ver = 0;
		err = ath6kl_wmi_set_rx_frame_format_cmd(ar->wmi,
							 vif->fw_vif_idx,
							 ar->rx_meta_ver, 0, 0);
		if (err) {
			dev->features = features | NETIF_F_RXCSUM;
			return err;
		}
	}

	return err;
}

static void ath6kl_set_multicast_list(struct net_device *ndev)
{
	struct ath6kl_vif *vif = netdev_priv(ndev);
	bool mc_all_on = false;
	int mc_count = netdev_mc_count(ndev);
	struct netdev_hw_addr *ha;
	bool found;
	struct ath6kl_mc_filter *mc_filter, *tmp;
	struct list_head mc_filter_new;
	int ret;

	if (!test_bit(WMI_READY, &vif->ar->flag) ||
	    !test_bit(WLAN_ENABLED, &vif->flags))
		return;

	/* Enable multicast-all filter. */
	mc_all_on = !!(ndev->flags & IFF_PROMISC) ||
		    !!(ndev->flags & IFF_ALLMULTI) ||
		    !!(mc_count > ATH6K_MAX_MC_FILTERS_PER_LIST);

	if (mc_all_on)
		set_bit(NETDEV_MCAST_ALL_ON, &vif->flags);
	else
		clear_bit(NETDEV_MCAST_ALL_ON, &vif->flags);

	if (test_bit(ATH6KL_FW_CAPABILITY_WOW_MULTICAST_FILTER,
		     vif->ar->fw_capabilities)) {
		mc_all_on = mc_all_on || (vif->ar->state == ATH6KL_STATE_ON);
	}

	if (!(ndev->flags & IFF_MULTICAST)) {
		mc_all_on = false;
		set_bit(NETDEV_MCAST_ALL_OFF, &vif->flags);
	} else {
		clear_bit(NETDEV_MCAST_ALL_OFF, &vif->flags);
	}

	/* Enable/disable "multicast-all" filter*/
	ath6kl_dbg(ATH6KL_DBG_TRC, "%s multicast-all filter\n",
		   mc_all_on ? "enabling" : "disabling");

	ret = ath6kl_wmi_mcast_filter_cmd(vif->ar->wmi, vif->fw_vif_idx,
						  mc_all_on);
	if (ret) {
		ath6kl_warn("Failed to %s multicast-all receive\n",
			    mc_all_on ? "enable" : "disable");
		return;
	}

	if (test_bit(NETDEV_MCAST_ALL_ON, &vif->flags))
		return;

	/* Keep the driver and firmware mcast list in sync. */
	list_for_each_entry_safe(mc_filter, tmp, &vif->mc_filter, list) {
		found = false;
		netdev_for_each_mc_addr(ha, ndev) {
			if (memcmp(ha->addr, mc_filter->hw_addr,
				   ATH6KL_MCAST_FILTER_MAC_ADDR_SIZE) == 0) {
				found = true;
				break;
			}
		}

		if (!found) {
			/*
			 * Delete the filter which was previously set
			 * but not in the new request.
			 */
			ath6kl_dbg(ATH6KL_DBG_TRC,
				   "Removing %pM from multicast filter\n",
				   mc_filter->hw_addr);
			ret = ath6kl_wmi_add_del_mcast_filter_cmd(vif->ar->wmi,
					vif->fw_vif_idx, mc_filter->hw_addr,
					false);
			if (ret) {
				ath6kl_warn("Failed to remove multicast filter:%pM\n",
					    mc_filter->hw_addr);
				return;
			}

			list_del(&mc_filter->list);
			kfree(mc_filter);
		}
	}

	INIT_LIST_HEAD(&mc_filter_new);

	netdev_for_each_mc_addr(ha, ndev) {
		found = false;
		list_for_each_entry(mc_filter, &vif->mc_filter, list) {
			if (memcmp(ha->addr, mc_filter->hw_addr,
				   ATH6KL_MCAST_FILTER_MAC_ADDR_SIZE) == 0) {
				found = true;
				break;
			}
		}

		if (!found) {
			mc_filter = kzalloc(sizeof(struct ath6kl_mc_filter),
					    GFP_ATOMIC);
			if (!mc_filter) {
				WARN_ON(1);
				goto out;
			}

			memcpy(mc_filter->hw_addr, ha->addr,
			       ATH6KL_MCAST_FILTER_MAC_ADDR_SIZE);
			/* Set the multicast filter */
			ath6kl_dbg(ATH6KL_DBG_TRC,
				   "Adding %pM to multicast filter list\n",
				   mc_filter->hw_addr);
			ret = ath6kl_wmi_add_del_mcast_filter_cmd(vif->ar->wmi,
					vif->fw_vif_idx, mc_filter->hw_addr,
					true);
			if (ret) {
				ath6kl_warn("Failed to add multicast filter :%pM\n",
					    mc_filter->hw_addr);
				kfree(mc_filter);
				goto out;
			}

			list_add_tail(&mc_filter->list, &mc_filter_new);
		}
	}

out:
	list_splice_tail(&mc_filter_new, &vif->mc_filter);
}

static const struct net_device_ops ath6kl_netdev_ops = {
	.ndo_open               = ath6kl_open,
	.ndo_stop               = ath6kl_close,
	.ndo_start_xmit         = ath6kl_data_tx,
	.ndo_get_stats          = ath6kl_get_stats,
	.ndo_set_features       = ath6kl_set_features,
	.ndo_set_rx_mode	= ath6kl_set_multicast_list,
};

void init_netdev(struct net_device *dev)
{
	struct ath6kl *ar = ath6kl_priv(dev);

	dev->netdev_ops = &ath6kl_netdev_ops;
	dev->destructor = free_netdev;
	dev->watchdog_timeo = ATH6KL_TX_TIMEOUT;

	dev->needed_headroom = ETH_HLEN;
	dev->needed_headroom += roundup(sizeof(struct ath6kl_llc_snap_hdr) +
					sizeof(struct wmi_data_hdr) +
					HTC_HDR_LENGTH +
					WMI_MAX_TX_META_SZ +
					ATH6KL_HTC_ALIGN_BYTES, 4);

	if (!test_bit(ATH6KL_FW_CAPABILITY_NO_IP_CHECKSUM,
		      ar->fw_capabilities))
		dev->hw_features |= NETIF_F_IP_CSUM | NETIF_F_RXCSUM;

	return;
}
