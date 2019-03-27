/*
 * WPA Supplicant - Scanning
 * Copyright (c) 2003-2014, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef SCAN_H
#define SCAN_H

int wpa_supplicant_enabled_networks(struct wpa_supplicant *wpa_s);
void wpa_supplicant_req_scan(struct wpa_supplicant *wpa_s, int sec, int usec);
int wpa_supplicant_delayed_sched_scan(struct wpa_supplicant *wpa_s,
				      int sec, int usec);
int wpa_supplicant_req_sched_scan(struct wpa_supplicant *wpa_s);
void wpa_supplicant_cancel_scan(struct wpa_supplicant *wpa_s);
void wpa_supplicant_cancel_delayed_sched_scan(struct wpa_supplicant *wpa_s);
void wpa_supplicant_cancel_sched_scan(struct wpa_supplicant *wpa_s);
void wpa_supplicant_notify_scanning(struct wpa_supplicant *wpa_s,
				    int scanning);
struct wpa_driver_scan_params;
int wpa_supplicant_trigger_scan(struct wpa_supplicant *wpa_s,
				struct wpa_driver_scan_params *params);
struct wpa_scan_results *
wpa_supplicant_get_scan_results(struct wpa_supplicant *wpa_s,
				struct scan_info *info, int new_scan);
int wpa_supplicant_update_scan_results(struct wpa_supplicant *wpa_s);
const u8 * wpa_scan_get_ie(const struct wpa_scan_res *res, u8 ie);
const u8 * wpa_scan_get_vendor_ie(const struct wpa_scan_res *res,
				  u32 vendor_type);
const u8 * wpa_scan_get_vendor_ie_beacon(const struct wpa_scan_res *res,
					 u32 vendor_type);
struct wpabuf * wpa_scan_get_vendor_ie_multi(const struct wpa_scan_res *res,
					     u32 vendor_type);
int wpa_supplicant_filter_bssid_match(struct wpa_supplicant *wpa_s,
				      const u8 *bssid);
void wpa_supplicant_update_scan_int(struct wpa_supplicant *wpa_s, int sec);
void scan_only_handler(struct wpa_supplicant *wpa_s,
		       struct wpa_scan_results *scan_res);
int wpas_scan_scheduled(struct wpa_supplicant *wpa_s);
struct wpa_driver_scan_params *
wpa_scan_clone_params(const struct wpa_driver_scan_params *src);
void wpa_scan_free_params(struct wpa_driver_scan_params *params);
int wpas_start_pno(struct wpa_supplicant *wpa_s);
int wpas_stop_pno(struct wpa_supplicant *wpa_s);
void wpas_scan_reset_sched_scan(struct wpa_supplicant *wpa_s);
void wpas_scan_restart_sched_scan(struct wpa_supplicant *wpa_s);

void wpas_mac_addr_rand_scan_clear(struct wpa_supplicant *wpa_s,
				   unsigned int type);
int wpas_mac_addr_rand_scan_set(struct wpa_supplicant *wpa_s,
				unsigned int type, const u8 *addr,
				const u8 *mask);
int wpas_abort_ongoing_scan(struct wpa_supplicant *wpa_s);
void filter_scan_res(struct wpa_supplicant *wpa_s,
		     struct wpa_scan_results *res);
void scan_snr(struct wpa_scan_res *res);
void scan_est_throughput(struct wpa_supplicant *wpa_s,
			 struct wpa_scan_res *res);
void wpa_supplicant_set_default_scan_ies(struct wpa_supplicant *wpa_s);

#endif /* SCAN_H */
