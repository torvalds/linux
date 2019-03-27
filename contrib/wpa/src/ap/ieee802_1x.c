/*
 * hostapd / IEEE 802.1X-2004 Authenticator
 * Copyright (c) 2002-2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "crypto/md5.h"
#include "crypto/crypto.h"
#include "crypto/random.h"
#include "common/ieee802_11_defs.h"
#include "radius/radius.h"
#include "radius/radius_client.h"
#include "eap_server/eap.h"
#include "eap_common/eap_wsc_common.h"
#include "eapol_auth/eapol_auth_sm.h"
#include "eapol_auth/eapol_auth_sm_i.h"
#include "p2p/p2p.h"
#include "hostapd.h"
#include "accounting.h"
#include "sta_info.h"
#include "wpa_auth.h"
#include "preauth_auth.h"
#include "pmksa_cache_auth.h"
#include "ap_config.h"
#include "ap_drv_ops.h"
#include "wps_hostapd.h"
#include "hs20.h"
/* FIX: Not really a good thing to require ieee802_11.h here.. (FILS) */
#include "ieee802_11.h"
#include "ieee802_1x.h"


#ifdef CONFIG_HS20
static void ieee802_1x_wnm_notif_send(void *eloop_ctx, void *timeout_ctx);
#endif /* CONFIG_HS20 */
static void ieee802_1x_finished(struct hostapd_data *hapd,
				struct sta_info *sta, int success,
				int remediation);


static void ieee802_1x_send(struct hostapd_data *hapd, struct sta_info *sta,
			    u8 type, const u8 *data, size_t datalen)
{
	u8 *buf;
	struct ieee802_1x_hdr *xhdr;
	size_t len;
	int encrypt = 0;

	len = sizeof(*xhdr) + datalen;
	buf = os_zalloc(len);
	if (buf == NULL) {
		wpa_printf(MSG_ERROR, "malloc() failed for "
			   "ieee802_1x_send(len=%lu)",
			   (unsigned long) len);
		return;
	}

	xhdr = (struct ieee802_1x_hdr *) buf;
	xhdr->version = hapd->conf->eapol_version;
	xhdr->type = type;
	xhdr->length = host_to_be16(datalen);

	if (datalen > 0 && data != NULL)
		os_memcpy(xhdr + 1, data, datalen);

	if (wpa_auth_pairwise_set(sta->wpa_sm))
		encrypt = 1;
#ifdef CONFIG_TESTING_OPTIONS
	if (hapd->ext_eapol_frame_io) {
		size_t hex_len = 2 * len + 1;
		char *hex = os_malloc(hex_len);

		if (hex) {
			wpa_snprintf_hex(hex, hex_len, buf, len);
			wpa_msg(hapd->msg_ctx, MSG_INFO,
				"EAPOL-TX " MACSTR " %s",
				MAC2STR(sta->addr), hex);
			os_free(hex);
		}
	} else
#endif /* CONFIG_TESTING_OPTIONS */
	if (sta->flags & WLAN_STA_PREAUTH) {
		rsn_preauth_send(hapd, sta, buf, len);
	} else {
		hostapd_drv_hapd_send_eapol(
			hapd, sta->addr, buf, len,
			encrypt, hostapd_sta_flags_to_drv(sta->flags));
	}

	os_free(buf);
}


void ieee802_1x_set_sta_authorized(struct hostapd_data *hapd,
				   struct sta_info *sta, int authorized)
{
	int res;

	if (sta->flags & WLAN_STA_PREAUTH)
		return;

	if (authorized) {
		ap_sta_set_authorized(hapd, sta, 1);
		res = hostapd_set_authorized(hapd, sta, 1);
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE8021X,
			       HOSTAPD_LEVEL_DEBUG, "authorizing port");
	} else {
		ap_sta_set_authorized(hapd, sta, 0);
		res = hostapd_set_authorized(hapd, sta, 0);
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE8021X,
			       HOSTAPD_LEVEL_DEBUG, "unauthorizing port");
	}

	if (res && errno != ENOENT) {
		wpa_printf(MSG_DEBUG, "Could not set station " MACSTR
			   " flags for kernel driver (errno=%d).",
			   MAC2STR(sta->addr), errno);
	}

	if (authorized) {
		os_get_reltime(&sta->connected_time);
		accounting_sta_start(hapd, sta);
	}
}


#ifndef CONFIG_FIPS
#ifndef CONFIG_NO_RC4

static void ieee802_1x_tx_key_one(struct hostapd_data *hapd,
				  struct sta_info *sta,
				  int idx, int broadcast,
				  u8 *key_data, size_t key_len)
{
	u8 *buf, *ekey;
	struct ieee802_1x_hdr *hdr;
	struct ieee802_1x_eapol_key *key;
	size_t len, ekey_len;
	struct eapol_state_machine *sm = sta->eapol_sm;

	if (sm == NULL)
		return;

	len = sizeof(*key) + key_len;
	buf = os_zalloc(sizeof(*hdr) + len);
	if (buf == NULL)
		return;

	hdr = (struct ieee802_1x_hdr *) buf;
	key = (struct ieee802_1x_eapol_key *) (hdr + 1);
	key->type = EAPOL_KEY_TYPE_RC4;
	WPA_PUT_BE16(key->key_length, key_len);
	wpa_get_ntp_timestamp(key->replay_counter);

	if (random_get_bytes(key->key_iv, sizeof(key->key_iv))) {
		wpa_printf(MSG_ERROR, "Could not get random numbers");
		os_free(buf);
		return;
	}

	key->key_index = idx | (broadcast ? 0 : BIT(7));
	if (hapd->conf->eapol_key_index_workaround) {
		/* According to some information, WinXP Supplicant seems to
		 * interpret bit7 as an indication whether the key is to be
		 * activated, so make it possible to enable workaround that
		 * sets this bit for all keys. */
		key->key_index |= BIT(7);
	}

	/* Key is encrypted using "Key-IV + MSK[0..31]" as the RC4-key and
	 * MSK[32..63] is used to sign the message. */
	if (sm->eap_if->eapKeyData == NULL || sm->eap_if->eapKeyDataLen < 64) {
		wpa_printf(MSG_ERROR, "No eapKeyData available for encrypting "
			   "and signing EAPOL-Key");
		os_free(buf);
		return;
	}
	os_memcpy((u8 *) (key + 1), key_data, key_len);
	ekey_len = sizeof(key->key_iv) + 32;
	ekey = os_malloc(ekey_len);
	if (ekey == NULL) {
		wpa_printf(MSG_ERROR, "Could not encrypt key");
		os_free(buf);
		return;
	}
	os_memcpy(ekey, key->key_iv, sizeof(key->key_iv));
	os_memcpy(ekey + sizeof(key->key_iv), sm->eap_if->eapKeyData, 32);
	rc4_skip(ekey, ekey_len, 0, (u8 *) (key + 1), key_len);
	os_free(ekey);

	/* This header is needed here for HMAC-MD5, but it will be regenerated
	 * in ieee802_1x_send() */
	hdr->version = hapd->conf->eapol_version;
	hdr->type = IEEE802_1X_TYPE_EAPOL_KEY;
	hdr->length = host_to_be16(len);
	hmac_md5(sm->eap_if->eapKeyData + 32, 32, buf, sizeof(*hdr) + len,
		 key->key_signature);

	wpa_printf(MSG_DEBUG, "IEEE 802.1X: Sending EAPOL-Key to " MACSTR
		   " (%s index=%d)", MAC2STR(sm->addr),
		   broadcast ? "broadcast" : "unicast", idx);
	ieee802_1x_send(hapd, sta, IEEE802_1X_TYPE_EAPOL_KEY, (u8 *) key, len);
	if (sta->eapol_sm)
		sta->eapol_sm->dot1xAuthEapolFramesTx++;
	os_free(buf);
}


static void ieee802_1x_tx_key(struct hostapd_data *hapd, struct sta_info *sta)
{
	struct eapol_authenticator *eapol = hapd->eapol_auth;
	struct eapol_state_machine *sm = sta->eapol_sm;

	if (sm == NULL || !sm->eap_if->eapKeyData)
		return;

	wpa_printf(MSG_DEBUG, "IEEE 802.1X: Sending EAPOL-Key(s) to " MACSTR,
		   MAC2STR(sta->addr));

#ifndef CONFIG_NO_VLAN
	if (sta->vlan_id > 0) {
		wpa_printf(MSG_ERROR, "Using WEP with vlans is not supported.");
		return;
	}
#endif /* CONFIG_NO_VLAN */

	if (eapol->default_wep_key) {
		ieee802_1x_tx_key_one(hapd, sta, eapol->default_wep_key_idx, 1,
				      eapol->default_wep_key,
				      hapd->conf->default_wep_key_len);
	}

	if (hapd->conf->individual_wep_key_len > 0) {
		u8 *ikey;
		ikey = os_malloc(hapd->conf->individual_wep_key_len);
		if (ikey == NULL ||
		    random_get_bytes(ikey, hapd->conf->individual_wep_key_len))
		{
			wpa_printf(MSG_ERROR, "Could not generate random "
				   "individual WEP key.");
			os_free(ikey);
			return;
		}

		wpa_hexdump_key(MSG_DEBUG, "Individual WEP key",
				ikey, hapd->conf->individual_wep_key_len);

		ieee802_1x_tx_key_one(hapd, sta, 0, 0, ikey,
				      hapd->conf->individual_wep_key_len);

		/* TODO: set encryption in TX callback, i.e., only after STA
		 * has ACKed EAPOL-Key frame */
		if (hostapd_drv_set_key(hapd->conf->iface, hapd, WPA_ALG_WEP,
					sta->addr, 0, 1, NULL, 0, ikey,
					hapd->conf->individual_wep_key_len)) {
			wpa_printf(MSG_ERROR, "Could not set individual WEP "
				   "encryption.");
		}

		os_free(ikey);
	}
}

#endif /* CONFIG_NO_RC4 */
#endif /* CONFIG_FIPS */


const char *radius_mode_txt(struct hostapd_data *hapd)
{
	switch (hapd->iface->conf->hw_mode) {
	case HOSTAPD_MODE_IEEE80211AD:
		return "802.11ad";
	case HOSTAPD_MODE_IEEE80211A:
		return "802.11a";
	case HOSTAPD_MODE_IEEE80211G:
		return "802.11g";
	case HOSTAPD_MODE_IEEE80211B:
	default:
		return "802.11b";
	}
}


int radius_sta_rate(struct hostapd_data *hapd, struct sta_info *sta)
{
	int i;
	u8 rate = 0;

	for (i = 0; i < sta->supported_rates_len; i++)
		if ((sta->supported_rates[i] & 0x7f) > rate)
			rate = sta->supported_rates[i] & 0x7f;

	return rate;
}


#ifndef CONFIG_NO_RADIUS
static void ieee802_1x_learn_identity(struct hostapd_data *hapd,
				      struct eapol_state_machine *sm,
				      const u8 *eap, size_t len)
{
	const u8 *identity;
	size_t identity_len;
	const struct eap_hdr *hdr = (const struct eap_hdr *) eap;

	if (len <= sizeof(struct eap_hdr) ||
	    (hdr->code == EAP_CODE_RESPONSE &&
	     eap[sizeof(struct eap_hdr)] != EAP_TYPE_IDENTITY) ||
	    (hdr->code == EAP_CODE_INITIATE &&
	     eap[sizeof(struct eap_hdr)] != EAP_ERP_TYPE_REAUTH) ||
	    (hdr->code != EAP_CODE_RESPONSE &&
	     hdr->code != EAP_CODE_INITIATE))
		return;

	eap_erp_update_identity(sm->eap, eap, len);
	identity = eap_get_identity(sm->eap, &identity_len);
	if (identity == NULL)
		return;

	/* Save station identity for future RADIUS packets */
	os_free(sm->identity);
	sm->identity = (u8 *) dup_binstr(identity, identity_len);
	if (sm->identity == NULL) {
		sm->identity_len = 0;
		return;
	}

	sm->identity_len = identity_len;
	hostapd_logger(hapd, sm->addr, HOSTAPD_MODULE_IEEE8021X,
		       HOSTAPD_LEVEL_DEBUG, "STA identity '%s'", sm->identity);
	sm->dot1xAuthEapolRespIdFramesRx++;
}


static int add_common_radius_sta_attr_rsn(struct hostapd_data *hapd,
					  struct hostapd_radius_attr *req_attr,
					  struct sta_info *sta,
					  struct radius_msg *msg)
{
	u32 suite;
	int ver, val;

	ver = wpa_auth_sta_wpa_version(sta->wpa_sm);
	val = wpa_auth_get_pairwise(sta->wpa_sm);
	suite = wpa_cipher_to_suite(ver, val);
	if (val != -1 &&
	    !hostapd_config_get_radius_attr(req_attr,
					    RADIUS_ATTR_WLAN_PAIRWISE_CIPHER) &&
	    !radius_msg_add_attr_int32(msg, RADIUS_ATTR_WLAN_PAIRWISE_CIPHER,
				       suite)) {
		wpa_printf(MSG_ERROR, "Could not add WLAN-Pairwise-Cipher");
		return -1;
	}

	suite = wpa_cipher_to_suite(((hapd->conf->wpa & 0x2) ||
				     hapd->conf->osen) ?
				    WPA_PROTO_RSN : WPA_PROTO_WPA,
				    hapd->conf->wpa_group);
	if (!hostapd_config_get_radius_attr(req_attr,
					    RADIUS_ATTR_WLAN_GROUP_CIPHER) &&
	    !radius_msg_add_attr_int32(msg, RADIUS_ATTR_WLAN_GROUP_CIPHER,
				       suite)) {
		wpa_printf(MSG_ERROR, "Could not add WLAN-Group-Cipher");
		return -1;
	}

	val = wpa_auth_sta_key_mgmt(sta->wpa_sm);
	suite = wpa_akm_to_suite(val);
	if (val != -1 &&
	    !hostapd_config_get_radius_attr(req_attr,
					    RADIUS_ATTR_WLAN_AKM_SUITE) &&
	    !radius_msg_add_attr_int32(msg, RADIUS_ATTR_WLAN_AKM_SUITE,
				       suite)) {
		wpa_printf(MSG_ERROR, "Could not add WLAN-AKM-Suite");
		return -1;
	}

#ifdef CONFIG_IEEE80211W
	if (hapd->conf->ieee80211w != NO_MGMT_FRAME_PROTECTION) {
		suite = wpa_cipher_to_suite(WPA_PROTO_RSN,
					    hapd->conf->group_mgmt_cipher);
		if (!hostapd_config_get_radius_attr(
			    req_attr, RADIUS_ATTR_WLAN_GROUP_MGMT_CIPHER) &&
		    !radius_msg_add_attr_int32(
			    msg, RADIUS_ATTR_WLAN_GROUP_MGMT_CIPHER, suite)) {
			wpa_printf(MSG_ERROR,
				   "Could not add WLAN-Group-Mgmt-Cipher");
			return -1;
		}
	}
#endif /* CONFIG_IEEE80211W */

	return 0;
}


static int add_common_radius_sta_attr(struct hostapd_data *hapd,
				      struct hostapd_radius_attr *req_attr,
				      struct sta_info *sta,
				      struct radius_msg *msg)
{
	char buf[128];

	if (!hostapd_config_get_radius_attr(req_attr,
					    RADIUS_ATTR_SERVICE_TYPE) &&
	    !radius_msg_add_attr_int32(msg, RADIUS_ATTR_SERVICE_TYPE,
				       RADIUS_SERVICE_TYPE_FRAMED)) {
		wpa_printf(MSG_ERROR, "Could not add Service-Type");
		return -1;
	}

	if (!hostapd_config_get_radius_attr(req_attr,
					    RADIUS_ATTR_NAS_PORT) &&
	    sta->aid > 0 &&
	    !radius_msg_add_attr_int32(msg, RADIUS_ATTR_NAS_PORT, sta->aid)) {
		wpa_printf(MSG_ERROR, "Could not add NAS-Port");
		return -1;
	}

	os_snprintf(buf, sizeof(buf), RADIUS_802_1X_ADDR_FORMAT,
		    MAC2STR(sta->addr));
	buf[sizeof(buf) - 1] = '\0';
	if (!radius_msg_add_attr(msg, RADIUS_ATTR_CALLING_STATION_ID,
				 (u8 *) buf, os_strlen(buf))) {
		wpa_printf(MSG_ERROR, "Could not add Calling-Station-Id");
		return -1;
	}

	if (sta->flags & WLAN_STA_PREAUTH) {
		os_strlcpy(buf, "IEEE 802.11i Pre-Authentication",
			   sizeof(buf));
	} else {
		os_snprintf(buf, sizeof(buf), "CONNECT %d%sMbps %s",
			    radius_sta_rate(hapd, sta) / 2,
			    (radius_sta_rate(hapd, sta) & 1) ? ".5" : "",
			    radius_mode_txt(hapd));
		buf[sizeof(buf) - 1] = '\0';
	}
	if (!hostapd_config_get_radius_attr(req_attr,
					    RADIUS_ATTR_CONNECT_INFO) &&
	    !radius_msg_add_attr(msg, RADIUS_ATTR_CONNECT_INFO,
				 (u8 *) buf, os_strlen(buf))) {
		wpa_printf(MSG_ERROR, "Could not add Connect-Info");
		return -1;
	}

	if (sta->acct_session_id) {
		os_snprintf(buf, sizeof(buf), "%016llX",
			    (unsigned long long) sta->acct_session_id);
		if (!radius_msg_add_attr(msg, RADIUS_ATTR_ACCT_SESSION_ID,
					 (u8 *) buf, os_strlen(buf))) {
			wpa_printf(MSG_ERROR, "Could not add Acct-Session-Id");
			return -1;
		}
	}

	if ((hapd->conf->wpa & 2) &&
	    !hapd->conf->disable_pmksa_caching &&
	    sta->eapol_sm && sta->eapol_sm->acct_multi_session_id) {
		os_snprintf(buf, sizeof(buf), "%016llX",
			    (unsigned long long)
			    sta->eapol_sm->acct_multi_session_id);
		if (!radius_msg_add_attr(
			    msg, RADIUS_ATTR_ACCT_MULTI_SESSION_ID,
			    (u8 *) buf, os_strlen(buf))) {
			wpa_printf(MSG_INFO,
				   "Could not add Acct-Multi-Session-Id");
			return -1;
		}
	}

#ifdef CONFIG_IEEE80211R_AP
	if (hapd->conf->wpa && wpa_key_mgmt_ft(hapd->conf->wpa_key_mgmt) &&
	    sta->wpa_sm &&
	    (wpa_key_mgmt_ft(wpa_auth_sta_key_mgmt(sta->wpa_sm)) ||
	     sta->auth_alg == WLAN_AUTH_FT) &&
	    !hostapd_config_get_radius_attr(req_attr,
					    RADIUS_ATTR_MOBILITY_DOMAIN_ID) &&
	    !radius_msg_add_attr_int32(msg, RADIUS_ATTR_MOBILITY_DOMAIN_ID,
				       WPA_GET_BE16(
					       hapd->conf->mobility_domain))) {
		wpa_printf(MSG_ERROR, "Could not add Mobility-Domain-Id");
		return -1;
	}
#endif /* CONFIG_IEEE80211R_AP */

	if ((hapd->conf->wpa || hapd->conf->osen) && sta->wpa_sm &&
	    add_common_radius_sta_attr_rsn(hapd, req_attr, sta, msg) < 0)
		return -1;

	return 0;
}


int add_common_radius_attr(struct hostapd_data *hapd,
			   struct hostapd_radius_attr *req_attr,
			   struct sta_info *sta,
			   struct radius_msg *msg)
{
	char buf[128];
	struct hostapd_radius_attr *attr;
	int len;

	if (!hostapd_config_get_radius_attr(req_attr,
					    RADIUS_ATTR_NAS_IP_ADDRESS) &&
	    hapd->conf->own_ip_addr.af == AF_INET &&
	    !radius_msg_add_attr(msg, RADIUS_ATTR_NAS_IP_ADDRESS,
				 (u8 *) &hapd->conf->own_ip_addr.u.v4, 4)) {
		wpa_printf(MSG_ERROR, "Could not add NAS-IP-Address");
		return -1;
	}

#ifdef CONFIG_IPV6
	if (!hostapd_config_get_radius_attr(req_attr,
					    RADIUS_ATTR_NAS_IPV6_ADDRESS) &&
	    hapd->conf->own_ip_addr.af == AF_INET6 &&
	    !radius_msg_add_attr(msg, RADIUS_ATTR_NAS_IPV6_ADDRESS,
				 (u8 *) &hapd->conf->own_ip_addr.u.v6, 16)) {
		wpa_printf(MSG_ERROR, "Could not add NAS-IPv6-Address");
		return -1;
	}
#endif /* CONFIG_IPV6 */

	if (!hostapd_config_get_radius_attr(req_attr,
					    RADIUS_ATTR_NAS_IDENTIFIER) &&
	    hapd->conf->nas_identifier &&
	    !radius_msg_add_attr(msg, RADIUS_ATTR_NAS_IDENTIFIER,
				 (u8 *) hapd->conf->nas_identifier,
				 os_strlen(hapd->conf->nas_identifier))) {
		wpa_printf(MSG_ERROR, "Could not add NAS-Identifier");
		return -1;
	}

	len = os_snprintf(buf, sizeof(buf), RADIUS_802_1X_ADDR_FORMAT ":",
			  MAC2STR(hapd->own_addr));
	os_memcpy(&buf[len], hapd->conf->ssid.ssid,
		  hapd->conf->ssid.ssid_len);
	len += hapd->conf->ssid.ssid_len;
	if (!hostapd_config_get_radius_attr(req_attr,
					    RADIUS_ATTR_CALLED_STATION_ID) &&
	    !radius_msg_add_attr(msg, RADIUS_ATTR_CALLED_STATION_ID,
				 (u8 *) buf, len)) {
		wpa_printf(MSG_ERROR, "Could not add Called-Station-Id");
		return -1;
	}

	if (!hostapd_config_get_radius_attr(req_attr,
					    RADIUS_ATTR_NAS_PORT_TYPE) &&
	    !radius_msg_add_attr_int32(msg, RADIUS_ATTR_NAS_PORT_TYPE,
				       RADIUS_NAS_PORT_TYPE_IEEE_802_11)) {
		wpa_printf(MSG_ERROR, "Could not add NAS-Port-Type");
		return -1;
	}

#ifdef CONFIG_INTERWORKING
	if (hapd->conf->interworking &&
	    !is_zero_ether_addr(hapd->conf->hessid)) {
		os_snprintf(buf, sizeof(buf), RADIUS_802_1X_ADDR_FORMAT,
			    MAC2STR(hapd->conf->hessid));
		buf[sizeof(buf) - 1] = '\0';
		if (!hostapd_config_get_radius_attr(req_attr,
						    RADIUS_ATTR_WLAN_HESSID) &&
		    !radius_msg_add_attr(msg, RADIUS_ATTR_WLAN_HESSID,
					 (u8 *) buf, os_strlen(buf))) {
			wpa_printf(MSG_ERROR, "Could not add WLAN-HESSID");
			return -1;
		}
	}
#endif /* CONFIG_INTERWORKING */

	if (sta && add_common_radius_sta_attr(hapd, req_attr, sta, msg) < 0)
		return -1;

	for (attr = req_attr; attr; attr = attr->next) {
		if (!radius_msg_add_attr(msg, attr->type,
					 wpabuf_head(attr->val),
					 wpabuf_len(attr->val))) {
			wpa_printf(MSG_ERROR, "Could not add RADIUS "
				   "attribute");
			return -1;
		}
	}

	return 0;
}


void ieee802_1x_encapsulate_radius(struct hostapd_data *hapd,
				   struct sta_info *sta,
				   const u8 *eap, size_t len)
{
	struct radius_msg *msg;
	struct eapol_state_machine *sm = sta->eapol_sm;

	if (sm == NULL)
		return;

	ieee802_1x_learn_identity(hapd, sm, eap, len);

	wpa_printf(MSG_DEBUG, "Encapsulating EAP message into a RADIUS "
		   "packet");

	sm->radius_identifier = radius_client_get_id(hapd->radius);
	msg = radius_msg_new(RADIUS_CODE_ACCESS_REQUEST,
			     sm->radius_identifier);
	if (msg == NULL) {
		wpa_printf(MSG_INFO, "Could not create new RADIUS packet");
		return;
	}

	if (radius_msg_make_authenticator(msg) < 0) {
		wpa_printf(MSG_INFO, "Could not make Request Authenticator");
		goto fail;
	}

	if (sm->identity &&
	    !radius_msg_add_attr(msg, RADIUS_ATTR_USER_NAME,
				 sm->identity, sm->identity_len)) {
		wpa_printf(MSG_INFO, "Could not add User-Name");
		goto fail;
	}

	if (add_common_radius_attr(hapd, hapd->conf->radius_auth_req_attr, sta,
				   msg) < 0)
		goto fail;

	/* TODO: should probably check MTU from driver config; 2304 is max for
	 * IEEE 802.11, but use 1400 to avoid problems with too large packets
	 */
	if (!hostapd_config_get_radius_attr(hapd->conf->radius_auth_req_attr,
					    RADIUS_ATTR_FRAMED_MTU) &&
	    !radius_msg_add_attr_int32(msg, RADIUS_ATTR_FRAMED_MTU, 1400)) {
		wpa_printf(MSG_INFO, "Could not add Framed-MTU");
		goto fail;
	}

	if (!radius_msg_add_eap(msg, eap, len)) {
		wpa_printf(MSG_INFO, "Could not add EAP-Message");
		goto fail;
	}

	/* State attribute must be copied if and only if this packet is
	 * Access-Request reply to the previous Access-Challenge */
	if (sm->last_recv_radius &&
	    radius_msg_get_hdr(sm->last_recv_radius)->code ==
	    RADIUS_CODE_ACCESS_CHALLENGE) {
		int res = radius_msg_copy_attr(msg, sm->last_recv_radius,
					       RADIUS_ATTR_STATE);
		if (res < 0) {
			wpa_printf(MSG_INFO, "Could not copy State attribute from previous Access-Challenge");
			goto fail;
		}
		if (res > 0) {
			wpa_printf(MSG_DEBUG, "Copied RADIUS State Attribute");
		}
	}

	if (hapd->conf->radius_request_cui) {
		const u8 *cui;
		size_t cui_len;
		/* Add previously learned CUI or nul CUI to request CUI */
		if (sm->radius_cui) {
			cui = wpabuf_head(sm->radius_cui);
			cui_len = wpabuf_len(sm->radius_cui);
		} else {
			cui = (const u8 *) "\0";
			cui_len = 1;
		}
		if (!radius_msg_add_attr(msg,
					 RADIUS_ATTR_CHARGEABLE_USER_IDENTITY,
					 cui, cui_len)) {
			wpa_printf(MSG_ERROR, "Could not add CUI");
			goto fail;
		}
	}

#ifdef CONFIG_HS20
	if (hapd->conf->hs20) {
		u8 ver = 1; /* Release 2 */
		if (HS20_VERSION > 0x10)
			ver = 2; /* Release 3 */
		if (!radius_msg_add_wfa(
			    msg, RADIUS_VENDOR_ATTR_WFA_HS20_AP_VERSION,
			    &ver, 1)) {
			wpa_printf(MSG_ERROR, "Could not add HS 2.0 AP "
				   "version");
			goto fail;
		}

		if (sta->hs20_ie && wpabuf_len(sta->hs20_ie) > 0) {
			const u8 *pos;
			u8 buf[3];
			u16 id;
			pos = wpabuf_head_u8(sta->hs20_ie);
			buf[0] = (*pos) >> 4;
			if (((*pos) & HS20_PPS_MO_ID_PRESENT) &&
			    wpabuf_len(sta->hs20_ie) >= 3)
				id = WPA_GET_LE16(pos + 1);
			else
				id = 0;
			WPA_PUT_BE16(buf + 1, id);
			if (!radius_msg_add_wfa(
				    msg,
				    RADIUS_VENDOR_ATTR_WFA_HS20_STA_VERSION,
				    buf, sizeof(buf))) {
				wpa_printf(MSG_ERROR, "Could not add HS 2.0 "
					   "STA version");
				goto fail;
			}
		}

		if (sta->roaming_consortium &&
		    !radius_msg_add_wfa(
			    msg, RADIUS_VENDOR_ATTR_WFA_HS20_ROAMING_CONSORTIUM,
			    wpabuf_head(sta->roaming_consortium),
			    wpabuf_len(sta->roaming_consortium))) {
			wpa_printf(MSG_ERROR,
				   "Could not add HS 2.0 Roaming Consortium");
			goto fail;
		}

		if (hapd->conf->t_c_filename) {
			be32 timestamp;

			if (!radius_msg_add_wfa(
				    msg,
				    RADIUS_VENDOR_ATTR_WFA_HS20_T_C_FILENAME,
				    (const u8 *) hapd->conf->t_c_filename,
				    os_strlen(hapd->conf->t_c_filename))) {
				wpa_printf(MSG_ERROR,
					   "Could not add HS 2.0 T&C Filename");
				goto fail;
			}

			timestamp = host_to_be32(hapd->conf->t_c_timestamp);
			if (!radius_msg_add_wfa(
				    msg,
				    RADIUS_VENDOR_ATTR_WFA_HS20_TIMESTAMP,
				    (const u8 *) &timestamp,
				    sizeof(timestamp))) {
				wpa_printf(MSG_ERROR,
					   "Could not add HS 2.0 Timestamp");
				goto fail;
			}
		}
	}
#endif /* CONFIG_HS20 */

	if (radius_client_send(hapd->radius, msg, RADIUS_AUTH, sta->addr) < 0)
		goto fail;

	return;

 fail:
	radius_msg_free(msg);
}
#endif /* CONFIG_NO_RADIUS */


static void handle_eap_response(struct hostapd_data *hapd,
				struct sta_info *sta, struct eap_hdr *eap,
				size_t len)
{
	u8 type, *data;
	struct eapol_state_machine *sm = sta->eapol_sm;
	if (sm == NULL)
		return;

	data = (u8 *) (eap + 1);

	if (len < sizeof(*eap) + 1) {
		wpa_printf(MSG_INFO, "handle_eap_response: too short response data");
		return;
	}

	sm->eap_type_supp = type = data[0];

	hostapd_logger(hapd, sm->addr, HOSTAPD_MODULE_IEEE8021X,
		       HOSTAPD_LEVEL_DEBUG, "received EAP packet (code=%d "
		       "id=%d len=%d) from STA: EAP Response-%s (%d)",
		       eap->code, eap->identifier, be_to_host16(eap->length),
		       eap_server_get_name(0, type), type);

	sm->dot1xAuthEapolRespFramesRx++;

	wpabuf_free(sm->eap_if->eapRespData);
	sm->eap_if->eapRespData = wpabuf_alloc_copy(eap, len);
	sm->eapolEap = TRUE;
}


static void handle_eap_initiate(struct hostapd_data *hapd,
				struct sta_info *sta, struct eap_hdr *eap,
				size_t len)
{
#ifdef CONFIG_ERP
	u8 type, *data;
	struct eapol_state_machine *sm = sta->eapol_sm;

	if (sm == NULL)
		return;

	if (len < sizeof(*eap) + 1) {
		wpa_printf(MSG_INFO,
			   "handle_eap_initiate: too short response data");
		return;
	}

	data = (u8 *) (eap + 1);
	type = data[0];

	hostapd_logger(hapd, sm->addr, HOSTAPD_MODULE_IEEE8021X,
		       HOSTAPD_LEVEL_DEBUG, "received EAP packet (code=%d "
		       "id=%d len=%d) from STA: EAP Initiate type %u",
		       eap->code, eap->identifier, be_to_host16(eap->length),
		       type);

	wpabuf_free(sm->eap_if->eapRespData);
	sm->eap_if->eapRespData = wpabuf_alloc_copy(eap, len);
	sm->eapolEap = TRUE;
#endif /* CONFIG_ERP */
}


/* Process incoming EAP packet from Supplicant */
static void handle_eap(struct hostapd_data *hapd, struct sta_info *sta,
		       u8 *buf, size_t len)
{
	struct eap_hdr *eap;
	u16 eap_len;

	if (len < sizeof(*eap)) {
		wpa_printf(MSG_INFO, "   too short EAP packet");
		return;
	}

	eap = (struct eap_hdr *) buf;

	eap_len = be_to_host16(eap->length);
	wpa_printf(MSG_DEBUG, "EAP: code=%d identifier=%d length=%d",
		   eap->code, eap->identifier, eap_len);
	if (eap_len < sizeof(*eap)) {
		wpa_printf(MSG_DEBUG, "   Invalid EAP length");
		return;
	} else if (eap_len > len) {
		wpa_printf(MSG_DEBUG, "   Too short frame to contain this EAP "
			   "packet");
		return;
	} else if (eap_len < len) {
		wpa_printf(MSG_DEBUG, "   Ignoring %lu extra bytes after EAP "
			   "packet", (unsigned long) len - eap_len);
	}

	switch (eap->code) {
	case EAP_CODE_REQUEST:
		wpa_printf(MSG_DEBUG, " (request)");
		return;
	case EAP_CODE_RESPONSE:
		wpa_printf(MSG_DEBUG, " (response)");
		handle_eap_response(hapd, sta, eap, eap_len);
		break;
	case EAP_CODE_SUCCESS:
		wpa_printf(MSG_DEBUG, " (success)");
		return;
	case EAP_CODE_FAILURE:
		wpa_printf(MSG_DEBUG, " (failure)");
		return;
	case EAP_CODE_INITIATE:
		wpa_printf(MSG_DEBUG, " (initiate)");
		handle_eap_initiate(hapd, sta, eap, eap_len);
		break;
	case EAP_CODE_FINISH:
		wpa_printf(MSG_DEBUG, " (finish)");
		break;
	default:
		wpa_printf(MSG_DEBUG, " (unknown code)");
		return;
	}
}


struct eapol_state_machine *
ieee802_1x_alloc_eapol_sm(struct hostapd_data *hapd, struct sta_info *sta)
{
	int flags = 0;
	if (sta->flags & WLAN_STA_PREAUTH)
		flags |= EAPOL_SM_PREAUTH;
	if (sta->wpa_sm) {
		flags |= EAPOL_SM_USES_WPA;
		if (wpa_auth_sta_get_pmksa(sta->wpa_sm))
			flags |= EAPOL_SM_FROM_PMKSA_CACHE;
	}
	return eapol_auth_alloc(hapd->eapol_auth, sta->addr, flags,
				sta->wps_ie, sta->p2p_ie, sta,
				sta->identity, sta->radius_cui);
}


static void ieee802_1x_save_eapol(struct sta_info *sta, const u8 *buf,
				  size_t len)
{
	if (sta->pending_eapol_rx) {
		wpabuf_free(sta->pending_eapol_rx->buf);
	} else {
		sta->pending_eapol_rx =
			os_malloc(sizeof(*sta->pending_eapol_rx));
		if (!sta->pending_eapol_rx)
			return;
	}

	sta->pending_eapol_rx->buf = wpabuf_alloc_copy(buf, len);
	if (!sta->pending_eapol_rx->buf) {
		os_free(sta->pending_eapol_rx);
		sta->pending_eapol_rx = NULL;
		return;
	}

	os_get_reltime(&sta->pending_eapol_rx->rx_time);
}


/**
 * ieee802_1x_receive - Process the EAPOL frames from the Supplicant
 * @hapd: hostapd BSS data
 * @sa: Source address (sender of the EAPOL frame)
 * @buf: EAPOL frame
 * @len: Length of buf in octets
 *
 * This function is called for each incoming EAPOL frame from the interface
 */
void ieee802_1x_receive(struct hostapd_data *hapd, const u8 *sa, const u8 *buf,
			size_t len)
{
	struct sta_info *sta;
	struct ieee802_1x_hdr *hdr;
	struct ieee802_1x_eapol_key *key;
	u16 datalen;
	struct rsn_pmksa_cache_entry *pmksa;
	int key_mgmt;

	if (!hapd->conf->ieee802_1x && !hapd->conf->wpa && !hapd->conf->osen &&
	    !hapd->conf->wps_state)
		return;

	wpa_printf(MSG_DEBUG, "IEEE 802.1X: %lu bytes from " MACSTR,
		   (unsigned long) len, MAC2STR(sa));
	sta = ap_get_sta(hapd, sa);
	if (!sta || (!(sta->flags & (WLAN_STA_ASSOC | WLAN_STA_PREAUTH)) &&
		     !(hapd->iface->drv_flags & WPA_DRIVER_FLAGS_WIRED))) {
		wpa_printf(MSG_DEBUG, "IEEE 802.1X data frame from not "
			   "associated/Pre-authenticating STA");

		if (sta && (sta->flags & WLAN_STA_AUTH)) {
			wpa_printf(MSG_DEBUG, "Saving EAPOL frame from " MACSTR
				   " for later use", MAC2STR(sta->addr));
			ieee802_1x_save_eapol(sta, buf, len);
		}

		return;
	}

	if (len < sizeof(*hdr)) {
		wpa_printf(MSG_INFO, "   too short IEEE 802.1X packet");
		return;
	}

	hdr = (struct ieee802_1x_hdr *) buf;
	datalen = be_to_host16(hdr->length);
	wpa_printf(MSG_DEBUG, "   IEEE 802.1X: version=%d type=%d length=%d",
		   hdr->version, hdr->type, datalen);

	if (len - sizeof(*hdr) < datalen) {
		wpa_printf(MSG_INFO, "   frame too short for this IEEE 802.1X packet");
		if (sta->eapol_sm)
			sta->eapol_sm->dot1xAuthEapLengthErrorFramesRx++;
		return;
	}
	if (len - sizeof(*hdr) > datalen) {
		wpa_printf(MSG_DEBUG, "   ignoring %lu extra octets after "
			   "IEEE 802.1X packet",
			   (unsigned long) len - sizeof(*hdr) - datalen);
	}

	if (sta->eapol_sm) {
		sta->eapol_sm->dot1xAuthLastEapolFrameVersion = hdr->version;
		sta->eapol_sm->dot1xAuthEapolFramesRx++;
	}

	key = (struct ieee802_1x_eapol_key *) (hdr + 1);
	if (datalen >= sizeof(struct ieee802_1x_eapol_key) &&
	    hdr->type == IEEE802_1X_TYPE_EAPOL_KEY &&
	    (key->type == EAPOL_KEY_TYPE_WPA ||
	     key->type == EAPOL_KEY_TYPE_RSN)) {
		wpa_receive(hapd->wpa_auth, sta->wpa_sm, (u8 *) hdr,
			    sizeof(*hdr) + datalen);
		return;
	}

	if (!hapd->conf->ieee802_1x && !hapd->conf->osen &&
	    !(sta->flags & (WLAN_STA_WPS | WLAN_STA_MAYBE_WPS))) {
		wpa_printf(MSG_DEBUG, "IEEE 802.1X: Ignore EAPOL message - "
			   "802.1X not enabled and WPS not used");
		return;
	}

	key_mgmt = wpa_auth_sta_key_mgmt(sta->wpa_sm);
	if (key_mgmt != -1 &&
	    (wpa_key_mgmt_wpa_psk(key_mgmt) || key_mgmt == WPA_KEY_MGMT_OWE ||
	     key_mgmt == WPA_KEY_MGMT_DPP)) {
		wpa_printf(MSG_DEBUG, "IEEE 802.1X: Ignore EAPOL message - "
			   "STA is using PSK");
		return;
	}

	if (!sta->eapol_sm) {
		sta->eapol_sm = ieee802_1x_alloc_eapol_sm(hapd, sta);
		if (!sta->eapol_sm)
			return;

#ifdef CONFIG_WPS
		if (!hapd->conf->ieee802_1x && hapd->conf->wps_state) {
			u32 wflags = sta->flags & (WLAN_STA_WPS |
						   WLAN_STA_WPS2 |
						   WLAN_STA_MAYBE_WPS);
			if (wflags == WLAN_STA_MAYBE_WPS ||
			    wflags == (WLAN_STA_WPS | WLAN_STA_MAYBE_WPS)) {
				/*
				 * Delay EAPOL frame transmission until a
				 * possible WPS STA initiates the handshake
				 * with EAPOL-Start. Only allow the wait to be
				 * skipped if the STA is known to support WPS
				 * 2.0.
				 */
				wpa_printf(MSG_DEBUG, "WPS: Do not start "
					   "EAPOL until EAPOL-Start is "
					   "received");
				sta->eapol_sm->flags |= EAPOL_SM_WAIT_START;
			}
		}
#endif /* CONFIG_WPS */

		sta->eapol_sm->eap_if->portEnabled = TRUE;
	}

	/* since we support version 1, we can ignore version field and proceed
	 * as specified in version 1 standard [IEEE Std 802.1X-2001, 7.5.5] */
	/* TODO: actually, we are not version 1 anymore.. However, Version 2
	 * does not change frame contents, so should be ok to process frames
	 * more or less identically. Some changes might be needed for
	 * verification of fields. */

	switch (hdr->type) {
	case IEEE802_1X_TYPE_EAP_PACKET:
		handle_eap(hapd, sta, (u8 *) (hdr + 1), datalen);
		break;

	case IEEE802_1X_TYPE_EAPOL_START:
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE8021X,
			       HOSTAPD_LEVEL_DEBUG, "received EAPOL-Start "
			       "from STA");
		sta->eapol_sm->flags &= ~EAPOL_SM_WAIT_START;
		pmksa = wpa_auth_sta_get_pmksa(sta->wpa_sm);
		if (pmksa) {
			hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_WPA,
				       HOSTAPD_LEVEL_DEBUG, "cached PMKSA "
				       "available - ignore it since "
				       "STA sent EAPOL-Start");
			wpa_auth_sta_clear_pmksa(sta->wpa_sm, pmksa);
		}
		sta->eapol_sm->eapolStart = TRUE;
		sta->eapol_sm->dot1xAuthEapolStartFramesRx++;
		eap_server_clear_identity(sta->eapol_sm->eap);
		wpa_auth_sm_event(sta->wpa_sm, WPA_REAUTH_EAPOL);
		break;

	case IEEE802_1X_TYPE_EAPOL_LOGOFF:
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE8021X,
			       HOSTAPD_LEVEL_DEBUG, "received EAPOL-Logoff "
			       "from STA");
		sta->acct_terminate_cause =
			RADIUS_ACCT_TERMINATE_CAUSE_USER_REQUEST;
		accounting_sta_stop(hapd, sta);
		sta->eapol_sm->eapolLogoff = TRUE;
		sta->eapol_sm->dot1xAuthEapolLogoffFramesRx++;
		eap_server_clear_identity(sta->eapol_sm->eap);
		break;

	case IEEE802_1X_TYPE_EAPOL_KEY:
		wpa_printf(MSG_DEBUG, "   EAPOL-Key");
		if (!ap_sta_is_authorized(sta)) {
			wpa_printf(MSG_DEBUG, "   Dropped key data from "
				   "unauthorized Supplicant");
			break;
		}
		break;

	case IEEE802_1X_TYPE_EAPOL_ENCAPSULATED_ASF_ALERT:
		wpa_printf(MSG_DEBUG, "   EAPOL-Encapsulated-ASF-Alert");
		/* TODO: implement support for this; show data */
		break;

	default:
		wpa_printf(MSG_DEBUG, "   unknown IEEE 802.1X packet type");
		sta->eapol_sm->dot1xAuthInvalidEapolFramesRx++;
		break;
	}

	eapol_auth_step(sta->eapol_sm);
}


/**
 * ieee802_1x_new_station - Start IEEE 802.1X authentication
 * @hapd: hostapd BSS data
 * @sta: The station
 *
 * This function is called to start IEEE 802.1X authentication when a new
 * station completes IEEE 802.11 association.
 */
void ieee802_1x_new_station(struct hostapd_data *hapd, struct sta_info *sta)
{
	struct rsn_pmksa_cache_entry *pmksa;
	int reassoc = 1;
	int force_1x = 0;
	int key_mgmt;

#ifdef CONFIG_WPS
	if (hapd->conf->wps_state &&
	    ((hapd->conf->wpa && (sta->flags & WLAN_STA_MAYBE_WPS)) ||
	     (sta->flags & WLAN_STA_WPS))) {
		/*
		 * Need to enable IEEE 802.1X/EAPOL state machines for possible
		 * WPS handshake even if IEEE 802.1X/EAPOL is not used for
		 * authentication in this BSS.
		 */
		force_1x = 1;
	}
#endif /* CONFIG_WPS */

	if (!force_1x && !hapd->conf->ieee802_1x && !hapd->conf->osen) {
		wpa_printf(MSG_DEBUG, "IEEE 802.1X: Ignore STA - "
			   "802.1X not enabled or forced for WPS");
		/*
		 * Clear any possible EAPOL authenticator state to support
		 * reassociation change from WPS to PSK.
		 */
		ieee802_1x_free_station(hapd, sta);
		return;
	}

	key_mgmt = wpa_auth_sta_key_mgmt(sta->wpa_sm);
	if (key_mgmt != -1 &&
	    (wpa_key_mgmt_wpa_psk(key_mgmt) || key_mgmt == WPA_KEY_MGMT_OWE ||
	     key_mgmt == WPA_KEY_MGMT_DPP)) {
		wpa_printf(MSG_DEBUG, "IEEE 802.1X: Ignore STA - using PSK");
		/*
		 * Clear any possible EAPOL authenticator state to support
		 * reassociation change from WPA-EAP to PSK.
		 */
		ieee802_1x_free_station(hapd, sta);
		return;
	}

	if (sta->eapol_sm == NULL) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE8021X,
			       HOSTAPD_LEVEL_DEBUG, "start authentication");
		sta->eapol_sm = ieee802_1x_alloc_eapol_sm(hapd, sta);
		if (sta->eapol_sm == NULL) {
			hostapd_logger(hapd, sta->addr,
				       HOSTAPD_MODULE_IEEE8021X,
				       HOSTAPD_LEVEL_INFO,
				       "failed to allocate state machine");
			return;
		}
		reassoc = 0;
	}

#ifdef CONFIG_WPS
	sta->eapol_sm->flags &= ~EAPOL_SM_WAIT_START;
	if (!hapd->conf->ieee802_1x && hapd->conf->wps_state &&
	    !(sta->flags & WLAN_STA_WPS2)) {
		/*
		 * Delay EAPOL frame transmission until a possible WPS STA
		 * initiates the handshake with EAPOL-Start. Only allow the
		 * wait to be skipped if the STA is known to support WPS 2.0.
		 */
		wpa_printf(MSG_DEBUG, "WPS: Do not start EAPOL until "
			   "EAPOL-Start is received");
		sta->eapol_sm->flags |= EAPOL_SM_WAIT_START;
	}
#endif /* CONFIG_WPS */

	sta->eapol_sm->eap_if->portEnabled = TRUE;

#ifdef CONFIG_IEEE80211R_AP
	if (sta->auth_alg == WLAN_AUTH_FT) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE8021X,
			       HOSTAPD_LEVEL_DEBUG,
			       "PMK from FT - skip IEEE 802.1X/EAP");
		/* Setup EAPOL state machines to already authenticated state
		 * because of existing FT information from R0KH. */
		sta->eapol_sm->keyRun = TRUE;
		sta->eapol_sm->eap_if->eapKeyAvailable = TRUE;
		sta->eapol_sm->auth_pae_state = AUTH_PAE_AUTHENTICATING;
		sta->eapol_sm->be_auth_state = BE_AUTH_SUCCESS;
		sta->eapol_sm->authSuccess = TRUE;
		sta->eapol_sm->authFail = FALSE;
		sta->eapol_sm->portValid = TRUE;
		if (sta->eapol_sm->eap)
			eap_sm_notify_cached(sta->eapol_sm->eap);
		ap_sta_bind_vlan(hapd, sta);
		return;
	}
#endif /* CONFIG_IEEE80211R_AP */

#ifdef CONFIG_FILS
	if (sta->auth_alg == WLAN_AUTH_FILS_SK ||
	    sta->auth_alg == WLAN_AUTH_FILS_SK_PFS ||
	    sta->auth_alg == WLAN_AUTH_FILS_PK) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE8021X,
			       HOSTAPD_LEVEL_DEBUG,
			       "PMK from FILS - skip IEEE 802.1X/EAP");
		/* Setup EAPOL state machines to already authenticated state
		 * because of existing FILS information. */
		sta->eapol_sm->keyRun = TRUE;
		sta->eapol_sm->eap_if->eapKeyAvailable = TRUE;
		sta->eapol_sm->auth_pae_state = AUTH_PAE_AUTHENTICATING;
		sta->eapol_sm->be_auth_state = BE_AUTH_SUCCESS;
		sta->eapol_sm->authSuccess = TRUE;
		sta->eapol_sm->authFail = FALSE;
		sta->eapol_sm->portValid = TRUE;
		if (sta->eapol_sm->eap)
			eap_sm_notify_cached(sta->eapol_sm->eap);
		return;
	}
#endif /* CONFIG_FILS */

	pmksa = wpa_auth_sta_get_pmksa(sta->wpa_sm);
	if (pmksa) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE8021X,
			       HOSTAPD_LEVEL_DEBUG,
			       "PMK from PMKSA cache - skip IEEE 802.1X/EAP");
		/* Setup EAPOL state machines to already authenticated state
		 * because of existing PMKSA information in the cache. */
		sta->eapol_sm->keyRun = TRUE;
		sta->eapol_sm->eap_if->eapKeyAvailable = TRUE;
		sta->eapol_sm->auth_pae_state = AUTH_PAE_AUTHENTICATING;
		sta->eapol_sm->be_auth_state = BE_AUTH_SUCCESS;
		sta->eapol_sm->authSuccess = TRUE;
		sta->eapol_sm->authFail = FALSE;
		if (sta->eapol_sm->eap)
			eap_sm_notify_cached(sta->eapol_sm->eap);
		pmksa_cache_to_eapol_data(hapd, pmksa, sta->eapol_sm);
		ap_sta_bind_vlan(hapd, sta);
	} else {
		if (reassoc) {
			/*
			 * Force EAPOL state machines to start
			 * re-authentication without having to wait for the
			 * Supplicant to send EAPOL-Start.
			 */
			sta->eapol_sm->reAuthenticate = TRUE;
		}
		eapol_auth_step(sta->eapol_sm);
	}
}


void ieee802_1x_free_station(struct hostapd_data *hapd, struct sta_info *sta)
{
	struct eapol_state_machine *sm = sta->eapol_sm;

#ifdef CONFIG_HS20
	eloop_cancel_timeout(ieee802_1x_wnm_notif_send, hapd, sta);
#endif /* CONFIG_HS20 */

	if (sta->pending_eapol_rx) {
		wpabuf_free(sta->pending_eapol_rx->buf);
		os_free(sta->pending_eapol_rx);
		sta->pending_eapol_rx = NULL;
	}

	if (sm == NULL)
		return;

	sta->eapol_sm = NULL;

#ifndef CONFIG_NO_RADIUS
	radius_msg_free(sm->last_recv_radius);
	radius_free_class(&sm->radius_class);
#endif /* CONFIG_NO_RADIUS */

	eapol_auth_free(sm);
}


#ifndef CONFIG_NO_RADIUS
static void ieee802_1x_decapsulate_radius(struct hostapd_data *hapd,
					  struct sta_info *sta)
{
	struct wpabuf *eap;
	const struct eap_hdr *hdr;
	int eap_type = -1;
	char buf[64];
	struct radius_msg *msg;
	struct eapol_state_machine *sm = sta->eapol_sm;

	if (sm == NULL || sm->last_recv_radius == NULL) {
		if (sm)
			sm->eap_if->aaaEapNoReq = TRUE;
		return;
	}

	msg = sm->last_recv_radius;

	eap = radius_msg_get_eap(msg);
	if (eap == NULL) {
		/* RFC 3579, Chap. 2.6.3:
		 * RADIUS server SHOULD NOT send Access-Reject/no EAP-Message
		 * attribute */
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE8021X,
			       HOSTAPD_LEVEL_WARNING, "could not extract "
			       "EAP-Message from RADIUS message");
		sm->eap_if->aaaEapNoReq = TRUE;
		return;
	}

	if (wpabuf_len(eap) < sizeof(*hdr)) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE8021X,
			       HOSTAPD_LEVEL_WARNING, "too short EAP packet "
			       "received from authentication server");
		wpabuf_free(eap);
		sm->eap_if->aaaEapNoReq = TRUE;
		return;
	}

	if (wpabuf_len(eap) > sizeof(*hdr))
		eap_type = (wpabuf_head_u8(eap))[sizeof(*hdr)];

	hdr = wpabuf_head(eap);
	switch (hdr->code) {
	case EAP_CODE_REQUEST:
		if (eap_type >= 0)
			sm->eap_type_authsrv = eap_type;
		os_snprintf(buf, sizeof(buf), "EAP-Request-%s (%d)",
			    eap_server_get_name(0, eap_type), eap_type);
		break;
	case EAP_CODE_RESPONSE:
		os_snprintf(buf, sizeof(buf), "EAP Response-%s (%d)",
			    eap_server_get_name(0, eap_type), eap_type);
		break;
	case EAP_CODE_SUCCESS:
		os_strlcpy(buf, "EAP Success", sizeof(buf));
		break;
	case EAP_CODE_FAILURE:
		os_strlcpy(buf, "EAP Failure", sizeof(buf));
		break;
	default:
		os_strlcpy(buf, "unknown EAP code", sizeof(buf));
		break;
	}
	buf[sizeof(buf) - 1] = '\0';
	hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE8021X,
		       HOSTAPD_LEVEL_DEBUG, "decapsulated EAP packet (code=%d "
		       "id=%d len=%d) from RADIUS server: %s",
		       hdr->code, hdr->identifier, be_to_host16(hdr->length),
		       buf);
	sm->eap_if->aaaEapReq = TRUE;

	wpabuf_free(sm->eap_if->aaaEapReqData);
	sm->eap_if->aaaEapReqData = eap;
}


static void ieee802_1x_get_keys(struct hostapd_data *hapd,
				struct sta_info *sta, struct radius_msg *msg,
				struct radius_msg *req,
				const u8 *shared_secret,
				size_t shared_secret_len)
{
	struct radius_ms_mppe_keys *keys;
	struct eapol_state_machine *sm = sta->eapol_sm;
	if (sm == NULL)
		return;

	keys = radius_msg_get_ms_keys(msg, req, shared_secret,
				      shared_secret_len);

	if (keys && keys->send && keys->recv) {
		size_t len = keys->send_len + keys->recv_len;
		wpa_hexdump_key(MSG_DEBUG, "MS-MPPE-Send-Key",
				keys->send, keys->send_len);
		wpa_hexdump_key(MSG_DEBUG, "MS-MPPE-Recv-Key",
				keys->recv, keys->recv_len);

		os_free(sm->eap_if->aaaEapKeyData);
		sm->eap_if->aaaEapKeyData = os_malloc(len);
		if (sm->eap_if->aaaEapKeyData) {
			os_memcpy(sm->eap_if->aaaEapKeyData, keys->recv,
				  keys->recv_len);
			os_memcpy(sm->eap_if->aaaEapKeyData + keys->recv_len,
				  keys->send, keys->send_len);
			sm->eap_if->aaaEapKeyDataLen = len;
			sm->eap_if->aaaEapKeyAvailable = TRUE;
		}
	} else {
		wpa_printf(MSG_DEBUG,
			   "MS-MPPE: 1x_get_keys, could not get keys: %p  send: %p  recv: %p",
			   keys, keys ? keys->send : NULL,
			   keys ? keys->recv : NULL);
	}

	if (keys) {
		os_free(keys->send);
		os_free(keys->recv);
		os_free(keys);
	}
}


static void ieee802_1x_store_radius_class(struct hostapd_data *hapd,
					  struct sta_info *sta,
					  struct radius_msg *msg)
{
	u8 *attr_class;
	size_t class_len;
	struct eapol_state_machine *sm = sta->eapol_sm;
	int count, i;
	struct radius_attr_data *nclass;
	size_t nclass_count;

	if (!hapd->conf->radius->acct_server || hapd->radius == NULL ||
	    sm == NULL)
		return;

	radius_free_class(&sm->radius_class);
	count = radius_msg_count_attr(msg, RADIUS_ATTR_CLASS, 1);
	if (count <= 0)
		return;

	nclass = os_calloc(count, sizeof(struct radius_attr_data));
	if (nclass == NULL)
		return;

	nclass_count = 0;

	attr_class = NULL;
	for (i = 0; i < count; i++) {
		do {
			if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_CLASS,
						    &attr_class, &class_len,
						    attr_class) < 0) {
				i = count;
				break;
			}
		} while (class_len < 1);

		nclass[nclass_count].data = os_memdup(attr_class, class_len);
		if (nclass[nclass_count].data == NULL)
			break;

		nclass[nclass_count].len = class_len;
		nclass_count++;
	}

	sm->radius_class.attr = nclass;
	sm->radius_class.count = nclass_count;
	wpa_printf(MSG_DEBUG, "IEEE 802.1X: Stored %lu RADIUS Class "
		   "attributes for " MACSTR,
		   (unsigned long) sm->radius_class.count,
		   MAC2STR(sta->addr));
}


/* Update sta->identity based on User-Name attribute in Access-Accept */
static void ieee802_1x_update_sta_identity(struct hostapd_data *hapd,
					   struct sta_info *sta,
					   struct radius_msg *msg)
{
	u8 *buf, *identity;
	size_t len;
	struct eapol_state_machine *sm = sta->eapol_sm;

	if (sm == NULL)
		return;

	if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_USER_NAME, &buf, &len,
				    NULL) < 0)
		return;

	identity = (u8 *) dup_binstr(buf, len);
	if (identity == NULL)
		return;

	hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE8021X,
		       HOSTAPD_LEVEL_DEBUG, "old identity '%s' updated with "
		       "User-Name from Access-Accept '%s'",
		       sm->identity ? (char *) sm->identity : "N/A",
		       (char *) identity);

	os_free(sm->identity);
	sm->identity = identity;
	sm->identity_len = len;
}


/* Update CUI based on Chargeable-User-Identity attribute in Access-Accept */
static void ieee802_1x_update_sta_cui(struct hostapd_data *hapd,
				      struct sta_info *sta,
				      struct radius_msg *msg)
{
	struct eapol_state_machine *sm = sta->eapol_sm;
	struct wpabuf *cui;
	u8 *buf;
	size_t len;

	if (sm == NULL)
		return;

	if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_CHARGEABLE_USER_IDENTITY,
				    &buf, &len, NULL) < 0)
		return;

	cui = wpabuf_alloc_copy(buf, len);
	if (cui == NULL)
		return;

	wpabuf_free(sm->radius_cui);
	sm->radius_cui = cui;
}


#ifdef CONFIG_HS20

static void ieee802_1x_hs20_sub_rem(struct sta_info *sta, u8 *pos, size_t len)
{
	sta->remediation = 1;
	os_free(sta->remediation_url);
	if (len > 2) {
		sta->remediation_url = os_malloc(len);
		if (!sta->remediation_url)
			return;
		sta->remediation_method = pos[0];
		os_memcpy(sta->remediation_url, pos + 1, len - 1);
		sta->remediation_url[len - 1] = '\0';
		wpa_printf(MSG_DEBUG, "HS 2.0: Subscription remediation needed "
			   "for " MACSTR " - server method %u URL %s",
			   MAC2STR(sta->addr), sta->remediation_method,
			   sta->remediation_url);
	} else {
		sta->remediation_url = NULL;
		wpa_printf(MSG_DEBUG, "HS 2.0: Subscription remediation needed "
			   "for " MACSTR, MAC2STR(sta->addr));
	}
	/* TODO: assign the STA into remediation VLAN or add filtering */
}


static void ieee802_1x_hs20_deauth_req(struct hostapd_data *hapd,
				       struct sta_info *sta, u8 *pos,
				       size_t len)
{
	if (len < 3)
		return; /* Malformed information */
	sta->hs20_deauth_requested = 1;
	wpa_printf(MSG_DEBUG, "HS 2.0: Deauthentication request - Code %u  "
		   "Re-auth Delay %u",
		   *pos, WPA_GET_LE16(pos + 1));
	wpabuf_free(sta->hs20_deauth_req);
	sta->hs20_deauth_req = wpabuf_alloc(len + 1);
	if (sta->hs20_deauth_req) {
		wpabuf_put_data(sta->hs20_deauth_req, pos, 3);
		wpabuf_put_u8(sta->hs20_deauth_req, len - 3);
		wpabuf_put_data(sta->hs20_deauth_req, pos + 3, len - 3);
	}
	ap_sta_session_timeout(hapd, sta, hapd->conf->hs20_deauth_req_timeout);
}


static void ieee802_1x_hs20_session_info(struct hostapd_data *hapd,
					 struct sta_info *sta, u8 *pos,
					 size_t len, int session_timeout)
{
	unsigned int swt;
	int warning_time, beacon_int;

	if (len < 1)
		return; /* Malformed information */
	os_free(sta->hs20_session_info_url);
	sta->hs20_session_info_url = os_malloc(len);
	if (sta->hs20_session_info_url == NULL)
		return;
	swt = pos[0];
	os_memcpy(sta->hs20_session_info_url, pos + 1, len - 1);
	sta->hs20_session_info_url[len - 1] = '\0';
	wpa_printf(MSG_DEBUG, "HS 2.0: Session Information URL='%s' SWT=%u "
		   "(session_timeout=%d)",
		   sta->hs20_session_info_url, swt, session_timeout);
	if (session_timeout < 0) {
		wpa_printf(MSG_DEBUG, "HS 2.0: No Session-Timeout set - ignore session info URL");
		return;
	}
	if (swt == 255)
		swt = 1; /* Use one minute as the AP selected value */

	if ((unsigned int) session_timeout < swt * 60)
		warning_time = 0;
	else
		warning_time = session_timeout - swt * 60;

	beacon_int = hapd->iconf->beacon_int;
	if (beacon_int < 1)
		beacon_int = 100; /* best guess */
	sta->hs20_disassoc_timer = swt * 60 * 1000 / beacon_int * 125 / 128;
	if (sta->hs20_disassoc_timer > 65535)
		sta->hs20_disassoc_timer = 65535;

	ap_sta_session_warning_timeout(hapd, sta, warning_time);
}


static void ieee802_1x_hs20_t_c_filtering(struct hostapd_data *hapd,
					  struct sta_info *sta, u8 *pos,
					  size_t len)
{
	if (len < 4)
		return; /* Malformed information */
	wpa_printf(MSG_DEBUG,
		   "HS 2.0: Terms and Conditions filtering %02x %02x %02x %02x",
		   pos[0], pos[1], pos[2], pos[3]);
	hs20_t_c_filtering(hapd, sta, pos[0] & BIT(0));
}


static void ieee802_1x_hs20_t_c_url(struct hostapd_data *hapd,
				    struct sta_info *sta, u8 *pos, size_t len)
{
	os_free(sta->t_c_url);
	sta->t_c_url = os_malloc(len + 1);
	if (!sta->t_c_url)
		return;
	os_memcpy(sta->t_c_url, pos, len);
	sta->t_c_url[len] = '\0';
	wpa_printf(MSG_DEBUG,
		   "HS 2.0: Terms and Conditions URL %s", sta->t_c_url);
}

#endif /* CONFIG_HS20 */


static void ieee802_1x_check_hs20(struct hostapd_data *hapd,
				  struct sta_info *sta,
				  struct radius_msg *msg,
				  int session_timeout)
{
#ifdef CONFIG_HS20
	u8 *buf, *pos, *end, type, sublen;
	size_t len;

	buf = NULL;
	sta->remediation = 0;
	sta->hs20_deauth_requested = 0;

	for (;;) {
		if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_VENDOR_SPECIFIC,
					    &buf, &len, buf) < 0)
			break;
		if (len < 6)
			continue;
		pos = buf;
		end = buf + len;
		if (WPA_GET_BE32(pos) != RADIUS_VENDOR_ID_WFA)
			continue;
		pos += 4;

		type = *pos++;
		sublen = *pos++;
		if (sublen < 2)
			continue; /* invalid length */
		sublen -= 2; /* skip header */
		if (pos + sublen > end)
			continue; /* invalid WFA VSA */

		switch (type) {
		case RADIUS_VENDOR_ATTR_WFA_HS20_SUBSCR_REMEDIATION:
			ieee802_1x_hs20_sub_rem(sta, pos, sublen);
			break;
		case RADIUS_VENDOR_ATTR_WFA_HS20_DEAUTH_REQ:
			ieee802_1x_hs20_deauth_req(hapd, sta, pos, sublen);
			break;
		case RADIUS_VENDOR_ATTR_WFA_HS20_SESSION_INFO_URL:
			ieee802_1x_hs20_session_info(hapd, sta, pos, sublen,
						     session_timeout);
			break;
		case RADIUS_VENDOR_ATTR_WFA_HS20_T_C_FILTERING:
			ieee802_1x_hs20_t_c_filtering(hapd, sta, pos, sublen);
			break;
		case RADIUS_VENDOR_ATTR_WFA_HS20_T_C_URL:
			ieee802_1x_hs20_t_c_url(hapd, sta, pos, sublen);
			break;
		}
	}
#endif /* CONFIG_HS20 */
}


struct sta_id_search {
	u8 identifier;
	struct eapol_state_machine *sm;
};


static int ieee802_1x_select_radius_identifier(struct hostapd_data *hapd,
					       struct sta_info *sta,
					       void *ctx)
{
	struct sta_id_search *id_search = ctx;
	struct eapol_state_machine *sm = sta->eapol_sm;

	if (sm && sm->radius_identifier >= 0 &&
	    sm->radius_identifier == id_search->identifier) {
		id_search->sm = sm;
		return 1;
	}
	return 0;
}


static struct eapol_state_machine *
ieee802_1x_search_radius_identifier(struct hostapd_data *hapd, u8 identifier)
{
	struct sta_id_search id_search;
	id_search.identifier = identifier;
	id_search.sm = NULL;
	ap_for_each_sta(hapd, ieee802_1x_select_radius_identifier, &id_search);
	return id_search.sm;
}


/**
 * ieee802_1x_receive_auth - Process RADIUS frames from Authentication Server
 * @msg: RADIUS response message
 * @req: RADIUS request message
 * @shared_secret: RADIUS shared secret
 * @shared_secret_len: Length of shared_secret in octets
 * @data: Context data (struct hostapd_data *)
 * Returns: Processing status
 */
static RadiusRxResult
ieee802_1x_receive_auth(struct radius_msg *msg, struct radius_msg *req,
			const u8 *shared_secret, size_t shared_secret_len,
			void *data)
{
	struct hostapd_data *hapd = data;
	struct sta_info *sta;
	u32 session_timeout = 0, termination_action, acct_interim_interval;
	int session_timeout_set;
	u32 reason_code;
	struct eapol_state_machine *sm;
	int override_eapReq = 0;
	struct radius_hdr *hdr = radius_msg_get_hdr(msg);
	struct vlan_description vlan_desc;
#ifndef CONFIG_NO_VLAN
	int *untagged, *tagged, *notempty;
#endif /* CONFIG_NO_VLAN */

	os_memset(&vlan_desc, 0, sizeof(vlan_desc));

	sm = ieee802_1x_search_radius_identifier(hapd, hdr->identifier);
	if (sm == NULL) {
		wpa_printf(MSG_DEBUG, "IEEE 802.1X: Could not find matching "
			   "station for this RADIUS message");
		return RADIUS_RX_UNKNOWN;
	}
	sta = sm->sta;

	/* RFC 2869, Ch. 5.13: valid Message-Authenticator attribute MUST be
	 * present when packet contains an EAP-Message attribute */
	if (hdr->code == RADIUS_CODE_ACCESS_REJECT &&
	    radius_msg_get_attr(msg, RADIUS_ATTR_MESSAGE_AUTHENTICATOR, NULL,
				0) < 0 &&
	    radius_msg_get_attr(msg, RADIUS_ATTR_EAP_MESSAGE, NULL, 0) < 0) {
		wpa_printf(MSG_DEBUG, "Allowing RADIUS Access-Reject without "
			   "Message-Authenticator since it does not include "
			   "EAP-Message");
	} else if (radius_msg_verify(msg, shared_secret, shared_secret_len,
				     req, 1)) {
		wpa_printf(MSG_INFO, "Incoming RADIUS packet did not have correct Message-Authenticator - dropped");
		return RADIUS_RX_INVALID_AUTHENTICATOR;
	}

	if (hdr->code != RADIUS_CODE_ACCESS_ACCEPT &&
	    hdr->code != RADIUS_CODE_ACCESS_REJECT &&
	    hdr->code != RADIUS_CODE_ACCESS_CHALLENGE) {
		wpa_printf(MSG_INFO, "Unknown RADIUS message code");
		return RADIUS_RX_UNKNOWN;
	}

	sm->radius_identifier = -1;
	wpa_printf(MSG_DEBUG, "RADIUS packet matching with station " MACSTR,
		   MAC2STR(sta->addr));

	radius_msg_free(sm->last_recv_radius);
	sm->last_recv_radius = msg;

	session_timeout_set =
		!radius_msg_get_attr_int32(msg, RADIUS_ATTR_SESSION_TIMEOUT,
					   &session_timeout);
	if (radius_msg_get_attr_int32(msg, RADIUS_ATTR_TERMINATION_ACTION,
				      &termination_action))
		termination_action = RADIUS_TERMINATION_ACTION_DEFAULT;

	if (hapd->conf->acct_interim_interval == 0 &&
	    hdr->code == RADIUS_CODE_ACCESS_ACCEPT &&
	    radius_msg_get_attr_int32(msg, RADIUS_ATTR_ACCT_INTERIM_INTERVAL,
				      &acct_interim_interval) == 0) {
		if (acct_interim_interval < 60) {
			hostapd_logger(hapd, sta->addr,
				       HOSTAPD_MODULE_IEEE8021X,
				       HOSTAPD_LEVEL_INFO,
				       "ignored too small "
				       "Acct-Interim-Interval %d",
				       acct_interim_interval);
		} else
			sta->acct_interim_interval = acct_interim_interval;
	}


	switch (hdr->code) {
	case RADIUS_CODE_ACCESS_ACCEPT:
#ifndef CONFIG_NO_VLAN
		if (hapd->conf->ssid.dynamic_vlan != DYNAMIC_VLAN_DISABLED) {
			notempty = &vlan_desc.notempty;
			untagged = &vlan_desc.untagged;
			tagged = vlan_desc.tagged;
			*notempty = !!radius_msg_get_vlanid(msg, untagged,
							    MAX_NUM_TAGGED_VLAN,
							    tagged);
		}

		if (vlan_desc.notempty &&
		    !hostapd_vlan_valid(hapd->conf->vlan, &vlan_desc)) {
			sta->eapol_sm->authFail = TRUE;
			hostapd_logger(hapd, sta->addr,
				       HOSTAPD_MODULE_RADIUS,
				       HOSTAPD_LEVEL_INFO,
				       "Invalid VLAN %d%s received from RADIUS server",
				       vlan_desc.untagged,
				       vlan_desc.tagged[0] ? "+" : "");
			os_memset(&vlan_desc, 0, sizeof(vlan_desc));
			ap_sta_set_vlan(hapd, sta, &vlan_desc);
			break;
		}

		if (hapd->conf->ssid.dynamic_vlan == DYNAMIC_VLAN_REQUIRED &&
		    !vlan_desc.notempty) {
			sta->eapol_sm->authFail = TRUE;
			hostapd_logger(hapd, sta->addr,
				       HOSTAPD_MODULE_IEEE8021X,
				       HOSTAPD_LEVEL_INFO, "authentication "
				       "server did not include required VLAN "
				       "ID in Access-Accept");
			break;
		}
#endif /* CONFIG_NO_VLAN */

		if (ap_sta_set_vlan(hapd, sta, &vlan_desc) < 0)
			break;

#ifndef CONFIG_NO_VLAN
		if (sta->vlan_id > 0) {
			hostapd_logger(hapd, sta->addr,
				       HOSTAPD_MODULE_RADIUS,
				       HOSTAPD_LEVEL_INFO,
				       "VLAN ID %d", sta->vlan_id);
		}
#endif /* CONFIG_NO_VLAN */

		if ((sta->flags & WLAN_STA_ASSOC) &&
		    ap_sta_bind_vlan(hapd, sta) < 0)
			break;

		sta->session_timeout_set = !!session_timeout_set;
		os_get_reltime(&sta->session_timeout);
		sta->session_timeout.sec += session_timeout;

		/* RFC 3580, Ch. 3.17 */
		if (session_timeout_set && termination_action ==
		    RADIUS_TERMINATION_ACTION_RADIUS_REQUEST)
			sm->reAuthPeriod = session_timeout;
		else if (session_timeout_set)
			ap_sta_session_timeout(hapd, sta, session_timeout);
		else
			ap_sta_no_session_timeout(hapd, sta);

		sm->eap_if->aaaSuccess = TRUE;
		override_eapReq = 1;
		ieee802_1x_get_keys(hapd, sta, msg, req, shared_secret,
				    shared_secret_len);
		ieee802_1x_store_radius_class(hapd, sta, msg);
		ieee802_1x_update_sta_identity(hapd, sta, msg);
		ieee802_1x_update_sta_cui(hapd, sta, msg);
		ieee802_1x_check_hs20(hapd, sta, msg,
				      session_timeout_set ?
				      (int) session_timeout : -1);
		break;
	case RADIUS_CODE_ACCESS_REJECT:
		sm->eap_if->aaaFail = TRUE;
		override_eapReq = 1;
		if (radius_msg_get_attr_int32(msg, RADIUS_ATTR_WLAN_REASON_CODE,
					      &reason_code) == 0) {
			wpa_printf(MSG_DEBUG,
				   "RADIUS server indicated WLAN-Reason-Code %u in Access-Reject for "
				   MACSTR, reason_code, MAC2STR(sta->addr));
			sta->disconnect_reason_code = reason_code;
		}
		break;
	case RADIUS_CODE_ACCESS_CHALLENGE:
		sm->eap_if->aaaEapReq = TRUE;
		if (session_timeout_set) {
			/* RFC 2869, Ch. 2.3.2; RFC 3580, Ch. 3.17 */
			sm->eap_if->aaaMethodTimeout = session_timeout;
			hostapd_logger(hapd, sm->addr,
				       HOSTAPD_MODULE_IEEE8021X,
				       HOSTAPD_LEVEL_DEBUG,
				       "using EAP timeout of %d seconds (from "
				       "RADIUS)",
				       sm->eap_if->aaaMethodTimeout);
		} else {
			/*
			 * Use dynamic retransmission behavior per EAP
			 * specification.
			 */
			sm->eap_if->aaaMethodTimeout = 0;
		}
		break;
	}

	ieee802_1x_decapsulate_radius(hapd, sta);
	if (override_eapReq)
		sm->eap_if->aaaEapReq = FALSE;

#ifdef CONFIG_FILS
#ifdef NEED_AP_MLME
	if (sta->flags & WLAN_STA_PENDING_FILS_ERP) {
		/* TODO: Add a PMKSA entry on success? */
		ieee802_11_finish_fils_auth(
			hapd, sta, hdr->code == RADIUS_CODE_ACCESS_ACCEPT,
			sm->eap_if->aaaEapReqData,
			sm->eap_if->aaaEapKeyData,
			sm->eap_if->aaaEapKeyDataLen);
	}
#endif /* NEED_AP_MLME */
#endif /* CONFIG_FILS */

	eapol_auth_step(sm);

	return RADIUS_RX_QUEUED;
}
#endif /* CONFIG_NO_RADIUS */


void ieee802_1x_abort_auth(struct hostapd_data *hapd, struct sta_info *sta)
{
	struct eapol_state_machine *sm = sta->eapol_sm;
	if (sm == NULL)
		return;

	hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE8021X,
		       HOSTAPD_LEVEL_DEBUG, "aborting authentication");

#ifndef CONFIG_NO_RADIUS
	radius_msg_free(sm->last_recv_radius);
	sm->last_recv_radius = NULL;
#endif /* CONFIG_NO_RADIUS */

	if (sm->eap_if->eapTimeout) {
		/*
		 * Disconnect the STA since it did not reply to the last EAP
		 * request and we cannot continue EAP processing (EAP-Failure
		 * could only be sent if the EAP peer actually replied).
		 */
		wpa_dbg(hapd->msg_ctx, MSG_DEBUG, "EAP Timeout, STA " MACSTR,
			MAC2STR(sta->addr));

		sm->eap_if->portEnabled = FALSE;
		ap_sta_disconnect(hapd, sta, sta->addr,
				  WLAN_REASON_PREV_AUTH_NOT_VALID);
	}
}


static int ieee802_1x_rekey_broadcast(struct hostapd_data *hapd)
{
	struct eapol_authenticator *eapol = hapd->eapol_auth;

	if (hapd->conf->default_wep_key_len < 1)
		return 0;

	os_free(eapol->default_wep_key);
	eapol->default_wep_key = os_malloc(hapd->conf->default_wep_key_len);
	if (eapol->default_wep_key == NULL ||
	    random_get_bytes(eapol->default_wep_key,
			     hapd->conf->default_wep_key_len)) {
		wpa_printf(MSG_INFO, "Could not generate random WEP key");
		os_free(eapol->default_wep_key);
		eapol->default_wep_key = NULL;
		return -1;
	}

	wpa_hexdump_key(MSG_DEBUG, "IEEE 802.1X: New default WEP key",
			eapol->default_wep_key,
			hapd->conf->default_wep_key_len);

	return 0;
}


static int ieee802_1x_sta_key_available(struct hostapd_data *hapd,
					struct sta_info *sta, void *ctx)
{
	if (sta->eapol_sm) {
		sta->eapol_sm->eap_if->eapKeyAvailable = TRUE;
		eapol_auth_step(sta->eapol_sm);
	}
	return 0;
}


static void ieee802_1x_rekey(void *eloop_ctx, void *timeout_ctx)
{
	struct hostapd_data *hapd = eloop_ctx;
	struct eapol_authenticator *eapol = hapd->eapol_auth;

	if (eapol->default_wep_key_idx >= 3)
		eapol->default_wep_key_idx =
			hapd->conf->individual_wep_key_len > 0 ? 1 : 0;
	else
		eapol->default_wep_key_idx++;

	wpa_printf(MSG_DEBUG, "IEEE 802.1X: New default WEP key index %d",
		   eapol->default_wep_key_idx);

	if (ieee802_1x_rekey_broadcast(hapd)) {
		hostapd_logger(hapd, NULL, HOSTAPD_MODULE_IEEE8021X,
			       HOSTAPD_LEVEL_WARNING, "failed to generate a "
			       "new broadcast key");
		os_free(eapol->default_wep_key);
		eapol->default_wep_key = NULL;
		return;
	}

	/* TODO: Could setup key for RX here, but change default TX keyid only
	 * after new broadcast key has been sent to all stations. */
	if (hostapd_drv_set_key(hapd->conf->iface, hapd, WPA_ALG_WEP,
				broadcast_ether_addr,
				eapol->default_wep_key_idx, 1, NULL, 0,
				eapol->default_wep_key,
				hapd->conf->default_wep_key_len)) {
		hostapd_logger(hapd, NULL, HOSTAPD_MODULE_IEEE8021X,
			       HOSTAPD_LEVEL_WARNING, "failed to configure a "
			       "new broadcast key");
		os_free(eapol->default_wep_key);
		eapol->default_wep_key = NULL;
		return;
	}

	ap_for_each_sta(hapd, ieee802_1x_sta_key_available, NULL);

	if (hapd->conf->wep_rekeying_period > 0) {
		eloop_register_timeout(hapd->conf->wep_rekeying_period, 0,
				       ieee802_1x_rekey, hapd, NULL);
	}
}


static void ieee802_1x_eapol_send(void *ctx, void *sta_ctx, u8 type,
				  const u8 *data, size_t datalen)
{
#ifdef CONFIG_WPS
	struct sta_info *sta = sta_ctx;

	if ((sta->flags & (WLAN_STA_WPS | WLAN_STA_MAYBE_WPS)) ==
	    WLAN_STA_MAYBE_WPS) {
		const u8 *identity;
		size_t identity_len;
		struct eapol_state_machine *sm = sta->eapol_sm;

		identity = eap_get_identity(sm->eap, &identity_len);
		if (identity &&
		    ((identity_len == WSC_ID_ENROLLEE_LEN &&
		      os_memcmp(identity, WSC_ID_ENROLLEE,
				WSC_ID_ENROLLEE_LEN) == 0) ||
		     (identity_len == WSC_ID_REGISTRAR_LEN &&
		      os_memcmp(identity, WSC_ID_REGISTRAR,
				WSC_ID_REGISTRAR_LEN) == 0))) {
			wpa_printf(MSG_DEBUG, "WPS: WLAN_STA_MAYBE_WPS -> "
				   "WLAN_STA_WPS");
			sta->flags |= WLAN_STA_WPS;
		}
	}
#endif /* CONFIG_WPS */

	ieee802_1x_send(ctx, sta_ctx, type, data, datalen);
}


static void ieee802_1x_aaa_send(void *ctx, void *sta_ctx,
				const u8 *data, size_t datalen)
{
#ifndef CONFIG_NO_RADIUS
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta = sta_ctx;

	ieee802_1x_encapsulate_radius(hapd, sta, data, datalen);
#endif /* CONFIG_NO_RADIUS */
}


static void _ieee802_1x_finished(void *ctx, void *sta_ctx, int success,
				 int preauth, int remediation)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta = sta_ctx;
	if (preauth)
		rsn_preauth_finished(hapd, sta, success);
	else
		ieee802_1x_finished(hapd, sta, success, remediation);
}


static int ieee802_1x_get_eap_user(void *ctx, const u8 *identity,
				   size_t identity_len, int phase2,
				   struct eap_user *user)
{
	struct hostapd_data *hapd = ctx;
	const struct hostapd_eap_user *eap_user;
	int i;
	int rv = -1;

	eap_user = hostapd_get_eap_user(hapd, identity, identity_len, phase2);
	if (eap_user == NULL)
		goto out;

	os_memset(user, 0, sizeof(*user));
	user->phase2 = phase2;
	for (i = 0; i < EAP_MAX_METHODS; i++) {
		user->methods[i].vendor = eap_user->methods[i].vendor;
		user->methods[i].method = eap_user->methods[i].method;
	}

	if (eap_user->password) {
		user->password = os_memdup(eap_user->password,
					   eap_user->password_len);
		if (user->password == NULL)
			goto out;
		user->password_len = eap_user->password_len;
		user->password_hash = eap_user->password_hash;
		if (eap_user->salt && eap_user->salt_len) {
			user->salt = os_memdup(eap_user->salt,
					       eap_user->salt_len);
			if (!user->salt)
				goto out;
			user->salt_len = eap_user->salt_len;
		}
	}
	user->force_version = eap_user->force_version;
	user->macacl = eap_user->macacl;
	user->ttls_auth = eap_user->ttls_auth;
	user->remediation = eap_user->remediation;
	rv = 0;

out:
	if (rv)
		wpa_printf(MSG_DEBUG, "%s: Failed to find user", __func__);

	return rv;
}


static int ieee802_1x_sta_entry_alive(void *ctx, const u8 *addr)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;
	sta = ap_get_sta(hapd, addr);
	if (sta == NULL || sta->eapol_sm == NULL)
		return 0;
	return 1;
}


static void ieee802_1x_logger(void *ctx, const u8 *addr,
			      eapol_logger_level level, const char *txt)
{
#ifndef CONFIG_NO_HOSTAPD_LOGGER
	struct hostapd_data *hapd = ctx;
	int hlevel;

	switch (level) {
	case EAPOL_LOGGER_WARNING:
		hlevel = HOSTAPD_LEVEL_WARNING;
		break;
	case EAPOL_LOGGER_INFO:
		hlevel = HOSTAPD_LEVEL_INFO;
		break;
	case EAPOL_LOGGER_DEBUG:
	default:
		hlevel = HOSTAPD_LEVEL_DEBUG;
		break;
	}

	hostapd_logger(hapd, addr, HOSTAPD_MODULE_IEEE8021X, hlevel, "%s",
		       txt);
#endif /* CONFIG_NO_HOSTAPD_LOGGER */
}


static void ieee802_1x_set_port_authorized(void *ctx, void *sta_ctx,
					   int authorized)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta = sta_ctx;
	ieee802_1x_set_sta_authorized(hapd, sta, authorized);
}


static void _ieee802_1x_abort_auth(void *ctx, void *sta_ctx)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta = sta_ctx;
	ieee802_1x_abort_auth(hapd, sta);
}


static void _ieee802_1x_tx_key(void *ctx, void *sta_ctx)
{
#ifndef CONFIG_FIPS
#ifndef CONFIG_NO_RC4
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta = sta_ctx;
	ieee802_1x_tx_key(hapd, sta);
#endif /* CONFIG_NO_RC4 */
#endif /* CONFIG_FIPS */
}


static void ieee802_1x_eapol_event(void *ctx, void *sta_ctx,
				   enum eapol_event type)
{
	/* struct hostapd_data *hapd = ctx; */
	struct sta_info *sta = sta_ctx;
	switch (type) {
	case EAPOL_AUTH_SM_CHANGE:
		wpa_auth_sm_notify(sta->wpa_sm);
		break;
	case EAPOL_AUTH_REAUTHENTICATE:
		wpa_auth_sm_event(sta->wpa_sm, WPA_REAUTH_EAPOL);
		break;
	}
}


#ifdef CONFIG_ERP

static struct eap_server_erp_key *
ieee802_1x_erp_get_key(void *ctx, const char *keyname)
{
	struct hostapd_data *hapd = ctx;
	struct eap_server_erp_key *erp;

	dl_list_for_each(erp, &hapd->erp_keys, struct eap_server_erp_key,
			 list) {
		if (os_strcmp(erp->keyname_nai, keyname) == 0)
			return erp;
	}

	return NULL;
}


static int ieee802_1x_erp_add_key(void *ctx, struct eap_server_erp_key *erp)
{
	struct hostapd_data *hapd = ctx;

	dl_list_add(&hapd->erp_keys, &erp->list);
	return 0;
}

#endif /* CONFIG_ERP */


int ieee802_1x_init(struct hostapd_data *hapd)
{
	int i;
	struct eapol_auth_config conf;
	struct eapol_auth_cb cb;

	dl_list_init(&hapd->erp_keys);

	os_memset(&conf, 0, sizeof(conf));
	conf.ctx = hapd;
	conf.eap_reauth_period = hapd->conf->eap_reauth_period;
	conf.wpa = hapd->conf->wpa;
	conf.individual_wep_key_len = hapd->conf->individual_wep_key_len;
	conf.eap_server = hapd->conf->eap_server;
	conf.ssl_ctx = hapd->ssl_ctx;
	conf.msg_ctx = hapd->msg_ctx;
	conf.eap_sim_db_priv = hapd->eap_sim_db_priv;
	conf.eap_req_id_text = hapd->conf->eap_req_id_text;
	conf.eap_req_id_text_len = hapd->conf->eap_req_id_text_len;
	conf.erp_send_reauth_start = hapd->conf->erp_send_reauth_start;
	conf.erp_domain = hapd->conf->erp_domain;
	conf.erp = hapd->conf->eap_server_erp;
	conf.tls_session_lifetime = hapd->conf->tls_session_lifetime;
	conf.tls_flags = hapd->conf->tls_flags;
	conf.pac_opaque_encr_key = hapd->conf->pac_opaque_encr_key;
	conf.eap_fast_a_id = hapd->conf->eap_fast_a_id;
	conf.eap_fast_a_id_len = hapd->conf->eap_fast_a_id_len;
	conf.eap_fast_a_id_info = hapd->conf->eap_fast_a_id_info;
	conf.eap_fast_prov = hapd->conf->eap_fast_prov;
	conf.pac_key_lifetime = hapd->conf->pac_key_lifetime;
	conf.pac_key_refresh_time = hapd->conf->pac_key_refresh_time;
	conf.eap_sim_aka_result_ind = hapd->conf->eap_sim_aka_result_ind;
	conf.tnc = hapd->conf->tnc;
	conf.wps = hapd->wps;
	conf.fragment_size = hapd->conf->fragment_size;
	conf.pwd_group = hapd->conf->pwd_group;
	conf.pbc_in_m1 = hapd->conf->pbc_in_m1;
	if (hapd->conf->server_id) {
		conf.server_id = (const u8 *) hapd->conf->server_id;
		conf.server_id_len = os_strlen(hapd->conf->server_id);
	} else {
		conf.server_id = (const u8 *) "hostapd";
		conf.server_id_len = 7;
	}

	os_memset(&cb, 0, sizeof(cb));
	cb.eapol_send = ieee802_1x_eapol_send;
	cb.aaa_send = ieee802_1x_aaa_send;
	cb.finished = _ieee802_1x_finished;
	cb.get_eap_user = ieee802_1x_get_eap_user;
	cb.sta_entry_alive = ieee802_1x_sta_entry_alive;
	cb.logger = ieee802_1x_logger;
	cb.set_port_authorized = ieee802_1x_set_port_authorized;
	cb.abort_auth = _ieee802_1x_abort_auth;
	cb.tx_key = _ieee802_1x_tx_key;
	cb.eapol_event = ieee802_1x_eapol_event;
#ifdef CONFIG_ERP
	cb.erp_get_key = ieee802_1x_erp_get_key;
	cb.erp_add_key = ieee802_1x_erp_add_key;
#endif /* CONFIG_ERP */

	hapd->eapol_auth = eapol_auth_init(&conf, &cb);
	if (hapd->eapol_auth == NULL)
		return -1;

	if ((hapd->conf->ieee802_1x || hapd->conf->wpa) &&
	    hostapd_set_drv_ieee8021x(hapd, hapd->conf->iface, 1))
		return -1;

#ifndef CONFIG_NO_RADIUS
	if (radius_client_register(hapd->radius, RADIUS_AUTH,
				   ieee802_1x_receive_auth, hapd))
		return -1;
#endif /* CONFIG_NO_RADIUS */

	if (hapd->conf->default_wep_key_len) {
		for (i = 0; i < 4; i++)
			hostapd_drv_set_key(hapd->conf->iface, hapd,
					    WPA_ALG_NONE, NULL, i, 0, NULL, 0,
					    NULL, 0);

		ieee802_1x_rekey(hapd, NULL);

		if (hapd->eapol_auth->default_wep_key == NULL)
			return -1;
	}

	return 0;
}


void ieee802_1x_erp_flush(struct hostapd_data *hapd)
{
	struct eap_server_erp_key *erp;

	while ((erp = dl_list_first(&hapd->erp_keys, struct eap_server_erp_key,
				    list)) != NULL) {
		dl_list_del(&erp->list);
		bin_clear_free(erp, sizeof(*erp));
	}
}


void ieee802_1x_deinit(struct hostapd_data *hapd)
{
	eloop_cancel_timeout(ieee802_1x_rekey, hapd, NULL);

	if (hapd->driver && hapd->drv_priv &&
	    (hapd->conf->ieee802_1x || hapd->conf->wpa))
		hostapd_set_drv_ieee8021x(hapd, hapd->conf->iface, 0);

	eapol_auth_deinit(hapd->eapol_auth);
	hapd->eapol_auth = NULL;

	ieee802_1x_erp_flush(hapd);
}


int ieee802_1x_tx_status(struct hostapd_data *hapd, struct sta_info *sta,
			 const u8 *buf, size_t len, int ack)
{
	struct ieee80211_hdr *hdr;
	u8 *pos;
	const unsigned char rfc1042_hdr[ETH_ALEN] =
		{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };

	if (sta == NULL)
		return -1;
	if (len < sizeof(*hdr) + sizeof(rfc1042_hdr) + 2)
		return 0;

	hdr = (struct ieee80211_hdr *) buf;
	pos = (u8 *) (hdr + 1);
	if (os_memcmp(pos, rfc1042_hdr, sizeof(rfc1042_hdr)) != 0)
		return 0;
	pos += sizeof(rfc1042_hdr);
	if (WPA_GET_BE16(pos) != ETH_P_PAE)
		return 0;
	pos += 2;

	return ieee802_1x_eapol_tx_status(hapd, sta, pos, buf + len - pos,
					  ack);
}


int ieee802_1x_eapol_tx_status(struct hostapd_data *hapd, struct sta_info *sta,
			       const u8 *buf, int len, int ack)
{
	const struct ieee802_1x_hdr *xhdr =
		(const struct ieee802_1x_hdr *) buf;
	const u8 *pos = buf + sizeof(*xhdr);
	struct ieee802_1x_eapol_key *key;

	if (len < (int) sizeof(*xhdr))
		return 0;
	wpa_printf(MSG_DEBUG, "IEEE 802.1X: " MACSTR " TX status - version=%d "
		   "type=%d length=%d - ack=%d",
		   MAC2STR(sta->addr), xhdr->version, xhdr->type,
		   be_to_host16(xhdr->length), ack);

#ifdef CONFIG_WPS
	if (xhdr->type == IEEE802_1X_TYPE_EAP_PACKET && ack &&
	    (sta->flags & WLAN_STA_WPS) &&
	    ap_sta_pending_delayed_1x_auth_fail_disconnect(hapd, sta)) {
		wpa_printf(MSG_DEBUG,
			   "WPS: Indicate EAP completion on ACK for EAP-Failure");
		hostapd_wps_eap_completed(hapd);
	}
#endif /* CONFIG_WPS */

	if (xhdr->type != IEEE802_1X_TYPE_EAPOL_KEY)
		return 0;

	if (pos + sizeof(struct wpa_eapol_key) <= buf + len) {
		const struct wpa_eapol_key *wpa;
		wpa = (const struct wpa_eapol_key *) pos;
		if (wpa->type == EAPOL_KEY_TYPE_RSN ||
		    wpa->type == EAPOL_KEY_TYPE_WPA)
			wpa_auth_eapol_key_tx_status(hapd->wpa_auth,
						     sta->wpa_sm, ack);
	}

	/* EAPOL EAP-Packet packets are eventually re-sent by either Supplicant
	 * or Authenticator state machines, but EAPOL-Key packets are not
	 * retransmitted in case of failure. Try to re-send failed EAPOL-Key
	 * packets couple of times because otherwise STA keys become
	 * unsynchronized with AP. */
	if (!ack && pos + sizeof(*key) <= buf + len) {
		key = (struct ieee802_1x_eapol_key *) pos;
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE8021X,
			       HOSTAPD_LEVEL_DEBUG, "did not Ack EAPOL-Key "
			       "frame (%scast index=%d)",
			       key->key_index & BIT(7) ? "uni" : "broad",
			       key->key_index & ~BIT(7));
		/* TODO: re-send EAPOL-Key couple of times (with short delay
		 * between them?). If all attempt fail, report error and
		 * deauthenticate STA so that it will get new keys when
		 * authenticating again (e.g., after returning in range).
		 * Separate limit/transmit state needed both for unicast and
		 * broadcast keys(?) */
	}
	/* TODO: could move unicast key configuration from ieee802_1x_tx_key()
	 * to here and change the key only if the EAPOL-Key packet was Acked.
	 */

	return 1;
}


u8 * ieee802_1x_get_identity(struct eapol_state_machine *sm, size_t *len)
{
	if (sm == NULL || sm->identity == NULL)
		return NULL;

	*len = sm->identity_len;
	return sm->identity;
}


u8 * ieee802_1x_get_radius_class(struct eapol_state_machine *sm, size_t *len,
				 int idx)
{
	if (sm == NULL || sm->radius_class.attr == NULL ||
	    idx >= (int) sm->radius_class.count)
		return NULL;

	*len = sm->radius_class.attr[idx].len;
	return sm->radius_class.attr[idx].data;
}


struct wpabuf * ieee802_1x_get_radius_cui(struct eapol_state_machine *sm)
{
	if (sm == NULL)
		return NULL;
	return sm->radius_cui;
}


const u8 * ieee802_1x_get_key(struct eapol_state_machine *sm, size_t *len)
{
	*len = 0;
	if (sm == NULL)
		return NULL;

	*len = sm->eap_if->eapKeyDataLen;
	return sm->eap_if->eapKeyData;
}


void ieee802_1x_notify_port_enabled(struct eapol_state_machine *sm,
				    int enabled)
{
	if (sm == NULL)
		return;
	sm->eap_if->portEnabled = enabled ? TRUE : FALSE;
	eapol_auth_step(sm);
}


void ieee802_1x_notify_port_valid(struct eapol_state_machine *sm,
				  int valid)
{
	if (sm == NULL)
		return;
	sm->portValid = valid ? TRUE : FALSE;
	eapol_auth_step(sm);
}


void ieee802_1x_notify_pre_auth(struct eapol_state_machine *sm, int pre_auth)
{
	if (sm == NULL)
		return;
	if (pre_auth)
		sm->flags |= EAPOL_SM_PREAUTH;
	else
		sm->flags &= ~EAPOL_SM_PREAUTH;
}


static const char * bool_txt(Boolean val)
{
	return val ? "TRUE" : "FALSE";
}


int ieee802_1x_get_mib(struct hostapd_data *hapd, char *buf, size_t buflen)
{
	/* TODO */
	return 0;
}


int ieee802_1x_get_mib_sta(struct hostapd_data *hapd, struct sta_info *sta,
			   char *buf, size_t buflen)
{
	int len = 0, ret;
	struct eapol_state_machine *sm = sta->eapol_sm;
	struct os_reltime diff;
	const char *name1;
	const char *name2;

	if (sm == NULL)
		return 0;

	ret = os_snprintf(buf + len, buflen - len,
			  "dot1xPaePortNumber=%d\n"
			  "dot1xPaePortProtocolVersion=%d\n"
			  "dot1xPaePortCapabilities=1\n"
			  "dot1xPaePortInitialize=%d\n"
			  "dot1xPaePortReauthenticate=FALSE\n",
			  sta->aid,
			  EAPOL_VERSION,
			  sm->initialize);
	if (os_snprintf_error(buflen - len, ret))
		return len;
	len += ret;

	/* dot1xAuthConfigTable */
	ret = os_snprintf(buf + len, buflen - len,
			  "dot1xAuthPaeState=%d\n"
			  "dot1xAuthBackendAuthState=%d\n"
			  "dot1xAuthAdminControlledDirections=%d\n"
			  "dot1xAuthOperControlledDirections=%d\n"
			  "dot1xAuthAuthControlledPortStatus=%d\n"
			  "dot1xAuthAuthControlledPortControl=%d\n"
			  "dot1xAuthQuietPeriod=%u\n"
			  "dot1xAuthServerTimeout=%u\n"
			  "dot1xAuthReAuthPeriod=%u\n"
			  "dot1xAuthReAuthEnabled=%s\n"
			  "dot1xAuthKeyTxEnabled=%s\n",
			  sm->auth_pae_state + 1,
			  sm->be_auth_state + 1,
			  sm->adminControlledDirections,
			  sm->operControlledDirections,
			  sm->authPortStatus,
			  sm->portControl,
			  sm->quietPeriod,
			  sm->serverTimeout,
			  sm->reAuthPeriod,
			  bool_txt(sm->reAuthEnabled),
			  bool_txt(sm->keyTxEnabled));
	if (os_snprintf_error(buflen - len, ret))
		return len;
	len += ret;

	/* dot1xAuthStatsTable */
	ret = os_snprintf(buf + len, buflen - len,
			  "dot1xAuthEapolFramesRx=%u\n"
			  "dot1xAuthEapolFramesTx=%u\n"
			  "dot1xAuthEapolStartFramesRx=%u\n"
			  "dot1xAuthEapolLogoffFramesRx=%u\n"
			  "dot1xAuthEapolRespIdFramesRx=%u\n"
			  "dot1xAuthEapolRespFramesRx=%u\n"
			  "dot1xAuthEapolReqIdFramesTx=%u\n"
			  "dot1xAuthEapolReqFramesTx=%u\n"
			  "dot1xAuthInvalidEapolFramesRx=%u\n"
			  "dot1xAuthEapLengthErrorFramesRx=%u\n"
			  "dot1xAuthLastEapolFrameVersion=%u\n"
			  "dot1xAuthLastEapolFrameSource=" MACSTR "\n",
			  sm->dot1xAuthEapolFramesRx,
			  sm->dot1xAuthEapolFramesTx,
			  sm->dot1xAuthEapolStartFramesRx,
			  sm->dot1xAuthEapolLogoffFramesRx,
			  sm->dot1xAuthEapolRespIdFramesRx,
			  sm->dot1xAuthEapolRespFramesRx,
			  sm->dot1xAuthEapolReqIdFramesTx,
			  sm->dot1xAuthEapolReqFramesTx,
			  sm->dot1xAuthInvalidEapolFramesRx,
			  sm->dot1xAuthEapLengthErrorFramesRx,
			  sm->dot1xAuthLastEapolFrameVersion,
			  MAC2STR(sm->addr));
	if (os_snprintf_error(buflen - len, ret))
		return len;
	len += ret;

	/* dot1xAuthDiagTable */
	ret = os_snprintf(buf + len, buflen - len,
			  "dot1xAuthEntersConnecting=%u\n"
			  "dot1xAuthEapLogoffsWhileConnecting=%u\n"
			  "dot1xAuthEntersAuthenticating=%u\n"
			  "dot1xAuthAuthSuccessesWhileAuthenticating=%u\n"
			  "dot1xAuthAuthTimeoutsWhileAuthenticating=%u\n"
			  "dot1xAuthAuthFailWhileAuthenticating=%u\n"
			  "dot1xAuthAuthEapStartsWhileAuthenticating=%u\n"
			  "dot1xAuthAuthEapLogoffWhileAuthenticating=%u\n"
			  "dot1xAuthAuthReauthsWhileAuthenticated=%u\n"
			  "dot1xAuthAuthEapStartsWhileAuthenticated=%u\n"
			  "dot1xAuthAuthEapLogoffWhileAuthenticated=%u\n"
			  "dot1xAuthBackendResponses=%u\n"
			  "dot1xAuthBackendAccessChallenges=%u\n"
			  "dot1xAuthBackendOtherRequestsToSupplicant=%u\n"
			  "dot1xAuthBackendAuthSuccesses=%u\n"
			  "dot1xAuthBackendAuthFails=%u\n",
			  sm->authEntersConnecting,
			  sm->authEapLogoffsWhileConnecting,
			  sm->authEntersAuthenticating,
			  sm->authAuthSuccessesWhileAuthenticating,
			  sm->authAuthTimeoutsWhileAuthenticating,
			  sm->authAuthFailWhileAuthenticating,
			  sm->authAuthEapStartsWhileAuthenticating,
			  sm->authAuthEapLogoffWhileAuthenticating,
			  sm->authAuthReauthsWhileAuthenticated,
			  sm->authAuthEapStartsWhileAuthenticated,
			  sm->authAuthEapLogoffWhileAuthenticated,
			  sm->backendResponses,
			  sm->backendAccessChallenges,
			  sm->backendOtherRequestsToSupplicant,
			  sm->backendAuthSuccesses,
			  sm->backendAuthFails);
	if (os_snprintf_error(buflen - len, ret))
		return len;
	len += ret;

	/* dot1xAuthSessionStatsTable */
	os_reltime_age(&sta->acct_session_start, &diff);
	ret = os_snprintf(buf + len, buflen - len,
			  /* TODO: dot1xAuthSessionOctetsRx */
			  /* TODO: dot1xAuthSessionOctetsTx */
			  /* TODO: dot1xAuthSessionFramesRx */
			  /* TODO: dot1xAuthSessionFramesTx */
			  "dot1xAuthSessionId=%016llX\n"
			  "dot1xAuthSessionAuthenticMethod=%d\n"
			  "dot1xAuthSessionTime=%u\n"
			  "dot1xAuthSessionTerminateCause=999\n"
			  "dot1xAuthSessionUserName=%s\n",
			  (unsigned long long) sta->acct_session_id,
			  (wpa_key_mgmt_wpa_ieee8021x(
				   wpa_auth_sta_key_mgmt(sta->wpa_sm))) ?
			  1 : 2,
			  (unsigned int) diff.sec,
			  sm->identity);
	if (os_snprintf_error(buflen - len, ret))
		return len;
	len += ret;

	if (sm->acct_multi_session_id) {
		ret = os_snprintf(buf + len, buflen - len,
				  "authMultiSessionId=%016llX\n",
				  (unsigned long long)
				  sm->acct_multi_session_id);
		if (os_snprintf_error(buflen - len, ret))
			return len;
		len += ret;
	}

	name1 = eap_server_get_name(0, sm->eap_type_authsrv);
	name2 = eap_server_get_name(0, sm->eap_type_supp);
	ret = os_snprintf(buf + len, buflen - len,
			  "last_eap_type_as=%d (%s)\n"
			  "last_eap_type_sta=%d (%s)\n",
			  sm->eap_type_authsrv, name1,
			  sm->eap_type_supp, name2);
	if (os_snprintf_error(buflen - len, ret))
		return len;
	len += ret;

	return len;
}


#ifdef CONFIG_HS20
static void ieee802_1x_wnm_notif_send(void *eloop_ctx, void *timeout_ctx)
{
	struct hostapd_data *hapd = eloop_ctx;
	struct sta_info *sta = timeout_ctx;

	if (sta->remediation) {
		wpa_printf(MSG_DEBUG, "HS 2.0: Send WNM-Notification to "
			   MACSTR " to indicate Subscription Remediation",
			   MAC2STR(sta->addr));
		hs20_send_wnm_notification(hapd, sta->addr,
					   sta->remediation_method,
					   sta->remediation_url);
		os_free(sta->remediation_url);
		sta->remediation_url = NULL;
	}

	if (sta->hs20_deauth_req) {
		wpa_printf(MSG_DEBUG, "HS 2.0: Send WNM-Notification to "
			   MACSTR " to indicate imminent deauthentication",
			   MAC2STR(sta->addr));
		hs20_send_wnm_notification_deauth_req(hapd, sta->addr,
						      sta->hs20_deauth_req);
	}

	if (sta->hs20_t_c_filtering) {
		wpa_printf(MSG_DEBUG, "HS 2.0: Send WNM-Notification to "
			   MACSTR " to indicate Terms and Conditions filtering",
			   MAC2STR(sta->addr));
		hs20_send_wnm_notification_t_c(hapd, sta->addr, sta->t_c_url);
		os_free(sta->t_c_url);
		sta->t_c_url = NULL;
	}
}
#endif /* CONFIG_HS20 */


static void ieee802_1x_finished(struct hostapd_data *hapd,
				struct sta_info *sta, int success,
				int remediation)
{
	const u8 *key;
	size_t len;
	/* TODO: get PMKLifetime from WPA parameters */
	static const int dot11RSNAConfigPMKLifetime = 43200;
	unsigned int session_timeout;
	struct os_reltime now, remaining;

#ifdef CONFIG_HS20
	if (remediation && !sta->remediation) {
		sta->remediation = 1;
		os_free(sta->remediation_url);
		sta->remediation_url =
			os_strdup(hapd->conf->subscr_remediation_url);
		sta->remediation_method = 1; /* SOAP-XML SPP */
	}

	if (success && (sta->remediation || sta->hs20_deauth_req ||
			sta->hs20_t_c_filtering)) {
		wpa_printf(MSG_DEBUG, "HS 2.0: Schedule WNM-Notification to "
			   MACSTR " in 100 ms", MAC2STR(sta->addr));
		eloop_cancel_timeout(ieee802_1x_wnm_notif_send, hapd, sta);
		eloop_register_timeout(0, 100000, ieee802_1x_wnm_notif_send,
				       hapd, sta);
	}
#endif /* CONFIG_HS20 */

	key = ieee802_1x_get_key(sta->eapol_sm, &len);
	if (sta->session_timeout_set) {
		os_get_reltime(&now);
		os_reltime_sub(&sta->session_timeout, &now, &remaining);
		session_timeout = (remaining.sec > 0) ? remaining.sec : 1;
	} else {
		session_timeout = dot11RSNAConfigPMKLifetime;
	}
	if (success && key && len >= PMK_LEN && !sta->remediation &&
	    !sta->hs20_deauth_requested &&
	    wpa_auth_pmksa_add(sta->wpa_sm, key, len, session_timeout,
			       sta->eapol_sm) == 0) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_WPA,
			       HOSTAPD_LEVEL_DEBUG,
			       "Added PMKSA cache entry (IEEE 802.1X)");
	}

	if (!success) {
		/*
		 * Many devices require deauthentication after WPS provisioning
		 * and some may not be be able to do that themselves, so
		 * disconnect the client here. In addition, this may also
		 * benefit IEEE 802.1X/EAPOL authentication cases, too since
		 * the EAPOL PAE state machine would remain in HELD state for
		 * considerable amount of time and some EAP methods, like
		 * EAP-FAST with anonymous provisioning, may require another
		 * EAPOL authentication to be started to complete connection.
		 */
		ap_sta_delayed_1x_auth_fail_disconnect(hapd, sta);
	}
}
