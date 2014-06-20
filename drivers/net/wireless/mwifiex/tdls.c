/* Marvell Wireless LAN device driver: TDLS handling
 *
 * Copyright (C) 2014, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available on the worldwide web at
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#include "main.h"
#include "wmm.h"
#include "11n.h"
#include "11n_rxreorder.h"
#include "11ac.h"

#define TDLS_REQ_FIX_LEN      6
#define TDLS_RESP_FIX_LEN     8
#define TDLS_CONFIRM_FIX_LEN  6

static void mwifiex_restore_tdls_packets(struct mwifiex_private *priv,
					 const u8 *mac, u8 status)
{
	struct mwifiex_ra_list_tbl *ra_list;
	struct list_head *tid_list;
	struct sk_buff *skb, *tmp;
	struct mwifiex_txinfo *tx_info;
	unsigned long flags;
	u32 tid;
	u8 tid_down;

	dev_dbg(priv->adapter->dev, "%s: %pM\n", __func__, mac);
	spin_lock_irqsave(&priv->wmm.ra_list_spinlock, flags);

	skb_queue_walk_safe(&priv->tdls_txq, skb, tmp) {
		if (!ether_addr_equal(mac, skb->data))
			continue;

		__skb_unlink(skb, &priv->tdls_txq);
		tx_info = MWIFIEX_SKB_TXCB(skb);
		tid = skb->priority;
		tid_down = mwifiex_wmm_downgrade_tid(priv, tid);

		if (status == TDLS_SETUP_COMPLETE) {
			ra_list = mwifiex_wmm_get_queue_raptr(priv, tid, mac);
			ra_list->tdls_link = true;
			tx_info->flags |= MWIFIEX_BUF_FLAG_TDLS_PKT;
		} else {
			tid_list = &priv->wmm.tid_tbl_ptr[tid_down].ra_list;
			if (!list_empty(tid_list))
				ra_list = list_first_entry(tid_list,
					      struct mwifiex_ra_list_tbl, list);
			else
				ra_list = NULL;
			tx_info->flags &= ~MWIFIEX_BUF_FLAG_TDLS_PKT;
		}

		if (!ra_list) {
			mwifiex_write_data_complete(priv->adapter, skb, 0, -1);
			continue;
		}

		skb_queue_tail(&ra_list->skb_head, skb);

		ra_list->ba_pkt_count++;
		ra_list->total_pkt_count++;

		if (atomic_read(&priv->wmm.highest_queued_prio) <
						       tos_to_tid_inv[tid_down])
			atomic_set(&priv->wmm.highest_queued_prio,
				   tos_to_tid_inv[tid_down]);

		atomic_inc(&priv->wmm.tx_pkts_queued);
	}

	spin_unlock_irqrestore(&priv->wmm.ra_list_spinlock, flags);
	return;
}

static void mwifiex_hold_tdls_packets(struct mwifiex_private *priv,
				      const u8 *mac)
{
	struct mwifiex_ra_list_tbl *ra_list;
	struct list_head *ra_list_head;
	struct sk_buff *skb, *tmp;
	unsigned long flags;
	int i;

	dev_dbg(priv->adapter->dev, "%s: %pM\n", __func__, mac);
	spin_lock_irqsave(&priv->wmm.ra_list_spinlock, flags);

	for (i = 0; i < MAX_NUM_TID; i++) {
		if (!list_empty(&priv->wmm.tid_tbl_ptr[i].ra_list)) {
			ra_list_head = &priv->wmm.tid_tbl_ptr[i].ra_list;
			list_for_each_entry(ra_list, ra_list_head, list) {
				skb_queue_walk_safe(&ra_list->skb_head, skb,
						    tmp) {
					if (!ether_addr_equal(mac, skb->data))
						continue;
					__skb_unlink(skb, &ra_list->skb_head);
					atomic_dec(&priv->wmm.tx_pkts_queued);
					ra_list->total_pkt_count--;
					skb_queue_tail(&priv->tdls_txq, skb);
				}
			}
		}
	}

	spin_unlock_irqrestore(&priv->wmm.ra_list_spinlock, flags);
	return;
}

/* This function appends rate TLV to scan config command. */
static int
mwifiex_tdls_append_rates_ie(struct mwifiex_private *priv,
			     struct sk_buff *skb)
{
	u8 rates[MWIFIEX_SUPPORTED_RATES], *pos;
	u16 rates_size, supp_rates_size, ext_rates_size;

	memset(rates, 0, sizeof(rates));
	rates_size = mwifiex_get_supported_rates(priv, rates);

	supp_rates_size = min_t(u16, rates_size, MWIFIEX_TDLS_SUPPORTED_RATES);

	if (skb_tailroom(skb) < rates_size + 4) {
		dev_err(priv->adapter->dev,
			"Insuffient space while adding rates\n");
		return -ENOMEM;
	}

	pos = skb_put(skb, supp_rates_size + 2);
	*pos++ = WLAN_EID_SUPP_RATES;
	*pos++ = supp_rates_size;
	memcpy(pos, rates, supp_rates_size);

	if (rates_size > MWIFIEX_TDLS_SUPPORTED_RATES) {
		ext_rates_size = rates_size - MWIFIEX_TDLS_SUPPORTED_RATES;
		pos = skb_put(skb, ext_rates_size + 2);
		*pos++ = WLAN_EID_EXT_SUPP_RATES;
		*pos++ = ext_rates_size;
		memcpy(pos, rates + MWIFIEX_TDLS_SUPPORTED_RATES,
		       ext_rates_size);
	}

	return 0;
}

static void mwifiex_tdls_add_aid(struct mwifiex_private *priv,
				struct sk_buff *skb)
{
	struct ieee_types_assoc_rsp *assoc_rsp;
	u8 *pos;

	assoc_rsp = (struct ieee_types_assoc_rsp *)&priv->assoc_rsp_buf;
	pos = (void *)skb_put(skb, 4);
	*pos++ = WLAN_EID_AID;
	*pos++ = 2;
	*pos++ = le16_to_cpu(assoc_rsp->a_id);

	return;
}

static int mwifiex_tdls_add_vht_capab(struct mwifiex_private *priv,
				      struct sk_buff *skb)
{
	struct ieee80211_vht_cap vht_cap;
	u8 *pos;

	pos = (void *)skb_put(skb, sizeof(struct ieee80211_vht_cap) + 2);
	*pos++ = WLAN_EID_VHT_CAPABILITY;
	*pos++ = sizeof(struct ieee80211_vht_cap);

	memset(&vht_cap, 0, sizeof(struct ieee80211_vht_cap));

	mwifiex_fill_vht_cap_tlv(priv, &vht_cap, priv->curr_bss_params.band);
	memcpy(pos, &vht_cap, sizeof(vht_cap));

	return 0;
}

static int
mwifiex_tdls_add_ht_oper(struct mwifiex_private *priv, const u8 *mac,
			 u8 vht_enabled, struct sk_buff *skb)
{
	struct ieee80211_ht_operation *ht_oper;
	struct mwifiex_sta_node *sta_ptr;
	struct mwifiex_bssdescriptor *bss_desc =
					&priv->curr_bss_params.bss_descriptor;
	u8 *pos;

	sta_ptr = mwifiex_get_sta_entry(priv, mac);
	if (unlikely(!sta_ptr)) {
		dev_warn(priv->adapter->dev,
			 "TDLS peer station not found in list\n");
		return -1;
	}

	pos = (void *)skb_put(skb, sizeof(struct ieee80211_ht_operation) + 2);
	*pos++ = WLAN_EID_HT_OPERATION;
	*pos++ = sizeof(struct ieee80211_ht_operation);
	ht_oper = (void *)pos;

	ht_oper->primary_chan = bss_desc->channel;

	/* follow AP's channel bandwidth */
	if (ISSUPP_CHANWIDTH40(priv->adapter->hw_dot_11n_dev_cap) &&
	    bss_desc->bcn_ht_cap &&
	    ISALLOWED_CHANWIDTH40(bss_desc->bcn_ht_oper->ht_param))
		ht_oper->ht_param = bss_desc->bcn_ht_oper->ht_param;

	if (vht_enabled) {
		ht_oper->ht_param =
			  mwifiex_get_sec_chan_offset(bss_desc->channel);
		ht_oper->ht_param |= BIT(2);
	}

	memcpy(&sta_ptr->tdls_cap.ht_oper, ht_oper,
	       sizeof(struct ieee80211_ht_operation));

	return 0;
}

static int mwifiex_tdls_add_vht_oper(struct mwifiex_private *priv,
				     const u8 *mac, struct sk_buff *skb)
{
	struct mwifiex_bssdescriptor *bss_desc;
	struct ieee80211_vht_operation *vht_oper;
	struct ieee80211_vht_cap *vht_cap, *ap_vht_cap = NULL;
	struct mwifiex_sta_node *sta_ptr;
	struct mwifiex_adapter *adapter = priv->adapter;
	u8 supp_chwd_set, peer_supp_chwd_set;
	u8 *pos, ap_supp_chwd_set, chan_bw;
	u16 mcs_map_user, mcs_map_resp, mcs_map_result;
	u16 mcs_user, mcs_resp, nss;
	u32 usr_vht_cap_info;

	bss_desc = &priv->curr_bss_params.bss_descriptor;

	sta_ptr = mwifiex_get_sta_entry(priv, mac);
	if (unlikely(!sta_ptr)) {
		dev_warn(adapter->dev, "TDLS peer station not found in list\n");
		return -1;
	}

	if (!mwifiex_is_bss_in_11ac_mode(priv)) {
		if (sta_ptr->tdls_cap.extcap.ext_capab[7] &
		   WLAN_EXT_CAPA8_TDLS_WIDE_BW_ENABLED) {
			dev_dbg(adapter->dev,
				"TDLS peer doesn't support wider bandwitdh\n");
			return 0;
		}
	} else {
		ap_vht_cap = bss_desc->bcn_vht_cap;
	}

	pos = (void *)skb_put(skb, sizeof(struct ieee80211_vht_operation) + 2);
	*pos++ = WLAN_EID_VHT_OPERATION;
	*pos++ = sizeof(struct ieee80211_vht_operation);
	vht_oper = (struct ieee80211_vht_operation *)pos;

	if (bss_desc->bss_band & BAND_A)
		usr_vht_cap_info = adapter->usr_dot_11ac_dev_cap_a;
	else
		usr_vht_cap_info = adapter->usr_dot_11ac_dev_cap_bg;

	/* find the minmum bandwith between AP/TDLS peers */
	vht_cap = &sta_ptr->tdls_cap.vhtcap;
	supp_chwd_set = GET_VHTCAP_CHWDSET(usr_vht_cap_info);
	peer_supp_chwd_set =
			 GET_VHTCAP_CHWDSET(le32_to_cpu(vht_cap->vht_cap_info));
	supp_chwd_set = min_t(u8, supp_chwd_set, peer_supp_chwd_set);

	/* We need check AP's bandwidth when TDLS_WIDER_BANDWIDTH is off */

	if (ap_vht_cap && sta_ptr->tdls_cap.extcap.ext_capab[7] &
	    WLAN_EXT_CAPA8_TDLS_WIDE_BW_ENABLED) {
		ap_supp_chwd_set =
		      GET_VHTCAP_CHWDSET(le32_to_cpu(ap_vht_cap->vht_cap_info));
		supp_chwd_set = min_t(u8, supp_chwd_set, ap_supp_chwd_set);
	}

	switch (supp_chwd_set) {
	case IEEE80211_VHT_CHANWIDTH_80MHZ:
		vht_oper->chan_width = IEEE80211_VHT_CHANWIDTH_80MHZ;
		break;
	case IEEE80211_VHT_CHANWIDTH_160MHZ:
		vht_oper->chan_width = IEEE80211_VHT_CHANWIDTH_160MHZ;
		break;
	case IEEE80211_VHT_CHANWIDTH_80P80MHZ:
		vht_oper->chan_width = IEEE80211_VHT_CHANWIDTH_80P80MHZ;
		break;
	default:
		vht_oper->chan_width = IEEE80211_VHT_CHANWIDTH_USE_HT;
		break;
	}

	mcs_map_user = GET_DEVRXMCSMAP(adapter->usr_dot_11ac_mcs_support);
	mcs_map_resp = le16_to_cpu(vht_cap->supp_mcs.rx_mcs_map);
	mcs_map_result = 0;

	for (nss = 1; nss <= 8; nss++) {
		mcs_user = GET_VHTNSSMCS(mcs_map_user, nss);
		mcs_resp = GET_VHTNSSMCS(mcs_map_resp, nss);

		if ((mcs_user == IEEE80211_VHT_MCS_NOT_SUPPORTED) ||
		    (mcs_resp == IEEE80211_VHT_MCS_NOT_SUPPORTED))
			SET_VHTNSSMCS(mcs_map_result, nss,
				      IEEE80211_VHT_MCS_NOT_SUPPORTED);
		else
			SET_VHTNSSMCS(mcs_map_result, nss,
				      min_t(u16, mcs_user, mcs_resp));
	}

	vht_oper->basic_mcs_set = cpu_to_le16(mcs_map_result);

	switch (vht_oper->chan_width) {
	case IEEE80211_VHT_CHANWIDTH_80MHZ:
		chan_bw = IEEE80211_VHT_CHANWIDTH_80MHZ;
		break;
	case IEEE80211_VHT_CHANWIDTH_160MHZ:
		chan_bw = IEEE80211_VHT_CHANWIDTH_160MHZ;
		break;
	case IEEE80211_VHT_CHANWIDTH_80P80MHZ:
		chan_bw = IEEE80211_VHT_CHANWIDTH_80MHZ;
		break;
	default:
		chan_bw = IEEE80211_VHT_CHANWIDTH_USE_HT;
		break;
	}
	vht_oper->center_freq_seg1_idx =
			mwifiex_get_center_freq_index(priv, BAND_AAC,
						      bss_desc->channel,
						      chan_bw);

	return 0;
}

static void mwifiex_tdls_add_ext_capab(struct mwifiex_private *priv,
				       struct sk_buff *skb)
{
	struct ieee_types_extcap *extcap;

	extcap = (void *)skb_put(skb, sizeof(struct ieee_types_extcap));
	extcap->ieee_hdr.element_id = WLAN_EID_EXT_CAPABILITY;
	extcap->ieee_hdr.len = 8;
	memset(extcap->ext_capab, 0, 8);
	extcap->ext_capab[4] |= WLAN_EXT_CAPA5_TDLS_ENABLED;

	if (priv->adapter->is_hw_11ac_capable)
		extcap->ext_capab[7] |= WLAN_EXT_CAPA8_TDLS_WIDE_BW_ENABLED;
}

static void mwifiex_tdls_add_qos_capab(struct sk_buff *skb)
{
	u8 *pos = (void *)skb_put(skb, 3);

	*pos++ = WLAN_EID_QOS_CAPA;
	*pos++ = 1;
	*pos++ = MWIFIEX_TDLS_DEF_QOS_CAPAB;
}

static int mwifiex_prep_tdls_encap_data(struct mwifiex_private *priv,
					const u8 *peer, u8 action_code,
					u8 dialog_token,
					u16 status_code, struct sk_buff *skb)
{
	struct ieee80211_tdls_data *tf;
	int ret;
	u16 capab;
	struct ieee80211_ht_cap *ht_cap;
	u8 radio, *pos;

	capab = priv->curr_bss_params.bss_descriptor.cap_info_bitmap;

	tf = (void *)skb_put(skb, offsetof(struct ieee80211_tdls_data, u));
	memcpy(tf->da, peer, ETH_ALEN);
	memcpy(tf->sa, priv->curr_addr, ETH_ALEN);
	tf->ether_type = cpu_to_be16(ETH_P_TDLS);
	tf->payload_type = WLAN_TDLS_SNAP_RFTYPE;

	switch (action_code) {
	case WLAN_TDLS_SETUP_REQUEST:
		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_SETUP_REQUEST;
		skb_put(skb, sizeof(tf->u.setup_req));
		tf->u.setup_req.dialog_token = dialog_token;
		tf->u.setup_req.capability = cpu_to_le16(capab);
		ret = mwifiex_tdls_append_rates_ie(priv, skb);
		if (ret) {
			dev_kfree_skb_any(skb);
			return ret;
		}

		pos = (void *)skb_put(skb, sizeof(struct ieee80211_ht_cap) + 2);
		*pos++ = WLAN_EID_HT_CAPABILITY;
		*pos++ = sizeof(struct ieee80211_ht_cap);
		ht_cap = (void *)pos;
		radio = mwifiex_band_to_radio_type(priv->curr_bss_params.band);
		ret = mwifiex_fill_cap_info(priv, radio, ht_cap);
		if (ret) {
			dev_kfree_skb_any(skb);
			return ret;
		}

		if (priv->adapter->is_hw_11ac_capable) {
			ret = mwifiex_tdls_add_vht_capab(priv, skb);
			if (ret) {
				dev_kfree_skb_any(skb);
				return ret;
			}
			mwifiex_tdls_add_aid(priv, skb);
		}

		mwifiex_tdls_add_ext_capab(priv, skb);
		mwifiex_tdls_add_qos_capab(skb);
		break;

	case WLAN_TDLS_SETUP_RESPONSE:
		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_SETUP_RESPONSE;
		skb_put(skb, sizeof(tf->u.setup_resp));
		tf->u.setup_resp.status_code = cpu_to_le16(status_code);
		tf->u.setup_resp.dialog_token = dialog_token;
		tf->u.setup_resp.capability = cpu_to_le16(capab);
		ret = mwifiex_tdls_append_rates_ie(priv, skb);
		if (ret) {
			dev_kfree_skb_any(skb);
			return ret;
		}

		pos = (void *)skb_put(skb, sizeof(struct ieee80211_ht_cap) + 2);
		*pos++ = WLAN_EID_HT_CAPABILITY;
		*pos++ = sizeof(struct ieee80211_ht_cap);
		ht_cap = (void *)pos;
		radio = mwifiex_band_to_radio_type(priv->curr_bss_params.band);
		ret = mwifiex_fill_cap_info(priv, radio, ht_cap);
		if (ret) {
			dev_kfree_skb_any(skb);
			return ret;
		}

		if (priv->adapter->is_hw_11ac_capable) {
			ret = mwifiex_tdls_add_vht_capab(priv, skb);
			if (ret) {
				dev_kfree_skb_any(skb);
				return ret;
			}
			mwifiex_tdls_add_aid(priv, skb);
		}

		mwifiex_tdls_add_ext_capab(priv, skb);
		mwifiex_tdls_add_qos_capab(skb);
		break;

	case WLAN_TDLS_SETUP_CONFIRM:
		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_SETUP_CONFIRM;
		skb_put(skb, sizeof(tf->u.setup_cfm));
		tf->u.setup_cfm.status_code = cpu_to_le16(status_code);
		tf->u.setup_cfm.dialog_token = dialog_token;
		if (priv->adapter->is_hw_11ac_capable) {
			ret = mwifiex_tdls_add_vht_oper(priv, peer, skb);
			if (ret) {
				dev_kfree_skb_any(skb);
				return ret;
			}
			ret = mwifiex_tdls_add_ht_oper(priv, peer, 1, skb);
			if (ret) {
				dev_kfree_skb_any(skb);
				return ret;
			}
		} else {
			ret = mwifiex_tdls_add_ht_oper(priv, peer, 0, skb);
			if (ret) {
				dev_kfree_skb_any(skb);
				return ret;
			}
		}
		break;

	case WLAN_TDLS_TEARDOWN:
		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_TEARDOWN;
		skb_put(skb, sizeof(tf->u.teardown));
		tf->u.teardown.reason_code = cpu_to_le16(status_code);
		break;

	case WLAN_TDLS_DISCOVERY_REQUEST:
		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_DISCOVERY_REQUEST;
		skb_put(skb, sizeof(tf->u.discover_req));
		tf->u.discover_req.dialog_token = dialog_token;
		break;
	default:
		dev_err(priv->adapter->dev, "Unknown TDLS frame type.\n");
		return -EINVAL;
	}

	return 0;
}

static void
mwifiex_tdls_add_link_ie(struct sk_buff *skb, const u8 *src_addr,
			 const u8 *peer, const u8 *bssid)
{
	struct ieee80211_tdls_lnkie *lnkid;

	lnkid = (void *)skb_put(skb, sizeof(struct ieee80211_tdls_lnkie));
	lnkid->ie_type = WLAN_EID_LINK_ID;
	lnkid->ie_len = sizeof(struct ieee80211_tdls_lnkie) -
			sizeof(struct ieee_types_header);

	memcpy(lnkid->bssid, bssid, ETH_ALEN);
	memcpy(lnkid->init_sta, src_addr, ETH_ALEN);
	memcpy(lnkid->resp_sta, peer, ETH_ALEN);
}

int mwifiex_send_tdls_data_frame(struct mwifiex_private *priv, const u8 *peer,
				 u8 action_code, u8 dialog_token,
				 u16 status_code, const u8 *extra_ies,
				 size_t extra_ies_len)
{
	struct sk_buff *skb;
	struct mwifiex_txinfo *tx_info;
	int ret;
	u16 skb_len;

	skb_len = MWIFIEX_MIN_DATA_HEADER_LEN +
		  max(sizeof(struct ieee80211_mgmt),
		      sizeof(struct ieee80211_tdls_data)) +
		  MWIFIEX_MGMT_FRAME_HEADER_SIZE +
		  MWIFIEX_SUPPORTED_RATES +
		  3 + /* Qos Info */
		  sizeof(struct ieee_types_extcap) +
		  sizeof(struct ieee80211_ht_cap) +
		  sizeof(struct ieee_types_bss_co_2040) +
		  sizeof(struct ieee80211_ht_operation) +
		  sizeof(struct ieee80211_tdls_lnkie) +
		  extra_ies_len;

	if (priv->adapter->is_hw_11ac_capable)
		skb_len += sizeof(struct ieee_types_vht_cap) +
			   sizeof(struct ieee_types_vht_oper) +
			   sizeof(struct ieee_types_aid);

	skb = dev_alloc_skb(skb_len);
	if (!skb) {
		dev_err(priv->adapter->dev,
			"allocate skb failed for management frame\n");
		return -ENOMEM;
	}
	skb_reserve(skb, MWIFIEX_MIN_DATA_HEADER_LEN);

	switch (action_code) {
	case WLAN_TDLS_SETUP_REQUEST:
	case WLAN_TDLS_SETUP_CONFIRM:
	case WLAN_TDLS_TEARDOWN:
	case WLAN_TDLS_DISCOVERY_REQUEST:
		ret = mwifiex_prep_tdls_encap_data(priv, peer, action_code,
						   dialog_token, status_code,
						   skb);
		if (ret) {
			dev_kfree_skb_any(skb);
			return ret;
		}
		if (extra_ies_len)
			memcpy(skb_put(skb, extra_ies_len), extra_ies,
			       extra_ies_len);
		mwifiex_tdls_add_link_ie(skb, priv->curr_addr, peer,
					 priv->cfg_bssid);
		break;
	case WLAN_TDLS_SETUP_RESPONSE:
		ret = mwifiex_prep_tdls_encap_data(priv, peer, action_code,
						   dialog_token, status_code,
						   skb);
		if (ret) {
			dev_kfree_skb_any(skb);
			return ret;
		}
		if (extra_ies_len)
			memcpy(skb_put(skb, extra_ies_len), extra_ies,
			       extra_ies_len);
		mwifiex_tdls_add_link_ie(skb, peer, priv->curr_addr,
					 priv->cfg_bssid);
		break;
	}

	switch (action_code) {
	case WLAN_TDLS_SETUP_REQUEST:
	case WLAN_TDLS_SETUP_RESPONSE:
		skb->priority = MWIFIEX_PRIO_BK;
		break;
	default:
		skb->priority = MWIFIEX_PRIO_VI;
		break;
	}

	tx_info = MWIFIEX_SKB_TXCB(skb);
	tx_info->bss_num = priv->bss_num;
	tx_info->bss_type = priv->bss_type;

	__net_timestamp(skb);
	mwifiex_queue_tx_pkt(priv, skb);

	return 0;
}

static int
mwifiex_construct_tdls_action_frame(struct mwifiex_private *priv,
				    const u8 *peer,
				    u8 action_code, u8 dialog_token,
				    u16 status_code, struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt;
	u8 bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	int ret;
	u16 capab;
	struct ieee80211_ht_cap *ht_cap;
	u8 radio, *pos;

	capab = priv->curr_bss_params.bss_descriptor.cap_info_bitmap;

	mgmt = (void *)skb_put(skb, offsetof(struct ieee80211_mgmt, u));

	memset(mgmt, 0, 24);
	memcpy(mgmt->da, peer, ETH_ALEN);
	memcpy(mgmt->sa, priv->curr_addr, ETH_ALEN);
	memcpy(mgmt->bssid, priv->cfg_bssid, ETH_ALEN);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);

	/* add address 4 */
	pos = skb_put(skb, ETH_ALEN);

	switch (action_code) {
	case WLAN_PUB_ACTION_TDLS_DISCOVER_RES:
		skb_put(skb, sizeof(mgmt->u.action.u.tdls_discover_resp) + 1);
		mgmt->u.action.category = WLAN_CATEGORY_PUBLIC;
		mgmt->u.action.u.tdls_discover_resp.action_code =
					      WLAN_PUB_ACTION_TDLS_DISCOVER_RES;
		mgmt->u.action.u.tdls_discover_resp.dialog_token =
								   dialog_token;
		mgmt->u.action.u.tdls_discover_resp.capability =
							     cpu_to_le16(capab);
		/* move back for addr4 */
		memmove(pos + ETH_ALEN, &mgmt->u.action.category,
			sizeof(mgmt->u.action.u.tdls_discover_resp));
		/* init address 4 */
		memcpy(pos, bc_addr, ETH_ALEN);

		ret = mwifiex_tdls_append_rates_ie(priv, skb);
		if (ret) {
			dev_kfree_skb_any(skb);
			return ret;
		}

		pos = (void *)skb_put(skb, sizeof(struct ieee80211_ht_cap) + 2);
		*pos++ = WLAN_EID_HT_CAPABILITY;
		*pos++ = sizeof(struct ieee80211_ht_cap);
		ht_cap = (void *)pos;
		radio = mwifiex_band_to_radio_type(priv->curr_bss_params.band);
		ret = mwifiex_fill_cap_info(priv, radio, ht_cap);
		if (ret) {
			dev_kfree_skb_any(skb);
			return ret;
		}

		if (priv->adapter->is_hw_11ac_capable) {
			ret = mwifiex_tdls_add_vht_capab(priv, skb);
			if (ret) {
				dev_kfree_skb_any(skb);
				return ret;
			}
			mwifiex_tdls_add_aid(priv, skb);
		}

		mwifiex_tdls_add_ext_capab(priv, skb);
		mwifiex_tdls_add_qos_capab(skb);
		break;
	default:
		dev_err(priv->adapter->dev, "Unknown TDLS action frame type\n");
		return -EINVAL;
	}

	return 0;
}

int mwifiex_send_tdls_action_frame(struct mwifiex_private *priv, const u8 *peer,
				   u8 action_code, u8 dialog_token,
				   u16 status_code, const u8 *extra_ies,
				   size_t extra_ies_len)
{
	struct sk_buff *skb;
	struct mwifiex_txinfo *tx_info;
	u8 *pos;
	u32 pkt_type, tx_control;
	u16 pkt_len, skb_len;

	skb_len = MWIFIEX_MIN_DATA_HEADER_LEN +
		  max(sizeof(struct ieee80211_mgmt),
		      sizeof(struct ieee80211_tdls_data)) +
		  MWIFIEX_MGMT_FRAME_HEADER_SIZE +
		  MWIFIEX_SUPPORTED_RATES +
		  sizeof(struct ieee_types_extcap) +
		  sizeof(struct ieee80211_ht_cap) +
		  sizeof(struct ieee_types_bss_co_2040) +
		  sizeof(struct ieee80211_ht_operation) +
		  sizeof(struct ieee80211_tdls_lnkie) +
		  extra_ies_len +
		  3 + /* Qos Info */
		  ETH_ALEN; /* Address4 */

	if (priv->adapter->is_hw_11ac_capable)
		skb_len += sizeof(struct ieee_types_vht_cap) +
			   sizeof(struct ieee_types_vht_oper) +
			   sizeof(struct ieee_types_aid);

	skb = dev_alloc_skb(skb_len);
	if (!skb) {
		dev_err(priv->adapter->dev,
			"allocate skb failed for management frame\n");
		return -ENOMEM;
	}

	skb_reserve(skb, MWIFIEX_MIN_DATA_HEADER_LEN);

	pkt_type = PKT_TYPE_MGMT;
	tx_control = 0;
	pos = skb_put(skb, MWIFIEX_MGMT_FRAME_HEADER_SIZE + sizeof(pkt_len));
	memset(pos, 0, MWIFIEX_MGMT_FRAME_HEADER_SIZE + sizeof(pkt_len));
	memcpy(pos, &pkt_type, sizeof(pkt_type));
	memcpy(pos + sizeof(pkt_type), &tx_control, sizeof(tx_control));

	if (mwifiex_construct_tdls_action_frame(priv, peer, action_code,
						dialog_token, status_code,
						skb)) {
		dev_kfree_skb_any(skb);
		return -EINVAL;
	}

	if (extra_ies_len)
		memcpy(skb_put(skb, extra_ies_len), extra_ies, extra_ies_len);

	/* the TDLS link IE is always added last we are the responder */

	mwifiex_tdls_add_link_ie(skb, peer, priv->curr_addr,
				 priv->cfg_bssid);

	skb->priority = MWIFIEX_PRIO_VI;

	tx_info = MWIFIEX_SKB_TXCB(skb);
	tx_info->bss_num = priv->bss_num;
	tx_info->bss_type = priv->bss_type;
	tx_info->flags |= MWIFIEX_BUF_FLAG_TDLS_PKT;

	pkt_len = skb->len - MWIFIEX_MGMT_FRAME_HEADER_SIZE - sizeof(pkt_len);
	memcpy(skb->data + MWIFIEX_MGMT_FRAME_HEADER_SIZE, &pkt_len,
	       sizeof(pkt_len));
	__net_timestamp(skb);
	mwifiex_queue_tx_pkt(priv, skb);

	return 0;
}

/* This function process tdls action frame from peer.
 * Peer capabilities are stored into station node structure.
 */
void mwifiex_process_tdls_action_frame(struct mwifiex_private *priv,
				       u8 *buf, int len)
{
	struct mwifiex_sta_node *sta_ptr;
	u8 *peer, *pos, *end;
	u8 i, action, basic;
	int ie_len = 0;

	if (len < (sizeof(struct ethhdr) + 3))
		return;
	if (*(buf + sizeof(struct ethhdr)) != WLAN_TDLS_SNAP_RFTYPE)
		return;
	if (*(buf + sizeof(struct ethhdr) + 1) != WLAN_CATEGORY_TDLS)
		return;

	peer = buf + ETH_ALEN;
	action = *(buf + sizeof(struct ethhdr) + 2);

	/* just handle TDLS setup request/response/confirm */
	if (action > WLAN_TDLS_SETUP_CONFIRM)
		return;

	dev_dbg(priv->adapter->dev,
		"rx:tdls action: peer=%pM, action=%d\n", peer, action);

	sta_ptr = mwifiex_add_sta_entry(priv, peer);
	if (!sta_ptr)
		return;

	switch (action) {
	case WLAN_TDLS_SETUP_REQUEST:
		if (len < (sizeof(struct ethhdr) + TDLS_REQ_FIX_LEN))
			return;

		pos = buf + sizeof(struct ethhdr) + 4;
		/* payload 1+ category 1 + action 1 + dialog 1 */
		sta_ptr->tdls_cap.capab = cpu_to_le16(*(u16 *)pos);
		ie_len = len - sizeof(struct ethhdr) - TDLS_REQ_FIX_LEN;
		pos += 2;
		break;

	case WLAN_TDLS_SETUP_RESPONSE:
		if (len < (sizeof(struct ethhdr) + TDLS_RESP_FIX_LEN))
			return;
		/* payload 1+ category 1 + action 1 + dialog 1 + status code 2*/
		pos = buf + sizeof(struct ethhdr) + 6;
		sta_ptr->tdls_cap.capab = cpu_to_le16(*(u16 *)pos);
		ie_len = len - sizeof(struct ethhdr) - TDLS_RESP_FIX_LEN;
		pos += 2;
		break;

	case WLAN_TDLS_SETUP_CONFIRM:
		if (len < (sizeof(struct ethhdr) + TDLS_CONFIRM_FIX_LEN))
			return;
		pos = buf + sizeof(struct ethhdr) + TDLS_CONFIRM_FIX_LEN;
		ie_len = len - sizeof(struct ethhdr) - TDLS_CONFIRM_FIX_LEN;
		break;
	default:
		dev_warn(priv->adapter->dev, "Unknown TDLS frame type.\n");
		return;
	}

	for (end = pos + ie_len; pos + 1 < end; pos += 2 + pos[1]) {
		if (pos + 2 + pos[1] > end)
			break;

		switch (*pos) {
		case WLAN_EID_SUPP_RATES:
			sta_ptr->tdls_cap.rates_len = pos[1];
			for (i = 0; i < pos[1]; i++)
				sta_ptr->tdls_cap.rates[i] = pos[i + 2];
			break;

		case WLAN_EID_EXT_SUPP_RATES:
			basic = sta_ptr->tdls_cap.rates_len;
			for (i = 0; i < pos[1]; i++)
				sta_ptr->tdls_cap.rates[basic + i] = pos[i + 2];
			sta_ptr->tdls_cap.rates_len += pos[1];
			break;
		case WLAN_EID_HT_CAPABILITY:
			memcpy((u8 *)&sta_ptr->tdls_cap.ht_capb, pos,
			       sizeof(struct ieee80211_ht_cap));
			sta_ptr->is_11n_enabled = 1;
			break;
		case WLAN_EID_HT_OPERATION:
			memcpy(&sta_ptr->tdls_cap.ht_oper, pos,
			       sizeof(struct ieee80211_ht_operation));
			break;
		case WLAN_EID_BSS_COEX_2040:
			sta_ptr->tdls_cap.coex_2040 = pos[2];
			break;
		case WLAN_EID_EXT_CAPABILITY:
			memcpy((u8 *)&sta_ptr->tdls_cap.extcap, pos,
			       sizeof(struct ieee_types_header) +
			       min_t(u8, pos[1], 8));
			break;
		case WLAN_EID_RSN:
			memcpy((u8 *)&sta_ptr->tdls_cap.rsn_ie, pos,
			       sizeof(struct ieee_types_header) + pos[1]);
			break;
		case WLAN_EID_QOS_CAPA:
			sta_ptr->tdls_cap.qos_info = pos[2];
			break;
		case WLAN_EID_VHT_OPERATION:
			if (priv->adapter->is_hw_11ac_capable)
				memcpy(&sta_ptr->tdls_cap.vhtoper, pos,
				       sizeof(struct ieee80211_vht_operation));
			break;
		case WLAN_EID_VHT_CAPABILITY:
			if (priv->adapter->is_hw_11ac_capable) {
				memcpy((u8 *)&sta_ptr->tdls_cap.vhtcap, pos,
				       sizeof(struct ieee80211_vht_cap));
				sta_ptr->is_11ac_enabled = 1;
			}
			break;
		case WLAN_EID_AID:
			if (priv->adapter->is_hw_11ac_capable)
				sta_ptr->tdls_cap.aid =
					      le16_to_cpu(*(__le16 *)(pos + 2));
		default:
			break;
		}
	}

	return;
}

static int
mwifiex_tdls_process_config_link(struct mwifiex_private *priv, const u8 *peer)
{
	struct mwifiex_sta_node *sta_ptr;
	struct mwifiex_ds_tdls_oper tdls_oper;

	memset(&tdls_oper, 0, sizeof(struct mwifiex_ds_tdls_oper));
	sta_ptr = mwifiex_get_sta_entry(priv, peer);

	if (!sta_ptr || sta_ptr->tdls_status == TDLS_SETUP_FAILURE) {
		dev_err(priv->adapter->dev,
			"link absent for peer %pM; cannot config\n", peer);
		return -EINVAL;
	}

	memcpy(&tdls_oper.peer_mac, peer, ETH_ALEN);
	tdls_oper.tdls_action = MWIFIEX_TDLS_CONFIG_LINK;
	return mwifiex_send_cmd(priv, HostCmd_CMD_TDLS_OPER,
				HostCmd_ACT_GEN_SET, 0, &tdls_oper, true);
}

static int
mwifiex_tdls_process_create_link(struct mwifiex_private *priv, const u8 *peer)
{
	struct mwifiex_sta_node *sta_ptr;
	struct mwifiex_ds_tdls_oper tdls_oper;

	memset(&tdls_oper, 0, sizeof(struct mwifiex_ds_tdls_oper));
	sta_ptr = mwifiex_get_sta_entry(priv, peer);

	if (sta_ptr && sta_ptr->tdls_status == TDLS_SETUP_INPROGRESS) {
		dev_dbg(priv->adapter->dev,
			"Setup already in progress for peer %pM\n", peer);
		return 0;
	}

	sta_ptr = mwifiex_add_sta_entry(priv, peer);
	if (!sta_ptr)
		return -ENOMEM;

	sta_ptr->tdls_status = TDLS_SETUP_INPROGRESS;
	mwifiex_hold_tdls_packets(priv, peer);
	memcpy(&tdls_oper.peer_mac, peer, ETH_ALEN);
	tdls_oper.tdls_action = MWIFIEX_TDLS_CREATE_LINK;
	return mwifiex_send_cmd(priv, HostCmd_CMD_TDLS_OPER,
				HostCmd_ACT_GEN_SET, 0, &tdls_oper, true);
}

static int
mwifiex_tdls_process_disable_link(struct mwifiex_private *priv, const u8 *peer)
{
	struct mwifiex_sta_node *sta_ptr;
	struct mwifiex_ds_tdls_oper tdls_oper;
	unsigned long flags;

	memset(&tdls_oper, 0, sizeof(struct mwifiex_ds_tdls_oper));
	sta_ptr = mwifiex_get_sta_entry(priv, peer);

	if (sta_ptr) {
		if (sta_ptr->is_11n_enabled) {
			mwifiex_11n_cleanup_reorder_tbl(priv);
			spin_lock_irqsave(&priv->wmm.ra_list_spinlock,
					  flags);
			mwifiex_11n_delete_all_tx_ba_stream_tbl(priv);
			spin_unlock_irqrestore(&priv->wmm.ra_list_spinlock,
					       flags);
		}
		mwifiex_del_sta_entry(priv, peer);
	}

	mwifiex_restore_tdls_packets(priv, peer, TDLS_LINK_TEARDOWN);
	memcpy(&tdls_oper.peer_mac, peer, ETH_ALEN);
	tdls_oper.tdls_action = MWIFIEX_TDLS_DISABLE_LINK;
	return mwifiex_send_cmd(priv, HostCmd_CMD_TDLS_OPER,
				HostCmd_ACT_GEN_SET, 0, &tdls_oper, true);
}

static int
mwifiex_tdls_process_enable_link(struct mwifiex_private *priv, const u8 *peer)
{
	struct mwifiex_sta_node *sta_ptr;
	struct ieee80211_mcs_info mcs;
	unsigned long flags;
	int i;

	sta_ptr = mwifiex_get_sta_entry(priv, peer);

	if (sta_ptr && (sta_ptr->tdls_status != TDLS_SETUP_FAILURE)) {
		dev_dbg(priv->adapter->dev,
			"tdls: enable link %pM success\n", peer);

		sta_ptr->tdls_status = TDLS_SETUP_COMPLETE;

		mcs = sta_ptr->tdls_cap.ht_capb.mcs;
		if (mcs.rx_mask[0] != 0xff)
			sta_ptr->is_11n_enabled = true;
		if (sta_ptr->is_11n_enabled) {
			if (le16_to_cpu(sta_ptr->tdls_cap.ht_capb.cap_info) &
			    IEEE80211_HT_CAP_MAX_AMSDU)
				sta_ptr->max_amsdu =
					MWIFIEX_TX_DATA_BUF_SIZE_8K;
			else
				sta_ptr->max_amsdu =
					MWIFIEX_TX_DATA_BUF_SIZE_4K;

			for (i = 0; i < MAX_NUM_TID; i++)
				sta_ptr->ampdu_sta[i] =
					      priv->aggr_prio_tbl[i].ampdu_user;
		} else {
			for (i = 0; i < MAX_NUM_TID; i++)
				sta_ptr->ampdu_sta[i] = BA_STREAM_NOT_ALLOWED;
		}

		memset(sta_ptr->rx_seq, 0xff, sizeof(sta_ptr->rx_seq));
		mwifiex_restore_tdls_packets(priv, peer, TDLS_SETUP_COMPLETE);
	} else {
		dev_dbg(priv->adapter->dev,
			"tdls: enable link %pM failed\n", peer);
		if (sta_ptr) {
			mwifiex_11n_cleanup_reorder_tbl(priv);
			spin_lock_irqsave(&priv->wmm.ra_list_spinlock,
					  flags);
			mwifiex_11n_delete_all_tx_ba_stream_tbl(priv);
			spin_unlock_irqrestore(&priv->wmm.ra_list_spinlock,
					       flags);
			mwifiex_del_sta_entry(priv, peer);
		}
		mwifiex_restore_tdls_packets(priv, peer, TDLS_LINK_TEARDOWN);

		return -1;
	}

	return 0;
}

int mwifiex_tdls_oper(struct mwifiex_private *priv, const u8 *peer, u8 action)
{
	switch (action) {
	case MWIFIEX_TDLS_ENABLE_LINK:
		return mwifiex_tdls_process_enable_link(priv, peer);
	case MWIFIEX_TDLS_DISABLE_LINK:
		return mwifiex_tdls_process_disable_link(priv, peer);
	case MWIFIEX_TDLS_CREATE_LINK:
		return mwifiex_tdls_process_create_link(priv, peer);
	case MWIFIEX_TDLS_CONFIG_LINK:
		return mwifiex_tdls_process_config_link(priv, peer);
	}
	return 0;
}

int mwifiex_get_tdls_link_status(struct mwifiex_private *priv, const u8 *mac)
{
	struct mwifiex_sta_node *sta_ptr;

	sta_ptr = mwifiex_get_sta_entry(priv, mac);
	if (sta_ptr)
		return sta_ptr->tdls_status;

	return TDLS_NOT_SETUP;
}

void mwifiex_disable_all_tdls_links(struct mwifiex_private *priv)
{
	struct mwifiex_sta_node *sta_ptr;
	struct mwifiex_ds_tdls_oper tdls_oper;
	unsigned long flags;

	if (list_empty(&priv->sta_list))
		return;

	list_for_each_entry(sta_ptr, &priv->sta_list, list) {
		memset(&tdls_oper, 0, sizeof(struct mwifiex_ds_tdls_oper));

		if (sta_ptr->is_11n_enabled) {
			mwifiex_11n_cleanup_reorder_tbl(priv);
			spin_lock_irqsave(&priv->wmm.ra_list_spinlock,
					  flags);
			mwifiex_11n_delete_all_tx_ba_stream_tbl(priv);
			spin_unlock_irqrestore(&priv->wmm.ra_list_spinlock,
					       flags);
		}

		mwifiex_restore_tdls_packets(priv, sta_ptr->mac_addr,
					     TDLS_LINK_TEARDOWN);
		memcpy(&tdls_oper.peer_mac, sta_ptr->mac_addr, ETH_ALEN);
		tdls_oper.tdls_action = MWIFIEX_TDLS_DISABLE_LINK;
		if (mwifiex_send_cmd(priv, HostCmd_CMD_TDLS_OPER,
				     HostCmd_ACT_GEN_SET, 0, &tdls_oper, false))
			dev_warn(priv->adapter->dev,
				 "Disable link failed for TDLS peer %pM",
				 sta_ptr->mac_addr);
	}

	mwifiex_del_all_sta_list(priv);
}
