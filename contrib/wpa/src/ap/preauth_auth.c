/*
 * hostapd - Authenticator for IEEE 802.11i RSN pre-authentication
 * Copyright (c) 2004-2007, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#ifdef CONFIG_RSN_PREAUTH

#include "utils/common.h"
#include "utils/eloop.h"
#include "l2_packet/l2_packet.h"
#include "common/wpa_common.h"
#include "eapol_auth/eapol_auth_sm.h"
#include "eapol_auth/eapol_auth_sm_i.h"
#include "hostapd.h"
#include "ap_config.h"
#include "ieee802_1x.h"
#include "sta_info.h"
#include "wpa_auth.h"
#include "preauth_auth.h"

#ifndef ETH_P_PREAUTH
#define ETH_P_PREAUTH 0x88C7 /* IEEE 802.11i pre-authentication */
#endif /* ETH_P_PREAUTH */

static const int dot11RSNAConfigPMKLifetime = 43200;

struct rsn_preauth_interface {
	struct rsn_preauth_interface *next;
	struct hostapd_data *hapd;
	struct l2_packet_data *l2;
	char *ifname;
	int ifindex;
};


static void rsn_preauth_receive(void *ctx, const u8 *src_addr,
				const u8 *buf, size_t len)
{
	struct rsn_preauth_interface *piface = ctx;
	struct hostapd_data *hapd = piface->hapd;
	struct ieee802_1x_hdr *hdr;
	struct sta_info *sta;
	struct l2_ethhdr *ethhdr;

	wpa_printf(MSG_DEBUG, "RSN: receive pre-auth packet "
		   "from interface '%s'", piface->ifname);
	if (len < sizeof(*ethhdr) + sizeof(*hdr)) {
		wpa_printf(MSG_DEBUG, "RSN: too short pre-auth packet "
			   "(len=%lu)", (unsigned long) len);
		return;
	}

	ethhdr = (struct l2_ethhdr *) buf;
	hdr = (struct ieee802_1x_hdr *) (ethhdr + 1);

	if (os_memcmp(ethhdr->h_dest, hapd->own_addr, ETH_ALEN) != 0) {
		wpa_printf(MSG_DEBUG, "RSN: pre-auth for foreign address "
			   MACSTR, MAC2STR(ethhdr->h_dest));
		return;
	}

	sta = ap_get_sta(hapd, ethhdr->h_source);
	if (sta && (sta->flags & WLAN_STA_ASSOC)) {
		wpa_printf(MSG_DEBUG, "RSN: pre-auth for already association "
			   "STA " MACSTR, MAC2STR(sta->addr));
		return;
	}
	if (!sta && hdr->type == IEEE802_1X_TYPE_EAPOL_START) {
		sta = ap_sta_add(hapd, ethhdr->h_source);
		if (sta == NULL)
			return;
		sta->flags = WLAN_STA_PREAUTH;

		ieee802_1x_new_station(hapd, sta);
		if (sta->eapol_sm == NULL) {
			ap_free_sta(hapd, sta);
			sta = NULL;
		} else {
			sta->eapol_sm->radius_identifier = -1;
			sta->eapol_sm->portValid = TRUE;
			sta->eapol_sm->flags |= EAPOL_SM_PREAUTH;
		}
	}
	if (sta == NULL)
		return;
	sta->preauth_iface = piface;
	ieee802_1x_receive(hapd, ethhdr->h_source, (u8 *) (ethhdr + 1),
			   len - sizeof(*ethhdr));
}


static int rsn_preauth_iface_add(struct hostapd_data *hapd, const char *ifname)
{
	struct rsn_preauth_interface *piface;

	wpa_printf(MSG_DEBUG, "RSN pre-auth interface '%s'", ifname);

	piface = os_zalloc(sizeof(*piface));
	if (piface == NULL)
		return -1;
	piface->hapd = hapd;

	piface->ifname = os_strdup(ifname);
	if (piface->ifname == NULL) {
		goto fail1;
	}

	piface->l2 = l2_packet_init(piface->ifname, NULL, ETH_P_PREAUTH,
				    rsn_preauth_receive, piface, 1);
	if (piface->l2 == NULL) {
		wpa_printf(MSG_ERROR, "Failed to open register layer 2 access "
			   "to ETH_P_PREAUTH");
		goto fail2;
	}

	piface->next = hapd->preauth_iface;
	hapd->preauth_iface = piface;
	return 0;

fail2:
	os_free(piface->ifname);
fail1:
	os_free(piface);
	return -1;
}


void rsn_preauth_iface_deinit(struct hostapd_data *hapd)
{
	struct rsn_preauth_interface *piface, *prev;

	piface = hapd->preauth_iface;
	hapd->preauth_iface = NULL;
	while (piface) {
		prev = piface;
		piface = piface->next;
		l2_packet_deinit(prev->l2);
		os_free(prev->ifname);
		os_free(prev);
	}
}


int rsn_preauth_iface_init(struct hostapd_data *hapd)
{
	char *tmp, *start, *end;

	if (hapd->conf->rsn_preauth_interfaces == NULL)
		return 0;

	tmp = os_strdup(hapd->conf->rsn_preauth_interfaces);
	if (tmp == NULL)
		return -1;
	start = tmp;
	for (;;) {
		while (*start == ' ')
			start++;
		if (*start == '\0')
			break;
		end = os_strchr(start, ' ');
		if (end)
			*end = '\0';

		if (rsn_preauth_iface_add(hapd, start)) {
			rsn_preauth_iface_deinit(hapd);
			os_free(tmp);
			return -1;
		}

		if (end)
			start = end + 1;
		else
			break;
	}
	os_free(tmp);
	return 0;
}


static void rsn_preauth_finished_cb(void *eloop_ctx, void *timeout_ctx)
{
	struct hostapd_data *hapd = eloop_ctx;
	struct sta_info *sta = timeout_ctx;
	wpa_printf(MSG_DEBUG, "RSN: Removing pre-authentication STA entry for "
		   MACSTR, MAC2STR(sta->addr));
	ap_free_sta(hapd, sta);
}


void rsn_preauth_finished(struct hostapd_data *hapd, struct sta_info *sta,
			  int success)
{
	const u8 *key;
	size_t len;
	hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_WPA,
		       HOSTAPD_LEVEL_INFO, "pre-authentication %s",
		       success ? "succeeded" : "failed");

	key = ieee802_1x_get_key(sta->eapol_sm, &len);
	if (len > PMK_LEN)
		len = PMK_LEN;
	if (success && key) {
		if (wpa_auth_pmksa_add_preauth(hapd->wpa_auth, key, len,
					       sta->addr,
					       dot11RSNAConfigPMKLifetime,
					       sta->eapol_sm) == 0) {
			hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_WPA,
				       HOSTAPD_LEVEL_DEBUG,
				       "added PMKSA cache entry (pre-auth)");
		} else {
			hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_WPA,
				       HOSTAPD_LEVEL_DEBUG,
				       "failed to add PMKSA cache entry "
				       "(pre-auth)");
		}
	}

	/*
	 * Finish STA entry removal from timeout in order to avoid freeing
	 * STA data before the caller has finished processing.
	 */
	eloop_register_timeout(0, 0, rsn_preauth_finished_cb, hapd, sta);
}


void rsn_preauth_send(struct hostapd_data *hapd, struct sta_info *sta,
		      u8 *buf, size_t len)
{
	struct rsn_preauth_interface *piface;
	struct l2_ethhdr *ethhdr;

	piface = hapd->preauth_iface;
	while (piface) {
		if (piface == sta->preauth_iface)
			break;
		piface = piface->next;
	}

	if (piface == NULL) {
		wpa_printf(MSG_DEBUG, "RSN: Could not find pre-authentication "
			   "interface for " MACSTR, MAC2STR(sta->addr));
		return;
	}

	ethhdr = os_malloc(sizeof(*ethhdr) + len);
	if (ethhdr == NULL)
		return;

	os_memcpy(ethhdr->h_dest, sta->addr, ETH_ALEN);
	os_memcpy(ethhdr->h_source, hapd->own_addr, ETH_ALEN);
	ethhdr->h_proto = host_to_be16(ETH_P_PREAUTH);
	os_memcpy(ethhdr + 1, buf, len);

	if (l2_packet_send(piface->l2, sta->addr, ETH_P_PREAUTH, (u8 *) ethhdr,
			   sizeof(*ethhdr) + len) < 0) {
		wpa_printf(MSG_ERROR, "Failed to send preauth packet using "
			   "l2_packet_send\n");
	}
	os_free(ethhdr);
}


void rsn_preauth_free_station(struct hostapd_data *hapd, struct sta_info *sta)
{
	eloop_cancel_timeout(rsn_preauth_finished_cb, hapd, sta);
}

#endif /* CONFIG_RSN_PREAUTH */
