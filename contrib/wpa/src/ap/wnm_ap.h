/*
 * IEEE 802.11v WNM related functions and structures
 * Copyright (c) 2011-2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WNM_AP_H
#define WNM_AP_H

struct sta_info;

int ieee802_11_rx_wnm_action_ap(struct hostapd_data *hapd,
				const struct ieee80211_mgmt *mgmt, size_t len);
int wnm_send_disassoc_imminent(struct hostapd_data *hapd,
			       struct sta_info *sta, int disassoc_timer);
int wnm_send_ess_disassoc_imminent(struct hostapd_data *hapd,
				   struct sta_info *sta, const char *url,
				   int disassoc_timer);
int wnm_send_bss_tm_req(struct hostapd_data *hapd, struct sta_info *sta,
			u8 req_mode, int disassoc_timer, u8 valid_int,
			const u8 *bss_term_dur, const char *url,
			const u8 *nei_rep, size_t nei_rep_len,
			const u8 *mbo_attrs, size_t mbo_len);
void ap_sta_reset_steer_flag_timer(void *eloop_ctx, void *timeout_ctx);
int wnm_send_coloc_intf_req(struct hostapd_data *hapd, struct sta_info *sta,
			    unsigned int auto_report, unsigned int timeout);

#endif /* WNM_AP_H */
