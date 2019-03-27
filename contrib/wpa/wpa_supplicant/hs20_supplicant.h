/*
 * Copyright (c) 2011-2013, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef HS20_SUPPLICANT_H
#define HS20_SUPPLICANT_H

void hs20_configure_frame_filters(struct wpa_supplicant *wpa_s);
void wpas_hs20_add_indication(struct wpabuf *buf, int pps_mo_id);
void wpas_hs20_add_roam_cons_sel(struct wpabuf *buf,
				 const struct wpa_ssid *ssid);

int hs20_anqp_send_req(struct wpa_supplicant *wpa_s, const u8 *dst, u32 stypes,
		       const u8 *payload, size_t payload_len, int inmem);
void hs20_put_anqp_req(u32 stypes, const u8 *payload, size_t payload_len,
		       struct wpabuf *buf);
void hs20_parse_rx_hs20_anqp_resp(struct wpa_supplicant *wpa_s,
				  struct wpa_bss *bss, const u8 *sa,
				  const u8 *data, size_t slen, u8 dialog_token);
int is_hs20_network(struct wpa_supplicant *wpa_s, struct wpa_ssid *ssid,
		    struct wpa_bss *bss);
int hs20_get_pps_mo_id(struct wpa_supplicant *wpa_s, struct wpa_ssid *ssid);
void hs20_notify_parse_done(struct wpa_supplicant *wpa_s);

void hs20_rx_subscription_remediation(struct wpa_supplicant *wpa_s,
				      const char *url, u8 osu_method);
void hs20_rx_deauth_imminent_notice(struct wpa_supplicant *wpa_s, u8 code,
				    u16 reauth_delay, const char *url);
void hs20_rx_t_c_acceptance(struct wpa_supplicant *wpa_s, const char *url);

void hs20_free_osu_prov(struct wpa_supplicant *wpa_s);
void hs20_next_osu_icon(struct wpa_supplicant *wpa_s);
void hs20_osu_icon_fetch(struct wpa_supplicant *wpa_s);
int hs20_fetch_osu(struct wpa_supplicant *wpa_s, int skip_scan);
void hs20_cancel_fetch_osu(struct wpa_supplicant *wpa_s);
void hs20_icon_fetch_failed(struct wpa_supplicant *wpa_s);
void hs20_start_osu_scan(struct wpa_supplicant *wpa_s);
void hs20_init(struct wpa_supplicant *wpa_s);
void hs20_deinit(struct wpa_supplicant *wpa_s);
int hs20_get_icon(struct wpa_supplicant *wpa_s, const u8 *bssid,
		  const char *file_name, size_t offset, size_t size,
		  char *reply, size_t buf_len);
int hs20_del_icon(struct wpa_supplicant *wpa_s, const u8 *bssid,
		  const char *file_name);

#endif /* HS20_SUPPLICANT_H */
