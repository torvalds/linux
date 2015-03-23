/*
 * hostapd / IEEE 802.1X-2004 Authenticator
 * Copyright (c) 2002-2011, Jouni Malinen <j@w1.fi>
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
#include "crypto/md5.h"
#include "crypto/crypto.h"
#include "crypto/random.h"
#include "common/ieee802_11_defs.h"
#include "common/wpa_ctrl.h"
#include "radius/radius.h"
#include "radius/radius_client.h"
#include "eap_server/eap.h"
#include "eap_common/eap_wsc_common.h"
#include "eapol_auth/eapol_auth_sm.h"
#include "eapol_auth/eapol_auth_sm_i.h"
#include "hostapd.h"
#include "accounting.h"
#include "sta_info.h"
#include "wpa_auth.h"
#include "preauth_auth.h"
#include "pmksa_cache_auth.h"
#include "ap_config.h"
#include "ap_drv_ops.h"
#include "ieee802_1x.h"


static void ieee802_1x_finished(struct hostapd_data *hapd,
				struct sta_info *sta, int success);


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
	if (sta->flags & WLAN_STA_PREAUTH) {
		rsn_preauth_send(hapd, sta, buf, len);
	} else {
		hostapd_drv_hapd_send_eapol(hapd, sta->addr, buf, len,
					    encrypt, sta->flags);
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
		if (!ap_sta_is_authorized(sta))
			wpa_msg(hapd->msg_ctx, MSG_INFO,
				AP_STA_CONNECTED MACSTR, MAC2STR(sta->addr));
		ap_sta_set_authorized(hapd, sta, 1);
		res = hostapd_set_authorized(hapd, sta, 1);
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE8021X,
			       HOSTAPD_LEVEL_DEBUG, "authorizing port");
	} else {
		if (ap_sta_is_authorized(sta) && (sta->flags & WLAN_STA_ASSOC))
			wpa_msg(hapd->msg_ctx, MSG_INFO,
				AP_STA_DISCONNECTED MACSTR,
				MAC2STR(sta->addr));
		ap_sta_set_authorized(hapd, sta, 0);
		res = hostapd_set_authorized(hapd, sta, 0);
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE8021X,
			       HOSTAPD_LEVEL_DEBUG, "unauthorizing port");
	}

	if (res && errno != ENOENT) {
		printf("Could not set station " MACSTR " flags for kernel "
		       "driver (errno=%d).\n", MAC2STR(sta->addr), errno);
	}

	if (authorized)
		accounting_sta_start(hapd, sta);
}


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
	key->key_length = htons(key_len);
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


#ifndef CONFIG_NO_VLAN
static struct hostapd_wep_keys *
ieee802_1x_group_alloc(struct hostapd_data *hapd, const char *ifname)
{
	struct hostapd_wep_keys *key;

	key = os_zalloc(sizeof(*key));
	if (key == NULL)
		return NULL;

	key->default_len = hapd->conf->default_wep_key_len;

	if (key->idx >= hapd->conf->broadcast_key_idx_max ||
	    key->idx < hapd->conf->broadcast_key_idx_min)
		key->idx = hapd->conf->broadcast_key_idx_min;
	else
		key->idx++;

	if (!key->key[key->idx])
		key->key[key->idx] = os_malloc(key->default_len);
	if (key->key[key->idx] == NULL ||
	    random_get_bytes(key->key[key->idx], key->default_len)) {
		printf("Could not generate random WEP key (dynamic VLAN).\n");
		os_free(key->key[key->idx]);
		key->key[key->idx] = NULL;
		os_free(key);
		return NULL;
	}
	key->len[key->idx] = key->default_len;

	wpa_printf(MSG_DEBUG, "%s: Default WEP idx %d for dynamic VLAN\n",
		   ifname, key->idx);
	wpa_hexdump_key(MSG_DEBUG, "Default WEP key (dynamic VLAN)",
			key->key[key->idx], key->len[key->idx]);

	if (hostapd_drv_set_key(ifname, hapd, WPA_ALG_WEP,
				broadcast_ether_addr, key->idx, 1,
				NULL, 0, key->key[key->idx],
				key->len[key->idx]))
		printf("Could not set dynamic VLAN WEP encryption key.\n");

	hostapd_set_drv_ieee8021x(hapd, ifname, 1);

	return key;
}


static struct hostapd_wep_keys *
ieee802_1x_get_group(struct hostapd_data *hapd, struct hostapd_ssid *ssid,
		     size_t vlan_id)
{
	const char *ifname;

	if (vlan_id == 0)
		return &ssid->wep;

	if (vlan_id <= ssid->max_dyn_vlan_keys && ssid->dyn_vlan_keys &&
	    ssid->dyn_vlan_keys[vlan_id])
		return ssid->dyn_vlan_keys[vlan_id];

	wpa_printf(MSG_DEBUG, "IEEE 802.1X: Creating new group "
		   "state machine for VLAN ID %lu",
		   (unsigned long) vlan_id);

	ifname = hostapd_get_vlan_id_ifname(hapd->conf->vlan, vlan_id);
	if (ifname == NULL) {
		wpa_printf(MSG_DEBUG, "IEEE 802.1X: Unknown VLAN ID %lu - "
			   "cannot create group key state machine",
			   (unsigned long) vlan_id);
		return NULL;
	}

	if (ssid->dyn_vlan_keys == NULL) {
		int size = (vlan_id + 1) * sizeof(ssid->dyn_vlan_keys[0]);
		ssid->dyn_vlan_keys = os_zalloc(size);
		if (ssid->dyn_vlan_keys == NULL)
			return NULL;
		ssid->max_dyn_vlan_keys = vlan_id;
	}

	if (ssid->max_dyn_vlan_keys < vlan_id) {
		struct hostapd_wep_keys **na;
		int size = (vlan_id + 1) * sizeof(ssid->dyn_vlan_keys[0]);
		na = os_realloc(ssid->dyn_vlan_keys, size);
		if (na == NULL)
			return NULL;
		ssid->dyn_vlan_keys = na;
		os_memset(&ssid->dyn_vlan_keys[ssid->max_dyn_vlan_keys + 1], 0,
			  (vlan_id - ssid->max_dyn_vlan_keys) *
			  sizeof(ssid->dyn_vlan_keys[0]));
		ssid->max_dyn_vlan_keys = vlan_id;
	}

	ssid->dyn_vlan_keys[vlan_id] = ieee802_1x_group_alloc(hapd, ifname);

	return ssid->dyn_vlan_keys[vlan_id];
}
#endif /* CONFIG_NO_VLAN */


void ieee802_1x_tx_key(struct hostapd_data *hapd, struct sta_info *sta)
{
	struct eapol_authenticator *eapol = hapd->eapol_auth;
	struct eapol_state_machine *sm = sta->eapol_sm;
#ifndef CONFIG_NO_VLAN
	struct hostapd_wep_keys *key = NULL;
	int vlan_id;
#endif /* CONFIG_NO_VLAN */

	if (sm == NULL || !sm->eap_if->eapKeyData)
		return;

	wpa_printf(MSG_DEBUG, "IEEE 802.1X: Sending EAPOL-Key(s) to " MACSTR,
		   MAC2STR(sta->addr));

#ifndef CONFIG_NO_VLAN
	vlan_id = sta->vlan_id;
	if (vlan_id < 0 || vlan_id > MAX_VLAN_ID)
		vlan_id = 0;

	if (vlan_id) {
		key = ieee802_1x_get_group(hapd, sta->ssid, vlan_id);
		if (key && key->key[key->idx])
			ieee802_1x_tx_key_one(hapd, sta, key->idx, 1,
					      key->key[key->idx],
					      key->len[key->idx]);
	} else
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


const char *radius_mode_txt(struct hostapd_data *hapd)
{
	switch (hapd->iface->conf->hw_mode) {
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

	if (len <= sizeof(struct eap_hdr) ||
	    eap[sizeof(struct eap_hdr)] != EAP_TYPE_IDENTITY)
		return;

	identity = eap_get_identity(sm->eap, &identity_len);
	if (identity == NULL)
		return;

	/* Save station identity for future RADIUS packets */
	os_free(sm->identity);
	sm->identity = os_malloc(identity_len + 1);
	if (sm->identity == NULL) {
		sm->identity_len = 0;
		return;
	}

	os_memcpy(sm->identity, identity, identity_len);
	sm->identity_len = identity_len;
	sm->identity[identity_len] = '\0';
	hostapd_logger(hapd, sm->addr, HOSTAPD_MODULE_IEEE8021X,
		       HOSTAPD_LEVEL_DEBUG, "STA identity '%s'", sm->identity);
	sm->dot1xAuthEapolRespIdFramesRx++;
}


static void ieee802_1x_encapsulate_radius(struct hostapd_data *hapd,
					  struct sta_info *sta,
					  const u8 *eap, size_t len)
{
	struct radius_msg *msg;
	char buf[128];
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
		printf("Could not create net RADIUS packet\n");
		return;
	}

	radius_msg_make_authenticator(msg, (u8 *) sta, sizeof(*sta));

	if (sm->identity &&
	    !radius_msg_add_attr(msg, RADIUS_ATTR_USER_NAME,
				 sm->identity, sm->identity_len)) {
		printf("Could not add User-Name\n");
		goto fail;
	}

	if (hapd->conf->own_ip_addr.af == AF_INET &&
	    !radius_msg_add_attr(msg, RADIUS_ATTR_NAS_IP_ADDRESS,
				 (u8 *) &hapd->conf->own_ip_addr.u.v4, 4)) {
		printf("Could not add NAS-IP-Address\n");
		goto fail;
	}

#ifdef CONFIG_IPV6
	if (hapd->conf->own_ip_addr.af == AF_INET6 &&
	    !radius_msg_add_attr(msg, RADIUS_ATTR_NAS_IPV6_ADDRESS,
				 (u8 *) &hapd->conf->own_ip_addr.u.v6, 16)) {
		printf("Could not add NAS-IPv6-Address\n");
		goto fail;
	}
#endif /* CONFIG_IPV6 */

	if (hapd->conf->nas_identifier &&
	    !radius_msg_add_attr(msg, RADIUS_ATTR_NAS_IDENTIFIER,
				 (u8 *) hapd->conf->nas_identifier,
				 os_strlen(hapd->conf->nas_identifier))) {
		printf("Could not add NAS-Identifier\n");
		goto fail;
	}

	if (!radius_msg_add_attr_int32(msg, RADIUS_ATTR_NAS_PORT, sta->aid)) {
		printf("Could not add NAS-Port\n");
		goto fail;
	}

	os_snprintf(buf, sizeof(buf), RADIUS_802_1X_ADDR_FORMAT ":%s",
		    MAC2STR(hapd->own_addr), hapd->conf->ssid.ssid);
	buf[sizeof(buf) - 1] = '\0';
	if (!radius_msg_add_attr(msg, RADIUS_ATTR_CALLED_STATION_ID,
				 (u8 *) buf, os_strlen(buf))) {
		printf("Could not add Called-Station-Id\n");
		goto fail;
	}

	os_snprintf(buf, sizeof(buf), RADIUS_802_1X_ADDR_FORMAT,
		    MAC2STR(sta->addr));
	buf[sizeof(buf) - 1] = '\0';
	if (!radius_msg_add_attr(msg, RADIUS_ATTR_CALLING_STATION_ID,
				 (u8 *) buf, os_strlen(buf))) {
		printf("Could not add Calling-Station-Id\n");
		goto fail;
	}

	/* TODO: should probably check MTU from driver config; 2304 is max for
	 * IEEE 802.11, but use 1400 to avoid problems with too large packets
	 */
	if (!radius_msg_add_attr_int32(msg, RADIUS_ATTR_FRAMED_MTU, 1400)) {
		printf("Could not add Framed-MTU\n");
		goto fail;
	}

	if (!radius_msg_add_attr_int32(msg, RADIUS_ATTR_NAS_PORT_TYPE,
				       RADIUS_NAS_PORT_TYPE_IEEE_802_11)) {
		printf("Could not add NAS-Port-Type\n");
		goto fail;
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
	if (!radius_msg_add_attr(msg, RADIUS_ATTR_CONNECT_INFO,
				 (u8 *) buf, os_strlen(buf))) {
		printf("Could not add Connect-Info\n");
		goto fail;
	}

	if (eap && !radius_msg_add_eap(msg, eap, len)) {
		printf("Could not add EAP-Message\n");
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
			printf("Could not copy State attribute from previous "
			       "Access-Challenge\n");
			goto fail;
		}
		if (res > 0) {
			wpa_printf(MSG_DEBUG, "Copied RADIUS State Attribute");
		}
	}

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
		printf("handle_eap_response: too short response data\n");
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


/* Process incoming EAP packet from Supplicant */
static void handle_eap(struct hostapd_data *hapd, struct sta_info *sta,
		       u8 *buf, size_t len)
{
	struct eap_hdr *eap;
	u16 eap_len;

	if (len < sizeof(*eap)) {
		printf("   too short EAP packet\n");
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
	default:
		wpa_printf(MSG_DEBUG, " (unknown code)");
		return;
	}
}


static struct eapol_state_machine *
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
				sta->wps_ie, sta->p2p_ie, sta);
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

	if (!hapd->conf->ieee802_1x && !hapd->conf->wpa &&
	    !hapd->conf->wps_state)
		return;

	wpa_printf(MSG_DEBUG, "IEEE 802.1X: %lu bytes from " MACSTR,
		   (unsigned long) len, MAC2STR(sa));
	sta = ap_get_sta(hapd, sa);
	if (!sta || !(sta->flags & (WLAN_STA_ASSOC | WLAN_STA_PREAUTH))) {
		wpa_printf(MSG_DEBUG, "IEEE 802.1X data frame from not "
			   "associated/Pre-authenticating STA");
		return;
	}

	if (len < sizeof(*hdr)) {
		printf("   too short IEEE 802.1X packet\n");
		return;
	}

	hdr = (struct ieee802_1x_hdr *) buf;
	datalen = be_to_host16(hdr->length);
	wpa_printf(MSG_DEBUG, "   IEEE 802.1X: version=%d type=%d length=%d",
		   hdr->version, hdr->type, datalen);

	if (len - sizeof(*hdr) < datalen) {
		printf("   frame too short for this IEEE 802.1X packet\n");
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

	if (!hapd->conf->ieee802_1x &&
	    !(sta->flags & (WLAN_STA_WPS | WLAN_STA_MAYBE_WPS))) {
		wpa_printf(MSG_DEBUG, "IEEE 802.1X: Ignore EAPOL message - "
			   "802.1X not enabled and WPS not used");
		return;
	}

	key_mgmt = wpa_auth_sta_key_mgmt(sta->wpa_sm);
	if (key_mgmt != -1 && wpa_key_mgmt_wpa_psk(key_mgmt)) {
		wpa_printf(MSG_DEBUG, "IEEE 802.1X: Ignore EAPOL message - "
			   "STA is using PSK");
		return;
	}

	if (!sta->eapol_sm) {
		sta->eapol_sm = ieee802_1x_alloc_eapol_sm(hapd, sta);
		if (!sta->eapol_sm)
			return;

#ifdef CONFIG_WPS
		if (!hapd->conf->ieee802_1x &&
		    ((sta->flags & (WLAN_STA_WPS | WLAN_STA_MAYBE_WPS)) ==
		     WLAN_STA_MAYBE_WPS)) {
			/*
			 * Delay EAPOL frame transmission until a possible WPS
			 * STA initiates the handshake with EAPOL-Start.
			 */
			sta->eapol_sm->flags |= EAPOL_SM_WAIT_START;
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
	if (hapd->conf->wps_state && hapd->conf->wpa &&
	    (sta->flags & (WLAN_STA_WPS | WLAN_STA_MAYBE_WPS))) {
		/*
		 * Need to enable IEEE 802.1X/EAPOL state machines for possible
		 * WPS handshake even if IEEE 802.1X/EAPOL is not used for
		 * authentication in this BSS.
		 */
		force_1x = 1;
	}
#endif /* CONFIG_WPS */

	if (!force_1x && !hapd->conf->ieee802_1x) {
		wpa_printf(MSG_DEBUG, "IEEE 802.1X: Ignore STA - "
			   "802.1X not enabled or forced for WPS");
		return;
	}

	key_mgmt = wpa_auth_sta_key_mgmt(sta->wpa_sm);
	if (key_mgmt != -1 && wpa_key_mgmt_wpa_psk(key_mgmt)) {
		wpa_printf(MSG_DEBUG, "IEEE 802.1X: Ignore STA - using PSK");
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
	if (!hapd->conf->ieee802_1x && !(sta->flags & WLAN_STA_WPS)) {
		/*
		 * Delay EAPOL frame transmission until a possible WPS
		 * initiates the handshake with EAPOL-Start.
		 */
		sta->eapol_sm->flags |= EAPOL_SM_WAIT_START;
	}
#endif /* CONFIG_WPS */

	sta->eapol_sm->eap_if->portEnabled = TRUE;

#ifdef CONFIG_IEEE80211R
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
		if (sta->eapol_sm->eap)
			eap_sm_notify_cached(sta->eapol_sm->eap);
		/* TODO: get vlan_id from R0KH using RRB message */
		return;
	}
#endif /* CONFIG_IEEE80211R */

	pmksa = wpa_auth_sta_get_pmksa(sta->wpa_sm);
	if (pmksa) {
		int old_vlanid;

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
		if (sta->eapol_sm->eap)
			eap_sm_notify_cached(sta->eapol_sm->eap);
		old_vlanid = sta->vlan_id;
		pmksa_cache_to_eapol_data(pmksa, sta->eapol_sm);
		if (sta->ssid->dynamic_vlan == DYNAMIC_VLAN_DISABLED)
			sta->vlan_id = 0;
		ap_sta_bind_vlan(hapd, sta, old_vlanid);
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


void ieee802_1x_free_station(struct sta_info *sta)
{
	struct eapol_state_machine *sm = sta->eapol_sm;

	if (sm == NULL)
		return;

	sta->eapol_sm = NULL;

#ifndef CONFIG_NO_RADIUS
	radius_msg_free(sm->last_recv_radius);
	radius_free_class(&sm->radius_class);
#endif /* CONFIG_NO_RADIUS */

	os_free(sm->identity);
	eapol_auth_free(sm);
}


#ifndef CONFIG_NO_RADIUS
static void ieee802_1x_decapsulate_radius(struct hostapd_data *hapd,
					  struct sta_info *sta)
{
	u8 *eap;
	size_t len;
	struct eap_hdr *hdr;
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

	eap = radius_msg_get_eap(msg, &len);
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

	if (len < sizeof(*hdr)) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE8021X,
			       HOSTAPD_LEVEL_WARNING, "too short EAP packet "
			       "received from authentication server");
		os_free(eap);
		sm->eap_if->aaaEapNoReq = TRUE;
		return;
	}

	if (len > sizeof(*hdr))
		eap_type = eap[sizeof(*hdr)];

	hdr = (struct eap_hdr *) eap;
	switch (hdr->code) {
	case EAP_CODE_REQUEST:
		if (eap_type >= 0)
			sm->eap_type_authsrv = eap_type;
		os_snprintf(buf, sizeof(buf), "EAP-Request-%s (%d)",
			    eap_type >= 0 ? eap_server_get_name(0, eap_type) :
			    "??",
			    eap_type);
		break;
	case EAP_CODE_RESPONSE:
		os_snprintf(buf, sizeof(buf), "EAP Response-%s (%d)",
			    eap_type >= 0 ? eap_server_get_name(0, eap_type) :
			    "??",
			    eap_type);
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
	sm->eap_if->aaaEapReqData = wpabuf_alloc_ext_data(eap, len);
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
	u8 *class;
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

	nclass = os_zalloc(count * sizeof(struct radius_attr_data));
	if (nclass == NULL)
		return;

	nclass_count = 0;

	class = NULL;
	for (i = 0; i < count; i++) {
		do {
			if (radius_msg_get_attr_ptr(msg, RADIUS_ATTR_CLASS,
						    &class, &class_len,
						    class) < 0) {
				i = count;
				break;
			}
		} while (class_len < 1);

		nclass[nclass_count].data = os_malloc(class_len);
		if (nclass[nclass_count].data == NULL)
			break;

		os_memcpy(nclass[nclass_count].data, class, class_len);
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

	identity = os_malloc(len + 1);
	if (identity == NULL)
		return;

	os_memcpy(identity, buf, len);
	identity[len] = '\0';

	hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE8021X,
		       HOSTAPD_LEVEL_DEBUG, "old identity '%s' updated with "
		       "User-Name from Access-Accept '%s'",
		       sm->identity ? (char *) sm->identity : "N/A",
		       (char *) identity);

	os_free(sm->identity);
	sm->identity = identity;
	sm->identity_len = len;
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
	int session_timeout_set, old_vlanid = 0;
	struct eapol_state_machine *sm;
	int override_eapReq = 0;
	struct radius_hdr *hdr = radius_msg_get_hdr(msg);

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
		printf("Incoming RADIUS packet did not have correct "
		       "Message-Authenticator - dropped\n");
		return RADIUS_RX_INVALID_AUTHENTICATOR;
	}

	if (hdr->code != RADIUS_CODE_ACCESS_ACCEPT &&
	    hdr->code != RADIUS_CODE_ACCESS_REJECT &&
	    hdr->code != RADIUS_CODE_ACCESS_CHALLENGE) {
		printf("Unknown RADIUS message code\n");
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
		if (sta->ssid->dynamic_vlan == DYNAMIC_VLAN_DISABLED)
			sta->vlan_id = 0;
#ifndef CONFIG_NO_VLAN
		else {
			old_vlanid = sta->vlan_id;
			sta->vlan_id = radius_msg_get_vlanid(msg);
		}
		if (sta->vlan_id > 0 &&
		    hostapd_get_vlan_id_ifname(hapd->conf->vlan,
					       sta->vlan_id)) {
			hostapd_logger(hapd, sta->addr,
				       HOSTAPD_MODULE_RADIUS,
				       HOSTAPD_LEVEL_INFO,
				       "VLAN ID %d", sta->vlan_id);
		} else if (sta->ssid->dynamic_vlan == DYNAMIC_VLAN_REQUIRED) {
			sta->eapol_sm->authFail = TRUE;
			hostapd_logger(hapd, sta->addr,
				       HOSTAPD_MODULE_IEEE8021X,
				       HOSTAPD_LEVEL_INFO, "authentication "
				       "server did not include required VLAN "
				       "ID in Access-Accept");
			break;
		}
#endif /* CONFIG_NO_VLAN */

		if (ap_sta_bind_vlan(hapd, sta, old_vlanid) < 0)
			break;

		/* RFC 3580, Ch. 3.17 */
		if (session_timeout_set && termination_action ==
		    RADIUS_TERMINATION_ACTION_RADIUS_REQUEST) {
			sm->reAuthPeriod = session_timeout;
		} else if (session_timeout_set)
			ap_sta_session_timeout(hapd, sta, session_timeout);

		sm->eap_if->aaaSuccess = TRUE;
		override_eapReq = 1;
		ieee802_1x_get_keys(hapd, sta, msg, req, shared_secret,
				    shared_secret_len);
		ieee802_1x_store_radius_class(hapd, sta, msg);
		ieee802_1x_update_sta_identity(hapd, sta, msg);
		if (sm->eap_if->eapKeyAvailable &&
		    wpa_auth_pmksa_add(sta->wpa_sm, sm->eapol_key_crypt,
				       session_timeout_set ?
				       (int) session_timeout : -1, sm) == 0) {
			hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_WPA,
				       HOSTAPD_LEVEL_DEBUG,
				       "Added PMKSA cache entry");
		}
		break;
	case RADIUS_CODE_ACCESS_REJECT:
		sm->eap_if->aaaFail = TRUE;
		override_eapReq = 1;
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
		printf("Could not generate random WEP key.\n");
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
				 int preauth)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta = sta_ctx;
	if (preauth)
		rsn_preauth_finished(hapd, sta, success);
	else
		ieee802_1x_finished(hapd, sta, success);
}


static int ieee802_1x_get_eap_user(void *ctx, const u8 *identity,
				   size_t identity_len, int phase2,
				   struct eap_user *user)
{
	struct hostapd_data *hapd = ctx;
	const struct hostapd_eap_user *eap_user;
	int i, count;

	eap_user = hostapd_get_eap_user(hapd->conf, identity,
					identity_len, phase2);
	if (eap_user == NULL)
		return -1;

	os_memset(user, 0, sizeof(*user));
	user->phase2 = phase2;
	count = EAP_USER_MAX_METHODS;
	if (count > EAP_MAX_METHODS)
		count = EAP_MAX_METHODS;
	for (i = 0; i < count; i++) {
		user->methods[i].vendor = eap_user->methods[i].vendor;
		user->methods[i].method = eap_user->methods[i].method;
	}

	if (eap_user->password) {
		user->password = os_malloc(eap_user->password_len);
		if (user->password == NULL)
			return -1;
		os_memcpy(user->password, eap_user->password,
			  eap_user->password_len);
		user->password_len = eap_user->password_len;
	}
	user->force_version = eap_user->force_version;
	user->ttls_auth = eap_user->ttls_auth;

	return 0;
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
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta = sta_ctx;
	ieee802_1x_tx_key(hapd, sta);
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


int ieee802_1x_init(struct hostapd_data *hapd)
{
	int i;
	struct eapol_auth_config conf;
	struct eapol_auth_cb cb;

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


void ieee802_1x_deinit(struct hostapd_data *hapd)
{
	eloop_cancel_timeout(ieee802_1x_rekey, hapd, NULL);

	if (hapd->driver != NULL &&
	    (hapd->conf->ieee802_1x || hapd->conf->wpa))
		hostapd_set_drv_ieee8021x(hapd, hapd->conf->iface, 0);

	eapol_auth_deinit(hapd->eapol_auth);
	hapd->eapol_auth = NULL;
}


int ieee802_1x_tx_status(struct hostapd_data *hapd, struct sta_info *sta,
			 const u8 *buf, size_t len, int ack)
{
	struct ieee80211_hdr *hdr;
	struct ieee802_1x_hdr *xhdr;
	struct ieee802_1x_eapol_key *key;
	u8 *pos;
	const unsigned char rfc1042_hdr[ETH_ALEN] =
		{ 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };

	if (sta == NULL)
		return -1;
	if (len < sizeof(*hdr) + sizeof(rfc1042_hdr) + 2 + sizeof(*xhdr))
		return 0;

	hdr = (struct ieee80211_hdr *) buf;
	pos = (u8 *) (hdr + 1);
	if (os_memcmp(pos, rfc1042_hdr, sizeof(rfc1042_hdr)) != 0)
		return 0;
	pos += sizeof(rfc1042_hdr);
	if (WPA_GET_BE16(pos) != ETH_P_PAE)
		return 0;
	pos += 2;

	xhdr = (struct ieee802_1x_hdr *) pos;
	pos += sizeof(*xhdr);

	wpa_printf(MSG_DEBUG, "IEEE 802.1X: " MACSTR " TX status - version=%d "
		   "type=%d length=%d - ack=%d",
		   MAC2STR(sta->addr), xhdr->version, xhdr->type,
		   be_to_host16(xhdr->length), ack);

	if (xhdr->type == IEEE802_1X_TYPE_EAPOL_KEY &&
	    pos + sizeof(struct wpa_eapol_key) <= buf + len) {
		const struct wpa_eapol_key *wpa;
		wpa = (const struct wpa_eapol_key *) pos;
		if (wpa->type == EAPOL_KEY_TYPE_RSN ||
		    wpa->type == EAPOL_KEY_TYPE_WPA)
			wpa_auth_eapol_key_tx_status(hapd->wpa_auth,
						     sta->wpa_sm, ack);
	}

	/* EAPOL EAP-Packet packets are eventually re-sent by either Supplicant
	 * or Authenticator state machines, but EAPOL-Key packets are not
	 * retransmitted in case of failure. Try to re-sent failed EAPOL-Key
	 * packets couple of times because otherwise STA keys become
	 * unsynchronized with AP. */
	if (xhdr->type == IEEE802_1X_TYPE_EAPOL_KEY && !ack &&
	    pos + sizeof(*key) <= buf + len) {
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


const u8 * ieee802_1x_get_key(struct eapol_state_machine *sm, size_t *len)
{
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


static const char * bool_txt(Boolean bool)
{
	return bool ? "TRUE" : "FALSE";
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
	if (ret < 0 || (size_t) ret >= buflen - len)
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
	if (ret < 0 || (size_t) ret >= buflen - len)
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
	if (ret < 0 || (size_t) ret >= buflen - len)
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
	if (ret < 0 || (size_t) ret >= buflen - len)
		return len;
	len += ret;

	/* dot1xAuthSessionStatsTable */
	ret = os_snprintf(buf + len, buflen - len,
			  /* TODO: dot1xAuthSessionOctetsRx */
			  /* TODO: dot1xAuthSessionOctetsTx */
			  /* TODO: dot1xAuthSessionFramesRx */
			  /* TODO: dot1xAuthSessionFramesTx */
			  "dot1xAuthSessionId=%08X-%08X\n"
			  "dot1xAuthSessionAuthenticMethod=%d\n"
			  "dot1xAuthSessionTime=%u\n"
			  "dot1xAuthSessionTerminateCause=999\n"
			  "dot1xAuthSessionUserName=%s\n",
			  sta->acct_session_id_hi, sta->acct_session_id_lo,
			  (wpa_key_mgmt_wpa_ieee8021x(
				   wpa_auth_sta_key_mgmt(sta->wpa_sm))) ?
			  1 : 2,
			  (unsigned int) (time(NULL) -
					  sta->acct_session_start),
			  sm->identity);
	if (ret < 0 || (size_t) ret >= buflen - len)
		return len;
	len += ret;

	return len;
}


static void ieee802_1x_finished(struct hostapd_data *hapd,
				struct sta_info *sta, int success)
{
	const u8 *key;
	size_t len;
	/* TODO: get PMKLifetime from WPA parameters */
	static const int dot11RSNAConfigPMKLifetime = 43200;

	key = ieee802_1x_get_key(sta->eapol_sm, &len);
	if (success && key && len >= PMK_LEN &&
	    wpa_auth_pmksa_add(sta->wpa_sm, key, dot11RSNAConfigPMKLifetime,
			       sta->eapol_sm) == 0) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_WPA,
			       HOSTAPD_LEVEL_DEBUG,
			       "Added PMKSA cache entry (IEEE 802.1X)");
	}

#ifdef CONFIG_WPS
	if (!success && (sta->flags & WLAN_STA_WPS)) {
		/*
		 * Many devices require deauthentication after WPS provisioning
		 * and some may not be be able to do that themselves, so
		 * disconnect the client here.
		 */
		wpa_printf(MSG_DEBUG, "WPS: Force disconnection after "
			   "EAP-Failure");
		/* Add a small sleep to increase likelihood of previously
		 * requested EAP-Failure TX getting out before this should the
		 * driver reorder operations.
		 */
		os_sleep(0, 10000);
		ap_sta_disconnect(hapd, sta, sta->addr,
				  WLAN_REASON_PREV_AUTH_NOT_VALID);
	}
#endif /* CONFIG_WPS */
}
