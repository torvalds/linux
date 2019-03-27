/*
 * hostapd / WPA authenticator glue code
 * Copyright (c) 2002-2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "utils/list.h"
#include "common/ieee802_11_defs.h"
#include "common/sae.h"
#include "common/wpa_ctrl.h"
#include "crypto/sha1.h"
#include "eapol_auth/eapol_auth_sm.h"
#include "eapol_auth/eapol_auth_sm_i.h"
#include "eap_server/eap.h"
#include "l2_packet/l2_packet.h"
#include "eth_p_oui.h"
#include "hostapd.h"
#include "ieee802_1x.h"
#include "preauth_auth.h"
#include "sta_info.h"
#include "tkip_countermeasures.h"
#include "ap_drv_ops.h"
#include "ap_config.h"
#include "pmksa_cache_auth.h"
#include "wpa_auth.h"
#include "wpa_auth_glue.h"


static void hostapd_wpa_auth_conf(struct hostapd_bss_config *conf,
				  struct hostapd_config *iconf,
				  struct wpa_auth_config *wconf)
{
	os_memset(wconf, 0, sizeof(*wconf));
	wconf->wpa = conf->wpa;
	wconf->wpa_key_mgmt = conf->wpa_key_mgmt;
	wconf->wpa_pairwise = conf->wpa_pairwise;
	wconf->wpa_group = conf->wpa_group;
	wconf->wpa_group_rekey = conf->wpa_group_rekey;
	wconf->wpa_strict_rekey = conf->wpa_strict_rekey;
	wconf->wpa_gmk_rekey = conf->wpa_gmk_rekey;
	wconf->wpa_ptk_rekey = conf->wpa_ptk_rekey;
	wconf->wpa_group_update_count = conf->wpa_group_update_count;
	wconf->wpa_disable_eapol_key_retries =
		conf->wpa_disable_eapol_key_retries;
	wconf->wpa_pairwise_update_count = conf->wpa_pairwise_update_count;
	wconf->rsn_pairwise = conf->rsn_pairwise;
	wconf->rsn_preauth = conf->rsn_preauth;
	wconf->eapol_version = conf->eapol_version;
	wconf->wmm_enabled = conf->wmm_enabled;
	wconf->wmm_uapsd = conf->wmm_uapsd;
	wconf->disable_pmksa_caching = conf->disable_pmksa_caching;
	wconf->okc = conf->okc;
#ifdef CONFIG_IEEE80211W
	wconf->ieee80211w = conf->ieee80211w;
	wconf->group_mgmt_cipher = conf->group_mgmt_cipher;
	wconf->sae_require_mfp = conf->sae_require_mfp;
#endif /* CONFIG_IEEE80211W */
#ifdef CONFIG_IEEE80211R_AP
	wconf->ssid_len = conf->ssid.ssid_len;
	if (wconf->ssid_len > SSID_MAX_LEN)
		wconf->ssid_len = SSID_MAX_LEN;
	os_memcpy(wconf->ssid, conf->ssid.ssid, wconf->ssid_len);
	os_memcpy(wconf->mobility_domain, conf->mobility_domain,
		  MOBILITY_DOMAIN_ID_LEN);
	if (conf->nas_identifier &&
	    os_strlen(conf->nas_identifier) <= FT_R0KH_ID_MAX_LEN) {
		wconf->r0_key_holder_len = os_strlen(conf->nas_identifier);
		os_memcpy(wconf->r0_key_holder, conf->nas_identifier,
			  wconf->r0_key_holder_len);
	}
	os_memcpy(wconf->r1_key_holder, conf->r1_key_holder, FT_R1KH_ID_LEN);
	wconf->r0_key_lifetime = conf->r0_key_lifetime;
	wconf->r1_max_key_lifetime = conf->r1_max_key_lifetime;
	wconf->reassociation_deadline = conf->reassociation_deadline;
	wconf->rkh_pos_timeout = conf->rkh_pos_timeout;
	wconf->rkh_neg_timeout = conf->rkh_neg_timeout;
	wconf->rkh_pull_timeout = conf->rkh_pull_timeout;
	wconf->rkh_pull_retries = conf->rkh_pull_retries;
	wconf->r0kh_list = &conf->r0kh_list;
	wconf->r1kh_list = &conf->r1kh_list;
	wconf->pmk_r1_push = conf->pmk_r1_push;
	wconf->ft_over_ds = conf->ft_over_ds;
	wconf->ft_psk_generate_local = conf->ft_psk_generate_local;
#endif /* CONFIG_IEEE80211R_AP */
#ifdef CONFIG_HS20
	wconf->disable_gtk = conf->disable_dgaf;
	if (conf->osen) {
		wconf->disable_gtk = 1;
		wconf->wpa = WPA_PROTO_OSEN;
		wconf->wpa_key_mgmt = WPA_KEY_MGMT_OSEN;
		wconf->wpa_pairwise = 0;
		wconf->wpa_group = WPA_CIPHER_CCMP;
		wconf->rsn_pairwise = WPA_CIPHER_CCMP;
		wconf->rsn_preauth = 0;
		wconf->disable_pmksa_caching = 1;
#ifdef CONFIG_IEEE80211W
		wconf->ieee80211w = 1;
#endif /* CONFIG_IEEE80211W */
	}
#endif /* CONFIG_HS20 */
#ifdef CONFIG_TESTING_OPTIONS
	wconf->corrupt_gtk_rekey_mic_probability =
		iconf->corrupt_gtk_rekey_mic_probability;
	if (conf->own_ie_override &&
	    wpabuf_len(conf->own_ie_override) <= MAX_OWN_IE_OVERRIDE) {
		wconf->own_ie_override_len = wpabuf_len(conf->own_ie_override);
		os_memcpy(wconf->own_ie_override,
			  wpabuf_head(conf->own_ie_override),
			  wconf->own_ie_override_len);
	}
#endif /* CONFIG_TESTING_OPTIONS */
#ifdef CONFIG_P2P
	os_memcpy(wconf->ip_addr_go, conf->ip_addr_go, 4);
	os_memcpy(wconf->ip_addr_mask, conf->ip_addr_mask, 4);
	os_memcpy(wconf->ip_addr_start, conf->ip_addr_start, 4);
	os_memcpy(wconf->ip_addr_end, conf->ip_addr_end, 4);
#endif /* CONFIG_P2P */
#ifdef CONFIG_FILS
	wconf->fils_cache_id_set = conf->fils_cache_id_set;
	os_memcpy(wconf->fils_cache_id, conf->fils_cache_id,
		  FILS_CACHE_ID_LEN);
#endif /* CONFIG_FILS */
}


static void hostapd_wpa_auth_logger(void *ctx, const u8 *addr,
				    logger_level level, const char *txt)
{
#ifndef CONFIG_NO_HOSTAPD_LOGGER
	struct hostapd_data *hapd = ctx;
	int hlevel;

	switch (level) {
	case LOGGER_WARNING:
		hlevel = HOSTAPD_LEVEL_WARNING;
		break;
	case LOGGER_INFO:
		hlevel = HOSTAPD_LEVEL_INFO;
		break;
	case LOGGER_DEBUG:
	default:
		hlevel = HOSTAPD_LEVEL_DEBUG;
		break;
	}

	hostapd_logger(hapd, addr, HOSTAPD_MODULE_WPA, hlevel, "%s", txt);
#endif /* CONFIG_NO_HOSTAPD_LOGGER */
}


static void hostapd_wpa_auth_disconnect(void *ctx, const u8 *addr,
					u16 reason)
{
	struct hostapd_data *hapd = ctx;
	wpa_printf(MSG_DEBUG, "%s: WPA authenticator requests disconnect: "
		   "STA " MACSTR " reason %d",
		   __func__, MAC2STR(addr), reason);
	ap_sta_disconnect(hapd, NULL, addr, reason);
}


static int hostapd_wpa_auth_mic_failure_report(void *ctx, const u8 *addr)
{
	struct hostapd_data *hapd = ctx;
	return michael_mic_failure(hapd, addr, 0);
}


static void hostapd_wpa_auth_psk_failure_report(void *ctx, const u8 *addr)
{
	struct hostapd_data *hapd = ctx;
	wpa_msg(hapd->msg_ctx, MSG_INFO, AP_STA_POSSIBLE_PSK_MISMATCH MACSTR,
		MAC2STR(addr));
}


static void hostapd_wpa_auth_set_eapol(void *ctx, const u8 *addr,
				       wpa_eapol_variable var, int value)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta = ap_get_sta(hapd, addr);
	if (sta == NULL)
		return;
	switch (var) {
	case WPA_EAPOL_portEnabled:
		ieee802_1x_notify_port_enabled(sta->eapol_sm, value);
		break;
	case WPA_EAPOL_portValid:
		ieee802_1x_notify_port_valid(sta->eapol_sm, value);
		break;
	case WPA_EAPOL_authorized:
		ieee802_1x_set_sta_authorized(hapd, sta, value);
		break;
	case WPA_EAPOL_portControl_Auto:
		if (sta->eapol_sm)
			sta->eapol_sm->portControl = Auto;
		break;
	case WPA_EAPOL_keyRun:
		if (sta->eapol_sm)
			sta->eapol_sm->keyRun = value ? TRUE : FALSE;
		break;
	case WPA_EAPOL_keyAvailable:
		if (sta->eapol_sm)
			sta->eapol_sm->eap_if->eapKeyAvailable =
				value ? TRUE : FALSE;
		break;
	case WPA_EAPOL_keyDone:
		if (sta->eapol_sm)
			sta->eapol_sm->keyDone = value ? TRUE : FALSE;
		break;
	case WPA_EAPOL_inc_EapolFramesTx:
		if (sta->eapol_sm)
			sta->eapol_sm->dot1xAuthEapolFramesTx++;
		break;
	}
}


static int hostapd_wpa_auth_get_eapol(void *ctx, const u8 *addr,
				      wpa_eapol_variable var)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta = ap_get_sta(hapd, addr);
	if (sta == NULL || sta->eapol_sm == NULL)
		return -1;
	switch (var) {
	case WPA_EAPOL_keyRun:
		return sta->eapol_sm->keyRun;
	case WPA_EAPOL_keyAvailable:
		return sta->eapol_sm->eap_if->eapKeyAvailable;
	default:
		return -1;
	}
}


static const u8 * hostapd_wpa_auth_get_psk(void *ctx, const u8 *addr,
					   const u8 *p2p_dev_addr,
					   const u8 *prev_psk, size_t *psk_len)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta = ap_get_sta(hapd, addr);
	const u8 *psk;

	if (psk_len)
		*psk_len = PMK_LEN;

#ifdef CONFIG_SAE
	if (sta && sta->auth_alg == WLAN_AUTH_SAE) {
		if (!sta->sae || prev_psk)
			return NULL;
		return sta->sae->pmk;
	}
	if (sta && wpa_auth_uses_sae(sta->wpa_sm)) {
		wpa_printf(MSG_DEBUG,
			   "No PSK for STA trying to use SAE with PMKSA caching");
		return NULL;
	}
#endif /* CONFIG_SAE */

#ifdef CONFIG_OWE
	if ((hapd->conf->wpa_key_mgmt & WPA_KEY_MGMT_OWE) &&
	    sta && sta->owe_pmk) {
		if (psk_len)
			*psk_len = sta->owe_pmk_len;
		return sta->owe_pmk;
	}
	if ((hapd->conf->wpa_key_mgmt & WPA_KEY_MGMT_OWE) && sta) {
		struct rsn_pmksa_cache_entry *sa;

		sa = wpa_auth_sta_get_pmksa(sta->wpa_sm);
		if (sa && sa->akmp == WPA_KEY_MGMT_OWE) {
			if (psk_len)
				*psk_len = sa->pmk_len;
			return sa->pmk;
		}
	}
#endif /* CONFIG_OWE */

	psk = hostapd_get_psk(hapd->conf, addr, p2p_dev_addr, prev_psk);
	/*
	 * This is about to iterate over all psks, prev_psk gives the last
	 * returned psk which should not be returned again.
	 * logic list (all hostapd_get_psk; all sta->psk)
	 */
	if (sta && sta->psk && !psk) {
		struct hostapd_sta_wpa_psk_short *pos;
		psk = sta->psk->psk;
		for (pos = sta->psk; pos; pos = pos->next) {
			if (pos->is_passphrase) {
				pbkdf2_sha1(pos->passphrase,
					    hapd->conf->ssid.ssid,
					    hapd->conf->ssid.ssid_len, 4096,
					    pos->psk, PMK_LEN);
				pos->is_passphrase = 0;
			}
			if (pos->psk == prev_psk) {
				psk = pos->next ? pos->next->psk : NULL;
				break;
			}
		}
	}
	return psk;
}


static int hostapd_wpa_auth_get_msk(void *ctx, const u8 *addr, u8 *msk,
				    size_t *len)
{
	struct hostapd_data *hapd = ctx;
	const u8 *key;
	size_t keylen;
	struct sta_info *sta;

	sta = ap_get_sta(hapd, addr);
	if (sta == NULL) {
		wpa_printf(MSG_DEBUG, "AUTH_GET_MSK: Cannot find STA");
		return -1;
	}

	key = ieee802_1x_get_key(sta->eapol_sm, &keylen);
	if (key == NULL) {
		wpa_printf(MSG_DEBUG, "AUTH_GET_MSK: Key is null, eapol_sm: %p",
			   sta->eapol_sm);
		return -1;
	}

	if (keylen > *len)
		keylen = *len;
	os_memcpy(msk, key, keylen);
	*len = keylen;

	return 0;
}


static int hostapd_wpa_auth_set_key(void *ctx, int vlan_id, enum wpa_alg alg,
				    const u8 *addr, int idx, u8 *key,
				    size_t key_len)
{
	struct hostapd_data *hapd = ctx;
	const char *ifname = hapd->conf->iface;

	if (vlan_id > 0) {
		ifname = hostapd_get_vlan_id_ifname(hapd->conf->vlan, vlan_id);
		if (ifname == NULL)
			return -1;
	}

#ifdef CONFIG_TESTING_OPTIONS
	if (addr && !is_broadcast_ether_addr(addr)) {
		struct sta_info *sta;

		sta = ap_get_sta(hapd, addr);
		if (sta) {
			sta->last_tk_alg = alg;
			sta->last_tk_key_idx = idx;
			if (key)
				os_memcpy(sta->last_tk, key, key_len);
			sta->last_tk_len = key_len;
		}
#ifdef CONFIG_IEEE80211W
	} else if (alg == WPA_ALG_IGTK ||
		   alg == WPA_ALG_BIP_GMAC_128 ||
		   alg == WPA_ALG_BIP_GMAC_256 ||
		   alg == WPA_ALG_BIP_CMAC_256) {
		hapd->last_igtk_alg = alg;
		hapd->last_igtk_key_idx = idx;
		if (key)
			os_memcpy(hapd->last_igtk, key, key_len);
		hapd->last_igtk_len = key_len;
#endif /* CONFIG_IEEE80211W */
	} else {
		hapd->last_gtk_alg = alg;
		hapd->last_gtk_key_idx = idx;
		if (key)
			os_memcpy(hapd->last_gtk, key, key_len);
		hapd->last_gtk_len = key_len;
	}
#endif /* CONFIG_TESTING_OPTIONS */
	return hostapd_drv_set_key(ifname, hapd, alg, addr, idx, 1, NULL, 0,
				   key, key_len);
}


static int hostapd_wpa_auth_get_seqnum(void *ctx, const u8 *addr, int idx,
				       u8 *seq)
{
	struct hostapd_data *hapd = ctx;
	return hostapd_get_seqnum(hapd->conf->iface, hapd, addr, idx, seq);
}


static int hostapd_wpa_auth_send_eapol(void *ctx, const u8 *addr,
				       const u8 *data, size_t data_len,
				       int encrypt)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;
	u32 flags = 0;

#ifdef CONFIG_TESTING_OPTIONS
	if (hapd->ext_eapol_frame_io) {
		size_t hex_len = 2 * data_len + 1;
		char *hex = os_malloc(hex_len);

		if (hex == NULL)
			return -1;
		wpa_snprintf_hex(hex, hex_len, data, data_len);
		wpa_msg(hapd->msg_ctx, MSG_INFO, "EAPOL-TX " MACSTR " %s",
			MAC2STR(addr), hex);
		os_free(hex);
		return 0;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	sta = ap_get_sta(hapd, addr);
	if (sta)
		flags = hostapd_sta_flags_to_drv(sta->flags);

	return hostapd_drv_hapd_send_eapol(hapd, addr, data, data_len,
					   encrypt, flags);
}


static int hostapd_wpa_auth_for_each_sta(
	void *ctx, int (*cb)(struct wpa_state_machine *sm, void *ctx),
	void *cb_ctx)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;

	for (sta = hapd->sta_list; sta; sta = sta->next) {
		if (sta->wpa_sm && cb(sta->wpa_sm, cb_ctx))
			return 1;
	}
	return 0;
}


struct wpa_auth_iface_iter_data {
	int (*cb)(struct wpa_authenticator *sm, void *ctx);
	void *cb_ctx;
};

static int wpa_auth_iface_iter(struct hostapd_iface *iface, void *ctx)
{
	struct wpa_auth_iface_iter_data *data = ctx;
	size_t i;
	for (i = 0; i < iface->num_bss; i++) {
		if (iface->bss[i]->wpa_auth &&
		    data->cb(iface->bss[i]->wpa_auth, data->cb_ctx))
			return 1;
	}
	return 0;
}


static int hostapd_wpa_auth_for_each_auth(
	void *ctx, int (*cb)(struct wpa_authenticator *sm, void *ctx),
	void *cb_ctx)
{
	struct hostapd_data *hapd = ctx;
	struct wpa_auth_iface_iter_data data;
	if (hapd->iface->interfaces == NULL ||
	    hapd->iface->interfaces->for_each_interface == NULL)
		return -1;
	data.cb = cb;
	data.cb_ctx = cb_ctx;
	return hapd->iface->interfaces->for_each_interface(
		hapd->iface->interfaces, wpa_auth_iface_iter, &data);
}


#ifdef CONFIG_IEEE80211R_AP

struct wpa_ft_rrb_rx_later_data {
	struct dl_list list;
	u8 addr[ETH_ALEN];
	size_t data_len;
	/* followed by data_len octets of data */
};

static void hostapd_wpa_ft_rrb_rx_later(void *eloop_ctx, void *timeout_ctx)
{
	struct hostapd_data *hapd = eloop_ctx;
	struct wpa_ft_rrb_rx_later_data *data, *n;

	dl_list_for_each_safe(data, n, &hapd->l2_queue,
			      struct wpa_ft_rrb_rx_later_data, list) {
		if (hapd->wpa_auth) {
			wpa_ft_rrb_rx(hapd->wpa_auth, data->addr,
				      (const u8 *) (data + 1),
				      data->data_len);
		}
		dl_list_del(&data->list);
		os_free(data);
	}
}


struct wpa_auth_ft_iface_iter_data {
	struct hostapd_data *src_hapd;
	const u8 *dst;
	const u8 *data;
	size_t data_len;
};


static int hostapd_wpa_auth_ft_iter(struct hostapd_iface *iface, void *ctx)
{
	struct wpa_auth_ft_iface_iter_data *idata = ctx;
	struct wpa_ft_rrb_rx_later_data *data;
	struct hostapd_data *hapd;
	size_t j;

	for (j = 0; j < iface->num_bss; j++) {
		hapd = iface->bss[j];
		if (hapd == idata->src_hapd ||
		    !hapd->wpa_auth ||
		    os_memcmp(hapd->own_addr, idata->dst, ETH_ALEN) != 0)
			continue;

		wpa_printf(MSG_DEBUG,
			   "FT: Send RRB data directly to locally managed BSS "
			   MACSTR "@%s -> " MACSTR "@%s",
			   MAC2STR(idata->src_hapd->own_addr),
			   idata->src_hapd->conf->iface,
			   MAC2STR(hapd->own_addr), hapd->conf->iface);

		/* Defer wpa_ft_rrb_rx() until next eloop step as this is
		 * when it would be triggered when reading from a socket.
		 * This avoids
		 * hapd0:send -> hapd1:recv -> hapd1:send -> hapd0:recv,
		 * that is calling hapd0:recv handler from within
		 * hapd0:send directly.
		 */
		data = os_zalloc(sizeof(*data) + idata->data_len);
		if (!data)
			return 1;

		os_memcpy(data->addr, idata->src_hapd->own_addr, ETH_ALEN);
		os_memcpy(data + 1, idata->data, idata->data_len);
		data->data_len = idata->data_len;

		dl_list_add(&hapd->l2_queue, &data->list);

		if (!eloop_is_timeout_registered(hostapd_wpa_ft_rrb_rx_later,
						 hapd, NULL))
			eloop_register_timeout(0, 0,
					       hostapd_wpa_ft_rrb_rx_later,
					       hapd, NULL);

		return 1;
	}

	return 0;
}

#endif /* CONFIG_IEEE80211R_AP */


static int hostapd_wpa_auth_send_ether(void *ctx, const u8 *dst, u16 proto,
				       const u8 *data, size_t data_len)
{
	struct hostapd_data *hapd = ctx;
	struct l2_ethhdr *buf;
	int ret;

#ifdef CONFIG_TESTING_OPTIONS
	if (hapd->ext_eapol_frame_io && proto == ETH_P_EAPOL) {
		size_t hex_len = 2 * data_len + 1;
		char *hex = os_malloc(hex_len);

		if (hex == NULL)
			return -1;
		wpa_snprintf_hex(hex, hex_len, data, data_len);
		wpa_msg(hapd->msg_ctx, MSG_INFO, "EAPOL-TX " MACSTR " %s",
			MAC2STR(dst), hex);
		os_free(hex);
		return 0;
	}
#endif /* CONFIG_TESTING_OPTIONS */

#ifdef CONFIG_IEEE80211R_AP
	if (proto == ETH_P_RRB && hapd->iface->interfaces &&
	    hapd->iface->interfaces->for_each_interface) {
		int res;
		struct wpa_auth_ft_iface_iter_data idata;
		idata.src_hapd = hapd;
		idata.dst = dst;
		idata.data = data;
		idata.data_len = data_len;
		res = hapd->iface->interfaces->for_each_interface(
			hapd->iface->interfaces, hostapd_wpa_auth_ft_iter,
			&idata);
		if (res == 1)
			return data_len;
	}
#endif /* CONFIG_IEEE80211R_AP */

	if (hapd->driver && hapd->driver->send_ether)
		return hapd->driver->send_ether(hapd->drv_priv, dst,
						hapd->own_addr, proto,
						data, data_len);
	if (hapd->l2 == NULL)
		return -1;

	buf = os_malloc(sizeof(*buf) + data_len);
	if (buf == NULL)
		return -1;
	os_memcpy(buf->h_dest, dst, ETH_ALEN);
	os_memcpy(buf->h_source, hapd->own_addr, ETH_ALEN);
	buf->h_proto = host_to_be16(proto);
	os_memcpy(buf + 1, data, data_len);
	ret = l2_packet_send(hapd->l2, dst, proto, (u8 *) buf,
			     sizeof(*buf) + data_len);
	os_free(buf);
	return ret;
}


#ifdef CONFIG_ETH_P_OUI
static struct eth_p_oui_ctx * hostapd_wpa_get_oui(struct hostapd_data *hapd,
						  u8 oui_suffix)
{
	switch (oui_suffix) {
#ifdef CONFIG_IEEE80211R_AP
	case FT_PACKET_R0KH_R1KH_PULL:
		return hapd->oui_pull;
	case FT_PACKET_R0KH_R1KH_RESP:
		return hapd->oui_resp;
	case FT_PACKET_R0KH_R1KH_PUSH:
		return hapd->oui_push;
	case FT_PACKET_R0KH_R1KH_SEQ_REQ:
		return hapd->oui_sreq;
	case FT_PACKET_R0KH_R1KH_SEQ_RESP:
		return hapd->oui_sresp;
#endif /* CONFIG_IEEE80211R_AP */
	default:
		return NULL;
	}
}
#endif /* CONFIG_ETH_P_OUI */


#ifdef CONFIG_IEEE80211R_AP

struct oui_deliver_later_data {
	struct dl_list list;
	u8 src_addr[ETH_ALEN];
	u8 dst_addr[ETH_ALEN];
	size_t data_len;
	u8 oui_suffix;
	/* followed by data_len octets of data */
};

static void hostapd_oui_deliver_later(void *eloop_ctx, void *timeout_ctx)
{
	struct hostapd_data *hapd = eloop_ctx;
	struct oui_deliver_later_data *data, *n;
	struct eth_p_oui_ctx *oui_ctx;

	dl_list_for_each_safe(data, n, &hapd->l2_oui_queue,
			      struct oui_deliver_later_data, list) {
		oui_ctx = hostapd_wpa_get_oui(hapd, data->oui_suffix);
		if (hapd->wpa_auth && oui_ctx) {
			eth_p_oui_deliver(oui_ctx, data->src_addr,
					  data->dst_addr,
					  (const u8 *) (data + 1),
					  data->data_len);
		}
		dl_list_del(&data->list);
		os_free(data);
	}
}


struct wpa_auth_oui_iface_iter_data {
	struct hostapd_data *src_hapd;
	const u8 *dst_addr;
	const u8 *data;
	size_t data_len;
	u8 oui_suffix;
};

static int hostapd_wpa_auth_oui_iter(struct hostapd_iface *iface, void *ctx)
{
	struct wpa_auth_oui_iface_iter_data *idata = ctx;
	struct oui_deliver_later_data *data;
	struct hostapd_data *hapd;
	size_t j;

	for (j = 0; j < iface->num_bss; j++) {
		hapd = iface->bss[j];
		if (hapd == idata->src_hapd)
			continue;
		if (!is_multicast_ether_addr(idata->dst_addr) &&
		    os_memcmp(hapd->own_addr, idata->dst_addr, ETH_ALEN) != 0)
			continue;

		/* defer eth_p_oui_deliver until next eloop step as this is
		 * when it would be triggerd from reading from sock
		 * This avoids
		 * hapd0:send -> hapd1:recv -> hapd1:send -> hapd0:recv,
		 * that is calling hapd0:recv handler from within
		 * hapd0:send directly.
		 */
		data = os_zalloc(sizeof(*data) + idata->data_len);
		if (!data)
			return 1;

		os_memcpy(data->src_addr, idata->src_hapd->own_addr, ETH_ALEN);
		os_memcpy(data->dst_addr, idata->dst_addr, ETH_ALEN);
		os_memcpy(data + 1, idata->data, idata->data_len);
		data->data_len = idata->data_len;
		data->oui_suffix = idata->oui_suffix;

		dl_list_add(&hapd->l2_oui_queue, &data->list);

		if (!eloop_is_timeout_registered(hostapd_oui_deliver_later,
						 hapd, NULL))
			eloop_register_timeout(0, 0,
					       hostapd_oui_deliver_later,
					       hapd, NULL);

		return 1;
	}

	return 0;
}

#endif /* CONFIG_IEEE80211R_AP */


static int hostapd_wpa_auth_send_oui(void *ctx, const u8 *dst, u8 oui_suffix,
				     const u8 *data, size_t data_len)
{
#ifdef CONFIG_ETH_P_OUI
	struct hostapd_data *hapd = ctx;
	struct eth_p_oui_ctx *oui_ctx;

#ifdef CONFIG_IEEE80211R_AP
	if (hapd->iface->interfaces &&
	    hapd->iface->interfaces->for_each_interface) {
		struct wpa_auth_oui_iface_iter_data idata;
		int res;

		idata.src_hapd = hapd;
		idata.dst_addr = dst;
		idata.data = data;
		idata.data_len = data_len;
		idata.oui_suffix = oui_suffix;
		res = hapd->iface->interfaces->for_each_interface(
			hapd->iface->interfaces, hostapd_wpa_auth_oui_iter,
			&idata);
		if (res == 1)
			return data_len;
	}
#endif /* CONFIG_IEEE80211R_AP */

	oui_ctx = hostapd_wpa_get_oui(hapd, oui_suffix);
	if (!oui_ctx)
		return -1;

	return eth_p_oui_send(oui_ctx, hapd->own_addr, dst, data, data_len);
#else /* CONFIG_ETH_P_OUI */
	return -1;
#endif /* CONFIG_ETH_P_OUI */
}


#ifdef CONFIG_IEEE80211R_AP

static int hostapd_wpa_auth_send_ft_action(void *ctx, const u8 *dst,
					   const u8 *data, size_t data_len)
{
	struct hostapd_data *hapd = ctx;
	int res;
	struct ieee80211_mgmt *m;
	size_t mlen;
	struct sta_info *sta;

	sta = ap_get_sta(hapd, dst);
	if (sta == NULL || sta->wpa_sm == NULL)
		return -1;

	m = os_zalloc(sizeof(*m) + data_len);
	if (m == NULL)
		return -1;
	mlen = ((u8 *) &m->u - (u8 *) m) + data_len;
	m->frame_control = IEEE80211_FC(WLAN_FC_TYPE_MGMT,
					WLAN_FC_STYPE_ACTION);
	os_memcpy(m->da, dst, ETH_ALEN);
	os_memcpy(m->sa, hapd->own_addr, ETH_ALEN);
	os_memcpy(m->bssid, hapd->own_addr, ETH_ALEN);
	os_memcpy(&m->u, data, data_len);

	res = hostapd_drv_send_mlme(hapd, (u8 *) m, mlen, 0);
	os_free(m);
	return res;
}


static struct wpa_state_machine *
hostapd_wpa_auth_add_sta(void *ctx, const u8 *sta_addr)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;

	if (hostapd_add_sta_node(hapd, sta_addr, WLAN_AUTH_FT) < 0)
		return NULL;

	sta = ap_sta_add(hapd, sta_addr);
	if (sta == NULL)
		return NULL;
	if (sta->wpa_sm) {
		sta->auth_alg = WLAN_AUTH_FT;
		return sta->wpa_sm;
	}

	sta->wpa_sm = wpa_auth_sta_init(hapd->wpa_auth, sta->addr, NULL);
	if (sta->wpa_sm == NULL) {
		ap_free_sta(hapd, sta);
		return NULL;
	}
	sta->auth_alg = WLAN_AUTH_FT;

	return sta->wpa_sm;
}


static int hostapd_wpa_auth_set_vlan(void *ctx, const u8 *sta_addr,
				     struct vlan_description *vlan)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;

	sta = ap_get_sta(hapd, sta_addr);
	if (!sta || !sta->wpa_sm)
		return -1;

	if (vlan->notempty &&
	    !hostapd_vlan_valid(hapd->conf->vlan, vlan)) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_INFO,
			       "Invalid VLAN %d%s received from FT",
			       vlan->untagged, vlan->tagged[0] ? "+" : "");
		return -1;
	}

	if (ap_sta_set_vlan(hapd, sta, vlan) < 0)
		return -1;
	/* Configure wpa_group for GTK but ignore error due to driver not
	 * knowing this STA. */
	ap_sta_bind_vlan(hapd, sta);

	if (sta->vlan_id)
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_INFO, "VLAN ID %d", sta->vlan_id);

	return 0;
}


static int hostapd_wpa_auth_get_vlan(void *ctx, const u8 *sta_addr,
				     struct vlan_description *vlan)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;

	sta = ap_get_sta(hapd, sta_addr);
	if (!sta)
		return -1;

	if (sta->vlan_desc)
		*vlan = *sta->vlan_desc;
	else
		os_memset(vlan, 0, sizeof(*vlan));

	return 0;
}


static int
hostapd_wpa_auth_set_identity(void *ctx, const u8 *sta_addr,
			      const u8 *identity, size_t identity_len)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;

	sta = ap_get_sta(hapd, sta_addr);
	if (!sta)
		return -1;

	os_free(sta->identity);
	sta->identity = NULL;

	if (sta->eapol_sm) {
		os_free(sta->eapol_sm->identity);
		sta->eapol_sm->identity = NULL;
		sta->eapol_sm->identity_len = 0;
	}

	if (!identity_len)
		return 0;

	/* sta->identity is NULL terminated */
	sta->identity = os_zalloc(identity_len + 1);
	if (!sta->identity)
		return -1;
	os_memcpy(sta->identity, identity, identity_len);

	if (sta->eapol_sm) {
		sta->eapol_sm->identity = os_zalloc(identity_len);
		if (!sta->eapol_sm->identity)
			return -1;
		os_memcpy(sta->eapol_sm->identity, identity, identity_len);
		sta->eapol_sm->identity_len = identity_len;
	}

	return 0;
}


static size_t
hostapd_wpa_auth_get_identity(void *ctx, const u8 *sta_addr, const u8 **buf)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;
	size_t len;
	char *identity;

	sta = ap_get_sta(hapd, sta_addr);
	if (!sta)
		return 0;

	*buf = ieee802_1x_get_identity(sta->eapol_sm, &len);
	if (*buf && len)
		return len;

	if (!sta->identity) {
		*buf = NULL;
		return 0;
	}

	identity = sta->identity;
	len = os_strlen(identity);
	*buf = (u8 *) identity;

	return len;
}


static int
hostapd_wpa_auth_set_radius_cui(void *ctx, const u8 *sta_addr,
				const u8 *radius_cui, size_t radius_cui_len)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;

	sta = ap_get_sta(hapd, sta_addr);
	if (!sta)
		return -1;

	os_free(sta->radius_cui);
	sta->radius_cui = NULL;

	if (sta->eapol_sm) {
		wpabuf_free(sta->eapol_sm->radius_cui);
		sta->eapol_sm->radius_cui = NULL;
	}

	if (!radius_cui)
		return 0;

	/* sta->radius_cui is NULL terminated */
	sta->radius_cui = os_zalloc(radius_cui_len + 1);
	if (!sta->radius_cui)
		return -1;
	os_memcpy(sta->radius_cui, radius_cui, radius_cui_len);

	if (sta->eapol_sm) {
		sta->eapol_sm->radius_cui = wpabuf_alloc_copy(radius_cui,
							      radius_cui_len);
		if (!sta->eapol_sm->radius_cui)
			return -1;
	}

	return 0;
}


static size_t
hostapd_wpa_auth_get_radius_cui(void *ctx, const u8 *sta_addr, const u8 **buf)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;
	struct wpabuf *b;
	size_t len;
	char *radius_cui;

	sta = ap_get_sta(hapd, sta_addr);
	if (!sta)
		return 0;

	b = ieee802_1x_get_radius_cui(sta->eapol_sm);
	if (b) {
		len = wpabuf_len(b);
		*buf = wpabuf_head(b);
		return len;
	}

	if (!sta->radius_cui) {
		*buf = NULL;
		return 0;
	}

	radius_cui = sta->radius_cui;
	len = os_strlen(radius_cui);
	*buf = (u8 *) radius_cui;

	return len;
}


static void hostapd_wpa_auth_set_session_timeout(void *ctx, const u8 *sta_addr,
						 int session_timeout)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;

	sta = ap_get_sta(hapd, sta_addr);
	if (!sta)
		return;

	if (session_timeout) {
		os_get_reltime(&sta->session_timeout);
		sta->session_timeout.sec += session_timeout;
		sta->session_timeout_set = 1;
		ap_sta_session_timeout(hapd, sta, session_timeout);
	} else {
		sta->session_timeout_set = 0;
		ap_sta_no_session_timeout(hapd, sta);
	}
}


static int hostapd_wpa_auth_get_session_timeout(void *ctx, const u8 *sta_addr)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;
	struct os_reltime now, remaining;

	sta = ap_get_sta(hapd, sta_addr);
	if (!sta || !sta->session_timeout_set)
		return 0;

	os_get_reltime(&now);
	if (os_reltime_before(&sta->session_timeout, &now)) {
		/* already expired, return >0 as timeout was set */
		return 1;
	}

	os_reltime_sub(&sta->session_timeout, &now, &remaining);

	return (remaining.sec > 0) ? remaining.sec : 1;
}


static void hostapd_rrb_receive(void *ctx, const u8 *src_addr, const u8 *buf,
				size_t len)
{
	struct hostapd_data *hapd = ctx;
	struct l2_ethhdr *ethhdr;
	if (len < sizeof(*ethhdr))
		return;
	ethhdr = (struct l2_ethhdr *) buf;
	wpa_printf(MSG_DEBUG, "FT: RRB received packet " MACSTR " -> "
		   MACSTR, MAC2STR(ethhdr->h_source), MAC2STR(ethhdr->h_dest));
	if (!is_multicast_ether_addr(ethhdr->h_dest) &&
	    os_memcmp(hapd->own_addr, ethhdr->h_dest, ETH_ALEN) != 0)
		return;
	wpa_ft_rrb_rx(hapd->wpa_auth, ethhdr->h_source, buf + sizeof(*ethhdr),
		      len - sizeof(*ethhdr));
}


static void hostapd_rrb_oui_receive(void *ctx, const u8 *src_addr,
				    const u8 *dst_addr, u8 oui_suffix,
				    const u8 *buf, size_t len)
{
	struct hostapd_data *hapd = ctx;

	wpa_printf(MSG_DEBUG, "FT: RRB received packet " MACSTR " -> "
		   MACSTR, MAC2STR(src_addr), MAC2STR(dst_addr));
	if (!is_multicast_ether_addr(dst_addr) &&
	    os_memcmp(hapd->own_addr, dst_addr, ETH_ALEN) != 0)
		return;
	wpa_ft_rrb_oui_rx(hapd->wpa_auth, src_addr, dst_addr, oui_suffix, buf,
			  len);
}


static int hostapd_wpa_auth_add_tspec(void *ctx, const u8 *sta_addr,
				      u8 *tspec_ie, size_t tspec_ielen)
{
	struct hostapd_data *hapd = ctx;
	return hostapd_add_tspec(hapd, sta_addr, tspec_ie, tspec_ielen);
}



static int hostapd_wpa_register_ft_oui(struct hostapd_data *hapd,
				       const char *ft_iface)
{
	hapd->oui_pull = eth_p_oui_register(hapd, ft_iface,
					    FT_PACKET_R0KH_R1KH_PULL,
					    hostapd_rrb_oui_receive, hapd);
	if (!hapd->oui_pull)
		return -1;

	hapd->oui_resp = eth_p_oui_register(hapd, ft_iface,
					    FT_PACKET_R0KH_R1KH_RESP,
					    hostapd_rrb_oui_receive, hapd);
	if (!hapd->oui_resp)
		return -1;

	hapd->oui_push = eth_p_oui_register(hapd, ft_iface,
					    FT_PACKET_R0KH_R1KH_PUSH,
					    hostapd_rrb_oui_receive, hapd);
	if (!hapd->oui_push)
		return -1;

	hapd->oui_sreq = eth_p_oui_register(hapd, ft_iface,
					    FT_PACKET_R0KH_R1KH_SEQ_REQ,
					    hostapd_rrb_oui_receive, hapd);
	if (!hapd->oui_sreq)
		return -1;

	hapd->oui_sresp = eth_p_oui_register(hapd, ft_iface,
					     FT_PACKET_R0KH_R1KH_SEQ_RESP,
					     hostapd_rrb_oui_receive, hapd);
	if (!hapd->oui_sresp)
		return -1;

	return 0;
}


static void hostapd_wpa_unregister_ft_oui(struct hostapd_data *hapd)
{
	eth_p_oui_unregister(hapd->oui_pull);
	hapd->oui_pull = NULL;
	eth_p_oui_unregister(hapd->oui_resp);
	hapd->oui_resp = NULL;
	eth_p_oui_unregister(hapd->oui_push);
	hapd->oui_push = NULL;
	eth_p_oui_unregister(hapd->oui_sreq);
	hapd->oui_sreq = NULL;
	eth_p_oui_unregister(hapd->oui_sresp);
	hapd->oui_sresp = NULL;
}
#endif /* CONFIG_IEEE80211R_AP */


int hostapd_setup_wpa(struct hostapd_data *hapd)
{
	struct wpa_auth_config _conf;
	static const struct wpa_auth_callbacks cb = {
		.logger = hostapd_wpa_auth_logger,
		.disconnect = hostapd_wpa_auth_disconnect,
		.mic_failure_report = hostapd_wpa_auth_mic_failure_report,
		.psk_failure_report = hostapd_wpa_auth_psk_failure_report,
		.set_eapol = hostapd_wpa_auth_set_eapol,
		.get_eapol = hostapd_wpa_auth_get_eapol,
		.get_psk = hostapd_wpa_auth_get_psk,
		.get_msk = hostapd_wpa_auth_get_msk,
		.set_key = hostapd_wpa_auth_set_key,
		.get_seqnum = hostapd_wpa_auth_get_seqnum,
		.send_eapol = hostapd_wpa_auth_send_eapol,
		.for_each_sta = hostapd_wpa_auth_for_each_sta,
		.for_each_auth = hostapd_wpa_auth_for_each_auth,
		.send_ether = hostapd_wpa_auth_send_ether,
		.send_oui = hostapd_wpa_auth_send_oui,
#ifdef CONFIG_IEEE80211R_AP
		.send_ft_action = hostapd_wpa_auth_send_ft_action,
		.add_sta = hostapd_wpa_auth_add_sta,
		.add_tspec = hostapd_wpa_auth_add_tspec,
		.set_vlan = hostapd_wpa_auth_set_vlan,
		.get_vlan = hostapd_wpa_auth_get_vlan,
		.set_identity = hostapd_wpa_auth_set_identity,
		.get_identity = hostapd_wpa_auth_get_identity,
		.set_radius_cui = hostapd_wpa_auth_set_radius_cui,
		.get_radius_cui = hostapd_wpa_auth_get_radius_cui,
		.set_session_timeout = hostapd_wpa_auth_set_session_timeout,
		.get_session_timeout = hostapd_wpa_auth_get_session_timeout,
#endif /* CONFIG_IEEE80211R_AP */
	};
	const u8 *wpa_ie;
	size_t wpa_ie_len;

	hostapd_wpa_auth_conf(hapd->conf, hapd->iconf, &_conf);
	if (hapd->iface->drv_flags & WPA_DRIVER_FLAGS_EAPOL_TX_STATUS)
		_conf.tx_status = 1;
	if (hapd->iface->drv_flags & WPA_DRIVER_FLAGS_AP_MLME)
		_conf.ap_mlme = 1;
	hapd->wpa_auth = wpa_init(hapd->own_addr, &_conf, &cb, hapd);
	if (hapd->wpa_auth == NULL) {
		wpa_printf(MSG_ERROR, "WPA initialization failed.");
		return -1;
	}

	if (hostapd_set_privacy(hapd, 1)) {
		wpa_printf(MSG_ERROR, "Could not set PrivacyInvoked "
			   "for interface %s", hapd->conf->iface);
		return -1;
	}

	wpa_ie = wpa_auth_get_wpa_ie(hapd->wpa_auth, &wpa_ie_len);
	if (hostapd_set_generic_elem(hapd, wpa_ie, wpa_ie_len)) {
		wpa_printf(MSG_ERROR, "Failed to configure WPA IE for "
			   "the kernel driver.");
		return -1;
	}

	if (rsn_preauth_iface_init(hapd)) {
		wpa_printf(MSG_ERROR, "Initialization of RSN "
			   "pre-authentication failed.");
		return -1;
	}

#ifdef CONFIG_IEEE80211R_AP
	if (!hostapd_drv_none(hapd) &&
	    wpa_key_mgmt_ft(hapd->conf->wpa_key_mgmt)) {
		const char *ft_iface;

		ft_iface = hapd->conf->bridge[0] ? hapd->conf->bridge :
			   hapd->conf->iface;
		hapd->l2 = l2_packet_init(ft_iface, NULL, ETH_P_RRB,
					  hostapd_rrb_receive, hapd, 1);
		if (hapd->l2 == NULL &&
		    (hapd->driver == NULL ||
		     hapd->driver->send_ether == NULL)) {
			wpa_printf(MSG_ERROR, "Failed to open l2_packet "
				   "interface");
			return -1;
		}

		if (hostapd_wpa_register_ft_oui(hapd, ft_iface)) {
			wpa_printf(MSG_ERROR,
				   "Failed to open ETH_P_OUI interface");
			return -1;
		}
	}
#endif /* CONFIG_IEEE80211R_AP */

	return 0;

}


void hostapd_reconfig_wpa(struct hostapd_data *hapd)
{
	struct wpa_auth_config wpa_auth_conf;
	hostapd_wpa_auth_conf(hapd->conf, hapd->iconf, &wpa_auth_conf);
	wpa_reconfig(hapd->wpa_auth, &wpa_auth_conf);
}


void hostapd_deinit_wpa(struct hostapd_data *hapd)
{
	ieee80211_tkip_countermeasures_deinit(hapd);
	rsn_preauth_iface_deinit(hapd);
	if (hapd->wpa_auth) {
		wpa_deinit(hapd->wpa_auth);
		hapd->wpa_auth = NULL;

		if (hapd->drv_priv && hostapd_set_privacy(hapd, 0)) {
			wpa_printf(MSG_DEBUG, "Could not disable "
				   "PrivacyInvoked for interface %s",
				   hapd->conf->iface);
		}

		if (hapd->drv_priv &&
		    hostapd_set_generic_elem(hapd, (u8 *) "", 0)) {
			wpa_printf(MSG_DEBUG, "Could not remove generic "
				   "information element from interface %s",
				   hapd->conf->iface);
		}
	}
	ieee802_1x_deinit(hapd);

#ifdef CONFIG_IEEE80211R_AP
	eloop_cancel_timeout(hostapd_wpa_ft_rrb_rx_later, hapd, ELOOP_ALL_CTX);
	hostapd_wpa_ft_rrb_rx_later(hapd, NULL); /* flush without delivering */
	eloop_cancel_timeout(hostapd_oui_deliver_later, hapd, ELOOP_ALL_CTX);
	hostapd_oui_deliver_later(hapd, NULL); /* flush without delivering */
	l2_packet_deinit(hapd->l2);
	hapd->l2 = NULL;
	hostapd_wpa_unregister_ft_oui(hapd);
#endif /* CONFIG_IEEE80211R_AP */
}
