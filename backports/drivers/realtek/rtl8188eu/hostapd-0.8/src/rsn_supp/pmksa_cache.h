/*
 * wpa_supplicant - WPA2/RSN PMKSA cache functions
 * Copyright (c) 2003-2008, Jouni Malinen <j@w1.fi>
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

#ifndef PMKSA_CACHE_H
#define PMKSA_CACHE_H

/**
 * struct rsn_pmksa_cache_entry - PMKSA cache entry
 */
struct rsn_pmksa_cache_entry {
	struct rsn_pmksa_cache_entry *next;
	u8 pmkid[PMKID_LEN];
	u8 pmk[PMK_LEN];
	size_t pmk_len;
	os_time_t expiration;
	int akmp; /* WPA_KEY_MGMT_* */
	u8 aa[ETH_ALEN];

	os_time_t reauth_time;

	/**
	 * network_ctx - Network configuration context
	 *
	 * This field is only used to match PMKSA cache entries to a specific
	 * network configuration (e.g., a specific SSID and security policy).
	 * This can be a pointer to the configuration entry, but PMKSA caching
	 * code does not dereference the value and this could be any kind of
	 * identifier.
	 */
	void *network_ctx;
	int opportunistic;
};

struct rsn_pmksa_cache;

#if defined(IEEE8021X_EAPOL) && !defined(CONFIG_NO_WPA2)

struct rsn_pmksa_cache *
pmksa_cache_init(void (*free_cb)(struct rsn_pmksa_cache_entry *entry,
				 void *ctx, int replace),
		 void *ctx, struct wpa_sm *sm);
void pmksa_cache_deinit(struct rsn_pmksa_cache *pmksa);
struct rsn_pmksa_cache_entry * pmksa_cache_get(struct rsn_pmksa_cache *pmksa,
					       const u8 *aa, const u8 *pmkid);
int pmksa_cache_list(struct rsn_pmksa_cache *pmksa, char *buf, size_t len);
struct rsn_pmksa_cache_entry *
pmksa_cache_add(struct rsn_pmksa_cache *pmksa, const u8 *pmk, size_t pmk_len,
		const u8 *aa, const u8 *spa, void *network_ctx, int akmp);
void pmksa_cache_notify_reconfig(struct rsn_pmksa_cache *pmksa);
struct rsn_pmksa_cache_entry * pmksa_cache_get_current(struct wpa_sm *sm);
void pmksa_cache_clear_current(struct wpa_sm *sm);
int pmksa_cache_set_current(struct wpa_sm *sm, const u8 *pmkid,
			    const u8 *bssid, void *network_ctx,
			    int try_opportunistic);
struct rsn_pmksa_cache_entry *
pmksa_cache_get_opportunistic(struct rsn_pmksa_cache *pmksa,
			      void *network_ctx, const u8 *aa);

#else /* IEEE8021X_EAPOL and !CONFIG_NO_WPA2 */

static inline struct rsn_pmksa_cache *
pmksa_cache_init(void (*free_cb)(struct rsn_pmksa_cache_entry *entry,
				 void *ctx, int replace),
		 void *ctx, struct wpa_sm *sm)
{
	return (void *) -1;
}

static inline void pmksa_cache_deinit(struct rsn_pmksa_cache *pmksa)
{
}

static inline struct rsn_pmksa_cache_entry *
pmksa_cache_get(struct rsn_pmksa_cache *pmksa, const u8 *aa, const u8 *pmkid)
{
	return NULL;
}

static inline struct rsn_pmksa_cache_entry *
pmksa_cache_get_current(struct wpa_sm *sm)
{
	return NULL;
}

static inline int pmksa_cache_list(struct rsn_pmksa_cache *pmksa, char *buf,
				   size_t len)
{
	return -1;
}

static inline struct rsn_pmksa_cache_entry *
pmksa_cache_add(struct rsn_pmksa_cache *pmksa, const u8 *pmk, size_t pmk_len,
		const u8 *aa, const u8 *spa, void *network_ctx, int akmp)
{
	return NULL;
}

static inline void pmksa_cache_notify_reconfig(struct rsn_pmksa_cache *pmksa)
{
}

static inline void pmksa_cache_clear_current(struct wpa_sm *sm)
{
}

static inline int pmksa_cache_set_current(struct wpa_sm *sm, const u8 *pmkid,
					  const u8 *bssid,
					  void *network_ctx,
					  int try_opportunistic)
{
	return -1;
}

#endif /* IEEE8021X_EAPOL and !CONFIG_NO_WPA2 */

#endif /* PMKSA_CACHE_H */
