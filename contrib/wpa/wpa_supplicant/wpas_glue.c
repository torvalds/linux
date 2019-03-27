/*
 * WPA Supplicant - Glue code to setup EAPOL and RSN modules
 * Copyright (c) 2003-2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "eapol_supp/eapol_supp_sm.h"
#include "eap_peer/eap.h"
#include "rsn_supp/wpa.h"
#include "eloop.h"
#include "config.h"
#include "l2_packet/l2_packet.h"
#include "common/wpa_common.h"
#include "wpa_supplicant_i.h"
#include "driver_i.h"
#include "rsn_supp/pmksa_cache.h"
#include "sme.h"
#include "common/ieee802_11_defs.h"
#include "common/wpa_ctrl.h"
#include "wpas_glue.h"
#include "wps_supplicant.h"
#include "bss.h"
#include "scan.h"
#include "notify.h"
#include "wpas_kay.h"


#ifndef CONFIG_NO_CONFIG_BLOBS
#if defined(IEEE8021X_EAPOL) || !defined(CONFIG_NO_WPA)
static void wpa_supplicant_set_config_blob(void *ctx,
					   struct wpa_config_blob *blob)
{
	struct wpa_supplicant *wpa_s = ctx;
	wpa_config_set_blob(wpa_s->conf, blob);
	if (wpa_s->conf->update_config) {
		int ret = wpa_config_write(wpa_s->confname, wpa_s->conf);
		if (ret) {
			wpa_printf(MSG_DEBUG, "Failed to update config after "
				   "blob set");
		}
	}
}


static const struct wpa_config_blob *
wpa_supplicant_get_config_blob(void *ctx, const char *name)
{
	struct wpa_supplicant *wpa_s = ctx;
	return wpa_config_get_blob(wpa_s->conf, name);
}
#endif /* defined(IEEE8021X_EAPOL) || !defined(CONFIG_NO_WPA) */
#endif /* CONFIG_NO_CONFIG_BLOBS */


#if defined(IEEE8021X_EAPOL) || !defined(CONFIG_NO_WPA)
static u8 * wpa_alloc_eapol(const struct wpa_supplicant *wpa_s, u8 type,
			    const void *data, u16 data_len,
			    size_t *msg_len, void **data_pos)
{
	struct ieee802_1x_hdr *hdr;

	*msg_len = sizeof(*hdr) + data_len;
	hdr = os_malloc(*msg_len);
	if (hdr == NULL)
		return NULL;

	hdr->version = wpa_s->conf->eapol_version;
	hdr->type = type;
	hdr->length = host_to_be16(data_len);

	if (data)
		os_memcpy(hdr + 1, data, data_len);
	else
		os_memset(hdr + 1, 0, data_len);

	if (data_pos)
		*data_pos = hdr + 1;

	return (u8 *) hdr;
}


/**
 * wpa_ether_send - Send Ethernet frame
 * @wpa_s: Pointer to wpa_supplicant data
 * @dest: Destination MAC address
 * @proto: Ethertype in host byte order
 * @buf: Frame payload starting from IEEE 802.1X header
 * @len: Frame payload length
 * Returns: >=0 on success, <0 on failure
 */
static int wpa_ether_send(struct wpa_supplicant *wpa_s, const u8 *dest,
			  u16 proto, const u8 *buf, size_t len)
{
#ifdef CONFIG_TESTING_OPTIONS
	if (wpa_s->ext_eapol_frame_io && proto == ETH_P_EAPOL) {
		size_t hex_len = 2 * len + 1;
		char *hex = os_malloc(hex_len);

		if (hex == NULL)
			return -1;
		wpa_snprintf_hex(hex, hex_len, buf, len);
		wpa_msg(wpa_s, MSG_INFO, "EAPOL-TX " MACSTR " %s",
			MAC2STR(dest), hex);
		os_free(hex);
		return 0;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	if (wpa_s->l2) {
		return l2_packet_send(wpa_s->l2, dest, proto, buf, len);
	}

	return -1;
}
#endif /* IEEE8021X_EAPOL || !CONFIG_NO_WPA */


#ifdef IEEE8021X_EAPOL

/**
 * wpa_supplicant_eapol_send - Send IEEE 802.1X EAPOL packet to Authenticator
 * @ctx: Pointer to wpa_supplicant data (wpa_s)
 * @type: IEEE 802.1X packet type (IEEE802_1X_TYPE_*)
 * @buf: EAPOL payload (after IEEE 802.1X header)
 * @len: EAPOL payload length
 * Returns: >=0 on success, <0 on failure
 *
 * This function adds Ethernet and IEEE 802.1X header and sends the EAPOL frame
 * to the current Authenticator.
 */
static int wpa_supplicant_eapol_send(void *ctx, int type, const u8 *buf,
				     size_t len)
{
	struct wpa_supplicant *wpa_s = ctx;
	u8 *msg, *dst, bssid[ETH_ALEN];
	size_t msglen;
	int res;

	/* TODO: could add l2_packet_sendmsg that allows fragments to avoid
	 * extra copy here */

	if (wpa_key_mgmt_wpa_psk(wpa_s->key_mgmt) ||
	    wpa_s->key_mgmt == WPA_KEY_MGMT_OWE ||
	    wpa_s->key_mgmt == WPA_KEY_MGMT_DPP ||
	    wpa_s->key_mgmt == WPA_KEY_MGMT_NONE) {
		/* Current SSID is not using IEEE 802.1X/EAP, so drop possible
		 * EAPOL frames (mainly, EAPOL-Start) from EAPOL state
		 * machines. */
		wpa_printf(MSG_DEBUG, "WPA: drop TX EAPOL in non-IEEE 802.1X "
			   "mode (type=%d len=%lu)", type,
			   (unsigned long) len);
		return -1;
	}

	if (pmksa_cache_get_current(wpa_s->wpa) &&
	    type == IEEE802_1X_TYPE_EAPOL_START) {
		/*
		 * We were trying to use PMKSA caching and sending EAPOL-Start
		 * would abort that and trigger full EAPOL authentication.
		 * However, we've already waited for the AP/Authenticator to
		 * start 4-way handshake or EAP authentication, and apparently
		 * it has not done so since the startWhen timer has reached zero
		 * to get the state machine sending EAPOL-Start. This is not
		 * really supposed to happen, but an interoperability issue with
		 * a deployed AP has been identified where the connection fails
		 * due to that AP failing to operate correctly if PMKID is
		 * included in the Association Request frame. To work around
		 * this, assume PMKSA caching failed and try to initiate full
		 * EAP authentication.
		 */
		if (!wpa_s->current_ssid ||
		    wpa_s->current_ssid->eap_workaround) {
			wpa_printf(MSG_DEBUG,
				   "RSN: Timeout on waiting for the AP to initiate 4-way handshake for PMKSA caching or EAP authentication - try to force it to start EAP authentication");
		} else {
			wpa_printf(MSG_DEBUG,
				   "RSN: PMKSA caching - do not send EAPOL-Start");
			return -1;
		}
	}

	if (is_zero_ether_addr(wpa_s->bssid)) {
		wpa_printf(MSG_DEBUG, "BSSID not set when trying to send an "
			   "EAPOL frame");
		if (wpa_drv_get_bssid(wpa_s, bssid) == 0 &&
		    !is_zero_ether_addr(bssid)) {
			dst = bssid;
			wpa_printf(MSG_DEBUG, "Using current BSSID " MACSTR
				   " from the driver as the EAPOL destination",
				   MAC2STR(dst));
		} else {
			dst = wpa_s->last_eapol_src;
			wpa_printf(MSG_DEBUG, "Using the source address of the"
				   " last received EAPOL frame " MACSTR " as "
				   "the EAPOL destination",
				   MAC2STR(dst));
		}
	} else {
		/* BSSID was already set (from (Re)Assoc event, so use it as
		 * the EAPOL destination. */
		dst = wpa_s->bssid;
	}

	msg = wpa_alloc_eapol(wpa_s, type, buf, len, &msglen, NULL);
	if (msg == NULL)
		return -1;

	wpa_printf(MSG_DEBUG, "TX EAPOL: dst=" MACSTR, MAC2STR(dst));
	wpa_hexdump(MSG_MSGDUMP, "TX EAPOL", msg, msglen);
	res = wpa_ether_send(wpa_s, dst, ETH_P_EAPOL, msg, msglen);
	os_free(msg);
	return res;
}


/**
 * wpa_eapol_set_wep_key - set WEP key for the driver
 * @ctx: Pointer to wpa_supplicant data (wpa_s)
 * @unicast: 1 = individual unicast key, 0 = broadcast key
 * @keyidx: WEP key index (0..3)
 * @key: Pointer to key data
 * @keylen: Key length in bytes
 * Returns: 0 on success or < 0 on error.
 */
static int wpa_eapol_set_wep_key(void *ctx, int unicast, int keyidx,
				 const u8 *key, size_t keylen)
{
	struct wpa_supplicant *wpa_s = ctx;
	if (wpa_s->key_mgmt == WPA_KEY_MGMT_IEEE8021X_NO_WPA) {
		int cipher = (keylen == 5) ? WPA_CIPHER_WEP40 :
			WPA_CIPHER_WEP104;
		if (unicast)
			wpa_s->pairwise_cipher = cipher;
		else
			wpa_s->group_cipher = cipher;
	}
	return wpa_drv_set_key(wpa_s, WPA_ALG_WEP,
			       unicast ? wpa_s->bssid : NULL,
			       keyidx, unicast, NULL, 0, key, keylen);
}


static void wpa_supplicant_aborted_cached(void *ctx)
{
	struct wpa_supplicant *wpa_s = ctx;
	wpa_sm_aborted_cached(wpa_s->wpa);
}


static const char * result_str(enum eapol_supp_result result)
{
	switch (result) {
	case EAPOL_SUPP_RESULT_FAILURE:
		return "FAILURE";
	case EAPOL_SUPP_RESULT_SUCCESS:
		return "SUCCESS";
	case EAPOL_SUPP_RESULT_EXPECTED_FAILURE:
		return "EXPECTED_FAILURE";
	}
	return "?";
}


static void wpa_supplicant_eapol_cb(struct eapol_sm *eapol,
				    enum eapol_supp_result result,
				    void *ctx)
{
	struct wpa_supplicant *wpa_s = ctx;
	int res, pmk_len;
	u8 pmk[PMK_LEN];

	wpa_printf(MSG_DEBUG, "EAPOL authentication completed - result=%s",
		   result_str(result));

	if (wpas_wps_eapol_cb(wpa_s) > 0)
		return;

	wpa_s->eap_expected_failure = result ==
		EAPOL_SUPP_RESULT_EXPECTED_FAILURE;

	if (result != EAPOL_SUPP_RESULT_SUCCESS) {
		/*
		 * Make sure we do not get stuck here waiting for long EAPOL
		 * timeout if the AP does not disconnect in case of
		 * authentication failure.
		 */
		wpa_supplicant_req_auth_timeout(wpa_s, 2, 0);
	} else {
		ieee802_1x_notify_create_actor(wpa_s, wpa_s->last_eapol_src);
	}

	if (result != EAPOL_SUPP_RESULT_SUCCESS ||
	    !(wpa_s->drv_flags & WPA_DRIVER_FLAGS_4WAY_HANDSHAKE))
		return;

	if (!wpa_key_mgmt_wpa_ieee8021x(wpa_s->key_mgmt))
		return;

	wpa_printf(MSG_DEBUG, "Configure PMK for driver-based RSN 4-way "
		   "handshake");

	pmk_len = PMK_LEN;
	if (wpa_key_mgmt_ft(wpa_s->key_mgmt)) {
#ifdef CONFIG_IEEE80211R
		u8 buf[2 * PMK_LEN];
		wpa_printf(MSG_DEBUG, "RSN: Use FT XXKey as PMK for "
			   "driver-based 4-way hs and FT");
		res = eapol_sm_get_key(eapol, buf, 2 * PMK_LEN);
		if (res == 0) {
			os_memcpy(pmk, buf + PMK_LEN, PMK_LEN);
			os_memset(buf, 0, sizeof(buf));
		}
#else /* CONFIG_IEEE80211R */
		res = -1;
#endif /* CONFIG_IEEE80211R */
	} else {
		res = eapol_sm_get_key(eapol, pmk, PMK_LEN);
		if (res) {
			/*
			 * EAP-LEAP is an exception from other EAP methods: it
			 * uses only 16-byte PMK.
			 */
			res = eapol_sm_get_key(eapol, pmk, 16);
			pmk_len = 16;
		}
	}

	if (res) {
		wpa_printf(MSG_DEBUG, "Failed to get PMK from EAPOL state "
			   "machines");
		return;
	}

	wpa_hexdump_key(MSG_DEBUG, "RSN: Configure PMK for driver-based 4-way "
			"handshake", pmk, pmk_len);

	if (wpa_drv_set_key(wpa_s, WPA_ALG_PMK, NULL, 0, 0, NULL, 0, pmk,
			    pmk_len)) {
		wpa_printf(MSG_DEBUG, "Failed to set PMK to the driver");
	}

	wpa_supplicant_cancel_scan(wpa_s);
	wpa_supplicant_cancel_auth_timeout(wpa_s);
	wpa_supplicant_set_state(wpa_s, WPA_COMPLETED);

}


static void wpa_supplicant_notify_eapol_done(void *ctx)
{
	struct wpa_supplicant *wpa_s = ctx;
	wpa_msg(wpa_s, MSG_DEBUG, "WPA: EAPOL processing complete");
	if (wpa_key_mgmt_wpa_ieee8021x(wpa_s->key_mgmt)) {
		wpa_supplicant_set_state(wpa_s, WPA_4WAY_HANDSHAKE);
	} else {
		wpa_supplicant_cancel_auth_timeout(wpa_s);
		wpa_supplicant_set_state(wpa_s, WPA_COMPLETED);
	}
}

#endif /* IEEE8021X_EAPOL */


#ifndef CONFIG_NO_WPA

static int wpa_get_beacon_ie(struct wpa_supplicant *wpa_s)
{
	int ret = 0;
	struct wpa_bss *curr = NULL, *bss;
	struct wpa_ssid *ssid = wpa_s->current_ssid;
	const u8 *ie;

	dl_list_for_each(bss, &wpa_s->bss, struct wpa_bss, list) {
		if (os_memcmp(bss->bssid, wpa_s->bssid, ETH_ALEN) != 0)
			continue;
		if (ssid == NULL ||
		    ((bss->ssid_len == ssid->ssid_len &&
		      os_memcmp(bss->ssid, ssid->ssid, ssid->ssid_len) == 0) ||
		     ssid->ssid_len == 0)) {
			curr = bss;
			break;
		}
	}

	if (curr) {
		ie = wpa_bss_get_vendor_ie(curr, WPA_IE_VENDOR_TYPE);
		if (wpa_sm_set_ap_wpa_ie(wpa_s->wpa, ie, ie ? 2 + ie[1] : 0))
			ret = -1;

		ie = wpa_bss_get_ie(curr, WLAN_EID_RSN);
		if (wpa_sm_set_ap_rsn_ie(wpa_s->wpa, ie, ie ? 2 + ie[1] : 0))
			ret = -1;
	} else {
		ret = -1;
	}

	return ret;
}


static int wpa_supplicant_get_beacon_ie(void *ctx)
{
	struct wpa_supplicant *wpa_s = ctx;
	if (wpa_get_beacon_ie(wpa_s) == 0) {
		return 0;
	}

	/* No WPA/RSN IE found in the cached scan results. Try to get updated
	 * scan results from the driver. */
	if (wpa_supplicant_update_scan_results(wpa_s) < 0)
		return -1;

	return wpa_get_beacon_ie(wpa_s);
}


static u8 * _wpa_alloc_eapol(void *wpa_s, u8 type,
			     const void *data, u16 data_len,
			     size_t *msg_len, void **data_pos)
{
	return wpa_alloc_eapol(wpa_s, type, data, data_len, msg_len, data_pos);
}


static int _wpa_ether_send(void *wpa_s, const u8 *dest, u16 proto,
			   const u8 *buf, size_t len)
{
	return wpa_ether_send(wpa_s, dest, proto, buf, len);
}


static void _wpa_supplicant_cancel_auth_timeout(void *wpa_s)
{
	wpa_supplicant_cancel_auth_timeout(wpa_s);
}


static void _wpa_supplicant_set_state(void *wpa_s, enum wpa_states state)
{
	wpa_supplicant_set_state(wpa_s, state);
}


/**
 * wpa_supplicant_get_state - Get the connection state
 * @wpa_s: Pointer to wpa_supplicant data
 * Returns: The current connection state (WPA_*)
 */
static enum wpa_states wpa_supplicant_get_state(struct wpa_supplicant *wpa_s)
{
	return wpa_s->wpa_state;
}


static enum wpa_states _wpa_supplicant_get_state(void *wpa_s)
{
	return wpa_supplicant_get_state(wpa_s);
}


static void _wpa_supplicant_deauthenticate(void *wpa_s, int reason_code)
{
	wpa_supplicant_deauthenticate(wpa_s, reason_code);
	/* Schedule a scan to make sure we continue looking for networks */
	wpa_supplicant_req_scan(wpa_s, 5, 0);
}


static void * wpa_supplicant_get_network_ctx(void *wpa_s)
{
	return wpa_supplicant_get_ssid(wpa_s);
}


static int wpa_supplicant_get_bssid(void *ctx, u8 *bssid)
{
	struct wpa_supplicant *wpa_s = ctx;
	return wpa_drv_get_bssid(wpa_s, bssid);
}


static int wpa_supplicant_set_key(void *_wpa_s, enum wpa_alg alg,
				  const u8 *addr, int key_idx, int set_tx,
				  const u8 *seq, size_t seq_len,
				  const u8 *key, size_t key_len)
{
	struct wpa_supplicant *wpa_s = _wpa_s;
	if (alg == WPA_ALG_TKIP && key_idx == 0 && key_len == 32) {
		/* Clear the MIC error counter when setting a new PTK. */
		wpa_s->mic_errors_seen = 0;
	}
#ifdef CONFIG_TESTING_GET_GTK
	if (key_idx > 0 && addr && is_broadcast_ether_addr(addr) &&
	    alg != WPA_ALG_NONE && key_len <= sizeof(wpa_s->last_gtk)) {
		os_memcpy(wpa_s->last_gtk, key, key_len);
		wpa_s->last_gtk_len = key_len;
	}
#endif /* CONFIG_TESTING_GET_GTK */
#ifdef CONFIG_TESTING_OPTIONS
	if (addr && !is_broadcast_ether_addr(addr)) {
		wpa_s->last_tk_alg = alg;
		os_memcpy(wpa_s->last_tk_addr, addr, ETH_ALEN);
		wpa_s->last_tk_key_idx = key_idx;
		if (key)
			os_memcpy(wpa_s->last_tk, key, key_len);
		wpa_s->last_tk_len = key_len;
	}
#endif /* CONFIG_TESTING_OPTIONS */
	return wpa_drv_set_key(wpa_s, alg, addr, key_idx, set_tx, seq, seq_len,
			       key, key_len);
}


static int wpa_supplicant_mlme_setprotection(void *wpa_s, const u8 *addr,
					     int protection_type,
					     int key_type)
{
	return wpa_drv_mlme_setprotection(wpa_s, addr, protection_type,
					  key_type);
}


static struct wpa_ssid * wpas_get_network_ctx(struct wpa_supplicant *wpa_s,
					      void *network_ctx)
{
	struct wpa_ssid *ssid;

	for (ssid = wpa_s->conf->ssid; ssid; ssid = ssid->next) {
		if (network_ctx == ssid)
			return ssid;
	}

	return NULL;
}


static int wpa_supplicant_add_pmkid(void *_wpa_s, void *network_ctx,
				    const u8 *bssid, const u8 *pmkid,
				    const u8 *fils_cache_id,
				    const u8 *pmk, size_t pmk_len)
{
	struct wpa_supplicant *wpa_s = _wpa_s;
	struct wpa_ssid *ssid;
	struct wpa_pmkid_params params;

	os_memset(&params, 0, sizeof(params));
	ssid = wpas_get_network_ctx(wpa_s, network_ctx);
	if (ssid)
		wpa_msg(wpa_s, MSG_INFO, PMKSA_CACHE_ADDED MACSTR " %d",
			MAC2STR(bssid), ssid->id);
	if (ssid && fils_cache_id) {
		params.ssid = ssid->ssid;
		params.ssid_len = ssid->ssid_len;
		params.fils_cache_id = fils_cache_id;
	} else {
		params.bssid = bssid;
	}

	params.pmkid = pmkid;
	params.pmk = pmk;
	params.pmk_len = pmk_len;

	return wpa_drv_add_pmkid(wpa_s, &params);
}


static int wpa_supplicant_remove_pmkid(void *_wpa_s, void *network_ctx,
				       const u8 *bssid, const u8 *pmkid,
				       const u8 *fils_cache_id)
{
	struct wpa_supplicant *wpa_s = _wpa_s;
	struct wpa_ssid *ssid;
	struct wpa_pmkid_params params;

	os_memset(&params, 0, sizeof(params));
	ssid = wpas_get_network_ctx(wpa_s, network_ctx);
	if (ssid)
		wpa_msg(wpa_s, MSG_INFO, PMKSA_CACHE_REMOVED MACSTR " %d",
			MAC2STR(bssid), ssid->id);
	if (ssid && fils_cache_id) {
		params.ssid = ssid->ssid;
		params.ssid_len = ssid->ssid_len;
		params.fils_cache_id = fils_cache_id;
	} else {
		params.bssid = bssid;
	}

	params.pmkid = pmkid;

	return wpa_drv_remove_pmkid(wpa_s, &params);
}


#ifdef CONFIG_IEEE80211R
static int wpa_supplicant_update_ft_ies(void *ctx, const u8 *md,
					const u8 *ies, size_t ies_len)
{
	struct wpa_supplicant *wpa_s = ctx;
	if (wpa_s->drv_flags & WPA_DRIVER_FLAGS_SME)
		return sme_update_ft_ies(wpa_s, md, ies, ies_len);
	return wpa_drv_update_ft_ies(wpa_s, md, ies, ies_len);
}


static int wpa_supplicant_send_ft_action(void *ctx, u8 action,
					 const u8 *target_ap,
					 const u8 *ies, size_t ies_len)
{
	struct wpa_supplicant *wpa_s = ctx;
	int ret;
	u8 *data, *pos;
	size_t data_len;

	if (action != 1) {
		wpa_printf(MSG_ERROR, "Unsupported send_ft_action action %d",
			   action);
		return -1;
	}

	/*
	 * Action frame payload:
	 * Category[1] = 6 (Fast BSS Transition)
	 * Action[1] = 1 (Fast BSS Transition Request)
	 * STA Address
	 * Target AP Address
	 * FT IEs
	 */

	data_len = 2 + 2 * ETH_ALEN + ies_len;
	data = os_malloc(data_len);
	if (data == NULL)
		return -1;
	pos = data;
	*pos++ = 0x06; /* FT Action category */
	*pos++ = action;
	os_memcpy(pos, wpa_s->own_addr, ETH_ALEN);
	pos += ETH_ALEN;
	os_memcpy(pos, target_ap, ETH_ALEN);
	pos += ETH_ALEN;
	os_memcpy(pos, ies, ies_len);

	ret = wpa_drv_send_action(wpa_s, wpa_s->assoc_freq, 0,
				  wpa_s->bssid, wpa_s->own_addr, wpa_s->bssid,
				  data, data_len, 0);
	os_free(data);

	return ret;
}


static int wpa_supplicant_mark_authenticated(void *ctx, const u8 *target_ap)
{
	struct wpa_supplicant *wpa_s = ctx;
	struct wpa_driver_auth_params params;
	struct wpa_bss *bss;

	bss = wpa_bss_get_bssid(wpa_s, target_ap);
	if (bss == NULL)
		return -1;

	os_memset(&params, 0, sizeof(params));
	params.bssid = target_ap;
	params.freq = bss->freq;
	params.ssid = bss->ssid;
	params.ssid_len = bss->ssid_len;
	params.auth_alg = WPA_AUTH_ALG_FT;
	params.local_state_change = 1;
	return wpa_drv_authenticate(wpa_s, &params);
}
#endif /* CONFIG_IEEE80211R */


#ifdef CONFIG_TDLS

static int wpa_supplicant_tdls_get_capa(void *ctx, int *tdls_supported,
					int *tdls_ext_setup,
					int *tdls_chan_switch)
{
	struct wpa_supplicant *wpa_s = ctx;

	*tdls_supported = 0;
	*tdls_ext_setup = 0;
	*tdls_chan_switch = 0;

	if (!wpa_s->drv_capa_known)
		return -1;

	if (wpa_s->drv_flags & WPA_DRIVER_FLAGS_TDLS_SUPPORT)
		*tdls_supported = 1;

	if (wpa_s->drv_flags & WPA_DRIVER_FLAGS_TDLS_EXTERNAL_SETUP)
		*tdls_ext_setup = 1;

	if (wpa_s->drv_flags & WPA_DRIVER_FLAGS_TDLS_CHANNEL_SWITCH)
		*tdls_chan_switch = 1;

	return 0;
}


static int wpa_supplicant_send_tdls_mgmt(void *ctx, const u8 *dst,
					 u8 action_code, u8 dialog_token,
					 u16 status_code, u32 peer_capab,
					 int initiator, const u8 *buf,
					 size_t len)
{
	struct wpa_supplicant *wpa_s = ctx;
	return wpa_drv_send_tdls_mgmt(wpa_s, dst, action_code, dialog_token,
				      status_code, peer_capab, initiator, buf,
				      len);
}


static int wpa_supplicant_tdls_oper(void *ctx, int oper, const u8 *peer)
{
	struct wpa_supplicant *wpa_s = ctx;
	return wpa_drv_tdls_oper(wpa_s, oper, peer);
}


static int wpa_supplicant_tdls_peer_addset(
	void *ctx, const u8 *peer, int add, u16 aid, u16 capability,
	const u8 *supp_rates, size_t supp_rates_len,
	const struct ieee80211_ht_capabilities *ht_capab,
	const struct ieee80211_vht_capabilities *vht_capab,
	u8 qosinfo, int wmm, const u8 *ext_capab, size_t ext_capab_len,
	const u8 *supp_channels, size_t supp_channels_len,
	const u8 *supp_oper_classes, size_t supp_oper_classes_len)
{
	struct wpa_supplicant *wpa_s = ctx;
	struct hostapd_sta_add_params params;

	os_memset(&params, 0, sizeof(params));

	params.addr = peer;
	params.aid = aid;
	params.capability = capability;
	params.flags = WPA_STA_TDLS_PEER | WPA_STA_AUTHORIZED;

	/*
	 * Don't rely only on qosinfo for WMM capability. It may be 0 even when
	 * present. Allow the WMM IE to also indicate QoS support.
	 */
	if (wmm || qosinfo)
		params.flags |= WPA_STA_WMM;

	params.ht_capabilities = ht_capab;
	params.vht_capabilities = vht_capab;
	params.qosinfo = qosinfo;
	params.listen_interval = 0;
	params.supp_rates = supp_rates;
	params.supp_rates_len = supp_rates_len;
	params.set = !add;
	params.ext_capab = ext_capab;
	params.ext_capab_len = ext_capab_len;
	params.supp_channels = supp_channels;
	params.supp_channels_len = supp_channels_len;
	params.supp_oper_classes = supp_oper_classes;
	params.supp_oper_classes_len = supp_oper_classes_len;

	return wpa_drv_sta_add(wpa_s, &params);
}


static int wpa_supplicant_tdls_enable_channel_switch(
	void *ctx, const u8 *addr, u8 oper_class,
	const struct hostapd_freq_params *params)
{
	struct wpa_supplicant *wpa_s = ctx;

	return wpa_drv_tdls_enable_channel_switch(wpa_s, addr, oper_class,
						  params);
}


static int wpa_supplicant_tdls_disable_channel_switch(void *ctx, const u8 *addr)
{
	struct wpa_supplicant *wpa_s = ctx;

	return wpa_drv_tdls_disable_channel_switch(wpa_s, addr);
}

#endif /* CONFIG_TDLS */

#endif /* CONFIG_NO_WPA */


enum wpa_ctrl_req_type wpa_supplicant_ctrl_req_from_string(const char *field)
{
	if (os_strcmp(field, "IDENTITY") == 0)
		return WPA_CTRL_REQ_EAP_IDENTITY;
	else if (os_strcmp(field, "PASSWORD") == 0)
		return WPA_CTRL_REQ_EAP_PASSWORD;
	else if (os_strcmp(field, "NEW_PASSWORD") == 0)
		return WPA_CTRL_REQ_EAP_NEW_PASSWORD;
	else if (os_strcmp(field, "PIN") == 0)
		return WPA_CTRL_REQ_EAP_PIN;
	else if (os_strcmp(field, "OTP") == 0)
		return WPA_CTRL_REQ_EAP_OTP;
	else if (os_strcmp(field, "PASSPHRASE") == 0)
		return WPA_CTRL_REQ_EAP_PASSPHRASE;
	else if (os_strcmp(field, "SIM") == 0)
		return WPA_CTRL_REQ_SIM;
	else if (os_strcmp(field, "PSK_PASSPHRASE") == 0)
		return WPA_CTRL_REQ_PSK_PASSPHRASE;
	else if (os_strcmp(field, "EXT_CERT_CHECK") == 0)
		return WPA_CTRL_REQ_EXT_CERT_CHECK;
	return WPA_CTRL_REQ_UNKNOWN;
}


const char * wpa_supplicant_ctrl_req_to_string(enum wpa_ctrl_req_type field,
					       const char *default_txt,
					       const char **txt)
{
	const char *ret = NULL;

	*txt = default_txt;

	switch (field) {
	case WPA_CTRL_REQ_EAP_IDENTITY:
		*txt = "Identity";
		ret = "IDENTITY";
		break;
	case WPA_CTRL_REQ_EAP_PASSWORD:
		*txt = "Password";
		ret = "PASSWORD";
		break;
	case WPA_CTRL_REQ_EAP_NEW_PASSWORD:
		*txt = "New Password";
		ret = "NEW_PASSWORD";
		break;
	case WPA_CTRL_REQ_EAP_PIN:
		*txt = "PIN";
		ret = "PIN";
		break;
	case WPA_CTRL_REQ_EAP_OTP:
		ret = "OTP";
		break;
	case WPA_CTRL_REQ_EAP_PASSPHRASE:
		*txt = "Private key passphrase";
		ret = "PASSPHRASE";
		break;
	case WPA_CTRL_REQ_SIM:
		ret = "SIM";
		break;
	case WPA_CTRL_REQ_PSK_PASSPHRASE:
		*txt = "PSK or passphrase";
		ret = "PSK_PASSPHRASE";
		break;
	case WPA_CTRL_REQ_EXT_CERT_CHECK:
		*txt = "External server certificate validation";
		ret = "EXT_CERT_CHECK";
		break;
	default:
		break;
	}

	/* txt needs to be something */
	if (*txt == NULL) {
		wpa_printf(MSG_WARNING, "No message for request %d", field);
		ret = NULL;
	}

	return ret;
}


void wpas_send_ctrl_req(struct wpa_supplicant *wpa_s, struct wpa_ssid *ssid,
			const char *field_name, const char *txt)
{
	char *buf;
	size_t buflen;
	int len;

	buflen = 100 + os_strlen(txt) + ssid->ssid_len;
	buf = os_malloc(buflen);
	if (buf == NULL)
		return;
	len = os_snprintf(buf, buflen, "%s-%d:%s needed for SSID ",
			  field_name, ssid->id, txt);
	if (os_snprintf_error(buflen, len)) {
		os_free(buf);
		return;
	}
	if (ssid->ssid && buflen > len + ssid->ssid_len) {
		os_memcpy(buf + len, ssid->ssid, ssid->ssid_len);
		len += ssid->ssid_len;
		buf[len] = '\0';
	}
	buf[buflen - 1] = '\0';
	wpa_msg(wpa_s, MSG_INFO, WPA_CTRL_REQ "%s", buf);
	os_free(buf);
}


#ifdef IEEE8021X_EAPOL
#if defined(CONFIG_CTRL_IFACE) || !defined(CONFIG_NO_STDOUT_DEBUG)
static void wpa_supplicant_eap_param_needed(void *ctx,
					    enum wpa_ctrl_req_type field,
					    const char *default_txt)
{
	struct wpa_supplicant *wpa_s = ctx;
	struct wpa_ssid *ssid = wpa_s->current_ssid;
	const char *field_name, *txt = NULL;

	if (ssid == NULL)
		return;

	if (field == WPA_CTRL_REQ_EXT_CERT_CHECK)
		ssid->eap.pending_ext_cert_check = PENDING_CHECK;
	wpas_notify_network_request(wpa_s, ssid, field, default_txt);

	field_name = wpa_supplicant_ctrl_req_to_string(field, default_txt,
						       &txt);
	if (field_name == NULL) {
		wpa_printf(MSG_WARNING, "Unhandled EAP param %d needed",
			   field);
		return;
	}

	wpas_notify_eap_status(wpa_s, "eap parameter needed", field_name);

	wpas_send_ctrl_req(wpa_s, ssid, field_name, txt);
}
#else /* CONFIG_CTRL_IFACE || !CONFIG_NO_STDOUT_DEBUG */
#define wpa_supplicant_eap_param_needed NULL
#endif /* CONFIG_CTRL_IFACE || !CONFIG_NO_STDOUT_DEBUG */


#ifdef CONFIG_EAP_PROXY

static void wpa_supplicant_eap_proxy_cb(void *ctx)
{
	struct wpa_supplicant *wpa_s = ctx;
	size_t len;

	wpa_s->mnc_len = eapol_sm_get_eap_proxy_imsi(wpa_s->eapol, -1,
						     wpa_s->imsi, &len);
	if (wpa_s->mnc_len > 0) {
		wpa_s->imsi[len] = '\0';
		wpa_printf(MSG_DEBUG, "eap_proxy: IMSI %s (MNC length %d)",
			   wpa_s->imsi, wpa_s->mnc_len);
	} else {
		wpa_printf(MSG_DEBUG, "eap_proxy: IMSI not available");
	}
}


static void wpa_sm_sim_state_error_handler(struct wpa_supplicant *wpa_s)
{
	int i;
	struct wpa_ssid *ssid;
	const struct eap_method_type *eap_methods;

	if (!wpa_s->conf)
		return;

	for (ssid = wpa_s->conf->ssid; ssid; ssid = ssid->next)	{
		eap_methods = ssid->eap.eap_methods;
		if (!eap_methods)
			continue;

		for (i = 0; eap_methods[i].method != EAP_TYPE_NONE; i++) {
			if (eap_methods[i].vendor == EAP_VENDOR_IETF &&
			    (eap_methods[i].method == EAP_TYPE_SIM ||
			     eap_methods[i].method == EAP_TYPE_AKA ||
			     eap_methods[i].method == EAP_TYPE_AKA_PRIME)) {
				wpa_sm_pmksa_cache_flush(wpa_s->wpa, ssid);
				break;
			}
		}
	}
}


static void
wpa_supplicant_eap_proxy_notify_sim_status(void *ctx,
					   enum eap_proxy_sim_state sim_state)
{
	struct wpa_supplicant *wpa_s = ctx;

	wpa_printf(MSG_DEBUG, "eap_proxy: SIM card status %u", sim_state);
	switch (sim_state) {
	case SIM_STATE_ERROR:
		wpa_sm_sim_state_error_handler(wpa_s);
		break;
	default:
		wpa_printf(MSG_DEBUG, "eap_proxy: SIM card status unknown");
		break;
	}
}

#endif /* CONFIG_EAP_PROXY */


static void wpa_supplicant_port_cb(void *ctx, int authorized)
{
	struct wpa_supplicant *wpa_s = ctx;
#ifdef CONFIG_AP
	if (wpa_s->ap_iface) {
		wpa_printf(MSG_DEBUG, "AP mode active - skip EAPOL Supplicant "
			   "port status: %s",
			   authorized ? "Authorized" : "Unauthorized");
		return;
	}
#endif /* CONFIG_AP */
	wpa_printf(MSG_DEBUG, "EAPOL: Supplicant port status: %s",
		   authorized ? "Authorized" : "Unauthorized");
	wpa_drv_set_supp_port(wpa_s, authorized);
}


static void wpa_supplicant_cert_cb(void *ctx, int depth, const char *subject,
				   const char *altsubject[], int num_altsubject,
				   const char *cert_hash,
				   const struct wpabuf *cert)
{
	struct wpa_supplicant *wpa_s = ctx;

	wpas_notify_certification(wpa_s, depth, subject, altsubject,
				  num_altsubject, cert_hash, cert);
}


static void wpa_supplicant_status_cb(void *ctx, const char *status,
				     const char *parameter)
{
	struct wpa_supplicant *wpa_s = ctx;

	wpas_notify_eap_status(wpa_s, status, parameter);
}


static void wpa_supplicant_eap_error_cb(void *ctx, int error_code)
{
	struct wpa_supplicant *wpa_s = ctx;

	wpas_notify_eap_error(wpa_s, error_code);
}


static void wpa_supplicant_set_anon_id(void *ctx, const u8 *id, size_t len)
{
	struct wpa_supplicant *wpa_s = ctx;
	char *str;
	int res;

	wpa_hexdump_ascii(MSG_DEBUG, "EAP method updated anonymous_identity",
			  id, len);

	if (wpa_s->current_ssid == NULL)
		return;

	if (id == NULL) {
		if (wpa_config_set(wpa_s->current_ssid, "anonymous_identity",
				   "NULL", 0) < 0)
			return;
	} else {
		str = os_malloc(len * 2 + 1);
		if (str == NULL)
			return;
		wpa_snprintf_hex(str, len * 2 + 1, id, len);
		res = wpa_config_set(wpa_s->current_ssid, "anonymous_identity",
				     str, 0);
		os_free(str);
		if (res < 0)
			return;
	}

	if (wpa_s->conf->update_config) {
		res = wpa_config_write(wpa_s->confname, wpa_s->conf);
		if (res) {
			wpa_printf(MSG_DEBUG, "Failed to update config after "
				   "anonymous_id update");
		}
	}
}
#endif /* IEEE8021X_EAPOL */


int wpa_supplicant_init_eapol(struct wpa_supplicant *wpa_s)
{
#ifdef IEEE8021X_EAPOL
	struct eapol_ctx *ctx;
	ctx = os_zalloc(sizeof(*ctx));
	if (ctx == NULL) {
		wpa_printf(MSG_ERROR, "Failed to allocate EAPOL context.");
		return -1;
	}

	ctx->ctx = wpa_s;
	ctx->msg_ctx = wpa_s;
	ctx->eapol_send_ctx = wpa_s;
	ctx->preauth = 0;
	ctx->eapol_done_cb = wpa_supplicant_notify_eapol_done;
	ctx->eapol_send = wpa_supplicant_eapol_send;
	ctx->set_wep_key = wpa_eapol_set_wep_key;
#ifndef CONFIG_NO_CONFIG_BLOBS
	ctx->set_config_blob = wpa_supplicant_set_config_blob;
	ctx->get_config_blob = wpa_supplicant_get_config_blob;
#endif /* CONFIG_NO_CONFIG_BLOBS */
	ctx->aborted_cached = wpa_supplicant_aborted_cached;
	ctx->opensc_engine_path = wpa_s->conf->opensc_engine_path;
	ctx->pkcs11_engine_path = wpa_s->conf->pkcs11_engine_path;
	ctx->pkcs11_module_path = wpa_s->conf->pkcs11_module_path;
	ctx->openssl_ciphers = wpa_s->conf->openssl_ciphers;
	ctx->wps = wpa_s->wps;
	ctx->eap_param_needed = wpa_supplicant_eap_param_needed;
#ifdef CONFIG_EAP_PROXY
	ctx->eap_proxy_cb = wpa_supplicant_eap_proxy_cb;
	ctx->eap_proxy_notify_sim_status =
		wpa_supplicant_eap_proxy_notify_sim_status;
#endif /* CONFIG_EAP_PROXY */
	ctx->port_cb = wpa_supplicant_port_cb;
	ctx->cb = wpa_supplicant_eapol_cb;
	ctx->cert_cb = wpa_supplicant_cert_cb;
	ctx->cert_in_cb = wpa_s->conf->cert_in_cb;
	ctx->status_cb = wpa_supplicant_status_cb;
	ctx->eap_error_cb = wpa_supplicant_eap_error_cb;
	ctx->set_anon_id = wpa_supplicant_set_anon_id;
	ctx->cb_ctx = wpa_s;
	wpa_s->eapol = eapol_sm_init(ctx);
	if (wpa_s->eapol == NULL) {
		os_free(ctx);
		wpa_printf(MSG_ERROR, "Failed to initialize EAPOL state "
			   "machines.");
		return -1;
	}
#endif /* IEEE8021X_EAPOL */

	return 0;
}


#ifndef CONFIG_NO_WPA

static void wpa_supplicant_set_rekey_offload(void *ctx,
					     const u8 *kek, size_t kek_len,
					     const u8 *kck, size_t kck_len,
					     const u8 *replay_ctr)
{
	struct wpa_supplicant *wpa_s = ctx;

	wpa_drv_set_rekey_info(wpa_s, kek, kek_len, kck, kck_len, replay_ctr);
}


static int wpa_supplicant_key_mgmt_set_pmk(void *ctx, const u8 *pmk,
					   size_t pmk_len)
{
	struct wpa_supplicant *wpa_s = ctx;

	if (wpa_s->conf->key_mgmt_offload &&
	    (wpa_s->drv_flags & WPA_DRIVER_FLAGS_KEY_MGMT_OFFLOAD))
		return wpa_drv_set_key(wpa_s, WPA_ALG_PMK, NULL, 0, 0,
				       NULL, 0, pmk, pmk_len);
	else
		return 0;
}


static void wpa_supplicant_fils_hlp_rx(void *ctx, const u8 *dst, const u8 *src,
				       const u8 *pkt, size_t pkt_len)
{
	struct wpa_supplicant *wpa_s = ctx;
	char *hex;
	size_t hexlen;

	hexlen = pkt_len * 2 + 1;
	hex = os_malloc(hexlen);
	if (!hex)
		return;
	wpa_snprintf_hex(hex, hexlen, pkt, pkt_len);
	wpa_msg(wpa_s, MSG_INFO, FILS_HLP_RX "dst=" MACSTR " src=" MACSTR
		" frame=%s", MAC2STR(dst), MAC2STR(src), hex);
	os_free(hex);
}

#endif /* CONFIG_NO_WPA */


int wpa_supplicant_init_wpa(struct wpa_supplicant *wpa_s)
{
#ifndef CONFIG_NO_WPA
	struct wpa_sm_ctx *ctx;
	ctx = os_zalloc(sizeof(*ctx));
	if (ctx == NULL) {
		wpa_printf(MSG_ERROR, "Failed to allocate WPA context.");
		return -1;
	}

	ctx->ctx = wpa_s;
	ctx->msg_ctx = wpa_s;
	ctx->set_state = _wpa_supplicant_set_state;
	ctx->get_state = _wpa_supplicant_get_state;
	ctx->deauthenticate = _wpa_supplicant_deauthenticate;
	ctx->set_key = wpa_supplicant_set_key;
	ctx->get_network_ctx = wpa_supplicant_get_network_ctx;
	ctx->get_bssid = wpa_supplicant_get_bssid;
	ctx->ether_send = _wpa_ether_send;
	ctx->get_beacon_ie = wpa_supplicant_get_beacon_ie;
	ctx->alloc_eapol = _wpa_alloc_eapol;
	ctx->cancel_auth_timeout = _wpa_supplicant_cancel_auth_timeout;
	ctx->add_pmkid = wpa_supplicant_add_pmkid;
	ctx->remove_pmkid = wpa_supplicant_remove_pmkid;
#ifndef CONFIG_NO_CONFIG_BLOBS
	ctx->set_config_blob = wpa_supplicant_set_config_blob;
	ctx->get_config_blob = wpa_supplicant_get_config_blob;
#endif /* CONFIG_NO_CONFIG_BLOBS */
	ctx->mlme_setprotection = wpa_supplicant_mlme_setprotection;
#ifdef CONFIG_IEEE80211R
	ctx->update_ft_ies = wpa_supplicant_update_ft_ies;
	ctx->send_ft_action = wpa_supplicant_send_ft_action;
	ctx->mark_authenticated = wpa_supplicant_mark_authenticated;
#endif /* CONFIG_IEEE80211R */
#ifdef CONFIG_TDLS
	ctx->tdls_get_capa = wpa_supplicant_tdls_get_capa;
	ctx->send_tdls_mgmt = wpa_supplicant_send_tdls_mgmt;
	ctx->tdls_oper = wpa_supplicant_tdls_oper;
	ctx->tdls_peer_addset = wpa_supplicant_tdls_peer_addset;
	ctx->tdls_enable_channel_switch =
		wpa_supplicant_tdls_enable_channel_switch;
	ctx->tdls_disable_channel_switch =
		wpa_supplicant_tdls_disable_channel_switch;
#endif /* CONFIG_TDLS */
	ctx->set_rekey_offload = wpa_supplicant_set_rekey_offload;
	ctx->key_mgmt_set_pmk = wpa_supplicant_key_mgmt_set_pmk;
	ctx->fils_hlp_rx = wpa_supplicant_fils_hlp_rx;

	wpa_s->wpa = wpa_sm_init(ctx);
	if (wpa_s->wpa == NULL) {
		wpa_printf(MSG_ERROR, "Failed to initialize WPA state "
			   "machine");
		os_free(ctx);
		return -1;
	}
#endif /* CONFIG_NO_WPA */

	return 0;
}


void wpa_supplicant_rsn_supp_set_config(struct wpa_supplicant *wpa_s,
					struct wpa_ssid *ssid)
{
	struct rsn_supp_config conf;
	if (ssid) {
		os_memset(&conf, 0, sizeof(conf));
		conf.network_ctx = ssid;
		conf.allowed_pairwise_cipher = ssid->pairwise_cipher;
#ifdef IEEE8021X_EAPOL
		conf.proactive_key_caching = ssid->proactive_key_caching < 0 ?
			wpa_s->conf->okc : ssid->proactive_key_caching;
		conf.eap_workaround = ssid->eap_workaround;
		conf.eap_conf_ctx = &ssid->eap;
#endif /* IEEE8021X_EAPOL */
		conf.ssid = ssid->ssid;
		conf.ssid_len = ssid->ssid_len;
		conf.wpa_ptk_rekey = ssid->wpa_ptk_rekey;
#ifdef CONFIG_P2P
		if (ssid->p2p_group && wpa_s->current_bss &&
		    !wpa_s->p2p_disable_ip_addr_req) {
			struct wpabuf *p2p;
			p2p = wpa_bss_get_vendor_ie_multi(wpa_s->current_bss,
							  P2P_IE_VENDOR_TYPE);
			if (p2p) {
				u8 group_capab;
				group_capab = p2p_get_group_capab(p2p);
				if (group_capab &
				    P2P_GROUP_CAPAB_IP_ADDR_ALLOCATION)
					conf.p2p = 1;
				wpabuf_free(p2p);
			}
		}
#endif /* CONFIG_P2P */
		conf.wpa_rsc_relaxation = wpa_s->conf->wpa_rsc_relaxation;
#ifdef CONFIG_FILS
		if (wpa_key_mgmt_fils(wpa_s->key_mgmt))
			conf.fils_cache_id =
				wpa_bss_get_fils_cache_id(wpa_s->current_bss);
#endif /* CONFIG_FILS */
	}
	wpa_sm_set_config(wpa_s->wpa, ssid ? &conf : NULL);
}
