/*
 * MBO related functions and structures
 * Copyright (c) 2016, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef MBO_AP_H
#define MBO_AP_H

struct hostapd_data;
struct sta_info;
struct ieee802_11_elems;

#ifdef CONFIG_MBO

void mbo_ap_check_sta_assoc(struct hostapd_data *hapd, struct sta_info *sta,
			    struct ieee802_11_elems *elems);
int mbo_ap_get_info(struct sta_info *sta, char *buf, size_t buflen);
void mbo_ap_wnm_notification_req(struct hostapd_data *hapd, const u8 *addr,
				 const u8 *buf, size_t len);
void mbo_ap_sta_free(struct sta_info *sta);

#else /* CONFIG_MBO */

static inline void mbo_ap_check_sta_assoc(struct hostapd_data *hapd,
					  struct sta_info *sta,
					  struct ieee802_11_elems *elems)
{
}

static inline int mbo_ap_get_info(struct sta_info *sta, char *buf,
				  size_t buflen)
{
	return 0;
}

static inline void mbo_ap_wnm_notification_req(struct hostapd_data *hapd,
					       const u8 *addr,
					       const u8 *buf, size_t len)
{
}

static inline void mbo_ap_sta_free(struct sta_info *sta)
{
}

#endif /* CONFIG_MBO */

#endif /* MBO_AP_H */
