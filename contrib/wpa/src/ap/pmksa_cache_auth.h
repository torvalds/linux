/*
 * hostapd - PMKSA cache for IEEE 802.11i RSN
 * Copyright (c) 2004-2008, 2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef PMKSA_CACHE_H
#define PMKSA_CACHE_H

#include "radius/radius.h"

/**
 * struct rsn_pmksa_cache_entry - PMKSA cache entry
 */
struct rsn_pmksa_cache_entry {
	struct rsn_pmksa_cache_entry *next, *hnext;
	u8 pmkid[PMKID_LEN];
	u8 pmk[PMK_LEN_MAX];
	size_t pmk_len;
	os_time_t expiration;
	int akmp; /* WPA_KEY_MGMT_* */
	u8 spa[ETH_ALEN];

	u8 *identity;
	size_t identity_len;
	struct wpabuf *cui;
	struct radius_class_data radius_class;
	u8 eap_type_authsrv;
	struct vlan_description *vlan_desc;
	int opportunistic;

	u64 acct_multi_session_id;
};

struct rsn_pmksa_cache;
struct radius_das_attrs;

struct rsn_pmksa_cache *
pmksa_cache_auth_init(void (*free_cb)(struct rsn_pmksa_cache_entry *entry,
				      void *ctx), void *ctx);
void pmksa_cache_auth_deinit(struct rsn_pmksa_cache *pmksa);
struct rsn_pmksa_cache_entry *
pmksa_cache_auth_get(struct rsn_pmksa_cache *pmksa,
		     const u8 *spa, const u8 *pmkid);
struct rsn_pmksa_cache_entry * pmksa_cache_get_okc(
	struct rsn_pmksa_cache *pmksa, const u8 *spa, const u8 *aa,
	const u8 *pmkid);
struct rsn_pmksa_cache_entry *
pmksa_cache_auth_add(struct rsn_pmksa_cache *pmksa,
		     const u8 *pmk, size_t pmk_len, const u8 *pmkid,
		     const u8 *kck, size_t kck_len,
		     const u8 *aa, const u8 *spa, int session_timeout,
		     struct eapol_state_machine *eapol, int akmp);
struct rsn_pmksa_cache_entry *
pmksa_cache_auth_create_entry(const u8 *pmk, size_t pmk_len, const u8 *pmkid,
			      const u8 *kck, size_t kck_len, const u8 *aa,
			      const u8 *spa, int session_timeout,
			      struct eapol_state_machine *eapol, int akmp);
int pmksa_cache_auth_add_entry(struct rsn_pmksa_cache *pmksa,
			       struct rsn_pmksa_cache_entry *entry);
struct rsn_pmksa_cache_entry *
pmksa_cache_add_okc(struct rsn_pmksa_cache *pmksa,
		    const struct rsn_pmksa_cache_entry *old_entry,
		    const u8 *aa, const u8 *pmkid);
void pmksa_cache_to_eapol_data(struct hostapd_data *hapd,
			       struct rsn_pmksa_cache_entry *entry,
			       struct eapol_state_machine *eapol);
void pmksa_cache_free_entry(struct rsn_pmksa_cache *pmksa,
			    struct rsn_pmksa_cache_entry *entry);
int pmksa_cache_auth_radius_das_disconnect(struct rsn_pmksa_cache *pmksa,
					   struct radius_das_attrs *attr);
int pmksa_cache_auth_list(struct rsn_pmksa_cache *pmksa, char *buf, size_t len);
void pmksa_cache_auth_flush(struct rsn_pmksa_cache *pmksa);
int pmksa_cache_auth_list_mesh(struct rsn_pmksa_cache *pmksa, const u8 *addr,
			       char *buf, size_t len);

#endif /* PMKSA_CACHE_H */
