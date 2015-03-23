/*
 * hostapd / IEEE 802.11 authentication (ACL)
 * Copyright (c) 2003-2009, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 *
 * Access control list for IEEE 802.11 authentication can uses statically
 * configured ACL from configuration files or an external RADIUS server.
 * Results from external RADIUS queries are cached to allow faster
 * authentication frame processing.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "radius/radius.h"
#include "radius/radius_client.h"
#include "hostapd.h"
#include "ap_config.h"
#include "ap_drv_ops.h"
#include "ieee802_11.h"
#include "ieee802_11_auth.h"

#define RADIUS_ACL_TIMEOUT 30


struct hostapd_cached_radius_acl {
	os_time_t timestamp;
	macaddr addr;
	int accepted; /* HOSTAPD_ACL_* */
	struct hostapd_cached_radius_acl *next;
	u32 session_timeout;
	u32 acct_interim_interval;
	int vlan_id;
};


struct hostapd_acl_query_data {
	os_time_t timestamp;
	u8 radius_id;
	macaddr addr;
	u8 *auth_msg; /* IEEE 802.11 authentication frame from station */
	size_t auth_msg_len;
	struct hostapd_acl_query_data *next;
};


#ifndef CONFIG_NO_RADIUS
static void hostapd_acl_cache_free(struct hostapd_cached_radius_acl *acl_cache)
{
	struct hostapd_cached_radius_acl *prev;

	while (acl_cache) {
		prev = acl_cache;
		acl_cache = acl_cache->next;
		os_free(prev);
	}
}


static int hostapd_acl_cache_get(struct hostapd_data *hapd, const u8 *addr,
				 u32 *session_timeout,
				 u32 *acct_interim_interval, int *vlan_id)
{
	struct hostapd_cached_radius_acl *entry;
	struct os_time now;

	os_get_time(&now);
	entry = hapd->acl_cache;

	while (entry) {
		if (os_memcmp(entry->addr, addr, ETH_ALEN) == 0) {
			if (now.sec - entry->timestamp > RADIUS_ACL_TIMEOUT)
				return -1; /* entry has expired */
			if (entry->accepted == HOSTAPD_ACL_ACCEPT_TIMEOUT)
				if (session_timeout)
					*session_timeout =
						entry->session_timeout;
			if (acct_interim_interval)
				*acct_interim_interval =
					entry->acct_interim_interval;
			if (vlan_id)
				*vlan_id = entry->vlan_id;
			return entry->accepted;
		}

		entry = entry->next;
	}

	return -1;
}
#endif /* CONFIG_NO_RADIUS */


static void hostapd_acl_query_free(struct hostapd_acl_query_data *query)
{
	if (query == NULL)
		return;
	os_free(query->auth_msg);
	os_free(query);
}


#ifndef CONFIG_NO_RADIUS
static int hostapd_radius_acl_query(struct hostapd_data *hapd, const u8 *addr,
				    struct hostapd_acl_query_data *query)
{
	struct radius_msg *msg;
	char buf[128];

	query->radius_id = radius_client_get_id(hapd->radius);
	msg = radius_msg_new(RADIUS_CODE_ACCESS_REQUEST, query->radius_id);
	if (msg == NULL)
		return -1;

	radius_msg_make_authenticator(msg, addr, ETH_ALEN);

	os_snprintf(buf, sizeof(buf), RADIUS_ADDR_FORMAT, MAC2STR(addr));
	if (!radius_msg_add_attr(msg, RADIUS_ATTR_USER_NAME, (u8 *) buf,
				 os_strlen(buf))) {
		wpa_printf(MSG_DEBUG, "Could not add User-Name");
		goto fail;
	}

	if (!radius_msg_add_attr_user_password(
		    msg, (u8 *) buf, os_strlen(buf),
		    hapd->conf->radius->auth_server->shared_secret,
		    hapd->conf->radius->auth_server->shared_secret_len)) {
		wpa_printf(MSG_DEBUG, "Could not add User-Password");
		goto fail;
	}

	if (hapd->conf->own_ip_addr.af == AF_INET &&
	    !radius_msg_add_attr(msg, RADIUS_ATTR_NAS_IP_ADDRESS,
				 (u8 *) &hapd->conf->own_ip_addr.u.v4, 4)) {
		wpa_printf(MSG_DEBUG, "Could not add NAS-IP-Address");
		goto fail;
	}

#ifdef CONFIG_IPV6
	if (hapd->conf->own_ip_addr.af == AF_INET6 &&
	    !radius_msg_add_attr(msg, RADIUS_ATTR_NAS_IPV6_ADDRESS,
				 (u8 *) &hapd->conf->own_ip_addr.u.v6, 16)) {
		wpa_printf(MSG_DEBUG, "Could not add NAS-IPv6-Address");
		goto fail;
	}
#endif /* CONFIG_IPV6 */

	if (hapd->conf->nas_identifier &&
	    !radius_msg_add_attr(msg, RADIUS_ATTR_NAS_IDENTIFIER,
				 (u8 *) hapd->conf->nas_identifier,
				 os_strlen(hapd->conf->nas_identifier))) {
		wpa_printf(MSG_DEBUG, "Could not add NAS-Identifier");
		goto fail;
	}

	os_snprintf(buf, sizeof(buf), RADIUS_802_1X_ADDR_FORMAT ":%s",
		    MAC2STR(hapd->own_addr), hapd->conf->ssid.ssid);
	if (!radius_msg_add_attr(msg, RADIUS_ATTR_CALLED_STATION_ID,
				 (u8 *) buf, os_strlen(buf))) {
		wpa_printf(MSG_DEBUG, "Could not add Called-Station-Id");
		goto fail;
	}

	os_snprintf(buf, sizeof(buf), RADIUS_802_1X_ADDR_FORMAT,
		    MAC2STR(addr));
	if (!radius_msg_add_attr(msg, RADIUS_ATTR_CALLING_STATION_ID,
				 (u8 *) buf, os_strlen(buf))) {
		wpa_printf(MSG_DEBUG, "Could not add Calling-Station-Id");
		goto fail;
	}

	if (!radius_msg_add_attr_int32(msg, RADIUS_ATTR_NAS_PORT_TYPE,
				       RADIUS_NAS_PORT_TYPE_IEEE_802_11)) {
		wpa_printf(MSG_DEBUG, "Could not add NAS-Port-Type");
		goto fail;
	}

	os_snprintf(buf, sizeof(buf), "CONNECT 11Mbps 802.11b");
	if (!radius_msg_add_attr(msg, RADIUS_ATTR_CONNECT_INFO,
				 (u8 *) buf, os_strlen(buf))) {
		wpa_printf(MSG_DEBUG, "Could not add Connect-Info");
		goto fail;
	}

	radius_client_send(hapd->radius, msg, RADIUS_AUTH, addr);
	return 0;

 fail:
	radius_msg_free(msg);
	return -1;
}
#endif /* CONFIG_NO_RADIUS */


/**
 * hostapd_allowed_address - Check whether a specified STA can be authenticated
 * @hapd: hostapd BSS data
 * @addr: MAC address of the STA
 * @msg: Authentication message
 * @len: Length of msg in octets
 * @session_timeout: Buffer for returning session timeout (from RADIUS)
 * @acct_interim_interval: Buffer for returning account interval (from RADIUS)
 * @vlan_id: Buffer for returning VLAN ID
 * Returns: HOSTAPD_ACL_ACCEPT, HOSTAPD_ACL_REJECT, or HOSTAPD_ACL_PENDING
 */
int hostapd_allowed_address(struct hostapd_data *hapd, const u8 *addr,
			    const u8 *msg, size_t len, u32 *session_timeout,
			    u32 *acct_interim_interval, int *vlan_id)
{
	if (session_timeout)
		*session_timeout = 0;
	if (acct_interim_interval)
		*acct_interim_interval = 0;
	if (vlan_id)
		*vlan_id = 0;

	if (hostapd_maclist_found(hapd->conf->accept_mac,
				  hapd->conf->num_accept_mac, addr, vlan_id))
		return HOSTAPD_ACL_ACCEPT;

	if (hostapd_maclist_found(hapd->conf->deny_mac,
				  hapd->conf->num_deny_mac, addr, vlan_id))
		return HOSTAPD_ACL_REJECT;

	if (hapd->conf->macaddr_acl == ACCEPT_UNLESS_DENIED)
		return HOSTAPD_ACL_ACCEPT;
	if (hapd->conf->macaddr_acl == DENY_UNLESS_ACCEPTED)
		return HOSTAPD_ACL_REJECT;

	if (hapd->conf->macaddr_acl == USE_EXTERNAL_RADIUS_AUTH) {
#ifdef CONFIG_NO_RADIUS
		return HOSTAPD_ACL_REJECT;
#else /* CONFIG_NO_RADIUS */
		struct hostapd_acl_query_data *query;

		/* Check whether ACL cache has an entry for this station */
		int res = hostapd_acl_cache_get(hapd, addr, session_timeout,
						acct_interim_interval,
						vlan_id);
		if (res == HOSTAPD_ACL_ACCEPT ||
		    res == HOSTAPD_ACL_ACCEPT_TIMEOUT)
			return res;
		if (res == HOSTAPD_ACL_REJECT)
			return HOSTAPD_ACL_REJECT;

		query = hapd->acl_queries;
		while (query) {
			if (os_memcmp(query->addr, addr, ETH_ALEN) == 0) {
				/* pending query in RADIUS retransmit queue;
				 * do not generate a new one */
				return HOSTAPD_ACL_PENDING;
			}
			query = query->next;
		}

		if (!hapd->conf->radius->auth_server)
			return HOSTAPD_ACL_REJECT;

		/* No entry in the cache - query external RADIUS server */
		query = os_zalloc(sizeof(*query));
		if (query == NULL) {
			wpa_printf(MSG_ERROR, "malloc for query data failed");
			return HOSTAPD_ACL_REJECT;
		}
		time(&query->timestamp);
		os_memcpy(query->addr, addr, ETH_ALEN);
		if (hostapd_radius_acl_query(hapd, addr, query)) {
			wpa_printf(MSG_DEBUG, "Failed to send Access-Request "
				   "for ACL query.");
			hostapd_acl_query_free(query);
			return HOSTAPD_ACL_REJECT;
		}

		query->auth_msg = os_malloc(len);
		if (query->auth_msg == NULL) {
			wpa_printf(MSG_ERROR, "Failed to allocate memory for "
				   "auth frame.");
			hostapd_acl_query_free(query);
			return HOSTAPD_ACL_REJECT;
		}
		os_memcpy(query->auth_msg, msg, len);
		query->auth_msg_len = len;
		query->next = hapd->acl_queries;
		hapd->acl_queries = query;

		/* Queued data will be processed in hostapd_acl_recv_radius()
		 * when RADIUS server replies to the sent Access-Request. */
		return HOSTAPD_ACL_PENDING;
#endif /* CONFIG_NO_RADIUS */
	}

	return HOSTAPD_ACL_REJECT;
}


#ifndef CONFIG_NO_RADIUS
static void hostapd_acl_expire_cache(struct hostapd_data *hapd, os_time_t now)
{
	struct hostapd_cached_radius_acl *prev, *entry, *tmp;

	prev = NULL;
	entry = hapd->acl_cache;

	while (entry) {
		if (now - entry->timestamp > RADIUS_ACL_TIMEOUT) {
			wpa_printf(MSG_DEBUG, "Cached ACL entry for " MACSTR
				   " has expired.", MAC2STR(entry->addr));
			if (prev)
				prev->next = entry->next;
			else
				hapd->acl_cache = entry->next;
			hostapd_drv_set_radius_acl_expire(hapd, entry->addr);
			tmp = entry;
			entry = entry->next;
			os_free(tmp);
			continue;
		}

		prev = entry;
		entry = entry->next;
	}
}


static void hostapd_acl_expire_queries(struct hostapd_data *hapd,
				       os_time_t now)
{
	struct hostapd_acl_query_data *prev, *entry, *tmp;

	prev = NULL;
	entry = hapd->acl_queries;

	while (entry) {
		if (now - entry->timestamp > RADIUS_ACL_TIMEOUT) {
			wpa_printf(MSG_DEBUG, "ACL query for " MACSTR
				   " has expired.", MAC2STR(entry->addr));
			if (prev)
				prev->next = entry->next;
			else
				hapd->acl_queries = entry->next;

			tmp = entry;
			entry = entry->next;
			hostapd_acl_query_free(tmp);
			continue;
		}

		prev = entry;
		entry = entry->next;
	}
}


/**
 * hostapd_acl_expire - ACL cache expiration callback
 * @eloop_ctx: struct hostapd_data *
 * @timeout_ctx: Not used
 */
static void hostapd_acl_expire(void *eloop_ctx, void *timeout_ctx)
{
	struct hostapd_data *hapd = eloop_ctx;
	struct os_time now;

	os_get_time(&now);
	hostapd_acl_expire_cache(hapd, now.sec);
	hostapd_acl_expire_queries(hapd, now.sec);

	eloop_register_timeout(10, 0, hostapd_acl_expire, hapd, NULL);
}


/**
 * hostapd_acl_recv_radius - Process incoming RADIUS Authentication messages
 * @msg: RADIUS response message
 * @req: RADIUS request message
 * @shared_secret: RADIUS shared secret
 * @shared_secret_len: Length of shared_secret in octets
 * @data: Context data (struct hostapd_data *)
 * Returns: RADIUS_RX_PROCESSED if RADIUS message was a reply to ACL query (and
 * was processed here) or RADIUS_RX_UNKNOWN if not.
 */
static RadiusRxResult
hostapd_acl_recv_radius(struct radius_msg *msg, struct radius_msg *req,
			const u8 *shared_secret, size_t shared_secret_len,
			void *data)
{
	struct hostapd_data *hapd = data;
	struct hostapd_acl_query_data *query, *prev;
	struct hostapd_cached_radius_acl *cache;
	struct radius_hdr *hdr = radius_msg_get_hdr(msg);

	query = hapd->acl_queries;
	prev = NULL;
	while (query) {
		if (query->radius_id == hdr->identifier)
			break;
		prev = query;
		query = query->next;
	}
	if (query == NULL)
		return RADIUS_RX_UNKNOWN;

	wpa_printf(MSG_DEBUG, "Found matching Access-Request for RADIUS "
		   "message (id=%d)", query->radius_id);

	if (radius_msg_verify(msg, shared_secret, shared_secret_len, req, 0)) {
		wpa_printf(MSG_INFO, "Incoming RADIUS packet did not have "
			   "correct authenticator - dropped\n");
		return RADIUS_RX_INVALID_AUTHENTICATOR;
	}

	if (hdr->code != RADIUS_CODE_ACCESS_ACCEPT &&
	    hdr->code != RADIUS_CODE_ACCESS_REJECT) {
		wpa_printf(MSG_DEBUG, "Unknown RADIUS message code %d to ACL "
			   "query", hdr->code);
		return RADIUS_RX_UNKNOWN;
	}

	/* Insert Accept/Reject info into ACL cache */
	cache = os_zalloc(sizeof(*cache));
	if (cache == NULL) {
		wpa_printf(MSG_DEBUG, "Failed to add ACL cache entry");
		goto done;
	}
	time(&cache->timestamp);
	os_memcpy(cache->addr, query->addr, sizeof(cache->addr));
	if (hdr->code == RADIUS_CODE_ACCESS_ACCEPT) {
		if (radius_msg_get_attr_int32(msg, RADIUS_ATTR_SESSION_TIMEOUT,
					      &cache->session_timeout) == 0)
			cache->accepted = HOSTAPD_ACL_ACCEPT_TIMEOUT;
		else
			cache->accepted = HOSTAPD_ACL_ACCEPT;

		if (radius_msg_get_attr_int32(
			    msg, RADIUS_ATTR_ACCT_INTERIM_INTERVAL,
			    &cache->acct_interim_interval) == 0 &&
		    cache->acct_interim_interval < 60) {
			wpa_printf(MSG_DEBUG, "Ignored too small "
				   "Acct-Interim-Interval %d for STA " MACSTR,
				   cache->acct_interim_interval,
				   MAC2STR(query->addr));
			cache->acct_interim_interval = 0;
		}

		cache->vlan_id = radius_msg_get_vlanid(msg);
	} else
		cache->accepted = HOSTAPD_ACL_REJECT;
	cache->next = hapd->acl_cache;
	hapd->acl_cache = cache;

#ifdef CONFIG_DRIVER_RADIUS_ACL
	hostapd_drv_set_radius_acl_auth(hapd, query->addr, cache->accepted,
					cache->session_timeout);
#else /* CONFIG_DRIVER_RADIUS_ACL */
#ifdef NEED_AP_MLME
	/* Re-send original authentication frame for 802.11 processing */
	wpa_printf(MSG_DEBUG, "Re-sending authentication frame after "
		   "successful RADIUS ACL query");
	ieee802_11_mgmt(hapd, query->auth_msg, query->auth_msg_len, NULL);
#endif /* NEED_AP_MLME */
#endif /* CONFIG_DRIVER_RADIUS_ACL */

 done:
	if (prev == NULL)
		hapd->acl_queries = query->next;
	else
		prev->next = query->next;

	hostapd_acl_query_free(query);

	return RADIUS_RX_PROCESSED;
}
#endif /* CONFIG_NO_RADIUS */


/**
 * hostapd_acl_init: Initialize IEEE 802.11 ACL
 * @hapd: hostapd BSS data
 * Returns: 0 on success, -1 on failure
 */
int hostapd_acl_init(struct hostapd_data *hapd)
{
#ifndef CONFIG_NO_RADIUS
	if (radius_client_register(hapd->radius, RADIUS_AUTH,
				   hostapd_acl_recv_radius, hapd))
		return -1;

	eloop_register_timeout(10, 0, hostapd_acl_expire, hapd, NULL);
#endif /* CONFIG_NO_RADIUS */

	return 0;
}


/**
 * hostapd_acl_deinit - Deinitialize IEEE 802.11 ACL
 * @hapd: hostapd BSS data
 */
void hostapd_acl_deinit(struct hostapd_data *hapd)
{
	struct hostapd_acl_query_data *query, *prev;

#ifndef CONFIG_NO_RADIUS
	eloop_cancel_timeout(hostapd_acl_expire, hapd, NULL);

	hostapd_acl_cache_free(hapd->acl_cache);
#endif /* CONFIG_NO_RADIUS */

	query = hapd->acl_queries;
	while (query) {
		prev = query;
		query = query->next;
		hostapd_acl_query_free(prev);
	}
}
