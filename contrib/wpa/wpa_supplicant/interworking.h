/*
 * Interworking (IEEE 802.11u)
 * Copyright (c) 2011-2012, Qualcomm Atheros
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef INTERWORKING_H
#define INTERWORKING_H

enum gas_query_result;

int anqp_send_req(struct wpa_supplicant *wpa_s, const u8 *dst,
		  u16 info_ids[], size_t num_ids, u32 subtypes,
		  u32 mbo_subtypes);
void anqp_resp_cb(void *ctx, const u8 *dst, u8 dialog_token,
		  enum gas_query_result result,
		  const struct wpabuf *adv_proto,
		  const struct wpabuf *resp, u16 status_code);
int gas_send_request(struct wpa_supplicant *wpa_s, const u8 *dst,
		     const struct wpabuf *adv_proto,
		     const struct wpabuf *query);
int interworking_fetch_anqp(struct wpa_supplicant *wpa_s);
void interworking_stop_fetch_anqp(struct wpa_supplicant *wpa_s);
int interworking_select(struct wpa_supplicant *wpa_s, int auto_select,
			int *freqs);
int interworking_connect(struct wpa_supplicant *wpa_s, struct wpa_bss *bss,
			 int only_add);
void interworking_start_fetch_anqp(struct wpa_supplicant *wpa_s);
int interworking_home_sp_cred(struct wpa_supplicant *wpa_s,
			      struct wpa_cred *cred,
			      struct wpabuf *domain_names);
int domain_name_list_contains(struct wpabuf *domain_names,
			      const char *domain, int exact_match);

#endif /* INTERWORKING_H */
