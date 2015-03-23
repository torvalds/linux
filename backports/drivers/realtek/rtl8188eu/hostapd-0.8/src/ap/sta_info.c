/*
 * hostapd / Station table
 * Copyright (c) 2002-2009, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "common/ieee802_11_defs.h"
#include "radius/radius.h"
#include "radius/radius_client.h"
#include "drivers/driver.h"
#include "p2p/p2p.h"
#include "hostapd.h"
#include "accounting.h"
#include "ieee802_1x.h"
#include "ieee802_11.h"
#include "wpa_auth.h"
#include "preauth_auth.h"
#include "ap_config.h"
#include "beacon.h"
#include "ap_mlme.h"
#include "vlan_init.h"
#include "p2p_hostapd.h"
#include "ap_drv_ops.h"
#include "sta_info.h"

static void ap_sta_remove_in_other_bss(struct hostapd_data *hapd,
				       struct sta_info *sta);
static void ap_handle_session_timer(void *eloop_ctx, void *timeout_ctx);
#ifdef CONFIG_IEEE80211W
static void ap_sa_query_timer(void *eloop_ctx, void *timeout_ctx);
#endif /* CONFIG_IEEE80211W */

int ap_for_each_sta(struct hostapd_data *hapd,
		    int (*cb)(struct hostapd_data *hapd, struct sta_info *sta,
			      void *ctx),
		    void *ctx)
{
	struct sta_info *sta;

	for (sta = hapd->sta_list; sta; sta = sta->next) {
		if (cb(hapd, sta, ctx))
			return 1;
	}

	return 0;
}


struct sta_info * ap_get_sta(struct hostapd_data *hapd, const u8 *sta)
{
	struct sta_info *s;

	s = hapd->sta_hash[STA_HASH(sta)];
	while (s != NULL && os_memcmp(s->addr, sta, 6) != 0)
		s = s->hnext;
	return s;
}


static void ap_sta_list_del(struct hostapd_data *hapd, struct sta_info *sta)
{
	struct sta_info *tmp;

	if (hapd->sta_list == sta) {
		hapd->sta_list = sta->next;
		return;
	}

	tmp = hapd->sta_list;
	while (tmp != NULL && tmp->next != sta)
		tmp = tmp->next;
	if (tmp == NULL) {
		wpa_printf(MSG_DEBUG, "Could not remove STA " MACSTR " from "
			   "list.", MAC2STR(sta->addr));
	} else
		tmp->next = sta->next;
}


void ap_sta_hash_add(struct hostapd_data *hapd, struct sta_info *sta)
{
	sta->hnext = hapd->sta_hash[STA_HASH(sta->addr)];
	hapd->sta_hash[STA_HASH(sta->addr)] = sta;
}


static void ap_sta_hash_del(struct hostapd_data *hapd, struct sta_info *sta)
{
	struct sta_info *s;

	s = hapd->sta_hash[STA_HASH(sta->addr)];
	if (s == NULL) return;
	if (os_memcmp(s->addr, sta->addr, 6) == 0) {
		hapd->sta_hash[STA_HASH(sta->addr)] = s->hnext;
		return;
	}

	while (s->hnext != NULL &&
	       os_memcmp(s->hnext->addr, sta->addr, ETH_ALEN) != 0)
		s = s->hnext;
	if (s->hnext != NULL)
		s->hnext = s->hnext->hnext;
	else
		wpa_printf(MSG_DEBUG, "AP: could not remove STA " MACSTR
			   " from hash table", MAC2STR(sta->addr));
}


void ap_free_sta(struct hostapd_data *hapd, struct sta_info *sta)
{
	int set_beacon = 0;

	accounting_sta_stop(hapd, sta);

	/* just in case */
	ap_sta_set_authorized(hapd, sta, 0);

	if (sta->flags & WLAN_STA_WDS)
		hostapd_set_wds_sta(hapd, sta->addr, sta->aid, 0);

	if (!(sta->flags & WLAN_STA_PREAUTH))
		hostapd_drv_sta_remove(hapd, sta->addr);

	ap_sta_hash_del(hapd, sta);
	ap_sta_list_del(hapd, sta);

	if (sta->aid > 0)
		hapd->sta_aid[(sta->aid - 1) / 32] &=
			~BIT((sta->aid - 1) % 32);

	hapd->num_sta--;
	if (sta->nonerp_set) {
		sta->nonerp_set = 0;
		hapd->iface->num_sta_non_erp--;
		if (hapd->iface->num_sta_non_erp == 0)
			set_beacon++;
	}

	if (sta->no_short_slot_time_set) {
		sta->no_short_slot_time_set = 0;
		hapd->iface->num_sta_no_short_slot_time--;
		if (hapd->iface->current_mode->mode == HOSTAPD_MODE_IEEE80211G
		    && hapd->iface->num_sta_no_short_slot_time == 0)
			set_beacon++;
	}

	if (sta->no_short_preamble_set) {
		sta->no_short_preamble_set = 0;
		hapd->iface->num_sta_no_short_preamble--;
		if (hapd->iface->current_mode->mode == HOSTAPD_MODE_IEEE80211G
		    && hapd->iface->num_sta_no_short_preamble == 0)
			set_beacon++;
	}

	if (sta->no_ht_gf_set) {
		sta->no_ht_gf_set = 0;
		hapd->iface->num_sta_ht_no_gf--;
	}

	if (sta->no_ht_set) {
		sta->no_ht_set = 0;
		hapd->iface->num_sta_no_ht--;
	}

	if (sta->ht_20mhz_set) {
		sta->ht_20mhz_set = 0;
		hapd->iface->num_sta_ht_20mhz--;
	}

#ifdef CONFIG_P2P
	if (sta->no_p2p_set) {
		sta->no_p2p_set = 0;
		hapd->num_sta_no_p2p--;
		if (hapd->num_sta_no_p2p == 0)
			hostapd_p2p_non_p2p_sta_disconnected(hapd);
	}
#endif /* CONFIG_P2P */

#if defined(NEED_AP_MLME) && defined(CONFIG_IEEE80211N)
	if (hostapd_ht_operation_update(hapd->iface) > 0)
		set_beacon++;
#endif /* NEED_AP_MLME && CONFIG_IEEE80211N */

	if (set_beacon)
		ieee802_11_set_beacons(hapd->iface);

	eloop_cancel_timeout(ap_handle_timer, hapd, sta);
	eloop_cancel_timeout(ap_handle_session_timer, hapd, sta);

	ieee802_1x_free_station(sta);
	wpa_auth_sta_deinit(sta->wpa_sm);
	rsn_preauth_free_station(hapd, sta);
#ifndef CONFIG_NO_RADIUS
	radius_client_flush_auth(hapd->radius, sta->addr);
#endif /* CONFIG_NO_RADIUS */

	os_free(sta->last_assoc_req);
	os_free(sta->challenge);

#ifdef CONFIG_IEEE80211W
	os_free(sta->sa_query_trans_id);
	eloop_cancel_timeout(ap_sa_query_timer, hapd, sta);
#endif /* CONFIG_IEEE80211W */

#ifdef CONFIG_P2P
	p2p_group_notif_disassoc(hapd->p2p_group, sta->addr);
#endif /* CONFIG_P2P */

	wpabuf_free(sta->wps_ie);
	wpabuf_free(sta->p2p_ie);

	os_free(sta->ht_capabilities);

	os_free(sta);
}


void hostapd_free_stas(struct hostapd_data *hapd)
{
	struct sta_info *sta, *prev;

	sta = hapd->sta_list;

	while (sta) {
		prev = sta;
		if (sta->flags & WLAN_STA_AUTH) {
			mlme_deauthenticate_indication(
				hapd, sta, WLAN_REASON_UNSPECIFIED);
		}
		sta = sta->next;
		wpa_printf(MSG_DEBUG, "Removing station " MACSTR,
			   MAC2STR(prev->addr));
		ap_free_sta(hapd, prev);
	}
}


/**
 * ap_handle_timer - Per STA timer handler
 * @eloop_ctx: struct hostapd_data *
 * @timeout_ctx: struct sta_info *
 *
 * This function is called to check station activity and to remove inactive
 * stations.
 */
void ap_handle_timer(void *eloop_ctx, void *timeout_ctx)
{
	struct hostapd_data *hapd = eloop_ctx;
	struct sta_info *sta = timeout_ctx;
	unsigned long next_time = 0;

	if (sta->timeout_next == STA_REMOVE) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_INFO, "deauthenticated due to "
			       "local deauth request");
		ap_free_sta(hapd, sta);
		return;
	}

	if ((sta->flags & WLAN_STA_ASSOC) &&
	    (sta->timeout_next == STA_NULLFUNC ||
	     sta->timeout_next == STA_DISASSOC)) {
		int inactive_sec;
		inactive_sec = hostapd_drv_get_inact_sec(hapd, sta->addr);
		if (inactive_sec == -1) {
			wpa_msg(hapd, MSG_DEBUG, "Check inactivity: Could not "
				"get station info rom kernel driver for "
				MACSTR, MAC2STR(sta->addr));
		} else if (inactive_sec < hapd->conf->ap_max_inactivity &&
			   sta->flags & WLAN_STA_ASSOC) {
			/* station activity detected; reset timeout state */
			wpa_msg(hapd, MSG_DEBUG, "Station " MACSTR " has been "
				"active %is ago",
				MAC2STR(sta->addr), inactive_sec);
			sta->timeout_next = STA_NULLFUNC;
			next_time = hapd->conf->ap_max_inactivity -
				inactive_sec;
		} else {
			wpa_msg(hapd, MSG_DEBUG, "Station " MACSTR " has been "
				"inactive too long: %d sec, max allowed: %d",
				MAC2STR(sta->addr), inactive_sec,
				hapd->conf->ap_max_inactivity);
		}
	}

	if ((sta->flags & WLAN_STA_ASSOC) &&
	    sta->timeout_next == STA_DISASSOC &&
	    !(sta->flags & WLAN_STA_PENDING_POLL)) {
		wpa_msg(hapd, MSG_DEBUG, "Station " MACSTR " has ACKed data "
			"poll", MAC2STR(sta->addr));
		/* data nullfunc frame poll did not produce TX errors; assume
		 * station ACKed it */
		sta->timeout_next = STA_NULLFUNC;
		next_time = hapd->conf->ap_max_inactivity;
	}

	if (next_time) {
		eloop_register_timeout(next_time, 0, ap_handle_timer, hapd,
				       sta);
		return;
	}

	if (sta->timeout_next == STA_NULLFUNC &&
	    (sta->flags & WLAN_STA_ASSOC)) {
#ifndef CONFIG_NATIVE_WINDOWS
		/* send data frame to poll STA and check whether this frame
		 * is ACKed */
		struct ieee80211_hdr hdr;

		wpa_printf(MSG_DEBUG, "  Polling STA with data frame");
		sta->flags |= WLAN_STA_PENDING_POLL;

		os_memset(&hdr, 0, sizeof(hdr));
		if (hapd->driver &&
		    os_strcmp(hapd->driver->name, "hostap") == 0) {
			/*
			 * WLAN_FC_STYPE_NULLFUNC would be more appropriate,
			 * but it is apparently not retried so TX Exc events
			 * are not received for it.
			 */
			hdr.frame_control =
				IEEE80211_FC(WLAN_FC_TYPE_DATA,
					     WLAN_FC_STYPE_DATA);
		} else {
			hdr.frame_control =
				IEEE80211_FC(WLAN_FC_TYPE_DATA,
					     WLAN_FC_STYPE_NULLFUNC);
		}

		hdr.frame_control |= host_to_le16(WLAN_FC_FROMDS);
		os_memcpy(hdr.IEEE80211_DA_FROMDS, sta->addr, ETH_ALEN);
		os_memcpy(hdr.IEEE80211_BSSID_FROMDS, hapd->own_addr,
			  ETH_ALEN);
		os_memcpy(hdr.IEEE80211_SA_FROMDS, hapd->own_addr, ETH_ALEN);

		if (hostapd_drv_send_mlme(hapd, &hdr, sizeof(hdr)) < 0)
			perror("ap_handle_timer: send");
#endif /* CONFIG_NATIVE_WINDOWS */
	} else if (sta->timeout_next != STA_REMOVE) {
		int deauth = sta->timeout_next == STA_DEAUTH;

		wpa_printf(MSG_DEBUG, "Sending %s info to STA " MACSTR,
			   deauth ? "deauthentication" : "disassociation",
			   MAC2STR(sta->addr));

		if (deauth) {
			hostapd_drv_sta_deauth(
				hapd, sta->addr,
				WLAN_REASON_PREV_AUTH_NOT_VALID);
		} else {
			hostapd_drv_sta_disassoc(
				hapd, sta->addr,
				WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY);
		}
	}

	switch (sta->timeout_next) {
	case STA_NULLFUNC:
		sta->timeout_next = STA_DISASSOC;
		eloop_register_timeout(AP_DISASSOC_DELAY, 0, ap_handle_timer,
				       hapd, sta);
		break;
	case STA_DISASSOC:
		sta->flags &= ~WLAN_STA_ASSOC;
		ieee802_1x_notify_port_enabled(sta->eapol_sm, 0);
		if (!sta->acct_terminate_cause)
			sta->acct_terminate_cause =
				RADIUS_ACCT_TERMINATE_CAUSE_IDLE_TIMEOUT;
		accounting_sta_stop(hapd, sta);
		ieee802_1x_free_station(sta);
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_INFO, "disassociated due to "
			       "inactivity");
		sta->timeout_next = STA_DEAUTH;
		eloop_register_timeout(AP_DEAUTH_DELAY, 0, ap_handle_timer,
				       hapd, sta);
		mlme_disassociate_indication(
			hapd, sta, WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY);
		break;
	case STA_DEAUTH:
	case STA_REMOVE:
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_INFO, "deauthenticated due to "
			       "inactivity");
		if (!sta->acct_terminate_cause)
			sta->acct_terminate_cause =
				RADIUS_ACCT_TERMINATE_CAUSE_IDLE_TIMEOUT;
		mlme_deauthenticate_indication(
			hapd, sta,
			WLAN_REASON_PREV_AUTH_NOT_VALID);
		ap_free_sta(hapd, sta);
		break;
	}
}


static void ap_handle_session_timer(void *eloop_ctx, void *timeout_ctx)
{
	struct hostapd_data *hapd = eloop_ctx;
	struct sta_info *sta = timeout_ctx;
	u8 addr[ETH_ALEN];

	if (!(sta->flags & WLAN_STA_AUTH))
		return;

	mlme_deauthenticate_indication(hapd, sta,
				       WLAN_REASON_PREV_AUTH_NOT_VALID);
	hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
		       HOSTAPD_LEVEL_INFO, "deauthenticated due to "
		       "session timeout");
	sta->acct_terminate_cause =
		RADIUS_ACCT_TERMINATE_CAUSE_SESSION_TIMEOUT;
	os_memcpy(addr, sta->addr, ETH_ALEN);
	ap_free_sta(hapd, sta);
	hostapd_drv_sta_deauth(hapd, addr, WLAN_REASON_PREV_AUTH_NOT_VALID);
}


void ap_sta_session_timeout(struct hostapd_data *hapd, struct sta_info *sta,
			    u32 session_timeout)
{
	hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
		       HOSTAPD_LEVEL_DEBUG, "setting session timeout to %d "
		       "seconds", session_timeout);
	eloop_cancel_timeout(ap_handle_session_timer, hapd, sta);
	eloop_register_timeout(session_timeout, 0, ap_handle_session_timer,
			       hapd, sta);
}


void ap_sta_no_session_timeout(struct hostapd_data *hapd, struct sta_info *sta)
{
	eloop_cancel_timeout(ap_handle_session_timer, hapd, sta);
}


struct sta_info * ap_sta_add(struct hostapd_data *hapd, const u8 *addr)
{
	struct sta_info *sta;

	sta = ap_get_sta(hapd, addr);
	if (sta)
		return sta;

	wpa_printf(MSG_DEBUG, "  New STA");
	if (hapd->num_sta >= hapd->conf->max_num_sta) {
		/* FIX: might try to remove some old STAs first? */
		wpa_printf(MSG_DEBUG, "no more room for new STAs (%d/%d)",
			   hapd->num_sta, hapd->conf->max_num_sta);
		return NULL;
	}

	sta = os_zalloc(sizeof(struct sta_info));
	if (sta == NULL) {
		wpa_printf(MSG_ERROR, "malloc failed");
		return NULL;
	}
	sta->acct_interim_interval = hapd->conf->acct_interim_interval;

	/* initialize STA info data */
	eloop_register_timeout(hapd->conf->ap_max_inactivity, 0,
			       ap_handle_timer, hapd, sta);
	os_memcpy(sta->addr, addr, ETH_ALEN);
	sta->next = hapd->sta_list;
	hapd->sta_list = sta;
	hapd->num_sta++;
	ap_sta_hash_add(hapd, sta);
	sta->ssid = &hapd->conf->ssid;
	ap_sta_remove_in_other_bss(hapd, sta);

	return sta;
}


static int ap_sta_remove(struct hostapd_data *hapd, struct sta_info *sta)
{
	ieee802_1x_notify_port_enabled(sta->eapol_sm, 0);

	wpa_printf(MSG_DEBUG, "Removing STA " MACSTR " from kernel driver",
		   MAC2STR(sta->addr));
	if (hostapd_drv_sta_remove(hapd, sta->addr) &&
	    sta->flags & WLAN_STA_ASSOC) {
		wpa_printf(MSG_DEBUG, "Could not remove station " MACSTR
			   " from kernel driver.", MAC2STR(sta->addr));
		return -1;
	}
	return 0;
}


static void ap_sta_remove_in_other_bss(struct hostapd_data *hapd,
				       struct sta_info *sta)
{
	struct hostapd_iface *iface = hapd->iface;
	size_t i;

	for (i = 0; i < iface->num_bss; i++) {
		struct hostapd_data *bss = iface->bss[i];
		struct sta_info *sta2;
		/* bss should always be set during operation, but it may be
		 * NULL during reconfiguration. Assume the STA is not
		 * associated to another BSS in that case to avoid NULL pointer
		 * dereferences. */
		if (bss == hapd || bss == NULL)
			continue;
		sta2 = ap_get_sta(bss, sta->addr);
		if (!sta2)
			continue;

		ap_sta_disconnect(bss, sta2, sta2->addr,
				  WLAN_REASON_PREV_AUTH_NOT_VALID);
	}
}


void ap_sta_disassociate(struct hostapd_data *hapd, struct sta_info *sta,
			 u16 reason)
{
	wpa_printf(MSG_DEBUG, "%s: disassociate STA " MACSTR,
		   hapd->conf->iface, MAC2STR(sta->addr));
	sta->flags &= ~WLAN_STA_ASSOC;
	ap_sta_remove(hapd, sta);
	sta->timeout_next = STA_DEAUTH;
	eloop_cancel_timeout(ap_handle_timer, hapd, sta);
	eloop_register_timeout(AP_MAX_INACTIVITY_AFTER_DISASSOC, 0,
			       ap_handle_timer, hapd, sta);
	accounting_sta_stop(hapd, sta);
	ieee802_1x_free_station(sta);

	mlme_disassociate_indication(hapd, sta, reason);
}


void ap_sta_deauthenticate(struct hostapd_data *hapd, struct sta_info *sta,
			   u16 reason)
{
	wpa_printf(MSG_DEBUG, "%s: deauthenticate STA " MACSTR,
		   hapd->conf->iface, MAC2STR(sta->addr));
	sta->flags &= ~(WLAN_STA_AUTH | WLAN_STA_ASSOC);
	ap_sta_remove(hapd, sta);
	sta->timeout_next = STA_REMOVE;
	eloop_cancel_timeout(ap_handle_timer, hapd, sta);
	eloop_register_timeout(AP_MAX_INACTIVITY_AFTER_DEAUTH, 0,
			       ap_handle_timer, hapd, sta);
	accounting_sta_stop(hapd, sta);
	ieee802_1x_free_station(sta);

	mlme_deauthenticate_indication(hapd, sta, reason);
}


int ap_sta_bind_vlan(struct hostapd_data *hapd, struct sta_info *sta,
		     int old_vlanid)
{
#ifndef CONFIG_NO_VLAN
	const char *iface;
	struct hostapd_vlan *vlan = NULL;
	int ret;

	/*
	 * Do not proceed furthur if the vlan id remains same. We do not want
	 * duplicate dynamic vlan entries.
	 */
	if (sta->vlan_id == old_vlanid)
		return 0;

	/*
	 * During 1x reauth, if the vlan id changes, then remove the old id and
	 * proceed furthur to add the new one.
	 */
	if (old_vlanid > 0)
		vlan_remove_dynamic(hapd, old_vlanid);

	iface = hapd->conf->iface;
	if (sta->ssid->vlan[0])
		iface = sta->ssid->vlan;

	if (sta->ssid->dynamic_vlan == DYNAMIC_VLAN_DISABLED)
		sta->vlan_id = 0;
	else if (sta->vlan_id > 0) {
		vlan = hapd->conf->vlan;
		while (vlan) {
			if (vlan->vlan_id == sta->vlan_id ||
			    vlan->vlan_id == VLAN_ID_WILDCARD) {
				iface = vlan->ifname;
				break;
			}
			vlan = vlan->next;
		}
	}

	if (sta->vlan_id > 0 && vlan == NULL) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG, "could not find VLAN for "
			       "binding station to (vlan_id=%d)",
			       sta->vlan_id);
		return -1;
	} else if (sta->vlan_id > 0 && vlan->vlan_id == VLAN_ID_WILDCARD) {
		vlan = vlan_add_dynamic(hapd, vlan, sta->vlan_id);
		if (vlan == NULL) {
			hostapd_logger(hapd, sta->addr,
				       HOSTAPD_MODULE_IEEE80211,
				       HOSTAPD_LEVEL_DEBUG, "could not add "
				       "dynamic VLAN interface for vlan_id=%d",
				       sta->vlan_id);
			return -1;
		}

		iface = vlan->ifname;
		if (vlan_setup_encryption_dyn(hapd, sta->ssid, iface) != 0) {
			hostapd_logger(hapd, sta->addr,
				       HOSTAPD_MODULE_IEEE80211,
				       HOSTAPD_LEVEL_DEBUG, "could not "
				       "configure encryption for dynamic VLAN "
				       "interface for vlan_id=%d",
				       sta->vlan_id);
		}

		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG, "added new dynamic VLAN "
			       "interface '%s'", iface);
	} else if (vlan && vlan->vlan_id == sta->vlan_id) {
		if (sta->vlan_id > 0) {
			vlan->dynamic_vlan++;
			hostapd_logger(hapd, sta->addr,
				       HOSTAPD_MODULE_IEEE80211,
				       HOSTAPD_LEVEL_DEBUG, "updated existing "
				       "dynamic VLAN interface '%s'", iface);
		}

		/*
		 * Update encryption configuration for statically generated
		 * VLAN interface. This is only used for static WEP
		 * configuration for the case where hostapd did not yet know
		 * which keys are to be used when the interface was added.
		 */
		if (vlan_setup_encryption_dyn(hapd, sta->ssid, iface) != 0) {
			hostapd_logger(hapd, sta->addr,
				       HOSTAPD_MODULE_IEEE80211,
				       HOSTAPD_LEVEL_DEBUG, "could not "
				       "configure encryption for VLAN "
				       "interface for vlan_id=%d",
				       sta->vlan_id);
		}
	}

	hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
		       HOSTAPD_LEVEL_DEBUG, "binding station to interface "
		       "'%s'", iface);

	if (wpa_auth_sta_set_vlan(sta->wpa_sm, sta->vlan_id) < 0)
		wpa_printf(MSG_INFO, "Failed to update VLAN-ID for WPA");

	ret = hostapd_drv_set_sta_vlan(iface, hapd, sta->addr, sta->vlan_id);
	if (ret < 0) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG, "could not bind the STA "
			       "entry to vlan_id=%d", sta->vlan_id);
	}
	return ret;
#else /* CONFIG_NO_VLAN */
	return 0;
#endif /* CONFIG_NO_VLAN */
}


#ifdef CONFIG_IEEE80211W

int ap_check_sa_query_timeout(struct hostapd_data *hapd, struct sta_info *sta)
{
	u32 tu;
	struct os_time now, passed;
	os_get_time(&now);
	os_time_sub(&now, &sta->sa_query_start, &passed);
	tu = (passed.sec * 1000000 + passed.usec) / 1024;
	if (hapd->conf->assoc_sa_query_max_timeout < tu) {
		hostapd_logger(hapd, sta->addr,
			       HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "association SA Query timed out");
		sta->sa_query_timed_out = 1;
		os_free(sta->sa_query_trans_id);
		sta->sa_query_trans_id = NULL;
		sta->sa_query_count = 0;
		eloop_cancel_timeout(ap_sa_query_timer, hapd, sta);
		return 1;
	}

	return 0;
}


static void ap_sa_query_timer(void *eloop_ctx, void *timeout_ctx)
{
	struct hostapd_data *hapd = eloop_ctx;
	struct sta_info *sta = timeout_ctx;
	unsigned int timeout, sec, usec;
	u8 *trans_id, *nbuf;

	if (sta->sa_query_count > 0 &&
	    ap_check_sa_query_timeout(hapd, sta))
		return;

	nbuf = os_realloc(sta->sa_query_trans_id,
			  (sta->sa_query_count + 1) * WLAN_SA_QUERY_TR_ID_LEN);
	if (nbuf == NULL)
		return;
	if (sta->sa_query_count == 0) {
		/* Starting a new SA Query procedure */
		os_get_time(&sta->sa_query_start);
	}
	trans_id = nbuf + sta->sa_query_count * WLAN_SA_QUERY_TR_ID_LEN;
	sta->sa_query_trans_id = nbuf;
	sta->sa_query_count++;

	os_get_random(trans_id, WLAN_SA_QUERY_TR_ID_LEN);

	timeout = hapd->conf->assoc_sa_query_retry_timeout;
	sec = ((timeout / 1000) * 1024) / 1000;
	usec = (timeout % 1000) * 1024;
	eloop_register_timeout(sec, usec, ap_sa_query_timer, hapd, sta);

	hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
		       HOSTAPD_LEVEL_DEBUG,
		       "association SA Query attempt %d", sta->sa_query_count);

#ifdef NEED_AP_MLME
	ieee802_11_send_sa_query_req(hapd, sta->addr, trans_id);
#endif /* NEED_AP_MLME */
}


void ap_sta_start_sa_query(struct hostapd_data *hapd, struct sta_info *sta)
{
	ap_sa_query_timer(hapd, sta);
}


void ap_sta_stop_sa_query(struct hostapd_data *hapd, struct sta_info *sta)
{
	eloop_cancel_timeout(ap_sa_query_timer, hapd, sta);
	os_free(sta->sa_query_trans_id);
	sta->sa_query_trans_id = NULL;
	sta->sa_query_count = 0;
}

#endif /* CONFIG_IEEE80211W */


void ap_sta_set_authorized(struct hostapd_data *hapd, struct sta_info *sta,
			   int authorized)
{
	if (!!authorized == !!(sta->flags & WLAN_STA_AUTHORIZED))
		return;

	if (authorized)
		sta->flags |= WLAN_STA_AUTHORIZED;
	else
		sta->flags &= ~WLAN_STA_AUTHORIZED;

	if (hapd->sta_authorized_cb)
		hapd->sta_authorized_cb(hapd->sta_authorized_cb_ctx,
					sta->addr, authorized);
}


void ap_sta_disconnect(struct hostapd_data *hapd, struct sta_info *sta,
		       const u8 *addr, u16 reason)
{

	if (sta == NULL && addr)
		sta = ap_get_sta(hapd, addr);

	if (addr)
		hostapd_drv_sta_deauth(hapd, addr, reason);

	if (sta == NULL)
		return;
	ap_sta_set_authorized(hapd, sta, 0);
	sta->flags &= ~(WLAN_STA_AUTH | WLAN_STA_ASSOC);
	eloop_cancel_timeout(ap_handle_timer, hapd, sta);
	eloop_register_timeout(0, 0, ap_handle_timer, hapd, sta);
	sta->timeout_next = STA_REMOVE;
}
