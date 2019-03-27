/*
 * wpa_supplicant / WPS integration
 * Copyright (c) 2008-2014, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "eloop.h"
#include "uuid.h"
#include "crypto/random.h"
#include "crypto/dh_group5.h"
#include "common/ieee802_11_defs.h"
#include "common/ieee802_11_common.h"
#include "common/wpa_common.h"
#include "common/wpa_ctrl.h"
#include "eap_common/eap_wsc_common.h"
#include "eap_peer/eap.h"
#include "eapol_supp/eapol_supp_sm.h"
#include "rsn_supp/wpa.h"
#include "wps/wps_attr_parse.h"
#include "config.h"
#include "wpa_supplicant_i.h"
#include "driver_i.h"
#include "notify.h"
#include "blacklist.h"
#include "bss.h"
#include "scan.h"
#include "ap.h"
#include "p2p/p2p.h"
#include "p2p_supplicant.h"
#include "wps_supplicant.h"


#ifndef WPS_PIN_SCAN_IGNORE_SEL_REG
#define WPS_PIN_SCAN_IGNORE_SEL_REG 3
#endif /* WPS_PIN_SCAN_IGNORE_SEL_REG */

/*
 * The minimum time in seconds before trying to associate to a WPS PIN AP that
 * does not have Selected Registrar TRUE.
 */
#ifndef WPS_PIN_TIME_IGNORE_SEL_REG
#define WPS_PIN_TIME_IGNORE_SEL_REG 5
#endif /* WPS_PIN_TIME_IGNORE_SEL_REG */

static void wpas_wps_timeout(void *eloop_ctx, void *timeout_ctx);
static void wpas_clear_wps(struct wpa_supplicant *wpa_s);


static void wpas_wps_clear_ap_info(struct wpa_supplicant *wpa_s)
{
	os_free(wpa_s->wps_ap);
	wpa_s->wps_ap = NULL;
	wpa_s->num_wps_ap = 0;
	wpa_s->wps_ap_iter = 0;
}


static void wpas_wps_assoc_with_cred(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;
	int use_fast_assoc = timeout_ctx != NULL;

	wpa_printf(MSG_DEBUG, "WPS: Continuing association after eapol_cb");
	if (!use_fast_assoc ||
	    wpa_supplicant_fast_associate(wpa_s) != 1)
		wpa_supplicant_req_scan(wpa_s, 0, 0);
}


static void wpas_wps_assoc_with_cred_cancel(struct wpa_supplicant *wpa_s)
{
	eloop_cancel_timeout(wpas_wps_assoc_with_cred, wpa_s, (void *) 0);
	eloop_cancel_timeout(wpas_wps_assoc_with_cred, wpa_s, (void *) 1);
}


int wpas_wps_eapol_cb(struct wpa_supplicant *wpa_s)
{
	if (wpas_p2p_wps_eapol_cb(wpa_s) > 0)
		return 1;

	if (!wpa_s->wps_success &&
	    wpa_s->current_ssid &&
	    eap_is_wps_pin_enrollee(&wpa_s->current_ssid->eap)) {
		const u8 *bssid = wpa_s->bssid;
		if (is_zero_ether_addr(bssid))
			bssid = wpa_s->pending_bssid;

		wpa_printf(MSG_DEBUG, "WPS: PIN registration with " MACSTR
			   " did not succeed - continue trying to find "
			   "suitable AP", MAC2STR(bssid));
		wpa_blacklist_add(wpa_s, bssid);

		wpa_supplicant_deauthenticate(wpa_s,
					      WLAN_REASON_DEAUTH_LEAVING);
		wpa_s->reassociate = 1;
		wpa_supplicant_req_scan(wpa_s,
					wpa_s->blacklist_cleared ? 5 : 0, 0);
		wpa_s->blacklist_cleared = 0;
		return 1;
	}

	wpas_wps_clear_ap_info(wpa_s);
	eloop_cancel_timeout(wpas_wps_timeout, wpa_s, NULL);
	if (wpa_s->key_mgmt == WPA_KEY_MGMT_WPS && !wpa_s->wps_success)
		wpa_msg(wpa_s, MSG_INFO, WPS_EVENT_FAIL);

	if (wpa_s->key_mgmt == WPA_KEY_MGMT_WPS && wpa_s->current_ssid &&
	    !(wpa_s->current_ssid->key_mgmt & WPA_KEY_MGMT_WPS)) {
		int disabled = wpa_s->current_ssid->disabled;
		unsigned int freq = wpa_s->assoc_freq;
		struct wpa_bss *bss;
		struct wpa_ssid *ssid = NULL;
		int use_fast_assoc = 0;

		wpa_printf(MSG_DEBUG, "WPS: Network configuration replaced - "
			   "try to associate with the received credential "
			   "(freq=%u)", freq);
		wpa_s->own_disconnect_req = 1;
		wpa_supplicant_deauthenticate(wpa_s,
					      WLAN_REASON_DEAUTH_LEAVING);
		if (disabled) {
			wpa_printf(MSG_DEBUG, "WPS: Current network is "
				   "disabled - wait for user to enable");
			return 1;
		}
		wpa_s->after_wps = 5;
		wpa_s->wps_freq = freq;
		wpa_s->normal_scans = 0;
		wpa_s->reassociate = 1;

		wpa_printf(MSG_DEBUG, "WPS: Checking whether fast association "
			   "without a new scan can be used");
		bss = wpa_supplicant_pick_network(wpa_s, &ssid);
		if (bss) {
			struct wpabuf *wps;
			struct wps_parse_attr attr;

			wps = wpa_bss_get_vendor_ie_multi(bss,
							  WPS_IE_VENDOR_TYPE);
			if (wps && wps_parse_msg(wps, &attr) == 0 &&
			    attr.wps_state &&
			    *attr.wps_state == WPS_STATE_CONFIGURED)
				use_fast_assoc = 1;
			wpabuf_free(wps);
		}

		/*
		 * Complete the next step from an eloop timeout to allow pending
		 * driver events related to the disconnection to be processed
		 * first. This makes it less likely for disconnection event to
		 * cause problems with the following connection.
		 */
		wpa_printf(MSG_DEBUG, "WPS: Continue association from timeout");
		wpas_wps_assoc_with_cred_cancel(wpa_s);
		eloop_register_timeout(0, 10000,
				       wpas_wps_assoc_with_cred, wpa_s,
				       use_fast_assoc ? (void *) 1 :
				       (void *) 0);
		return 1;
	}

	if (wpa_s->key_mgmt == WPA_KEY_MGMT_WPS && wpa_s->current_ssid) {
		wpa_printf(MSG_DEBUG, "WPS: Registration completed - waiting "
			   "for external credential processing");
		wpas_clear_wps(wpa_s);
		wpa_s->own_disconnect_req = 1;
		wpa_supplicant_deauthenticate(wpa_s,
					      WLAN_REASON_DEAUTH_LEAVING);
		return 1;
	}

	return 0;
}


static void wpas_wps_security_workaround(struct wpa_supplicant *wpa_s,
					 struct wpa_ssid *ssid,
					 const struct wps_credential *cred)
{
	struct wpa_driver_capa capa;
	struct wpa_bss *bss;
	const u8 *ie;
	struct wpa_ie_data adv;
	int wpa2 = 0, ccmp = 0;

	/*
	 * Many existing WPS APs do not know how to negotiate WPA2 or CCMP in
	 * case they are configured for mixed mode operation (WPA+WPA2 and
	 * TKIP+CCMP). Try to use scan results to figure out whether the AP
	 * actually supports stronger security and select that if the client
	 * has support for it, too.
	 */

	if (wpa_drv_get_capa(wpa_s, &capa))
		return; /* Unknown what driver supports */

	if (ssid->ssid == NULL)
		return;
	bss = wpa_bss_get(wpa_s, cred->mac_addr, ssid->ssid, ssid->ssid_len);
	if (!bss)
		bss = wpa_bss_get(wpa_s, wpa_s->bssid,
				  ssid->ssid, ssid->ssid_len);
	if (bss == NULL) {
		wpa_printf(MSG_DEBUG, "WPS: The AP was not found from BSS "
			   "table - use credential as-is");
		return;
	}

	wpa_printf(MSG_DEBUG, "WPS: AP found from BSS table");

	ie = wpa_bss_get_ie(bss, WLAN_EID_RSN);
	if (ie && wpa_parse_wpa_ie(ie, 2 + ie[1], &adv) == 0) {
		wpa2 = 1;
		if (adv.pairwise_cipher & WPA_CIPHER_CCMP)
			ccmp = 1;
	} else {
		ie = wpa_bss_get_vendor_ie(bss, WPA_IE_VENDOR_TYPE);
		if (ie && wpa_parse_wpa_ie(ie, 2 + ie[1], &adv) == 0 &&
		    adv.pairwise_cipher & WPA_CIPHER_CCMP)
			ccmp = 1;
	}

	if (ie == NULL && (ssid->proto & WPA_PROTO_WPA) &&
	    (ssid->pairwise_cipher & WPA_CIPHER_TKIP)) {
		/*
		 * TODO: This could be the initial AP configuration and the
		 * Beacon contents could change shortly. Should request a new
		 * scan and delay addition of the network until the updated
		 * scan results are available.
		 */
		wpa_printf(MSG_DEBUG, "WPS: The AP did not yet advertise WPA "
			   "support - use credential as-is");
		return;
	}

	if (ccmp && !(ssid->pairwise_cipher & WPA_CIPHER_CCMP) &&
	    (ssid->pairwise_cipher & WPA_CIPHER_TKIP) &&
	    (capa.key_mgmt & WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK)) {
		wpa_printf(MSG_DEBUG, "WPS: Add CCMP into the credential "
			   "based on scan results");
		if (wpa_s->conf->ap_scan == 1)
			ssid->pairwise_cipher |= WPA_CIPHER_CCMP;
		else
			ssid->pairwise_cipher = WPA_CIPHER_CCMP;
	}

	if (wpa2 && !(ssid->proto & WPA_PROTO_RSN) &&
	    (ssid->proto & WPA_PROTO_WPA) &&
	    (capa.enc & WPA_DRIVER_CAPA_ENC_CCMP)) {
		wpa_printf(MSG_DEBUG, "WPS: Add WPA2 into the credential "
			   "based on scan results");
		if (wpa_s->conf->ap_scan == 1)
			ssid->proto |= WPA_PROTO_RSN;
		else
			ssid->proto = WPA_PROTO_RSN;
	}
}


static void wpas_wps_remove_dup_network(struct wpa_supplicant *wpa_s,
					struct wpa_ssid *new_ssid)
{
	struct wpa_ssid *ssid, *next;

	for (ssid = wpa_s->conf->ssid, next = ssid ? ssid->next : NULL; ssid;
	     ssid = next, next = ssid ? ssid->next : NULL) {
		/*
		 * new_ssid has already been added to the list in
		 * wpas_wps_add_network(), so skip it.
		 */
		if (ssid == new_ssid)
			continue;

		if (ssid->bssid_set || new_ssid->bssid_set) {
			if (ssid->bssid_set != new_ssid->bssid_set)
				continue;
			if (os_memcmp(ssid->bssid, new_ssid->bssid, ETH_ALEN) !=
			    0)
				continue;
		}

		/* compare SSID */
		if (ssid->ssid_len == 0 || ssid->ssid_len != new_ssid->ssid_len)
			continue;

		if (ssid->ssid && new_ssid->ssid) {
			if (os_memcmp(ssid->ssid, new_ssid->ssid,
				      ssid->ssid_len) != 0)
				continue;
		} else if (ssid->ssid || new_ssid->ssid)
			continue;

		/* compare security parameters */
		if (ssid->auth_alg != new_ssid->auth_alg ||
		    ssid->key_mgmt != new_ssid->key_mgmt ||
		    (ssid->group_cipher != new_ssid->group_cipher &&
		     !(ssid->group_cipher & new_ssid->group_cipher &
		       WPA_CIPHER_CCMP)))
			continue;

		/*
		 * Some existing WPS APs will send two creds in case they are
		 * configured for mixed mode operation (WPA+WPA2 and TKIP+CCMP).
		 * Try to merge these two creds if they are received in the same
		 * M8 message.
		 */
		if (ssid->wps_run && ssid->wps_run == new_ssid->wps_run &&
		    wpa_key_mgmt_wpa_psk(ssid->key_mgmt)) {
			if (new_ssid->passphrase && ssid->passphrase &&
			    os_strcmp(new_ssid->passphrase, ssid->passphrase) !=
			    0) {
				wpa_printf(MSG_DEBUG,
					   "WPS: M8 Creds with different passphrase - do not merge");
				continue;
			}

			if (new_ssid->psk_set &&
			    (!ssid->psk_set ||
			     os_memcmp(new_ssid->psk, ssid->psk, 32) != 0)) {
				wpa_printf(MSG_DEBUG,
					   "WPS: M8 Creds with different PSK - do not merge");
				continue;
			}

			if ((new_ssid->passphrase && !ssid->passphrase) ||
			    (!new_ssid->passphrase && ssid->passphrase)) {
				wpa_printf(MSG_DEBUG,
					   "WPS: M8 Creds with different passphrase/PSK type - do not merge");
				continue;
			}

			wpa_printf(MSG_DEBUG,
				   "WPS: Workaround - merge likely WPA/WPA2-mixed mode creds in same M8 message");
			new_ssid->proto |= ssid->proto;
			new_ssid->pairwise_cipher |= ssid->pairwise_cipher;
		} else {
			/*
			 * proto and pairwise_cipher difference matter for
			 * non-mixed-mode creds.
			 */
			if (ssid->proto != new_ssid->proto ||
			    ssid->pairwise_cipher != new_ssid->pairwise_cipher)
				continue;
		}

		/* Remove the duplicated older network entry. */
		wpa_printf(MSG_DEBUG, "Remove duplicate network %d", ssid->id);
		wpas_notify_network_removed(wpa_s, ssid);
		if (wpa_s->current_ssid == ssid)
			wpa_s->current_ssid = NULL;
		wpa_config_remove_network(wpa_s->conf, ssid->id);
	}
}


static int wpa_supplicant_wps_cred(void *ctx,
				   const struct wps_credential *cred)
{
	struct wpa_supplicant *wpa_s = ctx;
	struct wpa_ssid *ssid = wpa_s->current_ssid;
	u16 auth_type;
#ifdef CONFIG_WPS_REG_DISABLE_OPEN
	int registrar = 0;
#endif /* CONFIG_WPS_REG_DISABLE_OPEN */

	if ((wpa_s->conf->wps_cred_processing == 1 ||
	     wpa_s->conf->wps_cred_processing == 2) && cred->cred_attr) {
		size_t blen = cred->cred_attr_len * 2 + 1;
		char *buf = os_malloc(blen);
		if (buf) {
			wpa_snprintf_hex(buf, blen,
					 cred->cred_attr, cred->cred_attr_len);
			wpa_msg(wpa_s, MSG_INFO, "%s%s",
				WPS_EVENT_CRED_RECEIVED, buf);
			os_free(buf);
		}

		wpas_notify_wps_credential(wpa_s, cred);
	} else
		wpa_msg(wpa_s, MSG_INFO, WPS_EVENT_CRED_RECEIVED);

	wpa_hexdump_key(MSG_DEBUG, "WPS: Received Credential attribute",
			cred->cred_attr, cred->cred_attr_len);

	if (wpa_s->conf->wps_cred_processing == 1)
		return 0;

	wpa_hexdump_ascii(MSG_DEBUG, "WPS: SSID", cred->ssid, cred->ssid_len);
	wpa_printf(MSG_DEBUG, "WPS: Authentication Type 0x%x",
		   cred->auth_type);
	wpa_printf(MSG_DEBUG, "WPS: Encryption Type 0x%x", cred->encr_type);
	wpa_printf(MSG_DEBUG, "WPS: Network Key Index %d", cred->key_idx);
	wpa_hexdump_key(MSG_DEBUG, "WPS: Network Key",
			cred->key, cred->key_len);
	wpa_printf(MSG_DEBUG, "WPS: MAC Address " MACSTR,
		   MAC2STR(cred->mac_addr));

	auth_type = cred->auth_type;
	if (auth_type == (WPS_AUTH_WPAPSK | WPS_AUTH_WPA2PSK)) {
		wpa_printf(MSG_DEBUG, "WPS: Workaround - convert mixed-mode "
			   "auth_type into WPA2PSK");
		auth_type = WPS_AUTH_WPA2PSK;
	}

	if (auth_type != WPS_AUTH_OPEN &&
	    auth_type != WPS_AUTH_WPAPSK &&
	    auth_type != WPS_AUTH_WPA2PSK) {
		wpa_printf(MSG_DEBUG, "WPS: Ignored credentials for "
			   "unsupported authentication type 0x%x",
			   auth_type);
		return 0;
	}

	if (auth_type == WPS_AUTH_WPAPSK || auth_type == WPS_AUTH_WPA2PSK) {
		if (cred->key_len < 8 || cred->key_len > 2 * PMK_LEN) {
			wpa_printf(MSG_ERROR, "WPS: Reject PSK credential with "
				   "invalid Network Key length %lu",
				   (unsigned long) cred->key_len);
			return -1;
		}
	}

	if (ssid && (ssid->key_mgmt & WPA_KEY_MGMT_WPS)) {
		wpa_printf(MSG_DEBUG, "WPS: Replace WPS network block based "
			   "on the received credential");
#ifdef CONFIG_WPS_REG_DISABLE_OPEN
		if (ssid->eap.identity &&
		    ssid->eap.identity_len == WSC_ID_REGISTRAR_LEN &&
		    os_memcmp(ssid->eap.identity, WSC_ID_REGISTRAR,
			      WSC_ID_REGISTRAR_LEN) == 0)
			registrar = 1;
#endif /* CONFIG_WPS_REG_DISABLE_OPEN */
		os_free(ssid->eap.identity);
		ssid->eap.identity = NULL;
		ssid->eap.identity_len = 0;
		os_free(ssid->eap.phase1);
		ssid->eap.phase1 = NULL;
		os_free(ssid->eap.eap_methods);
		ssid->eap.eap_methods = NULL;
		if (!ssid->p2p_group) {
			ssid->temporary = 0;
			ssid->bssid_set = 0;
		}
		ssid->disabled_until.sec = 0;
		ssid->disabled_until.usec = 0;
		ssid->auth_failures = 0;
	} else {
		wpa_printf(MSG_DEBUG, "WPS: Create a new network based on the "
			   "received credential");
		ssid = wpa_config_add_network(wpa_s->conf);
		if (ssid == NULL)
			return -1;
		if (wpa_s->current_ssid) {
			/*
			 * Should the GO issue multiple credentials for some
			 * reason, each credential should be marked as a
			 * temporary P2P group similarly to the one that gets
			 * marked as such based on the pre-configured values
			 * used for the WPS network block.
			 */
			ssid->p2p_group = wpa_s->current_ssid->p2p_group;
			ssid->temporary = wpa_s->current_ssid->temporary;
		}
		wpas_notify_network_added(wpa_s, ssid);
	}

	wpa_config_set_network_defaults(ssid);
	ssid->wps_run = wpa_s->wps_run;

	os_free(ssid->ssid);
	ssid->ssid = os_malloc(cred->ssid_len);
	if (ssid->ssid) {
		os_memcpy(ssid->ssid, cred->ssid, cred->ssid_len);
		ssid->ssid_len = cred->ssid_len;
	}

	switch (cred->encr_type) {
	case WPS_ENCR_NONE:
		break;
	case WPS_ENCR_TKIP:
		ssid->pairwise_cipher = WPA_CIPHER_TKIP;
		break;
	case WPS_ENCR_AES:
		ssid->pairwise_cipher = WPA_CIPHER_CCMP;
		if (wpa_s->drv_capa_known &&
		    (wpa_s->drv_enc & WPA_DRIVER_CAPA_ENC_GCMP)) {
			ssid->pairwise_cipher |= WPA_CIPHER_GCMP;
			ssid->group_cipher |= WPA_CIPHER_GCMP;
		}
		if (wpa_s->drv_capa_known &&
		    (wpa_s->drv_enc & WPA_DRIVER_CAPA_ENC_GCMP_256)) {
			ssid->pairwise_cipher |= WPA_CIPHER_GCMP_256;
			ssid->group_cipher |= WPA_CIPHER_GCMP_256;
		}
		if (wpa_s->drv_capa_known &&
		    (wpa_s->drv_enc & WPA_DRIVER_CAPA_ENC_CCMP_256)) {
			ssid->pairwise_cipher |= WPA_CIPHER_CCMP_256;
			ssid->group_cipher |= WPA_CIPHER_CCMP_256;
		}
		break;
	}

	switch (auth_type) {
	case WPS_AUTH_OPEN:
		ssid->auth_alg = WPA_AUTH_ALG_OPEN;
		ssid->key_mgmt = WPA_KEY_MGMT_NONE;
		ssid->proto = 0;
#ifdef CONFIG_WPS_REG_DISABLE_OPEN
		if (registrar) {
			wpa_msg(wpa_s, MSG_INFO, WPS_EVENT_OPEN_NETWORK
				"id=%d - Credentials for an open "
				"network disabled by default - use "
				"'select_network %d' to enable",
				ssid->id, ssid->id);
			ssid->disabled = 1;
		}
#endif /* CONFIG_WPS_REG_DISABLE_OPEN */
		break;
	case WPS_AUTH_WPAPSK:
		ssid->auth_alg = WPA_AUTH_ALG_OPEN;
		ssid->key_mgmt = WPA_KEY_MGMT_PSK;
		ssid->proto = WPA_PROTO_WPA;
		break;
	case WPS_AUTH_WPA2PSK:
		ssid->auth_alg = WPA_AUTH_ALG_OPEN;
		ssid->key_mgmt = WPA_KEY_MGMT_PSK;
		ssid->proto = WPA_PROTO_RSN;
		break;
	}

	if (ssid->key_mgmt == WPA_KEY_MGMT_PSK) {
		if (cred->key_len == 2 * PMK_LEN) {
			if (hexstr2bin((const char *) cred->key, ssid->psk,
				       PMK_LEN)) {
				wpa_printf(MSG_ERROR, "WPS: Invalid Network "
					   "Key");
				return -1;
			}
			ssid->psk_set = 1;
			ssid->export_keys = 1;
		} else if (cred->key_len >= 8 && cred->key_len < 2 * PMK_LEN) {
			os_free(ssid->passphrase);
			ssid->passphrase = os_malloc(cred->key_len + 1);
			if (ssid->passphrase == NULL)
				return -1;
			os_memcpy(ssid->passphrase, cred->key, cred->key_len);
			ssid->passphrase[cred->key_len] = '\0';
			wpa_config_update_psk(ssid);
			ssid->export_keys = 1;
		} else {
			wpa_printf(MSG_ERROR, "WPS: Invalid Network Key "
				   "length %lu",
				   (unsigned long) cred->key_len);
			return -1;
		}
	}
	ssid->priority = wpa_s->conf->wps_priority;

	wpas_wps_security_workaround(wpa_s, ssid, cred);

	wpas_wps_remove_dup_network(wpa_s, ssid);

#ifndef CONFIG_NO_CONFIG_WRITE
	if (wpa_s->conf->update_config &&
	    wpa_config_write(wpa_s->confname, wpa_s->conf)) {
		wpa_printf(MSG_DEBUG, "WPS: Failed to update configuration");
		return -1;
	}
#endif /* CONFIG_NO_CONFIG_WRITE */

	if (ssid->priority)
		wpa_config_update_prio_list(wpa_s->conf);

	/*
	 * Optimize the post-WPS scan based on the channel used during
	 * the provisioning in case EAP-Failure is not received.
	 */
	wpa_s->after_wps = 5;
	wpa_s->wps_freq = wpa_s->assoc_freq;

	return 0;
}


static void wpa_supplicant_wps_event_m2d(struct wpa_supplicant *wpa_s,
					 struct wps_event_m2d *m2d)
{
	wpa_msg(wpa_s, MSG_INFO, WPS_EVENT_M2D
		"dev_password_id=%d config_error=%d",
		m2d->dev_password_id, m2d->config_error);
	wpas_notify_wps_event_m2d(wpa_s, m2d);
#ifdef CONFIG_P2P
	if (wpa_s->p2pdev && wpa_s->p2pdev != wpa_s) {
		wpa_msg(wpa_s->p2pdev, MSG_INFO, WPS_EVENT_M2D
			"dev_password_id=%d config_error=%d",
			m2d->dev_password_id, m2d->config_error);
	}
	if (m2d->config_error == WPS_CFG_MULTIPLE_PBC_DETECTED) {
		/*
		 * Notify P2P from eloop timeout to avoid issues with the
		 * interface getting removed while processing a message.
		 */
		eloop_register_timeout(0, 0, wpas_p2p_pbc_overlap_cb, wpa_s,
				       NULL);
	}
#endif /* CONFIG_P2P */
}


static void wpas_wps_clear_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;
	wpa_printf(MSG_DEBUG, "WPS: Clear WPS network from timeout");
	wpas_clear_wps(wpa_s);
}


static void wpa_supplicant_wps_event_fail(struct wpa_supplicant *wpa_s,
					  struct wps_event_fail *fail)
{
	if (fail->error_indication > 0 &&
	    fail->error_indication < NUM_WPS_EI_VALUES) {
		wpa_msg(wpa_s, MSG_INFO,
			WPS_EVENT_FAIL "msg=%d config_error=%d reason=%d (%s)",
			fail->msg, fail->config_error, fail->error_indication,
			wps_ei_str(fail->error_indication));
		if (wpa_s->p2pdev && wpa_s->p2pdev != wpa_s)
			wpa_msg(wpa_s->p2pdev, MSG_INFO, WPS_EVENT_FAIL
				"msg=%d config_error=%d reason=%d (%s)",
				fail->msg, fail->config_error,
				fail->error_indication,
				wps_ei_str(fail->error_indication));
	} else {
		wpa_msg(wpa_s, MSG_INFO,
			WPS_EVENT_FAIL "msg=%d config_error=%d",
			fail->msg, fail->config_error);
		if (wpa_s->p2pdev && wpa_s->p2pdev != wpa_s)
			wpa_msg(wpa_s->p2pdev, MSG_INFO, WPS_EVENT_FAIL
				"msg=%d config_error=%d",
				fail->msg, fail->config_error);
	}

	/*
	 * Need to allow WPS processing to complete, e.g., by sending WSC_NACK.
	 */
	wpa_printf(MSG_DEBUG, "WPS: Register timeout to clear WPS network");
	eloop_cancel_timeout(wpas_wps_clear_timeout, wpa_s, NULL);
	eloop_register_timeout(0, 100000, wpas_wps_clear_timeout, wpa_s, NULL);

	wpas_notify_wps_event_fail(wpa_s, fail);
	wpas_p2p_wps_failed(wpa_s, fail);
}


static void wpas_wps_reenable_networks_cb(void *eloop_ctx, void *timeout_ctx);

static void wpas_wps_reenable_networks(struct wpa_supplicant *wpa_s)
{
	struct wpa_ssid *ssid;
	int changed = 0;

	eloop_cancel_timeout(wpas_wps_reenable_networks_cb, wpa_s, NULL);

	for (ssid = wpa_s->conf->ssid; ssid; ssid = ssid->next) {
		if (ssid->disabled_for_connect && ssid->disabled) {
			ssid->disabled_for_connect = 0;
			ssid->disabled = 0;
			wpas_notify_network_enabled_changed(wpa_s, ssid);
			changed++;
		}
	}

	if (changed) {
#ifndef CONFIG_NO_CONFIG_WRITE
		if (wpa_s->conf->update_config &&
		    wpa_config_write(wpa_s->confname, wpa_s->conf)) {
			wpa_printf(MSG_DEBUG, "WPS: Failed to update "
				   "configuration");
		}
#endif /* CONFIG_NO_CONFIG_WRITE */
	}
}


static void wpas_wps_reenable_networks_cb(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;
	/* Enable the networks disabled during wpas_wps_reassoc */
	wpas_wps_reenable_networks(wpa_s);
}


int wpas_wps_reenable_networks_pending(struct wpa_supplicant *wpa_s)
{
	return eloop_is_timeout_registered(wpas_wps_reenable_networks_cb,
					   wpa_s, NULL);
}


static void wpa_supplicant_wps_event_success(struct wpa_supplicant *wpa_s)
{
	wpa_msg(wpa_s, MSG_INFO, WPS_EVENT_SUCCESS);
	wpa_s->wps_success = 1;
	wpas_notify_wps_event_success(wpa_s);
	if (wpa_s->current_ssid)
		wpas_clear_temp_disabled(wpa_s, wpa_s->current_ssid, 1);
	wpa_s->extra_blacklist_count = 0;

	/*
	 * Enable the networks disabled during wpas_wps_reassoc after 10
	 * seconds. The 10 seconds timer is to allow the data connection to be
	 * formed before allowing other networks to be selected.
	 */
	eloop_register_timeout(10, 0, wpas_wps_reenable_networks_cb, wpa_s,
			       NULL);

	wpas_p2p_wps_success(wpa_s, wpa_s->bssid, 0);
}


static void wpa_supplicant_wps_event_er_ap_add(struct wpa_supplicant *wpa_s,
					       struct wps_event_er_ap *ap)
{
	char uuid_str[100];
	char dev_type[WPS_DEV_TYPE_BUFSIZE];

	uuid_bin2str(ap->uuid, uuid_str, sizeof(uuid_str));
	if (ap->pri_dev_type)
		wps_dev_type_bin2str(ap->pri_dev_type, dev_type,
				     sizeof(dev_type));
	else
		dev_type[0] = '\0';

	wpa_msg(wpa_s, MSG_INFO, WPS_EVENT_ER_AP_ADD "%s " MACSTR
		" pri_dev_type=%s wps_state=%d |%s|%s|%s|%s|%s|%s|",
		uuid_str, MAC2STR(ap->mac_addr), dev_type, ap->wps_state,
		ap->friendly_name ? ap->friendly_name : "",
		ap->manufacturer ? ap->manufacturer : "",
		ap->model_description ? ap->model_description : "",
		ap->model_name ? ap->model_name : "",
		ap->manufacturer_url ? ap->manufacturer_url : "",
		ap->model_url ? ap->model_url : "");
}


static void wpa_supplicant_wps_event_er_ap_remove(struct wpa_supplicant *wpa_s,
						  struct wps_event_er_ap *ap)
{
	char uuid_str[100];
	uuid_bin2str(ap->uuid, uuid_str, sizeof(uuid_str));
	wpa_msg(wpa_s, MSG_INFO, WPS_EVENT_ER_AP_REMOVE "%s", uuid_str);
}


static void wpa_supplicant_wps_event_er_enrollee_add(
	struct wpa_supplicant *wpa_s, struct wps_event_er_enrollee *enrollee)
{
	char uuid_str[100];
	char dev_type[WPS_DEV_TYPE_BUFSIZE];

	uuid_bin2str(enrollee->uuid, uuid_str, sizeof(uuid_str));
	if (enrollee->pri_dev_type)
		wps_dev_type_bin2str(enrollee->pri_dev_type, dev_type,
				     sizeof(dev_type));
	else
		dev_type[0] = '\0';

	wpa_msg(wpa_s, MSG_INFO, WPS_EVENT_ER_ENROLLEE_ADD "%s " MACSTR
		" M1=%d config_methods=0x%x dev_passwd_id=%d pri_dev_type=%s "
		"|%s|%s|%s|%s|%s|",
		uuid_str, MAC2STR(enrollee->mac_addr), enrollee->m1_received,
		enrollee->config_methods, enrollee->dev_passwd_id, dev_type,
		enrollee->dev_name ? enrollee->dev_name : "",
		enrollee->manufacturer ? enrollee->manufacturer : "",
		enrollee->model_name ? enrollee->model_name : "",
		enrollee->model_number ? enrollee->model_number : "",
		enrollee->serial_number ? enrollee->serial_number : "");
}


static void wpa_supplicant_wps_event_er_enrollee_remove(
	struct wpa_supplicant *wpa_s, struct wps_event_er_enrollee *enrollee)
{
	char uuid_str[100];
	uuid_bin2str(enrollee->uuid, uuid_str, sizeof(uuid_str));
	wpa_msg(wpa_s, MSG_INFO, WPS_EVENT_ER_ENROLLEE_REMOVE "%s " MACSTR,
		uuid_str, MAC2STR(enrollee->mac_addr));
}


static void wpa_supplicant_wps_event_er_ap_settings(
	struct wpa_supplicant *wpa_s,
	struct wps_event_er_ap_settings *ap_settings)
{
	char uuid_str[100];
	char key_str[65];
	const struct wps_credential *cred = ap_settings->cred;

	key_str[0] = '\0';
	if (cred->auth_type & (WPS_AUTH_WPAPSK | WPS_AUTH_WPA2PSK)) {
		if (cred->key_len >= 8 && cred->key_len <= 64) {
			os_memcpy(key_str, cred->key, cred->key_len);
			key_str[cred->key_len] = '\0';
		}
	}

	uuid_bin2str(ap_settings->uuid, uuid_str, sizeof(uuid_str));
	/* Use wpa_msg_ctrl to avoid showing the key in debug log */
	wpa_msg_ctrl(wpa_s, MSG_INFO, WPS_EVENT_ER_AP_SETTINGS
		     "uuid=%s ssid=%s auth_type=0x%04x encr_type=0x%04x "
		     "key=%s",
		     uuid_str, wpa_ssid_txt(cred->ssid, cred->ssid_len),
		     cred->auth_type, cred->encr_type, key_str);
}


static void wpa_supplicant_wps_event_er_set_sel_reg(
	struct wpa_supplicant *wpa_s,
	struct wps_event_er_set_selected_registrar *ev)
{
	char uuid_str[100];

	uuid_bin2str(ev->uuid, uuid_str, sizeof(uuid_str));
	switch (ev->state) {
	case WPS_ER_SET_SEL_REG_START:
		wpa_msg(wpa_s, MSG_DEBUG, WPS_EVENT_ER_SET_SEL_REG
			"uuid=%s state=START sel_reg=%d dev_passwd_id=%u "
			"sel_reg_config_methods=0x%x",
			uuid_str, ev->sel_reg, ev->dev_passwd_id,
			ev->sel_reg_config_methods);
		break;
	case WPS_ER_SET_SEL_REG_DONE:
		wpa_msg(wpa_s, MSG_DEBUG, WPS_EVENT_ER_SET_SEL_REG
			"uuid=%s state=DONE", uuid_str);
		break;
	case WPS_ER_SET_SEL_REG_FAILED:
		wpa_msg(wpa_s, MSG_INFO, WPS_EVENT_ER_SET_SEL_REG
			"uuid=%s state=FAILED", uuid_str);
		break;
	}
}


static void wpa_supplicant_wps_event(void *ctx, enum wps_event event,
				     union wps_event_data *data)
{
	struct wpa_supplicant *wpa_s = ctx;
	switch (event) {
	case WPS_EV_M2D:
		wpa_supplicant_wps_event_m2d(wpa_s, &data->m2d);
		break;
	case WPS_EV_FAIL:
		wpa_supplicant_wps_event_fail(wpa_s, &data->fail);
		break;
	case WPS_EV_SUCCESS:
		wpa_supplicant_wps_event_success(wpa_s);
		break;
	case WPS_EV_PWD_AUTH_FAIL:
#ifdef CONFIG_AP
		if (wpa_s->ap_iface && data->pwd_auth_fail.enrollee)
			wpa_supplicant_ap_pwd_auth_fail(wpa_s);
#endif /* CONFIG_AP */
		break;
	case WPS_EV_PBC_OVERLAP:
		break;
	case WPS_EV_PBC_TIMEOUT:
		break;
	case WPS_EV_PBC_ACTIVE:
		wpa_msg(wpa_s, MSG_INFO, WPS_EVENT_ACTIVE);
		break;
	case WPS_EV_PBC_DISABLE:
		wpa_msg(wpa_s, MSG_INFO, WPS_EVENT_DISABLE);
		break;
	case WPS_EV_ER_AP_ADD:
		wpa_supplicant_wps_event_er_ap_add(wpa_s, &data->ap);
		break;
	case WPS_EV_ER_AP_REMOVE:
		wpa_supplicant_wps_event_er_ap_remove(wpa_s, &data->ap);
		break;
	case WPS_EV_ER_ENROLLEE_ADD:
		wpa_supplicant_wps_event_er_enrollee_add(wpa_s,
							 &data->enrollee);
		break;
	case WPS_EV_ER_ENROLLEE_REMOVE:
		wpa_supplicant_wps_event_er_enrollee_remove(wpa_s,
							    &data->enrollee);
		break;
	case WPS_EV_ER_AP_SETTINGS:
		wpa_supplicant_wps_event_er_ap_settings(wpa_s,
							&data->ap_settings);
		break;
	case WPS_EV_ER_SET_SELECTED_REGISTRAR:
		wpa_supplicant_wps_event_er_set_sel_reg(wpa_s,
							&data->set_sel_reg);
		break;
	case WPS_EV_AP_PIN_SUCCESS:
		break;
	}
}


static int wpa_supplicant_wps_rf_band(void *ctx)
{
	struct wpa_supplicant *wpa_s = ctx;

	if (!wpa_s->current_ssid || !wpa_s->assoc_freq)
		return 0;

	return (wpa_s->assoc_freq > 50000) ? WPS_RF_60GHZ :
		(wpa_s->assoc_freq > 2484) ? WPS_RF_50GHZ : WPS_RF_24GHZ;
}


enum wps_request_type wpas_wps_get_req_type(struct wpa_ssid *ssid)
{
	if (eap_is_wps_pbc_enrollee(&ssid->eap) ||
	    eap_is_wps_pin_enrollee(&ssid->eap))
		return WPS_REQ_ENROLLEE;
	else
		return WPS_REQ_REGISTRAR;
}


static void wpas_clear_wps(struct wpa_supplicant *wpa_s)
{
	int id;
	struct wpa_ssid *ssid, *remove_ssid = NULL, *prev_current;

	wpa_s->after_wps = 0;
	wpa_s->known_wps_freq = 0;

	prev_current = wpa_s->current_ssid;

	/* Enable the networks disabled during wpas_wps_reassoc */
	wpas_wps_reenable_networks(wpa_s);

	eloop_cancel_timeout(wpas_wps_timeout, wpa_s, NULL);
	eloop_cancel_timeout(wpas_wps_clear_timeout, wpa_s, NULL);

	/* Remove any existing WPS network from configuration */
	ssid = wpa_s->conf->ssid;
	while (ssid) {
		if (ssid->key_mgmt & WPA_KEY_MGMT_WPS) {
			if (ssid == wpa_s->current_ssid) {
				wpa_s->own_disconnect_req = 1;
				wpa_supplicant_deauthenticate(
					wpa_s, WLAN_REASON_DEAUTH_LEAVING);
			}
			id = ssid->id;
			remove_ssid = ssid;
		} else
			id = -1;
		ssid = ssid->next;
		if (id >= 0) {
			if (prev_current == remove_ssid) {
				wpa_sm_set_config(wpa_s->wpa, NULL);
				eapol_sm_notify_config(wpa_s->eapol, NULL,
						       NULL);
			}
			wpas_notify_network_removed(wpa_s, remove_ssid);
			wpa_config_remove_network(wpa_s->conf, id);
		}
	}

	wpas_wps_clear_ap_info(wpa_s);
}


static void wpas_wps_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;
	union wps_event_data data;

	wpa_msg(wpa_s, MSG_INFO, WPS_EVENT_TIMEOUT "Requested operation timed "
		"out");
	os_memset(&data, 0, sizeof(data));
	data.fail.config_error = WPS_CFG_MSG_TIMEOUT;
	data.fail.error_indication = WPS_EI_NO_ERROR;
	/*
	 * Call wpas_notify_wps_event_fail() directly instead of through
	 * wpa_supplicant_wps_event() which would end up registering unnecessary
	 * timeouts (those are only for the case where the failure happens
	 * during an EAP-WSC exchange).
	 */
	wpas_notify_wps_event_fail(wpa_s, &data.fail);
	wpas_clear_wps(wpa_s);
}


static struct wpa_ssid * wpas_wps_add_network(struct wpa_supplicant *wpa_s,
					      int registrar, const u8 *dev_addr,
					      const u8 *bssid)
{
	struct wpa_ssid *ssid;

	ssid = wpa_config_add_network(wpa_s->conf);
	if (ssid == NULL)
		return NULL;
	wpas_notify_network_added(wpa_s, ssid);
	wpa_config_set_network_defaults(ssid);
	ssid->temporary = 1;
	if (wpa_config_set(ssid, "key_mgmt", "WPS", 0) < 0 ||
	    wpa_config_set(ssid, "eap", "WSC", 0) < 0 ||
	    wpa_config_set(ssid, "identity", registrar ?
			   "\"" WSC_ID_REGISTRAR "\"" :
			   "\"" WSC_ID_ENROLLEE "\"", 0) < 0) {
		wpas_notify_network_removed(wpa_s, ssid);
		wpa_config_remove_network(wpa_s->conf, ssid->id);
		return NULL;
	}

#ifdef CONFIG_P2P
	if (dev_addr)
		os_memcpy(ssid->go_p2p_dev_addr, dev_addr, ETH_ALEN);
#endif /* CONFIG_P2P */

	if (bssid) {
#ifndef CONFIG_P2P
		struct wpa_bss *bss;
		int count = 0;
#endif /* CONFIG_P2P */

		os_memcpy(ssid->bssid, bssid, ETH_ALEN);
		ssid->bssid_set = 1;

		/*
		 * Note: With P2P, the SSID may change at the time the WPS
		 * provisioning is started, so better not filter the AP based
		 * on the current SSID in the scan results.
		 */
#ifndef CONFIG_P2P
		dl_list_for_each(bss, &wpa_s->bss, struct wpa_bss, list) {
			if (os_memcmp(bssid, bss->bssid, ETH_ALEN) != 0)
				continue;

			os_free(ssid->ssid);
			ssid->ssid = os_memdup(bss->ssid, bss->ssid_len);
			if (ssid->ssid == NULL)
				break;
			ssid->ssid_len = bss->ssid_len;
			wpa_hexdump_ascii(MSG_DEBUG, "WPS: Picked SSID from "
					  "scan results",
					  ssid->ssid, ssid->ssid_len);
			count++;
		}

		if (count > 1) {
			wpa_printf(MSG_DEBUG, "WPS: More than one SSID found "
				   "for the AP; use wildcard");
			os_free(ssid->ssid);
			ssid->ssid = NULL;
			ssid->ssid_len = 0;
		}
#endif /* CONFIG_P2P */
	}

	return ssid;
}


static void wpas_wps_temp_disable(struct wpa_supplicant *wpa_s,
				  struct wpa_ssid *selected)
{
	struct wpa_ssid *ssid;

	if (wpa_s->current_ssid) {
		wpa_s->own_disconnect_req = 1;
		wpa_supplicant_deauthenticate(
			wpa_s, WLAN_REASON_DEAUTH_LEAVING);
	}

	/* Mark all other networks disabled and trigger reassociation */
	ssid = wpa_s->conf->ssid;
	while (ssid) {
		int was_disabled = ssid->disabled;
		ssid->disabled_for_connect = 0;
		/*
		 * In case the network object corresponds to a persistent group
		 * then do not send out network disabled signal. In addition,
		 * do not change disabled status of persistent network objects
		 * from 2 to 1 should we connect to another network.
		 */
		if (was_disabled != 2) {
			ssid->disabled = ssid != selected;
			if (was_disabled != ssid->disabled) {
				if (ssid->disabled)
					ssid->disabled_for_connect = 1;
				wpas_notify_network_enabled_changed(wpa_s,
								    ssid);
			}
		}
		ssid = ssid->next;
	}
}


static void wpas_wps_reassoc(struct wpa_supplicant *wpa_s,
			     struct wpa_ssid *selected, const u8 *bssid,
			     int freq)
{
	struct wpa_bss *bss;

	wpa_s->wps_run++;
	if (wpa_s->wps_run == 0)
		wpa_s->wps_run++;
	wpa_s->after_wps = 0;
	wpa_s->known_wps_freq = 0;
	if (freq) {
		wpa_s->after_wps = 5;
		wpa_s->wps_freq = freq;
	} else if (bssid) {
		bss = wpa_bss_get_bssid_latest(wpa_s, bssid);
		if (bss && bss->freq > 0) {
			wpa_s->known_wps_freq = 1;
			wpa_s->wps_freq = bss->freq;
		}
	}

	wpas_wps_temp_disable(wpa_s, selected);

	wpa_s->disconnected = 0;
	wpa_s->reassociate = 1;
	wpa_s->scan_runs = 0;
	wpa_s->normal_scans = 0;
	wpa_s->wps_success = 0;
	wpa_s->blacklist_cleared = 0;

	wpa_supplicant_cancel_sched_scan(wpa_s);
	wpa_supplicant_req_scan(wpa_s, 0, 0);
}


int wpas_wps_start_pbc(struct wpa_supplicant *wpa_s, const u8 *bssid,
		       int p2p_group)
{
	struct wpa_ssid *ssid;

#ifdef CONFIG_AP
	if (wpa_s->ap_iface) {
		wpa_printf(MSG_DEBUG,
			   "WPS: Reject request to start Registrar(as station) operation while AP mode is enabled");
		return -1;
	}
#endif /* CONFIG_AP */
	wpas_clear_wps(wpa_s);
	ssid = wpas_wps_add_network(wpa_s, 0, NULL, bssid);
	if (ssid == NULL)
		return -1;
	ssid->temporary = 1;
	ssid->p2p_group = p2p_group;
	/*
	 * When starting a regular WPS process (not P2P group formation)
	 * the registrar/final station can be either AP or PCP
	 * so use a "don't care" value for the pbss flag.
	 */
	if (!p2p_group)
		ssid->pbss = 2;
#ifdef CONFIG_P2P
	if (p2p_group && wpa_s->go_params && wpa_s->go_params->ssid_len) {
		ssid->ssid = os_zalloc(wpa_s->go_params->ssid_len + 1);
		if (ssid->ssid) {
			ssid->ssid_len = wpa_s->go_params->ssid_len;
			os_memcpy(ssid->ssid, wpa_s->go_params->ssid,
				  ssid->ssid_len);
			if (wpa_s->go_params->freq > 56160) {
				/* P2P in 60 GHz uses PBSS */
				ssid->pbss = 1;
			}
			wpa_hexdump_ascii(MSG_DEBUG, "WPS: Use specific AP "
					  "SSID", ssid->ssid, ssid->ssid_len);
		}
	}
#endif /* CONFIG_P2P */
	if (wpa_config_set(ssid, "phase1", "\"pbc=1\"", 0) < 0)
		return -1;
	if (wpa_s->wps_fragment_size)
		ssid->eap.fragment_size = wpa_s->wps_fragment_size;
	wpa_supplicant_wps_event(wpa_s, WPS_EV_PBC_ACTIVE, NULL);
	eloop_register_timeout(WPS_PBC_WALK_TIME, 0, wpas_wps_timeout,
			       wpa_s, NULL);
	wpas_wps_reassoc(wpa_s, ssid, bssid, 0);
	return 0;
}


static int wpas_wps_start_dev_pw(struct wpa_supplicant *wpa_s,
				 const u8 *dev_addr, const u8 *bssid,
				 const char *pin, int p2p_group, u16 dev_pw_id,
				 const u8 *peer_pubkey_hash,
				 const u8 *ssid_val, size_t ssid_len, int freq)
{
	struct wpa_ssid *ssid;
	char val[128 + 2 * WPS_OOB_PUBKEY_HASH_LEN];
	unsigned int rpin = 0;
	char hash[2 * WPS_OOB_PUBKEY_HASH_LEN + 10];

#ifdef CONFIG_AP
	if (wpa_s->ap_iface) {
		wpa_printf(MSG_DEBUG,
			   "WPS: Reject request to start Registrar(as station) operation while AP mode is enabled");
		return -1;
	}
#endif /* CONFIG_AP */
	wpas_clear_wps(wpa_s);
	if (bssid && is_zero_ether_addr(bssid))
		bssid = NULL;
	ssid = wpas_wps_add_network(wpa_s, 0, dev_addr, bssid);
	if (ssid == NULL) {
		wpa_printf(MSG_DEBUG, "WPS: Could not add network");
		return -1;
	}
	ssid->temporary = 1;
	ssid->p2p_group = p2p_group;
	/*
	 * When starting a regular WPS process (not P2P group formation)
	 * the registrar/final station can be either AP or PCP
	 * so use a "don't care" value for the pbss flag.
	 */
	if (!p2p_group)
		ssid->pbss = 2;
	if (ssid_val) {
		ssid->ssid = os_malloc(ssid_len);
		if (ssid->ssid) {
			os_memcpy(ssid->ssid, ssid_val, ssid_len);
			ssid->ssid_len = ssid_len;
		}
	}
	if (peer_pubkey_hash) {
		os_memcpy(hash, " pkhash=", 8);
		wpa_snprintf_hex_uppercase(hash + 8, sizeof(hash) - 8,
					   peer_pubkey_hash,
					   WPS_OOB_PUBKEY_HASH_LEN);
	} else {
		hash[0] = '\0';
	}
#ifdef CONFIG_P2P
	if (p2p_group && wpa_s->go_params && wpa_s->go_params->ssid_len) {
		os_free(ssid->ssid);
		ssid->ssid = os_zalloc(wpa_s->go_params->ssid_len + 1);
		if (ssid->ssid) {
			ssid->ssid_len = wpa_s->go_params->ssid_len;
			os_memcpy(ssid->ssid, wpa_s->go_params->ssid,
				  ssid->ssid_len);
			if (wpa_s->go_params->freq > 56160) {
				/* P2P in 60 GHz uses PBSS */
				ssid->pbss = 1;
			}
			wpa_hexdump_ascii(MSG_DEBUG, "WPS: Use specific AP "
					  "SSID", ssid->ssid, ssid->ssid_len);
		}
	}
#endif /* CONFIG_P2P */
	if (pin)
		os_snprintf(val, sizeof(val), "\"pin=%s dev_pw_id=%u%s\"",
			    pin, dev_pw_id, hash);
	else if (pin == NULL && dev_pw_id == DEV_PW_NFC_CONNECTION_HANDOVER) {
		os_snprintf(val, sizeof(val), "\"dev_pw_id=%u%s\"",
			    dev_pw_id, hash);
	} else {
		if (wps_generate_pin(&rpin) < 0) {
			wpa_printf(MSG_DEBUG, "WPS: Could not generate PIN");
			return -1;
		}
		os_snprintf(val, sizeof(val), "\"pin=%08d dev_pw_id=%u%s\"",
			    rpin, dev_pw_id, hash);
	}
	if (wpa_config_set(ssid, "phase1", val, 0) < 0) {
		wpa_printf(MSG_DEBUG, "WPS: Failed to set phase1 '%s'", val);
		return -1;
	}
	if (wpa_s->wps_fragment_size)
		ssid->eap.fragment_size = wpa_s->wps_fragment_size;
	eloop_register_timeout(WPS_PBC_WALK_TIME, 0, wpas_wps_timeout,
			       wpa_s, NULL);
	wpa_s->wps_ap_iter = 1;
	wpas_wps_reassoc(wpa_s, ssid, bssid, freq);
	return rpin;
}


int wpas_wps_start_pin(struct wpa_supplicant *wpa_s, const u8 *bssid,
		       const char *pin, int p2p_group, u16 dev_pw_id)
{
	os_get_reltime(&wpa_s->wps_pin_start_time);
	return wpas_wps_start_dev_pw(wpa_s, NULL, bssid, pin, p2p_group,
				     dev_pw_id, NULL, NULL, 0, 0);
}


void wpas_wps_pbc_overlap(struct wpa_supplicant *wpa_s)
{
	union wps_event_data data;

	os_memset(&data, 0, sizeof(data));
	data.fail.config_error = WPS_CFG_MULTIPLE_PBC_DETECTED;
	data.fail.error_indication = WPS_EI_NO_ERROR;
	/*
	 * Call wpas_notify_wps_event_fail() directly instead of through
	 * wpa_supplicant_wps_event() which would end up registering unnecessary
	 * timeouts (those are only for the case where the failure happens
	 * during an EAP-WSC exchange).
	 */
	wpas_notify_wps_event_fail(wpa_s, &data.fail);
}

/* Cancel the wps pbc/pin requests */
int wpas_wps_cancel(struct wpa_supplicant *wpa_s)
{
#ifdef CONFIG_AP
	if (wpa_s->ap_iface) {
		wpa_printf(MSG_DEBUG, "WPS: Cancelling in AP mode");
		return wpa_supplicant_ap_wps_cancel(wpa_s);
	}
#endif /* CONFIG_AP */

	if (wpa_s->wpa_state == WPA_SCANNING ||
	    wpa_s->wpa_state == WPA_DISCONNECTED) {
		wpa_printf(MSG_DEBUG, "WPS: Cancel operation - cancel scan");
		wpa_supplicant_cancel_scan(wpa_s);
		wpas_clear_wps(wpa_s);
	} else if (wpa_s->wpa_state >= WPA_ASSOCIATED) {
		wpa_printf(MSG_DEBUG, "WPS: Cancel operation - "
			   "deauthenticate");
		wpa_s->own_disconnect_req = 1;
		wpa_supplicant_deauthenticate(wpa_s,
					      WLAN_REASON_DEAUTH_LEAVING);
		wpas_clear_wps(wpa_s);
	} else {
		wpas_wps_reenable_networks(wpa_s);
		wpas_wps_clear_ap_info(wpa_s);
		if (eloop_cancel_timeout(wpas_wps_clear_timeout, wpa_s, NULL) >
		    0)
			wpas_clear_wps(wpa_s);
	}

	wpa_s->after_wps = 0;

	return 0;
}


int wpas_wps_start_reg(struct wpa_supplicant *wpa_s, const u8 *bssid,
		       const char *pin, struct wps_new_ap_settings *settings)
{
	struct wpa_ssid *ssid;
	char val[200];
	char *pos, *end;
	int res;

#ifdef CONFIG_AP
	if (wpa_s->ap_iface) {
		wpa_printf(MSG_DEBUG,
			   "WPS: Reject request to start Registrar(as station) operation while AP mode is enabled");
		return -1;
	}
#endif /* CONFIG_AP */
	if (!pin)
		return -1;
	wpas_clear_wps(wpa_s);
	ssid = wpas_wps_add_network(wpa_s, 1, NULL, bssid);
	if (ssid == NULL)
		return -1;
	ssid->temporary = 1;
	pos = val;
	end = pos + sizeof(val);
	res = os_snprintf(pos, end - pos, "\"pin=%s", pin);
	if (os_snprintf_error(end - pos, res))
		return -1;
	pos += res;
	if (settings) {
		res = os_snprintf(pos, end - pos, " new_ssid=%s new_auth=%s "
				  "new_encr=%s new_key=%s",
				  settings->ssid_hex, settings->auth,
				  settings->encr, settings->key_hex);
		if (os_snprintf_error(end - pos, res))
			return -1;
		pos += res;
	}
	res = os_snprintf(pos, end - pos, "\"");
	if (os_snprintf_error(end - pos, res))
		return -1;
	if (wpa_config_set(ssid, "phase1", val, 0) < 0)
		return -1;
	if (wpa_s->wps_fragment_size)
		ssid->eap.fragment_size = wpa_s->wps_fragment_size;
	eloop_register_timeout(WPS_PBC_WALK_TIME, 0, wpas_wps_timeout,
			       wpa_s, NULL);
	wpas_wps_reassoc(wpa_s, ssid, bssid, 0);
	return 0;
}


static int wpas_wps_new_psk_cb(void *ctx, const u8 *mac_addr,
			       const u8 *p2p_dev_addr, const u8 *psk,
			       size_t psk_len)
{
	if (is_zero_ether_addr(p2p_dev_addr)) {
		wpa_printf(MSG_DEBUG,
			   "Received new WPA/WPA2-PSK from WPS for STA " MACSTR,
			   MAC2STR(mac_addr));
	} else {
		wpa_printf(MSG_DEBUG,
			   "Received new WPA/WPA2-PSK from WPS for STA " MACSTR
			   " P2P Device Addr " MACSTR,
			   MAC2STR(mac_addr), MAC2STR(p2p_dev_addr));
	}
	wpa_hexdump_key(MSG_DEBUG, "Per-device PSK", psk, psk_len);

	/* TODO */

	return 0;
}


static void wpas_wps_pin_needed_cb(void *ctx, const u8 *uuid_e,
				   const struct wps_device_data *dev)
{
	char uuid[40], txt[400];
	int len;
	char devtype[WPS_DEV_TYPE_BUFSIZE];
	if (uuid_bin2str(uuid_e, uuid, sizeof(uuid)))
		return;
	wpa_printf(MSG_DEBUG, "WPS: PIN needed for UUID-E %s", uuid);
	len = os_snprintf(txt, sizeof(txt), "WPS-EVENT-PIN-NEEDED %s " MACSTR
			  " [%s|%s|%s|%s|%s|%s]",
			  uuid, MAC2STR(dev->mac_addr), dev->device_name,
			  dev->manufacturer, dev->model_name,
			  dev->model_number, dev->serial_number,
			  wps_dev_type_bin2str(dev->pri_dev_type, devtype,
					       sizeof(devtype)));
	if (!os_snprintf_error(sizeof(txt), len))
		wpa_printf(MSG_INFO, "%s", txt);
}


static void wpas_wps_set_sel_reg_cb(void *ctx, int sel_reg, u16 dev_passwd_id,
				    u16 sel_reg_config_methods)
{
#ifdef CONFIG_WPS_ER
	struct wpa_supplicant *wpa_s = ctx;

	if (wpa_s->wps_er == NULL)
		return;
	wpa_printf(MSG_DEBUG, "WPS ER: SetSelectedRegistrar - sel_reg=%d "
		   "dev_password_id=%u sel_reg_config_methods=0x%x",
		   sel_reg, dev_passwd_id, sel_reg_config_methods);
	wps_er_set_sel_reg(wpa_s->wps_er, sel_reg, dev_passwd_id,
			   sel_reg_config_methods);
#endif /* CONFIG_WPS_ER */
}


static u16 wps_fix_config_methods(u16 config_methods)
{
	if ((config_methods &
	     (WPS_CONFIG_DISPLAY | WPS_CONFIG_VIRT_DISPLAY |
	      WPS_CONFIG_PHY_DISPLAY)) == WPS_CONFIG_DISPLAY) {
		wpa_printf(MSG_INFO, "WPS: Converting display to "
			   "virtual_display for WPS 2.0 compliance");
		config_methods |= WPS_CONFIG_VIRT_DISPLAY;
	}
	if ((config_methods &
	     (WPS_CONFIG_PUSHBUTTON | WPS_CONFIG_VIRT_PUSHBUTTON |
	      WPS_CONFIG_PHY_PUSHBUTTON)) == WPS_CONFIG_PUSHBUTTON) {
		wpa_printf(MSG_INFO, "WPS: Converting push_button to "
			   "virtual_push_button for WPS 2.0 compliance");
		config_methods |= WPS_CONFIG_VIRT_PUSHBUTTON;
	}

	return config_methods;
}


static void wpas_wps_set_uuid(struct wpa_supplicant *wpa_s,
			      struct wps_context *wps)
{
	char buf[50];
	const char *src;

	if (is_nil_uuid(wpa_s->conf->uuid)) {
		struct wpa_supplicant *first;
		first = wpa_s->global->ifaces;
		while (first && first->next)
			first = first->next;
		if (first && first != wpa_s) {
			if (wps != wpa_s->global->ifaces->wps)
				os_memcpy(wps->uuid,
					  wpa_s->global->ifaces->wps->uuid,
					  WPS_UUID_LEN);
			src = "from the first interface";
		} else if (wpa_s->conf->auto_uuid == 1) {
			uuid_random(wps->uuid);
			src = "based on random data";
		} else {
			uuid_gen_mac_addr(wpa_s->own_addr, wps->uuid);
			src = "based on MAC address";
		}
	} else {
		os_memcpy(wps->uuid, wpa_s->conf->uuid, WPS_UUID_LEN);
		src = "based on configuration";
	}

	uuid_bin2str(wps->uuid, buf, sizeof(buf));
	wpa_dbg(wpa_s, MSG_DEBUG, "WPS: UUID %s: %s", src, buf);
}


static void wpas_wps_set_vendor_ext_m1(struct wpa_supplicant *wpa_s,
				       struct wps_context *wps)
{
	wpabuf_free(wps->dev.vendor_ext_m1);
	wps->dev.vendor_ext_m1 = NULL;

	if (wpa_s->conf->wps_vendor_ext_m1) {
		wps->dev.vendor_ext_m1 =
			wpabuf_dup(wpa_s->conf->wps_vendor_ext_m1);
		if (!wps->dev.vendor_ext_m1) {
			wpa_printf(MSG_ERROR, "WPS: Cannot "
				   "allocate memory for vendor_ext_m1");
		}
	}
}


int wpas_wps_init(struct wpa_supplicant *wpa_s)
{
	struct wps_context *wps;
	struct wps_registrar_config rcfg;
	struct hostapd_hw_modes *modes;
	u16 m;

	wps = os_zalloc(sizeof(*wps));
	if (wps == NULL)
		return -1;

	wps->cred_cb = wpa_supplicant_wps_cred;
	wps->event_cb = wpa_supplicant_wps_event;
	wps->rf_band_cb = wpa_supplicant_wps_rf_band;
	wps->cb_ctx = wpa_s;

	wps->dev.device_name = wpa_s->conf->device_name;
	wps->dev.manufacturer = wpa_s->conf->manufacturer;
	wps->dev.model_name = wpa_s->conf->model_name;
	wps->dev.model_number = wpa_s->conf->model_number;
	wps->dev.serial_number = wpa_s->conf->serial_number;
	wps->config_methods =
		wps_config_methods_str2bin(wpa_s->conf->config_methods);
	if ((wps->config_methods & (WPS_CONFIG_DISPLAY | WPS_CONFIG_LABEL)) ==
	    (WPS_CONFIG_DISPLAY | WPS_CONFIG_LABEL)) {
		wpa_printf(MSG_ERROR, "WPS: Both Label and Display config "
			   "methods are not allowed at the same time");
		os_free(wps);
		return -1;
	}
	wps->config_methods = wps_fix_config_methods(wps->config_methods);
	wps->dev.config_methods = wps->config_methods;
	os_memcpy(wps->dev.pri_dev_type, wpa_s->conf->device_type,
		  WPS_DEV_TYPE_LEN);

	wps->dev.num_sec_dev_types = wpa_s->conf->num_sec_device_types;
	os_memcpy(wps->dev.sec_dev_type, wpa_s->conf->sec_device_type,
		  WPS_DEV_TYPE_LEN * wps->dev.num_sec_dev_types);

	wpas_wps_set_vendor_ext_m1(wpa_s, wps);

	wps->dev.os_version = WPA_GET_BE32(wpa_s->conf->os_version);
	modes = wpa_s->hw.modes;
	if (modes) {
		for (m = 0; m < wpa_s->hw.num_modes; m++) {
			if (modes[m].mode == HOSTAPD_MODE_IEEE80211B ||
			    modes[m].mode == HOSTAPD_MODE_IEEE80211G)
				wps->dev.rf_bands |= WPS_RF_24GHZ;
			else if (modes[m].mode == HOSTAPD_MODE_IEEE80211A)
				wps->dev.rf_bands |= WPS_RF_50GHZ;
			else if (modes[m].mode == HOSTAPD_MODE_IEEE80211AD)
				wps->dev.rf_bands |= WPS_RF_60GHZ;
		}
	}
	if (wps->dev.rf_bands == 0) {
		/*
		 * Default to claiming support for both bands if the driver
		 * does not provide support for fetching supported bands.
		 */
		wps->dev.rf_bands = WPS_RF_24GHZ | WPS_RF_50GHZ;
	}
	os_memcpy(wps->dev.mac_addr, wpa_s->own_addr, ETH_ALEN);
	wpas_wps_set_uuid(wpa_s, wps);

	wps->auth_types = WPS_AUTH_WPA2PSK | WPS_AUTH_WPAPSK;
	wps->encr_types = WPS_ENCR_AES | WPS_ENCR_TKIP;

	os_memset(&rcfg, 0, sizeof(rcfg));
	rcfg.new_psk_cb = wpas_wps_new_psk_cb;
	rcfg.pin_needed_cb = wpas_wps_pin_needed_cb;
	rcfg.set_sel_reg_cb = wpas_wps_set_sel_reg_cb;
	rcfg.cb_ctx = wpa_s;

	wps->registrar = wps_registrar_init(wps, &rcfg);
	if (wps->registrar == NULL) {
		wpa_printf(MSG_DEBUG, "Failed to initialize WPS Registrar");
		os_free(wps);
		return -1;
	}

	wpa_s->wps = wps;

	return 0;
}


#ifdef CONFIG_WPS_ER
static void wpas_wps_nfc_clear(struct wps_context *wps)
{
	wps->ap_nfc_dev_pw_id = 0;
	wpabuf_free(wps->ap_nfc_dh_pubkey);
	wps->ap_nfc_dh_pubkey = NULL;
	wpabuf_free(wps->ap_nfc_dh_privkey);
	wps->ap_nfc_dh_privkey = NULL;
	wpabuf_free(wps->ap_nfc_dev_pw);
	wps->ap_nfc_dev_pw = NULL;
}
#endif /* CONFIG_WPS_ER */


void wpas_wps_deinit(struct wpa_supplicant *wpa_s)
{
	wpas_wps_assoc_with_cred_cancel(wpa_s);
	eloop_cancel_timeout(wpas_wps_timeout, wpa_s, NULL);
	eloop_cancel_timeout(wpas_wps_clear_timeout, wpa_s, NULL);
	eloop_cancel_timeout(wpas_wps_reenable_networks_cb, wpa_s, NULL);
	wpas_wps_clear_ap_info(wpa_s);

#ifdef CONFIG_P2P
	eloop_cancel_timeout(wpas_p2p_pbc_overlap_cb, wpa_s, NULL);
#endif /* CONFIG_P2P */

	if (wpa_s->wps == NULL)
		return;

#ifdef CONFIG_WPS_ER
	wps_er_deinit(wpa_s->wps_er, NULL, NULL);
	wpa_s->wps_er = NULL;
	wpas_wps_nfc_clear(wpa_s->wps);
#endif /* CONFIG_WPS_ER */

	wps_registrar_deinit(wpa_s->wps->registrar);
	wpabuf_free(wpa_s->wps->dh_pubkey);
	wpabuf_free(wpa_s->wps->dh_privkey);
	wpabuf_free(wpa_s->wps->dev.vendor_ext_m1);
	os_free(wpa_s->wps->network_key);
	os_free(wpa_s->wps);
	wpa_s->wps = NULL;
}


int wpas_wps_ssid_bss_match(struct wpa_supplicant *wpa_s,
			    struct wpa_ssid *ssid, struct wpa_bss *bss)
{
	struct wpabuf *wps_ie;

	if (!(ssid->key_mgmt & WPA_KEY_MGMT_WPS))
		return -1;

	wps_ie = wpa_bss_get_vendor_ie_multi(bss, WPS_IE_VENDOR_TYPE);
	if (eap_is_wps_pbc_enrollee(&ssid->eap)) {
		if (!wps_ie) {
			wpa_printf(MSG_DEBUG, "   skip - non-WPS AP");
			return 0;
		}

		if (!wps_is_selected_pbc_registrar(wps_ie)) {
			wpa_printf(MSG_DEBUG, "   skip - WPS AP "
				   "without active PBC Registrar");
			wpabuf_free(wps_ie);
			return 0;
		}

		/* TODO: overlap detection */
		wpa_printf(MSG_DEBUG, "   selected based on WPS IE "
			   "(Active PBC)");
		wpabuf_free(wps_ie);
		return 1;
	}

	if (eap_is_wps_pin_enrollee(&ssid->eap)) {
		if (!wps_ie) {
			wpa_printf(MSG_DEBUG, "   skip - non-WPS AP");
			return 0;
		}

		/*
		 * Start with WPS APs that advertise our address as an
		 * authorized MAC (v2.0) or active PIN Registrar (v1.0) and
		 * allow any WPS AP after couple of scans since some APs do not
		 * set Selected Registrar attribute properly when using
		 * external Registrar.
		 */
		if (!wps_is_addr_authorized(wps_ie, wpa_s->own_addr, 1)) {
			struct os_reltime age;

			os_reltime_age(&wpa_s->wps_pin_start_time, &age);

			if (wpa_s->scan_runs < WPS_PIN_SCAN_IGNORE_SEL_REG ||
			    age.sec < WPS_PIN_TIME_IGNORE_SEL_REG) {
				wpa_printf(MSG_DEBUG,
					   "   skip - WPS AP without active PIN Registrar (scan_runs=%d age=%d)",
					   wpa_s->scan_runs, (int) age.sec);
				wpabuf_free(wps_ie);
				return 0;
			}
			wpa_printf(MSG_DEBUG, "   selected based on WPS IE");
		} else {
			wpa_printf(MSG_DEBUG, "   selected based on WPS IE "
				   "(Authorized MAC or Active PIN)");
		}
		wpabuf_free(wps_ie);
		return 1;
	}

	if (wps_ie) {
		wpa_printf(MSG_DEBUG, "   selected based on WPS IE");
		wpabuf_free(wps_ie);
		return 1;
	}

	return -1;
}


int wpas_wps_ssid_wildcard_ok(struct wpa_supplicant *wpa_s,
			      struct wpa_ssid *ssid,
			      struct wpa_bss *bss)
{
	struct wpabuf *wps_ie = NULL;
	int ret = 0;

	if (eap_is_wps_pbc_enrollee(&ssid->eap)) {
		wps_ie = wpa_bss_get_vendor_ie_multi(bss, WPS_IE_VENDOR_TYPE);
		if (wps_ie && wps_is_selected_pbc_registrar(wps_ie)) {
			/* allow wildcard SSID for WPS PBC */
			ret = 1;
		}
	} else if (eap_is_wps_pin_enrollee(&ssid->eap)) {
		wps_ie = wpa_bss_get_vendor_ie_multi(bss, WPS_IE_VENDOR_TYPE);
		if (wps_ie &&
		    (wps_is_addr_authorized(wps_ie, wpa_s->own_addr, 1) ||
		     wpa_s->scan_runs >= WPS_PIN_SCAN_IGNORE_SEL_REG)) {
			/* allow wildcard SSID for WPS PIN */
			ret = 1;
		}
	}

	if (!ret && ssid->bssid_set &&
	    os_memcmp(ssid->bssid, bss->bssid, ETH_ALEN) == 0) {
		/* allow wildcard SSID due to hardcoded BSSID match */
		ret = 1;
	}

#ifdef CONFIG_WPS_STRICT
	if (wps_ie) {
		if (wps_validate_beacon_probe_resp(wps_ie, bss->beacon_ie_len >
						   0, bss->bssid) < 0)
			ret = 0;
		if (bss->beacon_ie_len) {
			struct wpabuf *bcn_wps;
			bcn_wps = wpa_bss_get_vendor_ie_multi_beacon(
				bss, WPS_IE_VENDOR_TYPE);
			if (bcn_wps == NULL) {
				wpa_printf(MSG_DEBUG, "WPS: Mandatory WPS IE "
					   "missing from AP Beacon");
				ret = 0;
			} else {
				if (wps_validate_beacon(wps_ie) < 0)
					ret = 0;
				wpabuf_free(bcn_wps);
			}
		}
	}
#endif /* CONFIG_WPS_STRICT */

	wpabuf_free(wps_ie);

	return ret;
}


int wpas_wps_scan_pbc_overlap(struct wpa_supplicant *wpa_s,
			      struct wpa_bss *selected, struct wpa_ssid *ssid)
{
	const u8 *sel_uuid;
	struct wpabuf *wps_ie;
	int ret = 0;
	size_t i;

	if (!eap_is_wps_pbc_enrollee(&ssid->eap))
		return 0;

	wpa_printf(MSG_DEBUG, "WPS: Check whether PBC session overlap is "
		   "present in scan results; selected BSSID " MACSTR,
		   MAC2STR(selected->bssid));

	/* Make sure that only one AP is in active PBC mode */
	wps_ie = wpa_bss_get_vendor_ie_multi(selected, WPS_IE_VENDOR_TYPE);
	if (wps_ie) {
		sel_uuid = wps_get_uuid_e(wps_ie);
		wpa_hexdump(MSG_DEBUG, "WPS: UUID of the selected BSS",
			    sel_uuid, UUID_LEN);
	} else {
		wpa_printf(MSG_DEBUG, "WPS: Selected BSS does not include "
			   "WPS IE?!");
		sel_uuid = NULL;
	}

	for (i = 0; i < wpa_s->num_wps_ap; i++) {
		struct wps_ap_info *ap = &wpa_s->wps_ap[i];

		if (!ap->pbc_active ||
		    os_memcmp(selected->bssid, ap->bssid, ETH_ALEN) == 0)
			continue;

		wpa_printf(MSG_DEBUG, "WPS: Another BSS in active PBC mode: "
			   MACSTR, MAC2STR(ap->bssid));
		wpa_hexdump(MSG_DEBUG, "WPS: UUID of the other BSS",
			    ap->uuid, UUID_LEN);
		if (sel_uuid == NULL ||
		    os_memcmp(sel_uuid, ap->uuid, UUID_LEN) != 0) {
			ret = 1; /* PBC overlap */
			wpa_msg(wpa_s, MSG_INFO, "WPS: PBC overlap detected: "
				MACSTR " and " MACSTR,
				MAC2STR(selected->bssid),
				MAC2STR(ap->bssid));
			break;
		}

		/* TODO: verify that this is reasonable dual-band situation */
	}

	wpabuf_free(wps_ie);

	return ret;
}


void wpas_wps_notify_scan_results(struct wpa_supplicant *wpa_s)
{
	struct wpa_bss *bss;
	unsigned int pbc = 0, auth = 0, pin = 0, wps = 0;

	if (wpa_s->disconnected || wpa_s->wpa_state >= WPA_ASSOCIATED)
		return;

	dl_list_for_each(bss, &wpa_s->bss, struct wpa_bss, list) {
		struct wpabuf *ie;
		ie = wpa_bss_get_vendor_ie_multi(bss, WPS_IE_VENDOR_TYPE);
		if (!ie)
			continue;
		if (wps_is_selected_pbc_registrar(ie))
			pbc++;
		else if (wps_is_addr_authorized(ie, wpa_s->own_addr, 0))
			auth++;
		else if (wps_is_selected_pin_registrar(ie))
			pin++;
		else
			wps++;
		wpabuf_free(ie);
	}

	if (pbc)
		wpa_msg_ctrl(wpa_s, MSG_INFO, WPS_EVENT_AP_AVAILABLE_PBC);
	else if (auth)
		wpa_msg_ctrl(wpa_s, MSG_INFO, WPS_EVENT_AP_AVAILABLE_AUTH);
	else if (pin)
		wpa_msg_ctrl(wpa_s, MSG_INFO, WPS_EVENT_AP_AVAILABLE_PIN);
	else if (wps)
		wpa_msg_ctrl(wpa_s, MSG_INFO, WPS_EVENT_AP_AVAILABLE);
}


int wpas_wps_searching(struct wpa_supplicant *wpa_s)
{
	struct wpa_ssid *ssid;

	for (ssid = wpa_s->conf->ssid; ssid; ssid = ssid->next) {
		if ((ssid->key_mgmt & WPA_KEY_MGMT_WPS) && !ssid->disabled)
			return 1;
	}

	return 0;
}


int wpas_wps_scan_result_text(const u8 *ies, size_t ies_len, char *buf,
			      char *end)
{
	struct wpabuf *wps_ie;
	int ret;

	wps_ie = ieee802_11_vendor_ie_concat(ies, ies_len, WPS_DEV_OUI_WFA);
	if (wps_ie == NULL)
		return 0;

	ret = wps_attr_text(wps_ie, buf, end);
	wpabuf_free(wps_ie);
	return ret;
}


int wpas_wps_er_start(struct wpa_supplicant *wpa_s, const char *filter)
{
#ifdef CONFIG_WPS_ER
	if (wpa_s->wps_er) {
		wps_er_refresh(wpa_s->wps_er);
		return 0;
	}
	wpa_s->wps_er = wps_er_init(wpa_s->wps, wpa_s->ifname, filter);
	if (wpa_s->wps_er == NULL)
		return -1;
	return 0;
#else /* CONFIG_WPS_ER */
	return 0;
#endif /* CONFIG_WPS_ER */
}


void wpas_wps_er_stop(struct wpa_supplicant *wpa_s)
{
#ifdef CONFIG_WPS_ER
	wps_er_deinit(wpa_s->wps_er, NULL, NULL);
	wpa_s->wps_er = NULL;
#endif /* CONFIG_WPS_ER */
}


#ifdef CONFIG_WPS_ER
int wpas_wps_er_add_pin(struct wpa_supplicant *wpa_s, const u8 *addr,
			const char *uuid, const char *pin)
{
	u8 u[UUID_LEN];
	const u8 *use_uuid = NULL;
	u8 addr_buf[ETH_ALEN];

	if (os_strcmp(uuid, "any") == 0) {
	} else if (uuid_str2bin(uuid, u) == 0) {
		use_uuid = u;
	} else if (hwaddr_aton(uuid, addr_buf) == 0) {
		use_uuid = wps_er_get_sta_uuid(wpa_s->wps_er, addr_buf);
		if (use_uuid == NULL)
			return -1;
	} else
		return -1;
	return wps_registrar_add_pin(wpa_s->wps->registrar, addr,
				     use_uuid,
				     (const u8 *) pin, os_strlen(pin), 300);
}


int wpas_wps_er_pbc(struct wpa_supplicant *wpa_s, const char *uuid)
{
	u8 u[UUID_LEN], *use_uuid = NULL;
	u8 addr[ETH_ALEN], *use_addr = NULL;

	if (uuid_str2bin(uuid, u) == 0)
		use_uuid = u;
	else if (hwaddr_aton(uuid, addr) == 0)
		use_addr = addr;
	else
		return -1;
	return wps_er_pbc(wpa_s->wps_er, use_uuid, use_addr);
}


int wpas_wps_er_learn(struct wpa_supplicant *wpa_s, const char *uuid,
		      const char *pin)
{
	u8 u[UUID_LEN], *use_uuid = NULL;
	u8 addr[ETH_ALEN], *use_addr = NULL;

	if (uuid_str2bin(uuid, u) == 0)
		use_uuid = u;
	else if (hwaddr_aton(uuid, addr) == 0)
		use_addr = addr;
	else
		return -1;

	return wps_er_learn(wpa_s->wps_er, use_uuid, use_addr, (const u8 *) pin,
			    os_strlen(pin));
}


static int wpas_wps_network_to_cred(struct wpa_ssid *ssid,
				    struct wps_credential *cred)
{
	os_memset(cred, 0, sizeof(*cred));
	if (ssid->ssid_len > SSID_MAX_LEN)
		return -1;
	os_memcpy(cred->ssid, ssid->ssid, ssid->ssid_len);
	cred->ssid_len = ssid->ssid_len;
	if (ssid->key_mgmt & WPA_KEY_MGMT_PSK) {
		cred->auth_type = (ssid->proto & WPA_PROTO_RSN) ?
			WPS_AUTH_WPA2PSK : WPS_AUTH_WPAPSK;
		if (ssid->pairwise_cipher & WPA_CIPHER_CCMP)
			cred->encr_type = WPS_ENCR_AES;
		else
			cred->encr_type = WPS_ENCR_TKIP;
		if (ssid->passphrase) {
			cred->key_len = os_strlen(ssid->passphrase);
			if (cred->key_len >= 64)
				return -1;
			os_memcpy(cred->key, ssid->passphrase, cred->key_len);
		} else if (ssid->psk_set) {
			cred->key_len = 32;
			os_memcpy(cred->key, ssid->psk, 32);
		} else
			return -1;
	} else {
		cred->auth_type = WPS_AUTH_OPEN;
		cred->encr_type = WPS_ENCR_NONE;
	}

	return 0;
}


int wpas_wps_er_set_config(struct wpa_supplicant *wpa_s, const char *uuid,
			   int id)
{
	u8 u[UUID_LEN], *use_uuid = NULL;
	u8 addr[ETH_ALEN], *use_addr = NULL;
	struct wpa_ssid *ssid;
	struct wps_credential cred;
	int ret;

	if (uuid_str2bin(uuid, u) == 0)
		use_uuid = u;
	else if (hwaddr_aton(uuid, addr) == 0)
		use_addr = addr;
	else
		return -1;
	ssid = wpa_config_get_network(wpa_s->conf, id);
	if (ssid == NULL || ssid->ssid == NULL)
		return -1;

	if (wpas_wps_network_to_cred(ssid, &cred) < 0)
		return -1;
	ret = wps_er_set_config(wpa_s->wps_er, use_uuid, use_addr, &cred);
	os_memset(&cred, 0, sizeof(cred));
	return ret;
}


int wpas_wps_er_config(struct wpa_supplicant *wpa_s, const char *uuid,
		       const char *pin, struct wps_new_ap_settings *settings)
{
	u8 u[UUID_LEN], *use_uuid = NULL;
	u8 addr[ETH_ALEN], *use_addr = NULL;
	struct wps_credential cred;
	size_t len;

	if (uuid_str2bin(uuid, u) == 0)
		use_uuid = u;
	else if (hwaddr_aton(uuid, addr) == 0)
		use_addr = addr;
	else
		return -1;
	if (settings->ssid_hex == NULL || settings->auth == NULL ||
	    settings->encr == NULL || settings->key_hex == NULL)
		return -1;

	os_memset(&cred, 0, sizeof(cred));
	len = os_strlen(settings->ssid_hex);
	if ((len & 1) || len > 2 * sizeof(cred.ssid) ||
	    hexstr2bin(settings->ssid_hex, cred.ssid, len / 2))
		return -1;
	cred.ssid_len = len / 2;

	len = os_strlen(settings->key_hex);
	if ((len & 1) || len > 2 * sizeof(cred.key) ||
	    hexstr2bin(settings->key_hex, cred.key, len / 2))
		return -1;
	cred.key_len = len / 2;

	if (os_strcmp(settings->auth, "OPEN") == 0)
		cred.auth_type = WPS_AUTH_OPEN;
	else if (os_strcmp(settings->auth, "WPAPSK") == 0)
		cred.auth_type = WPS_AUTH_WPAPSK;
	else if (os_strcmp(settings->auth, "WPA2PSK") == 0)
		cred.auth_type = WPS_AUTH_WPA2PSK;
	else
		return -1;

	if (os_strcmp(settings->encr, "NONE") == 0)
		cred.encr_type = WPS_ENCR_NONE;
#ifdef CONFIG_TESTING_OPTIONS
	else if (os_strcmp(settings->encr, "WEP") == 0)
		cred.encr_type = WPS_ENCR_WEP;
#endif /* CONFIG_TESTING_OPTIONS */
	else if (os_strcmp(settings->encr, "TKIP") == 0)
		cred.encr_type = WPS_ENCR_TKIP;
	else if (os_strcmp(settings->encr, "CCMP") == 0)
		cred.encr_type = WPS_ENCR_AES;
	else
		return -1;

	return wps_er_config(wpa_s->wps_er, use_uuid, use_addr,
			     (const u8 *) pin, os_strlen(pin), &cred);
}


#ifdef CONFIG_WPS_NFC
struct wpabuf * wpas_wps_er_nfc_config_token(struct wpa_supplicant *wpa_s,
					     int ndef, const char *uuid)
{
	struct wpabuf *ret;
	u8 u[UUID_LEN], *use_uuid = NULL;
	u8 addr[ETH_ALEN], *use_addr = NULL;

	if (!wpa_s->wps_er)
		return NULL;

	if (uuid_str2bin(uuid, u) == 0)
		use_uuid = u;
	else if (hwaddr_aton(uuid, addr) == 0)
		use_addr = addr;
	else
		return NULL;

	ret = wps_er_nfc_config_token(wpa_s->wps_er, use_uuid, use_addr);
	if (ndef && ret) {
		struct wpabuf *tmp;
		tmp = ndef_build_wifi(ret);
		wpabuf_free(ret);
		if (tmp == NULL)
			return NULL;
		ret = tmp;
	}

	return ret;
}
#endif /* CONFIG_WPS_NFC */


static int callbacks_pending = 0;

static void wpas_wps_terminate_cb(void *ctx)
{
	wpa_printf(MSG_DEBUG, "WPS ER: Terminated");
	if (--callbacks_pending <= 0)
		eloop_terminate();
}
#endif /* CONFIG_WPS_ER */


int wpas_wps_terminate_pending(struct wpa_supplicant *wpa_s)
{
#ifdef CONFIG_WPS_ER
	if (wpa_s->wps_er) {
		callbacks_pending++;
		wps_er_deinit(wpa_s->wps_er, wpas_wps_terminate_cb, wpa_s);
		wpa_s->wps_er = NULL;
		return 1;
	}
#endif /* CONFIG_WPS_ER */
	return 0;
}


void wpas_wps_update_config(struct wpa_supplicant *wpa_s)
{
	struct wps_context *wps = wpa_s->wps;

	if (wps == NULL)
		return;

	if (wpa_s->conf->changed_parameters & CFG_CHANGED_CONFIG_METHODS) {
		wps->config_methods = wps_config_methods_str2bin(
			wpa_s->conf->config_methods);
		if ((wps->config_methods &
		     (WPS_CONFIG_DISPLAY | WPS_CONFIG_LABEL)) ==
		    (WPS_CONFIG_DISPLAY | WPS_CONFIG_LABEL)) {
			wpa_printf(MSG_ERROR, "WPS: Both Label and Display "
				   "config methods are not allowed at the "
				   "same time");
			wps->config_methods &= ~WPS_CONFIG_LABEL;
		}
	}
	wps->config_methods = wps_fix_config_methods(wps->config_methods);
	wps->dev.config_methods = wps->config_methods;

	if (wpa_s->conf->changed_parameters & CFG_CHANGED_DEVICE_TYPE)
		os_memcpy(wps->dev.pri_dev_type, wpa_s->conf->device_type,
			  WPS_DEV_TYPE_LEN);

	if (wpa_s->conf->changed_parameters & CFG_CHANGED_SEC_DEVICE_TYPE) {
		wps->dev.num_sec_dev_types = wpa_s->conf->num_sec_device_types;
		os_memcpy(wps->dev.sec_dev_type, wpa_s->conf->sec_device_type,
			  wps->dev.num_sec_dev_types * WPS_DEV_TYPE_LEN);
	}

	if (wpa_s->conf->changed_parameters & CFG_CHANGED_VENDOR_EXTENSION)
		wpas_wps_set_vendor_ext_m1(wpa_s, wps);

	if (wpa_s->conf->changed_parameters & CFG_CHANGED_OS_VERSION)
		wps->dev.os_version = WPA_GET_BE32(wpa_s->conf->os_version);

	if (wpa_s->conf->changed_parameters & CFG_CHANGED_UUID)
		wpas_wps_set_uuid(wpa_s, wps);

	if (wpa_s->conf->changed_parameters &
	    (CFG_CHANGED_DEVICE_NAME | CFG_CHANGED_WPS_STRING)) {
		/* Update pointers to make sure they refer current values */
		wps->dev.device_name = wpa_s->conf->device_name;
		wps->dev.manufacturer = wpa_s->conf->manufacturer;
		wps->dev.model_name = wpa_s->conf->model_name;
		wps->dev.model_number = wpa_s->conf->model_number;
		wps->dev.serial_number = wpa_s->conf->serial_number;
	}
}


#ifdef CONFIG_WPS_NFC

#ifdef CONFIG_WPS_ER
static struct wpabuf *
wpas_wps_network_config_token(struct wpa_supplicant *wpa_s, int ndef,
			      struct wpa_ssid *ssid)
{
	struct wpabuf *ret;
	struct wps_credential cred;

	if (wpas_wps_network_to_cred(ssid, &cred) < 0)
		return NULL;

	ret = wps_er_config_token_from_cred(wpa_s->wps, &cred);

	if (ndef && ret) {
		struct wpabuf *tmp;
		tmp = ndef_build_wifi(ret);
		wpabuf_free(ret);
		if (tmp == NULL)
			return NULL;
		ret = tmp;
	}

	return ret;
}
#endif /* CONFIG_WPS_ER */


struct wpabuf * wpas_wps_nfc_config_token(struct wpa_supplicant *wpa_s,
					  int ndef, const char *id_str)
{
#ifdef CONFIG_WPS_ER
	if (id_str) {
		int id;
		char *end = NULL;
		struct wpa_ssid *ssid;

		id = strtol(id_str, &end, 10);
		if (end && *end)
			return NULL;

		ssid = wpa_config_get_network(wpa_s->conf, id);
		if (ssid == NULL)
			return NULL;
		return wpas_wps_network_config_token(wpa_s, ndef, ssid);
	}
#endif /* CONFIG_WPS_ER */
#ifdef CONFIG_AP
	if (wpa_s->ap_iface)
		return wpas_ap_wps_nfc_config_token(wpa_s, ndef);
#endif /* CONFIG_AP */
	return NULL;
}


struct wpabuf * wpas_wps_nfc_token(struct wpa_supplicant *wpa_s, int ndef)
{
	if (wpa_s->conf->wps_nfc_pw_from_config) {
		return wps_nfc_token_build(ndef,
					   wpa_s->conf->wps_nfc_dev_pw_id,
					   wpa_s->conf->wps_nfc_dh_pubkey,
					   wpa_s->conf->wps_nfc_dev_pw);
	}

	return wps_nfc_token_gen(ndef, &wpa_s->conf->wps_nfc_dev_pw_id,
				 &wpa_s->conf->wps_nfc_dh_pubkey,
				 &wpa_s->conf->wps_nfc_dh_privkey,
				 &wpa_s->conf->wps_nfc_dev_pw);
}


int wpas_wps_start_nfc(struct wpa_supplicant *wpa_s, const u8 *go_dev_addr,
		       const u8 *bssid,
		       const struct wpabuf *dev_pw, u16 dev_pw_id,
		       int p2p_group, const u8 *peer_pubkey_hash,
		       const u8 *ssid, size_t ssid_len, int freq)
{
	struct wps_context *wps = wpa_s->wps;
	char pw[32 * 2 + 1];

	if (dev_pw_id != DEV_PW_NFC_CONNECTION_HANDOVER && dev_pw == NULL) {
		dev_pw = wpa_s->conf->wps_nfc_dev_pw;
		dev_pw_id = wpa_s->conf->wps_nfc_dev_pw_id;
	}

	if (wpa_s->conf->wps_nfc_dh_pubkey == NULL ||
	    wpa_s->conf->wps_nfc_dh_privkey == NULL) {
		wpa_printf(MSG_DEBUG, "WPS: Missing DH params - "
			   "cannot start NFC-triggered connection");
		return -1;
	}

	if (dev_pw_id != DEV_PW_NFC_CONNECTION_HANDOVER && dev_pw == NULL) {
		wpa_printf(MSG_DEBUG, "WPS: Missing Device Password (id=%u) - "
			   "cannot start NFC-triggered connection", dev_pw_id);
		return -1;
	}

	dh5_free(wps->dh_ctx);
	wpabuf_free(wps->dh_pubkey);
	wpabuf_free(wps->dh_privkey);
	wps->dh_privkey = wpabuf_dup(wpa_s->conf->wps_nfc_dh_privkey);
	wps->dh_pubkey = wpabuf_dup(wpa_s->conf->wps_nfc_dh_pubkey);
	if (wps->dh_privkey == NULL || wps->dh_pubkey == NULL) {
		wps->dh_ctx = NULL;
		wpabuf_free(wps->dh_pubkey);
		wps->dh_pubkey = NULL;
		wpabuf_free(wps->dh_privkey);
		wps->dh_privkey = NULL;
		wpa_printf(MSG_DEBUG, "WPS: Failed to get DH priv/pub key");
		return -1;
	}
	wps->dh_ctx = dh5_init_fixed(wps->dh_privkey, wps->dh_pubkey);
	if (wps->dh_ctx == NULL) {
		wpabuf_free(wps->dh_pubkey);
		wps->dh_pubkey = NULL;
		wpabuf_free(wps->dh_privkey);
		wps->dh_privkey = NULL;
		wpa_printf(MSG_DEBUG, "WPS: Failed to initialize DH context");
		return -1;
	}

	if (dev_pw) {
		wpa_snprintf_hex_uppercase(pw, sizeof(pw),
					   wpabuf_head(dev_pw),
					   wpabuf_len(dev_pw));
	}
	return wpas_wps_start_dev_pw(wpa_s, go_dev_addr, bssid,
				     dev_pw ? pw : NULL,
				     p2p_group, dev_pw_id, peer_pubkey_hash,
				     ssid, ssid_len, freq);
}


static int wpas_wps_use_cred(struct wpa_supplicant *wpa_s,
			     struct wps_parse_attr *attr)
{
	/*
	 * Disable existing networks temporarily to allow the newly learned
	 * credential to be preferred. Enable the temporarily disabled networks
	 * after 10 seconds.
	 */
	wpas_wps_temp_disable(wpa_s, NULL);
	eloop_register_timeout(10, 0, wpas_wps_reenable_networks_cb, wpa_s,
			       NULL);

	if (wps_oob_use_cred(wpa_s->wps, attr) < 0)
		return -1;

	if (wpa_s->wpa_state == WPA_INTERFACE_DISABLED)
		return 0;

	if (attr->ap_channel) {
		u16 chan = WPA_GET_BE16(attr->ap_channel);
		int freq = 0;

		if (chan >= 1 && chan <= 13)
			freq = 2407 + 5 * chan;
		else if (chan == 14)
			freq = 2484;
		else if (chan >= 30)
			freq = 5000 + 5 * chan;

		if (freq) {
			wpa_printf(MSG_DEBUG, "WPS: Credential container indicated AP channel %u -> %u MHz",
				   chan, freq);
			wpa_s->after_wps = 5;
			wpa_s->wps_freq = freq;
		}
	}

	wpa_printf(MSG_DEBUG, "WPS: Request reconnection with new network "
		   "based on the received credential added");
	wpa_s->normal_scans = 0;
	wpa_supplicant_reinit_autoscan(wpa_s);
	wpa_s->disconnected = 0;
	wpa_s->reassociate = 1;

	wpa_supplicant_cancel_sched_scan(wpa_s);
	wpa_supplicant_req_scan(wpa_s, 0, 0);

	return 0;
}


#ifdef CONFIG_WPS_ER
static int wpas_wps_add_nfc_password_token(struct wpa_supplicant *wpa_s,
					   struct wps_parse_attr *attr)
{
	return wps_registrar_add_nfc_password_token(
		wpa_s->wps->registrar, attr->oob_dev_password,
		attr->oob_dev_password_len);
}
#endif /* CONFIG_WPS_ER */


static int wpas_wps_nfc_tag_process(struct wpa_supplicant *wpa_s,
				    const struct wpabuf *wps)
{
	struct wps_parse_attr attr;

	wpa_hexdump_buf(MSG_DEBUG, "WPS: Received NFC tag payload", wps);

	if (wps_parse_msg(wps, &attr)) {
		wpa_printf(MSG_DEBUG, "WPS: Ignore invalid data from NFC tag");
		return -1;
	}

	if (attr.num_cred)
		return wpas_wps_use_cred(wpa_s, &attr);

#ifdef CONFIG_WPS_ER
	if (attr.oob_dev_password)
		return wpas_wps_add_nfc_password_token(wpa_s, &attr);
#endif /* CONFIG_WPS_ER */

	wpa_printf(MSG_DEBUG, "WPS: Ignore unrecognized NFC tag");
	return -1;
}


int wpas_wps_nfc_tag_read(struct wpa_supplicant *wpa_s,
			  const struct wpabuf *data, int forced_freq)
{
	const struct wpabuf *wps = data;
	struct wpabuf *tmp = NULL;
	int ret;

	if (wpabuf_len(data) < 4)
		return -1;

	if (*wpabuf_head_u8(data) != 0x10) {
		/* Assume this contains full NDEF record */
		tmp = ndef_parse_wifi(data);
		if (tmp == NULL) {
#ifdef CONFIG_P2P
			tmp = ndef_parse_p2p(data);
			if (tmp) {
				ret = wpas_p2p_nfc_tag_process(wpa_s, tmp,
							       forced_freq);
				wpabuf_free(tmp);
				return ret;
			}
#endif /* CONFIG_P2P */
			wpa_printf(MSG_DEBUG, "WPS: Could not parse NDEF");
			return -1;
		}
		wps = tmp;
	}

	ret = wpas_wps_nfc_tag_process(wpa_s, wps);
	wpabuf_free(tmp);
	return ret;
}


struct wpabuf * wpas_wps_nfc_handover_req(struct wpa_supplicant *wpa_s,
					  int ndef)
{
	struct wpabuf *ret;

	if (wpa_s->conf->wps_nfc_dh_pubkey == NULL &&
	    wps_nfc_gen_dh(&wpa_s->conf->wps_nfc_dh_pubkey,
			   &wpa_s->conf->wps_nfc_dh_privkey) < 0)
		return NULL;

	ret = wps_build_nfc_handover_req(wpa_s->wps,
					 wpa_s->conf->wps_nfc_dh_pubkey);

	if (ndef && ret) {
		struct wpabuf *tmp;
		tmp = ndef_build_wifi(ret);
		wpabuf_free(ret);
		if (tmp == NULL)
			return NULL;
		ret = tmp;
	}

	return ret;
}


#ifdef CONFIG_WPS_NFC

static struct wpabuf *
wpas_wps_er_nfc_handover_sel(struct wpa_supplicant *wpa_s, int ndef,
			     const char *uuid)
{
#ifdef CONFIG_WPS_ER
	struct wpabuf *ret;
	u8 u[UUID_LEN], *use_uuid = NULL;
	u8 addr[ETH_ALEN], *use_addr = NULL;
	struct wps_context *wps = wpa_s->wps;

	if (wps == NULL)
		return NULL;

	if (uuid == NULL)
		return NULL;
	if (uuid_str2bin(uuid, u) == 0)
		use_uuid = u;
	else if (hwaddr_aton(uuid, addr) == 0)
		use_addr = addr;
	else
		return NULL;

	if (wpa_s->conf->wps_nfc_dh_pubkey == NULL) {
		if (wps_nfc_gen_dh(&wpa_s->conf->wps_nfc_dh_pubkey,
				   &wpa_s->conf->wps_nfc_dh_privkey) < 0)
			return NULL;
	}

	wpas_wps_nfc_clear(wps);
	wps->ap_nfc_dev_pw_id = DEV_PW_NFC_CONNECTION_HANDOVER;
	wps->ap_nfc_dh_pubkey = wpabuf_dup(wpa_s->conf->wps_nfc_dh_pubkey);
	wps->ap_nfc_dh_privkey = wpabuf_dup(wpa_s->conf->wps_nfc_dh_privkey);
	if (!wps->ap_nfc_dh_pubkey || !wps->ap_nfc_dh_privkey) {
		wpas_wps_nfc_clear(wps);
		return NULL;
	}

	ret = wps_er_nfc_handover_sel(wpa_s->wps_er, wpa_s->wps, use_uuid,
				      use_addr, wpa_s->conf->wps_nfc_dh_pubkey);
	if (ndef && ret) {
		struct wpabuf *tmp;
		tmp = ndef_build_wifi(ret);
		wpabuf_free(ret);
		if (tmp == NULL)
			return NULL;
		ret = tmp;
	}

	return ret;
#else /* CONFIG_WPS_ER */
	return NULL;
#endif /* CONFIG_WPS_ER */
}
#endif /* CONFIG_WPS_NFC */


struct wpabuf * wpas_wps_nfc_handover_sel(struct wpa_supplicant *wpa_s,
					  int ndef, int cr, const char *uuid)
{
	struct wpabuf *ret;
	if (!cr)
		return NULL;
	ret = wpas_ap_wps_nfc_handover_sel(wpa_s, ndef);
	if (ret)
		return ret;
	return wpas_wps_er_nfc_handover_sel(wpa_s, ndef, uuid);
}


static int wpas_wps_nfc_rx_handover_sel(struct wpa_supplicant *wpa_s,
					const struct wpabuf *data)
{
	struct wpabuf *wps;
	int ret = -1;
	u16 wsc_len;
	const u8 *pos;
	struct wpabuf msg;
	struct wps_parse_attr attr;
	u16 dev_pw_id;
	const u8 *bssid = NULL;
	int freq = 0;

	wps = ndef_parse_wifi(data);
	if (wps == NULL)
		return -1;
	wpa_printf(MSG_DEBUG, "WPS: Received application/vnd.wfa.wsc "
		   "payload from NFC connection handover");
	wpa_hexdump_buf(MSG_DEBUG, "WPS: NFC payload", wps);
	if (wpabuf_len(wps) < 2) {
		wpa_printf(MSG_DEBUG, "WPS: Too short Wi-Fi Handover Select "
			   "Message");
		goto out;
	}
	pos = wpabuf_head(wps);
	wsc_len = WPA_GET_BE16(pos);
	if (wsc_len > wpabuf_len(wps) - 2) {
		wpa_printf(MSG_DEBUG, "WPS: Invalid WSC attribute length (%u) "
			   "in Wi-Fi Handover Select Message", wsc_len);
		goto out;
	}
	pos += 2;

	wpa_hexdump(MSG_DEBUG,
		    "WPS: WSC attributes in Wi-Fi Handover Select Message",
		    pos, wsc_len);
	if (wsc_len < wpabuf_len(wps) - 2) {
		wpa_hexdump(MSG_DEBUG,
			    "WPS: Ignore extra data after WSC attributes",
			    pos + wsc_len, wpabuf_len(wps) - 2 - wsc_len);
	}

	wpabuf_set(&msg, pos, wsc_len);
	ret = wps_parse_msg(&msg, &attr);
	if (ret < 0) {
		wpa_printf(MSG_DEBUG, "WPS: Could not parse WSC attributes in "
			   "Wi-Fi Handover Select Message");
		goto out;
	}

	if (attr.oob_dev_password == NULL ||
	    attr.oob_dev_password_len < WPS_OOB_PUBKEY_HASH_LEN + 2) {
		wpa_printf(MSG_DEBUG, "WPS: No Out-of-Band Device Password "
			   "included in Wi-Fi Handover Select Message");
		ret = -1;
		goto out;
	}

	if (attr.ssid == NULL) {
		wpa_printf(MSG_DEBUG, "WPS: No SSID included in Wi-Fi Handover "
			   "Select Message");
		ret = -1;
		goto out;
	}

	wpa_hexdump_ascii(MSG_DEBUG, "WPS: SSID", attr.ssid, attr.ssid_len);

	if (attr.mac_addr) {
		bssid = attr.mac_addr;
		wpa_printf(MSG_DEBUG, "WPS: MAC Address (BSSID): " MACSTR,
			   MAC2STR(bssid));
	}

	if (attr.rf_bands)
		wpa_printf(MSG_DEBUG, "WPS: RF Bands: %d", *attr.rf_bands);

	if (attr.ap_channel) {
		u16 chan = WPA_GET_BE16(attr.ap_channel);

		wpa_printf(MSG_DEBUG, "WPS: AP Channel: %d", chan);

		if (chan >= 1 && chan <= 13 &&
		    (attr.rf_bands == NULL || *attr.rf_bands & WPS_RF_24GHZ))
			freq = 2407 + 5 * chan;
		else if (chan == 14 &&
			 (attr.rf_bands == NULL ||
			  *attr.rf_bands & WPS_RF_24GHZ))
			freq = 2484;
		else if (chan >= 30 &&
			 (attr.rf_bands == NULL ||
			  *attr.rf_bands & WPS_RF_50GHZ))
			freq = 5000 + 5 * chan;
		else if (chan >= 1 && chan <= 4 &&
			 (attr.rf_bands == NULL ||
			  *attr.rf_bands & WPS_RF_60GHZ))
			freq = 56160 + 2160 * chan;

		if (freq) {
			wpa_printf(MSG_DEBUG,
				   "WPS: AP indicated channel %u -> %u MHz",
				   chan, freq);
		}
	}

	wpa_hexdump(MSG_DEBUG, "WPS: Out-of-Band Device Password",
		    attr.oob_dev_password, attr.oob_dev_password_len);
	dev_pw_id = WPA_GET_BE16(attr.oob_dev_password +
				 WPS_OOB_PUBKEY_HASH_LEN);
	if (dev_pw_id != DEV_PW_NFC_CONNECTION_HANDOVER) {
		wpa_printf(MSG_DEBUG, "WPS: Unexpected OOB Device Password ID "
			   "%u in Wi-Fi Handover Select Message", dev_pw_id);
		ret = -1;
		goto out;
	}
	wpa_hexdump(MSG_DEBUG, "WPS: AP Public Key hash",
		    attr.oob_dev_password, WPS_OOB_PUBKEY_HASH_LEN);

	ret = wpas_wps_start_nfc(wpa_s, NULL, bssid, NULL, dev_pw_id, 0,
				 attr.oob_dev_password,
				 attr.ssid, attr.ssid_len, freq);

out:
	wpabuf_free(wps);
	return ret;
}


int wpas_wps_nfc_report_handover(struct wpa_supplicant *wpa_s,
				 const struct wpabuf *req,
				 const struct wpabuf *sel)
{
	wpa_printf(MSG_DEBUG, "NFC: WPS connection handover reported");
	wpa_hexdump_buf_key(MSG_DEBUG, "WPS: Carrier record in request", req);
	wpa_hexdump_buf_key(MSG_DEBUG, "WPS: Carrier record in select", sel);
	return wpas_wps_nfc_rx_handover_sel(wpa_s, sel);
}


int wpas_er_wps_nfc_report_handover(struct wpa_supplicant *wpa_s,
				    const struct wpabuf *req,
				    const struct wpabuf *sel)
{
	struct wpabuf *wps;
	int ret = -1;
	u16 wsc_len;
	const u8 *pos;
	struct wpabuf msg;
	struct wps_parse_attr attr;
	u16 dev_pw_id;

	/*
	 * Enrollee/station is always initiator of the NFC connection handover,
	 * so use the request message here to find Enrollee public key hash.
	 */
	wps = ndef_parse_wifi(req);
	if (wps == NULL)
		return -1;
	wpa_printf(MSG_DEBUG, "WPS: Received application/vnd.wfa.wsc "
		   "payload from NFC connection handover");
	wpa_hexdump_buf(MSG_DEBUG, "WPS: NFC payload", wps);
	if (wpabuf_len(wps) < 2) {
		wpa_printf(MSG_DEBUG, "WPS: Too short Wi-Fi Handover Request "
			   "Message");
		goto out;
	}
	pos = wpabuf_head(wps);
	wsc_len = WPA_GET_BE16(pos);
	if (wsc_len > wpabuf_len(wps) - 2) {
		wpa_printf(MSG_DEBUG, "WPS: Invalid WSC attribute length (%u) "
			   "in rt Wi-Fi Handover Request Message", wsc_len);
		goto out;
	}
	pos += 2;

	wpa_hexdump(MSG_DEBUG,
		    "WPS: WSC attributes in Wi-Fi Handover Request Message",
		    pos, wsc_len);
	if (wsc_len < wpabuf_len(wps) - 2) {
		wpa_hexdump(MSG_DEBUG,
			    "WPS: Ignore extra data after WSC attributes",
			    pos + wsc_len, wpabuf_len(wps) - 2 - wsc_len);
	}

	wpabuf_set(&msg, pos, wsc_len);
	ret = wps_parse_msg(&msg, &attr);
	if (ret < 0) {
		wpa_printf(MSG_DEBUG, "WPS: Could not parse WSC attributes in "
			   "Wi-Fi Handover Request Message");
		goto out;
	}

	if (attr.oob_dev_password == NULL ||
	    attr.oob_dev_password_len < WPS_OOB_PUBKEY_HASH_LEN + 2) {
		wpa_printf(MSG_DEBUG, "WPS: No Out-of-Band Device Password "
			   "included in Wi-Fi Handover Request Message");
		ret = -1;
		goto out;
	}

	if (attr.uuid_e == NULL) {
		wpa_printf(MSG_DEBUG, "WPS: No UUID-E included in Wi-Fi "
			   "Handover Request Message");
		ret = -1;
		goto out;
	}

	wpa_hexdump(MSG_DEBUG, "WPS: UUID-E", attr.uuid_e, WPS_UUID_LEN);

	wpa_hexdump(MSG_DEBUG, "WPS: Out-of-Band Device Password",
		    attr.oob_dev_password, attr.oob_dev_password_len);
	dev_pw_id = WPA_GET_BE16(attr.oob_dev_password +
				 WPS_OOB_PUBKEY_HASH_LEN);
	if (dev_pw_id != DEV_PW_NFC_CONNECTION_HANDOVER) {
		wpa_printf(MSG_DEBUG, "WPS: Unexpected OOB Device Password ID "
			   "%u in Wi-Fi Handover Request Message", dev_pw_id);
		ret = -1;
		goto out;
	}
	wpa_hexdump(MSG_DEBUG, "WPS: Enrollee Public Key hash",
		    attr.oob_dev_password, WPS_OOB_PUBKEY_HASH_LEN);

	ret = wps_registrar_add_nfc_pw_token(wpa_s->wps->registrar,
					     attr.oob_dev_password,
					     DEV_PW_NFC_CONNECTION_HANDOVER,
					     NULL, 0, 1);

out:
	wpabuf_free(wps);
	return ret;
}

#endif /* CONFIG_WPS_NFC */


static void wpas_wps_dump_ap_info(struct wpa_supplicant *wpa_s)
{
	size_t i;
	struct os_reltime now;

	if (wpa_debug_level > MSG_DEBUG)
		return;

	if (wpa_s->wps_ap == NULL)
		return;

	os_get_reltime(&now);

	for (i = 0; i < wpa_s->num_wps_ap; i++) {
		struct wps_ap_info *ap = &wpa_s->wps_ap[i];
		struct wpa_blacklist *e = wpa_blacklist_get(wpa_s, ap->bssid);

		wpa_printf(MSG_DEBUG, "WPS: AP[%d] " MACSTR " type=%d "
			   "tries=%d last_attempt=%d sec ago blacklist=%d",
			   (int) i, MAC2STR(ap->bssid), ap->type, ap->tries,
			   ap->last_attempt.sec > 0 ?
			   (int) now.sec - (int) ap->last_attempt.sec : -1,
			   e ? e->count : 0);
	}
}


static struct wps_ap_info * wpas_wps_get_ap_info(struct wpa_supplicant *wpa_s,
						 const u8 *bssid)
{
	size_t i;

	if (wpa_s->wps_ap == NULL)
		return NULL;

	for (i = 0; i < wpa_s->num_wps_ap; i++) {
		struct wps_ap_info *ap = &wpa_s->wps_ap[i];
		if (os_memcmp(ap->bssid, bssid, ETH_ALEN) == 0)
			return ap;
	}

	return NULL;
}


static void wpas_wps_update_ap_info_bss(struct wpa_supplicant *wpa_s,
					struct wpa_scan_res *res)
{
	struct wpabuf *wps;
	enum wps_ap_info_type type;
	struct wps_ap_info *ap;
	int r, pbc_active;
	const u8 *uuid;

	if (wpa_scan_get_vendor_ie(res, WPS_IE_VENDOR_TYPE) == NULL)
		return;

	wps = wpa_scan_get_vendor_ie_multi(res, WPS_IE_VENDOR_TYPE);
	if (wps == NULL)
		return;

	r = wps_is_addr_authorized(wps, wpa_s->own_addr, 1);
	if (r == 2)
		type = WPS_AP_SEL_REG_OUR;
	else if (r == 1)
		type = WPS_AP_SEL_REG;
	else
		type = WPS_AP_NOT_SEL_REG;

	uuid = wps_get_uuid_e(wps);
	pbc_active = wps_is_selected_pbc_registrar(wps);

	ap = wpas_wps_get_ap_info(wpa_s, res->bssid);
	if (ap) {
		if (ap->type != type) {
			wpa_printf(MSG_DEBUG, "WPS: AP " MACSTR
				   " changed type %d -> %d",
				   MAC2STR(res->bssid), ap->type, type);
			ap->type = type;
			if (type != WPS_AP_NOT_SEL_REG)
				wpa_blacklist_del(wpa_s, ap->bssid);
		}
		ap->pbc_active = pbc_active;
		if (uuid)
			os_memcpy(ap->uuid, uuid, WPS_UUID_LEN);
		goto out;
	}

	ap = os_realloc_array(wpa_s->wps_ap, wpa_s->num_wps_ap + 1,
			      sizeof(struct wps_ap_info));
	if (ap == NULL)
		goto out;

	wpa_s->wps_ap = ap;
	ap = &wpa_s->wps_ap[wpa_s->num_wps_ap];
	wpa_s->num_wps_ap++;

	os_memset(ap, 0, sizeof(*ap));
	os_memcpy(ap->bssid, res->bssid, ETH_ALEN);
	ap->type = type;
	ap->pbc_active = pbc_active;
	if (uuid)
		os_memcpy(ap->uuid, uuid, WPS_UUID_LEN);
	wpa_printf(MSG_DEBUG, "WPS: AP " MACSTR " type %d added",
		   MAC2STR(ap->bssid), ap->type);

out:
	wpabuf_free(wps);
}


void wpas_wps_update_ap_info(struct wpa_supplicant *wpa_s,
			     struct wpa_scan_results *scan_res)
{
	size_t i;

	for (i = 0; i < scan_res->num; i++)
		wpas_wps_update_ap_info_bss(wpa_s, scan_res->res[i]);

	wpas_wps_dump_ap_info(wpa_s);
}


void wpas_wps_notify_assoc(struct wpa_supplicant *wpa_s, const u8 *bssid)
{
	struct wps_ap_info *ap;

	wpa_s->after_wps = 0;

	if (!wpa_s->wps_ap_iter)
		return;
	ap = wpas_wps_get_ap_info(wpa_s, bssid);
	if (ap == NULL)
		return;
	ap->tries++;
	os_get_reltime(&ap->last_attempt);
}
