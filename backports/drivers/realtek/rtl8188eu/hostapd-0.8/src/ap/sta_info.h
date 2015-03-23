/*
 * hostapd / Station table
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

#ifndef STA_INFO_H
#define STA_INFO_H

/* STA flags */
#define WLAN_STA_AUTH BIT(0)
#define WLAN_STA_ASSOC BIT(1)
#define WLAN_STA_PS BIT(2)
#define WLAN_STA_TIM BIT(3)
#define WLAN_STA_PERM BIT(4)
#define WLAN_STA_AUTHORIZED BIT(5)
#define WLAN_STA_PENDING_POLL BIT(6) /* pending activity poll not ACKed */
#define WLAN_STA_SHORT_PREAMBLE BIT(7)
#define WLAN_STA_PREAUTH BIT(8)
#define WLAN_STA_WMM BIT(9)
#define WLAN_STA_MFP BIT(10)
#define WLAN_STA_HT BIT(11)
#define WLAN_STA_WPS BIT(12)
#define WLAN_STA_MAYBE_WPS BIT(13)
#define WLAN_STA_WDS BIT(14)
#define WLAN_STA_ASSOC_REQ_OK BIT(15)
#define WLAN_STA_NONERP BIT(31)

/* Maximum number of supported rates (from both Supported Rates and Extended
 * Supported Rates IEs). */
#define WLAN_SUPP_RATES_MAX 32


struct sta_info {
	struct sta_info *next; /* next entry in sta list */
	struct sta_info *hnext; /* next entry in hash table list */
	u8 addr[6];
	u16 aid; /* STA's unique AID (1 .. 2007) or 0 if not yet assigned */
	u32 flags; /* Bitfield of WLAN_STA_* */
	u16 capability;
	u16 listen_interval; /* or beacon_int for APs */
	u8 supported_rates[WLAN_SUPP_RATES_MAX];
	int supported_rates_len;

	unsigned int nonerp_set:1;
	unsigned int no_short_slot_time_set:1;
	unsigned int no_short_preamble_set:1;
	unsigned int no_ht_gf_set:1;
	unsigned int no_ht_set:1;
	unsigned int ht_20mhz_set:1;
	unsigned int no_p2p_set:1;

	u16 auth_alg;
	u8 previous_ap[6];

	enum {
		STA_NULLFUNC = 0, STA_DISASSOC, STA_DEAUTH, STA_REMOVE
	} timeout_next;

	/* IEEE 802.1X related data */
	struct eapol_state_machine *eapol_sm;

	/* IEEE 802.11f (IAPP) related data */
	struct ieee80211_mgmt *last_assoc_req;

	u32 acct_session_id_hi;
	u32 acct_session_id_lo;
	time_t acct_session_start;
	int acct_session_started;
	int acct_terminate_cause; /* Acct-Terminate-Cause */
	int acct_interim_interval; /* Acct-Interim-Interval */

	unsigned long last_rx_bytes;
	unsigned long last_tx_bytes;
	u32 acct_input_gigawords; /* Acct-Input-Gigawords */
	u32 acct_output_gigawords; /* Acct-Output-Gigawords */

	u8 *challenge; /* IEEE 802.11 Shared Key Authentication Challenge */

	struct wpa_state_machine *wpa_sm;
	struct rsn_preauth_interface *preauth_iface;

	struct hostapd_ssid *ssid; /* SSID selection based on (Re)AssocReq */
	struct hostapd_ssid *ssid_probe; /* SSID selection based on ProbeReq */

	int vlan_id;

	struct ieee80211_ht_capabilities *ht_capabilities;

#ifdef CONFIG_IEEE80211W
	int sa_query_count; /* number of pending SA Query requests;
			     * 0 = no SA Query in progress */
	int sa_query_timed_out;
	u8 *sa_query_trans_id; /* buffer of WLAN_SA_QUERY_TR_ID_LEN *
				* sa_query_count octets of pending SA Query
				* transaction identifiers */
	struct os_time sa_query_start;
#endif /* CONFIG_IEEE80211W */

	struct wpabuf *wps_ie; /* WPS IE from (Re)Association Request */
	struct wpabuf *p2p_ie; /* P2P IE from (Re)Association Request */
};


/* Default value for maximum station inactivity. After AP_MAX_INACTIVITY has
 * passed since last received frame from the station, a nullfunc data frame is
 * sent to the station. If this frame is not acknowledged and no other frames
 * have been received, the station will be disassociated after
 * AP_DISASSOC_DELAY seconds. Similarily, the station will be deauthenticated
 * after AP_DEAUTH_DELAY seconds has passed after disassociation. */
#define AP_MAX_INACTIVITY (5 * 60)
#define AP_DISASSOC_DELAY (1)
#define AP_DEAUTH_DELAY (1)
/* Number of seconds to keep STA entry with Authenticated flag after it has
 * been disassociated. */
#define AP_MAX_INACTIVITY_AFTER_DISASSOC (1 * 30)
/* Number of seconds to keep STA entry after it has been deauthenticated. */
#define AP_MAX_INACTIVITY_AFTER_DEAUTH (1 * 5)


struct hostapd_data;

int ap_for_each_sta(struct hostapd_data *hapd,
		    int (*cb)(struct hostapd_data *hapd, struct sta_info *sta,
			      void *ctx),
		    void *ctx);
struct sta_info * ap_get_sta(struct hostapd_data *hapd, const u8 *sta);
void ap_sta_hash_add(struct hostapd_data *hapd, struct sta_info *sta);
void ap_free_sta(struct hostapd_data *hapd, struct sta_info *sta);
void ap_free_sta(struct hostapd_data *hapd, struct sta_info *sta);
void hostapd_free_stas(struct hostapd_data *hapd);
void ap_handle_timer(void *eloop_ctx, void *timeout_ctx);
void ap_sta_session_timeout(struct hostapd_data *hapd, struct sta_info *sta,
			    u32 session_timeout);
void ap_sta_no_session_timeout(struct hostapd_data *hapd,
			       struct sta_info *sta);
struct sta_info * ap_sta_add(struct hostapd_data *hapd, const u8 *addr);
void ap_sta_disassociate(struct hostapd_data *hapd, struct sta_info *sta,
			 u16 reason);
void ap_sta_deauthenticate(struct hostapd_data *hapd, struct sta_info *sta,
			   u16 reason);
int ap_sta_bind_vlan(struct hostapd_data *hapd, struct sta_info *sta,
		     int old_vlanid);
void ap_sta_start_sa_query(struct hostapd_data *hapd, struct sta_info *sta);
void ap_sta_stop_sa_query(struct hostapd_data *hapd, struct sta_info *sta);
int ap_check_sa_query_timeout(struct hostapd_data *hapd, struct sta_info *sta);
void ap_sta_disconnect(struct hostapd_data *hapd, struct sta_info *sta,
		       const u8 *addr, u16 reason);

void ap_sta_set_authorized(struct hostapd_data *hapd,
			   struct sta_info *sta, int authorized);
static inline int ap_sta_is_authorized(struct sta_info *sta)
{
	return sta->flags & WLAN_STA_AUTHORIZED;
}

#endif /* STA_INFO_H */
