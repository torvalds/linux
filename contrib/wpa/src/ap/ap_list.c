/*
 * hostapd / AP table
 * Copyright (c) 2002-2009, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2003-2004, Instant802 Networks, Inc.
 * Copyright (c) 2006, Devicescape Software, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "common/ieee802_11_defs.h"
#include "common/ieee802_11_common.h"
#include "hostapd.h"
#include "ap_config.h"
#include "ieee802_11.h"
#include "sta_info.h"
#include "beacon.h"
#include "ap_list.h"


/* AP list is a double linked list with head->prev pointing to the end of the
 * list and tail->next = NULL. Entries are moved to the head of the list
 * whenever a beacon has been received from the AP in question. The tail entry
 * in this link will thus be the least recently used entry. */


static int ap_list_beacon_olbc(struct hostapd_iface *iface, struct ap_info *ap)
{
	int i;

	if (iface->current_mode == NULL ||
	    iface->current_mode->mode != HOSTAPD_MODE_IEEE80211G ||
	    iface->conf->channel != ap->channel)
		return 0;

	if (ap->erp != -1 && (ap->erp & ERP_INFO_NON_ERP_PRESENT))
		return 1;

	for (i = 0; i < WLAN_SUPP_RATES_MAX; i++) {
		int rate = (ap->supported_rates[i] & 0x7f) * 5;
		if (rate == 60 || rate == 90 || rate > 110)
			return 0;
	}

	return 1;
}


static struct ap_info * ap_get_ap(struct hostapd_iface *iface, const u8 *ap)
{
	struct ap_info *s;

	s = iface->ap_hash[STA_HASH(ap)];
	while (s != NULL && os_memcmp(s->addr, ap, ETH_ALEN) != 0)
		s = s->hnext;
	return s;
}


static void ap_ap_list_add(struct hostapd_iface *iface, struct ap_info *ap)
{
	if (iface->ap_list) {
		ap->prev = iface->ap_list->prev;
		iface->ap_list->prev = ap;
	} else
		ap->prev = ap;
	ap->next = iface->ap_list;
	iface->ap_list = ap;
}


static void ap_ap_list_del(struct hostapd_iface *iface, struct ap_info *ap)
{
	if (iface->ap_list == ap)
		iface->ap_list = ap->next;
	else
		ap->prev->next = ap->next;

	if (ap->next)
		ap->next->prev = ap->prev;
	else if (iface->ap_list)
		iface->ap_list->prev = ap->prev;
}


static void ap_ap_hash_add(struct hostapd_iface *iface, struct ap_info *ap)
{
	ap->hnext = iface->ap_hash[STA_HASH(ap->addr)];
	iface->ap_hash[STA_HASH(ap->addr)] = ap;
}


static void ap_ap_hash_del(struct hostapd_iface *iface, struct ap_info *ap)
{
	struct ap_info *s;

	s = iface->ap_hash[STA_HASH(ap->addr)];
	if (s == NULL) return;
	if (os_memcmp(s->addr, ap->addr, ETH_ALEN) == 0) {
		iface->ap_hash[STA_HASH(ap->addr)] = s->hnext;
		return;
	}

	while (s->hnext != NULL &&
	       os_memcmp(s->hnext->addr, ap->addr, ETH_ALEN) != 0)
		s = s->hnext;
	if (s->hnext != NULL)
		s->hnext = s->hnext->hnext;
	else
		wpa_printf(MSG_INFO, "AP: could not remove AP " MACSTR
			   " from hash table",  MAC2STR(ap->addr));
}


static void ap_free_ap(struct hostapd_iface *iface, struct ap_info *ap)
{
	ap_ap_hash_del(iface, ap);
	ap_ap_list_del(iface, ap);

	iface->num_ap--;
	os_free(ap);
}


static void hostapd_free_aps(struct hostapd_iface *iface)
{
	struct ap_info *ap, *prev;

	ap = iface->ap_list;

	while (ap) {
		prev = ap;
		ap = ap->next;
		ap_free_ap(iface, prev);
	}

	iface->ap_list = NULL;
}


static struct ap_info * ap_ap_add(struct hostapd_iface *iface, const u8 *addr)
{
	struct ap_info *ap;

	ap = os_zalloc(sizeof(struct ap_info));
	if (ap == NULL)
		return NULL;

	/* initialize AP info data */
	os_memcpy(ap->addr, addr, ETH_ALEN);
	ap_ap_list_add(iface, ap);
	iface->num_ap++;
	ap_ap_hash_add(iface, ap);

	if (iface->num_ap > iface->conf->ap_table_max_size && ap != ap->prev) {
		wpa_printf(MSG_DEBUG, "Removing the least recently used AP "
			   MACSTR " from AP table", MAC2STR(ap->prev->addr));
		ap_free_ap(iface, ap->prev);
	}

	return ap;
}


void ap_list_process_beacon(struct hostapd_iface *iface,
			    const struct ieee80211_mgmt *mgmt,
			    struct ieee802_11_elems *elems,
			    struct hostapd_frame_info *fi)
{
	struct ap_info *ap;
	int new_ap = 0;
	int set_beacon = 0;

	if (iface->conf->ap_table_max_size < 1)
		return;

	ap = ap_get_ap(iface, mgmt->bssid);
	if (!ap) {
		ap = ap_ap_add(iface, mgmt->bssid);
		if (!ap) {
			wpa_printf(MSG_INFO,
				   "Failed to allocate AP information entry");
			return;
		}
		new_ap = 1;
	}

	merge_byte_arrays(ap->supported_rates, WLAN_SUPP_RATES_MAX,
			  elems->supp_rates, elems->supp_rates_len,
			  elems->ext_supp_rates, elems->ext_supp_rates_len);

	if (elems->erp_info)
		ap->erp = elems->erp_info[0];
	else
		ap->erp = -1;

	if (elems->ds_params)
		ap->channel = elems->ds_params[0];
	else if (elems->ht_operation)
		ap->channel = elems->ht_operation[0];
	else if (fi)
		ap->channel = fi->channel;

	if (elems->ht_capabilities)
		ap->ht_support = 1;
	else
		ap->ht_support = 0;

	os_get_reltime(&ap->last_beacon);

	if (!new_ap && ap != iface->ap_list) {
		/* move AP entry into the beginning of the list so that the
		 * oldest entry is always in the end of the list */
		ap_ap_list_del(iface, ap);
		ap_ap_list_add(iface, ap);
	}

	if (!iface->olbc &&
	    ap_list_beacon_olbc(iface, ap)) {
		iface->olbc = 1;
		wpa_printf(MSG_DEBUG, "OLBC AP detected: " MACSTR
			   " (channel %d) - enable protection",
			   MAC2STR(ap->addr), ap->channel);
		set_beacon++;
	}

#ifdef CONFIG_IEEE80211N
	if (!iface->olbc_ht && !ap->ht_support &&
	    (ap->channel == 0 ||
	     ap->channel == iface->conf->channel ||
	     ap->channel == iface->conf->channel +
	     iface->conf->secondary_channel * 4)) {
		iface->olbc_ht = 1;
		hostapd_ht_operation_update(iface);
		wpa_printf(MSG_DEBUG, "OLBC HT AP detected: " MACSTR
			   " (channel %d) - enable protection",
			   MAC2STR(ap->addr), ap->channel);
		set_beacon++;
	}
#endif /* CONFIG_IEEE80211N */

	if (set_beacon)
		ieee802_11_update_beacons(iface);
}


void ap_list_timer(struct hostapd_iface *iface)
{
	struct os_reltime now;
	struct ap_info *ap;
	int set_beacon = 0;

	if (!iface->ap_list)
		return;

	os_get_reltime(&now);

	while (iface->ap_list) {
		ap = iface->ap_list->prev;
		if (!os_reltime_expired(&now, &ap->last_beacon,
					iface->conf->ap_table_expiration_time))
			break;

		ap_free_ap(iface, ap);
	}

	if (iface->olbc || iface->olbc_ht) {
		int olbc = 0;
		int olbc_ht = 0;

		ap = iface->ap_list;
		while (ap && (olbc == 0 || olbc_ht == 0)) {
			if (ap_list_beacon_olbc(iface, ap))
				olbc = 1;
			if (!ap->ht_support)
				olbc_ht = 1;
			ap = ap->next;
		}
		if (!olbc && iface->olbc) {
			wpa_printf(MSG_DEBUG, "OLBC not detected anymore");
			iface->olbc = 0;
			set_beacon++;
		}
#ifdef CONFIG_IEEE80211N
		if (!olbc_ht && iface->olbc_ht) {
			wpa_printf(MSG_DEBUG, "OLBC HT not detected anymore");
			iface->olbc_ht = 0;
			hostapd_ht_operation_update(iface);
			set_beacon++;
		}
#endif /* CONFIG_IEEE80211N */
	}

	if (set_beacon)
		ieee802_11_update_beacons(iface);
}


int ap_list_init(struct hostapd_iface *iface)
{
	return 0;
}


void ap_list_deinit(struct hostapd_iface *iface)
{
	hostapd_free_aps(iface);
}
