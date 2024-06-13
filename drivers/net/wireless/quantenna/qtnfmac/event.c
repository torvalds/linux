// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2015-2016 Quantenna Communications. All rights reserved. */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/nospec.h>

#include "cfg80211.h"
#include "core.h"
#include "qlink.h"
#include "bus.h"
#include "trans.h"
#include "util.h"
#include "event.h"
#include "qlink_util.h"

static int
qtnf_event_handle_sta_assoc(struct qtnf_wmac *mac, struct qtnf_vif *vif,
			    const struct qlink_event_sta_assoc *sta_assoc,
			    u16 len)
{
	const u8 *sta_addr;
	u16 frame_control;
	struct station_info *sinfo;
	size_t payload_len;
	u16 tlv_type;
	u16 tlv_value_len;
	const struct qlink_tlv_hdr *tlv;
	int ret = 0;

	if (unlikely(len < sizeof(*sta_assoc))) {
		pr_err("VIF%u.%u: payload is too short (%u < %zu)\n",
		       mac->macid, vif->vifid, len, sizeof(*sta_assoc));
		return -EINVAL;
	}

	if (vif->wdev.iftype != NL80211_IFTYPE_AP) {
		pr_err("VIF%u.%u: STA_ASSOC event when not in AP mode\n",
		       mac->macid, vif->vifid);
		return -EPROTO;
	}

	sinfo = kzalloc(sizeof(*sinfo), GFP_KERNEL);
	if (!sinfo)
		return -ENOMEM;

	sta_addr = sta_assoc->sta_addr;
	frame_control = le16_to_cpu(sta_assoc->frame_control);

	pr_debug("VIF%u.%u: MAC:%pM FC:%x\n", mac->macid, vif->vifid, sta_addr,
		 frame_control);

	qtnf_sta_list_add(vif, sta_addr);

	sinfo->assoc_req_ies = NULL;
	sinfo->assoc_req_ies_len = 0;
	sinfo->generation = vif->generation;

	payload_len = len - sizeof(*sta_assoc);

	qlink_for_each_tlv(tlv, sta_assoc->ies, payload_len) {
		tlv_type = le16_to_cpu(tlv->type);
		tlv_value_len = le16_to_cpu(tlv->len);

		if (tlv_type == QTN_TLV_ID_IE_SET) {
			const struct qlink_tlv_ie_set *ie_set;
			unsigned int ie_len;

			if (tlv_value_len <
			    (sizeof(*ie_set) - sizeof(ie_set->hdr))) {
				ret = -EINVAL;
				goto out;
			}

			ie_set = (const struct qlink_tlv_ie_set *)tlv;
			ie_len = tlv_value_len -
				(sizeof(*ie_set) - sizeof(ie_set->hdr));

			if (ie_set->type == QLINK_IE_SET_ASSOC_REQ && ie_len) {
				sinfo->assoc_req_ies = ie_set->ie_data;
				sinfo->assoc_req_ies_len = ie_len;
			}
		}
	}

	if (!qlink_tlv_parsing_ok(tlv, sta_assoc->ies, payload_len)) {
		pr_err("Malformed TLV buffer\n");
		ret = -EINVAL;
		goto out;
	}

	cfg80211_new_sta(vif->netdev, sta_assoc->sta_addr, sinfo,
			 GFP_KERNEL);

out:
	kfree(sinfo);
	return ret;
}

static int
qtnf_event_handle_sta_deauth(struct qtnf_wmac *mac, struct qtnf_vif *vif,
			     const struct qlink_event_sta_deauth *sta_deauth,
			     u16 len)
{
	const u8 *sta_addr;
	u16 reason;

	if (unlikely(len < sizeof(*sta_deauth))) {
		pr_err("VIF%u.%u: payload is too short (%u < %zu)\n",
		       mac->macid, vif->vifid, len,
		       sizeof(struct qlink_event_sta_deauth));
		return -EINVAL;
	}

	if (vif->wdev.iftype != NL80211_IFTYPE_AP) {
		pr_err("VIF%u.%u: STA_DEAUTH event when not in AP mode\n",
		       mac->macid, vif->vifid);
		return -EPROTO;
	}

	sta_addr = sta_deauth->sta_addr;
	reason = le16_to_cpu(sta_deauth->reason);

	pr_debug("VIF%u.%u: MAC:%pM reason:%x\n", mac->macid, vif->vifid,
		 sta_addr, reason);

	if (qtnf_sta_list_del(vif, sta_addr))
		cfg80211_del_sta(vif->netdev, sta_deauth->sta_addr,
				 GFP_KERNEL);

	return 0;
}

static int
qtnf_event_handle_bss_join(struct qtnf_vif *vif,
			   const struct qlink_event_bss_join *join_info,
			   u16 len)
{
	struct wiphy *wiphy = priv_to_wiphy(vif->mac);
	enum ieee80211_statuscode status = le16_to_cpu(join_info->status);
	struct cfg80211_chan_def chandef;
	struct cfg80211_bss *bss = NULL;
	u8 *ie = NULL;
	size_t payload_len;
	u16 tlv_type;
	u16 tlv_value_len;
	const struct qlink_tlv_hdr *tlv;
	const u8 *rsp_ies = NULL;
	size_t rsp_ies_len = 0;

	if (unlikely(len < sizeof(*join_info))) {
		pr_err("VIF%u.%u: payload is too short (%u < %zu)\n",
		       vif->mac->macid, vif->vifid, len,
		       sizeof(struct qlink_event_bss_join));
		return -EINVAL;
	}

	if (vif->wdev.iftype != NL80211_IFTYPE_STATION) {
		pr_err("VIF%u.%u: BSS_JOIN event when not in STA mode\n",
		       vif->mac->macid, vif->vifid);
		return -EPROTO;
	}

	pr_debug("VIF%u.%u: BSSID:%pM chan:%u status:%u\n",
		 vif->mac->macid, vif->vifid, join_info->bssid,
		 le16_to_cpu(join_info->chan.chan.center_freq), status);

	if (status != WLAN_STATUS_SUCCESS)
		goto done;

	qlink_chandef_q2cfg(wiphy, &join_info->chan, &chandef);
	if (!cfg80211_chandef_valid(&chandef)) {
		pr_warn("MAC%u.%u: bad channel freq=%u cf1=%u cf2=%u bw=%u\n",
			vif->mac->macid, vif->vifid,
			chandef.chan ? chandef.chan->center_freq : 0,
			chandef.center_freq1,
			chandef.center_freq2,
			chandef.width);
		status = WLAN_STATUS_UNSPECIFIED_FAILURE;
		goto done;
	}

	bss = cfg80211_get_bss(wiphy, chandef.chan, join_info->bssid,
			       NULL, 0, IEEE80211_BSS_TYPE_ESS,
			       IEEE80211_PRIVACY_ANY);
	if (!bss) {
		pr_warn("VIF%u.%u: add missing BSS:%pM chan:%u\n",
			vif->mac->macid, vif->vifid,
			join_info->bssid, chandef.chan->hw_value);

		if (!vif->wdev.u.client.ssid_len) {
			pr_warn("VIF%u.%u: SSID unknown for BSS:%pM\n",
				vif->mac->macid, vif->vifid,
				join_info->bssid);
			status = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto done;
		}

		ie = kzalloc(2 + vif->wdev.u.client.ssid_len, GFP_KERNEL);
		if (!ie) {
			pr_warn("VIF%u.%u: IE alloc failed for BSS:%pM\n",
				vif->mac->macid, vif->vifid,
				join_info->bssid);
			status = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto done;
		}

		ie[0] = WLAN_EID_SSID;
		ie[1] = vif->wdev.u.client.ssid_len;
		memcpy(ie + 2, vif->wdev.u.client.ssid,
		       vif->wdev.u.client.ssid_len);

		bss = cfg80211_inform_bss(wiphy, chandef.chan,
					  CFG80211_BSS_FTYPE_UNKNOWN,
					  join_info->bssid, 0,
					  WLAN_CAPABILITY_ESS, 100,
					  ie, 2 + vif->wdev.u.client.ssid_len,
					  0, GFP_KERNEL);
		if (!bss) {
			pr_warn("VIF%u.%u: can't connect to unknown BSS: %pM\n",
				vif->mac->macid, vif->vifid,
				join_info->bssid);
			status = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto done;
		}
	}

	payload_len = len - sizeof(*join_info);

	qlink_for_each_tlv(tlv, join_info->ies, payload_len) {
		tlv_type = le16_to_cpu(tlv->type);
		tlv_value_len = le16_to_cpu(tlv->len);

		if (tlv_type == QTN_TLV_ID_IE_SET) {
			const struct qlink_tlv_ie_set *ie_set;
			unsigned int ie_len;

			if (tlv_value_len <
			    (sizeof(*ie_set) - sizeof(ie_set->hdr))) {
				pr_warn("invalid IE_SET TLV\n");
				status = WLAN_STATUS_UNSPECIFIED_FAILURE;
				goto done;
			}

			ie_set = (const struct qlink_tlv_ie_set *)tlv;
			ie_len = tlv_value_len -
				(sizeof(*ie_set) - sizeof(ie_set->hdr));

			switch (ie_set->type) {
			case QLINK_IE_SET_ASSOC_RESP:
				if (ie_len) {
					rsp_ies = ie_set->ie_data;
					rsp_ies_len = ie_len;
				}
				break;
			default:
				pr_warn("unexpected IE type: %u\n",
					ie_set->type);
				break;
			}
		}
	}

	if (!qlink_tlv_parsing_ok(tlv, join_info->ies, payload_len))
		pr_warn("Malformed TLV buffer\n");
done:
	cfg80211_connect_result(vif->netdev, join_info->bssid, NULL, 0, rsp_ies,
				rsp_ies_len, status, GFP_KERNEL);
	if (bss) {
		if (!ether_addr_equal(vif->bssid, join_info->bssid))
			ether_addr_copy(vif->bssid, join_info->bssid);
		cfg80211_put_bss(wiphy, bss);
	}

	if (status == WLAN_STATUS_SUCCESS)
		netif_carrier_on(vif->netdev);

	kfree(ie);
	return 0;
}

static int
qtnf_event_handle_bss_leave(struct qtnf_vif *vif,
			    const struct qlink_event_bss_leave *leave_info,
			    u16 len)
{
	if (unlikely(len < sizeof(*leave_info))) {
		pr_err("VIF%u.%u: payload is too short (%u < %zu)\n",
		       vif->mac->macid, vif->vifid, len,
		       sizeof(struct qlink_event_bss_leave));
		return -EINVAL;
	}

	if (vif->wdev.iftype != NL80211_IFTYPE_STATION) {
		pr_err("VIF%u.%u: BSS_LEAVE event when not in STA mode\n",
		       vif->mac->macid, vif->vifid);
		return -EPROTO;
	}

	pr_debug("VIF%u.%u: disconnected\n", vif->mac->macid, vif->vifid);

	cfg80211_disconnected(vif->netdev, le16_to_cpu(leave_info->reason),
			      NULL, 0, 0, GFP_KERNEL);
	netif_carrier_off(vif->netdev);

	return 0;
}

static int
qtnf_event_handle_mgmt_received(struct qtnf_vif *vif,
				const struct qlink_event_rxmgmt *rxmgmt,
				u16 len)
{
	const size_t min_len = sizeof(*rxmgmt) +
			       sizeof(struct ieee80211_hdr_3addr);
	const struct ieee80211_hdr_3addr *frame = (void *)rxmgmt->frame_data;
	const u16 frame_len = len - sizeof(*rxmgmt);
	enum nl80211_rxmgmt_flags flags = 0;

	if (unlikely(len < min_len)) {
		pr_err("VIF%u.%u: payload is too short (%u < %zu)\n",
		       vif->mac->macid, vif->vifid, len, min_len);
		return -EINVAL;
	}

	if (le32_to_cpu(rxmgmt->flags) & QLINK_RXMGMT_FLAG_ANSWERED)
		flags |= NL80211_RXMGMT_FLAG_ANSWERED;

	pr_debug("%s LEN:%u FC:%.4X SA:%pM\n", vif->netdev->name, frame_len,
		 le16_to_cpu(frame->frame_control), frame->addr2);

	cfg80211_rx_mgmt(&vif->wdev, le32_to_cpu(rxmgmt->freq), rxmgmt->sig_dbm,
			 rxmgmt->frame_data, frame_len, flags);

	return 0;
}

static int
qtnf_event_handle_scan_results(struct qtnf_vif *vif,
			       const struct qlink_event_scan_result *sr,
			       u16 len)
{
	struct cfg80211_bss *bss;
	struct ieee80211_channel *channel;
	struct wiphy *wiphy = priv_to_wiphy(vif->mac);
	enum cfg80211_bss_frame_type frame_type = CFG80211_BSS_FTYPE_UNKNOWN;
	size_t payload_len;
	u16 tlv_type;
	u16 tlv_value_len;
	const struct qlink_tlv_hdr *tlv;
	const u8 *ies = NULL;
	size_t ies_len = 0;

	if (len < sizeof(*sr)) {
		pr_err("VIF%u.%u: payload is too short\n", vif->mac->macid,
		       vif->vifid);
		return -EINVAL;
	}

	channel = ieee80211_get_channel(wiphy, le16_to_cpu(sr->freq));
	if (!channel) {
		pr_err("VIF%u.%u: channel at %u MHz not found\n",
		       vif->mac->macid, vif->vifid, le16_to_cpu(sr->freq));
		return -EINVAL;
	}

	payload_len = len - sizeof(*sr);

	qlink_for_each_tlv(tlv, sr->payload, payload_len) {
		tlv_type = le16_to_cpu(tlv->type);
		tlv_value_len = le16_to_cpu(tlv->len);

		if (tlv_type == QTN_TLV_ID_IE_SET) {
			const struct qlink_tlv_ie_set *ie_set;
			unsigned int ie_len;

			if (tlv_value_len <
			    (sizeof(*ie_set) - sizeof(ie_set->hdr)))
				return -EINVAL;

			ie_set = (const struct qlink_tlv_ie_set *)tlv;
			ie_len = tlv_value_len -
				(sizeof(*ie_set) - sizeof(ie_set->hdr));

			switch (ie_set->type) {
			case QLINK_IE_SET_BEACON_IES:
				frame_type = CFG80211_BSS_FTYPE_BEACON;
				break;
			case QLINK_IE_SET_PROBE_RESP_IES:
				frame_type = CFG80211_BSS_FTYPE_PRESP;
				break;
			default:
				frame_type = CFG80211_BSS_FTYPE_UNKNOWN;
			}

			if (ie_len) {
				ies = ie_set->ie_data;
				ies_len = ie_len;
			}
		}
	}

	if (!qlink_tlv_parsing_ok(tlv, sr->payload, payload_len))
		return -EINVAL;

	bss = cfg80211_inform_bss(wiphy, channel, frame_type,
				  sr->bssid, get_unaligned_le64(&sr->tsf),
				  le16_to_cpu(sr->capab),
				  le16_to_cpu(sr->bintval), ies, ies_len,
				  DBM_TO_MBM(sr->sig_dbm), GFP_KERNEL);
	if (!bss)
		return -ENOMEM;

	cfg80211_put_bss(wiphy, bss);

	return 0;
}

static int
qtnf_event_handle_scan_complete(struct qtnf_wmac *mac,
				const struct qlink_event_scan_complete *status,
				u16 len)
{
	if (len < sizeof(*status)) {
		pr_err("MAC%u: payload is too short\n", mac->macid);
		return -EINVAL;
	}

	qtnf_scan_done(mac, le32_to_cpu(status->flags) & QLINK_SCAN_ABORTED);

	return 0;
}

static int
qtnf_event_handle_freq_change(struct qtnf_wmac *mac,
			      const struct qlink_event_freq_change *data,
			      u16 len)
{
	struct wiphy *wiphy = priv_to_wiphy(mac);
	struct cfg80211_chan_def chandef;
	struct qtnf_vif *vif;
	int i;

	if (len < sizeof(*data)) {
		pr_err("MAC%u: payload is too short\n", mac->macid);
		return -EINVAL;
	}

	if (!wiphy->registered)
		return 0;

	qlink_chandef_q2cfg(wiphy, &data->chan, &chandef);

	if (!cfg80211_chandef_valid(&chandef)) {
		pr_err("MAC%u: bad channel freq=%u cf1=%u cf2=%u bw=%u\n",
		       mac->macid, chandef.chan->center_freq,
		       chandef.center_freq1, chandef.center_freq2,
		       chandef.width);
		return -EINVAL;
	}

	pr_debug("MAC%d: new channel ieee=%u freq1=%u freq2=%u bw=%u\n",
		 mac->macid, chandef.chan->hw_value, chandef.center_freq1,
		 chandef.center_freq2, chandef.width);

	for (i = 0; i < QTNF_MAX_INTF; i++) {
		vif = &mac->iflist[i];

		if (vif->wdev.iftype == NL80211_IFTYPE_UNSPECIFIED)
			continue;

		if (vif->wdev.iftype == NL80211_IFTYPE_STATION &&
		    !vif->wdev.connected)
			continue;

		if (!vif->netdev)
			continue;

		mutex_lock(&vif->wdev.mtx);
		cfg80211_ch_switch_notify(vif->netdev, &chandef, 0, 0);
		mutex_unlock(&vif->wdev.mtx);
	}

	return 0;
}

static int qtnf_event_handle_radar(struct qtnf_vif *vif,
				   const struct qlink_event_radar *ev,
				   u16 len)
{
	struct wiphy *wiphy = priv_to_wiphy(vif->mac);
	struct cfg80211_chan_def chandef;

	if (len < sizeof(*ev)) {
		pr_err("MAC%u: payload is too short\n", vif->mac->macid);
		return -EINVAL;
	}

	if (!wiphy->registered || !vif->netdev)
		return 0;

	qlink_chandef_q2cfg(wiphy, &ev->chan, &chandef);

	if (!cfg80211_chandef_valid(&chandef)) {
		pr_err("MAC%u: bad channel f1=%u f2=%u bw=%u\n",
		       vif->mac->macid,
		       chandef.center_freq1, chandef.center_freq2,
		       chandef.width);
		return -EINVAL;
	}

	pr_info("%s: radar event=%u f1=%u f2=%u bw=%u\n",
		vif->netdev->name, ev->event,
		chandef.center_freq1, chandef.center_freq2,
		chandef.width);

	switch (ev->event) {
	case QLINK_RADAR_DETECTED:
		cfg80211_radar_event(wiphy, &chandef, GFP_KERNEL);
		break;
	case QLINK_RADAR_CAC_FINISHED:
		if (!vif->wdev.cac_started)
			break;

		cfg80211_cac_event(vif->netdev, &chandef,
				   NL80211_RADAR_CAC_FINISHED, GFP_KERNEL);
		break;
	case QLINK_RADAR_CAC_ABORTED:
		if (!vif->wdev.cac_started)
			break;

		cfg80211_cac_event(vif->netdev, &chandef,
				   NL80211_RADAR_CAC_ABORTED, GFP_KERNEL);
		break;
	case QLINK_RADAR_CAC_STARTED:
		if (vif->wdev.cac_started)
			break;

		if (!wiphy_ext_feature_isset(wiphy,
					     NL80211_EXT_FEATURE_DFS_OFFLOAD))
			break;

		cfg80211_cac_event(vif->netdev, &chandef,
				   NL80211_RADAR_CAC_STARTED, GFP_KERNEL);
		break;
	default:
		pr_warn("%s: unhandled radar event %u\n",
			vif->netdev->name, ev->event);
		break;
	}

	return 0;
}

static int
qtnf_event_handle_external_auth(struct qtnf_vif *vif,
				const struct qlink_event_external_auth *ev,
				u16 len)
{
	struct cfg80211_external_auth_params auth = {0};
	struct wiphy *wiphy = priv_to_wiphy(vif->mac);
	int ret;

	if (len < sizeof(*ev)) {
		pr_err("MAC%u: payload is too short\n", vif->mac->macid);
		return -EINVAL;
	}

	if (!wiphy->registered || !vif->netdev)
		return 0;

	if (ev->ssid_len) {
		int len = clamp_val(ev->ssid_len, 0, IEEE80211_MAX_SSID_LEN);

		memcpy(auth.ssid.ssid, ev->ssid, len);
		auth.ssid.ssid_len = len;
	}

	auth.key_mgmt_suite = le32_to_cpu(ev->akm_suite);
	ether_addr_copy(auth.bssid, ev->bssid);
	auth.action = ev->action;

	pr_debug("%s: external SAE processing: bss=%pM action=%u akm=%u\n",
		 vif->netdev->name, auth.bssid, auth.action,
		 auth.key_mgmt_suite);

	ret = cfg80211_external_auth_request(vif->netdev, &auth, GFP_KERNEL);
	if (ret)
		pr_warn("failed to offload external auth request\n");

	return ret;
}

static int
qtnf_event_handle_mic_failure(struct qtnf_vif *vif,
			      const struct qlink_event_mic_failure *mic_ev,
			      u16 len)
{
	struct wiphy *wiphy = priv_to_wiphy(vif->mac);
	u8 pairwise;

	if (len < sizeof(*mic_ev)) {
		pr_err("VIF%u.%u: payload is too short (%u < %zu)\n",
		       vif->mac->macid, vif->vifid, len,
		       sizeof(struct qlink_event_mic_failure));
		return -EINVAL;
	}

	if (!wiphy->registered || !vif->netdev)
		return 0;

	if (vif->wdev.iftype != NL80211_IFTYPE_STATION) {
		pr_err("VIF%u.%u: MIC_FAILURE event when not in STA mode\n",
		       vif->mac->macid, vif->vifid);
		return -EPROTO;
	}

	pairwise = mic_ev->pairwise ?
		NL80211_KEYTYPE_PAIRWISE : NL80211_KEYTYPE_GROUP;

	pr_info("%s: MIC error: src=%pM key_index=%u pairwise=%u\n",
		vif->netdev->name, mic_ev->src, mic_ev->key_index, pairwise);

	cfg80211_michael_mic_failure(vif->netdev, mic_ev->src, pairwise,
				     mic_ev->key_index, NULL, GFP_KERNEL);

	return 0;
}

static int
qtnf_event_handle_update_owe(struct qtnf_vif *vif,
			     const struct qlink_event_update_owe *owe_ev,
			     u16 len)
{
	struct wiphy *wiphy = priv_to_wiphy(vif->mac);
	struct cfg80211_update_owe_info owe_info = {};
	const u16 ie_len = len - sizeof(*owe_ev);
	u8 *ie;

	if (len < sizeof(*owe_ev)) {
		pr_err("VIF%u.%u: payload is too short (%u < %zu)\n",
		       vif->mac->macid, vif->vifid, len,
		       sizeof(struct qlink_event_update_owe));
		return -EINVAL;
	}

	if (!wiphy->registered || !vif->netdev)
		return 0;

	if (vif->wdev.iftype != NL80211_IFTYPE_AP) {
		pr_err("VIF%u.%u: UPDATE_OWE event when not in AP mode\n",
		       vif->mac->macid, vif->vifid);
		return -EPROTO;
	}

	ie = kzalloc(ie_len, GFP_KERNEL);
	if (!ie)
		return -ENOMEM;

	memcpy(owe_info.peer, owe_ev->peer, ETH_ALEN);
	memcpy(ie, owe_ev->ies, ie_len);
	owe_info.ie_len = ie_len;
	owe_info.ie = ie;
	owe_info.assoc_link_id = -1;

	pr_info("%s: external OWE processing: peer=%pM\n",
		vif->netdev->name, owe_ev->peer);

	cfg80211_update_owe_info_event(vif->netdev, &owe_info, GFP_KERNEL);
	kfree(ie);

	return 0;
}

static int qtnf_event_parse(struct qtnf_wmac *mac,
			    const struct sk_buff *event_skb)
{
	const struct qlink_event *event;
	struct qtnf_vif *vif = NULL;
	int ret = -1;
	u16 event_id;
	u16 event_len;
	u8 vifid;

	event = (const struct qlink_event *)event_skb->data;
	event_id = le16_to_cpu(event->event_id);
	event_len = le16_to_cpu(event->mhdr.len);

	if (event->vifid >= QTNF_MAX_INTF) {
		pr_err("invalid vif(%u)\n", event->vifid);
		return -EINVAL;
	}

	vifid = array_index_nospec(event->vifid, QTNF_MAX_INTF);
	vif = &mac->iflist[vifid];

	switch (event_id) {
	case QLINK_EVENT_STA_ASSOCIATED:
		ret = qtnf_event_handle_sta_assoc(mac, vif, (const void *)event,
						  event_len);
		break;
	case QLINK_EVENT_STA_DEAUTH:
		ret = qtnf_event_handle_sta_deauth(mac, vif,
						   (const void *)event,
						   event_len);
		break;
	case QLINK_EVENT_MGMT_RECEIVED:
		ret = qtnf_event_handle_mgmt_received(vif, (const void *)event,
						      event_len);
		break;
	case QLINK_EVENT_SCAN_RESULTS:
		ret = qtnf_event_handle_scan_results(vif, (const void *)event,
						     event_len);
		break;
	case QLINK_EVENT_SCAN_COMPLETE:
		ret = qtnf_event_handle_scan_complete(mac, (const void *)event,
						      event_len);
		break;
	case QLINK_EVENT_BSS_JOIN:
		ret = qtnf_event_handle_bss_join(vif, (const void *)event,
						 event_len);
		break;
	case QLINK_EVENT_BSS_LEAVE:
		ret = qtnf_event_handle_bss_leave(vif, (const void *)event,
						  event_len);
		break;
	case QLINK_EVENT_FREQ_CHANGE:
		ret = qtnf_event_handle_freq_change(mac, (const void *)event,
						    event_len);
		break;
	case QLINK_EVENT_RADAR:
		ret = qtnf_event_handle_radar(vif, (const void *)event,
					      event_len);
		break;
	case QLINK_EVENT_EXTERNAL_AUTH:
		ret = qtnf_event_handle_external_auth(vif, (const void *)event,
						      event_len);
		break;
	case QLINK_EVENT_MIC_FAILURE:
		ret = qtnf_event_handle_mic_failure(vif, (const void *)event,
						    event_len);
		break;
	case QLINK_EVENT_UPDATE_OWE:
		ret = qtnf_event_handle_update_owe(vif, (const void *)event,
						   event_len);
		break;
	default:
		pr_warn("unknown event type: %x\n", event_id);
		break;
	}

	return ret;
}

static int qtnf_event_process_skb(struct qtnf_bus *bus,
				  const struct sk_buff *skb)
{
	const struct qlink_event *event;
	struct qtnf_wmac *mac;
	int res;

	if (unlikely(!skb || skb->len < sizeof(*event))) {
		pr_err("invalid event buffer\n");
		return -EINVAL;
	}

	event = (struct qlink_event *)skb->data;

	mac = qtnf_core_get_mac(bus, event->macid);

	pr_debug("new event id:%x len:%u mac:%u vif:%u\n",
		 le16_to_cpu(event->event_id), le16_to_cpu(event->mhdr.len),
		 event->macid, event->vifid);

	if (unlikely(!mac))
		return -ENXIO;

	rtnl_lock();
	res = qtnf_event_parse(mac, skb);
	rtnl_unlock();

	return res;
}

void qtnf_event_work_handler(struct work_struct *work)
{
	struct qtnf_bus *bus = container_of(work, struct qtnf_bus, event_work);
	struct sk_buff_head *event_queue = &bus->trans.event_queue;
	struct sk_buff *current_event_skb = skb_dequeue(event_queue);

	while (current_event_skb) {
		qtnf_event_process_skb(bus, current_event_skb);
		dev_kfree_skb_any(current_event_skb);
		current_event_skb = skb_dequeue(event_queue);
	}
}
