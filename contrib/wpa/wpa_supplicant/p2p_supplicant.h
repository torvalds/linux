/*
 * wpa_supplicant - P2P
 * Copyright (c) 2009-2010, Atheros Communications
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef P2P_SUPPLICANT_H
#define P2P_SUPPLICANT_H

enum p2p_wps_method;
struct p2p_go_neg_results;
enum p2p_send_action_result;
struct p2p_peer_info;
struct p2p_channels;
struct wps_event_fail;
struct p2ps_provision;

enum wpas_p2p_channel_update_trig {
	WPAS_P2P_CHANNEL_UPDATE_ANY,
	WPAS_P2P_CHANNEL_UPDATE_DRIVER,
	WPAS_P2P_CHANNEL_UPDATE_STATE_CHANGE,
	WPAS_P2P_CHANNEL_UPDATE_AVOID,
	WPAS_P2P_CHANNEL_UPDATE_DISALLOW,
	WPAS_P2P_CHANNEL_UPDATE_CS,
};

int wpas_p2p_add_p2pdev_interface(struct wpa_supplicant *wpa_s,
				  const char *conf_p2p_dev);
struct wpa_supplicant * wpas_get_p2p_go_iface(struct wpa_supplicant *wpa_s,
					      const u8 *ssid, size_t ssid_len);
struct wpa_supplicant * wpas_get_p2p_client_iface(struct wpa_supplicant *wpa_s,
						  const u8 *peer_dev_addr);
int wpas_p2p_connect(struct wpa_supplicant *wpa_s, const u8 *peer_addr,
		     const char *pin, enum p2p_wps_method wps_method,
		     int persistent_group, int auto_join, int join, int auth,
		     int go_intent, int freq, unsigned int vht_center_freq2,
		     int persistent_id, int pd, int ht40, int vht,
		     unsigned int vht_chwidth, const u8 *group_ssid,
		     size_t group_ssid_len);
int wpas_p2p_handle_frequency_conflicts(struct wpa_supplicant *wpa_s,
                                          int freq, struct wpa_ssid *ssid);
int wpas_p2p_group_add(struct wpa_supplicant *wpa_s, int persistent_group,
		       int freq, int vht_center_freq2, int ht40, int vht,
		       int max_oper_chwidth);
int wpas_p2p_group_add_persistent(struct wpa_supplicant *wpa_s,
				  struct wpa_ssid *ssid, int addr_allocated,
				  int force_freq, int neg_freq,
				  int vht_center_freq2, int ht40,
				  int vht, int max_oper_chwidth,
				  const struct p2p_channels *channels,
				  int connection_timeout, int force_scan);
struct p2p_group * wpas_p2p_group_init(struct wpa_supplicant *wpa_s,
				       struct wpa_ssid *ssid);
enum wpas_p2p_prov_disc_use {
	WPAS_P2P_PD_FOR_GO_NEG,
	WPAS_P2P_PD_FOR_JOIN,
	WPAS_P2P_PD_AUTO,
	WPAS_P2P_PD_FOR_ASP
};
int wpas_p2p_prov_disc(struct wpa_supplicant *wpa_s, const u8 *peer_addr,
		       const char *config_method,
		       enum wpas_p2p_prov_disc_use use,
		       struct p2ps_provision *p2ps_prov);
void wpas_send_action_tx_status(struct wpa_supplicant *wpa_s, const u8 *dst,
				const u8 *data, size_t data_len,
				enum p2p_send_action_result result);
int wpas_p2p_scan_result_text(const u8 *ies, size_t ies_len, char *buf,
			      char *end);
enum p2p_discovery_type;
int wpas_p2p_find(struct wpa_supplicant *wpa_s, unsigned int timeout,
		  enum p2p_discovery_type type,
		  unsigned int num_req_dev_types, const u8 *req_dev_types,
		  const u8 *dev_id, unsigned int search_delay,
		  u8 seek_cnt, const char **seek_string, int freq);
void wpas_p2p_stop_find(struct wpa_supplicant *wpa_s);
int wpas_p2p_listen(struct wpa_supplicant *wpa_s, unsigned int timeout);
int wpas_p2p_listen_start(struct wpa_supplicant *wpa_s, unsigned int timeout);
int wpas_p2p_assoc_req_ie(struct wpa_supplicant *wpa_s, struct wpa_bss *bss,
			  u8 *buf, size_t len, int p2p_group);
void wpas_p2p_scan_ie(struct wpa_supplicant *wpa_s, struct wpabuf *ies);
u64 wpas_p2p_sd_request(struct wpa_supplicant *wpa_s, const u8 *dst,
			const struct wpabuf *tlvs);
u64 wpas_p2p_sd_request_asp(struct wpa_supplicant *wpa_s, const u8 *dst, u8 id,
			    const char *svc_str, const char *info_substr);
u64 wpas_p2p_sd_request_upnp(struct wpa_supplicant *wpa_s, const u8 *dst,
			     u8 version, const char *query);
u64 wpas_p2p_sd_request_wifi_display(struct wpa_supplicant *wpa_s,
				     const u8 *dst, const char *role);
int wpas_p2p_sd_cancel_request(struct wpa_supplicant *wpa_s, u64 req);
void wpas_p2p_sd_response(struct wpa_supplicant *wpa_s, int freq,
			  const u8 *dst, u8 dialog_token,
			  const struct wpabuf *resp_tlvs);
void wpas_p2p_sd_service_update(struct wpa_supplicant *wpa_s);
void wpas_p2p_service_flush(struct wpa_supplicant *wpa_s);
int wpas_p2p_service_add_bonjour(struct wpa_supplicant *wpa_s,
				 struct wpabuf *query, struct wpabuf *resp);
int wpas_p2p_service_del_bonjour(struct wpa_supplicant *wpa_s,
				 const struct wpabuf *query);
int wpas_p2p_service_add_upnp(struct wpa_supplicant *wpa_s, u8 version,
			      const char *service);
int wpas_p2p_service_del_upnp(struct wpa_supplicant *wpa_s, u8 version,
			      const char *service);
int wpas_p2p_service_add_asp(struct wpa_supplicant *wpa_s, int auto_accept,
			     u32 adv_id, const char *adv_str, u8 svc_state,
			     u16 config_methods, const char *svc_info,
			     const u8 *cpt_priority);
int wpas_p2p_service_del_asp(struct wpa_supplicant *wpa_s, u32 adv_id);
void wpas_p2p_service_flush_asp(struct wpa_supplicant *wpa_s);
int wpas_p2p_service_p2ps_id_exists(struct wpa_supplicant *wpa_s, u32 adv_id);
void wpas_sd_request(void *ctx, int freq, const u8 *sa, u8 dialog_token,
		     u16 update_indic, const u8 *tlvs, size_t tlvs_len);
void wpas_sd_response(void *ctx, const u8 *sa, u16 update_indic,
		      const u8 *tlvs, size_t tlvs_len);
int wpas_p2p_reject(struct wpa_supplicant *wpa_s, const u8 *addr);
int wpas_p2p_invite(struct wpa_supplicant *wpa_s, const u8 *peer_addr,
		    struct wpa_ssid *ssid, const u8 *go_dev_addr, int freq,
		    int vht_center_freq2, int ht40, int vht,
		    int max_oper_chwidth, int pref_freq);
int wpas_p2p_invite_group(struct wpa_supplicant *wpa_s, const char *ifname,
			  const u8 *peer_addr, const u8 *go_dev_addr);
int wpas_p2p_presence_req(struct wpa_supplicant *wpa_s, u32 duration1,
			  u32 interval1, u32 duration2, u32 interval2);
int wpas_p2p_ext_listen(struct wpa_supplicant *wpa_s, unsigned int period,
			unsigned int interval);
int wpas_p2p_deauth_notif(struct wpa_supplicant *wpa_s, const u8 *bssid,
			  u16 reason_code, const u8 *ie, size_t ie_len,
			  int locally_generated);
void wpas_p2p_disassoc_notif(struct wpa_supplicant *wpa_s, const u8 *bssid,
			     u16 reason_code, const u8 *ie, size_t ie_len,
			     int locally_generated);
int wpas_p2p_set_noa(struct wpa_supplicant *wpa_s, u8 count, int start,
		     int duration);
int wpas_p2p_set_cross_connect(struct wpa_supplicant *wpa_s, int enabled);
int wpas_p2p_cancel(struct wpa_supplicant *wpa_s);
int wpas_p2p_unauthorize(struct wpa_supplicant *wpa_s, const char *addr);
int wpas_p2p_disconnect(struct wpa_supplicant *wpa_s);
struct wpa_ssid * wpas_p2p_get_persistent(struct wpa_supplicant *wpa_s,
					  const u8 *addr, const u8 *ssid,
					  size_t ssid_len);
void wpas_p2p_notify_ap_sta_authorized(struct wpa_supplicant *wpa_s,
				       const u8 *addr);
int wpas_p2p_scan_no_go_seen(struct wpa_supplicant *wpa_s);
int wpas_p2p_get_ht40_mode(struct wpa_supplicant *wpa_s,
			   struct hostapd_hw_modes *mode, u8 channel);
int wpas_p2p_get_vht80_center(struct wpa_supplicant *wpa_s,
			      struct hostapd_hw_modes *mode, u8 channel);
int wpas_p2p_get_vht160_center(struct wpa_supplicant *wpa_s,
			       struct hostapd_hw_modes *mode, u8 channel);
unsigned int wpas_p2p_search_delay(struct wpa_supplicant *wpa_s);
void wpas_p2p_new_psk_cb(struct wpa_supplicant *wpa_s, const u8 *mac_addr,
			 const u8 *p2p_dev_addr,
			 const u8 *psk, size_t psk_len);
void wpas_p2p_remove_client(struct wpa_supplicant *wpa_s, const u8 *peer,
			    int iface_addr);
struct wpabuf * wpas_p2p_nfc_handover_req(struct wpa_supplicant *wpa_s,
					  int ndef);
struct wpabuf * wpas_p2p_nfc_handover_sel(struct wpa_supplicant *wpa_s,
					  int ndef, int tag);
int wpas_p2p_nfc_tag_process(struct wpa_supplicant *wpa_s,
			     const struct wpabuf *data, int forced_freq);
int wpas_p2p_nfc_report_handover(struct wpa_supplicant *wpa_s, int init,
				 const struct wpabuf *req,
				 const struct wpabuf *sel, int forced_freq);
int wpas_p2p_nfc_tag_enabled(struct wpa_supplicant *wpa_s, int enabled);
void wpas_p2p_pbc_overlap_cb(void *eloop_ctx, void *timeout_ctx);

#ifdef CONFIG_P2P

int wpas_p2p_init(struct wpa_global *global, struct wpa_supplicant *wpa_s);
void wpas_p2p_deinit(struct wpa_supplicant *wpa_s);
void wpas_p2p_completed(struct wpa_supplicant *wpa_s);
void wpas_p2p_update_config(struct wpa_supplicant *wpa_s);
int wpas_p2p_probe_req_rx(struct wpa_supplicant *wpa_s, const u8 *addr,
			  const u8 *dst, const u8 *bssid,
			  const u8 *ie, size_t ie_len,
			  unsigned int rx_freq, int ssi_signal);
void wpas_p2p_wps_success(struct wpa_supplicant *wpa_s, const u8 *peer_addr,
			  int registrar);

void wpas_p2p_update_channel_list(struct wpa_supplicant *wpa_s,
				  enum wpas_p2p_channel_update_trig trig);

void wpas_p2p_update_best_channels(struct wpa_supplicant *wpa_s,
				   int freq_24, int freq_5, int freq_overall);
void wpas_p2p_rx_action(struct wpa_supplicant *wpa_s, const u8 *da,
			const u8 *sa, const u8 *bssid,
			u8 category, const u8 *data, size_t len, int freq);
void wpas_p2p_remain_on_channel_cb(struct wpa_supplicant *wpa_s,
				   unsigned int freq, unsigned int duration);
void wpas_p2p_cancel_remain_on_channel_cb(struct wpa_supplicant *wpa_s,
					  unsigned int freq);
void wpas_p2p_interface_unavailable(struct wpa_supplicant *wpa_s);
void wpas_p2p_notif_connected(struct wpa_supplicant *wpa_s);
void wpas_p2p_notif_disconnected(struct wpa_supplicant *wpa_s);
int wpas_p2p_notif_pbc_overlap(struct wpa_supplicant *wpa_s);
int wpas_p2p_4way_hs_failed(struct wpa_supplicant *wpa_s);
void wpas_p2p_ap_setup_failed(struct wpa_supplicant *wpa_s);
void wpas_p2p_indicate_state_change(struct wpa_supplicant *wpa_s);
void wpas_p2p_deinit_iface(struct wpa_supplicant *wpa_s);
void wpas_p2p_ap_deinit(struct wpa_supplicant *wpa_s);
void wpas_p2p_network_removed(struct wpa_supplicant *wpa_s,
			      struct wpa_ssid *ssid);
int wpas_p2p_in_progress(struct wpa_supplicant *wpa_s);
int wpas_p2p_wps_eapol_cb(struct wpa_supplicant *wpa_s);
void wpas_p2p_wps_failed(struct wpa_supplicant *wpa_s,
			 struct wps_event_fail *fail);
int wpas_p2p_group_remove(struct wpa_supplicant *wpa_s, const char *ifname);
int wpas_p2p_lo_start(struct wpa_supplicant *wpa_s, unsigned int freq,
		      unsigned int period, unsigned int interval,
		      unsigned int count);
int wpas_p2p_lo_stop(struct wpa_supplicant *wpa_s);

#else /* CONFIG_P2P */

static inline int
wpas_p2p_init(struct wpa_global *global, struct wpa_supplicant *wpa_s)
{
	return 0;
}

static inline void wpas_p2p_deinit(struct wpa_supplicant *wpa_s)
{
}

static inline void wpas_p2p_completed(struct wpa_supplicant *wpa_s)
{
}

static inline void wpas_p2p_update_config(struct wpa_supplicant *wpa_s)
{
}

static inline int wpas_p2p_probe_req_rx(struct wpa_supplicant *wpa_s,
					const u8 *addr,
					const u8 *dst, const u8 *bssid,
					const u8 *ie, size_t ie_len,
					unsigned int rx_freq, int ssi_signal)
{
	return 0;
}

static inline void wpas_p2p_wps_success(struct wpa_supplicant *wpa_s,
					const u8 *peer_addr, int registrar)
{
}

static inline void
wpas_p2p_update_channel_list(struct wpa_supplicant *wpa_s,
			     enum wpas_p2p_channel_update_trig trig)
{
}

static inline void wpas_p2p_update_best_channels(struct wpa_supplicant *wpa_s,
						 int freq_24, int freq_5,
						 int freq_overall)
{
}

static inline void wpas_p2p_rx_action(struct wpa_supplicant *wpa_s,
				      const u8 *da,
				      const u8 *sa, const u8 *bssid,
				      u8 category, const u8 *data, size_t len,
				      int freq)
{
}

static inline void wpas_p2p_remain_on_channel_cb(struct wpa_supplicant *wpa_s,
						 unsigned int freq,
						 unsigned int duration)
{
}

static inline void
wpas_p2p_cancel_remain_on_channel_cb(struct wpa_supplicant *wpa_s,
				     unsigned int freq)
{
}

static inline void wpas_p2p_interface_unavailable(struct wpa_supplicant *wpa_s)
{
}

static inline void wpas_p2p_notif_connected(struct wpa_supplicant *wpa_s)
{
}

static inline void wpas_p2p_notif_disconnected(struct wpa_supplicant *wpa_s)
{
}

static inline int wpas_p2p_notif_pbc_overlap(struct wpa_supplicant *wpa_s)
{
	return 0;
}

static inline int wpas_p2p_4way_hs_failed(struct wpa_supplicant *wpa_s)
{
	return 0;
}

static inline void wpas_p2p_ap_setup_failed(struct wpa_supplicant *wpa_s)
{
}

static inline void wpas_p2p_indicate_state_change(struct wpa_supplicant *wpa_s)
{
}

static inline void wpas_p2p_deinit_iface(struct wpa_supplicant *wpa_s)
{
}

static inline void wpas_p2p_ap_deinit(struct wpa_supplicant *wpa_s)
{
}

static inline void wpas_p2p_network_removed(struct wpa_supplicant *wpa_s,
					    struct wpa_ssid *ssid)
{
}

static inline int wpas_p2p_in_progress(struct wpa_supplicant *wpa_s)
{
	return 0;
}

static inline int wpas_p2p_wps_eapol_cb(struct wpa_supplicant *wpa_s)
{
	return 0;
}

static inline void wpas_p2p_wps_failed(struct wpa_supplicant *wpa_s,
				       struct wps_event_fail *fail)
{
}

static inline int wpas_p2p_group_remove(struct wpa_supplicant *wpa_s,
					const char *ifname)
{
	return 0;
}

#endif /* CONFIG_P2P */

#endif /* P2P_SUPPLICANT_H */
