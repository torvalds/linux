/*
 * WPA Supplicant - Basic AP mode support routines
 * Copyright (c) 2003-2009, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2009, Atheros Communications
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef AP_H
#define AP_H

int wpa_supplicant_create_ap(struct wpa_supplicant *wpa_s,
			     struct wpa_ssid *ssid);
void wpa_supplicant_ap_deinit(struct wpa_supplicant *wpa_s);
void wpa_supplicant_ap_rx_eapol(struct wpa_supplicant *wpa_s,
				const u8 *src_addr, const u8 *buf, size_t len);
int wpa_supplicant_ap_wps_pbc(struct wpa_supplicant *wpa_s, const u8 *bssid,
			      const u8 *p2p_dev_addr);
int wpa_supplicant_ap_wps_pin(struct wpa_supplicant *wpa_s, const u8 *bssid,
			      const char *pin, char *buf, size_t buflen,
			      int timeout);
int wpa_supplicant_ap_wps_cancel(struct wpa_supplicant *wpa_s);
void wpas_wps_ap_pin_disable(struct wpa_supplicant *wpa_s);
const char * wpas_wps_ap_pin_random(struct wpa_supplicant *wpa_s, int timeout);
const char * wpas_wps_ap_pin_get(struct wpa_supplicant *wpa_s);
int wpas_wps_ap_pin_set(struct wpa_supplicant *wpa_s, const char *pin,
			int timeout);
int ap_ctrl_iface_sta_first(struct wpa_supplicant *wpa_s,
			    char *buf, size_t buflen);
int ap_ctrl_iface_sta(struct wpa_supplicant *wpa_s, const char *txtaddr,
		      char *buf, size_t buflen);
int ap_ctrl_iface_sta_next(struct wpa_supplicant *wpa_s, const char *txtaddr,
			   char *buf, size_t buflen);
int ap_ctrl_iface_sta_deauthenticate(struct wpa_supplicant *wpa_s,
				     const char *txtaddr);
int ap_ctrl_iface_sta_disassociate(struct wpa_supplicant *wpa_s,
				   const char *txtaddr);
int ap_ctrl_iface_wpa_get_status(struct wpa_supplicant *wpa_s, char *buf,
				 size_t buflen, int verbose);
void ap_tx_status(void *ctx, const u8 *addr,
		  const u8 *buf, size_t len, int ack);
void ap_eapol_tx_status(void *ctx, const u8 *dst,
			const u8 *data, size_t len, int ack);
void ap_client_poll_ok(void *ctx, const u8 *addr);
void ap_rx_from_unknown_sta(void *ctx, const u8 *addr, int wds);
void ap_mgmt_rx(void *ctx, struct rx_mgmt *rx_mgmt);
void ap_mgmt_tx_cb(void *ctx, const u8 *buf, size_t len, u16 stype, int ok);
int wpa_supplicant_ap_update_beacon(struct wpa_supplicant *wpa_s);
int wpa_supplicant_ap_mac_addr_filter(struct wpa_supplicant *wpa_s,
				      const u8 *addr);
void wpa_supplicant_ap_pwd_auth_fail(struct wpa_supplicant *wpa_s);
int ap_switch_channel(struct wpa_supplicant *wpa_s,
		      struct csa_settings *settings);
int ap_ctrl_iface_chanswitch(struct wpa_supplicant *wpa_s, const char *txtaddr);
void wpas_ap_ch_switch(struct wpa_supplicant *wpa_s, int freq, int ht,
		       int offset, int width, int cf1, int cf2);
struct wpabuf * wpas_ap_wps_nfc_config_token(struct wpa_supplicant *wpa_s,
					     int ndef);
#ifdef CONFIG_AP
struct wpabuf * wpas_ap_wps_nfc_handover_sel(struct wpa_supplicant *wpa_s,
					     int ndef);
#else /* CONFIG_AP */
static inline struct wpabuf *
wpas_ap_wps_nfc_handover_sel(struct wpa_supplicant *wpa_s,
			     int ndef)
{
	return NULL;
}
#endif /* CONFIG_AP */

int wpas_ap_wps_nfc_report_handover(struct wpa_supplicant *wpa_s,
				    const struct wpabuf *req,
				    const struct wpabuf *sel);
int wpas_ap_wps_add_nfc_pw(struct wpa_supplicant *wpa_s, u16 pw_id,
			   const struct wpabuf *pw, const u8 *pubkey_hash);

struct hostapd_config;
int wpa_supplicant_conf_ap_ht(struct wpa_supplicant *wpa_s,
			      struct wpa_ssid *ssid,
			      struct hostapd_config *conf);

int wpas_ap_stop_ap(struct wpa_supplicant *wpa_s);

int wpas_ap_pmksa_cache_list(struct wpa_supplicant *wpa_s, char *buf,
			     size_t len);
void wpas_ap_pmksa_cache_flush(struct wpa_supplicant *wpa_s);
int wpas_ap_pmksa_cache_list_mesh(struct wpa_supplicant *wpa_s, const u8 *addr,
				  char *buf, size_t len);
int wpas_ap_pmksa_cache_add_external(struct wpa_supplicant *wpa_s, char *cmd);

void wpas_ap_event_dfs_radar_detected(struct wpa_supplicant *wpa_s,
				      struct dfs_event *radar);
void wpas_ap_event_dfs_cac_started(struct wpa_supplicant *wpa_s,
				   struct dfs_event *radar);
void wpas_ap_event_dfs_cac_finished(struct wpa_supplicant *wpa_s,
				    struct dfs_event *radar);
void wpas_ap_event_dfs_cac_aborted(struct wpa_supplicant *wpa_s,
				   struct dfs_event *radar);
void wpas_ap_event_dfs_cac_nop_finished(struct wpa_supplicant *wpa_s,
					struct dfs_event *radar);

void ap_periodic(struct wpa_supplicant *wpa_s);

#endif /* AP_H */
