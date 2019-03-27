/*
 * Hotspot 2.0 AP ANQP processing
 * Copyright (c) 2011-2013, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef HS20_H
#define HS20_H

struct hostapd_data;

u8 * hostapd_eid_hs20_indication(struct hostapd_data *hapd, u8 *eid);
u8 * hostapd_eid_osen(struct hostapd_data *hapd, u8 *eid);
int hs20_send_wnm_notification(struct hostapd_data *hapd, const u8 *addr,
			       u8 osu_method, const char *url);
int hs20_send_wnm_notification_deauth_req(struct hostapd_data *hapd,
					  const u8 *addr,
					  const struct wpabuf *payload);
int hs20_send_wnm_notification_t_c(struct hostapd_data *hapd,
				   const u8 *addr, const char *url);
void hs20_t_c_filtering(struct hostapd_data *hapd, struct sta_info *sta,
			int enabled);

#endif /* HS20_H */
