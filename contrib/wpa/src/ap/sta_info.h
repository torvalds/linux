/*
 * hostapd / Station table
 * Copyright (c) 2002-2017, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef STA_INFO_H
#define STA_INFO_H

#include "common/defs.h"
#include "list.h"
#include "vlan.h"
#include "common/wpa_common.h"
#include "common/ieee802_11_defs.h"

/* STA flags */
#define WLAN_STA_AUTH BIT(0)
#define WLAN_STA_ASSOC BIT(1)
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
#define WLAN_STA_WPS2 BIT(16)
#define WLAN_STA_GAS BIT(17)
#define WLAN_STA_VHT BIT(18)
#define WLAN_STA_WNM_SLEEP_MODE BIT(19)
#define WLAN_STA_VHT_OPMODE_ENABLED BIT(20)
#define WLAN_STA_VENDOR_VHT BIT(21)
#define WLAN_STA_PENDING_FILS_ERP BIT(22)
#define WLAN_STA_PENDING_DISASSOC_CB BIT(29)
#define WLAN_STA_PENDING_DEAUTH_CB BIT(30)
#define WLAN_STA_NONERP BIT(31)

/* Maximum number of supported rates (from both Supported Rates and Extended
 * Supported Rates IEs). */
#define WLAN_SUPP_RATES_MAX 32

struct hostapd_data;

struct mbo_non_pref_chan_info {
	struct mbo_non_pref_chan_info *next;
	u8 op_class;
	u8 pref;
	u8 reason_code;
	u8 num_channels;
	u8 channels[];
};

struct pending_eapol_rx {
	struct wpabuf *buf;
	struct os_reltime rx_time;
};

struct sta_info {
	struct sta_info *next; /* next entry in sta list */
	struct sta_info *hnext; /* next entry in hash table list */
	u8 addr[6];
	be32 ipaddr;
	struct dl_list ip6addr; /* list head for struct ip6addr */
	u16 aid; /* STA's unique AID (1 .. 2007) or 0 if not yet assigned */
	u16 disconnect_reason_code; /* RADIUS server override */
	u32 flags; /* Bitfield of WLAN_STA_* */
	u16 capability;
	u16 listen_interval; /* or beacon_int for APs */
	u8 supported_rates[WLAN_SUPP_RATES_MAX];
	int supported_rates_len;
	u8 qosinfo; /* Valid when WLAN_STA_WMM is set */

#ifdef CONFIG_MESH
	enum mesh_plink_state plink_state;
	u16 peer_lid;
	u16 my_lid;
	u16 peer_aid;
	u16 mpm_close_reason;
	int mpm_retries;
	u8 my_nonce[WPA_NONCE_LEN];
	u8 peer_nonce[WPA_NONCE_LEN];
	u8 aek[32];	/* SHA256 digest length */
	u8 mtk[WPA_TK_MAX_LEN];
	size_t mtk_len;
	u8 mgtk_rsc[6];
	u8 mgtk_key_id;
	u8 mgtk[WPA_TK_MAX_LEN];
	size_t mgtk_len;
	u8 igtk_rsc[6];
	u8 igtk[WPA_TK_MAX_LEN];
	size_t igtk_len;
	u16 igtk_key_id;
	u8 sae_auth_retry;
#endif /* CONFIG_MESH */

	unsigned int nonerp_set:1;
	unsigned int no_short_slot_time_set:1;
	unsigned int no_short_preamble_set:1;
	unsigned int no_ht_gf_set:1;
	unsigned int no_ht_set:1;
	unsigned int ht40_intolerant_set:1;
	unsigned int ht_20mhz_set:1;
	unsigned int no_p2p_set:1;
	unsigned int qos_map_enabled:1;
	unsigned int remediation:1;
	unsigned int hs20_deauth_requested:1;
	unsigned int session_timeout_set:1;
	unsigned int radius_das_match:1;
	unsigned int ecsa_supported:1;
	unsigned int added_unassoc:1;
	unsigned int pending_wds_enable:1;
	unsigned int power_capab:1;
	unsigned int agreed_to_steer:1;
	unsigned int hs20_t_c_filtering:1;

	u16 auth_alg;

	enum {
		STA_NULLFUNC = 0, STA_DISASSOC, STA_DEAUTH, STA_REMOVE,
		STA_DISASSOC_FROM_CLI
	} timeout_next;

	u16 deauth_reason;
	u16 disassoc_reason;

	/* IEEE 802.1X related data */
	struct eapol_state_machine *eapol_sm;

	struct pending_eapol_rx *pending_eapol_rx;

	u64 acct_session_id;
	struct os_reltime acct_session_start;
	int acct_session_started;
	int acct_terminate_cause; /* Acct-Terminate-Cause */
	int acct_interim_interval; /* Acct-Interim-Interval */
	unsigned int acct_interim_errors;

	/* For extending 32-bit driver counters to 64-bit counters */
	u32 last_rx_bytes_hi;
	u32 last_rx_bytes_lo;
	u32 last_tx_bytes_hi;
	u32 last_tx_bytes_lo;

	u8 *challenge; /* IEEE 802.11 Shared Key Authentication Challenge */

	struct wpa_state_machine *wpa_sm;
	struct rsn_preauth_interface *preauth_iface;

	int vlan_id; /* 0: none, >0: VID */
	struct vlan_description *vlan_desc;
	int vlan_id_bound; /* updated by ap_sta_bind_vlan() */
	 /* PSKs from RADIUS authentication server */
	struct hostapd_sta_wpa_psk_short *psk;

	char *identity; /* User-Name from RADIUS */
	char *radius_cui; /* Chargeable-User-Identity from RADIUS */

	struct ieee80211_ht_capabilities *ht_capabilities;
	struct ieee80211_vht_capabilities *vht_capabilities;
	u8 vht_opmode;

#ifdef CONFIG_IEEE80211W
	int sa_query_count; /* number of pending SA Query requests;
			     * 0 = no SA Query in progress */
	int sa_query_timed_out;
	u8 *sa_query_trans_id; /* buffer of WLAN_SA_QUERY_TR_ID_LEN *
				* sa_query_count octets of pending SA Query
				* transaction identifiers */
	struct os_reltime sa_query_start;
#endif /* CONFIG_IEEE80211W */

#if defined(CONFIG_INTERWORKING) || defined(CONFIG_DPP)
#define GAS_DIALOG_MAX 8 /* Max concurrent dialog number */
	struct gas_dialog_info *gas_dialog;
	u8 gas_dialog_next;
#endif /* CONFIG_INTERWORKING || CONFIG_DPP */

	struct wpabuf *wps_ie; /* WPS IE from (Re)Association Request */
	struct wpabuf *p2p_ie; /* P2P IE from (Re)Association Request */
	struct wpabuf *hs20_ie; /* HS 2.0 IE from (Re)Association Request */
	/* Hotspot 2.0 Roaming Consortium from (Re)Association Request */
	struct wpabuf *roaming_consortium;
	u8 remediation_method;
	char *remediation_url; /* HS 2.0 Subscription Remediation Server URL */
	char *t_c_url; /* HS 2.0 Terms and Conditions Server URL */
	struct wpabuf *hs20_deauth_req;
	char *hs20_session_info_url;
	int hs20_disassoc_timer;
#ifdef CONFIG_FST
	struct wpabuf *mb_ies; /* MB IEs from (Re)Association Request */
#endif /* CONFIG_FST */

	struct os_reltime connected_time;

#ifdef CONFIG_SAE
	struct sae_data *sae;
	unsigned int mesh_sae_pmksa_caching:1;
#endif /* CONFIG_SAE */

	/* valid only if session_timeout_set == 1 */
	struct os_reltime session_timeout;

	/* Last Authentication/(Re)Association Request/Action frame sequence
	 * control */
	u16 last_seq_ctrl;
	/* Last Authentication/(Re)Association Request/Action frame subtype */
	u8 last_subtype;

#ifdef CONFIG_MBO
	u8 cell_capa; /* 0 = unknown (not an MBO STA); otherwise,
		       * enum mbo_cellular_capa values */
	struct mbo_non_pref_chan_info *non_pref_chan;
#endif /* CONFIG_MBO */

	u8 *supp_op_classes; /* Supported Operating Classes element, if
			      * received, starting from the Length field */

	u8 rrm_enabled_capa[5];

	s8 min_tx_power;
	s8 max_tx_power;

#ifdef CONFIG_TAXONOMY
	struct wpabuf *probe_ie_taxonomy;
	struct wpabuf *assoc_ie_taxonomy;
#endif /* CONFIG_TAXONOMY */

#ifdef CONFIG_FILS
	u8 fils_snonce[FILS_NONCE_LEN];
	u8 fils_session[FILS_SESSION_LEN];
	u8 fils_erp_pmkid[PMKID_LEN];
	u8 *fils_pending_assoc_req;
	size_t fils_pending_assoc_req_len;
	unsigned int fils_pending_assoc_is_reassoc:1;
	unsigned int fils_dhcp_rapid_commit_proxy:1;
	unsigned int fils_erp_pmkid_set:1;
	unsigned int fils_drv_assoc_finish:1;
	struct wpabuf *fils_hlp_resp;
	struct wpabuf *hlp_dhcp_discover;
	void (*fils_pending_cb)(struct hostapd_data *hapd, struct sta_info *sta,
				u16 resp, struct wpabuf *data, int pub);
#ifdef CONFIG_FILS_SK_PFS
	struct crypto_ecdh *fils_ecdh;
#endif /* CONFIG_FILS_SK_PFS */
	struct wpabuf *fils_dh_ss;
	struct wpabuf *fils_g_sta;
#endif /* CONFIG_FILS */

#ifdef CONFIG_OWE
	u8 *owe_pmk;
	size_t owe_pmk_len;
	struct crypto_ecdh *owe_ecdh;
	u16 owe_group;
#endif /* CONFIG_OWE */

	u8 *ext_capability;
	char *ifname_wds; /* WDS ifname, if in use */

#ifdef CONFIG_TESTING_OPTIONS
	enum wpa_alg last_tk_alg;
	int last_tk_key_idx;
	u8 last_tk[WPA_TK_MAX_LEN];
	size_t last_tk_len;
#endif /* CONFIG_TESTING_OPTIONS */
};


/* Default value for maximum station inactivity. After AP_MAX_INACTIVITY has
 * passed since last received frame from the station, a nullfunc data frame is
 * sent to the station. If this frame is not acknowledged and no other frames
 * have been received, the station will be disassociated after
 * AP_DISASSOC_DELAY seconds. Similarly, the station will be deauthenticated
 * after AP_DEAUTH_DELAY seconds has passed after disassociation. */
#define AP_MAX_INACTIVITY (5 * 60)
#define AP_DISASSOC_DELAY (3)
#define AP_DEAUTH_DELAY (1)
/* Number of seconds to keep STA entry with Authenticated flag after it has
 * been disassociated. */
#define AP_MAX_INACTIVITY_AFTER_DISASSOC (1 * 30)
/* Number of seconds to keep STA entry after it has been deauthenticated. */
#define AP_MAX_INACTIVITY_AFTER_DEAUTH (1 * 5)


int ap_for_each_sta(struct hostapd_data *hapd,
		    int (*cb)(struct hostapd_data *hapd, struct sta_info *sta,
			      void *ctx),
		    void *ctx);
struct sta_info * ap_get_sta(struct hostapd_data *hapd, const u8 *sta);
struct sta_info * ap_get_sta_p2p(struct hostapd_data *hapd, const u8 *addr);
void ap_sta_hash_add(struct hostapd_data *hapd, struct sta_info *sta);
void ap_free_sta(struct hostapd_data *hapd, struct sta_info *sta);
void ap_sta_ip6addr_del(struct hostapd_data *hapd, struct sta_info *sta);
void hostapd_free_stas(struct hostapd_data *hapd);
void ap_handle_timer(void *eloop_ctx, void *timeout_ctx);
void ap_sta_replenish_timeout(struct hostapd_data *hapd, struct sta_info *sta,
			      u32 session_timeout);
void ap_sta_session_timeout(struct hostapd_data *hapd, struct sta_info *sta,
			    u32 session_timeout);
void ap_sta_no_session_timeout(struct hostapd_data *hapd,
			       struct sta_info *sta);
void ap_sta_session_warning_timeout(struct hostapd_data *hapd,
				    struct sta_info *sta, int warning_time);
struct sta_info * ap_sta_add(struct hostapd_data *hapd, const u8 *addr);
void ap_sta_disassociate(struct hostapd_data *hapd, struct sta_info *sta,
			 u16 reason);
void ap_sta_deauthenticate(struct hostapd_data *hapd, struct sta_info *sta,
			   u16 reason);
#ifdef CONFIG_WPS
int ap_sta_wps_cancel(struct hostapd_data *hapd,
		      struct sta_info *sta, void *ctx);
#endif /* CONFIG_WPS */
int ap_sta_bind_vlan(struct hostapd_data *hapd, struct sta_info *sta);
int ap_sta_set_vlan(struct hostapd_data *hapd, struct sta_info *sta,
		    struct vlan_description *vlan_desc);
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

void ap_sta_deauth_cb(struct hostapd_data *hapd, struct sta_info *sta);
void ap_sta_disassoc_cb(struct hostapd_data *hapd, struct sta_info *sta);
void ap_sta_clear_disconnect_timeouts(struct hostapd_data *hapd,
				      struct sta_info *sta);

int ap_sta_flags_txt(u32 flags, char *buf, size_t buflen);
void ap_sta_delayed_1x_auth_fail_disconnect(struct hostapd_data *hapd,
					    struct sta_info *sta);
int ap_sta_pending_delayed_1x_auth_fail_disconnect(struct hostapd_data *hapd,
						   struct sta_info *sta);

#endif /* STA_INFO_H */
