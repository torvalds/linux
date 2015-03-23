/*
 * hostapd / Configuration helper functions
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
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "crypto/sha1.h"
#include "radius/radius_client.h"
#include "common/ieee802_11_defs.h"
#include "common/eapol_common.h"
#include "eap_common/eap_wsc_common.h"
#include "eap_server/eap.h"
#include "wpa_auth.h"
#include "sta_info.h"
#include "ap_config.h"


static void hostapd_config_free_vlan(struct hostapd_bss_config *bss)
{
	struct hostapd_vlan *vlan, *prev;

	vlan = bss->vlan;
	prev = NULL;
	while (vlan) {
		prev = vlan;
		vlan = vlan->next;
		os_free(prev);
	}

	bss->vlan = NULL;
}


void hostapd_config_defaults_bss(struct hostapd_bss_config *bss)
{
	bss->logger_syslog_level = HOSTAPD_LEVEL_INFO;
	bss->logger_stdout_level = HOSTAPD_LEVEL_INFO;
	bss->logger_syslog = (unsigned int) -1;
	bss->logger_stdout = (unsigned int) -1;

	bss->auth_algs = WPA_AUTH_ALG_OPEN | WPA_AUTH_ALG_SHARED;

	bss->wep_rekeying_period = 300;
	/* use key0 in individual key and key1 in broadcast key */
	bss->broadcast_key_idx_min = 1;
	bss->broadcast_key_idx_max = 2;
	bss->eap_reauth_period = 3600;

	bss->wpa_group_rekey = 600;
	bss->wpa_gmk_rekey = 86400;
	bss->wpa_key_mgmt = WPA_KEY_MGMT_PSK;
	bss->wpa_pairwise = WPA_CIPHER_TKIP;
	bss->wpa_group = WPA_CIPHER_TKIP;
	bss->rsn_pairwise = 0;

	bss->max_num_sta = MAX_STA_COUNT;

	bss->dtim_period = 2;

	bss->radius_server_auth_port = 1812;
	bss->ap_max_inactivity = AP_MAX_INACTIVITY;
	bss->eapol_version = EAPOL_VERSION;

	bss->max_listen_interval = 65535;

	bss->pwd_group = 19; /* ECC: GF(p=256) */

#ifdef CONFIG_IEEE80211W
	bss->assoc_sa_query_max_timeout = 1000;
	bss->assoc_sa_query_retry_timeout = 201;
#endif /* CONFIG_IEEE80211W */
#ifdef EAP_SERVER_FAST
	 /* both anonymous and authenticated provisioning */
	bss->eap_fast_prov = 3;
	bss->pac_key_lifetime = 7 * 24 * 60 * 60;
	bss->pac_key_refresh_time = 1 * 24 * 60 * 60;
#endif /* EAP_SERVER_FAST */

	/* Set to -1 as defaults depends on HT in setup */
	bss->wmm_enabled = -1;

#ifdef CONFIG_IEEE80211R
	bss->ft_over_ds = 1;
#endif /* CONFIG_IEEE80211R */
}


struct hostapd_config * hostapd_config_defaults(void)
{
#define ecw2cw(ecw) ((1 << (ecw)) - 1)

	struct hostapd_config *conf;
	struct hostapd_bss_config *bss;
	const int aCWmin = 4, aCWmax = 10;
	const struct hostapd_wmm_ac_params ac_bk =
		{ aCWmin, aCWmax, 7, 0, 0 }; /* background traffic */
	const struct hostapd_wmm_ac_params ac_be =
		{ aCWmin, aCWmax, 3, 0, 0 }; /* best effort traffic */
	const struct hostapd_wmm_ac_params ac_vi = /* video traffic */
		{ aCWmin - 1, aCWmin, 2, 3000 / 32, 1 };
	const struct hostapd_wmm_ac_params ac_vo = /* voice traffic */
		{ aCWmin - 2, aCWmin - 1, 2, 1500 / 32, 1 };
	const struct hostapd_tx_queue_params txq_bk =
		{ 7, ecw2cw(aCWmin), ecw2cw(aCWmax), 0 };
	const struct hostapd_tx_queue_params txq_be =
		{ 3, ecw2cw(aCWmin), 4 * (ecw2cw(aCWmin) + 1) - 1, 0};
	const struct hostapd_tx_queue_params txq_vi =
		{ 1, (ecw2cw(aCWmin) + 1) / 2 - 1, ecw2cw(aCWmin), 30};
	const struct hostapd_tx_queue_params txq_vo =
		{ 1, (ecw2cw(aCWmin) + 1) / 4 - 1,
		  (ecw2cw(aCWmin) + 1) / 2 - 1, 15};

#undef ecw2cw

	conf = os_zalloc(sizeof(*conf));
	bss = os_zalloc(sizeof(*bss));
	if (conf == NULL || bss == NULL) {
		wpa_printf(MSG_ERROR, "Failed to allocate memory for "
			   "configuration data.");
		os_free(conf);
		os_free(bss);
		return NULL;
	}

	bss->radius = os_zalloc(sizeof(*bss->radius));
	if (bss->radius == NULL) {
		os_free(conf);
		os_free(bss);
		return NULL;
	}

	hostapd_config_defaults_bss(bss);

	conf->num_bss = 1;
	conf->bss = bss;

	conf->beacon_int = 100;
	conf->rts_threshold = -1; /* use driver default: 2347 */
	conf->fragm_threshold = -1; /* user driver default: 2346 */
	conf->send_probe_response = 1;

	conf->wmm_ac_params[0] = ac_be;
	conf->wmm_ac_params[1] = ac_bk;
	conf->wmm_ac_params[2] = ac_vi;
	conf->wmm_ac_params[3] = ac_vo;

	conf->tx_queue[0] = txq_vo;
	conf->tx_queue[1] = txq_vi;
	conf->tx_queue[2] = txq_be;
	conf->tx_queue[3] = txq_bk;

	conf->ht_capab = HT_CAP_INFO_SMPS_DISABLED;

	return conf;
}


int hostapd_mac_comp(const void *a, const void *b)
{
	return os_memcmp(a, b, sizeof(macaddr));
}


int hostapd_mac_comp_empty(const void *a)
{
	macaddr empty = { 0 };
	return os_memcmp(a, empty, sizeof(macaddr));
}


static int hostapd_config_read_wpa_psk(const char *fname,
				       struct hostapd_ssid *ssid)
{
	FILE *f;
	char buf[128], *pos;
	int line = 0, ret = 0, len, ok;
	u8 addr[ETH_ALEN];
	struct hostapd_wpa_psk *psk;

	if (!fname)
		return 0;

	f = fopen(fname, "r");
	if (!f) {
		wpa_printf(MSG_ERROR, "WPA PSK file '%s' not found.", fname);
		return -1;
	}

	while (fgets(buf, sizeof(buf), f)) {
		line++;

		if (buf[0] == '#')
			continue;
		pos = buf;
		while (*pos != '\0') {
			if (*pos == '\n') {
				*pos = '\0';
				break;
			}
			pos++;
		}
		if (buf[0] == '\0')
			continue;

		if (hwaddr_aton(buf, addr)) {
			wpa_printf(MSG_ERROR, "Invalid MAC address '%s' on "
				   "line %d in '%s'", buf, line, fname);
			ret = -1;
			break;
		}

		psk = os_zalloc(sizeof(*psk));
		if (psk == NULL) {
			wpa_printf(MSG_ERROR, "WPA PSK allocation failed");
			ret = -1;
			break;
		}
		if (is_zero_ether_addr(addr))
			psk->group = 1;
		else
			os_memcpy(psk->addr, addr, ETH_ALEN);

		pos = buf + 17;
		if (*pos == '\0') {
			wpa_printf(MSG_ERROR, "No PSK on line %d in '%s'",
				   line, fname);
			os_free(psk);
			ret = -1;
			break;
		}
		pos++;

		ok = 0;
		len = os_strlen(pos);
		if (len == 64 && hexstr2bin(pos, psk->psk, PMK_LEN) == 0)
			ok = 1;
		else if (len >= 8 && len < 64) {
			pbkdf2_sha1(pos, ssid->ssid, ssid->ssid_len,
				    4096, psk->psk, PMK_LEN);
			ok = 1;
		}
		if (!ok) {
			wpa_printf(MSG_ERROR, "Invalid PSK '%s' on line %d in "
				   "'%s'", pos, line, fname);
			os_free(psk);
			ret = -1;
			break;
		}

		psk->next = ssid->wpa_psk;
		ssid->wpa_psk = psk;
	}

	fclose(f);

	return ret;
}


static int hostapd_derive_psk(struct hostapd_ssid *ssid)
{
	ssid->wpa_psk = os_zalloc(sizeof(struct hostapd_wpa_psk));
	if (ssid->wpa_psk == NULL) {
		wpa_printf(MSG_ERROR, "Unable to alloc space for PSK");
		return -1;
	}
	wpa_hexdump_ascii(MSG_DEBUG, "SSID",
			  (u8 *) ssid->ssid, ssid->ssid_len);
	wpa_hexdump_ascii_key(MSG_DEBUG, "PSK (ASCII passphrase)",
			      (u8 *) ssid->wpa_passphrase,
			      os_strlen(ssid->wpa_passphrase));
	pbkdf2_sha1(ssid->wpa_passphrase,
		    ssid->ssid, ssid->ssid_len,
		    4096, ssid->wpa_psk->psk, PMK_LEN);
	wpa_hexdump_key(MSG_DEBUG, "PSK (from passphrase)",
			ssid->wpa_psk->psk, PMK_LEN);
	return 0;
}


int hostapd_setup_wpa_psk(struct hostapd_bss_config *conf)
{
	struct hostapd_ssid *ssid = &conf->ssid;

	if (ssid->wpa_passphrase != NULL) {
		if (ssid->wpa_psk != NULL) {
			wpa_printf(MSG_DEBUG, "Using pre-configured WPA PSK "
				   "instead of passphrase");
		} else {
			wpa_printf(MSG_DEBUG, "Deriving WPA PSK based on "
				   "passphrase");
			if (hostapd_derive_psk(ssid) < 0)
				return -1;
		}
		ssid->wpa_psk->group = 1;
	}

	if (ssid->wpa_psk_file) {
		if (hostapd_config_read_wpa_psk(ssid->wpa_psk_file,
						&conf->ssid))
			return -1;
	}

	return 0;
}


int hostapd_wep_key_cmp(struct hostapd_wep_keys *a, struct hostapd_wep_keys *b)
{
	int i;

	if (a->idx != b->idx || a->default_len != b->default_len)
		return 1;
	for (i = 0; i < NUM_WEP_KEYS; i++)
		if (a->len[i] != b->len[i] ||
		    os_memcmp(a->key[i], b->key[i], a->len[i]) != 0)
			return 1;
	return 0;
}


static void hostapd_config_free_radius(struct hostapd_radius_server *servers,
				       int num_servers)
{
	int i;

	for (i = 0; i < num_servers; i++) {
		os_free(servers[i].shared_secret);
	}
	os_free(servers);
}


static void hostapd_config_free_eap_user(struct hostapd_eap_user *user)
{
	os_free(user->identity);
	os_free(user->password);
	os_free(user);
}


static void hostapd_config_free_wep(struct hostapd_wep_keys *keys)
{
	int i;
	for (i = 0; i < NUM_WEP_KEYS; i++) {
		os_free(keys->key[i]);
		keys->key[i] = NULL;
	}
}


static void hostapd_config_free_bss(struct hostapd_bss_config *conf)
{
	struct hostapd_wpa_psk *psk, *prev;
	struct hostapd_eap_user *user, *prev_user;

	if (conf == NULL)
		return;

	psk = conf->ssid.wpa_psk;
	while (psk) {
		prev = psk;
		psk = psk->next;
		os_free(prev);
	}

	os_free(conf->ssid.wpa_passphrase);
	os_free(conf->ssid.wpa_psk_file);
	hostapd_config_free_wep(&conf->ssid.wep);
#ifdef CONFIG_FULL_DYNAMIC_VLAN
	os_free(conf->ssid.vlan_tagged_interface);
#endif /* CONFIG_FULL_DYNAMIC_VLAN */

	user = conf->eap_user;
	while (user) {
		prev_user = user;
		user = user->next;
		hostapd_config_free_eap_user(prev_user);
	}

	os_free(conf->dump_log_name);
	os_free(conf->eap_req_id_text);
	os_free(conf->accept_mac);
	os_free(conf->deny_mac);
	os_free(conf->nas_identifier);
	hostapd_config_free_radius(conf->radius->auth_servers,
				   conf->radius->num_auth_servers);
	hostapd_config_free_radius(conf->radius->acct_servers,
				   conf->radius->num_acct_servers);
	os_free(conf->rsn_preauth_interfaces);
	os_free(conf->ctrl_interface);
	os_free(conf->ca_cert);
	os_free(conf->server_cert);
	os_free(conf->private_key);
	os_free(conf->private_key_passwd);
	os_free(conf->dh_file);
	os_free(conf->pac_opaque_encr_key);
	os_free(conf->eap_fast_a_id);
	os_free(conf->eap_fast_a_id_info);
	os_free(conf->eap_sim_db);
	os_free(conf->radius_server_clients);
	os_free(conf->test_socket);
	os_free(conf->radius);
	hostapd_config_free_vlan(conf);
	if (conf->ssid.dyn_vlan_keys) {
		struct hostapd_ssid *ssid = &conf->ssid;
		size_t i;
		for (i = 0; i <= ssid->max_dyn_vlan_keys; i++) {
			if (ssid->dyn_vlan_keys[i] == NULL)
				continue;
			hostapd_config_free_wep(ssid->dyn_vlan_keys[i]);
			os_free(ssid->dyn_vlan_keys[i]);
		}
		os_free(ssid->dyn_vlan_keys);
		ssid->dyn_vlan_keys = NULL;
	}

#ifdef CONFIG_IEEE80211R
	{
		struct ft_remote_r0kh *r0kh, *r0kh_prev;
		struct ft_remote_r1kh *r1kh, *r1kh_prev;

		r0kh = conf->r0kh_list;
		conf->r0kh_list = NULL;
		while (r0kh) {
			r0kh_prev = r0kh;
			r0kh = r0kh->next;
			os_free(r0kh_prev);
		}

		r1kh = conf->r1kh_list;
		conf->r1kh_list = NULL;
		while (r1kh) {
			r1kh_prev = r1kh;
			r1kh = r1kh->next;
			os_free(r1kh_prev);
		}
	}
#endif /* CONFIG_IEEE80211R */

#ifdef CONFIG_WPS
	os_free(conf->wps_pin_requests);
	os_free(conf->device_name);
	os_free(conf->manufacturer);
	os_free(conf->model_name);
	os_free(conf->model_number);
	os_free(conf->serial_number);
	os_free(conf->config_methods);
	os_free(conf->ap_pin);
	os_free(conf->extra_cred);
	os_free(conf->ap_settings);
	os_free(conf->upnp_iface);
	os_free(conf->friendly_name);
	os_free(conf->manufacturer_url);
	os_free(conf->model_description);
	os_free(conf->model_url);
	os_free(conf->upc);
#endif /* CONFIG_WPS */
}


/**
 * hostapd_config_free - Free hostapd configuration
 * @conf: Configuration data from hostapd_config_read().
 */
void hostapd_config_free(struct hostapd_config *conf)
{
	size_t i;

	if (conf == NULL)
		return;

	for (i = 0; i < conf->num_bss; i++)
		hostapd_config_free_bss(&conf->bss[i]);
	os_free(conf->bss);
	os_free(conf->supported_rates);
	os_free(conf->basic_rates);

	os_free(conf);
}


/**
 * hostapd_maclist_found - Find a MAC address from a list
 * @list: MAC address list
 * @num_entries: Number of addresses in the list
 * @addr: Address to search for
 * @vlan_id: Buffer for returning VLAN ID or %NULL if not needed
 * Returns: 1 if address is in the list or 0 if not.
 *
 * Perform a binary search for given MAC address from a pre-sorted list.
 */
int hostapd_maclist_found(struct mac_acl_entry *list, int num_entries,
			  const u8 *addr, int *vlan_id)
{
	int start, end, middle, res;

	start = 0;
	end = num_entries - 1;

	while (start <= end) {
		middle = (start + end) / 2;
		res = os_memcmp(list[middle].addr, addr, ETH_ALEN);
		if (res == 0) {
			if (vlan_id)
				*vlan_id = list[middle].vlan_id;
			return 1;
		}
		if (res < 0)
			start = middle + 1;
		else
			end = middle - 1;
	}

	return 0;
}


int hostapd_rate_found(int *list, int rate)
{
	int i;

	if (list == NULL)
		return 0;

	for (i = 0; list[i] >= 0; i++)
		if (list[i] == rate)
			return 1;

	return 0;
}


const char * hostapd_get_vlan_id_ifname(struct hostapd_vlan *vlan, int vlan_id)
{
	struct hostapd_vlan *v = vlan;
	while (v) {
		if (v->vlan_id == vlan_id || v->vlan_id == VLAN_ID_WILDCARD)
			return v->ifname;
		v = v->next;
	}
	return NULL;
}


const u8 * hostapd_get_psk(const struct hostapd_bss_config *conf,
			   const u8 *addr, const u8 *prev_psk)
{
	struct hostapd_wpa_psk *psk;
	int next_ok = prev_psk == NULL;

	for (psk = conf->ssid.wpa_psk; psk != NULL; psk = psk->next) {
		if (next_ok &&
		    (psk->group || os_memcmp(psk->addr, addr, ETH_ALEN) == 0))
			return psk->psk;

		if (psk->psk == prev_psk)
			next_ok = 1;
	}

	return NULL;
}


const struct hostapd_eap_user *
hostapd_get_eap_user(const struct hostapd_bss_config *conf, const u8 *identity,
		     size_t identity_len, int phase2)
{
	struct hostapd_eap_user *user = conf->eap_user;

#ifdef CONFIG_WPS
	if (conf->wps_state && identity_len == WSC_ID_ENROLLEE_LEN &&
	    os_memcmp(identity, WSC_ID_ENROLLEE, WSC_ID_ENROLLEE_LEN) == 0) {
		static struct hostapd_eap_user wsc_enrollee;
		os_memset(&wsc_enrollee, 0, sizeof(wsc_enrollee));
		wsc_enrollee.methods[0].method = eap_server_get_type(
			"WSC", &wsc_enrollee.methods[0].vendor);
		return &wsc_enrollee;
	}

	if (conf->wps_state && identity_len == WSC_ID_REGISTRAR_LEN &&
	    os_memcmp(identity, WSC_ID_REGISTRAR, WSC_ID_REGISTRAR_LEN) == 0) {
		static struct hostapd_eap_user wsc_registrar;
		os_memset(&wsc_registrar, 0, sizeof(wsc_registrar));
		wsc_registrar.methods[0].method = eap_server_get_type(
			"WSC", &wsc_registrar.methods[0].vendor);
		wsc_registrar.password = (u8 *) conf->ap_pin;
		wsc_registrar.password_len = conf->ap_pin ?
			os_strlen(conf->ap_pin) : 0;
		return &wsc_registrar;
	}
#endif /* CONFIG_WPS */

	while (user) {
		if (!phase2 && user->identity == NULL) {
			/* Wildcard match */
			break;
		}

		if (user->phase2 == !!phase2 && user->wildcard_prefix &&
		    identity_len >= user->identity_len &&
		    os_memcmp(user->identity, identity, user->identity_len) ==
		    0) {
			/* Wildcard prefix match */
			break;
		}

		if (user->phase2 == !!phase2 &&
		    user->identity_len == identity_len &&
		    os_memcmp(user->identity, identity, identity_len) == 0)
			break;
		user = user->next;
	}

	return user;
}
