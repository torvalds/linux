/*
 * wpa_supplicant - WPA definitions
 * Copyright (c) 2003-2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WPA_H
#define WPA_H

#include "common/defs.h"
#include "common/eapol_common.h"
#include "common/wpa_common.h"
#include "common/ieee802_11_defs.h"

struct wpa_sm;
struct eapol_sm;
struct wpa_config_blob;
struct hostapd_freq_params;

struct wpa_sm_ctx {
	void *ctx; /* pointer to arbitrary upper level context */
	void *msg_ctx; /* upper level context for wpa_msg() calls */

	void (*set_state)(void *ctx, enum wpa_states state);
	enum wpa_states (*get_state)(void *ctx);
	void (*deauthenticate)(void * ctx, int reason_code);
	int (*set_key)(void *ctx, enum wpa_alg alg,
		       const u8 *addr, int key_idx, int set_tx,
		       const u8 *seq, size_t seq_len,
		       const u8 *key, size_t key_len);
	void * (*get_network_ctx)(void *ctx);
	int (*get_bssid)(void *ctx, u8 *bssid);
	int (*ether_send)(void *ctx, const u8 *dest, u16 proto, const u8 *buf,
			  size_t len);
	int (*get_beacon_ie)(void *ctx);
	void (*cancel_auth_timeout)(void *ctx);
	u8 * (*alloc_eapol)(void *ctx, u8 type, const void *data, u16 data_len,
			    size_t *msg_len, void **data_pos);
	int (*add_pmkid)(void *ctx, void *network_ctx, const u8 *bssid,
			 const u8 *pmkid, const u8 *fils_cache_id,
			 const u8 *pmk, size_t pmk_len);
	int (*remove_pmkid)(void *ctx, void *network_ctx, const u8 *bssid,
			    const u8 *pmkid, const u8 *fils_cache_id);
	void (*set_config_blob)(void *ctx, struct wpa_config_blob *blob);
	const struct wpa_config_blob * (*get_config_blob)(void *ctx,
							  const char *name);
	int (*mlme_setprotection)(void *ctx, const u8 *addr,
				  int protection_type, int key_type);
	int (*update_ft_ies)(void *ctx, const u8 *md, const u8 *ies,
			     size_t ies_len);
	int (*send_ft_action)(void *ctx, u8 action, const u8 *target_ap,
			      const u8 *ies, size_t ies_len);
	int (*mark_authenticated)(void *ctx, const u8 *target_ap);
#ifdef CONFIG_TDLS
	int (*tdls_get_capa)(void *ctx, int *tdls_supported,
			     int *tdls_ext_setup, int *tdls_chan_switch);
	int (*send_tdls_mgmt)(void *ctx, const u8 *dst,
			      u8 action_code, u8 dialog_token,
			      u16 status_code, u32 peer_capab,
			      int initiator, const u8 *buf, size_t len);
	int (*tdls_oper)(void *ctx, int oper, const u8 *peer);
	int (*tdls_peer_addset)(void *ctx, const u8 *addr, int add, u16 aid,
				u16 capability, const u8 *supp_rates,
				size_t supp_rates_len,
				const struct ieee80211_ht_capabilities *ht_capab,
				const struct ieee80211_vht_capabilities *vht_capab,
				u8 qosinfo, int wmm, const u8 *ext_capab,
				size_t ext_capab_len, const u8 *supp_channels,
				size_t supp_channels_len,
				const u8 *supp_oper_classes,
				size_t supp_oper_classes_len);
	int (*tdls_enable_channel_switch)(
		void *ctx, const u8 *addr, u8 oper_class,
		const struct hostapd_freq_params *params);
	int (*tdls_disable_channel_switch)(void *ctx, const u8 *addr);
#endif /* CONFIG_TDLS */
	void (*set_rekey_offload)(void *ctx, const u8 *kek, size_t kek_len,
				  const u8 *kck, size_t kck_len,
				  const u8 *replay_ctr);
	int (*key_mgmt_set_pmk)(void *ctx, const u8 *pmk, size_t pmk_len);
	void (*fils_hlp_rx)(void *ctx, const u8 *dst, const u8 *src,
			    const u8 *pkt, size_t pkt_len);
};


enum wpa_sm_conf_params {
	RSNA_PMK_LIFETIME /* dot11RSNAConfigPMKLifetime */,
	RSNA_PMK_REAUTH_THRESHOLD /* dot11RSNAConfigPMKReauthThreshold */,
	RSNA_SA_TIMEOUT /* dot11RSNAConfigSATimeout */,
	WPA_PARAM_PROTO,
	WPA_PARAM_PAIRWISE,
	WPA_PARAM_GROUP,
	WPA_PARAM_KEY_MGMT,
	WPA_PARAM_MGMT_GROUP,
	WPA_PARAM_RSN_ENABLED,
	WPA_PARAM_MFP
};

struct rsn_supp_config {
	void *network_ctx;
	int allowed_pairwise_cipher; /* bitfield of WPA_CIPHER_* */
	int proactive_key_caching;
	int eap_workaround;
	void *eap_conf_ctx;
	const u8 *ssid;
	size_t ssid_len;
	int wpa_ptk_rekey;
	int p2p;
	int wpa_rsc_relaxation;
	const u8 *fils_cache_id;
};

#ifndef CONFIG_NO_WPA

struct wpa_sm * wpa_sm_init(struct wpa_sm_ctx *ctx);
void wpa_sm_deinit(struct wpa_sm *sm);
void wpa_sm_notify_assoc(struct wpa_sm *sm, const u8 *bssid);
void wpa_sm_notify_disassoc(struct wpa_sm *sm);
void wpa_sm_set_pmk(struct wpa_sm *sm, const u8 *pmk, size_t pmk_len,
		    const u8 *pmkid, const u8 *bssid);
void wpa_sm_set_pmk_from_pmksa(struct wpa_sm *sm);
void wpa_sm_set_fast_reauth(struct wpa_sm *sm, int fast_reauth);
void wpa_sm_set_scard_ctx(struct wpa_sm *sm, void *scard_ctx);
void wpa_sm_set_config(struct wpa_sm *sm, struct rsn_supp_config *config);
void wpa_sm_set_own_addr(struct wpa_sm *sm, const u8 *addr);
void wpa_sm_set_ifname(struct wpa_sm *sm, const char *ifname,
		       const char *bridge_ifname);
void wpa_sm_set_eapol(struct wpa_sm *sm, struct eapol_sm *eapol);
int wpa_sm_set_assoc_wpa_ie(struct wpa_sm *sm, const u8 *ie, size_t len);
int wpa_sm_set_assoc_wpa_ie_default(struct wpa_sm *sm, u8 *wpa_ie,
				    size_t *wpa_ie_len);
int wpa_sm_set_ap_wpa_ie(struct wpa_sm *sm, const u8 *ie, size_t len);
int wpa_sm_set_ap_rsn_ie(struct wpa_sm *sm, const u8 *ie, size_t len);
int wpa_sm_get_mib(struct wpa_sm *sm, char *buf, size_t buflen);

int wpa_sm_set_param(struct wpa_sm *sm, enum wpa_sm_conf_params param,
		     unsigned int value);

int wpa_sm_get_status(struct wpa_sm *sm, char *buf, size_t buflen,
		      int verbose);
int wpa_sm_pmf_enabled(struct wpa_sm *sm);

void wpa_sm_key_request(struct wpa_sm *sm, int error, int pairwise);

int wpa_parse_wpa_ie(const u8 *wpa_ie, size_t wpa_ie_len,
		     struct wpa_ie_data *data);

void wpa_sm_aborted_cached(struct wpa_sm *sm);
int wpa_sm_rx_eapol(struct wpa_sm *sm, const u8 *src_addr,
		    const u8 *buf, size_t len);
int wpa_sm_parse_own_wpa_ie(struct wpa_sm *sm, struct wpa_ie_data *data);
int wpa_sm_pmksa_cache_list(struct wpa_sm *sm, char *buf, size_t len);
struct rsn_pmksa_cache_entry * wpa_sm_pmksa_cache_head(struct wpa_sm *sm);
struct rsn_pmksa_cache_entry *
wpa_sm_pmksa_cache_add_entry(struct wpa_sm *sm,
			     struct rsn_pmksa_cache_entry * entry);
void wpa_sm_pmksa_cache_add(struct wpa_sm *sm, const u8 *pmk, size_t pmk_len,
			    const u8 *pmkid, const u8 *bssid,
			    const u8 *fils_cache_id);
int wpa_sm_pmksa_exists(struct wpa_sm *sm, const u8 *bssid,
			const void *network_ctx);
void wpa_sm_drop_sa(struct wpa_sm *sm);
int wpa_sm_has_ptk(struct wpa_sm *sm);

void wpa_sm_update_replay_ctr(struct wpa_sm *sm, const u8 *replay_ctr);

void wpa_sm_pmksa_cache_flush(struct wpa_sm *sm, void *network_ctx);

int wpa_sm_get_p2p_ip_addr(struct wpa_sm *sm, u8 *buf);

void wpa_sm_set_rx_replay_ctr(struct wpa_sm *sm, const u8 *rx_replay_counter);
void wpa_sm_set_ptk_kck_kek(struct wpa_sm *sm,
			    const u8 *ptk_kck, size_t ptk_kck_len,
			    const u8 *ptk_kek, size_t ptk_kek_len);
int wpa_fils_is_completed(struct wpa_sm *sm);

#else /* CONFIG_NO_WPA */

static inline struct wpa_sm * wpa_sm_init(struct wpa_sm_ctx *ctx)
{
	return (struct wpa_sm *) 1;
}

static inline void wpa_sm_deinit(struct wpa_sm *sm)
{
}

static inline void wpa_sm_notify_assoc(struct wpa_sm *sm, const u8 *bssid)
{
}

static inline void wpa_sm_notify_disassoc(struct wpa_sm *sm)
{
}

static inline void wpa_sm_set_pmk(struct wpa_sm *sm, const u8 *pmk,
				  size_t pmk_len, const u8 *pmkid,
				  const u8 *bssid)
{
}

static inline void wpa_sm_set_pmk_from_pmksa(struct wpa_sm *sm)
{
}

static inline void wpa_sm_set_fast_reauth(struct wpa_sm *sm, int fast_reauth)
{
}

static inline void wpa_sm_set_scard_ctx(struct wpa_sm *sm, void *scard_ctx)
{
}

static inline void wpa_sm_set_config(struct wpa_sm *sm,
				     struct rsn_supp_config *config)
{
}

static inline void wpa_sm_set_own_addr(struct wpa_sm *sm, const u8 *addr)
{
}

static inline void wpa_sm_set_ifname(struct wpa_sm *sm, const char *ifname,
				     const char *bridge_ifname)
{
}

static inline void wpa_sm_set_eapol(struct wpa_sm *sm, struct eapol_sm *eapol)
{
}

static inline int wpa_sm_set_assoc_wpa_ie(struct wpa_sm *sm, const u8 *ie,
					  size_t len)
{
	return -1;
}

static inline int wpa_sm_set_assoc_wpa_ie_default(struct wpa_sm *sm,
						  u8 *wpa_ie,
						  size_t *wpa_ie_len)
{
	return -1;
}

static inline int wpa_sm_set_ap_wpa_ie(struct wpa_sm *sm, const u8 *ie,
				       size_t len)
{
	return -1;
}

static inline int wpa_sm_set_ap_rsn_ie(struct wpa_sm *sm, const u8 *ie,
				       size_t len)
{
	return -1;
}

static inline int wpa_sm_get_mib(struct wpa_sm *sm, char *buf, size_t buflen)
{
	return 0;
}

static inline int wpa_sm_set_param(struct wpa_sm *sm,
				   enum wpa_sm_conf_params param,
				   unsigned int value)
{
	return -1;
}

static inline int wpa_sm_get_status(struct wpa_sm *sm, char *buf,
				    size_t buflen, int verbose)
{
	return 0;
}

static inline int wpa_sm_pmf_enabled(struct wpa_sm *sm)
{
	return 0;
}

static inline void wpa_sm_key_request(struct wpa_sm *sm, int error,
				      int pairwise)
{
}

static inline int wpa_parse_wpa_ie(const u8 *wpa_ie, size_t wpa_ie_len,
				   struct wpa_ie_data *data)
{
	return -1;
}

static inline void wpa_sm_aborted_cached(struct wpa_sm *sm)
{
}

static inline int wpa_sm_rx_eapol(struct wpa_sm *sm, const u8 *src_addr,
				  const u8 *buf, size_t len)
{
	return -1;
}

static inline int wpa_sm_parse_own_wpa_ie(struct wpa_sm *sm,
					  struct wpa_ie_data *data)
{
	return -1;
}

static inline int wpa_sm_pmksa_cache_list(struct wpa_sm *sm, char *buf,
					  size_t len)
{
	return -1;
}

static inline void wpa_sm_drop_sa(struct wpa_sm *sm)
{
}

static inline int wpa_sm_has_ptk(struct wpa_sm *sm)
{
	return 0;
}

static inline void wpa_sm_update_replay_ctr(struct wpa_sm *sm,
					    const u8 *replay_ctr)
{
}

static inline void wpa_sm_pmksa_cache_flush(struct wpa_sm *sm,
					    void *network_ctx)
{
}

static inline void wpa_sm_set_rx_replay_ctr(struct wpa_sm *sm,
					    const u8 *rx_replay_counter)
{
}

static inline void wpa_sm_set_ptk_kck_kek(struct wpa_sm *sm, const u8 *ptk_kck,
					  size_t ptk_kck_len,
					  const u8 *ptk_kek, size_t ptk_kek_len)
{
}

static inline int wpa_fils_is_completed(struct wpa_sm *sm)
{
	return 0;
}

#endif /* CONFIG_NO_WPA */

#ifdef CONFIG_IEEE80211R

int wpa_sm_set_ft_params(struct wpa_sm *sm, const u8 *ies, size_t ies_len);
int wpa_ft_prepare_auth_request(struct wpa_sm *sm, const u8 *mdie);
int wpa_ft_add_mdie(struct wpa_sm *sm, u8 *ies, size_t ies_len,
		    const u8 *mdie);
const u8 * wpa_sm_get_ft_md(struct wpa_sm *sm);
int wpa_ft_process_response(struct wpa_sm *sm, const u8 *ies, size_t ies_len,
			    int ft_action, const u8 *target_ap,
			    const u8 *ric_ies, size_t ric_ies_len);
int wpa_ft_is_completed(struct wpa_sm *sm);
void wpa_reset_ft_completed(struct wpa_sm *sm);
int wpa_ft_validate_reassoc_resp(struct wpa_sm *sm, const u8 *ies,
				 size_t ies_len, const u8 *src_addr);
int wpa_ft_start_over_ds(struct wpa_sm *sm, const u8 *target_ap,
			 const u8 *mdie);

#else /* CONFIG_IEEE80211R */

static inline int
wpa_sm_set_ft_params(struct wpa_sm *sm, const u8 *ies, size_t ies_len)
{
	return 0;
}

static inline int wpa_ft_prepare_auth_request(struct wpa_sm *sm,
					      const u8 *mdie)
{
	return 0;
}

static inline int wpa_ft_add_mdie(struct wpa_sm *sm, u8 *ies, size_t ies_len,
				  const u8 *mdie)
{
	return 0;
}

static inline int
wpa_ft_process_response(struct wpa_sm *sm, const u8 *ies, size_t ies_len,
			int ft_action, const u8 *target_ap)
{
	return 0;
}

static inline int wpa_ft_is_completed(struct wpa_sm *sm)
{
	return 0;
}

static inline void wpa_reset_ft_completed(struct wpa_sm *sm)
{
}

static inline int
wpa_ft_validate_reassoc_resp(struct wpa_sm *sm, const u8 *ies, size_t ies_len,
			     const u8 *src_addr)
{
	return -1;
}

#endif /* CONFIG_IEEE80211R */


/* tdls.c */
void wpa_tdls_ap_ies(struct wpa_sm *sm, const u8 *ies, size_t len);
void wpa_tdls_assoc_resp_ies(struct wpa_sm *sm, const u8 *ies, size_t len);
int wpa_tdls_start(struct wpa_sm *sm, const u8 *addr);
void wpa_tdls_remove(struct wpa_sm *sm, const u8 *addr);
int wpa_tdls_teardown_link(struct wpa_sm *sm, const u8 *addr, u16 reason_code);
int wpa_tdls_send_discovery_request(struct wpa_sm *sm, const u8 *addr);
int wpa_tdls_init(struct wpa_sm *sm);
void wpa_tdls_teardown_peers(struct wpa_sm *sm);
void wpa_tdls_deinit(struct wpa_sm *sm);
void wpa_tdls_enable(struct wpa_sm *sm, int enabled);
void wpa_tdls_disable_unreachable_link(struct wpa_sm *sm, const u8 *addr);
const char * wpa_tdls_get_link_status(struct wpa_sm *sm, const u8 *addr);
int wpa_tdls_is_external_setup(struct wpa_sm *sm);
int wpa_tdls_enable_chan_switch(struct wpa_sm *sm, const u8 *addr,
				u8 oper_class,
				struct hostapd_freq_params *freq_params);
int wpa_tdls_disable_chan_switch(struct wpa_sm *sm, const u8 *addr);
#ifdef CONFIG_TDLS_TESTING
extern unsigned int tdls_testing;
#endif /* CONFIG_TDLS_TESTING */


int wpa_wnmsleep_install_key(struct wpa_sm *sm, u8 subelem_id, u8 *buf);
void wpa_sm_set_test_assoc_ie(struct wpa_sm *sm, struct wpabuf *buf);
const u8 * wpa_sm_get_anonce(struct wpa_sm *sm);
unsigned int wpa_sm_get_key_mgmt(struct wpa_sm *sm);

struct wpabuf * fils_build_auth(struct wpa_sm *sm, int dh_group, const u8 *md);
int fils_process_auth(struct wpa_sm *sm, const u8 *bssid, const u8 *data,
		      size_t len);
struct wpabuf * fils_build_assoc_req(struct wpa_sm *sm, const u8 **kek,
				     size_t *kek_len, const u8 **snonce,
				     const u8 **anonce,
				     const struct wpabuf **hlp,
				     unsigned int num_hlp);
int fils_process_assoc_resp(struct wpa_sm *sm, const u8 *resp, size_t len);

struct wpabuf * owe_build_assoc_req(struct wpa_sm *sm, u16 group);
int owe_process_assoc_resp(struct wpa_sm *sm, const u8 *bssid,
			   const u8 *resp_ies, size_t resp_ies_len);

void wpa_sm_set_reset_fils_completed(struct wpa_sm *sm, int set);
void wpa_sm_set_fils_cache_id(struct wpa_sm *sm, const u8 *fils_cache_id);

#endif /* WPA_H */
