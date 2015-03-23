/*
 * hostapd / WMM (Wi-Fi Multimedia)
 * Copyright 2002-2003, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
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
#include "common/ieee802_11_defs.h"
#include "common/ieee802_11_common.h"
#include "hostapd.h"
#include "ieee802_11.h"
#include "sta_info.h"
#include "ap_config.h"
#include "ap_drv_ops.h"
#include "wmm.h"


/* TODO: maintain separate sequence and fragment numbers for each AC
 * TODO: IGMP snooping to track which multicasts to forward - and use QOS-DATA
 * if only WMM stations are receiving a certain group */


static inline u8 wmm_aci_aifsn(int aifsn, int acm, int aci)
{
	u8 ret;
	ret = (aifsn << WMM_AC_AIFNS_SHIFT) & WMM_AC_AIFSN_MASK;
	if (acm)
		ret |= WMM_AC_ACM;
	ret |= (aci << WMM_AC_ACI_SHIFT) & WMM_AC_ACI_MASK;
	return ret;
}


static inline u8 wmm_ecw(int ecwmin, int ecwmax)
{
	return ((ecwmin << WMM_AC_ECWMIN_SHIFT) & WMM_AC_ECWMIN_MASK) |
		((ecwmax << WMM_AC_ECWMAX_SHIFT) & WMM_AC_ECWMAX_MASK);
}


/*
 * Add WMM Parameter Element to Beacon, Probe Response, and (Re)Association
 * Response frames.
 */
u8 * hostapd_eid_wmm(struct hostapd_data *hapd, u8 *eid)
{
	u8 *pos = eid;
	struct wmm_parameter_element *wmm =
		(struct wmm_parameter_element *) (pos + 2);
	int e;

	if (!hapd->conf->wmm_enabled)
		return eid;
	eid[0] = WLAN_EID_VENDOR_SPECIFIC;
	wmm->oui[0] = 0x00;
	wmm->oui[1] = 0x50;
	wmm->oui[2] = 0xf2;
	wmm->oui_type = WMM_OUI_TYPE;
	wmm->oui_subtype = WMM_OUI_SUBTYPE_PARAMETER_ELEMENT;
	wmm->version = WMM_VERSION;
	wmm->qos_info = hapd->parameter_set_count & 0xf;

	if (hapd->conf->wmm_uapsd)
		wmm->qos_info |= 0x80;

	wmm->reserved = 0;

	/* fill in a parameter set record for each AC */
	for (e = 0; e < 4; e++) {
		struct wmm_ac_parameter *ac = &wmm->ac[e];
		struct hostapd_wmm_ac_params *acp =
			&hapd->iconf->wmm_ac_params[e];

		ac->aci_aifsn = wmm_aci_aifsn(acp->aifs,
					      acp->admission_control_mandatory,
					      e);
		ac->cw = wmm_ecw(acp->cwmin, acp->cwmax);
		ac->txop_limit = host_to_le16(acp->txop_limit);
	}

	pos = (u8 *) (wmm + 1);
	eid[1] = pos - eid - 2; /* element length */

	return pos;
}


/* This function is called when a station sends an association request with
 * WMM info element. The function returns zero on success or non-zero on any
 * error in WMM element. eid does not include Element ID and Length octets. */
int hostapd_eid_wmm_valid(struct hostapd_data *hapd, const u8 *eid, size_t len)
{
	struct wmm_information_element *wmm;

	wpa_hexdump(MSG_MSGDUMP, "WMM IE", eid, len);

	if (len < sizeof(struct wmm_information_element)) {
		wpa_printf(MSG_DEBUG, "Too short WMM IE (len=%lu)",
			   (unsigned long) len);
		return -1;
	}

	wmm = (struct wmm_information_element *) eid;
	wpa_printf(MSG_DEBUG, "Validating WMM IE: OUI %02x:%02x:%02x  "
		   "OUI type %d  OUI sub-type %d  version %d  QoS info 0x%x",
		   wmm->oui[0], wmm->oui[1], wmm->oui[2], wmm->oui_type,
		   wmm->oui_subtype, wmm->version, wmm->qos_info);
	if (wmm->oui_subtype != WMM_OUI_SUBTYPE_INFORMATION_ELEMENT ||
	    wmm->version != WMM_VERSION) {
		wpa_printf(MSG_DEBUG, "Unsupported WMM IE Subtype/Version");
		return -1;
	}

	return 0;
}


static void wmm_send_action(struct hostapd_data *hapd, const u8 *addr,
			    const struct wmm_tspec_element *tspec,
			    u8 action_code, u8 dialogue_token, u8 status_code)
{
	u8 buf[256];
	struct ieee80211_mgmt *m = (struct ieee80211_mgmt *) buf;
	struct wmm_tspec_element *t = (struct wmm_tspec_element *)
		m->u.action.u.wmm_action.variable;
	int len;

	hostapd_logger(hapd, addr, HOSTAPD_MODULE_IEEE80211,
		       HOSTAPD_LEVEL_DEBUG,
		       "action response - reason %d", status_code);
	os_memset(buf, 0, sizeof(buf));
	m->frame_control = IEEE80211_FC(WLAN_FC_TYPE_MGMT,
					WLAN_FC_STYPE_ACTION);
	os_memcpy(m->da, addr, ETH_ALEN);
	os_memcpy(m->sa, hapd->own_addr, ETH_ALEN);
	os_memcpy(m->bssid, hapd->own_addr, ETH_ALEN);
	m->u.action.category = WLAN_ACTION_WMM;
	m->u.action.u.wmm_action.action_code = action_code;
	m->u.action.u.wmm_action.dialog_token = dialogue_token;
	m->u.action.u.wmm_action.status_code = status_code;
	os_memcpy(t, tspec, sizeof(struct wmm_tspec_element));
	len = ((u8 *) (t + 1)) - buf;

	if (hostapd_drv_send_mlme(hapd, m, len) < 0)
		perror("wmm_send_action: send");
}


int wmm_process_tspec(struct wmm_tspec_element *tspec)
{
	int medium_time, pps, duration;
	int up, psb, dir, tid;
	u16 val, surplus;

	up = (tspec->ts_info[1] >> 3) & 0x07;
	psb = (tspec->ts_info[1] >> 2) & 0x01;
	dir = (tspec->ts_info[0] >> 5) & 0x03;
	tid = (tspec->ts_info[0] >> 1) & 0x0f;
	wpa_printf(MSG_DEBUG, "WMM: TS Info: UP=%d PSB=%d Direction=%d TID=%d",
		   up, psb, dir, tid);
	val = le_to_host16(tspec->nominal_msdu_size);
	wpa_printf(MSG_DEBUG, "WMM: Nominal MSDU Size: %d%s",
		   val & 0x7fff, val & 0x8000 ? " (fixed)" : "");
	wpa_printf(MSG_DEBUG, "WMM: Mean Data Rate: %u bps",
		   le_to_host32(tspec->mean_data_rate));
	wpa_printf(MSG_DEBUG, "WMM: Minimum PHY Rate: %u bps",
		   le_to_host32(tspec->minimum_phy_rate));
	val = le_to_host16(tspec->surplus_bandwidth_allowance);
	wpa_printf(MSG_DEBUG, "WMM: Surplus Bandwidth Allowance: %u.%04u",
		   val >> 13, 10000 * (val & 0x1fff) / 0x2000);

	val = le_to_host16(tspec->nominal_msdu_size);
	if (val == 0) {
		wpa_printf(MSG_DEBUG, "WMM: Invalid Nominal MSDU Size (0)");
		return WMM_ADDTS_STATUS_INVALID_PARAMETERS;
	}
	/* pps = Ceiling((Mean Data Rate / 8) / Nominal MSDU Size) */
	pps = ((le_to_host32(tspec->mean_data_rate) / 8) + val - 1) / val;
	wpa_printf(MSG_DEBUG, "WMM: Packets-per-second estimate for TSPEC: %d",
		   pps);

	if (le_to_host32(tspec->minimum_phy_rate) < 1000000) {
		wpa_printf(MSG_DEBUG, "WMM: Too small Minimum PHY Rate");
		return WMM_ADDTS_STATUS_INVALID_PARAMETERS;
	}

	duration = (le_to_host16(tspec->nominal_msdu_size) & 0x7fff) * 8 /
		(le_to_host32(tspec->minimum_phy_rate) / 1000000) +
		50 /* FIX: proper SIFS + ACK duration */;

	/* unsigned binary number with an implicit binary point after the
	 * leftmost 3 bits, i.e., 0x2000 = 1.0 */
	surplus = le_to_host16(tspec->surplus_bandwidth_allowance);
	if (surplus <= 0x2000) {
		wpa_printf(MSG_DEBUG, "WMM: Surplus Bandwidth Allowance not "
			   "greater than unity");
		return WMM_ADDTS_STATUS_INVALID_PARAMETERS;
	}

	medium_time = surplus * pps * duration / 0x2000;
	wpa_printf(MSG_DEBUG, "WMM: Estimated medium time: %u", medium_time);

	/*
	 * TODO: store list of granted (and still active) TSPECs and check
	 * whether there is available medium time for this request. For now,
	 * just refuse requests that would by themselves take very large
	 * portion of the available bandwidth.
	 */
	if (medium_time > 750000) {
		wpa_printf(MSG_DEBUG, "WMM: Refuse TSPEC request for over "
			   "75%% of available bandwidth");
		return WMM_ADDTS_STATUS_REFUSED;
	}

	/* Convert to 32 microseconds per second unit */
	tspec->medium_time = host_to_le16(medium_time / 32);

	return WMM_ADDTS_STATUS_ADMISSION_ACCEPTED;
}


static void wmm_addts_req(struct hostapd_data *hapd,
			  const struct ieee80211_mgmt *mgmt,
			  struct wmm_tspec_element *tspec, size_t len)
{
	const u8 *end = ((const u8 *) mgmt) + len;
	int res;

	if ((const u8 *) (tspec + 1) > end) {
		wpa_printf(MSG_DEBUG, "WMM: TSPEC overflow in ADDTS Request");
		return;
	}

	wpa_printf(MSG_DEBUG, "WMM: ADDTS Request (Dialog Token %d) for TSPEC "
		   "from " MACSTR,
		   mgmt->u.action.u.wmm_action.dialog_token,
		   MAC2STR(mgmt->sa));

	res = wmm_process_tspec(tspec);
	wpa_printf(MSG_DEBUG, "WMM: ADDTS processing result: %d", res);

	wmm_send_action(hapd, mgmt->sa, tspec, WMM_ACTION_CODE_ADDTS_RESP,
			mgmt->u.action.u.wmm_action.dialog_token, res);
}


void hostapd_wmm_action(struct hostapd_data *hapd,
			const struct ieee80211_mgmt *mgmt, size_t len)
{
	int action_code;
	int left = len - IEEE80211_HDRLEN - 4;
	const u8 *pos = ((const u8 *) mgmt) + IEEE80211_HDRLEN + 4;
	struct ieee802_11_elems elems;
	struct sta_info *sta = ap_get_sta(hapd, mgmt->sa);

	/* check that the request comes from a valid station */
	if (!sta ||
	    (sta->flags & (WLAN_STA_ASSOC | WLAN_STA_WMM)) !=
	    (WLAN_STA_ASSOC | WLAN_STA_WMM)) {
		hostapd_logger(hapd, mgmt->sa, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "wmm action received is not from associated wmm"
			       " station");
		/* TODO: respond with action frame refused status code */
		return;
	}

	/* extract the tspec info element */
	if (ieee802_11_parse_elems(pos, left, &elems, 1) == ParseFailed) {
		hostapd_logger(hapd, mgmt->sa, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "hostapd_wmm_action - could not parse wmm "
			       "action");
		/* TODO: respond with action frame invalid parameters status
		 * code */
		return;
	}

	if (!elems.wmm_tspec ||
	    elems.wmm_tspec_len != (sizeof(struct wmm_tspec_element) - 2)) {
		hostapd_logger(hapd, mgmt->sa, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "hostapd_wmm_action - missing or wrong length "
			       "tspec");
		/* TODO: respond with action frame invalid parameters status
		 * code */
		return;
	}

	/* TODO: check the request is for an AC with ACM set, if not, refuse
	 * request */

	action_code = mgmt->u.action.u.wmm_action.action_code;
	switch (action_code) {
	case WMM_ACTION_CODE_ADDTS_REQ:
		wmm_addts_req(hapd, mgmt, (struct wmm_tspec_element *)
			      (elems.wmm_tspec - 2), len);
		return;
#if 0
	/* TODO: needed for client implementation */
	case WMM_ACTION_CODE_ADDTS_RESP:
		wmm_setup_request(hapd, mgmt, len);
		return;
	/* TODO: handle station teardown requests */
	case WMM_ACTION_CODE_DELTS:
		wmm_teardown(hapd, mgmt, len);
		return;
#endif
	}

	hostapd_logger(hapd, mgmt->sa, HOSTAPD_MODULE_IEEE80211,
		       HOSTAPD_LEVEL_DEBUG,
		       "hostapd_wmm_action - unknown action code %d",
		       action_code);
}
