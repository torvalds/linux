/*
 * WPA Supplicant - RSN PMKSA cache
 * Copyright (c) 2004-2008, Jouni Malinen <j@w1.fi>
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

#include "includes.h"

#include "common.h"
#include "eloop.h"
#include "eapol_supp/eapol_supp_sm.h"
#include "wpa.h"
#include "wpa_i.h"
#include "pmksa_cache.h"

#if defined(IEEE8021X_EAPOL) && !defined(CONFIG_NO_WPA2)

static const int pmksa_cache_max_entries = 32;

struct rsn_pmksa_cache {
	struct rsn_pmksa_cache_entry *pmksa; /* PMKSA cache */
	int pmksa_count; /* number of entries in PMKSA cache */
	struct wpa_sm *sm; /* TODO: get rid of this reference(?) */

	void (*free_cb)(struct rsn_pmksa_cache_entry *entry, void *ctx,
			int replace);
	void *ctx;
};


static void pmksa_cache_set_expiration(struct rsn_pmksa_cache *pmksa);


static void _pmksa_cache_free_entry(struct rsn_pmksa_cache_entry *entry)
{
	os_free(entry);
}


static void pmksa_cache_free_entry(struct rsn_pmksa_cache *pmksa,
				   struct rsn_pmksa_cache_entry *entry,
				   int replace)
{
	pmksa->pmksa_count--;
	pmksa->free_cb(entry, pmksa->ctx, replace);
	_pmksa_cache_free_entry(entry);
}


static void pmksa_cache_expire(void *eloop_ctx, void *timeout_ctx)
{
	struct rsn_pmksa_cache *pmksa = eloop_ctx;
	struct os_time now;

	os_get_time(&now);
	while (pmksa->pmksa && pmksa->pmksa->expiration <= now.sec) {
		struct rsn_pmksa_cache_entry *entry = pmksa->pmksa;
		pmksa->pmksa = entry->next;
		wpa_printf(MSG_DEBUG, "RSN: expired PMKSA cache entry for "
			   MACSTR, MAC2STR(entry->aa));
		pmksa_cache_free_entry(pmksa, entry, 0);
	}

	pmksa_cache_set_expiration(pmksa);
}


static void pmksa_cache_reauth(void *eloop_ctx, void *timeout_ctx)
{
	struct rsn_pmksa_cache *pmksa = eloop_ctx;
	pmksa->sm->cur_pmksa = NULL;
	eapol_sm_request_reauth(pmksa->sm->eapol);
}


static void pmksa_cache_set_expiration(struct rsn_pmksa_cache *pmksa)
{
	int sec;
	struct rsn_pmksa_cache_entry *entry;
	struct os_time now;

	eloop_cancel_timeout(pmksa_cache_expire, pmksa, NULL);
	eloop_cancel_timeout(pmksa_cache_reauth, pmksa, NULL);
	if (pmksa->pmksa == NULL)
		return;
	os_get_time(&now);
	sec = pmksa->pmksa->expiration - now.sec;
	if (sec < 0)
		sec = 0;
	eloop_register_timeout(sec + 1, 0, pmksa_cache_expire, pmksa, NULL);

	entry = pmksa->sm->cur_pmksa ? pmksa->sm->cur_pmksa :
		pmksa_cache_get(pmksa, pmksa->sm->bssid, NULL);
	if (entry) {
		sec = pmksa->pmksa->reauth_time - now.sec;
		if (sec < 0)
			sec = 0;
		eloop_register_timeout(sec, 0, pmksa_cache_reauth, pmksa,
				       NULL);
	}
}


/**
 * pmksa_cache_add - Add a PMKSA cache entry
 * @pmksa: Pointer to PMKSA cache data from pmksa_cache_init()
 * @pmk: The new pairwise master key
 * @pmk_len: PMK length in bytes, usually PMK_LEN (32)
 * @aa: Authenticator address
 * @spa: Supplicant address
 * @network_ctx: Network configuration context for this PMK
 * @akmp: WPA_KEY_MGMT_* used in key derivation
 * Returns: Pointer to the added PMKSA cache entry or %NULL on error
 *
 * This function create a PMKSA entry for a new PMK and adds it to the PMKSA
 * cache. If an old entry is already in the cache for the same Authenticator,
 * this entry will be replaced with the new entry. PMKID will be calculated
 * based on the PMK and the driver interface is notified of the new PMKID.
 */
struct rsn_pmksa_cache_entry *
pmksa_cache_add(struct rsn_pmksa_cache *pmksa, const u8 *pmk, size_t pmk_len,
		const u8 *aa, const u8 *spa, void *network_ctx, int akmp)
{
	struct rsn_pmksa_cache_entry *entry, *pos, *prev;
	struct os_time now;

	if (pmk_len > PMK_LEN)
		return NULL;

	entry = os_zalloc(sizeof(*entry));
	if (entry == NULL)
		return NULL;
	os_memcpy(entry->pmk, pmk, pmk_len);
	entry->pmk_len = pmk_len;
	rsn_pmkid(pmk, pmk_len, aa, spa, entry->pmkid,
		  wpa_key_mgmt_sha256(akmp));
	os_get_time(&now);
	entry->expiration = now.sec + pmksa->sm->dot11RSNAConfigPMKLifetime;
	entry->reauth_time = now.sec + pmksa->sm->dot11RSNAConfigPMKLifetime *
		pmksa->sm->dot11RSNAConfigPMKReauthThreshold / 100;
	entry->akmp = akmp;
	os_memcpy(entry->aa, aa, ETH_ALEN);
	entry->network_ctx = network_ctx;

	/* Replace an old entry for the same Authenticator (if found) with the
	 * new entry */
	pos = pmksa->pmksa;
	prev = NULL;
	while (pos) {
		if (os_memcmp(aa, pos->aa, ETH_ALEN) == 0) {
			if (pos->pmk_len == pmk_len &&
			    os_memcmp(pos->pmk, pmk, pmk_len) == 0 &&
			    os_memcmp(pos->pmkid, entry->pmkid, PMKID_LEN) ==
			    0) {
				wpa_printf(MSG_DEBUG, "WPA: reusing previous "
					   "PMKSA entry");
				os_free(entry);
				return pos;
			}
			if (prev == NULL)
				pmksa->pmksa = pos->next;
			else
				prev->next = pos->next;
			if (pos == pmksa->sm->cur_pmksa) {
				/* We are about to replace the current PMKSA
				 * cache entry. This happens when the PMKSA
				 * caching attempt fails, so we don't want to
				 * force pmksa_cache_free_entry() to disconnect
				 * at this point. Let's just make sure the old
				 * PMKSA cache entry will not be used in the
				 * future.
				 */
				wpa_printf(MSG_DEBUG, "RSN: replacing current "
					   "PMKSA entry");
				pmksa->sm->cur_pmksa = NULL;
			}
			wpa_printf(MSG_DEBUG, "RSN: Replace PMKSA entry for "
				   "the current AP");
			pmksa_cache_free_entry(pmksa, pos, 1);
			break;
		}
		prev = pos;
		pos = pos->next;
	}

	if (pmksa->pmksa_count >= pmksa_cache_max_entries && pmksa->pmksa) {
		/* Remove the oldest entry to make room for the new entry */
		pos = pmksa->pmksa;
		pmksa->pmksa = pos->next;
		wpa_printf(MSG_DEBUG, "RSN: removed the oldest PMKSA cache "
			   "entry (for " MACSTR ") to make room for new one",
			   MAC2STR(pos->aa));
		wpa_sm_remove_pmkid(pmksa->sm, pos->aa, pos->pmkid);
		pmksa_cache_free_entry(pmksa, pos, 0);
	}

	/* Add the new entry; order by expiration time */
	pos = pmksa->pmksa;
	prev = NULL;
	while (pos) {
		if (pos->expiration > entry->expiration)
			break;
		prev = pos;
		pos = pos->next;
	}
	if (prev == NULL) {
		entry->next = pmksa->pmksa;
		pmksa->pmksa = entry;
		pmksa_cache_set_expiration(pmksa);
	} else {
		entry->next = prev->next;
		prev->next = entry;
	}
	pmksa->pmksa_count++;
	wpa_printf(MSG_DEBUG, "RSN: added PMKSA cache entry for " MACSTR,
		   MAC2STR(entry->aa));
	wpa_sm_add_pmkid(pmksa->sm, entry->aa, entry->pmkid);

	return entry;
}


/**
 * pmksa_cache_deinit - Free all entries in PMKSA cache
 * @pmksa: Pointer to PMKSA cache data from pmksa_cache_init()
 */
void pmksa_cache_deinit(struct rsn_pmksa_cache *pmksa)
{
	struct rsn_pmksa_cache_entry *entry, *prev;

	if (pmksa == NULL)
		return;

	entry = pmksa->pmksa;
	pmksa->pmksa = NULL;
	while (entry) {
		prev = entry;
		entry = entry->next;
		os_free(prev);
	}
	pmksa_cache_set_expiration(pmksa);
	os_free(pmksa);
}


/**
 * pmksa_cache_get - Fetch a PMKSA cache entry
 * @pmksa: Pointer to PMKSA cache data from pmksa_cache_init()
 * @aa: Authenticator address or %NULL to match any
 * @pmkid: PMKID or %NULL to match any
 * Returns: Pointer to PMKSA cache entry or %NULL if no match was found
 */
struct rsn_pmksa_cache_entry * pmksa_cache_get(struct rsn_pmksa_cache *pmksa,
					       const u8 *aa, const u8 *pmkid)
{
	struct rsn_pmksa_cache_entry *entry = pmksa->pmksa;
	while (entry) {
		if ((aa == NULL || os_memcmp(entry->aa, aa, ETH_ALEN) == 0) &&
		    (pmkid == NULL ||
		     os_memcmp(entry->pmkid, pmkid, PMKID_LEN) == 0))
			return entry;
		entry = entry->next;
	}
	return NULL;
}


/**
 * pmksa_cache_notify_reconfig - Reconfiguration notification for PMKSA cache
 * @pmksa: Pointer to PMKSA cache data from pmksa_cache_init()
 *
 * Clear references to old data structures when wpa_supplicant is reconfigured.
 */
void pmksa_cache_notify_reconfig(struct rsn_pmksa_cache *pmksa)
{
	struct rsn_pmksa_cache_entry *entry = pmksa->pmksa;
	while (entry) {
		entry->network_ctx = NULL;
		entry = entry->next;
	}
}


static struct rsn_pmksa_cache_entry *
pmksa_cache_clone_entry(struct rsn_pmksa_cache *pmksa,
			const struct rsn_pmksa_cache_entry *old_entry,
			const u8 *aa)
{
	struct rsn_pmksa_cache_entry *new_entry;

	new_entry = pmksa_cache_add(pmksa, old_entry->pmk, old_entry->pmk_len,
				    aa, pmksa->sm->own_addr,
				    old_entry->network_ctx, old_entry->akmp);
	if (new_entry == NULL)
		return NULL;

	/* TODO: reorder entries based on expiration time? */
	new_entry->expiration = old_entry->expiration;
	new_entry->opportunistic = 1;

	return new_entry;
}


/**
 * pmksa_cache_get_opportunistic - Try to get an opportunistic PMKSA entry
 * @pmksa: Pointer to PMKSA cache data from pmksa_cache_init()
 * @network_ctx: Network configuration context
 * @aa: Authenticator address for the new AP
 * Returns: Pointer to a new PMKSA cache entry or %NULL if not available
 *
 * Try to create a new PMKSA cache entry opportunistically by guessing that the
 * new AP is sharing the same PMK as another AP that has the same SSID and has
 * already an entry in PMKSA cache.
 */
struct rsn_pmksa_cache_entry *
pmksa_cache_get_opportunistic(struct rsn_pmksa_cache *pmksa, void *network_ctx,
			      const u8 *aa)
{
	struct rsn_pmksa_cache_entry *entry = pmksa->pmksa;

	if (network_ctx == NULL)
		return NULL;
	while (entry) {
		if (entry->network_ctx == network_ctx) {
			entry = pmksa_cache_clone_entry(pmksa, entry, aa);
			if (entry) {
				wpa_printf(MSG_DEBUG, "RSN: added "
					   "opportunistic PMKSA cache entry "
					   "for " MACSTR, MAC2STR(aa));
			}
			return entry;
		}
		entry = entry->next;
	}
	return NULL;
}


/**
 * pmksa_cache_get_current - Get the current used PMKSA entry
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * Returns: Pointer to the current PMKSA cache entry or %NULL if not available
 */
struct rsn_pmksa_cache_entry * pmksa_cache_get_current(struct wpa_sm *sm)
{
	if (sm == NULL)
		return NULL;
	return sm->cur_pmksa;
}


/**
 * pmksa_cache_clear_current - Clear the current PMKSA entry selection
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 */
void pmksa_cache_clear_current(struct wpa_sm *sm)
{
	if (sm == NULL)
		return;
	sm->cur_pmksa = NULL;
}


/**
 * pmksa_cache_set_current - Set the current PMKSA entry selection
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @pmkid: PMKID for selecting PMKSA or %NULL if not used
 * @bssid: BSSID for PMKSA or %NULL if not used
 * @network_ctx: Network configuration context
 * @try_opportunistic: Whether to allow opportunistic PMKSA caching
 * Returns: 0 if PMKSA was found or -1 if no matching entry was found
 */
int pmksa_cache_set_current(struct wpa_sm *sm, const u8 *pmkid,
			    const u8 *bssid, void *network_ctx,
			    int try_opportunistic)
{
	struct rsn_pmksa_cache *pmksa = sm->pmksa;
	sm->cur_pmksa = NULL;
	if (pmkid)
		sm->cur_pmksa = pmksa_cache_get(pmksa, NULL, pmkid);
	if (sm->cur_pmksa == NULL && bssid)
		sm->cur_pmksa = pmksa_cache_get(pmksa, bssid, NULL);
	if (sm->cur_pmksa == NULL && try_opportunistic && bssid)
		sm->cur_pmksa = pmksa_cache_get_opportunistic(pmksa,
							      network_ctx,
							      bssid);
	if (sm->cur_pmksa) {
		wpa_hexdump(MSG_DEBUG, "RSN: PMKID",
			    sm->cur_pmksa->pmkid, PMKID_LEN);
		return 0;
	}
	return -1;
}


/**
 * pmksa_cache_list - Dump text list of entries in PMKSA cache
 * @pmksa: Pointer to PMKSA cache data from pmksa_cache_init()
 * @buf: Buffer for the list
 * @len: Length of the buffer
 * Returns: number of bytes written to buffer
 *
 * This function is used to generate a text format representation of the
 * current PMKSA cache contents for the ctrl_iface PMKSA command.
 */
int pmksa_cache_list(struct rsn_pmksa_cache *pmksa, char *buf, size_t len)
{
	int i, ret;
	char *pos = buf;
	struct rsn_pmksa_cache_entry *entry;
	struct os_time now;

	os_get_time(&now);
	ret = os_snprintf(pos, buf + len - pos,
			  "Index / AA / PMKID / expiration (in seconds) / "
			  "opportunistic\n");
	if (ret < 0 || ret >= buf + len - pos)
		return pos - buf;
	pos += ret;
	i = 0;
	entry = pmksa->pmksa;
	while (entry) {
		i++;
		ret = os_snprintf(pos, buf + len - pos, "%d " MACSTR " ",
				  i, MAC2STR(entry->aa));
		if (ret < 0 || ret >= buf + len - pos)
			return pos - buf;
		pos += ret;
		pos += wpa_snprintf_hex(pos, buf + len - pos, entry->pmkid,
					PMKID_LEN);
		ret = os_snprintf(pos, buf + len - pos, " %d %d\n",
				  (int) (entry->expiration - now.sec),
				  entry->opportunistic);
		if (ret < 0 || ret >= buf + len - pos)
			return pos - buf;
		pos += ret;
		entry = entry->next;
	}
	return pos - buf;
}


/**
 * pmksa_cache_init - Initialize PMKSA cache
 * @free_cb: Callback function to be called when a PMKSA cache entry is freed
 * @ctx: Context pointer for free_cb function
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * Returns: Pointer to PMKSA cache data or %NULL on failure
 */
struct rsn_pmksa_cache *
pmksa_cache_init(void (*free_cb)(struct rsn_pmksa_cache_entry *entry,
				 void *ctx, int replace),
		 void *ctx, struct wpa_sm *sm)
{
	struct rsn_pmksa_cache *pmksa;

	pmksa = os_zalloc(sizeof(*pmksa));
	if (pmksa) {
		pmksa->free_cb = free_cb;
		pmksa->ctx = ctx;
		pmksa->sm = sm;
	}

	return pmksa;
}

#endif /* IEEE8021X_EAPOL and !CONFIG_NO_WPA2 */
