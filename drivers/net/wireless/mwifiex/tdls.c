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

#define TDLS_REQ_FIX_LEN      6
#define TDLS_RESP_FIX_LEN     8
#define TDLS_CONFIRM_FIX_LEN  6

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

static void mwifiex_tdls_add_ext_capab(struct sk_buff *skb)
{
	struct ieee_types_extcap *extcap;

	extcap = (void *)skb_put(skb, sizeof(struct ieee_types_extcap));
	extcap->ieee_hdr.element_id = WLAN_EID_EXT_CAPABILITY;
	extcap->ieee_hdr.len = 8;
	memset(extcap->ext_capab, 0, 8);
	extcap->ext_capab[4] |= WLAN_EXT_CAPA5_TDLS_ENABLED;
}

static void mwifiex_tdls_add_qos_capab(struct sk_buff *skb)
{
	u8 *pos = (void *)skb_put(skb, 3);

	*pos++ = WLAN_EID_QOS_CAPA;
	*pos++ = 1;
	*pos++ = MWIFIEX_TDLS_DEF_QOS_CAPAB;
}

static int mwifiex_prep_tdls_encap_data(struct mwifiex_private *priv,
			     u8 *peer, u8 action_code, u8 dialog_token,
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

		mwifiex_tdls_add_ext_capab(skb);
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

		mwifiex_tdls_add_ext_capab(skb);
		mwifiex_tdls_add_qos_capab(skb);
		break;

	case WLAN_TDLS_SETUP_CONFIRM:
		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_SETUP_CONFIRM;
		skb_put(skb, sizeof(tf->u.setup_cfm));
		tf->u.setup_cfm.status_code = cpu_to_le16(status_code);
		tf->u.setup_cfm.dialog_token = dialog_token;
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
mwifiex_tdls_add_link_ie(struct sk_buff *skb, u8 *src_addr, u8 *peer, u8 *bssid)
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

int mwifiex_send_tdls_data_frame(struct mwifiex_private *priv,
				 u8 *peer, u8 action_code, u8 dialog_token,
				 u16 status_code, const u8 *extra_ies,
				 size_t extra_ies_len)
{
	struct sk_buff *skb;
	struct mwifiex_txinfo *tx_info;
	struct timeval tv;
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

	do_gettimeofday(&tv);
	skb->tstamp = timeval_to_ktime(tv);
	mwifiex_queue_tx_pkt(priv, skb);

	return 0;
}

static int
mwifiex_construct_tdls_action_frame(struct mwifiex_private *priv, u8 *peer,
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

		mwifiex_tdls_add_ext_capab(skb);
		mwifiex_tdls_add_qos_capab(skb);
		break;
	default:
		dev_err(priv->adapter->dev, "Unknown TDLS action frame type\n");
		return -EINVAL;
	}

	return 0;
}

int mwifiex_send_tdls_action_frame(struct mwifiex_private *priv,
				 u8 *peer, u8 action_code, u8 dialog_token,
				 u16 status_code, const u8 *extra_ies,
				 size_t extra_ies_len)
{
	struct sk_buff *skb;
	struct mwifiex_txinfo *tx_info;
	struct timeval tv;
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
	do_gettimeofday(&tv);
	skb->tstamp = timeval_to_ktime(tv);
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
	if (*(u8 *)(buf + sizeof(struct ethhdr)) != WLAN_TDLS_SNAP_RFTYPE)
		return;
	if (*(u8 *)(buf + sizeof(struct ethhdr) + 1) != WLAN_CATEGORY_TDLS)
		return;

	peer = buf + ETH_ALEN;
	action = *(u8 *)(buf + sizeof(struct ethhdr) + 2);

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
		default:
			break;
		}
	}

	return;
}
