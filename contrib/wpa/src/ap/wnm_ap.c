/*
 * hostapd - WNM
 * Copyright (c) 2011-2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "common/ieee802_11_defs.h"
#include "common/wpa_ctrl.h"
#include "ap/hostapd.h"
#include "ap/sta_info.h"
#include "ap/ap_config.h"
#include "ap/ap_drv_ops.h"
#include "ap/wpa_auth.h"
#include "mbo_ap.h"
#include "wnm_ap.h"

#define MAX_TFS_IE_LEN  1024


/* get the TFS IE from driver */
static int ieee80211_11_get_tfs_ie(struct hostapd_data *hapd, const u8 *addr,
				   u8 *buf, u16 *buf_len, enum wnm_oper oper)
{
	wpa_printf(MSG_DEBUG, "%s: TFS get operation %d", __func__, oper);

	return hostapd_drv_wnm_oper(hapd, oper, addr, buf, buf_len);
}


/* set the TFS IE to driver */
static int ieee80211_11_set_tfs_ie(struct hostapd_data *hapd, const u8 *addr,
				   u8 *buf, u16 *buf_len, enum wnm_oper oper)
{
	wpa_printf(MSG_DEBUG, "%s: TFS set operation %d", __func__, oper);

	return hostapd_drv_wnm_oper(hapd, oper, addr, buf, buf_len);
}


/* MLME-SLEEPMODE.response */
static int ieee802_11_send_wnmsleep_resp(struct hostapd_data *hapd,
					 const u8 *addr, u8 dialog_token,
					 u8 action_type, u16 intval)
{
	struct ieee80211_mgmt *mgmt;
	int res;
	size_t len;
	size_t gtk_elem_len = 0;
	size_t igtk_elem_len = 0;
	struct wnm_sleep_element wnmsleep_ie;
	u8 *wnmtfs_ie;
	u8 wnmsleep_ie_len;
	u16 wnmtfs_ie_len;
	u8 *pos;
	struct sta_info *sta;
	enum wnm_oper tfs_oper = action_type == WNM_SLEEP_MODE_ENTER ?
		WNM_SLEEP_TFS_RESP_IE_ADD : WNM_SLEEP_TFS_RESP_IE_NONE;

	sta = ap_get_sta(hapd, addr);
	if (sta == NULL) {
		wpa_printf(MSG_DEBUG, "%s: station not found", __func__);
		return -EINVAL;
	}

	/* WNM-Sleep Mode IE */
	os_memset(&wnmsleep_ie, 0, sizeof(struct wnm_sleep_element));
	wnmsleep_ie_len = sizeof(struct wnm_sleep_element);
	wnmsleep_ie.eid = WLAN_EID_WNMSLEEP;
	wnmsleep_ie.len = wnmsleep_ie_len - 2;
	wnmsleep_ie.action_type = action_type;
	wnmsleep_ie.status = WNM_STATUS_SLEEP_ACCEPT;
	wnmsleep_ie.intval = host_to_le16(intval);

	/* TFS IE(s) */
	wnmtfs_ie = os_zalloc(MAX_TFS_IE_LEN);
	if (wnmtfs_ie == NULL)
		return -1;
	if (ieee80211_11_get_tfs_ie(hapd, addr, wnmtfs_ie, &wnmtfs_ie_len,
				    tfs_oper)) {
		wnmtfs_ie_len = 0;
		os_free(wnmtfs_ie);
		wnmtfs_ie = NULL;
	}

#define MAX_GTK_SUBELEM_LEN 45
#define MAX_IGTK_SUBELEM_LEN 26
	mgmt = os_zalloc(sizeof(*mgmt) + wnmsleep_ie_len +
			 MAX_GTK_SUBELEM_LEN + MAX_IGTK_SUBELEM_LEN);
	if (mgmt == NULL) {
		wpa_printf(MSG_DEBUG, "MLME: Failed to allocate buffer for "
			   "WNM-Sleep Response action frame");
		res = -1;
		goto fail;
	}
	os_memcpy(mgmt->da, addr, ETH_ALEN);
	os_memcpy(mgmt->sa, hapd->own_addr, ETH_ALEN);
	os_memcpy(mgmt->bssid, hapd->own_addr, ETH_ALEN);
	mgmt->frame_control = IEEE80211_FC(WLAN_FC_TYPE_MGMT,
					   WLAN_FC_STYPE_ACTION);
	mgmt->u.action.category = WLAN_ACTION_WNM;
	mgmt->u.action.u.wnm_sleep_resp.action = WNM_SLEEP_MODE_RESP;
	mgmt->u.action.u.wnm_sleep_resp.dialogtoken = dialog_token;
	pos = (u8 *)mgmt->u.action.u.wnm_sleep_resp.variable;
	/* add key data if MFP is enabled */
	if (!wpa_auth_uses_mfp(sta->wpa_sm) ||
	    hapd->conf->wnm_sleep_mode_no_keys ||
	    action_type != WNM_SLEEP_MODE_EXIT) {
		mgmt->u.action.u.wnm_sleep_resp.keydata_len = 0;
	} else {
		gtk_elem_len = wpa_wnmsleep_gtk_subelem(sta->wpa_sm, pos);
		pos += gtk_elem_len;
		wpa_printf(MSG_DEBUG, "Pass 4, gtk_len = %d",
			   (int) gtk_elem_len);
#ifdef CONFIG_IEEE80211W
		res = wpa_wnmsleep_igtk_subelem(sta->wpa_sm, pos);
		if (res < 0)
			goto fail;
		igtk_elem_len = res;
		pos += igtk_elem_len;
		wpa_printf(MSG_DEBUG, "Pass 4 igtk_len = %d",
			   (int) igtk_elem_len);
#endif /* CONFIG_IEEE80211W */

		WPA_PUT_LE16((u8 *)
			     &mgmt->u.action.u.wnm_sleep_resp.keydata_len,
			     gtk_elem_len + igtk_elem_len);
	}
	os_memcpy(pos, &wnmsleep_ie, wnmsleep_ie_len);
	/* copy TFS IE here */
	pos += wnmsleep_ie_len;
	if (wnmtfs_ie)
		os_memcpy(pos, wnmtfs_ie, wnmtfs_ie_len);

	len = 1 + sizeof(mgmt->u.action.u.wnm_sleep_resp) + gtk_elem_len +
		igtk_elem_len + wnmsleep_ie_len + wnmtfs_ie_len;

	/* In driver, response frame should be forced to sent when STA is in
	 * PS mode */
	res = hostapd_drv_send_action(hapd, hapd->iface->freq, 0,
				      mgmt->da, &mgmt->u.action.category, len);

	if (!res) {
		wpa_printf(MSG_DEBUG, "Successfully send WNM-Sleep Response "
			   "frame");

		/* when entering wnmsleep
		 * 1. pause the node in driver
		 * 2. mark the node so that AP won't update GTK/IGTK during
		 * WNM Sleep
		 */
		if (wnmsleep_ie.status == WNM_STATUS_SLEEP_ACCEPT &&
		    wnmsleep_ie.action_type == WNM_SLEEP_MODE_ENTER) {
			sta->flags |= WLAN_STA_WNM_SLEEP_MODE;
			hostapd_drv_wnm_oper(hapd, WNM_SLEEP_ENTER_CONFIRM,
					     addr, NULL, NULL);
			wpa_set_wnmsleep(sta->wpa_sm, 1);
		}
		/* when exiting wnmsleep
		 * 1. unmark the node
		 * 2. start GTK/IGTK update if MFP is not used
		 * 3. unpause the node in driver
		 */
		if ((wnmsleep_ie.status == WNM_STATUS_SLEEP_ACCEPT ||
		     wnmsleep_ie.status ==
		     WNM_STATUS_SLEEP_EXIT_ACCEPT_GTK_UPDATE) &&
		    wnmsleep_ie.action_type == WNM_SLEEP_MODE_EXIT) {
			sta->flags &= ~WLAN_STA_WNM_SLEEP_MODE;
			wpa_set_wnmsleep(sta->wpa_sm, 0);
			hostapd_drv_wnm_oper(hapd, WNM_SLEEP_EXIT_CONFIRM,
					     addr, NULL, NULL);
			if (!wpa_auth_uses_mfp(sta->wpa_sm) ||
			    hapd->conf->wnm_sleep_mode_no_keys)
				wpa_wnmsleep_rekey_gtk(sta->wpa_sm);
		}
	} else
		wpa_printf(MSG_DEBUG, "Fail to send WNM-Sleep Response frame");

#undef MAX_GTK_SUBELEM_LEN
#undef MAX_IGTK_SUBELEM_LEN
fail:
	os_free(wnmtfs_ie);
	os_free(mgmt);
	return res;
}


static void ieee802_11_rx_wnmsleep_req(struct hostapd_data *hapd,
				       const u8 *addr, const u8 *frm, int len)
{
	/* Dialog Token [1] | WNM-Sleep Mode IE | TFS Response IE */
	const u8 *pos = frm;
	u8 dialog_token;
	struct wnm_sleep_element *wnmsleep_ie = NULL;
	/* multiple TFS Req IE (assuming consecutive) */
	u8 *tfsreq_ie_start = NULL;
	u8 *tfsreq_ie_end = NULL;
	u16 tfsreq_ie_len = 0;

	if (!hapd->conf->wnm_sleep_mode) {
		wpa_printf(MSG_DEBUG, "Ignore WNM-Sleep Mode Request from "
			   MACSTR " since WNM-Sleep Mode is disabled",
			   MAC2STR(addr));
		return;
	}

	dialog_token = *pos++;
	while (pos + 1 < frm + len) {
		u8 ie_len = pos[1];
		if (pos + 2 + ie_len > frm + len)
			break;
		if (*pos == WLAN_EID_WNMSLEEP &&
		    ie_len >= (int) sizeof(*wnmsleep_ie) - 2)
			wnmsleep_ie = (struct wnm_sleep_element *) pos;
		else if (*pos == WLAN_EID_TFS_REQ) {
			if (!tfsreq_ie_start)
				tfsreq_ie_start = (u8 *) pos;
			tfsreq_ie_end = (u8 *) pos;
		} else
			wpa_printf(MSG_DEBUG, "WNM: EID %d not recognized",
				   *pos);
		pos += ie_len + 2;
	}

	if (!wnmsleep_ie) {
		wpa_printf(MSG_DEBUG, "No WNM-Sleep IE found");
		return;
	}

	if (wnmsleep_ie->action_type == WNM_SLEEP_MODE_ENTER &&
	    tfsreq_ie_start && tfsreq_ie_end &&
	    tfsreq_ie_end - tfsreq_ie_start >= 0) {
		tfsreq_ie_len = (tfsreq_ie_end + tfsreq_ie_end[1] + 2) -
			tfsreq_ie_start;
		wpa_printf(MSG_DEBUG, "TFS Req IE(s) found");
		/* pass the TFS Req IE(s) to driver for processing */
		if (ieee80211_11_set_tfs_ie(hapd, addr, tfsreq_ie_start,
					    &tfsreq_ie_len,
					    WNM_SLEEP_TFS_REQ_IE_SET))
			wpa_printf(MSG_DEBUG, "Fail to set TFS Req IE");
	}

	ieee802_11_send_wnmsleep_resp(hapd, addr, dialog_token,
				      wnmsleep_ie->action_type,
				      le_to_host16(wnmsleep_ie->intval));

	if (wnmsleep_ie->action_type == WNM_SLEEP_MODE_EXIT) {
		/* clear the tfs after sending the resp frame */
		ieee80211_11_set_tfs_ie(hapd, addr, tfsreq_ie_start,
					&tfsreq_ie_len, WNM_SLEEP_TFS_IE_DEL);
	}
}


static int ieee802_11_send_bss_trans_mgmt_request(struct hostapd_data *hapd,
						  const u8 *addr,
						  u8 dialog_token)
{
	struct ieee80211_mgmt *mgmt;
	size_t len;
	u8 *pos;
	int res;

	mgmt = os_zalloc(sizeof(*mgmt));
	if (mgmt == NULL)
		return -1;
	os_memcpy(mgmt->da, addr, ETH_ALEN);
	os_memcpy(mgmt->sa, hapd->own_addr, ETH_ALEN);
	os_memcpy(mgmt->bssid, hapd->own_addr, ETH_ALEN);
	mgmt->frame_control = IEEE80211_FC(WLAN_FC_TYPE_MGMT,
					   WLAN_FC_STYPE_ACTION);
	mgmt->u.action.category = WLAN_ACTION_WNM;
	mgmt->u.action.u.bss_tm_req.action = WNM_BSS_TRANS_MGMT_REQ;
	mgmt->u.action.u.bss_tm_req.dialog_token = dialog_token;
	mgmt->u.action.u.bss_tm_req.req_mode = 0;
	mgmt->u.action.u.bss_tm_req.disassoc_timer = host_to_le16(0);
	mgmt->u.action.u.bss_tm_req.validity_interval = 1;
	pos = mgmt->u.action.u.bss_tm_req.variable;

	wpa_printf(MSG_DEBUG, "WNM: Send BSS Transition Management Request to "
		   MACSTR " dialog_token=%u req_mode=0x%x disassoc_timer=%u "
		   "validity_interval=%u",
		   MAC2STR(addr), dialog_token,
		   mgmt->u.action.u.bss_tm_req.req_mode,
		   le_to_host16(mgmt->u.action.u.bss_tm_req.disassoc_timer),
		   mgmt->u.action.u.bss_tm_req.validity_interval);

	len = pos - &mgmt->u.action.category;
	res = hostapd_drv_send_action(hapd, hapd->iface->freq, 0,
				      mgmt->da, &mgmt->u.action.category, len);
	os_free(mgmt);
	return res;
}


static void ieee802_11_rx_bss_trans_mgmt_query(struct hostapd_data *hapd,
					       const u8 *addr, const u8 *frm,
					       size_t len)
{
	u8 dialog_token, reason;
	const u8 *pos, *end;
	int enabled = hapd->conf->bss_transition;

#ifdef CONFIG_MBO
	if (hapd->conf->mbo_enabled)
		enabled = 1;
#endif /* CONFIG_MBO */
	if (!enabled) {
		wpa_printf(MSG_DEBUG,
			   "Ignore BSS Transition Management Query from "
			   MACSTR
			   " since BSS Transition Management is disabled",
			   MAC2STR(addr));
		return;
	}

	if (len < 2) {
		wpa_printf(MSG_DEBUG, "WNM: Ignore too short BSS Transition Management Query from "
			   MACSTR, MAC2STR(addr));
		return;
	}

	pos = frm;
	end = pos + len;
	dialog_token = *pos++;
	reason = *pos++;

	wpa_printf(MSG_DEBUG, "WNM: BSS Transition Management Query from "
		   MACSTR " dialog_token=%u reason=%u",
		   MAC2STR(addr), dialog_token, reason);

	wpa_hexdump(MSG_DEBUG, "WNM: BSS Transition Candidate List Entries",
		    pos, end - pos);

	ieee802_11_send_bss_trans_mgmt_request(hapd, addr, dialog_token);
}


void ap_sta_reset_steer_flag_timer(void *eloop_ctx, void *timeout_ctx)
{
	struct hostapd_data *hapd = eloop_ctx;
	struct sta_info *sta = timeout_ctx;

	if (sta->agreed_to_steer) {
		wpa_printf(MSG_DEBUG, "%s: Reset steering flag for STA " MACSTR,
			   hapd->conf->iface, MAC2STR(sta->addr));
		sta->agreed_to_steer = 0;
	}
}


static void ieee802_11_rx_bss_trans_mgmt_resp(struct hostapd_data *hapd,
					      const u8 *addr, const u8 *frm,
					      size_t len)
{
	u8 dialog_token, status_code, bss_termination_delay;
	const u8 *pos, *end;
	int enabled = hapd->conf->bss_transition;
	struct sta_info *sta;

#ifdef CONFIG_MBO
	if (hapd->conf->mbo_enabled)
		enabled = 1;
#endif /* CONFIG_MBO */
	if (!enabled) {
		wpa_printf(MSG_DEBUG,
			   "Ignore BSS Transition Management Response from "
			   MACSTR
			   " since BSS Transition Management is disabled",
			   MAC2STR(addr));
		return;
	}

	if (len < 3) {
		wpa_printf(MSG_DEBUG, "WNM: Ignore too short BSS Transition Management Response from "
			   MACSTR, MAC2STR(addr));
		return;
	}

	pos = frm;
	end = pos + len;
	dialog_token = *pos++;
	status_code = *pos++;
	bss_termination_delay = *pos++;

	wpa_printf(MSG_DEBUG, "WNM: BSS Transition Management Response from "
		   MACSTR " dialog_token=%u status_code=%u "
		   "bss_termination_delay=%u", MAC2STR(addr), dialog_token,
		   status_code, bss_termination_delay);

	sta = ap_get_sta(hapd, addr);
	if (!sta) {
		wpa_printf(MSG_DEBUG, "Station " MACSTR
			   " not found for received BSS TM Response",
			   MAC2STR(addr));
		return;
	}

	if (status_code == WNM_BSS_TM_ACCEPT) {
		if (end - pos < ETH_ALEN) {
			wpa_printf(MSG_DEBUG, "WNM: not enough room for Target BSSID field");
			return;
		}
		sta->agreed_to_steer = 1;
		eloop_cancel_timeout(ap_sta_reset_steer_flag_timer, hapd, sta);
		eloop_register_timeout(2, 0, ap_sta_reset_steer_flag_timer,
				       hapd, sta);
		wpa_printf(MSG_DEBUG, "WNM: Target BSSID: " MACSTR,
			   MAC2STR(pos));
		wpa_msg(hapd->msg_ctx, MSG_INFO, BSS_TM_RESP MACSTR
			" status_code=%u bss_termination_delay=%u target_bssid="
			MACSTR,
			MAC2STR(addr), status_code, bss_termination_delay,
			MAC2STR(pos));
		pos += ETH_ALEN;
	} else {
		sta->agreed_to_steer = 0;
		wpa_msg(hapd->msg_ctx, MSG_INFO, BSS_TM_RESP MACSTR
			" status_code=%u bss_termination_delay=%u",
			MAC2STR(addr), status_code, bss_termination_delay);
	}

	wpa_hexdump(MSG_DEBUG, "WNM: BSS Transition Candidate List Entries",
		    pos, end - pos);
}


static void ieee802_11_rx_wnm_notification_req(struct hostapd_data *hapd,
					       const u8 *addr, const u8 *buf,
					       size_t len)
{
	u8 dialog_token, type;

	if (len < 2)
		return;
	dialog_token = *buf++;
	type = *buf++;
	len -= 2;

	wpa_printf(MSG_DEBUG,
		   "WNM: Received WNM Notification Request frame from "
		   MACSTR " (dialog_token=%u type=%u)",
		   MAC2STR(addr), dialog_token, type);
	wpa_hexdump(MSG_MSGDUMP, "WNM: Notification Request subelements",
		    buf, len);
	if (type == WLAN_EID_VENDOR_SPECIFIC)
		mbo_ap_wnm_notification_req(hapd, addr, buf, len);
}


static void ieee802_11_rx_wnm_coloc_intf_report(struct hostapd_data *hapd,
						const u8 *addr, const u8 *buf,
						size_t len)
{
	u8 dialog_token;
	char *hex;
	size_t hex_len;

	if (!hapd->conf->coloc_intf_reporting) {
		wpa_printf(MSG_DEBUG,
			   "WNM: Ignore unexpected Collocated Interference Report from "
			   MACSTR, MAC2STR(addr));
		return;
	}

	if (len < 1) {
		wpa_printf(MSG_DEBUG,
			   "WNM: Ignore too short Collocated Interference Report from "
			   MACSTR, MAC2STR(addr));
		return;
	}
	dialog_token = *buf++;
	len--;

	wpa_printf(MSG_DEBUG,
		   "WNM: Received Collocated Interference Report frame from "
		   MACSTR " (dialog_token=%u)",
		   MAC2STR(addr), dialog_token);
	wpa_hexdump(MSG_MSGDUMP, "WNM: Collocated Interference Report Elements",
		    buf, len);

	hex_len = 2 * len + 1;
	hex = os_malloc(hex_len);
	if (!hex)
		return;
	wpa_snprintf_hex(hex, hex_len, buf, len);
	wpa_msg_ctrl(hapd->msg_ctx, MSG_INFO, COLOC_INTF_REPORT MACSTR " %d %s",
		     MAC2STR(addr), dialog_token, hex);
	os_free(hex);
}


int ieee802_11_rx_wnm_action_ap(struct hostapd_data *hapd,
				const struct ieee80211_mgmt *mgmt, size_t len)
{
	u8 action;
	const u8 *payload;
	size_t plen;

	if (len < IEEE80211_HDRLEN + 2)
		return -1;

	payload = ((const u8 *) mgmt) + IEEE80211_HDRLEN + 1;
	action = *payload++;
	plen = len - IEEE80211_HDRLEN - 2;

	switch (action) {
	case WNM_BSS_TRANS_MGMT_QUERY:
		ieee802_11_rx_bss_trans_mgmt_query(hapd, mgmt->sa, payload,
						   plen);
		return 0;
	case WNM_BSS_TRANS_MGMT_RESP:
		ieee802_11_rx_bss_trans_mgmt_resp(hapd, mgmt->sa, payload,
						  plen);
		return 0;
	case WNM_SLEEP_MODE_REQ:
		ieee802_11_rx_wnmsleep_req(hapd, mgmt->sa, payload, plen);
		return 0;
	case WNM_NOTIFICATION_REQ:
		ieee802_11_rx_wnm_notification_req(hapd, mgmt->sa, payload,
						   plen);
		return 0;
	case WNM_COLLOCATED_INTERFERENCE_REPORT:
		ieee802_11_rx_wnm_coloc_intf_report(hapd, mgmt->sa, payload,
						    plen);
		return 0;
	}

	wpa_printf(MSG_DEBUG, "WNM: Unsupported WNM Action %u from " MACSTR,
		   action, MAC2STR(mgmt->sa));
	return -1;
}


int wnm_send_disassoc_imminent(struct hostapd_data *hapd,
			       struct sta_info *sta, int disassoc_timer)
{
	u8 buf[1000], *pos;
	struct ieee80211_mgmt *mgmt;

	os_memset(buf, 0, sizeof(buf));
	mgmt = (struct ieee80211_mgmt *) buf;
	mgmt->frame_control = IEEE80211_FC(WLAN_FC_TYPE_MGMT,
					   WLAN_FC_STYPE_ACTION);
	os_memcpy(mgmt->da, sta->addr, ETH_ALEN);
	os_memcpy(mgmt->sa, hapd->own_addr, ETH_ALEN);
	os_memcpy(mgmt->bssid, hapd->own_addr, ETH_ALEN);
	mgmt->u.action.category = WLAN_ACTION_WNM;
	mgmt->u.action.u.bss_tm_req.action = WNM_BSS_TRANS_MGMT_REQ;
	mgmt->u.action.u.bss_tm_req.dialog_token = 1;
	mgmt->u.action.u.bss_tm_req.req_mode =
		WNM_BSS_TM_REQ_DISASSOC_IMMINENT;
	mgmt->u.action.u.bss_tm_req.disassoc_timer =
		host_to_le16(disassoc_timer);
	mgmt->u.action.u.bss_tm_req.validity_interval = 0;

	pos = mgmt->u.action.u.bss_tm_req.variable;

	wpa_printf(MSG_DEBUG, "WNM: Send BSS Transition Management Request frame to indicate imminent disassociation (disassoc_timer=%d) to "
		   MACSTR, disassoc_timer, MAC2STR(sta->addr));
	if (hostapd_drv_send_mlme(hapd, buf, pos - buf, 0) < 0) {
		wpa_printf(MSG_DEBUG, "Failed to send BSS Transition "
			   "Management Request frame");
		return -1;
	}

	return 0;
}


static void set_disassoc_timer(struct hostapd_data *hapd, struct sta_info *sta,
			       int disassoc_timer)
{
	int timeout, beacon_int;

	/*
	 * Prevent STA from reconnecting using cached PMKSA to force
	 * full authentication with the authentication server (which may
	 * decide to reject the connection),
	 */
	wpa_auth_pmksa_remove(hapd->wpa_auth, sta->addr);

	beacon_int = hapd->iconf->beacon_int;
	if (beacon_int < 1)
		beacon_int = 100; /* best guess */
	/* Calculate timeout in ms based on beacon_int in TU */
	timeout = disassoc_timer * beacon_int * 128 / 125;
	wpa_printf(MSG_DEBUG, "Disassociation timer for " MACSTR
		   " set to %d ms", MAC2STR(sta->addr), timeout);

	sta->timeout_next = STA_DISASSOC_FROM_CLI;
	eloop_cancel_timeout(ap_handle_timer, hapd, sta);
	eloop_register_timeout(timeout / 1000,
			       timeout % 1000 * 1000,
			       ap_handle_timer, hapd, sta);
}


int wnm_send_ess_disassoc_imminent(struct hostapd_data *hapd,
				   struct sta_info *sta, const char *url,
				   int disassoc_timer)
{
	u8 buf[1000], *pos;
	struct ieee80211_mgmt *mgmt;
	size_t url_len;

	os_memset(buf, 0, sizeof(buf));
	mgmt = (struct ieee80211_mgmt *) buf;
	mgmt->frame_control = IEEE80211_FC(WLAN_FC_TYPE_MGMT,
					   WLAN_FC_STYPE_ACTION);
	os_memcpy(mgmt->da, sta->addr, ETH_ALEN);
	os_memcpy(mgmt->sa, hapd->own_addr, ETH_ALEN);
	os_memcpy(mgmt->bssid, hapd->own_addr, ETH_ALEN);
	mgmt->u.action.category = WLAN_ACTION_WNM;
	mgmt->u.action.u.bss_tm_req.action = WNM_BSS_TRANS_MGMT_REQ;
	mgmt->u.action.u.bss_tm_req.dialog_token = 1;
	mgmt->u.action.u.bss_tm_req.req_mode =
		WNM_BSS_TM_REQ_DISASSOC_IMMINENT |
		WNM_BSS_TM_REQ_ESS_DISASSOC_IMMINENT;
	mgmt->u.action.u.bss_tm_req.disassoc_timer =
		host_to_le16(disassoc_timer);
	mgmt->u.action.u.bss_tm_req.validity_interval = 0x01;

	pos = mgmt->u.action.u.bss_tm_req.variable;

	/* Session Information URL */
	url_len = os_strlen(url);
	if (url_len > 255)
		return -1;
	*pos++ = url_len;
	os_memcpy(pos, url, url_len);
	pos += url_len;

	if (hostapd_drv_send_mlme(hapd, buf, pos - buf, 0) < 0) {
		wpa_printf(MSG_DEBUG, "Failed to send BSS Transition "
			   "Management Request frame");
		return -1;
	}

	if (disassoc_timer) {
		/* send disassociation frame after time-out */
		set_disassoc_timer(hapd, sta, disassoc_timer);
	}

	return 0;
}


int wnm_send_bss_tm_req(struct hostapd_data *hapd, struct sta_info *sta,
			u8 req_mode, int disassoc_timer, u8 valid_int,
			const u8 *bss_term_dur, const char *url,
			const u8 *nei_rep, size_t nei_rep_len,
			const u8 *mbo_attrs, size_t mbo_len)
{
	u8 *buf, *pos;
	struct ieee80211_mgmt *mgmt;
	size_t url_len;

	wpa_printf(MSG_DEBUG, "WNM: Send BSS Transition Management Request to "
		   MACSTR " req_mode=0x%x disassoc_timer=%d valid_int=0x%x",
		   MAC2STR(sta->addr), req_mode, disassoc_timer, valid_int);
	buf = os_zalloc(1000 + nei_rep_len + mbo_len);
	if (buf == NULL)
		return -1;
	mgmt = (struct ieee80211_mgmt *) buf;
	mgmt->frame_control = IEEE80211_FC(WLAN_FC_TYPE_MGMT,
					   WLAN_FC_STYPE_ACTION);
	os_memcpy(mgmt->da, sta->addr, ETH_ALEN);
	os_memcpy(mgmt->sa, hapd->own_addr, ETH_ALEN);
	os_memcpy(mgmt->bssid, hapd->own_addr, ETH_ALEN);
	mgmt->u.action.category = WLAN_ACTION_WNM;
	mgmt->u.action.u.bss_tm_req.action = WNM_BSS_TRANS_MGMT_REQ;
	mgmt->u.action.u.bss_tm_req.dialog_token = 1;
	mgmt->u.action.u.bss_tm_req.req_mode = req_mode;
	mgmt->u.action.u.bss_tm_req.disassoc_timer =
		host_to_le16(disassoc_timer);
	mgmt->u.action.u.bss_tm_req.validity_interval = valid_int;

	pos = mgmt->u.action.u.bss_tm_req.variable;

	if ((req_mode & WNM_BSS_TM_REQ_BSS_TERMINATION_INCLUDED) &&
	    bss_term_dur) {
		os_memcpy(pos, bss_term_dur, 12);
		pos += 12;
	}

	if (url) {
		/* Session Information URL */
		url_len = os_strlen(url);
		if (url_len > 255) {
			os_free(buf);
			return -1;
		}

		*pos++ = url_len;
		os_memcpy(pos, url, url_len);
		pos += url_len;
	}

	if (nei_rep) {
		os_memcpy(pos, nei_rep, nei_rep_len);
		pos += nei_rep_len;
	}

	if (mbo_len > 0) {
		pos += mbo_add_ie(pos, buf + sizeof(buf) - pos, mbo_attrs,
				  mbo_len);
	}

	if (hostapd_drv_send_mlme(hapd, buf, pos - buf, 0) < 0) {
		wpa_printf(MSG_DEBUG,
			   "Failed to send BSS Transition Management Request frame");
		os_free(buf);
		return -1;
	}
	os_free(buf);

	if (disassoc_timer) {
		/* send disassociation frame after time-out */
		set_disassoc_timer(hapd, sta, disassoc_timer);
	}

	return 0;
}


int wnm_send_coloc_intf_req(struct hostapd_data *hapd, struct sta_info *sta,
			    unsigned int auto_report, unsigned int timeout)
{
	u8 buf[100], *pos;
	struct ieee80211_mgmt *mgmt;
	u8 dialog_token = 1;

	if (auto_report > 3 || timeout > 63)
		return -1;
	os_memset(buf, 0, sizeof(buf));
	mgmt = (struct ieee80211_mgmt *) buf;
	mgmt->frame_control = IEEE80211_FC(WLAN_FC_TYPE_MGMT,
					   WLAN_FC_STYPE_ACTION);
	os_memcpy(mgmt->da, sta->addr, ETH_ALEN);
	os_memcpy(mgmt->sa, hapd->own_addr, ETH_ALEN);
	os_memcpy(mgmt->bssid, hapd->own_addr, ETH_ALEN);
	mgmt->u.action.category = WLAN_ACTION_WNM;
	mgmt->u.action.u.coloc_intf_req.action =
		WNM_COLLOCATED_INTERFERENCE_REQ;
	mgmt->u.action.u.coloc_intf_req.dialog_token = dialog_token;
	mgmt->u.action.u.coloc_intf_req.req_info = auto_report | (timeout << 2);
	pos = &mgmt->u.action.u.coloc_intf_req.req_info;
	pos++;

	wpa_printf(MSG_DEBUG, "WNM: Sending Collocated Interference Request to "
		   MACSTR " (dialog_token=%u auto_report=%u timeout=%u)",
		   MAC2STR(sta->addr), dialog_token, auto_report, timeout);
	if (hostapd_drv_send_mlme(hapd, buf, pos - buf, 0) < 0) {
		wpa_printf(MSG_DEBUG,
			   "WNM: Failed to send Collocated Interference Request frame");
		return -1;
	}

	return 0;
}
