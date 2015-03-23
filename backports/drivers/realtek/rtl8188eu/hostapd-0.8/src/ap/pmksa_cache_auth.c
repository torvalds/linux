/*
 * hostapd - PMKSA cache for IEEE 802.11i RSN
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

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "eapol_auth/eapol_auth_sm.h"
#include "eapol_auth/eapol_auth_sm_i.h"
#include "sta_info.h"
#include "ap_config.h"
#include "pmksa_cache_auth.h"


static const int pmksa_cache_max_entries = 1024;
static const int dot11RSNAConfigPMKLifetime = 43200;

struct rsn_pmksa_cache {
#define PMKID_HASH_SIZE 128
#define PMKID_HASH(pmkid) (unsigned int) ((pmkid)[0] & 0x7f)
	struct rsn_pmksa_cache_entry *pmkid[PMKID_HASH_SIZE];
	struct rsn_pmksa_cache_entry *pmksa;
	int pmksa_count;

	void (*free_cb)(struct rsn_pmksa_cache_entry *entry, void *ctx);
	void *ctx;
};


static void pmksa_cache_set_expiration(struct rsn_pmksa_cache *pmksa);


static void _pmksa_cache_free_entry(struct rsn_pmksa_cache_entry *entry)
{
	if (entry == NULL)
		return;
	os_free(entry->identity);
#ifndef CONFIG_NO_RADIUS
	radius_free_class(&entry->radius_class);
#endif /* CONFIG_NO_RADIUS */
	os_free(entry);
}


static void pmksa_cache_free_entry(struct rsn_pmksa_cache *pmksa,
				   struct rsn_pmksa_cache_entry *entry)
{
	struct rsn_pmksa_cache_entry *pos, *prev;

	pmksa->pmksa_count--;
	pmksa->free_cb(entry, pmksa->ctx);
	pos = pmksa->pmkid[PMKID_HASH(entry->pmkid)];
	prev = NULL;
	while (pos) {
		if (pos == entry) {
			if (prev != NULL) {
				prev->hnext = pos->hnext;
			} else {
				pmksa->pmkid[PMKID_HASH(entry->pmkid)] =
					pos->hnext;
			}
			break;
		}
		prev = pos;
		pos = pos->hnext;
	}

	pos = pmksa->pmksa;
	prev = NULL;
	while (pos) {
		if (pos == entry) {
			if (prev != NULL)
				prev->next = pos->next;
			else
				pmksa->pmksa = pos->next;
			break;
		}
		prev = pos;
		pos = pos->next;
	}
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
			   MACSTR, MAC2STR(entry->spa));
		pmksa_cache_free_entry(pmksa, entry);
	}

	pmksa_cache_set_expiration(pmksa);
}


static void pmksa_cache_set_expiration(struct rsn_pmksa_cache *pmksa)
{
	int sec;
	struct os_time now;

	eloop_cancel_timeout(pmksa_cache_expire, pmksa, NULL);
	if (pmksa->pmksa == NULL)
		return;
	os_get_time(&now);
	sec = pmksa->pmksa->expiration - now.sec;
	if (sec < 0)
		sec = 0;
	eloop_register_timeout(sec + 1, 0, pmksa_cache_expire, pmksa, NULL);
}


static void pmksa_cache_from_eapol_data(struct rsn_pmksa_cache_entry *entry,
					struct eapol_state_machine *eapol)
{
	if (eapol == NULL)
		return;

	if (eapol->identity) {
		entry->identity = os_malloc(eapol->identity_len);
		if (entry->identity) {
			entry->identity_len = eapol->identity_len;
			os_memcpy(entry->identity, eapol->identity,
				  eapol->identity_len);
		}
	}

#ifndef CONFIG_NO_RADIUS
	radius_copy_class(&entry->radius_class, &eapol->radius_class);
#endif /* CONFIG_NO_RADIUS */

	entry->eap_type_authsrv = eapol->eap_type_authsrv;
	entry->vlan_id = ((struct sta_info *) eapol->sta)->vlan_id;
}


void pmksa_cache_to_eapol_data(struct rsn_pmksa_cache_entry *entry,
			       struct eapol_state_machine *eapol)
{
	if (entry == NULL || eapol == NULL)
		return;

	if (entry->identity) {
		os_free(eapol->identity);
		eapol->identity = os_malloc(entry->identity_len);
		if (eapol->identity) {
			eapol->identity_len = entry->identity_len;
			os_memcpy(eapol->identity, entry->identity,
				  entry->identity_len);
		}
		wpa_hexdump_ascii(MSG_DEBUG, "STA identity from PMKSA",
				  eapol->identity, eapol->identity_len);
	}

#ifndef CONFIG_NO_RADIUS
	radius_free_class(&eapol->radius_class);
	radius_copy_class(&eapol->radius_class, &entry->radius_class);
#endif /* CONFIG_NO_RADIUS */
	if (eapol->radius_class.attr) {
		wpa_printf(MSG_DEBUG, "Copied %lu Class attribute(s) from "
			   "PMKSA", (unsigned long) eapol->radius_class.count);
	}

	eapol->eap_type_authsrv = entry->eap_type_authsrv;
	((struct sta_info *) eapol->sta)->vlan_id = entry->vlan_id;
}


static void pmksa_cache_link_entry(struct rsn_pmksa_cache *pmksa,
				   struct rsn_pmksa_cache_entry *entry)
{
	struct rsn_pmksa_cache_entry *pos, *prev;

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
	} else {
		entry->next = prev->next;
		prev->next = entry;
	}
	entry->hnext = pmksa->pmkid[PMKID_HASH(entry->pmkid)];
	pmksa->pmkid[PMKID_HASH(entry->pmkid)] = entry;

	pmksa->pmksa_count++;
	wpa_printf(MSG_DEBUG, "RSN: added PMKSA cache entry for " MACSTR,
		   MAC2STR(entry->spa));
	wpa_hexdump(MSG_DEBUG, "RSN: added PMKID", entry->pmkid, PMKID_LEN);
}


/**
 * pmksa_cache_auth_add - Add a PMKSA cache entry
 * @pmksa: Pointer to PMKSA cache data from pmksa_cache_auth_init()
 * @pmk: The new pairwise master key
 * @pmk_len: PMK length in bytes, usually PMK_LEN (32)
 * @aa: Authenticator address
 * @spa: Supplicant address
 * @session_timeout: Session timeout
 * @eapol: Pointer to EAPOL state machine data
 * @akmp: WPA_KEY_MGMT_* used in key derivation
 * Returns: Pointer to the added PMKSA cache entry or %NULL on error
 *
 * This function create a PMKSA entry for a new PMK and adds it to the PMKSA
 * cache. If an old entry is already in the cache for the same Supplicant,
 * this entry will be replaced with the new entry. PMKID will be calculated
 * based on the PMK.
 */
struct rsn_pmksa_cache_entry *
pmksa_cache_auth_add(struct rsn_pmksa_cache *pmksa,
		     const u8 *pmk, size_t pmk_len,
		const u8 *aa, const u8 *spa, int session_timeout,
		struct eapol_state_machine *eapol, int akmp)
{
	struct rsn_pmksa_cache_entry *entry, *pos;
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
	entry->expiration = now.sec;
	if (session_timeout > 0)
		entry->expiration += session_timeout;
	else
		entry->expiration += dot11RSNAConfigPMKLifetime;
	entry->akmp = akmp;
	os_memcpy(entry->spa, spa, ETH_ALEN);
	pmksa_cache_from_eapol_data(entry, eapol);

	/* Replace an old entry for the same STA (if found) with the new entry
	 */
	pos = pmksa_cache_auth_get(pmksa, spa, NULL);
	if (pos)
		pmksa_cache_free_entry(pmksa, pos);

	if (pmksa->pmksa_count >= pmksa_cache_max_entries && pmksa->pmksa) {
		/* Remove the oldest entry to make room for the new entry */
		wpa_printf(MSG_DEBUG, "RSN: removed the oldest PMKSA cache "
			   "entry (for " MACSTR ") to make room for new one",
			   MAC2STR(pmksa->pmksa->spa));
		pmksa_cache_free_entry(pmksa, pmksa->pmksa);
	}

	pmksa_cache_link_entry(pmksa, entry);

	return entry;
}


struct rsn_pmksa_cache_entry *
pmksa_cache_add_okc(struct rsn_pmksa_cache *pmksa,
		    const struct rsn_pmksa_cache_entry *old_entry,
		    const u8 *aa, const u8 *pmkid)
{
	struct rsn_pmksa_cache_entry *entry;

	entry = os_zalloc(sizeof(*entry));
	if (entry == NULL)
		return NULL;
	os_memcpy(entry->pmkid, pmkid, PMKID_LEN);
	os_memcpy(entry->pmk, old_entry->pmk, old_entry->pmk_len);
	entry->pmk_len = old_entry->pmk_len;
	entry->expiration = old_entry->expiration;
	entry->akmp = old_entry->akmp;
	os_memcpy(entry->spa, old_entry->spa, ETH_ALEN);
	entry->opportunistic = 1;
	if (old_entry->identity) {
		entry->identity = os_malloc(old_entry->identity_len);
		if (entry->identity) {
			entry->identity_len = old_entry->identity_len;
			os_memcpy(entry->identity, old_entry->identity,
				  old_entry->identity_len);
		}
	}
#ifndef CONFIG_NO_RADIUS
	radius_copy_class(&entry->radius_class, &old_entry->radius_class);
#endif /* CONFIG_NO_RADIUS */
	entry->eap_type_authsrv = old_entry->eap_type_authsrv;
	entry->vlan_id = old_entry->vlan_id;
	entry->opportunistic = 1;

	pmksa_cache_link_entry(pmksa, entry);

	return entry;
}


/**
 * pmksa_cache_auth_deinit - Free all entries in PMKSA cache
 * @pmksa: Pointer to PMKSA cache data from pmksa_cache_auth_init()
 */
void pmksa_cache_auth_deinit(struct rsn_pmksa_cache *pmksa)
{
	struct rsn_pmksa_cache_entry *entry, *prev;
	int i;

	if (pmksa == NULL)
		return;

	entry = pmksa->pmksa;
	while (entry) {
		prev = entry;
		entry = entry->next;
		_pmksa_cache_free_entry(prev);
	}
	eloop_cancel_timeout(pmksa_cache_expire, pmksa, NULL);
	for (i = 0; i < PMKID_HASH_SIZE; i++)
		pmksa->pmkid[i] = NULL;
	os_free(pmksa);
}


/**
 * pmksa_cache_auth_get - Fetch a PMKSA cache entry
 * @pmksa: Pointer to PMKSA cache data from pmksa_cache_auth_init()
 * @spa: Supplicant address or %NULL to match any
 * @pmkid: PMKID or %NULL to match any
 * Returns: Pointer to PMKSA cache entry or %NULL if no match was found
 */
struct rsn_pmksa_cache_entry *
pmksa_cache_auth_get(struct rsn_pmksa_cache *pmksa,
		     const u8 *spa, const u8 *pmkid)
{
	struct rsn_pmksa_cache_entry *entry;

	if (pmkid)
		entry = pmksa->pmkid[PMKID_HASH(pmkid)];
	else
		entry = pmksa->pmksa;
	while (entry) {
		if ((spa == NULL ||
		     os_memcmp(entry->spa, spa, ETH_ALEN) == 0) &&
		    (pmkid == NULL ||
		     os_memcmp(entry->pmkid, pmkid, PMKID_LEN) == 0))
			return entry;
		entry = pmkid ? entry->hnext : entry->next;
	}
	return NULL;
}


/**
 * pmksa_cache_get_okc - Fetch a PMKSA cache entry using OKC
 * @pmksa: Pointer to PMKSA cache data from pmksa_cache_auth_init()
 * @aa: Authenticator address
 * @spa: Supplicant address
 * @pmkid: PMKID
 * Returns: Pointer to PMKSA cache entry or %NULL if no match was found
 *
 * Use opportunistic key caching (OKC) to find a PMK for a supplicant.
 */
struct rsn_pmksa_cache_entry * pmksa_cache_get_okc(
	struct rsn_pmksa_cache *pmksa, const u8 *aa, const u8 *spa,
	const u8 *pmkid)
{
	struct rsn_pmksa_cache_entry *entry;
	u8 new_pmkid[PMKID_LEN];

	entry = pmksa->pmksa;
	while (entry) {
		if (os_memcmp(entry->spa, spa, ETH_ALEN) != 0)
			continue;
		rsn_pmkid(entry->pmk, entry->pmk_len, aa, spa, new_pmkid,
			  wpa_key_mgmt_sha256(entry->akmp));
		if (os_memcmp(new_pmkid, pmkid, PMKID_LEN) == 0)
			return entry;
		entry = entry->next;
	}
	return NULL;
}


/**
 * pmksa_cache_auth_init - Initialize PMKSA cache
 * @free_cb: Callback function to be called when a PMKSA cache entry is freed
 * @ctx: Context pointer for free_cb function
 * Returns: Pointer to PMKSA cache data or %NULL on failure
 */
struct rsn_pmksa_cache *
pmksa_cache_auth_init(void (*free_cb)(struct rsn_pmksa_cache_entry *entry,
				      void *ctx), void *ctx)
{
	struct rsn_pmksa_cache *pmksa;

	pmksa = os_zalloc(sizeof(*pmksa));
	if (pmksa) {
		pmksa->free_cb = free_cb;
		pmksa->ctx = ctx;
	}

	return pmksa;
}
