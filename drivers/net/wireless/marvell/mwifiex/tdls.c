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
#define MWIFIEX_TDLS_WMM_INFO_SIZE 7

static void mwifiex_restore_tdls_packets(struct mwifiex_private *priv,
					 const u8 *mac, u8 status)
{
	struct mwifiex_ra_list_tbl *ra_list;
	struct list_head *tid_list;
	struct sk_buff *skb, *tmp;
	struct mwifiex_txinfo *tx_info;
	u32 tid;
	u8 tid_down;

	mwifiex_dbg(priv->adapter, DATA, "%s: %pM\n", __func__, mac);
	spin_lock_bh(&priv->wmm.ra_list_spinlock);

	skb_queue_walk_safe(&priv->tdls_txq, skb, tmp) {
		if (!ether_addr_equal(mac, skb->data))
			continue;

		__skb_unlink(skb, &priv->tdls_txq);
		tx_info = MWIFIEX_SKB_TXCB(skb);
		tid = skb->priority;
		tid_down = mwifiex_wmm_downgrade_tid(priv, tid);

		if (mwifiex_is_tdls_link_setup(status)) {
			ra_list = mwifiex_wmm_get_queue_raptr(priv, tid, mac);
			ra_list->tdls_link = true;
			tx_info->flags |= MWIFIEX_BUF_FLAG_TDLS_PKT;
		} else {
			tid_list = &priv->wmm.tid_tbl_ptr[tid_down].ra_list;
			ra_list = list_first_entry_or_null(tid_list,
					struct mwifiex_ra_list_tbl, list);
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

	spin_unlock_bh(&priv->wmm.ra_list_spinlock);
	return;
}

static void mwifiex_hold_tdls_packets(struct mwifiex_private *priv,
				      const u8 *mac)
{
	struct mwifiex_ra_list_tbl *ra_list;
	struct list_head *ra_list_head;
	struct sk_buff *skb, *tmp;
	int i;

	mwifiex_dbg(priv->adapter, DATA, "%s: %pM\n", __func__, mac);
	spin_lock_bh(&priv->wmm.ra_list_spinlock);

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

	spin_unlock_bh(&priv->wmm.ra_list_spinlock);
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
		mwifiex_dbg(priv->adapter, ERROR,
			    "Insufficient space while adding rates\n");
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
	pos = skb_put(skb, 4);
	*pos++ = WLAN_EID_AID;
	*pos++ = 2;
	memcpy(pos, &assoc_rsp->a_id, sizeof(assoc_rsp->a_id));

	return;
}

static int mwifiex_tdls_add_vht_capab(struct mwifiex_private *priv,
				      struct sk_buff *skb)
{
	struct ieee80211_vht_cap vht_cap;
	u8 *pos;

	pos = skb_put(skb, sizeof(struct ieee80211_vht_cap) + 2);
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
		mwifiex_dbg(priv->adapter, ERROR,
			    "TDLS peer station not found in list\n");
		return -1;
	}

	if (!(le16_to_cpu(sta_ptr->tdls_cap.ht_capb.cap_info))) {
		mwifiex_dbg(priv->adapter, WARN,
			    "TDLS peer doesn't support ht capabilities\n");
		return 0;
	}

	pos = skb_put(skb, sizeof(struct ieee80211_ht_operation) + 2);
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
		mwifiex_dbg(adapter, ERROR,
			    "TDLS peer station not found in list\n");
		return -1;
	}

	if (!(le32_to_cpu(sta_ptr->tdls_cap.vhtcap.vht_cap_info))) {
		mwifiex_dbg(adapter, WARN,
			    "TDLS peer doesn't support vht capabilities\n");
		return 0;
	}

	if (!mwifiex_is_bss_in_11ac_mode(priv)) {
		if (sta_ptr->tdls_cap.extcap.ext_capab[7] &
		   WLAN_EXT_CAPA8_TDLS_WIDE_BW_ENABLED) {
			mwifiex_dbg(adapter, WARN,
				    "TDLS peer doesn't support wider bandwidth\n");
			return 0;
		}
	} else {
		ap_vht_cap = bss_desc->bcn_vht_cap;
	}

	pos = skb_put(skb, sizeof(struct ieee80211_vht_operation) + 2);
	*pos++ = WLAN_EID_VHT_OPERATION;
	*pos++ = sizeof(struct ieee80211_vht_operation);
	vht_oper = (struct ieee80211_vht_operation *)pos;

	if (bss_desc->bss_band & BAND_A)
		usr_vht_cap_info = adapter->usr_dot_11ac_dev_cap_a;
	else
		usr_vht_cap_info = adapter->usr_dot_11ac_dev_cap_bg;

	/* find the minimum bandwidth between AP/TDLS peers */
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
	vht_oper->center_freq_seg0_idx =
			mwifiex_get_center_freq_index(priv, BAND_AAC,
						      bss_desc->channel,
						      chan_bw);

	return 0;
}

static void mwifiex_tdls_add_ext_capab(struct mwifiex_private *priv,
				       struct sk_buff *skb)
{
	struct ieee_types_extcap *extcap;

	extcap = skb_put(skb, sizeof(struct ieee_types_extcap));
	extcap->ieee_hdr.element_id = WLAN_EID_EXT_CAPABILITY;
	extcap->ieee_hdr.len = 8;
	memset(extcap->ext_capab, 0, 8);
	extcap->ext_capab[4] |= WLAN_EXT_CAPA5_TDLS_ENABLED;
	extcap->ext_capab[3] |= WLAN_EXT_CAPA4_TDLS_CHAN_SWITCH;

	if (priv->adapter->is_hw_11ac_capable)
		extcap->ext_capab[7] |= WLAN_EXT_CAPA8_TDLS_WIDE_BW_ENABLED;
}

static void mwifiex_tdls_add_qos_capab(struct sk_buff *skb)
{
	u8 *pos = skb_put(skb, 3);

	*pos++ = WLAN_EID_QOS_CAPA;
	*pos++ = 1;
	*pos++ = MWIFIEX_TDLS_DEF_QOS_CAPAB;
}

static void
mwifiex_tdls_add_wmm_param_ie(struct mwifiex_private *priv, struct sk_buff *skb)
{
	struct ieee80211_wmm_param_ie *wmm;
	u8 ac_vi[] = {0x42, 0x43, 0x5e, 0x00};
	u8 ac_vo[] = {0x62, 0x32, 0x2f, 0x00};
	u8 ac_be[] = {0x03, 0xa4, 0x00, 0x00};
	u8 ac_bk[] = {0x27, 0xa4, 0x00, 0x00};

	wmm = skb_put_zero(skb, sizeof(*wmm));

	wmm->element_id = WLAN_EID_VENDOR_SPECIFIC;
	wmm->len = sizeof(*wmm) - 2;
	wmm->oui[0] = 0x00; /* Microsoft OUI 00:50:F2 */
	wmm->oui[1] = 0x50;
	wmm->oui[2] = 0xf2;
	wmm->oui_type = 2; /* WME */
	wmm->oui_subtype = 1; /* WME param */
	wmm->version = 1; /* WME ver */
	wmm->qos_info = 0; /* U-APSD not in use */

	/* use default WMM AC parameters for TDLS link*/
	memcpy(&wmm->ac[0], ac_be, sizeof(ac_be));
	memcpy(&wmm->ac[1], ac_bk, sizeof(ac_bk));
	memcpy(&wmm->ac[2], ac_vi, sizeof(ac_vi));
	memcpy(&wmm->ac[3], ac_vo, sizeof(ac_vo));
}

static void
mwifiex_add_wmm_info_ie(struct mwifiex_private *priv, struct sk_buff *skb,
			u8 qosinfo)
{
	u8 *buf;

	buf = skb_put(skb,
		      MWIFIEX_TDLS_WMM_INFO_SIZE + sizeof(struct ieee_types_header));

	*buf++ = WLAN_EID_VENDOR_SPECIFIC;
	*buf++ = 7; /* len */
	*buf++ = 0x00; /* Microsoft OUI 00:50:F2 */
	*buf++ = 0x50;
	*buf++ = 0xf2;
	*buf++ = 2; /* WME */
	*buf++ = 0; /* WME info */
	*buf++ = 1; /* WME ver */
	*buf++ = qosinfo; /* U-APSD no in use */
}

static void mwifiex_tdls_add_bss_co_2040(struct sk_buff *skb)
{
	struct ieee_types_bss_co_2040 *bssco;

	bssco = skb_put(skb, sizeof(struct ieee_types_bss_co_2040));
	bssco->ieee_hdr.element_id = WLAN_EID_BSS_COEX_2040;
	bssco->ieee_hdr.len = sizeof(struct ieee_types_bss_co_2040) -
			      sizeof(struct ieee_types_header);
	bssco->bss_2040co = 0x01;
}

static void mwifiex_tdls_add_supported_chan(struct sk_buff *skb)
{
	struct ieee_types_generic *supp_chan;
	u8 chan_supp[] = {1, 11};

	supp_chan = skb_put(skb,
			    (sizeof(struct ieee_types_header) + sizeof(chan_supp)));
	supp_chan->ieee_hdr.element_id = WLAN_EID_SUPPORTED_CHANNELS;
	supp_chan->ieee_hdr.len = sizeof(chan_supp);
	memcpy(supp_chan->data, chan_supp, sizeof(chan_supp));
}

static void mwifiex_tdls_add_oper_class(struct sk_buff *skb)
{
	struct ieee_types_generic *reg_class;
	u8 rc_list[] = {1,
		1, 2, 3, 4, 12, 22, 23, 24, 25, 27, 28, 29, 30, 32, 33};
	reg_class = skb_put(skb,
			    (sizeof(struct ieee_types_header) + sizeof(rc_list)));
	reg_class->ieee_hdr.element_id = WLAN_EID_SUPPORTED_REGULATORY_CLASSES;
	reg_class->ieee_hdr.len = sizeof(rc_list);
	memcpy(reg_class->data, rc_list, sizeof(rc_list));
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

	tf = skb_put(skb, offsetof(struct ieee80211_tdls_data, u));
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

		pos = skb_put(skb, sizeof(struct ieee80211_ht_cap) + 2);
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
		mwifiex_tdls_add_bss_co_2040(skb);
		mwifiex_tdls_add_supported_chan(skb);
		mwifiex_tdls_add_oper_class(skb);
		mwifiex_add_wmm_info_ie(priv, skb, 0);
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

		pos = skb_put(skb, sizeof(struct ieee80211_ht_cap) + 2);
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
		mwifiex_tdls_add_bss_co_2040(skb);
		mwifiex_tdls_add_supported_chan(skb);
		mwifiex_tdls_add_oper_class(skb);
		mwifiex_add_wmm_info_ie(priv, skb, 0);
		break;

	case WLAN_TDLS_SETUP_CONFIRM:
		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_SETUP_CONFIRM;
		skb_put(skb, sizeof(tf->u.setup_cfm));
		tf->u.setup_cfm.status_code = cpu_to_le16(status_code);
		tf->u.setup_cfm.dialog_token = dialog_token;

		mwifiex_tdls_add_wmm_param_ie(priv, skb);
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
		mwifiex_dbg(priv->adapter, ERROR, "Unknown TDLS frame type.\n");
		return -EINVAL;
	}

	return 0;
}

static void
mwifiex_tdls_add_link_ie(struct sk_buff *skb, const u8 *src_addr,
			 const u8 *peer, const u8 *bssid)
{
	struct ieee80211_tdls_lnkie *lnkid;

	lnkid = skb_put(skb, sizeof(struct ieee80211_tdls_lnkie));
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
		  (2 * (sizeof(struct ieee_types_header))) +
		   MWIFIEX_SUPPORTED_CHANNELS +
		   MWIFIEX_OPERATING_CLASSES +
		  sizeof(struct ieee80211_wmm_param_ie) +
		  extra_ies_len;

	if (priv->adapter->is_hw_11ac_capable)
		skb_len += sizeof(struct ieee_types_vht_cap) +
			   sizeof(struct ieee_types_vht_oper) +
			   sizeof(struct ieee_types_aid);

	skb = dev_alloc_skb(skb_len);
	if (!skb) {
		mwifiex_dbg(priv->adapter, ERROR,
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
			skb_put_data(skb, extra_ies, extra_ies_len);
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
			skb_put_data(skb, extra_ies, extra_ies_len);
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
	memset(tx_info, 0, sizeof(*tx_info));
	tx_info->bss_num = priv->bss_num;
	tx_info->bss_type = priv->bss_type;

	__net_timestamp(skb);
	mwifiex_queue_tx_pkt(priv, skb);

	/* Delay 10ms to make sure tdls setup confirm/teardown frame
	 * is received by peer
	*/
	if (action_code == WLAN_TDLS_SETUP_CONFIRM ||
	    action_code == WLAN_TDLS_TEARDOWN)
		msleep_interruptible(10);

	return 0;
}

static int
mwifiex_construct_tdls_action_frame(struct mwifiex_private *priv,
				    const u8 *peer,
				    u8 action_code, u8 dialog_token,
				    u16 status_code, struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt;
	int ret;
	u16 capab;
	struct ieee80211_ht_cap *ht_cap;
	u8 radio, *pos;

	capab = priv->curr_bss_params.bss_descriptor.cap_info_bitmap;

	mgmt = skb_put(skb, offsetof(struct ieee80211_mgmt, u));

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
		eth_broadcast_addr(pos);

		ret = mwifiex_tdls_append_rates_ie(priv, skb);
		if (ret) {
			dev_kfree_skb_any(skb);
			return ret;
		}

		pos = skb_put(skb, sizeof(struct ieee80211_ht_cap) + 2);
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
		mwifiex_tdls_add_bss_co_2040(skb);
		mwifiex_tdls_add_supported_chan(skb);
		mwifiex_tdls_add_qos_capab(skb);
		mwifiex_tdls_add_oper_class(skb);
		break;
	default:
		mwifiex_dbg(priv->adapter, ERROR, "Unknown TDLS action frame type\n");
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
		mwifiex_dbg(priv->adapter, ERROR,
			    "allocate skb failed for management frame\n");
		return -ENOMEM;
	}

	skb_reserve(skb, MWIFIEX_MIN_DATA_HEADER_LEN);

	pkt_type = PKT_TYPE_MGMT;
	tx_control = 0;
	pos = skb_put_zero(skb,
			   MWIFIEX_MGMT_FRAME_HEADER_SIZE + sizeof(pkt_len));
	memcpy(pos, &pkt_type, sizeof(pkt_type));
	memcpy(pos + sizeof(pkt_type), &tx_control, sizeof(tx_control));

	if (mwifiex_construct_tdls_action_frame(priv, peer, action_code,
						dialog_token, status_code,
						skb)) {
		dev_kfree_skb_any(skb);
		return -EINVAL;
	}

	if (extra_ies_len)
		skb_put_data(skb, extra_ies, extra_ies_len);

	/* the TDLS link IE is always added last we are the responder */

	mwifiex_tdls_add_link_ie(skb, peer, priv->curr_addr,
				 priv->cfg_bssid);

	skb->priority = MWIFIEX_PRIO_VI;

	tx_info = MWIFIEX_SKB_TXCB(skb);
	memset(tx_info, 0, sizeof(*tx_info));
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
	u16 cap = 0;
	int ie_len = 0;

	if (len < (sizeof(struct ethhdr) + 3))
		return;
	if (*(buf + sizeof(struct ethhdr)) != WLAN_TDLS_SNAP_RFTYPE)
		return;
	if (*(buf + sizeof(struct ethhdr) + 1) != WLAN_CATEGORY_TDLS)
		return;

	peer = buf + ETH_ALEN;
	action = *(buf + sizeof(struct ethhdr) + 2);
	mwifiex_dbg(priv->adapter, DATA,
		    "rx:tdls action: peer=%pM, action=%d\n", peer, action);

	switch (action) {
	case WLAN_TDLS_SETUP_REQUEST:
		if (len < (sizeof(struct ethhdr) + TDLS_REQ_FIX_LEN))
			return;

		pos = buf + sizeof(struct ethhdr) + 4;
		/* payload 1+ category 1 + action 1 + dialog 1 */
		cap = get_unaligned_le16(pos);
		ie_len = len - sizeof(struct ethhdr) - TDLS_REQ_FIX_LEN;
		pos += 2;
		break;

	case WLAN_TDLS_SETUP_RESPONSE:
		if (len < (sizeof(struct ethhdr) + TDLS_RESP_FIX_LEN))
			return;
		/* payload 1+ category 1 + action 1 + dialog 1 + status code 2*/
		pos = buf + sizeof(struct ethhdr) + 6;
		cap = get_unaligned_le16(pos);
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
		mwifiex_dbg(priv->adapter, ERROR, "Unknown TDLS frame type.\n");
		return;
	}

	sta_ptr = mwifiex_add_sta_entry(priv, peer);
	if (!sta_ptr)
		return;

	sta_ptr->tdls_cap.capab = cpu_to_le16(cap);

	for (end = pos + ie_len; pos + 1 < end; pos += 2 + pos[1]) {
		if (pos + 2 + pos[1] > end)
			break;

		switch (*pos) {
		case WLAN_EID_SUPP_RATES:
			if (pos[1] > 32)
				return;
			sta_ptr->tdls_cap.rates_len = pos[1];
			for (i = 0; i < pos[1]; i++)
				sta_ptr->tdls_cap.rates[i] = pos[i + 2];
			break;

		case WLAN_EID_EXT_SUPP_RATES:
			if (pos[1] > 32)
				return;
			basic = sta_ptr->tdls_cap.rates_len;
			if (pos[1] > 32 - basic)
				return;
			for (i = 0; i < pos[1]; i++)
				sta_ptr->tdls_cap.rates[basic + i] = pos[i + 2];
			sta_ptr->tdls_cap.rates_len += pos[1];
			break;
		case WLAN_EID_HT_CAPABILITY:
			if (pos > end - sizeof(struct ieee80211_ht_cap) - 2)
				return;
			if (pos[1] != sizeof(struct ieee80211_ht_cap))
				return;
			/* copy the ie's value into ht_capb*/
			memcpy((u8 *)&sta_ptr->tdls_cap.ht_capb, pos + 2,
			       sizeof(struct ieee80211_ht_cap));
			sta_ptr->is_11n_enabled = 1;
			break;
		case WLAN_EID_HT_OPERATION:
			if (pos > end -
			    sizeof(struct ieee80211_ht_operation) - 2)
				return;
			if (pos[1] != sizeof(struct ieee80211_ht_operation))
				return;
			/* copy the ie's value into ht_oper*/
			memcpy(&sta_ptr->tdls_cap.ht_oper, pos + 2,
			       sizeof(struct ieee80211_ht_operation));
			break;
		case WLAN_EID_BSS_COEX_2040:
			if (pos > end - 3)
				return;
			if (pos[1] != 1)
				return;
			sta_ptr->tdls_cap.coex_2040 = pos[2];
			break;
		case WLAN_EID_EXT_CAPABILITY:
			if (pos > end - sizeof(struct ieee_types_header))
				return;
			if (pos[1] < sizeof(struct ieee_types_header))
				return;
			if (pos[1] > 8)
				return;
			memcpy((u8 *)&sta_ptr->tdls_cap.extcap, pos,
			       sizeof(struct ieee_types_header) +
			       min_t(u8, pos[1], 8));
			break;
		case WLAN_EID_RSN:
			if (pos > end - sizeof(struct ieee_types_header))
				return;
			if (pos[1] < sizeof(struct ieee_types_header))
				return;
			if (pos[1] > IEEE_MAX_IE_SIZE -
			    sizeof(struct ieee_types_header))
				return;
			memcpy((u8 *)&sta_ptr->tdls_cap.rsn_ie, pos,
			       sizeof(struct ieee_types_header) +
			       min_t(u8, pos[1], IEEE_MAX_IE_SIZE -
				     sizeof(struct ieee_types_header)));
			break;
		case WLAN_EID_QOS_CAPA:
			if (pos > end - 3)
				return;
			if (pos[1] != 1)
				return;
			sta_ptr->tdls_cap.qos_info = pos[2];
			break;
		case WLAN_EID_VHT_OPERATION:
			if (priv->adapter->is_hw_11ac_capable) {
				if (pos > end -
				    sizeof(struct ieee80211_vht_operation) - 2)
					return;
				if (pos[1] !=
				    sizeof(struct ieee80211_vht_operation))
					return;
				/* copy the ie's value into vhtoper*/
				memcpy(&sta_ptr->tdls_cap.vhtoper, pos + 2,
				       sizeof(struct ieee80211_vht_operation));
			}
			break;
		case WLAN_EID_VHT_CAPABILITY:
			if (priv->adapter->is_hw_11ac_capable) {
				if (pos > end -
				    sizeof(struct ieee80211_vht_cap) - 2)
					return;
				if (pos[1] != sizeof(struct ieee80211_vht_cap))
					return;
				/* copy the ie's value into vhtcap*/
				memcpy((u8 *)&sta_ptr->tdls_cap.vhtcap, pos + 2,
				       sizeof(struct ieee80211_vht_cap));
				sta_ptr->is_11ac_enabled = 1;
			}
			break;
		case WLAN_EID_AID:
			if (priv->adapter->is_hw_11ac_capable) {
				if (pos > end - 4)
					return;
				if (pos[1] != 2)
					return;
				sta_ptr->tdls_cap.aid =
					get_unaligned_le16((pos + 2));
			}
			break;
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
		mwifiex_dbg(priv->adapter, ERROR,
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
		mwifiex_dbg(priv->adapter, WARN,
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

	memset(&tdls_oper, 0, sizeof(struct mwifiex_ds_tdls_oper));
	sta_ptr = mwifiex_get_sta_entry(priv, peer);

	if (sta_ptr) {
		if (sta_ptr->is_11n_enabled) {
			mwifiex_11n_cleanup_reorder_tbl(priv);
			spin_lock_bh(&priv->wmm.ra_list_spinlock);
			mwifiex_11n_delete_all_tx_ba_stream_tbl(priv);
			spin_unlock_bh(&priv->wmm.ra_list_spinlock);
		}
		mwifiex_del_sta_entry(priv, peer);
	}

	mwifiex_restore_tdls_packets(priv, peer, TDLS_LINK_TEARDOWN);
	mwifiex_auto_tdls_update_peer_status(priv, peer, TDLS_NOT_SETUP);
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
	int i;

	sta_ptr = mwifiex_get_sta_entry(priv, peer);

	if (sta_ptr && (sta_ptr->tdls_status != TDLS_SETUP_FAILURE)) {
		mwifiex_dbg(priv->adapter, MSG,
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
		if (sta_ptr->tdls_cap.extcap.ext_capab[3] &
		    WLAN_EXT_CAPA4_TDLS_CHAN_SWITCH) {
			mwifiex_config_tdls_enable(priv);
			mwifiex_config_tdls_cs_params(priv);
		}

		memset(sta_ptr->rx_seq, 0xff, sizeof(sta_ptr->rx_seq));
		mwifiex_restore_tdls_packets(priv, peer, TDLS_SETUP_COMPLETE);
		mwifiex_auto_tdls_update_peer_status(priv, peer,
						     TDLS_SETUP_COMPLETE);
	} else {
		mwifiex_dbg(priv->adapter, ERROR,
			    "tdls: enable link %pM failed\n", peer);
		if (sta_ptr) {
			mwifiex_11n_cleanup_reorder_tbl(priv);
			spin_lock_bh(&priv->wmm.ra_list_spinlock);
			mwifiex_11n_delete_all_tx_ba_stream_tbl(priv);
			spin_unlock_bh(&priv->wmm.ra_list_spinlock);
			mwifiex_del_sta_entry(priv, peer);
		}
		mwifiex_restore_tdls_packets(priv, peer, TDLS_LINK_TEARDOWN);
		mwifiex_auto_tdls_update_peer_status(priv, peer,
						     TDLS_NOT_SETUP);

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

int mwifiex_get_tdls_list(struct mwifiex_private *priv,
			  struct tdls_peer_info *buf)
{
	struct mwifiex_sta_node *sta_ptr;
	struct tdls_peer_info *peer = buf;
	int count = 0;

	if (!ISSUPP_TDLS_ENABLED(priv->adapter->fw_cap_info))
		return 0;

	/* make sure we are in station mode and connected */
	if (!(priv->bss_type == MWIFIEX_BSS_TYPE_STA && priv->media_connected))
		return 0;

	spin_lock_bh(&priv->sta_list_spinlock);
	list_for_each_entry(sta_ptr, &priv->sta_list, list) {
		if (mwifiex_is_tdls_link_setup(sta_ptr->tdls_status)) {
			ether_addr_copy(peer->peer_addr, sta_ptr->mac_addr);
			peer++;
			count++;
			if (count >= MWIFIEX_MAX_TDLS_PEER_SUPPORTED)
				break;
		}
	}
	spin_unlock_bh(&priv->sta_list_spinlock);

	return count;
}

void mwifiex_disable_all_tdls_links(struct mwifiex_private *priv)
{
	struct mwifiex_sta_node *sta_ptr;
	struct mwifiex_ds_tdls_oper tdls_oper;

	if (list_empty(&priv->sta_list))
		return;

	list_for_each_entry(sta_ptr, &priv->sta_list, list) {
		memset(&tdls_oper, 0, sizeof(struct mwifiex_ds_tdls_oper));

		if (sta_ptr->is_11n_enabled) {
			mwifiex_11n_cleanup_reorder_tbl(priv);
			spin_lock_bh(&priv->wmm.ra_list_spinlock);
			mwifiex_11n_delete_all_tx_ba_stream_tbl(priv);
			spin_unlock_bh(&priv->wmm.ra_list_spinlock);
		}

		mwifiex_restore_tdls_packets(priv, sta_ptr->mac_addr,
					     TDLS_LINK_TEARDOWN);
		memcpy(&tdls_oper.peer_mac, sta_ptr->mac_addr, ETH_ALEN);
		tdls_oper.tdls_action = MWIFIEX_TDLS_DISABLE_LINK;
		if (mwifiex_send_cmd(priv, HostCmd_CMD_TDLS_OPER,
				     HostCmd_ACT_GEN_SET, 0, &tdls_oper, false))
			mwifiex_dbg(priv->adapter, ERROR,
				    "Disable link failed for TDLS peer %pM",
				    sta_ptr->mac_addr);
	}

	mwifiex_del_all_sta_list(priv);
}

int mwifiex_tdls_check_tx(struct mwifiex_private *priv, struct sk_buff *skb)
{
	struct mwifiex_auto_tdls_peer *peer;
	u8 mac[ETH_ALEN];

	ether_addr_copy(mac, skb->data);

	spin_lock_bh(&priv->auto_tdls_lock);
	list_for_each_entry(peer, &priv->auto_tdls_list, list) {
		if (!memcmp(mac, peer->mac_addr, ETH_ALEN)) {
			if (peer->rssi <= MWIFIEX_TDLS_RSSI_HIGH &&
			    peer->tdls_status == TDLS_NOT_SETUP &&
			    (peer->failure_count <
			     MWIFIEX_TDLS_MAX_FAIL_COUNT)) {
				peer->tdls_status = TDLS_SETUP_INPROGRESS;
				mwifiex_dbg(priv->adapter, INFO,
					    "setup TDLS link, peer=%pM rssi=%d\n",
					    peer->mac_addr, peer->rssi);

				cfg80211_tdls_oper_request(priv->netdev,
							   peer->mac_addr,
							   NL80211_TDLS_SETUP,
							   0, GFP_ATOMIC);
				peer->do_setup = false;
				priv->check_tdls_tx = false;
			} else if (peer->failure_count <
				   MWIFIEX_TDLS_MAX_FAIL_COUNT &&
				   peer->do_discover) {
				mwifiex_send_tdls_data_frame(priv,
							     peer->mac_addr,
						    WLAN_TDLS_DISCOVERY_REQUEST,
							     1, 0, NULL, 0);
				peer->do_discover = false;
			}
		}
	}
	spin_unlock_bh(&priv->auto_tdls_lock);

	return 0;
}

void mwifiex_flush_auto_tdls_list(struct mwifiex_private *priv)
{
	struct mwifiex_auto_tdls_peer *peer, *tmp_node;

	spin_lock_bh(&priv->auto_tdls_lock);
	list_for_each_entry_safe(peer, tmp_node, &priv->auto_tdls_list, list) {
		list_del(&peer->list);
		kfree(peer);
	}

	INIT_LIST_HEAD(&priv->auto_tdls_list);
	spin_unlock_bh(&priv->auto_tdls_lock);
	priv->check_tdls_tx = false;
}

void mwifiex_add_auto_tdls_peer(struct mwifiex_private *priv, const u8 *mac)
{
	struct mwifiex_auto_tdls_peer *tdls_peer;

	if (!priv->adapter->auto_tdls)
		return;

	spin_lock_bh(&priv->auto_tdls_lock);
	list_for_each_entry(tdls_peer, &priv->auto_tdls_list, list) {
		if (!memcmp(tdls_peer->mac_addr, mac, ETH_ALEN)) {
			tdls_peer->tdls_status = TDLS_SETUP_INPROGRESS;
			tdls_peer->rssi_jiffies = jiffies;
			spin_unlock_bh(&priv->auto_tdls_lock);
			return;
		}
	}

	/* create new TDLS peer */
	tdls_peer = kzalloc(sizeof(*tdls_peer), GFP_ATOMIC);
	if (tdls_peer) {
		ether_addr_copy(tdls_peer->mac_addr, mac);
		tdls_peer->tdls_status = TDLS_SETUP_INPROGRESS;
		tdls_peer->rssi_jiffies = jiffies;
		INIT_LIST_HEAD(&tdls_peer->list);
		list_add_tail(&tdls_peer->list, &priv->auto_tdls_list);
		mwifiex_dbg(priv->adapter, INFO,
			    "Add auto TDLS peer= %pM to list\n", mac);
	}

	spin_unlock_bh(&priv->auto_tdls_lock);
}

void mwifiex_auto_tdls_update_peer_status(struct mwifiex_private *priv,
					  const u8 *mac, u8 link_status)
{
	struct mwifiex_auto_tdls_peer *peer;

	if (!priv->adapter->auto_tdls)
		return;

	spin_lock_bh(&priv->auto_tdls_lock);
	list_for_each_entry(peer, &priv->auto_tdls_list, list) {
		if (!memcmp(peer->mac_addr, mac, ETH_ALEN)) {
			if ((link_status == TDLS_NOT_SETUP) &&
			    (peer->tdls_status == TDLS_SETUP_INPROGRESS))
				peer->failure_count++;
			else if (mwifiex_is_tdls_link_setup(link_status))
				peer->failure_count = 0;

			peer->tdls_status = link_status;
			break;
		}
	}
	spin_unlock_bh(&priv->auto_tdls_lock);
}

void mwifiex_auto_tdls_update_peer_signal(struct mwifiex_private *priv,
					  u8 *mac, s8 snr, s8 nflr)
{
	struct mwifiex_auto_tdls_peer *peer;

	if (!priv->adapter->auto_tdls)
		return;

	spin_lock_bh(&priv->auto_tdls_lock);
	list_for_each_entry(peer, &priv->auto_tdls_list, list) {
		if (!memcmp(peer->mac_addr, mac, ETH_ALEN)) {
			peer->rssi = nflr - snr;
			peer->rssi_jiffies = jiffies;
			break;
		}
	}
	spin_unlock_bh(&priv->auto_tdls_lock);
}

void mwifiex_check_auto_tdls(struct timer_list *t)
{
	struct mwifiex_private *priv = from_timer(priv, t, auto_tdls_timer);
	struct mwifiex_auto_tdls_peer *tdls_peer;
	u16 reason = WLAN_REASON_TDLS_TEARDOWN_UNSPECIFIED;

	if (WARN_ON_ONCE(!priv || !priv->adapter)) {
		pr_err("mwifiex: %s: adapter or private structure is NULL\n",
		       __func__);
		return;
	}

	if (unlikely(!priv->adapter->auto_tdls))
		return;

	if (!priv->auto_tdls_timer_active) {
		mwifiex_dbg(priv->adapter, INFO,
			    "auto TDLS timer inactive; return");
		return;
	}

	priv->check_tdls_tx = false;

	spin_lock_bh(&priv->auto_tdls_lock);
	list_for_each_entry(tdls_peer, &priv->auto_tdls_list, list) {
		if ((jiffies - tdls_peer->rssi_jiffies) >
		    (MWIFIEX_AUTO_TDLS_IDLE_TIME * HZ)) {
			tdls_peer->rssi = 0;
			tdls_peer->do_discover = true;
			priv->check_tdls_tx = true;
		}

		if (((tdls_peer->rssi >= MWIFIEX_TDLS_RSSI_LOW) ||
		     !tdls_peer->rssi) &&
		    mwifiex_is_tdls_link_setup(tdls_peer->tdls_status)) {
			tdls_peer->tdls_status = TDLS_LINK_TEARDOWN;
			mwifiex_dbg(priv->adapter, MSG,
				    "teardown TDLS link,peer=%pM rssi=%d\n",
				    tdls_peer->mac_addr, -tdls_peer->rssi);
			tdls_peer->do_discover = true;
			priv->check_tdls_tx = true;
			cfg80211_tdls_oper_request(priv->netdev,
						   tdls_peer->mac_addr,
						   NL80211_TDLS_TEARDOWN,
						   reason, GFP_ATOMIC);
		} else if (tdls_peer->rssi &&
			   tdls_peer->rssi <= MWIFIEX_TDLS_RSSI_HIGH &&
			   tdls_peer->tdls_status == TDLS_NOT_SETUP &&
			   tdls_peer->failure_count <
			   MWIFIEX_TDLS_MAX_FAIL_COUNT) {
				priv->check_tdls_tx = true;
				tdls_peer->do_setup = true;
				mwifiex_dbg(priv->adapter, INFO,
					    "check TDLS with peer=%pM\t"
					    "rssi=%d\n", tdls_peer->mac_addr,
					    tdls_peer->rssi);
		}
	}
	spin_unlock_bh(&priv->auto_tdls_lock);

	mod_timer(&priv->auto_tdls_timer,
		  jiffies + msecs_to_jiffies(MWIFIEX_TIMER_10S));
}

void mwifiex_setup_auto_tdls_timer(struct mwifiex_private *priv)
{
	timer_setup(&priv->auto_tdls_timer, mwifiex_check_auto_tdls, 0);
	priv->auto_tdls_timer_active = true;
	mod_timer(&priv->auto_tdls_timer,
		  jiffies + msecs_to_jiffies(MWIFIEX_TIMER_10S));
}

void mwifiex_clean_auto_tdls(struct mwifiex_private *priv)
{
	if (ISSUPP_TDLS_ENABLED(priv->adapter->fw_cap_info) &&
	    priv->adapter->auto_tdls &&
	    priv->bss_type == MWIFIEX_BSS_TYPE_STA) {
		priv->auto_tdls_timer_active = false;
		del_timer(&priv->auto_tdls_timer);
		mwifiex_flush_auto_tdls_list(priv);
	}
}

static int mwifiex_config_tdls(struct mwifiex_private *priv, u8 enable)
{
	struct mwifiex_tdls_config config;

	config.enable = cpu_to_le16(enable);
	return mwifiex_send_cmd(priv, HostCmd_CMD_TDLS_CONFIG,
				ACT_TDLS_CS_ENABLE_CONFIG, 0, &config, true);
}

int mwifiex_config_tdls_enable(struct mwifiex_private *priv)
{
	return mwifiex_config_tdls(priv, true);
}

int mwifiex_config_tdls_disable(struct mwifiex_private *priv)
{
	return mwifiex_config_tdls(priv, false);
}

int mwifiex_config_tdls_cs_params(struct mwifiex_private *priv)
{
	struct mwifiex_tdls_config_cs_params config_tdls_cs_params;

	config_tdls_cs_params.unit_time = MWIFIEX_DEF_CS_UNIT_TIME;
	config_tdls_cs_params.thr_otherlink = MWIFIEX_DEF_CS_THR_OTHERLINK;
	config_tdls_cs_params.thr_directlink = MWIFIEX_DEF_THR_DIRECTLINK;

	return mwifiex_send_cmd(priv, HostCmd_CMD_TDLS_CONFIG,
				ACT_TDLS_CS_PARAMS, 0,
				&config_tdls_cs_params, true);
}

int mwifiex_stop_tdls_cs(struct mwifiex_private *priv, const u8 *peer_mac)
{
	struct mwifiex_tdls_stop_cs_params stop_tdls_cs_params;

	ether_addr_copy(stop_tdls_cs_params.peer_mac, peer_mac);

	return mwifiex_send_cmd(priv, HostCmd_CMD_TDLS_CONFIG,
				ACT_TDLS_CS_STOP, 0,
				&stop_tdls_cs_params, true);
}

int mwifiex_start_tdls_cs(struct mwifiex_private *priv, const u8 *peer_mac,
			  u8 primary_chan, u8 second_chan_offset, u8 band)
{
	struct mwifiex_tdls_init_cs_params start_tdls_cs_params;

	ether_addr_copy(start_tdls_cs_params.peer_mac, peer_mac);
	start_tdls_cs_params.primary_chan = primary_chan;
	start_tdls_cs_params.second_chan_offset = second_chan_offset;
	start_tdls_cs_params.band = band;

	start_tdls_cs_params.switch_time = cpu_to_le16(MWIFIEX_DEF_CS_TIME);
	start_tdls_cs_params.switch_timeout =
					cpu_to_le16(MWIFIEX_DEF_CS_TIMEOUT);
	start_tdls_cs_params.reg_class = MWIFIEX_DEF_CS_REG_CLASS;
	start_tdls_cs_params.periodicity = MWIFIEX_DEF_CS_PERIODICITY;

	return mwifiex_send_cmd(priv, HostCmd_CMD_TDLS_CONFIG,
				ACT_TDLS_CS_INIT, 0,
				&start_tdls_cs_params, true);
}
