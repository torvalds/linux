/*
 * wpa_supplicant - SME
 * Copyright (c) 2009-2014, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "utils/eloop.h"
#include "common/ieee802_11_defs.h"
#include "common/ieee802_11_common.h"
#include "eapol_supp/eapol_supp_sm.h"
#include "common/wpa_common.h"
#include "common/sae.h"
#include "rsn_supp/wpa.h"
#include "rsn_supp/pmksa_cache.h"
#include "config.h"
#include "wpa_supplicant_i.h"
#include "driver_i.h"
#include "wpas_glue.h"
#include "wps_supplicant.h"
#include "p2p_supplicant.h"
#include "notify.h"
#include "bss.h"
#include "scan.h"
#include "sme.h"
#include "hs20_supplicant.h"

#define SME_AUTH_TIMEOUT 5
#define SME_ASSOC_TIMEOUT 5

static void sme_auth_timer(void *eloop_ctx, void *timeout_ctx);
static void sme_assoc_timer(void *eloop_ctx, void *timeout_ctx);
static void sme_obss_scan_timeout(void *eloop_ctx, void *timeout_ctx);
#ifdef CONFIG_IEEE80211W
static void sme_stop_sa_query(struct wpa_supplicant *wpa_s);
#endif /* CONFIG_IEEE80211W */


#ifdef CONFIG_SAE

static int index_within_array(const int *array, int idx)
{
	int i;
	for (i = 0; i < idx; i++) {
		if (array[i] <= 0)
			return 0;
	}
	return 1;
}


static int sme_set_sae_group(struct wpa_supplicant *wpa_s)
{
	int *groups = wpa_s->conf->sae_groups;
	int default_groups[] = { 19, 20, 21, 25, 26, 0 };

	if (!groups || groups[0] <= 0)
		groups = default_groups;

	/* Configuration may have changed, so validate current index */
	if (!index_within_array(groups, wpa_s->sme.sae_group_index))
		return -1;

	for (;;) {
		int group = groups[wpa_s->sme.sae_group_index];
		if (group <= 0)
			break;
		if (sae_set_group(&wpa_s->sme.sae, group) == 0) {
			wpa_dbg(wpa_s, MSG_DEBUG, "SME: Selected SAE group %d",
				wpa_s->sme.sae.group);
			return 0;
		}
		wpa_s->sme.sae_group_index++;
	}

	return -1;
}


static struct wpabuf * sme_auth_build_sae_commit(struct wpa_supplicant *wpa_s,
						 struct wpa_ssid *ssid,
						 const u8 *bssid, int external)
{
	struct wpabuf *buf;
	size_t len;
	const char *password;

#ifdef CONFIG_TESTING_OPTIONS
	if (wpa_s->sae_commit_override) {
		wpa_printf(MSG_DEBUG, "SAE: TESTING - commit override");
		buf = wpabuf_alloc(4 + wpabuf_len(wpa_s->sae_commit_override));
		if (!buf)
			return NULL;
		wpabuf_put_le16(buf, 1); /* Transaction seq# */
		wpabuf_put_le16(buf, WLAN_STATUS_SUCCESS);
		wpabuf_put_buf(buf, wpa_s->sae_commit_override);
		return buf;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	password = ssid->sae_password;
	if (!password)
		password = ssid->passphrase;
	if (!password) {
		wpa_printf(MSG_DEBUG, "SAE: No password available");
		return NULL;
	}

	if (sme_set_sae_group(wpa_s) < 0) {
		wpa_printf(MSG_DEBUG, "SAE: Failed to select group");
		return NULL;
	}

	if (sae_prepare_commit(wpa_s->own_addr, bssid,
			       (u8 *) password, os_strlen(password),
			       ssid->sae_password_id,
			       &wpa_s->sme.sae) < 0) {
		wpa_printf(MSG_DEBUG, "SAE: Could not pick PWE");
		return NULL;
	}

	len = wpa_s->sme.sae_token ? wpabuf_len(wpa_s->sme.sae_token) : 0;
	if (ssid->sae_password_id)
		len += 4 + os_strlen(ssid->sae_password_id);
	buf = wpabuf_alloc(4 + SAE_COMMIT_MAX_LEN + len);
	if (buf == NULL)
		return NULL;
	if (!external) {
		wpabuf_put_le16(buf, 1); /* Transaction seq# */
		wpabuf_put_le16(buf, WLAN_STATUS_SUCCESS);
	}
	sae_write_commit(&wpa_s->sme.sae, buf, wpa_s->sme.sae_token,
			 ssid->sae_password_id);

	return buf;
}


static struct wpabuf * sme_auth_build_sae_confirm(struct wpa_supplicant *wpa_s,
						  int external)
{
	struct wpabuf *buf;

	buf = wpabuf_alloc(4 + SAE_CONFIRM_MAX_LEN);
	if (buf == NULL)
		return NULL;

	if (!external) {
		wpabuf_put_le16(buf, 2); /* Transaction seq# */
		wpabuf_put_le16(buf, WLAN_STATUS_SUCCESS);
	}
	sae_write_confirm(&wpa_s->sme.sae, buf);

	return buf;
}

#endif /* CONFIG_SAE */


/**
 * sme_auth_handle_rrm - Handle RRM aspects of current authentication attempt
 * @wpa_s: Pointer to wpa_supplicant data
 * @bss: Pointer to the bss which is the target of authentication attempt
 */
static void sme_auth_handle_rrm(struct wpa_supplicant *wpa_s,
				struct wpa_bss *bss)
{
	const u8 rrm_ie_len = 5;
	u8 *pos;
	const u8 *rrm_ie;

	wpa_s->rrm.rrm_used = 0;

	wpa_printf(MSG_DEBUG,
		   "RRM: Determining whether RRM can be used - device support: 0x%x",
		   wpa_s->drv_rrm_flags);

	rrm_ie = wpa_bss_get_ie(bss, WLAN_EID_RRM_ENABLED_CAPABILITIES);
	if (!rrm_ie || !(bss->caps & IEEE80211_CAP_RRM)) {
		wpa_printf(MSG_DEBUG, "RRM: No RRM in network");
		return;
	}

	if (!((wpa_s->drv_rrm_flags &
	       WPA_DRIVER_FLAGS_DS_PARAM_SET_IE_IN_PROBES) &&
	      (wpa_s->drv_rrm_flags & WPA_DRIVER_FLAGS_QUIET)) &&
	    !(wpa_s->drv_rrm_flags & WPA_DRIVER_FLAGS_SUPPORT_RRM)) {
		wpa_printf(MSG_DEBUG,
			   "RRM: Insufficient RRM support in driver - do not use RRM");
		return;
	}

	if (sizeof(wpa_s->sme.assoc_req_ie) <
	    wpa_s->sme.assoc_req_ie_len + rrm_ie_len + 2) {
		wpa_printf(MSG_INFO,
			   "RRM: Unable to use RRM, no room for RRM IE");
		return;
	}

	wpa_printf(MSG_DEBUG, "RRM: Adding RRM IE to Association Request");
	pos = wpa_s->sme.assoc_req_ie + wpa_s->sme.assoc_req_ie_len;
	os_memset(pos, 0, 2 + rrm_ie_len);
	*pos++ = WLAN_EID_RRM_ENABLED_CAPABILITIES;
	*pos++ = rrm_ie_len;

	/* Set supported capabilites flags */
	if (wpa_s->drv_rrm_flags & WPA_DRIVER_FLAGS_TX_POWER_INSERTION)
		*pos |= WLAN_RRM_CAPS_LINK_MEASUREMENT;

	*pos |= WLAN_RRM_CAPS_BEACON_REPORT_PASSIVE |
		WLAN_RRM_CAPS_BEACON_REPORT_ACTIVE |
		WLAN_RRM_CAPS_BEACON_REPORT_TABLE;

	if (wpa_s->lci)
		pos[1] |= WLAN_RRM_CAPS_LCI_MEASUREMENT;

	wpa_s->sme.assoc_req_ie_len += rrm_ie_len + 2;
	wpa_s->rrm.rrm_used = 1;
}


static void sme_send_authentication(struct wpa_supplicant *wpa_s,
				    struct wpa_bss *bss, struct wpa_ssid *ssid,
				    int start)
{
	struct wpa_driver_auth_params params;
	struct wpa_ssid *old_ssid;
#ifdef CONFIG_IEEE80211R
	const u8 *ie;
#endif /* CONFIG_IEEE80211R */
#if defined(CONFIG_IEEE80211R) || defined(CONFIG_FILS)
	const u8 *md = NULL;
#endif /* CONFIG_IEEE80211R || CONFIG_FILS */
	int i, bssid_changed;
	struct wpabuf *resp = NULL;
	u8 ext_capab[18];
	int ext_capab_len;
	int skip_auth;
	u8 *wpa_ie;
	size_t wpa_ie_len;
#ifdef CONFIG_MBO
	const u8 *mbo_ie;
#endif /* CONFIG_MBO */

	if (bss == NULL) {
		wpa_msg(wpa_s, MSG_ERROR, "SME: No scan result available for "
			"the network");
		wpas_connect_work_done(wpa_s);
		return;
	}

	skip_auth = wpa_s->conf->reassoc_same_bss_optim &&
		wpa_s->reassoc_same_bss;
	wpa_s->current_bss = bss;

	os_memset(&params, 0, sizeof(params));
	wpa_s->reassociate = 0;

	params.freq = bss->freq;
	params.bssid = bss->bssid;
	params.ssid = bss->ssid;
	params.ssid_len = bss->ssid_len;
	params.p2p = ssid->p2p_group;

	if (wpa_s->sme.ssid_len != params.ssid_len ||
	    os_memcmp(wpa_s->sme.ssid, params.ssid, params.ssid_len) != 0)
		wpa_s->sme.prev_bssid_set = 0;

	wpa_s->sme.freq = params.freq;
	os_memcpy(wpa_s->sme.ssid, params.ssid, params.ssid_len);
	wpa_s->sme.ssid_len = params.ssid_len;

	params.auth_alg = WPA_AUTH_ALG_OPEN;
#ifdef IEEE8021X_EAPOL
	if (ssid->key_mgmt & WPA_KEY_MGMT_IEEE8021X_NO_WPA) {
		if (ssid->leap) {
			if (ssid->non_leap == 0)
				params.auth_alg = WPA_AUTH_ALG_LEAP;
			else
				params.auth_alg |= WPA_AUTH_ALG_LEAP;
		}
	}
#endif /* IEEE8021X_EAPOL */
	wpa_dbg(wpa_s, MSG_DEBUG, "Automatic auth_alg selection: 0x%x",
		params.auth_alg);
	if (ssid->auth_alg) {
		params.auth_alg = ssid->auth_alg;
		wpa_dbg(wpa_s, MSG_DEBUG, "Overriding auth_alg selection: "
			"0x%x", params.auth_alg);
	}
#ifdef CONFIG_SAE
	wpa_s->sme.sae_pmksa_caching = 0;
	if (wpa_key_mgmt_sae(ssid->key_mgmt)) {
		const u8 *rsn;
		struct wpa_ie_data ied;

		rsn = wpa_bss_get_ie(bss, WLAN_EID_RSN);
		if (!rsn) {
			wpa_dbg(wpa_s, MSG_DEBUG,
				"SAE enabled, but target BSS does not advertise RSN");
		} else if (wpa_parse_wpa_ie(rsn, 2 + rsn[1], &ied) == 0 &&
			   wpa_key_mgmt_sae(ied.key_mgmt)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "Using SAE auth_alg");
			params.auth_alg = WPA_AUTH_ALG_SAE;
		} else {
			wpa_dbg(wpa_s, MSG_DEBUG,
				"SAE enabled, but target BSS does not advertise SAE AKM for RSN");
		}
	}
#endif /* CONFIG_SAE */

	for (i = 0; i < NUM_WEP_KEYS; i++) {
		if (ssid->wep_key_len[i])
			params.wep_key[i] = ssid->wep_key[i];
		params.wep_key_len[i] = ssid->wep_key_len[i];
	}
	params.wep_tx_keyidx = ssid->wep_tx_keyidx;

	bssid_changed = !is_zero_ether_addr(wpa_s->bssid);
	os_memset(wpa_s->bssid, 0, ETH_ALEN);
	os_memcpy(wpa_s->pending_bssid, bss->bssid, ETH_ALEN);
	if (bssid_changed)
		wpas_notify_bssid_changed(wpa_s);

	if ((wpa_bss_get_vendor_ie(bss, WPA_IE_VENDOR_TYPE) ||
	     wpa_bss_get_ie(bss, WLAN_EID_RSN)) &&
	    wpa_key_mgmt_wpa(ssid->key_mgmt)) {
		int try_opportunistic;
		const u8 *cache_id = NULL;

		try_opportunistic = (ssid->proactive_key_caching < 0 ?
				     wpa_s->conf->okc :
				     ssid->proactive_key_caching) &&
			(ssid->proto & WPA_PROTO_RSN);
#ifdef CONFIG_FILS
		if (wpa_key_mgmt_fils(ssid->key_mgmt))
			cache_id = wpa_bss_get_fils_cache_id(bss);
#endif /* CONFIG_FILS */
		if (pmksa_cache_set_current(wpa_s->wpa, NULL, bss->bssid,
					    wpa_s->current_ssid,
					    try_opportunistic, cache_id,
					    0) == 0)
			eapol_sm_notify_pmkid_attempt(wpa_s->eapol);
		wpa_s->sme.assoc_req_ie_len = sizeof(wpa_s->sme.assoc_req_ie);
		if (wpa_supplicant_set_suites(wpa_s, bss, ssid,
					      wpa_s->sme.assoc_req_ie,
					      &wpa_s->sme.assoc_req_ie_len)) {
			wpa_msg(wpa_s, MSG_WARNING, "SME: Failed to set WPA "
				"key management and encryption suites");
			wpas_connect_work_done(wpa_s);
			return;
		}
#ifdef CONFIG_HS20
	} else if (wpa_bss_get_vendor_ie(bss, OSEN_IE_VENDOR_TYPE) &&
		   (ssid->key_mgmt & WPA_KEY_MGMT_OSEN)) {
		/* No PMKSA caching, but otherwise similar to RSN/WPA */
		wpa_s->sme.assoc_req_ie_len = sizeof(wpa_s->sme.assoc_req_ie);
		if (wpa_supplicant_set_suites(wpa_s, bss, ssid,
					      wpa_s->sme.assoc_req_ie,
					      &wpa_s->sme.assoc_req_ie_len)) {
			wpa_msg(wpa_s, MSG_WARNING, "SME: Failed to set WPA "
				"key management and encryption suites");
			wpas_connect_work_done(wpa_s);
			return;
		}
#endif /* CONFIG_HS20 */
	} else if ((ssid->key_mgmt & WPA_KEY_MGMT_IEEE8021X_NO_WPA) &&
		   wpa_key_mgmt_wpa_ieee8021x(ssid->key_mgmt)) {
		/*
		 * Both WPA and non-WPA IEEE 802.1X enabled in configuration -
		 * use non-WPA since the scan results did not indicate that the
		 * AP is using WPA or WPA2.
		 */
		wpa_supplicant_set_non_wpa_policy(wpa_s, ssid);
		wpa_s->sme.assoc_req_ie_len = 0;
	} else if (wpa_key_mgmt_wpa_any(ssid->key_mgmt)) {
		wpa_s->sme.assoc_req_ie_len = sizeof(wpa_s->sme.assoc_req_ie);
		if (wpa_supplicant_set_suites(wpa_s, NULL, ssid,
					      wpa_s->sme.assoc_req_ie,
					      &wpa_s->sme.assoc_req_ie_len)) {
			wpa_msg(wpa_s, MSG_WARNING, "SME: Failed to set WPA "
				"key management and encryption suites (no "
				"scan results)");
			wpas_connect_work_done(wpa_s);
			return;
		}
#ifdef CONFIG_WPS
	} else if (ssid->key_mgmt & WPA_KEY_MGMT_WPS) {
		struct wpabuf *wps_ie;
		wps_ie = wps_build_assoc_req_ie(wpas_wps_get_req_type(ssid));
		if (wps_ie && wpabuf_len(wps_ie) <=
		    sizeof(wpa_s->sme.assoc_req_ie)) {
			wpa_s->sme.assoc_req_ie_len = wpabuf_len(wps_ie);
			os_memcpy(wpa_s->sme.assoc_req_ie, wpabuf_head(wps_ie),
				  wpa_s->sme.assoc_req_ie_len);
		} else
			wpa_s->sme.assoc_req_ie_len = 0;
		wpabuf_free(wps_ie);
		wpa_supplicant_set_non_wpa_policy(wpa_s, ssid);
#endif /* CONFIG_WPS */
	} else {
		wpa_supplicant_set_non_wpa_policy(wpa_s, ssid);
		wpa_s->sme.assoc_req_ie_len = 0;
	}

	/* In case the WPA vendor IE is used, it should be placed after all the
	 * non-vendor IEs, as the lower layer expects the IEs to be ordered as
	 * defined in the standard. Store the WPA IE so it can later be
	 * inserted at the correct location.
	 */
	wpa_ie = NULL;
	wpa_ie_len = 0;
	if (wpa_s->wpa_proto == WPA_PROTO_WPA) {
		wpa_ie = os_memdup(wpa_s->sme.assoc_req_ie,
				   wpa_s->sme.assoc_req_ie_len);
		if (wpa_ie) {
			wpa_dbg(wpa_s, MSG_DEBUG, "WPA: Storing WPA IE");

			wpa_ie_len = wpa_s->sme.assoc_req_ie_len;
			wpa_s->sme.assoc_req_ie_len = 0;
		} else {
			wpa_msg(wpa_s, MSG_WARNING, "WPA: Failed copy WPA IE");
			wpas_connect_work_done(wpa_s);
			return;
		}
	}

#ifdef CONFIG_IEEE80211R
	ie = wpa_bss_get_ie(bss, WLAN_EID_MOBILITY_DOMAIN);
	if (ie && ie[1] >= MOBILITY_DOMAIN_ID_LEN)
		md = ie + 2;
	wpa_sm_set_ft_params(wpa_s->wpa, ie, ie ? 2 + ie[1] : 0);
	if (md) {
		/* Prepare for the next transition */
		wpa_ft_prepare_auth_request(wpa_s->wpa, ie);
	}

	if (md && !wpa_key_mgmt_ft(ssid->key_mgmt))
		md = NULL;
	if (md) {
		wpa_dbg(wpa_s, MSG_DEBUG, "SME: FT mobility domain %02x%02x",
			md[0], md[1]);

		if (wpa_s->sme.assoc_req_ie_len + 5 <
		    sizeof(wpa_s->sme.assoc_req_ie)) {
			struct rsn_mdie *mdie;
			u8 *pos = wpa_s->sme.assoc_req_ie +
				wpa_s->sme.assoc_req_ie_len;
			*pos++ = WLAN_EID_MOBILITY_DOMAIN;
			*pos++ = sizeof(*mdie);
			mdie = (struct rsn_mdie *) pos;
			os_memcpy(mdie->mobility_domain, md,
				  MOBILITY_DOMAIN_ID_LEN);
			mdie->ft_capab = md[MOBILITY_DOMAIN_ID_LEN];
			wpa_s->sme.assoc_req_ie_len += 5;
		}

		if (wpa_s->sme.ft_used &&
		    os_memcmp(md, wpa_s->sme.mobility_domain, 2) == 0 &&
		    wpa_sm_has_ptk(wpa_s->wpa)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "SME: Trying to use FT "
				"over-the-air");
			params.auth_alg = WPA_AUTH_ALG_FT;
			params.ie = wpa_s->sme.ft_ies;
			params.ie_len = wpa_s->sme.ft_ies_len;
		}
	}
#endif /* CONFIG_IEEE80211R */

#ifdef CONFIG_IEEE80211W
	wpa_s->sme.mfp = wpas_get_ssid_pmf(wpa_s, ssid);
	if (wpa_s->sme.mfp != NO_MGMT_FRAME_PROTECTION) {
		const u8 *rsn = wpa_bss_get_ie(bss, WLAN_EID_RSN);
		struct wpa_ie_data _ie;
		if (rsn && wpa_parse_wpa_ie(rsn, 2 + rsn[1], &_ie) == 0 &&
		    _ie.capabilities &
		    (WPA_CAPABILITY_MFPC | WPA_CAPABILITY_MFPR)) {
			wpa_dbg(wpa_s, MSG_DEBUG, "SME: Selected AP supports "
				"MFP: require MFP");
			wpa_s->sme.mfp = MGMT_FRAME_PROTECTION_REQUIRED;
		}
	}
#endif /* CONFIG_IEEE80211W */

#ifdef CONFIG_P2P
	if (wpa_s->global->p2p) {
		u8 *pos;
		size_t len;
		int res;
		pos = wpa_s->sme.assoc_req_ie + wpa_s->sme.assoc_req_ie_len;
		len = sizeof(wpa_s->sme.assoc_req_ie) -
			wpa_s->sme.assoc_req_ie_len;
		res = wpas_p2p_assoc_req_ie(wpa_s, bss, pos, len,
					    ssid->p2p_group);
		if (res >= 0)
			wpa_s->sme.assoc_req_ie_len += res;
	}
#endif /* CONFIG_P2P */

#ifdef CONFIG_FST
	if (wpa_s->fst_ies) {
		int fst_ies_len = wpabuf_len(wpa_s->fst_ies);

		if (wpa_s->sme.assoc_req_ie_len + fst_ies_len <=
		    sizeof(wpa_s->sme.assoc_req_ie)) {
			os_memcpy(wpa_s->sme.assoc_req_ie +
				  wpa_s->sme.assoc_req_ie_len,
				  wpabuf_head(wpa_s->fst_ies),
				  fst_ies_len);
			wpa_s->sme.assoc_req_ie_len += fst_ies_len;
		}
	}
#endif /* CONFIG_FST */

	sme_auth_handle_rrm(wpa_s, bss);

	wpa_s->sme.assoc_req_ie_len += wpas_supp_op_class_ie(
		wpa_s, bss->freq,
		wpa_s->sme.assoc_req_ie + wpa_s->sme.assoc_req_ie_len,
		sizeof(wpa_s->sme.assoc_req_ie) - wpa_s->sme.assoc_req_ie_len);

	if (params.p2p)
		wpa_drv_get_ext_capa(wpa_s, WPA_IF_P2P_CLIENT);
	else
		wpa_drv_get_ext_capa(wpa_s, WPA_IF_STATION);

	ext_capab_len = wpas_build_ext_capab(wpa_s, ext_capab,
					     sizeof(ext_capab));
	if (ext_capab_len > 0) {
		u8 *pos = wpa_s->sme.assoc_req_ie;
		if (wpa_s->sme.assoc_req_ie_len > 0 && pos[0] == WLAN_EID_RSN)
			pos += 2 + pos[1];
		os_memmove(pos + ext_capab_len, pos,
			   wpa_s->sme.assoc_req_ie_len -
			   (pos - wpa_s->sme.assoc_req_ie));
		wpa_s->sme.assoc_req_ie_len += ext_capab_len;
		os_memcpy(pos, ext_capab, ext_capab_len);
	}

#ifdef CONFIG_HS20
	if (is_hs20_network(wpa_s, ssid, bss)) {
		struct wpabuf *hs20;

		hs20 = wpabuf_alloc(20 + MAX_ROAMING_CONS_OI_LEN);
		if (hs20) {
			int pps_mo_id = hs20_get_pps_mo_id(wpa_s, ssid);
			size_t len;

			wpas_hs20_add_indication(hs20, pps_mo_id);
			wpas_hs20_add_roam_cons_sel(hs20, ssid);
			len = sizeof(wpa_s->sme.assoc_req_ie) -
				wpa_s->sme.assoc_req_ie_len;
			if (wpabuf_len(hs20) <= len) {
				os_memcpy(wpa_s->sme.assoc_req_ie +
					  wpa_s->sme.assoc_req_ie_len,
					  wpabuf_head(hs20), wpabuf_len(hs20));
				wpa_s->sme.assoc_req_ie_len += wpabuf_len(hs20);
			}
			wpabuf_free(hs20);
		}
	}
#endif /* CONFIG_HS20 */

	if (wpa_ie) {
		size_t len;

		wpa_dbg(wpa_s, MSG_DEBUG, "WPA: Reinsert WPA IE");

		len = sizeof(wpa_s->sme.assoc_req_ie) -
			wpa_s->sme.assoc_req_ie_len;

		if (len > wpa_ie_len) {
			os_memcpy(wpa_s->sme.assoc_req_ie +
				  wpa_s->sme.assoc_req_ie_len,
				  wpa_ie, wpa_ie_len);
			wpa_s->sme.assoc_req_ie_len += wpa_ie_len;
		} else {
			wpa_dbg(wpa_s, MSG_DEBUG, "WPA: Failed to add WPA IE");
		}

		os_free(wpa_ie);
	}

	if (wpa_s->vendor_elem[VENDOR_ELEM_ASSOC_REQ]) {
		struct wpabuf *buf = wpa_s->vendor_elem[VENDOR_ELEM_ASSOC_REQ];
		size_t len;

		len = sizeof(wpa_s->sme.assoc_req_ie) -
			wpa_s->sme.assoc_req_ie_len;
		if (wpabuf_len(buf) <= len) {
			os_memcpy(wpa_s->sme.assoc_req_ie +
				  wpa_s->sme.assoc_req_ie_len,
				  wpabuf_head(buf), wpabuf_len(buf));
			wpa_s->sme.assoc_req_ie_len += wpabuf_len(buf);
		}
	}

#ifdef CONFIG_MBO
	mbo_ie = wpa_bss_get_vendor_ie(bss, MBO_IE_VENDOR_TYPE);
	if (mbo_ie) {
		int len;

		len = wpas_mbo_ie(wpa_s, wpa_s->sme.assoc_req_ie +
				  wpa_s->sme.assoc_req_ie_len,
				  sizeof(wpa_s->sme.assoc_req_ie) -
				  wpa_s->sme.assoc_req_ie_len,
				  !!mbo_attr_from_mbo_ie(mbo_ie,
							 OCE_ATTR_ID_CAPA_IND));
		if (len >= 0)
			wpa_s->sme.assoc_req_ie_len += len;
	}
#endif /* CONFIG_MBO */

#ifdef CONFIG_SAE
	if (!skip_auth && params.auth_alg == WPA_AUTH_ALG_SAE &&
	    pmksa_cache_set_current(wpa_s->wpa, NULL, bss->bssid, ssid, 0,
				    NULL, WPA_KEY_MGMT_SAE) == 0) {
		wpa_dbg(wpa_s, MSG_DEBUG,
			"PMKSA cache entry found - try to use PMKSA caching instead of new SAE authentication");
		wpa_sm_set_pmk_from_pmksa(wpa_s->wpa);
		params.auth_alg = WPA_AUTH_ALG_OPEN;
		wpa_s->sme.sae_pmksa_caching = 1;
	}

	if (!skip_auth && params.auth_alg == WPA_AUTH_ALG_SAE) {
		if (start)
			resp = sme_auth_build_sae_commit(wpa_s, ssid,
							 bss->bssid, 0);
		else
			resp = sme_auth_build_sae_confirm(wpa_s, 0);
		if (resp == NULL) {
			wpas_connection_failed(wpa_s, bss->bssid);
			return;
		}
		params.auth_data = wpabuf_head(resp);
		params.auth_data_len = wpabuf_len(resp);
		wpa_s->sme.sae.state = start ? SAE_COMMITTED : SAE_CONFIRMED;
	}
#endif /* CONFIG_SAE */

	old_ssid = wpa_s->current_ssid;
	wpa_s->current_ssid = ssid;
	wpa_supplicant_rsn_supp_set_config(wpa_s, wpa_s->current_ssid);
	wpa_supplicant_initiate_eapol(wpa_s);

#ifdef CONFIG_FILS
	/* TODO: FILS operations can in some cases be done between different
	 * network_ctx (i.e., same credentials can be used with multiple
	 * networks). */
	if (params.auth_alg == WPA_AUTH_ALG_OPEN &&
	    wpa_key_mgmt_fils(ssid->key_mgmt)) {
		const u8 *indic;
		u16 fils_info;
		const u8 *realm, *username, *rrk;
		size_t realm_len, username_len, rrk_len;
		u16 next_seq_num;

		/*
		 * Check FILS Indication element (FILS Information field) bits
		 * indicating supported authentication algorithms against local
		 * configuration (ssid->fils_dh_group). Try to use FILS
		 * authentication only if the AP supports the combination in the
		 * network profile. */
		indic = wpa_bss_get_ie(bss, WLAN_EID_FILS_INDICATION);
		if (!indic || indic[1] < 2) {
			wpa_printf(MSG_DEBUG, "SME: " MACSTR
				   " does not include FILS Indication element - cannot use FILS authentication with it",
				   MAC2STR(bss->bssid));
			goto no_fils;
		}

		fils_info = WPA_GET_LE16(indic + 2);
		if (ssid->fils_dh_group == 0 && !(fils_info & BIT(9))) {
			wpa_printf(MSG_DEBUG, "SME: " MACSTR
				   " does not support FILS SK without PFS - cannot use FILS authentication with it",
				   MAC2STR(bss->bssid));
			goto no_fils;
		}
		if (ssid->fils_dh_group != 0 && !(fils_info & BIT(10))) {
			wpa_printf(MSG_DEBUG, "SME: " MACSTR
				   " does not support FILS SK with PFS - cannot use FILS authentication with it",
				   MAC2STR(bss->bssid));
			goto no_fils;
		}

		if (wpa_s->last_con_fail_realm &&
		    eapol_sm_get_erp_info(wpa_s->eapol, &ssid->eap,
					  &username, &username_len,
					  &realm, &realm_len, &next_seq_num,
					  &rrk, &rrk_len) == 0 &&
		    realm && realm_len == wpa_s->last_con_fail_realm_len &&
		    os_memcmp(realm, wpa_s->last_con_fail_realm,
			      realm_len) == 0) {
			wpa_printf(MSG_DEBUG,
				   "SME: FILS authentication for this realm failed last time - try to regenerate ERP key hierarchy");
			goto no_fils;
		}

		if (pmksa_cache_set_current(wpa_s->wpa, NULL, bss->bssid,
					    ssid, 0,
					    wpa_bss_get_fils_cache_id(bss),
					    0) == 0)
			wpa_printf(MSG_DEBUG,
				   "SME: Try to use FILS with PMKSA caching");
		resp = fils_build_auth(wpa_s->wpa, ssid->fils_dh_group, md);
		if (resp) {
			int auth_alg;

			if (ssid->fils_dh_group)
				wpa_printf(MSG_DEBUG,
					   "SME: Try to use FILS SK authentication with PFS (DH Group %u)",
					   ssid->fils_dh_group);
			else
				wpa_printf(MSG_DEBUG,
					   "SME: Try to use FILS SK authentication without PFS");
			auth_alg = ssid->fils_dh_group ?
				WPA_AUTH_ALG_FILS_SK_PFS : WPA_AUTH_ALG_FILS;
			params.auth_alg = auth_alg;
			params.auth_data = wpabuf_head(resp);
			params.auth_data_len = wpabuf_len(resp);
			wpa_s->sme.auth_alg = auth_alg;
		}
	}
no_fils:
#endif /* CONFIG_FILS */

	wpa_supplicant_cancel_sched_scan(wpa_s);
	wpa_supplicant_cancel_scan(wpa_s);

	wpa_msg(wpa_s, MSG_INFO, "SME: Trying to authenticate with " MACSTR
		" (SSID='%s' freq=%d MHz)", MAC2STR(params.bssid),
		wpa_ssid_txt(params.ssid, params.ssid_len), params.freq);

	eapol_sm_notify_portValid(wpa_s->eapol, FALSE);
	wpa_clear_keys(wpa_s, bss->bssid);
	wpa_supplicant_set_state(wpa_s, WPA_AUTHENTICATING);
	if (old_ssid != wpa_s->current_ssid)
		wpas_notify_network_changed(wpa_s);

#ifdef CONFIG_HS20
	hs20_configure_frame_filters(wpa_s);
#endif /* CONFIG_HS20 */

#ifdef CONFIG_P2P
	/*
	 * If multi-channel concurrency is not supported, check for any
	 * frequency conflict. In case of any frequency conflict, remove the
	 * least prioritized connection.
	 */
	if (wpa_s->num_multichan_concurrent < 2) {
		int freq, num;
		num = get_shared_radio_freqs(wpa_s, &freq, 1);
		if (num > 0 && freq > 0 && freq != params.freq) {
			wpa_printf(MSG_DEBUG,
				   "Conflicting frequency found (%d != %d)",
				   freq, params.freq);
			if (wpas_p2p_handle_frequency_conflicts(wpa_s,
								params.freq,
								ssid) < 0) {
				wpas_connection_failed(wpa_s, bss->bssid);
				wpa_supplicant_mark_disassoc(wpa_s);
				wpabuf_free(resp);
				wpas_connect_work_done(wpa_s);
				return;
			}
		}
	}
#endif /* CONFIG_P2P */

	if (skip_auth) {
		wpa_msg(wpa_s, MSG_DEBUG,
			"SME: Skip authentication step on reassoc-to-same-BSS");
		wpabuf_free(resp);
		sme_associate(wpa_s, ssid->mode, bss->bssid, WLAN_AUTH_OPEN);
		return;
	}


	wpa_s->sme.auth_alg = params.auth_alg;
	if (wpa_drv_authenticate(wpa_s, &params) < 0) {
		wpa_msg(wpa_s, MSG_INFO, "SME: Authentication request to the "
			"driver failed");
		wpas_connection_failed(wpa_s, bss->bssid);
		wpa_supplicant_mark_disassoc(wpa_s);
		wpabuf_free(resp);
		wpas_connect_work_done(wpa_s);
		return;
	}

	eloop_register_timeout(SME_AUTH_TIMEOUT, 0, sme_auth_timer, wpa_s,
			       NULL);

	/*
	 * Association will be started based on the authentication event from
	 * the driver.
	 */

	wpabuf_free(resp);
}


static void sme_auth_start_cb(struct wpa_radio_work *work, int deinit)
{
	struct wpa_connect_work *cwork = work->ctx;
	struct wpa_supplicant *wpa_s = work->wpa_s;

	if (deinit) {
		if (work->started)
			wpa_s->connect_work = NULL;

		wpas_connect_work_free(cwork);
		return;
	}

	wpa_s->connect_work = work;

	if (cwork->bss_removed ||
	    !wpas_valid_bss_ssid(wpa_s, cwork->bss, cwork->ssid) ||
	    wpas_network_disabled(wpa_s, cwork->ssid)) {
		wpa_dbg(wpa_s, MSG_DEBUG, "SME: BSS/SSID entry for authentication not valid anymore - drop connection attempt");
		wpas_connect_work_done(wpa_s);
		return;
	}

	/* Starting new connection, so clear the possibly used WPA IE from the
	 * previous association. */
	wpa_sm_set_assoc_wpa_ie(wpa_s->wpa, NULL, 0);

	sme_send_authentication(wpa_s, cwork->bss, cwork->ssid, 1);
}


void sme_authenticate(struct wpa_supplicant *wpa_s,
		      struct wpa_bss *bss, struct wpa_ssid *ssid)
{
	struct wpa_connect_work *cwork;

	if (bss == NULL || ssid == NULL)
		return;
	if (wpa_s->connect_work) {
		wpa_dbg(wpa_s, MSG_DEBUG, "SME: Reject sme_authenticate() call since connect_work exist");
		return;
	}

	if (radio_work_pending(wpa_s, "sme-connect")) {
		/*
		 * The previous sme-connect work might no longer be valid due to
		 * the fact that the BSS list was updated. In addition, it makes
		 * sense to adhere to the 'newer' decision.
		 */
		wpa_dbg(wpa_s, MSG_DEBUG,
			"SME: Remove previous pending sme-connect");
		radio_remove_works(wpa_s, "sme-connect", 0);
	}

	wpas_abort_ongoing_scan(wpa_s);

	cwork = os_zalloc(sizeof(*cwork));
	if (cwork == NULL)
		return;
	cwork->bss = bss;
	cwork->ssid = ssid;
	cwork->sme = 1;

#ifdef CONFIG_SAE
	wpa_s->sme.sae.state = SAE_NOTHING;
	wpa_s->sme.sae.send_confirm = 0;
	wpa_s->sme.sae_group_index = 0;
#endif /* CONFIG_SAE */

	if (radio_add_work(wpa_s, bss->freq, "sme-connect", 1,
			   sme_auth_start_cb, cwork) < 0)
		wpas_connect_work_free(cwork);
}


#ifdef CONFIG_SAE

static int sme_external_auth_build_buf(struct wpabuf *buf,
				       struct wpabuf *params,
				       const u8 *sa, const u8 *da,
				       u16 auth_transaction, u16 seq_num)
{
	struct ieee80211_mgmt *resp;

	resp = wpabuf_put(buf, offsetof(struct ieee80211_mgmt,
					u.auth.variable));

	resp->frame_control = host_to_le16((WLAN_FC_TYPE_MGMT << 2) |
					   (WLAN_FC_STYPE_AUTH << 4));
	os_memcpy(resp->da, da, ETH_ALEN);
	os_memcpy(resp->sa, sa, ETH_ALEN);
	os_memcpy(resp->bssid, da, ETH_ALEN);
	resp->u.auth.auth_alg = host_to_le16(WLAN_AUTH_SAE);
	resp->seq_ctrl = host_to_le16(seq_num << 4);
	resp->u.auth.auth_transaction = host_to_le16(auth_transaction);
	resp->u.auth.status_code = host_to_le16(WLAN_STATUS_SUCCESS);
	if (params)
		wpabuf_put_buf(buf, params);

	return 0;
}


static void sme_external_auth_send_sae_commit(struct wpa_supplicant *wpa_s,
					      const u8 *bssid,
					      struct wpa_ssid *ssid)
{
	struct wpabuf *resp, *buf;

	resp = sme_auth_build_sae_commit(wpa_s, ssid, bssid, 1);
	if (!resp)
		return;

	wpa_s->sme.sae.state = SAE_COMMITTED;
	buf = wpabuf_alloc(4 + SAE_COMMIT_MAX_LEN + wpabuf_len(resp));
	if (!buf) {
		wpabuf_free(resp);
		return;
	}

	wpa_s->sme.seq_num++;
	sme_external_auth_build_buf(buf, resp, wpa_s->own_addr,
				    bssid, 1, wpa_s->sme.seq_num);
	wpa_drv_send_mlme(wpa_s, wpabuf_head(buf), wpabuf_len(buf), 1, 0);
	wpabuf_free(resp);
	wpabuf_free(buf);
}


static void sme_send_external_auth_status(struct wpa_supplicant *wpa_s,
					  u16 status)
{
	struct external_auth params;

	os_memset(&params, 0, sizeof(params));
	params.status = status;
	os_memcpy(params.ssid, wpa_s->sme.ext_auth.ssid,
		  wpa_s->sme.ext_auth.ssid_len);
	params.ssid_len = wpa_s->sme.ext_auth.ssid_len;
	os_memcpy(params.bssid, wpa_s->sme.ext_auth.bssid, ETH_ALEN);
	wpa_drv_send_external_auth_status(wpa_s, &params);
}


static void sme_handle_external_auth_start(struct wpa_supplicant *wpa_s,
					   union wpa_event_data *data)
{
	struct wpa_ssid *ssid;
	size_t ssid_str_len = data->external_auth.ssid_len;
	u8 *ssid_str = data->external_auth.ssid;

	/* Get the SSID conf from the ssid string obtained */
	for (ssid = wpa_s->conf->ssid; ssid; ssid = ssid->next) {
		if (!wpas_network_disabled(wpa_s, ssid) &&
		    ssid_str_len == ssid->ssid_len &&
		    os_memcmp(ssid_str, ssid->ssid, ssid_str_len) == 0 &&
		    (ssid->key_mgmt & WPA_KEY_MGMT_SAE))
			break;
	}
	if (ssid)
		sme_external_auth_send_sae_commit(wpa_s,
						  data->external_auth.bssid,
						  ssid);
	else
		sme_send_external_auth_status(wpa_s,
					      WLAN_STATUS_UNSPECIFIED_FAILURE);
}


static void sme_external_auth_send_sae_confirm(struct wpa_supplicant *wpa_s,
					       const u8 *da)
{
	struct wpabuf *resp, *buf;

	resp = sme_auth_build_sae_confirm(wpa_s, 1);
	if (!resp) {
		wpa_printf(MSG_DEBUG, "SAE: Confirm message buf alloc failure");
		return;
	}

	wpa_s->sme.sae.state = SAE_CONFIRMED;
	buf = wpabuf_alloc(4 + SAE_CONFIRM_MAX_LEN + wpabuf_len(resp));
	if (!buf) {
		wpa_printf(MSG_DEBUG, "SAE: Auth Confirm buf alloc failure");
		wpabuf_free(resp);
		return;
	}
	wpa_s->sme.seq_num++;
	sme_external_auth_build_buf(buf, resp, wpa_s->own_addr,
				    da, 2, wpa_s->sme.seq_num);
	wpa_drv_send_mlme(wpa_s, wpabuf_head(buf), wpabuf_len(buf), 1, 0);
	wpabuf_free(resp);
	wpabuf_free(buf);
}


void sme_external_auth_trigger(struct wpa_supplicant *wpa_s,
			       union wpa_event_data *data)
{
	if (RSN_SELECTOR_GET(&data->external_auth.key_mgmt_suite) !=
	    RSN_AUTH_KEY_MGMT_SAE)
		return;

	if (data->external_auth.action == EXT_AUTH_START) {
		os_memcpy(&wpa_s->sme.ext_auth, data,
			  sizeof(struct external_auth));
		wpa_s->sme.seq_num = 0;
		wpa_s->sme.sae.state = SAE_NOTHING;
		wpa_s->sme.sae.send_confirm = 0;
		wpa_s->sme.sae_group_index = 0;
		sme_handle_external_auth_start(wpa_s, data);
	} else if (data->external_auth.action == EXT_AUTH_ABORT) {
		/* Report failure to driver for the wrong trigger */
		sme_send_external_auth_status(wpa_s,
					      WLAN_STATUS_UNSPECIFIED_FAILURE);
	}
}


static int sme_sae_auth(struct wpa_supplicant *wpa_s, u16 auth_transaction,
			u16 status_code, const u8 *data, size_t len,
			int external, const u8 *sa)
{
	int *groups;

	wpa_dbg(wpa_s, MSG_DEBUG, "SME: SAE authentication transaction %u "
		"status code %u", auth_transaction, status_code);

	if (auth_transaction == 1 &&
	    status_code == WLAN_STATUS_ANTI_CLOGGING_TOKEN_REQ &&
	    wpa_s->sme.sae.state == SAE_COMMITTED &&
	    (external || wpa_s->current_bss) && wpa_s->current_ssid) {
		int default_groups[] = { 19, 20, 21, 25, 26, 0 };
		u16 group;

		groups = wpa_s->conf->sae_groups;
		if (!groups || groups[0] <= 0)
			groups = default_groups;

		if (len < sizeof(le16)) {
			wpa_dbg(wpa_s, MSG_DEBUG,
				"SME: Too short SAE anti-clogging token request");
			return -1;
		}
		group = WPA_GET_LE16(data);
		wpa_dbg(wpa_s, MSG_DEBUG,
			"SME: SAE anti-clogging token requested (group %u)",
			group);
		if (sae_group_allowed(&wpa_s->sme.sae, groups, group) !=
		    WLAN_STATUS_SUCCESS) {
			wpa_dbg(wpa_s, MSG_ERROR,
				"SME: SAE group %u of anti-clogging request is invalid",
				group);
			return -1;
		}
		wpabuf_free(wpa_s->sme.sae_token);
		wpa_s->sme.sae_token = wpabuf_alloc_copy(data + sizeof(le16),
							 len - sizeof(le16));
		if (!external)
			sme_send_authentication(wpa_s, wpa_s->current_bss,
						wpa_s->current_ssid, 1);
		else
			sme_external_auth_send_sae_commit(
				wpa_s, wpa_s->sme.ext_auth.bssid,
				wpa_s->current_ssid);
		return 0;
	}

	if (auth_transaction == 1 &&
	    status_code == WLAN_STATUS_FINITE_CYCLIC_GROUP_NOT_SUPPORTED &&
	    wpa_s->sme.sae.state == SAE_COMMITTED &&
	    (external || wpa_s->current_bss) && wpa_s->current_ssid) {
		wpa_dbg(wpa_s, MSG_DEBUG, "SME: SAE group not supported");
		wpa_s->sme.sae_group_index++;
		if (sme_set_sae_group(wpa_s) < 0)
			return -1; /* no other groups enabled */
		wpa_dbg(wpa_s, MSG_DEBUG, "SME: Try next enabled SAE group");
		if (!external)
			sme_send_authentication(wpa_s, wpa_s->current_bss,
						wpa_s->current_ssid, 1);
		else
			sme_external_auth_send_sae_commit(
				wpa_s, wpa_s->sme.ext_auth.bssid,
				wpa_s->current_ssid);
		return 0;
	}

	if (auth_transaction == 1 &&
	    status_code == WLAN_STATUS_UNKNOWN_PASSWORD_IDENTIFIER) {
		const u8 *bssid = sa ? sa : wpa_s->pending_bssid;

		wpa_msg(wpa_s, MSG_INFO,
			WPA_EVENT_SAE_UNKNOWN_PASSWORD_IDENTIFIER MACSTR,
			MAC2STR(bssid));
		return -1;
	}

	if (status_code != WLAN_STATUS_SUCCESS)
		return -1;

	if (auth_transaction == 1) {
		u16 res;

		groups = wpa_s->conf->sae_groups;

		wpa_dbg(wpa_s, MSG_DEBUG, "SME SAE commit");
		if ((!external && wpa_s->current_bss == NULL) ||
		    wpa_s->current_ssid == NULL)
			return -1;
		if (wpa_s->sme.sae.state != SAE_COMMITTED)
			return -1;
		if (groups && groups[0] <= 0)
			groups = NULL;
		res = sae_parse_commit(&wpa_s->sme.sae, data, len, NULL, NULL,
				       groups);
		if (res == SAE_SILENTLY_DISCARD) {
			wpa_printf(MSG_DEBUG,
				   "SAE: Drop commit message due to reflection attack");
			return 0;
		}
		if (res != WLAN_STATUS_SUCCESS)
			return -1;

		if (sae_process_commit(&wpa_s->sme.sae) < 0) {
			wpa_printf(MSG_DEBUG, "SAE: Failed to process peer "
				   "commit");
			return -1;
		}

		wpabuf_free(wpa_s->sme.sae_token);
		wpa_s->sme.sae_token = NULL;
		if (!external)
			sme_send_authentication(wpa_s, wpa_s->current_bss,
						wpa_s->current_ssid, 0);
		else
			sme_external_auth_send_sae_confirm(wpa_s, sa);
		return 0;
	} else if (auth_transaction == 2) {
		wpa_dbg(wpa_s, MSG_DEBUG, "SME SAE confirm");
		if (wpa_s->sme.sae.state != SAE_CONFIRMED)
			return -1;
		if (sae_check_confirm(&wpa_s->sme.sae, data, len) < 0)
			return -1;
		wpa_s->sme.sae.state = SAE_ACCEPTED;
		sae_clear_temp_data(&wpa_s->sme.sae);

		if (external) {
			/* Report success to driver */
			sme_send_external_auth_status(wpa_s,
						      WLAN_STATUS_SUCCESS);
		}

		return 1;
	}

	return -1;
}


void sme_external_auth_mgmt_rx(struct wpa_supplicant *wpa_s,
			       const u8 *auth_frame, size_t len)
{
	const struct ieee80211_mgmt *header;
	size_t auth_length;

	header = (const struct ieee80211_mgmt *) auth_frame;
	auth_length = IEEE80211_HDRLEN + sizeof(header->u.auth);

	if (len < auth_length) {
		/* Notify failure to the driver */
		sme_send_external_auth_status(wpa_s,
					      WLAN_STATUS_UNSPECIFIED_FAILURE);
		return;
	}

	if (le_to_host16(header->u.auth.auth_alg) == WLAN_AUTH_SAE) {
		int res;

		res = sme_sae_auth(
			wpa_s, le_to_host16(header->u.auth.auth_transaction),
			le_to_host16(header->u.auth.status_code),
			header->u.auth.variable,
			len - auth_length, 1, header->sa);
		if (res < 0) {
			/* Notify failure to the driver */
			sme_send_external_auth_status(
				wpa_s, WLAN_STATUS_UNSPECIFIED_FAILURE);
			return;
		}
		if (res != 1)
			return;

		wpa_printf(MSG_DEBUG,
			   "SME: SAE completed - setting PMK for 4-way handshake");
		wpa_sm_set_pmk(wpa_s->wpa, wpa_s->sme.sae.pmk, PMK_LEN,
			       wpa_s->sme.sae.pmkid, wpa_s->pending_bssid);
	}
}

#endif /* CONFIG_SAE */


void sme_event_auth(struct wpa_supplicant *wpa_s, union wpa_event_data *data)
{
	struct wpa_ssid *ssid = wpa_s->current_ssid;

	if (ssid == NULL) {
		wpa_dbg(wpa_s, MSG_DEBUG, "SME: Ignore authentication event "
			"when network is not selected");
		return;
	}

	if (wpa_s->wpa_state != WPA_AUTHENTICATING) {
		wpa_dbg(wpa_s, MSG_DEBUG, "SME: Ignore authentication event "
			"when not in authenticating state");
		return;
	}

	if (os_memcmp(wpa_s->pending_bssid, data->auth.peer, ETH_ALEN) != 0) {
		wpa_dbg(wpa_s, MSG_DEBUG, "SME: Ignore authentication with "
			"unexpected peer " MACSTR,
			MAC2STR(data->auth.peer));
		return;
	}

	wpa_dbg(wpa_s, MSG_DEBUG, "SME: Authentication response: peer=" MACSTR
		" auth_type=%d auth_transaction=%d status_code=%d",
		MAC2STR(data->auth.peer), data->auth.auth_type,
		data->auth.auth_transaction, data->auth.status_code);
	wpa_hexdump(MSG_MSGDUMP, "SME: Authentication response IEs",
		    data->auth.ies, data->auth.ies_len);

	eloop_cancel_timeout(sme_auth_timer, wpa_s, NULL);

#ifdef CONFIG_SAE
	if (data->auth.auth_type == WLAN_AUTH_SAE) {
		int res;
		res = sme_sae_auth(wpa_s, data->auth.auth_transaction,
				   data->auth.status_code, data->auth.ies,
				   data->auth.ies_len, 0, NULL);
		if (res < 0) {
			wpas_connection_failed(wpa_s, wpa_s->pending_bssid);
			wpa_supplicant_set_state(wpa_s, WPA_DISCONNECTED);

		}
		if (res != 1)
			return;

		wpa_printf(MSG_DEBUG, "SME: SAE completed - setting PMK for "
			   "4-way handshake");
		wpa_sm_set_pmk(wpa_s->wpa, wpa_s->sme.sae.pmk, PMK_LEN,
			       wpa_s->sme.sae.pmkid, wpa_s->pending_bssid);
	}
#endif /* CONFIG_SAE */

	if (data->auth.status_code != WLAN_STATUS_SUCCESS) {
		char *ie_txt = NULL;

		if (data->auth.ies && data->auth.ies_len) {
			size_t buflen = 2 * data->auth.ies_len + 1;
			ie_txt = os_malloc(buflen);
			if (ie_txt) {
				wpa_snprintf_hex(ie_txt, buflen, data->auth.ies,
						 data->auth.ies_len);
			}
		}
		wpa_msg(wpa_s, MSG_INFO, WPA_EVENT_AUTH_REJECT MACSTR
			" auth_type=%u auth_transaction=%u status_code=%u%s%s",
			MAC2STR(data->auth.peer), data->auth.auth_type,
			data->auth.auth_transaction, data->auth.status_code,
			ie_txt ? " ie=" : "",
			ie_txt ? ie_txt : "");
		os_free(ie_txt);

#ifdef CONFIG_FILS
		if (wpa_s->sme.auth_alg == WPA_AUTH_ALG_FILS ||
		    wpa_s->sme.auth_alg == WPA_AUTH_ALG_FILS_SK_PFS)
			fils_connection_failure(wpa_s);
#endif /* CONFIG_FILS */

		if (data->auth.status_code !=
		    WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG ||
		    wpa_s->sme.auth_alg == data->auth.auth_type ||
		    wpa_s->current_ssid->auth_alg == WPA_AUTH_ALG_LEAP) {
			wpas_connection_failed(wpa_s, wpa_s->pending_bssid);
			wpa_supplicant_set_state(wpa_s, WPA_DISCONNECTED);
			return;
		}

		wpas_connect_work_done(wpa_s);

		switch (data->auth.auth_type) {
		case WLAN_AUTH_OPEN:
			wpa_s->current_ssid->auth_alg = WPA_AUTH_ALG_SHARED;

			wpa_dbg(wpa_s, MSG_DEBUG, "SME: Trying SHARED auth");
			wpa_supplicant_associate(wpa_s, wpa_s->current_bss,
						 wpa_s->current_ssid);
			return;

		case WLAN_AUTH_SHARED_KEY:
			wpa_s->current_ssid->auth_alg = WPA_AUTH_ALG_LEAP;

			wpa_dbg(wpa_s, MSG_DEBUG, "SME: Trying LEAP auth");
			wpa_supplicant_associate(wpa_s, wpa_s->current_bss,
						 wpa_s->current_ssid);
			return;

		default:
			return;
		}
	}

#ifdef CONFIG_IEEE80211R
	if (data->auth.auth_type == WLAN_AUTH_FT) {
		const u8 *ric_ies = NULL;
		size_t ric_ies_len = 0;

		if (wpa_s->ric_ies) {
			ric_ies = wpabuf_head(wpa_s->ric_ies);
			ric_ies_len = wpabuf_len(wpa_s->ric_ies);
		}
		if (wpa_ft_process_response(wpa_s->wpa, data->auth.ies,
					    data->auth.ies_len, 0,
					    data->auth.peer,
					    ric_ies, ric_ies_len) < 0) {
			wpa_dbg(wpa_s, MSG_DEBUG,
				"SME: FT Authentication response processing failed");
			wpa_msg(wpa_s, MSG_INFO, WPA_EVENT_DISCONNECTED "bssid="
				MACSTR
				" reason=%d locally_generated=1",
				MAC2STR(wpa_s->pending_bssid),
				WLAN_REASON_DEAUTH_LEAVING);
			wpas_connection_failed(wpa_s, wpa_s->pending_bssid);
			wpa_supplicant_mark_disassoc(wpa_s);
			return;
		}
	}
#endif /* CONFIG_IEEE80211R */

#ifdef CONFIG_FILS
	if (data->auth.auth_type == WLAN_AUTH_FILS_SK ||
	    data->auth.auth_type == WLAN_AUTH_FILS_SK_PFS) {
		u16 expect_auth_type;

		expect_auth_type = wpa_s->sme.auth_alg ==
			WPA_AUTH_ALG_FILS_SK_PFS ? WLAN_AUTH_FILS_SK_PFS :
			WLAN_AUTH_FILS_SK;
		if (data->auth.auth_type != expect_auth_type) {
			wpa_dbg(wpa_s, MSG_DEBUG,
				"SME: FILS Authentication response used different auth alg (%u; expected %u)",
				data->auth.auth_type, expect_auth_type);
			wpa_msg(wpa_s, MSG_INFO, WPA_EVENT_DISCONNECTED "bssid="
				MACSTR
				" reason=%d locally_generated=1",
				MAC2STR(wpa_s->pending_bssid),
				WLAN_REASON_DEAUTH_LEAVING);
			wpas_connection_failed(wpa_s, wpa_s->pending_bssid);
			wpa_supplicant_mark_disassoc(wpa_s);
			return;
		}

		if (fils_process_auth(wpa_s->wpa, wpa_s->pending_bssid,
				      data->auth.ies, data->auth.ies_len) < 0) {
			wpa_dbg(wpa_s, MSG_DEBUG,
				"SME: FILS Authentication response processing failed");
			wpa_msg(wpa_s, MSG_INFO, WPA_EVENT_DISCONNECTED "bssid="
				MACSTR
				" reason=%d locally_generated=1",
				MAC2STR(wpa_s->pending_bssid),
				WLAN_REASON_DEAUTH_LEAVING);
			wpas_connection_failed(wpa_s, wpa_s->pending_bssid);
			wpa_supplicant_mark_disassoc(wpa_s);
			return;
		}
	}
#endif /* CONFIG_FILS */

	sme_associate(wpa_s, ssid->mode, data->auth.peer,
		      data->auth.auth_type);
}


#ifdef CONFIG_FILS
#ifdef CONFIG_IEEE80211R
static void remove_ie(u8 *buf, size_t *len, u8 eid)
{
	u8 *pos, *next, *end;

	pos = (u8 *) get_ie(buf, *len, eid);
	if (pos) {
		next = pos + 2 + pos[1];
		end = buf + *len;
		*len -= 2 + pos[1];
		os_memmove(pos, next, end - next);
	}
}
#endif /* CONFIG_IEEE80211R */
#endif /* CONFIG_FILS */


void sme_associate(struct wpa_supplicant *wpa_s, enum wpas_mode mode,
		   const u8 *bssid, u16 auth_type)
{
	struct wpa_driver_associate_params params;
	struct ieee802_11_elems elems;
#ifdef CONFIG_FILS
	u8 nonces[2 * FILS_NONCE_LEN];
#endif /* CONFIG_FILS */
#ifdef CONFIG_HT_OVERRIDES
	struct ieee80211_ht_capabilities htcaps;
	struct ieee80211_ht_capabilities htcaps_mask;
#endif /* CONFIG_HT_OVERRIDES */
#ifdef CONFIG_VHT_OVERRIDES
	struct ieee80211_vht_capabilities vhtcaps;
	struct ieee80211_vht_capabilities vhtcaps_mask;
#endif /* CONFIG_VHT_OVERRIDES */

	os_memset(&params, 0, sizeof(params));

#ifdef CONFIG_FILS
	if (auth_type == WLAN_AUTH_FILS_SK ||
	    auth_type == WLAN_AUTH_FILS_SK_PFS) {
		struct wpabuf *buf;
		const u8 *snonce, *anonce;
		const unsigned int max_hlp = 20;
		struct wpabuf *hlp[max_hlp];
		unsigned int i, num_hlp = 0;
		struct fils_hlp_req *req;

		dl_list_for_each(req, &wpa_s->fils_hlp_req, struct fils_hlp_req,
				 list) {
			hlp[num_hlp] = wpabuf_alloc(2 * ETH_ALEN + 6 +
					      wpabuf_len(req->pkt));
			if (!hlp[num_hlp])
				break;
			wpabuf_put_data(hlp[num_hlp], req->dst, ETH_ALEN);
			wpabuf_put_data(hlp[num_hlp], wpa_s->own_addr,
					ETH_ALEN);
			wpabuf_put_data(hlp[num_hlp],
					"\xaa\xaa\x03\x00\x00\x00", 6);
			wpabuf_put_buf(hlp[num_hlp], req->pkt);
			num_hlp++;
			if (num_hlp >= max_hlp)
				break;
		}

		buf = fils_build_assoc_req(wpa_s->wpa, &params.fils_kek,
					   &params.fils_kek_len, &snonce,
					   &anonce,
					   (const struct wpabuf **) hlp,
					   num_hlp);
		for (i = 0; i < num_hlp; i++)
			wpabuf_free(hlp[i]);
		if (!buf)
			return;
		wpa_hexdump(MSG_DEBUG, "FILS: assoc_req before FILS elements",
			    wpa_s->sme.assoc_req_ie,
			    wpa_s->sme.assoc_req_ie_len);
#ifdef CONFIG_IEEE80211R
		if (wpa_key_mgmt_ft(wpa_s->key_mgmt)) {
			/* Remove RSNE and MDE to allow them to be overridden
			 * with FILS+FT specific values from
			 * fils_build_assoc_req(). */
			remove_ie(wpa_s->sme.assoc_req_ie,
				  &wpa_s->sme.assoc_req_ie_len,
				  WLAN_EID_RSN);
			wpa_hexdump(MSG_DEBUG,
				    "FILS: assoc_req after RSNE removal",
				    wpa_s->sme.assoc_req_ie,
				    wpa_s->sme.assoc_req_ie_len);
			remove_ie(wpa_s->sme.assoc_req_ie,
				  &wpa_s->sme.assoc_req_ie_len,
				  WLAN_EID_MOBILITY_DOMAIN);
			wpa_hexdump(MSG_DEBUG,
				    "FILS: assoc_req after MDE removal",
				    wpa_s->sme.assoc_req_ie,
				    wpa_s->sme.assoc_req_ie_len);
		}
#endif /* CONFIG_IEEE80211R */
		/* TODO: Make wpa_s->sme.assoc_req_ie use dynamic allocation */
		if (wpa_s->sme.assoc_req_ie_len + wpabuf_len(buf) >
		    sizeof(wpa_s->sme.assoc_req_ie)) {
			wpa_printf(MSG_ERROR,
				   "FILS: Not enough buffer room for own AssocReq elements");
			wpabuf_free(buf);
			return;
		}
		os_memcpy(wpa_s->sme.assoc_req_ie + wpa_s->sme.assoc_req_ie_len,
			  wpabuf_head(buf), wpabuf_len(buf));
		wpa_s->sme.assoc_req_ie_len += wpabuf_len(buf);
		wpabuf_free(buf);
		wpa_hexdump(MSG_DEBUG, "FILS: assoc_req after FILS elements",
			    wpa_s->sme.assoc_req_ie,
			    wpa_s->sme.assoc_req_ie_len);

		os_memcpy(nonces, snonce, FILS_NONCE_LEN);
		os_memcpy(nonces + FILS_NONCE_LEN, anonce, FILS_NONCE_LEN);
		params.fils_nonces = nonces;
		params.fils_nonces_len = sizeof(nonces);
	}
#endif /* CONFIG_FILS */

#ifdef CONFIG_OWE
#ifdef CONFIG_TESTING_OPTIONS
	if (get_ie_ext(wpa_s->sme.assoc_req_ie, wpa_s->sme.assoc_req_ie_len,
		       WLAN_EID_EXT_OWE_DH_PARAM)) {
		wpa_printf(MSG_INFO, "TESTING: Override OWE DH element");
	} else
#endif /* CONFIG_TESTING_OPTIONS */
	if (auth_type == WLAN_AUTH_OPEN &&
	    wpa_s->key_mgmt == WPA_KEY_MGMT_OWE) {
		struct wpabuf *owe_ie;
		u16 group;

		if (wpa_s->current_ssid && wpa_s->current_ssid->owe_group) {
			group = wpa_s->current_ssid->owe_group;
		} else if (wpa_s->assoc_status_code ==
			   WLAN_STATUS_FINITE_CYCLIC_GROUP_NOT_SUPPORTED) {
			if (wpa_s->last_owe_group == 19)
				group = 20;
			else if (wpa_s->last_owe_group == 20)
				group = 21;
			else
				group = OWE_DH_GROUP;
		} else {
			group = OWE_DH_GROUP;
		}

		wpa_s->last_owe_group = group;
		wpa_printf(MSG_DEBUG, "OWE: Try to use group %u", group);
		owe_ie = owe_build_assoc_req(wpa_s->wpa, group);
		if (!owe_ie) {
			wpa_printf(MSG_ERROR,
				   "OWE: Failed to build IE for Association Request frame");
			return;
		}
		if (wpa_s->sme.assoc_req_ie_len + wpabuf_len(owe_ie) >
		    sizeof(wpa_s->sme.assoc_req_ie)) {
			wpa_printf(MSG_ERROR,
				   "OWE: Not enough buffer room for own Association Request frame elements");
			wpabuf_free(owe_ie);
			return;
		}
		os_memcpy(wpa_s->sme.assoc_req_ie + wpa_s->sme.assoc_req_ie_len,
			  wpabuf_head(owe_ie), wpabuf_len(owe_ie));
		wpa_s->sme.assoc_req_ie_len += wpabuf_len(owe_ie);
		wpabuf_free(owe_ie);
	}
#endif /* CONFIG_OWE */

	params.bssid = bssid;
	params.ssid = wpa_s->sme.ssid;
	params.ssid_len = wpa_s->sme.ssid_len;
	params.freq.freq = wpa_s->sme.freq;
	params.bg_scan_period = wpa_s->current_ssid ?
		wpa_s->current_ssid->bg_scan_period : -1;
	params.wpa_ie = wpa_s->sme.assoc_req_ie_len ?
		wpa_s->sme.assoc_req_ie : NULL;
	params.wpa_ie_len = wpa_s->sme.assoc_req_ie_len;
	wpa_hexdump(MSG_DEBUG, "SME: Association Request IEs",
		    params.wpa_ie, params.wpa_ie_len);
	params.pairwise_suite = wpa_s->pairwise_cipher;
	params.group_suite = wpa_s->group_cipher;
	params.mgmt_group_suite = wpa_s->mgmt_group_cipher;
	params.key_mgmt_suite = wpa_s->key_mgmt;
	params.wpa_proto = wpa_s->wpa_proto;
#ifdef CONFIG_HT_OVERRIDES
	os_memset(&htcaps, 0, sizeof(htcaps));
	os_memset(&htcaps_mask, 0, sizeof(htcaps_mask));
	params.htcaps = (u8 *) &htcaps;
	params.htcaps_mask = (u8 *) &htcaps_mask;
	wpa_supplicant_apply_ht_overrides(wpa_s, wpa_s->current_ssid, &params);
#endif /* CONFIG_HT_OVERRIDES */
#ifdef CONFIG_VHT_OVERRIDES
	os_memset(&vhtcaps, 0, sizeof(vhtcaps));
	os_memset(&vhtcaps_mask, 0, sizeof(vhtcaps_mask));
	params.vhtcaps = &vhtcaps;
	params.vhtcaps_mask = &vhtcaps_mask;
	wpa_supplicant_apply_vht_overrides(wpa_s, wpa_s->current_ssid, &params);
#endif /* CONFIG_VHT_OVERRIDES */
#ifdef CONFIG_IEEE80211R
	if (auth_type == WLAN_AUTH_FT && wpa_s->sme.ft_ies &&
	    get_ie(wpa_s->sme.ft_ies, wpa_s->sme.ft_ies_len,
		   WLAN_EID_RIC_DATA)) {
		/* There seems to be a pretty inconvenient bug in the Linux
		 * kernel IE splitting functionality when RIC is used. For now,
		 * skip correct behavior in IE construction here (i.e., drop the
		 * additional non-FT-specific IEs) to avoid kernel issues. This
		 * is fine since RIC is used only for testing purposes in the
		 * current implementation. */
		wpa_printf(MSG_INFO,
			   "SME: Linux kernel workaround - do not try to include additional IEs with RIC");
		params.wpa_ie = wpa_s->sme.ft_ies;
		params.wpa_ie_len = wpa_s->sme.ft_ies_len;
	} else if (auth_type == WLAN_AUTH_FT && wpa_s->sme.ft_ies) {
		const u8 *rm_en, *pos, *end;
		size_t rm_en_len = 0;
		u8 *rm_en_dup = NULL, *wpos;

		/* Remove RSNE, MDE, FTE to allow them to be overridden with
		 * FT specific values */
		remove_ie(wpa_s->sme.assoc_req_ie,
			  &wpa_s->sme.assoc_req_ie_len,
			  WLAN_EID_RSN);
		remove_ie(wpa_s->sme.assoc_req_ie,
			  &wpa_s->sme.assoc_req_ie_len,
			  WLAN_EID_MOBILITY_DOMAIN);
		remove_ie(wpa_s->sme.assoc_req_ie,
			  &wpa_s->sme.assoc_req_ie_len,
			  WLAN_EID_FAST_BSS_TRANSITION);
		rm_en = get_ie(wpa_s->sme.assoc_req_ie,
			       wpa_s->sme.assoc_req_ie_len,
			       WLAN_EID_RRM_ENABLED_CAPABILITIES);
		if (rm_en) {
			/* Need to remove RM Enabled Capabilities element as
			 * well temporarily, so that it can be placed between
			 * RSNE and MDE. */
			rm_en_len = 2 + rm_en[1];
			rm_en_dup = os_memdup(rm_en, rm_en_len);
			remove_ie(wpa_s->sme.assoc_req_ie,
				  &wpa_s->sme.assoc_req_ie_len,
				  WLAN_EID_RRM_ENABLED_CAPABILITIES);
		}
		wpa_hexdump(MSG_DEBUG,
			    "SME: Association Request IEs after FT IE removal",
			    wpa_s->sme.assoc_req_ie,
			    wpa_s->sme.assoc_req_ie_len);
		if (wpa_s->sme.assoc_req_ie_len + wpa_s->sme.ft_ies_len +
		    rm_en_len > sizeof(wpa_s->sme.assoc_req_ie)) {
			wpa_printf(MSG_ERROR,
				   "SME: Not enough buffer room for FT IEs in Association Request frame");
			os_free(rm_en_dup);
			return;
		}

		os_memmove(wpa_s->sme.assoc_req_ie + wpa_s->sme.ft_ies_len +
			   rm_en_len,
			   wpa_s->sme.assoc_req_ie,
			   wpa_s->sme.assoc_req_ie_len);
		pos = wpa_s->sme.ft_ies;
		end = pos + wpa_s->sme.ft_ies_len;
		wpos = wpa_s->sme.assoc_req_ie;
		if (*pos == WLAN_EID_RSN) {
			os_memcpy(wpos, pos, 2 + pos[1]);
			wpos += 2 + pos[1];
			pos += 2 + pos[1];
		}
		if (rm_en_dup) {
			os_memcpy(wpos, rm_en_dup, rm_en_len);
			wpos += rm_en_len;
			os_free(rm_en_dup);
		}
		os_memcpy(wpos, pos, end - pos);
		wpa_s->sme.assoc_req_ie_len += wpa_s->sme.ft_ies_len +
			rm_en_len;
		params.wpa_ie = wpa_s->sme.assoc_req_ie;
		params.wpa_ie_len = wpa_s->sme.assoc_req_ie_len;
		wpa_hexdump(MSG_DEBUG,
			    "SME: Association Request IEs after FT override",
			    params.wpa_ie, params.wpa_ie_len);
	}
#endif /* CONFIG_IEEE80211R */
	params.mode = mode;
	params.mgmt_frame_protection = wpa_s->sme.mfp;
	params.rrm_used = wpa_s->rrm.rrm_used;
	if (wpa_s->sme.prev_bssid_set)
		params.prev_bssid = wpa_s->sme.prev_bssid;

	wpa_msg(wpa_s, MSG_INFO, "Trying to associate with " MACSTR
		" (SSID='%s' freq=%d MHz)", MAC2STR(params.bssid),
		params.ssid ? wpa_ssid_txt(params.ssid, params.ssid_len) : "",
		params.freq.freq);

	wpa_supplicant_set_state(wpa_s, WPA_ASSOCIATING);

	if (params.wpa_ie == NULL ||
	    ieee802_11_parse_elems(params.wpa_ie, params.wpa_ie_len, &elems, 0)
	    < 0) {
		wpa_dbg(wpa_s, MSG_DEBUG, "SME: Could not parse own IEs?!");
		os_memset(&elems, 0, sizeof(elems));
	}
	if (elems.rsn_ie) {
		params.wpa_proto = WPA_PROTO_RSN;
		wpa_sm_set_assoc_wpa_ie(wpa_s->wpa, elems.rsn_ie - 2,
					elems.rsn_ie_len + 2);
	} else if (elems.wpa_ie) {
		params.wpa_proto = WPA_PROTO_WPA;
		wpa_sm_set_assoc_wpa_ie(wpa_s->wpa, elems.wpa_ie - 2,
					elems.wpa_ie_len + 2);
	} else if (elems.osen) {
		params.wpa_proto = WPA_PROTO_OSEN;
		wpa_sm_set_assoc_wpa_ie(wpa_s->wpa, elems.osen - 2,
					elems.osen_len + 2);
	} else
		wpa_sm_set_assoc_wpa_ie(wpa_s->wpa, NULL, 0);
	if (wpa_s->current_ssid && wpa_s->current_ssid->p2p_group)
		params.p2p = 1;

	if (wpa_s->p2pdev->set_sta_uapsd)
		params.uapsd = wpa_s->p2pdev->sta_uapsd;
	else
		params.uapsd = -1;

	if (wpa_drv_associate(wpa_s, &params) < 0) {
		wpa_msg(wpa_s, MSG_INFO, "SME: Association request to the "
			"driver failed");
		wpas_connection_failed(wpa_s, wpa_s->pending_bssid);
		wpa_supplicant_set_state(wpa_s, WPA_DISCONNECTED);
		os_memset(wpa_s->pending_bssid, 0, ETH_ALEN);
		return;
	}

	eloop_register_timeout(SME_ASSOC_TIMEOUT, 0, sme_assoc_timer, wpa_s,
			       NULL);

#ifdef CONFIG_TESTING_OPTIONS
	wpabuf_free(wpa_s->last_assoc_req_wpa_ie);
	wpa_s->last_assoc_req_wpa_ie = NULL;
	if (params.wpa_ie)
		wpa_s->last_assoc_req_wpa_ie =
			wpabuf_alloc_copy(params.wpa_ie, params.wpa_ie_len);
#endif /* CONFIG_TESTING_OPTIONS */
}


int sme_update_ft_ies(struct wpa_supplicant *wpa_s, const u8 *md,
		      const u8 *ies, size_t ies_len)
{
	if (md == NULL || ies == NULL) {
		wpa_dbg(wpa_s, MSG_DEBUG, "SME: Remove mobility domain");
		os_free(wpa_s->sme.ft_ies);
		wpa_s->sme.ft_ies = NULL;
		wpa_s->sme.ft_ies_len = 0;
		wpa_s->sme.ft_used = 0;
		return 0;
	}

	os_memcpy(wpa_s->sme.mobility_domain, md, MOBILITY_DOMAIN_ID_LEN);
	wpa_hexdump(MSG_DEBUG, "SME: FT IEs", ies, ies_len);
	os_free(wpa_s->sme.ft_ies);
	wpa_s->sme.ft_ies = os_memdup(ies, ies_len);
	if (wpa_s->sme.ft_ies == NULL)
		return -1;
	wpa_s->sme.ft_ies_len = ies_len;
	return 0;
}


static void sme_deauth(struct wpa_supplicant *wpa_s)
{
	int bssid_changed;

	bssid_changed = !is_zero_ether_addr(wpa_s->bssid);

	if (wpa_drv_deauthenticate(wpa_s, wpa_s->pending_bssid,
				   WLAN_REASON_DEAUTH_LEAVING) < 0) {
		wpa_msg(wpa_s, MSG_INFO, "SME: Deauth request to the driver "
			"failed");
	}
	wpa_s->sme.prev_bssid_set = 0;

	wpas_connection_failed(wpa_s, wpa_s->pending_bssid);
	wpa_supplicant_set_state(wpa_s, WPA_DISCONNECTED);
	os_memset(wpa_s->bssid, 0, ETH_ALEN);
	os_memset(wpa_s->pending_bssid, 0, ETH_ALEN);
	if (bssid_changed)
		wpas_notify_bssid_changed(wpa_s);
}


void sme_event_assoc_reject(struct wpa_supplicant *wpa_s,
			    union wpa_event_data *data)
{
	wpa_dbg(wpa_s, MSG_DEBUG, "SME: Association with " MACSTR " failed: "
		"status code %d", MAC2STR(wpa_s->pending_bssid),
		data->assoc_reject.status_code);

	eloop_cancel_timeout(sme_assoc_timer, wpa_s, NULL);

#ifdef CONFIG_SAE
	if (wpa_s->sme.sae_pmksa_caching && wpa_s->current_ssid &&
	    wpa_key_mgmt_sae(wpa_s->current_ssid->key_mgmt)) {
		wpa_dbg(wpa_s, MSG_DEBUG,
			"PMKSA caching attempt rejected - drop PMKSA cache entry and fall back to SAE authentication");
		wpa_sm_aborted_cached(wpa_s->wpa);
		wpa_sm_pmksa_cache_flush(wpa_s->wpa, wpa_s->current_ssid);
		if (wpa_s->current_bss) {
			struct wpa_bss *bss = wpa_s->current_bss;
			struct wpa_ssid *ssid = wpa_s->current_ssid;

			wpa_drv_deauthenticate(wpa_s, wpa_s->pending_bssid,
					       WLAN_REASON_DEAUTH_LEAVING);
			wpas_connect_work_done(wpa_s);
			wpa_supplicant_mark_disassoc(wpa_s);
			wpa_supplicant_connect(wpa_s, bss, ssid);
			return;
		}
	}
#endif /* CONFIG_SAE */

	/*
	 * For now, unconditionally terminate the previous authentication. In
	 * theory, this should not be needed, but mac80211 gets quite confused
	 * if the authentication is left pending.. Some roaming cases might
	 * benefit from using the previous authentication, so this could be
	 * optimized in the future.
	 */
	sme_deauth(wpa_s);
}


void sme_event_auth_timed_out(struct wpa_supplicant *wpa_s,
			      union wpa_event_data *data)
{
	wpa_dbg(wpa_s, MSG_DEBUG, "SME: Authentication timed out");
	wpas_connection_failed(wpa_s, wpa_s->pending_bssid);
	wpa_supplicant_mark_disassoc(wpa_s);
}


void sme_event_assoc_timed_out(struct wpa_supplicant *wpa_s,
			       union wpa_event_data *data)
{
	wpa_dbg(wpa_s, MSG_DEBUG, "SME: Association timed out");
	wpas_connection_failed(wpa_s, wpa_s->pending_bssid);
	wpa_supplicant_mark_disassoc(wpa_s);
}


void sme_event_disassoc(struct wpa_supplicant *wpa_s,
			struct disassoc_info *info)
{
	wpa_dbg(wpa_s, MSG_DEBUG, "SME: Disassociation event received");
	if (wpa_s->sme.prev_bssid_set) {
		/*
		 * cfg80211/mac80211 can get into somewhat confused state if
		 * the AP only disassociates us and leaves us in authenticated
		 * state. For now, force the state to be cleared to avoid
		 * confusing errors if we try to associate with the AP again.
		 */
		wpa_dbg(wpa_s, MSG_DEBUG, "SME: Deauthenticate to clear "
			"driver state");
		wpa_drv_deauthenticate(wpa_s, wpa_s->sme.prev_bssid,
				       WLAN_REASON_DEAUTH_LEAVING);
	}
}


static void sme_auth_timer(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;
	if (wpa_s->wpa_state == WPA_AUTHENTICATING) {
		wpa_msg(wpa_s, MSG_DEBUG, "SME: Authentication timeout");
		sme_deauth(wpa_s);
	}
}


static void sme_assoc_timer(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;
	if (wpa_s->wpa_state == WPA_ASSOCIATING) {
		wpa_msg(wpa_s, MSG_DEBUG, "SME: Association timeout");
		sme_deauth(wpa_s);
	}
}


void sme_state_changed(struct wpa_supplicant *wpa_s)
{
	/* Make sure timers are cleaned up appropriately. */
	if (wpa_s->wpa_state != WPA_ASSOCIATING)
		eloop_cancel_timeout(sme_assoc_timer, wpa_s, NULL);
	if (wpa_s->wpa_state != WPA_AUTHENTICATING)
		eloop_cancel_timeout(sme_auth_timer, wpa_s, NULL);
}


void sme_disassoc_while_authenticating(struct wpa_supplicant *wpa_s,
				       const u8 *prev_pending_bssid)
{
	/*
	 * mac80211-workaround to force deauth on failed auth cmd,
	 * requires us to remain in authenticating state to allow the
	 * second authentication attempt to be continued properly.
	 */
	wpa_dbg(wpa_s, MSG_DEBUG, "SME: Allow pending authentication "
		"to proceed after disconnection event");
	wpa_supplicant_set_state(wpa_s, WPA_AUTHENTICATING);
	os_memcpy(wpa_s->pending_bssid, prev_pending_bssid, ETH_ALEN);

	/*
	 * Re-arm authentication timer in case auth fails for whatever reason.
	 */
	eloop_cancel_timeout(sme_auth_timer, wpa_s, NULL);
	eloop_register_timeout(SME_AUTH_TIMEOUT, 0, sme_auth_timer, wpa_s,
			       NULL);
}


void sme_clear_on_disassoc(struct wpa_supplicant *wpa_s)
{
	wpa_s->sme.prev_bssid_set = 0;
#ifdef CONFIG_SAE
	wpabuf_free(wpa_s->sme.sae_token);
	wpa_s->sme.sae_token = NULL;
	sae_clear_data(&wpa_s->sme.sae);
#endif /* CONFIG_SAE */
#ifdef CONFIG_IEEE80211R
	if (wpa_s->sme.ft_ies || wpa_s->sme.ft_used)
		sme_update_ft_ies(wpa_s, NULL, NULL, 0);
#endif /* CONFIG_IEEE80211R */
}


void sme_deinit(struct wpa_supplicant *wpa_s)
{
	os_free(wpa_s->sme.ft_ies);
	wpa_s->sme.ft_ies = NULL;
	wpa_s->sme.ft_ies_len = 0;
#ifdef CONFIG_IEEE80211W
	sme_stop_sa_query(wpa_s);
#endif /* CONFIG_IEEE80211W */
	sme_clear_on_disassoc(wpa_s);

	eloop_cancel_timeout(sme_assoc_timer, wpa_s, NULL);
	eloop_cancel_timeout(sme_auth_timer, wpa_s, NULL);
	eloop_cancel_timeout(sme_obss_scan_timeout, wpa_s, NULL);
}


static void sme_send_2040_bss_coex(struct wpa_supplicant *wpa_s,
				   const u8 *chan_list, u8 num_channels,
				   u8 num_intol)
{
	struct ieee80211_2040_bss_coex_ie *bc_ie;
	struct ieee80211_2040_intol_chan_report *ic_report;
	struct wpabuf *buf;

	wpa_printf(MSG_DEBUG, "SME: Send 20/40 BSS Coexistence to " MACSTR
		   " (num_channels=%u num_intol=%u)",
		   MAC2STR(wpa_s->bssid), num_channels, num_intol);
	wpa_hexdump(MSG_DEBUG, "SME: 20/40 BSS Intolerant Channels",
		    chan_list, num_channels);

	buf = wpabuf_alloc(2 + /* action.category + action_code */
			   sizeof(struct ieee80211_2040_bss_coex_ie) +
			   sizeof(struct ieee80211_2040_intol_chan_report) +
			   num_channels);
	if (buf == NULL)
		return;

	wpabuf_put_u8(buf, WLAN_ACTION_PUBLIC);
	wpabuf_put_u8(buf, WLAN_PA_20_40_BSS_COEX);

	bc_ie = wpabuf_put(buf, sizeof(*bc_ie));
	bc_ie->element_id = WLAN_EID_20_40_BSS_COEXISTENCE;
	bc_ie->length = 1;
	if (num_intol)
		bc_ie->coex_param |= WLAN_20_40_BSS_COEX_20MHZ_WIDTH_REQ;

	if (num_channels > 0) {
		ic_report = wpabuf_put(buf, sizeof(*ic_report));
		ic_report->element_id = WLAN_EID_20_40_BSS_INTOLERANT;
		ic_report->length = num_channels + 1;
		ic_report->op_class = 0;
		os_memcpy(wpabuf_put(buf, num_channels), chan_list,
			  num_channels);
	}

	if (wpa_drv_send_action(wpa_s, wpa_s->assoc_freq, 0, wpa_s->bssid,
				wpa_s->own_addr, wpa_s->bssid,
				wpabuf_head(buf), wpabuf_len(buf), 0) < 0) {
		wpa_msg(wpa_s, MSG_INFO,
			"SME: Failed to send 20/40 BSS Coexistence frame");
	}

	wpabuf_free(buf);
}


int sme_proc_obss_scan(struct wpa_supplicant *wpa_s)
{
	struct wpa_bss *bss;
	const u8 *ie;
	u16 ht_cap;
	u8 chan_list[P2P_MAX_CHANNELS], channel;
	u8 num_channels = 0, num_intol = 0, i;

	if (!wpa_s->sme.sched_obss_scan)
		return 0;

	wpa_s->sme.sched_obss_scan = 0;
	if (!wpa_s->current_bss || wpa_s->wpa_state != WPA_COMPLETED)
		return 1;

	/*
	 * Check whether AP uses regulatory triplet or channel triplet in
	 * country info. Right now the operating class of the BSS channel
	 * width trigger event is "unknown" (IEEE Std 802.11-2012 10.15.12),
	 * based on the assumption that operating class triplet is not used in
	 * beacon frame. If the First Channel Number/Operating Extension
	 * Identifier octet has a positive integer value of 201 or greater,
	 * then its operating class triplet.
	 *
	 * TODO: If Supported Operating Classes element is present in beacon
	 * frame, have to lookup operating class in Annex E and fill them in
	 * 2040 coex frame.
	 */
	ie = wpa_bss_get_ie(wpa_s->current_bss, WLAN_EID_COUNTRY);
	if (ie && (ie[1] >= 6) && (ie[5] >= 201))
		return 1;

	os_memset(chan_list, 0, sizeof(chan_list));

	dl_list_for_each(bss, &wpa_s->bss, struct wpa_bss, list) {
		/* Skip other band bss */
		enum hostapd_hw_mode mode;
		mode = ieee80211_freq_to_chan(bss->freq, &channel);
		if (mode != HOSTAPD_MODE_IEEE80211G &&
		    mode != HOSTAPD_MODE_IEEE80211B)
			continue;

		ie = wpa_bss_get_ie(bss, WLAN_EID_HT_CAP);
		ht_cap = (ie && (ie[1] == 26)) ? WPA_GET_LE16(ie + 2) : 0;
		wpa_printf(MSG_DEBUG, "SME OBSS scan BSS " MACSTR
			   " freq=%u chan=%u ht_cap=0x%x",
			   MAC2STR(bss->bssid), bss->freq, channel, ht_cap);

		if (!ht_cap || (ht_cap & HT_CAP_INFO_40MHZ_INTOLERANT)) {
			if (ht_cap & HT_CAP_INFO_40MHZ_INTOLERANT)
				num_intol++;

			/* Check whether the channel is already considered */
			for (i = 0; i < num_channels; i++) {
				if (channel == chan_list[i])
					break;
			}
			if (i != num_channels)
				continue;

			chan_list[num_channels++] = channel;
		}
	}

	sme_send_2040_bss_coex(wpa_s, chan_list, num_channels, num_intol);
	return 1;
}


static void wpa_obss_scan_freqs_list(struct wpa_supplicant *wpa_s,
				     struct wpa_driver_scan_params *params)
{
	/* Include only affected channels */
	struct hostapd_hw_modes *mode;
	int count, i;
	int start, end;

	mode = get_mode(wpa_s->hw.modes, wpa_s->hw.num_modes,
			HOSTAPD_MODE_IEEE80211G);
	if (mode == NULL) {
		/* No channels supported in this band - use empty list */
		params->freqs = os_zalloc(sizeof(int));
		return;
	}

	if (wpa_s->sme.ht_sec_chan == HT_SEC_CHAN_UNKNOWN &&
	    wpa_s->current_bss) {
		const u8 *ie;

		ie = wpa_bss_get_ie(wpa_s->current_bss, WLAN_EID_HT_OPERATION);
		if (ie && ie[1] >= 2) {
			u8 o;

			o = ie[3] & HT_INFO_HT_PARAM_SECONDARY_CHNL_OFF_MASK;
			if (o == HT_INFO_HT_PARAM_SECONDARY_CHNL_ABOVE)
				wpa_s->sme.ht_sec_chan = HT_SEC_CHAN_ABOVE;
			else if (o == HT_INFO_HT_PARAM_SECONDARY_CHNL_BELOW)
				wpa_s->sme.ht_sec_chan = HT_SEC_CHAN_BELOW;
		}
	}

	start = wpa_s->assoc_freq - 10;
	end = wpa_s->assoc_freq + 10;
	switch (wpa_s->sme.ht_sec_chan) {
	case HT_SEC_CHAN_UNKNOWN:
		/* HT40+ possible on channels 1..9 */
		if (wpa_s->assoc_freq <= 2452)
			start -= 20;
		/* HT40- possible on channels 5-13 */
		if (wpa_s->assoc_freq >= 2432)
			end += 20;
		break;
	case HT_SEC_CHAN_ABOVE:
		end += 20;
		break;
	case HT_SEC_CHAN_BELOW:
		start -= 20;
		break;
	}
	wpa_printf(MSG_DEBUG,
		   "OBSS: assoc_freq %d possible affected range %d-%d",
		   wpa_s->assoc_freq, start, end);

	params->freqs = os_calloc(mode->num_channels + 1, sizeof(int));
	if (params->freqs == NULL)
		return;
	for (count = 0, i = 0; i < mode->num_channels; i++) {
		int freq;

		if (mode->channels[i].flag & HOSTAPD_CHAN_DISABLED)
			continue;
		freq = mode->channels[i].freq;
		if (freq - 10 >= end || freq + 10 <= start)
			continue; /* not affected */
		params->freqs[count++] = freq;
	}
}


static void sme_obss_scan_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;
	struct wpa_driver_scan_params params;

	if (!wpa_s->current_bss) {
		wpa_printf(MSG_DEBUG, "SME OBSS: Ignore scan request");
		return;
	}

	os_memset(&params, 0, sizeof(params));
	wpa_obss_scan_freqs_list(wpa_s, &params);
	params.low_priority = 1;
	wpa_printf(MSG_DEBUG, "SME OBSS: Request an OBSS scan");

	if (wpa_supplicant_trigger_scan(wpa_s, &params))
		wpa_printf(MSG_DEBUG, "SME OBSS: Failed to trigger scan");
	else
		wpa_s->sme.sched_obss_scan = 1;
	os_free(params.freqs);

	eloop_register_timeout(wpa_s->sme.obss_scan_int, 0,
			       sme_obss_scan_timeout, wpa_s, NULL);
}


void sme_sched_obss_scan(struct wpa_supplicant *wpa_s, int enable)
{
	const u8 *ie;
	struct wpa_bss *bss = wpa_s->current_bss;
	struct wpa_ssid *ssid = wpa_s->current_ssid;
	struct hostapd_hw_modes *hw_mode = NULL;
	int i;

	eloop_cancel_timeout(sme_obss_scan_timeout, wpa_s, NULL);
	wpa_s->sme.sched_obss_scan = 0;
	wpa_s->sme.ht_sec_chan = HT_SEC_CHAN_UNKNOWN;
	if (!enable)
		return;

	/*
	 * Schedule OBSS scan if driver is using station SME in wpa_supplicant
	 * or it expects OBSS scan to be performed by wpa_supplicant.
	 */
	if (!((wpa_s->drv_flags & WPA_DRIVER_FLAGS_SME) ||
	      (wpa_s->drv_flags & WPA_DRIVER_FLAGS_OBSS_SCAN)) ||
	    ssid == NULL || ssid->mode != IEEE80211_MODE_INFRA)
		return;

	if (!wpa_s->hw.modes)
		return;

	/* only HT caps in 11g mode are relevant */
	for (i = 0; i < wpa_s->hw.num_modes; i++) {
		hw_mode = &wpa_s->hw.modes[i];
		if (hw_mode->mode == HOSTAPD_MODE_IEEE80211G)
			break;
	}

	/* Driver does not support HT40 for 11g or doesn't have 11g. */
	if (i == wpa_s->hw.num_modes || !hw_mode ||
	    !(hw_mode->ht_capab & HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET))
		return;

	if (bss == NULL || bss->freq < 2400 || bss->freq > 2500)
		return; /* Not associated on 2.4 GHz band */

	/* Check whether AP supports HT40 */
	ie = wpa_bss_get_ie(wpa_s->current_bss, WLAN_EID_HT_CAP);
	if (!ie || ie[1] < 2 ||
	    !(WPA_GET_LE16(ie + 2) & HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET))
		return; /* AP does not support HT40 */

	ie = wpa_bss_get_ie(wpa_s->current_bss,
			    WLAN_EID_OVERLAPPING_BSS_SCAN_PARAMS);
	if (!ie || ie[1] < 14)
		return; /* AP does not request OBSS scans */

	wpa_s->sme.obss_scan_int = WPA_GET_LE16(ie + 6);
	if (wpa_s->sme.obss_scan_int < 10) {
		wpa_printf(MSG_DEBUG, "SME: Invalid OBSS Scan Interval %u "
			   "replaced with the minimum 10 sec",
			   wpa_s->sme.obss_scan_int);
		wpa_s->sme.obss_scan_int = 10;
	}
	wpa_printf(MSG_DEBUG, "SME: OBSS Scan Interval %u sec",
		   wpa_s->sme.obss_scan_int);
	eloop_register_timeout(wpa_s->sme.obss_scan_int, 0,
			       sme_obss_scan_timeout, wpa_s, NULL);
}


#ifdef CONFIG_IEEE80211W

static const unsigned int sa_query_max_timeout = 1000;
static const unsigned int sa_query_retry_timeout = 201;

static int sme_check_sa_query_timeout(struct wpa_supplicant *wpa_s)
{
	u32 tu;
	struct os_reltime now, passed;
	os_get_reltime(&now);
	os_reltime_sub(&now, &wpa_s->sme.sa_query_start, &passed);
	tu = (passed.sec * 1000000 + passed.usec) / 1024;
	if (sa_query_max_timeout < tu) {
		wpa_dbg(wpa_s, MSG_DEBUG, "SME: SA Query timed out");
		sme_stop_sa_query(wpa_s);
		wpa_supplicant_deauthenticate(
			wpa_s, WLAN_REASON_PREV_AUTH_NOT_VALID);
		return 1;
	}

	return 0;
}


static void sme_send_sa_query_req(struct wpa_supplicant *wpa_s,
				  const u8 *trans_id)
{
	u8 req[2 + WLAN_SA_QUERY_TR_ID_LEN];
	wpa_dbg(wpa_s, MSG_DEBUG, "SME: Sending SA Query Request to "
		MACSTR, MAC2STR(wpa_s->bssid));
	wpa_hexdump(MSG_DEBUG, "SME: SA Query Transaction ID",
		    trans_id, WLAN_SA_QUERY_TR_ID_LEN);
	req[0] = WLAN_ACTION_SA_QUERY;
	req[1] = WLAN_SA_QUERY_REQUEST;
	os_memcpy(req + 2, trans_id, WLAN_SA_QUERY_TR_ID_LEN);
	if (wpa_drv_send_action(wpa_s, wpa_s->assoc_freq, 0, wpa_s->bssid,
				wpa_s->own_addr, wpa_s->bssid,
				req, sizeof(req), 0) < 0)
		wpa_msg(wpa_s, MSG_INFO, "SME: Failed to send SA Query "
			"Request");
}


static void sme_sa_query_timer(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;
	unsigned int timeout, sec, usec;
	u8 *trans_id, *nbuf;

	if (wpa_s->sme.sa_query_count > 0 &&
	    sme_check_sa_query_timeout(wpa_s))
		return;

	nbuf = os_realloc_array(wpa_s->sme.sa_query_trans_id,
				wpa_s->sme.sa_query_count + 1,
				WLAN_SA_QUERY_TR_ID_LEN);
	if (nbuf == NULL) {
		sme_stop_sa_query(wpa_s);
		return;
	}
	if (wpa_s->sme.sa_query_count == 0) {
		/* Starting a new SA Query procedure */
		os_get_reltime(&wpa_s->sme.sa_query_start);
	}
	trans_id = nbuf + wpa_s->sme.sa_query_count * WLAN_SA_QUERY_TR_ID_LEN;
	wpa_s->sme.sa_query_trans_id = nbuf;
	wpa_s->sme.sa_query_count++;

	if (os_get_random(trans_id, WLAN_SA_QUERY_TR_ID_LEN) < 0) {
		wpa_printf(MSG_DEBUG, "Could not generate SA Query ID");
		sme_stop_sa_query(wpa_s);
		return;
	}

	timeout = sa_query_retry_timeout;
	sec = ((timeout / 1000) * 1024) / 1000;
	usec = (timeout % 1000) * 1024;
	eloop_register_timeout(sec, usec, sme_sa_query_timer, wpa_s, NULL);

	wpa_dbg(wpa_s, MSG_DEBUG, "SME: Association SA Query attempt %d",
		wpa_s->sme.sa_query_count);

	sme_send_sa_query_req(wpa_s, trans_id);
}


static void sme_start_sa_query(struct wpa_supplicant *wpa_s)
{
	sme_sa_query_timer(wpa_s, NULL);
}


static void sme_stop_sa_query(struct wpa_supplicant *wpa_s)
{
	eloop_cancel_timeout(sme_sa_query_timer, wpa_s, NULL);
	os_free(wpa_s->sme.sa_query_trans_id);
	wpa_s->sme.sa_query_trans_id = NULL;
	wpa_s->sme.sa_query_count = 0;
}


void sme_event_unprot_disconnect(struct wpa_supplicant *wpa_s, const u8 *sa,
				 const u8 *da, u16 reason_code)
{
	struct wpa_ssid *ssid;
	struct os_reltime now;

	if (wpa_s->wpa_state != WPA_COMPLETED)
		return;
	ssid = wpa_s->current_ssid;
	if (wpas_get_ssid_pmf(wpa_s, ssid) == NO_MGMT_FRAME_PROTECTION)
		return;
	if (os_memcmp(sa, wpa_s->bssid, ETH_ALEN) != 0)
		return;
	if (reason_code != WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA &&
	    reason_code != WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA)
		return;
	if (wpa_s->sme.sa_query_count > 0)
		return;

	os_get_reltime(&now);
	if (wpa_s->sme.last_unprot_disconnect.sec &&
	    !os_reltime_expired(&now, &wpa_s->sme.last_unprot_disconnect, 10))
		return; /* limit SA Query procedure frequency */
	wpa_s->sme.last_unprot_disconnect = now;

	wpa_dbg(wpa_s, MSG_DEBUG, "SME: Unprotected disconnect dropped - "
		"possible AP/STA state mismatch - trigger SA Query");
	sme_start_sa_query(wpa_s);
}


void sme_sa_query_rx(struct wpa_supplicant *wpa_s, const u8 *sa,
		     const u8 *data, size_t len)
{
	int i;

	if (wpa_s->sme.sa_query_trans_id == NULL ||
	    len < 1 + WLAN_SA_QUERY_TR_ID_LEN ||
	    data[0] != WLAN_SA_QUERY_RESPONSE)
		return;
	wpa_dbg(wpa_s, MSG_DEBUG, "SME: Received SA Query response from "
		MACSTR " (trans_id %02x%02x)", MAC2STR(sa), data[1], data[2]);

	if (os_memcmp(sa, wpa_s->bssid, ETH_ALEN) != 0)
		return;

	for (i = 0; i < wpa_s->sme.sa_query_count; i++) {
		if (os_memcmp(wpa_s->sme.sa_query_trans_id +
			      i * WLAN_SA_QUERY_TR_ID_LEN,
			      data + 1, WLAN_SA_QUERY_TR_ID_LEN) == 0)
			break;
	}

	if (i >= wpa_s->sme.sa_query_count) {
		wpa_dbg(wpa_s, MSG_DEBUG, "SME: No matching SA Query "
			"transaction identifier found");
		return;
	}

	wpa_dbg(wpa_s, MSG_DEBUG, "SME: Reply to pending SA Query received "
		"from " MACSTR, MAC2STR(sa));
	sme_stop_sa_query(wpa_s);
}

#endif /* CONFIG_IEEE80211W */
