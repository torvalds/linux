/*
 * Hotspot 2.0 AP ANQP processing
 * Copyright (c) 2009, Atheros Communications, Inc.
 * Copyright (c) 2011-2013, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "common/ieee802_11_defs.h"
#include "common/wpa_ctrl.h"
#include "hostapd.h"
#include "ap_config.h"
#include "ap_drv_ops.h"
#include "sta_info.h"
#include "hs20.h"


u8 * hostapd_eid_hs20_indication(struct hostapd_data *hapd, u8 *eid)
{
	u8 conf;
	if (!hapd->conf->hs20)
		return eid;
	*eid++ = WLAN_EID_VENDOR_SPECIFIC;
	*eid++ = 7;
	WPA_PUT_BE24(eid, OUI_WFA);
	eid += 3;
	*eid++ = HS20_INDICATION_OUI_TYPE;
	conf = HS20_VERSION; /* Release Number */
	conf |= HS20_ANQP_DOMAIN_ID_PRESENT;
	if (hapd->conf->disable_dgaf)
		conf |= HS20_DGAF_DISABLED;
	*eid++ = conf;
	WPA_PUT_LE16(eid, hapd->conf->anqp_domain_id);
	eid += 2;

	return eid;
}


u8 * hostapd_eid_osen(struct hostapd_data *hapd, u8 *eid)
{
	u8 *len;
	u16 capab;

	if (!hapd->conf->osen)
		return eid;

	*eid++ = WLAN_EID_VENDOR_SPECIFIC;
	len = eid++; /* to be filled */
	WPA_PUT_BE24(eid, OUI_WFA);
	eid += 3;
	*eid++ = HS20_OSEN_OUI_TYPE;

	/* Group Data Cipher Suite */
	RSN_SELECTOR_PUT(eid, RSN_CIPHER_SUITE_NO_GROUP_ADDRESSED);
	eid += RSN_SELECTOR_LEN;

	/* Pairwise Cipher Suite Count and List */
	WPA_PUT_LE16(eid, 1);
	eid += 2;
	RSN_SELECTOR_PUT(eid, RSN_CIPHER_SUITE_CCMP);
	eid += RSN_SELECTOR_LEN;

	/* AKM Suite Count and List */
	WPA_PUT_LE16(eid, 1);
	eid += 2;
	RSN_SELECTOR_PUT(eid, RSN_AUTH_KEY_MGMT_OSEN);
	eid += RSN_SELECTOR_LEN;

	/* RSN Capabilities */
	capab = 0;
	if (hapd->conf->wmm_enabled) {
		/* 4 PTKSA replay counters when using WMM */
		capab |= (RSN_NUM_REPLAY_COUNTERS_16 << 2);
	}
#ifdef CONFIG_IEEE80211W
	if (hapd->conf->ieee80211w != NO_MGMT_FRAME_PROTECTION) {
		capab |= WPA_CAPABILITY_MFPC;
		if (hapd->conf->ieee80211w == MGMT_FRAME_PROTECTION_REQUIRED)
			capab |= WPA_CAPABILITY_MFPR;
	}
#endif /* CONFIG_IEEE80211W */
	WPA_PUT_LE16(eid, capab);
	eid += 2;

	*len = eid - len - 1;

	return eid;
}


int hs20_send_wnm_notification(struct hostapd_data *hapd, const u8 *addr,
			       u8 osu_method, const char *url)
{
	struct wpabuf *buf;
	size_t len = 0;
	int ret;

	/* TODO: should refuse to send notification if the STA is not associated
	 * or if the STA did not indicate support for WNM-Notification */

	if (url) {
		len = 1 + os_strlen(url);
		if (5 + len > 255) {
			wpa_printf(MSG_INFO, "HS 2.0: Too long URL for "
				   "WNM-Notification: '%s'", url);
			return -1;
		}
	}

	buf = wpabuf_alloc(4 + 7 + len);
	if (buf == NULL)
		return -1;

	wpabuf_put_u8(buf, WLAN_ACTION_WNM);
	wpabuf_put_u8(buf, WNM_NOTIFICATION_REQ);
	wpabuf_put_u8(buf, 1); /* Dialog token */
	wpabuf_put_u8(buf, 1); /* Type - 1 reserved for WFA */

	/* Subscription Remediation subelement */
	wpabuf_put_u8(buf, WLAN_EID_VENDOR_SPECIFIC);
	wpabuf_put_u8(buf, 5 + len);
	wpabuf_put_be24(buf, OUI_WFA);
	wpabuf_put_u8(buf, HS20_WNM_SUB_REM_NEEDED);
	if (url) {
		wpabuf_put_u8(buf, len - 1);
		wpabuf_put_data(buf, url, len - 1);
		wpabuf_put_u8(buf, osu_method);
	} else {
		/* Server URL and Server Method fields not included */
		wpabuf_put_u8(buf, 0);
	}

	ret = hostapd_drv_send_action(hapd, hapd->iface->freq, 0, addr,
				      wpabuf_head(buf), wpabuf_len(buf));

	wpabuf_free(buf);

	return ret;
}


int hs20_send_wnm_notification_deauth_req(struct hostapd_data *hapd,
					  const u8 *addr,
					  const struct wpabuf *payload)
{
	struct wpabuf *buf;
	int ret;

	/* TODO: should refuse to send notification if the STA is not associated
	 * or if the STA did not indicate support for WNM-Notification */

	buf = wpabuf_alloc(4 + 6 + wpabuf_len(payload));
	if (buf == NULL)
		return -1;

	wpabuf_put_u8(buf, WLAN_ACTION_WNM);
	wpabuf_put_u8(buf, WNM_NOTIFICATION_REQ);
	wpabuf_put_u8(buf, 1); /* Dialog token */
	wpabuf_put_u8(buf, 1); /* Type - 1 reserved for WFA */

	/* Deauthentication Imminent Notice subelement */
	wpabuf_put_u8(buf, WLAN_EID_VENDOR_SPECIFIC);
	wpabuf_put_u8(buf, 4 + wpabuf_len(payload));
	wpabuf_put_be24(buf, OUI_WFA);
	wpabuf_put_u8(buf, HS20_WNM_DEAUTH_IMMINENT_NOTICE);
	wpabuf_put_buf(buf, payload);

	ret = hostapd_drv_send_action(hapd, hapd->iface->freq, 0, addr,
				      wpabuf_head(buf), wpabuf_len(buf));

	wpabuf_free(buf);

	return ret;
}


int hs20_send_wnm_notification_t_c(struct hostapd_data *hapd,
				   const u8 *addr, const char *url)
{
	struct wpabuf *buf;
	int ret;
	size_t url_len;

	if (!url) {
		wpa_printf(MSG_INFO, "HS 2.0: No T&C Server URL available");
		return -1;
	}

	url_len = os_strlen(url);
	if (5 + url_len > 255) {
		wpa_printf(MSG_INFO,
			   "HS 2.0: Too long T&C Server URL for WNM-Notification: '%s'",
			   url);
		return -1;
	}

	buf = wpabuf_alloc(4 + 7 + url_len);
	if (!buf)
		return -1;

	wpabuf_put_u8(buf, WLAN_ACTION_WNM);
	wpabuf_put_u8(buf, WNM_NOTIFICATION_REQ);
	wpabuf_put_u8(buf, 1); /* Dialog token */
	wpabuf_put_u8(buf, 1); /* Type - 1 reserved for WFA */

	/* Terms and Conditions Acceptance subelement */
	wpabuf_put_u8(buf, WLAN_EID_VENDOR_SPECIFIC);
	wpabuf_put_u8(buf, 4 + 1 + url_len);
	wpabuf_put_be24(buf, OUI_WFA);
	wpabuf_put_u8(buf, HS20_WNM_T_C_ACCEPTANCE);
	wpabuf_put_u8(buf, url_len);
	wpabuf_put_str(buf, url);

	ret = hostapd_drv_send_action(hapd, hapd->iface->freq, 0, addr,
				      wpabuf_head(buf), wpabuf_len(buf));

	wpabuf_free(buf);

	return ret;
}


void hs20_t_c_filtering(struct hostapd_data *hapd, struct sta_info *sta,
			int enabled)
{
	if (enabled) {
		wpa_printf(MSG_DEBUG,
			   "HS 2.0: Terms and Conditions filtering required for "
			   MACSTR, MAC2STR(sta->addr));
		sta->hs20_t_c_filtering = 1;
		/* TODO: Enable firewall filtering for the STA */
		wpa_msg(hapd->msg_ctx, MSG_INFO, HS20_T_C_FILTERING_ADD MACSTR,
			MAC2STR(sta->addr));
	} else {
		wpa_printf(MSG_DEBUG,
			   "HS 2.0: Terms and Conditions filtering not required for "
			   MACSTR, MAC2STR(sta->addr));
		sta->hs20_t_c_filtering = 0;
		/* TODO: Disable firewall filtering for the STA */
		wpa_msg(hapd->msg_ctx, MSG_INFO,
			HS20_T_C_FILTERING_REMOVE MACSTR, MAC2STR(sta->addr));
	}
}
