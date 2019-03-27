/*
 * hostapd / Radio Measurement (RRM)
 * Copyright(c) 2013 - 2016 Intel Mobile Communications GmbH.
 * Copyright(c) 2011 - 2016 Intel Corporation. All rights reserved.
 * Copyright (c) 2016-2017, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "common/wpa_ctrl.h"
#include "hostapd.h"
#include "ap_drv_ops.h"
#include "sta_info.h"
#include "eloop.h"
#include "neighbor_db.h"
#include "rrm.h"

#define HOSTAPD_RRM_REQUEST_TIMEOUT 5


static void hostapd_lci_rep_timeout_handler(void *eloop_data, void *user_ctx)
{
	struct hostapd_data *hapd = eloop_data;

	wpa_printf(MSG_DEBUG, "RRM: LCI request (token %u) timed out",
		   hapd->lci_req_token);
	hapd->lci_req_active = 0;
}


static void hostapd_handle_lci_report(struct hostapd_data *hapd, u8 token,
				      const u8 *pos, size_t len)
{
	if (!hapd->lci_req_active || hapd->lci_req_token != token) {
		wpa_printf(MSG_DEBUG, "Unexpected LCI report, token %u", token);
		return;
	}

	hapd->lci_req_active = 0;
	eloop_cancel_timeout(hostapd_lci_rep_timeout_handler, hapd, NULL);
	wpa_printf(MSG_DEBUG, "LCI report token %u len %zu", token, len);
}


static void hostapd_range_rep_timeout_handler(void *eloop_data, void *user_ctx)
{
	struct hostapd_data *hapd = eloop_data;

	wpa_printf(MSG_DEBUG, "RRM: Range request (token %u) timed out",
		   hapd->range_req_token);
	hapd->range_req_active = 0;
}


static void hostapd_handle_range_report(struct hostapd_data *hapd, u8 token,
					const u8 *pos, size_t len)
{
	if (!hapd->range_req_active || hapd->range_req_token != token) {
		wpa_printf(MSG_DEBUG, "Unexpected range report, token %u",
			   token);
		return;
	}

	hapd->range_req_active = 0;
	eloop_cancel_timeout(hostapd_range_rep_timeout_handler, hapd, NULL);
	wpa_printf(MSG_DEBUG, "Range report token %u len %zu", token, len);
}


static void hostapd_handle_beacon_report(struct hostapd_data *hapd,
					 const u8 *addr, u8 token, u8 rep_mode,
					 const u8 *pos, size_t len)
{
	char report[2 * 255 + 1];

	wpa_printf(MSG_DEBUG, "Beacon report token %u len %zu from " MACSTR,
		   token, len, MAC2STR(addr));
	/* Skip to the beginning of the Beacon report */
	if (len < 3)
		return;
	pos += 3;
	len -= 3;
	report[0] = '\0';
	if (wpa_snprintf_hex(report, sizeof(report), pos, len) < 0)
		return;
	wpa_msg(hapd->msg_ctx, MSG_INFO, BEACON_RESP_RX MACSTR " %u %02x %s",
		MAC2STR(addr), token, rep_mode, report);
}


static void hostapd_handle_radio_msmt_report(struct hostapd_data *hapd,
					     const u8 *buf, size_t len)
{
	const struct ieee80211_mgmt *mgmt = (const struct ieee80211_mgmt *) buf;
	const u8 *pos, *ie, *end;
	u8 token, rep_mode;

	end = buf + len;
	token = mgmt->u.action.u.rrm.dialog_token;
	pos = mgmt->u.action.u.rrm.variable;

	while ((ie = get_ie(pos, end - pos, WLAN_EID_MEASURE_REPORT))) {
		if (ie[1] < 3) {
			wpa_printf(MSG_DEBUG, "Bad Measurement Report element");
			break;
		}

		rep_mode = ie[3];
		wpa_printf(MSG_DEBUG, "Measurement report mode 0x%x type %u",
			   rep_mode, ie[4]);

		switch (ie[4]) {
		case MEASURE_TYPE_LCI:
			hostapd_handle_lci_report(hapd, token, ie + 2, ie[1]);
			break;
		case MEASURE_TYPE_FTM_RANGE:
			hostapd_handle_range_report(hapd, token, ie + 2, ie[1]);
			break;
		case MEASURE_TYPE_BEACON:
			hostapd_handle_beacon_report(hapd, mgmt->sa, token,
						     rep_mode, ie + 2, ie[1]);
			break;
		default:
			wpa_printf(MSG_DEBUG,
				   "Measurement report type %u is not supported",
				   ie[4]);
			break;
		}

		pos = ie + ie[1] + 2;
	}
}


static u16 hostapd_parse_location_lci_req_age(const u8 *buf, size_t len)
{
	const u8 *subelem;

	/* Range Request element + Location Subject + Maximum Age subelement */
	if (len < 3 + 1 + 4)
		return 0;

	/* Subelements are arranged as IEs */
	subelem = get_ie(buf + 4, len - 4, LCI_REQ_SUBELEM_MAX_AGE);
	if (subelem && subelem[1] == 2)
		return WPA_GET_LE16(subelem + 2);

	return 0;
}


static int hostapd_check_lci_age(struct hostapd_neighbor_entry *nr, u16 max_age)
{
	struct os_time curr, diff;
	unsigned long diff_l;

	if (nr->stationary || max_age == 0xffff)
		return 1;

	if (!max_age)
		return 0;

	if (os_get_time(&curr))
		return 0;

	os_time_sub(&curr, &nr->lci_date, &diff);

	/* avoid overflow */
	if (diff.sec > 0xffff)
		return 0;

	/* LCI age is calculated in 10th of a second units. */
	diff_l = diff.sec * 10 + diff.usec / 100000;

	return max_age > diff_l;
}


static size_t hostapd_neighbor_report_len(struct wpabuf *buf,
					  struct hostapd_neighbor_entry *nr,
					  int send_lci, int send_civic)
{
	size_t len = 2 + wpabuf_len(nr->nr);

	if (send_lci && nr->lci)
		len += 2 + wpabuf_len(nr->lci);

	if (send_civic && nr->civic)
		len += 2 + wpabuf_len(nr->civic);

	return len;
}


static void hostapd_send_nei_report_resp(struct hostapd_data *hapd,
					 const u8 *addr, u8 dialog_token,
					 struct wpa_ssid_value *ssid, u8 lci,
					 u8 civic, u16 lci_max_age)
{
	struct hostapd_neighbor_entry *nr;
	struct wpabuf *buf;
	u8 *msmt_token;

	/*
	 * The number and length of the Neighbor Report elements in a Neighbor
	 * Report frame is limited by the maximum allowed MMPDU size; + 3 bytes
	 * of RRM header.
	 */
	buf = wpabuf_alloc(3 + IEEE80211_MAX_MMPDU_SIZE);
	if (!buf)
		return;

	wpabuf_put_u8(buf, WLAN_ACTION_RADIO_MEASUREMENT);
	wpabuf_put_u8(buf, WLAN_RRM_NEIGHBOR_REPORT_RESPONSE);
	wpabuf_put_u8(buf, dialog_token);

	dl_list_for_each(nr, &hapd->nr_db, struct hostapd_neighbor_entry,
			 list) {
		int send_lci;
		size_t len;

		if (ssid->ssid_len != nr->ssid.ssid_len ||
		    os_memcmp(ssid->ssid, nr->ssid.ssid, ssid->ssid_len) != 0)
			continue;

		send_lci = (lci != 0) && hostapd_check_lci_age(nr, lci_max_age);
		len = hostapd_neighbor_report_len(buf, nr, send_lci, civic);

		if (len - 2 > 0xff) {
			wpa_printf(MSG_DEBUG,
				   "NR entry for " MACSTR " exceeds 0xFF bytes",
				   MAC2STR(nr->bssid));
			continue;
		}

		if (len > wpabuf_tailroom(buf))
			break;

		wpabuf_put_u8(buf, WLAN_EID_NEIGHBOR_REPORT);
		wpabuf_put_u8(buf, len - 2);
		wpabuf_put_buf(buf, nr->nr);

		if (send_lci && nr->lci) {
			wpabuf_put_u8(buf, WLAN_EID_MEASURE_REPORT);
			wpabuf_put_u8(buf, wpabuf_len(nr->lci));
			/*
			 * Override measurement token - the first byte of the
			 * Measurement Report element.
			 */
			msmt_token = wpabuf_put(buf, 0);
			wpabuf_put_buf(buf, nr->lci);
			*msmt_token = lci;
		}

		if (civic && nr->civic) {
			wpabuf_put_u8(buf, WLAN_EID_MEASURE_REPORT);
			wpabuf_put_u8(buf, wpabuf_len(nr->civic));
			/*
			 * Override measurement token - the first byte of the
			 * Measurement Report element.
			 */
			msmt_token = wpabuf_put(buf, 0);
			wpabuf_put_buf(buf, nr->civic);
			*msmt_token = civic;
		}
	}

	hostapd_drv_send_action(hapd, hapd->iface->freq, 0, addr,
				wpabuf_head(buf), wpabuf_len(buf));
	wpabuf_free(buf);
}


static void hostapd_handle_nei_report_req(struct hostapd_data *hapd,
					  const u8 *buf, size_t len)
{
	const struct ieee80211_mgmt *mgmt = (const struct ieee80211_mgmt *) buf;
	const u8 *pos, *ie, *end;
	struct wpa_ssid_value ssid = {
		.ssid_len = 0
	};
	u8 token;
	u8 lci = 0, civic = 0; /* Measurement tokens */
	u16 lci_max_age = 0;

	if (!(hapd->conf->radio_measurements[0] &
	      WLAN_RRM_CAPS_NEIGHBOR_REPORT))
		return;

	end = buf + len;

	token = mgmt->u.action.u.rrm.dialog_token;
	pos = mgmt->u.action.u.rrm.variable;
	len = end - pos;

	ie = get_ie(pos, len, WLAN_EID_SSID);
	if (ie && ie[1] && ie[1] <= SSID_MAX_LEN) {
		ssid.ssid_len = ie[1];
		os_memcpy(ssid.ssid, ie + 2, ssid.ssid_len);
	} else {
		ssid.ssid_len = hapd->conf->ssid.ssid_len;
		os_memcpy(ssid.ssid, hapd->conf->ssid.ssid, ssid.ssid_len);
	}

	while ((ie = get_ie(pos, len, WLAN_EID_MEASURE_REQUEST))) {
		if (ie[1] < 3)
			break;

		wpa_printf(MSG_DEBUG,
			   "Neighbor report request, measure type %u",
			   ie[4]);

		switch (ie[4]) { /* Measurement Type */
		case MEASURE_TYPE_LCI:
			lci = ie[2]; /* Measurement Token */
			lci_max_age = hostapd_parse_location_lci_req_age(ie + 2,
									 ie[1]);
			break;
		case MEASURE_TYPE_LOCATION_CIVIC:
			civic = ie[2]; /* Measurement token */
			break;
		}

		pos = ie + ie[1] + 2;
		len = end - pos;
	}

	hostapd_send_nei_report_resp(hapd, mgmt->sa, token, &ssid, lci, civic,
				     lci_max_age);
}


void hostapd_handle_radio_measurement(struct hostapd_data *hapd,
				      const u8 *buf, size_t len)
{
	const struct ieee80211_mgmt *mgmt = (const struct ieee80211_mgmt *) buf;

	/*
	 * Check for enough bytes: header + (1B)Category + (1B)Action +
	 * (1B)Dialog Token.
	 */
	if (len < IEEE80211_HDRLEN + 3)
		return;

	wpa_printf(MSG_DEBUG, "Radio measurement frame, action %u from " MACSTR,
		   mgmt->u.action.u.rrm.action, MAC2STR(mgmt->sa));

	switch (mgmt->u.action.u.rrm.action) {
	case WLAN_RRM_RADIO_MEASUREMENT_REPORT:
		hostapd_handle_radio_msmt_report(hapd, buf, len);
		break;
	case WLAN_RRM_NEIGHBOR_REPORT_REQUEST:
		hostapd_handle_nei_report_req(hapd, buf, len);
		break;
	default:
		wpa_printf(MSG_DEBUG, "RRM action %u is not supported",
			   mgmt->u.action.u.rrm.action);
		break;
	}
}


int hostapd_send_lci_req(struct hostapd_data *hapd, const u8 *addr)
{
	struct wpabuf *buf;
	struct sta_info *sta = ap_get_sta(hapd, addr);
	int ret;

	if (!sta || !(sta->flags & WLAN_STA_AUTHORIZED)) {
		wpa_printf(MSG_INFO,
			   "Request LCI: Destination address is not connected");
		return -1;
	}

	if (!(sta->rrm_enabled_capa[1] & WLAN_RRM_CAPS_LCI_MEASUREMENT)) {
		wpa_printf(MSG_INFO,
			   "Request LCI: Station does not support LCI in RRM");
		return -1;
	}

	if (hapd->lci_req_active) {
		wpa_printf(MSG_DEBUG,
			   "Request LCI: LCI request is already in process, overriding");
		hapd->lci_req_active = 0;
		eloop_cancel_timeout(hostapd_lci_rep_timeout_handler, hapd,
				     NULL);
	}

	/* Measurement request (5) + Measurement element with LCI (10) */
	buf = wpabuf_alloc(5 + 10);
	if (!buf)
		return -1;

	hapd->lci_req_token++;
	/* For wraparounds - the token must be nonzero */
	if (!hapd->lci_req_token)
		hapd->lci_req_token++;

	wpabuf_put_u8(buf, WLAN_ACTION_RADIO_MEASUREMENT);
	wpabuf_put_u8(buf, WLAN_RRM_RADIO_MEASUREMENT_REQUEST);
	wpabuf_put_u8(buf, hapd->lci_req_token);
	wpabuf_put_le16(buf, 0); /* Number of repetitions */

	wpabuf_put_u8(buf, WLAN_EID_MEASURE_REQUEST);
	wpabuf_put_u8(buf, 3 + 1 + 4);

	wpabuf_put_u8(buf, 1); /* Measurement Token */
	/*
	 * Parallel and Enable bits are 0, Duration, Request, and Report are
	 * reserved.
	 */
	wpabuf_put_u8(buf, 0);
	wpabuf_put_u8(buf, MEASURE_TYPE_LCI);

	wpabuf_put_u8(buf, LOCATION_SUBJECT_REMOTE);

	wpabuf_put_u8(buf, LCI_REQ_SUBELEM_MAX_AGE);
	wpabuf_put_u8(buf, 2);
	wpabuf_put_le16(buf, 0xffff);

	ret = hostapd_drv_send_action(hapd, hapd->iface->freq, 0, addr,
				      wpabuf_head(buf), wpabuf_len(buf));
	wpabuf_free(buf);
	if (ret)
		return ret;

	hapd->lci_req_active = 1;

	eloop_register_timeout(HOSTAPD_RRM_REQUEST_TIMEOUT, 0,
			       hostapd_lci_rep_timeout_handler, hapd, NULL);

	return 0;
}


int hostapd_send_range_req(struct hostapd_data *hapd, const u8 *addr,
			   u16 random_interval, u8 min_ap,
			   const u8 *responders, unsigned int n_responders)
{
	struct wpabuf *buf;
	struct sta_info *sta;
	u8 *len;
	unsigned int i;
	int ret;

	wpa_printf(MSG_DEBUG, "Request range: dest addr " MACSTR
		   " rand interval %u min AP %u n_responders %u", MAC2STR(addr),
		   random_interval, min_ap, n_responders);

	if (min_ap == 0 || min_ap > n_responders) {
		wpa_printf(MSG_INFO, "Request range: Wrong min AP count");
		return -1;
	}

	sta = ap_get_sta(hapd, addr);
	if (!sta || !(sta->flags & WLAN_STA_AUTHORIZED)) {
		wpa_printf(MSG_INFO,
			   "Request range: Destination address is not connected");
		return -1;
	}

	if (!(sta->rrm_enabled_capa[4] & WLAN_RRM_CAPS_FTM_RANGE_REPORT)) {
		wpa_printf(MSG_ERROR,
			   "Request range: Destination station does not support FTM range report in RRM");
		return -1;
	}

	if (hapd->range_req_active) {
		wpa_printf(MSG_DEBUG,
			   "Request range: Range request is already in process; overriding");
		hapd->range_req_active = 0;
		eloop_cancel_timeout(hostapd_range_rep_timeout_handler, hapd,
				     NULL);
	}

	/* Action + measurement type + token + reps + EID + len = 7 */
	buf = wpabuf_alloc(7 + 255);
	if (!buf)
		return -1;

	hapd->range_req_token++;
	if (!hapd->range_req_token) /* For wraparounds */
		hapd->range_req_token++;

	/* IEEE P802.11-REVmc/D5.0, 9.6.7.2 */
	wpabuf_put_u8(buf, WLAN_ACTION_RADIO_MEASUREMENT);
	wpabuf_put_u8(buf, WLAN_RRM_RADIO_MEASUREMENT_REQUEST);
	wpabuf_put_u8(buf, hapd->range_req_token); /* Dialog Token */
	wpabuf_put_le16(buf, 0); /* Number of Repetitions */

	/* IEEE P802.11-REVmc/D5.0, 9.4.2.21 */
	wpabuf_put_u8(buf, WLAN_EID_MEASURE_REQUEST);
	len = wpabuf_put(buf, 1); /* Length will be set later */

	wpabuf_put_u8(buf, 1); /* Measurement Token */
	/*
	 * Parallel and Enable bits are 0; Duration, Request, and Report are
	 * reserved.
	 */
	wpabuf_put_u8(buf, 0); /* Measurement Request Mode */
	wpabuf_put_u8(buf, MEASURE_TYPE_FTM_RANGE); /* Measurement Type */

	/* IEEE P802.11-REVmc/D5.0, 9.4.2.21.19 */
	wpabuf_put_le16(buf, random_interval); /* Randomization Interval */
	wpabuf_put_u8(buf, min_ap); /* Minimum AP Count */

	/* FTM Range Subelements */

	/*
	 * Taking the neighbor report part of the range request from neighbor
	 * database instead of requesting the separate bits of data from the
	 * user.
	 */
	for (i = 0; i < n_responders; i++) {
		struct hostapd_neighbor_entry *nr;

		nr = hostapd_neighbor_get(hapd, responders + ETH_ALEN * i,
					  NULL);
		if (!nr) {
			wpa_printf(MSG_INFO, "Missing neighbor report for "
				   MACSTR, MAC2STR(responders + ETH_ALEN * i));
			wpabuf_free(buf);
			return -1;
		}

		if (wpabuf_tailroom(buf) < 2 + wpabuf_len(nr->nr)) {
			wpa_printf(MSG_ERROR, "Too long range request");
			wpabuf_free(buf);
			return -1;
		}

		wpabuf_put_u8(buf, WLAN_EID_NEIGHBOR_REPORT);
		wpabuf_put_u8(buf, wpabuf_len(nr->nr));
		wpabuf_put_buf(buf, nr->nr);
	}

	/* Action + measurement type + token + reps + EID + len = 7 */
	*len = wpabuf_len(buf) - 7;

	ret = hostapd_drv_send_action(hapd, hapd->iface->freq, 0, addr,
				      wpabuf_head(buf), wpabuf_len(buf));
	wpabuf_free(buf);
	if (ret)
		return ret;

	hapd->range_req_active = 1;

	eloop_register_timeout(HOSTAPD_RRM_REQUEST_TIMEOUT, 0,
			       hostapd_range_rep_timeout_handler, hapd, NULL);

	return 0;
}


void hostapd_clean_rrm(struct hostapd_data *hapd)
{
	hostpad_free_neighbor_db(hapd);
	eloop_cancel_timeout(hostapd_lci_rep_timeout_handler, hapd, NULL);
	hapd->lci_req_active = 0;
	eloop_cancel_timeout(hostapd_range_rep_timeout_handler, hapd, NULL);
	hapd->range_req_active = 0;
}


int hostapd_send_beacon_req(struct hostapd_data *hapd, const u8 *addr,
			    u8 req_mode, const struct wpabuf *req)
{
	struct wpabuf *buf;
	struct sta_info *sta = ap_get_sta(hapd, addr);
	int ret;
	enum beacon_report_mode mode;
	const u8 *pos;

	/* Request data:
	 * Operating Class (1), Channel Number (1), Randomization Interval (2),
	 * Measurement Duration (2), Measurement Mode (1), BSSID (6),
	 * Optional Subelements (variable)
	 */
	if (wpabuf_len(req) < 13) {
		wpa_printf(MSG_INFO, "Beacon request: Too short request data");
		return -1;
	}
	pos = wpabuf_head(req);
	mode = pos[6];

	if (!sta || !(sta->flags & WLAN_STA_AUTHORIZED)) {
		wpa_printf(MSG_INFO,
			   "Beacon request: " MACSTR " is not connected",
			   MAC2STR(addr));
		return -1;
	}

	switch (mode) {
	case BEACON_REPORT_MODE_PASSIVE:
		if (!(sta->rrm_enabled_capa[0] &
		      WLAN_RRM_CAPS_BEACON_REPORT_PASSIVE)) {
			wpa_printf(MSG_INFO,
				   "Beacon request: " MACSTR
				   " does not support passive beacon report",
				   MAC2STR(addr));
			return -1;
		}
		break;
	case BEACON_REPORT_MODE_ACTIVE:
		if (!(sta->rrm_enabled_capa[0] &
		      WLAN_RRM_CAPS_BEACON_REPORT_ACTIVE)) {
			wpa_printf(MSG_INFO,
				   "Beacon request: " MACSTR
				   " does not support active beacon report",
				   MAC2STR(addr));
			return -1;
		}
		break;
	case BEACON_REPORT_MODE_TABLE:
		if (!(sta->rrm_enabled_capa[0] &
		      WLAN_RRM_CAPS_BEACON_REPORT_TABLE)) {
			wpa_printf(MSG_INFO,
				   "Beacon request: " MACSTR
				   " does not support table beacon report",
				   MAC2STR(addr));
			return -1;
		}
		break;
	default:
		wpa_printf(MSG_INFO,
			   "Beacon request: Unknown measurement mode %d", mode);
		return -1;
	}

	buf = wpabuf_alloc(5 + 2 + 3 + wpabuf_len(req));
	if (!buf)
		return -1;

	hapd->beacon_req_token++;
	if (!hapd->beacon_req_token)
		hapd->beacon_req_token++;

	wpabuf_put_u8(buf, WLAN_ACTION_RADIO_MEASUREMENT);
	wpabuf_put_u8(buf, WLAN_RRM_RADIO_MEASUREMENT_REQUEST);
	wpabuf_put_u8(buf, hapd->beacon_req_token);
	wpabuf_put_le16(buf, 0); /* Number of repetitions */

	/* Measurement Request element */
	wpabuf_put_u8(buf, WLAN_EID_MEASURE_REQUEST);
	wpabuf_put_u8(buf, 3 + wpabuf_len(req));
	wpabuf_put_u8(buf, 1); /* Measurement Token */
	wpabuf_put_u8(buf, req_mode); /* Measurement Request Mode */
	wpabuf_put_u8(buf, MEASURE_TYPE_BEACON); /* Measurement Type */
	wpabuf_put_buf(buf, req);

	ret = hostapd_drv_send_action(hapd, hapd->iface->freq, 0, addr,
				      wpabuf_head(buf), wpabuf_len(buf));
	wpabuf_free(buf);
	if (ret < 0)
		return ret;

	return hapd->beacon_req_token;
}


void hostapd_rrm_beacon_req_tx_status(struct hostapd_data *hapd,
				      const struct ieee80211_mgmt *mgmt,
				      size_t len, int ok)
{
	if (len < 24 + 3)
		return;
	wpa_msg(hapd->msg_ctx, MSG_INFO, BEACON_REQ_TX_STATUS MACSTR
		" %u ack=%d", MAC2STR(mgmt->da),
		mgmt->u.action.u.rrm.dialog_token, ok);
}
