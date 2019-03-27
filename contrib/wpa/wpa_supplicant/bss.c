/*
 * BSS table
 * Copyright (c) 2009-2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "common/ieee802_11_defs.h"
#include "drivers/driver.h"
#include "eap_peer/eap.h"
#include "wpa_supplicant_i.h"
#include "config.h"
#include "notify.h"
#include "scan.h"
#include "bss.h"


#define WPA_BSS_FREQ_CHANGED_FLAG	BIT(0)
#define WPA_BSS_SIGNAL_CHANGED_FLAG	BIT(1)
#define WPA_BSS_PRIVACY_CHANGED_FLAG	BIT(2)
#define WPA_BSS_MODE_CHANGED_FLAG	BIT(3)
#define WPA_BSS_WPAIE_CHANGED_FLAG	BIT(4)
#define WPA_BSS_RSNIE_CHANGED_FLAG	BIT(5)
#define WPA_BSS_WPS_CHANGED_FLAG	BIT(6)
#define WPA_BSS_RATES_CHANGED_FLAG	BIT(7)
#define WPA_BSS_IES_CHANGED_FLAG	BIT(8)


static void wpa_bss_set_hessid(struct wpa_bss *bss)
{
#ifdef CONFIG_INTERWORKING
	const u8 *ie = wpa_bss_get_ie(bss, WLAN_EID_INTERWORKING);
	if (ie == NULL || (ie[1] != 7 && ie[1] != 9)) {
		os_memset(bss->hessid, 0, ETH_ALEN);
		return;
	}
	if (ie[1] == 7)
		os_memcpy(bss->hessid, ie + 3, ETH_ALEN);
	else
		os_memcpy(bss->hessid, ie + 5, ETH_ALEN);
#endif /* CONFIG_INTERWORKING */
}


/**
 * wpa_bss_anqp_alloc - Allocate ANQP data structure for a BSS entry
 * Returns: Allocated ANQP data structure or %NULL on failure
 *
 * The allocated ANQP data structure has its users count set to 1. It may be
 * shared by multiple BSS entries and each shared entry is freed with
 * wpa_bss_anqp_free().
 */
struct wpa_bss_anqp * wpa_bss_anqp_alloc(void)
{
	struct wpa_bss_anqp *anqp;
	anqp = os_zalloc(sizeof(*anqp));
	if (anqp == NULL)
		return NULL;
#ifdef CONFIG_INTERWORKING
	dl_list_init(&anqp->anqp_elems);
#endif /* CONFIG_INTERWORKING */
	anqp->users = 1;
	return anqp;
}


/**
 * wpa_bss_anqp_clone - Clone an ANQP data structure
 * @anqp: ANQP data structure from wpa_bss_anqp_alloc()
 * Returns: Cloned ANQP data structure or %NULL on failure
 */
static struct wpa_bss_anqp * wpa_bss_anqp_clone(struct wpa_bss_anqp *anqp)
{
	struct wpa_bss_anqp *n;

	n = os_zalloc(sizeof(*n));
	if (n == NULL)
		return NULL;

#define ANQP_DUP(f) if (anqp->f) n->f = wpabuf_dup(anqp->f)
#ifdef CONFIG_INTERWORKING
	dl_list_init(&n->anqp_elems);
	ANQP_DUP(capability_list);
	ANQP_DUP(venue_name);
	ANQP_DUP(network_auth_type);
	ANQP_DUP(roaming_consortium);
	ANQP_DUP(ip_addr_type_availability);
	ANQP_DUP(nai_realm);
	ANQP_DUP(anqp_3gpp);
	ANQP_DUP(domain_name);
	ANQP_DUP(fils_realm_info);
#endif /* CONFIG_INTERWORKING */
#ifdef CONFIG_HS20
	ANQP_DUP(hs20_capability_list);
	ANQP_DUP(hs20_operator_friendly_name);
	ANQP_DUP(hs20_wan_metrics);
	ANQP_DUP(hs20_connection_capability);
	ANQP_DUP(hs20_operating_class);
	ANQP_DUP(hs20_osu_providers_list);
	ANQP_DUP(hs20_operator_icon_metadata);
	ANQP_DUP(hs20_osu_providers_nai_list);
#endif /* CONFIG_HS20 */
#undef ANQP_DUP

	return n;
}


/**
 * wpa_bss_anqp_unshare_alloc - Unshare ANQP data (if shared) in a BSS entry
 * @bss: BSS entry
 * Returns: 0 on success, -1 on failure
 *
 * This function ensures the specific BSS entry has an ANQP data structure that
 * is not shared with any other BSS entry.
 */
int wpa_bss_anqp_unshare_alloc(struct wpa_bss *bss)
{
	struct wpa_bss_anqp *anqp;

	if (bss->anqp && bss->anqp->users > 1) {
		/* allocated, but shared - clone an unshared copy */
		anqp = wpa_bss_anqp_clone(bss->anqp);
		if (anqp == NULL)
			return -1;
		anqp->users = 1;
		bss->anqp->users--;
		bss->anqp = anqp;
		return 0;
	}

	if (bss->anqp)
		return 0; /* already allocated and not shared */

	/* not allocated - allocate a new storage area */
	bss->anqp = wpa_bss_anqp_alloc();
	return bss->anqp ? 0 : -1;
}


/**
 * wpa_bss_anqp_free - Free an ANQP data structure
 * @anqp: ANQP data structure from wpa_bss_anqp_alloc() or wpa_bss_anqp_clone()
 */
static void wpa_bss_anqp_free(struct wpa_bss_anqp *anqp)
{
#ifdef CONFIG_INTERWORKING
	struct wpa_bss_anqp_elem *elem;
#endif /* CONFIG_INTERWORKING */

	if (anqp == NULL)
		return;

	anqp->users--;
	if (anqp->users > 0) {
		/* Another BSS entry holds a pointer to this ANQP info */
		return;
	}

#ifdef CONFIG_INTERWORKING
	wpabuf_free(anqp->capability_list);
	wpabuf_free(anqp->venue_name);
	wpabuf_free(anqp->network_auth_type);
	wpabuf_free(anqp->roaming_consortium);
	wpabuf_free(anqp->ip_addr_type_availability);
	wpabuf_free(anqp->nai_realm);
	wpabuf_free(anqp->anqp_3gpp);
	wpabuf_free(anqp->domain_name);
	wpabuf_free(anqp->fils_realm_info);

	while ((elem = dl_list_first(&anqp->anqp_elems,
				     struct wpa_bss_anqp_elem, list))) {
		dl_list_del(&elem->list);
		wpabuf_free(elem->payload);
		os_free(elem);
	}
#endif /* CONFIG_INTERWORKING */
#ifdef CONFIG_HS20
	wpabuf_free(anqp->hs20_capability_list);
	wpabuf_free(anqp->hs20_operator_friendly_name);
	wpabuf_free(anqp->hs20_wan_metrics);
	wpabuf_free(anqp->hs20_connection_capability);
	wpabuf_free(anqp->hs20_operating_class);
	wpabuf_free(anqp->hs20_osu_providers_list);
	wpabuf_free(anqp->hs20_operator_icon_metadata);
	wpabuf_free(anqp->hs20_osu_providers_nai_list);
#endif /* CONFIG_HS20 */

	os_free(anqp);
}


static void wpa_bss_update_pending_connect(struct wpa_supplicant *wpa_s,
					   struct wpa_bss *old_bss,
					   struct wpa_bss *new_bss)
{
	struct wpa_radio_work *work;
	struct wpa_connect_work *cwork;

	work = radio_work_pending(wpa_s, "sme-connect");
	if (!work)
		work = radio_work_pending(wpa_s, "connect");
	if (!work)
		return;

	cwork = work->ctx;
	if (cwork->bss != old_bss)
		return;

	wpa_printf(MSG_DEBUG,
		   "Update BSS pointer for the pending connect radio work");
	cwork->bss = new_bss;
	if (!new_bss)
		cwork->bss_removed = 1;
}


void wpa_bss_remove(struct wpa_supplicant *wpa_s, struct wpa_bss *bss,
		    const char *reason)
{
	if (wpa_s->last_scan_res) {
		unsigned int i;
		for (i = 0; i < wpa_s->last_scan_res_used; i++) {
			if (wpa_s->last_scan_res[i] == bss) {
				os_memmove(&wpa_s->last_scan_res[i],
					   &wpa_s->last_scan_res[i + 1],
					   (wpa_s->last_scan_res_used - i - 1)
					   * sizeof(struct wpa_bss *));
				wpa_s->last_scan_res_used--;
				break;
			}
		}
	}
	wpa_bss_update_pending_connect(wpa_s, bss, NULL);
	dl_list_del(&bss->list);
	dl_list_del(&bss->list_id);
	wpa_s->num_bss--;
	wpa_dbg(wpa_s, MSG_DEBUG, "BSS: Remove id %u BSSID " MACSTR
		" SSID '%s' due to %s", bss->id, MAC2STR(bss->bssid),
		wpa_ssid_txt(bss->ssid, bss->ssid_len), reason);
	wpas_notify_bss_removed(wpa_s, bss->bssid, bss->id);
	wpa_bss_anqp_free(bss->anqp);
	os_free(bss);
}


/**
 * wpa_bss_get - Fetch a BSS table entry based on BSSID and SSID
 * @wpa_s: Pointer to wpa_supplicant data
 * @bssid: BSSID
 * @ssid: SSID
 * @ssid_len: Length of @ssid
 * Returns: Pointer to the BSS entry or %NULL if not found
 */
struct wpa_bss * wpa_bss_get(struct wpa_supplicant *wpa_s, const u8 *bssid,
			     const u8 *ssid, size_t ssid_len)
{
	struct wpa_bss *bss;
	if (!wpa_supplicant_filter_bssid_match(wpa_s, bssid))
		return NULL;
	dl_list_for_each(bss, &wpa_s->bss, struct wpa_bss, list) {
		if (os_memcmp(bss->bssid, bssid, ETH_ALEN) == 0 &&
		    bss->ssid_len == ssid_len &&
		    os_memcmp(bss->ssid, ssid, ssid_len) == 0)
			return bss;
	}
	return NULL;
}


void calculate_update_time(const struct os_reltime *fetch_time,
			   unsigned int age_ms,
			   struct os_reltime *update_time)
{
	os_time_t usec;

	update_time->sec = fetch_time->sec;
	update_time->usec = fetch_time->usec;
	update_time->sec -= age_ms / 1000;
	usec = (age_ms % 1000) * 1000;
	if (update_time->usec < usec) {
		update_time->sec--;
		update_time->usec += 1000000;
	}
	update_time->usec -= usec;
}


static void wpa_bss_copy_res(struct wpa_bss *dst, struct wpa_scan_res *src,
			     struct os_reltime *fetch_time)
{
	dst->flags = src->flags;
	os_memcpy(dst->bssid, src->bssid, ETH_ALEN);
	dst->freq = src->freq;
	dst->beacon_int = src->beacon_int;
	dst->caps = src->caps;
	dst->qual = src->qual;
	dst->noise = src->noise;
	dst->level = src->level;
	dst->tsf = src->tsf;
	dst->est_throughput = src->est_throughput;
	dst->snr = src->snr;

	calculate_update_time(fetch_time, src->age, &dst->last_update);
}


static int wpa_bss_is_wps_candidate(struct wpa_supplicant *wpa_s,
				    struct wpa_bss *bss)
{
#ifdef CONFIG_WPS
	struct wpa_ssid *ssid;
	struct wpabuf *wps_ie;
	int pbc = 0, ret;

	wps_ie = wpa_bss_get_vendor_ie_multi(bss, WPS_IE_VENDOR_TYPE);
	if (!wps_ie)
		return 0;

	if (wps_is_selected_pbc_registrar(wps_ie)) {
		pbc = 1;
	} else if (!wps_is_addr_authorized(wps_ie, wpa_s->own_addr, 1)) {
		wpabuf_free(wps_ie);
		return 0;
	}

	for (ssid = wpa_s->conf->ssid; ssid; ssid = ssid->next) {
		if (!(ssid->key_mgmt & WPA_KEY_MGMT_WPS))
			continue;
		if (ssid->ssid_len &&
		    (ssid->ssid_len != bss->ssid_len ||
		     os_memcmp(ssid->ssid, bss->ssid, ssid->ssid_len) != 0))
			continue;

		if (pbc)
			ret = eap_is_wps_pbc_enrollee(&ssid->eap);
		else
			ret = eap_is_wps_pin_enrollee(&ssid->eap);
		wpabuf_free(wps_ie);
		return ret;
	}
	wpabuf_free(wps_ie);
#endif /* CONFIG_WPS */

	return 0;
}


static int wpa_bss_known(struct wpa_supplicant *wpa_s, struct wpa_bss *bss)
{
	struct wpa_ssid *ssid;

	for (ssid = wpa_s->conf->ssid; ssid; ssid = ssid->next) {
		if (ssid->ssid == NULL || ssid->ssid_len == 0)
			continue;
		if (ssid->ssid_len == bss->ssid_len &&
		    os_memcmp(ssid->ssid, bss->ssid, ssid->ssid_len) == 0)
			return 1;
	}

	return 0;
}


static int wpa_bss_in_use(struct wpa_supplicant *wpa_s, struct wpa_bss *bss)
{
	if (bss == wpa_s->current_bss)
		return 1;

	if (wpa_s->current_bss &&
	    (bss->ssid_len != wpa_s->current_bss->ssid_len ||
	     os_memcmp(bss->ssid, wpa_s->current_bss->ssid,
		       bss->ssid_len) != 0))
		return 0; /* SSID has changed */

	return !is_zero_ether_addr(bss->bssid) &&
		(os_memcmp(bss->bssid, wpa_s->bssid, ETH_ALEN) == 0 ||
		 os_memcmp(bss->bssid, wpa_s->pending_bssid, ETH_ALEN) == 0);
}


static int wpa_bss_remove_oldest_unknown(struct wpa_supplicant *wpa_s)
{
	struct wpa_bss *bss;

	dl_list_for_each(bss, &wpa_s->bss, struct wpa_bss, list) {
		if (!wpa_bss_known(wpa_s, bss) &&
		    !wpa_bss_is_wps_candidate(wpa_s, bss)) {
			wpa_bss_remove(wpa_s, bss, __func__);
			return 0;
		}
	}

	return -1;
}


static int wpa_bss_remove_oldest(struct wpa_supplicant *wpa_s)
{
	struct wpa_bss *bss;

	/*
	 * Remove the oldest entry that does not match with any configured
	 * network.
	 */
	if (wpa_bss_remove_oldest_unknown(wpa_s) == 0)
		return 0;

	/*
	 * Remove the oldest entry that isn't currently in use.
	 */
	dl_list_for_each(bss, &wpa_s->bss, struct wpa_bss, list) {
		if (!wpa_bss_in_use(wpa_s, bss)) {
			wpa_bss_remove(wpa_s, bss, __func__);
			return 0;
		}
	}

	return -1;
}


static struct wpa_bss * wpa_bss_add(struct wpa_supplicant *wpa_s,
				    const u8 *ssid, size_t ssid_len,
				    struct wpa_scan_res *res,
				    struct os_reltime *fetch_time)
{
	struct wpa_bss *bss;

	bss = os_zalloc(sizeof(*bss) + res->ie_len + res->beacon_ie_len);
	if (bss == NULL)
		return NULL;
	bss->id = wpa_s->bss_next_id++;
	bss->last_update_idx = wpa_s->bss_update_idx;
	wpa_bss_copy_res(bss, res, fetch_time);
	os_memcpy(bss->ssid, ssid, ssid_len);
	bss->ssid_len = ssid_len;
	bss->ie_len = res->ie_len;
	bss->beacon_ie_len = res->beacon_ie_len;
	os_memcpy(bss + 1, res + 1, res->ie_len + res->beacon_ie_len);
	wpa_bss_set_hessid(bss);

	if (wpa_s->num_bss + 1 > wpa_s->conf->bss_max_count &&
	    wpa_bss_remove_oldest(wpa_s) != 0) {
		wpa_printf(MSG_ERROR, "Increasing the MAX BSS count to %d "
			   "because all BSSes are in use. We should normally "
			   "not get here!", (int) wpa_s->num_bss + 1);
		wpa_s->conf->bss_max_count = wpa_s->num_bss + 1;
	}

	dl_list_add_tail(&wpa_s->bss, &bss->list);
	dl_list_add_tail(&wpa_s->bss_id, &bss->list_id);
	wpa_s->num_bss++;
	wpa_dbg(wpa_s, MSG_DEBUG, "BSS: Add new id %u BSSID " MACSTR
		" SSID '%s' freq %d",
		bss->id, MAC2STR(bss->bssid), wpa_ssid_txt(ssid, ssid_len),
		bss->freq);
	wpas_notify_bss_added(wpa_s, bss->bssid, bss->id);
	return bss;
}


static int are_ies_equal(const struct wpa_bss *old,
			 const struct wpa_scan_res *new_res, u32 ie)
{
	const u8 *old_ie, *new_ie;
	struct wpabuf *old_ie_buff = NULL;
	struct wpabuf *new_ie_buff = NULL;
	int new_ie_len, old_ie_len, ret, is_multi;

	switch (ie) {
	case WPA_IE_VENDOR_TYPE:
		old_ie = wpa_bss_get_vendor_ie(old, ie);
		new_ie = wpa_scan_get_vendor_ie(new_res, ie);
		is_multi = 0;
		break;
	case WPS_IE_VENDOR_TYPE:
		old_ie_buff = wpa_bss_get_vendor_ie_multi(old, ie);
		new_ie_buff = wpa_scan_get_vendor_ie_multi(new_res, ie);
		is_multi = 1;
		break;
	case WLAN_EID_RSN:
	case WLAN_EID_SUPP_RATES:
	case WLAN_EID_EXT_SUPP_RATES:
		old_ie = wpa_bss_get_ie(old, ie);
		new_ie = wpa_scan_get_ie(new_res, ie);
		is_multi = 0;
		break;
	default:
		wpa_printf(MSG_DEBUG, "bss: %s: cannot compare IEs", __func__);
		return 0;
	}

	if (is_multi) {
		/* in case of multiple IEs stored in buffer */
		old_ie = old_ie_buff ? wpabuf_head_u8(old_ie_buff) : NULL;
		new_ie = new_ie_buff ? wpabuf_head_u8(new_ie_buff) : NULL;
		old_ie_len = old_ie_buff ? wpabuf_len(old_ie_buff) : 0;
		new_ie_len = new_ie_buff ? wpabuf_len(new_ie_buff) : 0;
	} else {
		/* in case of single IE */
		old_ie_len = old_ie ? old_ie[1] + 2 : 0;
		new_ie_len = new_ie ? new_ie[1] + 2 : 0;
	}

	if (!old_ie || !new_ie)
		ret = !old_ie && !new_ie;
	else
		ret = (old_ie_len == new_ie_len &&
		       os_memcmp(old_ie, new_ie, old_ie_len) == 0);

	wpabuf_free(old_ie_buff);
	wpabuf_free(new_ie_buff);

	return ret;
}


static u32 wpa_bss_compare_res(const struct wpa_bss *old,
			       const struct wpa_scan_res *new_res)
{
	u32 changes = 0;
	int caps_diff = old->caps ^ new_res->caps;

	if (old->freq != new_res->freq)
		changes |= WPA_BSS_FREQ_CHANGED_FLAG;

	if (old->level != new_res->level)
		changes |= WPA_BSS_SIGNAL_CHANGED_FLAG;

	if (caps_diff & IEEE80211_CAP_PRIVACY)
		changes |= WPA_BSS_PRIVACY_CHANGED_FLAG;

	if (caps_diff & IEEE80211_CAP_IBSS)
		changes |= WPA_BSS_MODE_CHANGED_FLAG;

	if (old->ie_len == new_res->ie_len &&
	    os_memcmp(old + 1, new_res + 1, old->ie_len) == 0)
		return changes;
	changes |= WPA_BSS_IES_CHANGED_FLAG;

	if (!are_ies_equal(old, new_res, WPA_IE_VENDOR_TYPE))
		changes |= WPA_BSS_WPAIE_CHANGED_FLAG;

	if (!are_ies_equal(old, new_res, WLAN_EID_RSN))
		changes |= WPA_BSS_RSNIE_CHANGED_FLAG;

	if (!are_ies_equal(old, new_res, WPS_IE_VENDOR_TYPE))
		changes |= WPA_BSS_WPS_CHANGED_FLAG;

	if (!are_ies_equal(old, new_res, WLAN_EID_SUPP_RATES) ||
	    !are_ies_equal(old, new_res, WLAN_EID_EXT_SUPP_RATES))
		changes |= WPA_BSS_RATES_CHANGED_FLAG;

	return changes;
}


static void notify_bss_changes(struct wpa_supplicant *wpa_s, u32 changes,
			       const struct wpa_bss *bss)
{
	if (changes & WPA_BSS_FREQ_CHANGED_FLAG)
		wpas_notify_bss_freq_changed(wpa_s, bss->id);

	if (changes & WPA_BSS_SIGNAL_CHANGED_FLAG)
		wpas_notify_bss_signal_changed(wpa_s, bss->id);

	if (changes & WPA_BSS_PRIVACY_CHANGED_FLAG)
		wpas_notify_bss_privacy_changed(wpa_s, bss->id);

	if (changes & WPA_BSS_MODE_CHANGED_FLAG)
		wpas_notify_bss_mode_changed(wpa_s, bss->id);

	if (changes & WPA_BSS_WPAIE_CHANGED_FLAG)
		wpas_notify_bss_wpaie_changed(wpa_s, bss->id);

	if (changes & WPA_BSS_RSNIE_CHANGED_FLAG)
		wpas_notify_bss_rsnie_changed(wpa_s, bss->id);

	if (changes & WPA_BSS_WPS_CHANGED_FLAG)
		wpas_notify_bss_wps_changed(wpa_s, bss->id);

	if (changes & WPA_BSS_IES_CHANGED_FLAG)
		wpas_notify_bss_ies_changed(wpa_s, bss->id);

	if (changes & WPA_BSS_RATES_CHANGED_FLAG)
		wpas_notify_bss_rates_changed(wpa_s, bss->id);

	wpas_notify_bss_seen(wpa_s, bss->id);
}


static struct wpa_bss *
wpa_bss_update(struct wpa_supplicant *wpa_s, struct wpa_bss *bss,
	       struct wpa_scan_res *res, struct os_reltime *fetch_time)
{
	u32 changes;

	if (bss->last_update_idx == wpa_s->bss_update_idx) {
		struct os_reltime update_time;

		/*
		 * Some drivers (e.g., cfg80211) include multiple BSS entries
		 * for the same BSS if that BSS's channel changes. The BSS list
		 * implementation in wpa_supplicant does not do that and we need
		 * to filter out the obsolete results here to make sure only the
		 * most current BSS information remains in the table.
		 */
		wpa_printf(MSG_DEBUG, "BSS: " MACSTR
			   " has multiple entries in the scan results - select the most current one",
			   MAC2STR(bss->bssid));
		calculate_update_time(fetch_time, res->age, &update_time);
		wpa_printf(MSG_DEBUG,
			   "Previous last_update: %u.%06u (freq %d%s)",
			   (unsigned int) bss->last_update.sec,
			   (unsigned int) bss->last_update.usec,
			   bss->freq,
			   (bss->flags & WPA_BSS_ASSOCIATED) ? " assoc" : "");
		wpa_printf(MSG_DEBUG, "New last_update: %u.%06u (freq %d%s)",
			   (unsigned int) update_time.sec,
			   (unsigned int) update_time.usec,
			   res->freq,
			   (res->flags & WPA_SCAN_ASSOCIATED) ? " assoc" : "");
		if ((bss->flags & WPA_BSS_ASSOCIATED) ||
		    (!(res->flags & WPA_SCAN_ASSOCIATED) &&
		     !os_reltime_before(&bss->last_update, &update_time))) {
			wpa_printf(MSG_DEBUG,
				   "Ignore this BSS entry since the previous update looks more current");
			return bss;
		}
		wpa_printf(MSG_DEBUG,
			   "Accept this BSS entry since it looks more current than the previous update");
	}

	changes = wpa_bss_compare_res(bss, res);
	if (changes & WPA_BSS_FREQ_CHANGED_FLAG)
		wpa_printf(MSG_DEBUG, "BSS: " MACSTR " changed freq %d --> %d",
			   MAC2STR(bss->bssid), bss->freq, res->freq);
	bss->scan_miss_count = 0;
	bss->last_update_idx = wpa_s->bss_update_idx;
	wpa_bss_copy_res(bss, res, fetch_time);
	/* Move the entry to the end of the list */
	dl_list_del(&bss->list);
#ifdef CONFIG_P2P
	if (wpa_bss_get_vendor_ie(bss, P2P_IE_VENDOR_TYPE) &&
	    !wpa_scan_get_vendor_ie(res, P2P_IE_VENDOR_TYPE)) {
		/*
		 * This can happen when non-P2P station interface runs a scan
		 * without P2P IE in the Probe Request frame. P2P GO would reply
		 * to that with a Probe Response that does not include P2P IE.
		 * Do not update the IEs in this BSS entry to avoid such loss of
		 * information that may be needed for P2P operations to
		 * determine group information.
		 */
		wpa_dbg(wpa_s, MSG_DEBUG, "BSS: Do not update scan IEs for "
			MACSTR " since that would remove P2P IE information",
			MAC2STR(bss->bssid));
	} else
#endif /* CONFIG_P2P */
	if (bss->ie_len + bss->beacon_ie_len >=
	    res->ie_len + res->beacon_ie_len) {
		os_memcpy(bss + 1, res + 1, res->ie_len + res->beacon_ie_len);
		bss->ie_len = res->ie_len;
		bss->beacon_ie_len = res->beacon_ie_len;
	} else {
		struct wpa_bss *nbss;
		struct dl_list *prev = bss->list_id.prev;
		dl_list_del(&bss->list_id);
		nbss = os_realloc(bss, sizeof(*bss) + res->ie_len +
				  res->beacon_ie_len);
		if (nbss) {
			unsigned int i;
			for (i = 0; i < wpa_s->last_scan_res_used; i++) {
				if (wpa_s->last_scan_res[i] == bss) {
					wpa_s->last_scan_res[i] = nbss;
					break;
				}
			}
			if (wpa_s->current_bss == bss)
				wpa_s->current_bss = nbss;
			wpa_bss_update_pending_connect(wpa_s, bss, nbss);
			bss = nbss;
			os_memcpy(bss + 1, res + 1,
				  res->ie_len + res->beacon_ie_len);
			bss->ie_len = res->ie_len;
			bss->beacon_ie_len = res->beacon_ie_len;
		}
		dl_list_add(prev, &bss->list_id);
	}
	if (changes & WPA_BSS_IES_CHANGED_FLAG)
		wpa_bss_set_hessid(bss);
	dl_list_add_tail(&wpa_s->bss, &bss->list);

	notify_bss_changes(wpa_s, changes, bss);

	return bss;
}


/**
 * wpa_bss_update_start - Start a BSS table update from scan results
 * @wpa_s: Pointer to wpa_supplicant data
 *
 * This function is called at the start of each BSS table update round for new
 * scan results. The actual scan result entries are indicated with calls to
 * wpa_bss_update_scan_res() and the update round is finished with a call to
 * wpa_bss_update_end().
 */
void wpa_bss_update_start(struct wpa_supplicant *wpa_s)
{
	wpa_s->bss_update_idx++;
	wpa_dbg(wpa_s, MSG_DEBUG, "BSS: Start scan result update %u",
		wpa_s->bss_update_idx);
	wpa_s->last_scan_res_used = 0;
}


/**
 * wpa_bss_update_scan_res - Update a BSS table entry based on a scan result
 * @wpa_s: Pointer to wpa_supplicant data
 * @res: Scan result
 * @fetch_time: Time when the result was fetched from the driver
 *
 * This function updates a BSS table entry (or adds one) based on a scan result.
 * This is called separately for each scan result between the calls to
 * wpa_bss_update_start() and wpa_bss_update_end().
 */
void wpa_bss_update_scan_res(struct wpa_supplicant *wpa_s,
			     struct wpa_scan_res *res,
			     struct os_reltime *fetch_time)
{
	const u8 *ssid, *p2p, *mesh;
	struct wpa_bss *bss;

	if (wpa_s->conf->ignore_old_scan_res) {
		struct os_reltime update;
		calculate_update_time(fetch_time, res->age, &update);
		if (os_reltime_before(&update, &wpa_s->scan_trigger_time)) {
			struct os_reltime age;
			os_reltime_sub(&wpa_s->scan_trigger_time, &update,
				       &age);
			wpa_dbg(wpa_s, MSG_DEBUG, "BSS: Ignore driver BSS "
				"table entry that is %u.%06u seconds older "
				"than our scan trigger",
				(unsigned int) age.sec,
				(unsigned int) age.usec);
			return;
		}
	}

	ssid = wpa_scan_get_ie(res, WLAN_EID_SSID);
	if (ssid == NULL) {
		wpa_dbg(wpa_s, MSG_DEBUG, "BSS: No SSID IE included for "
			MACSTR, MAC2STR(res->bssid));
		return;
	}
	if (ssid[1] > SSID_MAX_LEN) {
		wpa_dbg(wpa_s, MSG_DEBUG, "BSS: Too long SSID IE included for "
			MACSTR, MAC2STR(res->bssid));
		return;
	}

	p2p = wpa_scan_get_vendor_ie(res, P2P_IE_VENDOR_TYPE);
#ifdef CONFIG_P2P
	if (p2p == NULL &&
	    wpa_s->p2p_group_interface != NOT_P2P_GROUP_INTERFACE) {
		/*
		 * If it's a P2P specific interface, then don't update
		 * the scan result without a P2P IE.
		 */
		wpa_printf(MSG_DEBUG, "BSS: No P2P IE - skipping BSS " MACSTR
			   " update for P2P interface", MAC2STR(res->bssid));
		return;
	}
#endif /* CONFIG_P2P */
	if (p2p && ssid[1] == P2P_WILDCARD_SSID_LEN &&
	    os_memcmp(ssid + 2, P2P_WILDCARD_SSID, P2P_WILDCARD_SSID_LEN) == 0)
		return; /* Skip P2P listen discovery results here */

	/* TODO: add option for ignoring BSSes we are not interested in
	 * (to save memory) */

	mesh = wpa_scan_get_ie(res, WLAN_EID_MESH_ID);
	if (mesh && mesh[1] <= SSID_MAX_LEN)
		ssid = mesh;

	bss = wpa_bss_get(wpa_s, res->bssid, ssid + 2, ssid[1]);
	if (bss == NULL)
		bss = wpa_bss_add(wpa_s, ssid + 2, ssid[1], res, fetch_time);
	else {
		bss = wpa_bss_update(wpa_s, bss, res, fetch_time);
		if (wpa_s->last_scan_res) {
			unsigned int i;
			for (i = 0; i < wpa_s->last_scan_res_used; i++) {
				if (bss == wpa_s->last_scan_res[i]) {
					/* Already in the list */
					return;
				}
			}
		}
	}

	if (bss == NULL)
		return;
	if (wpa_s->last_scan_res_used >= wpa_s->last_scan_res_size) {
		struct wpa_bss **n;
		unsigned int siz;
		if (wpa_s->last_scan_res_size == 0)
			siz = 32;
		else
			siz = wpa_s->last_scan_res_size * 2;
		n = os_realloc_array(wpa_s->last_scan_res, siz,
				     sizeof(struct wpa_bss *));
		if (n == NULL)
			return;
		wpa_s->last_scan_res = n;
		wpa_s->last_scan_res_size = siz;
	}

	if (wpa_s->last_scan_res)
		wpa_s->last_scan_res[wpa_s->last_scan_res_used++] = bss;
}


static int wpa_bss_included_in_scan(const struct wpa_bss *bss,
				    const struct scan_info *info)
{
	int found;
	size_t i;

	if (info == NULL)
		return 1;

	if (info->num_freqs) {
		found = 0;
		for (i = 0; i < info->num_freqs; i++) {
			if (bss->freq == info->freqs[i]) {
				found = 1;
				break;
			}
		}
		if (!found)
			return 0;
	}

	if (info->num_ssids) {
		found = 0;
		for (i = 0; i < info->num_ssids; i++) {
			const struct wpa_driver_scan_ssid *s = &info->ssids[i];
			if ((s->ssid == NULL || s->ssid_len == 0) ||
			    (s->ssid_len == bss->ssid_len &&
			     os_memcmp(s->ssid, bss->ssid, bss->ssid_len) ==
			     0)) {
				found = 1;
				break;
			}
		}
		if (!found)
			return 0;
	}

	return 1;
}


/**
 * wpa_bss_update_end - End a BSS table update from scan results
 * @wpa_s: Pointer to wpa_supplicant data
 * @info: Information about scan parameters
 * @new_scan: Whether this update round was based on a new scan
 *
 * This function is called at the end of each BSS table update round for new
 * scan results. The start of the update was indicated with a call to
 * wpa_bss_update_start().
 */
void wpa_bss_update_end(struct wpa_supplicant *wpa_s, struct scan_info *info,
			int new_scan)
{
	struct wpa_bss *bss, *n;

	os_get_reltime(&wpa_s->last_scan);
	if ((info && info->aborted) || !new_scan)
		return; /* do not expire entries without new scan */

	dl_list_for_each_safe(bss, n, &wpa_s->bss, struct wpa_bss, list) {
		if (wpa_bss_in_use(wpa_s, bss))
			continue;
		if (!wpa_bss_included_in_scan(bss, info))
			continue; /* expire only BSSes that were scanned */
		if (bss->last_update_idx < wpa_s->bss_update_idx)
			bss->scan_miss_count++;
		if (bss->scan_miss_count >=
		    wpa_s->conf->bss_expiration_scan_count) {
			wpa_bss_remove(wpa_s, bss, "no match in scan");
		}
	}

	wpa_printf(MSG_DEBUG, "BSS: last_scan_res_used=%u/%u",
		   wpa_s->last_scan_res_used, wpa_s->last_scan_res_size);
}


/**
 * wpa_bss_flush_by_age - Flush old BSS entries
 * @wpa_s: Pointer to wpa_supplicant data
 * @age: Maximum entry age in seconds
 *
 * Remove BSS entries that have not been updated during the last @age seconds.
 */
void wpa_bss_flush_by_age(struct wpa_supplicant *wpa_s, int age)
{
	struct wpa_bss *bss, *n;
	struct os_reltime t;

	if (dl_list_empty(&wpa_s->bss))
		return;

	os_get_reltime(&t);
	t.sec -= age;

	dl_list_for_each_safe(bss, n, &wpa_s->bss, struct wpa_bss, list) {
		if (wpa_bss_in_use(wpa_s, bss))
			continue;

		if (os_reltime_before(&bss->last_update, &t)) {
			wpa_bss_remove(wpa_s, bss, __func__);
		} else
			break;
	}
}


/**
 * wpa_bss_init - Initialize BSS table
 * @wpa_s: Pointer to wpa_supplicant data
 * Returns: 0 on success, -1 on failure
 *
 * This prepares BSS table lists and timer for periodic updates. The BSS table
 * is deinitialized with wpa_bss_deinit() once not needed anymore.
 */
int wpa_bss_init(struct wpa_supplicant *wpa_s)
{
	dl_list_init(&wpa_s->bss);
	dl_list_init(&wpa_s->bss_id);
	return 0;
}


/**
 * wpa_bss_flush - Flush all unused BSS entries
 * @wpa_s: Pointer to wpa_supplicant data
 */
void wpa_bss_flush(struct wpa_supplicant *wpa_s)
{
	struct wpa_bss *bss, *n;

	wpa_s->clear_driver_scan_cache = 1;

	if (wpa_s->bss.next == NULL)
		return; /* BSS table not yet initialized */

	dl_list_for_each_safe(bss, n, &wpa_s->bss, struct wpa_bss, list) {
		if (wpa_bss_in_use(wpa_s, bss))
			continue;
		wpa_bss_remove(wpa_s, bss, __func__);
	}
}


/**
 * wpa_bss_deinit - Deinitialize BSS table
 * @wpa_s: Pointer to wpa_supplicant data
 */
void wpa_bss_deinit(struct wpa_supplicant *wpa_s)
{
	wpa_bss_flush(wpa_s);
}


/**
 * wpa_bss_get_bssid - Fetch a BSS table entry based on BSSID
 * @wpa_s: Pointer to wpa_supplicant data
 * @bssid: BSSID
 * Returns: Pointer to the BSS entry or %NULL if not found
 */
struct wpa_bss * wpa_bss_get_bssid(struct wpa_supplicant *wpa_s,
				   const u8 *bssid)
{
	struct wpa_bss *bss;
	if (!wpa_supplicant_filter_bssid_match(wpa_s, bssid))
		return NULL;
	dl_list_for_each_reverse(bss, &wpa_s->bss, struct wpa_bss, list) {
		if (os_memcmp(bss->bssid, bssid, ETH_ALEN) == 0)
			return bss;
	}
	return NULL;
}


/**
 * wpa_bss_get_bssid_latest - Fetch the latest BSS table entry based on BSSID
 * @wpa_s: Pointer to wpa_supplicant data
 * @bssid: BSSID
 * Returns: Pointer to the BSS entry or %NULL if not found
 *
 * This function is like wpa_bss_get_bssid(), but full BSS table is iterated to
 * find the entry that has the most recent update. This can help in finding the
 * correct entry in cases where the SSID of the AP may have changed recently
 * (e.g., in WPS reconfiguration cases).
 */
struct wpa_bss * wpa_bss_get_bssid_latest(struct wpa_supplicant *wpa_s,
					  const u8 *bssid)
{
	struct wpa_bss *bss, *found = NULL;
	if (!wpa_supplicant_filter_bssid_match(wpa_s, bssid))
		return NULL;
	dl_list_for_each_reverse(bss, &wpa_s->bss, struct wpa_bss, list) {
		if (os_memcmp(bss->bssid, bssid, ETH_ALEN) != 0)
			continue;
		if (found == NULL ||
		    os_reltime_before(&found->last_update, &bss->last_update))
			found = bss;
	}
	return found;
}


#ifdef CONFIG_P2P
/**
 * wpa_bss_get_p2p_dev_addr - Fetch a BSS table entry based on P2P Device Addr
 * @wpa_s: Pointer to wpa_supplicant data
 * @dev_addr: P2P Device Address of the GO
 * Returns: Pointer to the BSS entry or %NULL if not found
 */
struct wpa_bss * wpa_bss_get_p2p_dev_addr(struct wpa_supplicant *wpa_s,
					  const u8 *dev_addr)
{
	struct wpa_bss *bss;
	dl_list_for_each_reverse(bss, &wpa_s->bss, struct wpa_bss, list) {
		u8 addr[ETH_ALEN];
		if (p2p_parse_dev_addr((const u8 *) (bss + 1), bss->ie_len,
				       addr) == 0 &&
		    os_memcmp(addr, dev_addr, ETH_ALEN) == 0)
			return bss;
	}
	return NULL;
}
#endif /* CONFIG_P2P */


/**
 * wpa_bss_get_id - Fetch a BSS table entry based on identifier
 * @wpa_s: Pointer to wpa_supplicant data
 * @id: Unique identifier (struct wpa_bss::id) assigned for the entry
 * Returns: Pointer to the BSS entry or %NULL if not found
 */
struct wpa_bss * wpa_bss_get_id(struct wpa_supplicant *wpa_s, unsigned int id)
{
	struct wpa_bss *bss;
	dl_list_for_each(bss, &wpa_s->bss, struct wpa_bss, list) {
		if (bss->id == id)
			return bss;
	}
	return NULL;
}


/**
 * wpa_bss_get_id_range - Fetch a BSS table entry based on identifier range
 * @wpa_s: Pointer to wpa_supplicant data
 * @idf: Smallest allowed identifier assigned for the entry
 * @idf: Largest allowed identifier assigned for the entry
 * Returns: Pointer to the BSS entry or %NULL if not found
 *
 * This function is similar to wpa_bss_get_id() but allows a BSS entry with the
 * smallest id value to be fetched within the specified range without the
 * caller having to know the exact id.
 */
struct wpa_bss * wpa_bss_get_id_range(struct wpa_supplicant *wpa_s,
				      unsigned int idf, unsigned int idl)
{
	struct wpa_bss *bss;
	dl_list_for_each(bss, &wpa_s->bss_id, struct wpa_bss, list_id) {
		if (bss->id >= idf && bss->id <= idl)
			return bss;
	}
	return NULL;
}


/**
 * wpa_bss_get_ie - Fetch a specified information element from a BSS entry
 * @bss: BSS table entry
 * @ie: Information element identitifier (WLAN_EID_*)
 * Returns: Pointer to the information element (id field) or %NULL if not found
 *
 * This function returns the first matching information element in the BSS
 * entry.
 */
const u8 * wpa_bss_get_ie(const struct wpa_bss *bss, u8 ie)
{
	return get_ie((const u8 *) (bss + 1), bss->ie_len, ie);
}


/**
 * wpa_bss_get_vendor_ie - Fetch a vendor information element from a BSS entry
 * @bss: BSS table entry
 * @vendor_type: Vendor type (four octets starting the IE payload)
 * Returns: Pointer to the information element (id field) or %NULL if not found
 *
 * This function returns the first matching information element in the BSS
 * entry.
 */
const u8 * wpa_bss_get_vendor_ie(const struct wpa_bss *bss, u32 vendor_type)
{
	const u8 *end, *pos;

	pos = (const u8 *) (bss + 1);
	end = pos + bss->ie_len;

	while (end - pos > 1) {
		if (2 + pos[1] > end - pos)
			break;
		if (pos[0] == WLAN_EID_VENDOR_SPECIFIC && pos[1] >= 4 &&
		    vendor_type == WPA_GET_BE32(&pos[2]))
			return pos;
		pos += 2 + pos[1];
	}

	return NULL;
}


/**
 * wpa_bss_get_vendor_ie_beacon - Fetch a vendor information from a BSS entry
 * @bss: BSS table entry
 * @vendor_type: Vendor type (four octets starting the IE payload)
 * Returns: Pointer to the information element (id field) or %NULL if not found
 *
 * This function returns the first matching information element in the BSS
 * entry.
 *
 * This function is like wpa_bss_get_vendor_ie(), but uses IE buffer only
 * from Beacon frames instead of either Beacon or Probe Response frames.
 */
const u8 * wpa_bss_get_vendor_ie_beacon(const struct wpa_bss *bss,
					u32 vendor_type)
{
	const u8 *end, *pos;

	if (bss->beacon_ie_len == 0)
		return NULL;

	pos = (const u8 *) (bss + 1);
	pos += bss->ie_len;
	end = pos + bss->beacon_ie_len;

	while (end - pos > 1) {
		if (2 + pos[1] > end - pos)
			break;
		if (pos[0] == WLAN_EID_VENDOR_SPECIFIC && pos[1] >= 4 &&
		    vendor_type == WPA_GET_BE32(&pos[2]))
			return pos;
		pos += 2 + pos[1];
	}

	return NULL;
}


/**
 * wpa_bss_get_vendor_ie_multi - Fetch vendor IE data from a BSS entry
 * @bss: BSS table entry
 * @vendor_type: Vendor type (four octets starting the IE payload)
 * Returns: Pointer to the information element payload or %NULL if not found
 *
 * This function returns concatenated payload of possibly fragmented vendor
 * specific information elements in the BSS entry. The caller is responsible for
 * freeing the returned buffer.
 */
struct wpabuf * wpa_bss_get_vendor_ie_multi(const struct wpa_bss *bss,
					    u32 vendor_type)
{
	struct wpabuf *buf;
	const u8 *end, *pos;

	buf = wpabuf_alloc(bss->ie_len);
	if (buf == NULL)
		return NULL;

	pos = (const u8 *) (bss + 1);
	end = pos + bss->ie_len;

	while (end - pos > 1) {
		if (2 + pos[1] > end - pos)
			break;
		if (pos[0] == WLAN_EID_VENDOR_SPECIFIC && pos[1] >= 4 &&
		    vendor_type == WPA_GET_BE32(&pos[2]))
			wpabuf_put_data(buf, pos + 2 + 4, pos[1] - 4);
		pos += 2 + pos[1];
	}

	if (wpabuf_len(buf) == 0) {
		wpabuf_free(buf);
		buf = NULL;
	}

	return buf;
}


/**
 * wpa_bss_get_vendor_ie_multi_beacon - Fetch vendor IE data from a BSS entry
 * @bss: BSS table entry
 * @vendor_type: Vendor type (four octets starting the IE payload)
 * Returns: Pointer to the information element payload or %NULL if not found
 *
 * This function returns concatenated payload of possibly fragmented vendor
 * specific information elements in the BSS entry. The caller is responsible for
 * freeing the returned buffer.
 *
 * This function is like wpa_bss_get_vendor_ie_multi(), but uses IE buffer only
 * from Beacon frames instead of either Beacon or Probe Response frames.
 */
struct wpabuf * wpa_bss_get_vendor_ie_multi_beacon(const struct wpa_bss *bss,
						   u32 vendor_type)
{
	struct wpabuf *buf;
	const u8 *end, *pos;

	buf = wpabuf_alloc(bss->beacon_ie_len);
	if (buf == NULL)
		return NULL;

	pos = (const u8 *) (bss + 1);
	pos += bss->ie_len;
	end = pos + bss->beacon_ie_len;

	while (end - pos > 1) {
		if (2 + pos[1] > end - pos)
			break;
		if (pos[0] == WLAN_EID_VENDOR_SPECIFIC && pos[1] >= 4 &&
		    vendor_type == WPA_GET_BE32(&pos[2]))
			wpabuf_put_data(buf, pos + 2 + 4, pos[1] - 4);
		pos += 2 + pos[1];
	}

	if (wpabuf_len(buf) == 0) {
		wpabuf_free(buf);
		buf = NULL;
	}

	return buf;
}


/**
 * wpa_bss_get_max_rate - Get maximum legacy TX rate supported in a BSS
 * @bss: BSS table entry
 * Returns: Maximum legacy rate in units of 500 kbps
 */
int wpa_bss_get_max_rate(const struct wpa_bss *bss)
{
	int rate = 0;
	const u8 *ie;
	int i;

	ie = wpa_bss_get_ie(bss, WLAN_EID_SUPP_RATES);
	for (i = 0; ie && i < ie[1]; i++) {
		if ((ie[i + 2] & 0x7f) > rate)
			rate = ie[i + 2] & 0x7f;
	}

	ie = wpa_bss_get_ie(bss, WLAN_EID_EXT_SUPP_RATES);
	for (i = 0; ie && i < ie[1]; i++) {
		if ((ie[i + 2] & 0x7f) > rate)
			rate = ie[i + 2] & 0x7f;
	}

	return rate;
}


/**
 * wpa_bss_get_bit_rates - Get legacy TX rates supported in a BSS
 * @bss: BSS table entry
 * @rates: Buffer for returning a pointer to the rates list (units of 500 kbps)
 * Returns: number of legacy TX rates or -1 on failure
 *
 * The caller is responsible for freeing the returned buffer with os_free() in
 * case of success.
 */
int wpa_bss_get_bit_rates(const struct wpa_bss *bss, u8 **rates)
{
	const u8 *ie, *ie2;
	int i, j;
	unsigned int len;
	u8 *r;

	ie = wpa_bss_get_ie(bss, WLAN_EID_SUPP_RATES);
	ie2 = wpa_bss_get_ie(bss, WLAN_EID_EXT_SUPP_RATES);

	len = (ie ? ie[1] : 0) + (ie2 ? ie2[1] : 0);

	r = os_malloc(len);
	if (!r)
		return -1;

	for (i = 0; ie && i < ie[1]; i++)
		r[i] = ie[i + 2] & 0x7f;

	for (j = 0; ie2 && j < ie2[1]; j++)
		r[i + j] = ie2[j + 2] & 0x7f;

	*rates = r;
	return len;
}


#ifdef CONFIG_FILS
const u8 * wpa_bss_get_fils_cache_id(struct wpa_bss *bss)
{
	const u8 *ie;

	if (bss) {
		ie = wpa_bss_get_ie(bss, WLAN_EID_FILS_INDICATION);
		if (ie && ie[1] >= 4 && WPA_GET_LE16(ie + 2) & BIT(7))
			return ie + 4;
	}

	return NULL;
}
#endif /* CONFIG_FILS */
