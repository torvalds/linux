/*
 * Common driver-related functions
 * Copyright (c) 2003-2017, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include "utils/common.h"
#include "driver.h"

void wpa_scan_results_free(struct wpa_scan_results *res)
{
	size_t i;

	if (res == NULL)
		return;

	for (i = 0; i < res->num; i++)
		os_free(res->res[i]);
	os_free(res->res);
	os_free(res);
}


const char * event_to_string(enum wpa_event_type event)
{
#define E2S(n) case EVENT_ ## n: return #n
	switch (event) {
	E2S(ASSOC);
	E2S(DISASSOC);
	E2S(MICHAEL_MIC_FAILURE);
	E2S(SCAN_RESULTS);
	E2S(ASSOCINFO);
	E2S(INTERFACE_STATUS);
	E2S(PMKID_CANDIDATE);
	E2S(TDLS);
	E2S(FT_RESPONSE);
	E2S(IBSS_RSN_START);
	E2S(AUTH);
	E2S(DEAUTH);
	E2S(ASSOC_REJECT);
	E2S(AUTH_TIMED_OUT);
	E2S(ASSOC_TIMED_OUT);
	E2S(WPS_BUTTON_PUSHED);
	E2S(TX_STATUS);
	E2S(RX_FROM_UNKNOWN);
	E2S(RX_MGMT);
	E2S(REMAIN_ON_CHANNEL);
	E2S(CANCEL_REMAIN_ON_CHANNEL);
	E2S(RX_PROBE_REQ);
	E2S(NEW_STA);
	E2S(EAPOL_RX);
	E2S(SIGNAL_CHANGE);
	E2S(INTERFACE_ENABLED);
	E2S(INTERFACE_DISABLED);
	E2S(CHANNEL_LIST_CHANGED);
	E2S(INTERFACE_UNAVAILABLE);
	E2S(BEST_CHANNEL);
	E2S(UNPROT_DEAUTH);
	E2S(UNPROT_DISASSOC);
	E2S(STATION_LOW_ACK);
	E2S(IBSS_PEER_LOST);
	E2S(DRIVER_GTK_REKEY);
	E2S(SCHED_SCAN_STOPPED);
	E2S(DRIVER_CLIENT_POLL_OK);
	E2S(EAPOL_TX_STATUS);
	E2S(CH_SWITCH);
	E2S(WNM);
	E2S(CONNECT_FAILED_REASON);
	E2S(DFS_RADAR_DETECTED);
	E2S(DFS_CAC_FINISHED);
	E2S(DFS_CAC_ABORTED);
	E2S(DFS_NOP_FINISHED);
	E2S(SURVEY);
	E2S(SCAN_STARTED);
	E2S(AVOID_FREQUENCIES);
	E2S(NEW_PEER_CANDIDATE);
	E2S(ACS_CHANNEL_SELECTED);
	E2S(DFS_CAC_STARTED);
	E2S(P2P_LO_STOP);
	E2S(BEACON_LOSS);
	E2S(DFS_PRE_CAC_EXPIRED);
	E2S(EXTERNAL_AUTH);
	E2S(PORT_AUTHORIZED);
	E2S(STATION_OPMODE_CHANGED);
	E2S(INTERFACE_MAC_CHANGED);
	E2S(WDS_STA_INTERFACE_STATUS);
	}

	return "UNKNOWN";
#undef E2S
}


const char * channel_width_to_string(enum chan_width width)
{
	switch (width) {
	case CHAN_WIDTH_20_NOHT:
		return "20 MHz (no HT)";
	case CHAN_WIDTH_20:
		return "20 MHz";
	case CHAN_WIDTH_40:
		return "40 MHz";
	case CHAN_WIDTH_80:
		return "80 MHz";
	case CHAN_WIDTH_80P80:
		return "80+80 MHz";
	case CHAN_WIDTH_160:
		return "160 MHz";
	default:
		return "unknown";
	}
}


int ht_supported(const struct hostapd_hw_modes *mode)
{
	if (!(mode->flags & HOSTAPD_MODE_FLAG_HT_INFO_KNOWN)) {
		/*
		 * The driver did not indicate whether it supports HT. Assume
		 * it does to avoid connection issues.
		 */
		return 1;
	}

	/*
	 * IEEE Std 802.11n-2009 20.1.1:
	 * An HT non-AP STA shall support all EQM rates for one spatial stream.
	 */
	return mode->mcs_set[0] == 0xff;
}


int vht_supported(const struct hostapd_hw_modes *mode)
{
	if (!(mode->flags & HOSTAPD_MODE_FLAG_VHT_INFO_KNOWN)) {
		/*
		 * The driver did not indicate whether it supports VHT. Assume
		 * it does to avoid connection issues.
		 */
		return 1;
	}

	/*
	 * A VHT non-AP STA shall support MCS 0-7 for one spatial stream.
	 * TODO: Verify if this complies with the standard
	 */
	return (mode->vht_mcs_set[0] & 0x3) != 3;
}


static int wpa_check_wowlan_trigger(const char *start, const char *trigger,
				    int capa_trigger, u8 *param_trigger)
{
	if (os_strcmp(start, trigger) != 0)
		return 0;
	if (!capa_trigger)
		return 0;

	*param_trigger = 1;
	return 1;
}


struct wowlan_triggers *
wpa_get_wowlan_triggers(const char *wowlan_triggers,
			const struct wpa_driver_capa *capa)
{
	struct wowlan_triggers *triggers;
	char *start, *end, *buf;
	int last;

	if (!wowlan_triggers)
		return NULL;

	buf = os_strdup(wowlan_triggers);
	if (buf == NULL)
		return NULL;

	triggers = os_zalloc(sizeof(*triggers));
	if (triggers == NULL)
		goto out;

#define CHECK_TRIGGER(trigger) \
	wpa_check_wowlan_trigger(start, #trigger,			\
				  capa->wowlan_triggers.trigger,	\
				  &triggers->trigger)

	start = buf;
	while (*start != '\0') {
		while (isblank((unsigned char) *start))
			start++;
		if (*start == '\0')
			break;
		end = start;
		while (!isblank((unsigned char) *end) && *end != '\0')
			end++;
		last = *end == '\0';
		*end = '\0';

		if (!CHECK_TRIGGER(any) &&
		    !CHECK_TRIGGER(disconnect) &&
		    !CHECK_TRIGGER(magic_pkt) &&
		    !CHECK_TRIGGER(gtk_rekey_failure) &&
		    !CHECK_TRIGGER(eap_identity_req) &&
		    !CHECK_TRIGGER(four_way_handshake) &&
		    !CHECK_TRIGGER(rfkill_release)) {
			wpa_printf(MSG_DEBUG,
				   "Unknown/unsupported wowlan trigger '%s'",
				   start);
			os_free(triggers);
			triggers = NULL;
			goto out;
		}

		if (last)
			break;
		start = end + 1;
	}
#undef CHECK_TRIGGER

out:
	os_free(buf);
	return triggers;
}


const char * driver_flag_to_string(u64 flag)
{
#define DF2S(x) case WPA_DRIVER_FLAGS_ ## x: return #x
	switch (flag) {
	DF2S(DRIVER_IE);
	DF2S(SET_KEYS_AFTER_ASSOC);
	DF2S(DFS_OFFLOAD);
	DF2S(4WAY_HANDSHAKE);
	DF2S(WIRED);
	DF2S(SME);
	DF2S(AP);
	DF2S(SET_KEYS_AFTER_ASSOC_DONE);
	DF2S(HT_2040_COEX);
	DF2S(P2P_CONCURRENT);
	DF2S(P2P_DEDICATED_INTERFACE);
	DF2S(P2P_CAPABLE);
	DF2S(AP_TEARDOWN_SUPPORT);
	DF2S(P2P_MGMT_AND_NON_P2P);
	DF2S(SANE_ERROR_CODES);
	DF2S(OFFCHANNEL_TX);
	DF2S(EAPOL_TX_STATUS);
	DF2S(DEAUTH_TX_STATUS);
	DF2S(BSS_SELECTION);
	DF2S(TDLS_SUPPORT);
	DF2S(TDLS_EXTERNAL_SETUP);
	DF2S(PROBE_RESP_OFFLOAD);
	DF2S(AP_UAPSD);
	DF2S(INACTIVITY_TIMER);
	DF2S(AP_MLME);
	DF2S(SAE);
	DF2S(OBSS_SCAN);
	DF2S(IBSS);
	DF2S(RADAR);
	DF2S(DEDICATED_P2P_DEVICE);
	DF2S(QOS_MAPPING);
	DF2S(AP_CSA);
	DF2S(MESH);
	DF2S(ACS_OFFLOAD);
	DF2S(KEY_MGMT_OFFLOAD);
	DF2S(TDLS_CHANNEL_SWITCH);
	DF2S(HT_IBSS);
	DF2S(VHT_IBSS);
	DF2S(SUPPORT_HW_MODE_ANY);
	DF2S(OFFCHANNEL_SIMULTANEOUS);
	DF2S(FULL_AP_CLIENT_STATE);
	DF2S(P2P_LISTEN_OFFLOAD);
	DF2S(SUPPORT_FILS);
	DF2S(BEACON_RATE_LEGACY);
	DF2S(BEACON_RATE_HT);
	DF2S(BEACON_RATE_VHT);
	DF2S(MGMT_TX_RANDOM_TA);
	DF2S(MGMT_TX_RANDOM_TA_CONNECTED);
	DF2S(SCHED_SCAN_RELATIVE_RSSI);
	DF2S(HE_CAPABILITIES);
	DF2S(FILS_SK_OFFLOAD);
	DF2S(OCE_STA);
	DF2S(OCE_AP);
	DF2S(OCE_STA_CFON);
	DF2S(MFP_OPTIONAL);
	}
	return "UNKNOWN";
#undef DF2S
}
