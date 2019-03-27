/*
 * hostapd - PMKSA cache for IEEE 802.11i RSN
 * Copyright (c) 2004-2008, 2012-2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "eapol_auth/eapol_auth_sm.h"
#include "eapol_auth/eapol_auth_sm_i.h"
#include "radius/radius_das.h"
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
	os_free(entry->vlan_desc);
	os_free(entry->identity);
	wpabuf_free(entry->cui);
#ifndef CONFIG_NO_RADIUS
	radius_free_class(&entry->radius_class);
#endif /* CONFIG_NO_RADIUS */
	bin_clear_free(entry, sizeof(*entry));
}


void pmksa_cache_free_entry(struct rsn_pmksa_cache *pmksa,
			    struct rsn_pmksa_cache_entry *entry)
{
	struct rsn_pmksa_cache_entry *pos, *prev;
	unsigned int hash;

	pmksa->pmksa_count--;
	pmksa->free_cb(entry, pmksa->ctx);

	/* unlink from hash list */
	hash = PMKID_HASH(entry->pmkid);
	pos = pmksa->pmkid[hash];
	prev = NULL;
	while (pos) {
		if (pos == entry) {
			if (prev != NULL)
				prev->hnext = entry->hnext;
			else
				pmksa->pmkid[hash] = entry->hnext;
			break;
		}
		prev = pos;
		pos = pos->hnext;
	}

	/* unlink from entry list */
	pos = pmksa->pmksa;
	prev = NULL;
	while (pos) {
		if (pos == entry) {
			if (prev != NULL)
				prev->next = entry->next;
			else
				pmksa->pmksa = entry->next;
			break;
		}
		prev = pos;
		pos = pos->next;
	}

	_pmksa_cache_free_entry(entry);
}


/**
 * pmksa_cache_auth_flush - Flush all PMKSA cache entries
 * @pmksa: Pointer to PMKSA cache data from pmksa_cache_auth_init()
 */
void pmksa_cache_auth_flush(struct rsn_pmksa_cache *pmksa)
{
	while (pmksa->pmksa) {
		wpa_printf(MSG_DEBUG, "RSN: Flush PMKSA cache entry for "
			   MACSTR, MAC2STR(pmksa->pmksa->spa));
		pmksa_cache_free_entry(pmksa, pmksa->pmksa);
	}
}


static void pmksa_cache_expire(void *eloop_ctx, void *timeout_ctx)
{
	struct rsn_pmksa_cache *pmksa = eloop_ctx;
	struct os_reltime now;

	os_get_reltime(&now);
	while (pmksa->pmksa && pmksa->pmksa->expiration <= now.sec) {
		wpa_printf(MSG_DEBUG, "RSN: expired PMKSA cache entry for "
			   MACSTR, MAC2STR(pmksa->pmksa->spa));
		pmksa_cache_free_entry(pmksa, pmksa->pmksa);
	}

	pmksa_cache_set_expiration(pmksa);
}


static void pmksa_cache_set_expiration(struct rsn_pmksa_cache *pmksa)
{
	int sec;
	struct os_reltime now;

	eloop_cancel_timeout(pmksa_cache_expire, pmksa, NULL);
	if (pmksa->pmksa == NULL)
		return;
	os_get_reltime(&now);
	sec = pmksa->pmksa->expiration - now.sec;
	if (sec < 0)
		sec = 0;
	eloop_register_timeout(sec + 1, 0, pmksa_cache_expire, pmksa, NULL);
}


static void pmksa_cache_from_eapol_data(struct rsn_pmksa_cache_entry *entry,
					struct eapol_state_machine *eapol)
{
	struct vlan_description *vlan_desc;

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

	if (eapol->radius_cui)
		entry->cui = wpabuf_dup(eapol->radius_cui);

#ifndef CONFIG_NO_RADIUS
	radius_copy_class(&entry->radius_class, &eapol->radius_class);
#endif /* CONFIG_NO_RADIUS */

	entry->eap_type_authsrv = eapol->eap_type_authsrv;

	vlan_desc = ((struct sta_info *) eapol->sta)->vlan_desc;
	if (vlan_desc && vlan_desc->notempty) {
		entry->vlan_desc = os_zalloc(sizeof(struct vlan_description));
		if (entry->vlan_desc)
			*entry->vlan_desc = *vlan_desc;
	} else {
		entry->vlan_desc = NULL;
	}

	entry->acct_multi_session_id = eapol->acct_multi_session_id;
}


void pmksa_cache_to_eapol_data(struct hostapd_data *hapd,
			       struct rsn_pmksa_cache_entry *entry,
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

	if (entry->cui) {
		wpabuf_free(eapol->radius_cui);
		eapol->radius_cui = wpabuf_dup(entry->cui);
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
#ifndef CONFIG_NO_VLAN
	ap_sta_set_vlan(hapd, eapol->sta, entry->vlan_desc);
#endif /* CONFIG_NO_VLAN */

	eapol->acct_multi_session_id = entry->acct_multi_session_id;
}


static void pmksa_cache_link_entry(struct rsn_pmksa_cache *pmksa,
				   struct rsn_pmksa_cache_entry *entry)
{
	struct rsn_pmksa_cache_entry *pos, *prev;
	int hash;

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

	hash = PMKID_HASH(entry->pmkid);
	entry->hnext = pmksa->pmkid[hash];
	pmksa->pmkid[hash] = entry;

	pmksa->pmksa_count++;
	if (prev == NULL)
		pmksa_cache_set_expiration(pmksa);
	wpa_printf(MSG_DEBUG, "RSN: added PMKSA cache entry for " MACSTR,
		   MAC2STR(entry->spa));
	wpa_hexdump(MSG_DEBUG, "RSN: added PMKID", entry->pmkid, PMKID_LEN);
}


/**
 * pmksa_cache_auth_add - Add a PMKSA cache entry
 * @pmksa: Pointer to PMKSA cache data from pmksa_cache_auth_init()
 * @pmk: The new pairwise master key
 * @pmk_len: PMK length in bytes, usually PMK_LEN (32)
 * @pmkid: Calculated PMKID
 * @kck: Key confirmation key or %NULL if not yet derived
 * @kck_len: KCK length in bytes
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
		     const u8 *pmk, size_t pmk_len, const u8 *pmkid,
		     const u8 *kck, size_t kck_len,
		     const u8 *aa, const u8 *spa, int session_timeout,
		     struct eapol_state_machine *eapol, int akmp)
{
	struct rsn_pmksa_cache_entry *entry;

	entry = pmksa_cache_auth_create_entry(pmk, pmk_len, pmkid, kck, kck_len,
					      aa, spa, session_timeout, eapol,
					      akmp);

	if (pmksa_cache_auth_add_entry(pmksa, entry) < 0)
		return NULL;

	return entry;
}


/**
 * pmksa_cache_auth_create_entry - Create a PMKSA cache entry
 * @pmk: The new pairwise master key
 * @pmk_len: PMK length in bytes, usually PMK_LEN (32)
 * @pmkid: Calculated PMKID
 * @kck: Key confirmation key or %NULL if not yet derived
 * @kck_len: KCK length in bytes
 * @aa: Authenticator address
 * @spa: Supplicant address
 * @session_timeout: Session timeout
 * @eapol: Pointer to EAPOL state machine data
 * @akmp: WPA_KEY_MGMT_* used in key derivation
 * Returns: Pointer to the added PMKSA cache entry or %NULL on error
 *
 * This function creates a PMKSA entry.
 */
struct rsn_pmksa_cache_entry *
pmksa_cache_auth_create_entry(const u8 *pmk, size_t pmk_len, const u8 *pmkid,
			      const u8 *kck, size_t kck_len, const u8 *aa,
			      const u8 *spa, int session_timeout,
			      struct eapol_state_machine *eapol, int akmp)
{
	struct rsn_pmksa_cache_entry *entry;
	struct os_reltime now;

	if (pmk_len > PMK_LEN_MAX)
		return NULL;

	if (wpa_key_mgmt_suite_b(akmp) && !kck)
		return NULL;

	entry = os_zalloc(sizeof(*entry));
	if (entry == NULL)
		return NULL;
	os_memcpy(entry->pmk, pmk, pmk_len);
	entry->pmk_len = pmk_len;
	if (pmkid)
		os_memcpy(entry->pmkid, pmkid, PMKID_LEN);
	else if (akmp == WPA_KEY_MGMT_IEEE8021X_SUITE_B_192)
		rsn_pmkid_suite_b_192(kck, kck_len, aa, spa, entry->pmkid);
	else if (wpa_key_mgmt_suite_b(akmp))
		rsn_pmkid_suite_b(kck, kck_len, aa, spa, entry->pmkid);
	else
		rsn_pmkid(pmk, pmk_len, aa, spa, entry->pmkid, akmp);
	os_get_reltime(&now);
	entry->expiration = now.sec;
	if (session_timeout > 0)
		entry->expiration += session_timeout;
	else
		entry->expiration += dot11RSNAConfigPMKLifetime;
	entry->akmp = akmp;
	os_memcpy(entry->spa, spa, ETH_ALEN);
	pmksa_cache_from_eapol_data(entry, eapol);

	return entry;
}


/**
 * pmksa_cache_auth_add_entry - Add a PMKSA cache entry
 * @pmksa: Pointer to PMKSA cache data from pmksa_cache_auth_init()
 * @entry: Pointer to PMKSA cache entry
 *
 * This function adds PMKSA cache entry to the PMKSA cache. If an old entry is
 * already in the cache for the same Supplicant, this entry will be replaced
 * with the new entry. PMKID will be calculated based on the PMK.
 */
int pmksa_cache_auth_add_entry(struct rsn_pmksa_cache *pmksa,
			       struct rsn_pmksa_cache_entry *entry)
{
	struct rsn_pmksa_cache_entry *pos;

	if (entry == NULL)
		return -1;

	/* Replace an old entry for the same STA (if found) with the new entry
	 */
	pos = pmksa_cache_auth_get(pmksa, entry->spa, NULL);
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

	return 0;
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
	if (old_entry->cui)
		entry->cui = wpabuf_dup(old_entry->cui);
#ifndef CONFIG_NO_RADIUS
	radius_copy_class(&entry->radius_class, &old_entry->radius_class);
#endif /* CONFIG_NO_RADIUS */
	entry->eap_type_authsrv = old_entry->eap_type_authsrv;
	if (old_entry->vlan_desc) {
		entry->vlan_desc = os_zalloc(sizeof(struct vlan_description));
		if (entry->vlan_desc)
			*entry->vlan_desc = *old_entry->vlan_desc;
	} else {
		entry->vlan_desc = NULL;
	}
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
	pmksa->pmksa_count = 0;
	pmksa->pmksa = NULL;
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

	if (pmkid) {
		for (entry = pmksa->pmkid[PMKID_HASH(pmkid)]; entry;
		     entry = entry->hnext) {
			if ((spa == NULL ||
			     os_memcmp(entry->spa, spa, ETH_ALEN) == 0) &&
			    os_memcmp(entry->pmkid, pmkid, PMKID_LEN) == 0)
				return entry;
		}
	} else {
		for (entry = pmksa->pmksa; entry; entry = entry->next) {
			if (spa == NULL ||
			    os_memcmp(entry->spa, spa, ETH_ALEN) == 0)
				return entry;
		}
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

	for (entry = pmksa->pmksa; entry; entry = entry->next) {
		if (os_memcmp(entry->spa, spa, ETH_ALEN) != 0)
			continue;
		rsn_pmkid(entry->pmk, entry->pmk_len, aa, spa, new_pmkid,
			  entry->akmp);
		if (os_memcmp(new_pmkid, pmkid, PMKID_LEN) == 0)
			return entry;
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


static int das_attr_match(struct rsn_pmksa_cache_entry *entry,
			  struct radius_das_attrs *attr)
{
	int match = 0;

	if (attr->sta_addr) {
		if (os_memcmp(attr->sta_addr, entry->spa, ETH_ALEN) != 0)
			return 0;
		match++;
	}

	if (attr->acct_multi_session_id) {
		char buf[20];

		if (attr->acct_multi_session_id_len != 16)
			return 0;
		os_snprintf(buf, sizeof(buf), "%016llX",
			    (unsigned long long) entry->acct_multi_session_id);
		if (os_memcmp(attr->acct_multi_session_id, buf, 16) != 0)
			return 0;
		match++;
	}

	if (attr->cui) {
		if (!entry->cui ||
		    attr->cui_len != wpabuf_len(entry->cui) ||
		    os_memcmp(attr->cui, wpabuf_head(entry->cui),
			      attr->cui_len) != 0)
			return 0;
		match++;
	}

	if (attr->user_name) {
		if (!entry->identity ||
		    attr->user_name_len != entry->identity_len ||
		    os_memcmp(attr->user_name, entry->identity,
			      attr->user_name_len) != 0)
			return 0;
		match++;
	}

	return match;
}


int pmksa_cache_auth_radius_das_disconnect(struct rsn_pmksa_cache *pmksa,
					   struct radius_das_attrs *attr)
{
	int found = 0;
	struct rsn_pmksa_cache_entry *entry, *prev;

	if (attr->acct_session_id)
		return -1;

	entry = pmksa->pmksa;
	while (entry) {
		if (das_attr_match(entry, attr)) {
			found++;
			prev = entry;
			entry = entry->next;
			pmksa_cache_free_entry(pmksa, prev);
			continue;
		}
		entry = entry->next;
	}

	return found ? 0 : -1;
}


/**
 * pmksa_cache_auth_list - Dump text list of entries in PMKSA cache
 * @pmksa: Pointer to PMKSA cache data from pmksa_cache_auth_init()
 * @buf: Buffer for the list
 * @len: Length of the buffer
 * Returns: Number of bytes written to buffer
 *
 * This function is used to generate a text format representation of the
 * current PMKSA cache contents for the ctrl_iface PMKSA command.
 */
int pmksa_cache_auth_list(struct rsn_pmksa_cache *pmksa, char *buf, size_t len)
{
	int i, ret;
	char *pos = buf;
	struct rsn_pmksa_cache_entry *entry;
	struct os_reltime now;

	os_get_reltime(&now);
	ret = os_snprintf(pos, buf + len - pos,
			  "Index / SPA / PMKID / expiration (in seconds) / opportunistic\n");
	if (os_snprintf_error(buf + len - pos, ret))
		return pos - buf;
	pos += ret;
	i = 0;
	entry = pmksa->pmksa;
	while (entry) {
		ret = os_snprintf(pos, buf + len - pos, "%d " MACSTR " ",
				  i, MAC2STR(entry->spa));
		if (os_snprintf_error(buf + len - pos, ret))
			return pos - buf;
		pos += ret;
		pos += wpa_snprintf_hex(pos, buf + len - pos, entry->pmkid,
					PMKID_LEN);
		ret = os_snprintf(pos, buf + len - pos, " %d %d\n",
				  (int) (entry->expiration - now.sec),
				  entry->opportunistic);
		if (os_snprintf_error(buf + len - pos, ret))
			return pos - buf;
		pos += ret;
		entry = entry->next;
	}
	return pos - buf;
}


#ifdef CONFIG_PMKSA_CACHE_EXTERNAL
#ifdef CONFIG_MESH

/**
 * pmksa_cache_auth_list_mesh - Dump text list of entries in PMKSA cache
 * @pmksa: Pointer to PMKSA cache data from pmksa_cache_auth_init()
 * @addr: MAC address of the peer (NULL means any)
 * @buf: Buffer for the list
 * @len: Length of the buffer
 * Returns: Number of bytes written to buffer
 *
 * This function is used to generate a text format representation of the
 * current PMKSA cache contents for the ctrl_iface PMKSA_GET command to store
 * in external storage.
 */
int pmksa_cache_auth_list_mesh(struct rsn_pmksa_cache *pmksa, const u8 *addr,
			       char *buf, size_t len)
{
	int ret;
	char *pos, *end;
	struct rsn_pmksa_cache_entry *entry;
	struct os_reltime now;

	pos = buf;
	end = buf + len;
	os_get_reltime(&now);


	/*
	 * Entry format:
	 * <BSSID> <PMKID> <PMK> <expiration in seconds>
	 */
	for (entry = pmksa->pmksa; entry; entry = entry->next) {
		if (addr && os_memcmp(entry->spa, addr, ETH_ALEN) != 0)
			continue;

		ret = os_snprintf(pos, end - pos, MACSTR " ",
				  MAC2STR(entry->spa));
		if (os_snprintf_error(end - pos, ret))
			return 0;
		pos += ret;

		pos += wpa_snprintf_hex(pos, end - pos, entry->pmkid,
					PMKID_LEN);

		ret = os_snprintf(pos, end - pos, " ");
		if (os_snprintf_error(end - pos, ret))
			return 0;
		pos += ret;

		pos += wpa_snprintf_hex(pos, end - pos, entry->pmk,
					entry->pmk_len);

		ret = os_snprintf(pos, end - pos, " %d\n",
				  (int) (entry->expiration - now.sec));
		if (os_snprintf_error(end - pos, ret))
			return 0;
		pos += ret;
	}

	return pos - buf;
}

#endif /* CONFIG_MESH */
#endif /* CONFIG_PMKSA_CACHE_EXTERNAL */
