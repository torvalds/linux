/*
 * hostapd / Initialization and configuration
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
#include "radius/radius_client.h"
#include "drivers/driver.h"
#include "hostapd.h"
#include "authsrv.h"
#include "sta_info.h"
#include "accounting.h"
#include "ap_list.h"
#include "beacon.h"
#include "iapp.h"
#include "ieee802_1x.h"
#include "ieee802_11_auth.h"
#include "vlan_init.h"
#include "wpa_auth.h"
#include "wps_hostapd.h"
#include "hw_features.h"
#include "wpa_auth_glue.h"
#include "ap_drv_ops.h"
#include "ap_config.h"
#include "p2p_hostapd.h"


static int hostapd_flush_old_stations(struct hostapd_data *hapd);
static int hostapd_setup_encryption(char *iface, struct hostapd_data *hapd);

extern int wpa_debug_level;


static void hostapd_reload_bss(struct hostapd_data *hapd)
{
#ifndef CONFIG_NO_RADIUS
	radius_client_reconfig(hapd->radius, hapd->conf->radius);
#endif /* CONFIG_NO_RADIUS */

	if (hostapd_setup_wpa_psk(hapd->conf)) {
		wpa_printf(MSG_ERROR, "Failed to re-configure WPA PSK "
			   "after reloading configuration");
	}

	if (hapd->conf->ieee802_1x || hapd->conf->wpa)
		hostapd_set_drv_ieee8021x(hapd, hapd->conf->iface, 1);
	else
		hostapd_set_drv_ieee8021x(hapd, hapd->conf->iface, 0);

	if (hapd->conf->wpa && hapd->wpa_auth == NULL)
		hostapd_setup_wpa(hapd);
	else if (hapd->conf->wpa) {
		const u8 *wpa_ie;
		size_t wpa_ie_len;
		hostapd_reconfig_wpa(hapd);
		wpa_ie = wpa_auth_get_wpa_ie(hapd->wpa_auth, &wpa_ie_len);
		if (hostapd_set_generic_elem(hapd, wpa_ie, wpa_ie_len))
			wpa_printf(MSG_ERROR, "Failed to configure WPA IE for "
				   "the kernel driver.");
	} else if (hapd->wpa_auth) {
		wpa_deinit(hapd->wpa_auth);
		hapd->wpa_auth = NULL;
		hostapd_set_privacy(hapd, 0);
		hostapd_setup_encryption(hapd->conf->iface, hapd);
		hostapd_set_generic_elem(hapd, (u8 *) "", 0);
	}

	ieee802_11_set_beacon(hapd);
	hostapd_update_wps(hapd);

	if (hapd->conf->ssid.ssid_set &&
	    hostapd_set_ssid(hapd, (u8 *) hapd->conf->ssid.ssid,
			     hapd->conf->ssid.ssid_len)) {
		wpa_printf(MSG_ERROR, "Could not set SSID for kernel driver");
		/* try to continue */
	}
	wpa_printf(MSG_DEBUG, "Reconfigured interface %s", hapd->conf->iface);
}


int hostapd_reload_config(struct hostapd_iface *iface)
{
	struct hostapd_data *hapd = iface->bss[0];
	struct hostapd_config *newconf, *oldconf;
	size_t j;

	if (iface->config_read_cb == NULL)
		return -1;
	newconf = iface->config_read_cb(iface->config_fname);
	if (newconf == NULL)
		return -1;

	/*
	 * Deauthenticate all stations since the new configuration may not
	 * allow them to use the BSS anymore.
	 */
	for (j = 0; j < iface->num_bss; j++) {
		hostapd_flush_old_stations(iface->bss[j]);

#ifndef CONFIG_NO_RADIUS
		/* TODO: update dynamic data based on changed configuration
		 * items (e.g., open/close sockets, etc.) */
		radius_client_flush(iface->bss[j]->radius, 0);
#endif /* CONFIG_NO_RADIUS */
	}

	oldconf = hapd->iconf;
	iface->conf = newconf;

	for (j = 0; j < iface->num_bss; j++) {
		hapd = iface->bss[j];
		hapd->iconf = newconf;
		hapd->conf = &newconf->bss[j];
		hostapd_reload_bss(hapd);
	}

	hostapd_config_free(oldconf);


	return 0;
}


static void hostapd_broadcast_key_clear_iface(struct hostapd_data *hapd,
					      char *ifname)
{
	int i;

	for (i = 0; i < NUM_WEP_KEYS; i++) {
		if (hostapd_drv_set_key(ifname, hapd, WPA_ALG_NONE, NULL, i,
					0, NULL, 0, NULL, 0)) {
			wpa_printf(MSG_DEBUG, "Failed to clear default "
				   "encryption keys (ifname=%s keyidx=%d)",
				   ifname, i);
		}
	}
#ifdef CONFIG_IEEE80211W
	if (hapd->conf->ieee80211w) {
		for (i = NUM_WEP_KEYS; i < NUM_WEP_KEYS + 2; i++) {
			if (hostapd_drv_set_key(ifname, hapd, WPA_ALG_NONE,
						NULL, i, 0, NULL,
						0, NULL, 0)) {
				wpa_printf(MSG_DEBUG, "Failed to clear "
					   "default mgmt encryption keys "
					   "(ifname=%s keyidx=%d)", ifname, i);
			}
		}
	}
#endif /* CONFIG_IEEE80211W */
}


static int hostapd_broadcast_wep_clear(struct hostapd_data *hapd)
{
	hostapd_broadcast_key_clear_iface(hapd, hapd->conf->iface);
	return 0;
}


static int hostapd_broadcast_wep_set(struct hostapd_data *hapd)
{
	int errors = 0, idx;
	struct hostapd_ssid *ssid = &hapd->conf->ssid;

	idx = ssid->wep.idx;
	if (ssid->wep.default_len &&
	    hostapd_drv_set_key(hapd->conf->iface,
				hapd, WPA_ALG_WEP, broadcast_ether_addr, idx,
				1, NULL, 0, ssid->wep.key[idx],
				ssid->wep.len[idx])) {
		wpa_printf(MSG_WARNING, "Could not set WEP encryption.");
		errors++;
	}

	if (ssid->dyn_vlan_keys) {
		size_t i;
		for (i = 0; i <= ssid->max_dyn_vlan_keys; i++) {
			const char *ifname;
			struct hostapd_wep_keys *key = ssid->dyn_vlan_keys[i];
			if (key == NULL)
				continue;
			ifname = hostapd_get_vlan_id_ifname(hapd->conf->vlan,
							    i);
			if (ifname == NULL)
				continue;

			idx = key->idx;
			if (hostapd_drv_set_key(ifname, hapd, WPA_ALG_WEP,
						broadcast_ether_addr, idx, 1,
						NULL, 0, key->key[idx],
						key->len[idx])) {
				wpa_printf(MSG_WARNING, "Could not set "
					   "dynamic VLAN WEP encryption.");
				errors++;
			}
		}
	}

	return errors;
}

/**
 * hostapd_cleanup - Per-BSS cleanup (deinitialization)
 * @hapd: Pointer to BSS data
 *
 * This function is used to free all per-BSS data structures and resources.
 * This gets called in a loop for each BSS between calls to
 * hostapd_cleanup_iface_pre() and hostapd_cleanup_iface() when an interface
 * is deinitialized. Most of the modules that are initialized in
 * hostapd_setup_bss() are deinitialized here.
 */
static void hostapd_cleanup(struct hostapd_data *hapd)
{
	if (hapd->iface->ctrl_iface_deinit)
		hapd->iface->ctrl_iface_deinit(hapd);

	iapp_deinit(hapd->iapp);
	hapd->iapp = NULL;
	accounting_deinit(hapd);
	hostapd_deinit_wpa(hapd);
	vlan_deinit(hapd);
	hostapd_acl_deinit(hapd);
#ifndef CONFIG_NO_RADIUS
	radius_client_deinit(hapd->radius);
	hapd->radius = NULL;
#endif /* CONFIG_NO_RADIUS */

	hostapd_deinit_wps(hapd);

	authsrv_deinit(hapd);

	if (hapd->interface_added &&
	    hostapd_if_remove(hapd, WPA_IF_AP_BSS, hapd->conf->iface)) {
		wpa_printf(MSG_WARNING, "Failed to remove BSS interface %s",
			   hapd->conf->iface);
	}

	os_free(hapd->probereq_cb);
	hapd->probereq_cb = NULL;

#ifdef CONFIG_P2P
	wpabuf_free(hapd->p2p_beacon_ie);
	hapd->p2p_beacon_ie = NULL;
	wpabuf_free(hapd->p2p_probe_resp_ie);
	hapd->p2p_probe_resp_ie = NULL;
#endif /* CONFIG_P2P */
}


/**
 * hostapd_cleanup_iface_pre - Preliminary per-interface cleanup
 * @iface: Pointer to interface data
 *
 * This function is called before per-BSS data structures are deinitialized
 * with hostapd_cleanup().
 */
static void hostapd_cleanup_iface_pre(struct hostapd_iface *iface)
{
}


/**
 * hostapd_cleanup_iface - Complete per-interface cleanup
 * @iface: Pointer to interface data
 *
 * This function is called after per-BSS data structures are deinitialized
 * with hostapd_cleanup().
 */
static void hostapd_cleanup_iface(struct hostapd_iface *iface)
{
	hostapd_free_hw_features(iface->hw_features, iface->num_hw_features);
	iface->hw_features = NULL;
	os_free(iface->current_rates);
	iface->current_rates = NULL;
	ap_list_deinit(iface);
	hostapd_config_free(iface->conf);
	iface->conf = NULL;

	os_free(iface->config_fname);
	os_free(iface->bss);
	os_free(iface);
}


static int hostapd_setup_encryption(char *iface, struct hostapd_data *hapd)
{
	int i;

	hostapd_broadcast_wep_set(hapd);

	if (hapd->conf->ssid.wep.default_len) {
		hostapd_set_privacy(hapd, 1);
		return 0;
	}

	for (i = 0; i < 4; i++) {
		if (hapd->conf->ssid.wep.key[i] &&
		    hostapd_drv_set_key(iface, hapd, WPA_ALG_WEP, NULL, i,
					i == hapd->conf->ssid.wep.idx, NULL, 0,
					hapd->conf->ssid.wep.key[i],
					hapd->conf->ssid.wep.len[i])) {
			wpa_printf(MSG_WARNING, "Could not set WEP "
				   "encryption.");
			return -1;
		}
		if (hapd->conf->ssid.wep.key[i] &&
		    i == hapd->conf->ssid.wep.idx)
			hostapd_set_privacy(hapd, 1);
	}

	return 0;
}


static int hostapd_flush_old_stations(struct hostapd_data *hapd)
{
	int ret = 0;
	u8 addr[ETH_ALEN];

	if (hostapd_drv_none(hapd) || hapd->drv_priv == NULL)
		return 0;

	wpa_printf(MSG_DEBUG, "Flushing old station entries");
	if (hostapd_flush(hapd)) {
		wpa_printf(MSG_WARNING, "Could not connect to kernel driver.");
		ret = -1;
	}
	wpa_printf(MSG_DEBUG, "Deauthenticate all stations");
	os_memset(addr, 0xff, ETH_ALEN);
	hostapd_drv_sta_deauth(hapd, addr, WLAN_REASON_PREV_AUTH_NOT_VALID);
	hostapd_free_stas(hapd);

	return ret;
}


/**
 * hostapd_validate_bssid_configuration - Validate BSSID configuration
 * @iface: Pointer to interface data
 * Returns: 0 on success, -1 on failure
 *
 * This function is used to validate that the configured BSSIDs are valid.
 */
static int hostapd_validate_bssid_configuration(struct hostapd_iface *iface)
{
	u8 mask[ETH_ALEN] = { 0 };
	struct hostapd_data *hapd = iface->bss[0];
	unsigned int i = iface->conf->num_bss, bits = 0, j;
	int res;
	int auto_addr = 0;

	if (hostapd_drv_none(hapd))
		return 0;

	/* Generate BSSID mask that is large enough to cover the BSSIDs. */

	/* Determine the bits necessary to cover the number of BSSIDs. */
	for (i--; i; i >>= 1)
		bits++;

	/* Determine the bits necessary to any configured BSSIDs,
	   if they are higher than the number of BSSIDs. */
	for (j = 0; j < iface->conf->num_bss; j++) {
		if (hostapd_mac_comp_empty(iface->conf->bss[j].bssid) == 0) {
			if (j)
				auto_addr++;
			continue;
		}

		for (i = 0; i < ETH_ALEN; i++) {
			mask[i] |=
				iface->conf->bss[j].bssid[i] ^
				hapd->own_addr[i];
		}
	}

	if (!auto_addr)
		goto skip_mask_ext;

	for (i = 0; i < ETH_ALEN && mask[i] == 0; i++)
		;
	j = 0;
	if (i < ETH_ALEN) {
		j = (5 - i) * 8;

		while (mask[i] != 0) {
			mask[i] >>= 1;
			j++;
		}
	}

	if (bits < j)
		bits = j;

	if (bits > 40) {
		wpa_printf(MSG_ERROR, "Too many bits in the BSSID mask (%u)",
			   bits);
		return -1;
	}

	os_memset(mask, 0xff, ETH_ALEN);
	j = bits / 8;
	for (i = 5; i > 5 - j; i--)
		mask[i] = 0;
	j = bits % 8;
	while (j--)
		mask[i] <<= 1;

skip_mask_ext:
	wpa_printf(MSG_DEBUG, "BSS count %lu, BSSID mask " MACSTR " (%d bits)",
		   (unsigned long) iface->conf->num_bss, MAC2STR(mask), bits);

	res = hostapd_valid_bss_mask(hapd, hapd->own_addr, mask);
	if (res == 0)
		return 0;

	if (res < 0) {
		wpa_printf(MSG_ERROR, "Driver did not accept BSSID mask "
			   MACSTR " for start address " MACSTR ".",
			   MAC2STR(mask), MAC2STR(hapd->own_addr));
		return -1;
	}

	if (!auto_addr)
		return 0;

	for (i = 0; i < ETH_ALEN; i++) {
		if ((hapd->own_addr[i] & mask[i]) != hapd->own_addr[i]) {
			wpa_printf(MSG_ERROR, "Invalid BSSID mask " MACSTR
				   " for start address " MACSTR ".",
				   MAC2STR(mask), MAC2STR(hapd->own_addr));
			wpa_printf(MSG_ERROR, "Start address must be the "
				   "first address in the block (i.e., addr "
				   "AND mask == addr).");
			return -1;
		}
	}

	return 0;
}


static int mac_in_conf(struct hostapd_config *conf, const void *a)
{
	size_t i;

	for (i = 0; i < conf->num_bss; i++) {
		if (hostapd_mac_comp(conf->bss[i].bssid, a) == 0) {
			return 1;
		}
	}

	return 0;
}




/**
 * hostapd_setup_bss - Per-BSS setup (initialization)
 * @hapd: Pointer to BSS data
 * @first: Whether this BSS is the first BSS of an interface
 *
 * This function is used to initialize all per-BSS data structures and
 * resources. This gets called in a loop for each BSS when an interface is
 * initialized. Most of the modules that are initialized here will be
 * deinitialized in hostapd_cleanup().
 */
static int hostapd_setup_bss(struct hostapd_data *hapd, int first)
{
	struct hostapd_bss_config *conf = hapd->conf;
	u8 ssid[HOSTAPD_MAX_SSID_LEN + 1];
	int ssid_len, set_ssid;
	char force_ifname[IFNAMSIZ];
	u8 if_addr[ETH_ALEN];

	if (!first) {
		if (hostapd_mac_comp_empty(hapd->conf->bssid) == 0) {
			/* Allocate the next available BSSID. */
			do {
				inc_byte_array(hapd->own_addr, ETH_ALEN);
			} while (mac_in_conf(hapd->iconf, hapd->own_addr));
		} else {
			/* Allocate the configured BSSID. */
			os_memcpy(hapd->own_addr, hapd->conf->bssid, ETH_ALEN);

			if (hostapd_mac_comp(hapd->own_addr,
					     hapd->iface->bss[0]->own_addr) ==
			    0) {
				wpa_printf(MSG_ERROR, "BSS '%s' may not have "
					   "BSSID set to the MAC address of "
					   "the radio", hapd->conf->iface);
				return -1;
			}
		}

		hapd->interface_added = 1;
		if (hostapd_if_add(hapd->iface->bss[0], WPA_IF_AP_BSS,
				   hapd->conf->iface, hapd->own_addr, hapd,
				   &hapd->drv_priv, force_ifname, if_addr,
				   hapd->conf->bridge[0] ? hapd->conf->bridge :
				   NULL)) {
			wpa_printf(MSG_ERROR, "Failed to add BSS (BSSID="
				   MACSTR ")", MAC2STR(hapd->own_addr));
			return -1;
		}
	}

	if (conf->wmm_enabled < 0)
		conf->wmm_enabled = hapd->iconf->ieee80211n;

	hostapd_flush_old_stations(hapd);
	hostapd_set_privacy(hapd, 0);

	hostapd_broadcast_wep_clear(hapd);
	if (hostapd_setup_encryption(hapd->conf->iface, hapd))
		return -1;

	/*
	 * Fetch the SSID from the system and use it or,
	 * if one was specified in the config file, verify they
	 * match.
	 */
	ssid_len = hostapd_get_ssid(hapd, ssid, sizeof(ssid));
	if (ssid_len < 0) {
		wpa_printf(MSG_ERROR, "Could not read SSID from system");
		return -1;
	}
	if (conf->ssid.ssid_set) {
		/*
		 * If SSID is specified in the config file and it differs
		 * from what is being used then force installation of the
		 * new SSID.
		 */
		set_ssid = (conf->ssid.ssid_len != (size_t) ssid_len ||
			    os_memcmp(conf->ssid.ssid, ssid, ssid_len) != 0);
	} else {
		/*
		 * No SSID in the config file; just use the one we got
		 * from the system.
		 */
		set_ssid = 0;
		conf->ssid.ssid_len = ssid_len;
		os_memcpy(conf->ssid.ssid, ssid, conf->ssid.ssid_len);
		conf->ssid.ssid[conf->ssid.ssid_len] = '\0';
	}

	if (!hostapd_drv_none(hapd)) {
		wpa_printf(MSG_ERROR, "Using interface %s with hwaddr " MACSTR
			   " and ssid '%s'",
			   hapd->conf->iface, MAC2STR(hapd->own_addr),
			   hapd->conf->ssid.ssid);
	}

	if (hostapd_setup_wpa_psk(conf)) {
		wpa_printf(MSG_ERROR, "WPA-PSK setup failed.");
		return -1;
	}

	/* Set SSID for the kernel driver (to be used in beacon and probe
	 * response frames) */
	if (set_ssid && hostapd_set_ssid(hapd, (u8 *) conf->ssid.ssid,
					 conf->ssid.ssid_len)) {
		wpa_printf(MSG_ERROR, "Could not set SSID for kernel driver");
		return -1;
	}

	if (wpa_debug_level == MSG_MSGDUMP)
		conf->radius->msg_dumps = 1;
#ifndef CONFIG_NO_RADIUS
	hapd->radius = radius_client_init(hapd, conf->radius);
	if (hapd->radius == NULL) {
		wpa_printf(MSG_ERROR, "RADIUS client initialization failed.");
		return -1;
	}
#endif /* CONFIG_NO_RADIUS */

	if (hostapd_acl_init(hapd)) {
		wpa_printf(MSG_ERROR, "ACL initialization failed.");
		return -1;
	}
	if (hostapd_init_wps(hapd, conf))
		return -1;

	if (authsrv_init(hapd) < 0)
		return -1;

	if (ieee802_1x_init(hapd)) {
		wpa_printf(MSG_ERROR, "IEEE 802.1X initialization failed.");
		return -1;
	}

	if (hapd->conf->wpa && hostapd_setup_wpa(hapd))
		return -1;

	if (accounting_init(hapd)) {
		wpa_printf(MSG_ERROR, "Accounting initialization failed.");
		return -1;
	}

	if (hapd->conf->ieee802_11f &&
	    (hapd->iapp = iapp_init(hapd, hapd->conf->iapp_iface)) == NULL) {
		wpa_printf(MSG_ERROR, "IEEE 802.11F (IAPP) initialization "
			   "failed.");
		return -1;
	}

	if (hapd->iface->ctrl_iface_init &&
	    hapd->iface->ctrl_iface_init(hapd)) {
		wpa_printf(MSG_ERROR, "Failed to setup control interface");
		return -1;
	}

	if (!hostapd_drv_none(hapd) && vlan_init(hapd)) {
		wpa_printf(MSG_ERROR, "VLAN initialization failed.");
		return -1;
	}

	ieee802_11_set_beacon(hapd);

	if (hapd->driver && hapd->driver->set_operstate)
		hapd->driver->set_operstate(hapd->drv_priv, 1);

	return 0;
}


static void hostapd_tx_queue_params(struct hostapd_iface *iface)
{
	struct hostapd_data *hapd = iface->bss[0];
	int i;
	struct hostapd_tx_queue_params *p;

	for (i = 0; i < NUM_TX_QUEUES; i++) {
		p = &iface->conf->tx_queue[i];

		if (hostapd_set_tx_queue_params(hapd, i, p->aifs, p->cwmin,
						p->cwmax, p->burst)) {
			wpa_printf(MSG_DEBUG, "Failed to set TX queue "
				   "parameters for queue %d.", i);
			/* Continue anyway */
		}
	}
}


static int setup_interface(struct hostapd_iface *iface)
{
	struct hostapd_data *hapd = iface->bss[0];
	size_t i;
	char country[4];

	/*
	 * Make sure that all BSSes get configured with a pointer to the same
	 * driver interface.
	 */
	for (i = 1; i < iface->num_bss; i++) {
		iface->bss[i]->driver = hapd->driver;
		iface->bss[i]->drv_priv = hapd->drv_priv;
	}

	if (hostapd_validate_bssid_configuration(iface))
		return -1;

	if (hapd->iconf->country[0] && hapd->iconf->country[1]) {
		os_memcpy(country, hapd->iconf->country, 3);
		country[3] = '\0';
		if (hostapd_set_country(hapd, country) < 0) {
			wpa_printf(MSG_ERROR, "Failed to set country code");
			return -1;
		}
	}

	if (hostapd_get_hw_features(iface)) {
		/* Not all drivers support this yet, so continue without hw
		 * feature data. */
	} else {
		int ret = hostapd_select_hw_mode(iface);
		if (ret < 0) {
			wpa_printf(MSG_ERROR, "Could not select hw_mode and "
				   "channel. (%d)", ret);
			return -1;
		}
		ret = hostapd_check_ht_capab(iface);
		if (ret < 0)
			return -1;
		if (ret == 1) {
			wpa_printf(MSG_DEBUG, "Interface initialization will "
				   "be completed in a callback");
			return 0;
		}
	}
	return hostapd_setup_interface_complete(iface, 0);
}


int hostapd_setup_interface_complete(struct hostapd_iface *iface, int err)
{
	struct hostapd_data *hapd = iface->bss[0];
	size_t j;
	u8 *prev_addr;

	if (err) {
		wpa_printf(MSG_ERROR, "Interface initialization failed");
		eloop_terminate();
		return -1;
	}

	wpa_printf(MSG_DEBUG, "Completing interface initialization");
	if (hapd->iconf->channel) {
		iface->freq = hostapd_hw_get_freq(hapd, hapd->iconf->channel);
		wpa_printf(MSG_DEBUG, "Mode: %s  Channel: %d  "
			   "Frequency: %d MHz",
			   hostapd_hw_mode_txt(hapd->iconf->hw_mode),
			   hapd->iconf->channel, iface->freq);

		if (hostapd_set_freq(hapd, hapd->iconf->hw_mode, iface->freq,
				     hapd->iconf->channel,
				     hapd->iconf->ieee80211n,
				     hapd->iconf->secondary_channel)) {
			wpa_printf(MSG_ERROR, "Could not set channel for "
				   "kernel driver");
			return -1;
		}
	}

	if (iface->current_mode) {
		if (hostapd_prepare_rates(hapd, iface->current_mode)) {
			wpa_printf(MSG_ERROR, "Failed to prepare rates "
				   "table.");
			hostapd_logger(hapd, NULL, HOSTAPD_MODULE_IEEE80211,
				       HOSTAPD_LEVEL_WARNING,
				       "Failed to prepare rates table.");
			return -1;
		}
	}

	if (hapd->iconf->rts_threshold > -1 &&
	    hostapd_set_rts(hapd, hapd->iconf->rts_threshold)) {
		wpa_printf(MSG_ERROR, "Could not set RTS threshold for "
			   "kernel driver");
		return -1;
	}

	if (hapd->iconf->fragm_threshold > -1 &&
	    hostapd_set_frag(hapd, hapd->iconf->fragm_threshold)) {
		wpa_printf(MSG_ERROR, "Could not set fragmentation threshold "
			   "for kernel driver");
		return -1;
	}

	prev_addr = hapd->own_addr;

	for (j = 0; j < iface->num_bss; j++) {
		hapd = iface->bss[j];
		if (j)
			os_memcpy(hapd->own_addr, prev_addr, ETH_ALEN);
		if (hostapd_setup_bss(hapd, j == 0))
			return -1;
		if (hostapd_mac_comp_empty(hapd->conf->bssid) == 0)
			prev_addr = hapd->own_addr;
	}

	hostapd_tx_queue_params(iface);

	ap_list_init(iface);

	if (hostapd_driver_commit(hapd) < 0) {
		wpa_printf(MSG_ERROR, "%s: Failed to commit driver "
			   "configuration", __func__);
		return -1;
	}

	if (hapd->setup_complete_cb)
		hapd->setup_complete_cb(hapd->setup_complete_cb_ctx);

	wpa_printf(MSG_DEBUG, "%s: Setup of interface done.",
		   iface->bss[0]->conf->iface);

	return 0;
}


/**
 * hostapd_setup_interface - Setup of an interface
 * @iface: Pointer to interface data.
 * Returns: 0 on success, -1 on failure
 *
 * Initializes the driver interface, validates the configuration,
 * and sets driver parameters based on the configuration.
 * Flushes old stations, sets the channel, encryption,
 * beacons, and WDS links based on the configuration.
 */
int hostapd_setup_interface(struct hostapd_iface *iface)
{
	int ret;

	ret = setup_interface(iface);
	if (ret) {
		wpa_printf(MSG_ERROR, "%s: Unable to setup interface.",
			   iface->bss[0]->conf->iface);
		return -1;
	}

	return 0;
}


/**
 * hostapd_alloc_bss_data - Allocate and initialize per-BSS data
 * @hapd_iface: Pointer to interface data
 * @conf: Pointer to per-interface configuration
 * @bss: Pointer to per-BSS configuration for this BSS
 * Returns: Pointer to allocated BSS data
 *
 * This function is used to allocate per-BSS data structure. This data will be
 * freed after hostapd_cleanup() is called for it during interface
 * deinitialization.
 */
struct hostapd_data *
hostapd_alloc_bss_data(struct hostapd_iface *hapd_iface,
		       struct hostapd_config *conf,
		       struct hostapd_bss_config *bss)
{
	struct hostapd_data *hapd;

	hapd = os_zalloc(sizeof(*hapd));
	if (hapd == NULL)
		return NULL;

	hapd->new_assoc_sta_cb = hostapd_new_assoc_sta;
	hapd->iconf = conf;
	hapd->conf = bss;
	hapd->iface = hapd_iface;
	hapd->driver = hapd->iconf->driver;

	return hapd;
}


void hostapd_interface_deinit(struct hostapd_iface *iface)
{
	size_t j;

	if (iface == NULL)
		return;

	hostapd_cleanup_iface_pre(iface);
	for (j = 0; j < iface->num_bss; j++) {
		struct hostapd_data *hapd = iface->bss[j];
		hostapd_free_stas(hapd);
		hostapd_flush_old_stations(hapd);
		hostapd_cleanup(hapd);
	}
}


void hostapd_interface_free(struct hostapd_iface *iface)
{
	size_t j;
	for (j = 0; j < iface->num_bss; j++)
		os_free(iface->bss[j]);
	hostapd_cleanup_iface(iface);
}


/**
 * hostapd_new_assoc_sta - Notify that a new station associated with the AP
 * @hapd: Pointer to BSS data
 * @sta: Pointer to the associated STA data
 * @reassoc: 1 to indicate this was a re-association; 0 = first association
 *
 * This function will be called whenever a station associates with the AP. It
 * can be called from ieee802_11.c for drivers that export MLME to hostapd and
 * from drv_callbacks.c based on driver events for drivers that take care of
 * management frames (IEEE 802.11 authentication and association) internally.
 */
void hostapd_new_assoc_sta(struct hostapd_data *hapd, struct sta_info *sta,
			   int reassoc)
{
	if (hapd->tkip_countermeasures) {
		hostapd_drv_sta_deauth(hapd, sta->addr,
				       WLAN_REASON_MICHAEL_MIC_FAILURE);
		return;
	}

	hostapd_prune_associations(hapd, sta->addr);

	/* IEEE 802.11F (IAPP) */
	if (hapd->conf->ieee802_11f)
		iapp_new_station(hapd->iapp, sta);

#ifdef CONFIG_P2P
	if (sta->p2p_ie == NULL && !sta->no_p2p_set) {
		sta->no_p2p_set = 1;
		hapd->num_sta_no_p2p++;
		if (hapd->num_sta_no_p2p == 1)
			hostapd_p2p_non_p2p_sta_connected(hapd);
	}
#endif /* CONFIG_P2P */

	/* Start accounting here, if IEEE 802.1X and WPA are not used.
	 * IEEE 802.1X/WPA code will start accounting after the station has
	 * been authorized. */
	if (!hapd->conf->ieee802_1x && !hapd->conf->wpa)
		accounting_sta_start(hapd, sta);

	/* Start IEEE 802.1X authentication process for new stations */
	ieee802_1x_new_station(hapd, sta);
	if (reassoc) {
		if (sta->auth_alg != WLAN_AUTH_FT &&
		    !(sta->flags & (WLAN_STA_WPS | WLAN_STA_MAYBE_WPS)))
			wpa_auth_sm_event(sta->wpa_sm, WPA_REAUTH);
	} else
		wpa_auth_sta_associated(hapd->wpa_auth, sta->wpa_sm);
}
