/*
 * hostapd / Callback functions for driver wrappers
 * Copyright (c) 2002-2009, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "radius/radius.h"
#include "drivers/driver.h"
#include "common/ieee802_11_defs.h"
#include "common/ieee802_11_common.h"
#include "common/wpa_ctrl.h"
#include "crypto/random.h"
#include "p2p/p2p.h"
#include "wps/wps.h"
#include "hostapd.h"
#include "ieee802_11.h"
#include "sta_info.h"
#include "accounting.h"
#include "tkip_countermeasures.h"
#include "iapp.h"
#include "ieee802_1x.h"
#include "wpa_auth.h"
#include "wmm.h"
#include "wps_hostapd.h"
#include "ap_drv_ops.h"
#include "ap_config.h"


int hostapd_notif_assoc(struct hostapd_data *hapd, const u8 *addr,
			const u8 *ie, size_t ielen, int reassoc)
{
	struct sta_info *sta;
	int new_assoc, res;
	struct ieee802_11_elems elems;
#ifdef CONFIG_P2P
	const u8 *all_ies = ie;
	size_t all_ies_len = ielen;
#endif /* CONFIG_P2P */

	if (addr == NULL) {
		/*
		 * This could potentially happen with unexpected event from the
		 * driver wrapper. This was seen at least in one case where the
		 * driver ended up being set to station mode while hostapd was
		 * running, so better make sure we stop processing such an
		 * event here.
		 */
		wpa_printf(MSG_DEBUG, "hostapd_notif_assoc: Skip event with "
			   "no address");
		return -1;
	}
	random_add_randomness(addr, ETH_ALEN);

	hostapd_logger(hapd, addr, HOSTAPD_MODULE_IEEE80211,
		       HOSTAPD_LEVEL_INFO, "associated");

	ieee802_11_parse_elems(ie, ielen, &elems, 0);
	if (elems.wps_ie) {
		ie = elems.wps_ie - 2;
		ielen = elems.wps_ie_len + 2;
		wpa_printf(MSG_DEBUG, "STA included WPS IE in (Re)AssocReq");
	} else if (elems.rsn_ie) {
		ie = elems.rsn_ie - 2;
		ielen = elems.rsn_ie_len + 2;
		wpa_printf(MSG_DEBUG, "STA included RSN IE in (Re)AssocReq");
	} else if (elems.wpa_ie) {
		ie = elems.wpa_ie - 2;
		ielen = elems.wpa_ie_len + 2;
		wpa_printf(MSG_DEBUG, "STA included WPA IE in (Re)AssocReq");
	} else {
		ie = NULL;
		ielen = 0;
		wpa_printf(MSG_DEBUG, "STA did not include WPS/RSN/WPA IE in "
			   "(Re)AssocReq");
	}

	sta = ap_get_sta(hapd, addr);
	if (sta) {
		accounting_sta_stop(hapd, sta);
	} else {
		sta = ap_sta_add(hapd, addr);
		if (sta == NULL)
			return -1;
	}
	sta->flags &= ~(WLAN_STA_WPS | WLAN_STA_MAYBE_WPS);

#ifdef CONFIG_P2P
	if (elems.p2p) {
		wpabuf_free(sta->p2p_ie);
		sta->p2p_ie = ieee802_11_vendor_ie_concat(all_ies, all_ies_len,
							  P2P_IE_VENDOR_TYPE);
	}
#endif /* CONFIG_P2P */

	if (hapd->conf->wpa) {
		if (ie == NULL || ielen == 0) {
			if (hapd->conf->wps_state) {
				wpa_printf(MSG_DEBUG, "STA did not include "
					   "WPA/RSN IE in (Re)Association "
					   "Request - possible WPS use");
				sta->flags |= WLAN_STA_MAYBE_WPS;
				goto skip_wpa_check;
			}

			wpa_printf(MSG_DEBUG, "No WPA/RSN IE from STA");
			return -1;
		}
		if (hapd->conf->wps_state && ie[0] == 0xdd && ie[1] >= 4 &&
		    os_memcmp(ie + 2, "\x00\x50\xf2\x04", 4) == 0) {
			sta->flags |= WLAN_STA_WPS;
			goto skip_wpa_check;
		}

		if (sta->wpa_sm == NULL)
			sta->wpa_sm = wpa_auth_sta_init(hapd->wpa_auth,
							sta->addr);
		if (sta->wpa_sm == NULL) {
			wpa_printf(MSG_ERROR, "Failed to initialize WPA state "
				   "machine");
			return -1;
		}
		res = wpa_validate_wpa_ie(hapd->wpa_auth, sta->wpa_sm,
					  ie, ielen, NULL, 0);
		if (res != WPA_IE_OK) {
			int resp;
			wpa_printf(MSG_DEBUG, "WPA/RSN information element "
				   "rejected? (res %u)", res);
			wpa_hexdump(MSG_DEBUG, "IE", ie, ielen);
			if (res == WPA_INVALID_GROUP)
				resp = WLAN_REASON_GROUP_CIPHER_NOT_VALID;
			else if (res == WPA_INVALID_PAIRWISE)
				resp = WLAN_REASON_PAIRWISE_CIPHER_NOT_VALID;
			else if (res == WPA_INVALID_AKMP)
				resp = WLAN_REASON_AKMP_NOT_VALID;
#ifdef CONFIG_IEEE80211W
			else if (res == WPA_MGMT_FRAME_PROTECTION_VIOLATION)
				resp = WLAN_REASON_INVALID_IE;
			else if (res == WPA_INVALID_MGMT_GROUP_CIPHER)
				resp = WLAN_REASON_GROUP_CIPHER_NOT_VALID;
#endif /* CONFIG_IEEE80211W */
			else
				resp = WLAN_REASON_INVALID_IE;
			hostapd_drv_sta_disassoc(hapd, sta->addr, resp);
			ap_free_sta(hapd, sta);
			return -1;
		}
	} else if (hapd->conf->wps_state) {
#ifdef CONFIG_WPS_STRICT
		if (ie) {
			struct wpabuf *wps;
			wps = ieee802_11_vendor_ie_concat(ie, ielen,
							  WPS_IE_VENDOR_TYPE);
			if (wps && wps_validate_assoc_req(wps) < 0) {
				hostapd_drv_sta_disassoc(
					hapd, sta->addr,
					WLAN_REASON_INVALID_IE);
				ap_free_sta(hapd, sta);
				wpabuf_free(wps);
				return -1;
			}
			wpabuf_free(wps);
		}
#endif /* CONFIG_WPS_STRICT */
		if (ie && ielen > 4 && ie[0] == 0xdd && ie[1] >= 4 &&
		    os_memcmp(ie + 2, "\x00\x50\xf2\x04", 4) == 0) {
			sta->flags |= WLAN_STA_WPS;
		} else
			sta->flags |= WLAN_STA_MAYBE_WPS;
	}
skip_wpa_check:

	new_assoc = (sta->flags & WLAN_STA_ASSOC) == 0;
	sta->flags |= WLAN_STA_AUTH | WLAN_STA_ASSOC;
	wpa_auth_sm_event(sta->wpa_sm, WPA_ASSOC);

	hostapd_new_assoc_sta(hapd, sta, !new_assoc);

	ieee802_1x_notify_port_enabled(sta->eapol_sm, 1);

#ifdef CONFIG_P2P
	p2p_group_notif_assoc(hapd->p2p_group, sta->addr,
			      all_ies, all_ies_len);
#endif /* CONFIG_P2P */

	return 0;
}


void hostapd_notif_disassoc(struct hostapd_data *hapd, const u8 *addr)
{
	struct sta_info *sta;

	if (addr == NULL) {
		/*
		 * This could potentially happen with unexpected event from the
		 * driver wrapper. This was seen at least in one case where the
		 * driver ended up reporting a station mode event while hostapd
		 * was running, so better make sure we stop processing such an
		 * event here.
		 */
		wpa_printf(MSG_DEBUG, "hostapd_notif_disassoc: Skip event "
			   "with no address");
		return;
	}

	hostapd_logger(hapd, addr, HOSTAPD_MODULE_IEEE80211,
		       HOSTAPD_LEVEL_INFO, "disassociated");

	sta = ap_get_sta(hapd, addr);
	if (sta == NULL) {
		wpa_printf(MSG_DEBUG, "Disassociation notification for "
			   "unknown STA " MACSTR, MAC2STR(addr));
		return;
	}

	sta->flags &= ~(WLAN_STA_AUTH | WLAN_STA_ASSOC);
	wpa_msg(hapd->msg_ctx, MSG_INFO, AP_STA_DISCONNECTED MACSTR,
		MAC2STR(sta->addr));
	wpa_auth_sm_event(sta->wpa_sm, WPA_DISASSOC);
	sta->acct_terminate_cause = RADIUS_ACCT_TERMINATE_CAUSE_USER_REQUEST;
	ieee802_1x_notify_port_enabled(sta->eapol_sm, 0);
	ap_free_sta(hapd, sta);
}


void hostapd_event_sta_low_ack(struct hostapd_data *hapd, const u8 *addr)
{
	struct sta_info *sta = ap_get_sta(hapd, addr);

	if (!sta || !hapd->conf->disassoc_low_ack)
		return;

	hostapd_logger(hapd, addr, HOSTAPD_MODULE_IEEE80211,
		       HOSTAPD_LEVEL_INFO, "disconnected due to excessive "
		       "missing ACKs");
	hostapd_drv_sta_disassoc(hapd, addr, WLAN_REASON_DISASSOC_LOW_ACK);
	if (sta)
		ap_sta_disassociate(hapd, sta, WLAN_REASON_DISASSOC_LOW_ACK);
}


int hostapd_probe_req_rx(struct hostapd_data *hapd, const u8 *sa,
			 const u8 *ie, size_t ie_len)
{
	size_t i;
	int ret = 0;

	if (sa == NULL || ie == NULL)
		return -1;

	random_add_randomness(sa, ETH_ALEN);
	for (i = 0; hapd->probereq_cb && i < hapd->num_probereq_cb; i++) {
		if (hapd->probereq_cb[i].cb(hapd->probereq_cb[i].ctx,
					    sa, ie, ie_len) > 0) {
			ret = 1;
			break;
		}
	}
	return ret;
}


#ifdef HOSTAPD

#ifdef NEED_AP_MLME

static const u8 * get_hdr_bssid(const struct ieee80211_hdr *hdr, size_t len)
{
	u16 fc, type, stype;

	/*
	 * PS-Poll frames are 16 bytes. All other frames are
	 * 24 bytes or longer.
	 */
	if (len < 16)
		return NULL;

	fc = le_to_host16(hdr->frame_control);
	type = WLAN_FC_GET_TYPE(fc);
	stype = WLAN_FC_GET_STYPE(fc);

	switch (type) {
	case WLAN_FC_TYPE_DATA:
		if (len < 24)
			return NULL;
		switch (fc & (WLAN_FC_FROMDS | WLAN_FC_TODS)) {
		case WLAN_FC_FROMDS | WLAN_FC_TODS:
		case WLAN_FC_TODS:
			return hdr->addr1;
		case WLAN_FC_FROMDS:
			return hdr->addr2;
		default:
			return NULL;
		}
	case WLAN_FC_TYPE_CTRL:
		if (stype != WLAN_FC_STYPE_PSPOLL)
			return NULL;
		return hdr->addr1;
	case WLAN_FC_TYPE_MGMT:
		return hdr->addr3;
	default:
		return NULL;
	}
}


#define HAPD_BROADCAST ((struct hostapd_data *) -1)

static struct hostapd_data * get_hapd_bssid(struct hostapd_iface *iface,
					    const u8 *bssid)
{
	size_t i;

	if (bssid == NULL)
		return NULL;
	if (bssid[0] == 0xff && bssid[1] == 0xff && bssid[2] == 0xff &&
	    bssid[3] == 0xff && bssid[4] == 0xff && bssid[5] == 0xff)
		return HAPD_BROADCAST;

	for (i = 0; i < iface->num_bss; i++) {
		if (os_memcmp(bssid, iface->bss[i]->own_addr, ETH_ALEN) == 0)
			return iface->bss[i];
	}

	return NULL;
}


static void hostapd_rx_from_unknown_sta(struct hostapd_data *hapd,
					const u8 *frame, size_t len)
{
	const struct ieee80211_hdr *hdr = (const struct ieee80211_hdr *) frame;
	u16 fc = le_to_host16(hdr->frame_control);
	hapd = get_hapd_bssid(hapd->iface, get_hdr_bssid(hdr, len));
	if (hapd == NULL || hapd == HAPD_BROADCAST)
		return;

	ieee802_11_rx_from_unknown(hapd, hdr->addr2,
				   (fc & (WLAN_FC_TODS | WLAN_FC_FROMDS)) ==
				   (WLAN_FC_TODS | WLAN_FC_FROMDS));
}


static void hostapd_mgmt_rx(struct hostapd_data *hapd, struct rx_mgmt *rx_mgmt)
{
	struct hostapd_iface *iface = hapd->iface;
	const struct ieee80211_hdr *hdr;
	const u8 *bssid;
	struct hostapd_frame_info fi;

	hdr = (const struct ieee80211_hdr *) rx_mgmt->frame;
	bssid = get_hdr_bssid(hdr, rx_mgmt->frame_len);
	if (bssid == NULL)
		return;

	hapd = get_hapd_bssid(iface, bssid);
	if (hapd == NULL) {
		u16 fc;
		fc = le_to_host16(hdr->frame_control);

		/*
		 * Drop frames to unknown BSSIDs except for Beacon frames which
		 * could be used to update neighbor information.
		 */
		if (WLAN_FC_GET_TYPE(fc) == WLAN_FC_TYPE_MGMT &&
		    WLAN_FC_GET_STYPE(fc) == WLAN_FC_STYPE_BEACON)
			hapd = iface->bss[0];
		else
			return;
	}

	os_memset(&fi, 0, sizeof(fi));
	fi.datarate = rx_mgmt->datarate;
	fi.ssi_signal = rx_mgmt->ssi_signal;

	if (hapd == HAPD_BROADCAST) {
		size_t i;
		for (i = 0; i < iface->num_bss; i++)
			ieee802_11_mgmt(iface->bss[i], rx_mgmt->frame,
					rx_mgmt->frame_len, &fi);
	} else
		ieee802_11_mgmt(hapd, rx_mgmt->frame, rx_mgmt->frame_len, &fi);

	random_add_randomness(&fi, sizeof(fi));
}


static void hostapd_mgmt_tx_cb(struct hostapd_data *hapd, const u8 *buf,
			       size_t len, u16 stype, int ok)
{
	struct ieee80211_hdr *hdr;
	hdr = (struct ieee80211_hdr *) buf;
	hapd = get_hapd_bssid(hapd->iface, get_hdr_bssid(hdr, len));
	if (hapd == NULL || hapd == HAPD_BROADCAST)
		return;
	ieee802_11_mgmt_cb(hapd, buf, len, stype, ok);
}

#endif /* NEED_AP_MLME */


static int hostapd_event_new_sta(struct hostapd_data *hapd, const u8 *addr)
{
	struct sta_info *sta = ap_get_sta(hapd, addr);
	if (sta)
		return 0;

	wpa_printf(MSG_DEBUG, "Data frame from unknown STA " MACSTR
		   " - adding a new STA", MAC2STR(addr));
	sta = ap_sta_add(hapd, addr);
	if (sta) {
		hostapd_new_assoc_sta(hapd, sta, 0);
	} else {
		wpa_printf(MSG_DEBUG, "Failed to add STA entry for " MACSTR,
			   MAC2STR(addr));
		return -1;
	}

	return 0;
}


static void hostapd_event_eapol_rx(struct hostapd_data *hapd, const u8 *src,
				   const u8 *data, size_t data_len)
{
	struct hostapd_iface *iface = hapd->iface;
	size_t j;

	for (j = 0; j < iface->num_bss; j++) {
		if (ap_get_sta(iface->bss[j], src)) {
			hapd = iface->bss[j];
			break;
		}
	}

	ieee802_1x_receive(hapd, src, data, data_len);
}


void wpa_supplicant_event(void *ctx, enum wpa_event_type event,
			  union wpa_event_data *data)
{
	struct hostapd_data *hapd = ctx;

	switch (event) {
	case EVENT_MICHAEL_MIC_FAILURE:
		michael_mic_failure(hapd, data->michael_mic_failure.src, 1);
		break;
	case EVENT_SCAN_RESULTS:
		if (hapd->iface->scan_cb)
			hapd->iface->scan_cb(hapd->iface);
		break;
#ifdef CONFIG_IEEE80211R
	case EVENT_FT_RRB_RX:
		wpa_ft_rrb_rx(hapd->wpa_auth, data->ft_rrb_rx.src,
			      data->ft_rrb_rx.data, data->ft_rrb_rx.data_len);
		break;
#endif /* CONFIG_IEEE80211R */
	case EVENT_WPS_BUTTON_PUSHED:
		hostapd_wps_button_pushed(hapd, NULL);
		break;
#ifdef NEED_AP_MLME
	case EVENT_TX_STATUS:
		switch (data->tx_status.type) {
		case WLAN_FC_TYPE_MGMT:
			hostapd_mgmt_tx_cb(hapd, data->tx_status.data,
					   data->tx_status.data_len,
					   data->tx_status.stype,
					   data->tx_status.ack);
			break;
		case WLAN_FC_TYPE_DATA:
			hostapd_tx_status(hapd, data->tx_status.dst,
					  data->tx_status.data,
					  data->tx_status.data_len,
					  data->tx_status.ack);
			break;
		}
		break;
	case EVENT_RX_FROM_UNKNOWN:
		hostapd_rx_from_unknown_sta(hapd, data->rx_from_unknown.frame,
					    data->rx_from_unknown.len);
		break;
	case EVENT_RX_MGMT:
		hostapd_mgmt_rx(hapd, &data->rx_mgmt);
		break;
#endif /* NEED_AP_MLME */
	case EVENT_RX_PROBE_REQ:
		if (data->rx_probe_req.sa == NULL ||
		    data->rx_probe_req.ie == NULL)
			break;
		hostapd_probe_req_rx(hapd, data->rx_probe_req.sa,
				     data->rx_probe_req.ie,
				     data->rx_probe_req.ie_len);
		break;
	case EVENT_NEW_STA:
		hostapd_event_new_sta(hapd, data->new_sta.addr);
		break;
	case EVENT_EAPOL_RX:
		hostapd_event_eapol_rx(hapd, data->eapol_rx.src,
				       data->eapol_rx.data,
				       data->eapol_rx.data_len);
		break;
	case EVENT_ASSOC:
		hostapd_notif_assoc(hapd, data->assoc_info.addr,
				    data->assoc_info.req_ies,
				    data->assoc_info.req_ies_len,
				    data->assoc_info.reassoc);
		break;
	case EVENT_DISASSOC:
		if (data)
			hostapd_notif_disassoc(hapd, data->disassoc_info.addr);
		break;
	case EVENT_DEAUTH:
		if (data)
			hostapd_notif_disassoc(hapd, data->deauth_info.addr);
		break;
	case EVENT_STATION_LOW_ACK:
		if (!data)
			break;
		hostapd_event_sta_low_ack(hapd, data->low_ack.addr);
		break;
	default:
		wpa_printf(MSG_DEBUG, "Unknown event %d", event);
		break;
	}
}

#endif /* HOSTAPD */
